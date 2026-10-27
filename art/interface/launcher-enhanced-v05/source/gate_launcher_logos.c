/* Native source-pixel contract for the exact shared module ring. No SDL/GL. */
#include "llogo.h"
#include "launcher_logos.h"
#include "pngwrite.h"
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv)
{
 unsigned char cnc[160*160*4],ra[160*160*4],mask[160*160*4];
 int x,y,whole_exact=1,ring_exact=1,ring_pixels=0,visible_ring=0,red_leaks=0,red_face_pixels=0;
 unsigned char *a,*b,*m;char path[2048];
 if(argc!=2){fprintf(stderr,"usage: %s OUTPUT_DIRECTORY\n",argv[0]);return 2;}
 for(y=0;y<160;y++)for(x=0;x<160;x++){
  unsigned coverage=ll_logo_aperture(x,y);int offset=(y*160+x)*4;
  a=cnc+offset;b=ra+offset;m=mask+offset;
  ll_logo_sample(launcher_module_rgba,launcher_ra_rgba,160,160,LL_LOGO_CNC,x,y,a);
  ll_logo_sample(launcher_module_rgba,launcher_ra_rgba,160,160,LL_LOGO_RA,x,y,b);
  if(memcmp(a,launcher_module_rgba+offset,4))whole_exact=0;
  m[0]=m[1]=m[2]=(unsigned char)coverage;m[3]=255;
  /* Dominant red detects the C enamel without classing warm steel reflections. */
  if(a[3]>=128&&a[0]>80&&a[0]>2*a[1]&&a[0]>2*a[2])red_face_pixels++;
  if(!coverage){
   ring_pixels++;if(a[3])visible_ring++;
   if(memcmp(a,b,4))ring_exact=0;
   if(a[3]>=128&&a[0]>80&&a[0]>2*a[1]&&a[0]>2*a[2])red_leaks++;
  }
 }
 if(!whole_exact||!ring_exact||red_leaks||visible_ring<5000||red_face_pixels<500){
  fprintf(stderr,"LOGOGATE|FAIL|cnc_exact=%d|ring_exact=%d|red_leaks=%d|visible=%d\n",whole_exact,ring_exact,red_leaks,visible_ring);return 1;
 }
 snprintf(path,sizeof path,"%s/CNC-native.png",argv[1]);if(!png_write_rgba(path,cnc,160,160))return 1;
 snprintf(path,sizeof path,"%s/RA-native.png",argv[1]);if(!png_write_rgba(path,ra,160,160))return 1;
 snprintf(path,sizeof path,"%s/aperture.png",argv[1]);if(!png_write_rgba(path,mask,160,160))return 1;
 printf("LOGOGATE|ok|cnc_entire_rgba_exact=%d|shared_ring_rgba_exact=%d|shared_pixels=%d|visible_shared_pixels=%d|visible_red_C_leaks=%d|cpu_only=1\n",whole_exact,ring_exact,ring_pixels,visible_ring,red_leaks);
 return 0;
}
