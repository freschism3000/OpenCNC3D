#!/bin/sh
# gate-ra-save.sh [install-dir] -- Red Alert's saves: its own, round trip exact, and played on.
#
# Hidden throughout (OPENCNC3D_BACKGROUND), in scratch save folders, never the player's.
#
#   A  ROUND TRIP AND PLAY ON, Allied 1 (snow) and Soviet 1: the renderer, scripted, saves at
#      tick 300, plays 200 more ticks, loads, and the object dump must be the pre-save one line
#      for line over a world that moved in between; then it plays 300 ticks on from the load,
#      and the world must move again and reach frame 600.
#   B  ONE FOLDER, TWO GAMES: Tiberian Dawn saves SCG01EA into slot 0 of a folder, then Red
#      Alert's SCG01EA (the same name) runs in the same folder. Red Alert must list none of it,
#      refuse to load it ("saved by Tiberian Dawn") and refuse to save over it, and save into
#      slot 1 instead; Tiberian Dawn must then list only its own slot and still load it exactly,
#      and must still refuse the Red Alert slot once its game tag is cleared to zero, the
#      way every record from before the tag reads (the pack names the module).
#   C  THE SHELL: the Red Alert module through its launcher, playing Allied 1 with a folder
#      holding a Soviet 1 slot, loads it from the pause dialog. The mission is another one, so
#      the game hands over to the shell, which boots Soviet 1 through the Red Alert engine and
#      loads the slot there.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
install=$(CDPATH= cd -- "$install" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-save.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$install"
fail() { echo "gate-ra-save: FAIL: $1" >&2; exit 1; }
[ -f ra3d/RedAlert.dylib ] || fail "no Red Alert module is staged in $install"

RA="--ra3d --dylib ra3d/RedAlert.dylib --content ra3d/data --dir ra3d/missions/ --dosdata ra3d/audio --nosound --w 640 --h 400"
TD="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/ --nosound --w 640 --h 400"

# the three dumps a script asked for, in order, objects and houses only
dumps() {
    awk -v want="$2" '/^OBJDUMP-BEGIN/{n++} n==want&&/^(OBJ\||HOUSE\|)/{print}' "$1"
}

# ---- A ------------------------------------------------------------------------------
cat > "$work/a.txt" <<EOF
tick 300
objdump
save 0 round trip
tick 200
objdump
load 0
objdump
tick 300
objdump
slots
quit
EOF
for scen in SCG01EA SCU01EA; do
    mkdir -p "$work/a-$scen"
    OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen $scen --pack ra3d/SCG01EA.pack \
        --savedir "$work/a-$scen" --script "$work/a.txt" > "$work/a-$scen.log" 2>&1 \
        || fail "$scen: the run failed"
    grep -Eq "^SAVE\|slot=0\|round trip\|bytes=[1-9][0-9]*\|frame=300\|scen=$scen\|" "$work/a-$scen.log" \
        || fail "$scen: no save at frame 300"
    grep -Eq "^LOAD\|slot=0\|frame=300\|scen=$scen\|" "$work/a-$scen.log" || fail "$scen: no load back to frame 300"
    grep -Fq "SLOT|0|round trip|scen=$scen|" "$work/a-$scen.log" || fail "$scen: the slot is not listed"
    frames=$(sed -n 's/^OBJDUMP-END count=[0-9]* frame=\([0-9]*\)$/\1/p' "$work/a-$scen.log" | paste -sd' ' -)
    [ "$frames" = "300 500 300 600" ] || fail "$scen: dump frames [$frames], want [300 500 300 600]"
    dumps "$work/a-$scen.log" 1 > "$work/a1"; dumps "$work/a-$scen.log" 2 > "$work/a2"
    dumps "$work/a-$scen.log" 3 > "$work/a3"; dumps "$work/a-$scen.log" 4 > "$work/a4"
    lines=$(wc -l < "$work/a1" | tr -d ' ')
    [ "$lines" -ge 40 ] || fail "$scen: only $lines dump lines"
    cmp -s "$work/a1" "$work/a3" || fail "$scen: the world after the load is not the world that was saved: $(diff "$work/a1" "$work/a3" | head -3 | tr '\n' ' ')"
    moved=$(diff "$work/a1" "$work/a2" | grep -c '^>' || true)
    [ "$moved" -ge 1 ] || fail "$scen: the world stood still between the save and the load, so the round trip proves nothing"
    on=$(diff "$work/a3" "$work/a4" | grep -c '^>' || true)
    [ "$on" -ge 1 ] || fail "$scen: the loaded game did not play on"
    echo "gate-ra-save: PASS: $scen saved at frame 300 and loaded back after 200 more ticks with all $lines dump lines identical ($moved had changed in between), then played on to frame 600 ($on lines moved)"
done

# ---- B ------------------------------------------------------------------------------
mkdir -p "$work/b"
printf 'tick 120\nobjdump\nsave 0 cnc slot\nquit\n' > "$work/b-td1.txt"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $TD --scen SCG01EA --pack SCG01EA.pack --savedir "$work/b" \
    --script "$work/b-td1.txt" > "$work/b-td1.log" 2>&1 || fail "Tiberian Dawn's save run failed"
grep -Eq '^SAVE\|slot=0\|cnc slot\|.*\|scen=SCG01EA\|' "$work/b-td1.log" || fail "Tiberian Dawn did not save"
printf 'tick 60\nslots\nload 0\nsave 0 red alert over it\nsave 1 red alert slot\nslots\nquit\n' > "$work/b-ra.txt"
# The refused load and the refused save are counted as script failures, on purpose, so this
# run's exit status says nothing; its lines are what is asserted, including the count.
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen SCG01EA --pack ra3d/SCG01EA.pack --savedir "$work/b" \
    --script "$work/b-ra.txt" > "$work/b-ra.log" 2>&1 || true
grep -Eq '^SCRIPT\|end .*: 7 lines, 2 failures$' "$work/b-ra.log" || fail "Red Alert's run in the shared folder did not run to its end"
grep -Fq 'SLOTS|0 occupied of 16' "$work/b-ra.log" || fail "Red Alert listed Tiberian Dawn's slot"
grep -Fq 'LOAD|slot=0|REFUSED saved by Tiberian Dawn' "$work/b-ra.log" || fail "Red Alert did not refuse Tiberian Dawn's slot"
grep -Fq 'SAVE|slot=0|REFUSED it holds a Tiberian Dawn save' "$work/b-ra.log" || fail "Red Alert would save over Tiberian Dawn's slot"
grep -Eq '^SAVE\|slot=1\|red alert slot\|.*\|scen=SCG01EA\|' "$work/b-ra.log" || fail "Red Alert did not save into slot 1"
grep -Fq 'SLOTS|1 occupied of 16' "$work/b-ra.log" || fail "Red Alert does not list exactly its own slot"
printf 'tick 1\nslots\nload 0\nobjdump\nload 1\nquit\n' > "$work/b-td2.txt"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $TD --scen SCG01EA --pack SCG01EA.pack --savedir "$work/b" \
    --script "$work/b-td2.txt" > "$work/b-td2.log" 2>&1 || true
grep -Eq '^SCRIPT\|end .*: 6 lines, 1 failures$' "$work/b-td2.log" || fail "Tiberian Dawn's load run did not run to its end"
grep -Fq 'SLOTS|1 occupied of 16' "$work/b-td2.log" || fail "Tiberian Dawn does not list exactly its own slot"
grep -Fq 'SLOT|0|cnc slot|scen=SCG01EA|' "$work/b-td2.log" || fail "Tiberian Dawn lost its slot"
grep -Eq '^LOAD\|slot=0\|frame=120\|scen=SCG01EA\|' "$work/b-td2.log" || fail "Tiberian Dawn did not load its slot"
grep -Fq 'LOAD|slot=1|REFUSED saved by Red Alert' "$work/b-td2.log" || fail "Tiberian Dawn did not refuse Red Alert's slot"
dumps "$work/b-td1.log" 1 | grep -v '^HOUSE|' > "$work/b1"
dumps "$work/b-td2.log" 1 | grep -v '^HOUSE|' > "$work/b2"
cmp -s "$work/b1" "$work/b2" || fail "Tiberian Dawn's slot did not load back exactly"
# A Red Alert record from before the game tag (byte 103 zero, as every older record holds)
# must still be told apart by its pack, which is the module's.
python3 - "$work/b/OPENCNC.IDX" <<'PY'
import sys
p = sys.argv[1]
d = bytearray(open(p, "rb").read())
r = d[128:256]
assert r[72:77] == b"ra3d/", r[72:96]
assert d[128 + 103] == 1, d[128 + 103]
d[128 + 103] = 0
open(p, "wb").write(bytes(d))
PY
printf 'tick 1\nslots\nload 1\nquit\n' > "$work/b-td3.txt"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $TD --scen SCG01EA --pack SCG01EA.pack --savedir "$work/b" \
    --script "$work/b-td3.txt" > "$work/b-td3.log" 2>&1 || true
grep -Fq 'SLOTS|1 occupied of 16' "$work/b-td3.log" || fail "Tiberian Dawn listed an untagged Red Alert slot"
grep -Fq 'LOAD|slot=1|REFUSED saved by Red Alert' "$work/b-td3.log" || fail "Tiberian Dawn did not recognise an untagged Red Alert slot by its pack"
echo "gate-ra-save: PASS: one folder, two games: Red Alert saw none of Tiberian Dawn's SCG01EA slot, refused to load it or save over it and saved into slot 1; Tiberian Dawn saw only its own slot, refused Red Alert's, and loaded its own exactly ($(wc -l < "$work/b1" | tr -d ' ') lines)"

# ---- C ------------------------------------------------------------------------------
mkdir -p "$work/c"
printf 'tick 200\nsave 0 soviet slot\nquit\n' > "$work/c-save.txt"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen SCU01EA --pack ra3d/SCG01EA.pack --savedir "$work/c" \
    --script "$work/c-save.txt" > "$work/c-save.log" 2>&1 || fail "the Soviet 1 save run failed"
grep -Eq '^SAVE\|slot=0\|soviet slot\|.*\|frame=200\|scen=SCU01EA\|' "$work/c-save.log" || fail "no Soviet 1 slot to load"
OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- \
    --harness 60 --rounds 1 --no-movies --nosound --savedir "$work/c" \
    --autoesc 2 --autoload --shotdir "$work/c" > "$work/c.log" 2>&1 || true
grep -Fq 'RA3D|start|SCG01EA|original mission' "$work/c.log" || fail "the shell did not start Allied 1"
grep -Fq 'LOAD|slot=0|saved in SCU01EA, running SCG01EA: handing over to the shell' "$work/c.log" \
    || fail "the pause dialog did not hand Soviet 1's slot to the shell"
grep -Fq 'APP|load|slot=0|scen=SCU01EA|' "$work/c.log" || fail "the shell did not boot the slot's mission"
grep -Fq '|game=ra3d' "$work/c.log" || fail "the shell booted the slot outside the Red Alert engine"
grep -Eq '^LOAD\|slot=0\|frame=200\|scen=SCU01EA\|' "$work/c.log" || fail "the slot did not load into Soviet 1"
echo "gate-ra-save: PASS: the shell: a Soviet 1 slot loaded from Allied 1's pause dialog handed over, booted Soviet 1 through the Red Alert engine and loaded there at frame 200"
