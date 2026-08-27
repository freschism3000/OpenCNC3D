/*
 * wsaud.h -- streaming reader for Westwood .AUD, the format of every music track,
 * every EVA line and every sound effect on the 1995 MS-DOS disc.
 *
 * Header (common/audio.h AUDHeaderType, little endian, 12 bytes):
 *   u16 Rate          samples per second
 *   u32 Size          compressed bytes that follow the header
 *   u32 UncompSize    decompressed bytes, in the file's own sample format
 *   u8  Flags         bit 0 stereo, bit 1 16 bit
 *   u8  Compression   1 = Westwood ADPCM (8 bit out), 99 = SOS ADPCM (16 bit out)
 * then a run of chunks:
 *   u16 CompSize, u16 UncompSize, u32 0x0000DEAF, then CompSize bytes
 *
 * Three things the naive reader gets wrong, all of them present in the real data:
 *
 *   1. Type 1 exists. Ten files in SOUNDS.MIX are type 1 and they are the infantry
 *      death screams. Type 1 decodes to unsigned 8 bit and its predictor restarts
 *      per chunk; type 99 decodes to signed 16 bit and its predictor runs across the
 *      whole file. Getting that backwards gives noise, not an error.
 *   2. A chunk with CompSize == UncompSize is stored raw and must be copied, not fed
 *      to a codec (soundio_common.cpp does exactly this test).
 *   3. Rates are not all 22050. MGUN2, TONE2, TRANS1 and YELL1 are 22222 Hz and
 *      TNKFIRE2.JUV is 44100 Hz, so somebody has to resample.
 *
 * This reader always hands back signed 16 bit host order samples at the file's own
 * rate. Resampling is the bank's job, not the container's.
 */

#ifndef WSAUD_H
#define WSAUD_H

#include "mixfile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudStream AudStream;

/* Open a loose .AUD on disk. */
AudStream *aud_open_file(const char *path, char *err, int errlen);
/* Open an entry inside an already open MIX. The MixFile must outlive the stream. */
AudStream *aud_open_mix(MixFile *mx, const char *name, char *err, int errlen);
void aud_close(AudStream *a);

int aud_rate(const AudStream *a);
int aud_channels(const AudStream *a);
int aud_bits(const AudStream *a);        /* the file's own bit depth, 8 or 16 */
int aud_compression(const AudStream *a); /* 1 or 99 */
/* Total frames (samples per channel) the header claims, for duration and progress. */
long aud_frames(const AudStream *a);

/* Decodes up to `max_samples` signed 16 bit samples (interleaved if stereo).
 * Returns samples produced, 0 at end of stream. With `loop` set, the end rewinds. */
int aud_read(AudStream *a, short *dst, int max_samples, int loop);

void aud_rewind(AudStream *a);

/* Convenience: decode the whole thing into one malloc'd s16 buffer.
 * Caller frees. Returns sample count, or -1 on failure. */
long aud_decode_all(AudStream *a, short **out);

#ifdef __cplusplus
}
#endif

#endif /* WSAUD_H */
