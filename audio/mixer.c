#include "mixer.h"

#include <stdlib.h>
#include <string.h>

#define MUSIC_RING 65536 /* samples: 3.0 s of headroom at 22050 Hz */
#define MOVIE_RING 65536

typedef struct
{
    int gain;
    int target;
    long ramp_left;
    int ramp_from;
    long ramp_total;
} Gain;

typedef struct
{
    const short *pcm;
    long samples;
    long pos;
    int gain;     /* per mille           */
    int pan;      /* -1000 .. +1000      */
    int lvol;     /* derived, 0..4096    */
    int rvol;     /* derived, 0..4096    */
    int bus;
    int priority;
    int active;
    int loop;            /* wrap to pos 0 instead of retiring; see mixer_play_loop  */
    unsigned int serial; /* handle = slot | serial << 8, so a stale handle is inert */
} Voice;

typedef struct
{
    short *buf;
    int cap;
    int head; /* read  */
    int tail; /* write */
} Ring;

struct Mixer
{
    Gain bus[MIX_BUS_COUNT];
    Gain master;
    Voice v[MIX_VOICES];
    unsigned int serial;

    Ring music, movie;

    int last_peak;
    long frames_rendered;
    long knee_samples; /* output samples the soft knee bent (above KNEE_START)   */
    long hard_samples; /* output samples that still hit the hard ceiling         */
};

/* ------------------------------------------------------------------- gains */

static void gain_init(Gain *g)
{
    g->gain = MIX_UNITY;
    g->target = MIX_UNITY;
    g->ramp_left = 0;
    g->ramp_from = MIX_UNITY;
    g->ramp_total = 0;
}

static void gain_set(Gain *g, int target, int ms)
{
    if (target < 0)
        target = 0;
    if (target > MIX_GAIN_MAX)
        target = MIX_GAIN_MAX;
    if (ms <= 0) {
        g->gain = target;
        g->target = target;
        g->ramp_left = 0;
        return;
    }
    g->ramp_from = g->gain;
    g->target = target;
    g->ramp_total = (long)ms * MIX_RATE / 1000;
    if (g->ramp_total < 1)
        g->ramp_total = 1;
    g->ramp_left = g->ramp_total;
}

/* Advance the ramp by `n` frames and return the gain to use for that block. Ramping
 * per block rather than per sample: at 22050 Hz a block is at most a few ms, the
 * step is inaudible, and it keeps the inner loop to two multiplies per frame. */
static int gain_advance(Gain *g, int n)
{
    int now = g->gain;
    if (g->ramp_left > 0) {
        long done;
        g->ramp_left -= n;
        if (g->ramp_left <= 0) {
            g->ramp_left = 0;
            g->gain = g->target;
        } else {
            done = g->ramp_total - g->ramp_left;
            g->gain = g->ramp_from + (int)(((long)(g->target - g->ramp_from) * done) / g->ramp_total);
        }
        now = (now + g->gain) / 2; /* midpoint of the block */
    }
    return now;
}

/* ------------------------------------------------------------------- rings */

static int ring_init(Ring *r, int cap)
{
    r->buf = (short *)calloc((size_t)cap, sizeof(short));
    r->cap = cap;
    r->head = r->tail = 0;
    return r->buf != NULL;
}

static int ring_level(const Ring *r)
{
    int n = r->tail - r->head;
    if (n < 0)
        n += r->cap;
    return n;
}

static int ring_free(const Ring *r) { return r->cap - 1 - ring_level(r); }

static int ring_push(Ring *r, const short *src, int n)
{
    int space = ring_free(r);
    int done = 0;
    if (n > space)
        n = space;
    while (done < n) {
        int chunk = r->cap - r->tail;
        if (chunk > n - done)
            chunk = n - done;
        memcpy(r->buf + r->tail, src + done, (size_t)chunk * sizeof(short));
        r->tail = (r->tail + chunk) % r->cap;
        done += chunk;
    }
    return done;
}

static int ring_pop(Ring *r, short *dst, int n)
{
    int have = ring_level(r);
    int done = 0;
    if (n > have)
        n = have;
    while (done < n) {
        int chunk = r->cap - r->head;
        if (chunk > n - done)
            chunk = n - done;
        memcpy(dst + done, r->buf + r->head, (size_t)chunk * sizeof(short));
        r->head = (r->head + chunk) % r->cap;
        done += chunk;
    }
    return done;
}

