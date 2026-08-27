#!/usr/bin/env python3
"""
bake_smudges.py -- bake the cartridge's own scorch marks, craters and building
aprons into one little-endian pack, `smudge.pack`.

  python3 game/bake_smudges.py            -> game/smudge.pack

WHAT A SMUDGE IS, on both sides.

The engine keeps it per CELL, not per object: CellClass carries `SmudgeType Smudge`
and `unsigned char SmudgeData` (cell.h:122). anim.cpp drops a random SCORCH1..6 under
an explosion and a CRATER1 under a bigger one; building.cpp lays a BIB apron under a
structure. The brain exports one SMUDGE|x|y|type|data line per such cell, and `data`
is already the frame number the console uses -- SmudgeClass::Mark computes
`w + h * Class->Width` for the multi-cell aprons and stacks craters by incrementing
then clamping to 4 (smudge.cpp:212, 218-219), which is byte for byte what the
cartridge does at ROM 0x16D824.

THE ART IS THE CARTRIDGE'S OWN, and it is named by the cartridge. The terrain loader
reads fifteen strips into consecutive slots at terrainState+0xC068 in exactly this
order, which is the ART INDEX this pack is keyed by:

    art 1..3    BIB1, BIB2, BIB3      the building aprons, 8 / 6 / 4 cells
    art 4..9    CR1..CR6              craters, 5 stack levels each
    art 10..15  SC1..SC6              scorch marks, one frame each

and the renderer maps the engine's SmudgeType onto it with
`art = (t <= 11) ? t + 4 : t - 11`, because the engine enumerates CRATER1..6,
SCORCH1..6, BIB1..3 while the cartridge loads the bibs first.

TWO THEATERS AND NO MORE. The loader tests TheaterType and reads the TEM_ strips for
temperate (2) and the DES_ strips for desert (0); jungle and winter load nothing,
because the cartridge ships no art for them. That is the cartridge's answer, not a
gap, and the renderer draws no decals in those theaters rather than substituting.

FORMAT. One RGBA sheet per theater, laid out as a fixed grid so no slot table is
needed: column = frame (0..7), row = art index - 1 (0..14).

    "SMUDGE1\\0"                     8 bytes
    u32 version                     1
    u32 theaters                    2
    u32 fw, fh                      24, 24
    u32 types                       15
    u32 maxframes                   8
    per theater:
        char name[12]               "TEMPERATE" / "DESERT"
        u32 theater_id              the engine's TheaterType (2 / 0)
        u32 texw, texh              192, 360
        u32 nframes[types]          how many of the 8 columns are real
        u8  rgba[texh][texw][4]

The alpha is the cartridge's own 1-bit key, strictly 0 or 255 in all thirty files,
which is what lets the renderer draw these with an alpha test on Tier 1 instead of
needing a blend. (The console itself sets G_AC_NONE and blends under FORCE_BL; the
key being strictly binary is what makes the two identical. Recorded as a known
gap.)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "romdump"))

import importlib.util

_spec = importlib.util.spec_from_file_location(
    "imgsqueeze", os.path.join(ROOT, "tools", "romdump", "imgsqueeze.py"))
IMG = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(IMG)

FW = FH = 24
MAXFRAMES = 8
# In the cartridge's own load order, which is the art index.
ART = (["BIB1", "BIB2", "BIB3"]
       + ["CR%d" % i for i in range(1, 7)]
       + ["SC%d" % i for i in range(1, 7)])
TYPES = len(ART)                       # 15
THEATERS = [("TEMPERATE", "TEM", 2), ("DESERT", "DES", 0)]

# What each strip MUST be. Asserted rather than trusted, because a strip that quietly
# changed length would silently move every frame after it: the aprons are w*h cells
# (BIB1 4x2, BIB2 3x2, BIB3 2x2 -- sdata.cpp), a crater stacks 0..4, and a scorch has
# one frame and no state at all.
EXPECT_FRAMES = {"BIB1": 8, "BIB2": 6, "BIB3": 4,
                 "CR1": 5, "CR2": 5, "CR3": 5, "CR4": 5, "CR5": 5, "CR6": 5,
                 "SC1": 1, "SC2": 1, "SC3": 1, "SC4": 1, "SC5": 1, "SC6": 1}


def load_strip(rom, name):
    """-> (frames, [rgba bytes per frame]) for one <THEATER>_<ART>.IMG."""
    data = IMG.find(rom, name)
    if data is None:
        return None
    res = IMG.imgmod.decode(data)
    if res is None:
        raise SystemExit("%s: the 16-byte IMG header does not solve" % name)
    meta, rows = res
    w, h = meta["w"], meta["h"]
    if h != FH or w % FW:
        raise SystemExit("%s is %dx%d, not a strip of %dx%d frames" % (name, w, h, FW, FH))
    n = w // FW
    frames = []
    for k in range(n):
        buf = bytearray()
        for y in range(FH):
            off = (k * FW) * 4
            buf += rows[y][off:off + FW * 4]
        # The alpha is a 1-bit key in every one of the thirty files. Assert it: an
        # anti-aliased edge would mean the alpha TEST the renderer uses is wrong and
        # a blend is needed, and that is a decision, not a detail.
        for i in range(3, len(buf), 4):
            if buf[i] not in (0, 255):
                raise SystemExit(
                    "%s frame %d has alpha %d: the art is no longer a 1-bit key, so "
                    "the renderer's alpha test is no longer equivalent to the "
                    "console's blend. Decide before baking." % (name, k, buf[i]))
        frames.append(bytes(buf))
    return frames


def bake(rom, out):
    sheets = []
    for tname, prefix, tid in THEATERS:
        texw, texh = MAXFRAMES * FW, TYPES * FH
        sheet = bytearray(texw * texh * 4)
        counts = []
        for row, art in enumerate(ART):
            frames = load_strip(rom, "%s_%s.IMG" % (prefix, art))
            if frames is None:
                raise SystemExit("%s_%s.IMG is not in the ROM" % (prefix, art))
            want = EXPECT_FRAMES[art]
            if len(frames) != want:
                raise SystemExit(
                    "%s_%s.IMG has %d frames, expected %d. The engine indexes these "
                    "with SmudgeData and would read off the end." % (
                        prefix, art, len(frames), want))
            counts.append(len(frames))
            for col, fr in enumerate(frames):
                for y in range(FH):
                    dst = ((row * FH + y) * texw + col * FW) * 4
                    src = y * FW * 4
                    sheet[dst:dst + FW * 4] = fr[src:src + FW * 4]
        sheets.append((tname, tid, texw, texh, counts, bytes(sheet)))
        opaque = sum(1 for i in range(3, len(sheet), 4) if sheet[i])
        print("%-10s %d strips, %d frames, %d opaque texels on a %dx%d sheet"
              % (tname, TYPES, sum(counts), opaque, texw, texh))

    with open(out, "wb") as f:
        f.write(b"SMUDGE1\0")
        for v in (1, len(sheets), FW, FH, TYPES, MAXFRAMES):
            f.write(struct.pack("<I", v))
        for tname, tid, texw, texh, counts, sheet in sheets:
            nm = tname.encode()[:12]
            f.write(nm + b"\0" * (12 - len(nm)))
            for v in (tid, texw, texh):
                f.write(struct.pack("<I", v))
            for c in counts:
                f.write(struct.pack("<I", c))
            f.write(sheet)
    print("wrote %s (%.1f KB)" % (out, os.path.getsize(out) / 1024.0))


def main():
    romp = os.path.join(ROOT, "data", "rom", "cnc_eu.z64")
    if not os.path.isfile(romp):
        raise SystemExit("no ROM at %s" % romp)
    rom = open(romp, "rb").read()
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "smudge.pack")
    bake(rom, out)


if __name__ == "__main__":
    main()
