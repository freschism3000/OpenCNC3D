"""C&C3D sidebar, drawn natively for 640x480 (Tier 1 floor: Win98 / Voodoo 2, 16-bit).

Not an upscale of the DOS bar. The pixel budget goes from 16,000 to 76,800 and the
pixels are finally square, so curves are curves and bevels get modelled properly
instead of being one light pixel and one dark one.

Placeholders, clearly marked: the 61 cameos and the faction medallion are the
original 32x24 / 80x69 painted art at nearest-neighbour 2x. Those need repainting at
native size; that is illustration work, not chrome, and it is a separate job."""
from PIL import Image, ImageDraw, ImageFilter
import numpy as np, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit as K

# ---------------------------------------------------------------- layout (640x480)
SCREEN_W, SCREEN_H = 640, 480
W, H = 160, 480                      # sidebar keeps its 25% share of screen width

TAB_Y,    TAB_H    = 0,   18
SEP_Y              = 18
RADAR_Y,  RADAR_H  = 19,  160        # square now that pixels are square
BTN_Y,    BTN_H    = 184, 22
BTN_BOXES = [(4, 66, "REPAIR"), (70, 110, "SELL"), (114, 155, "MAP")]
METER_X,  METER_W  = 0,   18
STRIP_Y0           = 210
CELL_W,   CELL_H   = 64,  48
COL_X              = [18, 88]
ROW_Y              = [210 + i*CELL_H for i in range(5)]   # 5 rows now, was 4
ARROW_Y,  ARROW_W, ARROW_H = 454, 32, 24
ARROW_XS  = [(20, "up"), (52, "dn"), (90, "up"), (122, "dn")]
METER_Y0, METER_Y1 = STRIP_Y0, ROW_Y[-1] + CELL_H

# ---------------------------------------------------------------- helpers
def new(w, h, c=None): return Image.new("RGBA", (w, h), (c + (255,)) if c else (0,0,0,0))
def P(im): return im.load()
def lerp(a, b, t): return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))

def mask2x(m):
    return np.repeat(np.repeat(m, 2, axis=0), 2, axis=1)

TXT   = mask2x(K.SIDEBAR_TXT)
LABEL = {k: mask2x(v[0]) for k, v in K.BTN_LABELS.items()}

def stamp(img, mask, x, y, colour, shadow=None):
    px = img.load(); h, w = mask.shape
    if shadow:
        for j in range(h):
            for i in range(w):
                if mask[j,i] and 0 <= x+i+2 < img.width and 0 <= y+j+2 < img.height:
                    px[x+i+2, y+j+2] = shadow
    for j in range(h):
        for i in range(w):
            if mask[j,i] and 0 <= x+i < img.width and 0 <= y+j < img.height:
                px[x+i, y+j] = colour

def vgrad(im, box, top, bot, dither=True):
    """Vertical gradient. RGB565 has only 5-6 bits per channel, so long ramps band;
    a 2x2 ordered dither hides it at no cost the Voodoo cares about."""
    x0, y0, x1, y1 = box; px = P(im)
    n = max(1, y1 - y0)
    B = [[0, 2], [3, 1]]
    for y in range(y0, y1 + 1):
        t = (y - y0) / n
        c = lerp(top, bot, t)
        cu = lerp(top, bot, min(1.0, t + 1.0/n))
        for x in range(x0, x1 + 1):
            use = cu if (dither and B[y & 1][x & 1] >= 2 and c != cu) else c
            px[x, y] = use + (255,)

def bevel(im, box, ramp_hi, ramp_lo, n=3, out=True):
    """Multi-step modelled edge. n tones per side instead of the DOS single pixel."""
    x0, y0, x1, y1 = box; px = P(im)
    hi, lo = (ramp_hi, ramp_lo) if out else (ramp_lo, ramp_hi)
    for k in range(n):
        h, l = hi[min(k, len(hi)-1)], lo[min(k, len(lo)-1)]
        for x in range(x0+k, x1-k+1):
            px[x, y0+k] = h + (255,); px[x, y1-k] = l + (255,)
        for y in range(y0+k, y1-k+1):
            px[x0+k, y] = h + (255,); px[x1-k, y] = l + (255,)

def chamfer_mask(w, h, r):
    m = np.ones((h, w), bool)
    for (cx, cy, sx, sy) in ((0,0,1,1),(w-1,0,-1,1),(0,h-1,1,-1),(w-1,h-1,-1,-1)):
        for i in range(r):
            for j in range(r - i): m[cy + sy*j, cx + sx*i] = False
    return m

