"""Compile the actual F5 state schema and exercise old/new preset roundtrips."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SOURCE=r'''
#include "fx_state.h"
#include <cassert>
#include <cmath>
int main(int argc,char**argv){
 assert(argc==3);
 for(int ra=0;ra<2;ra++){
  g_fxRedAlert=ra;FxState s;fx_defaults(&s);
  assert(std::abs(s.water_align_length-0.f)<1e-6f);
  assert(std::abs(s.water_align_blend-3.31518f)<1e-6f);
  assert(s.water_swash_hold==0.f && std::abs(s.water_swash_opacity-.48214f)<1e-6f);
  assert(fx_load(&s,argv[1]));
  assert(std::abs(s.water_align_length-0.f)<1e-6f && std::abs(s.water_align_blend-3.31518f)<1e-6f);
  assert(s.water_swash_hold==0.f && std::abs(s.water_swash_opacity-.48214f)<1e-6f);
  int found=0;
  for(const FxParam*p=FX_PARAMS;p->kind!=FXP_END;p++){
   if(!strcmp(p->key,"water_align_length")||!strcmp(p->key,"water_align_blend")){
    assert(p->kind==FXP_FLOAT && strlen(p->label)*6<=160);
    assert(p->hi==8.f);found++;
   }
   if(!strcmp(p->key,"water_swash_hold")||!strcmp(p->key,"water_swash_opacity")){
    assert(p->kind==FXP_FLOAT && strlen(p->label)*6<=160 && p->lo==0.f);
    assert(p->hi==(!strcmp(p->key,"water_swash_hold")?10.f:1.f));found++;
   }
  }
  assert(found==4);
  s.water_align_length=3.4f;s.water_align_blend=4.6f;
  s.water_swash_hold=3.75f;s.water_swash_opacity=.37f;
  assert(fx_save(&s,argv[2]));fx_defaults(&s);assert(fx_load(&s,argv[2]));
  assert(std::abs(s.water_align_length-3.4f)<1e-5f && std::abs(s.water_align_blend-4.6f)<1e-5f);
  assert(std::abs(s.water_swash_hold-3.75f)<1e-5f && std::abs(s.water_swash_opacity-.37f)<1e-5f);
 }
 puts("ALIGNMENT|PASS|TD/RA defaults, old preset inheritance, hold/opacity schema/label fit and save/load");
}
'''

class AlignmentControls(unittest.TestCase):
    def test_compiled_state(self):
        with tempfile.TemporaryDirectory(prefix='cnc3d-alignment-state-') as td:
            d=Path(td);src=d/'test.cpp';src.write_text(SOURCE)
            exe=d/'test'
            subprocess.run(['clang++','-std=c++14','-I',str(ROOT/'game'),str(src),'-o',str(exe)],check=True,capture_output=True)
            run=subprocess.run([str(exe),str(ROOT/'docs/tuning/opencnc3d-fx-2026-10-21-1712.cfg'),str(d/'saved.cfg')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

if __name__=='__main__':unittest.main()
