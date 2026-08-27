#!/usr/bin/env bash
#
# THE PHASE 1 XL GATE SET (docs/design-xl-brain.md, phase 1), in one command:
#
#   tools/xl-parity/run-xl-gates.sh [RUNDIR]
#
#   a. brain/xl/common byte-equals brain/vanilla/common
#   b. TiberianDawnXL builds clean on this arch (minos asserted by the build
#      script)
#   c. BOOT: SCG01EA -- classic 64x64 content confined into the 1024 stride --
#      survives a 3000-tick headless scripted run on the XL brain (rc 0, no
#      script failures)
#   d. PARITY ORACLE: the identical scripted session (11 object dumps through
#      tick 3000, tick-1500 shot, SOUND stream) on the CLASSIC brain and on XL,
#      on three missions across theaters (SCG01EA, SCB01EA, SCG10EA); the OBJ
#      streams must be identical after stride normalization (compare.py), the
#      shots byte-identical, plus a --trace 200 rule-firing diff on SCG01EA
#   e. DETERMINISM: the XL dylib twice on SCG01EA, one dump-hash
#
# The CLASSIC baseline builds from brain/vanilla's own checkout (which the
# vanilla gate pins to upstream+patch), into brain/vanilla/build-native; the
# play folder's shipped dylib and game/ are never touched. Run it from
# anywhere; it works out of playable/.
#
# NOTE (26 Aug 2026): the parity oracle is a BRING-UP instrument for classic
# content on the XL brain. It retires when the XL pathfinder lands (route
# choice legitimately diverges) and is archived here per the contract.
set -uo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo"
RUNDIR="${1:-$(mktemp -d /tmp/xl-gates.XXXXXX)}"
mkdir -p "$RUNDIR"
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "OK   $*"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL $*"; }

# ---- a. common byte-equality --------------------------------------------------
if tools/check-xl-common.sh >"$RUNDIR/common.log" 2>&1; then
    ok "XLa common byte-equal"
else
    bad "XLa common differs ($(tail -1 "$RUNDIR/common.log"))"
fi

# ---- b. builds ---------------------------------------------------------------
if tools/mac/build-brain-xl-mac.sh >"$RUNDIR/build-xl.log" 2>&1; then
    ok "XLb TiberianDawnXL builds (minos asserted)"
else
    bad "XLb XL build failed ($(tail -1 "$RUNDIR/build-xl.log"))"
fi
XL="$repo/brain/xl/build-native/tiberiandawn/TiberianDawnXL.dylib"

. tools/mac/deployment-target.sh
cmake -S brain/vanilla -B brain/vanilla/build-native \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="$CNC3D_MACOS_MIN" \
      -DBUILD_REMASTERTD=ON -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=OFF \
      -DBUILD_REMASTERRA=OFF -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=OFF \
      >"$RUNDIR/build-classic.log" 2>&1
if cmake --build brain/vanilla/build-native --target TiberianDawn \
      -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >>"$RUNDIR/build-classic.log" 2>&1; then
    ok "XLb classic baseline builds"
else
    bad "XLb classic baseline build failed"
fi
CLASSIC="$repo/brain/vanilla/build-native/tiberiandawn/TiberianDawn.dylib"

# The two dylibs must export the same CNC_* surface, plus XL's ABI facts.
nm -gU "$CLASSIC" | awk '{print $3}' | grep '^_CNC' | sort >"$RUNDIR/sym.classic"
nm -gU "$XL"      | awk '{print $3}' | grep '^_CNC' | sort >"$RUNDIR/sym.xl"
if [ -z "$(comm -23 "$RUNDIR/sym.classic" "$RUNDIR/sym.xl")" ] \
   && [ "$(comm -13 "$RUNDIR/sym.classic" "$RUNDIR/sym.xl")" = "_CNC3D_ABI_Facts" ]; then
    ok "XLb symbol surface: classic set + CNC3D_ABI_Facts, nothing else"
else
    bad "XLb symbol surface drifted (diff $RUNDIR/sym.classic $RUNDIR/sym.xl)"
fi

cd playable
BASE="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dir missions/ --content content/"
SIMGREP='^(OBJ|CARGO|TIB|WALL|SMUDGE|HOUSE|TICK|SOUND|OBJDUMP-END)'

