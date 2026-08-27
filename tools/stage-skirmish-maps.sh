#!/bin/sh
#
# stage-skirmish-maps.sh -- take the nine retail skirmish maps from the 1995 MS-DOS
# disc all the way to playable CNC3D packs, in one command.
#
# WHY THIS EXISTS
#
# The N64 port cut multiplayer, so the cartridge carries no terrain for the nine retail
# skirmish scenarios SCM01EA..SCM09EA. CNC3D draws terrain only from a cartridge-shaped
# <SCEN>.MAP, so without one there is no pack and no skirmish map to play on. The three
# tools that close that gap already exist and each is validated on its own; what was
# missing was the wiring between them, and the staging step in the middle that neither
# tool does for itself. Doing it by hand is nine scenarios times four steps, which is
# thirty-six chances to stage one file into the wrong directory and then spend an
# afternoon wondering why a DESERT map baked as TEMPERATE.
#
# WHAT IT DOES, per scenario
#
#   1. STAGE. Lifts <SCEN>.INI and <SCEN>.BIN out of data/dosdata/GENERAL.MIX into
#      game/missions/ (what the engine loads at run time) and the .INI additionally into
#      the extracted asset tree's INI/ directory. That second copy is not redundant:
#      n64_terrain.py reads the scenario's theater from there and from nowhere else, and
#      no SCM*.INI ships in the cartridge dump, so without it the resolver cannot even
#      tell DESERT from TEMPERATE.
#
#   2. CONVERT. tools/bin_to_n64map.py turns the PC .BIN into a cartridge .MAP by
#      inverting the theater's own TL4/TL8 tables. Two scenarios need
#      --allow-substitutions, and they are named in SUBS_ALLOWED below with the reason;
#      every substituted cell is printed here under a banner rather than swallowed,
#      because a silent substitution is precisely the failure this project refuses.
#
#   3. RESOLVE. n64_terrain.py turns the .MAP into terrain_<SCEN>.json: per-cell quads,
#      atlas UVs and a stats block. A non-zero "unmapped" count in those stats means
#      tile IDs fell outside both art banks and were drawn as clear ground.
#
#   4. BAKE. bake5.py turns that JSON plus the shared model/texture set into
#      <SCEN>.pack in the bakery's output directory, which is where both platform
#      packagers pick packs up from.
#
# IT IS RE-RUNNABLE. Staged files are rewritten only when their bytes actually change,
# so a second run does not churn timestamps and make the packagers re-copy the world.
# A converted .MAP that already exists is compared rather than overwritten, so pointing
# this script at a scenario the cartridge DOES ship (see PROVING THE BAKERY, below) can
# never damage cartridge data: a mismatch aborts instead of writing.
#
# IT FAILS LOUDLY. Every step is checked, the run stops at the first failure, and the
# closing summary is printed only if every scenario got all the way to a pack.
#
# PROVING THE BAKERY
#
# The pipeline is only worth as much as the proof that it is faithful, and that proof is
# available for free: run this script on a campaign scenario whose pack already ships and
# compare the bytes.
#
#     sh tools/stage-skirmish-maps.sh SCG01EA
#     md5 tools/bakery/game/SCG01EA.pack     # must equal the shipped pack
#
# WHAT IT NEEDS THAT GIT DOES NOT CARRY
#
# The bakery reaches its assets through two local symlink scaffolds and one piece of
# compiled bytecode. This script creates the scaffolds itself, idempotently and with
# relative links so they survive being moved. The bytecode, tools/bakery/sharecopy/eyes/
# bake_pk4.pyc, it can only check for: that file is excluded by the repository's blanket
# *.pyc ignore rule despite being the only surviving form of the PK4 baker, so a fresh
# clone does not have it and cannot bake anything. If it is missing here the script says
# so and stops rather than failing later inside an import.
#
# AUTHORED MAPS
#
# --from <dir> stages the scenario out of a directory of AUTHORED files instead of out
# of GENERAL.MIX, which is how a map made in the browser mission editor becomes a pack
# you can play. The editor writes <SCEN>.INI and <SCEN>.BIN, and <SCEN>.HGT when its
# elevation was edited; the .HGT is handed to bake5 as --heights so the hills reach the
# game. Without that last piece bake5 can only ever return what the cartridge shipped,
# which is exactly why every converted skirmish map is flat in play.
#
# In this mode a re-save REPLACES what was staged before. That is the opposite of the
# GENERAL.MIX path, which refuses to overwrite a file that differs, and it is right for
# both: disc data is a fact and must not be clobbered, whereas an authored map is meant
# to be edited and re-baked over and over.
#
# USAGE
#
#     sh tools/stage-skirmish-maps.sh              # all nine skirmish maps
#     sh tools/stage-skirmish-maps.sh SCM02EA      # one named scenario
#     sh tools/stage-skirmish-maps.sh --from ~/Downloads SCM90EA
#
set -eu

