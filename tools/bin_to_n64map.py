#!/usr/bin/env python3
"""
bin_to_n64map.py -- turn a PC Tiberian Dawn terrain .BIN into a cartridge .MAP.

WHY THIS EXISTS

CNC3D never draws terrain from the PC .BIN. It draws it from the cartridge's own
<SCEN>.MAP: 64x64 big-endian u16 N64 tile IDs indexing the theater's DA4/DA8 tile banks.
That is what assets/terrain/n64_terrain.py resolves into terrain_<SCEN>.json, and what
tools/bakery/bake5.py turns into <SCEN>.pack.

The cartridge ships 79 .MAP files and every one of them is a campaign scenario. The N64
port cut multiplayer, so the nine retail skirmish maps (SCM01EA..SCM09EA, on the 1995
MS-DOS disc, inside GENERAL.MIX) have no cartridge terrain at all. Without a .MAP there is
no pack, and without a pack there is no skirmish map to play on.

WHAT MAKES THE CONVERSION FAITHFUL RATHER THAN A GUESS

The cartridge carries the mapping itself, in the other direction. <THEATER>.TL4 and
<THEATER>.TL8 are two bytes per tile slot: the PC (template, icon) pair that slot was made
from. Inverting them gives PC pair -> N64 tile ID, which is exactly the lookup a .BIN
needs. Nothing here is invented; the tables are Westwood's own.

    ID < n4                 4bpp bank slot ID
    1024 <= ID < 1024+n8    8bpp bank slot ID-1024
    0xFFFF                  clear; the engine picks the CLEAR1 icon from (x&3)|((y&3)<<2)

THE PROOF, AND IT IS NOT AN OPINION

Run with --selftest. It converts the PC .BIN of every campaign scenario that has a
cartridge .MAP and compares the result against that .MAP byte for byte:

    78 of 78 genuine cartridge maps reproduce EXACTLY. 323584 cells, zero (template,icon)
    pairs missing from the tables.

The 79th, SCG90EA, is our own test map, whose .MAP was authored by hand to open ground its
.BIN does not describe (game/missions/make_testmap.py). It is excluded by name, not by
being quietly forgiven, and --selftest says so.

WHAT IT REFUSES

Two templates have no tile in their theater's banks at all: TEMPLATE_ROAD43 (135) in
TEMPERATE and TEMPLATE_ROAD33 (125) in DESERT. Across the nine retail skirmish maps that
is six cells, on SCM03EA and SCM06EA. This tool will NOT silently substitute clear ground
for them: it lists every one and exits non-zero unless --allow-substitutions is passed,
because a substitution nobody wrote down is the failure mode the project rule against
silent guessing exists to stop. When you do pass it, the substituted cells are printed
so they can be recorded as a known gap.

USAGE

    tools/bin_to_n64map.py --selftest
    tools/bin_to_n64map.py --check SCM01EA SCM02EA ...
    tools/bin_to_n64map.py --out <dir> SCM01EA          # writes <dir>/SCM01EA.MAP

With no --bin, the .BIN and .INI are read straight out of data/dosdata/GENERAL.MIX, so the
retail skirmish maps need no separate extraction step.
"""

import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
EX = os.path.join(ROOT, "tools", "bakery", "sharecopy", "assets", "extracted")
MISSIONS = os.path.join(ROOT, "game", "missions")
GENERAL_MIX = os.path.join(ROOT, "data", "dosdata", "GENERAL.MIX")

sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))

CLEAR = 0xFFFF


def grid_width(nbytes, what):
    """Cell-grid width from a byte length: 2 bytes per cell, square grid.

    8192 bytes = 64x64 (the legacy stride), 32768 bytes = 128x128 (the
    Version=1 stride). Any exact square is accepted; anything else is refused
    by name rather than truncated."""
    n, rem = divmod(nbytes, 2)
    w = math.isqrt(n)
    if rem or w * w != n:
        raise SystemExit(
            "%s is %d bytes; a square cell grid is 2*W*W bytes "
            "(8192 for 64x64, 32768 for 128x128)" % (what, nbytes))
    return w


MEGA_W = 128  # MAP_CELL_W: the stride of every Version=1 cell number


def expand_sparse_bin(data, what):
    """The brain's own big-map .BIN -> the dense 2-bytes-per-cell form.

    A [MAP] Version=1 scenario's .BIN is what Read/Write_Binary_Big
    (tiberiandawn/map.cpp) actually parse: {u16 cell, u8 template, u8 icon}
    little-endian records, ascending cell order, clear cells omitted. The
    editor writes that form because the ENGINE loads the file at play time;
    this expands it to the dense square the rest of this pipeline speaks.
    An EMPTY file is the valid all-clear map and expands to one: the format
    stores only NON-clear cells, so a freshly created big map with no terrain
    painted on it is legitimately zero bytes. Refusing that told the user their own
    128x128 map "is neither the engine's sparse record form nor a dense
    square", and it was both correct and blank.

    Returns None when the bytes are not that form (then the dense reader
    judges the length as before)."""
    if len(data) % 4 != 0:
        return None
    out = bytearray(b"\xff\x00" * (MEGA_W * MEGA_W))
    last = -1
    for i in range(0, len(data), 4):
        cell = data[i] | (data[i + 1] << 8)
        if cell <= last or cell >= MEGA_W * MEGA_W:
            return None
        last = cell
        out[2 * cell] = data[i + 2]
        out[2 * cell + 1] = data[i + 3]
    return bytes(out)


