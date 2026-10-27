#!/usr/bin/env python3
"""Verify saved crossing rocks, protected native bridge art and paired runtime data."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib,sys
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage as nd

# These conservative road-interior polygons were measured from the original
# whole-template art and DOS reference, independently of any feature/protection
# mask. In v06, BRIDGE3's saved mask mistakenly admitted CLEAR1 over 12,210 of
# these 19,952 pixels. Reusing that same mask as the QA selector hid the defect.
DECK_LANE_POLYGONS={
 'BRIDGE3':[(248,399),(480,242),(520,300),(288,457)],
 'BRIDGE4':[(301,184),(565,341),(529,401),(265,244)]}

def road_lane_mask(name,shape):
 mask=Image.new('L',(shape[1],shape[0]),0)
 ImageDraw.Draw(mask).polygon(DECK_LANE_POLYGONS[name],fill=255)
 return np.asarray(mask)>0

def verify_road_lane(name,ground,height,rock,alpha):
 """Use independent semantics, never the candidate's own protected mask."""
 lane=road_lane_mask(name,ground.shape)
 assert lane.sum()>10000,(name,'insufficient independent road support')
 assert not(ground[lane]>0).any(),(name,'CLEAR1 coverage inside continuous bridge road',int((ground[lane]>0).sum()))
 assert not(height[lane]>0).any(),(name,'road added relief')
 assert not(rock[lane]>0).any(),(name,'road treated as bank rock')
 assert np.all(alpha[lane]==255),(name,'water cut inside continuous bridge road')
 return dict(semantic_road_pixels=int(lane.sum()),clear_coverage_pixels=0,
             added_road_height=0,rock_mask_on_road=0,road_land_alpha=255)

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def records(path,magic,channels=1):
 b=path.read_bytes();assert b[:8]==magic,(path,'magic');v,c,n=struct.unpack_from('<III',b,8);assert v==1
 keys=[b[20+i*24:44+i*24]for i in range(n)];start=20+n*24
 if magic==b'C3DMATL\0':size=struct.unpack_from('<I',b,start)[0];pixels=zlib.decompress(b[start+4:]);assert len(b)==start+4+size
 else:pixels=b[start:]
 stride=c*c*channels;assert len(pixels)==n*stride
 return c,{(struct.unpack_from('<HHBB',k,16)):pixels[i*stride:(i+1)*stride]for i,k in enumerate(keys)}

def ground_records(path):
 """Keep the alternate key; collapsing byte22 can hide an obsolete mask."""
 raw=path.read_bytes();assert raw[:8]==b'C3DGRAS\0',(path,'magic')
 version,cell,count=struct.unpack_from('<III',raw,8);assert version==1
 start=20+24*count;stride=cell*cell*2
 assert len(raw)==start+count*stride,(path,'length')
 result={}
 for index in range(count):
  key=struct.unpack_from('<HHBBH',raw,20+24*index+16)
  assert key not in result,(path,'duplicate ground key',key)
  result[key]=np.frombuffer(raw,'u1',count=stride,offset=start+index*stride).reshape(cell,cell,2)
 return cell,result

def rgb_records(path):
 raw=path.read_bytes();assert raw[:8]==b'C3DT30\0\0',(path,'magic')
 version,cell,count=struct.unpack_from('<III',raw,8);assert version==1
 start=20+4*count;stride=cell*cell*3
 assert len(raw)==start+count*stride,(path,'length')
 result={}
 for index in range(count):
  key=struct.unpack_from('<HH',raw,20+4*index)
  assert key not in result,(path,'duplicate albedo key',key)
  result[key]=raw[start+index*stride:start+(index+1)*stride]
 return cell,result

def detail_templates(path):
 if not path.exists():return set()
 raw=path.read_bytes();assert raw[:8]==b'C3DDECO\0',(path,'magic')
 version,count=struct.unpack_from('<II',raw,8);assert version==1
 assert len(raw)==16+count*24,(path,'length')
 return {struct.unpack_from('<H',raw,16+index*24)[0]for index in range(count)}

def verify_runtime_road(row,albedo,ground,runtime,deck_masks):
 cell,ground_tiles=runtime['ground'];rgb_cell,rgb_tiles=runtime['rgb']
 assert cell==rgb_cell==144,'Bridge source/native pixel density differs'
 alternate_cells=0
 core=deck_masks['semantic'];support=deck_masks['support'];weight=deck_masks['end_weight']
 original_alternates=runtime['original_alternates'][1]
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);box=(slice(z*cell,(z+1)*cell),slice(x*cell,(x+1)*cell))
  key=(row['tid'],icon,0,0,0)
  assert key in ground_tiles,(row['name'],icon,'missing base ground record')
  assert np.array_equal(ground_tiles[key][...,0],ground[box]),(row['name'],icon,'runtime road ground differs')
  assert np.all(ground_tiles[key][...,1]==128),(row['name'],icon,'runtime ground carries baked shade')
  assert rgb_tiles[(row['tid'],icon)]==albedo[box].tobytes(),(row['name'],icon,'runtime road albedo differs')
  alt_key=key[:-1]+(1,)
  assert alt_key in ground_tiles and alt_key in original_alternates,(row['name'],icon,'bridge alternate ground missing')
  alternate_cells+=1
  old=original_alternates[alt_key]
  expected=old.copy();tile_support=support[box]
  expected[...,0][tile_support]=np.uint8(np.rint(old[...,0][tile_support]*(1-weight[box][tile_support]/255.0)))
  if 'ground_minimum'in deck_masks:expected[...,0]=np.maximum(expected[...,0],deck_masks['ground_minimum'][box])
  if 'exact_ground_support'in deck_masks:
   selected=deck_masks['exact_ground_support'][box];expected[...,0][selected]=deck_masks['exact_ground_values'][box][selected]
  assert np.array_equal(ground_tiles[alt_key],expected),(row['name'],icon,'alternate ground repair or outside support changed')
  assert not ground_tiles[alt_key][...,0][core[box]].any(),(row['name'],icon,'alternate CLEAR1 coverage on bridge road')
 return dict(base_ground_cells_exact=len(row['have']),albedo_cells_exact=len(row['have']),
             alternate_cells_exact=alternate_cells,alternate_can_be_selected=row['tid']in runtime['details'])

def polygon_mask(shape,polygons):
 image=Image.new('L',(shape[1],shape[0]),0);draw=ImageDraw.Draw(image)
 for polygon in polygons:draw.polygon([tuple(point)for point in polygon],fill=255)
 return np.asarray(image)>0

