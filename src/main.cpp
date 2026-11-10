// aegis_wing - ReXGlue Recompiled Project

#include "generated/default/aegis_wing_init.h"

#include "aegis_wing_app.h"
#include "aegis_wing_high_scores.h"
#include "aegis_wing_score_capture.h"
#include "aegis_wing_title_prompt.h"

bool AegisWing_TitleShowsGamepadPrompt() {
  return aegis_wing::TitleShowsGamepadPrompt();
}

void AegisWing_ApplyTitlePromptText(uint32_t buffer) {
  aegis_wing::ApplyTitlePromptText(buffer);
}

void AegisWing_OpenHighScores() {
  AegisWingApp::OpenHighScores();
}

bool AegisWing_IsHighScoresOpen() {
  return AegisWingApp::IsHighScoresOpen();
}

uint32_t AegisWing_EmptyResultCode() {
  // X_ERROR_EMPTY (0x000010D2) - "no keystroke pending".
  return 0x10D2;
}

bool AegisWing_ConsumePcSettingsNavKeystroke(uint16_t* out_vk,
                                             uint16_t* out_flags) {
  return AegisWingApp::ConsumePcSettingsNavKeystroke(out_vk, out_flags);
}

void AegisWing_WriteGuestKeystroke(uint32_t address, uint16_t vk,
                                   uint16_t flags) {
  AegisWingApp::WriteGuestKeystroke(address, vk, flags);
}

void AegisWing_RecordCurrentHighScores(uint32_t scene_address) {
  aegis_wing::RecordCurrentHighScores(scene_address);
}

void AegisWing_CaptureSessionStats(uint64_t xuid, uint32_t view_count,
                                   uint32_t views_address) {
  aegis_wing::CaptureSessionStats(xuid, view_count, views_address);
}

void AegisWing_OpenPcSettings() {
  AegisWingApp::OpenPcSettings();
}

void AegisWing_SelectPcSettings() {
  AegisWingApp::SelectPcSettings();
}

bool AegisWing_ShouldForcePcSettingsConfirm() {
  return AegisWingApp::ShouldForcePcSettingsConfirm();
}

bool AegisWing_IsPcSettingsOpen() {
  return AegisWingApp::IsPcSettingsOpen();
}

void AegisWing_NotePauseConfirmOpen(int /*from_pause*/) {
  // The origin no longer matters: the pause menu's own result decides whether
  // its captions return (AegisWing_OnPauseNavReturn).
  AegisWingApp::NotePauseConfirmOpen();
}

void AegisWing_OnPauseSceneInit(uint32_t scene) {
  AegisWingApp::OnPauseSceneInit(scene);
}

void AegisWing_OnPauseSceneDestroyed(uint32_t scene) {
  AegisWingApp::OnPauseSceneDestroyed(scene);
}

void AegisWing_OnPauseNavReturn(uint32_t scene) {
  AegisWingApp::OnPauseNavReturn(scene);
}

void AegisWing_OnSceneInit(uint32_t scene) {
  AegisWingApp::OnSceneInit(scene);
}

void AegisWing_OnMainMenuShown(uint32_t scene) {
  AegisWingApp::OnMainMenuShown(scene);
}

bool AegisWing_IsPauseConfirmOpen() {
  return AegisWingApp::IsPauseConfirmOpen();
}

void AegisWing_ClosePcSettings() {
  AegisWingApp::ClosePcSettings();
}

void AegisWing_OnHelpOptionsSceneInit() {
  AegisWingApp::OnHelpOptionsSceneInit();
}

void AegisWing_ShowHelpOptionsLabels() {
  AegisWingApp::ShowHelpOptionsLabels();
}

void AegisWing_HideHelpOptionsLabels() {
  AegisWingApp::HideHelpOptionsLabels();
}

void AegisWing_CloseHelpOptionsLabels() {
  AegisWingApp::CloseHelpOptionsLabels();
}

