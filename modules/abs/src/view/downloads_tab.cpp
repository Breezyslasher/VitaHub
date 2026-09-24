/**
 * VitaABS - Downloads Tab Implementation
 * Shows both server download queue and local downloads (like Vita_Suwayomi)
 * Server Queue: items actively downloading from Audiobookshelf server
 * Local Downloads: completed items stored on device for offline playback
 */

#include "view/downloads_tab.hpp"
#include "app/downloads_manager.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/application.hpp"
#include "activity/player_activity.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include <fstream>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

// Auto-refresh interval in milliseconds
static const int AUTO_REFRESH_INTERVAL_MS = 3000;
static const int AUTO_REFRESH_INTERVAL_LARGE_MS = 5000;
static const int LARGE_QUEUE_THRESHOLD = 50;

namespace vitaabs {

// Helper to format bytes as human-readable MB string
static std::string formatMB(int64_t bytes) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb < 0.1) {
        // Show KB for very small values
        double kb = static_cast<double>(bytes) / 1024.0;
        int kbInt = static_cast<int>(kb * 10);
        return std::to_string(kbInt / 10) + "." + std::to_string(kbInt % 10) + " KB";
    }
    int mbInt = static_cast<int>(mb * 10);
    return std::to_string(mbInt / 10) + "." + std::to_string(mbInt % 10) + " MB";
}

// Helper to load local cover image on Vita
static void loadLocalCoverImage(brls::Image* image, const std::string& localPath) {
    if (localPath.empty() || !image) return;

#ifdef __vita__
    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);

        if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB
            std::vector<uint8_t> data(size);
            if (sceIoRead(fd, data.data(), size) == size) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        sceIoClose(fd);
    }
#else
    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        file.close();
    }
#endif
}

