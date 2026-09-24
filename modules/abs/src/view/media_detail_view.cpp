/**
 * VitaABS - Media Detail View implementation
 */

#include "view/media_detail_view.hpp"
#include "app/audiobookshelf_client.hpp"
#include "view/media_item_cell.hpp"
#include "view/progress_dialog.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "utils/audio_utils.hpp"
#include "utils/async.hpp"
#include <thread>
#include <algorithm>
#include <functional>
#include <fstream>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace {

// Box subclass that shows/hides a square button hint on focus
class FocusHintBox : public brls::Box {
public:
    void setHintImage(brls::Image* hint) { m_hint = hint; }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        if (m_hint) m_hint->setVisibility(brls::Visibility::VISIBLE);
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        if (m_hint) m_hint->setVisibility(brls::Visibility::GONE);
    }

private:
    brls::Image* m_hint = nullptr;
};

} // anonymous namespace

namespace vitaabs {

MediaDetailView::MediaDetailView(const MediaItem& item)
    : m_item(item), m_alive(std::make_shared<std::atomic<bool>>(true)) {
    brls::Logger::info("MediaDetailView: Creating for '{}' id='{}' type='{}'",
                       item.title, item.id, item.type);

    // Komikku-style horizontal layout: left panel (cover + buttons) | right panel (info + list)
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);
    this->setBackgroundColor(nvgRGB(18, 18, 18));

