#!/usr/bin/env python3
"""Save matching Desert road exits from existing Desert artwork and native masks."""
from pathlib import Path
import hashlib,json
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt,label
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def smooth(x):x=np.clip(x,0,1);return x*x*(3-2*x)
def build():
 art=ROOT/'art/remaster/textures/DESERT';out=ROOT/'art/remaster/textures/unified_daylight_v01/desert_road_exits_v02';out.mkdir(parents=True,exist_ok=True)
 feature=art/'2026-10-19/cliff_shore_repair_v07';rows=json.loads((feature/'FEATURE_MANIFEST.json').read_text())['assets'];meta=json.loads((art/'set_144.json').read_text())['templates']
 # Existing finely grained earth within D10, without stones or sand surround.
 src=art/'D10/final_144.png';patch=np.asarray(Image.open(src).convert('RGB'),float)[182:254,360:504];patch=np.tile(patch,(2,1,1))
 # A narrow periodic correction retains the source detail at opposite boundaries.
 for d in range(8):
  k=1-smooth(d/8);shared=(patch[:,d].copy()+patch[:,-1-d].copy())*.5;patch[:,d]+=(shared-patch[:,d])*k;patch[:,-1-d]+=(shared-patch[:,-1-d])*k
  shared=(patch[d].copy()+patch[-1-d].copy())*.5;patch[d]+=(shared-patch[d])*k;patch[-1-d]+=(shared-patch[-1-d])*k
 patch[:,-1]=patch[:,0];patch[-1]=patch[0];patch=np.uint8(np.rint(patch));Image.fromarray(patch).save(out/'exit_material.png');records=[]
 for row in rows:
  n=row['name']
  if not n.startswith('D'):continue
  w,h=row['cells'];have=set(row['have']);original=art/n/'final_144.png';a=np.asarray(Image.open(original).convert('RGB'));ground=np.asarray(Image.open(feature/row['export']/'ground_mask.png').convert('L'));road=ground<128;edge=np.zeros(road.shape,bool)
  for icon in have:
   cy,cx=divmod(icon,w)
   for dx,dy in [(-1,0),(1,0),(0,-1),(0,1)]:
    nx,ny=cx+dx,cy+dy
    if 0<=nx<w and 0<=ny<h and ny*w+nx in have:continue
    if dx:edge[cy*144:(cy+1)*144,cx*144+(143 if dx>0 else 0)]=True
    else:edge[cy*144+(143 if dy>0 else 0),cx*144:(cx+1)*144]=True
  # Detached native rock/shadow islands are decorations, never road exits.
  components,count=label(road,np.ones((3,3)));sizes=np.bincount(components.ravel());sizes[0]=0
  body=components==sizes.argmax()
  dist=distance_transform_edt(~edge);weight=(1-smooth(dist/12))*body
  yy,xx=np.indices(road.shape);guide=patch[yy%144,xx%144];b=np.uint8(np.rint(a*(1-weight[...,None])+guide*weight[...,None]));assert np.array_equal(a[weight==0],b[weight==0])
  dest=out/n;dest.mkdir(exist_ok=True);Image.fromarray(b).save(dest/'albedo.png');Image.fromarray(np.uint8(road)*255).save(dest/'road_mask.png');Image.fromarray(np.uint8(np.rint(weight*255))).save(dest/'exit_mask.png');Image.new('L',(w*144,h*144),0).save(dest/'player_colour_mask.png')
  files={p.name:sha(p)for p in dest.iterdir()if p.suffix=='.png'}
  records.append(dict(name=n,tid=row['tid'],cells=[w,h],have=row['have'],source=str(original.relative_to(ROOT)),source_sha256=sha(original),ground_mask=str((feature/row['export']/'ground_mask.png').resolve().relative_to(ROOT)),ground_mask_sha256=sha(feature/row['export']/'ground_mask.png'),files=files,changed_pixels=int(np.any(a!=b,axis=-1).sum()),ground_coverage_changed=False,geometry_changed=False,player_colour_pairing='all-black linear grayscale, same dimensions and UVs as albedo.png'))
 (out/'MANIFEST.json').write_text(json.dumps(dict(schema=1,source=str(src.relative_to(ROOT)),source_sha256=sha(src),source_crop=[360,182,504,254],material_sha256=sha(out/'exit_material.png'),reach_px=12,scope='Enhanced Desert external road exits only; original interiors, footprints and relief retained',assets=records),indent=2)+'\n');print(len(records),'roads')
if __name__=='__main__':build()
