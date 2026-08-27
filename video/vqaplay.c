/*
 * vqaplay.c -- Westwood VQA version 2 player. See vqaplay.h for the sources each
 * piece is ported from; every rule in here is quoted from the GPL engine.
 */

#include "vqaplay.h"

#include <stdlib.h>
#include <string.h>

#define VQ_MAX_CB (2000 * 4 * 2 * 2) /* CBentries * blockw * blockh, with slack */

struct VQ_Movie
{
    FILE *fh;
    long end;

    int w, h, bw, bh, bpr, rows, blocks;
    int frames, fps, rate, channels, bits, groupsize;

    unsigned char *frame;    /* w*h palette indices                       */
    unsigned char pal[768];  /* 8-bit RGB, ready for a texture            */
    int pal_dirty;

    unsigned char *cb;       /* active codebook                           */
    unsigned char *cbnext;   /* the one that takes over after this frame   */
    long cbsize;
    unsigned char *partial;  /* CBP0 accumulation                          */
    long partial_len;
    int partial_parts;
    int have_next_cb;

    unsigned char *scratch;  /* chunk payload staging                      */
    long scratch_cap;
    unsigned char *vpt;      /* two pointer planes                         */

    /* SND2 / SOS ADPCM state, soscodec.cpp _SOS_COMPRESS_INFO for one channel */
    int adpcm_index;
    int adpcm_sample;

    unsigned char *pcm;      /* decoded 16-bit PCM ring, simple queue       */
    long pcm_len, pcm_cap, pcm_head;
};

/* ---------------------------------------------------------------- LCW ---- */
/* common/lcw.cpp LCW_Uncompress. VPTZ, CBFZ and CPLZ chunks are LCW packed. */

static long lcw_uncompress(const unsigned char *src, long srclen, unsigned char *dst, long dstlen)
{
    long sp = 0, dp = 0;
    while (dp < dstlen && sp < srclen) {
        unsigned char op = src[sp++];
        if (!(op & 0x80)) {
            int count = (op >> 4) + 3;
            long back, cp;
            if (sp >= srclen)
                break;
            back = src[sp++] + ((op & 0x0F) << 8);
            cp = dp - back;
            while (count-- && dp < dstlen) {
                dst[dp] = (cp >= 0) ? dst[cp] : 0;
                dp++;
                cp++;
            }
        } else if (!(op & 0x40)) {
            int count = op & 0x3F;
            if (op == 0x80)
                break;
            while (count-- && dp < dstlen && sp < srclen)
                dst[dp++] = src[sp++];
        } else if (op == 0xFE) {
            long count = src[sp] | (src[sp + 1] << 8);
            unsigned char val = src[sp + 2];
            sp += 3;
            while (count-- && dp < dstlen)
                dst[dp++] = val;
        } else if (op == 0xFF) {
            long count = src[sp] | (src[sp + 1] << 8);
            long cp = src[sp + 2] | (src[sp + 3] << 8);
            sp += 4;
            while (count-- && dp < dstlen)
                dst[dp++] = dst[cp++];
        } else {
            int count = (op & 0x3F) + 3;
            long cp = src[sp] | (src[sp + 1] << 8);
            sp += 2;
            while (count-- && dp < dstlen)
                dst[dp++] = dst[cp++];
        }
    }
    return dp;
}

/* -------------------------------------------------------------- ADPCM ---- */
/* soscodec.cpp. One compressed byte carries two nybbles, low nybble first. */

