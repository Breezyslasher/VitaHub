/**
 * VitaABS - Login Activity implementation
 * Handles user authentication for Audiobookshelf server
 */

#include "activity/login_activity.hpp"

#ifdef VITAHUB
#include "vitahub/bridge.hpp"
#endif
#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "view/progress_dialog.hpp"
#include "utils/async.hpp"
#include "utils/ui_theme.hpp"

#include <memory>

namespace vitaabs {

namespace {

// See utils/ui_theme.hpp — panel/hairline are derived contrast steps, not
// nearby theme keys, so a bordered input row cannot blend into the page.
inline NVGcolor kValueText()   { return uitok::text(); }
inline NVGcolor kMuted()       { return uitok::secondary(); }
inline NVGcolor kPlaceholder() { return uitok::secondary(); }
inline NVGcolor kPanel()       { return uitok::panel(); }
inline NVGcolor kBorderIdle()  { return uitok::hairline(); }
inline NVGcolor kAccent_()     { return uitok::accent(); }
const NVGcolor kBorderFocused = nvgRGB(0xd7, 0x9b, 0x5a);
const NVGcolor kAccent        = nvgRGB(0xd7, 0x9b, 0x5a);
const NVGcolor kAccentInk     = nvgRGB(0x14, 0x16, 0x1e);
const NVGcolor kErrorText     = nvgRGB(0xe0, 0xa2, 0xa2);
const NVGcolor kErrorFill     = nvgRGB(0x24, 0x1c, 0x1c);
const NVGcolor kErrorBorder   = nvgRGB(0x4a, 0x2b, 0x2b);

// Strip scheme and trailing slash so an error line can name the host the way
// the user typed it: "Could not reach 192.168.1.42:13378".
std::string hostOf(const std::string& url) {
    std::string s = url;
    for (const char* p : { "https://", "http://" }) {
        size_t n = std::string(p).size();
        if (s.compare(0, n, p) == 0) { s.erase(0, n); break; }
    }
    size_t slash = s.find('/');
    if (slash != std::string::npos) s.erase(slash);
    return s.empty() ? url : s;
}

}  // namespace

LoginActivity::LoginActivity() {
    brls::Logger::debug("LoginActivity created");
}

LoginActivity::~LoginActivity() {
    // Any sign-in still in flight must not touch these views once they go.
    if (m_attempt) (*m_attempt)++;
}

brls::View* LoginActivity::createContentView() {
    return brls::View::createFromXMLResource("abs/activity/login.xml");
}

// ── small helpers ──────────────────────────────────────────────────────

void LoginActivity::setValue(brls::Label* label, const std::string& value,
                             const std::string& placeholder) {
    if (!label) return;
    if (value.empty()) {
        label->setText(placeholder);
        label->setTextColor(kPlaceholder());
    } else {
        label->setText(value);
        label->setTextColor(kValueText());
    }
}

void LoginActivity::refreshPasswordValue() {
    if (!passwordLabel) return;
    if (m_password.empty()) {
        setValue(passwordLabel, "");
        return;
    }
    if (m_showSecret) {
        setValue(passwordLabel, m_password);
    } else {
        // One bullet per character, capped so a long API token cannot
        // overflow the row.
        size_t n = m_password.size() > 24 ? 24 : m_password.size();
        std::string dots;
        for (size_t i = 0; i < n; i++) dots += "•";
        setValue(passwordLabel, dots);
    }
}

void LoginActivity::setupFieldRow(brls::Box* row, brls::Label* editHint,
                                  std::function<void()> onActivate) {
    if (!row) return;

    row->registerClickAction([onActivate](brls::View*) {
        onActivate();
        return true;
    });
    row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

    row->getFocusEvent()->subscribe([row, editHint](brls::View*) {
        row->setBorderColor(kBorderFocused);
        if (editHint) editHint->setVisibility(brls::Visibility::VISIBLE);
    });
    row->getFocusLostEvent()->subscribe([row, editHint](brls::View*) {
        row->setBorderColor(kBorderIdle());
        if (editHint) editHint->setVisibility(brls::Visibility::INVISIBLE);
    });
}

void LoginActivity::showStatus(const std::string& text, const std::string& hint,
                               bool isError) {
    if (!statusBox || !statusLabel) return;

    statusBox->setVisibility(brls::Visibility::VISIBLE);
    statusLabel->setText(text);
    statusLabel->setTextColor(isError ? kErrorText : kMuted());

    if (isError) {
        statusBox->setBackgroundColor(kErrorFill);
        statusBox->setBorderColor(kErrorBorder);
        statusBox->setBorderThickness(1);
        statusBox->setPadding(12, 14, 12, 14);
    } else {
        statusBox->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        statusBox->setBorderThickness(0);
        statusBox->setPadding(0);
    }

    if (statusHint) {
        if (hint.empty()) {
            statusHint->setVisibility(brls::Visibility::GONE);
        } else {
            statusHint->setText(hint);
            statusHint->setTextColor(isError ? kErrorText : kMuted());
            statusHint->setVisibility(brls::Visibility::VISIBLE);
        }
    }
}

void LoginActivity::clearStatus() {
    if (statusBox) statusBox->setVisibility(brls::Visibility::GONE);
}

void LoginActivity::setBusy(bool busy) {
    m_busy = busy;

    if (loginLabel) {
        loginLabel->setText(busy ? "Signing in…" : "Sign in");
        loginLabel->setTextColor(busy ? kPlaceholder() : kAccentInk);
    }
    if (loginButton) {
        loginButton->setBackgroundColor(busy ? kPanel() : kAccent);
        loginButton->setFocusable(!busy);
    }
    if (testLabel) testLabel->setText(busy ? "Cancel" : "Test");

    // Focus would otherwise sit on a view that just stopped being focusable.
    if (busy && testButton) brls::Application::giveFocus(testButton);
}

void LoginActivity::toggleTokenMode() {
    m_tokenMode = !m_tokenMode;

    if (tokenToggle)
        tokenToggle->setText(m_tokenMode ? "Use a password" : "Use an API token");
    if (passwordCaption)
        passwordCaption->setText(m_tokenMode ? "API TOKEN" : "PASSWORD");

    // Audiobookshelf accepts a token in place of username+password, so the
    // username row has nothing to collect in token mode.
    if (usernameRow)
        usernameRow->setVisibility(m_tokenMode ? brls::Visibility::GONE
                                               : brls::Visibility::VISIBLE);

    // The secret means something different now; don't carry it across.
    m_password.clear();
    refreshPasswordValue();
    clearStatus();
}

// ── setup ──────────────────────────────────────────────────────────────

void LoginActivity::applyThemeColors() {
    // BoundView's operator T*() is non-const, so collect plain pointers first.
    brls::Box*   rows[]     = { serverRow, usernameRow, passwordRow };
    brls::Label* captions[] = { serverCaption, usernameCaption, passwordCaption };
    brls::Label* muted[]    = { subline, passwordShow, tokenToggle, offlineLabel };

    for (brls::Box* r : rows) {
        if (!r) continue;
        r->setBackgroundColor(kPanel());
        r->setBorderColor(kBorderIdle());
    }
    for (brls::Label* l : captions) { if (l) l->setTextColor(kMuted()); }
    for (brls::Label* l : muted)    { if (l) l->setTextColor(kMuted()); }

    if (titleLabel) titleLabel->setTextColor(kValueText());
    if (loginButton) loginButton->setBackgroundColor(kAccent);
    if (loginLabel)  loginLabel->setTextColor(kAccentInk);
    if (testButton) {
        testButton->setBackgroundColor(kPanel());
        testButton->setBorderColor(kBorderIdle());
    }
    if (testLabel) testLabel->setTextColor(kValueText());
}

void LoginActivity::onContentAvailable() {
    brls::Logger::debug("LoginActivity content available");

    applyThemeColors();

    // Focus paints a ring, not a fill: borealis draws a highlight BACKGROUND
    // behind a focused view, which flattens the accent-filled Sign in button
    // to a dark square the moment it takes focus.
    if (loginButton) { loginButton->setHideHighlightBackground(true); loginButton->setHighlightCornerRadius(8.0f); }
    if (testButton)  { testButton->setHideHighlightBackground(true);  testButton->setHighlightCornerRadius(8.0f); }
    for (brls::Box* r : { (brls::Box*)serverRow, (brls::Box*)usernameRow, (brls::Box*)passwordRow }) {
        if (r) { r->setHideHighlightBackground(true); r->setHighlightCornerRadius(6.0f); }
    }

    setValue(serverLabel, m_serverUrl);
    setValue(usernameLabel, m_username);
    refreshPasswordValue();
    clearStatus();

    setupFieldRow(serverRow, serverEdit, [this]() {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            m_serverUrl = text;
            setValue(serverLabel, m_serverUrl);
        }, "Enter Server URL", "http://your-server:13378", 256, m_serverUrl);
    });

    setupFieldRow(usernameRow, usernameEdit, [this]() {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            m_username = text;
            setValue(usernameLabel, m_username);
        }, "Enter Username", "", 128, m_username);
    });

    setupFieldRow(passwordRow, passwordEdit, [this]() {
        if (m_tokenMode) {
            // A token is pasted, not typed blind — use the plain text IME.
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_password = text;
                refreshPasswordValue();
            }, "Enter API Token", "", 512, m_password);
        } else {
            brls::Application::getImeManager()->openForPassword([this](std::string text) {
                m_password = text;
                refreshPasswordValue();
            }, "Enter Password", "", 128, "");
        }
    });

    if (passwordShow) {
        passwordShow->registerClickAction([this](brls::View*) {
            m_showSecret = !m_showSecret;
            passwordShow->setText(m_showSecret ? "Hide" : "Show");
            refreshPasswordValue();
            return true;
        });
        passwordShow->addGestureRecognizer(new brls::TapGestureRecognizer(passwordShow));
    }

    if (loginButton) {
        loginButton->registerClickAction([this](brls::View*) {
            onLoginPressed();
            return true;
        });
    }

    if (testButton) {
        testButton->registerClickAction([this](brls::View*) {
            if (m_busy) onCancelPressed();
            else        onTestConnectionPressed();
            return true;
        });
    }

    if (tokenToggle) {
        tokenToggle->registerClickAction([this](brls::View*) {
            toggleTokenMode();
            return true;
        });
        tokenToggle->addGestureRecognizer(new brls::TapGestureRecognizer(tokenToggle));
    }

    if (offlineButton) {
        offlineButton->registerClickAction([this](brls::View*) {
            onOfflinePressed();
            return true;
        });
    }

    // Circle quits the app rather than popping to nothing — this is the
    // root activity when signed out.
