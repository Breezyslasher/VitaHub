/**
 * Vita Music Assistant - Recycling Grid implementation
 * Infinite scroll: automatically fetches next page when user navigates
 * to the last row. Server-side pagination keeps memory low.
 */

#include "view/recycling_grid.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "utils/image_loader.hpp"
#include <cmath>
#include <chrono>
#include <algorithm>

// From the patched nanovg.c (patches/nanovg.c): accumulates consecutive nvgText
// calls that share state into a single render call.
extern "C" {
void nvgTextBatchBegin(NVGcontext* ctx);
void nvgTextBatchEnd(NVGcontext* ctx);
}

namespace vita_ma {

RecyclingGrid::RecyclingGrid() {
    this->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    // Content box to hold all items
    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setPadding(10);
    this->setContentView(m_contentBox);

    // PS Vita screen: 960x544, so 6 columns of ~120px items
    m_columns = 6;
    m_visibleRows = 3;
}

RecyclingGrid::~RecyclingGrid() {
    // If this grid was torn down mid-scroll it may have left ImageLoader's
    // uploads paused; release them so other screens can upload textures.
    if (m_uploadsDeferred) {
        ImageLoader::setDeferTextureUploads(false);
    }
}

void RecyclingGrid::setDataSource(const std::vector<MusicItem>& items) {
    m_items = items;
    m_loading = false;
    // Rows are rebuilt below, so the cached visibility/scroll state is stale.
    m_cachedFirstVisible = -1;
    m_cachedLastVisible = -1;
    m_scrollLoadCooldown = 0;
    m_lastScrollLoadY = 0.0f;
    // Restart the progressive cover loader for the new cell set.
    m_nextCoverLoadIdx = 0;
    m_allCoversQueued = false;
    rebuildGrid();
}

void RecyclingGrid::appendItems(const std::vector<MusicItem>& newItems) {
    if (newItems.empty()) {
        m_loading = false;
        return;
    }

    size_t oldSize = m_items.size();
    m_items.insert(m_items.end(), newItems.begin(), newItems.end());

    // Render the new items continuing from where we left off
    brls::Box* currentRow = nullptr;
    int itemsInRow = (int)(oldSize % m_columns);
    if (itemsInRow > 0 && m_contentBox->getChildren().size() > 0) {
        auto& children = m_contentBox->getChildren();
        currentRow = dynamic_cast<brls::Box*>(children.back());
        if (!currentRow) itemsInRow = 0;
    } else {
        itemsInRow = 0;
    }

    for (size_t i = oldSize; i < m_items.size(); i++) {
        addCellForItem(currentRow, itemsInRow, i);
    }

    m_renderedCount = m_items.size();
    m_loading = false;

    // Newly added rows default to VISIBLE. Invalidate the culling cache so the
    // next draw re-evaluates every row (otherwise, if the scroll position is
    // unchanged, the new off-screen rows would never get culled).
    m_cachedFirstVisible = -1;
    m_cachedLastVisible = -1;
    m_scrollLoadCooldown = 0;
    // Resume the progressive loader so the appended cells get their covers.
    // m_nextCoverLoadIdx already points at the first new cell.
    m_allCoversQueued = false;
}

void RecyclingGrid::setOnItemSelected(std::function<void(const MusicItem&)> callback) {
    m_onItemSelected = callback;
}

void RecyclingGrid::setOnItemStartAction(std::function<void(const MusicItem&)> callback) {
    m_onItemStartAction = callback;
}

void RecyclingGrid::setOnLoadMore(std::function<void()> callback) {
    m_onLoadMore = callback;
}

void RecyclingGrid::setHasMore(bool hasMore) {
    m_hasMore = hasMore;
    m_loading = false;
}

brls::View* RecyclingGrid::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    brls::View* next = brls::ScrollingFrame::getNextFocus(direction, currentView);

    // If navigating DOWN and there's nowhere to go, we're at the bottom.
    // Trigger loading the next page if more items are available.
    if (direction == brls::FocusDirection::DOWN && next == nullptr &&
        m_hasMore && !m_loading && m_onLoadMore) {
        m_loading = true;
        // Use brls::sync to defer the fetch so focus resolution completes first
        brls::sync([this]() {
            if (m_onLoadMore) m_onLoadMore();
        });
    }

