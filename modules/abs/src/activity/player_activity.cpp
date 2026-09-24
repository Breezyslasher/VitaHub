/**
 * VitaABS - Player Activity implementation
 */

#include "activity/player_activity.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "player/mpv_player.hpp"
#include "utils/image_loader.hpp"
#include "view/progress_dialog.hpp"
#include "utils/ui_theme.hpp"

#include <cstdio>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace vitaabs {

// ─── design tokens ────────────────────────────────────────────────────
// Everything except the brand accent resolves from borealis' active theme
// so the player matches the rest of the app in Light and Dark. The root
// box paints no background of its own — the frame's "brls/background"
// shows through, exactly like every other screen.
// See utils/ui_theme.hpp — panel/hairline are derived contrast steps so the
// stat tiles and transport pills read as raised surfaces, not as the page.
namespace ptok = uitok;

// Helper function to check if content should be marked as finished based on settings
static bool shouldMarkAsFinished(float currentTime, float totalDuration, bool isPodcast) {
    if (totalDuration <= 0) return false;

    // For podcasts, use the podcast auto-complete setting
    if (isPodcast) {
        AppSettings& settings = Application::getInstance().getSettings();
        AutoCompleteThreshold threshold = settings.podcastAutoComplete;

        switch (threshold) {
            case AutoCompleteThreshold::DISABLED:
                return false;
            case AutoCompleteThreshold::LAST_10_SEC:
                return (totalDuration - currentTime) <= 10.0f;
            case AutoCompleteThreshold::LAST_30_SEC:
                return (totalDuration - currentTime) <= 30.0f;
            case AutoCompleteThreshold::LAST_60_SEC:
                return (totalDuration - currentTime) <= 60.0f;
            case AutoCompleteThreshold::PERCENT_90:
                return currentTime >= totalDuration * 0.90f;
            case AutoCompleteThreshold::PERCENT_95:
                return currentTime >= totalDuration * 0.95f;
            case AutoCompleteThreshold::PERCENT_99:
                return currentTime >= totalDuration * 0.99f;
            default:
                return currentTime >= totalDuration * 0.95f;
        }
    }

    // For audiobooks, use default 95% threshold
    return currentTime >= totalDuration * 0.95f;
}

PlayerActivity::PlayerActivity(const std::string& itemId)
    : m_itemId(itemId), m_isLocalFile(false) {
    brls::Logger::debug("PlayerActivity created for item: {}", itemId);
}

PlayerActivity::PlayerActivity(const std::string& itemId, const std::string& episodeId,
                               float startTime)
    : m_itemId(itemId), m_episodeId(episodeId), m_isLocalFile(false) {
    brls::Logger::debug("PlayerActivity created for item: {}, episode: {}", itemId, episodeId);
    // If startTime is specified (>= 0), use it as pending seek position
    if (startTime >= 0) {
        m_pendingSeek = static_cast<double>(startTime);
        brls::Logger::debug("Starting at position: {}s", startTime);
    }
}

PlayerActivity::PlayerActivity(const std::string& itemId, bool isLocalFile)
    : m_itemId(itemId), m_isLocalFile(isLocalFile) {
    brls::Logger::debug("PlayerActivity created for {} item: {}",
                       isLocalFile ? "local" : "remote", itemId);
}

PlayerActivity::PlayerActivity(const std::string& itemId, const std::string& episodeId,
                               const std::string& preDownloadedPath, float startTime)
    : m_itemId(itemId), m_episodeId(episodeId), m_isLocalFile(false), m_isPreDownloaded(true) {
    m_tempFilePath = preDownloadedPath;
    if (startTime >= 0) {
        m_pendingSeek = static_cast<double>(startTime);
        brls::Logger::info("PlayerActivity: Will resume from {}s", startTime);
    }
    brls::Logger::debug("PlayerActivity created with pre-downloaded file: {}", preDownloadedPath);
}

PlayerActivity* PlayerActivity::createForDirectFile(const std::string& filePath) {
    PlayerActivity* activity = new PlayerActivity("", false);
    activity->m_isDirectFile = true;
    activity->m_directFilePath = filePath;
    brls::Logger::debug("PlayerActivity created for direct file: {}", filePath);
    return activity;
}

brls::View* PlayerActivity::createContentView() {
    return brls::View::createFromXMLResource("abs/activity/player.xml");
}

