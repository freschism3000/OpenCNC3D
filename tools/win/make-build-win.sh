#!/bin/sh
# Assemble a Windows test build that can be copied to the Windows host and double-click,
# and zip it.
#
#   tools/win/make-build-win.sh                  -> build/win-dist/ + ~/Desktop/<zip>
#   tools/win/make-build-win.sh --no-zip         -> folder only
#   tools/win/make-build-win.sh --bins-only      -> the binary-only update, flat
#   OPENCNC3D_PLAYABLE=/some/playable tools/win/make-build-win.sh
#   OPENCNC3D_NO_RA=1 tools/win/make-build-win.sh   -> without the Red Alert 3D module
#
# This is the Windows half of game/make-build.sh. The game DATA is not rebuilt here: the
# packs, the missions and the DOS archives are identical on both platforms (they are
# extracted assets, not compiled output), so they are taken from the Mac test folder
# rather than baked twice and risking two different sets of bytes.
#
# The file list is an INCLUDE list, not an exclude list. playable/ accumulates gate
# output, proof screenshots (151 MB of them) and Mac-only launchers, and a zip built by
# subtraction would quietly grow every one of those the first time someone adds a new
# kind of artefact. Runtime diagnostic Logs/ is never an input; the final check
# below also refuses it if a future staging step introduces that personal folder.
#
# THE MOVIES AND THE MUSIC ARE NOT IN IT. A player's install fetches them from disc images
# on archive.org instead (the movies from the 1995 Gold discs, EA's 2007 freeware release;
# the music from the Covert Operations disc image): Setup downloads them while it installs,
# the launcher asks on its first start in a folder unzipped from this package, and its
# gear button adds or removes them later. The game needs no switch for it, because a
# missing movie is skipped and a missing score is silence. The dosdata copy is the one
# place the include list takes a whole folder, and it is the folder the media live in;
# the MEDIA list below is what keeps them out of that copy, and the check near the end
# refuses a package that holds any of them, however they arrived.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

SRC="${OPENCNC3D_PLAYABLE:-$HOME/CNC3D/playable}"
STAGE="$ROOT/build/win-dist"
DIST_ZIP="${OPENCNC3D_WIN_ZIP:-$HOME/Desktop}"
DO_ZIP=1
BINS_ONLY=0
[ "$1" = "--no-zip" ] && DO_ZIP=0
# --bins-only: the binaries and small top-level packs that change with code. The full
# package is 845 MB and takes minutes to copy and zip, and 99% of it is game data that is
# identical build to build. When the machine already has a folder on Windows, this is the
# thing to send him: unzip over the top and the data stays where it is.
[ "$1" = "--bins-only" ] && BINS_ONLY=1

[ -d "$SRC" ] || { echo "no data folder at $SRC. Run game/make-build.sh first." >&2; exit 1; }

GITDESC=$(git describe --always --dirty 2>/dev/null || echo untracked)
# tools/release.sh sets OPENCNC3D_BUILD_ID to the number it is about to tag, because at the
# moment it builds, VERSION has been bumped and not yet committed, so git describe would
# name the artefact "-dirty" for what is about to become a clean release.
[ -n "$OPENCNC3D_BUILD_ID" ] && GITDESC="$OPENCNC3D_BUILD_ID"
# The folder inside the zip carries the build id, so that an unzipped copy on Windows
# still says which commit it came from. A zip that expands to "win-dist" tells whoever
# is looking at it nothing, and there will be more than one of these.
NAME="OpenCNC3D-windows-$GITDESC"
OUT="$STAGE/$NAME"

# OPENCNC3D_NO_RA=1 LEAVES THE RED ALERT 3D MODULE OUT: no ra3d/ folder, so no copy of the
# engine for it, no RedAlert.dll and none of its packs, missions, movies or audio. The
# launcher is the same program either way. It offers the modules it finds in its folder,
# so without ra3d/ its Red Alert slot is drawn as an empty one and the game it starts is
# the only module there is. tools/release.sh sets it for a release cut without the
# module, and the Mac packager reads the same variable, so both halves of one release
# carry the same modules. Unset or 0 stages the module whenever the data folder has it,
# without its movies and score: tools/ra3d/stage-runtime.py leaves out the media list's
# paths under ra3d/, and the media check near the end still asks.
NO_RA=0
case "${OPENCNC3D_NO_RA:-0}" in 0) ;; *) NO_RA=1 ;; esac

# THE MEDIA PATHS, relative to the package root: a trailing slash names a folder.
# tools/media/media-paths.txt is the list both packagers read (one path per line, '#' for
# a comment, the leading slash optional, read the same way by every reader of the list).
# The paths below are a FLOOR under that file, not a second copy of it: they stay out, and
# are refused, even if a line goes missing from the file, and every path the file adds
# stays out on top of them. Red Alert's movies and score are among them: they may not ship
# either, so the Red Alert module is staged without them.
MEDIA_FLOOR="dosdata/movies/
dosdata/music/
dosdata/SCORES.MIX
dosdata/TRANSIT.MIX
dosdata/transit/TRANSIT.MIX
movies/
ra3d/movies/
ra3d/audio/SCORES.MIX
ra3d/data/SCORES.MIX"
MEDIA_FILE="$ROOT/tools/media/media-paths.txt"
MEDIA_PATHS=$( {
    printf '%s\n' "$MEDIA_FLOOR"
    [ ! -f "$MEDIA_FILE" ] || sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
                                  -e 's|^\./||' -e 's|^/||' "$MEDIA_FILE"
} | grep -v '^$' | sort -u )
if [ -f "$MEDIA_FILE" ]; then
    MEDIA_FROM="tools/media/media-paths.txt and the floor"
