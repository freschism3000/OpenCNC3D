/*
 * t1_dosinf.h -- the 1995 MS-DOS infantry sprites, for tier1.
 *
 * Infantry are NOT meshes. The console billboarded 2D sprites for them and so does this,
 * which is why every infantryman in the mission has been missing from the Win98 screen:
 * their type codes resolve to no mesh, correctly, and nothing else was drawing them.
 *
 * dosinfantry.pack is already exactly what this needs and needs no conversion step at
 * all: 8-bit palette indices in the TEMPERAT palette (the same one the sidebar uses),
 * house remap already applied, and every strip texture power of two and at most 256x256
 * because the baker was written with the Voodoo 2's limit in mind.
 *
 * Index 0 is transparent. Index 4 is the DOS engine's shadow ghost colour, which it
 * darkened the ground with; it is treated as transparent here, exactly as the N64
 * billboards this replaces cast no shadow either.
 */

#ifndef T1_DOSINF_H
#define T1_DOSINF_H

#include "softras.h"
#include "t1_terrain.h"
#include "t1_cam.h"

/* Anim slots, in the pack's own order. Mirrors DosAnim in game/dosinf_mod.h. */
enum {
    T1I_STAND = 0, T1I_WALK, T1I_D_GUN, T1I_D_EXPL, T1I_D_EXPL2, T1I_D_GREN, T1I_D_FIRE,
    T1I_FIRE, T1I_PRONE, T1I_FIRE_PRONE, T1I_LIE_DOWN, T1I_CRAWL, T1I_GET_UP,
    T1I_IDLE1, T1I_IDLE2, T1I_SLOTS
};

typedef struct
{
    char name[25];
    int  frames, facings, stages, fw, fh;
    int  cols, texw, texh, src_stages;
    const unsigned char *idx;
    unsigned char *pad;         /* non-NULL when the strip had to be re-shaped; owned */
    SR_Texture tex;
} T1_InfStrip;

typedef struct
{
    char ini[9];
    int  strip[2][15];          /* [house row][anim slot], -1 for none */
} T1_InfType;

typedef struct
{
    unsigned char *blob;
    long blobsize;
    const unsigned char *pal8;  /* 768, the TEMPERAT palette */
    int nstrip, ntype;
    int reshaped;               /* strips padded to fit the card's 8:1 aspect limit */
    T1_InfStrip *strip;
    T1_InfType  *type;
} T1_Inf;

int  t1_inf_load(T1_Inf *inf, const char *path, char *err, int errlen);
void t1_inf_free(T1_Inf *inf);

/* -1 if this INI code has no DOS art. */
int  t1_inf_type(const T1_Inf *inf, const char *ini);

/* Which strip to draw, and which frame of it.
 *   house  0 = GoodGuy/neutral (identity remap), 1 = BadGuy (ltblue)
 *   dir    the engine's 0..255 facing
 *   doing / dostage come straight off the brain's OBJ line
 * Returns the strip index or -1, and writes the frame. */
int  t1_inf_pick(const T1_Inf *inf, int type, int house, int doing, int dostage,
                 int dir, int moving, int dying, int *frame);

/* The engine's own animation rate for a DoType, in ticks per stage, out of
 * InfantryClass::MasterDoControls. 0 means the stage does not advance. Exposed so the
 * caller can carry a stage forward between object dumps with the engine's arithmetic
 * rather than with a guess. */
int  t1_inf_rate(int doing);

/* ---- the soldier's ground shadow ---------------------------------------------------
 *
 * The console DID draw one, and the comment at the top of this file used to say the
 * opposite. It is 8x8 of intensity at ROM 0x1B4000, drawn mirrored in both axes, so it is
 * one quadrant of a 16x16 radially symmetric blob. Glide has no mirror clamp, so the
 * quadrant is expanded into the whole 16x16 once at load.
 *
 * Kept here rather than in the shadow pass because it is infantry art and because
 * game/soldier_shadow_tex.h is compiled UNEDITED, the way game/dosbar.c is. */
void t1_inf_shadow_init(void);
int  t1_inf_shadow_ready(void);
/* One blob on the ground at (wx, wz) in cells. Call inside the shadow state. */
long t1_inf_shadow_draw(const T1_Terrain *t, const T1_Cam *cam, const T1_Screen *scr,
                        float wx, float wz);

/* The frame's rectangle inside its strip texture, in texels. */
void t1_inf_frame_uv(const T1_InfStrip *s, int frame,
                     float *u0, float *v0, float *u1, float *v1);

#endif /* T1_DOSINF_H */
