/*
 * t1_efx.h -- the cartridge's combat effects, on the Voodoo 2.
 *
 * WHAT THIS IS AND WHAT IT IS NOT, stated up front because the difference matters.
 *
 * The N64 draws combat effects with a real PARTICLE ENGINE: an anim arms emitter sources
 * whose particles outlive it, bounce, tow ember trails and age out on their own clocks.
 * The desktop build has all of that, off 66 decoded recipes over 65 source templates and
 * 10 systems. THIS IS NOT THAT. This draws ONE BILLBOARD per live anim, out of the
 * cartridge's own art, at the cartridge's own size, picking the sequence through the
 * cartridge's own anim -> recipe -> source -> system chain. So an explosion is the right
 * art in the right place on the right tick, and it is not a cloud of debris.
 *
 * THAT WAS TRUE UNTIL NOW. The simulation is here: a fixed pool of particles that carry
 * their own position, velocity, gravity, size growth, life and alpha ramp, spawned by the
 * cartridge's own burst arithmetic off the recipe slots and integrated once per ENGINE
 * TICK. Debris chunks are ballistic, bounce off the heightfield at the console's own 0.7
 * elasticity, shrink as the console shrinks them, and tow one ember per tick behind them.
 *
 * EVERY NUMBER IS THE ROM'S. The spawn law, the integrator, the burst decay, the alpha
 * ramp and the frame law are transcriptions of the desktop build's game/effects_mod.h,
 * which is itself a line-by-line transcription of the disassembly with its addresses in
 * the comments. The two things that are OURS and are marked as such at their definitions:
 * the chunk launch direction (the explosion updater has never been disassembled) and the
 * stateless hash that stands in for the console's rand(), which exists so that two runs
 * of the same script produce the same pixels.
 */

#ifndef T1_EFX_H
#define T1_EFX_H

#include "softras.h"
#include "t1_cam.h"
#include "t1_terrain.h"
#include "w98_brain.h"

#define T1EFX_MAXSEQ   16
#define T1EFX_MAXFRAME 16

typedef struct
{
    char name[9];
    int  frames, w, h, uw, uh;
    SR_Texture tex[T1EFX_MAXFRAME];
} T1_EfxSeq;

/* THE POOL. Measured on the desktop build: a 900-tick skirmish with two burning wrecks
 * and a running firefight peaked at 979 live sprites and 34 live chunks. This box has a
 * quarter of the desktop's fill rate and a fiftieth of its CPU, so the caps are lower and
 * eviction is visible in the report rather than silent -- 512 sprites is about 2.5 ms of
 * submission here, which is the whole affordable share of an 18 ms frame. */
#define T1EFX_MAX_PART   512
#define T1EFX_MAX_CHUNK   24
#define T1EFX_EVICT       64

typedef struct
{
    float x, y, z;            /* position, in cells */
    float vx, vy, vz;         /* velocity, cells per engine tick */
    float ay;                 /* y acceleration, cells/tick^2 */
    float hw, grow;           /* HALF-width in cells, and its growth per tick */
    short life, age;          /* ticks */
    unsigned char sys;        /* which particle system, i.e. which art sequence */
    unsigned char arate;      /* the ALPHA RAMP SLOPE per tick, not a frame rate */
    unsigned char r, g, b, a;
    unsigned int  seed;       /* chunks: seeds their ember trail */
    /* CHUNKS ONLY. mesh 0..6 picks one of the seven display lists of its family;
     * family 0 is vehicle wreckage, 1 is structure. scale is the console's own live
     * size, starting at the source record's 0.25 and shrinking to nothing by tick 50. */
    unsigned char mesh, family, house;
    float scale;
} T1_EfxPart;

typedef struct
{
    unsigned char *blob;
    long           blobsize;
    T1_EfxSeq      seq[T1EFX_MAXSEQ];
    int            nseq, ok;
    long           drawn, unmapped;   /* cumulative over the run */
    char           firstunmapped[16];
    long           seqdrawn[T1EFX_MAXSEQ];   /* which art actually reached the screen */

    /* the simulation */
    T1_EfxPart     part[T1EFX_MAX_PART];
    T1_EfxPart     chunk[T1EFX_MAX_CHUNK];
    int            npart, nchunk;
    long           lasttick;          /* so a frame cannot step the pool twice */
    long           spawned, chunks_spawned, evicted, chunk_dropped;
    int            peak_part, peak_chunk;
    /* Which (anim type, heap id) pairs were live on the previous dump, so a slot fires
     * once per anim rather than once per frame. Open addressed: C89 has no map. */
    unsigned int   live[64];
    int            nlive;
} T1_Efx;

int  t1_efx_load(T1_Efx *e, const char *path, char *err, int errlen);
void t1_efx_free(T1_Efx *e);
int  t1_efx_upload(T1_Efx *e, char *err, int errlen);

/* Clear the pool. Mission start, and any time the tick counter goes backwards. */
void t1_efx_reset(T1_Efx *e);

/* THE SIMULATION. Called once per object dump with the engine's tick: it fires every
 * live anim's recipe slots and then integrates the pool forward to `tick`. It is the only
 * place anything spawns or moves, and it never reads a wall clock, so two runs of the
 * same script produce the same pixels. */
void t1_efx_step(T1_Efx *e, const T1_Terrain *terr, const W98_Overlays *ov, long tick);

/* Draws the pool. `meshdraw` is called for each debris chunk with its model index and
 * pose; pass NULL to draw the sprites only. */
long t1_efx_draw(T1_Efx *e, const T1_Terrain *terr, const T1_Cam *cam,
                 const T1_Screen *scr, const W98_Overlays *ov,
                 int (*shown)(int cx, int cz),
                 void (*meshdraw)(int family, int idx, float wx, float wy, float wz,
                                  float scale, int house, unsigned int seed, int age));

#endif /* T1_EFX_H */
