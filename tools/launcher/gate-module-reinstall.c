/* Real controller + journalled extractor, disposable native game-shaped packages. */
#define main launcher_entry
#include "../../launcher/launcher.c"
#undef main
#include <assert.h>
static int cancel_extract(void *user,int done,int total){(void)user;(void)total;return done<2;}
int main(int argc,char**argv){
 L_App a;LZ_Lock lock;char error[512],path[1400],backup[1400];int x,y,w,h,i;
 assert(argc==2);assert(!SDL_Init(SDL_INIT_TIMER));memset(&a,0,sizeof a);snprintf(a.dir,sizeof a.dir,"%s",argv[1]);a.mod[0].name="OpenCNC3D";a.mod[0].installed=1;
 assert(lz_lock(a.dir,&lock)==1);assert(lmod_cached(a.dir,1));l_find_ra_module(&a);assert(a.mod[1].name&&!a.mod[1].installed);
 l_set_view(&a,L_VIEW_BROWSE);assert(a.btn[L_BTN_PLAY].hidden&&a.btn[L_BTN_EDITOR].hidden&&!strcmp(a.btn[L_BTN_CLOSE].label,"Back"));
 l_browser_rect(&a,0,&x,&y,&w,&h);assert(l_mod_hit(&a,x+w/2,y+h/2)==-1);
 l_browser_rect(&a,1,&x,&y,&w,&h);assert(l_mod_hit(&a,x+w/2,y+h/2)==1);
 l_activate_module(&a,1);assert(a.view==L_VIEW_MODULE_OPTIONS);a.want_movies=a.want_music=0;l_activate(&a,L_BTN_PLAY);assert(a.view==L_VIEW_MODULE_RUN);
 for(i=0;i<5000&&(a.view==L_VIEW_MODULE_RUN||a.view==L_VIEW_MEDIA_RUN);i++){l_module_tick(&a);l_m_tick(&a);if(SDL_AtomicGet(&a.module_phase)==-1){fprintf(stderr,"%s\n",a.module_error);assert(0);}SDL_Delay(2);}
 assert(a.view==L_VIEW_MODULES&&a.mod[1].installed&&a.mod_sel==1&&lmod_ra_present(a.dir,NULL,0));
 l_set_view(&a,L_VIEW_BROWSE);l_browser_rect(&a,1,&x,&y,&w,&h);assert(l_mod_hit(&a,x+w/2,y+h/2)==-1&&a.hot==L_BTN_CLOSE);
 assert(lmod_remove(a.dir,1,error,sizeof error));assert(!lmod_ra_present(a.dir,NULL,0));db_pack_free(a.ra_pack);a.ra_pack=NULL;a.mod[1].name=NULL;a.mod[1].installed=0;
 l_find_ra_module(&a);assert(a.mod[1].name&&!a.mod[1].installed); /* restart catalog detection */
 assert(!lmod_install_cached(a.dir,1,LCFG_BUILD,cancel_extract,NULL,error,sizeof error));assert(!lmod_ra_present(a.dir,NULL,0));
 assert(lmod_install_cached(a.dir,1,LCFG_BUILD,NULL,NULL,error,sizeof error));assert(lmod_ra_present(a.dir,NULL,0));
 assert(lmod_remove(a.dir,1,error,sizeof error));
 /* A checksum mismatch must not touch any game path. */
 snprintf(path,sizeof path,"%s/.opencnc3d-modules/ra3d.zip.sha256",a.dir);snprintf(backup,sizeof backup,"%s.saved",path);assert(!rename(path,backup));
 {FILE*f=fopen(path,"wb");assert(f);fputs("0000000000000000000000000000000000000000000000000000000000000000\n",f);fclose(f);}
 assert(!lmod_install_cached(a.dir,1,LCFG_BUILD,NULL,NULL,error,sizeof error));assert(strstr(error,"checksum"));assert(!remove(path));assert(!rename(backup,path));
 /* Matching SHA alone is insufficient: injected save paths are rejected before extraction. */
 snprintf(path,sizeof path,"%s/.opencnc3d-modules/ra3d.zip",a.dir);snprintf(backup,sizeof backup,"%s.saved",path);assert(!rename(path,backup));
 {char foreign[1400];snprintf(foreign,sizeof foreign,"%s/foreign.zip",a.dir);assert(!rename(foreign,path));}
 {char sha[65],side[1400];FILE*f;assert(lz_sha256_file(path,sha,error,sizeof error));snprintf(side,sizeof side,"%s.sha256",path);f=fopen(side,"wb");assert(f);fprintf(f,"%s\n",sha);fclose(f);}
 assert(!lmod_install_cached(a.dir,1,LCFG_BUILD,NULL,NULL,error,sizeof error));assert(strstr(error,"unowned"));assert(!lmod_ra_present(a.dir,NULL,0));
 assert(l_has_named_file(a.dir,"savegame.sav")&&l_has_named_file(a.dir,"ra3d/missions/custom-map.ini")&&lmod_td_present(a.dir));
 l_free_components(&a);lz_unlock(&lock);SDL_Quit();puts("MODULE-REINSTALL|PASS|installed disabled, removed clickable after restart, controller install, remove/reinstall, cancellation rollback, checksum/ownership refusal, saves/settings/other game preserved");return 0;
}
