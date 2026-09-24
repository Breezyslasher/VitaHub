/**
 * Vita Music Assistant - Media Detail View
 * Shows detailed information about a music item
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include <atomic>
#include <functional>
#include <vector>
#include "app/ma_types.hpp"

namespace vita_ma {

// One row of the compact options popover (the START context menu). Each menu
// translates its actions into OptionRows and feeds the whole vector to
// MediaDetailView::showOptionsPopover(). Presentation only — the `action`
// runs after the popover dismisses. (Ported 1:1 from Vita_plex.)
struct OptionRow {
    std::string icon;     // resources/icons asset key (e.g. "play.png")
    std::string label;
    std::string sub;      // optional trailing mono value ("", "42 tracks")
    bool primary = false; // default-focused row (the play action)
    bool danger  = false; // muted (Cancel)
    std::function<bool(brls::View*)> action;
};

class MediaDetailView : public brls::Box {
public:
    MediaDetailView(const MusicItem& item);
    ~MediaDetailView();

    static brls::View* create();

    // Shared builder for the compact options popover (Vita_plex artboard
    // "D4a"): centered 320px dark panel over a scrim with a context line +
    // title header and icon rows. Every context menu funnels its rows through
    // here. Static because several callers are static members.
    static void showOptionsPopover(brls::View* anchor,
                                   const std::string& contextLine,
                                   const std::string& title,
                                   std::vector<OptionRow> rows);

private:
    void loadDetails();
    void loadChildren();
    void loadMusicCategories();
    void loadTrackList();              // Load tracks in vertical list (like Suwayomi chapters)
    void onPlay(bool resume = false);
    void setupChildrenFocusTransfer();  // Set up focus navigation for children items
    void showAlbumContextMenu(const MusicItem& album);  // Context menu for albums

public:
    // Static context menus callable from any view (home, search, library grid, etc.)
    static void showArtistContextMenuStatic(const MusicItem& artist);
    static void showAlbumContextMenuStatic(const MusicItem& album);
    static void performTrackActionStatic(const MusicItem& track);

    // Play tracks on the currently selected player (remote or local).
    // For remote players, sends playMedia to the server.
    // For local players, creates a PlayerActivity with a queue.
    // option: "play" (clear queue), "next" (play next), "add" (add to bottom)
    static void playTracksOnSelectedPlayer(const std::vector<MusicItem>& tracks,
                                           const std::string& option = "play");
    void performTrackAction(const MusicItem& track, size_t trackIndex);  // Handle track default action
    void showTrackActionDialog(const MusicItem& track, size_t trackIndex);  // Ask user what to do

    brls::HScrollingFrame* createMediaRow(const std::string& title, brls::Box** contentOut);

    MusicItem m_item;
    std::vector<MusicItem> m_children;

    // Main layout
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_mainContent = nullptr;

    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_yearLabel = nullptr;
    brls::Label* m_ratingLabel = nullptr;
    brls::Label* m_durationLabel = nullptr;
    brls::Label* m_summaryLabel = nullptr;
    brls::Image* m_posterImage = nullptr;
    brls::Button* m_playButton = nullptr;
    brls::Button* m_resumeButton = nullptr;
    brls::Box* m_childrenBox = nullptr;
    brls::Label* m_childrenLabel = nullptr;
    brls::HScrollingFrame* m_childrenScroll = nullptr;

    // Track list for albums (vertical list with its own nested scroll)
    brls::Box* m_trackListBox = nullptr;

    // Description
    std::string m_fullDescription;
    brls::ScrollingFrame* m_summaryScroll = nullptr;   // Scroll frame for description

    // Music category rows for artists
    brls::Box* m_musicCategoriesBox = nullptr;
    brls::Box* m_albumsContent = nullptr;
    brls::Box* m_singlesContent = nullptr;
    brls::Box* m_epsContent = nullptr;
    brls::Box* m_compilationsContent = nullptr;
    brls::Box* m_soundtracksContent = nullptr;

    // Scrolling container for seasons+extras (prevents whole page from scrolling)
    brls::ScrollingFrame* m_mediaContentScroll = nullptr;
    brls::Box* m_mediaContentBox = nullptr;

    // Music videos row for artists
    brls::Box* m_musicVideosContent = nullptr;

    // Track currently focused hint icon (like Suwayomi's m_currentFocusedIcon)
    brls::Image* m_currentFocusedHint = nullptr;
    brls::Label* m_currentFocusedHintLabel = nullptr;

    // Shared alive flag to prevent async callbacks from accessing destroyed view
    std::shared_ptr<std::atomic<bool>> m_alive;
};

} // namespace vita_ma
