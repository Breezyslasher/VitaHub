/**
 * Vita Music Assistant - Login Activity implementation
 * Supports Music Assistant (direct) and Remote (WebRTC) authentication
 *
 * Auth flow:
 *   1. User enters server URL, username, password
 *   2. POST /auth/login with credentials → receive token
 *   3. Connect WebSocket and send auth command with token
 *   4. On success, save settings and proceed to main activity
 */

#include "activity/login_activity.hpp"
#include "activity/qr_scanner_activity.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "app/webrtc_client.hpp"
#include "app/ma_types.hpp"
#include "utils/http_client.hpp"
#include "utils/async.hpp"
#include "view/progress_dialog.hpp"

#include <memory>
#include <cctype>
#include <thread>

namespace vita_ma {

namespace {

// Card palette (login 1c)
inline NVGcolor colText()      { return nvgRGB(0xee, 0xf2, 0xf6); }
inline NVGcolor colMuted()     { return nvgRGB(0x93, 0xa0, 0xae); }
inline NVGcolor colDim()       { return nvgRGB(0x5d, 0x68, 0x75); }
inline NVGcolor colCyan()      { return nvgRGB(0x00, 0xbc, 0xee); }
inline NVGcolor colCyanInk()   { return nvgRGB(0x04, 0x22, 0x2e); }
inline NVGcolor colClear()     { return nvgRGBA(0, 0, 0, 0); }

// Group a long Remote ID for display: XHK7-YM5K-6OJ3-...
std::string groupRemoteId(const std::string& id) {
    std::string out;
    for (size_t i = 0; i < id.size(); i++) {
        if (i > 0 && i % 4 == 0) out += '-';
        out += id[i];
    }
    return out;
}

} // namespace

LoginActivity::LoginActivity() {
    brls::Logger::debug("LoginActivity created");
}

LoginActivity::~LoginActivity() {
    m_caretTimer.stop();
}

brls::View* LoginActivity::createContentView() {
    return brls::View::createFromXMLResource("music/activity/login.xml");
}

void LoginActivity::setStatus(const std::string& text) {
    if (statusLabel) statusLabel->setText(text);
    if (statusDot) {
        statusDot->setVisibility(text.empty() ? brls::Visibility::INVISIBLE
                                              : brls::Visibility::VISIBLE);
    }
}

void LoginActivity::onContentAvailable() {
    brls::Logger::debug("LoginActivity content available");

    // Restore saved values
    auto& app = Application::getInstance();
    m_serverUrl = app.getServerUrl();
    m_username = app.getUsername();
    m_remoteId = app.getSettings().remoteId;
    // If the saved server is itself a Remote ID (legacy settings), surface it
    // in the remote field
    if (MAClient::isRemoteId(m_serverUrl)) {
        if (m_remoteId.empty()) m_remoteId = m_serverUrl;
        m_serverUrl.clear();
        m_authMode = AuthMode::REMOTE;
    }
    // Land on the Remote segment when the last session was remote
    if (app.getSettings().lastConnectionRemote && !m_remoteId.empty()) {
        m_authMode = AuthMode::REMOTE;
    }

    // Segmented mode switch
    auto wireSegment = [this](brls::Box* seg, AuthMode mode) {
        if (!seg) return;
        seg->registerClickAction([this, mode](brls::View*) {
            setAuthMode(mode);
            return true;
        });
        seg->addGestureRecognizer(new brls::TapGestureRecognizer(seg));
    };
    wireSegment(segMa, AuthMode::MUSIC_ASSISTANT);
    wireSegment(segRemote, AuthMode::REMOTE);

    // Server URL input (click to edit via IME)
    if (serverField) {
        serverField->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_serverUrl = text;
                updateFieldValues();
            }, "Enter Server URL", "http://your-server:8095", 256, m_serverUrl);
            return true;
        });
        serverField->addGestureRecognizer(new brls::TapGestureRecognizer(serverField));
    }

    // Username input
    if (usernameField) {
        usernameField->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_username = text;
                updateFieldValues();
            }, "Enter Username", "admin", 128, m_username);
            return true;
        });
        usernameField->addGestureRecognizer(new brls::TapGestureRecognizer(usernameField));
    }

    // Password input
    if (passwordField) {
        passwordField->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_password = text;
                updateFieldValues();
            }, "Enter Password", "", 128, "");
            return true;
        });
        passwordField->addGestureRecognizer(new brls::TapGestureRecognizer(passwordField));
    }

    // Remote ID input (WebRTC remote access)
    if (remoteField) {
        remoteField->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                // Canonicalize (strips hyphen grouping / extracts from a pasted URL)
                m_remoteId = WebRTCClient::normalizeRemoteId(text);
                updateFieldValues();
            }, "Enter Remote ID", "Remote ID from server settings", 80, m_remoteId);
            return true;
        });
        remoteField->addGestureRecognizer(new brls::TapGestureRecognizer(remoteField));
    }

    // QR scan: open the camera and scan the Remote ID code shown in the
    // server's Remote Access settings
    if (remoteScanButton) {
        remoteScanButton->registerClickAction([this](brls::View*) {
            brls::Application::pushActivity(new QrScannerActivity(
                [this](const std::string& payload) {
                    std::string id = WebRTCClient::normalizeRemoteId(payload);
                    if (id.empty()) {
                        setStatus("QR code is not a Remote ID");
                        return;
                    }
                    m_remoteId = id;
                    updateFieldValues();
                    setStatus("Remote ID scanned");
                }));
            return true;
        });
        remoteScanButton->addGestureRecognizer(new brls::TapGestureRecognizer(remoteScanButton));
    }

    // Connect / Log In button
    if (connectButton) {
        connectButton->registerClickAction([this](brls::View*) {
            onConnectPressed();
            return true;
        });
        connectButton->addGestureRecognizer(new brls::TapGestureRecognizer(connectButton));
        connectButton->setShadowType(brls::ShadowType::GENERIC);
    }

    // Blinking cyan caret in the Remote ID field
    m_caretTimer.setCallback([this]() {
        m_caretVisible = !m_caretVisible;
        if (remoteCaret && m_authMode == AuthMode::REMOTE) {
            remoteCaret->setVisibility(m_caretVisible ? brls::Visibility::VISIBLE
                                                      : brls::Visibility::INVISIBLE);
        }
    });
    m_caretTimer.start(600);

    updateUIForMode();
    updateFieldValues();
}

