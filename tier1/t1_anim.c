/* t1_anim.c -- see t1_anim.h. A C89 transcription of the desktop build's decoded
 * animation drivers; the ROM addresses in the comments are the evidence for each rule
 * and are kept verbatim so the two builds can be diffed against each other. */

#include <string.h>
#include <math.h>
#include "t1_anim.h"

/* ------------------------------------------------------------------------------------
 * THE IDLE ANIMATION TABLE, out of the 1995 engine's own bdata.cpp `_anims[]`.
 *
 * `rate` is the DOS tick rate for one stage of that building's BSTATE_IDLE animation.
 * `per_stage` is the cartridge ARM's own constant: how many baked frames one stage of
 * the building's counter advances. So a cycle takes (segment / per_stage) * rate engine
 * ticks and neither number is a taste value.
 *
 *     {STRUCT_CONST,          BSTATE_IDLE, 0,  4,  3}   FACT
 *     {STRUCT_EYE,            BSTATE_IDLE, 0, 16,  4}   EYE
 *     {STRUCT_RADAR,          BSTATE_IDLE, 0, 16,  4}   HQ
 *     {STRUCT_AIRSTRIP,       BSTATE_IDLE, 0, 16,  3}   AFLD
 *     {STRUCT_BARRACKS,       BSTATE_IDLE, 0, 10,  3}   HAND / PYLE
 *     {STRUCT_PUMP,           BSTATE_IDLE, 0, 14,  4}   V19
 *     {STRUCT_POWER,          BSTATE_IDLE, 0,  4, 15}   NUKE
 *     {STRUCT_ADVANCED_POWER, BSTATE_IDLE, 0,  4, 15}   NUK2
 *
 * `folded` marks the two arms that ping-pong. `seg1` is the frame at which the clip
 * stops being the idle animation, measured from the per-frame node motion in the baked
 * pack; 0 means the whole clip idles. Only the Construction Yard has one, and its second
 * segment is its BSTATE_ACTIVE animation -- which is what plays while it is BUILDING
 * something. Running the whole clip as one loop is exactly the bug reported on the
 * desktop build ("the yard's idle is the animation that belongs to building something").
 * ---------------------------------------------------------------------------------- */
typedef struct { const char *type; float per_stage; int rate; int folded; int seg1; } T1_Arm;

static const T1_Arm T1_ARMS[] = {
    { "FACT", 1.0f,     3,  0, 50 },   /* RAM 0x8003DFC0  frame = stage       */
    { "EYE",  1.0f,     4,  0, 0  },   /* RAM 0x8003DDE8  frame = stage       */
    { "HQ",   1.0f,     4,  0, 0  },   /* RAM 0x8003DDD0  frame = stage       */
    { "PYLE", 10.0f,    3,  0, 0  },   /* RAM 0x8003E1A4  frame = stage * 10  */
    { "AFLD", 2.0f,     3,  0, 0  },   /* RAM 0x8003DF48  frame = stage * 2   */
    { "HAND", 10.0f,    3,  0, 0  },   /* RAM 0x8003DE00  frame = stage * 10  */
    { "V19",  2.5f,     4,  0, 0  },   /* RAM 0x8003E1C8  frame = stage * 2.5 */
    { "NUKE", 1.0f,     15, 1,  0 },   /* RAM 0x8003DF64  stage<20 ? stage : 39-stage */
    { "NUK2", 1.0f,     15, 1,  0 },
    /* ATWR has no BSTATE_IDLE row in bdata.cpp's _anims[], so there is no engine rate to
     * borrow and the 3 below is a default, ours. Its frames-per-stage is still the
     * cartridge's own arm constant. */
    { "ATWR", 1.0f,     3,  0, 0  }    /* RAM 0x8003DDB8  frame = stage       */
};
#define T1_NARMS ((int)(sizeof T1_ARMS / sizeof T1_ARMS[0]))