else
    MEDIA_FROM="the floor alone, since tools/media/media-paths.txt is not in this tree"
fi

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
printf '%s\n' 'OpenCNC3D fresh public package v1' > "$OUT/.opencnc3d-public-stage"

# THE PACKAGE HAS ONE OBVIOUS FILE TO RUN, and it is OpenCNC3D.exe, the launcher. It
# carries the game icon (the windres step in build-launcher-win.sh), shows which build
# this is, offers the next one, and starts the game. The game itself is
# opencnc3d-game.exe, under the same name it has in the build tree, which is also the
# name the launcher execs, so nothing has to be told where it is.
#
# THE TWO NAMES MUST DIFFER WHEN LETTER CASE IS IGNORED, because Windows ignores it. A
# launcher and an engine whose names differ only in case are one file on Windows, and on
# the case-insensitive file system this package is staged on: the second copy below
# would replace the first in silence. So every name this script places by hand is
# checked first, before anything is copied, and the staged folder is checked again at
# the end.
#
# cnc_eyes.exe still ships, and it has TWO jobs. It is the verification binary the gate
# suite drives on Windows, and it is also the MAP EDITOR: the editor is a flag on the
# renderer (--edit) and not a program of its own, and opencnc3d-game.exe's entry point
# never reads that flag. So the launcher's EDITOR button runs this file. It is still not
# a plausible thing to double-click; the button in front of it is.
PLACED="OpenCNC3D.exe opencnc3d-game.exe OpenCNC3D-Diagnostics.exe cnc_eyes.exe netcheck.exe SDL2.dll TiberianDawn.dll"
PLACED_DUP=$(printf '%s\n' $PLACED | tr '[:upper:]' '[:lower:]' | sort | uniq -d)
[ -z "$PLACED_DUP" ] || {
    echo "two files this package places share a name once letter case is ignored:" >&2
    echo "$PLACED_DUP" | sed 's/^/  /' >&2
    echo "Windows treats them as one file. Rename one of them in the build scripts." >&2
    exit 1
}
cp "$ROOT/build/win/OpenCNC3D.exe"  "$OUT/OpenCNC3D.exe"
cp "$ROOT/build/win/opencnc3d-game.exe"  "$OUT/opencnc3d-game.exe"
cp "$ROOT/build/win/OpenCNC3D-Diagnostics.exe" "$OUT/OpenCNC3D-Diagnostics.exe"
cp "$ROOT/build/win/cnc_eyes.exe" \
   "$ROOT/build/win/SDL2.dll"  "$ROOT/build/win/TiberianDawn.dll" "$OUT/"
# Red Alert uses the same renderer build, its own brain and its own data, all inside
# ra3d/, staged without its movies and score. Only the full package carries it: the
# binary-only update is flat by contract (see the zip step at the end), so it has no
# subfolder to put a module in.
if [ $NO_RA -eq 1 ]; then
    echo "   red alert: left out (OPENCNC3D_NO_RA=1)"
elif [ $BINS_ONLY -eq 1 ]; then
    [ ! -d "$SRC/ra3d" ] || echo "   red alert: not in the binary-only update; the full package carries ra3d/"
elif [ -d "$SRC/ra3d" ]; then
    python3 "$ROOT/tools/ra3d/stage-runtime.py" --source "$SRC/ra3d" \
        --out "$OUT/ra3d" --app "$ROOT/build/win/opencnc3d-game.exe" \
        --brain "$ROOT/build/win/RedAlert.dll"
fi

# THE CONNECTIVITY TOOL ships with the game, and it is the reason this line exists rather
# than being a build-tree convenience. netcheck.exe has no game in it: it opens one UDP
# socket, runs three hundred lockstep turns against the other end and prints a digest of
# the orders both sides executed. Identical digests mean the two ends can lockstep;
# different ones mean they cannot, and it says so before the match is blamed.
#
# The person who needs it is the person whose game will not start, and that person has a
# zip rather than a compiler. It is 40 KB, it is built unconditionally by build-win.sh,
# and it links nothing beyond ws2_32, which is part of Windows.
cp "$ROOT/build/win/netcheck.exe" "$OUT/"

# Named rather than globbed, and checked, because "the package looks fine and the
# game will not start" is the failure this catches.
for f in $PLACED; do
    [ -s "$OUT/$f" ] || { echo "$f did not reach the package" >&2; exit 1; }
done

# Automatic reports use a separate entry of the same optimized renderer. Validate
# the entry marker and matching provenance without requiring a diagnostic +SHA mark.
set --
[ -z "$OPENCNC3D_COOKED" ] || set -- "$@" --cooked
[ -z "$OPENCNC3D_BUILD_ID" ] || set -- "$@" --expected-build "$OPENCNC3D_BUILD_ID"
python3 "$ROOT/tools/win/check-diagnostics.py" "$OUT" "$@"

