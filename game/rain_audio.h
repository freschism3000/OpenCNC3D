/* ====================================================================================
 *  rain_audio.h -- THE SOUND OF THE RAIN, and it is the one sound in this game that
 *                  did not come off a disc.
 *
 *  EVERY OTHER SOUND HERE IS THE 1995 GAME'S OR THE CARTRIDGE'S. That rule is what
 *  makes the audio worth anything, and it is not being relaxed generally: it is being
 *  set aside for WEATHER, deliberately and once, on the same footing as the sun, the
 *  clouds, the sea and the rain itself, none of which are decoded from anything either.
 *  There is no rain clip on the N64 cartridge or on the 1995 CD, because neither game
 *  has weather. So the choice was between rain that falls in silence and rain that
 *  makes a sound nobody recorded, and the second was taken.
 *
 *  IT IS SYNTHESISED AT BOOT, NOT SHIPPED AS A FILE. Four seconds of mono 22050 Hz,
 *  176 KB, built the way the cloud mask and the sea's foam sheet are: from a fixed
 *  generator, in code, so there is no asset to license, no file to lose, and the same
 *  bytes on every machine. Two parts, which is what rain is:
 *
 *    THE HISS. Band-limited noise that breathes slowly. Rain over a whole map is
 *    thousands of impacts a second and the ear hears their sum as noise, not as events.
 *
 *    THE NEAR DROPS. A few hundred short decaying ticks scattered through the loop: the
 *    handful of impacts close enough to be heard one at a time. Without them the hiss is
 *    a radio between stations; with them it is rain.
 *
 *  THE LOOP HAS NO SEAM, and the join is MEASURED rather than asserted: the build prints
 *  the sample step across the wrap beside the number of steps INSIDE the loop that are at
 *  least as big, and a gate reads that line. On this loop the join is 19433 against a mean
 *  step of 4747, with 493 other steps as big or bigger: the 99.4th percentile, and 493
 *  places nobody hears are worse.
 *
 *  Every filter here runs CIRCULARLY, one warm-up lap round the buffer before the lap that
 *  counts, so the state at sample 0 is the state arriving from sample N-1, and drops that
 *  begin near the end wrap into the beginning rather than being cut. That is correct and
 *  it stays. What is NOT true, and was assumed until it was checked, is that it is what
 *  makes the loop seamless: with the warm-up lap removed the join measures 25038 with 91
 *  steps still as big, which is not a click either. The loop is seamless mostly because of
 *  what it is. Broadband noise has no phase to break across a join, the short filters
 *  settle in two samples, and the only long-memory one is a slow envelope multiplying by
 *  0.8 to 1.0. The circular passes remove a real drift; they are not load-bearing.
 *
 *  IT IS TIER 2 ONLY, like the rest of the rain, and for a reason that is not the usual
 *  one. Nothing here needs a shader: it is C, integers and a noise generator, and
 *  mixer_play_loop is plain C that a Win98 backend inherits unchanged. It is absent on
 *  Tier 1 only because the rain that would justify it is.
 * ==================================================================================== */
#ifndef CNC3D_RAIN_AUDIO_H
#define CNC3D_RAIN_AUDIO_H

#include "../audio/cncaudio.h"

#define RAIN_AUD_SECS  4
#define RAIN_AUD_LEN   (MIX_RATE * RAIN_AUD_SECS)   /* 88200 mono samples       */
#define RAIN_AUD_DROPS 150                          /* near impacts in the loop */

static short* g_rainAudPcm = NULL;   /* the loop; owned here, never freed while playing */
static int    g_rainAudVoice = -1;   /* the looping voice, or -1                        */
static int    g_rainAudGain = -1;    /* last gain handed to the mixer, to avoid churn   */
static int    g_rainAudSaid = 0;

/* The generator. A fixed LCG, never rand(): the loop must be the same bytes in every
   run and on every build, because an audio gate compares recordings. */
static unsigned g_rainAudLcg = 0x13579BDFu;
static float rain_aud_noise(void)
{
    g_rainAudLcg = g_rainAudLcg * 1664525u + 1013904223u;
    return (float)(int)(g_rainAudLcg >> 8) * (1.0f / 8388608.0f) - 1.0f;   /* -1 .. 1 */
}

/* One pole, run round the buffer twice: a warm-up lap whose output is thrown away, then
   the lap that counts. That is what makes the result exactly periodic. */
static void rain_aud_lowpass(float* b, int n, float a)
{
    float y = b[n - 1];
    int i;
    for (i = 0; i < n; i++) y += a * (b[i] - y);          /* warm-up, output discarded */
    for (i = 0; i < n; i++) { y += a * (b[i] - y); b[i] = y; }
}

