#include "pngwrite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int crc_table[256];
static int crc_ready = 0;

static unsigned int crc32_buf(unsigned int crc, const unsigned char *b, long n)
{
    long i;
    if (!crc_ready) {
        int k, j;
        for (k = 0; k < 256; k++) {
            unsigned int c = (unsigned int)k;
            for (j = 0; j < 8; j++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[k] = c;
        }
        crc_ready = 1;
    }
    for (i = 0; i < n; i++)
        crc = crc_table[(crc ^ b[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void be32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void png_chunk(FILE *f, const char *tag, const unsigned char *data, long n)
{
    unsigned char hdr[4];
    unsigned int crc;
    be32(hdr, (unsigned int)n);
    fwrite(hdr, 1, 4, f);
    fwrite(tag, 1, 4, f);
    if (n)
        fwrite(data, 1, (size_t)n, f);
    crc = crc32_buf(0xFFFFFFFFu, (const unsigned char *)tag, 4);
    crc = crc32_buf(crc, data, n) ^ 0xFFFFFFFFu;
    be32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

int png_write_rgba(const char *path, const unsigned char *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    unsigned char ihdr[13];
    unsigned char *raw, *z;
    long rawlen, zlen, pos, i;
    unsigned int a = 1, b = 0;

    if (!f)
        return 0;
    fwrite("\211PNG\r\n\032\n", 1, 8, f);
    be32(ihdr, (unsigned int)w);
    be32(ihdr + 4, (unsigned int)h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);

    rawlen = (long)h * (1 + (long)w * 4);
    raw = (unsigned char *)malloc((size_t)rawlen);
    for (i = 0; i < h; i++) {
        raw[i * (1 + (long)w * 4)] = 0;
        memcpy(raw + i * (1 + (long)w * 4) + 1, rgba + i * (long)w * 4, (size_t)w * 4);
    }
    for (i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }

    zlen = 2 + rawlen + 5 * ((rawlen + 65534) / 65535) + 4;
    z = (unsigned char *)malloc((size_t)zlen);
    pos = 0;
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    for (i = 0; i < rawlen;) {
        long n = rawlen - i;
        int final;
        if (n > 65535)
            n = 65535;
        final = (i + n >= rawlen);
        z[pos++] = (unsigned char)final;
        z[pos++] = (unsigned char)(n & 0xFF);
        z[pos++] = (unsigned char)(n >> 8);
        z[pos++] = (unsigned char)(~n & 0xFF);
        z[pos++] = (unsigned char)((~n >> 8) & 0xFF);
        memcpy(z + pos, raw + i, (size_t)n);
        pos += n;
        i += n;
    }
    be32(z + pos, (b << 16) | a);
    pos += 4;
    png_chunk(f, "IDAT", z, pos);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}

void png_nearest_scale(const unsigned char *src, int sw, int sh, unsigned char *dst, int scale)
{
    int y, x, k;
    for (y = 0; y < sh * scale; y++)
        for (x = 0; x < sw * scale; x++) {
            const unsigned char *s = src + ((long)(y / scale) * sw + (x / scale)) * 4;
            unsigned char *d = dst + ((long)y * sw * scale + x) * 4;
            for (k = 0; k < 4; k++)
                d[k] = s[k];
        }
}
