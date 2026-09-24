/**
 * Vita Music Assistant - Application implementation
 */

#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "app/ma_types.hpp"
#include "app/sendspin_client.hpp"
#include "activity/login_activity.hpp"
#include "activity/main_activity.hpp"
#include "activity/player_activity.hpp"

#include <borealis.hpp>
#include <fstream>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#endif

namespace vita_ma {

static const char* SETTINGS_PATH = "ux0:data/VitaMA/settings.json";

Application& Application::getInstance() {
    static Application instance;
    return instance;
}

bool Application::init() {
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    brls::Logger::info("Vita Music Assistant {} initializing...", VMA_VERSION);

#ifdef __vita__
    // Create data directory
    int ret = sceIoMkdir("ux0:data/VitaMA", 0777);
    brls::Logger::debug("sceIoMkdir result: {:#x}", ret);
#endif

    // Load saved settings
    brls::Logger::info("Loading saved settings...");
    bool loaded = loadSettings();
    brls::Logger::info("Settings load result: {}", loaded ? "success" : "failed/not found");

    // Apply settings
    applyTheme();
    applyLogLevel();

    // Persist a freshly minted long-lived token (fires on the WS thread after a
    // direct login upgrades its short-lived token).
    MAClient::instance().setTokenUpgradedCallback([this](const std::string& newToken) {
        brls::sync([this, newToken]() {
            m_authToken = newToken;
            saveSettings();
            brls::Logger::info("App: stored long-lived token");
        });
    });

    // The server rejected our token (expired/invalid). The reconnect loop is
    // already stopped; prompt the user to sign in again.
    MAClient::instance().setAuthFailedCallback([this]() {
        brls::sync([this]() {
            // Only prompt once until a connection succeeds again, so a boot-time
            // expiry (main pushed, then async auth fails) or a stray retry can't
            // stack multiple login screens.
            if (m_reloginPrompted.exchange(true)) return;
            brls::Application::notify("Session expired. Please sign in again.");
            pushLoginActivity();
        });
    });

    m_initialized = true;
    return true;
}

void Application::run() {
    brls::Logger::info("Application::run - isLoggedIn={}, serverUrl={}",
                       isLoggedIn(), m_serverUrl.empty() ? "(empty)" : m_serverUrl);

    // Reconnect the way we last connected: over remote access when the last
    // session was remote (the saved server URL always keeps the direct
    // address), directly otherwise.
    std::string restoreTarget = m_serverUrl;
    if (m_settings.lastConnectionRemote && MAClient::isRemoteId(m_settings.remoteId)) {
        restoreTarget = m_settings.remoteId;
    }

    if (!restoreTarget.empty()) {
        brls::Logger::info("Restoring saved session...");
        // This runs BEFORE brls::Application::mainLoop() below, so main-thread
        // (brls::sync) message dispatch can't run yet - and connect() blocks
        // this thread waiting for the auth handshake. Deliver the socket's
        // messages on the receive thread for the duration of the handshake so
        // server_info -> auth resolves instead of deadlocking for 15s. Restore
        // normal main-thread dispatch once connected.
        MAClient::instance().setDispatchOnMainThread(false);
        bool connected = MAClient::instance().connect(restoreTarget, m_authToken);
        MAClient::instance().setDispatchOnMainThread(true);
        if (connected) {
            brls::Logger::info("Connected to server");
            connectSendspin();
            resolveOwnPlayerId();
            pushMainActivity();
        } else {
            brls::Logger::error("Failed to connect to saved server, showing login");
            pushLoginActivity();
        }
    } else {
        brls::Logger::info("No saved session, showing login screen");
        // Show login screen
        pushLoginActivity();
    }

    // Main loop handled by Borealis
    while (brls::Application::mainLoop()) {
#ifdef __vita__
        // Keep the system (and with it, WiFi) awake. Without this the Vita
        // auto-suspends after a couple of minutes idle, silently killing every
        // socket (MA WebSocket, Sendspin, WebRTC) - the server then logs a
        // 1006 close and marks the player unavailable. The screen may still
        // dim; only auto-suspend is suppressed.
        static int powerTickCounter = 0;
        if (++powerTickCounter >= 300) {  // roughly every 5s at 60fps
            powerTickCounter = 0;
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        }
#endif
    }
}

void Application::shutdown() {
    SendspinClient::instance().disconnect();
    saveSettings();
    m_initialized = false;
    brls::Logger::info("Vita Music Assistant shutting down");
}

void Application::connectSendspin() {
    if (m_serverUrl.empty()) return;

    // Remote access (server is a Remote ID): audio flows over the WebRTC
    // 'sendspin' data channel instead of a direct WebSocket.
    if (MAClient::isRemoteId(m_serverUrl)) {
        std::string clientId = "vita_ma_player";
        std::string clientName = m_settings.sendspinPlayerName;
        if (clientName.empty()) clientName = "PS Vita";
        SendspinClient::instance().connectRemote(clientId, clientName);
        return;
    }

    // Parse scheme + authority from the server URL.
    // e.g. "http://192.168.1.28:8095" or "https://music.example.com"
    std::string scheme;
    std::string authority = m_serverUrl;  // host[:port]

    size_t protoEnd = authority.find("://");
    if (protoEnd != std::string::npos) {
        scheme = authority.substr(0, protoEnd);
        for (auto& c : scheme) c = (char)tolower((unsigned char)c);
        authority = authority.substr(protoEnd + 3);
    }
    // Strip path
    size_t pathStart = authority.find('/');
    if (pathStart != std::string::npos) {
        authority = authority.substr(0, pathStart);
    }

    if (authority.empty()) {
        brls::Logger::error("Sendspin: cannot extract host from server URL");
        return;
    }

    // Host without the port (for the plain local case).
    std::string host = authority;
    size_t portStart = host.find(':');
    if (portStart != std::string::npos) {
        host = host.substr(0, portStart);
    }

    // Use configured player name, falling back to default
    std::string clientId = "vita_ma_player";
    std::string clientName = m_settings.sendspinPlayerName;
    if (clientName.empty()) clientName = "PS Vita";

    if (scheme == "https" || scheme == "wss") {
        // TLS server URL (remote / reverse-proxied setup): reach Sendspin
        // through the same proxy on the same authority at /sendspin, so audio
        // works away from home. Requires the proxy to route /sendspin to the
        // MA host's port 8927 (plain 8927 is normally not reachable remotely).
        std::string url = "wss://" + authority + "/sendspin";
        SendspinClient::instance().connectUrl(url, clientId, clientName);
    } else {
        // Plain local setup: connect directly to the Sendspin port.
        SendspinClient::instance().connect(host, 8927, clientId, clientName);
    }
}

void Application::resolveOwnPlayerId(int attemptsLeft) {
    auto& client = MAClient::instance();
    if (!client.isConnected()) return;

    // The raw Sendspin client id we registered with; MA derives the real
    // player_id from it (sometimes with a provider prefix).
    std::string clientId = App::instance().getPlayerId();
    std::string name = m_settings.sendspinPlayerName;
    if (name.empty()) name = "PS Vita";

    client.getPlayers([this, clientId, name, attemptsLeft](bool success, const Json& result) {
        std::string ownId;
        if (success && result.type() == Json::ARRAY) {
            for (size_t i = 0; i < result.size() && ownId.empty(); i++) {
                const Json& p = result[i];
                std::string pid = p.has("player_id") ? p["player_id"].str() : "";
                if (pid.empty()) continue;
                if (!clientId.empty() &&
                    (pid == clientId || pid.find(clientId) != std::string::npos)) {
                    ownId = pid;
                }
            }
            // Fall back to a name match if the id didn't line up.
            for (size_t i = 0; i < result.size() && ownId.empty(); i++) {
                const Json& p = result[i];
                std::string pn = p.has("name") ? p["name"].str() : "";
                if (pn == name && p.has("player_id")) ownId = p["player_id"].str();
            }
        }

        if (!ownId.empty()) {
            brls::sync([ownId]() {
                App::instance().setPlayerId(ownId);
                brls::Logger::info("Resolved own player_id: {}", ownId);
            });
            return;
        }

        // Not registered yet - retry shortly (MA lists the player a beat after
        // the Sendspin handshake).
        if (attemptsLeft > 0) {
            std::thread([this, attemptsLeft]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                brls::sync([this, attemptsLeft]() {
                    resolveOwnPlayerId(attemptsLeft - 1);
                });
            }).detach();
        }
    });
}

