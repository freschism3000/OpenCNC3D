#!/bin/sh
# Write READ-ME-WINDOWS.txt, the notes a Windows player gets.
#
#   tools/win/installer/readme-windows.sh <out-file> <build-id>
#
# ONE TEXT, TWO CARRIERS. The Windows package carries these notes (make-build-win.sh
# writes them into the staged folder) and so does Setup, which installs them beside the
# launcher before the package has been downloaded, and offers them on its last page. Two
# copies of the text would say two different things the first time one was edited, so
# both callers run this.
#
# OPENCNC3D_COOKED set means a cooked build, whose binaries carry no F5 tuning panel, so
# the key list leaves F5 out (make-build-win.sh proves the binaries match).
set -e
OUTF="$1"
GITDESC="$2"
[ -n "$OUTF" ] && [ -n "$GITDESC" ] || {
    echo "usage: readme-windows.sh <out-file> <build-id>" >&2
    exit 1
}

# F5 is listed only where it does something.
KEY_F5="F5 the Visuals tuning panel, "
[ -z "$OPENCNC3D_COOKED" ] || KEY_F5=""

cat > "$OUTF" <<TXT
OpenCNC3D, Windows test build
build $GITDESC

WHAT TO DOUBLE CLICK
  OpenCNC3D.exe

That is the launcher for normal play. It tells you which
build you have, shows what changed in it, and offers the next one when there is
one. Setup installs the launcher without a game. Open Add Game to download Dawn3D
or RA3D, then select the installed module and press Play. Games download separately.
Dawn3D opens the original C&C menu; RA3D opens Red Alert.

The Editor button opens the map editor. It starts on the first GDI mission; MAP
inside it makes a new map or opens another. Maps you save go into the
missions\\user_maps folder beside the game, which is also where the game's own User
Maps list reads them from, so a map you make is playable without moving anything.

Enhanced Visuals are ON, with the 640x480 HUD. ESC in game opens Options; Visuals
has CLASSIC for the 1995 presentation and ADVANCED for the individual switches.
Your choices are remembered.

THE MOVIES AND THE MUSIC
  The game's download does not carry the 1995 movies or the music. Both are
  downloaded from the original discs: the movies from the 1995 Gold
  discs, which EA released as freeware in 2007, and the music from the Covert
  Operations disc image.

    * The Module Browser offers movies and music independently for each game.
    * The gear button adds them later, or removes them again.
    * Setup itself downloads no game, movies or music.

  Without them the game plays as normal, with no cutscenes and no soundtrack.

THE CONTROLS
  The mouse works the way the 1995 game's did, which as of v0.5.9 is also the way
  this build does. It is NOT the modern right-click-to-move scheme.

    LEFT click     Selects what you click on. With something already selected,
                   clicking bare ground MOVES there and clicking an enemy ATTACKS
                   it. Clicking another of your own units selects that unit
                   instead of ordering the first one to shoot it.
    LEFT drag      Rubber-band select. SHIFT adds to the selection.
    RIGHT click    Cancels: leaves building placement, leaves repair or sell mode,
                   and otherwise deselects. It never gives orders.
    CTRL + LEFT    Force fire, including on your own buildings and bare ground.
    ALT  + LEFT    Force move.

  The pointer is the game's own order preview, so the arrow, the move circle, the
  crosshair and the select brackets tell you which of these you will get.

    S or X stop, G or Z guard, M radar, SPACE pause, TAB step, C camera,
    ${KEY_F5}ESC the pause menu.

If you have an older build, delete any PLAY*.bat files in it. They are gone.
The other file beside the game, cnc_eyes.exe, is two things: the verification
binary the test suite drives, and the map editor the Editor button starts. There is
still no reason to run it by hand; press Editor instead.

WHAT THIS BUILD IS
  A 32-bit Windows build, cross compiled on the Mac from the same sources as the
  Mac build, commit $GITDESC. 32-bit is on purpose and not a limitation: it is
  what the engine's own pointer arithmetic assumes, and it is the architecture
  the eventual Windows 98 / Voodoo 2 tier needs. It runs on 64-bit Windows 11
  with nothing extra installed.

WINDOWS WILL WARN YOU THE FIRST TIME, AND HERE IS HOW TO GET PAST IT
  You will see a blue "Windows protected your PC" box, or the game will simply
  refuse to start. Nothing is wrong with the download. That box is Microsoft
  Defender SmartScreen and it appears because this build is not code signed. An
  unsigned program keeps getting it until enough people have downloaded that exact
  file for Microsoft to have formed an opinion of it, which for a test build that
  changes every few days does not happen.

  If you install with OpenCNC3D-Setup, the box appears for Setup itself: click
  "More info" and then "Run anyway".

  If you use the zip, the quickest way past it, and the one that clears every
  file in the download at once, is to unblock the ZIP BEFORE extracting it:

    1. Right click the .zip you downloaded and choose Properties.
    2. At the bottom of the General tab, tick Unblock, then press OK.
    3. Now extract it, and run OpenCNC3D.exe.

  If you have already extracted it, start the game, click "More info" on the
  warning and then "Run anyway". You are asked once per build.

  If an antivirus quarantines the game outright instead of warning you, that has
  the same cause and is not a detection of anything real. Say which antivirus it
  was when you report it, because that is worth knowing.

WHAT IS AND IS NOT VERIFIED
  The game boots, loads its data and runs a mission on Windows.
  None of the 31 gates has been run on Windows, so "it starts and looks right"
  is the standard met here, not the one this project normally uses.

PERFORMANCE REPORTS
  Run OpenCNC3D-Diagnostics.exe beside the game, reproduce the problem, and exit.
  Zip the newest Logs/session-... folder and attach it to your report. If the game
  folder cannot be written, find it in %LOCALAPPDATA%\OpenCNC3D\Logs instead.
  It uses your existing settings and saves, with unchanged graphics quality.
  Nothing is uploaded automatically. Inspect the folder before sharing.

IF SOMETHING IS WRONG
  Send opencnc3d-log.txt. It is written beside OpenCNC3D.exe every time the game starts, so
  run the game once, reproduce the problem, then send the file. The things worth
  checking in it first:
    * GL_RENDERER          a software or fallback GL is the usual cause of a
                           black 3D view while the sidebar still looks perfect
    * GL_MULTITEX = NO     the terrain pass needs two texture units
    * GL_GOT depth 0       with no depth buffer the depth test discards the world
    * LoadLibrary / 126    the brain DLL, or something it depends on, is missing
    * BOOTFAIL|            a mission that would not start says why on this line,
                           and the same sentence was shown on screen; send the
                           line as it is, it names the file or the step
    * FATAL: signal        the game died inside the step named after "while:";
                           that line is the whole point of sending the log
TXT
