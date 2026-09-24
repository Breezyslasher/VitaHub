/**
 * Vita Music Assistant - Media Item Cell implementation
 */

#include "view/media_item_cell.hpp"
#include "app/ma_types.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "utils/image_loader.hpp"
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace vita_ma {

MediaItemCell::MediaItemCell()
    : m_alive(std::make_shared<std::atomic<bool>>(true)) {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(CELL_PADDING);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
    this->setWidth(120);
    this->setHeight(150);
    // No child views: the cover and title are drawn with NanoVG (by us when
    // standalone, or by the owning RecyclingGrid in batched passes).
}

MediaItemCell::~MediaItemCell() {
    if (m_alive) m_alive->store(false);
    if (m_nvgCover != 0) {
        NVGcontext* vg = brls::Application::getNVGContext();
        if (vg) nvgDeleteImage(vg, m_nvgCover);
        m_nvgCover = 0;
    }
}

void MediaItemCell::setItem(const MusicItem& item) {
    m_item = item;

    // Reset cover state so a reused cell reloads art for its new item.
    if (m_nvgCover != 0) {
        NVGcontext* vg = brls::Application::getNVGContext();
        if (vg) nvgDeleteImage(vg, m_nvgCover);
        m_nvgCover = 0;
        m_coverW = 0;
        m_coverH = 0;
    }
    m_thumbLoaded = false;
    m_titleCached = false;

    this->setWidth(120);
    this->setHeight(150);
}

void MediaItemCell::loadThumbnail() {
    // Mark loaded up front: the grid and draw() call this every frame, so we must
    // not re-enqueue while a load is in flight, and art-less cells must not rebuild
    // their URL every frame.
    m_thumbLoaded = true;

    if (m_item.imageUrl.empty()) return;

    std::string fullUrl = MAClient::instance().getThumbnailUrl(m_item.imageUrl, 300, 300, m_item.imageProvider);
    if (fullUrl.empty()) return;

    MediaItemCell* self = this;
    std::shared_ptr<std::atomic<bool>> alive = m_alive;
    ImageLoader::loadCoverAsync(fullUrl, [self, alive](int nvgImg, int w, int h) {
        if (!alive->load()) {
            NVGcontext* vg = brls::Application::getNVGContext();
            if (vg && nvgImg != 0) nvgDeleteImage(vg, nvgImg);
            return;
        }
        if (self->m_nvgCover != 0) {
            NVGcontext* vg = brls::Application::getNVGContext();
            if (vg) nvgDeleteImage(vg, self->m_nvgCover);
        }
        self->m_nvgCover = nvgImg;
        self->m_coverW = w;
        self->m_coverH = h;
    }, m_alive);
}

void MediaItemCell::loadThumbnailIfNeeded() {
    if (!m_thumbLoaded) loadThumbnail();
}

void MediaItemCell::unloadThumbnail() {
    if (m_nvgCover != 0) {
        NVGcontext* vg = brls::Application::getNVGContext();
        if (vg) nvgDeleteImage(vg, m_nvgCover);
        m_nvgCover = 0;
        m_coverW = 0;
        m_coverH = 0;
    }
    m_thumbLoaded = false;
}

void MediaItemCell::reloadThumbnail() {
    loadThumbnailIfNeeded();
}

brls::View* MediaItemCell::create() {
    return new MediaItemCell();
}

void MediaItemCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx) {
    // Record the draw rect so a grid can paint our cover/title in batched passes.
    m_drawX = x;
    m_drawY = y;
    m_drawW = width;
    m_drawH = height;

    loadThumbnailIfNeeded();

    // Box::draw renders the background and focus border only.
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    // When used outside a grid, paint our own cover + title. Inside a grid this
    // is deferred so the grid can batch every cell's covers/titles together.
    if (!m_deferDraw) {
        drawCover(vg);
        drawText(vg);
    }

    if (m_pressed) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 8);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 80));
        nvgFill(vg);
    }
}

void MediaItemCell::drawCoverPlaceholder(NVGcontext* vg, float x, float y, float size,
                                         const MusicItem& item) {
    // Tinted tile: derive a muted, dark colour from the name so cover-less items
    // aren't all identical, while staying subdued next to real album art.
    unsigned h = 2166136261u;
    for (char c : item.name) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
    int r = 34 + static_cast<int>(h & 31u);
    int g = 40 + static_cast<int>((h >> 5) & 31u);
    int b = 50 + static_cast<int>((h >> 10) & 31u);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, size, size, 4.0f);
    nvgFillColor(vg, nvgRGB(r, g, b));
    nvgFill(vg);

    // Monogram: the first alphanumeric character of the name (uppercased).
    std::string mono;
    for (char c : item.name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            mono = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            break;
        }
    }
    if (mono.empty()) mono = "?";
    nvgFontFace(vg, "regular");
    nvgFontSize(vg, size * 0.42f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(150, 162, 176, 235));
    nvgText(vg, x + size * 0.5f, y + size * 0.5f, mono.c_str(), nullptr);
}

