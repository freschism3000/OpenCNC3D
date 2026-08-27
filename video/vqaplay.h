/*
 * vqaplay.h -- a streaming player for Westwood VQA version 2, the format every
 * Command & Conquer movie is in (LOGO.VQA, INTRO2.VQA, the mission briefings).
 *
 * Ported from the GPL Tiberian Dawn sources, not reverse engineered:
 *   common/vqafile.h      VQAHeader, 42 bytes, little endian inside a big endian IFF
 *   common/vqaloader.cpp  chunk handling, and the CBP0 rule: partial codebook parts
 *                         accumulate and the group becomes the codebook after
 *                         Header.Groupsize of them
 *   common/unvqbuff.cpp   UnVQ_4x2: two pointer planes, and pointer2 == 15 means
 *                         "fill the whole block with colour pointer1"
 *   common/vqapalette.cpp VQA_SetPalette: mask every byte to 6 bits, then
 *                         Increase_Palette_Luminance(15, 15, 15, cap 63)
 *   common/soscodec.cpp   SND2 is SOS 4:1 ADPCM, one byte in, two 16 bit samples out
 *
 * It streams: only one frame is in memory at a time, which matters because INTRO2.VQA
 * is 21 MB and the Windows 98 tier has to play it on a machine that cannot hold it.
 *
 * No SDL, no GL, no threads. The caller gets an 8-bit frame plus a palette, and PCM
 * bytes it can hand to whatever audio device it has.
 */

#ifndef VQAPLAY_H
#define VQAPLAY_H

#include <stdio.h>

typedef struct VQ_Movie VQ_Movie;

/* Opens and reads the header. Returns NULL and fills err on failure. */
VQ_Movie *vq_open(const char *path, char *err, int errlen);
void vq_close(VQ_Movie *m);

int vq_width(const VQ_Movie *m);
int vq_height(const VQ_Movie *m);
int vq_frames(const VQ_Movie *m);   /* as claimed by the header */
int vq_fps(const VQ_Movie *m);
int vq_sample_rate(const VQ_Movie *m);
int vq_channels(const VQ_Movie *m);

/* Decodes the next frame. Returns 1 on success, 0 at end of stream, -1 on error.
 * Audio that arrived alongside it is decoded into the movie's PCM queue. */
int vq_next_frame(VQ_Movie *m);

/* The current frame: w*h palette indices, and 768 bytes of 8-bit RGB. */
const unsigned char *vq_pixels(const VQ_Movie *m);
const unsigned char *vq_palette(const VQ_Movie *m);

/* True when the palette changed on the frame just decoded. */
int vq_palette_dirty(const VQ_Movie *m);

/* Moves up to `max` bytes of decoded 16-bit PCM out of the queue. Returns the count. */
int vq_take_audio(VQ_Movie *m, void *dst, int max);
int vq_audio_pending(const VQ_Movie *m);

#endif /* VQAPLAY_H */
