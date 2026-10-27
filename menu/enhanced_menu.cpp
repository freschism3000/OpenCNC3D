/* Enhanced menu v06. Accepted native artwork, original font, existing controllers.
   No Lab room/save fixtures and no replacement network or save implementation. */
#include "enhanced_menu.h"
#include "enhanced_menu_font.h"
extern "C" {
#include "pngwrite.h"
}
#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
/* GL 1.2's edge clamp, which every Windows driver supports but Windows' own gl.h, frozen at
   GL 1.1, does not name. */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include "../art/interface/enhanced-menu-logos-v01/menu_faction_meshes.h"
#include "../art/interface/ra-enhanced-menu-v01/emblems/menu_ra_emblem_meshes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "enhanced_menu_cursor.h"

namespace {
struct Art { int w=0,h=0; std::vector<unsigned char> pixels; };
struct Seat {
 int item=0,action=0,arg=0,x=0,y=0,w=0,h=0,lx=0,ly=0,lw=0,lh=0,kind=0;
 int value=0,min=0,max=1,step=1,badge=-1; bool disabled=false,active=false;
 std::string label,icon;
};
/* PLATE: a faction emblem on its steel tile (Red Alert's details plates), never pressed. */
enum {BUTTON, CHECK, RANGE, FIELD, ROW, COLOUR, PLATE};
enum {OPT=1,MAIN,OPS,LOBBY,BROWSER,DATABASE,CHOICE,SIDE,DEBRIEF};
enum {TAB=1,RESET,BACK,RANGESET,DROP,DROPCHOICE,MODE,DISP,OPSTOP,OPSFILTER,
 MAPROW,SEATDONE,SEND,MPSETUP,MPHANDLE,OPTROW,SKRANGE,OPTITEM,SKITEM,SKRANDOM,SKCOUNTRY,RARANGE,RACHECK,
 SETRANGE,GPCHECK,LANGDROP,LANGPICK,LANGGO,LANGCANCEL,LANGCLOSE};
struct View {
 int kind=0,w=0,h=0,scale=1,x=0,y=0,fbw=0,fbh=0,mousex=-1000,mousey=-1000;
 int held=-1,drag=-1,section=0,sksection=0,opsfilter=-1,opstop=0,drop=-1;
 int page=-1,lastselected=-1; const void *owner=nullptr;
 int popupFirst=-1,popupOwner=-1,popupChoice=-1,popupX=0,popupY=0,popupW=0,popupH=0;
 bool title=false,compact=false;
 /* the language box: the language it offers (-1 none), and whether its download was started */
 int langBox=-1; bool langStarted=false;
 std::vector<Seat> seats; std::vector<unsigned char> rgba;
 std::string frame,popupHint;
} v;
bool option_title=false;
const EM_LangHooks *langhooks=nullptr;   /* the language row's answers (em_set_language_hooks) */
std::map<std::string,Art> arts;
int loadstate=0; GLuint texture=0,bgtexture=0,logotexture=0,legaltexture=0;
const DB_Pack *menu_pack=nullptr;
int pointerx=-1000,pointery=-1000;
std::string sound_hover;
std::string legal_text;const DB_Pack *legal_pack=nullptr;
/* Body text, secondary text and accents. Tiberian Dawn's are these three; Red Alert's
   come from its pack, per palette (see Theme). */
unsigned TEXT=0xe6e5da, MUTED=0xaeb5ac, ACCENT=0x6ddf4c;
/* WHICH GAME'S KIT. One process draws one game's menus: Tiberian Dawn's v06 art, or Red
   Alert's (the ra-enhanced-menu-v01 design, baked by bake_ra_enhanced_menu.py from the 1996
   data into ra3d/enhanced-menu.pack). Red Alert's dialogs are red at the title and blue in a
   mission, as the 1996 game draws them in its title and scenario palettes, so each of its
   frame and button arts carries the palette it was baked in. */
struct Theme {
 bool ra=false,blue=false,soviet=false;   // soviet: the mission being played is a Soviet one
 unsigned slot[16]={};          // the pack's theme-red / theme-blue colours, in Slot order
 std::string version,legal;     // the main menu's version line and the footer's notice
};
enum Slot{S_TEXT,S_MUTED,S_ACCENT,S_WELLLIT,S_ROWON,S_ROWHOVER,S_RAMP0,S_OVERLAY=14,S_TRACK=15};
Theme theme;
std::vector<int> pack_advance;  // Red Alert's GRAD6FNT advance table, read from its pack
const int *advance=font_advance;int glyphs=226;
std::string art_id(const std::string &id){return theme.ra?id+(theme.blue?"-blue":"-red"):id;}
void theme_colours(){
 if(!theme.ra)return;
 auto it=arts.find(theme.blue?"theme-blue":"theme-red");if(it==arts.end())return;
 for(int i=0;i<16&&i<it->second.w;i++){const unsigned char *p=&it->second.pixels[i*4];theme.slot[i]=(p[0]<<16)|(p[1]<<8)|p[2];}
 TEXT=theme.slot[S_TEXT];MUTED=theme.slot[S_MUTED];ACCENT=theme.slot[S_ACCENT];
}
unsigned read32(FILE *f){unsigned char b[4];if(fread(b,1,4,f)!=4)return 0;return b[0]|unsigned(b[1])<<8|unsigned(b[2])<<16|unsigned(b[3])<<24;}
bool load(){
 if(loadstate)return loadstate>0; loadstate=-1;
 FILE *f=fopen(theme.ra?"ra3d/enhanced-menu.pack":"enhanced-menu.pack","rb");if(!f)return false;
 char magic[8];if(fread(magic,1,8,f)!=8||memcmp(magic,theme.ra?"ENMENURA":"ENMENU06",8)){fclose(f);return false;}
 unsigned count=read32(f);if(count>(theme.ra?1024u:256u)){fclose(f);return false;}
 std::map<std::string,Art> loaded;
 for(unsigned i=0;i<count;i++){
  char name[64];if(fread(name,1,64,f)!=64||!memchr(name,0,64)){fclose(f);return false;}
  Art a;a.w=read32(f);a.h=read32(f);unsigned bytes=read32(f);
  if(a.w<1||a.h<1||a.w>4096||a.h>2048||bytes!=unsigned(a.w*a.h*4)){fclose(f);return false;}
  a.pixels.resize(bytes);if(fread(a.pixels.data(),1,bytes,f)!=bytes){fclose(f);return false;}
  loaded[name]=std::move(a);
 }
 fclose(f);if(!loaded.count("font_menu")||!loaded.count(theme.ra?"lobby-frame-red":"lobby-frame"))return false;
 if(theme.ra){
  auto ad=loaded.find("font_advance");if(ad==loaded.end()||ad->second.w*8!=loaded.at("font_menu").w)return false;
  pack_advance.assign(ad->second.w,0);for(int i=0;i<ad->second.w;i++)pack_advance[i]=ad->second.pixels[i*4];
  advance=pack_advance.data();glyphs=ad->second.w;
 }
 arts.swap(loaded);loadstate=1;fprintf(stderr,"ENMENU|loaded|%s|assets=%zu\n",theme.ra?"red-alert-v01":"v06",arts.size());return true;
}
void pixel(int x,int y,unsigned col,int alpha=255){
 if(x<0||y<0||x>=v.w||y>=v.h)return;
 unsigned char *d=&v.rgba[(y*v.w+x)*4];unsigned r=(col>>16)&255,g=(col>>8)&255,b=col&255;
 if(alpha==255){d[0]=r;d[1]=g;d[2]=b;d[3]=255;return;}
 int a=d[3],out=alpha+a*(255-alpha)/255;if(!out)return;
 d[0]=(r*alpha+d[0]*a*(255-alpha)/255)/out;
 d[1]=(g*alpha+d[1]*a*(255-alpha)/255)/out;
 d[2]=(b*alpha+d[2]*a*(255-alpha)/255)/out;d[3]=out;
}
void rect(int x,int y,int w,int h,unsigned col,int alpha=255){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)pixel(xx,yy,col,alpha);}
void blit(const Art &a,int x,int y,int scale=1){
 for(int yy=0;yy<a.h;yy++)for(int xx=0;xx<a.w;xx++){
  const unsigned char *p=&a.pixels[(yy*a.w+xx)*4];if(!p[3])continue;
  rect(x+xx*scale,y+yy*scale,scale,scale,(p[0]<<16)|(p[1]<<8)|p[2],p[3]);
 }
}
void image(const std::string &id,int x,int y){auto it=arts.find(id);if(it!=arts.end())blit(it->second,x,y);}
int textwidth(const std::string &s){int w=0;for(unsigned char c:s)if(c<glyphs)w+=std::max(0,advance[c])*2;return w;}
std::string fitted(std::string s,int w){if(textwidth(s)<=w)return s;while(!s.empty()&&textwidth(s+"..")>w)s.pop_back();return s+"..";}
void text(const std::string &s,int x,int y,unsigned col=TEXT,bool center=false){
 if(center)x-=textwidth(s)/2;const Art &a=arts.at("font_menu");
 for(unsigned char c:s){if(c>=glyphs)continue;
  for(int yy=0;yy<8;yy++)for(int xx=0;xx<8;xx++){
   const unsigned char *p=&a.pixels[(yy*a.w+c*8+xx)*4];if(!p[3])continue;
   bool shadow=p[0]<64&&p[1]<64&&p[2]<64;rect(x+xx*2,y+yy*2,2,2,shadow?0:col,p[3]);
  }
  x+=std::max(0,advance[c])*2;
 }
}
std::vector<std::string> wrapped(const std::string &s,int width){
 std::vector<std::string> out;std::string line,word;
 auto flushword=[&](){if(word.empty())return;
  if(!line.empty()&&textwidth(line+" "+word)>width){out.push_back(line);line.clear();}
  while(textwidth(word)>width){size_t n=1;while(n<word.size()&&textwidth(word.substr(0,n+1))<=width)n++;
   if(!line.empty()){out.push_back(line);line.clear();}out.push_back(word.substr(0,n));word.erase(0,n);}
  if(!word.empty()){if(!line.empty())line+=' ';line+=word;}word.clear();};
 for(char c:s){if(c==' '||c=='\n'||c=='\r'||c=='\t'){flushword();if(c=='\n'){out.push_back(line);line.clear();}}else word+=c;}
 flushword();if(!line.empty())out.push_back(line);return out;
}
void note(const std::string &s,int x,int y,int width,int maxlines=99,unsigned col=MUTED,int pitch=26,bool newest=false){
 auto lines=wrapped(s,width);int start=newest?std::max(0,int(lines.size())-maxlines):0;
 for(int i=start;i<int(lines.size())&&i<start+maxlines;i++)text(lines[i],x,y+(i-start)*pitch,col);
}
/* Red Alert's well: the field shows through, darkened, inside the same cut-corner liner;
   a popup's well is solid so the list it carries reads over whatever it covers. */
void ra_well(const char *title,int x,int y,int w,int h,bool overlay){
 const unsigned dark=0x060202,lit=theme.slot[S_WELLLIT];
 if(overlay)rect(x,y,w,h,theme.slot[S_OVERLAY]);
 else for(int yy=std::max(0,y);yy<std::min(v.h,y+h);yy++)for(int xx=std::max(0,x);xx<std::min(v.w,x+w);xx++){
  unsigned char *d=&v.rgba[(yy*v.w+xx)*4];for(int c=0;c<3;c++)d[c]=(unsigned char)(d[c]*.36f);}
 rect(x+4,y,w-8,2,dark);rect(x,y+4,2,h-8,dark);rect(x+w-2,y+4,2,h-8,dark);rect(x+4,y+h-2,w-8,2,dark);
 for(int i=0;i<4;i++){pixel(x+3-i,y+i,dark);pixel(x+w-4+i,y+i,dark);pixel(x+3-i,y+h-1-i,dark);pixel(x+w-4+i,y+h-1-i,dark);}
 rect(x+4,y+h-2,w-8,1,lit,200);rect(x+w-2,y+5,1,h-10,lit,200);
 if(title&&*title){text(title,x+18,y+14,ACCENT);rect(x+16,y+40,w-32,1,0x040202);rect(x+16,y+41,w-32,1,lit,160);}
}
void well(const char *title,int x,int y,int w,int h,bool overlay=false){
 if(theme.ra){ra_well(title,x,y,w,h,overlay);return;}
 if(overlay)rect(x,y,w,h,0x101310);
 rect(x+4,y,w-8,2,0x080a09);rect(x,y+4,2,h-8,0x080a09);
 rect(x+w-2,y+4,2,h-8,0x080a09);rect(x+4,y+h-2,w-8,2,0x080a09);
 for(int i=0;i<4;i++){pixel(x+3-i,y+i,0x080a09);pixel(x+w-4+i,y+i,0x080a09);pixel(x+3-i,y+h-1-i,0x080a09);pixel(x+w-4+i,y+h-1-i,0x080a09);}
 rect(x+4,y+h-2,w-8,1,0x4a4b42);rect(x+w-2,y+5,1,h-10,0x4a4b42);
 if(title&&*title){text(title,x+18,y+14,ACCENT);rect(x+16,y+40,w-32,1,0x0b0d0b);rect(x+16,y+41,w-32,1,0x353930);}
}
void begin(int kind,const void *owner,const char *frame,const std::string &title){
 if(!load())return;
 if(v.kind!=kind||v.owner!=owner){v.held=v.drag=-1;v.drop=v.popupChoice=-1;v.page=-1;v.lastselected=-1;
  if(kind==OPT)v.section=0;if(kind==LOBBY)v.sksection=0;
  if(kind==OPS){v.opsfilter=-1;v.opstop=0;}}
 v.kind=kind;v.owner=owner;v.frame=frame;v.title=kind==MAIN||(kind==OPT&&option_title);
 /* Red Alert's small boxes (a confirmation, a notice) keep their own size at the title and
    stand in the middle of the screen, as its exit box does. */
 if(theme.ra&&kind==OPT&&!strcmp(frame,"confirm-frame"))v.title=false;
 v.compact=kind==OPT&&v.title;
 /* In a mission: the pause dialog and everything it opens, and the Database tab. */
 theme.blue=theme.ra&&((kind==OPT&&!option_title)||kind==DATABASE);theme_colours();
 /* Red Alert bakes its title dialogs at the main menu's height rather than stretching rows,
    so its grid stays whole. */
 const std::string id=theme.ra&&v.compact?std::string(frame)+"-compact-red":art_id(frame);
 const Art &a=arts.at(id);v.w=a.w;v.h=v.compact?arts.at(art_id("main-frame")).h:a.h;
 v.rgba.assign(v.w*v.h*4,0);v.seats.clear();v.popupFirst=v.popupOwner=-1;v.popupHint.clear();
 if(v.h==a.h)image(id,0,0);
 else for(int row=0;row<v.h;row++){
  int sourceRow=row;
  if(v.h<a.h){if(row>=v.h/2)sourceRow+=a.h-v.h;}
  else if(row>=a.h/2)sourceRow=row<a.h/2+v.h-a.h?a.h/2+(row-a.h/2)%64:row-v.h+a.h;
  memcpy(&v.rgba[row*v.w*4],&a.pixels[sourceRow*a.w*4],v.w*4);
 }
 text(title,v.w/2,32,TEXT,true);
}
void popup(int owner,int left,int top,int width,int height){v.popupFirst=int(v.seats.size());v.popupOwner=owner;v.popupX=left;v.popupY=top;v.popupW=width;v.popupH=height;}
bool popup_contains(int pointX,int pointY){return pointX>=v.popupX&&pointY>=v.popupY&&pointX<v.popupX+v.popupW&&pointY<v.popupY+v.popupH;}
Seat &add(int item,const std::string &label,int x,int y,int w,int h=40,int kind=BUTTON,int action=0,int arg=0){
 Seat q;q.item=item;q.label=label;q.x=x;q.y=y;q.w=w;q.h=h;q.kind=kind;q.action=action;q.arg=arg;
 v.seats.push_back(q);return v.seats.back();
}
void legacy(Seat &q,int x,int y,int w,int h){q.lx=x;q.ly=y;q.lw=w;q.lh=h;}
Seat &opt(DOPT_State *st,int item,const std::string &label,int x,int y,int w,int h=40,int kind=BUTTON){
 Seat &q=add(item,label,x,y,w,h,kind);int rx=0,ry=0,rw=0,rh=0;
 if(st->page==DOPT_PAGE_ADVANCED&&item<DOPT_VE_COUNT&&item>=DOPT_VE_GLOBAL_COUNT)dopt_adv_reveal(st,item);
 if(dopt_item_rect(st,item,&rx,&ry,&rw,&rh))legacy(q,rx,ry,rw,rh);else q.disabled=true;
 q.disabled=q.disabled||dopt_item_disabled(st,item);q.active=st->selected==item;return q;
}
Seat &sk(const SK_State *st,int item,const std::string &label,int x,int y,int w,int h=40,int kind=BUTTON){
 Seat &q=add(item,label,x,y,w,h,kind);int rx=0,ry=0,rw=0,rh=0;
 if(sk_item_rect(st,item,&rx,&ry,&rw,&rh))legacy(q,rx,ry,rw,rh);else q.disabled=true;
 q.disabled=q.disabled||sk_item_disabled(st,item);q.active=st->selected==item;return q;
}
Seat &mp(const MP_State *st,int item,const std::string &label,int x,int y,int w,int h=40,int kind=BUTTON){
 Seat &q=add(item,label,x,y,w,h,kind);int rx=0,ry=0,rw=0,rh=0;
 if(mp_item_rect(st,item,&rx,&ry,&rw,&rh))legacy(q,rx,ry,rw,rh);else q.disabled=true;
 q.disabled=q.disabled||mp_item_disabled(st,item);q.active=st->hover==item;return q;
}
/* Red Alert's fields, rows, checks and sliders: the Tiberian Dawn geometry in the dialog's
   own ramp, steel and gold. Returns false for the kinds both games draw from button art. */
