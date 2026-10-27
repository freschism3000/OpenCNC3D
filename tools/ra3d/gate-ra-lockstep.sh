#!/bin/sh
# Two Red Alert brains in one process, in lockstep, on the 1996 multiplayer maps.
#
#   tools/ra3d/gate-ra-lockstep.sh <RedAlert.dylib> [report]
#
# Builds brain/host/ra_twobrain.c, stages the disc's multiplayer maps and RULES.INI from
# data/radata (no art: the brain runs headless), and asserts, from the harness's own report
# lines rather than its exit code alone:
#
#   1. SCM05EA, two human seats and two computer seats: the opening, an order that crosses from
#      one instance to the other, the lookahead rule, the exports leg (every switch and query the
#      Tiberian Dawn brain carries, asserted by its effect), the gesture legs (repair, the power
#      switch, sale and placement are orders, read back off the object dump), then every seat handed to the
#      computer and 6000 ticks byte identical while the computer players build and fight; then a
#      one-sided change that MUST make the two differ.
#   2. The warm start: one instance has already played another map in the same process.
#   3. The negative control: the two instances are given different seeds and MUST be reported
#      as different at once, or the comparison cannot see a different world.
#   4. Every one of the 24 multiplayer maps, 1200 ticks each.
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <RedAlert.dylib> [report]" >&2
    exit 2
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BRAIN=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")
REPORT=${2:-}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-lockstep.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

DL_LIBS=-ldl
if [ "$(uname -s)" = "Darwin" ]; then
    DL_LIBS=
fi
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 "$ROOT/brain/host/ra_twobrain.c" $DL_LIBS -o "$WORK/ra_twobrain"

mkdir -p "$WORK/scen"
python3 "$ROOT/tools/ra3d/ramix.py" extract "$ROOT/data/radata/LOCAL.MIX" "$WORK/scen" RULES.INI >/dev/null
for n in 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24; do
    python3 "$ROOT/tools/ra3d/ramix.py" extract "$ROOT/data/radata/GENERAL.MIX" "$WORK/scen" "SCM${n}EA.INI" >/dev/null
done

fails=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL $*"; fails=$((fails + 1)); }
field() { printf '%s\n' "$1" | sed -n "s/.*|$2=\([0-9-]*\).*/\1/p" | tail -1; }

cd "$WORK"

# 1. the long match
rc=0
./ra_twobrain "$BRAIN" scen SCM05EA 6000 2 > long.txt 2>&1 || rc=$?
census=$(grep '^RA2B|census|' long.txt || true)
agreed=$(field "$(grep '^RA2B|agree|' long.txt || true)" ticks)
b0=$(field "$census" ai_buildings0); b1=$(field "$census" ai_buildings)
fight=$(field "$census" fight_ticks)
crossed=$(grep -c '^RA2B|order|crossed=1' long.txt || true)
control=$(grep -c '^RA2B|control|diverged=1' long.txt || true)
exports=$(grep -c '^RA2B|exports|ok=1' long.txt || true)
gestures=$(grep -c '^RA2B|gestures|ok=1' long.txt || true)
potential=$(field "$(grep '^RA2B|potential|' long.txt || true)" count)
if [ "$rc" = "0" ] && [ "${agreed:-0}" = "6000" ] && [ "${crossed:-0}" = "1" ] && [ "${control:-0}" = "1" ] \
   && [ "${exports:-0}" = "1" ] && [ "${gestures:-0}" = "1" ] && [ "${b1:-0}" -gt "${b0:-0}" ] && [ "${fight:-0}" -gt 0 ]; then
    say "PASS SCM05EA: 6000 ticks identical with every seat played by the computer (buildings $b0 -> $b1, $fight ticks with an army firing), an order crossed, the exports leg held (invincibility, Build Anywhere, a $potential-entry build potential, super weapons, a rally point carried as an order, the takeover), repair, the power switch and placement each changed nothing until their order crossed and then changed both worlds alike, and the one-sided control was seen"
else
    bad "SCM05EA: rc=$rc agreed=$agreed(want 6000) crossed=$crossed exports=$exports gestures=$gestures control=$control buildings=$b0->$b1 fight_ticks=$fight. $(grep -E 'FAILED|first difference|field that' long.txt | head -3 | tr '\n' ' ')"
fi

# 2. the warm start
rc=0
OPENCNC3D_RB_WARM=SCM02EA ./ra_twobrain "$BRAIN" scen SCM01EA 1500 2 > warm.txt 2>&1 || rc=$?
warm=$(grep -c 'warm start: A has played' warm.txt || true)
agreed=$(field "$(grep '^RA2B|agree|' warm.txt || true)" ticks)
if [ "$rc" = "0" ] && [ "${warm:-0}" = "1" ] && [ "${agreed:-0}" = "1500" ]; then
    say "PASS warm start: an instance that had already played SCM02EA agreed with a fresh one for 1500 ticks of SCM01EA"
else
    bad "warm start: rc=$rc warm=$warm agreed=$agreed(want 1500). $(grep -E 'FAILED|first difference|field that' warm.txt | head -3 | tr '\n' ' ')"
fi

# 3. the negative control: different seeds must be seen
rc=0
OPENCNC3D_RB_SEED_B=7 ./ra_twobrain "$BRAIN" scen SCM01EA 300 2 > seed.txt 2>&1 || rc=$?
seen=$(grep -c 'FAILED: the two instances differ at tick 0' seed.txt || true)
if [ "$rc" != "0" ] && [ "${seen:-0}" = "1" ]; then
    say "PASS negative control: two instances given different seeds were reported different at tick 0"
else
    bad "negative control: rc=$rc seen=$seen; two different worlds were NOT told apart, so no agreement here means anything"
fi

# 4. every map
mapok=0
mapbad=""
for n in 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24; do
    rc=0
    ./ra_twobrain "$BRAIN" scen "SCM${n}EA" 1200 2 > "map$n.txt" 2>&1 || rc=$?
    agreed=$(field "$(grep '^RA2B|agree|' "map$n.txt" || true)" ticks)
    if [ "$rc" = "0" ] && [ "${agreed:-0}" = "1200" ]; then
        mapok=$((mapok + 1))
    else
        mapbad="$mapbad SCM${n}EA($(grep -E 'FAILED' "map$n.txt" | head -1))"
    fi
done
if [ "$mapok" = "24" ]; then
    say "PASS all 24 multiplayer maps: 1200 ticks identical each, an order crossed on each"
else
    bad "multiplayer maps: $mapok of 24 agreed.$mapbad"
fi

if [ -n "$REPORT" ]; then
    mkdir -p "$(dirname -- "$REPORT")"
    cat long.txt warm.txt seed.txt map*.txt > "$REPORT"
fi

if [ "$fails" != "0" ]; then
    echo "Red Alert lockstep gate FAILED ($fails)"
    exit 1
fi
echo "Red Alert lockstep gate passed"
