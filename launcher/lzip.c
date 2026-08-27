/*
 * lzip.c -- see lzip.h.
 */

#include "lzip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#ifdef _WIN32
#include <direct.h>
#define LZ_MKDIR(p) _mkdir(p)
#define LZ_SEP '\\'
#else
#include <unistd.h>
#define LZ_MKDIR(p) mkdir(p, 0755)
#define LZ_SEP '/'
#endif

/* ======================================================================== *
 * SHA-256. FIPS 180-4, written out rather than pulled in: the launcher links
 * zlib and SDL and nothing else, and one hash is smaller than a dependency.
 * ======================================================================== */

typedef struct
{
    unsigned int h[8];
    unsigned long long len;
    unsigned char buf[64];
    int have;
} LZ_Sha;

static const unsigned int lz_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

#define LZ_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void lz_sha_block(LZ_Sha *s, const unsigned char *p)
{
    unsigned int w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16)
               | ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        unsigned int s0 = LZ_ROR(w[i - 15], 7) ^ LZ_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned int s1 = LZ_ROR(w[i - 2], 17) ^ LZ_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0];
    b = s->h[1];
    c = s->h[2];
    d = s->h[3];
    e = s->h[4];
    f = s->h[5];
    g = s->h[6];
    h = s->h[7];
    for (i = 0; i < 64; i++) {
        unsigned int S1 = LZ_ROR(e, 6) ^ LZ_ROR(e, 11) ^ LZ_ROR(e, 25);
        unsigned int ch = (e & f) ^ ((~e) & g);
        unsigned int S0 = LZ_ROR(a, 2) ^ LZ_ROR(a, 13) ^ LZ_ROR(a, 22);
        unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + S1 + ch + lz_k[i] + w[i];
        t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

static void lz_sha_init(LZ_Sha *s)
{
    s->h[0] = 0x6a09e667u;
    s->h[1] = 0xbb67ae85u;
    s->h[2] = 0x3c6ef372u;
    s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu;
    s->h[5] = 0x9b05688cu;
    s->h[6] = 0x1f83d9abu;
    s->h[7] = 0x5be0cd19u;
    s->len = 0;
    s->have = 0;
}

static void lz_sha_update(LZ_Sha *s, const unsigned char *p, size_t n)
{
    s->len += n;
    while (n) {
        size_t take = 64 - (size_t)s->have;
        if (take > n)
            take = n;
        memcpy(s->buf + s->have, p, take);
        s->have += (int)take;
        p += take;
        n -= take;
        if (s->have == 64) {
            lz_sha_block(s, s->buf);
            s->have = 0;
        }
    }
}

static void lz_sha_final(LZ_Sha *s, char *hex65)
{
    unsigned long long bits = s->len * 8;
    unsigned char pad[72];
    int padlen, i;
    static const char *hexdig = "0123456789abcdef";

    padlen = (s->have < 56) ? (56 - s->have) : (120 - s->have);
    memset(pad, 0, sizeof pad);
    pad[0] = 0x80;
    for (i = 0; i < 8; i++)
        pad[padlen + i] = (unsigned char)(bits >> (56 - 8 * i));
    lz_sha_update(s, pad, (size_t)padlen + 8);
    for (i = 0; i < 8; i++) {
        int j;
        for (j = 0; j < 4; j++) {
            unsigned char byte = (unsigned char)(s->h[i] >> (24 - 8 * j));
            hex65[i * 8 + j * 2] = hexdig[byte >> 4];
            hex65[i * 8 + j * 2 + 1] = hexdig[byte & 15];
        }
    }
    hex65[64] = '\0';
}

int lz_sha256_file(const char *path, char *hex65, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    LZ_Sha s;
    unsigned char *buf;
    size_t n;

    if (!f) {
        snprintf(err, (size_t)errlen, "could not open %s to check it", path);
        return 0;
    }
    buf = (unsigned char *)malloc(256 * 1024);
    if (!buf) {
        fclose(f);
        snprintf(err, (size_t)errlen, "out of memory");
        return 0;
    }
    lz_sha_init(&s);
    while ((n = fread(buf, 1, 256 * 1024, f)) > 0)
        lz_sha_update(&s, buf, n);
    free(buf);
    fclose(f);
    lz_sha_final(&s, hex65);
    return 1;
}

/* ======================================================================== *
 * Paths.
 * ======================================================================== */

int lz_mkdirs(const char *path)
{
    char tmp[1200];
    char *p;
    struct stat st;

    snprintf(tmp, sizeof tmp, "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char save = *p;
            *p = '\0';
            if (stat(tmp, &st) != 0)
                LZ_MKDIR(tmp);
            *p = save;
        }
    }
    if (stat(tmp, &st) != 0)
        LZ_MKDIR(tmp);
    return stat(path, &st) == 0;
}

