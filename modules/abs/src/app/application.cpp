/**
 * VitaABS - Application implementation
 */

#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/downloads_manager.hpp"
#include "activity/login_activity.hpp"
#include "activity/main_activity.hpp"
#include "activity/player_activity.hpp"
#include "platform/platform.hpp"

#include <borealis.hpp>
#include <fstream>
#include <cstring>
#include <cmath>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace vitaabs {

static std::string getSettingsPath() { return platform::path("settings.json"); }

Application& Application::getInstance() {
    static Application instance;
    return instance;
}

bool Application::init() {
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    brls::Logger::info("VitaABS {} initializing...", VITA_ABS_VERSION);

    platform::createDirRecursive(platform::dataDir());

    // Load saved settings
    brls::Logger::info("Loading saved settings...");
    bool loaded = loadSettings();
    brls::Logger::info("Settings load result: {}", loaded ? "success" : "failed/not found");

    // Apply settings
    applyTheme();
    applyLogLevel();

    m_initialized = true;
    return true;
}

void Application::run() {
    brls::Logger::info("Application::run - isLoggedIn={}, serverUrl={}",
                       isLoggedIn(), m_serverUrl.empty() ? "(empty)" : m_serverUrl);

    // Initialize downloads manager to check for offline content
    DownloadsManager::getInstance().init();

    // Check if we have saved login credentials
    if (isLoggedIn() && (!m_serverUrl.empty() || !m_localServerUrl.empty() || !m_remoteServerUrl.empty())) {
        brls::Logger::info("Restoring saved session...");
        // Set auth tokens
        AudiobookshelfClient::getInstance().setAuthToken(m_authToken);
        AudiobookshelfClient::getInstance().setRefreshToken(m_refreshToken);

        // Try to connect, with automatic URL switching if needed
        if (tryConnectToServer()) {
            brls::Logger::info("Restored session, connected to {}", m_serverUrl);

            // Auto-sync progress both directions on startup
            auto& dm = DownloadsManager::getInstance();
            if (!dm.getDownloads().empty()) {
                // First push local progress to server (offline playback resume points)
                dm.syncProgressToServer();
                // Then pull latest progress from server (played on other devices)
                dm.syncProgressFromServer();
                // Resume incomplete downloads
                dm.resumeDownloadsIfNeeded();
            }

            pushMainActivity();
        } else {
            // Connection failed - could be offline
            // Check if we have downloads, if so go to main activity (offline mode)
            auto downloads = DownloadsManager::getInstance().getDownloads();
            if (!downloads.empty()) {
                brls::Logger::info("Offline with {} downloads, going to main activity", downloads.size());
                pushMainActivity();
            } else {
                brls::Logger::error("Cannot connect and no downloads, showing login");
                pushLoginActivity();
            }
        }
    } else {
        // No saved session - check if we have downloads for offline mode
        auto downloads = DownloadsManager::getInstance().getDownloads();
        if (!downloads.empty()) {
            brls::Logger::info("No session but {} downloads exist, going to main activity", downloads.size());
            pushMainActivity();
        } else {
            brls::Logger::info("No saved session, showing login screen");
            pushLoginActivity();
        }
    }

    // Main loop handled by Borealis
    while (brls::Application::mainLoop()) {
        // Application keeps running
    }
}

void Application::shutdown() {
    // Save any pending download state before shutting down
    DownloadsManager::getInstance().saveState();
    saveSettings();
    m_initialized = false;
    brls::Logger::info("VitaABS shutting down");
}

void Application::pushLoginActivity() {
    brls::Application::pushActivity(new LoginActivity());
}

void Application::pushMainActivity() {
    brls::Application::pushActivity(new MainActivity());
}

void Application::pushPlayerActivity(const std::string& itemId, const std::string& episodeId,
                                      float startTime) {
    brls::Application::pushActivity(new PlayerActivity(itemId, episodeId, startTime));
}

