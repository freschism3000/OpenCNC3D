/*
 * lzip.c -- see lzip.h.
 */

#include "lzip.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include <dirent.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define LZ_MKDIR(p) _mkdir(p)
#define LZ_SEP '\\'
/* The C runtime has no lstat. Windows does have symbolic links and junctions, and
 * this stat follows them; every caller that must not be led elsewhere asks about
 * them separately (lz_is_link). */
#define LZ_LSTAT(p, st) stat(p, st)
#define LZ_NAP() Sleep(50)
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#define LZ_MKDIR(p) mkdir(p, 0755)
#define LZ_SEP '/'
#define LZ_LSTAT(p, st) lstat(p, st)
#define LZ_NAP() usleep(50000)
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

/* A zip entry's name, or a journal line's, checked before it is joined onto
 * anything. Everything here is a refusal rather than a sanitisation: quietly
 * rewriting a hostile path would mean extracting a file the archive did not
 * describe. A control character is refused too, because a name is one line of the
 * update journal, and so is a colon anywhere, which on Windows names a drive
 * ("C:x") or a stream hidden inside another file ("x:y"). */
static int lz_name_ok(const char *name)
{
    const char *p;
    if (!name || !*name)
        return 0;
    if (name[0] == '/' || name[0] == '\\')
        return 0;
    for (p = name; *p; p++) {
        if ((unsigned char)*p < 0x20 || *p == ':')
            return 0;
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

/* Whether a name is taken. A name the system will not even look at counts as
 * taken: on Windows a file removed while something still had it open stays on its
 * name, refusing every open (stat included), until the last holder closes it, and
 * a rename onto that name fails for as long as it does. */
static int lz_exists(const char *path)
{
    struct stat st;
    if (LZ_LSTAT(path, &st) == 0)
        return 1;
    return errno != ENOENT && errno != ENOTDIR;
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

/* One entry of the central directory, as a callback sees it. `crc` and `usize`
 * are what let the extractor recognise a file already on disk as this entry
 * without opening it for writing, and check what it wrote. */
typedef struct
{
    const char *name;
    unsigned int method, crc, csize, usize, lho, attrs;
    unsigned int index, total;
} LZ_Entry;

/* Return 1 to go on, 0 to stop without an error, -1 having filled in `err`. */
typedef int (*LZ_Walk)(void *user, FILE *f, const LZ_Entry *e, char *err, int errlen);

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
        unsigned int namelen, extralen, commentlen;
        char name[1024];
        LZ_Entry e;
        int rc;

        if (off + 46 > cdsize || lz_u32(cd + off) != 0x02014b50u) {
            snprintf(err, (size_t)errlen, "the archive's index is damaged");
            goto done;
        }
        e.method = lz_u16(cd + off + 10);
        e.crc = lz_u32(cd + off + 16);
        e.csize = lz_u32(cd + off + 20);
        e.usize = lz_u32(cd + off + 24);
        namelen = lz_u16(cd + off + 28);
        extralen = lz_u16(cd + off + 30);
        commentlen = lz_u16(cd + off + 32);
        e.attrs = lz_u32(cd + off + 38);
        e.lho = lz_u32(cd + off + 42);
        if (namelen >= sizeof name) {
            snprintf(err, (size_t)errlen, "the archive names a file with an absurd path");
            goto done;
        }
        memcpy(name, cd + off + 46, namelen);
        name[namelen] = '\0';
        off += 46 + namelen + extralen + commentlen;
        e.name = name;
        e.index = i + 1;
        e.total = entries;

        rc = fn(user, f, &e, err, errlen);
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
 * header, which is what a zip member holds. `crc` and `size` accumulate what was
 * written, for the caller to hold against the archive's own record.
 *
 * A STREAM THAT RUNS OUT BEFORE ITS END MARKER IS DAMAGED, not finished. zlib
 * reports the marker as Z_STREAM_END, and the data running out without one is
 * what a cut-down entry looks like from in here. */
static int lz_inflate(FILE *in, FILE *out, unsigned int csize, uLong *crc, unsigned long *size,
                      char *err, int errlen)
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
            *crc = crc32(*crc, outbuf, (uInt)n);
            *size += (unsigned long)n;
        } while (z.avail_out == 0);
        if (rc == Z_STREAM_END)
            break;
        if (csize == 0 && got == 0)
            break;
    }
    inflateEnd(&z);
    if (rc != Z_STREAM_END) {
        snprintf(err, (size_t)errlen, "the archive is damaged (a file in it ends early)");
        return 0;
    }
    return 1;
}

static int lz_store(FILE *in, FILE *out, unsigned int csize, uLong *crc, unsigned long *size,
                    char *err, int errlen)
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
        *crc = crc32(*crc, buf, (uInt)got);
        *size += (unsigned long)got;
        csize -= (unsigned int)got;
    }
    return 1;
}

/* ------------------------------------------------------------------------ *
 * The wrapper probe: is everything inside one top-level folder?
 * ------------------------------------------------------------------------ */

typedef struct
{
    char root[256]; /* "CNC3D-windows-v0.6.11/", slash included */
    size_t n;
    int wrapped;
} LZ_Wrap;

static int lz_wrap_cb(void *user, FILE *f, const LZ_Entry *e, char *err, int errlen)
{
    LZ_Wrap *w = (LZ_Wrap *)user;
    const char *slash = strchr(e->name, '/');
    size_t n = slash ? (size_t)(slash - e->name) + 1 : 0;
    (void)f;
    (void)err;
    (void)errlen;

    if (n < 2 || n >= sizeof w->root) {
        w->wrapped = 0; /* a file at the top level: flat */
        return 0;
    }
    if (!w->n) {
        memcpy(w->root, e->name, n);
        w->root[n] = '\0';
        w->n = n;
        w->wrapped = 1;
        return 1;
    }
    if (n != w->n || strncmp(e->name, w->root, n) != 0) {
        w->wrapped = 0; /* a second top-level name: flat */
        return 0;
    }
    return 1;
}

int lz_wrapped(const char *zip)
{
    LZ_Wrap w;
    char err[256];
    memset(&w, 0, sizeof w);
    if (!lz_walk_cd(zip, lz_wrap_cb, &w, err, sizeof err))
        return 0;
    return w.wrapped;
}