def verify_bridge_deck(directory,row):
 """Separate source semantics from the mask whose error is under test."""
 name=row['name'];current=directory/row['export'];prior=directory/row['pre_bridge_decks_v07_export']
 assert row['pre_bridge_decks_v07_export']=='desert_crossings_v06/'+name,(name,'deck baseline changed')
 region=directory/row['bridge_deck_region_source']
 assert sha(region)==row['bridge_deck_region_sha256'],(name,'deck regions hash')
 metadata=json.loads(region.read_text());assert metadata['schema']==1
 masks={}
 for filename,key in [('deck_semantic_mask.png','semantic'),('deck_support_mask.png','support'),
                      ('masonry_semantic_mask.png','masonry'),('deck_end_weight.png','end_weight'),
                      ('deck_donor_weight.png','donor_weight')]:
  assert sha(current/filename)==row[filename[:-4]+'_sha256'],(name,filename,'hash')
  image=Image.open(current/filename);assert image.mode=='L'
  masks[key]=np.asarray(image)
 shape=masks['semantic'].shape;have=np.zeros(shape,bool)
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 for key in ['semantic','support','masonry']:
  assert np.isin(masks[key],[0,255]).all(),(name,key,'must be binary')
  masks[key]=masks[key]>127
 semantic,support,masonry=(masks[key]for key in ['semantic','support','masonry'])
 # Rebuild preservation from authored polygons, without deriving it from the
 # previous protected mask or from ground/albedo classifications.
 expected_masonry=polygon_mask(shape,metadata['masonry_polygons'][name])&have
 expected_support=polygon_mask(shape,metadata['deck_polygons'][name])&have&~expected_masonry
 assert np.array_equal(masonry,expected_masonry),(name,'masonry semantic polygon differs')
 assert np.array_equal(support,expected_support),(name,'deck support polygon differs')
 assert masonry.sum()>20000 and semantic.sum()>30000,(name,'missing semantic preservation/core support')
 assert not(semantic&~support).any(),(name,'road core outside authored support')
 assert not(support&masonry).any(),(name,'road repair intersects masonry')
 assert np.all(masks['end_weight'][semantic]==255),(name,'core does not fully exclude ground')
 assert not masks['end_weight'][~support].any()and not masks['donor_weight'][~support].any(),(name,'deck weight outside support')
 assert np.all(masks['donor_weight']<=masks['end_weight']),(name,'RGB exceeds authorized road-end weight')
 new={filename:np.asarray(Image.open(current/filename))for filename in ['albedo.png','ground_mask.png','height.png','rock_mask.png','land_alpha.png','player_colour_mask.png','crossing_protected_mask.png']}
 old={filename:np.asarray(Image.open(prior/filename))for filename in new}
 for filename in ['albedo.png','ground_mask.png']:
  assert np.array_equal(new[filename][~support],old[filename][~support]),(name,filename,'outside deck support changed')
  assert np.array_equal(new[filename][masonry],old[filename][masonry]),(name,filename,'masonry changed')
 for filename in ['height.png','rock_mask.png','land_alpha.png','player_colour_mask.png']:
  assert sha(current/filename)==sha(prior/filename),(name,filename,'deck repair changed immutable map')
 for margin in row['margins']:
  filename=Path(margin['file']).name
  assert sha(current/filename)==sha(prior/filename),(name,filename,'deck repair changed margin')
 expected_protection=(old['crossing_protected_mask.png']>127)&~support
 assert np.array_equal(new['crossing_protected_mask.png']>127,expected_protection),(name,'protected mask no longer equals prior minus explicit deck')
 assert not new['ground_mask.png'][semantic].any(),(name,'CLEAR1 coverage in semantic road core')
 assert not new['height.png'][semantic].any()and not new['rock_mask.png'][semantic].any(),(name,'semantic road core lifted or rock-mapped')
 assert np.all(new['land_alpha.png'][semantic]==255),(name,'semantic road core cut as water')
 assert np.array_equal(new['albedo.png'][masks['donor_weight']==0],old['albedo.png'][masks['donor_weight']==0]),(name,'RGB changed without donor weight')
 if name=='BRIDGE4':
  assert sha(current/'albedo.png')==sha(prior/'albedo.png'),(name,'control bridge albedo changed')
  assert not masks['donor_weight'].any(),(name,'control bridge borrowed paint')
 donor=directory/metadata['donor']['source'];assert sha(donor)==metadata['donor']['sha256'],(name,'native road donor changed')
 if name=='BRIDGE3':
  old_ground_islands=semantic&(old['ground_mask.png']>0)
  assert np.all(masks['donor_weight'][old_ground_islands]==255),(name,'old sand islands not fully repainted as road')
  # Independently verify the actual transferred road pixels. The stored8-bit
  # blend weight differs slightly from the baker's full-precision weight, so
  # allow at most one RGB quantization step, never a broad colour deviation.
  yy,xx=np.where(masks['donor_weight']>0)
  points=np.stack((xx,yy,np.ones(len(xx))),axis=1)
  transform=np.asarray(metadata['donor']['homography_destination_to_source'])
  mapped=points@transform.T;mapped=mapped[:,:2]/mapped[:,2,None]
  polygon=np.asarray(metadata['donor']['safe_source_polygon'],dtype=float)
  crosses=[]
  for start,end in zip(polygon,np.roll(polygon,-1,axis=0)):
   edge=end-start;local=mapped-start;crosses.append(edge[0]*local[:,1]-edge[1]*local[:,0])
  crosses=np.asarray(crosses)
  assert np.all(np.all(crosses>=-1e-6,axis=0)|np.all(crosses<=1e-6,axis=0)),(name,'road donor samples outside safe road')
  donor_rgb=np.asarray(Image.open(donor).convert('RGB'),dtype=float)
  x0=np.floor(mapped[:,0]).astype(int);y0=np.floor(mapped[:,1]).astype(int)
  assert np.all((x0>=0)&(y0>=0)&(x0+1<donor_rgb.shape[1])&(y0+1<donor_rgb.shape[0])),(name,'road donor coordinates outside source')
  fx=(mapped[:,0]-x0)[:,None];fy=(mapped[:,1]-y0)[:,None]
  paint=(donor_rgb[y0,x0]*(1-fx)+donor_rgb[y0,x0+1]*fx)*(1-fy)+(donor_rgb[y0+1,x0]*(1-fx)+donor_rgb[y0+1,x0+1]*fx)*fy
  weight=masks['donor_weight'][yy,xx,None]/255.0
  expected=np.rint(old['albedo.png'][yy,xx]*(1-weight)+paint*weight)
  assert np.max(np.abs(new['albedo.png'][yy,xx].astype(float)-expected))<=1,(name,'road albedo differs from recorded continuous-road donor')
 lane=verify_road_lane(name,new['ground_mask.png'],new['height.png'],new['rock_mask.png'],new['land_alpha.png'])
 return dict(**lane,deck_core_pixels=int(semantic.sum()),deck_support_pixels=int(support.sum()),
             masonry_pixels_exact=int(masonry.sum()),outside_support_exact=True,immutable_maps_and_margins_exact=True,
             continuous_road_donor_verified=name=='BRIDGE3',control_albedo_exact=name=='BRIDGE4'),masks

# Independently located from the user's source-matched approach/bank crops.
# These points are controls, never the edit/support selector.
APPROACH_CONTROLS={
 'BRIDGE3':[(520,180),(555,200),(175,493),(220,492),(250,503)],
 'BRIDGE4':[(210,50),(255,75),(290,110),(580,500),(640,540),(670,555)]}
BANK_CONTROLS=[(528,534),(541,535),(551,540),(568,540),(581,547),(603,552),(622,563),(636,571)]

def verify_approach_controls(name,ground):
 for x,y in APPROACH_CONTROLS[name]:
  assert ground[y,x]>=200,(name,'old road/approach fragment survives independent control',x,y,int(ground[y,x]))
 if name=='BRIDGE3':
  for x,y in BANK_CONTROLS:
   assert ground[y,x]==255,(name,'painted bank line survives independent contour control',x,y)

