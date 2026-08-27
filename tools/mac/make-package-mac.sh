#!/bin/sh
# Assemble the macOS release folder: the game, and nothing else.
#
#   tools/mac/make-package-mac.sh <destdir>
#   CNC3D_PLAYABLE=/some/playable tools/mac/make-package-mac.sh <destdir>
#
# tools/release.sh calls this and then zips <destdir>. It is a separate script rather
# than a block inside release.sh for the same reason tools/win/make-build-win.sh is one:
# the packaging has to be runnable on its own, because release.sh commits, tags, pushes
# and publishes, and "let me just check what would go in the zip" must not be a thing
# that requires cutting a release to find out.
#
# THE FILE LIST IS AN INCLUDE LIST, NOT AN EXCLUDE LIST. That sentence is copied from
# make-build-win.sh on purpose: the two platforms should read the same way side by side.
#
# WHY, with the receipt. Earlier release.sh made the macOS package with
#
#     cp -RL playable "$STAGEDIR/$MACFULLNAME"
#     rm -f  "$STAGEDIR/$MACFULLNAME"/BUILD-HISTORY.txt
#
# which is "everything, minus exactly one file". But playable/ is the DEVELOPER'S
# working folder, and it accumulates. At v0.5.7 it held 433 gate screenshots in shots/
# (255 MB), three .wav audio captures, a save/load test directory, roughly 60 gate
# scripts and transcripts at the top level, about 40 loose proof PNGs, a stale binary
# (cnc_eyes_head, referenced by nothing in the repo), the gate suite itself, and the
# hand-tuned presentation preset. Every one of those went out to players. The published
# macOS zip was 748 MB against Windows' 511 MB and the 237 MB difference was almost
# exactly shots/.
#
# An exclude list cannot close that, because the next new kind of artefact is not on it
# and nobody finds out. An allow-list fails the other way round: it OMITS something,
# which is visible on day one and gets reported, instead of SHIPPING something, which
# is invisible for six releases. That asymmetry is the whole argument.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

OUT="$1"
[ -n "$OUT" ] || { echo "usage: tools/mac/make-package-mac.sh <destdir>" >&2; exit 2; }
SRC="${CNC3D_PLAYABLE:-$ROOT/playable}"
[ -d "$SRC" ] || { echo "no build folder at $SRC. Run game/make-build.sh first." >&2; exit 1; }

