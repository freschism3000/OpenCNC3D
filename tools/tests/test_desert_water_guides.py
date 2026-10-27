#!/usr/bin/env python3
"""Non-vacuous controls for source-painted river guides and frozen alternates."""
from pathlib import Path
import copy
import hashlib
import importlib.util
import json
import shutil
import struct
import tempfile
import unittest
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
ART=ROOT/'art/remaster/textures/unified_daylight_v01'
ORIGINAL=Path.home()/'CNC3D'
SPEC=importlib.util.spec_from_file_location('water_guide_gate',ROOT/'tools/gate-desert-crossings.py')
GATE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(GATE)

class DesertWaterGuideControls(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.scratch=tempfile.TemporaryDirectory(prefix='cnc3d-water-guide-controls-')
  cls.directory=Path(cls.scratch.name)
  for path in ART.iterdir():
   if path.is_dir()and path.name!='desert_bridge_water_guides_v11':
    (cls.directory/path.name).symlink_to(path,target_is_directory=True)
  shutil.copytree(ART/'desert_bridge_water_guides_v11',cls.directory/'desert_bridge_water_guides_v11')
  cls.rows=json.loads((cls.directory/'desert_bridge_water_guides_v11/MANIFEST.rows.json').read_text())
  (cls.directory/'MANIFEST.json').write_text(json.dumps(dict(assets=cls.rows)))
  spec=importlib.util.spec_from_file_location('water_guide_stager',ROOT/'tools/art/terrain/desert_details.py')
  cls.stager=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.stager)

 @classmethod
 def tearDownClass(cls):cls.scratch.cleanup()

 def mutation(self,name,filename,xy,value,message):
  row=copy.deepcopy(next(r for r in self.rows if r['name']==name))
  path=self.directory/row['export']/filename;saved=path.read_bytes();array=np.array(Image.open(path));x,y=xy;array[y,x]=value
  try:
   Image.fromarray(array).save(path);row[filename[:-4]+'_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
   with self.assertRaisesRegex(AssertionError,message):GATE.verify_bridge_water_guides(self.directory,row,ORIGINAL)
  finally:path.write_bytes(saved)

 def test_prior_false_opaque_islands_are_rejected(self):
  for row in self.rows:
   alpha=np.array(Image.open(self.directory/row['pre_water_guides_v11_export']/'land_alpha.png'))
   for x,y in GATE.WATER_GUIDE_CONTROLS[row['name']]:self.assertEqual(int(alpha[y,x]),255)
   with self.assertRaisesRegex(AssertionError,'painted-water guide remains opaque'):
    GATE.verify_water_guide_controls(row['name'],alpha)

 def test_full_candidates_and_prior_preservation_pass(self):
  for row in self.rows:
   report,_=GATE.verify_bridge_water_guides(self.directory,row,ORIGINAL)
   self.assertTrue(report['water_guides']['old_v10_opaque_negative_rejected'])
   self.assertGreater(report['water_guides']['full_water_core_pixels'],40000)
   self.assertFalse(report['native_coast_exact'])

 def test_reintroduced_opaque_guide_core_is_rejected(self):
  self.mutation('BRIDGE3','land_alpha.png',(192,336),255,'coast data differs')

 def test_world_ground_on_new_water_core_is_rejected(self):
  self.mutation('BRIDGE4','ground_mask.png',(101,450),255,'water admits world ground')

 def test_water_hole_in_real_road_is_rejected(self):
  self.mutation('BRIDGE3','land_alpha.png',(360,250),0,'coast data differs')

 def test_actual_wall_rgb_is_preserved(self):
  self.mutation('BRIDGE3','albedo.png',(347,211),(255,0,255),'native water RGB restoration differs')

 def test_complete_partial_alpha_rock_cap_is_preserved(self):
  self.mutation('BRIDGE3','albedo.png',(470,511),(255,0,255),'native water RGB restoration differs')

 def test_unreviewed_coast_edit_is_rejected(self):
  self.mutation('BRIDGE3','land_alpha.png',(700,100),0,'coast data differs')

 def test_exact_override_clears_all_reported_cores_in_base_and_alternate(self):
  keys=[];pixels=[];checks=[];shade=np.arange(144*144,dtype='u1').reshape(144,144)
  for row in self.rows:
   for x,y in GATE.WATER_GUIDE_CONTROLS[row['name']]:
    icon=(y//144)*row['cells'][0]+x//144
    if(row['tid'],icon)not in checks:
     checks.append((row['tid'],icon));keys.append(row['name'].encode().ljust(16,b'\0')+struct.pack('<HHBBH',row['tid'],icon,0,0,0))
     pixels.append(np.dstack((np.full((144,144),255,'u1'),shade)).tobytes())
  with tempfile.TemporaryDirectory(prefix='cnc3d-water-guide-stage-')as scratch:
   base=Path(scratch)/'base.pack';extra=Path(scratch)/'extra.pack'
   base.write_bytes(b'C3DGRAS\0'+struct.pack('<III',1,144,len(keys))+b''.join(keys)+b''.join(pixels))
   alternate=[key[:-2]+struct.pack('<H',1)for key in keys]
   extra.write_bytes(b'C3DGRAS\0'+struct.pack('<III',1,144,len(keys))+b''.join(alternate)+b''.join(pixels))
   self.stager.append_masks(base,extra,surface_source=self.directory)
   _,tiles=GATE.ground_records(base)
   for row in self.rows:
    for x,y in GATE.WATER_GUIDE_CONTROLS[row['name']]:
     icon=(y//144)*row['cells'][0]+x//144
     for alt in[0,1]:
      tile=tiles[(row['tid'],icon,0,0,alt)];self.assertEqual(int(tile[y%144,x%144,0]),0)
      self.assertTrue(np.array_equal(tile[...,1],shade))
   frozen=base.read_bytes();self.stager.append_masks(base,extra,surface_source=self.directory);self.assertEqual(base.read_bytes(),frozen)

if __name__=='__main__':unittest.main()
