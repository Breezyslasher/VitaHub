/**
 * Stand-in for Vita_plex's app/application.hpp, for hub/core.
 *
 * hub/core is Vita_plex's platform layer and updater, imported and renamed
 * (tools/import_modules.py). Those files reach into Plex's Application for a
 * user-agent string and the "skipped update" setting; this header answers
 * with VitaHub's own settings instead.
 */

#pragma once

#include "hub/hub_settings.hpp"

#ifndef VITAHUB_VERSION
#define VITAHUB_VERSION "0.0.0"
#endif
#ifndef VITAHUB_DISPLAY_VERSION
#define VITAHUB_DISPLAY_VERSION VITAHUB_VERSION
#endif

// User agent for the hub's HttpClient (update checks and downloads).
#define PLEX_CLIENT_NAME "VitaHub"
#define PLEX_CLIENT_VERSION VITAHUB_VERSION

namespace vitahub {

using AppSettings = HubSettings;

class Application {
  public:
    static Application& getInstance() {
        static Application app;
        return app;
    }
    AppSettings& getSettings() { return hubSettings(); }
    void saveSettings() { saveHubSettings(); }
};

}  // namespace vitahub
