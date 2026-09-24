/**
 * Vita Music Assistant - Library Tab implementation
 * Music-only library browser with categories: Artists, Albums, Tracks, Playlists
 */

#include "view/library_tab.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "activity/player_activity.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "app/music_queue.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include <chrono>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace vita_ma {

// MusicCategory is defined in library_tab.hpp.

namespace {
// Serialize all library parsing onto ONE persistent background thread. Rapidly
// scrolling the sidebar used to spawn a detached parse thread per tab - each
// parsing 1000+ items and allocating thousands of std::strings. A dozen of them
// running at once exhausted/fragmented the heap and crashed the UI thread mid
// std::string construction (data abort, freed-memory poison in the registers).
// A single worker bounds peak memory to one parse at a time; tasks for tabs that
// were destroyed while queued are dropped before the expensive parse runs.
class LibraryParseExecutor {
public:
    static LibraryParseExecutor& instance() {
        static LibraryParseExecutor inst;
        return inst;
    }
    void post(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_tasks.push(std::move(task));
        }
        m_cv.notify_one();
    }
private:
    LibraryParseExecutor() { std::thread([this]() { run(); }).detach(); }
    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [this]() { return !m_tasks.empty(); });
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            task();
        }
    }
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_tasks;
};
} // namespace

static const char* categoryName(MusicCategory cat) {
    switch (cat) {
        case MusicCategory::ARTISTS:    return "Artists";
        case MusicCategory::ALBUMS:     return "Albums";
        case MusicCategory::TRACKS:     return "Tracks";
        case MusicCategory::PLAYLISTS:  return "Playlists";
        case MusicCategory::PODCASTS:   return "Podcasts";
        case MusicCategory::AUDIOBOOKS: return "Audiobooks";
        case MusicCategory::RADIOS:     return "Radio";
    }
    return "Unknown";
}

// Helper to parse a JSON array of music items into MusicItem vector
[[maybe_unused]] static std::vector<MusicItem> parseMusicItems(const Json& result, MediaType expectedType) {
    std::vector<MusicItem> items;

    if (result.type() != Json::ARRAY) {
        brls::Logger::error("LibraryTab: Expected array result from server");
        return items;
    }

    for (size_t i = 0; i < result.size(); i++) {
        const Json& obj = result[i];
        MusicItem item;
        item.itemId     = obj.has("item_id")    ? obj["item_id"].str()    : "";
        item.name       = obj.has("name")        ? obj["name"].str()       : "";
        item.sortName   = obj.has("sort_name")   ? obj["sort_name"].str()  : "";
        item.uri        = obj.has("uri")         ? obj["uri"].str()        : "";
        item.mediaType  = expectedType;

        // Extract image URL: try image object, then metadata.images array
        if (obj.has("image") && obj["image"].type() == Json::OBJECT && obj["image"].has("path")) {
            item.imageUrl = MAClient::imageRefFromJson(obj["image"]);
            if (obj["image"].has("provider")) item.imageProvider = obj["image"]["provider"].str();
        } else if (obj.has("image") && obj["image"].type() == Json::STRING) {
            item.imageUrl = obj["image"].str();
        } else if (obj.has("metadata") && obj["metadata"].type() == Json::OBJECT) {
            const Json& meta = obj["metadata"];
            if (meta.has("images") && meta["images"].type() == Json::ARRAY && meta["images"].size() > 0) {
                const Json& img = meta["images"][static_cast<size_t>(0)];
                item.imageUrl = MAClient::imageRefFromJson(img);
                if (img.has("provider")) item.imageProvider = img["provider"].str();
            }
        }

        // Track-specific fields
        if (obj.has("artist_name"))  item.artistName  = obj["artist_name"].str();
        if (obj.has("album_name"))   item.albumName   = obj["album_name"].str();
        if (obj.has("track_number")) item.trackNumber  = obj["track_number"].intVal();
        if (obj.has("disc_number"))  item.discNumber   = obj["disc_number"].intVal();
        if (obj.has("duration"))     item.duration     = obj["duration"].intVal(); // already in seconds

        // Album-specific fields
        if (obj.has("year"))    item.year    = obj["year"].intVal();
        if (obj.has("version")) item.version = obj["version"].str();

        // Playlist-specific fields
        if (obj.has("item_count"))  item.itemCount   = obj["item_count"].intVal();
        if (obj.has("is_editable")) item.isEditable  = obj["is_editable"].boolVal();

        // Common fields
        if (obj.has("favorite")) item.favorite = obj["favorite"].boolVal();
        if (obj.has("provider")) item.provider = obj["provider"].str();

        items.push_back(item);
    }

    return items;
}

