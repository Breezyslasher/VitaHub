/**
 * Vita Music Assistant - Media Detail View implementation
 * Music-only: supports ARTIST, ALBUM, TRACK, PLAYLIST, RADIO types.
 */

#include "view/media_detail_view.hpp"
#include "view/media_item_cell.hpp"
#include "view/progress_dialog.hpp"
#include "activity/player_activity.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "app/music_queue.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include <thread>
#include <memory>
#include <atomic>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif

namespace vita_ma {

MediaDetailView::MediaDetailView(const MusicItem& item)
    : m_item(item), m_alive(std::make_shared<std::atomic<bool>>(true)) {

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Register back button (B/Circle) to pop this activity
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::Sound::SOUND_BACK);

    // Create scrollable content
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_mainContent = new brls::Box();
    m_mainContent->setAxis(brls::Axis::COLUMN);
    m_mainContent->setPadding(30);

    // Top row - poster and info
    auto* topRow = new brls::Box();
    topRow->setAxis(brls::Axis::ROW);
    topRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    topRow->setAlignItems(brls::AlignItems::FLEX_START);
    topRow->setMarginBottom(20);

    // Left side - poster (square album art for all music types)
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::COLUMN);
    leftBox->setWidth(200);
    leftBox->setMarginRight(30);

    auto* posterContainer = new brls::Box();
    posterContainer->setAxis(brls::Axis::COLUMN);
    posterContainer->setWidth(200);

    m_posterImage = new brls::Image();
    m_posterImage->setWidth(200);
    m_posterImage->setHeight(200);
    posterContainer->setHeight(200);
    m_posterImage->setScalingType(brls::ImageScalingType::FIT);
    m_posterImage->setVisibility(brls::Visibility::INVISIBLE);
    posterContainer->addView(m_posterImage);

    leftBox->addView(posterContainer);
    topRow->addView(leftBox);

    // Right side - details
    auto* rightBox = new brls::Box();
    rightBox->setAxis(brls::Axis::COLUMN);
    rightBox->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(m_item.name);
    m_titleLabel->setFontSize(26);
    m_titleLabel->setMarginBottom(10);
    rightBox->addView(m_titleLabel);

    // Metadata row
    auto* metaBox = new brls::Box();
    metaBox->setAxis(brls::Axis::ROW);
    metaBox->setMarginBottom(15);

    if (m_item.year > 0) {
        m_yearLabel = new brls::Label();
        m_yearLabel->setText(std::to_string(m_item.year));
        m_yearLabel->setFontSize(16);
        m_yearLabel->setMarginRight(15);
        metaBox->addView(m_yearLabel);
    }

    if (m_item.duration > 0) {
        m_durationLabel = new brls::Label();
        int minutes = m_item.duration / 60;
        m_durationLabel->setText(std::to_string(minutes) + " min");
        m_durationLabel->setFontSize(16);
        metaBox->addView(m_durationLabel);
    }

    rightBox->addView(metaBox);

    // Description/biography - shown for artists (biography) or general info
    if (m_item.mediaType == MediaType::ARTIST) {
        m_fullDescription = m_item.biography;
    }

    if (!m_fullDescription.empty()) {
        m_summaryScroll = new brls::ScrollingFrame();
        m_summaryScroll->setHeight(200);
        m_summaryScroll->setMarginBottom(20);

        m_summaryLabel = new brls::Label();
        m_summaryLabel->setFontSize(16);
        m_summaryLabel->setText(m_fullDescription);
        m_summaryLabel->setFocusable(true);

        m_summaryScroll->setContentView(m_summaryLabel);
        rightBox->addView(m_summaryScroll);
    }

    topRow->addView(rightBox);
    m_mainContent->addView(topRow);

    // Track list for albums (vertical list with nested scrolling)
    if (m_item.mediaType == MediaType::ALBUM) {
        auto* tracksLabel = new brls::Label();
        tracksLabel->setText("Tracks");
        tracksLabel->setFontSize(20);
        tracksLabel->setMarginBottom(10);
        m_mainContent->addView(tracksLabel);

        // Wrap track list in its own ScrollingFrame so only tracks scroll
        auto* trackScroll = new brls::ScrollingFrame();
        trackScroll->setGrow(1.0f);
        trackScroll->setMinHeight(200);

        m_trackListBox = new brls::Box();
        m_trackListBox->setAxis(brls::Axis::COLUMN);
        m_trackListBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        m_trackListBox->setAlignItems(brls::AlignItems::STRETCH);

        trackScroll->setContentView(m_trackListBox);
        m_mainContent->addView(trackScroll);
    }

    // Music categories container for artists - scrollable below fixed header
    if (m_item.mediaType == MediaType::ARTIST) {
        auto* categoriesScroll = new brls::ScrollingFrame();
        categoriesScroll->setGrow(1.0f);

        m_musicCategoriesBox = new brls::Box();
        m_musicCategoriesBox->setAxis(brls::Axis::COLUMN);

        categoriesScroll->setContentView(m_musicCategoriesBox);
        m_mainContent->addView(categoriesScroll);
    }

    if (m_item.mediaType == MediaType::ALBUM ||
        m_item.mediaType == MediaType::ARTIST) {
        // Top info is fixed, only media content below scrolls in its own container
        m_mainContent->setGrow(1.0f);
        this->addView(m_mainContent);
    } else {
        m_scrollView->setContentView(m_mainContent);
        this->addView(m_scrollView);
    }

    // Load full details
    loadDetails();
}

MediaDetailView::~MediaDetailView() {
    if (m_alive) {
        m_alive->store(false);
    }
    brls::Logger::debug("MediaDetailView: Destroyed");
}