    // Register back button (B/Circle) to pop this activity
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::Sound::SOUND_BACK);

    // ===== LEFT PANEL: Cover + Action Buttons =====
    auto* leftPanel = new brls::Box();
    leftPanel->setAxis(brls::Axis::COLUMN);
    leftPanel->setWidth(260);
    leftPanel->setPadding(20);
    leftPanel->setBackgroundColor(nvgRGB(28, 28, 28));

    // Cover image (centered)
    auto* coverContainer = new brls::Box();
    coverContainer->setAxis(brls::Axis::ROW);
    coverContainer->setJustifyContent(brls::JustifyContent::CENTER);
    coverContainer->setMarginBottom(15);

    m_posterImage = new brls::Image();
    m_posterImage->setSize(brls::Size(200, 200));
    m_posterImage->setScalingType(brls::ImageScalingType::FIT);
    m_posterImage->setCornerRadius(12);
    coverContainer->addView(m_posterImage);
    leftPanel->addView(coverContainer);

    // Action buttons for books and podcast episodes
    if (m_item.mediaType == MediaType::BOOK || m_item.mediaType == MediaType::PODCAST_EPISODE) {
        // Select button hint above play button
        auto* selectHintContainer = new brls::Box();
        selectHintContainer->setAxis(brls::Axis::ROW);
        selectHintContainer->setJustifyContent(brls::JustifyContent::FLEX_START);
        selectHintContainer->setMarginBottom(4);
        auto* selectHint = new brls::Image();
        selectHint->setSize(brls::Size(64, 16));
        selectHint->setScalingType(brls::ImageScalingType::FIT);
        selectHint->setImageFromFile(RESOURCE_PREFIX "images/select_button.png");
        selectHintContainer->addView(selectHint);
        leftPanel->addView(selectHintContainer);

        m_playButton = new brls::Button();
        m_playButton->setText("Play");
        m_playButton->setWidth(220);
        m_playButton->setHeight(40);
        m_playButton->setCornerRadius(20);
        m_playButton->setMarginBottom(8);
        m_playButton->setBackgroundColor(nvgRGBA(0, 128, 128, 255));
        m_playButton->registerClickAction([this](brls::View* view) {
            onPlay(true);
            return true;
        });
        leftPanel->addView(m_playButton);

        bool isDownloaded = DownloadsManager::getInstance().isDownloaded(m_item.id);

        if (!isDownloaded) {
            m_downloadButton = new brls::Button();
            m_downloadButton->setText("Download");
            m_downloadButton->setWidth(220);
            m_downloadButton->setHeight(40);
            m_downloadButton->setCornerRadius(20);
            m_downloadButton->setMarginBottom(8);
            m_downloadButton->registerClickAction([this](brls::View* view) {
                onDownload();
                return true;
            });
            leftPanel->addView(m_downloadButton);
        }

        if (isDownloaded) {
            m_deleteButton = new brls::Button();
            m_deleteButton->setText("Delete");
            m_deleteButton->setWidth(220);
            m_deleteButton->setHeight(40);
            m_deleteButton->setCornerRadius(20);
            m_deleteButton->setBackgroundColor(nvgRGBA(180, 60, 60, 255));
            m_deleteButton->registerClickAction([this](brls::View* view) {
                onDeleteDownload();
                return true;
            });
            leftPanel->addView(m_deleteButton);
        }
    }

    // Action buttons for podcasts
    if (m_item.mediaType == MediaType::PODCAST) {
        // Select button hint above play button
        auto* selectHintContainer = new brls::Box();
        selectHintContainer->setAxis(brls::Axis::ROW);
        selectHintContainer->setJustifyContent(brls::JustifyContent::FLEX_START);
        selectHintContainer->setMarginBottom(4);
        auto* selectHint = new brls::Image();
        selectHint->setSize(brls::Size(64, 16));
        selectHint->setScalingType(brls::ImageScalingType::FIT);
        selectHint->setImageFromFile(RESOURCE_PREFIX "images/select_button.png");
        selectHintContainer->addView(selectHint);
        leftPanel->addView(selectHintContainer);

        m_playButton = new brls::Button();
        m_playButton->setText("Play");
        m_playButton->setWidth(220);
        m_playButton->setHeight(40);
        m_playButton->setCornerRadius(20);
        m_playButton->setMarginBottom(8);
        m_playButton->setBackgroundColor(nvgRGBA(0, 128, 128, 255));
        m_playButton->registerClickAction([this](brls::View* view) {
            onPlay(false);
            return true;
        });
        leftPanel->addView(m_playButton);

        m_findEpisodesButton = new brls::Button();
        m_findEpisodesButton->setText("Find New");
        m_findEpisodesButton->setWidth(220);
        m_findEpisodesButton->setHeight(40);
        m_findEpisodesButton->setCornerRadius(20);
        m_findEpisodesButton->setMarginBottom(8);
        m_findEpisodesButton->registerClickAction([this](brls::View* view) {
            findNewEpisodes();
            return true;
        });
        leftPanel->addView(m_findEpisodesButton);

        m_deleteButton = new brls::Button();
        m_deleteButton->setText("Remove");
        m_deleteButton->setWidth(220);
        m_deleteButton->setHeight(40);
        m_deleteButton->setCornerRadius(20);
        m_deleteButton->setBackgroundColor(nvgRGBA(180, 60, 60, 255));
        m_deleteButton->setVisibility(brls::Visibility::GONE);
        m_deleteButton->registerClickAction([this](brls::View* view) {
            deleteAllDownloadedEpisodes();
            return true;
        });
        leftPanel->addView(m_deleteButton);
    }

    this->addView(leftPanel);

    // ===== RIGHT PANEL: Info + Chapter/Episode List =====
    auto* rightPanel = new brls::Box();
    rightPanel->setAxis(brls::Axis::COLUMN);
    rightPanel->setGrow(1.0f);
    rightPanel->setPadding(20);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(m_item.title);
    m_titleLabel->setFontSize(24);
    m_titleLabel->setMarginBottom(6);
    rightPanel->addView(m_titleLabel);

    // Author
    if (!m_item.authorName.empty()) {
        auto* authorLabel = new brls::Label();
        authorLabel->setText("By: " + m_item.authorName);
        authorLabel->setFontSize(16);
        authorLabel->setTextColor(nvgRGB(180, 180, 180));
        authorLabel->setMarginBottom(6);
        rightPanel->addView(authorLabel);
    }

    // Metadata row (year + duration)
    auto* metaBox = new brls::Box();
    metaBox->setAxis(brls::Axis::ROW);
    metaBox->setMarginBottom(10);

    if (!m_item.publishedYear.empty()) {
        m_yearLabel = new brls::Label();
        m_yearLabel->setText(m_item.publishedYear);
        m_yearLabel->setFontSize(14);
        m_yearLabel->setTextColor(nvgRGB(150, 150, 150));
        m_yearLabel->setMarginRight(15);
        metaBox->addView(m_yearLabel);
    }

    if (m_item.duration > 0) {
        m_durationLabel = new brls::Label();
        int hours = (int)(m_item.duration / 3600.0f);
        int mins = (int)((m_item.duration - hours * 3600) / 60.0f);
        if (hours > 0) {
            m_durationLabel->setText(std::to_string(hours) + "h " + std::to_string(mins) + "m");
        } else {
            m_durationLabel->setText(std::to_string(mins) + " min");
        }
        m_durationLabel->setFontSize(14);
        m_durationLabel->setTextColor(nvgRGB(150, 150, 150));
        metaBox->addView(m_durationLabel);
    }

    rightPanel->addView(metaBox);

    // Genre tags (blue text labels in a horizontal row, max 6)
    // Always create the box so it can be populated when full details load
    m_genreBox = new brls::Box();
    m_genreBox->setAxis(brls::Axis::ROW);
    m_genreBox->setMarginBottom(10);
    if (!m_item.genres.empty()) {
        for (size_t i = 0; i < m_item.genres.size() && i < 6; i++) {
            auto* genreLabel = new brls::Label();
            genreLabel->setText(m_item.genres[i]);
            genreLabel->setFontSize(11);
            genreLabel->setTextColor(nvgRGB(74, 159, 255));
            genreLabel->setMarginRight(12);
            m_genreBox->addView(genreLabel);
        }
    } else {
        m_genreBox->setVisibility(brls::Visibility::GONE);
    }
    rightPanel->addView(m_genreBox);

    // Summary/Description (truncated by default, L button expands)
    if (!m_item.description.empty()) {
        m_fullDescription = m_item.description;
        m_summaryLabel = new brls::Label();
        if (m_fullDescription.length() > 120) {
            m_summaryLabel->setText(m_fullDescription.substr(0, 117) + "...");
        } else {
            m_summaryLabel->setText(m_fullDescription);
        }
        m_summaryLabel->setFontSize(14);
        m_summaryLabel->setTextColor(nvgRGB(200, 200, 200));
        m_summaryLabel->setMarginBottom(12);
        rightPanel->addView(m_summaryLabel);
    }

    // Episodes list header + container for podcasts
    if (m_item.mediaType == MediaType::PODCAST) {
        auto* episodesHeader = new brls::Box();
        episodesHeader->setAxis(brls::Axis::ROW);
        episodesHeader->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        episodesHeader->setAlignItems(brls::AlignItems::CENTER);
        episodesHeader->setMarginBottom(8);

        auto* episodesLabel = new brls::Label();
        episodesLabel->setText("Episodes");
        episodesLabel->setFontSize(16);
        episodesHeader->addView(episodesLabel);

        m_episodeCountLabel = new brls::Label();
        m_episodeCountLabel->setFontSize(13);
        m_episodeCountLabel->setTextColor(nvgRGB(150, 150, 150));
        m_episodeCountLabel->setGrow(1.0f);
        m_episodeCountLabel->setMarginLeft(8);
        episodesHeader->addView(m_episodeCountLabel);

        // Action buttons row: Sort (R) | Filter (Y) | Downloads (Start)
        auto* actionsRow = new brls::Box();
        actionsRow->setAxis(brls::Axis::ROW);
        actionsRow->setAlignItems(brls::AlignItems::CENTER);

        // Sort button with R hint
        auto* sortContainer = new brls::Box();
        sortContainer->setAxis(brls::Axis::COLUMN);
        sortContainer->setAlignItems(brls::AlignItems::CENTER);
        sortContainer->setMarginRight(8);

        auto* sortHint = new brls::Image();
        sortHint->setWidth(24);
        sortHint->setHeight(16);
        sortHint->setScalingType(brls::ImageScalingType::FIT);
        sortHint->setImageFromFile(RESOURCE_PREFIX "images/r_button.png");
        sortHint->setMarginBottom(2);
        sortContainer->addView(sortHint);

        auto* sortBtn = new brls::Button();
        sortBtn->setWidth(36);
        sortBtn->setHeight(32);
        sortBtn->setCornerRadius(8);
        sortBtn->setJustifyContent(brls::JustifyContent::CENTER);
        sortBtn->setAlignItems(brls::AlignItems::CENTER);
        m_sortIcon = new brls::Image();
        m_sortIcon->setWidth(20);
        m_sortIcon->setHeight(20);
        m_sortIcon->setScalingType(brls::ImageScalingType::FIT);
        m_sortIcon->setImageFromFile(RESOURCE_PREFIX "icons/sort-9-1.png");
        sortBtn->addView(m_sortIcon);
        sortBtn->registerClickAction([this](brls::View*) {
            m_sortDescending = !m_sortDescending;
            m_sortIcon->setImageFromFile(m_sortDescending
                ? RESOURCE_PREFIX "icons/sort-9-1.png"
                : RESOURCE_PREFIX "icons/sort-1-9.png");
            applyFilters();
            return true;
        });
        sortContainer->addView(sortBtn);
        actionsRow->addView(sortContainer);

        // Filter button with Y (triangle) hint
        auto* filterContainer = new brls::Box();
        filterContainer->setAxis(brls::Axis::COLUMN);
        filterContainer->setAlignItems(brls::AlignItems::CENTER);
        filterContainer->setMarginRight(8);

        auto* filterHint = new brls::Image();
        filterHint->setWidth(16);
        filterHint->setHeight(16);
        filterHint->setScalingType(brls::ImageScalingType::FIT);
        filterHint->setImageFromFile(RESOURCE_PREFIX "images/triangle_button.png");
        filterHint->setMarginBottom(2);
        filterContainer->addView(filterHint);

        auto* filterBtn = new brls::Button();
        filterBtn->setWidth(36);
        filterBtn->setHeight(32);
        filterBtn->setCornerRadius(8);
        filterBtn->setJustifyContent(brls::JustifyContent::CENTER);
        filterBtn->setAlignItems(brls::AlignItems::CENTER);
        auto* filterIcon = new brls::Image();
        filterIcon->setWidth(20);
        filterIcon->setHeight(20);
        filterIcon->setScalingType(brls::ImageScalingType::FIT);
        filterIcon->setImageFromFile(RESOURCE_PREFIX "icons/filter-menu-outline.png");
        filterBtn->addView(filterIcon);
        filterBtn->registerClickAction([this](brls::View*) {
            showFilterMenu();
            return true;
        });
        filterContainer->addView(filterBtn);
        actionsRow->addView(filterContainer);

        // Downloads menu button with Start hint
        auto* dlMenuContainer = new brls::Box();
        dlMenuContainer->setAxis(brls::Axis::COLUMN);
        dlMenuContainer->setAlignItems(brls::AlignItems::CENTER);

        auto* dlMenuHint = new brls::Image();
        dlMenuHint->setWidth(64);
        dlMenuHint->setHeight(16);
        dlMenuHint->setScalingType(brls::ImageScalingType::FIT);
        dlMenuHint->setImageFromFile(RESOURCE_PREFIX "images/start_button.png");
        dlMenuHint->setMarginBottom(2);
        dlMenuContainer->addView(dlMenuHint);

        auto* dlMenuBtn = new brls::Button();
        dlMenuBtn->setWidth(36);
        dlMenuBtn->setHeight(32);
        dlMenuBtn->setCornerRadius(8);
        dlMenuBtn->setJustifyContent(brls::JustifyContent::CENTER);
        dlMenuBtn->setAlignItems(brls::AlignItems::CENTER);
        auto* dlMenuIcon = new brls::Image();
        dlMenuIcon->setWidth(20);
        dlMenuIcon->setHeight(20);
        dlMenuIcon->setScalingType(brls::ImageScalingType::FIT);
        dlMenuIcon->setImageFromFile(RESOURCE_PREFIX "icons/menu.png");
        dlMenuBtn->addView(dlMenuIcon);
        dlMenuBtn->registerClickAction([this](brls::View*) {
            showDownloadOptions();
            return true;
        });
        dlMenuContainer->addView(dlMenuBtn);
        actionsRow->addView(dlMenuContainer);

        episodesHeader->addView(actionsRow);

        rightPanel->addView(episodesHeader);

        // Scrollable vertical episodes list
        m_scrollView = new brls::ScrollingFrame();
        m_scrollView->setGrow(1.0f);

        m_childrenBox = new brls::Box();
        m_childrenBox->setAxis(brls::Axis::COLUMN);

        m_scrollView->setContentView(m_childrenBox);
        rightPanel->addView(m_scrollView);
    }

    // Chapters list header + container for books
    if (m_item.mediaType == MediaType::BOOK) {
        auto* chaptersHeader = new brls::Box();
        chaptersHeader->setAxis(brls::Axis::ROW);
        chaptersHeader->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        chaptersHeader->setAlignItems(brls::AlignItems::CENTER);
        chaptersHeader->setMarginBottom(8);

        auto* chaptersLabel = new brls::Label();
        chaptersLabel->setText("Chapters");
        chaptersLabel->setFontSize(16);
        chaptersHeader->addView(chaptersLabel);

        rightPanel->addView(chaptersHeader);

        // Scrollable vertical chapters list
        m_scrollView = new brls::ScrollingFrame();
        m_scrollView->setGrow(1.0f);

        m_chaptersBox = new brls::Box();
        m_chaptersBox->setAxis(brls::Axis::COLUMN);

        m_scrollView->setContentView(m_chaptersBox);
        rightPanel->addView(m_scrollView);
    }

    // For other types that have no list, add a scroll view for the info
    if (m_item.mediaType != MediaType::PODCAST && m_item.mediaType != MediaType::BOOK) {
        m_scrollView = new brls::ScrollingFrame();
        m_scrollView->setGrow(1.0f);
        // Wrap rightPanel content would need restructuring; for now just ensure scrollView exists
    }

    this->addView(rightPanel);

    // Register L button to toggle description expand/collapse
    this->registerAction("Summary", brls::ControllerButton::BUTTON_LB, [this](brls::View*) {
        if (!m_summaryLabel || m_fullDescription.empty()) return true;
        m_descriptionExpanded = !m_descriptionExpanded;
        if (m_descriptionExpanded) {
            m_summaryLabel->setText(m_fullDescription);
        } else {
            if (m_fullDescription.length() > 120) {
                m_summaryLabel->setText(m_fullDescription.substr(0, 117) + "...");
            } else {
                m_summaryLabel->setText(m_fullDescription);
            }
        }
        return true;
    });

    // Register Select button to resume playback from saved position
    this->registerAction("Resume", brls::ControllerButton::BUTTON_BACK, [this](brls::View*) {
        onPlay(true);
        return true;
    });

    // Register Y (triangle) button for filter menu (podcasts)
    if (m_item.mediaType == MediaType::PODCAST) {
        this->registerAction("Filter", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
            showFilterMenu();
            return true;
        });

        // Register R button to toggle sort order
        this->registerAction("Sort", brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
            m_sortDescending = !m_sortDescending;
            m_sortIcon->setImageFromFile(m_sortDescending
                ? RESOURCE_PREFIX "icons/sort-9-1.png"
                : RESOURCE_PREFIX "icons/sort-1-9.png");
            applyFilters();
            return true;
        });

        // Register Start button to open downloads menu
        this->registerAction("Downloads", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
            showDownloadOptions();
            return true;
        });
    }

    // Load full details
    loadDetails();
}

brls::HScrollingFrame* MediaDetailView::createMediaRow(const std::string& title, brls::Box** contentOut) {
    // Unused - kept for interface compatibility
    (void)title;
    if (contentOut) *contentOut = nullptr;
    return nullptr;
}

brls::View* MediaDetailView::create() {
    return nullptr; // Factory not used
}

