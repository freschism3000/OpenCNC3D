#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../game/heading_smooth.h"
static bool near(float a,float b){return fabsf(a-b)<0.0001f;}
int main(){
 assert(near(heading_interpolate(254,2,.25f),255));
 assert(near(heading_interpolate(254,2,.5f),0));
 assert(near(heading_interpolate(2,254,.25f),1));
 assert(near(heading_interpolate(2,254,.5f),0));
 assert(near(heading_interpolate(2,254,.75f),255));
 assert(near(heading_interpolate(0,1,.25f),.25f));
 assert(near(heading_interpolate(-1,32,.5f),32));
 assert(near(heading_interpolate(32,-1,.5f),-1));
 for(int a=0;a<256;a++)for(int b=0;b<256;b++){
  const int delta=((b-a+128)&255)-128;
  for(int i=0;i<=16;i++){
   const float t=(float)i/16.f,v=heading_interpolate(a,b,t);
   assert(v>=0&&v<256);
   float d=v-a;
   if(d>127.999f)d-=256;if(d<-128.001f)d+=256;
   assert(near(d,delta*t));
  }
  assert(near(heading_interpolate(a,b,-1),a));
  assert(near(heading_interpolate(a,b,2),b));
 }
 puts("PASS: all 65536 heading pairs, 17 subframes each, wrap in both directions, fractional output, unknown angles and clamping.");
}