void PlayerActivity::onContentAvailable() {
    brls::Logger::debug("PlayerActivity content available");

    // Cancel pending background thumbnail loads to free network bandwidth for streaming
    ImageLoader::cancelAll();

    // Load media details
    loadMedia();

    // Set up progress slider
    if (progressSlider) {
        progressSlider->setProgress(0.0f);
        progressSlider->getProgressEvent()->subscribe([this](float progress) {
            // Seek to position
            MpvPlayer& player = MpvPlayer::getInstance();
            double duration = player.getDuration();
            player.seekTo(duration * progress);
        });
    }

    // Set up button click handlers
    // Focus paints a ring, not a fill. borealis draws a highlight BACKGROUND
    // behind a focused view, which turned the accent play button into a flat
    // dark square the moment it took focus — the same wash the in-app
    // updater's makeButton disables.
    if (btnRewind)   { btnRewind->setHideHighlightBackground(true);   btnRewind->setHighlightCornerRadius(8.0f); }
    if (btnForward)  { btnForward->setHideHighlightBackground(true);  btnForward->setHighlightCornerRadius(8.0f); }
    if (btnPlayPause){ btnPlayPause->setHideHighlightBackground(true); btnPlayPause->setHighlightCornerRadius(44.0f); }

    if (btnPlayPause) {
        btnPlayPause->registerClickAction([this](brls::View* view) {
            togglePlayPause();
            return true;
        });
        btnPlayPause->addGestureRecognizer(new brls::TapGestureRecognizer(btnPlayPause));
    }

    // Get seek interval from settings
    AppSettings& settings = Application::getInstance().getSettings();
    int seekInterval = settings.seekInterval;

    applySeekIcons(seekInterval);

    if (btnRewind) {
        btnRewind->registerClickAction([this, seekInterval](brls::View* view) {
            seek(-seekInterval);
            return true;
        });
        btnRewind->addGestureRecognizer(new brls::TapGestureRecognizer(btnRewind));
    }

    if (btnForward) {
        btnForward->registerClickAction([this, seekInterval](brls::View* view) {
            seek(seekInterval);
            return true;
        });
        btnForward->addGestureRecognizer(new brls::TapGestureRecognizer(btnForward));
    }

    // Register controller actions
    this->registerAction("Play/Pause", brls::ControllerButton::BUTTON_A, [this](brls::View* view) {
        togglePlayPause();
        return true;
    });

    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    std::string rewindAction = "Rewind " + std::to_string(seekInterval) + "s";
    this->registerAction(rewindAction, brls::ControllerButton::BUTTON_LB, [this, seekInterval](brls::View* view) {
        seek(-seekInterval);
        return true;
    });

    std::string forwardAction = "Forward " + std::to_string(seekInterval) + "s";
    this->registerAction(forwardAction, brls::ControllerButton::BUTTON_RB, [this, seekInterval](brls::View* view) {
        seek(seekInterval);
        return true;
    });

    // Speed no longer has its own on-screen button — it reads out in the
    // left stat tile and cycles from Triangle. On Vita that is BUTTON_Y
    // (psv_input.cpp maps SCE_CTRL_TRIANGLE -> BUTTON_Y); the same slot is
    // the "top" face button on every other pad borealis supports.
    this->registerAction("Speed", brls::ControllerButton::BUTTON_Y, [this](brls::View* view) {
        cyclePlaybackSpeed();
        return true;
    });

    // The SPEED tile is the control, not just a readout: Triangle alone was
    // not discoverable. Focusing it tints the border like the login field
    // rows, and Cross cycles through the same 0.5x-2.0x list.
    if (tileLeft) {
        tileLeft->setHideHighlightBackground(true);
        tileLeft->setHighlightCornerRadius(6.0f);
        tileLeft->registerClickAction([this](brls::View*) {
            cyclePlaybackSpeed();
            return true;
        });
        tileLeft->addGestureRecognizer(new brls::TapGestureRecognizer(tileLeft));

        brls::Box* tile = tileLeft;
        tile->getFocusEvent()->subscribe([tile](brls::View*) {
            tile->setBorderColor(ptok::accent());
        });
        tile->getFocusLostEvent()->subscribe([tile](brls::View*) {
            tile->setBorderColor(ptok::hairline());
        });
    }

    applyThemeColors();

    // Initialize speed label from settings
    updateSpeedLabel();

    // Focus starts on play/pause.
    if (btnPlayPause) {
        brls::Application::giveFocus(btnPlayPause);
    }

    // Start update timer
    m_updateTimer.setCallback([this]() {
        updateProgress();
    });
    m_updateTimer.start(1000); // Update every second
}

void PlayerActivity::willDisappear(bool resetState) {
    brls::Activity::willDisappear(resetState);

    m_destroying = true;
    *m_alive = false;

    // Stop update timer first
    m_updateTimer.stop();

    // For photos, nothing to stop
    if (m_isPhoto) {
        return;
    }

    // Stop playback and save progress
    MpvPlayer& player = MpvPlayer::getInstance();

    // Only try to save progress if player is in a valid state
    if (player.isInitialized() && (player.isPlaying() || player.isPaused())) {
        double position = player.getPosition();
        if (position > 0) {
            float currentTime = (float)position;
            float totalDuration = (float)player.getDuration();

            if (m_isLocalFile) {
                // Save progress for downloaded media (in seconds)
                DownloadsManager::getInstance().updateProgress(m_itemId, currentTime, m_episodeId);
                DownloadsManager::getInstance().saveState();
                brls::Logger::info("PlayerActivity: Saved local progress {}s for {} (episode: {})",
                                  currentTime, m_itemId, m_episodeId.empty() ? "none" : m_episodeId);

                // Also sync to server if online
                AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
                if (client.isAuthenticated()) {
                    bool isPodcast = !m_episodeId.empty();
                    bool isFinished = shouldMarkAsFinished(currentTime, totalDuration, isPodcast);
                    client.updateProgress(m_itemId, currentTime, totalDuration, isFinished, m_episodeId);
                    brls::Logger::info("PlayerActivity: Synced local progress to server");
                }
            } else {
                // Close playback session with final position
                if (!m_sessionId.empty()) {
                    float timeListened = currentTime - m_lastSyncedTime;
                    if (timeListened < 0) timeListened = 0;
                    AudiobookshelfClient::getInstance().closePlaybackSession(
                        m_sessionId, currentTime, totalDuration, timeListened);
                    brls::Logger::info("PlayerActivity: Closed session {} at {}s", m_sessionId, currentTime);
                } else {
                    // Fallback to progress update if no session
                    AudiobookshelfClient::getInstance().updateProgress(m_itemId, currentTime, totalDuration, false, m_episodeId);
                }
            }
        }
    }

    // Stop playback (safe to call even if not playing)
    if (player.isInitialized()) {
        player.stop();
    }

    m_isPlaying = false;

    // Re-enable background thumbnail loading
    ImageLoader::setPaused(false);

    m_tempFilePath.clear();
}