def verify_bridge_approaches(directory,row):
 name=row['name'];current=directory/row['export'];prior=directory/row['pre_bridge_approaches_v08_export']
 assert row['pre_bridge_approaches_v08_export']=='desert_bridge_decks_v07/'+name,(name,'v08 baseline changed')
 prior_row=dict(row);prior_row['export']=row['pre_bridge_approaches_v08_export']
 report,masks=verify_bridge_deck(directory,prior_row)
 region=directory/row['bridge_approach_region_source'];assert sha(region)==row['bridge_approach_region_sha256']
 metadata=json.loads(region.read_text());assert metadata['schema']==1
 new={};old={}
 for filename in ['albedo.png','ground_mask.png','crossing_protected_mask.png']:
  new[filename]=np.asarray(Image.open(current/filename));old[filename]=np.asarray(Image.open(prior/filename))
 for filename in ['ground_minimum_mask.png','retirement_support_mask.png','approach_weight.png','bank_weight.png','approach_semantic_mask.png','clear_grain_target.png','retirement_rgb_weight.png']:
  assert sha(current/filename)==row[filename[:-4]+'_sha256'],(name,filename,'v08 hash')
  new[filename]=np.asarray(Image.open(current/filename))
 shape=new['ground_mask.png'].shape;support=new['retirement_support_mask.png']>127
 minimum=new['ground_minimum_mask.png'];weight=new['retirement_rgb_weight.png'];alpha=np.asarray(Image.open(prior/'land_alpha.png'))
 for filename in ['height.png','rock_mask.png','land_alpha.png','player_colour_mask.png','deck_semantic_mask.png','deck_support_mask.png','masonry_semantic_mask.png','deck_end_weight.png','deck_donor_weight.png','approach_mask.png']:
  assert sha(current/filename)==sha(prior/filename),(name,filename,'v08 changed immutable map')
 for margin in row['margins']:
  filename=Path(margin['file']).name;assert sha(current/filename)==sha(prior/filename),(name,'v08 changed margin',filename)
 for selection in [~support,masks['masonry'],masks['semantic'],alpha==0]:
  assert np.array_equal(new['albedo.png'][selection],old['albedo.png'][selection]),(name,'v08 changed preserved RGB')
  assert np.array_equal(new['ground_mask.png'][selection],old['ground_mask.png'][selection]),(name,'v08 changed preserved ground')
 assert np.array_equal(support,(minimum>0)|(weight>0)),(name,'v08 support is not actual edit support')
 assert np.array_equal(new['ground_mask.png'],np.maximum(old['ground_mask.png'],minimum)),(name,'v08 ground minimum application differs')
 expected=np.uint8(np.rint(old['albedo.png']*(1-weight[...,None]/255.0)+new['clear_grain_target.png']*(weight[...,None]/255.0)))
 assert np.array_equal(new['albedo.png'],expected),(name,'v08 source-ground blend differs')
 expected=(old['crossing_protected_mask.png']>127)&~support
 assert np.array_equal(new['crossing_protected_mask.png']>127,expected),(name,'v08 protection is not prior minus support')
 have=np.zeros(shape,bool)
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 rock=np.asarray(Image.open(prior/'rock_mask.png'));height=np.asarray(Image.open(prior/'height.png'))
 allowed=have&(alpha==255)&(rock==0)&(height==0)&~masks['masonry']&~masks['semantic']
 polygon=polygon_mask(shape,metadata['approach_polygons'][name])
 assert np.array_equal(new['approach_semantic_mask.png']>127,polygon&allowed),(name,'v08 authored approach semantics differ')
 def smooth(value):
  value=np.clip(value,0,1);return value**3*(value*(value*6-15)+10)
 approach=np.uint8(np.rint(255*smooth(nd.distance_transform_edt(polygon)/metadata['outer_feather_pixels'])*smooth(nd.distance_transform_edt(~masks['semantic'])/metadata['deck_handoff_pixels'])*allowed))
 assert np.array_equal(approach,new['approach_weight.png']),(name,'v08 approach handoff/outer feather differs')
 bank=np.zeros(shape,'u1')
 if name in metadata['bank_contour']['templates']:
  rules=metadata['bank_contour'];distance=nd.distance_transform_edt(~((alpha<250)&have))
  bank_allowed=have&(alpha>0)&(rock==0)&(height==0)&~masks['masonry']&~masks['semantic']
  lo,hi=rules['full_strength_distance_pixels'],rules['outer_distance_pixels']
  bank=np.uint8(np.rint(255*(1-smooth((distance-lo)/(hi-lo)))*bank_allowed))
 assert np.array_equal(bank,new['bank_weight.png']),(name,'v08 full contour support differs')
 assert np.array_equal(weight,np.maximum(approach,bank)),(name,'v08 RGB retirement weight differs')
 grain_path=region.parent/metadata['grain_copy'];assert sha(grain_path)==metadata['grain_copy_sha256']
 grain=np.asarray(Image.open(grain_path).convert('RGB'));yy,xx=np.indices(shape);distance=np.full(shape,np.inf)
 for line in metadata['track_centerlines'][name]:
  for a,b in zip(line,line[1:]):
   ax,ay=a;bx,by=b;vx,vy=bx-ax,by-ay;t=np.clip(((xx-ax)*vx+(yy-ay)*vy)/(vx*vx+vy*vy),0,1)
   distance=np.minimum(distance,np.hypot(xx-ax-t*vx,yy-ay-t*vy))
 track=np.exp(-.5*(distance/metadata['track_sigma_pixels'])**2)
 expected_min=np.maximum(np.uint8(np.rint(approach.astype(float)*(1-metadata['track_coverage_reduction']*track))),bank)
 assert np.array_equal(minimum,expected_min),(name,'v08 saved ground target differs')
 ox,oy=metadata['grain_offset'];target=grain[(yy+oy)%grain.shape[0],(xx+ox)%grain.shape[1]].astype(float)
 target=np.uint8(np.rint(target*(1-metadata['track_rgb_reduction']*track[...,None]*(approach>bank)[...,None])))
 assert np.array_equal(target,new['clear_grain_target.png']),(name,'v08 source ground/quiet track pixels differ')
 verify_approach_controls(name,new['ground_mask.png'])
 verify_road_lane(name,new['ground_mask.png'],height,rock,alpha)
 masks['ground_minimum']=minimum;masks['retirement_support']=support
 report['v07_control_albedo_exact']=report.pop('control_albedo_exact')
 report.update(deck_core_albedo_exact=True,approach=dict(retirement_pixels=int(support.sum()),
  full_approach_pixels=int((approach==255).sum()),full_bank_pixels=int((bank==255).sum()),
  independent_approach_controls=len(APPROACH_CONTROLS[name]),independent_bank_controls=len(BANK_CONTROLS)if name=='BRIDGE3'else 0,
  masonry_deck_water_and_geometry_exact=True,outside_support_exact=True,source_grain_and_smooth_weights_verified=True))
 return report,masks

STONE_CONTROLS=[(155,211),(150,228),(143,249),(753,299),(715,310),(769,324),(477,513),(469,517),(492,521)]
WALL_CONTROLS=[(347,211),(372,223),(395,193),(420,167),(365,475),(383,471),(397,477),(420,467),(440,475)]
SKIRT_CONTROLS=[(440,203),(432,208),(414,503),(395,508)]

