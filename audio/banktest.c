/*
 * banktest.c -- decode EVERY sound the engine can ask for and measure what came out.
 *
 * This is the honesty harness. A silent buffer decodes without error, so "it played"
 * proves nothing; what proves something is a number per file. For each name in the
 * engine's own VocType / VoxType / theme tables it reports the header fields, the
 * decoded length, the peak, the RMS and the DC offset, then classifies:
 *
 *   OK        plausible speech, music or effect
 *   SILENT    RMS below 1.0, which is a dead decoder
 *   CLIPPED   parked at the rails for more than 64 consecutive samples, which is a
 *             saturated predictor. See the note above RAIL_RUN_LIMIT: a rail COUNT
 *             is the wrong test and flags legitimately hot sounds like BLEEP2.
 *   DC        mean far off zero, which is how a botched 8 bit conversion looks
 *   MISSING   not on this disc. Reported, never substituted.
 *
 *   ./banktest DOSDATA_DIR [--csv out.csv] [--dump DIR] [--music-seconds N]
 */

#include "cncaudio.h"
#include "sfxtable.h"
#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    long samples;
    int peak;
    double rms;
    double dc;
    long rails;
    long longest_rail_run;
} Stats;

static void measure(const short *pcm, long n, Stats *s)
{
    long i, rails = 0, run = 0, longest = 0;
    double sum = 0.0, sumsq = 0.0;
    int peak = 0;

    for (i = 0; i < n; i++) {
        int v = pcm[i];
        int a = v < 0 ? -v : v;
        if (a > peak)
            peak = a;
        if (v >= 32760 || v <= -32760) {
            rails++;
            run++;
            if (run > longest)
                longest = run;
        } else {
            run = 0;
        }
        sum += v;
        sumsq += (double)v * (double)v;
    }
    s->samples = n;
    s->peak = peak;
    s->rms = n ? sqrt(sumsq / (double)n) : 0.0;
    s->dc = n ? sum / (double)n : 0.0;
    s->rails = rails;
    s->longest_rail_run = longest;
}

/* What a runaway ADPCM predictor looks like: it does not just touch the rail on a
 * loud transient, it PARKS there, because the accumulator has saturated and every
 * further delta is clamped away. So the test is a long CONSECUTIVE run at the rail,
 * not a rail count. Measured on the real disc: BLEEP2.AUD, a deliberately hot 1.2 kHz
 * bleep, touches the rail 60 times in 5536 samples but its longest run is ONE sample.
 * A saturated decoder produces runs of hundreds. 64 samples is 2.9 ms and leaves an
 * enormous margin either way. */
#define RAIL_RUN_LIMIT 64

static const char *verdict(const Stats *s)
{
    if (s->samples == 0)
        return "EMPTY";
    if (s->rms < 1.0)
        return "SILENT";
    if (s->longest_rail_run > RAIL_RUN_LIMIT)
        return "CLIPPED";
    if (fabs(s->dc) > 2000.0)
        return "DC";
    return "OK";
}

static int n_ok = 0, n_silent = 0, n_clipped = 0, n_dc = 0, n_missing = 0, n_empty = 0;
static FILE *csv = NULL;
static const char *dumpdir = NULL;
static int dumped = 0;

