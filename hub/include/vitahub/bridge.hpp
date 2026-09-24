/**
 * The part of the hub that module code may call.
 *
 * Kept deliberately small and free of hub internals: the modules are imported
 * from their standalone repos, and every call into the hub is a hand edit
 * recorded in tools/patches/<module>.patch.
 */

#pragma once

#include <cstddef>

namespace brls {
class View;
}

namespace vitahub {

/// Pop every activity above the hub's own, landing back on the hub.
void returnToHub();

/// Leave the current module and open another one ("plex", "abs", ...).
void switchToModule(const char* id);

/// Number of activities the hub keeps below a module's first activity.
/// Module code that asked "am I the root activity?" with
/// `getActivitiesStack().size() > 1` now asks `> hubStackDepth() + 1`.
std::size_t hubStackDepth();

/// Content view for the "VitaHub" sidebar tab every module adds to its own
/// sidebar: jump to another service or back to the hub.
brls::View* createServicesTab(const char* currentModuleId);

}  // namespace vitahub
