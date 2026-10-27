/* EVA terminal transmission treatment. The metal artwork stays spatially exact:
 * only bounded green phosphor energy is mixed into the freshly composed canvas.
 * Global motion closes over sixteen seconds; reactions use the same clock and a
 * short, explicit click age. This code has no SDL, heap or random-state dependency. */
#include "lsignal.h"
#include <stddef.h>

static unsigned ls_signal_hash(unsigned seed)
{
 seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;
 seed*=0x846ca68bu;return seed^(seed>>16);
}

static int ls_signal_abs(int n)
{
 return n<0?-n:n;
}

static int ls_signal_min(int a,int b)
{
 return a<b?a:b;
}

/* Add energy toward a green target relative to the existing red/blue material,
 * rather than adding the same tint repeatedly where effects overlap. Bright
 * atlas ink gets at most two levels; exact black shadows/exterior stay exact.
 * Native coloured artwork is protected in proportion to its chroma. */
static void ls_signal_green(unsigned char *p,int drive)
{
 unsigned luminance,attenuation,green,neutral,target,delta,subtract;
 int chroma,lo,hi;
 if(drive<=0)return;
 if(drive>50)drive=50;
 luminance=(54u*p[0]+183u*p[1]+19u*p[2])>>8;
 if(luminance<8u)return;
 attenuation=luminance<=96u?255u:(255u-luminance)*255u/159u;
 hi=p[0]>p[1]?p[0]:p[1];if(p[2]>hi)hi=p[2];
 lo=p[0]<p[1]?p[0]:p[1];if(p[2]<lo)lo=p[2];
 chroma=hi-lo;
 if(chroma>48)attenuation=attenuation*48u/(unsigned)chroma;
 green=((unsigned)drive*attenuation+127u)/255u;
 if(luminance>=192u&&green>2u)green=2u;
 neutral=((p[0]<p[1]?p[0]:p[1])+(p[2]<p[1]?p[2]:p[1]))/2u;
 target=neutral+green;if(target>255u)target=255u;
 if(target<=p[1])return;
 delta=target-p[1];subtract=(delta+3u)/4u;
 p[0]=(unsigned char)(p[0]>subtract?p[0]-subtract:0u);
 p[1]=(unsigned char)target;
 p[2]=(unsigned char)(p[2]>subtract?p[2]-subtract:0u);
 /* Straight RGBA alpha is deliberately untouched, including transparent inputs. */
}

/* Distance along a rectangular signal rail, or -1 outside its two-pixel lane. */
static int ls_signal_rail(int x,int y,int left,int top,int right,int bottom)
{
 int rw=right-left,rh=bottom-top;
 if(rw<=0||rh<=0)return -1;
 if(y>=top&&y<=top+1&&x>=left&&x<=right)return x-left;
 if(x>=right-1&&x<=right&&y>=top&&y<=bottom)return rw+y-top;
 if(y>=bottom-1&&y<=bottom&&x>=left&&x<=right)return rw+rh+right-x;
 if(x>=left&&x<=left+1&&y>=top&&y<=bottom)return rw+rh+rw+bottom-y;
 return -1;
}