def verify_bridge_stone_boundaries(directory,row):
 name=row['name'];assert name=='BRIDGE3'
 assert row['pre_stone_boundaries_v09_export']=='desert_bridge_approaches_v08/BRIDGE3'
 prior_row=next(r for r in json.loads((directory/'desert_bridge_approaches_v08/MANIFEST.candidate.json').read_text())['assets']if(r['theater'],r['name'],r['variant'])==('DESERT',name,0))
 report,masks=verify_bridge_approaches(directory,prior_row)
 current=directory/row['export'];prior=directory/row['pre_stone_boundaries_v09_export']
 region=directory/row['stone_boundary_region_source'];assert sha(region)==row['stone_boundary_region_sha256']
 metadata=json.loads(region.read_text());assert metadata['schema']==1
 files=['albedo.png','ground_mask.png','rock_mask.png','masonry_semantic_mask.png','ground_minimum_mask.png',
  'complete_rock_semantic_mask.png','boundary_support_mask.png','boundary_ground_override_mask.png',
  'ground_override_mask.png','ground_override_values.png','rock_ground_skirt_support_mask.png',
  'wall_shadow_support_mask.png','wall_shadow_weight.png','actual_wall_keep_mask.png','rock_outside_feather_weight.png']
 new={}
 for filename in files:
  assert sha(current/filename)==row[filename[:-4]+'_sha256'],(name,filename,'v09 hash')
  new[filename]=np.asarray(Image.open(current/filename))
 old={filename:np.asarray(Image.open(prior/filename))for filename in ['albedo.png','ground_mask.png','rock_mask.png','masonry_semantic_mask.png','ground_minimum_mask.png','crossing_protected_mask.png']}
 alpha=np.asarray(Image.open(prior/'land_alpha.png'));height=np.asarray(Image.open(prior/'height.png'));shape=alpha.shape
 have=np.zeros(shape,bool)
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 review=polygon_mask(shape,metadata['actual_wall_review_windows'])&have
 keep=nd.binary_dilation(polygon_mask(shape,metadata['actual_wall_keeper_polygons']),iterations=metadata['actual_wall_antialias_keeper_guard_px'])&have
 masonry=((old['masonry_semantic_mask.png']>127)&~review)|keep
 assert np.array_equal(new['actual_wall_keep_mask.png']>127,masonry),(name,'v09 actual wall semantics differ')
 assert np.array_equal(new['masonry_semantic_mask.png']>127,masonry),(name,'v09 masonry mask differs')
 deck=masks['semantic'];oldrock=old['rock_mask.png']>127
 authored=polygon_mask(shape,metadata['complete_rock_polygons'])&have
 rock=oldrock|(authored&~masonry&(alpha>0)&~deck)
 assert np.array_equal(new['rock_mask.png']>127,rock)and np.array_equal(new['complete_rock_semantic_mask.png']>127,rock),(name,'v09 full native rock form shrank')
 distance=nd.distance_transform_edt(~rock);reach=metadata['outside_rock_ground_retirement_reach_px']
 skirt=(distance<=reach)&have&(alpha>0)&~masonry&~deck
 mineral=(rock|skirt)&~masonry&~deck
 shadow=polygon_mask(shape,metadata['ground_shadow_retirement_polygons'])&have&(alpha>0)&(height==0)&~masonry&~rock&~deck&~skirt
 def smooth(value):
  value=np.clip(value,0,1);return value*value*(3-2*value)
 shadow_weight=np.uint8(np.rint(255*smooth(nd.distance_transform_edt(~masonry)/metadata['wall_contact_feather_px'])*shadow))
 support=mineral|shadow
 for filename,expected in [('boundary_support_mask.png',support),('boundary_ground_override_mask.png',support),('ground_override_mask.png',support),('wall_shadow_support_mask.png',shadow),('rock_ground_skirt_support_mask.png',skirt)]:
  assert np.array_equal(new[filename]>127,expected),(name,filename,'v09 support differs')
 assert np.array_equal(new['wall_shadow_weight.png'],shadow_weight),(name,'v09 contact weight differs')
 expected=old['ground_mask.png'].copy();expected[mineral]=np.uint8(np.rint(255*smooth(distance[mineral]/metadata['outside_rock_feather_px'])));expected=np.maximum(expected,shadow_weight)
 assert np.array_equal(new['ground_mask.png'],expected),(name,'v09 complete stone/skirt ground differs')
 expected_min=old['ground_minimum_mask.png'].copy();expected_min[mineral]=0;expected_min=np.maximum(expected_min,shadow_weight)
 assert np.array_equal(new['ground_minimum_mask.png'],expected_min),(name,'v09 old ground minimum resurrects mineral cut')
 assert np.array_equal(new['ground_override_values.png'],new['ground_mask.png']*np.uint8(support)),(name,'v09 exact override differs or exceeds support')
 for filename in ['height.png','land_alpha.png','player_colour_mask.png','deck_semantic_mask.png','deck_support_mask.png','deck_end_weight.png','deck_donor_weight.png','approach_weight.png','bank_weight.png','retirement_support_mask.png','retirement_rgb_weight.png','clear_grain_target.png']:
  assert sha(current/filename)==sha(prior/filename),(name,filename,'v09 changed unrelated source')
 for margin in row['margins']:
  filename=Path(margin['file']).name;assert sha(current/filename)==sha(prior/filename),(name,'v09 margin changed')
 for selection in [~support,masonry,deck,alpha==0]:
  assert np.array_equal(new['albedo.png'][selection],old['albedo.png'][selection]),(name,'v09 changed preserved wall/deck/water/outside RGB')
  assert np.array_equal(new['ground_mask.png'][selection],old['ground_mask.png'][selection]),(name,'v09 changed preserved ground')
 assert np.array_equal(new['albedo.png'][oldrock],old['albedo.png'][oldrock]),(name,'v09 existing approved rock RGB changed')
 protection=np.asarray(Image.open(current/'crossing_protected_mask.png'))>127
 assert np.array_equal(protection,(old['crossing_protected_mask.png']>127)&~support),(name,'v09 protection differs')
 added=rock&~oldrock;paint=new['albedo.png'][added].astype(float)
 assert np.max(np.abs(paint[:,0]-paint[:,1]*(1.16/1.015)))<=2 and np.max(np.abs(paint[:,2]-paint[:,1]*(.80/1.015)))<=2,(name,'v09 warm faces not shared sandstone palette')
 for x,y in STONE_CONTROLS:assert rock[y,x]and new['ground_mask.png'][y,x]==0,(name,'omitted native rock face',x,y)
 for x,y in WALL_CONTROLS:assert masonry[y,x]and np.array_equal(new['albedo.png'][y,x],old['albedo.png'][y,x]),(name,'actual wall altered',x,y)
 for x,y in SKIRT_CONTROLS:assert new['ground_mask.png'][y,x]>=200,(name,'hard ground shadow persists',x,y)
 verify_approach_controls(name,new['ground_mask.png']);verify_road_lane(name,new['ground_mask.png'],height,new['rock_mask.png'],alpha)
 masks.update(ground_minimum=new['ground_minimum_mask.png'],exact_ground_support=support,exact_ground_values=new['ground_override_values.png'],boundary_support=support)
 report.pop('immutable_maps_and_margins_exact')
 report.update(height_coast_player_margins_exact=True,mineral_mask_revision='explicit complete native rock forms')
 report.update(masonry_pixels_exact=int(masonry.sum()),stone_boundaries=dict(new_complete_rock_pixels=int(added.sum()),
  full_native_rock_pixels=int(rock.sum()),actual_wall_pixels_exact=int(masonry.sum()),ground_shadow_pixels=int(shadow.sum()),
  independent_rock_controls=len(STONE_CONTROLS),independent_wall_controls=len(WALL_CONTROLS),independent_skirt_controls=len(SKIRT_CONTROLS),
  existing_rock_deck_water_height_coast_player_margins_exact=True,outside_explicit_support_exact=True))
 return report,masks