bool paint_ra(const Seat &q,bool hover,bool down,unsigned col){
 const unsigned *T=theme.slot;
 if(q.kind==PLATE){image("plate",q.x,q.y);return true;}
 if(q.kind==FIELD){rect(q.x,q.y,q.w,q.h,0x060303);rect(q.x,q.y,q.w,1,q.active?ACCENT:0x807870);rect(q.x,q.y,1,q.h,0x807870);rect(q.x,q.y+q.h-1,q.w,1,0x342826);rect(q.x+q.w-1,q.y,1,q.h,0x342826);
  text(fitted(q.label,q.w-24),q.x+12,q.y+12,col);if(q.active&&SDL_GetTicks()%800<400)rect(std::min(q.x+12+textwidth(q.label),q.x+q.w-14),q.y+10,1,19,ACCENT);return true;}
 if(q.kind==ROW){if(q.active||hover){rect(q.x,q.y,q.w,q.h,q.active?T[S_ROWON]:T[S_ROWHOVER]);rect(q.x,q.y,2,q.h,ACCENT);}text(fitted(q.label,q.w-(q.badge<0?24:56)),q.x+(q.badge<0?12:44),q.y+(q.h-16)/2,col);return true;}
 if(q.kind==CHECK){if(hover&&!q.disabled)rect(q.x,q.y,q.w,q.h,T[S_ROWHOVER]);int x=q.x+2,y=q.y+7;
  rect(x,y,22,22,0x969696);rect(x+1,y+1,20,20,0x080404);rect(x+1,y+1,20,1,0x3c3c3c);rect(x+2,y+20,18,1,0x281e1e);rect(x+20,y+2,1,18,0x281e1e);
  if(q.value){int pts[][2]={{5,10},{7,12},{9,14},{11,12},{13,10},{15,8},{17,6}};for(auto &p:pts)rect(x+p[0]-1,y+p[1]-1,3,3,q.disabled?0x968264:ACCENT);}
  text(fitted(q.label,q.w-44),q.x+38,q.y+10,col);return true;}
 if(q.kind==RANGE){int fill=int((q.w-28)*double(q.value-q.min)/std::max(1,q.max-q.min));int x=q.x+14,y=q.y+14,w=q.w-28;
  rect(x-2,y-2,w+4,12,0x060202);rect(x,y+9,w,1,0x5a3c38);rect(x+w+1,y,1,9,0x5a3c38);rect(x,y,w,8,T[S_TRACK]);
  rect(x,y,fill,8,q.disabled?0x5a4642:hover?T[S_RAMP0+2]:T[S_RAMP0+4]);rect(x,y,fill,1,q.disabled?0x968c82:ACCENT);
  image(art_id(std::string("button-28-28-")+(down?"pressed":hover?"hover":"normal")),q.x+fill,q.y+4);return true;}
 return false;
}
void paint(){
 for(int n=0;n<int(v.seats.size());n++){
  if(n==v.popupFirst)well("",v.popupX,v.popupY,v.popupW,v.popupH,true);
  const Seat &q=v.seats[n];bool hover=(v.popupFirst<0||n>=v.popupFirst||n==v.popupOwner)&&v.mousex>=q.x&&v.mousey>=q.y&&v.mousex<q.x+q.w&&v.mousey<q.y+q.h;
  bool down=v.held==n;unsigned col=q.disabled?MUTED:TEXT;
  if(theme.ra&&paint_ra(q,hover,down,col))continue;
  if(q.kind==FIELD){rect(q.x,q.y,q.w,q.h,0x080a09);rect(q.x,q.y,q.w,1,q.active?0xb19a63:0x606255);rect(q.x,q.y,1,q.h,0x606255);rect(q.x,q.y+q.h-1,q.w,1,0x353b30);rect(q.x+q.w-1,q.y,1,q.h,0x353b30);text(fitted(q.label,q.w-24),q.x+12,q.y+12,col);if(q.active&&SDL_GetTicks()%800<400)rect(std::min(q.x+12+textwidth(q.label),q.x+q.w-14),q.y+10,1,19,0xb5cf91);continue;}
  if(q.kind==ROW){if(q.active||hover){rect(q.x,q.y,q.w,q.h,q.active?0x273521:0x30342b);rect(q.x,q.y,2,q.h,0x637450);}text(fitted(q.label,q.w-(q.badge<0?24:56)),q.x+(q.badge<0?12:44),q.y+(q.h-16)/2,col);continue;}
  if(q.kind==CHECK){if(hover&&!q.disabled)rect(q.x,q.y,q.w,q.h,0x353c30);int x=q.x+2,y=q.y+7;
   rect(x,y,22,22,0x787b6b);rect(x+1,y+1,20,20,0x10130f);rect(x+2,y+20,18,1,0x373b32);rect(x+20,y+2,1,18,0x373b32);
   if(q.value){int pts[][2]={{5,10},{7,12},{9,14},{11,12},{13,10},{15,8},{17,6}};for(auto &p:pts)rect(x+p[0]-1,y+p[1]-1,3,3,q.disabled?0x797f73:0x72df4c);}
   text(fitted(q.label,q.w-44),q.x+38,q.y+10,col);continue;}
  if(q.kind==RANGE){int fill=int((q.w-28)*double(q.value-q.min)/std::max(1,q.max-q.min));int x=q.x+14,y=q.y+14,w=q.w-28;
   rect(x-2,y-2,w+4,12,0x080a08);rect(x,y+9,w,1,0x55574c);rect(x+w+1,y,1,9,0x55574c);rect(x,y,w,8,0x1b2618);
   rect(x,y,fill,8,q.disabled?0x41483b:hover?0x7ddb45:0x527d35);rect(x,y,fill,1,0xa5c777);
   image(std::string("button-28-28-")+(down?"pressed":hover?"hover":"normal"),q.x+fill,q.y+4);continue;}
  std::string state=q.disabled?"disabled":down?"pressed":hover?(q.active?"activehover":"hover"):q.active?"active":"normal";
  std::string id=art_id("button-"+std::to_string(q.w)+"-"+std::to_string(q.h)+"-"+state);
  if(arts.count(id))image(id,q.x,q.y);else {rect(q.x,q.y,q.w,q.h,0x292d28);rect(q.x+2,q.y,q.w-4,2,0x858b7d);rect(q.x,q.y+2,2,q.h-4,0x666a60);rect(q.x+2,q.y+q.h-2,q.w-4,2,0x090b09);}
  if(q.kind==COLOUR){rect(q.x+7,q.y+7,14,14,q.value);continue;}
  if(!q.icon.empty()){auto it=arts.find(art_id(q.icon));if(it!=arts.end())blit(it->second,q.x+18,q.y+(q.h-it->second.h)/2);}
  text(fitted(q.label,q.w-(q.badge>=0?52:16)),q.x+q.w/2+(q.badge>=0?14:0)-(q.action==DROP?8:0),q.y+(q.h-16)/2+(down?1:0),col,true);
  if(q.action==DROP)for(int i=0;i<5;i++)rect(q.x+q.w-27+i,q.y+17+i,9-2*i,1,col);
 }
 if(!v.popupHint.empty())text(v.popupHint,v.popupX+16,v.popupY+v.popupH-26,MUTED);
}
int hit(int x,int y){
 for(int i=int(v.seats.size())-1;i>=0;i--){
  if(v.popupFirst>=0&&i<v.popupFirst&&i!=v.popupOwner)continue;
  const Seat &q=v.seats[i];if(x>=q.x&&y>=q.y&&x<q.x+q.w&&y<q.y+q.h)return q.disabled?-1:i;
 }
 return -1;
}
int legal_scale(int w,int h){return std::max(1,std::min(w/DM_LEGAL_W,h/480));}
void layout(int w,int h){v.fbw=w;v.fbh=h;v.scale=std::max(1,h/693);const int titleWidth=v.title?std::max(v.w,arts.at(theme.ra?"visuals-frame-red":"visuals-frame").w):v.w;while(v.scale>1&&(titleWidth*v.scale>w-40||v.h*v.scale>h-20))v.scale--;v.x=(w-v.w*v.scale)/2;v.y=(h-v.h*v.scale)/2+(v.title?52*v.scale:0);if(v.title)v.y=std::min(v.y,h-v.h*v.scale-DM_LEGAL_H*legal_scale(w,h));}
int virtualseat(int x){int n=-20000-x;return x<=-20000&&n>=0&&n<int(v.seats.size())?n:-1;}
void glquad(GLuint t,float x,float y,float w,float h,float u,float z){glBindTexture(GL_TEXTURE_2D,t);glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(x,y);glTexCoord2f(u,0);glVertex2f(x+w,y);glTexCoord2f(u,z);glVertex2f(x+w,y+h);glTexCoord2f(0,z);glVertex2f(x,y+h);glEnd();}
void arttexture(GLuint &tex,const Art &a){if(tex)return;int w=1,h=1;while(w<a.w)w*=2;while(h<a.h)h*=2;std::vector<unsigned char> padded(w*h*4,0);for(int y=0;y<a.h;y++)memcpy(&padded[y*w*4],&a.pixels[y*a.w*4],a.w*4);glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,padded.data());}
/* Red Alert's title is drawn from its HIRES plate where that fits at least twice, from the
   LORES plate below that, so a small window does not show a 640 x 400 title mostly hidden. */
GLuint bgtexture_lo=0,logotexture_lo=0;
/* ONE SIZE FOR THE BACKDROP ON EVERY SCREEN. The title fitted the plate (the largest whole
   multiple inside the window) and every other screen covered the window with it, so opening
   the lobby, the Special Ops list, a confirm box or a notice made the picture jump larger
   (Red Alert at 1920x1080: 1280x800 to 1920x1200) and closing it shrank it again. Every
   screen fits it, as the title does, in both games. */
void backdrop(int w,int h){const bool lo=theme.ra&&std::min(w/640,h/400)<2;const Art &a=arts.at(lo?"title-backdrop-lo":"title-backdrop");arttexture(lo?bgtexture_lo:bgtexture,a);GLuint tex=lo?bgtexture_lo:bgtexture;int s=std::max(1,std::min(w/a.w,h/a.h)),pw=1,ph=1;while(pw<a.w)pw*=2;while(ph<a.h)ph*=2;glquad(tex,(w-a.w*s)/2,(h-a.h*s)/2,a.w*s,a.h*s,float(a.w)/pw,float(a.h)/ph);}
void preview(const SK_State *st,const DB_Pack *p,int ix,int x,int y,int size){
 /* Red Alert's map pictures are in its kit (preview-SCMnnEA, baked from ra3d/previews),
    drawn at whole multiples of their size */
 if(theme.ra&&ix>=0&&ix<st->count&&st->maps[ix].scen){auto it=arts.find(std::string("preview-")+st->maps[ix].scen);
  if(it!=arts.end()){const Art &a=it->second;const int k=std::max(1,size/std::max(1,a.w));blit(a,x+(size-a.w*k)/2,y+(size-a.h*k)/2,k);return;}}
 Art a;a.w=a.h=size;a.pixels.resize(size*size*4);if(sk_preview_rgba(st,p,ix,a.pixels.data(),size,size))blit(a,x,y);else note("No preview available",x,y+size/2,size,2);}
void footer(const char *primary,int item,int backitem,DOPT_State *st){opt(st,backitem,"Back",38,v.h-86,192);opt(st,item,primary,v.w-258,v.h-86,220);}
} // namespace

extern "C" void em_sound_event(const SDL_Event *e,SDL_Window *window){
 if(!e||!window||!v.w)return;
 if(e->type==SDL_WINDOWEVENT&&e->window.event==SDL_WINDOWEVENT_LEAVE){sound_hover.clear();return;}
 bool motion=e->type==SDL_MOUSEMOTION,press=e->type==SDL_MOUSEBUTTONDOWN&&e->button.button==SDL_BUTTON_LEFT;
 if(!motion&&!press)return;
 int ww,wh;SDL_GetWindowSize(window,&ww,&wh);if(ww<=0||wh<=0)return;
 int fx=(motion?e->motion.x:e->button.x)*v.fbw/ww,fy=(motion?e->motion.y:e->button.y)*v.fbh/wh;
 int n=hit((fx-v.x)/v.scale,(fy-v.y)/v.scale);
 std::string key;
 if(fx>=v.x&&fy>=v.y&&n>=0&&v.seats[n].kind!=FIELD){
  const Seat &q=v.seats[n];
  key=std::to_string(v.kind)+":"+std::to_string((uintptr_t)v.owner)+":"+std::to_string(q.item)+":"+std::to_string(q.action)+":"+std::to_string(q.arg)+":"+q.label;
  if(press||key!=sound_hover)game_enhanced_menu_sound(press?1:0);
 }
 sound_hover=key;
}

