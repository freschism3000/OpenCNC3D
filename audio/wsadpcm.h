/*
 * wsadpcm.h -- the older Westwood ADPCM, ported from common/auduncmp.cpp
 * (Audio_Unzap). This is AUD compression type 1.
 *
 * Why it exists: SOUNDS.MIX on the 1995 disc carries ten type 1 files, and they are
 * not decoration. They are the infantry death screams:
 *
 *   NUYELL1 NUYELL3 NUYELL4 NUYELL5 NUYELL6 NUYELL7 NUYELL10 NUYELL11 NUYELL12 YELL1
 *
 * which is VOC_SCREAM1..12 and VOC_YELL1. A decoder that only speaks type 99 plays
 * every infantry death as silence.
 *
 * Type 1 output is UNSIGNED 8-bit, and the predictor restarts at 0x80 for every
 * chunk, because soundio_common.cpp calls Audio_Unzap once per chunk with a fresh
 * local `sample`. Do not carry state across chunks here, unlike the SOS codec.
 */

#ifndef WSADPCM_H
#define WSADPCM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes one chunk. `outbytes` is the chunk's declared uncompressed size, which is
 * how the original knows when to stop (the format is self terminating on output
 * count, not on input count). Writes unsigned 8-bit samples. Returns bytes written. */
int ws_unzap(const unsigned char *src, int srclen, unsigned char *dst, int outbytes);

#ifdef __cplusplus
}
#endif

#endif /* WSADPCM_H */