def verify_wet_feather_pixels(name,alpha,have,rock,wall,candidate,native):
 """The immutable source cut owns wet exterior, separately from literal stone.

 Alpha alone cannot classify the user's partial-alpha foot cap as water; the
 independently verified complete source rock and actual-wall silhouettes own
 that exception. Ground skirts/feathers never confer a solid-body exception.
 """
 wet=have&(alpha<248);protected=wet&~rock&~wall
 for filename in ['albedo.png','ground_mask.png','rock_mask.png']:
  assert np.array_equal(candidate[filename][protected],native[filename][protected]),(name,filename,'visible wet exterior differs from native source')
 assert not np.any(candidate['ground_mask.png'][wet&rock]),(name,'world ground exposed on literal partial-alpha stone')
 return wet,protected

def verify_bridge_wet_feather(directory,row):
 name=row['name'];assert name=='BRIDGE3'
 assert row['pre_bridge_wet_feather_v10_export']=='desert_stone_boundaries_v09/BRIDGE3'
 prior_row=json.loads((directory/'desert_stone_boundaries_v09/MANIFEST.rows.json').read_text())[0]
 report,masks=verify_bridge_stone_boundaries(directory,prior_row)
 current=directory/row['export'];prior=directory/prior_row['export'];native=directory/'desert_bridge_decks_v07/BRIDGE3'
 region=directory/row['bridge_wet_feather_region_source'];assert sha(region)==row['bridge_wet_feather_region_sha256']
 metadata=json.loads(region.read_text());assert metadata['schema']==1 and metadata['dry_alpha_min']==248
 assert metadata['native_wet_source_export']=='desert_bridge_decks_v07/BRIDGE3'
 assert metadata['base_export']==prior_row['export']
 assert metadata['base_row_sha256']==sha(directory/'desert_stone_boundaries_v09/MANIFEST.rows.json')
 assert metadata['native_land_alpha_sha256']==sha(native/'land_alpha.png')
 old={p.name:np.asarray(Image.open(p))for p in prior.glob('*.png')}
 new={p.name:np.asarray(Image.open(p))for p in current.glob('*.png')}
 original={p.name:np.asarray(Image.open(p))for p in native.glob('*.png')}
 for filename in ['albedo.png','ground_mask.png','rock_mask.png']:
  assert sha(native/filename)==metadata['native_wet_source_hashes'][filename]
 for filename,array in new.items():
  if filename!='crossing_protected_mask.png':assert sha(current/filename)==row[filename[:-4]+'_sha256'],(name,filename,'v10 hash')
 alpha=original['land_alpha.png'];shape=alpha.shape;have=np.zeros(shape,bool)
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 rock=old['complete_rock_semantic_mask.png']>127;wall=old['actual_wall_keep_mask.png']>127
 wet,protected=verify_wet_feather_pixels(name,alpha,have,rock,wall,new,original)
 assert np.array_equal(new['wet_protection_mask.png']>127,protected),(name,'wet exterior selector differs from independent cut/solid semantics')
 assert np.array_equal(new['native_partial_water_mask.png']>127,wet),(name,'native wet selector changed')
 assert np.array_equal(new['partial_solid_body_mask.png']>127,wet&(rock|wall)),(name,'partial-alpha solid selector differs')
 assert np.array_equal(new['dry_allowed_mask.png']>127,have&~wet),(name,'dry retirement selector differs')
 expected={filename:array.copy()for filename,array in old.items()}
 for filename in ['albedo.png','ground_mask.png','rock_mask.png']:expected[filename][protected]=original[filename][protected]
 required_zero=['ground_minimum_mask.png','retirement_support_mask.png','approach_weight.png','bank_weight.png',
  'approach_semantic_mask.png','retirement_rgb_weight.png','rock_ground_skirt_support_mask.png','wall_shadow_support_mask.png',
  'wall_shadow_weight.png','rock_outside_feather_weight.png']
 assert metadata['active_masks_zero_on_wet']==required_zero,(name,'active wet-writer list differs')
 for filename in required_zero:expected[filename][wet]=0
 for filename in ['boundary_support_mask.png','boundary_ground_override_mask.png','ground_override_mask.png','ground_override_values.png']:expected[filename][protected]=0
 expected['crossing_protected_mask.png'][protected]=original['crossing_protected_mask.png'][protected]
 wall_support=np.zeros(shape,bool)
 if metadata['wall_patch'] is not None:
  wall_region=region.parent/'wall_outline_regions.json';assert sha(wall_region)==metadata['wall_patch_metadata_sha256']
  wm=json.loads(wall_region.read_text());assert wm==metadata['wall_patch']
  review=polygon_mask(shape,[wm['wall_review_polygon']])
  keeper=nd.binary_dilation(polygon_mask(shape,[wm['actual_wall_keeper_polygon']]),iterations=wm['keeper_antialias_guard_px'])
  revised_wall=(wall&~review)|keeper
  wall_support=polygon_mask(shape,[wm['ground_outline_retirement_polygon']])&~revised_wall&~rock&~masks['semantic']&(alpha==255)&(old['height.png']==0)
  assert not np.any(wall_support&wet),(name,'wall strip reaches visible water')
  distance=np.clip(nd.distance_transform_edt(~revised_wall)/wm['contact_feather_px'],0,1)
  weight=(distance*distance*(3-2*distance))*wall_support
  assert np.array_equal(new['wall_outline_support_mask.png']>127,wall_support),(name,'wall outline support differs from source polygons')
  assert np.array_equal(new['wall_outline_weight.png'],np.uint8(np.rint(weight*255))),(name,'wall contact fade differs')
  grain=np.asarray(Image.open(directory/'desert_bridge_approaches_v08/clear_grain_source.png'));yy,xx=np.indices(shape);target=grain[(yy+83)%grain.shape[0],(xx+47)%grain.shape[1]]
  rgb=np.uint8(np.rint(old['albedo.png']*(1-weight[...,None])+target*weight[...,None]))
  ground=np.maximum(old['ground_mask.png'],np.uint8(np.rint(weight*255)))
  minimum=np.maximum(old['ground_minimum_mask.png'],np.uint8(np.rint(weight*255)))
  expected['albedo.png'][wall_support]=rgb[wall_support];expected['ground_mask.png'][wall_support]=ground[wall_support]
  expected['ground_minimum_mask.png'][wall_support]=minimum[wall_support]
  for filename in ['ground_override_mask.png','boundary_support_mask.png','boundary_ground_override_mask.png']:expected[filename][wall_support]=255
  expected['crossing_protected_mask.png'][wall_support]=0
  for filename in ['masonry_semantic_mask.png','actual_wall_keep_mask.png']:expected[filename]=np.uint8(revised_wall)*255
 expected['ground_override_values.png']=expected['ground_mask.png']*np.uint8(expected['ground_override_mask.png']>127)
 for filename,array in expected.items():
  assert np.array_equal(new[filename],array),(name,filename,'v10 composition differs from native wet restoration and opaque wall strip')
 for filename in ['height.png','land_alpha.png','player_colour_mask.png','rock_mask.png']+[p.name for p in prior.glob('margin_*.png')]:assert sha(current/filename)==sha(prior/filename),(name,filename,'v10 changed immutable geometry/material')
 assert np.array_equal(new['albedo.png'][rock],old['albedo.png'][rock]),(name,'literal stone recolour was undone by partial alpha')
 for x,y in STONE_CONTROLS:assert rock[y,x]and new['ground_mask.png'][y,x]==0 and np.array_equal(new['albedo.png'][y,x],old['albedo.png'][y,x]),(name,'required uniform foot cap/stone changed',x,y)
 for x,y in APPROACH_CONTROLS[name]:assert new['ground_mask.png'][y,x]>=200,(name,'dry approach repair lost',x,y)
 for x,y in BANK_CONTROLS:
  if alpha[y,x]>=248:assert new['ground_mask.png'][y,x]==255,(name,'nearopaque baked guide cleanup lost',x,y)
 verify_road_lane(name,new['ground_mask.png'],new['height.png'],new['rock_mask.png'],alpha)
 masks.update(ground_minimum=new['ground_minimum_mask.png'],retirement_support=new['retirement_support_mask.png']>127,
  boundary_support=new['boundary_support_mask.png']>127,exact_ground_support=new['ground_override_mask.png']>127,
  exact_ground_values=new['ground_override_values.png'],wet_exterior=protected)
 report['approach'].update(historical_v08_retirement_pixels=report['approach']['retirement_pixels'],
  retirement_pixels=int(np.count_nonzero(new['retirement_support_mask.png'])),
  full_approach_pixels=int((new['approach_weight.png']==255).sum()),full_bank_pixels=int((new['bank_weight.png']==255).sum()))
 report['masonry_pixels_exact']=int(np.count_nonzero(new['actual_wall_keep_mask.png']))
 report['wet_feather']=dict(native_wet_pixels=int(wet.sum()),protected_visible_exterior_pixels=int(protected.sum()),
  partial_alpha_rock_pixels=int((wet&rock).sum()),partial_alpha_wall_pixels=int((wet&wall).sum()),
  opaque_wall_outline_pixels=int(wall_support.sum()),exterior_rgb_ground_exact=True,complete_rock_material_and_foot_cap_exact=True,
  variant_native_ground_preservation='base source exact; alternate checked from frozen native records only when runtime_verified=true')
 return report,masks