void ls_signal_apply(unsigned char *rgba,int w,int h,unsigned elapsed_ms)
{
 unsigned phase,tick,i;
 int y,x,sweep,beam,rail_length,rail_head;
 int packet_x[6],packet_y[6],packet_w[6],packet_strength[6];
 if(!rgba||w<=0||h<=0)return;
 phase=elapsed_ms%LS_SIGNAL_PERIOD_MS;
 /* Raster motion is time-owned at 60 Hz; presentation may run at any rate. */
 tick=phase*60u/1000u;
 sweep=(int)(tick*(unsigned)(h+160)/960u)-80;
 rail_length=2*(w-45+h-43);
 rail_head=rail_length>0?(int)(phase*(unsigned)rail_length/LS_SIGNAL_PERIOD_MS):0;
 for(i=0;i<6u;i++){
  unsigned seed=ls_signal_hash(0x455641u+i*977u);
  unsigned start=(i*LS_SIGNAL_PERIOD_MS/6u+1137u)%LS_SIGNAL_PERIOD_MS;
  unsigned age=(phase+LS_SIGNAL_PERIOD_MS-start)%LS_SIGNAL_PERIOD_MS;
  packet_y[i]=(int)(seed%(unsigned)h);
  packet_x[i]=24+(int)(ls_signal_hash(seed)%(unsigned)(w>48?w-48:1));
  packet_w[i]=42+(int)((seed>>9)%110u);
  packet_strength[i]=age<720u?(int)((720u-age)*24u/720u):0;
 }
 for(y=0;y<h;y++){
  int distance=ls_signal_abs(y-sweep),phosphor=(y&3)==0?3:0;
  int side_row=((y+11)/8)%3==0;
  int packet_row[6],has_packet=0;
  int rail_row=y>=14&&y<=h-29;
  int rail_horizontal=(y>=14&&y<=15)||(y>=h-30&&y<=h-29);
  unsigned char *p=rgba+(size_t)y*(size_t)w*4u;
  beam=distance<80?(80-distance)*22/80:0;
  if(distance<2)beam=30;
  for(i=0;i<6u;i++){
   packet_row[i]=packet_strength[i]&&y>=packet_y[i]&&y<packet_y[i]+2;
   if(packet_row[i])has_packet=1;
  }
  for(x=0;x<w;x++,p+=4){
   int drive=beam+phosphor,rail;
   /* Sparse packet streaks travel through the whole terminal, with an attack
    * edge and a softly falling tail. No full-frame blinking or image shifting. */
   if(has_packet)for(i=0;i<6u;i++)if(packet_row[i]&&x>=packet_x[i]&&x-packet_x[i]<packet_w[i]){
    int tail=(packet_w[i]-(x-packet_x[i]))*packet_strength[i]/packet_w[i];
    drive+=tail;
   }
   rail=rail_row&&(rail_horizontal||x==22||x==23||x==w-24||x==w-23)
        ?ls_signal_rail(x,y,22,14,w-23,h-29):-1;
   if(rail>=0&&rail_length>0){
    int age=(rail-rail_head+rail_length)%rail_length;
    if(age<112)drive+=28*(112-age)/112;
    age=(age+rail_length/2)%rail_length;
    if(age<72)drive+=20*(72-age)/72;
   }
   /* Quiet EVA data ticks in the empty side gutters. Their deterministic row
    * gates move at two hertz while the running rail carries the eye. */
   if(y>76&&y<h-84&&side_row&&((x>=37&&x<=40)||(x>=w-41&&x<=w-38))){
    unsigned bits=ls_signal_hash((unsigned)(y/8)*41u+phase/500u);
    if((bits&3u)==0u)drive+=14;
   }
   if(drive>32)drive=32;
   ls_signal_green(p,drive);
  }
 }
}

