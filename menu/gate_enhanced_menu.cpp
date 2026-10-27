/* Hidden native-render/control integration test, not a playable UI fixture. */
#include "enhanced_menu.cpp"
#include <cassert>
static int applied=0;
static int sounds[2]={};
extern "C" void game_enhanced_menu_sound(int click){sounds[click?1:0]++;}
static std::string shotdir;
static void settings_apply(void *,const DOPT_Settings *){applied++;}
static void check_seats(){
 for(size_t i=0;i<v.seats.size();i++){const Seat &a=v.seats[i];assert(a.x>=20&&a.y>=60&&a.x+a.w<=v.w-20&&a.y+a.h<=v.h-20);
  if(a.kind==BUTTON){std::string id=art_id("button-"+std::to_string(a.w)+"-"+std::to_string(a.h)+"-normal");assert(arts.count(id));}
  for(size_t j=0;j<i;j++){const Seat &b=v.seats[j];bool overlap=a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;
   if(overlap&&v.drop==-1){fprintf(stderr,"overlap: %s / %s\n",a.label.c_str(),b.label.c_str());assert(!overlap);}}
 }
}
static void sound_seats(SDL_Window *win){
 SDL_SetWindowSize(win,1280,720);em_viewport(1280,720);sound_hover.clear();
 const Seat *button=nullptr;for(const Seat &q:v.seats)if(!q.disabled&&q.kind!=FIELD){button=&q;break;}
 assert(button);SDL_Event e={};e.type=SDL_MOUSEMOTION;e.motion.x=v.x+(button->x+button->w/2)*v.scale;e.motion.y=v.y+(button->y+button->h/2)*v.scale;
 int h=sounds[0],c=sounds[1];em_sound_event(&e,win);em_sound_event(&e,win);assert(sounds[0]==h+1&&sounds[1]==c);
 e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;em_sound_event(&e,win);assert(sounds[1]==c+1);
 e.type=SDL_MOUSEBUTTONUP;em_sound_event(&e,win);assert(sounds[1]==c+1);
 e.type=SDL_MOUSEMOTION;e.motion.x=e.motion.y=0;em_sound_event(&e,win);assert(sounds[0]==h+1);
 for(const Seat &q:v.seats)if(q.disabled||q.kind==FIELD){e.motion.x=v.x+(q.x+q.w/2)*v.scale;e.motion.y=v.y+(q.y+q.h/2)*v.scale;em_sound_event(&e,win);assert(sounds[0]==h+1);}
}
static void save(const char *name){check_seats();assert(em_write_shot((shotdir+"/"+name+".png").c_str(),1280,720));
 sound_seats(SDL_GL_GetCurrentWindow());
 printf("ENMENU|feedback|%s|hover/click, release, repeated motion, disabled/empty/field=PASS\n",name);}