extern "C" void em_menu_overlay(const DB_Pack *pack,int x,int y){menu_pack=pack;pointerx=x;pointery=y;}
extern "C" void em_title_mode(int enabled){option_title=enabled!=0;}
extern "C" void em_set_language_hooks(const EM_LangHooks *hooks){langhooks=hooks;}
extern "C" int em_density(void){return v.scale;}
extern "C" int em_available(void){return load();}
namespace {
const int main_order[]={DM_START,DM_SPECIAL,DM_USERMAPS,DM_LOAD,DM_MULTIPLAYER,DM_SKIRMISH,DM_VISUALS,DM_INTRO,DM_EXIT};
const int main_y[]={64,108,152,196,260,304,368,412,456};
}
extern "C" int em_main_next(const DM_State *st,int item,int delta){
 int at=0;for(int i=0;i<DM_ITEM_COUNT;i++)if(main_order[i]==item)at=i;
 for(int i=0;i<DM_ITEM_COUNT;i++){at=(at+(delta<0?DM_ITEM_COUNT-1:1))%DM_ITEM_COUNT;if(!dm_state_item_disabled(st,main_order[at]))return main_order[at];}
 return item;
}
extern "C" void em_main(const DM_State *st){
 if(!load())return;begin(MAIN,st,"main-frame","MAIN MENU");
 for(int n=0;n<DM_ITEM_COUNT;n++){int i=main_order[n];const char *label=i==DM_SPECIAL?(theme.ra?"Special Ops":"Special Ops / Covert Ops"):i==DM_VISUALS?"Settings":dm_item_label(i);Seat &q=add(i,label,36,main_y[n],500);q.disabled=dm_state_item_disabled(st,i);q.active=st->selected==i;int x,y,w,h;if(dm_item_rect(st,i,&x,&y,&w,&h))legacy(q,x,y,w,h);}
 if(theme.ra)for(int y:{246,354}){rect(38,y,496,2,0x060202);rect(38,y+2,496,1,theme.slot[S_WELLLIT]);rect(38,y+3,496,1,0x140402);}
 else for(int y:{246,354}){rect(38,y,496,2,0x070908);rect(38,y+2,496,1,0x43453d);rect(38,y+3,496,1,0x181b17);}
 /* Red Alert's own menu state carries the 1996 menu's "RA3D" line; Enhanced names the build. */
 text(std::string("ENHANCED / ")+(theme.ra?theme.version.c_str():st->version?st->version:""),v.w/2,516,ACCENT,true);paint();
}
extern "C" void em_to_legacy(int fx,int fy,int *x,int *y){
 v.mousex=(fx-v.x)/v.scale;v.mousey=(fy-v.y)/v.scale;
 if(fx<v.x||fy<v.y){v.mousex=v.mousey=-1000;}
 int n=v.drag>=0?v.drag:hit(v.mousex,v.mousey);
 if(n<0){*x=*y=-1000;return;}const Seat &q=v.seats[n];
 if(q.action){*x=-20000-n;*y=std::max(0,std::min(1000,(v.mousex-q.x-14)*1000/std::max(1,q.w-28)));}
 else {*x=q.lx+q.lw/2;*y=q.ly+q.lh/2;}
}
extern "C" int em_item_rect(int item,int *x,int *y,int *w,int *h){
 for(const Seat &q:v.seats)if(q.item==item){*x=v.x+q.x*v.scale;*y=v.y+q.y*v.scale;*w=q.w*v.scale;*h=q.h*v.scale;return 1;}return 0;
}
extern "C" void em_viewport(int width,int height){layout(width,height);}
/* The framebuffer centre of the live seat with this label (letter case ignored), for a
   scripted click that takes the pointer's own path. */
extern "C" int em_seat_point(const char *label,int *x,int *y){
 auto same=[](const std::string &a,const char *b){size_t i=0;for(;i<a.size()&&b[i];i++)if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i]))return false;return i==a.size()&&!b[i];};
 for(const Seat &q:v.seats)if(!q.disabled&&label&&same(q.label,label)){*x=v.x+(q.x+q.w/2)*v.scale;*y=v.y+(q.y+q.h/2)*v.scale;return 1;}
 return 0;
}
namespace {
/* Render the unchanged website model at native row density. Hidden captures use a
   fixed angle; normal menus turn once in about 31 seconds. UI geometry owns the seat. */
float faction_angle_override=-1.0f;
/* One emblem: triangles of 24 floats (three corners of position, normal and UV), a
   material per triangle, and that many 32 x 32 nearest-sampled textures. */
struct Emblem{const float *mesh;int tris;const unsigned char *mats;const unsigned char *tex[4];int ntex;};
const Emblem td_emblems[2]={
 {menu_logo_gdi,int(sizeof(menu_logo_gdi)/sizeof(float)/24),menu_logo_gdi_materials,{menu_logo_gdi_texture0,menu_logo_gdi_texture1,nullptr,nullptr},2},
 {menu_logo_nod,int(sizeof(menu_logo_nod)/sizeof(float)/24),menu_logo_nod_materials,{menu_logo_nod_texture0,menu_logo_nod_texture1,menu_logo_nod_texture2,nullptr},3}};
const Emblem ra_emblems[2]={
 {menu_ra_allied,int(sizeof(menu_ra_allied)/sizeof(float)/24),menu_ra_allied_materials,
  {menu_ra_allied_textures,menu_ra_allied_textures+1*32*32*4,menu_ra_allied_textures+2*32*32*4,menu_ra_allied_textures+3*32*32*4},menu_ra_allied_texture_count},
 {menu_ra_soviet,int(sizeof(menu_ra_soviet)/sizeof(float)/24),menu_ra_soviet_materials,
  {menu_ra_soviet_textures,menu_ra_soviet_textures+1*32*32*4,menu_ra_soviet_textures+2*32*32*4,menu_ra_soviet_textures+3*32*32*4},menu_ra_soviet_texture_count}};
void faction_badges(){
 float angle=faction_angle_override>=0?faction_angle_override:SDL_getenv("OPENCNC3D_BACKGROUND")?.25f:SDL_GetTicks()*.0002f;
 const Emblem *em=theme.ra?ra_emblems:td_emblems;
 static GLuint tex[2][2][4]={};GLuint (*t)[4]=tex[theme.ra?1:0];
 for(int f=0;f<2;f++)for(int m=0;m<em[f].ntex;m++)if(!t[f][m]){glGenTextures(1,&t[f][m]);glBindTexture(GL_TEXTURE_2D,t[f][m]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,em[f].tex[m]);}
 float c=cosf(angle),s=sinf(angle),a=cosf(.34f),b=sinf(.34f);
 auto rotate=[=](const float *p,float *out){float u=c*p[0]+s*p[2],w=-s*p[0]+c*p[2];out[0]=u;out[1]=a*p[1]-b*w;out[2]=b*p[1]+a*w;};
 /* Smooth shading is asked for, not assumed: the movie player and the 1995 menu both leave
    GL_FLAT behind, which drew the badges faceted after the intro. em_draw's attribute push
    restores whatever was set before. */
 GLint vp[4];glGetIntegerv(GL_VIEWPORT,vp);glShadeModel(GL_SMOOTH);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glEnable(GL_SCISSOR_TEST);glClearDepth(1);
 std::vector<Seat> titlebadges;
 if(v.kind==MAIN){
  int size=std::min(200,std::max(0,(v.fbw-v.w*v.scale)/2/v.scale-36));
  if(size>=80)for(int f=0;f<2;f++){Seat q;q.badge=f;q.w=size;
   q.x=f?v.w+(v.fbw-v.x-v.w*v.scale)/v.scale/2-size/2:-v.x/v.scale/2-size/2;
   q.y=(v.fbh-v.y-std::max(36*v.scale,DM_LEGAL_H*legal_scale(v.fbw,v.fbh)+8))/v.scale-size;titlebadges.push_back(q);}
 }
 const std::vector<Seat> &badges=v.kind==MAIN?titlebadges:v.seats;
 for(const Seat &q:badges)if(q.badge>=0){int f=q.badge;
  /* the seat's square: the main menu's outer pair, a details plate's well, or a 28 pixel badge
     at the left of a row or (Red Alert, centred) a button */
  int ox=v.kind==MAIN?0:6,oy=v.kind==MAIN?0:2,side=v.kind==MAIN?q.w:28;
  if(q.kind==PLATE){ox=(q.w-(q.h-14))/2;oy=7;side=q.h-14;}else if(theme.ra&&q.kind==BUTTON&&v.kind!=MAIN)oy=(q.h-28)/2;
  float x=v.x+(q.x+ox)*v.scale,y=v.y+(q.y+oy)*v.scale,size=side*v.scale;
  glScissor(int(x),vp[3]-int(ceilf(y+size)),int(ceilf(size)),int(ceilf(size)));glClear(GL_DEPTH_BUFFER_BIT);
  for(int m=0;m<em[f].ntex;m++){glBindTexture(GL_TEXTURE_2D,t[f][m]);glBegin(GL_TRIANGLES);
   for(int i=0;i<em[f].tris;i++)if(em[f].mats[i]==m)for(int j=0;j<3;j++){
    const float *src=em[f].mesh+i*24+j*8;float p[3],n[3];rotate(src,p);rotate(src+3,n);
    float light=.36f+.7f*std::max(0.f,n[0]*.25f+n[1]*.5f+n[2]*.83f),shine=.42f*powf(std::max(0.f,n[0]*.13f+n[1]*.25f+n[2]*.96f),30.f);
    glColor3f(light+shine,light+shine,light+shine);glTexCoord2f(src[6],1-src[7]);glVertex3f(x+size/2+p[0]*size*.43f,y+size/2-p[1]*size*.43f,p[2]*.25f);
   }glEnd();
  }
 }
 glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glColor4f(1,1,1,1);
}
}
extern "C" void em_draw(int width,int height,int clear){
 if(!load()||!v.w)return;layout(width,height);
 glPushAttrib(GL_ALL_ATTRIB_BITS);glDisable(GL_SCISSOR_TEST);glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);glDisable(GL_FOG);
 if(clear){glDepthMask(GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDepthMask(GL_FALSE);}
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width,height,0,-1,1);glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glColor4f(1,1,1,1);
 if(clear)backdrop(width,height);else {glDisable(GL_TEXTURE_2D);glColor4f(0,0,0,.3f);glBegin(GL_QUADS);glVertex2i(0,0);glVertex2i(width,0);glVertex2i(width,height);glVertex2i(0,height);glEnd();glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);}
 if(clear&&v.title&&!theme.ra){const Art &a=arts.at("wordmark");arttexture(logotexture,a);int pw=1,ph=1;while(pw<a.w)pw*=2;while(ph<a.h)ph*=2;glquad(logotexture,(width-a.w*v.scale*2)/2,std::max(0,v.y-a.h*v.scale*2-16*v.scale),a.w*v.scale*2,a.h*v.scale*2,float(a.w)/pw,float(a.h)/ph);}
 /* Red Alert's lettering, lifted off its title: the HIRES cut at half the panel's scale when
    that scale is even, the LORES cut at the panel's scale when it is odd, so it always lands
    on whole pixels. */
 if(clear&&v.title&&theme.ra){const bool even=v.scale%2==0;const Art &a=arts.at(even?"wordmark":"wordmark-lo");GLuint &t=even?logotexture:logotexture_lo;arttexture(t,a);
  const int k=even?v.scale/2:v.scale;int pw=1,ph=1;while(pw<a.w)pw*=2;while(ph<a.h)ph*=2;
  glquad(t,(width-a.w*k)/2,std::max(0,v.y-a.h*k-16*v.scale),a.w*k,a.h*k,float(a.w)/pw,float(a.h)/ph);}
 if(!texture){glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2048,1024,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);}
 glBindTexture(GL_TEXTURE_2D,texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,v.w,v.h,GL_RGBA,GL_UNSIGNED_BYTE,v.rgba.data());
 glquad(texture,v.x,v.y,v.w*v.scale,v.h*v.scale,float(v.w)/2048,float(v.h)/1024);
 if(v.kind==DATABASE||v.kind==MAIN||(v.kind==OPS&&((const DO_State*)v.owner)->grouped))faction_badges();
 else if(theme.ra&&std::any_of(v.seats.begin(),v.seats.end(),[](const Seat &q){return q.badge>=0;}))faction_badges();
 /* The original live legal notice belongs below the title panel. Its original
    bitmap font is retained, with integer scaling and no baked-in wording. */
 if(clear&&v.kind==MAIN&&menu_pack){
  const DM_State *st=(const DM_State*)v.owner;
  /* Red Alert's menu state keeps an empty notice, because its 1996 title plate carries its
     own line; the Enhanced footer prints the theme's. */
  // RA3D title cleanup, 24 October 2026: no copyright below the main menu.
  const char *notice=theme.ra?nullptr:st->copyright;
  if(notice&&*notice){
   Art a;a.w=DM_LEGAL_W;a.h=DM_LEGAL_H;
   if(!legaltexture||legal_pack!=menu_pack||legal_text!=notice){
    if(theme.ra){
     /* Red Alert's footer in the menu's own GRAD6FNT at its native size, in capitals, each
        line centred in the same 640 x 28 strip, in the grey Tiberian Dawn's footer prints */
     a.pixels.assign(a.w*a.h*4,0);const Art &font=arts.at("font_menu");std::vector<std::string> lines;std::string line;
     for(const char *c=notice;;c++){if(!*c||*c=='\n'){lines.push_back(line);line.clear();if(!*c)break;}else line+=char(toupper((unsigned char)*c));}
     int y=(a.h-(8+(int(lines.size())-1)*10))/2;
     for(const std::string &l:lines){int x=(a.w-textwidth(l)/2)/2;
      for(unsigned char c:l){if(c>=glyphs)continue;for(int yy=0;yy<8;yy++)for(int xx=0;xx<8;xx++){const unsigned char *p=&font.pixels[(yy*font.w+c*8+xx)*4];if(!p[3]||x+xx<0||x+xx>=a.w||y+yy<0||y+yy>=a.h)continue;
        unsigned char *d=&a.pixels[((y+yy)*a.w+x+xx)*4];bool shadow=p[0]<64;d[0]=shadow?0:175;d[1]=shadow?0:183;d[2]=shadow?0:183;d[3]=255;}
       x+=std::max(0,advance[c]);}
      y+=10;}
    }else{
    std::vector<unsigned char> page(DM_LEGAL_W*DM_SCREEN_H*2*4,0);
    dm_draw_legal_rgba(page.data(),DM_LEGAL_W,DM_SCREEN_H*2,DM_LEGAL_W,menu_pack,notice,256);
    a.pixels.assign(page.end()-a.w*a.h*4,page.end());}
    if(legaltexture)glDeleteTextures(1,&legaltexture);legaltexture=0;arttexture(legaltexture,a);
    legal_pack=menu_pack;legal_text=notice;
   }
   int s=legal_scale(width,height);
   int pw=1,ph=1;while(pw<a.w)pw*=2;while(ph<a.h)ph*=2;
   glquad(legaltexture,(width-a.w*s)/2,height-a.h*s,a.w*s,a.h*s,float(a.w)/pw,float(a.h)/ph);
  }
 }
 if(clear==1&&pointerx>=0&&pointery>=0&&pointerx<width&&pointery<height)
  emcursor::draw(pointerx,pointery,v.scale);
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopAttrib();
}
extern "C" int em_write_shot(const char *path,int width,int height){
 if(!load()||!v.w)return 0;em_draw(width,height,1);std::vector<unsigned char> data(width*height*4),top(data.size());glReadBuffer(GL_BACK);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,data.data());for(int y=0;y<height;y++)memcpy(&top[y*width*4],&data[(height-1-y)*width*4],width*4);return png_write_rgba(path,top.data(),width,height);
}
extern "C" void em_reset(void){v=View();option_title=false;sound_hover.clear();}
/* Red Alert's kit for this process, before the first screen asks for the pack: the version
   line and the legal notice its screens print. */