/* The console runs its display at roughly 30 Hz and `rate` is counted against the 15 Hz
 * GAME tick, so a counter advanced once per VIDEO frame runs at exactly twice ours. That
 * arrived at the same factor independently by eye on the Barracks flag and confirmed it.
 * IT IS A HYPOTHESIS, NOT A DECODE: the console's clock source was never found. If
 * someone reads it out of the ROM, this constant is the thing to delete. */
#define T1_ANIM_VIDEO_VS_TICK 2.0f

float t1_anim_structure(const W98_Object *o, int frames, long ticks)
{
    const T1_Arm *arm = 0;
    int i, f0, f1, segframes, span, counter, t;
    float frame, ticks_per_stage;

    if (frames <= 1) return -1.0f;

    /* --- the SAM site: three states of one 501-frame clip, all engine-driven ---------
     * Decoded at RAM 0x8003DE24. Unlike every arm below it this one needs no counter of
     * ours: the cartridge's own mission code writes the stage by hand, so the numbers
     * line up with the brain exactly. Constants: 0.15625 at RAM 0x80003E08, 40 at
     * 0x0E0C/0E10, 5 at 0x0E14, 200 at 0x0E18, 12.5 at 0x0E1C. */
    if (!strcmp(o->name, "SAM"))
    {
        if (o->status == 2 || o->status == 3 || o->status == 6)
        {
            /* Up and turned to its target. face is PrimaryFacing, the same byte the arm
             * reads at building+0x3C. */
            float f = (float)(o->face >= 0 ? o->face : 0) * 0.15625f;
            if (f < 0.0f)  f += 40.0f;
            if (f > 40.0f) f -= 40.0f;
            frame = (40.0f - f) * 5.0f + 200.0f;
        }
        else
        {
            int stage = o->dostage > 0 ? o->dostage : 0;
            if (stage >= 33) stage = 64 - stage;      /* the lowering fold */
            frame = (float)stage * 12.5f;
        }
        if (frame < 0.0f) frame = 0.0f;
        if (frame > (float)(frames - 1)) frame = (float)(frames - 1);
        return frame;
    }

    /* --- the War Factory door: no counter, no loop, the engine's own door position ---
     * The war factory opens with Open_Door(2, 11), so Stages is 10 and Door_Stage() runs
     * 0..9; 59/9 is RAM 0x80003E04's 6.55556, which maps those ten stages onto clip
     * frames 0..59. Frames 60..100 are a second motion the cartridge's arm never reaches,
     * so neither do we. */
    if (!strcmp(o->name, "WEAP"))
    {
        if (o->doorstage < 0) return 0.0f;    /* hold it shut rather than invent a pose */
        frame = (float)o->doorstage * (59.0f / 9.0f);
        if (frame < 0.0f) frame = 0.0f;
        if (frame > (float)(frames - 1)) frame = (float)(frames - 1);
        return frame;
    }

    for (i = 0; i < T1_NARMS; ++i)
        if (!strcmp(o->name, T1_ARMS[i].type)) { arm = &T1_ARMS[i]; break; }
    if (!arm) return -1.0f;

    /* Which segment of the clip the engine's own BState selects. A building with no
     * second segment ignores BState entirely and idles. */
    f0 = 0; f1 = frames;
    if (arm->seg1 > 0 && arm->seg1 < frames)
    {
        if (o->doing == T1_BSTATE_ACTIVE) { f0 = arm->seg1; f1 = frames; }
        else                              { f0 = 0;         f1 = arm->seg1; }
    }
    segframes = f1 - f0;
    if (segframes <= 1) return (float)f0;

    /* WHY THE STAGE FREE-RUNS instead of reading the brain's `dostage`, which is the
     * quantity the cartridge's arms index. Two reasons:
     *  (1) the 1995 idle animation and the 3-D clip are not the same length and are not
     *      close -- bdata.cpp gives the yard FOUR idle stages against a fifty-frame idle
     *      segment, so driving one from the other plays 8% of it in a four-step stutter;
     *  (2) the console's own counter cannot settle it either, because on the cartridge it
     *      does not move at all (Anims flat, rate 0).
     * The arms are unclamped -- PYLE writes frame = stage*10 with no modulo and the
     * cartridge's cycle controller at RAM 0x8007F264 folds any t back into the period --
     * so counting and wrapping by the segment length is the cartridge's own shape. */
    span = arm->folded ? (segframes - 1) * 2 : segframes;
    if (span <= 0) return (float)f0;
    ticks_per_stage = (arm->rate > 0 ? (float)arm->rate : 1.0f) / T1_ANIM_VIDEO_VS_TICK;
    if (ticks_per_stage <= 0.0f) ticks_per_stage = 1.0f;
    counter = (int)((float)ticks / ticks_per_stage);
    t = (int)((float)counter * arm->per_stage) % span;
    frame = (float)t;
    if (arm->folded && t >= segframes)             /* ...and back down again */
        frame = (float)((segframes - 1) * 2 - t);
    if (frame < 0.0f) frame = 0.0f;
    if (frame > (float)(segframes - 1)) frame = (float)(segframes - 1);
    return (float)f0 + frame;
}

