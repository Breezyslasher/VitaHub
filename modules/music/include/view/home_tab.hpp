/**
 * Vita Music Assistant - Home Tab
 * Shows recently added music
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include <atomic>
#include "app/ma_types.hpp"
#include "view/recycling_grid.hpp"
#include "view/horizontal_scroll_row.hpp"

namespace vita_ma {

class HomeTab : public brls::Box {
public:
    HomeTab();
    ~HomeTab();

    void onFocusGained() override;
    void willDisappear(bool resetState) override;

private:
    void loadContent();
    void onItemSelected(const MusicItem& item);

    // Helper to create a media row with horizontal scrolling
    HorizontalScrollRow* createMediaRow();
    void populateRow(HorizontalScrollRow* row, const std::vector<MusicItem>& items, bool directPlay = false);

    // Vertical scroll container
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_scrollContent = nullptr;

    brls::Label* m_titleLabel = nullptr;

    // Recently Added Music section
    HorizontalScrollRow* m_musicRow = nullptr;

    std::vector<MusicItem> m_recentMusic;
    bool m_loaded = false;

    // Alive flag for crash prevention on quick tab switching
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace vita_ma
