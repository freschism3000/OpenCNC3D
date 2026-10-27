/*
 * lmedia.c -- see lmedia.h.
 *
 * WHERE THE BYTES COME FROM, and why this is the whole of it. Every file is a run of
 * bytes at a known offset inside a disc image some host serves, so a download is one
 * HTTP Range request per file (ln_get_range), resumed from what an earlier attempt
 * left, and checked against the SHA-256 the manifest pins before it takes its name.
 * A host that ignores Range is refused at its first byte rather than allowed to stream
 * a whole disc; a host that sends the wrong bytes is caught by the hash and the next
 * host is asked. Nothing is unpacked, so nothing here needs a decompressor.
 *
 * WHAT IS TRIED, IN WHAT ORDER. The mirrors of the file's disc, in the manifest's
 * order. Each gets up to three attempts, two seconds and then eight apart, each one
 * resuming the part file. A host that cannot be reached at all, or that answers Range
 * with the whole file or another piece, is not asked again in the same job, so a
 * machine with no network fails in the time it takes each host to not answer once,
 * rather than three times per file. A host that answers one file wrongly (damaged
 * bytes, a 404) is passed over for that file at once, because asking again gets the
 * same answer.
 *
 * WHAT IS REMOVED. Exactly the paths the manifest names for a component, its file and
 * legacy lines, and each one's .ocpart. Never a pattern, never a folder, and never a
 * path that runs through a link: a development folder whose dosdata is a link to the
 * repository's own data has nothing taken out of it.
 *
 * A MODULE'S FILES COUNT ONLY WHERE THE MODULE IS. The manifest's `module ra3d` line says
 * that the files under ra3d/ are the Red Alert module's movies and score, so they are
 * fetched, counted as missing and summed into the sizes only where lmodule.c says Red
 * Alert is installed, the same test the module grid draws its tile by, asked again each
 * time rather than once at start, because an update can bring the module into a folder
 * the launcher is already running in. Unticking a box removes them wherever they are.
 *
 * A FILE WITH A HEAD is the head's bytes followed by its range of the disc. Red Alert's
 * score is one: the game's audio reader takes plain MIX headers only, and the disc's
 * archive has an encrypted one, so the manifest pins the plain header (258 bytes) and the
 * range is the disc's data block as it is. The range arrives in its own part file,
 * <name>.range.ocpart, resumable like any other, and the file is then written whole as
 * <name>.ocpart, head first, and checked against its SHA-256 before it takes its name.
 */

#include "lmedia.h"
#include "lmodule.h"
#include "lnet.h"
#include "lpath.h"
#include "lzip.h"
#include "../game/lang_manifest.h"

#include <SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* The compiled-in manifest, generated into launcher/lmedia_manifest.c. */
extern const char LMEDIA_MANIFEST[];

#define LM_MOVIES 0
#define LM_MUSIC 1
#define LM_NCOMP 2

#define LM_MAX_MIRRORS 32
#define LM_MAX_MODULES 4
#define LM_MAX_HEAD 2048 /* bytes; localized Red Alert SOUNDS.MIX needs 1422 */
#define LM_ATTEMPTS 3

/* Where a file with a head keeps its range while it arrives. It ends in LZ_PART, so
 * everything that refuses a part file in a package refuses this one too. */
#define LM_RANGE_PART ".range" LZ_PART

/* Kept free beyond the download itself: a disk filled to its last byte is a machine
 * in trouble, whatever the game wanted. */
#define LM_SPARE (16LL * 1024 * 1024)

static const char *const lm_comp_name[LM_NCOMP] = {"movies", "music"};
static const char *const lm_comp_label[LM_NCOMP] = {"MOVIES", "MUSIC"};
static const char *const lm_disc_name[] = {"GDI", "NOD", "COVERT", "RA_ALLIED", "RA_SOVIET", "DE_GDI", "DE_NOD", "DE_RA_ALLIED", "DE_RA_SOVIET", "FR_RA_SOVIET", NULL};

typedef struct
{
    int disc;
    char url[2048];
    long long base;
} LM_Mirror;

typedef struct
{
    int comp;
    char rel[512];
    long long size;
    char sha[65];
    int disc;
    long long offset;      /* where the range starts in the disc image */
    unsigned char *head;   /* written ahead of the range; NULL for most files */
    int nhead;
    int module;            /* the index of its module folder, or -1 for the game's own */
} LM_File;

typedef struct
{
    int comp;
    char rel[512];
} LM_Legacy;

struct LM_State
{
    SDL_mutex *mx;
    SDL_Thread *worker;
    char dir[1024];
    char record[96];

    /* The manifest, read once by lm_create and never changed afterwards. */
    int ok;
    char bad[300];
    LM_Mirror mir[LM_MAX_MIRRORS];
    int nmir;
    LM_File *files;
    int nfiles;
    LM_Legacy *legacy;
    int nlegacy;
    char modules[LM_MAX_MODULES][32];
    int nmodules;

    int stall;

    /* The job. Written by the worker under mx, read by anyone under mx. */
    int want[LM_NCOMP];
    /* Install checkboxes request additions; unchecked content and its prior
     * preference/state are retained. Settings continues to use replacement mode. */
    int add_only, keep_record, keep_want[LM_NCOMP];
    char keep_state[LM_NCOMP][32];
    LM_Phase phase;
    long long done, total;
    long long shown; /* the most `done` has been in this job: what lm_bytes reports */
    char item[200];
    char status[512];
    int offline;
    volatile int cancel;
};

/* ------------------------------------------------------------------------ *
 * Small things.
 * ------------------------------------------------------------------------ */

static void lm_lock(LM_State *m)
{
    SDL_LockMutex(m->mx);
}

static void lm_unlock(LM_State *m)
{
    SDL_UnlockMutex(m->mx);
}

static void lm_set_phase(LM_State *m, LM_Phase p)
{
    lm_lock(m);
    m->phase = p;
    lm_unlock(m);
}

/* "MOVIES: GDI1.VQA (12 OF 75)", "MUSIC: CHECKING SCORES.MIX (1 OF 2)". */
static void lm_set_item(LM_State *m, int comp, const char *verb, const char *name, int i, int n)
{
    lm_lock(m);
    snprintf(m->item, sizeof m->item, "%s: %s%.120s (%d OF %d)", lm_comp_label[comp], verb, name, i,
             n);
    lm_unlock(m);
}

static const char *lm_base(const char *rel)
{
    const char *b = strrchr(rel, '/');
    return b ? b + 1 : rel;
}

static int lm_find_disc(const char *s)
{
    int i;
    for (i = 0; lm_disc_name[i]; i++)
        if (!strcmp(s, lm_disc_name[i]))
            return i;
    return -1;
}

static int lm_find_comp(const char *s)
{
    int i;
    for (i = 0; i < LM_NCOMP; i++)
        if (!strcmp(s, lm_comp_name[i]))
            return i;
    return -1;
}

static int lm_same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

/* A path the launcher keeps for itself can never be a movie or a piece of music: a
 * manifest naming one would have the media job write over the record or an update's
 * own files. */
