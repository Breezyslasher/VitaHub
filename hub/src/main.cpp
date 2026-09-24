/**
 * VitaHub entry point.
 *
 * Owns everything that is process-wide and used to be duplicated in each
 * app's main(): platform bootstrap (Vita system modules and networking, PS4
 * Orbis modules, Switch sockets, Android asset extraction, log files),
 * borealis and the window, and the main loop. Modules only ever run inside
 * this. The sequence follows Vita_plex's main(), whose platform layer
 * hub/core is (hub/core/src/platform/platform_<name>.cpp, picked by CMake).
 */

#include <borealis.hpp>
#ifdef __SWITCH__
#include <switch.h>
#include <cstdio>
#endif

#include <clocale>
#include <cstring>
#include <string>

#include "hub/hub.hpp"
#include "hub/hub_settings.hpp"
#include "hub/theme.hpp"
#include "platform/platform.hpp"
#include "utils/app_update.hpp"

namespace {

void failAndExit(int code) {
    vitahub::platform::shutdown();
    if (vitahub::platform::needsHardExit()) vitahub::platform::hardExit(code);
}

}  // namespace

// Shared entry point: main() on every platform, SDL_main() on Android
// (hub/core/src/platform/platform_android.cpp).
extern "C" int VitaHubMainEntry(int argc, char* argv[]) {
#ifdef __SWITCH__
    // Refuse applet-mode launches (the homebrew menu opened from the album
    // rather than over a title): applets get a fraction of the RAM, so video
    // playback dies, and only 2 BSD socket sessions (Vita_plex).
    {
        AppletType at = appletGetAppletType();
        if (at != AppletType_Application && at != AppletType_SystemApplication) {
            consoleInit(NULL);
            printf("\n  VitaHub needs full memory to run.\n");
            printf("\n  Launch it with title override: hold R while\n");
            printf("  opening any game, then start VitaHub from\n");
            printf("  the homebrew menu.\n");
            printf("\n  Press + to exit.\n");
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            PadState pad;
            padInitializeDefault(&pad);
            while (appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
                consoleUpdate(NULL);
            }
            consoleExit(NULL);
            return 0;
        }
    }
#endif

    std::setlocale(LC_ALL, "C.UTF-8");

    // Where our own executable lives: on Switch the updater replaces it.
    if (argc > 0) vitahub::app_update::setSelfPath(argv[0]);

    // A deep link on the command line (desktop URL handlers pass it as the
    // first argument, Vita_Suwayomi's Android intent as --deeplink <url>).
    std::string deepLink;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        if (std::strcmp(argv[i], "--deeplink") == 0 && i + 1 < argc) {
            deepLink = argv[++i];
        } else if (deepLink.empty() && std::strstr(argv[i], "://")) {
            deepLink = argv[i];
        }
    }

    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    if (!vitahub::platform::init()) {
        brls::Logger::error("VitaHub: platform init failed");
        failAndExit(1);
        return 1;
    }

    for (vitahub::Module* m : vitahub::modules()) m->beforeWindow();

    if (!brls::Application::init()) {
        brls::Logger::error("VitaHub: unable to initialise borealis");
        failAndExit(1);
        return 1;
    }

    // All four apps used the same sidebar padding.
    brls::getStyle().addMetric("brls/sidebar/padding_left", 20);
    brls::getStyle().addMetric("brls/sidebar/padding_right", 20);

    brls::Application::createWindow("VitaHub");

    vitahub::theme::snapshotDefaults();
    vitahub::theme::applyUnified(vitahub::theme::hubAccent);

    vitahub::loadHubSettings();
    brls::Application::setFPSStatus(vitahub::hubSettings().showFps);

    for (vitahub::Module* m : vitahub::modules()) m->registerViews();

    brls::Application::pushActivity(new vitahub::HubActivity(), brls::TransitionAnimation::NONE);

    // Which service to show first: the one a deep link belongs to, else the
    // last one used if the user asked for that.
    vitahub::Module* first = nullptr;
    if (!deepLink.empty()) {
        for (vitahub::Module* m : vitahub::modules()) {
            if (m->acceptDeepLink(deepLink)) {
                first = m;
                break;
            }
        }
    }
    const vitahub::HubSettings& settings = vitahub::hubSettings();
    if (!first && settings.launchTarget == vitahub::LaunchTarget::LAST_USED)
        first = vitahub::findModule(settings.lastModule);
    if (first) brls::sync([first]() { vitahub::openModule(first); });

    if (settings.autoCheckUpdates) vitahub::app_update::checkForUpdates(false);

    while (brls::Application::mainLoop()) {
        for (vitahub::Module* m : vitahub::modules()) {
            if (m->started()) m->onFrame();
        }
    }

    for (vitahub::Module* m : vitahub::modules()) {
        if (m->started()) m->shutdown();
    }
    vitahub::saveHubSettings();

    vitahub::platform::shutdown();
    if (vitahub::platform::needsHardExit()) vitahub::platform::hardExit(0);
    return 0;
}

int main(int argc, char* argv[]) {
    return VitaHubMainEntry(argc, argv);
}
