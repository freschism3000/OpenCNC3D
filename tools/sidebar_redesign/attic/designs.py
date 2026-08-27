"""Five chrome systems for the DOS sidebar. Different FORMS, not different palettes:
cut corners, corner brackets, hooded bezels, vent slots, chevrons, segmented meters.
Sizes and positions never move; only the treatment does."""
from PIL import Image, ImageDraw
import numpy as np, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit as K

# ---------------------------------------------------------------- primitives
def new(w, h, c=None): return Image.new("RGBA", (w, h), (c + (255,)) if c else (0,0,0,0))
def P(im): return im.load()

def chamfer_mask(w, h, r):
    m = Image.new("L", (w, h), 255); d = ImageDraw.Draw(m)
    for (cx, cy, sx, sy) in ((0,0,1,1),(w-1,0,-1,1),(0,h-1,1,-1),(w-1,h-1,-1,-1)):
        for i in range(r):
            for j in range(r - i): d.point((cx + sx*i, cy + sy*j), 0)
    return np.array(m) > 0

def edge_ring(m):
    p = np.pad(m, 1)
    return m & ~(p[:-2,1:-1] & p[2:,1:-1] & p[1:-1,:-2] & p[1:-1,2:])

def shell(im, mask, hi, lo, mid, n=1):
    px = P(im); h, w = mask.shape
    cur = mask
    for _ in range(n):
        ring = edge_ring(cur)
        for y, x in zip(*np.where(ring)):
            up   = y == 0   or not cur[y-1, x]
            left = x == 0   or not cur[y, x-1]
            dn   = y == h-1 or not cur[y+1, x]
            rt   = x == w-1 or not cur[y, x+1]
            c = hi if (up or left) and not (dn or rt) else (
                lo if (dn or rt) and not (up or left) else mid)
            px[x, y] = c + (255,)
        cur = cur & ~ring

def fill_mask(im, mask, c):
    px = P(im)
    for y, x in zip(*np.where(mask)): px[x, y] = c + (255,)

def rivet(im, x, y, t):
    px = P(im)
    px[x,y] = t["hi1"]+(255,); px[x+1,y] = t["mid"]+(255,)
    px[x,y+1] = t["mid"]+(255,); px[x+1,y+1] = t["lo1"]+(255,)

def vents(im, x0, x1, y0, n, t, gap=2):
    px = P(im)
    for i in range(n):
        y = y0 + i*gap
        for x in range(x0, x1+1):
            px[x,y] = t["lo2"]+(255,)
            if y+1 < im.height: px[x,y+1] = t["hi2"]+(255,)

def chevron(im, cx, cy, half, thick, up, c, edge=None):
    """cy is the APEX row; arms grow away from it."""
    px = P(im)
    for i in range(half+1):
        for k in range(thick):
            y = cy + (i if up else -i) + (k if up else -k)
            for x in (cx-i, cx+i):
                if 0 <= x < im.width and 0 <= y < im.height: px[x,y] = c+(255,)
    if edge:
        for i in range(half+1):
            y = cy + (i if up else -i)
            for x in (cx-i, cx+i):
                if 0 <= x < im.width and 0 <= y < im.height: px[x,y] = edge+(255,)

def tri(im, cx, cy, half, up, c, hi=None, lo=None):
    px = P(im)
    for r in range(half):
        y = cy - r if up else cy + r
        for x in range(cx-(half-r)+1, cx+(half-r)):
            if 0 <= x < im.width and 0 <= y < im.height: px[x,y] = c+(255,)
    for col, off in ((hi, 1), (lo, -1)):
        if not col: continue
        for r in range(half):
            y = cy - r if up else cy + r
            x = cx-(half-r)+1 if off == 1 else cx+(half-r)-1
            if 0 <= x < im.width and 0 <= y < im.height: px[x,y] = col+(255,)

def engrave(im, mask, x, y, dark, light):
    K.stamp(im, mask, x, y+1, light+(255,)); K.stamp(im, mask, x, y, dark+(255,))

