/* dossave.c -- see dossave.h. The index file and the path arithmetic, nothing else. */

#include "dossave.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#define DS_MAGIC 0x53443343u /* 'C3DS' little-endian */
#define DS_VERSION 1u

static char g_dsDir[512] = "";
static char g_dsPath[600];

/* Is this path already absolute? The engine's own test, common/paths_posix.cpp:238-241
 * and paths_win.cpp:140-147: a leading separator on POSIX, a drive letter or a UNC prefix
 * on Windows. */
static int ds_is_absolute(const char *p)
{
    if (!p || !*p) return 0;
    if (p[0] == '/' || p[0] == '\\') return 1;
    if (p[0] && p[1] == ':') return 1;
    return 0;
}

void ds_set_dir(const char *dir)
{
    if (!dir || !*dir) {
        g_dsDir[0] = 0;
        return;
    }
    /* ABSOLUTE OR NOTHING. CDFileClass::Open short-circuits on an absolute path for both
       read and write (cdfile.cpp:485-517) and opens exactly that; a RELATIVE path is
       redirected into the engine's own user directory instead, so the payload would be
       written somewhere we never look and the load would find nothing. A caller passing
       a relative --savedir is not doing anything wrong, so resolve it here rather than
       refusing it. */
    if (!ds_is_absolute(dir)) {
        char cwd[400];
        if (getcwd(cwd, sizeof cwd)) {
            snprintf(g_dsDir, sizeof g_dsDir, "%s/%s", cwd, dir);
        } else {
            g_dsDir[0] = 0;
            return;
        }
    } else {
        snprintf(g_dsDir, sizeof g_dsDir, "%s", dir);
    }
    /* One trailing separator, whatever the caller handed over. SDL_GetPrefPath already
     * appends one and a --savedir from a shell usually does not. */
    {
        size_t n = strlen(g_dsDir);
        if (n && g_dsDir[n - 1] != '/' && g_dsDir[n - 1] != '\\' && n + 2 < sizeof g_dsDir) {
            g_dsDir[n] = '/';
            g_dsDir[n + 1] = 0;
        }
    }
}

const char *ds_get_dir(void) { return g_dsDir; }

const char *ds_payload_path(int slot)
{
    if (!g_dsDir[0]) return "";
    /* 1995's own name (saveload.cpp:110), 8.3-safe so Win98 takes it unchanged. */
    snprintf(g_dsPath, sizeof g_dsPath, "%sSAVEGAME.%03d", g_dsDir, slot);
    return g_dsPath;
}

static const char *ds_index_path(void)
{
    static char p[600];
    if (!g_dsDir[0]) return "";
    snprintf(p, sizeof p, "%sCNC3DSAV.IDX", g_dsDir);
    return p;
}

int ds_payload_exists(int slot)
{
    FILE *f;
    const char *p = ds_payload_path(slot);
    if (!p || !*p) return 0;
    f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* ---- the fixed record ---------------------------------------------------------- *
 * Little-endian by hand rather than by struct layout, because this file is read by a
 * different compiler on Win98 than the one that wrote it on a Mac. HALLFAME.DAT is
 * written the same way and for the same reason.
 * -------------------------------------------------------------------------------- */

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16)
           | ((unsigned int)p[3] << 24);
}

static void wr32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void ds_pack(const DS_Slot *s, unsigned char *r)
{
    memset(r, 0, DS_REC);
    if (!s->used) return;
    wr32(r + 0, DS_MAGIC);
    wr32(r + 4, DS_VERSION);
    wr32(r + 8, (unsigned int)s->slot);
    wr32(r + 12, s->bytes);
    memcpy(r + 16, s->descr, DS_DESCR);
    memcpy(r + 60, s->scen, sizeof s->scen < 12 ? sizeof s->scen : 12);
    memcpy(r + 72, s->pack, sizeof s->pack < 24 ? sizeof s->pack : 24);
    r[96] = s->camp_active;
    r[97] = s->camp_side;
    r[98] = s->camp_scenario;
    r[99] = s->camp_dir;
    r[100] = s->camp_var;
    r[101] = s->build;
    r[102] = s->cam_mode;
    wr32(r + 104, (unsigned int)s->camx_m);
    wr32(r + 108, (unsigned int)s->camz_m);
    wr32(r + 112, (unsigned int)s->dist_m);
    wr32(r + 116, (unsigned int)s->zoom_m);
    wr32(r + 120, (unsigned int)s->music);
    wr32(r + 124, (unsigned int)s->frame);
}

static int ds_unpack(const unsigned char *r, DS_Slot *s)
{
    memset(s, 0, sizeof *s);
    if (rd32(r + 0) != DS_MAGIC || rd32(r + 4) != DS_VERSION) return 0;
    s->used = 1;
    s->slot = (int)rd32(r + 8);
    s->bytes = rd32(r + 12);
    memcpy(s->descr, r + 16, DS_DESCR);
    s->descr[DS_DESCR] = 0;
    memcpy(s->scen, r + 60, 11);
    s->scen[11] = 0;
    memcpy(s->pack, r + 72, 23);
    s->pack[23] = 0;
    s->camp_active = r[96];
    s->camp_side = r[97];
    s->camp_scenario = r[98];
    s->camp_dir = r[99];
    s->camp_var = r[100];
    s->build = r[101];
    s->cam_mode = r[102];
    s->camx_m = (int)rd32(r + 104);
    s->camz_m = (int)rd32(r + 108);
    s->dist_m = (int)rd32(r + 112);
    s->zoom_m = (int)rd32(r + 116);
    s->music = (int)rd32(r + 120);
    s->frame = (int)rd32(r + 124);
    return 1;
}

int ds_read_index(DS_Slot out[DS_SLOTS])
{
    unsigned char rec[DS_REC];
    FILE *f;
    int i, n = 0;

    for (i = 0; i < DS_SLOTS; i++) memset(&out[i], 0, sizeof out[i]);
    if (!g_dsDir[0]) return -1;
    f = fopen(ds_index_path(), "rb");
    if (!f) return 0;                    /* no index yet is not an error */
    for (i = 0; i < DS_SLOTS; i++) {
        if (fread(rec, 1, DS_REC, f) != DS_REC) break;
        if (ds_unpack(rec, &out[i])) {
            /* A record whose payload has been deleted from under us is NOT a slot. */
            if (ds_payload_exists(out[i].slot)) n++;
            else out[i].used = 0;
        }
    }
    fclose(f);
    return n;
}

int ds_write_index(const DS_Slot in[DS_SLOTS])
{
    unsigned char rec[DS_REC];
    FILE *f;
    int i;

    if (!g_dsDir[0]) return 0;
    f = fopen(ds_index_path(), "wb");
    if (!f) return 0;
    for (i = 0; i < DS_SLOTS; i++) {
        ds_pack(&in[i], rec);
        if (fwrite(rec, 1, DS_REC, f) != DS_REC) { fclose(f); return 0; }
    }
    fclose(f);
    return 1;
}

int ds_first_free(const DS_Slot t[DS_SLOTS])
{
    int i;
    for (i = 0; i < DS_SLOTS; i++)
        if (!t[i].used) return i;
    return -1;
}
