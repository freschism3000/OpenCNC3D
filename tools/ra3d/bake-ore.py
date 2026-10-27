#!/usr/bin/env python3
"""Bake Red Alert's ore, gems and ore mine from the Red Alert lab's approved sets into the
files Enhanced draws them from: data/ra3d_ore/ (ore3d_TEM.pack and ore3d_WIN.pack, the ore
and gem fields of each theater; MINE_TEM.ebld and MINE_WIN.ebld, the ore mine), with a
manifest.

    tools/ra3d/bake-ore.py [--export-root build-native/remaster-ra] [--out data/ra3d_ore]
    tools/ra3d/bake-ore.py --check          read the baked files back

THE SOURCES are the lab's painted sets (tools/art/remaster/ra/run_ORE.sh: build_ORE.py in
headless Blender, paint_hp.py and pass_ORE.py), under <export-root>/resources-rm: OREPACK,
GEMPACK and MINE, each a mesh (<CODE>_rm.json) and a temperate and a snow sheet
(final/<CODE>_rm_0.png, final/<CODE>_rm_snow_0.png). The layout is read again from the 1996
sprites by tools/art/remaster/ra/ore_layout.py and must be the one the sets were built from
(resources-rm/ore_layout.json), or nothing is baked. The cells are laid by the lab's own code
(tools/art/remaster/ra/ore_cells.py: Pack, its clumps and their smooth normals), so the game
reads the very triangles the lab shows.

APPROVED ROWS ONLY: every one of resources/GOLD01..04, GEM01..04 and MINE approved in
tools/art/remaster/reviews-ra.json, and every source no newer than its approval; a set
painted after it is a remodel in review, and the bake refuses rather than shipping it.

THE PACK (little endian), one per theater, both sets in it (ore first, then gems):

    "RAORE1\\0\\0", u32 version 2, u32 sets
    per set:
      u32 kind (0 ore, 1 gems), u32 arrangements (4), u32 density frames (12, 3)
      u32 side, then side * side RGBA texels, top row first: the painted sheet box-filtered
          to SIDE; ITS ALPHA IS THE EARTH'S EDGE FADE (pass_ORE.py), 255 on the gold and the
          stones
      u32 side, then side * side RGBA texels: THE SPECULAR MAP (pass_ORE.py's <sheet>_spec.png),
          red how strongly a texel reflects the sun, green how tight its highlight is, blue
          its weight (the light pass's upright sheen), alpha 255; the earth's texels 0
          (version 1 had none)
      u32 clumps; per clump: u32 role (0 an earth, 1 a nugget, 2 a stone), u32 vertices,
          u32 triangles, f32 ax, ay, top (half its plan extent east and north, its highest
          point), then per vertex f32 x, y, z, nx, ny, nz, u, v and per triangle 3 u16.
          Model frame, cells: x east, y north, z up. An earth lies where it lies on its cell
          (the cell's centre the origin); a nugget or a stone is centred on its plan box,
          its base at z 0 (a nugget is built sunk below it). v is 1 at the sheet's top row.
      i32 earth[arrangements * frames]: the earth clump of (arrangement, frame), -1 none
      per arrangement:
          u32 nuggets; per nugget f32 x, y, a, b, h, ang, i32 born, f32 size[frames]
          u32 stones; per stone i32 clump, f32 x, y, u8 shown[frames], padded to 4 bytes

The game lays a cell from it by ore_cells.py's rule (game/ra3d_ore_mod.h).

THE MINE is a terrain object, drawn by the remastered-building path as the trees are: one
intact-only .ebld per theater, baked by the shared baker (tools/art/remaster/bake_ebld.py)
from the lab's export of the row (resources/MINE/viewer/remaster.json), whose pane stands the
temperate model west and the snow one east: each is taken alone and stood back on the
cell's centre, the engine's centre coordinate for the mine (tdata.cpp: XYP_COORD(12, 12)).

The output is committed (no build machine has Blender or the lab's sets); the bake is
deterministic: the same sets give the same bytes. tools/ra3d/stage-structures.py stages it.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
RA_TOOLS = ROOT / 'tools/art/remaster/ra'
TOOLS = ROOT / 'tools/art/remaster'
OUT = ROOT / 'data/ra3d_ore'
REVIEWS = TOOLS / 'reviews-ra.json'
ROWS = ('GOLD01', 'GOLD02', 'GOLD03', 'GOLD04', 'GEM01', 'GEM02', 'GEM03', 'GEM04', 'MINE')
SIDE = 512
THEATERS = (('TEM', 'temperate', ''), ('WIN', 'snow', 'snow_'))
# where ore_cells.py stands each theater's mine in the lab's pane (its SIDE)
MINE_PANE = {'temperate': -0.56, 'snow': 0.56}


def lab_root():
    other = os.environ.get('OPENCNC3D_LAB_ROOT')
    return Path(other) / 'build-native/remaster-ra' if other else ROOT / 'build-native/remaster-ra'


def td_lab_root():
    other = os.environ.get('OPENCNC3D_LAB_ROOT')
    return Path(other) / 'build-native/remaster' if other else ROOT / 'build-native/remaster'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def stamp(p):
    return time.strftime('%Y-%m-%d %H:%M', time.localtime(Path(p).stat().st_mtime))


def die(msg):
    sys.exit('bake-ore: ' + msg)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def sources(wr):
    rm = wr / 'resources-rm'
    out = {}
    for code in ('OREPACK', 'GEMPACK', 'MINE'):
        out[f'resources-rm/{code}/{code}_rm.json'] = rm / code / f'{code}_rm.json'
        for _, _, s in THEATERS:
            out[f'resources-rm/{code}/final/{code}_rm_{s}0.png'] = rm / code / 'final' / f'{code}_rm_{s}0.png'
            if code != 'MINE':
                out[f'resources-rm/{code}/final/{code}_rm_{s}0_spec.png'] = rm / code / 'final' / f'{code}_rm_{s}0_spec.png'
    out['resources-rm/ore_layout.json'] = rm / 'ore_layout.json'
    out['resources/MINE/viewer/remaster.json'] = wr / 'resources/MINE/viewer/remaster.json'
    return out


def approvals(srcs):
    reviews = json.loads(REVIEWS.read_text())
    stamps = {}
    for t in ROWS:
        row = reviews.get(f'resources/{t}') or {}
        if row.get('state') != 'approved' or not row.get('remaster_approved'):
            die(f'resources/{t} is not approved in the Red Alert lab')
        stamps[t] = row['remaster_approved']
    first = min(stamps.values())
    for name, p in srcs.items():
        if not p.is_file():
            die(f'no {name} in the lab ({p})')
        if stamp(p) > first:
            die(f'{name} ({stamp(p)}) is newer than the approval ({first}): a remodel in review')
    return stamps


def check_layout(wr):
    """The 1996 layout read again by the committed script, against the one the sets were
    built from."""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / 'ore_layout.json'
        r = subprocess.run([sys.executable, str(RA_TOOLS / 'ore_layout.py'), '--json', str(out)],
                           capture_output=True, text=True)
        if r.returncode != 0 or not out.is_file():
            die('ore_layout.py failed: ' + r.stderr.strip()[-400:])
        if out.read_bytes() != (wr / 'resources-rm/ore_layout.json').read_bytes():
            die('the layout read off the 1996 sprites is not the one the lab sets were built from')
        return json.loads(out.read_text())


def lab_cells(wr):
    """ore_cells.py, the lab's rule and its Pack, reading this work root."""
    argv = sys.argv
    sys.path.insert(0, str(RA_TOOLS))
    try:
        sys.argv = ['ore_cells.py', str(wr)]
        return load('ra_ore_cells', RA_TOOLS / 'ore_cells.py')
    finally:
        sys.argv = argv


