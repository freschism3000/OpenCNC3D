#!/usr/bin/env python3
"""
CNC3D -- full asset extractor for the Command & Conquer (N64) ROM.

SUPERSEDED BY archive.py, AND THIS FILE IS NOW A THIN WRAPPER OVER IT.

The two scripts grew apart. This one applied a single `ARCHIVE_BASE` to every
directory table, but the last two tables have their own data bases, so all 43 of
their records were read from the wrong address. It also decoded the compression
method as `mc >> 24` where the read-core uses `mc >> 16` plus a stored-file
correction. Measured against the ROM:

    archive.py   1624 records, 53 .BRF, ALL 53 readable text, 0 problems
    extract.py   1615 records, 39 .BRF, 25 readable and 14 NOISE, 28 problems

The noise was silent: the files were written, they had plausible sizes, and
nothing said they were garbage. Fourteen of the cartridge's briefing scripts have
been unreadable in every extracted corpus this project has produced.

The fix is deliberately NOT "copy the corrected offsets into this file too". Two
copies of the same table is how they drifted apart in the first place, and one of
the two was already right. There is now one implementation, in archive.py, and
this name keeps working because docs/re-findings.md:174 and
tools/bakery/romdump_local/extract.py cite it.

Usage is unchanged:
    python3 extract.py rom/cnc_eu.z64 assets/extracted
    python3 extract.py rom/cnc_eu.z64 assets/extracted --only INI,MAP,BRF
"""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

import archive  # noqa: E402  the one implementation


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    rom, outdir = sys.argv[1], sys.argv[2]
    only = None
    if "--only" in sys.argv:
        only = {e.strip().upper()
                for e in sys.argv[sys.argv.index("--only") + 1].split(",") if e.strip()}

    data = archive.load(rom)
    os.makedirs(outdir, exist_ok=True)
    n = archive.cmd_extract(data, outdir, decompress=True)

    if only:
        # archive.py writes everything; honour --only by pruning after the fact rather
        # than by reimplementing its walk. Slower, and correct.
        kept = 0
        for root, _dirs, files in os.walk(outdir):
            for fn in files:
                ext = os.path.splitext(fn)[1].lstrip(".").upper()
                if ext and ext not in only:
                    os.remove(os.path.join(root, fn))
                else:
                    kept += 1
        print("kept %d files matching --only %s" % (kept, ",".join(sorted(only))))
    return 0 if n is None else 0


if __name__ == "__main__":
    raise SystemExit(main())
