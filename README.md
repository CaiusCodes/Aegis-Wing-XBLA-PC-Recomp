# Aegis Wing PC recompilation

Native Windows static recompilation of the Xbox 360 Arcade release of
**Aegis Wing**, using ReXGlue 0.9.0 and its Xenos graphics backend.

## Supported game build

- Title ID: `5841083C`
- Media ID: `3FAB2CBE`
- XEX version: `0.0.1.3`
- XEX timestamp: 23 March 2007

The extracted retail files live in `game/`. They are not generated source and
must come from a legally obtained copy of the game.

## Build (Windows x64)

Prerequisites are Git, CMake, Ninja and Clang.

ReXGlue is the `rexglue-sdk` submodule, pinned to upstream v0.9.0
(`3eb9b511b4140d2769e27be63eae57d41bfa2afa`). This port's SDK changes (LAN
play, keyboard/mouse menus, local leaderboards, graphics and window fixes) are
one patch, `patches/rexglue.patch`, which CMake applies to the submodule at
configure time (`cmake/RexgluePatch.cmake`); the SDK is never edited by hand.
ReXGlue's dependencies sit deep in nested submodules, so enable long paths
when cloning. Shallow submodules fetch the same pinned commits without their
history (about 300 MB of Git data instead of about 1 GB):

```powershell
git -c core.longpaths=true clone --recurse-submodules --shallow-submodules <this repository>
# or, in an existing clone:
git -c core.longpaths=true submodule update --init --recursive --depth 1
```

Extract your own Aegis Wing package into `game/`
(`.\tools\Extract-STFS.ps1 -Path <package> -OutputDir game`), then build the
code generator, generate the recompiled code and build the game:

```powershell
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --target rexglue
.\tools\Regenerate-Code.ps1
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --parallel
```

The post-build step stages the extracted game as `assets/` beside the host
executable. Saves, achievement state, and shader caches are kept in the
portable `userdata/` directory.

The executable is produced at:

```text
out/build/win-amd64-relwithdebinfo/Aegis Wing.exe
```

## Portable release package

To create a user-facing portable package after building, run:

```powershell
.\tools\Make-Release.ps1
```

This produces `out/release/.package-staging/Aegis Wing XBLA Recomp/` and the
matching `Aegis-Wing-XBLA-Recomp-v<version>.zip`, in the standard layout shared
by the XBLA recomp projects. Before Setup runs, the release folder holds only:

```text
Aegis Wing XBLA Recomp/
  Setup Aegis Wing.exe
  README.txt
  licenses/
```

`Setup Aegis Wing.exe` (built from `packaging/installer_stub.cpp` by
`packaging/Build-Stubs.ps1`, which embeds the asInvoker manifest Windows needs
before it will run anything named "Setup" without elevation) carries its
install payload appended to the executable: the game executable, ReXGlue
runtime and Visual C++ DLLs, `resources/` (the Setup script, STFS extractor,
menu patcher and default settings) and `release-manifest.json` (every payload
file with size and SHA-256). Setup unpacks that payload to a temporary folder,
checks the player's own legally obtained Aegis Wing STFS/LIVE package (title
5841083C, content type 000D0000, the exact `default.xex` build), and only then
creates `Game/`:

```text
Aegis Wing XBLA Recomp/
  Setup Aegis Wing.exe
  README.txt
  licenses/
  Game/
    Aegis Wing.exe
    resources/
    release-manifest.json
    assets/              unpacked, PC-patched game data
    userdata/            saves, high scores, shader cache
    aegis_wing.toml      settings (kept on reinstall)
    logs/                game and Setup logs
```

Running Setup again replaces the files listed in `Game/release-manifest.json`
(removing ones a newer release dropped, if unchanged), rebuilds `Game/assets`,
keeps the player's data, and tidies away the files of the older root layout
(root play launcher, `resources/`, `logs/`) that it can positively identify.
The game resolves everything relative to its own executable, so it runs from
`Game/` whatever the working directory. The release never contains game data;
`Make-Release.ps1` refuses to package anything that looks like it, and fails if
the release folder holds anything but Setup, README.txt and licenses.

