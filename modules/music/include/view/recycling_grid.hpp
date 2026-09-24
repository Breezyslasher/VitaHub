/**
 * Vita Music Assistant - Recycling Grid
 * Memory-efficient grid view with infinite scroll pagination.
 * Automatically fetches the next page when scrolling near the bottom.
 */

#pragma once

#include <borealis.hpp>
#include "app/ma_types.hpp"
#include <functional>

namespace vita_ma {

class MediaItemCell;

class RecyclingGrid : public brls::ScrollingFrame {
public:
    RecyclingGrid();
    ~RecyclingGrid() override;

    void setDataSource(const std::vector<MusicItem>& items);
    // Append additional items (called when next page arrives from server)
    void appendItems(const std::vector<MusicItem>& newItems);
    void setOnItemSelected(std::function<void(const MusicItem&)> callback);
    void setOnItemStartAction(std::function<void(const MusicItem&)> callback);

    // Called automatically when user scrolls to the bottom.
    // Owner should fetch the next page and call appendItems().
    void setOnLoadMore(std::function<void()> callback);

    // Tell the grid whether more items are available on the server
    void setHasMore(bool hasMore);

    // Override to detect when user tries to scroll past the bottom
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

    // Override draw to manage texture loading for visible rows
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    static brls::View* create();

private:
    void rebuildGrid();
    void onItemClicked(int index);
    void addCellForItem(brls::Box*& currentRow, int& itemsInRow, size_t index);
    // Create a fully-wired cell for m_items[index] without attaching it to the
    // tree, so callers can add it to a DETACHED row box and batch yoga layout
    // (one invalidation per row instead of per cell - the Vita_Suwayomi trick).
    MediaItemCell* createCell(size_t index);

    // Build rows [from .. ] into `into`, up to maxRows rows, returning the next
    // unbuilt item index. Accumulates per-phase timing into m_buildCreateUs /
    // m_buildAttachUs so we can see whether cell construction or addView layout
    // dominates.
    size_t buildRows(brls::Box* into, size_t fromIndex, size_t maxRows);
    // Called each frame while m_building: builds a time-bounded batch of rows so
    // a large library fills in over a few frames instead of freezing the UI.
    void buildStep();
    // Prioritise loading covers for cells in/near the viewport (no unloading).
    void loadCoversForScrollPosition();
    // Hide off-screen rows (Visibility::INVISIBLE) so ScrollingFrame::draw skips
    // their entire subtree — the single biggest scroll-time CPU saving.
    void updateRowVisibility();
    // Pause/resume ImageLoader GPU uploads based on scroll velocity so a 15-20ms
    // texture upload never lands inside a scroll frame.
    void updateScrollGating();

    std::vector<MusicItem> m_items;
    std::function<void(const MusicItem&)> m_onItemSelected;
    std::function<void(const MusicItem&)> m_onItemStartAction;
    std::function<void()> m_onLoadMore;

    brls::Box* m_contentBox = nullptr;
    int m_columns = 6;
    int m_visibleRows = 3;
    size_t m_renderedCount = 0;

    bool m_hasMore = false;
    bool m_loading = false;  // Prevents duplicate fetch requests

    // Incremental (frame-sliced) grid build state, so building a large library
    // never blocks the UI thread in one shot.
    bool m_building = false;       // rows still left to build across frames
    size_t m_buildNextIdx = 0;     // next item index to build
    long long m_buildCreateUs = 0; // total us spent in createCell across the build
    long long m_buildAttachUs = 0; // total us spent in addView across the build

    // Flat list of every cell, in item order, for cover loading.
    std::vector<MediaItemCell*> m_cells;

    static constexpr int TEXTURE_BUFFER_ROWS = 3;  // Extra rows near viewport to prioritise

    // Approx vertical pitch of one row (cell height 150 + 10 row margin).
    static constexpr float ROW_PITCH = 160.0f;

    // Visibility-culling cache: the currently-shown row range, so we only touch
    // rows at the boundaries when the range changes instead of every frame.
    int m_cachedFirstVisible = -1;
    int m_cachedLastVisible = -1;

    // Scroll-velocity tracking for gating texture uploads.
    float m_prevScrollY = 0.0f;
    int m_scrollSettledFrames = 0;   // frames since the last noticeable scroll delta
    bool m_uploadsDeferred = false;  // whether we currently have uploads paused

    // Throttle for the scroll-position cover prioritiser.
    float m_lastScrollLoadY = 0.0f;
    int m_scrollLoadCooldown = 0;

    // Progressive cover loader: like Vita_Suwayomi, every cover is loaded once
    // (a few per frame) and never unloaded, so the whole library fills in and
    // scrolling never re-decodes/re-uploads a texture.
    int m_nextCoverLoadIdx = 0;
    bool m_allCoversQueued = false;
};

} // namespace vita_ma
