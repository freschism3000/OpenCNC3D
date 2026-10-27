#!/usr/bin/env python3
"""Hidden GDI1 coast regression using source wet pixels and projected land ownership.

Six camera perturbations retain the established blue-intrusion check. The native
1200px view then verifies true water independently of the opaque mask, while
preserving the actual opaque bank/island pixels inside the rectangular crop.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import re
import struct
import subprocess
import numpy as np
from PIL import Image


def buffer(directory, kind):
    raw = gzip.open(directory / f'capture-{kind}.bin.gz', 'rb').read()
    width, height, channels = struct.unpack_from('<III', raw)
    return np.frombuffer(raw, dtype='<f4', offset=12).reshape(
        height, width, 1 if channels == 1 else 4)[::-1]


def analyze_semantics(directory, field_path=None, opaque_path=None):
    field_path = field_path or directory / 'field16.bin'
    opaque_path = opaque_path or directory / 'opaque.bin'
    normal, depth = buffer(directory, 'normals'), buffer(directory, 'depth')[..., 0]
    height, width = depth.shape
    assert (width, height) == (1200, 800)
    image = np.asarray(Image.open(directory / 'shore.png').convert('RGB'), float)
    assert np.array_equal(image, np.asarray(Image.open(directory / 'after.png').convert('RGB'), float)), 'Diagnostic frame changed'
    raw = field_path.read_bytes()
    assert raw[:8] == b'C3DFLD16'
    version, fw, fh, resolution, distance_range = struct.unpack_from('<5I', raw, 8)
    assert version == 1 and distance_range == 16
    field = np.frombuffer(raw, dtype='<u2', offset=28).reshape(fh, fw, 4) / 65535.
    raw = opaque_path.read_bytes()
    assert raw[:8] == b'C3DOPAQ1'
    version, ow, oh = struct.unpack_from('<3I', raw, 8)
    assert (version, ow, oh) == (1, width, height)
    opaque = np.frombuffer(raw, dtype=np.uint8, offset=20).reshape(height, width, 4)[::-1, :, 3] > 127
    matrices = json.loads((directory / 'capture-matrices.json').read_text())
    matrix = np.asarray(matrices['inverse_vp']).reshape(4, 4, order='F')
    yy, xx = np.mgrid[285:345, 350:780]
    clip = np.stack(((xx + .5) / width * 2 - 1, 1 - (yy + .5) / height * 2,
                     depth[yy, xx] * 2 - 1, np.ones_like(xx)), axis=-1) @ matrix.T
    world = clip[..., :3] / clip[..., 3, None]
    fx, fz = world[..., 0] * resolution - .5, world[..., 2] * resolution - .5
    ix, iz = np.floor(fx).astype(int), np.floor(fz).astype(int)
    tx, tz = fx - ix, fz - iz
    assert (ix >= 0).all() and (iz >= 0).all() and (ix < fw - 1).all() and (iz < fh - 1).all()
    sampled = ((field[iz, ix] * (1 - tx)[..., None] + field[iz, ix + 1] * tx[..., None]) * (1 - tz)[..., None]
               + (field[iz + 1, ix] * (1 - tx)[..., None] + field[iz + 1, ix + 1] * tx[..., None]) * tz[..., None])
    signed_distance = (sampled[..., 0] - .5) * 2 * distance_range
    # Source wet selection never excludes the opaque mask. A wrong CLEAR1 overlay
    # or a mask that claims the water must fail this independent control.
    source_wet = (sampled[..., 3] >= .999) & (signed_distance > .30)
    water = (normal[yy, xx, 3] > .26) & (normal[yy, xx, 3] < .30)
    owned = opaque[yy, xx]
    log = (directory / 'run.log').read_text()
    levels = re.search(r'WATER\|plane\|.*?y=([-.\d]+)\.\.([-.\d]+)', log)
    level_source = directory / 'run.log'
    if levels is None and field_path.parent != directory:
        # A pre-plane renderer is measured against the explicitly supplied
        # current source field and its independently logged GDI1 body authority.
        level_source = field_path.parent / 'run.log'
        levels = re.search(r'WATER\|plane\|.*?y=([-.\d]+)\.\.([-.\d]+)', level_source.read_text())
    assert levels and abs(float(levels[1]) - float(levels[2])) < .001, 'GDI1 must have one horizontal body'
    plane = float(levels[1])
    bed_error = np.abs(world[..., 1] - (plane - .04))
    metrics = dict(size=[width, height], semantic_roi=[350, 285, 780, 345],
                   source_wet_pixels=int(source_wet.sum()),
                   source_wet_nonwater_pixels=int((source_wet & ~water).sum()),
                   source_wet_opaque_pixels=int((source_wet & owned).sum()),
                   source_wet_nonflat_bed_pixels=int((source_wet & (bed_error > .02)).sum()),
                   opaque_pixels=int(owned.sum()), nonwater_material_pixels=int((~water).sum()),
                   unowned_pixels=int((~owned).sum()),
                   unowned_nonwater_pixels=int((~owned & ~water).sum()),
                   water_on_opaque_pixels=int((water & owned).sum()),
                   water_material_pixels=int(water.sum()),
                   above_plane_depth_pixels=int((water & (world[..., 1] > plane + .02)).sum()),
                   legacy_green_hue_pixels=int(((image[yy, xx, 1] > image[yy, xx, 0] * 1.10)
                                                & (image[yy, xx, 1] > image[yy, xx, 2] * 1.08)).sum()),
                   pre_fix_green_hue_pixels=16804, matched_diagnostic_frame=True,
                   field16_path=str(field_path), opaque_mask_path=str(opaque_path),
                   plane_level_log=str(level_source),
                   field16_sha256=hashlib.sha256(field_path.read_bytes()).hexdigest(),
                   opaque_mask_sha256=hashlib.sha256(opaque_path.read_bytes()).hexdigest(),
                   selection='Independent source alpha>=.999 and sd>.30; not masked by opaque ownership. Unowned crop pixels must also be water.')
    errors = []
    if metrics['source_wet_pixels'] < 5000:
        errors.append('Insufficient independently selected wet interior pixels')
    if metrics['unowned_pixels'] < 15000:
        errors.append('Opaque mask consumes too much of the known water crop')
    for name in ('source_wet_nonwater_pixels', 'source_wet_opaque_pixels',
                 'source_wet_nonflat_bed_pixels', 'unowned_nonwater_pixels',
                 'water_on_opaque_pixels', 'above_plane_depth_pixels'):
        if metrics[name]:
            errors.append(f'{name}: {metrics[name]}')
    metrics.update(passed=not errors, errors=errors)
    return metrics


def run(a):
    d, o = a.playable.resolve(), a.output.resolve()
    o.mkdir(parents=True, exist_ok=True)
    (o / 'saves').mkdir(exist_ok=True)
    lines = ['gfxmode 2', 'gfx texset 3', 'gfx show_stats 0', 'reveal 1', 'gfx grass 0',
             'gfx cloud_on 0', 'gfx fog_fx 0', 'gfx bloom_on 0', 'gfx tree3d_wind 0',
             'gfx fol_wind 0', 'dist 1920', 'tick 2']
    poses = [(58, 59.5), (58.004, 59.5), (58, 59.504), (57.996, 59.5), (58, 59.496), (58, 59.5)]
    for i, (x, z) in enumerate(poses):
        lines += [f'cam {x} {z}', f'shot {o}/{i}.png', 'tick 1']
    lines += ['quit']
    script = o / 'commands.txt'
    script.write_text('\n'.join(lines) + '\n')
    args = [str(d / 'cnc_eyes'), '--nosound', '--enhanced', '--gfx', str(a.cfg.resolve()),
            '--noshroud', '--nosidebar', '--nominimap', '--w', '2400', '--h', '1600',
            '--scen', 'SCG01EA', '--pack', 'SCG01EA.pack', '--dir', 'missions/',
            '--dylib', 'TiberianDawn.dylib', '--content', 'content/', '--savedir', str(o / 'saves'),
            '--gfxsave', str(o / 'discard.cfg'), '--script', str(script)]
    env = {**os.environ, 'OPENCNC3D_BACKGROUND': '1', 'SDL_MAC_BACKGROUND_APP': '1'}
    with (o / 'run.log').open('w') as log:
        result = subprocess.run(args, cwd=d, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    log = (o / 'run.log').read_text()
    assert result.returncode == 0 and re.search(r'SCRIPT\|end .*0 failures', log), log[-2000:]
    assert 'TERRAINSURFACE|built|TEMPERAT' in log
    counts = []
    for i in range(len(poses)):
        image = np.asarray(Image.open(o / f'{i}.png').convert('RGB'), float)
        assert image.shape == (1600, 2400, 3)
        red, green, blue = image[420:500, 600:1500].transpose(2, 0, 1)
        count = int(((blue > red * 1.025) & (blue > green * .97) & (blue > 35)).sum())
        counts.append(count)
        assert count < 1500, ('coplanar water reappeared on the coastal land', i, count)
    report = dict(passed=True, scenario='SCG01EA', frames=len(poses),
                  native_flat_coast_roi=[600, 420, 1500, 500], blue_intrusions=counts,
                  pre_fix_same_cfg_intrusions=12944, threshold=1500, hidden=True)
    small = o / 'small'
    small.mkdir(exist_ok=True)
    small_lines = ['gfxmode 2', 'gfx texset 3', 'gfx show_stats 0', 'reveal 1', 'gfx grass 0',
                   'gfx cloud_on 0', 'gfx fog_fx 0', 'gfx bloom_on 0', 'gfx tree3d_wind 0',
                   'gfx fol_wind 0', 'dist 1920', 'tick 2', f'shot {small}/shore.png',
                   f'renderdump {small}/capture', f'shot {small}/after.png', 'quit']
    (small / 'commands.txt').write_text('\n'.join(small_lines) + '\n')
    args[args.index('--w') + 1] = '1200'
    args[args.index('--h') + 1] = '800'
    args[args.index('--script') + 1] = str(small / 'commands.txt')
    env.update(OPENCNC3D_WATER_FIELD16_DUMP=str(small / 'field16.bin'),
               OPENCNC3D_WATER_DUMP=str(small / 'field8.raw'),
               OPENCNC3D_WATER_OPAQUE_DUMP=str(small / 'opaque.bin'))
    with (small / 'run.log').open('w') as log:
        result = subprocess.run(args, cwd=d, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    assert result.returncode == 0 and re.search(r'SCRIPT\|end .*0 failures', (small / 'run.log').read_text())
    report['small_view'] = analyze_semantics(small)
    report['passed'] = report['small_view']['passed']
    (o / 'RESULTS.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--playable', type=Path)
    mode.add_argument('--analyze-only', type=Path, help='Analyze saved semantic capture without launching a game')
    parser.add_argument('--cfg', type=Path)
    parser.add_argument('--field16', type=Path, help='Explicit independent source field for a historical negative control')
    parser.add_argument('--opaque-mask', type=Path, help='Explicit matched current owner mask for a historical negative control')
    parser.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    if options.analyze_only:
        options.output.mkdir(parents=True, exist_ok=True)
        result = analyze_semantics(options.analyze_only, options.field16, options.opaque_mask)
        (options.output / 'RESULTS.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result))
        raise SystemExit(0 if result['passed'] else 1)
    if options.cfg is None:
        parser.error('--cfg is required with --playable')
    raise SystemExit(run(options))