brls::HScrollingFrame* MediaDetailView::createMediaRow(const std::string& title, brls::Box** contentOut) {
    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(20);
    label->setMarginBottom(10);
    label->setMarginTop(15);
    m_musicCategoriesBox->addView(label);

    auto* scrollFrame = new brls::HScrollingFrame();
    scrollFrame->setHeight(150);
    scrollFrame->setMarginBottom(10);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::ROW);
    content->setJustifyContent(brls::JustifyContent::FLEX_START);

    scrollFrame->setContentView(content);
    m_musicCategoriesBox->addView(scrollFrame);

    if (contentOut) {
        *contentOut = content;
    }

    return scrollFrame;
}

brls::View* MediaDetailView::create() {
    return nullptr; // Factory not used
}

void MediaDetailView::loadDetails() {
    std::string itemId = m_item.itemId;
    std::string imageUrl = m_item.imageUrl;
    MediaType mediaType = m_item.mediaType;

    // Load thumbnail
    if (m_posterImage && !imageUrl.empty()) {
        MAClient& client = MAClient::instance();
        std::string url = client.getThumbnailUrl(imageUrl, 400, 400, m_item.imageProvider);
        ImageLoader::loadAsync(url, [](brls::Image* image) {
            image->setVisibility(brls::Visibility::VISIBLE);
        }, m_posterImage, m_alive);
    }

    // TODO: fetchMediaDetails does not exist yet in new MA API.
    // For now we use the item data we already have from the list view.
    // When the MA API adds a getItem(itemId) -> full details method, call it here.

    // Load children if applicable
    if (m_item.mediaType == MediaType::ARTIST) {
        loadMusicCategories();
    } else if (m_item.mediaType == MediaType::ALBUM || m_item.mediaType == MediaType::PLAYLIST) {
        loadTrackList();
    }
}

void MediaDetailView::loadChildren() {
    // TODO: fetchChildren does not exist in new MA API.
    // Use getAlbumTracks / getArtistAlbums etc. via MAClient when needed.
}

void MediaDetailView::loadMusicCategories() {
    if (!m_musicCategoriesBox) return;

    std::string itemId = m_item.itemId;
    std::string provider = m_item.provider;
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    asyncRun([this, itemId, provider, aliveWeak]() {
        MAClient& client = MAClient::instance();

        // Fetch artist's albums via the new MA API. done/accumulator are heap
        // shared_ptrs captured by value so the main-thread callback can't touch
        // this worker's stack after a timeout (use-after-free / data abort).
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto allAlbumItemsPtr = std::make_shared<std::vector<MusicItem>>();
        auto& allAlbumItems = *allAlbumItemsPtr;

        client.getArtistAlbums(itemId, [done, allAlbumItemsPtr](bool success, const Json& result) {
            if (success && result.type() == Json::ARRAY) {
                for (size_t i = 0; i < result.size(); i++) {
                    const Json& item = result[i];
                    MusicItem mi;
                    mi.itemId = item.has("item_id") ? item["item_id"].str() : "";
                    mi.name = item.has("name") ? item["name"].str() : "";
                    // Extract image URL: try image object, then metadata.images array
                    if (item.has("image") && item["image"].type() == Json::OBJECT && item["image"].has("path")) {
                        mi.imageUrl = MAClient::imageRefFromJson(item["image"]);
                        if (item["image"].has("provider")) mi.imageProvider = item["image"]["provider"].str();
                    } else if (item.has("image") && item["image"].type() == Json::STRING) {
                        mi.imageUrl = item["image"].str();
                    } else if (item.has("metadata") && item["metadata"].type() == Json::OBJECT) {
                        const Json& meta = item["metadata"];
                        if (meta.has("images") && meta["images"].type() == Json::ARRAY && meta["images"].size() > 0) {
                            const Json& img = meta["images"][static_cast<size_t>(0)];
                            mi.imageUrl = MAClient::imageRefFromJson(img);
                            if (img.has("provider")) mi.imageProvider = img["provider"].str();
                        }
                    }
                    mi.mediaType = MediaType::ALBUM;
                    mi.year = item.has("year") ? item["year"].intVal() : 0;
                    mi.subtype = item.has("album_type") ? item["album_type"].str() :
                                 (item.has("subtype") ? item["subtype"].str() : "");
                    mi.artistName = item.has("artist") ? item["artist"].str() :
                                    (item.has("artists") && item["artists"].size() > 0 ?
                                     item["artists"][static_cast<size_t>(0)].has("name") ?
                                     item["artists"][static_cast<size_t>(0)]["name"].str() : "" : "");
                    mi.provider = item.has("provider") ? item["provider"].str() : "";
                    mi.uri = item.has("uri") ? item["uri"].str() : "";
                    allAlbumItemsPtr->push_back(mi);
                }
            }
            done->store(true);
        }, provider);

        // Wait for async response (simplified blocking wait for Vita)
        // TODO: Refactor to fully async callback chain
        int waitMs = 0;
        while (!done->load() && waitMs < 10000) {
#ifdef __vita__
            sceKernelDelayThread(50 * 1000);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
            waitMs += 50;
        }

        brls::sync([this, allAlbumItems, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !alive->load()) return;

            m_musicCategoriesBox->clearViews();

            // Group albums by subtype
            std::vector<MusicItem> albums, singles, eps, compilations, soundtracks, other;
            for (const auto& item : allAlbumItems) {
                std::string subtype = item.subtype;
                for (char& c : subtype) c = tolower(c);

                if (subtype == "single") {
                    singles.push_back(item);
                } else if (subtype == "ep") {
                    eps.push_back(item);
                } else if (subtype == "compilation") {
                    compilations.push_back(item);
                } else if (subtype == "soundtrack") {
                    soundtracks.push_back(item);
                } else if (subtype == "album" || subtype.empty()) {
                    albums.push_back(item);
                } else {
                    other.push_back(item);
                }
            }

            auto addCategory = [this](const std::string& title, const std::vector<MusicItem>& items) {
                if (items.empty()) return;

                brls::Box* content = nullptr;
                createMediaRow(title + " (" + std::to_string(items.size()) + ")", &content);

                for (const auto& item : items) {
                    auto* cell = new MediaItemCell();
                    cell->setItem(item);
                    cell->setMarginRight(10);

                    MusicItem capturedItem = item;
                    cell->registerClickAction([this, capturedItem](brls::View* view) {
                        auto* detailView = new MediaDetailView(capturedItem);
                        brls::Application::pushActivity(new brls::Activity(detailView));
                        return true;
                    });
                    cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

                    cell->registerAction("Options", brls::ControllerButton::BUTTON_START, [this, capturedItem](brls::View* view) {
                        showAlbumContextMenu(capturedItem);
                        return true;
                    });

                    content->addView(cell);
                }
            };

            addCategory("Albums", albums);
            addCategory("Singles", singles);
            addCategory("EPs", eps);
            addCategory("Compilations", compilations);
            addCategory("Soundtracks", soundtracks);
            addCategory("Other", other);

            // Focus setup: if no description, transfer focus to first category item
            if (m_fullDescription.empty() && m_musicCategoriesBox &&
                !m_musicCategoriesBox->getChildren().empty()) {
                for (auto* child : m_musicCategoriesBox->getChildren()) {
                    auto* hScroll = dynamic_cast<brls::HScrollingFrame*>(child);
                    if (hScroll) {
                        brls::Application::giveFocus(hScroll);
                        break;
                    }
                }
            }
        });
    });
}

