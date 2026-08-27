#!/usr/bin/env python3
"""Perfectly looping spin GIF of a cartridge mesh.

The loop is exact by construction: N frames at i*360/N degrees, frame N would BE frame 0
so it is never emitted, and the light is fixed in camera space so nothing else varies.
All frames share one quantised palette, otherwise GIF palette drift shows up as a flicker
that is not in the geometry.

TWO KINDS OF MESH, AND THEY MUST BE SHADED DIFFERENTLY (fbx_export.vertex_attr_kind):

  * NORMALS in the Vtx bytes (the faction logos) -- light them. Lambert plus a Blinn
    highlight off the cartridge's own normals; that is what makes the gold read as metal.
  * COLOURS in the Vtx bytes (the EVA wordmark, the speaker) -- the shading is already
    BAKED, three grey levels for face, bevel and side. The cartridge's combiner here is
    TEXEL0 * SHADE, so the faithful render is exactly texture x vertex colour and no
    lighting at all. Lighting it again on top double-shades it into mud.

A fixed GAIN is applied to the baked-colour path only, because those meshes were authored
to sit on a lit briefing screen and come out dim as a standalone GIF on black. That is a
presentation choice and the only one in this file; nothing else deviates from the data.
"""
import sys, os, math
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import textures as TX, fbx_export as FX

ROM = FX.load_rom()
SS = 2                                  # supersample factor
BG = np.array([14, 14, 18]) / 255.0


GAIN = 1.55           # baked-colour meshes only; see the note above


def load(name):
    table = FX.briefing_models(ROM)
    mesh = FX.build_mesh(ROM, table[name])
    texs = []
    for mat in mesh["mats"]:
        t = mat["tex"]
        rows = TX.decode_texture(ROM, t['rom'], t['fmt'], t['siz'], t['w'], t['h'], t['pal'])
        texs.append(np.array([[list(r[i*4:i*4+4]) for i in range(t['w'])] for r in rows],
                             dtype=np.float64))
    P = np.array(mesh["positions"], dtype=np.float64)
    P -= (P.min(0) + P.max(0)) / 2                 # centre exactly, so the spin has no wobble
    return mesh, P, texs


def bilinear(tex, u, v):
    """Sample like the RDP does -- the console bilinear-filters these 32x32 tiles."""
    h, w = tex.shape[0], tex.shape[1]
    x = u * w - 0.5
    y = v * h - 0.5
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int)
    fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    x0m, x1m = np.mod(x0, w), np.mod(x0 + 1, w)
    y0m, y1m = np.mod(y0, h), np.mod(y0 + 1, h)
    a = tex[y0m, x0m, :3]; b = tex[y0m, x1m, :3]
    c = tex[y1m, x0m, :3]; d = tex[y1m, x1m, :3]
    return ((a*(1-fx) + b*fx)*(1-fy) + (c*(1-fx) + d*fx)*fy) / 255.0


def fit_radii(P):
    """The two radii that actually bound a Y-spin.

    Fitting a bounding SPHERE is wrong for anything that is not roughly square: the EVA
    wordmark is 150 x 50, and a sphere fit leaves it a third of the height of its own
    frame. What bounds the animation is the XZ radius (the widest the silhouette ever
    gets, at any angle) and the Y radius (which a Y-spin never changes at all).
    """
    rxz = float(np.sqrt(P[:, 0] ** 2 + P[:, 2] ** 2).max())
    ry = float(np.abs(P[:, 1]).max())
    return rxz, ry


def auto_canvas(rxz, ry, long_side=400, min_short=160):
    """Canvas proportioned to the model, so a wide wordmark is not mostly background."""
    a = rxz / max(ry, 1e-6)
    if a >= 1.0:
        h = int(max(min_short, round(long_side / a / 2) * 2))
        return long_side, min(h, long_side)
    w = int(max(min_short, round(long_side * a / 2) * 2))
    return min(w, long_side), long_side