// ── Split-shelf info slots ─────────────────────────────────────────────
// The layout is identical for both media types; only the slot contents
// differ. Exactly one of the two context labels (chapter line / episode
// description) is visible at a time.

void PlayerActivity::applyThemeColors() {
    // BRLS_BIND yields a BoundView whose operator T*() is non-const, so the
    // bound views cannot go straight into a braced-init list — pull them out
    // into plain pointers first.
    brls::Box*   tiles[]  = { tileLeft, tileRight };
    brls::Box*   pills[]  = { btnRewind, btnForward };
    brls::Label* dim[]    = { eyebrowLabel, tileLeftCaption, tileRightCaption,
                              authorLabel, descriptionLabel, subtitleLabel,
                              timeElapsedLabel, timeRemainingLabel };
    brls::Label* bright[] = { titleLabel, chapterInfoLabel, tileRightValue };

    for (brls::Box* tile : tiles) {
        if (!tile) continue;
        tile->setBackgroundColor(ptok::panel());
        tile->setBorderColor(ptok::hairline());
    }
    for (brls::Box* b : pills) {
        if (b) b->setBackgroundColor(ptok::panel());
    }
    if (btnPlayPause) btnPlayPause->setBackgroundColor(ptok::accent());
    if (headerRule)   headerRule->setColor(ptok::hairline());

    for (brls::Label* l : dim)    { if (l) l->setTextColor(ptok::secondary()); }
    for (brls::Label* l : bright) { if (l) l->setTextColor(ptok::text()); }
    if (speedLabel) speedLabel->setTextColor(ptok::accent());
}

// The icon set ships 5/10/15/30/45/60-second variants; ABS offers
// 5/10/15/30/60. Fall back to the plain glyph if a value ever drifts off
// that list so the button is never blank.
void PlayerActivity::applySeekIcons(int seconds) {
    static const int kHave[] = { 5, 10, 15, 30, 45, 60 };
    bool exact = false;
    for (int v : kHave) if (v == seconds) { exact = true; break; }

    const std::string suffix = exact ? ("-" + std::to_string(seconds)) : std::string();
    if (rewindIcon)
        rewindIcon->setImageFromRes("icons/rewind" + (exact ? suffix : std::string("-30")) + ".png");
    if (forwardIcon)
        forwardIcon->setImageFromRes("icons/fast-forward" + (exact ? suffix : std::string("-30")) + ".png");
}

void PlayerActivity::applyItemInfo(const MediaItem& item) {
    m_isPodcastItem = (item.mediaType == MediaType::PODCAST_EPISODE) ||
                      (item.type == "podcast") || !m_episodeId.empty();

    m_chapters = item.chapters;
    m_currentChapter = -1;

    if (titleLabel) {
        // For a podcast the episode title is the headline, not the show.
        const std::string& headline =
            (m_isPodcastItem && !item.subtitle.empty()) ? item.subtitle : item.title;
        titleLabel->setText(headline);
    }

    if (authorLabel) {
        authorLabel->setText(m_isPodcastItem ? item.title : item.authorName);
    }

    if (eyebrowLabel) {
        std::string eyebrow = "NOW PLAYING";
        if (m_isPodcastItem) {
            // "EPISODE 50 · 14 JUL 2013" — each half only if we have it.
            std::string left;
            if (item.episodeNumber > 0)
                left = "EPISODE " + std::to_string(item.episodeNumber);
            std::string right = item.pubDate;
            if (!left.empty() && !right.empty())      eyebrow = left + "  " + right;
            else if (!left.empty())                   eyebrow = left;
            else if (!right.empty())                  eyebrow = right;
        }
        eyebrowLabel->setText(eyebrow);
    }

    if (subtitleLabel) {
        // Narrator for books, host/show for podcasts.
        std::string subline = m_isPodcastItem ? item.authorName : item.narratorName;
        if (!subline.empty() && !m_isPodcastItem)
            subline = "Narrated by " + subline;
        subtitleLabel->setText(subline);
        subtitleLabel->setVisibility(subline.empty() ? brls::Visibility::GONE
                                                     : brls::Visibility::VISIBLE);
    }

    // Context slot: description for podcasts, chapter line for books.
    if (descriptionLabel) {
        bool show = m_isPodcastItem && !item.description.empty();
        descriptionLabel->setText(item.description);
        descriptionLabel->setVisibility(show ? brls::Visibility::VISIBLE
                                             : brls::Visibility::GONE);
    }
    if (chapterInfoLabel) {
        chapterInfoLabel->setVisibility(m_isPodcastItem ? brls::Visibility::GONE
                                                        : brls::Visibility::VISIBLE);
    }

    // Right stat tile.
    if (tileRightCaption && tileRightValue) {
        if (m_isPodcastItem) {
            tileRightCaption->setText("DOWNLOADED");
            bool local = m_isLocalFile || m_isPreDownloaded;
            if (local && item.size > 0) {
                tileRightValue->setText(std::to_string(item.size / (1024 * 1024)) + " MB");
            } else {
                tileRightValue->setText(local ? "YES" : "STREAMING");
            }
        } else {
            tileRightCaption->setText("CHAPTER");
            tileRightValue->setText(m_chapters.empty()
                                        ? "—"
                                        : "1 / " + std::to_string(m_chapters.size()));
        }
    }
}

