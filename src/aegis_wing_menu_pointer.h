#pragma once

#include <cstdint>

namespace rex::memory {
class Memory;
}

namespace aegis_wing {

// Mouse pointing in the retail XUI menus: moving the mouse over a menu row
// gives it the focus, exactly as if the stick had moved there. Runs on the
// guest thread (the MnK driver's poll hook), where it can call the title's
// own XUI focus routine. `menu_up` says whether a menu is in front.
void UpdateMenuPointer(rex::memory::Memory* memory, bool menu_up);

// True while the front scene is the in-level one (CIngameMenu): a live XUI
// scene that only waits for the pause button, so it is gameplay, not a menu.
bool IsLevelSceneInFront(rex::memory::Memory* memory);

}  // namespace aegis_wing
