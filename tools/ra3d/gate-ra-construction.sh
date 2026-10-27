#!/bin/sh
# Build a real Soviet power plant through its RA cameo and place it with a map click.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
work=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-build.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'dos_classic 1\nenabled 0\n' > "$work/classic.cfg"
cat > "$work/construction.script" <<EOF
tick 30
cam 69 48
sbclickitem POWR
ready POWR 2000
startplace POWR
hover 69 48
shot $work/legal.png
hover 70 48
shot $work/blocked.png
hover 69 48
lclickcell 69 48
tick 2
sidebar after
objdump
quit
EOF
cd "$install"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib \
    --content ra3d/data --dir ra3d/missions/ --scen SCU02EA \
    --pack ra3d/SCG01EA.pack --dosdata ra3d/audio --nosound \
    --gfx "$work/classic.cfg" --script "$work/construction.script" \
    > "$work/run.log" 2>&1
grep -Eq 'DOS2D\|loaded [1-9][0-9]+ original Red Alert SHP shapes' "$work/run.log"
grep -Fq 'SCRIPT|hover|69,48|legal=1' "$work/run.log"
grep -Fq 'SCRIPT|hover|70,48|legal=0' "$work/run.log"
grep -Fq 'SIDEBAR-REQUEST|PLACE|POWR|cell=69,48|grid=34,11' "$work/run.log"
grep -Eq 'OBJ\|BUILDING\|POWR\|USSR\|newcell=6213\|x=69\|y=48.*\|limbo=0' "$work/run.log"
grep -Fq 'SCRIPT|end ' "$work/run.log"
grep -Fq '0 failures' "$work/run.log"
test -s "$work/legal.png"
test -s "$work/blocked.png"
if cmp -s "$work/legal.png" "$work/blocked.png"; then
    echo 'RA placement cursor pictures did not change between legal and blocked origins' >&2
    exit 1
fi
cat > "$work/production.script" <<EOF
tick 30
sbclickitem BARR
ready BARR 2000
startplace BARR
cam 69 48
hover 69 48
lclickcell 69 48
tick 20
sbitem E1
sbclickitem E1
ready E1 2000
tick 20
sbclickitem YAK
ready YAK 2000
tick 20
cam 76 57
dosdrawprobe
shot $work/yak.png
objdump
quit
EOF
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib \
    --content ra3d/data --dir ra3d/missions/ --scen SCU02EA \
    --pack ra3d/SCG01EA.pack --dosdata ra3d/audio --nosound \
    --gfx "$work/classic.cfg" --script "$work/production.script" \
    > "$work/production.log" 2>&1
grep -Fq 'SIDEBAR-REQUEST|PLACE|BARR|cell=69,48|grid=34,11' "$work/production.log"
grep -Fq 'SCRIPT|ready|E1|COMPLETED' "$work/production.log"
grep -Fq 'SCRIPT|ready|YAK|COMPLETED' "$work/production.log"
grep -Eq 'OBJ\|BUILDING\|BARR\|USSR\|newcell=6213\|x=69\|y=48.*\|limbo=0' "$work/production.log"
grep -Eq 'OBJ\|INFANTRY\|E1\|USSR\|.*\|id=22\|' "$work/production.log"
grep -Eq 'OBJ\|AIRCRAFT\|YAK\|USSR\|.*\|limbo=0' "$work/production.log"
grep -Fq 'DOSDRAW|layer|YAK|asset=YAK|' "$work/production.log"
grep -Eq 'DOS2D\|loaded [1-9][0-9]+ original Red Alert SHP shapes' "$work/production.log"
if grep -Fq 'DOS2D|missing original SHP YAK' "$work/production.log"; then
    echo 'built YAK has no original Classic sprite' >&2
    exit 1
fi
test -s "$work/yak.png"
grep -Fq '0 failures' "$work/production.log"
cat > "$work/navy.script" <<EOF
tick 30
objdump
sbclickitem SS
ready SS 3000
tick 20
objdump
quit
EOF
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib \
    --content ra3d/data --dir ra3d/missions/ --scen SCU08EA \
    --pack ra3d/SCG01EA.pack --dosdata ra3d/audio --nosound \
    --script "$work/navy.script" > "$work/navy.log" 2>&1
grep -Fq 'SIDEBAR-INPUT|build|SS' "$work/navy.log"
grep -Fq 'SCRIPT|ready|SS|COMPLETED' "$work/navy.log"
grep -Fq '0 failures' "$work/navy.log"
python3 - "$work/navy.log" <<'PY'
import re
import sys

sections = []
current = None
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if line.startswith("OBJDUMP-BEGIN"):
        current = set()
    elif line.startswith("OBJDUMP-END") and current is not None:
        sections.append(current)
        current = None
    elif current is not None and line.startswith("OBJ|VESSEL|SS|USSR|"):
        if "|limbo=0|" in line:
            match = re.search(r"\|id=(\d+)\|", line)
            if match:
                current.add(int(match.group(1)))
