#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,re
import argparse
p=argparse.ArgumentParser(description='Check actual building overlay alpha math against the original calculation.')
p.add_argument('--repo',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
b=a.out.resolve();b.mkdir(parents=True,exist_ok=True);private=a.repo.resolve()/'game'
def func(s,n):
 a=s.index('static unsigned char '+n+'(');c=s.index('{',a);i=c+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
orig='static unsigned char reference(float x, float z, int zone)\n{\n    const float ang = atan2f(-z, x) * (180.0f / EBLD_PI);\n    float d = ang - 45.0f * (float)zone;\n    while (d > 180.0f)  d -= 360.0f;\n    while (d < -180.0f) d += 360.0f;\n    float w = 1.0f - fabsf(d) / 45.0f;\n    if (w < 0.0f) w = 0.0f;\n    return (unsigned char)(255.0f * w + 0.5f);\n}'
s=(private/'ebld_mod.h').read_text();assert 'static const float EBLD_PI = 3.14159265f;' in s;actual=func(s,'ebld_zone_alpha_angle')+'\n'+func(s,'ebld_zone_alpha')
code='''#include <cmath>\n#include <cstdio>\n#include <cstring>\n#include <cstdint>\n#define EBLD_PI 3.14159265358979323846f\n'''+orig+'\n'+actual+r'''
int main(){unsigned long checks=0;uint32_t state=982451653;auto next=[&](){state^=state<<13;state^=state>>17;state^=state<<5;return state;};
 auto check=[&](float x,float z){float ang=atan2f(-z,x)*(180.0f/EBLD_PI);for(int zone=0;zone<8;zone++){unsigned char a=reference(x,z,zone),b=ebld_zone_alpha(x,z,zone),c=ebld_zone_alpha_angle(ang,zone);if(a!=b||a!=c){printf("FAIL %g %g %d %d %d %d\n",x,z,zone,a,b,c);return false;}checks++;}return true;};
 const float values[]={0.0f,-0.0f,1.0f,-1.0f,1024.0f,-1024.0f,1e-35f,-1e-35f,1e35f,-1e35f};
 for(float x:values)for(float z:values)if(!check(x,z))return 1;
 for(int k=0;k<1000000;k++){uint32_t bx=next(),bz=next();bx=(bx&0x807fffff)|((bx>>1)%254+1)<<23;bz=(bz&0x807fffff)|((bz>>1)%254+1)<<23;float x,z;memcpy(&x,&bx,4);memcpy(&z,&bz,4);if(!check(x,z))return 1;}
 printf("CUTMATH|checks=%lu|failures=0\n",checks);return 0;}
'''
source=b/'cut-math.cpp';source.write_text(code);exe=b/'cut-math';subprocess.run(['clang++','-std=c++14','-O2',str(source),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,check=True);print(r.stdout,end='');m=re.search(r'checks=(\d+)',r.stdout);(b/'cut-math-proof.json').write_text(json.dumps({'passed':True,'actual_functions':True,'reference':'original overlay alpha calculation','checks':int(m[1]),'negative_zero_and_finite_random':True},indent=2)+'\n')
