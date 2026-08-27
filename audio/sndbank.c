#include "sndbank.h"
#include "mixfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BANK_MAX_MIX 8
#define BANK_MAX_DIR 4
#define BANK_MAX_MISS 256
#define BANK_HASH 1024
#define BANK_DEFAULT_BUDGET (8L * 1024L * 1024L)

typedef struct Clip
{
    struct Clip *hnext; /* hash chain   */
    struct Clip *lru_prev, *lru_next;
    char name[24];
    SndClip info;
    short *pcm;
    long bytes;
    int pinned;
} Clip;

struct SndBank
{
    MixFile *mix[BANK_MAX_MIX];
    int nmix;
    char dir[BANK_MAX_DIR][512];
    int ndir;

    Clip *hash[BANK_HASH];
    Clip *lru_head, *lru_tail; /* head = most recent */
    long budget, used;
    int count;

    char miss[BANK_MAX_MISS][24];
    int nmiss;

    /* Every loose file that has actually WON a lookup, so the shadowing is loud.
     * Loose-beats-archive is a feature (a fix can be dropped in without rebaking),
     * but it is also the one path by which a sound that is not from the 1995 discs
     * could reach the mixer, so it never happens silently. */
    char loose[BANK_MAX_MISS][24];
    int nloose;
};