void PlayerActivity::applyLocalInfo(const std::string& subline, bool isPodcast) {
    m_isPodcastItem = isPodcast;
    m_chapters.clear();
    m_currentChapter = -1;

    if (eyebrowLabel)  eyebrowLabel->setText("NOW PLAYING");
    if (subtitleLabel) {
        subtitleLabel->setText(subline);
        subtitleLabel->setVisibility(subline.empty() ? brls::Visibility::GONE
                                                     : brls::Visibility::VISIBLE);
    }
    if (descriptionLabel) descriptionLabel->setVisibility(brls::Visibility::GONE);
    if (chapterInfoLabel) chapterInfoLabel->setVisibility(brls::Visibility::VISIBLE);

    if (tileRightCaption && tileRightValue) {
        tileRightCaption->setText("DOWNLOADED");
        tileRightValue->setText("YES");
    }
}

void PlayerActivity::updateChapterTile(double position) {
    if (m_chapters.empty() || !tileRightValue) return;

    int idx = -1;
    for (size_t i = 0; i < m_chapters.size(); i++) {
        if (position >= m_chapters[i].start &&
            (m_chapters[i].end <= 0.0f || position < m_chapters[i].end)) {
            idx = static_cast<int>(i);
            break;
        }
    }
    if (idx < 0 || idx == m_currentChapter) return;

    m_currentChapter = idx;
    tileRightValue->setText(std::to_string(idx + 1) + " / " +
                            std::to_string(m_chapters.size()));

    // The context line names the chapter the tile is counting.
    if (chapterInfoLabel && !m_chapters[idx].title.empty()) {
        chapterInfoLabel->setText("Ch. " + std::to_string(idx + 1) + " — " +
                                  m_chapters[idx].title);
    }
}