float t1_anim_default(const T1_MeshBank *b, int mi, long ticks)
{
    int frames = 0, tpf = 1, t0 = 0, t1 = 0, loop = 1, span, t;
    if (!t1_mesh_clip(b, mi, &frames, &tpf, &t0, &t1, &loop)) return -1.0f;
    if (tpf <= 0) tpf = 1;
    /* The clip table's first entry is the idle loop by convention: the baker writes the
     * t = 0 range first. A mesh with an unusable range loops over all its frames. */
    t0 /= tpf; t1 /= tpf;
    if (t1 <= t0 || t1 > frames) { t0 = 0; t1 = frames; }
    span = t1 - t0;
    if (span <= 0) return -1.0f;
    t = (int)(ticks / tpf);
    if (!loop && t >= span) return (float)(t1 - 1);
    return (float)(t0 + (t % span));
}

float t1_anim_object(const T1_MeshBank *b, int mi, const W98_Object *o, long ticks)
{
    int frames = 0;
    int building = (strcmp(o->kind, "BUILDING") == 0);

    if (!t1_mesh_clip(b, mi, &frames, 0, 0, 0, 0) || frames <= 1) return -1.0f;

    /* THE CONSTRUCTION YARD HOLDS ITS REST POSE UNLESS IT IS BUILDING SOMETHING, and
     * this is a measurement rather than a preference. Differencing the original console
     * capture frame by frame, the only thing that moves on that building is the two fan
     * discs: the crane does not swing and the roof plate does not shift. FACT's clip has
     * two segments (the arm table gives it seg1 = 50) and the second one is the yard
     * BUILDING something, which the engine marks by BState. A yard doing nothing reports
     * doing=1 BS_IDLE; it goes to doing=2 BS_ACTIVE for exactly the span a placed
     * building is going up and returns to 1 when it finishes. Driving the IDLE segment
     * from a free counter is what detached the plate from the vault on the desktop build.
     *
     * Deliberately NOT a blanket rule: the Barracks flag was confirmed to animate, so
     * other structures really do play theirs. FACT is named because FACT is what the
     * capture shows. */
    if (building && strcmp(o->name, "FACT") == 0 && o->doing != T1_BSTATE_ACTIVE)
        return -1.0f;

    if (building)
    {
        float f = t1_anim_structure(o, frames, ticks);
        if (f >= 0.0f) return f;
    }
    return t1_anim_default(b, mi, ticks);
}

/* ---- the buildup clock. See the long note in t1_anim.h for why it is ours. --------- */
/* THE ID IS ONLY UNIQUE INSIDE ITS OWN HEAP. `id` is the object's slot in its own
 * TFixedIHeapClass pool, so the first BUILDING and the first UNIT are both id 0. Keying
 * the tracker on the id alone is why the very first version of this never played a single
 * buildup: deploying the MCV deletes unit 0 and creates building 0, the tracker already
 * had a row for "0" that it had marked as not-a-building, and every Construction Yard
 * from then on was born already finished. One character of the kind is enough to
 * separate the five heaps. */
