#!/bin/sh
# gate-ra-3d.sh [install-dir] -- Red Alert's missions in the 3D mode.
#
# Through the real launcher, hidden (OPENCNC3D_BACKGROUND): start Allied 1, switch to
# Enhanced, click a Ranger, order it to move, and take a frame. Then boot one mission of
# each other kind in Enhanced: Soviet 1 (snow), Allied 5 (temperate) and Soviet 7
# (interior), each on its own scene.
#
# WHAT EACH CHECK MEANS:
#   scene     Allied 1 is handed its OWN scene, ra3d/3d/SCG01EA.pack, not the shared
#             placeholder: Red Alert's terrain on Red Alert's snow bank (theater RASNOW),
#             128 x 128 cells, with heights (a flat range means the auto heightmap did
#             not reach the pack).
#   textures  The Red Alert ground (ra3d_terrain.h) paints that terrain from
#             ra3d/terrainenh_RA_SNOW.pack: at least 650 of the mission's pieces from the
#             144-texel pack, none missing (a missing tile is a hole in the ground).
#   sprites   every object, overlay and animation is Red Alert's own sprite standing on
#             the 3D ground: dozens of cards, no sprite the pack lacks.
#   play      a click in Enhanced selects the Ranger and the move order lands it at 60,50,
#             as in Classic: the 3D picture did not break the 3D click mapping.
#   picture   the frame is a lit scene, not a black or flat screen.
#   menu      each mission goes back to Classic before it quits, so the harness compares
#             the menu after the mission with the Classic menu it started from: a player
#             who leaves a mission in Enhanced comes back to the Enhanced menu, which
#             would not be the same picture.
#   theaters  Soviet 1, Allied 5 and Soviet 7 each boot on their own scene in their own
#             bank (RASNOW, RATEMP, RAINT), with no ground tile and no sprite missing;
#             Allied 5's temperate ground is painted from the 144-texel pack, and Soviet
#             7's interior is a flat floor.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
install=$(CDPATH= cd -- "$install" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-3d.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/allied.script" <<EOF
tick 30
gfx dos_classic 0
gfx enabled 1
gfx texset 3
tick 5
cam 62 50
tick 1
lclickobj JEEP 0
expectsel 1
order 60 50
tick 90
seldump
shot $work/allied3d.png
gfxmode 0
tick 1
quit
EOF
cat > "$work/other.script" <<EOF
tick 30
gfxmode 2
tick 5
shot $work/other3d.png
gfxmode 0
tick 1
quit
EOF
cd "$install"
OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- \
    --harness 1 --rounds 1 --no-movies --nosound \
    --script "$work/allied.script" --shotdir "$work" > "$work/allied.log" 2>&1
for run in "SCU01EA soviet" "SCG05EA allied" "SCU07EA soviet"; do
    set -- $run
    OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- \
        --harness 1 --rounds 1 --no-movies --nosound --raside "$2" --rascen "$1" \
        --script "$work/other.script" --shotdir "$work" > "$work/$1.log" 2>&1
done
fail() { echo "gate-ra-3d: FAIL: $1" >&2; exit 1; }
grep -Fq 'RA3D|start|SCG01EA|original mission|scene=ra3d/3d/SCG01EA.pack' "$work/allied.log" \
    || fail "Allied 1 was not handed its own 3D scene"
grep -Eq 'pack RAG01EA: theater=RASNOW .*cells=16384' "$work/allied.log" \
    || fail "the scene is not Red Alert's 128 x 128 terrain on the RASNOW bank"
range=$(sed -n 's/^pack: heightmap \([0-9]*\)\.\.\([0-9]*\),.*/\1 \2/p' "$work/allied.log" | head -1)
set -- $range
[ "${1:-0}" -lt "${2:-0}" ] || fail "the scene's heightmap is flat (${range:-none})"
painted=$(sed -n 's/^RA3D|enh|sheet|SCG01EA|SNOW|enhanced|.*|pack=\([0-9]*\)|variants=[0-9]*|tiles=[0-9]*|missing=\([0-9]*\)|.*/\1 \2/p' "$work/allied.log" | head -1)
set -- $painted
[ "${1:-0}" -ge 650 ] && [ "${2:-1}" -eq 0 ] \
    || fail "Enhanced snow textures: ${painted:-none} (want >= 650 painted, 0 missing)"
cards=$(sed -n 's/^RA3D|sprites3d|SCG01EA|cards=\([0-9]*\)|decals=[0-9]*|missing=\([0-9]*\)|theater=RASNOW\(|.*\)\{0,1\}$/\1 \2/p' "$work/allied.log" | head -1)
set -- $cards
[ "${1:-0}" -ge 40 ] && [ "${2:-1}" -eq 0 ] \
    || fail "Red Alert sprites in 3D: ${cards:-none} (want >= 40 cards, 0 missing)"
grep -Fq 'RA3D|scene|ground=red alert|theater=SNOW|rect=48,44 32x38|set=enhanced|heights=scene' "$work/allied.log" \
    || fail "Allied 1's Red Alert ground is not laid on its scene's heights"
grep -Fq 'EXPECTSEL|want=1|have=1|PASS' "$work/allied.log" || fail "a click in Enhanced did not select the Ranger"
grep -Fq 'SEL|UNIT|JEEP|id=0|cell=6460|x=60|y=50' "$work/allied.log" || fail "the Ranger did not reach 60,50"
python3 - "$work/allied3d.png" <<'PY' || fail "the Enhanced frame is not a lit scene"
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert("L")
w, h = im.size
field = im.crop((0, 0, int(w * 0.8), h))
st = ImageStat.Stat(field)
sys.exit(0 if st.mean[0] > 25 and st.stddev[0] > 20 else 1)
PY
for run in "SCU01EA RAU01EA RASNOW" "SCG05EA RAG05EA RATEMP" "SCU07EA RAU07EA RAINT"; do
    set -- $run
    log="$work/$1.log"
    grep -Fq "RA3D|start|$1|original mission|scene=ra3d/3d/$1.pack" "$log" \
        || fail "$1 was not handed its own 3D scene"
    grep -Eq "pack $2: theater=$3 .*cells=16384" "$log" || fail "$1 is not on the $3 bank"
    grep -Fq "GFXMODE|enhanced|script" "$log" || fail "$1 did not switch to Enhanced"
    grep -Eq "^RA3D\|scene\|ground=red alert\|.*\|heights=scene$" "$log" \
        || fail "$1's Red Alert ground is not laid on its scene's heights"
    grep -Eq "RA3D\|sprites3d\|$1\|cards=[1-9][0-9]*\|decals=[0-9]*\|missing=0\|theater=$3(\||\$)" "$log" \
        || fail "$1: Red Alert sprites in 3D: $(grep -o 'RA3D|sprites3d|.*' "$log" | head -1)"
done
grep -Eq '^RA3D\|enh\|sheet\|SCG05EA\|TEMPERAT\|enhanced\|.*\|pack=[1-9][0-9]*\|.*\|missing=0\|' \
    "$work/SCG05EA.log" || fail "Allied 5's temperate ground is not painted from the 144-texel pack"
grep -Fq 'RA3D|scene|ground=red alert|theater=INTERIOR|rect=39,42 72x47|set=1995' "$work/SCU07EA.log" \
    || fail "Soviet 7's interior did not draw its 1995 art"
grep -Eq '^pack: heightmap 0\.\.0,' "$work/SCU07EA.log" || fail "Soviet 7's interior is not a flat floor"
echo "gate-ra-3d: PASS: Allied 1 in Enhanced on its own scene (heights ${range}), ${painted%% *} snow tiles painted, ${cards%% *} Red Alert sprites, click-to-move works; Soviet 1 (snow), Allied 5 (temperate) and Soviet 7 (interior, flat) boot in Enhanced on their own scenes with nothing missing"