/* ------------------------------------------------------------------- setup */

Mixer *mixer_create(void)
{
    Mixer *m = (Mixer *)calloc(1, sizeof(Mixer));
    int i;
    if (!m)
        return NULL;
    for (i = 0; i < MIX_BUS_COUNT; i++)
        gain_init(&m->bus[i]);
    gain_init(&m->master);
    if (!ring_init(&m->music, MUSIC_RING) || !ring_init(&m->movie, MOVIE_RING)) {
        mixer_destroy(m);
        return NULL;
    }
    m->serial = 1;
    return m;
}

void mixer_destroy(Mixer *m)
{
    if (!m)
        return;
    free(m->music.buf);
    free(m->movie.buf);
    free(m);
}

void mixer_bus_gain(Mixer *m, int bus, int permille, int ms)
{
    if (bus < 0 || bus >= MIX_BUS_COUNT)
        return;
    gain_set(&m->bus[bus], permille, ms);
}

int mixer_bus_gain_get(const Mixer *m, int bus)
{
    if (bus < 0 || bus >= MIX_BUS_COUNT)
        return 0;
    return m->bus[bus].gain;
}

void mixer_master_gain(Mixer *m, int permille, int ms) { gain_set(&m->master, permille, ms); }
int mixer_master_gain_get(const Mixer *m) { return m->master.gain; }

/* ------------------------------------------------------------------ voices */

/* Balance pan: the near channel holds unity, the far channel fades out. Q12 fixed
 * point, no cos table, no floats.
 *
 * Deliberately NOT equal power. Music and movie audio are mixed at unity on both
 * channels, so a centred effect must be unity on both too, or it sits quieter than
 * the score for no reason the player can perceive. Equal power would place centre at
 * 0.707 (-3 dB) and reintroduce a smaller version of the very fault below.
 *
 * The honest cost of this law: a sound panning from centre to hard left loses 3 dB of
 * total energy on the way, because the right channel fades while the left stays put.
 * Next to the distance attenuation the caller already applies, that is inaudible.
 *
 * Fixed. This previously divided by 2000, not 1000, so centre came out at
 * 2048 (0.5 in Q12) and EVERY centred effect and EVA line played 6.02 dB under the
 * music. The comment asserted the opposite of what the arithmetic did, which is
 * exactly why it survived review: the code read as correct. */
static void pan_split(int gain, int pan, int *lvol, int *rvol)
{
    long l, r;
    if (pan < -1000)
        pan = -1000;
    if (pan > 1000)
        pan = 1000;
    l = (long)(1000 - pan); /* 0 .. 2000, unity at 1000 */
    r = (long)(1000 + pan);
    if (l > 1000)
        l = 1000; /* clamp so the near side never exceeds unity and cannot clip */
    if (r > 1000)
        r = 1000;
    l = l * 4096 / 1000;
    r = r * 4096 / 1000;
    *lvol = (int)(l * gain / MIX_UNITY);
    *rvol = (int)(r * gain / MIX_UNITY);
}

int mixer_play(Mixer *m, int bus, const short *pcm, long samples, int gain, int pan, int priority)
{
    int i, slot = -1, worst = -1, worst_pri = 0x7FFFFFFF;

    if (!m || !pcm || samples <= 0 || bus < 0 || bus >= MIX_BUS_COUNT)
        return -1;
    if (gain < 0)
        gain = 0;
    if (gain > MIX_GAIN_MAX)
        gain = MIX_GAIN_MAX;

    for (i = 0; i < MIX_VOICES; i++) {
        if (!m->v[i].active) {
            slot = i;
            break;
        }
        if (m->v[i].priority < worst_pri) {
            worst_pri = m->v[i].priority;
            worst = i;
        }
    }
    if (slot < 0) {
        if (worst < 0 || worst_pri > priority)
            return -1; /* nothing worth stealing: drop the new sound, do not thrash */
        slot = worst;
    }

    m->v[slot].pcm = pcm;
    m->v[slot].samples = samples;
    m->v[slot].pos = 0;
    m->v[slot].gain = gain;
    m->v[slot].pan = pan;
    pan_split(gain, pan, &m->v[slot].lvol, &m->v[slot].rvol);
    m->v[slot].bus = bus;
    m->v[slot].priority = priority;
    m->v[slot].active = 1;
    m->v[slot].loop = 0;
    /* Serial keeps a stale handle from steering a recycled slot. 22 bits so the
     * packed handle never reaches the sign bit of an int. */
    m->serial = (m->serial + 1) & 0x3FFFFFu;
    if (m->serial == 0)
        m->serial = 1;
    m->v[slot].serial = m->serial;
    return slot | (int)(m->v[slot].serial << 8);
}