#define T1_TRACK_MAX 256
static struct { int id; char heap; long tick0; int building;
                int lx, ly, still; } g_track[T1_TRACK_MAX];
static int g_ntrack;
static int g_seenfirst;      /* the first object dump has been and gone */

/* The facing audit's tally: how far, in DirType units, the direction a unit MOVED sat
 * from the facing the engine reported for it. Zero is a perfect match; 128 is backwards;
 * 64 is sideways. Per type as well as overall, because the two bugs reported were
 * one type each. */
static long g_audit_n, g_audit_sum, g_audit_max;
static struct { char name[12]; long n, sum, max; } g_audit[8];
static int  g_audit_nn;

static long track_birth(int id, char heap)
{
    int i;
    for (i = 0; i < g_ntrack; ++i)
        if (g_track[i].id == id && g_track[i].heap == heap) return g_track[i].tick0;
    return -1;
}

void t1_anim_track(const W98_Object *objs, int n, long ticks)
{
    static int seen[T1_TRACK_MAX];
    int i, k;

    /* Retire ids the engine has stopped reporting, so a heap slot reused by a later
     * object does not inherit the dead one's birthday and refuse to build. */
    for (k = 0; k < g_ntrack; ++k) seen[k] = 0;
    for (i = 0; i < n; ++i)
    {
        int id = objs[i].id;
        if (id < 0 || objs[i].limbo) continue;
        for (k = 0; k < g_ntrack; ++k)
            if (g_track[k].id == id && g_track[k].heap == objs[i].kind[0])
            {
                /* HAS IT MOVED SINCE THE LAST DUMP. Leptons, not cells: a unit crossing
                 * a cell boundary is still driving for the twenty ticks in between, and
                 * a cell-granularity test would call it stopped for most of the trip. */
                int moved = (objs[i].lx != g_track[k].lx)
                         || (objs[i].ly != g_track[k].ly);
                /* THE FACING AUDIT. The invariant is main's own: the compass direction a
                 * unit actually MOVES must be the engine facing it reports, because the
                 * drawn nose is that facing by construction once model_forward is right.
                 * A single tick's delta is only a few leptons and quantised, so samples
                 * under four leptons of travel are thrown away rather than averaged in. */
                if (moved && strcmp(objs[i].kind, "UNIT") == 0 && objs[i].face >= 0)
                {
                    int dx = objs[i].lx - g_track[k].lx;
                    int dy = objs[i].ly - g_track[k].ly;
                    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
                    if (adx + ady >= 4)
                    {
                        int mv = (int)(atan2((double)dx, (double)-dy)
                                       * (128.0 / 3.14159265358979)) & 255;
                        int d  = (mv - objs[i].face) & 255;
                        if (d > 128) d -= 256;
                        if (d < 0) d = -d;
                        ++g_audit_n; g_audit_sum += d;
                        if (d > g_audit_max) g_audit_max = d;
                        if (g_audit_nn < 8)
                        {
                            int q;
                            for (q = 0; q < g_audit_nn; ++q)
                                if (!strcmp(g_audit[q].name, objs[i].name)) break;
                            if (q == g_audit_nn)
                            {
                                strncpy(g_audit[q].name, objs[i].name, 11);
                                g_audit[q].name[11] = 0;
                                g_audit[q].n = 0; g_audit[q].sum = 0; g_audit[q].max = 0;
                                ++g_audit_nn;
                            }
                            ++g_audit[q].n; g_audit[q].sum += d;
                            if (d > g_audit[q].max) g_audit[q].max = d;
                        }
                    }
                }
                g_track[k].lx = objs[i].lx;
                g_track[k].ly = objs[i].ly;
                if (moved) g_track[k].still = 0;
                else if (g_track[k].still < 1000000) ++g_track[k].still;
                seen[k] = 1; break;
            }
        if (k == g_ntrack && g_ntrack < T1_TRACK_MAX)
        {
            g_track[k].id       = id;
            g_track[k].heap     = objs[i].kind[0];
            g_track[k].tick0    = ticks;
            /* WHICH BUILDINGS ASSEMBLE. Two ways to qualify, and the second one is what
             * makes the MCV deploy work:
             *
             *  - the engine says CONSTRUCTION at the first sighting, which is what a
             *    building placed from the sidebar reports; or
             *  - the object was BORN after the first object dump. Everything standing at
             *    mission start -- SCG01EA alone has about twenty civilian structures --
             *    is present in dump one and must NOT rebuild itself, but a Construction
             *    Yard only ever comes into existence by an MCV deploying, and on this
             *    brain that transition can be over before a dump catches its BState:
             *    with makecnt stuck at 1 the engine leaves CONSTRUCTION almost at once.
             *    Seen in the report as rig_draws = 0 with a perfectly good rig loaded. */
            g_track[k].building = (strcmp(objs[i].kind, "BUILDING") == 0)
                                && (g_seenfirst
                                    || objs[i].doing == T1_BSTATE_CONSTRUCTION);
            g_track[k].lx    = objs[i].lx;
            g_track[k].ly    = objs[i].ly;
            g_track[k].still = T1_AIM_STILL_TICKS;   /* first sight: at rest */
            seen[k] = 1;
            ++g_ntrack;
        }
    }
    g_seenfirst = 1;
    for (k = 0; k < g_ntrack; )
    {
        if (!seen[k])
        {
            g_track[k] = g_track[g_ntrack - 1];
            seen[k]    = seen[g_ntrack - 1];
            --g_ntrack;
        }
        else ++k;
    }
}