static void audit(SndBank *bank, const char *kind, const char *name, long limit_samples)
{
    char err[256];
    AudStream *a;
    short *pcm = NULL;
    long n;
    Stats st;
    const char *v;

    a = bank_open_stream(bank, name, err, (int)sizeof err);
    if (!a) {
        printf("  %-8s %-14s MISSING  (%s)\n", kind, name, err);
        if (csv)
            fprintf(csv, "%s,%s,,,,,,,,,,MISSING\n", kind, name);
        n_missing++;
        return;
    }

    if (limit_samples > 0) {
        pcm = (short *)malloc((size_t)limit_samples * sizeof(short));
        n = pcm ? aud_read(a, pcm, (int)limit_samples, 0) : 0;
    } else {
        n = aud_decode_all(a, &pcm);
        if (n < 0)
            n = 0;
    }

    measure(pcm, n, &st);
    v = verdict(&st);

    printf("  %-8s %-14s %5d Hz %2dch %2db comp%-3d  %8ld smp %7.2fs  peak %6d  rms %8.1f  dc %8.1f  rail_run %4ld  %s\n",
           kind, name, aud_rate(a), aud_channels(a), aud_bits(a), aud_compression(a), st.samples,
           (double)st.samples / (double)(aud_rate(a) * aud_channels(a)), st.peak, st.rms, st.dc,
           st.longest_rail_run, v);

    if (csv)
        fprintf(csv, "%s,%s,%d,%d,%d,%d,%ld,%.4f,%d,%.2f,%.2f,%ld,%s\n", kind, name, aud_rate(a),
                aud_channels(a), aud_bits(a), aud_compression(a), st.samples,
                (double)st.samples / (double)(aud_rate(a) * aud_channels(a)), st.peak, st.rms,
                st.dc, st.longest_rail_run, v);

    if (strcmp(v, "OK") == 0)
        n_ok++;
    else if (strcmp(v, "SILENT") == 0)
        n_silent++;
    else if (strcmp(v, "CLIPPED") == 0)
        n_clipped++;
    else if (strcmp(v, "DC") == 0)
        n_dc++;
    else
        n_empty++;

    if (dumpdir && n > 0) {
        char path[600];
        WavOut *w;
        snprintf(path, sizeof path, "%s/%s.wav", dumpdir, name);
        w = wav_open(path, aud_rate(a), aud_channels(a));
        if (w) {
            wav_write(w, pcm, n);
            wav_close(w);
            free(w);
            dumped++;
        }
    }

    free(pcm);
    aud_close(a);
}

