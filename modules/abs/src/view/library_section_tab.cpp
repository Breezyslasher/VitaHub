/**
 * VitaABS - Library Section Tab implementation
 */

#include "view/library_section_tab.hpp"
#include "view/podcast_search_tab.hpp"
#include "app/audiobookshelf_client.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "app/application.hpp"
#include "utils/async.hpp"

namespace vitaabs {

LibrarySectionTab::LibrarySectionTab(const std::string& sectionKey, const std::string& title, const std::string& sectionType)
    : m_sectionKey(sectionKey), m_title(title), m_sectionType(sectionType) {

    // Create alive flag for async callback safety
    m_alive = std::make_shared<std::atomic<bool>>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(title);
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(15);
    this->addView(m_titleLabel);

    const auto& settings = Application::getInstance().getSettings();

    // View mode selector (All / Collections / Categories)
    m_viewModeBox = new brls::Box();
    m_viewModeBox->setAxis(brls::Axis::ROW);
    m_viewModeBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_viewModeBox->setAlignItems(brls::AlignItems::CENTER);
    m_viewModeBox->setMarginBottom(15);

    // All Items button
    m_allBtn = new brls::Button();
    m_allBtn->setText("All");
    m_allBtn->setMarginRight(10);
    m_allBtn->registerClickAction([this](brls::View* view) {
        showAllItems();
        return true;
    });
    m_viewModeBox->addView(m_allBtn);

    // Collections button (only show if collections are enabled)
    if (settings.showCollections) {
        m_collectionsBtn = new brls::Button();
        m_collectionsBtn->setText("Collections");
        m_collectionsBtn->setMarginRight(10);
        m_collectionsBtn->registerClickAction([this](brls::View* view) {
            showCollections();
            return true;
        });
        m_viewModeBox->addView(m_collectionsBtn);
    }

    // Note: Categories/Genres button removed - Audiobookshelf doesn't have a genre browsing API

    // Back button (hidden by default, shown in filtered view)
    m_backBtn = new brls::Button();
    m_backBtn->setText("< Back");
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->registerClickAction([this](brls::View* view) {
        showAllItems();
        return true;
    });
    m_viewModeBox->addView(m_backBtn);

    // Find Podcasts and Check Episodes buttons (only for podcast libraries)
    if (sectionType == "podcast") {
        // Find Podcasts button container with Start button hint
        m_findPodcastsContainer = new brls::Box();
        m_findPodcastsContainer->setAxis(brls::Axis::COLUMN);
        m_findPodcastsContainer->setAlignItems(brls::AlignItems::CENTER);
        m_findPodcastsContainer->setMarginLeft(20);

        auto* searchHintIcon = new brls::Image();
        searchHintIcon->setHeight(16);
        searchHintIcon->setScalingType(brls::ImageScalingType::FIT);
        searchHintIcon->setImageFromFile(RESOURCE_PREFIX "images/start_button.png");
        searchHintIcon->setMarginBottom(2);
        m_findPodcastsContainer->addView(searchHintIcon);

        m_findPodcastsBtn = new brls::Button();
        m_findPodcastsBtn->setWidth(44);
        m_findPodcastsBtn->setHeight(40);
        m_findPodcastsBtn->setCornerRadius(8);
        m_findPodcastsBtn->setJustifyContent(brls::JustifyContent::CENTER);
        m_findPodcastsBtn->setAlignItems(brls::AlignItems::CENTER);

        auto* searchIcon = new brls::Image();
        searchIcon->setWidth(24);
        searchIcon->setHeight(24);
        searchIcon->setScalingType(brls::ImageScalingType::FIT);
        searchIcon->setImageFromFile(RESOURCE_PREFIX "icons/search.png");
        m_findPodcastsBtn->addView(searchIcon);

        m_findPodcastsBtn->registerClickAction([this](brls::View* view) {
            openPodcastSearch();
            return true;
        });
        m_findPodcastsContainer->addView(m_findPodcastsBtn);
        m_viewModeBox->addView(m_findPodcastsContainer);

        // Check New Episodes button container with Select button hint
        m_checkEpisodesContainer = new brls::Box();
        m_checkEpisodesContainer->setAxis(brls::Axis::COLUMN);
        m_checkEpisodesContainer->setAlignItems(brls::AlignItems::CENTER);
        m_checkEpisodesContainer->setMarginLeft(8);

        auto* refreshHintIcon = new brls::Image();
        refreshHintIcon->setHeight(16);
        refreshHintIcon->setScalingType(brls::ImageScalingType::FIT);
        refreshHintIcon->setImageFromFile(RESOURCE_PREFIX "images/select_button.png");
        refreshHintIcon->setMarginBottom(2);
        m_checkEpisodesContainer->addView(refreshHintIcon);

        m_checkEpisodesBtn = new brls::Button();
        m_checkEpisodesBtn->setWidth(44);
        m_checkEpisodesBtn->setHeight(40);
        m_checkEpisodesBtn->setCornerRadius(8);
        m_checkEpisodesBtn->setJustifyContent(brls::JustifyContent::CENTER);
        m_checkEpisodesBtn->setAlignItems(brls::AlignItems::CENTER);

        auto* refreshIcon = new brls::Image();
        refreshIcon->setWidth(24);
        refreshIcon->setHeight(24);
        refreshIcon->setScalingType(brls::ImageScalingType::FIT);
        refreshIcon->setImageFromFile(RESOURCE_PREFIX "icons/refresh.png");
        m_checkEpisodesBtn->addView(refreshIcon);

        m_checkEpisodesBtn->registerClickAction([this](brls::View* view) {
            checkAllNewEpisodes();
            return true;
        });
        m_checkEpisodesContainer->addView(m_checkEpisodesBtn);
        m_viewModeBox->addView(m_checkEpisodesContainer);

        // Register Start button to open podcast search
        this->registerAction("Search", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
            openPodcastSearch();
            return true;
        });

        // Register Select button to check for new episodes
        this->registerAction("Update", brls::ControllerButton::BUTTON_BACK, [this](brls::View*) {
            checkAllNewEpisodes();
            return true;
        });
    }