/* 0..1 through the engine's five seconds, or -1 for "this object is not assembling". */
static float buildup_u(const W98_Object *o, long ticks)
{
    long t0;
    float u;
    if (strcmp(o->kind, "BUILDING") != 0) return -1.0f;
    if (o->id < 0) return -1.0f;
    {   /* only if it was CONSTRUCTION when we first saw it */
        int i;
        for (i = 0; i < g_ntrack; ++i)
            if (g_track[i].id == o->id && g_track[i].heap == o->kind[0])
            { if (!g_track[i].building) return -1.0f; break; }
        if (i == g_ntrack) return -1.0f;
    }
    t0 = track_birth(o->id, o->kind[0]);
    if (t0 < 0) return -1.0f;
    u = (float)(ticks - t0) / (float)T1_BUILDUP_TICKS;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) return -1.0f;              /* finished: the whole building stands */
    return u;
}

float t1_anim_build_frac(const W98_Object *o, long ticks)
{
    float u = buildup_u(o, ticks), f;
    int selling = (strcmp(o->mission, "Selling") == 0);

    /* A SOLD building sheds its sections in reverse, and that one the engine does tell
     * us about: MISSION_DECONSTRUCTION is reported by name (mission.cpp:502) and it runs
     * for as long as the engine keeps saying so. */
    if (selling && strcmp(o->kind, "BUILDING") == 0)
    {
        long t0 = track_birth(o->id, o->kind[0]);
        float v = (t0 >= 0) ? (float)(ticks - t0) / (float)T1_BUILDUP_TICKS : 1.0f;
        if (v > 1.0f) v = 1.0f;
        return 1.0f - v;
    }
    if (u < 0.0f) return 1.0f;

    /* The console's assembly runs at TWICE the engine's buildup clock (measured by comparing
     * the desktop build against the real cartridge): a placed building is fully assembled
     * halfway through and then stands complete for the rest of the span. */
    f = 2.0f * u;
    if (f > 1.0f) f = 1.0f;
    if (f < 0.02f) f = 0.02f;    /* one section immediately, never an empty pad */
    return f;
}

