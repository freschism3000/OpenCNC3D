/*
 * t1_tib.h -- tiberium, the cartridge's own ANY_TI art, on the ground.
 *
 * One flat quad per cell, drawn on the TERRAIN'S OWN CORNERS so a field follows the hills
 * instead of hovering over them, with the frame taken verbatim from the engine's
 * OverlayData (the 1995 engine uses it as the SHP frame index with no arithmetic, and
 * neither does this).
 *
 * A ground decal must never occlude what stands on it, so it draws with the depth test on
 * and the depth WRITE off, and it needs the co-planar comparison for the same reason the
 * shroud does.
 */

#ifndef T1_TIB_H
#define T1_TIB_H

#include "softras.h"
#include "t1_cam.h"
#include "t1_terrain.h"
#include "w98_brain.h"

typedef struct
{
    unsigned char *blob;
    long           blobsize;
    int            types, frames, fw, fh, nsheet, sheet_sz;
    const unsigned char *pal;        /* 768; index 0 is the hole, and it is magenta */
    const unsigned short *slot;      /* types*frames triples: sheet, x0, y0 */
    SR_Texture     tex[8];
    int            ok;
} T1_Tib;

int  t1_tib_load(T1_Tib *t, const char *path, char *err, int errlen);
void t1_tib_free(T1_Tib *t);

/* Uploads every sheet. Call once, after the card is open. */
int  t1_tib_upload(T1_Tib *t, char *err, int errlen);

long t1_tib_draw(T1_Tib *t, const T1_Terrain *terr, const T1_Cam *cam,
                 const T1_Screen *scr, const W98_Overlays *ov,
                 int (*shown)(int cx, int cz));

/* The same draw over any list of {cell, kind, stage}. The SMUDGES use it: a scorch mark
 * and a tiberium crystal are the same thing to draw, one flat cutout quad on the ground's
 * own corners, so they share a renderer rather than getting a second copy of one. */
long t1_tib_draw_list(T1_Tib *t, const T1_Terrain *terr, const T1_Cam *cam,
                      const T1_Screen *scr, const W98_Tib *cells, int n,
                      int (*shown)(int cx, int cz));

#endif /* T1_TIB_H */
