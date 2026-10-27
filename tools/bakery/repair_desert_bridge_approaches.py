#!/usr/bin/env python3
"""Retire complete bridge approach paint into the continuous Desert ground.

Existing v07 road cores, explicit masonry, mineral, water and geometry stay
immutable. This is a source-raster/material-mask repair, not new geometry.
"""
from pathlib import Path
import argparse,copy,hashlib,json,shutil
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd

APPROACHES={
 'BRIDGE3':[
  [(432,0),(720,0),(720,213),(624,262),(455,233),(404,177),(432,144)],
  [(0,560),(104,440),(174,406),(230,415),(393,448),(414,521),(236,720),(0,720)]],
 'BRIDGE4':[
  [(144,0),(336,0),(458,110),(419,190),(287,239),(144,158)],
  [(465,410),(671,351),(864,481),(864,576),(555,576),(447,481)]]}
TRACKS={
 'BRIDGE3':[[(675,0),(570,160),(521,223)],[(0,704),(165,518),(267,458)]],
 'BRIDGE4':[[(175,0),(285,148),(315,189)],[(556,430),(674,511),(778,576)]]}

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def smooth(value):
 value=np.clip(value,0,1)
 return value**3*(value*(value*6-15)+10)
def polygon_mask(shape,polygons):
 image=Image.new('L',(shape[1],shape[0]),0)
 for polygon in polygons:ImageDraw.Draw(image).polygon(polygon,fill=255)
 return np.asarray(image)>0
def save(path,array):Image.fromarray(np.asarray(array,dtype='u1')).save(path)
def track_field(shape,polylines):
 yy,xx=np.indices(shape);distance=np.full(shape,np.inf)
 for line in polylines:
  for a,b in zip(line,line[1:]):
   ax,ay=a;bx,by=b;vx,vy=bx-ax,by-ay
   t=np.clip(((xx-ax)*vx+(yy-ay)*vy)/(vx*vx+vy*vy),0,1)
   distance=np.minimum(distance,np.hypot(xx-ax-t*vx,yy-ay-t*vy))
 return np.exp(-.5*(distance/34)**2)

