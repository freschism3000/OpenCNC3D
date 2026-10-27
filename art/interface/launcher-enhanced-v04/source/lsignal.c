/* A restrained EVA transmission layer over the existing Enhanced launcher.
 * Native phosphor rows, a soft scan beam and sparse packet interference share a
 * closed sixteen-second clock. No image, geometry or interaction is replaced. */
#include "lsignal.h"
#include <stddef.h>

static unsigned ls_signal_hash(unsigned seed)
{
 seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;
 seed*=0x846ca68bu;return seed^(seed>>16);
}

void ls_signal_apply(unsigned char *rgba,int w,int h,unsigned elapsed_ms)
{
 static const unsigned event_start[3]={74u,209u,373u};
 static const unsigned event_duration[3]={5u,4u,5u};
 unsigned phase,tick,event=3u,event_age=0u,i;
 int y,x,sweep,packet_x[3],packet_y[3],packet_w[3],packet_strength=0;

 if(!rgba||w<=0||h<=0)return;
 phase=elapsed_ms%LS_SIGNAL_PERIOD_MS;
 /* A time-owned thirty-hertz raster, independent of the presentation rate. */
 tick=phase*30u/1000u;
 sweep=(int)(tick*(unsigned)(h+112)/480u)-56;
 for(i=0;i<3u;i++){
  if(tick>=event_start[i]&&tick<event_start[i]+event_duration[i]){
   event=i;event_age=tick-event_start[i];break;
  }
 }
 if(event<3u){
  unsigned duration=event_duration[event];
  unsigned distance=event_age<duration-1u-event_age?event_age:duration-1u-event_age;
  /* Small attack/release, with no random brightness changes between frames. */
  packet_strength=3+(int)distance*2;
  for(i=0;i<3u;i++){
   unsigned seed=ls_signal_hash(0x455641u+event*131u+i*977u);
   packet_y[i]=(int)(seed%(unsigned)h);
   packet_x[i]=(int)(ls_signal_hash(seed)%(unsigned)w);
   packet_w[i]=24+(int)((seed>>9)%64u);
  }
 }

 for(y=0;y<h;y++){
  int distance=y-sweep,beam,phosphor=(y&3)==0?1:0;
  int packet_row[3],has_packet=0;
  unsigned char *p=rgba+(size_t)y*(size_t)w*4u;
  if(distance<0)distance=-distance;
  beam=distance<56?(56-distance)*6/56:0;
  for(i=0;i<3u;i++){
   packet_row[i]=event<3u&&y==packet_y[i];
   if(packet_row[i])has_packet=1;
  }
  if(!beam&&!phosphor&&!has_packet)continue;
  for(x=0;x<w;x++,p+=4){
   int drive=beam+phosphor;
   unsigned luminance,attenuation,green,subtract;
   if(has_packet)for(i=0;i<3u;i++){
    if(packet_row[i]&&x>=packet_x[i]&&x-packet_x[i]<packet_w[i])drive+=packet_strength;
   }
   if(!drive)continue;
   if(drive>14)drive=14;
   luminance=(54u*p[0]+183u*p[1]+19u*p[2])>>8;
   /* Keep native black text shadows and cut-corner exterior pixels exact.
    * Above medium grey, attenuate toward zero at white: bright lettering and
    * logo highlights get at most a few channel values of transmission tint. */
   if(luminance<8u)continue;
   attenuation=luminance<=96u?255u:(255u-luminance)*255u/159u;
   green=((unsigned)drive*attenuation+127u)/255u;
   subtract=(green+2u)/4u;
   p[0]=(unsigned char)(p[0]>subtract?p[0]-subtract:0u);
   p[1]=(unsigned char)(p[1]+green>255u?255u:p[1]+green);
   p[2]=(unsigned char)(p[2]>subtract?p[2]-subtract:0u);
   /* p[3] deliberately untouched, including non-opaque canvas inputs. */
  }
 }
}
