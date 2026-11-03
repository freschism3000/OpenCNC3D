/*
 * lmodule.c -- see lmodule.h.
 */

#include "lmodule.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* WHAT MAKES RED ALERT INSTALLED is what a package carries for it, and nothing a
 * package leaves out. Its movies (ra3d/movies/) and its score (ra3d/audio/SCORES.MIX)
 * are on the media list no package may carry, and the game plays without them, by
 * file presence alone: a missing movie is skipped and a missing score is silence. The
 * test once asked for REDINTRO.VQA, PROLOG.VQA and SCORES.MIX as well, so a folder
 * without the movies and the music showed Red Alert's slot as "Coming soon" and
 * --module ra3d --play refused it. tools/launcher/module-check.sh holds a folder
 * shaped like a package to this list. */
const char *const lmod_ra_needs[] = {
#if defined(_WIN32) || defined(__linux__)
    "ra3d/opencnc3d-game.exe",
    "ra3d/RedAlert.dll",
#else
    "ra3d/opencnc3d",
    "ra3d/RedAlert.dylib",
#endif
    "ra3d/dosmenu.pack",
    "ra3d/SCG01EA.pack",
    "ra3d/audio/SOUNDS.MIX",
    "ra3d/audio/SPEECH.MIX",
    "ra3d/audio/RA_LOCAL.MIX",
    "ra3d/missions/SCG01EA.INI",
    "ra3d/missions/TUTORIAL.INI",
    "ra3d/missions/RULES.INI"
};

const int lmod_ra_nneeds = (int)(sizeof lmod_ra_needs / sizeof *lmod_ra_needs);

