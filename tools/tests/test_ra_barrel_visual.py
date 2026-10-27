"""Verify RA barrel presentation size and native base independently of a GL window."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RaBarrelVisual(unittest.TestCase):
    def test_actual_matrix_preserves_anchor_and_composes_once(self):
        code = r'''
#include <cassert>
#include <cmath>
#include "ra_barrel_visual.h"
static void point(const float* m,const float* v,float* p) {
    for(int r=0;r<3;++r) p[r]=m[r*4]*v[0]+m[r*4+1]*v[1]+m[r*4+2]*v[2]+m[r*4+3];
}
int main() {
    float out[12];
    for(const char* name:{"ebld:BARL:intact","ebld:BRL3:cut","ebld:BARL:cutdmg",
                          "ebld:BRL3:over7","ebld:BARL:wreck","ebld:BRL3:glowx"}) {
        assert(ra_barrel_visual_matrix(true,true,false,name,false,false,nullptr,out));
        assert(out[0]==.6f&&out[5]==.6f&&out[10]==.6f);
        const float foot[]={0,RA_BARREL_VISUAL_BASE_Y,0},top[]={0,368.6400146484375f,0};
        float pf[3],pt[3];point(out,foot,pf);point(out,top,pt);
        assert(fabsf(pf[1]-foot[1])<1e-6f);
        assert(fabsf((pt[1]-pf[1])/(top[1]-foot[1])-.6f)<1e-6f);
        assert(pf[0]==0&&pf[2]==0); // The engine's horizontal anchor never moves.
        float saved[12];for(int i=0;i<12;++i)saved[i]=out[i];
        assert(!ra_barrel_visual_matrix(true,true,false,name,false,true,saved,out));
        for(int i=0;i<12;++i)assert(out[i]==saved[i]); // A recursive draw never scales twice.
    }
    // Animated/moving pieces already have a 3x4 matrix. The visual correction wraps
    // its resolved local point instead of replacing its pose or modifying simulation.
    const float pose[]={0,-1,0,24, 1,0,0,40, 0,0,1,-12};
    const float vertex[]={17,31,-7};float previous[3],scaled[3];point(pose,vertex,previous);
    assert(ra_barrel_visual_matrix(true,true,false,"ebld:BARL:wreck",false,false,pose,out));
    point(out,vertex,scaled);
    assert(fabsf(scaled[0]-previous[0]*.6f)<1e-5f);
    assert(fabsf(scaled[2]-previous[2]*.6f)<1e-5f);
    assert(fabsf(scaled[1]-(RA_BARREL_VISUAL_BASE_Y+(previous[1]-RA_BARREL_VISUAL_BASE_Y)*.6f))<1e-5f);
    // Legacy plate contact remains y0, while its footprint shrinks to the same width.
    assert(ra_barrel_visual_matrix(true,true,false,"ebld:BARL:intact",true,false,nullptr,out));
    assert(out[7]==0&&out[0]==.6f&&out[10]==.6f);
    // Other games, Classic, unrelated props, source cartridges, and death effects
    // retain the caller's matrix byte-for-byte by declining before writing output.
    for(int ra=0;ra<2;++ra) for(int enhanced=0;enhanced<2;++enhanced)
      for(int classic=0;classic<2;++classic) {
        if(ra&&enhanced&&!classic)continue;
        for(float& v:out)v=123;
        assert(!ra_barrel_visual_matrix(ra,enhanced,classic,"ebld:BARL:intact",false,false,nullptr,out));
        for(float v:out)assert(v==123);
    }
    for(const char* name:{"BARL","ebld:BARL2:intact","ebld:BRL30:intact","ebld:MINV:intact",
                          "ebld:BOXES01:intact","ebld:BARL:scorch","ebld:BRL3:scorch","ebld:BARL:"}) {
        for(float& v:out)v=123;
        assert(!ra_barrel_visual_matrix(true,true,false,name,false,false,nullptr,out));
        for(float v:out)assert(v==123);
    }
}
'''.replace('#include <cmath>', '#include <cmath>\n#include <initializer_list>')
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory)
            (folder/'test.cpp').write_text(code)
            subprocess.run(['clang++','-std=c++14','-I',str(ROOT/'game'),
                            str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
            subprocess.run([str(folder/'test')],check=True)

    def test_committed_models_use_the_measured_base_in_every_stage(self):
        # The existing project reader validates the actual committed model format.
        # A later asset revision must consciously update the presentation anchor if
        # it moves the native base; silently retaining an obsolete base would hover.
        sys.path.insert(0,str(ROOT/'tools/art/remaster'))
        import bake_ebld
        header=(ROOT/'game/ra_barrel_visual.h').read_text()
        base=float(re.search(r'RA_BARREL_VISUAL_BASE_Y = ([\d.-]+)f',header).group(1))
        for typ in ('BARL','BRL3'):
            model=bake_ebld.check(ROOT/'game/ebld_ra_props'/f'{typ}.ebld',quiet=True)
            self.assertEqual(len(model['sets']),3)
            for stage in model['sets']:
                points=stage.tris['v']['p'].reshape(-1,3)
                self.assertAlmostEqual(float(points[:,1].min()),base,places=5)
                old_extent=points.max(0)-points.min(0)
                new=points*.6
                new[:,1]+=(1-.6)*base
                for ratio in (new.max(0)-new.min(0))/old_extent:
                    self.assertAlmostEqual(float(ratio),.6,places=6)
                self.assertAlmostEqual(float(new[:,1].min()),base,places=5)


if __name__=='__main__':
    unittest.main()