# ---- c. boot ------------------------------------------------------------------
if ../game/cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --dylib "$XL" \
      --script "$repo/tools/xl-parity/xlgate-boot.txt" >"$RUNDIR/boot.log" 2>&1 \
   && grep -q "SCRIPT|end.*0 failures" "$RUNDIR/boot.log"; then
    ok "XLc boot: SCG01EA 3000 ticks on XL, no crash, no script failures"
else
    bad "XLc boot failed (see $RUNDIR/boot.log)"
fi

# ---- d. parity ---------------------------------------------------------------
for M in SCG01EA SCB01EA SCG10EA; do
    for side in classic xl; do
        [ $side = classic ] && DY="$CLASSIC" || DY="$XL"
        ../game/cnc_eyes --scen $M --pack $M.pack $BASE --dumpsound --dylib "$DY" \
            --script "$repo/tools/xl-parity/xlgate-parity.txt" >"$RUNDIR/par-$side-$M.log" 2>&1
        mv xlpar_shot.png "$RUNDIR/shot-$side-$M.png" 2>/dev/null
    done
    if python3 "$repo/tools/xl-parity/compare.py" \
          "$RUNDIR/par-classic-$M.log" 128 "$RUNDIR/par-xl-$M.log" 1024 \
          >"$RUNDIR/parity-$M.txt" 2>&1; then
        ok "XLd parity $M: $(cat "$RUNDIR/parity-$M.txt")"
    else
        bad "XLd parity $M DIVERGED: $(head -1 "$RUNDIR/parity-$M.txt")"
    fi
    NSHOT=$(shasum "$RUNDIR/shot-classic-$M.png" "$RUNDIR/shot-xl-$M.png" 2>/dev/null \
            | cut -d' ' -f1 | sort -u | wc -l | tr -d ' ')
    if [ "$NSHOT" = "1" ]; then
        ok "XLd shot $M: tick-1500 screenshots byte-identical across brains"
    else
        bad "XLd shot $M: screenshots differ across brains"
    fi
done

# --trace N is honoured on the SHOT path only (game_run_shot); without a
# --shot companion the flag is ignored and the binary opens the interactive
# game and never exits. The shot file itself is never written -- the trace
# takes over before the picture -- but the flag is what selects the path.
../game/cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --trace 200     --shot "$RUNDIR/trace-unused.png" --ticks 200 --dylib "$CLASSIC" >"$RUNDIR/trace-classic.log" 2>&1
../game/cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --trace 200     --shot "$RUNDIR/trace-unused.png" --ticks 200 --dylib "$XL" >"$RUNDIR/trace-xl.log" 2>&1
NTR=$(grep -c "^TRACE" "$RUNDIR/trace-classic.log")
if [ "$NTR" -gt 0 ]    && diff <(grep "^TRACE" "$RUNDIR/trace-classic.log") <(grep "^TRACE" "$RUNDIR/trace-xl.log") >/dev/null; then
    ok "XLd trace: 200-tick rule-firing streams identical ($NTR TRACE lines)"
else
    bad "XLd trace: rule firings diverge or the trace never ran (lines=$NTR)"
fi

# ---- e. determinism ----------------------------------------------------------
for R in 1 2; do
    ../game/cnc_eyes --scen SCG01EA --pack SCG01EA.pack $BASE --dumpsound --dylib "$XL" \
        --script "$repo/tools/xl-parity/xlgate-parity.txt" >"$RUNDIR/det-$R.log" 2>&1
    mv xlpar_shot.png "$RUNDIR/det-shot-$R.png" 2>/dev/null
    grep -E "$SIMGREP" "$RUNDIR/det-$R.log" | shasum | cut -d' ' -f1 >"$RUNDIR/det-$R.hash"
done
if [ "$(cat "$RUNDIR/det-1.hash")" = "$(cat "$RUNDIR/det-2.hash")" ]; then
    ok "XLe determinism: two XL runs, one dump-hash ($(cat "$RUNDIR/det-1.hash"))"
else
    bad "XLe determinism: dump-hashes differ"
fi

echo "===== XL gates: $PASS pass, $FAIL fail (logs in $RUNDIR)"
[ "$FAIL" = "0" ]