void MediaDetailView::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);

    // Re-arm alive flag
    m_alive = std::make_shared<std::atomic<bool>>(true);
    m_lastProgressUpdate = std::chrono::steady_clock::now();

    // Register download progress callback for live UI updates
    DownloadsManager& mgr = DownloadsManager::getInstance();
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    mgr.setProgressCallback([this, aliveWeak](float downloadedBytes, float totalBytes) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProgressUpdate).count();

        // Throttle to every 200ms for smooth but not excessive updates
        if (elapsed < 200 && downloadedBytes < totalBytes) return;
        m_lastProgressUpdate = now;

        brls::sync([this, aliveWeak, downloadedBytes, totalBytes]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            // Update download button text with live MB progress
            if (m_downloadButton) {
                double dlMB = downloadedBytes / (1024.0 * 1024.0);
                double totMB = totalBytes / (1024.0 * 1024.0);
                int dlInt = static_cast<int>(dlMB * 10);
                int totInt = static_cast<int>(totMB * 10);
                std::string text = std::to_string(dlInt / 10) + "." + std::to_string(dlInt % 10) + " / "
                                 + std::to_string(totInt / 10) + "." + std::to_string(totInt % 10) + " MB";
                m_downloadButton->setText(text);
            }
        });
    });

    // Register item completion callback for UI refresh
    mgr.setItemCompletionCallback([this, aliveWeak](const std::string& itemId, const std::string& episodeId, bool success) {
        brls::sync([this, aliveWeak, itemId, episodeId, success]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (success) {
                // Update download button for audiobooks
                if (m_downloadButton && itemId == m_item.id && episodeId.empty()) {
                    m_downloadButton->setText("Downloaded");
                    m_downloadButton->setBackgroundColor(nvgRGBA(46, 204, 113, 200));
                }

                // Refresh episode list so download icons update (green checkmark)
                if (m_item.mediaType == MediaType::PODCAST && m_childrenBox) {
                    applyFilters();
                }

                // Update podcast download/delete button visibility
                if (m_item.mediaType == MediaType::PODCAST) {
                    bool allDownloaded = areAllEpisodesDownloaded();
                    bool anyDownloaded = hasAnyDownloadedEpisodes();
                    if (m_downloadButton) {
                        m_downloadButton->setVisibility(allDownloaded ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
                    }
                    if (m_deleteButton) {
                        m_deleteButton->setVisibility(anyDownloaded ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
                    }
                }
            } else {
                // Download failed - reset button
                if (m_downloadButton) {
                    m_downloadButton->setText("Download");
                }
            }
        });
    });
}

void MediaDetailView::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);

    if (m_alive) *m_alive = false;

    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.setProgressCallback(nullptr);
    mgr.setItemCompletionCallback(nullptr);
}

void MediaDetailView::loadDetails() {
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Load full details
    MediaItem fullItem;
    bool loadedFromServer = client.fetchItem(m_item.id, fullItem);

    if (loadedFromServer) {
        m_item = fullItem;

        // Update UI with full details
        if (m_titleLabel && !m_item.title.empty()) {
            m_titleLabel->setText(m_item.title);
        }

        if (m_summaryLabel && !m_item.description.empty()) {
            m_fullDescription = m_item.description;
            m_descriptionExpanded = false;
            if (m_fullDescription.length() > 120) {
                m_summaryLabel->setText(m_fullDescription.substr(0, 117) + "...");
            } else {
                m_summaryLabel->setText(m_fullDescription);
            }
        }

        // Update genre tags if now available from full item details
        if (m_genreBox && !m_item.genres.empty()) {
            m_genreBox->clearViews();
            for (size_t i = 0; i < m_item.genres.size() && i < 6; i++) {
                auto* genreLabel = new brls::Label();
                genreLabel->setText(m_item.genres[i]);
                genreLabel->setFontSize(11);
                genreLabel->setTextColor(nvgRGB(74, 159, 255));
                genreLabel->setMarginRight(12);
                m_genreBox->addView(genreLabel);
            }
            m_genreBox->setVisibility(brls::Visibility::VISIBLE);
        }

        // Update play button text dynamically
        if (m_playButton) {
            if (m_item.mediaType == MediaType::BOOK) {
                if (m_item.isFinished) {
                    m_playButton->setText("Play Again");
                } else if (m_item.currentTime > 0 && !m_item.chapters.empty()) {
                    // Find current chapter
                    int chapterNum = 1;
                    for (size_t i = 0; i < m_item.chapters.size(); i++) {
                        if (m_item.currentTime >= m_item.chapters[i].start &&
                            m_item.currentTime < m_item.chapters[i].end) {
                            chapterNum = static_cast<int>(i + 1);
                            break;
                        }
                    }
                    m_playButton->setText("Play Ch. " + std::to_string(chapterNum));
                } else if (m_item.currentTime > 0) {
                    m_playButton->setText("Resume");
                }
            }
        }

        // Update download button state
        if (m_downloadButton && !m_item.audioTracks.empty()) {
            if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
                m_downloadButton->setText("Downloaded");
            } else {
                m_downloadButton->setText("Download");
            }
        }

        // Chapters box is created in constructor for BOOK type.
        // If mediaType wasn't known at construction time, chapters will be
        // populated directly if the box exists.
    } else {
        // Server fetch failed - try to load metadata from DownloadsManager (offline mode)
        brls::Logger::info("MediaDetailView: Server fetch failed, loading metadata from downloads");
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        auto allDownloads = downloadsMgr.getDownloads();

        // Find matching download (for audiobooks, match itemId; for podcast episodes, we check later)
        for (const auto& dl : allDownloads) {
            if (dl.itemId == m_item.id && dl.state == DownloadState::COMPLETED) {
                // For podcasts, we want the first episode's parent info or any episode
                // For audiobooks, this matches directly
                brls::Logger::info("MediaDetailView: Found offline metadata for {}", dl.title);

                // Update title if not already set from m_item
                if (m_titleLabel && !dl.title.empty() && m_item.title.empty()) {
                    m_titleLabel->setText(dl.title);
                    m_item.title = dl.title;
                }

                // Update author
                if (!dl.authorName.empty() && m_item.authorName.empty()) {
                    m_item.authorName = dl.authorName;
                }

                // Update description
                if (m_summaryLabel && !dl.description.empty()) {
                    m_fullDescription = dl.description;
                    m_descriptionExpanded = false;
                    if (m_fullDescription.length() > 120) {
                        m_summaryLabel->setText(m_fullDescription.substr(0, 117) + "...");
                    } else {
                        m_summaryLabel->setText(dl.description);
                    }
                    m_item.description = dl.description;
                }

                // Load chapters for audiobooks
                if (!dl.chapters.empty() && m_item.chapters.empty()) {
                    for (const auto& ch : dl.chapters) {
                        Chapter ci;
                        ci.title = ch.title;
                        ci.start = ch.start;
                        ci.end = ch.end;
                        m_item.chapters.push_back(ci);
                    }
                    brls::Logger::info("MediaDetailView: Loaded {} chapters from offline data", m_item.chapters.size());
                }

                // For audiobooks, we only need one match
                if (dl.episodeId.empty()) {
                    break;
                }
            }
        }

        // Chapters box is created in constructor for BOOK type
    }

    // Load thumbnail - try local cover first if offline or for downloaded content
    if (m_posterImage && !m_item.id.empty()) {
        // Check if we have a local cover from downloads
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        std::string localCoverPath = downloadsMgr.getLocalCoverPath(m_item.id);

        if (!localCoverPath.empty()) {
            // Load local cover
            brls::Logger::info("MediaDetailView: Loading local cover: {}", localCoverPath);
            loadLocalCover(localCoverPath);
        } else if (loadedFromServer) {
            // Fetch from server
            std::string url = client.getCoverUrl(m_item.id, 400, 400);
            ImageLoader::loadAsync(url, [](brls::Image* image) {}, m_posterImage, m_alive);
        }
        // If offline and no local cover, leave poster empty
    }

    // Load children (podcast episodes)
    if (m_item.mediaType == MediaType::PODCAST) {
        loadChildren();
    }

    // Populate chapters for audiobooks
    if (m_item.mediaType == MediaType::BOOK) {
        brls::Logger::debug("Populating chapters for book: {} chapters available", m_item.chapters.size());
        populateChapters();
    }
}

void MediaDetailView::loadChildren() {
    if (!m_childrenBox) return;

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    bool loadedFromServer = client.fetchPodcastEpisodes(m_item.id, m_children);

    // If server fetch failed, try to load downloaded episodes from DownloadsManager
    if (!loadedFromServer) {
        brls::Logger::info("MediaDetailView: Server fetch failed, loading downloaded episodes");
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        auto allDownloads = downloadsMgr.getDownloads();

        m_children.clear();
        for (const auto& dl : allDownloads) {
            if (dl.itemId == m_item.id && dl.state == DownloadState::COMPLETED && !dl.episodeId.empty()) {
                // Create a MediaItem from the download info
                MediaItem episode;
                episode.id = dl.episodeId;
                episode.podcastId = dl.itemId;
                episode.episodeId = dl.episodeId;
                episode.title = dl.title;
                episode.authorName = dl.authorName;
                episode.duration = dl.duration;
                episode.mediaType = MediaType::PODCAST_EPISODE;
                episode.isDownloaded = true;
                // Use local cover path if available
                if (!dl.localCoverPath.empty()) {
                    episode.coverPath = dl.localCoverPath;
                } else {
                    episode.coverPath = dl.coverUrl;
                }
                m_children.push_back(episode);
                brls::Logger::debug("MediaDetailView: Added downloaded episode: {}", dl.title);
            }
        }

        if (!m_children.empty()) {
            brls::Logger::info("MediaDetailView: Found {} downloaded episodes for podcast", m_children.size());
        }
    }

    if (!m_children.empty()) {
        // Build episode rows via applyFilters (handles sort + filter + row creation)
        applyFilters();

        // Update play button text to show next unheard episode
        if (m_playButton && m_item.mediaType == MediaType::PODCAST) {
            for (size_t i = 0; i < m_children.size(); i++) {
                if (m_children[i].progress < 1.0f && !m_children[i].isFinished) {
                    m_playButton->setText("Play Ep. " + std::to_string(i + 1));
                    break;
                }
            }
        }

        // Update podcast download/delete button visibility now that we know the episodes
        if (m_item.mediaType == MediaType::PODCAST) {
            bool allDownloaded = areAllEpisodesDownloaded();
            bool anyDownloaded = hasAnyDownloadedEpisodes();

            if (m_downloadButton) {
                m_downloadButton->setVisibility(allDownloaded ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
            }
            if (m_deleteButton) {
                m_deleteButton->setVisibility(anyDownloaded ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            }
        }
    }
}

void MediaDetailView::loadMusicCategories() {
    // Not used for Audiobookshelf
}

void MediaDetailView::loadLocalCover(const std::string& localPath) {
    if (localPath.empty() || !m_posterImage) return;

#ifdef __vita__
    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);

        if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB
            std::vector<uint8_t> data(size);
            if (sceIoRead(fd, data.data(), size) == size) {
                m_posterImage->setImageFromMem(data.data(), data.size());
                brls::Logger::debug("MediaDetailView: Local cover loaded ({} bytes)", size);
            }
        }
        sceIoClose(fd);
    } else {
        brls::Logger::warning("MediaDetailView: Failed to open local cover: {}", localPath);
    }
