#pragma once

#include <string>

namespace vitahub {

enum class LaunchTarget {
    HUB = 0,         // always start on the VitaHub home screen
    LAST_USED = 1,   // reopen the service used last
};

struct HubSettings {
    // Paint every module with the hub's shared dark palette, using the
    // module's brand colour as the accent. Off = each module keeps the look
    // of its standalone app.
    bool unifiedTheme = true;
    LaunchTarget launchTarget = LaunchTarget::HUB;
    std::string lastModule;
    bool showFps = false;
};

HubSettings& hubSettings();
void loadHubSettings();
void saveHubSettings();

/// Hub data directory: ux0:data/VitaHub on Vita, $XDG_DATA_HOME/VitaHub on desktop.
std::string hubDataDir();

}  // namespace vitahub