void LoginActivity::updateFieldValues() {
    if (serverValue) {
        bool empty = m_serverUrl.empty();
        serverValue->setText(empty ? "http://your-server:8095" : m_serverUrl);
        serverValue->setTextColor(empty ? colDim() : colText());
    }
    if (usernameValue) {
        bool empty = m_username.empty();
        usernameValue->setText(empty ? "Not set" : m_username);
        usernameValue->setTextColor(empty ? colDim() : colText());
    }
    if (passwordValue) {
        bool empty = m_password.empty();
        passwordValue->setText(empty ? "Not set" : std::string(m_password.length(), '*'));
        passwordValue->setTextColor(empty ? colDim() : colText());
    }
    if (remoteValue) {
        bool empty = m_remoteId.empty();
        remoteValue->setText(empty ? "Enter Remote ID" : groupRemoteId(m_remoteId));
        remoteValue->setTextColor(empty ? colDim() : colText());
        remoteValue->setFontSize(empty ? 18.0f : 16.0f);
    }
}

void LoginActivity::setAuthMode(AuthMode mode) {
    if (m_authMode == mode) return;
    m_authMode = mode;
    updateUIForMode();
}

void LoginActivity::updateUIForMode() {
    // Segmented switch: active segment fills cyan with dark ink text
    auto styleSegment = [](brls::Box* seg, brls::Label* label, bool active) {
        if (seg)   seg->setBackgroundColor(active ? colCyan() : colClear());
        if (label) label->setTextColor(active ? colCyanInk() : colMuted());
    };
    styleSegment(segMa, segMaLabel, m_authMode == AuthMode::MUSIC_ASSISTANT);
    styleSegment(segRemote, segRemoteLabel, m_authMode == AuthMode::REMOTE);

    bool remote = (m_authMode == AuthMode::REMOTE);
    // Remote mode keeps Username/Password (used for a first-time login over
    // the WebRTC channel when no token is saved); only Server URL disappears.
    if (serverCaption)
        serverCaption->setVisibility(remote ? brls::Visibility::GONE
                                            : brls::Visibility::VISIBLE);
    if (serverField)
        serverField->setVisibility(remote ? brls::Visibility::GONE
                                          : brls::Visibility::VISIBLE);
    if (fieldsRemote)
        fieldsRemote->setVisibility(remote ? brls::Visibility::VISIBLE
                                           : brls::Visibility::GONE);
    // A GONE section still leaves its interactive rows in the focus chain, so
    // the Remote ID field / QR-scan button stayed reachable on the Music
    // Assistant login screen. Toggle focusability with the mode so they can't be
    // focused unless Remote mode is active.
    if (remoteField)      remoteField->setFocusable(remote);
    if (remoteScanButton) remoteScanButton->setFocusable(remote);

    if (connectLabel) connectLabel->setText(remote ? "Connect" : "Log In");

    switch (m_authMode) {
        case AuthMode::MUSIC_ASSISTANT:
            setStatus("Enter your Music Assistant server credentials");
            break;
        case AuthMode::REMOTE:
            setStatus("Connect from anywhere via secure WebRTC");
            break;
    }
}