/* A zip entry's name, checked before it is joined onto anything. Everything here
 * is a refusal rather than a sanitisation: quietly rewriting a hostile path would
 * mean extracting a file the archive did not describe. */
static int lz_name_ok(const char *name)
{
    const char *p;
    if (!name || !*name)
        return 0;
    if (name[0] == '/' || name[0] == '\\')
        return 0;
    if (name[1] == ':') /* C:\ and friends */
        return 0;
    for (p = name; *p; p++) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\\' || p[2] == '\0')) {
            if (p == name || p[-1] == '/' || p[-1] == '\\')
                return 0;
        }
    }
    return 1;
}

static const char *lz_strip(const char *name, int strip)
{
    while (strip-- > 0) {
        const char *slash = strchr(name, '/');
        if (!slash)
            return NULL; /* nothing left after stripping: skip the entry */
        name = slash + 1;
    }
    return *name ? name : NULL;
}

/* ======================================================================== *
 * The archive.
 *
 * ONE WALK OVER THE CENTRAL DIRECTORY, used by both callers. Finding the index
 * and stepping through it is the part of the zip format with the offsets in it,
 * and two copies of that is two chances to read one four bytes off. lz_walk_cd
 * owns it; the extractor and the entry probe are callbacks.
 * ======================================================================== */

static unsigned int lz_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16)
           | ((unsigned int)p[3] << 24);
}

static unsigned int lz_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

/* Return 1 to go on, 0 to stop without an error, -1 having filled in `err`. */
typedef int (*LZ_Walk)(void *user, FILE *f, const char *name, unsigned int method,
                       unsigned int csize, unsigned int lho, unsigned int attrs,
                       unsigned int index, unsigned int total, char *err, int errlen);

static int lz_walk_cd(const char *zip, LZ_Walk fn, void *user, char *err, int errlen)
{
    FILE *f = fopen(zip, "rb");
    unsigned char *tail = NULL, *cd = NULL;
    long size, tailn, eocd = -1;
    unsigned int entries, cdsize, cdoff, i, off = 0;
    int ok = 0;

    if (!f) {
        snprintf(err, (size_t)errlen, "could not open the download");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);

    /* The end of central directory record sits within the last 64 KB plus
     * whatever comment the zip carries, so that is how far back to look. */
    tailn = size < 66000 ? size : 66000;
    tail = (unsigned char *)malloc((size_t)tailn);
    if (!tail) {
        snprintf(err, (size_t)errlen, "out of memory");
        goto done;
    }
    fseek(f, size - tailn, SEEK_SET);
    if (fread(tail, 1, (size_t)tailn, f) != (size_t)tailn) {
        snprintf(err, (size_t)errlen, "could not read the download");
        goto done;
    }
    for (i = (unsigned int)tailn; i >= 4; i--) {
        if (lz_u32(tail + i - 4) == 0x06054b50u) {
            eocd = size - tailn + (long)i - 4;
            break;
        }
    }
    if (eocd < 0) {
        snprintf(err, (size_t)errlen, "this is not a zip file");
        goto done;
    }
    {
        const unsigned char *e = tail + (eocd - (size - tailn));
        entries = lz_u16(e + 10);
        cdsize = lz_u32(e + 12);
        cdoff = lz_u32(e + 16);
    }
    if (cdoff == 0xFFFFFFFFu || cdsize == 0xFFFFFFFFu || entries == 0xFFFFu) {
        snprintf(err, (size_t)errlen,
                 "this archive uses Zip64, which this launcher cannot read");
        goto done;
    }

    cd = (unsigned char *)malloc(cdsize);
    if (!cd) {
        snprintf(err, (size_t)errlen, "out of memory");
        goto done;
    }
    fseek(f, (long)cdoff, SEEK_SET);
    if (fread(cd, 1, cdsize, f) != cdsize) {
        snprintf(err, (size_t)errlen, "the archive's index is truncated");
        goto done;
    }

    for (i = 0; i < entries; i++) {
        unsigned int method, csize, namelen, extralen, commentlen, lho, attrs;
        char name[1024];
        int rc;

        if (off + 46 > cdsize || lz_u32(cd + off) != 0x02014b50u) {
            snprintf(err, (size_t)errlen, "the archive's index is damaged");
            goto done;
        }
        method = lz_u16(cd + off + 10);
        csize = lz_u32(cd + off + 20);
        namelen = lz_u16(cd + off + 28);
        extralen = lz_u16(cd + off + 30);
        commentlen = lz_u16(cd + off + 32);
        attrs = lz_u32(cd + off + 38);
        lho = lz_u32(cd + off + 42);
        if (namelen >= sizeof name) {
            snprintf(err, (size_t)errlen, "the archive names a file with an absurd path");
            goto done;
        }
        memcpy(name, cd + off + 46, namelen);
        name[namelen] = '\0';
        off += 46 + namelen + extralen + commentlen;

        rc = fn(user, f, name, method, csize, lho, attrs, i + 1, entries, err, errlen);
        if (rc < 0)
            goto done;
        if (rc == 0)
            break;
    }
    ok = 1;

done:
    free(cd);
    free(tail);
    fclose(f);
    return ok;
}

