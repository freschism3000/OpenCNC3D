"""Exercise the production contact-height builder on nonflat native banks."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class NativeContact(unittest.TestCase):
 def test_native_heights_shared_edges_and_no_floor_mesh(self):
  raw=(ROOT/'game/water_plane.h').read_text();a=raw.index('static int water_plane_lay_heights');b=raw.index('\n/* Build from precisely',a);fn=raw[a:b]
  code='''#include <vector>
#include <cassert>
#include <cmath>
const int g_gridW=2,g_gridH=2;const float WATER_PLANE_BED_GAP=.04f;
struct WaterPlaneState{std::vector<float>surf,floor,level;std::vector<int>shoreBody;};WaterPlaneState g_waterPlane;
float terrain_corner_y(int x,int z){return x*.3f+z*.2f-.9f;}
'''+fn+'''
int main(){g_waterPlane.level={-4};g_waterPlane.shoreBody={0,0,0,-1};water_plane_lay_heights();for(int z=0;z<2;z++)for(int x=0;x<2;x++)for(int dz=0;dz<2;dz++)for(int dx=0;dx<2;dx++){int cell=z*2+x;if(g_waterPlane.shoreBody[cell]<0)continue;assert(fabs(g_waterPlane.surf[cell*4+dx+2*dz]-terrain_corner_y(x+dx,z+dz))<1e-7);assert(fabs(g_waterPlane.floor[(z+dz)*3+x+dx]-(terrain_corner_y(x+dx,z+dz)-.04f))<1e-7);}assert(g_waterPlane.surf[1]==g_waterPlane.surf[4]);assert(g_waterPlane.surf[2]==g_waterPlane.surf[8]);}
'''
  with tempfile.TemporaryDirectory() as t:
   q=Path(t);(q/'test.cpp').write_text(code);subprocess.run(['clang++','-std=c++14',str(q/'test.cpp'),'-o',str(q/'test')],check=True);subprocess.run([str(q/'test')],check=True)
  bed=(ROOT/'game/water_bed.h').read_text();self.assertNotIn('g_waterBedNodes',bed);self.assertNotIn('WATER_BANK_STEPS',bed)

# Reuse the camera/field fixture used to calibrate the older geometry gate, but
# replace its generated bank lattice with the new empty-bed contract.
import importlib.util,struct
sp=importlib.util.spec_from_file_location('contact_gate',ROOT/'tools/gate-water-native-contact.py');G=importlib.util.module_from_spec(sp);sp.loader.exec_module(G)
sp=importlib.util.spec_from_file_location('bank_fixture',ROOT/'tools/tests/test_submerged_terrain.py');F=importlib.util.module_from_spec(sp);sp.loader.exec_module(F)
class NativeCaptureGate(unittest.TestCase):
 def setUp(self):
  self.f=F.SubmergedTerrain();self.f.setUp();self.f.plane();self.o=self.f.o
  (self.o/'bed.bin').write_bytes(b'C3DBED1\0'+struct.pack('<I',0));(self.o/'run.log').write_text('native_height_max_error=0.000000000|generated_bank_vertices=0\n')
 def tearDown(self):self.f.tearDown()
 def test_actual_surface_without_banks(self):self.assertGreater(G.check(self.o)['gpu_water_pixels'],100)
 def test_missing_surface_rejected(self):
  self.f.normal[8:16,20:26]=0;self.f.buffer('normals',self.f.normal)
  with self.assertRaises(AssertionError):G.check(self.o)
 def test_wrong_depth_rejected(self):
  self.f.depth[8:16,20:26,0]+=.03;self.f.buffer('depth',self.f.depth)
  with self.assertRaises(AssertionError):G.check(self.o)
 def test_regenerated_banks_rejected(self):
  self.f.write_bed()
  with self.assertRaises(AssertionError):G.check(self.o)
 def test_lowered_surface_rejected(self):
  (self.o/'run.log').write_text('native_height_max_error=0.200000000|generated_bank_vertices=0\n')
  with self.assertRaises(AssertionError):G.check(self.o)

if __name__=='__main__':unittest.main()
