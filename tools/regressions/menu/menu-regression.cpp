// Private source-instrumented regression; no hooks are shipped with the game.
#define OPENCNC3D_NO_MAIN 1
#include "cnc_eyes.cpp"
#include "../app/campaign.h"
static int menu_regression_tally(Camp *, void *, int);
static void menu_regression_number(int, int, const char *, const unsigned char *);
#include "../app/campaign.c"
#include <cassert>
extern "C" {
#include "../video/pngwrite.h"
}
#include <set>
#include <fstream>

static int failures=0,checks=0,scoreCases=0;
static std::string output,scoreName;
static void check(bool good,const char *name){checks++;printf("MENU_CHECK|%s|%s\n",good?"PASS":"FAIL",name);if(!good)failures++;}
static void legacy_click(SK_State *st,int item){int x,y,w,h;check(sk_item_rect(st,item,&x,&y,&w,&h)!=0,"legacy control rectangle exists");sk_press(st,x+w/2,y+h/2);sk_release(st,x+w/2,y+h/2);}
static void enhanced_click(SK_State *st,const DB_Pack *p,int item,int width,int height){
 em_lobby(st,p);em_viewport(width,height);int x,y,w,h;check(em_item_rect(item,&x,&y,&w,&h)!=0,"Enhanced control rectangle exists");
 int lx,ly;em_to_legacy(x+w/2,y+h/2,&lx,&ly);em_sk_press(st,lx,ly);em_sk_release(st,lx,ly);
}
static SK_Map maps[]={ {"SCM01EA","Eight start regression","TEMPERATE",62,62,8,0}, {"SCM02EA","Two start regression","TEMPERATE",62,62,2,0} };
static void skirmish_checks(const DB_Pack *p,int width,int height){
 em_reset(); /* each screen entry follows main-menu ownership in normal play */
 SK_State st;SK_Lobby result;sk_init(&st,maps,2,NULL);sk_result(&st,&result);
 check(result.ai_count==7,"historical map-filled default preserved");st.popup=7;st.start[7]=7;
 check(!sk_item_disabled(&st,SK_I_POP_OPEN),"offline Empty can be selected");
 enhanced_click(&st,p,-10,width,height);em_lobby(&st,p);em_viewport(width,height);
 check(em_write_shot((output+"/empty-dropdown-"+std::to_string(width)+".png").c_str(),width,height)!=0,"capture native Empty dropdown");
 enhanced_click(&st,p,SK_I_POP_OPEN,width,height);sk_result(&st,&result);
 check(sk_row_mode(&st,7)==SK_SEAT_OPEN&&result.mode[7]==SK_SEAT_OPEN&&result.ai_count==6,"Enhanced Empty removes exactly one opponent");
 em_lobby(&st,p);em_viewport(width,height);check(em_write_shot((output+"/empty-selected-"+std::to_string(width)+".png").c_str(),width,height)!=0,"capture selected Empty state");
 check(sk_row_start(&st,7)==-1,"Empty releases selected start");char name[32];sk_seat_name(&st,7,name,sizeof name);check(!strcmp(name,"EMPTY"),"empty roster labels agree");
 check(sk_item_disabled(&st,SK_I_POP_S8)!=0,"empty offline seat cannot reserve a start");
 enhanced_click(&st,p,-10,width,height);enhanced_click(&st,p,SK_I_POP_BOT,width,height);sk_result(&st,&result);check(result.ai_count==7,"Empty can become Computer again");
 for(int seat=2;seat<8;seat++){st.popup=seat;legacy_click(&st,seat%2?SK_I_POP_OPEN:SK_I_POP_BLOCK);}sk_result(&st,&result);
 check(result.ai_count==1,"mixed Empty and Blocked leaves one opponent");st.popup=1;
 enhanced_click(&st,p,-10,width,height);em_lobby(&st,p);em_viewport(width,height);check(em_write_shot((output+"/last-opponent-"+std::to_string(width)+".png").c_str(),width,height)!=0,"capture last-opponent protection");
 check(sk_item_disabled(&st,SK_I_POP_OPEN)&&sk_item_disabled(&st,SK_I_POP_BLOCK),"last opponent cannot be removed by either choice");
 check(!sk_item_disabled(&st,SK_I_PLAY),"one-opponent match can start");st.popup=7;
 check(!sk_item_disabled(&st,SK_I_POP_BLOCK)&&!sk_item_disabled(&st,SK_I_POP_OPEN),"unused modes remain selectable at minimum roster");
 check(!sk_item_disabled(&st,SK_I_POP_BOT),"unused seat can be restored at minimum roster");
 st.mode[1]=SK_SEAT_OPEN;check(sk_item_disabled(&st,SK_I_PLAY),"malformed no-opponent roster cannot start");
 sk_init(&st,maps,2,NULL);st.net=1;st.net_taken[0]=1;st.mode[1]=SK_SEAT_OPEN;st.popup=1;st.start[1]=1;
 check(!sk_item_disabled(&st,SK_I_POP_OPEN),"host Empty remains joinable");legacy_click(&st,SK_I_POP_OPEN);
 check(st.start[1]==1&&sk_row_mode(&st,1)==SK_SEAT_OPEN,"network Empty preserves reserved start");
 st.net=2;check(sk_item_disabled(&st,SK_I_POP_BOT)&&sk_item_disabled(&st,SK_I_POP_OPEN)&&sk_item_disabled(&st,SK_I_POP_BLOCK),"joiner cannot change seat modes");
 st.net=1;st.net_taken[1]=1;check(sk_item_disabled(&st,SK_I_POP_OPEN),"occupied human cannot be replaced by Empty");
 sk_init(&st,maps,2,NULL);st.popup=0;check(sk_item_disabled(&st,SK_I_POP_OPEN),"local human cannot be removed");
 sk_init(&st,maps,2,NULL);st.net_seats=2;st.net=1;st.mode[1]=SK_SEAT_OPEN;st.popup=1;
 check(!sk_item_disabled(&st,SK_I_POP_OPEN)&&sk_item_disabled(&st,SK_I_POP_BLOCK),"two-seat host preserves joinable capacity");
 check(sk_item_disabled(&st,SK_I_PLAY),"empty network room cannot start a one-person match");st.net_taken[1]=st.net_ready[1]=1;check(!sk_item_disabled(&st,SK_I_PLAY),"ready joined human can start network room");
 sk_init(&st,maps,2,NULL);sk_enh_pick_row(&st,1); /* a closed picker cannot choose a map */
 check(st.sel==0,"closed map picker preserves map selection");
 int rx[3],rw[3],y,h;st.popup=7;for(int i=0;i<3;i++)check(sk_item_rect(&st,SK_I_POP_BOT+i,&rx[i],&y,&rw[i],&h)!=0,"legacy choice has a seat");
 check(rx[0]+rw[0]<=rx[1]&&rx[1]+rw[1]<=rx[2],"legacy Computer Empty Blocked hit regions are disjoint");
}
static std::vector<CNCPlayerInfoStruct> captured;
static bool fake_mp(int,CNCMultiplayerOptionsStruct &opts,int n,CNCPlayerInfoStruct *rows,int){check(opts.MPlayerCount==n,"engine options agree with compact roster");captured.assign(rows,rows+n);return true;}
static bool fake_player_state(GameStateRequestEnum request,uint64,unsigned char *data,unsigned int size){if(request!=GAME_STATE_PLAYER_INFO||size<sizeof(CNCPlayerInfoStruct))return false;CNCPlayerInfoStruct info={};info.AllyFlags=3;memcpy(data,&info,sizeof info);return true;}
static void engine_checks(){
 GameOpts o={};o.bases=1;o.credits=5000;o.ai_count=1;for(int i=0;i<8;i++){o.seat_mode[i]=NM_SEAT_OPEN;o.player_colour[i]=i;o.player_team[i]=i;o.start_wp[i]=-1;}
 o.seat_mode[0]=NM_SEAT_HUMAN;o.seat_mode[5]=NM_SEAT_BOT;o.seat_mode[3]=NM_SEAT_BLOCK;o.player_team[5]=o.player_team[0];
 CNC_Set_MP_t saved=BrainSetMP;BrainSetMP=fake_mp;check(arm_skirmish(&o),"one-opponent sparse lobby reaches engine");
 check(captured.size()==2&&g_matchSeat[0]==0&&g_matchSeat[1]==5,"Empty and Blocked do not become ghost players");
 if(captured.size()>=2)check(!captured[0].IsAI&&captured[1].IsAI&&captured[1].ColorIndex==5&&captured[1].Team==0&&captured[1].AllyFlags==3,"sparse seat retains colour team AI identity and alliance");
 CNC_Get_State_t stateSaved=BrainGetState;BrainGetState=fake_player_state;for(int i=0;i<8;i++)o.player_team[i]=0;skirmish_report_teams(&o);check(game_skirmish_teams_ok()==1,"empty allied seats do not pollute actual team validation");BrainGetState=stateSaved;
 o.seat_mode[5]=NM_SEAT_OPEN;captured.clear();check(!arm_skirmish(&o)&&captured.empty(),"engine independently rejects one-person roster");BrainSetMP=saved;
}
// Ink is measured from the shipped font pixels with the actual teletype's six-pixel pitch.
static std::set<int> type_ink(const CampCtx *k,const CampType &t){
 std::set<int> ink;std::vector<unsigned char> px(320*200);DB_Surface s;db_surface_init(&s,320,200,px.data());unsigned char pal[16];for(int i=0;i<16;i++)pal[i]=i>1?1:0;
 for(size_t i=0;i<strlen(t.text);i++){char one[2]={t.text[i],0};db_print(&s,k->sf,one,t.x+int(i)*6,t.y,pal,0);}for(int i=0;i<320*200;i++)if(px[i])ink.insert(i);return ink;
}
static std::map<int,std::string> scoreNumbers;
static void menu_regression_number(int x,int y,const char *text,const unsigned char *pal){if(pal==camp_sc_greenpal&&x==264&&(y==128||y==140||y==152))scoreNumbers[y]=text;}
static int menu_regression_tally(Camp *c,void *ctx,int side){
 CampCtx *k=(CampCtx*)ctx;std::vector<const CampType*> build;bool neutral=false;int overlap=0;
 for(int i=0;i<k->ntype;i++){const CampType &t=k->type[i];if(t.pal==camp_sc_greenpal&&t.y>=126){build.push_back(&t);if(t.x==200&&t.y==152)neutral=true;}}
 for(size_t a=0;a<build.size();a++)for(size_t b=a+1;b<build.size();b++){auto x=type_ink(k,*build[a]),y=type_ink(k,*build[b]);for(int v:x)if(y.count(v))overlap++;}
 check(overlap==0,"score building heading and faction label ink do not overlap");check(neutral,"civilian building count retains source label");
 check(build.size()==5,"score has two heading lines and three faction labels");
 check(scoreNumbers.size()==3,"score captures all three building count rows");
 for(const auto &number:scoreNumbers){check(264+db_string_width(k->sf,number.second.c_str(),0)<=320,"multidigit building count fits original value column");std::vector<unsigned char> px(320*200);DB_Surface surface;db_surface_init(&surface,320,200,px.data());unsigned char pal[16];for(int i=0;i<16;i++)pal[i]=i>1?1:0;db_print(&surface,k->sf,number.second.c_str(),264,number.first,pal,0);bool clear=true;for(const CampType *t:build)for(int index:type_ink(k,*t))if(px[index])clear=false;check(clear,"building values do not overwrite heading or faction labels");}
 FILE *f=fopen((output+"/"+scoreName+".json").c_str(),"w");assert(f);fprintf(f,"{\"side\":%d,\"overlap_pixels\":%d,\"labels\":[",side,overlap);
 for(size_t i=0;i<build.size();i++)fprintf(f,"%s{\"text\":\"%s\",\"x\":%d,\"y\":%d}",i?",":"",build[i]->text,build[i]->x,build[i]->y);fprintf(f,"]}\n");fclose(f);
 camp_draw(c);int w,h;SDL_GL_GetDrawableSize(c->win,&w,&h);check(game_grab_png((output+"/"+scoreName+".png").c_str(),w,h)!=0,"capture actual campaign tally");
 // A raw 320x200 witness gives direct evidence independent of window scaling and logo geometry.
 std::vector<unsigned char> raw(320*200*4);for(int i=0;i<320*200;i++){int v=c->plate[i];for(int rgb=0;rgb<3;rgb++)raw[i*4+rgb]=c->pal[v*3+rgb]*4;raw[i*4+3]=255;}
 check(png_write_rgba((output+"/"+scoreName+"-plate.png").c_str(),raw.data(),320,200)!=0,"capture unchanged native font pixels");scoreCases++;return 1;
}
int main(int argc,char **argv){
 assert(argc==2);output=argv[1];SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
 SDL_Window *win=SDL_CreateWindow("Private menu regression",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(win);SDL_GLContext ctx=SDL_GL_CreateContext(win);assert(ctx);SDL_GL_SetSwapInterval(0);
 game_visuals_default_enhanced(NULL);char err[256];DB_Pack *p=db_pack_load("dosmenu.pack",err,sizeof err);assert(p&&em_available());engine_checks();
 int widths[]={800,1280,1920},heights[]={600,720,1080};for(int i=0;i<3;i++){SDL_SetWindowSize(win,widths[i],heights[i]);skirmish_checks(p,widths[i],heights[i]);}
 CncAudio *audio=cnc_audio_create("dosdata",err,sizeof err);assert(audio);camp_autopilot=1;
 for(int i=0;i<3;i++)for(int side=0;side<2;side++){
  SDL_SetWindowSize(win,widths[i],heights[i]);Camp c;assert(camp_open(&c,win,audio,"campaign.pack","dosmenu.pack",err,sizeof err));CampScore score={};
  score.score=i==0?245:i==1?12500:999999;score.leadership=80;score.efficiency=25;score.minutes=15;score.credits=3518;
  score.gdi_killed=i==0?52:i==1?999:9999;score.nod_killed=11;score.civ_killed=0;score.gdi_bldg=i==0?0:i==1?12:9999;score.nod_bldg=i==0?0:i==1?345:9999;score.civ_bldg=i==0?0:i==1?6:9999;
  scoreNumbers.clear();scoreName="score-"+std::to_string(widths[i])+"-side"+std::to_string(side);camp_score(&c,&score,side,1);camp_close(&c);
 }
 cnc_audio_destroy(audio);db_pack_free(p);em_reset();SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();
 printf("MENU_RESULT|checks=%d|failures=%d|scores=%d\n",checks,failures,scoreCases);return failures?1:0;
}
