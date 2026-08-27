#!/usr/bin/env python3
"""Render each broken model before/after applying the ROM mount transform, side by side."""
import os, sys, json, base64, struct
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SCR = os.environ.get("CNC3D_SCRATCH") or os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE); sys.path.insert(0, SCR + "/survey")
import mount, rend2

um = json.load(open(SCR + "/unit_models.json"))
PL = json.load(open(SCR + "/survey/payload_v4.json"))
BY = {m['name']: m for m in PL}

NAMES = ['APC','ATWR','EYE','GUN','HAND','HARV','HQ','JEEP','LTNK','MTNK','PYLE','V19','WEAP']


def apply_mount(m, name, transpose=True):
    """Return a copy of the payload model with per-part vertices moved to parent space."""
    mi = um[name]['model_index']
    pts = mount.part_transforms(mi)
    raw = base64.b64decode(m['pos'])
    pos = np.frombuffer(raw, dtype='>i2').astype(np.float64).reshape(-1, 3).copy()
    for s in m['subs']:
        p = s['part']
        if p >= len(pts):
            continue
        M3, t = pts[p]['M'], pts[p]['t']
        sl = slice(s['first'], s['first'] + s['count'])
        v = pos[sl]
        pos[sl] = (v @ M3 if transpose else v @ M3.T) + t
    out = dict(m)
    q = np.clip(np.rint(pos), -32768, 32767).astype('>i2')
    out['pos'] = base64.b64encode(q.tobytes()).decode()
    return out


def strip(names, path, tag, transpose=True, tile=230):
    F = rend2.font(13); F2 = rend2.font(10)
    cols, rows = 2, len(names)
    sh = Image.new('RGB', (cols*tile, rows*(tile+18)), (12, 13, 16))
    d = ImageDraw.Draw(sh)
    for i, nm in enumerate(names):
        m = BY[nm]
        for j, mm in enumerate([m, apply_mount(m, nm, transpose)]):
            im, _ = rend2.raster(mm, tile, tile, yaw=35, pitch=25)
            sh.paste(im, (j*tile, i*(tile+18)))
        d.text((5, i*(tile+18)+tile+2), "%s   left=raw   right=%s" % (nm, tag),
               fill=(255, 235, 120), font=F)
    sh.save(path)
    return path


if __name__ == "__main__":
    tr = sys.argv[1] != 'T' if len(sys.argv) > 1 else True
    tag = sys.argv[2] if len(sys.argv) > 2 else 'mounted'
    half = len(NAMES)//2 + 1
    print(strip(NAMES[:half], f"{HERE}/cmp_a.png", tag, tr))
    print(strip(NAMES[half:], f"{HERE}/cmp_b.png", tag, tr))