DownloadsTab::DownloadsTab() {
    m_alive = std::make_shared<std::atomic<bool>>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Header row with title and action buttons
    auto headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->setMargins(0, 0, 15, 0);
    this->addView(headerRow);

    // Left side: title and status
    auto titleBox = new brls::Box();
    titleBox->setAxis(brls::Axis::ROW);
    titleBox->setAlignItems(brls::AlignItems::CENTER);
    titleBox->setShrink(0);
    titleBox->setGrow(1.0f);
    headerRow->addView(titleBox);

    auto header = new brls::Label();
    header->setText("Downloads");
    header->setFontSize(24);
    header->setSingleLine(true);
    titleBox->addView(header);

    // Download status label
    m_downloadStatusLabel = new brls::Label();
    m_downloadStatusLabel->setText("");
    m_downloadStatusLabel->setFontSize(14);
    m_downloadStatusLabel->setMarginLeft(15);
    m_downloadStatusLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    titleBox->addView(m_downloadStatusLabel);

    // Actions row
    m_actionsRow = new brls::Box();
    m_actionsRow->setAxis(brls::Axis::ROW);
    m_actionsRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->addView(m_actionsRow);

    // Start/Stop toggle button
    m_startStopBtn = new brls::Button();
    m_startStopBtn->setWidth(70);
    m_startStopBtn->setHeight(32);
    m_startStopBtn->setCornerRadius(6);
    m_startStopBtn->setMarginRight(8);
    m_startStopBtn->setPaddingLeft(8);
    m_startStopBtn->setPaddingRight(8);
    m_startStopBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_startStopBtn->setAlignItems(brls::AlignItems::CENTER);

    m_startStopLabel = new brls::Label();
    m_startStopLabel->setText("Start");
    m_startStopLabel->setFontSize(12);
    m_startStopBtn->addView(m_startStopLabel);

    m_startStopBtn->registerClickAction([this](brls::View*) {
        bool wasRunning = m_downloaderRunning;
        DownloadsManager& mgr = DownloadsManager::getInstance();
        if (wasRunning) {
            mgr.pauseDownloads();
            m_downloaderRunning = false;
            if (m_startStopLabel) m_startStopLabel->setText("Start");
            if (m_downloadStatusLabel) {
                m_downloadStatusLabel->setText("- Stopped");
                m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
            }
            brls::Application::notify("Downloads paused");
        } else {
            mgr.resumeIncompleteDownloads();
            mgr.startDownloads();
            m_downloaderRunning = true;
            if (m_startStopLabel) m_startStopLabel->setText("Pause");
            if (m_downloadStatusLabel) {
                m_downloadStatusLabel->setText("- Downloading");
                m_downloadStatusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
            }
            brls::Application::notify("Downloads started");
        }
        refresh();
        return true;
    });
    m_actionsRow->addView(m_startStopBtn);

    // Stop button
    m_pauseBtn = new brls::Button();
    m_pauseBtn->setWidth(60);
    m_pauseBtn->setHeight(32);
    m_pauseBtn->setCornerRadius(6);
    m_pauseBtn->setMarginRight(8);
    m_pauseBtn->setPaddingLeft(8);
    m_pauseBtn->setPaddingRight(8);
    m_pauseBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_pauseBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* pauseLabel = new brls::Label();
    pauseLabel->setText("Stop");
    pauseLabel->setFontSize(12);
    m_pauseBtn->addView(pauseLabel);

    m_pauseBtn->registerClickAction([this](brls::View*) {
        DownloadsManager& mgr = DownloadsManager::getInstance();
        mgr.pauseDownloads();
        m_downloaderRunning = false;
        if (m_startStopLabel) m_startStopLabel->setText("Start");
        if (m_downloadStatusLabel) {
            m_downloadStatusLabel->setText("- Stopped");
            m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
        }
        brls::Application::notify("Downloads stopped");
        refresh();
        return true;
    });
    m_actionsRow->addView(m_pauseBtn);

    // Clear button
    m_clearBtn = new brls::Button();
    m_clearBtn->setWidth(70);
    m_clearBtn->setHeight(32);
    m_clearBtn->setCornerRadius(6);
    m_clearBtn->setMarginRight(8);
    m_clearBtn->setPaddingLeft(8);
    m_clearBtn->setPaddingRight(8);
    m_clearBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_clearBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* clearLabel = new brls::Label();
    clearLabel->setText("Clear");
    clearLabel->setFontSize(12);
    m_clearBtn->addView(clearLabel);

    m_clearBtn->registerClickAction([this](brls::View*) {
        DownloadsManager& mgr = DownloadsManager::getInstance();

        // Pause downloads - the actual wait + clear runs off the UI thread
        mgr.pauseDownloads();

        m_downloaderRunning = false;
        if (m_startStopLabel) m_startStopLabel->setText("Start");
        if (m_downloadStatusLabel) m_downloadStatusLabel->setText("- Clearing...");

        std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;
        asyncRun([this, aliveWeak]() {
            DownloadsManager& mgr = DownloadsManager::getInstance();
            mgr.waitForDownloadThread();

            // Cancel all non-completed downloads
            auto downloads = mgr.getDownloadStates();
            for (const auto& item : downloads) {
                if (item.state != DownloadState::COMPLETED) {
                    mgr.cancelDownload(item.itemId);
                }
            }

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (m_downloadStatusLabel) m_downloadStatusLabel->setText("");
                brls::Application::notify("Queue cleared");
                refresh();
            });
        });
        return true;
    });
    m_actionsRow->addView(m_clearBtn);

    // Sync button
    m_syncBtn = new brls::Button();
    m_syncBtn->setWidth(70);
    m_syncBtn->setHeight(32);
    m_syncBtn->setCornerRadius(6);
    m_syncBtn->setPaddingLeft(8);
    m_syncBtn->setPaddingRight(8);
    m_syncBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_syncBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* syncLabel = new brls::Label();
    syncLabel->setText("Sync");
    syncLabel->setFontSize(12);
    m_syncBtn->addView(syncLabel);

    m_syncBtn->registerClickAction([](brls::View*) {
        asyncRun([]() {
            DownloadsManager::getInstance().syncProgressToServer();
            brls::sync([]() {
                brls::Application::notify("Progress synced to server");
            });
        });
        return true;
    });
    m_actionsRow->addView(m_syncBtn);

    // === Local Downloads Section ===
    m_serverSection = new brls::Box();
    m_serverSection->setAxis(brls::Axis::COLUMN);
    m_serverSection->setGrow(1.0f);
    m_serverSection->setMargins(0, 0, 15, 0);
    m_serverSection->setVisibility(brls::Visibility::GONE);
    this->addView(m_serverSection);

    m_serverHeader = new brls::Label();
    m_serverHeader->setText("Local Downloads");
    m_serverHeader->setFontSize(18);
    m_serverHeader->setMargins(0, 0, 10, 0);
    m_serverSection->addView(m_serverHeader);

    m_serverEmptyLabel = new brls::Label();
    m_serverEmptyLabel->setText("No active downloads");
    m_serverEmptyLabel->setFontSize(14);
    m_serverEmptyLabel->setTextColor(nvgRGBA(120, 120, 120, 255));
    m_serverEmptyLabel->setMargins(10, 0, 10, 0);
    m_serverSection->addView(m_serverEmptyLabel);

    m_serverScroll = new brls::ScrollingFrame();
    m_serverScroll->setGrow(1.0f);
    m_serverScroll->setVisibility(brls::Visibility::GONE);
    m_serverSection->addView(m_serverScroll);

    m_serverContainer = new brls::Box();
    m_serverContainer->setAxis(brls::Axis::COLUMN);
    m_serverScroll->setContentView(m_serverContainer);

    // === Server Downloads Section (ABS server downloading podcast episodes) ===
    m_absSection = new brls::Box();
    m_absSection->setAxis(brls::Axis::COLUMN);
    m_absSection->setMargins(0, 0, 15, 0);
    m_absSection->setVisibility(brls::Visibility::GONE);
    this->addView(m_absSection);

    m_absHeader = new brls::Label();
    m_absHeader->setText("Server Downloads");
    m_absHeader->setFontSize(18);
    m_absHeader->setMargins(0, 0, 10, 0);
    m_absSection->addView(m_absHeader);

    m_absEmptyLabel = new brls::Label();
    m_absEmptyLabel->setText("No server downloads");
    m_absEmptyLabel->setFontSize(14);
    m_absEmptyLabel->setTextColor(nvgRGBA(120, 120, 120, 255));
    m_absEmptyLabel->setMargins(10, 0, 10, 0);
    m_absSection->addView(m_absEmptyLabel);

    m_absContainer = new brls::Box();
    m_absContainer->setAxis(brls::Axis::COLUMN);
    m_absSection->addView(m_absContainer);

    // Empty state
    m_emptyStateBox = new brls::Box();
    m_emptyStateBox->setAxis(brls::Axis::COLUMN);
    m_emptyStateBox->setJustifyContent(brls::JustifyContent::CENTER);
    m_emptyStateBox->setAlignItems(brls::AlignItems::CENTER);
    m_emptyStateBox->setGrow(1.0f);
    m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);

    auto* emptyIcon = new brls::Label();
    emptyIcon->setText("No Downloads");
    emptyIcon->setFontSize(24);
    emptyIcon->setTextColor(nvgRGB(128, 128, 128));
    emptyIcon->setMarginBottom(10);
    m_emptyStateBox->addView(emptyIcon);

    auto* emptyHint = new brls::Label();
    emptyHint->setText("Use the download button on media details to save for offline listening");
    emptyHint->setFontSize(16);
    emptyHint->setTextColor(nvgRGB(100, 100, 100));
    m_emptyStateBox->addView(emptyHint);

    this->addView(m_emptyStateBox);
}

void DownloadsTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);

    // Re-arm alive flag
    m_alive = std::make_shared<std::atomic<bool>>(true);

    // Initialize downloads manager once (not on every refresh)
    DownloadsManager::getInstance().init();

    // Clear tracking vectors for fresh start
    m_serverRowElements.clear();
    m_lastServerItems.clear();
    m_currentFocusedIcon = nullptr;

    // Remove stale row views (invalidate the row-scoped alive flag first so
    // pending cover uploads can't touch the deleted rows' images)
    if (m_rowsAlive) *m_rowsAlive = false;
    m_rowsAlive = std::make_shared<std::atomic<bool>>(true);
    if (m_serverContainer) {
        while (m_serverContainer->getChildren().size() > 0)
            m_serverContainer->removeView(m_serverContainer->getChildren()[0]);
    }

    m_lastProgressRefresh = std::chrono::steady_clock::now();

    // Register progress callback for live UI updates
    DownloadsManager& mgr = DownloadsManager::getInstance();
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;
    mgr.setProgressCallback([this, aliveWeak](float downloadedBytes, float totalBytes) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProgressRefresh).count();

        // Update every 500ms to avoid flooding the UI thread
        const int FAST_PROGRESS_INTERVAL_MS = 500;
        if (elapsed >= FAST_PROGRESS_INTERVAL_MS || downloadedBytes >= totalBytes) {
            m_lastProgressRefresh = now;
            if (m_autoRefreshEnabled.load()) {
                brls::sync([this, aliveWeak]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) return;
                    if (m_autoRefreshEnabled.load()) {
                        refreshServerQueue();
                    }
                });
            }
        }
    });

    // Register item completion callback
    mgr.setItemCompletionCallback([this, aliveWeak](const std::string& itemId, const std::string& episodeId, bool success) {
        if (!m_autoRefreshEnabled.load()) return;

        brls::sync([this, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (!m_autoRefreshEnabled.load()) return;
            // Full refresh to move item from server queue to local downloads
            refresh();
        });
    });

    refresh();
    refreshServerDownloads();  // Fetch ABS server download queue (network, only on tab appear)
    startAutoRefresh();
}

