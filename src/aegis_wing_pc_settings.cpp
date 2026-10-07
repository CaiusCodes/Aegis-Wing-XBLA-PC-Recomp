#include "aegis_wing_pc_settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

#include <imgui.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/virtual_key.h>

#include "generated/default/aegis_wing_init.h"
#include "aegis_wing_app.h"
#include "aegis_wing_player_names.h"
#include "aegis_wing_title_prompt.h"

namespace {

constexpr float kDesignWidth = 1280.0f;
constexpr float kDesignHeight = 720.0f;

enum Row : int {
  kRowPlayerName,
  kRowSoundVolume,
  kRowMusicVolume,
  kRowDisplayMode,
  kRowResolution,
  kRowVSync,
  kRowShowFps,
  kRowCount
};
constexpr std::array<std::string_view, kRowCount> kLabels = {
    "Player Name", "Sound Volume", "Music Volume", "Display Mode",
    "Resolution", "VSync", "Show FPS"};
constexpr std::array<std::string_view, kRowCount> kDescriptions = {
    "Your name in LAN games and high scores. Press Enter or click it to type one.",
    "Adjust the volume of sound effects.",
    "Adjust the volume of the soundtrack.",
    "Choose between a desktop window and full-screen play.",
    "Set the output resolution used by the PC version.",
    "Synchronize frames to the display to prevent screen tearing.",
    "Display the live frame rate in the corner of the screen."};

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
  show_fps_ = AegisWingApp::GetPcShowFps();
  player_name_ = AegisWingApp::GetPcPlayerName();
  editing_name_ = false;
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
    if (editing_name_) {
      block = kBlockAll;
    } else if (selected_row_ == kRowPlayerName) {
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
}

void AegisWingPcSettingsDialog::HideHelpOptionsLabels() {
  help_options_labels_visible_ = false;
  previous_help_gamepad_buttons_ = ReadGamepadButtons();
}

void AegisWingPcSettingsDialog::CloseHelpOptionsLabels() {
  help_options_labels_visible_ = false;
  help_options_context_active_ = false;
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
    case kRowVSync:
      vsync_ = !vsync_;
      AegisWingApp::SetPcVSync(vsync_);
      break;
    case kRowShowFps:
      show_fps_ = !show_fps_;
      AegisWingApp::SetPcShowFps(show_fps_);
      break;
    default:
      break;
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
  // This panel handles the mouse itself (hover a row, click an arrow), so a
  // click here must not also reach the game as A, which saves and closes it.
  rex::input::mnk::MnkInputDriver::SetMenuClickButtons(!visible_);
  DrawVersionLabel(io);
  // Keeps the title screen's prompt in step with the connected gamepads.
  aegis_wing::UpdateTitlePrompt();

  // After a child scene closes back to the pause menu, delay the pause-caption
  // re-show until its fade-out finishes so two menus never draw on top of
  // each other. The pause scene may be torn down meanwhile (quit to the main
  // menu), so check it is still live when the delay expires.
  if (pause_labels_pending_) {
    if (std::chrono::steady_clock::now() < pause_labels_show_at_) {
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
    if (help_pressed & kPadBack) {
      help_options_labels_visible_ = true;
    }
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
    const bool navigating = !closing_ && !editing_name_;

    if (editing_name_) {
      UpdateNameEdit(io);
    }
    if (navigating && (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) ||
        (gamepad_pressed & kPadUp))) {
      selected_row_ = (selected_row_ + kRowCount - 1) % kRowCount;
      AegisWingApp::PulsePcSettingsNav();
    }
    if (navigating && (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) ||
        (gamepad_pressed & kPadDown))) {
      selected_row_ = (selected_row_ + 1) % kRowCount;
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
    // B (pad, Shift or Esc) and a right click go back too. Every change is
    // already applied as it is made, so going back is the same as confirming:
    // the native scene saves and returns to Help & Options either way.
    const bool go_back = ImGui::IsMouseClicked(ImGuiMouseButton_Right, false) ||
                         (gamepad_pressed & kPadBack) != 0;
    const bool confirm = !on_name_row && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                          (gamepad_pressed & kPadConfirm));
    if (navigating && !editing_name_ && (confirm || go_back)) {
      // Closing lifts any block first, so the A sent below reaches the page.
      closing_ = true;
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

  // The native Settings scene supplies the background and PC Settings title.
  // Draw the replacement controls directly in that scene instead of placing
  // a second modal panel over it.
  DrawCenteredText(draw_list, regular_font, 22.0f * scale,
                   layout.Point(640.0f, 161.0f),
                   IM_COL32(173, 202, 225, 255),
                   "PLAYER, AUDIO, DISPLAY & PERFORMANCE");

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
  std::array<std::string_view, kRowCount> values = {
      name_value,
      sound_value,
      music_value,
      display_mode_ == 0 ? std::string_view("Windowed")
                         : std::string_view("Fullscreen"),
      kResolutions[static_cast<size_t>(std::clamp(resolution_, 0, 3))],
      vsync_ ? std::string_view("On") : std::string_view("Off"),
      show_fps_ ? std::string_view("On") : std::string_view("Off")};

  // Seven rows between the subtitle and the description line.
  constexpr float kRowLeft = 300.0f;
  constexpr float kRowRight = 980.0f;
  constexpr float kFirstRowTop = 180.0f;
  constexpr float kRowStep = 52.0f;
  constexpr float kRowHeight = 44.0f;
  constexpr float kRowMiddle = kRowHeight * 0.5f;

  const ImVec2 mouse = io.MousePos;
  const bool mouse_clicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
  const bool mouse_moved =
      std::abs(io.MouseDelta.x) > 0.01f ||
      std::abs(io.MouseDelta.y) > 0.01f;
  for (int row = 0; row < kRowCount; ++row) {
    const float top = kFirstRowTop + row * kRowStep;
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
                               23.5f * scale);
      draw_list->AddRectFilled(layout.Point(kRowLeft + 7.0f, top + 7.0f),
                               layout.Point(kRowRight - 7.0f,
                                            top + kRowHeight - 7.0f),
                               hovered ? IM_COL32(22, 126, 210, 255)
                                       : IM_COL32(12, 103, 187, 255),
                               15.0f * scale);
      draw_list->AddLine(layout.Point(kRowLeft + 32.0f, top + 11.0f),
                         layout.Point(kRowRight - 32.0f, top + 11.0f),
                         IM_COL32(90, 187, 245, 180), 2.0f * scale);
    } else {
      draw_list->AddRectFilled(row_min, row_max,
                               hovered ? IM_COL32(24, 58, 91, 205)
                                       : IM_COL32(4, 15, 28, 170),
                               20.0f * scale);
      draw_list->AddRect(row_min, row_max, IM_COL32(103, 137, 162, 100),
                         20.0f * scale, 0, 1.5f * scale);
    }

    const float row_font_size = 23.0f * scale;
    const ImVec2 label_pos = layout.Point(338.0f, top + 10.5f);
    DrawTextShadow(draw_list, bold_font, row_font_size, label_pos,
                   IM_COL32(246, 247, 249, 255), kLabels[row],
                   2.0f * scale);

    const bool typing_here = row == kRowPlayerName && editing_name_;
    if (typing_here) {
      // A light field behind the name being typed; no arrows meanwhile.
      draw_list->AddRectFilled(layout.Point(684.0f, top + 9.0f),
                               layout.Point(950.0f, top + kRowHeight - 9.0f),
                               IM_COL32(4, 30, 58, 235), 6.0f * scale);
      draw_list->AddRect(layout.Point(684.0f, top + 9.0f),
                         layout.Point(950.0f, top + kRowHeight - 9.0f),
                         IM_COL32(178, 220, 250, 255), 6.0f * scale, 0,
                         1.5f * scale);
    }
    const ImVec2 value_center = layout.Point(812.0f, top + kRowMiddle);
    DrawCenteredText(draw_list, regular_font, 23.0f * scale, value_center,
                     IM_COL32(244, 248, 252, 255), values[row],
                     1.5f * scale);
    if (!typing_here) {
      DrawCenteredText(draw_list, bold_font, 23.0f * scale,
                       layout.Point(704.0f, top + kRowMiddle),
                       IM_COL32(178, 220, 250, 255), "<");
      DrawCenteredText(draw_list, bold_font, 23.0f * scale,
                       layout.Point(934.0f, top + kRowMiddle),
                       IM_COL32(178, 220, 250, 255), ">");
    }
  }

