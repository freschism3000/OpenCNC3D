/*
 * lupdate.c -- see lupdate.h.
 *
 * THE SITE IS THE SOURCE OF TRUTH, and it already was before this file existed.
 * cnc3dgame.com's Builds section is drawn from three routes, and the launcher
 * reads exactly the same three:
 *
 *   GET /api/builds       the newest release: its tag, and every asset with an
 *                         id, a size, a platform (macos|windows) and a kind
 *                         (full|binaries)
 *   GET /api/changelog    the changelog, entry by entry, bodies in markdown
 *   GET /api/download?asset=<id>
 *                         a 302 to a short-lived signed GitHub asset URL
 *
 * So there is no second place to publish to and no key to ship. The repo is
 * private and its release assets are not public, but the site proxies them, which
 * means the launcher and the website cannot disagree about what the newest build
 * is: they are reading one answer. Putting a build on the site puts it in every
 * launcher, and there is no third step to forget.
 *
 * WHAT THE SITE DOES NOT SAY, and what this does about it. /api/builds gives a
 * size per asset, no checksum, and nothing about whether the DATA changed between
 * two builds. Both matter:
 *
 *   - Without a checksum, a truncated or proxy-mangled download can only be
 *     caught by its length. That catches the common failure and nothing subtle.
 *   - Without a data fingerprint the launcher cannot prove the small binary-only
 *     package is enough, so every update becomes the full 500 MB one.
 *
 * Both are answered by ONE SMALL FILE published as an ordinary release asset
 * beside the zips (`CNC3D-vX.Y.Z-manifest.txt`, written by
 * tools/launcher/make-manifest.sh). The launcher looks for it in the asset list
 * and, finding it, gets a SHA-256 for every zip and the data fingerprint for this
 * platform. Not finding it, it falls back to the full package checked by length,
 * which is correct and merely larger. That fallback is why none of this needs a
 * change to the website.
 */

#include "lupdate.h"
#include "lcfg.h"
#include "ljson.h"
#include "lnet.h"
#include "lpath.h"
#include "lzip.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define LU_PLATFORM "windows"
#else
#include <unistd.h>
#define LU_PLATFORM "macos"
#endif

/* The site, unless a build was pointed somewhere else. A staging deployment, a
 * local `next dev`, or the selftest's fake site are the only reasons to;
 * tools/launcher/make-config.sh is where that is set. */
#define LU_SITE (LCFG_BASE[0] ? LCFG_BASE : "https://cnc3dgame.com")

#define LU_API_CAP (4 * 1024 * 1024) /* /api/changelog is the big one */

typedef struct
{
    long long id;
    char name[256];
    long long size;
    char sha[80]; /* empty unless the release published a manifest asset */
} LU_Asset;

struct LU_State
{
    SDL_mutex *lock;
    SDL_Thread *worker;

    char dir[1024];
    char installed[64];
    char installed_data[80];

    /* Written by the worker under the lock, read by the UI thread. */
    LU_Phase phase;
    int progress; /* thousandths, -1 unknown */
    char status[256];
    char latest[64];
    char latest_data[80];
    LU_Asset full, bins, manifest;
    char *notes;
    int notes_dirty;
    int cancel;
};

/* ------------------------------------------------------------------------ *
 * The little accessors. Every one takes the lock, because the whole point of
 * the worker is that the UI thread never waits on the network.
 * ------------------------------------------------------------------------ */

static void lu_set(LU_State *u, LU_Phase phase, const char *status)
{
    SDL_LockMutex(u->lock);
    u->phase = phase;
    if (status)
        snprintf(u->status, sizeof u->status, "%s", status);
    SDL_UnlockMutex(u->lock);
}

LU_Phase lu_phase(const LU_State *u)
{
    LU_Phase p;
    if (!u)
        return LU_NOTCONFIGURED;
    SDL_LockMutex(u->lock);
    p = u->phase;
    SDL_UnlockMutex(u->lock);
    return p;
}