static void rain_audio_build(void)
{
    float* hiss;
    float* low;
    float* env;
    int i, d;
    float peak = 0.0f;

    if (g_rainAudPcm) return;
    g_rainAudPcm = (short*)malloc(sizeof(short) * RAIN_AUD_LEN);
    hiss = (float*)malloc(sizeof(float) * RAIN_AUD_LEN);
    low  = (float*)malloc(sizeof(float) * RAIN_AUD_LEN);
    env  = (float*)malloc(sizeof(float) * RAIN_AUD_LEN);
    if (!g_rainAudPcm || !hiss || !low || !env) {
        free(hiss); free(low); free(env);
        free(g_rainAudPcm); g_rainAudPcm = NULL;
        fprintf(stderr, "RAIN|sound|could not allocate the loop; the rain stays silent\n");
        return;
    }

    g_rainAudLcg = 0x13579BDFu;
    for (i = 0; i < RAIN_AUD_LEN; i++) hiss[i] = rain_aud_noise();
    /* THE BAND, AND IT IS DARKER THAN THE FIRST ONE WAS. Rain heard outdoors, falling
       on soil and grass and a few metres of air, is a wash with body in it. The first
       cut of this ran the top out to about 3.4 kHz on a single pole, which leaves a
       fifth of the energy above 8 kHz, and that reads as rain on a ROOF: hard, bright,
       close, hitting something that rings. Two poles now instead of one, so the top
       comes down steeply rather than trailing to Nyquist, and the low corner is dropped
       so there is more of the 300 Hz to 1.5 kHz that makes it a wash and not a hiss. */
    memcpy(low, hiss, sizeof(float) * RAIN_AUD_LEN);
    rain_aud_lowpass(hiss, RAIN_AUD_LEN, 0.60f);
    rain_aud_lowpass(hiss, RAIN_AUD_LEN, 0.60f);
    rain_aud_lowpass(low,  RAIN_AUD_LEN, 0.045f);
    for (i = 0; i < RAIN_AUD_LEN; i++) hiss[i] -= low[i];

    /* the breath: a very slow envelope, so the hiss swells and eases instead of sitting
       at one level, which is what gives a synthetic loop away in the first ten seconds */
    for (i = 0; i < RAIN_AUD_LEN; i++) env[i] = rain_aud_noise();
    rain_aud_lowpass(env, RAIN_AUD_LEN, 0.0009f);
    {
        float lo = env[0], hi = env[0];
        for (i = 1; i < RAIN_AUD_LEN; i++) {
            if (env[i] < lo) lo = env[i];
            if (env[i] > hi) hi = env[i];
        }
        for (i = 0; i < RAIN_AUD_LEN; i++)
            env[i] = 0.88f + 0.12f * ((hi > lo) ? (env[i] - lo) / (hi - lo) : 0.5f);
    }
    for (i = 0; i < RAIN_AUD_LEN; i++) hiss[i] *= env[i];

    /* THE NEAR DROPS, AND THEY ARE THUDS RATHER THAN TICKS. A drop landing on soil or
       on a leaf is a soft, short knock; a drop landing on a tin roof is a click, and a
       few hundred clicks a second is exactly what a roof sounds like, which is what the
       first cut of this was. So there are half as many, they are a third as loud, they
       last two to three times longer, and each one rises over a few milliseconds rather
       than starting at full height. One that begins near the end of the loop WRAPS into
       the beginning, or the join would be audible as a hole in them. */
    for (d = 0; d < RAIN_AUD_DROPS; d++) {
        const int   at  = (int)((rain_aud_noise() * 0.5f + 0.5f) * (float)RAIN_AUD_LEN);
        const int   len = 420 + (int)((rain_aud_noise() * 0.5f + 0.5f) * 900.0f);
        const float amp = 0.045f + 0.115f * (rain_aud_noise() * 0.5f + 0.5f);
        const float dk  = 3.2f / (float)len;
        const int   ris = len / 8;
        float a = amp;
        for (i = 0; i < len; i++) {
            const float rise = (i < ris) ? ((float)i / (float)ris) : 1.0f;
            hiss[(at + i) % RAIN_AUD_LEN] += rain_aud_noise() * a * rise;
            a -= a * dk;
        }
    }
    /* one more pass over the lot, which rounds the drops' own edges as well as the
       hiss: what is wanted is rain a few metres off, not rain on the microphone. Gentle,
       because three poles took it too far the other way and left a rumble with no rain
       in it: measured, 60% of the energy under 1.5 kHz and 6% above 4 kHz. */
    rain_aud_lowpass(hiss, RAIN_AUD_LEN, 0.75f);

    /* NORMALISE TO THE STEADY LEVEL, NOT TO THE LOUDEST TRANSIENT, and that is what
       makes this a rain rather than a squall. Scaling so the single biggest drop lands at
       full scale pushes the HISS down by however far that drop happened to overshoot, so
       the bed goes quiet and the drops stand proud of it: the ear hears spatter, not
       rain. Scaling on the root mean square instead keeps the wash at a known level and
       lets the drops sit where they fall, and the few that would clip are rounded off by
       a soft knee rather than a hard edge, which would put a click on each one. */
    {
        double sum = 0.0;
        float rms, sc;
        for (i = 0; i < RAIN_AUD_LEN; i++) sum += (double)hiss[i] * (double)hiss[i];
        rms = (float)sqrt(sum / (double)RAIN_AUD_LEN);
        sc = (rms > 1e-6f) ? (4200.0f / rms) : 0.0f;
        for (i = 0; i < RAIN_AUD_LEN; i++) {
            float v = hiss[i] * sc;
            const float a = v < 0.0f ? -v : v;
            if (a > 12000.0f) {                  /* the soft knee */
                const float over = (a - 12000.0f) / 20000.0f;
                const float k = 12000.0f + 20000.0f * (over / (1.0f + over));
                v = (v < 0.0f) ? -k : k;
            }
            if (v >  32767.0f) v =  32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            g_rainAudPcm[i] = (short)v;
        }
        if (peak < 0.0f) peak = 0.0f;            /* kept: the loudest raw sample, unused */
    }
    free(hiss); free(low); free(env);

    /* THE JOIN, MEASURED AND PRINTED, because "it loops seamlessly" is the kind of claim
       that is only ever checked by ear and only by whoever wrote it. What is printed is
       the step across the wrap, the average step inside the loop, and the NUMBER of steps
       inside that are at least as big. The mean alone is the wrong question for a noise
       bed: adjacent samples of broadband noise are nearly independent, so any single step
       runs several times the mean and the join is not special for being one of them. What
       would make it a click is being an OUTLIER, and that is what the rank says. A loop
       filtered straight down the buffer instead of round it puts the join at the top. */
    {
        long step = 0;
        int k, over = 0;
        const int seam = abs((int)g_rainAudPcm[0] - (int)g_rainAudPcm[RAIN_AUD_LEN - 1]);
        for (k = 1; k < RAIN_AUD_LEN; k++) {
            const int s2 = abs((int)g_rainAudPcm[k] - (int)g_rainAudPcm[k - 1]);
            step += s2;
            if (s2 >= seam) over++;
        }
        fprintf(stderr,
                "RAIN|sound|loop|samples=%d|rate=%d|drops=%d|seam=%d|meanstep=%ld|over=%d\n",
                RAIN_AUD_LEN, MIX_RATE, RAIN_AUD_DROPS,
                seam, step / (RAIN_AUD_LEN - 1), over);
    }
}