DownloadsTab::~DownloadsTab() {
    if (m_alive) *m_alive = false;
    if (m_rowsAlive) *m_rowsAlive = false;
}

void DownloadsTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);

    if (m_alive) *m_alive = false;
    if (m_rowsAlive) *m_rowsAlive = false;

    stopAutoRefresh();

    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.setProgressCallback(nullptr);
    mgr.setItemCompletionCallback(nullptr);

    ImageLoader::cancelAll();

    m_serverRowElements.clear();
    m_currentFocusedIcon = nullptr;
}

void DownloadsTab::refresh() {
    refreshServerQueue();
    // Note: refreshServerDownloads() is NOT called here - it makes network requests
    // and should only be called on tab appear, not on every auto-refresh/progress tick
}

void DownloadsTab::refreshServerQueue() {
    DownloadsManager& mgr = DownloadsManager::getInstance();

    // Use lightweight state snapshot (no deep copy of chapters/files)
    auto downloads = mgr.getDownloadStates();

    // Build list of active (non-completed) items for server queue
    struct ServerInfo {
        std::string itemId, episodeId, title, authorName, coverUrl, localCoverPath;
        int64_t downloadedBytes, totalBytes;
        int state;
    };
    std::vector<ServerInfo> serverItems;

    bool isAnyDownloading = false;
    for (const auto& item : downloads) {
        if (item.state != DownloadState::COMPLETED) {
            ServerInfo si;
            si.itemId = item.itemId;
            si.episodeId = item.episodeId;
            si.title = item.title;
            si.authorName = item.authorName;
            si.coverUrl = item.coverUrl;
            si.localCoverPath = item.localCoverPath;
            si.downloadedBytes = item.downloadedBytes;
            si.totalBytes = item.totalBytes;
            si.state = static_cast<int>(item.state);
            serverItems.push_back(si);
            if (item.state == DownloadState::DOWNLOADING) isAnyDownloading = true;
        }
    }

    // Update status
    m_downloaderRunning = isAnyDownloading || mgr.isDownloading();
    if (m_startStopLabel) {
        m_startStopLabel->setText(m_downloaderRunning ? "Pause" : "Start");
    }
    if (m_downloadStatusLabel) {
        if (serverItems.empty()) {
            m_downloadStatusLabel->setText("");
        } else if (m_downloaderRunning) {
            m_downloadStatusLabel->setText("- Downloading");
            m_downloadStatusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
        } else {
            bool hasFailed = false;
            for (const auto& si : serverItems) {
                if (si.state == static_cast<int>(DownloadState::FAILED)) { hasFailed = true; break; }
            }
            if (hasFailed) {
                m_downloadStatusLabel->setText("- Error");
                m_downloadStatusLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
            } else {
                m_downloadStatusLabel->setText("- Stopped");
                m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
            }
        }
    }

    if (serverItems.empty()) {
        m_serverSection->setVisibility(brls::Visibility::GONE);
        m_lastServerItems.clear();
        m_serverRowElements.clear();
        if (m_rowsAlive) *m_rowsAlive = false;
        m_rowsAlive = std::make_shared<std::atomic<bool>>(true);
        while (m_serverContainer->getChildren().size() > 0) {
            m_serverContainer->removeView(m_serverContainer->getChildren()[0]);
        }

        if (m_emptyStateBox) {
            m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        }
        return;
    }

    // Hide empty state
    if (m_emptyStateBox) m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    m_serverSection->setVisibility(brls::Visibility::VISIBLE);
    m_serverEmptyLabel->setVisibility(brls::Visibility::GONE);
    m_serverScroll->setVisibility(brls::Visibility::VISIBLE);

    // Build new cache
    std::vector<CachedServerItem> newCache;
    for (const auto& si : serverItems) {
        CachedServerItem cached;
        cached.itemId = si.itemId;
        cached.episodeId = si.episodeId;
        cached.downloadedBytes = si.downloadedBytes;
        cached.totalBytes = si.totalBytes;
        cached.state = si.state;
        newCache.push_back(cached);
    }

    // Check for in-place progress updates (same items, just progress changed)
    bool structureChanged = (newCache.size() != m_lastServerItems.size());
    if (!structureChanged) {
        for (size_t i = 0; i < newCache.size(); i++) {
            if (newCache[i].itemId != m_lastServerItems[i].itemId ||
                newCache[i].episodeId != m_lastServerItems[i].episodeId) {
                structureChanged = true;
                break;
            }
        }
    }

    // Update in-place if possible (no structural changes)
    if (!structureChanged) {
        for (size_t i = 0; i < newCache.size() && i < m_serverRowElements.size(); i++) {
            const auto& newItem = newCache[i];
            const auto& oldItem = m_lastServerItems[i];
            if (newItem.downloadedBytes != oldItem.downloadedBytes ||
                newItem.state != oldItem.state) {
                if (m_serverRowElements[i].progressLabel) {
                    std::string progressText;
                    if (newItem.state == static_cast<int>(DownloadState::DOWNLOADING)) {
                        // Show progress in MB format
                        progressText = formatMB(newItem.downloadedBytes) + " / " + formatMB(newItem.totalBytes);
                        m_serverRowElements[i].progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
                    } else if (newItem.state == static_cast<int>(DownloadState::QUEUED)) {
                        progressText = "Queued";
                        m_serverRowElements[i].progressLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
                    } else if (newItem.state == static_cast<int>(DownloadState::PAUSED)) {
                        progressText = "Paused - " + formatMB(newItem.downloadedBytes) + " / " + formatMB(newItem.totalBytes);
                        m_serverRowElements[i].progressLabel->setTextColor(nvgRGBA(200, 180, 100, 255));
                    } else if (newItem.state == static_cast<int>(DownloadState::FAILED)) {
                        progressText = "Failed";
                        m_serverRowElements[i].progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
                    }
                    m_serverRowElements[i].progressLabel->setText(progressText);

                    // Update background color
                    if (m_serverRowElements[i].row) {
                        if (newItem.state == static_cast<int>(DownloadState::DOWNLOADING)) {
                            m_serverRowElements[i].row->setBackgroundColor(nvgRGBA(30, 60, 30, 200));
                        } else if (newItem.state == static_cast<int>(DownloadState::FAILED)) {
                            m_serverRowElements[i].row->setBackgroundColor(nvgRGBA(60, 30, 30, 200));
                        } else if (newItem.state == static_cast<int>(DownloadState::PAUSED)) {
                            m_serverRowElements[i].row->setBackgroundColor(nvgRGBA(50, 50, 30, 200));
                        } else {
                            m_serverRowElements[i].row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
                        }
                    }
                }
            }
        }
        m_lastServerItems = newCache;
        return;
    }

    // Full rebuild needed
    m_lastServerItems = newCache;
    m_serverRowElements.clear();
    if (m_rowsAlive) *m_rowsAlive = false;
    m_rowsAlive = std::make_shared<std::atomic<bool>>(true);
    while (m_serverContainer->getChildren().size() > 0) {
        m_serverContainer->removeView(m_serverContainer->getChildren()[0]);
    }

    for (const auto& si : serverItems) {
        brls::Label* progressLabel = nullptr;
        brls::Image* xButtonIcon = nullptr;
        auto* row = createServerRow(si.itemId, si.episodeId, si.title, si.authorName,
                                     si.downloadedBytes, si.totalBytes, si.state,
                                     si.coverUrl, si.localCoverPath,
                                     progressLabel, xButtonIcon);
        m_serverContainer->addView(row);

        ServerRowElements elem;
        elem.row = row;
        elem.progressLabel = progressLabel;
        elem.xButtonIcon = xButtonIcon;
        elem.itemId = si.itemId;
        elem.episodeId = si.episodeId;
        m_serverRowElements.push_back(elem);
    }

    updateNavigationRoutes();
}

