/**
 * Vita Music Assistant - Native audio player implementation
 */

#include "player/native_audio_player.hpp"
#include <borealis.hpp>
#include <cstring>
#include <cstdio>
#include <algorithm>

// dr_flac: single-header FLAC decoder (public domain / MIT-0). We drive it from
// callbacks over the pushed byte stream; no file IO or Ogg needed.
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

#ifdef __vita__
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#endif

namespace vita_ma {

// sceAudioOut plays a fixed number of frames per output call ("grain"). 1024
// frames @ 44100 Hz is ~23ms of audio.
static constexpr int GRAIN = 1024;
// Bail out if the encoded backlog explodes (output stalled / port dead) rather
// than growing memory without bound on a 256 MB device.
static constexpr size_t MAX_BACKLOG = 8 * 1024 * 1024;
// Decoded PCM ring capacity (~2s of interleaved stereo S16) and how much to
// prime before the first sceAudioOut call (~400ms). The prebuffer absorbs
// decode/network jitter so playback doesn't underrun.
static constexpr size_t PCM_CAP_SAMPLES  = 44100u * 2u * 2u;      // 2 s stereo
static constexpr size_t PCM_PREBUF_SAMPLES = (44100u * 2u * 2u) / 5u;  // ~400 ms

NativeAudioPlayer& NativeAudioPlayer::instance() {
    static NativeAudioPlayer inst;
    return inst;
}

NativeAudioPlayer::~NativeAudioPlayer() {
    stop();
}

void NativeAudioPlayer::startStream(const std::string& codec, int sampleRate,
                                    int channels, int bitDepth,
                                    const std::vector<uint8_t>& codecHeader) {
    std::lock_guard<std::mutex> ctrl(m_ctrlMutex);
    stopLocked();  // tear down any previous stream

    m_codec = codec;
    m_sampleRate = sampleRate > 0 ? sampleRate : 44100;
    m_channels = (channels == 1 || channels == 2) ? channels : 2;
    m_bitDepth = bitDepth > 0 ? bitDepth : 16;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_encoded.clear();
        m_readPos = 0;
        m_baseOffset = 0;
        // Seed the decoder with the container header (FLAC needs fLaC+STREAMINFO).
        std::vector<uint8_t> hdr = codecHeader;
        if (m_codec == "flac" && !hdr.empty()) {
            // dr_flac's native open requires the "fLaC" marker. Some servers send
            // only the STREAMINFO metadata block; prepend the magic if missing.
            bool hasMagic = hdr.size() >= 4 && hdr[0] == 'f' && hdr[1] == 'L' &&
                            hdr[2] == 'a' && hdr[3] == 'C';
            if (!hasMagic) {
                brls::Logger::info("NativeAudio: FLAC header missing fLaC magic, prepending");
                hdr.insert(hdr.begin(), {'f', 'L', 'a', 'C'});
            }
            // Log the first bytes for diagnosis.
            char hex[64] = {0};
            for (size_t i = 0; i < hdr.size() && i < 12; i++)
                snprintf(hex + i * 3, 4, "%02x ", hdr[i]);
            brls::Logger::info("NativeAudio: FLAC header[{}]: {}", hdr.size(), hex);
        }
        if (!hdr.empty()) {
            m_encoded.insert(m_encoded.end(), hdr.begin(), hdr.end());
        }
        m_headerLen = m_encoded.size();
    }
    {
        std::lock_guard<std::mutex> lock(m_pcmMutex);
        if (m_pcm.size() != PCM_CAP_SAMPLES) m_pcm.assign(PCM_CAP_SAMPLES, 0);
        m_pcmHead = 0;
        m_pcmCount = 0;
    }
    m_endOfStream.store(false);
    m_decodeDone.store(false);
    m_paused.store(false);
    m_playedFrames.store(0);
    m_running.store(true);

#ifdef __vita__
    // Give the decoder plenty of CPU headroom so it stays well ahead of
    // real-time (mirrors the player/QR scanner clock boost).
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
#endif

