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
#   f. THE 128-WIDE FORMAT: the parity oracle on a MAP_VERSION_MEGA map
#      (game/examples/USER91, 120x120, with a 472-byte sparse .BIN). Phase 1 had
#      no gate on this format at all and the brain crashed outright on it.
#      COVERAGE, MEASURED RATHER THAN CLAIMED: this file used to say USER91
#      "touches every one of the nine confinement sites at once". It does not.
#      Its [TERRAIN], [OVERLAY], [SMUDGE] and [UNITS] sections are EMPTY; the map
#      is 3 structures, 2 infantry, 8 waypoints and the .BIN. So it reaches FOUR
#      sites (map.cpp, building.cpp, infantry.cpp, display.cpp waypoints) and
#      leaves five unexercised on the MEGA arm: overlay.cpp, smudge.cpp,
#      terrain.cpp, unit.cpp and display.cpp's cell triggers. Do not restore the
#      old sentence without new content that earns it.
#   g. XL-NATIVE CONTENT: generated 256, 512 and 1024 maps boot and run on the
#      XL brain through tools/xl-parity/xlboot, a headless loader that is NOT
#      the game (the host still has a compile-time 128 ceiling of its own), plus
#      two-run determinism at 256 and a NEGATIVE CONTROL that proves the .BIN
#      reader is gated at all.
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

# ---- a2. house bits are shifted 64 wide ----------------------------------------
# The guard runs its own self-test FIRST and that is the point of asking for it here:
# a grep gate whose pattern has stopped matching reports a clean tree forever, and this
# one guards undefined behaviour that is invisible until the roster grows past 32.
if bash tools/xl-house-mask-guard.sh --self-test >"$RUNDIR/maskguard.log" 2>&1 &&
   bash tools/xl-house-mask-guard.sh >>"$RUNDIR/maskguard.log" 2>&1; then
    ok "XLa house masks: no bare 1 << against a house index (guard self-tested)"
else
    bad "XLa house mask guard: $(tail -1 "$RUNDIR/maskguard.log")"
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
    # BOTH files have to EXIST before their digests can agree. shasum skips a missing
    # file with a warning and still exits 0, so one present and one absent used to
    # collapse to a single digest and print "byte-identical across brains" -- a false
    # sentence about a comparison that never happened.
    NSHOT=$(shasum "$RUNDIR/shot-classic-$M.png" "$RUNDIR/shot-xl-$M.png" 2>/dev/null \
            | cut -d' ' -f1 | sort -u | wc -l | tr -d ' ')
    if [ ! -s "$RUNDIR/shot-classic-$M.png" ] || [ ! -s "$RUNDIR/shot-xl-$M.png" ]; then
        NSHOT=missing
    fi
    if [ "$NSHOT" = "1" ]; then
        ok "XLd shot $M: tick-1500 screenshots byte-identical across brains"
    else
        bad "XLd shot $M: screenshots differ across brains (or one brain wrote none: $NSHOT)"
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
    grep -E "$SIMGREP" "$RUNDIR/det-$R.log" >"$RUNDIR/det-$R.sim"
    shasum <"$RUNDIR/det-$R.sim" | cut -d' ' -f1 >"$RUNDIR/det-$R.hash"
done
# Two hashes of nothing also match. shasum of empty input is da39a3ee..., so comparing
# the digests alone is a test that two dead runs pass; count what went into them.
NDE=$(grep -c . "$RUNDIR/det-1.sim" 2>/dev/null || echo 0)
if [ "$NDE" -lt 20 ]; then
    bad "XLe determinism: only $NDE simulation lines to compare (want >= 20) -- nothing ran"
elif [ "$(cat "$RUNDIR/det-1.hash")" = "$(cat "$RUNDIR/det-2.hash")" ]; then
    ok "XLe determinism: two XL runs, $NDE lines, one dump-hash ($(cat "$RUNDIR/det-1.hash"))"
else
    bad "XLe determinism: dump-hashes differ"
fi

