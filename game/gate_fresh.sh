#!/bin/bash
# ==================================================================================
# GATE: the binaries in the run directory are NEWER than the sources they are built
# from -- BOTH of them.
#
# WHY THIS EXISTS, twice over. There are two binaries: cnc_eyes (the renderer, which
# every --script gate drives) and cnc3d (the app, which is what PLAY.command launches
# and therefore what actually ships). They share almost all of their source. It
# is trivially easy to rebuild one, verify the change with a headless run, gate it
# green, commit it -- and hand over a build where the change is not there, because
# the menu route is still running a binary from hours ago.
#
# That has now happened TWICE:
#   17 Aug, morning -- cnc3d still carried the old pack-magic check and rejected every
#                      v13 pack with "the mission failed to start". Four gates failed
#                      and I spent the diagnosis blaming the movies.
#   17 Aug, evening -- cnc3d was three hours stale, so the cursor shadow and the
#                      cursor depth fix were both absent from PLAY.command. Both were
#                      reported as regressions. They were not regressions; they
#                      were never in the binary that ran.
#
# The pixel gates cannot catch this: they all drive cnc_eyes, which is always the one
# that got rebuilt. So the check is on the FILES, not on the picture.
#
# It compares against every source that either binary is built from, not just the one
# that was edited, because the failure mode is precisely "I edited a shared header and
# only rebuilt one of them".
#
# THE BRAIN IS THE THIRD BINARY, and it hid a defect right up to a release cut. The
# skirmish work added an act= field to the DLL interface so the two multiplayer houses
# can be told apart. The field was in the source and ABSENT from the deployed dylib,
# because make-build.sh COPIES the brain and asserts it arrived byte for byte while
# nothing rebuilds it. Two skirmish gates went red and named the missing field, which is
# the only reason it was found in minutes instead of after shipping.
#
# The deployed dylib's own timestamp cannot catch this, and that is the whole subtlety:
# cp refreshes the destination mtime, so a freshly copied stale brain looks NEWER than
# the source it predates. So the brain is checked as a CHAIN instead:
#   1. the built dylib in game/ must be newer than every brain source, and
#   2. the deployed dylib must be byte-identical to that built one.
# Either link alone is defeatable. Together they cannot both hold on a stale brain.
# ==================================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
RUNDIR="${RUNDIR:-$REPO/playable}"

