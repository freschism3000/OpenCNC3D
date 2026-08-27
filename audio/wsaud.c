#include "wsaud.h"
#include "sosadpcm.h"
#include "wsadpcm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUD_CHUNK_MAGIC 0x0000DEAFu
#define AUD_COMP_WS 1
#define AUD_COMP_SOS 99
#define AUD_HDR 12

/* A byte range, either a whole loose file or one entry inside a MIX. */
typedef struct
{
    FILE *own;   /* set when we opened a loose file and must close it */
    MixFile *mx; /* set when we live inside a MIX (not owned)         */
    long base;   /* absolute offset of the AUD header                 */
    long len;    /* bytes in the range                                */
    long pos;    /* cursor within the range                           */
} AudSrc;

struct AudStream
{
    AudSrc src;
    int rate, channels, bits, comp;
    long uncomp_bytes; /* header UncompSize, in the file's own sample format */

    SOS_State sos;

    short *pending;
    int pending_len; /* samples */
    int pending_pos;
    int pending_cap;

    unsigned char *cbuf;
    int cbuf_cap;
    unsigned char *rawbuf; /* type 1 stages through 8 bit before widening */
    int rawbuf_cap;
};

static int src_read(AudSrc *s, void *dst, int len)
{
    long avail = s->len - s->pos;
    int got;

    if (len <= 0 || avail <= 0)
        return 0;
    if ((long)len > avail)
        len = (int)avail;

    if (s->own) {
        if (fseek(s->own, s->base + s->pos, SEEK_SET) != 0)
            return 0;
        got = (int)fread(dst, 1, (size_t)len, s->own);
    } else {
        got = mixfile_read(s->mx, s->base + s->pos, dst, len);
    }
    s->pos += got;
    return got;
}

static unsigned short le16(const unsigned char *p) { return (unsigned short)(p[0] | (p[1] << 8)); }

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16)
           | ((unsigned int)p[3] << 24);
}

static AudStream *aud_finish_open(AudStream *a, const char *what, char *err, int errlen)
{
    unsigned char hdr[AUD_HDR];

    if (src_read(&a->src, hdr, AUD_HDR) != AUD_HDR) {
        snprintf(err, (size_t)errlen, "%s: truncated AUD header", what);
        aud_close(a);
        return NULL;
    }
    a->rate = (int)le16(hdr);
    a->uncomp_bytes = (long)le32(hdr + 6);
    a->channels = (hdr[10] & 1) ? 2 : 1;
    a->bits = (hdr[10] & 2) ? 16 : 8;
    a->comp = hdr[11];

    if (a->comp != AUD_COMP_SOS && a->comp != AUD_COMP_WS) {
        snprintf(err, (size_t)errlen, "%s: unknown AUD compression %d", what, a->comp);
        aud_close(a);
        return NULL;
    }
    if (a->rate < 4000 || a->rate > 48000) {
        snprintf(err, (size_t)errlen, "%s: implausible rate %d Hz", what, a->rate);
        aud_close(a);
        return NULL;
    }
    sos_reset(&a->sos);
    return a;
}

AudStream *aud_open_file(const char *path, char *err, int errlen)
{
    AudStream *a = (AudStream *)calloc(1, sizeof(AudStream));
    if (!a)
        return NULL;
    a->src.own = fopen(path, "rb");
    if (!a->src.own) {
        snprintf(err, (size_t)errlen, "cannot open %s", path);
        free(a);
        return NULL;
    }
    fseek(a->src.own, 0, SEEK_END);
    a->src.len = ftell(a->src.own);
    a->src.base = 0;
    a->src.pos = 0;
    return aud_finish_open(a, path, err, errlen);
}

AudStream *aud_open_mix(MixFile *mx, const char *name, char *err, int errlen)
{
    AudStream *a;
    long off, size;

    if (!mixfile_find(mx, name, &off, &size)) {
        snprintf(err, (size_t)errlen, "%s not in %s", name, mixfile_path(mx));
        return NULL;
    }
    a = (AudStream *)calloc(1, sizeof(AudStream));
    if (!a)
        return NULL;
    a->src.mx = mx;
    a->src.base = off;
    a->src.len = size;
    a->src.pos = 0;
    return aud_finish_open(a, name, err, errlen);
}

void aud_close(AudStream *a)
{
    if (!a)
        return;
    if (a->src.own)
        fclose(a->src.own);
    free(a->pending);
    free(a->cbuf);
    free(a->rawbuf);
    free(a);
}

int aud_rate(const AudStream *a) { return a->rate; }
int aud_channels(const AudStream *a) { return a->channels; }
int aud_bits(const AudStream *a) { return a->bits; }
int aud_compression(const AudStream *a) { return a->comp; }

long aud_frames(const AudStream *a)
{
    long bytes_per_frame = (long)a->channels * (a->bits / 8);
    if (bytes_per_frame <= 0)
        return 0;
    return a->uncomp_bytes / bytes_per_frame;
}

void aud_rewind(AudStream *a)
{
    a->src.pos = AUD_HDR;
    sos_reset(&a->sos); /* the SOS predictor must restart with the stream */
    a->pending_len = a->pending_pos = 0;
}

