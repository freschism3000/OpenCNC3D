#!/usr/bin/env python3
"""Repair saved Desert crossing banks using explicit source-led rock regions.

The crossing/arch pixels stay immutable. Region polygons are recorded in native
whole-template pixel coordinates and intersect the accepted feature and coast
mattes; no diffuse colour test chooses runtime rock, land, or bridge geometry.
"""
from pathlib import Path
import argparse, hashlib, json, shutil
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as nd

LUMA=np.array([.2126,.7152,.0722])
REGIONS={
 'FORD1':[
  [(10,0),(90,0),(132,58),(128,87),(79,97),(9,52)],
  [(14,68),(51,66),(59,93),(36,105),(13,96)],
  [(40,105),(99,95),(128,116),(117,139),(63,158),(39,145)],
  [(81,334),(126,328),(158,362),(156,415),(129,432),(71,428),(78,388)],
  [(18,357),(55,354),(78,379),(64,415),(36,432),(16,423)],
  [(53,308),(74,308),(79,328),(60,336)]],
 'BRIDGE3':[
  [(130,263),(154,210),(182,188),(202,197),(223,158),(261,151),(296,171),(323,207),(344,245),(329,274),(293,302),(207,301),(170,285)],
  [(49,457),(64,420),(91,397),(124,407),(153,434),(167,465),(150,490),(94,500),(53,489)],
  [(622,307),(653,286),(681,296),(701,278),(737,293),(752,333),(785,344),(815,384),(808,414),(780,434),(741,433),(721,450),(668,445),(617,420),(602,378)],
  [(421,553),(440,511),(479,503),(516,536),(540,566),(514,593),(469,594),(437,575)],
  [(0,453),(27,444),(47,466),(40,493),(12,496)]],
 'BRIDGE4':[
  [(432,118),(448,96),(489,78),(512,57),(555,49),(591,64),(610,90),(644,53),(716,44),(757,69),(781,109),(787,168),(754,218),(703,246),(650,230),(611,207),(584,188),(552,198),(515,201),(484,187),(463,162)],
  [(9,326),(27,294),(68,287),(83,263),(105,255),(123,282),(128,310),(149,296),(165,280),(191,296),(208,330),(214,365),(185,385),(150,388),(110,382),(77,391),(35,386),(12,368)],
  [(676,348),(700,327),(716,291),(738,282),(761,300),(773,337),(809,346),(837,373),(827,399),(795,405),(766,391),(733,395),(703,380)],
  [(294,509),(314,493),(340,504),(357,523),(349,544),(314,547),(291,532)],
  [(370,478),(387,456),(414,459),(435,478),(423,495),(397,498)],
  [(359,534),(389,507),(419,521),(430,546),(414,561),(382,560)]]}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,a):Image.fromarray(np.uint8(np.rint(np.clip(a,0,1)*255))).save(p)
