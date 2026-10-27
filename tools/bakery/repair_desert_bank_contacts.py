#!/usr/bin/env python3
"""Feather the literal straight cut lines in SH20 and SH52's source rock painting.

This is a bounded raster contact edit. Preserve original RGB, physical relief,
coast, all other rock perimeters, mineral cores and explicit player-colour masks.
"""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from PIL import Image
REGIONS={'SH20':[('bottom',68,14,[255,49,513,70]),('left',260,12,[259,8,274,70])],
         'SH52':[('right',465,12,[451,49,467,168]),('bottom',168,12,[438,154,467,169])]}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def smooth(t):t=np.clip(t,0,1);return t**3*(t*(t*6-15)+10)
def repair(root):
 d=root/'art/remaster/textures/unified_daylight_v01';path=d/'MANIFEST.json';manifest=json.loads(path.read_text());out=d/'desert_bank_contacts_v01';out.mkdir(exist_ok=True);report=[]
 coast=root/'art/remaster/textures/DESERT/2026-10-18/cliffs_shores_v04';coasts={r['name']:r for r in json.loads((coast/'COAST_MANIFEST.json').read_text())['templates']}
 for row in manifest['assets']:
  name=row['name']
  if row['theater']!='DESERT' or name not in REGIONS or row['variant']:continue
  folder=d/row['export'];ground_path=folder/'ground_mask.png';rock_path=folder/'rock_mask.png';ground=np.array(Image.open(ground_path).convert('L'));rock=np.array(Image.open(rock_path).convert('L'))>127;alpha_path=coast/coasts[name]['land_alpha'];alpha=np.array(Image.open(alpha_path).convert('L'));yy,xx=np.indices(ground.shape);weight=np.zeros(ground.shape)
  for side,line,width,box in REGIONS[name]:
   distance={'bottom':line-yy,'right':line-xx,'left':xx-line}[side];x0,y0,x1,y1=box;support=(xx>=x0)&(xx<x1)&(yy>=y0)&(yy<y1)&rock&(alpha==255)
   weight=np.maximum(weight,(1-smooth(np.maximum(0,distance)/width))*support)
  fixed=np.maximum(ground,np.uint8(np.rint(weight*255)));changed=fixed!=ground;file=out/(name+'_ground.png');Image.fromarray(fixed).save(file)
  row['ground_contact_file']=str(file.relative_to(d));row['ground_contact_sha256']=sha(file)
  report.append(dict(name=name,variant=row['variant'],cells=row['cells'],have=row['have'],regions=REGIONS[name],original_ground=str(ground_path.relative_to(root)),original_ground_sha256=sha(ground_path),rock_source=str(rock_path.relative_to(root)),rock_source_sha256=sha(rock_path),coast_source=str(alpha_path.relative_to(root)),coast_source_sha256=sha(alpha_path),ground_contact_file=row['ground_contact_file'],ground_contact_sha256=sha(file),changed_pixels=int(changed.sum()),geometry_rgb_coast_and_other_perimeters_exact=True,player_colour_pairing=str((folder/'player_colour_mask.png').relative_to(root))))
 (out/'MANIFEST.json').write_text(json.dumps(dict(schema=1,source_pixels_per_cell=144,scope=__doc__,assets=report),indent=2)+'\n');path.write_text(json.dumps(manifest,indent=2)+'\n');print('Exported bounded bank contacts:',len(report))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();repair(a.root)