def clump_blob(cl, role):
    """A clump's record: its triangles indexed, v flipped to the sheet's top row."""
    key, pos, tris = {}, [], []
    for co, n, uv in cl['tris']:
        t = []
        for j in range(3):
            k = (tuple(np.round(co[j], 6)), tuple(np.round(n[j], 4)), tuple(np.round(uv[j], 6)))
            if k not in key:
                key[k] = len(pos)
                pos.append((float(co[j][0]), float(co[j][1]), float(co[j][2]),
                            float(n[j][0]), float(n[j][1]), float(n[j][2]),
                            float(uv[j][0]), 1.0 - float(uv[j][1])))
            t.append(key[k])
        if t[0] != t[1] and t[1] != t[2] and t[0] != t[2]:
            tris.append(t)
    if len(pos) > 65535:
        die('a clump has more vertices than 16-bit indices reach')
    b = bytearray(struct.pack('<3I3f', role, len(pos), len(tris), cl['ax'], cl['ay'], cl['top']))
    for v in pos:
        b += struct.pack('<8f', *v)
    for t in tris:
        b += struct.pack('<3H', *t)
    return bytes(b), len(tris)


def bleed(a, passes=48):
    """The colour of every fully transparent texel taken from its nearest painted ones (the
    painter leaves them white): a texel filtered across the earth's faded edge, or a mip
    level averaged over it, would otherwise pale toward white. Alpha is untouched."""
    a = a.copy()
    rgb = a[..., :3].astype(np.float64)
    have = a[..., 3] > 0
    for _ in range(passes):
        if have.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(have.shape)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                h = np.roll(np.roll(have, dy, 0), dx, 1)
                acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * h[..., None]
                cnt += h
        grow = ~have & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        have = have | grow
    if not have.all():
        rgb[~have] = rgb[have].mean(0) if have.any() else 0
    a[..., :3] = np.clip(np.round(rgb), 0, 255).astype(np.uint8)
    return a