void PlayerActivity::loadMedia() {
    // Prevent rapid re-entry
    if (m_loadingMedia) {
        brls::Logger::debug("PlayerActivity: Already loading media, skipping");
        return;
    }
    m_loadingMedia = true;

    // Handle pre-downloaded file (downloaded in media detail view before player push)
    if (m_isPreDownloaded && !m_tempFilePath.empty()) {
        brls::Logger::info("PlayerActivity: Playing pre-downloaded file: {}", m_tempFilePath);

        // First try to get metadata from downloads manager (works offline)
        DownloadsManager& downloads = DownloadsManager::getInstance();

        // Find the matching download item (works for both books and podcast episodes)
        std::string offlineTitle, offlineAuthor, offlineCoverPath, offlineCoverUrl;
        bool foundInDownloads = false;

        auto allDownloads = downloads.getDownloads();
        for (const auto& dl : allDownloads) {
            if (dl.itemId == m_itemId && dl.state == DownloadState::COMPLETED) {
                // For episodes, also match episodeId
                if (m_episodeId.empty() || dl.episodeId == m_episodeId) {
                    offlineTitle = dl.title;
                    offlineAuthor = dl.authorName;
                    offlineCoverPath = dl.localCoverPath;
                    offlineCoverUrl = dl.coverUrl;
                    foundInDownloads = true;
                    break;
                }
            }
        }

        // Pause background thumbnail loading and cancel in-flight loads BEFORE
        // queueing our own cover art — cancelAll() bumps the loader generation,
        // so calling it after loadCoverArt would cancel the player's own cover.
        ImageLoader::setPaused(true);
        ImageLoader::cancelAll();
        ImageLoader::clearCache();

        bool metadataLoaded = false;
        if (foundInDownloads) {
            brls::Logger::info("PlayerActivity: Using offline metadata from downloads manager");
            if (titleLabel && !offlineTitle.empty()) {
                titleLabel->setText(offlineTitle);
                metadataLoaded = true;
            }
            if (authorLabel && !offlineAuthor.empty()) {
                authorLabel->setText(offlineAuthor);
            }
            // Load local cover if available, otherwise try server
            if (!offlineCoverPath.empty()) {
                brls::Logger::info("PlayerActivity: Loading local cover: {}", offlineCoverPath);
                loadCoverArt(offlineCoverPath);
            } else if (!offlineCoverUrl.empty()) {
                loadCoverArt(offlineCoverUrl);
            }
        }

        // If no metadata from downloads, try server (when online)
        if (!metadataLoaded) {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            MediaItem item;
            if (client.fetchItem(m_itemId, item)) {
                applyItemInfo(item);
                if (!item.coverPath.empty()) {
                    std::string fullCoverUrl = client.getCoverUrl(m_itemId);
                    loadCoverArt(fullCoverUrl);
                }
            } else {
                brls::Logger::warning("PlayerActivity: Could not fetch metadata (offline or error)");
            }
        }

        // Initialize and load player
        MpvPlayer& player = MpvPlayer::getInstance();
        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                ImageLoader::setPaused(false);
                m_loadingMedia = false;
                return;
            }
        }

        std::string title = titleLabel ? titleLabel->getFullText() : m_itemId;
        // Pass start time directly to loadUrl for more reliable seeking
        double startTime = m_pendingSeek;
        m_pendingSeek = 0.0;  // Clear pending seek since we're handling it via loadUrl
        brls::Logger::info("PlayerActivity: Loading pre-downloaded file with startTime={}s", startTime);
        if (!player.loadUrl(m_tempFilePath, title, startTime)) {
            brls::Logger::error("Failed to load pre-downloaded file: {}", m_tempFilePath);
            m_loadingMedia = false;
            return;
        }

        // Apply saved playback speed
        AppSettings& preDownloadSettings = Application::getInstance().getSettings();
        float preDownloadSpeed = getSpeedValue(static_cast<int>(preDownloadSettings.playbackSpeed));
        if (preDownloadSpeed != 1.0f) {
            player.setSpeed(preDownloadSpeed);
        }



        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Handle direct file playback (debug/testing)
    if (m_isDirectFile) {
        brls::Logger::info("PlayerActivity: Playing direct file: {}", m_directFilePath);

        // Extract filename from path for title
        size_t lastSlash = m_directFilePath.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos)
            ? m_directFilePath.substr(lastSlash + 1)
            : m_directFilePath;

        if (titleLabel) {
            titleLabel->setText(filename);
        }

        if (authorLabel) {
            authorLabel->setText("Local File");
        }
        applyLocalInfo("", /*isPodcast=*/false);

        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                m_loadingMedia = false;
                return;
            }
        }

        // Load direct file
        if (!player.loadUrl(m_directFilePath, "Test File")) {
            brls::Logger::error("Failed to load direct file: {}", m_directFilePath);
            m_loadingMedia = false;
            return;
        }

        // Show video view


        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Handle local file playback (downloaded media)
    if (m_isLocalFile) {
        DownloadsManager& downloads = DownloadsManager::getInstance();

        // Try to fetch latest progress from server before playing (if online)
        // This ensures we have the most up-to-date resume position
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        if (client.isAuthenticated()) {
            brls::Logger::info("PlayerActivity: Fetching latest progress from server for {}", m_itemId);
            downloads.fetchProgressFromServer(m_itemId, m_episodeId);
        }

        // Get download item (using episodeId for podcast episodes)
        DownloadItem* download = downloads.getDownload(m_itemId, m_episodeId);

        if (!download || download->state != DownloadState::COMPLETED) {
            brls::Logger::error("PlayerActivity: Downloaded media not found or incomplete");
            m_loadingMedia = false;
            return;
        }

        // Get the playback path (handles multi-file audiobooks)
        std::string playbackPath = downloads.getPlaybackPath(m_itemId);
        if (playbackPath.empty()) {
            brls::Logger::error("PlayerActivity: Could not get playback path for: {}", m_itemId);
            m_loadingMedia = false;
            return;
        }

        brls::Logger::info("PlayerActivity: Playing local file: {}", playbackPath);

        // Set title
        if (titleLabel) {
            titleLabel->setText(download->title);
        }

        // Set author/parent title
        if (authorLabel) {
            if (!download->authorName.empty()) {
                authorLabel->setText(download->authorName);
            } else if (!download->parentTitle.empty()) {
                authorLabel->setText(download->parentTitle);
            }
        }
        // A downloaded podcast episode carries an episodeId; a book does not.
        applyLocalInfo(download->parentTitle, /*isPodcast=*/!m_episodeId.empty());

        // Load cover art if available
        if (!download->coverUrl.empty()) {
            loadCoverArt(download->coverUrl);
        }

        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                m_loadingMedia = false;
                return;
            }
        }

        // Calculate start time from saved viewOffset
        double startTime = (download->viewOffset > 0) ? download->viewOffset / 1000.0 : -1.0;
        brls::Logger::info("PlayerActivity: Loading local file with startTime={}s", startTime);

        // Load local file (using playback path for multi-file support)
        if (!player.loadUrl(playbackPath, download->title, startTime)) {
            brls::Logger::error("Failed to load local file: {}", playbackPath);
            m_loadingMedia = false;
            return;
        }

        // Apply saved playback speed
        AppSettings& localSettings = Application::getInstance().getSettings();
        float localSpeed = getSpeedValue(static_cast<int>(localSettings.playbackSpeed));
        if (localSpeed != 1.0f) {
            player.setSpeed(localSpeed);
        }

        // Show video view


        m_isPlaying = true;
        m_loadingMedia = false;
        return;
    }

    // Before attempting remote playback, check if content is downloaded
    // This handles the case when coming from library view with a downloaded episode
    {
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        downloadsMgr.init();

        // Check if this item (or episode) is downloaded
        if (downloadsMgr.isDownloaded(m_itemId, m_episodeId)) {
            brls::Logger::info("PlayerActivity: Item is downloaded, using local playback");

            // Try to fetch latest progress from server before playing (if online)
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            if (client.isAuthenticated()) {
                brls::Logger::info("PlayerActivity: Fetching latest progress from server for downloaded item");
                downloadsMgr.fetchProgressFromServer(m_itemId, m_episodeId);
            }

            // Find the download info (after potential server update)
            auto allDownloads = downloadsMgr.getDownloads();
            for (const auto& dl : allDownloads) {
                if (dl.itemId == m_itemId && dl.state == DownloadState::COMPLETED) {
                    if (m_episodeId.empty() || dl.episodeId == m_episodeId) {
                        // Found the matching download - use local playback
                        std::string playbackPath = dl.localPath;

                        // Set metadata
                        if (titleLabel && !dl.title.empty()) {
                            titleLabel->setText(dl.title);
                        }
                        if (authorLabel && !dl.authorName.empty()) {
                            authorLabel->setText(dl.authorName);
                        }

                        // Load cover (prefer local, fall back to URL)
                        if (!dl.localCoverPath.empty()) {
                            loadCoverArt(dl.localCoverPath);
                        } else if (!dl.coverUrl.empty()) {
                            loadCoverArt(dl.coverUrl);
                        }

                        // Initialize player
                        MpvPlayer& player = MpvPlayer::getInstance();
                        if (!player.isInitialized()) {
                            if (!player.init()) {
                                brls::Logger::error("Failed to initialize MPV player");
                                m_loadingMedia = false;
                                return;
                            }
                        }

                        // Calculate start time from saved position
                        double startTime = -1.0;
                        if (dl.currentTime > 0) {
                            startTime = dl.currentTime;
                        } else if (dl.viewOffset > 0) {
                            startTime = dl.viewOffset / 1000.0;
                        }

                        // Load local file with start time
                        brls::Logger::info("PlayerActivity: Loading downloaded file: {} (startTime={}s)", playbackPath, startTime);
                        if (!player.loadUrl(playbackPath, dl.title, startTime)) {
                            brls::Logger::error("Failed to load downloaded file: {}", playbackPath);
                            m_loadingMedia = false;
                            return;
                        }

                        // Apply saved playback speed
                        AppSettings& dlSettings = Application::getInstance().getSettings();
                        float dlSpeed = getSpeedValue(static_cast<int>(dlSettings.playbackSpeed));
                        if (dlSpeed != 1.0f) {
                            player.setSpeed(dlSpeed);
                        }



                        // Mark as local file for progress saving
                        m_isLocalFile = true;

                        m_isPlaying = true;
                        m_loadingMedia = false;
                        return;
                    }
                }
            }
        }
    }

    // Remote playback from Audiobookshelf server
    // Stream directly via URL - mpv handles HTTP streaming natively
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    MediaItem item;

    if (client.fetchItem(m_itemId, item)) {
        // Pause background thumbnail loading and cancel in-flight loads BEFORE
        // queueing our own cover art — cancelAll() bumps the loader generation,
        // so calling it after loadCoverArt would cancel the player's own cover.
        ImageLoader::setPaused(true);
        ImageLoader::cancelAll();
        ImageLoader::clearCache();

        // Title, author, eyebrow, subline, context slot and right tile.
        applyItemInfo(item);

        // Load cover art
        if (!item.coverPath.empty()) {
            std::string fullCoverUrl = client.getCoverUrl(m_itemId);
            loadCoverArt(fullCoverUrl);
        }

        // Start a playback session with Audiobookshelf
        PlaybackSession session;
        brls::Logger::info("PlayerActivity: Starting playback session for item: {}, episode: {}",
                          m_itemId, m_episodeId.empty() ? "(none)" : m_episodeId);

        if (!client.startPlaybackSession(m_itemId, session, m_episodeId)) {
            brls::Logger::error("Failed to start playback session for: {}", m_itemId);
            m_loadingMedia = false;
            return;
        }

        // Store session ID for periodic sync
        m_sessionId = session.id;

        brls::Logger::info("PlayerActivity: Session created - id: {}, audioTracks: {}, playMethod: {}",
                          session.id, session.audioTracks.size(), session.playMethod);

        float startTime = session.currentTime;
        brls::Logger::debug("PlayerActivity: Will resume from position: {}s", startTime);

        // Build direct stream URL from playback session
        std::string streamUrl;
        if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
            streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
        } else {
            streamUrl = client.getDirectStreamUrl(m_itemId, 0);
        }

        if (streamUrl.empty()) {
            brls::Logger::error("Failed to get stream URL for: {}", m_itemId);
            m_loadingMedia = false;
            return;
        }

        brls::Logger::info("PlayerActivity: Streaming directly from URL: {}", streamUrl);

        if (chapterInfoLabel) {
            chapterInfoLabel->setText("Streaming...");
        }

        // Initialize and play via direct URL streaming (mpv handles HTTP natively)
        MpvPlayer& player = MpvPlayer::getInstance();

        if (!player.isInitialized()) {
            brls::Logger::info("PlayerActivity: Initializing MPV player...");
            if (!player.init()) {
                brls::Logger::error("Failed to initialize MPV player");
                ImageLoader::setPaused(false);
                m_loadingMedia = false;
                return;
            }
            brls::Logger::info("PlayerActivity: MPV player initialized successfully");
        }

        brls::Logger::info("PlayerActivity: Loading stream URL (startTime={}s)", startTime);
        if (!player.loadUrl(streamUrl, item.title, startTime > 0 ? static_cast<double>(startTime) : -1.0)) {
            brls::Logger::error("Failed to load stream URL: {}", streamUrl);
            m_loadingMedia = false;
            return;
        }
        brls::Logger::info("PlayerActivity: MPV loadUrl succeeded, waiting for playback to start...");

        if (chapterInfoLabel) {
            chapterInfoLabel->setText("");  // Clear streaming status once loaded
        }

        // Apply saved playback speed
        AppSettings& playSettings = Application::getInstance().getSettings();
        float speed = getSpeedValue(static_cast<int>(playSettings.playbackSpeed));
        if (speed != 1.0f) {
            player.setSpeed(speed);
        }

        // Show video view for audio playback (shows progress/controls)


        m_isPlaying = true;
    } else {
        brls::Logger::error("Failed to fetch item details for: {}", m_itemId);
    }

    m_loadingMedia = false;
}

