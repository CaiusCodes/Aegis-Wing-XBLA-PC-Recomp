#include "aegis_wing_pc_settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include "generated/default/aegis_wing_init.h"
#include "aegis_wing_app.h"
#include "aegis_wing_player_names.h"
#include "aegis_wing_title_prompt.h"
#include "aegis_wing_update_check.h"

namespace {

constexpr float kDesignWidth = 1280.0f;
constexpr float kDesignHeight = 720.0f;

enum Row : int {
  kRowPlayerName,
  kRowSoundVolume,
  kRowMusicVolume,
  kRowDisplayMode,
  kRowResolution,
  kRowRenderScale,
  kRowVSync,
  kRowUpscaling,
  kRowAntiAliasing,
  kRowShowFps,
  kRowDisplay,
  kRowKeyboard,
  kRowUpdates,
  kRowCount
};
constexpr std::array<std::string_view, 3> kUpscalingNames = {"Off", "Sharpen", "FSR"};
constexpr std::array<std::string_view, 3> kAntiAliasingNames = {"Off", "FXAA", "FXAA Extreme"};
constexpr std::array<std::string_view, 3> kRenderScaleNames = {"1x", "2x", "3x"};
constexpr std::array<std::string_view, kRowCount> kLabels = {
    "Player Name", "Sound Volume", "Music Volume", "Display Mode", "Resolution",
    "Render Scale", "VSync", "Upscaling", "Anti-Aliasing", "Show FPS", "Display", "Keyboard Controls", "Check for Updates"};
constexpr std::array<std::string_view, kRowCount> kDescriptions = {
    "Your name in LAN games and high scores. Press Enter or click it to type one.",
    "Adjust the volume of sound effects.",
    "Adjust the volume of the soundtrack.",
    "Choose between a desktop window and full-screen play.",
    "Set the output resolution used by the PC version.",
    "Draws the game sharper on faster graphics cards. Leaving Settings restarts the game.",
    "Synchronize frames to the display to prevent screen tearing.",
    "Off keeps the original look; Sharpen (AMD CAS) and FSR (AMD FSR 1) sharpen it.",
    "Smooths jagged edges. FXAA is light; FXAA Extreme smooths more.",
    "Display the live frame rate in the corner of the screen.",
    "Window, resolution, render scale, VSync, upscaling, anti-aliasing and FPS counter.",
    "Choose the keys and mouse buttons used for each controller button.",
    "Asks GitHub whether a newer version of this PC port is out."};

// Settings shows the player and audio rows and opens two pages: Display
// (these rows) and Keyboard Controls (below).
constexpr std::array<int, 6> kMainRows = {kRowPlayerName, kRowSoundVolume, kRowMusicVolume,
                                          kRowDisplay, kRowKeyboard, kRowUpdates};
constexpr std::array<int, 7> kDisplayRows = {kRowDisplayMode, kRowResolution, kRowRenderScale,
                                             kRowVSync,       kRowUpscaling,  kRowAntiAliasing,
                                             kRowShowFps};

bool OpensPage(int row) { return row == kRowDisplay || row == kRowKeyboard; }
// Rows that A and Enter act on instead of closing Settings.
bool ActsOnConfirm(int row) { return OpensPage(row) || row == kRowUpdates; }

// Check for Updates: checks, or once a newer version is known, opens its page.
void ActivateUpdates() {
  if (aegis_wing::GetUpdateState() == aegis_wing::UpdateState::kAvailable) {
    aegis_wing::OpenReleasesPage();
  } else {
    aegis_wing::StartUpdateCheck();
  }
}

// Keyboard Controls page: the keyboard and mouse binds of the controller
// buttons (the SDK's keybind_* settings, read on every input poll, so a
// change applies at once). Two columns of seven cells; the last cell resets.
struct KeyBind {
  const char* cvar;
  const char* label;
  const char* fallback;  // this port's default (see AegisWingApp::OnPreSetup)
};
// The first kKeyRows have a cell on the page. The rest have none but give up
// a key that is assigned elsewhere, like every other bind.
constexpr std::array<KeyBind, 20> kAllBinds = {{
    {"keybind_a", "Fire / Confirm (A)", "Space"},
    {"keybind_b", "Back (B)", "Shift"},
    {"keybind_x", "X Button", "X,R"},
    {"keybind_y", "Y Button", "Y,E"},
    {"keybind_start", "Pause / Start", "Escape,Return"},
    {"keybind_left_shoulder", "Left Bumper", "Q"},
    {"keybind_right_shoulder", "Right Bumper", "F"},
    {"keybind_left_trigger", "Left Trigger", "RMB"},
    {"keybind_right_trigger", "Right Trigger", "LMB"},
    {"keybind_dpad_up", "Up", "Up"},
    {"keybind_dpad_down", "Down", "Down"},
    {"keybind_dpad_left", "Left", "Left"},
    {"keybind_dpad_right", "Right", "Right"},
    {"keybind_lstick_up", "Stick Up", "W"},
    {"keybind_lstick_down", "Stick Down", "S"},
    {"keybind_lstick_left", "Stick Left", "A"},
    {"keybind_lstick_right", "Stick Right", "D"},
    {"keybind_lstick_press", "Left Stick Press", "C"},
    {"keybind_rstick_press", "Right Stick Press", "MMB"},
    {"keybind_back", "Back Button", "Tab"},
}};
constexpr int kKeyRows = 13;
constexpr int kKeyResetCell = kKeyRows;
constexpr int kKeyCells = kKeyRows + 1;
constexpr int kKeyColumnRows = 7;

std::vector<std::string> SplitBind(const std::string& bind) {
  std::vector<std::string> keys;
  size_t start = 0;
  while (start <= bind.size()) {
    const size_t comma = bind.find(',', start);
    std::string one = bind.substr(start, comma == std::string::npos ? std::string::npos
                                                                    : comma - start);
    while (!one.empty() && one.front() == ' ') one.erase(one.begin());
    while (!one.empty() && one.back() == ' ') one.pop_back();
    if (!one.empty()) keys.push_back(one);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return keys;
}

std::string JoinBind(const std::vector<std::string>& keys) {
  std::string bind;
  for (const auto& key : keys) bind += (bind.empty() ? "" : ",") + key;
  return bind;
}

bool SameKey(const std::string& a, const std::string& b) {
  const auto va = rex::ui::ParseVirtualKey(a);
  if (va != rex::ui::VirtualKey::kNone) return va == rex::ui::ParseVirtualKey(b);
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

// The bind as players read it: "Escape,Return" is "Esc, Enter".
std::string KeyDisplayName(const std::string& key) {
  if (key == "LMB") return "Left Click";
  if (key == "RMB") return "Right Click";
  if (key == "MMB") return "Middle Click";
  if (key == "Return") return "Enter";
  if (key == "Escape") return "Esc";
  return key;
}

std::string BindDisplay(const std::string& bind) {
  std::string out;
  for (const auto& key : SplitBind(bind)) out += (out.empty() ? "" : ", ") + KeyDisplayName(key);
  return out.empty() ? "None" : out;
}

// ImGui key to the SDK's key names (as the SDK's settings overlay does).
rex::ui::VirtualKey ImGuiKeyToVirtualKey(ImGuiKey key) {
  using VK = rex::ui::VirtualKey;
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
    return static_cast<VK>(static_cast<uint16_t>(VK::kA) + (key - ImGuiKey_A));
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
    return static_cast<VK>(static_cast<uint16_t>(VK::k0) + (key - ImGuiKey_0));
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24)
    return static_cast<VK>(static_cast<uint16_t>(VK::kF1) + (key - ImGuiKey_F1));
  if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9)
    return static_cast<VK>(static_cast<uint16_t>(VK::kNumpad0) + (key - ImGuiKey_Keypad0));
  switch (key) {
    case ImGuiKey_Space: return VK::kSpace;
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter: return VK::kReturn;
    case ImGuiKey_Tab: return VK::kTab;
    case ImGuiKey_Backspace: return VK::kBack;
    case ImGuiKey_Delete: return VK::kDelete;
    case ImGuiKey_Insert: return VK::kInsert;
    case ImGuiKey_Home: return VK::kHome;
    case ImGuiKey_End: return VK::kEnd;
    case ImGuiKey_PageUp: return VK::kPrior;
    case ImGuiKey_PageDown: return VK::kNext;
    case ImGuiKey_LeftArrow: return VK::kLeft;
    case ImGuiKey_RightArrow: return VK::kRight;
    case ImGuiKey_UpArrow: return VK::kUp;
    case ImGuiKey_DownArrow: return VK::kDown;
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift: return VK::kShift;
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl: return VK::kControl;
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt: return VK::kMenu;
    case ImGuiKey_CapsLock: return VK::kCapital;
    case ImGuiKey_GraveAccent: return VK::kOem3;
    case ImGuiKey_Minus: return VK::kOemMinus;
    case ImGuiKey_Equal: return VK::kOemPlus;
    case ImGuiKey_LeftBracket: return VK::kOem4;
    case ImGuiKey_RightBracket: return VK::kOem6;
    case ImGuiKey_Backslash: return VK::kOem5;
    case ImGuiKey_Semicolon: return VK::kOem1;
    case ImGuiKey_Apostrophe: return VK::kOem7;
    case ImGuiKey_Comma: return VK::kOemComma;
    case ImGuiKey_Period: return VK::kOemPeriod;
    case ImGuiKey_Slash: return VK::kOem2;
    default: return VK::kNone;
  }
}

bool IsKeyboardKey(int k) {
  return !(k >= ImGuiKey_GamepadStart && k <= ImGuiKey_GamepadRStickDown) &&
         !(k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY);
}

bool AnyKeyboardKeyDown() {
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
    if (IsKeyboardKey(k) && ImGui::IsKeyDown(static_cast<ImGuiKey>(k))) return true;
  }
  return false;
}