void MediaDetailView::onPlay(bool resume) {
    if (m_item.mediaType == MediaType::TRACK) {
        std::vector<MusicItem> single = {m_item};
        playTracksOnSelectedPlayer(single, "play");
    }
    // For albums and playlists, play loaded children tracks
    else if (m_item.mediaType == MediaType::ALBUM || m_item.mediaType == MediaType::PLAYLIST) {
        if (!m_children.empty()) {
            playTracksOnSelectedPlayer(m_children, "play");
        }
    }
}

void MediaDetailView::loadTrackList() {
    if (!m_trackListBox) return;

    std::string itemId = m_item.itemId;
    std::string provider = m_item.provider;
    std::weak_ptr<std::atomic<bool>> aliveWeak = m_alive;

    bool isPlaylist = (m_item.mediaType == MediaType::PLAYLIST);
    asyncRun([this, itemId, provider, aliveWeak, isPlaylist]() {
        MAClient& client = MAClient::instance();

        // Heap shared_ptrs captured by value so the main-thread callback never
        // writes into this worker's stack after a timeout (data-abort UAF).
        auto done = std::make_shared<std::atomic<bool>>(false);
        auto tracksPtr = std::make_shared<std::vector<MusicItem>>();
        auto& tracks = *tracksPtr;

        auto tracksCb = [done, tracksPtr](bool success, const Json& result) {
            if (success && result.type() == Json::ARRAY) {
                for (size_t i = 0; i < result.size(); i++) {
                    const Json& item = result[i];
                    MusicItem mi;
                    mi.itemId = item.has("item_id") ? item["item_id"].str() : "";
                    mi.name = item.has("name") ? item["name"].str() : "";
                    // Extract image URL: try image object, then metadata.images array
                    if (item.has("image") && item["image"].type() == Json::OBJECT && item["image"].has("path")) {
                        mi.imageUrl = MAClient::imageRefFromJson(item["image"]);
                        if (item["image"].has("provider")) mi.imageProvider = item["image"]["provider"].str();
                    } else if (item.has("image") && item["image"].type() == Json::STRING) {
                        mi.imageUrl = item["image"].str();
                    } else if (item.has("metadata") && item["metadata"].type() == Json::OBJECT) {
                        const Json& meta = item["metadata"];
                        if (meta.has("images") && meta["images"].type() == Json::ARRAY && meta["images"].size() > 0) {
                            const Json& img = meta["images"][static_cast<size_t>(0)];
                            mi.imageUrl = MAClient::imageRefFromJson(img);
                            if (img.has("provider")) mi.imageProvider = img["provider"].str();
                        }
                    }
                    mi.mediaType = MediaType::TRACK;
                    mi.trackNumber = item.has("track_number") ? item["track_number"].intVal() : 0;
                    mi.discNumber = item.has("disc_number") ? item["disc_number"].intVal() : 0;
                    mi.duration = item.has("duration") ? item["duration"].intVal() : 0;
                    mi.artistName = item.has("artist") ? item["artist"].str() :
                                    (item.has("artists") && item["artists"].size() > 0 ?
                                     item["artists"][static_cast<size_t>(0)].has("name") ?
                                     item["artists"][static_cast<size_t>(0)]["name"].str() : "" : "");
                    mi.albumName = item.has("album") ? item["album"].str() : "";
                    mi.uri = item.has("uri") ? item["uri"].str() : "";
                    mi.provider = item.has("provider") ? item["provider"].str() : "";
                    tracksPtr->push_back(mi);
                }
            }
            done->store(true);
        };

        if (isPlaylist) {
            client.getPlaylistTracks(itemId, tracksCb, 0, provider);
        } else {
            client.getAlbumTracks(itemId, tracksCb, provider);
        }

        // Wait for async response
        // TODO: Refactor to fully async callback chain
        int waitMs = 0;
        while (!done->load() && waitMs < 10000) {
#ifdef __vita__
            sceKernelDelayThread(50 * 1000);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
            waitMs += 50;
        }

        brls::sync([this, tracks, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !alive->load()) return;

            m_trackListBox->clearViews();
            m_children = tracks;

            for (size_t i = 0; i < tracks.size(); i++) {
                const auto& track = tracks[i];

                // Create a row for each track
                auto* row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
                row->setAlignItems(brls::AlignItems::CENTER);
                row->setHeight(56);
                row->setPadding(10, 16, 10, 16);
                row->setMarginBottom(4);
                row->setCornerRadius(8);
                row->setBackgroundColor(nvgRGBA(50, 50, 60, 200));
                row->setFocusable(true);

                // Left side: track number + title
                auto* leftBox = new brls::Box();
                leftBox->setAxis(brls::Axis::ROW);
                leftBox->setAlignItems(brls::AlignItems::CENTER);
                leftBox->setGrow(1.0f);

                auto* trackNum = new brls::Label();
                trackNum->setFontSize(14);
                trackNum->setMarginRight(12);
                trackNum->setTextColor(nvgRGBA(150, 150, 150, 255));
                if (track.trackNumber > 0) {
                    trackNum->setText(std::to_string(track.trackNumber));
                } else {
                    trackNum->setText(std::to_string(i + 1));
                }
                leftBox->addView(trackNum);

                auto* titleLabel = new brls::Label();
                titleLabel->setFontSize(14);
                titleLabel->setText(track.name);
                leftBox->addView(titleLabel);

                row->addView(leftBox);

                // Right side: button hint + duration
                auto* rightSide = new brls::Box();
                rightSide->setAxis(brls::Axis::ROW);
                rightSide->setAlignItems(brls::AlignItems::CENTER);

                auto* hintIcon = new brls::Image();
                hintIcon->setImageFromRes("images/square_button.png");
                hintIcon->setWidth(16);
                hintIcon->setHeight(16);
                hintIcon->setMarginRight(2);
                hintIcon->setVisibility(brls::Visibility::INVISIBLE);
                rightSide->addView(hintIcon);

                auto* hintLabel = new brls::Label();
                hintLabel->setFontSize(10);
                hintLabel->setTextColor(nvgRGBA(150, 150, 180, 180));
                hintLabel->setText("Options");
                hintLabel->setMarginRight(10);
                hintLabel->setVisibility(brls::Visibility::INVISIBLE);
                rightSide->addView(hintLabel);

                // Show hint on focus, hide previous
                brls::Image* capturedHintIcon = hintIcon;
                brls::Label* capturedHintLabel = hintLabel;
                row->getFocusEvent()->subscribe([this, capturedHintIcon, capturedHintLabel](brls::View*) {
                    // Hide previously focused hint
                    if (m_currentFocusedHint && m_currentFocusedHint != capturedHintIcon) {
                        m_currentFocusedHint->setVisibility(brls::Visibility::INVISIBLE);
                    }
                    if (m_currentFocusedHintLabel && m_currentFocusedHintLabel != capturedHintLabel) {
                        m_currentFocusedHintLabel->setVisibility(brls::Visibility::INVISIBLE);
                    }
                    // Show current hint
                    capturedHintIcon->setVisibility(brls::Visibility::VISIBLE);
                    capturedHintLabel->setVisibility(brls::Visibility::VISIBLE);
                    m_currentFocusedHint = capturedHintIcon;
                    m_currentFocusedHintLabel = capturedHintLabel;
                });

                if (track.duration > 0) {
                    auto* durLabel = new brls::Label();
                    durLabel->setFontSize(12);
                    durLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
                    int min = track.duration / 60;
                    int sec = track.duration % 60;
                    char durStr[16];
                    snprintf(durStr, sizeof(durStr), "%d:%02d", min, sec);
                    durLabel->setText(durStr);
                    rightSide->addView(durLabel);
                }

                row->addView(rightSide);

                // Click to perform default track action
                MusicItem capturedTrack = track;
                row->registerClickAction([this, capturedTrack, i](brls::View* view) {
                    performTrackAction(capturedTrack, i);
                    return true;
                });
                row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

                // START button always shows track action dialog
                row->registerAction("Options", brls::ControllerButton::BUTTON_START, [this, capturedTrack, i](brls::View* view) {
                    showTrackActionDialog(capturedTrack, i);
                    return true;
                });

                m_trackListBox->addView(row);
            }

            // Set up focus transfer for album track list
            if (!m_trackListBox->getChildren().empty()) {
                brls::View* firstTrack = m_trackListBox->getChildren().front();

                if (m_summaryLabel && !m_fullDescription.empty()) {
                    // Description exists: DOWN from description goes to first track
                    m_summaryLabel->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstTrack);
                    // UP from tracks goes to description
                    for (auto* child : m_trackListBox->getChildren()) {
                        child->setCustomNavigationRoute(brls::FocusDirection::UP, m_summaryLabel);
                    }
                } else {
                    // No description: transfer focus to first track to avoid focus errors
                    brls::Application::giveFocus(firstTrack);
                }
            }
        });
    });
}

