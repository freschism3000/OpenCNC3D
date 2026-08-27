"""Assemble the externally-generated HUD art at 160x480.

The layout is DERIVED FROM THE ART, not imposed on it: panel_body is the chassis, so
its own openings dictate where everything sits. Pieces keep their native aspect ratio;
nothing gets squashed to fit a number I made up earlier."""
from PIL import Image
import numpy as np, os, json

SRC, W, H = "art_new", 160, 480
CAMEOS = ["FACT","E1","NUKE","E2","PROC","JEEP","WEAP","MTNK","HAND","APC"]

# measured off the chassis (bevel highlight lines in panel_body scaled to 160x480)
RADAR  = (15, 24, 129, 108)          # x, y, w, h  -> screen opening
DIVIDE = (26, 135, 124, 24)          # band between radar and build area
BUILD  = (26, 161, 124, 296)
METER  = (10, 185, 15, 259)
TOPBAR = (30, 2, 100, 17)
BOTBAR = (26, 458, 124, 21)

def load(n): return Image.open(f"{SRC}/{n}.png").convert("RGBA")

def fit(im, w, h, mode="contain"):
    """Scale keeping aspect. contain = fit inside, cover = fill and centre-crop."""
    s = min(w/im.width, h/im.height) if mode == "contain" else max(w/im.width, h/im.height)
    nw, nh = max(1, round(im.width*s)), max(1, round(im.height*s))
    r = im.resize((nw, nh), Image.LANCZOS)
    out = Image.new("RGBA", (w, h), (0,0,0,0))
    out.alpha_composite(r, ((w-nw)//2, (h-nh)//2))
    return out

def grid(cols=2, rows=5):
    bx, by, bw, bh = BUILD
    cw, ch = bw // cols, bh // rows
    return cw, ch, [(bx + c*cw, by + r*ch) for r in range(rows) for c in range(cols)]

def build(show_cameos=False):
    base = load("panel_body").resize((W, H), Image.LANCZOS)
    im = Image.new("RGBA", (W, H), (0,0,0,0))
    im.alpha_composite(base)

    tx, ty, tw, th = TOPBAR
    im.alpha_composite(fit(load("text_sidebar"), tw, th), (tx, ty))

    dx, dy, dw, dh = DIVIDE
    btns = [load(f"button_{n}") for n in ("repair", "sell", "map")]
    hh = dh - 2
    scaled = [fit(b, round(hh*b.width/b.height), hh) for b in btns]
    total = sum(s.width for s in scaled); gap = max(2, (dw - total)//(len(scaled)+1))
    x = dx + max(0, (dw - total - gap*(len(scaled)-1))//2)
    for s in scaled:
        im.alpha_composite(s, (x, dy + (dh-hh)//2)); x += s.width + gap

    mx, my, mw, mh = METER
    im.alpha_composite(fit(load("meter_full"), mw, mh, "cover"), (mx, my))

    cw, ch, slots = grid()
    well, frame = load("cell_well"), load("cell_frame")
    wf, ff = fit(well, cw, ch, "cover"), fit(frame, cw, ch, "cover")
    for i, (x, y) in enumerate(slots):
        if show_cameos and i < len(CAMEOS):
            im.alpha_composite(wf, (x, y))
            cam = Image.open(f"art/{CAMEOS[i]}.png").convert("RGBA")
            im.alpha_composite(fit(cam, cw-6, ch-6), (x+3, y+3))
            im.alpha_composite(ff, (x, y))
        else:
            im.alpha_composite(wf, (x, y))        # empty slot: well only

    bx, by, bw, bh = BOTBAR
    ar = [load("arrow_up"), load("arrow_down")]*2
    ah = bh - 1
    sc = [fit(a, round(ah*a.width/a.height), ah) for a in ar]
    tot = sum(s.width for s in sc); g = max(2, (bw - tot)//(len(sc)+1))
    x = bx + max(0, (bw - tot - g*(len(sc)-1))//2)
    for s in sc:
        im.alpha_composite(s, (x, by)); x += s.width + g
    return im, (cw, ch)

if __name__ == "__main__":
    im, (cw, ch) = build()
    im.save("new_hud.png"); im.resize((W*3, H*3), Image.NEAREST).save("new_hud@3x.png")
    im2, _ = build(show_cameos=True)
    im2.save("new_hud_cameos.png"); im2.resize((W*3, H*3), Image.NEAREST).save("new_hud_cameos@3x.png")
    print("assembled 160x480; cell slot = %dx%d (aspect %.2f)" % (cw, ch, cw/ch))
    for n in ("cell_well", "cell_frame"):
        s = Image.open(f"{SRC}/{n}.png"); print("   %s native aspect %.3f" % (n, s.width/s.height))
