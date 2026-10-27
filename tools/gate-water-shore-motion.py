#!/usr/bin/env python3
"""Compile the shared GLSL cycle and band expressions to verify shore motion.

This is a CPU float-math/source contract, not GPU compilation or visual proof.
The generated C++ and extracted GLSL are retained beside the report.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ADAPTER = r'''
#include <cmath>
#include <cstdio>
#include <algorithm>
struct vec2 { float x,y; vec2(float a=0):x(a),y(a){} vec2(float a,float b):x(a),y(b){} };
struct vec4 { float x,y,z,w;vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
vec2 operator+(vec2 a,vec2 b){return vec2(a.x+b.x,a.y+b.y);}
vec2 operator-(vec2 a,vec2 b){return vec2(a.x-b.x,a.y-b.y);}
vec2 operator*(vec2 a,vec2 b){return vec2(a.x*b.x,a.y*b.y);}
vec2 operator*(vec2 a,float b){return vec2(a.x*b,a.y*b);}
vec2 operator*(float a,vec2 b){return b*a;}
vec2 operator/(vec2 a,float b){return vec2(a.x/b,a.y/b);}
vec2 clamp(vec2 a,float lo,float hi){return vec2(std::max(lo,std::min(a.x,hi)),std::max(lo,std::min(a.y,hi)));}
float dot(vec2 a,vec2 b){return a.x*b.x+a.y*b.y;}
float fract(float a){return a-std::floor(a);}
vec2 fract(vec2 a){return vec2(fract(a.x),fract(a.y));}
vec2 floor(vec2 a){return vec2(std::floor(a.x),std::floor(a.y));}
float clamp(float a,float lo,float hi){return std::max(lo,std::min(a,hi));}
float mix(float a,float b,float t){return a+(b-a)*t;}
using std::max; using std::floor;
float uWaveSpeed,uSwashHold;
float uAlignLength=.35f,uAlignBlend=2.15f;
'''

MAIN = r'''
int failures=0;unsigned observations=0;
float maxDerivativeError=0,maxCycleError=0,maxSpatialTravelDifference=0;
void check(bool ok,const char*why){if(!ok){if(failures<12)printf("FAIL %s\n",why);failures++;}}
int main(){
 const vec2 places[]={vec2(0,0),vec2(10.3f,12.7f),vec2(64,54),vec2(78,64),vec2(37.1f,38.9f),vec2(120,10)};
 for(float base:{0.f,.001f,(.35f+.35f*.25f)/1.92f,2.f})
 for(float rate:{0.f,.175f,.35f,.7f,2.f})for(float hold:{0.f,.3f,2.f,10.f}){
  uWaveSpeed=rate;uSwashHold=hold;
  for(vec2 world:places){
   const float phi=coastClock(world);check(phi>=0&&phi<=.9f,"bounded swash phase field");
   const float basePeriod=rate>0?6.f*.35f/rate:6.f;
   const float period=rate>0?basePeriod+hold:basePeriod;
   const float h=rate>0?hold/basePeriod:0.f,gain=.8f/((.32f+h)/(1+h));
   float previous=-1,previousSurge=-1;
   for(int i=0;i<=1000;i++){
    float phase=i/1000.f,time=rate>0?(3+phase-phi)*period:phase*period;
    vec4 v=coastCycle(world,time,base);observations++;
    check(std::abs(v.w-base*(rate>0?.20f+gain*v.y:1.f))<base*2e-6f+1e-7f,"speed follows shared swash surge without a pause");
    float presence=rate>0 && base>0?v.y:0;
    check(std::abs(coastPresence(v)-presence)<1e-6f,"shore chart fades with the shared swash surge");
    check(std::abs(coastInfluence(.77f,0.f,v)-.77f*presence)<1e-6f,"ocean colour and normals return to their ordinary samples between waves");
    check(std::abs(coastInfluence(.77f,1.f,v)-.77f)<1e-6f,"river steering is retained independently of ocean fade");
    check(std::abs(coastInfluence(.77f,.5f,v)-.77f*(.5f+.5f*presence))<1e-6f,"river mouths blend smoothly");
    check(v.z+1e-6f>=previous,"water travel never reverses");previous=v.z;
    check(v.y>=-1e-5f&&v.y<=1.00001f,"swash bounded");
    if(rate>0){
     const float position=phase*(1+h);
     if(i>0&&position<.279f)check(v.y+1e-5f>=previousSurge,"swash runs up");
     if(position>.281f+h)check(v.y<=previousSurge+1e-5f,"swash retreats");
     if(position>.2801f&&position<.2799f+h)check(std::abs(v.x-.28f)<2e-5f&&v.y>.99999f,"holds the actual maximum run-up and foam age");
    }
    previousSurge=v.y;
    float dt=period*.001f,lo=time-dt,hi=time+dt;
    float speed=(coastCycle(world,hi,base).z-coastCycle(world,lo,base).z)/(hi-lo);
    // Compare the measured interval against independently integrated speed.
    // At zero hold its derivative has a deliberate corner at prompt reversal;
    // a finite interval's average is not the instantaneous peak speed.
    float integral=0;
    for(int k=0;k<=8;k++)integral+=(k==0||k==8?1:k%2?4:2)*coastCycle(world,lo+(hi-lo)*(k/8.f),base).w;
    float error=std::abs(speed-integral/24.f);maxDerivativeError=std::max(error,maxDerivativeError);
    check(error<=base*.002f+1e-5f,"actual travel derivative continuous and positive");
    if(base>0)check(speed>0,"no paused water frames");
   }
   if(rate>0){
    const float start=(3-phi)*period;
    check(coastCycle(world,start+.28f*basePeriod,base).y>.9998f,"run-up duration unchanged by hold");
    check(coastCycle(world,start+hold+.99f*basePeriod,base).y<.001f,"retreat duration unchanged by hold");
    check(coastCycle(world,start+.275f*basePeriod,base).y<.96f,"run-up does not hide a near-peak pause before Hold");
    check(coastCycle(world,start+hold+.29f*basePeriod,base).y<.98f,"retreat starts promptly as soon as Hold ends");
    for(float fraction:{0.f,.1f,.5f,.9f,1.f}){
     vec4 peak=coastCycle(world,start+.28f*basePeriod+hold*fraction,base);
     check(peak.y>.9998f&&std::abs(peak.x-.28f)<2e-5f,"peak holds for the requested seconds at every crest speed");
    }
    if(base>0)check(coastCycle(world,start+.28f*basePeriod,base).w>base*1.04f && coastCycle(world,start+hold+.99f*basePeriod,base).w<base*.21f,"strong surge followed by gradual slowdown");
    for(float q:{.01f,.04f,.16f,.28f,.5f,.9f,1.f}){
     float time=(q-phi)*period;vec4 initial=coastCycle(world,time,base);
     check(std::abs(initial.w-coastCycle(world,time+period,base).w)<base*.0001f+1e-6f,"pulse repeats at the swash period");
     for(int cycle:{1,17,100,500}){
      vec4 v=coastCycle(world,time+cycle*period,base);
      float e=std::abs(v.z-initial.z-cycle*period*base);maxCycleError=std::max(e,maxCycleError);
      check(e<=.0005f+cycle*period*base*3e-7f,"no temporal UV reset over 500 cycles");
     }
    }
   }
   for(float time:{0.f,1.f,100.f,6000.f}){
    float d=std::abs(coastCycle(world,time,base).z-coastCycle(vec2(0),time,base).z);
    maxSpatialTravelDifference=std::max(d,maxSpatialTravelDifference);
    check(d<=base*period*2.f+1e-4f,"phase offsets stay bounded rather than accumulating spatial shear");
    check(coastCycle(world,time+.125f,base).z>=coastCycle(world,time,base).z,"long-running travel remains forward");
   }
  }
 }
 for(float reach:{0.f,.35f,3.f,8.f})for(float blend:{.05f,2.15f,8.f})for(float river:{0.f,.25f,.5f,.75f,1.f}){
  uAlignLength=reach;uAlignBlend=blend;
  float inner=mix(reach,.10f,river),outer=mix(reach+blend,.65f,river),previous=1;
  for(int i=0;i<=10000;i++){
   float sd=-1+i*.0018f,b=extractedCoastBand(sd,river);
   check(b>=-2e-6f&&b<=1.000002f,"shore blend bounded to float roundoff");check(b<=previous+1e-6f,"shore blend monotone");previous=b;
   if(sd<=inner)check(b==1,"inner shore remains fully directed inland");
   if(sd>=outer)check(b==0,"open ocean has exactly original UV samples");
  }
  const float h=.001f;
  check(std::abs(extractedCoastBand(inner+h,river)-1)<1e-4f,"inner transition has no hard edge");
  check(extractedCoastBand(outer-h,river)<1e-4f,"outer transition has no hard edge");
 }
 uAlignLength=.35f;uAlignBlend=2.15f;
 check(extractedCoastBand(1.f,0.f)>0,"shore steering retained near beach");
 check(extractedCoastBand(2.5f,0.f)==0,"sea influence bounded to 2.5 cells");
 check(extractedCoastBand(.65f,1.f)==0,"river steering width unchanged");
 // Regions are coherent locally, span several seconds, and fade laterally.
 float clockMin=1,clockMax=0,fadeMin=1;
 for(int z=0;z<8;z++)for(int x=0;x<8;x++){
  vec2 centre(x*6.f-2.71f,z*6.f+1.93f);
  float c=coastClock(centre);clockMin=std::min(clockMin,c);clockMax=std::max(clockMax,c);
  check(std::abs(c-coastClock(centre+vec2(.5f,.5f)))<1e-5f,"each swash section has a coherent central clock");
  check(coastSectionFade(centre)>.9999f,"full section opacity at its centre");
  float edge=coastSectionFade(centre+vec2(3.f,3.f));fadeMin=std::min(fadeMin,edge);
  check(edge>=.4999f&&edge<=.5001f,"overlapping section sides fade without grid holes");
  for(float q:{.22f,.78f}){
   vec2 boundary=centre+vec2(6*q,0);float c0=coastClock(boundary),cl=coastClock(boundary-vec2(.001f,0)),cr=coastClock(boundary+vec2(.001f,0));
   check(std::abs(cl-c0)<.0001f&&std::abs(cr-c0)<.0001f,"C2 section joins have no timing seam");
  }
 }
 check(clockMax-clockMin>.65f,"swashes span independent coastal sections rather than a nearly global clock");
 printf("{\"passed\":%s,\"sample_observations\":%u,\"max_derivative_error\":%.9g,\"max_cycle_increment_error\":%.9g,\"max_spatial_travel_difference\":%.9g,\"failed_assertions\":%d}\n",failures?"false":"true",observations,maxDerivativeError,maxCycleError,maxSpatialTravelDifference,failures);
 return failures?1:0;
}
'''


def check_source(raw):
    # The same steady coordinates feed both colour layers, both normal layers and
    # the broad reflection slope. Blending samples avoids a direction*time UV warp.
    required = [
        'coast.z=coastInfluence(spatialCoast,river,wave);',
        'coast.z=coastInfluence(coast.z,river,wave);',
        'col=mix(col,coastalColour,coast.z);',
        't1=mix(t1,nt1,coast.z);t2=mix(t2,nt2,coast.z);',
        'broadSlope=mix(broadSlope,cb,coast.z);',
        'texture2D(uW1,x).rgb*texture2D(uW2,x).rgb',
        'waterCoastNormal(coastX*0.55,coastZ*0.55,coastWeight,coast.xy)',
        'waterCoastNormal(coastX*1.35+0.5,coastZ*1.35+0.5,coastWeight,coast.xy)',
    ]
    for token in required:
        if token not in raw:
            raise ValueError('Missing shared sampled-colour/normal motion path: '+token)
    if raw.count('vec2 coastX=vec2(vWorld.x,-sd-wave.z),coastZ=vec2(vWorld.z,coastX.y);') != 2:
        raise ValueError('Water and beach film must share steady shore coordinates')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True,help='game/water_mod.h')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    raw=a.source.read_text();check_source(raw)
    start=raw.index('#define WATER_COAST_FLOW_GLSL');end=raw.index('static const char* WATER_FS',start)
    parts=[]
    for line in raw[start:end].splitlines():
        match=re.match(r'\s*("(?:[^"\\]|\\.)*")',line)
        if match:parts.append(json.loads(match.group(1)))
    macro=''.join(parts);cycle=macro[:macro.index('vec3 waterCoast(')]
    band=re.search(r'(float inner=.*?;).*?(float band=.*?;)',macro,re.S)
    if not band:raise ValueError('Cannot extract production shore blend expressions')
    glsl=cycle+'\nfloat extractedCoastBand(float sd,float river) {\n'+band[1]+'\n'+band[2]+'\nreturn band;\n}\n'
    (a.output/'extracted-cycle-and-band.glsl').write_text(glsl)
    compiled=re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',glsl).replace('sin(','sinf(')
    source=a.output/'cycle-source-check.cpp';source.write_text(ADAPTER+compiled+MAIN)
    binary=a.output/'cycle-source-check';subprocess.run(['clang++','-std=c++14','-O2',str(source),'-o',str(binary)],check=True)
    run=subprocess.run([str(binary)],capture_output=True,text=True);(a.output/'run.log').write_text(run.stdout+run.stderr)
    result=json.loads(run.stdout.strip().splitlines()[-1]);result.update(source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),extracted_glsl_sha256=hashlib.sha256(glsl.encode()).hexdigest(),sea_band_cells=[.35,2.5],river_band_cells=[.10,.65],scope='CPU source-extracted float math and shared sampling contract; not GPU/visual proof')
    (a.output/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return run.returncode


if __name__=='__main__':sys.exit(main())