def frame(mesh, P, texs, ang, W, H, rxz, ry, fov=30.0):
    SW, SH = W * SS, H * SS
    ca, sa = math.cos(ang), math.sin(ang)
    R = np.array([[ca, 0.0, sa], [0.0, 1.0, 0.0], [-sa, 0.0, ca]])
    V = P @ R.T
    N = np.array(mesh["poly_nrm"]) @ R.T

    f = (SH / 2) / math.tan(math.radians(fov) / 2)
    # Push the camera back until BOTH radii sit inside the frame with a 10% margin.
    dist = max(rxz * f / (SW / 2 * 0.90), ry * f / (SH / 2 * 0.90))
    ez = np.maximum(V[:, 2] + dist, 1e-3)           # camera looks down -Z from +Z
    X = V[:, 0] * f / ez + SW / 2
    Y = SH / 2 - V[:, 1] * f / ez
    invz = 1.0 / ez

    img = np.empty((SH, SW, 3)); img[:] = BG
    zb = np.full((SH, SW), -1e30)
    L = np.array([-0.34, 0.46, 0.82]); L /= np.linalg.norm(L)      # fixed in camera space
    HALF = L + np.array([0.0, 0.0, 1.0])                           # Blinn half-vector
    HALF /= np.linalg.norm(HALF)                                   # (not H -- that is the canvas height)

    uvs_all = np.array(mesh["poly_uv"])
    lit = mesh["kind"] == "normal"
    cols_all = None if lit else np.array(mesh["poly_col"])
    for fi, poly in enumerate(mesh["polys"]):
        x, y, iz = X[poly], Y[poly], invz[poly]
        x0, x1 = max(int(x.min()), 0), min(int(x.max()) + 2, SW)
        y0, y1 = max(int(y.min()), 0), min(int(y.max()) + 2, SH)
        if x1 <= x0 or y1 <= y0:
            continue
        d = (y[1]-y[2])*(x[0]-x[2]) + (x[2]-x[1])*(y[0]-y[2])
        if abs(d) < 1e-12:
            continue
        px, py = np.meshgrid(np.arange(x0, x1) + .5, np.arange(y0, y1) + .5)
        l0 = ((y[1]-y[2])*(px-x[2]) + (x[2]-x[1])*(py-y[2])) / d
        l1 = ((y[2]-y[0])*(px-x[2]) + (x[0]-x[2])*(py-y[2])) / d
        l2 = 1 - l0 - l1
        m = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not m.any():
            continue
        zz = l0*iz[0] + l1*iz[1] + l2*iz[2]         # 1/z: bigger is nearer
        sub = zb[y0:y1, x0:x1]
        upd = m & (zz > sub)
        if not upd.any():
            continue
        # perspective-correct barycentrics for the attributes
        w0, w1, w2 = l0*iz[0], l1*iz[1], l2*iz[2]
        ws = w0 + w1 + w2
        w0, w1, w2 = w0/ws, w1/ws, w2/ws

        n = N[fi*3:fi*3+3]
        nx = w0*n[0,0] + w1*n[1,0] + w2*n[2,0]
        ny = w0*n[0,1] + w1*n[1,1] + w2*n[2,1]
        nz = w0*n[0,2] + w1*n[1,2] + w2*n[2,2]
        nl = np.sqrt(nx*nx + ny*ny + nz*nz); nl[nl == 0] = 1
        nx, ny, nz = nx/nl, ny/nl, nz/nl
        flip = np.sign(nz); flip[flip == 0] = 1     # two-sided: these plates are seen from both faces
        nx, ny, nz = nx*flip, ny*flip, nz*flip

        uv = uvs_all[fi*3:fi*3+3]
        tu = w0*uv[0,0] + w1*uv[1,0] + w2*uv[2,0]
        tv = w0*uv[0,1] + w1*uv[1,1] + w2*uv[2,1]
        col = bilinear(texs[mesh["poly_mat"][fi]], tu, tv)

        if lit:
            lam = np.clip(nx*L[0] + ny*L[1] + nz*L[2], 0, 1)
            spec = np.power(np.clip(nx*HALF[0] + ny*HALF[1] + nz*HALF[2], 0, 1), 34)
            out = np.clip(col*(0.30 + 0.88*lam)[..., None] + spec[..., None]*0.55, 0, 1)
        else:
            # TEXEL0 * SHADE, exactly as the cartridge's combiner does it.
            cv = cols_all[fi*3:fi*3+3]
            sr = w0*cv[0,0] + w1*cv[1,0] + w2*cv[2,0]
            sg = w0*cv[0,1] + w1*cv[1,1] + w2*cv[2,1]
            sb = w0*cv[0,2] + w1*cv[1,2] + w2*cv[2,2]
            out = np.clip(col * np.stack([sr, sg, sb], -1) * GAIN, 0, 1)
        img[y0:y1, x0:x1][upd] = out[upd]
        sub[upd] = zz[upd]

    im = Image.fromarray((img*255 + 0.5).astype(np.uint8))
    return im.resize((W, H), Image.BOX)              # box downsample = clean antialias


def spin(name, out, canvas=None, nframes=48, delay=40):
    mesh, P, texs = load(name)
    rxz, ry = fit_radii(P)
    W, H = canvas or auto_canvas(rxz, ry)
    frames = []
    for i in range(nframes):                         # 0..N-1: frame N would equal frame 0
        frames.append(frame(mesh, P, texs, 2*math.pi*i/nframes, W, H, rxz, ry))
        print("\r  %s %d/%d" % (name, i+1, nframes), end="", flush=True)
    print()
    # One palette for the whole animation. Median-cut over every third frame at half
    # size: the object is rigid, so that sample carries the same colours as all 48 and
    # the quantiser runs in a second instead of a minute.
    samp = frames[::3]
    sw, sh = W // 2, H // 2
    strip = Image.new("RGB", (sw, sh*len(samp)))
    for i, fr in enumerate(samp):
        strip.paste(fr.resize((sw, sh), Image.BOX), (0, i*sh))
    pal = strip.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE)
    q = [fr.quantize(palette=pal, dither=Image.NONE) for fr in frames]
    q[0].save(out, save_all=True, append_images=q[1:], duration=delay, loop=0,
              optimize=True, disposal=1)
    return out, len(frames), (W, H)


if __name__ == "__main__":
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    names = sys.argv[2:] or ["BRF_LOGO_GDI", "BRF_LOGO_NOD", "BRF_EVA_ROOT"]
    for name in names:
        stem = name.lower().replace("brf_", "").replace("_root", "")
        p, n, c = spin(name, os.path.join(outdir, stem + "_spin.gif"))
        print("  -> %s  %dx%d  %d frames  %.2f MB"
              % (p, c[0], c[1], n, os.path.getsize(p)/1e6))
