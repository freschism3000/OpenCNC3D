/* t1_dosinf.c -- see t1_dosinf.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t1_dosinf.h"
#include "t1_glide.h"

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* ---------------------------------------------------------------------------
 * The engine's own facing tables, copied verbatim from game/dosinf_mod.h, which
 * transcribed them from infantry.cpp:84 and the 32-facing wheel.
 *
 * THEY ARE COPIED AND NOT COMPUTED, ON PURPOSE. The obvious closed form for the
 * 256-to-32 step, ((dir + 4) >> 3) & 31, disagrees with the real table on 79 of
 * the 256 directions. Deriving it would have mis-faced roughly a third of every
 * infantryman, in a way that reads as art being slightly off rather than as a
 * bug. That was checked before this file was written, not after.
 *
 * Duplicating them is a real cost and is, recorded as an open gap: the shared
 * copy lives in a GL header this branch may not touch yet.
 * ------------------------------------------------------------------------- */
static const unsigned char T1I_Facing32[256] = {
     0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,
     2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  6,  6,
     6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,
     8,  8,  8,  9,  9,  9,  9,  9,  9,  9, 10, 10, 10, 10, 10, 10,
    10, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12,
    13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19,
    19, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 22, 22,
    22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24,
    24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26, 26, 26,
    26, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28,
    29, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30,
    30, 31, 31, 31, 31, 31, 31, 31, 31, 31,  0,  0,  0,  0,  0,  0
};
static const unsigned char T1I_HumanShape[32] = {
     0,  0,  7,  7,  7,  7,  6,  6,  6,  6,  5,  5,  5,  5,  5,  4,
     4,  4,  3,  3,  3,  3,  2,  2,  2,  2,  1,  1,  1,  1,  1,  0
};

static int t1i_facenum(int dir) { return T1I_HumanShape[T1I_Facing32[dir & 255]]; }

/* ---- THE ENGINE'S OWN ANIMATION RATE, per DoType ------------------------------------
 *
 * InfantryClass::MasterDoControls, infantry.cpp:95-129, the Rate column. StageClass
 * advances Stage exactly once every Rate ticks (stage.h:88-99) and Graphic_Logic runs
 * once per tick for every non-building (techno.cpp:2043), so a stage between two object
 * dumps is not a guess: it is the engine's own arithmetic, reproduced.
 *
 * THAT IS WHY IT IS HERE. The object dump runs every eighth RENDERED frame, which at 50
 * FPS is about six samples a second against a counter that steps up to fifteen times a
 * second. The rate-1 animations -- firing standing and firing prone -- lose more than half
 * their stages to that aliasing, and every animation loses some. Rate 0 means the stage
 * does not advance at all and must not be extrapolated. */
static const unsigned char T1I_DoRate[34] = {
    0, /* STAND_READY */   0, /* STAND_GUARD */  0, /* PRONE */        2, /* WALK */
    1, /* FIRE_WEAPON */   2, /* LIE_DOWN */     2, /* CRAWL */        3, /* GET_UP */
    1, /* FIRE_PRONE */    2, /* IDLE1 */        2, /* IDLE2 */        2, /* ON_GUARD */
    2, /* FIGHT_READY */   2, /* PUNCH */        2, /* KICK */         2, /* PUNCH_HIT1 */
    2, /* PUNCH_HIT2 */    1, /* PUNCH_DEATH */  2, /* KICK_HIT1 */    2, /* KICK_HIT2 */
    1, /* KICK_DEATH */    2, /* READY_WEAPON */ 2, /* GUN_DEATH */    2, /* EXPL_DEATH */
    2, /* EXPL2_DEATH */   2, /* GRENADE_DEATH */2, /* FIRE_DEATH */   2, /* GESTURE1 */
    2, /* SALUTE1 */       2, /* GESTURE2 */     2, /* SALUTE2 */      2, /* PULL_GUN */
    2, /* PLEAD */         2  /* PLEAD_DEATH */
};

int t1_inf_rate(int doing)
{
    if (doing < 0 || doing >= 34) return 0;
    return T1I_DoRate[doing];
}

/* Engine DoType -> death slot. 17 and 20 are the hand-to-hand deaths, which have no
 * strip in the pack; they borrow the gun death rather than vanishing. */
static int t1i_death_slot(int doing)
{
    switch (doing) {
    case 23: return T1I_D_EXPL;
    case 24: return T1I_D_EXPL2;
    case 25: return T1I_D_GREN;
    case 26: return T1I_D_FIRE;
    default: return T1I_D_GUN;
    }
}

/* Live (non-death) DoType -> slot, defines.h:1523. Anything else is -1 and the caller
 * falls back to its own stand/walk choice from whether the man is moving. */
