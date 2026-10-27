#!/usr/bin/env python3
"""Data-only SDL mocks for native fullscreen, persistent modes and drawable status.

Example: python3 tools/gate-display-modes.py --repo /path/to/CNC3D --out /tmp/display-checks
Optional: --baseline /path/to/before-source (expects those checks to fail).
Uses the actual supplied renderer/helper/FX/menu source, never a fixture copy by default.
Requires a C++ compiler and SDL2 development headers (sdl2-config, or --sdl-cflags).
Does not link SDL, create a window, initialize video/audio or change a monitor mode.
"""
import argparse, hashlib, json, os, re, shlex, subprocess, tempfile
from pathlib import Path


def function(text, name):
    match = re.search(r'^(?:FS_MAYBE_UNUSED )?(?:static )?(?:void|int) ' + name + r'\([^;]+?\)\n\{', text, re.M)
    if not match:
        raise ValueError("Function missing from supplied source: " + name)
    start = match.start()
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        if text[end] == '{': depth += 1
        if text[end] == '}': depth -= 1
        end += 1
    return text[start:end]

CODE = r'''
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include <fstream>
#include "@FX_STATE@"
#include "@DOPT@"
extern "C" void game_display_changed(SDL_Window*,int);
#include "@FULLSCREEN@"
@EXTRAFS@
struct SDL_Window { Uint32 flags=SDL_WINDOW_SHOWN; int w=1280,h=720,savedw=1280,savedh=720; SDL_DisplayMode preferred={}; void* changed=nullptr; };
static SDL_Window window;
static std::vector<std::string> calls;
static int deskfail=0,modefail=0,fullfail=0,closestfail=0,previousfail=0,fullcalls=0;
static std::vector<int> fail_full_calls;
extern "C" {
void* SDL_memset(void* p,int c,size_t n){return std::memset(p,c,n);}
char* SDL_getenv(const char* n){return std::getenv(n);}
const char* SDL_GetError(){return "mock injected failure";}
void* SDL_SetWindowData(SDL_Window*w,const char*,void*p){void*old=w->changed;w->changed=p;return old;}
void* SDL_GetWindowData(SDL_Window*w,const char*){return w->changed;}
SDL_Window* SDL_GetWindowFromID(Uint32 id){return id==1?&window:nullptr;}
Uint32 SDL_GetWindowFlags(SDL_Window* w){return w->flags;}
int SDL_GetWindowDisplayIndex(SDL_Window*){return 0;}
void SDL_GetWindowSize(SDL_Window* w,int* x,int* y){*x=w->w;*y=w->h;}
int SDL_GetDesktopDisplayMode(int,SDL_DisplayMode* out){calls.push_back("desktop");if(deskfail)return -1;SDL_zero(*out);out->w=1920;out->h=1080;out->refresh_rate=144;out->format=SDL_PIXELFORMAT_RGB888;return 0;}
int SDL_GetDisplayUsableBounds(int,SDL_Rect* r){r->x=0;r->y=0;r->w=1920;r->h=1040;return 0;}
SDL_DisplayMode* SDL_GetClosestDisplayMode(int,const SDL_DisplayMode* want,SDL_DisplayMode* out){calls.push_back("closest");if(closestfail)return nullptr;*out=*want;out->refresh_rate=144;return out;}
int SDL_GetWindowDisplayMode(SDL_Window*w,SDL_DisplayMode*out){if(previousfail)return -1;*out=w->preferred;if(out->w<=0)out->w=w->w;if(out->h<=0)out->h=w->h;return 0;}
int SDL_SetWindowDisplayMode(SDL_Window* w,const SDL_DisplayMode* m){calls.push_back("preferred");if(modefail){modefail--;return -1;}if(m)w->preferred=*m;else SDL_zero(w->preferred);return 0;}
int SDL_SetWindowFullscreen(SDL_Window* w,Uint32 f){calls.push_back("fullscreen:"+std::to_string(f));fullcalls++;if(fullfail||std::find(fail_full_calls.begin(),fail_full_calls.end(),fullcalls)!=fail_full_calls.end())return -1;bool was=w->flags&SDL_WINDOW_FULLSCREEN;if(!was&&f){w->savedw=w->w;w->savedh=w->h;}w->flags=(w->flags&~SDL_WINDOW_FULLSCREEN_DESKTOP)|f;if(f==SDL_WINDOW_FULLSCREEN_DESKTOP){w->w=1920;w->h=1080;}else if(f){w->w=w->preferred.w>0?w->preferred.w:w->savedw;w->h=w->preferred.h>0?w->preferred.h:w->savedh;}else if(was){w->w=w->savedw;w->h=w->savedh;}return 0;}
void SDL_SetWindowBordered(SDL_Window*,SDL_bool){calls.push_back("border");}
void SDL_SetWindowResizable(SDL_Window*,SDL_bool){calls.push_back("resizable");}
void SDL_SetWindowSize(SDL_Window* w,int x,int y){calls.push_back("size");w->w=x;w->h=y;}
void SDL_SetWindowPosition(SDL_Window*,int,int){calls.push_back("position");}
SDL_Window* SDL_GL_GetCurrentWindow(){return &window;}
}
static FxState g_visOnDisk;
static DOPT_State g_optState;
static char g_visCfgPath[512]={};
static bool g_fxPresetNamed=false,g_fxDisplayPresetLoaded=false,g_optOpen=false,automated=false,panel=false;
static bool opt_run_is_automated(){return automated;}
static int shell_automated(){return automated?1:0;}
static int fxp_is_open(){return panel;}
static void opt_visuals_adopt(){g_visOnDisk=g_fx;}
static void res_desktop(int*dw,int*dh,int*uw,int*uh){*dw=1920;*dh=1080;*uw=1920;*uh=1040;}
@FUNCTIONS@
static void app_startup_display_sync(SDL_Window*win,int dispW,int dispH){@BOOTSTRAP@}
static std::string readall(const std::string& path){std::ifstream f(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());}
static std::string currentline;
static const int MUTED=0;
static void text(const std::string&s,int,int,int){currentline=s;}
static void current_status(DOPT_State*st){@STATUS@}
static void capture_drawable(int fbw,int fbh){@ASSIGNMENTS@}
static int failures=0;
static void check(bool v,const char*n){printf("DISPLAYTEST|%s|%s\n",v?"pass":"FAIL",n);if(!v)failures++;}
static void reset(){window=SDL_Window{};fs_set_change_handler(&window,game_display_changed);calls.clear();deskfail=modefail=fullfail=closestfail=previousfail=fullcalls=0;fail_full_calls.clear();automated=panel=g_optOpen=g_fxPresetNamed=g_fxDisplayPresetLoaded=false;g_visCfgPath[0]=0;unsetenv("OPENCNC3D_BACKGROUND");fx_defaults(&g_fx);}
static SDL_Event key(){SDL_Event e={};e.type=SDL_KEYDOWN;e.key.windowID=1;e.key.keysym.sym=SDLK_RETURN;e.key.keysym.mod=KMOD_ALT;return e;}
int main(int argc,char**argv){
 if(argc!=2)return 2;std::string path=argv[1];
 reset();check(!(fs_initial_flags(FS_MODE_FULLSCREEN)&SDL_WINDOW_FULLSCREEN),"exclusive mode deferred until explicit preferred mode");
 check((fs_initial_flags(FS_MODE_BORDERLESS)&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP,"borderless startup flags fill desktop");
 reset();check(fs_apply_mode(&window,FS_MODE_FULLSCREEN,0,0)&&window.w==1920&&window.h==1080&&window.preferred.refresh_rate==144,"Desktop fullscreen selects native size and refresh");
 check(calls.size()>=4&&calls[0]=="desktop"&&calls[1]=="fullscreen:0"&&calls[2]=="preferred"&&calls[3]=="fullscreen:1","desktop mode resolved and installed before exclusive entry");
 reset();fs_apply_mode(&window,FS_MODE_FULLSCREEN,640,480);fs_apply_mode(&window,FS_MODE_FULLSCREEN,0,0);check(window.w==1920&&window.h==1080,"Desktop clears stale 640x480 exclusive mode");
 reset();deskfail=1;check(!fs_apply_mode(&window,FS_MODE_FULLSCREEN,0,0)&&window.w==1280&&calls.size()==1,"desktop lookup failure changes no display state");
 reset();app_startup_display_sync(&window,0,0);check((int)g_fx.display_mode==0&&(int)g_fx.res_w==1280,"no-preset windowed startup matches live dial without saving");
 reset();game_display_started(FS_MODE_WINDOWED,1280,720);g_fx.enabled=1;g_fx.bloom_on=1;g_fx.vsync=2;snprintf(g_visCfgPath,sizeof g_visCfgPath,"%s",path.c_str());fx_save(&g_fx,path.c_str());opt_visuals_adopt();SDL_Event e=key();check(fs_handle_event(&e)==1,"normal fullscreen key consumed");
 FxState loaded;fx_defaults(&loaded);fx_load(&loaded,path.c_str());check((int)loaded.display_mode==FS_MODE_BORDERLESS&&loaded.enabled==1&&loaded.bloom_on==1&&loaded.vsync==2,"keyboard borderless choice persisted with visuals and VSync preserved");
 g_visCfgPath[0]=0;fx_defaults(&g_fx);int mode=0,rw=0,rh=0;check(game_display_wanted(path.c_str(),&mode,&rw,&rh)&&mode==FS_MODE_BORDERLESS,"restart reads persisted borderless choice");
 window=SDL_Window{};fs_set_change_handler(&window,game_display_changed);window.flags|=fs_initial_flags(mode);if((window.flags&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP){window.w=1920;window.h=1080;}
 check(fs_window_mode(&window)==FS_MODE_BORDERLESS&&window.w==1920&&window.h==1080,"borderless restart fills native desktop in mock SDL");
 game_display_started(mode,rw,rh);snprintf(g_visCfgPath,sizeof g_visCfgPath,"%s",path.c_str());fs_handle_event(&e);fx_defaults(&loaded);fx_load(&loaded,path.c_str());check((int)loaded.display_mode==FS_MODE_WINDOWED&&(int)loaded.res_w==1280&&(int)loaded.res_h==720,"keyboard return persists restored window dimensions");
 reset();game_display_started(0,1280,720);snprintf(g_visCfgPath,sizeof g_visCfgPath,"%s",path.c_str());fx_save(&g_fx,path.c_str());auto modal_before=readall(path);g_optOpen=true;g_optState={};g_optState.vis.dispmode=0;g_optState.vis.nres=2;g_optState.vis.res_w[0]=1920;g_optState.vis.res_h[0]=1080;g_optState.vis.res_w[1]=1280;g_optState.vis.res_h[1]=720;fs_handle_event(&e);check(g_optState.vis.dispmode==1&&g_optState.vis.residx==1&&readall(path)==modal_before,"modal hotkey synchronizes display button and leaves save to dialog");
 reset();g_fxPresetNamed=true;g_fxDisplayPresetLoaded=true;g_fx.display_mode=2;g_fx.res_w=1600;g_fx.res_h=900;game_display_wanted(path.c_str(),&mode,&rw,&rh);check(mode==2&&rw==1600&&rh==900,"explicit gfx display choice wins remembered working-folder preset");
 reset();g_fxDisplayPresetLoaded=true;snprintf(g_visCfgPath,sizeof g_visCfgPath,"%s",path.c_str());g_fx.display_mode=2;g_fx.res_w=800;g_fx.res_h=600;game_display_wanted(nullptr,&mode,&rw,&rh);check(mode==2&&rw==800&&rh==600,"explicit visualscfg display choice honored");
 reset();g_fxPresetNamed=true;g_fxDisplayPresetLoaded=false;check(!game_display_wanted(path.c_str(),&mode,&rw,&rh),"missing named gfx is not a restored display preset");
 reset();snprintf(g_visCfgPath,sizeof g_visCfgPath,"%s",(path+"-missing").c_str());g_fxDisplayPresetLoaded=false;check(!game_display_wanted(path.c_str(),&mode,&rw,&rh),"missing visualscfg is not a restored borderless preference");
 reset();game_display_started(0,1280,720);snprintf(g_visCfgPath,sizeof g_visCfgPath,"%s",path.c_str());fx_save(&g_fx,path.c_str());auto before=readall(path);fullfail=1;fs_handle_event(&e);check((int)g_fx.display_mode==0&&readall(path)==before,"failed keyboard mode switch does not persist a false mode");
 reset();DOPT_Visuals v={};v.dispmode=2;v.nres=1;g_fx.display_mode=0;g_fx.res_w=1280;g_fx.res_h=720;modefail=1;opt_apply_display(&v);check((int)g_fx.display_mode==0&&g_fx.res_w==1280,"failed Visuals mode application does not commit requested dial");
 reset();fs_apply_mode(&window,2,1280,720);game_display_started(2,1280,720);v={};v.dispmode=2;v.nres=1;modefail=1;opt_apply_display(&v);check(fs_window_mode(&window)==2&&window.w==1280&&window.h==720&&(int)g_fx.display_mode==2&&g_fx.res_w==1280,"rejected preferred mode rolls back prior exclusive mode and dial");
 reset();fs_apply_mode(&window,2,1280,720);game_display_started(2,1280,720);fullcalls=0;fail_full_calls={2};v={};v.dispmode=2;v.nres=1;opt_apply_display(&v);check(fs_window_mode(&window)==2&&window.w==1280&&window.h==720&&g_fx.res_w==1280,"rejected exclusive entry rolls back previous preferred mode and flags");
 reset();fs_apply_mode(&window,2,1280,720);game_display_started(2,1280,720);fullcalls=0;fail_full_calls={2,4};v={};v.dispmode=2;v.nres=1;g_optOpen=true;opt_apply_display(&v);check(fs_window_mode(&window)==0&&(int)g_fx.display_mode==0&&g_fx.res_w==1280&&g_optState.vis.dispmode==0,"rollback rejection reconciles live windowed mode and modal selector");
 reset();deskfail=1;fs_apply_mode(&window,2,0,0);app_startup_display_sync(&window,0,0);check((int)g_fx.display_mode==0&&g_fx.res_w==1280,"startup fullscreen failure reports actual windowed mode without saving");
 reset();setenv("OPENCNC3D_BACKGROUND","1",1);before=readall(path);snprintf(g_visCfgPath,sizeof g_visCfgPath,"%s",path.c_str());int initial=(int)g_fx.display_mode;check(!(fs_initial_flags(2)&SDL_WINDOW_FULLSCREEN),"background startup flags never enter fullscreen");check(!fs_apply_mode(&window,2,0,0),"background helper refuses display mode change");fs_handle_event(&e);game_display_changed(&window,2);app_startup_display_sync(&window,0,0);check(calls.empty()&&(int)g_fx.display_mode==initial&&readall(path)==before,"background keyboard and persistence callback leave display and config alone");
 reset();window.flags|=SDL_WINDOW_HIDDEN;check(!fs_apply_mode(&window,2,0,0),"hidden window refuses mode change");fs_handle_event(&e);check(calls.empty(),"hidden keyboard shortcut changes no mode");
 reset();e.key.repeat=1;fs_handle_event(&e);check(calls.empty(),"held fullscreen key cannot repeat mode transitions");
 g_optState={};g_optState.vis.nres=1;g_optState.vis.res_w[0]=1920;g_optState.vis.res_h[0]=1080;g_optState.vis.drawable_w=0;g_optState.vis.drawable_h=0;capture_drawable(1280,720);current_status(&g_optState);check(currentline=="Current: 1280 X 720","Current label shows drawable independently of selected Desktop");capture_drawable(1920,1080);current_status(&g_optState);check(currentline=="Current: 1920 X 1080","Current label follows actual resize");
 printf("DISPLAYTEST|summary|failures=%d\n",failures);return failures?1:0;
}
'''