#else
    // Non-Vita: use standard file I/O
    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                m_posterImage->setImageFromMem(data.data(), data.size());
                brls::Logger::debug("MediaDetailView: Local cover loaded ({} bytes)", size);
            }
        }
        file.close();
    }
#endif
}

void MediaDetailView::populateChapters() {
    if (!m_chaptersBox) return;

    m_chaptersBox->clearViews();

    // Check if we have chapters
    if (m_item.chapters.empty()) {
        // Show message if no chapters
        auto* noChaptersLabel = new brls::Label();
        noChaptersLabel->setText("No chapter information available");
        noChaptersLabel->setFontSize(14);
        noChaptersLabel->setTextColor(nvgRGB(150, 150, 150));
        noChaptersLabel->setMarginTop(10);
        m_chaptersBox->addView(noChaptersLabel);
        return;
    }

    // Helper to format time
    auto formatTime = [](float seconds) -> std::string {
        int totalSec = static_cast<int>(seconds);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;

        char buf[32];
        if (hours > 0) {
            snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, mins, secs);
        } else {
            snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
        }
        return std::string(buf);
    };

    // Current playback position for highlighting
    float currentTime = m_item.currentTime;

    for (size_t i = 0; i < m_item.chapters.size(); ++i) {
        const auto& chapter = m_item.chapters[i];

        auto* chapterRow = new FocusHintBox();
        chapterRow->setAxis(brls::Axis::ROW);
        chapterRow->setAlignItems(brls::AlignItems::CENTER);
        chapterRow->setHeight(56);
        chapterRow->setMarginBottom(4);
        chapterRow->setPadding(10, 14, 10, 14);
        chapterRow->setCornerRadius(8);
        chapterRow->setFocusable(true);

        // Square button hint (visible on focus only)
        auto* squareHint = new brls::Image();
        squareHint->setSize(brls::Size(16, 16));
        squareHint->setScalingType(brls::ImageScalingType::FIT);
        squareHint->setImageFromFile(RESOURCE_PREFIX "images/square_button.png");
        squareHint->setMarginRight(8);
        squareHint->setVisibility(brls::Visibility::GONE);
        chapterRow->addView(squareHint);
        chapterRow->setHintImage(squareHint);

        // Highlight current chapter
        bool isCurrentChapter = (currentTime >= chapter.start && currentTime < chapter.end);
        if (isCurrentChapter) {
            chapterRow->setBackgroundColor(nvgRGBA(0, 128, 128, 200));
        } else {
            chapterRow->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
        }

        // Left side: number + title + time (grows)
        auto* textBox = new brls::Box();
        textBox->setAxis(brls::Axis::COLUMN);
        textBox->setGrow(1.0f);

        // Chapter title
        auto* titleLabel = new brls::Label();
        std::string title = chapter.title;
        if (title.empty()) {
            title = "Chapter " + std::to_string(i + 1);
        }
        if (title.length() > 45) {
            title = title.substr(0, 42) + "...";
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(14);
        if (isCurrentChapter) {
            titleLabel->setTextColor(nvgRGB(255, 255, 255));
        }
        textBox->addView(titleLabel);

        // Time info
        auto* timeLabel = new brls::Label();
        float duration = chapter.end - chapter.start;
        timeLabel->setText(formatTime(chapter.start) + " - " + formatTime(duration));
        timeLabel->setFontSize(11);
        timeLabel->setTextColor(nvgRGB(150, 150, 150));
        textBox->addView(timeLabel);

        chapterRow->addView(textBox);

        // Click to play from chapter
        std::string itemId = m_item.id;
        float chapterStart = chapter.start;
        chapterRow->registerClickAction([this, itemId, chapterStart](brls::View*) {
            startDownloadAndPlay(itemId, "", chapterStart);
            return true;
        });

        m_chaptersBox->addView(chapterRow);
    }
}

void MediaDetailView::showFilterMenu() {
    // Build filter options for podcast episodes
    std::vector<std::string> options;
    options.push_back(m_filterDownloaded ? "Downloaded  [ON]" : "Downloaded");
    options.push_back(m_filterUnheard ? "Unheard  [ON]" : "Unheard");
    options.push_back("Clear Filters");

    auto* dialog = new brls::Dialog("Episode Filters");
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(20);
    content->setWidth(500);

    for (size_t i = 0; i < options.size(); ++i) {
        auto* optionBox = new brls::Box();
        optionBox->setAxis(brls::Axis::ROW);
        optionBox->setAlignItems(brls::AlignItems::CENTER);
        optionBox->setJustifyContent(brls::JustifyContent::CENTER);
        optionBox->setHeight(50);
        optionBox->setMarginBottom(8);
        optionBox->setPadding(12);
        optionBox->setCornerRadius(8);
        optionBox->setFocusable(true);

        // Color active filters green
        if ((i == 0 && m_filterDownloaded) || (i == 1 && m_filterUnheard)) {
            optionBox->setBackgroundColor(nvgRGBA(0, 128, 128, 200));
        } else if (i == 2) {
            optionBox->setBackgroundColor(nvgRGBA(80, 60, 60, 255));
        } else {
            optionBox->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
        }

        auto* label = new brls::Label();
        label->setText(options[i]);
        label->setFontSize(16);
        optionBox->addView(label);

        size_t idx = i;
        optionBox->registerClickAction([this, idx, dialog](brls::View*) {
            switch (idx) {
                case 0:
                    m_filterDownloaded = !m_filterDownloaded;
                    brls::Application::notify(m_filterDownloaded ? "Filter: Downloaded" : "Filter removed");
                    break;
                case 1:
                    m_filterUnheard = !m_filterUnheard;
                    brls::Application::notify(m_filterUnheard ? "Filter: Unheard" : "Filter removed");
                    break;
                case 2:
                    m_filterDownloaded = false;
                    m_filterUnheard = false;
                    brls::Application::notify("Filters cleared");
                    break;
            }
            dialog->dismiss();
            applyFilters();
            return true;
        });

        content->addView(optionBox);
    }

    dialog->addView(content);
    dialog->open();
}

void MediaDetailView::applyFilters() {
    if (!m_childrenBox) return;

    m_childrenBox->clearViews();

    // Build filtered + sorted list
    std::vector<MediaItem> filtered;
    DownloadsManager& dlMgr = DownloadsManager::getInstance();

    for (const auto& child : m_children) {
        std::string epItemId = child.podcastId.empty() ? m_item.id : child.podcastId;
        bool isEpDownloaded = dlMgr.isDownloaded(epItemId, child.episodeId);

        if (m_filterDownloaded && !isEpDownloaded) continue;
        if (m_filterUnheard && child.progress >= 1.0f) continue;

        filtered.push_back(child);
    }

    // Sort
    if (!m_sortDescending) {
        std::reverse(filtered.begin(), filtered.end());
    }

    // Update count label
    if (m_episodeCountLabel) {
        m_episodeCountLabel->setText("(" + std::to_string(filtered.size()) + "/" +
                                     std::to_string(m_children.size()) + ")");
    }

    // Helper to format duration
    auto formatDuration = [](float seconds) -> std::string {
        int totalSec = static_cast<int>(seconds);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        if (hours > 0) {
            return std::to_string(hours) + "h " + std::to_string(mins) + "m";
        }
        return std::to_string(mins) + " min";
    };

    for (size_t i = 0; i < filtered.size(); ++i) {
        const auto& child = filtered[i];

        auto* episodeRow = new FocusHintBox();
        episodeRow->setAxis(brls::Axis::ROW);
        episodeRow->setAlignItems(brls::AlignItems::CENTER);
        episodeRow->setHeight(56);
        episodeRow->setMarginBottom(4);
        episodeRow->setPadding(10, 14, 10, 14);
        episodeRow->setCornerRadius(8);
        episodeRow->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
        episodeRow->setFocusable(true);

        // Square button hint (visible on focus only)
        auto* squareHint = new brls::Image();
        squareHint->setSize(brls::Size(16, 16));
        squareHint->setScalingType(brls::ImageScalingType::FIT);
        squareHint->setImageFromFile(RESOURCE_PREFIX "images/square_button.png");
        squareHint->setMarginRight(8);
        squareHint->setVisibility(brls::Visibility::GONE);
        episodeRow->addView(squareHint);
        episodeRow->setHintImage(squareHint);

        // Left side: title + duration
        auto* textBox = new brls::Box();
        textBox->setAxis(brls::Axis::COLUMN);
        textBox->setGrow(1.0f);

        auto* titleLabel = new brls::Label();
        std::string title = child.title;
        if (title.length() > 45) {
            title = title.substr(0, 42) + "...";
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(14);
        textBox->addView(titleLabel);

        if (child.duration > 0) {
            auto* durationLabel = new brls::Label();
            durationLabel->setText(formatDuration(child.duration));
            durationLabel->setFontSize(11);
            durationLabel->setTextColor(nvgRGB(150, 150, 150));
            textBox->addView(durationLabel);
        }

        episodeRow->addView(textBox);

        // Right side: download status button
        auto* dlBtn = new brls::Button();
        dlBtn->setWidth(55);
        dlBtn->setHeight(36);
        dlBtn->setCornerRadius(18);
        dlBtn->setJustifyContent(brls::JustifyContent::CENTER);
        dlBtn->setAlignItems(brls::AlignItems::CENTER);

        auto* dlIcon = new brls::Image();
        dlIcon->setWidth(20);
        dlIcon->setHeight(20);
        dlIcon->setScalingType(brls::ImageScalingType::FIT);

        std::string epItemId = child.podcastId.empty() ? m_item.id : child.podcastId;
        std::string epId = child.episodeId;

        // Check download state: completed, queued/downloading, or not queued
        bool isEpDownloaded = dlMgr.isDownloaded(epItemId, epId);
        bool isEpQueued = false;
        bool isEpDownloading = false;
        {
            DownloadItem* dlItem = dlMgr.getDownload(epItemId, epId);
            if (dlItem) {
                isEpQueued = (dlItem->state == DownloadState::QUEUED);
                isEpDownloading = (dlItem->state == DownloadState::DOWNLOADING);
            }
        }

        if (isEpDownloaded) {
            dlIcon->setImageFromFile(RESOURCE_PREFIX "icons/checkbox_checked.png");
            dlBtn->setBackgroundColor(nvgRGBA(46, 204, 113, 200));
        } else if (isEpQueued || isEpDownloading) {
            dlIcon->setImageFromFile(RESOURCE_PREFIX "icons/download.png");
            dlBtn->setBackgroundColor(nvgRGBA(200, 180, 60, 200));  // Yellow for queued/downloading
        } else {
            dlIcon->setImageFromFile(RESOURCE_PREFIX "icons/download.png");
            dlBtn->setBackgroundColor(nvgRGBA(60, 60, 60, 200));
        }

        dlBtn->addView(dlIcon);

        // Click and X button: toggle download queue state (like Suwayomi)
        // Queued/Downloading → cancel, Downloaded → delete, Not queued → queue download
        std::string epTitle = child.title;
        std::string epAuthor = m_item.authorName;
        float epDuration = child.duration;
        std::string epMediaType = (child.mediaType == MediaType::PODCAST_EPISODE) ? "episode" : "book";

        dlBtn->registerClickAction([this, epItemId, epId, epTitle, epAuthor, epDuration, epMediaType, dlBtn](brls::View*) {
            DownloadsManager& dm = DownloadsManager::getInstance();
            dm.init();

            // Query live state at action time (not stale bind-time values)
            DownloadItem* dlItem = dm.getDownload(epItemId, epId);
            bool downloaded = dm.isDownloaded(epItemId, epId);
            bool queued = dlItem && (dlItem->state == DownloadState::QUEUED);
            bool downloading = dlItem && (dlItem->state == DownloadState::DOWNLOADING);

            if (queued || downloading) {
                // Cancel the download
                dm.cancelDownload(epItemId, epId);
                brls::Application::notify("Download cancelled");
                applyFilters();
            } else if (downloaded) {
                // Delete the download
                dm.deleteDownloadByEpisodeId(epItemId, epId);
                brls::Application::notify("Deleted download");
                applyFilters();
            } else {
                // Queue the download (like Suwayomi - no blocking dialog)
                bool queued = dm.queueDownload(epItemId, epTitle, epAuthor, epDuration, epMediaType, "", epId);
                if (queued) {
                    dm.startDownloads();
                    brls::Application::notify("Queued: " + epTitle);
                } else {
                    brls::Application::notify("Already in queue");
                }
                applyFilters();  // Refresh to show queued state
            }
            return true;
        });

        episodeRow->addView(dlBtn);

        // Square button action: toggle download queue state (like Suwayomi X button)
        episodeRow->registerAction("Download", brls::ControllerButton::BUTTON_X,
            [this, epItemId, epId, epTitle, epAuthor, epDuration, epMediaType](brls::View*) {
            DownloadsManager& dm = DownloadsManager::getInstance();
            dm.init();

            // Query live state
            DownloadItem* dlItem = dm.getDownload(epItemId, epId);
            bool downloaded = dm.isDownloaded(epItemId, epId);
            bool isQueued = dlItem && (dlItem->state == DownloadState::QUEUED);
            bool isDownloading = dlItem && (dlItem->state == DownloadState::DOWNLOADING);

            if (isQueued || isDownloading) {
                dm.cancelDownload(epItemId, epId);
                brls::Application::notify("Download cancelled");
            } else if (downloaded) {
                dm.deleteDownloadByEpisodeId(epItemId, epId);
                brls::Application::notify("Deleted download");
            } else {
                bool q = dm.queueDownload(epItemId, epTitle, epAuthor, epDuration, epMediaType, "", epId);
                if (q) {
                    dm.startDownloads();
                    brls::Application::notify("Queued: " + epTitle);
                } else {
                    brls::Application::notify("Already in queue");
                }
            }
            // Defer UI refresh to next frame
            std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                applyFilters();
            });
            return true;
        }, false, false, brls::Sound::SOUND_CLICK);

        episodeRow->registerClickAction([this, child](brls::View*) {
            if (child.mediaType == MediaType::PODCAST_EPISODE) {
                startDownloadAndPlay(child.podcastId, child.episodeId);
            } else {
                auto* detailView = new MediaDetailView(child);
                brls::Application::pushActivity(new brls::Activity(detailView));
            }
            return true;
        });

        m_childrenBox->addView(episodeRow);
    }
}