int t1_anim_mcvrig(const T1_MeshBank *b, const W98_Object *o, long ticks, float *animT)
{
    int mi, frames = 0;
    float frame, u;

    *animT = -1.0f;
    if (strcmp(o->kind, "BUILDING") != 0 || strcmp(o->name, "FACT") != 0) return -1;
    u = buildup_u(o, ticks);
    if (u < 0.0f) return -1;
    mi = t1_mesh_for_type(b, "MCVANIM");
    if (mi < 0) return -1;
    if (!t1_mesh_clip(b, mi, &frames, 0, 0, 0, 0) || frames <= 1) return -1;

    /* THE CARTRIDGE'S OWN FORMULA is frame = stage * 1.5625 (RAM 0x80003D14, written
     * straight into the draw command by BuildingClass's 3-D draw virtual for a yard under
     * MISSION_CONSTRUCTION). 1.5625 is 100/64 and the rig's clip is 102 baked frames, so
     * the console's stage counter for this building runs 0..64 -- i.e. the clip is played
     * across the whole of the construction state, once, forwards.
     *
     * Our brain has no stage counter here at all (see t1_anim.h), so the span is the
     * engine's five seconds and the mapping is the same one: the whole clip, once. When a
     * brain that loads MAKE.SHP arrives, this becomes stage * 1.5625 * (64/makecnt) and
     * reduces to the ROM's literal constant at makecnt 64. */
    frame = u * (float)(frames - 1);

    /* SELLING is a registered CHOICE, not a decode: a sold Construction Yard hands an MCV
     * back in Tiberian Dawn, so the same clip runs backwards. No ROM evidence either way
     * and none is claimed. */
    if (strcmp(o->mission, "Selling") == 0) frame = (float)(frames - 1) - frame;
    if (frame < 0.0f) frame = 0.0f;
    if (frame > (float)(frames - 1)) frame = (float)(frames - 1);
    *animT = frame;
    return mi;
}

int t1_anim_procrig(const T1_MeshBank *b, const W98_Object *o, float *animT)
{
    int mi, frames = 0, stage;
    float rig, frame;

    *animT = -1.0f;
    if (strcmp(o->kind, "BUILDING") != 0 || strcmp(o->name, "PROC") != 0) return -1;

    /* The arm, transcribed from RAM 0x8003DFD8, with the engine's own refinery stage as
     * its input. bdata.cpp gives STRUCT_REFINERY five ranges -- IDLE {0,6}, FULL {6,6},
     * ACTIVE {12,7}, AUX1 {19,5}, AUX2 {24,6} -- i.e. stages 0..29, and the arm's own
     * thresholds are 6, 12, 19, 24, 29 and 30. Constants: 10 at RAM 0x80003E2C/0E34. */
    stage = o->dostage > 0 ? o->dostage : 0;
    if (stage >= 60) return -1;               /* the arm draws nothing at all up here */
    if (stage >= 30) stage -= 30;
    if (stage < 12 || stage == 29) return -1; /* idle and FULL emit the refinery only */

    mi = t1_mesh_for_type(b, "PROCANIM");
    if (mi < 0) return -1;
    if (!t1_mesh_clip(b, mi, &frames, 0, 0, 0, 0) || frames <= 1) return -1;

    rig = (stage < 24) ? (float)(stage - 11) : (float)(29 - stage);
    frame = rig * 10.0f;
    if (frame < 0.0f) frame = 0.0f;
    if (frame > (float)(frames - 1)) frame = (float)(frames - 1);
    *animT = frame;
    return mi;
}

int t1_anim_still(const W98_Object *o)
{
    int i;
    if (o->id < 0) return 1;
    for (i = 0; i < g_ntrack; ++i)
        if (g_track[i].id == o->id && g_track[i].heap == o->kind[0])
            return g_track[i].still >= T1_AIM_STILL_TICKS;
    return 1;   /* never seen: treat as standing, which is how it starts */
}

void t1_anim_face_audit(long *n, long *sum, long *max) 
{ *n = g_audit_n; *sum = g_audit_sum; *max = g_audit_max; }

int t1_anim_face_audit_type(int i, const char **name, long *n, long *sum, long *max)
{
    if (i < 0 || i >= g_audit_nn) return 0;
    *name = g_audit[i].name; *n = g_audit[i].n;
    *sum = g_audit[i].sum;   *max = g_audit[i].max;
    return 1;
}
