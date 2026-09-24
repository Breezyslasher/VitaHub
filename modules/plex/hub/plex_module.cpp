// VitaHub adapter for Vita_plex. Mirrors what the standalone app's main()
// did after the hub-owned parts (system init, window, main loop).

#include "hub/hub.hpp"
#include "hub/module.hpp"

#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "app/hint_icons.hpp"
#include "platform/platform.hpp"
#include "utils/http_client.hpp"
#include "utils/shell_integration.hpp"
#include "view/video_view.hpp"

namespace vitahub {

namespace {

class PlexModule : public Module {
  public:
    const ModuleInfo& info() const override {
        static const ModuleInfo i = {
            "plex",
            "Plex",
            "Movies, TV, music & Live TV",
            "Stream and download from your Plex Media Server: movies, shows, music with "
            "background playback, photos, Live TV & DVR, and SyncLounge watch parties.",
            "hub/plex.png",
            nvgRGB(229, 160, 13),
            VITA_PLEX_DISPLAY_VERSION,
        };
        return i;
    }

    // Windows: the AppUserModelID the taskbar and toasts are keyed on has to
    // be set before any window exists. No-op elsewhere.
    void beforeWindow() override { vitaplex::shell::init(); }

    bool acceptDeepLink(const std::string& url) override {
        if (url.rfind("plex://", 0) == 0 || url.rfind("vitaplex://", 0) == 0 ||
            url.rfind("https://app.plex.tv", 0) == 0) {
            vitaplex::platform::offerDeepLink(url);
            return true;
        }
        return false;
    }

    void registerViews() override {
        brls::Application::registerXMLView("vitaplex:VideoView", vitaplex::VideoView::create);
    }

    void open() override {
        auto& app = vitaplex::Application::getInstance();
        if (!m_started) {
            vitaplex::HttpClient::globalInit();
            vitaplex::HintIcons::init();
            vitaplex::DownloadsManager::getInstance().init();
            if (!app.init()) {
                brls::Application::notify("Plex failed to start");
                return;
            }
            vitaplex::shell::setShortcutAllowed(app.getSettings().windowsStartMenuShortcut);
            m_started = true;
            paintModuleTheme(*this);
            app.start();
            return;
        }
        app.applyTheme();
        paintModuleTheme(*this);
        if (signedIn())
            app.pushMainActivity();
        else
            app.start();
    }

    void shutdown() override { vitaplex::Application::getInstance().shutdown(); }

    std::string status() const override {
        if (!m_started) return "Not started";
        auto& app = vitaplex::Application::getInstance();
        if (app.isOfflineMode()) return "Offline (downloads only)";
        if (!signedIn()) return "Not signed in";
        return "Signed in to " + app.getServerUrl();
    }

  private:
    static bool signedIn() {
        auto& app = vitaplex::Application::getInstance();
        return (app.isLoggedIn() || app.getSettings().localServerMode) && !app.getServerUrl().empty();
    }
};

}  // namespace

Module* createPlexModule() {
    static PlexModule module;
    return &module;
}

}  // namespace vitahub
