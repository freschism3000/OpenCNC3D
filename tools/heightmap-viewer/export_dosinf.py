#!/usr/bin/env python3
"""CNC3D -- MS-DOS infantry billboards for the heightmap viewer.

The viewer draws its little men as billboards baked from the N64 cartridge pack.
The game itself stopped doing that a year ago: it draws the 1995 MS-DOS sprites
through game/dosinf_mod.h out of game/bake_dosinfantry.py's pack. This exporter
gives the viewer the same art, in the form a browser can eat:

    public/data/dosinf.png    one RGBA sheet, 20 types x 8 facings x 2 houses
    public/data/dosinf.json   the rects, plus the engine's own facing tables

Nothing here is a second opinion about the DOS art. Every rule below is IMPORTED
from game/bake_dosinfantry.py (which guards its own work behind main(), so the
import is side-effect free) or transcribed from a cited line of the C++ / GPL
source. Where this file and the shipped bake could ever disagree, they cannot:
they run the same tables through the same helpers.

WHAT COMES FROM WHERE

  Art        data/dosdata/CONQUER.MIX, the twenty infantry .SHP, 50x39, flags=0.
             Decoded by menu/tools/mixshp.py (the proven port of common/lcw.cpp,
             common/xordelta.cpp, common/keyframe.cpp).
  Palette    data/dosdata/LOCAL.MIX -> TEMPERAT.PAL, 768 bytes of 6-bit DAC.
             Widened v << 2 (video_ddraw.cpp:910); the clamp to 252 is a no-op for
#   6-bit input (63 << 2 == 252) and is carried over from bake_dosinfantry.py:387, which is what
             bake_dosinfantry.py:387 does. NOT mixshp.load_pal's replicate-the-
             top-two-bits widening: that is a different, brighter ramp, and the
             pack the game already ships was baked the v<<2 way. Two widenings of
             the same palette in one project is exactly the kind of drift that
             ends with "the browser's Nod looks slightly off". bake's palette
             assert (index 176 must be (61,53,30)) is carried across verbatim, so
             sourcing the repacked content/TEMPERAT.MIX by mistake is a hard
             failure rather than quietly wrong colours.
  Frames     infantry.cpp:574 Draw_It:
                 shapenum = Frame + HumanShape[Facing_To_32(dir)] * Jump
                          + Fetch_Stage() % Count
             so a row is FACING-MAJOR. The viewer never animates, so only the
             STAND row (Frame 0, Count 1, Jump 1) is baked -- but all 8 facings
             of it, via bake_dosinfantry.frames_of(), the same function the game
             pack uses.
  Facings    infantry.cpp:84 HumanShape[32]: facenum 0..7 is COUNTER-CLOCKWISE
             from north -- N, NW, W, SW, S, SE, E, NE. All 8 are stored in the
             DOS art; there is no mirroring to undo.
  Houses     Row 0 is the identity (GDI/gold, and neutral). Row 1 rewrites
             palette indices 176..191 -- the gold uniform band -- to
             bake_dosinfantry.N64_BAND_NOD, the cartridge's desaturated blue-grey
             (ROM 0x99130 entries 16..31). Deliberately NOT const.cpp:404's
             RemapLtBlue teal: the teal was looked at and rejected. See the
             long note at bake_dosinfantry.py:303-328 for why the two bands are
             the same sixteen slots.
  Shadow     display.cpp:357 UShadowCols: palette index 4 is a ghost colour the
             DOS engine darkens the ground with, not a body colour. Index 0 is
             transparent. Both go to alpha 0 here, same as the game's renderer.
  Crop       bake_dosinfantry.py:55-66. Draw_It centres the 50x39 frame on the
             object, so frame column 27 (ANCHOR_COL) is the ground x. The crop is
             the union body bbox over the row's frames, made SYMMETRIC about
             column 27 so the billboard's centre line is the man's ground point,
             and the crop's bottom row is the feet line. STAND, WALK and FIRE
             share one bottom row so the feet cannot jump a pixel between poses;
             that unification is reproduced here even though only STAND is
             emitted, because a viewer sprite that sat 1px off the game's would
             be a bug nobody could see the cause of.
  Scale      24 texels per world unit (spriteTexelsPerUnit, as in export_pack.py:
             275 and the renderer's sprite_texels_per_unit() under CAM_N64). DOS
             art is drawn at 24 px/cell, so baking 1:1 is native world scale.

BLEED AND PADDING. Every sprite is bled with export.bleed_rgba(passes=4) inside
its own 2px transparent margin BEFORE it is pasted, so a bilinear tap one texel
outside the figure finds the figure's own colour rather than black or a
neighbour's. Bleeding per-tile rather than per-sheet is the point: bleeding the
assembled sheet would smear each man into the one packed next to him.

CROSS-CHECKS, all before a single byte is written:
  * bake_dosinfantry.verify_against_idata() re-parses idata.cpp and compares
    every DO row of every type. Its skip path prints loudly; this script turns
    that skip into a failure, because the viewer has no other check.
  * the palette provenance assert above.
  * after writing: the PNG is reopened and every rect in the JSON is proved
    in-bounds, non-empty and not fully transparent, and the two house rows are
    proved to actually DIFFER for the combat types (a remap that changed nothing
    is the failure mode this catches).

Usage:  python3 tools/heightmap-viewer/export_dosinf.py
Read-only on everything except its own two outputs.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "public", "data")

# The SHP/MIX decoder and the shipped bake, in that order: bake_dosinfantry does
# its own mixshp lookup, but importing mixshp first keeps this file's own
# dependency explicit rather than riding on someone else's sys.path edit.
sys.path.insert(0, os.path.join(REPO, "menu", "tools"))
sys.path.insert(0, os.path.join(REPO, "game"))
sys.path.insert(0, HERE)

from mixshp import MixFile, Shape                      # noqa: E402
import bake_dosinfantry as BAKE                        # noqa: E402
from export import bleed_rgba                          # noqa: E402

# ---------------------------------------------------------------------------
# The engine's facing tables, transcribed from game/dosinf_mod.h so the viewer
# can pick a facing the way the game does instead of inventing an angle->sprite
# rule of its own (which is how the two would drift apart).
#
#   dosinf_mod.h:85-94  DOSINF_Facing32[256]  -- const.cpp:211 Facing32[], a
#       DirType 0..255 mapped onto the 32-facing wheel, INCLUDING Westwood's
#       3D-Studio 45-degree distortion compensation (which is why the run lengths
#       are uneven; it is not a rounding table).
#   dosinf_mod.h:98-99  DOSINF_HumanShape[32] -- wheel position to one of the 8
#       stored sprite facings, counter-clockwise from north.
#   dosinf_mod.h:103    facenum = HumanShape[Facing32[dir & 255]]
#
# NOTE ON THE FIELD NAME: the requirement was "facing32" as 32 ints. The real
# table is 256 entries long -- it is indexed by the 8-bit DirType, and its OUTPUT
# is 0..31. Emitting a 32-entry slice would be unusable, so the full 256 go out
# and the JSON carries a "facingNote" saying so.
DOSINF_FACING32 = [
    0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,
    7,  7,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,
    11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21,
    22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 26,
    26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29,
    30, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,  0,  0,  0,  0,  0]
DOSINF_HUMANSHAPE = [0, 0, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 5, 4,
                     4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0]

FACING_ORDER = ("counter-clockwise from north: N,NW,W,SW,S,SE,E,NE; "
                "8 stored, no mirroring")

# The twenty InfantryTypes, in the order the brief lists them.
TYPE_ORDER = (["E1", "E2", "E3", "E4", "E5", "E6", "RMBO"]
              + ["C%d" % i for i in range(1, 11)]
              + ["CHAN", "DELPHI", "MOEBIUS"])

# The types that belong to no house. The game bakes ONE strip for each of these and
# points both house rows at it (bake_dosinfantry.py:557 and :562 pass a single house
# row, :538-539 duplicates it), so the Nod band must never touch them -- C3 wears
# palette 176..191 and a Nod-owned C3 stays gold in the real game.
NEUTRAL = frozenset(BAKE.CIVILIANS) | {ini for ini, _ in BAKE.NAMED}

STAND = BAKE.SLOT_NAMES.index("STAND")
WALK = BAKE.SLOT_NAMES.index("WALK")
FIRE = BAKE.SLOT_NAMES.index("FIRE")

PAD = 2          # transparent margin per sprite; also the gap between neighbours


def do_tables():
    """ini name -> the type's 15 DO rows, straight out of the shipped bake.

    Nothing is re-typed: COMBAT is a dict of the seven soldiers, CIVILIAN_DO is
    shared by C1..C10 (all ten are byte-identical in idata.cpp) and DELPHI, and
    NAMED carries MOEBIUS / CHAN / DELPHI with their own shorter SHPs."""
    t = dict(BAKE.COMBAT)
    for ini in BAKE.CIVILIANS:
        t[ini] = BAKE.CIVILIAN_DO
    for ini, rows in BAKE.NAMED:
        t[ini] = rows
    missing = [i for i in TYPE_ORDER if i not in t]
    assert not missing, "no DO table for %s" % missing
    return t


def stand_crop(shp, rows):
    """The STAND crop box, computed exactly as bake_dosinfantry.py:469-491 does.

    The unification with WALK (and FIRE where the type has one) is not optional
    decoration: it is what makes the browser billboard's feet line the same pixel
    row as the game's, and it is only visible as a bug when the two are compared
    side by side, which nobody will do on purpose."""
    boxes = {}
    live = [STAND, WALK] + ([FIRE] if rows[FIRE] is not None else [])
    for slot in live:
        fr, cnt, jmp = rows[slot]
        bx0 = by0 = 1 << 30
        bx1 = by1 = -1
        for f in BAKE.frames_of(shp, fr, cnt, jmp):
            x0, y0, x1, y1 = BAKE.body_bbox(shp.frame(f), shp.width, shp.height)
            bx0, by0 = min(bx0, x0), min(by0, y0)
            bx1, by1 = max(bx1, x1), max(by1, y1)
        # symmetric about the column Draw_It lands on the object's ground x
        half = max(BAKE.ANCHOR_COL - bx0, bx1 - BAKE.ANCHOR_COL)
        boxes[slot] = [BAKE.ANCHOR_COL - half, by0, BAKE.ANCHOR_COL + half, by1]
    feet = max(boxes[s][3] for s in live)
    box = boxes[STAND]
    box[3] = feet
    return box


def sprite_rgba(shp, frame, box, pal8, nod):
    """One cropped facing as raw RGBA bytes.

    Indices 0 (transparent) and 4 (the DOS shadow ghost, display.cpp:357) both
    become alpha 0. The Nod row substitutes RGB directly for indices 176..191
    rather than remapping to another palette index the way the game pack has to:
    the pack carries ONE 256-entry palette and must park the cartridge band in
    spare slots, but a PNG has no such constraint, so the band lands as the exact
    ROM colours with no slot-collision hazard at all."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0 + 1, y1 - y0 + 1
    buf = shp.frame(frame)
    sw, sh = shp.width, shp.height
    out = bytearray(w * h * 4)
    for yy in range(h):
        sy = y0 + yy
        for xx in range(w):
            sx = x0 + xx
            v = buf[sy * sw + sx] if (0 <= sx < sw and 0 <= sy < sh) else 0
            if v == BAKE.TRANSPARENT or v == BAKE.SHADOW:
                continue                      # already 0,0,0,0
            if nod and 176 <= v <= 191:
                r, g, b = BAKE.N64_BAND_NOD[v - 176]
            else:
                r, g, b = pal8[v * 3], pal8[v * 3 + 1], pal8[v * 3 + 2]
            o = (yy * w + xx) * 4
            out[o] = r
            out[o + 1] = g
            out[o + 2] = b
            out[o + 3] = 255
    return bytes(out), w, h


