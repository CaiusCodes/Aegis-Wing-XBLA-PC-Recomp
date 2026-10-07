# Aegis Wing XBLA PC Recomp

A native Windows PC version of **Aegis Wing**, the 2007 Xbox Live Arcade
shoot-'em-up, made by statically recompiling the original Xbox 360 game with
[ReXGlue](https://github.com/rexglue/rexglue-sdk). The game runs as a normal
Windows program with keyboard, mouse and controller support, PC display
settings, and LAN multiplayer in place of Xbox LIVE.

> **You need your own copy of Aegis Wing.** This project contains no game
> files: no program, graphics, sound or music from the original game. Setup
> reads the Xbox 360 Aegis Wing package you provide, checks that it is the
> supported version, and builds the game folder from it on your PC.

## Requirements

- Windows 10 or 11, 64-bit
- A graphics card with DirectX 12 support
- Your own Aegis Wing Xbox Live Arcade package (the Xbox 360 game file for
  Title ID `5841083C`)

Nothing else needs installing; the Visual C++ runtime is included. An Xbox
controller is optional.

## Install and play

1. Download the release ZIP from the
   [Releases](https://github.com/CaiusCodes/Aegis-Wing-XBLA-PC-Recomp/releases)
   page and extract it to a writable folder (not Program Files).
2. Run **Setup Aegis Wing.exe**.
3. Choose your Aegis Wing package, or drag it onto the Setup window. Setup
   tells you straight away if it is the wrong file or an unsupported version.
4. Choose **Install game**, pick any optional extras (fullscreen, desktop
   shortcut, Unlock Stage Select), then **Play now**.

Afterwards, start the game with `Game\Aegis Wing.exe`. Saves, high scores and
settings stay inside the extracted folder, so it can be moved or backed up as
a whole. Run Setup again at any time to reinstall or change the extras; your
saves and settings are kept.

Setup only reads your package and never changes it. Neither Setup nor the game
contacts Xbox Live.

## Features

- Single player, local co-op for up to four players, and **LAN multiplayer**
  (Multiplayer → Join LAN Game / Create LAN Game) over a local network or a
  virtual LAN such as ZeroTier, Radmin VPN or Tailscale
- Keyboard and mouse in menus and gameplay, alongside Xbox controllers
- In-game **Settings** (Help & Options → Settings): player name, sound and
  music volume, windowed or fullscreen, resolution up to 3840×2160, VSync and
  an FPS counter
- Local high-score table in place of the Xbox LIVE leaderboards
- Portable install: everything lives in one folder

### Controls

| Key / button | Action |
|---|---|
| Arrow keys | Fly, move through menus |
| Space | A (fire, confirm) |
| Shift | B (back) |
| Escape | Back in menus, pause during play |
| Enter | Start |
| X, Y | X and Y buttons |
| Mouse | Point at menu items; left click is A, right click is B |

An Xbox controller works as on the console. Without a controller, the title
screen says *Press Any Key*, and any key or click continues.

## Known limitations

- Windows only for now.
- Only the Xbox Live Arcade release with Title ID `5841083C` (game program
  version 0.0.1.3) is supported; Setup refuses other builds.
- Xbox LIVE features are not available: online play works through LAN or a
  virtual LAN only, and achievements and online leaderboards are replaced or
  removed.
- The local high-score table lists player one as "User" rather than the
  player name.
- Setup and the game are not digitally signed, so Windows SmartScreen may warn
  the first time; choose *More info* → *Run anyway*. Windows also asks for
  network access the first time you join or create a LAN game.

If something goes wrong, include the newest files from `Game\logs` when
reporting it.

## Building from source

For developers. You need Git, CMake, Ninja, Clang and Windows PowerShell, plus
your own Aegis Wing package.

ReXGlue is the `rexglue-sdk` submodule, pinned to upstream v0.9.0. This
project's changes to it are kept in `patches/rexglue.patch`, which CMake
applies automatically. ReXGlue's dependencies are nested deeply, so enable
long paths when cloning:

```powershell
git -c core.longpaths=true clone --recurse-submodules --shallow-submodules https://github.com/CaiusCodes/Aegis-Wing-XBLA-PC-Recomp.git
```

Extract your package into `game/`, build the code generator, generate the
recompiled code from your own `default.xex`, then build the game:

```powershell
.\tools\Extract-STFS.ps1 -Path <your package> -OutputDir game
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --target rexglue
.\tools\Regenerate-Code.ps1
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --parallel
```

The game is built to `out/build/win-amd64-release/Aegis Wing.exe`.
`tools\Regenerate-Code.ps1` checks the XEX, runs the code generator and
applies the PC changes in `tools/Apply-GeneratedPcPatches.ps1`; the generated
code is never edited by hand or committed. `.\tools\Make-Release.ps1` builds
the release ZIP (Setup, README and licences; never any game data).

## Licence and credits

This project's own code is released under the BSD 3-Clause licence (see
[LICENSE](LICENSE), which also lists what it does not cover).
`tools/Extract-STFS.ps1` is derived from
[Velocity](https://github.com/hetelek/Velocity) and is GPL-3.0. ReXGlue,
which includes code derived from [Xenia](https://xenia.jp), and its
third-party libraries keep their own licences; see
[packaging/licenses](packaging/licenses).

Aegis Wing, Xbox and Xbox 360 are trademarks or property of Microsoft. This is
an unofficial fan project, not affiliated with or endorsed by Microsoft.
