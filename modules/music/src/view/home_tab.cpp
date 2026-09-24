/**
 * Vita Music Assistant - Home Tab implementation
 */

#include "view/home_tab.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

namespace vita_ma {

HomeTab::HomeTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Create vertical scrolling container for the entire tab
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);
    m_scrollView->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    m_scrollContent = new brls::Box();
    m_scrollContent->setAxis(brls::Axis::COLUMN);
    m_scrollContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_scrollContent->setAlignItems(brls::AlignItems::STRETCH);
    m_scrollContent->setPadding(20);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Home");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(20);
    m_scrollContent->addView(m_titleLabel);

    // Section rows are built dynamically from music/recommendations in loadContent().

    m_scrollView->setContentView(m_scrollContent);
    this->addView(m_scrollView);

    // Load content immediately
    brls::Logger::debug("HomeTab: Loading content...");
    loadContent();
}

HorizontalScrollRow* HomeTab::createMediaRow() {
    auto* row = new HorizontalScrollRow();
    row->setHeight(210);
    row->setMarginBottom(10);
    return row;
}

void HomeTab::populateRow(HorizontalScrollRow* row, const std::vector<MusicItem>& items, bool directPlay) {
    if (!row) return;

    row->clearViews();

    for (const auto& item : items) {
        auto* cell = new MediaItemCell();
        cell->setItem(item);
        cell->setMarginRight(10);

        MusicItem capturedItem = item;
        cell->registerClickAction([this, capturedItem, directPlay](brls::View* view) {
            if (directPlay) {
                // Play directly for recently played items
                Application::getInstance().pushPlayerActivity(capturedItem.itemId);
            } else {
                onItemSelected(capturedItem);
            }
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        // START opens a context menu for every item, dispatched by type (the
        // same mapping LibraryTab uses). Previously only artists and albums got
        // a menu, so tracks/playlists/stations had no Options action at all.
        cell->registerAction("Options", brls::ControllerButton::BUTTON_START,
            [capturedItem](brls::View* view) {
                switch (capturedItem.mediaType) {
                    case MediaType::ARTIST:
                        MediaDetailView::showArtistContextMenuStatic(capturedItem);
                        break;
                    case MediaType::TRACK:
                        MediaDetailView::performTrackActionStatic(capturedItem);
                        break;
                    default:  // albums, playlists, stations, podcasts, audiobooks
                        MediaDetailView::showAlbumContextMenuStatic(capturedItem);
                        break;
                }
                return true;
            });

        row->addView(cell);
    }

    // Add placeholder if empty
    if (items.empty()) {
        auto* placeholder = new brls::Label();
        placeholder->setText("No items");
        placeholder->setFontSize(16);
        placeholder->setMarginLeft(10);
        row->addView(placeholder);
    }
}

HomeTab::~HomeTab() {
    if (m_alive) { *m_alive = false; }
}

void HomeTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);
    // Invalidate alive flag so pending async callbacks bail out
    if (m_alive) *m_alive = false;
    ImageLoader::cancelAll();
    // Free image cache when leaving home tab to reclaim memory
    ImageLoader::clearCache();

    // Free stored item data to reduce baseline memory
    m_recentMusic.clear();
    m_recentMusic.shrink_to_fit();

    // Mark as not loaded so data is re-fetched when returning
    m_loaded = false;
}

void HomeTab::onFocusGained() {
    brls::Box::onFocusGained();
    // Re-create alive flag so new async callbacks work (old ones still bail out)
    m_alive = std::make_shared<bool>(true);

    if (!m_loaded) {
        loadContent();
    }
}

