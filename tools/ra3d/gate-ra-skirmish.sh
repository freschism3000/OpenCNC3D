#!/bin/sh
# gate-ra-skirmish.sh [install-dir] -- a Red Alert skirmish: started, played by the computer for
# half an hour, and the same match every time it is given the same seed.
#
# Hidden throughout (OPENCNC3D_BACKGROUND), in scratch save folders, never the player's.
# 27000 ticks is thirty minutes at the engine's 15 ticks a second; an object dump every 1500.
#
#   A  THE LIST: ra3d/skirmish-maps.txt names the disc's 24 multiplayer maps, each with at
#      least two start positions and its picture beside it, a 120 x 120 PNG.
#   B  HALF AN HOUR, TWICE: Shallow Grave (SCM08EA, snow), the person as Greece against the
#      USSR, England and Ukraine played by the computer, 10000 credits, seed 4242. The two runs
#      must agree on every line of all 18 dumps; the computers must build (their building
#      count grows by at least 20) and fight (in at least three dumps their units have targets
#      and something on the map is damaged).
#   C  THE SEED IS SEEN: the same match on seed 4243 must differ, or the agreement in B could
#      be two runs that ignore the seed.
#   D  THE SHELL: the Red Alert module started with --raskirmish on First Come First Serve
#      (SCM10EA, temperate), the person as the USSR against three Allied computers, goes
#      through the call a skirmish screen makes (ra_skirmish_apply) and must play exactly the
#      match the renderer plays from the command line with the same seats and seed.
#   E  THE COMPUTERS' DIFFICULTY: the match of B for ten minutes with --difficulty easy, normal
#      and hard and expert. Normal must match the default; behavior must differ while all
#      seats retain neutral difficulty handicaps. Per-seat skills are decisions, not stats.
#   F  THE DEBRIEF: the shell plays Shallow Grave against one hard computer twice in a row
#      until it has beaten the idle person, and each match ends on Red Alert's debrief:
#      the person beaten, the computer's battle won and kills, the session's columns
#      moving on, and the screen drawn. B covers combat among three independent computers.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
install=$(CDPATH= cd -- "$install" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-skirmish.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$install"
fail() { echo "gate-ra-skirmish: FAIL: $1" >&2; exit 1; }
[ -f ra3d/RedAlert.dylib ] || fail "no Red Alert module is staged in $install"

RA="--ra3d --dylib ra3d/RedAlert.dylib --content ra3d/data --dir ra3d/missions/ --dosdata ra3d/audio --nosound --w 640 --h 400"
i=0
: > "$work/long.txt"
while [ $i -lt 18 ]; do printf 'tick 1500\nobjdump\n' >> "$work/long.txt"; i=$((i + 1)); done
echo quit >> "$work/long.txt"

# objects and houses of every dump, each line led by its dump's number
dumps() { awk '/^OBJDUMP-BEGIN/{n++} n>=1&&/^(OBJ\||HOUSE\|)/{print n"|"$0}' "$1"; }
# one line a dump: n computer_buildings computer_units targeting damaged
census() {
    awk -F'|' -v human="$2" '
        /^OBJDUMP-BEGIN/ { n++; b[n] = 0; u[n] = 0; t[n] = 0; d[n] = 0; next }
        n && /^OBJ\|/ {
            own = $4; str = -1; max = -1; tar = "-1"
            for (k = 5; k <= NF; k++) {
                split($k, kv, "=")
                if (kv[1] == "str") str = kv[2] + 0
                else if (kv[1] == "max") max = kv[2] + 0
                else if (kv[1] == "tar") tar = kv[2]
            }
            if (str >= 0 && str < max) d[n]++
            if (own !~ /^Multi/ || own == human) next
            if ($2 == "BUILDING") b[n]++
            else { u[n]++; if (tar != "-1") t[n]++ }
        }
        END { for (k = 1; k <= n; k++) print k, b[k], u[k], t[k], d[k] }' "$1"
}
skirmish() {   # skirmish LOG SCEN SEED [flags...]
    log=$1 scen=$2 seed=$3
    shift 3
    OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen "$scen" --pack "ra3d/3d/$scen.pack" --skirmish \
        --seed "$seed" --credits 10000 --savedir "$work" --script "$work/long.txt" "$@" > "$log" 2>&1 \
        || fail "$scen seed $seed: the run failed"
    grep -Eq '^SCRIPT\|end .*: 37 lines, 0 failures$' "$log" || fail "$scen seed $seed: the script did not run to its end"
    [ "$(grep -c '^OBJDUMP-END' "$log")" = "18" ] || fail "$scen seed $seed: not 18 dumps"
    grep -Eq '^skirmish: colours\|[0-7],[0-7],[0-7],[0-7]\|duplicates=0$' "$log" || fail "$scen seed $seed: the seats are not in four colours"
    grep -Fq 'skirmish: teams|lobby=1|engine=1|agree' "$log" || fail "$scen seed $seed: the engine's alliances are not the seats'"
}

# ---- A ------------------------------------------------------------------------------
[ -f ra3d/skirmish-maps.txt ] || fail "ra3d/skirmish-maps.txt is missing (tools/ra3d/bake-ra-map-previews.py)"
maps=$(grep -vc '^#' ra3d/skirmish-maps.txt)
[ "$maps" = "24" ] || fail "the map list names $maps maps, not 24"
python3 - ra3d <<'PY' || fail "a map in the list has no picture, or fewer than two starts"
import sys, os, struct
mod = sys.argv[1]
for line in open(os.path.join(mod, "skirmish-maps.txt")):
    if line.startswith("#"): continue
    f = line.rstrip("\n").split("|")
    assert int(f[4]) >= 2 and len(f[6].split(";")) == int(f[4]), f
    png = open(os.path.join(mod, f[5]), "rb").read(32)
    assert png[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack(">II", png[16:24]) == (120, 120), f[5]
PY
echo "gate-ra-skirmish: PASS: the map list names the 24 multiplayer maps, each with its start positions and a 120 x 120 picture"

# ---- B ------------------------------------------------------------------------------
SEATS="--ai 3 --countries greece,ussr,england,ukraine"
skirmish "$work/b1.log" SCM08EA 4242 $SEATS &
p1=$!
skirmish "$work/b2.log" SCM08EA 4242 $SEATS &
p2=$!
skirmish "$work/c.log" SCM08EA 4243 $SEATS &
p3=$!
wait $p1 || exit 1
wait $p2 || exit 1
wait $p3 || exit 1
grep -Fq 'skirmish: 4 player(s), Red Alert, human plays the Allies, 10000 credits' "$work/b1.log" \
    || fail "SCM08EA did not start as a four-seat Red Alert skirmish"
dumps "$work/b1.log" > "$work/b1"
dumps "$work/b2.log" > "$work/b2"
lines=$(wc -l < "$work/b1" | tr -d ' ')
cmp -s "$work/b1" "$work/b2" || fail "two runs on seed 4242 differ: $(diff "$work/b1" "$work/b2" | head -2 | tr '\n' ' ')"
census "$work/b1.log" Multi1 > "$work/census"
b0=$(awk 'NR==1{print $2}' "$work/census")
bmax=$(awk 'BEGIN{m=0} $2>m{m=$2} END{print m}' "$work/census")
fights=$(awk '$4>0 && $5>0{n++} END{print n+0}' "$work/census")
tmax=$(awk 'BEGIN{m=0} $4>m{m=$4} END{print m}' "$work/census")
[ $((bmax - b0)) -ge 20 ] || fail "the computers did not build: $b0 -> $bmax buildings"
[ "$fights" -ge 3 ] || fail "the computers did not fight: $fights dumps with targets and damage"
echo "gate-ra-skirmish: PASS: Shallow Grave, thirty minutes twice on seed 4242: all $lines dump lines identical; the computers built from $b0 to $bmax buildings and fought in $fights of 18 dumps (up to $tmax units with a target)"

# ---- C ------------------------------------------------------------------------------
dumps "$work/c.log" > "$work/c"
diffs=$(diff "$work/b1" "$work/c" | grep -c '^>' || true)
[ "$diffs" -ge 1 ] || fail "seed 4243 played the same match as seed 4242, so the agreement proves nothing"
echo "gate-ra-skirmish: PASS: seed 4243 played another match ($diffs dump lines differ)"

# ---- D ------------------------------------------------------------------------------
# The launcher forwards at most 16 arguments after "--", so this list stays under that;
# OPENCNC3D_BACKGROUND alone keeps the window hidden.
OPENCNC3D_BACKGROUND=1 ./opencnc3d-launcher --dir . --module ra3d --play -- --no-movies --nosound \
    --raskirmish SCM10EA --side soviet --ai 3 --seed 4242 --credits 10000 \
    --savedir "$work" --script "$work/long.txt" > "$work/d.log" 2>&1 || fail "the shell's run failed"
grep -Fq 'APP|raskirmish|24 maps|listed' "$work/d.log" || fail "the shell did not read the map list"
grep -Fq 'APP|raskirmish|start|SCM10EA|players=4|tech=10|credits=10000|ore=1|' "$work/d.log" \
    || fail "the shell did not start SCM10EA as asked"
grep -Fq 'APP|raskirmish|seat=0|person|country=USSR|' "$work/d.log" || fail "the person is not the USSR"
for c in England Germany France; do
    grep -Eq "^APP\|raskirmish\|seat=[123]\|computer\|country=$c\|" "$work/d.log" || fail "no computer plays $c"
done
grep -Fq 'APP|raskirmish|done|reason=' "$work/d.log" || fail "the shell's match did not end with its script"
skirmish "$work/d2.log" SCM10EA 4242 --ai 3 --side soviet --countries ussr,england,germany,france --colours 2,3,5,6
dumps "$work/d.log" > "$work/d1"
dumps "$work/d2.log" > "$work/d2"
[ "$(grep -c '^OBJDUMP-END' "$work/d.log")" = "18" ] || fail "the shell's match did not dump 18 times"
cmp -s "$work/d1" "$work/d2" || fail "the shell's match is not the command line's: $(diff "$work/d1" "$work/d2" | head -2 | tr '\n' ' ')"
census "$work/d.log" Multi1 > "$work/dcensus"
db0=$(awk 'NR==1{print $2}' "$work/dcensus")
dbmax=$(awk 'BEGIN{m=0} $2>m{m=$2} END{print m}' "$work/dcensus")
echo "gate-ra-skirmish: PASS: the shell started First Come First Serve as the USSR against England, Germany and France through ra_skirmish_apply and played the command line's match to the line ($(wc -l < "$work/d1" | tr -d ' ') lines; the computers built from $db0 to $dbmax buildings)"

# ---- E ------------------------------------------------------------------------------
: > "$work/ten.txt"
i=0
while [ $i -lt 6 ]; do printf 'tick 1500\nobjdump\naidump\n' >> "$work/ten.txt"; i=$((i + 1)); done
echo quit >> "$work/ten.txt"
for d in none easy normal hard expert; do
    flag=""; [ "$d" = none ] || flag="--difficulty $d"
    OPENCNC3D_BACKGROUND=1 ./cnc_eyes $RA --scen SCM08EA --pack ra3d/3d/SCM08EA.pack --skirmish --seed 4242 \
        --credits 10000 $SEATS $flag --script "$work/ten.txt" > "$work/e-$d.log" 2>&1 &
    eval "pe_$d=\$!"
done
for d in none easy normal hard expert; do eval "wait \$pe_$d" || fail "E: the $d run failed"; done
for d in easy normal hard expert; do
    case "$d" in easy) label=Easy;; normal) label=Normal;; hard) label=Hard;; expert) label=Expert;; esac
    grep -q "^FAIRAI|.*|name=$label|" "$work/e-$d.log" || fail "E: the $d run did not set its computers"
    ! grep '^FAIRRULE|' "$work/e-$d.log" | grep -v '|difficulty=1|' >/dev/null || fail "E: $d applied a stat handicap"