def edge_ring(m):
    p = np.pad(m, 1)
    return m & ~(p[:-2,1:-1] & p[2:,1:-1] & p[1:-1,:-2] & p[1:-1,2:])

def shell(im, mask, hi, lo, mid, n=1):
    px = P(im); h, w = mask.shape; cur = mask
    for k in range(n):
        ring = edge_ring(cur)
        H = hi[min(k, len(hi)-1)]; L = lo[min(k, len(lo)-1)]
        for y, x in zip(*np.where(ring)):
            up   = y == 0   or not cur[y-1, x]
            left = x == 0   or not cur[y, x-1]
            dn   = y == h-1 or not cur[y+1, x]
            rt   = x == w-1 or not cur[y, x+1]
            c = H if (up or left) and not (dn or rt) else (
                L if (dn or rt) and not (up or left) else mid)
            px[x, y] = c + (255,)
        cur = cur & ~ring

def fill_mask(im, mask, c):
    px = P(im)
    for y, x in zip(*np.where(mask)): px[x, y] = c + (255,)

def screw(im, cx, cy, t, r=4):
    """A real screw head: dome, slot, seat shadow. Needs the resolution to read."""
    px = P(im)
    for y in range(-r-1, r+2):
        for x in range(-r-1, r+2):
            d = math.hypot(x, y)
            X, Y = cx+x, cy+y
            if not (0 <= X < im.width and 0 <= Y < im.height): continue
            if d <= r:
                lit = (-x - y) / (r * 1.6) + 0.5
                px[X, Y] = lerp(t["lo1"], t["hi1"], max(0.0, min(1.0, lit))) + (255,)
            elif d <= r + 1:
                px[X, Y] = t["lo2"] + (255,)
    for x in range(-r+1, r):                       # slot
        X = cx + x
        if 0 <= X < im.width:
            px[X, cy]   = t["lo2"] + (255,)
            px[X, cy+1] = t["hi2"] + (255,)

def vent(im, x0, x1, y, t, depth=3):
    px = P(im)
    for x in range(x0, x1 + 1):
        for k in range(depth):
            if 0 <= y+k < im.height:
                px[x, y+k] = (t["lo2"] if k < depth-1 else t["hi2"]) + (255,)

def seam(im, x0, x1, y, t):
    px = P(im)
    for x in range(x0, x1 + 1):
        px[x, y]   = t["lo2"] + (255,)
        if y+1 < im.height: px[x, y+1] = t["hi2"] + (255,)

def art2x(im):
    return im.resize((im.width*2, im.height*2), Image.NEAREST)

def cameo2x(name):
    return art2x(K.cameo(name))

def emblem2x(faction="GDI"):
    return art2x(K.emblem(faction))

