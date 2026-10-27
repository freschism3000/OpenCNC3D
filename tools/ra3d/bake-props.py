#!/usr/bin/env python3
"""Bake Red Alert's props from the Red Alert lab into the files Enhanced draws them from:
game/ebld_ra_props/<TYPE>.ebld (and the fence's placement, FENC.eplc), with a manifest.

Usage: tools/ra3d/bake-props.py [--export-root build-native/remaster-ra] [TYPE ...]
       tools/ra3d/bake-props.py --check

THE PROPS are the lab's civil/ rows that are neither a civilian building nor a crate reused
from OpenCNC3D: Red Alert's own art, modelled in the Red Alert lab (tools/art/remaster/ra/,
art/remaster-ra/civil/<TYPE>/NOTES.md). Each is baked from its export,
build-native/remaster-ra/civil/<TYPE>/viewer, by the shared baker
(tools/art/remaster/bake_ebld.py), in the form the engine draws it in:

  BARL BRL3        the explosive barrels, one-cell buildings of the neutral side, with their
                   stages: the drum cut (destroy.json) and the burst (destroyed.json, the
                   drums' pieces thrown by role), and their death's explosion (fx.json:
                   Red Alert's FBALL1). Full files: the engine wears, bursts and scorches
                   them as it does a structure with its stages.
  MINV MINP        the mines, one-cell buildings: the standing model only (the engine
                   deletes a mine that goes off, and its own anim is the explosion).
  BOXES01..09      the interior's crate stacks, terrain objects, CenterBase frame.
  ICE01..05        the ice floes, terrain objects on the sea, CenterBase frame, z 0 the
                   sea's surface.
  WWCRATE          the water crate, a cell decoration with its idle bob loop and ripple
                   rings (translucent parts of the clip).
  FPLS             the flag location, a cell decoration with its idle pulse (the 1996
                   frames' own), its light bands self-lit.
  FENC             the barbed-wire fence, a wall: sixteen connection pieces with their
                   damaged and destroyed stages (pieces.json, destroy.json, destroyed.json)
                   and its placement (place_FENC.json), which the engine plays when the
                   Soviets build a cell.

APPROVED ROWS ONLY. A type is baked when its row in tools/art/remaster/reviews-ra.json is
approved (`remaster_approved`), and only from an export no newer than that approval: an
export written after it is a remodel in review, and the type is left out (named) rather
than baked. A type with stages bakes them only when the row's `stages_approved` covers the
export too; otherwise its standing model alone. The manifest records each file's sources
and their SHA-256, the approval it was baked under, and what it carries.

TWO THINGS THE FILE FORMAT HAS NO PLACE FOR are put into a form it has, in a copy of the
export (the lab's files are never modified):
  - a clip part hidden on a frame (the water crate's ripple rings come and go): the part's
    matrix on that frame collapses it to one point (no area, so nothing is drawn), which
    the idle loop plays frame for frame without blending;
  - a texture whose sides are not a power of two (the flag location's glow sheet): resampled
    to the next power of two, the UVs unchanged (they are fractions of the sheet).

The output is committed art (no build machine has the lab's exports). The bake is
deterministic: the same exports give the same bytes.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'tools/art/remaster'
OUT = ROOT / 'game/ebld_ra_props'
REVIEWS = TOOLS / 'reviews-ra.json'
LAB = ROOT / 'build-native/remaster-ra'
# (the Power Plant's scorch, which an intact-only file borrows: the shared baker's default)
TD_LAB = ROOT / 'build-native/remaster'

# type: (kind, footprint in cells). The footprints are the engine's (redalert/bdata.cpp
# BSIZE_11 for the barrels and mines; redalert/tdata.cpp's occupy lists for the terrain).
PROPS = {
    'BARL': ('building', (1, 1)), 'BRL3': ('building', (1, 1)),
    'MINV': ('mine', (1, 1)), 'MINP': ('mine', (1, 1)),
    **{f'BOXES0{i}': ('terrain', (1, 1)) for i in range(1, 10)},
    'ICE01': ('terrain', (2, 2)), 'ICE02': ('terrain', (1, 2)), 'ICE03': ('terrain', (2, 1)),
    'ICE04': ('terrain', (1, 1)), 'ICE05': ('terrain', (1, 1)),
    'WWCRATE': ('cell', (1, 1)), 'FPLS': ('cell', (1, 1)),
    'FENC': ('wall', (1, 1)),
}


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def stamp(p):
    return time.strftime('%Y-%m-%d %H:%M', time.localtime(Path(p).stat().st_mtime))


def export_time(view, names):
    return max(stamp(view / n) for n in names if (view / n).is_file())


def pow2(n):
    p = 1
    while p < n:
        p *= 2
    return p


def prepared_view(view, tmp):
    """The export as the baker can read it: a copy in `tmp` (files linked, remaster.json and
    any resampled texture written) when a clip hides a part on a frame or a texture is not a
    power of two a side; the export itself otherwise. Returns (folder, notes)."""
    R = json.loads((view / 'remaster.json').read_text())
    notes = []
    a = R.get('anim')
    if a and a.get('vis') is not None and len(a['vis']) and min(a['vis']) == 0:
        frames, parts = int(a['frames']), len(a['parts'])
        mat, vis = list(a['mat']), list(a['vis'])
        hidden = 0
        for f in range(frames):
            for p in range(parts):
                if vis[f * parts + p]:
                    continue
                o = (f * parts + p) * 12
                t = (mat[o + 3], mat[o + 7], mat[o + 11])
                mat[o:o + 12] = [0.0, 0.0, 0.0, t[0], 0.0, 0.0, 0.0, t[1], 0.0, 0.0, 0.0, t[2]]
                vis[f * parts + p] = 1
                hidden += 1
        a['mat'], a['vis'] = mat, vis
        notes.append(f'clip: {hidden} part-frames hidden by the lab collapsed to a point')
    from PIL import Image
    resized = {}
    for g in R['groups']:
        for name in set((g.get('tex') or {}).values()) | ({g['house']} if g.get('house') else set()):
            if name in resized or not (view / name).is_file():
                continue
            with Image.open(view / name) as im:
                w, h = im.size
                if (w, h) != (pow2(w), pow2(h)):
                    resized[name] = (im.convert('RGBA') if im.mode not in ('L', 'RGBA') else im.copy(),
                                     (pow2(w), pow2(h)))
    if not notes and not resized:
        return view, notes
    tmp.mkdir(parents=True, exist_ok=True)
    for f in view.iterdir():
        if f.name != 'remaster.json' and f.name not in resized:
            (tmp / f.name).symlink_to(f.resolve())
    for name, (im, size) in sorted(resized.items()):
        im.resize(size, Image.BILINEAR).save(tmp / name)
        notes.append(f'{name}: resampled from {im.size[0]}x{im.size[1]} to {size[0]}x{size[1]}')
    (tmp / 'remaster.json').write_text(json.dumps(R))
    return tmp, notes


def baker():
    if str(TOOLS) not in sys.path:
        sys.path.insert(0, str(TOOLS))     # (the wall stages read the walls' kit, build_SBAG.py)
    b = load('ra_props_baker', TOOLS / 'bake_ebld.py')
    b.FOOTPRINT.update({k: v[1] for k, v in PROPS.items()})
    b.YAW_FILE = str(ROOT / 'art/remaster-ra/yaw.json')
    b.SCALE_FILE = str(ROOT / 'art/remaster-ra/scale.json')
    b.LAB_SHAKE_CELLS = dict(getattr(b, 'LAB_SHAKE_CELLS', {}), **{k: 1 for k in PROPS})
    if 'FENC' not in b.WALLS:
        b.WALLS = tuple(b.WALLS) + ('FENC',)
    return b


def place_baker():
    p = load('ra_props_place', TOOLS / 'bake_eplc.py')
    if 'FENC' not in p.WALLS:
        p.WALLS = tuple(p.WALLS) + ('FENC',)
    return p


STAGE_FILES = ('destroy.json', 'destroyed.json', 'fx.json', 'pieces.json')


def bake(types, export_root, out):
    reviews = json.loads(REVIEWS.read_text())
    b = baker()
    out.mkdir(parents=True, exist_ok=True)
    mpath = out / 'manifest.json'
    old = json.loads(mpath.read_text()).get('models', {}) if mpath.is_file() else {}
    models, left = {}, []
    for code in types:
        kind, _ = PROPS[code]
        row = reviews.get(f'civil/{code}') or {}
        at = row.get('remaster_approved') or ''
        view = export_root / 'civil' / code / 'viewer'
        if not at or row.get('state') != 'approved':
            left.append(f'{code}: not approved in the Red Alert lab')
            continue
        if not (view / 'remaster.json').is_file():
            left.append(f'{code}: no lab export at {view}')
            continue
        newest = export_time(view, [f.name for f in view.iterdir() if f.is_file()])
        if newest > at:
            left.append(f'{code}: its export ({newest}) is newer than its approval ({at}): in review')
            continue
        staged = kind in ('building', 'wall') and (view / 'destroy.json').is_file() \
            and (view / 'destroyed.json').is_file()
        stages_at = row.get('stages_approved') or ''
        if staged and (not stages_at or stages_at < at):
            staged = False
        if kind == 'wall' and not staged:
            left.append(f'{code}: a wall needs its approved stages')
            continue
        intact_only = not staged
        with tempfile.TemporaryDirectory() as tmp:
            src, notes = prepared_view(view, Path(tmp) / code / 'viewer')
            info = b.bake(code, str(src), str(out), str(TD_LAB), intact_only=intact_only)
        info['sources'] = {n: sha(view / n) for n in sorted(info.get('sources', {}))
                           if (view / n).is_file()}
        info['kind'] = kind
        info['stages'] = 'approved' if staged else 'intact only'
        info['export'] = f'build-native/remaster-ra/civil/{code}/viewer'
        info['approval'] = {k: row[k] for k in ('state', 'phase', 'remaster_approved',
                                                'stages_approved') if k in row}
        if notes:
            info['notes'] = info.get('notes', []) + notes
        models[code] = info
        print(f'{code}: {kind}, {info["stages"]}, {info.get("bytes", 0)} bytes')
        if kind == 'wall' and (view / f'place_{code}.json').is_file():
            pb = place_baker()
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / code).mkdir()
                (root / code / 'viewer').symlink_to(view.resolve())
                pinfo = pb.bake(code, str(root), str(out)) if hasattr(pb, 'bake') else None
            models[code]['placement'] = pinfo or 'baked'
    for k, v in old.items():
        models.setdefault(k, v)
    mpath.write_text(json.dumps(dict(
        what="Red Alert's props for Enhanced: the barrels, the mines, the crate stacks, the ice "
             "floes, the water crate, the flag location and the barbed-wire fence, baked by "
             "tools/ra3d/bake-props.py from the Red Alert lab's approved exports",
        models=dict(sorted(models.items()))), indent=1) + '\n')
    for l in left:
        print(f'   left out: {l}')
    return models, left


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--export-root', type=Path, default=LAB)
    ap.add_argument('--out', type=Path, default=OUT)
    ap.add_argument('--check', action='store_true', help='read every baked file back')
    ap.add_argument('types', nargs='*', choices=sorted(PROPS))
    a = ap.parse_args()
    if a.check:
        b = baker()
        for p in sorted(a.out.glob('*.ebld')):
            b.check(str(p), quiet=True)
        print(f'{len(list(a.out.glob("*.ebld")))} files read back')
        return 0
    bake(a.types or sorted(PROPS), a.export_root, a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
