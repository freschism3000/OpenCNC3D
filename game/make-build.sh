#!/bin/sh
# Build the current commit and refresh the Desktop test folder.
# The packs and dylib come from LOCAL_DATA (game data never lives in git; see DATA.md).
set -e
cd "$(dirname "$0")"
# THE DESTINATION AND THE DATA BELONG TO THE WORKING COPY THIS SCRIPT LIVES IN, and both
# used to be spelled $HOME/CNC3D regardless of where that was.
#
# On the one working copy whose path happens to be $HOME/CNC3D that reads the same either
# way, which is why it survived. Anywhere else it is wrong and it is wrong SILENTLY: a
# build run from a second checkout compiles this tree's sources and then installs them
# over the FIRST checkout's playable folder, so one session's binaries replace another's
# and the script reports success. Both are legitimate arrangements (a scratch clone, a
# second branch, two sessions working in parallel), and neither should be able to write
# into the other by default.
#
# $REPO is this script's own parent, so the answer is the same on the original copy and
# correct everywhere else. An explicit first argument still wins, which is what
# tools/release.sh and any deliberate out-of-tree build rely on.
REPO="$(cd .. && pwd)"
DEST="${1:-$REPO/playable}"
LOCAL_DATA="${CNC3D_DATA:-$REPO/data/dist-v0.3.1}"
GITDESC=$(git describe --always --dirty 2>/dev/null || echo untracked)
# tools/release.sh sets CNC3D_BUILD_ID to the number it is about to tag, because at the
# moment it builds, VERSION has been bumped and not yet committed, so git describe would
# name the artefact "-dirty" for what is about to become a clean release.
[ -n "$CNC3D_BUILD_ID" ] && GITDESC="$CNC3D_BUILD_ID"

# GENERATE BEFORE YOU COMPILE. bake_pack.py writes game/hud640_layout.h, and hud640.h
# takes every coordinate from it, so it is a compile INPUT. It used to run after the three
# builds, two hundred lines below, which left every binary a second or two older than a
# header it had compiled against and made G30 report STALE after a perfectly good build.
# "STALE by 0 min" is the signature: not a real staleness, a same-minute ordering.
# This is the FIFTH variant of one trap on this project -- a tool touching a file another
# tool is timed against -- and the other four all cost somebody an afternoon. The bake
# itself still runs in its old place for the PACK it writes; this early call is only so
# the header exists and is older than the objects. It is cheap and idempotent: a second
# run rewrites identical bytes, and the guard below is the same one the later call uses.
python3 ../tools/sidebar_redesign/bake_pack.py >/dev/null || {
    echo "make-build.sh: bake_pack.py failed; hud640_layout.h would be stale" >&2; exit 1; }

./build.sh
(cd ../app && ./build.sh)          # the merged program: menu, movies, mission, sound
(cd ../launcher && ./build.sh) # the front door: version, changelog, update, Play
mkdir -p "$DEST"
cp cnc_eyes "$DEST/"
cp ../app/cnc3d "$DEST/"

# THE LAUNCHER LIVES AT THE PACKAGE ROOT, not inside C&C3D.app, and that is on
# purpose. Put it in the bundle and it needs its own copy of the SDL libraries
# three directories away from the ones already here, or a rewritten load command
# that breaks the moment anyone moves the app. At the root it is an ordinary file
# beside the game: tools/bundle-sdl.sh below finds it by scanning the folder, and
# the binary-only update can name it. C&C3D.app stays a tracked, binary-free
# wrapper whose script execs it (repository rules rule 2 keeps binaries out of git).
cp ../launcher/cnc3d-launcher "$DEST/"
[ -x "$DEST/cnc3d-launcher" ] || { echo "the launcher did not reach $DEST" >&2; exit 1; }

# And the bundle that opens it, so a freshly built playable/ is the shape a player
# gets rather than a folder with no way in. rsync -aL because the tracked master
# is the only copy and this must not follow it back.
rsync -aL --exclude '.DS_Store' "$REPO/tools/launchers/C&C3D.app" "$DEST/" 2>/dev/null || true

