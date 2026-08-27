#!/bin/sh
# Assemble a Windows test build that can be copied to the Windows host and double-click,
# and zip it.
#
#   tools/win/make-build-win.sh                  -> build/win-dist/ + ~/Desktop/<zip>
#   tools/win/make-build-win.sh --no-zip         -> folder only
#   CNC3D_PLAYABLE=/some/playable tools/win/make-build-win.sh
#
# This is the Windows half of game/make-build.sh. The game DATA is not rebuilt here: the
# packs, the missions and the DOS archives are identical on both platforms (they are
# extracted assets, not compiled output), so they are taken from the Mac test folder
# rather than baked twice and risking two different sets of bytes.
#
# The file list is an INCLUDE list, not an exclude list. playable/ accumulates gate
# output, proof screenshots (151 MB of them) and Mac-only launchers, and a zip built by
# subtraction would quietly grow every one of those the first time someone adds a new
# kind of artefact.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

SRC="${CNC3D_PLAYABLE:-$HOME/CNC3D/playable}"
STAGE="$ROOT/build/win-dist"
DIST_ZIP="${CNC3D_WIN_ZIP:-$HOME/Desktop}"
DO_ZIP=1
BINS_ONLY=0
[ "$1" = "--no-zip" ] && DO_ZIP=0
# --bins-only: the four files that change when code changes, and nothing else. The full
# package is 845 MB and takes minutes to copy and zip, and 99% of it is game data that is
# identical build to build. When the machine already has a folder on Windows, this is the
# thing to send him: unzip over the top and the data stays where it is.
[ "$1" = "--bins-only" ] && BINS_ONLY=1

[ -d "$SRC" ] || { echo "no data folder at $SRC. Run game/make-build.sh first." >&2; exit 1; }

GITDESC=$(git describe --always --dirty 2>/dev/null || echo untracked)
# tools/release.sh sets CNC3D_BUILD_ID to the number it is about to tag, because at the
# moment it builds, VERSION has been bumped and not yet committed, so git describe would
# name the artefact "-dirty" for what is about to become a clean release.
[ -n "$CNC3D_BUILD_ID" ] && GITDESC="$CNC3D_BUILD_ID"
# The folder inside the zip carries the build id, so that an unzipped copy on the Windows
# box still says which commit it came from. A zip that expands to "win-dist" tells whoever
# is looking at it nothing, and there will be more than one of these.
NAME="CNC3D-windows-$GITDESC"
OUT="$STAGE/$NAME"

echo "== compiling the Windows binaries"
tools/win/build-win.sh

# THE LAUNCHER, in the same breath as the game. Not optional and not a later step:
# it is what the player double-clicks, what the Start Menu shortcut points at, and
# what offers the next build. A package staged without it is a package whose one
# obvious file is missing.
echo "== compiling the Windows launcher"
tools/win/build-launcher-win.sh

echo "== assembling $OUT"
rm -rf "$STAGE"
mkdir -p "$OUT"

# THE GAME SHIPS AS ONE OBVIOUS FILE., looking at a folder of four
# PLAY-*.bat files: "remove the following, and just have a single executeable (both PC and
# Mac version), called C&C3D with a game Icon as well, so its clear that this is the
# version to run." The binary is named cnc3d.exe in the build tree because that is what
# the gates and the source list call it; it is named C&C3D.exe in the PACKAGE because that
# is what a player sees. It carries the icon (see the windres step in build-win.sh).
#
# cnc_eyes.exe still ships, and that is deliberate rather than an oversight: it is the
# verification binary the gate suite drives on Windows, and dropping it would make
# the Windows build notes impossible to follow. It is not a plausible thing to double-click,
# which is what the launchers were.
# C&C3D.exe IS THE LAUNCHER NOW, and cnc3d.exe is the engine under it. Until
# 24 Aug 2026 the engine itself was copied to C&C3D.exe, because it was the one
# thing to double-click. It still is; there is simply something in front of it
# that answers "which build is this, and is there a newer one" before it starts.
# The engine keeps its build-tree name in the package, which is also the name the
# launcher execs, so nothing has to be told where it is.
cp "$ROOT/build/win/C&C3D.exe"  "$OUT/C&C3D.exe"
cp "$ROOT/build/win/cnc3d.exe"  "$OUT/cnc3d.exe"
cp "$ROOT/build/win/cnc_eyes.exe" \
   "$ROOT/build/win/SDL2.dll"  "$ROOT/build/win/TiberianDawn.dll" "$OUT/"

