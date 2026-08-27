/* sosadpcm.c -- common/soscodec.cpp, the tables and the step, nothing else. */

#include "sosadpcm.h"

static const short sos_index_tab[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static const short sos_step_tab[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,   23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,   80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,  279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,  963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024, 3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

void sos_reset(SOS_State *st)
{
    st->index = 0;
    st->sample = 0;
}

static int sos_step(SOS_State *st, int nybble)
{
    int step = sos_step_tab[st->index];
    int diff = step >> 3;
    int idx;

    if (nybble & 4)
        diff += step;
    if (nybble & 2)
        diff += step >> 1;
    if (nybble & 1)
        diff += step >> 2;
    if (nybble & 8)
        diff = -diff;

    st->sample += diff;
    if (st->sample > 32767)
        st->sample = 32767;
    if (st->sample < -32768)
        st->sample = -32768;

    idx = st->index + sos_index_tab[nybble & 7];
    if (idx < 0)
        idx = 0;
    if (idx > 88)
        idx = 88;
    st->index = idx;

    return st->sample;
}

int sos_decode_s16(SOS_State *st, const unsigned char *src, int n, short *dst)
{
    int i, out = 0;
    for (i = 0; i < n; i++) {
        dst[out++] = (short)sos_step(st, src[i] & 0x0F);
        dst[out++] = (short)sos_step(st, (src[i] >> 4) & 0x0F);
    }
    return out;
}

int sos_decode_u8(SOS_State *st, const unsigned char *src, int n, unsigned char *dst)
{
    int i, out = 0;
    for (i = 0; i < n; i++) {
        int s = sos_step(st, src[i] & 0x0F);
        dst[out++] = (unsigned char)((((unsigned int)s & 0xFF00u) >> 8) ^ 0x80u);
        s = sos_step(st, (src[i] >> 4) & 0x0F);
        dst[out++] = (unsigned char)((((unsigned int)s & 0xFF00u) >> 8) ^ 0x80u);
    }
    return out;
}

int sos_decode(SOS_State *st, const unsigned char *src, int n, unsigned char *dst)
{
    int i, out = 0;
    for (i = 0; i < n; i++) {
        short s = (short)sos_step(st, src[i] & 0x0F);
        dst[out++] = (unsigned char)(s & 0xFF);
        dst[out++] = (unsigned char)((s >> 8) & 0xFF);
        s = (short)sos_step(st, (src[i] >> 4) & 0x0F);
        dst[out++] = (unsigned char)(s & 0xFF);
        dst[out++] = (unsigned char)((s >> 8) & 0xFF);
    }
    return out;
}