def repair(source,project,names):
 d=project/'art/remaster/textures/unified_daylight_v01';m=json.loads((d/'MANIFEST.json').read_text());rows={r['name']:r for r in m['assets']if r['theater']=='DESERT' and r['variant']==0}
 native=source/'art/remaster/textures/DESERT';meta=json.loads((native/'set_144.json').read_text())['templates'];f=native/'2026-10-19/cliff_shore_repair_v07';features={r['name']:r for r in json.loads((f/'FEATURE_MANIFEST.json').read_text())['assets']if r['variant']==0};coast=native/'2026-10-18/cliffs_shores_v04';coasts={r['name']:r for r in json.loads((coast/'COAST_MANIFEST.json').read_text())['templates']}
 mineral_path=project/'art/remaster/ROCKS/cliff_sandstone_v05/material.png';mineral=np.asarray(Image.open(mineral_path).convert('RGB'),float)/255@LUMA;mineral-=mineral.mean()-.5
 out=d/'desert_crossings_v05';out.mkdir(parents=True,exist_ok=True);report=[]
 region_path=out/'source_bank_regions.json';region_path.write_text(json.dumps(dict(units='native Enhanced pixels at144px/cell',source='immutable whole-template final_144.png',regions=REGIONS),indent=2)+'\n')
 for name in names:
  if name not in REGIONS:raise ValueError('No reviewed source region: '+name)
  t=meta[name];w,h=t['cells'];src=native/name/'final_144.png';rgb=np.asarray(Image.open(src).convert('RGB'),float)/255;yy,xx=np.indices(rgb.shape[:2]);have=np.zeros(rgb.shape[:2],bool)
  for icon in t['have']:z,x=divmod(icon,w);have[z*144:(z+1)*144,x*144:(x+1)*144]=1
  original_feature=np.asarray(Image.open(f/features[name]['export']/'ground_mask.png'))<128
  if name in rows and 'land_alpha_sha256'in rows[name]:alpha=np.asarray(Image.open(d/rows[name]['export']/'land_alpha.png'),float)/255
  else:alpha=np.asarray(Image.open(coast/coasts[name]['land_alpha']),float)/255
  selection=Image.new('1',(w*144,h*144));draw=ImageDraw.Draw(selection)
  for p in REGIONS[name]:draw.polygon(p,fill=1)
  selected=np.asarray(selection,bool)&have
  # A complete native boulder (including its warm faces) is one mineral surface.
  rock=selected&original_feature&(alpha>=250/255)
  outside=np.clip(nd.distance_transform_edt(np.pad(~rock,12,mode='edge'))[12:-12,12:-12]/5,0,1);ground=outside*outside*(3-2*outside)*alpha*have
  protected=original_feature&~rock&have
  ground[protected]=0
  y=rgb@LUMA;low=nd.gaussian_filter(y,14);broad=nd.gaussian_filter(y,42);detail=(y-low)*.60+(low-broad)*.25
  tex=mineral[(yy+t['tid']*311)%mineral.shape[0],(xx+t['tid']*137)%mineral.shape[1]]
  value=np.clip(.47+detail+(tex-.5)*.80,.22,.76);paint=np.clip(value[...,None]*[1.16,1.015,.80],0,1)
  near=nd.distance_transform_edt(~rock,return_distances=False,return_indices=True);paint=paint[near[0],near[1]]
  result=np.where((ground<.999)[...,None],paint,rgb);result[protected]=rgb[protected];result[alpha<250/255]=rgb[alpha<250/255];result[~have]=rgb[~have]
  rise=np.clip(nd.distance_transform_edt(rock)/18,0,1);height=nd.gaussian_filter(rise*.65,1)*rise;height[protected|~rock]=0
  dest=out/name;dest.mkdir(exist_ok=True)
  for fn,a in [('albedo.png',result),('rock_mask.png',rock),('ground_mask.png',ground),('height.png',height),('land_alpha.png',alpha),('bank_region_mask.png',selected),('crossing_protected_mask.png',protected)]:save(dest/fn,a)
  Image.new('L',(w*144,h*144),0).save(dest/'player_colour_mask.png')
  q=rows.get(name)
  if q is None:q=dict(name=name,variant=0,source=str(src.relative_to(source)),source_sha256=sha(src),cells=t['cells'],tid=t['tid'],have=t['have'],theater='DESERT');m['assets'].append(q)
  else:q.setdefault('pre_crossings_v05_export',q['export'])
  margins=[]
  for mar in t.get('margins',[]):
   p=native/name/mar['file'];im=np.array(Image.open(p).convert('RGBA'))
   if name=='FORD1':im[...,3]=0
   destmar=dest/Path(mar['file']).name;Image.fromarray(im,'RGBA').save(destmar);margins.append(dict(mar,file=str(destmar.relative_to(d)),sha256=sha(destmar)))
  q.update(export=str(dest.relative_to(d)),surface_kind='river-rock',rock_pixels=int(rock.sum()),margins=margins,material_recipe='Shared cliff sandstone on explicit complete crossing-bank rock regions; original crossing and bridge masonry retained with zero added relief; native coast alpha unchanged.',shared_mineral_detail_source=str(mineral_path.relative_to(project)),bank_region_source=str(region_path.relative_to(d)),bank_region_sha256=sha(region_path))
  for fn,key in [('albedo.png','albedo_sha256'),('rock_mask.png','rock_mask_sha256'),('ground_mask.png','ground_mask_sha256'),('height.png','height_sha256'),('player_colour_mask.png','player_colour_mask_sha256'),('land_alpha.png','land_alpha_sha256')]:q[key]=sha(dest/fn)
  report.append(dict(name=name,occupied_cells=len(t['have']),rock_pixels=int(rock.sum()),protected_crossing_pixels=int(protected.sum()),protected_albedo_exact=bool(np.array_equal(np.uint8(np.rint(result[protected]*255)),np.uint8(np.rint(rgb[protected]*255)))),protected_height_max=float(height[protected].max()) if protected.any()else 0,water_height_max=float(height[alpha<250/255].max())if(alpha<250/255).any()else 0))
 (d/'MANIFEST.json').write_text(json.dumps(m,indent=2)+'\n');(out/'REPORT.json').write_text(json.dumps(dict(source=str(source),material_sha256=sha(mineral_path),templates=report),indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--names',default='FORD1,BRIDGE3,BRIDGE4');a=p.parse_args();repair(a.source,a.project,a.names.split(','))
