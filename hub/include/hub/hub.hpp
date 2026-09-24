#pragma once

#include <borealis.hpp>

#include "hub/module.hpp"

namespace vitahub {

/// The hub's own screen: a sidebar with Home, one page per service and
/// Settings. It is always the bottom activity; every module's UI is pushed
/// on top of it.
class HubActivity : public brls::Activity {
  public:
    brls::View* createContentView() override;
    void onContentAvailable() override;

    /// A module's last activity was popped (Circle on its root screen, or
    /// returnToHub()): the hub is on screen again.
    void onResume() override;

  private:
    brls::TabFrame* m_tabs = nullptr;
};

/// Start (if needed) and show `module` on top of the hub.
void openModule(Module* module);

/// Module that currently owns the screen, or nullptr when the hub is showing.
Module* currentModule();

/// Called by a module between loading its own settings/theme and pushing its
/// first activity, so the unified theme is in place before any view is built.
void paintModuleTheme(const Module& module);

/// Build the "card" row used for a service on the hub home and the services tab.
brls::Box* createServiceCard(Module* module, bool compact);

}  // namespace vitahub