def shelf_pack(sizes, width):
    """Dumb shelf packer, tallest first. Returns {key: (x, y)} and the height.

    The sprites are all within a few pixels of each other in size, so shelves
    waste almost nothing here and a real packer would be ceremony."""
    order = sorted(sizes, key=lambda k: (-sizes[k][1], -sizes[k][0], k))
    pos, x, y, shelf = {}, 0, 0, 0
    for k in order:
        w, h = sizes[k]
        assert w <= width, ("sprite %s is %dpx wide, sheet is %d" % (k, w, width))
        if x + w > width:
            x, y, shelf = 0, y + shelf, 0
        pos[k] = (x, y)
        x += w
        shelf = max(shelf, h)
    return pos, y + shelf


def main():
    from PIL import Image

    cq = MixFile(os.path.join(REPO, "data", "dosdata", "CONQUER.MIX"))
    pal6 = MixFile(os.path.join(REPO, "data", "dosdata", "LOCAL.MIX")).read("TEMPERAT.PAL")
    # bake_dosinfantry.py:383-386, carried across unchanged. The second assert is
    # the one that matters: content/TEMPERAT.MIX is a REPACKED mix whose
    # TEMPERAT.PAL is not the 1995 palette, and sourcing it there is wrong
    # colours with no other symptom.
    assert len(pal6) == 768 and max(pal6) <= 63
    assert tuple(pal6[176 * 3:176 * 3 + 3]) == (61, 53, 30), "wrong TEMPERAT.PAL source"
    pal8 = bytes(min(252, v << 2) for v in pal6)       # video_ddraw.cpp:910

    tables = do_tables()

    # Prove the transcriptions against the GPL source BEFORE any pixel is read.
    # bake_dosinfantry's own version prints a warning and returns 0 when
    # brain/vanilla is absent; here that is fatal, because unlike the game build
    # the viewer has no second chance to notice.
    checked = BAKE.verify_against_idata([(i, tables[i]) for i in TYPE_ORDER])
    if not checked:
        raise SystemExit("export_dosinf: idata.cpp cross-check did not run "
                         "(brain/vanilla missing) -- refusing to bake unverified tables")

    # ---- decode every sprite -------------------------------------------------
    tiles = {}          # (ini, house, facenum) -> (rgba bytes, w, h)
    crops = {}          # ini -> [x0, y0, x1, y1]
    identical = []      # types whose Nod row is pixel-identical to GDI
    for ini in TYPE_ORDER:
        shp = Shape(cq.read(ini + ".SHP"))
        rows = tables[ini]
        box = stand_crop(shp, rows)
        crops[ini] = box
        fr, cnt, jmp = rows[STAND]
        frames = BAKE.frames_of(shp, fr, cnt, jmp)     # facing-major, 8 entries
        assert len(frames) == BAKE.NFACINGS, (ini, frames)
        neutral = ini in NEUTRAL
        for house in ("gdi", "nod"):
            # The Nod band is applied to the UNIFORMED types only. Civilians and
            # the named characters are neutral-house and the game has no Nod strip
            # for them at all: bake_dosinfantry.py:555 passes two house rows for
            # COMBAT and :557/:562 pass one for CIVILIANS and NAMED, and :538-539
            # then points the second row at the first. The shipped manifest agrees
            # -- game/dosinfantry.manifest.json carries #1 strips for exactly
            # E1 E2 E3 E4 E5 E6 RMBO. C3 in particular DOES wear palette 176..191,
            # so remapping him would recolour a man the game leaves gold.
            if neutral and house == "nod":
                for f in range(BAKE.NFACINGS):
                    tiles[(ini, "nod", f)] = tiles[(ini, "gdi", f)]
                continue
            for f, frame in enumerate(frames):
                tiles[(ini, house, f)] = sprite_rgba(shp, frame, box, pal8,
                                                     house == "nod")
        if neutral:
            identical.append(ini)

    # ---- pack ----------------------------------------------------------------
    # Each sprite is padded by PAD on every side and the PADDED tile is packed, so
    # neighbours end up 2*PAD apart and the bleed below has somewhere to go.
    packed = {k: v for k, v in tiles.items()
              if not (k[1] == "nod" and k[0] in NEUTRAL)}
    sizes = {k: (v[1] + 2 * PAD, v[2] + 2 * PAD) for k, v in packed.items()}
    best = None
    for width in (128, 256, 512, 1024, 2048):
        pos, h = shelf_pack(sizes, width) if max(w for w, _ in sizes.values()) <= width \
            else (None, None)
        if pos is None:
            continue
        h = 1 << (h - 1).bit_length()          # power of two, kinder to GL
        # Smallest area wins; on a tie take the SQUARER sheet. 128x1024 and
        # 256x512 hold the same texels, but a 1024-tall strip is closer to the
        # max texture size of the oldest hardware anyone might open this on.
        key = (width * h, max(width, h))
        if best is None or key < best[0]:
            best = (key, width, h, pos)
    _key, sheetW, sheetH, pos = best
    sheet = Image.new("RGBA", (sheetW, sheetH), (0, 0, 0, 0))
    rects = {}
    for k, (px, w, h) in packed.items():
        tile = Image.new("RGBA", (w + 2 * PAD, h + 2 * PAD), (0, 0, 0, 0))
        tile.paste(Image.frombytes("RGBA", (w, h), px), (PAD, PAD))
        # Bleed the tile, not the sheet: bleed_rgba pushes colour into alpha-0
        # texels without touching alpha, and doing it here keeps each man's colour
        # inside his own margin instead of smearing into whoever is packed beside
        # him. export.py:257 -- same four passes the terrain atlas uses.
        tile = bleed_rgba(tile, passes=4)
        x, y = pos[k]
        sheet.paste(tile, (x, y))
        rects[k] = {"x": x + PAD, "y": y + PAD, "w": w, "h": h}
    for ini in NEUTRAL:
        for f in range(BAKE.NFACINGS):
            rects[(ini, "nod", f)] = rects[(ini, "gdi", f)]

    os.makedirs(OUT, exist_ok=True)
    png = os.path.join(OUT, "dosinf.png")
    # written to a temporary name and moved into place only once verify() passes,
    # so a failed run cannot leave a bad sheet on disk for the next reader
    sheet.save(png + ".tmp", format="PNG")

    meta = {
        "source": "CONQUER.MIX",
        "palette": "TEMPERAT.PAL from LOCAL.MIX",
        "sheet": "dosinf.png",
        "sheetW": sheetW, "sheetH": sheetH,
        "spriteTexelsPerUnit": 24.0,
        "facingOrder": FACING_ORDER,
        "anchor": "rect centre column is the man's ground x (frame column 27, "
                  "Draw_It); rect bottom row is the feet line, unified with WALK "
                  "and FIRE per type",
        "shadow": "palette index 0 and index 4 (the DOS shadow ghost) are alpha 0",
        "pad": PAD,
        "houseNote": "row 'gdi' is the identity palette (also correct for neutral "
                     "civilians); row 'nod' rewrites indices 176..191 to the "
                     "cartridge's ROM 0x99130 band, NOT the 1995 RemapLtBlue teal",
        "houseIdentical": identical,
        "types": {ini: {h: [rects[(ini, h, f)] for f in range(BAKE.NFACINGS)]
                        for h in ("gdi", "nod")} for ini in TYPE_ORDER},
        "facingNote": "facenum = humanShape[facing32[dir & 255]]; facing32 is the "
                      "full 256-entry DirType table from dosinf_mod.h:85, not a "
                      "32-entry slice -- its OUTPUT is 0..31, its INDEX is 0..255",
        "facing32": DOSINF_FACING32,
        "humanShape": DOSINF_HUMANSHAPE,
    }
    js = os.path.join(OUT, "dosinf.json")
    with open(js + ".tmp", "w") as f:
        json.dump(meta, f, separators=(",", ":"), sort_keys=True)

    verify(png + ".tmp", js + ".tmp")
    os.replace(png + ".tmp", png)
    os.replace(js + ".tmp", js)

    heights = {ini: crops[ini][3] - crops[ini][1] + 1 for ini in TYPE_ORDER}
    widths = {ini: crops[ini][2] - crops[ini][0] + 1 for ini in TYPE_ORDER}
    print("wrote %s  %dx%d" % (png, sheetW, sheetH))
    print("wrote %s  %.1f KB" % (js, os.path.getsize(js) / 1024.0))
    print("%d types, %d frames (%d facings x 2 house rows), crops %dx%d..%dx%d, "
          "E1 stand %dx%d"
          % (len(TYPE_ORDER), len(tiles), BAKE.NFACINGS,
             min(widths.values()), min(heights.values()),
             max(widths.values()), max(heights.values()),
             widths["E1"], heights["E1"]))
    if identical:
        print("neutral (Nod band rewrites nothing, as in the game pack): %s"
              % " ".join(identical))


