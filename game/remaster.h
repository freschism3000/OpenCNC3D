/* ==================================================================================== *
 *  remaster.h -- find a Command & Conquer Remastered Collection install, if the player
 *  has one, so Enhanced Visuals can offer its terrain art as a third choice.
 *
 *  WE SHIP NONE OF THAT ART AND WE NEVER WILL. It is Electronic Arts' work in a product
 *  they sell. This reads it out of the player's OWN installation at runtime, which is the
 *  same footing as data/dosdata: your copy, your machine. If the install is not there the
 *  option is greyed out and says why, and nothing is substituted, which is the standing
 *  rule for anything this project cannot reproduce faithfully.
 *
 *  HOW IT LOOKS, and the ordering is deliberate:
 *
 *    1. CNC3D_REMASTER_DIR in the environment. An explicit answer always wins, which is
 *       also what makes this testable without owning the game.
 *    2. Every Steam library this machine can see. Steam records its extra libraries in
 *       steamapps/libraryfolders.vdf, and each library records what is installed in
 *       steamapps/appmanifest_<appid>.acf, whose "installdir" names the folder under
 *       steamapps/common. Both files are the same trivial quoted-token format.
 *    3. The EA App / Origin default folders on Windows.
 *
 *  A ROOT IS NOT TRUSTED UNTIL IT IS PROBED. rm_looks_like_install() is what decides,
 *  and it tests for what we are actually going to READ -- a data folder with .MEG
 *  archives in it -- rather than for an executable name. That is on purpose: the exact
 *  archive names are the part of this that documentation could not pin down, so the code
 *  looks for the shape and not for a filename somebody remembered.
 *
 *  CROSS-MOUNTED LIBRARIES ARE EXPECTED, not an edge case. A Steam library can be a
 *  Windows install on a mounted volume read from another operating system, and its
 *  libraryfolders.vdf then records a drive letter that means nothing where it is being
 *  read. So a recorded path that does not resolve is not a failure: the directory the
 *  .vdf was FOUND in is always searched too, and every mounted volume is a candidate
 *  root. Verified against exactly such a library.
 * ==================================================================================== */
#ifndef CNC3D_REMASTER_H
#define CNC3D_REMASTER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define RM_MAYBE_UNUSED __attribute__((unused))
#else
#define RM_MAYBE_UNUSED
#endif

#define RM_PATH_MAX 1024

/* The Steam application id of the Remastered Collection. Confirmed three ways (the live
   Steam store API, OpenRA's TiberianDawnHD mod.yaml, and the map editor's Program.cs).
   Still only a FAST PATH: when the manifest is not found by id, rm_scan_library falls
   back to matching the folder name, so even a wrong id costs a directory listing rather
   than the feature. */
#define RM_STEAM_APPID "1213210"

