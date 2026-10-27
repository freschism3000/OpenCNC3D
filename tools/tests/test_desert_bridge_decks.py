#!/usr/bin/env python3
"""Source-semantic controls for continuous intact Desert bridge roads."""
from pathlib import Path
import importlib.util
import copy
import hashlib
import json
import shutil
import struct
import tempfile
import unittest
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location('crossings_gate',ROOT/'tools/gate-desert-crossings.py')
GATE=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)
ART=ROOT/'art/remaster/textures/unified_daylight_v01'

def maps(name):
 directory=ART/'desert_crossings_v06'/name
 return [np.array(Image.open(directory/filename)) for filename in
         ['ground_mask.png','height.png','rock_mask.png','land_alpha.png']]

class DesertBridgeRoadControls(unittest.TestCase):
 def test_actual_v06_bridge3_road_holes_are_rejected(self):
  arrays=maps('BRIDGE3')
  lane=GATE.road_lane_mask('BRIDGE3',arrays[0].shape)
  self.assertEqual(int(lane.sum()),19952)
  self.assertEqual(int((arrays[0][lane]>0).sum()),12210)
  with self.assertRaisesRegex(AssertionError,'CLEAR1 coverage'):
   GATE.verify_road_lane('BRIDGE3',*arrays)

 def test_actual_v06_bridge4_continuous_road_passes(self):
  report=GATE.verify_road_lane('BRIDGE4',*maps('BRIDGE4'))
  self.assertEqual(report['semantic_road_pixels'],21710)

 def test_single_ground_pixel_on_road_is_rejected(self):
  arrays=maps('BRIDGE4');arrays[0][250,360]=1
  with self.assertRaisesRegex(AssertionError,'CLEAR1 coverage'):
   GATE.verify_road_lane('BRIDGE4',*arrays)

 def test_added_height_on_road_is_rejected(self):
  arrays=maps('BRIDGE4');arrays[1][250,360]=1
  with self.assertRaisesRegex(AssertionError,'road added relief'):
   GATE.verify_road_lane('BRIDGE4',*arrays)

 def test_rock_mask_on_road_is_rejected(self):
  arrays=maps('BRIDGE4');arrays[2][250,360]=255
  with self.assertRaisesRegex(AssertionError,'road treated as bank rock'):
   GATE.verify_road_lane('BRIDGE4',*arrays)

 def test_water_hole_on_road_is_rejected(self):
  arrays=maps('BRIDGE4');arrays[3][250,360]=0
  with self.assertRaisesRegex(AssertionError,'water cut'):
   GATE.verify_road_lane('BRIDGE4',*arrays)

 def test_ground_outside_road_does_not_define_road_eligibility(self):
  arrays=maps('BRIDGE4');arrays[0][0,200]=255
  GATE.verify_road_lane('BRIDGE4',*arrays)

