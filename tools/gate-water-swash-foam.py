#!/usr/bin/env python3
"""Check the production foam lifecycle, independently of positive ocean travel.

Compiles the actual GLSL age/width/life function to float C++; material sampling
contracts reject forward-only lip transport and a sliding deposited layer.
Runtime colour/contact captures remain separate from these source checks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ADAPTER = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
struct vec4 {float x,y,z,w;vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
float clamp(float a,float b,float c){return std::max(b,std::min(a,c));}
float mix(float a,float b,float t){return a+(b-a)*t;}
float smoothstep(float a,float b,float x){x=clamp((x-a)/(b-a),0,1);return x*x*(3-2*x);}
'''
MAIN = r'''
int main(){int failures=0;unsigned observations=0;
auto check=[&](bool ok,const char*why){if(!ok){if(failures<8)printf("FAIL %s\n",why);++failures;}};
vec4 onset=coastFoamLife(0),approach=coastFoamLife(.10f),peak=coastFoamLife(.28f),early=coastFoamLife(.40f),middle=coastFoamLife(.60f),late=coastFoamLife(.90f),end=coastFoamLife(1);
check(onset.z==0&&onset.w==0&&end.z==0&&end.w==0,"invisible reset at next shore-first approach");
check(peak.y>approach.y*2&&peak.z>.99f&&peak.w==0,"rim builds before reversal; residue begins only after arrival");
check(early.y>peak.y*1.4f&&middle.y>early.y,"lip enlarges through early backwash");
check(early.w>.8f&&middle.w<early.w&&late.w<.02f,"deposited foam persists then fades");
check(late.z<.1f&&late.z<middle.z,"retreating lip drains gradually");
for(int i=0;i<=10000;i++){float p=i/10000.f;vec4 f=coastFoamLife(p);observations++;
check(f.x>=0&&f.x<=1&&f.y>.0f&&f.y<.2f&&f.z>=0&&f.z<=1&&f.w>=0&&f.w<=1,"bounded material lifecycle");
if(p<.28f)check(f.w==0,"no residue ahead of incoming water");
if(p>.32f&&i<10000){vec4 n=coastFoamLife(p+.0001f);check(n.y+1e-6f>=f.y&&n.w<=f.w+1e-6f,"spreading residue fades monotonically after Hold");}
vec4 lo=coastFoamLife(p-1e-5f),hi=coastFoamLife(p+1e-5f);
check(std::abs(hi.y-lo.y)<.001f&&std::abs(hi.z-lo.z)<.002f&&std::abs(hi.w-lo.w)<.002f,"no material pop at reversal or fade");
}
printf("{\"passed\":%s,\"observations\":%u,\"failed_assertions\":%d,\"approach_width\":%.6f,\"peak_width\":%.6f,\"backwash_width\":%.6f,\"early_deposit_alpha\":%.6f,\"late_deposit_alpha\":%.6f}\n",failures?"false":"true",observations,failures,approach.y,peak.y,middle.y,early.w,late.w);return failures?1:0;}
'''

def check_source(raw):
    shore=raw[raw.index('static const char* SHORE_FS ='):]
    foam=shore[shore.index('vec4 foamLife=coastFoamLife(phase);'):shore.index('// The plane supplies')]
    for token in ['vec2(vWorld.x,-sd+front)*1.35+jitter',
                  'vec2(vWorld.z,-sd+front)*1.35+jitter',
                  'vec2(vWorld.x,-sd+peak)*1.35+jitter',
                  'vec2(vWorld.z,-sd+peak)*1.35+jitter',
                  'coastFoamCells(depositX,depositZ,coastWeight,foamLife.x)*foamLife.w',
                  'crest*0.72+trailing*0.42','1.0-(1.0-lip)*(1.0-residue)']:
        if token not in foam:raise ValueError('Missing reversible lip / anchored deposit material: '+token)
    if 'wave.z' in foam.replace('// The lip\'s texture runs up AND back with the front, independent of wave.z.','') or 'uTime' in foam:
        raise ValueError('Ocean travel must not scroll the foam material')
    for token in ['smoothstep(-0.02,0.04,sd)','uSwashOpacity,0.0,1.0','foam*=projectedCoverage']:
        if token not in shore:raise ValueError('Lost shore-only contact or opacity contract: '+token)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    raw=a.source.read_text();check_source(raw)
    block=raw.split('#define WATER_SWASH_FOAM_GLSL',1)[1].split('\n\n',1)[0]
    parts=[json.loads(m[1])for line in block.splitlines()if(m:=re.match(r'\s*("(?:[^"\\]|\\.)*")',line))]
    glsl=''.join(parts);glsl=glsl[glsl.index('vec4 coastFoamLife'):glsl.index('// Dense bubble')]
    (a.output/'extracted-foam-life.glsl').write_text(glsl)
    src=a.output/'foam-life.cpp';src.write_text(ADAPTER+re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',glsl)+MAIN)
    exe=a.output/'foam-life';subprocess.run(['clang++','-std=c++14','-O2',str(src),'-o',str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True);(a.output/'run.log').write_text(r.stdout+r.stderr)
    result=json.loads(r.stdout.splitlines()[-1]);result.update(source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),scope='Source-extracted float lifecycle and sampling contracts; runtime colour/contact verified separately')
    (a.output/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return r.returncode
if __name__=='__main__':raise SystemExit(main())