done
dumps "$work/e-none.log" > "$work/en"; dumps "$work/e-normal.log" > "$work/eN"
dumps "$work/e-easy.log" > "$work/ee"; dumps "$work/e-hard.log" > "$work/eh"
cmp -s "$work/en" "$work/eN" || fail "E: normal is not the match with no difficulty said"
! cmp -s "$work/ee" "$work/eN" || fail "E: easy played the normal match"
! cmp -s "$work/eh" "$work/eN" || fail "E: hard played the normal match"
dumps "$work/e-expert.log" > "$work/ex"
! cmp -s "$work/ex" "$work/eN" || fail "E: expert played the normal match"
echo "gate-ra-skirmish: PASS: Easy, Normal, Hard and Expert use distinct behavior with neutral rule handicaps; Normal matches the default"

# ---- F ------------------------------------------------------------------------------
: > "$work/beaten.txt"
i=0
while [ $i -lt 13 ]; do printf 'tick 1500\n' >> "$work/beaten.txt"; i=$((i + 1)); done
echo quit >> "$work/beaten.txt"
mkdir -p "$work/f"
OPENCNC3D_BACKGROUND=1 ./opencnc3d --module ra3d --no-movies --nosound --savedir "$work" \
    --raskirmish SCM08EA --ai 1 --difficulty hard --credits 10000 --seed 4242 --rarounds 2 \
    --script "$work/beaten.txt" --shotdir "$work/f" > "$work/f.log" 2>&1 || fail "F: the shell's run failed"