def sheet(path):
    from PIL import Image
    im = Image.open(path).convert('RGBA')
    if im.size != (SIDE, SIDE):
        im = im.resize((SIDE, SIDE), Image.BOX)
    return bleed(np.asarray(im)).tobytes()


def spec_sheet(path):
    """The set's specular map (pass_ORE.py: red the strength, green the tightness, blue the
    highlight's weight) at SIDE, alpha 255; its texels outside the islands already bled by
    the painter."""
    from PIL import Image
    im = Image.open(path).convert('RGB')
    if im.size != (SIDE, SIDE):
        im = im.resize((SIDE, SIDE), Image.BOX)
    a = np.asarray(im)
    return np.dstack([a, np.full(a.shape[:2], 255, np.uint8)]).astype(np.uint8).tobytes()


def set_blob(OC, lay, code, sfx, info):
    pk = OC.Pack(code)
    ore = code == 'OREPACK'
    arr = ('GOLD01', 'GOLD02', 'GOLD03', 'GOLD04') if ore else ('GEM01', 'GEM02', 'GEM03', 'GEM04')
    nf = 12 if ore else 3
    clumps, earth, tris = [], [], 0
    for t in arr:
        for f in range(nf):
            cl = pk.clump('earth_%s_%d' % (t, f), keep=True)
            if cl is None:
                earth.append(-1)
                continue
            b, n = clump_blob(cl, 0)
            earth.append(len(clumps))
            clumps.append(b)
            tris += n
    stone_of = {}
    if ore:
        for i in range(6):
            cl = pk.clump('nug_%d' % i)
            if cl is None:
                die(f'{code} has no nug_{i}')
            b, n = clump_blob(cl, 1)
            clumps.append(b)
            tris += n
    else:
        for t in arr:
            for s in lay['cells'][t]['stones']:
                if s['stone'] in stone_of:
                    continue
                cl = pk.clump(s['stone'])
                if cl is None:
                    die(f'{code} has no stone {s["stone"]}')
                b, n = clump_blob(cl, 2)
                stone_of[s['stone']] = len(clumps)
                clumps.append(b)
                tris += n
    out = bytearray(struct.pack('<3I', 0 if ore else 1, len(arr), nf))
    out += struct.pack('<I', SIDE) + sheet(pk.dir + '/final/%s_rm_%s0.png' % (code, sfx))
    out += struct.pack('<I', SIDE) + spec_sheet(pk.dir + '/final/%s_rm_%s0_spec.png' % (code, sfx))
    out += struct.pack('<I', len(clumps))
    for b in clumps:
        out += b
    out += struct.pack('<%di' % len(earth), *earth)
    for t in arr:
        c = lay['cells'][t]
        nugs = c.get('nuggets', []) if ore else []
        out += struct.pack('<I', len(nugs))
        for n in nugs:
            out += struct.pack('<6fi', n['x'], n['y'], n['a'], n['b'], n['h'], n['ang'], n['born'])
            out += struct.pack('<%df' % nf, *n['size'])
        stones = [] if ore else c['stones']
        out += struct.pack('<I', len(stones))
        for s in stones:
            # (a stone's point as ore_cells.py lays it: the proxy's pixel fractions, y north)
            out += struct.pack('<i2f', stone_of[s['stone']], s['x'] - 0.5, 0.5 - s['z'])
            shown = bytes(1 if s['stage'][f] else 0 for f in range(nf))
            out += shown + bytes((-nf) % 4)
    info[code] = dict(clumps=len(clumps), earths=sum(1 for e in earth if e >= 0), triangles=tris,
                      nuggets=[len(lay['cells'][t].get('nuggets', [])) for t in arr] if ore else None,
                      stones=[len(lay['cells'][t].get('stones', [])) for t in arr] if not ore else None)
    return bytes(out)


