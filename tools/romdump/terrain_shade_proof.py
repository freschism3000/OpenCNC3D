#!/usr/bin/env python3
"""
CNC3D -- proof renderer for the recovered N64 terrain shade (READ-ONLY).

Not part of the game. A small software rasteriser that draws a scenario's heightfield
with the map's own 24x24 theater art twice -- once at glColor(1,1,1) (what the GL build
ships today) and once at glColor(shade/255) using the constants terrain_shade.py
recovered from the cartridge -- so the two can be looked at side by side.

It deliberately mirrors the renderer it is arguing about:
  * cell tessellation = the console's own (G_VTX loads NW,SW,NE,SE and G_TRI2 draws
    (0,1,2)+(2,1,3), so the shared diagonal is SW-NE),
  * camera = docs/n64-camera-math.md (yaw 0, pitch tied to D, fovy 50, aspect 4:3),
  * texturing = GL_MODULATE equivalent, texel * vertex colour, Gouraud interpolated.

Usage:
    python3 terrain_shade_proof.py [SCEN] [camX camZ D] [-o OUT.png]
    python3 terrain_shade_proof.py SCG01EA 41.5 45 2400 -o proof.png

Needs numpy + Pillow. Reads only the ROM and tools/bakery/.../terrain_<SCEN>.png.
"""
import math, os, sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import terrain_shade as TS

ART = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "terrain")
W, H = 960, 720
ASPECT = 4.0 / 3.0
FOCAL = 1.0 / math.tan(math.radians(50.0) / 2.0)


def camera(camX, camZ, D_leptons):
    return camX, camZ, D_leptons / 256.0, 0.78 + 0.14 * (D_leptons - 2400.0) / 1400.0


def project(wx, wy, wz, cam):
    camX, camZ, D, p = cam
    ax, az = wx - camX, wz - camZ
    sp, cp = math.sin(p), math.cos(p)
    ye = cp - az * sp + wy * sp
    depth = D - sp - az * cp - wy * cp
    return (W / 2.0 * (1.0 + ax * FOCAL / (ASPECT * depth)),
            H / 2.0 * (1.0 - ye * FOCAL / depth),
            depth)


def raster(tris, tex, shaded):
    img = np.zeros((H, W, 3), np.float32)
    zb = np.full((H, W), 1e30, np.float32)
    th, tw = tex.shape[0], tex.shape[1]
    for (x0, y0, z0, u0, v0, s0), (x1, y1, z1, u1, v1, s1), (x2, y2, z2, u2, v2, s2) in tris:
        minx = max(0, int(math.floor(min(x0, x1, x2))))
        maxx = min(W - 1, int(math.ceil(max(x0, x1, x2))))
        miny = max(0, int(math.floor(min(y0, y1, y2))))
        maxy = min(H - 1, int(math.ceil(max(y0, y1, y2))))
        if minx > maxx or miny > maxy:
            continue
        d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(d) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(minx, maxx + 1, dtype=np.float32) + 0.5,
                             np.arange(miny, maxy + 1, dtype=np.float32) + 0.5)
        l0 = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / d
        l1 = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / d
        l2 = 1.0 - l0 - l1
        m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not m.any():
            continue
        iw = l0 / z0 + l1 / z1 + l2 / z2
        zz = 1.0 / np.where(iw == 0, 1e-9, iw)
        uu = (l0 * u0 / z0 + l1 * u1 / z1 + l2 * u2 / z2) * zz
        vv = (l0 * v0 / z0 + l1 * v1 / z1 + l2 * v2 / z2) * zz
        ss = (l0 * s0 / z0 + l1 * s1 / z1 + l2 * s2 / z2) * zz
        sub = zb[miny:maxy + 1, minx:maxx + 1]
        m &= zz < sub
        if not m.any():
            continue
        texel = tex[np.clip(vv.astype(np.int32), 0, th - 1),
                    np.clip(uu.astype(np.int32), 0, tw - 1)]
        col = texel * ss[..., None] if shaded else texel
        img[miny:maxy + 1, minx:maxx + 1][m] = col[m]
        sub[m] = zz[m]
    return np.clip(img, 0, 255).astype(np.uint8)