/* Seek `f` to the first byte of an entry's data.
 *
 * The local header repeats the name and the extra field, and its EXTRA FIELD
 * LENGTH IS NOT ALWAYS THE CENTRAL DIRECTORY'S. Reading it rather than assuming
 * it is the difference between inflating the data and inflating four bytes into
 * it, which presents as "the archive is damaged" and is not. */
static int lz_seek_data(FILE *f, unsigned int lho, char *err, int errlen)
{
    unsigned char lh[30];
    fseek(f, (long)lho, SEEK_SET);
    if (fread(lh, 1, 30, f) != 30 || lz_u32(lh) != 0x04034b50u) {
        snprintf(err, (size_t)errlen, "the archive's file headers are damaged");
        return 0;
    }
    fseek(f, (long)lho + 30 + (long)lz_u16(lh + 26) + (long)lz_u16(lh + 28), SEEK_SET);
    return 1;
}

/* Inflate `csize` compressed bytes from `in` into `out`. Raw deflate, no zlib
 * header, which is what a zip member holds. */
static int lz_inflate(FILE *in, FILE *out, unsigned int csize, char *err, int errlen)
{
    z_stream z;
    unsigned char inbuf[64 * 1024], outbuf[64 * 1024];
    int rc = Z_OK;

    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
        snprintf(err, (size_t)errlen, "could not start decompression");
        return 0;
    }
    for (;;) {
        size_t want = csize < sizeof inbuf ? csize : sizeof inbuf;
        size_t got = want ? fread(inbuf, 1, want, in) : 0;
        if (want && got == 0) {
            inflateEnd(&z);
            snprintf(err, (size_t)errlen, "the archive ends in the middle of a file");
            return 0;
        }
        csize -= (unsigned int)got;
        z.next_in = inbuf;
        z.avail_in = (uInt)got;
        do {
            size_t n;
            z.next_out = outbuf;
            z.avail_out = (uInt)sizeof outbuf;
            rc = inflate(&z, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                inflateEnd(&z);
                snprintf(err, (size_t)errlen, "the archive is damaged (zlib %d)", rc);
                return 0;
            }
            n = sizeof outbuf - z.avail_out;
            if (n && fwrite(outbuf, 1, n, out) != n) {
                inflateEnd(&z);
                snprintf(err, (size_t)errlen, "the disk would not take the new files");
                return 0;
            }
        } while (z.avail_out == 0);
        if (rc == Z_STREAM_END)
            break;
        if (csize == 0 && got == 0)
            break;
    }
    inflateEnd(&z);
    return 1;
}

static int lz_store(FILE *in, FILE *out, unsigned int csize, char *err, int errlen)
{
    unsigned char buf[64 * 1024];
    while (csize) {
        size_t want = csize < sizeof buf ? csize : sizeof buf;
        size_t got = fread(buf, 1, want, in);
        if (got == 0) {
            snprintf(err, (size_t)errlen, "the archive ends in the middle of a file");
            return 0;
        }
        if (fwrite(buf, 1, got, out) != got) {
            snprintf(err, (size_t)errlen, "the disk would not take the new files");
            return 0;
        }
        csize -= (unsigned int)got;
    }
    return 1;
}