cd "$(dirname "$0")/.."
ROOT=$(pwd)

# The nine retail skirmish scenarios, in disc order.
DEFAULT_SCENARIOS="SCM01EA SCM02EA SCM03EA SCM04EA SCM05EA SCM06EA SCM07EA SCM08EA SCM09EA"

# The only two scenarios allowed to lose cells, and they lose three each. Both use a road
# template that has no tile in its theater's art bank at all: TEMPLATE_ROAD43 (135) in
# TEMPERATE on SCM03EA, TEMPLATE_ROAD33 (125) in DESERT on SCM06EA. The cells fall back to
# clear ground. Anything NOT on this list that reports a substitution is a new loss and
# must stop the run, which is why this is a list and not a flag.
SUBS_ALLOWED="SCM03EA SCM06EA"

MIX="$ROOT/data/dosdata/GENERAL.MIX"
MISSIONS="$ROOT/game/missions"
EX="$ROOT/tools/bakery/sharecopy/assets/extracted"
TERRAIN="$ROOT/tools/bakery/sharecopy/assets/terrain"
PACKS="$ROOT/tools/bakery/game"
PK4="$ROOT/tools/bakery/sharecopy/eyes/bake_pk4.pyc"

# --from <dir> switches step 1 from GENERAL.MIX to a directory of authored files.
FROMDIR=""
ARGS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --from)
            [ $# -ge 2 ] || { echo "FAILED: --from needs a directory" >&2; exit 1; }
            FROMDIR="$2"; shift 2 ;;
        --from=*) FROMDIR="${1#--from=}"; shift ;;
        -*) echo "FAILED: unknown option $1" >&2; exit 1 ;;
        *)  ARGS="$ARGS $1"; shift ;;
    esac
done
if [ -n "$ARGS" ]; then SCENARIOS="$ARGS"; else SCENARIOS="$DEFAULT_SCENARIOS"; fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

