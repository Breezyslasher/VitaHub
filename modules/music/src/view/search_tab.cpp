/**
 * Vita Music Assistant - Search Tab implementation
 *
 * Results are shown as home-style horizontal rails, one per media type, in a
 * fixed order. A horizontally-scrolling chip row above the rails multi-selects
 * which media types to search: none selected searches (and shows) every type,
 * otherwise only the selected type(s). The chip selection maps directly to the
 * search media_types parameter.
 */

#include "view/search_tab.hpp"
#include "view/media_detail_view.hpp"
#include "view/media_item_cell.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "utils/image_loader.hpp"

namespace vita_ma {

// Media types shown as chips and rails, in display order. jsonKey is the field
// name in the MA SearchResults response (note: radio results are under
// "radios"). api is the MediaType enum string passed in media_types.
namespace {
struct TypeDef {
    const char* label;
    MediaType   type;
    const char* api;
    const char* jsonKey;
};
const TypeDef TYPES[] = {
    {"Tracks",     MediaType::TRACK,     "track",     "tracks"},
    {"Albums",     MediaType::ALBUM,     "album",     "albums"},
    {"Artists",    MediaType::ARTIST,    "artist",    "artists"},
    {"Playlists",  MediaType::PLAYLIST,  "playlist",  "playlists"},
    {"Radio",      MediaType::RADIO,     "radio",     "radios"},
    {"Audiobooks", MediaType::AUDIOBOOK, "audiobook", "audiobooks"},
    {"Podcasts",   MediaType::PODCAST,   "podcast",   "podcasts"},
};
constexpr int NUM_TYPES = sizeof(TYPES) / sizeof(TYPES[0]);
} // namespace

// Helper: parse a media_type string to MediaType enum
static MediaType parseMediaType(const std::string& s) {
    if (s == "track")     return MediaType::TRACK;
    if (s == "album")     return MediaType::ALBUM;
    if (s == "artist")    return MediaType::ARTIST;
    if (s == "playlist")  return MediaType::PLAYLIST;
    if (s == "radio")     return MediaType::RADIO;
    if (s == "audiobook") return MediaType::AUDIOBOOK;
    if (s == "podcast")   return MediaType::PODCAST;
    return MediaType::UNKNOWN;
}

// Helper: parse a single JSON object into a MusicItem
static MusicItem musicItemFromJson(const Json& j) {
    MusicItem item;
    if (j.has("item_id"))    item.itemId    = j["item_id"].str();
    if (j.has("name"))       item.name      = j["name"].str();
    if (j.has("sort_name"))  item.sortName  = j["sort_name"].str();
    if (j.has("uri"))        item.uri       = j["uri"].str();
    // Extract image URL: try image object, then metadata.images array
    if (j.has("image") && j["image"].type() == Json::OBJECT && j["image"].has("path")) {
        item.imageUrl = MAClient::imageRefFromJson(j["image"]);
        if (j["image"].has("provider")) item.imageProvider = j["image"]["provider"].str();
    } else if (j.has("image") && j["image"].type() == Json::STRING) {
        item.imageUrl = j["image"].str();
    } else if (j.has("metadata") && j["metadata"].type() == Json::OBJECT) {
        const Json& meta = j["metadata"];
        if (meta.has("images") && meta["images"].type() == Json::ARRAY && meta["images"].size() > 0) {
            const Json& img = meta["images"][static_cast<size_t>(0)];
            item.imageUrl = MAClient::imageRefFromJson(img);
            if (img.has("provider")) item.imageProvider = img["provider"].str();
        }
    }

    if (j.has("media_type")) {
        item.mediaType = parseMediaType(j["media_type"].str());
    }

    // Track fields
    if (j.has("track_number")) item.trackNumber = j["track_number"].intVal();
    if (j.has("disc_number"))  item.discNumber  = j["disc_number"].intVal();
    if (j.has("duration"))     item.duration    = j["duration"].intVal();

    // Artist name - may be a string or nested in "artists" array
    if (j.has("artist")) {
        item.artistName = j["artist"].str();
    } else if (j.has("artists") && j["artists"].size() > 0) {
        const auto& firstArtist = j["artists"][static_cast<size_t>(0)];
        if (firstArtist.has("name")) {
            item.artistName = firstArtist["name"].str();
        }
    }

    if (j.has("album"))      item.albumName  = j["album"].str();
    if (j.has("year"))       item.year       = j["year"].intVal();
    if (j.has("version"))    item.version    = j["version"].str();
    if (j.has("subtype"))    item.subtype    = j["subtype"].str();
    if (j.has("biography"))  item.biography  = j["biography"].str();
    if (j.has("item_count")) item.itemCount  = j["item_count"].intVal();
    if (j.has("is_editable")) item.isEditable = j["is_editable"].boolVal();
    if (j.has("favorite"))   item.favorite   = j["favorite"].boolVal();
    if (j.has("provider"))   item.provider   = j["provider"].str();

    return item;
}

SearchTab::SearchTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Search");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(14);
    this->addView(m_titleLabel);

