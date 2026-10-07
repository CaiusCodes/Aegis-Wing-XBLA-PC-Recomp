#include "aegis_wing_title_prompt.h"

#include <atomic>
#include <bitset>
#include <chrono>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "generated/default/aegis_wing_init.h"
#include "aegis_wing_app.h"

namespace aegis_wing {
namespace {

using Clock = std::chrono::steady_clock;

// Starts true so the retail prompt shows until the first check has run.
std::atomic<bool> gamepad_connected{true};
// When the title draw code last showed the prompt (steady-clock ticks).
std::atomic<int64_t> title_prompt_seen_at{0};

constexpr auto kTitleStillShowing = std::chrono::milliseconds(250);

bool TitlePromptShowing() {
  const auto window = std::chrono::duration_cast<Clock::duration>(kTitleStillShowing).count();
  return Clock::now().time_since_epoch().count() -
             title_prompt_seen_at.load(std::memory_order_relaxed) <
         window;
}

bool AnyKeyApplies() {
  return TitlePromptShowing() && !gamepad_connected.load(std::memory_order_relaxed);
}

// Keys bound to A (keybind_a, comma-separated) already continue on their own.
bool IsBoundToA(rex::ui::VirtualKey key) {
  const std::string binds = rex::cvar::GetFlagByName("keybind_a");
  size_t start = 0;
  while (start <= binds.size()) {
    size_t end = binds.find(',', start);
    if (end == std::string::npos) end = binds.size();
    std::string_view name(binds.data() + start, end - start);
    while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
    while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
    if (!name.empty() && rex::ui::ParseVirtualKey(name) == key) {
      return true;
    }
    start = end + 1;
  }
  return false;
}

// The title screen reads XamInputGetKeystroke and accepts only A, so any
// other key or click becomes one A keystroke.
void PressA() {
  rex::input::mnk::MnkInputDriver::InjectPadKeystroke(
      static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadA));
}

// Sees every key and click before the game's keyboard/mouse driver (which
// listens at z-order 0). A key or click it turns into A is consumed, press
// and release, so its own binding (Enter is Start, the arrows are the D-pad)
// cannot also reach the main menu that the A opens.
class TitleInputListener final : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    const auto key = static_cast<uint16_t>(e.virtual_key());
    if (key >= consumed_keys_.size()) {
      return;
    }
    if (AnyKeyApplies() && !IsBoundToA(e.virtual_key())) {
      if (!consumed_keys_.test(key)) {
        PressA();
      }
      consumed_keys_.set(key);
      e.set_handled(true);
      return;
    }
    // Held past the title: the driver sees it from here on, release included.
    consumed_keys_.reset(key);
  }
  void OnKeyUp(rex::ui::KeyEvent& e) override {
    const auto key = static_cast<uint16_t>(e.virtual_key());
    if (key < consumed_keys_.size() && consumed_keys_.test(key)) {
      consumed_keys_.reset(key);
      e.set_handled(true);
    }
  }
  void OnMouseDown(rex::ui::MouseEvent& e) override {
    if (!AnyKeyApplies()) {
      return;
    }
    // Where the title counts as a menu, a left click is already A.
    if (e.button() == rex::ui::MouseEvent::Button::kLeft && AegisWingApp::IsMenuUp()) {
      return;
    }
    PressA();
    consumed_buttons_.set(static_cast<size_t>(e.button()));
    e.set_handled(true);
  }
  void OnMouseUp(rex::ui::MouseEvent& e) override {
    const auto button = static_cast<size_t>(e.button());
    if (button < consumed_buttons_.size() && consumed_buttons_.test(button)) {
      consumed_buttons_.reset(button);
      e.set_handled(true);
    }
  }

 private:
  std::bitset<256> consumed_keys_;
  std::bitset<8> consumed_buttons_;
};

TitleInputListener title_input_listener;

}  // namespace

void AttachTitlePrompt(rex::ui::Window* window) {
  if (window) {
    // Above the input drivers, so the key reaches this listener first.
    window->AddInputListener(&title_input_listener, 1000);
  }
}

bool TitleShowsGamepadPrompt() {
  title_prompt_seen_at.store(Clock::now().time_since_epoch().count(),
                             std::memory_order_relaxed);
  return gamepad_connected.load(std::memory_order_relaxed);
}

void ApplyTitlePromptText(uint32_t buffer) {
  if (gamepad_connected.load(std::memory_order_relaxed)) {
    return;
  }
  auto* memory = AegisWingApp::GetGuestMemory();
  if (!memory || !buffer) {
    return;
  }
  // The guest string is UTF-16BE; the retail one ("Press    to continue")
  // is longer, so the buffer has room. The game draws this line left-aligned
  // where the retail prompt starts; four leading spaces (about the width the
  // A icon took) centre the shorter text under the logo as the retail line was.
  constexpr std::string_view kText = "    Press Any Key";
  auto* out = memory->TranslateVirtual<uint8_t*>(buffer);
  for (size_t i = 0; i < kText.size(); ++i) {
    out[i * 2] = 0;
    out[i * 2 + 1] = static_cast<uint8_t>(kText[i]);
  }
  out[kText.size() * 2] = 0;
  out[kText.size() * 2 + 1] = 0;
}

void UpdateTitlePrompt() {
  auto* input_system =
      static_cast<rex::input::InputSystem*>(AegisWingApp::GetPcInputSystem());
  if (input_system) {
    gamepad_connected.store(input_system->HasConnectedGamepad(), std::memory_order_relaxed);
  }
}

}  // namespace aegis_wing
