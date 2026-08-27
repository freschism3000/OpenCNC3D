/*
 * mixtest.c -- render a scripted scene through the whole engine to a stereo WAV,
 * with no sound device involved, and print numbers for every second of it.
 *
 * The scene is chosen so each failure mode is visible in the waveform:
 *
 *   0.0 s  music starts (AOI from SCORES.MIX, streamed) and ramps up over 1 s
 *   2.0 s  EVA speaks. Speech is on its own bus.
 *   3.0 s  six tank shots all to the LEFT of the view: rms L must exceed rms R
 *   4.0 s  the same six mirrored to the RIGHT: rms R must exceed rms L
 *   5.0 s  a second EVA line interrupts the first, which must cut, not overlap
 *   6.0 s  music fades to zero over 1 s: the following second must measure near
 *          silence, which is the fade proving itself
 *   8.0 s  music back up
 *   8.5 s  sixteen shots at once against 32 voices, to exercise priority stealing
 *
 * It also prints the distance and pan table from mixer_effect_place() up front, so
 * the falloff curve is a readable list of numbers rather than an assertion.
 *
 *   ./mixtest DOSDATA_DIR OUT.WAV
 */

#include "cncaudio.h"
#include "sfxtable.h"
#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECONDS 10
#define BLOCK 512 /* frames per render call: 23 ms, like a real callback */

static int voc_index(const char *base)
{
    int i;
    for (i = 0; i < SFX_VOC_COUNT; i++)
        if (strcmp(sfx_voc[i].name, base) == 0)
            return i;
    return -1;
}

static int vox_index(const char *base)
{
    int i;
    for (i = 0; i < SFX_VOX_COUNT; i++)
        if (strcmp(sfx_vox[i], base) == 0)
            return i;
    return -1;
}

