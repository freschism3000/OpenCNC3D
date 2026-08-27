#!/usr/bin/env python3
"""Build a minimal CONQUER.MIX containing exactly the building buildup art
(<INI>MAKE.SHP) that tiberiandawn/bdata.cpp:3839 asks MFCD::Retrieve for.

Why the file is called CONQUER.MIX and not cnc3dmake.mix: the engine only ever
registers a fixed set of archive names (init.cpp:417 new MFCD("CONQUER.MIX"),
then init.cpp:511 MFCD::Cache("CONQUER.MIX")). A new name would need a brain
patch to be registered; reusing the CONQUER.MIX slot needs zero code changes.
The engine tolerates the tiny subset because every lookup is by-name bsearch,
never by expected inventory.

Index sort: mixfile.h compfunc compares the id as SIGNED int32, so the table
is sorted by signed id.

Source of truth for candidate names: the NAME strings in bdata.cpp.
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "sidebar-tools"))
from mixlib import Mix, mix_id  # noqa: E402

# Inputs (both are external to this repo, override on the command line):
#   --conquer  the 1995 DOS CD CONQUER.MIX (the full one; this script extracts
#              only the <INI>MAKE.SHP buildup members from it)
#   --bdata    tiberiandawn/bdata.cpp from the patched Vanilla Conquer source
#              (the NAME strings are the source of truth for candidate names)
ap = argparse.ArgumentParser(description=__doc__,
                             formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--conquer", default=os.environ.get(
    "CNC3D_CONQUER", os.path.join(HERE, "..", "dosdata", "CONQUER.MIX")),
    help="full DOS CD CONQUER.MIX (default: ../dosdata/CONQUER.MIX or $CNC3D_CONQUER)")
ap.add_argument("--bdata", default=os.environ.get(
    "CNC3D_BDATA", os.path.join(HERE, "..", "..", "brain", "vanilla",
                                "tiberiandawn", "bdata.cpp")),
    help="Vanilla Conquer tiberiandawn/bdata.cpp (default: ../../brain/vanilla/... or $CNC3D_BDATA)")
ap.add_argument("--out", default=os.path.join(HERE, "CONQUER.MIX"),
    help="output path (default: the game root CONQUER.MIX next to this script)")
ARGS = ap.parse_args()
BDATA = ARGS.bdata
CONQUER = ARGS.conquer
OUT = ARGS.out

missing_inputs = [p for p in (CONQUER, BDATA) if not os.path.isfile(p)]
if missing_inputs:
    sys.exit("missing input(s):\n  " + "\n  ".join(missing_inputs) +
             "\nPoint --conquer at the 1995 DOS CD CONQUER.MIX and --bdata at "
             "the Vanilla Conquer tiberiandawn/bdata.cpp (or set $CNC3D_CONQUER "
             "/ $CNC3D_BDATA). Both are read-only sources, never modified.")


def building_names():
    """Every NAME string in bdata.cpp (the '// NAME:' annotated ctor args)."""
    names = []
    with open(BDATA) as fh:
        for line in fh:
            m = re.search(r'"([A-Z0-9]{1,8})",\s*//\s*NAME', line)
            if m:
                names.append(m.group(1))
    return names


def main():
    src = Mix(CONQUER)
    names = building_names()
    print("bdata.cpp NAME strings: %d" % len(names))

    members = []   # (member name, blob)
    missing = []
    for n in names:
        member = n + "MAKE.SHP"
        if src.has(member):
            members.append((member, src.get(member)))
        else:
            missing.append(n)
    print("MAKE shapes found in CONQUER.MIX: %d" % len(members))
    print("no MAKE art (expected for walls/civ/etc): %s" % " ".join(missing))

    # sanity: every extracted blob starts like a TD SHP (u16 framecount > 0)
    for name, blob in members:
        nframes = struct.unpack_from("<H", blob, 0)[0]
        if not (0 < nframes < 200):
            raise SystemExit("%s does not look like a SHP (frames=%d)" % (name, nframes))

    # ---- write the minimal MIX ------------------------------------------------
    entries = []
    off = 0
    for name, blob in members:
        entries.append([mix_id(name), off, len(blob), name, blob])
        off += len(blob)

    def signed(v):
        return v - 0x100000000 if v & 0x80000000 else v

    entries.sort(key=lambda e: signed(e[0]))

    datasize = off
    if os.path.dirname(OUT):
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as fh:
        fh.write(struct.pack("<HI", len(entries), datasize))
        for eid, eoff, esize, _, _ in entries:
            fh.write(struct.pack("<III", eid, eoff, esize))
        # payload must be written in the same offset order it was assigned
        for name, blob in members:
            fh.write(blob)

    print("wrote %s: %d members, %d bytes" % (OUT, len(entries), os.path.getsize(OUT)))

    # round-trip check with the same reader the sidebar bake used
    chk = Mix(OUT)
    for name, blob in members:
        if chk.get(name) != blob:
            raise SystemExit("round-trip FAILED for " + name)
    print("round-trip: all %d members byte-identical" % len(members))
    for name, blob in sorted(members):
        # KeyFrameHeaderType: u16 frames, x, y, width, height, largest, s16 flags
        nframes, _x, _y, w, h = struct.unpack_from("<HHHHH", blob, 0)
        print("  %-14s %6d bytes  frames=%-3d %dx%d" % (name, len(blob), nframes, w, h))


if __name__ == "__main__":
    main()
