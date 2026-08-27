#include "audiotap.h"
#include "wavio.h"

#include <stdio.h>
#include <stdlib.h>

#define TAP_BLOCK 1024 /* stereo frames per mixer_render call */

static WavOut *g_wav = NULL;
static long g_frames = 0;
static long g_carry_num = 0; /* fractional milliseconds, kept exact in integers */
static short g_buf[TAP_BLOCK * 2];

int tap_open(const char *path)
{
    if (g_wav)
        tap_close();
    g_wav = wav_open(path, MIX_RATE, 2);
    if (!g_wav) {
        fprintf(stderr, "audiotap: cannot write %s\n", path);
        return 0;
    }
    g_frames = 0;
    g_carry_num = 0;
    fprintf(stderr, "audiotap: recording the mix to %s (%d Hz stereo)\n", path, MIX_RATE);
    return 1;
}

int tap_active(void) { return g_wav != NULL; }

void tap_pump(CncAudio *au, int ms)
{
    long want;

    if (!g_wav || ms <= 0 || !au)
        return;

    /* MIX_RATE * ms / 1000 with the remainder carried, so 15 ticks a second at
       66 ms each come to exactly MIX_RATE frames after a second and the recording
       does not drift away from the sim clock. */
    g_carry_num += (long)MIX_RATE * ms;
    want = g_carry_num / 1000;
    g_carry_num -= want * 1000;

    while (want > 0) {
        int n = want > TAP_BLOCK ? TAP_BLOCK : (int)want;
        /* The game thread owns the mixer here: nothing else is rendering, because
           no device was opened when the tap is in use. */
        if (au)
            cnc_audio_update(au);
        mixer_render(cnc_audio_mixer(au), g_buf, n);
        wav_write(g_wav, g_buf, (long)n * 2);
        g_frames += n;
        want -= n;
    }
}

long tap_samples(void) { return g_frames; }
int tap_peak(void) { return g_wav ? wav_peak(g_wav) : 0; }
double tap_rms(void) { return g_wav ? wav_rms(g_wav) : 0.0; }

void tap_close(void)
{
    if (!g_wav)
        return;
    fprintf(stderr, "audiotap: %ld frames (%.2f s), peak %d, rms %.1f\n", g_frames,
            (double)g_frames / MIX_RATE, wav_peak(g_wav), wav_rms(g_wav));
    printf("AUDIOTAP|frames=%ld|seconds=%.2f|peak=%d|rms=%.1f\n", g_frames,
           (double)g_frames / MIX_RATE, wav_peak(g_wav), wav_rms(g_wav));
    fflush(stdout);
    wav_close(g_wav);
    g_wav = NULL;
}
