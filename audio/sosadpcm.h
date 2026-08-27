/*
 * sosadpcm.h -- the Westwood "SOS" 4:1 ADPCM codec, ported from common/soscodec.cpp.
 *
 * The same codec appears in two places, which is why it lives on its own:
 *   - VQA movies, in their SND2 chunks (common/vqaloader.cpp VQA_Load_SND2)
 *   - .AUD files with compression type 99, which is every music track, every EVA
 *     line and all but ten of the sound effects on the 1995 discs
 *
 * One compressed byte carries two nybbles, low nybble first, and each nybble is one
 * sample. State (predictor and step index) runs across the whole stream, so a decoder
 * must not be reset between chunks of the same file.
 *
 * Two output widths, because the format has two. 16 bit writes the predictor; 8 bit
 * writes its high byte with the sign bit flipped, which is what the BITS_8 branch of
 * sosCODECDecompressDataTemplate does.
 */

#ifndef SOSADPCM_H
#define SOSADPCM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int index;  /* step table index, 0..88          */
    int sample; /* running predictor, -32768..32767 */
} SOS_State;

void sos_reset(SOS_State *st);

/* Decodes `n` compressed bytes into 2*n signed 16 bit samples. Returns samples. */
int sos_decode_s16(SOS_State *st, const unsigned char *src, int n, short *dst);

/* Decodes `n` compressed bytes into 2*n unsigned 8 bit samples. Returns samples. */
int sos_decode_u8(SOS_State *st, const unsigned char *src, int n, unsigned char *dst);

/* Legacy byte-oriented entry point kept so video/vqaplay.c builds unchanged:
 * writes 2*n little endian 16 bit samples, returns BYTES written. */
int sos_decode(SOS_State *st, const unsigned char *src, int n, unsigned char *dst);

#ifdef __cplusplus
}
#endif

#endif /* SOSADPCM_H */
