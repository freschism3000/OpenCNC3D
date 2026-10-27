#!/bin/sh
# Build the current commit and refresh the Desktop test folder.
# The packs and dylib come from LOCAL_DATA (game data never lives in git; see BUILDING.md).
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
python3 "$REPO/tools/bakery/materialize-grassmask.py"
DEST="${1:-$REPO/playable}"
LOCAL_DATA="${OPENCNC3D_DATA:-$REPO/data/dist-v0.3.1}"
GITDESC=$(git describe --always --dirty 2>/dev/null || echo untracked)
# tools/release.sh sets OPENCNC3D_BUILD_ID to the number it is about to tag, because at the
# moment it builds, VERSION has been bumped and not yet committed, so git describe would
# name the artefact "-dirty" for what is about to become a clean release.
[ -n "$OPENCNC3D_BUILD_ID" ] && GITDESC="$OPENCNC3D_BUILD_ID"

# Freeze the clean source identity before any baker writes derived outputs.
# The large grass-mask raw/provenance files are ignored, hash-verified derivatives
# of the committed compressed source, so materialization keeps this identity clean.
# Both renderer and merged app must compile this same pre-bake identity.
if [ -z "$OPENCNC3D_SKIP_VERSION_HEADER" ]; then
    sh "$REPO/tools/version.sh" --header "$REPO/game/opencnc3d_build.h"
    export OPENCNC3D_SKIP_VERSION_HEADER=1
fi

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

python3 ../menu/bake_enhanced_menu.py ../art/interface/enhanced-menu-v06 ../menu/enhanced-menu.pack

./build.sh
(cd ../app && ./build.sh)          # the merged program: menu, movies, mission, sound
(cd ../launcher && ./build.sh) # the front door: version, changelog, update, Play
mkdir -p "$DEST"
cp cnc_eyes "$DEST/"
cp ../app/opencnc3d "$DEST/"
# Preserve an existing legacy C&C3D.app Dock launcher, whose entry point retains the old name.
if [ -x "$DEST/C&C3D.app/Contents/MacOS/cnc3d-launch" ]; then
    cp ../app/opencnc3d "$DEST/cnc3d"
fi
# Refresh an already staged RA module with this same renderer and its own building art.
#
# OPENCNC3D_NO_RA=1 MAKES A BUILD WITH NO RED ALERT IN IT, which is what the public
# packages are (tools/release.sh sets it). Everything Red Alert lives under ra3d/: its
# brain, its packs, its building art and its copy of the program. This build then puts
# none of it in $DEST, and an ra3d/ already there from an earlier build is left exactly as
# it was rather than deleted, because it is somebody's staged module; both packagers leave
# it out under the same switch.
if [ "$OPENCNC3D_NO_RA" = "1" ]; then
    echo "Red Alert: left out (OPENCNC3D_NO_RA=1); nothing is staged into $DEST/ra3d"
    [ ! -d "$DEST/ra3d" ] || echo "   the ra3d/ already there is from an earlier build: not refreshed, not packaged"
elif [ -d "$DEST/ra3d" ]; then
    cp ../app/opencnc3d "$DEST/ra3d/opencnc3d"
    if [ -x "$DEST/C&C3D.app/Contents/MacOS/cnc3d-launch" ]; then
        cp ../app/opencnc3d "$DEST/ra3d/cnc3d"
    fi
    cp "$REPO/brain/vanilla/build-native/redalert/RedAlert.dylib" "$DEST/ra3d/RedAlert.dylib"
    python3 "$REPO/tools/ra3d/stage-structures.py" "$DEST/ra3d"
    # The 24 multiplayer maps and the list with a picture of each that a skirmish screen
    # reads, refreshed here as well because a module staged before they existed has none.
    python3 "$REPO/tools/ra3d/ramix.py" extract "$REPO/data/radata/GENERAL.MIX" "$DEST/ra3d/missions" \
        $(for n in $(seq -w 1 24); do printf 'SCM%sEA.INI ' "$n"; done) >/dev/null
    python3 "$REPO/tools/ra3d/bake-ra-map-previews.py" "$DEST/ra3d" >/dev/null \
        || { echo "FATAL: Red Alert's map previews did not bake" >&2; exit 1; }
fi
cp ../art/generated/temperate_grass/v3_color_matched/grass_rgb.bin \
   "$DEST/temperate_grass_rgb.bin"
# The Enhanced textures, two packs per theater (build.sh made them): the cells at 144
# texels a cell and the join blend's strips. Windows takes every .pack from here, so both
# platforms ship them. The first set's packs (terrain30_<TH>.pack) go from a folder that
# still has them: the game would never read them, and the Windows zip would carry them.
rm -f "$DEST"/terrain30_*.pack
for th in TEMPERAT DESERT WINTER SNOW; do
    cp "../art/remaster/textures/$th/terrainenh_$th.pack" "$DEST/"
    cp "../art/remaster/textures/$th/terrainseam_$th.pack" "$DEST/"
    if [ -f "terrainsurface_$th.pack" ]; then cp "terrainsurface_$th.pack" "$DEST/"; fi
done
cp ../art/generated/temperate_cliffs/approved_style_v1/cliffs_rgb_color_matched.pack \
   "$DEST/temperate_cliffs_rgb.pack"
cp ../art/generated/temperate_cliffs/approved_style_v1/cliffs_height.pack \
   "$DEST/temperate_cliffs_height.pack"
cp ../art/generated/temperate_cliffs/approved_style_v1/cliffs_normal.pack \
   "$DEST/temperate_cliffs_normal.pack"
cp ../art/generated/temperate_roads/approved_style_v1/roads_rgb.pack \
   "$DEST/temperate_roads_rgb.pack"