void Application::pushLoginActivity() {
    brls::Application::pushActivity(new LoginActivity());
}

void Application::pushMainActivity() {
    // A successful connection re-arms the "session expired" prompt.
    m_reloginPrompted.store(false);
    brls::Application::pushActivity(new MainActivity());
}

void Application::pushPlayerActivity(const std::string& queueId) {
    brls::Application::pushActivity(new PlayerActivity(queueId));
}

void Application::applyTheme() {
    brls::ThemeVariant variant;

    switch (m_settings.theme) {
        case AppTheme::LIGHT:
            variant = brls::ThemeVariant::LIGHT;
            break;
        case AppTheme::DARK:
            variant = brls::ThemeVariant::DARK;
            break;
        case AppTheme::SYSTEM:
        default:
            // Default to dark for Vita
            variant = brls::ThemeVariant::DARK;
            break;
    }

    brls::Application::getPlatform()->setThemeVariant(variant);
    brls::Logger::info("Applied theme: {}", getThemeString(m_settings.theme));
}

void Application::applyLogLevel() {
    if (m_settings.debugLogging) {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        brls::Logger::info("Debug logging enabled");
    } else {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
        brls::Logger::info("Debug logging disabled");
    }
}

std::string Application::getQualityString(AudioQuality quality) {
    switch (quality) {
        case AudioQuality::LOSSLESS: return "Lossless (FLAC)";
        case AudioQuality::HIGH: return "High (320 Kbps)";
        case AudioQuality::NORMAL: return "Normal (192 Kbps)";
        case AudioQuality::LOW: return "Low (96 Kbps)";
        default: return "Unknown";
    }
}

