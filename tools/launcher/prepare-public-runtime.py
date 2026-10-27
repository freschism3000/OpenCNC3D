#!/usr/bin/env python3
"""Finish only a marked fresh public package; preserve source/private authoring.

Drop runtime-unused authoring copies and the unused root RA HUD. Share SAND's
material donor only after exact pack hashes and both physical key streams agree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

MARKER='.opencnc3d-public-stage'
MARKER_BYTES=b'OpenCNC3D fresh public package v1\n'
AUTHORING=('stone-materials','desert-details','rock-materials')
ROOT=Path(__file__).resolve().parents[2]

def sha(path):
    with path.open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def keys(path,magic):
    with path.open('rb')as stream:
        head=stream.read(20)
        if len(head)!=20 or head[:8]!=magic:raise ValueError('Invalid material/surface header: '+str(path))
        version,cell,count=struct.unpack_from('<III',head,8)
        if version!=1 or not 8<=cell<=144 or not 1<=count<=8192:raise ValueError('Invalid material/surface layout: '+str(path))
        value=stream.read(count*24)
        if len(value)!=count*24:raise ValueError('Truncated material/surface keys: '+str(path))
        return cell,count,value

def prepare(stage):
    stage=stage.absolute()
    if stage.is_symlink() or not stage.is_dir() or stage.resolve()!=stage:
        raise ValueError('Public package stage must be a real directory without symlink ancestors')
    if stage==ROOT or ROOT.is_relative_to(stage):
        raise ValueError('The source tree or its ancestor is not a public package stage')
    for runtime in (Path.home()/'CNC3D/playable',Path.home()/'CNC3D-latest/playable'):
        for protected in (runtime,runtime.resolve()):
            if stage.is_relative_to(protected)or protected.is_relative_to(stage):
                raise ValueError('An installed runtime or overlapping directory is not a public package stage')
    marker=stage/MARKER
    if marker.is_symlink() or not marker.is_file() or marker.read_bytes()!=MARKER_BYTES:
        raise ValueError('Refusing an unmarked destination: only fresh public package stages may be reduced')
    # Validate everything before removing any owned package copy.
    for name in AUTHORING:
        path=stage/name
        if path.is_symlink() or (path.exists()and not path.is_dir()):raise ValueError('Invalid public authoring-copy path: '+str(path))
    hud=stage/'ra-enhanced-hud.pack'
    if hud.is_symlink() or (hud.exists()and not hud.is_file()):raise ValueError('Invalid root RA HUD copy')
    sand=stage/'terrainmaterial_SAND.pack';desert=stage/'terrainmaterial_DESERT.pack';donor=None
    if sand.exists()or sand.is_symlink():
        paths=[sand,desert,stage/'terrainsurface_SAND.pack',stage/'terrainsurface_DESERT.pack']
        if any(p.is_symlink()or not p.is_file()for p in paths):raise ValueError('SAND donor sharing requires four regular paired packs')
        sand_hash,desert_hash=sha(sand),sha(desert)
        layouts=[keys(sand,b'C3DMATL\0'),keys(desert,b'C3DMATL\0'),keys(paths[2],b'C3DSURF\0'),keys(paths[3],b'C3DSURF\0')]
        if (sand_hash!=desert_hash or layouts[0]!=layouts[1]or layouts[2]!=layouts[3]
            or layouts[0][1:]!=layouts[2][1:]):
            raise ValueError('Distinct SAND material/keys preserved; public donor sharing requires exact equality')
        donor=dict(sha256=desert_hash,bytes=sand.stat().st_size,material_cell=layouts[0][0],
                   physical_cell=layouts[2][0],key_count=layouts[0][1],keys_sha256=hashlib.sha256(layouts[0][2]).hexdigest())
    removed=[]
    for name in AUTHORING:
        path=stage/name
        if path.exists():shutil.rmtree(path);removed.append(name+'/')
    if hud.exists():hud.unlink();removed.append(hud.name)
    if donor:sand.unlink();removed.append(sand.name)
    marker.unlink()
    return dict(public_runtime_prepared=True,removed_package_copies=removed,sand_desert_identical_material=donor,
                source_private_authoring_and_all_retained_runtime_bytes_untouched=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',type=Path);args=parser.parse_args()
    print(json.dumps(prepare(args.stage),sort_keys=True))
