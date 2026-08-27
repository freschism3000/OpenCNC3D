"""H1 Machined, rebuilt as proper pixel art for 640x480.

Everything structural is faceted metal off material.py: flat surfaces at discrete
ramp steps, one light from the top-left, hard outlines, deliberate brush streaks.
Detail (screws, grooves, vents, engraving) is drawn on top rather than generated."""
from PIL import Image, ImageFilter
import numpy as np, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit as K, material as M, medallion as MED
from hires import (W, H, TAB_H, SEP_Y, RADAR_Y, RADAR_H, BTN_Y, BTN_H, BTN_BOXES,
                   METER_X, METER_W, METER_Y0, METER_Y1, STRIP_Y0, CELL_W, CELL_H,
                   COL_X, ROW_Y, ARROW_Y, ARROW_W, ARROW_H, ARROW_XS, TXT, LABEL,
                   stamp, art_hi)

S, G, A = M.STEEL, M.GREEN, M.AMBER
DARK = M.R("04050a","090c14","10141f","171d2b","1f2637","28324a","36425e","4a5878","64749a","8494b8")

def blank(w, h): return Image.new("RGBA", (w, h), (0,0,0,0))

def disc(r):
    d = 2*r + 1
    yy, xx = np.mgrid[0:d, 0:d]
    return ((xx-r)**2 + (yy-r)**2) <= r*r

def screw(dst, cx, cy, r=5, ramp=S):
    """Domed head, cut slot, seated shadow. A disc lit with bevel == radius domes."""
    m = disc(r)
    head = M.facet_shade(m, bevel=r, ramp=ramp, base=5.2, spec=True).copy()
    px = head.load(); d = 2*r+1
    for x in range(1, d-1):                                   # slot
        if m[r, x]:
            px[x, r] = ramp[1] + (255,)
            if m[r+1, x]: px[x, r+1] = ramp[7] + (255,)        # bounce off the slot wall
    seat = blank(d+2, d+2); sp = seat.load()
    for y in range(d):
        for x in range(d):
            if m[y, x]: sp[x+2, y+2] = ramp[0] + (140,)
    dst.alpha_composite(seat, (cx-r-1, cy-r-1))
    dst.alpha_composite(head, (cx-r, cy-r))

def groove(dst, x0, x1, y, ramp=S):
    px = dst.load()
    for x in range(x0, x1+1):
        if 0 <= x < dst.width:
            px[x, y]   = ramp[1] + (255,)
            px[x, y+1] = ramp[7] + (255,)

def louvre(dst, x0, x1, y, ramp=S):
    """A vent slat: dark opening, lit lower lip."""
    px = dst.load()
    for x in range(x0, x1+1):
        if 0 <= x < dst.width:
            px[x, y]   = ramp[0] + (255,)
            px[x, y+1] = ramp[2] + (255,)
            px[x, y+2] = ramp[7] + (255,)

def engrave(dst, mask, x, y, ramp=S):
    stamp(dst, mask, x, y+1, ramp[8] + (255,))
    stamp(dst, mask, x, y,   ramp[1] + (255,))

def well(w, h, chamfer, floor=2, bevel=3, ramp=S):
    return M.recess(w, h, chamfer, ramp, bevel=bevel, floor=floor).copy()

