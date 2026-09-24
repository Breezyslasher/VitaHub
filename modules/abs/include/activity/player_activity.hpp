/**
 * VitaABS - Player Activity
 * Audio playback screen with controls
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include <memory>
#include <atomic>
#include <string>
#include <vector>

#include "app/audiobookshelf_client.hpp"   // MediaItem, Chapter

namespace vitaabs {

class PlayerActivity : public brls::Activity {
public:
    // Play audiobook/podcast item (single file or book)
    PlayerActivity(const std::string& itemId);

    // Play podcast episode or audiobook with specific episode
    PlayerActivity(const std::string& itemId, const std::string& episodeId,
                   float startTime = -1.0f);

    // Play local downloaded file
    PlayerActivity(const std::string& itemId, bool isLocalFile);

    // Play with pre-downloaded temp file (downloaded before player push)
    PlayerActivity(const std::string& itemId, const std::string& episodeId,
                   const std::string& preDownloadedPath, float startTime = -1.0f);

    // Play direct file path (for debug/testing)
    static PlayerActivity* createForDirectFile(const std::string& filePath);

    brls::View* createContentView() override;

    void onContentAvailable() override;

    void willDisappear(bool resetState) override;

private:
    void loadMedia();
    void loadCoverArt(const std::string& coverUrl);
    // Fill the eyebrow / subline / context slot / right stat tile from a
    // fetched item. Books get the chapter line, podcasts get the episode
    // eyebrow and description — same layout either way, different slots.
    void applyItemInfo(const MediaItem& item);
    // Lighter variant for the offline paths, which have loose strings from
    // DownloadItem rather than a full MediaItem.
    void applyLocalInfo(const std::string& subline, bool isPodcast);
    // Keeps the right tile's "CHAPTER n / m" current as playback moves.
    void updateChapterTile(double position);
    void updateProgress();
    void syncProgressToServer();  // Periodic sync to server during playback
    void updatePlayPauseButton();
    // Pick the rewind/fast-forward glyphs matching the configured seek
    // interval (the icon set ships 5/10/15/30/45/60 second variants).
    void applySeekIcons(int seconds);
    // The screen paints with borealis' active theme so it matches the rest of
    // the app in Light and Dark; only the bronze accent is fixed. Applied once
    // in onContentAvailable — the theme can only be changed from Settings, and
    // the player is built fresh on every push, so there is nothing to re-tint.
    void applyThemeColors();
    void updateSpeedLabel();
    void cyclePlaybackSpeed();
    void togglePlayPause();
    void seek(int seconds);
    std::string formatTime(double seconds);
    std::string formatTimeRemaining(double remaining);
    float getSpeedValue(int index);

    std::string m_itemId;
    std::string m_episodeId;       // For podcast episodes
    std::string m_directFilePath;  // For direct file playback (debug)
    std::string m_tempFilePath;    // Temp file for streaming (downloaded before playback)
    std::string m_coverUrl;        // URL for cover art
    bool m_isPlaying = false;
    bool m_isPhoto = false;
    bool m_isLocalFile = false;   // Playing from local download
    bool m_isDirectFile = false;  // Playing direct file path (debug)
    bool m_isPreDownloaded = false; // File was pre-downloaded before player push
    bool m_destroying = false;    // Flag to prevent timer callbacks during destruction
    bool m_loadingMedia = false;  // Flag to prevent rapid re-entry of loadMedia
    // Last state pushed to the play/pause image. setImageFromRes decodes and
    // uploads a texture, so the once-a-second tick must not call it blindly.
    int m_playIconState = -1;     // -1 unset, 0 = showing play, 1 = showing pause
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    double m_pendingSeek = 0.0;   // Pending seek position (set when resuming)
    double m_totalDuration = 0.0; // Total duration for display
    brls::RepeatingTimer m_updateTimer;
    int m_syncCounter = 0;        // Counter for periodic server sync (every 30 updates = 30 seconds)
    float m_lastSyncedTime = 0.0f; // Last position synced to server
    std::string m_sessionId;      // Active playback session ID (for server sync)
    // Chapter list of the item being played, kept so the right stat tile can
    // report "CHAPTER n / m" without refetching. Empty for podcasts and for
    // books the server reports no chapters for.
    std::vector<Chapter> m_chapters;
    int m_currentChapter = -1;    // Index into m_chapters; -1 = unknown
    bool m_isPodcastItem = false; // Drives which context slot is visible

    // Main UI bindings
    BRLS_BIND(brls::Box, playerContainer, "player/container");
    BRLS_BIND(brls::Image, coverImage, "player/cover");
    BRLS_BIND(brls::Label, titleLabel, "player/title");
    BRLS_BIND(brls::Label, authorLabel, "player/author");
    BRLS_BIND(brls::Slider, progressSlider, "player/progress");
    BRLS_BIND(brls::Label, timeElapsedLabel, "player/timeElapsed");
    BRLS_BIND(brls::Label, timeRemainingLabel, "player/timeRemaining");
    BRLS_BIND(brls::Box, btnRewind, "player/btnRewind");
    BRLS_BIND(brls::Box, btnPlayPause, "player/btnPlayPause");
    BRLS_BIND(brls::Box, btnForward, "player/btnForward");
    BRLS_BIND(brls::Image, rewindIcon, "player/rewindIcon");
    BRLS_BIND(brls::Image, forwardIcon, "player/forwardIcon");
    BRLS_BIND(brls::Image, playPauseIcon, "player/playPauseIcon");
    BRLS_BIND(brls::Label, speedLabel, "player/speedLabel");
    BRLS_BIND(brls::Label, chapterInfoLabel, "player/chapterInfo");

    // Split-shelf layout additions
    BRLS_BIND(brls::Label, eyebrowLabel, "player/eyebrow");
    BRLS_BIND(brls::Label, subtitleLabel, "player/subtitle");
    BRLS_BIND(brls::Label, tileRightCaption, "player/tileRightCaption");
    BRLS_BIND(brls::Label, tileRightValue, "player/tileRightValue");
    BRLS_BIND(brls::Label, descriptionLabel, "player/description");
    BRLS_BIND(brls::Box, tileLeft, "player/tileLeft");
    BRLS_BIND(brls::Box, tileRight, "player/tileRight");
    BRLS_BIND(brls::Label, tileLeftCaption, "player/tileLeftCaption");
    BRLS_BIND(brls::Rectangle, headerRule, "player/rule");

};

} // namespace vitaabs