static int lm_rel_ok(const char *rel)
{
    const char *b = lm_base(rel);
    size_t n = strlen(b);
    if (!lz_name_safe(rel) || rel[strlen(rel) - 1] == '/')
        return 0;
    if (!strncmp(rel, "opencnc3d-", 10) || !strncmp(rel, ".opencnc3d-", 11))
        return 0;
    if (n > 7 && lm_same_name(b + n - 7, ".ocpart"))
        return 0;
    return 1;
}

/* A whole, non-negative decimal number and nothing after it. */
static int lm_num(const char *s, long long *out)
{
    long long v = 0;
    if (!*s)
        return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > (0x7fffffffffffffffLL - 9) / 10)
            return 0;
        v = v * 10 + (*s - '0');
    }
    *out = v;
    return 1;
}

/* An even run of lower-case hex digits, decoded into `out` when it is not NULL. */
static int lm_hexbytes(const char *s, unsigned char *out)
{
    size_t i, n = strlen(s);
    if (n == 0 || n % 2)
        return 0;
    for (i = 0; i < n; i += 2) {
        int hi = (s[i] >= '0' && s[i] <= '9') ? s[i] - '0' : (s[i] >= 'a' && s[i] <= 'f') ? s[i] - 'a' + 10 : -1;
        int lo = (s[i + 1] >= '0' && s[i + 1] <= '9') ? s[i + 1] - '0'
                 : (s[i + 1] >= 'a' && s[i + 1] <= 'f') ? s[i + 1] - 'a' + 10 : -1;
        if (hi < 0 || lo < 0)
            return 0;
        if (out)
            out[i / 2] = (unsigned char)(hi * 16 + lo);
    }
    return 1;
}