void MediaDetailView::performTrackAction(const MusicItem& track, size_t trackIndex) {
    TrackDefaultAction action = Application::getInstance().getSettings().trackDefaultAction;

    if (action == TrackDefaultAction::ASK_EACH_TIME) {
        showTrackActionDialog(track, trackIndex);
        return;
    }

    std::vector<MusicItem> single = {track};

    switch (action) {
        case TrackDefaultAction::PLAY_NEXT:
            playTracksOnSelectedPlayer(single, "next");
            break;

        case TrackDefaultAction::PLAY_NOW_REPLACE:
            playTracksOnSelectedPlayer(single, "play");
            break;

        case TrackDefaultAction::ADD_TO_BOTTOM:
            playTracksOnSelectedPlayer(single, "add");
            break;

        case TrackDefaultAction::PLAY_NOW_CLEAR:
        default:
            playTracksOnSelectedPlayer(single, "play");
            break;
    }
}

// ---------------------------------------------------------------------------
// Compact options popover (the START context menu), ported 1:1 from Vita_plex
// (artboard "D4a"): centered 320px dark panel over a scrim, context line +
// title header, hairline divider, 44px icon rows.

// Palette literals scoped to this component (matches artboard "D4a").
namespace popcol {
    // panel = the app shell surface, line = the sidebar separator hairline.
    inline NVGcolor panel()     { return nvgRGB(50, 50, 50); }
    inline NVGcolor line()      { return nvgRGB(67, 67, 74); }
    inline NVGcolor text()      { return nvgRGB(255, 255, 255); }
    inline NVGcolor muted()     { return nvgRGB(0xA8, 0xA6, 0xB4); }
    inline NVGcolor dim()       { return nvgRGB(0x80, 0x7E, 0x8C); }
    inline NVGcolor gold()      { return nvgRGB(0xE5, 0xA0, 0x0D); }
    inline NVGcolor scrim()     { return nvgRGBA(10, 9, 14, 128); }
}