// An Xbox gamertag: up to 15 characters. Plain printable ASCII (the game's
// fonts and the LAN protocol both expect it), without quotes or backslashes
// so the name always reads back cleanly from aegis_wing.toml.
constexpr size_t kMaxPlayerName = 15;

bool IsPlayerNameChar(unsigned int c) {
  return c >= 0x20 && c <= 0x7E && c != '"' && c != '\\';
}

std::string TrimName(std::string name) {
  const size_t first = name.find_first_not_of(' ');
  if (first == std::string::npos) {
    return {};
  }
  const size_t last = name.find_last_not_of(' ');
  return name.substr(first, last - first + 1);
}

bool SameName(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}
constexpr std::array<std::string_view, 4> kResolutions = {
    "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160"};

struct Layout {
  float scale;
  ImVec2 offset;

  ImVec2 Point(float x, float y) const {
    return ImVec2(offset.x + x * scale, offset.y + y * scale);
  }
};

void DrawTextShadow(ImDrawList* draw_list, ImFont* font, float size,
                    const ImVec2& position, ImU32 color,
                    std::string_view text, float shadow_offset) {
  if (text.empty()) {
    return;  // see DrawCenteredText
  }
  draw_list->AddText(font, size,
                     ImVec2(position.x + shadow_offset,
                            position.y + shadow_offset),
                     IM_COL32(0, 0, 0, 210), text.data(),
                     text.data() + text.size());
  draw_list->AddText(font, size, position, color, text.data(),
                     text.data() + text.size());
}

void DrawCenteredText(ImDrawList* draw_list, ImFont* font, float size,
                      const ImVec2& center, ImU32 color,
                      std::string_view text, float shadow_offset = 0.0f) {
  // ImGui measures a null text pointer with strlen: nothing to draw anyway.
  if (text.empty()) {
    return;
  }
  const ImVec2 measured = font->CalcTextSizeA(
      size, 10000.0f, 0.0f, text.data(), text.data() + text.size());
  const ImVec2 position(center.x - measured.x * 0.5f,
                        center.y - measured.y * 0.5f);
  if (shadow_offset > 0.0f) {
    DrawTextShadow(draw_list, font, size, position, color, text,
                   shadow_offset);
  } else {
    draw_list->AddText(font, size, position, color, text.data(),
                       text.data() + text.size());
  }
}

enum GamepadButtonBits : uint32_t {
  kPadUp = 1u << 0,
  kPadDown = 1u << 1,
  kPadLeft = 1u << 2,
  kPadRight = 1u << 3,
  kPadConfirm = 1u << 4,
  kPadBack = 1u << 5,
};

uint32_t ReadGamepadButtons() {
  auto* input_system = static_cast<rex::input::InputSystem*>(
      AegisWingApp::GetPcInputSystem());
  if (!input_system) {
    return 0;
  }

  uint32_t buttons = 0;

  // Use the same merged Xbox input state exposed to the recompiled game.
  // Check every local slot so the settings page also works when the first
  // active controller was assigned to a slot other than zero.
  for (uint32_t user = 0; user < 4; ++user) {
    rex::input::X_INPUT_STATE state{};
    if (input_system->GetState(user, &state) != 0) {
      continue;
    }

    const uint16_t xbox_buttons =
        static_cast<uint16_t>(state.gamepad.buttons);
    const int16_t thumb_x = static_cast<int16_t>(state.gamepad.thumb_lx);
    const int16_t thumb_y = static_cast<int16_t>(state.gamepad.thumb_ly);
    constexpr int16_t kStickThreshold = 16000;

    if ((xbox_buttons & rex::input::X_INPUT_GAMEPAD_DPAD_UP) ||
        thumb_y > kStickThreshold)
      buttons |= kPadUp;
    if ((xbox_buttons & rex::input::X_INPUT_GAMEPAD_DPAD_DOWN) ||
        thumb_y < -kStickThreshold)
      buttons |= kPadDown;
    if ((xbox_buttons & rex::input::X_INPUT_GAMEPAD_DPAD_LEFT) ||
        thumb_x < -kStickThreshold)
      buttons |= kPadLeft;
    if ((xbox_buttons & rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT) ||
        thumb_x > kStickThreshold)
      buttons |= kPadRight;
    if (xbox_buttons & rex::input::X_INPUT_GAMEPAD_A)
      buttons |= kPadConfirm;
    if (xbox_buttons & rex::input::X_INPUT_GAMEPAD_B)
      buttons |= kPadBack;
  }
  return buttons;
}

}  // namespace

AegisWingPcSettingsDialog::AegisWingPcSettingsDialog(
    rex::ui::ImGuiDrawer* drawer)
    : ImGuiDialog(drawer) {}

void AegisWingPcSettingsDialog::Open() {
  sound_volume_ = AegisWingApp::GetPcSoundVolume();
  music_volume_ = AegisWingApp::GetPcMusicVolume();
  display_mode_ = AegisWingApp::GetPcFullscreen() ? 1 : 0;
  resolution_ = AegisWingApp::GetPcResolutionIndex();
  vsync_ = AegisWingApp::GetPcVSync();
  upscaling_ = AegisWingApp::GetPcUpscalingIndex();
  render_scale_ = AegisWingApp::GetPcRenderScale();
  anti_aliasing_ = AegisWingApp::GetPcAntiAliasingIndex();
  show_fps_ = AegisWingApp::GetPcShowFps();
  player_name_ = AegisWingApp::GetPcPlayerName();
  editing_name_ = false;
  keys_page_ = false;
  display_page_ = false;
  capturing_cell_ = -1;
  // Start on Sound Volume, as before; Player Name sits above it.
  selected_row_ = kRowSoundVolume;
  closing_ = false;
  previous_gamepad_buttons_ = ReadGamepadButtons();
  request_focus_ = true;
  visible_ = true;
  UpdateGuestInputBlock();
}

void AegisWingPcSettingsDialog::Close() {
  visible_ = false;
  closing_ = false;
  editing_name_ = false;
  keys_page_ = false;
  display_page_ = false;
  capturing_cell_ = -1;
  UpdateGuestInputBlock();
}

