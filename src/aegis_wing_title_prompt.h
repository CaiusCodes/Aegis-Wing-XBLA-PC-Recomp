#pragma once

#include <cstdint>

namespace rex::ui {
class Window;
}

// The title screen's "Press (A) to continue" prompt. With a gamepad connected
// it stays as the game draws it. Without one, the A icon is skipped, the text
// reads "Press Any Key", and any key or mouse click continues.
namespace aegis_wing {

// Starts listening for keys and clicks on the game window.
void AttachTitlePrompt(rex::ui::Window* window);

// Called by the title screen's draw code each frame the prompt is shown.
// Returns whether to draw the gamepad A icon.
bool TitleShowsGamepadPrompt();

// Called right after the title screen copies its prompt string into the
// guest buffer at `buffer`; replaces it when no gamepad is connected.
void ApplyTitlePromptText(uint32_t buffer);

// UI thread, once per frame: refreshes whether a gamepad is connected.
void UpdateTitlePrompt();

}  // namespace aegis_wing
