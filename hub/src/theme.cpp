#include "hub/theme.hpp"

#include <borealis.hpp>

#include <string>
#include <unordered_map>

namespace vitahub {
namespace theme {

namespace {

// Every borealis theme slot that the hub or any module repaints. Keep this in
// sync with the modules' applyTheme() implementations, otherwise a slot one
// module painted leaks into the next module.
const char* const kSlots[] = {
    "brls/clear",
    "brls/background",
    "brls/sidebar/background",
    "brls/sidebar/separator",
    "brls/applet_frame/separator",
    "brls/header/border",
    "brls/text",
    "brls/text_disabled",
    "brls/header/subtitle",
    "brls/header/rectangle",
    "brls/accent",
    "brls/sidebar/active_item",
    "brls/list/listItem_value_color",
    "brls/button/primary_enabled_background",
    "brls/button/primary_enabled_text",
    "brls/button/primary_disabled_background",
    "brls/button/primary_disabled_text",
    "brls/button/default_enabled_background",
    "brls/button/default_disabled_background",
    "brls/button/default_enabled_text",
    "brls/button/default_disabled_text",
    "brls/button/enabled_border_color",
    "brls/button/disabled_border_color",
    "brls/button/highlight_enabled_text",
    "brls/button/highlight_disabled_text",
    "brls/highlight/color1",
    "brls/highlight/color2",
    "brls/highlight/background",
    "brls/click_pulse",
    "brls/slider/line_filled",
    "brls/slider/line_empty",
    "brls/slider/pointer_color",
    "brls/slider/pointer_border_color",
    "brls/spinner/bar_color",
};

std::unordered_map<std::string, NVGcolor> s_dark;
std::unordered_map<std::string, NVGcolor> s_light;
bool s_haveSnapshot = false;

NVGcolor withAlpha(NVGcolor c, float a) {
    c.a = a;
    return c;
}

NVGcolor lighten(NVGcolor c, float amount) {
    c.r = c.r + (1.0f - c.r) * amount;
    c.g = c.g + (1.0f - c.g) * amount;
    c.b = c.b + (1.0f - c.b) * amount;
    return c;
}

NVGcolor darken(NVGcolor c, float amount) {
    c.r *= 1.0f - amount;
    c.g *= 1.0f - amount;
    c.b *= 1.0f - amount;
    return c;
}

// Text drawn on top of an accent fill: dark ink on light accents, white on
// dark ones.
NVGcolor inkFor(NVGcolor c) {
    float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    return luma > 0.5f ? nvgRGB(36, 28, 8) : nvgRGB(255, 255, 255);
}

}  // namespace

void snapshotDefaults() {
    if (s_haveSnapshot) return;
    for (const char* slot : kSlots) {
        s_dark[slot]  = brls::Theme::getDarkTheme().getColor(slot);
        s_light[slot] = brls::Theme::getLightTheme().getColor(slot);
    }
    s_haveSnapshot = true;
}

void restoreDefaults() {
    if (!s_haveSnapshot) return;
    for (const auto& kv : s_dark) brls::Theme::getDarkTheme().addColor(kv.first, kv.second);
    for (const auto& kv : s_light) brls::Theme::getLightTheme().addColor(kv.first, kv.second);
}

void applyUnified(NVGcolor accent) {
    const NVGcolor accentBright = lighten(accent, 0.25f);
    const NVGcolor accentDeep   = darken(accent, 0.15f);
    const NVGcolor halo         = lighten(accent, 0.45f);
    const NVGcolor ink          = inkFor(accent);

    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    for (brls::Theme* t : {&brls::Theme::getDarkTheme(), &brls::Theme::getLightTheme()}) {
        t->addColor("brls/clear", bg);
        t->addColor("brls/background", bg);
        t->addColor("brls/sidebar/background", panel);
        t->addColor("brls/sidebar/separator", line);
        t->addColor("brls/applet_frame/separator", line);
        t->addColor("brls/header/border", line);
        t->addColor("brls/text", text);
        t->addColor("brls/text_disabled", dim);
        t->addColor("brls/header/subtitle", muted);
        t->addColor("brls/header/rectangle", muted);
        t->addColor("brls/accent", accent);
        t->addColor("brls/sidebar/active_item", accent);
        t->addColor("brls/list/listItem_value_color", accent);
        t->addColor("brls/button/primary_enabled_background", accent);
        t->addColor("brls/button/primary_enabled_text", ink);
        t->addColor("brls/button/primary_disabled_background", surface3);
        t->addColor("brls/button/primary_disabled_text", dim);
        t->addColor("brls/button/default_enabled_background", surface3);
        t->addColor("brls/button/default_disabled_background", surface2);
        t->addColor("brls/button/default_enabled_text", text);
        t->addColor("brls/button/default_disabled_text", dim);
        t->addColor("brls/button/enabled_border_color", line);
        t->addColor("brls/button/disabled_border_color", line);
        t->addColor("brls/button/highlight_enabled_text", accent);
        t->addColor("brls/button/highlight_disabled_text", dim);
        t->addColor("brls/highlight/color1", accent);
        t->addColor("brls/highlight/color2", halo);
        t->addColor("brls/highlight/background", surface2);
        t->addColor("brls/click_pulse", withAlpha(accent, 0.04f));
        t->addColor("brls/slider/line_filled", accent);
        t->addColor("brls/slider/line_empty", surface3);
        t->addColor("brls/slider/pointer_color", accentBright);
        t->addColor("brls/slider/pointer_border_color", accentDeep);
        t->addColor("brls/spinner/bar_color", withAlpha(accent, 90.0f / 255.0f));
    }
}

}  // namespace theme
}  // namespace vitahub