AegisWingPcSettingsDialog::GuestInputBlock
AegisWingPcSettingsDialog::CurrentGuestInputBlock() {
  const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
  if (now < block_all_until_.load(std::memory_order_relaxed)) {
    return kBlockAll;
  }
  return static_cast<GuestInputBlock>(guest_input_block_.load(std::memory_order_relaxed));
}

void AegisWingPcSettingsDialog::UpdateGuestInputBlock() {
  GuestInputBlock block = kBlockNone;
  if (visible_ && !closing_) {
    if (editing_name_ || keys_page_ || display_page_) {
      block = kBlockAll;
    } else if (selected_row_ == kRowPlayerName || ActsOnConfirm(selected_row_)) {
      block = kBlockConfirm;
    }
  }
  guest_input_block_.store(block, std::memory_order_relaxed);
}

void AegisWingPcSettingsDialog::EnsureGuestInputFilters() {
  if (guest_filters_registered_) {
    return;
  }
  auto* input_system = static_cast<rex::input::InputSystem*>(
      AegisWingApp::GetPcInputSystem());
  if (!input_system) {
    return;
  }
  guest_filters_registered_ = true;
  input_system->SetKeystrokeFilterCallback(
      [](const rex::input::X_INPUT_KEYSTROKE& keystroke) {
        switch (CurrentGuestInputBlock()) {
          case kBlockAll:
            return false;
          case kBlockConfirm: {
            const uint16_t vk = keystroke.virtual_key;
            return vk != static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadA) &&
                   vk != static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadStart);
          }
          default:
            return true;
        }
      });
  // Held buttons too, but only for the game's own threads: this panel reads
  // the same merged state from the UI thread and must keep seeing it.
  input_system->SetButtonFilterCallback([](uint16_t buttons) -> uint16_t {
    if (!rex::runtime::ThreadState::Get()) {
      return buttons;
    }
    switch (CurrentGuestInputBlock()) {
      case kBlockAll:
        return 0;
      case kBlockConfirm:
        return static_cast<uint16_t>(buttons & ~(rex::input::X_INPUT_GAMEPAD_A |
                                                 rex::input::X_INPUT_GAMEPAD_START));
      default:
        return buttons;
    }
  });
}

void AegisWingPcSettingsDialog::StepPlayerName(int direction) {
  const int count = static_cast<int>(std::size(kAegisWingPlayerNames));
  if (count == 0) {
    return;
  }
  int index = -1;
  for (int i = 0; i < count; ++i) {
    if (SameName(kAegisWingPlayerNames[i], player_name_)) {
      index = i;
      break;
    }
  }
  // A name of the player's own: Right starts at the first preset, Left at
  // the last.
  if (index < 0) {
    index = direction > 0 ? 0 : count - 1;
  } else {
    index = (index + direction + count) % count;
  }
  player_name_ = std::string(kAegisWingPlayerNames[index]);
  AegisWingApp::SetPcPlayerName(player_name_);
}

void AegisWingPcSettingsDialog::BeginNameEdit() {
  editing_name_ = true;
  name_edit_ = player_name_.substr(0, kMaxPlayerName);
  name_edit_started_ = std::chrono::steady_clock::now();
  UpdateGuestInputBlock();
}

void AegisWingPcSettingsDialog::EndNameEdit(bool keep) {
  if (keep) {
    const std::string name = TrimName(name_edit_);
    if (!name.empty() && name != player_name_) {
      player_name_ = name;
      AegisWingApp::SetPcPlayerName(player_name_);
    }
  }
  editing_name_ = false;
  // The keys just typed may not all have been read off the input queue yet.
  block_all_until_.store(
      (std::chrono::steady_clock::now() + std::chrono::milliseconds(400))
          .time_since_epoch()
          .count(),
      std::memory_order_relaxed);
  // Held keys (Shift is B, Space is A) must not register as fresh presses.
  previous_gamepad_buttons_ = ReadGamepadButtons();
  UpdateGuestInputBlock();
}

void AegisWingPcSettingsDialog::UpdateNameEdit(ImGuiIO& io) {
  for (const ImWchar c : io.InputQueueCharacters) {
    if (IsPlayerNameChar(c) && name_edit_.size() < kMaxPlayerName) {
      name_edit_.push_back(static_cast<char>(c));
    }
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !name_edit_.empty()) {
    name_edit_.pop_back();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
      ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
    EndNameEdit(true);
  } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right, false)) {
    EndNameEdit(false);
  }
}

void AegisWingPcSettingsDialog::ShowHelpOptionsLabels() {
  if (!help_options_context_active_) {
    previous_help_gamepad_buttons_ = ReadGamepadButtons();
  }
  help_options_context_active_ = true;
  help_options_labels_visible_ = true;
  help_back_pending_ = false;
}

void AegisWingPcSettingsDialog::HideHelpOptionsLabels() {
  help_options_labels_visible_ = false;
  help_back_pending_ = false;
  previous_help_gamepad_buttons_ = ReadGamepadButtons();
}

void AegisWingPcSettingsDialog::CloseHelpOptionsLabels() {
  help_options_labels_visible_ = false;
  help_options_context_active_ = false;
  help_back_pending_ = false;
  help_front_ = 0;
  previous_help_gamepad_buttons_ = 0;
}

void AegisWingPcSettingsDialog::ShowPauseMenuLabels() {
  pause_labels_pending_ = false;
  pause_menu_labels_visible_ = true;
}

void AegisWingPcSettingsDialog::HidePauseMenuLabels() {
  // A new pause choice supersedes any re-show still waiting on a fade-out,
  // otherwise it could land on top of the Help parent or the confirm.
  pause_labels_pending_ = false;
  pause_menu_labels_visible_ = false;
}

void AegisWingPcSettingsDialog::ClearPauseMenuLabels() {
  pause_labels_pending_ = false;
  pause_menu_labels_visible_ = false;
}

void AegisWingPcSettingsDialog::SchedulePauseMenuLabels(double delay_seconds) {
  pause_menu_labels_visible_ = false;
  pause_labels_pending_ = true;
  pause_labels_show_at_ =
      std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(delay_seconds));
}

void AegisWingPcSettingsDialog::ChangeSelectedSetting(int direction) {
  switch (selected_row_) {
    case kRowPlayerName:
      StepPlayerName(direction);
      break;
    case kRowSoundVolume:
      sound_volume_ = std::clamp(sound_volume_ + direction * 5, 0, 100);
      AegisWingApp::SetPcSoundVolume(sound_volume_);
      break;
    case kRowMusicVolume:
      music_volume_ = std::clamp(music_volume_ + direction * 5, 0, 100);
      AegisWingApp::SetPcMusicVolume(music_volume_);
      break;
    case kRowDisplayMode:
      display_mode_ = display_mode_ == 0 ? 1 : 0;
      AegisWingApp::SetPcFullscreen(display_mode_ == 1);
      break;
    case kRowResolution:
      resolution_ = (resolution_ + direction +
                     static_cast<int>(kResolutions.size())) %
                    static_cast<int>(kResolutions.size());
      AegisWingApp::SetPcResolutionIndex(resolution_);
      break;
    case kRowRenderScale:
      render_scale_ = (render_scale_ - 1 + direction + 3) % 3 + 1;
      AegisWingApp::SetPcRenderScale(render_scale_);
      break;
    case kRowVSync:
      vsync_ = !vsync_;
      AegisWingApp::SetPcVSync(vsync_);
      break;
    case kRowUpscaling:
      upscaling_ = (upscaling_ + direction + 3) % 3;
      AegisWingApp::SetPcUpscalingIndex(upscaling_);
      break;
    case kRowAntiAliasing:
      anti_aliasing_ = (anti_aliasing_ + direction + 3) % 3;
      AegisWingApp::SetPcAntiAliasingIndex(anti_aliasing_);
      break;
    case kRowShowFps:
      show_fps_ = !show_fps_;
      AegisWingApp::SetPcShowFps(show_fps_);
      break;
    case kRowDisplay:
      OpenDisplayPage();
      break;
    case kRowKeyboard:
      OpenKeysPage();
      break;
    case kRowUpdates:
      if (direction > 0) ActivateUpdates();
      break;
    default:
      break;
  }
}

