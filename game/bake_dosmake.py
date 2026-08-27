#!/usr/bin/env python3
"""
bake_dosmake.py -- flatten the 1995 MS-DOS construction ("buildup") animations into
one little-endian pack, `dosmake.pack`, so the renderer can draw the scaffold while
the ENGINE runs its construction state machine (which it already does: selling has
worked since the 21-entry CONQUER.MIX shipped; only the art pass was missing).

Everything is transcribed from the GPL engine sources or decoded by the proven SHP
decoder (menu/tools/mixshp.py). Nothing is guessed.

WHAT THE ENGINE SAYS (citations, all under brain/vanilla/tiberiandawn):

  * Frame selection -- building.cpp:500 BuildingClass::Draw_It:
        shapenum = Fetch_Stage();
        if (BState == BSTATE_CONSTRUCTION) {
            shapefile = Class->Get_Buildup_Data();          // <ini>MAKE.SHP
            if (Mission == MISSION_DECONSTRUCTION)
                shapenum = (Anims[BState].Start + Anims[BState].Count - 1) - shapenum;
        }
    and bdata.cpp:3849 Init_Anim(BSTATE_CONSTRUCTION, 0, count, timedelay) with
    count = Get_Build_Frame_Count(MAKE.SHP) and timedelay = 5*TICKS_PER_SECOND/count,
    i.e. the whole buildup spans ~5 seconds whatever the frame count. Begin_Mode
    (building.cpp) does Set_Stage(Start=0), so Fetch_Stage() IS the frame index,
    ascending 0..count-1 for construction and REVERSED by the draw for selling.
  * House colours   -- house.cpp:2186 Remap_Table(GAME_NORMAL, unit=false):
    Nod (HOUSE_BAD) BUILDINGS remap through RemapRed ("as opposed to the default
    color of bluegrey"); everyone else through Class->RemapTable, which for the
    shipped houses is the identity. So exactly two pixel variants exist: identity
    (gold) and red. const.cpp:293 RemapRed rewrites only indices 176..191.
  * Shadow          -- same ghost rule as the infantry pack: index 4 is the DOS
    shadow colour; the renderer maps 0 and 4 to alpha 0.
  * Palette         -- TEMPERAT.PAL from OUR LOCAL.MIX copy, the same source and
    the same guard as bake_dosinfantry.py (the repacked content/TEMPERAT.MIX
    carries a WRONG palette; never source it there).
  * World scale     -- DOS building art is 24 px per footprint cell (measured:
    every MAKE frame is exactly fw*24 wide). The terrain atlas is 24 texels per
    cell and sprite_texels_per_unit() is 24, so baking 1:1 and dividing by tpu
    puts the scaffold at native world scale on the building's own footprint.

VOODOO 2 FIT -- PAGED SHEETS, NOT SUBSAMPLED FRAMES:

  The infantry baker subsamples a strip that overflows 256x256 (only E4's two
  16-stage rows needed it). Buildups are much bigger art (TMPL is 72x72 x 36
  frames) and subsampling them would cut a 5-second animation to 5 visible steps,
  so this pack SPLITS a strip across several 256x256 sheets instead: frame f
  lives on sheet f // per_sheet at grid slot f % per_sheet. Still GL 1.1, still
  Voodoo 2 legal (more texture binds, which a 5-second one-shot can afford).

FORMAT (all little-endian), magic "DOSMAKE1":

    char  magic[8]      "DOSMAKE1"
    u32   version       1
    u32   strip_count
    u32   type_count
    u8    pal6[768]     TEMPERAT.PAL as it sits on the CD (6-bit DAC)
    u8    pal8[768]     v << 2, max 252
    strip_count x {
        char name[16]   e.g. "WEAPMAKE#0"  (#0 gold/identity, #1 red)
        u32  frames, fw, fh
        u32  cols, rows, texw, texh, nsheets   per_sheet = cols*rows;
                                               sheet s covers frames
                                               [s*per_sheet, ...)
        nsheets x u8 idx[texw*texh]   8-bit palette indices, house remap ALREADY
                                      applied; 0 transparent, 4 shadow ghost
    }
    type_count x {
        char ini[8]     the building's INI name (NUKE, PYLE, ...)
        i32  strip[2]   house rows GOLD, RED
    }
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "menu", "tools")))

from mixshp import MixFile, Shape  # noqa: E402

# The shipped minimal CONQUER.MIX holds exactly the 21 buildups (make_cnc3dmake.py
# regenerates it from the CD). The playable folder is its canonical home.
CONQUER_CANDIDATES = [
    os.path.join(HERE, "..", "playable", "CONQUER.MIX"),
    os.path.join(HERE, "dosdata", "CONQUER.MIX"),
]
LOCAL_CANDIDATES = [
    os.path.join(HERE, "..", "data", "dosdata", "LOCAL.MIX"),
    os.path.join(HERE, "dosdata", "LOCAL.MIX"),
]
OUT = os.path.join(HERE, "dosmake.pack")

MAGIC = b"DOSMAKE1"
VERSION = 1
TRANSPARENT, SHADOW = 0, 4
MAX_TEX = 256

# INI name -> MAKE stem. All 21 verified present in the shipped CONQUER.MIX with
# these frame counts (FACT 32, SAM 30, TMPL 36, ATWR/AFLD 14, BIO 16, rest 20).
TYPES = ["FACT", "NUKE", "PYLE", "WEAP", "PROC", "SILO", "HQ", "GUN", "SAM",
         "OBLI", "GTWR", "ATWR", "HAND", "AFLD", "HPAD", "FIX", "BIO", "HOSP",
         "TMPL", "EYE", "NUK2"]

# const.cpp:293 RemapRed, indices 176..191 only (the rest is identity).
RED_176_191 = [127, 126, 125, 124, 122, 46, 120, 47, 125, 124, 123, 122, 42, 121, 120, 120]


def remap_red(i):
    return RED_176_191[i - 176] if 176 <= i <= 191 else i


def next_pot(v):
    p = 1
    while p < v:
        p <<= 1
    return p


def first_existing(paths, what):
    for p in paths:
        if os.path.isfile(p):
            return p
    raise SystemExit("cannot find %s (tried %s)" % (what, ", ".join(paths)))


def pack_name(s, n):
    b = s.encode("ascii")
    assert len(b) <= n, s
    return b + b"\x00" * (n - len(b))


def main():
    cq = MixFile(first_existing(CONQUER_CANDIDATES, "CONQUER.MIX"))
    pal6 = MixFile(first_existing(LOCAL_CANDIDATES, "LOCAL.MIX")).read("TEMPERAT.PAL")
    assert len(pal6) == 768 and max(pal6) <= 63
    # the same wrong-palette guard as the infantry bake: the CD palette's gold
    # range starts (61,53,30) at index 176
    assert tuple(pal6[176 * 3:176 * 3 + 3]) == (61, 53, 30), "wrong TEMPERAT.PAL source"
    pal8 = bytes(min(252, v << 2) for v in pal6)

    strips = []   # (name, frames, fw, fh, cols, rows, texw, texh, [sheet bytes])
    types = []    # (ini, [gold_id, red_id])
    report = {}

    for ini in TYPES:
        shp = Shape(cq.read(ini + "MAKE.SHP"))
        fw, fh, nfr = shp.width, shp.height, shp.frames
        # every MAKE frame is footprint-sized DOS art: fw is a whole number of
        # 24 px cells (measured across all 21; OBLI/ATWR are 1 cell wide, 2 tall)
        assert fw % 24 == 0, (ini, fw)

        cols = max(1, MAX_TEX // fw)
        rows = max(1, MAX_TEX // fh)
        per_sheet = cols * rows
        nsheets = (nfr + per_sheet - 1) // per_sheet
        # last sheet may be shorter; all sheets share one POT size for simplicity
        texw = next_pot(min(cols, nfr) * fw)
        texh = next_pot(min(rows, (nfr + cols - 1) // cols) * fh)
        assert texw <= MAX_TEX and texh <= MAX_TEX, (ini, texw, texh)

        for hname, remap in ((0, None), (1, remap_red)):
            sheets = []
            for s in range(nsheets):
                sheet = bytearray(texw * texh)
                for i in range(per_sheet):
                    f = s * per_sheet + i
                    if f >= nfr:
                        break
                    buf = shp.frame(f)
                    cx, cy = (i % cols) * fw, (i // cols) * fh
                    for yy in range(fh):
                        row = buf[yy * fw:(yy + 1) * fw]
                        for xx in range(fw):
                            v = row[xx]
                            if remap and v not in (TRANSPARENT, SHADOW):
                                v2 = remap(v)
                                assert v2 not in (TRANSPARENT, SHADOW), (ini, v, v2)
                                v = v2
                            sheet[(cy + yy) * texw + cx + xx] = v
                sheets.append(bytes(sheet))
            name = "%sMAKE#%d" % (ini, hname)
            if hname == 0:
                types.append((ini, [len(strips), -1]))
            else:
                types[-1][1][1] = len(strips)
            strips.append((name, nfr, fw, fh, cols, rows, texw, texh, sheets))
        report[ini] = {"frames": nfr, "fw": fw, "fh": fh,
                       "cells": [fw // 24, fh // 24],
                       "sheets": nsheets, "tex": [texw, texh]}

    with open(OUT, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<III", VERSION, len(strips), len(types)))
        fh.write(pal6)
        fh.write(pal8)
        for name, nfr, fw, fh2, cols, rows, texw, texh, sheets in strips:
            fh.write(pack_name(name, 16))
            fh.write(struct.pack("<8I", nfr, fw, fh2, cols, rows, texw, texh, len(sheets)))
            for s in sheets:
                fh.write(s)
        for ini, ids in types:
            fh.write(pack_name(ini, 8))
            fh.write(struct.pack("<2i", ids[0], ids[1]))

    manifest = {
        "pack": os.path.basename(OUT), "magic": MAGIC.decode(), "version": VERSION,
        "bytes": os.path.getsize(OUT),
        "frame_rule": "frame = engine Fetch_Stage(); Deconstruction draws frames-1-stage "
                      "(building.cpp:500 Draw_It, transcribed)",
        "houses": "#0 identity (everyone), #1 RemapRed (Nod BUILDINGS only, "
                  "house.cpp:2186); red rewrites indices 176..191 (const.cpp:293)",
        "shadow": "index 4 kept in data, renderer maps it (and 0) to alpha 0",
        "paging": "frame f -> sheet f // (cols*rows), slot f %% (cols*rows); "
                  "sheets are all <= 256x256 for the Voodoo 2, no frame subsampling",
        "types": report,
    }
    with open(os.path.splitext(OUT)[0] + ".manifest.json", "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)

    print("wrote %s: %d strips, %d types, %.1f KB"
          % (OUT, len(strips), len(types), os.path.getsize(OUT) / 1024.0))
    for ini in TYPES:
        r = report[ini]
        print("  %-4s %2d frames %dx%d px (%dx%d cells) %d sheet(s) %dx%d"
              % (ini, r["frames"], r["fw"], r["fh"], r["cells"][0], r["cells"][1],
                 r["sheets"], r["tex"][0], r["tex"][1]))


if __name__ == "__main__":
    main()