void MediaDetailView::onPlay(bool resume) {
    // For podcasts, handle episode selection first
    if (m_item.mediaType == MediaType::PODCAST) {
        std::string podcastId, episodeId;
        if (!m_children.empty()) {
            podcastId = m_children[0].podcastId;
            episodeId = m_children[0].episodeId;
        } else {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<MediaItem> episodes;
            if (client.fetchPodcastEpisodes(m_item.id, episodes) && !episodes.empty()) {
                podcastId = episodes[0].podcastId;
                episodeId = episodes[0].episodeId;
            } else {
                brls::Application::notify("No episodes available");
                return;
            }
        }
        // Start download for podcast episode
        startDownloadAndPlay(podcastId, episodeId);
        return;
    }

    // For books and podcast episodes, start download
    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string episodeId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.episodeId : "";

    startDownloadAndPlay(itemId, episodeId);
}

void MediaDetailView::startDownloadAndPlay(const std::string& itemId, const std::string& episodeId,
                                            float requestedStartTime, bool downloadOnly) {
    brls::Logger::info("startDownloadAndPlay: itemId={}, episodeId={}, startTime={}, downloadOnly={}",
                       itemId, episodeId, requestedStartTime, downloadOnly);

    // Initialize downloads manager
    DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
    downloadsMgr.init();

    // Check if item is downloaded - if so, play from local file
    if (!downloadOnly && downloadsMgr.isDownloaded(itemId, episodeId)) {
        std::string cachedPath = downloadsMgr.getPlaybackPath(itemId);
        brls::Logger::info("Found in downloads: {}", cachedPath);

        // Fetch latest progress from server before playing
        float startTime = requestedStartTime;
        if (startTime < 0) {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            if (client.isAuthenticated()) {
                float serverTime = 0.0f, serverProgress = 0.0f;
                bool serverFinished = false;
                if (client.getProgress(itemId, serverTime, serverProgress, serverFinished, episodeId)) {
                    startTime = serverTime;
                    brls::Logger::info("Fetched progress from server: {}s", startTime);
                }
            }

            // Also check local progress
            if (startTime <= 0) {
                DownloadItem* download = downloadsMgr.getDownload(itemId);
                if (download && download->currentTime > 0) {
                    startTime = download->currentTime;
                    brls::Logger::info("Using local download progress: {}s", startTime);
                }
            }
        }

        Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, cachedPath, startTime);
        return;
    }

    // If not download-only, stream directly from server URL
    // mpv handles HTTP streaming natively (matching Vita_plex approach)
    if (!downloadOnly) {
        // If downloadOnPlay setting is enabled, also queue for background download
        if (Application::getInstance().getSettings().downloadOnPlay) {
            DownloadsManager& dm = DownloadsManager::getInstance();
            dm.init();
            if (!dm.isDownloaded(itemId, episodeId)) {
                std::string title = m_item.title;
                std::string author = m_item.authorName;
                float dur = m_item.duration;
                std::string mediaType = m_item.type;
                std::string series = m_item.seriesName;
                if (dm.queueDownload(itemId, title, author, dur, mediaType, series, episodeId)) {
                    dm.startDownloads();
                    brls::Logger::info("downloadOnPlay: Queued {} for background download", title);
                }
            }
        }
        brls::Logger::info("Streaming directly from server");
        Application::getInstance().pushPlayerActivity(itemId, episodeId, requestedStartTime);
        return;
    }

    // Download-only mode: download to downloads folder
    auto* progressDialog = ProgressDialog::showDownloading(m_item.title);

    // Store necessary data for the async operation
    std::string title = m_item.title;
    std::string authorName = m_item.authorName;
    std::string itemType = m_item.type;
    float duration = m_item.duration;
    std::string description = m_item.description;
    std::string coverUrl = AudiobookshelfClient::getInstance().getCoverUrl(itemId);

    // Copy chapters for offline use
    std::vector<DownloadChapter> downloadChapters;
    for (const auto& ch : m_item.chapters) {
        DownloadChapter dch;
        dch.title = ch.title;
        dch.start = ch.start;
        dch.end = ch.end;
        downloadChapters.push_back(dch);
    }

    // Run download in background
    asyncRun([this, progressDialog, itemId, episodeId, title, authorName, itemType, duration, coverUrl, description, downloadChapters]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        // Start playback session
        PlaybackSession session;
        if (!client.startPlaybackSession(itemId, session, episodeId)) {
            brls::Logger::error("Failed to start playback session");
            brls::sync([progressDialog]() {
                progressDialog->setStatus("Failed to start session");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
            return;
        }

        brls::Logger::info("Session started: {} tracks, currentTime={}s, duration={}s",
                          session.audioTracks.size(), session.currentTime, session.duration);

        // Determine file extension and multi-file status
        bool isMultiFile = session.audioTracks.size() > 1;
        std::string mimeType = "audio/mpeg";
        if (!session.audioTracks.empty() && !session.audioTracks[0].mimeType.empty()) {
            mimeType = session.audioTracks[0].mimeType;
        }

        std::string ext = ".mp3";
        if (mimeType.find("mp4") != std::string::npos || mimeType.find("m4a") != std::string::npos ||
            mimeType.find("m4b") != std::string::npos) {
            ext = ".m4a";
        } else if (mimeType.find("flac") != std::string::npos) {
            ext = ".flac";
        } else if (mimeType.find("ogg") != std::string::npos) {
            ext = ".ogg";
        }

        // For multi-file audiobooks, use mp4 container only for m4a sources
        std::string finalExt = ext;
        if (isMultiFile && ext == ".m4a") {
            finalExt = ".m4b";
        }

        // Determine destination path (always downloads folder)
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        std::string destPath;
        std::string filename = itemId;
        if (!episodeId.empty()) {
            filename += "_" + episodeId;
        }
        filename += finalExt;
        destPath = downloadsMgr.getDownloadsPath() + "/" + filename;

#ifdef __vita__
        HttpClient httpClient;
        httpClient.setTimeout(300);  // 5 minute timeout
        int64_t totalDownloaded = 0;
        bool downloadSuccess = true;

        if (isMultiFile) {
            // Multi-file audiobook handling
            int numTracks = static_cast<int>(session.audioTracks.size());
            std::vector<std::string> trackFiles;

            // Check if combined file already exists on disk
            std::string combinedPath = downloadsMgr.getDownloadsPath() + "/" + itemId + finalExt;

            SceIoStat existingStat;
            if (sceIoGetstat(combinedPath.c_str(), &existingStat) >= 0 && existingStat.st_size > 0) {
                brls::Logger::info("Found existing combined file: {} ({} bytes)", combinedPath, existingStat.st_size);

                downloadsMgr.registerCompletedDownload(itemId, episodeId, title, authorName,
                    combinedPath, existingStat.st_size, duration, itemType, coverUrl, description, downloadChapters);

                brls::sync([progressDialog, this]() {
                    progressDialog->setStatus("Download complete!");
                    if (m_downloadButton) m_downloadButton->setText("Downloaded");
                    brls::delay(1500, [progressDialog]() { progressDialog->dismiss(); });
                });
                return;
            }

            // Download all tracks sequentially
            brls::Logger::info("Download-only mode: Downloading all {} tracks for multi-file audiobook", numTracks);

            for (int trackIdx = 0; trackIdx < numTracks && downloadSuccess; trackIdx++) {
                const AudioTrack& track = session.audioTracks[trackIdx];
                std::string trackUrl = client.getStreamUrl(track.contentUrl, "");

                if (trackUrl.empty()) {
                    brls::Logger::error("Failed to get URL for track {}", trackIdx);
                    downloadSuccess = false;
                    break;
                }

                std::string trackExt = ext;
                if (!track.mimeType.empty() && (track.mimeType.find("mp4") != std::string::npos ||
                    track.mimeType.find("m4a") != std::string::npos)) {
                    trackExt = ".m4a";
                }

                // Use downloads temp path for track files
                std::string trackPath = downloadsMgr.getDownloadsPath() + "/" + itemId + "_track" + std::to_string(trackIdx) + trackExt;
                trackFiles.push_back(trackPath);

                int currentTrack = trackIdx;
                brls::sync([progressDialog, currentTrack, numTracks]() {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Downloading track %d/%d...", currentTrack + 1, numTracks);
                    progressDialog->setStatus(buf);
                });

                SceUID fd = sceIoOpen(trackPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                if (fd < 0) {
                    brls::Logger::error("Failed to create track file: {}", trackPath);
                    downloadSuccess = false;
                    break;
                }

                int64_t trackDownloaded = 0;
                int64_t trackSize = 0;

                bool trackOk = httpClient.downloadFile(trackUrl,
                    [&](const char* data, size_t size) -> bool {
                        int written = sceIoWrite(fd, data, size);
                        if (written < 0) return false;
                        trackDownloaded += size;

                        if (trackSize > 0) {
                            int64_t currentTrackNum = currentTrack;
                            brls::sync([progressDialog, trackDownloaded, trackSize, currentTrackNum, numTracks]() {
                                char buf[96];
                                int percent = static_cast<int>((trackDownloaded * 100) / trackSize);
                                int dlMB = static_cast<int>(trackDownloaded / (1024 * 1024));
                                int totalMB = static_cast<int>(trackSize / (1024 * 1024));
                                snprintf(buf, sizeof(buf), "Track %d/%d: %d%% (%d/%d MB)",
                                        static_cast<int>(currentTrackNum) + 1, numTracks, percent, dlMB, totalMB);
                                progressDialog->setStatus(buf);
                            });
                        }
                        return true;
                    },
                    [&](int64_t size) {
                        if (size > 0) trackSize = size;
                    }
                );

                sceIoClose(fd);

                if (!trackOk) {
                    brls::Logger::error("Failed to download track {}", trackIdx);
                    sceIoRemove(trackPath.c_str());
                    downloadSuccess = false;
                } else {
                    totalDownloaded += trackDownloaded;
                    brls::Logger::info("Track {}/{} complete ({} bytes)", trackIdx + 1, numTracks, trackDownloaded);
                }
            }

            // Combine all tracks if all downloaded successfully
            if (downloadSuccess) {
                brls::sync([progressDialog]() {
                    progressDialog->setStatus("Combining audio files...");
                });

                brls::Logger::info("Combining {} tracks into {}", numTracks, destPath);

                int totalTracks = numTracks;
                if (concatenateAudioFiles(trackFiles, destPath, [progressDialog, totalTracks](int current, int total) {
                    brls::sync([progressDialog, current, totalTracks]() {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Combining file %d/%d...", current, totalTracks);
                        progressDialog->setStatus(buf);
                    });
                })) {
                    brls::Logger::info("Successfully combined {} tracks", numTracks);

                    SceIoStat stat;
                    if (sceIoGetstat(destPath.c_str(), &stat) >= 0) {
                        totalDownloaded = stat.st_size;
                    }

                    for (const auto& trackFile : trackFiles) {
                        sceIoRemove(trackFile.c_str());
                    }
                } else {
                    brls::Logger::error("Failed to combine tracks");
                    downloadSuccess = false;
                    for (const auto& trackFile : trackFiles) {
                        sceIoRemove(trackFile.c_str());
                    }
                }
            } else {
                for (const auto& trackFile : trackFiles) {
                    if (!trackFile.empty()) {
                        sceIoRemove(trackFile.c_str());
                    }
                }
            }

        } else {
            // Single file download
            std::string streamUrl;
            if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
                streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
            } else {
                streamUrl = client.getDirectStreamUrl(itemId, 0);
            }

            if (streamUrl.empty()) {
                brls::Logger::error("Failed to get stream URL");
                downloadSuccess = false;
            } else {
                SceUID fd = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                if (fd < 0) {
                    brls::Logger::error("Failed to create file: {}", destPath);
                    downloadSuccess = false;
                } else {
                    int64_t totalSize = 0;

                    downloadSuccess = httpClient.downloadFile(streamUrl,
                        [&](const char* data, size_t size) -> bool {
                            int written = sceIoWrite(fd, data, size);
                            if (written < 0) return false;
                            totalDownloaded += size;

                            if (totalSize > 0) {
                                brls::sync([progressDialog, totalDownloaded, totalSize]() {
                                    progressDialog->updateDownloadProgress(totalDownloaded, totalSize);
                                });
                            }
                            return true;
                        },
                        [&](int64_t size) {
                            totalSize = size;
                            brls::Logger::info("File size: {} bytes", size);
                        }
                    );

                    sceIoClose(fd);

                    if (!downloadSuccess) {
                        sceIoRemove(destPath.c_str());
                    }
                }
            }
        }

        // Handle result
        if (downloadSuccess) {
            downloadsMgr.registerCompletedDownload(itemId, episodeId, title,
                authorName, destPath, totalDownloaded, duration, itemType,
                coverUrl, description, downloadChapters);

            brls::Logger::info("Download complete: {}", destPath);

            brls::sync([progressDialog, this]() {
                progressDialog->setStatus("Download complete!");
                if (m_downloadButton) m_downloadButton->setText("Downloaded");
                brls::delay(1500, [progressDialog]() { progressDialog->dismiss(); });
            });
        } else {
            brls::sync([progressDialog, this]() {
                progressDialog->setStatus("Download failed");
                if (m_downloadButton) m_downloadButton->setText("Download");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
        }
#else
        // Non-Vita: just show success
        brls::sync([progressDialog, this]() {
            progressDialog->setStatus("Download complete!");
            if (m_downloadButton) m_downloadButton->setText("Downloaded");
            brls::delay(1500, [progressDialog]() { progressDialog->dismiss(); });
        });
#endif
    });
}

void MediaDetailView::onDownload() {
    brls::Logger::info("onDownload called for item: {} ({})", m_item.title, m_item.id);

    // Use the same download logic as streaming but save to downloads
    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string episodeId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.episodeId : "";

    // Check if already downloaded (check both itemId and episodeId)
    if (DownloadsManager::getInstance().isDownloaded(itemId, episodeId)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    startDownloadOnly(itemId, episodeId);
}

void MediaDetailView::onDeleteDownload() {
    brls::Logger::info("onDeleteDownload called for item: {} ({})", m_item.title, m_item.id);

    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string title = m_item.title;

    // Show confirmation dialog
    brls::Dialog* dialog = new brls::Dialog("Delete downloaded content?\n\n\"" + title + "\"");

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Delete", [dialog, itemId, this]() {
        DownloadsManager::getInstance().deleteDownload(itemId);
        brls::Application::notify("Download deleted");

        // Update UI - hide delete button
        if (m_deleteButton) {
            m_deleteButton->setVisibility(brls::Visibility::GONE);
        }

        dialog->close();
    });

    dialog->open();
}

bool MediaDetailView::areAllEpisodesDownloaded() {
    if (m_item.mediaType != MediaType::PODCAST) return false;
    if (m_children.empty()) return false;

    DownloadsManager& mgr = DownloadsManager::getInstance();
    for (const auto& ep : m_children) {
        if (!mgr.isDownloaded(m_item.id, ep.episodeId)) {
            return false;
        }
    }
    return true;
}

bool MediaDetailView::hasAnyDownloadedEpisodes() {
    if (m_item.mediaType != MediaType::PODCAST) return false;

    DownloadsManager& mgr = DownloadsManager::getInstance();

    // Check children if available
    if (!m_children.empty()) {
        for (const auto& ep : m_children) {
            if (mgr.isDownloaded(m_item.id, ep.episodeId)) {
                return true;
            }
        }
    }

    // Also check downloads directly for this podcast
    auto downloads = mgr.getDownloads();
    for (const auto& dl : downloads) {
        if (dl.itemId == m_item.id && dl.state == DownloadState::COMPLETED) {
            return true;
        }
    }

    return false;
}

void MediaDetailView::deleteAllDownloadedEpisodes() {
    brls::Logger::info("deleteAllDownloadedEpisodes called for podcast: {} ({})", m_item.title, m_item.id);

    std::string podcastId = m_item.id;
    std::string podcastTitle = m_item.title;

    // Get downloaded episodes for this podcast
    DownloadsManager& mgr = DownloadsManager::getInstance();
    auto downloads = mgr.getDownloads();

    // Collect downloaded episode info: <episodeId, title>
    std::vector<std::pair<std::string, std::string>> downloadedEpisodes;
    for (const auto& dl : downloads) {
        if (dl.itemId == podcastId && dl.state == DownloadState::COMPLETED) {
            downloadedEpisodes.push_back({dl.episodeId, dl.title});
        }
    }

    if (downloadedEpisodes.empty()) {
        brls::Application::notify("No downloaded episodes found");
        return;
    }

    if (downloadedEpisodes.size() == 1) {
        // Single episode - show simple confirmation
        std::string episodeTitle = downloadedEpisodes[0].second;
        std::string episodeId = downloadedEpisodes[0].first;

        brls::Dialog* dialog = new brls::Dialog("Delete downloaded episode?\n\n\"" + episodeTitle + "\"");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Delete", [dialog, podcastId, episodeId, this]() {
            DownloadsManager::getInstance().deleteDownloadByEpisodeId(podcastId, episodeId);
            brls::Application::notify("Episode deleted");

            // Update UI
            if (m_deleteButton) {
                m_deleteButton->setVisibility(brls::Visibility::GONE);
            }
            if (m_downloadButton) {
                m_downloadButton->setVisibility(brls::Visibility::VISIBLE);
            }

            dialog->close();
        });

        dialog->open();
    } else {
        // Multiple episodes - show dialog with Delete All and clickable episode list
        showDeleteEpisodesDialog(downloadedEpisodes, podcastId, podcastTitle);
    }
}