void AegisWingPcSettingsDialog::TestSelectUpdatesRow() {
  if (visible_ && !closing_ && !keys_page_ && !display_page_) {
    selected_row_ = kRowUpdates;
    UpdateGuestInputBlock();
  }
}

void AegisWingPcSettingsDialog::OpenDisplayPage() {
  display_page_ = true;
  selected_row_ = kRowDisplayMode;
  UpdateGuestInputBlock();
}

void AegisWingPcSettingsDialog::CloseDisplayPage() {
  display_page_ = false;
  selected_row_ = kRowDisplay;
  // Held keys must not register as fresh presses on the main page.
  previous_gamepad_buttons_ = ReadGamepadButtons();
  block_all_until_.store(
      (std::chrono::steady_clock::now() + std::chrono::milliseconds(300))
          .time_since_epoch()
          .count(),
      std::memory_order_relaxed);
  UpdateGuestInputBlock();
}

// Up and down move through the rows of the page that is showing.
void AegisWingPcSettingsDialog::StepRow(int direction) {
  const int* rows = display_page_ ? kDisplayRows.data() : kMainRows.data();
  const int count = static_cast<int>(display_page_ ? kDisplayRows.size() : kMainRows.size());
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (rows[i] == selected_row_) index = i;
  }
  selected_row_ = rows[(index + direction + count) % count];
}

void AegisWingPcSettingsDialog::OpenKeysPage() {
  keys_page_ = true;
  key_cell_ = 0;
  capturing_cell_ = -1;
  keys_note_.clear();
  UpdateGuestInputBlock();
}

void AegisWingPcSettingsDialog::CloseKeysPage() {
  keys_page_ = false;
  display_page_ = false;
  capturing_cell_ = -1;
  // Held keys must not register as fresh presses on the main page.
  previous_gamepad_buttons_ = ReadGamepadButtons();
  block_all_until_.store(
      (std::chrono::steady_clock::now() + std::chrono::milliseconds(300))
          .time_since_epoch()
          .count(),
      std::memory_order_relaxed);
  UpdateGuestInputBlock();
}

void AegisWingPcSettingsDialog::ActivateKeyCell(int cell) {
  key_cell_ = cell;
  if (cell == kKeyResetCell) {
    for (const auto& bind : kAllBinds) rex::cvar::SetFlagByName(bind.cvar, bind.fallback);
    AegisWingApp::SavePcSettingsNow();
    keys_note_ = "Every key is back to its default.";
    return;
  }
  capturing_cell_ = cell;
  capture_armed_ = false;
}

// A key on one bind only: the new key leaves every other bind it was on.
void AegisWingPcSettingsDialog::AssignKey(int cell, const std::string& key) {
  const KeyBind& target = kAllBinds[static_cast<size_t>(cell)];
  std::string moved;
  for (const auto& bind : kAllBinds) {
    if (&bind == &target) continue;
    auto keys = SplitBind(rex::cvar::GetFlagByName(bind.cvar));
    const size_t before = keys.size();
    keys.erase(std::remove_if(keys.begin(), keys.end(),
                              [&](const std::string& k) { return SameKey(k, key); }),
               keys.end());
    if (keys.size() == before) continue;
    rex::cvar::SetFlagByName(bind.cvar, JoinBind(keys));
    moved += std::string(moved.empty() ? "" : ", ") + bind.label +
             (keys.empty() ? " (now None)" : "");
  }
  rex::cvar::SetFlagByName(target.cvar, key);
  AegisWingApp::SavePcSettingsNow();
  keys_note_ = KeyDisplayName(key) + " is now " + target.label + ".";
  if (!moved.empty()) keys_note_ += " Taken from " + moved + ".";
}

// The first key or mouse button pressed, by its SDK name.
bool AegisWingPcSettingsDialog::CaptureKey(std::string* key) {
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
    const auto imgui_key = static_cast<ImGuiKey>(k);
    if (imgui_key == ImGuiKey_Escape || !IsKeyboardKey(k) ||
        !ImGui::IsKeyPressed(imgui_key, false)) {
      continue;
    }
    const std::string name = rex::ui::VirtualKeyToString(ImGuiKeyToVirtualKey(imgui_key));
    if (!name.empty()) {
      *key = name;
      return true;
    }
  }
  static const char* const kMouseNames[] = {"LMB", "RMB", "MMB", "Mouse4", "Mouse5"};
  for (int button = 0; button < 5; ++button) {
    if (ImGui::IsMouseClicked(button, false)) {
      *key = kMouseNames[button];
      return true;
    }
  }
  return false;
}

void AegisWingPcSettingsDialog::UpdateKeysPage(uint32_t gamepad_pressed) {
  if (capturing_cell_ >= 0) {
    // Wait until whatever started the capture (Enter, Space, A, a click) is
    // released, so it is not taken as the new key.
    if (!capture_armed_) {
      capture_armed_ = !AnyKeyboardKeyDown() && !ImGui::IsAnyMouseDown() &&
                       ReadGamepadButtons() == 0;
      return;
    }
    // Esc cancels. So does B on a controller, but not Shift (B's own key),
    // which can be the key being assigned.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ((gamepad_pressed & kPadBack) && !AnyKeyboardKeyDown())) {
      capturing_cell_ = -1;
      keys_note_ = "Not changed.";
      return;
    }
    std::string key;
    if (CaptureKey(&key)) {
      AssignKey(capturing_cell_, key);
      capturing_cell_ = -1;
      // A click that was the new key is not also a click on a cell.
      suppress_click_ = true;
    }
    return;
  }
  int column = key_cell_ / kKeyColumnRows;
  int row = key_cell_ % kKeyColumnRows;
  const int before = key_cell_;
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) || (gamepad_pressed & kPadUp)) {
    row = (row + kKeyColumnRows - 1) % kKeyColumnRows;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) || (gamepad_pressed & kPadDown)) {
    row = (row + 1) % kKeyColumnRows;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || (gamepad_pressed & kPadLeft) ||
      ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || (gamepad_pressed & kPadRight)) {
    column = 1 - column;
  }
  key_cell_ = column * kKeyColumnRows + row;
  if (key_cell_ != before) {
    AegisWingApp::PulsePcSettingsNav();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
      ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) || (gamepad_pressed & kPadConfirm)) {
    ActivateKeyCell(key_cell_);
  } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right, false) ||
             (gamepad_pressed & kPadBack)) {
    CloseKeysPage();
  }
}