// Translucent host so the underlying screen shows through the scrim.
class PopoverActivity : public brls::Activity {
public:
    explicit PopoverActivity(brls::Box* content) : brls::Activity(content) {}
    bool isTranslucent() override { return true; }
};

void MediaDetailView::showOptionsPopover(brls::View* anchor,
                                         const std::string& contextLine,
                                         const std::string& title,
                                         std::vector<OptionRow> rows) {
    namespace pc = popcol;

    // ── Geometry ────────────────────────────────────────────────────────
    // Centered, audio-picker style. The anchor is not used for positioning —
    // every menu opens centered.
    const float screenW = brls::Application::contentWidth;
    const float screenH = brls::Application::contentHeight;
    const float kPopoverW = 320.0f;
    const float kMargin   = 40.0f;
    (void)anchor;

    // ── Scrim (full-screen, centers the panel) ──────────────────────────
    auto* scrim = new brls::Box();
    scrim->setAxis(brls::Axis::COLUMN);
    scrim->setWidthPercentage(100.0f);
    scrim->setHeightPercentage(100.0f);
    scrim->setJustifyContent(brls::JustifyContent::CENTER);
    scrim->setAlignItems(brls::AlignItems::CENTER);
    scrim->setBackgroundColor(pc::scrim());
    scrim->addGestureRecognizer(new brls::TapGestureRecognizer(scrim,
        []() { brls::Application::popActivity(); }));

    // ── Popover panel ───────────────────────────────────────────────────
    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setBackgroundColor(pc::panel());
    panel->setBorderColor(pc::line());
    panel->setBorderThickness(1.0f);
    panel->setShadowType(brls::ShadowType::GENERIC);
    panel->setPadding(8.0f, 8.0f, 8.0f, 8.0f);
    panel->setCornerRadius(14.0f);
    // Fixed 320px, clamped on screens too narrow to hold it with margins.
    float panelW = kPopoverW;
    if (panelW + 2.0f * kMargin > screenW) panelW = screenW - 2.0f * kMargin;
    panel->setWidth(panelW);

    // ── Header (context line + title) ───────────────────────────────────
    // borealis only supports a uniform border, so the bottom rule under the
    // header is a separate 1px divider box rather than a per-side border.
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::COLUMN);
    header->setPadding(8.0f, 10.0f, 11.0f, 10.0f);

    if (!contextLine.empty()) {
        auto* ctx = new brls::Label();
        ctx->setText(contextLine);
        ctx->setFontSize(11.0f);
        ctx->setTextColor(pc::dim());
        ctx->setSingleLine(true);
        ctx->setMarginBottom(2.0f);
        header->addView(ctx);
    }
    auto* titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(16.0f);
    titleLabel->setTextColor(pc::text());
    titleLabel->setSingleLine(true);
    header->addView(titleLabel);
    panel->addView(header);

    auto* divider = new brls::Box();
    divider->setHeight(1.0f);
    divider->setAlignSelf(brls::AlignSelf::STRETCH);
    divider->setBackgroundColor(pc::line());
    divider->setMarginBottom(6.0f);
    panel->addView(divider);

    // Only a genuinely long menu (e.g. Add to Playlist with many playlists)
    // scrolls; the common short context menus add their rows straight to the
    // panel and render centered, exactly as before.
    brls::Box* rowsParent = panel;
    {
        const float availForRows = screenH - 2.0f * kMargin - 90.0f;  // minus header/divider/padding
        const float wantRows = static_cast<float>(rows.size()) * 44.0f;
        if (wantRows > availForRows && availForRows > 132.0f) {
            auto* rowsBox = new brls::Box();
            rowsBox->setAxis(brls::Axis::COLUMN);
            auto* scrollFrame = new brls::ScrollingFrame();
            scrollFrame->setContentView(rowsBox);
            scrollFrame->setHeight(availForRows);
            panel->addView(scrollFrame);
            rowsParent = rowsBox;
        }
    }

    // ── Rows ────────────────────────────────────────────────────────────
    brls::View* defaultFocus = nullptr;
    brls::View* firstRow      = nullptr;
    for (auto& r : rows) {
        OptionRow row = r;  // copy into the row closure

        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setAlignItems(brls::AlignItems::CENTER);
        rowBox->setHeight(44.0f);
        rowBox->setPadding(0.0f, 12.0f, 0.0f, 12.0f);
        rowBox->setCornerRadius(9.0f);
        rowBox->setFocusable(true);
        rowBox->setHighlightCornerRadius(9.0f);

        // Leading icon.
        auto* img = new brls::Image();
        if (!row.icon.empty()) img->setImageFromRes("icons/" + row.icon);
        img->setScalingType(brls::ImageScalingType::FIT);
        img->setWidth(20.0f);
        img->setHeight(20.0f);
        img->setMarginRight(11.0f);
        rowBox->addView(img);

        // Label.
        auto* lbl = new brls::Label();
        lbl->setText(row.label);
        lbl->setFontSize(15.0f);
        lbl->setSingleLine(true);
        lbl->setGrow(1.0f);
        if (row.danger) lbl->setTextColor(pc::muted());
        else            lbl->setTextColor(pc::text());
        rowBox->addView(lbl);

        // Trailing mono sub-value.
        if (!row.sub.empty()) {
            auto* sub = new brls::Label();
            sub->setText(row.sub);
            sub->setFontSize(12.0f);
            sub->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            sub->setSingleLine(true);
            sub->setMarginLeft(8.0f);
            sub->setTextColor(pc::dim());
            rowBox->addView(sub);
        }

        // Activate: dismiss the popover first (preserving the old
        // dialog->dismiss() ordering), then run the verbatim action body.
        auto act = row.action;
        auto onActivate = [act](brls::View* v) -> bool {
            brls::Application::popActivity(brls::TransitionAnimation::FADE,
                [act, v]() { if (act) act(v); });
            return true;
        };
        rowBox->registerClickAction(onActivate);
        rowBox->addGestureRecognizer(new brls::TapGestureRecognizer(rowBox));

        rowsParent->addView(rowBox);
        if (!firstRow) firstRow = rowBox;
        if (row.primary && !defaultFocus) defaultFocus = rowBox;
    }
    if (!defaultFocus) defaultFocus = firstRow;

    scrim->addView(panel);

    scrim->registerAction("Back", brls::ControllerButton::BUTTON_B,
        [](brls::View*) { brls::Application::popActivity(); return true; });

    brls::Application::pushActivity(new PopoverActivity(scrim));
    if (defaultFocus) brls::Application::giveFocus(defaultFocus);
}