// DOM-free variant: parse library items straight from the raw JSON array
// substring (no Json DOM), the way Vita_Suwayomi does. Building a full DOM of a
// 2.6 MB / ~40k-node response cost ~8s on Vita; this pulls only the handful of
// fields we need per object and runs in a fraction of that.
static std::vector<MusicItem> parseMusicItemsRaw(const std::string& rawArray,
                                                 MediaType expectedType) {
    std::vector<MusicItem> items;

    auto applyImage = [](const std::string& io, MusicItem& item) -> bool {
        if (io.empty()) return false;
        std::string proxy = MAClient::rawExtractField(io, "proxy_id");
        if (!proxy.empty()) {
            item.imageUrl = "proxyid:" + proxy;
        } else {
            std::string path = MAClient::rawExtractField(io, "path");
            if (path.empty()) return false;
            item.imageUrl = path;
        }
        std::string prov = MAClient::rawExtractField(io, "provider");
        if (!prov.empty()) item.imageProvider = prov;
        return true;
    };

    // Single pass: find each top-level object's [objStart, objEnd) once (no
    // per-object substring copies), then extract fields directly from that byte
    // range of the original string. Only the field values we keep get allocated.
    const char* d = rawArray.data();
    const size_t n = rawArray.size();
    size_t i = 0;
    while (i < n && d[i] != '[') i++;
    if (i < n) i++;  // step past '['
    items.reserve(1024);

    while (i < n) {
        while (i < n && d[i] != '{') { if (d[i] == ']') { i = n; break; } i++; }
        if (i >= n) break;
        size_t objStart = i;
        int depth = 1;
        bool inStr = false;
        i++;
        while (i < n && depth > 0) {
            char ch = d[i];
            if (ch == '"' && d[i - 1] != '\\') inStr = !inStr;
            else if (!inStr) { if (ch == '{') depth++; else if (ch == '}') depth--; }
            i++;
        }
        size_t objEnd = i;  // one past the object's closing '}'

        MusicItem item;
        std::string s;
        item.itemId    = MAClient::rawExtractFieldIn(rawArray, "item_id", objStart, objEnd);
        item.name      = MAClient::rawExtractFieldIn(rawArray, "name", objStart, objEnd);
        item.sortName  = MAClient::rawExtractFieldIn(rawArray, "sort_name", objStart, objEnd);
        item.uri       = MAClient::rawExtractFieldIn(rawArray, "uri", objStart, objEnd);
        item.mediaType = expectedType;
        s = MAClient::rawExtractFieldIn(rawArray, "favorite", objStart, objEnd); if (!s.empty()) item.favorite = (s == "true");
        s = MAClient::rawExtractFieldIn(rawArray, "provider", objStart, objEnd); if (!s.empty()) item.provider = s;

        // Image: a top-level "image" object, else the first of (metadata) "images".
        if (!applyImage(MAClient::rawExtractFieldIn(rawArray, "image", objStart, objEnd), item)) {
            std::string imagesArr = MAClient::rawExtractFieldIn(rawArray, "images", objStart, objEnd);
            if (!imagesArr.empty()) {
                auto imgs = MAClient::rawSplitArrayObjects(imagesArr);
                if (!imgs.empty()) applyImage(imgs[0], item);
            }
        }

        if (expectedType == MediaType::TRACK) {
            s = MAClient::rawExtractFieldIn(rawArray, "artist_name", objStart, objEnd);  if (!s.empty()) item.artistName = s;
            s = MAClient::rawExtractFieldIn(rawArray, "album_name", objStart, objEnd);   if (!s.empty()) item.albumName = s;
            s = MAClient::rawExtractFieldIn(rawArray, "track_number", objStart, objEnd); if (!s.empty()) item.trackNumber = atoi(s.c_str());
            s = MAClient::rawExtractFieldIn(rawArray, "disc_number", objStart, objEnd);  if (!s.empty()) item.discNumber = atoi(s.c_str());
            s = MAClient::rawExtractFieldIn(rawArray, "duration", objStart, objEnd);     if (!s.empty()) item.duration = atoi(s.c_str());
        } else if (expectedType == MediaType::ALBUM) {
            s = MAClient::rawExtractFieldIn(rawArray, "year", objStart, objEnd);         if (!s.empty()) item.year = atoi(s.c_str());
            s = MAClient::rawExtractFieldIn(rawArray, "version", objStart, objEnd);      if (!s.empty()) item.version = s;
        } else if (expectedType == MediaType::PLAYLIST) {
            s = MAClient::rawExtractFieldIn(rawArray, "item_count", objStart, objEnd);   if (!s.empty()) item.itemCount = atoi(s.c_str());
            s = MAClient::rawExtractFieldIn(rawArray, "is_editable", objStart, objEnd);  if (!s.empty()) item.isEditable = (s == "true");
        }

        items.push_back(std::move(item));
    }

    return items;
}