void PlayerActivity::updateProgress() {
    // Don't update if destroying or showing photo
    if (m_destroying || m_isPhoto) return;

    MpvPlayer& player = MpvPlayer::getInstance();

    if (!player.isInitialized()) return;

    // Always process MPV events to handle state transitions
    player.update();

    // Update play/pause button state
    updatePlayPauseButton();

    // Skip UI updates while MPV is still loading - be gentle on Vita's limited hardware
    if (player.isLoading()) {
        return;
    }

    // Handle pending seek when playback becomes ready
    static bool justSeeked = false;
    if (m_pendingSeek > 0.0) {
        // Try to seek once player is ready (playing or paused with valid duration)
        if (player.isPlaying() || (player.isPaused() && player.getDuration() > 0)) {
            brls::Logger::info("PlayerActivity: Seeking to resume position {}s", m_pendingSeek);
            player.seekTo(m_pendingSeek);
            m_pendingSeek = 0.0;
            justSeeked = true;
        }
    }

    double position = player.getPosition();
    double duration = player.getDuration();

    // Log position after seek to verify it worked
    if (justSeeked && position > 0) {
        brls::Logger::info("PlayerActivity: Position after seek: {}s", position);
        justSeeked = false;
    }

    // Store duration for later use
    if (duration > 0) {
        m_totalDuration = duration;
    }

    if (duration > 0) {
        // Update progress slider
        if (progressSlider) {
            progressSlider->setProgress((float)(position / duration));
        }

        // Update elapsed time label
        if (timeElapsedLabel) {
            timeElapsedLabel->setText(formatTime(position));
        }

        // Update remaining time label
        if (timeRemainingLabel) {
            double remaining = duration - position;
            timeRemainingLabel->setText(formatTimeRemaining(remaining));
        }

        // Right stat tile + chapter line follow the playhead. No-ops when
        // the item has no chapters, and when the chapter hasn't changed.
        updateChapterTile(position);
    }

    // Periodic progress sync (every 30 seconds while playing)
    if (m_isPlaying && !m_isDirectFile) {
        m_syncCounter++;
        if (m_syncCounter >= 30) {  // Every 30 updates (30 seconds)
            m_syncCounter = 0;
            float currentPos = static_cast<float>(position);
            // Only sync/save if position changed significantly (more than 5 seconds)
            if (std::abs(currentPos - m_lastSyncedTime) > 5.0f) {
                if (m_isLocalFile) {
                    // Save progress for downloaded media locally
                    DownloadsManager::getInstance().updateProgress(m_itemId, currentPos, m_episodeId);
                    DownloadsManager::getInstance().saveState();
                    brls::Logger::debug("PlayerActivity: Auto-saved local progress {}s", currentPos);

                    // Also sync to server if online
                    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
                    if (client.isAuthenticated()) {
                        float totalDuration = static_cast<float>(duration);
                        bool isPodcast = !m_episodeId.empty();
                        bool isFinished = shouldMarkAsFinished(currentPos, totalDuration, isPodcast);
                        client.updateProgress(m_itemId, currentPos, totalDuration, isFinished, m_episodeId);
                    }
                    m_lastSyncedTime = currentPos;
                } else {
                    // Remote playback - sync to server
                    syncProgressToServer();
                }
            }
        }
    }

    // Check for and display background download progress (multi-file audiobooks)
    AppSettings& settings = Application::getInstance().getSettings();
    if (settings.showDownloadProgress && chapterInfoLabel) {
        BackgroundDownloadProgress bgProgress = Application::getInstance().getBackgroundDownloadProgress();
        if (bgProgress.active && bgProgress.itemId == m_itemId) {
            // Show download progress
            char progressBuf[128];
            if (bgProgress.totalBytes > 0) {
                int percent = static_cast<int>((bgProgress.downloadedBytes * 100) / bgProgress.totalBytes);
                int dlMB = static_cast<int>(bgProgress.downloadedBytes / (1024 * 1024));
                int totalMB = static_cast<int>(bgProgress.totalBytes / (1024 * 1024));
                snprintf(progressBuf, sizeof(progressBuf), "Track %d/%d - %d%% (%d/%d MB)",
                        bgProgress.currentTrack, bgProgress.totalTracks,
                        percent, dlMB, totalMB);
            } else {
                snprintf(progressBuf, sizeof(progressBuf), "Track %d/%d - %s",
                        bgProgress.currentTrack, bgProgress.totalTracks,
                        bgProgress.status.c_str());
            }
            chapterInfoLabel->setText(progressBuf);
        } else if (!bgProgress.active && chapterInfoLabel->getFullText().find("Track") != std::string::npos) {
            // Clear the label when download completes
            chapterInfoLabel->setText("");
        }
    }

    // Check if playback ended (only if we were actually playing)
    if (m_isPlaying && player.hasEnded()) {
        m_isPlaying = false;  // Prevent multiple triggers
        float totalDuration = (float)player.getDuration();

        if (m_isLocalFile) {
            // Save completed progress for downloaded media
            DownloadsManager::getInstance().updateProgress(m_itemId, totalDuration, m_episodeId);
            DownloadsManager::getInstance().saveState();
            brls::Logger::info("PlayerActivity: Saved completed progress for local file");
        }

        // Mark as finished with Audiobookshelf (set isFinished=true)
        AudiobookshelfClient::getInstance().updateProgress(m_itemId, totalDuration, totalDuration, true, m_episodeId);
        brls::Application::popActivity();
    }
}

