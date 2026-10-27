#!/usr/bin/env python3
"""Correct explicitly reviewed bridge river-water guides misclassified as land.

No hue classifier or new geometry: source polygons follow native water and wave
painting. Keeper masks guard all complete rock, actual masonry and road cores.
Writes versioned full-semantic and alpha-only control candidates, never main.
"""
from pathlib import Path
import argparse,copy,hashlib,json,shutil
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd

REGIONS={'BRIDGE3': [[(0, 320), (27, 313), (42, 300), (60, 294), (83, 294), (99, 290), (114, 280), (134, 277), (154, 283), (172, 287), (190, 289), (206, 293), (219, 296), (235, 298), (253, 304), (269, 305), (262, 317), (249, 326), (235, 338), (218, 353), (205, 370), (194, 381), (180, 397), (164, 405), (148, 407), (133, 406), (118, 400), (106, 398), (96, 403), (88, 412), (74, 417), (58, 421), (43, 421), (31, 418), (20, 420), (0, 422)], [(584, 417), (606, 414), (624, 425), (642, 429), (661, 439), (684, 440), (701, 435), (714, 440), (737, 450), (759, 459), (774, 470), (802, 480), (833, 479), (863, 481), (863, 560), (832, 563), (802, 566), (772, 565), (752, 558), (731, 551), (710, 544), (686, 539), (659, 535), (637, 530), (615, 527), (590, 522), (572, 515), (563, 500), (560, 481), (560, 459), (570, 438)], [(418, 499), (440, 493), (454, 491), (460, 485), (472, 477), (484, 477), (498, 470), (512, 456), (520, 437), (536, 432), (550, 420), (564, 408), (579, 396), (590, 379), (603, 382), (592, 396), (582, 410), (576, 425), (567, 443), (562, 463), (564, 481), (566, 501), (572, 515), (551, 515), (534, 513), (520, 510), (500, 505), (482, 498), (459, 495), (440, 494), (425, 498)]], 'BRIDGE4': [[(0, 470), (0, 551), (44, 543), (80, 528), (110, 506), (134, 490), (162, 486), (185, 494), (208, 500), (229, 499), (249, 490), (268, 483), (289, 483), (309, 478), (329, 481), (348, 479), (363, 473), (377, 468), (385, 464), (379, 445), (363, 427), (347, 405), (332, 389), (310, 379), (295, 374), (284, 373), (273, 366), (259, 357), (246, 351), (237, 347), (222, 358), (211, 371), (191, 378), (176, 380), (160, 386), (144, 393), (129, 398), (111, 403), (91, 411), (71, 421), (49, 431), (26, 437), (10, 445), (0, 459)], [(530, 214), (558, 214), (578, 202), (600, 202), (623, 213), (646, 226), (667, 235), (688, 239), (710, 234), (727, 224), (748, 211), (770, 195), (790, 182), (811, 178), (833, 179), (855, 178), (863, 175), (863, 359), (840, 349), (824, 349), (807, 345), (791, 339), (778, 343), (769, 330), (765, 309), (758, 291), (748, 287), (737, 289), (724, 302), (720, 314), (712, 329), (703, 334), (686, 346), (667, 357), (659, 357), (644, 349), (631, 336), (613, 325), (598, 310), (584, 291), (567, 278), (552, 265), (544, 247), (530, 232)]]}

CONTROLS={'BRIDGE3':[(192,336),(660,466)],'BRIDGE4':[(101,450),(109,443)]}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def polygon_mask(shape,polygons):
 im=Image.new('L',(shape[1],shape[0]));draw=ImageDraw.Draw(im)
 for polygon in polygons:draw.polygon(polygon,fill=255)
 return np.asarray(im)>0
