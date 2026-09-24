/**
 * VitaABS - shared UI theme tokens
 *
 * Screens that paint their own panels (player, login) resolve colours from
 * borealis' active theme so they match the rest of the app in both Light and
 * Dark. Only the Audiobookshelf bronze is fixed, as a brand colour.
 *
 * Panel and hairline are derived by mixing the theme's TEXT colour into its
 * BACKGROUND rather than by reading a nearby theme key. borealis' own
 * "brls/sidebar/background" is only 5 levels off "brls/background" in the dark
 * theme (50,50,50 vs 45,45,45), which made bordered input rows disappear into
 * the page. Mixing gives a guaranteed contrast step that flips direction
 * automatically: it lightens on a dark theme and darkens on a light one.
 */

#pragma once

#include <borealis.hpp>

namespace vitaabs {
namespace uitok {

inline NVGcolor mix(NVGcolor a, NVGcolor b, float t) {
    return nvgRGBAf(a.r + (b.r - a.r) * t,
                    a.g + (b.g - a.g) * t,
                    a.b + (b.b - a.b) * t,
                    1.0f);
}

inline NVGcolor background() { return brls::Application::getTheme()["brls/background"]; }
inline NVGcolor text()       { return brls::Application::getTheme()["brls/text"]; }
// Secondary text: captions, sublines, timestamps, placeholders.
//
// NOT "brls/text_disabled" — that is a DISABLED colour, RGB(80,80,80) in the
// dark theme, which lands at 1.2:1 against a panel and is unreadable. Derived
// the same way as panel/hairline, it measures 5.5:1 on dark and 4.0:1 on
// light against the panel fill, so it clears WCAG AA for UI text on both.
inline NVGcolor secondary()  { return mix(background(), text(), 0.70f); }
// Genuinely de-emphasised / disabled state, straight from the theme.
inline NVGcolor muted()      { return brls::Application::getTheme()["brls/text_disabled"]; }

// A raised surface: cards, input rows, transport pills, stat tiles.
inline NVGcolor panel()      { return mix(background(), text(), 0.10f); }
// Borders and dividers — a step further so an outlined row reads as an edge.
inline NVGcolor hairline()   { return mix(background(), text(), 0.26f); }

// Audiobookshelf bronze. Fixed across themes.
inline NVGcolor accent()     { return nvgRGB(0xd7, 0x9b, 0x5a); }
// Text drawn on top of an accent fill.
inline NVGcolor accentInk()  { return nvgRGB(0x14, 0x16, 0x1e); }

}  // namespace uitok
}  // namespace vitaabs
