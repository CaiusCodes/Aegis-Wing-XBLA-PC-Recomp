#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include <rex/ui/imgui_dialog.h>

class AegisWingPcSettingsDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit AegisWingPcSettingsDialog(rex::ui::ImGuiDrawer* drawer);

  void Open();
  void Close();
  void ShowHelpOptionsLabels();
  void HideHelpOptionsLabels();
  void CloseHelpOptionsLabels();
  void ShowPauseMenuLabels();
  void HidePauseMenuLabels();
  // Hides the pause captions and cancels any scheduled re-show.
  void ClearPauseMenuLabels();
  void SchedulePauseMenuLabels(double delay_seconds);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void ChangeSelectedSetting(int direction);
  // "vX.Y.Z" in the main menu's bottom-left corner while it is in front.
  void DrawVersionLabel(ImGuiIO& io);

  // Player Name row: step through the preset list, or type a name.
  void StepPlayerName(int direction);
  void BeginNameEdit();
  void EndNameEdit(bool keep);
  // Typing, Backspace, Enter and Esc while the name is being edited.
  void UpdateNameEdit(ImGuiIO& io);

  // The native Settings scene under this panel acts on the A keystroke (save
  // and close) and the game reads every bound key, so while the Player Name
  // row is selected A and Start are kept from the game, and while a name is
  // being typed nothing reaches it. Read on the game's input threads through
  // the input system's filters (registered once, on first draw).
  enum GuestInputBlock : int { kBlockNone = 0, kBlockConfirm = 1, kBlockAll = 2 };
  static GuestInputBlock CurrentGuestInputBlock();
  void EnsureGuestInputFilters();
  void UpdateGuestInputBlock();
  inline static std::atomic<int> guest_input_block_{kBlockNone};
  // Keys typed into the name can still sit in the input queue just after the
  // edit ends; keep everything from the game until this time (steady clock
  // ticks).
  inline static std::atomic<int64_t> block_all_until_{0};
  bool guest_filters_registered_ = false;

  std::string player_name_;
  bool editing_name_ = false;
  std::string name_edit_;
  std::chrono::steady_clock::time_point name_edit_started_{};

  bool visible_ = false;
  bool help_options_context_active_ = false;
  bool help_options_labels_visible_ = false;
  bool pause_menu_labels_visible_ = false;
  bool request_focus_ = false;
  bool closing_ = false;
  int selected_row_ = 0;
  int sound_volume_ = 100;
  int music_volume_ = 100;
  int display_mode_ = 0;
  int resolution_ = 1;
  bool vsync_ = true;
  bool show_fps_ = false;
  uint32_t previous_gamepad_buttons_ = 0;
  uint32_t previous_help_gamepad_buttons_ = 0;
  bool pause_labels_pending_ = false;
  std::chrono::steady_clock::time_point pause_labels_show_at_{};
  uint32_t previous_confirm_gamepad_buttons_ = 0;
};
