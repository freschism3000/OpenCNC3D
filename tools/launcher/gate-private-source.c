/* Actual backend and setup CLI with counting transport stubs: no HTTP/window. */
#define SDL_MAIN_HANDLED
#define main launcher_entry
#include "../../launcher/launcher.c"
#undef main
#include "../../launcher/lupdate.c"
#include "../../launcher/lsetup_cli.h"
#include <assert.h>

static int network_calls;
char *ln_get_text(const char *url,const char *key,long cap,char *err,int len)
{(void)url;(void)key;(void)cap;network_calls++;snprintf(err,(size_t)len,"fixture transport reached");return NULL;}
char *ln_get_text_reach(const char *url,const char *key,long cap,int *unreached,char *err,int len)
{if(unreached)*unreached=1;return ln_get_text(url,key,cap,err,len);}
char *ln_post_text(const char *url,const char *key,const char *body,const char *type,long cap,char *err,int len)
{(void)body;(void)type;return ln_get_text(url,key,cap,err,len);}
int ln_get_file(const char *url,const char *key,const char *path,LN_Progress cb,void *user,char *err,int len)
{(void)path;(void)cb;(void)user;free(ln_get_text(url,key,0,err,len));return 0;}
int ln_get_range(const char *url,const char *key,const char *path,long long off,long long count,
                 int stall,volatile int *cancel,LN_Progress cb,void *user,long *status,char *err,int len)
{(void)path;(void)off;(void)count;(void)stall;(void)cancel;(void)cb;(void)user;(void)status;free(ln_get_text(url,key,0,err,len));return LN_RANGE_NOCONNECT;}
long long ln_file_size(const char *path)
{struct stat st;return stat(path,&st)?-1:(long long)st.st_size;}

static void private_error(int okay,const char *err)
{assert(!okay);assert(strstr(err,"private source build"));assert(network_calls==0);}

static void runtime_checks(const char *dir,int is_private)
{
    LMOD_Catalog cat;LU_State state;char err[512],path[1600],hex[65];int i,handled,rc;
    char *argv[18];
    memset(&cat,0,sizeof cat);snprintf(cat.version,sizeof cat.version,"%s",LCFG_BUILD);
    snprintf(cat.site,sizeof cat.site,"http://127.0.0.1:9");
    for(i=0;i<2;i++){cat.module[i].available=1;cat.module[i].id=i+1;cat.module[i].size=100;memset(cat.module[i].sha,'0',64);cat.module[i].sha[64]=0;}
    memset(hex,'0',64);hex[64]=0;snprintf(path,sizeof path,"%s/missing-package.zip",dir);
    if(!is_private){
        assert(!lmod_install_remote(dir,1,LCFG_BUILD,&cat,NULL,NULL,err,sizeof err));
        assert(network_calls==1&&!strstr(err,"private source build"));
        puts("PRIVATE-SOURCE|PUBLIC-CONTROL|runtime_transport_reached=1");return;
    }
    for(i=0;i<2;i++){
        private_error(lmod_install_cached(dir,i,LCFG_BUILD,NULL,NULL,err,sizeof err),err);
        private_error(lmod_install_file(dir,i,LCFG_BUILD,path,hex,NULL,NULL,err,sizeof err),err);
        private_error(lmod_install_remote(dir,i,LCFG_BUILD,&cat,NULL,NULL,err,sizeof err),err);
    }
    for(i=0;i<2;i++){
        memset(&state,0,sizeof state);snprintf(state.dir,sizeof state.dir,"%s",dir);state.fresh=i;
        private_error(lu_do_apply(&state,err,sizeof err),err);
        assert(!state.locked);
    }
    argv[0]="private-gate";argv[1]="--dir";argv[2]=(char*)dir;argv[3]="--install-module";argv[4]="ra3d";
    argv[5]="--module-site";argv[6]="http://127.0.0.1:9";argv[7]="--cancel-file";
    snprintf(path,sizeof path,"%s/cancel-sentinel",dir);argv[8]=path;argv[9]=NULL;
    handled=0;rc=lsetup_cli_main(9,argv,&handled);assert(handled&&rc==1&&network_calls==0);
    argv[5]="--module-file";argv[6]="missing.zip";argv[7]="--module-sha";argv[8]=hex;
    handled=0;rc=lsetup_cli_main(9,argv,&handled);assert(handled&&rc==1&&network_calls==0);
    argv[3]="--install-game";argv[4]="--media";argv[5]="movies,music";argv[6]="--cancel-file";
    snprintf(path,sizeof path,"%s/cancel-sentinel",dir);argv[7]=path;argv[8]=NULL;
    handled=0;rc=lsetup_cli_main(8,argv,&handled);assert(handled&&rc==1&&network_calls==0);
    puts("PRIVATE-SOURCE|PRIVATE-CONTROL|cached_file_remote_TD_RA_refused=1|fresh_and_update_refused=1|CLI_before_catalog=1|HTTP=0");
}

