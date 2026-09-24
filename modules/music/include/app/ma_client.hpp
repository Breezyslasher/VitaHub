#pragma once

#include "app.h"
#include "app/websocket_client.hpp"
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

namespace vita_ma {

// JSON helper - minimal JSON builder/parser for Vita (no external deps)
class Json {
public:
    enum Type { OBJECT, ARRAY, STRING, NUMBER, BOOL, NULL_TYPE };

    Json() : m_type(OBJECT) {}
    Json(Type t) : m_type(t) {}
    Json(const std::string& s) : m_type(STRING), m_strVal(s) {}
    Json(const char* s) : m_type(STRING), m_strVal(s ? s : "") {}
    Json(int n) : m_type(NUMBER), m_numVal(n) {}
    Json(double n) : m_type(NUMBER), m_numVal(n) {}
    Json(bool b) : m_type(BOOL), m_boolVal(b) {}

    // Explicitly keep copies AND (noexcept) moves so building the parse tree
    // moves subtrees instead of deep-copying them - and so std::vector<Json>
    // reallocation uses the move ctor rather than the copy ctor.
    Json(const Json&) = default;
    Json& operator=(const Json&) = default;
    Json(Json&&) noexcept = default;
    Json& operator=(Json&&) noexcept = default;

    static Json parse(const std::string& str);
    std::string dump() const;

    // Object access
    Json& operator[](const std::string& key);
    const Json& operator[](const std::string& key) const;
    bool has(const std::string& key) const;

    // Array access
    Json& operator[](size_t index);
    const Json& operator[](size_t index) const;
    void push_back(const Json& val);
    size_t size() const;

    // Value access
    std::string str() const { return m_strVal; }
    int intVal() const { return static_cast<int>(m_numVal); }
    double numVal() const { return m_numVal; }
    bool boolVal() const { return m_boolVal; }
    Type type() const { return m_type; }
    bool isNull() const { return m_type == NULL_TYPE; }

    // Setters
    void setStr(const std::string& s) { m_type = STRING; m_strVal = s; }
    void setNum(double n) { m_type = NUMBER; m_numVal = n; }
    void setBool(bool b) { m_type = BOOL; m_boolVal = b; }

private:
    Type m_type = NULL_TYPE;
    std::string m_strVal;
    double m_numVal = 0;
    bool m_boolVal = false;
    // Object members kept in insertion order as a flat vector rather than a
    // std::map: parsing a large response allocated a red-black-tree node and
    // copied a key string for every one of ~40k object fields, which dominated
    // the ~9s parse of the 2.6 MB library response on Vita. Appending to a
    // vector avoids the per-field tree allocation/rebalancing; lookups are
    // linear but objects have only a handful of keys.
    std::vector<std::pair<std::string, Json>> m_objMap;
    std::vector<Json> m_arrVec;

    static Json parseValue(const std::string& str, size_t& pos);
    static Json parseObject(const std::string& str, size_t& pos);
    static Json parseArray(const std::string& str, size_t& pos);
    static std::string parseString(const std::string& str, size_t& pos);
    static Json parseNumber(const std::string& str, size_t& pos);
    static void skipWhitespace(const std::string& str, size_t& pos);
};

// Event types from Music Assistant
enum class MAEvent {
    CONNECTED,
    DISCONNECTED,
    AUTH_FAILED,        // token rejected by the server (expired/invalid)
    QUEUE_UPDATED,
    QUEUE_ITEMS_UPDATED,
    QUEUE_TIME_UPDATED,
    PLAYER_UPDATED,
    MEDIA_ITEM_UPDATED,
    PROVIDERS_UPDATED,
    UNKNOWN
};

// Callback for command responses
using MAResponseCallback = std::function<void(bool success, const Json& result)>;
// Callback for events
using MAEventCallback = std::function<void(MAEvent event, const Json& data)>;

class MAClient {
public:
    static MAClient& instance();