# THE SDL LIBRARIES TRAVEL WITH THE BUILD. Until 20 Aug 2026 they did not, and every
# macOS release ever cut was unplayable on any Mac but this one: both binaries carried
# /usr/local/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib as a hard load command, there was no
# LC_RPATH, and no such file was in the folder. dyld gave up before main() ran. That is
# the same class of bug as the dosdata symlink below -- the build works here and is dead
# on arrival anywhere else -- and it breaks rule 9's "A RELEASE IS PLAYABLE".
#
# Called with no binary list ON PURPOSE, so it scans the whole folder rather than the two
# files this script just copied: playable/ is what gets zipped, and anything else left in
# it (cnc_eyes_head, say) would otherwise ship still pointing at Homebrew.
sh ../tools/bundle-sdl.sh "$DEST"

cp ../menu/dosmenu.pack "$DEST/" 2>/dev/null || true

# WHAT LOCAL_DATA IS, AND WHY ALMOST NOTHING COMES FROM IT.
#
# This block used to be a loop that tested ten files for existence and printed a WARNING.
# It never copied one. `grep -n LOCAL_DATA` found three lines in the whole script: the
# header comment, the assignment, and that test. So the comment at the top of this file,
# "The packs and dylib come from LOCAL_DATA", was true of nothing.
#
# The first repair attempted here was to make that loop copy what it checked. THAT WAS
# WRONG AND IS WORTH RECORDING, because it looked obviously right. LOCAL_DATA points at
# data/dist-v0.3.1, which is a SNAPSHOT OF v0.3.1 and is stale by many releases: its
# campaign.pack is 12449036 bytes against the bakery's current 3248248, and its
# dosinfantry.pack is 4254740 against 4973856. Copying from it produces a play folder that
# is complete, starts, runs, and is wrong. Measured: a worktree assembled that way scored
# 96 pass and 7 fail, the failures being every infantry gate, the flame tongue, the
# bilinear fringe and all three campaign-flow gates, none of which name data as the cause.
#
# EVERY auxiliary pack has a baker in this repo: bake_campaign.py, bake_dosinfantry.py,
# bake_dossidebar.py, bake_dosmake.py, bake_efx.py, bake_cameos.py, bake_verdict.py,
# bake_dostiberium.py, bake_smudges.py and the three under tools/sidebar_redesign. So the
# correct source for all of them is the bakery, never the snapshot, and this script's job
# is to say so rather than to paper over it with old bytes.
#
# content/ and CONQUER.MIX are the two with no baker. They are left alone here: where they
# are meant to live is an open question in known-gap notes and not this script's to settle.
# Note only that the RIGHT CONQUER.MIX is the 268366-byte one beside the other play data,
# not the 2244000-byte archive of the same name in data/dosdata, which is a different file.

# THE PACKS THE GAME ACTUALLY RUNS COME FROM THE BAKERY, and until 19 Aug 2026 nothing
# put them here: this script copied the binaries and left whatever .pack files happened
# to be sitting in the folder from some earlier hand copy. That is how a build can carry
# a fix in its source and not in its data, which makes the build number on the menu a
# lie. bake5.py writes into tools/bakery/game; anything newer there wins.
#
# TWO PLACES, because the bakery is not the only thing that writes a pack. bake5.py puts
# the per-scenario packs in tools/bakery/game, and the standalone bakers -- tiberium,
# smudges, the verdict banner, the DOS infantry -- write beside themselves in game/. A
# refresh that scanned only the first would ship a stale smudge.pack on a clean machine
# and nothing would say so, which is the same hole this block was written to close.
# TWO BAKERS DO NOT WRITE INTO EITHER SCANNED DIRECTORY, so the loop below cannot
# refresh what they make and a folder that already has an old copy keeps it for ever:
#
#   bake_pack.py  writes straight to $REPO/playable/hud640.pack -- not tools/bakery/game,
#                 not game/ -- so the loop has never seen it at all.
#   bake_logos.py writes game/logos.pack, which the loop DOES see, but only a re-bake
#                 makes it newer than the copy already in the folder.
#
# That is how a build ships a feature's code without its art: the DATABASE tab's plate
# lives in hud640.pack and its EVA wordmark in logos.pack, and a binary-only refresh
# leaves both behind. Baking here costs a couple of seconds and closes it. Both read only
# committed inputs (tools/sidebar_redesign/chunks/ and data/rom), so this works on a
# clean clone.
echo "baking the two packs no scanned directory carries"
python3 ../tools/sidebar_redesign/bake_pack.py >/dev/null || {
    echo "make-build.sh: bake_pack.py failed; hud640.pack would be stale" >&2; exit 1; }