static void private_ui(const char *dir)
{
    L_App a;LU_State state;int i;
    memset(&a,0,sizeof a);memset(&state,0,sizeof state);
    state.lock=SDL_CreateMutex();assert(state.lock);state.phase=LU_AVAILABLE;
    a.up=&state;a.private_source=lz_is_private_developer(dir);assert(a.private_source);
    snprintf(a.dir,sizeof a.dir,"%s",dir);a.view=L_VIEW_MODULES;
    a.mod[0].name="Dawn3D";a.mod[1].name="RA3D";
    for(i=0;i<2;i++){a.mod[i].installed=1;a.mod[i].needs_update=1;a.catalog.module[i].available=1;}
    SDL_AtomicSet(&a.catalog_phase,2);l_module_versions(&a);
    for(i=0;i<2;i++){
        assert(!a.mod[i].needs_update&&!l_module_available(&a,i));
        a.mod_sel=i;a.play_after=a.quit=0;l_set_buttons(&a);
        assert(!strcmp(a.btn[L_BTN_PLAY].label,"Launch")&&!a.btn[L_BTN_PLAY].disabled);
        l_activate(&a,L_BTN_PLAY);assert(a.play_after==1&&a.quit&&a.view==L_VIEW_MODULES);
        assert(state.phase==LU_AVAILABLE&&network_calls==0);
    }
    a.catalog_allowed=1;SDL_AtomicSet(&a.catalog_phase,0);l_catalog_start(&a);
    assert(SDL_AtomicGet(&a.catalog_phase)==0&&network_calls==0);
    SDL_DestroyMutex(state.lock);
    puts("PRIVATE-SOURCE|UI-CONTROL|both_Launch_enabled_and_activated=1|global_available_not_applied=1|module_mismatch_not_actionable=1|catalog_skipped=1|HTTP=0");
}

static void media_check(const char *dir,const char *manifest)
{
    char *argv[]={"private-gate","--dir",(char*)dir,"--media-install","movies,music",
        "--media-game","ra","--media-add-only","--media-manifest",(char*)manifest,NULL};
    int handled=0;assert(lsetup_cli_main(10,argv,&handled)==0&&handled&&network_calls==0);
    puts("PRIVATE-SOURCE|MEDIA-CONTROL|private_optional_media_allowed=1|HTTP=0");
}

static void recovery_check(const char *dir)
{
    LZ_Lock lock;char err[512];
    assert(lz_is_private_developer(dir)&&lz_lock(dir,&lock)==1);
    assert(lmod_settle(dir,err,sizeof err)&&network_calls==0);
    lz_unlock(&lock);
    puts("PRIVATE-SOURCE|RECOVERY-CONTROL|interrupted_module_staging_restored=1|HTTP=0");
}

int main(int argc,char **argv)
{
    assert(argc>=3);assert(!SDL_Init(SDL_INIT_TIMER));
    if(!strcmp(argv[2],"predicate")){assert(argc==4);assert(lz_is_private_developer(argv[1])==atoi(argv[3]));puts("PRIVATE-SOURCE|PREDICATE|PASS");}
    else if(!strcmp(argv[2],"private")){runtime_checks(argv[1],1);private_ui(argv[1]);}
    else if(!strcmp(argv[2],"public"))runtime_checks(argv[1],0);
    else if(!strcmp(argv[2],"recovery"))recovery_check(argv[1]);
    else {assert(!strcmp(argv[2],"media")&&argc==4);media_check(argv[1],argv[3]);}
    SDL_Quit();return 0;
}
