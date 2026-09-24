#include "hub/hub_settings.hpp"

#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include "platform/paths.hpp"

namespace vitahub {

namespace {

HubSettings s_settings;

// ux0:data/VitaHub/hub.json on Vita, the per-platform VitaHub data
// directory elsewhere (hub/core/include/platform/paths.hpp).
std::string settingsPath() { return platformPath("hub.json"); }

}  // namespace

HubSettings& hubSettings() { return s_settings; }

void loadHubSettings() {
    // std::fstream works on every target (Vita's newlib forwards to sceIo),
    // as in Vita_plex's own settings code.
    std::ifstream in(settingsPath());
    if (!in) return;
    try {
        nlohmann::json j = nlohmann::json::parse(in);
        s_settings.unifiedTheme = j.value("unifiedTheme", s_settings.unifiedTheme);
        s_settings.launchTarget = static_cast<LaunchTarget>(j.value("launchTarget", 0));
        s_settings.lastModule   = j.value("lastModule", std::string());
        s_settings.showFps      = j.value("showFps", false);
        s_settings.autoCheckUpdates     = j.value("autoCheckUpdates", true);
        s_settings.skippedUpdateVersion = j.value("skippedUpdateVersion", std::string());
    } catch (const std::exception& e) {
        brls::Logger::warning("VitaHub: ignoring unreadable {}: {}", settingsPath(), e.what());
    }
}

void saveHubSettings() {
    nlohmann::json j = {
        {"unifiedTheme", s_settings.unifiedTheme},
        {"launchTarget", static_cast<int>(s_settings.launchTarget)},
        {"lastModule", s_settings.lastModule},
        {"showFps", s_settings.showFps},
        {"autoCheckUpdates", s_settings.autoCheckUpdates},
        {"skippedUpdateVersion", s_settings.skippedUpdateVersion},
    };
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(settingsPath()).parent_path(), ec);
    std::ofstream out(settingsPath(), std::ios::trunc);
    if (!out) {
        brls::Logger::error("VitaHub: cannot write {}", settingsPath());
        return;
    }
    out << j.dump(2);
}

}  // namespace vitahub