static const short sos_index_tab[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static const short sos_step_tab[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,   23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,   80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,  279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,  963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024, 3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static int sos_step(VQ_Movie *m, int nybble)
{
    int step = sos_step_tab[m->adpcm_index];
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

    m->adpcm_sample += diff;
    if (m->adpcm_sample > 32767)
        m->adpcm_sample = 32767;
    if (m->adpcm_sample < -32768)
        m->adpcm_sample = -32768;

    idx = m->adpcm_index + sos_index_tab[nybble & 7];
    if (idx < 0)
        idx = 0;
    if (idx > 88)
        idx = 88;
    m->adpcm_index = idx;

    return m->adpcm_sample;
}

static void pcm_push(VQ_Movie *m, const unsigned char *src, long n)
{
    long need, i;

    /* Compact first: the queue is drained from the head every frame. */
    if (m->pcm_head > 0 && m->pcm_head == m->pcm_len) {
        m->pcm_head = m->pcm_len = 0;
    } else if (m->pcm_head > 65536) {
        memmove(m->pcm, m->pcm + m->pcm_head, (size_t)(m->pcm_len - m->pcm_head));
        m->pcm_len -= m->pcm_head;
        m->pcm_head = 0;
    }

    need = m->pcm_len + n * 4;
    if (need > m->pcm_cap) {
        long cap = m->pcm_cap ? m->pcm_cap : 65536;
        while (cap < need)
            cap *= 2;
        m->pcm = (unsigned char *)realloc(m->pcm, (size_t)cap);
        m->pcm_cap = cap;
    }

    for (i = 0; i < n; i++) {
        short s;
        s = (short)sos_step(m, src[i] & 0x0F);
        m->pcm[m->pcm_len++] = (unsigned char)(s & 0xFF);
        m->pcm[m->pcm_len++] = (unsigned char)((s >> 8) & 0xFF);
        s = (short)sos_step(m, (src[i] >> 4) & 0x0F);
        m->pcm[m->pcm_len++] = (unsigned char)(s & 0xFF);
        m->pcm[m->pcm_len++] = (unsigned char)((s >> 8) & 0xFF);
    }
}

/* ------------------------------------------------------------- reading --- */

static int rd(VQ_Movie *m, void *p, long n)
{
    return fread(p, 1, (size_t)n, m->fh) == (size_t)n;
}

static unsigned int be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

static unsigned short le16(const unsigned char *p)
{
    return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned char *scratch(VQ_Movie *m, long n)
{
    if (n > m->scratch_cap) {
        long cap = m->scratch_cap ? m->scratch_cap : 4096;
        while (cap < n)
            cap *= 2;
        m->scratch = (unsigned char *)realloc(m->scratch, (size_t)cap);
        m->scratch_cap = cap;
    }
    return m->scratch;
}

VQ_Movie *vq_open(const char *path, char *err, int errlen)
{
    VQ_Movie *m = (VQ_Movie *)calloc(1, sizeof(VQ_Movie));
    unsigned char hdr[64];
    unsigned char tag[8];
    unsigned int size;

    if (!m)
        return NULL;
    m->fh = fopen(path, "rb");
    if (!m->fh) {
        snprintf(err, (size_t)errlen, "cannot open %s", path);
        free(m);
        return NULL;
    }
    fseek(m->fh, 0, SEEK_END);
    m->end = ftell(m->fh);
    fseek(m->fh, 0, SEEK_SET);

    if (!rd(m, tag, 8) || memcmp(tag, "FORM", 4) != 0) {
        snprintf(err, (size_t)errlen, "%s: not an IFF FORM", path);
        vq_close(m);
        return NULL;
    }
    if (!rd(m, tag, 4) || memcmp(tag, "WVQA", 4) != 0) {
        snprintf(err, (size_t)errlen, "%s: not a WVQA movie", path);
        vq_close(m);
        return NULL;
    }
    if (!rd(m, tag, 8) || memcmp(tag, "VQHD", 4) != 0) {
        snprintf(err, (size_t)errlen, "%s: expected VQHD", path);
        vq_close(m);
        return NULL;
    }
    size = be32(tag + 4);
    if (size > sizeof hdr || !rd(m, hdr, (long)size)) {
        snprintf(err, (size_t)errlen, "%s: bad VQHD (%u bytes)", path, size);
        vq_close(m);
        return NULL;
    }
    if (size & 1)
        fgetc(m->fh);

    if (le16(hdr) != 2) {
        snprintf(err, (size_t)errlen, "%s: VQA version %d, only 2 is supported", path, le16(hdr));
        vq_close(m);
        return NULL;
    }

    m->frames = le16(hdr + 4);
    m->w = le16(hdr + 6);
    m->h = le16(hdr + 8);
    m->bw = hdr[10];
    m->bh = hdr[11];
    m->fps = hdr[12];
    m->groupsize = hdr[13];
    m->rate = le16(hdr + 24);
    m->channels = hdr[26];
    m->bits = hdr[27];

    if (m->bw != 4 || m->bh != 2) {
        snprintf(err, (size_t)errlen, "%s: %dx%d blocks, only 4x2 is implemented",
                 path, m->bw, m->bh);
        vq_close(m);
        return NULL;
    }

    m->bpr = m->w / m->bw;
    m->rows = m->h / m->bh;
    m->blocks = m->bpr * m->rows;
    if (!m->rate)
        m->rate = 22050;
    if (!m->channels)
        m->channels = 1;

    m->frame = (unsigned char *)calloc((size_t)(m->w * m->h), 1);
    m->cb = (unsigned char *)calloc(VQ_MAX_CB, 1);
    m->cbnext = (unsigned char *)calloc(VQ_MAX_CB, 1);
    m->partial = (unsigned char *)calloc(VQ_MAX_CB, 1);
    m->vpt = (unsigned char *)calloc((size_t)(m->blocks * 2 + 16), 1);
    m->adpcm_index = 0;
    m->adpcm_sample = 0;
    return m;
}

void vq_close(VQ_Movie *m)
{
    if (!m)
        return;
    if (m->fh)
        fclose(m->fh);
    free(m->frame);
    free(m->cb);
    free(m->cbnext);
    free(m->partial);
    free(m->vpt);
    free(m->scratch);
    free(m->pcm);
    free(m);
}

int vq_width(const VQ_Movie *m) { return m->w; }
int vq_height(const VQ_Movie *m) { return m->h; }
int vq_frames(const VQ_Movie *m) { return m->frames; }
int vq_fps(const VQ_Movie *m) { return m->fps ? m->fps : 15; }
int vq_sample_rate(const VQ_Movie *m) { return m->rate; }
int vq_channels(const VQ_Movie *m) { return m->channels; }
const unsigned char *vq_pixels(const VQ_Movie *m) { return m->frame; }
const unsigned char *vq_palette(const VQ_Movie *m) { return m->pal; }
int vq_palette_dirty(const VQ_Movie *m) { return m->pal_dirty; }
int vq_audio_pending(const VQ_Movie *m) { return (int)(m->pcm_len - m->pcm_head); }

int vq_take_audio(VQ_Movie *m, void *dst, int max)
{
    long have = m->pcm_len - m->pcm_head;
    if (have <= 0)
        return 0;
    if (have > max)
        have = max;
    memcpy(dst, m->pcm + m->pcm_head, (size_t)have);
    m->pcm_head += have;
    return (int)have;
}

/* vqapalette.cpp:41 VQA_SetPalette: 6 bits per component, then a 15% luminance
 * lift capped at 63. The widening to 8 bits is the engine's v << 2. */
static void set_palette(VQ_Movie *m, const unsigned char *src, long n)
{
    long i;
    for (i = 0; i < n && i < 768; i++) {
        int v = src[i] & 0x3F;
        v += v * 15 / 100;
        if (v > 63)
            v = 63;
        m->pal[i] = (unsigned char)(v << 2);
    }
    m->pal_dirty = 1;
}

/* unvqbuff.cpp:14 UnVQ_4x2 */
static void unvq(VQ_Movie *m)
{
    const unsigned char *p1 = m->vpt;
    const unsigned char *p2 = m->vpt + m->blocks;
    int i;

    for (i = 0; i < m->blocks; i++) {
        int bx = (i % m->bpr) * m->bw;
        int by = (i / m->bpr) * m->bh;
        unsigned char *dst = m->frame + by * m->w + bx;
        int r;

        if (p2[i] == 15) {
            for (r = 0; r < m->bh; r++)
                memset(dst + r * m->w, p1[i], (size_t)m->bw);
        } else {
            const unsigned char *e = m->cb + (((p2[i] << 8) | p1[i]) * m->bw * m->bh);
            for (r = 0; r < m->bh; r++)
                memcpy(dst + r * m->w, e + r * m->bw, (size_t)m->bw);
        }
    }
}

static int frame_chunk(VQ_Movie *m, long end)
{
    unsigned char tag[8];

    while (ftell(m->fh) + 8 <= end) {
        unsigned int size;
        unsigned char *buf;

        if (!rd(m, tag, 8))
            return -1;
        size = be32(tag + 4);
        buf = scratch(m, (long)size + 2);
        if (!rd(m, buf, (long)size))
            return -1;
        if (size & 1)
            fgetc(m->fh);

        if (!memcmp(tag, "CPL0", 4)) {
            set_palette(m, buf, (long)size);
        } else if (!memcmp(tag, "CPLZ", 4)) {
            unsigned char tmp[768];
            long n = lcw_uncompress(buf, (long)size, tmp, (long)sizeof tmp);
            set_palette(m, tmp, n);
        } else if (!memcmp(tag, "CBF0", 4)) {
            if (size > VQ_MAX_CB)
                size = VQ_MAX_CB;
            memcpy(m->cb, buf, size);
            m->cbsize = size;
        } else if (!memcmp(tag, "CBFZ", 4)) {
            m->cbsize = lcw_uncompress(buf, (long)size, m->cb, VQ_MAX_CB);
        } else if (!memcmp(tag, "CBP0", 4) || !memcmp(tag, "CBPZ", 4)) {
            /* vqaloader.cpp:129 VQA_Load_CBP0: the parts accumulate, and the group
             * becomes the codebook once Header.Groupsize of them have arrived. */
            if (m->partial_len + (long)size <= VQ_MAX_CB) {
                memcpy(m->partial + m->partial_len, buf, size);
                m->partial_len += (long)size;
            }
            if (++m->partial_parts >= m->groupsize) {
                if (!memcmp(tag, "CBPZ", 4))
                    lcw_uncompress(m->partial, m->partial_len, m->cbnext, VQ_MAX_CB);
                else
                    memcpy(m->cbnext, m->partial, (size_t)m->partial_len);
                m->have_next_cb = 1;
                m->partial_len = 0;
                m->partial_parts = 0;
            }
        } else if (!memcmp(tag, "VPTZ", 4)) {
            lcw_uncompress(buf, (long)size, m->vpt, m->blocks * 2);
            unvq(m);
        } else if (!memcmp(tag, "VPT0", 4) || !memcmp(tag, "VPTR", 4)) {
            long n = (long)size;
            if (n > m->blocks * 2)
                n = m->blocks * 2;
            memcpy(m->vpt, buf, (size_t)n);
            unvq(m);
        } else if (!memcmp(tag, "SND0", 4)) {
            /* Uncompressed PCM, passed straight through. */
            long need = m->pcm_len + (long)size;
            if (need > m->pcm_cap) {
                long cap = m->pcm_cap ? m->pcm_cap : 65536;
                while (cap < need)
                    cap *= 2;
                m->pcm = (unsigned char *)realloc(m->pcm, (size_t)cap);
                m->pcm_cap = cap;
            }
            memcpy(m->pcm + m->pcm_len, buf, size);
            m->pcm_len += (long)size;
        } else if (!memcmp(tag, "SND2", 4)) {
            pcm_push(m, buf, (long)size);
        }
    }
    return 0;
}

int vq_next_frame(VQ_Movie *m)
{
    unsigned char tag[8];

    m->pal_dirty = 0;

    for (;;) {
        long pos = ftell(m->fh);
        unsigned int size;

        if (pos + 8 > m->end)
            return 0;
        if (!rd(m, tag, 8))
            return 0;
        size = be32(tag + 4);

        if (!memcmp(tag, "VQFR", 4)) {
            long end = ftell(m->fh) + (long)size;
            if (frame_chunk(m, end) < 0)
                return -1;
            fseek(m->fh, end + (size & 1), SEEK_SET);

            /* The accumulated codebook takes over from the NEXT frame, which is
             * what vqaloader.cpp does by handing CurCB on only after the swap. */
            if (m->have_next_cb) {
                unsigned char *t = m->cb;
                m->cb = m->cbnext;
                m->cbnext = t;
                m->have_next_cb = 0;
            }
            return 1;
        }

        if (!memcmp(tag, "SND2", 4)) {
            unsigned char *buf = scratch(m, (long)size + 2);
            if (!rd(m, buf, (long)size))
                return 0;
            if (size & 1)
                fgetc(m->fh);
            pcm_push(m, buf, (long)size);
            continue;
        }

        /* FINF and anything else we do not need. */
        fseek(m->fh, (long)size + (size & 1), SEEK_CUR);
    }
}