# ---- the editor ships, and the button in front of it is wired to it ----------------
# The launcher's EDITOR button was drawn grey for as long as the editor existed in the
# tree and not in a package. Turning it on is one line of C; keeping it honest is this
# block, and the two are one change. An enabled button over a package with no editor
# behind it fails in front of the player at the moment it is pressed, which is worse
# than a grey button and is exactly what the grey button was avoiding.
#
# CHECKED BY CONTENT, NOT BY NAME. cnc_eyes.exe is copied a few lines above, so its
# presence says nothing about whether the editor was compiled into it: a build with the
# editor sources dropped still produces a file of that name. The string below is an
# editor-only diagnostic, and it discriminates -- measured on the cross compiled .exe,
# it appears in cnc_eyes.exe and not at all in the launcher.
# A COOKED PACKAGE CARRIES NO F5 PANEL, and this is what stops the fail-open case: the
# packager asked for a cook (OPENCNC3D_COOKED set, as tools/release.sh does) but the binaries
# were built without the define. The cooked fxp_toggle prints one sentence and nothing
# else; a binary that does not carry it was not cooked. Checked on all three game entries.
if [ -n "$OPENCNC3D_COOKED" ]; then
  for b in cnc_eyes.exe opencnc3d-game.exe OpenCNC3D-Diagnostics.exe; do
    strings -a "$OUT/$b" 2>/dev/null | grep -q 'unavailable in a cooked build' || {
      echo "$b was packaged as a cooked build but carries the F5 panel: it was compiled" >&2
      echo "without OPENCNC3D_COOKED. Rebuild with the packager, not by hand." >&2
      exit 1; }
  done
  echo "cooked: game, editor and diagnostic entry carry no F5 panel"
fi
strings -a "$OUT/cnc_eyes.exe" 2>/dev/null | grep -q 'edit: world grid' || {
    echo "cnc_eyes.exe carries no editor: the editor-only diagnostic string is not in" >&2
    echo "it. The launcher's EDITOR button runs this binary with --edit, so a package" >&2
    echo "built from it would open the renderer in a mission instead of the editor." >&2
    exit 1
}
# AND THE BUTTON KNOWS HOW TO REACH IT. The launcher names the editor's executable and
# its flag as literals, so both are in the binary if, and only if, it was built with the
# path that opens the editor. A launcher built before this feature has NEITHER string,
# which is what makes this a test rather than a formality. It proves the path was
# COMPILED IN and not that the button is drawn enabled; nothing readable out of a cross
# compiled .exe can prove the second, and that half is checked on the Mac, where the
# launcher can actually be run.
for f in cnc_eyes --edit; do
    strings -a "$OUT/OpenCNC3D.exe" 2>/dev/null | grep -q -- "$f" || {
        echo "OpenCNC3D.exe does not contain '$f', so it was built without the path that" >&2
        echo "opens the editor and its EDITOR button would do nothing." >&2
        exit 1
    }
done
echo "   editor: cnc_eyes.exe carries it, OpenCNC3D.exe knows how to open it"

# Double-clicking starts the launcher. Three checks distinguish a missing file
# from an executable with the expected filename but the wrong contents:
#
#   1. OpenCNC3D.exe is byte for byte what tools/win/build-launcher-win.sh produced.
#      Provenance, exactly.
#   2. It carries "/api/builds", which only the launcher does. Semantics, so a
#      cross-wired copy is caught even if it came from the right place.
#   3. OpenCNC3D.exe and opencnc3d-game.exe are DIFFERENT files. The two were swapped on
#      24 Aug, and copying one over both is the way to undo that without noticing:
#      the package still plays, and the launcher has silently gone.
#
# Until 24 Aug 2026 OpenCNC3D.exe WAS the engine, so a stale build script or a
# reverted line puts the old arrangement back, and the only symptom is that the
# front door is missing from a package that otherwise works perfectly.
cmp -s "$ROOT/build/win/OpenCNC3D.exe" "$OUT/OpenCNC3D.exe" || {
    echo "OpenCNC3D.exe in the package is not the launcher that was just built." >&2
    echo "That name is what a player double-clicks and what the installer's Start" >&2
    echo "Menu shortcut points at, so shipping the engine under it means shipping" >&2
    echo "no launcher at all." >&2
    exit 1
}
strings -a "$OUT/OpenCNC3D.exe" 2>/dev/null | grep -q '/api/builds' || {
    echo "OpenCNC3D.exe does not contain the site's build route, so whatever it is," >&2
    echo "it is not a launcher that can check for updates." >&2
    exit 1
}
cmp -s "$OUT/OpenCNC3D.exe" "$OUT/opencnc3d-game.exe" && {
    echo "OpenCNC3D.exe and opencnc3d-game.exe are the same file. One is the launcher and the" >&2
    echo "other is the game; a package where they are the same is missing one." >&2
    exit 1
}
echo "   OpenCNC3D.exe is the launcher, opencnc3d-game.exe is the game"

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
# first bug reports were photographs of a console window. opencnc3d-game.exe WRITES opencnc3d-log.txt
# as of v0.6.0 (app/opencnc3d.cpp redirects stdout and stderr into it at startup), which is
# what the READ-ME below has always told players to send. Before that it told them to
# send a file nothing created, so every Windows report arrived with no log at all
# today.