void Application::pushPlayerActivityWithFile(const std::string& itemId, const std::string& episodeId,
                                              const std::string& preDownloadedPath, float startTime) {
    brls::Application::pushActivity(new PlayerActivity(itemId, episodeId, preDownloadedPath, startTime));
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

    // brls::Slider reads these in its constructor, so every slider the app
    // builds afterwards (the player's progress bar above all) picks up the
    // Audiobookshelf accent instead of borealis' stock blue. Set on both
    // variants — addColor overwrites, so this is safe to re-run.
    const NVGcolor accent      = nvgRGB(0xd7, 0x9b, 0x5a);
    const NVGcolor trackEmpty  = nvgRGB(0x26, 0x2a, 0x34);
    const NVGcolor knob        = nvgRGB(0xf3, 0xe3, 0xd1);
    for (brls::Theme* t : { &brls::Theme::getLightTheme(), &brls::Theme::getDarkTheme() }) {
        t->addColor("brls/slider/line_filled", accent);
        t->addColor("brls/slider/line_empty", trackEmpty);
        t->addColor("brls/slider/pointer_color", knob);
        t->addColor("brls/slider/pointer_border_color", accent);
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


std::string Application::getThemeString(AppTheme theme) {
    switch (theme) {
        case AppTheme::SYSTEM: return "System";
        case AppTheme::LIGHT: return "Light";
        case AppTheme::DARK: return "Dark";
        default: return "Unknown";
    }
}

std::string Application::getPlaybackSpeedString(PlaybackSpeed speed) {
    switch (speed) {
        case PlaybackSpeed::SPEED_0_5X: return "0.5x";
        case PlaybackSpeed::SPEED_0_75X: return "0.75x";
        case PlaybackSpeed::SPEED_1X: return "1x (Normal)";
        case PlaybackSpeed::SPEED_1_25X: return "1.25x";
        case PlaybackSpeed::SPEED_1_5X: return "1.5x";
        case PlaybackSpeed::SPEED_1_75X: return "1.75x";
        case PlaybackSpeed::SPEED_2X: return "2x";
        default: return "Unknown";
    }
}

std::string Application::getSleepTimerString(SleepTimer timer) {
    switch (timer) {
        case SleepTimer::OFF: return "Off";
        case SleepTimer::MINUTES_5: return "5 minutes";
        case SleepTimer::MINUTES_10: return "10 minutes";
        case SleepTimer::MINUTES_15: return "15 minutes";
        case SleepTimer::MINUTES_30: return "30 minutes";
        case SleepTimer::MINUTES_45: return "45 minutes";
        case SleepTimer::MINUTES_60: return "60 minutes";
        case SleepTimer::END_OF_CHAPTER: return "End of Chapter";
        default: return "Unknown";
    }
}

float Application::getPlaybackSpeedValue(PlaybackSpeed speed) {
    switch (speed) {
        case PlaybackSpeed::SPEED_0_5X: return 0.5f;
        case PlaybackSpeed::SPEED_0_75X: return 0.75f;
        case PlaybackSpeed::SPEED_1X: return 1.0f;
        case PlaybackSpeed::SPEED_1_25X: return 1.25f;
        case PlaybackSpeed::SPEED_1_5X: return 1.5f;
        case PlaybackSpeed::SPEED_1_75X: return 1.75f;
        case PlaybackSpeed::SPEED_2X: return 2.0f;
        default: return 1.0f;
    }
}

std::string Application::formatTime(float seconds) {
    if (seconds < 0) seconds = 0;

    int totalSeconds = (int)seconds;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;

    char buffer[32];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    } else {
        snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }
    return buffer;
}

std::string Application::formatDuration(float seconds) {
    if (seconds < 0) seconds = 0;

    int totalSeconds = (int)seconds;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;

    char buffer[64];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%dh %dm", hours, minutes);
    } else {
        snprintf(buffer, sizeof(buffer), "%d min", minutes);
    }
    return buffer;
}

