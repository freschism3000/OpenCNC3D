"""Saved Desert contact mattes and road-exit conservation checks."""
from pathlib import Path
import hashlib,json,sys,unittest
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/bakery'))
from desert_feature_edges import ground_updates
from road_continuity import updates
class DesertEdges(unittest.TestCase):
 def test_rock_faces_and_ground_are_conserved(self):
  source=ROOT/'art/remaster/textures/unified_daylight_v01/desert_feature_edges_v01';m=json.loads((source/'MANIFEST.json').read_text())
  self.assertEqual({r['name']for r in m['assets']},{'P01','P03'})
  for r in m['assets']:
   ground=np.array(Image.open(source/r['ground_mask']));keep=np.array(Image.open(source/r['object_keep_mask']))>0;old=np.array(Image.open(ROOT/r['original_ground']))<128
   self.assertTrue(np.all(ground[keep]==0));self.assertTrue(np.all(ground[~old]==255));self.assertGreater(((ground>0)&(ground<255)).sum(),1000)
   self.assertEqual(hashlib.sha256((ROOT/r['source']).read_bytes()).hexdigest(),r['source_sha256'])
  self.assertTrue({(67,0,0,0),(69,0,0,0),(121,0,0,0),(121,1,0,0),(121,2,0,0)}<=set(ground_updates(ROOT)))
 def test_road_interiors_and_native_coverage_are_conserved(self):
  source=ROOT/'art/remaster/textures/unified_daylight_v01/desert_road_exits_v02';m=json.loads((source/'MANIFEST.json').read_text());self.assertEqual(len(m['assets']),43)
  for r in m['assets']:
   old=np.array(Image.open(ROOT/r['source']).convert('RGB'));new=np.array(Image.open(source/r['name']/'albedo.png'));weight=np.array(Image.open(source/r['name']/'exit_mask.png'));road=np.array(Image.open(source/r['name']/'road_mask.png'))>0
   self.assertTrue(np.array_equal(old[weight==0],new[weight==0]),r['name']);self.assertTrue(np.array_equal(road,np.array(Image.open(ROOT/r['ground_mask']))<128),r['name'])
 def test_nod1_d29_contacts_leave_road_exact(self):
  source=ROOT/'art/remaster/textures/unified_daylight_v01/desert_feature_edges_v02';r=json.loads((source/'MANIFEST.json').read_text())['assets'][0]
  old=np.array(Image.open(ROOT/r['original_ground']));new=np.array(Image.open(source/r['ground_mask']));region=np.array(Image.open(source/r['repair_region_mask']))>0;keep=np.array(Image.open(source/r['object_keep_mask']))>0
  self.assertTrue(np.array_equal(old[~region],new[~region]));self.assertTrue(np.all(new[keep]==0));self.assertGreater(((new>0)&(new<255)).sum(),800);self.assertEqual(r['tid'],121)
 def test_road_exit_repair_preserves_detached_rocks(self):
  from scipy.ndimage import label
  source=ROOT/'art/remaster/textures/unified_daylight_v01/desert_road_exits_v02';m=json.loads((source/'MANIFEST.json').read_text())
  for r in m['assets']:
   old=np.array(Image.open(ROOT/r['source']).convert('RGB'));new=np.array(Image.open(source/r['name']/'albedo.png'));mask=np.array(Image.open(source/r['name']/'road_mask.png'))>0;comp,n=label(mask,np.ones((3,3)));counts=np.bincount(comp.ravel());counts[0]=0;decor=(comp!=counts.argmax())&mask
   self.assertTrue(np.array_equal(old[decor],new[decor]),r['name'])
 def test_all_road_exit_overlap_texels_agree(self):
  # Physical neighbour edges with overlapping native road coverage share the
  # same texels, independent of which two templates are placed beside each other.
  source=ROOT/'art/remaster/textures/unified_daylight_v01/desert_road_exits_v02';m=json.loads((source/'MANIFEST.json').read_text());edges=[[],[]]
  for r in m['assets']:
   w,h=r['cells'];have=set(r['have']);a=np.array(Image.open(source/r['name']/'albedo.png'));mask=np.array(Image.open(source/r['name']/'exit_mask.png'))==255
   for icon in have:
    y,x=divmod(icon,w);cell=a[y*144:(y+1)*144,x*144:(x+1)*144];cov=mask[y*144:(y+1)*144,x*144:(x+1)*144]
    for dx,dy in [(-1,0),(1,0),(0,-1),(0,1)]:
     nx,ny=x+dx,y+dy
     if 0<=nx<w and 0<=ny<h and ny*w+nx in have:continue
     rgb=cell[:,0 if dx<0 else -1]if dx else cell[0 if dy<0 else -1];c=cov[:,0 if dx<0 else -1]if dx else cov[0 if dy<0 else -1];edges[0 if dx else 1].append((rgb,c))
  checked=0
  for axis in edges:
   for a,ca in axis:
    for b,cb in axis:
     keep=ca&cb
     if keep.any():self.assertTrue(np.array_equal(a[keep],b[keep]));checked+=int(keep.sum())
  self.assertGreater(checked,10000)
if __name__=='__main__':unittest.main()
