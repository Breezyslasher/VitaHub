#pragma once

#include <nanovg.h>

namespace vitahub {
namespace theme {

// Shared palette (the Vita_plex palette, which was already the most complete
// of the four apps). The accent is the only colour that changes per service.
inline const NVGcolor bg       = nvgRGB(45, 45, 45);
inline const NVGcolor panel    = nvgRGB(50, 50, 50);
inline const NVGcolor surface  = nvgRGB(52, 52, 62);
inline const NVGcolor surface2 = nvgRGB(60, 60, 72);
inline const NVGcolor surface3 = nvgRGB(67, 67, 79);
inline const NVGcolor line     = nvgRGB(67, 67, 74);
inline const NVGcolor text     = nvgRGB(255, 255, 255);
inline const NVGcolor muted    = nvgRGB(163, 163, 163);
inline const NVGcolor dim      = nvgRGB(124, 124, 132);
inline const NVGcolor hubAccent = nvgRGB(229, 160, 13);  // gold

/// Record borealis' stock theme colours. Call once, right after
/// brls::Application::init(), before any module repaints them.
void snapshotDefaults();

/// Put borealis' theme back the way it shipped, undoing whatever the
/// previous module painted, so the next one starts from a clean slate.
void restoreDefaults();

/// Paint the unified VitaHub look with `accent` as the accent colour.
void applyUnified(NVGcolor accent);

}  // namespace theme
}  // namespace vitahub