    // Connection. serverUrl may be an http(s)/ws(s) URL for a direct
    // connection, or a Remote ID (26-char base32) for WebRTC remote access -
    // connect() dispatches automatically.
    bool connect(const std::string& serverUrl, const std::string& authToken = "");
    // Connect through MA remote access (WebRTC data channel via the signaling
    // server). Requires an auth token obtained from a previous direct login.
    bool connectRemote(const std::string& remoteId, const std::string& authToken = "");
    void disconnect();
    bool isConnected() const;

    // Choose where the direct WebSocket delivers text messages: the main thread
    // (default, via brls::sync) or the receive thread. The cold-start restore
    // connect blocks the main thread before the borealis main loop runs, so its
    // auth handshake must be delivered on the receive thread or it deadlocks
    // until the timeout. Callers flip this off around that blocking connect.
    void setDispatchOnMainThread(bool enable) { m_ws.setDispatchOnMainThread(enable); }

    // True when connected (or connecting) via WebRTC remote access
    bool isRemoteMode() const { return m_remoteMode.load(); }
    // True if the string looks like a Remote ID rather than a URL
    static bool isRemoteId(const std::string& s);

    // Set event callback
    void setEventCallback(MAEventCallback cb) { m_eventCallback = std::move(cb); }

    // After the next successful *direct* authentication, mint a long-lived
    // token (auth/token/create) so future connections - including remote
    // WebRTC ones, which can't reach /auth/login - never expire. Set this
    // before a fresh username/password login.
    void setUpgradeToLongLived(bool v) { m_upgradeToLongLived.store(v); }

    // Credential login over the socket itself (works over WebRTC remote
    // connections too): when set and no token is given, the client sends the
    // 'auth/login' command after server_info, then authenticates with the
    // returned token. providerId "builtin" or "homeassistant". Cleared after
    // one use.
    void setPendingCredentials(const std::string& username, const std::string& password,
                               const std::string& providerId = "builtin");
    // Invoked (on the WS thread) with the freshly minted long-lived token so
    // the app can persist it. m_authToken is already updated when this fires.
    void setTokenUpgradedCallback(std::function<void(const std::string&)> cb) {
        m_onTokenUpgraded = std::move(cb);
    }
    // Invoked (on the WS thread) when the server rejects our token. The
    // reconnect loop is stopped before this fires - the app should surface a
    // re-login prompt. Registered once at startup, never cleared.
    void setAuthFailedCallback(std::function<void()> cb) {
        m_onAuthFailed = std::move(cb);
    }

    // === Music Library Commands ===

    // Artists
    void getLibraryArtists(MAResponseCallback cb, const std::string& search = "",
                           int limit = 500, int offset = 0);
    void getArtist(const std::string& itemId, MAResponseCallback cb,
                   const std::string& provider = "library");
    void getArtistAlbums(const std::string& itemId, MAResponseCallback cb,
                         const std::string& provider = "library");
    void getArtistTracks(const std::string& itemId, MAResponseCallback cb,
                         const std::string& provider = "library");

    // Albums
    void getLibraryAlbums(MAResponseCallback cb, const std::string& search = "",
                          int limit = 500, int offset = 0);
    void getAlbum(const std::string& itemId, MAResponseCallback cb,
                  const std::string& provider = "library");
    void getAlbumTracks(const std::string& itemId, MAResponseCallback cb,
                        const std::string& provider = "library");

    // Tracks
    void getLibraryTracks(MAResponseCallback cb, const std::string& search = "",
                          int limit = 500, int offset = 0);
    void getTrack(const std::string& itemId, MAResponseCallback cb,
                  const std::string& provider = "library");

    // Playlists
    void getLibraryPlaylists(MAResponseCallback cb, const std::string& search = "",
                             int limit = 500, int offset = 0);
    void getPlaylist(const std::string& itemId, MAResponseCallback cb,
                     const std::string& provider = "library");
    void getPlaylistTracks(const std::string& itemId, MAResponseCallback cb,
                           int page = 0, const std::string& provider = "library");
    void createPlaylist(const std::string& name, MAResponseCallback cb);

    // Radio
    void getLibraryRadios(MAResponseCallback cb, const std::string& search = "",
                          int limit = 500, int offset = 0);

