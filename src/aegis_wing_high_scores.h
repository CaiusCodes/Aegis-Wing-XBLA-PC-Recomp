#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

namespace aegis_wing {

struct HighScoreEntry {
  int64_t score = 0;
  std::string player;
  int64_t timestamp = 0;
};

std::vector<HighScoreEntry> LoadHighScores();
void RecordCurrentHighScores(uint32_t scene_address);

}  // namespace aegis_wing

class AegisWingHighScoresDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit AegisWingHighScoresDialog(rex::ui::ImGuiDrawer* drawer);

  void Open();
  void Close();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool visible_ = false;
  bool closing_ = false;
  bool request_focus_ = false;
  uint32_t previous_gamepad_buttons_ = 0;
  std::vector<aegis_wing::HighScoreEntry> scores_;
};
