#!/bin/sh
# Assemble the macOS release folder: the game, and nothing else.
#
#   tools/mac/make-package-mac.sh <destdir>
#   OPENCNC3D_PLAYABLE=/some/playable tools/mac/make-package-mac.sh <destdir>
#   OPENCNC3D_NO_RA=1 tools/mac/make-package-mac.sh <destdir>     with no Red Alert in it
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
SRC="${OPENCNC3D_PLAYABLE:-$ROOT/playable}"
[ -d "$SRC" ] || { echo "no build folder at $SRC. Run game/make-build.sh first." >&2; exit 1; }

die() { printf '\nPACKAGING REFUSED: %s\n' "$*" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd -P)
printf '%s\n' 'OpenCNC3D fresh public package v1' > "$OUT/.opencnc3d-public-stage"

# A required entry is copied with need_*, an optional one with the [ -f ] && cp form.
# The difference matters: a missing SOUNDS.MIX has to stop the release, while a missing
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
    #
    # AND user_maps, WHICH IS SOMEBODY'S PERSONAL FOLDER AND NOT CONTENT. The editor saves
    # every map into playable/missions/user_maps -- including an autosave every two minutes
    # that nobody pressed -- and the gate suite leaves scratch maps there too. This rsync is
    # recursive, so all of it shipped: v0.6.6 carried 61 files of one person's own maps to
    # everybody who downloaded it. The two tracked worked examples are put back by name
    # below, because they ARE content and the exclusion would otherwise take them with it.
    #
    # A second argument names an rsync exclude file, which is how dosdata leaves its
    # movies and music behind (see THE MOVIES AND THE MUSIC below).
    if [ -n "$2" ]; then
        rsync -aL --exclude '.DS_Store' --exclude 'user_maps/' --exclude-from="$2" \
            "$SRC/$1" "$OUT/"
    else
        rsync -aL --exclude '.DS_Store' --exclude 'user_maps/' "$SRC/$1" "$OUT/"
    fi
}

# THE MOVIES AND THE MUSIC DO NOT SHIP. The 1995 cutscenes and the score are not in any
# package: the Windows installer and the launcher's Setup screen fetch them from the disc
# images on archive.org (the movies from the 1995 Gold discs, EA's 2007 freeware release;
# the music from the Covert Operations disc image), and only when the player leaves their
# boxes ticked. A folder without them plays cleanly; the game finds both by file presence
# and reads no setting.
#
# THE LIST IS ONE FILE, tools/media/media-paths.txt, so this packager, the Windows
# packager, the data fingerprint and the release's check of the zips all leave out the
# same paths: one path per line relative to the package root, a trailing "/" for a folder,
# '#' for a comment, and the leading "/" optional, read the same way by all four. It is
# turned here into rsync anchors, each line given its "/" whether it had one or not. The
# Red Alert module's movies and score are on it too: tools/ra3d/stage-runtime.py reads the
# same file and stages the module without them, and they are refused at the end like the
# rest.
# The copy below is the same list, used only when that file is absent, so a checkout
# without it still cannot ship the media.
MEDIA_LIST="$ROOT/tools/media/media-paths.txt"
MEDIA_EXCLUDE=$(mktemp)
trap 'rm -f "$MEDIA_EXCLUDE"' EXIT
if [ -f "$MEDIA_LIST" ]; then
    sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
        -e 's|^\./||' -e 's|^/||' "$MEDIA_LIST" | grep -v '^$' | sed 's|^|/|' > "$MEDIA_EXCLUDE" || true
else
    printf '%s\n' /dosdata/movies/ /dosdata/music/ /dosdata/SCORES.MIX \
        /dosdata/TRANSIT.MIX /dosdata/transit/TRANSIT.MIX /movies/ \
        /ra3d/movies/ /ra3d/audio/SCORES.MIX /ra3d/data/SCORES.MIX > "$MEDIA_EXCLUDE"
fi
[ -s "$MEDIA_EXCLUDE" ] || die "the media list $MEDIA_LIST is empty, so nothing would keep
       the movies and music out of the package."

