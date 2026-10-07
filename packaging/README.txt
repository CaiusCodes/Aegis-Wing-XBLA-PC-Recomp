AEGIS WING XBLA RECOMP
======================

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

1. Extract the release ZIP to a writable folder. You get one folder,
   "Aegis Wing XBLA Recomp", holding Setup, this README and licenses.
2. Run "Setup Aegis Wing.exe".
3. Choose your Aegis Wing package, or drag it onto the Setup window. Setup
   checks it straight away and tells you if it is the wrong file.
4. Choose Install game. Setup creates a Game folder beside itself, unpacks
   your package into it, applies the PC menu updates, and then offers a few
   optional extras (Unlock Stage Select, fullscreen, a desktop shortcut).
   None is switched on unless you tick it.
5. Choose Play now, or later run "Game\Aegis Wing.exe".

Setup only reads your package; it never changes it. It does not contact
Xbox Live or require an Xbox Live sign-in.

CONTROLS
--------

An Xbox controller works as on the console. Keyboard and mouse are always
on as well:

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

Setup gives you a random player name, shown in the lobby, during play and in
LAN games. To change it, open Help & Options, Settings and choose Player
Name: Left and Right (or A) pick another name from the list, and Enter or a
mouse click lets you type your own (up to 15 characters).

Join LAN Game and Create LAN Game, under Multiplayer, play over your local
network or a virtual LAN. Windows asks once whether Aegis Wing may use the
network the first time you join or create a LAN game; allow it for LAN play.
Single player and local multiplayer never use the network.

PORTABLE DATA
-------------

Everything stays inside the extracted "Aegis Wing XBLA Recomp" folder:

  Setup Aegis Wing.exe        Installs the game, or changes your extras later
  README.txt, licenses\       This file and the third-party notices
  Game\                       Created by Setup:
    Aegis Wing.exe            Starts the game
    assets\                   Game data unpacked from your package
    resources\                Setup's scripts and the default settings
    release-manifest.json     The files this release installed
    userdata\                 Saves, high scores and the local profile
    aegis_wing.toml           Display, audio and control settings
    logs\                     Game and Setup logs

To move or back up the installation, copy the entire folder. Do not rename
individual numbered folders inside Game\userdata.

UPDATING FROM AN EARLIER BUILD
------------------------------

Extract this release over the old folder and run Setup. Your saves and
settings are kept (and move into the Game folder if an older build kept them
in the folder root). Setup also tidies away what earlier releases kept in the
folder root: the old "Aegis Wing.exe" play launcher, the resources folder and
the Setup logs. It only removes files it recognises as its own; anything else
stays where it is.

REINSTALLING OR CHANGING EXTRAS
-------------------------------

Run "Setup Aegis Wing.exe" again at any time. Installing again replaces the
game files Setup installed and rebuilds Game\assets from your package, but
keeps your saves, high scores, settings and logs. To only change the extras,
choose Change extras.

CURRENT COMPATIBILITY
---------------------

Working:
  - Single-player, local multiplayer and LAN / virtual LAN multiplayer
  - Controller, keyboard and mouse input, audio, pause/resume and PC display
    settings
  - Portable saves, local high scores and configuration
  - Clean application exit

The Xbox Live-only menus, warnings, leaderboards and achievements UI are not
used by this PC build.

TROUBLESHOOTING
---------------

Extract the ZIP before running Setup. Do not run it from inside the ZIP.

Setup and the game are not digitally signed, so the first time you run Setup
Windows may show "Windows protected your PC". Choose More info, then Run
anyway. If a problem occurs, include the newest files from Game\logs when
reporting it. If Setup fails before the Game folder exists, its log is in the
"Aegis Wing Setup logs" folder inside your temporary folder (%TEMP%).

Third-party notices and license texts are available in the licenses folder.