extern "C" void em_set_red_alert(const char *version,const char *legal){
 if(!theme.ra){theme.ra=true;loadstate=0;arts.clear();}
 theme.version=version?version:"";theme.legal=legal?legal:"";
}
extern "C" int em_red_alert(void){return theme.ra;}
extern "C" void em_set_mission_side(int soviet){theme.soviet=soviet!=0;}
/* RED ALERT'S ROUTES THAT ARE NOT IN THE BUILD YET. Their buttons stay live and answer with
   a notice saying what is missing, until the engine side of each is connected and the host
   marks it ready. Tiberian Dawn asks none of this: every route is its own and ready. */
namespace {bool route_ready[EM_ROUTE_COUNT]={};}
extern "C" void em_set_route_ready(int route,int ready){if(route>=0&&route<EM_ROUTE_COUNT)route_ready[route]=ready!=0;}
extern "C" int em_route_ready(int route){return !theme.ra||(route>=0&&route<EM_ROUTE_COUNT&&route_ready[route]);}
extern "C" const char *em_route_notice(int route){
 switch(route){
 case EM_ROUTE_SAVES:return "Red Alert saves are kept apart from Tiberian Dawn's. That split is not in this build yet, so saving and loading wait for it.";
 case EM_ROUTE_SKIRMISH:return "Skirmish is not in this Red Alert build yet: it waits for Red Alert's countries, colours, tech levels and ore.";
 case EM_ROUTE_NETWORK:return "Multiplayer is not in this Red Alert build yet: its lobby room does not list Red Alert's maps and countries yet.";
 case EM_ROUTE_USERMAPS:return "Red Alert user maps cannot be started in this build yet.";
 default:return "";}
}
namespace {
/* The pause page's Load, Save and Delete open the slot list; for Red Alert, until its saves
   have their own index, the notice instead. */
bool saves_blocked(const DOPT_State *st,int item){return st->page==DOPT_PAGE_OPTIONS&&(item==DOPT_LOAD||item==DOPT_SAVE||item==DOPT_DELETE)&&!em_route_ready(EM_ROUTE_SAVES);}
}

namespace {
/* Both games use the same live world-detail switches. A checkbox reports the
   renderer's value; it must never substitute an unchecked, disabled seat. */
bool ra_unavailable(int item){(void)item;return false;}
const char *vis_label(int item){return theme.ra&&item==DOPT_VE_TIB3D?"3D Ore":dopt_vis_elem_label(item);}
/* Vertical sync has a third state, adaptive, ticked like on; the label is what tells them apart. */
void optcheck(DOPT_State *st,int item,int x,int y){Seat &q=opt(st,item,item==DOPT_VE_VSYNC&&st->vis.vsync==2?"Adaptive sync":vis_label(item),x,y,424,36,CHECK);q.action=OPTITEM;
 q.value=item==DOPT_VE_VSYNC?st->vis.vsync:item==DOPT_VE_FPS?st->vis.fps:st->vis.prof[st->advTab].elem[item];q.active=false;
 if(ra_unavailable(item)){q.disabled=true;q.value=0;}}
void optdrop(DOPT_State *st,int item,int x,int y){text(dopt_vis_elem_label(item),x,y-26);Seat &q=opt(st,item,dopt_drop_text(st,item,dopt_enh_drop_value(st,item)),x,y,424);q.action=DROP;q.arg=item;q.active=v.drop==item;}
void optrange(DOPT_State *st,int item,const char *label,int x,int y,int w,int value,int max,bool percent){
 text(label,x,y-28);std::string val=percent?std::to_string(value*100/max)+"%":(st->page==DOPT_PAGE_CONTROLS&&item==DOPT_C_SPEED)?dopt_speed_name(value):value==4?"Normal":std::to_string(value+1);
 text(val,x+w-textwidth(val),y-28,ACCENT);Seat &q=opt(st,item,label,x,y,w,36,RANGE);q.action=RANGESET;q.value=value;q.max=max;q.active=false;
}
void optback(DOPT_State *st,int x=550,int y=-1){Seat &q=add(-1,"Back",x,y<0?v.h-86:y,192);q.action=BACK;}
void optreset(){Seat &q=add(-1,"Reset to Defaults",38,v.h-86,292);q.action=RESET;}
/* A slider of the Settings screen's Gameplay and Audio tabs (dopt_enh_setting). Their seats
   are numbered EM_SETTING_SEAT - which and EM_GAMEPLAY_SEAT - toggle, so the gate can find
   them by number (em_item_rect) as it finds every other seat. */
void setrange(DOPT_State *st,int which,const char *label,int x,int y,int w,bool percent){
 const int value=dopt_enh_setting_value(st,which),max=dopt_enh_setting_max(which);
 text(label,x,y-28);std::string val=percent?std::to_string(value*100/max)+"%":which==DOPT_ES_SPEED?dopt_speed_name(value):value==4?"Normal":std::to_string(value+1);
 text(val,x+w-textwidth(val),y-28,ACCENT);Seat &q=add(EM_SETTING_SEAT-which,label,x,y,w,36,RANGE);q.action=SETRANGE;q.arg=which;q.value=value;q.max=max;q.active=false;
}
/* THE LANGUAGE ROW AND ITS BOX (EM_LangHooks, answered by the game). Picking a language
   whose pack is on the disk uses it at once; picking one that is not opens the box, which
   says what will be fetched and how much, fetches it with a bar on the screen when asked,
   and uses it the moment it is complete. Cancelling keeps what arrived, and the next try
   resumes it. */
std::string megabytes(long long b){char t[32];snprintf(t,sizeof t,"%.0f MB",double(b)/1e6+0.5);return t;}
void langrow(int x,int y){
 if(!langhooks||langhooks->count()<2)return;
 text("Language",x,y-28);const int cur=langhooks->current();
 Seat &q=add(EM_LANG_SEAT,langhooks->name(cur),x,y,424);q.action=LANGDROP;q.active=v.drop==EM_LANG_DROP;
 if(v.drop==EM_LANG_DROP){const int n=langhooks->count();int owner=int(v.seats.size())-1,top=y+44;
  if(top+n*42+8>v.h-106)top=std::max(78,y-n*42-12);popup(owner,x-4,top-4,432,n*42+8);
  for(int i=0;i<n;i++){std::string label=langhooks->name(i);if(!langhooks->installed(i))label+="  (download)";
   Seat &e=add(EM_LANG_SEAT-1-i,label,x,top+i*42,424);e.action=LANGPICK;e.arg=i;e.active=i==cur;}}
}
void langbox(){
 const int i=v.langBox;long long done=0,total=0;char msg[256]="";
 const int st=langhooks->progress(&done,&total,msg,sizeof msg);
 const std::string name=langhooks->name(i);
 well(("LANGUAGE: "+name).c_str(),38,160,940,358-(v.compact?72:0));
 if(!v.langStarted){
  note(name+": the announcer, the unit voices, the briefings, the names of what you build and the movies, "
       "from the 1995 discs. The movies that release cut or changed stay in English, and so do the soldiers.",56,218,900,4,TEXT);
  text("To download: "+megabytes(langhooks->missing(i)),56,v.compact?330:352,ACCENT);
  Seat &go=add(EM_LANG_SEAT-20,"Download",56,v.compact?370:400,220);go.action=LANGGO;go.arg=i;
  Seat &no=add(EM_LANG_SEAT-21,"Not now",292,v.compact?370:400,220);no.action=LANGCLOSE;return;}
 if(st==1){/* running: the bar */
  const int bx=56,by=v.compact?286:300,bw=904,bh=24;rect(bx,by,bw,bh,0x101310);
  const int fill=total>0?int(double(bw-4)*double(done)/double(total)):0;rect(bx+2,by+2,std::max(0,std::min(bw-4,fill)),bh-4,theme.ra?0xa01c14:0x3a8a2a);
  text("Downloading "+name+": "+megabytes(done)+" of "+megabytes(total),56,by-36,TEXT);
  Seat &c=add(EM_LANG_SEAT-22,"Cancel",56,v.compact?370:400,220);c.action=LANGCANCEL;return;}
 if(st==2){if(langhooks->current()!=i)langhooks->choose(i);
  note(name+" is ready and in use: the voices now, the names and briefings from the next mission.",56,218,900,3,TEXT);
  Seat &ok=add(EM_LANG_SEAT-21,"OK",56,v.compact?370:400,220);ok.action=LANGCLOSE;return;}
 note(msg[0]?msg:"The download stopped.",56,218,900,3,TEXT);
 Seat &again=add(EM_LANG_SEAT-20,"Try again",56,v.compact?370:400,220);again.action=LANGGO;again.arg=i;
 Seat &no=add(EM_LANG_SEAT-21,"Close",292,v.compact?370:400,220);no.action=LANGCLOSE;
}

/* SETTINGS, ONE SCREEN FOR EVERYTHING. The title's Settings opened the visuals alone; game
   speed, scrolling, the mouse and every volume were only in the pause menu, so a player
   could not set them before a mission. The tab row now runs Display, World, Effects and
   Interface (the picture) and then Gameplay and Audio, on the same page: those two write
   the block the pause menu's Game Controls and Sound Controls write and are remembered
   with it, and OK, Back and Escape close the screen from any tab as before. More is the
   picture's own overflow and is reached from its button. */
void visuals(DOPT_State *st){
 static const char *cats[]={"Display","World","Effects","Interface","More","Gameplay","Audio"};
 static const int tabs[]={0,1,2,3,5,6};
 const int shrink=v.compact?72:0;
 // More retains source settings absent from the review fixture, without dropping them.
 for(int i=0;i<6;i++){Seat &q=add(-1,cats[tabs[i]],38+i*158,86,148);q.action=TAB;q.arg=tabs[i];q.active=v.section==tabs[i];}
 const char *modes[]={"Classic","N64","Enhanced"};
 if(v.section==0){
  well("DISPLAY",38,160,460,358-shrink);well("GRAPHICS MODE",518,160,460,180);well("OUTPUT",518,360,460,158-shrink);
  int dispitems[]={DOPT_VE_FULLSCREEN,DOPT_VE_WINDOWED,DOPT_VE_BORDERLESS};const char *disps[]={"Fullscreen","Windowed","Borderless"};int dispvalues[]={2,0,1};
  for(int i=0;i<3;i++){Seat &q=opt(st,dispitems[i],disps[i],54+i*144,208,140);q.action=OPTITEM;q.active=st->vis.dispmode==dispvalues[i];}
  optdrop(st,DOPT_VE_RESOLUTION,56,v.compact?272:294);optcheck(st,DOPT_VE_VSYNC,56,v.compact?330:356);optcheck(st,DOPT_VE_FPS,56,v.compact?382:408);
  if(st->vis.drawable_w>0&&st->vis.drawable_h>0)text("Current: "+std::to_string(st->vis.drawable_w)+" X "+std::to_string(st->vis.drawable_h),56,v.compact?426:476,MUTED);
  for(int i=0;i<3;i++){Seat &q=add(-1,modes[i],534+i*144,208,140);q.action=MODE;q.arg=i;q.active=st->vis.mode==i;q.disabled=i==1&&st->n64_unavailable;}
  text(std::string(modes[st->vis.mode])+" presentation",536,278,MUTED);
  optcheck(st,DOPT_VE_GAMMA,536,406);
 }else if(v.section==1){
  well("SURFACES",38,160,460,358-shrink);well("OBJECTS",518,160,460,358-shrink);
  optdrop(st,DOPT_VE_TEXSET,56,244);optcheck(st,DOPT_VE_WATER,56,v.compact?300:314);optcheck(st,DOPT_VE_BILINEAR,56,v.compact?344:366);
  optdrop(st,DOPT_VE_INFSET,536,244);optcheck(st,DOPT_VE_TREES,536,v.compact?300:314);optcheck(st,DOPT_VE_GRASS,536,v.compact?344:366);optcheck(st,DOPT_VE_TIB3D,536,v.compact?388:418);

 }else if(v.section==2){
  well("LIGHTING",38,160,460,358-shrink);well("POST PROCESSING",518,160,460,358-shrink);
  int left[]={DOPT_VE_SHADOWS,DOPT_VE_OCCLUSION,DOPT_VE_LIGHT},right[]={DOPT_VE_BLOOM,DOPT_VE_GRADE,DOPT_VE_CRT};
  for(int i=0;i<3;i++){optcheck(st,left[i],56,210+i*52);optcheck(st,right[i],536,210+i*52);}
 }else if(v.section==3){
  well("INTERFACE",38,160,460,358-shrink);well("CAMERA",518,160,460,358-shrink);
  optcheck(st,DOPT_VE_NEWHUD,56,210);optcheck(st,DOPT_VE_SMOOTH,56,262);optdrop(st,DOPT_VE_UISCALE,56,362);optdrop(st,DOPT_VE_PERSPECTIVE,536,244);
  text("Classic: top-down view",536,326,MUTED);text("Isometric: angled view",536,358,MUTED);

 }else if(v.section==4){
  well("WORLD DETAIL",38,160,460,358-shrink);well("TERRAIN DETAIL",518,160,460,358-shrink);
  int a[]={DOPT_VE_FOLIAGE,DOPT_VE_ROCKS};
  for(int detailIndex=0;detailIndex<2;detailIndex++)optcheck(st,a[detailIndex],56,210+detailIndex*52);
  optcheck(st,DOPT_VE_PATCHES,536,210);
 }else if(v.section==5&&v.langBox>=0&&langhooks){
  langbox();
 }else if(v.section==5){
  well("GAME",38,160,460,358-shrink);well("INPUT",518,160,460,358-shrink);
  setrange(st,DOPT_ES_SPEED,"Game speed",56,v.compact?238:252,424,false);
  text("Slower",56,v.compact?286:300,MUTED);text("Faster",414,v.compact?286:300,MUTED);
  setrange(st,DOPT_ES_SCROLL,"Scroll rate",56,v.compact?362:390,424,false);
  text("Slower",56,v.compact?410:438,MUTED);text("Faster",414,v.compact?410:438,MUTED);
  for(int i=0;i<DOPT_G_TOGGLES;i++){Seat &q=add(EM_GAMEPLAY_SEAT-i,dopt_gp_label(i),536,210+i*52,424,36,CHECK);q.action=GPCHECK;q.arg=i;q.value=st->gp.on[i];q.active=false;}
  langrow(536,v.compact?400:430);
 }else{
  well("VOLUME",38,160,460,358-shrink);well("VOICES AND WORLD",518,160,460,358-shrink);
  setrange(st,DOPT_ES_MUSIC,"Music",56,v.compact?238:252,424,true);
  setrange(st,DOPT_ES_SOUND,"Sound effects",56,v.compact?326:352,424,true);
  setrange(st,DOPT_ES_VOICES,"Voices",536,v.compact?238:252,424,true);
  text("The announcer's reports",536,v.compact?286:300,MUTED);
  setrange(st,DOPT_ES_AMBIENCE,"Ambience",536,v.compact?362:390,424,true);
 }
 if(v.section<4){Seat &q=add(-1,"More Settings",342,v.h-86,192);q.action=TAB;q.arg=4;}
 optreset();optback(st);opt(st,DOPT_A_OK,"OK",758,v.h-86,220);
 if(v.drop>=0){
  int owner=-1;for(int i=0;i<int(v.seats.size());i++)if(v.seats[i].action==DROP&&v.seats[i].arg==v.drop)owner=i;
  if(owner>=0){Seat q=v.seats[owner];int count=dopt_drop_count(st,v.drop),start=v.drop==DOPT_VE_RESOLUTION?st->restop:0;
   int available=0;for(int i=start;i<count;i++)if(dopt_drop_listed(v.drop,i))available++;
   const int shown=std::min(available,7);
   const int hintHeight=count>7?30:0;
   int y=q.y+44;if(y+shown*42+8+hintHeight>v.h-106)y=std::max(78,q.y-shown*42-12-hintHeight);
   popup(owner,q.x-4,y-4,q.w+8,shown*42+8+hintHeight);
   for(int val=start,row=0;val<count&&row<shown;val++){if(!dopt_drop_listed(v.drop,val))continue;Seat &e=add(v.drop,dopt_drop_text(st,v.drop,val),q.x,y+row*42,q.w);row++;e.action=DROPCHOICE;e.arg=val;e.active=val==(v.popupChoice>=0?v.popupChoice:dopt_enh_drop_value(st,v.drop));e.disabled=!dopt_enh_drop_allowed(st,v.drop,val);}
   if(count>7)v.popupHint="Mouse wheel: more resolutions";
  }
 }
}
}
extern "C" void em_options(DOPT_State *st){
 if(!load())return;int oldpage=st->page;
 if(st->page==DOPT_PAGE_VISUALS){st->page=DOPT_PAGE_ADVANCED;st->advTab=st->vis.mode;st->advTop=0;}
 const char *frame="visuals-frame",*title="OPTIONS";
 switch(st->page){case DOPT_PAGE_OPTIONS:frame="pause-frame";break;case DOPT_PAGE_CONTROLS:title="GAME CONTROLS";break;
 case DOPT_PAGE_GAMEPLAY:title="GAMEPLAY";break;case DOPT_PAGE_ADVANCED:title="SETTINGS";break;
 case DOPT_PAGE_SLOTS:title=st->sl.mode==DOPT_SL_SAVE?"SAVE MISSION":st->sl.mode==DOPT_SL_DELETE?"DELETE MISSION":"LOAD MISSION";break;
 case DOPT_PAGE_CONFIRM:frame="confirm-frame";title=st->cfkind==2?"EXIT GAME":st->cfkind==1?"DELETE MISSION":st->match?"LEAVE MATCH":"ABORT MISSION";break;
 case DOPT_PAGE_RESTATE:frame="settings-frame";title="MISSION BRIEFING";break;
 case DOPT_PAGE_NOTICE:frame="confirm-frame";title=st->ncaption;break;
 case DOPT_PAGE_SOUND:title="AUDIO";break;case DOPT_PAGE_CHEATS:title="CHEATS";break;}
 begin(OPT,st,frame,title);if(oldpage==DOPT_PAGE_VISUALS)v.section=0;
 if(v.page!=st->page){v.held=v.drag=-1;v.drop=v.popupChoice=-1;v.page=st->page;}
  if(st->page==DOPT_PAGE_OPTIONS){
  text("MISSION PAUSED",354,64,ACCENT,true);text("MISSION",36,110,MUTED);text("SETTINGS",362,110,MUTED);
  int a[]={DOPT_LOAD,DOPT_SAVE,DOPT_DELETE,DOPT_RESTATE},b[]={DOPT_GAME,DOPT_VISUALS,DOPT_GAMEPLAY,DOPT_AUDIO};
  for(int i=0;i<4;i++)opt(st,a[i],dopt_item_label(st,a[i]),36,136+i*48,310);
  for(int i=0;i<4;i++)opt(st,b[i],dopt_item_label(st,b[i]),362,136+i*48,310);
  opt(st,DOPT_RESUME,"Resume Mission",36,410,204).active=true;
  opt(st,DOPT_ABORT,dopt_abort_label(st),252,410,204);opt(st,DOPT_EXIT,"Exit Game",468,410,204);
 }else if(st->page==DOPT_PAGE_CONTROLS){
  /* (the volumes and the jukebox are the pause menu's Audio page) */
  well("GAMEPLAY",38,110,940,v.h-228);
  optrange(st,DOPT_C_SPEED,"Game speed",56,v.compact?188:216,904,st->set.speed,6,false);optrange(st,DOPT_C_SCROLL,"Scroll rate",56,v.compact?310:354,904,st->set.scrollrate,6,false);
  text("Slower",56,v.compact?236:264,MUTED);text("Faster",894,v.compact?236:264,MUTED);text("Slower",56,v.compact?358:402,MUTED);text("Faster",894,v.compact?358:402,MUTED);
  optreset();optback(st);opt(st,DOPT_C_OK,"OK",758,v.h-86,220);
 }else if(st->page==DOPT_PAGE_GAMEPLAY){
  well("INPUT",38,110,460,v.h-228);well("FEEDBACK",518,110,460,v.h-228);
  for(int i=0;i<DOPT_G_TOGGLES;i++){Seat &q=opt(st,i,dopt_gp_label(i),i==2?536:56,i==1?266:202,424,36,CHECK);q.value=st->gp.on[i];q.active=false;}
  optback(st,38);opt(st,DOPT_G_OK,"OK",758,v.h-86,220);
 }else if(st->page==DOPT_PAGE_ADVANCED){visuals(st);
 }else if(st->page==DOPT_PAGE_SLOTS){
  well("SAVED MISSIONS",38,110,600,v.h-228);well("MISSION",658,110,320,v.h-228);
  int rows=st->sl.mode==DOPT_SL_SAVE?9:10;
  int start=std::max(0,std::min(st->sl.top,std::max(0,st->sl.nrows-rows)));
  /* An empty list has no selection (-1), which is never a row index. */
  if(st->sl.sel>=0&&st->sl.sel<start)start=st->sl.sel;if(st->sl.sel>=start+rows)start=st->sl.sel-rows+1;
  for(int i=0;i<rows&&start+i<st->sl.nrows;i++){Seat &q=add(DOPT_SL_LIST,dopt_slot_row_text(st,start+i),56,164+i*(v.compact?24:32),564,v.compact?24:32,ROW,OPTROW,start+i);q.active=st->sl.sel==start+i;}
  if(!st->sl.nrows)text("No saved missions",56,178,MUTED);
  note(st->sl.descr,676,186,284,6,TEXT);
  if(st->sl.mode==DOPT_SL_SAVE){text("Mission name",676,374);Seat &q=opt(st,DOPT_SL_EDIT,st->sl.descr,676,398,284,40,FIELD);q.active=st->selected==DOPT_SL_EDIT;}
  footer(st->sl.mode==DOPT_SL_SAVE?"Save":st->sl.mode==DOPT_SL_DELETE?"Delete":"Load",DOPT_SL_OK,DOPT_SL_CANCEL,st);
 }else if(st->page==DOPT_PAGE_CONFIRM){
  text(dopt_confirm_msg(st),338,136,TEXT,true);const char *msg2=dopt_confirm_msg2(st);if(msg2&&*msg2)text(msg2,338,162,TEXT,true);
  if(!st->match&&st->cfkind==0){opt(st,DOPT_CF_CANCEL,"Cancel",38,238,192);opt(st,DOPT_CF_RESTART,"Restart",242,238,192);opt(st,DOPT_CF_ABORT,dopt_confirm_yes(st),446,238,192);}
  else{opt(st,DOPT_CF_CANCEL,dopt_confirm_no(st),98,238,220);opt(st,DOPT_CF_ABORT,dopt_confirm_yes(st),358,238,220);}
 }else if(st->page==DOPT_PAGE_RESTATE){
  well("MISSION OBJECTIVE",38,110,600,v.h-234);
  /* Red Alert puts its side's briefing paper behind this box (ALIPAPER or SOVPAPER, the
     side of the mission being played, as the 1996 Restate_Mission does), at 62% under the
     text. */
  if(theme.ra){auto it=arts.find(theme.soviet?"paper-soviet":"paper-allied");
   if(it!=arts.end()){const Art &a=it->second;for(int yy=44;yy<std::min(a.h,v.h-234)-4;yy++)memcpy(&v.rgba[((110+yy)*v.w+38+4)*4],&a.pixels[(yy*a.w+4)*4],(std::min(a.w,600)-8)*4);}}
  note(st->brief,56,178,564,10,TEXT);
  if(st->rvideo)opt(st,DOPT_R_LEFT,"Video",38,v.h-86,192);opt(st,st->rvideo?DOPT_R_RIGHT:DOPT_R_LEFT,"Back",446,v.h-86,192);
 }else if(st->page==DOPT_PAGE_NOTICE){
  note(st->brief,98,112,480,4,TEXT);opt(st,0,"OK",228,238,220);
 }else if(st->page==DOPT_PAGE_SOUND){
  well("JUKEBOX",38,110,600,v.h-228);well("VOLUME",658,110,320,v.h-228);
  for(int i=0;i<9&&st->trkTop+i<st->ntracks;i++){int ix=st->trkTop+i;Seat &q=add(DOPT_S_LIST,st->tracks[ix].fullname?st->tracks[ix].fullname:st->tracks[ix].base,56,164+i*(v.compact?24:32),564,v.compact?24:32,ROW,OPTROW,ix);q.active=st->trkSel==ix;}
  optrange(st,DOPT_S_MUSIC,"Music",676,v.compact?188:210,284,st->set.music,239,true);optrange(st,DOPT_S_SOUND,"Sound effects",676,v.compact?256:290,284,st->set.sound,239,true);
  optrange(st,DOPT_S_SPEECH,"Voices",676,v.compact?324:370,284,st->set.speech,239,true);optrange(st,DOPT_S_AMBIENCE,"Ambience",676,v.compact?392:450,284,st->set.ambience,239,true);
  opt(st,DOPT_S_PLAY,"Play",38,v.h-86,192).icon=theme.ra?"btn-pl":"";opt(st,DOPT_S_STOP,"Stop",242,v.h-86,192).icon=theme.ra?"btn-st":"";opt(st,DOPT_S_OK,"Back",758,v.h-86,220);
  Seat &a=opt(st,DOPT_S_SHUFFLE,"Shuffle",56,v.compact?396:468,268,36,CHECK);a.value=st->shuffle;a.active=false;Seat &b=opt(st,DOPT_S_REPEAT,"Repeat",352,v.compact?396:468,268,36,CHECK);b.value=st->repeat;b.active=false;
 }else if(st->page==DOPT_PAGE_CHEATS){
  well("SANDBOX",38,110,940,v.h-228);for(int i=0;i<DOPT_CH_TOGGLES;i++){Seat &q=opt(st,i,dopt_cheat_label(i),i<4?56:536,178+(i%4)*60,424,36,CHECK);q.value=st->cheat.on[i];q.active=false;}
  opt(st,DOPT_CH_WIN,"Win",38,v.h-86,192);opt(st,DOPT_CH_LOSE,"Lose",242,v.h-86,192);opt(st,DOPT_CH_RESET,"Reset",550,v.h-86,192);opt(st,DOPT_CH_OK,"OK",758,v.h-86,220);
 }
 // Every regular native button invokes the source controller by item, not old geometry.
 for(Seat &q:v.seats)if(!q.action){q.action=OPTITEM;q.arg=q.item;}
 paint();
}