    this->addView(m_viewModeBox);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const MediaItem& item) {
        onItemSelected(item);
    });
    m_contentGrid->setOnLoadMore([this]() {
        loadNextPage();
    });
    this->addView(m_contentGrid);

    // Load content immediately
    brls::Logger::debug("LibraryTab: Created for section {} ({}) type={}", m_sectionKey, m_title, m_sectionType);
    loadContent();
}

LibrarySectionTab::~LibrarySectionTab() {
    // Mark as no longer alive to prevent async callbacks from updating destroyed UI
    if (m_alive) {
        *m_alive = false;
    }
    brls::Logger::debug("LibrarySectionTab: Destroyed for section {}", m_sectionKey);
}

void LibrarySectionTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded) {
        loadContent();
    }
}

void LibrarySectionTab::loadContent() {
    brls::Logger::debug("LibrarySectionTab::loadContent - section: {} (async)", m_sectionKey);

    const auto& settings = Application::getInstance().getSettings();
    std::string key = m_sectionKey;
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;  // Capture weak_ptr for async safety

    // First page only; the grid's onLoadMore fetches the rest as the user
    // scrolls (infinite scroll, like Vita-Music-Assistant). Bump the load
    // generation so completions from a previous load/page fetch are dropped.
    int gen = ++m_loadGen;
    m_loadingPage = false;

    asyncRun([this, key, gen, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;
        int total = -1;
        int rawCount = 0;

        if (client.fetchLibraryItemsPage(key, items, 0, kPageSize, total, "", &rawCount)) {
            brls::Logger::info("LibraryTab: Got {} items for section {} (total={}, raw={})",
                               items.size(), key, total, rawCount);

            brls::sync([this, items, total, rawCount, gen, aliveWeak]() {
                // Check if object is still alive before updating UI
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    brls::Logger::debug("LibraryTab: Tab destroyed, skipping UI update");
                    return;
                }
                if (gen != m_loadGen) return;  // superseded by a newer load

                m_items = items;
                m_page = 1;
                m_serverTotal = total;
                // A full raw page means the server likely has more; a short one
                // means we've reached the end (raw count, not accepted count —
                // items dropped for missing id/title must not wedge pagination).
                m_hasMore = (rawCount >= kPageSize);
                // Only update grid if we're in ALL_ITEMS mode
                if (m_viewMode == LibraryViewMode::ALL_ITEMS) {
                    m_contentGrid->setDataSource(m_items);
                    m_contentGrid->setHasMore(m_hasMore);
                }
                m_loaded = true;
            });
        } else {
            brls::Logger::error("LibraryTab: Failed to load content for section {}", key);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                // Offline - show empty state with offline label
                if (m_title.find("(Offline)") == std::string::npos) {
                    m_titleLabel->setText(m_title + " (Offline)");
                }
                hideNavigationButtons();
                m_loaded = true;
            });
        }
    });

    // Preload collections for quick switching
    if (settings.showCollections) {
        loadCollections();
    }
    // Note: Genre preloading removed - Audiobookshelf doesn't have a genre browsing API
}

