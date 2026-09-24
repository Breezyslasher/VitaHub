#include "hub/hub_settings.hpp"

#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(__vita__)
#include <psp2/io/stat.h>
#else
#include <sys/stat.h>
#endif

namespace vitahub {

namespace {

HubSettings s_settings;

void makeDir(const std::string& path) {
#if defined(__vita__)
    sceIoMkdir(path.c_str(), 0777);
#else
    // Create each component; ignore "already exists".
    for (size_t pos = 1; pos != std::string::npos; ) {
        pos = path.find('/', pos + 1);
        mkdir(path.substr(0, pos).c_str(), 0755);
    }
#endif
}

std::string settingsPath() { return hubDataDir() + "/hub.json"; }

}  // namespace

std::string hubDataDir() {
#if defined(__vita__)
    return "ux0:data/VitaHub";
#else
    const char* xdg  = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && xdg[0] == '/') return std::string(xdg) + "/VitaHub";
    if (home && *home) return std::string(home) + "/.local/share/VitaHub";
    return "./VitaHub";
#endif
}

HubSettings& hubSettings() { return s_settings; }

void loadHubSettings() {
    makeDir(hubDataDir());
    std::ifstream in(settingsPath());
    if (!in) return;
    try {
        nlohmann::json j = nlohmann::json::parse(in);
        s_settings.unifiedTheme = j.value("unifiedTheme", s_settings.unifiedTheme);
        s_settings.launchTarget = static_cast<LaunchTarget>(j.value("launchTarget", 0));
        s_settings.lastModule   = j.value("lastModule", std::string());
        s_settings.showFps      = j.value("showFps", false);
    } catch (const std::exception& e) {
        brls::Logger::warning("VitaHub: ignoring unreadable {}: {}", settingsPath(), e.what());
    }
}

void saveHubSettings() {
    makeDir(hubDataDir());
    nlohmann::json j = {
        {"unifiedTheme", s_settings.unifiedTheme},
        {"launchTarget", static_cast<int>(s_settings.launchTarget)},
        {"lastModule", s_settings.lastModule},
        {"showFps", s_settings.showFps},
    };
    std::ofstream out(settingsPath(), std::ios::trunc);
    if (!out) {
        brls::Logger::error("VitaHub: cannot write {}", settingsPath());
        return;
    }
    out << j.dump(2);
}

}  // namespace vitahub
