#!/bin/bash
# ==================================================================================
# GATE: every campaign mission loads, ticks and draws.
#
# WHY THIS EXISTS. Earlier the project shipped four mission packs and the
# other thirty-two campaign missions had never been run even once. "The pipeline
# works, SCG02EA is the worked example" was true and told you nothing about SCG12EB.
# This runs the WHOLE reachable campaign and fails on the first mission that does not
# come up.
#
# WHICH MISSIONS. The 36 the campaign state machine can actually reach, walked from
# the cartridge's own scenario arithmetic rather than from the 79 scenarios that
# happen to have terrain -- the ROM carries spares (SCB21EA, SCG30EA, SCG90EA and 16
# more) that nothing in the flow ever selects.
#
# WHAT IS CHECKED, and why each one earns its place:
#
#   theater=NAME       the pack really bound a tile set, not a blank
#   heightmap lo..hi   with hi > lo. A flat 0..0 is what a missing PK9 block looks
#                      like, and a spare scenario with no ROM heightmap bakes flat
#                      and LOOKS fine until you notice the whole map is a table.
#   objects > 0        the brain parsed the .INI and instantiated the scenario
#   meshes  > 0        a frame was really rendered. Without a `shot` in the script
#                      the draw counters are all zero and every count below passes
#                      VACUOUSLY -- this suite's besetting sin, so: a shot, and a
#                      positive mesh count to prove the shot drew something.
#   fallback boxes = 0 every object on screen resolved to real cartridge art
#   0 failures         the script itself ran clean
#   no "cannot open"   no missing pack, mission, dylib or content file
#
# --noshroud is not cosmetic here. At mission start the shroud hides all but the
# player's opening group, so a shrouded run draws one or two meshes and cannot see a
# missing model anywhere else on the map. With it off, every object in the scenario
# is drawn and the fallback count covers the whole map.
#
# NOT CHECKED: "invisible terrain". Those are the TC01..TC05 tree clumps, which the
# console draws nothing for either (see ClumpDef in cnc_eyes.cpp). The shipped-good
# SCG01EA reports 11 of them. A gate demanding zero would fail on a correct build.
# ==================================================================================
set -u
RUNDIR="${RUNDIR:-$(cd "$(dirname "$0")/../playable" && pwd)}"
cd "$RUNDIR" || { echo "gate_campaign: no RUNDIR $RUNDIR"; exit 1; }

MISSIONS="SCB01EA SCB02EA SCB03EA SCB04EA SCB05EA SCB06EA SCB07EA SCB08EA SCB09EA
SCB10EA SCB11EA SCB12EA SCB13EA SCG01EA SCG02EA SCG03EA SCG04EA SCG04WA SCG04WB
SCG05EA SCG05WA SCG05WB SCG06EA SCG07EA SCG08EA SCG08EB SCG09EA SCG10EA SCG10EB
SCG11EA SCG12EA SCG12EB SCG13EA SCG13EB SCG14EA SCG15EA"

EXPECTED=36
COUNT=$(echo $MISSIONS | wc -w | tr -d ' ')
if [ "$COUNT" -ne "$EXPECTED" ]; then
    echo "gate_campaign: list is $COUNT missions, expected $EXPECTED"; exit 1
fi

SCRIPT=$(mktemp /tmp/gate_campaign.XXXXXX)
printf 'tick 20\ncammode old\nzoom min\nshot /tmp/gate_campaign_shot.png\nquit\n' > "$SCRIPT"
rm -f /tmp/gate_campaign_shot.png

# hud640.pack IS EXPECTED TO BE HERE. The per-mission "cannot open" check below carves
# out this one file, because cnc_eyes falls back to the DOS sidebar when it is missing
# and that fallback is deliberate. The carve-out was written when the 640x480 HUD did
# not exist; now that it does, a genuinely absent HUD pack would sail through all 36
# missions in silence. So the file's existence is asserted ONCE, here, and the carve-out
# below stays narrow rather than becoming a blind spot.
pass=0; fail=0
if [ ! -s hud640.pack ]; then
    echo "  hud640.pack MISSING or empty -- the 640x480 HUD would silently fall back to the DOS bar"
    fail=$((fail + 1))
fi
for s in $MISSIONS; do
    out=$(./cnc_eyes --noshroud --scen "$s" --pack "$s.pack" --cameos cameos.pack \
          --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib ./TiberianDawn.dylib \
          --dir ./missions/ --content ./content/ --script "$SCRIPT" 2>&1)

    th=$(echo "$out" | grep -o 'theater=[A-Z][A-Z]*' | head -1)
    hm=$(echo "$out" | grep -o 'heightmap [0-9]*\.\.[0-9]*' | head -1)
    lo=${hm#heightmap }; lo=${lo%%..*}; hi=${hm##*..}
    ob=$(echo "$out" | grep -o 'objects=[0-9]*' | head -1 | cut -d= -f2)
    me=$(echo "$out" | grep '^draw:' | sed -n 's/^draw: \([0-9]*\) meshes.*/\1/p')
    fb=$(echo "$out" | grep '^draw:' | sed -n 's/.* \([0-9]*\) fallback boxes.*/\1/p')
    fa=$(echo "$out" | grep -o '[0-9]* failures' | head -1 | cut -d' ' -f1)
    # "cannot open" ONLY for files this gate is actually about: the mission's pack, its
    # .INI/.BIN, the brain. An earlier version grepped the whole log for the phrase and
    # so failed all 36 missions on
    #     sidebar: 640x480 HUD unavailable (cannot open hud640.pack); using the DOS bar
    # which is a deliberate, harmless fallback to the DOS sidebar. A gate that fires on
    # an optional asset's fallback message is not testing what it claims to test.
    co=$(echo "$out" | grep -i 'cannot open' | grep -cvE 'hud640\.pack')

    why=""
    [ -n "$th" ]                        || why="$why no-theater"
    { [ -n "$hi" ] && [ -n "$lo" ] && [ "$hi" -gt "$lo" ] 2>/dev/null; } \
                                        || why="$why flat-heightmap($hm)"
    [ "${ob:-0}" -gt 0 ] 2>/dev/null    || why="$why no-objects"
    [ "${me:-0}" -gt 0 ] 2>/dev/null    || why="$why nothing-drawn"
    [ "${fb:-1}" -eq 0 ] 2>/dev/null    || why="$why $fb-fallback-boxes"
    [ "${fa:-1}" -eq 0 ] 2>/dev/null    || why="$why script-failures"
    [ "${co:-1}" -eq 0 ] 2>/dev/null    || why="$why cannot-open"

    if [ -z "$why" ]; then
        pass=$((pass + 1))
        printf '  %-8s %-9s h=%s..%s obj=%-4s mesh=%-4s PASS\n' "$s" "$th" "$lo" "$hi" "$ob" "$me"
    else
        fail=$((fail + 1))
        printf '  %-8s FAIL:%s\n' "$s" "$why"
        echo "$out" | grep -E '^FALLBACK\||cannot open' | head -5 | sed 's/^/      /'
    fi
done
rm -f "$SCRIPT" /tmp/gate_campaign_shot.png

echo "gate_campaign: $pass/$EXPECTED missions up, $fail failed"
[ "$fail" -eq 0 ] && [ "$pass" -eq "$EXPECTED" ] || exit 1
exit 0