std::string Application::getThemeString(AppTheme theme) {
    switch (theme) {
        case AppTheme::SYSTEM: return "System";
        case AppTheme::LIGHT: return "Light";
        case AppTheme::DARK: return "Dark";
        default: return "Unknown";
    }
}

bool Application::loadSettings() {
#ifdef __vita__
    brls::Logger::debug("loadSettings: Opening {}", SETTINGS_PATH);

    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) {
        brls::Logger::debug("No settings file found (error: {:#x})", fd);
        return false;
    }

    // Get file size
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    brls::Logger::debug("loadSettings: File size = {}", size);

    if (size <= 0 || size > 16384) {
        brls::Logger::error("loadSettings: Invalid file size");
        sceIoClose(fd);
        return false;
    }

    std::string content;
    content.resize(size);
    sceIoRead(fd, &content[0], size);
    sceIoClose(fd);

    brls::Logger::debug("loadSettings: Read {} bytes", content.length());

    // Simple JSON parsing for strings (handles whitespace after colon)
    auto extractString = [&content](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        // Skip whitespace after colon
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        // Expect opening quote
        if (pos >= content.length() || content[pos] != '"') return "";
        pos++; // Skip opening quote
        size_t end = content.find("\"", pos);
        if (end == std::string::npos) return "";
        return content.substr(pos, end - pos);
    };

    // Parse integers
    auto extractInt = [&content](const std::string& key) -> int {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        size_t end = content.find_first_of(",}\n", pos);
        if (end == std::string::npos) return 0;
        return atoi(content.substr(pos, end - pos).c_str());
    };

    // Parse booleans
    auto extractBool = [&content](const std::string& key, bool defaultVal = false) -> bool {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return defaultVal;
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        return (content.substr(pos, 4) == "true");
    };

    // Load authentication
    m_authToken = extractString("authToken");
    m_serverUrl = extractString("serverUrl");
    m_username = extractString("username");

    brls::Logger::info("loadSettings: authToken={}, serverUrl={}, username={}",
                       m_authToken.empty() ? "(empty)" : "(set)",
                       m_serverUrl.empty() ? "(empty)" : m_serverUrl,
                       m_username.empty() ? "(empty)" : m_username);

    // Load UI settings
    m_settings.theme = static_cast<AppTheme>(extractInt("theme"));
    m_settings.debugLogging = extractBool("debugLogging", true);
    m_settings.showDebugTab = extractBool("showDebugTab", true);

    // Load layout settings
    m_settings.collapseSidebar = extractBool("collapseSidebar", false);
    m_settings.sidebarOrder = extractString("sidebarOrder");

    // Load content display settings
    m_settings.showPlaylists = extractBool("showPlaylists", true);
    m_settings.hideTitlesInGrid = extractBool("hideTitlesInGrid", false);

    // Load playback settings
    m_settings.autoPlayNext = extractBool("autoPlayNext", true);
    m_settings.seekInterval = extractInt("seekInterval");
    if (m_settings.seekInterval <= 0) m_settings.seekInterval = 10;
    m_settings.controlsAutoHideSeconds = extractInt("controlsAutoHideSeconds");
    if (m_settings.controlsAutoHideSeconds < 0) m_settings.controlsAutoHideSeconds = 5;

    // Load audio quality settings
    m_settings.audioQuality = static_cast<AudioQuality>(extractInt("audioQuality"));

    // Load network settings
    m_settings.connectionTimeout = extractInt("connectionTimeout");
    if (m_settings.connectionTimeout <= 0) m_settings.connectionTimeout = 180; // 3 minutes default

    // Load music settings
    int trackAction = extractInt("trackDefaultAction");
    if (trackAction >= 0 && trackAction <= 4) {
        m_settings.trackDefaultAction = static_cast<TrackDefaultAction>(trackAction);
    }
    m_settings.backgroundMusic = extractBool("backgroundMusic", true);

    // Load player settings
    m_settings.localPlayback = extractBool("localPlayback", true);
    m_settings.nativeAudio = extractBool("nativeAudio", true);
    m_settings.sendspinPlayerName = extractString("sendspinPlayerName");
    if (m_settings.sendspinPlayerName.empty()) m_settings.sendspinPlayerName = "PS Vita";
    m_settings.selectedPlayerId = extractString("selectedPlayerId");

    // Remote access
    m_settings.remoteId = extractString("remoteId");
    m_settings.remoteAccessEnabled = extractBool("remoteAccessEnabled", false);
    m_settings.lastConnectionRemote = extractBool("lastConnectionRemote", false);

    brls::Logger::info("Settings loaded successfully");
    return !m_authToken.empty();