[ "$(grep -c '^APP|radebrief|SCM08EA|Shallow Grave (4)|win=0|decided=1|' "$work/f.log")" = "2" ] \
    || fail "F: two decided matches did not each end on the debrief"
grep -Fq 'APP|radebrief|row=0|PLAYER|country=Greece|colour=Light blue|wins=0|' "$work/f.log" || fail "F: the person's row is wrong"
grep -q '^APP|radebrief|row=0|PLAYER|.*|defeated=1|me=1$' "$work/f.log" || fail "F: the person was not beaten"
grep -Eq '^APP\|radebrief\|row=1\|COMPUTER\|country=USSR\|.*\|wins=2\|kills=([0-9]+),\1,-1,-1\|' "$work/f.log" \
    || fail "F: the computer's row did not carry two won battles and the same kills in both columns"
# Classic draws the debrief in a notice; the Enhanced menu draws its own screen
grep -Eq 'NOTICE\|open\|caption=Mission failed\||RADEBRIEF\|shown\|' "$work/f.log" || fail "F: the debrief was not drawn"
[ -s "$work/f/ra_debrief.png" ] || fail "F: no picture of the debrief"
echo "gate-ra-skirmish: PASS: the debrief: two matches against one hard computer ended with the person beaten, each on Red Alert's debrief, the computer's battles won counted across the session (2) and each match's kills in its own column"