    // Search. mediaTypes filters which media types the server returns (MA enum
    // strings: track/album/artist/playlist/radio/audiobook/podcast); empty =
    // all supported types. libraryOnly restricts results to the local library
    // (providers=["library"]); false searches the library and all providers.
    void search(const std::string& query, const std::vector<std::string>& mediaTypes,
                bool libraryOnly, MAResponseCallback cb, int limit = 25);

    // Browse
    void browse(const std::string& path, MAResponseCallback cb);

    // Favorites
    void addToFavorites(const std::string& mediaType, const std::string& itemId,
                        MAResponseCallback cb);
    void removeFromFavorites(const std::string& mediaType, const std::string& itemId,
                             MAResponseCallback cb);

    // Recently played
    void getRecentlyPlayed(MAResponseCallback cb, int limit = 20);
    void getRecommendations(MAResponseCallback cb);

    // === Queue Commands ===

    void playMedia(const std::string& queueId, const std::string& uri,
                   const std::string& option = "play", MAResponseCallback cb = nullptr);
    void queuePlay(const std::string& queueId, MAResponseCallback cb = nullptr);
    void queuePause(const std::string& queueId, MAResponseCallback cb = nullptr);
    void queueStop(const std::string& queueId, MAResponseCallback cb = nullptr);
    void queueNext(const std::string& queueId, MAResponseCallback cb = nullptr);
    void queuePrevious(const std::string& queueId, MAResponseCallback cb = nullptr);
    void queueSeek(const std::string& queueId, int position, MAResponseCallback cb = nullptr);
    void queueShuffle(const std::string& queueId, bool enabled, MAResponseCallback cb = nullptr);
    void queueRepeat(const std::string& queueId, const std::string& mode,
                     MAResponseCallback cb = nullptr);
    void queueClear(const std::string& queueId, MAResponseCallback cb = nullptr);
    void getQueueItems(const std::string& queueId, MAResponseCallback cb,
                       int limit = 500, int offset = 0);
    void getQueue(const std::string& queueId, MAResponseCallback cb);
    void queueMoveItem(const std::string& queueId, const std::string& itemId,
                       int posShift, MAResponseCallback cb = nullptr);
    void queueDeleteItem(const std::string& queueId, const std::string& itemId,
                         MAResponseCallback cb = nullptr);
    void playQueueIndex(const std::string& queueId, int index,
                        MAResponseCallback cb = nullptr);

    // === Player Commands ===

    void playerVolumeSet(const std::string& playerId, int level,
                         MAResponseCallback cb = nullptr);
    void playerVolumeUp(const std::string& playerId, MAResponseCallback cb = nullptr);
    void playerVolumeDown(const std::string& playerId, MAResponseCallback cb = nullptr);
    void playerVolumeMute(const std::string& playerId, bool muted,
                          MAResponseCallback cb = nullptr);
    void playerPower(const std::string& playerId, bool powered,
                     MAResponseCallback cb = nullptr);

    // Get stream URL for local playback
    // getStreamUrl removed - use player_queues/play_media + Sendspin instead

    // Get all players
    void getPlayers(MAResponseCallback cb);

    // Delete a playlist
    void deletePlaylist(const std::string& itemId, MAResponseCallback cb = nullptr);

    // Get thumbnail URL for an image path
    // Returns a full URL suitable for image loading
    std::string getThumbnailUrl(const std::string& imageUrl, int width = 0, int height = 0,
                               const std::string& provider = "");

    // Extract an image reference from a serialized MediaItemImage object.
    // Prefers the opaque server-side proxy_id (returned as "proxyid:<id>",
    // consumed by getThumbnailUrl to build the canonical /imageproxy/<id>
    // endpoint); falls back to the raw path for older servers.
    static std::string imageRefFromJson(const Json& imageObj);

    // Server info
    ServerInfo getServerInfo() const { return m_serverInfo; }

    // Send a command to the server (public for WebRTC relay use)
    void sendCommand(const std::string& command, const Json& kwargs,
                     MAResponseCallback cb = nullptr);

