#pragma once

/**
 * Vita Music Assistant - Music Assistant Remote Access client (WebRTC)
 *
 * Implements genuine MA remote access on the Vita using libdatachannel
 * (ICE via libjuice, DTLS via mbedTLS, SCTP data channels via usrsctp).
 *
 * Protocol (verified against music-assistant/signaling-server and the MA
 * server's controllers/webserver/remote_access/gateway.py):
 *
 *  1. Connect to wss://signaling.music-assistant.io/ws and send
 *     {"type":"connect-request","remoteId":"<26-char base32 id>"}.
 *  2. Receive {"type":"connected","sessionId"} then
 *     {"type":"session-ready","sessionId","iceServers":[...]}.
 *  3. Open an RTCPeerConnection with those ICE servers, create the data
 *     channels, and exchange {"type":"offer"/"answer"/"ice-candidate",
 *     "sessionId","data":{...}} through the signaling socket.
 *  4. Data channels bridged by the MA gateway:
 *       "ma-api"   -> the server's local WS API (/ws) - normal MA protocol
 *       "sendspin" -> the server's local Sendspin endpoint (:8927) - audio
 *     HTTP assets (imageproxy) are tunneled over the ma-api channel as
 *     {"type":"http-proxy-request","id","method","path","headers"} ->
 *     {"type":"http-proxy-response","id","status","headers","body"(hex)}.
 *
 * MAClient and SendspinClient route their traffic through this class when
 * connected via a Remote ID; the rest of the app is unchanged.
 */

#include "app/websocket_client.hpp"
#include "app/ma_client.hpp"
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <condition_variable>

// Forward declarations so app headers don't pull in <rtc/rtc.hpp>
namespace rtc {
class PeerConnection;
class DataChannel;
}

namespace vita_ma {

enum class WebRTCState {
    DISCONNECTED,
    CONNECTING_SIGNALING,  // Connecting to signaling server
    SIGNALING_CONNECTED,   // connect-request sent, waiting for session
    NEGOTIATING,           // Peer connection created, SDP/ICE in progress
    CONNECTED,             // ma-api data channel open
    ERROR
};

// Remote access info from the MA server (remote_access/info command)
struct RemoteAccessInfo {
    bool enabled = false;
    bool connected = false;
    std::string remoteId;       // 26-char base32 id (cert fingerprint derived)
    std::string signalingUrl;   // wss://signaling.music-assistant.io/ws
};

using WebRTCStateCallback = std::function<void(WebRTCState state, const std::string& detail)>;

class WebRTCClient {
public:
    static WebRTCClient& instance();

    // Establish a remote connection to the MA instance registered under
    // remoteId. Data flows via callbacks/send methods below once CONNECTED.
    bool connectRemote(const std::string& remoteId);
    void disconnect();

    WebRTCState getState() const { return m_state.load(); }
    bool isConnected() const { return m_state.load() == WebRTCState::CONNECTED; }
    // Human-readable detail of the most recent failure (e.g. the signaling
    // server's error text), for surfacing in the UI.
    std::string getLastError() {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_lastError;
    }

    // --- ma-api channel (MA WebSocket API messages) ---
    bool sendApi(const std::string& message);
    void setApiMessageCallback(std::function<void(const std::string&)> cb) {
        m_apiMessageCb = std::move(cb);
    }

    // --- sendspin channel (Sendspin player protocol) ---
    bool sendSendspinText(const std::string& message);
    bool sendSendspinBinary(const uint8_t* data, size_t size);
    void setSendspinTextCallback(std::function<void(const std::string&)> cb) {
        m_sendspinTextCb = std::move(cb);
    }
    void setSendspinBinaryCallback(std::function<void(const uint8_t*, size_t)> cb) {
        m_sendspinBinaryCb = std::move(cb);
    }
    // Fired (possibly immediately) when the sendspin channel is open.
    void setSendspinOpenCallback(std::function<void()> cb);

    void setStateCallback(WebRTCStateCallback cb) { m_stateCallback = std::move(cb); }
    // Fired when an established connection is lost (for reconnect logic).
    void setClosedCallback(std::function<void()> cb) { m_closedCb = std::move(cb); }

    // --- HTTP tunneling over the ma-api channel ---
    // Blocking fetch (call from a background thread). path includes the query,
    // e.g. "/imageproxy?path=...&size=256". Body is returned decoded (bytes).
    bool httpProxyFetch(const std::string& method, const std::string& path,
                        int timeoutMs, int& statusOut, std::string& bodyOut);

    // True if the given server string looks like a Remote ID rather than a
    // URL/hostname. The canonical id is a 26-char base32 string (derived from
    // the server's certificate fingerprint); display grouping with hyphens and
    // pasted app URLs (?remote_id=...) are accepted too.
    static bool isRemoteId(const std::string& s);
    // Canonicalize user input: extract from a pasted URL, strip hyphen/space
    // grouping and uppercase when the result matches the 26-char base32 shape.
    static std::string normalizeRemoteId(const std::string& s);

    // Ask the signaling server whether a Remote ID is currently registered
    // (GET /api/check/:remoteId). Async; cb delivered on the UI thread.
    static void checkRemoteOnline(const std::string& remoteId,
                                  std::function<void(bool reachable, bool online)> cb);

    // Get remote access info from a locally-connected MA server
    static void fetchRemoteAccessInfo(MAClient& client,
        std::function<void(bool success, const RemoteAccessInfo& info)> cb);

private:
    WebRTCClient() = default;
    ~WebRTCClient() = default;

    // Signaling
    WebSocketClient m_signalingWs;
    std::string m_remoteId;
    std::string m_sessionId;
    std::string m_signalingUrl = "wss://signaling.music-assistant.io/ws";
    void onSignalingMessage(const std::string& message);
    bool sendSignaling(const Json& msg);

    // True while a connectRemote() attempt is running (guards double-press)
    std::atomic<bool> m_connectInFlight{false};

    // connectRemote() blocks on this until the handshake reaches a terminal
    // state (CONNECTED / ERROR / DISCONNECTED); setState() notifies.
    std::mutex m_stateMutex;
    std::condition_variable m_stateCv;
    std::string m_lastError;  // guarded by m_stateMutex

    // Peer connection / channels (guarded by m_rtcMutex)
    std::mutex m_rtcMutex;
    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::DataChannel> m_apiChannel;
    std::shared_ptr<rtc::DataChannel> m_sendspinChannel;
    bool m_sendspinOpen = false;
    void startPeerConnection(const Json& iceServersJson);
    void teardownPeerConnection();

    // Callbacks
    std::function<void(const std::string&)> m_apiMessageCb;
    std::function<void(const std::string&)> m_sendspinTextCb;
    std::function<void(const uint8_t*, size_t)> m_sendspinBinaryCb;
    std::function<void()> m_sendspinOpenCb;
    std::function<void()> m_closedCb;
    WebRTCStateCallback m_stateCallback;

    // http-proxy correlation
    struct ProxyPending {
        bool done = false;
        int status = 0;
        std::string body;  // decoded bytes
    };
    std::mutex m_proxyMutex;
    std::condition_variable m_proxyCv;
    std::map<std::string, std::shared_ptr<ProxyPending>> m_proxyPending;
    std::atomic<int> m_proxyIdCounter{0};
    // Returns true if the message was an http-proxy-response and was consumed.
    bool handleHttpProxyResponse(const std::string& message);

    std::atomic<WebRTCState> m_state{WebRTCState::DISCONNECTED};
    void setState(WebRTCState state, const std::string& detail = "");
    void onApiChannelMessage(const std::string& message);
};

} // namespace vita_ma
