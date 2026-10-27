/* CPU controller and disk transaction checks; disposable installation only. */
#define main launcher_entry
#include "../../launcher/launcher.c"
#undef main
#include <assert.h>
static void directories(const char *path){char buf[1400];char*p;snprintf(buf,sizeof buf,"%s",path);for(p=buf+1;*p;p++)if(*p=='/'){*p=0;mkdir(buf,0755);*p='/';}}
static void fixture(const char *dir,const char *rel){char path[1400];FILE*f;snprintf(path,sizeof path,"%s/%s",dir,rel);directories(path);f=fopen(path,"wb");assert(f);fputs("fixture",f);assert(!fclose(f));}
int main(int argc,char**argv){
 L_App a;LZ_Lock lock;char err[256],path[1400],other[1400];int i;
 assert(argc==2);memset(&a,0,sizeof a);snprintf(a.dir,sizeof a.dir,"%s",argv[1]);
 fixture(a.dir,"opencnc3d-install.txt");fixture(a.dir,"TiberianDawn.dylib");fixture(a.dir,"opencnc3d");fixture(a.dir,"CONQUER.MIX");
 assert(!lmod_remove(a.dir,0,err,sizeof err));assert(lmod_td_present(a.dir));assert(lz_lock(a.dir,&lock)==1);
 a.mod[0].name="OpenCNC3D";a.mod[0].installed=1;a.hot=L_BTN_PLAY;a.pressed=-1;a.mod_pressed=-1;
 l_set_buttons(&a);assert(!a.btn[L_BTN_REMOVE].hidden&&!a.btn[L_BTN_REMOVE].disabled);
 l_activate(&a,L_BTN_REMOVE+a.mod_sel);assert(a.view==L_VIEW_REMOVE&&a.remove_module==0&&a.hot==L_BTN_QUIT);
 for(i=0;i<L_BTN_COUNT;i++)assert(a.btn[i].hidden||i==L_BTN_PLAY||i==L_BTN_QUIT||i==L_BTN_WINDOW_CLOSE);
 {SDL_Event e;memset(&e,0,sizeof e);e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_ESCAPE;l_event(&a,&e);assert(a.view==L_VIEW_MODULES&&a.hot==L_BTN_PLAY&&lmod_td_present(a.dir));}
 l_activate(&a,L_BTN_REMOVE+a.mod_sel);l_activate(&a,L_BTN_QUIT);assert(a.view==L_VIEW_MODULES&&lmod_td_present(a.dir));
 /* A foreign staging file prevents every move. */
 fixture(a.dir,"CONQUER.MIX.module-removing");assert(!lmod_remove(a.dir,0,err,sizeof err));assert(lmod_td_present(a.dir));
 snprintf(path,sizeof path,"%s/CONQUER.MIX.module-removing",a.dir);assert(!remove(path));
 /* Recovery of a crash before commit restores the original exactly. */
 snprintf(path,sizeof path,"%s/TiberianDawn.dylib",a.dir);snprintf(other,sizeof other,"%s.module-removing",path);assert(!rename(path,other));
 {FILE*f;snprintf(path,sizeof path,"%s/opencnc3d-module-removal.txt",a.dir);directories(path);f=fopen(path,"wb");assert(f);fputs("0 0\n",f);fclose(f);}
 assert(lmod_settle(a.dir,err,sizeof err)&&lmod_td_present(a.dir));
 fixture(a.dir,"opencnc3d-fx.cfg");fixture(a.dir,"custom-map.ini");fixture(a.dir,"savegame.sav");
 l_activate(&a,L_BTN_REMOVE+a.mod_sel);l_activate(&a,L_BTN_PLAY);assert(a.view==L_VIEW_MODULES&&!a.mod[0].installed&&!lmod_td_present(a.dir));
 assert(a.btn[L_BTN_REMOVE].hidden&&a.btn[L_BTN_PLAY].disabled&&a.btn[L_BTN_EDITOR].disabled);
 assert(l_has_named_file(a.dir,"opencnc3d")&&l_has_named_file(a.dir,"opencnc3d-fx.cfg")&&l_has_named_file(a.dir,"custom-map.ini")&&l_has_named_file(a.dir,"savegame.sav"));
 /* Symlinks and linked directories fail closed without touching the target. */
#ifndef _WIN32
 snprintf(path,sizeof path,"%s/TiberianDawn.dylib",a.dir);assert(!symlink("savegame.sav",path));assert(!lmod_remove(a.dir,0,err,sizeof err));assert(l_has_named_file(a.dir,"savegame.sav"));assert(!unlink(path));
#endif
 /* An interrupted committed cleanup is completed, never rolled back. */
 fixture(a.dir,"TiberianDawn.dylib.module-removing");
 {FILE*f;snprintf(path,sizeof path,"%s/opencnc3d-module-removal.txt",a.dir);directories(path);f=fopen(path,"wb");assert(f);fputs("0 1\n",f);fclose(f);}
 assert(lmod_settle(a.dir,err,sizeof err)&&!l_has_named_file(a.dir,"TiberianDawn.dylib.module-removing"));
 assert(lmod_remove(a.dir,0,err,sizeof err));
 fixture(a.dir,"TiberianDawn.dylib");a.mod[0].installed=1;
 for(i=0;i<lmod_ra_nneeds;i++)fixture(a.dir,lmod_ra_needs[i]);
 fixture(a.dir,"ra3d/missions/custom-map.ini");fixture(a.dir,"ra3d/savegame.sav");fixture(a.dir,"ra3d/ra3d-fx.cfg");fixture(a.dir,"ra3d/audio/SCORES.MIX");
 assert(lmod_ra_present(a.dir,err,sizeof err));a.mod[1].installed=1;a.mod[1].name="RA3D";a.mod_sel=1;l_set_buttons(&a);
 l_activate_module(&a,2);assert(a.view==L_VIEW_BROWSE&&a.mod[0].installed&&a.mod[1].installed&&!a.btn[L_BTN_CLOSE].hidden);l_activate(&a,L_BTN_CLOSE);assert(a.view==L_VIEW_MODULES);
 l_activate(&a,L_BTN_REMOVE);assert(a.remove_module==0);l_activate(&a,L_BTN_QUIT);assert(a.mod_sel==1&&lmod_td_present(a.dir));
 l_activate(&a,L_BTN_REMOVE+a.mod_sel);assert(a.remove_module==1);l_activate(&a,L_BTN_QUIT);assert(lmod_ra_present(a.dir,err,sizeof err));
 l_activate(&a,L_BTN_REMOVE+a.mod_sel);l_activate(&a,L_BTN_PLAY);assert(!lmod_ra_present(a.dir,err,sizeof err)&&lmod_td_present(a.dir));assert(a.mod_sel==0&&a.mod[0].installed&&!a.mod[1].installed);
 assert(l_has_named_file(a.dir,"ra3d/missions/custom-map.ini")&&l_has_named_file(a.dir,"ra3d/savegame.sav")&&l_has_named_file(a.dir,"ra3d/ra3d-fx.cfg")&&l_has_named_file(a.dir,"ra3d/audio/SCORES.MIX"));
 assert(lmod_remove(a.dir,1,err,sizeof err));lz_unlock(&lock);
 puts("MODULE-REMOVAL|PASS|controller, Keep/default, owned removal, empty launch disabled, preservation, lock, staging collision, symlink refusal, rollback, committed recovery, idempotence");return 0;
}
