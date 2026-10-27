"""Effects-off water keeps one periodic material and immutable land/alpha."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class StaticWater(unittest.TestCase):
 def test_periodic_edges_and_immutable_opaque_land(self):
  code=r'''
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>
#include "HEADER"
int main(){for(int n:{24,144}){
 std::vector<unsigned char> source(288*288*3);for(size_t i=0;i<source.size();i++)source[i]=(unsigned char)((i*37+i/288)%256);
 auto tile=water_static_tile(source,288,n);assert(tile.size()==(size_t)n*n*3);
 for(int y=0;y<n;y++)for(int c=0;c<3;c++)assert(tile[(y*n)*3+c]==tile[(y*n+n-1)*3+c]);
 for(int x=0;x<n;x++)for(int c=0;c<3;c++)assert(tile[x*3+c]==tile[((n-1)*n+x)*3+c]);
 std::vector<unsigned char> rgba(n*n*4),coverage(n*n);for(size_t i=0;i<rgba.size();i++)rgba[i]=(unsigned char)(i*13);auto before=rgba;
 for(size_t i=0;i<coverage.size();i++)coverage[i]=(i%3==0?255:i%3==1?0:128);
 water_static_composite(rgba.data(),n,n*4,tile,coverage.data(),n,1);
 for(size_t i=0;i<coverage.size();i++){
  assert(rgba[i*4+3]==before[i*4+3]);
  for(int c=0;c<3;c++){
   if(coverage[i]==255)assert(rgba[i*4+c]==before[i*4+c]);
   if(coverage[i]==0)assert(rgba[i*4+c]==tile[i*3+c]);
   if(coverage[i]==128)assert(rgba[i*4+c]==(before[i*4+c]*128+tile[i*3+c]*127+127)/255);
  }
 }
}assert(water_static_tile({},288,144).empty());}
'''.replace('HEADER',str(ROOT/'game/water_static_tile.h'))
  with tempfile.TemporaryDirectory()as t:
   p=Path(t);(p/'check.cpp').write_text(code);subprocess.run(['clang++','-std=c++14',str(p/'check.cpp'),'-o',str(p/'check')],check=True);subprocess.run([str(p/'check')],check=True)
if __name__=='__main__':unittest.main()