void LibrarySectionTab::loadNextPage() {
    // In-flight guard (VMA's m_loadingPage): the grid's own m_loading flag is
    // reset by setDataSource/setHasMore, so it alone cannot prevent the same
    // page being fetched twice.
    if (m_loadingPage || !m_hasMore) return;
    m_loadingPage = true;

    brls::Logger::debug("LibraryTab: Loading next page {} for section {}", m_page, m_sectionKey);

    std::string key = m_sectionKey;
    int page = m_page;
    int gen = m_loadGen;
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    asyncRun([this, key, page, gen, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;
        int total = -1;
        int rawCount = 0;

        bool ok = client.fetchLibraryItemsPage(key, items, page, kPageSize, total, "", &rawCount);

        brls::sync([this, items, total, rawCount, ok, gen, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (gen != m_loadGen) return;  // a fresh loadContent superseded us

            m_loadingPage = false;

            if (!ok) {
                // Fetch failed: stop asking for more.
                m_hasMore = false;
                if (m_contentGrid) m_contentGrid->setHasMore(false);
                return;
            }

            m_items.insert(m_items.end(), items.begin(), items.end());
            m_page++;
            if (total >= 0) m_serverTotal = total;
            m_hasMore = (rawCount >= kPageSize);

            brls::Logger::info("LibraryTab: Page {} added {} items ({} / {})",
                               m_page - 1, items.size(), m_items.size(), m_serverTotal);

            if (m_viewMode == LibraryViewMode::ALL_ITEMS && m_contentGrid) {
                m_contentGrid->appendItems(items);
                m_contentGrid->setHasMore(m_hasMore);
            }
        });
    });
}

