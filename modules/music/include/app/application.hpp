/**
 * Vita Music Assistant - Music Assistant Client for PlayStation Vita
 * Borealis-based Application
 *
 * Types (AppSettings, AudioQuality, etc.) are defined in app.h.
 * This header provides the Application singleton that manages
 * the borealis-based UI lifecycle.
 */

#pragma once

#include "app.h"
#include <string>
#include <functional>
#include <atomic>

namespace vita_ma {

/**
 * Application singleton - manages app lifecycle and global state
 */
class Application {
public:
    static Application& getInstance();

    // Initialize and run the application
    bool init();
    void run();
    void shutdown();

    // Navigation
    void pushLoginActivity();
    void pushMainActivity();
    void pushPlayerActivity(const std::string& queueId);

    // Authentication state
    bool isLoggedIn() const { return !m_authToken.empty(); }
    const std::string& getAuthToken() const { return m_authToken; }
    void setAuthToken(const std::string& token) { m_authToken = token; }
    const std::string& getServerUrl() const { return m_serverUrl; }
    void setServerUrl(const std::string& url) { m_serverUrl = url; }

    // Settings persistence
    bool loadSettings();
    bool saveSettings();

    // User info
    const std::string& getUsername() const { return m_username; }
    void setUsername(const std::string& name) { m_username = name; }

    // Application settings access
    AppSettings& getSettings() { return m_settings; }
    const AppSettings& getSettings() const { return m_settings; }

    // Connect to Sendspin after MA API connection is established
    void connectSendspin();

    // Resolve our own registered Sendspin player's real MA player_id (matched
    // by client id, then name) and store it in App, so local playback commands
    // target the correct player. Retries a few times while MA finishes
    // registering the player. Safe to call from any connect path.
    void resolveOwnPlayerId(int attemptsLeft = 5);

    // Apply theme
    void applyTheme();

    // Apply log level based on settings
    void applyLogLevel();

    // Get quality string for display
    static std::string getQualityString(AudioQuality quality);
    static std::string getThemeString(AppTheme theme);

private:
    Application() = default;
    ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool m_initialized = false;
    std::string m_authToken;
    std::string m_serverUrl;
    std::string m_username;
    AppSettings m_settings;
    // True once a "session expired" login prompt is showing; prevents stacking
    // duplicate login screens. Cleared when a connection succeeds.
    std::atomic<bool> m_reloginPrompted{false};
};

} // namespace vita_ma