void PlayerActivity::togglePlayPause() {
    MpvPlayer& player = MpvPlayer::getInstance();

    if (player.isPlaying()) {
        player.pause();
        m_isPlaying = false;
    } else if (player.isPaused()) {
        player.play();
        m_isPlaying = true;
    }

    // Update button immediately
    updatePlayPauseButton();
}

void PlayerActivity::updatePlayPauseButton() {
    if (!playPauseIcon) return;

    MpvPlayer& player = MpvPlayer::getInstance();
    const int want = player.isPlaying() ? 1 : 0;   // 1 = show pause, 0 = show play
    if (want == m_playIconState) return;           // decoding a texture is not free

    m_playIconState = want;
    playPauseIcon->setImageFromRes(want ? "icons/pause.png" : "icons/play.png");
}

void PlayerActivity::seek(int seconds) {
    MpvPlayer& player = MpvPlayer::getInstance();
    player.seekRelative(seconds);
}

std::string PlayerActivity::formatTime(double seconds) {
    if (seconds < 0) seconds = 0;

    int totalSecs = (int)seconds;
    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;
    int secs = totalSecs % 60;

    char buf[32];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, mins, secs);
    } else {
        snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
    }
    return std::string(buf);
}

std::string PlayerActivity::formatTimeRemaining(double remaining) {
    if (remaining < 0) remaining = 0;

    int totalSecs = (int)remaining;
    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;
    int secs = totalSecs % 60;

    char buf[32];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "-%d:%02d:%02d", hours, mins, secs);
    } else {
        snprintf(buf, sizeof(buf), "-%d:%02d", mins, secs);
    }
    return std::string(buf);
}