# Rebuild the small required decal pack from authored finals for both archive forms.
# Source gameplay folders may be older installations without it or with stale art.
python3 "$ROOT/tools/art/remaster/decals/build-runtime.py" --pack "$OUT/decalenh.pack" >/dev/null
[ -s "$OUT/decalenh.pack" ] || { echo "Enhanced decal pack is missing or empty" >&2; exit 1; }

if [ $BINS_ONLY -eq 1 ]; then
    # DOS Classic's two top-level files ride with the binaries, as they do in the macOS
    # binary-only update. Its theater archives (content/TEMPERAT.MIX, content/DESERT.MIX)
    # do not: they live in a subfolder and this zip is flat, and they are game data that
    # the full package carries and the data fingerprint counts, so a folder without them
    # is never offered this update in the first place.
    [ -f "$SRC/dosclassic.pack" ] || { echo "dosclassic.pack is required for DOS Classic" >&2; exit 1; }
    cp "$SRC/dosclassic.pack" "$SRC/CONQUER.MIX" "$OUT/"
    # The menu pack too: a repainted title screen keeps its size, and the data
    # fingerprint counts sizes, so this update is the only way it arrives.
    [ -f "$SRC/dosmenu.pack" ] || { echo "dosmenu.pack is required for the menus" >&2; exit 1; }
    cp "$SRC/dosmenu.pack" "$OUT/"
    cp "$SRC/enhanced-menu.pack" "$OUT/"
    cat > "$OUT/READ-ME-FIRST.txt" <<TXT
Binaries only, build $GITDESC.

Copy these over the folder you already have, keeping the game data where it is:

  OpenCNC3D.exe  opencnc3d-game.exe  OpenCNC3D-Diagnostics.exe
  cnc_eyes.exe  netcheck.exe  SDL2.dll  TiberianDawn.dll
  dosclassic.pack  dosmenu.pack  decalenh.pack  CONQUER.MIX

OpenCNC3D.exe is the launcher and opencnc3d-game.exe is the game. If you have the launcher
already you do not need to do any of this by hand: press Update in it.

For a performance report, run OpenCNC3D-Diagnostics.exe, reproduce the problem,
and exit. Zip the newest Logs/session-... folder and attach it to your report.
If that folder cannot be written beside the game, find it in
%LOCALAPPDATA%\OpenCNC3D\Logs. Settings, saves and graphics quality are unchanged;
nothing is uploaded automatically. Inspect the folder before sharing.