void LoginActivity::onConnectPressed() {
    if (m_authMode == AuthMode::REMOTE) {
        onRemoteLoginPressed();
        return;
    }

    if (m_serverUrl.empty()) {
        setStatus("Please enter a server URL");
        return;
    }

    // A Remote ID pasted into the server field still works: route it to the
    // remote flow rather than trying HTTP against it.
    if (MAClient::isRemoteId(m_serverUrl)) {
        m_remoteId = WebRTCClient::normalizeRemoteId(m_serverUrl);
        setAuthMode(AuthMode::REMOTE);
        updateFieldValues();
        onRemoteLoginPressed();
        return;
    }

    if (m_username.empty()) {
        setStatus("Please enter a username");
        return;
    }

    if (m_password.empty()) {
        setStatus("Please enter a password");
        return;
    }

    loginWithMA(m_serverUrl, m_username, m_password);
}

void LoginActivity::onRemoteLoginPressed() {
    if (m_remoteId.empty()) {
        setStatus("Enter a Remote ID first");
        return;
    }
    if (!MAClient::isRemoteId(m_remoteId)) {
        setStatus("Invalid Remote ID - copy it from the server's Remote Access settings");
        return;
    }

    // Prefer a saved token from a previous sign-in. Without one, log in with
    // credentials over the WebRTC channel itself: 'auth/login' is a socket
    // command (callable unauthenticated), so no HTTP reachability is needed.
    auto& app = Application::getInstance();
    std::string token = app.getAuthToken();
    bool credentialLogin = token.empty();
    if (credentialLogin) {
        if (m_username.empty() || m_password.empty()) {
            setStatus("Enter your username and password (first remote login)");
            return;
        }
        MAClient::instance().setPendingCredentials(m_username, m_password, "builtin");
    }

    // Persist the Remote ID so it is remembered next launch.
    app.getSettings().remoteId = m_remoteId;
    app.saveSettings();

    setStatus("Establishing secure WebRTC connection...");
    std::string user = !m_username.empty() ? m_username : app.getUsername();
    std::string remoteId = m_remoteId;

    // connect() blocks until the WebRTC handshake completes (or fails), so it
    // must not run on the UI thread.
    std::thread([this, remoteId, token, user]() {
        bool ok = MAClient::instance().connect(remoteId, token);

        brls::sync([this, remoteId, user, ok]() {
            if (ok) {
                auto& a = Application::getInstance();
                // Keep the saved server URL (the direct address); remember the
                // remote route via remoteId + lastConnectionRemote instead.
                a.getSettings().remoteId = remoteId;
                a.getSettings().lastConnectionRemote = true;
                a.setUsername(user);
                a.saveSettings();
                // Credential logins persist their token via the long-lived
                // token upgrade callback; a reused token is stored already.
                setStatus("Connected!");
                a.connectSendspin();
                a.resolveOwnPlayerId();
                a.pushMainActivity();
            } else {
                std::string err = WebRTCClient::instance().getLastError();
                if (err.empty()) err = "check Remote ID and credentials";
                setStatus("Remote connection failed: " + err);
            }
        });
    }).detach();
}