void MediaDetailView::showDeleteEpisodesDialog(
    const std::vector<std::pair<std::string, std::string>>& episodes,
    const std::string& podcastId,
    const std::string& podcastTitle) {

    // Create a dialog with Delete All button and clickable episode list
    auto* dialog = new brls::Dialog("Downloaded Episodes (" + std::to_string(episodes.size()) + ")");

    // Register circle button to close dialog
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    // Create content box
    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(20);
    content->setWidth(700);

    // Delete All button at top with confirmation
    auto* deleteAllBtn = new brls::Button();
    deleteAllBtn->setText("Delete All Downloaded Episodes");
    deleteAllBtn->setMarginBottom(15);

    // Copy episodes for the lambda
    std::vector<std::pair<std::string, std::string>> episodesCopy = episodes;

    deleteAllBtn->registerClickAction([this, dialog, episodesCopy, podcastId, podcastTitle](brls::View*) {
        // Show confirmation dialog
        brls::Dialog* confirmDialog = new brls::Dialog(
            "Delete all " + std::to_string(episodesCopy.size()) + " downloaded episodes from\n\"" + podcastTitle + "\"?");

        confirmDialog->addButton("Cancel", [confirmDialog]() {
            confirmDialog->close();
        });

        confirmDialog->addButton("Delete All", [this, dialog, confirmDialog, episodesCopy, podcastId]() {
            DownloadsManager& mgr = DownloadsManager::getInstance();

            int deleted = 0;
            for (const auto& ep : episodesCopy) {
                if (mgr.deleteDownloadByEpisodeId(podcastId, ep.first)) {
                    deleted++;
                }
            }

            brls::Application::notify("Deleted " + std::to_string(deleted) + " episode(s)");

            // Update UI
            if (m_deleteButton) {
                m_deleteButton->setVisibility(brls::Visibility::GONE);
            }
            if (m_downloadButton) {
                m_downloadButton->setVisibility(brls::Visibility::VISIBLE);
            }

            confirmDialog->close();
            dialog->dismiss();
        });

        confirmDialog->open();
        return true;
    });
    content->addView(deleteAllBtn);

    // Separator
    auto* separator = new brls::Rectangle();
    separator->setHeight(1);
    separator->setColor(nvgRGB(80, 80, 80));
    separator->setMarginBottom(15);
    content->addView(separator);

    // Instructions
    auto* instructionsLabel = new brls::Label();
    instructionsLabel->setText("Click an episode to delete it:");
    instructionsLabel->setFontSize(14);
    instructionsLabel->setTextColor(nvgRGB(180, 180, 180));
    instructionsLabel->setMarginBottom(10);
    content->addView(instructionsLabel);

    // Create scrolling list of episodes - clicking each deletes just that one
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setHeight(300);

    auto* listBox = new brls::Box();
    listBox->setAxis(brls::Axis::COLUMN);

    for (const auto& ep : episodes) {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(12);
        row->setMarginBottom(8);
        row->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
        row->setCornerRadius(6);
        row->setFocusable(true);

        // Episode title
        auto* titleLabel = new brls::Label();
        std::string title = ep.second;
        if (title.length() > 50) {
            title = title.substr(0, 47) + "...";
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(15);
        titleLabel->setGrow(1.0f);
        row->addView(titleLabel);

        // Delete indicator
        auto* deleteLabel = new brls::Label();
        deleteLabel->setText("[X]");
        deleteLabel->setFontSize(14);
        deleteLabel->setTextColor(nvgRGB(200, 100, 100));
        deleteLabel->setWidth(40);
        row->addView(deleteLabel);

        // Click to delete this episode
        std::string episodeId = ep.first;
        std::string episodeTitle = ep.second;
        row->registerClickAction([this, dialog, podcastId, episodeId, episodeTitle, row](brls::View*) {
            // Show confirmation for single episode
            brls::Dialog* confirmDialog = new brls::Dialog("Delete this episode?\n\n\"" + episodeTitle + "\"");

            confirmDialog->addButton("Cancel", [confirmDialog]() {
                confirmDialog->close();
            });

            confirmDialog->addButton("Delete", [this, dialog, confirmDialog, podcastId, episodeId, row]() {
                DownloadsManager& mgr = DownloadsManager::getInstance();
                if (mgr.deleteDownloadByEpisodeId(podcastId, episodeId)) {
                    brls::Application::notify("Episode deleted");

                    // Hide this row
                    row->setVisibility(brls::Visibility::GONE);

                    // Check if any episodes remain
                    if (!hasAnyDownloadedEpisodes()) {
                        if (m_deleteButton) {
                            m_deleteButton->setVisibility(brls::Visibility::GONE);
                        }
                        if (m_downloadButton) {
                            m_downloadButton->setVisibility(brls::Visibility::VISIBLE);
                        }
                        dialog->dismiss();
                    }
                } else {
                    brls::Application::notify("Failed to delete episode");
                }

                confirmDialog->close();
            });

            confirmDialog->open();
            return true;
        });

        listBox->addView(row);
    }

    scrollView->setContentView(listBox);
    content->addView(scrollView);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([dialog](brls::View*) {
        dialog->dismiss();
        return true;
    });
    content->addView(closeBtn);

    dialog->addView(content);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::startDownloadOnly(const std::string& itemId, const std::string& episodeId) {
    brls::Logger::info("startDownloadOnly: itemId={}, episodeId={}", itemId, episodeId);

    DownloadsManager& dm = DownloadsManager::getInstance();
    dm.init();

    // Check if already downloaded
    if (dm.isDownloaded(itemId, episodeId)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    // Check if already in queue
    DownloadItem* existing = dm.getDownload(itemId, episodeId);
    if (existing && (existing->state == DownloadState::QUEUED || existing->state == DownloadState::DOWNLOADING)) {
        brls::Application::notify("Already in download queue");
        return;
    }

    // Queue the download (like Suwayomi - no blocking dialog)
    std::string title = m_item.title;
    std::string author = m_item.authorName;
    float duration = m_item.duration;
    std::string mediaType = m_item.type;
    std::string series = m_item.seriesName;

    bool queued = dm.queueDownload(itemId, title, author, duration, mediaType, series, episodeId);
    if (queued) {
        dm.startDownloads();
        if (m_downloadButton) {
            m_downloadButton->setText("Queued");
            m_downloadButton->setBackgroundColor(nvgRGBA(200, 180, 60, 200));
        }
        brls::Application::notify("Queued: " + title);
    } else {
        brls::Application::notify("Already in queue");
    }
}

void MediaDetailView::batchDownloadEpisodes(const std::vector<MediaItem>& episodes) {
    if (episodes.empty()) {
        brls::Application::notify("No episodes to download");
        return;
    }

    brls::Logger::info("batchDownloadEpisodes: Queueing {} episodes for download", episodes.size());

    // Queue all episodes to the download manager (like Suwayomi - no blocking dialog)
    DownloadsManager& dm = DownloadsManager::getInstance();
    dm.init();

    std::string podcastId = m_item.id;
    std::string podcastAuthor = m_item.authorName.empty() ? m_item.title : m_item.authorName;
    int queued = 0;
    int skipped = 0;

    for (const auto& ep : episodes) {
        std::string episodeId = ep.episodeId;

        // Skip if already downloaded or already in queue
        if (dm.isDownloaded(podcastId, episodeId)) {
            skipped++;
            continue;
        }
        DownloadItem* existing = dm.getDownload(podcastId, episodeId);
        if (existing && (existing->state == DownloadState::QUEUED || existing->state == DownloadState::DOWNLOADING)) {
            skipped++;
            continue;
        }

        if (dm.queueDownload(podcastId, ep.title, podcastAuthor, ep.duration, "episode", "", episodeId)) {
            queued++;
        }
    }

    if (queued > 0) {
        dm.startDownloads();
        brls::Application::notify("Queued " + std::to_string(queued) + " episodes");
    } else {
        brls::Application::notify("All episodes already downloaded or queued");
    }

    // Refresh UI to show queued states
    applyFilters();
}

void MediaDetailView::showDownloadOptions() {
    if (m_item.mediaType != MediaType::PODCAST) {
        return;
    }

    // Count episodes and find undownloaded ones
    DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
    std::vector<MediaItem> undownloadedEpisodes;

    for (const auto& ep : m_children) {
        bool isDownloaded = downloadsMgr.isDownloaded(m_item.id, ep.episodeId);
        if (!isDownloaded) {
            undownloadedEpisodes.push_back(ep);
        }
    }

    // If only 1 episode total OR only 1 undownloaded episode, download directly
    if (m_children.size() == 1 || undownloadedEpisodes.size() == 1) {
        if (undownloadedEpisodes.empty()) {
            brls::Application::notify("All episodes already downloaded");
            return;
        }
        // Download the single undownloaded episode directly
        batchDownloadEpisodes(undownloadedEpisodes);
        return;
    }

    // If no undownloaded episodes, notify and return
    if (undownloadedEpisodes.empty()) {
        brls::Application::notify("All episodes already downloaded");
        return;
    }

    // Count unheard (not finished) episodes
    int unheardCount = 0;
    for (const auto& ep : m_children) {
        bool isDownloaded = downloadsMgr.isDownloaded(m_item.id, ep.episodeId);
        if (ep.progress < 1.0f && !isDownloaded) {
            unheardCount++;
        }
    }

    // Show download options dialog: All, Unwatched, Next 5, Next 10, Next 15
    int episodeCount = static_cast<int>(m_children.size());
    std::string title = "Download Episodes (" + std::to_string(episodeCount) + ")";

    auto* dialog = new brls::Dialog(title);

    // Register circle button to close dialog
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    // Create content box with vertical list of options
    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(20);
    content->setWidth(400);

    // Helper lambda to create an option row
    auto addOption = [&](const std::string& label, const std::string& countText,
                         bool enabled, std::function<void()> action) {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        row->setPadding(12, 16, 12, 16);
        row->setMarginBottom(6);
        row->setCornerRadius(8);
        row->setHeight(44);

        auto* nameLabel = new brls::Label();
        nameLabel->setFontSize(15);
        nameLabel->setGrow(1.0f);

        auto* countLabel = new brls::Label();
        countLabel->setFontSize(13);

        if (enabled) {
            row->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
            row->setFocusable(true);
            nameLabel->setText(label);
            countLabel->setText(countText);
            countLabel->setTextColor(nvgRGB(150, 150, 150));
            row->registerClickAction([dialog, action](brls::View*) {
                dialog->dismiss();
                action();
                return true;
            });
        } else {
            row->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
            row->setFocusable(false);
            nameLabel->setText(label);
            nameLabel->setTextColor(nvgRGB(80, 80, 80));
            countLabel->setText(countText);
            countLabel->setTextColor(nvgRGB(60, 60, 60));
        }

        row->addView(nameLabel);
        row->addView(countLabel);
        content->addView(row);
    };

    // Download All
    addOption("Download All", std::to_string(undownloadedEpisodes.size()) + " episodes",
              true, [this]() { downloadAll(); });

    // Download Unwatched
    addOption("Unwatched", std::to_string(unheardCount) + " episodes",
              unheardCount > 0, [this]() { downloadUnwatched(); });

    // Next 5
    addOption("Next 5", "", unheardCount >= 5,
              [this]() { downloadUnwatched(5); });

    // Next 10
    addOption("Next 10", "", unheardCount >= 10,
              [this]() { downloadUnwatched(10); });

    // Next 15
    addOption("Next 15", "", unheardCount >= 15,
              [this]() { downloadUnwatched(15); });

    dialog->addView(content);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::downloadAll() {
    // Queue all episodes from loaded children (like Suwayomi - no blocking dialog)
    if (m_children.empty()) {
        // If children not loaded yet, fetch them first
        std::string podcastId = m_item.id;
        std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

        asyncRun([this, podcastId, aliveWeak]() {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<MediaItem> episodes;

            if (client.fetchPodcastEpisodes(podcastId, episodes)) {
                brls::sync([this, aliveWeak, episodes]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) return;
                    batchDownloadEpisodes(episodes);
                });
            } else {
                brls::sync([]() {
                    brls::Application::notify("Failed to fetch episodes");
                });
            }
        });
    } else {
        batchDownloadEpisodes(m_children);
    }
}

void MediaDetailView::downloadUnwatched(int maxCount) {
    // Filter unheard episodes and queue them (like Suwayomi - no blocking dialog)
    std::vector<MediaItem> unheardEpisodes;

    if (m_children.empty()) {
        // Fetch episodes first
        std::string podcastId = m_item.id;
        std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

        asyncRun([this, podcastId, maxCount, aliveWeak]() {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<MediaItem> allEpisodes;

            if (client.fetchPodcastEpisodes(podcastId, allEpisodes)) {
                std::vector<MediaItem> unheard;
                for (auto& ep : allEpisodes) {
                    if (!ep.isFinished && ep.currentTime == 0) {
                        unheard.push_back(ep);
                        if (maxCount > 0 && static_cast<int>(unheard.size()) >= maxCount) break;
                    }
                }
                brls::sync([this, aliveWeak, unheard]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) return;
                    if (unheard.empty()) {
                        brls::Application::notify("No unheard episodes found");
                    } else {
                        batchDownloadEpisodes(unheard);
                    }
                });
            } else {
                brls::sync([]() { brls::Application::notify("Failed to fetch episodes"); });
            }
        });
        return;
    }

    for (const auto& ep : m_children) {
        if (!ep.isFinished && ep.currentTime == 0) {
            unheardEpisodes.push_back(ep);
            if (maxCount > 0 && static_cast<int>(unheardEpisodes.size()) >= maxCount) break;
        }
    }

    if (unheardEpisodes.empty()) {
        brls::Application::notify("No unheard episodes found");
        return;
    }

    batchDownloadEpisodes(unheardEpisodes);
}

