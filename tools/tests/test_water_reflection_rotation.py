"""Exercise the production F5 schema and the water-only reflection direction."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = r'''
#include "fx_state.h"
#include "fx_panel_policy.h"
#include "water_reflection.h"
#include <cassert>
#include <cmath>
#include <cstring>
int main(int argc,char**argv) {
 assert(argc==3);
 const float inputs[][3]={{0.f,-.6f,.8f},{-0.f,-0.f,1.f},{.28f,-.42f,-.86f}};
 for(const auto& input:inputs) {
  float original[3],before[3],actual[3];memcpy(before,input,sizeof(before));
  for(int i=0;i<3;i++)original[i]=-input[i];
  water_reflection_direction(input,0.f,actual);
  assert(!memcmp(original,actual,sizeof(actual)));
  assert(!memcmp(before,input,sizeof(before)));
 }
 float out[3];water_reflection_direction(inputs[0],90.f,out);
 assert(fabsf(out[0]-.8f)<1e-6f && out[1]==.6f && fabsf(out[2])<1e-6f);
 water_reflection_direction(inputs[0],-90.f,out);
 assert(fabsf(out[0]+.8f)<1e-6f && out[1]==.6f && fabsf(out[2])<1e-6f);
 water_reflection_direction(inputs[0],180.f,out);
 assert(fabsf(out[0])<1e-6f && out[1]==.6f && fabsf(out[2]-.8f)<1e-6f);
 for(int yaw=-180;yaw<=180;yaw+=5) {
  water_reflection_direction(inputs[0],(float)yaw,out);
  assert(out[1]==.6f && fabsf(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]-1.f)<1e-6f);
 }
 for(int ra=0;ra<2;ra++) {
  g_fxRedAlert=ra;FxState s;fx_defaults(&s);assert(s.water_reflection_yaw==90.f);
  assert(fx_load(&s,argv[1]));assert(s.water_reflection_yaw==90.f);
  assert(s.sun_az==37.f && s.sun_el==28.f && s.water_spec==1.25f);
  int found=0;const FxParam* group=nullptr;const FxParam* control=nullptr;
  for(const FxParam*p=FX_PARAMS;p->kind!=FXP_END;p++) {
   if(p->kind==FXP_GROUP)group=p;
   if(p->key && !strcmp(p->key,"water_reflection_yaw")) {
    assert(p->kind==FXP_FLOAT && p->lo==-180.f && p->hi==180.f);
    assert(group && strstr(group->label,"WATER") && fxp_group_tab(group)==1);
    assert(fxp_row_visible(p,group,&s) && fxp_group_visible(group,&s));
    assert(!strcmp(p->label,"Reflection rotation") && strlen(p->label)*6<=160);
    assert(fx_quant(p,-250.f)==-180.f && fx_quant(p,250.f)==180.f);
    found++;control=p;
   }
  }
  assert(found==1 && control);
  const float shallows=s.water_shallow,fade=s.water_fade;
  *fx_fptr(&s,control)=73.25f;
  assert(s.sun_az==37.f && s.sun_el==28.f && s.water_shallow==shallows && s.water_fade==fade);
  assert(fx_save(&s,argv[2]));fx_defaults(&s);assert(fx_load(&s,argv[2]));
  assert(s.water_reflection_yaw==73.25f && s.sun_az==37.f && s.sun_el==28.f);
 }
 puts("WATER-REFLECTION|PASS|TD/RA schema, visibility, legacy inheritance, roundtrip and exact neutral direction");
}
'''


class WaterReflectionRotation(unittest.TestCase):
    def test_compiled_state_and_direction(self):
        with tempfile.TemporaryDirectory(prefix="cnc3d-water-reflection-") as tmp:
            out = Path(tmp)
            (out / "test.cpp").write_text(SOURCE)
            (out / "legacy.cfg").write_text("sun_az 37\nsun_el 28\nwater_spec 1.25\n")
            build = subprocess.run(
                ["clang++", "-std=c++14", "-I", str(ROOT / "game"),
                 str(out / "test.cpp"), "-o", str(out / "test")],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(out / "test"), str(out / "legacy.cfg"),
                                  str(out / "saved.cfg")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_production_shader_ownership(self):
        water = (ROOT / "game/water_mod.h").read_text()
        for snippet in (
            'fx_set3f(prog, "uSunDir", -g_fxSunDir[0], -g_fxSunDir[1], -g_fxSunDir[2]);',
            "water_reflection_direction(g_fxSunDir, g_fx.water_reflection_yaw, reflectionLight);",
            "vec3 L = normalize(uSunDir);",
            "vec3 H = normalize(normalize(uReflectionDir) + V);",
            "dot(N, normalize(vec3(L.x, max(L.y, 0.25), L.z)))",
        ):
            self.assertTrue(snippet in water, snippet)
        self.assertEqual(water.count("uReflectionDir"), 3)
        shore = water.split("static const char* SHORE_FS =", 1)[1].split("static GLuint", 1)[0]
        self.assertNotIn("uReflectionDir", shore)
        self.assertEqual(hashlib.sha256((ROOT / "game/water_virtual_bed.h").read_bytes()).hexdigest(),
                         "b2e41d2af9e0189d299a8dd79ffef7b92b0a936fef18a9beaf22c939cd6ed768")


if __name__ == "__main__":
    unittest.main()
