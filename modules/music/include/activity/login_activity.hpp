/**
 * Vita Music Assistant - Login Activity
 * "Focused card" design (1c): one centered glass card on a dark backdrop.
 * Supports Music Assistant (username/password) and Remote (WebRTC) sign-in.
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include "app/ma_types.hpp"

namespace vita_ma {

enum class AuthMode {
    MUSIC_ASSISTANT,   // Direct MA server login (username/password)
    REMOTE             // Remote access via WebRTC (Remote ID + saved token)
};

class LoginActivity : public brls::Activity {
public:
    LoginActivity();
    ~LoginActivity() override;

    brls::View* createContentView() override;

    void onContentAvailable() override;

private:
    void onConnectPressed();
    void onRemoteLoginPressed();
    void setAuthMode(AuthMode mode);
    void updateUIForMode();
    void updateFieldValues();

    // Auth flows
    void loginWithMA(const std::string& serverUrl,
                     const std::string& username,
                     const std::string& password);

    // After getting a token, connect the WebSocket
    void connectWithToken(const std::string& serverUrl,
                          const std::string& token,
                          const std::string& username);

    // Card chrome
    BRLS_BIND(brls::Label, titleLabel, "login/title");
    BRLS_BIND(brls::Label, subtitleLabel, "login/subtitle");

    // Segmented mode switch
    BRLS_BIND(brls::Box, segMa, "login/seg_ma");
    BRLS_BIND(brls::Box, segRemote, "login/seg_remote");
    BRLS_BIND(brls::Label, segMaLabel, "login/seg_ma_label");
    BRLS_BIND(brls::Label, segRemoteLabel, "login/seg_remote_label");

    // Credential fields (Music Assistant mode)
    BRLS_BIND(brls::Box, fieldsCredentials, "login/fields_credentials");
    BRLS_BIND(brls::Label, serverCaption, "login/server_caption");
    BRLS_BIND(brls::Box, serverField, "login/server_field");
    BRLS_BIND(brls::Label, serverValue, "login/server_value");
    BRLS_BIND(brls::Box, usernameField, "login/username_field");
    BRLS_BIND(brls::Label, usernameValue, "login/username_value");
    BRLS_BIND(brls::Box, passwordField, "login/password_field");
    BRLS_BIND(brls::Label, passwordValue, "login/password_value");

    // Remote field (Remote mode)
    BRLS_BIND(brls::Box, fieldsRemote, "login/fields_remote");
    BRLS_BIND(brls::Box, remoteField, "login/remote_field");
    BRLS_BIND(brls::Label, remoteValue, "login/remote_value");
    BRLS_BIND(brls::Box, remoteCaret, "login/remote_caret");
    BRLS_BIND(brls::Box, remoteScanButton, "login/remote_scan_button");

    // Connect button + status
    BRLS_BIND(brls::Box, connectButton, "login/connect_button");
    BRLS_BIND(brls::Label, connectLabel, "login/connect_label");
    BRLS_BIND(brls::Box, statusDot, "login/status_dot");
    BRLS_BIND(brls::Label, statusLabel, "login/status");

    AuthMode m_authMode = AuthMode::MUSIC_ASSISTANT;
    std::string m_serverUrl;
    std::string m_username;
    std::string m_password;
    std::string m_remoteId;

    // Blinking caret in the Remote ID field
    brls::RepeatingTimer m_caretTimer;
    bool m_caretVisible = true;

    void setStatus(const std::string& text);
};

} // namespace vita_ma
