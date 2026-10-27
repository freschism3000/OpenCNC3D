#!/usr/bin/env python3
"""Export source-road coverage with a soft contact on the continuous world sand.

Saved feature silhouettes and road core are the authority. No diffuse classifier,
geometry edit or runtime colour inference is used. Small attached decorations get
proportionally narrower feathers so their actual stone centres remain visible.
"""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from PIL import Image
from scipy import ndimage as nd

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def smooth(t):
 t=np.clip(t,0,1);return t*t*t*(t*(t*6-15)+10)
def soften(ground):
 source=np.asarray(ground);mask=source<128
 # Replicate template boundaries: the road continues into its neighbouring stamp.
 distance=nd.distance_transform_edt(np.pad(mask,12,mode='edge'))[12:-12,12:-12]
 labels,count=nd.label(mask,np.ones((3,3)));width=np.ones(mask.shape)
 for label in range(1,count+1):
  member=labels==label;width[member]=min(8.,max(1.5,float(distance[member].max())*.4))
 opacity=smooth((distance-.5)/width)
 return np.maximum(source,np.uint8(np.rint(255*(1-opacity))))
def repair(root,out):
 art=root/'art/remaster/textures/DESERT';features=art/'2026-10-19/cliff_shore_repair_v07'
 rows=json.loads((features/'FEATURE_MANIFEST.json').read_text())['assets'];out.mkdir(parents=True,exist_ok=True);records=[]
 for row in rows:
  if not row['name'].startswith('D'):continue
  name=row['name'];source=features/row['export']/'ground_mask.png'
  if name=='D29':source=root/'art/remaster/textures/unified_daylight_v01/desert_feature_edges_v02/D29/ground_mask.png'
  ground=np.array(Image.open(source).convert('L'));fixed=soften(ground)
  # Existing explicit stone keepers remain fully opaque.
  keeper=root/'art/remaster/textures/unified_daylight_v01/desert_feature_edges_v02'/name/'object_keep_mask.png'
  if keeper.exists():fixed[np.array(Image.open(keeper))>0]=0
  dest=out/name;dest.mkdir(exist_ok=True);Image.fromarray(fixed).save(dest/'ground_mask.png');Image.new('L',ground.shape[::-1],0).save(dest/'player_colour_mask.png')
  records.append(dict(name=name,tid=row['tid'],cells=row['cells'],have=row['have'],variant=0,game=0,
   source=str((art/name/'final_144.png').relative_to(root)),source_sha256=sha(art/name/'final_144.png'),
   original_ground=str(source.relative_to(root)),original_ground_sha256=sha(source),
   ground_mask=name+'/ground_mask.png',ground_mask_sha256=sha(dest/'ground_mask.png'),
   player_colour_mask=name+'/player_colour_mask.png',player_colour_mask_sha256=sha(dest/'player_colour_mask.png'),
   player_colour_pairing='Original albedo, same144px/cell UVs; intentionally non-recolourable terrain.',
   changed_pixels=int((fixed!=ground).sum()),transition_pixels=int(((fixed>0)&(fixed<255)).sum())))
 (out/'MANIFEST.json').write_text(json.dumps(dict(schema=1,recipe='Source coverage, inward C2 feather max8px; small forms scale to radius; native core and D29 stone keepers retained. No albedo/height/coast edit.',assets=records),indent=2)+'\n')
 print('Exported road contacts:',len(records))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();repair(a.root,a.root/'art/remaster/textures/unified_daylight_v01/desert_feature_edges_v03')