static const char* categoryMediaType(MusicCategory category) {
    switch (category) {
        case MusicCategory::ARTISTS:    return "artists";
        case MusicCategory::ALBUMS:     return "albums";
        case MusicCategory::TRACKS:     return "tracks";
        case MusicCategory::PLAYLISTS:  return "playlists";
        case MusicCategory::PODCASTS:   return "podcasts";
        case MusicCategory::AUDIOBOOKS: return "audiobooks";
        case MusicCategory::RADIOS:     return "radios";
    }
    return "albums";
}

// Media type each category's items should be tagged with.
static MediaType categoryMediaTypeEnum(MusicCategory category) {
    switch (category) {
        case MusicCategory::ARTISTS:    return MediaType::ARTIST;
        case MusicCategory::ALBUMS:     return MediaType::ALBUM;
        case MusicCategory::TRACKS:     return MediaType::TRACK;
        case MusicCategory::PLAYLISTS:  return MediaType::PLAYLIST;
        case MusicCategory::PODCASTS:   return MediaType::PODCAST;
        case MusicCategory::AUDIOBOOKS: return MediaType::AUDIOBOOK;
        case MusicCategory::RADIOS:     return MediaType::RADIO;
    }
    return MediaType::UNKNOWN;
}

LibraryTab::LibraryTab(MusicCategory category) {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    m_currentCategory = static_cast<int>(category);
    m_currentCategoryName = categoryName(category);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(m_currentCategoryName);
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(20);
    this->addView(m_titleLabel);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const MusicItem& item) {
        onItemSelected(item);
    });
    m_contentGrid->setOnItemStartAction([this](const MusicItem& item) {
        showAlbumContextMenu(item);
    });
    this->addView(m_contentGrid);

    brls::Logger::debug("LibraryTab: Initializing with {} category", m_currentCategoryName);
    onCategorySelected(category);
}

LibraryTab::~LibraryTab() {
    if (m_alive) { *m_alive = false; }
}

void LibraryTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);
    if (m_alive) *m_alive = false;
    ImageLoader::cancelAll();
}

void LibraryTab::onFocusGained() {
    brls::Box::onFocusGained();
    m_alive = std::make_shared<bool>(true);

    if (!m_loaded) {
        onCategorySelected(static_cast<MusicCategory>(m_currentCategory));
    }
}

void LibraryTab::onCategorySelected(MusicCategory category) {
    m_currentCategory = static_cast<int>(category);
    m_currentCategoryName = categoryName(category);
    m_titleLabel->setText(m_currentCategoryName);

    loadCategoryContent(category);
}

void LibraryTab::loadCategoryContent(MusicCategory category) {
    brls::Logger::debug("LibraryTab::loadCategoryContent - {}", categoryName(category));

    // Reset pagination state for fresh category load
    m_offset = 0;
    m_hasMore = false;
    m_loadingPage = false;
    m_items.clear();
    int loadGen = ++m_loadGen;  // invalidates any in-flight pages from a prior category

    MAClient& client = MAClient::instance();
    auto aliveWeak = std::weak_ptr<bool>(m_alive);

    auto onResponse = [this, category, aliveWeak, loadGen](bool success, const std::string& rawResult) {
        if (!success) {
            brls::Logger::error("LibraryTab: Failed to load {} content", categoryName(category));
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load library content");
            });
            return;
        }

        MediaType expectedType = categoryMediaTypeEnum(category);

        // Parse off the UI thread - even the raw extraction takes a few seconds
        // for a 1000+ item library, and this callback runs on the main thread.
        // rawResult is a temporary that won't outlive this call, so copy it for
        // the worker; apply the parsed items back on the main thread.
        std::string raw = rawResult;
        LibraryParseExecutor::instance().post([this, raw = std::move(raw), expectedType, category, aliveWeak, loadGen]() {
            // Drop work for a tab that was destroyed/hidden while this task sat in
            // the queue (rapid sidebar browsing) before the expensive parse. If we
            // skip, m_loaded stays false so re-focusing the tab reloads it.
            {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
            }
            auto parseT0 = std::chrono::steady_clock::now();
            std::vector<MusicItem> items = parseMusicItemsRaw(raw, expectedType);
            int count = (int)items.size();
            auto parseMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - parseT0).count();
            brls::Logger::info("LibraryTab: Got {} {} items - parseMusicItemsRaw {}ms (worker)",
                               count, categoryName(category), (long)parseMs);

            brls::sync([this, items = std::move(items), count, aliveWeak, loadGen]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (loadGen != m_loadGen) return;  // category changed while loading

                auto t0 = std::chrono::steady_clock::now();

                m_offset += count;
                m_hasMore = (count >= PAGE_SIZE);  // full page => there may be more
                m_loadingPage = false;

                if (m_items.empty()) {
                    m_items = items;
                    m_contentGrid->setDataSource(m_items);
                } else {
                    m_items.insert(m_items.end(), items.begin(), items.end());
                    m_contentGrid->appendItems(items);
                }

                m_contentGrid->setHasMore(m_hasMore);
                m_loaded = true;

                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                brls::Logger::info("LibraryTab: applied page ({} items, total {}) on UI thread in {}ms",
                                   count, (int)m_items.size(), (long)ms);
            });
        });
    };

    // Set up pagination callback
    m_contentGrid->setOnLoadMore([this]() { loadNextPage(); });

    // Raw (DOM-free) fetch: the response is parsed directly from the string.
    client.getLibraryItemsRaw(categoryMediaType(category), onResponse, "", PAGE_SIZE, 0);
}

