/**
 * Vita Music Assistant - Search Tab
 * Home-style horizontal rails grouped by media type, with a multi-select
 * filter-chip row that maps to the search media_types parameter.
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "app/ma_types.hpp"
#include "view/horizontal_scroll_row.hpp"

namespace vita_ma {

class SearchTab : public brls::Box {
public:
    SearchTab();
    ~SearchTab();

    void onFocusGained() override;
    void willDisappear(bool resetState) override;

private:
    void buildChipRow();
    void styleChip(brls::Box* chip, brls::Label* label, bool selected);
    void toggleType(const std::string& apiType);
    void toggleLibraryOnly();
    void performSearch(const std::string& query);
    void rebuildRails();
    void populateRow(HorizontalScrollRow* row, const std::vector<MusicItem>& items);
    void onItemSelected(const MusicItem& item);

    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_searchLabel = nullptr;
    brls::Label* m_resultsLabel = nullptr;
    HorizontalScrollRow* m_chipRow = nullptr;

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_scrollContent = nullptr;

    // One chip per media type, parallel to the TYPES table in the .cpp.
    struct Chip { brls::Box* box = nullptr; brls::Label* label = nullptr; std::string apiType; };
    std::vector<Chip> m_chips;

    // "In Library" source filter: on = library only, off = all providers.
    brls::Box* m_libraryChip = nullptr;
    brls::Label* m_libraryChipLabel = nullptr;
    bool m_libraryOnly = false;

    // Selected media types (MA enum strings). Empty = show every type.
    std::set<std::string> m_selectedTypes;

    std::string m_searchQuery;
    // Results grouped by type; index matches the TYPES table order.
    std::vector<std::vector<MusicItem>> m_grouped;

    // Alive flag + generation counter for async-callback safety.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    int m_loadGeneration = 0;
};

} // namespace vita_ma
