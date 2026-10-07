#include "aegis_wing_high_scores.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string_view>

#include <imgui.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

#include "generated/default/aegis_wing_init.h"
#include "aegis_wing_app.h"

namespace {

constexpr float kDesignWidth = 1280.0f;
constexpr float kDesignHeight = 720.0f;
constexpr uint32_t kPlayerDataAddress = 0x924C2620;
constexpr uint32_t kPlayerDataStride = 96;
constexpr uint32_t kTotalScoreOffset = 60;
constexpr size_t kMaximumScores = 10;

std::mutex high_score_mutex;
std::array<int64_t, 4> last_recorded_scores{};
std::chrono::steady_clock::time_point last_recorded_time{};

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
                     IM_COL32(0, 0, 0, 220), text.data(),
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

uint32_t ReadGamepadButtons() {
  auto* input_system = static_cast<rex::input::InputSystem*>(
      AegisWingApp::GetPcInputSystem());
  if (!input_system) {
    return 0;
  }
  uint32_t buttons = 0;
  for (uint32_t user = 0; user < 4; ++user) {
    rex::input::X_INPUT_STATE state{};
    if (input_system->GetState(user, &state) != 0) {
      continue;
    }
    const uint16_t xbox_buttons =
        static_cast<uint16_t>(state.gamepad.buttons);
    if (xbox_buttons & rex::input::X_INPUT_GAMEPAD_B) {
      buttons |= 1;
    }
  }
  return buttons;
}

std::filesystem::path HighScorePath() {
  return rex::filesystem::GetExecutableFolder() / "userdata" /
         "high_scores.tsv";
}

std::vector<aegis_wing::HighScoreEntry> LoadHighScoresUnlocked() {
  std::vector<aegis_wing::HighScoreEntry> scores;
  std::ifstream file(HighScorePath());
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line.starts_with("score\t")) {
      continue;
    }
    std::istringstream row(line);
    std::string score_text;
    std::string player;
    std::string timestamp_text;
    if (!std::getline(row, score_text, '\t') ||
        !std::getline(row, player, '\t') ||
        !std::getline(row, timestamp_text, '\t')) {
      continue;
    }
    try {
      aegis_wing::HighScoreEntry entry;
      entry.score = std::stoll(score_text);
      entry.player = player.empty() ? "User" : player;
      entry.timestamp = std::stoll(timestamp_text);
      if (entry.score > 0) {
        scores.push_back(std::move(entry));
      }
    } catch (...) {
      // Ignore a malformed row while preserving the rest of the local table.
    }
  }
  std::stable_sort(scores.begin(), scores.end(), [](const auto& left,
                                                     const auto& right) {
    if (left.score != right.score) {
      return left.score > right.score;
    }
    return left.timestamp < right.timestamp;
  });
  if (scores.size() > kMaximumScores) {
    scores.resize(kMaximumScores);
  }
  return scores;
}

void SaveHighScoresUnlocked(
    const std::vector<aegis_wing::HighScoreEntry>& scores) {
  const auto path = HighScorePath();
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return;
  }
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return;
  }
  file << "score\tplayer\ttimestamp\n";
  for (const auto& entry : scores) {
    file << entry.score << '\t' << entry.player << '\t'
         << entry.timestamp << '\n';
  }
}

// The table is tab-separated text, one row per line.
std::string TableSafeName(std::string name) {
  for (char& c : name) {
    if (c == '\t' || c == '\r' || c == '\n') c = ' ';
  }
  return name.empty() ? "User" : name;
}

std::string FormatScore(int64_t score) {
  std::string digits = std::to_string(score);
  for (int index = static_cast<int>(digits.size()) - 3; index > 0;
       index -= 3) {
    digits.insert(static_cast<size_t>(index), ",");
  }
  return digits;
}

std::string FormatDate(int64_t timestamp) {
  const std::time_t value = static_cast<std::time_t>(timestamp);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &value);
#else
  localtime_r(&value, &local);
#endif
  std::ostringstream text;
  text << std::put_time(&local, "%Y-%m-%d");
  return text.str();
}

}  // namespace