newest=0
newest_name=""
for f in "$REPO"/game/*.cpp "$REPO"/game/*.h "$REPO"/app/*.cpp "$REPO"/app/*.c \
         "$REPO"/app/*.h "$REPO"/menu/*.c "$REPO"/menu/*.h "$REPO"/audio/*.c \
         "$REPO"/audio/*.h; do
    [ -f "$f" ] || continue
    t=$(stat -f %m "$f" 2>/dev/null || stat -c %Y "$f" 2>/dev/null)
    [ -n "$t" ] || continue
    if [ "$t" -gt "$newest" ]; then newest="$t"; newest_name="$f"; fi
done

fail=0
for bin in cnc_eyes cnc3d; do
    p="$RUNDIR/$bin"
    if [ ! -x "$p" ]; then
        echo "  $bin: MISSING from $RUNDIR"
        fail=1
        continue
    fi
    bt=$(stat -f %m "$p" 2>/dev/null || stat -c %Y "$p" 2>/dev/null)
    if [ "$bt" -lt "$newest" ]; then
        age=$(( (newest - bt) / 60 ))
        echo "  $bin: STALE by ${age} min -- older than $(basename "$newest_name")."
        echo "      rebuild it:  cd $REPO/$([ "$bin" = cnc3d ] && echo app || echo game) && ./build.sh && cp $bin $RUNDIR/"
        fail=1
    else
        echo "  $bin: current"
    fi
done

# ---- THE BRAIN ------------------------------------------------------------------
# Link 1: is the BUILT dylib newer than every brain source?
brain_newest=0
brain_newest_name=""
for f in "$REPO"/brain/vanilla/tiberiandawn/*.cpp "$REPO"/brain/vanilla/tiberiandawn/*.h \
         "$REPO"/brain/patches/*.h "$REPO"/brain/patches/*.cpp "$REPO"/brain/patches/*.diff; do
    [ -f "$f" ] || continue
    t=$(stat -f %m "$f" 2>/dev/null || stat -c %Y "$f" 2>/dev/null)
    [ -n "$t" ] || continue
    if [ "$t" -gt "$brain_newest" ]; then brain_newest="$t"; brain_newest_name="$f"; fi
done

built="$REPO/game/TiberianDawn.dylib"
deployed="$RUNDIR/TiberianDawn.dylib"
if [ ! -f "$deployed" ]; then
    echo "  TiberianDawn.dylib: MISSING from $RUNDIR"
    fail=1
elif [ ! -f "$built" ]; then
    echo "  TiberianDawn.dylib: no built copy at game/ to compare the deployed one against"
    echo "      rebuild it:  tools/mac/build-brain-mac.sh"
    fail=1
else
    bt=$(stat -f %m "$built" 2>/dev/null || stat -c %Y "$built" 2>/dev/null)
    if [ "$bt" -lt "$brain_newest" ]; then
        age=$(( (brain_newest - bt) / 60 ))
        echo "  TiberianDawn.dylib: the BUILT brain is STALE by ${age} min -- older than $(basename "$brain_newest_name")."
        echo "      rebuild it:  tools/mac/build-brain-mac.sh && cp game/TiberianDawn.dylib $RUNDIR/"
        fail=1
    elif ! cmp -s "$built" "$deployed"; then
        echo "  TiberianDawn.dylib: the DEPLOYED brain is not the built one (content differs)."
        echo "      a copied brain keeps its own mtime, so only this comparison can see it"
        echo "      redeploy it:  cp game/TiberianDawn.dylib $RUNDIR/"
        fail=1
    else
        echo "  TiberianDawn.dylib: current"
    fi
fi

# ---- THE GATE SCRIPTS ------------------------------------------------------------
# The binaries are not the only thing that goes stale in a run folder. Every --script gate
# feeds the app a .txt of verbs, those files are TRACKED, and nothing refreshes a run
# folder when they change. A stale one does not error: the missing verbs simply never run,
# so the gate measures an assertion that was never exercised and then reports the
# shortfall as a defect in the game.
#
# That cost a full suite run to diagnose. After a fast-forward, six tracked scripts were
# absent from the run folder and gate_aircraft.txt was a 67-line copy of a 90-line file
# with the facewatch verb missing entirely. Five gates went red, four of them naming
# renderer problems that did not exist.
#
# ONLY the scripts gates.sh names BARE are checked, because only those resolve against the
# run folder. Six .txt files are read as "$GATEDIR/name" instead and therefore always come
# from the tree; demanding those in the run folder would be a false red, which is the one
# thing a staleness gate must never produce. The suite also writes a dozen scripts of its
# own at runtime (gate_cargo.txt, gfx_g36_*.cfg and the rest); those are untracked outputs
# with no tracked counterpart to disagree with.
#
# -e not -d on .git: in a git WORKTREE .git is a FILE pointing at the real repo, and a -d
# test made this whole check silently vacuous the first time it was written.
if command -v git >/dev/null 2>&1 && [ -e "$REPO/.git" ]; then
    missing=0
    stale=0
    for f in $(cd "$REPO" && git ls-files 'game/gate_*.txt' 2>/dev/null); do
        b=$(basename "$f")
        # bare reference = an occurrence whose prefix is not $GATEDIR/
        grep -o -- "[A-Za-z0-9_./$]*$b" "$REPO/game/gates.sh" 2>/dev/null \
            | grep -qv 'GATEDIR/' || continue
        if [ ! -e "$RUNDIR/$b" ]; then
            missing=$((missing + 1))
            [ "$missing" -le 6 ] && echo "  gate script MISSING from the run folder: $b"
        elif ! cmp -s "$REPO/$f" "$RUNDIR/$b"; then
            stale=$((stale + 1))
            [ "$stale" -le 6 ] && echo "  gate script STALE in the run folder: $b"
        fi
    done
    if [ "$missing" -gt 0 ] || [ "$stale" -gt 0 ]; then
        echo "  $missing missing and $stale stale tracked gate script(s)."
        echo "      refresh them:  cd $REPO/game && ./make-build.sh $RUNDIR"
        echo "      a stale verb file does not error, it silently skips the assertion"
        fail=1
    else
        echo "  gate scripts: every one the suite names matches the tree"
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "gate_fresh: all three binaries and every tracked gate script match the tree"
    exit 0
fi
echo "gate_fresh: a deployed binary is older than the source it is built from"
exit 1