    brls::Logger::info("NativeAudio: starting {} {}Hz {}ch {}bit",
                       m_codec, m_sampleRate, m_channels, m_bitDepth);
    m_decodeThread = std::thread([this]() { decodeLoop(); });
    m_outputThread = std::thread([this]() { outputLoop(); });
}

void NativeAudioPlayer::pushAudio(const uint8_t* data, size_t len) {
    if (!m_running.load() || len == 0) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Compact the consumed prefix occasionally so the vector doesn't grow
        // forever while streaming.
        if (m_readPos > 1024 * 1024 && m_readPos * 2 > m_encoded.size()) {
            m_baseOffset += m_readPos;
            m_encoded.erase(m_encoded.begin(), m_encoded.begin() + m_readPos);
            m_readPos = 0;
        }
        if (m_encoded.size() - m_readPos > MAX_BACKLOG) {
            brls::Logger::warning("NativeAudio: backlog over cap, stopping");
            m_running.store(false);
        } else {
            m_encoded.insert(m_encoded.end(), data, data + len);
        }
    }
    m_cv.notify_one();
}

void NativeAudioPlayer::stop() {
    std::lock_guard<std::mutex> ctrl(m_ctrlMutex);
    stopLocked();
}

void NativeAudioPlayer::pause() {
    // Stall the output thread; hardware drains its last queued grain then goes
    // silent. Decoder + PCM ring are untouched so resume() is instant.
    m_paused.store(true);
}

void NativeAudioPlayer::resume() {
    m_paused.store(false);
    m_pcmCv.notify_all();
}

void NativeAudioPlayer::stopLocked() {
    m_running.store(false);
    m_endOfStream.store(true);
    m_cv.notify_all();
    m_pcmCv.notify_all();
    if (m_decodeThread.joinable()) m_decodeThread.join();
    if (m_outputThread.joinable()) m_outputThread.join();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_encoded.clear();
        m_readPos = 0;
        m_baseOffset = 0;
    }
    {
        std::lock_guard<std::mutex> lock(m_pcmMutex);
        m_pcmHead = 0;
        m_pcmCount = 0;
    }
    m_endOfStream.store(false);
    m_decodeDone.store(false);
    m_paused.store(false);
}

// --- PCM ring (decoder -> output) --------------------------------------------

void NativeAudioPlayer::pcmPush(const int16_t* src, size_t samples) {
    size_t off = 0;
    while (off < samples && m_running.load()) {
        std::unique_lock<std::mutex> lock(m_pcmMutex);
        m_pcmCv.wait(lock, [this]() {
            return m_pcmCount < PCM_CAP_SAMPLES || !m_running.load();
        });
        if (!m_running.load()) return;
        size_t space = PCM_CAP_SAMPLES - m_pcmCount;
        size_t n = std::min(space, samples - off);
        size_t tail = (m_pcmHead + m_pcmCount) % PCM_CAP_SAMPLES;
        size_t first = std::min(n, PCM_CAP_SAMPLES - tail);
        std::memcpy(&m_pcm[tail], src + off, first * sizeof(int16_t));
        if (n > first)
            std::memcpy(&m_pcm[0], src + off + first, (n - first) * sizeof(int16_t));
        m_pcmCount += n;
        off += n;
        lock.unlock();
        m_pcmCv.notify_all();
    }
}

size_t NativeAudioPlayer::pcmPop(int16_t* dst, size_t samples, bool& drained) {
    std::unique_lock<std::mutex> lock(m_pcmMutex);
    m_pcmCv.wait(lock, [this]() {
        return m_pcmCount > 0 || m_decodeDone.load() || !m_running.load();
    });
    if (!m_running.load()) { drained = true; return 0; }
    size_t n = std::min(samples, m_pcmCount);
    size_t first = std::min(n, PCM_CAP_SAMPLES - m_pcmHead);
    std::memcpy(dst, &m_pcm[m_pcmHead], first * sizeof(int16_t));
    if (n > first)
        std::memcpy(dst + first, &m_pcm[0], (n - first) * sizeof(int16_t));
    m_pcmHead = (m_pcmHead + n) % PCM_CAP_SAMPLES;
    m_pcmCount -= n;
    drained = (m_pcmCount == 0 && m_decodeDone.load());
    lock.unlock();
    m_pcmCv.notify_all();
    return n;
}

