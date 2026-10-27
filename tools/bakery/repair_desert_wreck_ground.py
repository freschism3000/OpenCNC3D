#!/usr/bin/env python3
"""Feather the existing P04 crash scar with its saved under-model mark plan.

The source RGB and wreck mesh remain exact. This exports world-ground coverage,
so the old hard painted soil perimeter cannot form an opaque decal on the sand.
"""
from pathlib import Path
import argparse
import hashlib
import json

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

CELL = 144


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def smooth(value):
    t = np.clip(value, 0, 1)
    return t*t*(3-2*t)


def coverage(plan):
    """Use original seeded mark boundaries, with an eight-texel inward fade."""
    yy, xx = (np.mgrid[:CELL, :CELL]+.5)/CELL
    def noise(sigma, seed):
        n = gaussian_filter(np.random.RandomState(seed).randn(CELL, CELL), sigma,
                            mode='wrap')
        return n/max(1e-6, n.std())
    seed = plan.get('seed', 0)
    wobble = .016*noise(CELL*.06, seed)+.007*noise(CELL*.02, seed+1)
    signed = np.full((CELL, CELL), 9.)
    for mark in plan['marks']:
        if 'at' in mark:
            d = np.hypot(xx-mark['at'][0], yy-mark['at'][1])-mark['r']
        else:
            points = np.asarray(mark['path'], float)
            widths = mark['width']
            if not isinstance(widths, list):
                widths = [widths]*len(points)
            d = np.full_like(signed, 9.)
            for i, (a, b) in enumerate(zip(points, points[1:])):
                delta = b-a
                t = np.clip(((xx-a[0])*delta[0]+(yy-a[1])*delta[1]) /
                            max(1e-9, delta@delta), 0, 1)
                half = (widths[i]+(widths[i+1]-widths[i])*t)/2
                d = np.minimum(d, np.hypot(xx-a[0]-t*delta[0],
                                          yy-a[1]-t*delta[1])-half)
        factor = 1.5 if mark['kind'] in ('burn', 'fringe', 'soil') else .5
        signed = np.minimum(signed, d+wobble*factor)
    # Inward feather keeps the painted polygon edge beneath pure world ground.
    weight = smooth(-signed*CELL/8.)
    pixel = np.arange(CELL)+.5
    border = np.minimum(pixel, CELL-pixel)
    border = smooth((border-8)/12)
    return np.uint8(np.rint(255*weight*np.minimum(border[:, None], border[None, :])))


def repair(project, original, out):
    source = original/'art/remaster/textures/DESERT/P04/final_144.png'
    plan_path = source.parent/'UNDER_MODEL_144'
    plan = json.loads(plan_path.read_text())
    meta = json.loads((source.parent.parent/'set_144.json').read_text())['templates']['P04']
    assert Image.open(source).size == (CELL, CELL)
    out.mkdir(parents=True, exist_ok=True)
    (out/'albedo.png').write_bytes(source.read_bytes())
    retained = coverage(plan)
    Image.fromarray(255-retained).save(out/'ground_mask.png')
    Image.fromarray(retained).save(out/'scar_coverage.png')
    for filename in ('height.png', 'rock_mask.png', 'player_colour_mask.png'):
        Image.new('L', (CELL, CELL), 0).save(out/filename)
    Image.new('L', (CELL, CELL), 255).save(out/'land_alpha.png')
    (out/'UNDER_MODEL_144.json').write_bytes(plan_path.read_bytes())
    assert not retained[:8].any() and not retained[-8:].any()
    assert not retained[:, :8].any() and not retained[:, -8:].any()
    assert (retained == 255).sum() > 1000 and ((retained > 0)&(retained < 255)).sum() > 1000
    surface_root = project/'art/remaster/textures/unified_daylight_v01'
    row = dict(theater='DESERT', name='P04', tid=70, variant=0,
               cells=[1, 1], have=[0], export=str(out.relative_to(surface_root)),
               margins=[], source=str(source.relative_to(original)),
               source_sha256=sha(source), rock_pixels=0,
               material_recipe='Existing crash-scar RGB, preserved byte-exact; saved under-model mark plan with eight-texel inward smooth coverage to continuous world sand. No model geometry or albedo colour change.',
               surface_kind='under-model-ground', under_model_plan_sha256=sha(plan_path))
    for filename in ('albedo', 'height', 'rock_mask', 'ground_mask', 'land_alpha', 'player_colour_mask'):
        row[filename+'_sha256'] = sha(out/(filename+'.png'))
    (out/'ROW.candidate.json').write_text(json.dumps(row, indent=2)+'\n')
    report = dict(source_sha256=sha(source), plan_sha256=sha(plan_path),
                  albedo_byte_exact=sha(source)==sha(out/'albedo.png'),
                  full_scar_pixels=int((retained==255).sum()),
                  transition_pixels=int(((retained>0)&(retained<255)).sum()),
                  pure_world_ground_pixels=int((retained==0).sum()),
                  player_colour='Explicit all-black linear mask; intentionally non-recolourable terrain.',
                  geometry_changed=False, relief_added=False)
    (out/'REPORT.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    repair(args.project.resolve(), args.original.resolve(), args.out.resolve())