def bake_packs(OC, lay, out):
    files = {}
    for th, theater, sfx in THEATERS:
        info = {}
        blob = bytearray(b'RAORE1\0\0' + struct.pack('<2I', 2, 2))
        for code in ('OREPACK', 'GEMPACK'):
            blob += set_blob(OC, lay, code, sfx, info)
        p = out / f'ore3d_{th}.pack'
        p.write_bytes(bytes(blob))
        files[p.name] = dict(theater=theater, bytes=len(blob), sha256=hashlib.sha256(blob).hexdigest(),
                             sheet=SIDE, sets=info)
        print(f'{p.name}: {theater}, {len(blob)} bytes, ore {info["OREPACK"]["triangles"]} and gems '
              f'{info["GEMPACK"]["triangles"]} triangles in their clumps')
    return files


def resample(src, dst):
    """The mine's sheet as the baker takes it: painted at 1152 a side (232 texels a cell, the
    set's scale), resampled to the power of two below it, the UVs unchanged (they are
    fractions of the sheet). Returns the note."""
    from PIL import Image
    with Image.open(src) as im:
        w, h = im.size
        p = 1
        while p * 2 <= max(w, h):
            p *= 2
        if (w, h) == (p, p):
            dst.symlink_to(src.resolve())
            return []
        im.convert('RGBA').resize((p, p), Image.BOX).save(dst)
    return [f'{src.name}: resampled from {w}x{h} to {p}x{p}']