void LibraryTab::loadNextPage() {
    if (m_loadingPage || !m_hasMore) return;
    m_loadingPage = true;
    int loadGen = m_loadGen;  // bound to the current category load

    MusicCategory category = static_cast<MusicCategory>(m_currentCategory);
    brls::Logger::debug("LibraryTab: Loading next page at offset {}", m_offset);

    MAClient& client = MAClient::instance();
    auto aliveWeak = std::weak_ptr<bool>(m_alive);

    auto onResponse = [this, category, aliveWeak, loadGen](bool success, const std::string& rawResult) {
        if (!success) {
            brls::sync([this, aliveWeak, loadGen]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (loadGen != m_loadGen) return;
                m_loadingPage = false;
                m_contentGrid->setHasMore(false);
            });
            return;
        }

        MediaType expectedType = categoryMediaTypeEnum(category);

        std::vector<MusicItem> items = parseMusicItemsRaw(rawResult, expectedType);
        int count = (int)items.size();
        brls::Logger::info("LibraryTab: Got {} more {} items (offset {})", count, categoryName(category), m_offset);

        brls::sync([this, items, count, aliveWeak, loadGen]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (loadGen != m_loadGen) return;  // category changed while loading

            auto t0 = std::chrono::steady_clock::now();

            m_offset += count;
            m_hasMore = (count >= PAGE_SIZE);
            m_loadingPage = false;

            m_items.insert(m_items.end(), items.begin(), items.end());
            m_contentGrid->appendItems(items);
            m_contentGrid->setHasMore(m_hasMore);

            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            brls::Logger::info("LibraryTab: appended page ({} items, total {}) on UI thread in {}ms",
                               count, (int)m_items.size(), (long)ms);

            // Lazy pagination only: the next page loads when the user scrolls
            // near the end (RecyclingGrid onLoadMore), not eagerly here.
        });
    };

    client.getLibraryItemsRaw(categoryMediaType(category), onResponse, "", PAGE_SIZE, m_offset);
}

void LibraryTab::onItemSelected(const MusicItem& item) {
    // For tracks, play directly
    if (item.mediaType == MediaType::TRACK) {
        // TODO: Use MA queue API (playMedia) to start playback of this track
        // For now, push player activity with the item's ID
        brls::Logger::debug("LibraryTab: Playing track: {}", item.name);
        Application::getInstance().pushPlayerActivity(item.itemId);
        return;
    }

    // For artists, albums, playlists - show detail view
    auto* detailView = new MediaDetailView(item);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

void LibraryTab::showAlbumContextMenu(const MusicItem& album) {
    // Delegate to the fully-implemented static context menus in MediaDetailView.
    // These support both local and remote players via playTracksOnSelectedPlayer.
    if (album.mediaType == MediaType::ARTIST) {
        MediaDetailView::showArtistContextMenuStatic(album);
    } else if (album.mediaType == MediaType::TRACK) {
        MediaDetailView::performTrackActionStatic(album);
    } else {
        // Albums, Playlists, and anything else: use album context menu
        // (works for playlists too since both fetch tracks and queue them)
        MediaDetailView::showAlbumContextMenuStatic(album);
    }
}

} // namespace vita_ma
