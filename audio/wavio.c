#include "wavio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct WavOut
{
    FILE *f;
    int rate, channels;
    long samples;
    int peak;
    double sumsq;
};

static void put32(FILE *f, unsigned int v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

static void put16(FILE *f, unsigned int v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

WavOut *wav_open(const char *path, int rate, int channels)
{
    WavOut *w = (WavOut *)calloc(1, sizeof(WavOut));
    if (!w)
        return NULL;
    w->f = fopen(path, "wb");
    if (!w->f) {
        free(w);
        return NULL;
    }
    w->rate = rate;
    w->channels = channels;

    fwrite("RIFF", 1, 4, w->f);
    put32(w->f, 0);
    fwrite("WAVEfmt ", 1, 8, w->f);
    put32(w->f, 16);
    put16(w->f, 1);
    put16(w->f, (unsigned)channels);
    put32(w->f, (unsigned)rate);
    put32(w->f, (unsigned)(rate * channels * 2));
    put16(w->f, (unsigned)(channels * 2));
    put16(w->f, 16);
    fwrite("data", 1, 4, w->f);
    put32(w->f, 0);
    return w;
}

void wav_write(WavOut *w, const short *pcm, long samples)
{
    long i;
    for (i = 0; i < samples; i++) {
        int a = pcm[i] < 0 ? -(int)pcm[i] : (int)pcm[i];
        if (a > w->peak)
            w->peak = a;
        w->sumsq += (double)pcm[i] * (double)pcm[i];
    }
    fwrite(pcm, sizeof(short), (size_t)samples, w->f);
    w->samples += samples;
}

long wav_close(WavOut *w)
{
    long n;
    if (!w)
        return 0;
    n = w->samples;
    fseek(w->f, 4, SEEK_SET);
    put32(w->f, (unsigned)(36 + n * 2));
    fseek(w->f, 40, SEEK_SET);
    put32(w->f, (unsigned)(n * 2));
    fclose(w->f);
    w->f = NULL;
    return n;
}

int wav_peak(const WavOut *w) { return w ? w->peak : 0; }

double wav_rms(const WavOut *w)
{
    if (!w || w->samples == 0)
        return 0.0;
    return sqrt(w->sumsq / (double)w->samples);
}
