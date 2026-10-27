#!/usr/bin/env python3
"""Compile real collector polygon-state helpers against a modeled ordered driver.
The full native renderer pixel/float-target regression covers actual replay,
client fallback, compact failure, cache pressure, fog and mode transitions.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,os
ap=argparse.ArgumentParser();ap.add_argument('--repo',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.repo=a.repo.resolve();a.out=a.out.resolve()
if Path('/private/tmp') not in a.out.parents:raise SystemExit('Use a private /private/tmp output folder.')
a.out.mkdir(parents=True,exist_ok=True);header=a.repo/'game/mesh_batch.h';text=header.read_text();helpers=text[text.index('static bool g_meshFragmentCollect = false;'):text.index('static std::vector<MeshBatchVertex> g_meshFragmentVertices;')]
head=r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef TEST_WINDOWS_DEFAULT
#define _WIN32 1
#endif
using GLenum=unsigned;using GLboolean=unsigned char;
const GLenum GL_CULL_FACE=1,GL_POLYGON_OFFSET_FILL=2;const GLboolean GL_TRUE=1;
static bool physical[3]={};static unsigned long long calls=0,checks=0;
static void glEnable(GLenum cap){physical[cap]=true;calls++;}
static void glDisable(GLenum cap){physical[cap]=false;calls++;}
static GLboolean glIsEnabled(GLenum cap){return physical[cap]?1:0;}
static void check(bool ok,const char* text){checks++;if(!ok){fprintf(stderr,"FAIL|%s\n",text);exit(2);}}
'''
tail=r'''
static unsigned rng=1234567;static unsigned next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char** argv){bool enabled=argc>1?strcmp(argv[1],"0")!=0:
#ifdef _WIN32
true;
#else
false;
#endif
check(mesh_fragment_polygon_defer_enabled()==enabled,"platform default or explicit launch override");
for(int scope=0;scope<2000;scope++){
 physical[1]=next()&1;physical[2]=next()&1;bool expected[3]={false,physical[1],physical[2]};
 g_meshFragmentCollect=true;g_meshFragmentStateDeferred=enabled;g_meshFragmentCull=physical[1];g_meshFragmentOffset=physical[2];std::vector<std::pair<bool,bool>> commands;
 auto flush=[&](){for(auto c:commands){physical[1]=c.first;physical[2]=c.second;check(physical[1]==c.first&&physical[2]==c.second,"draw observes captured polygon state");}commands.clear();mesh_fragment_polygon_apply();check(physical[1]==expected[1]&&physical[2]==expected[2],"flush publishes final logical state, including empty prefix");};
 for(int i=0;i<100;i++){unsigned v=next();GLenum cap=(v&1)?GL_CULL_FACE:GL_POLYGON_OFFSET_FILL;bool on=(v>>1)&1;expected[cap]=on;if(!mesh_fragment_polygon_set(cap,on)){if(on)glEnable(cap);else glDisable(cap);}check(mesh_fragment_polygon(cap)==expected[cap],"command observes logical enable");commands.push_back({mesh_fragment_polygon(GL_CULL_FACE),mesh_fragment_polygon(GL_POLYGON_OFFSET_FILL)});check(commands.back().first==expected[1]&&commands.back().second==expected[2],"captured enable order exact");if(v%11==0)flush();if(v%23==0){flush();g_meshFragmentCollect=false;check(!mesh_fragment_polygon_set(cap,!on),"ordinary fallback bypasses deferred setter");check(mesh_fragment_polygon(cap)==expected[cap],"ordinary client/immediate draw sees physical enable");g_meshFragmentCollect=true;}}
 g_meshFragmentCollect=false;flush();g_meshFragmentStateDeferred=false;check(!mesh_fragment_polygon_set(GL_CULL_FACE,true),"scope teardown removes interception");
}
printf("FRAGMENTPOLYGONTEST|passed|enabled=%d|checks=%llu|calls=%llu\n",enabled,checks,calls);return 0;}
'''
source=a.out/'fragment-polygon.cpp';source.write_text(head+helpers+tail);env={k:v for k,v in os.environ.items() if k!='OPENCNC3D_FRAGMENT_POLYGON_DEFER'};rows=[]
for platform,flags in [('native',[]),('windows-default',['-DTEST_WINDOWS_DEFAULT'])]:
 exe=a.out/platform;subprocess.run(['clang++','-std=c++14','-O2',*flags,'-o',str(exe),str(source)],check=True,env=env,capture_output=True)
 for control in [None,'0','1']:
  e=env.copy()
  if control is not None:e['OPENCNC3D_FRAGMENT_POLYGON_DEFER']=control
  p=subprocess.run([str(exe)]+([] if control is None else [control]),env=e,text=True,capture_output=True);assert p.returncode==0,p.stderr;rows.append({'platform':platform,'override':control,'stdout':p.stdout.splitlines()})
result={'passed':True,'actual_header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'modeled_driver':True,'native_windows_runtime':False,'results':rows};(a.out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
