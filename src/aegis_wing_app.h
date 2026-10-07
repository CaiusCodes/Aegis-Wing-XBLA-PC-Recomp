// aegis_wing - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/kernel/xam/netplay.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/window_sdl.h>

#include "aegis_wing_crash_report.h"
#include "aegis_wing_high_scores.h"
#include "aegis_wing_menu_pointer.h"
#include "aegis_wing_pc_patches.h"
#include "aegis_wing_pc_settings.h"
#include "aegis_wing_title_prompt.h"

class AegisWingApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  std::string GetWindowTitle() const override {
    return "Aegis Wing";
  }

  inline static AegisWingApp* current_instance_ = nullptr;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    auto app = std::unique_ptr<AegisWingApp>(
        new AegisWingApp(ctx, "aegis_wing", PPCImageConfig));
    current_instance_ = app.get();
    return app;
  }

  static void OpenPcSettings() {
    auto* app = current_instance_;
    if (!app) {
      return;
    }
    pc_settings_open_.store(true, std::memory_order_release);
    pc_settings_close_requested_.store(false, std::memory_order_release);
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->pc_settings_dialog_) {
        app->pc_settings_dialog_->Open();
      }
    });
  }

  static void SelectPcSettings() {
    // The Help & Options event handler calls this only for its Settings row.
    // HideHelpOptionsLabels performs the actual UI transition after the
    // parent labels have been removed.
    pc_settings_selection_pending_.store(true, std::memory_order_release);
  }

  static void OpenHighScores() {
    auto* app = current_instance_;
    if (!app) {
      return;
    }
    high_scores_open_.store(true, std::memory_order_release);
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->high_scores_dialog_) {
        app->high_scores_dialog_->Open();
      }
    });
  }

  static bool IsHighScoresOpen() {
    return high_scores_open_.load(std::memory_order_acquire);
  }

  static void CloseHighScores() {
    auto* app = current_instance_;
    high_scores_open_.store(false, std::memory_order_release);
    if (!app) {
      return;
    }
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->high_scores_dialog_) {
        app->high_scores_dialog_->Close();
      }
    });
  }

  static void RequestPcSettingsClose() {
    if (pc_settings_open_.load(std::memory_order_acquire)) {
      pc_settings_close_requested_.store(true, std::memory_order_release);
    }
  }

  static bool ShouldForcePcSettingsConfirm() {
    return pc_settings_open_.load(std::memory_order_acquire) &&
           pc_settings_close_requested_.load(std::memory_order_acquire);
  }

  static bool IsPcSettingsOpen() {
    return pc_settings_open_.load(std::memory_order_acquire);
  }

  static void ClosePcSettings() {
    auto* app = current_instance_;
    pc_settings_open_.store(false, std::memory_order_release);
    pc_settings_close_requested_.store(false, std::memory_order_release);
    pc_settings_selection_pending_.store(false, std::memory_order_release);
    if (!app) {
      return;
    }
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->pc_settings_dialog_) {
        app->pc_settings_dialog_->Close();
        app->pc_settings_dialog_->ShowHelpOptionsLabels();
      }
    });
  }

  // The Help & Options scene was just created: it owns the host captions
  // until it closes.
  static void OnHelpOptionsSceneInit() {
    help_scene_live_.store(true, std::memory_order_release);
    ShowHelpOptionsLabels();
  }

  // Also reached from generic "Continue / go back" handlers that other
  // scenes share (the network alert box is one), so it only draws while the
  // Help & Options scene is actually alive.
  static void ShowHelpOptionsLabels() {
    auto* app = current_instance_;
    if (!app || !help_scene_live_.load(std::memory_order_acquire)) {
      return;
    }
    // Returning to the Help parent always ends the native Settings scene.
    // Clear the host layer even if XUI bypassed the Settings button callback.
    pc_settings_open_.store(false, std::memory_order_release);
    pc_settings_close_requested_.store(false, std::memory_order_release);
    pc_settings_selection_pending_.store(false, std::memory_order_release);
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->pc_settings_dialog_) {
        app->pc_settings_dialog_->Close();
        app->pc_settings_dialog_->ShowHelpOptionsLabels();
      }
    });
  }

  static void HideHelpOptionsLabels() {
    auto* app = current_instance_;
    if (!app) {
      return;
    }
    // Every Help child passes through this transition. Only the Settings row
    // marks itself beforehand, so the replacement panel cannot spill onto
    // How To Play, Controls, or Credits.
    const bool open_pc_settings =
        pc_settings_selection_pending_.exchange(false,
                                                std::memory_order_acq_rel);
    pc_settings_open_.store(open_pc_settings, std::memory_order_release);
    pc_settings_close_requested_.store(false, std::memory_order_release);
    app->app_context().CallInUIThreadDeferred([app, open_pc_settings]() {
      if (app->pc_settings_dialog_) {
        app->pc_settings_dialog_->Close();
        app->pc_settings_dialog_->HideHelpOptionsLabels();
        if (open_pc_settings) {
          app->pc_settings_dialog_->Open();
        }
      }
    });
  }

  static void CloseHelpOptionsLabels() {
    help_scene_live_.store(false, std::memory_order_release);
    auto* app = current_instance_;
    if (!app) {
      return;
    }
    pc_settings_open_.store(false, std::memory_order_release);
    pc_settings_close_requested_.store(false, std::memory_order_release);
    pc_settings_selection_pending_.store(false, std::memory_order_release);
    const bool return_to_pause = help_session_from_pause_;
    help_session_from_pause_ = false;
    if (return_to_pause) {
      // The Help session opened on top of the pause menu; restore its
      // captions now that the scene is being revealed again.
      ShowPauseMenuLabels();
    }
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->pc_settings_dialog_) {
        app->pc_settings_dialog_->Close();
        app->pc_settings_dialog_->CloseHelpOptionsLabels();
      }
    });
  }

  // Pause captions are owned by the live retail pause scene: they can only be
  // shown between that scene's init and its destructor (or the moment it
  // decides to quit to the main menu). Every show request below is gated on
  // this, so no stale latch can leave pause text drawn over another menu.
  static bool IsHelpSceneLive() {
    return help_scene_live_.load(std::memory_order_acquire);
  }

  // Every Aegis Wing scene's shared init (sub_920B0F88) and "shown again"
  // (sub_920B12B8) store the scene in this global, so it always names the
  // menu in front. The version label shows only while that is the main menu
  // and no host overlay or exit confirm is up.
  static constexpr uint32_t kFrontSceneAddress = 0x924C2614;
  // Runs on the guest thread at every pad poll. The host's own panels (PC
  // Settings, High Scores) are not XUI, so pointing there is not passed on.
  static void OnGuestInputPoll() {
    // Host code (the PC Settings panel) reads the pad state too; only the
    // game's own threads may move its menu focus.
    if (!rex::runtime::ThreadState::Get()) {
      return;
    }
    aegis_wing::UpdateMenuPointer(
        GetGuestMemory(),
        IsMenuUp() && !IsPcSettingsOpen() && !IsHighScoresOpen());
  }

  // A menu scene class appeared (shared scene init). Its vtable is kept so a
  // live menu can be told from gameplay; there are only about twenty.
  static void OnSceneInit(uint32_t scene) {
    auto* memory = GetGuestMemory();
    if (!memory || !scene) {
      return;
    }
    const uint32_t vtable = rex::memory::load_and_swap<uint32_t>(
        memory->TranslateVirtual<const uint32_t*>(scene));
    for (auto& slot : scene_vtables_) {
      uint32_t current = slot.load(std::memory_order_acquire);
      if (current == vtable) {
        return;
      }
      if (current == 0 &&
          slot.compare_exchange_strong(current, vtable, std::memory_order_acq_rel)) {
        return;
      }
    }
  }

  // A menu is up when the front scene is a live object of a known menu
  // class. In a level no menu scene exists: the last front scene was torn
  // down (its destructor resets the vtable) or its memory reused, so neither
  // matches. The host's own panels count as menus too.
  static bool IsMenuUp() {
    if (IsHighScoresOpen() || IsPcSettingsOpen()) {
      return true;
    }
    auto* memory = GetGuestMemory();
    if (!memory) {
      return false;
    }
    const uint32_t front = rex::memory::load_and_swap<uint32_t>(
        memory->TranslateVirtual<const uint32_t*>(kFrontSceneAddress));
    if (front < 0x40000000u) {
      return false;
    }
    const uint32_t vtable = rex::memory::load_and_swap<uint32_t>(
        memory->TranslateVirtual<const uint32_t*>(front));
    for (const auto& slot : scene_vtables_) {
      const uint32_t known = slot.load(std::memory_order_acquire);
      if (known == 0) {
        break;
      }
      if (known == vtable) {
        // In a level the front scene is CIngameMenu, which only listens for
        // the pause button: that is gameplay, not a menu.
        return !aegis_wing::IsLevelSceneInFront(memory);
      }
    }
    return false;
  }

  static bool ShouldShowVersionLabel() {
    const uint32_t main_menu = main_menu_scene_.load(std::memory_order_acquire);
    auto* memory = GetGuestMemory();
    if (!main_menu || !memory || IsHighScoresOpen() || IsPcSettingsOpen() ||
        IsPauseConfirmOpen() || IsHelpSceneLive()) {
      return false;
    }
    const uint32_t front = rex::memory::load_and_swap<uint32_t>(
        memory->TranslateVirtual<const uint32_t*>(kFrontSceneAddress));
    return front == main_menu;
  }

  static bool IsPauseSceneLive() {
    return pause_scene_.load(std::memory_order_acquire) != 0;
  }

  static void ShowPauseMenuLabels() {
    auto* app = current_instance_;
    if (!app || !IsPauseSceneLive()) {
      return;
    }
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->pc_settings_dialog_ && IsPauseSceneLive()) {
        app->pc_settings_dialog_->ShowPauseMenuLabels();
      }
    });
  }

  static void OnPauseSceneInit(uint32_t scene) {
    REXLOG_INFO("[PAUSE] scene init {:08X}", scene);
    pause_scene_.store(scene, std::memory_order_release);
    // A fresh pause entry invalidates any state from a previous pause session.
    help_session_from_pause_ = false;
    pc_settings_confirm_open_.store(false, std::memory_order_release);
    ShowPauseMenuLabels();
  }

  static void OnPauseSceneDestroyed(uint32_t scene) {
    REXLOG_INFO("[PAUSE] scene destroyed {:08X}", scene);
    uint32_t expected = scene;
    if (pause_scene_.compare_exchange_strong(expected, 0,
                                             std::memory_order_acq_rel)) {
      ClearPauseMenuState();
    }
  }

  static void OnPauseNavReturn(uint32_t scene) {
    // The pause scene was revealed again after a child scene closed (exit
    // confirm, Help & Options). The retail handler that calls this quits to
    // the main menu when the exit confirm was answered Yes: its pending action
    // (+48) is not idle (100) and the confirm's answer byte (+52) is set.
    // Reading that result replaces host-side Yes/No tracking, which could
    // drift from the real focus and re-show the pause over the main menu.
    auto* memory = GetGuestMemory();
    if (!memory || scene == 0) {
      return;
    }
    const uint32_t action = rex::memory::load_and_swap<uint32_t>(
        memory->TranslateVirtual<const uint32_t*>(scene + 48));
    const uint8_t answer =
        *memory->TranslateVirtual<const uint8_t*>(scene + 52);
    const bool quitting = action != 100 && answer != 0;
    REXLOG_INFO("[PAUSE] nav return {:08X} action={} answer={} -> {}", scene,
                action, answer, quitting ? "main menu" : "pause");
    pc_settings_confirm_open_.store(false, std::memory_order_release);
    if (quitting) {
      pause_scene_.store(0, std::memory_order_release);
      ClearPauseMenuState();
      return;
    }
    if (pause_scene_.load(std::memory_order_acquire) != scene) {
      return;
    }
    auto* app = current_instance_;
    if (app) {
      // Re-show once the child scene has faded out so two menus never draw
      // on top of each other.
      app->app_context().CallInUIThreadDeferred([app]() {
        if (app->pc_settings_dialog_) {
          app->pc_settings_dialog_->SchedulePauseMenuLabels(0.8);
        }
      });
    }
  }

  static void OnMainMenuShown(uint32_t scene) {
    // Nothing from a gameplay pause session or an earlier Help & Options
    // session can be on screen at the main menu.
    main_menu_scene_.store(scene, std::memory_order_release);
    pause_scene_.store(0, std::memory_order_release);
    ClearPauseMenuState();
    if (help_scene_live_.exchange(false, std::memory_order_acq_rel)) {
      CloseHelpOptionsLabels();
    }
  }

  static void NotePauseConfirmOpen() {
    // An exit-confirm child scene is up (pause, lobby or main menu origin).
    // Host captions stay hidden until it is answered.
    pc_settings_confirm_open_.store(true, std::memory_order_release);
  }

  static bool IsPauseConfirmOpen() {
    return pc_settings_confirm_open_.load(std::memory_order_acquire);
  }

  static void NotePauseConfirmAnswered() {
    // Only ends the "confirm is up" state. Whether the pause captions come
    // back is decided by OnPauseNavReturn from the game's own result.
    pc_settings_confirm_open_.store(false, std::memory_order_release);
  }

  static void NotePauseHelpChoice() {
    // The pause dispatcher observed A being pressed on its Help & Options
    // button; the Help parent that appears next lives on top of the pause
    // scene, so closing it must restore the pause captions.
    help_session_from_pause_ = true;
  }

  static void HidePauseMenuLabels() {
    auto* app = current_instance_;
    if (!app) {
      return;
    }
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->pc_settings_dialog_) {
        app->pc_settings_dialog_->HidePauseMenuLabels();
      }
    });
  }

  static void PulsePcSettingsNav() {
    // Alternates the synthesized direction each time: the hidden native
    // focus has two positions, so every press moves it and plays the blip.
    const bool down = pc_settings_nav_down_next_.load(std::memory_order_acquire);
    pc_settings_nav_down_next_.store(!down, std::memory_order_release);
    pc_settings_nav_ks_vk_.store(down ? 0x5811 : 0x5810,
                                 std::memory_order_release);  // VK_DPAD_DOWN/UP.
    pc_settings_nav_ks_state_.store(1, std::memory_order_release);
  }

  static bool ConsumePcSettingsNavKeystroke(uint16_t* out_vk,
                                            uint16_t* out_flags) {
    int state = pc_settings_nav_ks_state_.load(std::memory_order_acquire);
    if (state == 0) {
      return false;
    }
    const int next = (state == 1) ? 2 : 0;
    if (!pc_settings_nav_ks_state_.compare_exchange_strong(
            state, next, std::memory_order_acq_rel)) {
      return false;
    }
    *out_vk = pc_settings_nav_ks_vk_.load(std::memory_order_acquire);
    *out_flags = (state == 1) ? 0x0001 : 0x0002;  // KEYDOWN / KEYUP.
    return true;
  }

  static void WriteGuestKeystroke(uint32_t address, uint16_t vk,
                                  uint16_t flags) {
    auto* app = current_instance_;
    if (!app || !app->runtime() || !app->runtime()->memory()) {
      return;
    }
    auto* p = app->runtime()->memory()->TranslateVirtual<uint8_t*>(address);
    auto put16 = [&](uint32_t a, uint16_t v) {
      p[a] = uint8_t(v >> 8);
      p[a + 1] = uint8_t(v & 0xFF);
    };
    put16(0, vk);      // virtual_key
    put16(2, 0);       // unicode
    put16(4, flags);   // flags
    p[6] = 0;          // user_index
    p[7] = 0;          // hid_code
  }

  static bool GetPcFullscreen() {
    const auto value = rex::cvar::GetFlagByName("fullscreen");
    return value == "true" || value == "1";
  }

  static bool GetPcVSync() {
    const auto value = rex::cvar::GetFlagByName("vsync");
    return value == "true" || value == "1";
  }

  static bool GetPcShowFps() {
    const auto value = rex::cvar::GetFlagByName("show_fps");
    return value == "true" || value == "1";
  }

  static ImFont* GetPcRegularFont() { return pc_regular_font_; }
  static ImFont* GetPcBoldFont() { return pc_bold_font_; }

  static void* GetPcInputSystem() {
    auto* app = current_instance_;
    return app && app->runtime() ? app->runtime()->input_system() : nullptr;
  }

  static rex::memory::Memory* GetGuestMemory() {
    auto* app = current_instance_;
    return app && app->runtime() ? app->runtime()->memory() : nullptr;
  }

  static int GetPcSoundVolume() {
    return GetGuestVolume(kSoundVolumeAddress);
  }

  static int GetPcMusicVolume() {
    return GetGuestVolume(kMusicVolumeAddress);
  }

  static void SetPcSoundVolume(int volume) {
    SetGuestVolume(kSoundVolumeAddress, volume);
  }

  static void SetPcMusicVolume(int volume) {
    SetGuestVolume(kMusicVolumeAddress, volume);
  }

  static int GetPcResolutionIndex() {
    const auto width = rex::cvar::GetFlagByName("video_mode_width");
    const auto height = rex::cvar::GetFlagByName("video_mode_height");
    if (width == "1280" && height == "720") return 0;
    if (width == "2560" && height == "1440") return 2;
    if (width == "3840" && height == "2160") return 3;
    return 1;
  }

  static void SetPcFullscreen(bool fullscreen) {
    auto* app = current_instance_;
    if (!app) {
      return;
    }
    rex::cvar::SetFlagByName("fullscreen", fullscreen ? "true" : "false");
    SavePcSettings();
    app->app_context().CallInUIThreadDeferred([app, fullscreen]() {
      if (app->window()) {
        app->window()->SetFullscreen(fullscreen);
      }
    });
  }

  static void SetPcVSync(bool enabled) {
    rex::cvar::SetFlagByName("vsync", enabled ? "true" : "false");
    SavePcSettings();
  }

  static void SetPcShowFps(bool enabled) {
    rex::cvar::SetFlagByName("show_fps", enabled ? "true" : "false");
    SavePcSettings();
  }

  // The player's name in LAN games and local high scores: net_player_name,
  // which Setup fills with a random name, else the Windows user name. The
  // game asks for it whenever it shows or sends it, so a change applies at
  // once; saves stay with the local profile, whose folder name never changes.
  static std::string GetPcPlayerName() {
    return rex::kernel::xam::netplay::ConfiguredPlayerName("User");
  }

  static void SetPcPlayerName(const std::string& name) {
    rex::cvar::SetFlagByName("net_player_name", name);
    SavePcSettings();
  }

  static void SetPcResolutionIndex(int index) {
    struct Resolution {
      int width;
      int height;
    };
    // Output sizes only. The internal render scale is fixed (see
    // kRenderScale), so every change here is a live window resize.
    constexpr std::array<Resolution, 4> kResolutions = {
        {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}}};
    if (index < 0 || index >= static_cast<int>(kResolutions.size())) {
      return;
    }

    if (!current_instance_) {
      return;
    }
    const auto resolution = kResolutions[index];
    rex::cvar::SetFlagByName("video_mode_width",
                            std::to_string(resolution.width));
    rex::cvar::SetFlagByName("video_mode_height",
                            std::to_string(resolution.height));
    SavePcSettings();

    // Apply the output size immediately by resizing the presentation window
    // (SetLogicalSize fits windowed mode inside the desktop and recenters).
    auto* app = current_instance_;
    app->app_context().CallInUIThreadDeferred([app, resolution]() {
      if (app->window()) {
        static_cast<rex::ui::WindowSDL*>(app->window())
            ->SetLogicalSize(uint32_t(resolution.width),
                             uint32_t(resolution.height));
      }
    });
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto app_directory = rex::filesystem::GetExecutableFolder();
    const auto user_data = app_directory / "userdata";

    paths.game_data_root = app_directory / "assets";
    paths.user_data_root = user_data;
    paths.cache_root = user_data / "cache";