cp ../art/generated/temperate_roads/approved_style_v1/roads_height.pack \
   "$DEST/temperate_roads_height.pack"
cp ../art/generated/temperate_roads/approved_style_v1/roads_normal.pack \
   "$DEST/temperate_roads_normal.pack"
cp ../art/generated/temperate_roads/approved_style_v1/roads_roughness.pack \
   "$DEST/temperate_roads_roughness.pack"
cp ../art/generated/temperate_shores/remastered_style_v1/shores_rgb.pack \
   "$DEST/temperate_shores_rgb.pack"
cp ../art/generated/temperate_shores/remastered_style_v1/shores_height.pack \
   "$DEST/temperate_shores_height.pack"
cp ../art/generated/temperate_shores/remastered_style_v1/shores_normal.pack \
   "$DEST/temperate_shores_normal.pack"
cp ../art/generated/temperate_bridges/remastered_style_v1/bridges_rgb.pack \
   "$DEST/temperate_bridges_rgb.pack"
cp ../art/generated/temperate_bridges/remastered_style_v1/bridges_height.pack \
   "$DEST/temperate_bridges_height.pack"
cp ../art/generated/temperate_bridges/remastered_style_v1/bridges_normal.pack \
   "$DEST/temperate_bridges_normal.pack"
cp ../art/generated/temperate_bridges/remastered_style_v1/bridges_roughness.pack \
   "$DEST/temperate_bridges_roughness.pack"
cp ../art/generated/temperate_rivers/remastered_style_v1/rivers_rgb.pack \
   "$DEST/temperate_rivers_rgb.pack"
cp ../art/generated/temperate_rivers/remastered_style_v1/rivers_height.pack \
   "$DEST/temperate_rivers_height.pack"
cp ../art/generated/temperate_rivers/remastered_style_v1/rivers_normal.pack \
   "$DEST/temperate_rivers_normal.pack"

# THE LAUNCHER LIVES AT THE PACKAGE ROOT, not inside OpenCNC3D.app, and that is on
# purpose. Put it in the bundle and it needs its own copy of the SDL libraries
# three directories away from the ones already here, or a rewritten load command
# that breaks the moment anyone moves the app. At the root it is an ordinary file
# beside the game: tools/bundle-sdl.sh below finds it by scanning the folder, and
# the binary-only update can name it. OpenCNC3D.app stays a tracked, binary-free
# wrapper whose script execs it, because binaries are kept out of git.
cp ../launcher/opencnc3d-launcher "$DEST/"
[ -x "$DEST/opencnc3d-launcher" ] || { echo "the launcher did not reach $DEST" >&2; exit 1; }

# And the bundle that opens it, so a freshly built playable/ is the shape a player
# gets rather than a folder with no way in. rsync -aL because the tracked master
# is the only copy and this must not follow it back.
rsync -aL --exclude '.DS_Store' "$REPO/tools/launchers/OpenCNC3D.app" "$DEST/" 2>/dev/null || true

# THE .app's OWN EXECUTABLE IS THE COMPILED ONE HERE TOO, not the tracked script. On an
# Apple Silicon Mac without Rosetta, macOS offers to install Rosetta for a bundle whose
# executable is a shell script, even when everything the script starts is native, and
# the game does not open. tools/mac/make-package-mac.sh has always swapped in
# launcher/opencnc3d-launch for the download; the play folder a developer double-clicks
# kept the script until this. Same binary, same ad-hoc signature as the package.
if [ -x ../launcher/opencnc3d-launch ]; then
  cp ../launcher/opencnc3d-launch "$DEST/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
  chmod +x "$DEST/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
  codesign --force -s - "$DEST/OpenCNC3D.app" 2>/dev/null \
    || echo "note: ad-hoc signing $DEST/OpenCNC3D.app failed" >&2
else
  echo "FAIL: launcher/opencnc3d-launch is missing (launcher/build.sh makes it); the play" >&2
  echo "      folder's OpenCNC3D.app would keep the script, which asks for Rosetta on Apple Silicon" >&2
  exit 1
fi

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