static int rm_is_dir(const char* p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static int rm_is_file(const char* p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

static void rm_join(char* out, size_t n, const char* a, const char* b)
{
    size_t la;
    if (!out || !n) return;
    snprintf(out, n, "%s", a ? a : "");
    la = strlen(out);
    if (la && out[la - 1] != '/' && out[la - 1] != '\\' && la + 1 < n) {
        out[la++] = '/';
        out[la] = 0;
    }
    if (b) snprintf(out + la, n - la, "%s", b);
}

/* Case-insensitive "does haystack contain needle". The folder names differ between
   storefronts ("Command and Conquer Remastered" / "CnCRemastered"), so matching is by
   substring rather than by equality. */
static int rm_icontains(const char* hay, const char* needle)
{
    size_t nl;
    if (!hay || !needle) return 0;
    nl = strlen(needle);
    if (!nl) return 1;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nl; i++) {
            char a = hay[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (!a || a != b) break;
        }
        if (i == nl) return 1;
    }
    return 0;
}

/* Pull the value of a quoted key out of Valve's KeyValues text ("key" "value"). Enough
   for .vdf and .acf, which are the same format and which we only ever read a couple of
   scalars out of. Returns 1 and fills `out` on the FIRST match. */
static int rm_kv_lookup(const char* path, const char* key, char* out, size_t n)
{
    char line[2048];
    FILE* f = fopen(path, "rb");
    int got = 0;
    if (!f) return 0;
    while (!got && fgets(line, sizeof line, f)) {
        const char* p = line;
        const char* ks;
        const char* ke;
        while (*p && *p != '"') p++;
        if (!*p) continue;
        ks = ++p;
        while (*p && *p != '"') p++;
        if (!*p) continue;
        ke = p++;
        if ((size_t)(ke - ks) != strlen(key) || strncmp(ks, key, (size_t)(ke - ks)) != 0)
            continue;
        while (*p && *p != '"') p++;
        if (!*p) continue;
        ks = ++p;
        while (*p && *p != '"') p++;
        if (!*p) continue;
        {
            size_t len = (size_t)(p - ks);
            size_t o = 0, i;
            /* Valve escapes the Windows separator as \; unescape so the string is a
               usable path on a machine that happens to be Windows. */
            for (i = 0; i < len && o + 1 < n; i++) {
                if (ks[i] == '\\' && i + 1 < len && ks[i + 1] == '\\') i++;
                out[o++] = ks[i];
            }
            out[o] = 0;
            got = 1;
        }
    }
    fclose(f);
    return got;
}

/* Find one entry of a directory by name, case-insensitively, and hand back the name as
   it is REALLY spelled. Needed rather than fussy: the two reference implementations of
   this disagree about the data folder's case (EA's own map editor writes DATA, OpenRA
   writes Data), and both are right on NTFS because NTFS does not care. On a case
   sensitive volume -- a copied folder, a Wine prefix, an external disk -- exactly one of
   them works, and which one is not knowable in advance. So never open a fixed spelling. */
static int rm_find_entry(const char* dir, const char* want, char* out, size_t n)
{
#if defined(_WIN32)
    /* Windows filesystems are case-insensitive; the plain name is the real name. */
    char p[RM_PATH_MAX];
    rm_join(p, sizeof p, dir, want);
    if (rm_is_dir(p) || rm_is_file(p)) { snprintf(out, n, "%s", want); return 1; }
    return 0;
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    int got = 0;
    if (!d) return 0;
    while (!got && (e = readdir(d)) != NULL) {
        const char *a = e->d_name, *b = want;
        for (; *a && *b; a++, b++) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) break;
        }
        if (!*a && !*b) { snprintf(out, n, "%s", e->d_name); got = 1; }
    }
    closedir(d);
    return got;
#endif
}

/* THE IDENTITY TEST, WRITTEN AGAINST A REAL INSTALL (3 Sep 2026).
   It used to look for TiberianDawn.dll and RedAlert.dll, because that is what the
   community fork of EA's own map editor tests. A real install has NEITHER, anywhere in
   the tree, so that test rejected the genuine article. Reality wins:

       <root>/Data/CONFIG.MEG         the archive holding the tileset XML
       <root>/Data/TEXTURES*.MEG      at least one, the archives holding the art

   That is a test of exactly what we go on to READ, which is the only property worth
   testing. WHICH texture archive carries the terrain is deliberately not named -- on
   a real install it is TEXTURES_TD_SRGB.MEG, but Data/MEGAFILES.XML lists archives that
   are not on disk at all, so the set is discovered by looking rather than assumed.
   Note also that steam_appid.txt DOES ship in the retail install and reads 1213210,
   against the research's expectation that it is developer-only; it is used as a
   corroborating signal below, never as a requirement. */
static int rm_has_meg(const char* dir)
{
#if defined(_WIN32)
    char pat[RM_PATH_MAX];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;
    snprintf(pat, sizeof pat, "%s\\*.MEG", dir);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) { found = 1; FindClose(h); }
    return found;
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    int found = 0;
    if (!d) return 0;
    while (!found && (e = readdir(d)) != NULL) {
        const size_t l = strlen(e->d_name);
        if (l > 4 && rm_icontains(e->d_name + l - 4, ".meg")) found = 1;
    }
    closedir(d);
    return found;
#endif
}

