#!/usr/bin/env python3
"""Check exact native Enhanced RA road pixels and saved source-outline repairs."""
from pathlib import Path
import argparse,json,runpy,sys
import numpy as np
from PIL import Image

def run(root,install):
 sys.path.insert(0,str(root/'tools/bakery'))
 import road_continuity as roads
 helpers=runpy.run_path(str(root/'tools/gate-shared-cliffs.py'))
 meta=json.loads((root/'art/remaster/textures/RASNOW/set_144.json').read_text())['templates'];names={n:r['tid']for n,r in meta.items()}
 actual=helpers['colour'](install/'ra3d/terrainenh_RA_SNOW.pack',names);strips=helpers['seams'](install/'ra3d/terrainseam_RA_SNOW.pack',names)
 expected=roads.updates(root,'RASNOW','terrainenh');seams=roads.updates(root,'RASNOW','terrainseam')
 for key,pixels in expected.items():assert actual[key]==pixels,('face',key)
 for key,pixels in seams.items():assert strips[key]==pixels,('strip',key)
 source=root/'art/remaster/textures/ra_road_continuity_v02'; reports=[]
 import hashlib
 for bank,folder in [('RASNOW',source),('SNOW',source/'SNOW'),('WINTER',source/'WINTER')]:
  m=json.loads((folder/'MANIFEST.json').read_text());count=0
  assert hashlib.sha256((root/m['native_exits_source']).read_bytes()).hexdigest()==m['native_exits_sha256']
  for r in m['assets']:
   before=np.asarray(Image.open(root/r['source']).convert('RGB'));after=np.asarray(Image.open(folder/r['name']/'albedo.png').convert('RGB'));mask=np.asarray(Image.open(folder/r['name']/'exit_mask.png'))
   assert hashlib.sha256((root/r['source']).read_bytes()).hexdigest()==r['source_sha256'],r['name']
   assert np.array_equal(before[mask==0],after[mask==0]),(bank,r['name'],'outside repair')
   for filename,digest in r['files'].items():
    path=folder/r['name']/filename;assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,(bank,r['name'],filename)
    im=Image.open(path);assert im.size==tuple(x*144 for x in r['cells'])
   player=Image.open(folder/r['name']/'player_colour_mask.png');assert player.mode=='L' and not np.asarray(player).any()
   ground=Image.open(folder/r['name']/'ground_mask.png');assert ground.mode=='L'
   count+=len(r['exits'])
  assert count==m['contacts_repaired']
  reports.append(dict(theater=bank,templates=len(m['assets']),native_exits_audited=m['native_exits_audited'],contacts_repaired=count))
 return dict(passed=True,reports=reports,installed_cells=len(expected),installed_strips=len(seams),outside_saved_repairs_identical=True,hashes_and_mask_pairings_verified=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--playable',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();r=run(a.source,a.playable)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps(r,indent=2))