WATER_GUIDE_CONTROLS={
 'BRIDGE3':[(192,336),(660,466)],
 'BRIDGE4':[(101,450),(109,443)]}

def verify_water_guide_controls(name,alpha):
 """Independent actual-render/source matches; not the authored edit selector."""
 for x,y in WATER_GUIDE_CONTROLS[name]:
  assert alpha[y,x]==0,(name,'native painted-water guide remains opaque',x,y,int(alpha[y,x]))

def verify_bridge_water_guides(directory,row,original):
 name=row['name'];current=directory/row['export']
 region=directory/row['water_guide_region_source'];assert sha(region)==row['water_guide_region_sha256']
 metadata=json.loads(region.read_text());assert metadata['schema']==1
 assert metadata['inward_feather_px']==4 and metadata['keeper_guard_px']==2
 baseline=region.parent/'MANIFEST.v10-baseline.json'
 if row.get('water_guide_alpha_only'):baseline=region.parent.parent/'MANIFEST.v10-baseline.json'
 prior_row=next(r for r in json.loads(baseline.read_text())['assets']if(r['theater'],r['name'],r['variant'])==('DESERT',name,0))
 assert row['pre_water_guides_v11_export']==prior_row['export']==metadata['source_export']
 if name=='BRIDGE3':report,masks=verify_bridge_wet_feather(directory,prior_row)
 else:report,masks=verify_bridge_approaches(directory,prior_row)
 prior=directory/prior_row['export']
 old={p.name:np.asarray(Image.open(p))for p in prior.glob('*.png')}
 new={p.name:np.asarray(Image.open(p))for p in current.glob('*.png')}
 for filename in ['albedo.png','ground_mask.png','land_alpha.png']:
  source_key={'albedo.png':'source_albedo_sha256','ground_mask.png':'source_ground_sha256','land_alpha.png':'source_land_alpha_sha256'}[filename]
  assert sha(prior/filename)==metadata[source_key],(name,filename,'v11 baseline hash')
 alpha=old['land_alpha.png'];shape=alpha.shape;have=np.zeros(shape,bool)
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 rock=old['rock_mask.png']>127;wall=old['masonry_semantic_mask.png']>127;deck=masks['semantic']
 keep=nd.binary_dilation(rock|wall|deck,iterations=2)
 authored=polygon_mask(shape,metadata['water_polygons']);support=authored&have&~keep
 weight=np.clip(nd.distance_transform_edt(authored)/4,0,1);weight=weight*weight*(3-2*weight)*support
 core=support&(weight==1);expected_alpha=np.uint8(np.rint(alpha*(1-weight)));coast_changed=expected_alpha!=alpha
 assert core.sum()>40000,(name,'v11 water semantics vacuous')
 for filename,expected in [('water_semantic_support_mask.png',support),('water_semantic_core_mask.png',core),
  ('coast_correction_support_mask.png',coast_changed),('water_semantic_keeper_mask.png',keep),('water_authored_polygon_mask.png',authored)]:
  assert sha(current/filename)==row[filename[:-4]+'_sha256'],(name,filename,'v11 hash')
  assert np.isin(new[filename],[0,255]).all()and np.array_equal(new[filename]>127,expected),(name,filename,'v11 independently reconstructed source semantics differ')
 assert np.array_equal(new['water_semantic_weight.png'],np.uint8(np.rint(weight*255))),(name,'v11 coast transition differs')
 assert np.array_equal(new['land_alpha.png'],expected_alpha),(name,'v11 coast data differs or exceeds reviewed water region')
 assert np.all(new['land_alpha.png']<=alpha),(name,'v11 invented dry land in native water')
 assert not old['height.png'][support].any(),(name,'v11 source water crosses raised geometry')
 native_path=original/row['source'];assert sha(native_path)==metadata['native_source_sha256']
 native=np.asarray(Image.open(native_path).convert('RGB'));alpha_only=row.get('water_guide_alpha_only',False)
 if alpha_only:
  for filename in ['albedo.png','ground_mask.png','ground_minimum_mask.png']:
   assert sha(current/filename)==sha(prior/filename),(name,'alpha-only causal control changed other data',filename)
 else:
  expected_rgb=np.uint8(np.rint(old['albedo.png']*(1-weight[...,None])+native*weight[...,None]))
  expected_ground=np.uint8(np.rint(old['ground_mask.png']*(1-weight)))
  expected_minimum=np.uint8(np.rint(old['ground_minimum_mask.png']*(1-weight)))
  expected_override=(old['ground_override_mask.png']>127)if'ground_override_mask.png'in old else np.zeros(shape,bool)
  expected_override|=support
  assert np.array_equal(new['albedo.png'],expected_rgb),(name,'v11 native water RGB restoration differs')
  assert np.array_equal(new['ground_mask.png'],expected_ground),(name,'v11 water admits world ground')
  assert np.array_equal(new['ground_minimum_mask.png'],expected_minimum),(name,'v11 minimum resurrects world ground on water')
  assert np.array_equal(new['ground_override_mask.png']>127,expected_override),(name,'v11 base/alternate exact override support differs')
  assert np.array_equal(new['ground_override_values.png'],expected_ground*expected_override.astype('u1')),(name,'v11 base/alternate exact override values differ')
  assert not new['ground_mask.png'][core].any()and not new['ground_minimum_mask.png'][core].any(),(name,'v11 full water core has ground writer')
  expected_protected=(old['crossing_protected_mask.png']>127)&~support
  assert np.array_equal(new['crossing_protected_mask.png']>127,expected_protected),(name,'v11 protection not prior minus reviewed water')
  masks.update(ground_minimum=new['ground_minimum_mask.png'],exact_ground_support=expected_override,
   exact_ground_values=new['ground_override_values.png'])
 for filename in ['albedo.png','ground_mask.png','land_alpha.png']:
  for selection in [~support,keep]:
   assert np.array_equal(new[filename][selection],old[filename][selection]),(name,filename,'v11 changed outside water or actual keeper')
 for filename in ['height.png','rock_mask.png','player_colour_mask.png']+[p.name for p in prior.glob('margin_*.png')]:
  assert sha(current/filename)==sha(prior/filename),(name,filename,'v11 changed geometry/material/margin')
 verify_water_guide_controls(name,new['land_alpha.png'])
 # A deliberately wrong old-v10 positive island must fail this SAME control.
 try:verify_water_guide_controls(name,alpha)
 except AssertionError:negative_rejected=True
 else:negative_rejected=False
 assert negative_rejected and all(alpha[y,x]==255 for x,y in WATER_GUIDE_CONTROLS[name]),(name,'v11 old opaque-guide negative control did not fail')
 verify_road_lane(name,new['ground_mask.png'],new['height.png'],new['rock_mask.png'],new['land_alpha.png'])
 masks['boundary_support']=masks.get('boundary_support',np.zeros(shape,bool))|support
 historical_report=report
 report=dict(prior_source_revision=prior_row['export'],prior_revision_checks=historical_report,
  semantic_road_pixels=historical_report['semantic_road_pixels'],deck_core_pixels=int(deck.sum()),
  masonry_pixels_exact=int(wall.sum()),outside_semantic_water_support_exact=True,
  geometry_rock_player_margins_exact=True,native_coast_exact=False)
 report['water_guides']=dict(reviewed_water_pixels=int(support.sum()),full_water_core_pixels=int(core.sum()),
  explicit_coast_changed_pixels=int(coast_changed.sum()),inherited_opaque_255_lowered=int((coast_changed&(alpha==255)).sum()),
  independent_false_land_controls=len(WATER_GUIDE_CONTROLS[name]),old_v10_opaque_negative_rejected=True,
  outside_support_and_all_actual_keepers_exact=True,geometry_rock_player_margins_exact=True,
  native_coast_exact=False,coast_deviation='explicit source-led water-guide correction only',alpha_only_causal_probe=alpha_only)
 return report,masks