static unsigned int namehash(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

SndBank *bank_create(long cache_budget_bytes)
{
    SndBank *b = (SndBank *)calloc(1, sizeof(SndBank));
    if (!b)
        return NULL;
    b->budget = cache_budget_bytes > 0 ? cache_budget_bytes : BANK_DEFAULT_BUDGET;
    return b;
}

static void clip_free(SndBank *b, Clip *c)
{
    b->used -= c->bytes;
    b->count--;
    free(c->pcm);
    free(c);
}

void bank_destroy(SndBank *b)
{
    int i;
    if (!b)
        return;
    for (i = 0; i < BANK_HASH; i++) {
        Clip *c = b->hash[i];
        while (c) {
            Clip *n = c->hnext;
            free(c->pcm);
            free(c);
            c = n;
        }
    }
    for (i = 0; i < b->nmix; i++)
        mixfile_close(b->mix[i]);
    free(b);
}

int bank_add_mix(SndBank *b, const char *path, char *err, int errlen)
{
    MixFile *mx;
    if (b->nmix >= BANK_MAX_MIX) {
        snprintf(err, (size_t)errlen, "too many archives");
        return 0;
    }
    mx = mixfile_open(path, err, errlen);
    if (!mx)
        return 0;
    b->mix[b->nmix++] = mx;
    return 1;
}

int bank_add_dir(SndBank *b, const char *path)
{
    if (b->ndir >= BANK_MAX_DIR)
        return 0;
    strncpy(b->dir[b->ndir], path, sizeof b->dir[0] - 1);
    b->dir[b->ndir][sizeof b->dir[0] - 1] = 0;
    b->ndir++;
    return 1;
}

static void note_miss(SndBank *b, const char *name)
{
    int i;
    for (i = 0; i < b->nmiss; i++)
        if (strcmp(b->miss[i], name) == 0)
            return;
    if (b->nmiss < BANK_MAX_MISS) {
        strncpy(b->miss[b->nmiss], name, sizeof b->miss[0] - 1);
        b->miss[b->nmiss][sizeof b->miss[0] - 1] = 0;
        b->nmiss++;
    }
}

static int loose_path(SndBank *b, const char *name, int i, char *out, int outlen)
{
    if (i >= b->ndir)
        return 0;
    snprintf(out, (size_t)outlen, "%s/%s", b->dir[i], name);
    return 1;
}

/* A loose file beat the archives for this name. Said ONCE per name, on stdout where
 * the gates read, with the honest severity: a loose file that shadows an archive
 * entry has REPLACED a 1995 disc sound, and one that exists nowhere in the archives
 * is content from outside the discs entirely. */
static void note_loose(SndBank *b, const char *name, const char *path)
{
    int i, shadows = 0;
    for (i = 0; i < b->nloose; i++)
        if (strcmp(b->loose[i], name) == 0)
            return;
    if (b->nloose < BANK_MAX_MISS) {
        strncpy(b->loose[b->nloose], name, sizeof b->loose[0] - 1);
        b->loose[b->nloose][sizeof b->loose[0] - 1] = 0;
        b->nloose++;
    }
    for (i = 0; i < b->nmix; i++)
        if (mixfile_find(b->mix[i], name, NULL, NULL))
            shadows = 1;
    printf("AUDIO|loose|%s|%s|%s\n", name, path,
           shadows ? "SHADOWS a 1995 archive entry" : "NOT in any archive: outside the discs");
    fflush(stdout);
}

static AudStream *open_anywhere(SndBank *b, const char *name, char *err, int errlen)
{
    char path[600];
    int i;
    AudStream *a;

    for (i = 0; i < b->ndir; i++) {
        FILE *probe;
        loose_path(b, name, i, path, (int)sizeof path);
        probe = fopen(path, "rb");
        if (probe) {
            fclose(probe);
            a = aud_open_file(path, err, errlen);
            if (a) {
                note_loose(b, name, path);
                return a;
            }
        }
    }
    for (i = 0; i < b->nmix; i++) {
        long off, size;
        if (mixfile_find(b->mix[i], name, &off, &size)) {
            a = aud_open_mix(b->mix[i], name, err, errlen);
            if (a)
                return a;
        }
    }
    snprintf(err, (size_t)errlen, "%s: not in any archive or directory", name);
    return NULL;
}

int bank_has(SndBank *b, const char *name)
{
    char path[600];
    int i;
    for (i = 0; i < b->ndir; i++) {
        FILE *probe;
        loose_path(b, name, i, path, (int)sizeof path);
        probe = fopen(path, "rb");
        if (probe) {
            fclose(probe);
            return 1;
        }
    }
    for (i = 0; i < b->nmix; i++)
        if (mixfile_find(b->mix[i], name, NULL, NULL))
            return 1;
    return 0;
}

AudStream *bank_open_stream(SndBank *b, const char *name, char *err, int errlen)
{
    AudStream *a = open_anywhere(b, name, err, errlen);
    if (!a)
        note_miss(b, name);
    return a;
}

/* ---------------------------------------------------------------- LRU list */

static void lru_unlink(SndBank *b, Clip *c)
{
    if (c->lru_prev)
        c->lru_prev->lru_next = c->lru_next;
    else
        b->lru_head = c->lru_next;
    if (c->lru_next)
        c->lru_next->lru_prev = c->lru_prev;
    else
        b->lru_tail = c->lru_prev;
    c->lru_prev = c->lru_next = NULL;
}

static void lru_push_front(SndBank *b, Clip *c)
{
    c->lru_prev = NULL;
    c->lru_next = b->lru_head;
    if (b->lru_head)
        b->lru_head->lru_prev = c;
    b->lru_head = c;
    if (!b->lru_tail)
        b->lru_tail = c;
}

static void hash_unlink(SndBank *b, Clip *c)
{
    unsigned int h = namehash(c->name) % BANK_HASH;
    Clip **pp = &b->hash[h];
    while (*pp) {
        if (*pp == c) {
            *pp = c->hnext;
            return;
        }
        pp = &(*pp)->hnext;
    }
}

static void evict_to_budget(SndBank *b)
{
    while (b->used > b->budget && b->lru_tail) {
        Clip *c = b->lru_tail;
        /* Walk back past pinned clips. */
        while (c && c->pinned)
            c = c->lru_prev;
        if (!c)
            break;
        lru_unlink(b, c);
        hash_unlink(b, c);
        clip_free(b, c);
    }
}

static Clip *find_clip(SndBank *b, const char *name)
{
    unsigned int h = namehash(name) % BANK_HASH;
    Clip *c = b->hash[h];
    while (c) {
        /* names are stored uppercase-insensitive; compare case folded */
        const char *p = c->name, *q = name;
        while (*p && *q) {
            char a = *p, d = *q;
            if (a >= 'a' && a <= 'z')
                a = (char)(a - 'a' + 'A');
            if (d >= 'a' && d <= 'z')
                d = (char)(d - 'a' + 'A');
            if (a != d)
                break;
            p++;
            q++;
        }
        if (*p == 0 && *q == 0)
            return c;
        c = c->hnext;
    }
    return NULL;
}

/* -------------------------------------------------------------- resampling */

/* Linear interpolation from src_rate to BANK_RATE, mono. Cheap, branch free in the
 * inner loop, and fine for the four odd rate files on the disc (22222 Hz is 0.78 %
 * off and 44100 Hz is an exact halving). 16.16 fixed point so the Win98 build never
 * needs a float. */
static short *resample_mono(const short *src, long n, int src_rate, long *out_n)
{
    long i, m;
    unsigned int step, phase;
    short *dst;

    if (src_rate == BANK_RATE) {
        dst = (short *)malloc((size_t)(n > 0 ? n : 1) * sizeof(short));
        if (!dst)
            return NULL;
        memcpy(dst, src, (size_t)n * sizeof(short));
        *out_n = n;
        return dst;
    }

    step = (unsigned int)(((double)src_rate * 65536.0) / (double)BANK_RATE + 0.5);
    if (step == 0)
        step = 65536;
    m = (long)(((double)n * 65536.0) / (double)step);
    if (m < 1)
        m = 1;
    dst = (short *)malloc((size_t)m * sizeof(short));
    if (!dst)
        return NULL;

    phase = 0;
    for (i = 0; i < m; i++) {
        long idx = (long)(phase >> 16);
        unsigned int frac = phase & 0xFFFFu;
        int a = (idx < n) ? src[idx] : 0;
        int c = (idx + 1 < n) ? src[idx + 1] : a;
        dst[i] = (short)(a + (int)(((long)(c - a) * (long)frac) >> 16));
        phase += step;
    }
    *out_n = m;
    return dst;
}

static short *downmix(const short *src, long n, long *out_n)
{
    long i, m = n / 2;
    short *dst = (short *)malloc((size_t)(m > 0 ? m : 1) * sizeof(short));
    if (!dst)
        return NULL;
    for (i = 0; i < m; i++)
        dst[i] = (short)(((int)src[i * 2] + (int)src[i * 2 + 1]) / 2);
    *out_n = m;
    return dst;
}

const SndClip *bank_get(SndBank *b, const char *name)
{
    char err[256];
    Clip *c = find_clip(b, name);
    AudStream *a;
    short *raw = NULL, *mono, *final_pcm;
    long n, mn, fn;

    if (c) {
        lru_unlink(b, c);
        lru_push_front(b, c);
        return &c->info;
    }

    a = open_anywhere(b, name, err, (int)sizeof err);
    if (!a) {
        note_miss(b, name);
        return NULL;
    }

    n = aud_decode_all(a, &raw);
    if (n <= 0) {
        free(raw);
        aud_close(a);
        note_miss(b, name);
        return NULL;
    }

    c = (Clip *)calloc(1, sizeof(Clip));
    if (!c) {
        free(raw);
        aud_close(a);
        return NULL;
    }
    strncpy(c->name, name, sizeof c->name - 1);
    c->info.src_rate = aud_rate(a);
    c->info.src_bits = aud_bits(a);
    c->info.src_channels = aud_channels(a);
    c->info.src_comp = aud_compression(a);

    if (aud_channels(a) == 2) {
        mono = downmix(raw, n, &mn);
        free(raw);
        if (!mono) {
            free(c);
            aud_close(a);
            return NULL;
        }
    } else {
        mono = raw;
        mn = n;
    }

    final_pcm = resample_mono(mono, mn, aud_rate(a), &fn);
    free(mono);
    aud_close(a);
    if (!final_pcm) {
        free(c);
        return NULL;
    }

    c->pcm = final_pcm;
    c->bytes = fn * (long)sizeof(short);
    c->info.pcm = final_pcm;
    c->info.samples = fn;

    {
        unsigned int h = namehash(name) % BANK_HASH;
        c->hnext = b->hash[h];
        b->hash[h] = c;
    }
    lru_push_front(b, c);
    b->used += c->bytes;
    b->count++;
    evict_to_budget(b);
    return &c->info;
}

void bank_pin(SndBank *b, const char *name, int pinned)
{
    Clip *c = find_clip(b, name);
    if (c)
        c->pinned = pinned ? 1 : 0;
}

long bank_cache_bytes(const SndBank *b) { return b ? b->used : 0; }
int bank_clip_count(const SndBank *b) { return b ? b->count : 0; }
int bank_miss_count(const SndBank *b) { return b ? b->nmiss : 0; }
const char *bank_miss(const SndBank *b, int i)
{
    if (!b || i < 0 || i >= b->nmiss)
        return "";
    return b->miss[i];
}
