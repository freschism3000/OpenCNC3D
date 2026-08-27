/* t1_draw.c -- the backend seam's one variable.
 *
 * It lives in its own file so that t1_tri (an inline in t1_cam.h) has something to call
 * whether or not the Glide backend was compiled into this binary. NULL is the software
 * rasteriser; tier1/t1_glide.c points it at itself when the card opens. */
#include "t1_cam.h"

T1_TriFn t1_tri_hook = 0;