void LibrarySectionTab::loadCollections() {
    std::string key = m_sectionKey;
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    asyncRun([this, key, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<Collection> collections;

        if (client.fetchLibraryCollections(key, collections)) {
            brls::Logger::info("LibrarySectionTab: Got {} collections for section {}", collections.size(), key);

            // Convert Collection to MediaItem for display
            std::vector<MediaItem> collectionItems;
            for (const auto& col : collections) {
                MediaItem item;
                item.id = col.id;
                item.title = col.name;
                item.description = col.description;
                item.coverPath = col.coverPath;
                item.type = "collection";
                item.mediaType = MediaType::UNKNOWN;
                collectionItems.push_back(item);
            }

            brls::sync([this, collectionItems, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_collections = collectionItems;
                m_collectionsLoaded = true;

                // Hide collections button if none available
                if (m_collections.empty() && m_collectionsBtn) {
                    m_collectionsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        } else {
            brls::Logger::debug("LibrarySectionTab: No collections for section {}", key);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_collectionsLoaded = true;
                if (m_collectionsBtn) {
                    m_collectionsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        }
    });
}

void LibrarySectionTab::loadGenres() {
    // Audiobookshelf doesn't have a dedicated genre browsing API like Plex
    // Genre filtering would require fetching all items and extracting unique genres
    // For now, we disable the genres feature
    brls::Logger::debug("LibrarySectionTab: Genre browsing not supported in Audiobookshelf");

    m_genresLoaded = true;
    m_genres.clear();

    if (m_categoriesBtn) {
        m_categoriesBtn->setVisibility(brls::Visibility::GONE);
    }
}

void LibrarySectionTab::showAllItems() {
    m_viewMode = LibraryViewMode::ALL_ITEMS;
    m_titleLabel->setText(m_title);
    m_contentGrid->setDataSource(m_items);
    m_contentGrid->setHasMore(m_hasMore);
    updateViewModeButtons();
}

void LibrarySectionTab::showCollections() {
    if (!m_collectionsLoaded) {
        brls::Application::notify("Loading collections...");
        return;
    }

    if (m_collections.empty()) {
        brls::Application::notify("No collections available");
        return;
    }

    m_viewMode = LibraryViewMode::COLLECTIONS;
    m_titleLabel->setText(m_title + " - Collections");

    // Show collections in the grid (no pagination for collections)
    m_contentGrid->setDataSource(m_collections);
    m_contentGrid->setHasMore(false);
    updateViewModeButtons();
}

void LibrarySectionTab::showCategories() {
    if (!m_genresLoaded) {
        brls::Application::notify("Loading categories...");
        return;
    }

    if (m_genres.empty()) {
        brls::Application::notify("No categories available");
        return;
    }

    m_viewMode = LibraryViewMode::CATEGORIES;
    m_titleLabel->setText(m_title + " - Categories");

    // Convert genres to MediaItem format for the grid
    std::vector<MediaItem> genreItems;
    for (const auto& genre : m_genres) {
        MediaItem item;
        item.title = genre.title;
        item.id = genre.id;  // Use genre key for filtering
        item.type = "genre";
        item.mediaType = MediaType::UNKNOWN;
        genreItems.push_back(item);
    }

    m_contentGrid->setDataSource(genreItems);
    m_contentGrid->setHasMore(false);
    updateViewModeButtons();
}

void LibrarySectionTab::updateViewModeButtons() {
    // Show/hide back button
    bool inFilteredView = (m_viewMode == LibraryViewMode::FILTERED);
    m_backBtn->setVisibility(inFilteredView ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    // Show/hide mode buttons
    bool showModeButtons = (m_viewMode != LibraryViewMode::FILTERED);
    if (m_allBtn) {
        m_allBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_collectionsBtn) {
        m_collectionsBtn->setVisibility(showModeButtons && !m_collections.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_categoriesBtn) {
        m_categoriesBtn->setVisibility(showModeButtons && !m_genres.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
}

void LibrarySectionTab::hideNavigationButtons() {
    // Hide all navigation buttons for offline/downloaded-only mode
    if (m_allBtn) {
        m_allBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_collectionsBtn) {
        m_collectionsBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_categoriesBtn) {
        m_categoriesBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_backBtn) {
        m_backBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_findPodcastsContainer) {
        m_findPodcastsContainer->setVisibility(brls::Visibility::GONE);
    }
    if (m_checkEpisodesContainer) {
        m_checkEpisodesContainer->setVisibility(brls::Visibility::GONE);
    }
}

void LibrarySectionTab::onItemSelected(const MediaItem& item) {
    brls::Logger::debug("LibrarySectionTab::onItemSelected - title='{}' id='{}' type='{}' viewMode={}",
                       item.title, item.id, item.type, static_cast<int>(m_viewMode));

    // Handle selection based on current view mode
    if (m_viewMode == LibraryViewMode::COLLECTIONS) {
        // Selected a collection - show its contents
        brls::Logger::debug("LibrarySectionTab: Opening collection");
        onCollectionSelected(item);
        return;
    }

    if (m_viewMode == LibraryViewMode::CATEGORIES) {
        // Selected a category/genre - filter by it
        brls::Logger::debug("LibrarySectionTab: Opening category");
        GenreItem genre;
        genre.title = item.title;
        genre.id = item.id;
        onGenreSelected(genre);
        return;
    }

    // Normal item selection - for Audiobookshelf, most items go to detail view
    // Podcast episodes can be played directly
    if (item.mediaType == MediaType::PODCAST_EPISODE) {
        brls::Logger::debug("LibrarySectionTab: Playing podcast episode");
        Application::getInstance().pushPlayerActivity(item.podcastId, item.episodeId);
        return;
    }

    // Show media detail view for books and other types
    brls::Logger::info("LibrarySectionTab: Opening detail view for '{}'", item.title);
    auto* detailView = new MediaDetailView(item);
    brls::Logger::debug("LibrarySectionTab: MediaDetailView created, pushing activity");
    brls::Application::pushActivity(new brls::Activity(detailView));
    brls::Logger::debug("LibrarySectionTab: Activity pushed successfully");
}

void LibrarySectionTab::onCollectionSelected(const MediaItem& collection) {
    brls::Logger::debug("LibrarySectionTab: Selected collection: {}", collection.title);

    m_filterTitle = collection.title;
    std::string collectionId = collection.id;
    std::string filterTitle = m_filterTitle;
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    asyncRun([this, collectionId, filterTitle, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchCollectionBooks(collectionId, items)) {
            brls::Logger::info("LibrarySectionTab: Got {} items in collection", items.size());

            brls::sync([this, items, filterTitle, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    brls::Logger::debug("LibrarySectionTab: Tab no longer alive, skipping collection display");
                    return;
                }

                brls::Logger::debug("LibrarySectionTab: Displaying {} collection items", items.size());
                m_viewMode = LibraryViewMode::FILTERED;
                brls::Logger::debug("LibrarySectionTab: Setting title");
                m_titleLabel->setText(m_title + " - " + filterTitle);
                brls::Logger::debug("LibrarySectionTab: Setting grid data source");
                m_contentGrid->setDataSource(items);
                m_contentGrid->setHasMore(false);
                brls::Logger::debug("LibrarySectionTab: Grid updated, updating buttons");
                updateViewModeButtons();
                brls::Logger::debug("LibrarySectionTab: Buttons updated");

                // Try to give focus to the grid
                brls::Logger::debug("LibrarySectionTab: About to request focus on grid");
                if (m_contentGrid) {
                    brls::Application::giveFocus(m_contentGrid);
                }
                brls::Logger::debug("LibrarySectionTab: Collection display complete");
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load collection content");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load collection");
            });
        }
    });
}

void LibrarySectionTab::onGenreSelected(const GenreItem& genre) {
    brls::Logger::debug("LibraryTab: Selected genre: {} (key: {})", genre.title, genre.id);

    m_filterTitle = genre.title;
    std::string key = m_sectionKey;
    std::string genreKey = genre.id;
    std::string genreTitle = genre.title;
    std::string filterTitle = m_filterTitle;
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    asyncRun([this, key, genreKey, genreTitle, filterTitle, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        // Try with genre key first, fall back to title
        if (client.fetchByGenreKey(key, genreKey, items) || client.fetchByGenre(key, genreTitle, items)) {
            brls::Logger::info("LibraryTab: Got {} items for genre", items.size());

            brls::sync([this, items, filterTitle, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_viewMode = LibraryViewMode::FILTERED;
                m_titleLabel->setText(m_title + " - " + filterTitle);
                m_contentGrid->setDataSource(items);
                m_contentGrid->setHasMore(false);
                updateViewModeButtons();
            });
        } else {
            brls::Logger::error("LibraryTab: Failed to load genre content");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load category");
            });
        }
    });
}

void LibrarySectionTab::openPodcastSearch() {
    brls::Logger::debug("LibrarySectionTab: Opening podcast search for library {}", m_sectionKey);

    auto* searchTab = new PodcastSearchTab(m_sectionKey);
    brls::Application::pushActivity(new brls::Activity(searchTab));
}

void LibrarySectionTab::checkAllNewEpisodes() {
    brls::Logger::debug("LibrarySectionTab: Checking for new episodes in all podcasts");

    brls::Application::notify("Checking for new episodes...");

    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;
    std::string libraryKey = m_sectionKey;

    // Snapshot the podcast ids on the UI thread: the worker must NOT iterate
    // m_items directly — infinite-scroll pagination appends to it on the UI
    // thread while the (long) check runs, invalidating the worker's iterators.
    std::vector<std::string> podcastIds;
    for (const auto& item : m_items) {
        if (item.type == "podcast" || item.mediaType == MediaType::PODCAST) {
            podcastIds.push_back(item.id);
        }
    }

    asyncRun([podcastIds, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        int totalNew = 0;
        for (const auto& podcastId : podcastIds) {
            std::vector<MediaItem> newEps;
            if (client.checkNewEpisodes(podcastId, newEps)) {
                if (!newEps.empty()) {
                    totalNew += newEps.size();
                    // Auto-download new episodes
                    client.downloadAllNewEpisodes(podcastId);
                }
            }
        }

        brls::sync([totalNew, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (totalNew > 0) {
                brls::Application::notify("Found " + std::to_string(totalNew) + " new episode(s)");
            } else {
                brls::Application::notify("No new episodes found");
            }
        });
    });
}

} // namespace vitaabs
