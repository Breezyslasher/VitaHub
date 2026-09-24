/**
 * Vita Music Assistant - Media Item Cell
 * A cell for displaying media items in a grid.
 *
 * The cover and title are drawn with NanoVG (no Label children). Inside a
 * RecyclingGrid the grid draws them in batched passes (all covers, then all
 * titles) to avoid per-cell GPU shader switches; standalone cells (outside a
 * grid) draw their own content. See setDeferContentDraw().
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include <atomic>
#include <string>
#include "app/ma_types.hpp"

namespace vita_ma {

class MediaItemCell : public brls::Box {
public:
    MediaItemCell();
    ~MediaItemCell() override;

    void setItem(const MusicItem& item);
    const MusicItem& getItem() const { return m_item; }

    void onFocusGained() override;
    void onFocusLost() override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    static brls::View* create();

    // Cover (NanoVG handle) management
    void unloadThumbnail();
    void reloadThumbnail();
    void loadThumbnailIfNeeded();
    bool isThumbnailLoaded() const { return m_thumbLoaded; }

    // --- Batched-draw support (RecyclingGrid draws covers/titles itself) ---
    // When true, draw() only renders the box background; the grid paints the
    // cover and title from the getters below in batched passes.
    void setDeferContentDraw(bool defer) { m_deferDraw = defer; }

    float getDrawX() const { return m_drawX; }
    float getDrawY() const { return m_drawY; }
    float getDrawW() const { return m_drawW; }
    float getDrawH() const { return m_drawH; }

    int getCoverImage() const { return m_nvgCover; }
    int getCoverWidth() const { return m_coverW; }
    int getCoverHeight() const { return m_coverH; }
    bool cellHasFocus() const { return m_focused; }

    // Fit the title to one line of maxWidth (ellipsised), cached until the item
    // changes. Sets NanoVG font state, so call it BEFORE starting a text batch.
    const std::string& fittedTitle(NVGcontext* vg, float fontSize, float maxWidth);
    // The already-fitted title (no measuring / no state change). Valid after
    // fittedTitle() has run for this item.
    const std::string& cachedTitle() const { return m_cachedTitle; }
    // Secondary line: track number, or year / duration / item-count when focused.
    std::string secondaryLine() const;

    // Cover box geometry (top-centred square) derived from the last draw rect.
    static constexpr float COVER_SIZE = 110.0f;
    static constexpr float CELL_PADDING = 5.0f;

    // Draw a fallback tile (tinted rounded square + the item's initial) for items
    // with no cover art. Shared so both a standalone cell and the grid's batched
    // pass render the same placeholder. size is the square edge length.
    static void drawCoverPlaceholder(NVGcontext* vg, float x, float y, float size,
                                     const MusicItem& item);

private:
    void loadThumbnail();
    void drawCover(NVGcontext* vg);
    void drawText(NVGcontext* vg);

    bool m_pressed = false;
    bool m_focused = false;
    bool m_thumbLoaded = false;
    bool m_deferDraw = false;  // true when a RecyclingGrid batches our drawing

    MusicItem m_item;

    // Cached one-line, width-fitted title.
    std::string m_cachedTitle;
    bool m_titleCached = false;
    float m_cachedTitleWidth = 0.0f;

    // Alive flag - set to false in destructor to prevent use-after-free in async
    // cover-load callbacks.
    std::shared_ptr<std::atomic<bool>> m_alive;

    // Cover: a raw NanoVG image handle owned by this cell.
    int m_nvgCover = 0;
    int m_coverW = 0;
    int m_coverH = 0;

    // Last draw rectangle, recorded so the grid can paint our cover/title.
    float m_drawX = 0.0f, m_drawY = 0.0f, m_drawW = 0.0f, m_drawH = 0.0f;
};

} // namespace vita_ma
