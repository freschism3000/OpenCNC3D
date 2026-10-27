from pathlib import Path
import argparse,subprocess,json,shlex,os,re,hashlib,sys,shutil,tempfile
p=argparse.ArgumentParser();p.add_argument('--repo',required=True,type=Path);p.add_argument('--app-source',type=Path);p.add_argument('--out',required=True,type=Path);p.add_argument('--sdl-config',type=Path);p.add_argument('--baseline',action='store_true',help='Calibrate a source lacking the background guards, instead of accepting it as a regression pass.');a=p.parse_args();r=a.repo.resolve();out=a.out.resolve();app=(a.app_source or r/'app/opencnc3d.cpp').resolve()
allowed=[Path(tempfile.gettempdir()).resolve(),Path('/private/tmp')];assert any(q==out or q in out.parents for q in allowed),'Use a private system-temporary output directory';assert r not in out.parents and out!=r,'Never write inside the repository';out.mkdir(parents=True,exist_ok=True)
s=app.read_text();renderer=(r/'game/cnc_eyes.cpp').read_text();panel=(r/'game/fx_panel.h').read_text()
def function(text,name):
 m=re.search(r'(?m)^(?:static\s+)?(?:inline\s+)?(?:int|void|bool)\s+'+re.escape(name)+r'\([^;\n]*\)\s*\n?\{',text);assert m,name
 start=m.start();i=text.index('{',m.start());depth=1;j=i+1
 while depth:
  if text[j]=='{':depth+=1
  elif text[j]=='}':depth-=1
  j+=1
 return text[start:j]+'\n'
def gate(call):
 rows=s.splitlines()
 for i,line in enumerate(rows):
  if call in line and rows[i-1].strip().startswith('if '):return rows[i-1].strip()+'\n'+line.strip()+'\n'
 raise AssertionError(call)
