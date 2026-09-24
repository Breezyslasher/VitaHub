/**
 * VitaABS - Audiobookshelf Client for PlayStation Vita
 * Borealis-based Application
 */

#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <cstdint>

// Application version
#define VITA_ABS_VERSION "1.0.0"
#define VITA_ABS_VERSION_NUM 100

// Client identification
#define ABS_CLIENT_ID "vita-abs-client-001"
#define ABS_CLIENT_NAME "VitaABS"
#define ABS_CLIENT_VERSION VITA_ABS_VERSION
#define ABS_PLATFORM "PlayStation Vita"
#define ABS_DEVICE "PS Vita"

namespace vitaabs {

// Theme options
enum class AppTheme {
    SYSTEM = 0,  // Follow system setting
    LIGHT = 1,
    DARK = 2
};

// Playback speed options
enum class PlaybackSpeed {
    SPEED_0_5X = 0,    // 0.5x
    SPEED_0_75X = 1,   // 0.75x
    SPEED_1X = 2,      // Normal (1x)
    SPEED_1_25X = 3,   // 1.25x
    SPEED_1_5X = 4,    // 1.5x
    SPEED_1_75X = 5,   // 1.75x
    SPEED_2X = 6       // 2x
};

// Sleep timer options
enum class SleepTimer {
    OFF = 0,
    MINUTES_5 = 1,
    MINUTES_10 = 2,
    MINUTES_15 = 3,
    MINUTES_30 = 4,
    MINUTES_45 = 5,
    MINUTES_60 = 6,
    END_OF_CHAPTER = 7
};

// Auto-complete threshold for podcasts (when to mark as finished)
enum class AutoCompleteThreshold {
    DISABLED = 0,       // Never auto-complete
    LAST_10_SEC = 1,    // Last 10 seconds
    LAST_30_SEC = 2,    // Last 30 seconds
    LAST_60_SEC = 3,    // Last 60 seconds
    PERCENT_90 = 4,     // 90% complete
    PERCENT_95 = 5,     // 95% complete
    PERCENT_99 = 6      // 99% complete
};

// Background download progress tracking
struct BackgroundDownloadProgress {
    bool active = false;          // Whether a background download is in progress
    std::string itemId;           // Item being downloaded
    int currentTrack = 0;         // Current track number (1-based)
    int totalTracks = 0;          // Total number of tracks
    int64_t downloadedBytes = 0;  // Total bytes downloaded so far
    int64_t totalBytes = 0;       // Total bytes to download
    std::string status;           // Current status message
};

// Application settings structure
struct AppSettings {
    // UI Settings
    AppTheme theme = AppTheme::DARK;
    bool debugLogging = true;

    // Content Display Settings
    bool showCollections = true;
    bool showSeries = true;
    bool showAuthors = true;
    bool showProgress = true;          // Show progress bars on items
    bool showOnlyDownloaded = false;   // Show only downloaded items in library

    // Home Tab Settings
    bool showHomeTab = true;           // Show the Home tab
    int maxRecentEpisodes = 10;        // Max recently added episodes to show (0 = unlimited)

    // Playback Settings
    bool resumePlayback = true;        // Resume from last position
    PlaybackSpeed playbackSpeed = PlaybackSpeed::SPEED_1X;
    SleepTimer sleepTimer = SleepTimer::OFF;
    int seekInterval = 30;             // Skip forward/back interval in seconds
    int longSeekInterval = 300;        // Long skip interval (5 minutes)

    // Podcast Settings
    AutoCompleteThreshold podcastAutoComplete = AutoCompleteThreshold::LAST_30_SEC;  // When to mark podcasts as complete

    // Audio Settings
    bool boostVolume = false;          // Volume boost for quiet audiobooks
    int volumeBoostDb = 0;             // Volume boost in dB (0-12)

    // Chapter Settings
    bool showChapterList = true;       // Show chapter list in player
    bool skipChapterTransitions = false; // Skip chapter intro/outro silence

    // Network Settings
    int connectionTimeout = 30;        // seconds (applied to HTTP client)
    bool autoSwitchUrl = true;         // Auto-switch between local/remote URL on failure