    // Search bar (tap to open the Vita IME)
    m_searchLabel = new brls::Label();
    m_searchLabel->setText("  Tap to search...");
    m_searchLabel->setFontSize(20);
    m_searchLabel->setMarginBottom(10);
    m_searchLabel->setFocusable(true);
    m_searchLabel->registerClickAction([this](brls::View* view) {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            m_searchQuery = text;
            m_searchLabel->setText(text.empty() ? "  Tap to search..."
                                                : std::string("  ") + text);
            performSearch(text);
        }, "Search", "Enter search query", 256, m_searchQuery);
        return true;
    });
    m_searchLabel->addGestureRecognizer(new brls::TapGestureRecognizer(m_searchLabel));
    this->addView(m_searchLabel);

    // Filter-chip row (multi-select media-type filter)
    buildChipRow();

    // Result count
    m_resultsLabel = new brls::Label();
    m_resultsLabel->setText("");
    m_resultsLabel->setFontSize(15);
    m_resultsLabel->setTextColor(nvgRGB(0xa9, 0xb4, 0xc0));
    m_resultsLabel->setMarginBottom(8);
    this->addView(m_resultsLabel);

    // Vertical scroll holding the rails
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);
    m_scrollView->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    m_scrollContent = new brls::Box();
    m_scrollContent->setAxis(brls::Axis::COLUMN);
    m_scrollContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_scrollContent->setAlignItems(brls::AlignItems::STRETCH);

    m_scrollView->setContentView(m_scrollContent);
    this->addView(m_scrollView);

    m_grouped.resize(NUM_TYPES);
}

SearchTab::~SearchTab() {
    if (m_alive) *m_alive = false;
}

void SearchTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);
    if (m_alive) *m_alive = false;
    m_loadGeneration++;
    ImageLoader::cancelAll();
    ImageLoader::clearCache();
}

void SearchTab::onFocusGained() {
    brls::Box::onFocusGained();
    m_alive = std::make_shared<bool>(true);
    if (m_searchLabel) {
        brls::Application::giveFocus(m_searchLabel);
    }
}

void SearchTab::styleChip(brls::Box* chip, brls::Label* label, bool selected) {
    if (!chip) return;
    if (selected) {
        chip->setBackgroundColor(nvgRGB(0x00, 0xbc, 0xee));
        chip->setBorderThickness(0.0f);
        if (label) label->setTextColor(nvgRGB(0x04, 0x22, 0x2e));
    } else {
        chip->setBackgroundColor(nvgRGB(0x16, 0x1b, 0x23));
        chip->setBorderColor(nvgRGB(0x28, 0x32, 0x40));
        chip->setBorderThickness(1.0f);
        if (label) label->setTextColor(nvgRGB(0xa9, 0xb4, 0xc0));
    }
}