void MediaDetailView::showTrackActionDialog(const MusicItem& track, size_t trackIndex) {
    (void)trackIndex;
    brls::View* anchor = brls::Application::getCurrentFocus();
    MusicItem capturedTrack = track;

    std::vector<OptionRow> rows;

    rows.push_back({ "play.png", "Play Now (Clear Queue)", "", true, false,
        [capturedTrack](brls::View*) {
        std::vector<MusicItem> single = {capturedTrack};
        playTracksOnSelectedPlayer(single, "play");
        return true;
    }});

    rows.push_back({ "skip-next.png", "Play Next", "", false, false,
        [capturedTrack](brls::View*) {
        std::vector<MusicItem> single = {capturedTrack};
        playTracksOnSelectedPlayer(single, "next");
        return true;
    }});

    rows.push_back({ "format-list-group.png", "Add to Bottom of Queue", "", false, false,
        [capturedTrack](brls::View*) {
        std::vector<MusicItem> single = {capturedTrack};
        playTracksOnSelectedPlayer(single, "add");
        return true;
    }});

    rows.push_back({ "book-multiple.png", "Add to Playlist", "", false, false,
        [](brls::View*) {
        brls::Application::notify("Add to playlist: not yet implemented");
        return true;
    }});

    rows.push_back({ "cross.png", "Cancel", "", false, true,
        [](brls::View*) {
        return true;
    }});

    showOptionsPopover(anchor, "TRACK", track.name, std::move(rows));
}

void MediaDetailView::showAlbumContextMenu(const MusicItem& album) {
    showAlbumContextMenuStatic(album);
}

void MediaDetailView::showArtistContextMenuStatic(const MusicItem& artist) {
    brls::View* anchor = brls::Application::getCurrentFocus();
    MusicItem capturedArtist = artist;

    std::vector<OptionRow> rows;

    // Shuffle Artist - fetch all tracks and shuffle
    rows.push_back({ "shuffle-variant.png", "Shuffle Artist", "", true, false,
        [capturedArtist](brls::View*) {
        asyncRun([capturedArtist]() {
            MAClient& client = MAClient::instance();

            auto done = std::make_shared<std::atomic<bool>>(false);
            auto allTracksPtr = std::make_shared<std::vector<MusicItem>>();
            auto& allTracks = *allTracksPtr;

            client.getArtistTracks(capturedArtist.itemId, [done, allTracksPtr, capturedArtist](bool success, const Json& result) {
                if (success && result.type() == Json::ARRAY) {
                    for (size_t i = 0; i < result.size(); i++) {
                        const Json& item = result[i];
                        MusicItem mi;
                        mi.itemId = item.has("item_id") ? item["item_id"].str() : "";
                        mi.name = item.has("name") ? item["name"].str() : "";
                        // Extract image URL: try image object, then metadata.images array
                        if (item.has("image") && item["image"].type() == Json::OBJECT && item["image"].has("path")) {
                            mi.imageUrl = MAClient::imageRefFromJson(item["image"]);
                            if (item["image"].has("provider")) mi.imageProvider = item["image"]["provider"].str();
                        } else if (item.has("image") && item["image"].type() == Json::STRING) {
                            mi.imageUrl = item["image"].str();
                        } else if (item.has("metadata") && item["metadata"].type() == Json::OBJECT) {
                            const Json& meta = item["metadata"];
                            if (meta.has("images") && meta["images"].type() == Json::ARRAY && meta["images"].size() > 0) {
                                const Json& img = meta["images"][static_cast<size_t>(0)];
                                mi.imageUrl = MAClient::imageRefFromJson(img);
                                if (img.has("provider")) mi.imageProvider = img["provider"].str();
                            }
                        }
                        mi.mediaType = MediaType::TRACK;
                        mi.duration = item.has("duration") ? item["duration"].intVal() : 0;
                        mi.uri = item.has("uri") ? item["uri"].str() : "";
                        mi.artistName = capturedArtist.name;
                        allTracksPtr->push_back(mi);
                    }
                }
                done->store(true);
            }, capturedArtist.provider);

            int waitMs = 0;
            while (!done->load() && waitMs < 10000) {
#ifdef __vita__
                sceKernelDelayThread(50 * 1000);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
                waitMs += 50;
            }

            if (allTracks.empty()) {
                brls::sync([]() { brls::Application::notify("No tracks found"); });
                return;
            }

            // Shuffle
            srand((unsigned)time(nullptr));
            for (size_t i = allTracks.size() - 1; i > 0; i--) {
                size_t j = rand() % (i + 1);
                std::swap(allTracks[i], allTracks[j]);
            }

            brls::sync([allTracks]() {
                playTracksOnSelectedPlayer(allTracks, "play");
            });
        });
        return true;
    }});

    // Play All (in order)
    rows.push_back({ "play.png", "Play All", "", false, false,
        [capturedArtist](brls::View*) {
        asyncRun([capturedArtist]() {
            MAClient& client = MAClient::instance();

            auto done = std::make_shared<std::atomic<bool>>(false);
            auto allTracksPtr = std::make_shared<std::vector<MusicItem>>();
            auto& allTracks = *allTracksPtr;

            client.getArtistTracks(capturedArtist.itemId, [done, allTracksPtr, capturedArtist](bool success, const Json& result) {
                if (success && result.type() == Json::ARRAY) {
                    for (size_t i = 0; i < result.size(); i++) {
                        const Json& item = result[i];
                        MusicItem mi;
                        mi.itemId = item.has("item_id") ? item["item_id"].str() : "";
                        mi.name = item.has("name") ? item["name"].str() : "";
                        // Extract image URL: try image object, then metadata.images array
                        if (item.has("image") && item["image"].type() == Json::OBJECT && item["image"].has("path")) {
                            mi.imageUrl = MAClient::imageRefFromJson(item["image"]);
                            if (item["image"].has("provider")) mi.imageProvider = item["image"]["provider"].str();
                        } else if (item.has("image") && item["image"].type() == Json::STRING) {
                            mi.imageUrl = item["image"].str();
                        } else if (item.has("metadata") && item["metadata"].type() == Json::OBJECT) {
                            const Json& meta = item["metadata"];
                            if (meta.has("images") && meta["images"].type() == Json::ARRAY && meta["images"].size() > 0) {
                                const Json& img = meta["images"][static_cast<size_t>(0)];
                                mi.imageUrl = MAClient::imageRefFromJson(img);
                                if (img.has("provider")) mi.imageProvider = img["provider"].str();
                            }
                        }
                        mi.mediaType = MediaType::TRACK;
                        mi.duration = item.has("duration") ? item["duration"].intVal() : 0;
                        mi.uri = item.has("uri") ? item["uri"].str() : "";
                        mi.artistName = capturedArtist.name;
                        allTracksPtr->push_back(mi);
                    }
                }
                done->store(true);
            }, capturedArtist.provider);

            int waitMs = 0;
            while (!done->load() && waitMs < 10000) {
#ifdef __vita__
                sceKernelDelayThread(50 * 1000);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
                waitMs += 50;
            }

            if (allTracks.empty()) {
                brls::sync([]() { brls::Application::notify("No tracks found"); });
                return;
            }

            brls::sync([allTracks]() {
                playTracksOnSelectedPlayer(allTracks, "play");
            });
        });
        return true;
    }});

    rows.push_back({ "cross.png", "Cancel", "", false, true,
        [](brls::View*) {
        return true;
    }});

    showOptionsPopover(anchor, "ARTIST", artist.name, std::move(rows));
}

