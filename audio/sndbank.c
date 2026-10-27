#include "sndbank.h"
#include "mixfile.h"
#include "halfband.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BANK_MAX_MIX 12
#define BANK_MAX_DIR 4
#define BANK_MAX_MISS 256
#define BANK_HASH 1024
/* 8 MB of 22050 Hz clips; the same clips take twice the room at 44100 Hz. */
#define BANK_DEFAULT_BUDGET (8L * 1024L * 1024L * (BANK_RATE / 22050))
#define NEW_AUDIO_VARIANTS 16 /* BOMB1.wav plus BOMB1_2.wav .. BOMB1_16.wav */

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
    /* A LANGUAGE'S ARCHIVE (bank_add_language): searched before the English ones, except
       for the death sounds, which stay English (bank_keep_english). */
    unsigned char lang[BANK_MAX_MIX];
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

    /* NEW AUDIO: replacement sounds, one WAV per 1995 name, consulted before anything
     * else while the switch is on. The probe list remembers every name already looked
     * for and which of its variants were there, so a sound with no replacement costs
     * one look per session rather than one per shot. Turning the switch on clears it, so
     * a file dropped in while the game runs is found on the next toggle. */
    char newdir[512];
    int new_on;
    char probed[BANK_MAX_MISS][24];
    unsigned int probed_have[BANK_MAX_MISS]; /* bit k-1 set: variant k is on disk */
    int nprobed;
    unsigned int pick; /* xorshift state for choosing between variants */
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

static void lru_unlink(SndBank *b, Clip *c);
static void hash_unlink(SndBank *b, Clip *c);
static int bank_keep_english(const char *name);

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

/* Every clip not pinned, out of the cache: what it held may now come from another archive. */
static void bank_forget(SndBank *b)
{
    Clip *c = b->lru_head;
    while (c) {
        Clip *n = c->lru_next;
        if (!c->pinned) {
            lru_unlink(b, c);
            hash_unlink(b, c);
            clip_free(b, c);
        }
        c = n;
    }
}

int bank_add_language(SndBank *b, const char *path, char *err, int errlen)
{
    MixFile *mx;
    int i;
    if (b->nmix >= BANK_MAX_MIX) {
        snprintf(err, (size_t)errlen, "too many archives");
        return 0;
    }
    mx = mixfile_open(path, err, errlen);
    if (!mx)
        return 0;
    for (i = b->nmix; i > 0; i--) { b->mix[i] = b->mix[i - 1]; b->lang[i] = b->lang[i - 1]; }
    b->mix[0] = mx;
    b->lang[0] = 1;
    b->nmix++;
    bank_forget(b);
    return 1;
}