int mixer_play_loop(Mixer *m, int bus, const short *pcm, long samples, int gain, int pan, int priority)
{
    int h = mixer_play(m, bus, pcm, samples, gain, pan, priority);
    if (h >= 0)
        m->v[h & 0xFF].loop = 1;
    return h;
}

static Voice *voice_of(Mixer *m, int handle)
{
    int slot = handle & 0xFF;
    unsigned int serial = (unsigned int)handle >> 8;
    if (handle < 0 || slot >= MIX_VOICES)
        return NULL;
    if (!m->v[slot].active || m->v[slot].serial != serial)
        return NULL;
    return &m->v[slot];
}

void mixer_voice_stop(Mixer *m, int handle)
{
    Voice *v = voice_of(m, handle);
    if (v)
        v->active = 0;
}

void mixer_voice_gain(Mixer *m, int handle, int gain, int pan)
{
    Voice *v = voice_of(m, handle);
    if (!v)
        return;
    if (gain < 0)
        gain = 0;
    if (gain > MIX_GAIN_MAX)
        gain = MIX_GAIN_MAX;
    v->gain = gain;
    v->pan = pan;
    pan_split(gain, pan, &v->lvol, &v->rvol);
}

int mixer_voice_active(const Mixer *m, int handle)
{
    int slot = handle & 0xFF;
    unsigned int serial = (unsigned int)handle >> 8;
    if (handle < 0 || slot >= MIX_VOICES)
        return 0;
    return m->v[slot].active && m->v[slot].serial == serial;
}

void mixer_stop_bus(Mixer *m, int bus)
{
    int i;
    for (i = 0; i < MIX_VOICES; i++)
        if (m->v[i].active && m->v[i].bus == bus)
            m->v[i].active = 0;
}

int mixer_active_voices(const Mixer *m)
{
    int i, n = 0;
    for (i = 0; i < MIX_VOICES; i++)
        if (m->v[i].active)
            n++;
    return n;
}

/* ------------------------------------------------- the 1995 tactical rule */

#define TD_CELL_PIXELS 24 /* ICON_PIXEL_W: one cell is 24 screen pixels        */
#define TD_MAP_CELL_W 64  /* MAP_CELL_W for Tiberian Dawn                      */

