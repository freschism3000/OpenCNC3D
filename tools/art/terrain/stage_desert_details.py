#!/usr/bin/env python3
"""Stage the coupled source-placed Desert meshes and alternate coverage masks."""
from pathlib import Path
import argparse,importlib.util,shutil,subprocess,os,sys

def stage(root,dest):
 dest=dest.resolve()
 # The installation receives the same freshly baked stones and explicit masks as
 # the renderer; leaving them only in game/ silently kept old Dock materials.
 subprocess.run(['sh',str(root/'tools/art/rocks/stones.sh'),str(dest),os.environ.get('ROCK_STONES','painted')],check=True)
 source=root/'art/remaster/desert_details_v02/runtime'
 spec=importlib.util.spec_from_file_location('desert_detail_compiler',root/'tools/art/terrain/desert_details.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 for p in source.glob('*.pack'):shutil.copy2(p,dest/p.name)
 materials=dest/'desert-details';materials.mkdir(exist_ok=True)
 for p in source.parent.glob('*.png'):
  if '_albedo' in p.name or '_player_mask' in p.name:shutil.copy2(p,materials/p.name)
 for name in ['manifest.json','instances.json','labels.json']:shutil.copy2(source.parent/name,materials/name)
 for name in ['groundmask_DESERT.pack','groundmask_SAND.pack']:
  if(dest/name).is_file():
   m.append_masks(dest/name,source/'desert_details_masks.pack',surface_source=root/'art/remaster/textures/unified_daylight_v01')
   # The decoration compiler reapplies older saved ground-contact minimums.
   # Compile connector borders after that last writer; SAND shares these exact
   # Desert coverage keys and shade bytes through its existing palette recipe.
   sys.path.insert(0,str(root/'tools/bakery'))
   from terrain_connectors import correct
   before=(dest/name).read_bytes();after=correct(before,'groundmask_DESERT.pack')
   if after!=before:(dest/name).write_bytes(after)
 print('staged source-placed Desert bushes and their matching coverage')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('destination',type=Path);a=p.parse_args();stage(Path(__file__).resolve().parents[3],a.destination)