brls::Box* DownloadsTab::createServerRow(const std::string& itemId, const std::string& episodeId,
                                          const std::string& title, const std::string& authorName,
                                          int64_t downloadedBytes, int64_t totalBytes, int state,
                                          const std::string& coverUrl, const std::string& localCoverPath,
                                          brls::Label*& outProgressLabel, brls::Image*& outXButtonIcon) {
    auto row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(8);
    row->setMargins(0, 0, 8, 0);
    row->setCornerRadius(6);
    row->setFocusable(true);

    // Background color based on state
    if (state == static_cast<int>(DownloadState::DOWNLOADING)) {
        row->setBackgroundColor(nvgRGBA(30, 60, 30, 200));
    } else if (state == static_cast<int>(DownloadState::FAILED)) {
        row->setBackgroundColor(nvgRGBA(60, 30, 30, 200));
    } else if (state == static_cast<int>(DownloadState::PAUSED)) {
        row->setBackgroundColor(nvgRGBA(50, 50, 30, 200));
    } else {
        row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
    }

    // Cover image
    auto coverImage = new brls::Image();
    coverImage->setWidth(50);
    coverImage->setHeight(50);
    coverImage->setCornerRadius(4);
    coverImage->setMargins(0, 12, 0, 0);
    row->addView(coverImage);

    if (!localCoverPath.empty()) {
        loadLocalCoverImage(coverImage, localCoverPath);
    } else if (!coverUrl.empty()) {
        ImageLoader::loadAsync(coverUrl, [](brls::Image*) {}, coverImage,
                               m_rowsAlive ? m_rowsAlive : m_alive);
    }

    // Info column (left side, grows)
    auto infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setGrow(1.0f);

    auto titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(16);
    titleLabel->setSingleLine(true);
    infoBox->addView(titleLabel);

    if (!authorName.empty()) {
        auto authorLabel = new brls::Label();
        authorLabel->setText(authorName);
        authorLabel->setFontSize(13);
        authorLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
        authorLabel->setSingleLine(true);
        infoBox->addView(authorLabel);
    }

    row->addView(infoBox);

    // Status box (right side: progress label + square button icon)
    auto statusBox = new brls::Box();
    statusBox->setAxis(brls::Axis::ROW);
    statusBox->setAlignItems(brls::AlignItems::CENTER);

    // Progress label with MB format
    auto progressLabel = new brls::Label();
    progressLabel->setFontSize(14);
    progressLabel->setMargins(0, 0, 0, 10);

    std::string progressText;
    if (state == static_cast<int>(DownloadState::DOWNLOADING)) {
        progressText = formatMB(downloadedBytes) + " / " + formatMB(totalBytes);
        progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
    } else if (state == static_cast<int>(DownloadState::QUEUED)) {
        progressText = "Queued";
    } else if (state == static_cast<int>(DownloadState::PAUSED)) {
        progressText = "Paused - " + formatMB(downloadedBytes) + " / " + formatMB(totalBytes);
        progressLabel->setTextColor(nvgRGBA(200, 180, 100, 255));
    } else if (state == static_cast<int>(DownloadState::FAILED)) {
        progressText = "Failed";
        progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
    }
    progressLabel->setText(progressText);
    outProgressLabel = progressLabel;
    statusBox->addView(progressLabel);

    // Square button icon - only visible when row is focused
    auto* xButtonIcon = new brls::Image();
    xButtonIcon->setWidth(24);
    xButtonIcon->setHeight(24);
    xButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    xButtonIcon->setImageFromFile(RESOURCE_PREFIX "images/square_button.png");
    xButtonIcon->setMarginLeft(8);
    xButtonIcon->setVisibility(brls::Visibility::INVISIBLE);
    outXButtonIcon = xButtonIcon;
    statusBox->addView(xButtonIcon);

    row->addView(statusBox);

    // Show square button icon when this row gets focus
    row->getFocusEvent()->subscribe([this, xButtonIcon](brls::View* view) {
        // Hide previous focused icon (validate it's still a live element)
        if (m_currentFocusedIcon && m_currentFocusedIcon != xButtonIcon) {
            bool isValid = false;
            for (const auto& elem : m_serverRowElements) {
                if (elem.xButtonIcon == m_currentFocusedIcon) { isValid = true; break; }
            }
            if (isValid) {
                m_currentFocusedIcon->setVisibility(brls::Visibility::INVISIBLE);
            } else {
                m_currentFocusedIcon = nullptr;
            }
        }
        xButtonIcon->setVisibility(brls::Visibility::VISIBLE);
        m_currentFocusedIcon = xButtonIcon;
    });

    // Square button action - cancel download
    std::string capturedItemId = itemId;
    row->registerAction("Cancel", brls::ControllerButton::BUTTON_X, [this, capturedItemId](brls::View*) {
        DownloadsManager::getInstance().cancelDownload(capturedItemId);
        brls::Application::notify("Download cancelled");
        refresh();
        return true;
    });

    return row;
}