# THE MENU PACK IS BAKED HERE WHEN IT IS MISSING OR OLDER THAN WHAT IT IS MADE FROM.
# It holds the title screen with the logo, the 1995 fonts and the dialog frames, and it
# is a build product: a fresh checkout has none, and a pack left from an earlier bake
# would ship an earlier title. So it is baked from menu/art and the DOS data whenever
# either is newer, and a failed bake stops the build rather than shipping without it.
MENU_BAKE=0
[ -f ../menu/dosmenu.pack ] || MENU_BAKE=1
for f in ../menu/bake_dosmenu.py ../menu/art/*.png; do
    [ "$f" -nt ../menu/dosmenu.pack ] && MENU_BAKE=1
done
if [ "$MENU_BAKE" = 1 ]; then
    echo "   menu pack: baking menu/dosmenu.pack"
    python3 ../menu/bake_dosmenu.py || { echo "FATAL: the menu pack did not bake" >&2; exit 1; }
fi
cp ../menu/dosmenu.pack "$DEST/"
cp ../menu/enhanced-menu.pack "$DEST/"
# RED ALERT'S OWN ENHANCED KIT, baked from the 1996 data into its module (the Red Alert
# process opens ra3d/enhanced-menu.pack from the install root, never Tiberian Dawn's pack).
# Windows takes it with every other ra3d/*.pack (tools/ra3d/stage-runtime.py).
if [ -d "$DEST/ra3d" ] && [ "$OPENCNC3D_NO_RA" != "1" ]; then
    python3 ../menu/bake_ra_enhanced_menu.py "$DEST/ra3d/enhanced-menu.pack" >/dev/null \
        || { echo "FATAL: Red Alert's Enhanced menu pack did not bake" >&2; exit 1; }
    rm -f "$DEST/ra3d/enhanced-menu.json"
    cp ra-enhanced-hud.pack "$DEST/ra3d/enhanced-hud.pack"
fi

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
# are meant to live is still an open question, and not this script's to settle.
# game/build.sh preserves the playable CONQUER.MIX members and adds missing original
# sprite members from data/dosdata/CONQUER.MIX for DOS Classic.

# THE PACKS THE GAME ACTUALLY RUNS COME FROM THE BAKERY, and nothing
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
# AND THE SOLID TIBERIUM, for a different reason: the loop above DOES see game/, so a
# baked tib3d.pack travels -- but it is gitignored like every other pack, so a machine
# that has only ever pulled has never had one at all, and the feature is then simply
# absent with one line on stderr saying so. It reads only committed inputs
# (data/mods/original-tiberium-v4) and takes under a second, so bake it every time
# rather than making a fresh clone remember.
python3 bake_tib3d.py >/dev/null || {
    echo "make-build.sh: bake_tib3d.py failed; the Enhanced crystals would be absent" >&2; exit 1; }
# AND THE CARTRIDGE INFANTRY'S FIRING, PRONE AND CRAWLING STRIPS, the same way and for
# the same reason: game/bake_n64infantry.py reads them out of data/rom in well under a
# second. Without n64infantry.pack, soldiers drawn in the cartridge art stand still
# while they fight, with no muzzle flash, and one line on stderr is all that says so.
python3 bake_n64infantry.py >/dev/null || {
    echo "make-build.sh: bake_n64infantry.py failed; the cartridge infantry would not show firing" >&2; exit 1; }

# AND THE SKY, on the same reasoning and with one extra wrinkle. bake_sky.py turns the
# committed cross photograph under data/sky into the cube map and the nine harmonics that
# Enhanced lights every authored model by. Input and tool are both tracked and the bake is
# a couple of seconds, so by the rule that a build product with a command behind it is not
# committed, the PACK is not: it is made here every time.
#
# The wrinkle is that this one needs numpy and Pillow, which bake_tib3d.py does not, so
# the message names them. A build machine that has baked the trees already has both.
#
# IT REFUSES RATHER THAN WARNS, and that is the point. Without sky.pack the renderer falls
# back to the two-colour hemisphere it used before, which is a complete picture with no
# error in it -- so a shipped build missing this looks like a renderer whose metal went
# dull rather than like a build that lost a file.
python3 bake_sky.py "$REPO/data/sky/sky_cross.webp" -o sky.pack >/dev/null || {
    echo "make-build.sh: bake_sky.py failed; Enhanced would light every authored model" >&2
    echo "  from the flat two-colour fallback and say nothing about it." >&2
    echo "  It needs numpy and Pillow: python3 -m pip install numpy pillow" >&2; exit 1; }
[ -s sky.pack ] || { echo "make-build.sh: sky.pack came out empty" >&2; exit 1; }

# Imported trees are retired. Never re-stage a leftover legacy asset.
rm -f tree3d.pack ../playable/tree3d.pack ../playable/ra3d/tree3d.pack

# THE REMASTERED TIBERIUM travels the same way. tools/art/remaster/resources/run_TIB.sh
# builds and paints it in headless Blender and bake_candidate.py packs it, which is no step
# for every build, so the PACK is what is tracked and a build copies it. The game loads it
# under Enhanced in every theater (tib3d_mod.h, TIB3D_RINGS_PACK); without it the old
# tib3d.pack draws instead.
if [ -f "$REPO/data/tiberium/tib3d_rings.pack" ]; then
    cp "$REPO/data/tiberium/tib3d_rings.pack" tib3d_rings.pack
fi

# THE GROUND PATCHES travel for the same reason the trees do. bake_mottle.py builds the
# pack out of two hundred megabytes of source photographs that have no business in this
# repository, so the PACK is what is tracked and a build copies it. Without it the
# renderer looks for the file, does not find it, and draws plain ground, which is a
# correct picture that says nothing about why the patches are missing.
for patch in mottle.pack mottle_desert.pack; do
    [ ! -f "$REPO/data/mottle/$patch" ] || cp "$REPO/data/mottle/$patch" "$patch"
done

# THE SCATTERED BUSHES AND GRASS CLUMPS travel for the same reason as well.
# tools/art/foliage/bake_foliage3d.py builds the pack from a Unity Asset Store package
# that needs Unity to read, so the PACK is what is tracked and a build copies it. Without
# it the experimental art draws no bushes and says so once in the log.
if [ -f "$REPO/data/foliage/foliage3d.pack" ]; then
    cp "$REPO/data/foliage/foliage3d.pack" foliage3d.pack
fi
# THE ROCKS, the same way: rock3d.pack is baked from a Unity Asset Store package by
# tools/art/rocks/bake_rock3d.py and committed; a build copies it.
if [ -f "$REPO/data/rocks/rock3d.pack" ]; then
    cp "$REPO/data/rocks/rock3d.pack" rock3d.pack
fi
# THE PAINTED STONES, the switch. tools/art/rocks/build_stones.py makes the fifteen stones
# as angular hand-painted rocks in the remaster set's method, one pack per theater
# (rock3d_DESERT/TEMPERAT/WINTER.pack), and the game reads a theater's pack in place of
# rock3d.pack when the play folder has it. ROCK_STONES_DEFAULT is the one flag: "painted"
# bakes the theater packs into the folder, "unity" keeps the experimental stones (and
# removes any painted packs an earlier build left, so a folder never mixes the two).
# tools/art/rocks/stones.sh does the work and reads this line; the Windows package calls
# the same script, so both platforms carry the same stones. ROCK_STONES=unity in the
# environment builds one folder with the old stones, for a comparison.
ROCK_STONES_DEFAULT=painted
sh "$REPO/tools/art/rocks/stones.sh" "$DEST" "${ROCK_STONES:-$ROCK_STONES_DEFAULT}"
python3 "$REPO/tools/art/terrain/stage_desert_details.py" "$DEST"

# THE AUTHORED VEHICLE MODELS travel for the same reason the trees do, and are committed
# for the same reason: tools/art/blender_c3m.py rebuilds a .c3m out of a Blender file
# that is a hundred megabytes and has no business in this repository, so the PRODUCT is
# what is tracked and a build copies it. Without this a release ships a renderer that
# looks for models3d/, finds nothing, and draws the cartridge's vehicles -- which is a
# correct picture and the wrong one, and says nothing at all about why.
if [ -d "$REPO/game/models3d" ]; then
    mkdir -p "$DEST/models3d"
    for m in "$REPO"/game/models3d/*.c3m; do
        [ -f "$m" ] && cp "$m" "$DEST/models3d/"
    done
fi
# THE FIXED CARTRIDGE BUILDINGS (n64fix_mod.h), Enhanced only, travel the same way and
# for the same reason: tools/art/n64fix/bake_enhanced.py needs the Blender exports, so
# the baked game/n64fix/*.n64x are tracked and a build copies them. The folder is
# REPLACED rather than added to, because the renderer finds a model by name: a file
# left behind from a type whose approval was withdrawn would go on drawing it.
if [ -d "$REPO/game/n64fix" ]; then
    rm -rf "$DEST/n64fix"
    mkdir -p "$DEST/n64fix"
    for m in "$REPO"/game/n64fix/*.n64x; do
        [ -f "$m" ] && cp "$m" "$DEST/n64fix/"
    done
fi
# THE ENHANCED BUILDINGS (ebld_mod.h), Enhanced only, travel the same way and for the
# same reason: tools/art/remaster/bake_ebld.py reads a viewer export that only Blender
# can produce, so the baked game/ebld/<TYPE>.ebld files are tracked and a build copies
# them. REPLACED rather than added to, as n64fix is: the renderer finds a building's file
# by its type name, so a file left behind from a withdrawn type would go on drawing and
# breaking that building. Missing, Enhanced draws the fixed building and the old shatter,
# which is a correct picture and the wrong one; check_asset below refuses that folder.
if [ -d "$REPO/game/ebld" ]; then
    rm -rf "$DEST/ebld"
    mkdir -p "$DEST/ebld"
    for m in "$REPO"/game/ebld/*.ebld "$REPO"/game/ebld/*.ebld.fclv "$REPO"/game/ebld/*.spec; do
        [ -f "$m" ] && cp "$m" "$DEST/ebld/"
    done
fi
# THE PLACEMENTS (ebld_place.h), Enhanced only, beside them and the same way:
# tools/art/remaster/bake_eplc.py reads the lab's placements, made with Blender's parts, so
# the baked game/eplc/<TYPE>.eplc files are tracked, and the folder is REPLACED so a
# withdrawn type's file cannot go on moving it. Missing, a remastered structure goes up by
# the cartridge's reveal; check_asset below refuses that folder.
if [ -d "$REPO/game/eplc" ]; then
    rm -rf "$DEST/eplc"
    mkdir -p "$DEST/eplc"
    for m in "$REPO"/game/eplc/*.eplc; do
        [ -f "$m" ] && cp "$m" "$DEST/eplc/"
    done
fi
# THE REMASTERED VEHICLES AND AIRCRAFT (evhc_mod.h), Enhanced only, the same way and for
# the same reasons: tools/art/remaster/bake_evhc.py reads a viewer export only Blender can
# produce, so the baked game/evhc/<TYPE>.evhc files are tracked, and the folder is REPLACED
# so a withdrawn type's file cannot go on drawing it. Missing, Enhanced draws the
# cartridge's vehicles and their old shatter; check_asset below refuses that folder.
if [ -d "$REPO/game/evhc" ]; then
    rm -rf "$DEST/evhc"
    mkdir -p "$DEST/evhc"
    for m in "$REPO"/game/evhc/*.evhc "$REPO"/game/evhc/*.spec; do
        [ -f "$m" ] && cp "$m" "$DEST/evhc/"
    done
fi
# THE REMASTERED SOLDIERS (einf/<TYPE>.c3m, model3d_mod.h), Enhanced only, travel the same
# way and for the same reason: tools/art/remaster/infantry/ makes them with Blender, so
# the product is tracked and a build copies it. REPLACED rather than added to, as ebld is:
# the renderer finds a soldier's file by type name. Missing, Enhanced draws the 1995
# sprites; check_asset below refuses that folder.
if [ -d "$REPO/game/einf" ]; then
    rm -rf "$DEST/einf"
    mkdir -p "$DEST/einf"
    for m in "$REPO"/game/einf/*.c3m; do
        [ -f "$m" ] && cp "$m" "$DEST/einf/"
    done
fi
# THE REMASTERED CURSORS (ecur/ecur.bin, cursor3d_mod.h), Enhanced only, travel the same
# way and for the same reason: tools/art/remaster/cursors/bake_ecur.py bakes them from the
# approved lab models, which need Blender and the cartridge extraction, so the product is
# tracked and a build copies it. Missing, Enhanced draws the cartridge's cursors;
# check_asset below refuses that.
if [ -f "$REPO/game/ecur/ecur.bin" ]; then
    rm -rf "$DEST/ecur"
    mkdir -p "$DEST/ecur"
    cp "$REPO/game/ecur/ecur.bin" "$DEST/ecur/"
fi
# THE UNITY EXPLOSION (unityvfx_mod.h) travels the same way and for the same reason:
# game/vfx3d/<effect>/ is the product of tools/art/unity_vfx/, which needs Unity and the
# Asset Store package, so the product is tracked and a build copies it. Missing, the
# renderer says so once at start and draws the cartridge's explosions.
if [ -d "$REPO/game/vfx3d" ]; then
    mkdir -p "$DEST/vfx3d"
    rsync -a "$REPO/game/vfx3d/" "$DEST/vfx3d/"
fi
# bake_pack.py writes into the REPO's playable, which is not necessarily $DEST.
if [ "$REPO/playable/hud640.pack" != "$DEST/hud640.pack" ]; then
    cp "$REPO/playable/hud640.pack" "$DEST/hud640.pack"
fi

# THE GENERATED HEIGHTMAPS. A scenario the cartridge left flat (the Covert Operations
# missions first) gets a heightmap generated from its own tiles by tools/autoheight, and
# the reviewed result is committed as data/autoheight/<SCEN>.HGT. bake5.py reads it on
# its own, but nothing re-bakes a scenario pack during a build, so a heightmap committed
# after the pack was baked would never reach the game. Re-bake any such pack that is
# older than its heightmap (a few seconds each; the loop below then stages it), and
# afterwards REFUSE if a staged pack does not carry exactly the committed heights:
# without this, a map would play flat with nothing on screen to say so.
mkdir -p ../tools/bakery/game
for HGT in "$REPO"/data/autoheight/*.HGT; do
    [ -f "$HGT" ] || continue
    S=$(basename "$HGT" .HGT)
    P="../tools/bakery/game/$S.pack"
    if [ ! -f "$P" ] || [ "$HGT" -nt "$P" ]; then
        # bake5.py reads the scenario's resolved terrain, terrain_<SCEN>.json, which is a
        # generated intermediate and not committed: a fresh checkout has none. n64_terrain.py
        # makes it from the committed .MAP and .INI in well under a second, so make it here
        # when it is missing rather than relying on a checkout that happens to hold one.
        TJ="../tools/bakery/sharecopy/assets/terrain/terrain_$S.json"
        if [ ! -f "$TJ" ]; then
            echo "auto heightmap: resolving $S terrain"
            python3 ../tools/bakery/sharecopy/assets/terrain/n64_terrain.py "$S" \
                > "../tools/bakery/game/$S.terrain.log" 2>&1 && [ -f "$TJ" ] || {
                cat "../tools/bakery/game/$S.terrain.log" >&2
                echo "make-build.sh: n64_terrain.py $S failed; $S could not be baked" >&2; exit 1; }
        fi
        echo "auto heightmap: baking $S"
        python3 ../tools/bakery/bake5.py "$S" > "../tools/bakery/game/$S.bake.log" 2>&1 || {
            cat "../tools/bakery/game/$S.bake.log" >&2
            echo "make-build.sh: bake5.py $S failed; its generated heightmap would not ship" >&2; exit 1; }
    fi
done

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
python3 "$REPO/tools/autoheight/check_packs.py" "$DEST" || {
    echo "make-build.sh: a scenario pack does not carry its generated heightmap" >&2; exit 1; }

# THE REMASTERED EFFECTS (Enhanced): efx_enh.pack, baked every build from the committed
# sources in art/remaster/fx by tools/art/remaster/fx/make_fx.py, on top of the cartridge's
# own efx.pack (which bake_efx.py makes from the ROM, and which must therefore already be
# in $DEST). It refuses rather than warns, for the reason sky.pack does: without the pack
# Enhanced quietly draws the cartridge's effects, a complete picture with nothing wrong
# in it. The Windows build copies every pack in $DEST, so this covers both.
[ -f "$DEST/efx.pack" ] || { echo "make-build.sh: no $DEST/efx.pack to build the remastered effects on (game/bake_efx.py)" >&2; exit 1; }
python3 "$REPO/tools/art/remaster/fx/make_fx.py" --cart "$DEST/efx.pack" --pack "$DEST/efx_enh.pack" >/dev/null || {
    echo "make-build.sh: make_fx.py failed; Enhanced would draw the cartridge's effects" >&2
    echo "  and say nothing about it. It needs numpy and Pillow." >&2; exit 1; }
[ -s "$DEST/efx_enh.pack" ] || { echo "make-build.sh: efx_enh.pack came out empty" >&2; exit 1; }

# THE REMASTERED BUILDING APRONS (Enhanced): bibenh.pack, baked every build from the
# committed paintings in art/remaster/bib and the cartridge's own apron silhouettes by
# tools/art/remaster/bib/bake_bibenh.py. Refused rather than warned for the same reason:
# without it Enhanced quietly draws the cartridge's aprons. Windows copies it with every
# other pack in $DEST.
python3 "$REPO/tools/art/remaster/bib/bake_bibenh.py" --pack "$DEST/bibenh.pack" >/dev/null || {
    echo "make-build.sh: bake_bibenh.py failed; Enhanced would draw the cartridge's aprons" >&2
    echo "  and say nothing about it. It needs numpy, scipy and Pillow." >&2; exit 1; }
[ -s "$DEST/bibenh.pack" ] || { echo "make-build.sh: bibenh.pack came out empty" >&2; exit 1; }

# THE REMASTERED CRATERS AND SCORCH MARKS (Enhanced): decalenh.pack, baked every build from
# the committed finals in art/remaster/decals/final by
# tools/art/remaster/decals/build-runtime.py. Every authored layer must be present and the
# complete pack verified before atomic publication. Without it Enhanced quietly draws
# the cartridge's. Windows copies it with every other pack.
python3 "$REPO/tools/art/remaster/decals/build-runtime.py" --pack "$DEST/decalenh.pack" >/dev/null || {
    echo "make-build.sh: approved decal build failed; Enhanced would draw the cartridge's craters" >&2
    echo "  and scorch marks and say nothing about it. It needs numpy and Pillow." >&2; exit 1; }
[ -s "$DEST/decalenh.pack" ] || { echo "make-build.sh: decalenh.pack came out empty" >&2; exit 1; }

rsync -a missions "$DEST/" 2>/dev/null || true

# THE BRAIN TRAVELS WITH THE BUILD TOO, and it did not.
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

# THE GATE SUITE TRAVELS WITH THE BUILD, and it did not.
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
# THE REST OF THE GATE BINARIES game/build.sh makes. It stages them into the checkout's own
# playable/, which is this folder only when the build is run from that checkout; built from
# another worktree, the copies here stayed whatever an older build left, and after the move
# to universal binaries that meant Intel-only helpers beside a universal game. Not asserted
# one by one: the architecture check below refuses any Mach-O here that is missing a slice.
for b in gate_tunnel gate_beacon gate_lobby gate_roomcode gate_mpbrowse gate_enhanced_menu cnc_twobrain; do
  [ -x "$b" ] && cp "$b" "$DEST/" && g=$((g+1))
done
# TWO OF THOSE LINK SDL (gate_enhanced_menu, gate_mpbrowse), and they arrive AFTER the
# folder's SDL was fixed above. game/build.sh fixes them as it links them, but the copy in
# game/ is whatever the last build there left: one that stopped partway, or one still
# compiling in the same checkout, leaves them loading SDL through an @rpath nothing
# resolves, and the check at the end of this script then refuses the whole folder. So the
# folder is fixed once more here. It is idempotent: a binary already pointing at
# @executable_path is left alone.
sh ../tools/bundle-sdl.sh "$DEST"
# fx_state.h is a SOURCE HEADER in a folder that otherwise holds a build, and it is here
# for the same reason gate_optlayout is: a gate reads it, and a gate input that is quietly
# absent is worse than one that is loudly wrong. G259 is a static read of the dial table
# rather than a render -- it proves no two rows drive the same setting, which no picture
# can show -- so the table itself is its input. It resolves that file beside gates.sh, and
# gates.sh runs from THIS folder whenever a release is cut, so without this line the gate
# cannot pass in the one run that decides whether a build ships.
cp fx_state.h "$DEST/" 2>/dev/null || true
[ -f "$DEST/fx_state.h" ] || { echo "FAIL: fx_state.h did not reach $DEST (G259 reads the dial table out of it)" >&2; exit 1; }
g=$((g+1))
echo "gates: $g files copied to $DEST"

# Enhanced environment, cinematic and HUD WAVs. Local folder points at the reproducible
# import; package builders dereference it into a portable folder. A play folder that holds a
# real copy instead (cloned from another folder, which dereferences the link) is refreshed
# from the import on every build: left alone it kept the sounds it was first given, and
# played the placement's old generated impact and rumble after recordings replaced them.
media_dir() {
  [ -d "../data/$1" ] || return 0
  if [ -L "$DEST/$1" ]; then return 0; fi
  if [ -d "$DEST/$1" ]; then rsync -a --delete --exclude '.DS_Store' "../data/$1/" "$DEST/$1/"
  else ln -s "$(cd "../data/$1" && pwd)" "$DEST/$1"; fi
}
media_dir ambience
media_dir cinematic
media_dir foley

# THE SOUND DATA. Without dosdata/ beside the binary the build boots, plays and is
# completely silent, and the only thing that says so is one line on stderr. The
# archives are large (SCORES.MIX alone is 54 MB), so this is a symlink into the repo's
# data/dosdata rather than a copy; the packages substitute a real copy.
#
# THIS FOLDER KEEPS THE MOVIES AND THE MUSIC, all of data/dosdata through the link, and it
# must: the gates that play a movie or measure the score run here. The packages are the
# ones without them (tools/mac/make-package-mac.sh and the Windows packager leave out the
# paths in tools/media/media-paths.txt), and gate G316 plays the campaign in a folder made
# from this one with those paths taken away.
if [ ! -e "$DEST/dosdata" ] && [ -d ../data/dosdata ]; then
  ln -s "$(cd ../data/dosdata && pwd)" "$DEST/dosdata"
  echo "linked dosdata -> $DEST/dosdata"
fi
# THE NEW AUDIO replacement sounds, linked for the same reason: the folder is the
# repo's own, and the release package copies it for real.
if [ ! -e "$DEST/newaudio" ] && [ -d ../data/newaudio ]; then
  ln -s "$(cd ../data/newaudio && pwd)" "$DEST/newaudio"
  echo "linked newaudio -> $DEST/newaudio"
fi
# EVERY MACH-O IN THE FOLDER DECLARES A macOS FLOOR THIS OLD OR OLDER. Measured 20 Aug
# 2026: opencnc3d, cnc_eyes AND TiberianDawn.dylib all carried LC_BUILD_VERSION minos 26.0,
# because no build script set a deployment target and clang stamps whatever SDK it finds.
# dyld REFUSES a Mach-O whose minos is newer than the running system, so every Mac below
# macOS 26 rejected the release before main() ran -- on top of, and hiding behind, the
# SDL bundling that was supposed to make a release playable elsewhere.
#
# WHAT IS CHECKED: opencnc3d, cnc_eyes, netcheck and every .dylib -- the binaries by name and
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
for p in "$DEST/opencnc3d" "$DEST/cnc_eyes" "$DEST/netcheck" "$DEST"/*.dylib; do
  [ -f "$p" ] || continue
  # EVERY SLICE, not the first: a universal file carries one load command per
  # architecture, and plain otool -l reads only the first, so an arm64 slice asking for
  # a newer macOS would pass behind a compliant x86_64 one. The highest floor is kept.
  MIN=""
  for a in $(lipo -archs "$p" 2>/dev/null); do
    m=$(otool -arch "$a" -l "$p" 2>/dev/null \
        | awk '/LC_BUILD_VERSION|LC_VERSION_MIN_MACOSX/{f=1} f && /^ *(minos|version) /{print $2; exit}')
    [ -n "$m" ] || { MIN=""; break; }
    if [ -z "$MIN" ] || awk -v a="$m" -v b="$MIN" 'BEGIN{
          split(a,x,"."); split(b,y,".");
          exit ((x[1]*10000+x[2]*100+x[3]) > (y[1]*10000+y[2]*100+y[3])) ? 0 : 1 }'; then
      MIN=$m
    fi
  done
  if [ -z "$MIN" ]; then
    echo "FAIL: $(basename "$p") declares no macOS floor at all (no LC_BUILD_VERSION)." >&2
    floorbad=1; continue
  fi
  awk -v a="$MIN" -v b="$OPENCNC3D_MACOS_MIN" 'BEGIN{
        split(a,x,"."); split(b,y,".");
        exit ((x[1]*10000+x[2]*100+x[3]) <= (y[1]*10000+y[2]*100+y[3])) ? 0 : 1 }' \
    || { echo "FAIL: $(basename "$p") declares macOS $MIN, above the $OPENCNC3D_MACOS_MIN floor." >&2
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
echo "macOS floor: opencnc3d, cnc_eyes, netcheck and every .dylib in $DEST are $OPENCNC3D_MACOS_MIN or older"

# BOTH CPUS, EVERY BINARY. One Mac package runs on Apple Silicon and on Intel only if every
# Mach-O in it carries both slices (tools/mac/deployment-target.sh). The process is one
# architecture throughout, so a single thin file -- an SDL library left over from an older
# build, a brain built before the switch -- stops the game on the other kind of Mac in
# dyld, and on the build machine nothing is wrong at all. The .app's own executable is
# checked here only when it is a Mach-O: the play folder carries the script version, and
# the package swaps in the compiled one (tools/mac/make-package-mac.sh checks that).
#
# What this build puts here must be universal, and fails the build if not. Any OTHER Mach-O
# in the folder (a binary from an experiment that no build makes any more) is only named:
# it is not shipped, the package copies files by name, but it will not run on the Mac
# whose CPU it lacks.
archbad=0
for p in "$DEST"/* "$DEST/OpenCNC3D.app/Contents/MacOS"/*; do
  [ -f "$p" ] || continue
  case "$(file -b "$p")" in *Mach-O*) ;; *) continue ;; esac
  # AND IT CAN FIND ITS SDL. A binary copied in after tools/bundle-sdl.sh ran above keeps
  # the @rpath load command a CMake-built SDL records; with no LC_RPATH that resolves to
  # nothing and the binary dies in dyld (exit 134) before main.
  if otool -L "$p" 2>/dev/null | awk 'NR>1 && /^[ \t]/ { print $1 }' | grep -q '^@rpath/libSDL2'; then
    echo "FAIL: $(basename "$p") loads SDL through @rpath and has no rpath: tools/bundle-sdl.sh never fixed it." >&2
    archbad=1
  fi
  # shellcheck disable=SC2086
  lipo "$p" -verify_arch $OPENCNC3D_MAC_ARCHS 2>/dev/null && continue
  case "$(basename "$p")" in
    opencnc3d|cnc_eyes|opencnc3d-launcher|netcheck|cnc_twobrain|gate_*|*.dylib|opencnc3d-launch)
      echo "FAIL: $(basename "$p") is $(lipo -archs "$p" 2>/dev/null), not $OPENCNC3D_MAC_ARCHS." >&2
      archbad=1 ;;
    *)
      echo "note: $(basename "$p") in $DEST is $(lipo -archs "$p" 2>/dev/null) only; no build makes it" ;;
  esac
done
[ "$archbad" = "0" ] || {
  echo "" >&2
  echo "A file above would stop the game on the Mac whose CPU it lacks. Rebuild it through" >&2
  echo "game/build.sh, app/build.sh or tools/mac/build-brain-mac.sh; an SDL library is" >&2
  echo "replaced by tools/bundle-sdl.sh from the universal SDL tools/mac/setup-sdl-mac.sh makes." >&2
  exit 1; }
echo "architectures: every Mach-O this build puts in $DEST is $OPENCNC3D_MAC_ARCHS"

# A COOKED PACKAGE CARRIES NO F5 PANEL. Same check as tools/win/make-build-win.sh, for
# the same fail-open case: a cook that was asked for and not compiled in.
#
# READ OFF THE BINARIES, NOT THE FLAGS. Each one carries the build mark from
# game/opencnc3d_build.h (tools/version.sh writes it), and it ends "|cooked" only when
# OPENCNC3D_COOKED reached that compile. It names the build too, so a binary left over
# from another build fails here as well: the stamp has to be the one in the header now.
if [ -n "$OPENCNC3D_COOKED" ]; then
  STAMP=$(sed -n 's/^#define OPENCNC3D_BUILD "\(.*\)"$/\1/p' opencnc3d_build.h 2>/dev/null)
  [ -n "$STAMP" ] || { echo "FAIL: game/opencnc3d_build.h names no build" >&2; exit 1; }
  for b in cnc_eyes opencnc3d; do
    strings -a "$DEST/$b" 2>/dev/null | grep -qxF "OPENCNC3D|build|$STAMP|cooked" || {
      echo "FAIL: $b was packaged as a cooked build of $STAMP but does not say so. Its" >&2
      echo "build mark is: $(strings -a "$DEST/$b" 2>/dev/null | grep '^OPENCNC3D|build|' | sort -u | tr '\n' ' ')" >&2
      echo "A missing |cooked means it was compiled without OPENCNC3D_COOKED and still has" >&2
      echo "the F5 panel. Rebuild with the packager, not by hand." >&2
      exit 1; }
  done
  echo "cooked: cnc_eyes and opencnc3d are $STAMP, compiled with no F5 panel"
fi

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
check_asset content            "the theater archives (TEMPERAT.MIX, DESERT.MIX); game/build.sh stages them"
check_asset content/TEMPERAT.MIX "original temperate scenery; staged by game/build.sh"
check_asset content/DESERT.MIX   "original desert scenery; staged by game/build.sh"
check_asset dosclassic.pack    "game/bake_dosclassic.py"
check_asset CONQUER.MIX        "the playable archive with original DOS sprites merged by game/build.sh"
check_asset dossidebar.pack    "game/bake_dossidebar.py   (the game REFUSES to draw a sidebar without it)"
check_asset cameos.pack        "game/bake_cameos.py"
check_asset dosinfantry.pack   "game/bake_dosinfantry.py"
check_asset n64infantry.pack   "game/bake_n64infantry.py   (the cartridge infantry's firing, prone and crawling frames)"
check_asset dosmake.pack       "game/bake_dosmake.py"
check_asset campaign.pack      "game/bake_campaign.py"
check_asset efx.pack           "game/bake_efx.py"
check_asset efx_enh.pack       "tools/art/remaster/fx/make_fx.py   (Enhanced remastered effects; without it Enhanced draws the cartridge's)"
check_asset dosmenu.pack       "menu/, copied above"
check_asset verdict.pack       "game/bake_verdict.py"
check_asset dostib.pack        "game/bake_dostiberium.py"
check_asset tib3d.pack         "game/bake_tib3d.py       (Enhanced solid crystals; without it the flat overlay draws alone)"
check_asset tib3d_rings.pack   "data/tiberium/tib3d_rings.pack, copied above (the remastered Tiberium, Enhanced; without it the old tib3d.pack draws)"
check_asset foliage3d.pack     "data/foliage/foliage3d.pack   (experimental art bushes and grass clumps; without it none are scattered)"
check_asset rock3d.pack        "data/rocks/rock3d.pack   (the rocks at the foot of rock faces on a theater without a painted pack; without it none are laid there)"
check_asset ebld               "game/ebld/*.ebld, copied above; tools/art/remaster/bake_ebld.py writes them (the Enhanced Power Plant; without it Enhanced draws the fixed building and the old shatter)"
check_asset eplc               "game/eplc/*.eplc, copied above; tools/art/remaster/bake_eplc.py writes them (the remastered structures going up part by part; without it Enhanced shows the cartridge's reveal)"
check_asset evhc               "game/evhc/*.evhc, copied above; tools/art/remaster/bake_evhc.py writes them (the remastered vehicles and aircraft; without it Enhanced draws the cartridge's)"
check_asset einf               "game/einf/*.c3m, copied above; tools/art/remaster/infantry/soldier_c3m.py writes them (the remastered Minigunner; without it Enhanced draws the 1995 infantry sprites)"
check_asset ecur/ecur.bin      "game/ecur/ecur.bin, copied above; tools/art/remaster/cursors/bake_ecur.py writes it (the remastered cursors; without it Enhanced draws the cartridge's)"
check_asset smudge.pack        "game/bake_smudges.py"
check_asset bibenh.pack        "tools/art/remaster/bib/bake_bibenh.py   (Enhanced remastered building aprons; without it Enhanced draws the cartridge's)"
check_asset decalenh.pack      "tools/art/remaster/decals/bake_decalenh.py   (Enhanced remastered craters and scorch marks; without it Enhanced draws the cartridge's)"
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
  echo "report weeks later. Each line above names the command that makes the file." >&2
  echo "" >&2
  echo "Do NOT satisfy these by copying from data/dist-v0.3.1. It is a v0.3.1 snapshot," >&2
  echo "stale by several releases: a folder filled from it passes this check and then" >&2
  echo "fails seven gates that all appear to blame the renderer." >&2
  exit 1
fi

# SKIRMISH NEEDS A PAIR PER MAP, and nothing above can see that. Every name in the list
# above is a fixed auxiliary pack; not one scenario pack is among them, and the missions
# copy is "|| true", so a folder can pass every check written so far with the SCM mission
# INIs present and no SCM packs at all. skirmish_scan in app/opencnc3d.cpp admits a map only
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

# Shared sun-driven terrain detail packs use the same key contract in TD and RA.
for surface in terrainsurface_*.pack *_SAND.pack; do
    [ -f "$surface" ] && cp "$surface" "$DEST/"
done
# The RA module consumes Snow's accepted clear material and its native-outline
# coverage from the install root. Fresh clean builds must stage both; an older
# Dock folder containing them must not be an implicit dependency.
for surface in clearblend_RA_SNOW.pack groundmask_RA_SNOW.pack; do
    [ -s "$surface" ] || { echo "missing current-source RA ground pack: $surface" >&2; exit 1; }
    cp "$surface" "$DEST/"
done

# Final material data follows every albedo, physical surface and scatter replacement.
python3 "$REPO/tools/art/terrain/stage_desert_details.py" "$DEST"
python3 "$REPO/tools/art/rocks/stage_stone_materials.py" "$DEST"

# A byte-identical restored Dock stub still cannot launch if its execute bits
# were lost. Check the actual bundle entry points after every staging operation.
python3 "$REPO/tools/mac/gate-dock-launchers.py" "$DEST" --repair >/dev/null