size_t NativeAudioPlayer::readEncoded(void* out, size_t bytes) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() {
        return (m_encoded.size() - m_readPos) > 0 || m_endOfStream.load() ||
               !m_running.load();
    });
    if (!m_running.load()) return 0;
    size_t avail = m_encoded.size() - m_readPos;
    if (avail == 0) return 0;  // end of stream, drained
    size_t n = bytes < avail ? bytes : avail;
    std::memcpy(out, m_encoded.data() + m_readPos, n);
    m_readPos += n;
    return n;
}

#ifdef __vita__
// dr_flac callbacks: pull from / reposition within the encoded ring.
static size_t drflacReadCb(void* pUserData, void* pBufferOut, size_t bytesToRead) {
    return static_cast<NativeAudioPlayer*>(pUserData)->readEncodedForCallback(pBufferOut, bytesToRead);
}
static drflac_bool32 drflacSeekCb(void* pUserData, int offset, drflac_seek_origin origin) {
    return static_cast<NativeAudioPlayer*>(pUserData)->seekForCallback(offset, (int)origin)
               ? DRFLAC_TRUE : DRFLAC_FALSE;
}
static drflac_bool32 drflacTellCb(void* pUserData, drflac_int64* pCursor) {
    int64_t c = 0;
    bool ok = static_cast<NativeAudioPlayer*>(pUserData)->tellForCallback(&c);
    if (pCursor) *pCursor = (drflac_int64)c;
    return ok ? DRFLAC_TRUE : DRFLAC_FALSE;
}
#endif

// Producer: decode encoded audio into interleaved stereo S16 and fill the PCM
// ring (blocks when the ring is full - natural backpressure). Never touches
// sceAudioOut, so a slow decode can't stall audio output.
void NativeAudioPlayer::decodeLoop() {
#ifdef __vita__
    std::vector<int16_t> stereo(GRAIN * 2, 0);

    if (m_codec == "flac") {
        // Wait for the first audio frame so dr_flac's header init has header +
        // frame data and never hits a premature short/EOF read.
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return m_encoded.size() > m_headerLen || m_endOfStream.load() ||
                       !m_running.load();
            });
        }
        if (m_running.load()) {
            drflac* flac = drflac_open(drflacReadCb, drflacSeekCb, drflacTellCb, this, nullptr);
            if (!flac) {
                brls::Logger::error("NativeAudio: drflac_open failed");
            } else {
                brls::Logger::info("NativeAudio: FLAC opened {}ch {}Hz",
                                   flac->channels, flac->sampleRate);
                std::vector<int16_t> dec(GRAIN * flac->channels, 0);
                while (m_running.load()) {
                    drflac_uint64 got = drflac_read_pcm_frames_s16(flac, GRAIN, dec.data());
                    if (got == 0) break;  // end of stream
                    for (drflac_uint64 i = 0; i < got; i++) {
                        if (flac->channels == 1) {
                            stereo[i * 2] = stereo[i * 2 + 1] = dec[i];
                        } else {
                            stereo[i * 2]     = dec[i * flac->channels];
                            stereo[i * 2 + 1] = dec[i * flac->channels + 1];
                        }
                    }
                    pcmPush(stereo.data(), (size_t)got * 2);
                }
                drflac_close(flac);
            }
        }
    } else {
        // Raw interleaved S16 PCM.
        const size_t grainBytes = (size_t)GRAIN * m_channels * sizeof(int16_t);
        std::vector<uint8_t> raw(grainBytes, 0);
        while (m_running.load()) {
            size_t filled = 0;
            while (filled < grainBytes && m_running.load()) {
                size_t n = readEncoded(raw.data() + filled, grainBytes - filled);
                if (n == 0) break;  // end of stream
                filled += n;
            }
            if (filled == 0) break;
            size_t frames = filled / (m_channels * sizeof(int16_t));
            const int16_t* src = reinterpret_cast<const int16_t*>(raw.data());
            for (size_t i = 0; i < frames; i++) {
                if (m_channels == 1) {
                    stereo[i * 2] = stereo[i * 2 + 1] = src[i];
                } else {
                    stereo[i * 2]     = src[i * m_channels];
                    stereo[i * 2 + 1] = src[i * m_channels + 1];
                }
            }
            pcmPush(stereo.data(), frames * 2);
        }
    }

    // Signal the output thread that no more PCM will be produced.
    m_decodeDone.store(true);
    m_pcmCv.notify_all();
    brls::Logger::info("NativeAudio: decode finished");
