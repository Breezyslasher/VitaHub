#include "app/websocket_client.hpp"
#include <borealis.hpp>

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cerrno>
#include <thread>
#include <chrono>
#include <sys/time.h>

#include <sys/socket.h>
#include <fcntl.h>

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

namespace vita_ma {

// TLS receive BIO. Unlike mbedtls_net_recv, this maps EAGAIN/EWOULDBLOCK on
// the non-blocking socket to MBEDTLS_ERR_SSL_WANT_READ so mbedtls_ssl_read()
// returns immediately when no data is pending - required so the receive
// thread never blocks inside TLS holding the SSL lock (the context is shared
// with sender threads and is not thread-safe).
static int wsTlsRecv(void* ctx, unsigned char* buf, size_t len) {
    auto* net = static_cast<mbedtls_net_context*>(ctx);
    int ret = static_cast<int>(::recv(net->fd, buf, len, 0));
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

WebSocketClient::WebSocketClient() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

static void parseUrl(const std::string& url, std::string& host, std::string& path, int& port, bool& ssl) {
    ssl = false;
    port = 80;
    path = "/";

    std::string u = url;
    if (u.substr(0, 6) == "wss://") {
        ssl = true;
        port = 443;
        u = u.substr(6);
    } else if (u.substr(0, 5) == "ws://") {
        u = u.substr(5);
    }

    size_t pathPos = u.find('/');
    if (pathPos != std::string::npos) {
        path = u.substr(pathPos);
        u = u.substr(0, pathPos);
    }

    size_t colonPos = u.find(':');
    if (colonPos != std::string::npos) {
        host = u.substr(0, colonPos);
        port = std::atoi(u.substr(colonPos + 1).c_str());
    } else {
        host = u;
    }
}

std::string WebSocketClient::generateKey() {
    // Generate 16 random bytes, base64 encode
    uint8_t raw[16];
    for (int i = 0; i < 16; i++) {
        raw[i] = static_cast<uint8_t>(std::rand() % 256);
    }
    unsigned char b64[32];
    size_t olen = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &olen, raw, 16);
    return std::string(reinterpret_cast<char*>(b64), olen);
}

bool WebSocketClient::connect(const std::string& url, const std::string& subprotocol) {
    m_subprotocol = subprotocol;
    if (m_state.load() != WsState::DISCONNECTED) {
        disconnect();
    }

    m_state.store(WsState::CONNECTING);

    std::string host, path;
    int port;
    parseUrl(url, host, path, port, m_useSsl);

    brls::Logger::info("WS connecting to {}:{}{} (ssl={})", host, port, path, m_useSsl);

    if (m_useSsl) {
        // Initialize mbedTLS
        m_entropy = new mbedtls_entropy_context;
        m_ctrDrbg = new mbedtls_ctr_drbg_context;
        m_sslConf = new mbedtls_ssl_config;
        m_sslCtx = new mbedtls_ssl_context;
        m_netCtx = new mbedtls_net_context;

        auto* entropy = static_cast<mbedtls_entropy_context*>(m_entropy);
        auto* ctrDrbg = static_cast<mbedtls_ctr_drbg_context*>(m_ctrDrbg);
        auto* conf = static_cast<mbedtls_ssl_config*>(m_sslConf);
        auto* ssl = static_cast<mbedtls_ssl_context*>(m_sslCtx);
        auto* net = static_cast<mbedtls_net_context*>(m_netCtx);

        mbedtls_net_init(net);
        mbedtls_ssl_init(ssl);
        mbedtls_ssl_config_init(conf);
        mbedtls_ctr_drbg_init(ctrDrbg);
        mbedtls_entropy_init(entropy);

        if (mbedtls_ctr_drbg_seed(ctrDrbg, mbedtls_entropy_func, entropy, nullptr, 0) != 0) {
            brls::Logger::error("WS: mbedtls seed failed");
            cleanupSocket();
            m_state.store(WsState::DISCONNECTED);
            return false;
        }

        std::string portStr = std::to_string(port);
        if (mbedtls_net_connect(net, host.c_str(), portStr.c_str(), MBEDTLS_NET_PROTO_TCP) != 0) {
            brls::Logger::error("WS: TCP connect failed");
            cleanupSocket();
            m_state.store(WsState::DISCONNECTED);
            return false;
        }

        mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, ctrDrbg);
        mbedtls_ssl_setup(ssl, conf);
        mbedtls_ssl_set_hostname(ssl, host.c_str());
        mbedtls_ssl_set_bio(ssl, net, mbedtls_net_send, wsTlsRecv, nullptr);

        int ret;
        while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                brls::Logger::error("WS: SSL handshake failed: {}", ret);
                cleanupSocket();
                m_state.store(WsState::DISCONNECTED);
                return false;
            }
        }

        m_socket = net->fd;
    } else {
        // Plain TCP connection
#ifdef __vita__
        m_socket = sceNetSocket("ws", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
        if (m_socket < 0) {
            brls::Logger::error("WS: socket creation failed");
            m_state.store(WsState::DISCONNECTED);
            return false;
        }

        // Resolve hostname
        SceNetInAddr addr;
        memset(&addr, 0, sizeof(addr));

        // Try direct IP first
        if (sceNetInetPton(SCE_NET_AF_INET, host.c_str(), &addr) <= 0) {
            // Not an IP address, try DNS resolution
            int rid = sceNetResolverCreate("ws_resolver", NULL, 0);
            if (rid >= 0) {
                int ret = sceNetResolverStartNtoa(rid, host.c_str(), &addr, 0, 0, 0);
                sceNetResolverDestroy(rid);
                if (ret < 0) {
                    brls::Logger::error("WS: DNS resolve failed for {}", host);
                    sceNetSocketClose(m_socket);
                    m_socket = -1;
                    m_state.store(WsState::DISCONNECTED);
                    return false;
                }
            } else {
                brls::Logger::error("WS: Failed to create resolver for {}", host);
                sceNetSocketClose(m_socket);
                m_socket = -1;
                m_state.store(WsState::DISCONNECTED);
                return false;
            }
        }

        SceNetSockaddrIn serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_len = sizeof(serverAddr);
        serverAddr.sin_family = SCE_NET_AF_INET;
        serverAddr.sin_port = sceNetHtons(port);
        serverAddr.sin_addr = addr;

        if (sceNetConnect(m_socket, (SceNetSockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            brls::Logger::error("WS: connect failed");
            sceNetSocketClose(m_socket);
            m_socket = -1;
            m_state.store(WsState::DISCONNECTED);
            return false;
        }
#else
        m_socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket < 0) {
            m_state.store(WsState::DISCONNECTED);
            return false;
        }

        struct hostent* he = gethostbyname(host.c_str());
        if (!he) {
            ::close(m_socket);
            m_socket = -1;
            m_state.store(WsState::DISCONNECTED);
            return false;
        }

        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        memcpy(&serverAddr.sin_addr, he->h_addr, he->h_length);

        if (::connect(m_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            ::close(m_socket);
            m_socket = -1;
            m_state.store(WsState::DISCONNECTED);
            return false;
        }
#endif
    }

    // Perform WebSocket handshake
    if (!performHandshake(host, path, port, m_subprotocol)) {
        brls::Logger::error("WS: handshake failed");
        cleanupSocket();
        m_state.store(WsState::DISCONNECTED);
        return false;
    }

    m_state.store(WsState::CONNECTED);
    brls::Logger::info("WS connected successfully");

    if (m_useSsl) {
        // Make the socket non-blocking: wsTlsRecv maps EAGAIN to WANT_READ,
        // so the receive thread never sits inside mbedtls_ssl_read holding
        // the SSL lock (senders share the TLS context) - recvExact() polls
        // with a short sleep instead. SO_RCVTIMEO is NOT used because it is
        // silently ignored on Vita sockets; O_NONBLOCK via fcntl works.
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
        }
    }

    // Start receive thread
    m_receiveThread = std::thread(&WebSocketClient::receiveLoop, this);

    return true;
}

bool WebSocketClient::performHandshake(const std::string& host, const std::string& path, int port, const std::string& subprotocol) {
    std::string key = generateKey();

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n";
    req << "Host: " << host;
    if (port != 80 && port != 443) req << ":" << port;
    req << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << key << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    if (!subprotocol.empty()) {
        req << "Sec-WebSocket-Protocol: " << subprotocol << "\r\n";
    }
    req << "\r\n";

    std::string reqStr = req.str();
    if (rawSend(reinterpret_cast<const uint8_t*>(reqStr.c_str()), reqStr.size()) < 0) {
        return false;
    }

    // Read response
    char buf[2048];
    int totalRead = 0;
    bool headerComplete = false;

    while (!headerComplete && totalRead < (int)sizeof(buf) - 1) {
        int n = rawRecv(reinterpret_cast<uint8_t*>(buf + totalRead), 1);
        if (n <= 0) return false;
        totalRead += n;
        buf[totalRead] = '\0';

        if (totalRead >= 4 && strstr(buf, "\r\n\r\n")) {
            headerComplete = true;
        }
    }

    // Check for 101 Switching Protocols
    if (!strstr(buf, "101")) {
        brls::Logger::error("WS: server rejected upgrade: {}", buf);
        return false;
    }

    return true;
}

int WebSocketClient::rawSend(const uint8_t* data, size_t len) {
    if (m_useSsl && m_sslCtx) {
        // The receive thread shares this TLS context; ssl_read/ssl_write must
        // never run concurrently (mbedTLS contexts are not thread-safe).
        std::lock_guard<std::mutex> sslLock(m_sslMutex);
        auto* ssl = static_cast<mbedtls_ssl_context*>(m_sslCtx);
        size_t sent = 0;
        while (sent < len) {
            int ret = mbedtls_ssl_write(ssl, data + sent, len - sent);
            if (ret < 0) {
                if (ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
                    ret == MBEDTLS_ERR_SSL_WANT_READ) {
                    // Non-blocking socket busy - brief pause, then retry with
                    // the same arguments as mbedTLS requires.
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                return -1;
            }
            sent += ret;
        }
        return static_cast<int>(sent);
    } else {
#ifdef __vita__
        return sceNetSend(m_socket, data, len, 0);
#else
        return ::send(m_socket, data, len, 0);
#endif
    }
}

// Returns >0 bytes read, 0 for "no data yet, try again" (receive timeout),
// or <0 on error/connection closed.
int WebSocketClient::rawRecv(uint8_t* data, size_t len) {
    if (m_useSsl && m_sslCtx) {
        std::lock_guard<std::mutex> sslLock(m_sslMutex);
        auto* ssl = static_cast<mbedtls_ssl_context*>(m_sslCtx);
        int ret = mbedtls_ssl_read(ssl, data, len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
        if (ret == 0) return -1;  // clean TLS close
        return ret;               // >0 data, <0 error (incl. close notify)
    } else {
#ifdef __vita__
        int ret = sceNetRecv(m_socket, data, len, 0);
#else
        int ret = static_cast<int>(::recv(m_socket, data, len, 0));
#endif
        if (ret == 0) return -1;  // peer closed
        return ret;
    }
}

bool WebSocketClient::recvExact(uint8_t* data, size_t len) {
    size_t got = 0;
    while (got < len) {
        if (m_state.load() == WsState::DISCONNECTED) return false;
        int n = rawRecv(data + got, len - got);
        if (n < 0) return false;
        if (n == 0) {
            // No data yet (non-blocking socket) - sleep briefly so senders
            // can take the SSL lock, then poll again.
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            continue;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

bool WebSocketClient::sendFrame(WsOpcode opcode, const uint8_t* data, size_t len) {
    std::vector<uint8_t> frame;

    // FIN + opcode
    frame.push_back(0x80 | static_cast<uint8_t>(opcode));

    // Mask bit + length
    uint8_t maskBit = 0x80; // Client must mask
    if (len < 126) {
        frame.push_back(maskBit | static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(maskBit | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(maskBit | 127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
        }
    }

    // Masking key
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = static_cast<uint8_t>(std::rand() % 256);
    }
    frame.insert(frame.end(), mask, mask + 4);

    // Masked payload
    for (size_t i = 0; i < len; i++) {
        frame.push_back(data[i] ^ mask[i % 4]);
    }

    return rawSend(frame.data(), frame.size()) > 0;
}

bool WebSocketClient::readFrame(std::string& payload, WsOpcode& opcode) {
    uint8_t header[2];
    if (!recvExact(header, 2)) return false;

    opcode = static_cast<WsOpcode>(header[0] & 0x0F);
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLen = header[1] & 0x7F;

    if (payloadLen == 126) {
        uint8_t ext[2];
        if (!recvExact(ext, 2)) return false;
        payloadLen = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payloadLen == 127) {
        uint8_t ext[8];
        if (!recvExact(ext, 8)) return false;
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
            payloadLen = (payloadLen << 8) | ext[i];
        }
    }

    // Sanity check: reject frames larger than 16MB to prevent OOM on
    // corrupted frame headers (Vita only has 256MB RAM)
    if (payloadLen > 16 * 1024 * 1024) {
        brls::Logger::error("WS: frame too large ({} bytes), disconnecting", payloadLen);
        return false;
    }

    uint8_t mask[4] = {0};
    if (masked) {
        if (!recvExact(mask, 4)) return false;
    }

    payload.resize(static_cast<size_t>(payloadLen));
    if (payloadLen > 0 &&
        !recvExact(reinterpret_cast<uint8_t*>(&payload[0]),
                   static_cast<size_t>(payloadLen))) {
        return false;
    }

    if (masked) {
        for (size_t i = 0; i < payload.size(); i++) {
            payload[i] ^= mask[i % 4];
        }
    }

    return true;
}

bool WebSocketClient::send(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (!isConnected()) return false;
    return sendFrame(WsOpcode::TEXT,
                     reinterpret_cast<const uint8_t*>(message.c_str()),
                     message.size());
}

void WebSocketClient::receiveLoop() {
    while (m_state.load() == WsState::CONNECTED) {
        std::string payload;
        WsOpcode opcode;

        if (!readFrame(payload, opcode)) {
            if (m_state.load() == WsState::CONNECTED) {
                m_state.store(WsState::DISCONNECTED);
                if (m_onClose) {
                    if (m_dispatchOnMain) {
                        brls::sync([this]() {
                            m_onClose(1006, "Connection lost");
                        });
                    } else {
                        m_onClose(1006, "Connection lost");
                    }
                }
            }
            break;
        }

        switch (opcode) {
            case WsOpcode::TEXT:
                if (m_onMessage) {
                    if (m_dispatchOnMain) {
                        std::string msg = payload;
                        brls::sync([this, msg]() {
                            m_onMessage(msg);
                        });
                    } else {
                        m_onMessage(payload);
                    }
                }
                break;

            case WsOpcode::BINARY:
                if (m_onBinary) {
                    // Binary data must be dispatched immediately to avoid audio
                    // latency - do NOT use brls::sync for binary audio frames.
                    std::vector<uint8_t> binData(payload.begin(), payload.end());
                    m_onBinary(binData.data(), binData.size());
                }
                break;

            case WsOpcode::PING:
                sendFrame(WsOpcode::PONG,
                         reinterpret_cast<const uint8_t*>(payload.c_str()),
                         payload.size());
                break;

            case WsOpcode::CLOSE:
                m_state.store(WsState::DISCONNECTED);
                if (m_onClose) {
                    int code = 1000;
                    std::string reason;
                    if (payload.size() >= 2) {
                        code = (static_cast<uint8_t>(payload[0]) << 8) |
                                static_cast<uint8_t>(payload[1]);
                        if (payload.size() > 2) {
                            reason = payload.substr(2);
                        }
                    }
                    if (m_dispatchOnMain) {
                        brls::sync([this, code, reason]() {
                            m_onClose(code, reason);
                        });
                    } else {
                        m_onClose(code, reason);
                    }
                }
                // Send close frame back
                sendFrame(WsOpcode::CLOSE,
                         reinterpret_cast<const uint8_t*>(payload.c_str()),
                         payload.size());
                break;

            default:
                break;
        }
    }
}

void WebSocketClient::disconnect() {
    auto expected = WsState::CONNECTED;
    if (m_state.compare_exchange_strong(expected, WsState::CLOSING)) {
        // Send close frame
        uint8_t closeData[2] = {0x03, 0xE8}; // 1000 = normal closure
        sendFrame(WsOpcode::CLOSE, closeData, 2);
    }

    m_state.store(WsState::DISCONNECTED);

    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }

    cleanupSocket();
}

void WebSocketClient::cleanupSocket() {
    if (m_useSsl) {
        if (m_sslCtx) {
            mbedtls_ssl_free(static_cast<mbedtls_ssl_context*>(m_sslCtx));
            delete static_cast<mbedtls_ssl_context*>(m_sslCtx);
            m_sslCtx = nullptr;
        }
        if (m_sslConf) {
            mbedtls_ssl_config_free(static_cast<mbedtls_ssl_config*>(m_sslConf));
            delete static_cast<mbedtls_ssl_config*>(m_sslConf);
            m_sslConf = nullptr;
        }
        if (m_ctrDrbg) {
            mbedtls_ctr_drbg_free(static_cast<mbedtls_ctr_drbg_context*>(m_ctrDrbg));
            delete static_cast<mbedtls_ctr_drbg_context*>(m_ctrDrbg);
            m_ctrDrbg = nullptr;
        }
        if (m_entropy) {
            mbedtls_entropy_free(static_cast<mbedtls_entropy_context*>(m_entropy));
            delete static_cast<mbedtls_entropy_context*>(m_entropy);
            m_entropy = nullptr;
        }
        if (m_netCtx) {
            mbedtls_net_free(static_cast<mbedtls_net_context*>(m_netCtx));
            delete static_cast<mbedtls_net_context*>(m_netCtx);
            m_netCtx = nullptr;
        }
    } else if (m_socket >= 0) {
#ifdef __vita__
        sceNetSocketClose(m_socket);
#else
        ::close(m_socket);
#endif
    }
    m_socket = -1;
}

} // namespace vita_ma