def repair(project,original,out,apply=False):
 directory=project/'art/remaster/textures/unified_daylight_v01'
 manifest=json.loads((directory/'MANIFEST.json').read_text());candidate=copy.deepcopy(manifest)
 out.mkdir(parents=True,exist_ok=True)
 grain_path=original/'art/remaster/textures/DESERT/2026-10-18/clear_v03/sources/CLEAR1/v01/CLEAR1_v01_144.png'
 grain=np.asarray(Image.open(grain_path).convert('RGB'));shutil.copy2(grain_path,out/'clear_grain_source.png')
 metadata=dict(schema=1,units='whole-template native Enhanced pixels; 144 px/cell; top-left origin',
  approach_polygons=APPROACHES,track_centerlines=TRACKS,outer_feather_pixels=32,deck_handoff_pixels=24,
  track_sigma_pixels=34,track_coverage_reduction=.18,track_rgb_reduction=.10,
  grain_source=str(grain_path.relative_to(original)),grain_source_sha256=sha(grain_path),
  grain_copy='clear_grain_source.png',grain_copy_sha256=sha(out/'clear_grain_source.png'),grain_offset=[47,83],
  bank_contour=dict(templates=['BRIDGE3'],authority='all native occupied coast alpha<250; never RGB',
                    visible_land_support='alpha>0, including the saved land feather; alpha0 water RGB exact',
                    full_strength_distance_pixels=20,outer_distance_pixels=40),
  preserve_exact=['v07 full-strength deck cores','explicit polygon masonry','height.png','rock_mask.png',
                  'land_alpha.png','player_colour_mask.png','all margin PNGs','outside retirement support'],
  runtime_contract='After v07 alternate deck exclusion, max(existing ground coverage, saved ground_minimum_mask); unchanged shade and outside support.')
 region=out/'approach_regions.json';region.write_text(json.dumps(metadata,indent=2)+'\n')
 reports=[]
 for row in candidate['assets']:
  if row['theater']!='DESERT' or row['name']not in APPROACHES or row['variant']:continue
  name=row['name'];prior=row.get('pre_bridge_approaches_v08_export',row['export'])
  assert prior=='desert_bridge_decks_v07/'+name,'v08 requires the reviewed v07 road source'
  source=directory/prior;dest=out/name;dest.mkdir(exist_ok=True)
  for path in source.glob('*.png'):shutil.copy2(path,dest/path.name)
  rgb=np.asarray(Image.open(source/'albedo.png').convert('RGB'))
  ground=np.asarray(Image.open(source/'ground_mask.png').convert('L'))
  alpha=np.asarray(Image.open(source/'land_alpha.png').convert('L'))
  rock=np.asarray(Image.open(source/'rock_mask.png').convert('L'))
  height=np.asarray(Image.open(source/'height.png').convert('L'))
  masonry=np.asarray(Image.open(source/'masonry_semantic_mask.png').convert('L'))>127
  deck=np.asarray(Image.open(source/'deck_semantic_mask.png').convert('L'))>127
  shape=ground.shape;have=np.zeros(shape,bool)
  for icon in row['have']:
   z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
  allowed=have&(alpha==255)&(rock==0)&(height==0)&~masonry&~deck
  polygon=polygon_mask(shape,APPROACHES[name]);authored=polygon&have
  # Missing template icons are neighbouring world ground, not an artistic edge
  # to fade toward. Evaluate the authored contour before the occupancy clip.
  outer=smooth(nd.distance_transform_edt(polygon)/32)
  handoff=smooth(nd.distance_transform_edt(~deck)/24)
  approach=np.uint8(np.rint(255*outer*handoff*allowed))
  bank=np.zeros(shape,dtype='u1')
  if name=='BRIDGE3':
   native_water=(alpha<250)&have
   distance=nd.distance_transform_edt(~native_water)
   # The old pale guide lies at alpha249..253 as well as on opaque ground.
   # Extend land RGB through its existing visible feather; do not change the
   # authoritative alpha or touch fully transparent native water pixels.
   bank_allowed=have&(alpha>0)&(rock==0)&(height==0)&~masonry&~deck
   bank=np.uint8(np.rint(255*(1-smooth((distance-20)/20))*bank_allowed))
  track=track_field(shape,TRACKS[name])
  minimum=np.maximum(np.uint8(np.rint(approach.astype(float)*(1-.18*track))),bank)
  weight=np.maximum(approach,bank)
  support=(minimum>0)|(weight>0)
  yy,xx=np.indices(shape)
  target=grain[(yy+83)%grain.shape[0],(xx+47)%grain.shape[1]].astype(float)
  target*=1-.10*track[...,None]*(approach>bank)[...,None]
  target=np.uint8(np.rint(np.clip(target,0,255)))
  blend=weight[...,None]/255.0
  result=np.uint8(np.rint(rgb*(1-blend)+target*blend))
  new_ground=np.maximum(ground,minimum)
  protected=np.asarray(Image.open(source/'crossing_protected_mask.png'))>127
  outputs={'albedo.png':result,'ground_mask.png':new_ground,'ground_minimum_mask.png':minimum,
   'retirement_support_mask.png':np.uint8(support)*255,'approach_weight.png':approach,'bank_weight.png':bank,
   'approach_semantic_mask.png':np.uint8(authored&allowed)*255,'clear_grain_target.png':target,
   'retirement_rgb_weight.png':weight,'crossing_protected_mask.png':np.uint8(protected&~support)*255}
  for filename,array in outputs.items():save(dest/filename,array)
  for filename in ['height.png','rock_mask.png','land_alpha.png','player_colour_mask.png']:
   assert sha(dest/filename)==sha(source/filename)
  for selection in [~support,masonry,deck]:
   assert np.array_equal(result[selection],rgb[selection])
   assert np.array_equal(new_ground[selection],ground[selection])
  assert np.array_equal(result[alpha==0],rgb[alpha==0])
  row['pre_bridge_approaches_v08_export']=prior;row['export']=str(dest.relative_to(directory))
  row['bridge_approach_region_source']=str(region.relative_to(directory));row['bridge_approach_region_sha256']=sha(region)
  row['bridge_approach_recipe']='Complete authored approach paint retired into source CLEAR1/world ground; smooth quiet tracks and deck handoff; full dry bank contour retirement; v07 road cores and explicit masonry exact.'
  for filename in outputs:
   if filename=='crossing_protected_mask.png':continue
   key=filename[:-4]+'_sha256'
   row[key]=sha(dest/filename)
  for margin in row['margins']:
   margin['file']=str((dest/Path(margin['file']).name).relative_to(directory));margin['sha256']=sha(directory/margin['file'])
  reports.append(dict(name=name,source_export=prior,retirement_support_pixels=int(support.sum()),
   changed_rgb_pixels=int(np.any(result!=rgb,axis=-1).sum()),changed_ground_pixels=int((new_ground!=ground).sum()),
   full_approach_pixels=int((approach==255).sum()),full_bank_pixels=int((bank==255).sum()),
   masonry_pixels_exact=int(masonry.sum()),deck_core_pixels_exact=int(deck.sum()),
   albedo_sha256=sha(dest/'albedo.png'),ground_mask_sha256=sha(dest/'ground_mask.png')))
 (out/'MANIFEST.candidate.json').write_text(json.dumps(candidate,indent=2)+'\n')
 (out/'REPORT.json').write_text(json.dumps(dict(candidate_only=not apply,canonical_manifest_updated=apply,
  source_manifest_sha256=sha(directory/'MANIFEST.json'),region_sha256=sha(region),templates=reports),indent=2)+'\n')
 if apply:(directory/'MANIFEST.json').write_text(json.dumps(candidate,indent=2)+'\n')
 print(json.dumps(reports,indent=2))

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--project',type=Path,required=True);parser.add_argument('--original',type=Path,required=True)
 parser.add_argument('--out',type=Path);parser.add_argument('--apply-manifest',action='store_true')
 args=parser.parse_args();repair(args.project,args.original,args.out or args.project/'art/remaster/textures/unified_daylight_v01/desert_bridge_approaches_v08',args.apply_manifest)