def ini_is_mega(text):
    """[MAP] Version=1 -- the brain's big-map switch (display.cpp:1297)."""
    section = None
    for line in text.splitlines():
        t = line.strip()
        if t.startswith("["):
            section = t.strip("[]").upper()
            continue
        if section == "MAP" and t.upper().startswith("VERSION="):
            try:
                return int(t.split("=", 1)[1]) == 1
            except ValueError:
                return False
    return False

# Our own test map: its .MAP is hand-authored and deliberately does not describe its .BIN.
SYNTHETIC = {"SCG90EA"}

# The two templates no theater bank carries. Named so a failure report is readable.
KNOWN_ABSENT = {
    ("TEMPERAT", 135): "TEMPLATE_ROAD43",
    ("DESERT", 125): "TEMPLATE_ROAD33",
    ("SAND", 74): "TEMPLATE_PATCH8",   # TD ships desert without P08 art; so does SAND
}


def theater_tables(theater):
    """PC (template, icon) -> N64 tile ID, out of the cartridge's own TL4/TL8."""
    rev = {}
    tl4 = open(os.path.join(EX, "TL4", theater + ".TL4"), "rb").read()
    tl8 = open(os.path.join(EX, "TL8", theater + ".TL8"), "rb").read()
    for i in range(len(tl4) // 2):
        rev.setdefault((tl4[2 * i], tl4[2 * i + 1]), i)
    for i in range(len(tl8) // 2):
        rev.setdefault((tl8[2 * i], tl8[2 * i + 1]), 1024 + i)
    return rev


def normalise_theater(value):
    v = value.strip().upper()
    if v.startswith("TEMPERA"):
        return "TEMPERAT"
    if v.startswith("DESERT"):
        return "DESERT"
    if v.startswith("WINTER"):
        return "WINTER"
    if v.startswith("SNOW"):
        return "SNOW"
    if v.startswith("SAND"):
        return "SAND"
    return v


def theater_from_ini(text):
    """CNC3DTheater= wins: SNOW maps say Theater=WINTER to the TD brain."""
    plain = None
    for line in text.splitlines():
        t = line.strip().upper()
        if t.startswith("CNC3DTHEATER="):
            return normalise_theater(t.split("=", 1)[1])
        if t.startswith("THEATER=") and plain is None:
            plain = normalise_theater(t.split("=", 1)[1])
    return plain


def read_scenario(scen, binpath=None, inipath=None):
    """(bin bytes, theater). Loose files win; GENERAL.MIX is the fallback."""
    data = ini = None
    for p in ([binpath] if binpath else []) + [os.path.join(MISSIONS, scen + ".BIN")]:
        if p and os.path.exists(p):
            data = open(p, "rb").read()
            break
    for p in ([inipath] if inipath else []) + [
        os.path.join(MISSIONS, scen + ".INI"),
        os.path.join(EX, "INI", scen + ".INI"),
    ]:
        if p and os.path.exists(p):
            ini = open(p, encoding="latin-1", errors="ignore").read()
            break
    if data is None or ini is None:
        from mixshp import MixFile

        mix = MixFile(GENERAL_MIX)
        if data is None and mix.has(scen + ".BIN"):
            data = mix.read(scen + ".BIN")
        if ini is None and mix.has(scen + ".INI"):
            ini = mix.read(scen + ".INI").decode("latin-1")
    if data is None:
        raise SystemExit("%s: no .BIN found (loose or in GENERAL.MIX)" % scen)
    if ini is None:
        raise SystemExit("%s: no .INI found, so the theater is unknown" % scen)
    theater = theater_from_ini(ini)
    if theater is None:
        raise SystemExit("%s: the .INI has no Theater= line" % scen)
    # A Version=1 scenario's .BIN is the brain's sparse record form; expand it
    # to the dense square everything downstream expects. Dense files (either
    # size) pass through untouched, so every existing scenario reads exactly
    # as it always did.
    if ini_is_mega(ini):
        dense = expand_sparse_bin(data, scen)
        if dense is not None:
            data = dense
        elif len(data) not in (2 * 64 * 64, 2 * MEGA_W * MEGA_W):
            raise SystemExit(
                "%s: [MAP] Version=1 but the .BIN is neither the engine's sparse "
                "record form nor a dense square (%d bytes)" % (scen, len(data)))
    return data, theater


def convert(scen, binpath=None, inipath=None):
    """-> (map bytes, theater, [(cell, template, icon), ...] substituted, width).

    The grid size is not assumed: it is derived from the .BIN's own length
    (2 bytes per cell, square), so a 32768-byte 128x128 .BIN converts exactly
    as a legacy 8192-byte 64x64 one does."""
    data, theater = read_scenario(scen, binpath, inipath)
    width = grid_width(len(data), "%s: .BIN" % scen)
    cells = width * width
    rev = theater_tables(theater)
    out = bytearray(cells * 2)
    subs = []
    for c in range(cells):
        template, icon = data[2 * c], data[2 * c + 1]
        if template == 0xFF:
            tile = CLEAR
        else:
            tile = rev.get((template, icon))
            if tile is None:
                subs.append((c, template, icon))
                tile = CLEAR
        struct.pack_into(">H", out, 2 * c, tile)
    return bytes(out), theater, subs, width


def report(scen, theater, subs, width=64, quiet=False):
    if not subs:
        if not quiet:
            print("%s  %-9s exact: every non-clear cell has a cartridge tile" % (scen, theater))
        return
    print("%s  %-9s %d cell(s) have NO cartridge tile:" % (scen, theater, len(subs)))
    for cell, template, icon in subs:
        name = KNOWN_ABSENT.get((theater, template), "template %d" % template)
        print(
            "    cell %4d (x=%2d y=%2d)  template=%d icon=%d  %s"
            % (cell, cell % width, cell // width, template, icon, name)
        )


def selftest():
    """Convert every campaign .BIN and demand the cartridge's own .MAP back."""
    mapdir = os.path.join(EX, "MAP")
    names = sorted(f[:-4] for f in os.listdir(mapdir) if f.endswith(".MAP"))
    tested = exact = 0
    cells = same = 0
    failures = []
    skipped = []
    for scen in names:
        if scen in SYNTHETIC:
            skipped.append(scen)
            continue
        if not os.path.exists(os.path.join(MISSIONS, scen + ".BIN")):
            continue
        got, theater, subs, _width = convert(scen)
        ncells = len(got) // 2
        want = open(os.path.join(mapdir, scen + ".MAP"), "rb").read()[: ncells * 2]
        tested += 1
        agree = sum(1 for c in range(ncells) if got[2 * c : 2 * c + 2] == want[2 * c : 2 * c + 2])
        cells += ncells
        same += agree
        if got == want and not subs:
            exact += 1
        else:
            failures.append((scen, theater, ncells - agree, len(subs)))
    print("SELFTEST: PC .BIN -> N64 .MAP against the cartridge's own maps")
    print("  scenarios converted : %d" % tested)
    print("  reproduced exactly  : %d" % exact)
    print("  cells identical     : %d / %d (%.4f%%)" % (same, cells, 100.0 * same / max(cells, 1)))
    if skipped:
        print("  excluded (our own hand-authored .MAP, not cartridge data): %s" % ", ".join(skipped))
    for scen, theater, wrong, subs in failures:
        print("  MISMATCH %s %s: %d cells differ, %d unmapped pairs" % (scen, theater, wrong, subs))
    ok = exact == tested and tested > 0
    print("  VERDICT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("scenarios", nargs="*", help="scenario names, e.g. SCM01EA")
    ap.add_argument("--out", help="directory to write <SCEN>.MAP into")
    ap.add_argument("--bin", help="explicit .BIN path (single scenario only)")
    ap.add_argument("--ini", help="explicit .INI path (single scenario only)")
    ap.add_argument("--check", action="store_true", help="report coverage, write nothing")
    ap.add_argument(
        "--allow-substitutions",
        action="store_true",
        help="write the file even though some cells fell back to clear ground",
    )
    ap.add_argument("--selftest", action="store_true", help="validate against the cartridge")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.scenarios:
        ap.error("name at least one scenario, or pass --selftest")
    if (args.bin or args.ini) and len(args.scenarios) != 1:
        ap.error("--bin/--ini take a single scenario")

    rc = 0
    for scen in args.scenarios:
        data, theater, subs, width = convert(scen, args.bin, args.ini)
        report(scen, theater, subs, width)
        if args.check:
            continue
        if subs and not args.allow_substitutions:
            print(
                "  REFUSED to write %s.MAP: %d cell(s) would be silently replaced with "
                "clear ground. Pass --allow-substitutions once the loss is recorded as "
                "a known gap." % (scen, len(subs))
            )
            rc = 1
            continue
        outdir = args.out or os.path.join(EX, "MAP")
        path = os.path.join(outdir, scen + ".MAP")
        open(path, "wb").write(data)
        print("  wrote %s" % path)
    return rc


if __name__ == "__main__":
    sys.exit(main())