die() { echo ""; echo "FAILED: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Preconditions. Every one of these has a specific, non-obvious failure mode if it is
# missing, so each is named rather than left to blow up somewhere downstream.
# ---------------------------------------------------------------------------
if [ -z "$FROMDIR" ]; then
    [ -f "$MIX" ] || die "no GENERAL.MIX at $MIX. The 1995 disc data is the source for both
       the skirmish INIs and their terrain; nothing here can be reconstructed without it.
       To bake a map you authored instead, pass --from <dir>."
else
    [ -d "$FROMDIR" ] || die "no such directory: $FROMDIR"
    for SCEN in $SCENARIOS; do
        [ -f "$FROMDIR/$SCEN.INI" ] || die "$FROMDIR/$SCEN.INI is missing.
       The mission editor writes it next to the .BIN when you press Check and save."
        [ -f "$FROMDIR/$SCEN.BIN" ] || die "$FROMDIR/$SCEN.BIN is missing.
       The editor writes a .BIN only when a TILE changed. If you edited objects alone the
       map still needs terrain: bake it once from its source scenario, or copy that
       scenario's .BIN in beside the .INI."
    done
fi
[ -d "$MISSIONS" ] || die "no game/missions directory at $MISSIONS."
[ -d "$EX/INI" ] || die "no extracted INI directory at $EX/INI."
[ -d "$EX/MAP" ] || die "no extracted MAP directory at $EX/MAP."
[ -f "$PK4" ] || die "the PK4 baker bytecode is missing:
       $PK4
       tools/bakery/pk4mod.py loads it and bake5.py cannot start without it. Its source
       no longer exists, so it cannot be rebuilt. It is also excluded by the blanket
       *.pyc rule in .gitignore, which is why a fresh checkout does not have it: copy it
       from a working checkout, and treat the ignore rule as a defect worth fixing."

# ---------------------------------------------------------------------------
# The local symlink scaffolds. Both tools resolve their inputs through a share/CNC3D
# path that is not carried in git, so create it here rather than expecting whoever runs
# this to know. Relative targets on purpose: an absolute one would bind the checkout to
# one directory on one machine.
# ---------------------------------------------------------------------------
link_relative() {   # link_relative <target-relative-to-link> <link path>
    if [ -L "$2" ]; then
        [ "$(readlink "$2")" = "$1" ] || die "$2 already points at $(readlink "$2"),
       not at $1. Refusing to retarget a link this script did not make."
        return 0
    fi
    [ -e "$2" ] && die "$2 exists and is not a symlink. Expected a link to $1."
    mkdir -p "$(dirname "$2")"
    ln -s "$1" "$2"
    echo "scaffold: created $2 -> $1"
}

# n64_terrain.py looks for its inputs under <its own directory>/share/CNC3D/assets.
link_relative "../../../../extracted" "$TERRAIN/share/CNC3D/assets/extracted"
link_relative "../../../../terrain"   "$TERRAIN/share/CNC3D/assets/terrain"
# pk4mod.py rewrites the PK4 baker's one path constant to tools/bakery/support, and the
# baker then joins share/CNC3D onto it.
link_relative "../../sharecopy" "$ROOT/tools/bakery/support/share/CNC3D"

mkdir -p "$PACKS"

echo ""
echo "=========================================================================="
if [ -n "$FROMDIR" ]; then
echo "  authored map pipeline:  $FROMDIR -> .MAP -> terrain json -> .pack"
else
echo "  skirmish map pipeline:  GENERAL.MIX -> .MAP -> terrain json -> .pack"
fi
echo "  scenarios: $SCENARIOS"
echo "=========================================================================="

# ---------------------------------------------------------------------------
# STEP 1: stage the scenario files out of GENERAL.MIX.
# ---------------------------------------------------------------------------
echo ""
if [ -n "$FROMDIR" ]; then
    echo "-- 1. staging authored INI and BIN from $FROMDIR"
    for SCEN in $SCENARIOS; do
        # An authored map is meant to be re-saved and re-baked, so this REPLACES what
        # was staged before instead of refusing the way the GENERAL.MIX path does.
        #
        # But only for a scenario that is YOURS. A mission file that git tracks came
        # from the cartridge or the disc; it is canon, and overwriting it with an
        # authored map is a silent loss of the very data the rest of this pipeline is
        # validated against. The first draft of this branch did exactly that and
        # clobbered SCG01EA. Tracked means canon; refuse.
        # ...with one carve-out: the THEATER DONORS (SCW01EA winter, SCS01EA snow,
        # SCA01EA sand) are authored by this repo and tracked on purpose, so the
        # editor's New Map has a pack to reboot onto out of a fresh clone. They are
        # exactly the maps this path exists to re-bake when a bank is rebuilt.
        case "$SCEN" in
            SCW01EA|SCS01EA|SCA01EA) GUARDED=no ;;
            *)                        GUARDED=yes ;;
        esac
        [ "$GUARDED" = no ] || \
        for GUARD in "$MISSIONS/$SCEN.INI" "$MISSIONS/$SCEN.BIN" "$EX/INI/$SCEN.INI"; do
            if git -C "$ROOT" ls-files --error-unmatch "$GUARD" >/dev/null 2>&1; then
                die "$SCEN is cartridge/disc data: $(basename "$GUARD") is tracked in git.
       Authoring over it would destroy the scenario this pipeline is proved against.
       Save your map under a name of its own. Taken so far: every SCB/SCG the cartridge
       ships, SCM01EA..SCM09EA (the retail skirmish maps) and SCM90EA. SCM91EA upward is
       free. The editor's Play button picks a free name for you; this message is for a
       run started by hand."
            fi
        done
        cp -f "$FROMDIR/$SCEN.INI" "$MISSIONS/$SCEN.INI"
        cp -f "$FROMDIR/$SCEN.INI" "$EX/INI/$SCEN.INI"
        cp -f "$FROMDIR/$SCEN.BIN" "$MISSIONS/$SCEN.BIN"
        echo "  staged   $SCEN.INI  $SCEN.BIN"
        if [ -f "$FROMDIR/$SCEN.HGT" ]; then
            echo "  heights  $SCEN.HGT ($(wc -c < "$FROMDIR/$SCEN.HGT" | tr -d ' ') bytes)"
        else
            echo "  heights  none authored; the pack takes the cartridge's own, or FLAT"
        fi
    done