/* ------------------------------------------------------------------------ *
 * The extractor.
 *
 * AN UPDATE IS UNPACKED OVER A FOLDER THAT IS IN USE, and on Windows that folder
 * refuses writes. The launcher is an SDL program, so while it runs it has
 * SDL2.dll loaded from the very folder it is updating, and Windows will not open
 * a loaded DLL or a running .exe for writing. A plain fopen(out, "wb") per entry
 * therefore stopped every Windows update that carried SDL2.dll at that entry, in
 * zip order, with everything before it written and everything after it not.
 * Six rules answer that, and they are one mechanism rather than six patches:
 *
 * 1. A FILE ALREADY IDENTICAL TO THE ENTRY IS NEVER OPENED FOR WRITING. The
 *    central directory carries each entry's CRC-32 and size, and a regular file
 *    on disk with the same size and CRC is left exactly as it is. SDL2.dll is
 *    byte identical across most releases, so in the common case the file that
 *    cannot be written is never asked to be. An update over a mostly unchanged
 *    folder is also faster, since reading a file is cheaper than inflating it.
 *
 * 2. A CHANGED FILE IS REPLACED BY TWO RENAMES, NEVER WRITTEN OVER. The new bytes
 *    go to "<name>.cnc3d-new" and are checked there. Only then is the existing
 *    file renamed to "<name>.old" (".old2" to ".old9" while the name before it
 *    is taken, whatever holds it) and the new one renamed onto the name that frees
 *    up. Windows allows a loaded DLL or a running .exe to be RENAMED, and whatever has it
 *    loaded goes on using it under the new name, which is how a genuinely new
 *    SDL2.dll, or launcher, installs underneath the launcher that has them loaded.
 *
 *    SO A NAME NEVER HOLDS A PARTIAL FILE. It holds the old file, then, for the
 *    moment between the two renames, nothing, then the whole new file, already
 *    checked and, on POSIX, already executable. That matters most for the files
 *    the recovery needs in order to run at all. On Windows nothing in the launcher
 *    can repair a half-written C&C3D.exe or SDL2.dll, because the launcher cannot
 *    start from them; on macOS the app starts the game rather than the launcher
 *    when the launcher is not executable.
 *
 *    IF A RENAME IS REFUSED FOR LONGER THAN A MOMENT, THE ENTRY FAILS and rule 4
 *    undoes the rest. On Windows the case that reaches it is a file another
 *    program holds open without delete sharing, which a game left running from
 *    the folder does with its data. Failing names the file and leaves the folder
 *    as it was.
 *
 * 3. EVERY CHANGE IS JOURNALLED BEFORE IT IS MADE. LZ_JOURNAL, in the folder
 *    being updated, gets one line per file before its new copy is even created:
 *    "N <name>" for a name that was free, "A <n> <name>" for a file that will be
 *    stepped aside to .old<n>. Once every entry has succeeded it gets "C". A line
 *    on disk therefore describes at most one step that did not happen, and undoing
 *    a step that did not happen is a no-op: a .old that is not there was never
 *    moved, a new file that is not there was never made. So the undo runs off the
 *    journal alone, and runs the same whether it is this call cleaning up after a
 *    failure or a later start cleaning up after a kill.
 *
 *    EACH LINE, AND EACH NEW COPY, IS FORCED TO THE DISK before the step that
 *    rests on it, not only handed to the system. lz_sync says what that buys.
 *
 * 4. A FAILURE PUTS BACK EVERY FILE ALREADY WRITTEN. Every changed file was
 *    stepped aside, so its old copy is still on disk: a failure part way (a file
 *    that cannot be stepped aside, a full disk, a damaged entry, the window
 *    closed mid-install) removes what this extraction wrote and renames the old
 *    copies back, newest first. Without it the release order, cnc3d.exe before
 *    TiberianDawn.dll, can leave a new game binary beside the old engine. A
 *    staging folder swapped in at the end would give the same guarantee with
 *    twice the disk and a folder swap Windows refuses while any file inside is
 *    held; the undo uses the copies rule 2 already leaves. The cost is that the
 *    old copies of changed files stay on disk until the update finishes.
 *
 *    A NEW COPY THAT WILL NOT GO does not strand the old one. The undo RENAMES
 *    it to a spare .old name rather than removing it. On Windows a removal while
 *    something still has the file open with delete sharing, which is how a
 *    scanner reads a file that was just written, reports success and leaves the
 *    name taken until the scanner lets go, so the old copy could not rename back
 *    onto it. A rename frees the name at once, and a refused one is retried for a
 *    moment. The spare's name is journalled first, as "S <j> <name>", so a spare
 *    that cannot be removed afterwards is removed later by that name (rule 6). Only
 *    when the rename too is refused does the undo stop short: the journal stays,
 *    `err` says which file, the old copy keeps its .old name, and the next start
 *    puts it back. Nothing removes a copy beside a file while such a journal
 *    remains.
 *
 * 5. THE COMMIT MARKERS ARE WRITTEN LAST, whatever the zip's order. The names in
 *    `last` are extracted in a second pass, once every other entry has
 *    succeeded, and the files the caller supplies (`extra`) after them, so the
 *    install record the launcher writes is the final step before "C" rather than
 *    an unjournalled rewrite after it. The packagers cannot be relied on for the
 *    order: zip -r lists files in whatever order the file system hands them out.
 *    After a kill, rule 3's undo waits for the next start, and the launcher runs
 *    it before it reads the record.
 *
 * 6. NOTHING OUTSIDE THE INSTALL IS EVER CHANGED, AND NOTHING INSIDE IT THAT NO
 *    UPDATE MADE. Every step that changes the disk works on a path made by
 *    lz_join, which refuses a name that is absolute or climbs out, and a name
 *    reached through a symbolic link or a junction. Every one of those steps
 *    needs a held LZ_Lock, and lz_lock refuses a folder that is not an install
 *    (lz_is_install). And the only files the launcher removes that it did not just
 *    write are the copies a journal line records its update making, each by the
 *    exact name the line gives (lz_settle_names): "<name>.old<n>" for "A <n>" and
 *    for the undo's "S <n>", and "<name>.cnc3d-new" for "A" and "N". Each of those
 *    names is free when its line is written: lz_free_old passes over a taken .old
 *    name rather than freeing it, and lz_put fails a file whose .cnc3d-new is taken
 *    rather than removing it. Nothing is looked for by pattern, not even beside a
 *    file the update carries: a player's own "cnc3d.old" or "notes.txt.old" is left
 *    where it is.
 * ------------------------------------------------------------------------ */

/* One journalled step, as rule 3 writes it and the undo reads it. */
typedef struct
{
    char how;       /* 'N': the name was free. 'A': the old file went to .old<n>.  */
                    /* 'S': the undo's spare, a new copy renamed to .old<n>        */
    int n;          /* 1 for ".old", 2 to 9 for ".old2" to ".old9"               */
    char rel[1024]; /* the name inside the folder, forward slashed as in the zip */
} LZ_Rec;