void MediaDetailView::findNewEpisodes() {
    brls::Application::notify("Checking RSS feed for new episodes...");

    std::string podcastId = m_item.id;
    std::string podcastTitle = m_item.title;

    asyncRun([this, podcastId, podcastTitle]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> newEpisodes;

        if (client.checkNewEpisodes(podcastId, newEpisodes)) {
            brls::sync([this, newEpisodes, podcastId, podcastTitle]() {
                if (newEpisodes.empty()) {
                    brls::Application::notify("No new episodes found");
                } else {
                    showNewEpisodesDialog(newEpisodes, podcastId, podcastTitle);
                }
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to check for new episodes");
            });
        }
    });
}

void MediaDetailView::showNewEpisodesDialog(const std::vector<MediaItem>& episodes,
                                             const std::string& podcastId,
                                             const std::string& podcastTitle) {
    // Create a dialog to show new episodes
    auto* dialog = new brls::Dialog("New Episodes (" + std::to_string(episodes.size()) + ")");

    // Register circle button to close dialog
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    auto* contentBox = new brls::Box();
    contentBox->setAxis(brls::Axis::COLUMN);
    contentBox->setPadding(15);
    contentBox->setWidth(700);

    // Add "Download All" button at the top
    auto* downloadAllBtn = new brls::Button();
    downloadAllBtn->setText("Download All to Server");
    downloadAllBtn->setMarginBottom(15);
    downloadAllBtn->registerClickAction([this, episodes, podcastId, dialog](brls::View*) {
        dialog->dismiss();
        downloadNewEpisodesToServer(podcastId, episodes);
        return true;
    });
    contentBox->addView(downloadAllBtn);

    // Separator
    auto* separator = new brls::Rectangle();
    separator->setHeight(1);
    separator->setColor(nvgRGB(80, 80, 80));
    separator->setMarginBottom(15);
    contentBox->addView(separator);

    // Instructions
    auto* instructionsLabel = new brls::Label();
    instructionsLabel->setText("Select episodes to add to server:");
    instructionsLabel->setFontSize(14);
    instructionsLabel->setTextColor(nvgRGB(180, 180, 180));
    instructionsLabel->setMarginBottom(10);
    contentBox->addView(instructionsLabel);

    // Create scrollable list of episodes
    auto* scrollFrame = new brls::ScrollingFrame();
    scrollFrame->setHeight(350);

    auto* episodesList = new brls::Box();
    episodesList->setAxis(brls::Axis::COLUMN);

    // Store selected episodes
    auto selectedEpisodes = std::make_shared<std::vector<std::string>>();

    for (const auto& ep : episodes) {
        auto* episodeRow = new brls::Box();
        episodeRow->setAxis(brls::Axis::ROW);
        episodeRow->setAlignItems(brls::AlignItems::CENTER);
        episodeRow->setMarginBottom(15);  // More space between episodes
        episodeRow->setPadding(15);
        episodeRow->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
        episodeRow->setCornerRadius(8);
        episodeRow->setFocusable(true);

        // Episode info
        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);
        infoBox->setJustifyContent(brls::JustifyContent::CENTER);

        // Title - show first line
        auto* titleLabel = new brls::Label();
        std::string title = ep.title;
        std::string title2;

        // Split long titles into two lines
        if (title.length() > 50) {
            size_t splitPos = title.rfind(' ', 50);
            if (splitPos != std::string::npos && splitPos > 20) {
                title2 = title.substr(splitPos + 1);
                title = title.substr(0, splitPos);
                // Truncate second line if still too long
                if (title2.length() > 50) {
                    title2 = title2.substr(0, 47) + "...";
                }
            } else {
                // No good split point, just truncate
                title = title.substr(0, 47) + "...";
            }
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(15);
        infoBox->addView(titleLabel);

        // Second line of title if needed
        if (!title2.empty()) {
            auto* title2Label = new brls::Label();
            title2Label->setText(title2);
            title2Label->setFontSize(14);
            title2Label->setTextColor(nvgRGB(200, 200, 200));
            title2Label->setMarginTop(2);
            infoBox->addView(title2Label);
        }

        if (!ep.pubDate.empty()) {
            auto* dateLabel = new brls::Label();
            dateLabel->setText(ep.pubDate);
            dateLabel->setFontSize(12);
            dateLabel->setTextColor(nvgRGB(150, 150, 150));
            dateLabel->setMarginTop(6);
            infoBox->addView(dateLabel);
        }

        episodeRow->addView(infoBox);

        // Status label (shows "Added!" after adding)
        auto* statusLabel = new brls::Label();
        statusLabel->setText("");
        statusLabel->setFontSize(14);
        statusLabel->setTextColor(nvgRGB(100, 200, 100));
        statusLabel->setWidth(80);
        episodeRow->addView(statusLabel);

        // Click on row to add episode - pass full episode data for new RSS episodes
        MediaItem epCopy = ep;
        std::string pId = podcastId;
        episodeRow->registerClickAction([this, epCopy, pId, statusLabel](brls::View*) {
            // Download this single episode to server with full data
            std::vector<MediaItem> eps = {epCopy};
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            if (client.downloadNewEpisodesToServer(pId, eps)) {
                statusLabel->setText("Added!");
                brls::Application::notify("Episode queued for download on server");
            } else {
                brls::Application::notify("Failed to add episode");
            }
            return true;
        });

        episodesList->addView(episodeRow);
    }

    scrollFrame->setContentView(episodesList);
    contentBox->addView(scrollFrame);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([dialog](brls::View*) {
        dialog->dismiss();
        return true;
    });
    contentBox->addView(closeBtn);

    dialog->addView(contentBox);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::downloadNewEpisodesToServer(const std::string& podcastId,
                                                   const std::vector<MediaItem>& episodes) {
    brls::Application::notify("Adding " + std::to_string(episodes.size()) + " episodes to server...");

    asyncRun([podcastId, episodes]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        // Use full episode data for new RSS episodes
        bool success = client.downloadNewEpisodesToServer(podcastId, episodes);

        brls::sync([success, episodes]() {
            if (success) {
                brls::Application::notify("Queued " + std::to_string(episodes.size()) + " episodes for download");
            } else {
                brls::Application::notify("Failed to queue episodes");
            }
        });
    });
}

} // namespace vitaabs
