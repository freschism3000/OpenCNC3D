"""The UNIT CARD's chrome, cut from the shipped HUD art. Nothing here is drawn by hand.

The card is the selection readout in the bottom-left corner of the Enhanced HUD: the
selected unit's cameo, its name, health and damage, a row of smaller cameos for the rest
of the selection, and the ten control-group tabs under it. Every piece of chrome it uses
is cut from a chunk the 640x480 HUD already ships, so the card is the same object as the
sidebar wearing a different shape:

  card_body   the radar BEZEL out of chassis.png (rows 18..177, the framed dark window
              with its four screws and the vent slots), widened from 160 to 240 by
              tiling a mirrored sample of its middle, and shortened by cutting the
              window down from 120 rows to 84. The dark window is where everything is
              printed; the bezel is what makes it read as part of this panel.
  card_tab    one control-group tab, 24x26: the tab plate's own left end, a slice of
              its ribbed field and its own right end, so each tab is a framed button
              and ten in a row meet the way the top bar's tabs do; made taller by
              repeating the plate's middle rows, so the key digit and the unit count
              get a row each; relit through states.py the way every other control on
              the panel is.
              Four frames: 0 EMPTY (dimmed, the group holds nothing), 1 normal,
              2 hover, 3 ACTIVE (the panel's own amber, the group IS the selection).
  card_mini   the well a smaller cameo sits in: cell_well at half size. Two frames,
              normal and hover.
  cell_frame  the ring drawn back over a filled build slot, on the sidebar and on
              the card: the chassis's own pixels around the first slot with the
              cameo's octagon punched out, so the bevel turns the corners.
  card_frame  the same ring with the plate outside its corner diagonals cleared,
              for the card's black window.
  card_seg    one health segment, lit and unlit: the power meter's sunken segment
              turned over so it stands proud instead, lit top and left, with a
              dimmed copy of its lip along the underside.
  font_big    GRAD6FNT at the tab strip's own 1.75x scale (tabs.py), ASCII 32..95 in
              fixed 15x15 cells. digits_strip is this font's ten digits already; this
              is the rest of it.
  font_mid    the same font at 1.25x in 11x11 cells: the unit's name, which at the big
              size loses its tail on "Mobile Construction Yard" and at 1x is too small
              to read as a title.
  font_small  GRAD6FNT at 1x, same range, 9x9 cells, for numbers and labels.

Run after tabs.py and states.py, before bake_pack.py:
    python3 tools/sidebar_redesign/unitcard_art.py
"""
from PIL import Image
import numpy as np, os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosfont, states, tabs

HERE = os.path.dirname(os.path.abspath(__file__))
CH = os.path.join(HERE, "chunks")

CHASSIS = Image.open(f"{CH}/chassis.png").convert("RGBA")
TAB_PLATE = Image.open(f"{CH}/tab_plate.png").convert("RGBA")
CELL_WELL = Image.open(f"{CH}/cell_well.png").convert("RGBA")

# ---- the body -------------------------------------------------------------------
# Measured off chassis.png at x=80: row 17 is the seam above the bezel (luma 0), row 19
# its bright lip, rows 28..30 the window's dark inner bevel, row 31 the first window row
# (luma 4-5) and row 150 the last; rows 151..154 climb back out to the bright bottom
# lip and the vent slots run 157..171 with the seam under the plate at 177.
BEZEL_Y0, WIN_Y0, WIN_Y1, BEZEL_Y1 = 18, 31, 151, 178   # rows [Y0, Y1)
WIN_X0, WIN_X1 = 11, 147                                # the 136-wide window
KEEP = 50                # window rows kept at the top and at the bottom: 100 in all
BODY_W = 240
INSERT = BODY_W - CHASSIS.width                         # 80 columns tiled in
SPLIT_X = 80
SAMPLE = (60, 100)       # the columns tiled to fill: no screw, no slot end


def widen(strip):
    """A 160-wide bezel strip made 240 wide: left half, a mirrored tile of its middle,
    right half. The window interior, the top and bottom lips and the vent slots are all
    horizontally uniform between the screws, so the join is invisible."""
    out = Image.new("RGBA", (BODY_W, strip.height), (0, 0, 0, 0))
    out.paste(strip.crop((0, 0, SPLIT_X, strip.height)), (0, 0))
    sample = strip.crop((SAMPLE[0], 0, SAMPLE[1], strip.height))
    flip = sample.transpose(Image.FLIP_LEFT_RIGHT)
    x, i = SPLIT_X, 0
    while x < SPLIT_X + INSERT:
        t = flip if i % 2 else sample
        w = min(t.width, SPLIT_X + INSERT - x)
        out.paste(t.crop((0, 0, w, t.height)), (x, 0))
        x += w
        i += 1
    out.paste(strip.crop((SPLIT_X, 0, strip.width, strip.height)), (SPLIT_X + INSERT, 0))
    return out


