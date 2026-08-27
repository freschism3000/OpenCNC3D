#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "t1_efx.h"
#include "t1_glide.h"
/* game/efx_recipes.h is GENERATED for C++ and names its structs without the `struct`
 * keyword at their use sites. Forward typedefs give C the same spelling, so the header is
 * compiled UNEDITED here exactly as dosbar.c and hud640.c are; the branch's rule is that
 * it reuses the project's files rather than forking them. */
typedef struct EfxSourceRec EfxSourceRec;
typedef struct EfxSlot      EfxSlot;
typedef struct EfxRecipe    EfxRecipe;
#include "efx_recipes.h"

/* The ten particle systems and the art sequence each one draws with. Transcribed from
 * EFX_SYS in game/effects_mod.h, which is where the cartridge's own system registry
 * (RAM 0x800EC1B0, init at 0x80056944) was decoded into. Copied rather than included
 * because that header is GL from top to bottom and this branch does not edit it. */
static const char *const T1EFX_SYS[10] = {
    "GLOW",      /* 0 Intensity        Sam_*, Gun_*, Sparks, Dirt, LZ smoke */
    "FIREBALL",  /* 1 FireBall         Explode_S/M/L, LandingSmoke          */
    "DEBRIS",    /* 2 VehicleDamage                                          */
    "DEBRIS",    /* 3 StructureDamage                                        */
    "SHOCK",     /* 4 Shock                                                  */
    "FLAME",     /* 5 Burn_Fire        all fire / burn / napalm              */
    "SMOKE",     /* 6 Burn_Smoke                                             */
    "IONSPARK",  /* 7 Ion_Spark        Ion_Spark and Lightning_1..7          */
    "DEBRIS",    /* 8 Debris           chunk body and ember trail            */
    "FLTHROW"    /* 9 FlameThrower     Flame_*                               */
};

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int t1_efx_load(T1_Efx *e, const char *path, char *err, int errlen)
{
    FILE *f;
    long n;
    unsigned char *p;
    int i, k;
    long need = 0;

    memset(e, 0, sizeof *e);
    f = fopen(path, "rb");
    if (!f) { _snprintf(err, errlen, "cannot open %s", path); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    e->blob = (unsigned char *)malloc((size_t)n);
    if (!e->blob) { fclose(f); _snprintf(err, errlen, "out of memory"); return 0; }
    if (fread(e->blob, 1, (size_t)n, f) != (size_t)n)
    { fclose(f); _snprintf(err, errlen, "short read"); return 0; }
    fclose(f);
    e->blobsize = n;

    if (n < 32 || memcmp(e->blob, "T1EFX001", 8) != 0)
    { _snprintf(err, errlen, "%s is not a T1EFX001 file (tools/win98/mkefx.py)", path); return 0; }
    p = e->blob + 8;
    if (rd32(p) != 1) { _snprintf(err, errlen, "unknown t1efx version"); return 0; }
    p += 4;
    e->nseq = (int)rd32(p); p += 4;
    if (e->nseq < 1 || e->nseq > T1EFX_MAXSEQ)
    { _snprintf(err, errlen, "%d sequences", e->nseq); return 0; }

    for (i = 0; i < e->nseq; ++i)
    {
        T1_EfxSeq *s = &e->seq[i];
        memcpy(s->name, p, 8); s->name[8] = 0; p += 8;
        s->frames = (int)rd32(p); p += 4;
        s->w      = (int)rd32(p); p += 4;
        s->h      = (int)rd32(p); p += 4;
        s->uw     = (int)rd32(p); p += 4;
        s->uh     = (int)rd32(p); p += 4;
        if (s->frames < 1 || s->frames > T1EFX_MAXFRAME)
        { _snprintf(err, errlen, "%s has %d frames", s->name, s->frames); return 0; }
        need += (long)s->frames * s->w * s->h * 2;
    }
    if (p + need > e->blob + n) { _snprintf(err, errlen, "%s is truncated", path); return 0; }
    for (i = 0; i < e->nseq; ++i)
    {
        T1_EfxSeq *s = &e->seq[i];
        for (k = 0; k < s->frames; ++k)
        {
            sr_texture16(&s->tex[k], (const unsigned short *)p, s->w, s->h);
            p += (long)s->w * s->h * 2;
        }
    }
    e->ok = 1;
    return 1;
}

void t1_efx_free(T1_Efx *e)
{
    if (e->blob) free(e->blob);
    memset(e, 0, sizeof *e);
}

int t1_efx_upload(T1_Efx *e, char *err, int errlen)
{
    int i, k;
    if (!e->ok) return 0;
    for (i = 0; i < e->nseq; ++i)
        for (k = 0; k < e->seq[i].frames; ++k)
            if (!t1_glide_upload(&e->seq[i].tex[k], err, errlen)) return 0;
    return 1;
}

static const T1_EfxSeq *seq_named(const T1_Efx *e, const char *name)
{
    int i;
    for (i = 0; i < e->nseq; ++i)
        if (strcmp(e->seq[i].name, name) == 0) return &e->seq[i];
    return 0;
}

/* The cartridge's own chain: AnimType -> recipe -> the recipe's first source -> that
 * source's particle system -> the system's art sequence. Every step is the ROM's table,
 * not a guess about which explosion looks like which anim. */
static const T1_EfxSeq *seq_for_anim(const T1_Efx *e, int animtype, float *size_units)
{
    int ri, src;
    if (animtype < 0 || animtype >= EFX_ANIM2RECIPE_N) return 0;
    ri = EFX_ANIM2RECIPE[animtype];
    if (ri < 0 || ri >= 66) return 0;
    if (!EFX_RECIPE[ri].nslot) return 0;      /* GUNFIRE is empty BY DESIGN: the
                                               * cartridge shows no muzzle flash */
    src = EFX_RECIPE[ri].slot[0].src;
    if (src < 0 || src >= 65) return 0;
    *size_units = EFX_SRC[src].size;
    if (EFX_SRC[src].sys >= 10) return 0;
    return seq_named(e, T1EFX_SYS[EFX_SRC[src].sys]);
}

/* ====================================================================================
 *  THE SIMULATION
 *
 *  Transcribed from the desktop build's game/effects_mod.h, which is itself a
 *  line-by-line transcription of the disassembly. The ROM addresses in the comments are
 *  the evidence for each law; where a number is OURS rather than the cartridge's it says
 *  so at the number.
 * ==================================================================================== */

/* One cell is 256 of the cartridge's world units. */
#define EFX_U       (1.0f / 256.0f)
/* The explosion record at RAM 0x801C95F0 = {2.0, 1.0, -2.0, 20.0, 5.0}: y accel -2.0
 * units per tick squared, and elasticity 0.7 out of the chunk visual's +0x28. The shrink
 * is the visual record's +0x2C, integrated at RAM 0x8004EF90. */
#define EFX_CHUNK_YACC   (-2.0f)
#define EFX_CHUNK_BOUNCE  0.70f
#define EFX_CHUNK_SHRINK (-0.005f)

/* ---- the stateless RNG --------------------------------------------------------------
 * A pure hash, so a particle's randomness is a function of WHICH particle it is and never
 * of when it was asked for. The console calls rand(); we cannot, because the regression
 * on this branch is a set of screenshots and rand() would make two runs of the same
 * script differ. THIS SUBSTITUTION IS OURS and is the only one in the file. */
static unsigned efx_hash(unsigned a, unsigned b, unsigned c, unsigned d)
{
    unsigned h = 0x9E3779B9u;
    h ^= a; h *= 0x85EBCA6Bu; h ^= h >> 13;
    h ^= b; h *= 0xC2B2AE35u; h ^= h >> 16;
    h ^= c; h *= 0x27D4EB2Fu; h ^= h >> 15;
    h ^= d; h *= 0x165667B1u; h ^= h >> 16;
    return h;
}

/* The console draws rand8/256 (RAM 0x8004F0F8): EIGHT bits, not twenty-four. Matching it
 * means the quantisation of every speed, life, size and angle is the cartridge's own
 * rather than a finer one of our invention. */
static float efx_r8(unsigned h) { return (float)(h & 0xFFu) / 256.0f; }

/* The ground under a point, continuously rather than per corner: a particle lands where
 * the drawn terrain is, not where its cell's corner happens to be. Planar on the same
 * SW-NE diagonal the terrain itself is split along. */
static float efx_ground(const T1_Terrain *t, float wx, float wz)
{
    int cx = (int)wx, cz = (int)wz;
    float fx, fz, a, b, c, d;
    if (cx < 0) cx = 0; if (cx > 63) cx = 63;
    if (cz < 0) cz = 0; if (cz > 63) cz = 63;
    fx = wx - (float)cx; fz = wz - (float)cz;
    if (fx < 0.0f) fx = 0.0f; if (fx > 1.0f) fx = 1.0f;
    if (fz < 0.0f) fz = 0.0f; if (fz > 1.0f) fz = 1.0f;
    a = t1_terrain_corner_y(t, cx,     cz);
    b = t1_terrain_corner_y(t, cx + 1, cz);
    c = t1_terrain_corner_y(t, cx,     cz + 1);
    d = t1_terrain_corner_y(t, cx + 1, cz + 1);
    /* The cell's own two triangles, so the sampler folds exactly as the ground folds. */
    if (fx + fz <= 1.0f) return a + (b - a) * fx + (c - a) * fz;
    return d + (c - d) * (1.0f - fx) + (b - d) * (1.0f - fz);
}

void t1_efx_reset(T1_Efx *e)
{
    e->npart = e->nchunk = 0;
    e->nlive = 0;
    e->lasttick = -1;
}

/* Append a sprite. At the cap the OLDEST go, in one block, so the eviction memmove is
 * amortised instead of running on every push. Chunks are never evicted for a sprite:
 * the chunks are the silhouette. */
static void efx_push(T1_Efx *e, const T1_EfxPart *p)
{
    if (e->npart >= T1EFX_MAX_PART)
    {
        memmove(&e->part[0], &e->part[T1EFX_EVICT],
                sizeof(T1_EfxPart) * (T1EFX_MAX_PART - T1EFX_EVICT));
        e->npart = T1EFX_MAX_PART - T1EFX_EVICT;
        e->evicted += T1EFX_EVICT;
    }
    e->part[e->npart++] = *p;
    ++e->spawned;
    if (e->npart > e->peak_part) e->peak_part = e->npart;
}

/* ---- the spawn ----------------------------------------------------------------------
 * The wrapper at RAM 0x801FC7F0 plus the action integrator at RAM 0x8004EFE4:
 *     a   = uniform(aMin, aMax);   b = uniform(bMin, bMax)
 *     sp  = speed_base + rand8/256 * speed_rand
 *     vel = sp * ( sin b, cos b * cos a, sin a )
 *     pos = emitter + 3.0 * vel                    (the vec scale at RAM 0x8004F1B4)
 * The ROM's world axes are ours -- x east, y up, z south -- so it maps across with no
 * sign flips. */
static void efx_emit(T1_Efx *e, const struct EfxSourceRec *s,
                     float ex, float ey, float ez,
                     int n, unsigned key, long bornframe, int slot, int k)
{
    int i;
    for (i = 0; i < n; ++i)
    {
        unsigned h1 = efx_hash(key, (unsigned)bornframe,
                               ((unsigned)slot << 8) | (unsigned)k, (unsigned)i);
        unsigned h2 = efx_hash(h1, 0x5BF03635u, (unsigned)i, key);
        float A  = s->aMin + (s->aMax - s->aMin) * efx_r8(h1);
        float B  = s->bMin + (s->bMax - s->bMin) * efx_r8(h1 >> 8);
        float sp = (s->spd + efx_r8(h1 >> 16) * s->spdR) * EFX_U;
        T1_EfxPart p;

        memset(&p, 0, sizeof p);
        p.vx = sp * (float)sin(B);
        p.vy = sp * (float)cos(B) * (float)cos(A);
        p.vz = sp * (float)sin(A);
        p.x  = ex + 3.0f * p.vx;
        p.y  = ey + 3.0f * p.vy;
        p.z  = ez + 3.0f * p.vz;
        p.ay = s->yacc * EFX_U;
        /* size and sizeR are the billboard's HALF-EXTENT, not its diameter: the cartridge
         * builds the quad from unit camera axes and offsets each corner by size, so the
         * drawn side is 2*size. Halving it here is what drew every sprite at half the
         * cartridge's linear size and a quarter of its area, which is exactly the
         * "particles are too small" the desktop build was told about. */
        p.hw   = (s->size + efx_r8(h2) * s->sizeR) * EFX_U;
        p.grow = s->grow * EFX_U;
        if (p.hw < 0.0f) p.hw = 0.0f;
        p.life = (short)(s->life + (int)(efx_r8(h1 >> 24) * (float)s->lifeR));
        if (p.life < 1) p.life = 1;
        p.sys   = s->sys;
        p.arate = s->arate;
        p.r = s->r; p.g = s->g; p.b = s->b; p.a = s->a;
        p.seed = h2;
        efx_push(e, &p);
    }
}

/* SOURCE_Debris / SOURCE_Debris2: the flying wreckage.
 *
 * THE LAUNCH DIRECTION IS THE ONE INVENTED NUMBER IN THIS FILE. It lives inside the
 * explosion updater, which nobody has disassembled. It is not free, though: frames of the
 * real console put 14 to 17 pixels between consecutive embers at 166 px per cell, i.e.
 * the FULL 20-25 unit speed, so the launch must be near-horizontal with a free azimuth.
 * Feeding the source's own cone into the ordinary velocity formula would cap the
 * horizontal component at half that. If it ever looks wrong, the fix is these three
 * lines, not a redesign. */
static void efx_emit_chunks(T1_Efx *e, const struct EfxSourceRec *s,
                            float ex, float ey, float ez,
                            int n, unsigned key, long bornframe, int slot, int k,
                            int house)
{
    int i;
    for (i = 0; i < n; ++i)
    {
        unsigned h1, h2;
        float sp, theta, phi;
        T1_EfxPart c;
        if (e->nchunk >= T1EFX_MAX_CHUNK) { ++e->chunk_dropped; continue; }
        h1 = efx_hash(key, (unsigned)bornframe,
                      ((unsigned)slot << 8) | (unsigned)k, (unsigned)i);
        h2 = efx_hash(h1, 0x9E3779B9u, (unsigned)i, key);
        sp    = (s->spd + efx_r8(h1 >> 16) * s->spdR) * EFX_U;
        theta = 6.2831853f * efx_r8(h1);
        phi   = s->aMax * efx_r8(h1 >> 8);

        memset(&c, 0, sizeof c);
        c.vx = sp * (float)cos(phi) * (float)sin(theta);
        c.vy = sp * (float)sin(phi);
        c.vz = sp * (float)cos(phi) * (float)cos(theta);
        c.x  = ex + 3.0f * c.vx;
        c.y  = ey + 3.0f * c.vy;
        c.z  = ez + 3.0f * c.vz;
        c.ay = EFX_CHUNK_YACC * EFX_U;
        /* The source record's size (0.25) is the chunk's MESH SCALE, and at birth 0.25 is
         * exactly this renderer's model scale, so a chunk is drawn at its authored size
         * and dwindles to nothing by tick 50 against a 40-50 tick life. */
        c.scale = s->size;
        c.hw    = 0.18f * s->size;
        /* Which of the seven display lists. The console draws sys[+0x14][rand() % 7]
         * at RAM 0x8004E8AC; the hash stands in for rand() as everywhere else here. */
        c.mesh   = (unsigned char)(efx_hash(h2, 0x1F123BB5u, (unsigned)i, key) % 7u);
        c.family = (unsigned char)(s->sys == 3 ? 1 : 0);   /* 3 = StructureDamage */
        c.house  = (unsigned char)(house == 1 ? 1 : 0);
        c.life = (short)(s->life + (int)(efx_r8(h1 >> 24) * (float)s->lifeR));
        if (c.life < 1) c.life = 1;
        c.sys   = s->sys;
        c.arate = s->arate;
        c.r = s->r; c.g = s->g; c.b = s->b; c.a = s->a;
        c.seed = h2;
        e->chunk[e->nchunk++] = c;
        ++e->chunks_spawned;
        if (e->nchunk > e->peak_chunk) e->peak_chunk = e->nchunk;
    }
}

/* Fire one recipe slot for one anim at one stage. */
static void efx_fire_slot(T1_Efx *e, const T1_Terrain *terr, const W98_Anim *an,
                          const struct EfxSlot *sl, int slotidx, unsigned key,
                          long bornframe)
{
    const struct EfxSourceRec *s;
    int k, n, j;
    float count, ex, ey, ez;

    if (!sl->src) return;                     /* an empty slot: the console skips it */
    s = &EFX_SRC[sl->src];
    k = an->stage - (int)sl->stage;           /* ticks since this slot armed */
    if (k < 0) return;

    if (s->burst)
    {
        /* A burst action lives burst ticks; each tick it emits trunc(count) and then
         * count *= the per-tick multiplier (RAM 0x8004F3F8 / 0x8004F404). */
        if (k >= (int)s->burst) return;
        count = s->ppt;
        for (j = 0; j < k; ++j) count *= s->mult;
    }
    else count = s->ppt;                      /* continuous: re-armed every stage */

    n = (int)count;                           /* trunc, RAM 0x8004F068 */
    if (n <= 0) return;

    /* dy is relative to the TERRAIN HEIGHT under (x,z), not to the anim's own y: the
     * wrapper calls the ground-height function at RAM 0x801F7DFC with (x,z) and adds dy
     * to its result (RAM 0x801FC8F0-0x801FC92C). */
    ex = an->lx / 256.0f + (float)sl->dx * EFX_U;
    ez = an->ly / 256.0f + (float)sl->dz * EFX_U;
    ey = efx_ground(terr, ex, ez) + (float)sl->dy * EFX_U;

    if (s->sys == 2 || s->sys == 3)
        efx_emit_chunks(e, s, ex, ey, ez, n, key, bornframe, slotidx, k, 0);
    else
        efx_emit(e, s, ex, ey, ez, n, key, bornframe, slotidx, k);
}

/* Integrate one tick of the whole pool. */
static void efx_integrate(T1_Efx *e, const T1_Terrain *terr)
{
    int i, w = 0;

    /* Chunks first: they bounce, and they leave an ember behind them every tick. */
    for (i = 0; i < e->nchunk; ++i)
    {
        T1_EfxPart *c = &e->chunk[i];
        float gy;
        c->x += c->vx; c->y += c->vy; c->z += c->vz;
        c->vy += c->ay;
        gy = efx_ground(terr, c->x, c->z);
        if (c->y <= gy)
        {
            c->y = gy;
            c->vy = -c->vy * EFX_CHUNK_BOUNCE;
            c->vx *= EFX_CHUNK_BOUNCE;
            c->vz *= EFX_CHUNK_BOUNCE;
            if (c->vy * c->vy + c->vx * c->vx + c->vz * c->vz < 4.0e-6f)
            { c->vx = c->vy = c->vz = 0.0f; c->ay = 0.0f; }   /* parked */
        }
        ++c->age;
        if (c->scale != 0.0f)
        {
            c->scale += EFX_CHUNK_SHRINK;
            if (c->scale < 0.0f) c->scale = 0.0f;
        }
        /* THE TRAIL. One ember per tick at the chunk's own position, with no velocity of
         * its own, so the chain marks out the arc the chunk flew and stays behind after
         * it has gone. Embers are half a chunk. */
        if (c->age < c->life)
        {
            unsigned h = efx_hash(c->seed, (unsigned)c->age, 0u, 0u);
            T1_EfxPart em;
            memset(&em, 0, sizeof em);
            em.x = c->x; em.y = c->y; em.z = c->z;
            em.hw = c->hw * 0.5f;
            em.life = (short)(20 + (int)(efx_r8(h) * 20.0f));
            em.sys = 8;                       /* the ember art */
            em.arate = c->arate;
            em.r = c->r; em.g = c->g; em.b = c->b; em.a = c->a;
            em.seed = h;
            efx_push(e, &em);
            e->chunk[w++] = *c;
        }
    }
    e->nchunk = w;

    /* Sprites: pos += vel; vel += accel; size += growth; age++. Compacted in place, which
     * keeps index order equal to age order and makes eviction of the oldest a memmove. */
    w = 0;
    for (i = 0; i < e->npart; ++i)
    {
        T1_EfxPart *p = &e->part[i];
        p->x += p->vx; p->y += p->vy; p->z += p->vz;
        p->vy += p->ay;
        p->hw += p->grow;
        if (p->hw < 0.0f) p->hw = 0.0f;
        ++p->age;
        if (p->age < p->life) e->part[w++] = *p;
    }
    e->npart = w;
}

/* Was this (anim type, heap id) live on the previous step? Open addressed because C89
 * has no map and because 64 slots is more anims than a mission ever has alive. */
static int live_seen(T1_Efx *e, unsigned key)
{
    int i;
    for (i = 0; i < e->nlive; ++i) if (e->live[i] == key) return 1;
    return 0;
}

void t1_efx_step(T1_Efx *e, const T1_Terrain *terr, const W98_Overlays *ov, long tick)
{
    unsigned nextlive[64];
    int nnext = 0, i, k, steps;

    if (!e->ok) return;
    if (tick < e->lasttick) t1_efx_reset(e);      /* the clock went backwards */
    if (tick == e->lasttick) return;              /* one step per tick, never per frame */
    steps = (int)(tick - e->lasttick);
    if (e->lasttick < 0) steps = 1;
    if (steps > 4) steps = 4;                     /* a long stall does not become a burst */
    e->lasttick = tick;

    /* Fire every live anim's recipe slots for the stage the engine says it is on. */
    for (i = 0; i < ov->nanim; ++i)
    {
        const W98_Anim *a = &ov->anim[i];
        unsigned key = ((unsigned)a->type << 16) | ((unsigned)(a->stage) & 0xFFFFu);
        int ri;
        if (a->type < 0 || a->type >= EFX_ANIM2RECIPE_N) continue;
        ri = EFX_ANIM2RECIPE[a->type];
        if (ri < 0 || ri >= 66) continue;
        if (nnext < 64) nextlive[nnext++] = key;
        /* Once per (anim, stage): the engine reports the same anim on every dump and
         * firing on each of them would multiply every explosion by the frame rate. */
        if (live_seen(e, key)) continue;
        for (k = 0; k < (int)EFX_RECIPE[ri].nslot; ++k)
            efx_fire_slot(e, terr, a, &EFX_RECIPE[ri].slot[k], k, key, tick);
    }
    for (i = 0; i < nnext; ++i) e->live[i] = nextlive[i];
    e->nlive = nnext;

    while (steps-- > 0) efx_integrate(e, terr);
}

long t1_efx_draw(T1_Efx *e, const T1_Terrain *terr, const T1_Cam *cam,
                 const T1_Screen *scr, const W98_Overlays *ov,
                 int (*shown)(int cx, int cz),
                 void (*meshdraw)(int family, int idx, float wx, float wy, float wz,
                                  float scale, int house, unsigned int seed, int age))
{
    long px = 0;
    int i;
    (void)ov;
    if (!e->ok) return 0;

    /* Additive, depth tested but not depth written: an effect overlays the units it is
     * happening to and still sits behind a hill in front of it. */
    t1_glide_blend_add(1);
    t1_glide_depth_write(0);
    t1_glide_ckey(0, 0);
    t1_glide_filter(1);        /* the N64 filtered these tiny textures too */

    for (i = 0; i < e->npart; ++i)
    {
        const T1_EfxPart *p = &e->part[i];
        const T1_EfxSeq *s;
        SR_Vertex v;
        float sx, sy, half;
        int cx, cz, fr, lit;

        if (p->sys >= 10) continue;
        s = seq_named(e, T1EFX_SYS[p->sys]);
        if (!s || s->frames <= 0) continue;
        cx = (int)p->x; cz = (int)p->z;
        if (cx < 0 || cx > 63 || cz < 0 || cz > 63) continue;
        if (shown && !shown(cx, cz)) continue;

        t1_world_to_eye(cam, p->x, p->y, p->z, &v);
        if (!(v.w > 1.0f)) continue;
        sx = scr->cx + v.x * scr->fx / v.w;
        sy = scr->cy - v.y * scr->fy / v.w;
        half = p->hw * scr->fx / v.w;
        if (half < 1.0f) half = 1.0f;
        if (half > 200.0f) half = 200.0f;

        /* THE FRAME LAW is the particle's own age against its own life, not the anim's
         * stage: a sprite outlives the anim that made it. RAM 0x80050060/0x80050298. */
        fr = (int)((long)p->age * s->frames / (p->life > 0 ? p->life : 1));
        if (fr < 0) fr = 0;
        if (fr >= s->frames) fr = s->frames - 1;

        /* THE ALPHA RAMP, RAM 0x8004FFC8: min(lifeLeft * arate, a). The card has no
         * per-quad alpha on this path, so it is folded into the additive brightness,
         * which for an additive blend is the same thing. */
        lit = (p->life - p->age) * (int)p->arate;
        if (lit > (int)p->a) lit = (int)p->a;
        if (lit < 0) lit = 0;

        t1_glide_quad(sx - half, sy - half, sx + half, sy + half,
                      0.0f, 0.0f, (float)s->uw, (float)s->uh, &s->tex[fr],
                      (float)lit / 255.0f);
        px += (long)(half * half * 4.0f);
        ++e->drawn;
        ++e->seqdrawn[(int)(s - e->seq)];
    }

    t1_glide_filter(0);
    t1_glide_depth_write(1);
    t1_glide_blend_add(0);

    /* The debris chunks are MESHES, not sprites, so they are drawn by the caller's mesh
     * path in its own state rather than in the additive one. */
    if (meshdraw)
        for (i = 0; i < e->nchunk; ++i)
        {
            const T1_EfxPart *c = &e->chunk[i];
            int cx = (int)c->x, cz = (int)c->z;
            if (cx < 0 || cx > 63 || cz < 0 || cz > 63) continue;
            if (shown && !shown(cx, cz)) continue;
            if (c->scale <= 0.0f) continue;
            meshdraw((int)c->family, (int)c->mesh, c->x, c->y, c->z,
                     c->scale, (int)c->house, c->seed, (int)c->age);
        }
    (void)terr;
    return px;
}