/* Does this directory hold a file whose name starts with `pre` and ends with `suf`?
   Case-insensitive, because the data folder's own case is not knowable in advance. */
static int rm_has_matching(const char* dir, const char* pre, const char* suf)
{
#if defined(_WIN32)
    char pat[RM_PATH_MAX];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;
    snprintf(pat, sizeof pat, "%s\\%s*%s", dir, pre, suf);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) { found = 1; FindClose(h); }
    return found;
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    const size_t lp = strlen(pre), ls = strlen(suf);
    int found = 0;
    if (!d) return 0;
    while (!found && (e = readdir(d)) != NULL) {
        const size_t l = strlen(e->d_name);
        if (l < lp + ls) continue;
        if (!rm_icontains(e->d_name, pre)) continue;
        if (strncasecmp(e->d_name, pre, lp) != 0) continue;
        if (strncasecmp(e->d_name + l - ls, suf, ls) != 0) continue;
        found = 1;
    }
    closedir(d);
    return found;
#endif
}

RM_MAYBE_UNUSED static int rm_looks_like_install(const char* dir)
{
    char real[256], data[RM_PATH_MAX], cfg[256];
    if (!rm_is_dir(dir)) return 0;
    if (!rm_find_entry(dir, "Data", real, sizeof real)) return 0;
    rm_join(data, sizeof data, dir, real);
    if (!rm_is_dir(data)) return 0;
    if (!rm_find_entry(data, "CONFIG.MEG", cfg, sizeof cfg)) return 0;
    /* And there has to be art to read. CONFIG.MEG alone is only the index. */
    return rm_has_matching(data, "TEXTURES", ".MEG");
}

/* The data directory of a confirmed install, spelled the way the disk spells it. The
   archive mounter will want this; nothing else here does. */
RM_MAYBE_UNUSED static int rm_data_dir(const char* root, char* out, size_t n)
{
    char real[256];
    if (!rm_find_entry(root, "DATA", real, sizeof real)) return 0;
    rm_join(out, n, root, real);
    return rm_is_dir(out);
}

/* Accept a candidate directory, or one of its immediate children. Returns 1 and fills
   `out` with whichever actually passed the probe. */
static int rm_accept(const char* cand, char* out, size_t n)
{
    if (rm_looks_like_install(cand)) { snprintf(out, n, "%s", cand); return 1; }
#if !defined(_WIN32)
    {
        DIR* d = opendir(cand);
        struct dirent* e;
        char sub[RM_PATH_MAX];
        int got = 0;
        if (!d) return 0;
        while (!got && (e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            rm_join(sub, sizeof sub, cand, e->d_name);
            if (rm_looks_like_install(sub)) { snprintf(out, n, "%s", sub); got = 1; }
        }
        closedir(d);
        return got;
    }
#else
    {
        char pat[RM_PATH_MAX], sub[RM_PATH_MAX];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        int got = 0;
        snprintf(pat, sizeof pat, "%s\\*", cand);
        h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (fd.cFileName[0] == '.') continue;
            rm_join(sub, sizeof sub, cand, fd.cFileName);
            if (rm_looks_like_install(sub)) { snprintf(out, n, "%s", sub); got = 1; }
        } while (!got && FindNextFileA(h, &fd));
        FindClose(h);
        return got;
    }
#endif
}

/* One Steam library: try the manifest by app id first, then fall back to reading the
   names under steamapps/common. Returns 1 and fills `out` with the game folder. */
