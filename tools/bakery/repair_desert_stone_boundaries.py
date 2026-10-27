#!/usr/bin/env python3
"""Candidate-only full source stone faces and exterior bridge-shadow blending.

No mesh, height, coast or UV change. Explicit source regions complete the v05
mineral masks, whose legacy feature intersection omitted warm rock faces.
"""
from pathlib import Path
import argparse,copy,hashlib,json,shutil
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd

LUMA=np.array([.2126,.7152,.0722])
ROCKS=[
 [(130,263),(154,210),(182,188),(202,197),(223,158),(261,151),(296,171),(323,207),(344,245),(329,274),(293,302),(207,301),(170,285)],
 [(49,457),(64,420),(91,397),(124,407),(153,434),(167,465),(150,490),(94,500),(53,489)],
 [(622,307),(653,286),(681,296),(701,278),(718,279),(734,287),(744,290),(758,299),(773,311),(780,329),(785,344),(815,384),(808,414),(780,434),(741,433),(721,450),(668,445),(617,420),(602,378)],
 [(421,553),(430,536),(441,525),(451,516),(462,508),(473,506),(486,511),(499,520),(516,536),(540,566),(514,593),(469,594),(437,575)],
 [(0,453),(27,444),(47,466),(40,493),(12,496)],
]
# Whole-source semantic keeper silhouettes, independently traced around the
# two reported wall ends. Other existing wall semantics stay conservative.
WALL_KEEP=[
 [(339,197),(368,191),(379,181),(391,173),(406,159),(421,155),(433,160),(434,167),(428,178),(429,185),(429,194),(418,207),(414,217),(403,229),(390,233),(387,240),(377,248),(375,257),(355,259),(340,254),(329,244),(329,229),(330,214)],
 [(357,467),(367,455),(386,448),(398,451),(399,440),(464,440),(464,500),(460,500),(444,496),(431,488),(420,484),(408,489),(398,494),(390,498),(382,499),(371,496),(362,491),(358,484)],
]
WALL_REVIEW=[[(315,143),(449,143),(449,263),(318,263)],
             [(339,478),(470,478),(470,540),(339,540)]]
SHADOW_RETIRE=[
 [(317,185),(365,180),(397,151),(416,147),(437,151),(446,161),(438,180),(445,203),(421,224),(388,252),(379,265),(322,264),(317,218)],
 [(341,482),(362,485),(382,496),(402,495),(420,482),(445,489),(466,498),(470,522),(454,525),(442,523),(426,523),(408,525),(390,527),(375,518),(356,515),(341,507)],
]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def polygons(shape,regions):
 im=Image.new('L',(shape[1],shape[0]));d=ImageDraw.Draw(im)
 for poly in regions:d.polygon(poly,fill=255)
 return np.asarray(im)>0
def smooth(x):
 x=np.clip(x,0,1);return x*x*(3-2*x)
def image(path,a):Image.fromarray(np.uint8(np.rint(np.clip(a,0,255)))).save(path)