void AegisWingPcSettingsDialog::DrawKeysPage(ImGuiIO& io, ImDrawList* draw_list, float scale,
                                             ImVec2 offset, ImFont* regular_font,
                                             ImFont* bold_font) {
  const Layout layout{scale, offset};
  DrawCenteredText(draw_list, regular_font, 22.0f * scale, layout.Point(640.0f, 161.0f),
                   IM_COL32(173, 202, 225, 255), "KEYBOARD CONTROLS");

  constexpr float kColumnLeft[2] = {296.0f, 656.0f};
  constexpr float kCellWidth = 328.0f;
  constexpr float kFirstTop = 184.0f;
  constexpr float kStep = 56.0f;
  constexpr float kHeight = 44.0f;
  const ImVec2 mouse = io.MousePos;
  const bool mouse_clicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) && !suppress_click_;
  suppress_click_ = false;
  const bool mouse_moved = std::abs(io.MouseDelta.x) > 0.01f || std::abs(io.MouseDelta.y) > 0.01f;
  const bool capturing = capturing_cell_ >= 0;
  const auto blink = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
  for (int cell = 0; cell < kKeyCells; ++cell) {
    const float left = kColumnLeft[cell / kKeyColumnRows];
    const float top = kFirstTop + (cell % kKeyColumnRows) * kStep;
    const ImVec2 cell_min = layout.Point(left, top);
    const ImVec2 cell_max = layout.Point(left + kCellWidth, top + kHeight);
    const bool hovered = mouse.x >= cell_min.x && mouse.x <= cell_max.x &&
                         mouse.y >= cell_min.y && mouse.y <= cell_max.y;
    // The mouse picks and clicks cells, but not while a key is being
    // captured: then a click is the new key.
    if (!capturing && !closing_ && hovered && (mouse_moved || mouse_clicked)) {
      key_cell_ = cell;
      if (mouse_clicked) ActivateKeyCell(cell);
    }
    const bool selected = cell == key_cell_;
    if (selected) {
      draw_list->AddRectFilled(cell_min, cell_max, IM_COL32(181, 187, 191, 255), 20.0f * scale);
      draw_list->AddRectFilled(layout.Point(left + 6.0f, top + 6.0f),
                               layout.Point(left + kCellWidth - 6.0f, top + kHeight - 6.0f),
                               hovered ? IM_COL32(22, 126, 210, 255) : IM_COL32(12, 103, 187, 255),
                               14.0f * scale);
    } else {
      draw_list->AddRectFilled(cell_min, cell_max,
                               hovered ? IM_COL32(24, 58, 91, 205) : IM_COL32(4, 15, 28, 170),
                               18.0f * scale);
      draw_list->AddRect(cell_min, cell_max, IM_COL32(103, 137, 162, 100), 18.0f * scale, 0,
                         1.5f * scale);
    }
    const float font_size = 20.0f * scale;
    if (cell == kKeyResetCell) {
      DrawCenteredText(draw_list, bold_font, font_size,
                       layout.Point(left + kCellWidth * 0.5f, top + kHeight * 0.5f),
                       IM_COL32(246, 247, 249, 255), "Reset to Defaults", 2.0f * scale);
      continue;
    }
    const KeyBind& bind = kAllBinds[static_cast<size_t>(cell)];
    DrawTextShadow(draw_list, bold_font, font_size, layout.Point(left + 22.0f, top + 11.5f),
                   IM_COL32(246, 247, 249, 255), bind.label, 2.0f * scale);
    std::string value = BindDisplay(rex::cvar::GetFlagByName(bind.cvar));
    ImU32 value_color = value == "None" ? IM_COL32(255, 170, 150, 255) : IM_COL32(244, 248, 252, 255);
    if (cell == capturing_cell_) {
      value = (blink / 400) % 2 == 0 ? "Press a key" : "";
      value_color = IM_COL32(255, 226, 150, 255);
    }
    const ImVec2 measured = regular_font->CalcTextSizeA(font_size, 10000.0f, 0.0f, value.data(),
                                                        value.data() + value.size());
    const ImVec2 right_edge = layout.Point(left + kCellWidth - 22.0f, top + 11.5f);
    DrawTextShadow(draw_list, regular_font, font_size,
                   ImVec2(right_edge.x - measured.x, right_edge.y), value_color, value,
                   1.5f * scale);
  }

  std::string description;
  if (capturing) {
    description = std::string("Press the new key or mouse button for ") +
                  kAllBinds[static_cast<size_t>(capturing_cell_)].label + ". Esc cancels.";
  } else if (!keys_note_.empty()) {
    description = keys_note_;
  } else {
    description = "Choose an action and press Enter or click it. Keys apply at once.";
  }
  DrawCenteredText(draw_list, regular_font, 20.0f * scale, layout.Point(640.0f, 624.0f),
                   IM_COL32(190, 207, 221, 255), description);
  draw_list->AddLine(layout.Point(300.0f, 648.0f), layout.Point(980.0f, 648.0f),
                     IM_COL32(159, 174, 185, 110), 1.5f * scale);
  if (capturing) {
    DrawCenteredText(draw_list, regular_font, 23.0f * scale, layout.Point(640.0f, 682.0f),
                     IM_COL32(240, 242, 245, 255), "Esc  Cancel", 1.5f * scale);
  } else {
    const float a_x = 517.0f, b_x = 697.0f;
    draw_list->AddCircleFilled(layout.Point(a_x, 682.0f), 15.0f * scale,
                               IM_COL32(75, 177, 48, 255));
    DrawCenteredText(draw_list, bold_font, 20.0f * scale, layout.Point(a_x, 682.0f),
                     IM_COL32_WHITE, "A");
    DrawTextShadow(draw_list, regular_font, 23.0f * scale, layout.Point(a_x + 23.0f, 668.0f),
                   IM_COL32(240, 242, 245, 255), "Change Key", 1.5f * scale);
    draw_list->AddCircleFilled(layout.Point(b_x, 682.0f), 15.0f * scale,
                               IM_COL32(200, 46, 46, 255));
    DrawCenteredText(draw_list, bold_font, 20.0f * scale, layout.Point(b_x, 682.0f),
                     IM_COL32_WHITE, "B");
    DrawTextShadow(draw_list, regular_font, 23.0f * scale, layout.Point(b_x + 23.0f, 668.0f),
                   IM_COL32(240, 242, 245, 255), "Back", 1.5f * scale);
  }
}

void AegisWingPcSettingsDialog::DrawVersionLabel(ImGuiIO& io) {
  if (!AegisWingApp::ShouldShowVersionLabel()) {
    return;
  }
  // Small and quiet in the bottom-left corner of the main menu, scaled with
  // the 1280x720 design like the other host captions.
  const float scale = std::max(
      0.5f, std::min(io.DisplaySize.x / kDesignWidth,
                     io.DisplaySize.y / kDesignHeight));
  const ImVec2 offset((io.DisplaySize.x - kDesignWidth * scale) * 0.5f,
                      (io.DisplaySize.y - kDesignHeight * scale) * 0.5f);
  ImFont* font = AegisWingApp::GetPcRegularFont();
  if (!font) font = ImGui::GetFont();
  constexpr std::string_view kVersion = "v" AEGISWING_VERSION;
  const float size = 15.0f * scale;
  const ImVec2 measured = font->CalcTextSizeA(
      size, 10000.0f, 0.0f, kVersion.data(), kVersion.data() + kVersion.size());
  const ImVec2 position(offset.x + 22.0f * scale,
                        offset.y + (kDesignHeight - 18.0f) * scale - measured.y);
  DrawTextShadow(ImGui::GetBackgroundDrawList(), font, size, position,
                 IM_COL32(232, 234, 238, 120), kVersion, 1.0f * scale);
}

