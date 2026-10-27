"""Static cliff surf is removed locally; alpha, dry snow and distant details survive."""
from pathlib import Path
import subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[2]
class CliffWaterline(unittest.TestCase):
 def test_production_whole_template_material(self):
  s=(R/'game/water_mod.h').read_text();edt=s[s.index('static void water_edt_1d'):s.index('/* One separable box blur')]
  c='''#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <cassert>
static const double WATER_EDT_INF=1e20;
'''+edt+'\n'+(R/'game/ra3d_cliff_material.h').read_text()+'''
int main(){const int w=48,h=48;std::vector<unsigned char> rgb(w*h*3),wet(w*h,0),mask;
for(int y=0;y<h;y++)for(int x=0;x<w;x++){int i=y*w+x;rgb[i*3]=95;rgb[i*3+1]=78;rgb[i*3+2]=60;
if(y>=40){wet[i]=1;rgb[i*3]=35;rgb[i*3+1]=85;rgb[i*3+2]=110;}}
for(int x=2;x<w-2;x++){int i=39*w+x;rgb[i*3]=95;rgb[i*3+1]=165;rgb[i*3+2]=192;}
// A separate cool rock feature is eleven texels from native water.
int far=29*w+20;rgb[far*3]=45;rgb[far*3+1]=100;rgb[far*3+2]=160;
int snow=30*w+30;rgb[snow*3]=218;rgb[snow*3+1]=220;rgb[snow*3+2]=223;
std::vector<unsigned char> before(rgb),originalWet(wet);
size_t n=ra3d_cliff_dry_material(rgb,wet,w,h,1,&mask);assert(n>40&&mask[39*w+20]);assert(!mask[far]&&!mask[snow]);
assert(wet==originalWet);for(int i=0;i<w*h;i++)if(!mask[i])for(int c=0;c<3;c++)assert(rgb[i*3+c]==before[i*3+c]);
assert(rgb[(39*w+20)*3+2]-rgb[(39*w+20)*3]<7);
// Matching strip edges cannot put removed blue/white surf back into a join.
for(int side=0;side<2;side++){
 std::vector<unsigned char> strip(6*3*4),edge(6*3,65),m(6,0);m[2]=1;
 for(int i=0;i<18;i++){strip[i*4]=90;strip[i*4+1]=155;strip[i*4+2]=190;strip[i*4+3]=(unsigned char)(i*13);}
 auto was=strip;assert(ra3d_cliff_strip_material(strip.data(),6,3,side,edge,m)==3);
 for(int y=0;y<(side?3:6);y++)for(int x=0;x<(side?6:3);x++){int i=y*(side?6:3)+x,j=side?x:y;assert(strip[i*4+3]==was[i*4+3]);for(int c=0;c<3;c++)assert(strip[i*4+c]==(j==2?65:was[i*4+c]));}
}
// An all-dry cliff must remain exact, including blue mineral/sky details.
std::fill(wet.begin(),wet.end(),0);rgb=before;assert(ra3d_cliff_dry_material(rgb,wet,w,h,1)==0);assert(rgb==before);
}'''
  with tempfile.TemporaryDirectory()as d:
   p=Path(d);(p/'test.cpp').write_text(c);subprocess.run(['clang++','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
