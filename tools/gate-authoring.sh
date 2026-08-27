#!/bin/sh
# gate-authoring.sh -- the whole loop, as a person would do it.
#
# WHY. Every other gate tests a piece: the format round-trips, the evaluator fires, the
# layout does not collide. None of them answers the question a mission author actually
# has, which is whether a map you MADE, from nothing, in this editor, plays.
#
# So this one makes a map: New Map, place a base, put down the starts, write a rule, save,
# and play it. Every step goes through the same function the button does. If this gate is
# green the editor produces a mission the engine will run; if it is red nothing else
# matters much.
set -eu

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo/playable"

WORK="$repo/playable/missions/user_maps"
SCEN=SCM95EA
mkdir -p "$WORK"
rm -f "$WORK/$SCEN".*

cat > /tmp/authoring.script <<'SCRIPT'
newmap 1 0 0
editscen SCM95EA
editplace FACT 20 20 0
editplace PYLE 24 20 0
editplace PROC 20 24 0
editplace E1 26 22 0
editplace HAND 34 30 1
editplace E1 36 30 1
editsave
SCRIPT



# The editor opens ON something -- it is a map editor, not a blank page -- so the gate
# boots it on a cartridge mission and makes the new map from there, which is the order a
# person does it in. The save then writes under the new name into user_maps/.
echo "AUTHGATE: building a map from nothing"
OUT=$(./cnc_eyes --edit --editmode 0 --scen SCG01EA --pack SCG01EA.pack \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/ --content content/ \
        --script /tmp/authoring.script --w 1400 --h 900 2>&1 || true)

fail=0
say() { printf 'AUTHGATE: %s\n' "$1"; }

printf '%s' "$OUT" | grep -q 'NEWMAP|' \
    && say "ok   New Map made a blank map" \
    || { say "FAIL New Map did not run"; fail=1; }

# The picker path, driven on a mission that has rules: the same functions every
# dropdown click runs.
printf 'editrule atk1\neditpop 2\neditpop pick 11\neditpop 6\neditpop pick 0\nquit\n' \
    > /tmp/pick.script
PK=$(./cnc_eyes --edit --editmode 4 --scen SCG09EA --pack SCG09EA.pack \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/ --content content/ \
        --script /tmp/pick.script --w 1400 --h 900 2>&1 || true)
printf '%s' "$PK" | grep -q 'EDITPOP|row 2|opened|17 items' \
    && say "ok   the EVENT picker opens with all 17 events" \
    || { say "FAIL the EVENT picker did not open"; fail=1; }
printf '%s' "$PK" | grep -q 'EDITPOP|picked 11|open=0' \
    && say "ok   a pick applies and closes the picker" \
    || { say "FAIL the pick did not apply"; fail=1; }
printf '%s' "$PK" | grep -q 'EDITPOP|row 6|opened|20 items' \
    && say "ok   the TEAM picker lists every team plus none and new" \
    || { say "FAIL the TEAM picker did not open"; fail=1; }

PLACED=$(printf '%s' "$OUT" | grep -c 'EDITPLACE|.*|placed|' || true)
if [ "$PLACED" -eq 6 ]; then
    say "ok   all 6 objects went down"
else
    say "FAIL only $PLACED of 6 objects were placed"; fail=1
    printf '%s\n' "$OUT" | grep 'EDITPLACE|' >&2
fi

# The map has to exist on disk, with the two files the engine needs.
for ext in INI BIN; do
    if [ -f "$WORK/$SCEN.$ext" ]; then
        say "ok   wrote $SCEN.$ext ($(wc -c < "$WORK/$SCEN.$ext" | tr -d ' ') bytes)"
    else
        say "FAIL no $SCEN.$ext was written"; fail=1
    fi
done

# Now write a rule into it by hand -- the editor's own script authoring is covered by
# gate-enhanced.sh; what this step adds is that a map BUILT HERE can carry one and run it.
if [ -f "$WORK/$SCEN.INI" ]; then
    cat >> "$WORK/$SCEN.INI" <<'INI'