    // Raw response variant: the callback receives the response's "result" value
    // as a raw JSON substring instead of a parsed Json DOM. Used for very large
    // list responses (the library) where building a full DOM of ~40k nodes
    // costs ~8s on Vita; the caller extracts only the fields it needs directly
    // from the string (the Vita_Suwayomi approach).
    using MARawResponseCallback = std::function<void(bool success, const std::string& rawResult)>;
    void sendCommandRaw(const std::string& command, const Json& kwargs,
                        MARawResponseCallback cb);
    // Fetch library items ("albums"/"artists"/"tracks"/"playlists") with the
    // raw (DOM-free) response path.
    void getLibraryItemsRaw(const std::string& mediaType, MARawResponseCallback cb,
                            const std::string& search, int limit, int offset);

    // DOM-free JSON helpers (shared by the raw response path). rawExtractField
    // returns a key's value as a raw substring (string contents unquoted;
    // objects/arrays returned whole, brace/bracket-aware). rawSplitArrayObjects
    // splits a top-level JSON array of objects into per-object substrings.
    static std::string rawExtractField(const std::string& json, const std::string& key);
    // Bounded variant: search only within json[begin, end). Lets a caller walk a
    // big array once, note each object's byte range, and extract fields straight
    // from the original string with no per-object substring copies.
    static std::string rawExtractFieldIn(const std::string& json, const std::string& key,
                                         size_t begin, size_t end);
    static std::vector<std::string> rawSplitArrayObjects(const std::string& arrayJson);

private:
    MAClient() = default;
    ~MAClient() = default;
    std::string generateMessageId();

    // Handle incoming messages
    void onMessage(const std::string& message);
    void onError(const std::string& error);
    void onClose(int code, const std::string& reason);

    // Parse events
    MAEvent parseEventType(const std::string& eventStr);

    // Transport: local WebSocket, or the WebRTC remote-access data channel
    WebSocketClient m_ws;
    std::atomic<bool> m_remoteMode{false};
    bool sendRaw(const std::string& json);
    std::string m_serverUrl;
    std::string m_authToken;
    ServerInfo m_serverInfo;
    std::atomic<bool> m_authenticated{false};
    bool m_authRequired = false;

    // Pending commands (awaiting responses)
    std::map<std::string, MAResponseCallback> m_pendingCallbacks;
    // Callbacks awaiting a raw (un-parsed) response, keyed by message_id.
    std::map<std::string, MARawResponseCallback> m_pendingRawCallbacks;
    std::mutex m_callbackMutex;

    // Commands queued before auth completes
    struct QueuedCommand {
        std::string command;
        Json kwargs;
        MAResponseCallback cb;
    };
    std::vector<QueuedCommand> m_preAuthQueue;
    std::mutex m_preAuthMutex;
    void flushPreAuthQueue();

    // Event callback
    MAEventCallback m_eventCallback;

    // Message ID counter
    std::atomic<int> m_msgCounter{0};

    // Reconnect
    std::atomic<bool> m_shouldReconnect{true};

    // Long-lived token upgrade / auth-failure handling
    std::atomic<bool> m_upgradeToLongLived{false};
    std::function<void(const std::string&)> m_onTokenUpgraded;
    std::function<void()> m_onAuthFailed;
    void attemptReconnect();
    void upgradeToLongLivedToken();

    // One-shot credentials for socket-level auth/login (see setPendingCredentials)
    std::string m_pendingLoginUser;
    std::string m_pendingLoginPass;
    std::string m_pendingLoginProvider;
    void sendAuthCommand();   // authenticate with m_authToken
    void sendLoginCommand();  // auth/login with pending credentials, then auth

    // connect() blocks until the async auth exchange resolves (success,
    // rejection, or transport loss) so callers get a trustworthy result.
    std::mutex m_authWaitMutex;
    std::condition_variable m_authWaitCv;
    bool m_authResolved = false;
    void resolveAuthWait();
    bool waitForAuthOutcome(int timeoutMs);
};

} // namespace vita_ma