extern "C" int em_opt_press(DOPT_State *st,int x,int y){
 int n=virtualseat(x);
 if(v.popupFirst>=0&&(n<0||v.seats[n].disabled)){
  v.held=v.drag=-1;st->pressed=-1;
  if(!popup_contains(v.mousex,v.mousey)){v.drop=-1;st->texdrop=-1;}
  return 0;
 }
 if(n<0)return dopt_press(st,x,y);Seat q=v.seats[n];v.held=n;
 if(q.kind==FIELD){st->selected=q.item;st->pressed=-1;return 0;}
 if(q.action==RANGESET){v.drag=n;int val=q.min+(q.max-q.min)*std::max(0,std::min(1000,y))/1000;dopt_enh_control(st,q.item,val);st->selected=q.item;}
 if(q.action==SETRANGE){v.drag=n;dopt_enh_setting(st,q.arg,q.min+(q.max-q.min)*std::max(0,std::min(1000,y))/1000);}
 return 0;
}
extern "C" int em_opt_motion(DOPT_State *st,int x,int y){
 int n=virtualseat(x);if(n>=0){if(v.drag==n){const Seat &q=v.seats[n];const int val=q.min+(q.max-q.min)*std::max(0,std::min(1000,y))/1000;if(q.action==SETRANGE)dopt_enh_setting(st,q.arg,val);else dopt_enh_control(st,q.item,val);}return 0;}return v.popupFirst>=0?0:dopt_motion(st,x,y);
}
extern "C" int em_opt_release(DOPT_State *st,int x,int y){
 int n=virtualseat(x),held=v.held;v.held=v.drag=-1;
 if(n<0)return v.popupFirst>=0?0:dopt_release(st,x,y);if(n!=held)return 0;Seat q=v.seats[n];
 switch(q.action){
 case RANGESET:case SETRANGE:return 0;
 case GPCHECK:dopt_enh_gameplay_toggle(st,q.arg);return 0;
 case LANGDROP:v.drop=v.drop==EM_LANG_DROP?-1:EM_LANG_DROP;return 0;
 case LANGPICK:v.drop=-1;if(langhooks){if(langhooks->installed(q.arg))langhooks->choose(q.arg);else{v.langBox=q.arg;v.langStarted=false;}}return 0;
 case LANGGO:if(langhooks)langhooks->download(q.arg);v.langStarted=true;return 0;
 case LANGCANCEL:if(langhooks)langhooks->cancel();return 0;
 case LANGCLOSE:v.langBox=-1;v.langStarted=false;return 0;
 case OPTITEM:if(st->page==DOPT_PAGE_OPTIONS&&q.item==DOPT_EXIT){dopt_enh_confirm_exit(st);return 0;}
  if(saves_blocked(st,q.item)){st->selected=q.item;dopt_open_page_notice(st,"NOTICE",em_route_notice(EM_ROUTE_SAVES));return 0;}
  return dopt_enh_action(st,q.item);
 case TAB:v.section=q.arg;v.drop=-1;return 0;
 case BACK:st->texdrop=-1;v.drop=-1;return em_opt_key(st,DOPT_KEY_ESC);
 case RESET:if(st->page==DOPT_PAGE_CONTROLS){DOPT_Settings d;dopt_settings_init(&d);st->set.speed=d.speed;st->set.scrollrate=d.scrollrate;if(st->bind.apply)st->bind.apply(st->bind.user,&st->set);}
  /* (the Gameplay and Audio tabs reset their own settings, never the picture's) */
  else if(st->page==DOPT_PAGE_ADVANCED&&v.section>=5){DOPT_State d=*st;dopt_settings_init(&d.set);
   const int game[]={DOPT_ES_SPEED,DOPT_ES_SCROLL},sound[]={DOPT_ES_MUSIC,DOPT_ES_SOUND,DOPT_ES_VOICES,DOPT_ES_AMBIENCE};
   const int *w=v.section==5?game:sound,n=v.section==5?2:4;for(int k=0;k<n;k++)dopt_enh_setting(st,w[k],dopt_enh_setting_value(&d,w[k]));}
  else dopt_enh_action(st,DOPT_A_RESET);return 0;
 case MODE:dopt_enh_mode(st,q.arg);v.drop=-1;return 0;
 case DROP:v.drop=v.drop==q.arg?-1:q.arg;v.popupChoice=-1;st->texdrop=v.drop;st->restop=0;return 0;
 case DROPCHOICE:dopt_enh_choose(st,q.item,q.arg);v.drop=-1;return 0;
 case OPTROW:dopt_enh_row(st,q.arg);return 0;
 default:return 0;
 }
}