def run(repo, source, out, tag, cxx, sdl_flags, baseline=False):
    renderer = (repo / "game/cnc_eyes.cpp").read_text()
    functions = "\n".join(function(renderer, name) for name in
                           ("game_display_started", "game_display_changed"))
    actual_renderer = (source / "game/cnc_eyes.cpp").read_text()
    functions += "\n" + function(actual_renderer, "game_display_wanted")
    functions += "\n" + function(actual_renderer, "opt_apply_display")
    helper = (source / "game/fullscreen.h").read_text()
    extra_fs = ""
    # New startup/status observers let the same test exercise the old mode-switching
    # implementation. They do not replace the baseline helper being measured.
    if "static int fs_window_mode(" not in helper:
        functions += "\n" + function((repo / "game/fullscreen.h").read_text(), "fs_window_mode")
    if "static void fs_set_change_handler(" not in helper:
        extra_fs = "typedef void (*FS_DisplayChanged)(SDL_Window*,int);\n" + function(
            (repo / "game/fullscreen.h").read_text(), "fs_set_change_handler")
    app = (source / "app/opencnc3d.cpp").read_text()
    bootstrap = ""
    anchor = "        const int actualMode = fs_window_mode(win);"
    if anchor in app:
        start = app.rfind("    if (", 0, app.index(anchor))
        opening = app.index("{", start)
        depth, end = 1, opening + 1
        while depth:
            if app[end] == "{": depth += 1
            if app[end] == "}": depth -= 1
            end += 1
        bootstrap = app[start:end]
    status = next(line for line in (source / "menu/enhanced_menu.cpp").read_text().splitlines()
                  if 'text("Current: ' in line)
    assignments = "\n".join(line for line in (source / "game/dosopt_gl.h").read_text().splitlines()
                            if "g_optState.vis.drawable_" in line)
    replacements = {
        "@FX_STATE@": str(repo / "game/fx_state.h"),
        "@DOPT@": str(repo / "game/dosopt.h"),
        "@FULLSCREEN@": str(source / "game/fullscreen.h"),
        "@FUNCTIONS@": functions, "@STATUS@": status,
        "@ASSIGNMENTS@": assignments, "@EXTRAFS@": extra_fs, "@BOOTSTRAP@": bootstrap,
    }
    code = CODE
    for old, new in replacements.items(): code = code.replace(old, new)
    cpp = out / ("display-test-" + tag + ".cpp")
    exe = out / ("display-test-" + tag + (".exe" if os.name == "nt" else ""))
    cpp.write_text(code)
    compiler = cxx + ["-std=c++11", "-O1", "-Wno-unused-function"] + sdl_flags + [
        "-I" + str(repo / "game"), "-I" + str(repo / "menu"), str(cpp), "-o", str(exe)]
    subprocess.run(compiler, check=True)
    result = subprocess.run([str(exe), str(out / ("test-" + tag + ".cfg"))],
                            text=True, capture_output=True)
    (out / ("test-" + tag + ".log")).write_text(result.stdout + result.stderr)
    print(result.stdout, end="")
    source_hashes = {}
    for name in ("app/opencnc3d.cpp", "game/cnc_eyes.cpp", "game/fullscreen.h", "game/dosopt_gl.h", "menu/enhanced_menu.cpp"):
        source_hashes[name] = hashlib.sha256((source / name).read_bytes()).hexdigest()
    record = {
        "compiler": compiler, "source": str(source), "source_sha256": source_hashes,
        "exit_code": result.returncode, "pass_count": result.stdout.count("|pass|"),
        "failure_count": result.stdout.count("|FAIL|"),
        "baseline_expected_to_fail": baseline, "native_SDL_or_game_started": False,
    }
    (out / ("test-" + tag + ".json")).write_text(json.dumps(record, indent=2) + "\n")
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="actual fixed repository source")
    parser.add_argument("--out", type=Path, help="scratch output folder (default: a new temporary folder)")
    parser.add_argument("--baseline", type=Path, help="optional original source; expected to fail regression checks")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"), help="C++ compiler command")
    parser.add_argument("--sdl-cflags", help="SDL2 header flags; otherwise read sdl2-config --cflags")
    args = parser.parse_args()
    repo = args.repo.resolve()
    if not (repo / "game/fullscreen.h").is_file():
        parser.error("--repo must name the actual CNC3D source root")
    out = args.out.resolve() if args.out else Path(tempfile.mkdtemp(prefix="opencnc3d-display-checks-"))
    out.mkdir(parents=True, exist_ok=True)
    try:
        flags = args.sdl_cflags if args.sdl_cflags is not None else subprocess.check_output(
            ["sdl2-config", "--cflags"], text=True).strip()
        cxx, sdl = shlex.split(args.cxx), shlex.split(flags)
        result = run(repo, repo, out, "after", cxx, sdl)
        if args.baseline:
            old = run(repo, args.baseline.resolve(), out, "before", cxx, sdl, baseline=True)
            if old == 0:
                print("Baseline unexpectedly passes; regression proof is missing.")
                result = 1
        print("Evidence written to " + str(out))
        return result
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print("Display checks could not run: " + str(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