die() { printf '\nPACKAGING REFUSED: %s\n' "$*" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"

# A required entry is copied with need_*, an optional one with the [ -f ] && cp form.
# The difference matters: a missing SCORES.MIX has to stop the release, while a missing
# dostib.pack only means that baker has not been run on this machine yet.
need_file() {
    [ -f "$SRC/$1" ] || die "$1 is not in $SRC.
       It is on the macOS package's allow-list, so the package cannot be built without
       it. Run game/make-build.sh (and, for the packs, the bakers it names) first."
    cp "$SRC/$1" "$OUT/"
}
need_dir() {
    [ -d "$SRC/$1" ] || die "$1 is not a directory in $SRC (a dangling symlink counts).
       It is on the macOS package's allow-list."
    # -L dereferences, --exclude drops the Finder's droppings before they reach the zip.
    rsync -aL --exclude '.DS_Store' "$SRC/$1" "$OUT/"
}

# ---- the program ------------------------------------------------------------------
# Two binaries, not one. cnc3d is the app (menu, campaign, movies, and the Tier 2 chain
# on at startup); cnc_eyes is the renderer run directly, straight into one mission with
# the chain off.
#
# TEN of the package's thirteen .command launchers exec cnc_eyes, not four: PLAY-GDI,
# PLAY-NOD, PLAY-HUD-NEW, PLAY-HUD-OLD, PLAY-HUD-NOD, PLAY-BUILD-TEST, PLAY-CAMERA-AB,
# TUNE-SHADOWS, TUNE-EXPLOSIONS, TEST-MUZZLE-FLASH. Only PLAY, PLAY-HUD-MENU and
# TUNE-PRESENTATION run cnc3d, and so does C&C3D.app. Shipping cnc3d alone would leave
# ten of the fourteen double-click entry points dead. Counted over the union this script
# actually copies below, which is tools/launchers/*.command plus $SRC/*.command.
#
# The READ-ME names both binaries and splits its launcher list by which one each runs,
# because that is also what decides whether the picture comes up enhanced or classic.
# See tools/launchers/README.md.
need_file cnc3d
need_file cnc_eyes

# THE LAUNCHER. C&C3D.app's script execs this; without it the bundle falls back to
# running the game directly, which works but silently ships a package with no way
# to see the version, read the changelog or take an update. That fallback is there
# so a Mac can always play, not so a release can quietly lose its front door, so
# the packager refuses instead of relying on it.
need_file cnc3d-launcher

# EVERY .dylib, BY GLOB, not by name -- and the reason the glob is not a name list is
# sitting in the folder. Today it matches THREE:
#
#   TiberianDawn.dylib     the GPL engine
#   libSDL2-2.0.0.dylib    bundled by tools/bundle-sdl.sh, which also rewrites the load
#                          command, so `otool -L playable/cnc3d` now reads
#                          @executable_path/libSDL2-2.0.0.dylib and no longer names a
#                          Homebrew prefix that does not exist on the player's machine
#   libSDL3.dylib          what Homebrew installs as "SDL2" is sdl2-compat, a shim that
#                          dlopens SDL3 at run time. That dependency appears in NO
#                          otool output; bundle-sdl.sh found it by reading the shim's
#                          dlopen candidate list out of its string table
#
# Only the first was foreseen when this list was written. The other two arrived from a
# fix in a different script on a different day, and both reached the package without
# anyone editing this file -- which is the whole argument for matching a pattern here
# rather than enumerating names. A name list would have shipped the engine, omitted both
# SDL libraries, and produced a zip that dies in dyld before main() on any Mac that is
# not this one.
#
# The pattern is bare on purpose: "$SRC" is quoted so a path with spaces survives, and
# *.dylib is left OUTSIDE the quotes so the shell still expands it. Quote the whole
# thing and the loop runs once on a literal filename ending in "*.dylib", the [ -f ]
# guard skips it, _n stays 0, and the die below reports "there is no .dylib" for a
# folder that is full of them.
_n=0
for f in "$SRC"/*.dylib; do
    [ -f "$f" ] || continue
    cp "$f" "$OUT/"; _n=$((_n+1))
done
[ "$_n" -gt 0 ] || die "there is no .dylib in $SRC. The engine ships as TiberianDawn.dylib
       and the game cannot boot without it."

# ---- the game data ----------------------------------------------------------------
# The packs are the baked N64 terrain, models, cameos and DOS art; CONQUER.MIX is the
# 1995 archive the engine's own build/sell state machine reads.
_n=0
for f in "$SRC"/*.pack; do
    [ -f "$f" ] || continue
    cp "$f" "$OUT/"; _n=$((_n+1))
done
[ "$_n" -gt 0 ] || die "there are no .pack files in $SRC."
need_file CONQUER.MIX

need_dir content       # the two theatre .MIX stubs the terrain loader wants
need_dir missions      # the scenario INIs
need_dir movies        # LOGO.VQA and INTRO2.VQA

# DOSDATA IS THE ONE THAT HAS TO BE DEREFERENCED, AND IT IS CHECKED AFTERWARDS.
# In playable/ it is a SYMLINK into data/dosdata so that a local test folder does not
# carry a second 527 MB copy of the 1995 discs (game/make-build.sh makes the link, and
# has said "a release substitutes a real copy" since the day it did). Archive that link
# AS a link and it expands on the other machine as a dangling pointer, after which the
# game boots, runs, and is completely silent, with one line on stderr to say so. That is
# project rule 9's "A RELEASE IS PLAYABLE" clause, and it is the single most expensive
# thing this script can get wrong, so it is asserted rather than assumed.
need_dir dosdata
[ -L "$OUT/dosdata" ] && die "dosdata came across as a SYMLINK. Unzipped on another
       machine that is a dangling pointer and the game plays in silence."
[ -f "$OUT/dosdata/SCORES.MIX" ] || die "dosdata/SCORES.MIX is not in the package.
       Without the 1995 archives the build boots, plays, and has no sound at all."

# ---- the launchers ----------------------------------------------------------------
# C&C3D.app is the default double-click; the .command files are the specialised routes
# and the live tuning dials. Copied by glob because in playable/ the class is closed:
# a .command file there IS a double-click entry point, there is no other kind.
#
# TWO SOURCES, AND THE ORDER IS DELIBERATE. tools/launchers/ holds the tracked masters
# and goes down first; playable/ is copied over the top and therefore wins.
#
#   * playable/ wins because it is the folder the gate suite just ran in and the one
#     It was just played, and because it holds two launchers that have no tracked master
#     yet (PLAY-BUILD-TEST.command, PLAY-CAMERA-AB.command; a known gap). Shipping a launcher nobody exercised would be the worse trade.
#   * tools/launchers/ is underneath because a playable/ rebuilt from nothing has no
#     launchers at all: game/make-build.sh lays down binaries, packs, missions and
#     dosdata and not one .command. This step runs AFTER release.sh has tagged and
#     pushed, so refusing there over files that are sitting tracked in the repo two
#     directories away would be pedantry rather than safety.
# ONE APP, NO .command FILES. Each platform ships a single executable called C&C3D,
# carrying a game icon, so it is obvious which file to run.
#
# Thirteen .command launchers used to ship. They existed because `--dir` and `--content`
# defaulted to `../brain/missions/` and `../brain/stubcontent/`, paths that exist in the
# development tree and in no release, so nothing could be started without a script passing
# them in. Those defaults probe for the shipped layout now, the game runs with no arguments,
# and the specialised ones (a HUD, a window size, a scenario) are settings in the game
# rather than a choice of which file to double-click.
#
# The tracked masters stay in tools/launchers/ for development use. They are simply not
# part of what a player is handed.
[ -d "$ROOT/tools/launchers/C&C3D.app" ] || die "tools/launchers/C&C3D.app is missing.
       It is the ONLY thing a player double-clicks now, so there is no fallback to ship
       instead of it."
rsync -aL --exclude '.DS_Store' "$ROOT/tools/launchers/C&C3D.app" "$OUT/"
[ -d "$OUT/C&C3D.app" ] || die "C&C3D.app did not make it into the package. It is the
       default double-click, the one thing that always works, and the READ-ME
       names it first. The tracked master is tools/launchers/C&C3D.app."
[ -f "$OUT/C&C3D.app/Contents/Resources/AppIcon.icns" ] || die "C&C3D.app has no icon.
       it exists so the app is recognisable at a glance; rebuild it with
       tools/launchers/make_icon.py, which also writes the Windows .ico from the same
       source so the two platforms cannot drift."
_n=$(ls -A "$OUT"/*.command 2>/dev/null | wc -l | tr -d ' ')
[ "$_n" = "0" ] || die "a .command launcher reached the package ($_n of them). The macOS
       package ships C&C3D.app and nothing else to double-click; see the note above."

# THE APP OPENS THE LAUNCHER. (Reported: "Make sure its the default
# executeable / app for Windows / Mac, from release v0.6.3 and going forward.")
#
# C&C3D.app is a tracked script wrapper, because a binary cannot be committed
#, and that script has a DELIBERATE FALLBACK: if cnc3d-launcher
# is not beside the game it runs the game directly. That fallback exists so a Mac
# can always play, not so a release can quietly lose its front door, and the two
# are indistinguishable from inside the package. So both halves are checked here:
# the script must reach for the launcher, and the launcher must be there for it to
# find.
grep -q 'cnc3d-launcher' "$OUT/C&C3D.app/Contents/MacOS/cnc3d-launch" \
    || die "C&C3D.app's script does not run cnc3d-launcher. That app is the ONE thing
       a player double-clicks, so a package whose app goes straight to the game ships
       with no launcher at all: no version, no changelog, no update."
cmp -s "$SRC/cnc3d-launcher" "$OUT/cnc3d-launcher" \
    || die "cnc3d-launcher in the package is not the one in $SRC. The app execs it by
       name and would fall through to the game."
strings -a "$OUT/cnc3d-launcher" 2>/dev/null | grep -q '/api/builds' \
    || die "cnc3d-launcher does not contain the site's build route, so whatever it is,
       it is not a launcher that can check for updates. Rebuild it with
       game/make-build.sh, which is what refreshes $SRC."
echo "   C&C3D.app opens cnc3d-launcher, which can reach the site"

# ---- the paper --------------------------------------------------------------------
# FROM THE TRACKED MASTER, not from playable/. The copy that lived in playable/ was
# untracked, nothing regenerated it, and it went out in every release still announcing
# "C&C 3D v0.3.1 (macOS build, )".
[ -f "$ROOT/tools/launchers/READ-ME.txt" ] || die "tools/launchers/READ-ME.txt is missing.
       That file is the macOS package's README and it is tracked; restore it rather than
       falling back to whatever is in $SRC."
cp "$ROOT/tools/launchers/READ-ME.txt" "$OUT/"

# ---- what is deliberately NOT above, named so the omission is visible -------------
#
#  shots/, gate_*.txt, gate_*.py, gate_*.sh, gates.sh, *.log, *.wav, the loose proof
#  PNGs (bu*, gb_*, mcvrig_*, opt_*, part_*, sp*, flow_*, g90_*, h_*), selfplay.txt,
#  buildup_proof.txt
#      The verification harness and its evidence. 255 MB of it, useful only to whoever
#      is proving the build, and it is kept in the working copy. The gates still run
#      from playable/ exactly as before: release.sh runs them there, before this.
#
#  gfx_g44_lit.cfg, gfx_g44_dark.cfg, gfx_lowsun.cfg
#      Gate inputs. Anchored to GATE IDs rather than line numbers, because gates.sh is
#      edited often and a bare number goes stale silently: search the file for the id.
#      gates.sh writes the first two itself, in the preamble to gate G44 (the dynamic
#      light fade), with two `cat > gfx_g44_*.cfg` heredocs. The third it only READS,
#      in gate G52 (infantry shadows), where both ./cnc_eyes runs pass
#      `--gfx gfx_lowsun.cfg`. That one is TRACKED, at game/gfx_lowsun.cfg, and
#      game/make-build.sh copies it into the run folder and asserts it arrived. It used
#      not to be copied, and because a --gfx file that cannot be opened was silent, G52
#      measured infantry shadows under the DEFAULT sun and still reported a number.
#      Both halves of that are fixed; see the CLOSED note. No
#      launcher and no player path touches any of the three.
#
#  cnc3d-fx.cfg
#      THE TUNED PRESENTATION DIALS, and the one omission here that changes what the
#      player sees, so it is spelled out. cnc3d reads that filename at boot
#      (app/cnc3d.cpp:655), so while it sat in playable/ the macOS package shipped it and
#      the Windows package, assembled from a named list, never had it. Two packages of
#      the same tag, rendering from different numbers, with nothing saying so. Both now
#      boot fx_defaults() from game/fx_state.h. The preset itself is NOT lost: it is
#      tracked at docs/tuning/cnc3d-fx.cfg, with a header saying whose it is and how to
#      put it back. One can be regenerated at any time with TUNE-PRESENTATION.command.
#
#  cnc_eyes_head
#      A stale x86_64 binary from 15 Aug that nothing in the repo, gates.sh included,
#      references by name.
#
#  BUILD-HISTORY.txt, .DS_Store
#      Developer bookkeeping. BUILD-HISTORY.txt is the one file the old cp -RL package
#      remembered to delete.

# ---- what the launcher reads about its own install --------------------------------
# Written here, last, so both files describe the folder exactly as it is about to
# be zipped:
#
#   cnc3d-install.txt   the version, and the data fingerprint that decides whether
#                       the next update can be the 2.7 MB one or has to be 507 MB
#   CHANGELOG.txt       so the launcher's panel has the right notes in it before
#                       any network call has been made, or has failed
#
# The fingerprint comes from tools/launcher/data-id.sh, which is also what
# tools/launcher/make-manifest.sh reads back out of this file when it writes the
# manifest. One implementation of that hash, quoted at both ends: two would agree
# until the day they did not, and the only symptom would be every update silently
# becoming a full download.
VERNUM=$(tr -d '[:space:]' < "$ROOT/VERSION")
"$ROOT/tools/launcher/make-changelog-txt.sh" "$OUT/CHANGELOG.txt" >/dev/null
{
    echo "# written by tools/mac/make-package-mac.sh"
    echo "version $VERNUM"
    echo "data_id $("$ROOT/tools/launcher/data-id.sh" "$OUT")"
} > "$OUT/cnc3d-install.txt"
[ -s "$OUT/cnc3d-install.txt" ] || die "cnc3d-install.txt was not written. Without it
       the launcher cannot tell which build this is, and the update host cannot offer
       this package the small binary-only update."
echo "   install record: v$VERNUM  data $(awk '$1=="data_id"{print substr($2,1,12)}' "$OUT/cnc3d-install.txt")"

ENTRIES=$(find "$OUT" -mindepth 1 | wc -l | tr -d ' ')
TOP=$(ls -A "$OUT" | wc -l | tr -d ' ')
SIZE=$(du -sh "$OUT" | cut -f1)
echo "   $OUT  ($SIZE, $TOP top-level entries, $ENTRIES files and directories)"