namespace {
Seat &skbutton(const SK_State *st,int item,const std::string &label,int x,int y,int w,int h=40,int kind=BUTTON){Seat &q=sk(st,item,label,x,y,w,h,kind);q.action=SKITEM;q.arg=item;return q;}
/* Red Alert's sides, its rule that ore grows back, its ten tech levels and its 1996 remap
   colours (PALETTE.CPS rows 0 to 7, from the pack). */
const char *side_name(int house){return theme.ra?(house?"Soviet":"Allies"):(house?"Nod":"GDI");}
/* Red Alert's countries by the engine's number (HousesType), and the order its list shows
   them in: the Allies, then the Soviets. */
const char *const ra_country[8]={"Spain","Greece","USSR","England","Ukraine","Germany","France","Turkey"};
const int ra_country_order[8]={3,5,6,0,1,7,2,4};
/* What a seat's side reads: its country where the seats are countries. */
std::string seat_side(const SK_State *st,int seat){int c=sk_row_country(st,seat);return c>=0&&c<8?ra_country[c]:side_name(sk_row_house(st,seat));}
unsigned player_colour(int i);
/* RED ALERT'S OWN RULES (sk_enh_ra_rule), which the 1995 controller has no item for: a
   seat of their own, read only on a joiner's screen. The difficulty is a three step slider
   as on the 1996 skirmish dialog, its value named rather than numbered. */
Seat &rarule(const SK_State *st,int rule,const char *label,int x,int y,int w,int h,int kind){Seat &q=add(-50-rule,label,x,y,w,h,kind);q.action=kind==RANGE?RARANGE:RACHECK;q.arg=rule;q.value=sk_ra_rule(st,rule);q.disabled=sk_ra_rule_disabled(st)!=0;q.active=false;return q;}
void radifficulty(const SK_State *st,int x,int y){const int d=std::max(0,std::min(3,sk_ra_rule(st,SK_RA_DIFFICULTY)));text("New AI default",x,y-28);text(sk_level_name(d),x+424-textwidth(sk_level_name(d)),y-28,ACCENT);Seat &q=rarule(st,SK_RA_DIFFICULTY,"New AI default",x,y,424,36,RANGE);q.min=0;q.max=3;q.step=1;}
void skrange(const SK_State *st,int item,const char *label,int x,int y,int value,int lo,int hi,int step){text(label,x,y-28);std::string val=std::to_string(value);text(val,x+424-textwidth(val),y-28,ACCENT);Seat &q=skbutton(st,item,label,x,y,424,36,RANGE);q.action=SKRANGE;q.value=value;q.min=lo;q.max=hi;q.step=step;q.active=false;}
const unsigned int colours[]={0xdfb54b,0xdb4540,0x56baca,0xdd9138,0x58a354,0x6381c6,0xb778c5,0xbcb9ad};
unsigned player_colour(int i){i&=7;if(theme.ra){auto it=arts.find("remap-colours");if(it!=arts.end()&&it->second.w>i){const unsigned char *p=&it->second.pixels[i*4];return (p[0]<<16)|(p[1]<<8)|p[2];}}return colours[i];}
void skseat(const SK_State *st){
 int seat=st->popup;begin(LOBBY,st,"settings-frame","PLAYER SETUP");well(("PLAYER "+std::to_string(seat+1)).c_str(),38,102,600,356);
 auto dropdown=[&](int id,const char *label,const std::string &value,int x,int y,int w){text(label,x,y-24);Seat &q=add(id,value,x,y,w);q.action=DROP;q.arg=id;q.active=v.drop==id;};
 int mode=sk_row_mode(st,seat),side=sk_row_house(st,seat),team=sk_row_team(st,seat),start=sk_row_start(st,seat),colour=sk_row_colour(st,seat);
 dropdown(-10,"Seat type",mode==SK_SEAT_BOT?"Computer":mode==SK_SEAT_OPEN?"Empty":mode==SK_SEAT_BLOCK?"Blocked":"Player",56,178,268);
 dropdown(-14,"Difficulty",mode==SK_SEAT_BOT?sk_level_name(sk_row_level(st,seat)):"-",352,178,268);v.seats.back().disabled=mode!=SK_SEAT_BOT||st->net==2;
 dropdown(-11,sk_row_country(st,seat)>=0?"Country":theme.ra?"Side":"Faction",seat_side(st,seat),56,270,268);text("Player colour",352,246);
 for(int i=0;i<8;i++){Seat &q=skbutton(st,SK_I_POP_C1+i,"",352+i*32,270,28,28,COLOUR);q.active=colour==i;q.value=player_colour(i);}
 dropdown(-12,"Team",std::to_string(team+1),56,364,268);dropdown(-13,"Start position",start<0?"Random":std::to_string(start+1),352,364,268);
 Seat &done=add(-1,"Done",446,498,192);done.action=SEATDONE;Seat &back=add(-1,"Back",38,498,192);back.action=SEATDONE;
 int drop=v.drop;if(drop<=-10&&drop>=-14){
  std::vector<std::pair<std::string,int>> options;
  if(drop==-10){if(mode==SK_SEAT_HUMAN)options.push_back({"Player",-1});else{options.push_back({"Computer",SK_I_POP_BOT});options.push_back({"Empty",SK_I_POP_OPEN});options.push_back({"Blocked",SK_I_POP_BLOCK});}}
  if(drop==-11&&sk_row_country(st,seat)>=0)for(int k:ra_country_order)options.push_back({ra_country[k],-100-k});
  else if(drop==-11){options.push_back({side_name(0),SK_I_POP_GDI});options.push_back({side_name(1),SK_I_POP_NOD});}
  if(drop==-12)for(int i=0;i<8;i++)options.push_back({std::to_string(i+1),SK_I_POP_T1+i});
  if(drop==-14)for(int i=0;i<4;i++)options.push_back({sk_level_name(i),SK_I_POP_EASY+i});
  if(drop==-13){options.push_back({"Random",-2});int count=st->sel>=0?std::min(8,int(st->maps[st->sel].starts)):0;for(int i=0;i<count;i++)options.push_back({std::to_string(i+1),SK_I_POP_S1+i});}
  int x=drop==-13||drop==-14?352:56,w=268,ownerY=drop==-10||drop==-14?178:drop==-11?270:364,h=int(options.size())*42+8,y=ownerY+44;
  if(y+h>486)y=std::max(78,ownerY-h-4);
  int owner=-1;for(int index=0;index<int(v.seats.size());index++)if(v.seats[index].action==DROP&&v.seats[index].arg==drop)owner=index;
  popup(owner,x-4,y-4,w+8,h);
  for(int i=0;i<int(options.size());i++){Seat &q=add(options[i].second,options[i].first,x,y+i*42,w);q.action=options[i].second==-2?SKRANDOM:SKITEM;q.arg=q.item;q.disabled=q.item==-1||(q.item>=0&&sk_item_disabled(st,q.item));
   if(q.item<=-100){q.action=SKCOUNTRY;q.arg=-100-q.item;q.active=sk_row_country(st,seat)==q.arg;}}
 }
 paint();
 if(st->status&&v.drop==-1)note(st->status,56,432,564,1,ACCENT);
}
}
extern "C" void em_lobby(const SK_State *st,const DB_Pack *p){
 if(!load())return;
 if(st->popup>=0){skseat(st);return;}
 bool net=st->net!=0,rules=v.kind==LOBBY&&v.owner==st&&v.sksection==1;
 bool picker=st->picker!=0;
 begin(LOBBY,st,!picker&&!rules&&net?"lobby-frame":"visuals-frame",picker?"CHANGE MAP":net?"MULTIPLAYER LOBBY":"SKIRMISH");
 if(picker){
  skbutton(st,SK_I_PICK_TAB0,"Official",38,86,230).active=st->tab==0;skbutton(st,SK_I_PICK_TAB1,"User Maps",276,86,230).active=st->tab==1;
  well("MAPS",38,150,600,368);well("DETAILS",658,150,320,368);
  std::vector<int> maps;for(int i=0;i<st->count;i++)if(st->maps[i].user==st->tab)maps.push_back(i);
  v.opstop=std::max(0,std::min(v.opstop,std::max(0,int(maps.size())-8)));
  for(int i=0;i<8&&v.opstop+i<int(maps.size());i++){int ix=maps[v.opstop+i];Seat &q=add(SK_I_PICK_LIST,st->maps[ix].name?st->maps[ix].name:st->maps[ix].scen,56,198+i*34,564,32,ROW,MAPROW,ix);q.active=st->pick_sel==ix;}
  text(std::to_string(maps.size())+" maps",56,490,MUTED);
  if(maps.size()>8){Seat &a=add(-1,"Previous",346,474,132,32);a.action=OPSTOP;a.arg=-8;a.disabled=v.opstop==0;Seat &b=add(-1,"Next",488,474,132,32);b.action=OPSTOP;b.arg=8;b.disabled=v.opstop+8>=int(maps.size());}
  int ix=st->pick_sel;if(ix>=0&&ix<st->count){preview(st,p,ix,698,204,240);const SK_Map &m=st->maps[ix];text(std::to_string(m.starts)+" players / "+std::to_string(m.w)+" X "+std::to_string(m.h),676,460,MUTED);text(m.theater?m.theater:"",676,486,MUTED);}
  skbutton(st,SK_I_PICK_CANCEL,"Back",38,550,192);skbutton(st,SK_I_PICK_OK,"Use Map",758,550,220);paint();return;
 }
 for(int i=0;i<2;i++){Seat &q=add(-1,i?"Match Rules":"Players & Map",38+i*238,86,230);q.action=TAB;q.arg=i;q.active=v.sksection==i;}
 if(net)skbutton(st,SK_I_CHANGEMAP,"Change Map",514,86,230);
 if(rules){
  well("STARTING CONDITIONS",38,150,460,368);well("MATCH RULES",518,150,460,368);
  if(st->countries){
   /* RED ALERT'S RULES, as its 1996 dialog and the approved design have them: the three
      sliders and the computers' difficulty, then Bases, Ore Regenerates, Crates, Shroud
      Regrows, Capture The Flag and AI Takeover. Superweapons and Short Game keep the
      room's defaults (on and off). */
   skrange(st,SK_I_BUILD,"Tech Level",56,232,st->build,1,10,1);skrange(st,SK_I_CREDITS,"Credits",56,314,st->credits,0,10000,500);skrange(st,SK_I_UNITS,"Unit Count",56,396,st->units,0,10,1);radifficulty(st,56,478);
   const int ids[]={SK_I_BASES,SK_I_TIBERIUM,SK_I_CRATES};const char *names[]={"Bases","Ore Regenerates","Crates"};const int vals[]={1,1,st->crates};
   for(int i=0;i<3;i++){Seat &q=skbutton(st,ids[i],names[i],536,202+i*48,424,36,CHECK);q.value=vals[i];q.active=false;}
   rarule(st,SK_RA_SHROUD,"Shroud Regrows",536,346,424,36,CHECK);rarule(st,SK_RA_CTF,"Capture The Flag",536,394,424,36,CHECK);
   Seat &ai=skbutton(st,SK_I_AITAKE,"AI Takeover",536,442,424,36,CHECK);ai.value=st->aitake;ai.active=false;
  }else{
  skrange(st,SK_I_BUILD,"Tech Level",56,242,st->build,1,theme.ra?10:7,1);skrange(st,SK_I_CREDITS,"Credits",56,344,st->credits,0,10000,500);skrange(st,SK_I_UNITS,"Unit Count",56,446,st->units,0,10,1);
  int ids[]={SK_I_BASES,SK_I_TIBERIUM,SK_I_SUPER,SK_I_CRATES,SK_I_AITAKE,SK_I_SHORTGAME};const char *names[]={"Bases",theme.ra?"Ore Regenerates":"Tiberium Regrows","Superweapons","Crates","AI Takeover","Short Game"};int vals[]={1,1,st->super,st->crates,st->aitake,st->shortgame};
  for(int i=0;i<6;i++){Seat &q=skbutton(st,ids[i],names[i],536,202+i*48,424,36,CHECK);q.value=vals[i];q.active=false;}
  }
  Seat &a=add(-1,"Back",38,550,192);a.action=TAB;a.arg=0;Seat &b=add(-1,"Done",758,550,220);b.action=TAB;b.arg=0;
 }else{
  well("PLAYERS",38,150,net?508:600,368);well("MAP",net?566:658,150,net?188:320,368);
  text("Player",56,194,MUTED);text(net?"Side / Team":"Side / Team / Start",net?246:238,194,MUTED);text("Difficulty",net?408:500,194,MUTED);
  int seats=sk_players(st);std::vector<std::string> summaries;std::vector<int> rowcol;
  for(int i=0;i<std::min(8,seats);i++){
   char name[40];sk_seat_name(st,i,name,sizeof name);int mode=sk_row_mode(st,i),start=sk_row_start(st,i);
   Seat &q=skbutton(st,SK_I_ROW0+i,fitted(name,net?132:186),56,222+i*32,net?348:440,30,ROW);q.label="  "+q.label;q.active=false;
   Seat &level=skbutton(st,SK_I_LEVEL0+i,mode==SK_SEAT_BOT?sk_level_name(sk_row_level(st,i)):"-",net?408:500,222+i*32,120,30,ROW);level.active=false;
   /* Red Alert's network column keeps side and team; its start lives in Player Setup, since
      "Random" would be cut short in the 176 pixel column. */
   summaries.push_back(mode==SK_SEAT_OPEN||mode==SK_SEAT_BLOCK?"":seat_side(st,i)+" / "+std::to_string(sk_row_team(st,i)+1)+(net?std::string():" / "+(start<0?std::string("Random"):std::to_string(start+1))));
   rowcol.push_back(sk_row_colour(st,i));
  }
  int ix=st->sel;if(ix>=0&&ix<st->count){const SK_Map &m=st->maps[ix];
   preview(st,p,ix,net?600:698,net?204:202,net?120:240);
   if(net){note(m.name?m.name:m.scen,584,348,152,2);text(std::to_string(m.starts)+" players",584,412,MUTED);text("Starts 1 - "+std::to_string(m.starts),584,444,MUTED);text(std::to_string(m.w)+" X "+std::to_string(m.h),584,476,MUTED);}
   else {text(fitted(m.name?m.name:m.scen,284),676,460,MUTED);text(std::to_string(m.starts)+" players / "+std::to_string(m.w)+" X "+std::to_string(m.h),676,486,MUTED);}
  }
  if(net){
   well("CHAT",774,150,404,368);std::string messages;
   for(int i=0;i<st->chat_count&&i<SK_CHAT_LOG;i++){
    /* The network controller already files each line with its speaker prefix. */
    if(!messages.empty())messages+='\n';messages+=st->chat_line[i];
   }
   note(messages,792,194,368,11,TEXT,24,true);
   Seat &q=skbutton(st,SK_I_CHAT,st->chat_in,792,466,276,40,FIELD);q.active=st->chat_focus!=0;
   Seat &send=add(-1,"Send",1080,466,80);send.action=SEND;send.disabled=sk_item_disabled(st,SK_I_CHAT);
  }else skbutton(st,SK_I_CHANGEMAP,"Change Map",550,550,192);
  skbutton(st,SK_I_CANCEL,net?"Leave":"Back",38,550,192);
  skbutton(st,SK_I_PLAY,net?sk_item_label(st,SK_I_PLAY):"Play",net?958:758,550,220);
  paint();
  for(int i=0;i<int(summaries.size());i++){rect(64,231+i*32,12,12,player_colour(rowcol[i]));text(fitted(summaries[i],net?154:264),net?246:230,229+i*32,MUTED);}
  if(st->status)note(st->status,net?774:242,528,net?404:496,1,ACCENT);
  if(net&&st->net_join[0])text(fitted(st->net_join,414),v.w-38-textwidth(fitted(st->net_join,414)),62,ACCENT);
  return;
 }
 paint();if(st->status)note(st->status,242,528,496,1,ACCENT);
}
extern "C" int em_sk_press(SK_State *st,int x,int y){
 int n=virtualseat(x);
 if(v.popupFirst>=0&&(n<0||v.seats[n].disabled)){v.held=v.drag=-1;if(!popup_contains(v.mousex,v.mousey))v.drop=-1;return 0;}
 if(n<0)return sk_press(st,x,y);const Seat &q=v.seats[n];v.held=n;
 st->chat_focus=q.kind==FIELD&&q.item==SK_I_CHAT;
 if(q.action==SKRANGE){v.drag=n;int val=q.min+(q.max-q.min)*std::max(0,std::min(1000,y))/1000;val=q.min+((val-q.min+q.step/2)/q.step)*q.step;sk_enh_control(st,q.item,val);}
 if(q.action==RARANGE){v.drag=n;int val=q.min+((q.max-q.min)*std::max(0,std::min(1000,y))+500)/1000;sk_enh_ra_rule(st,q.arg,val);}
 return 0;
}
extern "C" int em_sk_motion(SK_State *st,int x,int y){
 int n=virtualseat(x);if(n>=0){if(v.drag==n){const Seat &q=v.seats[n];
  if(q.action==RARANGE){sk_enh_ra_rule(st,q.arg,q.min+((q.max-q.min)*std::max(0,std::min(1000,y))+500)/1000);return 0;}
  int val=q.min+(q.max-q.min)*std::max(0,std::min(1000,y))/1000;val=q.min+((val-q.min+q.step/2)/q.step)*q.step;sk_enh_control(st,q.item,val);}return 0;}return v.popupFirst>=0?0:sk_motion(st,x,y);
}
extern "C" int em_sk_release(SK_State *st,int x,int y){
 int n=virtualseat(x),held=v.held;v.held=v.drag=-1;if(n<0)return v.popupFirst>=0?0:sk_release(st,x,y);if(n!=held)return 0;const Seat &q=v.seats[n];
 switch(q.action){case SKRANGE:return 0;case TAB:v.sksection=q.arg;v.drop=-1;return 0;
 case SKITEM:{int act=sk_enh_action(st,q.item);if(SK_IS_PICK(q.item)||SK_IS_POP(q.item))v.drop=-1;return act;}
 case MAPROW:sk_enh_pick_row(st,q.arg);return 0;
 case OPSTOP:v.opstop=std::max(0,v.opstop+q.arg);return 0;
 case SEATDONE:st->popup=-1;v.drop=-1;return 0;
 case DROP:v.drop=v.drop==q.arg?-1:q.arg;return 0;
 case SKRANDOM:sk_enh_random_start(st);v.drop=-1;return 0;
 case SKCOUNTRY:sk_enh_set_country(st,st->popup,q.arg);v.drop=-1;return 0;
 case RACHECK:sk_enh_ra_rule(st,q.arg,!sk_ra_rule(st,q.arg));return 0;
 case RARANGE:return 0;
 case SEND:st->chat_focus=1;return sk_key(st,SK_KEY_ENTER);
 default:return 0;}
}

