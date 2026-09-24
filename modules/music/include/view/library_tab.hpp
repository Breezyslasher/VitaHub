/**
 * Vita Music Assistant - Library Tab
 * Browse music library content (Artists, Albums, Tracks, Playlists)
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/ma_types.hpp"
#include "view/recycling_grid.hpp"

namespace vita_ma {

// Music library categories.
enum class MusicCategory {
    ARTISTS,
    ALBUMS,
    TRACKS,
    PLAYLISTS,
    PODCASTS,
    AUDIOBOOKS,
    RADIOS
};

class LibraryTab : public brls::Box {
public:
    // Single-category browser: one sidebar tab per category.
    explicit LibraryTab(MusicCategory category = MusicCategory::ALBUMS);
    ~LibraryTab();

    void onFocusGained() override;
    void willDisappear(bool resetState) override;

private:
    void onCategorySelected(MusicCategory category);
    void loadCategoryContent(MusicCategory category);
    void onItemSelected(const MusicItem& item);
    void showAlbumContextMenu(const MusicItem& album);

    brls::Label* m_titleLabel = nullptr;
    RecyclingGrid* m_contentGrid = nullptr;

    std::vector<MusicItem> m_items;
    int m_currentCategory = 1; // default to Albums (MusicCategory::ALBUMS)
    std::string m_currentCategoryName = "Albums";
    bool m_loaded = false;

    // Fetch state. Music Assistant's */library_items commands don't cap the
    // `limit` (it becomes a SQL LIMIT), and there is no separate "get all"
    // endpoint, so we request one effectively-unlimited page to pull the whole
    // category in a single call — no pagination. The auto-continue path below
    // stays as a safety net in case a server ever does cap a response.
    static constexpr int PAGE_SIZE = 100000;
    int m_offset = 0;
    bool m_hasMore = false;
    // Bumped on every fresh category load; in-flight page responses from a
    // previous category compare against it and bail, so switching category
    // can't append the old category's items into the new grid.
    int m_loadGen = 0;
    bool m_loadingPage = false;
    void loadNextPage();

    // Alive flag for crash prevention on quick tab switching
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace vita_ma
