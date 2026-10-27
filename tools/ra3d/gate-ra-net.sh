#!/bin/sh
# gate-ra-net.sh [install-dir] -- a Red Alert match between two copies of the game on this
# machine, over loopback, through the same network path Tiberian Dawn's matches use.
#
# Hidden throughout (OPENCNC3D_BACKGROUND). Each copy runs its own Red Alert brain; the two
# arm one match from the host's handshake and run it in lockstep, the world hashed after
# every tick and the hashes exchanged every fifteen frames.
#
#   A  FIVE MINUTES, TWO PEOPLE AND TWO COMPUTERS: First Come First Serve (SCM10EA,
#      temperate), the host plays the USSR and the joiner Greece, England and Germany are the
#      computer, 10000 credits. The host deploys its MCV and builds and places a Power Plant;
#      the joiner deploys. Both must end cleanly, at least 300 hash reports agree between the
#      two and none disagree, no desync alarm, both name the same four countries, and the
#      joiner's world holds the Power Plant the host placed: an order that crossed the wire
#      and ran on a machine that never clicked.
#   B  THE SNOW: Shallow Grave (SCM08EA), the host Greece and the joiner the USSR, the same
#      Power Plant crossing, at least 10 reports agreeing and none disagreeing.
#   In both, the two engines start on the host's seed, not zero, and the two matches' seeds
#   differ.
#   C  NOT TIBERIAN DAWN: a Tiberian Dawn copy joining a Red Alert host is refused by both ends
#      for its order wire, and never reaches a match.
#   D  THE MAP'S BYTES: a Red Alert joiner whose SCM10EA.INI differs by one comment line is
#      refused for the map's bytes, and never reaches a match.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
install=$(CDPATH= cd -- "$install" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-net.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$install"
fail() { echo "gate-ra-net: FAIL: $1" >&2; exit 1; }
[ -f ra3d/RedAlert.dylib ] || fail "no Red Alert module is staged in $install"

RA="--ra3d --dylib ra3d/RedAlert.dylib --content ra3d/data --dir ra3d/missions/ --dosdata ra3d/audio --nosound --w 640 --h 400"
TD="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/ --nosound --w 640 --h 400"

host_script() {   # host_script FILE TICKS
    printf 'tick 5\nlclickobj MCV:@me\nactclickobj MCV:@me\ntick 250\nbuild POWR\nready POWR 1500\nstartplace POWR\nplace auto\ntick %s\nobjdump\nquit\n' "$2" > "$1"
}
printf 'tick 5\nlclickobj MCV:@me\nactclickobj MCV:@me\ntick 9000\nobjdump\nquit\n' > "$work/join.txt"
printf 'tick 30\nquit\n' > "$work/short.txt"

# the match: both logs, then the hash reports compared
match() {   # match LEG PORT SCEN HOSTFLAGS
    leg=$1 port=$2 scen=$3 flags=$4
    OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen "$scen" --pack "ra3d/3d/$scen.pack" --skirmish $flags \
        --host "$port" --script "$work/$leg-host.txt" > "$work/$leg-host.log" 2>&1 &
    hp=$!
    sleep 2
    OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen "$scen" --pack "ra3d/3d/$scen.pack" --skirmish \
        --join 127.0.0.1 "$port" --script "$work/join.txt" > "$work/$leg-join.log" 2>&1 &
    jp=$!
    hrc=0; jrc=0
    wait $hp || hrc=$?
    wait $jp || jrc=$?
    [ "$hrc" = "0" ] && [ "$jrc" = "0" ] || fail "$leg: host exit $hrc, joiner exit $jrc"
    grep '^NETSYNC|' "$work/$leg-host.log" | sort -u > "$work/$leg-h"
    grep '^NETSYNC|' "$work/$leg-join.log" | sort -u > "$work/$leg-j"
    agree=$(comm -12 "$work/$leg-h" "$work/$leg-j" | grep -c . || true)
    sort -t'|' -k2,2 "$work/$leg-h" > "$work/$leg-hs"
    sort -t'|' -k2,2 "$work/$leg-j" > "$work/$leg-js"
    differ=$(join -t'|' -j2 "$work/$leg-hs" "$work/$leg-js" | awk -F'|' '$3 != $5' | grep -c . || true)
    alarms=$(cat "$work/$leg-host.log" "$work/$leg-join.log" | grep -c '^NETDESYNC|' || true)
    [ "$differ" = "0" ] || fail "$leg: $differ hash reports disagree"
    [ "$alarms" = "0" ] || fail "$leg: $alarms desync alarms: $(grep -h '^NETDESYNC|' "$work/$leg-host.log" "$work/$leg-join.log" | head -1)"
    grep '^skirmish: seat=' "$work/$leg-host.log" > "$work/$leg-seats-h"
    grep '^skirmish: seat=' "$work/$leg-join.log" > "$work/$leg-seats-j"
    cmp -s "$work/$leg-seats-h" "$work/$leg-seats-j" || fail "$leg: the two seat tables differ: $(diff "$work/$leg-seats-h" "$work/$leg-seats-j" | head -2 | tr '\n' ' ')"
    # THE SEED IS THE HOST'S: drawn by it, carried in the setup, and both engines start on it.
    hseed=$(sed -n 's/^skirmish: .*, seed \([0-9]*\)$/\1/p' "$work/$leg-host.log" | head -1)
    jseed=$(sed -n 's/^skirmish: .*, seed \([0-9]*\)$/\1/p' "$work/$leg-join.log" | head -1)
    [ -n "$hseed" ] && [ "$hseed" != "0" ] && [ "$hseed" = "$jseed" ] || fail "$leg: seeds host=$hseed joiner=$jseed"
    plant=$(grep '^OBJ|BUILDING|POWR|Multi1|' "$work/$leg-join.log" | grep -c '|limbo=0|' || true)
    [ "$plant" -ge 1 ] || fail "$leg: the host's Power Plant is not in the joiner's world"
}

# ---- A ------------------------------------------------------------------------------
host_script "$work/a-host.txt" 4500
match a 18751 SCM10EA "--side soviet --ai 2 --credits 10000"
[ "$agree" -ge 300 ] || fail "A: only $agree hash reports agree"
for c in USSR Greece England Germany; do
    grep -Fq "|house=$c|" "$work/a-seats-h" || fail "A: no seat plays $c"
done
grep -Fq 'skirmish: 4 player(s), Red Alert, human plays the Allies' "$work/a-join.log" || fail "A: the joiner is not the Allies"
echo "gate-ra-net: PASS: First Come First Serve between two copies, the USSR and Greece against England and Germany: $agree hash reports agree and none disagree over five minutes, no desync alarm, one seat table on both, both engines on the host's seed $hseed, and the host's Power Plant stands in the joiner's world"
a_agree=$agree
a_seed=$hseed

# ---- B ------------------------------------------------------------------------------
host_script "$work/b-host.txt" 150
match b 18752 SCM08EA "--side allies --ai 0"
[ "$agree" -ge 10 ] || fail "B: only $agree hash reports agree"
grep -Fq '|house=Greece|' "$work/b-seats-h" && grep -Fq '|house=USSR|' "$work/b-seats-h" || fail "B: the seats are not Greece and the USSR"
[ "$hseed" != "$a_seed" ] || fail "B: two matches were given the same seed"
echo "gate-ra-net: PASS: Shallow Grave between two copies, Greece and the USSR: $agree hash reports agree and none disagree, both on seed $hseed (not the first match's), and the host's Power Plant stands in the joiner's world"

# ---- C ------------------------------------------------------------------------------
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen SCM10EA --pack ra3d/3d/SCM10EA.pack --skirmish --ai 0 \
    --host 18753 --script "$work/short.txt" > "$work/c-host.log" 2>&1 &
hp=$!
sleep 2
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $TD --scen SCM01EA --pack SCM01EA.pack --skirmish --side nod --ai 0 \
    --join 127.0.0.1 18753 --script "$work/short.txt" > "$work/c-join.log" 2>&1 || true
kill -9 $hp 2>/dev/null || true
wait $hp 2>/dev/null || true
grep -q '^NET|refused|.*reason=order-wire layout' "$work/c-host.log" || fail "C: the Red Alert host did not refuse a Tiberian Dawn joiner"
grep -q '^NET|refused-by-host|reason=order-wire layout' "$work/c-join.log" || fail "C: the Tiberian Dawn joiner was not told why"
! grep -q '^NET|match|seat=1' "$work/c-join.log" || fail "C: the Tiberian Dawn joiner reached a match"
echo "gate-ra-net: PASS: a Tiberian Dawn copy joining a Red Alert host is refused by both for its order wire and never reaches a match"

# ---- D ------------------------------------------------------------------------------
mkdir -p "$work/missions"
cp ra3d/missions/SCM10EA.INI ra3d/missions/RULES.INI "$work/missions/"
printf '; these bytes differ and mean the same thing\n' >> "$work/missions/SCM10EA.INI"
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen SCM10EA --pack ra3d/3d/SCM10EA.pack --skirmish --ai 0 \
    --host 18754 --script "$work/short.txt" > "$work/d-host.log" 2>&1 &
hp=$!
sleep 2
OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --dir "$work/missions/" --scen SCM10EA --pack ra3d/3d/SCM10EA.pack \
    --skirmish --join 127.0.0.1 18754 --script "$work/short.txt" > "$work/d-join.log" 2>&1 || true
kill -9 $hp 2>/dev/null || true
wait $hp 2>/dev/null || true
grep -q '^NET|refused|.*reason=map bytes' "$work/d-host.log" || fail "D: the host did not refuse a joiner holding other map bytes"
grep -q '^NET|refused-by-host|reason=map bytes' "$work/d-join.log" || fail "D: the joiner was not told why"
! grep -q '^NET|match|seat=1' "$work/d-join.log" || fail "D: the joiner reached a match"
echo "gate-ra-net: PASS: a Red Alert joiner whose SCM10EA.INI differs by one comment line is refused for the map's bytes and never reaches a match"
echo "gate-ra-net: all four legs pass ($a_agree reports agreeing over the long match)"