# Named rather than globbed, and checked, because "the package looks fine and the
# game will not start" is the failure this catches.
for f in "C&C3D.exe" cnc3d.exe cnc_eyes.exe SDL2.dll TiberianDawn.dll; do
    [ -s "$OUT/$f" ] || { echo "$f did not reach the package" >&2; exit 1; }
done

# THE THING A PLAYER DOUBLE-CLICKS IS THE LAUNCHER. (Reported: "Make sure
# its the default executeable / app for Windows / Mac, from release v0.6.3 and
# going forward.") Three checks rather than one, because the interesting failure
# is not a missing file, it is the RIGHT NAME CARRYING THE WRONG BINARY:
#
#   1. C&C3D.exe is byte for byte what tools/win/build-launcher-win.sh produced.
#      Provenance, exactly.
#   2. It carries "/api/builds", which only the launcher does. Semantics, so a
#      cross-wired copy is caught even if it came from the right place.
#   3. C&C3D.exe and cnc3d.exe are DIFFERENT files. The two were swapped on
#      24 Aug, and copying one over both is the way to undo that without noticing:
#      the package still plays, and the launcher has silently gone.
#
# Until 24 Aug 2026 C&C3D.exe WAS the engine, so a stale build script or a
# reverted line puts the old arrangement back, and the only symptom is that the
# front door is missing from a package that otherwise works perfectly.
cmp -s "$ROOT/build/win/C&C3D.exe" "$OUT/C&C3D.exe" || {
    echo "C&C3D.exe in the package is not the launcher that was just built." >&2
    echo "That name is what a player double-clicks and what the installer's Start" >&2
    echo "Menu shortcut points at, so shipping the engine under it means shipping" >&2
    echo "no launcher at all." >&2
    exit 1
}
strings -a "$OUT/C&C3D.exe" 2>/dev/null | grep -q '/api/builds' || {
    echo "C&C3D.exe does not contain the site's build route, so whatever it is," >&2
    echo "it is not a launcher that can check for updates." >&2
    exit 1
}
cmp -s "$OUT/C&C3D.exe" "$OUT/cnc3d.exe" && {
    echo "C&C3D.exe and cnc3d.exe are the same file. One is the launcher and the" >&2
    echo "other is the game; a package where they are the same is missing one." >&2
    exit 1
}
echo "   C&C3D.exe is the launcher, cnc3d.exe is the game"

# ---- launchers: THERE ARE NONE ANY MORE -------------------------------------------
# Four .bat files used to be written here: PLAY, PLAY-HUD-MENU, PLAY-HUD-NEW and
# PLAY-HUD-OLD. They existed for exactly one reason, and it was a defect rather than a
# feature: `--dir` and `--content` defaulted to `../brain/missions/` and
# `../brain/stubcontent/`, paths that exist in the development tree and in no release, so
# the game could not be started without something passing them in. Those defaults now
# probe for the shipped layout first (see the dir/content defaults in cnc_eyes.cpp), the
# game runs with no arguments at all, and the launchers have nothing left to do.
#
# What they also did was force a HUD and a window size. The HUD is a checkbox in Visuals
# now and the window size is remembered, so those are the player's to set rather than a
# choice baked into which file they happened to double-click.
#
# The log the .bat files wrote is the one real loss, and it is a known gap:
# on Windows a double-clicked GUI program has nowhere to print, so the
# first bug reports were photographs of a console window. cnc3d.exe WRITES cnc3d-log.txt
# as of v0.6.0 (app/cnc3d.cpp redirects stdout and stderr into it at startup), which is
# what the READ-ME below has always told players to send. Before that it told them to
# send a file nothing created, so every Windows report arrived with no log at all
# today.

if [ $BINS_ONLY -eq 1 ]; then
    cat > "$OUT/READ-ME-FIRST.txt" <<TXT
Binaries only, build $GITDESC.

Copy these over the folder you already have, keeping the game data where it is:

  C&C3D.exe  cnc3d.exe  cnc_eyes.exe  SDL2.dll  TiberianDawn.dll

C&C3D.exe is the launcher and cnc3d.exe is the game. If you have the launcher
already you do not need to do any of this by hand: press Update in it.

Delete any PLAY*.bat you still have from an older build. They are gone, and they
pass options the game no longer needs.
TXT
else
    cat > "$OUT/READ-ME-WINDOWS.txt" <<TXT
C&C 3D, Windows test build
build $GITDESC

WHAT TO DOUBLE CLICK
  C&C3D.exe

That is the launcher, and there is nothing else to choose. It tells you which
build you have, shows what changed in it, and offers the next one when there is
one. Press Play and the game boots the 1995 DOS main menu: START NEW GAME runs
the GDI campaign, TEST MAP is the sandbox.