static int rm_scan_library(const char* lib, char* out, size_t n)
{
    char manifest[RM_PATH_MAX], common[RM_PATH_MAX], installdir[512], cand[RM_PATH_MAX];

    rm_join(common, sizeof common, lib, "steamapps/common");
    if (!rm_is_dir(common)) {
        rm_join(common, sizeof common, lib, "SteamApps/common");   /* older casing */
        if (!rm_is_dir(common)) return 0;
    }

    /* ONE LEVEL OF NESTING IS EXPECTED. An install can be
       steamapps/common/Command & Conquer Remastered/CnCRemastered, i.e. the game sitting
       in a subfolder of the folder Steam names -- which is what a zip of the game
       extracts to. rm_accept tries the candidate itself and then each of its immediate
       children, so both layouts resolve and neither is hardcoded. */
    snprintf(manifest, sizeof manifest, "%s/steamapps/appmanifest_%s.acf",
             lib, RM_STEAM_APPID);
    if (rm_is_file(manifest)
        && rm_kv_lookup(manifest, "installdir", installdir, sizeof installdir)) {
        /* StateFlags bit 4 is "fully installed". A manifest survives the folder being
           deleted, and so does the registry key, so a resolver here is candidate
           GENERATION -- rm_looks_like_install below is what actually accepts it. */
        char flags[32];
        const int st = rm_kv_lookup(manifest, "StateFlags", flags, sizeof flags)
                     ? atoi(flags) : 4;
        if (st & 4) {
            rm_join(cand, sizeof cand, common, installdir);
            if (rm_accept(cand, out, n)) return 1;
        }
    }

#if !defined(_WIN32)
    {
        DIR* d = opendir(common);
        struct dirent* e;
        int got = 0;
        if (!d) return 0;
        while (!got && (e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            if (!(rm_icontains(e->d_name, "remaster")
                  || (rm_icontains(e->d_name, "command") && rm_icontains(e->d_name, "conquer"))
                  || rm_icontains(e->d_name, "cncremaster")))
                continue;
            rm_join(cand, sizeof cand, common, e->d_name);
            if (rm_accept(cand, out, n)) got = 1;
        }
        closedir(d);
        return got;
    }
#else
    {
        char pat[RM_PATH_MAX];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        int got = 0;
        snprintf(pat, sizeof pat, "%s\\*", common);
        h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (fd.cFileName[0] == '.') continue;
            if (!(rm_icontains(fd.cFileName, "remaster")
                  || (rm_icontains(fd.cFileName, "command") && rm_icontains(fd.cFileName, "conquer"))))
                continue;
            rm_join(cand, sizeof cand, common, fd.cFileName);
            if (rm_accept(cand, out, n)) got = 1;
        } while (!got && FindNextFileA(h, &fd));
        FindClose(h);
        return got;
    }
#endif
}

/* Every Steam library reachable from one Steam root: the root itself, plus each "path"
   in its libraryfolders.vdf that actually exists on THIS machine. */
static int rm_scan_steam_root(const char* root, char* out, size_t n)
{
    static const char* const vdfs[] = {
        "steamapps/libraryfolders.vdf", "config/libraryfolders.vdf",
        "SteamApps/libraryfolders.vdf", NULL
    };
    char vdf[RM_PATH_MAX], line[2048];
    int i;

    if (!rm_is_dir(root)) return 0;
    /* The root is a library in its own right, and on a cross-mounted install it is the
       ONLY one that resolves -- the recorded paths are the other machine's. */
    if (rm_scan_library(root, out, n)) return 1;

    for (i = 0; vdfs[i]; i++) {
        FILE* f;
        rm_join(vdf, sizeof vdf, root, vdfs[i]);
        f = fopen(vdf, "rb");
        if (!f) continue;
        while (fgets(line, sizeof line, f)) {
            char path[RM_PATH_MAX];
            const char* p = line;
            if (!rm_icontains(line, "\"path\"")) continue;
            /* Reuse the scalar reader on this one line by writing it to a temp parse:
               simpler to just find the second quoted token here. */
            while (*p && *p != '"') p++;            /* open of "path"  */
            if (*p) p++;
            while (*p && *p != '"') p++;            /* close of "path" */
            if (*p) p++;
            while (*p && *p != '"') p++;            /* open of value   */
            if (!*p) continue;
            p++;
            {
                size_t o = 0;
                while (*p && *p != '"' && o + 1 < sizeof path) {
                    if (*p == '\\' && p[1] == '\\') p++;
                    path[o++] = *p++;
                }
                path[o] = 0;
            }
            if (rm_is_dir(path) && rm_scan_library(path, out, n)) { fclose(f); return 1; }
        }
        fclose(f);
    }
    return 0;
}