/* ------------------------------------------------------------------------ *
 * The entry probe.
 * ------------------------------------------------------------------------ */

typedef struct
{
    const char *want;
    int strip;
    int found;
} LZ_Probe;

static int lz_probe_cb(void *user, FILE *f, const char *name, unsigned int method,
                       unsigned int csize, unsigned int lho, unsigned int attrs,
                       unsigned int index, unsigned int total, char *err, int errlen)
{
    LZ_Probe *p = (LZ_Probe *)user;
    const char *rel;
    (void)f; (void)method; (void)csize; (void)lho; (void)attrs;
    (void)index; (void)total; (void)err; (void)errlen;
    rel = lz_strip(name, p->strip);
    if (rel && !strcmp(rel, p->want)) {
        p->found = 1;
        return 0; /* stop: the answer cannot change */
    }
    return 1;
}

int lz_has_entry(const char *zip, int strip, const char *rel)
{
    LZ_Probe p;
    char err[256];
    p.want = rel;
    p.strip = strip;
    p.found = 0;
    if (!lz_walk_cd(zip, lz_probe_cb, &p, err, sizeof err))
        return 0;
    return p.found;
}

/* ------------------------------------------------------------------------ *
 * The extractor.
 * ------------------------------------------------------------------------ */

typedef struct
{
    const char *dest;
    int strip;
    LZ_Progress cb;
    void *user;
} LZ_Ext;

static int lz_extract_cb(void *user, FILE *f, const char *name, unsigned int method,
                         unsigned int csize, unsigned int lho, unsigned int attrs,
                         unsigned int index, unsigned int total, char *err, int errlen)
{
    LZ_Ext *x = (LZ_Ext *)user;
    char out[1200];
    const char *rel;
    FILE *o;
    int ok;

    if (!lz_name_ok(name)) {
        snprintf(err, (size_t)errlen,
                 "the archive tries to write outside the game folder (%s)", name);
        return -1;
    }
    rel = lz_strip(name, x->strip);
    if (!rel)
        return 1;

    snprintf(out, sizeof out, "%s%c%s", x->dest, LZ_SEP, rel);
    if (rel[strlen(rel) - 1] == '/') {
        lz_mkdirs(out);
        return 1;
    }
    {
        char dir[1200];
        char *slash, *back;
        snprintf(dir, sizeof dir, "%s", out);
        slash = strrchr(dir, '/');
        back = strrchr(dir, '\\');
        if (back > slash)
            slash = back;
        if (slash) {
            *slash = '\0';
            lz_mkdirs(dir);
        }
    }

    if (!lz_seek_data(f, lho, err, errlen))
        return -1;

    o = fopen(out, "wb");
    if (!o) {
        snprintf(err, (size_t)errlen, "could not write %s", out);
        return -1;
    }
    if (method == 0)
        ok = lz_store(f, o, csize, err, errlen);
    else if (method == 8)
        ok = lz_inflate(f, o, csize, err, errlen);
    else {
        snprintf(err, (size_t)errlen, "the archive uses compression method %u", method);
        ok = 0;
    }
    fclose(o);
    if (!ok)
        return -1;

#ifndef _WIN32
    /* The high 16 bits of the external attributes are the unix mode when the zip
     * was made on a unix host, which every zip this launcher fetches is. The
     * executable bit is the one that matters: without it the update leaves a
     * folder that looks complete and cannot start. */
    if ((attrs >> 16) & 0111)
        chmod(out, 0755);
#else
    (void)attrs;
#endif

    if (x->cb && !x->cb(x->user, (int)index, (int)total)) {
        snprintf(err, (size_t)errlen, "cancelled");
        return -1;
    }
    return 1;
}

int lz_extract(const char *zip, const char *dest, int strip, LZ_Progress cb, void *user,
               char *err, int errlen)
{
    LZ_Ext x;
    x.dest = dest;
    x.strip = strip;
    x.cb = cb;
    x.user = user;
    return lz_walk_cd(zip, lz_extract_cb, &x, err, errlen);
}
