#!/usr/bin/env python3
"""Detect black water fringes in a frozen, revealed shoreline renderdump.

Uses independently dumped projected opaque ownership and GPU water normals to
exclude the rock's own dark pixels. The calibrated Snow contact fixture uses a
0.04 RGB luminance threshold, with at most 0.1% dark outliers in a 12px band.
This tests that fixture's appearance, not arbitrary user lighting settings.
For shadowed cliff angles, --lighting-control explicitly measures matched renders
with AO and cast shadows off, preserving Lambert lighting and all water settings;
production frames remain separate visual evidence. Shadows are not water voids.
Native heights/holes and the optical slope are checked by the separate gates.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json
import numpy as np
from PIL import Image

spec=importlib.util.spec_from_file_location('ownership',Path(__file__).with_name('gate-water-normal-ownership.py'))
G=importlib.util.module_from_spec(spec);spec.loader.exec_module(G)

def check(o,lighting_control=False):
    n,_=G.read_buffer(o/'capture','normals')
    opaque=G.read_opaque(o/'opaque.bin',n.shape[:2]);near=opaque.copy()
    for _ in range(12):
        p=np.pad(near,1);near=np.logical_or.reduce([p[y:y+near.shape[0],x:x+near.shape[1]]for y in range(3)for x in range(3)])
    image_path=o/('lighting-control.png' if lighting_control else 'no-swash.png')
    image=np.asarray(Image.open(image_path).convert('RGB'),dtype=float)/255
    assert image.shape==n[...,:3].shape
    selected=near&~opaque&G.water_class(n)&~G.object_halo(n)
    count=int(selected.sum());assert count>1000,'Not enough visible shoreline water'
    luma=image@[.299,.587,.114];dark=int((selected&(luma<.04)).sum());ratio=dark/count
    return dict(capture=str(o),passed=ratio<=.001,water_band_pixels=count,near_black_pixels=dark,near_black_fraction=ratio,
                luma_percentiles=np.percentile(luma[selected],[0,1,5,50,95]).tolist(),
                lighting_control=lighting_control,image_sha256=hashlib.sha256(image_path.read_bytes()).hexdigest())

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--lighting-control',action='store_true',help='Use explicit matched AO/cast-shadow-off controls; preserve Lambert, water and scene settings');p.add_argument('captures',nargs='+',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reports=[check(o,a.lighting_control)for o in a.captures];result=dict(passed=all(r['passed']for r in reports),scope='Frozen revealed fixture; projected opaque mask and GPU water class; 12px contact band',reports=reports)
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return 0 if result['passed']else 1
if __name__=='__main__':raise SystemExit(main())