void AegisWing_ShowPauseMenuLabels() {
  AegisWingApp::ShowPauseMenuLabels();
}

void AegisWing_NotePauseHelpChoice() {
  AegisWingApp::NotePauseHelpChoice();
}

void AegisWing_HidePauseMenuLabels() {
  AegisWingApp::HidePauseMenuLabels();
}

REX_DEFINE_APP(aegis_wing, AegisWingApp::Create)

// CPU use. The game's GPU-completion poll (aegis_wing_recomp.4.cpp,
// sub_920EF458: a short delay loop, then a check of the command processor's
// read pointer with a 5000-tick timeout), which the game's GPU waits loop on,
// kept the main thread at ~100% of a CPU core: on the Xbox 360 it shared a core
// with other hardware threads. Called each time the poll says "still waiting":
// yield for the first 0.5 ms of a wait, then sleep in 0.5 ms high-resolution
// steps (as in the Perfect Dark port).
// Render Scale (Settings): the internal resolution, 1280x720 times this. The
// GPU backend reads resolution_scale only when it starts, so a change applies
// by restarting the game (AegisWingApp::ApplyRenderScale).
REXCVAR_DEFINE_INT32(aw_render_scale, 1, "AegisWing",
                     "Render scale: 1 (1280x720, the default, the lightest), 2 (2560x1440) or "
                     "3 (3840x2160); applies when the game starts")
    .range(1, 3);

// Testing only: cycles Upscaling and Anti-Aliasing through the same live path
// as the Settings page, 4 s per step, logging "[effect-test]" at each, so
// screen captures can be compared. Never saved (the steps set the cvars
// without SavePcSettings) and off by default.
REXCVAR_DEFINE_BOOL(aw_effect_selftest, false, "AegisWing",
                    "Testing: cycle the upscaling and anti-aliasing settings live");

// Testing only: from the title, presses A four times and Start (to level 1
// of single player), then keeps pressing A, as controller keystrokes inside
// the game, so measurements need no input to the desktop. Off by default.
REXCVAR_DEFINE_BOOL(aw_test_drive, false, "AegisWing",
                    "Testing: drive from the title into level 1 with injected A/Start presses");

// Testing only: a comma-separated list of controller presses and waits played
// inside the game from the start, e.g. "12,A,3,Down,A". A number waits that
// many seconds; A, B, Start, Up, Down, Left, Right press that button. Each
// step is logged as "[test-script]". Empty (off) by default.
REXCVAR_DEFINE_STRING(aw_test_script, "", "AegisWing",
                      "Testing: controller presses and waits to play from the start");

REXCVAR_DEFINE_BOOL(aw_sleep_in_waits, true, "AegisWing",
                    "Let the game's GPU waits sleep in 0.5 ms steps instead of spinning a "
                    "CPU core");

namespace {
void SleepHalfMillisecond() {
  struct Timer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr,
                                           0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                           TIMER_ALL_ACCESS);
    ~Timer() {
      if (handle) CloseHandle(handle);
    }
  };
  thread_local Timer timer;
  LARGE_INTEGER due;
  due.QuadPart = -5000;  // 0.5 ms, relative, in 100 ns units
  if (timer.handle && SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)) {
    WaitForSingleObject(timer.handle, INFINITE);
  } else {
    SwitchToThread();
  }
}
}  // namespace

void AegisWing_GpuWaitBackoff() {
  if (!REXCVAR_GET(aw_sleep_in_waits)) return;
  thread_local int64_t wait_start = 0;
  thread_local int64_t last_poll = 0;
  static const int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  // Polls within one wait come microseconds apart; a gap over 2 ms means a new wait.
  if (now.QuadPart - last_poll > freq / 500) wait_start = now.QuadPart;
  if (now.QuadPart - wait_start < freq / 2000) {
    SwitchToThread();
  } else {
    SleepHalfMillisecond();
  }
  QueryPerformanceCounter(&now);
  last_poll = now.QuadPart;
}