void HomeTab::loadContent() {
    brls::Logger::debug("HomeTab::loadContent - Starting async load");

    // Parse one MA media item object into a MusicItem.
    auto parseItem = [](const Json& obj) -> MusicItem {
        MusicItem item;
        item.itemId   = obj.has("item_id") ? obj["item_id"].str() : "";
        item.name     = obj.has("name")    ? obj["name"].str()    : "";
        item.uri      = obj.has("uri")     ? obj["uri"].str()     : "";
        item.provider = obj.has("provider")? obj["provider"].str(): "";

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

        std::string mt = obj.has("media_type") ? obj["media_type"].str() : "";
        if      (mt == "track")             item.mediaType = MediaType::TRACK;
        else if (mt == "album")             item.mediaType = MediaType::ALBUM;
        else if (mt == "artist")            item.mediaType = MediaType::ARTIST;
        else if (mt == "playlist")          item.mediaType = MediaType::PLAYLIST;
        else if (mt == "radio")             item.mediaType = MediaType::RADIO;
        else if (mt == "podcast")           item.mediaType = MediaType::PODCAST;
        else if (mt == "audiobook")         item.mediaType = MediaType::AUDIOBOOK;
        // Episodes/chapters are individually playable, like tracks.
        else if (mt == "podcast_episode")   item.mediaType = MediaType::TRACK;
        else if (mt == "audiobook_chapter") item.mediaType = MediaType::TRACK;
        // folder / flow_stream / anything new the server adds: don't guess it's
        // a track (that would try to play it) - mark UNKNOWN and handle safely.
        else                                item.mediaType = MediaType::UNKNOWN;

        if (obj.has("artist_name")) item.artistName = obj["artist_name"].str();
        if (obj.has("album_name"))  item.albumName  = obj["album_name"].str();
        if (obj.has("duration"))    item.duration   = obj["duration"].intVal();
        return item;
    };

    // MA's home content comes from music/recommendations: an array of folders,
    // each with a display name and a nested "items" array. (The previous code
    // called music/recently_played_items, which doesn't exist on this server,
    // so the home tab was always empty.)
    MAClient::instance().getRecommendations(
        [this, aliveWeak = std::weak_ptr<bool>(m_alive), parseItem](bool success, const Json& result) {
            if (!success) {
                brls::Logger::error("HomeTab: recommendations request failed");
                return;
            }

            std::vector<std::pair<std::string, std::vector<MusicItem>>> sections;
            if (result.type() == Json::ARRAY) {
                for (size_t i = 0; i < result.size(); i++) {
                    const Json& folder = result[i];
                    std::string title = folder.has("name") ? folder["name"].str() : "";
                    std::vector<MusicItem> items;
                    if (folder.has("items") && folder["items"].type() == Json::ARRAY) {
                        const Json& arr = folder["items"];
                        for (size_t j = 0; j < arr.size(); j++) items.push_back(parseItem(arr[j]));
                    } else if (folder.has("item_id")) {
                        // Not a folder — a bare media item; group under "Recommended".
                        items.push_back(parseItem(folder));
                    }
                    if (!items.empty())
                        sections.emplace_back(title.empty() ? "Recommended" : title, std::move(items));
                }
            }

            brls::Logger::info("HomeTab: recommendations -> {} sections", sections.size());

            brls::sync([this, sections, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                // Rebuild the scroll content: title + one labeled row per folder.
                m_scrollContent->clearViews();
                m_musicRow = nullptr;

                m_titleLabel = new brls::Label();
                m_titleLabel->setText("Home");
                m_titleLabel->setFontSize(28);
                m_titleLabel->setMarginBottom(20);
                m_scrollContent->addView(m_titleLabel);

                if (sections.empty()) {
                    auto* empty = new brls::Label();
                    empty->setText("Nothing to show yet");
                    empty->setFontSize(16);
                    m_scrollContent->addView(empty);
                    return;
                }

                for (const auto& sec : sections) {
                    auto* lbl = new brls::Label();
                    lbl->setText(sec.first);
                    lbl->setFontSize(22);
                    lbl->setMarginBottom(10);
                    lbl->setMarginTop(15);
                    m_scrollContent->addView(lbl);

                    auto* row = createMediaRow();
                    m_scrollContent->addView(row);
                    populateRow(row, sec.second, false);
                }
            });
        });

    m_loaded = true;
    brls::Logger::debug("HomeTab: Async content loading started");
}

void HomeTab::onItemSelected(const MusicItem& item) {
    switch (item.mediaType) {
        // Playable items (tracks, stations, episodes/chapters mapped to TRACK)
        // start immediately. A station has no track list, so opening a detail
        // view for it (which fetches album/artist/playlist tracks) came up
        // broken - play it instead.
        case MediaType::TRACK:
        case MediaType::RADIO:
            Application::getInstance().pushPlayerActivity(item.itemId);
            return;

        // Browsable containers open a detail view (its track/episode list).
        case MediaType::ARTIST:
        case MediaType::ALBUM:
        case MediaType::PLAYLIST:
        case MediaType::PODCAST:
        case MediaType::AUDIOBOOK: {
            auto* detailView = new MediaDetailView(item);
            brls::Application::pushActivity(new brls::Activity(detailView));
            return;
        }

        // Unrecognised type (e.g. a folder, or something the server adds
        // later): do nothing rather than play or open a broken page.
        case MediaType::UNKNOWN:
        default:
            brls::Logger::warning("HomeTab: unsupported item type for '{}', ignoring", item.name);
            return;
    }
}

} // namespace vita_ma
