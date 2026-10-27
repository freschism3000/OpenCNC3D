#!/usr/bin/env python3
"""Check P04's explicit world-ground blend and its installed base record."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

import numpy as np
from PIL import Image


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = args.source/'art/remaster/textures/unified_daylight_v01'
    folder = base/'desert_wreck_ground_v01/P04'
    row = json.loads((folder/'ROW.candidate.json').read_text())
    active = json.loads((base/'MANIFEST.json').read_text())['assets']
    assert [q for q in active if q['theater']=='DESERT' and q['name']=='P04'] == [row]
    source = args.original/row['source']
    assert sha(folder/'albedo.png') == sha(source) == row['source_sha256']
    for name in ('height', 'rock_mask', 'player_colour_mask'):
        assert Image.open(folder/(name+'.png')).getextrema() == (0, 0)
    assert Image.open(folder/'land_alpha.png').getextrema() == (255, 255)
    ground = np.array(Image.open(folder/'ground_mask.png'))
    assert ground.shape == (144, 144)
    assert np.all(ground[:8] == 255) and np.all(ground[-8:] == 255)
    assert np.all(ground[:, :8] == 255) and np.all(ground[:, -8:] == 255)
    # Independent samples: central burnt soil stays; far sand stays continuous.
    assert np.all(ground[86:92, 38:44] == 0)
    assert np.all(ground[24:38, 12:26] == 255)
    assert ((ground > 0)&(ground < 255)).sum() > 2000
    report = dict(passed=True, albedo_byte_exact=True,
                  no_new_geometry_or_relief=True, border_world_ground_exact=True,
                  soft_transition_pixels=int(((ground>0)&(ground<255)).sum()),
                  source_mask_sha256=sha(folder/'ground_mask.png'))
    if args.runtime:
        raw = (args.runtime/'groundmask_DESERT.pack').read_bytes()
        assert raw[:8] == b'C3DGRAS\0'
        version, cell, count = struct.unpack_from('<III', raw, 8)
        assert version == 1 and cell == 144
        start = 20+24*count
        found = []
        for index in range(count):
            key = struct.unpack_from('<HHBBH', raw, 20+24*index+16)
            if key == (70, 0, 0, 0, 0):
                data = np.frombuffer(raw, 'u1', 144*144*2,
                                     start+index*144*144*2).reshape(144,144,2)
                assert np.array_equal(data[...,0], ground)
                assert np.all(data[...,1] == 128)
                found.append(key)
        assert len(found) == 1
        report['installed_base_ground_exact'] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