bool Application::loadSettings() {
    std::string settingsPath = getSettingsPath();
    brls::Logger::debug("loadSettings: Opening {}", settingsPath);

    std::vector<uint8_t> data = platform::readFile(settingsPath);
    if (data.empty()) {
        brls::Logger::debug("No settings file found");
        return false;
    }

    std::string content(data.begin(), data.end());
    brls::Logger::debug("loadSettings: Read {} bytes", content.length());

    // Simple JSON parsing for strings
    auto extractString = [&content](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        if (pos >= content.length() || content[pos] != '"') return "";
        pos++;
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
    m_refreshToken = extractString("refreshToken");
    m_serverUrl = extractString("serverUrl");
    m_localServerUrl = extractString("localServerUrl");
    m_remoteServerUrl = extractString("remoteServerUrl");
    m_useLocalUrl = extractBool("useLocalUrl", true);
    m_username = extractString("username");
    m_currentLibraryId = extractString("currentLibraryId");

    // Migration: if no local/remote URLs but serverUrl exists, use it as local
    if (m_localServerUrl.empty() && m_remoteServerUrl.empty() && !m_serverUrl.empty()) {
        m_localServerUrl = m_serverUrl;
        m_useLocalUrl = true;
    }

    brls::Logger::info("loadSettings: authToken={}, serverUrl={}, username={}",
                       m_authToken.empty() ? "(empty)" : "(set)",
                       m_serverUrl.empty() ? "(empty)" : m_serverUrl,
                       m_username.empty() ? "(empty)" : m_username);

    // Load UI settings
    m_settings.theme = static_cast<AppTheme>(extractInt("theme"));
    m_settings.debugLogging = extractBool("debugLogging", true);

    // Load content display settings
    m_settings.showCollections = extractBool("showCollections", true);
    m_settings.showSeries = extractBool("showSeries", true);
    m_settings.showAuthors = extractBool("showAuthors", true);
    m_settings.showProgress = extractBool("showProgress", true);
    m_settings.showOnlyDownloaded = extractBool("showOnlyDownloaded", false);

    // Load home tab settings
    m_settings.showHomeTab = extractBool("showHomeTab", true);
    m_settings.maxRecentEpisodes = extractInt("maxRecentEpisodes");
    if (m_settings.maxRecentEpisodes <= 0) m_settings.maxRecentEpisodes = 10;

    // Load playback settings
    m_settings.resumePlayback = extractBool("resumePlayback", true);
    m_settings.playbackSpeed = static_cast<PlaybackSpeed>(extractInt("playbackSpeed"));
    m_settings.sleepTimer = static_cast<SleepTimer>(extractInt("sleepTimer"));
    m_settings.seekInterval = extractInt("seekInterval");
    if (m_settings.seekInterval <= 0) m_settings.seekInterval = 30;
    m_settings.longSeekInterval = extractInt("longSeekInterval");
    if (m_settings.longSeekInterval <= 0) m_settings.longSeekInterval = 300;

    // Load podcast settings
    m_settings.podcastAutoComplete = static_cast<AutoCompleteThreshold>(extractInt("podcastAutoComplete"));

    // Load audio settings
    m_settings.boostVolume = extractBool("boostVolume", false);
    m_settings.volumeBoostDb = extractInt("volumeBoostDb");

    // Load chapter settings
    m_settings.showChapterList = extractBool("showChapterList", true);
    m_settings.skipChapterTransitions = extractBool("skipChapterTransitions", false);

    // Load network settings
    m_settings.connectionTimeout = extractInt("connectionTimeout");
    if (m_settings.connectionTimeout <= 0) m_settings.connectionTimeout = 30;
    m_settings.autoSwitchUrl = extractBool("autoSwitchUrl", true);

    // Load in-app update settings
    m_settings.autoCheckUpdates = extractBool("autoCheckUpdates", true);
    m_settings.skippedUpdateVersion = extractString("skippedUpdateVersion");

    // Load download settings
    m_settings.autoStartDownloads = extractBool("autoStartDownloads", true);
    m_settings.deleteAfterFinish = extractBool("deleteAfterFinish", false);
    m_settings.downloadOnPlay = extractBool("downloadOnPlay", false);

    // Load player UI settings
    m_settings.showDownloadProgress = extractBool("showDownloadProgress", true);

    // Load sleep/power settings
    m_settings.preventSleep = extractBool("preventSleep", true);
    m_settings.pauseOnHeadphoneDisconnect = extractBool("pauseOnHeadphoneDisconnect", true);

    brls::Logger::info("Settings loaded successfully");
    return !m_authToken.empty();
}

bool Application::saveSettings() {
    std::string settingsPath = getSettingsPath();
    brls::Logger::info("saveSettings: Saving to {}", settingsPath);
    brls::Logger::debug("saveSettings: authToken={}, serverUrl={}, username={}",
                        m_authToken.empty() ? "(empty)" : "(set)",
                        m_serverUrl.empty() ? "(empty)" : m_serverUrl,
                        m_username.empty() ? "(empty)" : m_username);

    // Create JSON content
    std::string json = "{\n";

    // Authentication
    json += "  \"authToken\": \"" + m_authToken + "\",\n";
    json += "  \"refreshToken\": \"" + m_refreshToken + "\",\n";
    json += "  \"serverUrl\": \"" + m_serverUrl + "\",\n";
    json += "  \"localServerUrl\": \"" + m_localServerUrl + "\",\n";
    json += "  \"remoteServerUrl\": \"" + m_remoteServerUrl + "\",\n";
    json += "  \"useLocalUrl\": " + std::string(m_useLocalUrl ? "true" : "false") + ",\n";
    json += "  \"username\": \"" + m_username + "\",\n";
    json += "  \"currentLibraryId\": \"" + m_currentLibraryId + "\",\n";

    // UI settings
    json += "  \"theme\": " + std::to_string(static_cast<int>(m_settings.theme)) + ",\n";
    json += "  \"debugLogging\": " + std::string(m_settings.debugLogging ? "true" : "false") + ",\n";

    // Content display settings
    json += "  \"showCollections\": " + std::string(m_settings.showCollections ? "true" : "false") + ",\n";
    json += "  \"showSeries\": " + std::string(m_settings.showSeries ? "true" : "false") + ",\n";
    json += "  \"showAuthors\": " + std::string(m_settings.showAuthors ? "true" : "false") + ",\n";
    json += "  \"showProgress\": " + std::string(m_settings.showProgress ? "true" : "false") + ",\n";
    json += "  \"showOnlyDownloaded\": " + std::string(m_settings.showOnlyDownloaded ? "true" : "false") + ",\n";

    // Home tab settings
    json += "  \"showHomeTab\": " + std::string(m_settings.showHomeTab ? "true" : "false") + ",\n";
    json += "  \"maxRecentEpisodes\": " + std::to_string(m_settings.maxRecentEpisodes) + ",\n";

    // Playback settings
    json += "  \"resumePlayback\": " + std::string(m_settings.resumePlayback ? "true" : "false") + ",\n";
    json += "  \"playbackSpeed\": " + std::to_string(static_cast<int>(m_settings.playbackSpeed)) + ",\n";
    json += "  \"sleepTimer\": " + std::to_string(static_cast<int>(m_settings.sleepTimer)) + ",\n";
    json += "  \"seekInterval\": " + std::to_string(m_settings.seekInterval) + ",\n";
    json += "  \"longSeekInterval\": " + std::to_string(m_settings.longSeekInterval) + ",\n";

    // Podcast settings
    json += "  \"podcastAutoComplete\": " + std::to_string(static_cast<int>(m_settings.podcastAutoComplete)) + ",\n";

    // Audio settings
    json += "  \"boostVolume\": " + std::string(m_settings.boostVolume ? "true" : "false") + ",\n";
    json += "  \"volumeBoostDb\": " + std::to_string(m_settings.volumeBoostDb) + ",\n";

    // Chapter settings
    json += "  \"showChapterList\": " + std::string(m_settings.showChapterList ? "true" : "false") + ",\n";
    json += "  \"skipChapterTransitions\": " + std::string(m_settings.skipChapterTransitions ? "true" : "false") + ",\n";

    // Network settings
    json += "  \"connectionTimeout\": " + std::to_string(m_settings.connectionTimeout) + ",\n";
    json += "  \"autoSwitchUrl\": " + std::string(m_settings.autoSwitchUrl ? "true" : "false") + ",\n";
    json += "  \"autoCheckUpdates\": " + std::string(m_settings.autoCheckUpdates ? "true" : "false") + ",\n";
    json += "  \"skippedUpdateVersion\": \"" + m_settings.skippedUpdateVersion + "\",\n";

    // Download settings
    json += "  \"autoStartDownloads\": " + std::string(m_settings.autoStartDownloads ? "true" : "false") + ",\n";
    json += "  \"deleteAfterFinish\": " + std::string(m_settings.deleteAfterFinish ? "true" : "false") + ",\n";
    json += "  \"downloadOnPlay\": " + std::string(m_settings.downloadOnPlay ? "true" : "false") + ",\n";

    // Player UI settings
    json += "  \"showDownloadProgress\": " + std::string(m_settings.showDownloadProgress ? "true" : "false") + ",\n";

    // Sleep/power settings
    json += "  \"preventSleep\": " + std::string(m_settings.preventSleep ? "true" : "false") + ",\n";
    json += "  \"pauseOnHeadphoneDisconnect\": " + std::string(m_settings.pauseOnHeadphoneDisconnect ? "true" : "false") + "\n";

    json += "}\n";

    if (platform::writeFile(settingsPath, json)) {
        brls::Logger::info("Settings saved successfully ({} bytes)", json.length());
        return true;
    } else {
        brls::Logger::error("Failed to write settings file");
        return false;
    }
}

void Application::setBackgroundDownloadProgress(const BackgroundDownloadProgress& progress) {
    std::lock_guard<std::mutex> lock(m_bgDownloadMutex);
    m_bgDownloadProgress = progress;
}

BackgroundDownloadProgress Application::getBackgroundDownloadProgress() const {
    std::lock_guard<std::mutex> lock(m_bgDownloadMutex);
    return m_bgDownloadProgress;
}

void Application::clearBackgroundDownloadProgress() {
    std::lock_guard<std::mutex> lock(m_bgDownloadMutex);
    m_bgDownloadProgress = BackgroundDownloadProgress();
}

void Application::setUseLocalUrl(bool useLocal) {
    m_useLocalUrl = useLocal;
    // Update the active server URL based on selection
    if (useLocal && !m_localServerUrl.empty()) {
        m_serverUrl = m_localServerUrl;
    } else if (!useLocal && !m_remoteServerUrl.empty()) {
        m_serverUrl = m_remoteServerUrl;
    }
    // Update the client as well
    AudiobookshelfClient::getInstance().setServerUrl(m_serverUrl);
    brls::Logger::info("Switched to {} URL: {}", useLocal ? "local" : "remote", m_serverUrl);
}

bool Application::tryConnectToServer() {
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Try the currently selected URL first
    std::string primaryUrl = m_useLocalUrl ? m_localServerUrl : m_remoteServerUrl;
    std::string fallbackUrl = m_useLocalUrl ? m_remoteServerUrl : m_localServerUrl;

    if (!primaryUrl.empty()) {
        brls::Logger::info("Trying primary {} URL: {}", m_useLocalUrl ? "local" : "remote", primaryUrl);
        client.setServerUrl(primaryUrl);
        if (client.validateToken()) {
            m_serverUrl = primaryUrl;
            brls::Logger::info("Connected to primary URL");
            return true;
        }
    }

    // Try fallback URL if auto-switch is enabled and both URLs are configured
    if (m_settings.autoSwitchUrl && !fallbackUrl.empty()) {
        brls::Logger::info("Auto-switch: Primary failed, trying fallback URL: {}", fallbackUrl);
        client.setServerUrl(fallbackUrl);
        if (client.validateToken()) {
            m_serverUrl = fallbackUrl;
            m_useLocalUrl = !m_useLocalUrl;  // Switch to the working URL
            brls::Logger::info("Auto-switch: Connected to fallback URL, switched to {}", m_useLocalUrl ? "local" : "remote");
            saveSettings();
            return true;
        }
    }

    brls::Logger::error("Failed to connect to any server URL");
    return false;
}

} // namespace vitaabs
