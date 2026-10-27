"""Exercise production river/coast permission sampling on wet and dry banks."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class SwashScope(unittest.TestCase):
 def test_production_permission_sampler(self):
  raw=(ROOT/'game/water_mod.h').read_text();start=raw.index('static float water_swash_coast_at(float wx, float wz)\n{');end=raw.index('\n#include \"water_bed_material.h\"',start);fn=raw[start:end]
  harness='''#include <algorithm>
#include <vector>
#include <cmath>
#include <cassert>
static int g_wfW=0,g_wfH=0,g_wfRes=1;
static std::vector<unsigned char> g_wfWet,g_wfSwashRiver;
static std::vector<int> g_wfNearWater;
'''+fn+'''
int main(){assert(water_swash_coast_at(0,0)==0);
g_wfW=4;g_wfH=2;g_wfWet={1,1,0,0,1,1,0,0};g_wfSwashRiver={0,1,0,0,0,1,0,0};g_wfNearWater={0,1,1,0,4,5,5,-1};
assert(water_swash_coast_at(.5,.5)==1); // open sea
assert(water_swash_coast_at(1.5,.5)==0); // river
assert(water_swash_coast_at(2.5,.5)==0); // river dry bank
assert(water_swash_coast_at(3.5,.5)==1); // sea dry bank
assert(water_swash_coast_at(3.5,1.5)==0); // no nearby wet sample
assert(water_swash_coast_at(1,.5)==.5); // continuous mouth transition
for(int x=-100;x<100;x++)for(int y=-100;y<100;y++){float q=water_swash_coast_at(x/10.f,y/10.f);assert(q>=0&&q<=1);}
}'''
  with tempfile.TemporaryDirectory()as d:
   p=Path(d);(p/'test.cpp').write_text(harness);subprocess.run(['clang++','-std=c++14',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
 def test_native_connected_rivers_and_both_render_passes(self):
  s=(ROOT/'game/water_mod.h').read_text();self.assertIn('const int slot=ts_slot_of_cell(c,templateCount);',s)
  self.assertIn('wet[i] && riverCell[(size_t)(y/res)*g_gridW+x/res]',s)
  self.assertEqual(s.count('coast.z*=clamp(vSwashCoast,0.0,1.0)'),2)
  self.assertEqual(s.count('water_swash_coast_at('),4)
  self.assertIn('if(uSwash>0.5 && vSwashCoast<=0.0)discard;',s)
if __name__=='__main__':unittest.main()
