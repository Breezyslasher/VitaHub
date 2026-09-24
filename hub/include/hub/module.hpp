/**
 * VitaHub module interface.
 *
 * Every service (Plex, Audiobookshelf, Suwayomi, Music Assistant) is a
 * complete app living in modules/<id>/, compiled into its own static library
 * in its own namespace. A Module is the thin adapter that lets the hub start
 * it, open its UI on top of the hub, and shut it down.
 *
 * Modules are started lazily, the first time the user opens them, so an
 * unused service costs nothing at boot (no server connection, no settings
 * load, no memory).
 */

#pragma once

#include <nanovg.h>

#include <string>
#include <vector>

namespace vitahub {

struct ModuleInfo {
    std::string id;           // stable key: "plex", "abs", "suwayomi", "music"
    std::string name;         // "Plex"
    std::string kind;         // "Movies & TV", shown under the name
    std::string description;  // one or two sentences for the service page
    std::string icon;         // resource path, e.g. "hub/plex.png"
    NVGcolor accent;          // brand colour, used as the UI accent inside the module
    std::string version;      // upstream app version the module was imported from
};

class Module {
  public:
    virtual ~Module() = default;

    virtual const ModuleInfo& info() const = 0;

    /// Process-level setup that has to happen before borealis creates the
    /// window (e.g. Plex's Windows shell identity). Called once, at startup.
    virtual void beforeWindow() {}

    /// A URL VitaHub was launched with (command line / Android intent). If the
    /// module recognises it, it keeps it for when it starts and returns true;
    /// the hub then opens that module.
    virtual bool acceptDeepLink(const std::string& url) {
        (void)url;
        return false;
    }

    /// Register the module's custom XML views. Called once, at hub startup,
    /// after brls::Application::init().
    virtual void registerViews() {}

    /// Push the module's UI on top of the hub. The first call also starts the
    /// module (loads its settings, restores its session, connects to the
    /// server); later calls reopen it where the session left off.
    virtual void open() = 0;

    /// Save state before the process exits. Only called for started modules.
    virtual void shutdown() {}

    /// Called once per frame while the module is started.
    virtual void onFrame() {}

    /// One-line status for the hub ("Signed in to Home Server", "Not signed in").
    virtual std::string status() const { return started() ? "Running" : "Not started"; }

    /// Apply the module's own theme to borealis (the module's applyTheme()).
    /// The hub calls this before layering its unified theme on top.
    virtual void applyOwnTheme() {}

    bool started() const { return m_started; }

  protected:
    bool m_started = false;
};

/// All modules compiled into this build, in sidebar order.
const std::vector<Module*>& modules();

/// Look up a compiled-in module by id, or nullptr.
Module* findModule(const std::string& id);

}  // namespace vitahub