int bank_drop_languages(SndBank *b)
{
    int i, w = 0, dropped = 0;
    for (i = 0; i < b->nmix; i++) {
        if (b->lang[i]) { mixfile_close(b->mix[i]); dropped++; continue; }
        b->mix[w] = b->mix[i]; b->lang[w] = 0; w++;
    }
    b->nmix = w;
    if (dropped) bank_forget(b);
    return dropped;
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
        if (b->lang[i] && bank_keep_english(name))
            continue;
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
        if (!(b->lang[i] && bank_keep_english(name)) && mixfile_find(b->mix[i], name, NULL, NULL))
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

/* THE DEATH SOUNDS STAY ENGLISH. The German releases replaced every scream with a crunch
   or a thud in the archive that also holds their unit voices; a language pack is taken
   for its words, so these names are never answered from one. Tiberian Dawn's NUYELL1-12,
   YELL1, RAMYELL1 and SQUISH2, Red Alert's DEDMAN1-10 and SQUISHY2. */
static int bank_keep_english(const char *name)
{
    static const char *const deaths[] = { "NUYELL", "YELL", "RAMYELL", "SQUISH", "DEDMAN", 0 };
    int i;
    for (i = 0; deaths[i]; i++) {
        const char *p = deaths[i], *q = name;
        while (*p && *q && (*q == *p || *q == *p + 32)) { p++; q++; }
        if (!*p) return 1;
    }
    return 0;
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

/* Any mono clip to BANK_RATE.
 *
 * EXACTLY TWICE (every 22050 Hz file on the 1995 discs, into a 44100 Hz bank) goes
 * through halfband.h, the same fixed integer filter the music and movie streams use:
 * each source sample is kept and one windowed-sinc sample is made between each pair,
 * so the 1995 sound is carried over unchanged below 11 kHz.
 *
 * ANY OTHER RATIO (the four 22222 Hz files, and a replacement sound that was not
 * converted to 44100 before it was added) uses a Kaiser-windowed sinc, 16 zero
 * crossings a side, from a kernel table built once. Going down in rate the kernel is
 * widened so nothing above the new Nyquist folds back as aliasing. Each output is
 * normalised by its own weights, so a constant stays exactly constant. This path is
 * floating point and runs once per clip at load, never in the mix. */
#define SINC_ZEROS 16
#define SINC_RES 512 /* table points per unit */
#define SINC_BETA 8.0

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    int k;
    for (k = 1; k < 40; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

static const double *sinc_table(void)
{
    static double tab[SINC_ZEROS * SINC_RES + 2];
    static int built = 0;
    if (!built) {
        int i;
        const double pi = 3.14159265358979323846;
        const double i0b = bessel_i0(SINC_BETA);
        for (i = 0; i < SINC_ZEROS * SINC_RES + 2; i++) {
            double u = (double)i / SINC_RES, r = u / SINC_ZEROS;
            double sinc = u == 0.0 ? 1.0 : sin(pi * u) / (pi * u);
            tab[i] = r >= 1.0 ? 0.0 : sinc * bessel_i0(SINC_BETA * sqrt(1.0 - r * r)) / i0b;
        }
        built = 1;
    }
    return tab;
}

static short *resample_mono(const short *src, long n, int src_rate, long *out_n)
{
    long i, m;
    short *dst;

    if (src_rate == BANK_RATE) {
        dst = (short *)malloc((size_t)(n > 0 ? n : 1) * sizeof(short));
        if (!dst)
            return NULL;
        memcpy(dst, src, (size_t)n * sizeof(short));
        *out_n = n;
        return dst;
    }

    if (src_rate * 2 == BANK_RATE) {
        /* Padded with silence both ends, which is what the clip is surrounded by. */
        short *pad = (short *)calloc((size_t)(n + 2 * HB_TAPS), sizeof(short));
        if (!pad)
            return NULL;
        memcpy(pad + HB_TAPS, src, (size_t)n * sizeof(short));
        m = n * 2;
        dst = (short *)malloc((size_t)(m > 0 ? m : 1) * sizeof(short));
        if (!dst) {
            free(pad);
            return NULL;
        }
        for (i = 0; i < n; i++) {
            dst[i * 2] = src[i];
            HB_MID(pad + HB_TAPS + i, dst[i * 2 + 1]);
        }
        free(pad);
        *out_n = m;
        return dst;
    }

    {
        const double *tab = sinc_table();
        const double ratio = (double)src_rate / (double)BANK_RATE; /* source per output */
        const double fc = ratio > 1.0 ? 1.0 / ratio : 1.0;         /* cutoff, in source Nyquists */
        const double reach = SINC_ZEROS / fc;                      /* source samples a side */
        m = (long)((double)n / ratio);
        if (m < 1)
            m = 1;
        dst = (short *)malloc((size_t)m * sizeof(short));
        if (!dst)
            return NULL;
        for (i = 0; i < m; i++) {
            const double t = (double)i * ratio;
            long j = (long)ceil(t - reach), j1 = (long)floor(t + reach);
            double acc = 0.0, wsum = 0.0, v;
            for (; j <= j1; j++) {
                double u = fabs(t - (double)j) * fc * SINC_RES, w;
                long ui = (long)u;
                if (ui >= SINC_ZEROS * SINC_RES)
                    continue;
                w = tab[ui] + (tab[ui + 1] - tab[ui]) * (u - (double)ui);
                wsum += w;
                if (j >= 0 && j < n)
                    acc += w * (double)src[j];
            }
            v = wsum != 0.0 ? acc / wsum : 0.0;
            dst[i] = (short)(v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : (v < 0 ? v - 0.5 : v + 0.5));
        }
        *out_n = m;
        return dst;
    }
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

/* ---------------------------------------------------------------- new audio */

static unsigned int rd16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

/* A RIFF/WAVE file to signed 16 bit, channels interleaved as stored. Integer PCM at 8,
 * 16, 24 or 32 bits and 32 bit float, which between them cover what a sound library
 * exports. Anything else is refused by name rather than guessed at. */
static short *load_wav(const char *path, long *frames, int *rate, int *channels, int *bits,
                       char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf = NULL, *p, *end;
    unsigned char *data = NULL;
    long size, i, count;
    unsigned int fmt = 0, ch = 0, sr = 0, bps = 0, datalen = 0;
    short *out;

    if (!f) {
        snprintf(err, (size_t)errlen, "cannot open");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 12 || !(buf = (unsigned char *)malloc((size_t)size)) ||
        fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        snprintf(err, (size_t)errlen, "cannot read");
        return NULL;
    }
    fclose(f);
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        free(buf);
        snprintf(err, (size_t)errlen, "not a RIFF/WAVE file");
        return NULL;
    }
    p = buf + 12;
    end = buf + size;
    while (p + 8 <= end) {
        unsigned int len = rd32(p + 4);
        unsigned char *body = p + 8;
        if (len > (unsigned int)(end - body))
            len = (unsigned int)(end - body);
        if (memcmp(p, "fmt ", 4) == 0 && len >= 16) {
            fmt = rd16(body);
            ch = rd16(body + 2);
            sr = rd32(body + 4);
            bps = rd16(body + 14);
            if (fmt == 0xFFFE && len >= 26) /* WAVE_FORMAT_EXTENSIBLE: the real tag */
                fmt = rd16(body + 24);
        } else if (memcmp(p, "data", 4) == 0) {
            data = body;
            datalen = len;
        }
        p = body + len + (len & 1);
    }
    if (!data || !ch || !sr || !((fmt == 1 && (bps == 8 || bps == 16 || bps == 24 || bps == 32)) ||
                                (fmt == 3 && bps == 32))) {
        free(buf);
        snprintf(err, (size_t)errlen, "unsupported WAV (format %u, %u bit, %u channels)", fmt, bps,
                 ch);
        return NULL;
    }
    count = (long)(datalen / (bps / 8));
    out = (short *)malloc((size_t)(count > 0 ? count : 1) * sizeof(short));
    if (!out) {
        free(buf);
        return NULL;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *s = data + i * (long)(bps / 8);
        long v;
        if (fmt == 3) {
            union { unsigned int u; float fl; } cv;
            float x;
            cv.u = rd32(s);
            x = cv.fl;
            if (x > 1.0f) x = 1.0f;
            if (x < -1.0f) x = -1.0f;
            v = (long)(x * 32767.0f);
        } else if (bps == 8) {
            v = ((long)s[0] - 128) << 8;
        } else if (bps == 16) {
            v = (long)(short)rd16(s);
        } else if (bps == 24) {
            v = (long)(((int)((unsigned int)s[0] << 8 | (unsigned int)s[1] << 16 |
                              (unsigned int)s[2] << 24)) >> 16);
        } else {
            v = (long)(((int)rd32(s)) >> 16);
        }
        out[i] = (short)v;
    }
    free(buf);
    *frames = count / (long)ch;
    *rate = (int)sr;
    *channels = (int)ch;
    *bits = (int)bps;
    return out;
}

void bank_set_new_audio_dir(SndBank *b, const char *dir)
{
    if (!b)
        return;
    snprintf(b->newdir, sizeof b->newdir, "%s", dir ? dir : "");
    b->nprobed = 0;
    b->pick = 2463534242u;
}

void bank_set_new_audio(SndBank *b, int on)
{
    if (!b || (on ? 1 : 0) == b->new_on)
        return;
    b->new_on = on ? 1 : 0;
    if (b->new_on)
        b->nprobed = 0;
    printf("AUDIO|newaudio|%s|%s\n", b->new_on ? "ON" : "off",
           b->newdir[0] ? b->newdir : "(no folder found)");
    fflush(stdout);
}

int bank_new_audio(const SndBank *b) { return b ? b->new_on : 0; }

/* ONE REPLACEMENT'S CLIP, loaded from path and cached under key. NULL when the file
 * will not load, which is said once, with the reason. */
static const SndClip *new_audio_load(SndBank *b, const char *name, const char *key,
                                     const char *path)
{
    char err[128];
    int rate = 0, ch = 0, bits = 0;
    long frames = 0, fn;
    short *raw, *mono, *final_pcm;
    Clip *c = find_clip(b, key);

    if (c) {
        lru_unlink(b, c);
        lru_push_front(b, c);
        return &c->info;
    }
    raw = load_wav(path, &frames, &rate, &ch, &bits, err, (int)sizeof err);
    if (!raw || frames <= 0) {
        free(raw);
        printf("AUDIO|newaudio|%s|%s|REFUSED, %s\n", name, path, raw ? "empty" : err);
        fflush(stdout);
        return NULL;
    }
    if (ch > 1) { /* average every channel into one */
        long k;
        mono = (short *)malloc((size_t)frames * sizeof(short));
        if (!mono) {
            free(raw);
            return NULL;
        }
        for (k = 0; k < frames; k++) {
            long sum = 0;
            int j;
            for (j = 0; j < ch; j++)
                sum += raw[k * ch + j];
            mono[k] = (short)(sum / ch);
        }
        free(raw);
    } else {
        mono = raw;
    }
    final_pcm = resample_mono(mono, frames, rate, &fn);
    free(mono);
    if (!final_pcm)
        return NULL;
    c = (Clip *)calloc(1, sizeof(Clip));
    if (!c) {
        free(final_pcm);
        return NULL;
    }
    snprintf(c->name, sizeof c->name, "%s", key);
    c->info.src_rate = rate;
    c->info.src_bits = bits;
    c->info.src_channels = ch;
    c->info.src_comp = 0;
    c->pcm = final_pcm;
    c->bytes = fn * (long)sizeof(short);
    c->info.pcm = final_pcm;
    c->info.samples = fn;
    {
        unsigned int h = namehash(key) % BANK_HASH;
        c->hnext = b->hash[h];
        b->hash[h] = c;
    }
    lru_push_front(b, c);
    b->used += c->bytes;
    b->count++;
    printf("AUDIO|newaudio|%s|%s|%d Hz %d ch %d bit, %.2f s\n", name, path, rate, ch, bits,
           (double)frames / rate);
    fflush(stdout);
    evict_to_budget(b);
    return &c->info;
}

/* Variant k of a base name: k = 1 is BOMB1.wav, k = 2..NEW_AUDIO_VARIANTS is BOMB1_2.wav
 * and on. The cache key "~k:NAME" keeps every variant and the 1995 clip apart. */
static void new_audio_paths(const SndBank *b, const char *name, int k, char *key, int keylen,
                            char *path, int pathlen)
{
    char base[16];
    int i;
    for (i = 0; name[i] && name[i] != '.' && i < (int)sizeof base - 1; i++)
        base[i] = name[i];
    base[i] = 0;
    snprintf(key, (size_t)keylen, "~%d:%s", k, name);
    if (k == 1)
        snprintf(path, (size_t)pathlen, "%s/%s.wav", b->newdir, base);
    else
        snprintf(path, (size_t)pathlen, "%s/%s_%d.wav", b->newdir, base, k);
}

/* The replacement for a 1995 name, or NULL when there is none or the switch is off.
 * Keyed on the BASE name, so XPLOS.AUD and its juvenile XPLOS.JUV both take XPLOS.wav.
 *
 * VARIANTS. BOMB1.wav, BOMB1_2.wav, BOMB1_3.wav and so on up to _16 form one pool, and
 * every request draws one of them at random, so a sound heard over and over is not the
 * same take every time. A gap does not end the pool (a _4 without a _3 still counts),
 * and a pool with no plain BOMB1.wav is just its numbered files. The draw is from the
 * bank's own generator, never rand(), so it cannot disturb anything else that draws
 * numbers, and a recorded run is the same run twice. A file that will not load is
 * dropped from its pool and the draw is made again from what is left. */
static const SndClip *new_audio_get(SndBank *b, const char *name)
{
    char key[32], path[600];
    int i, k, slot = -1, n;

    if (!b->new_on || !b->newdir[0])
        return NULL;
    for (i = 0; i < b->nprobed; i++)
        if (strcmp(b->probed[i], name) == 0) {
            slot = i;
            break;
        }
    if (slot < 0) {
        unsigned int have = 0;
        for (k = 1; k <= NEW_AUDIO_VARIANTS; k++) {
            FILE *probe;
            new_audio_paths(b, name, k, key, (int)sizeof key, path, (int)sizeof path);
            probe = fopen(path, "rb");
            if (probe) {
                fclose(probe);
                have |= 1u << (k - 1);
            }
        }
        if (b->nprobed >= BANK_MAX_MISS)
            return NULL; /* a table this full is a bug elsewhere; the 1995 sound plays */
        slot = b->nprobed++;
        snprintf(b->probed[slot], sizeof b->probed[0], "%s", name);
        b->probed_have[slot] = have;
    }
    while (b->probed_have[slot]) {
        const unsigned int have = b->probed_have[slot];
        const SndClip *clip;
        int pick;
        for (n = 0, k = 0; k < NEW_AUDIO_VARIANTS; k++)
            n += (have >> k) & 1u;
        b->pick ^= b->pick << 13;
        b->pick ^= b->pick >> 17;
        b->pick ^= b->pick << 5;
        pick = (int)(b->pick % (unsigned int)n);
        for (k = 0; k < NEW_AUDIO_VARIANTS; k++)
            if (((have >> k) & 1u) && pick-- == 0)
                break;
        new_audio_paths(b, name, k + 1, key, (int)sizeof key, path, (int)sizeof path);
        clip = new_audio_load(b, name, key, path);
        if (clip)
            return clip;
        b->probed_have[slot] &= ~(1u << k);
    }
    return NULL;
}

const SndClip *bank_get(SndBank *b, const char *name)
{
    char err[256];
    Clip *c;
    AudStream *a;
    short *raw = NULL, *mono, *final_pcm;
    long n, mn, fn;
    const SndClip *repl = new_audio_get(b, name);

    if (repl)
        return repl;
    c = find_clip(b, name);
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
    char key[32];
    Clip *c = find_clip(b, name);
    if (c)
        c->pinned = pinned ? 1 : 0;
    /* A voice may be playing a NEW AUDIO replacement under this name instead, any of
     * its variants, and the switch can flip while it rings, so the pin covers them all. */
    {
        int k;
        for (k = 1; k <= NEW_AUDIO_VARIANTS; k++) {
            snprintf(key, sizeof key, "~%d:%s", k, name);
            c = find_clip(b, key);
            if (c)
                c->pinned = pinned ? 1 : 0;
        }
    }
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