def body():
    top = CHASSIS.crop((0, BEZEL_Y0, 160, WIN_Y0 + KEEP))
    bot = CHASSIS.crop((0, WIN_Y1 - KEEP, 160, BEZEL_Y1))
    im = Image.new("RGBA", (BODY_W, top.height + bot.height), (0, 0, 0, 0))
    im.paste(widen(top), (0, 0))
    im.paste(widen(bot), (0, top.height))
    # The bezel is OPAQUE everywhere the plate is: the chassis rows carry their own
    # alpha and the seam row under the plate may be transparent, which is right.
    return cut_corners(im)


# The radar bezel has chamfered corners: a 45 degree lip at the top, a 45 degree seam
# at the bottom. In the sidebar the plate continues outside those diagonals, so a
# rectangular cut of the bezel carries a triangle of plate in each corner, and on the
# card, which stands alone over the map, those triangles read as four square ears on
# a frame that is plainly meant to be chamfered. This clears them.
#
# Each number is the Manhattan distance from its corner to the diagonal, measured off
# the luma of the cut bezel: the pixel ON the diagonal is kept (it is the lip or the
# seam, and it becomes the edge), everything nearer the corner goes transparent. The
# right side is deeper than the left because the chassis carries a narrow strip
# beyond the right lip (shadow, dark seam, bright edge) that the bezel's right
# corners include and its left corners do not.
CHAMFER = {"tl": 8, "tr": 12, "bl": 6, "br": 8}


def cut_corners(im):
    w, h = im.size
    px = im.load()
    for y in range(h):
        for x in range(w):
            if (x + y < CHAMFER["tl"]
                    or (w - 1 - x) + y < CHAMFER["tr"]
                    or x + (h - 1 - y) < CHAMFER["bl"]
                    or (w - 1 - x) + (h - 1 - y) < CHAMFER["br"]):
                px[x, y] = (0, 0, 0, 0)
    return im


def body_layout(im):
    """Where the window is, in body pixels. Written beside the art so the C side can
    assert against it rather than re-measure."""
    return {"w": im.width, "h": im.height,
            "win": [WIN_X0, WIN_Y0 - BEZEL_Y0, (WIN_X1 - WIN_X0) + INSERT, 2 * KEEP]}


# ---- the tabs -------------------------------------------------------------------
TAB_W, TAB_H = 24, 26
PLATE_H = 17
PLATE_MID = (6, 11)      # the plate's flat rows, between its top and bottom bevels


# Each tab is its own framed button, assembled from the tab plate's OWN ends so ten in
# a row meet the way the top bar's tabs meet: a dark shadow edge against the next one's
# bright bevel. A slice out of the plate's middle had no ends at all, so the ten tabs
# read as one continuous ribbed rail with digits sprinkled along it, and the last one
# looked cut off where the card stopped.
#
# Measured off frame 0 of tab_plate: the left end is columns 0..3 (bright bevel down
# column 0, chamfer 3: the lip turns the corner at (3,0), (2,1), (1,2), (0,3)), the
# rivet sits in 5..11 and is left out, the ribbed field runs from column 20, and the
# right end is columns 154..159 (a softer chamfer of 5, a dark edge down column 158
# and the shadow it throws in 159). 4 + 14 + 6 = 24.
END_L = (0, 4)
END_R = (154, 160)
FIELD_X = 20
CHAMFER_L, CHAMFER_R = 3, 5


def tab_mask(w, h):
    """chamfer_mask with a different radius on each side, because the plate's two ends
    are not the same shape."""
    m = np.ones((h, w), bool)
    yy, xx = np.mgrid[0:h, 0:w]
    for (cx, cy, r) in ((0, 0, CHAMFER_L), (0, h - 1, CHAMFER_L),
                        (w - 1, 0, CHAMFER_R), (w - 1, h - 1, CHAMFER_R)):
        m &= ~((np.abs(xx - cx) + np.abs(yy - cy)) < r)
    return m


