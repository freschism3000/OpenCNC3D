#!/usr/bin/env python3
"""
bake_dosmenu.py -- everything the 1995 MS-DOS Command & Conquer MAIN MENU needs,
flattened into one little-endian pack, `dosmenu.pack`.

The pack format is byte-identical to sidebar/dossidebar.pack ("DOSBAR01"), so the
same loader reads both and dosmenu.c can lean on dosbar.c for every primitive:
the 8-bit surface, Draw_Box, the shape blitter and the 4bpp font printer.

Three differences in CONTENT, none in format:

  * The palette is the one embedded in TITLE.CPS, not TEMPERAT.PAL. The menu runs
    before any theater is loaded: Load_Title_Screen() sets the title picture's own
    palette, and the whole menu is drawn against it. The Westwood 16 colour GUI
    ramp still occupies indices 0..15, which is why the green dialog chrome comes
    out right.
  * "TITLE" is a shape with one 320x200 frame: the title screen itself. It is not
    a SHP, it is a CPS (LCW over a flat 64000 byte buffer), decoded here so the
    runtime never has to.
  * clocktab is present but zeroed. It is a sidebar construct (the build clock's
    translucency lookup) and the menu never uses it. Keeping the field means one
    loader, not two.

Art sources, all from the original 1995 DOS install:

  ../dosdata/LOCAL.MIX        TITLE.CPS      the title screen and its palette
  ../dosdata/cd1/CONQUER.MIX  OPTIONS.SHP    dialog filigree, frames 0 and 1
                              MOUSE.SHP      the mouse pointer, frame 0
                              6POINT.FNT     plain 6 point font
                              GRAD6FNT.FNT   the gradient font the menu prints with
                              CONQUER.ENG    the button labels, verbatim

Run:  python3 bake_dosmenu.py
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools"))

from mixshp import MixSet, Shape, ShapeBlock   # noqa: E402
from decode_fnt import Font                    # noqa: E402

DATA = os.path.normpath(os.path.join(HERE, "..", "dosdata"))
MIX_PATHS = ["LOCAL.MIX", "cd1/CONQUER.MIX"]

# the replacement title plate. When this file exists it is fitted to the original
# 320x200 plate and quantised to TITLE.CPS's own 256 colour palette, so the menu
# chrome (which addresses palette indices directly) keeps working unchanged.
TITLE_OVERRIDE = os.path.join(HERE, "art", "title.png")

# The bottom strip of the plate is painted out and the copyright line is printed at
# runtime instead, in the DOS 6 point font (dosmenu.c, dm_draw_copyright). Rows 193..199
# is where the 1995 plate put it and where the replacement art still has its remnant.
COPYRIGHT_STRIP_TOP = 186

# The wordmark block ("COMMAND & CONQUER 3D") in the replacement art. Making room for
# "3D" pushed the block up to row 127, and the menu panel's bottom edge is row 135, so
# nine rows of "COMMAND" end up behind the panel. WORDMARK_TOP re-fits the block into
# the space between it and the copyright strip: the block is scaled uniformly, keeping
# its aspect, and re-centred. None leaves the art exactly as delivered.
# 138 is where the 1995 plate starts its wordmark, three rows clear of the panel.
WORDMARK_INK_TOP = 127
WORDMARK_INK_BOTTOM = 193  # where the delivered art's wordmark block ends ("3D" included)
WORDMARK_TOP = 138

# Rows 127..132 of the delivered art carry the wordmark AND the last wisps of the two
# fireballs; from 133 down there is no flame at all (measured: 6 stray pixels at row
# 133, none below). In those rows the flame lives outside x 55..265 and the letters
# live inside, so the block takes a column window there and the full width below.
WORDMARK_FIRE_ROWS = 133
WORDMARK_X0 = 55
WORDMARK_X1 = 266

OUT = os.path.join(HERE, "dosmenu.pack")
MAGIC = b"DOSBAR01"
VERSION = 1

SHAPES = ["OPTIONS"]

# MOUSE.SHP is the OLDER Westwood shape container (getshape.cpp Extract_Shape),
# not the keyframe one, and its frames differ in size, so only the frame we need
# is baked: MOUSE_NORMAL, the plain arrow (mouse.h MouseType, index 0).
MOUSE_NORMAL = 0
FONTS = ["6POINT", "GRAD6FNT", "8POINT", "3POINT", "SCOREFNT"]

# conquer.h. The menu prints these by number; we resolve them here so the runtime
# carries no string table.
STRINGS = {
    "TXT_START_NEW_GAME": 25,
    "TXT_INTRO": 26,
    "TXT_LOAD_MISSION": 53,
    "TXT_EXIT_GAME": 64,
    "TXT_MULTIPLAYER_GAME": 210,
    "TXT_NEW_MISSIONS": 711,
}


def pack_name(s, n):
    b = s.encode("ascii")
    if len(b) > n:
        raise SystemExit("name %r does not fit in %d bytes" % (s, n))
    return b + b"\x00" * (n - len(b))


def widen(pal6):
    """video_sdl2.cpp:653 -- the engine widens a 6-bit DAC value with v << 2."""
    return bytes(min(252, v << 2) for v in pal6)


def load_cps(data):
    """CPS: u16 size-2, u16 compression (4 = LCW), u32 uncompressed, u16 palette."""
    from mixshp import lcw_uncompress
    _size, comp, uncomp, palsize = struct.unpack_from("<HHIH", data, 0)
    off = 10
    pal6 = None
    if palsize:
        pal6 = data[off:off + palsize]
        off += palsize
    if comp == 4:
        pix, written = lcw_uncompress(data, off, uncomp)
    elif comp == 0:
        pix, written = data[off:off + uncomp], uncomp
    else:
        raise SystemExit("CPS compression %d not supported" % comp)
    if written != uncomp:
        raise SystemExit("CPS decoded %d bytes, header says %d" % (written, uncomp))
    return bytes(pix), pal6


def fit_override(path, orig_px, pal6, align=True):
    """Fit a replacement title plate onto the original 320x200 CPS plate.

    Two problems have to be solved and they are separate:

    1. GEOMETRY. Art comes back from an image editor at whatever size and with a few
       stray rows top or bottom. If `align` is on we search crops and keep the one
       whose top 55% best matches the original plate, because that region is normally
       untouched. The residual is reported: a small number means the new art really is
       the old art plus an edit, a large one means it is a genuinely new picture and
       the alignment number carries no meaning.

    2. COLOUR. The result must land back in TITLE.CPS's own 256 colour palette,
       because everything else the menu draws (the green dialog, the button text)
       addresses palette INDICES, and those indices only mean what they should while
       that palette is loaded. So this is a nearest-colour quantise, not a re-palette.

    Returns (indices bytes, report dict).
    """
    from PIL import Image
    import numpy as np

    pal8 = np.array([[min(252, v << 2) for v in pal6[i * 3:i * 3 + 3]] for i in range(256)],
                    dtype=np.int32)
    orig_rgb = pal8[np.frombuffer(orig_px, dtype=np.uint8).reshape(200, 320).astype(np.int32)]

    img = Image.open(path).convert("RGB")
    W, H = img.size
    top, bottom, best = 0, 0, None
    band = 110  # the top 55%: title art above the wordmark

    if align:
        for t in range(0, 25):
            for b in range(0, 25):
                if H - t - b < H // 2:
                    continue
                cand = np.asarray(img.crop((0, t, W, H - b)).resize((320, 200), Image.BOX),
                                  dtype=np.int32)
                d = np.abs(orig_rgb[:band] - cand[:band]).mean()
                if best is None or d < best:
                    best, top, bottom = d, t, b

    plate = img.crop((0, top, W, H - bottom)).resize((320, 200), Image.BOX)

    # Refit the wordmark block, and blank the strip the runtime copyright prints into.
    #
    # The block cannot simply be cut out and moved: its top rows overlap the tails of
    # the two fireballs, which are part of the backdrop and must stay where they are.
    # The wordmark is neutral grey and the fire is strongly orange, so the ink is
    # separated by CHROMA, not by rectangle: a pixel belongs to the wordmark when its
    # channels are close together and it is not black. Everything else is left behind.
    if WORDMARK_TOP is not None:
        # The backdrop is rebuilt by KEEPING the fire rather than erasing the wordmark.
        # Erasing left a halo: the wordmark's sheen is a dim reddish grey that no
        # "is it neutral" test catches cleanly, and the leftovers showed as dashes and
        # smudges beside the letters. Asking the opposite question is robust, because
        # the only thing that belongs in this band is flame: strongly coloured, red
        # dominant, and bright. Everything else in the band goes black.
        source = np.asarray(plate, dtype=np.int32).copy()
        band = np.asarray(plate.crop((0, WORDMARK_INK_TOP, 320, 200)), dtype=np.int32)
        r, g, b = band[:, :, 0], band[:, :, 1], band[:, :, 2]
        fire = ((band.max(2) - band.min(2)) > 55) & (r > g) & (g >= b) & (band.sum(2) > 110)
        band[~fire] = 0
        plate.paste(Image.fromarray(band.astype(np.uint8)), (0, WORDMARK_INK_TOP))

        # The block to move is the wordmark as delivered. Only its top few rows share
        # space with the flame, and there the flame is outside the letters, so those
        # rows are taken through a column window instead of a colour test. Colour
        # cannot separate them: the letters are metal LIT by the fire, so their right
        # hand side is as warm as the flame it reflects, and every chroma threshold
        # that removed the flame also ate into "COMMAND".
        blk = np.asarray(source[WORDMARK_INK_TOP:WORDMARK_INK_BOTTOM], dtype=np.int32)
        ink = np.zeros(blk.shape[:2], dtype=np.uint8)
        fire_rows = WORDMARK_FIRE_ROWS - WORDMARK_INK_TOP
        ink[:fire_rows, WORDMARK_X0:WORDMARK_X1] = 255
        ink[fire_rows:, :] = 255

        scale = float(COPYRIGHT_STRIP_TOP - WORDMARK_TOP) / float(blk.shape[0])
        nw = max(1, int(round(320 * scale)))
        nh = max(1, int(round(blk.shape[0] * scale)))
        small_blk = Image.fromarray(blk.astype(np.uint8)).resize((nw, nh), Image.LANCZOS)
        small_ink = Image.fromarray(ink).resize((nw, nh), Image.LANCZOS).point(
            lambda v: 255 if v > 110 else 0)

        # Erase the wordmark from where it was, using the same chroma mask, so the
        # fireball tails it overlapped survive and no ghost of the old position is
        # left behind. Then drop the scaled copy in at its new home.
        plate.paste(small_blk, ((320 - nw) // 2, WORDMARK_TOP), small_ink)
    plate.paste((0, 0, 0), (0, COPYRIGHT_STRIP_TOP, 320, 200))

    small = np.asarray(plate, dtype=np.int32)

    flat = small.reshape(-1, 3)
    idx = np.empty(flat.shape[0], dtype=np.uint8)
    for i in range(0, flat.shape[0], 8192):
        chunk = flat[i:i + 8192]
        dist = ((chunk[:, None, :] - pal8[None, :, :]) ** 2).sum(2)
        idx[i:i + 8192] = dist.argmin(1).astype(np.uint8)

    quant = pal8[idx.astype(np.int32)].reshape(200, 320, 3)
    report = {
        "source": os.path.relpath(path, HERE),
        "source_size": [W, H],
        "crop_top": top,
        "crop_bottom": bottom,
        "align_residual_top55pct": None if best is None else round(float(best), 2),
        "quantise_error_mean": round(float(np.abs(small - quant).mean()), 2),
        "rows_changed_vs_original": int((np.abs(orig_rgb - quant).sum(2).mean(1) > 40).sum()),
        "wordmark_top": WORDMARK_TOP,
        "copyright_strip_blanked_from": COPYRIGHT_STRIP_TOP,
    }
    return bytes(idx.tobytes()), report


def bake_shape(mixes, stem):
    shp = Shape(mixes.read(stem + ".SHP"))
    buf = bytearray()
    for i in range(shp.frames):
        f = shp.frame(i)
        if len(f) != shp.width * shp.height:
            raise SystemExit("%s frame %d decoded %d bytes, expected %d"
                             % (stem, i, len(f), shp.width * shp.height))
        buf += f
    return shp.frames, shp.width, shp.height, bytes(buf)


def bake_mouse(mixes, index):
    block = ShapeBlock(mixes.read("MOUSE.SHP"))
    px, hdr = block.frame(index)
    if len(px) != hdr["width"] * hdr["height"]:
        raise SystemExit("MOUSE frame %d decoded %d bytes, expected %d"
                         % (index, len(px), hdr["width"] * hdr["height"]))
    return 1, hdr["width"], hdr["height"], bytes(px)


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


def read_strings(mixes):
    """CONQUER.ENG: u16 offset per string, offsets[0]/2 == the count."""
    d = mixes.read("CONQUER.ENG")
    count = struct.unpack_from("<H", d, 0)[0] // 2
    out = {}
    for name, idx in STRINGS.items():
        if idx >= count:
            raise SystemExit("%s is index %d but the table holds %d" % (name, idx, count))
        off = struct.unpack_from("<H", d, idx * 2)[0]
        out[name] = d[off:d.index(b"\0", off)].decode("latin-1")
    return out


def main():
    global WORDMARK_TOP, OUT
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--wordmark-top" and args:
            WORDMARK_TOP = int(args.pop(0))
        elif a == "--out" and args:
            OUT = args.pop(0)
        else:
            raise SystemExit("usage: bake_dosmenu.py [--wordmark-top ROW] [--out PACK]")

    mixes = MixSet(DATA, MIX_PATHS)
    if not mixes.mixes:
        raise SystemExit("no MIX archives found under %s" % DATA)

    title_px, pal6 = load_cps(mixes.read("TITLE.CPS"))
    if pal6 is None:
        raise SystemExit("TITLE.CPS carries no embedded palette")
    if max(pal6) > 63:
        raise SystemExit("TITLE.CPS palette is not 6-bit (max %d)" % max(pal6))

    override = None
    if os.path.exists(TITLE_OVERRIDE):
        title_px, override = fit_override(TITLE_OVERRIDE, title_px, pal6)
        print("title plate: %s (%dx%d), crop top %d bottom %d, "
              "align residual %.2f, quantise error %.2f, %d rows differ from the 1995 plate"
              % (override["source"], override["source_size"][0], override["source_size"][1],
                 override["crop_top"], override["crop_bottom"],
                 override["align_residual_top55pct"], override["quantise_error_mean"],
                 override["rows_changed_vs_original"]))

    shapes = [("TITLE", 1, 320, 200, title_px)]
    for stem in SHAPES:
        shapes.append((stem,) + bake_shape(mixes, stem))
    shapes.append(("MOUSE",) + bake_mouse(mixes, MOUSE_NORMAL))
    fonts = [(stem,) + bake_font(mixes, stem) for stem in FONTS]
    strings = read_strings(mixes)

    with open(OUT, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<IIII", VERSION, len(shapes), len(fonts), 256))
        fh.write(bytes(pal6))
        fh.write(widen(pal6))
        fh.write(b"\x00" * 512)              # clocktab: unused by the menu
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

    manifest = {
        "pack": os.path.basename(OUT),
        "magic": MAGIC.decode(),
        "version": VERSION,
        "bytes": os.path.getsize(OUT),
        "palette": "embedded in TITLE.CPS (LOCAL.MIX)",
        "palette_widening": "v << 2 (video_sdl2.cpp:653)",
        "clocktab": "zeroed; sidebar-only construct",
        "shapes": {n: {"frames": f, "w": w, "h": h} for n, f, w, h, _ in shapes},
        "fonts": {n: {"nchars": c, "cell": [mw, mh]}
                  for n, c, mw, mh, _, _, _, _ in fonts},
        "strings": strings,
        "title_override": override,
    }
    with open(os.path.splitext(OUT)[0] + ".manifest.json", "w") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)

    print("wrote %s: %d shapes, %d fonts, %.1f KB"
          % (OUT, len(shapes), len(fonts), os.path.getsize(OUT) / 1024.0))
    print("  shapes : %s" % " ".join("%s(%dx%d x%d)" % (n, w, h, f)
                                     for n, f, w, h, _ in shapes))
    print("  fonts  : %s" % " ".join("%s(%d glyphs, %dx%d cell)" % (n, c, mw, mh)
                                     for n, c, mw, mh, _, _, _, _ in fonts))
    for k in sorted(strings):
        print("  %-22s %r" % (k, strings[k]))


if __name__ == "__main__":
    main()
