#!/usr/bin/env python3
"""Repair only saved dry approach paint; preserve native bridge core and all relief."""
from pathlib import Path
import argparse,json,hashlib,copy
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd
REGIONS={
'BRIDGE3': [[(0,600),(120,465),(145,478),(360,515),(200,720),(0,720)],[(432,0),(720,0),(720,148),(650,195),(435,135)]],
'BRIDGE4': [[(144,0),(248,0),(407,103),(382,122),(180,168),(144,144)],[(470,483),(643,377),(670,388),(863,542),(864,576),(640,576),(517,541)]],
'FORD1': [[(0,103),(87,89),(148,116),(162,153),(158,221),(137,253),(96,283),(0,303)],[(270,83),(353,106),(432,125),(432,279),(346,309),(271,319),(243,288),(252,246),(261,180),(246,141)]]}
TRACKS={'BRIDGE3':[[(0,704),(236,506)],[(675,0),(552,143)]], 'BRIDGE4':[[(175,0),(282,117)],[(555,452),(738,576)]], 'FORD1':[[(0,204),(150,180)],[(270,187),(431,202)]]}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,a):Image.fromarray(np.uint8(np.rint(np.clip(a,0,1)*255))).save(p)
def repair(project,original,out,apply=False):
 d=project/'art/remaster/textures/unified_daylight_v01';m=json.loads((d/'MANIFEST.json').read_text());out.mkdir(parents=True,exist_ok=True);report=[]
 grainpath=original/'art/remaster/textures/DESERT/2026-10-18/clear_v03/sources/CLEAR1/v01/CLEAR1_v01_144.png';grain=np.array(Image.open(grainpath).convert('RGB'),float)/255
 region=out/'approach_regions.json';region.write_text(json.dumps(dict(units='whole-template native144px/cell coordinates',regions=REGIONS,track_centrelines=TRACKS,grain_source=str(grainpath.relative_to(original)),grain_source_sha256=sha(grainpath)),indent=2)+'\n')
 for r in m['assets']:
  if r['theater']!='DESERT' or r['name']not in REGIONS or r['variant']:continue
  name=r['name'];base_export=r.get('pre_approaches_v06_export',r['export']);old=d/base_export;rgb=np.array(Image.open(old/'albedo.png').convert('RGB'),float)/255;ground=np.array(Image.open(old/'ground_mask.png'),float)/255;rock=np.array(Image.open(old/'rock_mask.png'))>127;alpha=np.array(Image.open(old/'land_alpha.png'));height=np.array(Image.open(old/'height.png'));protected=np.array(Image.open(old/'crossing_protected_mask.png'))>127;h,w=height.shape;yy,xx=np.indices((h,w));sel=Image.new('1',(w,h));draw=ImageDraw.Draw(sel)
  for poly in REGIONS[name]:draw.polygon(poly,fill=1)
  have=np.zeros((h,w),bool)
  for icon in r['have']:
   iz,ix=divmod(icon,r['cells'][0]);have[iz*144:(iz+1)*144,ix*144:(ix+1)*144]=1
  dry=(alpha==255)&~nd.binary_dilation(rock,iterations=6)&have;sel=np.array(sel,bool)&dry
  # A smooth coverage bridge to the exact world material removes ragged sprite
  # blocks. Keep subtle compacted tracks, with no broad baked slope lighting.
  feather=np.clip(nd.distance_transform_edt(sel)/16,0,1);feather=feather*feather*(3-2*feather);distance=np.full((h,w),1e6)
  for a,b in TRACKS[name]:
   ax,ay=a;bx,by=b;vx=bx-ax;vy=by-ay;t=np.clip(((xx-ax)*vx+(yy-ay)*vy)/(vx*vx+vy*vy),0,1);distance=np.minimum(distance,np.sqrt((xx-ax-t*vx)**2+(yy-ay-t*vy)**2))
  track=np.exp(-.5*(distance/36)**2)*.38;fine=grain[(yy+83)%grain.shape[0],(xx+47)%grain.shape[1]];luma=fine@np.array([.2126,.7152,.0722]);fine=(fine*.83+luma[...,None]*.17)*[.86,.85,.83]
  # Fine source-led ruts only: discard its old broad shadow and flat polygon bands.
  oldlum=rgb@np.array([.2126,.7152,.0722]);detail=np.clip(nd.gaussian_filter(oldlum,1)-nd.gaussian_filter(oldlum,8),-.06,.06);fine=np.clip(fine+detail[...,None]*.35,0,1)
  result=rgb*(1-feather[...,None])+fine*feather[...,None];groundnew=ground*(1-feather)+(1-track)*feather
  dest=out/name;dest.mkdir(exist_ok=True)
  for f in old.iterdir():
   if f.is_file()and f.suffix=='.png':(dest/f.name).write_bytes(f.read_bytes())
  write(dest/'albedo.png',result);write(dest/'ground_mask.png',groundnew);write(dest/'approach_mask.png',feather);write(dest/'track_weight.png',track*feather);write(dest/'crossing_protected_mask.png',protected&~(feather>0))
  originalrgb=np.array(Image.open(original/r['source']).convert('RGB'));new=np.array(Image.open(dest/'albedo.png'));p=protected&~(feather>0);assert np.array_equal(originalrgb[p],new[p]);assert sha(dest/'height.png')==sha(old/'height.png') and sha(dest/'land_alpha.png')==sha(old/'land_alpha.png')
  previous=base_export;r['pre_approaches_v06_export']=previous;r['export']=str(dest.relative_to(d));r['approach_region_source']=str(region.relative_to(d));r['approach_region_sha256']=sha(region);r['approach_recipe']='Dry source-led approach regions only; existing CLEAR1 grain plus subtle compacted tracks; native bridge core, rock masks, coast alpha and height exact.'
  for fn,k in [('albedo.png','albedo_sha256'),('ground_mask.png','ground_mask_sha256')]:r[k]=sha(dest/fn)
  # Use compiled face RGB under every source strip without changing strip alpha.
  for mar in r['margins']:
   f=old/Path(mar['file']).name;q=dest/f.name;mar['file']=str(q.relative_to(d));mar['sha256']=sha(q)
  report.append(dict(name=name,changed_pixels=int(np.any(new!=np.array(Image.open(old/'albedo.png')),axis=-1).sum()),approach_support_pixels=int((feather>0).sum()),bridge_or_crossing_core_pixels_exact=int(p.sum()),height_sha256=sha(dest/'height.png'),coast_sha256=sha(dest/'land_alpha.png'),height_coast_rock_unchanged=all(sha(dest/f)==sha(old/f)for f in ['height.png','land_alpha.png','rock_mask.png'])))
 (out/'MANIFEST.candidate.json').write_text(json.dumps(m,indent=2)+'\n');(out/'REPORT.json').write_text(json.dumps(dict(templates=report,grain_sha256=sha(grainpath),canonical_manifest_updated=apply),indent=2)+'\n')
 if apply:(d/'MANIFEST.json').write_text(json.dumps(m,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--project',type=Path,required=True);p.add_argument('--original',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--apply-manifest',action='store_true');a=p.parse_args();repair(a.project,a.original,a.out or a.project/'art/remaster/textures/unified_daylight_v01/desert_crossings_v06',a.apply_manifest)