def fit_emblem(iw, ih, faction="GDI"):
    """Uniform scale and centre. The medallion is round; squashing it into a
    non-square bezel opening distorts it."""
    e = K.emblem(faction)
    if (iw, ih) == (80, 69): return e
    s = min(iw/80, ih/69)
    w, h = max(1, round(80*s)), max(1, round(69*s))
    out = Image.new("RGBA", (iw, ih), (0,0,0,0))
    out.alpha_composite(e.resize((w, h), Image.LANCZOS), ((iw-w)//2, (ih-h)//2))
    return out

# ---------------------------------------------------------------- palettes + style
DESIGNS = {
"D1-machined": dict(name="Machined", style="machined",
  blurb="Milled plate. Cut corners, rivets, screw heads, vent slots.",
  face=(0x8c,0x94,0xa0), hi1=(0xe8,0xf0,0xfc), hi2=(0xb8,0xc0,0xcc), mid=(0x6c,0x74,0x80),
  lo1=(0x44,0x4c,0x58), lo2=(0x24,0x28,0x30), well=(0x3c,0x44,0x4c),
  ink=(0xe8,0xf0,0xfc), accent=(0x28,0xc0,0x54), accent2=(0xfc,0xc0,0x30), screen=(0x0c,0x10,0x14)),
"D2-reticle": dict(name="Reticle", style="reticle",
  blurb="Brackets, not boxes. Open edges show more cameo art.",
  face=(0x3c,0x40,0x44), hi1=(0xd8,0xe4,0xd8), hi2=(0x8c,0x98,0x90), mid=(0x58,0x60,0x5c),
  lo1=(0x24,0x28,0x2c), lo2=(0x10,0x12,0x14), well=(0x1c,0x20,0x22),
  ink=(0xd8,0xe4,0xd8), accent=(0x54,0xfc,0x88), accent2=(0xfc,0xd0,0x48), screen=(0x08,0x0c,0x0c)),
"D3-rugged": dict(name="Ruggedised", style="rugged",
  blurb="Moulded field case. Hooded bezel, label ledges, segmented meter.",
  face=(0x8c,0x84,0x60), hi1=(0xe8,0xe0,0xb0), hi2=(0xb4,0xac,0x84), mid=(0x6c,0x64,0x48),
  lo1=(0x44,0x40,0x2c), lo2=(0x24,0x22,0x16), well=(0x38,0x34,0x24),
  ink=(0xf0,0xe8,0xc0), accent=(0xd0,0xa8,0x18), accent2=(0xfc,0xe0,0x60), screen=(0x14,0x14,0x0c)),
"D4-crt": dict(name="CRT Console", style="crt",
  blurb="Curved glass, rounded screen, vignette, corner nodes.",
  face=(0x30,0x38,0x34), hi1=(0x84,0xd8,0x84), hi2=(0x50,0x70,0x58), mid=(0x24,0x2c,0x28),
  lo1=(0x18,0x1e,0x1a), lo2=(0x08,0x0c,0x08), well=(0x0c,0x14,0x0e),
  ink=(0xa0,0xe0,0x1c), accent=(0x54,0xfc,0x54), accent2=(0xd0,0xf0,0x00), screen=(0x04,0x0a,0x04)),
"D5-slab": dict(name="Slab", style="slab",
  blurb="Brutalist mass. Thick frames, deep sinks, no ornament.",
  face=(0x9c,0x9c,0x9c), hi1=(0xfc,0xfc,0xfc), hi2=(0xcc,0xcc,0xcc), mid=(0x74,0x74,0x74),
  lo1=(0x44,0x44,0x44), lo2=(0x1c,0x1c,0x1c), well=(0x38,0x38,0x38),
  ink=(0xfc,0xfc,0xfc), accent=(0x00,0xc0,0x00), accent2=(0x50,0x50,0xfc), screen=(0x00,0x00,0x00)),
}

# ---------------------------------------------------------------- header 80x7
def header(t):
    s = t["style"]; im = new(80, 7, t["face"]); px = P(im)
    if s == "machined":
        m = chamfer_mask(80, 7, 2); im = new(80, 7)
        fill_mask(im, m, t["face"]); shell(im, m, t["hi1"], t["lo1"], t["mid"])
        rivet(im, 3, 2, t); rivet(im, 75, 2, t)
        engrave(im, K.SIDEBAR_TXT, 20, 1, t["lo2"], t["hi1"])
    elif s == "reticle":
        im = new(80, 7); px = P(im)
        for x in range(80): px[x, 3] = t["mid"]+(255,)
        for x in range(0, 80, 8): px[x,2] = t["hi2"]+(255,); px[x,4] = t["hi2"]+(255,)
        for x in range(17, 64):
            for y in range(7): px[x, y] = (0,0,0,0)
        K.stamp(im, K.SIDEBAR_TXT, 20, 1, t["ink"]+(255,))
        for x in (17, 63):
            for y in range(1, 6): px[x, y] = t["accent"]+(255,)
    elif s == "rugged":
        for x in range(80):
            px[x,0] = t["hi1"]+(255,); px[x,1] = t["hi2"]+(255,)
            px[x,5] = t["lo1"]+(255,); px[x,6] = t["lo2"]+(255,)
        for x in range(34, 46):
            px[x,0] = t["lo1"]+(255,); px[x,1] = t["mid"]+(255,)
        engrave(im, K.SIDEBAR_TXT, 20, 1, t["lo2"], t["hi2"])
    elif s == "crt":
        for y in range(7):
            g = 1.0 - abs(y-3)/4.0
            for x in range(80):
                px[x,y] = tuple(int(a+(b-a)*g*0.55) for a,b in zip(t["face"],t["hi2"]))+(255,)
        for x in range(80): px[x,6] = t["lo2"]+(255,)
        K.stamp(im, K.SIDEBAR_TXT, 20, 1, t["ink"]+(255,), t["lo2"]+(255,))
    else:
        for x in range(80): px[x,0] = t["hi2"]+(255,); px[x,6] = t["lo2"]+(255,)
        engrave(im, K.SIDEBAR_TXT, 20, 1, t["lo2"], t["hi1"])
    return im

# ---------------------------------------------------------------- cell 32x24
def cell(t, empty):
    s = t["style"]; im = new(32, 24); px = P(im)
    if s == "machined":
        m = chamfer_mask(32, 24, 3)
        if empty: fill_mask(im, m, t["well"])
        shell(im, m, t["hi2"], t["lo2"], t["mid"])
    elif s == "reticle":
        if empty:
            for y in range(2, 22):
                for x in range(2, 30): px[x,y] = t["well"]+(255,)
        for (cx, cy, sx, sy) in ((0,0,1,1),(31,0,-1,1),(0,23,1,-1),(31,23,-1,-1)):
            for i in range(5):
                px[cx+sx*i, cy] = t["hi2"]+(255,); px[cx, cy+sy*i] = t["hi2"]+(255,)
            for i in range(3):
                px[cx+sx*i, cy+sy] = t["lo2"]+(255,); px[cx+sx, cy+sy*i] = t["lo2"]+(255,)
    elif s == "rugged":
        if empty:
            for y in range(3, 22):
                for x in range(3, 29): px[x,y] = t["well"]+(255,)
        for k, c in enumerate((t["lo2"], t["lo1"], t["mid"])):
            for x in range(k, 32-k):
                px[x,k] = c+(255,); px[x,23-k] = (t["hi2"] if k else t["mid"])+(255,)
            for y in range(k, 24-k):
                px[k,y] = c+(255,); px[31-k,y] = (t["hi2"] if k else t["mid"])+(255,)
        for x in range(3, 29): px[x,22] = t["hi2"]+(255,)      # label ledge, hairline
    elif s == "crt":
        if empty:
            for y in range(1, 23):
                for x in range(1, 31): px[x,y] = t["well"]+(255,)
        for x in range(32): px[x,0] = t["hi2"]+(255,); px[x,23] = t["lo2"]+(255,)
        for y in range(24): px[0,y] = t["hi2"]+(255,); px[31,y] = t["lo2"]+(255,)
        for (cx, cy, sx, sy) in ((0,0,1,1),(31,0,-1,1),(0,23,1,-1),(31,23,-1,-1)):
            for i in range(3):
                px[cx+sx*i, cy] = t["accent"]+(255,); px[cx, cy+sy*i] = t["accent"]+(255,)
            px[cx, cy] = (0,0,0,0)
    else:
        if empty:
            for y in range(3, 21):
                for x in range(3, 29): px[x,y] = t["well"]+(255,)
        for k, c in enumerate((t["lo2"], t["lo1"], t["mid"])):
            for x in range(k, 32-k): px[x,k] = c+(255,)
            for y in range(k, 24-k): px[k,y] = c+(255,)
        for k, c in enumerate((t["hi2"], t["mid"], t["mid"])):
            for x in range(k, 32-k): px[x,23-k] = c+(255,)
            for y in range(k, 24-k): px[31-k,y] = c+(255,)
    return im

# ---------------------------------------------------------------- arrows 16x12
def arrow(t, up, pressed=False):
    s = t["style"]; im = new(16, 12); px = P(im); o = 1 if pressed else 0
    if s == "machined":
        m = chamfer_mask(16, 12, 2)
        fill_mask(im, m, t["face"]); shell(im, m, t["hi1"], t["lo1"], t["mid"])
        chevron(im, 7+o, (3 if up else 8)+o, 4, 2, up, t["lo1"], t["hi2"])
    elif s == "reticle":
        for (cx, cy, sx, sy) in ((0,0,1,1),(15,0,-1,1),(0,11,1,-1),(15,11,-1,-1)):
            for i in range(4):
                px[cx+sx*i, cy] = t["mid"]+(255,); px[cx, cy+sy*i] = t["mid"]+(255,)
        chevron(im, 7+o, (3 if up else 8)+o, 4, 2, up, t["accent"])
    elif s == "rugged":
        for k, c in enumerate((t["lo2"], t["lo1"])):
            for x in range(k, 16-k): px[x,k] = c+(255,)
            for y in range(k, 12-k): px[k,y] = c+(255,)
        for k, c in enumerate((t["hi1"], t["hi2"])):
            for x in range(k, 16-k): px[x,11-k] = c+(255,)
            for y in range(k, 12-k): px[15-k,y] = c+(255,)
        for y in range(2, 10):
            for x in range(2, 14): px[x,y] = t["face"]+(255,)
        tri(im, 8+o, (9 if up else 2)+o, 6, up, t["lo1"], t["hi1"], t["lo2"])
    elif s == "crt":
        m = chamfer_mask(16, 12, 2)
        fill_mask(im, m, t["face"]); shell(im, m, t["hi2"], t["lo2"], t["mid"])
        chevron(im, 7+o, (2 if up else 9)+o, 3, 2, up, t["accent"])
        chevron(im, 7+o, (6 if up else 5)+o, 3, 2, up, t["hi2"])
    else:
        for x in range(16): px[x,0] = t["hi1"]+(255,); px[x,11] = t["lo2"]+(255,)
        for y in range(12): px[0,y] = t["hi1"]+(255,); px[15,y] = t["lo2"]+(255,)
        for y in range(1, 11):
            for x in range(1, 15): px[x,y] = t["face"]+(255,)
        tri(im, 8+o, (10 if up else 1)+o, 8, up, t["lo1"], t["hi2"], t["lo2"])
    return im

# ---------------------------------------------------------------- buttons
def button(t, label, w):
    s = t["style"]; h = 9; im = new(w, h); px = P(im)
    m, _ = K.BTN_LABELS[label]; gx = (w - m.shape[1]) // 2
    if s == "machined":
        mk = chamfer_mask(w, h, 2)
        fill_mask(im, mk, t["face"]); shell(im, mk, t["hi1"], t["lo1"], t["mid"])
        engrave(im, m, gx, 2, t["lo2"], t["hi1"])
    elif s == "reticle":
        for (cx, cy, sx, sy) in ((0,0,1,1),(w-1,0,-1,1),(0,h-1,1,-1),(w-1,h-1,-1,-1)):
            for i in range(3):
                px[cx+sx*i, cy] = t["mid"]+(255,); px[cx, cy+sy*i] = t["mid"]+(255,)
        K.stamp(im, m, gx, 2, t["ink"]+(255,))
    elif s == "rugged":
        for k, c in enumerate((t["hi1"], t["hi2"])):
            for x in range(k, w-k): px[x,k] = c+(255,)
            for y in range(k, h-k): px[k,y] = c+(255,)
        for k, c in enumerate((t["lo2"], t["lo1"])):
            for x in range(k, w-k): px[x,h-1-k] = c+(255,)
            for y in range(k, h-k): px[w-1-k,y] = c+(255,)
        for y in range(2, h-2):
            for x in range(2, w-2): px[x,y] = t["face"]+(255,)
        engrave(im, m, gx, 2, t["lo2"], t["hi2"])
    elif s == "crt":
        mk = chamfer_mask(w, h, 2)
        fill_mask(im, mk, t["face"]); shell(im, mk, t["hi2"], t["lo2"], t["mid"])
        K.stamp(im, m, gx, 2, t["ink"]+(255,), t["lo2"]+(255,))
    else:
        for x in range(w): px[x,0] = t["hi1"]+(255,); px[x,h-1] = t["lo2"]+(255,)
        for y in range(h): px[0,y] = t["hi1"]+(255,); px[w-1,y] = t["lo2"]+(255,)
        for y in range(1, h-1):
            for x in range(1, w-1): px[x,y] = t["face"]+(255,)
        engrave(im, m, gx, 2, t["lo2"], t["hi1"])
    return im

# ---------------------------------------------------------------- radar 80x69
def radar(t, faction="GDI"):
    s = t["style"]; im = new(80, 69, t["face"]); px = P(im)
    if s == "machined":
        b = 4; m = chamfer_mask(80, 69, 4); im = new(80, 69)
        fill_mask(im, m, t["face"]); shell(im, m, t["hi1"], t["lo1"], t["mid"]); px = P(im)
        for y in range(b, 69-b-6):
            for x in range(b, 80-b): px[x,y] = t["screen"]+(255,)
        for x in range(b, 80-b): px[x,b] = t["lo2"]+(255,)
        for y in range(b, 69-b-6): px[b,y] = t["lo2"]+(255,)
        vents(im, 8, 71, 69-b-5, 2, t)
        for (sx, sy) in ((3,3),(75,3),(3,62),(75,62)): rivet(im, sx, sy, t)
        im.alpha_composite(fit_emblem(80-2*b, 69-b-6-b, faction), (b, b))
    elif s == "reticle":
        im = new(80, 69, t["screen"]); px = P(im)
        im.alpha_composite(fit_emblem(80, 69, faction), (0, 0))
        for (cx, cy, sx, sy) in ((1,1,1,1),(78,1,-1,1),(1,67,1,-1),(78,67,-1,-1)):
            for i in range(10):
                px[cx+sx*i, cy] = t["accent"]+(255,); px[cx, cy+sy*i] = t["accent"]+(255,)
        for x in range(0, 80, 8): px[x,0] = t["hi2"]+(255,); px[x,68] = t["hi2"]+(255,)
        for y in range(0, 69, 8): px[0,y] = t["hi2"]+(255,); px[79,y] = t["hi2"]+(255,)
    elif s == "rugged":
        hood, b = 7, 4
        m = chamfer_mask(80, 69, 5); im = new(80, 69)
        fill_mask(im, m, t["face"]); shell(im, m, t["hi1"], t["lo1"], t["mid"]); px = P(im)
        for y in range(hood, 69-b-5):
            for x in range(b, 80-b): px[x,y] = t["screen"]+(255,)
        im.alpha_composite(fit_emblem(80-2*b, 69-b-5-hood, faction), (b, hood))
        for x in range(b, 80-b):
            for k, a in enumerate((0.75, 0.5, 0.25)):
                c = px[x, hood+k]
                px[x, hood+k] = tuple(int(v*(1-a)) for v in c[:3])+(255,)
        vents(im, 10, 69, 69-b-4, 2, t)
    elif s == "crt":
        b = 5
        scr = chamfer_mask(80-2*b, 69-2*b, 5)
        sub = new(80-2*b, 69-2*b); fill_mask(sub, scr, t["screen"])
        sub.alpha_composite(fit_emblem(80-2*b, 69-2*b, faction), (0, 0))
        sp = P(sub); sw, sh = sub.size
        for y in range(sh):
            for x in range(sw):
                if not scr[y, x]: sp[x,y] = (0,0,0,0); continue
                dx, dy = (x-sw/2)/(sw/2), (y-sh/2)/(sh/2)
                v = 1 - 0.55*min(1, (dx*dx+dy*dy)**1.6)
                if y % 3 == 0: v *= 0.78
                c = sp[x,y]; sp[x,y] = tuple(int(k*v) for k in c[:3])+(255,)
        shell(sub, scr, t["lo2"], t["hi2"], t["mid"])
        im.alpha_composite(sub, (b, b))
        shell(im, chamfer_mask(80, 69, 3), t["hi2"], t["lo2"], t["mid"])
    else:
        b = 6
        for k, c in enumerate((t["hi1"], t["hi2"])):
            for x in range(k, 80-k): px[x,k] = c+(255,)
            for y in range(k, 69-k): px[k,y] = c+(255,)
        for k, c in enumerate((t["lo2"], t["lo1"])):
            for x in range(k, 80-k): px[x,68-k] = c+(255,)
            for y in range(k, 69-k): px[79-k,y] = c+(255,)
        for y in range(b, 69-b):
            for x in range(b, 80-b): px[x,y] = t["screen"]+(255,)
        for x in range(b-1, 80-b+1): px[x,b-1] = t["lo2"]+(255,)
        for y in range(b-1, 69-b+1): px[b-1,y] = t["lo2"]+(255,)
        im.alpha_composite(fit_emblem(80-2*b, 69-2*b, faction), (b, b))
    return im

# ---------------------------------------------------------------- power meter 9 x 96
def meter(t, level=0.55, mark=0.66):
    s = t["style"]; h = K.METER_H; im = new(9, h, t["face"]); px = P(im)
    top = int(h*(1-level))
    if s == "machined":
        for y in range(h):
            px[0,y] = t["hi1"]+(255,); px[1,y] = t["lo2"]+(255,)
            px[7,y] = t["hi2"]+(255,); px[8,y] = t["lo1"]+(255,)
            for x in range(2, 7): px[x,y] = (t["accent"] if y>=top else t["well"])+(255,)
        for y in range(0, h, 6): px[1,y] = t["hi2"]+(255,); px[7,y] = t["lo2"]+(255,)
    elif s == "reticle":
        for y in range(h):
            px[4,y] = (t["accent"] if y>=top else t["lo1"])+(255,)
            px[3,y] = (t["accent"] if y>=top else t["lo2"])+(255,)
        for y in range(0, h, 8):
            for x in range(1, 3): px[x,y] = t["mid"]+(255,)
        for y in range(0, h, 16):
            for x in range(0, 3): px[x,y] = t["hi2"]+(255,)
    elif s == "rugged":
        for y in range(h): px[0,y] = t["lo2"]+(255,); px[8,y] = t["hi2"]+(255,)
        for i in range(h//5):
            y0 = i*5; on = y0 >= top
            for y in range(y0+1, min(y0+5, h)):
                for x in range(1, 8): px[x,y] = (t["accent"] if on else t["well"])+(255,)
            for x in range(1, 8): px[x,y0] = t["lo2"]+(255,)
    elif s == "crt":
        for y in range(h):
            for x in range(2, 7):
                c = t["accent"] if y>=top else t["well"]
                if y % 3 == 0: c = tuple(int(v*0.7) for v in c)
                px[x,y] = c+(255,)
            px[1,y] = t["lo2"]+(255,); px[7,y] = t["hi2"]+(255,)
        for y in range(0, h, 8): px[0,y] = t["hi2"]+(255,)
    else:
        for y in range(h):
            px[0,y] = t["hi1"]+(255,); px[8,y] = t["lo2"]+(255,)
            for x in range(1, 8): px[x,y] = (t["accent"] if y>=top else t["well"])+(255,)
        for y in range(0, h, 8):
            for x in range(1, 8): px[x,y] = t["lo2"]+(255,)
    my = int(h*(1-mark))
    for x in range(9): px[x,my] = t["accent2"]+(255,)
    for dy in (-1, 1):
        if 0 <= my+dy < h: px[0,my+dy] = t["accent2"]+(255,)
    return im

# ---------------------------------------------------------------- assembly
def build(t, faction="GDI"):
    im = new(K.W, K.H, t["face"]); px = P(im)
    im.alpha_composite(header(t), (0, 0))
    for x in range(K.W): px[x, K.SEP_Y] = t["lo2"]+(255,)
    im.alpha_composite(radar(t, faction), (0, K.RADAR_Y))
    for x0, x1, lab in K.BTN_BOXES:
        im.alpha_composite(button(t, lab, x1-x0+1), (x0, K.BTN_Y))
    im.alpha_composite(meter(t), (0, K.STRIP_Y0))
    empty, frame = cell(t, True), cell(t, False)
    for col, names in zip(K.COL_X, (K.CAMEOS_L, K.CAMEOS_R)):
        for row, nm in zip(K.ROW_Y, names):
            im.alpha_composite(empty, (col, row))
            im.alpha_composite(K.cameo(nm), (col, row))
            im.alpha_composite(frame, (col, row))
    for x, d in K.ARROW_XS:
        im.alpha_composite(arrow(t, d == "up"), (x, K.ARROW_Y))
    return im