def tab():
    # The plate is 17 rows and the tab wants 26, so its flat middle is repeated to
    # make up the difference: top bevel, middle rows as many times as it takes, bottom
    # bevel. The ends are cut from the same rows, so their bevels line up with the
    # field's.
    lw = END_L[1] - END_L[0]
    rw = END_R[1] - END_R[0]
    plate = Image.new("RGBA", (TAB_W, PLATE_H), (0, 0, 0, 0))
    plate.paste(TAB_PLATE.crop((END_L[0], 0, END_L[1], PLATE_H)), (0, 0))
    plate.paste(TAB_PLATE.crop((FIELD_X, 0, FIELD_X + TAB_W - lw - rw, PLATE_H)), (lw, 0))
    plate.paste(TAB_PLATE.crop((END_R[0], 0, END_R[1], PLATE_H)), (TAB_W - rw, 0))
    base = Image.new("RGBA", (TAB_W, TAB_H), (0, 0, 0, 0))
    base.paste(plate.crop((0, 0, TAB_W, PLATE_MID[0])), (0, 0))
    y = PLATE_MID[0]
    while y < TAB_H - (PLATE_H - PLATE_MID[1]):
        mid = plate.crop((0, PLATE_MID[0], TAB_W, PLATE_MID[1]))
        h = min(mid.height, TAB_H - (PLATE_H - PLATE_MID[1]) - y)
        base.paste(mid.crop((0, 0, TAB_W, h)), (0, y))
        y += h
    base.paste(plate.crop((0, PLATE_MID[1], TAB_W, PLATE_H)), (0, y))
    mask = tab_mask(TAB_W, TAB_H)
    lab = states.facets(mask)
    U, D, L, R, F = states.UP, states.DOWN, states.LEFT, states.RIGHT, states.FACE
    frames = [
        # 0 EMPTY: everything sunk to a little over half. Not a tint of the resting
        # frame at runtime -- states are frames on this panel -- but a frame authored
        # dark, so an unused slot reads as a plate with nothing behind it.
        states.relight(base, mask, lab, {U: 0.55, L: 0.55, F: 0.58, D: 0.55, R: 0.55}),
        states.relight(base, mask, lab, {}),
        states.relight(base, mask, lab,
                       {U: 1.22, L: 1.55, F: 1.70, D: 1.45, R: 1.45}, floor=0.7),
        states.relight(base, mask, lab,
                       {U: 1.22, L: 1.55, F: 1.70, D: 1.45, R: 1.45}, warm=0.85, floor=0.7),
    ]
    # relight leaves the corners outside the chamfer opaque, carrying the plate pixels
    # unchanged, which is right for a button set into the chassis: there the chassis
    # is what those pixels are. A tab on the card has nothing behind it but the map,
    # so its corners go transparent and the map shows through the chamfer, as it
    # does at the body's corners.
    clear = np.zeros((TAB_H, TAB_W, 4), np.uint8)
    strip = Image.new("RGBA", (TAB_W * len(frames), TAB_H), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        a = np.array(f)
        a[~mask] = clear[~mask]
        strip.alpha_composite(Image.fromarray(a, "RGBA"), (i * TAB_W, 0))
    return strip


# ---- the mini well ---------------------------------------------------------------
MINI_W, MINI_H = 36, 28


def mini():
    base = CELL_WELL.resize((MINI_W, MINI_H), Image.LANCZOS)
    mask = states.chamfer_mask(MINI_W, MINI_H, 3)
    lab = states.facets(mask)
    U, D, L, R, F = states.UP, states.DOWN, states.LEFT, states.RIGHT, states.FACE
    frames = [
        states.relight(base, mask, lab, {}),
        states.relight(base, mask, lab,
                       {U: 1.6, L: 1.6, F: 1.9, D: 1.6, R: 1.6}, floor=0.9),
    ]
    strip = Image.new("RGBA", (MINI_W * len(frames), MINI_H), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.alpha_composite(f, (i * MINI_W, 0))
    return strip


# ---- the cameo frames ----------------------------------------------------------------
# A filled build slot is drawn cameo first, then a frame ring back over it, because the
# cameo box (64x48) is larger than the well (61x45) and would otherwise sit on the
# bevel. The ring that shipped for this was a plain RECTANGLE, four pixels deep, while
# the well it sits over is an octagon (its corners cut at CHAMFER_WELL, which is also
# what the cameo is clipped to), so a filled slot showed a square ring with a dark
# triangle in each corner, next to empty slots whose bevels turn the corner.
#
# Both rings are now the chassis's own pixels around the first slot: the frame box at
# (FRAME_DX, FRAME_DY) with the cameo's octagon punched out of it, so what goes back
# over a cameo is exactly what was under it, bevel corners and all.
#
#   cell_frame  for the sidebar: everything in the box outside the octagon, including
#               the plate in the four corners, because on the sidebar that plate is
#               what is there.
#   card_frame  for the unit card, which stands over a black window: the same ring
#               with the plate outside the corner diagonals cleared, so the ring is an
#               octagon like everything else on the card.
CELL_W, CELL_H = 61, 45
FRAME_DX, FRAME_DY = -3, -4
FRAME_W, FRAME_H = 68, 54
CHAMFER_WELL = 7          # H6_CHAMFER: the cut the cameo is clipped to


def _well_corner_distance(w, h):
    """For every pixel of the frame box, its Manhattan distance inward from the nearest
    well corner along that corner's diagonal (negative outside the well)."""
    yy, xx = np.mgrid[0:h, 0:w]
    x0, y0 = -FRAME_DX, -FRAME_DY
    x1, y1 = x0 + CELL_W - 1, y0 + CELL_H - 1
    return np.minimum.reduce([(xx - x0) + (yy - y0), (x1 - xx) + (yy - y0),
                              (xx - x0) + (y1 - yy), (x1 - xx) + (y1 - yy)])


def cell_frame():
    lay = json.load(open(f"{CH}/layout.json"))
    cx, cy = lay["COLS"][0], lay["ROWS"][0]
    box = CHASSIS.crop((cx + FRAME_DX, cy + FRAME_DY,
                        cx + FRAME_DX + FRAME_W, cy + FRAME_DY + FRAME_H))
    a = np.array(box)
    yy, xx = np.mgrid[0:FRAME_H, 0:FRAME_W]
    x0, y0 = -FRAME_DX, -FRAME_DY
    inside = (xx >= x0) & (xx < x0 + CELL_W) & (yy >= y0) & (yy < y0 + CELL_H)
    inside &= _well_corner_distance(FRAME_W, FRAME_H) >= CHAMFER_WELL
    a[inside] = 0
    return Image.fromarray(a, "RGBA")


def card_frame():
    a = np.array(cell_frame())
    a[_well_corner_distance(FRAME_W, FRAME_H) < 1] = 0
    return Image.fromarray(a, "RGBA")


# ---- the health segments -------------------------------------------------------------
# The power meter's segment (meter_segment_lit / _unlit, 17x12) is one link of a
# vertical chain, and it is SUNK: a dark wall down its left, a bright edge up its
# right, a bright lip along its bottom where the light catches the far side of the
# recess, and the segment above supplying its top. The card's health bar is RAISED
# instead, a row of blocks standing on the window: the same segment turned over both
# ways, so the bright lip is on top and the bright edge on the left, and under it a
# two row rim made from that lip dimmed, the shaded underside of a block. The rim is
# dimmed rather than dark because the window behind the bar is black: a shadow line
# there is invisible and the block looks cut off at the bottom. 17x14.
RIM = (0.7, 0.45)         # the lip's brightness, row by row, down the underside


def card_seg(name):
    seg = Image.open(f"{CH}/{name}.png").convert("RGBA")
    a = np.array(seg.transpose(Image.FLIP_LEFT_RIGHT)
                    .transpose(Image.FLIP_TOP_BOTTOM)).astype(float)
    lip = a[0:1]
    rows = [a] + [np.clip(lip * np.array([g, g, g, 1.0]), 0, 255) for g in RIM]
    return Image.fromarray(np.concatenate(rows).astype(np.uint8), "RGBA")


# ---- the fonts --------------------------------------------------------------------
FIRST, LAST = 32, 95          # space .. underscore: digits, punctuation, upper case


def font_strip(scale, cell):
    """ASCII FIRST..LAST as fixed cells, each glyph at its own ink width against the
    cell's left edge. The C side measures the ink to get a proportional advance, so
    the cell only has to be wide enough for the widest letter plus its shadow."""
    saved = tabs.SCALE
    tabs.SCALE = scale
    try:
        n = LAST - FIRST + 1
        strip = Image.new("RGBA", (cell * n, cell), (0, 0, 0, 0))
        for i in range(n):
            c = chr(FIRST + i)
            if c == " ":
                continue
            g = tabs.text(c)
            if g.width > cell or g.height > cell:
                sys.exit("font cell %d too small for %r (%dx%d)" % (cell, c, g.width, g.height))
            strip.alpha_composite(g, (i * cell, 0))
        return strip
    finally:
        tabs.SCALE = saved


if __name__ == "__main__":
    b = body()
    b.save(f"{CH}/card_body.png")
    tab().save(f"{CH}/card_tab.png")
    mini().save(f"{CH}/card_mini.png")
    font_strip(1.75, 15).save(f"{CH}/font_big.png")
    font_strip(1.25, 11).save(f"{CH}/font_mid.png")
    font_strip(1.0, 9).save(f"{CH}/font_small.png")
    cell_frame().save(f"{CH}/cell_frame.png")
    card_frame().save(f"{CH}/card_frame.png")
    card_seg("meter_segment_lit").save(f"{CH}/card_seg_lit.png")
    card_seg("meter_segment_unlit").save(f"{CH}/card_seg_unlit.png")
    lay = body_layout(b)
    lay.update({"tab": [TAB_W, TAB_H, 4], "mini": [MINI_W, MINI_H, 2],
                "font_big": [15, FIRST, LAST], "font_mid": [11, FIRST, LAST],
                "font_small": [9, FIRST, LAST]})
    json.dump(lay, open(f"{CH}/card.json", "w", newline="\n"), indent=1)
    print("card_body %dx%d window %s" % (b.width, b.height, lay["win"]))