static int t1i_live_slot(int doing)
{
    switch (doing) {
    case 2:  return T1I_PRONE;
    /* DO_WALK. THE ENGINE'S OWN STATEMENT THAT THIS MAN IS WALKING, and it was missing.
     * Without it the caller fell back to guessing from the MISSION NAME, and only one of
     * the engine's twenty-two mission names is "Move": a rifleman crossing the map under
     * Attack, Hunt, Area Guard, Capture or Retreat was drawn standing still while he
     * slid along the ground. infantry.cpp sets Doing from IsDriving at :1242 and from
     * Start_Driver at :1416, and never consults the mission to do it. */
    case 3:  return T1I_WALK;
    case 4:  return T1I_FIRE;
    case 5:  return T1I_LIE_DOWN;
    case 6:  return T1I_CRAWL;
    case 7:  return T1I_GET_UP;
    case 8:  return T1I_FIRE_PRONE;
    case 9:  return T1I_IDLE1;
    case 10: return T1I_IDLE2;
    default: return -1;
    }
}

int t1_inf_load(T1_Inf *inf, const char *path, char *err, int errlen)
{
    FILE *f;
    long n;
    unsigned char *p;
    int i, j, k;

    memset(inf, 0, sizeof *inf);
    f = fopen(path, "rb");
    if (!f) { _snprintf(err, errlen, "cannot open %s", path); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    inf->blob = (unsigned char *)malloc((size_t)n);
    if (!inf->blob) { fclose(f); _snprintf(err, errlen, "out of memory (%ld)", n); return 0; }
    if (fread(inf->blob, 1, (size_t)n, f) != (size_t)n)
    { fclose(f); _snprintf(err, errlen, "short read on %s", path); return 0; }
    fclose(f);
    inf->blobsize = n;

    if (n < 1600 || memcmp(inf->blob, "DOSINF01", 8) != 0)
    { _snprintf(err, errlen, "%s is not a DOSINF01 pack", path); return 0; }

    p = inf->blob + 8;
    p += 4;                                   /* version: 3 and 4 both parse here */
    inf->nstrip = (int)rd32(p); p += 4;
    inf->ntype  = (int)rd32(p); p += 4;
    p += 768;                                 /* pal6, the 6-bit DAC original */
    inf->pal8 = p; p += 768;

    inf->strip = (T1_InfStrip *)calloc((size_t)inf->nstrip, sizeof(T1_InfStrip));
    inf->type  = (T1_InfType  *)calloc((size_t)inf->ntype,  sizeof(T1_InfType));
    if (!inf->strip || !inf->type)
    { _snprintf(err, errlen, "out of memory for %d strips", inf->nstrip); return 0; }

    for (i = 0; i < inf->nstrip; ++i)
    {
        T1_InfStrip *s = &inf->strip[i];
        memcpy(s->name, p, 24); s->name[24] = 0; p += 24;
        s->frames  = (int)rd32(p); p += 4;
        s->facings = (int)rd32(p); p += 4;
        s->stages  = (int)rd32(p); p += 4;
        s->fw      = (int)rd32(p); p += 4;
        s->fh      = (int)rd32(p); p += 4;
        s->cols    = (int)rd32(p); p += 4;
        s->texw    = (int)rd32(p); p += 4;
        s->texh    = (int)rd32(p); p += 4;
        s->src_stages = (int)rd32(p); p += 4;
        s->idx = p;
        p += (long)s->texw * s->texh;
        if (p > inf->blob + n)
        { _snprintf(err, errlen, "%s truncated at strip %d", path, i); return 0; }

        /* THE VOODOO HAS TWO TEXTURE LIMITS AND THE BAKER ONLY KNEW ABOUT ONE.
         *
         * bake_dosinfantry.py says its strips are "power-of-two and <= 256 (Voodoo 2
         * limit)", and they are. But the card also caps the ASPECT RATIO at 8:1, and
         * strips like 256x16 are 16:1, so Glide refuses them outright. On the first run
         * that was 4,744 refusals a session and almost every soldier missing.
         *
         * Fixed here rather than in the pack, because the pack is shared with the
         * desktop build: the short side is padded up with transparent index 0 until the
         * ratio is legal. The content and therefore every texel coordinate is unchanged,
         * so nothing above this needs to know. It costs 8 KB for a 256x16. */
        {
            int big = s->texw > s->texh ? s->texw : s->texh;
            int small = s->texw > s->texh ? s->texh : s->texw;
            if (small > 0 && big / small > 8)
            {
                int want = big / 8, y;
                if (s->texw > s->texh)
                {
                    s->pad = (unsigned char *)calloc((size_t)s->texw * want, 1);
                    if (s->pad)
                    {
                        for (y = 0; y < s->texh; ++y)
                            memcpy(s->pad + (long)y * s->texw,
                                   s->idx + (long)y * s->texw, (size_t)s->texw);
                        s->idx = s->pad;
                        s->texh = want;
                    }
                }
                else
                {
                    s->pad = (unsigned char *)calloc((size_t)want * s->texh, 1);
                    if (s->pad)
                    {
                        for (y = 0; y < s->texh; ++y)
                            memcpy(s->pad + (long)y * want,
                                   s->idx + (long)y * s->texw, (size_t)s->texw);
                        s->idx = s->pad;
                        s->texw = want;
                    }
                }
                ++inf->reshaped;
            }
        }
        sr_texture(&s->tex, s->idx, s->texw, s->texh);
        sr_texture_ckey(&s->tex, 1);
    }
    for (i = 0; i < inf->ntype; ++i)
    {
        memcpy(inf->type[i].ini, p, 8); inf->type[i].ini[8] = 0; p += 8;
        for (j = 0; j < 2; ++j)
            for (k = 0; k < 15; ++k)
            { inf->type[i].strip[j][k] = (int)rd32(p); p += 4; }
    }
    if (p > inf->blob + n)
    { _snprintf(err, errlen, "%s truncated in the type table", path); return 0; }
    return 1;
}

void t1_inf_free(T1_Inf *inf)
{
    if (inf->strip)
    {
        int i;
        for (i = 0; i < inf->nstrip; ++i)
            if (inf->strip[i].pad) free(inf->strip[i].pad);
        free(inf->strip);
    }
    if (inf->type)  free(inf->type);
    if (inf->blob)  free(inf->blob);
    memset(inf, 0, sizeof *inf);
}

int t1_inf_type(const T1_Inf *inf, const char *ini)
{
    int i;
    for (i = 0; i < inf->ntype; ++i)
        if (strncmp(inf->type[i].ini, ini, 8) == 0) return i;
    return -1;
}

int t1_inf_pick(const T1_Inf *inf, int type, int house, int doing, int dostage,
                int dir, int moving, int dying, int *frame)
{
    int slot, si, face, stage, isdeath;
    const T1_InfStrip *s;

    if (type < 0 || type >= inf->ntype) return -1;
    if (house < 0 || house > 1) house = 0;

    /* THE BRAIN ALREADY KNOWS WHICH DOTYPES ARE DEATHS and says so in one flag. The test
     * this replaces was the closed range 17..26, which swallows three DoTypes that are
     * not deaths at all -- KICK_HIT1, KICK_HIT2 and READY_WEAPON -- and drew a man in any
     * of them as a corpse. The engine's own set is PUNCH_DEATH, KICK_DEATH and
     * GUN_DEATH..FIRE_DEATH, which is exactly what `dying` carries. */
    isdeath = dying;
    if (isdeath) slot = t1i_death_slot(doing);
    else
    {
        slot = t1i_live_slot(doing);
        /* The caller's own moving/standing guess is the LAST resort, not the first: it is
         * consulted only where the engine has said nothing (STAND_READY, STAND_GUARD, or
         * no DoType at all). */
        if (slot < 0) slot = moving ? T1I_WALK : T1I_STAND;
    }
    si = inf->type[type].strip[house][slot];
    if (si < 0 || si >= inf->nstrip) si = inf->type[type].strip[house][T1I_STAND];
    if (si < 0 || si >= inf->nstrip) return -1;
    s = &inf->strip[si];

    /* A strip is FACING MAJOR: `stages` frames per facing, `facings` of them. Where the
     * baker had to subsample a strip to fit the Voodoo's texture limit, src_stages is
     * the engine's original count and the stage is mapped through it. */
    face = (s->facings > 1) ? t1i_facenum(dir) : 0;
    if (face >= s->facings) face = s->facings - 1;
    if (face < 0) face = 0;
    stage = 0;
    if (s->stages > 0)
    {
        int src = s->src_stages > 0 ? s->src_stages : s->stages;
        /* A DEATH HOLDS ITS LAST FRAME; everything else loops. Wrapping a death restarts
         * it, so a corpse got up and died again for as long as the engine kept the object
         * in its heap. */
        int st = isdeath ? (dostage < src ? dostage : src - 1) : (dostage % src);
        if (st < 0) st = 0;
        stage = (st * s->stages) / src;
        if (stage >= s->stages) stage = s->stages - 1;
        if (stage < 0) stage = 0;
    }
    *frame = face * s->stages + stage;
    if (*frame >= s->frames) *frame = s->frames - 1;
    if (*frame < 0) *frame = 0;
    return si;
}

void t1_inf_frame_uv(const T1_InfStrip *s, int frame,
                     float *u0, float *v0, float *u1, float *v1)
{
    int cols = s->cols > 0 ? s->cols : 1;
    int cx = frame % cols, cy = frame / cols;
    *u0 = (float)(cx * s->fw);
    *v0 = (float)(cy * s->fh);
    *u1 = *u0 + (float)s->fw;
    *v1 = *v0 + (float)s->fh;
}

/* ---- the soldier's ground shadow ----------------------------------------------------
 *
 * THE CONSOLE DREW ONE. The note at the top of this file said it did not, and used that
 * to justify treating the DOS engine's shadow-ghost palette index as transparent. Half of
 * that is right -- index 4 is the 1995 engine's own trick and must not be drawn -- but the
 * conclusion was wrong: the cartridge draws a real blob of its own, and this is it.
 *
 * 8x8 of intensity at ROM 0x1B4000, drawn by the RDP mirrored in S and T, so what is
 * stored is one QUADRANT of a 16x16 radially symmetric blob. Glide has no mirror clamp,
 * so the mirroring happens once here instead of every frame on the card.
 *
 * Drawn as WORLD geometry through t1_tri and not as a screen quad, deliberately: a screen
 * quad at a projected position is exactly the unbounded case that wedged the card, and
 * this one would be drawn for every man on the map. */
#include "soldier_shadow_tex.h"

static unsigned char g_infsh_px[16 * 16];
static SR_Texture    g_infsh;
static int           g_infsh_ok;

void t1_inf_shadow_init(void)
{
    int x, y;
    char err[128];
    if (g_infsh_ok) return;
    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x)
        {
            /* Mirror about the centre: the stored quadrant is the bottom-right one, so
             * the sample runs 7..0 on the low half and 0..7 on the high half. */
            int sx = x < 8 ? 7 - x : x - 8;
            int sy = y < 8 ? 7 - y : y - 8;
            g_infsh_px[y * 16 + x] = SOLDIER_SHADOW_I8[sy * 8 + sx];
        }
    sr_texture_alpha(&g_infsh, g_infsh_px, 16, 16);
    g_infsh_ok = t1_glide_upload(&g_infsh, err, sizeof err);
}

