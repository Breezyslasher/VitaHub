#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

namespace vita_ma {

// WebSocket frame opcodes
enum class WsOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

// WebSocket connection state
enum class WsState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    CLOSING
};

// Callback types
using WsMessageCallback = std::function<void(const std::string& message)>;
using WsBinaryCallback = std::function<void(const uint8_t* data, size_t size)>;
using WsErrorCallback = std::function<void(const std::string& error)>;
using WsCloseCallback = std::function<void(int code, const std::string& reason)>;

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    // Connect to a WebSocket server
    // url format: ws://host:port/path or wss://host:port/path
    // subprotocol: optional WebSocket subprotocol (e.g. "json")
    bool connect(const std::string& url, const std::string& subprotocol = "");
    void disconnect();

    // Send a text message
    bool send(const std::string& message);

    // State
    WsState getState() const { return m_state.load(); }
    bool isConnected() const { return m_state.load() == WsState::CONNECTED; }

    // Callbacks
    void setOnMessage(WsMessageCallback cb) { m_onMessage = std::move(cb); }
    void setOnBinary(WsBinaryCallback cb) { m_onBinary = std::move(cb); }
    void setOnError(WsErrorCallback cb) { m_onError = std::move(cb); }
    void setOnClose(WsCloseCallback cb) { m_onClose = std::move(cb); }

    // By default text/close callbacks are marshalled to the UI thread via
    // brls::sync. Disable for callbacks that don't touch the UI and must not
    // depend on the main loop running (e.g. WebRTC signaling: session restore
    // blocks the main thread before the first mainLoop() iteration, so a
    // brls::sync'd pairing message would deadlock until the 60s timeout).
    void setDispatchOnMainThread(bool enable) { m_dispatchOnMain = enable; }

private:
    void receiveLoop();
    bool performHandshake(const std::string& host, const std::string& path, int port, const std::string& subprotocol);
    bool sendFrame(WsOpcode opcode, const uint8_t* data, size_t len);
    bool readFrame(std::string& payload, WsOpcode& opcode);
    std::string generateKey();

    // Socket
    int m_socket = -1;
    void* m_sslCtx = nullptr;    // mbedtls_ssl_context*
    void* m_sslConf = nullptr;   // mbedtls_ssl_config*
    void* m_ctrDrbg = nullptr;   // mbedtls_ctr_drbg_context*
    void* m_entropy = nullptr;   // mbedtls_entropy_context*
    void* m_netCtx = nullptr;    // mbedtls_net_context*
    bool m_useSsl = false;
    std::string m_subprotocol;

    // Thread
    std::thread m_receiveThread;
    std::atomic<WsState> m_state{WsState::DISCONNECTED};
    std::mutex m_sendMutex;
    // Serializes every mbedtls_ssl_read/_write on the shared TLS context -
    // mbedTLS contexts are not thread-safe, and the receive thread reads while
    // other threads send. The socket has a short receive timeout so the reader
    // releases this lock periodically instead of blocking inside TLS forever.
    std::mutex m_sslMutex;

    // Callbacks
    WsMessageCallback m_onMessage;
    WsBinaryCallback m_onBinary;
    WsErrorCallback m_onError;
    WsCloseCallback m_onClose;
    bool m_dispatchOnMain = true;

    // Low-level I/O
    int rawSend(const uint8_t* data, size_t len);
    int rawRecv(uint8_t* data, size_t len);
    // Read exactly len bytes, retrying on receive timeouts (rawRecv() == 0)
    // until the socket is disconnected. Returns false on error/close.
    bool recvExact(uint8_t* data, size_t len);
    void cleanupSocket();
};

} // namespace vita_ma
