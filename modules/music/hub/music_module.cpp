// VitaHub adapter for Vita-Music-Assistant. Mirrors what the standalone
// app's main() did after the hub-owned parts (system init, window, main loop).

#include "hub/hub.hpp"
#include "hub/module.hpp"

#include "app.h"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "utils/http_client.hpp"

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif

namespace vitahub {

namespace {

class MusicModule : public Module {
  public:
    const ModuleInfo& info() const override {
        static const ModuleInfo i = {
            "music",
            "Music Assistant",
            "Music, radio & remote players",
            "Browse and play your Music Assistant library on the Vita (Sendspin) or control "
            "any player on your network. Remote access over WebRTC with QR sign-in.",
            "hub/music.png",
            nvgRGB(3, 169, 244),
            VMA_VERSION,
        };
        return i;
    }

    void open() override {
        auto& app = vita_ma::Application::getInstance();
        if (!m_started) {
            vita_ma::HttpClient::globalInit();
            if (!app.init()) {
                brls::Application::notify("Music Assistant failed to start");
                return;
            }
            m_started = true;
            paintModuleTheme(*this);
            app.start();
            return;
        }
        app.applyTheme();
        paintModuleTheme(*this);
        if (vita_ma::MAClient::instance().isConnected())
            app.pushMainActivity();
        else
            app.start();
    }

    // Same as the standalone app's main loop: keep the Vita from
    // auto-suspending, which would silently drop the MA WebSocket, Sendspin
    // and WebRTC sockets (the screen may still dim).
    void onFrame() override {
#ifdef __vita__
        if (++m_powerTickCounter >= 300) {  // roughly every 5 s at 60 fps
            m_powerTickCounter = 0;
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        }
#endif
    }

    void shutdown() override { vita_ma::Application::getInstance().shutdown(); }

    std::string status() const override {
        if (!m_started) return "Not started";
        if (!vita_ma::MAClient::instance().isConnected()) return "Not connected";
        return "Connected to " + vita_ma::Application::getInstance().getServerUrl();
    }

  private:
    int m_powerTickCounter = 0;
};

}  // namespace

Module* createMusicModule() {
    static MusicModule module;
    return &module;
}

}  // namespace vitahub