def verify_bridge_road_handoff(directory,row,original):
 name=row['name'];current=directory/row['export'];prior=directory/row['pre_road_handoff_v12_export']
 prior_row=next(r for r in json.loads((directory/'desert_bridge_water_guides_v11/MANIFEST.rows.json').read_text())if r['name']==name)
 assert prior_row['export']==row['pre_road_handoff_v12_export']
 report,masks=verify_bridge_water_guides(directory,prior_row,original)
 get=lambda folder,name:np.asarray(Image.open(folder/name))
 old_rgb=get(prior,'albedo.png');new_rgb=get(current,'albedo.png');old_ground=get(prior,'ground_mask.png');new_ground=get(current,'ground_mask.png')
 alpha=get(prior,'land_alpha.png');rock=get(prior,'rock_mask.png')>127;masonry=get(prior,'masonry_semantic_mask.png')>0;deck=get(prior,'deck_semantic_mask.png')>0
 shape=alpha.shape;have=np.zeros(shape,bool);w,h=row['cells']
 for icon in row['have']:
  z,x=divmod(icon,w);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 allowed=(get(prior,'approach_semantic_mask.png')>0)&(alpha==255)&~rock&~masonry&~deck
 distance=nd.distance_transform_edt(np.pad(have,1))[1:-1,1:-1]/144
 t=np.clip(distance,0,1);weight=(1-t**3*(t*(t*6-15)+10))*allowed;support=weight>0
 assert np.array_equal(get(current,'road_handoff_weight.png'),np.uint8(np.rint(weight*255))),(name,'road handoff exceeds native approach/boundary')
 for a,b in [(new_rgb,old_rgb),(new_ground,old_ground)]:
  for keep in [~support,masonry,deck,alpha<255,rock]:assert np.array_equal(a[keep],b[keep]),(name,'road handoff changed keeper or water')
 for f in ['height.png','land_alpha.png','rock_mask.png','player_colour_mask.png','deck_semantic_mask.png','masonry_semantic_mask.png']:
  assert sha(current/f)==sha(prior/f),(name,'road handoff changed geometry/coast/mask',f)
 material=original/row['road_handoff_material_source'];assert sha(material)==row['road_handoff_material_sha256'];rgb=get(material.parent,material.name);yy,xx=np.indices(shape)
 source=original/'art/remaster/textures/DESERT/2026-10-19/cliff_shore_repair_v07'
 q=next(q for q in json.loads((source/'FEATURE_MANIFEST.json').read_text())['assets']if q['name']==name and q['variant']==0)
 road=get(source/q['export'],'ground_mask.png')<128
 inside=nd.distance_transform_edt(np.pad(road,12,mode='edge'))[12:-12,12:-12];t=np.clip((inside-.5)/8,0,1);coverage=t**3*(t*(t*6-15)+10)
 expected_rgb=np.uint8(np.rint(old_rgb*(1-weight[...,None])+rgb[yy%144,xx%144]*weight[...,None]));expected_ground=np.uint8(np.rint(old_ground*(1-weight)+255*(1-coverage)*weight))
 assert np.array_equal(new_rgb,expected_rgb),(name,'road exit material mismatch')
 assert np.array_equal(new_ground,expected_ground),(name,'native road exit coverage mismatch')
 exits=(weight>.999)&(coverage==1);assert exits.sum()>100 and np.all(new_ground[exits]==0),(name,'bridge road does not meet road core')
 for key,file in [('ground_minimum','ground_minimum_mask.png'),('exact_ground_values','ground_override_values.png')]:masks[key]=get(current,file)
 masks['exact_ground_support']=get(current,'ground_override_mask.png')>127
 masks['boundary_support']=masks.get('boundary_support',np.zeros(shape,bool))|support
 report['road_handoff']=dict(support_pixels=int(support.sum()),full_exit_core_pixels=int(exits.sum()),native_road_coverage=True,source_exit_material_exact=True,all_keepers_coast_geometry_and_outside_support_exact=True)
 return report,masks