int lu_progress(const LU_State *u)
{
    int p;
    if (!u)
        return -1;
    SDL_LockMutex(u->lock);
    p = u->progress;
    SDL_UnlockMutex(u->lock);
    return p;
}

void lu_status(LU_State *u, char *buf, int len)
{
    if (!u) {
        snprintf(buf, (size_t)len, " ");
        return;
    }
    SDL_LockMutex(u->lock);
    snprintf(buf, (size_t)len, "%s", u->status);
    SDL_UnlockMutex(u->lock);
}

void lu_latest(LU_State *u, char *buf, int len)
{
    if (!u) {
        snprintf(buf, (size_t)len, "?");
        return;
    }
    SDL_LockMutex(u->lock);
    snprintf(buf, (size_t)len, "%s", u->latest[0] ? u->latest : "?");
    SDL_UnlockMutex(u->lock);
}

const char *lu_notes(LU_State *u)
{
    const char *n;
    if (!u)
        return NULL;
    SDL_LockMutex(u->lock);
    n = u->notes;
    SDL_UnlockMutex(u->lock);
    return n;
}

int lu_notes_changed(LU_State *u)
{
    int d;
    if (!u)
        return 0;
    SDL_LockMutex(u->lock);
    d = u->notes_dirty;
    u->notes_dirty = 0;
    SDL_UnlockMutex(u->lock);
    return d;
}

const char *lu_host_summary(void)
{
    return LU_SITE;
}

/* ------------------------------------------------------------------------ *
 * Versions.
 *
 * "0.6.3" against "0.6.2". A build made off a release commit stamps itself
 * "0.6.2+3a1f2c-dirty" (tools/version.sh), so parsing stops at the first thing
 * that is not a digit or a dot and a dirty local build compares as its base
 * number rather than as garbage. The site's tags carry a leading v.
 * ------------------------------------------------------------------------ */

static void lu_parse_version(const char *s, int out[3])
{
    int i;
    out[0] = out[1] = out[2] = 0;
    if (!s)
        return;
    while (*s == 'v' || *s == 'V')
        s++;
    for (i = 0; i < 3 && *s; i++) {
        int n = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (*s - '0');
            s++;
            digits++;
        }
        if (!digits)
            break;
        out[i] = n;
        if (*s != '.')
            break;
        s++;
    }
}

/* >0 when `a` is newer than `b`. An installed version of "?" is older than
 * anything, which is the honest answer for a folder that predates the launcher:
 * it cannot prove it is current, so it is offered the update. */
static int lu_newer(const char *a, const char *b)
{
    int va[3], vb[3], i;
    if (!b || !*b || !strcmp(b, "?"))
        return 1;
    lu_parse_version(a, va);
    lu_parse_version(b, vb);
    for (i = 0; i < 3; i++)
        if (va[i] != vb[i])
            return va[i] - vb[i];
    return 0;
}

/* ------------------------------------------------------------------------ *
 * URLs.
 * ------------------------------------------------------------------------ */

static char *lu_api(const char *path, char *buf, int len)
{
    const char *base = LU_SITE;
    size_t n = strlen(base);
    while (n && base[n - 1] == '/')
        n--;
    snprintf(buf, (size_t)len, "%.*s%s", (int)n, base, path);
    return buf;
}

static char *lu_asset_url(long long id, char *buf, int len)
{
    char path[128];
    snprintf(path, sizeof path, "/api/download?asset=%lld", id);
    return lu_api(path, buf, len);
}

/* ------------------------------------------------------------------------ *
 * /api/builds
 * ------------------------------------------------------------------------ */

static void lu_take_asset(LU_Asset *a, const LJ_Value *item)
{
    const char *name = lj_str(item, "name");
    a->id = lj_num(item, "id", -1);
    a->size = lj_num(item, "size", -1);
    a->sha[0] = '\0';
    snprintf(a->name, sizeof a->name, "%s", name ? name : "");
}

