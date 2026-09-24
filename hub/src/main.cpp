/**
 * VitaHub entry point.
 *
 * Owns everything that is process-wide and used to be duplicated in each
 * app's main(): Vita system modules and networking, logging, borealis and the
 * window, and the main loop. Modules only ever run inside this.
 */

#include <borealis.hpp>

#include <clocale>
#include <cstdio>
#include <string>

#include "hub/hub.hpp"
#include "hub/hub_settings.hpp"
#include "hub/theme.hpp"

#if defined(__vita__)
#include <psp2/apputil.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/libssl.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/sysmodule.h>

#include <curl/curl.h>

// One heap for the whole hub. 172 MB is what Vita_plex (video playback)
// and Vita-Music-Assistant ship; Vita_abs / Vita_Suwayomi used 192 MB, but
// the GXM video path needs the extra headroom outside the newlib heap more.
#ifndef VITAHUB_HEAP_MB
#define VITAHUB_HEAP_MB 172
#endif
extern "C" {
int _newlib_heap_size_user = VITAHUB_HEAP_MB * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;
}

namespace {

constexpr int NET_MEMORY_SIZE  = 4 * 1024 * 1024;  // Vita_abs / Vita_Suwayomi size
constexpr int SSL_MEMORY_SIZE  = 512 * 1024;
constexpr int HTTP_MEMORY_SIZE = 2 * 1024 * 1024;
char __attribute__((aligned(64))) g_netMemory[NET_MEMORY_SIZE];

void loadShaderCompiler() {
    if (sceKernelLoadStartModule("ur0:data/libshacccg.suprx", 0, nullptr, 0, nullptr, nullptr) >= 0) return;
    sceKernelLoadStartModule("vs0:sys/external/libshacccg.suprx", 0, nullptr, 0, nullptr, nullptr);
}

void initVitaSystem() {
    SceAppUtilInitParam initParam = {};
    SceAppUtilBootParam bootParam = {};
    sceAppUtilInit(&initParam, &bootParam);

    // The highest clocks any of the four apps used (Vita_abs, Vita_Suwayomi).
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    loadShaderCompiler();

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
}

bool initVitaNetwork() {
    SceNetInitParam netInitParam;
    netInitParam.memory = g_netMemory;
    netInitParam.size   = NET_MEMORY_SIZE;
    netInitParam.flags  = 0;
    int ret = sceNetInit(&netInitParam);
    if (ret < 0 && ret != (int)0x80410201) return false;  // already initialised is fine
    ret = sceNetCtlInit();
    if (ret < 0 && ret != (int)0x80412102) return false;
    ret = sceSslInit(SSL_MEMORY_SIZE);
    if (ret < 0 && ret != (int)0x80435001) return false;
    ret = sceHttpInit(HTTP_MEMORY_SIZE);
    if (ret < 0 && ret != (int)0x80431002) return false;
    // The modules' HttpClient::globalInit() calls are reference counted by
    // curl; this one keeps curl alive for the whole process.
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
}

void cleanupVitaNetwork() {
    curl_global_cleanup();
    sceHttpTerm();
    sceSslTerm();
    sceNetCtlTerm();
    sceNetTerm();
}

}  // namespace
#endif  // __vita__

namespace {

FILE* g_logFile = nullptr;

void openLogFile() {
#if defined(__vita__)
    sceIoMkdir("ux0:data/VitaHub", 0777);
    g_logFile = std::fopen("ux0:data/VitaHub/vitahub.log", "w");
#endif
    if (!g_logFile) return;
    setvbuf(g_logFile, nullptr, _IOLBF, 0);
    brls::Logger::getLogEvent()->subscribe([](brls::Logger::TimePoint, brls::LogLevel level, std::string log) {
        const char* tag = "INFO";
        switch (level) {
            case brls::LogLevel::LOG_ERROR: tag = "ERROR"; break;
            case brls::LogLevel::LOG_WARNING: tag = "WARNING"; break;
            case brls::LogLevel::LOG_DEBUG: tag = "DEBUG"; break;
            case brls::LogLevel::LOG_VERBOSE: tag = "VERBOSE"; break;
            default: break;
        }
        std::fprintf(g_logFile, "[%s] %s\n", tag, log.c_str());
    });
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::setlocale(LC_ALL, "C.UTF-8");

#if defined(__vita__)
    initVitaSystem();
    if (!initVitaNetwork()) {
        sceKernelExitProcess(0);
        return 1;
    }
#endif

    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    if (!brls::Application::init()) {
        brls::Logger::error("VitaHub: unable to initialise borealis");
        return 1;
    }
    openLogFile();

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

    const vitahub::HubSettings& settings = vitahub::hubSettings();
    if (settings.launchTarget == vitahub::LaunchTarget::LAST_USED) {
        if (vitahub::Module* last = vitahub::findModule(settings.lastModule)) {
            brls::sync([last]() { vitahub::openModule(last); });
        }
    }

    while (brls::Application::mainLoop()) {
        for (vitahub::Module* m : vitahub::modules()) {
            if (m->started()) m->onFrame();
        }
    }

    for (vitahub::Module* m : vitahub::modules()) {
        if (m->started()) m->shutdown();
    }
    vitahub::saveHubSettings();

#if defined(__vita__)
    cleanupVitaNetwork();
    if (g_logFile) std::fclose(g_logFile);
    sceKernelExitProcess(0);
#else
    if (g_logFile) std::fclose(g_logFile);
#endif
    return 0;
}
