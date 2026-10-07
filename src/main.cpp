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