controls=gate('game_controls_remember(g_ra3dPreview');visuals=gate('game_visuals_remember(fx_cfg)');mp=gate('dms_mp_remember("opencnc3d-multiplayer.cfg"');guarded='!fs_background_only()' in controls;assert guarded!=a.baseline,'Missing guard (or baseline already guarded): select the correct source/--baseline mode'
marker=s.index('const int carried = settings_carry_over();');start=s.rfind('\n    ',0,marker);lines=[x.strip() for x in s[:marker].splitlines() if x.strip()];opening=lines[-1];assert opening in ['{','if (!fs_background_only()) {'];migration=opening+'\nconst int carried=settings_carry_over();(void)carried;\n}\n'
body='''#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <map>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include "fx_gl.h"
#include "fx_state.h"
#include "dosopt.h"
#include "fullscreen.h"
static DOPT_State g_optState;
static bool g_optOpen=false,g_optSeeded=false,g_optHaveBaseline=false,g_visHaveBaseline=false;
static DOPT_Settings g_optOnDisk;
static FxState g_visOnDisk;
static char g_optCfgPath[512]={},g_visCfgPath[512]={},g_fxpSavePath[512]="opencnc3d-fx.cfg";
static bool g_ra3dPreview=false;static int g_gmwWarming=0,g_scriptFails=0;
static unsigned controlApplied=0,mpArmed=0,checks=0,failures=0;
static void opt_apply_settings(void*,const DOPT_Settings*){controlApplied++;}
static bool fxp_is_open(){return false;}
static void dms_mp_remember(const char*,int*){mpArmed++;}
'''
body+=function(panel,'fxp_set_save_path')
for name in ['opt_clampi','opt_controls_save','opt_controls_load','opt_controls_baseline','game_controls_remember','opt_controls_sync','opt_visuals_adopt','game_visuals_remember','opt_visuals_sync','opt_settings_sync']:body+=function(renderer,name)
body+=function(s,'carry_copy')+function(s,'settings_carry_over')
body+='static void startup(const int automated){const char* fx_cfg="opencnc3d-fx.cfg";int mp=0;\n'+controls+visuals+mp+'}\n'
body+='static void migration_start(){'+migration+'}\n'
# The exact explicit script-save statement is unchanged, and exercised separately.
begin=renderer.index('    } else if (!strcmp(cmd, "gfxsave")) {');end=renderer.index('    } else if (!strcmp(cmd, "gfxload")) {',begin);explicit=renderer[begin:end];explicit=explicit[explicit.index('{')+1:];body+='static void explicit_script_save(const char* arg){'+explicit+'\n}\n'
body+=r'''
static void check(bool good,const char* what){checks++;if(!good){failures++;fprintf(stderr,"PREFTEST|FAIL|%s\n",what);}}
static std::string read(const char* p){FILE* f=fopen(p,"rb");if(!f)return "";std::string s;char b[4096];size_t n;while((n=fread(b,1,sizeof b,f)))s.append(b,n);fclose(f);return s;}
static void put(const char* p,const char* text){FILE* f=fopen(p,"wb");if(f){fwrite(text,1,strlen(text),f);fclose(f);}else check(false,"fixture write");}
static void reset(){game_controls_remember(NULL);game_visuals_remember(NULL);g_optHaveBaseline=g_visHaveBaseline=g_optOpen=g_optSeeded=false;g_gmwWarming=0;memset(&g_optState,0,sizeof g_optState);dopt_settings_init(&g_optState.set);fx_defaults(&g_fx);g_fx.enabled=1;fxp_set_save_path("opencnc3d-fx.cfg");controlApplied=mpArmed=0;}
static void dir(const char* base,const char* name){char p[2048];snprintf(p,sizeof p,"%s/%s",base,name);mkdir(p,0700);check(chdir(p)==0,"private case cwd");}
int main(int argc,char** argv){if(argc<3)return 2;const char* root=argv[1];const bool guarded=atoi(argv[2])!=0;check(root[0]!=0,"private fixture root supplied by temporary-output runner");
 for(int bg=0;bg<2;bg++)for(int automated=0;automated<2;automated++)for(int changed=0;changed<2;changed++){
  char label[80];snprintf(label,sizeof label,"implicit-bg%d-auto%d-change%d",bg,automated,changed);dir(root,label);SDL_setenv("OPENCNC3D_BACKGROUND",bg?"1":"0",1);reset();opt_controls_save("opencnc3d-controls.cfg");fx_save(&g_fx,"opencnc3d-fx.cfg");std::string cb=read("opencnc3d-controls.cfg"),vb=read("opencnc3d-fx.cfg");startup(automated);if(changed){g_optState.set.speed=(g_optState.set.speed+1)%DOPT_MAX_SPEED;g_fx.bilinear=!g_fx.bilinear;}opt_settings_sync();bool cw=cb!=read("opencnc3d-controls.cfg"),vw=vb!=read("opencnc3d-fx.cfg");bool expect=!automated&&(!bg||!guarded)&&changed;check(cw==expect,"automatic controls writer");check(vw==expect,"automatic visual writer");check(mpArmed==unsigned(!automated&&(!bg||!guarded)),"multiplayer automatic arm");printf("PREFCASE|%s|controls_written=%d|visuals_written=%d|mp_armed=%u\n",label,cw,vw,mpArmed);
 }
 for(int bg=0;bg<2;bg++){
  char label[40];snprintf(label,sizeof label,"migration-bg%d",bg);dir(root,label);SDL_setenv("OPENCNC3D_BACKGROUND",bg?"1":"0",1);put("cnc3d-fx.cfg","earlier visual bytes\n");put("cnc3d-controls.cfg","earlier control bytes\n");put("cnc3d-multiplayer.cfg","earlier lobby bytes\n");migration_start();bool copied=!read("opencnc3d-fx.cfg").empty();check(copied==(!bg||!guarded),"background migration guard");check(read("cnc3d-fx.cfg")=="earlier visual bytes\n","earlier file preserved");if(copied){check(read("opencnc3d-fx.cfg")=="earlier visual bytes\n","migration exact visual bytes");put("opencnc3d-fx.cfg","existing target\n");migration_start();check(read("opencnc3d-fx.cfg")=="existing target\n","migration never overwrites target");}printf("PREFCASE|%s|implicit_copied=%d\n",label,copied);
 }
 dir(root,"explicit-background");SDL_setenv("OPENCNC3D_BACKGROUND","1",1);reset();fxp_set_save_path("panel-private.cfg");startup(0);bool explicitPanel=!strcmp(g_fxpSavePath,"panel-private.cfg");check(explicitPanel==guarded,"explicit gfxsave path kept in background");fx_save(&g_fx,g_fxpSavePath);check(!read(guarded?"panel-private.cfg":"opencnc3d-fx.cfg").empty(),"actual panel-target save");explicit_script_save("script-private.cfg");check(!read("script-private.cfg").empty(),"explicit gfxsave script works");reset();game_visuals_remember("visuals-private.cfg");startup(0);g_fx.bilinear=!g_fx.bilinear;opt_visuals_sync();check(!read(guarded?"visuals-private.cfg":"opencnc3d-fx.cfg").empty(),"explicit remember is retained only when automatic override blocked");printf("PREFCASE|explicit-background|panel_private_retained=%d|script_save=1|explicit_visuals_retained=%d\n",explicitPanel,guarded);
 printf("PREFTEST|checks=%u|failures=%u|guarded=%d|no_sdl_init=1|no_windows=1\n",checks,failures,guarded);return failures?1:0;
}
'''
cpp=out/'actual-prefs.cpp';cpp.write_text(body)
# This fixture runs real data serialization and source-extracted startup gates,
# not a replacement IO model. No SDL initialization, windows, GL calls or game run.
bundled=Path.home()/'.opencnc3d-macsdk/bin/sdl2-config';sdk=a.sdl_config or (bundled if bundled.exists() else Path(shutil.which('sdl2-config') or ''));assert sdk.is_file(),'Provide --sdl-config';flags=shlex.split(subprocess.check_output([str(sdk),'--cflags'],text=True));libs=shlex.split(subprocess.check_output([str(sdk),'--libs'],text=True));obj=out/'dosopt.o';bar=out/'dosbar.o';exe=out/'actual-prefs';env={**os.environ,'MACOSX_DEPLOYMENT_TARGET':'14.0'};platformlibs=['-framework','OpenGL'] if sys.platform=='darwin' else ['-lGL'];prefix=Path(subprocess.check_output([str(sdk),'--prefix'],text=True).strip());rpath=['-Wl,-rpath,'+str(prefix/'lib')] if os.name!='nt' else []
with (out/'compile.txt').open('w') as log:
 subprocess.run(['clang','-std=c89','-O2','-I'+str(r/'game'),'-c',str(r/'game/dosopt.c'),'-o',str(obj)],check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
 subprocess.run(['clang','-std=c89','-O2','-I'+str(r/'game'),'-c',str(r/'game/dosbar.c'),'-o',str(bar)],check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
 subprocess.run(['clang++','-std=c++14','-O2','-DGL_SILENCE_DEPRECATION','-I'+str(r/'game'),*flags,str(cpp),str(obj),str(bar),*libs,'-lz',*rpath,*platformlibs,'-o',str(exe)],check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
case=out/'cases';assert not case.exists(),'Use a fresh output directory';case.mkdir();q=subprocess.run([str(exe),str(case),'1' if guarded else '0'],capture_output=True,text=True,timeout=30);(out/'run.txt').write_text(q.stdout+q.stderr);assert q.returncode==0,q.stdout+q.stderr;result={'passed':True,'guarded':guarded,'no_game_or_window_launched':True,'real_controls_and_visual_serializers':True,'source_gates_extracted_exactly':True,'multiplayer_arm_checked_with_counter':True,'explicit_handlers_unchanged':True,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'source_hashes':{n:hashlib.sha256((r/n).read_bytes()).hexdigest() for n in ('game/cnc_eyes.cpp','game/fx_state.h','game/dosopt.c','game/dosopt.h','game/fx_panel.h')},'app_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'results':[l for l in q.stdout.splitlines() if l.startswith(('PREFCASE|','PREFTEST|'))]};(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
