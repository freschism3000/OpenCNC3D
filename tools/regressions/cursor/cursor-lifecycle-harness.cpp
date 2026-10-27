#define OPENCNC3D_NO_MAIN 1
#include "cnc_eyes.cpp"
#include "../menu/dosmenu_shell.h"
#include <cassert>
static void title(SDL_Window *win, const std::string &path) {
    assert(game_visuals_open(win, "dossidebar.pack",path.c_str())==0);
}
int main(int argc,char **argv){
    assert(argc>=2);SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
    SDL_Window *win=SDL_CreateWindow("Private cursor lifecycle regression",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(win);
    SDL_GLContext ctx=SDL_GL_CreateContext(win);assert(ctx);game_visuals_default_enhanced(NULL);
    DMS_Config cfg={};cfg.pack="dosmenu.pack";cfg.version="cursor regression";cfg.copyright="";DMS menu;char err[256];assert(dms_open(&menu,win,&cfg,err,sizeof err));
    std::string out=argv[1];
    title(win,out+"/settings-first.png");
    dms_redraw(&menu);dms_draw(&menu);
    title(win,out+"/settings-reopen.png");
    dms_redraw(&menu);dms_draw(&menu);
    SDL_setenv("CURSOR_TEST_CONTROLS","1",1);title(win,out+"/controls-reopen.png");SDL_setenv("CURSOR_TEST_CONTROLS","",1);
    dms_redraw(&menu);dms_draw(&menu);
    assert(game_load_menu_open(win,"dossidebar.pack",(out+"/load-menu.png").c_str())==-1);
    dms_redraw(&menu);dms_draw(&menu);
    assert(game_notice_open(win,"dossidebar.pack","Cursor regression","This notice retains the original pointer.",(out+"/notice-menu.png").c_str())==0);
    for(int mode=0;mode<2;mode++) {
        char *args[]={(char*)"probe",(char*)"--scen",(char*)"SCG01EA",(char*)"--pack",(char*)"SCG01EA.pack",(char*)"--noshroud",(char*)"--nosound",(char*)"--enhanced",(char*)"--texset",(char*)"3"};
        std::vector<char*> missionArgs(args,args+10);for(int n=2;n<argc;n++)missionArgs.push_back(argv[n]);
        GameOpts o;assert(game_parse_args(int(missionArgs.size()),missionArgs.data(),&o)==0);assert(game_boot(win,&o));
        g_c3dDraw=mode!=0;g_ehqStep=1.f/60.f;refresh_objects();g_mouseScrC=600;g_mouseScrR=500;update_cursor(1280,720);
        for(int page:{DOPT_PAGE_OPTIONS,DOPT_PAGE_VISUALS,DOPT_PAGE_CONTROLS,DOPT_PAGE_SLOTS}) {
            opt_show(o.scen);g_optState.page=page;
            if(page==DOPT_PAGE_SLOTS)dopt_open_slots(&g_optState,g_dbPack,DOPT_SL_SAVE);
            game_draw(win);opt_draw(1280,720,600,500);draw_cursor_top(1280,720,true,float(g_optScale));
            assert(g_curLocked&&cur_type()==MOUSE_NORMAL);
            std::string file=out+"/ingame-cursor"+std::to_string(mode)+"-page"+std::to_string(page)+".png";assert(game_grab_png(file.c_str(),1280,720));
            g_optOpen=false;cur_unlock();
        }
        codex_open();update_cursor(1280,720);
        for(int visit=0;visit<2;visit++) {
            game_draw(win);assert(!g_cursorOnMap&&cur_type()==MOUSE_NORMAL);
            std::string file=out+"/database-cursor"+std::to_string(mode)+"-visit"+std::to_string(visit)+".png";assert(game_grab_png(file.c_str(),1280,720));
        }
        codex_close();
        game_shutdown();dms_redraw(&menu);dms_draw(&menu);
        title(win,out+"/after-mission-cursor"+std::to_string(mode)+".png");
        dms_redraw(&menu);dms_draw(&menu);SDL_setenv("CURSOR_TEST_CONTROLS","1",1);
        title(win,out+"/after-mission-controls"+std::to_string(mode)+".png");SDL_setenv("CURSOR_TEST_CONTROLS","",1);
    }
    dms_close(&menu);cur_free();sb_free();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();
    return 0;
}
