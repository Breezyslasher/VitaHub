#pragma once

#include "app/websocket_client.hpp"
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>

namespace vita_ma {

class Json;  // defined in app/ma_client.hpp (included by the .cpp)

// Sendspin is Music Assistant's native streaming protocol.
// The Vita connects to the MA server's Sendspin port (8927) and registers
// as a player. The server then streams audio data over WebSocket binary
// frames when playback is triggered via player_queues/play_media.
//
// Protocol flow:
// 1. Client connects to ws://server_ip:8927/sendspin
// 2. Client sends client/hello with player role and supported formats
// 3. Server responds with server/hello - client is now a registered player
// 4. User triggers playback via player_queues/play_media with this player's ID
// 5. Server sends stream/start with audio format info
// 6. Server sends binary audio frames (9-byte header + audio data)
// 7. Audio data is served to MPV via a local HTTP server for real-time playback

enum class SendspinState {
    DISCONNECTED,
    CONNECTING,
    HANDSHAKING,    // Waiting for server/hello
    CONNECTED,      // Registered as player, waiting for stream
    BUFFERING,      // Receiving audio data, streaming to MPV
    STREAMING,      // MPV is actively playing
    PAUSED,
    ERROR
};

// Audio format info from Sendspin stream/start message
struct SendspinAudioFormat {
    std::string codec;       // "pcm", "flac", "opus"
    int sample_rate = 44100;
    int channels = 2;
    int bit_depth = 16;
    std::vector<uint8_t> codec_header;  // Container header (e.g. fLaC + STREAMINFO for FLAC)
};

// Sendspin binary message types (from protocol spec)
enum class SendspinBinaryType : uint8_t {
    AUDIO_CHUNK = 4,
    ARTWORK_0 = 8,
    ARTWORK_1 = 9,
    ARTWORK_2 = 10,
    ARTWORK_3 = 11,
    VISUALIZATION = 16
};

// Binary frame header: 1 byte type + 8 bytes timestamp (big-endian int64)
static constexpr size_t SENDSPIN_HEADER_SIZE = 9;

// Maximum WebSocket frame payload size (16MB - prevents OOM on corrupted frames)
static constexpr size_t MAX_WS_FRAME_SIZE = 16 * 1024 * 1024;

// Callback for stream state changes
using StreamStateCallback = std::function<void(SendspinState state)>;
// Callback for stream metadata (track change, etc.)
using StreamMetadataCallback = std::function<void(const std::string& key, const std::string& value)>;

class SendspinClient {
public:
    static SendspinClient& instance();

    // Connect to the MA server's Sendspin port and register as a player.
    bool connect(const std::string& serverIp, int sendspinPort,
                 const std::string& clientId, const std::string& clientName);

    // Connect to Sendspin at an explicit ws:// or wss:// URL and register as a
    // player. Used for remote setups where the sendspin endpoint is reached
    // through a TLS reverse proxy (e.g. wss://host/sendspin) instead of the
    // plain local port 8927.
    bool connectUrl(const std::string& wsUrl,
                    const std::string& clientId, const std::string& clientName);

    // Register as a player over the MA remote-access WebRTC 'sendspin' data
    // channel (bridged by the server's gateway to its local Sendspin endpoint).
    // Used when MAClient is connected via a Remote ID.
    bool connectRemote(const std::string& clientId, const std::string& clientName);

    // Disconnect from Sendspin
    void disconnect();

    // State
    SendspinState getState() const { return m_state.load(); }
    bool isConnected() const {
        auto s = m_state.load();
        return s == SendspinState::CONNECTED || s == SendspinState::STREAMING
            || s == SendspinState::BUFFERING || s == SendspinState::PAUSED;
    }
    bool isStreaming() const { return m_state.load() == SendspinState::STREAMING; }
    SendspinAudioFormat getAudioFormat() const { return m_format; }

    // The player_id to use with player_queues/play_media
    const std::string& getPlayerId() const { return m_clientId; }

    // Callbacks
    void setStateCallback(StreamStateCallback cb) { m_stateCallback = std::move(cb); }
    void setMetadataCallback(StreamMetadataCallback cb) { m_metadataCallback = std::move(cb); }

    // Stop current stream playback (keeps connection alive)
    void stopStream();

private:
    SendspinClient() = default;

    WebSocketClient m_ws;
    bool m_remoteMode = false;  // true when using the WebRTC 'sendspin' channel
    bool sendRaw(const std::string& text);
    std::atomic<SendspinState> m_state{SendspinState::DISCONNECTED};
    SendspinAudioFormat m_format;
    std::string m_clientId;
    std::string m_clientName;

    size_t m_audioChunkCount = 0;  // Audio chunks received in current stream (for logging)
    // Whether the Vita plays this stream locally (mirrors the localPlayback
    // setting captured at stream/start). When true, audio decodes via
    // NativeAudioPlayer (dr_flac + sceAudioOut); when false the Vita is acting
    // as a remote controller and plays no local audio.
    bool m_localPlayback = false;

    // Callbacks
    StreamStateCallback m_stateCallback;
    StreamMetadataCallback m_metadataCallback;

    // Protocol handlers
    void onTextMessage(const std::string& message);
    void onBinaryData(const uint8_t* data, size_t size);
    void onClose(int code, const std::string& reason);
    void setState(SendspinState state);

    // Handshake
    void sendClientHello();

    // Build a minimal FLAC file header (fLaC + STREAMINFO) for format detection
    static std::vector<uint8_t> buildFlacHeader(int sampleRate, int channels, int bitDepth);
};

} // namespace vita_ma