    return next;
}

MediaItemCell* RecyclingGrid::createCell(size_t index) {
    auto* cell = new MediaItemCell();
    cell->setItem(m_items[index]);
    cell->setMarginRight(10);
    cell->setDeferContentDraw(true);  // the grid batch-draws covers/titles

    int idx = (int)index;
    cell->registerClickAction([this, idx](brls::View* view) {
        onItemClicked(idx);
        return true;
    });
    cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

    // Register START button action for album items
    if (m_items[index].mediaType == MediaType::ALBUM && m_onItemStartAction) {
        MusicItem capturedItem = m_items[index];
        cell->registerAction("Options", brls::ControllerButton::BUTTON_START,
            [this, capturedItem](brls::View* view) {
                if (m_onItemStartAction) {
                    m_onItemStartAction(capturedItem);
                }
                return true;
            });
    }

    // Register START button action for artists
    if (m_items[index].mediaType == MediaType::ARTIST) {
        MusicItem capturedItem = m_items[index];
        cell->registerAction("Options", brls::ControllerButton::BUTTON_START,
            [capturedItem](brls::View* view) {
                MediaDetailView::showArtistContextMenuStatic(capturedItem);
                return true;
            });
    }

    // Register START button action for playlists
    if (m_items[index].mediaType == MediaType::PLAYLIST && m_onItemStartAction) {
        MusicItem capturedItem = m_items[index];
        cell->registerAction("Options", brls::ControllerButton::BUTTON_START,
            [this, capturedItem](brls::View* view) {
                if (m_onItemStartAction) {
                    m_onItemStartAction(capturedItem);
                }
                return true;
            });
    }

    return cell;
}

void RecyclingGrid::addCellForItem(brls::Box*& currentRow, int& itemsInRow, size_t index) {
    if (itemsInRow == 0) {
        currentRow = new brls::Box();
        currentRow->setAxis(brls::Axis::ROW);
        currentRow->setJustifyContent(brls::JustifyContent::FLEX_START);
        currentRow->setMarginBottom(10);
        m_contentBox->addView(currentRow);
    }

    MediaItemCell* cell = createCell(index);
    currentRow->addView(cell);
    m_cells.push_back(cell);

    itemsInRow++;
    if (itemsInRow >= m_columns) {
        itemsInRow = 0;
    }
}

