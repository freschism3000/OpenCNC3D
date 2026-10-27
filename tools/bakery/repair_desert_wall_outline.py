#!/usr/bin/env python3
"""Prepare one opaque-land NW bridge ground-outline patch over frozen v09.

The actual installed comparison revealed a thin painted brown ground outline
remaining behind the NW cap stones. Explicit source keeper/strip polygons retire
that ground; no diffuse classification, new geometry or historical asset edits.
"""
from pathlib import Path
import argparse,copy,hashlib,json,shutil
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd

KEEP=[(338,201),(351,197),(370,194),(382,183),(393,176),(407,163),
      (421,158),(432,161),(433,166),(426,177),(428,185),(428,194),
      (418,207),(414,217),(403,229),(390,233),(387,240),(377,248),
      (375,257),(355,259),(340,254),(329,244),(329,229),(330,214)]
STRIP=[(317,197),(332,185),(364,181),(373,175),(394,158),(408,150),
       (421,148),(435,153),(444,160),(442,170),(434,178),(426,186),
       (419,182),(427,171),(430,164),(427,160),(421,159),(411,163),
       (397,176),(382,188),(371,196),(345,201),(336,206),(331,216),
       (328,227),(322,225),(318,214)]
REVIEW=[(315,143),(449,143),(449,263),(318,263)]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def mask(shape,poly):
 im=Image.new('L',(shape[1],shape[0]));ImageDraw.Draw(im).polygon(poly,fill=255);return np.asarray(im)>0
def save(p,a):Image.fromarray(np.uint8(np.rint(np.clip(a,0,255)))).save(p)
def repair(project,out):
 d=project/'art/remaster/textures/unified_daylight_v01';base=d/'desert_stone_boundaries_v09/BRIDGE3';dest=out/'BRIDGE3';dest.mkdir(parents=True,exist_ok=True)
 for f in base.glob('*.png'):shutil.copy2(f,dest/f.name)
 row=copy.deepcopy(json.loads((d/'desert_stone_boundaries_v09/MANIFEST.rows.json').read_text())[0])
 rgb=np.asarray(Image.open(base/'albedo.png'));ground=np.asarray(Image.open(base/'ground_mask.png'));minimum=np.asarray(Image.open(base/'ground_minimum_mask.png'))
 oldwall=np.asarray(Image.open(base/'actual_wall_keep_mask.png'))>127;rock=np.asarray(Image.open(base/'rock_mask.png'))>127;deck=np.asarray(Image.open(base/'deck_semantic_mask.png'))>127
 alpha=np.asarray(Image.open(base/'land_alpha.png'));height=np.asarray(Image.open(base/'height.png'));shape=ground.shape
 keeper=nd.binary_dilation(mask(shape,KEEP),iterations=1)
 wall=(oldwall&~mask(shape,REVIEW))|keeper
 support=mask(shape,STRIP)&~wall&~rock&~deck&(alpha==255)&(height==0)
 distance=nd.distance_transform_edt(~wall);weight=np.clip(distance/6,0,1);weight=weight*weight*(3-2*weight)*support
 grain=np.asarray(Image.open(d/'desert_bridge_approaches_v08/clear_grain_source.png'));yy,xx=np.indices(shape);target=grain[(yy+83)%grain.shape[0],(xx+47)%grain.shape[1]]
 result=np.uint8(np.rint(rgb*(1-weight[...,None])+target*weight[...,None]));new_ground=np.maximum(ground,np.uint8(np.rint(weight*255)));new_minimum=np.maximum(minimum,np.uint8(np.rint(weight*255)))
 override=(np.asarray(Image.open(base/'ground_override_mask.png'))>127)|support
 protected=(np.asarray(Image.open(base/'crossing_protected_mask.png'))>127)&~support
 outputs={'albedo.png':result,'ground_mask.png':new_ground,'ground_minimum_mask.png':new_minimum,
  'masonry_semantic_mask.png':np.uint8(wall)*255,'actual_wall_keep_mask.png':np.uint8(wall)*255,
  'ground_override_mask.png':np.uint8(override)*255,'ground_override_values.png':new_ground*np.uint8(override),
  'crossing_protected_mask.png':np.uint8(protected)*255,
  'wall_outline_support_mask.png':np.uint8(support)*255,'wall_outline_weight.png':weight*255,
  'wall_outline_ground_values.png':new_ground*np.uint8(support),'wall_outline_minimum_values.png':new_minimum*np.uint8(support)}
 for fn,a in outputs.items():save(dest/fn,a)
 for selection in [~support,wall,rock,deck,alpha<255]:
  assert np.array_equal(result[selection],rgb[selection])and np.array_equal(new_ground[selection],ground[selection])
 immutable=['height.png','land_alpha.png','player_colour_mask.png','rock_mask.png']+[f.name for f in base.glob('margin_*.png')]
 assert all(sha(base/f)==sha(dest/f)for f in immutable)
 meta=dict(schema=1,source_export='desert_stone_boundaries_v09/BRIDGE3',source_albedo_sha256=sha(base/'albedo.png'),
  source_ground_sha256=sha(base/'ground_mask.png'),units='whole-template native Enhanced pixels;144px/cell',
  actual_wall_keeper_polygon=KEEP,keeper_antialias_guard_px=1,wall_review_polygon=REVIEW,
  ground_outline_retirement_polygon=STRIP,contact_feather_px=6,
  contract='Opaque alpha255 land only; actual wall/rock/core/height/coast/player/margins exact; apply resulting RGB/ground/minimum inside wall_outline_support_mask after independent wet-feather restore. Updated actual_wall_keep_mask replaces v09 NW review only.')
 regions=out/'wall_outline_regions.json';regions.write_text(json.dumps(meta,indent=2)+'\n')
 row.update(pre_wall_skirts_v10_export='desert_stone_boundaries_v09/BRIDGE3',export=str(dest.relative_to(d)),
  wall_outline_region_source=str(regions.relative_to(d)),wall_outline_region_sha256=sha(regions))
 for fn in outputs:row[fn[:-4]+'_sha256']=sha(dest/fn)
 for mar in row['margins']:
  f=dest/Path(mar['file']).name;mar['file']=str(f.relative_to(d));mar['sha256']=sha(f)
 (out/'MANIFEST.rows.json').write_text(json.dumps([row],indent=2)+'\n')
 report=dict(candidate_only=True,changed_rgb_pixels=int(np.any(result!=rgb,axis=-1).sum()),changed_ground_pixels=int((new_ground!=ground).sum()),
  patch_support_pixels=int(support.sum()),all_partialwet_exact=True,all_rock_and_road_core_exact=True,actual_wall_exact=True,
  all_geometry_coast_player_margin_bytes_exact=True,albedo_sha256=sha(dest/'albedo.png'),ground_mask_sha256=sha(dest/'ground_mask.png'))
 (out/'REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
 box=(280,145,450,260);a=Image.fromarray(rgb).crop(box).resize((680,460));b=Image.fromarray(result).crop(box).resize(a.size);im=Image.new('RGB',(1360,488));im.paste(a,(0,28));im.paste(b,(680,28));dr=ImageDraw.Draw(im);dr.text((5,8),'v09 NW wall source',fill='white');dr.text((685,8),'v10 opaque ground outline candidate',fill='white');im.save(out/'nw-wall-before-after.png')
 print(json.dumps(report,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--project',type=Path,required=True);p.add_argument('--out',type=Path);a=p.parse_args();repair(a.project,a.out or a.project/'art/remaster/textures/unified_daylight_v01/desert_wall_skirts_v10')