namespace {
int ops_category(const DO_State *st,int row){
 if(!st->grouped)return 0;int cat=-1;
 for(int i=0;i<=row&&i<st->count;i++)if(st->list[i].header){std::string h=st->list[i].name?st->list[i].name:"";
  if(theme.ra)cat=h.find("COUNTERSTRIKE")!=std::string::npos?0:h.find("AFTERMATH")!=std::string::npos?1:-1;
  else cat=h.find("COVERT")!=std::string::npos?0:h.find("SPECIAL")!=std::string::npos?1:-1;}
 return cat;
}
std::vector<int> ops_rows(const DO_State *st,int cat){std::vector<int> rows;
 for(int i=0;i<st->count;i++)if(do_row_playable(st,i)&&(!st->grouped||ops_category(st,i)==cat))rows.push_back(i);
 if(st->grouped)std::stable_sort(rows.begin(),rows.end(),[st](int a,int b){return st->list[a].nod<st->list[b].nod;});return rows;
}
}
extern "C" void em_ops_move(DO_State *st,int delta){
 if(!st->grouped){if(delta<=-10000)do_move_home(st);else if(delta>=10000)do_move_end(st);else do_move(st,delta);return;}
 const auto rows=ops_rows(st,std::max(0,v.opsfilter));if(rows.empty())return;
 auto it=std::find(rows.begin(),rows.end(),st->selected);int ix=it==rows.end()?0:int(it-rows.begin());
 ix=std::max(0,std::min(int(rows.size())-1,ix+delta));st->selected=rows[ix];
}
extern "C" void em_ops_filter(DO_State *st,int category){
 if(!st->grouped)return;v.opsfilter=std::max(0,std::min(1,category));v.opstop=0;
 auto rows=ops_rows(st,v.opsfilter);st->selected=rows.empty()?-1:rows.front();
}
extern "C" void em_ops(DO_State *st){
 if(!load())return;begin(OPS,st,"visuals-frame",st->grouped?"SPECIAL OPS":"USER MAPS");
 if(v.opsfilter<0)v.opsfilter=std::max(0,ops_category(st,st->selected));
 if(st->grouped){const char *tabs[]={theme.ra?"Counterstrike":"Covert Ops",theme.ra?"Aftermath":"Special Ops"};for(int i=0;i<2;i++){Seat &q=add(-1,tabs[i],38+i*238,86,230);q.action=OPSFILTER;q.arg=i;q.active=v.opsfilter==i;}}
 auto rows=ops_rows(st,v.opsfilter);
 auto it=std::find(rows.begin(),rows.end(),st->selected);
 if(it==rows.end()&&!rows.empty()){st->selected=rows.front();it=rows.begin();}
 int index=it==rows.end()?-1:int(it-rows.begin());
 int y=st->grouped?150:110,h=st->grouped?368:408;well("MISSIONS",38,y,600,h);well("DETAILS",658,y,320,h);
 if(st->selected!=v.lastselected){if(index>=0&&index<v.opstop)v.opstop=index;if(index>=v.opstop+8)v.opstop=index-7;v.lastselected=st->selected;}
 v.opstop=std::max(0,std::min(v.opstop,std::max(0,int(rows.size())-8)));
 for(int i=0;i<8&&v.opstop+i<int(rows.size());i++){int ix=rows[v.opstop+i];const DO_Mission &m=st->list[ix];Seat &q=add(ix,m.name?m.name:m.scen,56,y+48+i*34,564,32,ROW,OPTROW,ix);q.active=ix==st->selected;if(st->grouped)q.badge=m.nod?1:0;}
 text(std::to_string(rows.size())+" missions",56,490,MUTED);
 if(rows.size()>8){Seat &a=add(-1,"Previous",346,474,132,32);a.action=OPSTOP;a.arg=-8;a.disabled=v.opstop==0;Seat &b=add(-1,"Next",488,474,132,32);b.action=OPSTOP;b.arg=8;b.disabled=v.opstop+8>=int(rows.size());}
 if(index>=0&&theme.ra){const DO_Mission &m=st->list[st->selected];Seat &p=add(-1,"",742,212,136,120,PLATE);p.badge=m.nod?1:0;p.disabled=true;text(m.nod?"Soviet":"Allied",676,370,MUTED);text(m.scen?m.scen:"",676,404,MUTED);note(m.name?m.name:"",676,438,284,2);}
 else if(index>=0){const DO_Mission &m=st->list[st->selected];image(m.nod?"faction-Nod":"faction-GDI",742,212);text(m.nod?"Nod":"GDI",676,370,MUTED);text(m.scen?m.scen:"",676,404,MUTED);note(m.name?m.name:"",676,438,284,2);}
 /* Red Alert ships with no expansion missions and no user maps: the empty screen says why,
    and where user maps go. */
 if(index<0&&theme.ra){
  note(st->grouped?"No Counterstrike or Aftermath missions are installed. The 1996 freeware discs carry the base game only; the main menu greys this button until expansion missions are found.":"No user maps installed. Red Alert scenario files (.INI or .MPR) placed in the user maps folder appear here.",56,y+64,564,6);
  note("Nothing to play yet",676,y+64,284,2);}
 Seat &a=add(DO_HIT_CANCEL,"Back",38,550,192);legacy(a,DO_CANCEL_X,DO_BTN_Y,DO_CANCEL_W,DO_BTN_H);
 Seat &b=add(DO_HIT_PLAY,"Play",758,550,220);legacy(b,DO_PLAY_X,DO_BTN_Y,DO_PLAY_W,DO_BTN_H);b.disabled=index<0;paint();
}
extern "C" int em_ops_hit(const DO_State *st,int x,int y){int n=virtualseat(x);if(n<0)return do_hit_test(st,x,y);const Seat &q=v.seats[n];return q.action==OPTROW?q.arg:-1000-n;}
extern "C" void em_ops_action(DO_State *st,int code){
 int n=-1000-code;if(n<0||n>=int(v.seats.size()))return;Seat q=v.seats[n];
 if(q.action==OPSFILTER){em_ops_filter(st,q.arg);}
 if(q.action==OPSTOP){em_ops_move(st,q.arg);v.opstop=std::max(0,v.opstop+q.arg);}
}

extern "C" void em_browser(const MP_State *st){
 if(!load())return;
 if(st->prompt){begin(BROWSER,st,"settings-frame","PLAYER NAME");well("YOUR NAME",38,110,600,350);text("Player name",56,202);Seat &q=mp(st,MP_I_HANDLE,st->handle,56,238,564,40,FIELD);q.active=true;mp(st,MP_I_HANDLE_OK,"OK",446,498,192);paint();return;}
 if(st->page==MP_PAGE_WAIT){begin(BROWSER,st,"settings-frame","MULTIPLAYER LOBBY");well("ROOM",38,110,600,350);note(st->name,56,178,564,2,TEXT);note(st->myaddr,56,246,564,2,ACCENT);note(st->status,56,314,564,3);mp(st,MP_I_LEAVE,"Leave",38,498,192);mp(st,st->wait_ishost?MP_I_START:MP_I_READY,st->wait_ishost?"Start Game":"Ready",446,498,192);paint();return;}
 begin(BROWSER,st,"visuals-frame","MULTIPLAYER");bool host=st->tab==MP_TAB_HOST;
 if(!host){if(st->rowsel!=v.lastselected){if(st->rowsel>=0&&st->rowsel<v.opstop)v.opstop=st->rowsel;if(st->rowsel>=v.opstop+3)v.opstop=st->rowsel-2;v.lastselected=st->rowsel;}v.opstop=std::max(0,std::min(v.opstop,std::max(0,st->rowcount-3)));}
 mp(st,MP_I_TAB_HOST,"Host Game",38,86,230).active=host;mp(st,MP_I_TAB_JOIN,"Join Game",276,86,230).active=!host;
 if(host){
  well("GAME",38,150,460,368);well("ACCESS",518,150,460,368);
  text("Game name",56,208);Seat &name=mp(st,MP_I_NAME,st->name,56,232,424,40,FIELD);name.active=st->focus==MP_I_NAME;
  text("Player name",56,306);Seat &handle=add(MP_I_HANDLE,st->handle,56,330,424,40,FIELD,MPHANDLE);handle.active=st->focus==MP_I_HANDLE;
  Seat &setup=add(-1,"Set Up Match",56,422,424);setup.action=MPSETUP;text(fitted(st->mapname,424),56,478,MUTED);
  Seat &a=mp(st,MP_I_PRIVATE,"Private Game",536,204,424,36,CHECK);a.value=st->private_game;a.active=false;
  text("Passcode",536,270);Seat &pass=mp(st,MP_I_PASS,std::string(strlen(st->pass),'*'),536,294,424,40,FIELD);pass.active=st->focus==MP_I_PASS;
  Seat &b=mp(st,MP_I_RELAY,"Internet Game",536,356,424,36,CHECK);b.value=st->relay_game;b.active=false;
  Seat &c=mp(st,MP_I_PUBLIC,"List Publicly",536,414,424,36,CHECK);c.value=st->list_game;c.active=false;
  note(st->relay_game?"Join using a room code":"Public listing shares your address",536,474,424,2);
  mp(st,MP_I_CANCEL,"Back",38,550,192);mp(st,MP_I_HOST,"Host",758,550,220);
 }else{
  mp(st,MP_I_NET_LAN,"LAN",514,86,140).active=st->net==MP_NET_LAN;mp(st,MP_I_NET_NET,"Internet",662,86,140).active=st->net==MP_NET_INTERNET;
  well("AVAILABLE GAMES",38,150,600,368);well("JOIN DETAILS",658,150,320,368);
  text("Game",56,200,MUTED);text("Map",302,200,MUTED);text("Players",534,200,MUTED);
  for(int i=0;i<3&&v.opstop+i<st->rowcount;i++){int ix=v.opstop+i;const MP_Row &r=st->rows[ix];Seat &q=add(1000+ix,fitted(r.name,220),56,240+i*48,564,44,ROW,MAPROW,ix);q.active=st->rowsel==ix;}
  if(!st->rowcount)text("No games found",68,254,MUTED);
  Seat &a=mp(st,MP_I_HIDE_LOCKED,"Hide Locked",56,408,424,36,CHECK);a.value=st->hide_locked;a.active=false;Seat &b=mp(st,MP_I_HIDE_GREY,"Hide Greyed",56,454,424,36,CHECK);b.value=st->hide_grey;b.active=false;
  text("Player name",676,214);Seat &handle=add(MP_I_HANDLE,st->handle,676,238,284,40,FIELD,MPHANDLE);handle.active=st->focus==MP_I_HANDLE;
  text(st->net==MP_NET_INTERNET?"Room code / Address":"Host address",676,312);Seat &addr=mp(st,MP_I_ADDR,st->addr,676,336,284,40,FIELD);addr.active=st->focus==MP_I_ADDR;
  text("Join passcode",676,410);Seat &pass=mp(st,MP_I_PASS,std::string(strlen(st->pass),'*'),676,434,284,40,FIELD);pass.active=st->focus==MP_I_PASS;
  mp(st,MP_I_REFRESH,"Refresh",38,550,192);mp(st,MP_I_CANCEL,"Back",550,550,192);mp(st,MP_I_JOIN,"Join",758,550,220);
 }
 if(st->status[0])note(st->status,242,528,496,1,ACCENT);paint();
 if(!host)for(int i=0;i<3&&v.opstop+i<st->rowcount;i++){const MP_Row &r=st->rows[v.opstop+i];text(fitted(r.map,208),302,254+i*48,MUTED);text(std::to_string(r.players_now)+" / "+std::to_string(r.players_max),534,254+i*48,MUTED);}
}
extern "C" int em_mp_press(MP_State *st,int x,int y){
 int n=virtualseat(x);if(n<0)return mp_press(st,x,y);v.held=n;const Seat &q=v.seats[n];
 if(q.action==MPSETUP)return -3;
 if(q.action==MPHANDLE){st->focus=MP_I_HANDLE;return 0;}
 if(q.action==MAPROW){int ix=q.arg;if(ix<0||ix>=st->rowcount)return 0;mp_enh_select(st,ix);return 0;}return 0;
}
extern "C" void em_mp_motion(MP_State *st,int x,int y){if(virtualseat(x)<0)mp_motion(st,x,y);}