/* THE ANSWER. 1 and `out` filled when this machine has an install we can read. */
RM_MAYBE_UNUSED static int rm_find_install(char* out, size_t n)
{
    const char* env = getenv("CNC3D_REMASTER_DIR");
    char p[RM_PATH_MAX];
    const char* home = getenv("HOME");
    int i;

    if (out && n) out[0] = 0;
    if (env && *env) {
        /* An explicit answer wins even if the probe dislikes it, so a player with an
           unusual layout is never locked out by our idea of what an install looks like.
           It is still reported, so a wrong one is visible rather than silent. */
        if (rm_accept(env, out, n)) return 1;
        snprintf(out, n, "%s", env);
        if (!rm_looks_like_install(env))
            fprintf(stderr, "remaster: CNC3D_REMASTER_DIR=%s has no .MEG archives under "
                            "it; taking it anyway because it was named explicitly\n", env);
        return 1;
    }

#if defined(_WIN32)
    /* THE REGISTRY FIRST, because it is the only thing that finds an EA App or Origin
       install. Petroglyph's own installer writes it, and it is what OpenRA reads. The
       EA App's leaf FOLDER name for this title is documented nowhere, so there is no
       path to fall back on if this key is missing -- which is why it is read rather
       than guessed. */
    {
        static const char* const keys[] = {
            "Software\\Petroglyph\\CnCRemastered",
            "SOFTWARE\\Wow6432Node\\Petroglyph\\CnCRemastered",
            NULL
        };
        for (i = 0; keys[i]; i++) {
            HKEY h;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keys[i], 0, KEY_READ, &h) != ERROR_SUCCESS)
                continue;
            {
                DWORD type = 0, cb = (DWORD)sizeof p;
                const LONG r = RegQueryValueExA(h, "Install Dir", NULL, &type,
                                                (LPBYTE)p, &cb);
                RegCloseKey(h);
                if (r == ERROR_SUCCESS && type == REG_SZ && cb) {
                    p[cb < sizeof p ? cb : sizeof p - 1] = 0;
                    if (rm_accept(p, out, n)) return 1;
                }
            }
        }
    }
    {
        static const char* const roots[] = {
            "C:\\Program Files (x86)\\Steam",
            "C:\\Program Files\\Steam",
            NULL
        };
        for (i = 0; roots[i]; i++)
            if (rm_scan_steam_root(roots[i], out, n)) return 1;
    }
#else
    if (home) {
        static const char* const rel[] = {
            "Library/Application Support/Steam",     /* macOS  */
            ".steam/steam", ".local/share/Steam",    /* Linux  */
            NULL
        };
        for (i = 0; rel[i]; i++) {
            rm_join(p, sizeof p, home, rel[i]);
            if (rm_scan_steam_root(p, out, n)) return 1;
        }
    }
    /* EVERY MOUNTED VOLUME, which is how a Boot Camp or external-drive library is found.
       Cheap: one opendir of /Volumes and a stat per entry.

       THIS IS THE ONLY PATH THAT CAN EVER WORK ON A MAC. The Remastered Collection has
       no native macOS build at all -- Steam reports the title as Windows-only -- so a
       Mac never has one in its own Steam library. What it can have is the Windows install
       on a Boot Camp partition or an external disk, or inside a CrossOver/Whisky bottle,
       and all three of those are a mounted volume with a Steam folder on it. */
    {
        DIR* d = opendir("/Volumes");
        struct dirent* e;
        int got = 0;
        if (d) {
            while (!got && (e = readdir(d)) != NULL) {
                if (e->d_name[0] == '.') continue;
                snprintf(p, sizeof p, "/Volumes/%s/Steam", e->d_name);
                if (rm_scan_steam_root(p, out, n)) { got = 1; break; }
                snprintf(p, sizeof p, "/Volumes/%s/SteamLibrary", e->d_name);
                if (rm_scan_steam_root(p, out, n)) { got = 1; break; }
            }
            closedir(d);
        }
        if (got) return 1;
    }
#endif
    return 0;
}

#endif /* CNC3D_REMASTER_H */
