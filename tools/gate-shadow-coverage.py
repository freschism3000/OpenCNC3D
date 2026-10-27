#!/usr/bin/env python3
"""Compile the production sun-matrix functions and check receiver coverage.

The negative control is the pre-fix fx_post.h: close views with saved offsets
must fail this gate. This checks the real matrix, including its view transform,
normal offset and filter margin; it does not mirror the radius implementation.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile

def function(source,name):
 start=source.index('static ',source.index(name+'(')-40)
 start=source.rfind('\n',0,start)+1
 opening=source.index('{',source.index(name+'(',start));depth=1;end=opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 post=(a.source/'game/fx_post.h').read_text();gl=(a.source/'game/fx_gl.h').read_text();a.output.mkdir(parents=True,exist_ok=True)
 code='''#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
struct Fx {float sun_az,sun_el,shadow_ox,shadow_oz,shadow_span,shadow_res,shadow_soft,shadow_noff,shadow_pen;}g_fx;
float g_fxViewX0,g_fxViewX1,g_fxViewZ0,g_fxViewZ1;
float g_fxSunDir[3],g_fxSunSpan,g_fxSunR,g_fxSunEye[3],g_fxSunS[3],g_fxSunU[3],g_fxSunNear,g_fxSunFar;
'''+function(gl,'fx_mat_mul')+'\n'+function(post,'fx_sun_dir')+'\n'+function(post,'fx_sun_matrix')+'''
int main(){
 long cases=0,fail=0;float worst=0;
 for(float width:{4.f,8.f,32.f,128.f})for(float az:{0.f,45.f,67.5f,135.f,270.f})for(float el:{8.f,27.28572f,60.f,89.f})
 for(float ox:{-32.f,0.f,11.57143f,32.f})for(float oz:{-32.f,0.f,7.71428f,32.f})for(float span:{.6f,1.f,1.45714f,3.f}){
  g_fx={az,el,ox,oz,span,4096.f,1.35714f,1.5f,.08f};
  g_fxViewX0=40-width/2;g_fxViewX1=40+width/2;g_fxViewZ0=50-width/2;g_fxViewZ1=50+width/2;
  float matrix[16];fx_sun_matrix(matrix);cases++;
  for(float x:{g_fxViewX0,g_fxViewX1})for(float z:{g_fxViewZ0,g_fxViewZ1}){
   float p[3];for(int j=0;j<3;j++)p[j]=matrix[j]*x+matrix[8+j]*z+matrix[12+j];
   // A conservative complete Poisson disc, plus the normal offset and validity edge.
   const float kernel=2*(std::max(g_fx.shadow_soft,g_fx.shadow_pen/(2*g_fxSunR/4096))+g_fx.shadow_noff)/4096+.002f;
   const float limit=std::max(std::fabs(p[0]),std::fabs(p[1]))+kernel;worst=std::max(worst,limit);
   if(limit>=1 || std::fabs(p[2])>=1)fail++;
  }
 }
 printf("{\\"cases\\":%ld,\\"failed_corners\\":%ld,\\"worst_xy_with_kernel\\":%.8f}\\n",cases,fail,worst);return fail?1:0;
}
'''
 cpp=a.output/'coverage.cpp';exe=a.output/'coverage';cpp.write_text(code);subprocess.run(['clang++','-std=c++14',str(cpp),'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);result=json.loads(r.stdout);result.update(passed=r.returncode==0,source_sha256=hashlib.sha256(post.encode()).hexdigest(),scope='Production sun-matrix projection of ground receiver corners at multiple zooms, bearings, elevations, offsets and spans; full filter and normal-offset margin.')
 (a.output/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(r.returncode)
if __name__=='__main__':main()