static int click(DOPT_State *s,int item,int fraction=500){int x,y,w,h;assert(em_item_rect(item,&x,&y,&w,&h));int dx,dy;em_to_legacy(x+(w-1)*fraction/1000,y+h/2,&dx,&dy);em_opt_press(s,dx,dy);return em_opt_release(s,dx,dy);}
static int clicksk(SK_State *s,int item){int x,y,w,h;assert(em_item_rect(item,&x,&y,&w,&h));int dx,dy;em_to_legacy(x+w/2,y+h/2,&dx,&dy);em_sk_press(s,dx,dy);return em_sk_release(s,dx,dy);}
static void option_point(DOPT_State *state,int pointX,int pointY){
 int legacyX,legacyY;em_to_legacy(v.x+pointX*v.scale,v.y+pointY*v.scale,&legacyX,&legacyY);em_opt_press(state,legacyX,legacyY);
 em_options(state);em_viewport(v.fbw,v.fbh);em_to_legacy(v.x+pointX*v.scale,v.y+pointY*v.scale,&legacyX,&legacyY);em_opt_release(state,legacyX,legacyY);
}
static void dropdown_layer(DOPT_State *state,int item){
 em_options(state);em_viewport(v.fbw,v.fbh);click(state,item);em_options(state);assert(v.popupFirst>=0&&v.popupOwner>=0);
 const Seat first=v.seats[v.popupFirst];const int gapY=first.y+first.h;
 if(item==DOPT_VE_RESOLUTION){assert(!v.popupHint.empty());assert(v.popupY+v.popupH-26>=v.seats.back().y+v.seats.back().h);}
 for(int column=first.x+4;column<first.x+first.w-4;column++){
  const unsigned char *pixel=&v.rgba[(gapY*v.w+column)*4];assert(pixel[0]==0x10&&pixel[1]==0x13&&pixel[2]==0x10&&pixel[3]==255);
 }
 const DOPT_Visuals before=state->vis;
 const int scrollBefore=state->advTop;em_opt_scroll(state,1);assert(state->advTop==scrollBefore);assert(!memcmp(&before,&state->vis,sizeof before));
 if(item==DOPT_VE_RESOLUTION){assert(state->restop==1);em_opt_scroll(state,-1);assert(state->restop==0);}
 option_point(state,first.x+first.w/2,gapY);assert(v.drop==item&&state->texdrop==item);assert(!memcmp(&before,&state->vis,sizeof before));
 em_options(state);
 int disabled=-1;for(int index=v.popupFirst;index<int(v.seats.size());index++)if(v.seats[index].disabled){disabled=index;break;}
 if(disabled>=0){const Seat seat=v.seats[disabled];option_point(state,seat.x+seat.w/2,seat.y+seat.h/2);assert(v.drop==item&&state->texdrop==item);assert(!memcmp(&before,&state->vis,sizeof before));}
 em_options(state);
 int blocked=-1;for(int index=0;index<v.popupFirst;index++)if(index!=v.popupOwner&&!v.seats[index].disabled&&v.seats[index].action==OPTITEM){const Seat &seat=v.seats[index];if(!popup_contains(seat.x+seat.w/2,seat.y+seat.h/2)){blocked=index;break;}}
 assert(blocked>=0);const Seat underneath=v.seats[blocked];option_point(state,underneath.x+underneath.w/2,underneath.y+underneath.h/2);assert(v.drop==-1&&state->texdrop==-1);assert(!memcmp(&before,&state->vis,sizeof before));
 em_options(state);click(state,item);em_options(state);assert(v.popupFirst>=0);
 int allowed=-1;for(int index=v.popupFirst;index<int(v.seats.size());index++)if(!v.seats[index].disabled){allowed=index;break;}
 assert(allowed>=0);const Seat choice=v.seats[allowed];option_point(state,choice.x+choice.w/2,choice.y+choice.h/2);assert(v.drop==-1&&state->texdrop==-1);assert(dopt_enh_drop_value(state,item)==choice.arg);
 em_options(state);click(state,item);em_options(state);state->selected=DOPT_VE_WATER;const DOPT_Visuals keyboardBefore=state->vis;
 assert(em_opt_key(state,DOPT_KEY_DOWN)==DOPT_ACT_NONE&&state->selected==DOPT_VE_WATER&&state->page==DOPT_PAGE_ADVANCED&&v.drop==item);assert(!memcmp(&keyboardBefore,&state->vis,sizeof keyboardBefore));
 assert(v.popupChoice>=0&&dopt_enh_drop_allowed(state,item,v.popupChoice));const int keyboardChoice=v.popupChoice;
 assert(em_opt_key(state,DOPT_KEY_ENTER)==DOPT_ACT_NONE&&v.drop==-1&&state->texdrop==-1);assert(dopt_enh_drop_value(state,item)==keyboardChoice);assert(state->vis.prof[2].elem[DOPT_VE_WATER]==keyboardBefore.prof[2].elem[DOPT_VE_WATER]);
 em_options(state);click(state,item);em_options(state);const DOPT_Visuals escapeBefore=state->vis;assert(em_opt_key(state,DOPT_KEY_ESC)==DOPT_ACT_NONE&&state->page==DOPT_PAGE_ADVANCED&&v.drop==-1&&state->texdrop==-1);assert(!memcmp(&escapeBefore,&state->vis,sizeof escapeBefore));
 printf("ENMENU|dropdown|item=%d|opaque gaps, disabled rows, outside dismissal, no click-through, keyboard choice/escape, wheel isolation=PASS\n",item);
}
static void compact_sound_rows(DOPT_State *state){
 DOPT_Track tracks[16]={};for(int index=0;index<16;index++){tracks[index].base="fixture";tracks[index].fullname="Fixture track";tracks[index].index=index;}
 state->page=DOPT_PAGE_SOUND;dopt_set_tracks(state,tracks,16,0);state->selected=DOPT_S_LIST;state->trkSel=state->trkTop=0;em_options(state);assert(v.compact);
 em_opt_scroll(state,100);assert(state->trkTop==7);em_options(state);check_seats();bool finalVisible=false;int count=0;
 for(const Seat &seat:v.seats)if(seat.action==OPTROW){count++;if(seat.arg==15)finalVisible=true;}assert(count==9&&finalVisible);
 em_opt_scroll(state,-100);assert(state->trkTop==0);state->trkSel=0;
 for(int index=0;index<15;index++)assert(em_opt_key(state,DOPT_KEY_DOWN)==DOPT_ACT_NONE);
 assert(state->trkSel==15&&state->trkTop==7);em_options(state);check_seats();finalVisible=false;
 for(const Seat &seat:v.seats)if(seat.action==OPTROW&&seat.arg==15&&seat.active)finalVisible=true;assert(finalVisible);
 assert(em_write_shot((shotdir+"/title-sound-last-row-"+std::to_string(v.fbw)+".png").c_str(),v.fbw,v.fbh));dopt_set_tracks(state,nullptr,0,-1);
 puts("ENMENU|compact-sound|nine visible rows, final track reachable by wheel and keyboard=PASS");
}
static void compact_slot_rows(DOPT_State *state){
 state->page=DOPT_PAGE_SLOTS;state->sl.nrows=16;
 for(int index=0;index<state->sl.nrows;index++){state->sl.rows[index].slot=index;snprintf(state->sl.rows[index].text,sizeof state->sl.rows[index].text,"Fixture save %d",index+1);}
 for(int mode:{DOPT_SL_LOAD,DOPT_SL_SAVE,DOPT_SL_DELETE}){
  state->sl.mode=mode;state->selected=DOPT_SL_LIST;state->sl.sel=state->sl.top=0;em_options(state);assert(v.compact);
  for(int index=0;index<15;index++)assert(em_opt_key(state,DOPT_KEY_DOWN)==DOPT_ACT_NONE);
  assert(state->sl.sel==15);em_options(state);check_seats();bool finalVisible=false;int count=0;
  for(const Seat &seat:v.seats)if(seat.action==OPTROW){count++;if(seat.arg==15&&seat.active)finalVisible=true;}assert(count==(mode==DOPT_SL_SAVE?9:10)&&finalVisible);
  assert(em_write_shot((shotdir+"/title-slots-last-row-"+std::to_string(mode)+"-"+std::to_string(v.fbw)+".png").c_str(),v.fbw,v.fbh));
 }
 puts("ENMENU|compact-slots|load/save/delete populated lists fit and keyboard reaches final row=PASS");
}
static void retired_relief(DOPT_State *state){
 const DOPT_State savedState=*state;const int savedSection=v.section;state->page=DOPT_PAGE_ADVANCED;
 for(int mode=0;mode<DOPT_MODE_COUNT;mode++){
  state->advTab=mode;state->vis.prof[mode].elem[DOPT_VE_RELIEF]=1;state->selected=DOPT_VE_RELIEF;
  assert(!dopt_vis_in_mode(DOPT_VE_RELIEF,mode)&&dopt_item_disabled(state,DOPT_VE_RELIEF));
  const DOPT_Visuals before=state->vis;assert(dopt_key(state,DOPT_KEY_ENTER)==DOPT_ACT_NONE);assert(!memcmp(&before,&state->vis,sizeof before));
  int rows[DOPT_VE_COUNT];const int count=dopt_vis_mode_rows(mode,rows);for(int index=0;index<count;index++)assert(rows[index]!=DOPT_VE_RELIEF);
 }
 state->advTab=DOPT_MODE_ENHANCED;
 for(int section=0;section<5;section++){v.section=section;em_options(state);assert(!em_item_rect(DOPT_VE_RELIEF,nullptr,nullptr,nullptr,nullptr));for(const Seat &seat:v.seats)assert(seat.item!=DOPT_VE_RELIEF);}
 *state=savedState;v.section=savedSection;em_options(state);
 puts("ENMENU|retired-relief|absent from every Visuals section and controller mode; legacy profile value preserved=PASS");
}
static void ai_difficulty_rows(SK_State *state,const DB_Pack *pack,const char *prefix){
 const SK_State saved=*state;const int section=v.sksection;v.sksection=0;state->popup=-1;state->net=0;
 for(int i=1;i<sk_players(state);i++){state->mode[i]=SK_SEAT_BOT;state->ai_levels[i]=(unsigned char)((i-1)%4);}
 em_lobby(state,pack);em_viewport(1280,720);check_seats();int cells=0;
 for(const Seat &q:v.seats)if(SK_IS_LEVEL(q.item)){cells++;int seat=q.item-SK_I_LEVEL0;assert(q.disabled==(seat==0));assert(q.label==(seat==0?std::string("-"):sk_level_name(sk_row_level(state,seat))));assert(textwidth(q.label)<=q.w-8);}
 assert(cells==sk_players(state));
 for(int i=0;i<sk_players(state);i++){std::string summary=seat_side(state,i)+" / "+std::to_string(sk_row_team(state,i)+1)+" / Random";assert(textwidth(summary)<=264);}
 state->ai_levels[1]=1;
 for(int want:{2,3,0,1}){em_lobby(state,pack);em_viewport(1280,720);clicksk(state,SK_I_LEVEL0+1);assert(sk_row_level(state,1)==want);}
 state->popup=1;em_lobby(state,pack);em_viewport(1280,720);clicksk(state,-14);em_lobby(state,pack);assert(v.popupFirst>=0);assert(em_write_shot((shotdir+"/"+prefix+"-difficulty-options.png").c_str(),1280,720));
 for(int want=0;want<4;want++){em_lobby(state,pack);em_viewport(1280,720);clicksk(state,SK_I_POP_EASY+want);assert(sk_row_level(state,1)==want);if(want<3){em_lobby(state,pack);em_viewport(1280,720);clicksk(state,-14);}}
 em_lobby(state,pack);assert(em_write_shot((shotdir+"/"+prefix+"-difficulty-setup.png").c_str(),1280,720));
 state->popup=-1;em_lobby(state,pack);for(const auto &size:std::vector<std::pair<int,int>>{{1280,720},{2560,1440}}){
  SDL_Window *window=SDL_GL_GetCurrentWindow();SDL_SetWindowSize(window,size.first,size.second);SDL_PumpEvents();int dw,dh;SDL_GL_GetDrawableSize(window,&dw,&dh);assert(dw==size.first&&dh==size.second);
  em_viewport(size.first,size.second);assert(em_write_shot((shotdir+"/"+prefix+"-difficulty-"+std::to_string(size.first)+".png").c_str(),size.first,size.second));
  unsigned char corner[4];glReadPixels(size.first-1,size.second-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,corner);assert(corner[3]==255);
 }
 SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),1280,720);SDL_PumpEvents();em_viewport(1280,720);
 SK_Lobby result;sk_result(state,&result);for(int i=0;i<8;i++)assert(result.ai_levels[i]==state->ai_levels[i]);
 state->selected=SK_I_LEVEL0+1;sk_key(state,SK_KEY_LEFT);assert(sk_row_level(state,1)==2);sk_key(state,SK_KEY_RIGHT);assert(sk_row_level(state,1)==3);
 state->net=1;state->net_seat=0;state->net_seats=sk_players(state);state->net_taken[0]=1;state->net_taken[1]=0;
 sk_enh_set_level(state,1,0);assert(sk_row_level(state,1)==0);em_lobby(state,pack);assert(em_write_shot((shotdir+"/"+prefix+"-difficulty-network-host.png").c_str(),1280,720));state->net=2;state->net_seat=0;const int before=sk_row_level(state,1);
 sk_enh_set_level(state,1,3);assert(sk_row_level(state,1)==before);assert(sk_item_disabled(state,SK_I_LEVEL0+1));
 em_lobby(state,pack);check_seats();for(const Seat &q:v.seats)if(SK_IS_LEVEL(q.item))assert(q.disabled);
 assert(em_write_shot((shotdir+"/"+prefix+"-difficulty-network-joiner.png").c_str(),1280,720));
 if(!strcmp(prefix,"td")){
  assert(sk_check_layout(pack,state)==0);DB_Surface classic;unsigned char pixels[320*200],rgba[320*200*4];db_surface_init(&classic,320,200,pixels);sk_draw(&classic,pack,state);db_surface_to_rgba(&classic,pack->pal8,rgba,0);assert(png_write_rgba((shotdir+"/classic-difficulty.png").c_str(),rgba,320,200));
  SK_State classicState=*state;classicState.net=0;
  for(int seat=1;seat<sk_players(&classicState);seat++){classicState.popup=seat;
   for(int level=0;level<4;level++){int x,y,w,h;assert(sk_item_rect(&classicState,SK_I_POP_EASY+level,&x,&y,&w,&h));assert(sk_hit_test(&classicState,x+w/2,y+h/2)==SK_I_POP_EASY+level);sk_press(&classicState,x+w/2,y+h/2);sk_release(&classicState,x+w/2,y+h/2);assert(sk_row_level(&classicState,seat)==level);}
  }
  classicState.popup=1;sk_draw(&classic,pack,&classicState);db_surface_to_rgba(&classic,pack->pal8,rgba,0);assert(png_write_rgba((shotdir+"/classic-difficulty-options.png").c_str(),rgba,320,200));
 }
 *state=saved;v.sksection=section;em_lobby(state,pack);
 printf("ENMENU|%s|difficulty|four named levels, per-seat pointer/dropdown/keyboard, answer roundtrip, host-only edits, 720p/1440p fit=PASS\n",prefix);
}
/* THE SETTINGS SCREEN'S GAMEPLAY AND AUDIO TABS: drawn with their sliders and switches, a
   slider dragged to each end lands on that end of its setting through the bound apply, a
   switch toggles through the bound gameplay apply, and the screen stays on its own page. */
