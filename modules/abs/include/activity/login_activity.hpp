/**
 * VitaABS - Login Activity
 * Handles user authentication for Audiobookshelf server
 *
 * Layout is the "centred column" design (resources/xml/activity/login.xml):
 * three caption-over-value field rows, one primary button, a footer pair.
 * The three value labels keep their original binding ids, so this file only
 * changed what it writes into them — the bare value, never "Server: ...".
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include <atomic>
#include <memory>
#include <string>
#include "app/audiobookshelf_client.hpp"

namespace vitaabs {

class LoginActivity : public brls::Activity {
public:
    LoginActivity();
    ~LoginActivity() override;

    brls::View* createContentView() override;

    void onContentAvailable() override;

private:
    void onLoginPressed();
    void onTestConnectionPressed();
    void onOfflinePressed();
    void onCancelPressed();

    // Wire one field row: focus paints the accent border and reveals its
    // "Edit" hint; Cross opens the keyboard for that field.
    void setupFieldRow(brls::Box* row, brls::Label* editHint,
                       std::function<void()> onActivate);
    // Rewrite a value label: the bare value, dimmed placeholder when empty.
    void setValue(brls::Label* label, const std::string& value,
                  const std::string& placeholder = "Not set");
    void refreshPasswordValue();
    void toggleTokenMode();
    // busy = a sign-in is in flight: primary reads "Signing in…" and stops
    // being focusable, Test becomes Cancel.
    void setBusy(bool busy);
    void showStatus(const std::string& text, const std::string& hint = "",
                    bool isError = false);
    void clearStatus();
    // Paint the screen from borealis' active theme so it matches the rest of
    // the app; only the bronze accent is fixed. The theme is loaded during app
    // init, so it is already correct when this screen first renders.
    void applyThemeColors();

    BRLS_BIND(brls::Label, titleLabel, "login/title");
    BRLS_BIND(brls::Box, inputContainer, "login/input_container");
    BRLS_BIND(brls::Label, subline, "login/subline");
    BRLS_BIND(brls::Label, serverCaption, "login/server_caption");
    BRLS_BIND(brls::Label, usernameCaption, "login/username_caption");
    BRLS_BIND(brls::Label, offlineLabel, "login/offline_label");

    BRLS_BIND(brls::Box, serverRow, "login/server_row");
    BRLS_BIND(brls::Label, serverLabel, "login/server_label");
    BRLS_BIND(brls::Label, serverEdit, "login/server_edit");

    BRLS_BIND(brls::Box, usernameRow, "login/username_row");
    BRLS_BIND(brls::Label, usernameLabel, "login/username_label");
    BRLS_BIND(brls::Label, usernameEdit, "login/username_edit");

    BRLS_BIND(brls::Box, passwordRow, "login/password_row");
    BRLS_BIND(brls::Label, passwordCaption, "login/password_caption");
    BRLS_BIND(brls::Label, passwordLabel, "login/password_label");
    BRLS_BIND(brls::Label, passwordEdit, "login/password_edit");
    BRLS_BIND(brls::Label, passwordShow, "login/password_show");

    BRLS_BIND(brls::Button, loginButton, "login/login_button");
    BRLS_BIND(brls::Label, loginLabel, "login/login_label");
    BRLS_BIND(brls::Button, testButton, "login/test_button");
    BRLS_BIND(brls::Label, testLabel, "login/test_label");
    BRLS_BIND(brls::Label, tokenToggle, "login/token_toggle");
    BRLS_BIND(brls::Button, offlineButton, "login/offline_button");

    BRLS_BIND(brls::Box, statusBox, "login/status_box");
    BRLS_BIND(brls::Label, statusLabel, "login/status");
    BRLS_BIND(brls::Label, statusHint, "login/status_hint");

    std::string m_serverUrl;
    std::string m_username;
    std::string m_password;

    bool m_tokenMode  = false;  // password field holds an API token
    bool m_showSecret = false;  // reveal the password/token in clear text
    bool m_busy       = false;  // a sign-in is in flight

    // Guards the async sign-in callback: bumped by Cancel and by teardown so
    // a late result cannot touch views that are gone or no longer waited on.
    std::shared_ptr<std::atomic<int>> m_attempt = std::make_shared<std::atomic<int>>(0);
};

} // namespace vitaabs
