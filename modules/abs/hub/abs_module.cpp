// VitaHub adapter for Vita_abs. Mirrors what the standalone app's main()
// did after the hub-owned parts (system init, window, main loop).

#include "hub/hub.hpp"
#include "hub/module.hpp"

#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/http_client.hpp"

namespace vitahub {

namespace {

class AbsModule : public Module {
  public:
    const ModuleInfo& info() const override {
        static const ModuleInfo i = {
            "abs",
            "Audiobookshelf",
            "Audiobooks & podcasts",
            "Listen to your Audiobookshelf library: progress sync, bookmarks, chapters, "
            "sleep timer, podcast search and offline downloads.",
            "hub/abs.png",
            nvgRGB(215, 155, 90),
            VITAABS_DISPLAY_VERSION,
        };
        return i;
    }

    void open() override {
        auto& app = vitaabs::Application::getInstance();
        if (!m_started) {
            vitaabs::HttpClient::globalInit();
            vitaabs::DownloadsManager::getInstance().init();
            if (!app.init()) {
                brls::Application::notify("Audiobookshelf failed to start");
                return;
            }
            m_started = true;
            paintModuleTheme(*this);
            app.start();
            return;
        }
        app.applyTheme();
        paintModuleTheme(*this);
        if (app.isLoggedIn() && !app.getServerUrl().empty())
            app.pushMainActivity();
        else
            app.start();  // offline-with-downloads or login, as at launch
    }

    void shutdown() override { vitaabs::Application::getInstance().shutdown(); }

    std::string status() const override {
        if (!m_started) return "Not started";
        auto& app = vitaabs::Application::getInstance();
        if (!app.isLoggedIn()) return "Not signed in";
        return "Signed in to " + app.getServerUrl();
    }
};

}  // namespace

Module* createAbsModule() {
    static AbsModule module;
    return &module;
}

}  // namespace vitahub
