#!/usr/bin/env python3
"""LOOK AT a cartridge display list. Renders one F3DEX list to a PNG.

Written to answer "what IS model-table slot N?" by eye rather than by triangle
count. It needs no texture bank and no pack: geometry comes from
support/vx_rdp.walk over the display list, texels from vx_rdp.decode over the
tile descriptor the list sets for itself, and CI textures are looked up through
the cartridge's global TLUT at ROM 0x99130. Untextured faces fall back to their
own vertex colour, which for a list that runs under G_LIGHTING is a packed unit
normal and therefore reads green; that is the data, not a bug in this file.

    python3 render_dl.py --slot 205 -o /tmp/slot205.png
    python3 render_dl.py --dl 0x012DBC8 --angles 0,89 90,0 0,0 40,22
    python3 render_dl.py --slot 53 --slot 205 -o /tmp/two.png

One world cell is 1024 mesh units, so the printed extent doubles as a size check.
That number is the RENDERER's, not a guess: game/cnc_eyes.cpp sets
MODEL_SCALE = 1.0f / 1024.0f, and the independent confirmation is a wall straight,
authored x[-500,+501], i.e. 1001 units for the one cell it occupies. This file said 256
until 2 Sep 2026 and every extent it printed was therefore four times too large; the
tell was that it made the flying Orca four cells long.
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "support"))
import objgraph2 as O2          # noqa: E402
import vx_rdp as VX             # noqa: E402

CELL = 1024.0

# rom = ram - base, for the segments a model display list can reach.
SEGS = [(0x00C6EA0, 0x00DA420, 0x80122B60),      # ScriptModels
        (0x00DA420, 0x00FE210, 0x801369B0),      # GameModelsTextures
        (0x00FE210, 0x01643F0, 0x8015A7A0),      # GameModelsGeometry
        (0x01643F0, 0x01B67D0, 0x801C2600)]      # CodeOverlay


def ram_to_rom(ram):
    for rs, re_, ra in SEGS:
        if ra <= ram < ra + (re_ - rs):
            return ram - ra + rs
    return None


class Art(object):
    def __init__(self):
        self.rom = O2.rom()
        self.tlut = VX.read_tlut(self.rom, 0x99130)
        self.cache = {}

    def texture(self, st):
        key = (st["timg"], st["fmt"], st["siz"], st["lrs"], st["lrt"])
        if key in self.cache:
            return self.cache[key]
        img = None
        off = ram_to_rom(st["timg"]) if st["timg"] else None
        w, h = st["lrs"] // 4 + 1, st["lrt"] // 4 + 1
        if off is not None and 0 < w <= 256 and 0 < h <= 256:
            try:
                import numpy as np
                img = np.array(VX.decode(self.rom, off, w, h, st["fmt"],
                                         st["siz"], tlut=self.tlut),
                               dtype=np.uint8).reshape(h, w, 4)
            except Exception:
                img = None
        self.cache[key] = img
        return img


def geometry(dl_rom):
    """(positions, uvs, colours, triangles) straight out of the display list."""
    import numpy as np
    verts, tris = VX.walk(O2.rom(), dl_rom, VX.make_resolve(dl_rom))
    P = np.array([[v[0], v[1], v[2]] for v in verts], float).reshape(-1, 3)
    UV = np.array([[v[3], v[4]] for v in verts], float).reshape(-1, 2)
    C = np.array([[v[5], v[6], v[7]] for v in verts], float).reshape(-1, 3)
    return P, UV, C, tris


def _rot(V, az, el):
    import numpy as np
    a, e = math.radians(az), math.radians(el)
    ca, sa, ce, se = math.cos(a), math.sin(a), math.cos(e), math.sin(e)
    x = V[:, 0] * ca + V[:, 2] * sa
    zc = -V[:, 0] * sa + V[:, 2] * ca
    return np.c_[x, V[:, 1] * ce - zc * se, V[:, 1] * se + zc * ce]


def render(dl_rom, az, el, size=420, art=None, frame=None):
    """A z-buffered, texture-mapped image of one display list, as a HxWx3 array."""
    import numpy as np
    art = art or Art()
    V, UV, C, tris = geometry(dl_rom)
    if not len(V) or not tris:
        return np.full((size, size, 3), 246, np.uint8)
    P = _rot(V, az, el)
    ref = P if frame is None else _rot(frame, az, el)
    lo, hi = ref[:, :2].min(0), ref[:, :2].max(0)
    ctr = (lo + hi) / 2.0
    r = max(hi[0] - lo[0], hi[1] - lo[1]) * 0.58 + 1e-6
    sx = (P[:, 0] - (ctr[0] - r)) / (2 * r) * size
    sy = size - (P[:, 1] - (ctr[1] - r)) / (2 * r) * size
    img = np.full((size, size, 3), 246, np.uint8)
    zbuf = np.full((size, size), -1e18)
    L = np.array([0.4, 0.8, 0.45])
    L /= np.linalg.norm(L)
    for (i0, i1, i2, st) in tris:
        idx = (i0, i1, i2)
        x = np.array([sx[i] for i in idx])
        y = np.array([sy[i] for i in idx])
        z = np.array([P[i, 2] for i in idx])
        x0, x1 = max(int(x.min()), 0), min(int(x.max()) + 1, size)
        y0, y1 = max(int(y.min()), 0), min(int(y.max()) + 1, size)
        if x0 >= x1 or y0 >= y1:
            continue
        den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        b0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
        b1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
        b2 = 1.0 - b0 - b1
        m = (b0 >= -1e-6) & (b1 >= -1e-6) & (b2 >= -1e-6)
        if not m.any():
            continue
        zz = b0 * z[0] + b1 * z[1] + b2 * z[2]
        near = zbuf[y0:y1, x0:x1]
        m &= zz > near
        if not m.any():
            continue
        a, b, c = V[i0], V[i1], V[i2]
        n = np.cross(b - a, c - a)
        ln = np.linalg.norm(n)
        shade = 0.45 + 0.55 * abs(float(n @ L) / ln) if ln > 0 else 0.6
        T = art.texture(st)
        if T is not None:
            th, tw = T.shape[:2]
            u = (b0 * UV[i0, 0] + b1 * UV[i1, 0] + b2 * UV[i2, 0]) / 32.0
            v = (b0 * UV[i0, 1] + b1 * UV[i1, 1] + b2 * UV[i2, 1]) / 32.0
            col = T[np.clip(np.round(v).astype(int), 0, th - 1),
                    np.clip(np.round(u).astype(int), 0, tw - 1), :3].astype(float)
        else:
            col = b0[..., None] * C[i0] + b1[..., None] * C[i1] + b2[..., None] * C[i2]
        col = np.clip(col * shade, 0, 255)
        img[y0:y1, x0:x1][m] = col[m].astype(np.uint8)
        near[m] = zz[m]
    return img


def dl_of_slot(slot):
    parts = [p for p in O2.parts(slot) if p.get("gfx_rom")]
    if not parts:
        raise SystemExit("model-table slot %d has no geometry" % slot)
    return parts[0]["gfx_rom"]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--slot", action="append", type=int, default=[],
                    help="model-table slot (repeatable; one row per slot)")
    ap.add_argument("--dl", action="append", default=[],
                    help="display-list ROM offset, e.g. 0x012DBC8 (repeatable)")
    ap.add_argument("--angles", nargs="+", default=["0,89", "90,0", "0,0", "40,22"],
                    help="az,el pairs (default: top, side, front, three quarter)")
    ap.add_argument("--size", type=int, default=420)
    ap.add_argument("-o", "--out", default="dl.png")
    a = ap.parse_args()

    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rows = [(("slot %d" % s), dl_of_slot(s)) for s in a.slot]
    rows += [(("dl_%07X" % int(d, 0)), int(d, 0)) for d in a.dl]
    if not rows:
        raise SystemExit("give at least one --slot or --dl")
    angles = [tuple(float(x) for x in p.split(",")) for p in a.angles]

    art = Art()
    fig, axs = plt.subplots(len(rows), len(angles),
                            figsize=(3.6 * len(angles), 3.9 * len(rows)),
                            dpi=110, squeeze=False)
    for r, (label, dl) in enumerate(rows):
        V, _, _, tris = geometry(dl)
        ext = V.max(0) - V.min(0) if len(V) else np.zeros(3)
        head = ("%s  dl_%07X\n%d verts %d tris   %.0f x %.0f x %.0f u"
                "  = %.2f x %.2f x %.2f cells"
                % (label, dl, len(V), len(tris), ext[0], ext[1], ext[2],
                   ext[0] / CELL, ext[1] / CELL, ext[2] / CELL))
        print(head.replace("\n", "   "))
        for c, (az, el) in enumerate(angles):
            ax = axs[r][c]
            ax.imshow(render(dl, az, el, a.size, art))
            ax.axis("off")
            ax.set_title((head + "\n" if c == 0 else "")
                         + "az %g  el %g" % (az, el),
                         fontsize=8, family="monospace")
    fig.tight_layout()
    fig.savefig(a.out)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