    // In-app updates
    bool autoCheckUpdates = true;          // Silent check on startup
    std::string skippedUpdateVersion;      // Release tag the user chose to skip

    // Download Settings
    bool autoStartDownloads = true;
    bool deleteAfterFinish = false;    // Delete downloaded book after finishing
    bool downloadOnPlay = false;       // Queue download when pressing play (in addition to streaming)

    // Player UI Settings
    bool showDownloadProgress = true;  // Show background download progress in player for multi-file books

    // Sleep/Power Settings
    bool preventSleep = true;          // Prevent screen sleep during playback
    bool pauseOnHeadphoneDisconnect = true;
};

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
    void pushPlayerActivity(const std::string& itemId, const std::string& episodeId = "",
                            float startTime = -1.0f);
    // Push player with pre-downloaded file (downloaded before player push)
    void pushPlayerActivityWithFile(const std::string& itemId, const std::string& episodeId,
                                    const std::string& preDownloadedPath, float startTime = -1.0f);

    // Authentication state
    bool isLoggedIn() const { return !m_authToken.empty(); }
    const std::string& getAuthToken() const { return m_authToken; }
    void setAuthToken(const std::string& token) { m_authToken = token; }
    const std::string& getRefreshToken() const { return m_refreshToken; }
    void setRefreshToken(const std::string& token) { m_refreshToken = token; }

    // Server URL management (supports local and remote URLs)
    const std::string& getServerUrl() const { return m_serverUrl; }
    void setServerUrl(const std::string& url) { m_serverUrl = url; }
    const std::string& getLocalServerUrl() const { return m_localServerUrl; }
    void setLocalServerUrl(const std::string& url) { m_localServerUrl = url; }
    const std::string& getRemoteServerUrl() const { return m_remoteServerUrl; }
    void setRemoteServerUrl(const std::string& url) { m_remoteServerUrl = url; }
    bool isUsingLocalUrl() const { return m_useLocalUrl; }
    void setUseLocalUrl(bool useLocal);
    bool tryConnectToServer(); // Try to connect, auto-switching URLs if needed

    // Settings persistence
    bool loadSettings();
    bool saveSettings();

    // User info
    const std::string& getUsername() const { return m_username; }
    void setUsername(const std::string& name) { m_username = name; }

    // Current library (for context)
    const std::string& getCurrentLibraryId() const { return m_currentLibraryId; }
    void setCurrentLibraryId(const std::string& id) { m_currentLibraryId = id; }

    // Application settings access
    AppSettings& getSettings() { return m_settings; }
    const AppSettings& getSettings() const { return m_settings; }

    // Apply theme
    void applyTheme();

    // Apply log level based on settings
    void applyLogLevel();

    // Get quality string for display

    static std::string getThemeString(AppTheme theme);
    static std::string getPlaybackSpeedString(PlaybackSpeed speed);
    static std::string getSleepTimerString(SleepTimer timer);
    static float getPlaybackSpeedValue(PlaybackSpeed speed);

    // Format time for display (seconds to HH:MM:SS or MM:SS)
    static std::string formatTime(float seconds);
    static std::string formatDuration(float seconds);

    // Background download progress tracking (for multi-file audiobooks)
    void setBackgroundDownloadProgress(const BackgroundDownloadProgress& progress);
    BackgroundDownloadProgress getBackgroundDownloadProgress() const;
    void clearBackgroundDownloadProgress();

private:
    Application() = default;
    ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool m_initialized = false;
    std::string m_authToken;
    std::string m_refreshToken;
    std::string m_serverUrl;          // Currently active URL
    std::string m_localServerUrl;     // Local network URL (e.g., http://192.168.1.100:13378)
    std::string m_remoteServerUrl;    // Remote URL (e.g., https://abs.example.com)
    bool m_useLocalUrl = true;        // Whether currently using local URL
    std::string m_username;
    std::string m_currentLibraryId;
    AppSettings m_settings;

    // Background download progress tracking
    mutable std::mutex m_bgDownloadMutex;
    BackgroundDownloadProgress m_bgDownloadProgress;
};

} // namespace vitaabs
