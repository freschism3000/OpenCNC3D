#!/bin/sh
# gate-enhanced.sh -- prove an Enhanced rule fires in a REAL running mission.
#
# WHY A SEPARATE GATE. --scripttest checks the format and the logic on a made-up world,
# which is worth having and proves nothing about the engine. This one builds an actual
# map with an Enhanced rule on it, boots the actual brain, and demands the engine's own
# action body run as a result. There is no test-only branch in the binary: the map it
# plays is exactly the shape the editor writes.
#
# The rule is deliberately the simplest thing that cannot be faked natively -- two
# clauses combined with AND -- and its carrier's action is Lose, because a mission
# ending is a signal nothing else in a headless run produces.
set -eu

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo/playable"

SRC=SCG09EA
WORK=/tmp/cnc3d-enhgate
SCEN=SCM90EA
rm -rf "$WORK"
mkdir -p "$WORK"

# The terrain comes from the source mission's pack; only the INI and BIN move.
cp "missions/$SRC.BIN" "$WORK/$SCEN.BIN"
[ -f "missions/$SRC.HGT" ] && cp "missions/$SRC.HGT" "$WORK/$SCEN.HGT"

# Strip the source's own [Triggers] and [CellTriggers] so nothing else can end the
# mission, then write ours. A gate that passes because some OTHER rule fired would be
# worse than no gate.
# NOTE: awk has no trailing /i regex flag -- writing one is silently parsed as a
# concatenation and strips nothing, which is how the first version of this gate ended up
# with TWO [Triggers] sections and a carrier the engine never registered. Upper-case the
# section name and compare.
awk '
  /^\[/ {
      s = toupper($0)
      sub(/[ \t\r]+$/, "", s)
      keep = (s != "[TRIGGERS]" && s != "[CELLTRIGGERS]" && s != "[BASIC]")
  }
  keep { print }
' "missions/$SRC.INI" > "$WORK/$SCEN.INI"

cat >> "$WORK/$SCEN.INI" <<'INI'

[Basic]
Name=Enhanced gate
Player=GoodGuy
Enhanced=1

[Triggers]
zzt1=None,Lose,0,GoodGuy,None,0

[EnhancedScript]
gate=fire:zzt1|when:ALL|if:TIME 1|if:!COUNTER done 1|do:ADD done 1|repeat:0
anyof=when:ANY|if:CREDITS GoodGuy 999999|if:UNITS GoodGuy 1|do:SET seen 7|repeat:0
never=when:ALL|if:CREDITS GoodGuy 999999|repeat:0
early=when:ALL|if:UNITS GoodGuy 1|do:SET early 1|repeat:0
vBLD=when:ALL|if:BUILDINGS BadGuy 5|do:SET vbld 1|repeat:0
vTYP=when:ALL|if:TYPE BadGuy HAND 1|do:SET vtyp 1|repeat:0
vCNT=when:ALL|if:COUNTER early 1|do:SET vcnt 1|repeat:0
vZON=when:ALL|if:ZONE GoodGuy 1|do:SET vzon 1|repeat:0
vNEG=when:ALL|if:!TYPE BadGuy TMPL 1|do:SET vneg 1|repeat:0
vFIR=when:ALL|if:FIRED zzt1|do:SET vfir 1|repeat:0

[EnhancedZones]
310=vZON
311=vZON
374=vZON
INI

echo "ENHGATE: playing $SCEN -- an ALL rule that must fire, an ANY rule that must\n         fire on one true clause, and a rule that must stay quiet"
OUT=$(./cnc_eyes --scen "$SCEN" --pack "$SRC.pack" --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir "$WORK/" --content content/ \
        --shot /tmp/enhgate.png --ticks 200 2>&1 || true)

fail=0
say() { printf 'ENHGATE: %s\n' "$1"; }

if printf '%s' "$OUT" | grep -qE "carries [0-9]+ Enhanced rules?"; then
    say "ok   the map is recognised as Enhanced at load"
else
    say "FAIL the map was not recognised as Enhanced at load"; fail=1
fi

FIRED=$(printf '%s' "$OUT" | grep -oE 'enhanced: t[0-9]+  gate fired -> zzt1' | head -1)
if [ -n "$FIRED" ]; then
    say "ok   the rule fired: $FIRED"
else
    say "FAIL the rule never fired"; fail=1
fi

# TIME 1 is one tenth of a minute, which is 90 ticks. Firing much earlier or much later
# would mean the clause's unit is wrong, which is the single easiest thing to get wrong
# in this format and the reason it is asserted rather than eyeballed.
T=$(printf '%s' "$FIRED" | sed -n 's/^enhanced: t\([0-9][0-9]*\).*/\1/p')
if [ -n "$T" ] && [ "$T" -ge 88 ] && [ "$T" -le 95 ]; then
    say "ok   it fired at tick $T, which is the 90 ticks TIME 1 means"
else
    say "FAIL it fired at tick '${T:-none}'; TIME 1 is 90 ticks"; fail=1
fi

if printf '%s' "$OUT" | grep -qE 'scenario decided \(LOSE\)'; then
    say "ok   the engine ran the carrier's action: the mission ended in a loss"
else
    say "FAIL the carrier was sprung but the engine's action did not happen"; fail=1
fi

# ANY: the first clause is absurd and the second is true, so an OR must fire and an AND
# of the same absurd clause must not. Testing both together is what proves the
# combinator is read at all rather than ignored in favour of a default.
if printf '%s' "$OUT" | grep -qE 'enhanced: t[0-9]+  anyof fired'; then
    say "ok   ANY fired on its one true clause"
else
    say "FAIL ANY did not fire even though one of its clauses was true"; fail=1
fi
if printf '%s' "$OUT" | grep -qE 'enhanced: t[0-9]+  never fired'; then
    say "FAIL a rule whose only clause is false fired anyway"; fail=1
else
    say "ok   a rule with a false clause stayed quiet"
fi

# And the trace, on the same map: it must report both tiers, and agree with the run.
TR=$(./cnc_eyes --trace 200 --scen "$SCEN" --pack "$SRC.pack" --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir "$WORK/" --content content/ \
        --shot /tmp/enhgate-trace.png 2>&1 || true)
if printf '%s' "$TR" | grep -qE 'TRACE\| FIRED\|gate  ENHANCED, ALL of 2'; then
    say "ok   the trace reports the Enhanced rule as fired"
else
    say "FAIL the trace did not report the Enhanced rule"; fail=1
fi
if printf '%s' "$TR" | grep -qE 'TRACE\| NEVER\|never  ENHANCED'; then
    say "ok   the trace reports the dead rule as never fired"
else
    say "FAIL the trace did not flag the rule that never fires"; fail=1
fi
if printf '%s' "$TR" | grep -q 'TRACE|WORLD|GoodGuy'; then
    say "ok   the trace says what the world looked like at the end"
else
    say "FAIL the trace gave no world readout"; fail=1
fi

# The trace from inside the EDITOR, which is two reboots -- out to play and back again --
# and the place the results were nearly lost, because coming back re-reads the mission
# and resets every Enhanced rule's runtime state.
mkdir -p "$WORK/user_maps"
cp "$WORK/$SCEN.INI" "$WORK/$SCEN.BIN" "$WORK/user_maps/" 2>/dev/null || true
printf 'edittrace 3\nquit\n' > "$WORK/trace.script"
ED=$(./cnc_eyes --edit --editmode 6 --scen "$SCEN" --pack "$SRC.pack" --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir "$WORK/" --content content/ \
        --script "$WORK/trace.script" 2>&1 || true)
EDL=$(printf '%s' "$ED" | grep -oE 'EDITTRACE\|ok\|edit=1\|minutes=3\|fired=[0-9]+\|enhfired=[0-9]+' | head -1)
if [ -n "$EDL" ]; then
    say "ok   the editor's TRACE ran and came back: $EDL"
else
    say "FAIL the editor's TRACE did not complete and return to editing"; fail=1
fi
if printf '%s' "$EDL" | grep -qE 'enhfired=[1-9]'; then
    say "ok   Enhanced results survived the trip back to the editor"
else
    say "FAIL the Enhanced results were lost coming back to the editor"; fail=1
fi

# SAVE AND LOAD. The engine's save holds the engine's world and knows nothing about
# Enhanced rules, so without a sidecar a reload comes back with every counter at zero and
# every once-only rule un-spent -- and the ambush you already survived arrives again.
# `anyof` is the test case: its condition is still TRUE after the load, so a rule whose
# spent flag was lost would see a fresh edge and fire a second time.
printf 'tick 40\nsave 0 enhgate\nload 0\ntick 120\nquit\n' > "$WORK/saveload.script"
# --savedir is not optional here: without one the engine refuses to save at all, which
# would make this check pass by never running.
mkdir -p "$WORK/saves"
SL=$(./cnc_eyes --scen "$SCEN" --pack "$SRC.pack" --reskin \
        --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
        --dylib TiberianDawn.dylib --dir "$WORK/" --content content/ \
        --savedir "$WORK/saves" \
        --script "$WORK/saveload.script" --shot /tmp/enhgate-sl.png 2>&1 || true)
if printf '%s' "$SL" | grep -q 'SAVE|slot=0|REFUSED'; then
    say "FAIL the engine refused to save, so the reload check proved nothing"; fail=1
fi
if printf '%s' "$SL" | grep -q 'enhanced: restored'; then
    say "ok   the load restored the Enhanced state: $(printf '%s' "$SL" | grep -o 'restored .*' | head -1)"
else
    say "FAIL the load did not restore any Enhanced state"; fail=1
fi
AN=$(printf '%s' "$SL" | grep -c 'anyof fired' || true)
if [ "${AN:-0}" -eq 1 ]; then
    say "ok   a once-only rule did not fire again after the reload"
else
    say "FAIL a once-only rule fired $AN times across a save and load, expected 1"; fail=1
fi

# EVERY CLAUSE KIND, against a real world rather than a made-up one. Half the
# vocabulary had only ever been exercised by a harness that answered its own questions.
#   BUILDINGS  BadGuy owns 26 in this map
#   TYPE       BadGuy owns exactly one HAND
#   COUNTER    reads what `early` set
#   ZONE       cells 310/311/374 are where the GoodGuy MCV stands
#   negation   BadGuy owns no TMPL, so !TYPE is true
#   FIRED      zzt1 is volatile and springs at t90, so this must fire AFTER it
for v in vBLD vTYP vCNT vZON vNEG vFIR; do
    if printf '%s' "$OUT" | grep -qE "enhanced: t[0-9]+  $v fired"; then
        say "ok   clause $v held against the real world"
    else
        say "FAIL clause $v never became true"; fail=1
    fi
done
# FIRED must not become true BEFORE the trigger it watches goes. The same tick is
# correct and is the design: rules are evaluated in file order within one tick, so a
# rule can see what an earlier rule just did. zzt1 springs at 90 and vFIR sees it at 90.
TF=$(printf '%s' "$OUT" | sed -n 's/^enhanced: t\([0-9]*\)  vFIR fired.*/\1/p' | head -1)
if [ -n "$TF" ] && [ "$TF" -ge 90 ]; then
    say "ok   FIRED became true at tick $TF, not before zzt1 sprang at 90"
else
    say "FAIL FIRED became true at '${TF:-never}'; zzt1 springs at 90"; fail=1
fi

if [ "$fail" -eq 0 ]; then
    say "PASSED"
else
    say "FAILED"
    printf '%s\n' "$OUT" | tail -30 >&2
    printf '%s\n' "${TR:-}" | grep -E '^TRACE' >&2 || true
    printf '%s\n' "${ED:-}" | grep -E 'EDITTRACE|TRACE\|TOTAL' >&2 || true
    exit 1
fi