void DownloadsTab::refreshServerDownloads() {
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        // Get all libraries to find podcast libraries
        std::vector<Library> libraries;
        if (!client.fetchLibraries(libraries)) {
            return;
        }

        // Collect server downloads from all podcast libraries
        std::vector<ServerEpisodeDownload> allDownloads;
        for (const auto& lib : libraries) {
            if (lib.mediaType != "podcast") continue;

            ServerEpisodeDownload current;
            bool hasCurrent = false;
            std::vector<ServerEpisodeDownload> queue;

            if (client.fetchEpisodeDownloads(lib.id, current, hasCurrent, queue)) {
                if (hasCurrent && !current.isFinished) {
                    allDownloads.push_back(current);
                }
                for (const auto& dl : queue) {
                    if (!dl.isFinished) {
                        allDownloads.push_back(dl);
                    }
                }
            }
        }

        brls::sync([this, aliveWeak, allDownloads]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            // Clear existing rows
            while (m_absContainer->getChildren().size() > 0) {
                m_absContainer->removeView(m_absContainer->getChildren()[0]);
            }

            if (allDownloads.empty()) {
                m_absSection->setVisibility(brls::Visibility::GONE);
                return;
            }

            m_absSection->setVisibility(brls::Visibility::VISIBLE);
            m_absEmptyLabel->setVisibility(brls::Visibility::GONE);

            for (size_t i = 0; i < allDownloads.size(); i++) {
                const auto& dl = allDownloads[i];

                auto* row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                row->setAlignItems(brls::AlignItems::CENTER);
                row->setPadding(8);
                row->setMargins(0, 0, 6, 0);
                row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
                row->setCornerRadius(6);

                auto* infoBox = new brls::Box();
                infoBox->setAxis(brls::Axis::COLUMN);
                infoBox->setGrow(1.0f);

                auto* titleLabel = new brls::Label();
                titleLabel->setText(dl.episodeTitle);
                titleLabel->setFontSize(15);
                titleLabel->setSingleLine(true);
                infoBox->addView(titleLabel);

                auto* podcastLabel = new brls::Label();
                podcastLabel->setText(dl.podcastTitle);
                podcastLabel->setFontSize(13);
                podcastLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
                podcastLabel->setSingleLine(true);
                infoBox->addView(podcastLabel);

                row->addView(infoBox);

                auto* statusLabel = new brls::Label();
                statusLabel->setFontSize(13);
                if (i == 0 && !dl.failed) {
                    statusLabel->setText("Downloading");
                    statusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
                } else if (dl.failed) {
                    statusLabel->setText("Failed");
                    statusLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
                } else {
                    statusLabel->setText("Queued");
                    statusLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
                }
                row->addView(statusLabel);

                m_absContainer->addView(row);
            }
        });
    });
}

void DownloadsTab::startAutoRefresh() {
    if (m_autoRefreshTimerActive.load()) return;
    m_autoRefreshEnabled.store(true);
    m_autoRefreshTimerActive.store(true);

    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        while (m_autoRefreshEnabled.load()) {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) break;

            int totalItems = static_cast<int>(m_lastServerItems.size());
            int interval = (totalItems > LARGE_QUEUE_THRESHOLD) ?
                           AUTO_REFRESH_INTERVAL_LARGE_MS : AUTO_REFRESH_INTERVAL_MS;

            std::this_thread::sleep_for(std::chrono::milliseconds(interval));

            alive = aliveWeak.lock();
            if (!alive || !*alive) break;
            if (!m_autoRefreshEnabled.load()) break;

            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (m_autoRefreshEnabled.load()) {
                    refresh();
                }
            });
        }
        m_autoRefreshTimerActive.store(false);
    });
}

void DownloadsTab::stopAutoRefresh() {
    m_autoRefreshEnabled.store(false);
}

void DownloadsTab::updateNavigationRoutes() {
    // Let borealis handle default navigation between focusable items
}

} // namespace vitaabs
