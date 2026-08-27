/*
 * audtest.c -- decode one .AUD to a .WAV and report what came out.
 *
 *   ./audtest ../data/dosdata/music/AOI.AUD out.wav 30       loose file, 30 s
 *   ./audtest SOUNDS.MIX:NUYELL1.AUD out.wav                 straight from a MIX
 *
 * Peak and RMS catch the two ways an ADPCM port goes wrong: silence, and a predictor
 * that runs away and pins at the rails.
 */

#include "mixfile.h"
#include "wavio.h"
#include "wsaud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char err[256];
    AudStream *a = NULL;
    MixFile *mx = NULL;
    WavOut *w;
    short buf[16384];
    long total = 0, limit = 0;
    int n, loop = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s IN.AUD|MIXPATH:NAME OUT.WAV [seconds] [--loop]\n", argv[0]);
        return 2;
    }

    {
        char *colon = strrchr(argv[1], ':');
        if (colon && colon != argv[1] + 1) { /* not a Windows drive letter */
            char mixpath[512];
            size_t len = (size_t)(colon - argv[1]);
            if (len >= sizeof mixpath)
                len = sizeof mixpath - 1;
            memcpy(mixpath, argv[1], len);
            mixpath[len] = 0;
            mx = mixfile_open(mixpath, err, (int)sizeof err);
            if (!mx) {
                fprintf(stderr, "audtest: %s\n", err);
                return 1;
            }
            a = aud_open_mix(mx, colon + 1, err, (int)sizeof err);
        } else {
            a = aud_open_file(argv[1], err, (int)sizeof err);
        }
    }
    if (!a) {
        fprintf(stderr, "audtest: %s\n", err);
        return 1;
    }

    printf("%s: %d Hz, %d channel, %d bit, compression %d, %ld frames (%.2f s)\n", argv[1],
           aud_rate(a), aud_channels(a), aud_bits(a), aud_compression(a), aud_frames(a),
           (double)aud_frames(a) / aud_rate(a));

    if (argc > 3)
        limit = (long)(atof(argv[3]) * aud_rate(a)) * aud_channels(a);
    for (n = 4; n < argc; n++)
        if (strcmp(argv[n], "--loop") == 0)
            loop = 1;

    w = wav_open(argv[2], aud_rate(a), aud_channels(a));
    if (!w) {
        fprintf(stderr, "audtest: cannot write %s\n", argv[2]);
        return 1;
    }

    while ((n = aud_read(a, buf, (int)(sizeof buf / sizeof buf[0]), loop)) > 0) {
        wav_write(w, buf, n);
        total += n;
        if (limit && total >= limit)
            break;
    }

    printf("wrote %s: %ld samples (%.2f s), peak %d, rms %.1f\n", argv[2], wav_close(w),
           (double)total / aud_rate(a) / aud_channels(a), wav_peak(w), wav_rms(w));
    printf("  rms 0 means the decoder produced silence; peak pinned at 32767 across the\n"
           "  file means the predictor ran away.\n");

    free(w);
    aud_close(a);
    mixfile_close(mx);
    return 0;
}