int main(int argc, char **argv)
{
    char err[256];
    CncAudio *au;
    SndBank *bank;
    const char *dir = argc > 1 ? argv[1] : "data/dosdata";
    double music_seconds = 6.0;
    int i, e;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
            csv = fopen(argv[++i], "w");
        else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
            dumpdir = argv[++i];
        else if (strcmp(argv[i], "--music-seconds") == 0 && i + 1 < argc)
            music_seconds = atof(argv[++i]);
    }

    au = cnc_audio_create(dir, err, (int)sizeof err);
    if (!au) {
        fprintf(stderr, "banktest: %s\n", err);
        return 1;
    }
    bank = cnc_audio_bank(au);

    if (csv)
        fprintf(csv, "kind,name,rate,channels,bits,comp,samples,seconds,peak,rms,dc,rail_run,verdict\n");

    printf("== sound effects (VocType, %d entries) ==\n", SFX_VOC_COUNT);
    for (i = 0; i < SFX_VOC_COUNT; i++) {
        char name[32];
        if (sfx_voc[i].where == SFX_VAR) {
            /* Only the variants the engine can actually ask for. The sign of the
             * variation picks the pair (infantry positive, vehicles negative), and
             * a line only used by infantry has no .V00/.V02 on the disc because the
             * 1995 game never looked for one. Auditing all four invents holes. */
            if (sfx_voc[i].varuse == 0) {
                printf("  %-8s %-14s UNUSED   (no response table names it)\n", "VOC",
                       sfx_voc[i].name);
                continue;
            }
            if (sfx_voc[i].varuse & 1) {
                static const int pos[2] = {1, 2};
                for (e = 0; e < 2; e++) {
                    sfx_voc_filename(i, pos[e], 0, name, (int)sizeof name);
                    audit(bank, "VOC", name, 0);
                }
            }
            if (sfx_voc[i].varuse & 2) {
                static const int neg[2] = {-1, -2};
                for (e = 0; e < 2; e++) {
                    sfx_voc_filename(i, neg[e], 0, name, (int)sizeof name);
                    audit(bank, "VOC", name, 0);
                }
            }
        } else {
            sfx_voc_filename(i, 0, 0, name, (int)sizeof name);
            audit(bank, "VOC", name, 0);
            if (sfx_voc[i].where == SFX_JUV) {
                sfx_voc_filename(i, 0, 1, name, (int)sizeof name);
                audit(bank, "JUV", name, 0);
            }
        }
    }

    printf("\n== speech (VoxType, %d entries) ==\n", SFX_VOX_COUNT);
    for (i = 0; i < SFX_VOX_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof name, "%s.AUD", sfx_vox[i]);
        audit(bank, "VOX", name, 0);
    }

    printf("\n== music (themes, %d entries, first %.1f s each) ==\n", SFX_THEME_COUNT,
           music_seconds);
    for (i = 0; i < SFX_THEME_COUNT; i++) {
        char name[40];
        snprintf(name, sizeof name, "%s.AUD", sfx_theme[i].name);
        audit(bank, "THEME", name, (long)(music_seconds * 22050.0));
        if (sfx_theme[i].variation) {
            snprintf(name, sizeof name, "%s.VAR", sfx_theme[i].name);
            audit(bank, "THEMEVAR", name, (long)(music_seconds * 22050.0));
        }
    }

    /* The bank resamples everything to BANK_RATE at load. Four files on the disc are
     * 22222 Hz and one is 44100 Hz, so this path runs in the real game and has to be
     * checked, not assumed. Expected length is samples * BANK_RATE / src_rate. */
    printf("\n== resampling to %d Hz (the files that are not already 22050) ==\n", BANK_RATE);
    printf("  %-16s %7s %9s %9s %9s  %8s\n", "name", "src Hz", "src smp", "got smp", "want smp",
           "rms");
    {
        static const char *odd[] = {"MGUN2.AUD",    "TONE2.AUD", "TRANS1.AUD",
                                    "TNKFIRE2.JUV", "YELL1.AUD", "CONSTRU1.AUD", NULL};
        int k;
        for (k = 0; odd[k]; k++) {
            char err2[256];
            AudStream *a2 = bank_open_stream(bank, odd[k], err2, (int)sizeof err2);
            long srcn = 0, want;
            const SndClip *c;
            int srate;
            if (!a2)
                continue;
            srate = aud_rate(a2);
            {
                short *tmp = NULL;
                srcn = aud_decode_all(a2, &tmp);
                free(tmp);
            }
            aud_close(a2);
            c = bank_get(bank, odd[k]);
            if (!c)
                continue;
            want = (long)((double)srcn * (double)BANK_RATE / (double)srate + 0.5);
            {
                Stats rs;
                measure(c->pcm, c->samples, &rs);
                printf("  %-16s %7d %9ld %9ld %9ld  %8.1f  %s\n", odd[k], srate, srcn, c->samples,
                       want, rs.rms,
                       (c->samples >= want - 2 && c->samples <= want + 2) ? "length ok" : "LENGTH WRONG");
            }
            if (dumpdir) {
                char path2[600];
                WavOut *w2;
                snprintf(path2, sizeof path2, "%s/%s.resampled.wav", dumpdir, odd[k]);
                w2 = wav_open(path2, BANK_RATE, 1);
                if (w2) {
                    wav_write(w2, c->pcm, c->samples);
                    wav_close(w2);
                    free(w2);
                }
            }
        }
    }

    printf("\n== summary ==\n");
    printf("  OK %d   SILENT %d   CLIPPED %d   DC %d   EMPTY %d   MISSING %d\n", n_ok, n_silent,
           n_clipped, n_dc, n_empty, n_missing);
    if (dumpdir)
        printf("  wrote %d WAVs to %s\n", dumped, dumpdir);
    printf("  bank misses recorded: %d\n", bank_miss_count(bank));
    for (i = 0; i < bank_miss_count(bank); i++)
        printf("    no DOS sound for %s -> silence\n", bank_miss(bank, i));

    if (csv)
        fclose(csv);
    cnc_audio_destroy(au);

    /* A silent or clipped file is a decoder bug and must fail the harness.
     * A missing file is a data fact, not a bug: report and pass. */
    return (n_silent || n_clipped || n_dc || n_empty) ? 2 : 0;
}
