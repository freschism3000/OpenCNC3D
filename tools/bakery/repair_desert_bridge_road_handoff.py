#!/usr/bin/env python3
"""Join Desert bridge approaches to the shared native-road exit material.

Only the dry, previously retired approach regions within one cell of the
exterior stamp boundary change. Decks, masonry, rock, water and geometry stay
exact. The handoff fades into the existing quiet approach tracks inland.
"""
from pathlib import Path
import argparse,hashlib,json,shutil
import numpy as np
from PIL import Image
from scipy import ndimage as nd

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def smooth(t):t=np.clip(t,0,1);return t**3*(t*(t*6-15)+10)
def repair(root):
 d=root/'art/remaster/textures/unified_daylight_v01';path=d/'MANIFEST.json';manifest=json.loads(path.read_text());out=d/'desert_bridge_road_handoff_v12';out.mkdir(exist_ok=True)
 source=root/'art/remaster/textures/DESERT/2026-10-19/cliff_shore_repair_v07';features={r['name']:r for r in json.loads((source/'FEATURE_MANIFEST.json').read_text())['assets']if not r['variant']}
 material_path=d/'desert_road_exits_v02/exit_material.png';material=np.array(Image.open(material_path).convert('RGB'));report=[]
 for row in manifest['assets']:
  name=row['name']
  if row['theater']!='DESERT' or name not in ['BRIDGE3','BRIDGE4'] or row['variant']:continue
  prior=row.get('pre_road_handoff_v12_export',row['export']);folder=d/prior;dest=out/name;dest.mkdir(exist_ok=True)
  for p in folder.glob('*.png'):shutil.copy2(p,dest/p.name)
  get=lambda n:np.array(Image.open(folder/(n+'.png')).convert('L'))
  rgb=np.array(Image.open(folder/'albedo.png').convert('RGB'));ground=get('ground_mask');alpha=get('land_alpha');rock=get('rock_mask')>127;masonry=get('masonry_semantic_mask')>0;deck=get('deck_semantic_mask')>0
  allowed=(get('approach_semantic_mask')>0)&(alpha==255)&~rock&~masonry&~deck
  original=np.array(Image.open(source/features[name]['export']/'ground_mask.png').convert('L'))
  road=original<128;inside=nd.distance_transform_edt(np.pad(road,12,mode='edge'))[12:-12,12:-12];coverage=smooth((inside-.5)/8)
  w,h=row['cells'];have=np.zeros(ground.shape,bool)
  for icon in row['have']:
   z,x=divmod(icon,w);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
  distance=nd.distance_transform_edt(np.pad(have,1))[1:-1,1:-1];weight=(1-smooth(distance/144))*allowed
  support=weight>0;yy,xx=np.indices(ground.shape);target=material[yy%144,xx%144]
  new_rgb=np.uint8(np.rint(rgb*(1-weight[...,None])+target*weight[...,None]));new_ground=np.uint8(np.rint(ground*(1-weight)+(255*(1-coverage))*weight))
  # Frozen decoration masks are rebased through the exact authored override last.
  override=get('ground_override_mask')>0;override|=support;values=np.zeros_like(ground);values[override]=new_ground[override]
  minimum=get('ground_minimum_mask');minimum[support]=0;protected=get('crossing_protected_mask');protected[support]=0
  outputs={'albedo.png':new_rgb,'ground_mask.png':new_ground,'ground_override_mask.png':np.uint8(override)*255,'ground_override_values.png':values,'ground_minimum_mask.png':minimum,'crossing_protected_mask.png':protected,'road_handoff_weight.png':np.uint8(np.rint(weight*255))}
  for name_,a in outputs.items():Image.fromarray(a).save(dest/name_)
  for a,b in [(new_rgb,rgb),(new_ground,ground)]:assert np.array_equal(a[~support],b[~support])
  for file in ['height.png','rock_mask.png','land_alpha.png','player_colour_mask.png','deck_semantic_mask.png','masonry_semantic_mask.png']:assert sha(folder/file)==sha(dest/file)
  row['pre_road_handoff_v12_export']=prior;row['export']=str(dest.relative_to(d));row['road_handoff_material_source']=str(material_path.relative_to(root));row['road_handoff_material_sha256']=sha(material_path)
  for file in outputs:
   if file=='crossing_protected_mask.png':continue
   row[file[:-4]+'_sha256']=sha(dest/file)
  for margin in row['margins']:
   margin['file']=str((dest/Path(margin['file']).name).relative_to(d));margin['sha256']=sha(d/margin['file'])
  report.append(dict(name=name,source_export=prior,support_pixels=int(support.sum()),rgb_changed=int(np.any(new_rgb!=rgb,axis=-1).sum()),ground_changed=int((new_ground!=ground).sum()),geometry_albedo_outside_support_coast_and_masonry_exact=True,source_native_road_mask=str((source/features[name]['export']/'ground_mask.png').resolve().relative_to(root.resolve())),source_native_road_mask_sha256=sha(source/features[name]['export']/'ground_mask.png')))
 (out/'REPORT.json').write_text(json.dumps(dict(schema=1,source_pixels_per_cell=144,handoff_cells=1,material_source=str(material_path.relative_to(root)),material_sha256=sha(material_path),scope=__doc__,templates=report),indent=2)+'\n');path.write_text(json.dumps(manifest,indent=2)+'\n');print(report)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();repair(a.root)