def build(scen, camX, camZ, D, shaded, white=False, rom=None):
    heights, _cm, _meta = TS.load_maps(rom or TS.DEFAULT_ROM, scen)
    shade = TS.shade_grid(heights)
    if white:
        tex = np.full((1536, 1536, 3), 255.0, np.float32)
    else:
        tex = np.asarray(Image.open(os.path.join(ART, f"terrain_{scen}.png")).convert("RGB"),
                         dtype=np.float32)
    flat = sorted(heights[y][x] for y in range(65) for x in range(65))
    base = flat[len(flat) // 2] / 64.0
    cam = camera(camX, camZ, D)

    def vert(cx, cz):
        c, r, dep = project(float(cx), heights[cz][cx] / 64.0 - base, float(cz), cam)
        return c, r, dep, shade[cz][cx] / 255.0

    tris = []
    for cz in range(64):
        for cx in range(64):
            nw, sw = vert(cx, cz), vert(cx, cz + 1)
            ne, se = vert(cx + 1, cz), vert(cx + 1, cz + 1)
            if min(nw[2], sw[2], ne[2], se[2]) <= 0.05:
                continue
            u0, u1 = cx * 24.0 + 0.01, cx * 24.0 + 23.99
            v0, v1 = cz * 24.0 + 0.01, cz * 24.0 + 23.99
            P = ((nw[0], nw[1], nw[2], u0, v0, nw[3]),
                 (sw[0], sw[1], sw[2], u0, v1, sw[3]),
                 (ne[0], ne[1], ne[2], u1, v0, ne[3]),
                 (se[0], se[1], se[2], u1, v1, se[3]))
            tris.append((P[0], P[1], P[2]))          # NW, SW, NE
            tris.append((P[2], P[1], P[3]))          # NE, SW, SE
    return raster(tris, tex, shaded)


def sheet(scen, views, out, crop=None, zoom=1):
    rows = []
    for camX, camZ, D, label in views:
        a = build(scen, camX, camZ, D, False)
        b = build(scen, camX, camZ, D, True)
        c = build(scen, camX, camZ, D, True, white=True)
        ims = [Image.fromarray(x) for x in (a, b, c)]
        if crop:
            ims = [im.crop(crop) for im in ims]
        if zoom != 1:
            ims = [im.resize((im.width * zoom, im.height * zoom), Image.NEAREST) for im in ims]
        rows.append((label, ims))
    iw, ih = rows[0][1][0].size
    pad, head = 10, 46
    sh = Image.new("RGB", (iw * 3 + pad * 4, (ih + head) * len(rows) + pad),
                   (18, 18, 22))
    d = ImageDraw.Draw(sh)
    caps = ["A  UNSHADED - what the GL build ships today (glColor 1,1,1)",
            "B  SHADED - glColor = shade/255, constants read out of the cartridge",
            "C  the shade term alone (white texture)"]
    for r, (label, ims) in enumerate(rows):
        y = pad + r * (ih + head)
        d.text((pad, y), f"{scen}  {label}", fill=(240, 220, 150))
        for i, im in enumerate(ims):
            sh.paste(im, (pad + i * (iw + pad), y + head - 16))
            d.text((pad + i * (iw + pad), y + 20), caps[i], fill=(215, 215, 225))
    sh.save(out)
    return out


if __name__ == "__main__":
    args = [a for a in sys.argv[1:]]
    out = "terrain_shade_proof.png"
    crop, zoom = (300, 140, 780, 500), 2
    if "-o" in args:
        i = args.index("-o"); out = args[i + 1]; del args[i:i + 2]
    if "--crop" in args:
        i = args.index("--crop"); crop = tuple(int(v) for v in args[i + 1].split(",")); del args[i:i + 2]
    if "--zoom" in args:
        i = args.index("--zoom"); zoom = int(args[i + 1]); del args[i:i + 2]
    scen = args[0] if args else "SCG01EA"
    if len(args) >= 4:
        views = [(float(args[1]), float(args[2]), float(args[3]), "")]
    else:
        views = [(41.5, 45.0, 2400.0, "cam 41.5,45 D2400 - the diagonal rock ridge"),
                 (45.0, 53.0, 2400.0, "cam 45,53 D2400 - the SLOPE bowl at 43-48 x 52-55")]
    print("wrote", sheet(scen, views, out, crop=crop, zoom=zoom))
