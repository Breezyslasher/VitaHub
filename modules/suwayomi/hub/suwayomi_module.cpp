// VitaHub adapter for Vita_Suwayomi. Mirrors what the standalone app's main()
// did after the hub-owned parts (system init, window, main loop).

#include "hub/hub.hpp"
#include "hub/module.hpp"

#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/http_client.hpp"
#include "view/rotatable_image.hpp"

namespace vitahub {

namespace {

class SuwayomiModule : public Module {
  public:
    const ModuleInfo& info() const override {
        static const ModuleInfo i = {
            "suwayomi",
            "Suwayomi",
            "Manga",
            "Read manga from your Suwayomi server: paged and webtoon readers, rotation, "
            "downloads, tracking, extensions, migration and reading stats.",
            "hub/suwayomi.png",
            nvgRGB(91, 141, 239),
            VITA_SUWAYOMI_DISPLAY_VERSION,
        };
        return i;
    }

    bool acceptDeepLink(const std::string& url) override {
        if (url.rfind("vitasuwayomi://", 0) != 0) return false;
        vitasuwayomi::Application::getInstance().setDeeplink(url);
        return true;
    }

    void registerViews() override {
        // reader.xml uses <RotatableImage>; the reader registers its other
        // custom views itself.
        brls::Application::registerXMLView("RotatableImage", vitasuwayomi::RotatableImage::create);
    }

    void open() override {
        auto& app = vitasuwayomi::Application::getInstance();
        if (!m_started) {
            vitasuwayomi::HttpClient::globalInit();
            vitasuwayomi::DownloadsManager::getInstance().init();
            if (!app.init()) {
                brls::Application::notify("Suwayomi failed to start");
                return;
            }
            m_started = true;
            paintModuleTheme(*this);
            app.start();
            return;
        }
        app.applyTheme();
        paintModuleTheme(*this);
        if (app.isConnected())
            app.pushMainActivity();
        else
            app.start();  // reconnect, or offline library if downloads exist
    }

    void shutdown() override { vitasuwayomi::Application::getInstance().shutdown(); }

    std::string status() const override {
        if (!m_started) return "Not started";
        auto& app = vitasuwayomi::Application::getInstance();
        if (!app.isConnected()) return "Not connected";
        return "Connected to " + app.getServerUrl();
    }
};

}  // namespace

Module* createSuwayomiModule() {
    static SuwayomiModule module;
    return &module;
}

}  // namespace vitahub