int main(int argc, char **argv)
{
    char err[256];
    CncAudio *au;
    Mixer *mx;
    WavOut *w;
    const char *dir = argc > 1 ? argv[1] : "data/dosdata";
    const char *out = argc > 2 ? argv[2] : "mixtest.wav";
    short buf[BLOCK * 2];
    long frame = 0, total = (long)SECONDS * MIX_RATE;
    int sec_done = -1;
    double sec_l = 0.0, sec_r = 0.0;
    long sec_n = 0;
    int peak_l = 0, peak_r = 0;
    int tank, eva1, eva2;

    au = cnc_audio_create(dir, err, (int)sizeof err);
    if (!au) {
        fprintf(stderr, "mixtest: %s\n", err);
        return 1;
    }
    mx = cnc_audio_mixer(au);
    audio_backend_open(au, err, (int)sizeof err); /* null backend: no device */

    w = wav_open(out, MIX_RATE, 2);
    if (!w) {
        fprintf(stderr, "mixtest: cannot write %s\n", out);
        return 1;
    }

    tank = voc_index("TNKFIRE3"); /* VOC_TANK2, "sharp tank fire" */
    eva1 = vox_index("CONSTRU1");
    eva2 = vox_index("UNITREDY");
    printf("mixtest: backend=%s  tank voc=%d (%s)  eva=%d,%d\n", audio_backend_name(), tank,
           tank >= 0 ? sfx_voc[tank].name : "-", eva1, eva2);

    /* The tactical placement curve, printed rather than asserted. Listener at
     * (2000, 2000) with a 640x400 view, so |dx| <= 320 is on screen. */
    printf("\n  mixer_effect_place(): listener (2000,2000), view 640x400\n");
    printf("    world x    dx     gain   pan    note\n");
    {
        static const int probe[] = {2000, 2200, 2320, 2400, 2600, 3000, 3500, 4000, 6000,
                                    1800, 1400, 1000, 500,  -1};
        int k, g = 0, pn = 0;
        for (k = 0; probe[k] != -1; k++) {
            mixer_effect_place(2000, 2000, 640, 400, probe[k], 2000, &g, &pn);
            printf("    %7d %6d   %5d %5d    %s\n", probe[k], probe[k] - 2000, g, pn,
                   (probe[k] - 2000 >= -320 && probe[k] - 2000 <= 320) ? "in view" : "off screen");
        }
        mixer_effect_place(2000, 2000, 640, 400, -1, -1, &g, &pn);
        printf("    %7s %6s   %5d %5d    no coordinate (UI sound)\n", "-1", "-", g, pn);
    }

    /* A 640x400 view centred on world pixel (2000, 2000): the same units the sound
     * callback's PixelX / PixelY arrive in. */
    cnc_audio_set_listener(au, 2000, 2000, 640, 400);

    if (!cnc_music_play_theme(au, "AOI", 1))
        printf("  (no AOI in SCORES.MIX: the music bed will be silent)\n");
    else
        printf("  music: %s\n", cnc_music_current(au));
    mixer_bus_gain(mx, MIX_BUS_MUSIC, 0, 0);
    mixer_bus_gain(mx, MIX_BUS_MUSIC, 700, 1000); /* ramp up over the first second */
    cnc_audio_update(au);

    printf("\n  sec   rms L    rms R   peakL  peakR  voices  music_ring  events\n");

    while (frame < total) {
        int n = BLOCK;
        int i;
        double t = (double)frame / (double)MIX_RATE;
        char events[160];
        events[0] = 0;

        if (frame + n > total)
            n = (int)(total - frame);

        /* ---- scripted events, fired on the frame they are due ---- */
        {
            long f0 = frame, f1 = frame + n;
#define AT(sec) (f0 <= (long)((sec) * MIX_RATE) && (long)((sec) * MIX_RATE) < f1)
            if (AT(2.0) && eva1 >= 0) {
                cnc_audio_on_speech(au, eva1);
                strcat(events, "EVA:CONSTRU1 ");
            }
            if (AT(3.0) && tank >= 0) {
                static const int xs[6] = {1600, 1400, 1200, 1000, 800, 600};
                for (i = 0; i < 6; i++)
                    cnc_audio_on_sound_effect(au, tank, 0, xs[i], 2000);
                strcat(events, "6xTANK LEFT ");
            }
            if (AT(4.0) && tank >= 0) {
                static const int xs[6] = {2400, 2600, 2800, 3000, 3200, 3400};
                for (i = 0; i < 6; i++)
                    cnc_audio_on_sound_effect(au, tank, 0, xs[i], 2000);
                strcat(events, "6xTANK RIGHT ");
            }
            if (AT(5.0) && eva2 >= 0) {
                cnc_audio_on_speech(au, eva2);
                strcat(events, "EVA:UNITREDY(cuts) ");
            }
            if (AT(6.0)) {
                cnc_music_fade_out(au, 1000);
                strcat(events, "music fade->0 ");
            }
            if (AT(8.0)) {
                mixer_bus_gain(mx, MIX_BUS_MUSIC, 700, 500);
                strcat(events, "music back ");
            }
            if (AT(8.5) && tank >= 0) {
                for (i = 0; i < 16; i++)
                    cnc_audio_on_sound_effect(au, tank, 0, 2000 + i * 90, 2000);
                strcat(events, "16xTANK(steal) ");
            }
#undef AT
        }

        cnc_audio_update(au);
        mixer_render(mx, buf, n);
        wav_write(w, buf, (long)n * 2);

        for (i = 0; i < n; i++) {
            int l = buf[i * 2], r = buf[i * 2 + 1];
            int al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            sec_l += (double)l * l;
            sec_r += (double)r * r;
            if (al > peak_l)
                peak_l = al;
            if (ar > peak_r)
                peak_r = ar;
            sec_n++;
        }

        frame += n;

        if ((int)t != sec_done) {
            if (sec_done >= 0 && sec_n) {
                printf("  %3d  %8.1f %8.1f  %6d %6d   %4d   %8d   %s\n", sec_done,
                       sqrt(sec_l / sec_n), sqrt(sec_r / sec_n), peak_l, peak_r,
                       mixer_active_voices(mx), mixer_music_level(mx), events);
            }
            sec_done = (int)t;
            sec_l = sec_r = 0.0;
            sec_n = 0;
            peak_l = peak_r = 0;
        } else if (events[0]) {
            printf("       %51s%s\n", "", events);
        }
    }

    if (sec_n)
        printf("  %3d  %8.1f %8.1f  %6d %6d   %4d   %8d\n", sec_done, sqrt(sec_l / sec_n),
               sqrt(sec_r / sec_n), peak_l, peak_r, mixer_active_voices(mx),
               mixer_music_level(mx));

    printf("\n  wrote %s: %ld samples (%.2f s stereo), peak %d, rms %.1f\n", out, wav_close(w),
           (double)mixer_frames_rendered(mx) / MIX_RATE, wav_peak(w), wav_rms(w));
    printf("  NOTE: the 8.5 s burst deliberately overdrives (16 shots at once with the\n"
           "  FX bus at unity) to exercise voice stealing, so the peak pins. In play the\n"
           "  options slider puts the FX bus below unity and the tactical curve pulls the\n"
           "  off screen shots down; see the placement table above.\n");

    free(w);
    audio_backend_close();
    cnc_audio_destroy(au);
    return 0;
}