def run(source,original,playable=None,manifest=None,require_decks=False):
 d=source/'art/remaster/textures/unified_daylight_v01';m=json.loads((manifest or d/'MANIFEST.json').read_text());out=[]
 names=['FORD1','BRIDGE3','BRIDGE4'];meta=json.loads((original/'art/remaster/textures/DESERT/set_144.json').read_text())['templates']
 runtime={}
 if playable:
  runtime['height']=records(playable/'terrainsurface_DESERT.pack',b'C3DSURF\0')
  runtime['coast']=records(playable/'coastmask_DESERT.pack',b'C3DCOAS\0')
  runtime['material']=records(playable/'terrainmaterial_DESERT.pack',b'C3DMATL\0',4)
  assert set(runtime['height'][1])==set(runtime['material'][1]),'normal and physical keys must match'
  assert not any(k[0]in(170,172)for k in runtime['height'][1]),'Destroyed bridges retain native zero-detail fallback'
  sys.path.insert(0,str(source/'tools/bakery'));import stone_materials
  if any(r['theater']=='DESERT'and r['name']in ['BRIDGE3','BRIDGE4']and r.get('pre_bridge_decks_v07_export')for r in m['assets']):
   runtime['ground']=ground_records(playable/'groundmask_DESERT.pack')
   runtime['rgb']=rgb_records(playable/'terrainenh_DESERT.pack')
   runtime['details']=detail_templates(playable/'desert_details.pack')
   runtime['original_alternates']=ground_records(original/'art/remaster/desert_details_v02/runtime/desert_details_masks.pack')
   assert not any(k[0]in(170,172)for k in runtime['rgb'][1]),'Destroyed bridges retain native albedo fallback'
 for name in names:
  r=next(r for r in m['assets']if(r['theater'],r['name'],r['variant'])==('DESERT',name,0));t=meta[name];assert(r['cells'],r['have'],r['tid'])==(t['cells'],t['have'],t['tid'])
  p=d/r['export'];assert r['export'] in ('desert_crossings_v05/'+name,'desert_crossings_v06/'+name,'desert_bridge_decks_v07/'+name,'desert_bridge_approaches_v08/'+name,'desert_stone_boundaries_v09/'+name,'desert_bridge_wet_feather_v10/'+name,'desert_bridge_water_guides_v11/'+name,'desert_bridge_water_guides_v11/alpha-only-probe/'+name,'desert_bridge_road_handoff_v12/'+name)
  deck_report=None;deck_masks=None
  if r.get('pre_road_handoff_v12_export'):
   deck_report,deck_masks=verify_bridge_road_handoff(d,r,original)
  elif r.get('pre_water_guides_v11_export'):
   deck_report,deck_masks=verify_bridge_water_guides(d,r,original)
  elif r['export']=='desert_bridge_decks_v07/'+name:
   deck_report,deck_masks=verify_bridge_deck(d,r)
  elif r['export']=='desert_bridge_approaches_v08/'+name:
   deck_report,deck_masks=verify_bridge_approaches(d,r)
  elif r['export']=='desert_stone_boundaries_v09/'+name:
   deck_report,deck_masks=verify_bridge_stone_boundaries(d,r)
  elif r['export']=='desert_bridge_wet_feather_v10/'+name:
   deck_report,deck_masks=verify_bridge_wet_feather(d,r)
  elif require_decks and name in ['BRIDGE3','BRIDGE4']:
   raise AssertionError((name,'continuous intact bridge deck revision required'))
  for fn,key in [('albedo.png','albedo_sha256'),('rock_mask.png','rock_mask_sha256'),('ground_mask.png','ground_mask_sha256'),('height.png','height_sha256'),('land_alpha.png','land_alpha_sha256'),('player_colour_mask.png','player_colour_mask_sha256')]:assert sha(p/fn)==r[key],(name,fn,'hash')
  assert sha(original/r['source'])==r['source_sha256'];a=np.array(Image.open(p/'albedo.png'));old=np.array(Image.open(original/r['source']).convert('RGB'));h=np.array(Image.open(p/'height.png'));rock=np.array(Image.open(p/'rock_mask.png'))>127;protected=np.array(Image.open(p/'crossing_protected_mask.png'))>127;alpha=np.array(Image.open(p/'land_alpha.png'));player=Image.open(p/'player_colour_mask.png')
  assert player.mode=='L'and player.getextrema()==(0,0)
  assert np.array_equal(a[protected],old[protected]),(name,'native crossing art changed')
  assert h[protected].max()==0 and not(rock&protected).any(),(name,'crossing lifted')
  assert h[alpha<250].max()==0,(name,'water lifted')
  material_water=alpha==0 if r.get('pre_stone_boundaries_v09_export')else alpha<250
  assert not(rock&material_water).any(),(name,'fully wet water treated as rock')
  assert h.max()>0 and rock.sum()>1000,(name,'bank relief missing')
  if (p/'approach_mask.png').exists():
   v05=d/'desert_crossings_v05'/name;approach=np.array(Image.open(p/'approach_mask.png'))>0
   allowed=approach if deck_masks is None else approach|deck_masks['support']
   if deck_masks is not None and 'retirement_support'in deck_masks:allowed|=deck_masks['retirement_support']
   if deck_masks is not None and 'boundary_support'in deck_masks:allowed|=deck_masks['boundary_support']
   assert np.array_equal(a[~allowed],np.array(Image.open(v05/'albedo.png'))[~allowed]),(name,'non-approach/deck pixels changed')
   assert not(approach&(alpha<255)).any(),(name,'wet approach edit')
   immutable=['height.png','player_colour_mask.png']
   if not r.get('pre_water_guides_v11_export'):immutable.append('land_alpha.png')
   if not r.get('pre_stone_boundaries_v09_export'):immutable.append('rock_mask.png')
   for fn in immutable:assert sha(p/fn)==sha(v05/fn),(name,fn,'changed')
  runtime_cells=0
  if playable:
   w,hh=r['cells'];nr,sp=stone_materials.mineral(a,np.uint8(rock)*255);packed=np.dstack((nr[...,:2],nr[...,3],np.uint8(rock)*255))
   for icon in r['have']:
    z,x=divmod(icon,w);key=(r['tid'],icon,0,0);box=(x*144,z*144,(x+1)*144,(z+1)*144);c,tiles=runtime['height'];expected=Image.fromarray(h,'L').crop(box).resize((c,c),Image.Resampling.BOX).tobytes();assert tiles[key]==expected,(name,icon,'height')
    c,tiles=runtime['material'];assert c==144 and tiles[key]==packed[z*144:(z+1)*144,x*144:(x+1)*144].tobytes(),(name,icon,'normal')
    c,tiles=runtime['coast'];assert c==144 and tiles[key]==alpha[z*144:(z+1)*144,x*144:(x+1)*144].tobytes(),(name,icon,'coast data differs from saved source')
    runtime_cells+=1
   if deck_masks is not None:
    deck_report['runtime']=verify_runtime_road(r,a,np.asarray(Image.open(p/'ground_mask.png')),runtime,deck_masks)
  out.append(dict(name=name,rock_pixels=int(rock.sum()),protected_native_pixels=int(protected.sum()),native_crossing_albedo_exact=True,crossing_and_water_added_height=0,runtime_cells_verified=runtime_cells,bridge_deck=deck_report))
 return dict(passed=True,templates=out,runtime_verified=bool(playable),destroyed_bridge_ids=[170,172],destroyed_bridge_added_relief='zero; native broken bridge fallback retained',manifest_sha256=sha(manifest or d/'MANIFEST.json'),checker_sha256=sha(Path(__file__)),continuous_decks_required=require_decks)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--original',type=Path);p.add_argument('--playable',type=Path);p.add_argument('--output',type=Path);p.add_argument('--manifest',type=Path);p.add_argument('--require-bridge-decks',action='store_true');a=p.parse_args();result=run(a.source,a.original or a.source,a.playable,a.manifest,a.require_bridge_decks)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2))
