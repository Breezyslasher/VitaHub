/**
 * Vita Music Assistant - Player Activity
 * Audio playback screen with controls and queue support
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include "app/ma_types.hpp"
#include "app/ma_client.hpp"
#include "app/music_queue.hpp"

namespace vita_ma {

class PlayerActivity : public brls::Activity {
public:
    // Play a single track by key
    PlayerActivity(const std::string& mediaKey);

    // Play a direct stream URL (e.g. radio)

    // Play from queue (album, playlist, etc.)
    // Automatically creates a server-side play queue when online,
    // falls back to client-side queue when offline
    static PlayerActivity* createWithQueue(const std::vector<MusicItem>& tracks, int startIndex = 0);

    // Resume existing queue (return to player without resetting queue)
    static PlayerActivity* createResumeQueue();

    brls::View* createContentView() override;

    void onContentAvailable() override;

    void willDisappear(bool resetState) override;

private:
    void loadMedia();
    void loadFromQueue();           // Load current track from queue
    // Show the album cover: a monogram placeholder first, then the real art when
    // it loads. Empty resolvedUrl leaves the placeholder (no cover available).
    void showAlbumCover(const std::string& resolvedUrl, const std::string& name);
    void updateProgress();
    void togglePlayPause();
    void seek(int seconds);

    // Controls visibility toggle (like Suwayomi reader settings)
    void toggleControls();
    void showControls();
    void hideControls();
    void resetControlsIdleTimer();  // Reset inactivity timer on user input
    bool m_controlsVisible = true;
    int m_controlsIdleSeconds = 0;  // Seconds since last user interaction

    // Queue controls
    void playNext();
    void playPrevious();
    void toggleShuffle();
    void toggleRepeat();
    void updateShuffleIcon();       // Update shuffle button icon based on state
    void updateRepeatIcon();        // Update repeat button icon based on state
    void onTrackEnded(const QueueItem* nextTrack);  // Called when track ends
    void updateQueueDisplay();      // Update UI with queue info

    // Queue list overlay (Direction-A side sheet, ported from Vita_plex).
    // The sheet always renders the SERVER queue of the active player; every
    // mutation is a player_queues/* API call (see fetchQueueSnapshot).
    void showQueueOverlay();
    void hideQueueOverlay();
    void populateQueueList();       // Build queue list with cover art and titles
    void updateNowPlayingBlock();   // Refresh the "Now Playing" header row
    void removeFocusedQueueTrack(); // Remove the track for the focused up-next row
    void removeQueueTrackByIndex(int trackIdx);  // Shared remove (server call + rebuild)
    void moveFocusedQueueTrack(int direction);  // -1 = up, +1 = down (LB/RB)
    void clearUpcoming();           // Clear button: drop everything after current
    void linkFirstRowToClear();     // Route UP off the first row to the Clear button
    void refixQueueUpRoutes(int lo);
    void scrollQueueToChild(int idx);
    void toggleQueueGrab();         // pick up the focused track / drop it
    void setQueueGrab(bool on);     // enter/leave move mode + update the row cue
    void animateGrabLift(bool lifted);
    bool m_queueOverlayVisible = false;
    bool m_queueGrabActive = false;     // a track is "picked up" for Up/Down reorder
    bool m_queuePopulating = false;     // Guard against re-entrant populateQueueList
    // Current index the rows were last built around (Now Playing / Up Next split)
    int m_lastRenderedCurrentIndex = -1;
    // Currently focused up-next row (background tint + remove affordance)
    brls::Box* m_focusedQueueRow = nullptr;
    // 0..1 grab-lift progress (row slides out while held). Mapped to
    // translationX on m_focusedQueueRow each tick.
    brls::Animatable m_grabLift;
    static constexpr float kGrabLiftPx = 14.0f;  // how far the held row slides out
    // When >= 0, the child index populateQueueList / the batched build should
    // land focus on after a rebuild (set by reorder commits)
    int m_queueFocusTargetChild = -1;

    // Server queue snapshot rendered by the sheet (player_queues/get + items).
    // QueueItem.ratingKey carries the MA queue_item_id for API calls; the list
    // is already in play order (the server applies shuffle at enqueue time).
    std::vector<QueueItem> m_queueSnapshot;
    int m_queueSnapshotCurrentIdx = -1;
    void fetchQueueSnapshot(bool showWhenReady);  // refresh snapshot, then populate
    // Coalesce event-driven refetches: bulk edits (Clear = N deletes) fire a
    // QUEUE_UPDATED burst; run one fetch at a time and fold the burst into a
    // single trailing refetch.
    bool m_queueFetchInFlight = false;
    bool m_queueRefetchQueued = false;

    // Windowed queue rendering - only create rows for a window around the current track
    // to avoid creating thousands of views for large queues
    static constexpr int QUEUE_RENDER_LIMIT = 60;  // Max rows to create at once
    static constexpr int QUEUE_EXPAND_CHUNK = 20;   // Rows to add when expanding window
    static constexpr int QUEUE_EXPAND_TRIGGER = 5;   // Expand when focus is within this many rows of edge
    static constexpr int QUEUE_EXPAND_BATCH = 4;     // Rows to create per frame during async expansion
    int m_queueWindowStart = 0;     // First queue display index in the rendered window
    int m_queueWindowEnd = 0;       // One past last queue display index in the window
    int m_queueTotalCount = 0;      // Total queue items
    void expandQueueWindow(int direction);  // +1 = expand down, -1 = expand up
    // Async expansion state - creates rows across frames to avoid freezing
    bool m_expandActive = false;
    int m_expandNext = 0;           // Next queue display index to create
    int m_expandEnd = 0;            // One past last index to create
    void expandQueueBatch();        // Create next batch of expansion rows

    // Batched queue population - creates rows across multiple frames to avoid UI freeze
    static constexpr int QUEUE_BATCH_SIZE = 12;  // Rows to create per frame (keep low for Vita perf)
    int m_queueBatchNext = 0;                    // Next row index to create
    int m_queueBatchTotal = 0;                   // Total rows to create
    bool m_queueBatchActive = false;             // Whether batched creation is in progress
    void populateQueueBatch();                   // Create next batch of rows
    void createQueueRow(int displayIdx, int trackIdx, const QueueItem& track, bool isCurrent);

    // Queue row management - maps row views to their track indices
    // so gesture handlers can look up current position at interaction time
    // instead of relying on stale captured values
    struct QueueRowData {
        int trackIdx;
        std::string title;
        brls::Box* removeBtn = nullptr;  // revealed while the row is focused
    };
    std::unordered_map<brls::View*, QueueRowData> m_queueRowData;

    // Lazy thumbnail loading for queue rows - only loads covers for
    // visible rows instead of all tracks at once
    struct DeferredThumb {
        brls::Image* image;
        std::string thumbPath;      // Thumbnail URL/path (resolved lazily)
        std::string thumbProvider;  // MA provider instance ID for imageproxy
        std::string ratingKey;      // For checking local downloads lazily
        bool loaded;
    };
    std::vector<DeferredThumb> m_deferredThumbs;
    void loadQueueThumbsAroundIndex(int displayIndex);
    static constexpr int QUEUE_THUMB_BUFFER = 6;  // Load this many rows above/below visible

    // Helper to find a row's current display position in the queue list
    int findQueueRowDisplayIndex(brls::View* row);

    // Drag-to-reorder state: hold delay + live row movement
    struct DragState {
        bool active = false;                 // Whether a drag is in progress
        brls::View* draggedRow = nullptr;     // The row being dragged
        int originalDisplayIdx = -1;         // Display index where drag started
        int targetDisplayIdx = -1;           // Current target drop position
        int draggedTrackIdx = -1;            // Queue index of the track being dragged
        std::chrono::steady_clock::time_point holdStart;  // When touch began
        bool holdMet = false;                 // Whether hold threshold was met
        bool justEnded = false;              // Suppress tap/click right after drag ends
        bool scrollPassthrough = false;      // True when forwarding touch as scroll (hold not met)
        float initialScrollY = 0.0f;         // ScrollingFrame offset when touch began
        float dragStartY = 0.0f;             // Finger Y when drag mode activated (for row translation)
        float dragStartScrollY = 0.0f;       // Scroll offset when drag mode activated
        float scrollViewTop = 0.0f;          // Scroll view's absolute Y on screen (computed at drag start)
    };
    DragState m_dragState;
    static constexpr int HOLD_THRESHOLD_MS = 200;  // ms to hold before drag starts
    static constexpr float ROW_HEIGHT_PX = 62.0f;  // Approx row height for swap threshold

    std::string m_mediaKey;
    std::string m_directFilePath;  // For stream URL
    std::string m_streamTitle;     // Title for stream playback
    MediaType m_mediaType = MediaType::UNKNOWN;  // Type of media being played
    bool m_isLocalFile = false;
    bool m_isDirectFile = false;


    bool m_endHandled = false;      // Prevent multiple triggers when playback ends
    bool m_isPlaying = false;
    bool m_isQueueMode = false;    // Playing from queue
    bool m_isResuming = false;     // Resuming existing playback (don't restart track)
    bool m_destroying = false;     // Flag to prevent timer callbacks during destruction
    bool m_loadingMedia = false;   // Flag to prevent rapid re-entry of loadMedia
    double m_pendingSeek = 0.0;    // Pending seek position (set when resuming)
    bool m_updatingSlider = false;  // Guard to prevent slider update from triggering seek
    brls::RepeatingTimer m_updateTimer;

    // Deferred MPV init: URL and title are stored here during onContentAvailable()
    // and loaded in the first updateProgress() call. This prevents GXM context
    // conflicts between MPV's render context creation / decoder threads and
    // NanoVG during the borealis activity show phase.
    std::string m_pendingPlayUrl;
    std::string m_pendingPlayTitle;
    bool m_pendingIsAudio = false;

    // Alive flag for async image loads - prevents use-after-free when activity is destroyed
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);

    void updatePlayPauseLabel();

    BRLS_BIND(brls::Box, playerContainer, "player/container");
    BRLS_BIND(brls::Label, titleLabel, "player/title");
    BRLS_BIND(brls::Label, artistLabel, "player/artist");
    BRLS_BIND(brls::Label, timeElapsedLabel, "player/time_elapsed");
    BRLS_BIND(brls::Label, timeRemainingLabel, "player/time_remaining");
    BRLS_BIND(brls::Label, queueLabel, "player/queue_info");
    BRLS_BIND(brls::Slider, progressSlider, "player/progress");
    BRLS_BIND(brls::Box, controlsBox, "player/controls");
    BRLS_BIND(brls::Box, albumArtContainer, "player/album_art_container");
    BRLS_BIND(brls::Image, albumArt, "player/album_art");
    BRLS_BIND(brls::Box, albumArtPlaceholder, "player/album_art_placeholder");
    BRLS_BIND(brls::Label, albumArtInitial, "player/album_art_initial");
    BRLS_BIND(brls::Box, queueBtn, "player/queue_btn");
    BRLS_BIND(brls::Image, queueIcon, "player/queue_icon");
    BRLS_BIND(brls::Box, queueOverlay, "player/queue_overlay");
    BRLS_BIND(brls::Box, queueScrim, "player/queue_scrim");
    BRLS_BIND(brls::Label, queueOverlayTitle, "player/queue_overlay_title");
    BRLS_BIND(brls::Box, queueList, "player/queue_list");
    BRLS_BIND(brls::ScrollingFrame, queueScroll, "player/queue_scroll");
    BRLS_BIND(brls::Box, queueNowPlaying, "player/queue_now_playing");
    BRLS_BIND(brls::Image, queueNpThumb, "player/queue_np_thumb");
    BRLS_BIND(brls::Label, queueNpTitle, "player/queue_np_title");
    BRLS_BIND(brls::Label, queueNpArtist, "player/queue_np_artist");
    BRLS_BIND(brls::Label, queueNpLabel, "player/queue_np_label");
    BRLS_BIND(brls::Label, queueUpNextLabel, "player/queue_upnext_label");
    BRLS_BIND(brls::Box, queueClearBtn, "player/queue_clear_btn");


    // Player switcher
    BRLS_BIND(brls::Box, playerSwitchBtn, "player/player_switch_btn");
    BRLS_BIND(brls::Image, playerSwitchIcon, "player/player_switch_icon");
    BRLS_BIND(brls::Label, playerNameLabel, "player/player_name_label");
    void showPlayerSwitcher();
    void loadRemotePlayerState();  // Fetch and display the selected remote player's state
    void updatePlayerNameLabel();  // Update the label showing which player is active
    void pollRemotePlayerState();  // Periodic poll of remote player state (called from updateProgress)
    void onRemotePlayerEvent(const Json& data);  // Handle PLAYER_UPDATED events from server
    void onRemoteQueueEvent(const Json& data);   // Handle QUEUE_UPDATED events from server
    std::vector<PlayerInfo> m_availablePlayers;  // Cached player list
    std::string m_ownPlayerId;                   // MA player_id of our own registered Sendspin player

    // Find our own Sendspin-registered player in a players list (matched by the
    // Sendspin client id, then by name). Returns its MA player_id, or "".
    std::string findOwnPlayerId(const std::vector<PlayerInfo>& players) const;

    // Remote player state tracking
    std::string m_remoteCurrentUri;       // URI of currently playing track on remote player
    std::string m_remoteState;            // "playing", "paused", "idle"
    int m_remoteElapsed = 0;              // Elapsed seconds on remote player
    int m_remoteDuration = 0;             // Duration seconds of current track
    int m_remotePollCounter = 0;          // Counter for throttling remote polls

    // Base position (seconds) for native-audio playback: the decoder's played
    // frames count from 0 each stream, so a seek stores its target here and the
    // displayed position is base + NativeAudioPlayer::positionSeconds().
    double m_nativePosBase = 0.0;

    // Returns the player/queue ID for the currently selected player.
    // If a remote player is selected, returns its ID; otherwise returns the local Vita player ID.
    std::string getActivePlayerId() const;
    // Returns true if a remote player is currently selected
    bool isRemotePlayer() const;

    // Music-specific UI elements
    BRLS_BIND(brls::Box, musicInfo, "player/music_info");
    BRLS_BIND(brls::Label, musicTitleLabel, "player/music_title");
    BRLS_BIND(brls::Label, musicArtistLabel, "player/music_artist");
    BRLS_BIND(brls::Box, musicTransport, "player/music_transport");
    BRLS_BIND(brls::Box, musicPlayBtn, "player/music_play_btn");
    BRLS_BIND(brls::Image, musicPlayIcon, "player/music_play_icon");
    BRLS_BIND(brls::Box, musicPrevBtn, "player/music_prev_btn");
    BRLS_BIND(brls::Box, musicNextBtn, "player/music_next_btn");
    BRLS_BIND(brls::Box, shuffleBtn, "player/shuffle_btn");
    BRLS_BIND(brls::Image, shuffleIcon, "player/shuffle_icon");
    BRLS_BIND(brls::Box, repeatBtn, "player/repeat_btn");
    BRLS_BIND(brls::Image, repeatIcon, "player/repeat_icon");
};

} // namespace vita_ma
