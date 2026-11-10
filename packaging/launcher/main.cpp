// Aegis Wing XBLA PC Recomp - launcher entry point.
//
//   Aegis Wing.exe                 start the game; shows setup first when the
//                                  game data has not been imported (or a
//                                  needed file is missing)
//   Aegis Wing.exe --setup         open the setup window
//
// For automated tests (no window):
//   --setup --unattended [--package <file or folder>] [--extras stageselect,fullscreen,shortcut]
//           [--shortcut-folder <dir>] [--save-folder <dir> | --save-folder default]
//   Exit code 0 imported, 1 failed, 2 several packages found (pass --package), 3 none found,
//   4 the save folder could not be changed.
//
// Any other arguments are passed on to the game.
//
// A GUI-subsystem program, so starting it never flashes a console.
#include "launcher.h"

#include <objbase.h>
#include <shellapi.h>

#define AW_WIDEN2(x) L##x
#define AW_WIDEN(x) AW_WIDEN2(x)
#define AW_STRINGIZE2(x) #x
#define AW_STRINGIZE(x) AW_STRINGIZE2(x)

namespace aw {
const wchar_t* const kVersion = AW_WIDEN(AW_STRINGIZE(AW_VERSION));
}

namespace {

using namespace aw;

int RunUnattended(Paths p, const SetupOptions& options, const std::wstring& extras, const std::wstring& save_folder) {
  Log(L"Unattended setup");
  std::vector<Source> found;
  std::wstring why;
  if (!options.package.empty()) {
    found = SourcesIn(options.package, &why);
  } else {
    found = DetectSources(p);
    why = L"no package found";
  }
  if (found.size() != 1) {
    Log(L"Unattended: " + (found.empty() ? why : std::to_wstring(found.size()) + L" candidates, pass --package"));
    return found.empty() ? 3 : 2;
  }
  std::atomic<bool> cancel{false};
  const fs::path saves_from = found[0].kind == SourceKind::PreviousInstall ? found[0].path : FindPreviousInstall(p);
  ImportResult result = Import(p, found[0], saves_from, cancel, nullptr);
  if (!result.ok) return 1;
  if (!save_folder.empty()) {
    std::wstring note;
    const fs::path folder = save_folder == L"default" ? fs::path() : SaveFolderFor(fs::absolute(save_folder));
    const bool moved = SetSaveFolder(p, folder, nullptr, &note);
    if (!note.empty()) Log(L"Unattended: " + note);
    if (!moved) return 4;
  }
  Extras want;
  want.stage_select = extras.find(L"stageselect") != std::wstring::npos;
  want.fullscreen = extras.find(L"fullscreen") != std::wstring::npos;
  want.shortcut = extras.find(L"shortcut") != std::wstring::npos;
  // As on a first setup: unticked extras change nothing; never replace a shortcut that is not ours.
  const std::wstring note = ApplyExtras(p, want, Extras{}, options.shortcut_folder, nullptr);
  if (!note.empty()) Log(L"Unattended: " + note);
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool setup = false, unattended = false;
  std::wstring extras, save_folder;
  SetupOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--setup") {
      setup = true;
    } else if (arg == L"--unattended") {
      unattended = true;
    } else if (arg == L"--package" && i + 1 < argc) {
      options.package = argv[++i];
    } else if (arg == L"--extras" && i + 1 < argc) {
      extras = argv[++i];
    } else if (arg == L"--shortcut-folder" && i + 1 < argc) {
      options.shortcut_folder = argv[++i];
    } else if (arg == L"--save-folder" && i + 1 < argc) {
      save_folder = argv[++i];
    } else {
      options.game_args += (options.game_args.empty() ? L"" : L" ") + Quote(arg);
    }
  }
  if (argv) LocalFree(argv);

  const Paths p = MakePaths();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  // The normal case: everything is in place, so start the game at once.
  std::wstring missing;
  bool installed = IsInstalled(p, &missing);
  if (!installed) {
    RecoverInterruptedSwap(p);
    installed = IsInstalled(p, &missing);
  }
  // A save folder of the player's own that cannot be reached (a drive that
  // is not connected): say so and open setup rather than starting the game
  // with no saves.
  if (installed && !setup && options.package.empty() && !SaveFolderReachable(p)) {
    LogTo(p.logs);
    Log(L"Save folder not reachable: " + p.userdata.wstring());
    const std::wstring text = L"Your Aegis Wing save folder cannot be reached:\n\n" + p.userdata.wstring() +
                              L"\n\nIf it is on a drive that is not connected, connect it and start Aegis Wing "
                              L"again. Setup opens now so you can choose another folder.";
    MessageBoxW(nullptr, text.c_str(), L"Aegis Wing", MB_OK | MB_ICONWARNING);
    setup = true;
  }
  if (installed && !setup && options.package.empty()) {
    if (GetFileAttributesW(p.config.c_str()) == INVALID_FILE_ATTRIBUTES) {
      LogTo(p.logs);
      EnsureSettings(p);
    }
    RepairShortcut(p, options.shortcut_folder);
    std::wstring error;
    int code = 0;
    if (GameRunning(p)) {
      // A second copy would fight the first for its network port; leave it be.
      MessageBoxW(nullptr, L"Aegis Wing is already running from this folder.", L"Aegis Wing",
                  MB_OK | MB_ICONINFORMATION);
      code = 1;
    } else if (!LaunchGame(p, options.game_args, &error)) {
      LogTo(p.logs);
      Log(L"FAILED to start the game: " + error);
      MessageBoxW(nullptr, error.c_str(), L"Aegis Wing", MB_OK | MB_ICONERROR);
      code = 1;
    }
    CoUninitialize();
    return code;
  }

  // Setup. One window per folder: a second launch brings the first to the front.
  std::wstring mutex_name = L"Local\\AegisWingSetup-";
  for (wchar_t c : p.root.wstring()) mutex_name += (c == L'\\' || c == L':') ? L'_' : static_cast<wchar_t>(towlower(c));
  if (mutex_name.size() > 250) mutex_name.resize(250);
  HANDLE mutex = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(L"AegisWingSetup", nullptr)) {
      ShowWindow(other, SW_RESTORE);
      SetForegroundWindow(other);
    }
    CloseHandle(mutex);
    CoUninitialize();
    return 0;
  }

  LogTo(p.logs);
  options.forced = installed;
  options.reason = installed ? L"--setup" : L"missing " + missing;
  const int code = unattended ? RunUnattended(p, options, extras, save_folder) : RunSetupWindow(instance, p, options);
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  CoUninitialize();
  return code;
}
