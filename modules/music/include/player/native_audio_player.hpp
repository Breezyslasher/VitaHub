/**
 * Vita Music Assistant - Native audio player
 *
 * Decodes Sendspin audio (FLAC via dr_flac, or raw PCM) and plays it directly
 * through sceAudioOut, bypassing the local HTTP server + mpv path. Lighter and
 * lower-latency; selected by the "native audio" setting.
 *
 * Threading: Sendspin's receive thread calls pushAudio() to feed encoded bytes
 * into a ring; a single output thread pulls from that ring (blocking), decodes,
 * and writes PCM to sceAudioOut (whose output call blocks for real-time pacing).
 */

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace vita_ma {

class NativeAudioPlayer {
public:
    static NativeAudioPlayer& instance();

    // Begin a new stream. codec is "flac" or "pcm". codecHeader (the FLAC
    // fLaC+STREAMINFO bytes) is fed to the decoder first so it can initialize.
    void startStream(const std::string& codec, int sampleRate, int channels,
                     int bitDepth, const std::vector<uint8_t>& codecHeader);

    // Push encoded (FLAC) or raw interleaved little-endian S16 (PCM) bytes.
    void pushAudio(const uint8_t* data, size_t len);

    // Hard stop: tear down output immediately, discarding any buffered audio.
    void stop();

    // Pause/resume the audio output without tearing down the stream. The
    // output thread stops feeding sceAudioOut (hardware goes silent) but the
    // decoded PCM and decoder stay intact, so resume() continues instantly.
    void pause();
    void resume();

    bool isPlaying() const { return m_running.load(); }
    bool isPaused() const { return m_paused.load(); }

    // Seconds of audio actually sent to the output since the current stream
    // started (frozen while paused). Used to drive the player's seek bar.
    double positionSeconds() const {
        int sr = m_sampleRate > 0 ? m_sampleRate : 44100;
        return (double)m_playedFrames.load() / (double)sr;
    }

    // Forwarders for dr_flac's C callbacks (they only get a void* user
    // pointer). Public so the free functions in the .cpp can reach the ring.
    // Seek/tell operate on absolute stream positions so dr_flac can reposition
    // within the still-buffered window during open.
    size_t readEncodedForCallback(void* out, size_t bytes);
    bool seekForCallback(int offset, int origin);   // origin: 0=SET,1=CUR,2=END
    bool tellForCallback(int64_t* cursor);

private:
    NativeAudioPlayer() = default;
    ~NativeAudioPlayer();
    NativeAudioPlayer(const NativeAudioPlayer&) = delete;
    NativeAudioPlayer& operator=(const NativeAudioPlayer&) = delete;

    // Decode thread: pulls encoded bytes, decodes to interleaved stereo S16,
    // and fills the PCM ring (with backpressure). Output thread: drains the PCM
    // ring into sceAudioOut. Decoupling them means a slow decode or a late
    // network chunk never starves the audio hardware, as long as the PCM ring
    // stays primed (prebuffer) - the fix for stutter.
    void decodeLoop();
    void outputLoop();

    // Immediate teardown assuming m_ctrlMutex is already held. Public stop()/
    // startStream() take the mutex so a UI-thread flush can never race the
    // Sendspin thread's stream transitions on the same std::threads.
    void stopLocked();

    // Serializes startStream/stop across the Sendspin receive thread and any
    // caller (e.g. a UI-thread flush on skip).
    std::mutex m_ctrlMutex;

    // Blocking read from the encoded ring; returns bytes copied, or 0 at
    // end-of-stream / stop. Used as dr_flac's read callback and for raw PCM.
    size_t readEncoded(void* out, size_t bytes);

    // PCM ring helpers (interleaved stereo S16 samples).
    void pcmPush(const int16_t* src, size_t samples);         // producer, blocks if full
    size_t pcmPop(int16_t* dst, size_t samples, bool& drained); // consumer, blocks if empty

    std::thread m_decodeThread;
    std::thread m_outputThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};        // output stalled but stream intact
    std::atomic<bool> m_endOfStream{false};   // no more encoded data will arrive
    std::atomic<bool> m_decodeDone{false};    // decoder finished producing PCM
    std::atomic<uint64_t> m_playedFrames{0};  // frames sent to sceAudioOut this stream

    // Encoded byte queue (network -> decoder)
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<uint8_t> m_encoded;
    size_t m_readPos = 0;
    uint64_t m_baseOffset = 0;  // bytes dropped by compaction (absolute pos = base + readPos)
    size_t m_headerLen = 0;     // seed header length (defer open until a frame arrives)

    // Decoded PCM ring (decoder -> sceAudioOut), interleaved stereo S16
    std::mutex m_pcmMutex;
    std::condition_variable m_pcmCv;
    std::vector<int16_t> m_pcm;  // circular
    size_t m_pcmHead = 0;        // read index
    size_t m_pcmCount = 0;       // samples currently stored

    std::string m_codec;
    int m_sampleRate = 44100;
    int m_channels = 2;
    int m_bitDepth = 16;
};

} // namespace vita_ma
