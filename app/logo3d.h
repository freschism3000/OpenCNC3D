#ifndef CNC3D_LOGO3D_H
#define CNC3D_LOGO3D_H

/* The spinning faction logo that sits on top of the score screen.
   Geometry comes from game/bake_logos.py (playable/logos.pack); that file's
   header comment owns the format and where the models live in the cartridge. */

#ifdef __cplusplus
extern "C" {
#endif

#define LOGO3D_MAXLOGO   8
#define LOGO3D_MAXTEX    8
#define LOGO3D_MAXBATCH  8

/* Slot order is the baker's LOGOS list. GDI and NOD are the BRIEFING logos, each a
   one-sided plate. TITLE is GDI_LOGO_TITLE_ROOT, which carries BOTH emblems back to
   back on one disc -- the version with a backside, and the one to use wherever the
   model turns all the way round. Face it at 0 degrees for GDI, 180 for Nod. */
#define LOGO3D_GDI   0
#define LOGO3D_NOD   1
#define LOGO3D_TITLE 2
/* BRF_EVA_ROOT, the cartridge's own EVA wordmark, added for the DATABASE screen. Its
   Vtx RGB bytes are grey COLOURS rather than packed normals, so bake_logos.py
   synthesises face normals for it and the lighting on this one is OURS -- that file's
   KIND table carries the statement. */
#define LOGO3D_EVA   3

/* The pitch the emblems shipped at before it became a dial. Callers with nothing to
   tune from should pass this rather than a bare number. */
#define LOGO3D_TILT_DEFAULT 20.0f

typedef struct {
    int    texidx;              /* -1 = untextured */
    int    nvert;               /* multiple of 3 */
    float *v;                   /* nvert * 8: x,y,z, nx,ny,nz, u,v */
} Logo3DBatch;

typedef struct {
    char         name[9];
    float        radius;        /* max vertex distance from centre */
    int          ntex;
    unsigned int tex[LOGO3D_MAXTEX];   /* GLuint */
    int          nbatch;
    Logo3DBatch  batch[LOGO3D_MAXBATCH];
} Logo3DModel;

typedef struct {
    int         n;
    Logo3DModel m[LOGO3D_MAXLOGO];
} Logo3D;

/* 1 on success. On failure err carries why and the score screen simply runs
   without a logo -- a missing logos.pack is never fatal. */
int  logo3d_open(Logo3D *L, const char *path, char *err, int errlen);
void logo3d_close(Logo3D *L);

/* Draw logo `which` spun `angle` degrees about the screen vertical, fitted into
   the framebuffer rect (x,y,w,h) given TOP-LEFT origin like the rest of the
   campaign screens. fbh is the drawable height, needed to flip into GL's
   bottom-left viewport origin. Restores the GL state camp_draw expects.

   `fade` is the screen's palette fade, 0..256, the same number camp_draw applies
   to every plate pixel. The model is NOT part of the plate, so without it the
   logo sits at full brightness over a screen that is fading to black.

   THE LAST FOUR ARE THE PLACEMENT DIALS, and they move the MODEL inside the box rather
   than moving the box. That matters: the box is also the scissor, so a box that chased
   the emblem would clip it at the edges instead of letting it slide.
     offx, offy  nudge, in pixels of the box, y DOWN like every other screen coordinate
     sx, sy      scale, 1.0 being the emblem fitted to the box's smaller axis
     tilt        degrees leaned towards the viewer, applied OUTSIDE the spin, so it is
                 a fixed pitch the emblem turns inside rather than a wobble
   Give the box the whole opening and let these place the emblem within it; that way a
   dial can be turned to anything without the picture being cut off. */
void logo3d_draw(Logo3D *L, int which, int fbh, int x, int y, int w, int h,
                 float angle, int fade,
                 float offx, float offy, float sx, float sy, float tilt);

#ifdef __cplusplus
}
#endif
#endif