#endif
}

// Consumer: drain the PCM ring into sceAudioOut. Prebuffers first so playback
// starts with a cushion, then keeps the port fed a grain at a time; the
// blocking Output call paces it. Under-runs (decoder briefly behind) emit a
// grain of silence rather than a repeated/garbled buffer.
void NativeAudioPlayer::outputLoop() {
#ifdef __vita__
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, m_sampleRate,
                                   SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        brls::Logger::error("NativeAudio: sceAudioOutOpenPort failed: 0x{:08x}",
                            (unsigned)port);
        m_running.store(false);
        m_cv.notify_all();
        return;
    }

    // Prime the PCM ring before the first output so we start with slack.
    {
        std::unique_lock<std::mutex> lock(m_pcmMutex);
        m_pcmCv.wait(lock, [this]() {
            return m_pcmCount >= PCM_PREBUF_SAMPLES || m_decodeDone.load() ||
                   !m_running.load();
        });
    }

    std::vector<int16_t> outBuf(GRAIN * 2, 0);
    const size_t grainSamples = (size_t)GRAIN * 2;
    while (m_running.load()) {
        // Paused: stop feeding the hardware (it goes silent after its last
        // queued grain) but keep the stream alive until resume() or stop().
        if (m_paused.load()) {
            std::unique_lock<std::mutex> lock(m_pcmMutex);
            m_pcmCv.wait(lock, [this]() {
                return !m_paused.load() || !m_running.load();
            });
            if (!m_running.load()) break;
            continue;
        }
        bool drained = false;
        size_t got = pcmPop(outBuf.data(), grainSamples, drained);
        if (got == 0 && drained) break;  // stream finished and ring empty
        if (got < grainSamples) {
            // Zero-pad an underrun / final partial grain.
            std::memset(outBuf.data() + got, 0, (grainSamples - got) * sizeof(int16_t));
        }
        sceAudioOutOutput(port, outBuf.data());
        m_playedFrames.fetch_add(GRAIN);  // one grain = GRAIN stereo frames
        if (drained) break;
    }

    sceAudioOutReleasePort(port);
    m_running.store(false);
    m_cv.notify_all();  // wake a decoder that might still be blocked in readEncoded
    brls::Logger::info("NativeAudio: output stopped");
#else
    m_running.store(false);
#endif
}

// Thin forwarders so the free C callbacks can reach the private ring.
size_t NativeAudioPlayer::readEncodedForCallback(void* out, size_t bytes) {
    return readEncoded(out, bytes);
}

bool NativeAudioPlayer::tellForCallback(int64_t* cursor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (cursor) *cursor = (int64_t)(m_baseOffset + m_readPos);
    return true;
}

bool NativeAudioPlayer::seekForCallback(int offset, int origin) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // origin: 0 = SET (absolute), 1 = CUR (relative). END is unsupported on a
    // live stream.
    int64_t cur = (int64_t)(m_baseOffset + m_readPos);
    int64_t target = (origin == 1) ? cur + offset : offset;
    int64_t low = (int64_t)m_baseOffset;
    int64_t high = (int64_t)(m_baseOffset + m_encoded.size());
    if (target < low || target > high) return false;  // outside the buffered window
    m_readPos = (size_t)(target - low);
    return true;
}

} // namespace vita_ma