void LoginActivity::loginWithMA(const std::string& serverUrl,
                                 const std::string& username,
                                 const std::string& password) {
    auto* progressDialog = new ProgressDialog("Logging in");
    progressDialog->setStatus("Authenticating with Music Assistant...");
    progressDialog->show();

    auto cancelled = std::make_shared<bool>(false);
    progressDialog->setCancelCallback([cancelled]() {
        *cancelled = true;
    });

    asyncRun([this, serverUrl, username, password, progressDialog, cancelled]() {
        if (*cancelled) return;

        // Build the auth URL: POST /auth/login
        std::string authUrl = serverUrl;
        if (!authUrl.empty() && authUrl.back() == '/') authUrl.pop_back();
        authUrl += "/auth/login";

        // Build JSON body
        std::string body = "{\"credentials\":{\"username\":\"" +
            username + "\",\"password\":\"" + password + "\"}}";

        brls::Logger::info("Login: POST {}", authUrl);

        HttpClient http;
        http.setTimeout(15);
        HttpResponse resp = http.post(authUrl, body, "application/json");

        if (*cancelled) return;

        if (!resp.success || resp.statusCode != 200) {
            brls::sync([this, progressDialog, resp]() {
                std::string errMsg = "Login failed";
                if (!resp.error.empty()) {
                    errMsg += ": " + resp.error;
                } else if (resp.statusCode == 401) {
                    errMsg = "Invalid username or password";
                } else if (resp.statusCode == 403) {
                    errMsg = "Access denied";
                } else if (resp.statusCode > 0) {
                    errMsg += " (HTTP " + std::to_string(resp.statusCode) + ")";
                } else {
                    errMsg += ": Could not reach server";
                }
                progressDialog->setStatus(errMsg);
                if (statusLabel) statusLabel->setText(errMsg);
                brls::delay(2000, [progressDialog]() {
                    progressDialog->dismiss();
                });
            });
            return;
        }

        // Parse response to extract token
        Json result = Json::parse(resp.body);
        std::string token;

        if (result.has("token")) {
            token = result["token"].str();
        }

        if (token.empty()) {
            brls::sync([this, progressDialog]() {
                progressDialog->setStatus("Login failed: no token received");
                if (statusLabel) statusLabel->setText("Login failed: no token received");
                brls::delay(2000, [progressDialog]() {
                    progressDialog->dismiss();
                });
            });
            return;
        }

        brls::Logger::info("Login: got token, connecting WebSocket...");

        brls::sync([this, progressDialog, serverUrl, token, username]() {
            progressDialog->setStatus("Connecting to server...");
        });

        connectWithToken(serverUrl, token, username);

        brls::sync([this, progressDialog, serverUrl]() {
            if (MAClient::instance().isConnected()) {
                progressDialog->setStatus("Connected!");
                progressDialog->setProgress(1.0f);

                // Connect Sendspin for audio streaming
                Application::getInstance().connectSendspin();
                Application::getInstance().resolveOwnPlayerId();

                brls::delay(500, [this, progressDialog]() {
                    progressDialog->dismiss();
                    Application::getInstance().pushMainActivity();
                });
            } else {
                progressDialog->setStatus("Connected to server but WebSocket auth failed");
                if (statusLabel) statusLabel->setText("WebSocket connection failed");
                brls::delay(2000, [progressDialog]() {
                    progressDialog->dismiss();
                });
            }
        });
    });
}

void LoginActivity::connectWithToken(const std::string& serverUrl,
                                      const std::string& token,
                                      const std::string& username) {
    // Connect the WebSocket with the obtained token
    MAClient& client = MAClient::instance();

    // A fresh credential login yields a (possibly short-lived) token. Ask the
    // client to upgrade it to a long-lived token once authenticated, so future
    // connections - especially remote WebRTC ones that can't reach /auth/login
    // - never expire. Skipped for Remote IDs, which reuse an existing token.
    if (!MAClient::isRemoteId(serverUrl)) {
        client.setUpgradeToLongLived(true);
    }

    if (client.connect(serverUrl, token)) {
        brls::Logger::info("Login: WebSocket connected and authenticated");

        // Save credentials. A Remote ID is stored as the remote route - the
        // saved server URL always keeps the direct HTTP address.
        auto& app = Application::getInstance();
        if (MAClient::isRemoteId(serverUrl)) {
            app.getSettings().remoteId = serverUrl;
            app.getSettings().lastConnectionRemote = true;
        } else {
            app.setServerUrl(serverUrl);
            app.getSettings().lastConnectionRemote = false;
        }
        app.setAuthToken(token);
        app.setUsername(username);
        app.saveSettings();
    } else {
        brls::Logger::error("Login: WebSocket connection failed");
    }
}

} // namespace vita_ma