#ifdef VITAHUB
    // Inside VitaHub, Circle leaves the service instead of the whole hub.
    this->registerAction("VitaHub", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        vitahub::returnToHub();
        return true;
    });
#else
    this->registerAction("Quit", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::quit();
        return true;
    });
#endif

    // Land on the first thing the user still has to do.
    bool complete = !m_serverUrl.empty() && !m_password.empty() &&
                    (m_tokenMode || !m_username.empty());
    if (complete && loginButton)      brls::Application::giveFocus(loginButton);
    else if (serverRow)               brls::Application::giveFocus(serverRow);
}

// ── actions ────────────────────────────────────────────────────────────

void LoginActivity::onTestConnectionPressed() {
    if (m_serverUrl.empty()) {
        showStatus("Enter a server URL first.", "", true);
        return;
    }

    showStatus("Testing connection…");

    const std::string url = m_serverUrl;
    const int myAttempt = ++(*m_attempt);
    std::weak_ptr<std::atomic<int>> attemptWeak = m_attempt;

    asyncRun([this, url, myAttempt, attemptWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        bool reachable = client.connectToServer(url);
        ServerInfo info;
        bool haveInfo = reachable && client.fetchServerInfo(info);
        std::string name = info.serverName, version = info.version;

        brls::sync([this, myAttempt, attemptWeak, url, reachable, haveInfo, name, version]() {
            auto attempt = attemptWeak.lock();
            if (!attempt || attempt->load() != myAttempt) return;   // cancelled or gone

            if (!reachable) {
                showStatus("Could not reach " + hostOf(url) + ".",
                           "Check the port and that this device is on the same network.",
                           true);
            } else if (haveInfo) {
                showStatus("Connected to " + name + " v" + version + ".");
            } else {
                showStatus("Server is reachable.");
            }
        });
    });
}

