/* Native browser/controller + real loopback HTTP + journalled ZIP extraction. */
#define main launcher_entry
#include "../../launcher/launcher.c"
#undef main
#include <assert.h>
static int cancel_after_entry(void *user,int done,int total)
{(void)user;return total>0&&done*1000/total<740;}
static void confirm_options(L_App *a,int movies,int music)
{
    assert(a->view==L_VIEW_MODULE_OPTIONS);
    assert(!a->btn[L_BTN_CHK_MOVIES].hidden&&!a->btn[L_BTN_CHK_MUSIC].hidden);
    a->want_movies=movies;a->want_music=music;l_set_buttons(a);l_activate(a,L_BTN_PLAY);
    if(a->m_confirm)l_activate(a,L_BTN_PLAY);
    assert(a->view==L_VIEW_MODULE_RUN);
}
static void wait_module(L_App *a)
{int i;for(i=0;i<10000&&(a->view==L_VIEW_MODULE_RUN||a->view==L_VIEW_MEDIA_RUN);i++){l_module_tick(a);l_m_tick(a);if(SDL_AtomicGet(&a->module_phase)==-1){fprintf(stderr,"REMOTE ERROR: %s\n",a->module_error);assert(0);}SDL_Delay(1);}assert(a->view==L_VIEW_MODULES);}
static void catalog_refusals(const char *dir)
{
    char path[1400],*text;FILE *f;long n;LMOD_Catalog c;char err[256];
    const char *catalog="modules 1\nversion " LCFG_BUILD "\nmodule ra macos OpenCNC3D-module-ra-macos-v" LCFG_BUILD ".zip 100 0000000000000000000000000000000000000000000000000000000000000000\n";
    snprintf(path,sizeof path,"%s/builds.json",dir);f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);n=ftell(f);rewind(f);text=(char *)malloc(n+1);assert(text);assert(fread(text,1,n,f)==(size_t)n);text[n]=0;fclose(f);
    assert(!lmod_catalog_read("wrong-build",text,catalog,&c,err,sizeof err));
    assert(!lmod_catalog_read(LCFG_BUILD,text,catalog,&c,err,sizeof err)); /* asset size mismatch */
    assert(!lmod_catalog_read(LCFG_BUILD,text,"modules 1\nversion " LCFG_BUILD "\nmodule ra windows forged.zip 1 nope\n",&c,err,sizeof err));
    free(text);
}
static void browser_shot(const char *dir,const char *site,const char *filename)
{
    char out[1400];char *args[12];snprintf(out,sizeof out,"%s/%s",dir,filename);
    args[0]="module-gate";args[1]="--dir";args[2]=(char *)dir;args[3]="--shot";args[4]=out;
    args[5]="--view";args[6]="browser";args[7]="--module-site";args[8]=(char *)site;
    args[9]="--hidden-ui-test";args[10]=(char *)dir;args[11]=NULL;
    assert(launcher_entry(11,args)==0);
}
static void options_shot(const char *dir,const char *site,const char *media,int target)
{
    char out[1400];char *args[16];snprintf(out,sizeof out,"%s/%s-options.png",dir,target?"ra":"td");
    args[0]="module-gate";args[1]="--dir";args[2]=(char *)dir;args[3]="--shot";args[4]=out;
    args[5]="--view";args[6]="module-options";args[7]="--module-site";args[8]=(char *)site;
    args[9]="--hidden-ui-test";args[10]=(char *)dir;args[11]="--module";args[12]=target?"ra3d":"opencnc3d";
    args[13]="--media-manifest";args[14]=(char *)media;args[15]=NULL;assert(launcher_entry(15,args)==0);
}
static void installer_cli(const char *dir,const char *zip,const char *sha,const char *media)
{
    char offline[1400],progress[1400];char *args[14];int i;
    snprintf(offline,sizeof offline,"%s/offline-install",dir);assert(!mkdir(offline,0755));
    snprintf(progress,sizeof progress,"%s/progress.txt",offline);
    args[0]="module-gate";args[1]="--dir";args[2]=offline;args[3]="--install-module";args[4]="ra3d";
    args[5]="--module-file";args[6]=(char *)zip;args[7]="--module-sha";args[8]=(char *)sha;
    args[9]="--progress-file";args[10]=progress;args[11]=NULL;assert(launcher_entry(11,args)==0);
    assert(lmod_ra_present(offline,NULL,0)&&lmod_module_current(offline,1,LCFG_BUILD));
    args[3]="--media-install";args[4]="none";args[5]="--media-game";args[6]="ra";
    args[7]="--media-manifest";args[8]=(char *)media;args[11]="--media-add-only";args[12]=NULL;assert(launcher_entry(12,args)==0);
    {LM_State*m=lm_create_game(offline,media,1);int mo,mu;assert(lm_wanted(m,&mo,&mu,NULL,NULL)&&!mo&&!mu);lm_destroy(m);}
    assert(!l_has_named_file(offline,"opencnc3d-media-td.txt"));
    args[6]="td";i=launcher_entry(12,args);assert(i!=0&&!l_has_named_file(offline,"opencnc3d-media-td.txt"));
}

