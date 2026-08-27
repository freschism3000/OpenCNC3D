#!/bin/sh
# Wave 4 visual comparison set.
#
# Takes the same handful of shots every time, into a named directory, so a change can be
# judged against the build before it rather than against memory. Not a gate: gates assert,
# this one only photographs. Run it before a change and after, then look at both.
#
#   ./wave4_shots.sh /tmp/wave4/before
#
# Runs from playable/ because that is where the baked packs and the brain live.
set -e
OUT="${1:?usage: wave4_shots.sh <outdir>}"
mkdir -p "$OUT"
cd "$(dirname "$0")/../playable"

COMMON="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
  --dylib ./TiberianDawn.dylib --dir ./missions/ --content ./content/ --nosound"

shoot() {                        # shoot <tag> <scen> <pack> <script-body>
    tag="$1"; scen="$2"; pack="$3"; body="$4"
    printf '%s\nshot %s/%s.png\nquit\n' "$body" "$OUT" "$tag" > /tmp/w4_$tag.txt
    # shellcheck disable=SC2086
    ./cnc_eyes --scen "$scen" --pack "$pack" $COMMON --script /tmp/w4_$tag.txt \
        > "$OUT/$tag.log" 2>&1 || echo "RUN FAILED: $tag (see $OUT/$tag.log)"
    grep -E '^SCRIPT\|end' "$OUT/$tag.log" || true
}

# The battlefield as it is first seen it: water, shore, rocks, a vehicle, a health bar.
shoot overview  SCG01EB SCG01EA.pack "tick 30"
# Water and shoreline close up, on the gunboat map, sidebar off so the sea fills the frame.
shoot water     SCG10EA SCG10EA.pack "tick 20
zoomin
zoomin"
# The map edge: pan hard off the corner so whatever is drawn past the map is in frame.
shoot mapedge   SCG01EB SCG01EA.pack "tick 10
zoomout
zoomout
zoomout
pan -400 -300
tick 2"
# A vehicle close up: wheels, house colour, and the ground under it (shadow or no shadow).
shoot vehicle   SCG01EB SCG01EA.pack "tick 20
zoomin
zoomin
zoomin"
# Walls and buildings, for shadows and damage state.
shoot walls     SCG01EB SCG01EA.pack "tick 40
zoomin"

echo "shots in $OUT"