void LoginActivity::onCancelPressed() {
    // The HTTP call itself is synchronous and cannot be interrupted, so this
    // abandons the result rather than aborting the request: bumping the
    // attempt counter makes the callback a no-op when it lands.
    ++(*m_attempt);
    setBusy(false);
    showStatus("Cancelled.");
}

void LoginActivity::onLoginPressed() {
    if (m_busy) return;

    if (m_serverUrl.empty()) {
        showStatus("Enter a server URL first.", "", true);
        if (serverRow) brls::Application::giveFocus(serverRow);
        return;
    }
    if (!m_tokenMode && m_username.empty()) {
        showStatus("Enter a username.", "", true);
        if (usernameRow) brls::Application::giveFocus(usernameRow);
        return;
    }
    if (m_password.empty()) {
        showStatus(m_tokenMode ? "Enter an API token." : "Enter a password.", "", true);
        if (passwordRow) brls::Application::giveFocus(passwordRow);
        return;
    }

    setBusy(true);
    clearStatus();

    const std::string url = m_serverUrl;
    const std::string user = m_username;
    const std::string secret = m_password;
    const bool tokenMode = m_tokenMode;
    const int myAttempt = ++(*m_attempt);
    std::weak_ptr<std::atomic<int>> attemptWeak = m_attempt;

    asyncRun([this, url, user, secret, tokenMode, myAttempt, attemptWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        client.setServerUrl(url);

        bool ok;
        if (tokenMode) {
            // An API token stands in for the whole login exchange; validating
            // it is what proves the server accepted it.
            client.setAuthToken(secret);
            ok = client.validateToken();
        } else {
            ok = client.login(user, secret);
        }
        std::string token = ok ? client.getAuthToken() : std::string();

        brls::sync([this, myAttempt, attemptWeak, url, user, tokenMode, ok, token]() {
            auto attempt = attemptWeak.lock();
            if (!attempt || attempt->load() != myAttempt) return;   // cancelled or gone

            setBusy(false);

            if (!ok) {
                showStatus(tokenMode ? "That API token was rejected."
                                     : "Sign-in failed.",
                           tokenMode ? "Check the token and that the server is reachable."
                                     : "Check the username and password, then try again.",
                           true);
                return;
            }

            Application& app = Application::getInstance();
            app.setUsername(user);
            app.setAuthToken(token);

            // Private-range hosts are treated as the "local" URL so the
            // auto-switch setting has both halves to choose between.
            bool isLocalUrl = (url.find("192.168.") != std::string::npos ||
                               url.find("10.") != std::string::npos ||
                               url.find("172.16.") != std::string::npos ||
                               url.find("172.17.") != std::string::npos ||
                               url.find("172.18.") != std::string::npos ||
                               url.find("172.19.") != std::string::npos ||
                               url.find("172.2") != std::string::npos ||
                               url.find("172.30.") != std::string::npos ||
                               url.find("172.31.") != std::string::npos ||
                               url.find("localhost") != std::string::npos ||
                               url.find("127.0.0.1") != std::string::npos);

            if (isLocalUrl) {
                app.setLocalServerUrl(url);
                app.setUseLocalUrl(true);
            } else {
                app.setRemoteServerUrl(url);
                app.setUseLocalUrl(false);
            }
            app.setServerUrl(url);
            app.saveSettings();

            app.pushMainActivity();
        });
    });
}

void LoginActivity::onOfflinePressed() {
    brls::Logger::info("User selected offline mode");
    showStatus("Entering offline mode…");

    brls::sync([this]() {
        Application::getInstance().pushMainActivity();
    });
}

} // namespace vitaabs
