/**
 * VitaABS - Search Tab
 * Search for media content
 */

#pragma once

#include <borealis.hpp>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"
#include "view/horizontal_scroll_row.hpp"

namespace vitaabs {

class SearchTab : public brls::Box {
public:
    SearchTab();

    void onFocusGained() override;

private:
    void performSearch(const std::string& query);
    void onItemSelected(const MediaItem& item);
    void populateRow(brls::Box* rowContent, const std::vector<MediaItem>& items);

    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_searchLabel = nullptr;
    brls::Label* m_resultsLabel = nullptr;

    // Scrollable content for organized results
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_scrollContent = nullptr;

    // Category rows
    // Each *Row IS its own cell container (HorizontalScrollRow scrolls by
    // translating its children), so *Content simply aliases the row.
    HorizontalScrollRow* m_moviesRow = nullptr;
    brls::Box* m_moviesContent = nullptr;
    HorizontalScrollRow* m_showsRow = nullptr;
    brls::Box* m_showsContent = nullptr;
    HorizontalScrollRow* m_episodesRow = nullptr;
    brls::Box* m_episodesContent = nullptr;
    HorizontalScrollRow* m_musicRow = nullptr;
    brls::Box* m_musicContent = nullptr;

    std::string m_searchQuery;
    std::vector<MediaItem> m_results;
    std::vector<MediaItem> m_movies;
    std::vector<MediaItem> m_shows;
    std::vector<MediaItem> m_episodes;
    std::vector<MediaItem> m_music;
};

} // namespace vitaabs