void AegisWingPcSettingsDialog::OnDraw(ImGuiIO& io) {
  // In a menu the mouse belongs to it: left click is A, right click is B, the
  // wheel steps the highlight and Esc is Back. In a level the mouse keeps its
  // game binds and Esc is Start (pause).
  rex::input::mnk::MnkInputDriver::SetMenuMouseMode(AegisWingApp::IsMenuUp());
  // Resolve readback only where the menus need it (not in gameplay).
  AegisWingApp::UpdateReadbackForScene();
  AegisWingApp::LogFrontSceneForTests();
  // This panel handles the mouse itself (hover a row, click an arrow), so a
  // click here must not also reach the game as A, which saves and closes it.
  rex::input::mnk::MnkInputDriver::SetMenuClickButtons(!visible_);
  DrawVersionLabel(io);
  // Keeps the title screen's prompt in step with the connected gamepads.
  aegis_wing::UpdateTitlePrompt();

  // After a child scene closes back to the pause menu, the pause captions
  // come back the moment the pause scene is in front again, so two menus
  // never draw on top of each other and there is no blank gap; the delay is
  // only a fallback. The pause scene may be torn down meanwhile (quit to the
  // main menu), so check it is still live.
  if (pause_labels_pending_) {
    if (!AegisWingApp::IsPauseSceneInFront() &&
        std::chrono::steady_clock::now() < pause_labels_show_at_) {
      return;
    }
    pause_labels_pending_ = false;
    if (AegisWingApp::IsPauseSceneLive()) {
      pause_menu_labels_visible_ = true;
    }
  }

  // Exit-confirm overlay: native captions render here and every host caption
  // stays hidden. A or B answers it; the pause menu's own result decides what
  // comes back (see AegisWingApp::OnPauseNavReturn).
  if (AegisWingApp::IsPauseConfirmOpen()) {
    const uint32_t gamepad_buttons = ReadGamepadButtons();
    const uint32_t gamepad_pressed =
        gamepad_buttons & ~previous_confirm_gamepad_buttons_;
    previous_confirm_gamepad_buttons_ = gamepad_buttons;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        (gamepad_pressed & (kPadConfirm | kPadBack)) != 0) {
      AegisWingApp::NotePauseConfirmAnswered();
    }
    return;
  }

  // Leaving Settings: the game has already put Help & Options back in front,
  // so the rows stop now (they drew over it for up to a second while the
  // game finished closing the page) and the Help captions take over.
  if (visible_ && closing_ && closing_front_ != 0) {
    const uint32_t front = AegisWingApp::FrontScene();
    if (help_front_ != 0 ? front == help_front_ : front != closing_front_) {
      Close();
      ShowHelpOptionsLabels();
    }
  }

  if (!visible_ && !help_options_context_active_ &&
      !pause_menu_labels_visible_) {
    return;
  }

  // XUI's automatic B/back path on child pages (notably Credits) bypasses
  // their button-click handlers. Track B through the same Xbox input path and
  // restore the Help captions while the parent Help session remains active.
  if (help_options_context_active_ && !AegisWingApp::IsHelpSceneLive()) {
    // The Help scene closed without telling this layer; drop its captions so
    // a B press on another screen cannot bring them back.
    help_options_context_active_ = false;
    help_options_labels_visible_ = false;
  }
  if (help_options_context_active_ && !help_options_labels_visible_ &&
      !visible_) {
    const uint32_t help_buttons = ReadGamepadButtons();
    const uint32_t help_pressed =
        help_buttons & ~previous_help_gamepad_buttons_;
    previous_help_gamepad_buttons_ = help_buttons;
    // B leaves the child page: the captions come back once Help & Options
    // is in front again, not over the page while it is still showing.
    if ((help_pressed & kPadBack) && !help_back_pending_) {
      help_back_pending_ = true;
      help_back_from_ = AegisWingApp::FrontScene();
      help_back_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
  }
  if (help_back_pending_) {
    const uint32_t front = AegisWingApp::FrontScene();
    const bool back = help_front_ != 0 ? front == help_front_ : front != help_back_from_;
    if (back || std::chrono::steady_clock::now() >= help_back_deadline_) {
      help_back_pending_ = false;
      help_options_labels_visible_ = true;
    }
  }
  // The Help & Options scene, learnt while its captions are up.
  if (help_options_labels_visible_ && !visible_) {
    help_front_ = AegisWingApp::FrontScene();
  }

  // A Help child page keeps the parent Help context alive while its captions
  // are hidden. Do not fall through and draw the settings controls in that
  // state; only the real Settings selection sets visible_.
  if (!visible_ && !help_options_labels_visible_ &&
      !pause_menu_labels_visible_) {
    return;
  }

  if (visible_) {
    EnsureGuestInputFilters();
    // Poll the same Xbox input path used by the game and perform edge detection
    // here, because ImGui's optional gamepad backend does not receive it.
    // While a name is being typed the keyboard is text: Space and Shift also
    // read as A and B here, so pad buttons are ignored until the edit ends.
    const uint32_t gamepad_buttons = ReadGamepadButtons();
    const uint32_t gamepad_pressed =
        editing_name_ ? 0u : gamepad_buttons & ~previous_gamepad_buttons_;
    previous_gamepad_buttons_ = gamepad_buttons;
    const bool navigating = !closing_ && !editing_name_ && !keys_page_;

    if (editing_name_) {
      UpdateNameEdit(io);
    }
    if (keys_page_ && !closing_) {
      UpdateKeysPage(gamepad_pressed);
    }
    if (navigating && (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) ||
        (gamepad_pressed & kPadUp))) {
      StepRow(-1);
      AegisWingApp::PulsePcSettingsNav();
    }
    if (navigating && (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) ||
        (gamepad_pressed & kPadDown))) {
      StepRow(1);
      AegisWingApp::PulsePcSettingsNav();
    }
    if (navigating && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ||
        (gamepad_pressed & kPadLeft))) {
      ChangeSelectedSetting(-1);
    }
    if (navigating && (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) ||
        (gamepad_pressed & kPadRight))) {
      ChangeSelectedSetting(1);
    }
    // On Player Name, Enter types a name and A (pad or Space) steps to the
    // next one; the game never sees either there (UpdateGuestInputBlock).
    const bool on_name_row = navigating && selected_row_ == kRowPlayerName;
    if (on_name_row && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
      BeginNameEdit();
    } else if (on_name_row && (gamepad_pressed & kPadConfirm)) {
      StepPlayerName(1);
    }
    // Display and Keyboard Controls: Enter or A opens the page instead of
    // confirming.
    const bool enter_or_a = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                            (gamepad_pressed & kPadConfirm);
    const bool on_page_row = navigating && !display_page_ && ActsOnConfirm(selected_row_);
    if (on_page_row && enter_or_a) {
      if (selected_row_ == kRowDisplay) {
        OpenDisplayPage();
      } else if (selected_row_ == kRowKeyboard) {
        OpenKeysPage();
      } else {
        ActivateUpdates();
      }
    }
    // B (pad, Shift or Esc) and a right click go back too. Every change is
    // already applied as it is made, so going back is the same as confirming:
    // the native scene saves and returns to Help & Options either way.
    const bool go_back = ImGui::IsMouseClicked(ImGuiMouseButton_Right, false) ||
                         (gamepad_pressed & kPadBack) != 0;
    const bool confirm = !on_name_row && !on_page_row &&
                         (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                          (gamepad_pressed & kPadConfirm));
    // On the Display page every change applies as it is made: A, B, Esc or
    // a right click go back to Settings.
    const bool in_display_page = navigating && display_page_ && !on_page_row;
    if (in_display_page &&
        (confirm || go_back || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
      CloseDisplayPage();
    } else if (navigating && !display_page_ && !editing_name_ && (confirm || go_back)) {
      // Closing lifts any block first, so the A sent below reaches the page.
      closing_ = true;
      closing_front_ = AegisWingApp::FrontScene();
      block_all_until_.store(0, std::memory_order_relaxed);
      UpdateGuestInputBlock();
      // The retail page underneath confirms on an A keystroke. A brings its
      // own; going back with B or a right click has to send one.
      if (go_back) {
        rex::input::mnk::MnkInputDriver::InjectPadKeystroke(
            static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadA));
      }
      // Ask the native Settings scene to perform its normal
      // confirm/save/return action. Keep drawing until it acknowledges close.
      AegisWingApp::SetPcSoundVolume(sound_volume_);
      AegisWingApp::SetPcMusicVolume(music_volume_);
      AegisWingApp::RequestPcSettingsClose();
      // A new Render Scale applies by restarting the game, except from the
      // pause menu, where a level is in progress: then it waits for the next
      // start.
      if (AegisWingApp::RenderScaleNeedsRestart() &&
          !AegisWingApp::IsSettingsFromPause()) {
        AegisWingApp::RestartForRenderScale();
      }
    }

    // The original Audio Settings scene remains underneath this presentation
    // layer and owns profile persistence. Reassert our values so its hidden
    // slider focus cannot accidentally alter audio while a PC row is selected.
    AegisWingApp::SetPcSoundVolume(sound_volume_);
    AegisWingApp::SetPcMusicVolume(music_volume_);
    UpdateGuestInputBlock();
  }

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
  if (request_focus_) {
    ImGui::SetNextWindowFocus();
    request_focus_ = false;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  constexpr auto kWindowFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse;

  if (!ImGui::Begin("##AegisWingPcSettings", nullptr, kWindowFlags)) {
    ImGui::End();
    ImGui::PopStyleVar(2);
    return;
  }

  const float scale = std::max(
      0.5f, std::min(io.DisplaySize.x / kDesignWidth,
                     io.DisplaySize.y / kDesignHeight));
  const Layout layout{
      scale,
      ImVec2((io.DisplaySize.x - kDesignWidth * scale) * 0.5f,
             (io.DisplaySize.y - kDesignHeight * scale) * 0.5f)};
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImFont* regular_font = AegisWingApp::GetPcRegularFont();
  ImFont* bold_font = AegisWingApp::GetPcBoldFont();
  if (!regular_font) regular_font = ImGui::GetFont();
  if (!bold_font) bold_font = regular_font;

  // The game draws a new front scene a frame or two after switching to it:
  // captions wait 70 ms after any switch so they never sit on the old one.
  const uint32_t front_now = AegisWingApp::FrontScene();
  const auto draw_time = std::chrono::steady_clock::now();
  if (front_now != caption_front_) {
    caption_front_ = front_now;
    caption_front_since_ = draw_time;
  }
  const bool front_settled = draw_time - caption_front_since_ >= std::chrono::milliseconds(70);
  if (!visible_ && (help_options_labels_visible_ || pause_menu_labels_visible_) &&
      !front_settled) {
    ImGui::End();
    ImGui::PopStyleVar(2);
    return;
  }

  if (!visible_ && help_options_labels_visible_) {
    constexpr std::array<std::string_view, 4> kHelpLabels = {
        "How To Play", "Controls", "Settings", "Credits"};
    // The retail label baselines sit lower than their button anchors. Offset
    // the host captions upward so the selected caption is centered within the
    // blue highlight, while keeping the user's preferred menu order.
    constexpr std::array<float, 4> kHelpLabelY = {
        257.0f, 357.85f, 458.70f, 559.54f};
    for (size_t index = 0; index < kHelpLabels.size(); ++index) {
      DrawCenteredText(draw_list, bold_font, 28.0f * scale,
                       layout.Point(640.0f, kHelpLabelY[index]),
                       IM_COL32(245, 246, 248, 255), kHelpLabels[index],
                       2.0f * scale);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return;
  }

  if (!visible_ && pause_menu_labels_visible_) {
    // The retail pause scene can lose its cached text surface after gameplay.
    // Its buttons and selection animation continue to work, so draw only the
    // stable captions centered on the focus pill positions.
    constexpr std::array<std::string_view, 4> kPauseLabels = {
        "Resume Game", "High Scores", "Help & Options", "Exit Game"};
    constexpr std::array<float, 4> kPauseLabelY = {
        252.0f, 329.0f, 406.0f, 483.0f};
    DrawCenteredText(draw_list, bold_font, 28.0f * scale,
                     layout.Point(640.0f, 155.0f),
                     IM_COL32(245, 246, 248, 255), "GAME PAUSED",
                     2.0f * scale);
    for (size_t index = 0; index < kPauseLabels.size(); ++index) {
      DrawCenteredText(draw_list, bold_font, 28.0f * scale,
                       layout.Point(640.0f, kPauseLabelY[index]),
                       IM_COL32(245, 246, 248, 255), kPauseLabels[index],
                       2.0f * scale);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return;
  }

  if (keys_page_) {
    DrawKeysPage(io, draw_list, scale, layout.offset, regular_font, bold_font);
    ImGui::End();
    ImGui::PopStyleVar(2);
    return;
  }

  // The native Settings scene supplies the background and PC Settings title.
  // Draw the replacement controls directly in that scene instead of placing
  // a second modal panel over it.
  DrawCenteredText(draw_list, regular_font, 22.0f * scale,
                   layout.Point(640.0f, 161.0f),
                   IM_COL32(173, 202, 225, 255),
                   display_page_ ? "DISPLAY & PERFORMANCE" : "PLAYER, AUDIO & CONTROLS");

  // While typing, the name shows what has been typed so far and a caret.
  std::string name_value = player_name_;
  if (editing_name_) {
    const auto blink = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - name_edit_started_)
                           .count();
    name_value = name_edit_ + ((blink / 500) % 2 == 0 ? "_" : " ");
  }
  const std::string sound_value = std::to_string(sound_volume_) + "%";
  const std::string music_value = std::to_string(music_volume_) + "%";
  // A Render Scale this run is not drawing at says when it takes effect.
  std::string render_scale_value(
      kRenderScaleNames[static_cast<size_t>(std::clamp(render_scale_, 1, 3) - 1)]);
  if (render_scale_ != AegisWingApp::ActiveRenderScale()) {
    render_scale_value = std::to_string(render_scale_) +
                         (AegisWingApp::IsSettingsFromPause() ? "x - next start"
                                                              : "x - restarts");
  }
  // Check for Updates shows where the check is.
  const aegis_wing::UpdateState update_state = aegis_wing::GetUpdateState();
  std::string update_value;
  switch (update_state) {
    case aegis_wing::UpdateState::kChecking: update_value = "Checking..."; break;
    case aegis_wing::UpdateState::kUpToDate: update_value = "Up to date"; break;
    case aegis_wing::UpdateState::kAvailable:
      update_value = "v" + aegis_wing::GetLatestVersion() + " available";
      break;
    case aegis_wing::UpdateState::kFailed: update_value = "Not reachable"; break;
    default: update_value = "Check Now"; break;
  }
  std::array<std::string_view, kRowCount> values = {
      name_value,
      sound_value,
      music_value,
      display_mode_ == 0 ? std::string_view("Windowed")
                         : std::string_view("Fullscreen"),
      kResolutions[static_cast<size_t>(std::clamp(resolution_, 0, 3))],
      render_scale_value,
      vsync_ ? std::string_view("On") : std::string_view("Off"),
      kUpscalingNames[static_cast<size_t>(std::clamp(upscaling_, 0, 2))],
      kAntiAliasingNames[static_cast<size_t>(std::clamp(anti_aliasing_, 0, 2))],
      show_fps_ ? std::string_view("On") : std::string_view("Off"),
      // Display and Keyboard Controls open a page: just the ">" arrow.
      std::string_view(""),
      std::string_view(""),
      update_value};

  // The rows of the page that is showing, from under the subtitle.
  const int* page_rows = display_page_ ? kDisplayRows.data() : kMainRows.data();
  const int page_row_count =
      static_cast<int>(display_page_ ? kDisplayRows.size() : kMainRows.size());
  constexpr float kRowLeft = 300.0f;
  constexpr float kRowRight = 980.0f;
  constexpr float kFirstRowTop = 186.0f;
  constexpr float kRowStep = 52.0f;
  constexpr float kRowHeight = 42.0f;
  constexpr float kRowMiddle = kRowHeight * 0.5f;

  const ImVec2 mouse = io.MousePos;
  const bool mouse_clicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
  const bool mouse_moved =
      std::abs(io.MouseDelta.x) > 0.01f ||
      std::abs(io.MouseDelta.y) > 0.01f;
  for (int index = 0; index < page_row_count; ++index) {
    const int row = page_rows[index];
    const float top = kFirstRowTop + index * kRowStep;
    const ImVec2 row_min = layout.Point(kRowLeft, top);
    const ImVec2 row_max = layout.Point(kRowRight, top + kRowHeight);
    const bool hovered = mouse.x >= row_min.x && mouse.x <= row_max.x &&
                         mouse.y >= row_min.y && mouse.y <= row_max.y;
    // A stationary cursor must not override controller/keyboard selection.
    // This was especially visible after a window resize, when the same cursor
    // position landed over a different row and moved the highlight there.
    // The mouse is left alone while a name is being typed.
    if (visible_ && !closing_ && !editing_name_ && hovered &&
        (mouse_moved || mouse_clicked)) {
      selected_row_ = row;
      if (mouse_clicked && row == kRowPlayerName) {
        // The arrows step through the names; anywhere else types one.
        const float left_arrow_end = layout.Point(735.0f, top).x;
        const float right_arrow_start = layout.Point(903.0f, top).x;
        if (mouse.x >= layout.Point(674.0f, top).x && mouse.x < left_arrow_end) {
          StepPlayerName(-1);
        } else if (mouse.x > right_arrow_start) {
          StepPlayerName(1);
        } else {
          BeginNameEdit();
        }
      } else if (mouse_clicked) {
        // Both arrow glyphs live in the value half of the row, so splitting
        // the whole row at its midpoint made both glyphs count as "right".
        // Split halfway between the two displayed arrows instead.
        const float arrow_split_x = layout.Point(819.0f, top).x;
        const int direction = mouse.x < arrow_split_x ? -1 : 1;
        ChangeSelectedSetting(direction);
      }
      UpdateGuestInputBlock();
    }

    if (row == selected_row_) {
      draw_list->AddRectFilled(row_min, row_max,
                               IM_COL32(181, 187, 191, 255),
                               21.0f * scale);
      draw_list->AddRectFilled(layout.Point(kRowLeft + 7.0f, top + 7.0f),
                               layout.Point(kRowRight - 7.0f,
                                            top + kRowHeight - 7.0f),
                               hovered ? IM_COL32(22, 126, 210, 255)
                                       : IM_COL32(12, 103, 187, 255),
                               14.0f * scale);
      draw_list->AddLine(layout.Point(kRowLeft + 30.0f, top + 11.0f),
                         layout.Point(kRowRight - 30.0f, top + 11.0f),
                         IM_COL32(90, 187, 245, 180), 2.0f * scale);
    } else {
      draw_list->AddRectFilled(row_min, row_max,
                               hovered ? IM_COL32(24, 58, 91, 205)
                                       : IM_COL32(4, 15, 28, 170),
                               19.0f * scale);
      draw_list->AddRect(row_min, row_max, IM_COL32(103, 137, 162, 100),
                         19.0f * scale, 0, 1.5f * scale);
    }

    const float row_font_size = 22.0f * scale;
    const ImVec2 label_pos = layout.Point(338.0f, top + 9.5f);
    DrawTextShadow(draw_list, bold_font, row_font_size, label_pos,
                   IM_COL32(246, 247, 249, 255), kLabels[row],
                   2.0f * scale);

    const bool typing_here = row == kRowPlayerName && editing_name_;
    if (typing_here) {
      // A light field behind the name being typed; no arrows meanwhile.
      draw_list->AddRectFilled(layout.Point(684.0f, top + 8.0f),
                               layout.Point(950.0f, top + kRowHeight - 8.0f),
                               IM_COL32(4, 30, 58, 235), 6.0f * scale);
      draw_list->AddRect(layout.Point(684.0f, top + 8.0f),
                         layout.Point(950.0f, top + kRowHeight - 8.0f),
                         IM_COL32(178, 220, 250, 255), 6.0f * scale, 0,
                         1.5f * scale);
    }
    const ImVec2 value_center = layout.Point(812.0f, top + kRowMiddle);
    DrawCenteredText(draw_list, regular_font, row_font_size, value_center,
                     IM_COL32(244, 248, 252, 255), values[row],
                     1.5f * scale);
    // A row that opens a page (or acts) has only the ">" arrow.
    if (!typing_here && !ActsOnConfirm(row)) {
      DrawCenteredText(draw_list, bold_font, row_font_size,
                       layout.Point(704.0f, top + kRowMiddle),
                       IM_COL32(178, 220, 250, 255), "<");
    }
    if (!typing_here) {
      DrawCenteredText(draw_list, bold_font, row_font_size,
                       layout.Point(934.0f, top + kRowMiddle),
                       IM_COL32(178, 220, 250, 255), ">");
    }
  }

  std::string_view description =
      selected_row_ == kRowResolution
          ? "Resolution changes apply immediately."
          : kDescriptions[static_cast<size_t>(selected_row_)];
  std::string update_description;
  if (selected_row_ == kRowUpdates) {
    switch (update_state) {
      case aegis_wing::UpdateState::kChecking: update_description = "Asking GitHub for the newest version..."; break;
      case aegis_wing::UpdateState::kUpToDate:
        update_description = "You have the newest version (v" AEGISWING_VERSION ").";
        break;
      case aegis_wing::UpdateState::kAvailable:
        update_description = "Version " + aegis_wing::GetLatestVersion() +
                             " is out. Press A or Enter to open its download page.";
        break;
      case aegis_wing::UpdateState::kFailed:
        update_description = "GitHub could not be reached. Check your connection and try again.";
        break;
      default: break;
    }
    if (!update_description.empty()) description = update_description;
  }
  if (selected_row_ == kRowRenderScale && AegisWingApp::IsSettingsFromPause()) {
    description = "Draws the game sharper on faster graphics cards. Applies next time it starts.";
  }
  if (editing_name_) {
    description = "Type a name of up to 15 characters.";
  }
  DrawCenteredText(draw_list, regular_font, 20.0f * scale,
                   layout.Point(640.0f, 624.0f),
                   IM_COL32(190, 207, 221, 255),
                   description);

  draw_list->AddLine(layout.Point(300.0f, 648.0f),
                     layout.Point(980.0f, 648.0f),
                     IM_COL32(159, 174, 185, 110), 1.5f * scale);

  if (editing_name_) {
    // Typing: Enter keeps the name, Esc puts the old one back.
    DrawCenteredText(draw_list, regular_font, 23.0f * scale,
                     layout.Point(640.0f, 682.0f),
                     IM_COL32(240, 242, 245, 255),
                     "Enter  Save        Esc  Cancel", 1.5f * scale);
  } else if (display_page_) {
    // Everything on the Display page applies at once: just Back.
    const float b_x = 600.0f;
    draw_list->AddCircleFilled(layout.Point(b_x, 682.0f), 15.0f * scale,
                               IM_COL32(200, 46, 46, 255));
    DrawCenteredText(draw_list, bold_font, 20.0f * scale,
                     layout.Point(b_x, 682.0f), IM_COL32_WHITE, "B");
    DrawTextShadow(draw_list, regular_font, 23.0f * scale,
                   layout.Point(b_x + 23.0f, 668.0f),
                   IM_COL32(240, 242, 245, 255), "Back", 1.5f * scale);
  } else {
    // A Confirm (Next Name on Player Name, Open on a page row) and B Back.
    const bool on_name_row = selected_row_ == kRowPlayerName;
    const float a_x = on_name_row ? 497.0f : 527.0f;
    const float b_x = on_name_row ? 717.0f : 687.0f;
    draw_list->AddCircleFilled(layout.Point(a_x, 682.0f), 15.0f * scale,
                               IM_COL32(75, 177, 48, 255));
    DrawCenteredText(draw_list, bold_font, 20.0f * scale,
                     layout.Point(a_x, 682.0f), IM_COL32_WHITE, "A");
    DrawTextShadow(draw_list, regular_font, 23.0f * scale,
                   layout.Point(a_x + 23.0f, 668.0f),
                   IM_COL32(240, 242, 245, 255),
                   on_name_row                  ? "Next Name"
                   : OpensPage(selected_row_) ? "Open"
                   : selected_row_ == kRowUpdates
                       ? (update_state == aegis_wing::UpdateState::kAvailable ? "Download" : "Check")
                       : "Confirm",
                   1.5f * scale);
    draw_list->AddCircleFilled(layout.Point(b_x, 682.0f), 15.0f * scale,
                               IM_COL32(200, 46, 46, 255));
    DrawCenteredText(draw_list, bold_font, 20.0f * scale,
                     layout.Point(b_x, 682.0f), IM_COL32_WHITE, "B");
    DrawTextShadow(draw_list, regular_font, 23.0f * scale,
                   layout.Point(b_x + 23.0f, 668.0f),
                   IM_COL32(240, 242, 245, 255), "Back", 1.5f * scale);
  }

  ImGui::End();
  ImGui::PopStyleVar(2);
}
