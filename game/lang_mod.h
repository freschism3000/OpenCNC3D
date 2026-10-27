/* lang_mod.h -- THE LANGUAGE PACKS: a language other than English, chosen in Settings,
 * downloaded when it is chosen, and used wherever the game speaks or writes.
 *
 * WHAT A PACK IS. A folder of the language's own 1995 or 1996 files, in the install next to
 * the English ones (Tiberian Dawn's under dosdata/lang/<code>/), every file taken straight
 * out of the language's retail disc images by HTTP byte range and checked against the
 * SHA-256 pinned in game/lang_manifest.h (made by tools/media/make-language-manifest.py,
 * which also says why each file is in it). Nothing is decoded or rebuilt on the player's
 * machine: the bytes that land are the disc's bytes.
 *
 * WHAT IT CHANGES, and only while its language is the one chosen and every one of its
 * files is on the disk:
 *   - the announcer and the unit voices: the pack's SPEECH.MIX and SOUNDS.MIX are searched
 *     before the English ones, their death sounds never used (bank_add_language);
 *   - the briefings: the pack's MISSION.INI, ahead of a mission file's own English block
 *     (brief_mod.h);
 *   - the movies: a movie the pack carries plays from it; the ones its release cut or
 *     edited are not in it, and play in English;
 *   - the names of what can be built, from the pack's string table (lang_txt).
 * The sprites are never touched: the German release's infantry are censored cyborgs and
 * the 1995 English soldiers stay.
 *
 * FILE PRESENCE IS THE SWITCH, as it is for the movies and the score: a pack with any file
 * missing is not used at all, so a half-finished download cannot mix two languages.
 *
 * THE DOWNLOAD runs on a worker thread in the game application only (the headless
 * renderer has no HTTP client); the Settings screen reads its progress (lang_progress).
 * Each file lands under a temporary name, is checked whole, and only then is renamed into
 * place; an interrupted file resumes where it stopped.
 */
#ifndef LANG_MOD_H
#define LANG_MOD_H

#include "lang_manifest.h"
#include <string>
#include <vector>
#include <map>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* head: bytes written ahead of the disc's range (a Red Alert archive's index rewritten
   plain, the way the Red Alert score is fetched); empty for every other file */
struct LangFile { std::string code, game, rel, disc, sha, head; long long size, off; };
struct LangPack { std::string code, game, name; long long bytes; int files; };
static std::vector<LangFile> g_langFiles;
static std::vector<LangPack> g_langPacks;
static std::map<std::string, std::vector<std::pair<std::string, long long> > > g_langMirrors;
static bool g_langParsed = false;
/* THE INSTALL'S ROOT, the folder the manifest's paths are relative to: the parent of the
   folder the sound engine reads dosdata from (lang_set_root), "." until told. */
static char g_langRoot[512] = ".";
static void lang_set_root(const char* dosdata)
{
    if (!dosdata || !*dosdata) return;
    std::string d = dosdata;
    while (d.size() > 1 && (d[d.size() - 1] == '/' || d[d.size() - 1] == '\\')) d.erase(d.size() - 1);
    /* (Red Alert's sound folder is ra3d/audio: its root is two folders up) */
    for (int up = 0; up < 2; up++) {
        const size_t cut = d.find_last_of("/\\");
        const std::string leaf = cut == std::string::npos ? d : d.substr(cut + 1);
        d = cut == std::string::npos ? "." : d.substr(0, cut);
        if (leaf != "audio") break;
    }
    snprintf(g_langRoot, sizeof g_langRoot, "%s", d.c_str());
}
static std::string lang_full(const std::string& rel) { return std::string(g_langRoot) + "/" + rel; }
/* the language chosen ("en" for English) and the game this process is (td or ra) */
static char g_langCode[8] = "en";
static const char* lang_game(void);