# ---- the program ------------------------------------------------------------------
# Two binaries, not one. opencnc3d is the app (menu, campaign, movies, and the Tier 2 chain
# on at startup); cnc_eyes is the renderer run directly, straight into one mission with
# the chain off.
#
# TEN of the package's thirteen .command launchers exec cnc_eyes, not four: PLAY-GDI,
# PLAY-NOD, PLAY-HUD-NEW, PLAY-HUD-OLD, PLAY-HUD-NOD, PLAY-BUILD-TEST, PLAY-CAMERA-AB,
# TUNE-SHADOWS, TUNE-EXPLOSIONS, TEST-MUZZLE-FLASH. Only PLAY, PLAY-HUD-MENU and
# TUNE-PRESENTATION run opencnc3d, and so does OpenCNC3D.app. Shipping opencnc3d alone would leave
# ten of the fourteen double-click entry points dead. Counted over the union this script
# actually copies below, which is tools/launchers/*.command plus $SRC/*.command.
#
# The READ-ME names both binaries and splits its launcher list by which one each runs,
# because that is also what decides whether the picture comes up enhanced or classic.
# See tools/launchers/README.md.
need_file opencnc3d
need_file cnc_eyes
# RED ALERT is staged only when the build is not told to leave it out: OPENCNC3D_NO_RA=1,
# which tools/release.sh sets, packages no ra3d/ even when the play folder holds one, and
# the package is searched for any piece of it at the end. A module that IS staged comes
# without its movies and score, which stage-runtime.py leaves out by the media list, and
# plays without them as Tiberian Dawn does; the media check at the end still asks.
if [ "$OPENCNC3D_NO_RA" = "1" ]; then
    echo "   Red Alert: left out (OPENCNC3D_NO_RA=1)"
elif [ -d "$SRC/ra3d" ]; then
    python3 "$ROOT/tools/ra3d/stage-runtime.py" --source "$SRC/ra3d" \
        --out "$OUT/ra3d" --app "$SRC/opencnc3d" --brain "$SRC/ra3d/RedAlert.dylib"
fi
need_file dosclassic.pack

# THE CONNECTIVITY TOOL. It has no game in it: it opens one UDP socket, runs three hundred
# lockstep turns against the other end and prints a digest of the orders both sides
# executed. Identical digests mean the two ends can lockstep; different ones mean they
# cannot, and say so before anybody blames the match for it.
#
# It ships rather than staying a build-tree tool because the person who needs it is the
# person whose game will not start, and that person has a zip rather than a compiler. It
# is 40 KB and it links nothing the package does not already carry.
need_file netcheck

# THE LAUNCHER. OpenCNC3D.app's script execs this; without it the bundle falls back to
# running the game directly, which works but silently ships a package with no way
# to see the version, read the changelog or take an update. That fallback is there
# so a Mac can always play, not so a release can quietly lose its front door, so
# the packager refuses instead of relying on it.
need_file opencnc3d-launcher

# EVERY .dylib, BY GLOB, not by name -- and the reason the glob is not a name list is
# sitting in the folder. Today it matches THREE:
#
#   TiberianDawn.dylib     the GPL engine
#   libSDL2-2.0.0.dylib    bundled by tools/bundle-sdl.sh, which also rewrites the load
#                          command, so `otool -L playable/opencnc3d` now reads
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
# Required authored decals come from this source, even if SRC is an older installation.
python3 "$ROOT/tools/art/remaster/decals/build-runtime.py" --pack "$OUT/decalenh.pack" >/dev/null
[ -s "$OUT/decalenh.pack" ] || die "the approved Enhanced decal pack is missing or empty."
_n=0
for f in "$SRC"/*.pack; do
    [ "${f##*/}" != decalenh.pack ] || continue
    if [ "$OPENCNC3D_NO_RA" = "1" ]; then
        # This one painting is shared by TD's sea despite its historic filename.
        case "${f##*/}" in waterbase_RA_TEMPERAT.pack) ;; *_RA_*) continue ;; esac
    fi
    [ -f "$f" ] || continue
    cp "$f" "$OUT/"; _n=$((_n+1))
