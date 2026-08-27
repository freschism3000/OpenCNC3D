#!/usr/bin/env python3
"""The jukebox's track list, read off the shot's own pixels.

G48 used to claim that "Act On Instinct appearing in the shot's own pixels" was its
proof that the generated score table reached the screen, and then check no such thing:
it greped the COMPILED BINARY for the string literal and tested the PNG for being
non-empty. A jukebox that rendered an empty well, or one that drew row 0 nine times,
leaves that literal in the executable and writes a perfectly non-empty picture. This
file is what makes the claim true.

It is exact rather than approximate, the same way G23 is exact about the verdict
banner, and for the same reason: nothing in this path is resampled.

  * dosopt.c rasterises the dialog into a 320x200 8-bit surface (DOPT_SCREEN_W/H,
    dosopt.h:58-59) and dosopt_gl.h blits it as ONE GL_NEAREST quad at an INTEGER
    scale, letterboxed by opt_layout(): s = min(fbw/320, fbh/200), origin
    ((fbw - 320s)/2, (fbh - 200s)/2). So every DOS pixel is an s x s block of one
    exact colour, and the same arithmetic recovers it.
  * the row is drawn by dopt_draw_track (dosopt.c) with db_print + GRAD6FNT out of
    the DOS pack, so the expected ink is not a description of the text -- it is the
    same glyph cells the renderer walked, rendered here by db_print's own loop.

Five numbers, because each catches a different way of faking it:

    rows      rows of the list well carrying ink at all         (9 are drawn)
    named     of those, rows with ink in the NAME column        (the "No score tracks
              are installed." fallback puts one line at the LEFT margin and none here)
    distinct  how many of those name columns differ from each other (a table that
              resolves every row to the same index draws nine identical names)
    aoi       which row matches the glyphs of "Act On Instinct" EXACTLY, -1 for none
    miss      pixels of that row's name that are not the expected glyph, or the best
              (smallest) miss count over all rows when nothing matched, so a drift is
              diagnosable instead of just absent

    python3 gate_jukebox.py SHOT.png DOSPACK
        -> JUKEBOX|rows=9|named=9|distinct=9|aoi=8|miss=0
"""
import struct
import sys
from collections import Counter

from PIL import Image

WANT = "Act On Instinct"          # theme.cpp's first score track, TXT_THEME_AOI

# dosopt.h:58-59, :193-208. The DOS plate and the Sound Controls dialog inside it.
SCREEN_W, SCREEN_H = 320, 200
SND_W, SND_H = 292, 146
SND_X = (SCREEN_W - SND_W) // 2
SND_Y = (SCREEN_H - SND_H) // 2
LIST_X = SND_X + 1
LIST_Y = SND_Y + 54
LIST_W, LIST_H = 290, 73
ROW_H = 8
ROWS = LIST_H // ROW_H
TAB3 = 90                          # sounddlg.cpp's third tab: the track's name
XSPACING = -2                      # DB_FONT6_XSPACING, dosbar.h:113
INK_MIN = 20                       # a drawn name is 65 px at its thinnest ("RECON")


def load_font(path, name):
    """The pack's font table, db_pack_load at dosbar.c:47-150. DOSBAR01, then the three
    palettes, then the shapes, then the fonts -- all fixed-size records, no index, so the
    only way to the fonts is to walk the shapes."""
    b = open(path, "rb").read()
    if b[:8] != b"DOSBAR01":
        raise SystemExit("gate_jukebox: %s is not a DOSBAR01 pack" % path)
    nshapes, nfonts = struct.unpack_from("<II", b, 12)
    o = 24 + 768 + 768 + 512       # pal6, pal8, clocktab
    for _ in range(nshapes):
        frames, w, h = struct.unpack_from("<III", b, o + 12)
        o += 24 + frames * w * h
    for _ in range(nfonts):
        fname = b[o:o + 8].split(b"\0")[0].decode()
        nchars, maxw, maxh = struct.unpack_from("<III", b, o + 8)
        o += 20
        width = b[o:o + nchars]
        o += nchars * 3            # width, ytop, rows
        cells = b[o:o + nchars * maxw * maxh]
        o += nchars * maxw * maxh
        if fname == name:
            return nchars, maxw, maxh, width, cells
    raise SystemExit("gate_jukebox: no font %s in %s" % (name, path))


def render(font, text):
    """db_print, dosbar.c:393-413. db_font_palette_grad (dosbar.c:370) flattens the
    gradient ramp so classes 1 and 4..15 are ink and 0/2/3 are the transparent back;
    Buffer_Print skips a zero colour, which is why the shadow and outline classes never
    draw. The advance is width[c] + xspacing, and xspacing is NEGATIVE at 6 point."""
    nchars, maxw, maxh, width, cells = font
    ink, x = set(), 0
    for ch in text:
        c = ord(ch)
        if c >= nchars:
            continue
        base = c * maxw * maxh
        for yy in range(maxh):
            for xx in range(min(width[c], maxw)):
                v = cells[base + yy * maxw + xx]
                if v == 1 or v >= 4:
                    ink.add((x + xx, yy))
        x += width[c] + XSPACING
    return ink, x, maxh


def main():
    shot, pack = sys.argv[1], sys.argv[2]
    font = load_font(pack, "GRAD6FNT")
    tmpl, advance, glyph_h = render(font, WANT)

    im = Image.open(shot).convert("RGB")
    W, H = im.size
    # opt_layout(), dosopt_gl.h. Same arithmetic, so the plate is found rather than
    # assumed: a window that is not an exact multiple of 320x200 still resolves.
    s = min(W // SCREEN_W, H // SCREEN_H)
    if s < 1:
        raise SystemExit("gate_jukebox: %dx%d is too small to hold the DOS plate" % (W, H))
    ox = (W - SCREEN_W * s) // 2
    oy = (H - SCREEN_H * s) // 2
    px = im.load()

    def dos(dx, dy):
        """One DOS pixel, sampled at the centre of its s x s block."""
        return px[ox + dx * s + s // 2, oy + dy * s + s // 2]

    rows = named = 0
    names, aoi, best = [], -1, None
    for r in range(ROWS):
        ry = LIST_Y + 1 + r * ROW_H
        band = [dos(dx, dy) for dy in range(ry, ry + ROW_H)
                for dx in range(LIST_X + 1, LIST_X + LIST_W - 1)]
        # The background is whatever most of the row is: black for an ordinary row,
        # CC_GREEN_BKGD for the selected one, and the caller must not have to know
        # which row the engine chose to highlight.
        bg = Counter(band).most_common(1)[0][0]
        name = [(dx, dy) for dy in range(ry, ry + ROW_H)
                for dx in range(LIST_X + TAB3, LIST_X + LIST_W - 1)
                if dos(dx, dy) != bg]
        if any(p != bg for p in band):
            rows += 1
        if len(name) < INK_MIN:
            continue
        named += 1
        names.append(frozenset((dx - LIST_X - TAB3, dy - ry) for dx, dy in name))
        fore = Counter(dos(dx, dy) for dx, dy in name).most_common(1)[0][0]
        miss = 0
        for dy in range(glyph_h):
            for dx in range(advance):
                got = dos(LIST_X + TAB3 + dx, ry + dy)
                if got != (fore if (dx, dy) in tmpl else bg):
                    miss += 1
        if best is None or miss < best:
            best = miss
        if miss == 0:
            aoi = r

    print("JUKEBOX|rows=%d|named=%d|distinct=%d|aoi=%d|miss=%d"
          % (rows, named, len(set(names)), aoi, best if best is not None else -1))


main()