void ls_signal_react(unsigned char *rgba,int w,int h,int x,int y,int rw,int rh,
                     unsigned elapsed_ms,int hot,int pressed,unsigned click_age_ms)
{
 unsigned phase;
 int left,top,right,bottom,px,py,perimeter,head,pulse=0,clipped_width,clipped_height;
 if(!rgba||w<=0||h<=0||rw<=0||rh<=0)return;
 if(!hot&&!pressed&&click_age_ms>=LS_SIGNAL_CLICK_MS)return;
 /* These early clips also keep x+rw/y+rh from being evaluated off canvas. */
 if(x>=w||y>=h||x<=-rw||y<=-rh)return;
 left=x<0?0:x;top=y<0?0:y;
 clipped_width=x<0?rw+x:rw;clipped_height=y<0?rh+y:rh;
 right=clipped_width>w-left?w:left+clipped_width;
 bottom=clipped_height>h-top?h:top+clipped_height;
 phase=elapsed_ms%1600u;
 perimeter=2*(rw+rh-2);
 head=perimeter>0?(int)(phase*(unsigned)perimeter/1600u):0;
 if(click_age_ms<LS_SIGNAL_CLICK_MS)
  pulse=(int)((LS_SIGNAL_CLICK_MS-click_age_ms)*38u/LS_SIGNAL_CLICK_MS);
 for(py=top;py<bottom;py++){
  unsigned char *p=rgba+((size_t)py*(size_t)w+(size_t)left)*4u;
  int ry=py-y;
  for(px=left;px<right;px++,p+=4){
   int rx=px-x,edge=ls_signal_min(ls_signal_min(rx,rw-1-rx),ls_signal_min(ry,rh-1-ry));
   int drive=0,position,age;
   if(hot){
    if(edge<6)drive=8+(6-edge)*2;
    /* Moving edge energy makes the current target explicit. Corner brackets
     * sit on the source bevel, rather than adding a new box or moving its art. */
    if(edge<3&&perimeter>0){
     if(ry<=rx&&ry<rw-rx)position=rx;
     else if(rw-1-rx<=ry&&rw-1-rx<rh-ry)position=rw-1+ry;
     else if(rh-1-ry<=rx)position=rw+rh-2+rw-1-rx;
     else position=2*rw+rh-3+rh-1-ry;
     age=(position-head+perimeter)%perimeter;
     if(age<48)drive+=30*(48-age)/48;
    }
    if(edge<3&&((rx<17||rx>=rw-17)&&(ry<17||ry>=rh-17)))drive+=9;
    if(edge>=6){
     int scan=(int)(phase*(unsigned)(rh+24)/1600u)-12;
     int d=ls_signal_abs(ry-scan);
     if(d<12)drive+=(12-d)*7/12;
    }
   }
   if(pressed){
    drive+=edge<5?28:5;
    if(edge==3||edge==4)drive+=8;
   }
   if(pulse){
    int nx=rw>1?ls_signal_abs(2*rx-(rw-1))*1024/(rw-1):0;
    int ny=rh>1?ls_signal_abs(2*ry-(rh-1))*1024/(rh-1):0;
    int radius=nx>ny?nx:ny;
    int wave=(int)(click_age_ms*1250u/LS_SIGNAL_CLICK_MS);
    int d=ls_signal_abs(radius-wave);
    drive+=pulse/7;
    if(d<180)drive+=(180-d)*pulse/180;
    if(edge<3)drive+=pulse/3;
   }
   if(drive>50)drive=50;
   ls_signal_green(p,drive);
  }
 }
}

void ls_signal_idle(unsigned char *rgba,int w,int h,int x,int y,int rw,int rh,
                    unsigned elapsed_ms,unsigned identity)
{
 unsigned phase,ping_age;
 int left,top,right,bottom,px,py,head=-36,ping=0;
 int clipped_width,clipped_height;
 if(!rgba||w<=0||h<=0||rw<=0||rh<=0)return;
 if(x>=w||y>=h||x<=-rw||y<=-rh)return;
 left=x<0?0:x;top=y<0?0:y;
 clipped_width=x<0?rw+x:rw;clipped_height=y<0?rh+y:rh;
 right=clipped_width>w-left?w:left+clipped_width;
 bottom=clipped_height>h-top?h:top+clipped_height;
 phase=(elapsed_ms%8000u+(identity%8000u)*787u)%8000u;
 if(phase<4000u)head=(int)(phase*(unsigned)(rw+36)/4000u)-36;
 ping_age=(phase+8000u-4750u)%8000u;
 if(ping_age<150u){
  unsigned envelope=ping_age<75u?ping_age:150u-ping_age;
  ping=(int)(envelope*18u/75u);
 }
 if(head<0&&!ping)return;
 for(py=top;py<bottom;py++){
  unsigned char *p=rgba+((size_t)py*(size_t)w+(size_t)left)*4u;
  int ry=py-y;
  for(px=left;px<right;px++,p+=4){
   int rx=px-x,drive=0;
   if(phase<4000u&&ry>=rh-5&&ry<=rh-4){
    int distance=head-rx;
    if(distance>=0&&distance<32)drive=(32-distance)*18/32;
   }
   if(ping){
    int dx=(identity&1u)?rw-1-rx:rx;
    int dy=(identity&2u)?rh-1-ry:ry;
    if(dx<12&&dy<12&&(dx<2||dy<2)&&ping>drive)drive=ping;
   }
   ls_signal_green(p,drive);
  }
 }
}