void SearchTab::buildChipRow() {
    m_chipRow = new HorizontalScrollRow();
    m_chipRow->setHeight(44);
    m_chipRow->setMarginBottom(10);

    // Leading "In Library" source filter (separate from the media-type chips):
    // on = search the local library only, off = search every provider.
    m_libraryChip = new brls::Box();
    m_libraryChip->setAxis(brls::Axis::ROW);
    m_libraryChip->setJustifyContent(brls::JustifyContent::CENTER);
    m_libraryChip->setAlignItems(brls::AlignItems::CENTER);
    m_libraryChip->setHeight(32);
    m_libraryChip->setPaddingLeft(14);
    m_libraryChip->setPaddingRight(14);
    m_libraryChip->setMarginRight(14);
    m_libraryChip->setCornerRadius(16);
    m_libraryChip->setFocusable(true);
    m_libraryChipLabel = new brls::Label();
    m_libraryChipLabel->setText("In Library");
    m_libraryChipLabel->setFontSize(15);
    m_libraryChip->addView(m_libraryChipLabel);
    m_libraryChip->registerClickAction([this](brls::View* view) {
        toggleLibraryOnly();
        return true;
    });
    m_libraryChip->addGestureRecognizer(new brls::TapGestureRecognizer(m_libraryChip));
    styleChip(m_libraryChip, m_libraryChipLabel, /*selected*/ false);
    m_chipRow->addView(m_libraryChip);

    m_chips.clear();
    for (int i = 0; i < NUM_TYPES; i++) {
        auto* chip = new brls::Box();
        chip->setAxis(brls::Axis::ROW);
        chip->setJustifyContent(brls::JustifyContent::CENTER);
        chip->setAlignItems(brls::AlignItems::CENTER);
        chip->setHeight(32);
        chip->setPaddingLeft(14);
        chip->setPaddingRight(14);
        chip->setMarginRight(8);
        chip->setCornerRadius(16);
        chip->setFocusable(true);

        auto* label = new brls::Label();
        label->setText(TYPES[i].label);
        label->setFontSize(15);
        chip->addView(label);

        std::string api = TYPES[i].api;
        chip->registerClickAction([this, api](brls::View* view) {
            toggleType(api);
            return true;
        });
        chip->addGestureRecognizer(new brls::TapGestureRecognizer(chip));

        styleChip(chip, label, /*selected*/ false);
        m_chipRow->addView(chip);
        m_chips.push_back({chip, label, api});
    }

    this->addView(m_chipRow);
}

void SearchTab::toggleType(const std::string& apiType) {
    if (m_selectedTypes.count(apiType))
        m_selectedTypes.erase(apiType);
    else
        m_selectedTypes.insert(apiType);

    // Restyle chips to match the new selection.
    for (auto& c : m_chips) {
        styleChip(c.box, c.label, m_selectedTypes.count(c.apiType) > 0);
    }

    // The chip selection is the media_types filter: re-run the current query so
    // the server returns only the selected type(s) (or every type when none are
    // selected).
    if (!m_searchQuery.empty()) {
        performSearch(m_searchQuery);
    }
}

void SearchTab::toggleLibraryOnly() {
    m_libraryOnly = !m_libraryOnly;
    styleChip(m_libraryChip, m_libraryChipLabel, m_libraryOnly);
    if (!m_searchQuery.empty()) {
        performSearch(m_searchQuery);
    }
}