#else
    return false;
#endif
}

bool Application::saveSettings() {
#ifdef __vita__
    brls::Logger::info("saveSettings: Saving to {}", SETTINGS_PATH);
    brls::Logger::debug("saveSettings: authToken={}, serverUrl={}, username={}",
                        m_authToken.empty() ? "(empty)" : "(set)",
                        m_serverUrl.empty() ? "(empty)" : m_serverUrl,
                        m_username.empty() ? "(empty)" : m_username);

    // Create JSON content
    std::string json = "{\n";

    // Authentication
    json += "  \"authToken\": \"" + m_authToken + "\",\n";
    json += "  \"serverUrl\": \"" + m_serverUrl + "\",\n";
    json += "  \"username\": \"" + m_username + "\",\n";

    // UI settings
    json += "  \"theme\": " + std::to_string(static_cast<int>(m_settings.theme)) + ",\n";
    json += "  \"debugLogging\": " + std::string(m_settings.debugLogging ? "true" : "false") + ",\n";
    json += "  \"showDebugTab\": " + std::string(m_settings.showDebugTab ? "true" : "false") + ",\n";

    // Layout settings
    json += "  \"collapseSidebar\": " + std::string(m_settings.collapseSidebar ? "true" : "false") + ",\n";
    json += "  \"sidebarOrder\": \"" + m_settings.sidebarOrder + "\",\n";

    // Content display settings
    json += "  \"showPlaylists\": " + std::string(m_settings.showPlaylists ? "true" : "false") + ",\n";
    json += "  \"hideTitlesInGrid\": " + std::string(m_settings.hideTitlesInGrid ? "true" : "false") + ",\n";

    // Playback settings
    json += "  \"autoPlayNext\": " + std::string(m_settings.autoPlayNext ? "true" : "false") + ",\n";
    json += "  \"seekInterval\": " + std::to_string(m_settings.seekInterval) + ",\n";
    json += "  \"controlsAutoHideSeconds\": " + std::to_string(m_settings.controlsAutoHideSeconds) + ",\n";

    // Audio quality settings
    json += "  \"audioQuality\": " + std::to_string(static_cast<int>(m_settings.audioQuality)) + ",\n";

    // Network settings
    json += "  \"connectionTimeout\": " + std::to_string(m_settings.connectionTimeout) + ",\n";

    // Music settings
    json += "  \"trackDefaultAction\": " + std::to_string(static_cast<int>(m_settings.trackDefaultAction)) + ",\n";
    json += "  \"backgroundMusic\": " + std::string(m_settings.backgroundMusic ? "true" : "false") + ",\n";

    // Player settings
    json += "  \"localPlayback\": " + std::string(m_settings.localPlayback ? "true" : "false") + ",\n";
    json += "  \"nativeAudio\": " + std::string(m_settings.nativeAudio ? "true" : "false") + ",\n";
    json += "  \"sendspinPlayerName\": \"" + m_settings.sendspinPlayerName + "\",\n";
    json += "  \"selectedPlayerId\": \"" + m_settings.selectedPlayerId + "\",\n";
    json += "  \"remoteId\": \"" + m_settings.remoteId + "\",\n";
    json += "  \"remoteAccessEnabled\": " + std::string(m_settings.remoteAccessEnabled ? "true" : "false") + ",\n";
    json += "  \"lastConnectionRemote\": " + std::string(m_settings.lastConnectionRemote ? "true" : "false") + "\n";

    json += "}\n";

    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        brls::Logger::error("Failed to open settings file for writing: {:#x}", fd);
        return false;
    }

    int written = sceIoWrite(fd, json.c_str(), json.length());
    sceIoClose(fd);

    if (written == (int)json.length()) {
        brls::Logger::info("Settings saved successfully ({} bytes)", written);
        return true;
    } else {
        brls::Logger::error("Failed to write settings: only {} of {} bytes written", written, json.length());
        return false;
    }
#else
    return false;
#endif
}

} // namespace vita_ma