/* Simulate a committed removal whose Enhanced file is temporarily blocked.
 * A directory obstruction covers the same failure branch as Windows' file lock. */
static void committed_remove_retry(const char *dir)
{
    char record[1400],staged_record[1440],path[1400],staged[1440],blocked[1440],saved[1480],line[1400],rel[1024],sha[80],err[512];
    long long size;FILE *f;
    snprintf(record,sizeof record,"%s/opencnc3d-module-ra.txt",dir);f=fopen(record,"rb");assert(f);
    while(fgets(line,sizeof line,f))if(sscanf(line,"file %79s %lld %1023s",sha,&size,rel)==3&&!strncmp(rel,"ra3d/",5)){
        snprintf(path,sizeof path,"%s/%s",dir,rel);snprintf(staged,sizeof staged,"%s.module-removing",path);assert(!rename(path,staged));
    }
    fclose(f);
    snprintf(staged_record,sizeof staged_record,"%s.module-removing",record);assert(!rename(record,staged_record));
    snprintf(blocked,sizeof blocked,"%s/ra3d/einf/E1.c3m.module-removing",dir);snprintf(saved,sizeof saved,"%s.blocked-test",blocked);assert(!rename(blocked,saved));
    assert(!mkdir(blocked,0755));
    snprintf(path,sizeof path,"%s/opencnc3d-module-removal.txt",dir);f=fopen(path,"wb");assert(f);fputs("1 1\n",f);fclose(f);
    assert(!lmod_settle(dir,err,sizeof err));assert(l_has_named_file(dir,"opencnc3d-module-ra.txt.module-removing"));
    assert(!rmdir(blocked));assert(!rename(saved,blocked));assert(lmod_settle(dir,err,sizeof err));
    assert(!l_has_named_file(dir,"opencnc3d-module-ra.txt.module-removing"));assert(!l_has_named_file(dir,"ra3d/einf/E1.c3m.module-removing"));
}
static void must_fail(const char *dir,int target,const LMOD_Catalog *cat,const char *why)
{char err[512];assert(!lmod_install_remote(dir,target,LCFG_BUILD,cat,NULL,NULL,err,sizeof err));if(!strstr(err,why)){fprintf(stderr,"expected '%s', got '%s'\n",why,err);assert(0);}}
int main(int argc,char **argv)
{
 L_App a;LMOD_Catalog cat,bad;LZ_Lock lock;char err[512],path[1400],dst[1400];int x,y,w,h;
 assert(argc==4);catalog_refusals(argv[1]);browser_shot(argv[1],argv[2],"public-browser.png");assert(!SDL_Init(SDL_INIT_TIMER));memset(&a,0,sizeof a);snprintf(a.dir,sizeof a.dir,"%s",argv[1]);a.mod[0].name="Dawn3D";snprintf(a.media_manifest,sizeof a.media_manifest,"%s",argv[3]);l_find_ra_module(&a);
 assert(!lz_is_install(a.dir)&&!lmod_cached(a.dir,1));assert(lmod_catalog_fetch_at(argv[2],LCFG_BUILD,&cat,err,sizeof err));
 options_shot(a.dir,argv[2],argv[3],1);options_shot(a.dir,argv[2],argv[3],0);
 assert(cat.module[0].available&&cat.module[1].available);a.catalog=cat;SDL_AtomicSet(&a.catalog_phase,2);
 l_set_view(&a,L_VIEW_BROWSE);l_browser_rect(&a,1,&x,&y,&w,&h);assert(l_mod_hit(&a,x+w/2,y+h/2)==1);
 l_activate_module(&a,1);assert(a.view==L_VIEW_MODULE_OPTIONS&&a.want_movies&&a.want_music);
 l_activate(&a,L_BTN_QUIT);assert(a.view==L_VIEW_BROWSE&&!lmod_cached(a.dir,1)&&!lz_is_install(a.dir));
 l_activate_module(&a,1);confirm_options(&a,1,0);wait_module(&a);
 assert(l_has_named_file(a.dir,"ra3d/movies/fixture.VQA")&&!l_has_named_file(a.dir,"dosdata/movies/fixture.VQA"));
 {int mo,mu;assert(lm_wanted(a.media,&mo,&mu,NULL,NULL)&&mo&&!mu);}
 assert(a.mod[1].installed&&lmod_ra_present(a.dir,NULL,0)&&lz_is_install(a.dir));
 assert(l_has_named_file(a.dir,"ra3d/einf/E1.c3m")&&l_has_named_file(a.dir,"efx.pack")&&l_has_named_file(a.dir,"opencnc3d-module-ra.txt"));
 snprintf(path,sizeof path,"%s/.opencnc3d-modules/ra3d.zip",a.dir);installer_cli(a.dir,path,cat.module[1].sha,argv[3]);
 a.module_after_media=1;a.module_movies=a.module_music=1;SDL_AtomicSet(&a.module_phase,2);SDL_AtomicSet(&a.module_cancel,1);a.view=L_VIEW_MODULE_RUN;
 l_module_tick(&a);assert(a.view==L_VIEW_MODULES&&!a.m_running&&!a.module_after_media);
 {int mo,mu;assert(lm_wanted(a.media,&mo,&mu,NULL,NULL)&&mo&&!mu);}
 assert(lmod_module_current(a.dir,1,LCFG_BUILD));l_module_versions(&a);assert(!a.mod[1].needs_update);
 /* A known older or legacy RA package gets an explicit Update card, and the
  * same transaction replaces it while keeping current settings and saves. */
 snprintf(path,sizeof path,"%s/opencnc3d-module-ra.txt",a.dir);
 {FILE*f=fopen(path,"rb");char *s,*v,*tail;long n;assert(f);fseek(f,0,SEEK_END);n=ftell(f);rewind(f);s=(char *)malloc(n+1);assert(s);assert(fread(s,1,n,f)==(size_t)n);s[n]=0;fclose(f);v=strstr(s,"version ");assert(v);tail=strchr(v,'\n');assert(tail);f=fopen(path,"wb");assert(f);fwrite(s,1,(size_t)(v-s),f);fputs("version 0.0.1",f);fputs(tail,f);fclose(f);free(s);}
 snprintf(path,sizeof path,"%s/ra3d/RedAlert.dylib",a.dir);{FILE*f=fopen(path,"wb");assert(f);fputs("OLD BRAIN EXACT",f);fclose(f);}
 assert(lz_lock(a.dir,&lock)==1);assert(!lmod_install_cached(a.dir,1,LCFG_BUILD,cancel_after_entry,NULL,err,sizeof err));
 {char old[32];FILE*f=fopen(path,"rb");assert(f);assert(fread(old,1,15,f)==15);assert(!memcmp(old,"OLD BRAIN EXACT",15));fclose(f);}
 assert(!lmod_module_current(a.dir,1,LCFG_BUILD));lz_unlock(&lock);
 l_module_versions(&a);assert(a.mod[1].needs_update);browser_shot(argv[1],argv[2],"update-browser.png");l_set_view(&a,L_VIEW_BROWSE);l_browser_rect(&a,1,&x,&y,&w,&h);assert(l_mod_hit(&a,x+w/2,y+h/2)==1);
 l_set_view(&a,L_VIEW_MODULES);a.mod_sel=1;l_set_buttons(&a);assert(!strcmp(a.btn[L_BTN_PLAY].label,"Update"));l_activate(&a,L_BTN_PLAY);assert(a.want_movies&&!a.want_music);confirm_options(&a,0,0);assert(a.media_add_only&&!a.m_confirm);wait_module(&a);assert(lmod_module_current(a.dir,1,LCFG_BUILD)&&!a.mod[1].needs_update);
 {int mo,mu;assert(lm_wanted(a.media,&mo,&mu,NULL,NULL)&&mo&&!mu);assert(l_has_named_file(a.dir,"ra3d/movies/fixture.VQA"));assert(!strstr(a.mstatus,"removed"));}
 assert(lz_lock(a.dir,&lock)==1);l_set_view(&a,L_VIEW_BROWSE);assert(l_mod_hit(&a,x+w/2,y+h/2)==-1);
 committed_remove_retry(a.dir);assert(lmod_install_cached(a.dir,1,LCFG_BUILD,NULL,NULL,err,sizeof err));
 assert(lmod_remove(a.dir,1,err,sizeof err));assert(!lmod_ra_present(a.dir,NULL,0));
 assert(!l_has_named_file(a.dir,"ra3d/einf/E1.c3m")&&l_has_named_file(a.dir,"efx.pack")&&l_has_named_file(a.dir,"einf/SHARED.c3m"));
 assert(!lmod_install_cached(a.dir,1,LCFG_BUILD,cancel_after_entry,NULL,err,sizeof err));
 assert(!l_has_named_file(a.dir,"ra3d/opencnc3d")&&!l_has_named_file(a.dir,"opencnc3d-module-ra.txt"));
 assert(lmod_install_cached(a.dir,1,LCFG_BUILD,NULL,NULL,err,sizeof err));
 /* Removal recovery still knows the manifest when that record has moved aside. */
 snprintf(path,sizeof path,"%s/opencnc3d-module-ra.txt",a.dir);snprintf(dst,sizeof dst,"%s.module-removing",path);assert(!rename(path,dst));
 snprintf(path,sizeof path,"%s/opencnc3d-module-removal.txt",a.dir);{FILE*f=fopen(path,"wb");assert(f);fputs("1 0\n",f);fclose(f);}
 assert(lmod_settle(a.dir,err,sizeof err));assert(l_has_named_file(a.dir,"opencnc3d-module-ra.txt"));assert(lmod_remove(a.dir,1,err,sizeof err));
 bad=cat;memset(bad.module[1].sha,'0',64);bad.module[1].sha[64]=0;must_fail(a.dir,1,&bad,"checksum");
 bad=cat;snprintf(bad.version,sizeof bad.version,"wrong-version");must_fail(a.dir,1,&bad,"build");
 /* Server serves a validly hashed package with an injected CFG manifest path. */
 bad=cat;bad.module[1].id=8;snprintf(path,sizeof path,"%s/bad-meta.txt",a.dir);
 {FILE*f=fopen(path,"rb");assert(f);assert(fscanf(f,"%lld %64s",&bad.module[1].size,bad.module[1].sha)==2);fclose(f);}
 must_fail(a.dir,1,&bad,"protected player data");assert(!lmod_ra_present(a.dir,NULL,0));
 bad=cat;bad.module[1].id=9;snprintf(path,sizeof path,"%s/phantom-meta.txt",a.dir);
 {FILE*f=fopen(path,"rb");assert(f);assert(fscanf(f,"%lld %64s",&bad.module[1].size,bad.module[1].sha)==2);fclose(f);}
 must_fail(a.dir,1,&bad,"missing");assert(!l_has_named_file(a.dir,"opencnc3d-module-ra.txt"));
 /* Install TD through the same native worker; shared RA dependencies remain. */
 a.catalog=cat;a.mod[0].installed=0;l_activate_module(&a,0);confirm_options(&a,0,1);wait_module(&a);assert(lmod_td_present(a.dir));
 assert(l_has_named_file(a.dir,"dosdata/SCORES.MIX")&&!l_has_named_file(a.dir,"dosdata/movies/fixture.VQA")&&l_has_named_file(a.dir,"ra3d/movies/fixture.VQA"));
 {int mo,mu;assert(lm_wanted(a.media,&mo,&mu,NULL,NULL)&&!mo&&mu);}
 assert(lmod_remove(a.dir,0,err,sizeof err));assert(!lmod_td_present(a.dir));assert(l_has_named_file(a.dir,"einf/SHARED.c3m"));
 assert(l_has_named_file(a.dir,"savegame.sav")&&l_has_named_file(a.dir,"ra3d-fx.cfg")&&l_has_named_file(a.dir,"ra3d/missions/custom-map.ini"));
 l_free_components(&a);lz_unlock(&lock);db_pack_free(a.ra_pack);SDL_Quit();puts("MODULE-REMOTE|PASS|exact-build catalog, real ranged download, native browser/controller, empty bootstrap, whole-ZIP SHA, complete Enhanced subtrees, atomic record, protected files, shared retention, rollback, restart recovery, private restore, outdated Update, phantom ownership refusal, committed Enhanced removal retry, pre-install per-game Movies/Music choice, cancel before install, scoped media/records, installer offline SHA + game-scoped CLI, RA+TD");return 0;
}