For sharing, `Make-Release.ps1` also ships the Microsoft Visual C++ runtime
DLLs the binaries import (found with `llvm-readobj`; the release fails on any
import it cannot satisfy), copies the third-party license texts from the SDK
into `licenses/third-party/`, and refuses to package any file that embeds a
personal `C:\Users\<name>\` path. `CMakeLists.txt` strips source-tree paths
from `__FILE__` with `-ffile-prefix-map` for the same reason.

`generated/default` is never edited by hand. It is the output of ReXGlue's
code generator for the supported `game/default.xex` (SHA-256
`C57F6A8136EC0C7D966ED83717D1F211CACF64F7D756182E7183C416A55F2873`) plus the
PC port's patches in `tools/Apply-GeneratedPcPatches.ps1` (39 patches: menu
hooks, pause captions, local High Scores, the PC Settings panel, input
filtering, the title screen prompt and the image-size padding). To regenerate it:

```powershell
.\tools\Regenerate-Code.ps1
```

This checks the XEX, generates into a temporary folder, applies every patch
there and only then replaces `generated/default`; any failure leaves the
existing code untouched. `-OutputDir <folder>` writes elsewhere, for example
to compare a fresh generation with the current one.
`.\tools\Apply-GeneratedPcPatches.ps1 -Check` verifies that a tree has every
patch. `rexglue.exe` is built with the game, so build once before
regenerating.

## LAN multiplayer

The game's original Xbox LIVE Player Match code drives local-network play.
On the multiplayer menu:

- **Join LAN Game** searches the network and shows the game's own list of hosted
  games, with host, difficulty, ping and seats, plus Refresh (Y) and Join (A).
- **Create LAN Game** hosts a game other PCs can find.
- **Local Multiplayer** is unchanged shared-screen co-op.

Any network the PCs share works, including a virtual LAN such as ZeroTier,
Radmin VPN or Tailscale. Discovery uses UDP port 3076. Networking stays
switched off until Join LAN Game searches or Create LAN Game hosts (the
single-player game also creates a session, but a local one, without the
peer-network flag, which does not wake it), so a player who only plays alone
or in local co-op is never asked for firewall access. To reach a host that
broadcast discovery cannot see, set `net_join_address` in `aegis_wing.toml` to
its address.

The player's name (gamertag) is `net_player_name` in `aegis_wing.toml`. Setup
gives a new install a random name from `packaging/player_names.txt`, and the
player can change it under Help & Options > Settings > Player Name (Left/Right
or A step through the same list; Enter or a click types a name of up to 15
characters). The list is compiled into the game from the same file.

## Runtime controls

- Xbox controller: original Xbox 360 controls
- Main menu `Help & Options` -> `Settings`: PC display settings
- `F4`: ReXGlue diagnostics and fallback runtime settings
- `Esc`: release mouse / close overlays as applicable

The in-game Settings menu provides:

- Windowed or fullscreen display mode
- 1280x720, 1920x1080, 2560x1440, or 3840x2160 output
- VSync on/off
- FPS counter on/off

Changes are applied immediately and saved to `aegis_wing.toml`.

## Current status (2 September 2026)

The XEX has been extracted and fully translated to C++, and the host project is
configured for the Xenos GPU backend. A small compatibility layer supplies the
Xbox Live Vision camera exports missing from the packaged ReXGlue 0.9.0
runtime; it reports no camera connected, which is the expected PC behavior.

The generated host image span is padded from the retail XEX size (`0x4EB000`)
to `0x4F0000`. ReXGlue places its indirect-call table directly after that span
and requires the table to start on a 64 KiB boundary. The ReXGlue patch also
makes the runtime place that table in the `0x9...` XEX heap Aegis Wing uses.

The manifest identifies the title's setjmp/longjmp routines and the known
data-table callbacks that the static scanner cannot discover. With those fixes,
the native executable currently:

- initializes Direct3D 12 on the Xenos backend;
- initializes audio and input and detects an Xbox controller;
- mounts the complete retail asset tree and loads the XEX and achievements;
- enters guest execution, translates/loads graphics pipelines, and survives
  automated startup smoke tests without crashing;
- boots to the menu and is playable with a controller;
- removes Achievements and Leaderboards from the main and pause menus;
- offers Join LAN, Create LAN and Local Multiplayer on the multiplayer menu;
- skips the Xbox Live offline/leaderboard warning when starting gameplay;
- relabels the Xbox `Return to Arcade` action as `Exit Game`;
- opens a native PC display-settings panel from the game's Settings entry.

The port is therefore **bootable and broadly playable**, but still needs a
hands-on regression pass before calling it release-ready. The current build
contains a targeted repair for the blank pause menu by explicitly restoring the
pause root, title, Resume, Main Menu, and Help & Options controls. Confirm that
fix during play, along with returning from the PC settings panel, changing each
resolution/display option, level transitions, XMA audio, saving, and controller
behavior.

The runtime can log missing optional sample-framework shader files that are not
contained in the retail package, plus one non-fatal heap release warning; these
messages have not terminated the smoke tests.

Generated menu hooks are reapplied by `tools/Regenerate-Code.ps1`. The build's
post-build step also patches the staged copy of `assets/Media/Wingmen.xzp` with
PC menu wording, removes the Leaderboards and Achievements captions, and
compacts the remaining main/pause menu rows. The multiplayer scene is retitled
`Local Multiplayer`; Quick Match, Custom Match, Create Match, the online-play
notice, and all staged Xbox Live wording are removed. Internal control IDs are
preserved for safe initialization, then disabled and hidden after scene setup;
stale events from those controls are discarded so they cannot receive focus,
display legacy tooltips, or launch Xbox-only actions. The compiled up/down
navigation links are also rewritten so the main menu cycles directly through
Single Player, Multiplayer, Help & Options, and Exit Game, the pause menu cycles
through Resume Game, Help & Options, and Exit Game, and the multiplayer menu
focuses and cycles only on Local Multiplayer. The generated gameplay hook runs
the normal scene-start callback, then skips the obsolete in-game Xbox Live
leaderboard warning before its XUI message box is created. The legally obtained
source game files are left untouched.