else
echo "-- 1. staging INI and BIN out of GENERAL.MIX"
python3 - "$ROOT" "$MIX" "$MISSIONS" "$EX/INI" $SCENARIOS <<'PY' || die "staging out of GENERAL.MIX"
import os, sys

root, mixpath, missions, exini = sys.argv[1:5]
scenarios = sys.argv[5:]
sys.path.insert(0, os.path.join(root, "menu", "tools"))
from mixshp import MixFile

mix = MixFile(mixpath)


class Clash(Exception):
    pass


def put(path, data):
    """Write only what is not already there.

    A file that is already present and already correct is left alone: an identical
    rewrite would move its mtime and make both packagers re-copy a file that did not
    change. A file that is present and DIFFERENT is not overwritten either, it stops the
    run. Some scenarios in these directories were staged by other means or came from the
    cartridge dump, and silently replacing one of those with a disc copy is a change
    nobody asked for and nobody would see."""
    if os.path.exists(path):
        with open(path, "rb") as fh:
            if fh.read() == data:
                return "unchanged"
        raise Clash(path)
    with open(path, "wb") as fh:
        fh.write(data)
    return "written  "


rc = 0
for scen in scenarios:
    for ext, dests in ((".INI", [missions, exini]), (".BIN", [missions])):
        name = scen + ext
        if not mix.has(name):
            print("  MISSING from GENERAL.MIX: %s" % name)
            rc = 1
            continue
        data = mix.read(name)
        if not data:
            print("  EMPTY in GENERAL.MIX: %s" % name)
            rc = 1
            continue
        for d in dests:
            dest = os.path.join(d, name)
            try:
                state = put(dest, data)
            except Clash:
                print("  DIFFERS from GENERAL.MIX, NOT overwritten: %s"
                      % os.path.relpath(dest, root))
                print("      Delete that file if the disc copy is the one you want.")
                rc = 1
                continue
            print("  %s %-12s %6d bytes -> %s"
                  % (state, name, len(data), os.path.relpath(dest, root)))
sys.exit(rc)
PY
fi

# The theater lives in the [MAP] section of a scenario INI, not [Basic]. n64_terrain.py
# matches THEATER= anywhere in the file regardless of section, so a misplaced key would
# bake one theater and load as another with nothing cross-checking the two. Assert that
# every staged INI actually has the key before four minutes of baking depend on it.
for SCEN in $SCENARIOS; do
    grep -qi '^ *Theater *=' "$EX/INI/$SCEN.INI" \
        || die "$SCEN.INI carries no Theater= line. n64_terrain.py reads the theater from
       this file and from nowhere else."
done

# ---------------------------------------------------------------------------
# STEP 2: PC .BIN -> cartridge .MAP.
# ---------------------------------------------------------------------------
echo ""
echo "-- 2. converting PC .BIN to cartridge .MAP"
mkdir -p "$WORK/MAP"
for SCEN in $SCENARIOS; do
    ALLOW=""
    for A in $SUBS_ALLOWED; do
        [ "$A" = "$SCEN" ] && ALLOW="--allow-substitutions"
    done

    LOG="$WORK/$SCEN.convert.txt"
    if ! python3 "$ROOT/tools/bin_to_n64map.py" --out "$WORK/MAP" $ALLOW "$SCEN" >"$LOG" 2>&1; then
        cat "$LOG"
        die "$SCEN: bin_to_n64map.py refused. If the reason above is substituted cells,
       the loss must be recorded before this scenario is added to SUBS_ALLOWED."
    fi
    sed 's/^/  /' "$LOG"

    # Loud, not incidental: if any cell was replaced with clear ground, say so in a way
    # nobody scrolls past, and say it even on a scenario that was expected to lose cells.
    if grep -q "have NO cartridge tile" "$LOG"; then
        RULE=$(printf '%70s' '' | tr ' ' '*')
        echo "  $RULE"
        printf '  ** %-64s **\n' "$SCEN: CELLS SUBSTITUTED WITH CLEAR GROUND, listed above."
        printf '  ** %-64s **\n' "The template has no tile in this theater's art bank at all."
        printf '  ** %-64s **\n' "A permitted, recorded loss. It is not a clean conversion."
        echo "  $RULE"
    fi

    SRC="$WORK/MAP/$SCEN.MAP"
    DST="$EX/MAP/$SCEN.MAP"
    [ -f "$SRC" ] || die "$SCEN: bin_to_n64map.py reported success but wrote no .MAP."
    if [ -f "$DST" ]; then
        cmp -s "$SRC" "$DST" \
            || die "$SCEN: the converted .MAP differs from the one already in
       $EX/MAP. That directory holds cartridge data; this script will not overwrite it.
       Investigate the difference before going further."
        echo "  $SCEN.MAP already present and identical, left alone"
    else
        cp "$SRC" "$DST"
        echo "  wrote $SCEN.MAP into the extracted MAP directory"
    fi