int t1_inf_shadow_ready(void) { return g_infsh_ok; }

long t1_inf_shadow_draw(const T1_Terrain *t, const T1_Cam *cam, const T1_Screen *scr,
                        float wx, float wz)
{
    /* 40 leptons of half-extent, the console's own, and the same +5/-5 lepton nudge every
     * other shadow in the game is thrown by. */
    const float R = 40.0f / 256.0f;
    const float OX = 5.0f / 256.0f, OZ = -5.0f / 256.0f;
    SR_Vertex q[4];
    float cx = wx + OX, cz = wz + OZ;
    int k;
    long drawn = 0;

    if (!g_infsh_ok) return 0;
    t1_world_to_eye(cam, cx - R, t1_terrain_corner_y(t, (int)(cx - R), (int)(cz - R)) + 0.012f, cz - R, &q[0]);
    t1_world_to_eye(cam, cx + R, t1_terrain_corner_y(t, (int)(cx + R), (int)(cz - R)) + 0.012f, cz - R, &q[1]);
    t1_world_to_eye(cam, cx + R, t1_terrain_corner_y(t, (int)(cx + R), (int)(cz + R)) + 0.012f, cz + R, &q[2]);
    t1_world_to_eye(cam, cx - R, t1_terrain_corner_y(t, (int)(cx - R), (int)(cz + R)) + 0.012f, cz + R, &q[3]);
    q[0].u = 0.0f;  q[0].v = 0.0f;
    q[1].u = 16.0f; q[1].v = 0.0f;
    q[2].u = 16.0f; q[2].v = 16.0f;
    q[3].u = 0.0f;  q[3].v = 16.0f;
    for (k = 0; k < 4; ++k)
    {
        /* Black, and the vertex ALPHA is the second combiner's PRIM_A: the ROM scales the
         * soldier's coverage where it leaves the vehicle plates at full. */
        q[k].light = 0.0f; q[k].r = 0.0f; q[k].g = 0.0f; q[k].b = 0.0f;
    }
    {
        float a3[3];
        a3[0] = a3[1] = a3[2] = 80.0f / 255.0f;
        t1_glide_alpha3(a3);
        drawn += t1_tri(0, &q[0], &q[1], &q[2], &g_infsh, scr);
        drawn += t1_tri(0, &q[0], &q[2], &q[3], &g_infsh, scr);
        t1_glide_alpha3(0);
    }
    return drawn;
}