int mixer_effect_place(int listener_cx, int listener_cy, int view_w, int view_h, int x, int y,
                       int *gain, int *pan)
{
    int half_w = view_w / 2;
    int half_h = view_h / 2;
    int dx, dy, dist_cells, v, p;

    if (x < 0 || y < 0) { /* the engine's "no coordinate" marker */
        *gain = MIX_UNITY;
        *pan = 0;
        return 1;
    }

    dx = x - listener_cx;
    dy = y - listener_cy;

    /* In view: full volume, no pan. Sound_Effect() does exactly this. */
    if (dx >= -half_w && dx <= half_w && dy >= -half_h && dy <= half_h) {
        *gain = MIX_UNITY;
        *pan = 0;
        return 1;
    }

    /* Out of view: the vanilla curve, in cells.
     *   d      = min(cell distance, 64)
     *   v      = 255 - (d * 255 / 64), halved, floored at 25
     * which lands between 25/255 and 127/255 of unity, so crossing the view edge is
     * an 8 dB step down. That step is in the original too and is deliberate: it is
     * how the 1995 game said "this is happening off screen".
     *
     * One deviation, on purpose: the original measures distance from
     * Coord_Cell(Map.TacticalCoord), the view's TOP LEFT CORNER, not its centre, so
     * a sound above and left of the view scores as close while the mirror image to
     * the lower right scores as far. We measure from the centre, which is what the
     * pan already does, so the field is symmetric. */
    {
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int big = adx > ady ? adx : ady;
        int small = adx > ady ? ady : adx;
        /* Map.Cell_Distance is the diagonal metric: max + half the min. */
        dist_cells = (big + small / 2) / TD_CELL_PIXELS;
    }
    if (dist_cells > TD_MAP_CELL_W)
        dist_cells = TD_MAP_CELL_W;
    v = 255 - (dist_cells * 255 / TD_MAP_CELL_W);
    v /= 2;
    if (v < 25)
        v = 25;
    *gain = v * MIX_UNITY / 255;

    /* Pan. The 1995 formula, transliterated:
     *
     *   pan = Cell_X(cell) - (view left cell + half the view width in cells)
     *   if |pan| > half the view width in cells:
     *       pan = pan * 0x8000 / (MAP_CELL_W >> 2)      clamped to +/-0x7FFF
     *   else
     *       pan = 0
     *
     * so it is the offset from the view CENTRE that is scaled, not the offset
     * beyond the edge, and it reaches hard over at 16 cells. That produces a step
     * at the view boundary rather than a fade: a sound one pixel outside the view
     * jumps straight to about 83 % pan. That step is in the original and is kept.
     * 0x7FFF maps to our +/-1000. */
    p = 0;
    if (dx < -half_w || dx > half_w) {
        int off_cells = dx / TD_CELL_PIXELS; /* from the view centre */
        p = off_cells * 1000 / (TD_MAP_CELL_W / 4);
        if (p < -1000)
            p = -1000;
        if (p > 1000)
            p = 1000;
    }
    *pan = p;
    return 1;
}

/* ------------------------------------------------------------- ring facade */

void mixer_music_reset(Mixer *m) { m->music.head = m->music.tail = 0; }
int mixer_music_want(const Mixer *m) { return ring_free(&m->music); }
int mixer_music_level(const Mixer *m) { return ring_level(&m->music); }
int mixer_music_push(Mixer *m, const short *pcm, int samples)
{
    return ring_push(&m->music, pcm, samples);
}

void mixer_movie_reset(Mixer *m) { m->movie.head = m->movie.tail = 0; }
int mixer_movie_want(const Mixer *m) { return ring_free(&m->movie); }
int mixer_movie_level(const Mixer *m) { return ring_level(&m->movie); }
int mixer_movie_push(Mixer *m, const short *pcm, int samples)
{
    return ring_push(&m->movie, pcm, samples);
}

/* ------------------------------------------------------------------ render */

#define BLOCK 256 /* frames per gain step, 11.6 ms at 22050 Hz */

/* The clipper. A hard clamp at 32767 was measured doing real damage in the
 * configuration a player actually runs: music at unity under a firefight of
 * unity-gain effects sums well past full scale, and a hard ceiling turns every
 * one of those peaks into a square edge you can hear as crackle.
 *
 * This is a soft knee instead: linear to KNEE_START (75 % of full scale), then a
 * quadratic bend that maps the next 16382 counts of overshoot into the remaining
 * 8191, meeting the ceiling with zero slope. Below the knee it is bit-identical to
 * the old clamp, so every existing level measurement stands. Integer only, two
 * multiplies in the bent region, nothing for the common case: fine for Win98.
 *
 *   KNEE_START = 24576, KNEE_RANGE = 2 * (32767 - 24576) = 16382
 *   out = KNEE_START + x - x*x / (2 * KNEE_RANGE)   for x = in - KNEE_START
 *   (derivative 1 at x = 0, derivative 0 at x = KNEE_RANGE, out there = 32767)
 *
 * knee/hard counters are kept so "how much did we clip" is a printed number, not
 * an argument: the knee count is audible-but-gentle territory, the hard count is
 * genuine overload that survived even the knee. */
#define KNEE_START 24576
#define KNEE_RANGE 16382

static short soft_clip(Mixer *m, int v)
{
    int a = v < 0 ? -v : v;
    int out;
    if (a <= KNEE_START)
        return (short)v;
    if (a >= KNEE_START + KNEE_RANGE) {
        m->hard_samples++;
        out = 32767;
    } else {
        int x = a - KNEE_START;
        m->knee_samples++;
        out = KNEE_START + x - (int)(((long)x * x) / (2 * KNEE_RANGE));
    }
    return (short)(v < 0 ? -out : out);
}