done

# ---------------------------------------------------------------------------
# STEP 3: .MAP -> terrain_<SCEN>.json.
# ---------------------------------------------------------------------------
echo ""
echo "-- 3. resolving terrain"
for SCEN in $SCENARIOS; do
    ( cd "$TERRAIN" && python3 ./n64_terrain.py "$SCEN" ) || die "$SCEN: n64_terrain.py"
    [ -f "$TERRAIN/terrain_$SCEN.json" ] \
        || die "$SCEN: n64_terrain.py wrote no terrain_$SCEN.json"
done

# ---------------------------------------------------------------------------
# STEP 4: terrain json -> <SCEN>.pack.
# ---------------------------------------------------------------------------
echo ""
echo "-- 4. baking packs"
for SCEN in $SCENARIOS; do
    # An authored .HGT is the only way elevation drawn outside the ROM reaches the
    # game; without it bake_heights can only return what the cartridge shipped.
    HGTARG=""
    if [ -n "$FROMDIR" ] && [ -f "$FROMDIR/$SCEN.HGT" ]; then
        HGTARG="--heights $FROMDIR/$SCEN.HGT"
    fi
    # shellcheck disable=SC2086
    python3 "$ROOT/tools/bakery/bake5.py" $HGTARG "$SCEN" >"$WORK/$SCEN.bake.txt" 2>&1 \
        || { cat "$WORK/$SCEN.bake.txt"; die "$SCEN: bake5.py"; }
    tail -n 2 "$WORK/$SCEN.bake.txt" | sed 's/^/  /'
    [ -f "$PACKS/$SCEN.pack" ] || die "$SCEN: bake5.py wrote no $SCEN.pack"
done

# ---------------------------------------------------------------------------
# The summary. Printed last and only on a clean run, so its presence is the signal.
# ---------------------------------------------------------------------------
echo ""
echo "-- summary"
python3 - "$TERRAIN" "$PACKS" $SCENARIOS <<'PY' || die "writing the summary"
import json, os, sys

terrain, packs = sys.argv[1:3]
scenarios = sys.argv[3:]
head = ("scenario", "theater", "cells", "unmapped", "clear", "bank4", "bank8", "pack bytes")
rows = []
for scen in scenarios:
    with open(os.path.join(terrain, "terrain_%s.json" % scen)) as fh:
        t = json.load(fh)
    st = t["stats"]
    rows.append((scen, t["theater"], str(len(t["cells"])), str(st.get("unmapped", "?")),
                 str(st.get("clear", "?")), str(st.get("bank4", "?")),
                 str(st.get("bank8", "?")),
                 "%d" % os.path.getsize(os.path.join(packs, "%s.pack" % scen))))
w = [max(len(r[i]) for r in (list(rows) + [head])) for i in range(len(head))]
fmt = "  " + "  ".join("%%-%ds" % n for n in w)
print(fmt % head)
print("  " + "  ".join("-" * n for n in w))
for r in rows:
    print(fmt % r)
bad = [r[0] for r in rows if r[3] not in ("0", "?")]
if bad:
    print("")
    print("  NOTE: unmapped cells were drawn as clear ground on: %s" % ", ".join(bad))
PY

echo ""
echo "packs are in $PACKS; the mission files the engine loads are in $MISSIONS"
echo "done."
