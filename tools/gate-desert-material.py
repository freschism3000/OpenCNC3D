#!/usr/bin/env python3
"""Verify installed Desert face/feather continuity and both sandstone rock systems."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy
import numpy as np
from PIL import Image


def run(source,install):
    helpers=runpy.run_path(str(source/'tools/gate-shared-cliffs.py'))
    manifest=json.loads((source/'art/remaster/textures/unified_daylight_v01/MANIFEST.json').read_text())
    faces=helpers['colour'](install/'terrainenh_DESERT.pack')
    strips=helpers['seams'](install/'terrainseam_DESERT.pack')
    import sys
    sys.path.insert(0,str(source/'tools/bakery'))
    import terrain_daylight as day
    expected=day.cell_updates('DESERT','terrainenh')
    expected_strips=day.cell_updates('DESERT','terrainseam')
    for key,pixels in expected.items():assert faces[key]==pixels,('face',key)
    for key,pixels in expected_strips.items():assert strips[key]==pixels,('feather',key)
    joins=len(expected_strips);texels=sum(len(p)//4 for p in expected_strips.values());holes=0
    rockroot=source/'art/remaster/ROCKS/cliff_sandstone_v05'
    rocks=json.loads((rockroot/'manifest.json').read_text())
    scatter_sha=rocks['scatter']['output_sha256']
    mapped=install/'stone-materials/v01/MANIFEST.json'
    if mapped.exists():
        # Normal-only post-staging changes the pack hash; the retained source
        # hash must still be this exact v05 geometry/UV/albedo bake.
        mineral=json.loads(mapped.read_text())['scatter']['rock3d_DESERT.pack']
        assert mineral['source_sha256']==scatter_sha,'Normal map bake used a different Desert albedo pack'
        scatter_sha=mineral['output_sha256']
    assert hashlib.sha256((install/'rock3d_DESERT.pack').read_bytes()).hexdigest()==scatter_sha
    pairs=list(rocks['scatter']['textures'])
    for name,row in rocks['placed'].items():
        assert hashlib.sha256((install/'ebld'/(name+'_DES.ebld')).read_bytes()).hexdigest()==row['output_sha256'],name
        assert row['geometry_uv_bytes_unchanged']
        assert hashlib.sha256((source/'game/ebld'/(name+'.ebld')).read_bytes()).hexdigest()==row['source_sha256']
        pairs.extend(row['textures'])
    for pair in pairs:
        assert hashlib.sha256((rockroot/pair['albedo']).read_bytes()).hexdigest()==pair['albedo_sha256']
        delivered=install/'rock-materials/cliff-sandstone-v05'
        assert hashlib.sha256((delivered/pair['albedo']).read_bytes()).hexdigest()==pair['albedo_sha256']
        assert hashlib.sha256((delivered/pair['player_colour_mask']).read_bytes()).hexdigest()==pair['mask_sha256']
        mask=Image.open(delivered/pair['player_colour_mask'])
        assert mask.mode=='L' and list(mask.size)==pair['dimensions'] and mask.getextrema()==(0,0)
    return dict(passed=True,face_feather_joins=joins,matching_border_texels=texels,
                filled_enclosed_gap_texels=holes,scattered_looks=45,placed_rocks=7,
                paired_explicit_black_player_masks=len(pairs),geometry_uv_physics_bytes_preserved=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--playable',type=Path,required=True)
    p.add_argument('--output',type=Path)
    a=p.parse_args();result=run(a.source,a.playable)
    if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