def save(path,value):Image.fromarray(np.uint8(np.rint(np.clip(value,0,255)))).save(path)
def candidate(project,original,out,manifest,alpha_only=False):
 directory=project/'art/remaster/textures/unified_daylight_v01';result=copy.deepcopy(manifest);reports=[];out.mkdir(parents=True,exist_ok=True)
 for row in result['assets']:
  if row['theater']!='DESERT' or row['name']not in REGIONS or row['variant']!=0:continue
  name=row['name'];prior=row.get('pre_water_guides_v11_export',row['export']);base=directory/prior;dest=out/name;dest.mkdir(exist_ok=True)
  for path in base.glob('*.png'):shutil.copy2(path,dest/path.name)
  rgb=np.asarray(Image.open(base/'albedo.png').convert('RGB'));alpha=np.asarray(Image.open(base/'land_alpha.png').convert('L'));ground=np.asarray(Image.open(base/'ground_mask.png').convert('L'));height=np.asarray(Image.open(base/'height.png').convert('L'))
  rock=np.asarray(Image.open(base/'rock_mask.png'))>127;wall=np.asarray(Image.open(base/'masonry_semantic_mask.png'))>127;deck=np.asarray(Image.open(base/'deck_semantic_mask.png'))>127
  shape=alpha.shape;have=np.zeros(shape,bool)
  for icon in row['have']:
   z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
  authored=polygon_mask(shape,REGIONS[name]);keep=nd.binary_dilation(rock|wall|deck,iterations=2);support=authored&have&~keep
  distance=nd.distance_transform_edt(authored);weight=np.clip(distance/4,0,1);weight=weight*weight*(3-2*weight)*support;core=support&(weight==1)
  assert not np.any(height[support]),name+' planned water contains raised source relief'
  new_alpha=np.uint8(np.rint(alpha*(1-weight)));coast_changed=new_alpha!=alpha
  native_path=original/row['source'];native=np.asarray(Image.open(native_path).convert('RGB'))
  outputs={'land_alpha.png':new_alpha,'water_semantic_support_mask.png':support.astype('u1')*255,'water_semantic_core_mask.png':core.astype('u1')*255,'water_semantic_weight.png':weight*255,'coast_correction_support_mask.png':coast_changed.astype('u1')*255,'water_semantic_keeper_mask.png':keep.astype('u1')*255,'water_authored_polygon_mask.png':authored.astype('u1')*255}
  new_rgb=rgb.copy();new_ground=ground.copy()
  if not alpha_only:
   new_rgb=np.uint8(np.rint(rgb*(1-weight[...,None])+native*weight[...,None]));new_ground=np.uint8(np.rint(ground*(1-weight)))
   minimum=np.asarray(Image.open(base/'ground_minimum_mask.png')).copy();minimum=np.uint8(np.rint(minimum*(1-weight)))
   override_path=base/'ground_override_mask.png';override=np.asarray(Image.open(override_path))>127 if override_path.exists() else np.zeros(shape,bool);override|=support
   protected=np.asarray(Image.open(base/'crossing_protected_mask.png'))>127
   outputs.update({'albedo.png':new_rgb,'ground_mask.png':new_ground,'ground_minimum_mask.png':minimum,'ground_override_mask.png':override.astype('u1')*255,'ground_override_values.png':new_ground*override.astype('u1'),'crossing_protected_mask.png':(protected&~support).astype('u1')*255})
  for filename,value in outputs.items():save(dest/filename,value)
  for selection in [~support,keep]:
   assert np.array_equal(new_rgb[selection],rgb[selection]);assert np.array_equal(new_ground[selection],ground[selection]);assert np.array_equal(new_alpha[selection],alpha[selection])
  immutable=['height.png','rock_mask.png','player_colour_mask.png']+[p.name for p in base.glob('margin_*.png')]
  assert all(sha(base/f)==sha(dest/f)for f in immutable)
  if alpha_only:
   assert sha(base/'albedo.png')==sha(dest/'albedo.png')and sha(base/'ground_mask.png')==sha(dest/'ground_mask.png')
  else:
   assert not np.any(new_ground[core]);assert not np.any(np.asarray(Image.open(dest/'ground_minimum_mask.png'))[core])
  controls=[]
  for x,y in CONTROLS[name]:
   assert alpha[y,x]==255 and new_alpha[y,x]==0 and height[y,x]==0 and not keep[y,x]
   controls.append(dict(source_xy=[x,y],old_alpha=int(alpha[y,x]),new_alpha=int(new_alpha[y,x]),new_ground=int(new_ground[y,x])))
  metadata=dict(schema=1,name=name,tid=row['tid'],mode='alpha-only causal probe'if alpha_only else'full semantic correction',units='native whole-template pixels at144px/cell;top-left origin',source_export=prior,source_albedo_sha256=sha(base/'albedo.png'),source_land_alpha_sha256=sha(base/'land_alpha.png'),source_ground_sha256=sha(base/'ground_mask.png'),native_source=row['source'],native_source_sha256=sha(native_path),water_polygons=REGIONS[name],keeper_contract='all complete rock, actual masonry, full-strength road core;2pxguard',keeper_guard_px=2,inward_feather_px=4,source_role='Literal native blue/gray river painting and white wave streaks; no colour or hue inference',controls=controls,runtime_contract='Coast correction explicitly supersedes inherited dry guide cores. Full semantic: apply ground_override_mask/values LAST after frozen alternate append and max-minimum for both base and alternate records. No height/rock/player/margin change.',coast_deviation='Intentional reviewed source-water correction; native coast is no longer byte-exact only inside coast_correction_support_mask.')
  region=out/(name+'-water-regions.json');region.write_text(json.dumps(metadata,indent=2)+'\n')
  row.update(pre_water_guides_v11_export=prior,export=str(dest.relative_to(directory)),water_guide_region_source=str(region.relative_to(directory)),water_guide_region_sha256=sha(region),water_guide_recipe=metadata['coast_deviation'],water_guide_alpha_only=alpha_only)
  for filename in outputs:
   row[filename[:-4]+'_sha256']=sha(dest/filename)
  for margin in row['margins']:
   q=dest/Path(margin['file']).name;margin['file']=str(q.relative_to(directory));margin['sha256']=sha(q)
  reports.append(dict(name=name,source_export=prior,semantic_water_pixels=int(support.sum()),full_water_core_pixels=int(core.sum()),coast_changed_pixels=int(coast_changed.sum()),opaque255_lowered_pixels=int((coast_changed&(alpha==255)).sum()),changed_rgb_pixels=int(np.any(new_rgb!=rgb,axis=-1).sum()),changed_ground_pixels=int((new_ground!=ground).sum()),height_rock_player_margin_bytes_exact=True,all_keeper_pixels_exact=True,outside_semantic_support_exact=True,controls=controls,albedo_sha256=sha(dest/'albedo.png'),land_alpha_sha256=sha(dest/'land_alpha.png'),ground_mask_sha256=sha(dest/'ground_mask.png')))
  native_im=Image.fromarray(native);overlay=native_im.copy();draw=ImageDraw.Draw(overlay)
  for poly in REGIONS[name]:draw.line(poly+[poly[0]],fill='cyan',width=2)
  overlay.save(out/(name+'-source-plan.png'))
  im=Image.new('RGB',(shape[1]*2,shape[0]+26));im.paste(Image.fromarray(alpha).convert('RGB'),(0,26));im.paste(Image.fromarray(new_alpha).convert('RGB'),(shape[1],26));draw=ImageDraw.Draw(im);draw.text((5,7),'Inherited coast alpha',fill='white');draw.text((shape[1]+5,7),'v11 reviewed water correction',fill='white');im.save(out/(name+'-coast-before-after.png'))
 (out/'MANIFEST.candidate.json').write_text(json.dumps(result,indent=2)+'\n');rows=[r for r in result['assets']if r['theater']=='DESERT'and r['name']in REGIONS and r['variant']==0];(out/'MANIFEST.rows.json').write_text(json.dumps(rows,indent=2)+'\n');(out/'REPORT.json').write_text(json.dumps(dict(candidate_only=True,canonical_manifest_updated=False,alpha_only=alpha_only,templates=reports),indent=2)+'\n')
 return reports

def repair(project,original,out):
 d=project/'art/remaster/textures/unified_daylight_v01';path=d/'MANIFEST.json';active_before=path.read_bytes();out.mkdir(parents=True,exist_ok=True)
 baseline=out/'MANIFEST.v10-baseline.json'
 # A reviewed candidate retains its original baseline even when unrelated
 # upstream rows change. Activate only proposed BRIDGE3/4 rows into live main.
 if not baseline.exists():baseline.write_bytes(active_before)
 before=baseline.read_bytes();manifest=json.loads(before)
 full=candidate(project,original,out,manifest,False);probe=candidate(project,original,out/'alpha-only-probe',manifest,True)
 assert path.read_bytes()==active_before
 print(json.dumps(dict(full=full,alpha_only_probe=probe,canonical_manifest_unchanged=True,baseline_manifest_sha256=hashlib.sha256(before).hexdigest()),indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--original',type=Path,required=True);p.add_argument('--out',type=Path);a=p.parse_args();repair(a.project,a.original,a.out or a.project/'art/remaster/textures/unified_daylight_v01/desert_bridge_water_guides_v11')