typedef struct
{
    const char *dest;
    int strip;
    const char *const *last;
    const LZ_File *extra;
    int pass; /* 0: every entry not named in `last`; 1: only those */
    LZ_Progress cb;
    void *user;
    int done;
    int naps; /* how long a refused rename is waited on, for the whole extraction */
    FILE *journal;
    LZ_Rec *rec; /* the journal, in memory, in order */
    int nrec, caprec;
} LZ_Ext;

/* Where rule 2 writes a new copy before it takes its name. */
#define LZ_NEW ".cnc3d-new"

/* The install record. Both release packagers write it into every package, and an
 * update rewrites it as its last file. */
#define LZ_RECORD "cnc3d-install.txt"

/* Extractions running in this process. lz_settle stands down while there is one:
 * every copy it would look at belongs to that extraction's undo. */
static int lz_busy;

static int lz_is_last(const char *rel, const char *const *last)
{
    for (; last && *last; last++)
        if (!strcmp(rel, *last))
            return 1;
    return 0;
}

/* Two paths naming one file on a case-insensitive disk, which both players'
 * platforms have by default. */
static int lz_same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z')
            ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z')
            cb += 'a' - 'A';
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

/* Whether the caller supplies its own copy of `rel`, which replaces the archive's. */
static int lz_is_extra(const char *rel, const LZ_File *extra)
{
    for (; extra && extra->name; extra++)
        if (lz_same_name(rel, extra->name))
            return 1;
    return 0;
}

/* A link to somewhere else: a symbolic link, or on Windows any reparse point, which
 * junctions are. POSIX answers that through lstat; Windows' stat follows both, so
 * it is asked for the reparse point directly. */