static int lu_read_builds(LU_State *u, const char *json, char *err, int errlen)
{
    LJ_Value *root;
    const LJ_Value *latest, *assets, *item;
    const char *tag;
    char jerr[256];
    int found_full;

    root = lj_parse(json, jerr, sizeof jerr);
    if (!root) {
        snprintf(err, (size_t)errlen, "the site's build list could not be read: %.180s", jerr);
        return 0;
    }
    if (!lj_bool(root, "ok", 1)) {
        snprintf(err, (size_t)errlen, "cnc3dgame.com reported a problem with its builds");
        lj_free(root);
        return 0;
    }
    latest = lj_get(root, "latest");
    tag = lj_str(latest, "tag");
    if (!tag || !*tag) {
        /* An empty build list is a real state on a fresh deployment, and it is
         * not an error the player can act on. Said as itself. */
        snprintf(err, (size_t)errlen, "cnc3dgame.com is not listing any build yet");
        lj_free(root);
        return 0;
    }

    SDL_LockMutex(u->lock);
    snprintf(u->latest, sizeof u->latest, "%s", tag[0] == 'v' ? tag + 1 : tag);
    memset(&u->full, 0, sizeof u->full);
    memset(&u->bins, 0, sizeof u->bins);
    memset(&u->manifest, 0, sizeof u->manifest);
    u->full.id = u->bins.id = u->manifest.id = -1;
    u->latest_data[0] = '\0';

    assets = lj_get(latest, "assets");
    for (item = lj_first(assets); item; item = lj_next(item)) {
        const char *platform = lj_str(item, "platform");
        const char *kind = lj_str(item, "kind");
        const char *name = lj_str(item, "name");

        /* THE MANIFEST IS MATCHED BY NAME, not by platform or kind, because the
         * site classifies assets from their filenames and a .txt is not a
         * category it has. Whatever it decides to call the thing, the NAME is
         * ours and it contains "manifest". */
        if (name && strstr(name, "manifest")) {
            lu_take_asset(&u->manifest, item);
            continue;
        }
        if (!platform || strcmp(platform, LU_PLATFORM))
            continue;
        if (kind && !strcmp(kind, "full"))
            lu_take_asset(&u->full, item);
        else if (kind && !strcmp(kind, "binaries"))
            lu_take_asset(&u->bins, item);
    }
    found_full = u->full.id > 0;
    SDL_UnlockMutex(u->lock);

    lj_free(root);
    if (!found_full) {
        snprintf(err, (size_t)errlen,
                 "the newest build on the site has no " LU_PLATFORM " package");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------ *
 * The manifest asset: a SHA-256 per zip, and the data fingerprint per platform.
 * Optional, and its absence costs the small update and the checksum, nothing
 * else.
 * ------------------------------------------------------------------------ */

static void lu_read_manifest(LU_State *u, const char *text)
{
    char line[1024];
    const char *p = text;

    SDL_LockMutex(u->lock);
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char key[64], name[256], size[64], sha[80];

        if (n >= sizeof line)
            n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        p = nl ? nl + 1 : p + strlen(p);
        if (!line[0] || line[0] == '#')
            continue;

        if (sscanf(line, "%63s %255s %63s %79s", key, name, size, sha) == 4
            && strlen(sha) == 64) {
            /* Matched by NAME, not by the manifest's own platform key: the asset
             * ids came from the site and the filenames are what the two lists
             * share. A manifest that named a file the site is not serving would
             * otherwise attach a hash to the wrong download. */
            if (u->full.name[0] && !strcmp(name, u->full.name))
                snprintf(u->full.sha, sizeof u->full.sha, "%s", sha);
            else if (u->bins.name[0] && !strcmp(name, u->bins.name))
                snprintf(u->bins.sha, sizeof u->bins.sha, "%s", sha);
            continue;
        }
        if (sscanf(line, "%63s %79s", key, sha) == 2
            && !strcmp(key, LU_PLATFORM "_data_id"))
            snprintf(u->latest_data, sizeof u->latest_data, "%s", sha);
    }
    SDL_UnlockMutex(u->lock);
}

/* ------------------------------------------------------------------------ *
 * /api/changelog -> the panel's text.
 *
 * Rendered back into the markdown lui_text_build already reads, rather than
 * teaching the panel a second format. The site hands over the pieces (title,
 * date, body); this puts the heading back on the front of each one, in the shape
 * docs/CHANGELOG.md uses, so the online panel and the offline one look identical.
 * ------------------------------------------------------------------------ */

static char *lu_read_changelog(const char *json, char *err, int errlen)
{
    LJ_Value *root;
    const LJ_Value *entries, *e;
    char *out;
    size_t cap = 64 * 1024, len = 0;
    char jerr[256];

    root = lj_parse(json, jerr, sizeof jerr);
    if (!root) {
        snprintf(err, (size_t)errlen, "the site's changelog could not be read: %.180s", jerr);
        return NULL;
    }
    out = (char *)malloc(cap);
    if (!out) {
        lj_free(root);
        snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    out[0] = '\0';

    entries = lj_get(root, "entries");
    for (e = lj_first(entries); e; e = lj_next(e)) {
        const char *title = lj_str(e, "title");
        const char *date = lj_str(e, "date");
        const char *body = lj_str(e, "body");
        size_t need;

        if (!title)
            continue;
        need = strlen(title) + (date ? strlen(date) : 0) + (body ? strlen(body) : 0) + 16;
        if (len + need + 1 > cap) {
            char *grown;
            while (len + need + 1 > cap)
                cap *= 2;
            grown = (char *)realloc(out, cap);
            if (!grown) {
                free(out);
                lj_free(root);
                snprintf(err, (size_t)errlen, "out of memory");
                return NULL;
            }
            out = grown;
        }
        len += (size_t)snprintf(out + len, cap - len, "## %s%s%s%s\n\n%s\n\n", title,
                                date ? " (" : "", date ? date : "", date ? ")" : "",
                                body ? body : "");
    }
    lj_free(root);
    if (!len) {
        free(out);
        snprintf(err, (size_t)errlen, "the site's changelog is empty");
        return NULL;
    }
    return out;
}

/* ------------------------------------------------------------------------ *
 * cnc3d-install.txt: what this folder holds. Written by the release packager and
 * rewritten here after an update, so the next check has something true to compare.
 * ------------------------------------------------------------------------ */

static void lu_trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' '))
        s[--n] = '\0';
}