  std::string_view description =
      selected_row_ == kRowResolution
          ? "Resolution changes apply immediately."
          : kDescriptions[static_cast<size_t>(selected_row_)];
  if (editing_name_) {
    description = "Type a name of up to 15 characters.";
  }
  DrawCenteredText(draw_list, regular_font, 20.0f * scale,
                   layout.Point(640.0f, 558.0f),
                   IM_COL32(190, 207, 221, 255),
                   description);

  draw_list->AddLine(layout.Point(300.0f, 584.0f),
                     layout.Point(980.0f, 584.0f),
                     IM_COL32(159, 174, 185, 110), 1.5f * scale);

  if (editing_name_) {
    // Typing: Enter keeps the name, Esc puts the old one back.
    DrawCenteredText(draw_list, regular_font, 23.0f * scale,
                     layout.Point(640.0f, 625.0f),
                     IM_COL32(240, 242, 245, 255),
                     "Enter  Save        Esc  Cancel", 1.5f * scale);
  } else {
    // A Confirm (A Next Name on the Player Name row) and B Back, side by side.
    const bool on_name_row = selected_row_ == kRowPlayerName;
    const float a_x = on_name_row ? 497.0f : 527.0f;
    const float b_x = on_name_row ? 717.0f : 687.0f;
    draw_list->AddCircleFilled(layout.Point(a_x, 625.0f), 15.0f * scale,
                               IM_COL32(75, 177, 48, 255));
    DrawCenteredText(draw_list, bold_font, 20.0f * scale,
                     layout.Point(a_x, 625.0f), IM_COL32_WHITE, "A");
    DrawTextShadow(draw_list, regular_font, 23.0f * scale,
                   layout.Point(a_x + 23.0f, 611.0f),
                   IM_COL32(240, 242, 245, 255),
                   on_name_row ? "Next Name" : "Confirm", 1.5f * scale);
    draw_list->AddCircleFilled(layout.Point(b_x, 625.0f), 15.0f * scale,
                               IM_COL32(200, 46, 46, 255));
    DrawCenteredText(draw_list, bold_font, 20.0f * scale,
                     layout.Point(b_x, 625.0f), IM_COL32_WHITE, "B");
    DrawTextShadow(draw_list, regular_font, 23.0f * scale,
                   layout.Point(b_x + 23.0f, 611.0f),
                   IM_COL32(240, 242, 245, 255), "Back", 1.5f * scale);
  }

  DrawCenteredText(draw_list, regular_font, 17.0f * scale,
                   layout.Point(640.0f, 662.0f),
                   IM_COL32(126, 157, 181, 255),
                   "Use the D-pad to choose and change a setting");

  ImGui::End();
  ImGui::PopStyleVar(2);
}