# ---- f. the 128-wide (MAP_VERSION_MEGA) format --------------------------------
#
# WHY THIS GATE EXISTS, AND WHY IT IS NOT JUST ANOTHER NAME IN THE d LOOP.
#
# Every mission in d is MAP_VERSION_NORMAL: 64-wide content that both brains
# confine, classic into its 128 stride and XL into its 1024 one. That is a
# different code path from MAP_VERSION_MEGA, which is what the retail skirmish
# conversions and everything the mission editor makes actually are. Phase 1
# gated the first and not the second, and the second did not work: the XL brain
# SEGFAULTED on this map (control run, 27 Aug 2026) because its sparse .BIN
# record was spelled with a CELL that is four bytes wide here and two in the
# format, so it read eight-byte records out of a four-byte-record file.
#
# THE CONTENT IS STAGED FROM THE TRACKED COPY, into the run folder, on purpose.
# playable/missions/USER91.* exists on this machine and DIFFERS from
# game/examples/USER91.* -- gating against the play folder would gate against
# whatever a person last saved there. --dir points at the staged folder so the
# scenario cannot be resolved from anywhere else.
#
# The pack is SCG01EA's: a map borrows any pack of its own theater (TEMPERATE
# here) and the cells come from the map's own .BIN. There is no shot leg,
# because a borrowed pack is not a claim about pixels; this gate is about the
# BRAIN reading the format, which is what the OBJ stream shows.
MEGADIR="$RUNDIR/megamissions"
mkdir -p "$MEGADIR"
cp "$repo/game/examples/USER91.INI" "$repo/game/examples/USER91.BIN" \
   "$repo/game/examples/USER91.HGT" "$MEGADIR/" 2>/dev/null
if [ ! -s "$MEGADIR/USER91.INI" ] || [ ! -s "$MEGADIR/USER91.BIN" ]; then
    bad "XLf mega: game/examples/USER91.INI or .BIN did not stage"
else
    for side in classic xl; do
        [ $side = classic ] && DY="$CLASSIC" || DY="$XL"
        ../game/cnc_eyes --scen USER91 --pack SCG01EA.pack \
            --cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack \
            --content content/ --dir "$MEGADIR/" --dumpsound --dylib "$DY" \
            --script "$repo/tools/xl-parity/xlgate-parity.txt" \
            >"$RUNDIR/par-$side-USER91.log" 2>&1
        rm -f xlpar_shot.png
    done
    # An empty capture is a CRASH, not a parity failure, and compare.py says
    # MALFORMED rather than DIVERGED for it. Both are red; the message matters
    # because they send you to different places.
    NC=$(grep -cE "$SIMGREP" "$RUNDIR/par-classic-USER91.log")
    NX=$(grep -cE "$SIMGREP" "$RUNDIR/par-xl-USER91.log")
    if [ "$NX" = "0" ] || [ "$NC" = "0" ]; then
        bad "XLf mega: a brain produced NO dump at all (classic=$NC xl=$NX) -- it crashed, see $RUNDIR/par-*-USER91.log"
    elif python3 "$repo/tools/xl-parity/compare.py" \
            "$RUNDIR/par-classic-USER91.log" 128 "$RUNDIR/par-xl-USER91.log" 1024 \
            >"$RUNDIR/parity-USER91.txt" 2>&1; then
        ok "XLf mega USER91 (120x120, Version=1): $(cat "$RUNDIR/parity-USER91.txt")"
    else
        bad "XLf mega USER91 DIVERGED: $(head -1 "$RUNDIR/parity-USER91.txt")"
    fi
fi