def bake_mine(wr, out):
    b = load('ra_ore_mine_baker', TOOLS / 'bake_ebld.py')
    b.FOOTPRINT['MINE'] = (1, 1)
    b.YAW_FILE = str(ROOT / 'art/remaster-ra/yaw.json')
    b.SCALE_FILE = str(ROOT / 'art/remaster-ra/scale.json')
    view = wr / 'resources/MINE/viewer'
    R = json.loads((view / 'remaster.json').read_text())
    files = {}
    for th, theater, _ in THEATERS:
        g = next((g for g in R['groups'] if g.get('mat') == f'MINE_{theater}'), None)
        if g is None:
            die(f'the MINE export has no {theater} model')
        g = dict(g)
        dx = MINE_PANE[theater]
        p = list(g['p'])
        for i in range(0, len(p), 3):
            p[i] = round(p[i] - dx, 5)
        g['p'] = p
        code = f'MINE_{th}'
        notes = []
        with tempfile.TemporaryDirectory() as tmp:
            v = Path(tmp) / code / 'viewer'
            v.mkdir(parents=True)
            for name in set(g['tex'].values()):
                notes += resample(view / name, v / name)
            (v / 'remaster.json').write_text(json.dumps(dict(
                name=code, phase=R.get('phase', 'model'), triangles=len(p) // 9, groups=[g])))
            info = b.bake(code, str(v), str(out), str(td_lab_root()), intact_only=True)
        files[info['file']] = dict(theater=theater, bytes=info['bytes'], sha256=info['sha256'],
                                   sets=info['sets'], textures=[t['name'] for t in info['textures']],
                                   notes=notes)
    return files


def check(out):
    for p in sorted(out.glob('ore3d_*.pack')):
        d = p.read_bytes()
        assert d[:8] == b'RAORE1\0\0', p
        ver, nsets = struct.unpack_from('<2I', d, 8)
        o = 16
        for _ in range(nsets):
            kind, narr, nf = struct.unpack_from('<3I', d, o); o += 12
            side, = struct.unpack_from('<I', d, o); o += 4 + side * side * 4
            if ver >= 2:
                ss, = struct.unpack_from('<I', d, o); o += 4 + ss * ss * 4
            nc, = struct.unpack_from('<I', d, o); o += 4
            for _ in range(nc):
                role, nv, nt = struct.unpack_from('<3I', d, o); o += 24
                o += nv * 32
                idx = struct.unpack_from('<%dH' % (nt * 3), d, o); o += nt * 6
                assert not idx or max(idx) < nv, p
            earth = struct.unpack_from('<%di' % (narr * nf), d, o); o += 4 * narr * nf
            assert all(-1 <= e < nc for e in earth), p
            for _ in range(narr):
                nn, = struct.unpack_from('<I', d, o); o += 4 + nn * (28 + 4 * nf)
                ns, = struct.unpack_from('<I', d, o); o += 4
                for _ in range(ns):
                    c, = struct.unpack_from('<i', d, o)
                    assert 0 <= c < nc, p
                    o += 12 + nf + (-nf) % 4
        assert o == len(d), (p, o, len(d))
        print(f'{p.name}: read back, {nsets} sets')
    b = load('ra_ore_mine_baker', TOOLS / 'bake_ebld.py')
    for p in sorted(out.glob('MINE_*.ebld')):
        b.check(str(p), quiet=True)
        print(f'{p.name}: read back')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--export-root', type=Path, default=None)
    ap.add_argument('--out', type=Path, default=OUT)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    if a.check:
        check(a.out)
        return 0
    wr = a.export_root or lab_root()
    srcs = sources(wr)
    stamps = approvals(srcs)
    lay = check_layout(wr)
    OC = lab_cells(wr)
    a.out.mkdir(parents=True, exist_ok=True)
    files = bake_packs(OC, lay, a.out)
    files.update(bake_mine(wr, a.out))
    (a.out / 'manifest.json').write_text(json.dumps(dict(
        what="Red Alert's ore, gems and ore mine for Enhanced, baked by tools/ra3d/bake-ore.py "
             "from the Red Alert lab's approved sets",
        approval={f'resources/{t}': s for t, s in stamps.items()},
        sources={n: sha(p) for n, p in sorted(srcs.items())},
        files=files), indent=1) + '\n')
    check(a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