class DesertBridgeV07PreservationControls(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.scratch=tempfile.TemporaryDirectory(prefix='cnc3d-bridge-deck-gate-')
  cls.directory=Path(cls.scratch.name)
  manifest=json.loads((ART/'desert_bridge_decks_v07/MANIFEST.candidate.json').read_text())
  cls.rows={r['name']:r for r in manifest['assets']if r['theater']=='DESERT'and r['name']in ['BRIDGE3','BRIDGE4']and not r['variant']}
  for version in ['desert_crossings_v06','desert_bridge_decks_v07']:
   for name in cls.rows:
    shutil.copytree(ART/version/name,cls.directory/version/name)
  shutil.copy2(ART/'desert_bridge_decks_v07/deck_regions.json',cls.directory/'desert_bridge_decks_v07/deck_regions.json')

 @classmethod
 def tearDownClass(cls):cls.scratch.cleanup()

 def mutation(self,filename,point,message):
  row=self.rows['BRIDGE3'];path=self.directory/row['export']/filename;old=path.read_bytes()
  array=np.array(Image.open(path));array[point]^=1
  try:
   Image.fromarray(array).save(path)
   with self.assertRaisesRegex(AssertionError,message):GATE.verify_bridge_deck(self.directory,row)
  finally:path.write_bytes(old)

 def test_actual_v07_both_roads_and_masonry_pass(self):
  for name,row in self.rows.items():
   report,_=GATE.verify_bridge_deck(self.directory,row)
   self.assertGreater(report['deck_core_pixels'],30000)
   self.assertGreater(report['masonry_pixels_exact'],20000)

 def test_ground_hole_in_repaired_road_is_rejected(self):
  self.mutation('ground_mask.png',(350,400),'semantic road core')

 def test_rgb_change_on_independent_masonry_is_rejected(self):
  row=self.rows['BRIDGE3'];mask=np.array(Image.open(self.directory/row['export']/'masonry_semantic_mask.png'))>127
  y,x=np.argwhere(mask)[len(np.argwhere(mask))//2]
  self.mutation('albedo.png',(y,x,0),'outside deck support|masonry')

 def test_rgb_change_outside_authorized_support_is_rejected(self):
  self.mutation('albedo.png',(200,100,0),'outside deck support')

 def test_restoring_old_sand_paint_with_fixed_ground_mask_is_rejected(self):
  row=self.rows['BRIDGE3'];path=self.directory/row['export']/'albedo.png';old=path.read_bytes()
  try:
   shutil.copy2(self.directory/row['pre_bridge_decks_v07_export']/'albedo.png',path)
   with self.assertRaisesRegex(AssertionError,'continuous-road donor'):GATE.verify_bridge_deck(self.directory,row)
  finally:path.write_bytes(old)

 def test_immutable_water_cut_change_is_rejected(self):
  self.mutation('land_alpha.png',(350,400),'immutable map')

 def test_forged_empty_masonry_mask_cannot_vacuously_pass(self):
  row=copy.deepcopy(self.rows['BRIDGE3']);path=self.directory/row['export']/'masonry_semantic_mask.png';old=path.read_bytes()
  try:
   with Image.open(path)as image:size=image.size
   Image.new('L',size,0).save(path)
   row['masonry_semantic_mask_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   with self.assertRaisesRegex(AssertionError,'masonry semantic polygon differs'):GATE.verify_bridge_deck(self.directory,row)
  finally:path.write_bytes(old)

 def runtime_fixture(self):
  row=self.rows['BRIDGE3'];directory=self.directory/row['export'];_,deck=GATE.verify_bridge_deck(self.directory,row)
  albedo=np.array(Image.open(directory/'albedo.png'));ground=np.array(Image.open(directory/'ground_mask.png'))
  old=np.array(Image.open(self.directory/row['pre_bridge_decks_v07_export']/'ground_mask.png'))
  base_tiles={};old_tiles={};rgb_tiles={}
  for icon in row['have']:
   z,x=divmod(icon,row['cells'][0]);box=(slice(z*144,(z+1)*144),slice(x*144,(x+1)*144));key=(row['tid'],icon,0,0,0);alt_key=key[:-1]+(1,)
   base_tiles[key]=np.dstack((ground[box],np.full((144,144),128,'u1')))
   original=np.dstack((old[box],np.full((144,144),128,'u1')));old_tiles[alt_key]=original.copy()
   repaired=original.copy();support=deck['support'][box];weight=deck['end_weight'][box]
   repaired[...,0][support]=np.uint8(np.rint(original[...,0][support]*(1-weight[support]/255.0)))
   base_tiles[alt_key]=repaired;rgb_tiles[(row['tid'],icon)]=albedo[box].tobytes()
  runtime=dict(ground=(144,base_tiles),rgb=(144,rgb_tiles),details=set(),original_alternates=(144,old_tiles))
  return row,albedo,ground,runtime,deck

 def test_actual_source_paired_base_and_alternate_fixture_passes(self):
  report=GATE.verify_runtime_road(*self.runtime_fixture())
  self.assertEqual(report['alternate_cells_exact'],22)
  self.assertFalse(report['alternate_can_be_selected'])

 def test_old_alternate_mask_resurrection_is_rejected_even_when_inactive(self):
  row,albedo,ground,runtime,deck=self.runtime_fixture()
  # This is the actual v06 road-hole painting, not a fabricated hue threshold.
  for key,old in runtime['original_alternates'][1].items():runtime['ground'][1][key]=old
  with self.assertRaisesRegex(AssertionError,'alternate ground repair'):
   GATE.verify_runtime_road(row,albedo,ground,runtime,deck)

 def test_alternate_edit_outside_support_is_rejected(self):
  row,albedo,ground,runtime,deck=self.runtime_fixture();key=(row['tid'],3,0,0,1)
  runtime['ground'][1][key][0,0,0]^=1
  with self.assertRaisesRegex(AssertionError,'outside support changed'):
   GATE.verify_runtime_road(row,albedo,ground,runtime,deck)

class DesertBridgeApproachV08Controls(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.scratch=tempfile.TemporaryDirectory(prefix='cnc3d-approach-gate-');cls.directory=Path(cls.scratch.name)
  manifest=json.loads((ART/'desert_bridge_approaches_v08/MANIFEST.candidate.json').read_text())
  cls.rows={r['name']:r for r in manifest['assets']if r['theater']=='DESERT'and r['name']in ['BRIDGE3','BRIDGE4']and not r['variant']}
  for version in ['desert_crossings_v06','desert_bridge_decks_v07','desert_bridge_approaches_v08']:
   for name in cls.rows:shutil.copytree(ART/version/name,cls.directory/version/name)
  for version,filename in [('desert_bridge_decks_v07','deck_regions.json'),('desert_bridge_approaches_v08','approach_regions.json'),('desert_bridge_approaches_v08','clear_grain_source.png')]:
   shutil.copy2(ART/version/filename,cls.directory/version/filename)
  (cls.directory/'MANIFEST.json').write_text(json.dumps(manifest))
  spec=importlib.util.spec_from_file_location('detail_stager',ROOT/'tools/art/terrain/desert_details.py');cls.stager=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.stager)

 @classmethod
 def tearDownClass(cls):cls.scratch.cleanup()

 def test_actual_old_approach_fragments_fail_independent_controls(self):
  for name in self.rows:
   ground=np.asarray(Image.open(ART/'desert_bridge_decks_v07'/name/'ground_mask.png'))
   with self.assertRaisesRegex(AssertionError,'old road/approach fragment'):GATE.verify_approach_controls(name,ground)

 def test_actual_v08_full_source_and_all_independent_controls_pass(self):
  for row in self.rows.values():
   report,_=GATE.verify_bridge_approaches(self.directory,row)
   self.assertGreater(report['approach']['full_approach_pixels'],50000)

 def test_restoring_old_approach_albedo_is_rejected(self):
  row=self.rows['BRIDGE3'];path=self.directory/row['export']/'albedo.png';saved=path.read_bytes()
  try:
   shutil.copy2(self.directory/row['pre_bridge_approaches_v08_export']/'albedo.png',path)
   with self.assertRaisesRegex(AssertionError,'source-ground blend differs'):GATE.verify_bridge_approaches(self.directory,row)
  finally:path.write_bytes(saved)

 def test_restoring_old_bank_coverage_is_rejected(self):
  row=self.rows['BRIDGE3'];path=self.directory/row['export']/'ground_mask.png';saved=path.read_bytes()
  try:
   array=np.array(Image.open(path));array[534,528]=253;Image.fromarray(array).save(path)
   with self.assertRaisesRegex(AssertionError,'minimum application differs'):GATE.verify_bridge_approaches(self.directory,row)
  finally:path.write_bytes(saved)

 def test_alpha0_water_cannot_be_changed_by_land_extension(self):
  row=self.rows['BRIDGE3'];path=self.directory/row['export']/'albedo.png';saved=path.read_bytes()
  try:
   array=np.array(Image.open(path));alpha=np.array(Image.open(self.directory/row['export']/'land_alpha.png'));y,x=np.argwhere(alpha==0)[100];array[y,x,0]^=1;Image.fromarray(array).save(path)
   with self.assertRaisesRegex(AssertionError,'preserved RGB'):GATE.verify_bridge_approaches(self.directory,row)
  finally:path.write_bytes(saved)

 def test_real_stager_preserves_shade_outside_support_and_is_idempotent(self):
  row=self.rows['BRIDGE3'];icon=9;z,x=divmod(icon,row['cells'][0]);box=(slice(z*144,(z+1)*144),slice(x*144,(x+1)*144))
  old=np.array(Image.open(ART/'desert_crossings_v06/BRIDGE3/ground_mask.png'))[box]
  prior=np.array(Image.open(ART/'desert_bridge_decks_v07/BRIDGE3/ground_mask.png'))[box]
  expected=np.array(Image.open(self.directory/row['export']/'ground_mask.png'))[box]
  shade=np.arange(144*144,dtype='u1').reshape(144,144)
  key=lambda alternate:b'BRIDGE3'.ljust(16,b'\0')+struct.pack('<HHBBH',169,icon,0,0,alternate)
  def pack(path,alternate,coverage):
   path.write_bytes(b'C3DGRAS\0'+struct.pack('<III',1,144,1)+key(alternate)+np.dstack((coverage,shade)).tobytes())
  with tempfile.TemporaryDirectory(prefix='cnc3d-ground-stage-')as scratch:
   base,extra=Path(scratch)/'base.pack',Path(scratch)/'extra.pack';pack(base,0,prior);pack(extra,1,old)
   self.stager.append_masks(base,extra,surface_source=self.directory)
   _,tiles=GATE.ground_records(base)
   self.assertTrue(np.array_equal(tiles[(169,icon,0,0,0)][...,0],expected))
   support=np.array(Image.open(self.directory/row['export']/'retirement_support_mask.png'))[box]>0
   deck_support=np.array(Image.open(self.directory/row['export']/'deck_support_mask.png'))[box]>0
   self.assertTrue(np.array_equal(tiles[(169,icon,0,0,1)][...,0][~(support|deck_support)],old[~(support|deck_support)]))
   for array in tiles.values():self.assertTrue(np.array_equal(array[...,1],shade))
   self.assertGreaterEqual(int(tiles[(169,icon,0,0,1)][36,88,0]),200)
   frozen=base.read_bytes();self.stager.append_masks(base,extra,surface_source=self.directory);self.assertEqual(base.read_bytes(),frozen)

class DesertStoneBoundaryV09Controls(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.scratch=tempfile.TemporaryDirectory(prefix='cnc3d-stone-boundary-gate-');cls.directory=Path(cls.scratch.name)
  cls.row=json.loads((ART/'desert_stone_boundaries_v09/MANIFEST.rows.json').read_text())[0]
  for version in ['desert_crossings_v06','desert_bridge_decks_v07','desert_bridge_approaches_v08']:
   for name in ['BRIDGE3','BRIDGE4']:shutil.copytree(ART/version/name,cls.directory/version/name)
  shutil.copytree(ART/'desert_stone_boundaries_v09/BRIDGE3',cls.directory/'desert_stone_boundaries_v09/BRIDGE3')
  for version,filenames in [('desert_bridge_decks_v07',['deck_regions.json']),('desert_bridge_approaches_v08',['MANIFEST.candidate.json','approach_regions.json','clear_grain_source.png']),('desert_stone_boundaries_v09',['boundary_regions.json'])]:
   for filename in filenames:shutil.copy2(ART/version/filename,cls.directory/version/filename)
  (cls.directory/'MANIFEST.json').write_text(json.dumps(dict(assets=[cls.row])))
  spec=importlib.util.spec_from_file_location('boundary_stager',ROOT/'tools/art/terrain/desert_details.py');cls.stager=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.stager)

 @classmethod
 def tearDownClass(cls):cls.scratch.cleanup()

 def test_actual_v09_source_controls_and_preservation_pass(self):
  report,_=GATE.verify_bridge_stone_boundaries(self.directory,self.row)
  self.assertEqual(report['stone_boundaries']['new_complete_rock_pixels'],12989)
  self.assertEqual(report['stone_boundaries']['actual_wall_pixels_exact'],60091)

 def test_actual_wall_change_fails_semantics_even_with_updated_hash(self):
  row=copy.deepcopy(self.row);path=self.directory/row['export']/'albedo.png';saved=path.read_bytes()
  try:
   array=np.array(Image.open(path));array[211,347,0]^=1;Image.fromarray(array).save(path);row['albedo_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   with self.assertRaisesRegex(AssertionError,'preserved wall/deck/water'):GATE.verify_bridge_stone_boundaries(self.directory,row)
  finally:path.write_bytes(saved)

 def test_omitting_new_warm_face_fails_whole_form_semantics(self):
  row=copy.deepcopy(self.row);path=self.directory/row['export']/'rock_mask.png';saved=path.read_bytes()
  try:
   array=np.array(Image.open(path));array[228,150]=0;Image.fromarray(array).save(path);row['rock_mask_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   with self.assertRaisesRegex(AssertionError,'full native rock form shrank'):GATE.verify_bridge_stone_boundaries(self.directory,row)
  finally:path.write_bytes(saved)

 def test_real_stager_exact_override_can_lower_base_and_alternate(self):
  row=self.row;icon=7;z,x=divmod(icon,row['cells'][0]);box=(slice(z*144,(z+1)*144),slice(x*144,(x+1)*144))
  old=np.array(Image.open(ART/'desert_crossings_v06/BRIDGE3/ground_mask.png'))[box]
  prior=np.array(Image.open(ART/'desert_bridge_approaches_v08/BRIDGE3/ground_mask.png'))[box]
  expected=np.array(Image.open(self.directory/row['export']/'ground_mask.png'))[box]
  shade=np.arange(144*144,dtype='u1').reshape(144,144)
  def pack(path,alternate,coverage):
   key=b'BRIDGE3'.ljust(16,b'\0')+struct.pack('<HHBBH',169,icon,0,0,alternate)
   path.write_bytes(b'C3DGRAS\0'+struct.pack('<III',1,144,1)+key+np.dstack((coverage,shade)).tobytes())
  with tempfile.TemporaryDirectory(prefix='cnc3d-boundary-stage-')as scratch:
   base,extra=Path(scratch)/'base.pack',Path(scratch)/'extra.pack';pack(base,0,prior);pack(extra,1,old)
   self.assertGreater(int(prior[84,6]),0)
   self.stager.append_masks(base,extra,surface_source=self.directory);_,tiles=GATE.ground_records(base)
   self.assertTrue(np.array_equal(tiles[(169,icon,0,0,0)][...,0],expected))
   for alternate in [0,1]:
    tile=tiles[(169,icon,0,0,alternate)];self.assertEqual(int(tile[84,6,0]),0);self.assertTrue(np.array_equal(tile[...,1],shade))
   frozen=base.read_bytes();self.stager.append_masks(base,extra,surface_source=self.directory);self.assertEqual(base.read_bytes(),frozen)

 def test_exact_override_cannot_write_outside_support(self):
  row=copy.deepcopy(self.row);path=self.directory/row['export']/'ground_override_values.png';saved=path.read_bytes();manifest=(self.directory/'MANIFEST.json').read_bytes()
  try:
   array=np.array(Image.open(path));array[0,0]=1;Image.fromarray(array).save(path);row['ground_override_values_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();(self.directory/'MANIFEST.json').write_text(json.dumps(dict(assets=[row])))
   with self.assertRaisesRegex(ValueError,'exceeds or differs'):self.stager.exact_ground_overrides(self.directory,144)
  finally:path.write_bytes(saved);(self.directory/'MANIFEST.json').write_bytes(manifest)

class DesertWetFeatherV10Controls(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.scratch=tempfile.TemporaryDirectory(prefix='cnc3d-wet-feather-gate-');cls.directory=Path(cls.scratch.name)
  for version in ['desert_crossings_v06','desert_bridge_decks_v07','desert_bridge_approaches_v08','desert_stone_boundaries_v09','desert_bridge_wet_feather_v10']:
   shutil.copytree(ART/version,cls.directory/version)
  cls.row=json.loads((cls.directory/'desert_bridge_wet_feather_v10/MANIFEST.rows.json').read_text())[0]
  (cls.directory/'MANIFEST.json').write_text(json.dumps(dict(assets=[cls.row])))
  spec=importlib.util.spec_from_file_location('wet_feather_stager',ROOT/'tools/art/terrain/desert_details.py');cls.stager=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.stager)

 @classmethod
 def tearDownClass(cls):cls.scratch.cleanup()

 def fields(self,folder):
  return {name:np.array(Image.open(folder/name))for name in ['albedo.png','ground_mask.png','rock_mask.png']}

 def test_actual_v10_native_exterior_and_uniform_foot_cap_pass(self):
  report,_=GATE.verify_bridge_wet_feather(self.directory,self.row)
  self.assertEqual(report['wet_feather']['protected_visible_exterior_pixels'],66263)
  self.assertEqual(report['wet_feather']['partial_alpha_rock_pixels'],5163)

 def test_actual_v09_regression_fails_native_visible_wet_selector(self):
  old=self.directory/'desert_stone_boundaries_v09/BRIDGE3';native=self.directory/'desert_bridge_decks_v07/BRIDGE3'
  alpha=np.array(Image.open(native/'land_alpha.png'));have=np.array(Image.open(self.directory/self.row['export']/'native_partial_water_mask.png'))>0
  rock=np.array(Image.open(old/'complete_rock_semantic_mask.png'))>0;wall=np.array(Image.open(old/'actual_wall_keep_mask.png'))>0
  with self.assertRaisesRegex(AssertionError,'visible wet exterior differs'):
   GATE.verify_wet_feather_pixels('BRIDGE3',alpha,have,rock,wall,self.fields(old),self.fields(native))

 def test_partial_alpha_water_rgb_mutation_fails_even_with_updated_hash(self):
  row=copy.deepcopy(self.row);folder=self.directory/row['export'];path=folder/'albedo.png';saved=path.read_bytes()
  try:
   alpha=np.array(Image.open(folder/'land_alpha.png'));protected=np.array(Image.open(folder/'wet_protection_mask.png'))>0
   y,x=np.argwhere(protected&(alpha>0))[100];array=np.array(Image.open(path));array[y,x,0]^=1;Image.fromarray(array).save(path);row['albedo_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   with self.assertRaisesRegex(AssertionError,'visible wet exterior differs'):GATE.verify_bridge_wet_feather(self.directory,row)
  finally:path.write_bytes(saved)

 def test_wet_ground_writer_resurrection_fails_even_if_raw_source_is_correct(self):
  row=copy.deepcopy(self.row);folder=self.directory/row['export'];path=folder/'ground_minimum_mask.png';saved=path.read_bytes()
  try:
   alpha=np.array(Image.open(folder/'land_alpha.png'));protected=np.array(Image.open(folder/'wet_protection_mask.png'))>0
   y,x=np.argwhere(protected&(alpha>0))[100];array=np.array(Image.open(path));array[y,x]=255;Image.fromarray(array).save(path);row['ground_minimum_mask_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   with self.assertRaisesRegex(AssertionError,'composition differs'):GATE.verify_bridge_wet_feather(self.directory,row)
  finally:path.write_bytes(saved)

 def test_undoing_partial_alpha_foot_cap_rgb_is_rejected(self):
  row=copy.deepcopy(self.row);folder=self.directory/row['export'];path=folder/'albedo.png';saved=path.read_bytes()
  try:
   array=np.array(Image.open(path));native=np.array(Image.open(self.directory/'desert_bridge_decks_v07/BRIDGE3/albedo.png'));array[511,470]=native[511,470];Image.fromarray(array).save(path);row['albedo_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   with self.assertRaisesRegex(AssertionError,'composition differs'):GATE.verify_bridge_wet_feather(self.directory,row)
  finally:path.write_bytes(saved)

 def test_real_stager_keeps_independent_base_and_alternate_wet_values(self):
  folder=self.directory/self.row['export'];protected=np.array(Image.open(folder/'wet_protection_mask.png'))>0;rock=np.array(Image.open(folder/'rock_mask.png'))>0
  # Pick the source tile with the most actual visible wet support, independently
  # of candidate ground values. Deliberately distinct variant values catch an
  # accidental common base override which ordinary equal-value fixtures miss.
  choices=[]
  for icon in self.row['have']:
   z,x=divmod(icon,self.row['cells'][0]);box=slice(z*144,(z+1)*144),slice(x*144,(x+1)*144);choices.append((int(protected[box].sum()),icon,box))
  count,icon,box=max(choices,key=lambda q:q[0]);self.assertGreater(count,10000)
  base_values=np.array(Image.open(folder/'ground_mask.png'))[box];native_values=np.array(Image.open(self.directory/'desert_bridge_decks_v07/BRIDGE3/ground_mask.png'))[box]
  yy,xx=np.indices((144,144));alternate_values=np.uint8((xx+3*yy)%248);shade=np.uint8((xx+yy)%256)
  def pack(path,alternate,coverage):
   key=b'BRIDGE3'.ljust(16,b'\0')+struct.pack('<HHBBH',169,icon,0,0,alternate)
   path.write_bytes(b'C3DGRAS\0'+struct.pack('<III',1,144,1)+key+np.dstack((coverage,shade)).tobytes())
  with tempfile.TemporaryDirectory(prefix='cnc3d-wet-variant-stage-')as scratch:
   base,extra=Path(scratch)/'base.pack',Path(scratch)/'extra.pack';pack(base,0,base_values);pack(extra,1,alternate_values)
   self.stager.append_masks(base,extra,surface_source=self.directory);_,tiles=GATE.ground_records(base)
   mask=protected[box]
   self.assertTrue(np.array_equal(tiles[(169,icon,0,0,0)][...,0][mask],native_values[mask]))
   self.assertTrue(np.array_equal(tiles[(169,icon,0,0,1)][...,0][mask],alternate_values[mask]))
   for variant in [0,1]:self.assertTrue(np.array_equal(tiles[(169,icon,0,0,variant)][...,1],shade))
   frozen=base.read_bytes();self.stager.append_masks(base,extra,surface_source=self.directory);self.assertEqual(base.read_bytes(),frozen)

if __name__=='__main__':unittest.main()
