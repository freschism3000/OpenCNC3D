#!/usr/bin/env python3
"""Check saved Snow contact semantics in an actual C3DGRAS runtime pack.

The hand-picked snow/stone probes deliberately fail the previous production
matte. They are independent of the authoring classifier. Full byte checks then
protect water, the other pack records, shade and the unchanged art/relief inputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
ap.add_argument('--pack', type=Path)
ap.add_argument('--baseline-pack', type=Path)
ap.add_argument('--output', type=Path)
args = ap.parse_args()
R = args.root.resolve()
S = R / 'art/remaster/textures/unified_daylight_v01'
O = S / 'snow_ground_contacts_v01'
pack = args.pack or R / 'game/groundmask_RA_SNOW.pack'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
errors = []


def require(ok, message):
    if not ok:
        errors.append(message)


def read_pack(path):
    data = path.read_bytes()
    version, cell, count = struct.unpack_from('<III', data, 8)
    if data[:8] != b'C3DGRAS\0' or version != 1 or cell != 144:
        raise ValueError('Invalid Snow ground pack: ' + str(path))
    start = 20 + count * 24
    stride = cell * cell * 2
    if len(data) != start + stride * count:
        raise ValueError('Invalid Snow ground pack byte count')
    result = {}
    for i in range(count):
        key = data[20 + i * 24:44 + i * 24]
        name = key[:16].split(b'\0')[0].decode()
        tid, icon, var, game, alternate = struct.unpack_from('<HHBBH', key, 16)
        k = (name, tid, icon, var, game, alternate)
        if k in result:
            raise ValueError('Duplicate Snow ground pack record')
        result[k] = np.frombuffer(data, np.uint8, stride, start + stride * i).reshape(cell, cell, 2)
    return result


manifest = json.loads((O / 'MANIFEST.json').read_text())
fixtures = json.loads((O / 'probes.json').read_text())
actual = read_pack(pack)
baseline = read_pack(args.baseline_pack) if args.baseline_pack else None
surfaces = {(r['name'], r['variant']): r for r in json.loads((S / 'MANIFEST.json').read_text())['assets'] if r['theater'] == 'RASNOW'}
affected = set()
by_name = {}
water_texels = 0
baseline_water_texels = 0
baseline_water_changed = 0
fractional_texels = 0
for row in manifest['assets']:
    r = surfaces[row['name'], row['variant']]
    p = S / r['export']
    for file, field in [('height.png', 'base_height_sha256'), ('rock_mask.png', 'base_rock_sha256'),
                        ('albedo.png', 'base_albedo_sha256'), ('player_colour_mask.png', 'base_player_mask_sha256')]:
        require(sha(p / file) == row[field], 'Protected source changed: ' + r['export'] + '/' + file)
    require(sha(R / row['source']) == row['source_sha256'], 'Approved painting changed: ' + row['name'])
    require(sha(R / row['base_ground']) == row['base_ground_sha256'], 'Original matte changed: ' + row['name'])
    for key, file in row['files'].items():
        require(sha(O / file) == row['sha256'][key], 'Saved semantic label changed: ' + file)
    expected = np.asarray(Image.open(O / row['files']['coverage']).convert('L'))
    old = np.asarray(Image.open(R / row['base_ground']).convert('L'))
    water = np.asarray(Image.open(O / row['files']['native_water']).convert('L')) > 127
    require(np.array_equal(expected[water], old[water]), 'Saved matte changed native water: ' + row['name'])
    w, h = row['cells']
    assembled = np.zeros((h * 144, w * 144), np.uint8)
    for icon in row['have']:
        y, x = divmod(icon, w)
        k = (row['name'], row['tid'], icon, row['variant'], 1, 0)
        affected.add(k)
        if k not in actual:
            require(False, 'Missing runtime source key: ' + repr(k))
            continue
        tile = actual[k]
        sl = (slice(y * 144, (y + 1) * 144), slice(x * 144, (x + 1) * 144))
        assembled[sl] = tile[..., 0]
        require(np.array_equal(tile[..., 0], expected[sl]), 'Runtime matte differs from saved contact: ' + repr(k))
        if baseline is not None and k in baseline:
            # The installed pack is the delivery baseline. Source-only checks
            # cannot prove that no later wet-contact correction was replaced.
            tile_water = water[sl]
            count = int(tile_water.sum())
            changed = int(np.count_nonzero(tile[..., 0][tile_water] != baseline[k][..., 0][tile_water]))
            baseline_water_texels += count
            baseline_water_changed += changed
            require(changed == 0, 'Runtime water differs from actual baseline pack: ' + repr(k))
    require(np.array_equal(assembled[water], old[water]), 'Runtime pack changed native water: ' + row['name'])
    water_texels += int(water.sum())
    fractional_texels += int(((expected > 0) & (expected < 255)).sum())
    by_name[row['name']] = (assembled, row)

probe_results = []
for probe in fixtures['probes']:
    name = probe['name']
    x, y = probe['pixel']
    assembled, row = by_name[name]
    rgb = np.asarray(Image.open(R / row['source']).convert('RGB'))[y, x].tolist()
    require(rgb == probe['source_rgb'], 'Reviewed source probe moved: ' + name + repr((x, y)))
    value = int(assembled[y, x])
    ok = value >= 250 if probe['role'] == 'snow' else value <= 5
    require(ok, 'Reviewed ' + probe['role'] + ' probe failed: ' + name + repr((x, y)) + '=' + str(value))
    probe_results.append(dict(name=name, pixel=[x, y], role=probe['role'], coverage=value, pass_=bool(ok)))

for key, data in actual.items():
    require(bool(np.all(data[..., 1] == 128)), 'Non-neutral shade byte in ' + repr(key))
if baseline is not None:
    require(actual.keys() == baseline.keys(), 'Runtime pack key set changed')
    for key in actual.keys() & baseline.keys():
        if key not in affected:
            require(np.array_equal(actual[key], baseline[key]), 'Unrelated/road pack record changed: ' + repr(key))
        else:
            require(np.array_equal(actual[key][..., 1], baseline[key][..., 1]), 'Original shade changed: ' + repr(key))
for path, digest in manifest['protected_runtime'].items():
    require(sha(R / path) == digest, 'Protected runtime source changed: ' + path)
require(fractional_texels > 0, 'Contact masks lost their fractional feather')
report = dict(pass_=not errors, pack=str(pack), sha256=sha(pack), records=len(actual),
              baseline_pack=str(args.baseline_pack) if args.baseline_pack else None,
              baseline_sha256=sha(args.baseline_pack) if args.baseline_pack else None,
              source_templates=len(manifest['assets']), reviewed_probes=probe_results,
              native_water_texels_exact=water_texels, fractional_coverage_texels=fractional_texels,
              native_water_baseline_texels_checked=baseline_water_texels,
              native_water_baseline_texels_changed=baseline_water_changed,
              protected_art_relief_and_depth_exact=not any(e.startswith(('Protected', 'Approved painting', 'Original matte')) for e in errors), errors=errors)
if args.output:
    args.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
sys.exit(0 if not errors else 1)
