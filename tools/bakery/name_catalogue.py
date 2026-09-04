#!/usr/bin/env python3
"""The cartridge's model-name catalogue, dumped from the ROM.

WHY THIS EXISTS. Two facts about this table were written down in the tree and both were
wrong: that it holds "100 names at ROM 0x1B70F8", and that it "covers slots 10..161
only". Neither survives a scan. Rather than correct the prose and leave the next person
to re-derive it by hand, the scan itself lives here, so every number quoted about this
table can be reproduced with one command.

WHAT THE TABLE IS. A flat array of 32-bit big-endian RAM pointers at ROM 0x1DE8FC,
indexed by MODEL-TABLE SLOT. A zero entry means that slot has no name. The pointers land
in a blob of NUL-terminated ASCII; the RAM-to-ROM delta is not assumed, it is CALIBRATED
here from a name whose slot is known independently (GDI_UNIT_ORCAZ1 on slot 53) and then
CHECKED against a second one (ANY_TER_TR01Z1 on slot 113). If either check fails the
script says so and stops rather than printing numbers derived from a guessed base.

The array is NOT dense and it is NOT one run. Beyond the long run of unit and structure
names it continues, after a stretch of unrelated words, into the front-end's BRF_* scene
roots. Entries in that stretch are rejected here by what they point AT (a printable,
NUL-terminated string of sensible length inside the ROM) rather than by where they sit,
because sitting inside the array is exactly what the unrelated words also do.

WHERE THE ARRAY ENDS IS NOT KNOWN, and this script does not pretend otherwise. Scanning
far enough eventually resolves slots 413 and 414 to ENGLISH and FRANCAIS, which are the
language list and almost certainly a NEIGHBOURING array at the same stride rather than
two more models. So any TOTAL printed here is a function of --max-slot and is quoted with
that bound or not at all. The findings that do not depend on the bound, and that are what
this script was written to settle, are printed as their own lines: the blob starts at ROM
0x1B7000 and fourteen named slots point BELOW 0x1B70F8, the named slots are not the run
10..161 alone, and slot 205 has no entry.

  python3 tools/bakery/name_catalogue.py            summary only
  python3 tools/bakery/name_catalogue.py --list     every named slot
  python3 tools/bakery/name_catalogue.py --slot 205 one slot, named or not
"""

import argparse
import os
import struct
import sys

# The repo is this script's own grandparent's parent, never a hardcoded home directory:
# this file is <repo>/tools/bakery/name_catalogue.py.
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROM_PATH = os.path.join(REPO, "data", "rom", "cnc_eu.z64")

TABLE_ROM = 0x1DE8FC        # the pointer array, indexed by model-table slot
MAX_SLOT = 512              # past the end of anything that has ever resolved
CALIB = ("GDI_UNIT_ORCAZ1", 53)
CHECK = ("ANY_TER_TR01Z1", 113)


def load_rom(path):
    if not os.path.exists(path):
        sys.exit("no ROM at %s (see the data README for where it comes from)" % path)
    with open(path, "rb") as f:
        return f.read()


def ptr(rom, slot):
    off = TABLE_ROM + 4 * slot
    if off + 4 > len(rom):
        return 0
    return struct.unpack(">I", rom[off:off + 4])[0]


def calibrate(rom):
    """RAM-to-ROM delta, derived and then confirmed. Never assumed."""
    name, slot = CALIB
    at = rom.find(name.encode() + b"\x00")
    if at < 0:
        sys.exit("calibration string %s is not in this ROM" % name)
    delta = ptr(rom, slot) - at
    cname, cslot = CHECK
    cat = rom.find(cname.encode() + b"\x00")
    if cat < 0 or ptr(rom, cslot) - delta != cat:
        sys.exit("the RAM-to-ROM delta 0x%X does not reproduce %s on slot %d; the table "
                 "or the ROM is not the one this script was written against"
                 % (delta, cname, cslot))
    return delta


def name_at(rom, delta, p):
    """The string a table entry points at, or None when it points at anything else."""
    if p == 0 or p < 0x80000000:
        return None
    off = p - delta
    if not (0 <= off < len(rom)):
        return None
    end = rom.find(b"\x00", off, off + 64)
    if end < 0:
        return None
    raw = rom[off:end]
    if len(raw) < 3 or not all(32 <= c < 127 for c in raw):
        return None
    return raw.decode(), off, end


def scan(rom, delta, max_slot=MAX_SLOT):
    out = {}
    for slot in range(max_slot):
        hit = name_at(rom, delta, ptr(rom, slot))
        if hit is not None:
            out[slot] = hit
    return out


def runs(slots):
    got, start, prev = [], None, None
    for s in slots:
        if prev is None or s != prev + 1:
            if start is not None:
                got.append((start, prev))
            start = s
        prev = s
    if start is not None:
        got.append((start, prev))
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=ROM_PATH)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--slot", type=int)
    ap.add_argument("--max-slot", type=int, default=MAX_SLOT,
                    help="how far to scan; every TOTAL below is a function of this")
    a = ap.parse_args()

    rom = load_rom(a.rom)
    delta = calibrate(rom)
    names = scan(rom, delta, a.max_slot)
    slots = sorted(names)

    if a.slot is not None:
        hit = names.get(a.slot)
        if hit is None:
            print("slot %d: NO NAME (table entry at ROM 0x%X is 0x%08X)"
                  % (a.slot, TABLE_ROM + 4 * a.slot, ptr(rom, a.slot)))
        else:
            print("slot %d: %s (string at ROM 0x%X)" % (a.slot, hit[0], hit[1]))
        return

    lo = min(h[1] for h in names.values())
    hi = max(h[2] for h in names.values())
    below = [s for s in slots if names[s][1] < 0x1B70F8]
    print("table       ROM 0x%X, 32-bit big-endian, indexed by model-table slot"
          % TABLE_ROM)
    print("delta       RAM - ROM = 0x%X (calibrated on %s/slot %d, confirmed on %s/slot %d)"
          % (delta, CALIB[0], CALIB[1], CHECK[0], CHECK[1]))
    print("named slots %d   (scanned to slot %d; this TOTAL moves with that bound)"
          % (len(slots), a.max_slot))
    print("runs        %s" % ", ".join("%d..%d" % r if r[0] != r[1] else "%d" % r[0]
                                       for r in runs(slots)))
    print("distinct    %d name strings (same caveat)"
          % len(set(h[0] for h in names.values())))
    print("blob        ROM 0x%X..0x%X" % (lo, hi))
    print("below 0x1B70F8: %d slot(s): %s"
          % (len(below), ", ".join(str(s) for s in below)))
    print("slot 205    %s"
          % ("NO NAME (table entry is 0x00000000)" if 205 not in names
             else names[205][0]))
    if a.list:
        for s in slots:
            print("  %3d  %-28s ROM 0x%X" % (s, names[s][0], names[s][1]))


if __name__ == "__main__":
    main()
