#include "hub/module.hpp"

namespace vitahub {

// Each module's hub/*_module.cpp defines its factory. Which ones exist is
// decided by the VITAHUB_MODULE_* CMake options.
#ifdef VITAHUB_HAVE_PLEX
Module* createPlexModule();
#endif
#ifdef VITAHUB_HAVE_ABS
Module* createAbsModule();
#endif
#ifdef VITAHUB_HAVE_SUWAYOMI
Module* createSuwayomiModule();
#endif
#ifdef VITAHUB_HAVE_MUSIC
Module* createMusicModule();
#endif

const std::vector<Module*>& modules() {
    static const std::vector<Module*> list = [] {
        std::vector<Module*> l;
#ifdef VITAHUB_HAVE_PLEX
        l.push_back(createPlexModule());
#endif
#ifdef VITAHUB_HAVE_ABS
        l.push_back(createAbsModule());
#endif
#ifdef VITAHUB_HAVE_SUWAYOMI
        l.push_back(createSuwayomiModule());
#endif
#ifdef VITAHUB_HAVE_MUSIC
        l.push_back(createMusicModule());
#endif
        return l;
    }();
    return list;
}

Module* findModule(const std::string& id) {
    for (Module* m : modules()) {
        if (m->info().id == id) return m;
    }
    return nullptr;
}

}  // namespace vitahub