done
[ "$_n" -gt 0 ] || die "there are no .pack files in $SRC."
[ ! -f "$SRC/temperate_grass_rgb.bin" ] || cp "$SRC/temperate_grass_rgb.bin" "$OUT/"
need_file CONQUER.MIX

# THE AUTHORED MODELS, models3d/<TYPE>.c3m, which the renderer finds by name beside the
# missions. This allow-list predates them and never named the folder, so a package made
# from it drew every authored vehicle and soldier with the cartridge's model and sprite
# while the Windows zip, which copies the folder from playable/, drew the new ones: the
# same release showing two different games. Counted, because a folder that arrives
# empty fails the same way and says nothing either.
if [ -d "$SRC/models3d" ]; then
    mkdir -p "$OUT/models3d"
    _m=0
    for f in "$SRC"/models3d/*.c3m; do
        [ -f "$f" ] || continue
        cp "$f" "$OUT/models3d/"; _m=$((_m+1))
    done
    [ "$_m" -gt 0 ] || die "$SRC/models3d holds no .c3m files, so the package would draw
       no authored model at all."
fi

# THE FIXED CARTRIDGE BUILDINGS, n64fix/<TYPE>.n64x, drawn in Enhanced in place of the
# cartridge's building models and found by name exactly as the authored models are, so
# this allow-list has to name them or the package draws the cartridge's buildings while
# the Windows zip draws the fixed ones. Taken from the TREE, as the Windows packager
# takes them: tracked files that no build step changes. Counted for the reason above.
if [ -d "$ROOT/game/n64fix" ]; then
    mkdir -p "$OUT/n64fix"
    _m=0
    for f in "$ROOT"/game/n64fix/*.n64x; do
        [ -f "$f" ] || continue
        cp "$f" "$OUT/n64fix/"; _m=$((_m+1))
    done
    [ "$_m" -gt 0 ] || die "game/n64fix holds no .n64x files, so the package would draw
       no fixed building at all."
fi

# THE ENHANCED BUILDINGS, ebld/<TYPE>.ebld: under Enhanced a type with one of these is
# drawn from its remastered model and breaks apart hit by hit in place of the fixed
# model and the old shatter. Found by type name like the fixed buildings, so named here
# for the same reason, taken from the TREE for the same reason, and counted for the
# same reason: without them the package's Power Plant differs from the Windows zip's.
if [ -d "$ROOT/game/ebld" ]; then
    mkdir -p "$OUT/ebld"
    _m=0
    for f in "$ROOT"/game/ebld/*.ebld; do
        [ -f "$f" ] || continue
        cp "$f" "$OUT/ebld/"; _m=$((_m+1))
    done
    [ "$_m" -gt 0 ] || die "game/ebld holds no .ebld files, so the package would draw
       no Enhanced building at all."
    # Faction livery masks belong to the paired model on both platforms.
    for f in "$ROOT"/game/ebld/*.ebld.fclv "$ROOT"/game/ebld/*.spec; do
        [ -f "$f" ] && cp "$f" "$OUT/ebld/"
    done
fi

# Final Desert albedo/geometry overrides must precede their paired mineral maps.
# Select the banks actually shipped after this package's RA surface filtering.
python3 "$ROOT/tools/art/terrain/stage_desert_details.py" "$OUT"
if [ "$OPENCNC3D_NO_RA" = "1" ]; then
    python3 "$ROOT/tools/art/rocks/stage_stone_materials.py" "$OUT" \
        --banks TEMPERAT DESERT WINTER SNOW SAND
else
    python3 "$ROOT/tools/art/rocks/stage_stone_materials.py" "$OUT"
fi

# THE PLACEMENTS, eplc/<TYPE>.eplc: under Enhanced a remastered structure with one of these
# goes up part by part, as the remaster lab places it, instead of by the cartridge's
# reveal. Taken from the TREE and counted for the same reason as the buildings above.
if [ -d "$ROOT/game/eplc" ]; then
    mkdir -p "$OUT/eplc"
    _m=0
    for f in "$ROOT"/game/eplc/*.eplc; do
        [ -f "$f" ] || continue
        cp "$f" "$OUT/eplc/"; _m=$((_m+1))
    done
    [ "$_m" -gt 0 ] || die "game/eplc holds no .eplc files, so the package's structures
       would go up by the cartridge's reveal."
fi

# THE REMASTERED VEHICLES AND AIRCRAFT, evhc/<TYPE>.evhc: under Enhanced a type with one of
# these is drawn, animated and wrecked from its remastered model in place of the
# cartridge's. Found by type name, taken from the TREE and counted, as the buildings above.
if [ -d "$ROOT/game/evhc" ]; then
    mkdir -p "$OUT/evhc"
    _m=0
    for f in "$ROOT"/game/evhc/*.evhc; do
        [ -f "$f" ] || continue
        cp "$f" "$OUT/evhc/"; _m=$((_m+1))
    done
    # (a type's specular maps, <TYPE>.spec, where its export has them)
    for f in "$ROOT"/game/evhc/*.spec; do [ -f "$f" ] && cp "$f" "$OUT/evhc/"; done
    [ "$_m" -gt 0 ] || die "game/evhc holds no .evhc files, so the package would draw
       no remastered vehicle at all."
fi

# THE REMASTERED SOLDIERS, einf/<TYPE>.c3m: under Enhanced an infantry type with one of
# these is drawn as an animated 3D soldier. From the TREE and counted, as the Enhanced
# buildings are, so the package's soldiers cannot differ from the Windows zip's.
if [ -d "$ROOT/game/einf" ]; then
    mkdir -p "$OUT/einf"
    _m=0
    for f in "$ROOT"/game/einf/*.c3m; do
        [ -f "$f" ] || continue
        cp "$f" "$OUT/einf/"; _m=$((_m+1))
    done
    [ "$_m" -gt 0 ] || die "game/einf holds no .c3m files, so the package would draw
       no remastered soldier at all."
fi

# THE REMASTERED CURSORS, ecur/ecur.bin: required source data, just as on Windows.
# The data fingerprint includes this path, so installs from a package that omitted it
# receive the complete package instead of a binary-only update.
[ -s "$ROOT/game/ecur/ecur.bin" ] || die "game/ecur/ecur.bin is missing or empty, so the
       macOS package would draw the cartridge cursors in Enhanced mode."
mkdir -p "$OUT/ecur"
cp "$ROOT/game/ecur/ecur.bin" "$OUT/ecur/"
[ -s "$OUT/ecur/ecur.bin" ] || die "ecur/ecur.bin is missing or empty in the macOS package."

# THE UNITY EXPLOSION, vfx3d/<effect>/, found by path beside the missions exactly as the
# models are, and missing from a package for the same reason if it is not named here:
# the package would draw the cartridge's explosion while the Windows zip drew Unity's.
if [ -d "$SRC/vfx3d" ]; then
    rsync -aL "$SRC/vfx3d" "$OUT/"
    [ -f "$OUT/vfx3d/claims.txt" ] || die "$SRC/vfx3d arrived without claims.txt, so the
       package would draw no Unity explosion."
fi

need_dir content       # original theater scenery merged with the playable terrain
for _theater in TEMPERAT DESERT; do
    [ -s "$OUT/content/$_theater.MIX" ] || die "content/$_theater.MIX is required for DOS Classic"
done
need_dir missions      # the scenario INIs
# No top-level movies/: it was a second copy of LOGO.VQA and INTRO2.VQA that nothing in the
# game reads (the game plays dosdata/movies/), and the movies no longer ship anyway.
# NO USER MAP SHIPS, the tracked examples included. need_dir excludes user_maps wholesale
# because it is a personal folder, and the package carries it EMPTY: the game and the
# editor save into it, so it exists, and nothing is in it. game/examples/ still tracks the
# worked examples and game/build.sh still stages them into a developer play folder, where
# the gates play them. Refused below if anything arrives in it.
_ex="$OUT/missions/user_maps"
mkdir -p "$_ex"
_um=$(cd "$_ex" && find . -type f ! -name '.DS_Store' | sed 's|^\./||' | head -8)
[ -z "$_um" ] || { echo "the package's user_maps folder is not empty, and no user map ships:" >&2
    echo "$_um" | sed 's/^/  /' >&2; exit 1; }


# DOSDATA IS THE ONE THAT HAS TO BE DEREFERENCED, AND IT IS CHECKED AFTERWARDS.
# In playable/ it is a SYMLINK into data/dosdata so that a local test folder does not
# carry a second 527 MB copy of the 1995 discs (game/make-build.sh makes the link, and
# the packages copy it for real). Archive that link AS a link and it expands on the other
# machine as a dangling pointer, after which the game boots, runs, and is completely
# silent, with one line on stderr to say so. That is project rule 9's "A RELEASE IS
# PLAYABLE" clause, and it is the single most expensive thing this script can get wrong,
# so it is asserted rather than assumed.
#
# It is copied WITHOUT the movies and the music (the media list above). The developer
# folder keeps them, because the gates that play a movie or measure the score run there.
need_dir dosdata "$MEDIA_EXCLUDE"
[ -L "$OUT/dosdata" ] && die "dosdata came across as a SYMLINK. Unzipped on another
       machine that is a dangling pointer and the game plays in silence."
# rsync keeps a folder whose only file was excluded (transit/ held nothing but
# TRANSIT.MIX), and an empty folder is not content. A download that brings the file back
# makes the folder again.
find "$OUT/dosdata" -mindepth 1 -type d -empty -delete
# THE SOUND HAS TO ARRIVE, and the score is not the proof of it any more: a package with
# no score is now what ships. The sound effects and the EVA lines are the 1995 archives
# that must be there, and without either the build boots, plays, and is silent.
for _mix in SOUNDS.MIX SPEECH.MIX; do
    [ -f "$OUT/dosdata/$_mix" ] || die "dosdata/$_mix is not in the package.
       Without the 1995 sound archives the build boots, plays, and has no sound at all."
done
# The movies and the music are asked of the WHOLE package at the end, below, so a movie
# or a score that arrives by any folder copied after this one is caught too.
# NEW AUDIO, dereferenced like dosdata and checked the same way. Without it the F5
# switch does nothing on the other machine, and says so only in the log.
need_dir cinematic
[ -L "$OUT/cinematic" ] && die "cinematic came across as a SYMLINK."
for clip in placement_impact building_collapse vehicle_impact structural_damage construction_rumble; do
    [ -f "$OUT/cinematic/$clip.wav" ] || die "cinematic audio is missing."
done
need_dir foley
[ -L "$OUT/foley" ] && die "foley came across as a SYMLINK."
for variant in 01 02 03 04 05 06 07 08 09 10; do
    [ -f "$OUT/foley/infantry_step_$variant.wav" ] || die "infantry foley is missing."
done
need_dir ambience
[ -L "$OUT/ambience" ] && die "ambience came across as a SYMLINK."
for clip in desert_zoomin desert_zoomout temperate_zoomin temperate_zoomout river_zoomin water_zoomin water_zoomout hud_click hud_mouseover tank_drive small_tank_drive vehicle_drive; do
    [ -f "$OUT/ambience/$clip.wav" ] || die "ambience/$clip.wav is missing."
done
need_dir newaudio
[ -L "$OUT/newaudio" ] && die "newaudio came across as a SYMLINK."
[ -f "$OUT/newaudio/README.txt" ] || die "newaudio/README.txt is not in the package, and
       the game only takes the folder as present when its README is there."

# ---- the launchers ----------------------------------------------------------------
# OpenCNC3D.app is the default double-click; the .command files are the specialised routes
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
#     dosdata and not one .command.
# ONE APP, NO .command FILES. Each platform ships a single executable called OpenCNC3D,
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
[ -d "$ROOT/tools/launchers/OpenCNC3D.app" ] || die "tools/launchers/OpenCNC3D.app is missing.
       It is the ONLY thing a player double-clicks now, so there is no fallback to ship
       instead of it."
rsync -aL --exclude '.DS_Store' "$ROOT/tools/launchers/OpenCNC3D.app" "$OUT/"
# THE BUNDLE'S EXECUTABLE IS THE COMPILED opencnc3d-launch, AND THE BUNDLE IS SIGNED. Both
# halves are one fix for one fault, found across two Macs: macOS asks the
# player once whether an app may use the local network and refuses every packet until
# they answer, but it only asks about an app it can NAME and VERIFY, and the process
# doing the asking has to still BE that app. A bundle whose executable is a script that
# execs a binary outside the bundle is neither, so the question was never put and LAN
# play was impossible on a Mac with no way to find out why.
#
# TWO TRAPS, both of which cost hours and both of which look like a corrupt download:
#   - A minimum-OS version newer than the player's Mac makes LaunchServices refuse the
#     bundle with -10825 and no explanation. Build machines run a newer SDK than players
#     do, so opencnc3d-launch MUST be built through tools/mac/deployment-target.sh, and
#     universal, or an Apple Silicon Mac cannot run the app's own executable.
#   - No certificate is involved here. This is ad-hoc signing, not notarisation, and the
#     right-click-Open an unsigned download needs is unchanged.
if [ -x "$ROOT/launcher/opencnc3d-launch" ]; then
    cp "$ROOT/launcher/opencnc3d-launch" "$OUT/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
    chmod +x "$OUT/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch"
else
    die "launcher/opencnc3d-launch is missing. launcher/build.sh makes it. Without it the
       bundle ships the old shell script, which cannot be signed and cannot hold the
       app identity the local-network permission is attached to: LAN play then fails on
       every Mac, silently."
fi
# BOTH CPUS IN WHAT SHIPS. game/make-build.sh checks the play folder; this checks the
# package, which is assembled from it by name and also swaps in the compiled .app
# executable. One package serves Apple Silicon and Intel, so a thin file here stops the
# game on the other kind of Mac and on no machine the build was tested on.
. "$ROOT/tools/mac/deployment-target.sh"
for f in "$OUT"/* "$OUT/OpenCNC3D.app/Contents/MacOS"/*; do
    [ -f "$f" ] || continue
    case "$(file -b "$f")" in *Mach-O*) ;; *) continue ;; esac
    # shellcheck disable=SC2086
    lipo "$f" -verify_arch $OPENCNC3D_MAC_ARCHS 2>/dev/null \
        || die "$(basename "$f") in the package is $(lipo -archs "$f" 2>/dev/null), not $OPENCNC3D_MAC_ARCHS"
done
if command -v codesign >/dev/null 2>&1; then
    codesign --force -s - "$OUT/OpenCNC3D.app" 2>/dev/null \
        || echo "note: ad-hoc signing OpenCNC3D.app failed; the Local Network prompt will not appear" >&2
fi
# NOT SIGNED, and that is a finding, not an omission: an ad-hoc signature on
# this bundle, whose executable is a shell script, makes macOS 15 refuse to open it at
# all ("not supported on this version of macOS"), while the unsigned bundle opens with
# the usual right-click-Open. The Local Network prompt does not need the signature: it
# needs the usage text in Info.plist and a send the player can see, which the launcher
# now makes before any game window.
[ -d "$OUT/OpenCNC3D.app" ] || die "OpenCNC3D.app did not make it into the package. It is the
       default double-click, the one thing that always works, and the READ-ME
       names it first. The tracked master is tools/launchers/OpenCNC3D.app."
[ -f "$OUT/OpenCNC3D.app/Contents/Resources/AppIcon.icns" ] || die "OpenCNC3D.app has no icon.
       it exists so the app is recognisable at a glance; rebuild it with
       tools/launchers/make_icon.py, which also writes the Windows .ico from the same
       source so the two platforms cannot drift."
_n=$(ls -A "$OUT"/*.command 2>/dev/null | wc -l | tr -d ' ')
[ "$_n" = "0" ] || die "a .command launcher reached the package ($_n of them). The macOS
       package ships OpenCNC3D.app and nothing else to double-click; see the note above."

# The app opens the launcher. A wrapper can fall back to the game when the
# launcher is absent, so verify both the wrapper's launcher route and the
# packaged launcher executable.
grep -q 'opencnc3d-launcher' "$OUT/OpenCNC3D.app/Contents/MacOS/opencnc3d-launch" \
    || die "OpenCNC3D.app's script does not run opencnc3d-launcher. That app is the ONE thing
       a player double-clicks, so a package whose app goes straight to the game ships
       with no launcher at all: no version, no changelog, no update."
cmp -s "$SRC/opencnc3d-launcher" "$OUT/opencnc3d-launcher" \
    || die "opencnc3d-launcher in the package is not the one in $SRC. The app execs it by
       name and would fall through to the game."
strings -a "$OUT/opencnc3d-launcher" 2>/dev/null | grep -q '/api/builds' \
    || die "opencnc3d-launcher does not contain the site's build route, so whatever it is,
       it is not a launcher that can check for updates. Rebuild it with
       game/make-build.sh, which is what refreshes $SRC."
echo "   OpenCNC3D.app opens opencnc3d-launcher, which can reach the site"

# ---- the paper --------------------------------------------------------------------
# FROM THE TRACKED MASTER, not from playable/. The copy that lived in playable/ was
# untracked, nothing regenerated it, and it went out in every release still announcing
# "OpenCNC3D v0.3.1 (macOS build, )".
[ -f "$ROOT/tools/launchers/READ-ME.txt" ] || die "tools/launchers/READ-ME.txt is missing.
       That file is the macOS package's README and it is tracked; restore it rather than
       falling back to whatever is in $SRC."
cp "$ROOT/tools/launchers/READ-ME.txt" "$OUT/"

# ---- THE LICENCE AND THE NOTICE, and they are not optional -----------------------
#
# THIS PROGRAM IS A SINGLE WORK BUILT ON EA's GPL SOURCE, so a binary package that
# carries neither the licence nor the notice of modification is not a compliant
# distribution. Every release up to and including v0.6.4 shipped without them, which
# is a defect in the builds already in players' hands and not only in the next one.
# The licence is unchanged; the notice describes only the packaged modules while
# preserving applicable source and art attribution. The full repository notice stays intact.
for f in LICENSE; do
    [ -f "$ROOT/$f" ] || die "$f is missing from the repo root.
       It is on the package's allow-list because a GPL binary distribution has to carry
       it. Restore it rather than shipping without it."
    cp "$ROOT/$f" "$OUT/"
done
python3 "$ROOT/tools/make-package-notice.py" "$OUT/NOTICE.md" \
    --package-root "$OUT" --no-ra "${OPENCNC3D_NO_RA:-0}"

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
#  opencnc3d-fx.cfg
#      THE TUNED PRESENTATION DIALS, and the one omission here that changes what the
#      player sees, so it is spelled out. opencnc3d reads that filename at boot
#      (app/opencnc3d.cpp:655), so while it sat in playable/ the macOS package shipped it and
#      the Windows package, assembled from a named list, never had it. Two packages of
#      the same tag, rendering from different numbers, with nothing saying so. Both now
#      boot fx_defaults() from game/fx_state.h. The preset itself is NOT lost: it is
#      tracked at docs/tuning/opencnc3d-fx.cfg, with a header saying whose it is and how to
#      put it back. One can be regenerated at any time with TUNE-PRESENTATION.command.
#
#  cnc_eyes_head
#      A stale x86_64 binary from 15 Aug that nothing in the repo, gates.sh included,
#      references by name.
#
#  BUILD-HISTORY.txt, .DS_Store
#      Developer bookkeeping. BUILD-HISTORY.txt is the one file the old cp -RL package
#      remembered to delete.
#
#  dosdata/movies/, dosdata/music/, dosdata/SCORES.MIX, dosdata/transit/TRANSIT.MIX,
#  movies/, and Red Alert's ra3d/movies/ and its SCORES.MIX
#      The 1995 movies and score, about 566 MB, and Red Alert's, about 790 MB more. The
#      Tiberian Dawn ones come from the disc images on archive.org at install time, when
#      the player wants them; Red Alert's have no download yet, so a package with the
#      module plays it without them. Any of them is refused just below if it arrives. The
#      list is tools/media/media-paths.txt.

# NO MOVIE AND NO SCORE CAME WITH ANY OF IT, asked of the whole finished package rather
# than trusted to the exclude list: every path on the list, matched without regard to case
# as a player's disk may match it, then anything that is a movie or a score by its name
# wherever it landed, then the two files a media download leaves in an install (its
# record and its partial files), which belong to the player's folder and never to a
# package. Red Alert's are refused the same way: its movies and score are on the list,
# and a .VQA or a SCORES.MIX under ra3d/ is a movie or a score like any other. Asked
# here, last, so that no folder copied above can bring one in unasked.
_media=$(cd "$OUT" && {
    while IFS= read -r _p; do
        _p=${_p#/}; _p=${_p%/}
        [ -n "$_p" ] && find . -ipath "./$_p" -print
    done < "$MEDIA_EXCLUDE"
    find . \( -iname '*.vqa' -o -iname 'SCORES.MIX' -o -iname 'TRANSIT.MIX' \
        -o -ipath './dosdata/music/*' -o -iname 'opencnc3d-media.txt' -o -iname '*.ocpart' \) \
        -print
} | sed 's|^\./||' | sort -u | head -12)
[ -z "$_media" ] || die "movies or music reached the package, and no package carries them:
$(echo "$_media" | sed 's/^/           /')
       The Windows installer and the launcher's Setup download them for the player who
       wants them. Red Alert's may not ship either: tools/ra3d/stage-runtime.py stages
       the module without the paths tools/media/media-paths.txt names under ra3d/, so
       one found under ra3d/ is either missing from that list or came by another road."

# ---- what the launcher reads about its own install --------------------------------
# Written here, last, so both files describe the folder exactly as it is about to
# be zipped:
#
#   opencnc3d-install.txt   the version, and the data fingerprint that decides whether
#                       the next update can be the 2.7 MB one or has to be 507 MB
#   CHANGELOG.txt       so the launcher's panel has the right notes in it before
#                       any network call has been made, or has failed
#
# The fingerprint comes from tools/launcher/data-id.sh, which is also what
# tools/launcher/make-manifest.sh reads back out of this file when it writes the
# manifest. One implementation of that hash, quoted at both ends: two would agree
# until the day they did not, and the only symptom would be every update silently
# becoming a full download.
#
# NO RED ALERT, ASKED OF THE FOLDER ITSELF, first: the fingerprint covers every data file
# in it. Everything Red Alert is under ra3d/ or named for it (RedAlert.dylib, ra3d-fx.cfg),
# so a package built without it holds no such name anywhere.
if [ "$OPENCNC3D_NO_RA" = "1" ]; then
    _ra=$(cd "$OUT" && find . \( -iname 'ra3d*' -o -iname 'redalert*' -o -name '*_RA_*' \) \
        ! -name 'waterbase_RA_TEMPERAT.pack' | head -5)
    [ -z "$_ra" ] || die "the package was built with OPENCNC3D_NO_RA=1 and still carries
       Red Alert:
$(echo "$_ra" | sed 's/^/           /')"
fi
python3 "$ROOT/tools/launcher/prepare-public-runtime.py" "$OUT"
VERNUM=$(tr -d '[:space:]' < "$ROOT/VERSION")
"$ROOT/tools/launcher/make-changelog-txt.sh" "$OUT/CHANGELOG.txt" >/dev/null
{
    echo "# written by tools/mac/make-package-mac.sh"
    echo "version $VERNUM"
    echo "data_id $("$ROOT/tools/launcher/data-id.sh" "$OUT")"
} > "$OUT/opencnc3d-install.txt"
[ -s "$OUT/opencnc3d-install.txt" ] || die "opencnc3d-install.txt was not written. Without it
       the launcher cannot tell which build this is, and the update host cannot offer
       this package the small binary-only update."
echo "   install record: v$VERNUM  data $(awk '$1=="data_id"{print substr($2,1,12)}' "$OUT/opencnc3d-install.txt")"

ENTRIES=$(find "$OUT" -mindepth 1 | wc -l | tr -d ' ')
TOP=$(ls -A "$OUT" | wc -l | tr -d ' ')
SIZE=$(du -sh "$OUT" | cut -f1)
echo "   $OUT  ($SIZE, $TOP top-level entries, $ENTRIES files and directories)"
