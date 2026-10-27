#!/usr/bin/env python3
"""Compile actual control-file helpers with real private file I/O and mocked mixer.
No SDL, graphics, game, user settings or shared runtime is started. Default is
syntax-only; --run explicitly executes the small portable helper proof.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
ap=argparse.ArgumentParser();ap.add_argument('--repo',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--run',action='store_true');a=ap.parse_args();a.repo=a.repo.resolve();a.out=a.out.resolve()
scratch_roots={Path(tempfile.gettempdir()).resolve(),Path('/tmp').resolve()}
if not any(p in a.out.parents for p in scratch_roots):raise SystemExit('Use a private child folder under the system temporary directory.')
a.out.mkdir(parents=True,exist_ok=True);p=a.repo/'game/cnc_eyes.cpp';text=p.read_text();start=text.index('static char g_optCfgPath[512]');end=text.index('/* THE DATABASE TAB. Included HERE',start);actual=text[start:end]
app=(a.repo/'app/opencnc3d.cpp').read_text();assert 'if (!automated) {\n        const char* controls' in app;assert 'if (fs_background_only()) game_controls_load_readonly(controls);' in app;assert 'else game_controls_remember(controls);' in app
head=r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "dosopt.h"
static DOPT_State g_optState={};static bool g_optSeeded=false,g_optOpen=false;
static unsigned checks=0,applies=0,readOpens=0,writeOpens=0;
static void check(bool v,const char* msg){checks++;if(!v){fprintf(stderr,"FAIL|%s\n",msg);exit(2);}}
void dopt_settings_init(DOPT_Settings* s){s->speed=4;s->scrollrate=3;s->music=s->sound=s->speech=s->ambience=DOPT_VOL_TOP;}
static void opt_apply_settings(void*,const DOPT_Settings*){applies++;}
static FILE* tracked_fopen(const char* p,const char* mode){if(mode[0]=='r')readOpens++;else writeOpens++;return fopen(p,mode);}
#define fopen(p,m) tracked_fopen(p,m)
'''
tail=r'''
static void reset(){memset(&g_optState,0,sizeof g_optState);g_optState.set.speed=-9;g_optCfgPath[0]=0;g_optHaveBaseline=false;g_optSeeded=false;g_optOpen=false;applies=readOpens=writeOpens=0;}
static void fixture(const std::string& p,const char* s){FILE* f=(fopen)(p.c_str(),"w");check(f!=NULL,"private fixture writable");fputs(s,f);fclose(f);}
static std::string contents(const std::string& p){FILE* f=(fopen)(p.c_str(),"r");if(!f)return "";std::string s;char b[512];size_t n;while((n=fread(b,1,sizeof b,f)))s.append(b,n);fclose(f);return s;}
int main(int argc,char** argv){check(argc==2,"private fixture directory supplied");std::string root=argv[1],missing=root+"/absent.cfg",present=root+"/present.cfg";
 remove(missing.c_str());fixture(present,"speed 99\nscroll -2\nmusic 123\nsound 234\nspeech 12\nambience 99\n");std::string before=contents(present);
 reset();strcpy(g_optCfgPath,"a-prior-explicit-save.cfg");game_controls_load_readonly(missing.c_str());check(readOpens==1&&writeOpens==0,"missing readonly path is read once");check(!g_optSeeded&&g_optState.set.speed==-9,"missing readonly leaves first-open seeding intact");check(!g_optCfgPath[0],"readonly clears any prior save permission");dopt_settings_init(&g_optState.set);opt_controls_baseline();g_optState.set.speed=2;opt_controls_sync();check(writeOpens==0&&contents(missing).empty(),"readonly first UI change cannot mint a file");
 reset();game_controls_load_readonly(present.c_str());check(g_optSeeded&&applies==1,"readonly restores and applies before first tick");check(g_optHaveBaseline&&!g_optCfgPath[0],"readonly baseline never arms save path");check(g_optState.set.speed==DOPT_MAX_SPEED-1&&g_optState.set.scrollrate==0,"actual parser retains clamps");check(g_optState.set.music==123&&g_optState.set.sound==234&&g_optState.set.speech==12&&g_optState.set.ambience==99,"all six actual parser fields restored");g_optState.set.speed=1;opt_controls_sync();check(writeOpens==0&&contents(present)==before,"readonly later edits cannot overwrite loaded file");
 reset();game_controls_remember(present.c_str());check(g_optSeeded&&applies==1&&!strcmp(g_optCfgPath,present.c_str()),"explicit remember restores and arms named path");opt_controls_sync();check(writeOpens==0,"unchanged explicit baseline does not write");g_optState.set.speed=2;opt_controls_sync();check(writeOpens==1&&contents(present).find("speed   2\n")!=std::string::npos,"explicit change still writes even for a background caller");opt_controls_sync();check(writeOpens==1,"explicit sync takes new baseline");
 reset();game_controls_remember(missing.c_str());check(!g_optSeeded&&g_optState.set.speed==-9&&g_optCfgPath[0],"normal missing file keeps old seed and remember behavior");dopt_settings_init(&g_optState.set);opt_controls_baseline();opt_controls_sync();check(writeOpens==0&&contents(missing).empty(),"normal no-op pause does not mint settings");g_optState.set.scrollrate=1;g_optOpen=true;opt_controls_sync();check(writeOpens==0,"normal save waits until pause closes");g_optOpen=false;opt_controls_sync();check(writeOpens==1&&contents(missing).find("scroll  1\n")!=std::string::npos,"normal changed pause saves once");
 reset();game_controls_remember(NULL);game_controls_load_readonly("");check(readOpens==0&&writeOpens==0&&!g_optCfgPath[0],"empty API paths disable remembering without opening files");
 reset();game_controls_remember(present.c_str());char same[sizeof g_optCfgPath];strcpy(same,g_optCfgPath);game_controls_load_readonly(g_optCfgPath);check(!g_optCfgPath[0]&&readOpens==2&&applies==2,"readonly handles aliased old path without erasing its load input");check(contents(same)==contents(present),"aliased readonly path preserved contents");
 printf("CONTROLSREADONLY|passed|checks=%u|no_SDL_GL=1\n",checks);return 0;}
'''
source=a.out/'controls-actual-helper.cpp';source.write_text(head+actual+tail);cmd=['clang++','-std=c++14','-O2','-I'+str(a.repo/'game')]
if a.run:cmd+=['-o',str(a.out/'controls-proof'),str(source)]
else:cmd+=['-fsyntax-only',str(source)]
p=subprocess.run(cmd,text=True,capture_output=True);(a.out/'compile.txt').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
result={'passed':True,'syntax_only':not a.run,'source_sha256':hashlib.sha256(actual.encode()).hexdigest(),'actual_parser_restore_sync':True,'mocked_mixer':True,'game_native_or_graphics_started':False}
if a.run:
 p=subprocess.run([str(a.out/'controls-proof'),str(a.out)],text=True,capture_output=True,timeout=10);(a.out/'run.txt').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr;result['stdout']=p.stdout.splitlines()
(a.out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
