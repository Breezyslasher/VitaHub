/**
 * VitaABS - Media Item Cell implementation
 * NanoVG-drawn cell (no child views), ported from Vita-Music-Assistant.
 */

#include "view/media_item_cell.hpp"
#include "app/audiobookshelf_client.hpp"
#include "utils/image_loader.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <cstdio>
#include <cctype>

namespace vitaabs {

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

void MediaItemCell::setItem(const MediaItem& item) {
    m_item = item;

    // Display title: prefix podcast episodes with their number.
    if (item.mediaType == MediaType::PODCAST_EPISODE && item.episodeNumber > 0) {
        m_displayTitle = "Ep " + std::to_string(item.episodeNumber) + ": " + item.title;
    } else {
        m_displayTitle = item.title;
    }

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
}

void MediaItemCell::loadThumbnail() {
    // Mark loaded up front: the grid and draw() call this every frame, so we must
    // not re-enqueue while a load is in flight, and art-less cells must not rebuild
    // their URL every frame.
    m_thumbLoaded = true;

    MediaItemCell* self = this;
    std::shared_ptr<std::atomic<bool>> alive = m_alive;
    auto onCover = [self, alive](int nvgImg, int w, int h) {
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
    };

    // Local cover for downloaded items (Vita ux0: path or an on-disk file).
    if (!m_item.coverPath.empty() && m_item.coverPath.rfind("http", 0) != 0) {
        bool isLocal = m_item.coverPath.rfind("ux0:", 0) == 0 ||
                       platform::fileExists(m_item.coverPath);
        if (isLocal) {
            ImageLoader::loadCoverFromFileAsync(m_item.coverPath, onCover, m_alive);
            return;
        }
    }

    if (m_item.id.empty()) return;

    std::string url = AudiobookshelfClient::getInstance().getCoverUrl(m_item.id, 280, 280);
    if (url.empty()) return;

    ImageLoader::loadCoverAsync(url, onCover, m_alive);
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
                                         const MediaItem& item) {
    // Tinted tile: derive a muted, dark colour from the title so cover-less items
    // aren't all identical, while staying subdued next to real cover art.
    unsigned h = 2166136261u;
    for (char c : item.title) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
    int r = 34 + static_cast<int>(h & 31u);
    int g = 40 + static_cast<int>((h >> 5) & 31u);
    int b = 50 + static_cast<int>((h >> 10) & 31u);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, size, size, 4.0f);
    nvgFillColor(vg, nvgRGB(r, g, b));
    nvgFill(vg);

    // Monogram: the first alphanumeric character of the title (uppercased).
    std::string mono;
    for (char c : item.title) {
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

void MediaItemCell::drawProgressOverlay(NVGcontext* vg, float cx, float cy, float size) const {
    if (m_item.currentTime <= 0 || m_item.duration <= 0) return;
    float progress = m_item.currentTime / m_item.duration;
    progress = std::min(1.0f, std::max(0.0f, progress));
    if (progress <= 0.0f) return;

    // Track (dark) then fill (accent gold), pinned to the cover's bottom edge.
    float barH = 4.0f;
    float by = cy + size - barH;
    nvgBeginPath(vg);
    nvgRect(vg, cx, by, size, barH);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 140));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, cx, by, size * progress, barH);
    nvgFillColor(vg, nvgRGBA(229, 160, 13, 255));
    nvgFill(vg);
}

void MediaItemCell::drawCover(NVGcontext* vg) {
    float cw = COVER_SIZE, ch = COVER_SIZE;
    float cx = m_drawX + (m_drawW - cw) * 0.5f;
    float cy = m_drawY + CELL_PADDING;

    // No cover art loaded: draw the fallback tile instead of an empty slot.
    if (m_nvgCover == 0 || m_coverW <= 0 || m_coverH <= 0) {
        drawCoverPlaceholder(vg, cx, cy, cw, m_item);
        drawProgressOverlay(vg, cx, cy, cw);
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

    drawProgressOverlay(vg, cx, cy, cw);
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

    const std::string& title = m_displayTitle;
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
        if (m_item.mediaType == MediaType::PODCAST_EPISODE && m_item.duration > 0) {
            int minutes = static_cast<int>(m_item.duration / 60.0f);
            return std::to_string(minutes) + " min";
        }
        if (m_item.mediaType == MediaType::BOOK) {
            std::string info = m_item.authorName;
            if (m_item.duration > 0) {
                int hours = static_cast<int>(m_item.duration / 3600.0f);
                int mins = static_cast<int>((m_item.duration - hours * 3600) / 60.0f);
                if (!info.empty()) info += " - ";
                info += std::to_string(hours) + "h " + std::to_string(mins) + "m";
            }
            return info;
        }
        if (m_item.mediaType == MediaType::PODCAST) {
            return m_item.authorName;
        }
        return "";
    }
    return "";
}

} // namespace vitaabs
