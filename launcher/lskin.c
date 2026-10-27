/* The launcher's independent CPU compositor for the game's exact Enhanced kit.
 * Reads reviewed RGBA art, preserves native bevel/fastener pixels, and shares
 * no game controller state. SDL, OpenGL and network code stay outside it. */
#include "lskin.h"
#include "lskin_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define LS_W 1016
#define LS_H 636
#define LS_MAX_ART 256
#define LS_MAX_BYTES (96u*1024u*1024u)
typedef struct LS_Art { char name[64]; int w,h; unsigned char *rgba; } LS_Art;
static LS_Art ls_art[LS_MAX_ART];
static int ls_count,ls_ready;
static int ls_clip_x,ls_clip_y,ls_clip_right=LS_W,ls_clip_bottom=LS_H;
static unsigned char ls_canvas[LS_W*LS_H*4];
static const unsigned ls_roles[4]={0xe6e5da,0xaeb5ac,0x6ddf4c,0xaeb5ac};

int ls_native_x(int x){return 28+x*3;}
int ls_native_y(int y){return 18+y*3;}
int ls_width(void){return LS_W;}
int ls_height(void){return LS_H;}
unsigned char *ls_pixels(void){return ls_canvas;}
int ls_enabled(void){return ls_ready;}
void ls_clip_reset(void){ls_clip_x=ls_clip_y=0;ls_clip_right=LS_W;ls_clip_bottom=LS_H;}
void ls_clip(int x,int y,int w,int h){ls_clip_x=ls_native_x(x);ls_clip_y=ls_native_y(y);ls_clip_right=ls_clip_x+(w>0?w*3:0);ls_clip_bottom=ls_clip_y+(h>0?h*3:0);}
static unsigned ls_colour(int role){return ls_roles[role>=0&&role<4?role:0];}
static LS_Art *ls_find(const char *name){int i;for(i=0;i<ls_count;i++)if(!strcmp(ls_art[i].name,name))return &ls_art[i];return NULL;}
static int ls_u32(FILE *f,unsigned *v){unsigned char b[4];if(fread(b,1,4,f)!=4)return 0;*v=(unsigned)b[0]|(unsigned)b[1]<<8|(unsigned)b[2]<<16|(unsigned)b[3]<<24;return 1;}
void ls_free(void){int i;for(i=0;i<ls_count;i++){free(ls_art[i].rgba);memset(&ls_art[i],0,sizeof(ls_art[i]));}ls_count=ls_ready=0;}
int ls_load(const char *path){
 FILE *f;char magic[8];unsigned count,i,w,h,bytes,total=0;LS_Art *a;
 ls_free();if(!path||!*path)return 0;f=fopen(path,"rb");if(!f)return 0;
 if(fread(magic,1,8,f)!=8||memcmp(magic,"ENMENU06",8)||!ls_u32(f,&count)||count>LS_MAX_ART)goto bad;
 for(i=0;i<count;i++){
  a=&ls_art[ls_count];
  if(fread(a->name,1,64,f)!=64||!memchr(a->name,0,64)||!a->name[0])goto bad;
  if(ls_find(a->name))goto bad;
  if(!ls_u32(f,&w)||!ls_u32(f,&h)||!ls_u32(f,&bytes)||w<1||h<1||w>4096||h>2048||bytes!=w*h*4u||bytes>LS_MAX_BYTES-total)goto bad;
  a->w=(int)w;a->h=(int)h;a->rgba=(unsigned char *)malloc(bytes);if(!a->rgba)goto bad;
  ls_count++;total+=bytes;if(fread(a->rgba,1,bytes,f)!=bytes)goto bad;
 }
 if(!ls_find("visuals-frame")||!ls_find("font_menu")||!ls_find("button-500-40-normal"))goto bad;
 a=ls_find("visuals-frame");if(a->w!=LS_W||a->h!=LS_H)goto bad;
 a=ls_find("font_menu");if(a->w!=1808||a->h!=8)goto bad;
 fclose(f);ls_ready=1;return 1;
bad: fclose(f);ls_free();return 0;
}
void ls_pixel_rgba(int x,int y,unsigned r,unsigned g,unsigned b,unsigned alpha){
 unsigned char *d;unsigned ia;
 if(x<0||y<0||x>=LS_W||y>=LS_H||x<ls_clip_x||y<ls_clip_y||x>=ls_clip_right||y>=ls_clip_bottom||!alpha)return;
 d=ls_canvas+((size_t)y*LS_W+x)*4;
 if(alpha>=255){d[0]=(unsigned char)r;d[1]=(unsigned char)g;d[2]=(unsigned char)b;d[3]=255;return;}
 ia=255-alpha;d[0]=(unsigned char)((r*alpha+d[0]*ia+127)/255);d[1]=(unsigned char)((g*alpha+d[1]*ia+127)/255);d[2]=(unsigned char)((b*alpha+d[2]*ia+127)/255);d[3]=255;
}
void ls_pixel_rect(int x,int y,int w,int h,unsigned col){
 int xx,yy,x1,y1;if(w<=0||h<=0)return;x1=x+w;y1=y+h;if(x<0)x=0;if(y<0)y=0;if(x1>LS_W)x1=LS_W;if(y1>LS_H)y1=LS_H;
 for(yy=y;yy<y1;yy++)for(xx=x;xx<x1;xx++)ls_pixel_rgba(xx,yy,(col>>16)&255,(col>>8)&255,col&255,255);
}
static void ls_copy_pixel(const LS_Art *a,int sx,int sy,int x,int y){const unsigned char *p;if(sx<0||sy<0||sx>=a->w||sy>=a->h)return;p=a->rgba+((size_t)sy*a->w+sx)*4;ls_pixel_rgba(x,y,p[0],p[1],p[2],p[3]);}
void ls_image(const char *id,int native_x,int native_y){LS_Art *a;int x,y;if(!ls_ready||!id)return;a=ls_find(id);if(!a)return;for(y=0;y<a->h;y++)for(x=0;x<a->w;x++)ls_copy_pixel(a,x,y,native_x+x,native_y+y);}
void ls_rgba_image(const unsigned char *rgba,int w,int h,int native_x,int native_y)
{
 int x,y;const unsigned char *p;
 if(!ls_ready||!rgba||w<1||h<1)return;
 for(y=0;y<h;y++)for(x=0;x<w;x++){
  p=rgba+((size_t)y*w+x)*4;
  ls_pixel_rgba(native_x+x,native_y+y,p[0],p[1],p[2],p[3]);
 }
}
void ls_begin(void){LS_Art *a;int x,y;ls_clip_reset();memset(ls_canvas,0,sizeof(ls_canvas));for(y=0;y<LS_H;y++)for(x=0;x<LS_W;x++)ls_canvas[(y*LS_W+x)*4+3]=255;if(!ls_ready)return;a=ls_find("visuals-frame");for(y=0;y<LS_H;y++)for(x=0;x<LS_W;x++)ls_copy_pixel(a,x,y,x,y);}
int ls_text_width(const char *text){int width=0;unsigned c;const unsigned char *p=(const unsigned char *)text;if(!p)return 0;while((c=*p++)!=0)if(c<sizeof(font_advance)/sizeof(font_advance[0])&&font_advance[c]>0)width+=font_advance[c]*2;return width;}
void ls_text(const char *text,int x,int y,int role,int align){
 LS_Art *font;const unsigned char *s,*p;unsigned c,col;int xx,yy,px,py,dx,dy;
 if(!ls_ready||!text)return;font=ls_find("font_menu");px=ls_native_x(x);py=ls_native_y(y);if(align==1)px-=ls_text_width(text)/2;else if(align==2)px-=ls_text_width(text);col=ls_colour(role);
 for(s=(const unsigned char *)text;(c=*s)!=0;s++){
  if(c>=sizeof(font_advance)/sizeof(font_advance[0]))continue;
  for(yy=0;yy<8;yy++)for(xx=0;xx<8;xx++){
   unsigned ink;p=font->rgba+((size_t)yy*font->w+c*8+xx)*4;if(!p[3])continue;ink=p[0]<64&&p[1]<64&&p[2]<64?0:col;
   for(dy=0;dy<2;dy++)for(dx=0;dx<2;dx++)ls_pixel_rgba(px+xx*2+dx,py+yy*2+dy,(ink>>16)&255,(ink>>8)&255,ink&255,p[3]);
  }
  if(font_advance[c]>0)px+=font_advance[c]*2;
 }
}
/* Fitted native slices: keep all 12-pixel corner/fastener seats and the full
 * bevel profile. Middle pixels repeat only the source's interior material;
 * no destination texel is resampled and no source corner appears on a rail. */