# ---- g. XL-native content at every tier ---------------------------------------
#
# WHAT THIS PROVES AND WHAT IT DOES NOT. It proves the ENGINE loads and runs a map
# bigger than any format before Version=2 could express. It says nothing about the
# game: game/cnc_eyes still carries C3D_MAP_MAX 128 in its own grids, so no XL-native
# map can be opened in the host yet. Keeping the two apart is the point of xlboot
# existing at all -- a failure here is the brain's, and a failure in the host is the
# host's, and one probe that did both would tell you neither.
#
# THE MAPS ARE GENERATED, NOT TRACKED, and that is deliberate: they are derived from
# one script that states the format in one place, so a format change cannot leave a
# stale committed map behind claiming to be the same thing.
cd "$repo"
XLBOOT="$RUNDIR/xlboot"
# DELETE BEFORE BUILDING. RUNDIR is a documented argument, so it gets reused, and a
# failed compile leaves the PREVIOUS binary sitting there executable. Every leg below
# is guarded on [ -x "$XLBOOT" ], so without this they would all score the old build
# while the build leg alone went red -- the stale-artefact trap, in a gate written
# after that trap was written down.
rm -f "$XLBOOT"
if c++ -std=c++14 -O1 -g -fms-extensions -fdeclspec -D__int64="long long" \
       -I brain/xl/tiberiandawn -I brain/xl/common \
       tools/xl-parity/xlboot.cpp -o "$XLBOOT" >"$RUNDIR/xlboot-build.log" 2>&1; then
    ok "XLg xlboot builds"
else
    bad "XLg xlboot build failed ($(tail -1 "$RUNDIR/xlboot-build.log"))"
fi

