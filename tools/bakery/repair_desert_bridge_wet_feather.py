#!/usr/bin/env python3
"""Restore native partial-water bridge feather while retaining dry-land repair.

Versioned source-raster repair. The immutable pre-retirement land-alpha map,
not RGB or a candidate support mask, owns the wet selector. No runtime writes.
"""
from pathlib import Path
import argparse,copy,hashlib,json,shutil
import numpy as np
from PIL import Image,ImageDraw

DRY_ALPHA_MIN=248

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return np.asarray(Image.open(path)).copy()
def save(path,array):Image.fromarray(np.asarray(array,dtype='u1')).save(path)

def repair(project,out,wall_patch=None):
 directory=project/'art/remaster/textures/unified_daylight_v01'
 manifest=directory/'MANIFEST.json';manifest_bytes=manifest.read_bytes()
 current=json.loads(manifest_bytes)
 row=copy.deepcopy(next(r for r in current['assets']if(r['theater'],r['name'],r['variant'])==('DESERT','BRIDGE3',0)))
 baseline_export='desert_stone_boundaries_v09/BRIDGE3'
 original_export='desert_bridge_decks_v07/BRIDGE3'
 source=directory/baseline_export;original=directory/original_export
 # Always start from the frozen v09 source, even when rebaking after activation.
 frozen=json.loads((directory/'desert_stone_boundaries_v09/MANIFEST.rows.json').read_text())[0]
 assert frozen['export']==baseline_export
 row=copy.deepcopy(frozen)
 dest=out/'BRIDGE3';dest.mkdir(parents=True,exist_ok=True)
 for path in source.glob('*.png'):shutil.copy2(path,dest/path.name)
 alpha=read(original/'land_alpha.png');shape=alpha.shape
 assert sha(source/'land_alpha.png')==sha(original/'land_alpha.png')
 have=np.zeros(shape,bool)
 for icon in row['have']:
  z,x=divmod(icon,row['cells'][0]);have[z*144:(z+1)*144,x*144:(x+1)*144]=True
 wet=have&(alpha<DRY_ALPHA_MIN);dry=have&~wet
 arrays={p.name:read(p)for p in source.glob('*.png')}
 # A complete source-authored stone face can overlap native alpha feather.
 # In particular the user's foot cap has alpha177..234: it must retain the
 # repaired rock material, with zero world-ground coverage. Exterior skirts
 # and bank retirement are never evidence that a pixel is a rock body.
 actual_rock=arrays['complete_rock_semantic_mask.png']>127
 actual_wall=arrays['actual_wall_keep_mask.png']>127
 solid=actual_rock|actual_wall
 protected=wet&~solid
 before={name:arrays[name].copy()for name in ['albedo.png','ground_mask.png','rock_mask.png']}
 for name in before:arrays[name][protected]=read(original/name)[protected]
 assert not np.any(arrays['ground_mask.png'][wet&actual_rock]),'Actual wet-overlap rock body must exclude world ground'
 assert np.array_equal(arrays['ground_mask.png'][wet&actual_wall],read(original/'ground_mask.png')[wet&actual_wall]),'Native wall feather ground changed'
 # Every writer, including the alternate-mask appender, must exclude the wet
 # selector. Alternate wet coverage is its own native source, not base values.
 zero_wet=['ground_minimum_mask.png','retirement_support_mask.png','approach_weight.png',
  'bank_weight.png','approach_semantic_mask.png','retirement_rgb_weight.png',
  'rock_ground_skirt_support_mask.png','wall_shadow_support_mask.png',
  'wall_shadow_weight.png','rock_outside_feather_weight.png']
 for name in zero_wet:
  if name in arrays:arrays[name][wet]=0
 for name in ['boundary_support_mask.png','boundary_ground_override_mask.png','ground_override_mask.png','ground_override_values.png']:
  arrays[name][protected]=0
 arrays['complete_rock_semantic_mask.png']=arrays['rock_mask.png'].copy()
 # Restore historical source protection wherever the revision itself is undone.
 arrays['crossing_protected_mask.png'][protected]=read(original/'crossing_protected_mask.png')[protected]
 wall_metadata=None
 if wall_patch:
  patch=Path(wall_patch);region=patch/'wall_outline_regions.json'
  wall_metadata=json.loads(region.read_text())
  patchdir=patch/'BRIDGE3';support=read(patchdir/'wall_outline_support_mask.png')>127
  assert support.shape==shape and not np.any(support&~dry),'Wall strip intersects protected wet feather'
  assert np.all(alpha[support]==255),'Wall strip must be opaque source land'
  for name in ['albedo.png','ground_mask.png','ground_minimum_mask.png']:
   arrays[name][support]=read(patchdir/name)[support]
  arrays['ground_override_mask.png'][support]=255
  arrays['boundary_support_mask.png'][support]=255
  arrays['boundary_ground_override_mask.png'][support]=255
  arrays['crossing_protected_mask.png'][support]=0
  for name in ['actual_wall_keep_mask.png','masonry_semantic_mask.png']:
   arrays[name]=read(patchdir/name)
  arrays['wall_outline_support_mask.png']=np.uint8(support)*255
  arrays['wall_outline_weight.png']=read(patchdir/'wall_outline_weight.png')
  shutil.copy2(region,out/'wall_outline_regions.json')
 arrays['ground_override_values.png']=arrays['ground_mask.png']*np.uint8(arrays['ground_override_mask.png']>127)
 arrays['wet_protection_mask.png']=np.uint8(protected)*255
 arrays['native_partial_water_mask.png']=np.uint8(wet)*255
 arrays['partial_solid_body_mask.png']=np.uint8(wet&solid)*255
 arrays['dry_allowed_mask.png']=np.uint8(dry)*255
 for name,array in arrays.items():save(dest/name,array)
 for name in ['height.png','land_alpha.png','player_colour_mask.png']+[p.name for p in source.glob('margin_*.png')]:
  assert sha(dest/name)==sha(source/name),(name,'immutable map')
 for name in before:assert np.array_equal(arrays[name][protected],read(original/name)[protected]),(name,'native wet feather differs')
 assert not np.any(arrays['ground_minimum_mask.png'][wet])
 assert not np.any(arrays['ground_override_mask.png'][protected])
 changed=np.zeros(shape,bool)
 for name in before:
  difference=arrays[name]!=before[name]
  if difference.ndim==3:difference=np.any(difference,axis=-1)
  changed|=difference
 metadata=dict(schema=1,units='whole-template Enhanced source pixels;144px/cell;top-left origin',
  base_export=baseline_export,base_row_source='desert_stone_boundaries_v09/MANIFEST.rows.json',
  base_row_sha256=sha(directory/'desert_stone_boundaries_v09/MANIFEST.rows.json'),
  native_wet_source_export=original_export,native_land_alpha_sha256=sha(original/'land_alpha.png'),
  native_wet_source_hashes={name:sha(original/name)for name in before},
  dry_alpha_min=DRY_ALPHA_MIN,
  wet_authority='occupied immutable native land_alpha <248; no RGB/hue/candidate-mask threshold',
  solid_exception='Complete source-authored rock polygons and actual wall silhouettes, independently reconstructed from frozen v09 regions; retain material RGB/mask and rock G0, plus native wall edge coverage. Exterior feather/skirt is never a solid exception.',
  dry_exception='alpha248..255 retains reviewed dry bank/rock repair, including source guide at249..253',
  active_masks_zero_on_wet=zero_wet,
  alternate_contract='Zero wet exterior retirement/minimum/exact-override support; preserve exterior original alternate coverage and shade independently of base. Actual rock body stays G0; native wall edge coverage remains exact.',
  wall_patch=wall_metadata,
  wall_patch_metadata_sha256=sha(out/'wall_outline_regions.json')if wall_metadata else None,
  preserve_exact=['native exterior visible wet RGB,base ground,rock mask','complete native stone body RGB/material with G0','height/coast/player/margins','dry repair outside saved opaque wall strip'])
 region=out/'wet_feather_regions.json';region.write_text(json.dumps(metadata,indent=2)+'\n')
 row.update(pre_bridge_wet_feather_v10_export=baseline_export,export=str(dest.relative_to(directory)),
  bridge_wet_feather_region_source=str(region.relative_to(directory)),bridge_wet_feather_region_sha256=sha(region),
  bridge_wet_feather_recipe='Native partial-water exterior RGB/coverage restored; literal complete stone material retained with G0; all wet exterior retirement writers disabled; reviewed alpha249–253 bank guide cleanup retained.')
 row['rock_pixels']=int((arrays['rock_mask.png']>127).sum())
 for name in arrays:
  if name!='crossing_protected_mask.png':row[name[:-4]+'_sha256']=sha(dest/name)
 for margin in row['margins']:
  margin['file']=str((dest/Path(margin['file']).name).relative_to(directory));margin['sha256']=sha(directory/margin['file'])
 (out/'MANIFEST.rows.json').write_text(json.dumps([row],indent=2)+'\n')
 candidate=copy.deepcopy(current)
 for i,r in enumerate(candidate['assets']):
  if(r['theater'],r['name'],r['variant'])==('DESERT','BRIDGE3',0):candidate['assets'][i]=row
 (out/'MANIFEST.candidate.json').write_text(json.dumps(candidate,indent=2)+'\n')
 report=dict(candidate_only=True,canonical_manifest_updated=False,protected_wet_pixels=int(protected.sum()),
  protected_partial_wet_pixels=int((protected&(alpha>0)).sum()),partial_wet_pixels=int((wet&(alpha>0)).sum()),partial_solid_body_pixels=int((wet&solid).sum()),
  restored_wet_rgb_pixels=int(np.any(arrays['albedo.png']!=before['albedo.png'],axis=-1)[protected].sum()),
  restored_wet_ground_pixels=int((arrays['ground_mask.png']!=before['ground_mask.png'])[protected].sum()),
  changed_total_rgb_pixels=int(np.any(arrays['albedo.png']!=before['albedo.png'],axis=-1).sum()),
  changed_total_ground_pixels=int((arrays['ground_mask.png']!=before['ground_mask.png']).sum()),
  restored_rock_pixels=int((arrays['rock_mask.png']!=before['rock_mask.png']).sum()),
  dry_source_unchanged_except_opaque_wall_strip=True,rock_pixels=row['rock_pixels'],
  source_manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),outputs={n:sha(dest/n)for n in before})
 (out/'REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
 for label,box in [('whole',(0,0,864,720)),('river-left',(0,270,360,450)),('river-right',(420,415,864,605)),('bank-guide',(480,480,700,620))]:
  a=Image.fromarray(before['albedo.png']).crop(box);b=Image.fromarray(arrays['albedo.png']).crop(box)
  sheet=Image.new('RGB',(a.width*2,a.height+22));sheet.paste(a,(0,22));sheet.paste(b,(a.width,22));draw=ImageDraw.Draw(sheet);draw.text((4,5),'v09 source',fill='white');draw.text((a.width+4,5),'v10 protected wet feather',fill='white');sheet.save(out/(label+'-before-after.png'))
 assert manifest.read_bytes()==manifest_bytes,'Active manifest modified'
 print(json.dumps(report,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--wall-patch',type=Path)
 a=p.parse_args();repair(a.project,a.out or a.project/'art/remaster/textures/unified_daylight_v01/desert_bridge_wet_feather_v10',a.wall_patch)
