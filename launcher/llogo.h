#ifndef CNC_LAUNCHER_MODULE_LOGO_H
#define CNC_LAUNCHER_MODULE_LOGO_H

#define LL_LOGO_W 160
#define LL_LOGO_H 160
enum { LL_LOGO_CNC=0, LL_LOGO_RA=1 };

/* Both modules share the exact approved CNC outer-ring source pixels. CNC is
 * always drawn pixel for pixel, including its full interior and existing alpha.
 * RA replaces only the inset face through the aperture described below, using
 * the existing RA green artwork. Inputs are the existing native 160x160 arrays;
 * no duplicated/repainted metal ring, generated pixels or colour conversion. */
void ll_logo_draw(const unsigned char *cnc_rgba,const unsigned char *ra_rgba,
                  int w,int h,int which,int native_x,int native_y);

/* Pure CPU accessors also expose the shared-pixel contract for art preflight.
 * Aperture 0 means the exact same CNC RGBA must be returned for BOTH modules;
 * 255 is the RA face; intermediate values form a sub-pixel inward seat.
 * Coordinates are native logo pixels, independent of launcher placement. */
unsigned ll_logo_aperture(int x,int y);
void ll_logo_sample(const unsigned char *cnc_rgba,const unsigned char *ra_rgba,
                    int w,int h,int which,int x,int y,unsigned char out[4]);

#endif