void mixer_render(Mixer *m, short *dst, int frames)
{
    short scratch[BLOCK];
    int acc[BLOCK * 2];
    int done = 0;
    int peak = 0;

    if (!m || !dst || frames <= 0)
        return;

    while (done < frames) {
        int n = frames - done;
        int i, gm, g_music, g_movie, g_fx, g_speech;
        int got;

        if (n > BLOCK)
            n = BLOCK;

        memset(acc, 0, (size_t)n * 2 * sizeof(int));

        gm = gain_advance(&m->master, n);
        g_music = gain_advance(&m->bus[MIX_BUS_MUSIC], n);
        g_fx = gain_advance(&m->bus[MIX_BUS_FX], n);
        g_speech = gain_advance(&m->bus[MIX_BUS_SPEECH], n);
        g_movie = gain_advance(&m->bus[MIX_BUS_MOVIE], n);

        /* Music: mono bed, centred. */
        if (g_music > 0) {
            got = ring_pop(&m->music, scratch, n);
            for (i = 0; i < got; i++) {
                int s = (int)scratch[i] * g_music / MIX_UNITY;
                acc[i * 2] += s;
                acc[i * 2 + 1] += s;
            }
        } else {
            ring_pop(&m->music, scratch, n); /* keep the clock honest while muted */
        }

        /* Movie audio: mono, centred. */
        if (g_movie > 0) {
            got = ring_pop(&m->movie, scratch, n);
            for (i = 0; i < got; i++) {
                int s = (int)scratch[i] * g_movie / MIX_UNITY;
                acc[i * 2] += s;
                acc[i * 2 + 1] += s;
            }
        } else {
            ring_pop(&m->movie, scratch, n);
        }

        /* Voices. */
        {
            int vi;
            for (vi = 0; vi < MIX_VOICES; vi++) {
                Voice *v = &m->v[vi];
                int bg, lv, rv, avail;
                int want, at;
                if (!v->active)
                    continue;
                bg = (v->bus == MIX_BUS_SPEECH) ? g_speech
                     : (v->bus == MIX_BUS_MUSIC) ? g_music
                     : (v->bus == MIX_BUS_MOVIE) ? g_movie
                                                 : g_fx;
                lv = (int)((long)v->lvol * bg / MIX_UNITY);
                rv = (int)((long)v->rvol * bg / MIX_UNITY);
                /* One pass per contiguous run of the clip. A one-shot has exactly one
                 * run (avail == the whole request, `break` on the last block), so this
                 * is byte identical to the old straight-line code; a looping voice
                 * takes a second pass from pos 0 and the seam lands on a SAMPLE
                 * boundary rather than the end of an output block. */
                want = n;
                at = 0;
                while (want > 0) {
                    avail = (int)(v->samples - v->pos);
                    if (avail > want)
                        avail = want;
                    if (bg > 0) {
                        for (i = 0; i < avail; i++) {
                            int s = v->pcm[v->pos + i];
                            acc[(at + i) * 2] += (s * lv) >> 12;
                            acc[(at + i) * 2 + 1] += (s * rv) >> 12;
                        }
                    }
                    v->pos += avail;
                    at += avail;
                    want -= avail;
                    if (v->pos >= v->samples) {
                        if (!v->loop) {
                            v->active = 0;
                            break;
                        }
                        v->pos = 0; /* the seam, sample accurate */
                    }
                }
            }
        }

        /* Master, soft clip, store. */
        for (i = 0; i < n * 2; i++) {
            int s = acc[i] * gm / MIX_UNITY;
            short o = soft_clip(m, s);
            int a = o < 0 ? -o : o;
            if (a > peak)
                peak = a;
            dst[(done * 2) + i] = o;
        }

        done += n;
    }

    m->last_peak = peak;
    m->frames_rendered += frames;
}

int mixer_last_peak(const Mixer *m) { return m ? m->last_peak : 0; }
long mixer_frames_rendered(const Mixer *m) { return m ? m->frames_rendered : 0; }

void mixer_clip_stats(const Mixer *m, long *knee, long *hard)
{
    if (knee)
        *knee = m ? m->knee_samples : 0;
    if (hard)
        *hard = m ? m->hard_samples : 0;
}