static void lang_parse(void)
{
    if (g_langParsed) return;
    g_langParsed = true;
    const char* p = LANG_MANIFEST;
    while (*p) {
        const char* e = strchr(p, '\n');
        std::string line(p, e ? (size_t)(e - p) : strlen(p));
        p = e ? e + 1 : p + strlen(p);
        if (line.empty() || line[0] == '#') continue;
        char a[64], b[64], c[600], d[600];
        long long n1 = 0, n2 = 0;
        char sha[80];
        if (sscanf(line.c_str(), "mirror %63s %599s %lld", a, c, &n1) == 3) {
            g_langMirrors[a].push_back(std::make_pair(std::string(c), n1));
        } else if (!strncmp(line.c_str(), "pack ", 5) && sscanf(line.c_str(), "pack %63s %63s", a, b) == 2) {
            const char* name = line.c_str() + 5;
            for (int k = 0; k < 2; k++) { name = strchr(name, ' '); if (name) name++; else break; }
            LangPack pk; pk.code = a; pk.game = b; pk.name = name ? name : a; pk.bytes = 0; pk.files = 0;
            g_langPacks.push_back(pk);
        } else if (sscanf(line.c_str(), "file %63s %63s %599s %lld %79s %599s %lld",
                          a, b, c, &n1, sha, d, &n2) == 7) {
            LangFile f; f.code = a; f.game = b; f.rel = c; f.size = n1; f.sha = sha; f.disc = d; f.off = n2;
            /* the eighth field, when there is one, is the head in hex */
            size_t at = 0;
            for (int k = 0; k < 8 && at != std::string::npos; k++) at = line.find(' ', at + 1);
            if (at != std::string::npos)
                for (size_t q = at + 1; q + 1 < line.size(); q += 2)
                    f.head += (char)strtol(line.substr(q, 2).c_str(), NULL, 16);
            g_langFiles.push_back(f);
            for (size_t k = 0; k < g_langPacks.size(); k++)
                if (g_langPacks[k].code == f.code && g_langPacks[k].game == f.game) {
                    g_langPacks[k].bytes += f.size; g_langPacks[k].files++;
                }
        }
    }
}

static long long lang_file_size(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? (long long)st.st_size : -1;
}

/* Whether every file of a pack is on the disk at its size (the hash was checked as it
   landed). */
static bool lang_installed(const char* code, const char* game)
{
    lang_parse();
    int n = 0;
    for (size_t k = 0; k < g_langFiles.size(); k++) {
        const LangFile& f = g_langFiles[k];
        if (f.code != code || f.game != game) continue;
        if (lang_file_size(lang_full(f.rel)) != f.size) return false;
        n++;
    }
    return n > 0;
}

/* The pack's folder for this game while its language is chosen and complete, else NULL.
   Tiberian Dawn's packs live in dosdata/lang/<code>. */
static const char* lang_dir(void)
{
    static char dir[600];
    if (!strcmp(g_langCode, "en") || !lang_installed(g_langCode, lang_game())) return NULL;
    snprintf(dir, sizeof dir, "%s/%s/lang/%s", g_langRoot,
             !strcmp(lang_game(), "ra") ? "ra3d" : "dosdata", g_langCode);
    return dir;
}

/* A file of the chosen pack by its name in the pack's folder ("MISSION.INI",
   "movies/GDI1.VQA"), into `out`; false when there is no such file to use. */
static bool lang_path(const char* name, char* out, size_t outn)
{
    const char* d = lang_dir();
    if (!d) return false;
    snprintf(out, outn, "%s/%s", d, name);
    return lang_file_size(out) > 0;
}

/* THE STRING TABLE. CONQUER.ENG on every language's disc: a u16 offset per string, from
   the start of the file, then the strings, each NUL-terminated (Text_String reads it the
   same way). Loaded on first use and again when the language changes. */
static std::vector<char> g_langTxtBuf;
static std::string g_langTxtFor;
static const char* lang_txt(int index)
{
    char path[700];
    if (!lang_path("CONQUER.ENG", path, sizeof path)) return NULL;
    if (g_langTxtFor != path) {
        g_langTxtFor = path;
        g_langTxtBuf.clear();
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            const long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (n > 2 && n < (1 << 20)) {
                g_langTxtBuf.resize((size_t)n + 1, 0);
                if (fread(&g_langTxtBuf[0], 1, (size_t)n, f) != (size_t)n) g_langTxtBuf.clear();
            }
            fclose(f);
        }
    }
    const size_t n = g_langTxtBuf.size();
    if (n < 3 || index < 0) return NULL;
    const unsigned char* b = (const unsigned char*)&g_langTxtBuf[0];
    const unsigned first = b[0] | (b[1] << 8);
    if ((size_t)(index * 2 + 1) >= first || first >= n) return NULL;
    const unsigned off = b[index * 2] | (b[index * 2 + 1] << 8);
    if (off >= n - 1) return NULL;
    return (const char*)&b[off];
}