static int ls_slice(int p,int extent,int source,int guard){int span;if(p<guard)return p;if(p>=extent-guard)return source-(extent-p);span=source-2*guard;return guard+(span>0?(p-guard)%span:0);}
void ls_button(int x,int y,int w,int h,int pressed,int hot,int disabled,int active){
 LS_Art *a;char name[80];const char *state;int xx,yy,sx,sy,nx,ny,nw,nh,gx,gy;
 if(!ls_ready||w<=0||h<=0)return;state=disabled?"disabled":pressed?"pressed":hot?(active?"activehover":"hover"):active?"active":"normal";
 snprintf(name,sizeof(name),"button-500-40-%s",state);a=ls_find(name);if(!a)return;
 nx=ls_native_x(x);ny=ls_native_y(y);nw=w*3;nh=h*3;gx=nw<24?nw/2:12;gy=nh<24?nh/2:12;
 for(yy=0;yy<nh;yy++){
  sy=ls_slice(yy,nh,a->h,gy);
  for(xx=0;xx<nw;xx++){
   int source_y=sy;sx=ls_slice(xx,nw,a->w,gx);
   if(nh>a->h&&xx>=gx&&xx<nw-gx&&yy>=gy&&yy<nh-gy){
    /* A tall module has an independently fitted face, not a repeated short
     * button reflection. Compose its finite material field from native 3x3
     * samples. The hash selects intact source texels, never interpolates them;
     * source bevels, corners and fastener seats remain outside this field. */
    unsigned seed=(unsigned)((xx-gx)/3)*73856093u^(unsigned)((yy-gy)/3)*19349663u^(unsigned)nw*83492791u;
    seed^=seed>>13;seed*=1274126177u;
    sx=12+(int)(seed%158u)*3+(xx-gx)%3;
    source_y=12+(int)((seed>>9)%5u)*3+(yy-gy)%3;
   }
   ls_copy_pixel(a,sx,source_y,nx+xx,ny+yy);
  }
 }
}
static void ls_native_well(int x,int y,int w,int h){
 int i;if(w<9||h<9)return;ls_pixel_rect(x+4,y,w-8,2,0x080a09);ls_pixel_rect(x,y+4,2,h-8,0x080a09);ls_pixel_rect(x+w-2,y+4,2,h-8,0x080a09);ls_pixel_rect(x+4,y+h-2,w-8,2,0x080a09);
 for(i=0;i<4;i++){ls_pixel_rect(x+3-i,y+i,1,1,0x080a09);ls_pixel_rect(x+w-4+i,y+i,1,1,0x080a09);ls_pixel_rect(x+3-i,y+h-1-i,1,1,0x080a09);ls_pixel_rect(x+w-4+i,y+h-1-i,1,1,0x080a09);}
 ls_pixel_rect(x+4,y+h-2,w-8,1,0x4a4b42);ls_pixel_rect(x+w-2,y+5,1,h-10,0x4a4b42);
}
void ls_well(int x,int y,int w,int h){if(ls_ready)ls_native_well(ls_native_x(x),ls_native_y(y),w*3,h*3);}
void ls_outline(int x,int y,int w,int h,int role){int nx=ls_native_x(x),ny=ls_native_y(y),nw=w*3,nh=h*3;unsigned col=ls_colour(role);if(!ls_ready||nw<5||nh<5)return;ls_pixel_rect(nx+4,ny,nw-8,2,col);ls_pixel_rect(nx+4,ny+nh-2,nw-8,2,col);ls_pixel_rect(nx,ny+4,2,nh-8,col);ls_pixel_rect(nx+nw-2,ny+4,2,nh-8,col);}
void ls_gauge(int x,int y,int w,int h,int frac,int phase){int nx,ny,nw,nh,fill,xx;if(!ls_ready||w<4||h<3)return;nx=ls_native_x(x);ny=ls_native_y(y);nw=w*3;nh=h*3;ls_pixel_rect(nx+2,ny+2,nw-4,nh-4,0x1b2618);ls_native_well(nx,ny,nw,nh);if(frac<0){for(xx=0;xx<nw-6;xx++)if((xx+phase*3)%24<12)ls_pixel_rect(nx+3+xx,ny+3,1,nh-6,0x527d35);}else{if(frac>1000)frac=1000;fill=(nw-6)*frac/1000;ls_pixel_rect(nx+3,ny+3,fill,nh-6,0x527d35);ls_pixel_rect(nx+3,ny+3,fill,1,0xa5c777);}}
void ls_plus(int cx,int cy,int arm,int thick,int role){int x=ls_native_x(cx),y=ls_native_y(cy),a=arm*3,t=thick*3;unsigned col=ls_colour(role);if(!ls_ready||a<1||t<1)return;ls_pixel_rect(x-a,y-t/2,a*2+1,t,col);ls_pixel_rect(x-t/2,y-a,t,a*2+1,col);}
void ls_check(int x,int y,int size,int checked,int hot,int disabled){
 int nx=ls_native_x(x),ny=ls_native_y(y),n=size*3,i,xx,yy;unsigned col=disabled?0x797f73:0x72df4c;static const int pts[7][2]={{5,10},{7,12},{9,14},{11,12},{13,10},{15,8},{17,6}};
 if(!ls_ready||n<8)return;ls_pixel_rect(nx,ny,n,n,hot&&!disabled?0x959b86:0x787b6b);ls_pixel_rect(nx+1,ny+1,n-2,n-2,0x10130f);ls_pixel_rect(nx+2,ny+n-2,n-4,1,0x373b32);ls_pixel_rect(nx+n-2,ny+2,1,n-4,0x373b32);
 if(checked)for(i=0;i<7;i++){xx=nx+pts[i][0]*(n-2)/22;yy=ny+pts[i][1]*(n-2)/22;ls_pixel_rect(xx-1,yy-1,3,3,col);}
}
void ls_gear(int x,int y,int w,int h,int pressed,int hot,int disabled){
 int nx,ny,cx,cy,r,xx,yy,d,inner;unsigned col;ls_button(x,y,w,h,pressed,hot,disabled,0);if(!ls_ready)return;nx=ls_native_x(x);ny=ls_native_y(y);cx=nx+w*3/2;cy=ny+h*3/2;r=(w<h?w:h)*3/2-9;if(r<3)r=3;inner=r/3;col=ls_colour(disabled?LS_DISABLED:hot?LS_ACCENT:LS_TEXT);
 for(yy=-r;yy<=r;yy++)for(xx=-r;xx<=r;xx++){d=xx*xx+yy*yy;if(d<inner*inner)continue;if(d<r*r&&((xx<r-3&&xx>3-r&&yy<r-3&&yy>3-r)||xx*xx<9||yy*yy<9||(xx-yy)*(xx-yy)<9||(xx+yy)*(xx+yy)<9))ls_pixel_rect(cx+xx,cy+yy+(pressed?1:0),1,1,col);}
}