void PlayerActivity::loadCoverArt(const std::string& coverUrl) {
    if (coverUrl.empty() || !coverImage) return;

    brls::Logger::debug("Loading cover art: {}", coverUrl);

    // Check if this is a local file path (starts with ux0: or / or doesn't start with http)
    bool isLocalPath = (coverUrl.find("ux0:") == 0 ||
                        coverUrl.find("/") == 0 ||
                        coverUrl.find("http") != 0);

    if (isLocalPath) {
        // Load local file directly
        brls::Logger::info("Loading local cover image: {}", coverUrl);
#ifdef __vita__
        SceUID fd = sceIoOpen(coverUrl.c_str(), SCE_O_RDONLY, 0);
        if (fd >= 0) {
            // Get file size
            SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
            sceIoLseek(fd, 0, SCE_SEEK_SET);

            if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB for cover
                std::vector<uint8_t> data(size);
                if (sceIoRead(fd, data.data(), size) == size) {
                    coverImage->setImageFromMem(data.data(), data.size());
                    brls::Logger::debug("Local cover art loaded ({} bytes)", size);
                }
            }
            sceIoClose(fd);
        } else {
            brls::Logger::warning("Failed to open local cover: {}", coverUrl);
        }
#else
        // Non-Vita: use standard file I/O
        std::ifstream file(coverUrl, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            if (size > 0 && size < 10 * 1024 * 1024) {
                std::vector<uint8_t> data(size);
                if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                    coverImage->setImageFromMem(data.data(), data.size());
                    brls::Logger::debug("Local cover art loaded ({} bytes)", size);
                }
            }
            file.close();
        }
#endif
    } else {
        // Use the image loader to load the cover asynchronously (HTTP).
        // loadMedia() pauses the loader before we run, so temporarily unpause
        // around our own load (VMA's showAlbumCover pattern) — the player's
        // cover must load even while background thumbnails are paused.
        bool wasPaused = ImageLoader::isPaused();
        if (wasPaused) ImageLoader::setPaused(false);
        ImageLoader::loadAsync(coverUrl, [](brls::Image* img) {
            brls::Logger::debug("Cover art loaded");
        }, coverImage, m_alive);
        if (wasPaused) ImageLoader::setPaused(true);
    }
}

float PlayerActivity::getSpeedValue(int index) {
    // Speed values matching PlaybackSpeed enum: 0.5x, 0.75x, 1.0x, 1.25x, 1.5x, 1.75x, 2.0x
    static const float speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
    if (index >= 0 && index < 7) {
        return speeds[index];
    }
    return 1.0f;
}

void PlayerActivity::updateSpeedLabel() {
    if (!speedLabel) return;

    AppSettings& settings = Application::getInstance().getSettings();
    float speed = getSpeedValue(static_cast<int>(settings.playbackSpeed));

    // Format speed with one decimal place
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << speed << "x";
    speedLabel->setText(ss.str());
}

void PlayerActivity::cyclePlaybackSpeed() {
    AppSettings& settings = Application::getInstance().getSettings();

    // Cycle through speeds: 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0
    int currentIndex = static_cast<int>(settings.playbackSpeed);
    int nextIndex = (currentIndex + 1) % 7;  // 7 speed options

    settings.playbackSpeed = static_cast<PlaybackSpeed>(nextIndex);
    Application::getInstance().saveSettings();

    // Apply new speed to player
    MpvPlayer& player = MpvPlayer::getInstance();
    float speed = getSpeedValue(nextIndex);
    player.setSpeed(speed);

    // Update the label
    updateSpeedLabel();

    brls::Logger::info("Playback speed changed to {}x", speed);
}

void PlayerActivity::syncProgressToServer() {
    MpvPlayer& player = MpvPlayer::getInstance();
    if (!player.isInitialized()) return;

    float currentTime = static_cast<float>(player.getPosition());
    float duration = static_cast<float>(player.getDuration());

    if (duration <= 0 || currentTime < 0) return;

    brls::Logger::debug("PlayerActivity: Periodic sync - {}s of {}s", currentTime, duration);

    // Use session sync if we have an active session, otherwise use progress update
    if (!m_sessionId.empty()) {
        AudiobookshelfClient::getInstance().syncPlaybackSession(m_sessionId, currentTime, duration);
    } else {
        AudiobookshelfClient::getInstance().updateProgress(m_itemId, currentTime, duration, false, m_episodeId);
    }

    m_lastSyncedTime = currentTime;
}

} // namespace vitaabs