#ifdef _WIN32
    // Release packages intentionally omit the copyrighted game data. Give a
    // clear, actionable message before ReXGlue attempts to mount an empty
    // assets directory and reports a much less useful startup failure.
    if (!std::filesystem::exists(paths.game_data_root / "default.xex")) {
      MessageBoxW(
          nullptr,
          L"Aegis Wing game data has not been installed yet.\n\n"
          L"Run Setup Aegis Wing.exe and choose your Xbox 360 Aegis Wing "
          L"package.",
          L"Aegis Wing Setup",
          MB_OK | MB_ICONINFORMATION);
      std::_Exit(0);
    }
#endif
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    aegis_wing::InstallCrashReporter();
    // XUI builds and reuses a render-target-backed glyph atlas. A delayed or
    // disabled resolve can permanently leave individual characters blank.
    // Enforce delayed readback even if an older saved config lacks the key.
    // This avoids the visible stalls caused by fully synchronous readback.
    rex::cvar::SetFlagByName("readback_resolve", "fast");
    // Render internally at a fixed 2x (2560x1440) at every output size. The
    // GPU backend bakes the scale into its render-target/texture caches and
    // translated shaders at startup, so it cannot follow live resolution
    // changes; a fixed scale keeps every output size sharp with no restart.
    // Overrides any older saved per-resolution value.
    rex::cvar::SetFlagByName("resolution_scale", std::to_string(kRenderScale));
    // LAN play wakes the network only when the player creates or joins a LAN
    // game, but the title reads the profile's sign-in state once at boot. If
    // it sees an offline profile then, joining a LAN lobby later crashes it
    // while it reads the remote player's data. Report the LIVE sign-in from
    // the start; this opens no sockets (binds stay on loopback until LAN play
    // wakes), so a solo player is still never asked about the firewall.
    rex::cvar::SetFlagByName("xlive_report_online", "true");
    // Keyboard: Enter is Start (Esc stays Start in a level and is Back in a
    // menu), and the X and Y keys are the X and Y buttons. Only the retail
    // defaults are replaced, so a player's own binds are left alone.
    auto upgrade_bind = [](const char* name, const char* old_default,
                           const char* value) {
      if (rex::cvar::GetFlagByName(name) == old_default) {
        rex::cvar::SetFlagByName(name, value);
      }
    };
    upgrade_bind("keybind_start", "Escape", "Escape,Return");
    upgrade_bind("keybind_x", "R", "X,R");
    upgrade_bind("keybind_y", "E", "Y,E");
    config.gpu_plugin = "xenos";
  }

  void OnPostLoadXexImage() override {
    aegis_wing::ApplyPcPatches(runtime());
  }

  // Exit Game ends the game program. ReXGlue then quits through its normal
  // subsystem teardown, which can deadlock: the window closes but the process
  // stays, invisible, still holding the game's network port, and the next
  // launch boots into a broken menu. Leave the way closing the window does
  // (ReXApp::OnClosing): flush the logs and end the process. Settings, saves
  // and high scores are already written when they change.
  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    REXLOG_INFO("Game exited; ending the process.");
    rex::FlushLogging();
    std::_Exit(0);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    // Players get their settings from the game's own Help & Options >
    // Settings panel, which applies and saves through the config system
    // directly. ReXGlue's developer overlays (F3 debug, F4 runtime settings,
    // F7 achievements, backtick console) are switched off: a stray key press
    // should never drop a debug window over the game.
    for (const char* bind : {"bind_debug_overlay", "bind_settings",
                             "bind_achievements", "bind_console"}) {
      rex::ui::UnregisterBind(bind);
    }
    // Pointing at menu rows with the mouse (see aegis_wing_menu_pointer.cpp).
    rex::input::mnk::MnkInputDriver::SetGuestPollHook(&OnGuestInputPoll);
    // "Press Any Key" on the title screen when no gamepad is connected.
    aegis_wing::AttachTitlePrompt(window());
    pc_settings_dialog_ =
        std::make_unique<AegisWingPcSettingsDialog>(drawer);
    high_scores_dialog_ =
        std::make_unique<AegisWingHighScoresDialog>(drawer);
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    const auto media_directory =
        rex::filesystem::GetExecutableFolder() / "assets" / "Media";
    const auto regular_path = (media_directory / "ERASMD.ttf").string();
    const auto bold_path = (media_directory / "ERASBD.ttf").string();

    ImFontConfig font_config{};
    font_config.OversampleH = 2;
    font_config.OversampleV = 2;
    font_config.PixelSnapH = false;
    pc_regular_font_ = atlas->AddFontFromFileTTF(
        regular_path.c_str(), 48.0f, &font_config,
        atlas->GetGlyphRangesDefault());

    ImFontConfig bold_config{};
    bold_config.OversampleH = 2;
    bold_config.OversampleV = 2;
    bold_config.PixelSnapH = false;
    pc_bold_font_ = atlas->AddFontFromFileTTF(
        bold_path.c_str(), 64.0f, &bold_config,
        atlas->GetGlyphRangesDefault());
  }

  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    return nullptr;
  }

  std::unique_ptr<rex::ui::AchievementNotificationDialog>
  CreateAchievementNotificationDialog() override {
    return nullptr;
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
 private:
  static constexpr int kRenderScale = 2;

  // These are the two native volume fields used by the original Audio
  // Settings scene. Keeping the PC panel connected to them preserves the
  // game's actual sound/music split and its existing profile-save path.
  static constexpr uint32_t kSoundVolumeAddress = 0x924C2598;
  static constexpr uint32_t kMusicVolumeAddress = 0x924C23E0;

  static int GetGuestVolume(uint32_t address) {
    auto* app = current_instance_;
    if (!app || !app->runtime() || !app->runtime()->memory()) {
      return 100;
    }
    const auto* value =
        app->runtime()->memory()->TranslateVirtual<const float*>(address);
    const float volume = rex::memory::load_and_swap<float>(value);
    return std::clamp(static_cast<int>(volume * 100.0f + 2.5f) / 5 * 5,
                      0, 100);
  }

  static void SetGuestVolume(uint32_t address, int volume) {
    auto* app = current_instance_;
    if (!app || !app->runtime() || !app->runtime()->memory()) {
      return;
    }
    auto* value = app->runtime()->memory()->TranslateVirtual<float*>(address);
    rex::memory::store_and_swap<float>(
        value, static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f);
  }

  inline static ImFont* pc_regular_font_ = nullptr;
  inline static ImFont* pc_bold_font_ = nullptr;
  inline static std::atomic_bool pc_settings_open_ = false;
  inline static std::atomic_bool pc_settings_close_requested_ = false;
  inline static std::atomic_bool pc_settings_selection_pending_ = false;
  inline static std::atomic_bool high_scores_open_ = false;

  // PC settings navigation sound: the native Settings scene under the panel
  // plays its XUI move blip whenever focus flips between its two (hidden)
  // focusable controls. Each PC-panel row change pulses one frame of D-pad
  // into the guest input, alternating direction so focus always moves.
  inline static std::atomic_int pc_settings_nav_ks_state_ = 0;  // 0 idle, 1 press, 2 release
  inline static std::atomic_uint16_t pc_settings_nav_ks_vk_ = 0;
  inline static std::atomic_bool pc_settings_nav_down_next_ = false;
  // True while an exit-confirm child scene is up: host captions stay hidden
  // until it is answered.
  inline static std::atomic_bool pc_settings_confirm_open_ = false;
  // Guest address of the live retail pause scene, 0 when none.
  inline static std::atomic_uint32_t pause_scene_ = 0;
  // True between the Help & Options scene's init and its close.
  inline static std::atomic_bool help_scene_live_ = false;
  // The main menu scene, from its own setup (0 before it first shows).
  inline static std::atomic_uint32_t main_menu_scene_ = 0;
  // vtables of the menu scene classes seen so far (0 = unused slot).
  inline static std::array<std::atomic_uint32_t, 32> scene_vtables_{};

  static void ClearPauseMenuState() {
    help_session_from_pause_ = false;
    pc_settings_confirm_open_.store(false, std::memory_order_release);
    auto* app = current_instance_;
    if (!app) {
      return;
    }
    app->app_context().CallInUIThreadDeferred([app]() {
      if (app->pc_settings_dialog_) {
        app->pc_settings_dialog_->ClearPauseMenuLabels();
      }
    });
  }

  // Set by the recompiled pause dispatcher when A is pressed on its Help &
  // Options button. Closing a Help session while this is set reveals the
  // pause scene, so its host-drawn captions must be shown again.
  inline static bool help_session_from_pause_ = false;

  static void SavePcSettings() {
    auto* app = current_instance_;
    if (app && !app->config_path().empty()) {
      rex::cvar::SaveConfig(app->config_path());
    }
  }

  std::unique_ptr<AegisWingPcSettingsDialog> pc_settings_dialog_;
  std::unique_ptr<AegisWingHighScoresDialog> high_scores_dialog_;
};
