AEGIS WING XBLA RECOMP
======================
Version @VERSION@

This is a portable native Windows recompilation of the Xbox 360 release of
Aegis Wing, built with ReXGlue.

IMPORTANT
---------

This release does not contain the original game files. You must supply your
own legally obtained Xbox 360 Aegis Wing package (the Xbox LIVE Arcade game
file).

Supported title:
  Aegis Wing
  Xbox 360 Title ID: 5841083C

Requirements:
  Windows 10 or 11 (64-bit) and a DirectX 12 capable graphics card.
  Nothing else needs installing: the Microsoft Visual C++ runtime the game
  uses is included.

FIRST-TIME SETUP
----------------

1. Extract the release ZIP to a writable folder (not Program Files). You get
   one folder, "Aegis Wing".
   Tip: put your package file beside "Aegis Wing.exe", or in a folder called
   "Game Package" next to it, and it is found automatically.
2. Open "Aegis Wing.exe". The first time, the setup window opens.
3. If your package was found it is checked and imported straight away.
   Otherwise drag the package onto the window, or click "Browse package".
   Setup checks it first and tells you if it is the wrong file or an
   unsupported version; the import starts as soon as it is accepted, and you
   can cancel it at any time.
4. Tick any extras you want (all optional, all off unless ticked):
     Unlock Stage Select, Start in fullscreen, Create desktop shortcut.
5. Click "PLAY" once it says AEGIS WING IS READY.

From then on, opening "Aegis Wing.exe" starts the game straight away.
Setup only reads your package; it never changes, moves or deletes it, and it
is not needed again. Nothing is installed outside this folder, no
administrator rights are needed, and nothing is written to the registry. The
launcher does not download anything and does not contact Xbox LIVE.

CONTROLS
--------

An Xbox controller works as on the console. Keyboard and mouse are always on
as well:

  Arrow keys        Fly / move through menus (D-pad)
  Space             A (fire, confirm)
  Shift             B (back)
  Escape            Back in menus, pause during play
  Enter             Start
  X, Y              X and Y buttons
  Mouse             Point at menu items; left click is A, right click is B

Without a controller connected, the title screen says "Press Any Key": any
key or mouse click continues.

PLAYER NAME AND LAN GAMES
-------------------------

You get a random player name, shown in the lobby, during play and in LAN
games. To change it, open Help & Options, Settings and choose Player Name:
Left and Right (or A) pick another name from the list, and Enter or a mouse
click lets you type your own (up to 15 characters).

Join LAN Game and Create LAN Game, under Multiplayer, play over your local
network or a virtual LAN (Radmin VPN, ZeroTier, Tailscale, Hamachi and the
like). Windows asks once whether Aegis Wing may use the network the first
time you join or create a LAN game; allow it for LAN play. Single player and
local multiplayer never use the network.

PORTABLE DATA
-------------

Everything stays inside the extracted "Aegis Wing" folder:

  Aegis Wing.exe              Open this to play (and to set up the first time)
  README.txt, licenses\       This file and the third-party notices
  resources\
    runtime\                  The game program and its DLLs
      logs\                   Game and setup logs
    assets\                   Game data imported from your package
    userdata\                 Saves, high scores, caches and aegis_wing.toml
                              (display, audio and control settings)

Paths are relative, so the folder can be moved or copied anywhere (a USB
drive works too). To back up your progress, copy resources\userdata. Do not
rename the numbered folders inside it.

SETUP AGAIN, UPDATING AND OLDER RELEASES
----------------------------------------

To change your extras or import your package again, start Aegis Wing.exe with
--setup (for example from a shortcut whose target ends in
"Aegis Wing.exe" --setup). Importing again replaces resources\assets only;
saves and settings in resources\userdata are kept. If a needed file goes
missing, opening Aegis Wing.exe shows the setup window again by itself.

To update, extract a new release over this folder (your resources\assets and
resources\userdata are kept), or into a new folder: setup finds the old copy
next to it and imports from it.

Coming from an earlier release (Setup Aegis Wing.exe and a Game folder):
extract this release into the old release folder, inside it, or next to it.
Setup finds the old Game folder and offers it alongside any package, so you
do not need the package again, and your saves, high scores and settings come
with it. The old folder is never changed or deleted; remove it yourself once
you are happy. (An install from a release older than 0.9.0 has older menu
files: setup then asks for your package instead, and still carries your saves
over.)

TROUBLESHOOTING
---------------

Extract the ZIP first. Do not run Aegis Wing.exe from inside the ZIP.

Aegis Wing.exe is not digitally signed, so the first time you open it
Windows may show "Windows protected your PC". Choose More info, then Run
anyway.

If setup rejects your package, the reason is shown in the window and in
resources\runtime\logs\setup.log. When reporting a problem, include the
newest files from resources\runtime\logs.

LEGAL
-----

Aegis Wing, Xbox, Xbox 360 and related names and marks belong to their
respective owners. This is an unofficial fan project, not affiliated with or
endorsed by them. Third-party notices and license texts are in the licenses
folder.

After setup, resources\assets contains data from your own package. Do not
share the folder once you have played from it; share the release ZIP instead.

Aegis Wing.exe in the main folder (the launcher and setup window) includes a
package reader derived from Velocity and is licensed under the GNU GPL
version 3 (licenses\LICENSE-Extract-STFS.txt); its source is in the project
repository. The game in resources\runtime is a separate program under the
BSD 3-Clause licence of this project and ReXGlue (licenses folder).