if len(sections) != 2 or len(sections[1] - sections[0]) != 1:
    raise SystemExit(f"expected one new live Soviet submarine, found {sections!r}")
PY
cat > "$work/vehicle.script" <<EOF
tick 30
objdump
sbclickitem WEAP
ready WEAP 6000
startplace WEAP
place auto
tick 30
sbclickitem 3TNK
ready 3TNK 6000
tick 1
objdump
quit
EOF
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib \
    --content ra3d/data --dir ra3d/missions/ --scen SCU09EA \
    --pack ra3d/SCG01EA.pack --dosdata ra3d/audio --nosound \
    --gfx "$work/classic.cfg" --script "$work/vehicle.script" \
    > "$work/vehicle.log" 2>&1
grep -Fq 'SIDEBAR-INPUT|build|WEAP' "$work/vehicle.log"
grep -Fq 'SIDEBAR-REQUEST|PLACE|WEAP|' "$work/vehicle.log"
grep -Fq 'SIDEBAR-INPUT|build|3TNK' "$work/vehicle.log"
grep -Fq 'SCRIPT|ready|3TNK|COMPLETED' "$work/vehicle.log"
grep -Fq '0 failures' "$work/vehicle.log"
python3 - "$work/vehicle.log" <<'PY'
import re
import sys

sections = []
factories = []
current = None
current_factory = None
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if line.startswith("OBJDUMP-BEGIN"):
        current = set()
        current_factory = set()
    elif line.startswith("OBJDUMP-END") and current is not None:
        sections.append(current)
        factories.append(current_factory)
        current = None
        current_factory = None
    elif current is not None and line.startswith("OBJ|UNIT|3TNK|USSR|"):
        if "|limbo=0|" in line:
            ident = re.search(r"\|id=(\d+)\|", line)
            cell = re.search(r"\|newcell=(\d+)\|", line)
            if ident and cell:
                # RA's heap can reuse the ID of a tank destroyed while this
                # long build runs. Keep its map position in the identity check:
                # a replacement at the new War Factory must still count.
                current.add((int(ident.group(1)), int(cell.group(1))))
    elif current is not None and line.startswith("OBJ|BUILDING|WEAP|USSR|"):
        if "|limbo=0|" in line:
            cell = re.search(r"\|newcell=(\d+)\|", line)
            if cell:
                current_factory.add(int(cell.group(1)))
new_tanks = sections[1] - sections[0] if len(sections) == 2 else set()
if len(new_tanks) != 1 or len(factories) != 2 or not all(
    any(abs(cell % 128 - factory % 128) <= 3 and
        abs(cell // 128 - factory // 128) <= 3
        for factory in factories[1])
    for _, cell in new_tanks):
    raise SystemExit(f"expected one new tank at a live War Factory, found "
                     f"{sections!r} factories={factories!r}")
PY
cat > "$work/tall-sidebar.script" <<EOF
tick 30
sbitem POWR
sbclickitem POWR
shot $work/tall-sidebar.png
quit
EOF
OPENCNC3D_BACKGROUND=1 ./cnc_eyes --ra3d --dylib ra3d/RedAlert.dylib \
    --content ra3d/data --dir ra3d/missions/ --scen SCU09EA \
    --pack ra3d/SCG01EA.pack --dosdata ra3d/audio --nosound \
    --w 1280 --h 1200 --gfx "$work/classic.cfg" \
    --script "$work/tall-sidebar.script" > "$work/tall-sidebar.log" 2>&1
grep -Fq 'SIDEBAR-INPUT|build|POWR' "$work/tall-sidebar.log"
grep -Fq '0 failures' "$work/tall-sidebar.log"
python3 - "$work/tall-sidebar.png" "$work/tall-sidebar.log" <<'PY'
from PIL import Image
import re
import sys

image = Image.open(sys.argv[1]).convert("RGB")
if image.size != (1280, 1200):
    raise SystemExit("tall sidebar render size changed")
match = re.search(r"SBRECT\|POWR\|7\|\d+,\d+\.\.\d+,\d+\|centre=(\d+),(\d+)",
                  open(sys.argv[2]).read())
if not match or not (960 <= int(match[1]) < 1280 and
                     1000 <= int(match[2]) < 1200):
    raise SystemExit("eighth build-row hitbox is not inside the lower sidebar")
lower = sum(image.getpixel((x, y)) != (0, 0, 0)
            for y in range(1040, 1160) for x in range(960, 1280))
if lower < 2000:
    raise SystemExit(f"repeated Win95 sidebar section missing: {lower} lower pixels")
PY
echo 'RA3D Soviet Classic placement, production and tall Win95 sidebar rows passed'