void RecyclingGrid::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx) {
    // Fill in remaining rows a time-bounded batch at a time so a large library
    // never freezes the UI thread on load.
    if (m_building) buildStep();

    // Cull off-screen rows BEFORE drawing so ScrollingFrame::draw() skips their
    // entire subtree (borealis short-circuits non-VISIBLE views in View::frame).
    updateRowVisibility();

    brls::ScrollingFrame::draw(vg, x, y, width, height, style, ctx);

    // Batched content draw: all covers first (image shader), then all titles
    // (text shader, accumulated into one render call). Drawing every cell's
    // cover and then every cell's title — instead of cover+title per cell —
    // stops the GPU ping-ponging between shaders each cell, which is the main
    // remaining scroll cost vs Vita_Suwayomi. The cells defer their own content
    // draw (setDeferContentDraw) so only their box background is drawn above.
    if (m_cachedFirstVisible >= 0 && !m_cells.empty()) {
        int total = static_cast<int>(m_cells.size());
        int startIdx = std::max(0, m_cachedFirstVisible * m_columns);
        int endIdx = std::min(m_cachedLastVisible * m_columns, total);

        nvgSave(vg);
        nvgIntersectScissor(vg, x, y, width, height);

        // Pass 1: covers.
        for (int i = startIdx; i < endIdx; i++) {
            MediaItemCell* cell = m_cells[i];
            if (!cell) continue;
            float dw = cell->getDrawW();
            if (dw <= 0.0f) continue;

            float cw = MediaItemCell::COVER_SIZE;
            float ch = MediaItemCell::COVER_SIZE;
            float cx = cell->getDrawX() + (dw - cw) * 0.5f;
            float cy = cell->getDrawY() + MediaItemCell::CELL_PADDING;

            int img = cell->getCoverImage();
            int iw = cell->getCoverWidth();
            int ih = cell->getCoverHeight();
            // No cover art: draw the shared fallback tile (tinted + initial).
            if (img == 0 || iw <= 0 || ih <= 0) {
                MediaItemCell::drawCoverPlaceholder(vg, cx, cy, cw, cell->getItem());
                continue;
            }

            float scale = std::max(cw / static_cast<float>(iw), ch / static_cast<float>(ih));
            float sw = static_cast<float>(iw) * scale;
            float sh = static_cast<float>(ih) * scale;
            float ox = cx + (cw - sw) * 0.5f;
            float oy = cy + (ch - sh) * 0.5f;

            NVGpaint paint = nvgImagePattern(vg, ox, oy, sw, sh, 0.0f, img, 1.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, cy, cw, ch, 4.0f);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }

        // Pre-fit every visible title first: fittedTitle() sets the font state
        // for measuring, which would otherwise break the text batch below.
        for (int i = startIdx; i < endIdx; i++) {
            MediaItemCell* cell = m_cells[i];
            if (cell && cell->getDrawW() > 0.0f)
                cell->fittedTitle(vg, 12.0f, cell->getDrawW() - 8.0f);
        }

        float titleY = MediaItemCell::CELL_PADDING + MediaItemCell::COVER_SIZE + 4.0f;

        // Pass 2a: titles (white, 12px) in a single batched run.
        nvgFontFace(vg, "regular");
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgFontSize(vg, 12.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgTextBatchBegin(vg);
        for (int i = startIdx; i < endIdx; i++) {
            MediaItemCell* cell = m_cells[i];
            if (!cell) continue;
            float dw = cell->getDrawW();
            if (dw <= 0.0f) continue;
            const std::string& title = cell->cachedTitle();
            if (title.empty()) continue;
            nvgText(vg, cell->getDrawX() + dw * 0.5f, cell->getDrawY() + titleY,
                    title.c_str(), nullptr);
        }
        nvgTextBatchEnd(vg);

        // Pass 2b: secondary lines (grey, 10px) in their own batched run.
        nvgFontSize(vg, 10.0f);
        nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
        nvgTextBatchBegin(vg);
        for (int i = startIdx; i < endIdx; i++) {
            MediaItemCell* cell = m_cells[i];
            if (!cell) continue;
            float dw = cell->getDrawW();
            if (dw <= 0.0f) continue;
            std::string sec = cell->secondaryLine();
            if (sec.empty()) continue;
            nvgText(vg, cell->getDrawX() + dw * 0.5f, cell->getDrawY() + titleY + 16.0f,
                    sec.c_str(), nullptr);
        }
        nvgTextBatchEnd(vg);

        nvgRestore(vg);
    }

    // After drawing: pause/resume brls::Image uploads based on scroll speed.
    updateScrollGating();

    // Prioritise covers in/near the viewport while scrolling (focus-driven
    // navigation moves focus; touch scrolling does not, so this drives loading
    // during drags). No unloading — covers are loaded once and kept.
    if (m_scrollLoadCooldown > 0) {
        m_scrollLoadCooldown--;
    }
    if (!m_cells.empty() && m_scrollLoadCooldown == 0) {
        float scrollY = this->getContentOffsetY();
        if (std::abs(scrollY - m_lastScrollLoadY) > ROW_PITCH) {
            m_lastScrollLoadY = scrollY;
            m_scrollLoadCooldown = 6;
            loadCoversForScrollPosition();
        }
    }

    // Progressive cover loader: queue a few not-yet-loaded cells each frame until
    // every cover is loaded, so the whole library fills in without scrolling and
    // scrolling never has to re-load a texture (the Vita_Suwayomi behaviour).
    if (!m_allCoversQueued && !m_cells.empty()) {
        int total = static_cast<int>(m_cells.size());
        int budget = 6;
        while (budget > 0 && m_nextCoverLoadIdx < total) {
            MediaItemCell* cell = m_cells[m_nextCoverLoadIdx];
            if (cell && !cell->isThumbnailLoaded()) {
                cell->loadThumbnailIfNeeded();
                budget--;
            }
            m_nextCoverLoadIdx++;
        }
        if (m_nextCoverLoadIdx >= total) {
            m_allCoversQueued = true;
        }
    }
}

void RecyclingGrid::loadCoversForScrollPosition() {
    if (m_cells.empty() || m_columns <= 0) return;

    float scrollY = this->getContentOffsetY();  // positive = scrolled down
    float viewportH = this->getHeight();
    int totalRows = (static_cast<int>(m_cells.size()) + m_columns - 1) / m_columns;

    int firstRow = std::max(0, static_cast<int>(scrollY / ROW_PITCH) - 1);
    int lastRow = std::min(totalRows,
                           static_cast<int>((scrollY + viewportH) / ROW_PITCH) + TEXTURE_BUFFER_ROWS);

    int startCell = firstRow * m_columns;
    int endCell = std::min(lastRow * m_columns, static_cast<int>(m_cells.size()));
    for (int i = startCell; i < endCell; i++) {
        if (m_cells[i]) m_cells[i]->loadThumbnailIfNeeded();
    }
}

void RecyclingGrid::updateRowVisibility() {
    if (!m_contentBox) return;
    auto& rows = m_contentBox->getChildren();
    if (rows.empty()) return;

    float scrollY = this->getContentOffsetY();  // positive = scrolled down
    float viewH = this->getHeight();
    int rowCount = static_cast<int>(rows.size());

    // Keep one row of slack above and two below so D-pad focus can always move
    // onto an adjacent (and therefore still-focusable) VISIBLE cell.
    int firstVisible = std::max(0, static_cast<int>(scrollY / ROW_PITCH) - 1);
    int lastVisible = std::min(rowCount, static_cast<int>((scrollY + viewH) / ROW_PITCH) + 2);

    // Nothing moved across a row boundary — leave visibility as-is.
    if (firstVisible == m_cachedFirstVisible && lastVisible == m_cachedLastVisible) return;

    if (m_cachedFirstVisible < 0) {
        // First evaluation (or after a rebuild): set every row in one pass.
        for (int i = 0; i < rowCount; i++) {
            brls::Visibility v = (i >= firstVisible && i < lastVisible)
                ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE;
            rows[i]->setVisibility(v);
        }
    } else {
        // Incremental: hide rows that left the window, show rows that entered it.
        for (int i = m_cachedFirstVisible; i < firstVisible; i++)
            if (i >= 0 && i < rowCount) rows[i]->setVisibility(brls::Visibility::INVISIBLE);
        for (int i = lastVisible; i < m_cachedLastVisible; i++)
            if (i >= 0 && i < rowCount) rows[i]->setVisibility(brls::Visibility::INVISIBLE);
        for (int i = firstVisible; i < lastVisible; i++)
            if (i >= 0 && i < rowCount) rows[i]->setVisibility(brls::Visibility::VISIBLE);
    }

    m_cachedFirstVisible = firstVisible;
    m_cachedLastVisible = lastVisible;
}

void RecyclingGrid::updateScrollGating() {
    float curY = this->getContentOffsetY();
    float frameDelta = std::abs(curY - m_prevScrollY);
    m_prevScrollY = curY;

    bool scrolling = frameDelta > 0.5f;
    if (scrolling) m_scrollSettledFrames = 0;
    else m_scrollSettledFrames++;

    // Defer while moving and for a few frames after stopping, so the flush of a
    // 15-20ms upload lands well clear of the motion.
    bool wantDefer = scrolling || m_scrollSettledFrames < 6;
    if (wantDefer != m_uploadsDeferred) {
        m_uploadsDeferred = wantDefer;
        ImageLoader::setDeferTextureUploads(wantDefer);
    }
}

size_t RecyclingGrid::buildRows(brls::Box* into, size_t fromIndex, size_t maxRows) {
    const size_t total = m_items.size();
    const size_t cols = (size_t)std::max(1, m_columns);
    size_t idx = fromIndex;
    size_t builtRows = 0;

    while (idx < total && builtRows < maxRows) {
        auto tc = std::chrono::steady_clock::now();
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setJustifyContent(brls::JustifyContent::FLEX_START);
        row->setMarginBottom(10);

        size_t end = std::min(idx + cols, total);
        for (size_t i = idx; i < end; i++) {
            MediaItemCell* cell = createCell(i);
            row->addView(cell);
            m_cells.push_back(cell);
        }
        auto tm = std::chrono::steady_clock::now();
        into->addView(row);
        auto te = std::chrono::steady_clock::now();

        m_buildCreateUs += std::chrono::duration_cast<std::chrono::microseconds>(tm - tc).count();
        m_buildAttachUs += std::chrono::duration_cast<std::chrono::microseconds>(te - tm).count();

        idx = end;
        builtRows++;
    }

    m_renderedCount = idx;
    return idx;
}

void RecyclingGrid::rebuildGrid() {
    m_cells.clear();
    m_renderedCount = 0;
    m_cachedFirstVisible = -1;
    m_cachedLastVisible = -1;
    m_buildCreateUs = 0;
    m_buildAttachUs = 0;
    m_building = false;
    m_buildNextIdx = 0;

    // Build the first screenful synchronously into a fresh, parentless box (so
    // there's immediately something to show and focus), attach it once, then
    // let buildStep() fill in the rest a few rows per frame. Building all rows
    // up front froze the UI ~11s for a 1000-item library because Box::addView()
    // runs a yoga layout each time (see buildStep timing logs); slicing it keeps
    // the UI responsive regardless of library size.
    static constexpr size_t INITIAL_ROWS = 6;

    auto* fresh = new brls::Box();
    fresh->setAxis(brls::Axis::COLUMN);
    fresh->setPadding(10);

    size_t idx = 0;
    if (!m_items.empty())
        idx = buildRows(fresh, 0, INITIAL_ROWS);

    // Move focus off the old content before setContentView() deletes it.
    brls::View* focus = brls::Application::getCurrentFocus();
    for (brls::View* v = focus; v; v = v->hasParent() ? v->getParent() : nullptr) {
        if (v == m_contentBox) {
            this->setFocusable(true);
            brls::Application::giveFocus(this);
            this->setFocusable(false);
            break;
        }
    }

    this->setContentView(fresh);
    m_contentBox = fresh;

    m_buildNextIdx = idx;
    m_building = (idx < m_items.size());
    if (!m_building) {
        brls::Logger::info("RecyclingGrid: built {} cells (create {}ms, attach {}ms)",
                           (int)m_cells.size(), (long)(m_buildCreateUs / 1000),
                           (long)(m_buildAttachUs / 1000));
    }
}

void RecyclingGrid::buildStep() {
    if (!m_building) return;

    auto frameStart = std::chrono::steady_clock::now();
    // Build rows until we've spent our per-frame budget, so no single frame
    // stalls. Later rows cost more (addView lays out the growing tree), so the
    // budget naturally builds fewer rows per frame near the end.
    while (m_buildNextIdx < m_items.size()) {
        m_buildNextIdx = buildRows(m_contentBox, m_buildNextIdx, 1);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - frameStart).count();
        if (elapsed >= 6) break;  // ~6ms/frame budget keeps us near 60 FPS
    }

    if (m_buildNextIdx >= m_items.size()) {
        m_building = false;
        // Force a fresh visibility pass now that every row exists.
        m_cachedFirstVisible = -1;
        m_cachedLastVisible = -1;
        m_allCoversQueued = false;  // ensure covers get queued for the late rows
        brls::Logger::info("RecyclingGrid: built {} cells (create {}ms, attach {}ms)",
                           (int)m_cells.size(), (long)(m_buildCreateUs / 1000),
                           (long)(m_buildAttachUs / 1000));
    }
}

void RecyclingGrid::onItemClicked(int index) {
    if (index >= 0 && index < (int)m_items.size() && m_onItemSelected) {
        m_onItemSelected(m_items[index]);
    }
}

brls::View* RecyclingGrid::create() {
    return new RecyclingGrid();
}

} // namespace vita_ma
