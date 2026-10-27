#!/bin/sh
# Put the right rock packs into a game folder: the one step both builds share, so the Mac
# folder (game/make-build.sh) and the Windows package (tools/win/make-build-win.sh) can
# never carry different stones.
#
#   tools/art/rocks/stones.sh <folder> [painted|unity]
#
# The choice defaults to ROCK_STONES, then to ROCK_STONES_DEFAULT in game/make-build.sh,
# which is the one flag. "painted" puts rock3d_DESERT.pack, rock3d_TEMPERAT.pack and
# rock3d_WINTER.pack (tools/art/rocks/build_stones.py) in the folder; the game reads a
# map's theater pack in place of rock3d.pack. "unity" removes them, so the folder draws
# rock3d.pack's stones and never a mix of the two. The packs are rebuilt by that script
# rather than committed; the bake takes about a minute and is kept in build-native/rocks,
# stamped with the script and the pack writer it depends on, until either changes.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
DEST=${1:?usage: stones.sh <folder> [painted|unity]}
FLAG=$(sed -n 's/^ROCK_STONES_DEFAULT=\([a-z]*\).*/\1/p' "$REPO/game/make-build.sh" | head -1)
CHOICE=${2:-${ROCK_STONES:-$FLAG}}
case "$CHOICE" in
painted)
    RKC="$REPO/build-native/rocks"
    STAMP=$(cat "$HERE/build_stones.py" "$REPO/game/bake_tree3d.py" | cksum | cut -d' ' -f1)
    if [ "$(cat "$RKC/stamp" 2>/dev/null)" != "$STAMP" ] \
       || [ ! -s "$RKC/rock3d_DESERT.pack" ] || [ ! -s "$RKC/rock3d_TEMPERAT.pack" ] \
       || [ ! -s "$RKC/rock3d_WINTER.pack" ]; then
        mkdir -p "$RKC"
        rm -f "$RKC/stamp"
        python3 "$HERE/build_stones.py" --out-dir "$RKC" >/dev/null || {
            echo "stones.sh: build_stones.py failed; the painted stones were asked for" >&2
            exit 1; }
        echo "$STAMP" > "$RKC/stamp"
    fi
    # The original painted pack remains an immutable geometry/UV input. Desert
    # alone receives the approved cliff material on those existing UVs, plus
    # theater overrides for the seven placed rocks. Include every input in cache.
    MATCH="$RKC/cliff-sandstone-material-v05"
    MATCH_STAMP=$(cat "$HERE/match_desert_cliffs.py" "$REPO/tools/bakery/terrain_daylight.py" \
        "$REPO/art/remaster/ROCKS/cliff_sandstone_v05/material.png" \
        "$RKC/rock3d_DESERT.pack" "$REPO"/game/ebld/ROCK[1-7].ebld | cksum | cut -d' ' -f1)
    if [ "$(cat "$MATCH/stamp" 2>/dev/null)" != "$MATCH_STAMP" ] \
       || [ ! -s "$MATCH/rock3d_DESERT.pack" ] || [ ! -s "$MATCH/ROCK7_DES.ebld" ]; then
        python3 "$HERE/match_desert_cliffs.py" --source "$REPO" \
            --scatter "$RKC/rock3d_DESERT.pack" --output "$MATCH"
        echo "$MATCH_STAMP" > "$MATCH/stamp"
    fi
    cp "$MATCH/rock3d_DESERT.pack" "$RKC/rock3d_TEMPERAT.pack" "$RKC/rock3d_WINTER.pack" "$DEST/"
    mkdir -p "$DEST/ebld"
    cp "$MATCH"/ROCK[1-7]_DES.ebld "$DEST/ebld/"
    mkdir -p "$DEST/rock-materials/cliff-sandstone-v05"
    cp "$MATCH"/*.png "$MATCH/manifest.json" "$DEST/rock-materials/cliff-sandstone-v05/"
    echo "   rocks: the painted stones, a pack per theater"
    ;;
unity)
    rm -f "$DEST/rock3d_DESERT.pack" "$DEST/rock3d_TEMPERAT.pack" "$DEST/rock3d_WINTER.pack"
    echo "   rocks: the experimental stones (rock3d.pack)"
    ;;
*)
    echo "stones.sh: the choice must be painted or unity, not '$CHOICE'" >&2
    exit 1 ;;
esac