static int tabapplied=0,tabgp=0;
static void count_apply(void*,const DOPT_Settings*){tabapplied++;}
static void count_gp(void*,const DOPT_Gameplay*){tabgp++;}
static void settings_tabs(DOPT_State *state,const char *prefix){
 dopt_bind(state,NULL,count_apply);dopt_bind_gameplay(state,count_gp);state->page=DOPT_PAGE_ADVANCED;em_options(state);
 const int sliders[2][4]={{DOPT_ES_SPEED,DOPT_ES_SCROLL,-1,-1},{DOPT_ES_MUSIC,DOPT_ES_SOUND,DOPT_ES_VOICES,DOPT_ES_AMBIENCE}};
 for(int tab=0;tab<2;tab++){
  v.section=5+tab;em_options(state);em_viewport(1280,720);check_seats();
  assert(em_write_shot((shotdir+"/"+prefix+"-settings-"+(tab?"audio":"gameplay")+".png").c_str(),1280,720));
  for(int k=0;k<4&&sliders[tab][k]>=0;k++){const int which=sliders[tab][k],before=tabapplied;
   click(state,EM_SETTING_SEAT-which,0);em_options(state);assert(dopt_enh_setting_value(state,which)==0);
   click(state,EM_SETTING_SEAT-which,1000);em_options(state);assert(dopt_enh_setting_value(state,which)==dopt_enh_setting_max(which));
   assert(tabapplied>before&&state->page==DOPT_PAGE_ADVANCED&&v.section==5+tab);}
  if(!tab)for(int i=0;i<DOPT_G_TOGGLES;i++){const int was=state->gp.on[i],before=tabgp;click(state,EM_GAMEPLAY_SEAT-i);em_options(state);assert(state->gp.on[i]!=was&&tabgp==before+1);}
 }
 v.section=0;printf("ENMENU|%s|settings-tabs|gameplay and audio drawn, sliders to both ends, switches toggled, page kept=PASS\n",prefix);
}
/* THE LANGUAGE ROW AND ITS BOX, on stand-in answers: English on the disk, Deutsch not; the
   list opens and offers both, picking Deutsch opens the box, Download starts it, the bar is
   drawn part way, and a finished download is used and closed with OK. */
static int fakecur=0,fakestate=0,fakechose=-1;
static int fk_count(){return 2;}
static const char *fk_name(int i){return i?"Deutsch":"English";}
static int fk_current(){return fakecur;}
static int fk_installed(int i){return i==0||fakestate==2;}
static long long fk_missing(int i){return i?385447221LL:0;}
static int fk_choose(int i){fakecur=i;fakechose=i;return 1;}
static int fk_download(int){fakestate=1;return 1;}
static int fk_progress(long long *d,long long *t,char *m,int n){if(d)*d=154000000;if(t)*t=385447221;if(m&&n)snprintf(m,n,"Downloading...");return fakestate;}
static void fk_cancel(){fakestate=4;}
static const EM_LangHooks fakehooks={fk_count,fk_name,fk_current,fk_installed,fk_missing,fk_choose,fk_download,fk_progress,fk_cancel};
static void language_box(DOPT_State *state,const char *prefix){
 em_set_language_hooks(&fakehooks);fakecur=0;fakestate=0;fakechose=-1;state->page=DOPT_PAGE_ADVANCED;em_options(state);v.section=5;v.langBox=-1;
 em_options(state);em_viewport(1280,720);check_seats();
 click(state,EM_LANG_SEAT);em_options(state);assert(v.drop==EM_LANG_DROP);check_seats();
 assert(em_write_shot((shotdir+"/"+prefix+"-language-list.png").c_str(),1280,720));
 click(state,EM_LANG_SEAT-2);em_options(state);assert(v.langBox==1&&!v.langStarted);check_seats();
 assert(em_write_shot((shotdir+"/"+prefix+"-language-ask.png").c_str(),1280,720));
 click(state,EM_LANG_SEAT-20);em_options(state);assert(v.langStarted&&fakestate==1);check_seats();
 assert(em_write_shot((shotdir+"/"+prefix+"-language-bar.png").c_str(),1280,720));
 fakestate=2;em_options(state);assert(fakechose==1);check_seats();
 assert(em_write_shot((shotdir+"/"+prefix+"-language-ready.png").c_str(),1280,720));
 click(state,EM_LANG_SEAT-21);em_options(state);assert(v.langBox==-1);
 em_set_language_hooks(nullptr);v.section=0;
 printf("ENMENU|%s|language|row, list, box, bar, ready and used=PASS\n",prefix);
}
/* THE PAUSE MENU'S AUDIO: Game Controls carries the speed and the scroll rate only, the
   Audio button under Gameplay opens the jukebox page with all four volumes (the voices
   among them), and its Back returns to the pause menu on Audio. */
static int pauseapplied=0;
static void count_pause(void*,const DOPT_Settings*){pauseapplied++;}
static void pause_audio(const DB_Pack *pack,const char *prefix){
 DOPT_State o={};dopt_settings_init(&o.set);dopt_open(&o,pack);dopt_bind(&o,nullptr,count_pause);em_title_mode(0);
 em_options(&o);em_viewport(1280,720);check_seats();
 int audio=0;for(const Seat &q:v.seats)if(q.item==DOPT_AUDIO){audio++;assert(q.label=="Audio");}assert(audio==1);
 assert(em_write_shot((shotdir+"/"+prefix+"-pause.png").c_str(),1280,720));
 o.page=DOPT_PAGE_CONTROLS;em_options(&o);check_seats();
 assert(!em_item_rect(DOPT_C_MUSIC,nullptr,nullptr,nullptr,nullptr)&&!em_item_rect(DOPT_C_SOUNDCTRL,nullptr,nullptr,nullptr,nullptr));
 assert(em_write_shot((shotdir+"/"+prefix+"-game-controls.png").c_str(),1280,720));
 click(&o,DOPT_C_SPEED,1000);assert(o.set.speed==6);em_options(&o);click(&o,DOPT_C_SPEED,0);assert(o.set.speed==0);
 o.page=DOPT_PAGE_OPTIONS;em_options(&o);click(&o,DOPT_AUDIO);em_options(&o);assert(o.page==DOPT_PAGE_SOUND);check_seats();
 assert(em_write_shot((shotdir+"/"+prefix+"-audio.png").c_str(),1280,720));
 const int sliders[4]={DOPT_S_MUSIC,DOPT_S_SOUND,DOPT_S_SPEECH,DOPT_S_AMBIENCE};
 for(int k=0;k<4;k++){const int before=pauseapplied;click(&o,sliders[k],0);em_options(&o);click(&o,sliders[k],1000);em_options(&o);assert(pauseapplied>before);}
 assert(o.set.speech==239&&o.set.music==239);
 click(&o,DOPT_S_OK);assert(o.page==DOPT_PAGE_OPTIONS&&o.selected==DOPT_AUDIO);
 printf("ENMENU|%s|pause-audio|Audio under Gameplay, Game Controls speed and scroll only, four volumes with the voices, back to the pause menu=PASS\n",prefix);
}
static void title_settings(SDL_Window *window,const DB_Pack *pack){
 DOPT_State state={};dopt_open(&state,pack);state.vis.mode=state.advTab=DOPT_MODE_ENHANCED;state.vis.enh_tex_ok=1;state.vis.dos_tex_ok=0;state.vis.prof[2].elem[DOPT_VE_NEWHUD]=1;
 state.vis.nres=10;for(int index=0;index<state.vis.nres;index++){state.vis.res_w[index]=640+index*160;state.vis.res_h[index]=480+index*90;state.vis.res_fit[index]=index<5;}
 const int pages[]={DOPT_PAGE_OPTIONS,DOPT_PAGE_CONTROLS,DOPT_PAGE_GAMEPLAY,DOPT_PAGE_ADVANCED,DOPT_PAGE_SLOTS,DOPT_PAGE_SOUND,DOPT_PAGE_CHEATS,DOPT_PAGE_RESTATE,DOPT_PAGE_CONFIRM,DOPT_PAGE_NOTICE};
 for(const auto &size:std::vector<std::pair<int,int>>{{1280,720},{1920,1440},{2560,1440}}){
  const int width=size.first,height=size.second;SDL_SetWindowSize(window,width,height);em_title_mode(0);DM_State mainState;dm_state_init(&mainState);em_main(&mainState);em_viewport(width,height);em_draw(width,height,1);
  const int panelY=v.y,panelHeight=v.h,panelScale=v.scale;std::vector<unsigned char> mainPixels(width*height*4),settingsPixels(mainPixels.size());glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,mainPixels.data());
  em_title_mode(1);
  for(int page:pages){state.page=page;em_options(&state);em_viewport(width,height);check_seats();assert(v.y==panelY&&v.h==panelHeight&&v.scale==panelScale&&v.y+v.h*v.scale<=height);em_draw(width,height,2);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,settingsPixels.data());
   for(int row=0;row<panelY;row++)assert(!memcmp(&mainPixels[(height-1-row)*width*4],&settingsPixels[(height-1-row)*width*4],width*4));
   assert(em_write_shot((shotdir+"/title-settings-"+std::to_string(page)+"-"+std::to_string(width)+".png").c_str(),width,height));
  }
  state.page=DOPT_PAGE_ADVANCED;state.texdrop=-1;em_options(&state);
  const int items[]={DOPT_VE_RESOLUTION,DOPT_VE_TEXSET,DOPT_VE_INFSET,DOPT_VE_UISCALE,DOPT_VE_PERSPECTIVE};
  for(int item:items){v.section=item==DOPT_VE_RESOLUTION?0:item==DOPT_VE_TEXSET||item==DOPT_VE_INFSET?1:3;em_options(&state);em_viewport(width,height);dropdown_layer(&state,item);
   em_options(&state);click(&state,item);em_options(&state);check_seats();assert(em_write_shot((shotdir+"/title-dropdown-"+std::to_string(item)+"-"+std::to_string(width)+".png").c_str(),width,height));state.texdrop=v.drop=-1;
  }
  printf("ENMENU|title-settings|%dx%d|matching panel, fixed backdrop and logo, all pages/dropdowns=PASS\n",width,height);
  compact_sound_rows(&state);compact_slot_rows(&state);
 }
 SDL_SetWindowSize(window,1280,720);em_title_mode(1);settings_tabs(&state,"td");language_box(&state,"td");
 em_title_mode(0);state.page=DOPT_PAGE_ADVANCED;em_options(&state);assert(v.h==636&&!v.compact&&!v.title);SDL_SetWindowSize(window,1280,720);em_viewport(1280,720);
}
static void skirmish_dropdown(SK_State *state,const DB_Pack *pack){
 state->net=0;state->popup=0;em_lobby(state,pack);em_viewport(1280,720);
 int legacyX,legacyY;const int colour=sk_row_colour(state,0);em_to_legacy(v.x+190*v.scale,v.y+290*v.scale,&legacyX,&legacyY);em_sk_press(state,legacyX,legacyY);em_sk_release(state,legacyX,legacyY);em_lobby(state,pack);assert(v.drop==-11&&v.popupFirst>=0);
 em_to_legacy(v.x+430*v.scale,v.y+284*v.scale,&legacyX,&legacyY);em_sk_press(state,legacyX,legacyY);em_lobby(state,pack);em_to_legacy(v.x+430*v.scale,v.y+284*v.scale,&legacyX,&legacyY);em_sk_release(state,legacyX,legacyY);assert(v.drop==-1&&sk_row_colour(state,0)==colour);
 puts("ENMENU|skirmish-dropdown|outside click closes without choosing underlying colour=PASS");state->popup=-1;
}