def verify(png, js):
    """Re-open what was just written and prove it, rather than trusting the writer.

    Three failure modes this catches, all of which have shipped in this project
    before: a rect that points off the sheet, a rect that is entirely transparent
    (a crop that missed the figure), and a house remap that silently did nothing."""
    from PIL import Image
    im = Image.open(png).convert("RGBA")
    meta = json.load(open(js))
    assert im.size == (meta["sheetW"], meta["sheetH"]), (im.size, meta["sheetW"])
    n = 0
    for ini, houses in meta["types"].items():
        for h, arr in houses.items():
            assert len(arr) == 8, (ini, h, len(arr))
            for f, r in enumerate(arr):
                assert r["w"] > 0 and r["h"] > 0, (ini, h, f, r)
                assert 0 <= r["x"] and r["x"] + r["w"] <= im.width, (ini, h, f, r)
                assert 0 <= r["y"] and r["y"] + r["h"] <= im.height, (ini, h, f, r)
                crop = im.crop((r["x"], r["y"], r["x"] + r["w"], r["y"] + r["h"]))
                assert crop.getchannel("A").getextrema()[1] > 0, \
                    "%s %s facing %d is fully transparent" % (ini, h, f)
                n += 1
    # A Nod row identical to GDI for a UNIFORMED type means the band did nothing.
    for ini in ("E1", "E2", "E3", "E4", "E5", "E6", "RMBO"):
        same = 0
        for f in range(8):
            g, d = meta["types"][ini]["gdi"][f], meta["types"][ini]["nod"][f]
            gp = im.crop((g["x"], g["y"], g["x"] + g["w"], g["y"] + g["h"])).tobytes()
            dp = im.crop((d["x"], d["y"], d["x"] + d["w"], d["y"] + d["h"])).tobytes()
            same += (gp == dp)
        assert same == 0, "%s: %d/8 Nod facings equal GDI -- the band did nothing" % (ini, same)
    # ...and a Nod row that DIFFERS for a neutral type means the band leaked onto
    # a man the game leaves alone. Both directions, or the next bug walks through
    # the half that is missing.
    for ini in sorted(NEUTRAL):
        for f in range(8):
            g, d = meta["types"][ini]["gdi"][f], meta["types"][ini]["nod"][f]
            gp = im.crop((g["x"], g["y"], g["x"] + g["w"], g["y"] + g["h"])).tobytes()
            dp = im.crop((d["x"], d["y"], d["x"] + d["w"], d["y"] + d["h"])).tobytes()
            assert gp == dp, ("%s: neutral type differs between house rows -- the "
                              "game has no Nod strip for it" % ini)
    print("verify: %d rects in-bounds, non-empty and non-blank; the 7 uniformed "
          "types differ between house rows and the %d neutral types do not"
          % (n, len(NEUTRAL)))


if __name__ == "__main__":
    main()