python3 bake_logos.py >/dev/null || {
    echo "make-build.sh: bake_logos.py failed; logos.pack would be stale" >&2; exit 1; }
# bake_pack.py writes into the REPO's playable, which is not necessarily $DEST.
if [ "$REPO/playable/hud640.pack" != "$DEST/hud640.pack" ]; then
    cp "$REPO/playable/hud640.pack" "$DEST/hud640.pack"
fi

n=0
for BAKED in ../tools/bakery/game .; do
  [ -d "$BAKED" ] || continue
  for p in "$BAKED"/*.pack; do
    [ -f "$p" ] || continue
    b=$(basename "$p")
    if [ ! -f "$DEST/$b" ] || [ "$p" -nt "$DEST/$b" ]; then cp "$p" "$DEST/$b"; n=$((n+1)); fi
  done
done
echo "packs: $n refreshed"
rsync -a missions "$DEST/" 2>/dev/null || true

# THE BRAIN TRAVELS WITH THE BUILD TOO, and until 22 Aug 2026 it did not.
#
# tools/mac/build-brain-mac.sh writes TiberianDawn.dylib into game/ and, unless you pass
# it a second destination by hand, NOWHERE ELSE. This script copied binaries and packs and
# left the brain in playable/ as whatever was last hand copied. So the shipped macOS brain
# was not a product of the build; it was a product of somebody remembering.
#
# WHAT THAT COSTS, with the receipt. v0.6.0 is the release that made aircraft exist, and
# it needed BOTH halves: a renderer that knows about K_AIRCRAFT and a brain that exports
# the Aircraft heap. It shipped correct only because the brain had been hand copied
# minutes earlier. Without that copy the package would have carried the new renderer over
# a brain that emits no aircraft at all, the milestone would have shipped doing nothing,
# and every gate would still have passed -- because the gates run in playable/ against
# that same stale brain, so they would have been asking the old brain whether the new
# feature worked.
#
# Copied UNCONDITIONALLY and then ASSERTED, the same way the gate files above are: a
# refresh that silently does nothing is the failure being closed, so "it did not arrive"
# has to be fatal rather than a warning nobody reads.
if [ -f TiberianDawn.dylib ]; then
    cp TiberianDawn.dylib "$DEST/TiberianDawn.dylib"
    [ -f "$DEST/TiberianDawn.dylib" ] || {
        echo "FAILED to copy TiberianDawn.dylib into $DEST" >&2; exit 1; }
    # Same file, byte for byte. cmp rather than a timestamp: a copy that half wrote is
    # newer AND wrong, which is the one case a timestamp cannot see.
    cmp -s TiberianDawn.dylib "$DEST/TiberianDawn.dylib" || {
        echo "TiberianDawn.dylib in $DEST does not match the one in game/" >&2; exit 1; }
    echo "brain: TiberianDawn.dylib refreshed into $DEST"
else
    echo "FATAL: no TiberianDawn.dylib in game/. Run tools/mac/build-brain-mac.sh." >&2
    echo "Refusing to make a build whose brain is whatever was last copied by hand:" >&2
    echo "that is how a release ships a renderer and an engine that disagree." >&2
    exit 1
fi

# THE GATE SUITE TRAVELS WITH THE BUILD, and until 20 Aug 2026 it did not.
#
# tools/release.sh gates every release with `sh playable/gates.sh` (the gates block, the
# line that sets RUNDIR to $ROOT/playable). playable/ is ignored in full (.gitignore:25)
# and nothing here ever put a gate file in it, so playable/gates.sh and playable/gate_*.py
# were HAND-SYNCED copies of the tracked originals in game/. Two consequences, and the
# second is the quiet one:
#
#   * a fresh clone cannot cut a release at all -- there is no playable/gates.sh to run;
#   * on this machine a release could be gated by a suite older than the one in git,
#     with nothing anywhere saying which one had run.
#
# G48 made that worse by hard-depending on gate_jukebox.py, so the suite in the run
# folder now needs a python file as well as the shell script.
#
# Copied UNCONDITIONALLY, not "if newer": the tracked file in game/ is the suite, and a
# hand edit made in the run folder is exactly the drift this closes. Every name below was
# checked to exist in game/ before it went on the list. Everything else the gates read
# (gate_cargo.txt, gfx_g36_*.cfg, gate_gfx_rt.txt and the rest) is WRITTEN BY gates.sh at
# run time, so it is deliberately not here.
g=0
for f in gates.sh gate_*.txt gate_*.py gate_*.sh gfx_lowsun.cfg selfplay.txt; do
  [ -f "$f" ] || continue          # a pattern that matched nothing stays literal in sh
  cp "$f" "$DEST/$f"; g=$((g+1))
done
[ -f "$DEST/gates.sh" ] || { echo "FAIL: gates.sh did not reach $DEST" >&2; exit 1; }
# gfx_lowsun.cfg by name as well as by the loop above, because it is the one gate INPUT
# that is a preset rather than a script and its absence is silent: cnc_eyes takes
# `--gfx NOSUCHFILE.cfg` without complaint (now it says so on stderr, cnc_eyes.cpp's
# --gfx branch), so G52 would measure infantry shadows under the DEFAULT 52 degree sun
# instead of the low one and still report a number.
[ -f "$DEST/gfx_lowsun.cfg" ] || { echo "FAIL: gfx_lowsun.cfg did not reach $DEST" >&2; exit 1; }
# selfplay.txt by name for the same reason, and it is the one that was actually missing:
# gates.sh reads it as `--script selfplay.txt` after the cd to RUNDIR, so G3 -- one of the
# five mandatory gates -- went red on any machine that did not happen to have a hand-copied
# one sitting in the run folder. It matched none of the patterns above because it is neither
# gate_* nor a preset.
[ -f "$DEST/selfplay.txt" ] || { echo "FAIL: selfplay.txt did not reach $DEST (G3 reads it)" >&2; exit 1; }
# gate_optlayout is a BINARY, so it matches none of the patterns above: they are .txt, .py
# and .sh. It is the pause dialog's geometry gate (G57), built by game/build.sh out of
# game/gate_optlayout.c, and it is copied and asserted BY NAME for exactly the reason
# gfx_lowsun.cfg is: a gate input whose absence would otherwise be silent. G57 is written
# to go RED rather than skip when it is not here, because a gate that could not run is not
# a gate that passed.
cp gate_optlayout "$DEST/" 2>/dev/null || true
[ -x "$DEST/gate_optlayout" ] || { echo "FAIL: gate_optlayout did not reach $DEST (G57 runs it; game/build.sh makes it)" >&2; exit 1; }
g=$((g+1))
# THE LOCKSTEP GATES, and the same argument as gate_optlayout word for word: they are
# binaries, so none of the .txt/.py/.sh patterns above reach them, and G130 runs both of
# them from this folder. Without this the gate that needs no window, no pack and no brain
# is the one gate that cannot run, and it reports exit=99 as a FAILURE rather than a skip.
# That is deliberate in the gate and useless here: a red light for a missing file, on the
# suite that decides whether a release may be cut.
#
# netcheck is not a gate. It rides along because this folder is where a person is standing
# when a match will not start, and it is the tool that answers whether the two ends can
# lockstep at all before the game is blamed for it.
for b in gate_lockstep gate_netloop netcheck; do
  cp "$b" "$DEST/" 2>/dev/null || true
  [ -x "$DEST/$b" ] || { echo "FAIL: $b did not reach $DEST (G130 runs the two gates; game/build.sh makes all three)" >&2; exit 1; }
  g=$((g+1))
done
echo "gates: $g files copied to $DEST"

# THE SOUND DATA. Without dosdata/ beside the binary the build boots, plays and is
# completely silent, and the only thing that says so is one line on stderr. The
# archives are large (SCORES.MIX alone is 54 MB), so this is a symlink into the repo's
# data/dosdata rather than a copy; a release tarball substitutes a real copy.
if [ ! -e "$DEST/dosdata" ] && [ -d ../data/dosdata ]; then
  ln -s "$(cd ../data/dosdata && pwd)" "$DEST/dosdata"
  echo "linked dosdata -> $DEST/dosdata"
fi
# EVERY MACH-O IN THE FOLDER DECLARES A macOS FLOOR THIS OLD OR OLDER. Measured 20 Aug
# 2026: cnc3d, cnc_eyes AND TiberianDawn.dylib all carried LC_BUILD_VERSION minos 26.0,
# because no build script set a deployment target and clang stamps whatever SDK it finds.
# dyld REFUSES a Mach-O whose minos is newer than the running system, so every Mac below
# macOS 26 rejected the release before main() ran -- on top of, and hiding behind, the
# SDL bundling that was supposed to make a release playable elsewhere.
#
# WHAT IS CHECKED: cnc3d, cnc_eyes, netcheck and every .dylib -- the binaries by name and
# the libraries by the same *.dylib glob the macOS packager uses. netcheck is on the list
# because it ships too, and because of what it is for: it is the tool a person reaches for
# when a match will not start, so a copy that dyld refuses would answer that question with
# nothing at all. Not the whole folder: playable/ is the developer's working folder
# and accumulates things that are on nobody's allow-list (cnc_eyes_head, a stale binary
# referenced by nothing, is sitting in it as this is written), and a build that refuses
# over a file that does not ship is a build that gets switched off.
#
# TiberianDawn.dylib is here for the reason the whole item exists. It is NOT built by
# this script -- tools/mac/build-brain-mac.sh builds it and copies it in -- so a hand
# copied 26.0 brain beside two fixed binaries is the exact half-fix this catches. All
# three load in one process: two out of three still refuses to launch, and merely looks
# fixed.
. ../tools/mac/deployment-target.sh
floorbad=0
for p in "$DEST/cnc3d" "$DEST/cnc_eyes" "$DEST/netcheck" "$DEST"/*.dylib; do
  [ -f "$p" ] || continue
  MIN=$(otool -l "$p" 2>/dev/null \
        | awk '/LC_BUILD_VERSION|LC_VERSION_MIN_MACOSX/{f=1} f && /^ *(minos|version) /{print $2; exit}')
  if [ -z "$MIN" ]; then
    echo "FAIL: $(basename "$p") declares no macOS floor at all (no LC_BUILD_VERSION)." >&2
    floorbad=1; continue
  fi
  awk -v a="$MIN" -v b="$CNC3D_MACOS_MIN" 'BEGIN{
        split(a,x,"."); split(b,y,".");
        exit ((x[1]*10000+x[2]*100+x[3]) <= (y[1]*10000+y[2]*100+y[3])) ? 0 : 1 }' \
    || { echo "FAIL: $(basename "$p") declares macOS $MIN, above the $CNC3D_MACOS_MIN floor." >&2
         floorbad=1; }
done
[ "$floorbad" = "0" ] || {
  echo "" >&2
  echo "A binary that asks for a newer macOS than the one running it is refused by dyld" >&2
  echo "before main(), so this build would not launch on any Mac below that version." >&2
  echo "The renderer and the program get the floor from tools/mac/deployment-target.sh;" >&2
  echo "the brain gets it from tools/mac/build-brain-mac.sh, which is what to run if the" >&2
  echo "name above is TiberianDawn.dylib." >&2
  exit 1; }
echo "macOS floor: cnc3d, cnc_eyes, netcheck and every .dylib in $DEST are $CNC3D_MACOS_MIN or older"

# A COMPLETE PLAY FOLDER, OR A REFUSAL THAT NAMES WHAT IS MISSING AND WHO MAKES IT.
#
# Almost every one of these degrades quietly when absent rather than failing: no cameos
# gives text-only buttons, no smudge pack draws no scorch marks or building aprons, no
# tiberium pack draws procedural crystals, no HUD pack silently falls back to the DOS bar.
# Only dossidebar.pack refuses out loud, and only content/ crashes. So a folder can be
# missing eleven of its thirteen data files, start, run a mission, and look wrong in ways
# nobody attributes to the build. That is exactly the shape of failure this project keeps
# paying for, and rule 9 says a release is PLAYABLE, not merely startable.
#
# The second column is the point: a missing file is useless information without the command
# that makes it. Everything below is either copied from LOCAL_DATA above or written by one
# named command in this repo.
MISSING=""
check_asset() {   # $1 = file or dir, $2 = how to get it
  [ -e "$DEST/$1" ] || MISSING="$MISSING
  $1
      $2"
}
check_asset content            "the theater archives. No baker in this repo: see known-gap notes"
check_asset CONQUER.MIX        "the 268366-byte one. NOT data/dosdata's 2244000-byte file of that name"
check_asset dossidebar.pack    "game/bake_dossidebar.py   (the game REFUSES to draw a sidebar without it)"
check_asset cameos.pack        "game/bake_cameos.py"
check_asset dosinfantry.pack   "game/bake_dosinfantry.py"
check_asset dosmake.pack       "game/bake_dosmake.py"
check_asset campaign.pack      "game/bake_campaign.py"
check_asset efx.pack           "game/bake_efx.py"
check_asset dosmenu.pack       "menu/, copied above"
check_asset verdict.pack       "game/bake_verdict.py"
check_asset dostib.pack        "game/bake_dostiberium.py"
check_asset smudge.pack        "game/bake_smudges.py"
check_asset logos.pack         "game/bake_logos.py       (the score screen's spinning faction logo)"
check_asset hud640.pack        "tools/sidebar_redesign/bake_pack.py"
check_asset cameos95.pack      "tools/sidebar_redesign/cameos95.py"
check_asset cameos_tdr.pack    "tools/sidebar_redesign/cameos_tdr.py"

# CONTENT, NOT EXISTENCE. check_asset above answers "is there a file called that", which
# a pack from any previous build satisfies for ever. These two carry art that a specific
# feature needs, and a stale one of either is the failure mode this whole block exists to
# prevent: correct code, old data, no error anywhere. Cheap to ask, so ask.
grep -qa tab_database "$DEST/hud640.pack" 2>/dev/null || MISSING="$MISSING
  hud640.pack carries no tab_database plate, so the DATABASE tab cannot draw
      python3 tools/sidebar_redesign/bake_pack.py"
python3 - "$DEST/logos.pack" <<'LOGOCHK' 2>/dev/null || MISSING="$MISSING
  logos.pack carries fewer than 4 logos, so the EVA wordmark is missing
      python3 game/bake_logos.py"
import struct, sys
d = open(sys.argv[1], "rb").read()
sys.exit(0 if d[:8] == b"C3DLOGO1" and struct.unpack("<I", d[8:12])[0] >= 4 else 1)
LOGOCHK

if [ -n "$MISSING" ]; then
  echo "" >&2
  echo "make-build.sh: $DEST is NOT a complete play folder. Missing:$MISSING" >&2
  echo "" >&2
  echo "Refusing rather than shipping a folder that starts and is wrong. Most of these" >&2
  echo "degrade silently, so the damage would show up as a renderer bug in somebody's" >&2
  echo "report weeks later. See known-gap notes, \"make-build.sh cannot assemble a" >&2
  echo "complete play folder from a fresh checkout\"." >&2
  echo "" >&2
  echo "Do NOT satisfy these by copying from data/dist-v0.3.1. It is a v0.3.1 snapshot," >&2
  echo "stale by several releases: a folder filled from it passes this check and then" >&2
  echo "fails seven gates that all appear to blame the renderer." >&2
  exit 1
fi

# SKIRMISH NEEDS A PAIR PER MAP, and nothing above can see that. Every name in the list
# above is a fixed auxiliary pack; not one scenario pack is among them, and the missions
# copy is "|| true", so a folder can pass every check written so far with the SCM mission
# INIs present and no SCM packs at all. skirmish_scan in app/cnc3d.cpp admits a map only
# when it finds BOTH halves, so such a folder ships a Skirmish button with an empty lobby
# behind it and nothing in this file says a word.
#
# PAIRS, NOT TOTALS. An INI can be staged without a pack, so "every INI has a pack" is
# not the rule and would refuse a folder that is correct. The rule is that at least one
# map is complete.
skpairs=0
for ini in "$DEST"/missions/SCM*.INI; do
  [ -f "$ini" ] || continue
  code=$(basename "$ini" .INI)
  if [ -f "$DEST/$code.pack" ]; then skpairs=$((skpairs+1)); fi
done
if [ "$skpairs" -eq 0 ]; then
  echo "" >&2
  echo "make-build.sh: $DEST has no playable skirmish map." >&2
  echo "A map counts only where the game counts it: missions/<CODE>.INI and" >&2
  echo "<CODE>.pack both present. sh tools/stage-skirmish-maps.sh makes both." >&2
  exit 1
fi
echo "skirmish: $skpairs map(s) with an INI and a pack"

echo "build $GITDESC -> $DEST"
echo "$GITDESC  $(date '+%Y-%m-%d %H:%M')" >> "$DEST/BUILD-HISTORY.txt"