static int grow(void **p, int *cap, int need, int elem)
{
    if (need <= *cap)
        return 1;
    {
        void *n = realloc(*p, (size_t)need * (size_t)elem);
        if (!n)
            return 0;
        *p = n;
        *cap = need;
    }
    return 1;
}

/* Pulls one chunk in and decodes it into `pending` as signed 16 bit.
 * Returns 0 at end of stream. */
static int aud_fill(AudStream *a)
{
    unsigned char chdr[8];
    int csize, usize, i;

    if (src_read(&a->src, chdr, 8) != 8)
        return 0;
    csize = (int)le16(chdr);
    usize = (int)le16(chdr + 2);
    if (le32(chdr + 4) != AUD_CHUNK_MAGIC)
        return 0; /* not a chunk header: treat as end of stream */
    if (csize <= 0)
        return 0;

    if (!grow((void **)&a->cbuf, &a->cbuf_cap, csize, 1))
        return 0;
    if (src_read(&a->src, a->cbuf, csize) != csize)
        return 0;

    if (a->bits == 16) {
        /* usize is bytes, two per sample */
        int want = usize / 2;
        int cap;
        if (want <= 0)
            want = csize * 2;
        /* Size the buffer for what the CODEC can emit, not for what the chunk header
         * claims. SOS writes two samples per input byte unconditionally; a corrupt
         * or hostile usize smaller than that would otherwise walk off the end. The
         * claimed length is still used to trim, below. */
        cap = csize * 2;
        if (want > cap)
            cap = want;
        if (!grow((void **)&a->pending, &a->pending_cap, cap + 8, (int)sizeof(short)))
            return 0;

        if (csize == usize) {
            /* Stored raw: copy little endian samples through, no codec. */
            for (i = 0; i < want; i++)
                a->pending[i] = (short)(a->cbuf[i * 2] | (a->cbuf[i * 2 + 1] << 8));
            a->pending_len = want;
        } else if (a->comp == AUD_COMP_SOS) {
            int produced = sos_decode_s16(&a->sos, a->cbuf, csize, a->pending);
            if (produced > want)
                produced = want;
            a->pending_len = produced;
        } else {
            /* Type 1 always produces 8 bit; a 16 bit header with type 1 is
             * malformed and we refuse it rather than emit noise. */
            return 0;
        }
    } else {
        /* 8 bit source: decode to unsigned 8 bit, then widen. */
        int want = usize > 0 ? usize : csize * 4;
        /* Worst case output per input byte: 4 for Westwood's 2 bit mode, 2 for SOS.
         * Size for that regardless of what the header claims, same reasoning as the
         * 16 bit branch above. */
        int cap = csize * 4;
        if (want > cap)
            cap = want;
        if (!grow((void **)&a->rawbuf, &a->rawbuf_cap, cap + 8, 1))
            return 0;
        if (!grow((void **)&a->pending, &a->pending_cap, cap + 8, (int)sizeof(short)))
            return 0;

        if (csize == usize) {
            memcpy(a->rawbuf, a->cbuf, (size_t)csize);
            a->pending_len = csize;
        } else if (a->comp == AUD_COMP_WS) {
            a->pending_len = ws_unzap(a->cbuf, csize, a->rawbuf, want);
        } else {
            /* SOS at 8 bit: the codec's 8 bit path takes the high byte of the
             * predictor and flips the sign bit (soscodec.cpp BITS_8). */
            a->pending_len = sos_decode_u8(&a->sos, a->cbuf, csize, a->rawbuf);
            if (a->pending_len > want)
                a->pending_len = want;
        }
        /* Unsigned 8 bit to signed 16 bit. Multiply rather than shift: the value is
         * negative for every sample below mid scale and left shifting a negative int
         * is undefined behaviour (UBSan flags it, and a compiler is free to make it
         * mean anything on the Win98 toolchain). */
        for (i = 0; i < a->pending_len; i++)
            a->pending[i] = (short)(((int)a->rawbuf[i] - 128) * 256);
    }

    a->pending_pos = 0;
    return a->pending_len > 0;
}

int aud_read(AudStream *a, short *dst, int max_samples, int loop)
{
    int done = 0;

    while (done < max_samples) {
        int have = a->pending_len - a->pending_pos;
        if (have <= 0) {
            if (!aud_fill(a)) {
                if (!loop)
                    break;
                aud_rewind(a);
                if (!aud_fill(a))
                    break;
            }
            continue;
        }
        if (have > max_samples - done)
            have = max_samples - done;
        memcpy(dst + done, a->pending + a->pending_pos, (size_t)have * sizeof(short));
        a->pending_pos += have;
        done += have;
    }
    return done;
}

long aud_decode_all(AudStream *a, short **out)
{
    long cap = aud_frames(a) * a->channels + 4096;
    long used = 0;
    short *buf;

    if (cap < 8192)
        cap = 8192;
    buf = (short *)malloc((size_t)cap * sizeof(short));
    if (!buf)
        return -1;

    for (;;) {
        int n;
        if (used == cap) {
            short *nb;
            cap *= 2;
            nb = (short *)realloc(buf, (size_t)cap * sizeof(short));
            if (!nb) {
                free(buf);
                return -1;
            }
            buf = nb;
        }
        n = aud_read(a, buf + used, (int)(cap - used), 0);
        if (n <= 0)
            break;
        used += n;
    }
    *out = buf;
    return used;
}