/* ---- RED ALERT'S KIT ----------------------------------------------------------------------
   The same screens drawn from ra3d/enhanced-menu.pack, the Red Alert module's own kit, in the
   same process after Tiberian Dawn's pass (whose pictures are written first and are not
   touched by this). Skipped, and said so, when no Red Alert module is staged. */
static const char *td_words[]={"GDI","Nod","Tiberium","Covert","Command & Conquer"};
static void ra_words(){for(const Seat &q:v.seats)for(const char *w:td_words)if(q.label.find(w)!=std::string::npos){fprintf(stderr,"Tiberian Dawn word in a Red Alert seat: %s\n",q.label.c_str());assert(0);}}
static void ra_art(){for(const Seat &q:v.seats)if(q.kind==BUTTON){std::string id=art_id("button-"+std::to_string(q.w)+"-"+std::to_string(q.h)+"-normal");if(!arts.count(id)){fprintf(stderr,"no art %s\n",id.c_str());assert(0);}}}
/* The frame's own field, just inside the left rail, is red at the title, blue in a mission. */
static void ra_palette(bool blue){const unsigned char *p=&v.rgba[(70*v.w+31)*4];int r=p[0],b=p[2];if(blue?!(b>r):!(r>b)){fprintf(stderr,"palette %s (screen %s, theme %s): field %d,%d,%d\n",blue?"blue":"red",v.frame.c_str(),theme.blue?"blue":"red",p[0],p[1],p[2]);assert(0);}}
static int ra_shots=0;
/* two saved missions, so the pause page's Load, Save and Delete are live */
static int ra_slot_rows(void *,DOPT_SlotRow *out,int max){const char *t[]={"Allied 1 - In the thick of it","Soviet 1 - Lesson in Blood"};int n=0;for(;n<2&&n<max;n++){out[n].slot=n;snprintf(out[n].text,sizeof out[n].text,"%s",t[n]);}return n;}
static void ra_save(const char *name,bool blue){
 check_seats();ra_words();ra_art();ra_palette(blue);
 for(const auto &size:std::vector<std::pair<int,int>>{{1280,720},{2560,1440}}){SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),size.first,size.second);em_viewport(size.first,size.second);
  assert(em_write_shot((shotdir+"/ra-"+name+"-"+std::to_string(size.second)+".png").c_str(),size.first,size.second));ra_shots++;}
 /* (a screen with a list open over it is sounded through the list, not the seats below) */
 if(v.popupFirst<0)sound_seats(SDL_GL_GetCurrentWindow());printf("ENMENU|ra|%s|%s palette, Red Alert art and words%s=PASS\n",name,blue?"blue":"red",v.popupFirst<0?", hover/click":", list open");
}
static void ra_pass(){
 FILE *f=fopen("ra3d/enhanced-menu.pack","rb");if(!f){puts("ENMENU|ra|SKIP|no Red Alert module staged (ra3d/enhanced-menu.pack)");return;}fclose(f);
 /* a fresh kit: the pack, the textures made from the last one, every screen */
 em_set_red_alert("OpenCNC3D gate","OpenCNC3D is an unofficial fan project, not affiliated with or endorsed by Electronic Arts.\nCommand & Conquer: Red Alert is a trademark of Electronic Arts Inc. Created by freschism");
 texture=bgtexture=logotexture=legaltexture=bgtexture_lo=logotexture_lo=0;em_reset();assert(em_available()&&em_red_alert());
 char err[256];DB_Pack *p=db_pack_load("ra3d/dosmenu.pack",err,sizeof err);assert(p);em_menu_overlay(p,-1000,-1000);
 DM_State m;dm_state_init(&m);m.red_alert=1;m.version="RA3D";m.copyright="";m.special_missing=1;m.selected=DM_START;em_main(&m);em_viewport(1280,720);
 {int seen=0;for(const Seat &q:v.seats){if(q.item==DM_SPECIAL){seen++;assert(q.disabled&&q.label=="Special Ops");}assert(textwidth(q.label)<=q.w-16);}assert(seen==1);assert(em_main_next(&m,DM_START,1)==DM_USERMAPS);}
 ra_save("main",false);
 /* RA3D cleanup: a legal string must not produce any main-menu footer pixels. */
 for(const auto &size:std::vector<std::pair<int,int>>{{1280,720},{2560,1440}}){int w=size.first,h=size.second;SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),w,h);em_main(&m);em_viewport(w,h);
  std::vector<unsigned char> a(w*h*4),b(a.size());em_draw(w,h,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,a.data());
  std::string keep=theme.legal;theme.legal="";em_draw(w,h,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,b.data());theme.legal=keep;
  int changed=0;for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(memcmp(&a[(y*w+x)*4],&b[(y*w+x)*4],4)){changed++;assert(h-1-y>=v.y+v.h*v.scale);}
  assert(changed==0);printf("ENMENU|ra|legal|%dx%d|changed=%d (footer absent)\n",w,h,changed);}
 /* the two emblems turn inside their own seats */
 {SDL_SetWindowSize(SDL_GL_GetCurrentWindow(),1280,720);em_main(&m);em_viewport(1280,720);std::vector<unsigned char> a(1280*720*4),b(a.size());
  em_draw(1280,720,1);glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,a.data());faction_angle_override=1;em_draw(1280,720,1);glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,b.data());faction_angle_override=-1;
  int c[2]={};for(int yy=0;yy<720;yy++)for(int xx=0;xx<1280;xx++)if(memcmp(&a[(yy*1280+xx)*4],&b[(yy*1280+xx)*4],4)){int y=719-yy;bool l=xx>=77&&xx<277&&y>=484&&y<684,r=xx>=1003&&xx<1203&&y>=484&&y<684;assert(l||r);c[r?1:0]++;}
  assert(c[0]>1000&&c[1]>1000);printf("ENMENU|ra|main-spin|Allied=%d|Soviet=%d|outside-seats=0\n",c[0],c[1]);}
 for(int i=0;i<2;i++){em_choice(i,1,-1);ra_save(i?"intro":"exit",false);}
 /* the side choice: both plates with an emblem, the sides, the difficulty, Start */
 em_side_choice(1,2,-1);em_viewport(1280,720);
 {int plates=0;for(const Seat &q:v.seats)if(q.kind==PLATE){plates++;assert(q.disabled&&q.badge>=0);}assert(plates==2);
  int x,y,w,h;assert(em_item_rect(EM_SIDE_SOVIET,&x,&y,&w,&h)&&em_pick(x+w/2,y+h/2)==EM_SIDE_SOVIET);assert(em_item_rect(EM_SIDE_HARD,&x,&y,&w,&h)&&em_pick(x+w/2,y+h/2)==EM_SIDE_HARD);
  assert(em_item_rect(EM_SIDE_START,&x,&y,&w,&h)&&em_pick(x+w/2,y+h/2)==EM_SIDE_START);
  for(const Seat &q:v.seats){if(q.item==EM_SIDE_SOVIET)assert(q.active);if(q.item==EM_SIDE_ALLIES)assert(!q.active);if(q.item==EM_SIDE_HARD)assert(q.active);}}
 em_side_choice(0,1,-1);ra_save("side",false);
 /* the dialogs, at the title (red) and in a mission (blue) */
 DOPT_State o={};dopt_settings_init(&o.set);dopt_open(&o,p);dopt_bind(&o,nullptr,settings_apply);dopt_bind_slots(&o,ra_slot_rows);o.scenario="SCG01EA";dopt_set_n64_unavailable(&o,1);
 o.vis.mode=o.advTab=DOPT_MODE_ENHANCED;o.vis.enh_tex_ok=1;
 em_title_mode(0);em_options(&o);ra_save("pause",true);
 /* Load, Save and Delete answer with the notice until Red Alert's saves have their own index,
    and the notice's OK goes back to the pause page */
 em_viewport(1280,720);click(&o,DOPT_SAVE);assert(o.page==DOPT_PAGE_NOTICE&&o.nback&&!strcmp(o.ncaption,"NOTICE"));em_options(&o);ra_save("notice-saves",true);
 em_viewport(1280,720);click(&o,0);assert(o.page==DOPT_PAGE_OPTIONS&&o.selected==DOPT_SAVE);
 em_set_route_ready(EM_ROUTE_SAVES,1);em_options(&o);em_viewport(1280,720);click(&o,DOPT_LOAD);assert(o.page==DOPT_PAGE_SLOTS);em_set_route_ready(EM_ROUTE_SAVES,0);
 em_options(&o);ra_save("load",true);o.page=DOPT_PAGE_OPTIONS;
 puts("ENMENU|ra|saves|notice until the split, OK back to the pause page, the slot list once ready=PASS");
 const struct{int page;const char *name;}pages[]={{DOPT_PAGE_CONTROLS,"controls"},{DOPT_PAGE_GAMEPLAY,"gameplay"},{DOPT_PAGE_CHEATS,"cheats"},{DOPT_PAGE_CONFIRM,"confirm"}};
 for(auto &pg:pages){o.page=pg.page;em_options(&o);ra_save(pg.name,true);}
 /* a match's box asks to surrender, and once surrendered asks to leave */
 dopt_set_match(&o,1,0);o.page=DOPT_PAGE_CONFIRM;em_options(&o);ra_save("confirm-surrender",true);
 dopt_set_match(&o,1,1);o.page=DOPT_PAGE_CONFIRM;em_options(&o);ra_save("confirm-leave",true);dopt_set_match(&o,0,0);
 DOPT_Track tracks[12]={};const char *names[]={"Bigfoot","Crush","Face the Enemy 1","Face the Enemy 2","Hell March","Run for Your Life","Smash","Trenches","Workmen","Await","Dense","Fogger"};
 for(int i=0;i<12;i++){tracks[i].base="fixture";tracks[i].fullname=names[i];tracks[i].index=i;}
 o.page=DOPT_PAGE_SOUND;dopt_set_tracks(&o,tracks,12,4);em_options(&o);{int icons=0;for(const Seat &q:v.seats)if(!q.icon.empty()){icons++;assert(arts.count(art_id(q.icon)));}assert(icons==2);}ra_save("sound",true);dopt_set_tracks(&o,nullptr,0,-1);
 /* the mission briefing stands on its side's paper: ALIPAPER for an Allied mission, SOVPAPER for a Soviet one */
 std::vector<unsigned char> paper[2];
 for(const char *scen:{"SCG01EA","SCU01EA"}){o.page=DOPT_PAGE_RESTATE;o.scenario=scen;em_set_mission_side(scen[2]=='U');strcpy(o.brief,"Rescue Einstein from the Headquarters inside this Soviet complex. Once found, evacuate him via the helicopter at the signal flare.");em_options(&o);
  for(int y=400;y<450;y++)paper[scen[2]=='U'].insert(paper[scen[2]=='U'].end(),&v.rgba[(y*v.w+60)*4],&v.rgba[(y*v.w+600)*4]);
  ra_save(scen[2]=='U'?"briefing-soviet":"briefing-allied",true);}
 assert(paper[0]!=paper[1]);em_set_mission_side(0);
 o.page=DOPT_PAGE_VISUALS;o.vis.mode=DOPT_MODE_ENHANCED;
 for(int i=0;i<5;i++){v.section=i;em_options(&o);if(i==0)v.section=0;
  for(const Seat &q:v.seats){if(q.item==DOPT_VE_TIB3D&&q.kind==CHECK)assert(q.label=="3D Ore"&&!q.disabled);
   if(q.kind==CHECK&&(q.item==DOPT_VE_FOLIAGE||q.item==DOPT_VE_ROCKS||q.item==DOPT_VE_PATCHES))assert(!q.disabled&&q.value==o.vis.prof[o.advTab].elem[q.item]);
   if(q.action==MODE&&q.arg==1)assert(q.disabled);}
  ra_save(("visuals-"+std::to_string(i)).c_str(),true);}
 /* RA's implemented features must be clickable in the actual Enhanced seat geometry. */
 for(int item:{DOPT_VE_TREES,DOPT_VE_GRASS,DOPT_VE_TIB3D,DOPT_VE_FOLIAGE,DOPT_VE_ROCKS,DOPT_VE_PATCHES,DOPT_VE_NEWHUD}){
  v.section=item==DOPT_VE_NEWHUD?3:(item==DOPT_VE_FOLIAGE||item==DOPT_VE_ROCKS||item==DOPT_VE_PATCHES)?4:1;em_options(&o);em_viewport(1280,720);
  int before=o.vis.prof[DOPT_MODE_ENHANCED].elem[item];click(&o,item);assert(o.vis.prof[DOPT_MODE_ENHANCED].elem[item]!=before);
  em_options(&o);click(&o,item);assert(o.vis.prof[DOPT_MODE_ENHANCED].elem[item]==before);
 }
 /* the arrows step over the rows Red Alert greys */
 v.section=1;em_options(&o);o.selected=DOPT_VE_INFSET;for(int k=0;k<6;k++){em_opt_key(&o,DOPT_KEY_DOWN);assert(!ra_unavailable(o.selected));}
 puts("ENMENU|ra|visuals|N64 and the rows Red Alert does not draw greyed, stepped over by the arrows=PASS");
 /* the resolution list open over the Display page */
 o.page=DOPT_PAGE_VISUALS;o.vis.nres=7;{const int rw[]={1280,1600,1920,1920,2560,2560,3840},rh[]={720,900,1080,1200,1440,1600,2160};for(int i=0;i<7;i++){o.vis.res_w[i]=rw[i];o.vis.res_h[i]=rh[i];o.vis.res_fit[i]=1;}}
 v.section=0;em_options(&o);em_viewport(1280,720);click(&o,DOPT_VE_RESOLUTION);em_options(&o);assert(v.popupFirst>=0);ra_save("visuals-resolution",true);v.drop=-1;o.texdrop=-1;
 /* the same dialogs opened from the title are red, at the main menu's height */
 em_title_mode(1);o.page=DOPT_PAGE_VISUALS;v.section=0;em_options(&o);assert(v.compact&&v.h==564);ra_save("title-visuals",false);
 {DOPT_State t=o;settings_tabs(&t,"ra");t.page=DOPT_PAGE_ADVANCED;v.section=6;em_options(&t);ra_save("title-settings-audio",false);v.section=0;}
 dopt_open_slots(&o,p,DOPT_SL_LOAD);em_options(&o);ra_save("title-load",false);em_title_mode(0);
 dopt_open_notice(&o,p,"NOTICE",em_route_notice(EM_ROUTE_USERMAPS));em_title_mode(1);em_options(&o);ra_save("notice-usermaps",false);em_title_mode(0);
 /* skirmish and the network lobby: Red Alert's sides, ore, ten tech levels, its colours */
 const SK_Map maps[]={{"SCM06EA","Isle of Fury (6-8)","TEMPERATE",96,96,8,0},{"SCM11EA","Island Hoppers (2-6)","SNOW",96,96,6,0}};
 SK_State sst;sk_init(&sst,maps,2,nullptr);sk_set_red_alert(&sst);em_lobby(&sst,p);
 for(int i=0;i<8;i++)assert(sk_row_colour(&sst,i)==sk_row_country(&sst,i));ai_difficulty_rows(&sst,p,"ra");ra_save("skirmish",false);
 v.sksection=1;em_lobby(&sst,p);{bool ore=false,shroud=false,ctf=false,computers=false,super=false;for(const Seat &q:v.seats){if(q.label=="Ore Regenerates")ore=true;if(q.label=="Shroud Regrows")shroud=true;if(q.label=="Capture The Flag")ctf=true;if(q.label=="New AI default")computers=true;if(q.label=="Superweapons")super=true;if(q.item==SK_I_BUILD)assert(q.max==10);}assert(ore&&shroud&&ctf&&computers&&!super);}ra_save("skirmish-rules",false);
 /* Red Alert's own rules pressed as a hand presses them: Shroud Regrows and Capture The Flag
    ticked, the computers' slider to its right end (Hard) and its left (Easy); the lobby's
    answer carries all three */
 {assert(sst.shroud==0&&sst.ctf==0&&sst.ai_difficulty==1);em_viewport(1280,720);clicksk(&sst,-50-SK_RA_SHROUD);em_lobby(&sst,p);clicksk(&sst,-50-SK_RA_CTF);em_lobby(&sst,p);assert(sst.shroud==1&&sst.ctf==1);
  int x,y,w,h,lx,ly;assert(em_item_rect(-50-SK_RA_DIFFICULTY,&x,&y,&w,&h));em_to_legacy(x+w-2,y+h/2,&lx,&ly);em_sk_press(&sst,lx,ly);em_sk_release(&sst,lx,ly);assert(sst.ai_difficulty==3);
  em_lobby(&sst,p);ra_save("skirmish-rules-set",false);em_viewport(1280,720);assert(em_item_rect(-50-SK_RA_DIFFICULTY,&x,&y,&w,&h));em_to_legacy(x+1,y+h/2,&lx,&ly);em_sk_press(&sst,lx,ly);em_sk_release(&sst,lx,ly);assert(sst.ai_difficulty==0);
  SK_Lobby answer;sk_result(&sst,&answer);assert(answer.shroud==1&&answer.ctf==1&&answer.ai_difficulty==0);
  sst.shroud=sst.ctf=0;sst.ai_difficulty=1;}
 puts("ENMENU|ra|rules|Shroud Regrows, Capture The Flag and the computers' Easy / Normal / Hard / Expert pressed and in the lobby's answer=PASS");
 v.sksection=0;
 sst.popup=1;em_lobby(&sst,p);for(const Seat &q:v.seats)if(q.kind==COLOUR)assert(q.value==int(player_colour(q.item-SK_I_POP_C1)));ra_save("player-setup",false);
 v.drop=-11;em_lobby(&sst,p);assert(v.popupFirst>=0);
 /* the country list: all eight, the Allies first; England chosen for seat 2 makes it Allied */
 {int n=0,england=-1;for(int i=v.popupFirst;i<int(v.seats.size());i++){n++;if(v.seats[i].label=="England")england=i;}assert(n==8&&england>=0);
  ra_save("player-setup-country",false);em_viewport(1280,720);const Seat e=v.seats[england];int lx,ly;em_to_legacy(v.x+(e.x+e.w/2)*v.scale,v.y+(e.y+e.h/2)*v.scale,&lx,&ly);em_sk_press(&sst,lx,ly);em_sk_release(&sst,lx,ly);
  assert(sk_row_country(&sst,1)==3&&sk_row_house(&sst,1)==0&&v.drop==-1);}
 puts("ENMENU|ra|countries|eight countries in the seat's list, the choice sets its side, each seat its own colour=PASS");
 sst.popup=-1;
 em_lobby(&sst,p);em_viewport(1280,720);clicksk(&sst,SK_I_CHANGEMAP);em_lobby(&sst,p);ra_save("maps",false);clicksk(&sst,SK_I_PICK_CANCEL);
 sst.net=1;sst.net_seat=0;sst.net_seats=8;sst.net_humans=1;sst.net_taken[0]=1;strcpy(sst.name[0],"Commander");sst.chat_count=0;sk_chat_push(&sst,-1,"Waiting for other commanders to join.");em_lobby(&sst,p);ra_save("lobby",false);
 v.sksection=1;em_lobby(&sst,p);ra_save("lobby-rules",false);
 /* a joiner reads the host's rules and cannot move them */
 sst.net=2;sst.net_seat=1;em_lobby(&sst,p);{int ra=0;for(const Seat &q:v.seats)if(q.action==RACHECK||q.action==RARANGE){ra++;assert(q.disabled);}assert(ra==3);}
 sk_enh_ra_rule(&sst,SK_RA_SHROUD,1);sk_enh_ra_rule(&sst,SK_RA_DIFFICULTY,2);assert(sst.shroud==0&&sst.ai_difficulty==1);ra_save("lobby-rules-joiner",false);sst.net=1;sst.net_seat=0;
 puts("ENMENU|ra|room-rules|a joiner's Red Alert rules are drawn read only and refuse a change=PASS");
 v.sksection=0;
 MP_State b;mp_init(&b);em_browser(&b);ra_save("host",false);b.tab=MP_TAB_JOIN;em_browser(&b);ra_save("join",false);
 b.prompt=1;em_browser(&b);ra_save("player-name",false);b.prompt=0;b.page=MP_PAGE_WAIT;em_browser(&b);ra_save("wait-room",false);
 /* Special Ops: Counterstrike and Aftermath tabs, Allied before Soviet, plates; User Maps empty */
 const DO_Mission ops[]={{"","COUNTERSTRIKE",0,1},{"SCG20EA","Sarin Gas 1: Crackdown",0,0},{"SCU20EA","Soviet Soldier Volkov and Chitzkoi",1,0},{"","AFTERMATH",0,1},{"SCG40EA","Harbor Reclamation",0,0},{"SCU40EA","Shock Therapy",1,0}};
 DO_State dst;do_state_init(&dst,ops,6);em_ops(&dst);{int tabs=0,plates=0;for(const Seat &q:v.seats){if(q.action==OPSFILTER){tabs++;assert(q.label=="Counterstrike"||q.label=="Aftermath");}if(q.kind==PLATE)plates++;}assert(tabs==2&&plates==1);}ra_save("special-ops",false);
 v.opsfilter=1;em_ops(&dst);ra_save("special-ops-aftermath",false);
 const DO_Mission none[]={{"","COUNTERSTRIKE",0,1},{"","AFTERMATH",0,1}};DO_State noops;do_state_init(&noops,none,2);em_ops(&noops);ra_save("special-ops-empty",false);
 /* the match debrief: the session's table after a third match, this machine's commander
    first and the beaten computers in their own colours */
 {EM_Debrief d;memset(&d,0,sizeof d);d.nrows=4;d.games=3;d.win=1;d.decided=1;d.seconds=754;snprintf(d.mapname,sizeof d.mapname,"Coastal Influence (4-6)");
  d.killed=17;d.razed=6;d.lost=9;d.harvested=12400;
  const char *names[4]={"Commander","COMPUTER","COMPUTER","COMPUTER"};const int country[4]={1,2,3,4},colour[4]={1,2,3,4},wins[4]={2,1,0,1};
  const int kills[4][4]={{14,9,23,-1},{8,17,11,-1},{-1,5,7,-1},{3,12,4,-1}};
  for(int r=0;r<4;r++){snprintf(d.row[r].name,sizeof d.row[r].name,"%s",names[r]);d.row[r].country=country[r];d.row[r].colour=colour[r];d.row[r].wins=wins[r];for(int k=0;k<4;k++)d.row[r].kills[k]=kills[r][k];d.row[r].defeated=r>0;d.row[r].is_local=r==0;}
  em_ra_debrief(&d,-1);{int buttons=0;for(const Seat &q:v.seats)if(q.item==EM_DEBRIEF_CONTINUE)buttons++;assert(buttons==1);}ra_save("debrief",false);
  d.win=0;d.row[0].defeated=1;d.row[1].defeated=0;em_ra_debrief(&d,-1);ra_save("debrief-defeat",false);}
 DO_State empty;do_state_init(&empty,nullptr,0);em_ops(&empty);ra_save("user-maps",false);
 /* the Database, a mission tab: blue, five categories with Ships */
 const char *roster[]={"Construction Yard","Power Plant","Tesla Coil"};EM_Database db={};db.count=3;db.house=2;db.names=roster;db.name=roster[2];db.code="TSLA";db.brief="";db.armour="Heavy";db.hp=400;db.cost=1500;db.damage=db.speed=db.rof=db.range256=-1;db.model_available=1;db.selected=2;
 em_database(&db);{int cats=0;for(const Seat &q:v.seats)if(q.item>=1000&&q.item<1010){cats++;}assert(cats==5);}ra_save("database",true);
 db_pack_free(p);printf("ENMENU|ra|PASS|%d Red Alert pictures: main, side, pause, notices, every options page, lobby, browser, ops, database\n",ra_shots);
}
int main(int argc,char **argv){
 assert(argc==2||argc==3);shotdir=argv[1];
 assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);SDL_Window *win=SDL_CreateWindow("Enhanced menu gate",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(win);SDL_GLContext gl=SDL_GL_CreateContext(win);assert(gl);assert(em_available());
 char err[256];DB_Pack *p=db_pack_load("dosmenu.pack",err,sizeof err);assert(p);
 if(argc==3&&!strcmp(argv[2],"--ra-only")){ra_pass();db_pack_free(p);SDL_GL_DeleteContext(gl);SDL_DestroyWindow(win);SDL_Quit();puts("ENMENU|RA-ONLY|PASS");return 0;}
 if(argc==3&&!strcmp(argv[2],"--difficulty-only")){
  const SK_Map maps[]={{"SCM01EA","Eight Commanders","TEMPERATE",62,62,8,0}};
  SK_State state;sk_init(&state,maps,1,nullptr);ai_difficulty_rows(&state,p,"td");db_pack_free(p);
  em_set_red_alert("Fair AI review","");texture=bgtexture=logotexture=legaltexture=bgtexture_lo=logotexture_lo=0;em_reset();assert(em_available());
  p=db_pack_load("ra3d/dosmenu.pack",err,sizeof err);assert(p);sk_init(&state,maps,1,nullptr);sk_set_red_alert(&state);ai_difficulty_rows(&state,p,"ra");db_pack_free(p);
  SDL_GL_DeleteContext(gl);SDL_DestroyWindow(win);SDL_Quit();puts("ENMENU|DIFFICULTY|PASS");return 0;
 }
 /* --settings-only: the title's Settings screen alone, both games, for a quick look */
 if(argc==3&&!strcmp(argv[2],"--settings-only")){
  title_settings(win,p);pause_audio(p,"td");db_pack_free(p);
  FILE *f=fopen("ra3d/dosmenu.pack","rb");
  if(f){fclose(f);em_set_red_alert("Settings review","");texture=bgtexture=logotexture=legaltexture=bgtexture_lo=logotexture_lo=0;em_reset();assert(em_available());
   p=db_pack_load("ra3d/dosmenu.pack",err,sizeof err);assert(p);DOPT_State o={};dopt_settings_init(&o.set);dopt_open(&o,p);em_title_mode(1);
   settings_tabs(&o,"ra");language_box(&o,"ra");pause_audio(p,"ra");em_title_mode(1);o.page=DOPT_PAGE_ADVANCED;v.section=6;em_options(&o);em_viewport(1280,720);assert(em_write_shot((shotdir+"/ra-title-settings-audio.png").c_str(),1280,720));db_pack_free(p);}
  SDL_GL_DeleteContext(gl);SDL_DestroyWindow(win);SDL_Quit();puts("ENMENU|SETTINGS|PASS");return 0;
 }
 DM_State m;dm_state_init(&m);m.selected=DM_START;m.version="v0.8.1";em_menu_overlay(p,-1000,-1000);em_main(&m);em_viewport(1280,720);save("main");
 /* Display order, keyboard navigation and the original controller actions agree. */
 for(int n=0;n<DM_ITEM_COUNT;n++){
  const Seat &q=v.seats[n];assert(q.item==main_order[n]&&q.y==main_y[n]);assert(textwidth(q.label)<=q.w-16);
  if(!q.disabled){int x,y,w,h,lx,ly;assert(em_item_rect(q.item,&x,&y,&w,&h));em_to_legacy(x+w/2,y+h/2,&lx,&ly);assert(dm_hit_test(&m,lx,ly)==q.item);}
  int next=em_main_next(&m,q.item,1);assert(!dm_state_item_disabled(&m,next));
 }
 assert(em_main_next(&m,DM_EXIT,1)==DM_START);assert(em_main_next(&m,DM_START,-1)==DM_EXIT);
 m.intro_missing=1;assert(em_main_next(&m,DM_VISUALS,1)==DM_EXIT);
 /* An install without the movies (the default until Setup downloads them) greys Intro on the Enhanced menu too. */
 em_main(&m);{int seen=0;for(const Seat &q:v.seats)if(q.item==DM_INTRO){seen++;assert(q.disabled);}assert(seen==1);}
 m.intro_missing=0;em_main(&m);for(const Seat &q:v.seats)if(q.item==DM_INTRO)assert(!q.disabled);
 printf("ENMENU|intro|greyed without the movies, live with them=PASS\n");
 for(int y:{246,354}){int lx,ly;em_to_legacy(v.x+v.w*v.scale/2,v.y+y*v.scale,&lx,&ly);assert(dm_hit_test(&m,lx,ly)<0);}
 printf("ENMENU|main-order|grouped labels, keyboard, controller actions and inert dividers=PASS\n");
 /* Compare actual framebuffer pixels with the legal text removed, then with
    the pointer added. Every difference must stay in its own screen seat. */
 for(const auto &size:std::vector<std::pair<int,int>>{{1280,720},{1600,960},{2560,1440}}){
  int w=size.first,h=size.second;SDL_SetWindowSize(win,w,h);em_main(&m);em_viewport(w,h);
  std::vector<unsigned char> legal(w*h*4),plain(legal.size()),cursor(legal.size());
  em_draw(w,h,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,legal.data());
  const char *notice=m.copyright;m.copyright="";em_draw(w,h,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,plain.data());m.copyright=notice;
  int changed=0;for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(memcmp(&plain[(y*w+x)*4],&legal[(y*w+x)*4],4)){changed++;assert(h-1-y>=v.y+v.h*v.scale);}
  assert(changed>100);assert(emcursor::load());assert(emcursor::mesh.size()==108);
  em_menu_overlay(p,60,100);em_draw(w,h,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,cursor.data());
  int green=0,ink=0;for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(memcmp(&cursor[(y*w+x)*4],&legal[(y*w+x)*4],4)){
   int sy=h-1-y;assert(x>=60-10*v.scale&&x<=60+40*v.scale&&sy>=100-10*v.scale&&sy<=100+50*v.scale);ink++;
   const unsigned char *px=&cursor[(y*w+x)*4];green+=px[1]>px[0]+20&&px[1]>px[2]+20;
  }assert(ink>100&&green>100);
  assert(em_write_shot((shotdir+"/main-overlay-"+std::to_string(w)+".png").c_str(),w,h));
  printf("ENMENU|overlay|%dx%d|legal=%d|cursor=%d|green=%d|outside-seats=0\n",w,h,changed,ink,green);em_menu_overlay(p,-1000,-1000);
 }
 SDL_SetWindowSize(win,1280,720);em_main(&m);em_viewport(1280,720);save("main");
 /* Main menu animation stays in the two requested outer seats. */
 std::vector<unsigned char> titleStill(1280*720*4),titleSpin(titleStill.size());glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,titleStill.data());
 faction_angle_override=1;save("main-spin");faction_angle_override=-1;glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,titleSpin.data());
 int titlechanged[2]={};for(int yy=0;yy<720;yy++)for(int xx=0;xx<1280;xx++)if(memcmp(&titleStill[(yy*1280+xx)*4],&titleSpin[(yy*1280+xx)*4],4)){
  int y=719-yy;bool left=xx>=77&&xx<277&&y>=484&&y<684,right=xx>=1003&&xx<1203&&y>=484&&y<684;
  assert(left||right);titlechanged[right?1:0]++;
 }assert(titlechanged[0]>1000&&titlechanged[1]>1000);printf("ENMENU|main-spin|GDI=%d|Nod=%d|outside-seats=0\n",titlechanged[0],titlechanged[1]);
 const char *roster[]={"Power Plant","Communications Center"};EM_Database db={};db.count=2;db.house=1;db.names=roster;db.name=roster[0];db.code="NUKE";db.brief="Supplies 100 power; unarmed; wood armour.";db.armour="Wood";db.hp=200;db.cost=300;db.damage=db.speed=db.rof=db.range256=-1;db.model_available=1;db.unlocks="Barracks, refinery";
 for(int detail=0;detail<4;detail++){db.detail=detail;em_database(&db);save(("database-"+std::to_string(detail)).c_str());
  for(const Seat &q:v.seats)if(q.kind==BUTTON)assert(textwidth(q.label)<=q.w-(q.badge>=0?52:16));}
 em_viewport(1280,720);int dbx,dby,dbw,dbh;assert(em_database_view(&dbx,&dby,&dbw,&dbh));assert(dbx==420&&dby==244&&dbw==332&&dbh==296);
 assert(em_pick(1036,148)==4000&&em_pick(1162,148)==4001&&em_pick(920,262)==5001);printf("ENMENU|database|fittings/text/model-well/hit-seats=PASS\n");
 DOPT_State o={};dopt_settings_init(&o.set);dopt_open(&o,p);dopt_bind(&o,nullptr,settings_apply);em_options(&o);save("pause");
 /* A title dialog must leave visible backdrop around its wide frame; a pause
    dialog must retain the battlefield behind it instead of clearing it. */
 o.page=DOPT_PAGE_VISUALS;o.vis.mode=DOPT_MODE_ENHANCED;em_options(&o);
 for(const auto &size:std::vector<std::pair<int,int>>{{1280,720},{2560,1440}}){
  int w=size.first,h=size.second;SDL_SetWindowSize(win,w,h);em_title_mode(1);em_options(&o);em_viewport(w,h);em_draw(w,h,2);
  std::vector<unsigned char> rgba(w*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
  int red=0;for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(x<v.x||x>=v.x+v.w*v.scale||h-1-y<v.y||h-1-y>=v.y+v.h*v.scale){const unsigned char *px=&rgba[(y*w+x)*4];red+=px[0]>80&&px[0]>px[1]+20&&px[0]>px[2]+40;}
  assert(red>1000);printf("ENMENU|settings-backdrop|%dx%d|outside-frame-red=%d\n",w,h,red);
  em_title_mode(0);em_options(&o);em_viewport(w,h);
  glClearColor(.12f,.24f,.36f,1);glClear(GL_COLOR_BUFFER_BIT);em_draw(w,h,0);unsigned char corner[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,corner);assert(corner[0]>15&&corner[0]<30&&corner[1]>35&&corner[1]<55&&corner[2]>55&&corner[2]<75);
 }
 SDL_SetWindowSize(win,1280,720);em_viewport(1280,720);
 o.page=DOPT_PAGE_CONTROLS;em_options(&o);em_viewport(1280,720);save("controls");assert(!em_item_rect(DOPT_C_MUSIC,nullptr,nullptr,nullptr,nullptr)&&!em_item_rect(DOPT_C_SOUNDCTRL,nullptr,nullptr,nullptr,nullptr));
 click(&o,DOPT_C_SPEED,0);assert(o.set.speed==0);em_options(&o);click(&o,DOPT_C_SPEED,1000);assert(o.set.speed==6);
 /* the volumes are the pause menu's Audio page, the voices among them */
 o.page=DOPT_PAGE_OPTIONS;em_options(&o);click(&o,DOPT_AUDIO);em_options(&o);assert(o.page==DOPT_PAGE_SOUND);save("audio");
 click(&o,DOPT_S_MUSIC,0);assert(o.set.music==0);em_options(&o);click(&o,DOPT_S_MUSIC,1000);assert(o.set.music==239&&applied>=2);
 em_options(&o);click(&o,DOPT_S_SPEECH,0);assert(o.set.speech==0);em_options(&o);click(&o,DOPT_S_SPEECH,1000);assert(o.set.speech==239);
 em_options(&o);click(&o,DOPT_S_OK);assert(o.page==DOPT_PAGE_OPTIONS&&o.selected==DOPT_AUDIO);
 o.page=DOPT_PAGE_VISUALS;o.vis.mode=DOPT_MODE_ENHANCED;em_options(&o);assert(!em_item_rect(DOPT_VE_SUPERSAMPLE,nullptr,nullptr,nullptr,nullptr));save("visuals-display");
 /* Vertical sync clicks through off, on and adaptive; on and adaptive are both ticked, so the label must tell them apart. */
 o.vis.vsync=0;for(int want:{1,2,0}){em_options(&o);em_viewport(1280,720);click(&o,DOPT_VE_VSYNC);assert(o.vis.vsync==want);
  em_options(&o);int seen=0;for(const Seat &q:v.seats)if(q.item==DOPT_VE_VSYNC){seen++;assert(q.value==want&&q.label==(want==2?std::string("Adaptive sync"):std::string(dopt_vis_elem_label(DOPT_VE_VSYNC))));}assert(seen==1);
  if(want==2)save("visuals-adaptive");}
 printf("ENMENU|vsync|off, on, adaptive, off; adaptive labelled apart=PASS\n");
 for(int i=1;i<5;i++){v.section=i;em_options(&o);save((std::string("visuals-")+std::to_string(i)).c_str());}
 retired_relief(&o);
 v.section=2;em_options(&o);em_viewport(1280,720);int before=o.vis.prof[2].elem[DOPT_VE_SHADOWS];click(&o,DOPT_VE_SHADOWS);assert(o.vis.prof[2].elem[DOPT_VE_SHADOWS]!=before);
 o.vis.from_menu=1;em_options(&o);assert(click(&o,DOPT_A_OK)==DOPT_ACT_RESUME);
 o.page=DOPT_PAGE_VISUALS;o.vis.from_menu=0;em_options(&o);assert(em_opt_key(&o,DOPT_KEY_ESC)==DOPT_ACT_NONE&&o.page==DOPT_PAGE_OPTIONS);
 o.page=DOPT_PAGE_VISUALS;o.vis.from_menu=1;em_options(&o);int bx=0,by=0;for(const Seat &q:v.seats)if(q.action==BACK){em_to_legacy(v.x+(q.x+q.w/2)*v.scale,v.y+(q.y+q.h/2)*v.scale,&bx,&by);}
 em_opt_press(&o,bx,by);assert(em_opt_release(&o,bx,by)==DOPT_ACT_RESUME);
 o.page=DOPT_PAGE_GAMEPLAY;em_options(&o);save("gameplay");before=o.gp.on[DOPT_G_SWAPBTN];click(&o,DOPT_G_SWAPBTN);assert(o.gp.on[DOPT_G_SWAPBTN]!=before);
 o.page=DOPT_PAGE_CONFIRM;em_options(&o);save("confirm");click(&o,DOPT_CF_CANCEL);assert(o.page==DOPT_PAGE_OPTIONS);
 o.page=DOPT_PAGE_RESTATE;strcpy(o.brief,"Secure the beachhead and establish a base. Build a refinery and harvest Tiberium. Eliminate the Nod forces in the area.");em_options(&o);save("briefing");
 dopt_open_slots(&o,p,DOPT_SL_LOAD);assert(o.sl.nrows==0&&o.sl.sel==-1);em_options(&o);
 for(const Seat &q:v.seats)assert(q.action!=OPTROW);save("load-empty");
 const SK_Map maps[]={{"SCM01EA","Green Acres","TEMPERATE",58,44,8,0},{"SCM02EA","Sand Trap","DESERT",62,62,8,0}};
 SK_Prev *pr=sk_prev_load("mappreview.pack",err,sizeof err);SK_State s;sk_init(&s,maps,2,pr);em_lobby(&s,p);em_viewport(1280,720);save("skirmish");clicksk(&s,SK_I_ROW0);assert(s.popup==0);em_lobby(&s,p);save("seat");clicksk(&s,SK_I_POP_C1+2);assert(sk_row_colour(&s,0)==2);s.popup=-1;
 v.sksection=1;em_lobby(&s,p);save("skirmish-rules");v.sksection=0;em_lobby(&s,p);clicksk(&s,SK_I_CHANGEMAP);assert(s.picker);em_lobby(&s,p);save("maps");clicksk(&s,SK_I_PICK_CANCEL);assert(!s.picker);
 s.net=1;s.net_seat=0;s.net_seats=8;s.net_humans=1;s.net_taken[0]=1;strcpy(s.name[0],"Commander");s.chat_count=0;
 sk_chat_push(&s,-1,"Waiting for other commanders to join.");sk_chat_push(&s,0,"This is a long message that must wrap cleanly into several lines without hiding the message input or losing the newest messages at the bottom of the transcript.");
 em_lobby(&s,p);save("lobby");strcpy(s.chat_in,"Ready when you are.");int sendx=0,sendy=0;for(const Seat &q:v.seats)if(q.action==SEND){em_to_legacy(v.x+q.x*v.scale+q.w*v.scale/2,v.y+q.y*v.scale+q.h*v.scale/2,&sendx,&sendy);}
 em_sk_press(&s,sendx,sendy);assert(em_sk_release(&s,sendx,sendy)==SK_ACT_SAY);
 s.net=2;v.sksection=1;em_lobby(&s,p);save("lobby-rules-joiner");before=s.credits;sk_enh_control(&s,SK_I_CREDITS,10000);assert(s.credits==before);assert(sk_enh_action(&s,SK_I_CHANGEMAP)==0&&!s.picker);
 MP_State b;mp_init(&b);em_browser(&b);save("host");b.tab=MP_TAB_JOIN;em_browser(&b);save("join-empty");
 DO_Mission missions[]={{"SCG22EA","Blackout",0,0},{"SCB20EA","Liberation",1,0}};DO_State ops;do_state_init(&ops,missions,2);em_ops(&ops);save("missions");
 const DO_Mission categories[]={{"","NOD COVERT OPERATIONS",1,1},{"SCB20EA","Nod Covert",1,0},{"","GDI COVERT OPERATIONS",0,1},{"SCG22EA","GDI Covert",0,0},{"","GDI SPECIAL OPS",0,1},{"SCG30EA","GDI Special",0,0},{"","NOD SPECIAL OPS",1,1},{"SCB21EA","Nod Special",1,0},{"","TEST MAPS",0,1},{"TESTMAP","Test Map",0,0}};
 DO_State combined;do_state_init(&combined,categories,10);em_ops(&combined);save("special-covert");
 std::vector<int> badgeOrder;int filters=0;for(const Seat &q:v.seats){if(q.action==OPSFILTER)filters++;if(q.badge>=0)badgeOrder.push_back(q.badge);assert(q.label!="Test Map"&&q.label!="Test Maps");}
 assert(filters==2&&(badgeOrder==std::vector<int>{0,1}));em_ops_move(&combined,-10000);assert(combined.selected==3);em_ops_move(&combined,1);assert(combined.selected==1);em_ops_move(&combined,10000);assert(combined.selected==1);
 int filter=-1;for(size_t i=0;i<v.seats.size();i++)if(v.seats[i].action==OPSFILTER&&v.seats[i].arg==1)filter=int(i);assert(filter>=0);em_ops_action(&combined,-1000-filter);em_ops(&combined);assert(combined.selected==5);save("special-console");em_ops_move(&combined,10000);assert(combined.selected==7);em_ops_move(&combined,1);assert(combined.selected==7);
 std::vector<unsigned char> still(1280*720*4),spin(still.size());glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,still.data());
 faction_angle_override=1.0f;save("special-spin");faction_angle_override=-1;glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,spin.data());
 int changed[2]={};for(int y=0;y<720;y++)for(int x=0;x<1280;x++)if(memcmp(&still[(y*1280+x)*4],&spin[(y*1280+x)*4],4)){
  bool seated=false;for(const Seat &q:v.seats)if(q.badge>=0){float bx=v.x+(q.x+6)*v.scale,by=v.y+(q.y+2)*v.scale;
   if(x>=int(bx)&&x<int(ceilf(bx+28*v.scale))&&720-1-y>=int(by)&&720-1-y<int(ceilf(by+28*v.scale))){changed[q.badge]++;seated=true;}}
  assert(seated);
 }assert(changed[0]>10&&changed[1]>10);printf("ENMENU|spin|GDI=%d|Nod=%d|outside-seats=0\n",changed[0],changed[1]);
 for(int i=0;i<3;i++){em_choice(i,0,-1);save((std::string("choice-")+std::to_string(i)).c_str());}
 title_settings(win,p);skirmish_dropdown(&s,p);ai_difficulty_rows(&s,p,"td");
 sk_prev_free(pr);db_pack_free(p);
 ra_pass();
 SDL_GL_DeleteContext(gl);SDL_DestroyWindow(win);SDL_Quit();puts("ENMENU|PASS|native seats, all screens, source sliders/actions, joiner permissions");return 0;
}