extern "C" void em_choice(int which,int selected,int pressed){
 if(!load())return;begin(CHOICE,nullptr,which==0?"confirm-frame":"settings-frame",which==0?"EXIT GAME":which==1?"INTRO & SNEAK PEEK":"CHOOSE YOUR SIDE");
 if(which==0){text(theme.ra?"Exit Red Alert 3D?":"Exit OpenCNC3D?",338,136,TEXT,true);add(0,"Cancel",98,238,220).active=selected==0;add(1,"Exit Game",358,238,220).active=selected==1;}
 else if(which==1){well("MOVIES",38,110,600,350);image("intro-frame",178,174);add(1,"Play Intro",56,382,564).active=selected==1;add(0,"Back",38,498,192).active=selected==0;}
 else {well("CAMPAIGN",38,110,600,350);image("faction-GDI",120,196);image("faction-Nod",420,196);add(1,"GDI",56,356,268).active=selected==1;add(2,"Nod",352,356,268).active=selected==2;add(0,"Back",38,498,192).active=selected==0;}
 v.held=-1;for(int i=0;i<int(v.seats.size());i++)if(v.seats[i].item==pressed)v.held=i;paint();
}
/* Red Alert's side choice, which the 1996 game asks as a plain message box after a
   difficulty slider: both emblems on their plates, the two sides, the difficulty, Back and
   Start, on one screen. side 0 Allies / 1 Soviet, difficulty 0..2. */
extern "C" void em_side_choice(int side,int difficulty,int pressed){
 if(!load())return;begin(SIDE,nullptr,"settings-frame","CHOOSE YOUR SIDE");
 well("CAMPAIGN",38,110,600,350);
 for(int f=0;f<2;f++){Seat &p=add(-1,"",f?434:106,166,136,120,PLATE);p.badge=f;p.disabled=true;}
 add(EM_SIDE_ALLIES,"Allies",56,312,268).active=side==0;add(EM_SIDE_SOVIET,"Soviet",352,312,268).active=side==1;
 text("Difficulty",56,374);
 const char *names[]={"Easy","Normal","Hard"};
 for(int k=0;k<3;k++)add(EM_SIDE_EASY+k,names[k],56+k*192,398,180).active=difficulty==k;
 add(EM_SIDE_BACK,"Back",38,498,192);add(EM_SIDE_START,"Start",446,498,192).active=true;
 v.held=-1;for(int i=0;i<int(v.seats.size());i++)if(v.seats[i].item==pressed&&!v.seats[i].disabled)v.held=i;paint();
}
extern "C" void em_ra_debrief(const EM_Debrief *d,int pressed){
 if(!load()||!d)return;begin(DEBRIEF,d,"visuals-frame","TOP SCORES");
 /* the result and the match, above the table */
 const char *result=d->win?"Victory":d->decided?"Defeat":"Match Ended";
 text(result,56,96,ACCENT);
 char facts[160];snprintf(facts,sizeof facts,"%s / %s / %d:%02d",d->mapname[0]?d->mapname:"",d->net?"Network":"Skirmish",d->seconds/60,d->seconds%60);
 const std::string f=fitted(facts,600);text(f,960-textwidth(f),96,MUTED);
 /* the 1996 screen's three panels: commander, battles won, kills one column a game */
 well("COMMANDER",38,130,300,388);well("BATTLES WON",358,130,300,388);well("KILLS",678,130,300,388);
 const int games=std::max(1,std::min(4,d->games));
 text("Game",696,182,MUTED);for(int k=0;k<games;k++){const std::string g=std::to_string(k+1);text(g,800+k*53-textwidth(g),182,k==games-1?ACCENT:MUTED);}
 const int rows=std::max(0,std::min(8,d->nrows));
 for(int r=0;r<rows;r++){const EM_DebriefRow &row=d->row[r];const int y=210+r*36;
  if(row.is_local){const unsigned on=theme.slot[S_ROWON];rect(46,y-6,284,30,on);rect(366,y-6,284,30,on);rect(686,y-6,284,30,on);}
  const unsigned own=player_colour(row.colour&7),ink=row.defeated?MUTED:TEXT;
  rect(56,y+2,12,12,own);text(fitted(row.name,140),76,y,own);
  const std::string country=ra_country[row.country&7];text(country,320-textwidth(country),y,MUTED);
  const std::string won=std::to_string(row.wins);text(won,508-textwidth(won)/2,y,ink);
  for(int k=0;k<games;k++){const std::string n=row.kills[k]<0?"-":std::to_string(row.kills[k]);text(n,800+k*53-textwidth(n),y,k==games-1?ink:MUTED);}
 }
 /* this machine's own match, and why it ended when it broke */
 char own[160];snprintf(own,sizeof own,"Destroyed: %d units, %d structures   Lost: %d",d->killed,d->razed,d->lost);
 text(fitted(own,700),38,536,MUTED);
 if(d->ended[0])text(fitted(d->ended,700),38,562,ACCENT);
 else{snprintf(own,sizeof own,"Ore harvested: %d",d->harvested);text(own,38,562,MUTED);}
 add(EM_DEBRIEF_CONTINUE,"Continue",758,550,220).active=true;
 v.held=-1;for(int i=0;i<int(v.seats.size());i++)if(v.seats[i].item==pressed)v.held=i;paint();
}
extern "C" int em_pick(int fx,int fy){int x,y;em_to_legacy(fx,fy,&x,&y);int n=hit(v.mousex,v.mousey);return n<0?-1:v.seats[n].item;}

extern "C" int em_mp_row_at(const MP_State *st,int x,int y){int n=virtualseat(x);return n>=0&&v.seats[n].action==MAPROW?v.seats[n].arg:mp_row_at(st,x,y);}
extern "C" void em_scroll(int delta){if(v.kind==OPS||v.kind==BROWSER||(v.kind==LOBBY&&((const SK_State*)v.owner)->picker))v.opstop=std::max(0,v.opstop+delta);}
extern "C" void em_opt_scroll(DOPT_State *st,int delta){
 if(v.kind==OPT&&v.owner==st&&v.drop>=0&&v.drop!=DOPT_VE_RESOLUTION)return;
 dopt_scroll(st,delta);
}
extern "C" int em_opt_key(DOPT_State *st,int key){
 int before=st->selected;bool enhanced=v.kind==OPT&&v.owner==st;
 if(enhanced&&st->page==DOPT_PAGE_ADVANCED&&v.drop>=0){
  if(key==DOPT_KEY_ESC){v.drop=v.popupChoice=-1;st->texdrop=-1;return DOPT_ACT_NONE;}
  const int count=dopt_drop_count(st,v.drop);
  int choice=v.popupChoice>=0?v.popupChoice:dopt_enh_drop_value(st,v.drop);
  if(key==DOPT_KEY_UP||key==DOPT_KEY_DOWN){
   for(int checked=0;checked<count;checked++){
    choice=(choice+count+(key==DOPT_KEY_UP?-1:1))%count;
    if(dopt_drop_listed(v.drop,choice)&&dopt_enh_drop_allowed(st,v.drop,choice)){
     v.popupChoice=choice;if(v.drop==DOPT_VE_RESOLUTION){if(choice<st->restop)st->restop=choice;if(choice>=st->restop+7)st->restop=choice-6;}
     game_enhanced_menu_sound(0);break;
    }
   }
  }else if(key==DOPT_KEY_ENTER&&dopt_enh_drop_allowed(st,v.drop,choice)){
   dopt_enh_choose(st,v.drop,choice);v.drop=v.popupChoice=-1;game_enhanced_menu_sound(1);
  }
  return DOPT_ACT_NONE;
 }
 if(enhanced&&key==DOPT_KEY_ENTER)for(const Seat &q:v.seats)if(q.item==before&&!q.disabled&&q.kind!=FIELD){game_enhanced_menu_sound(1);break;}
 if(enhanced&&key==DOPT_KEY_ENTER&&saves_blocked(st,st->selected)){dopt_open_page_notice(st,"NOTICE",em_route_notice(EM_ROUTE_SAVES));return DOPT_ACT_NONE;}

 if(v.kind==OPT&&v.owner==st&&st->page==DOPT_PAGE_ADVANCED&&
    ((key==DOPT_KEY_ESC&&st->texdrop<0)||(key==DOPT_KEY_ENTER&&st->selected==DOPT_A_OK)))
  return dopt_enh_action(st,DOPT_A_OK);
 int ret=dopt_key(st,key);
 /* the arrows step over rows Red Alert greys, as they step over any disabled row */
 for(int guard=0;guard<8&&enhanced&&st->page==DOPT_PAGE_ADVANCED&&ra_unavailable(st->selected)&&(key==DOPT_KEY_UP||key==DOPT_KEY_DOWN);guard++)ret=dopt_key(st,key);
 if(enhanced&&st->selected!=before&&(key==DOPT_KEY_UP||key==DOPT_KEY_DOWN||key==DOPT_KEY_LEFT||key==DOPT_KEY_RIGHT))game_enhanced_menu_sound(0);
 if(v.kind==OPT&&v.owner==st&&st->page==DOPT_PAGE_ADVANCED&&(key==DOPT_KEY_UP||key==DOPT_KEY_DOWN)){
  int id=st->selected;
  if(id<=DOPT_VE_FPS||id==DOPT_VE_GAMMA)v.section=0;
  else if(id==DOPT_VE_TEXSET||id==DOPT_VE_INFSET||id==DOPT_VE_WATER||id==DOPT_VE_BILINEAR||id==DOPT_VE_TREES||id==DOPT_VE_TIB3D||id==DOPT_VE_GRASS)v.section=1;
  else if(id>=DOPT_VE_SHADOWS&&id<=DOPT_VE_CRT)v.section=2;
  else if(id==DOPT_VE_UISCALE||id==DOPT_VE_PERSPECTIVE||id==DOPT_VE_NEWHUD||id==DOPT_VE_SMOOTH)v.section=3;
  else if(id>=DOPT_VE_NEWAUDIO&&id<DOPT_VE_COUNT)v.section=4;
 }
 if(st->texdrop>=0)v.drop=st->texdrop;else v.drop=-1;return ret;
}

/* Database shares the accepted housing, native button fittings and original font.
   Scene data comes from the engine table; model pixels are the runtime renderer's. */
extern "C" void em_database(const EM_Database *st){
 if(!load())return;begin(DATABASE,nullptr,"lobby-frame","DATABASE");
 /* Red Alert has a fifth category, its ships, so its tabs are narrower. */
 if(theme.ra){const char *cats[]={"Structures","Infantry","Vehicles","Ships","Aircraft"};
  for(int i=0;i<5;i++)add(1000+i,cats[i],38+i*168,86,160).active=st->category==i;}
 else{const char *cats[]={"Structures","Infantry","Vehicles","Aircraft"};
  for(int i=0;i<4;i++)add(1000+i,cats[i],38+i*200,86,192).active=st->category==i;}
 {Seat &q=add(4000,theme.ra?"Allied":"GDI",890,86,132);q.active=st->house==1;q.badge=0;}
 {Seat &q=add(4001,theme.ra?"Soviet":"Nod",1046,86,132);q.active=st->house==2;q.badge=1;}
 well("ROSTER",38,150,312,368);well("MODEL PREVIEW",370,150,368,368);well("FIELD DATA",758,150,420,368);
 for(int i=0;i<8&&i+st->top<st->count;i++)add(2000+i+st->top,st->names[i+st->top],56,200+i*32,276,30,ROW).active=i+st->top==st->selected;
 add(3000,"Previous",56,474,132,32).disabled=st->top==0;
 add(3001,"Next",200,474,132,32).disabled=st->top+8>=st->count;
 text(std::to_string(st->selected+1)+" / "+std::to_string(st->count),194,458,MUTED,true);
 const char *tabs[]={"Info","Stats","Matchups","Tech"};
 const int tabx[]={770,858,946,1086},tabw[]={80,80,132,80};
 for(int i=0;i<4;i++)add(5000+i,tabs[i],tabx[i],200,tabw[i],40).active=st->detail==i;
 auto names=wrapped(st->name?st->name:"",384);int y=250;
 for(auto &line:names){text(line,776,y,TEXT);y+=24;}
 text(std::string(st->code?st->code:"")+" / $"+std::to_string(st->cost),776,y+4,ACCENT);y+=40;
 if(st->detail==0){
  note(st->brief?st->brief:"",776,y,384,5,MUTED,24);
  text("Armour",776,454,MUTED);text(st->armour?st->armour:"--",952,454,TEXT);
  text("Hit points",776,486,MUTED);text(std::to_string(st->hp),952,486,ACCENT);
 }else if(st->detail==1){
  const char *labels[]={"Damage","Speed","Attack interval","Range"};
  char range[48];snprintf(range,sizeof range,"%.1f cells",st->range256/256.f);
  std::string vals[]={st->damage<0?"Unarmed":std::to_string(st->damage),st->speed<0?"Stationary":std::to_string(st->speed)+" mph",st->rof<0?"--":std::to_string(st->rof)+" ticks",st->range256<0?"--":range};
  for(int i=0;i<4;i++){text(labels[i],776,y+i*28,MUTED);text(vals[i],1000,y+i*28,TEXT);}
 }else if(st->detail==2){
  text("Strong against",776,y,ACCENT);auto strong=wrapped(st->strong&&*st->strong?st->strong:"None",384);
  for(auto &line:strong){text(line,776,y+26,MUTED);y+=22;}y+=36;
  text("Weak against",776,y,ACCENT);note(st->weak&&*st->weak?st->weak:"None",776,y+26,384,7,MUTED,22);
 }else{
  text("Unlocks",776,y,ACCENT);note(st->unlocks&&*st->unlocks?st->unlocks:"None",776,y+28,384,7,MUTED,22);
 }
 /* A Red Alert type without an Enhanced model shows its 1996 cameo, at twice its size. */
 if(!st->model_available&&theme.ra&&st->cameo&&st->cameo_w>0&&st->cameo_h>0){Art a;a.w=st->cameo_w;a.h=st->cameo_h;a.pixels.assign(st->cameo,st->cameo+a.w*a.h*4);blit(a,388+(332-a.w*2)/2,202+(296-a.h*2)/2,2);}
 else if(!st->model_available)note("Enhanced model not installed",390,324,328,3,MUTED,26);
 text("Arrows: browse / Esc: close",38,564,MUTED);add(6000,"Close",958,550,220);paint();
}
extern "C" int em_database_view(int *x,int *y,int *w,int *h){
 if(v.kind!=DATABASE)return 0;*x=v.x+388*v.scale;*y=v.y+202*v.scale;*w=332*v.scale;*h=296*v.scale;return 1;
}