static void lu_read_install(LU_State *u)
{
    char path[1200], line[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/cnc3d-install.txt", u->dir);
    f = fopen(path, "rb");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        lu_trim(line);
        if (!strncmp(line, "data_id ", 8)) {
            u->installed_data[0] = '\0';
            sscanf(line + 8, "%79s", u->installed_data);
        }
    }
    fclose(f);
}

static int lu_write_install(LU_State *u, char *err, int errlen)
{
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/cnc3d-install.txt", u->dir);
    f = fopen(path, "wb");
    if (!f) {
        snprintf(err, (size_t)errlen, "could not record the new version in %.300s", path);
        return 0;
    }
    fprintf(f, "# written by the C&C 3D launcher after an update\n");
    fprintf(f, "version %s\n", u->latest);
    fprintf(f, "data_id %s\n", u->latest_data[0] ? u->latest_data : "unknown");
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------------ *
 * Replacing the launcher underneath itself.
 *
 * The update zips carry every binary in the folder, and on Windows one of them
 * is the .exe currently running, which cannot be opened for writing. Renaming it
 * CAN be done while it runs, on Windows and on macOS both, and leaves the running
 * image untouched: the new file then lands on the free name. The stale .old is
 * swept on the next start, when nothing holds it.
 * ------------------------------------------------------------------------ */

/* The running launcher's path RELATIVE TO THE INSTALL, in the form a zip entry
 * uses. Returns 0 when the launcher lives outside the folder being updated, which
 * is the ordinary case during development and means there is nothing to step
 * aside for. */
static int lu_self_rel(const char *dir, char *rel, int rellen)
{
    char self[1200], *p;
    size_t n = strlen(dir);
    if (!lp_self(self, sizeof self))
        return 0;
    if (strncmp(self, dir, n) != 0)
        return 0;
    if (self[n] != '/' && self[n] != '\\')
        return 0;
    snprintf(rel, (size_t)rellen, "%s", self + n + 1);
    for (p = rel; *p; p++)
        if (*p == '\\')
            *p = '/'; /* zip entries are forward slashed on every platform */
    return rel[0] != '\0';
}

/* ONLY WHEN THE ZIP ACTUALLY CARRIES A REPLACEMENT, and the selftest is why that
 * sentence is here. The first version stepped aside unconditionally: against a
 * binary-only zip that did not contain the launcher, nothing replaced it, so the
 * update reported success and left the folder with no front door. A successful
 * update that removes the program you launched it from is the worst failure this
 * file can produce, so the archive is asked first. */
static int lu_step_aside(void)
{
    char self[1200], old[1300];
    if (!lp_self(self, sizeof self))
        return 0;
    snprintf(old, sizeof old, "%s.old", self);
    remove(old);
    return rename(self, old) == 0;
}

/* And back again, when the extraction that was going to replace it failed. */
static void lu_step_back(void)
{
    char self[1200], old[1300];
    if (!lp_self(self, sizeof self))
        return;
    snprintf(old, sizeof old, "%s.old", self);
    rename(old, self);
}

static void lu_sweep_old(void)
{
    char self[1200], old[1300];
    if (!lp_self(self, sizeof self))
        return;
    snprintf(old, sizeof old, "%s.old", self);
    remove(old);
}

/* ------------------------------------------------------------------------ *
 * The worker.
 * ------------------------------------------------------------------------ */

static int lu_dl_progress(void *user, long long done, long long total)
{
    LU_State *u = (LU_State *)user;
    int cancel;
    SDL_LockMutex(u->lock);
    u->progress = (total > 0) ? (int)((done * 1000) / total) : -1;
    cancel = u->cancel;
    SDL_UnlockMutex(u->lock);
    return !cancel;
}

static int lu_zip_progress(void *user, int done, int total)
{
    LU_State *u = (LU_State *)user;
    int cancel;
    SDL_LockMutex(u->lock);
    u->progress = total > 0 ? (done * 1000) / total : -1;
    cancel = u->cancel;
    SDL_UnlockMutex(u->lock);
    return !cancel;
}

static int lu_do_check(LU_State *u, char *err, int errlen)
{
    char url[1400];
    char *text;

    lu_set(u, LU_CHECKING, "Asking cnc3dgame.com for the newest build...");
    text = ln_get_text(lu_api("/api/builds", url, sizeof url), LCFG_KEY, LU_API_CAP, err,
                       errlen);
    if (!text)
        return 0;
    {
        int ok = lu_read_builds(u, text, err, errlen);
        free(text);
        if (!ok)
            return 0;
    }

    /* The manifest asset, when the release carries one. */
    if (u->manifest.id > 0) {
        char merr[256];
        char *m = ln_get_text(lu_asset_url(u->manifest.id, url, sizeof url), LCFG_KEY,
                              LU_API_CAP, merr, sizeof merr);
        if (m) {
            lu_read_manifest(u, m);
            free(m);
        }
    }

    /* The changelog, always, and for the newest build rather than for the one
     * installed: the panel's job at this moment is to answer "what would I get".
     * A changelog that will not load is not a failed check; the version numbers
     * are the answer and the notes are the courtesy. */
    {
        char cerr[256];
        text = ln_get_text(lu_api("/api/changelog", url, sizeof url), LCFG_KEY,
                           LU_API_CAP, cerr, sizeof cerr);
        if (text) {
            char *notes = lu_read_changelog(text, cerr, sizeof cerr);
            free(text);
            if (notes) {
                SDL_LockMutex(u->lock);
                free(u->notes);
                u->notes = notes;
                u->notes_dirty = 1;
                SDL_UnlockMutex(u->lock);
            }
        }
    }
    return 1;
}

static int lu_do_apply(LU_State *u, char *err, int errlen)
{
    char url[1400], zip[1300], hex[80];
    LU_Asset *a;
    int strip, small;
    char msg[256];

    lu_read_install(u);

    /* The small update when the data provably matches, the whole package when it
     * does not or when the release published nothing to check it against. */
    SDL_LockMutex(u->lock);
    small = u->bins.id > 0 && u->latest_data[0] && u->installed_data[0]
            && !strcmp(u->latest_data, u->installed_data);
    if (small) {
        a = &u->bins;
        strip = 0; /* the bins zip is flat: binaries at the root */
    } else {
        a = &u->full;
        strip = 1; /* the full zip wraps everything in CNC3D-<platform>-vX.Y.Z/ */
    }
    SDL_UnlockMutex(u->lock);

    snprintf(msg, sizeof msg, "Downloading %.180s (%lld MB)...", a->name,
             (a->size + 524288) / 1048576);
    SDL_LockMutex(u->lock);
    u->progress = 0;
    SDL_UnlockMutex(u->lock);
    lu_set(u, LU_DOWNLOADING, msg);

    snprintf(zip, sizeof zip, "%s/cnc3d-update.zip", u->dir);
    if (!ln_get_file(lu_asset_url(a->id, url, sizeof url), LCFG_KEY, zip, lu_dl_progress,
                     u, err, errlen))
        return 0;

    lu_set(u, LU_APPLYING, "Checking the download...");
    SDL_LockMutex(u->lock);
    u->progress = -1;
    SDL_UnlockMutex(u->lock);

    if (a->sha[0]) {
        if (!lz_sha256_file(zip, hex, err, errlen)) {
            remove(zip);
            return 0;
        }
        if (strcmp(hex, a->sha) != 0) {
            /* Refuse, and say what it means. A hash mismatch is almost always an
             * interrupted or proxied download rather than an attack, and telling
             * a player to try again is more useful than telling them about
             * SHA-256. */
            snprintf(err, (size_t)errlen,
                     "the download did not arrive intact. Try Update again.");
            remove(zip);
            return 0;
        }
    } else if (a->size > 0) {
        /* NO CHECKSUM WAS PUBLISHED WITH THIS BUILD, so the length is all there
         * is. It catches a transfer that stopped early, which is the common
         * failure, and it catches nothing subtle. Better than nothing and worse
         * than a hash; the release only has to carry one small file to make it a
         * hash. See the header. */
        FILE *f = fopen(zip, "rb");
        long long got = -1;
        if (f) {
            fseek(f, 0, SEEK_END);
            got = (long long)ftell(f);
            fclose(f);
        }
        if (got != a->size) {
            snprintf(err, (size_t)errlen,
                     "the download stopped early (%lld of %lld bytes). Try again.", got,
                     a->size);
            remove(zip);
            return 0;
        }
    }

    lu_set(u, LU_APPLYING, "Installing...");
    {
        char rel[1200];
        int stepped = 0;
        if (lu_self_rel(u->dir, rel, sizeof rel) && lz_has_entry(zip, strip, rel))
            stepped = lu_step_aside();
        if (!lz_extract(zip, u->dir, strip, lu_zip_progress, u, err, errlen)) {
            if (stepped)
                lu_step_back();
            remove(zip);
            return 0;
        }
    }
    remove(zip);

    if (!lu_write_install(u, err, errlen))
        return 0;

    /* Keep the offline changelog current, so the next start shows the notes for
     * what is now installed even with no network. */
    SDL_LockMutex(u->lock);
    if (u->notes) {
        char path[1200];
        FILE *f;
        snprintf(path, sizeof path, "%s/CHANGELOG.txt", u->dir);
        f = fopen(path, "wb");
        if (f) {
            fwrite(u->notes, 1, strlen(u->notes), f);
            fclose(f);
        }
    }
    snprintf(u->installed, sizeof u->installed, "%s", u->latest);
    SDL_UnlockMutex(u->lock);
    return 1;
}

static int lu_thread(void *user)
{
    LU_State *u = (LU_State *)user;
    char err[512];
    LU_Phase want;

    SDL_LockMutex(u->lock);
    want = u->phase;
    SDL_UnlockMutex(u->lock);

    err[0] = '\0';
    if (want == LU_CHECKING) {
        if (!lu_do_check(u, err, sizeof err)) {
            lu_set(u, LU_FAILED, err);
            return 0;
        }
        SDL_LockMutex(u->lock);
        {
            int newer = lu_newer(u->latest, u->installed);
            char line[256];
            if (newer > 0)
                snprintf(line, sizeof line, "v%s is available. Press Update.", u->latest);
            else
                snprintf(line, sizeof line, "This is the newest build.");
            u->phase = (newer > 0) ? LU_AVAILABLE : LU_UPTODATE;
            snprintf(u->status, sizeof u->status, "%s", line);
        }
        SDL_UnlockMutex(u->lock);
        return 0;
    }

    if (want == LU_DOWNLOADING) {
        if (!lu_do_apply(u, err, sizeof err)) {
            lu_set(u, LU_FAILED, err);
            return 0;
        }
        lu_set(u, LU_DONE, "Updated. Press Play.");
        return 0;
    }
    return 0;
}

static void lu_start(LU_State *u, LU_Phase phase, const char *status)
{
    LU_Phase now = lu_phase(u);
    if (now == LU_CHECKING || now == LU_DOWNLOADING || now == LU_APPLYING)
        return; /* one at a time; a second press is not a second download */
    if (u->worker) {
        SDL_WaitThread(u->worker, NULL);
        u->worker = NULL;
    }
    lu_set(u, phase, status);
    u->worker = SDL_CreateThread(lu_thread, "cnc3d-update", u);
    if (!u->worker)
        lu_set(u, LU_FAILED, "could not start the update worker");
}

void lu_check(LU_State *u)
{
    if (!u)
        return;
    lu_start(u, LU_CHECKING, "Checking cnc3dgame.com for a newer build...");
}

void lu_apply(LU_State *u)
{
    if (!u)
        return;
    /* LU_DOWNLOADING is the worker's instruction as well as its report; the
     * thread reads the phase it was started in to know which job it has. */
    lu_start(u, LU_DOWNLOADING, "Starting the download...");
}

/* ------------------------------------------------------------------------ *
 * Life cycle.
 * ------------------------------------------------------------------------ */

LU_State *lu_create(const char *dir, const char *version)
{
    LU_State *u = (LU_State *)calloc(1, sizeof *u);
    if (!u)
        return NULL;
    u->lock = SDL_CreateMutex();
    u->progress = -1;
    u->full.id = u->bins.id = u->manifest.id = -1;
    snprintf(u->dir, sizeof u->dir, "%s", dir ? dir : ".");
    snprintf(u->installed, sizeof u->installed, "%s", version ? version : "?");
    lu_read_install(u);
    lu_sweep_old();

    /* There is always a site to ask, so LU_NOTCONFIGURED is no longer a state the
     * launcher can start in. The enum keeps the value because lu_phase returns it
     * for a NULL state, which is what a caller gets if the allocation failed. */
    u->phase = LU_IDLE;
    snprintf(u->status, sizeof u->status, " ");
    return u;
}

void lu_destroy(LU_State *u)
{
    if (!u)
        return;
    SDL_LockMutex(u->lock);
    u->cancel = 1;
    SDL_UnlockMutex(u->lock);
    if (u->worker)
        SDL_WaitThread(u->worker, NULL);
    free(u->notes);
    SDL_DestroyMutex(u->lock);
    free(u);
}