if [ -x "$XLBOOT" ]; then
    for SZ in 256 512 1024; do
        MD="$RUNDIR/xlmap$SZ"
        rm -rf "$MD"
        if ! python3 tools/xl-parity/make-xl-map.py "$MD" "XL${SZ}EA" "$SZ" --houses 2 --army 8 \
                >"$RUNDIR/xlmap$SZ.log" 2>&1; then
            bad "XLg $SZ: the generator failed ($(tail -1 "$RUNDIR/xlmap$SZ.log"))"
            continue
        fi
        if [ ! -s "$MD/XL${SZ}EA.BIN" ] || [ ! -s "$MD/XL${SZ}EA.INI" ]; then
            bad "XLg $SZ: the generator wrote no .BIN or no .INI"
            continue
        fi
        # An XL .BIN is never empty: its header alone is twelve bytes, so a zero length
        # file is a failed write rather than an empty map. That is the ambiguity the
        # mega format has and this one does not, and it is worth asserting once.
        SZB=$(wc -c <"$MD/XL${SZ}EA.BIN" | tr -d ' ')
        if [ "$SZB" -lt 12 ]; then
            bad "XLg $SZ: the .BIN is shorter than its own header ($SZB bytes)"
            continue
        fi
        if "$XLBOOT" --dylib "$XL" --dir "$MD/" --content "$repo/playable/content/" \
               --scen "XL${SZ}EA" --ticks 600 >"$RUNDIR/xlboot$SZ.log" 2>&1 \
           && grep -q "^XLBOOT|ticks|600 of 600" "$RUNDIR/xlboot$SZ.log"; then
            NOBJ=$(grep -c "^OBJ|" "$RUNDIR/xlboot$SZ.log")
            # THE .BIN READER HAS TO BE ASSERTED SEPARATELY, and this is the whole
            # reason the leg below exists. A refused map binary is NOT fatal: the
            # scenario loader falls back to reading templates from the INI
            # (scenarioini.cpp), the map comes up without its terrain, and the object
            # dump carries no terrain channel at all -- so a deliberately corrupted
            # .BIN scored full marks on every leg of this gate, with a byte-identical
            # dump hash. The brain does say so, through GlyphX_Debug_Print, and xlboot
            # now prints what the brain says.
            NREFUSE=$(grep -c "brain-says|Read_Binary_XL" "$RUNDIR/xlboot$SZ.log")
            # ANTI-VACUITY. A run that loads nothing also crashes nowhere, and every
            # assertion above it would pass on an empty map. The generator puts
            # 2 structures and 10 units per house down, so a dump with nothing in it
            # means the scenario did not really load.
            if [ "$NREFUSE" != "0" ]; then
                bad "XLg ${SZ}x${SZ}: the brain REFUSED the .BIN and read the INI instead: $(grep -m1 "brain-says|Read_Binary_XL" "$RUNDIR/xlboot$SZ.log")"
            elif [ "$NOBJ" -ge 20 ]; then
                ok "XLg ${SZ}x${SZ} XL-native: 600 ticks, $NOBJ objects, .BIN accepted"
            else
                bad "XLg ${SZ}x${SZ}: ran, but the dump holds only $NOBJ objects (want >= 20)"
            fi
        else
            bad "XLg ${SZ}x${SZ} did not complete (see $RUNDIR/xlboot$SZ.log)"
        fi
    done

    # Determinism on XL-native content, the same law the classic tiers are held to.
    # A HASH OF NOTHING IS STILL A HASH, and two of them still match. `[ -s file ]`
    # cannot catch that: shasum of empty input writes da39a3ee... into a 41-byte file,
    # so the guard is decoration. Count the lines that went INTO the hash instead.
    NDET=0
    for R in 1 2; do
        "$XLBOOT" --dylib "$XL" --dir "$RUNDIR/xlmap256/" \
            --content "$repo/playable/content/" --scen XL256EA --ticks 600 \
            >"$RUNDIR/xldet-$R.log" 2>&1
        grep -E "$SIMGREP" "$RUNDIR/xldet-$R.log" >"$RUNDIR/xldet-$R.sim"
        shasum <"$RUNDIR/xldet-$R.sim" | cut -d' ' -f1 >"$RUNDIR/xldet-$R.hash"
    done
    NDET=$(grep -c . "$RUNDIR/xldet-1.sim" 2>/dev/null || echo 0)
    if [ "$NDET" -lt 20 ]; then
        bad "XLg determinism at 256: only $NDET simulation lines to compare (want >= 20) -- the runs produced nothing to be deterministic about"
    elif [ "$(cat "$RUNDIR/xldet-1.hash")" = "$(cat "$RUNDIR/xldet-2.hash")" ]; then
        ok "XLg determinism at 256: two runs, $NDET lines, one dump-hash ($(cat "$RUNDIR/xldet-1.hash"))"
    else
        bad "XLg determinism at 256: dump-hashes differ"
    fi

    # ---- THE NEGATIVE CONTROL ---------------------------------------------------
    #
    # Every leg above asserts that something GOOD happened. None of them could tell you
    # whether the assertion is capable of failing, and on 27 Aug 2026 one of them was
    # not: a .BIN with a corrupted magic passed all four, because the engine refuses the
    # file, loads the scenario from its INI anyway, and the dump has no terrain in it to
    # notice by. The dump hash was byte-identical between the good map and the corrupt
    # one.
    #
    # So the control corrupts the magic of a copy and asserts that the refusal IS
    # reported. If this leg ever goes red, the gate above has stopped watching the
    # reader, whatever else it says.
    BADMAP="$RUNDIR/xlmap-control"
    rm -rf "$BADMAP"
    if [ -d "$RUNDIR/xlmap256" ]; then
        cp -R "$RUNDIR/xlmap256" "$BADMAP"
        printf 'C3XA' | dd of="$BADMAP/XL256EA.BIN" bs=1 seek=0 conv=notrunc >/dev/null 2>&1
        "$XLBOOT" --dylib "$XL" --dir "$BADMAP/" --content "$repo/playable/content/" \
            --scen XL256EA --ticks 60 >"$RUNDIR/xlcontrol.log" 2>&1
        if grep -q "brain-says|Read_Binary_XL: not an XL map binary" "$RUNDIR/xlcontrol.log"; then
            ok "XLg negative control: a corrupted .BIN magic IS refused and reported"
        else
            bad "XLg negative control: the brain did NOT report refusing a corrupted .BIN, so the XLg legs above cannot see a bad map at all"
        fi
    else
        bad "XLg negative control: no 256 map to corrupt"
    fi
fi

echo "===== XL gates: $PASS pass, $FAIL fail (logs in $RUNDIR)"
[ "$FAIL" = "0" ]
