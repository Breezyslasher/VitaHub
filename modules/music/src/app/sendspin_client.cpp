#include "app/sendspin_client.hpp"
#include "app/webrtc_client.hpp"
#include "app/ma_client.hpp"
#include "player/native_audio_player.hpp"
#include "app/application.hpp"
#include "app.h"
#include <borealis.hpp>
#include <cstring>
#include <sstream>
#include <chrono>
#include <thread>
#include <mbedtls/base64.h>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif

namespace vita_ma {

SendspinClient& SendspinClient::instance() {
    static SendspinClient instance;
    return instance;
}

bool SendspinClient::connect(const std::string& serverIp, int sendspinPort,
                              const std::string& clientId, const std::string& clientName) {
    // Plain local connection on the dedicated Sendspin port.
    std::string url = "ws://" + serverIp + ":" + std::to_string(sendspinPort) + "/sendspin";
    return connectUrl(url, clientId, clientName);
}

bool SendspinClient::connectUrl(const std::string& wsUrl,
                                 const std::string& clientId, const std::string& clientName) {
    disconnect();

    m_remoteMode = false;
    m_clientId = clientId;
    m_clientName = clientName;

    setState(SendspinState::CONNECTING);
    brls::Logger::info("Sendspin: connecting to {}", wsUrl);

    // Set up WebSocket callbacks
    m_ws.setOnMessage([this](const std::string& msg) {
        onTextMessage(msg);
    });
    m_ws.setOnBinary([this](const uint8_t* data, size_t size) {
        onBinaryData(data, size);
    });
    m_ws.setOnClose([this](int code, const std::string& reason) {
        onClose(code, reason);
    });

    // Connect to the Sendspin WebSocket endpoint.
    // No subprotocol - the Sendspin server doesn't use WebSocket subprotocols
    if (!m_ws.connect(wsUrl)) {
        brls::Logger::error("Sendspin: connection failed to {}", wsUrl);
        setState(SendspinState::ERROR);
        return false;
    }

    // Send the client/hello handshake
    setState(SendspinState::HANDSHAKING);
    sendClientHello();

    return true;
}

bool SendspinClient::connectRemote(const std::string& clientId, const std::string& clientName) {
    disconnect();

    m_remoteMode = true;
    m_clientId = clientId;
    m_clientName = clientName;

    setState(SendspinState::CONNECTING);
    brls::Logger::info("Sendspin: connecting via remote-access data channel");

    WebRTCClient& rtc = WebRTCClient::instance();
    rtc.setSendspinTextCallback([this](const std::string& msg) {
        onTextMessage(msg);
    });
    rtc.setSendspinBinaryCallback([this](const uint8_t* data, size_t size) {
        onBinaryData(data, size);
    });
    // Handshake when the channel is open (fires immediately if it already is)
    rtc.setSendspinOpenCallback([this]() {
        brls::Logger::info("Sendspin: remote channel open, sending hello");
        setState(SendspinState::HANDSHAKING);
        sendClientHello();
    });

    return true;
}

bool SendspinClient::sendRaw(const std::string& text) {
    if (m_remoteMode) {
        return WebRTCClient::instance().sendSendspinText(text);
    }
    return m_ws.send(text);
}

void SendspinClient::disconnect() {
    if (m_state.load() == SendspinState::DISCONNECTED) return;

    brls::Logger::info("Sendspin: disconnecting");
    if (m_remoteMode) {
        // The data channel belongs to the shared remote-access session
        // (owned by MAClient); just stop listening on it.
        WebRTCClient& rtc = WebRTCClient::instance();
        rtc.setSendspinTextCallback(nullptr);
        rtc.setSendspinBinaryCallback(nullptr);
        rtc.setSendspinOpenCallback(nullptr);
        m_remoteMode = false;
    } else {
        m_ws.disconnect();
    }
    NativeAudioPlayer::instance().stop();
    setState(SendspinState::DISCONNECTED);
}

void SendspinClient::stopStream() {
    NativeAudioPlayer::instance().stop();
    if (m_state.load() == SendspinState::STREAMING ||
        m_state.load() == SendspinState::BUFFERING) {
        setState(SendspinState::CONNECTED);
    }
}

void SendspinClient::sendClientHello() {
    // Build client/hello message per Sendspin protocol spec
    // We declare ourselves as a player that supports FLAC and PCM
    Json msg;
    msg["type"] = Json("client/hello");

    Json payload;
    payload["client_id"] = Json(m_clientId);
    payload["name"] = Json(m_clientName);
    payload["version"] = Json(1);

    // Device info so the Vita is identified properly in Music Assistant
    Json deviceInfo;
    deviceInfo["product_name"] = Json(std::string("PS Vita"));
    deviceInfo["manufacturer"] = Json(std::string("Sony"));
    deviceInfo["software_version"] = Json(std::string("Vita Music Assistant 2.0.0"));
    payload["device_info"] = deviceInfo;

    // Supported roles - we are a player
    Json roles(Json::ARRAY);
    roles.push_back(Json("player@v1"));
    payload["supported_roles"] = roles;

    // Player support - declare supported audio formats
    Json playerSupport;

    Json formats(Json::ARRAY);

    // FLAC - lossless, preferred for local network
    Json flacFmt;
    flacFmt["codec"] = Json("flac");
    flacFmt["channels"] = Json(2);
    flacFmt["sample_rate"] = Json(44100);
    flacFmt["bit_depth"] = Json(16);
    formats.push_back(flacFmt);

    // PCM fallback
    Json pcmFmt;
    pcmFmt["codec"] = Json("pcm");
    pcmFmt["channels"] = Json(2);
    pcmFmt["sample_rate"] = Json(44100);
    pcmFmt["bit_depth"] = Json(16);
    formats.push_back(pcmFmt);

    playerSupport["supported_formats"] = formats;
    playerSupport["buffer_capacity"] = Json(4 * 1024 * 1024); // 4MB buffer
    Json cmds(Json::ARRAY);
    cmds.push_back(Json("volume"));
    cmds.push_back(Json("mute"));
    playerSupport["supported_commands"] = cmds;

    payload["player@v1_support"] = playerSupport;
    msg["payload"] = payload;

    brls::Logger::info("Sendspin: sending client/hello as '{}'", m_clientName);
    sendRaw(msg.dump());
}

void SendspinClient::onTextMessage(const std::string& message) {
    if (message.empty() || message[0] != '{') return;

    Json msg = Json::parse(message);
    if (!msg.has("type")) return;

    std::string type = msg["type"].str();

    if (type == "server/hello") {
        // Handshake complete - we are now registered as a player
        Json payload = msg.has("payload") ? msg["payload"] : msg;
        std::string serverName = payload.has("name") ? payload["name"].str() : "unknown";
        brls::Logger::info("Sendspin: registered with server '{}'", serverName);

        // Send initial player state
        Json stateMsg;
        stateMsg["type"] = Json("client/state");
        Json statePayload;
        statePayload["state"] = Json("synchronized");
        Json playerState;
        playerState["volume"] = Json(100);
        playerState["muted"] = Json(false);
        // Fixed output latency the server should compensate for: our audio
        // path buffers audio in the native decoder before it reaches the
        // speakers. Reported so the server can align playout timing.
        playerState["static_delay_ms"] = Json(500);
        statePayload["player"] = playerState;
        stateMsg["payload"] = statePayload;
        sendRaw(stateMsg.dump());

        setState(SendspinState::CONNECTED);

        // Store the player ID for use with play_media
        brls::sync([this]() {
            App::instance().setPlayerId(m_clientId);
        });
    }
    else if (type == "stream/start") {
        // Server is starting to stream audio to us
        Json payload = msg.has("payload") ? msg["payload"] : msg;
        Json player = payload.has("player") ? payload["player"] : payload;

        if (player.has("codec")) m_format.codec = player["codec"].str();
        if (player.has("sample_rate")) m_format.sample_rate = player["sample_rate"].intVal();
        if (player.has("channels")) m_format.channels = player["channels"].intVal();
        if (player.has("bit_depth")) m_format.bit_depth = player["bit_depth"].intVal();

        // Extract codec_header if provided (base64-encoded container header)
        // For FLAC this contains the fLaC magic + STREAMINFO metadata block
        // the decoder needs for format detection.
        m_format.codec_header.clear();
        if (player.has("codec_header")) {
            std::string b64 = player["codec_header"].str();
            if (!b64.empty()) {
                size_t decoded_len = 0;
                // First call to get required output size
                mbedtls_base64_decode(nullptr, 0, &decoded_len,
                    reinterpret_cast<const unsigned char*>(b64.c_str()), b64.size());
                if (decoded_len > 0) {
                    m_format.codec_header.resize(decoded_len);
                    size_t actual_len = 0;
                    int ret = mbedtls_base64_decode(m_format.codec_header.data(), decoded_len, &actual_len,
                        reinterpret_cast<const unsigned char*>(b64.c_str()), b64.size());
                    if (ret == 0) {
                        m_format.codec_header.resize(actual_len);
                        brls::Logger::info("Sendspin: got codec_header ({} bytes)", actual_len);
                    } else {
                        brls::Logger::error("Sendspin: failed to decode codec_header (ret={})", ret);
                        m_format.codec_header.clear();
                    }
                }
            }
        }

        // If no codec_header was provided for FLAC, synthesize one.
        // The decoder needs the fLaC magic + STREAMINFO block for format probing.
        if (m_format.codec == "flac" && m_format.codec_header.empty()) {
            m_format.codec_header = buildFlacHeader(
                m_format.sample_rate, m_format.channels, m_format.bit_depth);
            brls::Logger::info("Sendspin: synthesized FLAC header ({} bytes)", m_format.codec_header.size());
        }

        brls::Logger::info("Sendspin: stream/start - {} {}Hz {}ch {}bit (header={}B)",
            m_format.codec, m_format.sample_rate, m_format.channels, m_format.bit_depth,
            m_format.codec_header.size());

        // Decide the audio path for this stream up front. Local playback now
        // always decodes natively (dr_flac + sceAudioOut); when local playback
        // is off we simply don't play audio locally.
        m_localPlayback = Application::getInstance().getSettings().localPlayback;

        if (m_localPlayback) {
            // Native path: decode + output directly.
            NativeAudioPlayer::instance().startStream(
                m_format.codec, m_format.sample_rate, m_format.channels,
                m_format.bit_depth, m_format.codec_header);
        }

        setState(SendspinState::BUFFERING);
        m_audioChunkCount = 0;
    }
    else if (type == "stream/end") {
        brls::Logger::info("Sendspin: stream/end");
        // Tear down immediately rather than draining the buffered backlog.
        // The server pushes audio faster than real time, so several seconds can
        // be buffered ahead; stream/end is sent on pause/stop, and playing that
        // backlog out would delay the pause by seconds. Because Sendspin text
        // messages are dispatched on the UI thread, a drain-and-join here would
        // also freeze the UI for that whole time. stop() discards the buffers
        // and exits the audio threads at once; on resume the server restreams
        // from the paused position with a fresh stream/start.
        if (m_localPlayback) {
            NativeAudioPlayer::instance().stop();
        }

        if (m_state.load() == SendspinState::STREAMING ||
            m_state.load() == SendspinState::BUFFERING) {
            setState(SendspinState::CONNECTED);
        }
    }
    else if (type == "stream/clear") {
        brls::Logger::info("Sendspin: stream/clear - clearing buffers");
        if (m_localPlayback) {
            NativeAudioPlayer::instance().stop();
        }
    }
    else if (type == "server/time") {
        // We do not do sample-accurate synchronized playout (audio is played
        // best-effort through the native decoder), so the time channel
        // isn't used. It's optional in the protocol; ignore it.
    }
    else if (type == "server/command") {
        // Server command (e.g., volume change)
        Json payload = msg.has("payload") ? msg["payload"] : msg;
        if (payload.has("player")) {
            Json playerCmd = payload["player"];
            if (playerCmd.has("volume")) {
                int vol = playerCmd["volume"].intVal();
                brls::Logger::info("Sendspin: volume command: {}", vol);
            }
        }
    }
    else if (type == "group/update") {
        // Group update - ignore for now, Vita is a single player
    }
    else {
        brls::Logger::debug("Sendspin: unhandled message type: {}", type);
    }
}

void SendspinClient::onBinaryData(const uint8_t* data, size_t size) {
    // This runs on the WebSocket receive thread - must be thread-safe
    if (size < SENDSPIN_HEADER_SIZE) return;

    // Only process audio data after stream/start has been received.
    // Without this guard, stale audio from a previous server queue
    // leaks in and starts decoding before format info is available.
    auto state = m_state.load();
    if (state != SendspinState::BUFFERING && state != SendspinState::STREAMING) return;

    // Parse binary header: 1 byte type + 8 bytes timestamp (big-endian)
    uint8_t msgType = data[0];

    if (msgType == static_cast<uint8_t>(SendspinBinaryType::AUDIO_CHUNK)) {
        // Audio data - skip the 9-byte header
        const uint8_t* audioData = data + SENDSPIN_HEADER_SIZE;
        size_t audioSize = size - SENDSPIN_HEADER_SIZE;

        if (audioSize > 0) {
            m_audioChunkCount++;
            if (m_audioChunkCount <= 3) {
                brls::Logger::debug("Sendspin: audio chunk #{} ({} bytes)", m_audioChunkCount, audioSize);
            }

            if (m_localPlayback) {
                // Native path: hand encoded bytes straight to the decoder.
                NativeAudioPlayer::instance().pushAudio(audioData, audioSize);
            }

            // Transition from buffering to streaming
            if (m_state.load() == SendspinState::BUFFERING) {
                setState(SendspinState::STREAMING);
            }
        }
    }
    // Ignore artwork and visualization data for now
}

void SendspinClient::onClose(int code, const std::string& reason) {
    brls::Logger::info("Sendspin: connection closed ({}: {})", code, reason);
    NativeAudioPlayer::instance().stop();
    setState(SendspinState::DISCONNECTED);
}

void SendspinClient::setState(SendspinState state) {
    m_state.store(state);
    if (m_stateCallback) {
        brls::sync([this, state]() {
            m_stateCallback(state);
        });
    }
}

std::vector<uint8_t> SendspinClient::buildFlacHeader(int sampleRate, int channels, int bitDepth) {
    // Build a minimal FLAC file header: fLaC magic + STREAMINFO metadata block.
    // STREAMINFO is 34 bytes of data, preceded by a 4-byte metadata block header.
    // Total: 4 (magic) + 4 (block header) + 34 (STREAMINFO data) = 42 bytes.
    std::vector<uint8_t> header(42, 0);

    // "fLaC" magic
    header[0] = 'f'; header[1] = 'L'; header[2] = 'a'; header[3] = 'C';

    // Metadata block header: bit 7 = last-metadata-block flag (1), bits 6-0 = type (0 = STREAMINFO)
    // Byte 4: 0x80 = last block, type 0
    header[4] = 0x80;
    // Bytes 5-7: block data length = 34 (big-endian 24-bit)
    header[5] = 0x00;
    header[6] = 0x00;
    header[7] = 34;

    // STREAMINFO data (34 bytes starting at offset 8):
    // Bytes 0-1: minimum block size (samples) - use 4096 as default
    header[8] = 0x10; header[9] = 0x00;  // 4096
    // Bytes 2-3: maximum block size (samples) - use 4096 as default
    header[10] = 0x10; header[11] = 0x00; // 4096
    // Bytes 4-6: minimum frame size (0 = unknown)
    header[12] = 0; header[13] = 0; header[14] = 0;
    // Bytes 7-9: maximum frame size (0 = unknown)
    header[15] = 0; header[16] = 0; header[17] = 0;

    // Bytes 10-13 (+ 4 bits of 14): sample rate (20 bits), channels-1 (3 bits), bps-1 (5 bits), total samples high (4 bits)
    // Bits: ssssssss ssssssss ssssrrrr rbbbbbttt
    // Wait, the layout is:
    //   20 bits: sample rate
    //   3 bits:  channels - 1
    //   5 bits:  bits per sample - 1
    //   36 bits: total samples (0 = unknown)
    uint32_t sr = static_cast<uint32_t>(sampleRate);
    uint32_t ch = static_cast<uint32_t>(channels - 1) & 0x07;
    uint32_t bps = static_cast<uint32_t>(bitDepth - 1) & 0x1F;

    // Pack into bytes 18-21 (offsets 10-13 within STREAMINFO)
    header[18] = (sr >> 12) & 0xFF;
    header[19] = (sr >> 4) & 0xFF;
    header[20] = ((sr & 0x0F) << 4) | (ch << 1) | ((bps >> 4) & 0x01);
    header[21] = ((bps & 0x0F) << 4); // total samples high 4 bits = 0

    // Bytes 22-25: total samples low 32 bits = 0 (unknown)
    // Bytes 26-41: MD5 signature = all zeros (unknown)

    return header;
}

} // namespace vita_ma