static int lz_is_link(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

/* THE ONE WAY A PATH INSIDE THE INSTALL IS MADE, for every step that changes the
 * disk (rule 6). `rel` has to pass lz_name_ok, so it is not absolute, names no
 * drive or stream and never climbs out with "..", and every folder on its way down
 * from `dir` that exists has to be a real folder: a symbolic link or a junction
 * there would carry every step below it to wherever it points. Returns 0 for a name
 * that fails either test: with `out` empty, or, when the reason is a folder on the
 * way that is a link or a file, with `out` ending at that folder, which is what
 * lz_way_err names.
 *
 * The last part of the name is not asked about, on purpose. Every step taken on it
 * acts on the name and never on what a link there points at: rename and remove
 * move or delete the link itself, a new copy is only created on a name removed just
 * before, and the reads that decide anything (lz_same_file, a journal, the record)
 * accept nothing but a plain file. */
static int lz_join(char *out, size_t outlen, const char *dir, const char *rel)
{
    char *p;
    int n;

    out[0] = '\0';
    if (!lz_name_ok(rel))
        return 0;
    n = snprintf(out, outlen, "%s%c%s", dir, LZ_SEP, rel);
    if (n < 0 || (size_t)n >= outlen) {
        out[0] = '\0';
        return 0;
    }
    for (p = out + strlen(dir) + 1; *p; p++) {
        struct stat st;
        char sep = *p;
        int bad;
#ifdef _WIN32
        if (sep != '/' && sep != '\\')
            continue;
#else
        if (sep != '/')
            continue;
#endif
        *p = '\0';
        if (LZ_LSTAT(out, &st) != 0) {
            /* Missing, so nothing below it exists either. Anything else is a folder
             * this cannot vouch for. */
            bad = errno != ENOENT;
            *p = sep;
            if (bad)
                out[0] = '\0';
            return !bad;
        }
        if (!S_ISDIR(st.st_mode) || lz_is_link(out))
            return 0; /* `out` now ends at that folder */
        *p = sep;
    }
    return 1;
}

/* The refusal for a name lz_join turned down, `out` being what it left. An install
 * whose missions folder, say, is a link is refused on every update until that is a
 * real folder again, so the message names the folder and says so. */
static void lz_way_err(char *err, int errlen, const char *verb, const char *rel, const char *dir,
                       const char *out)
{
    size_t n = strlen(dir);
    if (out[0] && !strncmp(out, dir, n) && out[n] && out[n + 1])
        snprintf(err, (size_t)errlen,
                 "could not %s %s: %s in the game folder is a link or a file, not a real folder. "
                 "Put a real folder in its place and update again",
                 verb, rel, out + n + 1);
    else
        snprintf(err, (size_t)errlen, "could not %s %s: a folder on its way could not be checked",
                 verb, rel);
}

static void lz_old_name(char *out, size_t outlen, const char *path, int n)
{
    if (n <= 1)
        snprintf(out, outlen, "%s.old", path);
    else
        snprintf(out, outlen, "%s.old%d", path, n);
}

static void lz_new_name(char *out, size_t outlen, const char *path)
{
    snprintf(out, outlen, "%s" LZ_NEW, path);
}

/* The length of the original name inside "<name>.old" or "<name>.old2"..9, or 0
 * for a name lz_free_old never hands out. */
static size_t lz_old_base(const char *name)
{
    size_t n = strlen(name);
    if (n > 5 && name[n - 1] >= '2' && name[n - 1] <= '9')
        n--;
    if (n > 4 && !strncmp(name + n - 4, ".old", 4))
        return n - 4;
    return 0;
}

/* A name the launcher keeps for itself: the journals, the download and the lock at
 * the top of the folder, and anywhere, a copy an update keeps beside a file. An
 * archive entry on one of those would be taken for the launcher's own bookkeeping
 * and removed by it, so it is refused instead. No release carries such a name. */
static int lz_reserved(const char *rel)
{
    static const char *const own[] = {LZ_JOURNAL, LZ_DONE, "cnc3d-update.zip",
                                      "cnc3d-update.zip.part", ".cnc3d-launcher.lock", NULL};
    const char *base = rel, *p;
    size_t n;
    int i;

    for (p = rel; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    for (i = 0; own[i]; i++)
        if (lz_same_name(rel, own[i]))
            return 1;
    n = strlen(base);
    if (n > sizeof LZ_NEW - 1 && lz_same_name(base + n - (sizeof LZ_NEW - 1), LZ_NEW))
        return 1;
    return lz_old_base(base) != 0;
}

/* Rule 1. Opened for READING only, which Windows allows on a loaded DLL. Only a
 * plain file can match: a link is replaced, never read through. */
static int lz_same_file(const char *path, unsigned int want_crc, unsigned long want_size)
{
    struct stat st;
    unsigned char buf[32 * 1024];
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t n;
    FILE *f;

    if (LZ_LSTAT(path, &st) != 0 || !S_ISREG(st.st_mode) || lz_is_link(path)
        || (unsigned long)st.st_size != want_size)
        return 0;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        crc = crc32(crc, buf, (uInt)n);
    fclose(f);
    return (unsigned int)(crc & 0xFFFFFFFFu) == want_crc;
}

/* Rule 2. The first of <path>.old, .old2 ... .old9 that is free. A taken name is
 * passed over, never freed: whatever is on it, a player's own copy or one an earlier
 * update could not remove, is not this update's to remove. So the name an "A <n>"
 * line records was free when the line was written. Returns 0 when all nine are
 * taken. */
static int lz_free_old(const char *path)
{
    char old[1216];
    int i;
    for (i = 1; i <= 9; i++) {
        lz_old_name(old, sizeof old, path, i);
        if (!lz_exists(old))
            return i;
    }
    return 0;
}

/* remove(), and then asked again: on Windows a removal refused, or one that only
 * marks the file for deletion because something still has it open, leaves the name
 * taken. Returns 1 while it is. */
static int lz_drop(const char *path)
{
    if (!lz_exists(path))
        return 0;
    remove(path);
    return lz_exists(path);
}

/* Rule 6's removal, for the lines of a journal whose update is over: exactly the copy
 * each line records its update making beside the file it names, and nothing else
 * there. "A <n>" made "<name>.old<n>", "S <j>" (the undo's spare) made
 * "<name>.old<j>", and "A" and "N" made "<name>.cnc3d-new". Each of those names was
 * free when its line was written (lz_free_old, lz_put, lz_undo), so what is on it is
 * that update's copy. Called only where no unfinished update can be relying on one
 * (lz_close, lz_recover). Nothing is removed while <name> itself is missing: such a
 * copy may then be all that is left of its file. Returns how many are still there. */
static int lz_settle_names(const char *dest, const LZ_Rec *rec, int nrec)
{
    int i, left = 0;
    for (i = 0; i < nrec; i++) {
        char path[1200], c[1232];
        if (!lz_join(path, sizeof path, dest, rec[i].rel) || !lz_exists(path))
            continue;
        if (rec[i].how != 'S') {
            lz_new_name(c, sizeof c, path);
            left += lz_drop(c);
        }
        if (rec[i].how != 'N') {
            lz_old_name(c, sizeof c, path, rec[i].n);
            left += lz_drop(c);
        }
    }
    return left;
}

/* Rule 3's durability: what `f` holds is handed to the disk, not only to the
 * system, whose cache a power cut empties. It is called on each journal line
 * before the step the line describes, and on each new copy before the rename that
 * gives it its name, so on the disk a journal line always comes before its step
 * and a file under its real name is always whole; and on the "C" line before any
 * old copy is removed.
 *
 * WHAT IT CANNOT PROMISE: that the drive keeps what it acknowledged (macOS fsync
 * asks the system, not the drive's own cache), or that a newly created journal's
 * directory entry reaches the disk with its first line. Those are the file
 * system's to order.
 *
 * Returns 0 only for a failure that means the bytes are not safe, an I/O error or
 * a full disk. A file system that cannot force a write at all is taken as it is,
 * as lz_lock takes one that cannot lock: refusing every update there would be
 * worse than the power cut it guards. */
static int lz_sync(FILE *f)
{
    int rc;
    if (fflush(f) != 0)
        return 0;
#ifdef _WIN32
    rc = _commit(_fileno(f));
#else
    rc = fsync(fileno(f));
#endif
    return rc == 0 || (errno != EIO && errno != ENOSPC);
}

/* Rule 3. The step goes into the journal on disk, and only then into memory, so
 * the two never disagree about a step that was taken. */
static int lz_note(LZ_Ext *x, char how, int n, const char *rel, char *err, int errlen)
{
    LZ_Rec *r;
    int w;

    if (x->nrec == x->caprec) {
        int cap = x->caprec ? x->caprec * 2 : 64;
        LZ_Rec *grown = (LZ_Rec *)realloc(x->rec, (size_t)cap * sizeof *grown);
        if (!grown) {
            snprintf(err, (size_t)errlen, "out of memory");
            return 0;
        }
        x->rec = grown;
        x->caprec = cap;
    }
    if (how == 'A')
        w = fprintf(x->journal, "A %d %s\n", n, rel);
    else
        w = fprintf(x->journal, "N %s\n", rel);
    if (w < 0 || !lz_sync(x->journal)) {
        snprintf(err, (size_t)errlen, "could not write the update journal in the game folder");
        return 0;
    }
    r = &x->rec[x->nrec++];
    r->how = how;
    r->n = n;
    snprintf(r->rel, sizeof r->rel, "%s", rel);
    return 1;
}

/* remove(), retried for a moment while refused. `naps` is shared across one undo,
 * so a folder full of files that will never let go costs two seconds, not two
 * seconds each. */
static int lz_remove_patiently(const char *path, int *naps)
{
    for (;;) {
        if (remove(path) == 0 || !lz_exists(path))
            return 1;
        if (*naps <= 0)
            return 0;
        (*naps)--;
        LZ_NAP();
    }
}

/* rename(), retried the same way. The destination is free: the callers make sure
 * of it, because Windows refuses a rename onto a taken name and POSIX would
 * replace whatever is there. */
static int lz_rename_patiently(const char *from, const char *to, int *naps)
{
    for (;;) {
        if (rename(from, to) == 0)
            return 1;
        if (*naps <= 0 || !lz_exists(from))
            return 0;
        (*naps)--;
        LZ_NAP();
    }
}

/* Rule 4's spare, journalled before it is taken, as every step is: "S <j> <name>"
 * says the undo is about to rename a new copy to "<name>.old<j>", so a copy left
 * there is removed later by that name and no other (rule 6). Appended to the journal
 * on disk, whose last line a kill may have cut off: the \001 ends any such piece with
 * a character no name may hold, so the reader drops the piece instead of reading it
 * as a shorter name. Returns 0 when the line did not reach the disk. */
static int lz_note_spare(const char *jpath, int j, const char *rel)
{
    FILE *f = fopen(jpath, "ab");
    int ok;
    if (!f)
        return 0;
    ok = fprintf(f, "\001\nS %d %s\n", j, rel) >= 0 && lz_sync(f);
    return fclose(f) == 0 && ok;
}

/* Rule 4. Undo journalled steps, newest first, so the folder unwinds in the order
 * it was wound. `jpath` is the journal on disk, where the spares are noted. Returns
 * how many could not be undone, naming the newest of them in `stuck`. Safe to run
 * twice over the same steps: see rule 3. */
static int lz_undo(const char *dest, const LZ_Rec *rec, int nrec, const char *jpath, char *stuck,
                   int stucklen)
{
    int i, left = 0, naps = 40;

    for (i = nrec - 1; i >= 0; i--) {
        char path[1200], tmp[1232], old[1216], spare[1216];
        int ok = 1;

        if (rec[i].how == 'S')
            continue; /* the undo's own note, not a step to undo */
        if (!lz_join(path, sizeof path, dest, rec[i].rel))
            continue; /* rule 6: not a name inside the install any more */
        /* A new copy cut off before its rename. Its name was free when the line was
         * written, so it is this update's; one that will not go is removed by that
         * name when the journal is closed (rule 6), rather than counted as stuck. */
        lz_new_name(tmp, sizeof tmp, path);
        if (lz_exists(tmp))
            lz_remove_patiently(tmp, &naps);

        if (rec[i].how == 'N') {
            ok = lz_remove_patiently(path, &naps);
        } else {
            lz_old_name(old, sizeof old, path, rec[i].n);
            if (!lz_exists(old))
                continue; /* never moved aside, or already put back */
            spare[0] = '\0';
            if (lz_exists(path)) {
                /* Out of the way by a RENAME, for the reason in rule 4, onto a free
                 * name noted first. The spare is removed once the old copy is back;
                 * if that is refused, or only marked, rule 6 removes it later by the
                 * name its note gives. */
                int j;
                ok = 0;
                for (j = 1; j <= 9 && !ok; j++) {
                    lz_old_name(spare, sizeof spare, path, j);
                    if (j == rec[i].n || lz_exists(spare))
                        continue;
                    if (!lz_note_spare(jpath, j, rec[i].rel)) {
                        /* No room even for the note, so no spare: the new copy,
                         * this update's own, is removed instead. */
                        lz_remove_patiently(path, &naps);
                        break;
                    }
                    if (lz_rename_patiently(path, spare, &naps))
                        ok = 1;
                    else
                        break; /* it is the file that refuses, not the name */
                }
                if (!ok) {
                    spare[0] = '\0';
                    ok = !lz_exists(path); /* gone, by itself or by the removal above */
                }
            }
            if (ok)
                ok = lz_rename_patiently(old, path, &naps);
            if (ok && spare[0])
                remove(spare);
        }
        if (!ok) {
            if (!left)
                snprintf(stuck, (size_t)stucklen, "%s", rec[i].rel);
            left++;
        }
    }
    return left;
}

/* A journal read back: LZ_JOURNAL, or LZ_DONE, inside `dir`. Returns -1 when there
 * is none, 0 when that name holds something this launcher did not write there (a
 * link, a folder) or cannot be read, and 1 with its steps in `*out`, which the
 * caller frees, and `*committed` set when it holds the "C" line.
 *
 * A step is kept only when its whole line reached the disk, since rule 3 writes a
 * line before it takes the step, and only when its name passes lz_join inside
 * `dir`. A line naming anything else is not one the extractor wrote, however it got
 * there, and nothing is done about it. */
static int lz_read_journal(const char *dir, const char *name, LZ_Rec **out, int *nout,
                           int *committed)
{
    char jpath[1200], line[1100], path[1200];
    struct stat st;
    LZ_Rec *rec = NULL;
    int nrec = 0, cap = 0, torn = 0;
    FILE *f;

    *out = NULL;
    *nout = 0;
    *committed = 0;
    if (!lz_join(jpath, sizeof jpath, dir, name))
        return 0;
    if (LZ_LSTAT(jpath, &st) != 0)
        return lz_exists(jpath) ? 0 : -1;
    if (!S_ISREG(st.st_mode) || lz_is_link(jpath))
        return 0;
    f = fopen(jpath, "rb");
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        const char *rel;
        char how;
        int old;

        /* A line with no newline was cut off by a kill, and rule 3 writes a line
         * before taking its step, so that step was never taken. A line too long
         * for the buffer arrives in pieces, and no piece of it is a line either. */
        if (!n || line[n - 1] != '\n') {
            torn = 1;
            continue;
        }
        if (torn) {
            torn = 0;
            continue;
        }
        line[--n] = '\0';
        if (!strcmp(line, "C")) {
            *committed = 1;
            continue;
        }
        if (line[0] == 'N' && line[1] == ' ') {
            how = 'N';
            old = 0;
            rel = line + 2;
        } else if ((line[0] == 'A' || line[0] == 'S') && line[1] == ' ' && line[2] >= '1'
                   && line[2] <= '9' && line[3] == ' ') {
            how = line[0];
            old = line[2] - '0';
            rel = line + 4;
        } else {
            continue; /* the header, or nothing this version wrote */
        }
        if (strlen(rel) >= sizeof rec->rel || !lz_join(path, sizeof path, dir, rel))
            continue;
        if (nrec == cap) {
            LZ_Rec *grown;
            cap = cap ? cap * 2 : 64;
            grown = (LZ_Rec *)realloc(rec, (size_t)cap * sizeof *grown);
            if (!grown) {
                fclose(f);
                free(rec);
                return 0;
            }
            rec = grown;
        }
        rec[nrec].how = how;
        rec[nrec].n = old;
        snprintf(rec[nrec].rel, sizeof rec[nrec].rel, "%s", rel);
        nrec++;
    }
    fclose(f);
    *out = rec;
    *nout = nrec;
    return 1;
}

/* The install record: a plain file, not a link, whatever it holds. Requiring a
 * "version" line bought no safety, since whoever can put a record in a folder can
 * write every file in it, and it cost every later update to a folder whose record a
 * launcher up to v0.6.11 was killed while rewriting in place, empty or cut short. */
static int lz_is_record(const char *path)
{
    struct stat st;
    return LZ_LSTAT(path, &st) == 0 && S_ISREG(st.st_mode) && !lz_is_link(path);
}

int lz_is_install(const char *dir)
{
    char path[1200], old[1216];
    LZ_Rec *rec;
    int nrec, committed, i, found = 0;

    if (!dir || !*dir || !lz_join(path, sizeof path, dir, LZ_RECORD))
        return 0;
    if (lz_is_record(path))
        return 1;
    /* The one moment an install has no record of its own: an update, or its undo,
     * between the two renames that replace the record. The journal line for that
     * step and the stepped-aside record are both on disk then, and only then. */
    if (lz_read_journal(dir, LZ_JOURNAL, &rec, &nrec, &committed) != 1)
        return 0;
    for (i = 0; i < nrec && !found; i++) {
        if (rec[i].how == 'A' && !strcmp(rec[i].rel, LZ_RECORD)) {
            lz_old_name(old, sizeof old, path, rec[i].n);
            found = lz_is_record(old);
        }
    }
    free(rec);
    return found;
}

/* The end of a journal whose update is over, committed or completely undone. The
 * copies its lines record are removed (rule 6), as read back from the journal on
 * disk rather than from memory, because the undo notes its spares there. While some
 * of them are refused (on Windows, a copy something still has loaded) the journal is
 * renamed to LZ_DONE, in one step, so that a later start removes them by those names
 * and the name LZ_JOURNAL goes on meaning only an update that did not finish. When
 * LZ_DONE is taken already the journal is removed instead, and the copies it recorded
 * stay where they are: a leak, not a hazard. Called only once no unfinished update
 * remains in `dir`: after a commit, or after an undo that put every file back, every
 * such copy is rubbish. */
static void lz_close(const char *dir, const char *jpath, const char *dpath)
{
    LZ_Rec *rec;
    int nrec, committed, left = 0;

    if (lz_read_journal(dir, LZ_JOURNAL, &rec, &nrec, &committed) == 1) {
        left = lz_settle_names(dir, rec, nrec);
        free(rec);
    }
    if (left == 0 || lz_exists(dpath) || rename(jpath, dpath) != 0)
        remove(jpath);
}

/* Finish what earlier updates left in `dir`. Returns -1 when there was nothing, 1
 * when all of it is settled, and 0, with a sentence for the player in `why`, while
 * an unfinished update cannot be undone yet, which leaves its journal in place.
 *
 * THE ORDER IS THE SAFETY. An unfinished journal, one with no "C", goes first and
 * is undone: until it is, the .old copies it names are the only good copies of
 * their files. Only once that undo has put every file back are the copies beside a
 * named file rubbish, so only then are LZ_DONE's names settled, and the journal
 * closed (lz_close). A start killed part way through any of it finds the journal
 * again, and undoing a step already undone does nothing (rule 3). */
static int lz_recover(const char *dir, char *why, int whylen)
{
    char jpath[1200], dpath[1200], stuck[1024];
    LZ_Rec *rec = NULL, *done = NULL;
    int nrec = 0, ndone = 0, committed = 0, dcommitted = 0, jrc, drc;

    if (!lz_join(jpath, sizeof jpath, dir, LZ_JOURNAL) || !lz_join(dpath, sizeof dpath, dir, LZ_DONE)) {
        snprintf(why, (size_t)whylen, "the game folder's path is too long");
        return 0;
    }
    jrc = lz_read_journal(dir, LZ_JOURNAL, &rec, &nrec, &committed);
    if (jrc == 0) {
        snprintf(why, (size_t)whylen, "%s in the game folder is not a journal this launcher can read",
                 LZ_JOURNAL);
        return 0;
    }
    if (jrc == 1 && !committed && lz_undo(dir, rec, nrec, jpath, stuck, sizeof stuck)) {
        free(rec);
        snprintf(why, (size_t)whylen,
                 "%s could not be put back, because something still has it open", stuck);
        return 0;
    }

    /* No unfinished update remains past this point. */
    drc = lz_read_journal(dir, LZ_DONE, &done, &ndone, &dcommitted);
    if (drc == 1 && lz_settle_names(dir, done, ndone) == 0)
        remove(dpath);
    free(done);

    if (jrc == 1) {
        free(rec);
        lz_close(dir, jpath, dpath);
        if (lz_exists(jpath)) {
            snprintf(why, (size_t)whylen, "its journal in the game folder could not be removed");
            return 0;
        }
    }
    return (jrc == 1 || drc == 1) ? 1 : -1;
}

static int lz_tick(LZ_Ext *x, const LZ_Entry *e, char *err, int errlen)
{
    x->done++;
    if (x->cb && !x->cb(x->user, x->done, (int)e->total)) {
        snprintf(err, (size_t)errlen, "cancelled");
        return -1;
    }
    return 1;
}

/* Rules 1 to 3 for one file: the archive entry `e`, read from `f`, or the caller's
 * `mem`. Returns 1 once the file is on its name, 0 having filled in `err`. */
static int lz_put(LZ_Ext *x, const char *rel, FILE *f, const LZ_Entry *e, const LZ_File *mem,
                  char *err, int errlen)
{
    char out[1200], tmp[1232], old[1216];
    unsigned int want_crc;
    unsigned long want_size, size;
    uLong crc;
    FILE *o;
    int ok, i, n = 0, exec = 0;

    if (mem) {
        want_crc = (unsigned int)(crc32(crc32(0L, Z_NULL, 0), (const Bytef *)mem->bytes,
                                        (uInt)mem->len)
                                  & 0xFFFFFFFFu);
        want_size = (unsigned long)mem->len;
    } else {
        want_crc = e->crc;
        want_size = (unsigned long)e->usize;
        /* The high 16 bits of the external attributes are the unix mode when the
         * zip was made on a unix host, which every zip this launcher fetches is.
         * The executable bit is the one that matters: without it the update leaves
         * a folder that looks complete and cannot start. */
        exec = ((e->attrs >> 16) & 0111) != 0;
    }
    (void)exec; /* only POSIX has an executable bit to set */

    /* Rule 6: a name inside the install, reached through real folders only. */
    if (!lz_join(out, sizeof out, x->dest, rel)) {
        lz_way_err(err, errlen, "replace", rel, x->dest, out);
        return 0;
    }

    /* The undo can only put a file back once. An archive naming the same file
     * twice would step aside its own first copy over the original, so it is
     * refused, as a hostile path is. Asked before anything below touches the name,
     * so nothing this extraction depends on is ever removed. */
    for (i = 0; i < x->nrec; i++) {
        if (lz_same_name(x->rec[i].rel, rel)) {
            snprintf(err, (size_t)errlen, "the archive lists %s twice", rel);
            return 0;
        }
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

    if (lz_same_file(out, want_crc, want_size)) {
#ifndef _WIN32
        /* Left alone, but not left unable to start. Its mode is changed where it
         * stands only when this is the file's one name: with a second name, the
         * file is also somewhere else, whose mode would change with it, so it is
         * replaced by a copy of its own instead. */
        struct stat st;
        if (!exec)
            return 1;
        if (lstat(out, &st) == 0
            && ((st.st_mode & 0100)
                || (st.st_nlink == 1 && chmod(out, (st.st_mode & 07777) | 0111) == 0)))
            return 1;
#else
        return 1;
#endif
    }

    if (e && !lz_seek_data(f, e->lho, err, errlen))
        return 0;

    /* Rule 6: the name the new copy goes on has to be free before this file is
     * journalled, so that what the undo and the closing settle later remove by that
     * name is only ever this update's copy. Whatever is on it already, a player's own
     * file or a copy an earlier update could not remove, is left alone, and the update
     * fails naming it. */
    lz_new_name(tmp, sizeof tmp, out);
    if (lz_exists(tmp)) {
        snprintf(err, (size_t)errlen,
                 "could not replace %s: %s" LZ_NEW " is in the way. Move it out of the game "
                 "folder and update again",
                 rel, rel);
        return 0;
    }

    /* Rule 3: the step is journalled before any part of it is taken. */
    if (lz_exists(out)) {
        n = lz_free_old(out);
        if (!n) {
            snprintf(err, (size_t)errlen,
                     "could not replace %s: every spare name beside it is in use", rel);
            return 0;
        }
        if (!lz_note(x, 'A', n, rel, err, errlen))
            return 0;
    } else if (!lz_note(x, 'N', 0, rel, err, errlen)) {
        return 0;
    }

    /* Rule 2: the new copy is made and checked on a name of its own, which was free a
     * moment ago and is removed again, so nothing put there since is written through. */
    remove(tmp);
    o = fopen(tmp, "wb");
    if (!o) {
        snprintf(err, (size_t)errlen, "could not write %s", rel);
        return 0;
    }
    crc = crc32(0L, Z_NULL, 0);
    size = 0;
    if (mem) {
        ok = fwrite(mem->bytes, 1, mem->len, o) == mem->len;
        if (!ok)
            snprintf(err, (size_t)errlen, "the disk would not take the new files");
        crc = crc32(crc, (const Bytef *)mem->bytes, (uInt)mem->len);
        size = (unsigned long)mem->len;
    } else if (e->method == 0) {
        ok = lz_store(f, o, e->csize, &crc, &size, err, errlen);
    } else if (e->method == 8) {
        ok = lz_inflate(f, o, e->csize, &crc, &size, err, errlen);
    } else {
        snprintf(err, (size_t)errlen, "the archive uses compression method %u", e->method);
        ok = 0;
    }
    /* A full disk can surface only here, when the last buffer is flushed, and
     * ignoring it would commit a truncated file. */
    if (ok && !lz_sync(o)) {
        snprintf(err, (size_t)errlen, "the disk would not take the new files");
        ok = 0;
    }
    if (fclose(o) != 0 && ok) {
        snprintf(err, (size_t)errlen, "the disk would not take the new files");
        ok = 0;
    }
    /* THE BYTES ARE HELD AGAINST THE ARCHIVE'S OWN RECORD. The SHA-256 of the whole
     * zip only exists when the release published a manifest, and a stored entry
     * with a flipped byte inflates without a murmur; the CRC-32 and the size in
     * the central directory are always there. */
    if (ok && ((unsigned int)(crc & 0xFFFFFFFFu) != want_crc || size != want_size)) {
        snprintf(err, (size_t)errlen, "the archive is damaged: %s does not match its checksum",
                 rel);
        ok = 0;
    }
#ifndef _WIN32
    if (ok && exec && chmod(tmp, 0755) != 0) {
        snprintf(err, (size_t)errlen, "could not make %s executable", rel);
        ok = 0;
    }
#endif
    if (!ok)
        return 0; /* the undo removes the copy: its step is in the journal */

    /* Only a whole, checked file ever takes the real name. */
    if (n) {
        lz_old_name(old, sizeof old, out, n);
        if (!lz_rename_patiently(out, old, &x->naps)) {
            snprintf(err, (size_t)errlen,
                     "could not replace %s: it could not be moved aside, so it was left alone",
                     rel);
            return 0;
        }
    }
    if (!lz_rename_patiently(tmp, out, &x->naps)) {
        snprintf(err, (size_t)errlen, "could not replace %s: the new copy could not take its name",
                 rel);
        return 0;
    }
    return 1;
}

static int lz_extract_cb(void *user, FILE *f, const LZ_Entry *e, char *err, int errlen)
{
    LZ_Ext *x = (LZ_Ext *)user;
    const char *rel;

    if (!lz_name_ok(e->name)) {
        snprintf(err, (size_t)errlen,
                 "the archive tries to write outside the game folder (%s)", e->name);
        return -1;
    }
    rel = lz_strip(e->name, x->strip);
    if (!rel)
        return 1;
    if (lz_is_last(rel, x->last) != x->pass)
        return 1; /* rule 5: this entry belongs to the other pass */
    if (lz_is_extra(rel, x->extra))
        return 1; /* rule 5: the caller's own copy replaces it, last of all */
    if (strlen(rel) >= sizeof x->rec->rel) {
        snprintf(err, (size_t)errlen, "the archive names a file with an absurd path");
        return -1;
    }
    if (lz_reserved(rel)) {
        snprintf(err, (size_t)errlen, "the archive names a file the launcher keeps for itself (%s)",
                 rel);
        return -1;
    }
    if (rel[strlen(rel) - 1] == '/') {
        char dir[1200];
        if (!lz_join(dir, sizeof dir, x->dest, rel)) {
            lz_way_err(err, errlen, "make", rel, x->dest, dir);
            return -1;
        }
        lz_mkdirs(dir);
        return lz_tick(x, e, err, errlen);
    }
    if (!lz_put(x, rel, f, e, NULL, err, errlen))
        return -1;
    return lz_tick(x, e, err, errlen);
}

int lz_extract(const LZ_Lock *install, const char *zip, int strip, const char *const *last,
               const LZ_File *extra, LZ_Progress cb, void *user, char *err, int errlen)
{
    LZ_Ext x;
    const LZ_File *m;
    const char *dest;
    char jpath[1200], dpath[1200], why[1200];
    int ok;

    /* Rule 6: an install, locked, and nothing else. */
    if (!install || !install->held) {
        snprintf(err, (size_t)errlen, "the game folder is not locked for the update");
        return 0;
    }
    dest = install->dir;

    /* An earlier update that was killed, or whose undo was refused, is finished
     * first. Starting over it would truncate the only record of which .old files
     * are the good copies. */
    if (lz_recover(dest, why, sizeof why) == 0) {
        snprintf(err, (size_t)errlen,
                 "an earlier update could not be undone: %s. Close whatever is using the "
                 "game folder and start the launcher again",
                 why);
        return 0;
    }

    memset(&x, 0, sizeof x);
    x.dest = dest;
    x.strip = strip;
    x.last = last;
    x.extra = extra;
    x.cb = cb;
    x.user = user;
    x.naps = 40;

    if (!lz_join(jpath, sizeof jpath, dest, LZ_JOURNAL) || !lz_join(dpath, sizeof dpath, dest, LZ_DONE)) {
        snprintf(err, (size_t)errlen, "the game folder's path is too long");
        return 0;
    }
    remove(jpath); /* a new journal is a new file, never written through the old name */
    x.journal = fopen(jpath, "wb");
    if (!x.journal || fputs("cnc3d update journal 1\n", x.journal) < 0 || !lz_sync(x.journal)) {
        if (x.journal) {
            fclose(x.journal);
            remove(jpath);
        }
        snprintf(err, (size_t)errlen, "could not write the update journal in the game folder");
        return 0;
    }

    lz_busy++;
    x.pass = 0;
    ok = lz_walk_cd(zip, lz_extract_cb, &x, err, errlen);
    if (ok && last) {
        x.pass = 1;
        ok = lz_walk_cd(zip, lz_extract_cb, &x, err, errlen);
    }
    for (m = extra; ok && m && m->name; m++) {
        if (!lz_name_ok(m->name) || strlen(m->name) >= sizeof x.rec->rel) {
            snprintf(err, (size_t)errlen, "the launcher was asked to write an unusable name (%s)",
                     m->name);
            ok = 0;
        } else {
            ok = lz_put(&x, m->name, NULL, NULL, m, err, errlen);
        }
    }
    if (ok && (fputs("C\n", x.journal) < 0 || !lz_sync(x.journal))) {
        snprintf(err, (size_t)errlen, "could not write the update journal in the game folder");
        ok = 0;
    }
    fclose(x.journal);

    if (ok) {
        /* Committed: every old copy is rubbish. Those still held keep their names
         * in LZ_DONE, for the next start. */
        lz_close(dest, jpath, dpath);
    } else {
        char stuck[1024];
        if (lz_undo(dest, x.rec, x.nrec, jpath, stuck, sizeof stuck) == 0) {
            lz_close(dest, jpath, dpath);
        } else {
            size_t n = strlen(err);
            if (n + 1 < (size_t)errlen)
                snprintf(err + n, (size_t)errlen - n,
                         "; %s could not be put back yet, and will be when the launcher next starts",
                         stuck);
        }
    }
    lz_busy--;
    free(x.rec);
    return ok;
}

void lz_settle(const LZ_Lock *install)
{
    char why[1200];
    if (!install || !install->held || lz_busy)
        return;
    lz_recover(install->dir, why, sizeof why);
}

/* ------------------------------------------------------------------------ *
 * The install lock.
 *
 * Two launchers on one folder is an ordinary thing to have: the installer's
 * finish page starts one, and a player double-clicks another. Either one's
 * recovery would put back or remove copies the other's update needs to undo
 * itself, and both would download into the same file. So each launcher holds this
 * for as long as it runs, and only the holder recovers or updates.
 *
 * IT IS ONLY EVER TAKEN ON AN INSTALL (rule 6). lz_is_install is asked before
 * anything else, so a folder that is not one gets no lock file, and no recovery,
 * settling or extraction, since each of those needs a held lock.
 *
 * On Windows it is a hidden file opened with no sharing at all and deleted on
 * close, so a second open fails with a sharing violation and a launcher that
 * dies, however it dies, takes the file with it. It is opened as a reparse point,
 * so a link planted on its name is what gets deleted, never the file the link
 * points at. On POSIX it is flock() on the folder itself, so nothing is created in
 * the install; a folder whose file system cannot lock is treated as lockable,
 * since refusing every update there would be worse than the race it guards.
 * ------------------------------------------------------------------------ */

#ifdef _WIN32

int lz_lock(const char *dir, LZ_Lock *lock)
{
    char path[1200];
    int tries;

    memset(lock, 0, sizeof *lock);
    lock->fd = -1;
    if (!lz_is_install(dir))
        return -2;
    if (strlen(dir) >= sizeof lock->dir || !lz_join(path, sizeof path, dir, ".cnc3d-launcher.lock"))
        return -1;
    for (tries = 0; tries < 5; tries++) {
        HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE
                                   | FILE_FLAG_OPEN_REPARSE_POINT,
                               NULL);
        DWORD why;
        if (h != INVALID_HANDLE_VALUE) {
            lock->handle = (void *)h;
            snprintf(lock->dir, sizeof lock->dir, "%s", dir);
            lock->held = 1;
            return 1;
        }
        why = GetLastError();
        if (why == ERROR_SHARING_VIOLATION)
            return 0;
        if (why != ERROR_ACCESS_DENIED)
            return -1;
        /* Also what a lock file looks like for the moment between its last holder
         * closing it and the system deleting it, so it is asked again. */
        Sleep(50);
    }
    return -1;
}

void lz_unlock(LZ_Lock *lock)
{
    if (lock->handle) {
        CloseHandle((HANDLE)lock->handle);
        lock->handle = NULL;
    }
    lock->held = 0;
}

#else

int lz_lock(const char *dir, LZ_Lock *lock)
{
    int flags = O_RDONLY, e;

#ifdef O_CLOEXEC
    flags |= O_CLOEXEC; /* the game the launcher starts must not inherit the lock */
#endif
    memset(lock, 0, sizeof *lock);
    lock->fd = -1;
    if (!lz_is_install(dir))
        return -2;
    if (strlen(dir) >= sizeof lock->dir)
        return -1;
    lock->fd = open(dir, flags);
    if (lock->fd < 0)
        return -1;
    e = flock(lock->fd, LOCK_EX | LOCK_NB) == 0 ? 0 : errno;
    if (e == 0 || e == EOPNOTSUPP
#if defined(ENOTSUP) && ENOTSUP != EOPNOTSUPP
        || e == ENOTSUP
#endif
    ) {
        snprintf(lock->dir, sizeof lock->dir, "%s", dir);
        lock->held = 1;
        return 1;
    }
    close(lock->fd);
    lock->fd = -1;
    return e == EWOULDBLOCK ? 0 : -1;
}

void lz_unlock(LZ_Lock *lock)
{
    if (lock->fd >= 0) {
        close(lock->fd);
        lock->fd = -1;
    }
    lock->held = 0;
}

#endif
