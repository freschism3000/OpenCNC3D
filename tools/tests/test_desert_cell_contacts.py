"""Regression controls for black road margins and bounded source contact fixes."""
from pathlib import Path
import copy,hashlib,importlib.util,json,shutil,tempfile,unittest,sys
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/'art/remaster/textures/unified_daylight_v01'
sys.path.insert(0,str(ROOT/'tools/bakery'))
from road_continuity import updates
sp=importlib.util.spec_from_file_location('crossings_gate',ROOT/'tools/gate-desert-crossings.py');G=importlib.util.module_from_spec(sp);sp.loader.exec_module(G)
class RoadMargins(unittest.TestCase):
 def test_source_road_margins_have_no_opaque_black_canvas(self):
  strips=updates(ROOT,'DESERT','terrainseam');checked=0
  for key,pixels in strips.items():
   a=np.frombuffer(pixels,'u1').reshape(-1,4);opaque=a[:,3]>200
   if opaque.any():self.assertGreater(int(a[opaque,:3].max(-1).min()),8,str(key));checked+=int(opaque.sum())
  self.assertGreater(checked,100000)
 def test_road_contacts_preserve_sand_and_core(self):
  directory=ART/'desert_feature_edges_v03';rows=json.loads((directory/'MANIFEST.json').read_text())['assets'];self.assertEqual(len(rows),43)
  for row in rows:
   old=np.array(Image.open(ROOT/row['original_ground']));new=np.array(Image.open(directory/row['ground_mask']));self.assertTrue(np.all(new>=old));self.assertTrue(np.all(new[old==255]==255));self.assertGreater(((new>0)&(new<255)).sum(),100,row['name']);self.assertEqual(Image.open(directory/row['player_colour_mask']).getextrema(),(0,0))
 def test_only_literal_bank_cut_lines_change(self):
  rows=json.loads((ART/'desert_bank_contacts_v01/MANIFEST.json').read_text())['assets'];self.assertEqual({r['name']for r in rows},{'SH20','SH52'})
  for row in rows:
   old=np.array(Image.open(ROOT/row['original_ground']));new=np.array(Image.open(ART/row['ground_contact_file']));rock=np.array(Image.open(ROOT/row['rock_source']))>127;allowed=np.zeros(old.shape,bool)
   for side,line,width,box in row['regions']:
    x0,y0,x1,y1=box;allowed[y0:y1,x0:x1]=True
   self.assertTrue(np.array_equal(old[~allowed],new[~allowed]));self.assertTrue(np.array_equal(old[~rock],new[~rock]));alpha=np.array(Image.open(ROOT/row['coast_source']));self.assertTrue(np.array_equal(old[alpha<255],new[alpha<255]));self.assertGreater((old!=new).sum(),100);self.assertTrue(np.all(new>=old))

class BridgeHandoff(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.directory=Path(cls.temp.name)
  for p in ART.iterdir():
   if p.is_dir():
    if p.name=='desert_bridge_road_handoff_v12':shutil.copytree(p,cls.directory/p.name)
    else:(cls.directory/p.name).symlink_to(p,target_is_directory=True)
  cls.rows=[r for r in json.loads((ART/'MANIFEST.json').read_text())['assets']if r.get('pre_road_handoff_v12_export')]
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def test_both_native_road_exits_and_all_keepers(self):
  for row in self.rows:
   report,_=G.verify_bridge_road_handoff(self.directory,row,ROOT);self.assertGreater(report['road_handoff']['full_exit_core_pixels'],100)
 def mutation(self,filename,selector,value):
  row=copy.deepcopy(self.rows[0]);p=self.directory/row['export']/filename;old=p.read_bytes();a=np.array(Image.open(p));y,x=selector(row);a[y,x]=value
  try:
   Image.fromarray(a).save(p);row[filename[:-4]+'_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
   with self.assertRaises(AssertionError):G.verify_bridge_road_handoff(self.directory,row,ROOT)
  finally:p.write_bytes(old)
 def test_road_exit_hole_is_rejected(self):
  def pick(r):
   p=self.directory/r['export'];weight=np.array(Image.open(p/'road_handoff_weight.png'));ground=np.array(Image.open(p/'ground_mask.png'));return tuple(np.argwhere((weight==255)&(ground==0))[0])
  self.mutation('ground_mask.png',pick,255)
 def test_paint_on_water_is_rejected(self):self.mutation('ground_mask.png',lambda r:(336,192),255)
 def test_changed_masonry_is_rejected(self):
  def pick(r):return tuple(np.argwhere(np.array(Image.open(self.directory/r['export']/'masonry_semantic_mask.png'))>0)[0])
  self.mutation('albedo.png',pick,[255,0,255])
if __name__=='__main__':unittest.main()
