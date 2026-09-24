#pragma once

/**
 * Vita Music Assistant - Music Assistant Client for PlayStation Vita
 * Based on switchfin architecture (https://github.com/dragonflylee/switchfin)
 */

#include <string>
#include <vector>

// Application version
#define VMA_VERSION "2.0.0"
#define VMA_VERSION_NUM 200

// Screen dimensions (PS Vita)
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

// Music Assistant client identification
#define MA_CLIENT_ID "vita-music-assistant-001"
#define MA_CLIENT_NAME "Vita Music Assistant"
#define MA_CLIENT_VERSION VMA_VERSION
#define MA_PLATFORM "PlayStation Vita"
#define MA_DEVICE "PS Vita"

namespace vita_ma {

// Server info received from Music Assistant
struct ServerInfo {
    std::string server_id;
    std::string server_name;
    std::string server_version;
    int schema_version = 0;
    int min_supported_schema = 0;
};

// Media types (Music Assistant - audio only)
enum class MediaType {
    UNKNOWN,
    ARTIST,
    ALBUM,
    TRACK,
    PLAYLIST,
    RADIO,
    PODCAST,
    AUDIOBOOK
};

// Music item info (unified structure for all music types)
struct MusicItem {
    std::string itemId;
    std::string name;
    std::string sortName;
    std::string uri;           // library://track/123
    std::string imageUrl;
    std::string imageProvider;  // MA provider instance ID for imageproxy
    MediaType mediaType = MediaType::UNKNOWN;

    // Track info
    std::string artistName;
    std::string albumName;
    int trackNumber = 0;
    int discNumber = 0;
    int duration = 0;          // in seconds

    // Album info
    int year = 0;
    std::string version;       // album version (deluxe, remaster, etc.)
    std::string subtype;       // "album", "single", "ep", "compilation", "soundtrack", "live"

    // Artist info
    std::string biography;

    // Playlist info
    int itemCount = 0;
    bool isEditable = false;

    // Favorite status
    bool favorite = false;

    // Provider info
    std::string provider;
};

// Navigation stack entry
struct NavEntry {
    std::string key;
    std::string title;
    MediaType type;
    int selectedItem = 0;
    int scrollOffset = 0;
};

// Hub (for home screen - recently played, recommendations)
struct Hub {
    std::string title;
    std::string type;
    std::vector<MusicItem> items;
};

// Theme options
enum class AppTheme {
    SYSTEM = 0,
    LIGHT = 1,
    DARK = 2
};

// Audio quality setting
enum class AudioQuality {
    LOSSLESS = 0,       // FLAC lossless
    HIGH = 1,           // 320kbps
    NORMAL = 2,         // 192kbps
    LOW = 3             // 96kbps (for slow connections)
};

// Default action when selecting a track in album view
enum class TrackDefaultAction {
    PLAY_NEXT = 0,
    PLAY_NOW_REPLACE = 1,
    ADD_TO_BOTTOM = 2,
    PLAY_NOW_CLEAR = 3,
    ASK_EACH_TIME = 4
};

// Application settings
struct AppSettings {
    // UI Settings
    AppTheme theme = AppTheme::DARK;
    bool debugLogging = true;
    bool showDebugTab = true;

    // Layout Settings
    bool collapseSidebar = false;
    std::string sidebarOrder;

    // Content Display Settings
    bool showPlaylists = true;
    bool hideTitlesInGrid = false;

    // Playback Settings
    bool autoPlayNext = true;
    int seekInterval = 10;  // seconds
    int controlsAutoHideSeconds = 5;
    bool localPlayback = true;  // Play audio locally on Vita via Sendspin
    // Decode Sendspin audio natively (dr_flac + sceAudioOut) instead of routing
    // it through the local HTTP server into mpv. Lighter and lower-latency, and
    // now the default path; mpv remains an automatic fallback for the time being
    // (to be removed once native audio is proven as the sole decoder).
    bool nativeAudio = true;

    // Player Settings
    std::string sendspinPlayerName = "PS Vita";  // Name shown in Music Assistant
    std::string selectedPlayerId;  // Player ID to control (empty = local Vita player)

    // Audio Settings
    AudioQuality audioQuality = AudioQuality::NORMAL;
    bool backgroundMusic = true;

    // Network Settings
    int connectionTimeout = 180;

    // Music Settings
    TrackDefaultAction trackDefaultAction = TrackDefaultAction::ASK_EACH_TIME;

    // Debug settings
    bool enableFileLogging = false;

    // User info
    std::string username;

    // Server settings
    std::string serverUrl;
    bool rememberLogin = true;

    // Saved credentials
    std::string savedAuthToken;
    std::string savedServerUrl;
    std::string savedServerName;

    // Remote access
    std::string remoteId;              // 26-char base32 remote access id
    bool remoteAccessEnabled = false;
    // True when the last successful connection went over remote access;
    // session restore then reconnects via the Remote ID instead of the
    // saved server URL (which always keeps the direct HTTP address).
    bool lastConnectionRemote = false;
};

// Debug logging functions
void initDebugLog();
void closeDebugLog();
void debugLog(const char* format, ...);
void setDebugLogEnabled(bool enabled);

/**
 * Main application class - Music Assistant focused, audio only
 */
class App {
public:
    static App& instance();

    bool init();
    void run();
    void shutdown();

    // Authentication
    bool login(const std::string& username, const std::string& password);
    bool connectToServer(const std::string& url);
    void logout();
    bool isLoggedIn() const { return !m_authToken.empty(); }
    std::string getAuthToken() const { return m_authToken; }

    // Navigation stack
    void pushNavigation(const std::string& key, const std::string& title, MediaType type);
    void popNavigation();
    bool canGoBack() const { return !m_navStack.empty(); }

    // Search
    bool search(const std::string& query);

    // Getters for UI
    const std::vector<MusicItem>& getSearchResults() const { return m_searchResults; }
    const std::vector<Hub>& getHubs() const { return m_hubs; }
    const AppSettings& getSettings() const { return m_settings; }
    AppSettings& getSettings() { return m_settings; }

    // Player ID for remote playback
    void setPlayerId(const std::string& id) { m_playerId = id; }
    const std::string& getPlayerId() const { return m_playerId; }

    // Persistence
    bool saveSettings();
    bool loadSettings();
    bool hasSavedLogin() const { return !m_settings.savedAuthToken.empty(); }
    bool restoreSavedLogin();

    // Error handling
    void setError(const std::string& message);
    std::string getLastError() const { return m_lastError; }

private:
    App() = default;
    ~App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool m_running = false;
    std::string m_lastError;
    std::string m_authToken;
    std::string m_serverUrl;
    std::string m_playerId;

    // Data
    std::vector<MusicItem> m_searchResults;
    std::vector<Hub> m_hubs;
    std::vector<NavEntry> m_navStack;
    std::string m_searchQuery;
    AppSettings m_settings;
};

} // namespace vita_ma