namespace aegis_wing {

std::vector<HighScoreEntry> LoadHighScores() {
  std::lock_guard lock(high_score_mutex);
  return LoadHighScoresUnlocked();
}

void RecordCurrentHighScores(uint32_t scene_address) {
  (void)scene_address;
  auto* memory = AegisWingApp::GetGuestMemory();
  if (!memory) {
    return;
  }

  std::array<int64_t, 4> current_scores{};
  for (size_t player = 0; player < current_scores.size(); ++player) {
    const uint32_t address =
        kPlayerDataAddress + static_cast<uint32_t>(player) *
                                 kPlayerDataStride +
        kTotalScoreOffset;
    const auto* score_value =
        memory->TranslateVirtual<const uint32_t*>(address);
    current_scores[player] = static_cast<int32_t>(
        rex::memory::load_and_swap<uint32_t>(score_value));
  }

  const auto now_steady = std::chrono::steady_clock::now();
  {
    std::lock_guard lock(high_score_mutex);
    if (current_scores == last_recorded_scores &&
        now_steady - last_recorded_time < std::chrono::seconds(30)) {
      return;
    }
    last_recorded_scores = current_scores;
    last_recorded_time = now_steady;

    auto scores = LoadHighScoresUnlocked();
    const int64_t timestamp = static_cast<int64_t>(std::time(nullptr));
    for (size_t player = 0; player < current_scores.size(); ++player) {
      if (current_scores[player] <= 0) {
        continue;
      }
      scores.push_back(HighScoreEntry{
          current_scores[player],
          // Player one is whoever is at this PC: the name from Settings.
          player == 0 ? TableSafeName(AegisWingApp::GetPcPlayerName())
                      : "Player " + std::to_string(player + 1),
          timestamp});
    }
    std::stable_sort(scores.begin(), scores.end(), [](const auto& left,
                                                       const auto& right) {
      if (left.score != right.score) {
        return left.score > right.score;
      }
      return left.timestamp < right.timestamp;
    });
    if (scores.size() > kMaximumScores) {
      scores.resize(kMaximumScores);
    }
    SaveHighScoresUnlocked(scores);
  }
}

}  // namespace aegis_wing

AegisWingHighScoresDialog::AegisWingHighScoresDialog(
    rex::ui::ImGuiDrawer* drawer)
    : ImGuiDialog(drawer) {}

void AegisWingHighScoresDialog::Open() {
  scores_ = aegis_wing::LoadHighScores();
  previous_gamepad_buttons_ = ReadGamepadButtons();
  request_focus_ = true;
  closing_ = false;
  visible_ = true;
}

void AegisWingHighScoresDialog::Close() {
  visible_ = false;
  closing_ = false;
}