The Editor button is drawn grey on purpose. There is a map editor being built;
there is not yet one you can be handed.

Enhanced Visuals are ON, with the 640x480 HUD. ESC in game opens Options; Visuals
has CLASSIC for the 1995 presentation and ADVANCED for the individual switches.
Your choices are remembered.

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
    F5 the Visuals tuning panel, ESC the pause menu.

If you have an older build, delete any PLAY*.bat files in it. They are gone.
The other file beside the game, cnc_eyes.exe, is the verification binary the test
suite drives. It is not the game and there is no reason to run it by hand.

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

  The quickest way past it, and the one that clears every file in the download at
  once, is to unblock the ZIP BEFORE extracting it:

    1. Right click the .zip you downloaded and choose Properties.
    2. At the bottom of the General tab, tick Unblock, then press OK.
    3. Now extract it, and run C&C3D.exe.

  If you have already extracted it, start the game, click "More info" on the
  warning and then "Run anyway". You are asked once per build.

  If an antivirus quarantines the game outright instead of warning you, that has
  the same cause and is not a detection of anything real. Say which antivirus it
  was when you report it, because that is worth knowing.

WHAT IS AND IS NOT VERIFIED
  The game boots, loads its data, plays music and runs a mission on Windows.
  None of the 31 gates has been run on Windows, so "it starts and looks right"
  is the standard met here, not the one this project normally uses.

IF SOMETHING IS WRONG
  Send cnc3d-log.txt. It is written beside C&C3D.exe every time the game starts, so
  run the game once, reproduce the problem, then send the file. The things worth
  checking in it first:
    * GL_RENDERER          a software or fallback GL is the usual cause of a
                           black 3D view while the sidebar still looks perfect
    * GL_MULTITEX = NO     the terrain pass needs two texture units
    * GL_GOT depth 0       with no depth buffer the depth test discards the world
    * LoadLibrary / 126    the brain DLL, or something it depends on, is missing
TXT

    # The data. -L on dosdata because it is a symlink into the repo on the Mac side and
    # a symlink is worthless once this is unzipped on another machine.
    for f in "$SRC"/*.pack "$SRC"/CONQUER.MIX; do
        [ -f "$f" ] && cp "$f" "$OUT/"
    done
    for d in content missions movies; do
        [ -d "$SRC/$d" ] && rsync -aL "$SRC/$d" "$OUT/"
    done
    [ -e "$SRC/dosdata" ] && rsync -aL "$SRC/dosdata" "$OUT/"
fi

echo "$GITDESC  windows  $(date '+%Y-%m-%d %H:%M')" > "$OUT/BUILD-ID.txt"

# WHAT THE LAUNCHER READS ABOUT ITS OWN INSTALL. Two files, both written here so
# that they describe the folder as it is about to be zipped:
#
#   cnc3d-install.txt   the version, and the data fingerprint that decides whether
#                       the next update can be the 15 MB one or has to be 519 MB.
#   CHANGELOG.txt       so the launcher's panel has the right notes in it before
#                       any network call has been made, or has failed.
#
# The fingerprint comes from tools/launcher/data-id.sh, the same script
# tools/launcher/make-manifest.sh reads back out of this file when it writes the
# manifest. One implementation, quoted at both ends.
if [ $BINS_ONLY -eq 0 ]; then
    VERNUM=$(tr -d '[:space:]' < "$ROOT/VERSION")
    tools/launcher/make-changelog-txt.sh "$OUT/CHANGELOG.txt" >/dev/null
    {
        echo "# written by tools/win/make-build-win.sh"
        echo "version $VERNUM"
        echo "data_id $("$ROOT/tools/launcher/data-id.sh" "$OUT")"
    } > "$OUT/cnc3d-install.txt"
    echo "   install record: v$VERNUM  data $(awk '$1=="data_id"{print substr($2,1,12)}' "$OUT/cnc3d-install.txt")"
fi

SIZE=$(du -sh "$OUT" | cut -f1)
echo "   $OUT  ($SIZE)"

if [ $DO_ZIP -eq 1 ]; then
    [ $BINS_ONLY -eq 1 ] && SUF="-bins" || SUF=""
    ZIP="$DIST_ZIP/CNC3D-windows-$GITDESC$SUF.zip"
    echo "== zipping to $ZIP"
    rm -f "$ZIP"
    (cd "$STAGE" && zip -rqX "$ZIP" "$NAME" -x '*.DS_Store')
    echo
    echo "ready: $ZIP  ($(du -h "$ZIP" | cut -f1))"
else
    echo
    echo "ready: $OUT (not zipped)"
fi