Delete any PLAY*.bat you still have from an older build. They are gone, and they
pass options the game no longer needs.
TXT
else
    # The player's notes, from the one writer Setup also runs, so the package and the
    # installer cannot say two different things. A cooked package has no tuning panel
    # behind F5 (the check near the top of this script proves it), so the writer leaves
    # the key out when OPENCNC3D_COOKED is set.
    OPENCNC3D_COOKED="$OPENCNC3D_COOKED" sh "$ROOT/tools/win/installer/readme-windows.sh" \
        "$OUT/READ-ME-WINDOWS.txt" "$GITDESC"

    # THE LICENCE AND THE NOTICE, and they are not optional. This program is a single
    # work built on EA's GPL source, so a binary package carrying neither the licence nor
    # the notice of modification is not a compliant distribution. Every release up to and
    # including v0.6.4 shipped without them, which is a defect in the builds already in
    # players' hands and not only in the next one. Copied from the TRACKED masters at the
    # repo root, never from the play folder, for the same reason the READ-ME is written
    # fresh rather than copied: an untracked copy goes stale in silence.
    for f in LICENSE; do
        [ -f "$ROOT/$f" ] || { echo "$f is missing from the repo root; a GPL binary package has to carry it" >&2; exit 1; }
        cp "$ROOT/$f" "$OUT/"
    done
    python3 "$ROOT/tools/make-package-notice.py" "$OUT/NOTICE.md" \
        --package-root "$OUT" --no-ra "$NO_RA"

    # The data. -L on dosdata because it is a symlink into the repo on the Mac side and
    # a symlink is worthless once this is unzipped on another machine.
    [ -f "$SRC/dosclassic.pack" ] || { echo "dosclassic.pack is required for DOS Classic" >&2; exit 1; }
    [ -f "$SRC/CONQUER.MIX" ] || { echo "CONQUER.MIX is required for DOS Classic" >&2; exit 1; }
    for f in "$SRC"/*.pack "$SRC"/CONQUER.MIX; do
        [ "${f##*/}" != decalenh.pack ] || continue
        if [ $NO_RA -eq 1 ]; then
            case "${f##*/}" in waterbase_RA_TEMPERAT.pack) ;; *_RA_*) continue ;; esac
        fi
        [ -f "$f" ] && cp "$f" "$OUT/"
    done
    # THE ROCKS' THEATER PACKS, from the same script the Mac build runs rather than from
    # the play folder: whatever rock3d_*.pack the folder holds may be an earlier build's,
    # and the flag in game/make-build.sh is what this package must carry.
    sh "$ROOT/tools/art/rocks/stones.sh" "$OUT"
    [ ! -f "$SRC/temperate_grass_rgb.bin" ] || cp "$SRC/temperate_grass_rgb.bin" "$OUT/"
    # user_maps is excluded for the reason the mac packager gives at need_dir: it is a
    # personal folder the editor and the gates both write into, not content. The worked
    # examples are put back by name below. A top-level movies/ folder is not copied: it
    # is an old duplicate of two of the movies that no code reads, and movies are not
    # packaged any more in any case.
    for d in content missions; do
        [ -d "$SRC/$d" ] && rsync -aL --exclude 'user_maps/' "$SRC/$d" "$OUT/"
    done
    for theater in TEMPERAT DESERT; do
        [ -s "$OUT/content/$theater.MIX" ] || { echo "content/$theater.MIX is required for DOS Classic" >&2; exit 1; }
    done
    # THE DOS ARCHIVES, WITHOUT THE MEDIA. Every path of the MEDIA list that lies inside
    # dosdata/ is excluded from the copy, anchored at the package root the way the list is
    # written. The archives that remain are the sound effects, the EVA lines, the fonts,
    # the palette and the 2D art, and without them the game is not quiet but broken, so
    # the folder is required now rather than copied when present; SOUNDS.MIX and SPEECH.MIX
    # are what proves sound arrived, now that a silent score is intended. A folder the
    # exclusions leave empty (transit/, which holds only TRANSIT.MIX) is not shipped.
    [ -e "$SRC/dosdata" ] || { echo "no dosdata in $SRC: the package would have no sound and no 2D art" >&2; exit 1; }
    set --
    while IFS= read -r p; do
        case "$p" in dosdata/*) set -- "$@" --exclude "/$p" ;; esac
    done <<MEDIA
$MEDIA_PATHS
MEDIA
    rsync -aL "$@" "$SRC/dosdata" "$OUT/"
    find "$OUT/dosdata" -mindepth 1 -type d -empty -delete
    for f in SOUNDS.MIX SPEECH.MIX; do
        [ -s "$OUT/dosdata/$f" ] || {
            echo "dosdata/$f did not reach the package. The movies and music are meant to" >&2
            echo "be missing; the sound effects and the EVA lines are not." >&2
            exit 1
        }
    done
    echo "   dosdata: copied without the movies and music ($MEDIA_FROM)"
    # The NEW AUDIO replacement sounds, beside dosdata where the audio start-up looks.
    [ -e "$SRC/cinematic" ] && rsync -aL --exclude '.DS_Store' "$SRC/cinematic" "$OUT/"
    [ -e "$SRC/foley" ] && rsync -aL --exclude '.DS_Store' "$SRC/foley" "$OUT/"
    [ -e "$SRC/ambience" ] && rsync -aL --exclude '.DS_Store' "$SRC/ambience" "$OUT/"
    [ -e "$SRC/newaudio" ] && rsync -aL --exclude '.DS_Store' "$SRC/newaudio" "$OUT/"
    # The authored vehicle models, which are data like the packs and not a personal
    # folder like user_maps. Copied whole: the renderer finds a model by name inside
    # this directory and nothing registers one, so a file left behind here is a vehicle
    # that silently keeps its cartridge model on Windows and not on the Mac.
    [ -d "$SRC/models3d" ] && rsync -aL "$SRC/models3d" "$OUT/"
    # The Unity explosion's pack, for the same reason and in the same way.
    [ -d "$SRC/vfx3d" ] && rsync -aL "$SRC/vfx3d" "$OUT/"
    # The fixed cartridge buildings (Enhanced only), taken from the TREE rather than from
    # the play folder: they are tracked files that no step of the Mac build changes, so
    # the tree is the one copy that cannot be stale. Only the .n64x files; the manifest
    # beside them is provenance for the repository, not data for the game.
    if [ -d "$ROOT/game/n64fix" ]; then
        mkdir -p "$OUT/n64fix"
        cp "$ROOT"/game/n64fix/*.n64x "$OUT/n64fix/"
    fi
    # The Enhanced buildings (ebld/<TYPE>.ebld, Enhanced only), from the TREE for the
    # same reason: tracked files that no step of the Mac build changes. A folder that is
    # there with no .ebld in it stops the build here (the glob stays literal and cp
    # fails), rather than shipping a Windows zip whose Power Plant silently differs from
    # the Mac one.
    if [ -d "$ROOT/game/ebld" ]; then
        mkdir -p "$OUT/ebld"
        cp "$ROOT"/game/ebld/*.ebld "$OUT/ebld/"
        for m in "$ROOT"/game/ebld/*.ebld.fclv "$ROOT"/game/ebld/*.spec; do
            [ -f "$m" ] && cp "$m" "$OUT/ebld/"
        done
    fi
    python3 "$ROOT/tools/art/terrain/stage_desert_details.py" "$OUT"
    if [ $NO_RA -eq 1 ]; then
        python3 "$ROOT/tools/art/rocks/stage_stone_materials.py" "$OUT" \
            --banks TEMPERAT DESERT WINTER SNOW SAND
    else
        python3 "$ROOT/tools/art/rocks/stage_stone_materials.py" "$OUT"
    fi

    # The placements (eplc/<TYPE>.eplc, Enhanced only), from the TREE for the same reason;
    # an empty folder stops the build the same way.
    if [ -d "$ROOT/game/eplc" ]; then
        mkdir -p "$OUT/eplc"
        cp "$ROOT"/game/eplc/*.eplc "$OUT/eplc/"
    fi
    # The remastered vehicles and aircraft (evhc/<TYPE>.evhc, Enhanced only), from the TREE
    # for the same reason; an empty folder stops the build the same way.
    if [ -d "$ROOT/game/evhc" ]; then
        mkdir -p "$OUT/evhc"
        cp "$ROOT"/game/evhc/*.evhc "$OUT/evhc/"
        # (a type's specular maps, <TYPE>.spec, where its export has them)
        for f in "$ROOT"/game/evhc/*.spec; do [ -f "$f" ] && cp "$f" "$OUT/evhc/"; done
    fi
    # The remastered soldiers (einf/<TYPE>.c3m, Enhanced only), from the TREE for the same
    # reason; an empty folder stops the build the same way.
    if [ -d "$ROOT/game/einf" ]; then
        mkdir -p "$OUT/einf"
        cp "$ROOT"/game/einf/*.c3m "$OUT/einf/"
    fi
    # The remastered cursors (ecur/ecur.bin, Enhanced only), from the TREE for the same
    # reason. Missing from the tree it stops the build, rather than shipping a Windows zip
    # whose cursors silently differ from the Mac one.
    mkdir -p "$OUT/ecur"
    cp "$ROOT/game/ecur/ecur.bin" "$OUT/ecur/"

    # NO USER MAP SHIPS, the tracked examples included, as in the Mac package: user_maps
    # is excluded wholesale above and carried EMPTY, because the game and the editor save
    # into it. game/examples/ still tracks the worked examples for developer play folders.
    _ex="$OUT/missions/user_maps"
    mkdir -p "$_ex"
    _um=$(cd "$_ex" && find . -type f | sed 's|^\./||' | head -8)
    [ -z "$_um" ] || { echo "the package's user_maps folder is not empty, and no user map ships:" >&2
        echo "$_um" | sed 's/^/  /' >&2; exit 1; }

    # THE SAME PAIR CHECK THE MAC PACKAGER MAKES, on the files that actually landed here.
    # The two copies above are a glob and an rsync and neither can fail loudly, so a
    # source folder holding the SCM mission INIs and no SCM packs produces a Windows
    # package with a Skirmish button and an empty lobby behind it. A map counts only
    # where the game counts it: skirmish_scan in app/opencnc3d.cpp wants BOTH halves. At
    # least one COMPLETE map is the rule, not one pack per INI, because an INI can be
    # staged without a pack.
    skpairs=0
    for ini in "$OUT"/missions/SCM*.INI; do
        [ -f "$ini" ] || continue
        code=$(basename "$ini" .INI)
        if [ -f "$OUT/$code.pack" ]; then skpairs=$((skpairs+1)); fi
    done
    if [ "$skpairs" -eq 0 ]; then
        echo "no playable skirmish map reached $OUT. A map needs BOTH" >&2
        echo "missions/<CODE>.INI and <CODE>.pack. Check $SRC first, and run" >&2
        echo "sh tools/stage-skirmish-maps.sh if it has none either." >&2
        exit 1
    fi
    echo "   skirmish: $skpairs map(s) with an INI and a pack"

    # WHAT THE EDITOR NEEDS ON DISK, and it is a short list because most of the editor
    # is not on disk at all: its font, its emblem and its legality tables are baked into
    # the binary as C headers, so there is no editor data folder to forget.
    #
    # MEASURED BY REMOVAL rather than guessed. Run in a package shaped folder, the
    # editor opens with no cameos.pack, no dosinfantry.pack and no CONQUER.MIX, and
    # refuses to open without any one of these. content/ is the sharpest of them: its
    # absence is not a message but a segmentation fault inside the engine's own scenario
    # start, so a package missing it looks like a crashing editor rather than a package
    # with a hole in it.
    for f in dossidebar.pack content missions; do
        [ -e "$OUT/$f" ] || {
            echo "$f is not in $OUT, and the editor cannot open without it." >&2
            echo "The launcher's EDITOR button opens the editor from this folder, so a" >&2
            echo "package missing this ships a button that fails when it is pressed." >&2
            exit 1
        }
    done

    # FIVE MAPS, NOT ONE, and none of them is an example. SCG01EA is what the launcher
    # passes to --scen; the other four are the editor's donors, one per theater. A map
    # made in the editor has no baked terrain of its own and borrows the pack of the
    # donor for ITS theater, so shipping four of the five means MAP > New Map opens a
    # blank map in whichever theater was dropped with nothing on screen to explain it. A
    # map needs BOTH halves, for the same reason the skirmish check above says so.
    for f in SCG01EA SCB01EA SCW01EA SCS01EA SCA01EA; do
        if [ ! -f "$OUT/$f.pack" ] || [ ! -f "$OUT/missions/$f.INI" ]; then
            echo "the editor's $f donor is incomplete: it needs BOTH $f.pack and" >&2
            echo "missions/$f.INI, and every map made in that theater borrows it." >&2
            exit 1
        fi
    done
    echo "   editor: five theater donors complete, and the data it reads is here"
fi

echo "$GITDESC  windows  $(date '+%Y-%m-%d %H:%M')" > "$OUT/BUILD-ID.txt"

# WHAT THE LAUNCHER READS ABOUT ITS OWN INSTALL. Two files, both written here so
# that they describe the folder as it is about to be zipped:
#
#   opencnc3d-install.txt   the version, and the data fingerprint that decides whether
#                       the next update can be the 15 MB one or has to be 519 MB.
#   CHANGELOG.txt       so the launcher's panel has the right notes in it before
#                       any network call has been made, or has failed.
#
# The fingerprint comes from tools/launcher/data-id.sh, the same script
# tools/launcher/make-manifest.sh reads back out of this file when it writes the
# manifest. One implementation, quoted at both ends.
if [ -n "$OPENCNC3D_COOKED" ]; then
    python3 "$ROOT/tools/release/strip-windows-debug.py" prepare --stage "$OUT" \
        --reference-root "$ROOT/build/win" --marker "$STAGE/public-debug-strip-marker.json" \
        --receipt "$STAGE/public-debug-strip-receipt.json"
    python3 "$ROOT/tools/release/strip-windows-debug.py" apply --marker "$STAGE/public-debug-strip-marker.json"
    python3 "$ROOT/tools/launcher/prepare-public-runtime.py" "$OUT"
else
    # Developer test folders retain their explicit donor and provenance copies.
    rm "$OUT/.opencnc3d-public-stage"
fi
if [ $BINS_ONLY -eq 0 ]; then
    VERNUM=$(tr -d '[:space:]' < "$ROOT/VERSION")
    tools/launcher/make-changelog-txt.sh "$OUT/CHANGELOG.txt" >/dev/null
    {
        echo "# written by tools/win/make-build-win.sh"
        echo "version $VERNUM"
        echo "data_id $("$ROOT/tools/launcher/data-id.sh" "$OUT")"
    } > "$OUT/opencnc3d-install.txt"
    echo "   install record: v$VERNUM  data $(awk '$1=="data_id"{print substr($2,1,12)}' "$OUT/opencnc3d-install.txt")"
fi

# ---- what the staged folder must and must not hold --------------------------------
# NO MOVIES AND NO MUSIC, asked of the result rather than trusted to the exclusions,
# so a movie or a score that reaches the package by any other road stops the build:
#
#   - every path of the MEDIA list, matched without regard to case, as Windows will;
#   - any .VQA, SCORES.MIX or TRANSIT.MIX anywhere, so a new folder of them is caught
#     before anybody thinks to add it to the list;
#   - the launcher's own leftovers from fetching or recording them, a download part file
#     (.ocpart) or the choice record (opencnc3d-media.txt), anywhere. A package that
#     carried the record would tell every new install that its player had already chosen.
#
# RED ALERT IS NOT AN EXCEPTION. Its movies and score may not ship either. Its module,
# under ra3d/, is staged without them (tools/ra3d/stage-runtime.py reads the same list),
# so a movie or a score found under ra3d/ is refused here like any other, as the Mac
# packager refuses one.
MEDIA_LEFT=$(cd "$OUT" && {
    while IFS= read -r p; do
        [ -n "$p" ] && find . -ipath "./${p%/}" -print
    done <<MEDIA
$MEDIA_PATHS
MEDIA
    find . \( -iname '*.vqa' -o -iname 'scores.mix' -o -iname 'transit.mix' \) -print
    find . \( -iname '*.ocpart' -o -iname 'opencnc3d-media.txt' \) -print
} | sed 's|^\./||' | sort -u)
[ -z "$MEDIA_LEFT" ] || {
    echo "the package holds movies or music, which no package may carry:" >&2
    echo "$MEDIA_LEFT" | sed 's/^/  /' >&2
    [ ! -d "$OUT/ra3d" ] || {
        echo "Red Alert's movies and score may not ship either. tools/ra3d/stage-runtime.py" >&2
        echo "stages the module without the paths tools/media/media-paths.txt names under" >&2
        echo "ra3d/, so one found there is missing from that list or came by another road." >&2
    }
    exit 1
}
echo "   media: no movies and no music in the package"
# WITHOUT RED ALERT MEANS NONE OF IT. The module is one folder today, and leaving that
# folder out is the whole of the switch; this asks the result rather than trusting it,
# so a pack, a brain or an engine copy for Red Alert that reaches the package by any
# other road (a top-level pack copied by the glob above, say) stops the build here.
if [ $NO_RA -eq 1 ]; then
    RA_LEFT=$(cd "$OUT" && find . \( -iname 'ra3d' -o -iname 'RedAlert*' -o -name '*_RA_*' \) \
        ! -name 'waterbase_RA_TEMPERAT.pack' -print | sed 's|^\./||')
    [ -z "$RA_LEFT" ] || {
        echo "OPENCNC3D_NO_RA=1, and the package still holds Red Alert files:" >&2
        echo "$RA_LEFT" | sed 's/^/  /' >&2
        exit 1
    }
    echo "   red alert: none of it is in the package"
fi
# Diagnostic sessions are runtime output, never release content.
LOG_LEFT=$(cd "$OUT" && find . -ipath './Logs' -print)
[ -z "$LOG_LEFT" ] || {
    echo "the package holds runtime diagnostic Logs; session reports must not ship" >&2
    exit 1
}
# NO TWO PATHS THAT ARE ONE PATH ON WINDOWS. Windows compares names without regard to
# case, so two entries that differ only in case unpack as one file, and which of the two
# survives depends on the order of the zip. On a case-sensitive file system both are
# here to be counted; on a case-insensitive one the pair has already become one file,
# which is why the names placed by hand were checked before they were copied.
CASE_DUP=$(cd "$OUT" && find . -print | tr '[:upper:]' '[:lower:]' | sort | uniq -d | sed 's|^\./||')
[ -z "$CASE_DUP" ] || {
    echo "the package holds paths that differ only in letter case, which Windows treats" >&2
    echo "as the same path:" >&2
    echo "$CASE_DUP" | sed 's/^/  /' >&2
    exit 1
}

SIZE=$(du -sh "$OUT" | cut -f1)
echo "   $OUT  ($SIZE)"

if [ $DO_ZIP -eq 1 ]; then
    # A fresh release-owned override may not exist yet. Resolve it before the
    # archive subshell changes directory, so relative custom paths work too.
    mkdir -p "$DIST_ZIP"
    DIST_ZIP=$(cd "$DIST_ZIP" && pwd -P)
    [ $BINS_ONLY -eq 1 ] && SUF="-bins" || SUF=""
    ZIP="$DIST_ZIP/OpenCNC3D-windows-$GITDESC$SUF.zip"
    echo "== zipping to $ZIP"
    rm -f "$ZIP"
    # THE LAST ENTRIES ARE MADE, NOT HOPED FOR, because the launcher that unpacks this
    # zip is the one already installed. Launchers up to v0.6.11 write entries in zip
    # order with a plain fopen, write the install record wherever the zip lists it, and
    # stop at the first refusal; SDL2.dll is the one file they always have loaded, so
    # Windows always refuses it. zip -r lists files in whatever order the file system
    # hands them out, which put the record at entry 412 of 460, so an old launcher that
    # stopped at any of the 47 files after it (a pack it could not write, the window
    # closed part way) left a folder claiming a version it did not hold. So the full
    # package ends
    #
    #     CHANGELOG.txt  opencnc3d-install.txt  BUILD-ID.txt  SDL2.dll
    #
    # An old launcher stopped anywhere before the record has not claimed the new
    # version, and the update is offered again. One that reaches SDL2.dll has written
    # everything else, its own replacement included, and the next start runs the new
    # launcher, which replaces files by renaming.
    #
    # THE BINARY-ONLY ZIP IS FLAT, ending BUILD-ID.txt, SDL2.dll, and carries no record.
    # Those launchers unpack it without stripping a folder, so a zip wrapped in
    # OpenCNC3D-windows-<id>/ lands in a subfolder of the install, leaves every binary at
    # the top untouched, and records the new version; launchers from v0.6.12 on read
    # the shape off the archive either way.
    #
    # Both only make SDL2.dll harmless while it is byte identical to the installed copy,
    # which tools/release.sh holds it to (the note at SDL2_VER in setup-toolchain.sh).
    if [ $BINS_ONLY -eq 1 ]; then
        ZDIR="$OUT"
        ZPRE=""
    else
        ZDIR="$STAGE"
        ZPRE="$NAME/"
    fi
    ZTAIL=""
    for f in CHANGELOG.txt opencnc3d-install.txt BUILD-ID.txt SDL2.dll; do
        [ -f "$OUT/$f" ] && ZTAIL="$ZTAIL $f"
    done
    (
        cd "$ZDIR"
        set --
        for f in $ZTAIL; do set -- "$@" -x "$ZPRE$f"; done
        if [ -n "$ZPRE" ]; then
            zip -9rqX "$ZIP" "$NAME" -x '*.DS_Store' "$@"
        else
            zip -9rqX "$ZIP" . -x '*.DS_Store' "$@"
        fi
        for f in $ZTAIL; do zip -9qX -g "$ZIP" "$ZPRE$f"; done
    )
    ZWANT=$(for f in $ZTAIL; do printf '%s%s\n' "$ZPRE" "$f"; done)
    ZGOT=$(unzip -Z1 "$ZIP" | tail -n "$(echo $ZTAIL | wc -w | tr -d ' ')")
    ZBAD=""
    [ "$ZGOT" = "$ZWANT" ] || ZBAD="its last entries are not$ZTAIL"
    case " $ZTAIL " in *" SDL2.dll "*) ;; *) ZBAD="it carries no SDL2.dll" ;; esac
    if [ $BINS_ONLY -eq 1 ]; then
        ! unzip -Z1 "$ZIP" | grep -q / || ZBAD="the binary-only zip is not flat"
    else
        case " $ZTAIL " in *" opencnc3d-install.txt "*) ;; *) ZBAD="it carries no opencnc3d-install.txt" ;; esac
    fi
    [ -z "$ZBAD" ] || {
        rm -f "$ZIP"
        echo "the Windows zip is refused: $ZBAD. Every installed launcher up to v0.6.11" >&2
        echo "writes the install record where the zip lists it and stops at SDL2.dll," >&2
        echo "so the order above is what keeps those players' folders honest. The zip" >&2
        echo "has been removed." >&2
        exit 1
    }
    echo "   last entries:$ZTAIL$([ $BINS_ONLY -eq 1 ] && echo ', flat')"
    echo
    echo "ready: $ZIP  ($(du -h "$ZIP" | cut -f1))"
else
    echo
    echo "ready: $OUT (not zipped)"
fi
