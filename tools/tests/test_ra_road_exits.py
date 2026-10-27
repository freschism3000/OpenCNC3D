"""Native road topology permits real contacts and refuses empty shoulders."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class RoadExits(unittest.TestCase):
 def test_source_contact_and_symmetric_validation(self):
  code='''#include <cassert>
#include "HEADER"
int main(){
 assert(ra_road_exits_connect(39,0,3,35,2));
 for(int a=1;a<=45;a++)for(int ai=0;ai<16;ai++)for(int b=1;b<=45;b++)for(int bi=0;bi<16;bi++)for(int side=0;side<4;side++)
  assert(ra_road_exits_connect(a,ai,side,b,bi)==ra_road_exits_connect(b,bi,side^1,a,ai));
 assert(!ra_road_exits_connect(0,0,0,35,2));assert(!ra_road_exits_connect(39,16,3,35,2));assert(!ra_road_exits_connect(39,0,4,35,2));
}'''.replace('HEADER',str(ROOT/'game/ra3d_road_exits.h'))
  with tempfile.TemporaryDirectory()as t:
   p=Path(t);(p/'check.cpp').write_text(code);subprocess.run(['clang++','-std=c++14',str(p/'check.cpp'),'-o',str(p/'check')],check=True);subprocess.run([str(p/'check')],check=True)
if __name__=='__main__':unittest.main()