void MediaItemCell::drawCover(NVGcontext* vg) {
    float cw = COVER_SIZE, ch = COVER_SIZE;
    float cx = m_drawX + (m_drawW - cw) * 0.5f;
    float cy = m_drawY + CELL_PADDING;

    // No cover art loaded: draw the fallback tile instead of an empty slot.
    if (m_nvgCover == 0 || m_coverW <= 0 || m_coverH <= 0) {
        drawCoverPlaceholder(vg, cx, cy, cw, m_item);
        return;
    }

    float scale = std::max(cw / static_cast<float>(m_coverW),
                           ch / static_cast<float>(m_coverH));
    float sw = static_cast<float>(m_coverW) * scale;
    float sh = static_cast<float>(m_coverH) * scale;
    float ox = cx + (cw - sw) * 0.5f;
    float oy = cy + (ch - sh) * 0.5f;

    NVGpaint paint = nvgImagePattern(vg, ox, oy, sw, sh, 0.0f, m_nvgCover, 1.0f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, cx, cy, cw, ch, 4.0f);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

void MediaItemCell::drawText(NVGcontext* vg) {
    float maxW = m_drawW - 8.0f;
    fittedTitle(vg, 12.0f, maxW);  // ensure cached (sets its own LEFT align)

    float cx = m_drawX + m_drawW * 0.5f;
    float ty = m_drawY + CELL_PADDING + COVER_SIZE + 4.0f;

    nvgFontFace(vg, "regular");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    if (!m_cachedTitle.empty()) {
        nvgFontSize(vg, 12.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgText(vg, cx, ty, m_cachedTitle.c_str(), nullptr);
    }

    std::string sec = secondaryLine();
    if (!sec.empty()) {
        nvgFontSize(vg, 10.0f);
        nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
        nvgText(vg, cx, ty + 16.0f, sec.c_str(), nullptr);
    }
}

void MediaItemCell::onFocusGained() {
    brls::Box::onFocusGained();
    this->setBackgroundColor(nvgRGBA(60, 60, 80, 255));
    this->setBorderColor(nvgRGBA(229, 160, 13, 255));
    this->setBorderThickness(2.0f);
    m_focused = true;
}

void MediaItemCell::onFocusLost() {
    brls::Box::onFocusLost();
    this->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
    this->setBorderColor(nvgRGBA(0, 0, 0, 0));
    this->setBorderThickness(0.0f);
    m_pressed = false;
    m_focused = false;
}

const std::string& MediaItemCell::fittedTitle(NVGcontext* vg, float fontSize, float maxWidth) {
    if (m_titleCached && m_cachedTitleWidth == maxWidth) return m_cachedTitle;
    m_titleCached = true;
    m_cachedTitleWidth = maxWidth;

    const std::string& title = m_item.name;
    if (title.empty()) {
        m_cachedTitle.clear();
        return m_cachedTitle;
    }

    nvgFontFace(vg, "regular");
    nvgFontSize(vg, fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    NVGtextRow rows[2];
    int nrows = nvgTextBreakLines(vg, title.c_str(), nullptr, maxWidth, rows, 2);
    if (nrows <= 0) {
        m_cachedTitle = title;
        return m_cachedTitle;
    }
    if (nrows == 1 && rows[0].end == title.c_str() + title.size()) {
        m_cachedTitle.assign(rows[0].start, rows[0].end);
        return m_cachedTitle;
    }
    std::string line(rows[0].start, rows[0].end);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    m_cachedTitle = line + "...";
    return m_cachedTitle;
}

std::string MediaItemCell::secondaryLine() const {
    if (m_focused) {
        if (m_item.mediaType == MediaType::ALBUM && m_item.year > 0)
            return std::to_string(m_item.year);
        if (m_item.mediaType == MediaType::TRACK && m_item.duration > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d:%02d", m_item.duration / 60, m_item.duration % 60);
            return buf;
        }
        if (m_item.mediaType == MediaType::PLAYLIST && m_item.itemCount > 0)
            return std::to_string(m_item.itemCount) + " items";
        return "";
    }
    if (m_item.mediaType == MediaType::TRACK && m_item.trackNumber > 0)
        return "Track " + std::to_string(m_item.trackNumber);
    return "";
}

} // namespace vita_ma