void MediaDetailView::showAlbumContextMenuStatic(const MusicItem& album) {
    brls::View* anchor = brls::Application::getCurrentFocus();
    MusicItem capturedAlbum = album;

    // Helper: fetch tracks for an album or playlist, then play with given option
    auto fetchAndPlay = [](MusicItem item, std::string option) {
        asyncRun([item, option]() {
            MAClient& client = MAClient::instance();

            // done/tracks are heap-allocated and captured BY VALUE (shared_ptr)
            // so the response callback - which fires on the main thread - never
            // touches this worker's stack. Capturing them by reference and then
            // busy-waiting was a use-after-free: if the response arrived after
            // the 10s timeout (e.g. a busy server), push_back wrote into a freed
            // stack frame and the app crashed with a data abort.
            auto done = std::make_shared<std::atomic<bool>>(false);
            auto tracks = std::make_shared<std::vector<MusicItem>>();

            auto tracksCb = [done, tracks](bool success, const Json& result) {
                if (success && result.type() == Json::ARRAY) {
                    for (size_t i = 0; i < result.size(); i++) {
                        const Json& r = result[i];
                        MusicItem mi;
                        mi.itemId = r.has("item_id") ? r["item_id"].str() : "";
                        mi.name = r.has("name") ? r["name"].str() : "";
                        if (r.has("image") && r["image"].type() == Json::OBJECT && r["image"].has("path")) {
                            mi.imageUrl = MAClient::imageRefFromJson(r["image"]);
                            if (r["image"].has("provider")) mi.imageProvider = r["image"]["provider"].str();
                        } else if (r.has("image") && r["image"].type() == Json::STRING) {
                            mi.imageUrl = r["image"].str();
                        } else if (r.has("metadata") && r["metadata"].type() == Json::OBJECT) {
                            const Json& meta = r["metadata"];
                            if (meta.has("images") && meta["images"].type() == Json::ARRAY && meta["images"].size() > 0) {
                                const Json& img = meta["images"][static_cast<size_t>(0)];
                                mi.imageUrl = MAClient::imageRefFromJson(img);
                                if (img.has("provider")) mi.imageProvider = img["provider"].str();
                            }
                        }
                        mi.mediaType = MediaType::TRACK;
                        mi.duration = r.has("duration") ? r["duration"].intVal() : 0;
                        mi.uri = r.has("uri") ? r["uri"].str() : "";
                        mi.provider = r.has("provider") ? r["provider"].str() : "";
                        mi.artistName = r.has("artist") ? r["artist"].str() :
                            (r.has("artists") && r["artists"].size() > 0 &&
                             r["artists"][static_cast<size_t>(0)].has("name") ?
                             r["artists"][static_cast<size_t>(0)]["name"].str() : "");
                        tracks->push_back(mi);
                    }
                }
                done->store(true);
            };

            if (item.mediaType == MediaType::PLAYLIST) {
                client.getPlaylistTracks(item.itemId, tracksCb, 0, item.provider);
            } else {
                client.getAlbumTracks(item.itemId, tracksCb, item.provider);
            }

            int waitMs = 0;
            while (!done->load() && waitMs < 10000) {
#ifdef __vita__
                sceKernelDelayThread(50 * 1000);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
                waitMs += 50;
            }

            if (!tracks->empty()) {
                brls::sync([tracks, option]() {
                    playTracksOnSelectedPlayer(*tracks, option);
                });
            }
        });
    };

    std::vector<OptionRow> rows;

    rows.push_back({ "play.png", "Play Now (Clear Queue)", "", true, false,
        [capturedAlbum, fetchAndPlay](brls::View*) {
        fetchAndPlay(capturedAlbum, "play");
        return true;
    }});

    rows.push_back({ "skip-next.png", "Play Next", "", false, false,
        [capturedAlbum, fetchAndPlay](brls::View*) {
        fetchAndPlay(capturedAlbum, "next");
        return true;
    }});

    rows.push_back({ "format-list-group.png", "Add to Bottom of Queue", "", false, false,
        [capturedAlbum, fetchAndPlay](brls::View*) {
        fetchAndPlay(capturedAlbum, "add");
        return true;
    }});

    rows.push_back({ "cross.png", "Cancel", "", false, true,
        [](brls::View*) {
        return true;
    }});

    const char* ctx = (album.mediaType == MediaType::PLAYLIST) ? "PLAYLIST" : "ALBUM";
    showOptionsPopover(anchor, ctx, album.name, std::move(rows));
}

