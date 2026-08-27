/* wavio.h -- minimal RIFF/WAVE writer for the harnesses. Test code only; the engine
 * itself never touches a file except through mixfile.c. */

#ifndef WAVIO_H
#define WAVIO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WavOut WavOut;

WavOut *wav_open(const char *path, int rate, int channels);
void wav_write(WavOut *w, const short *pcm, long samples);
long wav_close(WavOut *w); /* returns total samples written */

/* Measured on the way past, so a harness never has to re-read what it just wrote. */
int wav_peak(const WavOut *w);
double wav_rms(const WavOut *w);

#ifdef __cplusplus
}
#endif

#endif /* WAVIO_H */