void AegisWingHighScoresDialog::OnDraw(ImGuiIO& io) {
  if (!visible_) {
    return;
  }

  const uint32_t gamepad_buttons = ReadGamepadButtons();
  const uint32_t gamepad_pressed =
      gamepad_buttons & ~previous_gamepad_buttons_;
  previous_gamepad_buttons_ = gamepad_buttons;
  if (!closing_ && (gamepad_pressed & 1)) {
    closing_ = true;
  }
  if (!closing_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    AegisWingApp::CloseHighScores();
    return;
  }
  // Keep the host overlay active until B is released so that the same press
  // cannot leak through and activate Back on the native scene underneath.
  if (closing_ && !(gamepad_buttons & 1)) {
    AegisWingApp::CloseHighScores();
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
  if (request_focus_) {
    ImGui::SetNextWindowFocus();
    request_focus_ = false;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  constexpr auto flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::Begin("##AegisWingHighScores", nullptr, flags)) {
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

  draw_list->AddRectFilled(layout.Point(0.0f, 0.0f),
                           layout.Point(1280.0f, 720.0f),
                           IM_COL32(0, 4, 12, 155));
  const ImVec2 panel_min = layout.Point(175.0f, 65.0f);
  const ImVec2 panel_max = layout.Point(1105.0f, 655.0f);
  draw_list->AddRectFilled(panel_min, panel_max,
                           IM_COL32(4, 16, 32, 242), 34.0f * scale);
  draw_list->AddRect(panel_min, panel_max,
                     IM_COL32(194, 201, 205, 255), 34.0f * scale, 0,
                     9.0f * scale);
  draw_list->AddRect(layout.Point(187.0f, 77.0f),
                     layout.Point(1093.0f, 643.0f),
                     IM_COL32(65, 103, 133, 210), 27.0f * scale, 0,
                     2.0f * scale);

  DrawCenteredText(draw_list, bold_font, 48.0f * scale,
                   layout.Point(640.0f, 118.0f),
                   IM_COL32(246, 247, 249, 255), "HIGH SCORES",
                   3.0f * scale);
  DrawCenteredText(draw_list, regular_font, 20.0f * scale,
                   layout.Point(640.0f, 157.0f),
                   IM_COL32(173, 202, 225, 255),
                   "LOCAL PILOTS - TOP 10");

  draw_list->AddRectFilled(layout.Point(225.0f, 185.0f),
                           layout.Point(1055.0f, 226.0f),
                           IM_COL32(11, 86, 154, 210), 10.0f * scale);
  DrawCenteredText(draw_list, bold_font, 21.0f * scale,
                   layout.Point(285.0f, 205.0f), IM_COL32_WHITE, "RANK");
  DrawTextShadow(draw_list, bold_font, 21.0f * scale,
                 layout.Point(365.0f, 191.0f), IM_COL32_WHITE, "PILOT",
                 1.5f * scale);
  DrawCenteredText(draw_list, bold_font, 21.0f * scale,
                   layout.Point(760.0f, 205.0f), IM_COL32_WHITE, "SCORE");
  DrawCenteredText(draw_list, bold_font, 21.0f * scale,
                   layout.Point(950.0f, 205.0f), IM_COL32_WHITE, "DATE");

  if (scores_.empty()) {
    DrawCenteredText(draw_list, regular_font, 27.0f * scale,
                     layout.Point(640.0f, 386.0f),
                     IM_COL32(190, 207, 221, 255),
                     "No local high scores recorded yet.");
  } else {
    for (size_t index = 0; index < scores_.size(); ++index) {
      const float center_y = 254.0f + static_cast<float>(index) * 33.0f;
      if (index % 2 == 0) {
        draw_list->AddRectFilled(layout.Point(225.0f, center_y - 15.0f),
                                 layout.Point(1055.0f, center_y + 15.0f),
                                 IM_COL32(14, 38, 62, 155),
                                 6.0f * scale);
      }
      const auto rank = std::to_string(index + 1);
      const auto score = FormatScore(scores_[index].score);
      const auto date = FormatDate(scores_[index].timestamp);
      DrawCenteredText(draw_list, bold_font, 20.0f * scale,
                       layout.Point(285.0f, center_y),
                       index == 0 ? IM_COL32(255, 211, 80, 255)
                                  : IM_COL32(236, 240, 244, 255),
                       rank);
      DrawTextShadow(draw_list, regular_font, 20.0f * scale,
                     layout.Point(365.0f, center_y - 12.0f),
                     IM_COL32(236, 240, 244, 255), scores_[index].player,
                     1.0f * scale);
      DrawCenteredText(draw_list, regular_font, 20.0f * scale,
                       layout.Point(760.0f, center_y),
                       IM_COL32(236, 240, 244, 255), score);
      DrawCenteredText(draw_list, regular_font, 20.0f * scale,
                       layout.Point(950.0f, center_y),
                       IM_COL32(190, 207, 221, 255), date);
    }
  }

  draw_list->AddCircleFilled(layout.Point(548.0f, 615.0f),
                             15.0f * scale,
                             IM_COL32(211, 49, 42, 255));
  DrawCenteredText(draw_list, bold_font, 20.0f * scale,
                   layout.Point(548.0f, 615.0f), IM_COL32_WHITE, "B");
  DrawTextShadow(draw_list, regular_font, 23.0f * scale,
                 layout.Point(572.0f, 601.0f),
                 IM_COL32(240, 242, 245, 255), "Back", 1.5f * scale);

  ImGui::End();
  ImGui::PopStyleVar(2);
}
