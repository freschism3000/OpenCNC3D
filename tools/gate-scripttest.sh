#!/bin/sh
# ============================================================================
#  THE SCRIPT ROUND-TRIP GATE.
#
#  The whole visual-scripting design rests on one claim: a mission opened in the
#  editor and saved without touching its script comes back with [Triggers] and
#  [CellTriggers] unchanged. That is what makes it safe to open a shipped
#  mission at all, and it is a claim, not a fact, until this runs.
#
#  So it runs over every mission that ships -- all 95 -- rather than a sample.
#  It also checks the other direction: once a rule IS edited, what is written
#  parses back to the rules that were in memory, so save-then-reload is a no-op.
#
#      sh tools/gate-scripttest.sh
#
#  Exit 0 if every mission passes. Anything else is a regression in the writer,
#  the parser, or the owned-section rule.
# ============================================================================
set -e
cd "$(dirname "$0")/../playable"

[ -x ./cnc_eyes ] || { echo "no ./cnc_eyes -- build it first" >&2; exit 1; }

PASS=0
FAIL=0
FAILED=""
TEAMS=0
CHKE=0
CHKW=0
for f in missions/*.INI; do
    m=$(basename "$f" .INI)
    # A mission with no pack of its own borrows one: this gate is about the INI,
    # and the terrain it is laid over does not enter into it.
    P="$m.pack"
    [ -f "$P" ] || P="SCG01EA.pack"
    R=$(./cnc_eyes --scripttest --shot /tmp/gate-shot.png --ticks 1 \
            --scen "$m" --pack "$P" --cameos cameos.pack --dospack dossidebar.pack \
            --dosinf dosinfantry.pack --dylib TiberianDawn.dylib \
            --dir missions/ --content content/ 2>&1 \
            | grep -oE 'SCRIPTTEST\|(PASSED|FAILED)|all [0-9]+ teams|CHECK\|TOTAL\|[0-9]+ errors, [0-9]+ warnings' || true)
    case "$R" in
        *PASSED*) PASS=$((PASS + 1)) ;;
        *)        FAIL=$((FAIL + 1)); FAILED="$FAILED $m" ;;
    esac
    # How many team types actually got compared. A silent zero here would mean the
    # round-trip check passed by never running, which is the failure mode a gate is
    # for -- so the number is reported rather than assumed.
    T=$(printf '%s' "$R" | grep -oE 'all [0-9]+ teams' | grep -oE '[0-9]+' || true)
    [ -n "$T" ] && TEAMS=$((TEAMS + T))
    # The mission check's totals across the campaign. Not asserted -- the shipped
    # missions really do contain dead rules -- but tracked, because a sudden jump means
    # the checker started crying wolf and a sudden drop means it went quiet.
    CE=$(printf '%s' "$R" | sed -n 's/.*CHECK|TOTAL|\([0-9]*\) errors.*/\1/p')
    CW=$(printf '%s' "$R" | sed -n 's/.*CHECK|TOTAL|[0-9]* errors, \([0-9]*\) warnings.*/\1/p')
    [ -n "$CE" ] && CHKE=$((CHKE + CE))
    [ -n "$CW" ] && CHKW=$((CHKW + CW))
done

echo "SCRIPTGATE: $PASS passed, $FAIL failed, $TEAMS team types re-emitted exactly"
echo "SCRIPTGATE: the mission check finds $CHKE errors and $CHKW warnings across the campaign"
if [ "$FAIL" -ne 0 ]; then
    echo "SCRIPTGATE: failed on:$FAILED" >&2
    exit 1
fi