void SearchTab::performSearch(const std::string& query) {
    if (query.empty()) {
        m_resultsLabel->setText("");
        for (auto& g : m_grouped) g.clear();
        rebuildRails();
        return;
    }

    m_resultsLabel->setText("Searching...");

    // Build the media_types list from the selected chips (empty = all types).
    std::vector<std::string> mediaTypes(m_selectedTypes.begin(), m_selectedTypes.end());

    int gen = ++m_loadGeneration;
    std::weak_ptr<bool> aliveWeak = m_alive;

    // Larger page than the default so each rail has depth to scroll (the
    // official app uses 200; keep it lower for the Vita's memory).
    MAClient::instance().search(query, mediaTypes, m_libraryOnly,
        [this, gen, aliveWeak](bool success, const Json& result) {
        brls::sync([this, success, result, gen, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (gen != m_loadGeneration) return;  // stale result

            for (auto& g : m_grouped) g.clear();

            if (!success) {
                m_resultsLabel->setText("Search failed");
                rebuildRails();
                return;
            }

            // MA search returns an object of typed arrays; group each into its
            // rail slot, forcing the media type so cells route correctly.
            int total = 0;
            for (int i = 0; i < NUM_TYPES; i++) {
                if (!result.has(TYPES[i].jsonKey)) continue;
                const Json& arr = result[TYPES[i].jsonKey];
                if (arr.type() != Json::ARRAY) continue;
                for (size_t k = 0; k < arr.size(); k++) {
                    MusicItem item = musicItemFromJson(arr[k]);
                    item.mediaType = TYPES[i].type;
                    m_grouped[i].push_back(std::move(item));
                }
                total += (int)m_grouped[i].size();
            }

            m_resultsLabel->setText("Found " + std::to_string(total));
            rebuildRails();
        });
    }, 50);
}

void SearchTab::rebuildRails() {
    if (!m_scrollContent) return;
    m_scrollContent->clearViews();

    bool any = false;
    for (int i = 0; i < NUM_TYPES; i++) {
        if (m_grouped[i].empty()) continue;
        any = true;

        auto* lbl = new brls::Label();
        lbl->setText(TYPES[i].label);
        lbl->setFontSize(20);
        lbl->setMarginTop(12);
        lbl->setMarginBottom(8);
        m_scrollContent->addView(lbl);

        auto* row = new HorizontalScrollRow();
        row->setHeight(210);
        row->setMarginBottom(6);
        m_scrollContent->addView(row);
        populateRow(row, m_grouped[i]);
    }

    if (!any && !m_searchQuery.empty()) {
        auto* empty = new brls::Label();
        empty->setText("No results");
        empty->setFontSize(16);
        empty->setTextColor(nvgRGB(0x6b, 0x76, 0x84));
        empty->setMarginTop(12);
        m_scrollContent->addView(empty);
    }
}

void SearchTab::populateRow(HorizontalScrollRow* row, const std::vector<MusicItem>& items) {
    if (!row) return;
    row->clearViews();

    for (const auto& item : items) {
        auto* cell = new MediaItemCell();
        cell->setItem(item);
        cell->setMarginRight(10);

        MusicItem capturedItem = item;
        cell->registerClickAction([this, capturedItem](brls::View* view) {
            onItemSelected(capturedItem);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        // START opens a context menu, dispatched by type (same mapping the
        // home and library tabs use).
        cell->registerAction("Options", brls::ControllerButton::BUTTON_START,
            [capturedItem](brls::View* view) {
                switch (capturedItem.mediaType) {
                    case MediaType::ARTIST:
                        MediaDetailView::showArtistContextMenuStatic(capturedItem);
                        break;
                    case MediaType::TRACK:
                        MediaDetailView::performTrackActionStatic(capturedItem);
                        break;
                    default:
                        MediaDetailView::showAlbumContextMenuStatic(capturedItem);
                        break;
                }
                return true;
            });

        row->addView(cell);
    }
}

void SearchTab::onItemSelected(const MusicItem& item) {
    // Tracks and radio stations play immediately; browsable containers open a
    // detail view.
    if (item.mediaType == MediaType::TRACK || item.mediaType == MediaType::RADIO) {
        Application::getInstance().pushPlayerActivity(item.itemId);
        return;
    }
    auto* detailView = new MediaDetailView(item);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

} // namespace vita_ma
