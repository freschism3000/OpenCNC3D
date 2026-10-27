"""Production cache may coarsen only unchanged native geometry and scope."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class ShoreCoarsening(unittest.TestCase):
 def test_exact_surface_scope_and_dynamic_weights(self):
  code=r'''
#include <cmath>
#include <cstring>
unsigned g_groundGen=1,g_padEpoch=1,g_generation=1,g_water=1;int g_gridW=2,g_gridH=2,profile=0,contact_queries=0;float g_terrainBase=0;
struct {char scen[16]="fixture";}g_pack;
unsigned terrain_surface_generation(){return g_generation;}unsigned water_plane_epoch(){return g_water;}bool ra3d_flat_ground(){return false;}
float terrain_surface_height(float x,float z){return profile==1&&x==.5f&&z==.5f?.1f:0;}
float terrain_y_base(float x,float z){if(profile==3)return x*x+z;return x+2*z+fmaxf(0,x+z-1)*.4f;}
float water_plane_height(float x,float z,float fallback){++contact_queries;return fallback-(profile==4&&x==.5f&&z==.5f?.1f:0);}
float water_swash_coast_at(float x,float z){return profile==2&&x==.5f&&z==.5f?0:1;}
#include "HEADER"
int main(){float constant[4]={1,1,1,1},affine[4]={0,.25f,.5f,.75f},curved[4]={1,1,1,.5f};assert(water_shore_linear_weights(constant));assert(water_shore_linear_weights(affine));assert(!water_shore_linear_weights(curved));water_shore_cache_prepare();water_shore_node(0,0,0,0);assert(g_waterShoreLastCell->coarse);assert(contact_queries==81);water_shore_node(0,0,4,4);assert(contact_queries==81);
// Relief, a sub-cell river mouth and an unrepresented interior height each need
// all81 supports. Rebuilding the relevant generation must invalidate the flag.
for(int mode=1;mode<=4;mode++){profile=mode;++g_generation;++g_water;water_shore_cache_prepare();water_shore_node(0,0,0,0);assert(!g_waterShoreLastCell->coarse);}profile=0;int before=contact_queries;++g_water;water_shore_cache_prepare();water_shore_node(0,0,0,0);assert(g_waterShoreLastCell->coarse);assert(contact_queries==before+81);}
'''.replace('HEADER',str(ROOT/'game/water_shore_cache.h'))
  with tempfile.TemporaryDirectory()as t:
   p=Path(t);(p/'test.cpp').write_text(code);subprocess.run(['clang++','-std=c++14',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