def fit(im, iw, ih):
    """Uniform scale + centre, nearest so placeholder art stays honest and crisp."""
    s = min(iw/im.width, ih/im.height)
    w, h = max(1, int(im.width*s)), max(1, int(im.height*s))
    out = Image.new("RGBA", (iw, ih), (0,0,0,0))
    out.alpha_composite(im.resize((w, h), Image.NEAREST), ((iw-w)//2, (ih-h)//2))
    return out

def art_hi(im, w, h):
    """Placeholder upscale for the painted art. 16-bit colour means we are no longer
    forced to stay chunky, so smooth + a light sharpen reads closer to the target than
    nearest does. Still a stand-in for a real repaint."""
    o = im.resize((w, h), Image.LANCZOS)
    return o.filter(ImageFilter.UnsharpMask(radius=2, percent=90, threshold=2))

# ---------------------------------------------------------------- designs
def T(**k): return k
DESIGNS = {
"H1-machined": T(name="Machined", style="machined",
  blurb="Milled aluminium. Brushed face, real screw heads, panel seams, deep vents.",
  face=(0x8a,0x93,0xa0), hi=[(0xf2,0xf7,0xff),(0xc4,0xcd,0xda),(0xa6,0xaf,0xbc)],
  lo=[(0x3e,0x46,0x52),(0x2a,0x31,0x3b),(0x18,0x1d,0x25)],
  hi1=(0xf2,0xf7,0xff), hi2=(0xc4,0xcd,0xda), mid=(0x6a,0x73,0x80),
  lo1=(0x3e,0x46,0x52), lo2=(0x18,0x1d,0x25), well=(0x33,0x3a,0x44),
  ink=(0xf2,0xf7,0xff), accent=(0x35,0xd0,0x66), accent2=(0xff,0xc8,0x3a),
  screen=(0x0a,0x0e,0x13), gtop=(0xa2,0xac,0xba), gbot=(0x6e,0x77,0x84)),
"H2-reticle": T(name="Reticle", style="reticle",
  blurb="Brackets, hairlines, negative space. Nothing frames the art that does not have to.",
  face=(0x2e,0x33,0x36), hi=[(0xe0,0xea,0xe2),(0x9a,0xa6,0x9e),(0x6a,0x74,0x6e)],
  lo=[(0x1c,0x20,0x22),(0x12,0x15,0x17),(0x08,0x0a,0x0b)],
  hi1=(0xe0,0xea,0xe2), hi2=(0x8c,0x98,0x90), mid=(0x50,0x58,0x54),
  lo1=(0x1c,0x20,0x22), lo2=(0x08,0x0a,0x0b), well=(0x16,0x1a,0x1c),
  ink=(0xe0,0xea,0xe2), accent=(0x54,0xfc,0x88), accent2=(0xff,0xd0,0x48),
  screen=(0x06,0x09,0x09), gtop=(0x34,0x3a,0x3d), gbot=(0x22,0x26,0x29)),
"H3-rugged": T(name="Ruggedised", style="rugged",
  blurb="Moulded field case. Hooded bezel with a cast shadow, chunky mouldings, segmented meter.",
  face=(0x8a,0x82,0x5e), hi=[(0xf0,0xe8,0xbe),(0xbe,0xb6,0x8e),(0x9c,0x94,0x70)],
  lo=[(0x46,0x42,0x2c),(0x30,0x2d,0x1e),(0x1c,0x1a,0x11)],
  hi1=(0xf0,0xe8,0xbe), hi2=(0xbe,0xb6,0x8e), mid=(0x68,0x62,0x46),
  lo1=(0x46,0x42,0x2c), lo2=(0x1c,0x1a,0x11), well=(0x33,0x30,0x21),
  ink=(0xf6,0xef,0xcd), accent=(0xd8,0xa8,0x20), accent2=(0xff,0xe0,0x66),
  screen=(0x12,0x12,0x0c), gtop=(0x9c,0x94,0x6e), gbot=(0x70,0x69,0x4c)),
"H4-crt": T(name="CRT Console", style="crt",
  blurb="Curved glass under a moulded lip. Vignette, scanlines, phosphor bloom at the corners.",
  face=(0x2a,0x31,0x2d), hi=[(0x8e,0xe0,0x8e),(0x56,0x7c,0x5e),(0x3a,0x4c,0x40)],
  lo=[(0x16,0x1c,0x18),(0x0d,0x12,0x0e),(0x05,0x08,0x06)],
  hi1=(0x8e,0xe0,0x8e), hi2=(0x50,0x70,0x58), mid=(0x22,0x2a,0x26),
  lo1=(0x16,0x1c,0x18), lo2=(0x05,0x08,0x06), well=(0x0a,0x12,0x0c),
  ink=(0xa8,0xe8,0x24), accent=(0x54,0xfc,0x54), accent2=(0xd8,0xf8,0x00),
  screen=(0x03,0x08,0x04), gtop=(0x32,0x3a,0x35), gbot=(0x1e,0x24,0x20)),
"H5-slab": T(name="Slab", style="slab",
  blurb="Brutalist mass. Thick surrounds, deep sinks, one light, no ornament.",
  face=(0x9a,0x9a,0x9a), hi=[(0xff,0xff,0xff),(0xd4,0xd4,0xd4),(0xb4,0xb4,0xb4)],
  lo=[(0x40,0x40,0x40),(0x28,0x28,0x28),(0x14,0x14,0x14)],
  hi1=(0xff,0xff,0xff), hi2=(0xd0,0xd0,0xd0), mid=(0x70,0x70,0x70),
  lo1=(0x40,0x40,0x40), lo2=(0x14,0x14,0x14), well=(0x30,0x30,0x30),
  ink=(0xff,0xff,0xff), accent=(0x18,0xc8,0x18), accent2=(0x60,0x60,0xff),
  screen=(0x00,0x00,0x00), gtop=(0xac,0xac,0xac), gbot=(0x84,0x84,0x84)),
}

def brushed(im, box, t, amp=7):
    """Vertical brush striations. Cheap, and at 2x it finally reads as metal."""
    x0, y0, x1, y1 = box; px = P(im)
    rng = np.random.RandomState(11)
    k = rng.randint(-amp, amp+1, size=(x1-x0+1,))
    for i, x in enumerate(range(x0, x1+1)):
        d = int(k[i])
        for y in range(y0, y1+1):
            c = px[x, y]
            px[x, y] = tuple(max(0, min(255, v + d)) for v in c[:3]) + (255,)

# ---------------------------------------------------------------- header 160x18
def header(t):
    s = t["style"]; im = new(W, TAB_H, t["face"])
    if s == "reticle":
        im = new(W, TAB_H); px = P(im)
        for x in range(W): px[x, TAB_H//2] = t["mid"] + (255,)
        for x in range(0, W, 16):
            for y in range(TAB_H//2 - 3, TAB_H//2 + 4, 6): px[x, y] = t["hi2"] + (255,)
        gx = (W - TXT.shape[1])//2
        for x in range(gx-8, gx+TXT.shape[1]+8):
            for y in range(TAB_H):
                if 0 <= x < W: px[x, y] = (0,0,0,0)
        stamp(im, TXT, gx, 4, t["ink"] + (255,))
        for x in (gx-8, gx+TXT.shape[1]+7):
            for y in range(2, TAB_H-2):
                if 0 <= x < W: px[x, y] = t["accent"] + (255,)
        return im
    m = chamfer_mask(W, TAB_H, 4) if s in ("machined", "crt") else np.ones((TAB_H, W), bool)
    im = new(W, TAB_H); fill_mask(im, m, t["face"])
    vgrad(im, (0, 0, W-1, TAB_H-1), t["gtop"], t["gbot"])
    if s == "machined": brushed(im, (2, 2, W-3, TAB_H-3), t, 6)
    for y in range(TAB_H):
        for x in range(W):
            if not m[y, x]: P(im)[x, y] = (0,0,0,0)
    shell(im, m, t["hi"], t["lo"], t["mid"], n=2)
    if s == "machined":
        screw(im, 9, TAB_H//2, t, 4); screw(im, W-10, TAB_H//2, t, 4)
    if s == "rugged":
        px = P(im)
        for x in range(W//2 - 14, W//2 + 14):
            px[x, 0] = t["lo1"] + (255,); px[x, 1] = t["mid"] + (255,)
    gx = (W - TXT.shape[1])//2
    stamp(im, TXT, gx, 4, t["lo2"] + (255,))
    stamp(im, TXT, gx, 3, t["ink"] + (255,))
    return im

# ---------------------------------------------------------------- radar 160x160
def radar(t, faction="GDI", with_coin=True):
    s = t["style"]; im = new(W, RADAR_H, t["face"]); px = P(im)
    if s == "reticle":
        im = new(W, RADAR_H, t["screen"]); px = P(im)
        if with_coin: im.alpha_composite(fit(art_hi(K.emblem(faction), 156, 134), W, RADAR_H), (0, 0))
        L = 26
        for (cx, cy, sx, sy) in ((2,2,1,1),(W-3,2,-1,1),(2,RADAR_H-3,1,-1),(W-3,RADAR_H-3,-1,-1)):
            for i in range(L):
                px[cx+sx*i, cy] = t["accent"]+(255,); px[cx, cy+sy*i] = t["accent"]+(255,)
            for i in range(L-6):
                px[cx+sx*i, cy+sy] = t["lo2"]+(255,); px[cx+sx, cy+sy*i] = t["lo2"]+(255,)
        for x in range(0, W, 16): px[x,0] = t["hi2"]+(255,); px[x,RADAR_H-1] = t["hi2"]+(255,)
        for y in range(0, RADAR_H, 16): px[0,y] = t["hi2"]+(255,); px[W-1,y] = t["hi2"]+(255,)
        return im
    if s == "machined":
        b, vh = 12, 16
        m = chamfer_mask(W, RADAR_H, 8); im = new(W, RADAR_H)
        fill_mask(im, m, t["face"]); vgrad(im, (0,0,W-1,RADAR_H-1), t["gtop"], t["gbot"])
        brushed(im, (2,2,W-3,RADAR_H-3), t, 6)
        for y in range(RADAR_H):
            for x in range(W):
                if not m[y,x]: P(im)[x,y] = (0,0,0,0)
        shell(im, m, t["hi"], t["lo"], t["mid"], n=3); px = P(im)
        sx0, sy0, sx1, sy1 = b, b, W-b-1, RADAR_H-b-vh-1
        for y in range(sy0, sy1+1):
            for x in range(sx0, sx1+1): px[x,y] = t["screen"]+(255,)
        bevel(im, (sx0-3, sy0-3, sx1+3, sy1+3), t["lo"], t["hi"], n=3, out=True)
        if with_coin: im.alpha_composite(fit(art_hi(K.emblem(faction), 150, 129),
                               sx1-sx0+1, sy1-sy0+1), (sx0, sy0))
        for i in range(3): vent(im, 24, W-25, sy1+8+i*5, t)
        for (cx, cy) in ((11,11),(W-12,11),(11,RADAR_H-12),(W-12,RADAR_H-12)): screw(im, cx, cy, t, 4)
        return im
    if s == "rugged":
        hood, b, vh = 22, 12, 14
        m = chamfer_mask(W, RADAR_H, 10); im = new(W, RADAR_H)
        fill_mask(im, m, t["face"]); vgrad(im, (0,0,W-1,RADAR_H-1), t["gtop"], t["gbot"])
        for y in range(RADAR_H):
            for x in range(W):
                if not m[y,x]: P(im)[x,y] = (0,0,0,0)
        shell(im, m, t["hi"], t["lo"], t["mid"], n=3); px = P(im)
        sx0, sy0, sx1, sy1 = b, hood, W-b-1, RADAR_H-b-vh-1
        for y in range(sy0, sy1+1):
            for x in range(sx0, sx1+1): px[x,y] = t["screen"]+(255,)
        if with_coin: im.alpha_composite(fit(art_hi(K.emblem(faction), 146, 126),
                               sx1-sx0+1, sy1-sy0+1), (sx0, sy0))
        for x in range(sx0, sx1+1):                       # hood casts a real gradient
            for k in range(10):
                a = (1 - k/10.0) * 0.8
                c = px[x, sy0+k]
                px[x, sy0+k] = tuple(int(v*(1-a)) for v in c[:3]) + (255,)
        bevel(im, (sx0-3, sy0-3, sx1+3, sy1+3), t["lo"], t["hi"], n=3, out=True)
        seam(im, 8, W-9, hood-6, t)
        for i in range(2): vent(im, 28, W-29, sy1+7+i*5, t)
        return im
    if s == "crt":
        b = 14
        im = new(W, RADAR_H, t["face"])
        vgrad(im, (0,0,W-1,RADAR_H-1), t["gtop"], t["gbot"])
        sw, sh = W-2*b, RADAR_H-2*b
        scr = chamfer_mask(sw, sh, 16)
        sub = new(sw, sh); fill_mask(sub, scr, t["screen"])
        if with_coin: sub.alpha_composite(fit(art_hi(K.emblem(faction), sw-8, int((sw-8)*69/80)), sw, sh), (0,0))
        sp = P(sub)
        for y in range(sh):
            for x in range(sw):
                if not scr[y,x]: sp[x,y] = (0,0,0,0); continue
                dx, dy = (x-sw/2)/(sw/2), (y-sh/2)/(sh/2)
                v = 1 - 0.6*min(1, (dx*dx+dy*dy)**1.5)
                if y % 3 == 0: v *= 0.74
                c = sp[x,y]; sp[x,y] = tuple(int(k*v) for k in c[:3])+(255,)
        shell(sub, scr, t["lo"], t["hi"], t["mid"], n=3)
        im.alpha_composite(sub, (b, b))
        outer = chamfer_mask(W, RADAR_H, 10)
        for y in range(RADAR_H):
            for x in range(W):
                if not outer[y,x]: P(im)[x,y] = (0,0,0,0)
        shell(im, outer, t["hi"], t["lo"], t["mid"], n=3)
        px = P(im)
        for (cx, cy) in ((b+18,b+18),(W-b-19,b+18),(b+18,RADAR_H-b-19),(W-b-19,RADAR_H-b-19)):
            for r in range(5):
                for a in range(0, 360, 30):
                    X = int(cx + r*math.cos(math.radians(a))); Y = int(cy + r*math.sin(math.radians(a)))
                    if 0 <= X < W and 0 <= Y < RADAR_H:
                        c = px[X, Y]
                        px[X, Y] = lerp(c[:3], t["accent"], 0.30*(1-r/5)) + (255,)
        return im
    b = 16                                              # slab
    im = new(W, RADAR_H, t["face"]); px = P(im)
    vgrad(im, (0,0,W-1,RADAR_H-1), t["gtop"], t["gbot"], dither=False)
    bevel(im, (0,0,W-1,RADAR_H-1), t["hi"], t["lo"], n=3, out=True)
    for y in range(b, RADAR_H-b):
        for x in range(b, W-b): px[x,y] = t["screen"]+(255,)
    bevel(im, (b-4, b-4, W-b+3, RADAR_H-b+3), t["lo"], t["hi"], n=4, out=True)
    if with_coin: im.alpha_composite(fit(art_hi(K.emblem(faction), W-2*b-8, int((W-2*b-8)*69/80)),
                           W-2*b, RADAR_H-2*b), (b, b))
    return im

# ---------------------------------------------------------------- cell 64x48
def cell(t, empty):
    s = t["style"]; im = new(CELL_W, CELL_H); px = P(im)
    if s == "machined":
        m = chamfer_mask(CELL_W, CELL_H, 6)
        if empty:
            fill_mask(im, m, t["well"]); vgrad(im, (0,0,CELL_W-1,CELL_H-1), t["well"], t["lo2"])
            for y in range(CELL_H):
                for x in range(CELL_W):
                    if not m[y,x]: px[x,y] = (0,0,0,0)
        shell(im, m, t["lo"], t["hi"], t["mid"], n=2)
    elif s == "reticle":
        if empty:
            for y in range(3, CELL_H-3):
                for x in range(3, CELL_W-3): px[x,y] = t["well"]+(255,)
        L = 11
        for (cx, cy, sx, sy) in ((0,0,1,1),(CELL_W-1,0,-1,1),(0,CELL_H-1,1,-1),(CELL_W-1,CELL_H-1,-1,-1)):
            for i in range(L):
                px[cx+sx*i, cy] = t["hi2"]+(255,); px[cx, cy+sy*i] = t["hi2"]+(255,)
            for i in range(L-4):
                px[cx+sx*i, cy+sy] = t["lo2"]+(255,); px[cx+sx, cy+sy*i] = t["lo2"]+(255,)
    elif s == "rugged":
        if empty:
            for y in range(5, CELL_H-5):
                for x in range(5, CELL_W-5): px[x,y] = t["well"]+(255,)
        for k in range(4):
            c = t["lo"][min(k,2)]; d = t["hi"][min(k,2)] if k else t["mid"]
            for x in range(k, CELL_W-k): px[x,k] = c+(255,); px[x,CELL_H-1-k] = d+(255,)
            for y in range(k, CELL_H-k): px[k,y] = c+(255,); px[CELL_W-1-k,y] = d+(255,)
        for x in range(6, CELL_W-6): px[x, CELL_H-5] = t["hi2"]+(255,)
    elif s == "crt":
        if empty:
            for y in range(2, CELL_H-2):
                for x in range(2, CELL_W-2): px[x,y] = t["well"]+(255,)
        for k in range(2):
            for x in range(k, CELL_W-k): px[x,k] = t["hi"][k+1]+(255,); px[x,CELL_H-1-k] = t["lo"][k]+(255,)
            for y in range(k, CELL_H-k): px[k,y] = t["hi"][k+1]+(255,); px[CELL_W-1-k,y] = t["lo"][k]+(255,)
        for (cx, cy, sx, sy) in ((0,0,1,1),(CELL_W-1,0,-1,1),(0,CELL_H-1,1,-1),(CELL_W-1,CELL_H-1,-1,-1)):
            for i in range(7):
                px[cx+sx*i, cy] = t["accent"]+(255,); px[cx, cy+sy*i] = t["accent"]+(255,)
            px[cx,cy] = (0,0,0,0)
    else:
        if empty:
            for y in range(6, CELL_H-6):
                for x in range(6, CELL_W-6): px[x,y] = t["well"]+(255,)
        for k in range(5):
            c = t["lo"][min(k,2)]; d = t["hi"][min(k,2)] if k < 2 else t["mid"]
            for x in range(k, CELL_W-k): px[x,k] = c+(255,); px[x,CELL_H-1-k] = d+(255,)
            for y in range(k, CELL_H-k): px[k,y] = c+(255,); px[CELL_W-1-k,y] = d+(255,)
    return im

# ---------------------------------------------------------------- arrows 32x24
def chev(im, cx, cy, half, thick, up, c, edge=None):
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
    for col, side in ((hi,1),(lo,-1)):
        if not col: continue
        for r in range(half):
            y = cy - r if up else cy + r
            for d in range(2):
                x = cx-(half-r)+1+d if side == 1 else cx+(half-r)-1-d
                if 0 <= x < im.width and 0 <= y < im.height: px[x,y] = col+(255,)

def arrow(t, up, pressed=False):
    s = t["style"]; im = new(ARROW_W, ARROW_H); px = P(im); o = 2 if pressed else 0
    apex_up, apex_dn = 5, 18
    if s == "machined":
        m = chamfer_mask(ARROW_W, ARROW_H, 5)
        fill_mask(im, m, t["face"]); vgrad(im, (0,0,ARROW_W-1,ARROW_H-1), t["gtop"], t["gbot"])
        for y in range(ARROW_H):
            for x in range(ARROW_W):
                if not m[y,x]: px[x,y] = (0,0,0,0)
        shell(im, m, t["hi"], t["lo"], t["mid"], n=2)
        chev(im, 15+o, (apex_up if up else apex_dn)+o, 8, 4, up, t["lo1"], t["hi2"])
    elif s == "reticle":
        for (cx, cy, sx, sy) in ((0,0,1,1),(ARROW_W-1,0,-1,1),(0,ARROW_H-1,1,-1),(ARROW_W-1,ARROW_H-1,-1,-1)):
            for i in range(8):
                px[cx+sx*i, cy] = t["mid"]+(255,); px[cx, cy+sy*i] = t["mid"]+(255,)
        chev(im, 15+o, (apex_up if up else apex_dn)+o, 8, 4, up, t["accent"])
    elif s == "rugged":
        for k in range(3):
            for x in range(k, ARROW_W-k): px[x,k] = t["lo"][k]+(255,); px[x,ARROW_H-1-k] = t["hi"][k]+(255,)
            for y in range(k, ARROW_H-k): px[k,y] = t["lo"][k]+(255,); px[ARROW_W-1-k,y] = t["hi"][k]+(255,)
        vgrad(im, (3,3,ARROW_W-4,ARROW_H-4), t["gtop"], t["gbot"])
        tri(im, 16+o, (ARROW_H-6 if up else 5)+o, 10, up, t["lo1"], t["hi1"], t["lo2"])
    elif s == "crt":
        m = chamfer_mask(ARROW_W, ARROW_H, 5)
        fill_mask(im, m, t["face"]); vgrad(im, (0,0,ARROW_W-1,ARROW_H-1), t["gtop"], t["gbot"])
        for y in range(ARROW_H):
            for x in range(ARROW_W):
                if not m[y,x]: px[x,y] = (0,0,0,0)
        shell(im, m, t["hi"], t["lo"], t["mid"], n=2)
        chev(im, 15+o, (3 if up else 20)+o, 6, 3, up, t["accent"])
        chev(im, 15+o, (11 if up else 12)+o, 6, 3, up, t["hi2"])
    else:
        for k in range(3):
            for x in range(k, ARROW_W-k): px[x,k] = t["hi"][k]+(255,); px[x,ARROW_H-1-k] = t["lo"][k]+(255,)
            for y in range(k, ARROW_H-k): px[k,y] = t["hi"][k]+(255,); px[ARROW_W-1-k,y] = t["lo"][k]+(255,)
        vgrad(im, (3,3,ARROW_W-4,ARROW_H-4), t["gtop"], t["gbot"], dither=False)
        tri(im, 16+o, (ARROW_H-5 if up else 4)+o, 13, up, t["lo1"], t["hi2"], t["lo2"])
    return im

# ---------------------------------------------------------------- buttons
def button(t, label, w):
    s = t["style"]; h = BTN_H; im = new(w, h); px = P(im)
    m = LABEL[label]; gx = (w - m.shape[1])//2; gy = (h - m.shape[0])//2
    if s == "reticle":
        for (cx, cy, sx, sy) in ((0,0,1,1),(w-1,0,-1,1),(0,h-1,1,-1),(w-1,h-1,-1,-1)):
            for i in range(7):
                px[cx+sx*i, cy] = t["mid"]+(255,); px[cx, cy+sy*i] = t["mid"]+(255,)
        stamp(im, m, gx, gy, t["ink"]+(255,))
        return im
    mk = chamfer_mask(w, h, 4) if s in ("machined","crt") else np.ones((h,w), bool)
    fill_mask(im, mk, t["face"]); vgrad(im, (0,0,w-1,h-1), t["gtop"], t["gbot"])
    if s == "machined": brushed(im, (2,2,w-3,h-3), t, 5)
    for y in range(h):
        for x in range(w):
            if not mk[y,x]: px[x,y] = (0,0,0,0)
    shell(im, mk, t["hi"], t["lo"], t["mid"], n=2)
    if s == "crt":
        stamp(im, m, gx, gy, t["ink"]+(255,), t["lo2"]+(255,))
    else:
        stamp(im, m, gx, gy+1, t["hi1"]+(255,)); stamp(im, m, gx, gy, t["lo2"]+(255,))
    return im

# ---------------------------------------------------------------- meter
def meter(t, level=0.55, mark=0.66):
    s = t["style"]; h = METER_Y1 - METER_Y0; im = new(METER_W, h, t["face"]); px = P(im)
    top = int(h*(1-level))
    if s == "reticle":
        im = new(METER_W, h); px = P(im)
        for y in range(h):
            for x in range(7, 11): px[x,y] = (t["accent"] if y>=top else t["lo1"])+(255,)
        for y in range(0, h, 16):
            for x in range(2, 6): px[x,y] = t["mid"]+(255,)
        for y in range(0, h, 48):
            for x in range(0, 6): px[x,y] = t["hi2"]+(255,)
    elif s == "rugged":
        vgrad(im, (0,0,METER_W-1,h-1), t["gtop"], t["gbot"])
        bevel(im, (0,0,METER_W-1,h-1), t["lo"], t["hi"], n=2, out=True)
        seg = 10
        for i in range(h//seg):
            y0 = i*seg; on = y0 >= top
            for y in range(y0+2, min(y0+seg, h)):
                for x in range(3, METER_W-3):
                    px[x,y] = (t["accent"] if on else t["well"])+(255,)
            for x in range(3, METER_W-3):
                px[x,y0] = t["lo2"]+(255,); px[x,y0+1] = t["lo1"]+(255,)
    else:
        vgrad(im, (0,0,METER_W-1,h-1), t["gtop"], t["gbot"])
        bevel(im, (0,0,METER_W-1,h-1), t["lo"], t["hi"], n=3, out=True)
        for y in range(h):
            for x in range(4, METER_W-4):
                c = t["accent"] if y>=top else t["well"]
                if s == "crt" and y % 3 == 0: c = tuple(int(v*0.7) for v in c)
                px[x,y] = c+(255,)
        bevel(im, (3,3,METER_W-4,h-4), t["lo"], t["hi"], n=1, out=True)
        for y in range(0, h, 12):
            for x in range(4, METER_W-4):
                px[x,y] = tuple(int(v*0.55) for v in px[x,y][:3])+(255,)
    my = int(h*(1-mark))
    for k in range(3):
        for x in range(METER_W):
            if 0 <= my+k < h: px[x, my+k] = (t["accent2"] if k < 2 else t["lo2"])+(255,)
    return im

# ---------------------------------------------------------------- assembly
def build(t, faction="GDI", with_coin=True, with_cameos=True):
    im = new(W, H, t["face"]); px = P(im)
    vgrad(im, (0,0,W-1,H-1), t["gtop"], t["gbot"], dither=(t["style"]!="slab"))
    im.alpha_composite(header(t), (0, TAB_Y))
    for x in range(W):
        px[x, SEP_Y] = t["lo2"]+(255,)
    im.alpha_composite(radar(t, faction, with_coin), (0, RADAR_Y))
    for x0, x1, lab in BTN_BOXES:
        im.alpha_composite(button(t, lab, x1-x0+1), (x0, BTN_Y))
    im.alpha_composite(meter(t), (METER_X, METER_Y0))
    empty, frame = cell(t, True), cell(t, False)
    names_l = K.CAMEOS_L + ["HAND"]
    names_r = K.CAMEOS_R + ["APC"]
    for col, names in zip(COL_X, (names_l, names_r)):
        for row, nm in zip(ROW_Y, names):
            im.alpha_composite(empty, (col, row))
            if with_cameos: im.alpha_composite(art_hi(K.cameo(nm), CELL_W, CELL_H), (col, row))
            im.alpha_composite(frame, (col, row))
    for x, d in ARROW_XS:
        im.alpha_composite(arrow(t, d == "up"), (x, ARROW_Y))
    return im
