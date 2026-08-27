"""Faceted material renderer for the Machined sidebar.

The previous pass lit every boundary pixel by "is my neighbour empty", which is
pillow shading: a uniform glow round the outline, and the single clearest tell of
programmer art. Real pixel art reads as FLAT FACETS at distinct ramp steps, with a
consistent light direction.

So: recover the outward surface normal from the shape itself, light it once, and
quantise into a hand-authored hue-shifted ramp. A long top edge then gets ONE tone
across its whole length instead of a gradient, which is the thing that makes it look
built rather than generated."""
import numpy as np, math
from PIL import Image

# ---------------------------------------------------------------- ramps
# Hand-picked, hue-shifted: shadows rotate toward violet-blue and gain saturation,
# highlights desaturate and cool off. Even steps would look like a computer made it.
def R(*hexes): return [tuple(int(h[i:i+2],16) for i in (0,2,4)) for h in hexes]

STEEL  = R("08090f","121626","1f2740","2f3a5a","44527a","5e6d95","8290b0","adbacd","d6e2ee","f4faff")
BRASS  = R("120c04","241a08","3d2c0e","5c4415","7d5f1c","a17d26","c69c33","e2bd52","f5da8a","fff3c8")
GREEN  = R("03110a","06200f","0a3417","104b20","17662b","1f8437","2aa344","4cc164","86dc92","c8f3cd")
AMBER  = R("1a0f02","301c05","4d2e08","6e440c","916012","b57f1c","d6a02b","eebf4d","fcda85","fff0c4")

LIGHT = np.array([-0.55, -0.66, 0.52]); LIGHT /= np.linalg.norm(LIGHT)
BAYER4 = np.array([[ 0, 8, 2,10],[12, 4,14, 6],[ 3,11, 1, 9],[15, 7,13, 5]]) / 16.0

# ---------------------------------------------------------------- geometry
def shift(a, dy, dx, fill=True):
    out = np.full_like(a, fill)
    h, w = a.shape
    ys0, ys1 = max(0, -dy), min(h, h - dy)
    xs0, xs1 = max(0, -dx), min(w, w - dx)
    if ys0 < ys1 and xs0 < xs1:
        out[ys0:ys1, xs0:xs1] = a[ys0+dy:ys1+dy, xs0+dx:xs1+dx]
    return out

def outward_normal(mask, maxr):
    """For each solid pixel: distance to the nearest empty pixel, and the unit XY
    direction pointing at it. That direction IS the outward surface normal, and it is
    constant along a straight edge, which is what kills the pillow look."""
    h, w = mask.shape
    bg = ~mask
    dist = np.full((h, w), float(maxr) + 1.0)
    nx = np.zeros((h, w)); ny = np.zeros((h, w))
    offs = [(dx, dy) for dy in range(-maxr, maxr+1) for dx in range(-maxr, maxr+1)
            if (dx or dy) and math.hypot(dx, dy) <= maxr]
    offs.sort(key=lambda o: o[0]*o[0] + o[1]*o[1])
    for dx, dy in offs:
        r = math.hypot(dx, dy)
        hit = mask & shift(bg, dy, dx, True) & (r < dist)
        dist[hit] = r; nx[hit] = dx / r; ny[hit] = dy / r
    return dist, nx, ny

LXY = np.array([-0.5, -0.62]); LXY /= np.linalg.norm(LXY)

def facet_shade(mask, bevel, ramp, base=4.55, spec=True, dither=0.0, brushed=0.0,
                brush_density=0.16, seed=3):
    """Facet tone comes from the surface DIRECTION alone, then steps toward the flat
    face across the bevel width. That gives a clean N-step edge using the full ramp,
    instead of a compressed lambert that leaves everything mid-grey."""
    h, w = mask.shape
    N = len(ramp)
    dist, nx, ny = outward_normal(mask, bevel + 1)
    t = np.clip((dist - 1.0) / max(1e-6, bevel), 0, 1)
    lam = nx*LXY[0] + ny*LXY[1]                       # -1 (facing away) .. +1 (into light)
    facet = (lam + 1.0) / 2.0 * (N - 1)
    idx = facet * (1 - t) + base * t
    if brushed:                                        # vertical striations, per column
        rng = np.random.RandomState(seed)
        col = (rng.rand(w) - 0.5) * 2.0 * brushed
        col[rng.rand(w) > brush_density] = 0.0
        idx = idx + np.where(t >= 0.999, col[None, :], 0.0)
    if dither:
        yy, xx = np.mgrid[0:h, 0:w]
        idx = idx + np.where(t >= 0.999, (BAYER4[yy % 4, xx % 4] - 0.5) * dither, 0.0)
    q = np.clip(np.rint(idx).astype(int), 0, N - 1)
    if spec:                                           # short hit on the up-light corners only
        lip = (dist <= 1.4) & (lam > 0.93)
        q[lip] = N - 1
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = np.array(ramp, np.uint8)[q]
    out[..., 3] = np.where(mask, 255, 0)
    return Image.fromarray(out, "RGBA")

# ---------------------------------------------------------------- shape helpers
def rect_mask(w, h, chamfer=0):
    m = np.ones((h, w), bool)
    for (cx, cy, sx, sy) in ((0,0,1,1),(w-1,0,-1,1),(0,h-1,1,-1),(w-1,h-1,-1,-1)):
        for i in range(chamfer):
            for j in range(chamfer - i): m[cy + sy*j, cx + sx*i] = False
    return m

def inset(mask, k):
    m = mask.copy()
    for _ in range(k):
        p = np.pad(m, 1)
        m = m & p[:-2,1:-1] & p[2:,1:-1] & p[1:-1,:-2] & p[1:-1,2:]
    return m

def plate(w, h, chamfer, ramp, bevel=3, base=4.55, dither=0.0, brushed=0.0,
          brush_density=0.16, outline=True, spec=True, mask=None):
    """A finished metal plate: hard dark outline, crisp facet steps, dithered face.
    The outline is what separates SNES-era UI art from a soft render."""
    m = rect_mask(w, h, chamfer) if mask is None else mask
    inner = inset(m, 1) if outline else m
    img = facet_shade(inner, bevel, ramp, base=base, spec=spec, dither=dither,
                      brushed=brushed, brush_density=brush_density)
    if outline:
        out = np.zeros((h, w, 4), np.uint8)
        out[..., :3] = np.array(ramp[0], np.uint8)
        out[..., 3] = np.where(m, 255, 0)
        base_img = Image.fromarray(out, "RGBA")
        base_img.alpha_composite(img)
        return base_img
    return img

def recess(w, h, chamfer, ramp, bevel=3, floor=1, mask=None):
    """An inset well: the bevel lights from the opposite side, floor sits dark."""
    m = rect_mask(w, h, chamfer) if mask is None else mask
    d, nx, ny = outward_normal(m, bevel + 1)
    t = np.clip((d - 1.0) / max(1e-6, bevel), 0, 1)
    ang = (1 - t) * (math.pi / 2)
    s, c = np.sin(ang), np.cos(ang)
    N = len(ramp)
    lam = -(nx*LXY[0] + ny*LXY[1])                    # inverted: a well lights opposite
    facet = (lam + 1.0) / 2.0 * (N - 1)
    idx = np.clip(np.rint(facet * (1 - t) + floor * t), 0, N-1).astype(int)
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = np.array(ramp, np.uint8)[idx]
    out[..., 3] = np.where(m, 255, 0)
    return Image.fromarray(out, "RGBA")