static int lm_hex64(const char *s)
{
    int i;
    for (i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return s[64] == '\0';
}

void lm_format_size(long long bytes, char *buf, int len)
{
    const double mib = 1024.0 * 1024.0;
    double b = (double)(bytes < 0 ? 0 : bytes);
    /* The switch to GB is where one decimal of MB would read 1024.0. */
    if (b >= 1023.95 * mib)
        snprintf(buf, (size_t)len, "%.1f GB", b / (mib * 1024.0));
    else
        snprintf(buf, (size_t)len, "%.1f MB", b / mib);
}

int lm_choice_recorded(const char *dir)
{
    char path[1600];
    if (!dir)
        return 0;
    snprintf(path, sizeof path, "%s/%s", dir, LZ_MEDIA_RECORD);
    return ln_file_size(path) >= 0;
}

/* ------------------------------------------------------------------------ *
 * The manifest.
 * ------------------------------------------------------------------------ */

static int lm_parse(LM_State *m, const char *text)
{
    const char *p = text;
    int lineno = 0, header = 0, capf = 0, capl = 0, i, j;
    char line[8192];
    char g[2 * LM_MAX_HEAD + 8];

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char kind[32], a[2048], b[2048], c[64], d[80], e[32], f[64];
        char *q;
        int got;

        lineno++;
        if (n >= sizeof line) {
            snprintf(m->bad, sizeof m->bad, "line %d is too long", lineno);
            return 0;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = nl ? nl + 1 : p + n;
        if ((q = strchr(line, '#')) != NULL)
            *q = '\0';
        for (q = line + strlen(line); q > line && isspace((unsigned char)q[-1]);)
            *--q = '\0';
        for (q = line; *q && isspace((unsigned char)*q);)
            q++;
        if (!*q)
            continue;

        if (!header) {
            if (strcmp(q, "OCMEDIA 1") != 0) {
                snprintf(m->bad, sizeof m->bad, "it does not start with OCMEDIA 1");
                return 0;
            }
            header = 1;
            continue;
        }

        got = sscanf(q, "%31s %2047s %2047s %63s %79s %31s %63s %4103s", kind, a, b, c, d, e, f, g);
        if (got >= 1 && !strcmp(kind, "mirror")) {
            LM_Mirror *mr;
            long long base = -1;
            if (got != 4 || lm_find_disc(a) < 0
                || (strncmp(b, "http://", 7) && strncmp(b, "https://", 8)) || !lm_num(c, &base)) {
                snprintf(m->bad, sizeof m->bad, "line %d is not a mirror line it can read", lineno);
                return 0;
            }
            if (m->nmir >= LM_MAX_MIRRORS) {
                snprintf(m->bad, sizeof m->bad, "it lists more than %d mirrors", LM_MAX_MIRRORS);
                return 0;
            }
            if (strlen(b) >= sizeof m->mir[0].url - 1) {
                snprintf(m->bad, sizeof m->bad, "line %d names an address too long to use", lineno);
                return 0;
            }
            mr = &m->mir[m->nmir++];
            mr->disc = lm_find_disc(a);
            memcpy(mr->url, b, strlen(b) + 1);
            mr->base = base;
            continue;
        }
        if (got >= 1 && !strcmp(kind, "module")) {
            if (got != 2 || !lmod_known_folder(a) || strlen(a) >= sizeof m->modules[0]) {
                snprintf(m->bad, sizeof m->bad, "line %d names a module this launcher does not know",
                         lineno);
                return 0;
            }
            for (j = 0; j < m->nmodules; j++)
                if (!strcmp(m->modules[j], a)) {
                    snprintf(m->bad, sizeof m->bad, "it names the module %.40s twice", a);
                    return 0;
                }
            if (m->nmodules >= LM_MAX_MODULES) {
                snprintf(m->bad, sizeof m->bad, "it names more than %d modules", LM_MAX_MODULES);
                return 0;
            }
            memcpy(m->modules[m->nmodules++], a, strlen(a) + 1);
            continue;
        }
        if (got >= 1 && !strcmp(kind, "file")) {
            LM_File *fl;
            long long size = -1, off = -1;
            size_t nhex = got == 8 ? strlen(g) : 0;
            if ((got != 7 && got != 8) || lm_find_comp(a) < 0 || strlen(b) >= sizeof fl->rel
                || !lm_rel_ok(b) || !lm_num(c, &size) || size <= 0
                || !lm_hex64(d) || lm_find_disc(e) < 0 || !lm_num(f, &off)
                || (got == 8 && (nhex % 2 || nhex > 2 * LM_MAX_HEAD || (long long)nhex / 2 >= size
                                 || !lm_hexbytes(g, NULL)))) {
                snprintf(m->bad, sizeof m->bad, "line %d is not a file line it can read", lineno);
                return 0;
            }
            if (m->nfiles >= capf) {
                LM_File *grown;
                capf = capf ? capf * 2 : 64;
                grown = (LM_File *)realloc(m->files, (size_t)capf * sizeof *grown);
                if (!grown) {
                    snprintf(m->bad, sizeof m->bad, "out of memory");
                    return 0;
                }
                m->files = grown;
            }
            fl = &m->files[m->nfiles++];
            fl->comp = lm_find_comp(a);
            memcpy(fl->rel, b, strlen(b) + 1);
            fl->size = size;
            memcpy(fl->sha, d, 64); /* lm_hex64 has checked it is exactly 64 */
            fl->sha[64] = '\0';
            fl->disc = lm_find_disc(e);
            fl->offset = off;
            fl->head = NULL;
            fl->nhead = 0;
            fl->module = -1;
            if (nhex) {
                fl->head = (unsigned char *)malloc(nhex / 2);
                if (!fl->head) {
                    snprintf(m->bad, sizeof m->bad, "out of memory");
                    return 0;
                }
                lm_hexbytes(g, fl->head);
                fl->nhead = (int)(nhex / 2);
            }
            continue;
        }
        if (got >= 1 && !strcmp(kind, "legacy")) {
            LM_Legacy *lg;
            if (got != 3 || lm_find_comp(a) < 0 || strlen(b) >= sizeof lg->rel || !lm_rel_ok(b)) {
                snprintf(m->bad, sizeof m->bad, "line %d is not a legacy line it can read",
                         lineno);
                return 0;
            }
            if (m->nlegacy >= capl) {
                LM_Legacy *grown;
                capl = capl ? capl * 2 : 32;
                grown = (LM_Legacy *)realloc(m->legacy, (size_t)capl * sizeof *grown);
                if (!grown) {
                    snprintf(m->bad, sizeof m->bad, "out of memory");
                    return 0;
                }
                m->legacy = grown;
            }
            lg = &m->legacy[m->nlegacy++];
            lg->comp = lm_find_comp(a);
            memcpy(lg->rel, b, strlen(b) + 1);
            continue;
        }
        snprintf(m->bad, sizeof m->bad, "line %d is a kind of line it does not know", lineno);
        return 0;
    }
    if (!header) {
        snprintf(m->bad, sizeof m->bad, "it is empty");
        return 0;
    }

    /* One name, one file: a second line for the same path would download it twice
     * and remove it once. And every file needs somewhere to come from. */
    for (i = 0; i < m->nfiles; i++) {
        int has = 0;
        for (j = i + 1; j < m->nfiles; j++)
            if (lm_same_name(m->files[i].rel, m->files[j].rel)) {
                snprintf(m->bad, sizeof m->bad, "it lists %.200s twice", m->files[i].rel);
                return 0;
            }
        for (j = 0; j < m->nmir; j++)
            if (m->mir[j].disc == m->files[i].disc)
                has = 1;
        if (!has) {
            snprintf(m->bad, sizeof m->bad, "%.200s comes from %s, which has no mirror",
                     m->files[i].rel, lm_disc_name[m->files[i].disc]);
            return 0;
        }
        /* Its module, by its first folder. A file in a module's folder with no module
         * line for it would be fetched into every install, so that is refused. */
        {
            char top[40];
            size_t k = strcspn(m->files[i].rel, "/");
            if (m->files[i].rel[k] == '/' && k < sizeof top) {
                memcpy(top, m->files[i].rel, k);
                top[k] = '\0';
                for (j = 0; j < m->nmodules; j++)
                    if (!strcmp(m->modules[j], top))
                        m->files[i].module = j;
                if (m->files[i].module < 0 && lmod_known_folder(top)) {
                    snprintf(m->bad, sizeof m->bad,
                             "%.200s is in the %s folder and it names no module line for it",
                             m->files[i].rel, top);
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Which of the manifest's modules are installed in the folder now. */
static void lm_modules_now(const LM_State *m, int inst[LM_MAX_MODULES])
{
    int j;
    for (j = 0; j < LM_MAX_MODULES; j++)
        inst[j] = j < m->nmodules && lmod_folder_installed(m->dir, m->modules[j]);
}

/* Whether a file belongs to this install: the game's own always, a module's only where
 * that module is installed. */
static int lm_counts(const LM_File *f, const int inst[LM_MAX_MODULES])
{
    return f->module < 0 || inst[f->module];
}

static char *lm_read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || n > 4 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------------------ *
 * Life cycle and the accessors.
 * ------------------------------------------------------------------------ */

LM_State *lm_create(const char *dir, const char *manifest_override_path)
{
    LM_State *m = (LM_State *)calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->mx = SDL_CreateMutex();
    m->stall = 30;
    m->phase = LM_IDLE;
    snprintf(m->dir, sizeof m->dir, "%s", dir ? dir : ".");
    snprintf(m->record, sizeof m->record, "%s", LZ_MEDIA_RECORD);

    if (manifest_override_path && *manifest_override_path) {
        char *text = lm_read_text(manifest_override_path);
        if (!text) {
            snprintf(m->bad, sizeof m->bad, "%s could not be read", manifest_override_path);
        } else {
            m->ok = lm_parse(m, text);
            free(text);
        }
    } else {
        m->ok = lm_parse(m, LMEDIA_MANIFEST);
    }
    if (!m->ok)
        snprintf(m->status, sizeof m->status,
                 "the list of movies and music in this launcher is damaged (%.200s), so nothing "
                 "can be downloaded",
                 m->bad);
    return m;
}

/* Settings owns a physically scoped manifest: reads, legacy cleanup, resumed parts,
 * and removal all see exactly the same game's file set. The first-install/CLI
 * combined manifest remains available through lm_create. */
LM_State *lm_create_game(const char *dir, const char *override, int ra)
{
    LM_State *m = lm_create(dir, override);
    int i, n = 0;
    if (!m) return NULL;
    for (i = 0; i < m->nfiles; i++) {
        if ((!strncmp(m->files[i].rel, "ra3d/", 5)) == !!ra)
            m->files[n++] = m->files[i];
        else free(m->files[i].head);
    }
    m->nfiles = n; n = 0;
    for (i = 0; i < m->nlegacy; i++)
        if ((!strncmp(m->legacy[i].rel, "ra3d/", 5)) == !!ra)
            m->legacy[n++] = m->legacy[i];
    m->nlegacy = n;
    snprintf(m->record, sizeof m->record, "opencnc3d-media-%s.txt", ra ? "ra" : "td");
    return m;
}

int lm_language_pack(int ra, int index, char *code, int code_len, char *name, int name_len)
{
    const char *p = LANG_MANIFEST;
    char c[16], game[16], label[80];
    int count = 0;
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (sscanf(p, "pack %15s %15s %79[^\n]", c, game, label) == 3 &&
            !strcmp(game, ra ? "ra" : "td") && count++ == index) {
            snprintf(code, (size_t)code_len, "%s", c);
            snprintf(name, (size_t)name_len, "%s", label);
            return 1;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

/* Adapt the existing pinned language catalog to the same safe range downloader.
 * Only the selected pack's exact paths may enter this manifest. */
LM_State *lm_create_language(const char *dir, int ra, const char *code, const char *override)
{
    LM_State *m = (LM_State *)calloc(1, sizeof *m);
    char *owned = override ? lm_read_text(override) : NULL;
    const char *source = override ? owned : LANG_MANIFEST, *p;
    char *text, prefix[80];
    size_t used;
    int files = 0;
    if (!m) { free(owned); return NULL; }
    m->mx = SDL_CreateMutex(); m->stall = 30; m->phase = LM_IDLE;
    snprintf(m->dir, sizeof m->dir, "%s", dir ? dir : ".");
    if (!code || (strcmp(code, "de") && (strcmp(code, "fr") || !ra)) || !source ||
        strncmp(source, "OCLANG 1\n", 9)) {
        snprintf(m->status, sizeof m->status, "The language catalog could not be read.");
        free(owned); return m;
    }
    snprintf(m->record, sizeof m->record, "opencnc3d-language-%s-%s.txt", ra ? "ra" : "td", code);
    snprintf(prefix, sizeof prefix, "%s%s/", ra ? "ra3d/lang/" : "dosdata/lang/", code);
    text = (char *)malloc(2 * strlen(source) + 128);
    if (!text) { free(owned); lm_destroy(m); return NULL; }
    strcpy(text, ra ? "OCMEDIA 1\nmodule ra3d\n" : "OCMEDIA 1\n"); used = strlen(text);
    p = source;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char c[16], game[16], rel[512], line[8192]; int rest = 0;
        if (n >= sizeof line) { snprintf(m->bad, sizeof m->bad, "language catalog line too long"); break; }
        memcpy(line, p, n); line[n] = 0;
        if (!strncmp(line, "mirror ", 7)) {
            memcpy(text + used, p, n); used += n; text[used++] = '\n';
        } else if (sscanf(line, "file %15s %15s %511s %n", c, game, rel, &rest) == 3 && rest > 0 && (size_t)rest < n &&
                   !strcmp(c, code) && !strcmp(game, ra ? "ra" : "td")) {
            if (strncmp(rel, prefix, strlen(prefix))) {
                snprintf(m->bad, sizeof m->bad, "language file outside its pack"); break;
            }
            used += (size_t)sprintf(text + used, "file movies %s ", rel);
            memcpy(text + used, line + rest, n - (size_t)rest);
            used += n - (size_t)rest; text[used++] = '\n'; files++;
        }
        if (!nl) break;
        p = nl + 1;
    }
    text[used] = 0;
    if (!m->bad[0] && files) m->ok = lm_parse(m, text);
    if (!m->ok) snprintf(m->status, sizeof m->status, "The language pack cannot be managed (%.200s).", m->bad);
    free(text); free(owned); return m;
}

void lm_destroy(LM_State *m)
{
    int i;
    if (!m)
        return;
    m->cancel = 1;
    if (m->worker)
        SDL_WaitThread(m->worker, NULL);
    for (i = 0; i < m->nfiles; i++)
        free(m->files[i].head);
    free(m->files);
    free(m->legacy);
    SDL_DestroyMutex(m->mx);
    free(m);
}

void lm_set_stall(LM_State *m, int seconds)
{
    if (m && seconds > 0)
        m->stall = seconds;
}

void lm_installed(LM_State *m, int *movies, int *music)
{
    int have[LM_NCOMP] = {1, 1}, count[LM_NCOMP] = {0, 0}, i, inst[LM_MAX_MODULES];
    if (!m) {
        if (movies)
            *movies = 0;
        if (music)
            *music = 0;
        return;
    }
    lm_modules_now(m, inst);
    for (i = 0; i < m->nfiles; i++) {
        char path[1600];
        const LM_File *f = &m->files[i];
        if (!lm_counts(f, inst))
            continue;
        count[f->comp]++;
        snprintf(path, sizeof path, "%s/%s", m->dir, f->rel);
        if (ln_file_size(path) != f->size)
            have[f->comp] = 0;
    }
    if (movies)
        *movies = count[LM_MOVIES] > 0 && have[LM_MOVIES];
    if (music)
        *music = count[LM_MUSIC] > 0 && have[LM_MUSIC];
}

void lm_present_any(LM_State *m, int *movies, int *music, long long *movies_bytes,
                    long long *music_bytes)
{
    int any[LM_NCOMP] = {0, 0}, i;
    long long bytes[LM_NCOMP] = {0, 0};
    if (m) {
        for (i = 0; i < m->nfiles + m->nlegacy; i++) {
            const char *rel = i < m->nfiles ? m->files[i].rel : m->legacy[i - m->nfiles].rel;
            int comp = i < m->nfiles ? m->files[i].comp : m->legacy[i - m->nfiles].comp;
            char path[1600], part[1640], rpart[1660];
            long long n, p, r;
            snprintf(path, sizeof path, "%s/%s", m->dir, rel);
            snprintf(part, sizeof part, "%s%s", path, LZ_PART);
            snprintf(rpart, sizeof rpart, "%s%s", path, LM_RANGE_PART);
            n = ln_file_size(path);
            p = ln_file_size(part);
            r = ln_file_size(rpart);
            if (n >= 0 || p >= 0 || r >= 0)
                any[comp] = 1;
            bytes[comp] += (n > 0 ? n : 0) + (p > 0 ? p : 0) + (r > 0 ? r : 0);
        }
    }
    if (movies)
        *movies = any[LM_MOVIES];
    if (music)
        *music = any[LM_MUSIC];
    if (movies_bytes)
        *movies_bytes = bytes[LM_MOVIES];
    if (music_bytes)
        *music_bytes = bytes[LM_MUSIC];
}

int lm_wanted(LM_State *m, int *movies, int *music, int *movies_pending, int *music_pending)
{
    char path[1600], line[256];
    int want[LM_NCOMP] = {-1, -1}, pend[LM_NCOMP] = {0, 0}, k;
    FILE *f;

    if (movies)
        *movies = 0;
    if (music)
        *music = 0;
    if (movies_pending)
        *movies_pending = 0;
    if (music_pending)
        *music_pending = 0;
    if (!m)
        return 0;
    snprintf(path, sizeof path, "%s/%s", m->dir, m->record);
    f = fopen(path, "rb");
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        char key[32], val[32];
        if (line[0] == '#' || sscanf(line, "%31s %31s", key, val) != 2)
            continue;
        for (k = 0; k < LM_NCOMP; k++) {
            char state_key[40];
            snprintf(state_key, sizeof state_key, "%s_state", lm_comp_name[k]);
            if (!strcmp(key, lm_comp_name[k]))
                want[k] = !strcmp(val, "yes") ? 1 : !strcmp(val, "no") ? 0 : -1;
            else if (!strcmp(key, state_key))
                pend[k] = !strcmp(val, "pending");
        }
    }
    fclose(f);
    if (want[LM_MOVIES] < 0 || want[LM_MUSIC] < 0)
        return 0;
    if (movies)
        *movies = want[LM_MOVIES];
    if (music)
        *music = want[LM_MUSIC];
    if (movies_pending)
        *movies_pending = pend[LM_MOVIES];
    if (music_pending)
        *music_pending = pend[LM_MUSIC];
    return 1;
}

void lm_sizes(LM_State *m, long long *movies_bytes, long long *music_bytes)
{
    long long sizes[LM_NCOMP] = {0, 0};
    int i, inst[LM_MAX_MODULES];
    if (m) {
        lm_modules_now(m, inst);
        for (i = 0; i < m->nfiles; i++)
            if (lm_counts(&m->files[i], inst))
                sizes[m->files[i].comp] += m->files[i].size;
    }
    if (movies_bytes)
        *movies_bytes = sizes[LM_MOVIES];
    if (music_bytes)
        *music_bytes = sizes[LM_MUSIC];
}

void lm_game_sizes(LM_State *m,long long *movies,long long *music)
{
    long long sizes[LM_NCOMP]={0,0};int i;
    if(m)for(i=0;i<m->nfiles;i++)sizes[m->files[i].comp]+=m->files[i].size;
    if(movies)*movies=sizes[LM_MOVIES];
    if(music)*music=sizes[LM_MUSIC];
}

void lm_cancel(LM_State *m)
{
    if (m)
        m->cancel = 1;
}

LM_Phase lm_phase(LM_State *m)
{
    LM_Phase p;
    if (!m)
        return LM_IDLE;
    lm_lock(m);
    p = m->phase;
    lm_unlock(m);
    return p;
}

void lm_bytes(LM_State *m, long long *done, long long *total)
{
    if (!m) {
        *done = *total = 0;
        return;
    }
    lm_lock(m);
    *done = m->shown;
    *total = m->total;
    lm_unlock(m);
    if (*total > 0 && *done > *total)
        *done = *total;
}

void lm_item(LM_State *m, char *buf, int len)
{
    if (!m) {
        snprintf(buf, (size_t)len, "%s", "");
        return;
    }
    lm_lock(m);
    snprintf(buf, (size_t)len, "%s", m->item);
    lm_unlock(m);
}

void lm_status(LM_State *m, char *buf, int len)
{
    if (!m) {
        snprintf(buf, (size_t)len, "%s", "");
        return;
    }
    lm_lock(m);
    snprintf(buf, (size_t)len, "%s", m->status);
    lm_unlock(m);
}

int lm_offline(LM_State *m)
{
    int o;
    if (!m)
        return 0;
    lm_lock(m);
    o = m->offline;
    lm_unlock(m);
    return o;
}

/* ------------------------------------------------------------------------ *
 * The disk.
 * ------------------------------------------------------------------------ */

/* The full path of `rel` inside the install, made only through lz_inside, so that a
 * folder on its way that is a link or a file refuses it. A refusal fills `why`. A path
 * that is itself a link is refused too: neither a download nor a removal acts on one. */
static int lm_path(const LM_State *m, const char *rel, char *out, size_t outlen, char *why,
                   int whylen)
{
    size_t n;
    if (!lz_inside(m->dir, rel, out, outlen)) {
        n = strlen(m->dir);
        if (out[0] && !strncmp(out, m->dir, n) && out[n] && out[n + 1])
            snprintf(why, (size_t)whylen,
                     "%s in the game folder is a link or a file, not a real folder, so %s was "
                     "left alone",
                     out + n + 1, lm_base(rel));
        else
            snprintf(why, (size_t)whylen, "%s could not be checked, so it was left alone", rel);
        return 0;
    }
    if (lz_is_symlink(out)) {
        snprintf(why, (size_t)whylen, "%s in the game folder is a link, so it was left alone", rel);
        return 0;
    }
    return 1;
}

/* Something on the name: a file, a folder, a link. */
static int lm_exists(const char *path)
{
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(path, &st) == 0;
#endif
}

/* `from` onto `to`, replacing it. Windows' rename refuses an existing target, and
 * removing the target first would leave a moment with neither file. */
static int lm_replace(const char *from, const char *to)
{
#ifdef _WIN32
    int tries;
    for (tries = 0; tries < 20; tries++) {
        if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return 1;
        Sleep(50); /* a scanner reading the file that was just written */
    }
    return 0;
#else
    return rename(from, to) == 0;
#endif
}

/* Removes one name, if anything is on it. 1 when nothing is there afterwards. */
static int lm_remove(const char *path)
{
    if (!lm_exists(path))
        return 1;
#ifdef _WIN32
    {
        DWORD a = GetFileAttributesA(path);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
            return 0; /* a folder on a file's name is not this file's to remove */
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesA(path, a & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
#else
    {
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
            return 0;
    }
#endif
    remove(path);
    return !lm_exists(path);
}

/* The record, written whole and renamed over the old one, so a kill leaves the old
 * record or the new one and never half of either. Called with the install lock held. */
static int lm_keep_record(LM_State *m)
{
    char path[1600], line[256], why[300];
    FILE *f;
    int k, ok;
    m->keep_record = 0;
    for (k = 0; k < LM_NCOMP; k++) {
        m->keep_want[k] = 0;
        snprintf(m->keep_state[k], sizeof m->keep_state[k], "none");
    }
    if (!lm_path(m, m->record, path, sizeof path, why, sizeof why)) return 0;
    f = fopen(path, "rb");
    if (!f) return !lm_exists(path);
    m->keep_record = 1;
    while (fgets(line, sizeof line, f)) {
        char key[32], val[32];
        if (line[0] == '#' || sscanf(line, "%31s %31s", key, val) != 2) continue;
        for (k = 0; k < LM_NCOMP; k++) {
            char state_key[40];
            snprintf(state_key, sizeof state_key, "%s_state", lm_comp_name[k]);
            if (!strcmp(key, lm_comp_name[k])) m->keep_want[k] = !strcmp(val, "yes");
            else if (!strcmp(key, state_key))
                snprintf(m->keep_state[k], sizeof m->keep_state[k], "%s", val);
        }
    }
    ok = !ferror(f);
    fclose(f);
    return ok;
}

static int lm_write_record(LM_State *m, const char *state[LM_NCOMP])
{
    char path[1600], part[1640], why[300];
    FILE *f;
    int ok;

    /* An entirely unchecked reinstall is a genuine no-op, including comments
     * and timestamps in an existing selection record. */
    if (m->add_only && !m->want[LM_MOVIES] && !m->want[LM_MUSIC] && m->keep_record)
        return 1;

    if (!lm_path(m, m->record, path, sizeof path, why, sizeof why))
        return 0;
    snprintf(part, sizeof part, "%s%s", path, LZ_PART);
    remove(part);
    f = fopen(part, "wb");
    if (!f)
        return 0;
    ok = fprintf(f,
                 "# Written by the OpenCNC3D launcher. Delete this file to see the setup screen "
                 "again.\n"
                 "movies %s\nmusic %s\nmovies_state %s\nmusic_state %s\n",
                 (m->want[LM_MOVIES] || (m->add_only && m->keep_want[LM_MOVIES])) ? "yes" : "no",
                 (m->want[LM_MUSIC] || (m->add_only && m->keep_want[LM_MUSIC])) ? "yes" : "no",
                 state[LM_MOVIES], state[LM_MUSIC])
         > 0;
    if (fflush(f) != 0)
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok || !lm_replace(part, path)) {
        remove(part);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------ *
 * The worker.
 * ------------------------------------------------------------------------ */

typedef struct
{
    LM_State *m;
    long long before; /* the job's bytes from the files finished before this one */
} LM_Tick;

/* The bytes so far, under mx. The bar shows the most they have ever been in this job,
 * so a file fetched a second time is counted again without the bar stepping back. */
static void lm_set_done_locked(LM_State *m, long long done)
{
    m->done = done;
    if (done > m->shown)
        m->shown = done;
}

static int lm_tick(void *user, long long have, long long total)
{
    LM_Tick *t = (LM_Tick *)user;
    (void)total;
    lm_lock(t->m);
    lm_set_done_locked(t->m, t->before + have);
    lm_unlock(t->m);
    return !t->m->cancel;
}

/* A wait that a Cancel cuts short. Returns 0 when cancelled. */
static int lm_wait(LM_State *m, int seconds)
{
    int i;
    for (i = 0; i < seconds * 10; i++) {
        if (m->cancel)
            return 0;
        SDL_Delay(100);
    }
    return !m->cancel;
}

/* The order files are fetched in: movies before music, and the small ones first in
 * each, so the bar and the counter move early. Ties keep the manifest's order. */
static int lm_by_size(const void *a, const void *b)
{
    const LM_File *fa = *(const LM_File *const *)a, *fb = *(const LM_File *const *)b;
    if (fa->comp != fb->comp)
        return fa->comp - fb->comp;
    if (fa->size != fb->size)
        return fa->size < fb->size ? -1 : 1;
    return fa < fb ? -1 : (fa > fb ? 1 : 0);
}

/* The job's shared bookkeeping, owned by the worker alone. */
typedef struct
{
    int dead[LM_MAX_MIRRORS];  /* never reached this job: not asked again           */
    int tried[LM_MAX_MIRRORS]; /* asked at least once                             */
    int reached;               /* some host answered something, at least once       */
    char why[512];             /* the last reason a file could not be had            */
} LM_Job;

/* One file: every mirror of its disc in order, each up to LM_ATTEMPTS times. Returns
 * 1 with the file on its name, 0 when no mirror gave it (why says the last reason),
 * -1 cancelled, -2 a failure no other host can help with (the disk). */
/* A file with a head, written whole: the head, then the range that has arrived in
 * `rpart`, into `part`. 1 when it is all there. */
static int lm_join(const LM_File *f, const char *rpart, const char *part)
{
    FILE *in, *out;
    char buf[16384]; /* on the worker's stack */
    size_t n;
    int ok;
    remove(part);
    in = fopen(rpart, "rb");
    if (!in)
        return 0;
    out = fopen(part, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    ok = fwrite(f->head, 1, (size_t)f->nhead, out) == (size_t)f->nhead;
    while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0)
        ok = fwrite(buf, 1, n, out) == n;
    if (ferror(in))
        ok = 0;
    fclose(in);
    if (fflush(out) != 0)
        ok = 0;
    if (fclose(out) != 0)
        ok = 0;
    if (!ok)
        remove(part);
    return ok;
}

static int lm_fetch_one(LM_State *m, LM_Job *job, const LM_File *f, const char *path,
                        long long before)
{
    char part[1640], rpart[1660], err[400];
    const char *into;
    int i;
    LM_Tick tick;

    snprintf(part, sizeof part, "%s%s", path, LZ_PART);
    snprintf(rpart, sizeof rpart, "%s%s", path, LM_RANGE_PART);
    /* A file with a head fetches its range into a part file of its own, so the part a
     * resume reads is only ever the disc's bytes. */
    into = f->nhead ? rpart : part;
    tick.m = m;
    tick.before = before;

    for (i = 0; i < m->nmir; i++) {
        const LM_Mirror *mr = &m->mir[i];
        int attempt;
        if (mr->disc != f->disc || job->dead[i])
            continue;
        for (attempt = 1; attempt <= LM_ATTEMPTS; attempt++) {
            long status = 0;
            int rc;

            if (m->cancel)
                return -1;
            job->tried[i] = 1;
            lm_set_phase(m, LM_FETCHING);
            err[0] = '\0';
            rc = ln_get_range(mr->url, NULL, into, mr->base + f->offset, f->size - f->nhead,
                              m->stall, &m->cancel, lm_tick, &tick, &status, err, sizeof err);
            if (rc != LN_RANGE_NOCONNECT)
                job->reached = 1;

            if (rc == LN_RANGE_OK) {
                char hex[80], herr[256];
                lm_set_phase(m, LM_CHECKING);
                if (f->nhead && !lm_join(f, rpart, part)) {
                    snprintf(job->why, sizeof job->why,
                             "the disk would not take %.200s. Make some room, then try again",
                             lm_base(f->rel));
                    return -2;
                }
                if (!lz_sha256_file(part, hex, herr, sizeof herr)) {
                    snprintf(job->why, sizeof job->why, "%.200s could not be read back to check it",
                             lm_base(f->rel));
                    return -2;
                }
                if (strcmp(hex, f->sha) != 0) {
                    /* The host answered with the right number of bytes and the wrong
                     * ones: a page instead of a file, a different edition of the disc.
                     * Asking it again gets the same bytes, so the next host is asked. */
                    char host[256];
                    const char *u = strstr(mr->url, "://");
                    size_t k;
                    u = u ? u + 3 : mr->url;
                    k = strcspn(u, "/?#");
                    if (k >= sizeof host)
                        k = sizeof host - 1;
                    memcpy(host, u, k);
                    host[k] = '\0';
                    remove(part);
                    remove(rpart);
                    snprintf(job->why, sizeof job->why,
                             "%.200s did not match its checksum when it came from %.200s", lm_base(f->rel),
                             host);
                    lm_lock(m);
                    lm_set_done_locked(m, before); /* the bar stays where it got to */
                    lm_unlock(m);
                    break;
                }
                if (!lm_replace(part, path)) {
                    snprintf(job->why, sizeof job->why,
                             "%.200s arrived but could not be put in place. Close the game, then try "
                             "again",
                             lm_base(f->rel));
                    return -2;
                }
                if (f->nhead)
                    remove(rpart);
                return 1;
            }
            if (rc == LN_RANGE_CANCEL)
                return -1;
            snprintf(job->why, sizeof job->why, "%s", err);
            if (rc == LN_RANGE_DISK) {
                snprintf(job->why, sizeof job->why,
                         "the disk would not take %.200s. Make some room, then try again",
                         lm_base(f->rel));
                return -2;
            }
            if (rc == LN_RANGE_NOCONNECT || rc == LN_RANGE_NORANGE) {
                /* Not reached at all, or answering Range with something else: it will
                 * do the same for every other file, so it is not asked again in this
                 * job. */
                job->dead[i] = 1;
                break;
            }
            if (rc == LN_RANGE_HTTP && !(status >= 500 || status == 408 || status == 429))
                break; /* 404, 403, 410: this host does not have it, and will not */
            /* BROKEN, or a server error: the host may answer next time, from where
             * the part file stops. */
            if (attempt < LM_ATTEMPTS && !lm_wait(m, attempt == 1 ? 2 : 8))
                return -1;
        }
    }
    return 0;
}

/* The parent folder of a file inside the install, made if it is missing. The path
 * has passed lm_path, so every folder on its way that exists is a real one. */
static void lm_make_parent(const char *path)
{
    char dir[1600], *slash;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
#ifdef _WIN32
    {
        char *back = strrchr(dir, '\\');
        if (back > slash)
            slash = back;
    }
#endif
    if (slash) {
        *slash = '\0';
        lz_mkdirs(dir);
    }
}

static int lm_thread(void *user)
{
    LM_State *m = (LM_State *)user;
    LZ_Lock lk;
    int own = 0, held = 0, record_ready = 0, rc, i, k, nfetch = 0;
    const LM_File **order = NULL, **fetch = NULL;
    long long *parts = NULL;
    int failed = 0, cancelled = 0, offline = 0, inst[LM_MAX_MODULES];
    int comp_ok[LM_NCOMP] = {1, 1};
    const char *state[LM_NCOMP];
    char why[512];
    LM_Job job;
    long long need = 0, have_parts = 0, total = 0, finished = 0, rest;

    memset(&job, 0, sizeof job);
    why[0] = '\0';
    lm_modules_now(m, inst); /* asked once for the job, before the lock is taken */

    /* The lock: this process's own when it already has one on the folder, which is
     * the launcher's window, whose updater took it at start; otherwise a new one,
     * released when the job ends. */
    if (lz_held_here(m->dir)) {
        held = 1;
    } else {
        rc = lz_lock(m->dir, &lk);
        if (rc == 1) {
            own = held = 1;
        } else {
            if (rc == 0)
                snprintf(why, sizeof why,
                         "another OpenCNC3D launcher is using this game folder. Close it and try "
                         "again");
            else if (rc == -2)
                snprintf(why, sizeof why,
                         "this folder is not an OpenCNC3D install (it has no "
                         "opencnc3d-install.txt), so nothing in it was changed");
            else
                snprintf(why, sizeof why, "the game folder could not be locked for the download");
            failed = 1;
            goto finish;
        }
    }

    /* Snapshot only after taking the install lock, so another launcher cannot
     * change a preference between the snapshot and this transaction. */
    if (m->add_only && !lm_keep_record(m)) {
        snprintf(why, sizeof why, "The existing media choice could not be read safely, so nothing was changed.");
        failed = 1;
        goto finish;
    }
    record_ready = 1;

    /* The choice goes on the disk before anything else does, as "pending" for each
     * component the disk may not match yet, so a job killed part way still says what
     * was chosen and that it is not finished. */
    for (k = 0; k < LM_NCOMP; k++) {
        int any = 0;
        if (m->add_only && !m->want[k]) {
            state[k] = m->keep_state[k];
            continue;
        }
        for (i = 0; i < m->nfiles; i++) {
            char path[1600];
            if (m->files[i].comp != k)
                continue;
            snprintf(path, sizeof path, "%s/%s", m->dir, m->files[i].rel);
            if (ln_file_size(path) >= 0)
                any = 1;
        }
        state[k] = (m->want[k] || any) ? "pending" : "none";
    }
    if (!lm_write_record(m, state)) {
        snprintf(why, sizeof why,
                 "opencnc3d-media.txt could not be written in the game folder, so nothing was "
                 "changed. Check that the folder can be written to");
        failed = 1;
        goto finish;
    }

    /* 1. REMOVAL, for each component that is not wanted: every name it owns, and then
     *    the verdict, so one refusal does not stop the rest from going. */
    for (k = 0; k < LM_NCOMP; k++) {
        int n = 0, count = 0;
        if (m->want[k] || m->add_only)
            continue;
        for (i = 0; i < m->nfiles; i++)
            count += m->files[i].comp == k;
        for (i = 0; i < m->nlegacy; i++)
            count += m->legacy[i].comp == k;
        lm_set_phase(m, LM_REMOVING);
        for (i = 0; i < m->nfiles + m->nlegacy; i++) {
            const char *rel = i < m->nfiles ? m->files[i].rel : m->legacy[i - m->nfiles].rel;
            int comp = i < m->nfiles ? m->files[i].comp : m->legacy[i - m->nfiles].comp;
            char path[1600], part[1640], pwhy[400];
            if (comp != k)
                continue;
            n++;
            lm_set_item(m, k, "REMOVING ", lm_base(rel), n, count);
            if (!lm_path(m, rel, path, sizeof path, pwhy, sizeof pwhy)) {
                comp_ok[k] = 0;
                if (!why[0]) /* the first refusal is the one worth reading */
                    snprintf(why, sizeof why, "%s", pwhy);
                continue;
            }
            snprintf(part, sizeof part, "%s%s", path, LZ_PART);
            if (!lm_remove(path) || !lm_remove(part)) {
                comp_ok[k] = 0;
                if (!why[0])
                    snprintf(why, sizeof why,
                             "%.200s could not be removed. Close the game, then try again",
                             lm_base(rel));
                continue;
            }
            snprintf(part, sizeof part, "%s%s", path, LM_RANGE_PART);
            if (!lm_remove(part)) {
                comp_ok[k] = 0;
                if (!why[0])
                    snprintf(why, sizeof why,
                             "%.200s could not be removed. Close the game, then try again",
                             lm_base(rel));
            }
        }
        if (!comp_ok[k])
            failed = 1;
    }

    /* 2. WHAT IS WANTED AND NOT THERE WHOLE, in fetching order. A file on the disk at
     *    its size is downloaded again only if its bytes are not the ones pinned. */
    order = (const LM_File **)malloc((size_t)(m->nfiles + 1) * sizeof *order);
    fetch = (const LM_File **)malloc((size_t)(m->nfiles + 1) * sizeof *fetch);
    parts = (long long *)malloc((size_t)(m->nfiles + 1) * sizeof *parts);
    if (!order || !fetch || !parts) {
        snprintf(why, sizeof why, "out of memory");
        failed = 1;
        goto finish;
    }
    for (i = 0; i < m->nfiles; i++)
        order[i] = &m->files[i];
    qsort(order, (size_t)m->nfiles, sizeof *order, lm_by_size);

    for (k = 0; k < LM_NCOMP; k++) {
        int n = 0, count = 0;
        if (!m->want[k])
            continue;
        for (i = 0; i < m->nfiles; i++)
            count += order[i]->comp == k && lm_counts(order[i], inst);
        for (i = 0; i < m->nfiles; i++) {
            const LM_File *f = order[i];
            char path[1600], pwhy[400];
            if (f->comp != k || !lm_counts(f, inst))
                continue;
            n++;
            if (m->cancel) {
                cancelled = 1;
                goto finish;
            }
            if (!lm_path(m, f->rel, path, sizeof path, pwhy, sizeof pwhy)) {
                comp_ok[k] = 0;
                failed = 1;
                if (!why[0]) /* the first refusal is the one worth reading */
                    snprintf(why, sizeof why, "%s", pwhy);
                continue;
            }
            if (ln_file_size(path) == f->size) {
                char hex[80], herr[256];
                lm_set_phase(m, LM_CHECKING);
                lm_set_item(m, k, "CHECKING ", lm_base(f->rel), n, count);
                if (lz_sha256_file(path, hex, herr, sizeof herr) && !strcmp(hex, f->sha))
                    continue;
            }
            fetch[nfetch++] = f;
        }
    }

    /* 3. ROOM FOR IT, counted from what the part files already hold. */
    for (i = 0; i < nfetch; i++) {
        char part[1660];
        long long have, range = fetch[i]->size - fetch[i]->nhead;
        snprintf(part, sizeof part, "%s/%s%s", m->dir, fetch[i]->rel,
                 fetch[i]->nhead ? LM_RANGE_PART : LZ_PART);
        have = ln_file_size(part);
        if (have < 0 || have > range)
            have = 0;
        parts[i] = have;
        total += fetch[i]->size;
        have_parts += have;
        need += range - have;
        /* and, for a file with a head, the whole file written beside its range */
        need += fetch[i]->nhead ? fetch[i]->size : 0;
    }
    if (nfetch) {
        long long free_bytes = 0;
        if (lp_free_bytes(m->dir, &free_bytes) && free_bytes < need + LM_SPARE) {
            char a[32], b[32];
            lm_format_size(need, a, sizeof a);
            lm_format_size(free_bytes, b, sizeof b);
            snprintf(why, sizeof why,
                     "there is not enough room on the disk: the download needs %s and %s is free",
                     a, b);
            for (k = 0; k < LM_NCOMP; k++)
                if (m->want[k])
                    comp_ok[k] = 0;
            failed = 1;
            goto finish;
        }
    }
    lm_lock(m);
    m->total = total;
    lm_set_done_locked(m, have_parts);
    lm_unlock(m);

    /* 4. THE DOWNLOADS, in fetching order (movies, then music, smallest first). The bar
     *    is the files already finished, plus the one arriving, plus what earlier
     *    attempts left in the part files still to come, so it never steps back when
     *    the next file starts. */
    rest = have_parts;
    for (i = 0; i < nfetch; i++) {
        const LM_File *f = fetch[i];
        char path[1600], pwhy[400];
        int got, n = 0, count = 0, j;
        for (j = 0; j < nfetch; j++) {
            if (fetch[j]->comp != f->comp)
                continue;
            count++;
            if (j <= i)
                n++;
        }
        if (m->cancel) {
            cancelled = 1;
            goto finish;
        }
        rest -= parts[i];
        lm_set_item(m, f->comp, "", lm_base(f->rel), n, count);
        if (!lm_path(m, f->rel, path, sizeof path, pwhy, sizeof pwhy)) {
            comp_ok[f->comp] = 0;
            failed = 1;
            snprintf(why, sizeof why, "%s", pwhy);
            goto finish;
        }
        lm_make_parent(path);
        got = lm_fetch_one(m, &job, f, path, finished + rest);
        if (got == -1) {
            cancelled = 1;
            goto finish;
        }
        if (got != 1) {
            /* No host gave this file. The rest would fare no better from the same
             * hosts, so the job stops here, and the next one resumes from here. */
            comp_ok[f->comp] = 0;
            failed = 1;
            if (got == 0 && !job.reached) {
                offline = 1;
                snprintf(why, sizeof why,
                         "could not reach the download servers, so the movies and music were not "
                         "downloaded. The game plays without them, and the launcher's settings "
                         "can add them later");
            } else if (got == 0) {
                snprintf(why, sizeof why, "%.150s could not be downloaded: %.300s", lm_base(f->rel),
                         job.why);
            } else {
                snprintf(why, sizeof why, "%s", job.why);
            }
            goto finish;
        }
        finished += f->size;
        lm_lock(m);
        lm_set_done_locked(m, finished + rest);
        lm_unlock(m);
    }

finish:
    /* The record says where the disk got to. A wanted component is complete only when
     * every one of its files was checked or fetched in this job and nothing stopped
     * the job; an unwanted one is none only when every name it owns is gone. */
    if (held && record_ready) {
        for (k = 0; k < LM_NCOMP; k++) {
            if (m->want[k])
                state[k] = (comp_ok[k] && !failed && !cancelled) ? "complete" : "pending";
            else if (m->add_only)
                state[k] = m->keep_state[k];
            else
                state[k] = comp_ok[k] ? "none" : "pending";
        }
        if (!lm_write_record(m, state) && !failed && !cancelled) {
            snprintf(why, sizeof why, "opencnc3d-media.txt could not be written in the game folder");
            failed = 1;
        }
    }
    if (own)
        lz_unlock(&lk);
    free(order);
    free(fetch);
    free(parts);

    lm_lock(m);
    if (cancelled) {
        m->phase = LM_CANCELLED;
        snprintf(m->status, sizeof m->status, "%s",
                 "the download was stopped. What arrived is kept, and the next one carries on "
                 "from there");
    } else if (failed) {
        m->phase = LM_FAILED;
        m->offline = offline;
        snprintf(m->status, sizeof m->status, "%s", why[0] ? why : "the download failed");
    } else {
        m->phase = LM_DONE;
        m->status[0] = '\0';
        m->item[0] = '\0';
        if (m->total > 0)
            lm_set_done_locked(m, m->total);
    }
    lm_unlock(m);
    return 0;
}

static int lm_apply_mode(LM_State *m, int want_movies, int want_music, int add_only)
{
    LM_Phase p;
    if (!m)
        return 0;
    if (!m->ok)
        return 0; /* lm_status already says why */
    p = lm_phase(m);
    if (p == LM_FETCHING || p == LM_CHECKING || p == LM_REMOVING)
        return 0;
    if (m->worker) {
        SDL_WaitThread(m->worker, NULL);
        m->worker = NULL;
    }
    lm_lock(m);
    m->add_only = add_only;
    m->want[LM_MOVIES] = want_movies ? 1 : 0;
    m->want[LM_MUSIC] = want_music ? 1 : 0;
    m->done = m->total = m->shown = 0;
    m->item[0] = '\0';
    m->status[0] = '\0';
    m->offline = 0;
    m->cancel = 0;
    /* The worker's first report, set before it exists, so a caller that asks at once
     * sees a job rather than the idle state it came from. */
    m->phase = (want_movies || want_music || add_only) ? LM_CHECKING : LM_REMOVING;
    lm_unlock(m);
    m->worker = SDL_CreateThread(lm_thread, "opencnc3d-media", m);
    if (!m->worker) {
        lm_lock(m);
        m->phase = LM_FAILED;
        snprintf(m->status, sizeof m->status, "could not start the download worker");
        lm_unlock(m);
        return 0;
    }
    return 1;
}

int lm_apply(LM_State *m, int want_movies, int want_music)
{
    return lm_apply_mode(m, want_movies, want_music, 0);
}

int lm_apply_add_only(LM_State *m, int movies, int music)
{
    return lm_apply_mode(m, movies, music, 1);
}
