#!/usr/bin/env python3
"""
bake_dossidebar.py -- flatten everything the 1995 MS-DOS Command & Conquer sidebar
needs into one little-endian pack, `dossidebar.pack`, for the Win98 / Voodoo 2
renderer to mmap and use with zero decoding at runtime.

Same conventions as eyes/bake_cameos.py: 8 byte magic, u32 version, u32 counts,
fixed-size NUL padded name fields, everything little-endian, payload inline.

ONE DELIBERATE DIFFERENCE from bake_cameos.py: shapes are stored as raw 8-bit
PALETTE INDICES, not RGBA, and they are NOT padded to a power of two.

  * The sidebar is a palettised composite. Index 0 is transparent (common/keybuff.cpp
    BF_Trans), the build clock is a translucency LOOKUP against the destination pixel
    (BF_Ghost_Trans: sbyte = ghost_tab[*dst + fbyte*256]), and the fonts are a 4bpp
    class-per-pixel format resolved through a 16 entry font palette at print time
    (common/font.cpp Buffer_Print). None of that survives a premature conversion to
    RGBA. Composite in 8-bit exactly like the DOS engine, convert once at the end.
  * Because the conversion happens once, the only thing that ever reaches OpenGL is a
    single 320x200-derived RGBA texture, which IS padded to a power of two in the
    renderer. Individual shapes never become textures, so POT padding here would only
    waste space.

FORMAT (all little-endian)
    char  magic[8]        "DOSBAR01"
    u32   version         1
    u32   shape_count
    u32   font_count
    u32   pal_entries     256
    u8    pal6[768]       the palette exactly as it sits in TEMPERAT.PAL, 6-bit DAC
    u8    pal8[768]       pal6 widened the way the engine widens it: v << 2, max 252
                          (video_ddraw.cpp:910, video_sdl2.cpp:653; NOT (v<<2)|(v>>4))
    u8    clocktab[512]   SidebarClass::StripClass::ClockTranslucentTable
                          [0..255]   ghost_lookup, 0xFF = opaque
                          [256..511] fading table toward LTGREY at 180/256
    shape_count x {
        char name[12]     "E1", "MTNK", "CLOCK", "STRIP", "STRIPUP", ...  (no ".SHP")
        u32  frames, width, height
        u8   pixels[frames * width * height]     palette indices, 0 = transparent
    }
    font_count x {
        char name[8]      "6POINT", "8POINT"
        u32  nchars, maxw, maxh
        u8   width[nchars]    Char_Pixel_Width = this + FontXSpacing
        u8   ytop[nchars]     rows of blank above the stored rows
        u8   rows[nchars]     stored rows
        u8   cells[nchars * maxw * maxh]   one byte per pixel, value 0..3, the nibble
                                           CLASS, resolved through fontpalette[] at
                                           print time (dialog.cpp Simple_Text_Print)
    }

Sources: everything is decoded by tools/mixshp.py and tools/decode_fnt.py, which are
ports of brain/vanilla/common/{lcw,keyframe,font}.cpp. Nothing here is guessed.
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# This copy lives in next/cursors/; the decode tooling stays where it always was.
SIDEBAR = os.path.normpath(os.path.join(HERE, "..", "..", "sidebar"))
sys.path.insert(0, os.path.join(SIDEBAR, "tools"))

from mixshp import MixSet, Shape, ShapeBlock, MIX_PATHS, SCRATCH  # noqa: E402
from decode_fnt import Font                                       # noqa: E402

OUT = os.path.join(HERE, "dossidebar.pack")

MAGIC = b"DOSBAR01"
VERSION = 1

# ---------------------------------------------------------------------------
# What goes in.
# ---------------------------------------------------------------------------

# 49 cameos were named up front. The decode pass proved 61 exist in the 1995 archives
# (brute-forced over [A-Z0-9]{1,4}ICON.SHP, minus the two hash collisions ATRN/OLN
# that are really ATOMICUP.SHP and MINIGUN.SHP). Baking the superset costs 47 KB.
CAMEOS = """A10 AFLD APC ARCO ARTY ATOM ATWR BARB BGGY BIKE BIO BOAT BOMB BRIK C17
CYCL E1 E2 E3 E4 E5 E6 EYE FACT FIX FTNK GTWR GUN HAND HARV HELI HOSP HPAD HQ HTNK
ION JEEP LST LTNK MCV MHQ MLRS MSAM MTNK NUK2 NUKE OBLI ORCA PROC PUMP PYLE RMBO ROAD
SAM SBAG SILO STNK TMPL TRAN WEAP WOOD""".split()

# PUMPICON.SHP and ROADICON.SHP are 73 byte blanks, byte-identical to STRIP.SHP
# frame 0 (the empty bevelled cell). They are baked as found, not as failures.
KNOWN_BLANK_CAMEOS = ("PUMP", "ROAD")

# (name in the pack, name in the MIX). They differ only where the MIX name is not a
# legal pack identifier, i.e. the house-suffixed radar bezels.
CHROME = [
    ("CLOCK", "CLOCK.SHP"),      # 32x24 x109: frame 0 = "cannot build" darken,
                                 #             1..108 = the pie wedge
    ("STRIP", "STRIP.SHP"),      # 32x24 x2  : SB_BLANK empty cell, SB_FRAME
    ("STRIPUP", "STRIPUP.SHP"),  # 16x12 x2  : scroll up
    ("STRIPDN", "STRIPDN.SHP"),  # 16x12 x2  : scroll down
    ("TABS", "TABS.SHP"),        # 80x7      : the top tab plate (tab.cpp Draw_It)
    ("POWER", "POWER.SHP"),      # 8x3       : the power bar drain marker
    ("PIPS", "PIPS.SHP"),        # 34x6  x8  : [empty, green, PRIMARY, READY, HOLD,
                                 #             yellow, white, red].  sidebar.cpp:1967
                                 #             PIP_READY and :1990 PIP_HOLDING are
                                 #             BITMAPS, not 6-point text.
    ("OPTIONS", "OPTIONS.SHP"),  # 103x79 x32: goptions.cpp:570 options dialog art
    ("RADARGDI", "RADAR.GDI"),   # 80x69 x43 : the radar bezel / activation animation.
    ("RADARNOD", "RADAR.NOD"),   #             radar.cpp:348 RADAR.<house suffix>,
                                 #             drawn at (RadX, RadY+1) frame 22
                                 #             (radar.h:135 RADAR_ACTIVATED_FRAME).
    ("MOUSE", "MOUSE.SHP"),      # 30x24 x178: every cursor the game has, one shape
                                 #             block. MouseClass::MouseControl
                                 #             (mouse.cpp:304) indexes into these
                                 #             frames; cursors.h carries that table.
]

# 6POINT is the sidebar button font (sidebar.cpp:319 Set_Font(Font6Ptr)).
# GRAD6FNT is the tab font: tab.cpp's lo-res branch prints the tab captions with
# TPF_6PT_GRAD, which dialog.cpp:449 resolves to GradFont6Ptr.
# 8POINT comes along because both DOS UI fonts are wanted.
FONTS = ["6POINT", "8POINT", "GRAD6FNT"]

PALETTE = "TEMPERAT.PAL"

# ---------------------------------------------------------------------------
# Palette handling.  tiberiandawn/... never does (v<<2)|(v>>4); see the docstring.
# ---------------------------------------------------------------------------


def widen(pal6):
    return bytes(min(252, (v & 0x3F) << 2) for v in pal6)


# ---------------------------------------------------------------------------
# ClockTranslucentTable.
#
# sidebar.cpp:1307  static TLucentType const ClockCols[1] = {{GREEN, LTGREY, 180, 0}};
# sidebar.cpp:1313  GamePalette = OriginalPalette, then indices 32..38 forced to 0x3f
#                   (CYCLE_COLOR_START=32, CYCLE_COLOR_COUNT=7, defines.h:1936) so the
#                   colour-cycling entries can never be chosen as a fade match.
# sidebar.cpp:1319  Build_Translucent_Table(GamePalette, ClockCols, 1, table)
#
# jshell.cpp:318 Build_Translucent_Table and fading.cpp:14 Build_Fading_Table, ported
# byte for byte including the C integer semantics (arithmetic >>, signed char wrap,
# and the "<=" tie-break that prefers the LATER palette entry).
# ---------------------------------------------------------------------------

GREEN, LTGREY = 3, 14         # pal/palette_roles.json ui_16, pinned from the engine
CYCLE_COLOR_START, CYCLE_COLOR_COUNT = 32, 7


def _s8(v):
    v &= 0xFF
    return v - 256 if v & 0x80 else v


def _u8(v):
    return v & 0xFF


def build_fading_table(pal, color, frac):
    """fading.cpp:14 Build_Fading_Table. `pal` is 768 raw 6-bit bytes."""
    dst = bytearray(256)
    if frac >= 256:
        frac = 255
    fraction = frac >> 1
    tr, tg, tb = pal[color * 3], pal[color * 3 + 1], pal[color * 3 + 2]
    dst[0] = 0

    for i in range(1, 256):
        ideal = []
        for ch, target in ((0, tr), (1, tg), (2, tb)):
            original = pal[i * 3 + ch]
            tmp = ((original - target) * fraction) << 1   # fits a signed short
            ideal.append(_u8(original - (tmp >> 8)))      # arithmetic shift, then u8
        ir, ig, ib = ideal

        matchcolor = color
        matchvalue = 0xFFFFFFFF
        for j in range(1, 256):                           # fade starts at pal+3
            if i == j:
                continue
            d = _s8(pal[j * 3] - ir)
            value = d * d
            d = _s8(pal[j * 3 + 1] - ig)
            value += d * d
            d = _s8(pal[j * 3 + 2] - ib)
            value += d * d
            if value <= matchvalue:                       # ties take the later entry
                matchvalue = value
                matchcolor = j
            if value == 0:
                break
        dst[i] = matchcolor
    return dst


def build_clock_translucent_table(pal6):
    game = bytearray(pal6)
    for i in range(CYCLE_COLOR_START * 3, (CYCLE_COLOR_START + CYCLE_COLOR_COUNT) * 3):
        game[i] = 0x3F
    lookup = bytearray(b"\xFF" * 256)
    lookup[GREEN] = 0
    return bytes(lookup) + bytes(build_fading_table(game, LTGREY, 180))


# ---------------------------------------------------------------------------


def pack_name(s, n):
    b = s.encode("ascii")
    if len(b) > n:
        raise SystemExit("name %r does not fit in %d bytes" % (s, n))
    return b + b"\x00" * (n - len(b))


def bake_shapeblock(mixes, member):
    """MOUSE.SHP is the OLDER ShapeBlock container (getshape.cpp Extract_Shape),
    not the keyframe one. Decoded with the same tools/mixshp.py port that made
    png/mouse/. The pack stores it exactly like every other shape: raw 8-bit
    palette indices, frame after frame. All 178 frames are 30x24 (verified by
    the decode pass and re-checked here)."""
    blk = ShapeBlock(mixes.read(member))
    w0, h0 = None, None
    buf = bytearray()
    for i in range(blk.count):
        f, hdr = blk.frame(i)
        w, h = hdr["width"], hdr["height"]
        if w0 is None:
            w0, h0 = w, h
        if (w, h) != (w0, h0):
            raise SystemExit("%s frame %d is %dx%d, expected uniform %dx%d"
                             % (member, i, w, h, w0, h0))
        if len(f) != w * h:
            raise SystemExit("%s frame %d decoded %d bytes, expected %d"
                             % (member, i, len(f), w * h))
        buf += f
    return blk.count, w0, h0, bytes(buf)


def bake_shape(mixes, stem, member=None):
    if member == "MOUSE.SHP":
        return bake_shapeblock(mixes, member)
    shp = Shape(mixes.read(member if member else stem + ".SHP"))
    buf = bytearray()
    for i in range(shp.frames):
        f = shp.frame(i)
        if len(f) != shp.width * shp.height:
            raise SystemExit("%s frame %d decoded %d bytes, expected %d"
                             % (stem, i, len(f), shp.width * shp.height))
        buf += f
    return shp.frames, shp.width, shp.height, bytes(buf)


def bake_font(mixes, stem):
    f = Font(mixes.read(stem + ".FNT"), stem + ".FNT")
    maxw, maxh, n = f.max_width, f.max_height, f.nchars
    widths = bytes(f.widths)
    ytop = bytes(min(255, v) for v in f.ytop)
    rows = bytes(min(255, v) for v in f.rows)
    cells = bytearray(n * maxw * maxh)
    for c in range(n):
        w, h, cell = f.glyph(c)
        base = c * maxw * maxh
        for y in range(min(h, maxh)):
            row = cell[y]
            for x in range(min(w, maxw)):
                cells[base + y * maxw + x] = row[x]
    return n, maxw, maxh, widths, ytop, rows, bytes(cells)


def main():
    mixes = MixSet(SCRATCH, MIX_PATHS)
    if not mixes.mixes:
        raise SystemExit("no MIX archives found under %s" % SCRATCH)

    pal6 = mixes.read(PALETTE)
    if len(pal6) != 768:
        raise SystemExit("%s is %d bytes, expected 768" % (PALETTE, len(pal6)))
    if max(pal6) > 63:
        raise SystemExit("%s is not a 6-bit palette (max %d)" % (PALETTE, max(pal6)))
    pal8 = widen(pal6)
    clocktab = build_clock_translucent_table(pal6)

    shapes = []
    for stem in CAMEOS:
        shapes.append((stem,) + bake_shape(mixes, stem + "ICON"))
    for name, member in CHROME:
        shapes.append((name,) + bake_shape(mixes, name, member))

    fonts = [(stem,) + bake_font(mixes, stem) for stem in FONTS]

    with open(OUT, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<IIII", VERSION, len(shapes), len(fonts), 256))
        fh.write(pal6)
        fh.write(pal8)
        fh.write(clocktab)
        for name, frames, w, h, px in shapes:
            fh.write(pack_name(name, 12))
            fh.write(struct.pack("<III", frames, w, h))
            fh.write(px)
        for name, n, maxw, maxh, widths, ytop, rows, cells in fonts:
            fh.write(pack_name(name, 8))
            fh.write(struct.pack("<III", n, maxw, maxh))
            fh.write(widths)
            fh.write(ytop)
            fh.write(rows)
            fh.write(cells)

    # ---- report, and a machine readable manifest next to the pack -----------
    manifest = {
        "pack": os.path.basename(OUT),
        "magic": MAGIC.decode(),
        "version": VERSION,
        "bytes": os.path.getsize(OUT),
        "palette": PALETTE,
        "palette_widening": "v << 2 (video_ddraw.cpp:910); NOT (v<<2)|(v>>4)",
        "clock_translucent_table": {
            "control": "TLucentType {GREEN=3, LTGREY=14, Fading=180, Frac=0}",
            "source": "sidebar.cpp:1307 + jshell.cpp:318 + fading.cpp:14",
            "layout": "u8 ghost_lookup[256] (0xFF=opaque) then u8 fade[256]",
        },
        "known_blank_cameos": list(KNOWN_BLANK_CAMEOS),
        "shapes": {n: {"frames": f, "w": w, "h": h} for n, f, w, h, _ in shapes},
        "fonts": {n: {"nchars": c, "cell": [mw, mh]}
                  for n, c, mw, mh, _, _, _, _ in fonts},
    }
    with open(os.path.splitext(OUT)[0] + ".manifest.json", "w") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)

    total_frames = sum(f for _, f, _, _, _ in shapes)
    print("wrote %s: %d shapes / %d frames, %d fonts, %.1f KB"
          % (OUT, len(shapes), total_frames, len(fonts),
             os.path.getsize(OUT) / 1024.0))
    print("  cameos : %d (%s are documented 73-byte blanks)"
          % (len(CAMEOS), "/".join(KNOWN_BLANK_CAMEOS)))
    print("  chrome : %s" % " ".join("%s(%dx%d x%d)" % (n, w, h, f)
                                     for n, f, w, h, _ in shapes[len(CAMEOS):]))
    print("  fonts  : %s" % " ".join("%s(%d glyphs, %dx%d cell)" % (n, c, mw, mh)
                                     for n, c, mw, mh, _, _, _, _ in fonts))
    print("  clock translucent table: ghost_lookup[GREEN]=%d, fade[LTGREY]=%d"
          % (clocktab[GREEN], clocktab[256 + LTGREY]))


if __name__ == "__main__":
    main()