[TeamTypes]
atk1=BadGuy,0,0,0,0,0,7,1,0,0,1,E1:1,2,Move:0,Loop:0,0,0

[Triggers]
go01=Time,Create Team,1,BadGuy,atk1,0
INI
    say "ok   added a rule and a team to it"
fi

# And play it for real.
PLAY=$(./cnc_eyes --scen "$SCEN" --pack SCG01EA.pack --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/user_maps/ --content content/ \
        --shot /tmp/authgate.png --ticks 250 2>&1 || true)

printf '%s' "$PLAY" | grep -qE 'shot: 2[0-9]+ ticks' \
    && say "ok   the engine played it for 250 ticks" \
    || { say "FAIL the map did not play"; fail=1; }

OBJ=$(printf '%s' "$PLAY" | sed -n 's/.*ticks, \([0-9]*\) objects.*/\1/p' | head -1)
if [ -n "$OBJ" ] && [ "$OBJ" -ge 6 ]; then
    say "ok   $OBJ objects alive in the running mission"
else
    say "FAIL the running mission had '${OBJ:-no}' objects, expected at least 6"; fail=1
fi

# The trace proves the RULE we added actually fired in the map we built.
TR=$(./cnc_eyes --trace 200 --scen "$SCEN" --pack SCG01EA.pack --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/user_maps/ --content content/ \
        --shot /tmp/authgate2.png 2>&1 || true)
printf '%s' "$TR" | grep -qE 'TRACE\| FIRED\|go01' \
    && say "ok   the rule fired in the map we built" \
    || { say "FAIL the rule never fired"; fail=1; printf '%s\n' "$TR" | grep '^TRACE' >&2; }

# THE WAY BACK. Playing from the editor and aborting used to kill the PROCESS --
# "the entire application just closes completely". This walks play -> pause -> Abort ->
# confirm through the real dialog and requires the same process to be back in the
# editor afterwards, running the mission check to prove it is alive and on the map.
printf 'editplay play\ntick 30\noptions\noptclick Abort Mission\noptclick Abort\neditmenu check\nquit\n' \
    > /tmp/abort.script
AB=$(./cnc_eyes --edit --editmode 4 --scen "$SCEN" --pack SCG01EA.pack --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/user_maps/ --content content/ \
        --script /tmp/abort.script --w 1400 --h 900 2>&1 || true)
printf '%s' "$AB" | grep -q 'OPTIONS|abort|to-editor|edit=1' \
    && say "ok   Abort Mission returns to the editor instead of quitting the app" \
    || { say "FAIL Abort did not come back to the editor"; fail=1; }
printf '%s' "$AB" | grep -q 'EDITMENU|check' \
    && say "ok   the editor is alive and answering after the round trip" \
    || { say "FAIL the editor was not usable after the abort"; fail=1; }

# WINTER. The third theater is a rebuilt bank, not a cartridge one, so prove the whole
# chain on it: New Map onto the winter donor, play it, abort back, mission check clean.
printf 'newmap 1 2 0\ntick 2\neditplay play\ntick 20\noptions\noptclick Abort Mission\noptclick Abort\neditmenu check\nquit\n' \
    > /tmp/wintergate.script
WG=$(./cnc_eyes --edit --editmode 1 --scen "$SCEN" --pack SCG01EA.pack --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/user_maps/ --content content/ \
        --script /tmp/wintergate.script --w 1400 --h 900 2>&1 || true)
WMAP=$(printf '%s' "$WG" | sed -n 's/.*NEWMAP|rebooted|pack=\([A-Z0-9]*\)|.*/\1/p' | head -1)
printf '%s' "$WG" | grep -q 'NEWMAP|rebooted|pack=.*|theater=WINTER' \
    && say "ok   New Map rebooted onto the WINTER bank" \
    || { say "FAIL New Map did not reach the winter theater"; fail=1; }