static int lmod_has_file(const char *dir, const char *name)
{
    char path[1200];
    FILE *file;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

int lmod_ra_present(const char *dir, char *missing, int missinglen)
{
    int i;
    for (i = 0; i < lmod_ra_nneeds; i++)
        if (!lmod_has_file(dir, lmod_ra_needs[i])) {
            if (missing && missinglen > 0)
                snprintf(missing, (size_t)missinglen, "%s is missing", lmod_ra_needs[i]);
            return 0;
        }
    return 1;
}

int lmod_known_folder(const char *folder)
{
    return folder && !strcmp(folder, LMOD_RA_DIR);
}

int lmod_folder_installed(const char *dir, const char *folder)
{
    if (!dir || !folder)
        return 0;
    if (!strcmp(folder, LMOD_RA_DIR))
        return lmod_ra_present(dir, NULL, 0);
    return 0;
}

#include "lzip.h"
#include <errno.h>
#include <sys/stat.h>
#include "lmodule_files.h"

static const char *const *lmod_owned_files(const char *dir, int target);
static int lmod_install_public(const char *dir, int target, const char *build, const char *zip,
                                int (*cb)(void *, int, int), void *user, char *err, int len);

static int lmod_cache_path(const char *dir, int target, const char *suffix, char *out)
{
    char rel[128];
    if (target < 0 || target > 1) return 0;
    snprintf(rel, sizeof rel, ".opencnc3d-modules/%s.zip%s", target ? "ra3d" : "td", suffix);
    return lz_inside(dir, rel, out, 1400) && !lz_is_symlink(out);
}
int lmod_cached(const char *dir, int target)
{
    char path[1400]; struct stat st;
    return lmod_cache_path(dir, target, "", path) && !stat(path, &st) && S_ISREG(st.st_mode) &&
           lmod_cache_path(dir, target, ".sha256", path) && !stat(path, &st) && S_ISREG(st.st_mode) &&
           lmod_cache_path(dir, target, ".version", path) && !stat(path, &st) && S_ISREG(st.st_mode);
}
int lmod_install_cached(const char *dir, int target, const char *build, int (*cb)(void *, int, int), void *user,
                         char *err, int len)
{
    char zip[1400], sidecar[1400], expected[80], actual[65], version[100]; FILE *f;
#if defined(_WIN32) || defined(__linux__)
    static const char *const td_required[] = {"TiberianDawn.dll", "CONQUER.MIX"};
#else
    static const char *const td_required[] = {"TiberianDawn.dylib", "CONQUER.MIX"};
#endif
    if (lz_is_private_developer(dir)) {
        snprintf(err, (size_t)len, "This private source build is updated from its source checkout, not public game packages.");
        return 0;
    }
    snprintf(err, (size_t)len, "The local game package could not be read.");
    if (!lz_held_here(dir) || !lmod_cached(dir, target)) {
        snprintf(err, (size_t)len, "A verified local install package is not available for this game."); return 0;
    }
    if(!lmod_settle(dir,err,len))return 0;
    lmod_cache_path(dir, target, ".version", sidecar); f = fopen(sidecar, "rb");
    if (!f) return 0;
    version[0] = 0; if (fscanf(f, "%99s", version) != 1) version[0] = 0; fclose(f);
    if ((!build || strcmp(version, build))) {
        snprintf(err, (size_t)len, "This game's install package needs refreshing for the current launcher build."); return 0;
    }
    lmod_cache_path(dir, target, ".sha256", sidecar); f = fopen(sidecar, "rb");
    if (!f) return 0;
    expected[0] = 0; if (fscanf(f, "%79s", expected) != 1) expected[0] = 0; fclose(f);
    lmod_cache_path(dir, target, "", zip);
    if (!lz_sha256_file(zip, actual, err, len)) return 0;
    if (strlen(expected) != 64 || strcmp(expected, actual)) {
        snprintf(err, (size_t)len, "The local game package failed its checksum. No game files were changed."); return 0;
    }
    {
        char manifest[80], probe[256]; char *text;
        snprintf(manifest,sizeof manifest,"opencnc3d-module-%s.txt",target?"ra":"td");
        text=lz_read_text(zip,manifest,4*1024*1024,probe,sizeof probe);
        if(text){free(text);return lmod_install_public(dir,target,build,zip,cb,user,err,len);}
    }
    return lz_install_owned(dir, zip, target ? lmod_ra_files : lmod_td_files,
                             target ? lmod_ra_needs : td_required, target ? lmod_ra_nneeds : 2,
                             cb, user, err, len);
}

int lmod_td_present(const char *dir)
{
#if defined(_WIN32) || defined(__linux__)
    return lmod_has_file(dir, "TiberianDawn.dll") && lmod_has_file(dir, "opencnc3d-game.exe");
#else
    return lmod_has_file(dir, "TiberianDawn.dylib") && lmod_has_file(dir, "opencnc3d");
#endif
}

static int lmod_paths(const char *dir, const char *rel, char *path, char *staged)
{
    char temp[1200];
    if (snprintf(temp, sizeof temp, "%s.module-removing", rel) >= (int)sizeof temp)
        return 0;
    return lz_inside(dir, rel, path, 1400) && !lz_is_symlink(path) &&
           lz_inside(dir, temp, staged, 1400) && !lz_is_symlink(staged);
}

/* A recoverable transaction: stage exact owned files beside themselves, commit,
 * then unlink. An interrupted staging rolls back; an interrupted cleanup resumes.
 * Never enumerate a player's directory or follow a link, even on Windows. */
int lmod_settle(const char *dir, char *err, int len)
{
    char journal[1400], path[1400], staged[1400];
    const char *const *files;
    FILE *f;
    struct stat st;
    int target, committed, i, ok = 1;
    if (!lz_held_here(dir) || !lz_inside(dir, "opencnc3d-module-removal.txt", journal, sizeof journal) || lz_is_symlink(journal))
        goto failure;
    f = fopen(journal, "rb");
    if (!f) {if (errno == ENOENT) return 1;goto failure;}
    if (fscanf(f, "%d %d", &target, &committed) != 2 || target < 0 || target > 1 || committed < 0 || committed > 1) {
        fclose(f); goto failure;
    }
    fclose(f);
    files = lmod_owned_files(dir, target);
    if (!files) goto failure;
    for (i = 0; files[i]; i++) {
        if (!lmod_paths(dir, files[i], path, staged)) {ok = 0;continue;}
        if (stat(staged, &st)) {if (errno != ENOENT) ok = 0;continue;}
        if (!S_ISREG(st.st_mode)) {ok = 0;continue;}
        if (committed) {
            /* The manifest is the only durable list of Enhanced files. Keep it
             * for retry if any earlier cleanup failed (e.g. a Windows file lock). */
            if (!ok && !strncmp(files[i], "opencnc3d-module-", 17)) continue;
            if (remove(staged)) ok = 0;
        }
        else if (stat(path, &st) == 0 || errno != ENOENT || rename(staged, path)) ok = 0;
    }
    if (ok && remove(journal) == 0) return 1;
failure:
    snprintf(err, (size_t)len, "Module removal needs recovery; files were kept. Close other launchers and retry.");
    return 0;
}

int lmod_remove(const char *dir, int target, char *err, int len)
{
    const char *const *files;
    char path[1400], staged[1400], journal[1400];
    FILE *f;
    struct stat st;
    int i, bad = 0;
    if (target < 0 || target > 1 || !lz_held_here(dir)) {
        snprintf(err, (size_t)len, "Cannot remove a module without the installation lock.");
        return 0;
    }
    if (!lmod_settle(dir, err, len)) return 0;
    files = lmod_owned_files(dir, target);
    if (!files) goto failure;
    /* Preflight the entire transaction before moving a single file. */
    for (i = 0; files[i]; i++) {
        if (!lmod_paths(dir, files[i], path, staged)) {bad = 1;break;}
        if (stat(path, &st) == 0) {if (!S_ISREG(st.st_mode)) {bad = 1;break;}}
        else if (errno != ENOENT) {bad = 1;break;}
        if (stat(staged, &st) == 0 || errno != ENOENT) {bad = 1;break;}
    }
    if (bad) {
        snprintf(err, (size_t)len, "Removal refused: an owned file is linked, blocked or has a recovery copy.");
        return 0;
    }
    if (!lz_inside(dir, "opencnc3d-module-removal.txt", journal, sizeof journal) || lz_is_symlink(journal)) return 0;
    f = fopen(journal, "wb");
    if (!f) goto failure;
    bad = fprintf(f, "%d 0\n", target) < 0;
    if (fclose(f)) bad = 1;
    if (bad) goto failure;
    for (i = 0; files[i]; i++) {
        if (!lmod_paths(dir, files[i], path, staged)) goto rollback;
        if (stat(path, &st)) {if (errno == ENOENT) continue;goto rollback;}
        if (rename(path, staged)) goto rollback;
    }
    /* The single commit byte changes only after every move succeeded. A torn
     * staging journal is never interpreted as permission to delete. */
    f = fopen(journal, "r+b");
    if (!f) goto rollback;
    if (fseek(f, 2, SEEK_SET) || fputc('1', f) == EOF) {fclose(f);goto rollback;}
    if (fclose(f)) goto failure;
    return lmod_settle(dir, err, len);
rollback:
    lmod_settle(dir, err, len);
failure:
    snprintf(err, (size_t)len, "Module removal could not finish. Close the game and retry; saves and settings are kept.");
    return 0;
}

#include "lmodule_remote.h"
