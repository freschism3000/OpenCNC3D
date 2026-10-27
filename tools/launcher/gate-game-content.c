/* Disposable range-server fixture: game ownership and the real settings controller. */
#define main launcher_entry
#include "../../launcher/launcher.c"
#undef main
#include <assert.h>
static void directories(const char *path){char buf[1600];char*p;snprintf(buf,sizeof buf,"%s",path);for(p=buf+1;*p;p++)if(*p=='/'){*p=0;mkdir(buf,0755);*p='/';}}
static void fixture(const char *dir,const char *rel){char p[1600];FILE*f;snprintf(p,sizeof p,"%s/%s",dir,rel);directories(p);f=fopen(p,"wb");assert(f);fputs("preserve",f);fclose(f);}
static void wait_job(LM_State*m){int n;for(n=0;n<3000;n++){LM_Phase p=lm_phase(m);if(p==LM_DONE)return;if(p==LM_FAILED||p==LM_CANCELLED){char why[512];lm_status(m,why,sizeof why);fprintf(stderr,"CONTENT|FAIL|%s\n",why);assert(0);}SDL_Delay(10);}assert(0);}
static void apply(LM_State*m,int movies,int music){assert(lm_apply(m,movies,music));wait_job(m);}
static void present(LM_State*m,int mo,int mu){int a,b;lm_installed(m,&a,&b);assert(a==mo&&b==mu);}
static const char *media_paths[]={"dosdata/movies/fixture.VQA","dosdata/SCORES.MIX","ra3d/movies/fixture.VQA","ra3d/audio/SCORES.MIX"};
static void media_hashes(const char *dir,char hashes[4][80]){int i;char path[1600],why[256];for(i=0;i<4;i++){snprintf(path,sizeof path,"%s/%s",dir,media_paths[i]);assert(lz_sha256_file(path,hashes[i],why,sizeof why));}}
static void media_exact(const char *dir,char hashes[4][80]){char now[4][80];int i;media_hashes(dir,now);for(i=0;i<4;i++)assert(!strcmp(now[i],hashes[i]));}
static void write_record(const char *dir,int game){char path[1600];FILE*f;snprintf(path,sizeof path,"%s/opencnc3d-media-%s.txt",dir,game?"ra":"td");f=fopen(path,"wb");assert(f);fputs("# Keep unchecked preference and state.\nmovies no\nmusic yes\nmovies_state pending\nmusic_state complete\n",f);fclose(f);}
static void read_record(const char *dir,int game,char text[512]){char path[1600];FILE*f;size_t n;snprintf(path,sizeof path,"%s/opencnc3d-media-%s.txt",dir,game?"ra":"td");f=fopen(path,"rb");assert(f);n=fread(text,1,511,f);assert(!ferror(f)&&feof(f));text[n]=0;fclose(f);}
static void add_only_choices(const char *dir,LM_State *td,LM_State *ra)
{
 int game,choice;char hashes[4][80],other[512],now[512],before[512];
 media_hashes(dir,hashes);
 for(game=0;game<2;game++)for(choice=0;choice<4;choice++){
  LM_State*m=game?ra:td;int mo,mu,mp,up;write_record(dir,game);read_record(dir,game,before);read_record(dir,!game,other);
  assert(lm_apply_add_only(m,choice&1,choice&2));wait_job(m);media_exact(dir,hashes);
  assert(lm_wanted(m,&mo,&mu,&mp,&up)&&mo==!!(choice&1)&&mu&&mp==!(choice&1)&&!up);
  read_record(dir,!game,now);assert(!strcmp(now,other));
  if(!choice){read_record(dir,game,now);assert(!strcmp(now,before));}
 }
}
static void add_only_interrupted(const char *dir,const char *manifest,int cancel)
{
 LM_State*m=lm_create_game(dir,manifest,1);char hashes[4][80],other[512],now[512],path[1600];int i,mo,mu,mp,up;LM_Phase p;
 snprintf(path,sizeof path,"%s/ra3d/movies/fixture.VQA",dir);{FILE*f=fopen(path,"wb");assert(f);fputs("EXISTING MEDIA MUST SURVIVE UNSUCCESSFUL ADDITION",f);fclose(f);}
 write_record(dir,1);read_record(dir,0,other);media_hashes(dir,hashes);assert(lm_apply_add_only(m,1,0));
 for(i=0;i<3000;i++){
  p=lm_phase(m);if(cancel&&p==LM_FETCHING)lm_cancel(m);
  if(p==LM_DONE||p==LM_FAILED||p==LM_CANCELLED)break;SDL_Delay(10);
 }
 assert(i<3000&&p==(cancel?LM_CANCELLED:LM_FAILED));media_exact(dir,hashes);read_record(dir,0,now);assert(!strcmp(other,now));
 assert(lm_wanted(m,&mo,&mu,&mp,&up)&&mo&&mu&&mp&&!up);lm_destroy(m);
}
int main(int argc,char**argv){
 L_App a;LM_State*td,*ra,*de,*fr,*tdde;LZ_Lock lock;int i,yes,no;long long size;char why[512];
 assert(argc==6);assert(!SDL_Init(SDL_INIT_TIMER));memset(&a,0,sizeof a);snprintf(a.dir,sizeof a.dir,"%s",argv[1]);
 fixture(a.dir,"opencnc3d-install.txt");fixture(a.dir,"TiberianDawn.dylib");fixture(a.dir,"opencnc3d");fixture(a.dir,"CONQUER.MIX");
 for(i=0;i<lmod_ra_nneeds;i++)fixture(a.dir,lmod_ra_needs[i]);
 fixture(a.dir,"savegame.sav");fixture(a.dir,"ra3d/lang/de/user-note.txt");assert(lz_lock(a.dir,&lock)==1);
 td=lm_create_game(a.dir,argv[2],0);ra=lm_create_game(a.dir,argv[2],1);
 de=lm_create_language(a.dir,1,"de",argv[3]);fr=lm_create_language(a.dir,1,"fr",argv[3]);tdde=lm_create_language(a.dir,0,"de",argv[3]);
 apply(td,1,0);present(td,1,0);present(ra,0,0);apply(ra,0,1);present(td,1,0);present(ra,0,1);
 apply(de,1,0);apply(fr,1,0);apply(tdde,1,0);present(de,1,0);present(fr,1,0);present(tdde,1,0);
 apply(de,0,0);present(de,0,0);present(fr,1,0);present(tdde,1,0);present(ra,0,1);present(td,1,0);
 apply(de,1,0);present(de,1,0);apply(ra,0,0);present(td,1,0);present(de,1,0);present(fr,1,0);apply(ra,1,1);present(ra,1,1);
 assert(lm_wanted(td,&yes,&no,NULL,NULL)&&yes&&!no);assert(lm_wanted(ra,&yes,&no,NULL,NULL)&&yes&&no);
 /* Reinstall choices add only; existing bytes and unchecked preferences survive
  * every combination, including a cancelled or checksum-failed addition. */
 apply(td,1,1);add_only_choices(a.dir,td,ra);add_only_interrupted(a.dir,argv[4],0);add_only_interrupted(a.dir,argv[5],1);apply(td,1,0);apply(ra,1,1);
 /* The shipped catalog must parse completely, including the localized MIX headers. */
 {LM_State*m=lm_create_language(a.dir,1,"de",NULL);lm_sizes(m,&size,NULL);lm_status(m,why,sizeof why);assert(!why[0]&&size>100000000);lm_destroy(m);}
 a.mod[0].name="OpenCNC3D";a.mod[1].name="RA3D";a.mod[0].installed=a.mod[1].installed=1;a.mod_sel=1;
 a.media=lm_create(a.dir,argv[2]);snprintf(a.media_manifest,sizeof a.media_manifest,"%s",argv[2]);l_set_buttons(&a);l_activate(&a,L_BTN_GEAR);assert(a.view==L_VIEW_SETTINGS);assert(a.language_count==2&&a.have_movies&&a.have_music);
 l_m_step(&a,1);assert(a.hot==L_BTN_CHK_MUSIC);l_m_step(&a,1);assert(a.hot==L_BTN_LANG0);l_m_step(&a,1);assert(a.hot==L_BTN_LANG1);
 for(i=0;i<2;i++)lm_destroy(a.language[i]);a.language[0]=de;a.language[1]=fr;l_language_refresh(&a);
 /* Unticking one pack asks first, Keep/Escape preserves all data. */
 l_activate(&a,L_BTN_LANG0);l_activate(&a,L_BTN_PLAY);assert(a.m_confirm&&a.hot==L_BTN_QUIT&&!a.m_running);l_activate(&a,L_BTN_QUIT);assert(!a.m_confirm);present(de,1,0);
 /* A mixed removal/download is sequenced by the actual controller. */
 l_activate(&a,L_BTN_CHK_MUSIC);l_activate(&a,L_BTN_PLAY);assert(a.m_confirm);l_activate(&a,L_BTN_PLAY);
 for(i=0;i<3000&&a.component_active;i++){l_m_tick(&a);SDL_Delay(10);}assert(!a.component_active&&a.view==L_VIEW_SETTINGS);
 present(a.settings_media,1,0);present(de,0,0);present(fr,1,0);present(tdde,1,0);present(td,1,0);
 l_activate(&a,L_BTN_LANG0);l_activate(&a,L_BTN_CHK_MUSIC);l_activate(&a,L_BTN_PLAY);
 for(i=0;i<3000&&a.component_active;i++){l_m_tick(&a);SDL_Delay(10);}assert(!a.component_active);present(a.settings_media,1,1);present(de,1,0);
 assert(l_has_named_file(a.dir,"savegame.sav")&&l_has_named_file(a.dir,"ra3d/lang/de/user-note.txt"));
 /* Switching the gear to Dawn uses its different media choice and pack catalog. */
 a.mod_sel=0;a.view=L_VIEW_MODULES;l_set_buttons(&a);l_activate(&a,L_BTN_GEAR);assert(a.language_count==1&&a.have_movies&&!a.have_music&&a.want_movies&&!a.want_music);
 l_free_components(&a);lm_destroy(td);lm_destroy(ra);lm_destroy(tdde);lz_unlock(&lock);SDL_Quit();
 puts("GAME-CONTENT|PASS|all8 add-only choices preserve media/unchecked preferences/other game, failed/cancelled additions preserve original bytes, Settings exact pack removal/re-download/mixed queue/confirmation/keyboard, other game/languages/saves preserved");return 0;
}