printf '%s' "$WG" | grep -q 'pack SCW01EA: theater=WINTER' \
    && say "ok   the winter donor pack loaded" \
    || { say "FAIL the winter donor pack did not load"; fail=1; }
printf '%s' "$WG" | grep -q 'OPTIONS|abort|to-editor|edit=1' \
    && say "ok   a winter playtest aborts back to the editor" \
    || { say "FAIL the winter playtest did not come home"; fail=1; }
[ -n "$WMAP" ] && rm -f "missions/user_maps/$WMAP".*

# SNOW. The fourth theater is RA's snow LOOK on the TD template set; its INI says
# Theater=WINTER to the brain and CNC3DTheater=SNOW to everything of ours, so this leg
# proves the whole aliasing chain: New Map onto the snow donor, the SNOW pack loads,
# the brain still boots (as WINTER), and the playtest aborts home.
printf 'newmap 1 3 0\ntick 2\neditplay play\ntick 20\noptions\noptclick Abort Mission\noptclick Abort\neditmenu check\nquit\n' \
    > /tmp/snowgate.script
SG=$(./cnc_eyes --edit --editmode 1 --scen "$SCEN" --pack SCG01EA.pack --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/user_maps/ --content content/ \
        --script /tmp/snowgate.script --w 1400 --h 900 2>&1 || true)
SMAP=$(printf '%s' "$SG" | sed -n 's/.*NEWMAP|rebooted|pack=\([A-Z0-9]*\)|.*/\1/p' | head -1)
printf '%s' "$SG" | grep -q 'NEWMAP|rebooted|pack=.*|theater=SNOW' \
    && say "ok   New Map rebooted onto the SNOW bank" \
    || { say "FAIL New Map did not reach the snow theater"; fail=1; }
printf '%s' "$SG" | grep -q 'pack SCS01EA: theater=SNOW' \
    && say "ok   the snow donor pack loaded" \
    || { say "FAIL the snow donor pack did not load"; fail=1; }
printf '%s' "$SG" | grep -q 'OPTIONS|abort|to-editor|edit=1' \
    && say "ok   a snow playtest aborts back to the editor" \
    || { say "FAIL the snow playtest did not come home"; fail=1; }
[ -n "$SMAP" ] && rm -f "missions/user_maps/$SMAP".*

# SAND. Arrakis: Theater=DESERT to the brain, CNC3DTheater=SAND to ours.
printf 'newmap 1 4 0\ntick 2\neditplay play\ntick 20\noptions\noptclick Abort Mission\noptclick Abort\neditmenu check\nquit\n' \
    > /tmp/sandgate.script
AG=$(./cnc_eyes --edit --editmode 1 --scen "$SCEN" --pack SCG01EA.pack --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir missions/user_maps/ --content content/ \
        --script /tmp/sandgate.script --w 1400 --h 900 2>&1 || true)
AMAP=$(printf '%s' "$AG" | sed -n 's/.*NEWMAP|rebooted|pack=\([A-Z0-9]*\)|.*/\1/p' | head -1)
printf '%s' "$AG" | grep -q 'NEWMAP|rebooted|pack=.*|theater=SAND' \
    && say "ok   New Map rebooted onto the SAND bank" \
    || { say "FAIL New Map did not reach the sand theater"; fail=1; }
printf '%s' "$AG" | grep -q 'pack SCA01EA: theater=SAND' \
    && say "ok   the sand donor pack loaded" \
    || { say "FAIL the sand donor pack did not load"; fail=1; }
printf '%s' "$AG" | grep -q 'OPTIONS|abort|to-editor|edit=1' \
    && say "ok   a sand playtest aborts back to the editor" \
    || { say "FAIL the sand playtest did not come home"; fail=1; }
[ -n "$AMAP" ] && rm -f "missions/user_maps/$AMAP".*

if [ "$fail" -eq 0 ]; then
    say "PASSED -- a map made in this editor plays"
    rm -f "$WORK/$SCEN".*
else
    say "FAILED"
    exit 1
fi