static void rain_audio_stop(void)
{
    if (g_rainAudVoice >= 0 && g_au)
        mixer_voice_stop(cnc_audio_mixer(g_au), g_rainAudVoice);
    g_rainAudVoice = -1;
    g_rainAudGain = -1;
}

/* Once a frame, from the same place the rain is drawn. A looping voice never retires by
   itself and holds its slot, so it is stopped the moment the rain is not live: that one
   test covers the dial, CLASSIC, a chain that failed to build and a paused game, all of
   which take rain_live() down. */
static void rain_audio_update(void)
{
    const int live = rain_live() && g_fx.rain_sound > 0.0f;
    int gain;

    if (!g_au) { g_rainAudVoice = -1; return; }
    if (!live) { rain_audio_stop(); return; }

    rain_audio_build();
    if (!g_rainAudPcm) return;

    /* the master intensity carries the loudness the way it carries everything else, so a
       drizzle is quieter than a downpour without a second dial to keep in step */
    gain = (int)(1000.0f * g_fx.rain_sound * (0.35f + 0.65f * g_fx.rain_amount) + 0.5f);
    if (gain < 0) gain = 0;
    if (gain > MIX_GAIN_MAX) gain = MIX_GAIN_MAX;

    if (g_rainAudVoice < 0 || !mixer_voice_active(cnc_audio_mixer(g_au), g_rainAudVoice)) {
        /* priority 255: a bed stolen by a passing rifle shot and never coming back is
           worse than no bed, and this voice is stopped by hand on every exit anyway */
        g_rainAudVoice = mixer_play_loop(cnc_audio_mixer(g_au), MIX_BUS_FX,
                                         g_rainAudPcm, RAIN_AUD_LEN, gain, 0, 255);
        g_rainAudGain = gain;
        if (!g_rainAudSaid) {
            g_rainAudSaid = 1;
            fprintf(stderr, "RAIN|sound|bed %s\n",
                    g_rainAudVoice >= 0 ? "playing" : "REFUSED");
        }
        return;
    }
    if (gain != g_rainAudGain) {
        mixer_voice_gain(cnc_audio_mixer(g_au), g_rainAudVoice, gain, 0);
        g_rainAudGain = gain;
    }
}

#endif /* CNC3D_RAIN_AUDIO_H */
