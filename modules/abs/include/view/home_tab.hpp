/**
 * VitaABS - Home Tab
 * Shows Continue Listening and Recently Added Episodes across all libraries
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include <atomic>
#include "app/audiobookshelf_client.hpp"
#include "view/horizontal_scroll_row.hpp"

namespace vitaabs {

class MediaItemCell;  // Forward declaration

class HomeTab : public brls::Box {
public:
    HomeTab();
    ~HomeTab() override;

    void onFocusGained() override;
    brls::View* getDefaultFocus() override;

private:
    void loadContent();
    void populateHorizontalRow(brls::Box* container, const std::vector<MediaItem>& items);
    void onItemSelected(const MediaItem& item);

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    brls::Label* m_titleLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Continue Listening section (horizontal row). The row IS the cell
    // container — HorizontalScrollRow scrolls by translating its own
    // children, so there is no separate content box to wrap them in.
    brls::Label* m_continueLabel = nullptr;
    HorizontalScrollRow* m_continueBox = nullptr;
    std::vector<MediaItem> m_continueItems;

    // Recently Added Episodes section (horizontal row)
    brls::Label* m_recentEpisodesLabel = nullptr;
    HorizontalScrollRow* m_recentEpisodesBox = nullptr;
    std::vector<MediaItem> m_recentEpisodes;

    bool m_loaded = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<std::atomic<bool>> m_alive;
};

} // namespace vitaabs
