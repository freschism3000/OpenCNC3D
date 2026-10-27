#!/usr/bin/env python3
"""The Red Alert title without its lettering, for the Enhanced menu backdrop.

The title (menu/art/ra3d-title-win95.png, the HIRES TITLE.PCX, and menu/art/ra3d-title.png,
the LORES TITLE.CPS, both with the RA3D lettering) has the lettering plate over the top of
its screen circle. This lifts it off, by hand-set rules rather than a generic fill, so no
trace of the lettering is left:

- inside the circle, the screen and its grid: the screen's own colour, and the grid's lines
  at their pitch and phase, in the tones sampled from the visible grid (the HIRES grid
  carries a dim pixel beside each line; the LORES grid does not);
- under the 3D badge, the red panel's top edge and the top of its inside: the rows of a
  clean column of that edge, with the panel's vertical grid lines put back at their columns;
- over the bezel ring, the ring itself: each radius's masked arc is filled from the arc
  below and the arc above it, rotated in about the circle's centre and crossfaded across
  the gap; only steel is sampled (never the Westwood plate or the grid's red);
- right of the ring, the speaker grille: its slats are horizontal, so each masked pixel
  takes the slat further along its own row.

Writes title-backdrop-hi.png (640x400) and title-backdrop-lo.png (320x200) beside this
script: the plates bake_ra_enhanced_menu.py packs."""
import math, os
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
ART = os.path.join(ROOT, 'menu', 'art')
# The lettering plate's outline at HIRES pixels (the C&C wings, RED ALERT, the 3D badge).
POLY = [(221, 13), (406, 13), (412, 40), (430, 41), (517, 55), (520, 63), (508, 84), (503, 121),
        (375, 141), (321, 151), (266, 141), (138, 121), (129, 85), (127, 63), (131, 55), (208, 41), (221, 40)]

DENSITY = {
    'hi': dict(src='ra3d-title-win95.png', k=1.0, grow=2,
               circle=(318.5, 205.2, 210.1),          # the screen's black, fitted to its visible edge
               screen=(0, 0, 0), panel_top=137,
               vpitch=42, vphase=152, hpitch=42, hphase=0,
               vline=(76, 1, 0), vdim=(12, 0, 0), hline=(68, 2, 0), hdim=(38, 1, 0),
               panel_col=196, panel_line=194, panel_x=(168, 472), strip=(137, 158, 240, 400),
               grille=(497, 49, 0.77, 26)),
    'lo': dict(src='ra3d-title.png', k=0.5, grow=1,
               circle=(158.7, 101.0, 105.6),
               screen=(31, 32, 42), panel_top=67,
               vpitch=22, vphase=5, hpitch=19, hphase=5,
               vline=(82, 5, 0), vdim=None, hline=(82, 5, 0), hdim=None,
               panel_col=100, panel_line=93, panel_x=(84, 236), strip=(66, 81, 118, 202),
               grille=(249, 24, 0.77, 13)),
}

def grown_mask(k, grow, size):
    m = Image.new('L', size, 0); ImageDraw.Draw(m).polygon([(x * k, y * k) for x, y in POLY], fill=255)
    a = np.array(m) > 0; g = a.copy()
    for dy in range(-grow, grow + 1):
        for dx in range(-grow, grow + 1):
            g |= np.roll(np.roll(a, dy, 0), dx, 1)
    return g

def clean(im, P):
    im = im.astype(np.float32); H, W, _ = im.shape
    mk = grown_mask(P['k'], P['grow'], (W, H)); out = im.copy()
    cx, cy, r = P['circle']
    yy, xx = np.mgrid[0:H, 0:W]; d = np.hypot(xx - cx, yy - cy)
    field = mk & (d < r - 1.0) & (yy < P['panel_top'])
    out[field] = P['screen']
    vl = (xx - P['vphase']) % P['vpitch'] == 0; hl = ((yy - P['hphase']) % P['hpitch'] == 0) & (yy > 0)
    if P['vdim']:
        out[field & (((xx - P['vphase'] - 1) % P['vpitch'] == 0) | ((xx - P['vphase'] + 1) % P['vpitch'] == 0))] = P['vdim']
    if P['hdim']:
        out[field & ((yy - P['hphase'] - 1) % P['hpitch'] == 0) & (yy > 1)] = P['hdim']
    out[field & hl] = P['hline']
    out[field & vl] = P['vline']
    # the panel's top edge and the top of its inside, under the 3D badge; the badge's rim
    # and shadow fall a few rows past its outline here, so the strip under it is redrawn whole
    y0, y1, x0, x1 = P['strip']; px0, px1 = P['panel_x']
    panel = (mk & (yy >= P['panel_top']) & (xx > px0) & (xx < px1)) | ((yy >= y0) & (yy < y1) & (xx > x0) & (xx < x1))
    out[panel] = im[yy[panel], P['panel_col']]
    for k in (-1, 0, 1):
        sel = panel & ((xx - P['panel_line'] - k) % P['vpitch'] == 0)
        out[sel] = im[yy[sel], P['panel_line'] + k]
    # the grille, right of the ring
    gx, gy, gs, gshift = P['grille']
    grille = mk & ~field & ~panel & (xx > gx + (yy - gy) * gs) & (xx < W - gshift)
    out[grille] = im[yy[grille], xx[grille] + gshift]
    # the ring: per radius, crossfade the arcs either side of the masked one
    ring = mk & ~field & ~panel & ~grille
    ang = np.arctan2(yy - cy, xx - cx); dr = np.round(d).astype(int)
    def steel(c): return abs(c[0] - c[1]) < 14 and abs(c[1] - c[2]) < 14
    for side in (-1, 1):
        sel = ring & ((xx < cx) if side < 0 else (xx >= cx))
        for rr in np.unique(dr[sel]):
            band = sel & (dr == rr)
            lo, hi = ang[band].min(), ang[band].max()
            span = hi - lo + math.radians(1.5)
            for y, x in zip(*np.where(band)):
                t = (ang[y, x] - lo) / max(1e-6, hi - lo)
                def sample(rot):
                    th = ang[y, x] + rot
                    ix = int(round(cx + d[y, x] * math.cos(th))); iy = int(round(cy + d[y, x] * math.sin(th)))
                    if 0 <= ix < W and 0 <= iy < H and not mk[iy, ix] and steel(im[iy, ix]): return im[iy, ix]
                    return None
                a1 = sample(-span * t - math.radians(0.75)); a2 = sample(span * (1 - t) + math.radians(0.75))
                if a1 is None and a2 is None: continue
                out[y, x] = a2 if a1 is None else a1 if a2 is None else a1 * (1 - t) + a2 * t
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)

def main():
    for name, P in DENSITY.items():
        im = np.array(Image.open(os.path.join(ART, P['src'])).convert('RGB'))
        path = os.path.join(HERE, 'title-backdrop-%s.png' % name)
        Image.fromarray(clean(im, P)).save(path); print('wrote', path)

if __name__ == '__main__':
    main()