# ---------------------------------------------------------------- parts
def header():
    im = M.plate(W, TAB_H, 4, S, bevel=2, brushed=0.55, brush_density=0.10).copy()
    groove(im, 3, W-4, TAB_H-4)
    screw(im, 9, TAB_H//2, 4)
    screw(im, W-10, TAB_H//2, 4)
    gx = (W - TXT.shape[1]) // 2
    engrave(im, TXT, gx, 3)
    return im

def radar(faction="GDI", with_coin=True):
    b, vent_h = 13, 18
    im = M.plate(W, RADAR_H, 9, S, bevel=3, brushed=0.55, brush_density=0.09).copy()
    sx0, sy0 = b, b
    sx1, sy1 = W-b-1, RADAR_H-b-vent_h-1
    sw, sh = sx1-sx0+1, sy1-sy0+1
    scr = well(sw, sh, 4, floor=1, bevel=3, ramp=DARK)
    if with_coin:                             # struck-coin relief, not a blurry upscale
        d = min(sw, sh) - 8
        coin = MED.render((d, d), faction)
        scr.alpha_composite(coin, ((sw-d)//2, (sh-d)//2))
    ring = M.recess(sw+6, sh+6, 5, S, bevel=3, floor=5)
    im.alpha_composite(ring, (sx0-3, sy0-3))
    im.alpha_composite(scr, (sx0, sy0))
    for i in range(3): louvre(im, 30, W-31, sy1 + 8 + i*5)
    groove(im, 8, W-9, sy1 + 5)
    for (cx, cy) in ((11,11),(W-12,11),(11,RADAR_H-12),(W-12,RADAR_H-12)): screw(im, cx, cy, 4)
    return im

def button(label, w):
    im = M.plate(w, BTN_H, 3, S, bevel=2, brushed=0.5, brush_density=0.10).copy()
    m = LABEL[label]
    engrave(im, m, (w - m.shape[1])//2, (BTN_H - m.shape[0])//2)
    return im

def cell(empty):
    ch = 5
    body = well(CELL_W, CELL_H, ch, floor=2, bevel=3).copy()
    if empty:
        return body
    mask = M.rect_mask(CELL_W, CELL_H, ch)
    hole = M.inset(mask, 3)
    px = body.load()
    for y, x in zip(*np.where(hole)): px[x, y] = (0,0,0,0)
    return body

def chevron(dst, cx, cy, half, thick, up, ramp=S):
    px = dst.load()
    for i in range(half+1):
        for k in range(thick):
            y = cy + (i if up else -i) + (k if up else -k)
            for x in (cx-i, cx+i):
                if 0 <= x < dst.width and 0 <= y < dst.height:
                    px[x, y] = ramp[2] + (255,)
    for i in range(half+1):                                   # lit leading edge
        y = cy + (i if up else -i)
        for x in (cx-i, cx+i):
            if 0 <= x < dst.width and 0 <= y < dst.height: px[x, y] = ramp[8] + (255,)
    for i in range(half+1):                                   # dark trailing edge
        y = cy + (i if up else -i) + (thick-1 if up else -(thick-1))
        for x in (cx-i, cx+i):
            if 0 <= x < dst.width and 0 <= y < dst.height: px[x, y] = ramp[0] + (255,)

def arrow(up, pressed=False):
    o = 1 if pressed else 0
    im = M.plate(ARROW_W, ARROW_H, 4, S, bevel=2, brushed=0.5, brush_density=0.12).copy()
    chevron(im, ARROW_W//2 - 1 + o, (6 if up else 17) + o, 8, 4, up)
    return im

def meter(level=0.55, mark=0.68):
    h = METER_Y1 - METER_Y0
    im = well(METER_W, h, 3, floor=2, bevel=3)
    seg_h, gap = 8, 2
    n = (h - 6) // (seg_h + gap)
    top_on = int(n * (1 - level))
    for i in range(n):
        y = 4 + i * (seg_h + gap)
        on = i >= top_on
        ramp = G if on else DARK
        blk = M.plate(METER_W - 8, seg_h, 1, ramp, bevel=1,
                      base=(5.6 if on else 3.2), outline=True, spec=on)
        im.alpha_composite(blk, (4, y))
    my = int(h * (1 - mark))
    tick = M.plate(METER_W, 4, 1, A, bevel=1, base=6.2, spec=True)
    im.alpha_composite(tick, (0, my))
    return im

# ---------------------------------------------------------------- assembly
def build(faction="GDI", with_coin=True, with_cameos=True):
    im = Image.new("RGBA", (W, H), S[4] + (255,))
    body = M.plate(W, H, 0, S, bevel=2, base=4.15, brushed=0.45,
                   brush_density=0.07, outline=False)
    im.alpha_composite(body)
    im.alpha_composite(header(), (0, 0))
    px = im.load()
    for x in range(W): px[x, SEP_Y] = S[0] + (255,)
    im.alpha_composite(radar(faction, with_coin), (0, RADAR_Y))
    for x0, x1, lab in BTN_BOXES:
        im.alpha_composite(button(lab, x1-x0+1), (x0, BTN_Y))
    im.alpha_composite(meter(), (METER_X, METER_Y0))
    empty, frame = cell(True), cell(False)
    names_l = K.CAMEOS_L + ["HAND"]
    names_r = K.CAMEOS_R + ["APC"]
    for col, names in zip(COL_X, (names_l, names_r)):
        for row, nm in zip(ROW_Y, names):
            im.alpha_composite(empty, (col, row))
            if with_cameos:
                im.alpha_composite(K.cameo(nm).resize((CELL_W, CELL_H), Image.NEAREST), (col, row))
            im.alpha_composite(frame, (col, row))
    for x, d in ARROW_XS:
        im.alpha_composite(arrow(d == "up"), (x, ARROW_Y))
    return im

if __name__ == "__main__":
    b = build(); b.save("machined_v2.png")
    b.resize((W*2, H*2), Image.NEAREST).save("machined_v2@2x.png")
    a = np.array(b.convert("RGB")).reshape(-1,3)
    print("built", b.size, "colours:", len(np.unique(a, axis=0)))
