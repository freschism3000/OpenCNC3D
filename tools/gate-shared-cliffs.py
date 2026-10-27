#!/usr/bin/env python3
"""Verify the installed native Enhanced banks share complete TD/RA cliff surfaces."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
from PIL import Image


def colour(path, names=None):
    b = path.read_bytes()
    native_ra = b[:8] == b'C3DRA144'
    assert native_ra or b[:8] == b'C3DT30\0\0', path
    _, cell, count = struct.unpack_from('<III', b, 8)
    key_size = 20 if native_ra else 4
    start, stride = 20 + count * key_size, cell * cell * 3
    assert len(b) == start + count * stride, path
    result = {}
    for i in range(count):
        at = 20 + i * key_size
        if native_ra:
            name = b[at:at+16].split(b'\0')[0].decode('ascii')
            icon, variant, size = struct.unpack_from('<HBB', b, at+16)
            field = icon | (variant << 8) | ((((size & 15) << 10) | ((size >> 4) << 13)) if variant else 0)
            key = names[name], field
        else:
            key = struct.unpack_from('<HH', b, at)
        result[key] = b[start+i*stride:start+(i+1)*stride]
    return result


def masks(path, height=False):
    b = path.read_bytes()
    assert b[:8] == (b'C3DSURF\0' if height else b'C3DGRAS\0'), path
    _, cell, count = struct.unpack_from('<III', b, 8)
    start, stride = 20 + count * 24, cell * cell * (1 if height else 2)
    assert len(b) == start + count * stride, path
    return {struct.unpack_from('<HHBB', b, 20 + i * 24 + 16):
            b[start + i * stride:start + (i + 1) * stride] for i in range(count)}


def seams(path, names=None):
    b = path.read_bytes()
    native_ra = b[:8] == b'C3DRASEM'
    assert native_ra or b[:8] == b'C3DSEAM\0', path
    _, cell, margin, count = struct.unpack_from('<IIII', b, 8)
    key_size = 24 if native_ra else 8
    start, stride = 24 + count * key_size, cell * margin * 4
    assert len(b) == start + count * stride, path
    result = {}
    for i in range(count):
        at = 24 + i * key_size
        if native_ra:
            name = b[at:at+16].split(b'\0')[0].decode('ascii')
            icon, side, _, variant, size, _ = struct.unpack_from('<HBBBBH', b, at+16)
            field = icon | (variant << 8) | ((((size & 15) << 10) | ((size >> 4) << 13)) if variant else 0)
            key = names[name], field, side
        else:
            key = struct.unpack_from('<HHB', b, at)
        result[key] = b[start+i*stride:start+(i+1)*stride]
    return result


def run(source, install):
    root = source / 'art/remaster/textures/unified_daylight_v01'
    rows = json.loads((root / 'MANIFEST.json').read_text())['assets']
    td = {(r['name'], r['variant']): r for r in rows if r['theater'] == 'TEMPERAT'}
    ra = [r for r in rows if r['theater'] == 'RATEMP'
          and r['name'].startswith('S') and not r['name'].startswith('SH')]
    paths = [install / 'terrainenh_TEMPERAT.pack', install / 'ra3d/terrainenh_RA_TEMPERAT.pack',
             install / 'terrainsurface_TEMPERAT.pack', install / 'terrainsurface_RA_TEMPERAT.pack',
             install / 'grassmask_TEMPERAT.pack', install / 'terrainseam_TEMPERAT.pack',
             install / 'ra3d/terrainseam_RA_TEMPERAT.pack']
    ra_names = {r['name']: r['tid'] for r in rows if r['theater'] == 'RATEMP'}
    # CLEAR/road/bridge names outside the physical-surface manifest are irrelevant
    # to the cliff gate, but still occupy real slots in the RA package.
    ra_names = dict(ra_names)
    for path in (paths[1], paths[6]):
        b = path.read_bytes(); n = struct.unpack_from('<I', b, 16 if path == paths[1] else 20)[0]
        head, size = (20, 20) if path == paths[1] else (24, 24)
        for i in range(n):
            name = b[head+i*size:head+i*size+16].split(b'\0')[0].decode('ascii')
            ra_names.setdefault(name, -len(ra_names)-1)
    ctd, cra = colour(paths[0]), colour(paths[1], ra_names)
    htd, hra, ground = masks(paths[2], True), masks(paths[3], True), masks(paths[4])
    std, sra = seams(paths[5]), seams(paths[6], ra_names)
    cells = strips = 0
    for r in ra:
        t = td[(r['name'], r['variant'])]
        assert (r['cells'], r['have']) == (t['cells'], t['have']), r['name']
        v = r['variant']
        for icon in r['have']:
            w, h = r['cells']
            field = icon | (v << 8) | ((((w - 1) << 10) | ((h - 1) << 13)) if v else 0)
            assert ctd[(t['tid'], field)] == cra[(r['tid'], field)], ('colour', r['name'], v, icon)
            assert htd[(t['tid'], icon, v, 0)] == hra[(r['tid'], icon, v, 1)], ('height', r['name'], v, icon)
            assert ground[(t['tid'], icon, v, 0)] == ground[(r['tid'], icon, v, 1)], ('ground mask', r['name'], v, icon)
            x, y = icon % w, icon // w
            matte = np.array(Image.open(root / t['export'] / 'rock_mask.png'))[y*144:(y+1)*144, x*144:(x+1)*144] == 255
            pixels = np.frombuffer(ctd[(t['tid'], field)], np.uint8).reshape(144, 144, 3).astype(int)
            if matte.any():
                assert np.max(np.ptp(pixels[matte], axis=1)) <= 9, ('warm mineral streak', r['name'], v, icon)
            cells += 1
        a = {(key[1], key[2]): value for key, value in std.items()
             if key[0] == t['tid'] and ((key[1] >> 8) & 3) == v}
        b = {(key[1], key[2]): value for key, value in sra.items()
             if key[0] == r['tid'] and ((key[1] >> 8) & 3) == v}
        assert a == b, ('external feather strips', r['name'], v)
        strips += len(a)
    return {'passed': True, 'templates_and_variants': len(ra), 'cells': cells,
            'feather_strips': strips, 'native_banks_verified': True,
            'installed_sha256': {str(p.relative_to(install)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument('--playable', type=Path, required=True)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    result = run(args.source.resolve(), args.playable.resolve())
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