/* ---- the download ---------------------------------------------------------------- */
enum { LANG_IDLE = 0, LANG_RUNNING, LANG_DONE, LANG_FAILED, LANG_CANCELLED };
static SDL_mutex* g_langLock = NULL;
static volatile int g_langState = LANG_IDLE;
static volatile int g_langCancel = 0;
static long long g_langDone = 0, g_langTotal = 0;
static char g_langMsg[256] = "";
static std::string g_langJob;          /* the code being fetched */

static void lang_say(const char* m)
{
    if (g_langLock) SDL_LockMutex(g_langLock);
    snprintf(g_langMsg, sizeof g_langMsg, "%s", m);
    if (g_langLock) SDL_UnlockMutex(g_langLock);
}

/* SHA-256, for checking what landed (FIPS 180-4, the one routine this needs). */
struct LangSha { unsigned int h[8]; unsigned char buf[64]; unsigned long long len; size_t n; };
static void lang_sha_block(LangSha& s, const unsigned char* p)
{
    static const unsigned int K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define LANG_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
    unsigned int w[64], a[8];
    for (int i = 0; i < 16; i++)
        w[i] = ((unsigned)p[i * 4] << 24) | ((unsigned)p[i * 4 + 1] << 16) | ((unsigned)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        const unsigned s0 = LANG_ROR(w[i - 15], 7) ^ LANG_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const unsigned s1 = LANG_ROR(w[i - 2], 17) ^ LANG_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (int i = 0; i < 8; i++) a[i] = s.h[i];
    for (int i = 0; i < 64; i++) {
        const unsigned S1 = LANG_ROR(a[4], 6) ^ LANG_ROR(a[4], 11) ^ LANG_ROR(a[4], 25);
        const unsigned ch = (a[4] & a[5]) ^ (~a[4] & a[6]);
        const unsigned t1 = a[7] + S1 + ch + K[i] + w[i];
        const unsigned S0 = LANG_ROR(a[0], 2) ^ LANG_ROR(a[0], 13) ^ LANG_ROR(a[0], 22);
        const unsigned mj = (a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]);
        const unsigned t2 = S0 + mj;
        a[7] = a[6]; a[6] = a[5]; a[5] = a[4]; a[4] = a[3] + t1;
        a[3] = a[2]; a[2] = a[1]; a[1] = a[0]; a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) s.h[i] += a[i];
#undef LANG_ROR
}
static bool lang_sha_file(const std::string& path, char* hex65)
{
    LangSha s;
    static const unsigned int H0[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(s.h, H0, sizeof H0); s.len = 0; s.n = 0;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> chunk(1 << 16);
    size_t got;
    while ((got = fread(&chunk[0], 1, chunk.size(), f)) > 0) {
        s.len += got;
        for (size_t i = 0; i < got; i++) {
            s.buf[s.n++] = chunk[i];
            if (s.n == 64) { lang_sha_block(s, s.buf); s.n = 0; }
        }
    }
    fclose(f);
    const unsigned long long bits = s.len * 8ULL;
    s.buf[s.n++] = 0x80;
    if (s.n > 56) { while (s.n < 64) s.buf[s.n++] = 0; lang_sha_block(s, s.buf); s.n = 0; }
    while (s.n < 56) s.buf[s.n++] = 0;
    for (int i = 7; i >= 0; i--) s.buf[s.n++] = (unsigned char)(bits >> (i * 8));
    lang_sha_block(s, s.buf);
    for (int i = 0; i < 8; i++) snprintf(hex65 + i * 8, 9, "%08x", s.h[i]);
    return true;
}

static void lang_mkdirs(const std::string& path)
{
    for (size_t k = 1; k < path.size(); k++)
        if (path[k] == '/') {
            const std::string d = path.substr(0, k);
#ifdef _WIN32
            _mkdir(d.c_str());
#else
            mkdir(d.c_str(), 0755);
#endif
        }
}

/* Bytes still to fetch for a pack (the files not yet on the disk at their size). */
static long long lang_pack_missing(const std::string& code, const std::string& game)
{
    lang_parse();
    long long n = 0;
    for (size_t k = 0; k < g_langFiles.size(); k++) {
        const LangFile& f = g_langFiles[k];
        if (f.code == code && f.game == game && lang_file_size(lang_full(f.rel)) != f.size) n += f.size;
    }
    return n;
}

#ifdef OPENCNC3D_NO_MAIN
extern "C" {
#include "../launcher/lnet.h"
}
static int lang_tick(void* user, long long have, long long total)
{
    (void)total;
    const long long base = *(const long long*)user;
    if (g_langLock) SDL_LockMutex(g_langLock);
    g_langDone = base + have;
    if (g_langLock) SDL_UnlockMutex(g_langLock);
    return g_langCancel ? 0 : 1;
}
static int lang_worker(void*)
{
    const std::string code = g_langJob, game = lang_game();
    long long base = 0;
    for (size_t k = 0; k < g_langFiles.size() && !g_langCancel; k++) {
        const LangFile& f = g_langFiles[k];
        const std::string full = lang_full(f.rel);
        if (f.code != code || f.game != game || lang_file_size(full) == f.size) continue;
        lang_mkdirs(full);
        const std::string part = full + ".ocpart";
        bool ok = false;
        char err[256] = "";
        const std::vector<std::pair<std::string, long long> >& ms = g_langMirrors[f.disc];
        const long long body = f.size - (long long)f.head.size();
        for (size_t m = 0; m < ms.size() && !ok && !g_langCancel; m++) {
            long status = 0;
            const int r = ln_get_range(ms[m].first.c_str(), NULL, part.c_str(), ms[m].second + f.off,
                                       body, 30, &g_langCancel, lang_tick, &base, &status,
                                       err, (int)sizeof err);
            if (r == LN_RANGE_CANCEL) break;
            if (r != LN_RANGE_OK) continue;
            if (!f.head.empty()) {
                /* the head, then the range: one new file, which is what is checked */
                const std::string whole = full + ".ochead";
                FILE* o = fopen(whole.c_str(), "wb");
                FILE* in = fopen(part.c_str(), "rb");
                bool w = o && in && fwrite(f.head.data(), 1, f.head.size(), o) == f.head.size();
                std::vector<char> chunk(1 << 16);
                size_t got;
                while (w && in && (got = fread(&chunk[0], 1, chunk.size(), in)) > 0)
                    w = fwrite(&chunk[0], 1, got, o) == got;
                if (o) fclose(o);
                if (in) fclose(in);
                remove(part.c_str());
                if (!w || rename(whole.c_str(), part.c_str()) != 0) {
                    remove(whole.c_str());
                    snprintf(err, sizeof err, "could not write %s", f.rel.c_str());
                    continue;
                }
            }
            char hex[65];
            if (!lang_sha_file(part, hex) || f.sha != hex) {
                remove(part.c_str());
                snprintf(err, sizeof err, "%s did not match its checksum", f.rel.c_str());
                continue;
            }
            remove(full.c_str());
            ok = rename(part.c_str(), full.c_str()) == 0;
            if (!ok) snprintf(err, sizeof err, "could not write %s", f.rel.c_str());
        }
        if (g_langCancel) break;
        if (!ok) {
            char m[300];
            snprintf(m, sizeof m, "The download stopped: %s", err[0] ? err : "no source answered");
            lang_say(m);
            g_langState = LANG_FAILED;
            return 0;
        }
        base += f.size;
    }
    if (g_langCancel) { lang_say("Cancelled. What arrived is kept, and the next try resumes it."); g_langState = LANG_CANCELLED; return 0; }
    lang_say("Ready.");
    g_langState = LANG_DONE;
    return 0;
}
#endif

/* Start fetching a pack. False (and a message) where this program cannot download. */
static bool lang_download(const char* code)
{
    lang_parse();
    if (g_langState == LANG_RUNNING) return false;
#ifdef OPENCNC3D_NO_MAIN
    if (!g_langLock) g_langLock = SDL_CreateMutex();
    g_langJob = code;
    g_langCancel = 0;
    g_langDone = 0;
    g_langTotal = lang_pack_missing(code, lang_game());
    lang_say("Downloading...");
    g_langState = LANG_RUNNING;
    SDL_Thread* t = SDL_CreateThread(lang_worker, "langpack", NULL);
    if (!t) { g_langState = LANG_FAILED; lang_say("The download could not start."); return false; }
    SDL_DetachThread(t);
    return true;
#else
    (void)code;
    g_langState = LANG_FAILED;
    lang_say("Language packs are downloaded by the game itself, not this tool.");
    return false;
#endif
}

static int lang_progress(long long* done, long long* total, char* msg, int msglen)
{
    if (g_langLock) SDL_LockMutex(g_langLock);
    if (done) *done = g_langDone;
    if (total) *total = g_langTotal;
    if (msg && msglen > 0) snprintf(msg, (size_t)msglen, "%s", g_langMsg);
    if (g_langLock) SDL_UnlockMutex(g_langLock);
    return g_langState;
}

#endif /* LANG_MOD_H */
