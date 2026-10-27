/* One shared source ring, with separately seated module faces.
 *
 * Measured approved CNC opening: centre (79.0,74.25), radii (55.0,48.5) native
 * pixels. The aperture remains within its black inner seat, preserving the
 * silver bevel, specular highlights, silhouette and transparent exterior.
 * Its inward AA begins at normalized radius squared .970; every red CNC face
 * pixel is further inside (measured maximum .931), so RA cannot inherit red C
 * fragments. The CNC case does not use the mask and remains wholly exact.
 *
 * RA source green aperture: centre (79.5,76.0), radii (63.5,60.5). Source
 * positions stay inside the green inset/black gasket, excluding its old silver
 * rim. This aperture is fitted into the CNC opening with independent axes:
 * 55/63.5 horizontally and 48.5/60.5 vertically. The mild perspective compression
 * retains the helicopter/rotor, forest, reticle and tank/barrels rather than
 * cropping away their outer face content. No source colours are altered.
 */
#include "llogo.h"
#include "lskin.h"
#include <stddef.h>
#include <string.h>

unsigned ll_logo_aperture(int x,int y)
{
 double dx,dy,q;
 if(x<0||y<0||x>=LL_LOGO_W||y>=LL_LOGO_H)return 0u;
 dx=((double)x-79.0)/55.0;dy=((double)y-74.25)/48.5;
 q=dx*dx+dy*dy;
 if(q>=1.0)return 0u;
 if(q<=0.970)return 255u;
 return (unsigned)((1.0-q)*(255.0/0.030)+0.5);
}

static void ll_logo_face_sample(const unsigned char *rgba,double x,double y,
                               unsigned char out[4])
{
 int ix=(int)x,iy=(int)y,k;
 unsigned fx=(unsigned)((x-(double)ix)*256.0+0.5);
 unsigned fy=(unsigned)((y-(double)iy)*256.0+0.5);
 unsigned w00=(256u-fx)*(256u-fy),w10=fx*(256u-fy);
 unsigned w01=(256u-fx)*fy,w11=fx*fy;
 const unsigned char *p00=rgba+((size_t)iy*LL_LOGO_W+ix)*4u;
 const unsigned char *p10=p00+4;
 const unsigned char *p01=p00+LL_LOGO_W*4;
 const unsigned char *p11=p01+4;
 for(k=0;k<4;k++)
  out[k]=(unsigned char)((p00[k]*w00+p10[k]*w10+p01[k]*w01+p11[k]*w11+32768u)>>16);
}

void ll_logo_sample(const unsigned char *cnc_rgba,const unsigned char *ra_rgba,
                    int w,int h,int which,int x,int y,unsigned char out[4])
{
 const unsigned char *ring;
 unsigned coverage;
 unsigned char face[4];
 int k;
 if(!out)return;
 memset(out,0,4);
 if(!cnc_rgba||w!=LL_LOGO_W||h!=LL_LOGO_H||x<0||y<0||x>=w||y>=h)return;
 ring=cnc_rgba+((size_t)y*w+x)*4u;
 if(which!=LL_LOGO_RA||!ra_rgba){memcpy(out,ring,4);return;}
 coverage=ll_logo_aperture(x,y);
 if(!coverage){memcpy(out,ring,4);return;}
 ll_logo_face_sample(ra_rgba,79.5+((double)x-79.0)*(63.5/55.0),
                     76.0+((double)y-74.25)*(60.5/48.5),face);
 if(coverage==255u){memcpy(out,face,4);return;}
 for(k=0;k<4;k++)
  out[k]=(unsigned char)((face[k]*coverage+ring[k]*(255u-coverage)+127u)/255u);
}

void ll_logo_draw(const unsigned char *cnc_rgba,const unsigned char *ra_rgba,
                  int w,int h,int which,int native_x,int native_y)
{
 int x,y;
 unsigned char pixel[4];
 if(!cnc_rgba||w!=LL_LOGO_W||h!=LL_LOGO_H)return;
 if(which!=LL_LOGO_RA||!ra_rgba){
  ls_rgba_image(cnc_rgba,w,h,native_x,native_y);return;
 }
 for(y=0;y<h;y++)for(x=0;x<w;x++){
  ll_logo_sample(cnc_rgba,ra_rgba,w,h,which,x,y,pixel);
  ls_pixel_rgba(native_x+x,native_y+y,pixel[0],pixel[1],pixel[2],pixel[3]);
 }
}