void MediaDetailView::performTrackActionStatic(const MusicItem& track) {
    TrackDefaultAction action = Application::getInstance().getSettings().trackDefaultAction;

    if (action == TrackDefaultAction::ASK_EACH_TIME) {
        // Simplified menu (no playlist option without MediaDetailView context)
        brls::View* anchor = brls::Application::getCurrentFocus();
        MusicItem capturedTrack = track;

        std::vector<OptionRow> rows;

        rows.push_back({ "play.png", "Play Now (Clear Queue)", "", true, false,
            [capturedTrack](brls::View*) {
            std::vector<MusicItem> single = {capturedTrack};
            playTracksOnSelectedPlayer(single, "play");
            return true;
        }});

        rows.push_back({ "skip-next.png", "Play Next", "", false, false,
            [capturedTrack](brls::View*) {
            std::vector<MusicItem> single = {capturedTrack};
            playTracksOnSelectedPlayer(single, "next");
            return true;
        }});

        rows.push_back({ "format-list-group.png", "Add to Bottom of Queue", "", false, false,
            [capturedTrack](brls::View*) {
            std::vector<MusicItem> single = {capturedTrack};
            playTracksOnSelectedPlayer(single, "add");
            return true;
        }});

        rows.push_back({ "cross.png", "Cancel", "", false, true,
            [](brls::View*) {
            return true;
        }});

        showOptionsPopover(anchor, "TRACK", track.name, std::move(rows));
        return;
    }

    std::vector<MusicItem> single = {track};

    switch (action) {
        case TrackDefaultAction::PLAY_NEXT:
            playTracksOnSelectedPlayer(single, "next");
            break;

        case TrackDefaultAction::PLAY_NOW_REPLACE:
            playTracksOnSelectedPlayer(single, "play");
            break;

        case TrackDefaultAction::ADD_TO_BOTTOM:
            playTracksOnSelectedPlayer(single, "add");
            break;

        case TrackDefaultAction::PLAY_NOW_CLEAR:
        default:
            playTracksOnSelectedPlayer(single, "play");
            break;
    }
}

void MediaDetailView::setupChildrenFocusTransfer() {
    // Simplified for music-only: focus navigation for children containers
    // is now handled inline in loadTrackList() and loadMusicCategories().
}

void MediaDetailView::playTracksOnSelectedPlayer(const std::vector<MusicItem>& tracks,
                                                  const std::string& option) {
    if (tracks.empty()) return;

    const auto& selectedId = Application::getInstance().getSettings().selectedPlayerId;

    if (!selectedId.empty()) {
        // Remote player: send tracks to server via playMedia
        auto& client = MAClient::instance();
        if (!client.isConnected()) {
            brls::Application::notify("Not connected to server");
            return;
        }

        // Build URI list from tracks
        Json mediaArr(Json::ARRAY);
        for (const auto& track : tracks) {
            std::string uri = track.uri;
            if (uri.empty()) uri = "library://track/" + track.itemId;
            mediaArr.push_back(Json(uri));
        }

        Json args;
        args["queue_id"] = Json(selectedId);
        args["media"] = mediaArr;
        args["option"] = Json(option);

        std::string notifyMsg;
        if (option == "play") {
            notifyMsg = "Playing " + std::to_string(tracks.size()) + " tracks on remote player";
        } else if (option == "next") {
            notifyMsg = "Queued next on remote player";
        } else if (option == "add") {
            notifyMsg = "Added to remote player queue";
        }

        client.sendCommand("player_queues/play_media", args,
            [notifyMsg](bool success, const Json&) {
                if (success && !notifyMsg.empty()) {
                    brls::sync([notifyMsg]() {
                        brls::Application::notify(notifyMsg);
                    });
                }
            });
        return;
    }

    // Local player: create a PlayerActivity with a queue
    if (option == "play") {
        auto* playerActivity = PlayerActivity::createWithQueue(tracks, 0);
        brls::Application::pushActivity(playerActivity);
    } else if (option == "next") {
        MusicQueue& queue = MusicQueue::getInstance();
        if (queue.isEmpty()) {
            auto* playerActivity = PlayerActivity::createWithQueue(tracks, 0);
            brls::Application::pushActivity(playerActivity);
        } else {
            for (int i = (int)tracks.size() - 1; i >= 0; i--) {
                queue.insertTrackAfterCurrent(tracks[i]);
            }
            brls::Application::notify("Queued next");
        }
    } else if (option == "add") {
        MusicQueue& queue = MusicQueue::getInstance();
        if (queue.isEmpty()) {
            auto* playerActivity = PlayerActivity::createWithQueue(tracks, 0);
            brls::Application::pushActivity(playerActivity);
        } else {
            queue.addTracks(tracks);
            brls::Application::notify("Added to queue");
        }
    }
}

} // namespace vita_ma