def repair(project,original,out,base_export):
 d=project/'art/remaster/textures/unified_daylight_v01';manifest_path=d/'MANIFEST.json'
 manifest_bytes=manifest_path.read_bytes();manifest=json.loads(manifest_bytes)
 row=copy.deepcopy(next(r for r in manifest['assets']if r['theater']=='DESERT'and r['name']=='BRIDGE3'and r['variant']==0))
 base=d/base_export;dest=out/'BRIDGE3';dest.mkdir(parents=True,exist_ok=True)
 for p in base.glob('*.png'):shutil.copy2(p,dest/p.name)
 rgb=np.asarray(Image.open(base/'albedo.png').convert('RGB'));ground=np.asarray(Image.open(base/'ground_mask.png').convert('L'))
 oldrock=np.asarray(Image.open(base/'rock_mask.png'))>127;alpha=np.asarray(Image.open(base/'land_alpha.png'));height=np.asarray(Image.open(base/'height.png'))
 oldmasonry=np.asarray(Image.open(base/'masonry_semantic_mask.png'))>127;deck=np.asarray(Image.open(base/'deck_semantic_mask.png'))>127
 source=original/row['source'];raw=np.asarray(Image.open(source).convert('RGB'),float)/255;shape=ground.shape;have=np.zeros(shape,bool)
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 review=polygons(shape,WALL_REVIEW)&have
 keepers=nd.binary_dilation(polygons(shape,WALL_KEEP),iterations=2)&have
 masonry=(oldmasonry&~review)|keepers
 authored=polygons(shape,ROCKS)&have
 # Preserve every existing relief/material pixel. Complete source polygons add
 # all formerly omitted warm planes, without legacy hue/feature intersection.
 rock=oldrock|(authored&~masonry&(alpha>0)&~deck)
 assert not np.any((rock&~oldrock)&deck)
 yy,xx=np.indices(shape);material=project/'art/remaster/ROCKS/cliff_sandstone_v05/material.png'
 mineral=np.asarray(Image.open(material).convert('RGB'),float)/255@LUMA;mineral-=mineral.mean()-.5
 y=raw@LUMA;low=nd.gaussian_filter(y,14);broad=nd.gaussian_filter(y,42)
 detail=(y-low)*.60+(low-broad)*.25
 tex=mineral[(yy+row['tid']*311)%mineral.shape[0],(xx+row['tid']*137)%mineral.shape[1]]
 value=np.clip(.47+detail+(tex-.5)*.80,.22,.76)
 paint=np.clip(value[...,None]*[1.16,1.015,.80],0,1)*255
 # The feather exists only OUTSIDE a complete stone body. Recolouring is full
 # strength on all selected native planes; no ground is mixed into their RGB.
 distance=nd.distance_transform_edt(~rock)
 feather_support=(distance<=8)&have&(alpha>0)&~masonry&~deck
 rock_skirt=(distance<=20)&have&(alpha>0)&~masonry&~deck
 mineral_support=(rock|rock_skirt)&~masonry&~deck
 new_ground=ground.copy();new_ground[mineral_support]=np.uint8(np.rint(255*smooth(distance[mineral_support]/8)))
 result=rgb.copy();painted=np.uint8(np.rint(paint))
 result[rock&~oldrock&~masonry&~deck]=painted[rock&~oldrock&~masonry&~deck]
 exterior=feather_support&~rock
 result[exterior]=painted[exterior]
 # Retire the painted shadow footprint over a 12px contact transition. The
 # actual wall silhouette and all full-strength road core pixels are keepers.
 shadow=polygons(shape,SHADOW_RETIRE)&have&(alpha>0)&(height==0)&~masonry&~rock&~deck
 shadow&=~rock_skirt
 wall_distance=nd.distance_transform_edt(~masonry)
 weight=smooth(wall_distance/12)*shadow
 grainpath=d/'desert_bridge_approaches_v08/clear_grain_source.png';grain=np.asarray(Image.open(grainpath).convert('RGB'))
 target=grain[(yy+83)%grain.shape[0],(xx+47)%grain.shape[1]]
 result=np.uint8(np.rint(result*(1-weight[...,None])+target*weight[...,None]))
 new_ground=np.maximum(new_ground,np.uint8(np.rint(weight*255)))
 support=mineral_support|shadow
 # Any frozen approach minimum would otherwise resurrect the old mineral cut.
 minimum=np.asarray(Image.open(base/'ground_minimum_mask.png')).copy();minimum[mineral_support]=0
 minimum=np.maximum(minimum,np.uint8(np.rint(weight*255)))
 protected=np.asarray(Image.open(base/'crossing_protected_mask.png'))>127
 outputs={'albedo.png':result,'ground_mask.png':new_ground,'rock_mask.png':np.uint8(rock)*255,
  'masonry_semantic_mask.png':np.uint8(masonry)*255,'ground_minimum_mask.png':minimum,
  'crossing_protected_mask.png':np.uint8(protected&~support)*255,
  'complete_rock_semantic_mask.png':np.uint8(rock)*255,
  'boundary_support_mask.png':np.uint8(support)*255,
  'boundary_ground_override_mask.png':np.uint8(support)*255,
  'ground_override_mask.png':np.uint8(support)*255,
  'ground_override_values.png':new_ground*np.uint8(support),
  'rock_ground_skirt_support_mask.png':np.uint8(rock_skirt)*255,
  'wall_shadow_support_mask.png':np.uint8(shadow)*255,
  'wall_shadow_weight.png':np.rint(weight*255),
  'actual_wall_keep_mask.png':np.uint8(masonry)*255,
  'rock_outside_feather_weight.png':np.rint((1-smooth(distance/8))*feather_support*255)}
 for fn,a in outputs.items():image(dest/fn,a)
 immutable=['height.png','land_alpha.png','player_colour_mask.png']+[q.name for q in base.glob('margin_*.png')]
 assert all(sha(dest/f)==sha(base/f)for f in immutable)
 assert np.array_equal(result[~support],rgb[~support])and np.array_equal(new_ground[~support],ground[~support])
 assert np.array_equal(result[masonry],rgb[masonry])and np.array_equal(new_ground[masonry],ground[masonry])
 assert np.array_equal(result[deck],rgb[deck])and np.array_equal(new_ground[deck],ground[deck])
 meta={'schema':1,'units':'whole-template source pixels at144px/cell; top-left origin',
  'base_export':base_export,'base_albedo_sha256':sha(base/'albedo.png'),'source':row['source'],'source_sha256':sha(source),
  'complete_rock_polygons':ROCKS,'actual_wall_keeper_polygons':WALL_KEEP,'actual_wall_review_windows':WALL_REVIEW,
  'actual_wall_antialias_keeper_guard_px':2,
  'ground_shadow_retirement_polygons':SHADOW_RETIRE,'outside_rock_feather_px':8,'outside_rock_ground_retirement_reach_px':20,'wall_contact_feather_px':12,
  'selection':'explicit complete source forms, occupied land and actual wall keepers; no RGB/hue threshold; preserve frozen relief support',
  'mineral_source':str(material.relative_to(project)),'mineral_source_sha256':sha(material),
  'runtime_contract':'After v07 deck exclusion and v08 ground minimum, exact override ground coverage inside boundary_ground_override_mask for BOTH base and alternate records; shade unchanged. Derive normals/specular from updated complete rock mask; no height change.'}
 out.mkdir(parents=True,exist_ok=True);regions=out/'boundary_regions.json';regions.write_text(json.dumps(meta,indent=2)+'\n')
 row.update(pre_stone_boundaries_v09_export=base_export,export=str(dest.relative_to(d)),
  stone_boundary_region_source=str(regions.relative_to(d)),stone_boundary_region_sha256=sha(regions),
  stone_boundary_recipe='Complete explicit native bank faces with existing shared cliff sandstone;8px outside-only stone feather;12px contact fade of source ground shadows; exact geometry/coast/UV/player maps.')
 row['rock_pixels']=int(rock.sum())
 for fn in outputs:
  if fn=='crossing_protected_mask.png':continue
  row[fn[:-4]+'_sha256']=sha(dest/fn)
 for margin in row['margins']:
  p=dest/Path(margin['file']).name;margin['file']=str(p.relative_to(d));margin['sha256']=sha(p)
 (out/'MANIFEST.rows.json').write_text(json.dumps([row],indent=2)+'\n')
 report={'candidate_only':True,'canonical_manifest_updated':False,'base_export':base_export,
  'new_rock_pixels':int((rock&~oldrock).sum()),'rock_pixels':int(rock.sum()),'shadow_pixels':int(shadow.sum()),
  'changed_rgb_pixels':int(np.any(result!=rgb,axis=-1).sum()),'changed_ground_pixels':int((new_ground!=ground).sum()),
  'actual_wall_rgb_ground_exact':True,'full_deck_core_rgb_ground_exact':True,'height_coast_player_margins_exact':True,
  'canonical_manifest_sha256':hashlib.sha256(manifest_bytes).hexdigest(),'albedo_sha256':sha(dest/'albedo.png'),
  'ground_mask_sha256':sha(dest/'ground_mask.png'),'rock_mask_sha256':sha(dest/'rock_mask.png')}
 (out/'REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
 for name,box in [('whole',(0,0,864,720)),('nw-rock',(105,150,355,310)),('se-rock',(600,270,825,455)),('foot-rock',(410,495,550,576)),('nw-wall',(280,145,450,260)),('foot-wall',(340,450,470,540))]:
  a=Image.fromarray(rgb).crop(box);b=Image.fromarray(result).crop(box);sheet=Image.new('RGB',(a.width*2,a.height+22));sheet.paste(a,(0,22));sheet.paste(b,(a.width,22));draw=ImageDraw.Draw(sheet);draw.text((4,5),'v08 source',fill='white');draw.text((a.width+4,5),'v09 candidate',fill='white');sheet.save(out/(name+'-before-after.png'))
 assert manifest_path.read_bytes()==manifest_bytes
 print(json.dumps(report,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--original',type=Path,required=True);p.add_argument('--base-export',default='desert_bridge_approaches_v08/BRIDGE3');p.add_argument('--out',type=Path);a=p.parse_args();repair(a.project,a.original,a.out or a.project/'art/remaster/textures/unified_daylight_v01/desert_stone_boundaries_v09',a.base_export)
