/* w98_brain.c -- see w98_brain.h. Loads the Tiberian Dawn brain and reads the world. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "w98_brain.h"

/* ---- the slice of the CNC_* ABI tier1 needs -------------------------------------- */

typedef void (*CNC_Event_Callback_Type)(const void *event);
typedef void (__cdecl *CNC_Init_t)(const char *, CNC_Event_Callback_Type);
typedef void (__cdecl *CNC_Config_t)(const void *rules);
typedef int  (__cdecl *CNC_Start_t)(const char *, const char *, const char *, int, int);
typedef int  (__cdecl *CNC_Advance_t)(unsigned __int64);
typedef int  (__cdecl *CNC_GetState_t)(int, unsigned __int64, unsigned char *, unsigned int);
typedef int  (__cdecl *CNC3D_Dump_t)(void);
typedef void (__cdecl *CNC_Input_t)(int, unsigned char, unsigned __int64, int, int, int, int);
typedef int  (__cdecl *CNC_Select_t)(unsigned __int64, int, int);
typedef void (__cdecl *CNC_ClearSel_t)(unsigned __int64);
typedef int  (__cdecl *CNC3D_C2P_t)(int, int, int *, int *);
/* CNC3D_Probe_Object_At: the engine's ActionType for those input pixels given the current
 * selection. A pure query; -1 means it refused the pixel, 0 is ACTION_NONE. */
typedef int  (__cdecl *CNC3D_Probe_t)(int, int);
typedef void (__cdecl *CNC_Sidebar_t)(int, unsigned __int64, int, int, short, short);

static HMODULE        g_dll;
static CNC_Init_t     g_init;
static CNC_Config_t   g_config;
static CNC_Start_t    g_start;
static CNC_Advance_t  g_advance;
static CNC_GetState_t g_getstate;
static CNC3D_Dump_t   g_dump;
static CNC_Input_t    g_input;
static CNC_Select_t   g_select;
static CNC_ClearSel_t g_clearsel;
static CNC3D_C2P_t    g_cell2px;
static CNC3D_Probe_t  g_probe;
static CNC_Sidebar_t  g_sidebarreq;

/* SidebarRequestEnum, dllinterface.h:366 */
#define SB_START_CONSTRUCTION  0
#define SB_HOLD_CONSTRUCTION   1
#define SB_CANCEL_CONSTRUCTION 2
#define SB_START_PLACEMENT     3
#define SB_PLACE               4
#define SB_CANCEL_PLACE        5
#define SB_CLICK_REPAIR        6

/* InputRequestEnum, dllinterface.h:399. Only the two tier1 uses are named. */
#define INPUT_REQUEST_COMMAND_AT_POSITION 9
#define INPUT_REQUEST_SPECIAL_KEYS       10

#define GAME_STATE_STATIC_MAP 1
#define GAME_STATE_SIDEBAR    4
#define GAME_STATE_PLACEMENT  5
#define GAME_STATE_SHROUD     6

/* Measured against the real header, not read off the source. See w98_brain.h. */
#define SB_HDR        59
#define SB_ENTRY     130
#define SB_OFF_NAME    0
#define SB_OFF_BTYPE  16
#define SB_OFF_BID    20
#define SB_OFF_COST   32
#define SB_OFF_PROG   44
/* PlacementList is 36 shorts at 48, its length is the int at 120. Pinned by the record
 * size: 48 + 2*36 + 4 + 6 bools = 130, which is what SB_ENTRY already says. */
#define SB_OFF_PLIST  48
#define SB_OFF_PLEN  120
#define SB_OFF_DONE  124
#define SB_OFF_CONS  125
#define SB_OFF_HOLD  126
#define SB_OFF_BUSY  127

static void wb_ensure_stdout(void);   /* defined with the capture code below */
static void wb_truncate_capture(void);

/* CNCDifficultyDataStruct, byte for byte, from tiberiandawn/dllinterface.h:796.
 *
 * ================== THE WHOLE CNC_* ABI IS #pragma pack(1) ==================
 * dllinterface.h:46 pushes pack(1) over the entire interface and pops it at :915, so
 * this record is THIRTY-NINE bytes: nine floats and three C++ bools, with no tail
 * padding whatever. Natural alignment would make it 40, and 40 is wrong.
 *
 * This cost most of a day, so it is worth stating exactly how it failed. The rules are
 * an ARRAY OF THREE difficulties. Get the size wrong by one byte and entry [0] is still
 * perfect, so everything that reads it looks right; entries [1] and [2] land one and two
 * bytes late and read floats that were never meant to be there. Single player uses the
 * MIDDLE one. So the engine started, fired, took damage, spawned reinforcements, ran its
 * triggers, animated walk cycles, and NOTHING EVER MOVED, because the difficulty it
 * actually uses read its GroundspeedBias out of the middle of another float.
 *
 * An earlier pass "fixed" this by changing the three flags from int to unsigned char,
 * which took the record from 48 bytes to 40. That was a real bug and a real improvement
 * and it changed nothing at all, because the answer was 39.
 *
 * The lesson generalises to every struct in this ABI: mirror it PACKED, or do not mirror
 * it. game/cnc_eyes.cpp and brain/host/run1.cpp never had this problem because they use
 * the real C++ header and inherit its pragma.
 * ========================================================================== */
typedef struct
{
    float FirepowerBias, GroundspeedBias, AirspeedBias, ArmorBias, ROFBias, CostBias;
    float BuildSpeedBias;
    float RepairDelay, BuildDelay;
    unsigned char IsBuildSlowdown, IsWallDestroyer, IsContentScan;
} __attribute__((packed)) WB_Difficulty;
typedef struct { WB_Difficulty Difficulties[3]; } WB_Rules;

/* Asserted, not trusted. A C89 compile-time check, since _Static_assert is C11 and this
 * file is built as gnu89. */
typedef char wb_rules_size_check[(sizeof(WB_Difficulty) == 39) ? 1 : -1];
typedef char wb_rules_total_check[(sizeof(WB_Rules) == 117) ? 1 : -1];

/* The static map struct's leading fields, which is all tier1 reads. The full struct is
 * about 590 KB (it carries every cell), so it is heap allocated rather than on the stack:
 * a 590 KB automatic would blow the default 1 MB stack on Windows. */
typedef struct
{
    int  MapCellX, MapCellY, MapCellWidth, MapCellHeight;
    int  OriginalMapCellX, OriginalMapCellY, OriginalMapCellWidth, OriginalMapCellHeight;
    int  Theater;
    char ScenarioName[512];   /* _MAX_FNAME + _MAX_EXT on mingw is 256 + 256 */
} WB_MapHead;


/* ---- the engine's event struct, mirrored byte for byte ------------------------------
 *
 * dllinterface.h wraps the whole ABI in #pragma pack(1), and getting that wrong is not a
 * theoretical risk on this branch: a 39-byte difficulty struct that C would have made 40
 * is what stopped every unit in the game from moving, and it took a day to find because
 * the numbers that came out were plausible. So every offset this file depends on is
 * asserted at compile time against a packed mirror, not trusted.
 *
 * EventType is an enum (4 bytes) and GlyphXPlayerID is __int64, so the union starts at
 * offset 12 with no padding in front of it.
 * ----------------------------------------------------------------------------------- */
typedef struct
{
    int      EventType;
    __int64  GlyphXPlayerID;
} __attribute__((packed)) WB_EvHead;
typedef char wb_evhead_check[(sizeof(WB_EvHead) == 12) ? 1 : -1];

typedef struct
{
    int  SFXIndex, Variation, PixelX, PixelY, PlayerID;
    char SoundEffectName[16];
    int  SoundEffectPriority, SoundEffectContext;
} __attribute__((packed)) WB_EvSfx;
typedef char wb_evsfx_check[(sizeof(WB_EvSfx) == 44) ? 1 : -1];

typedef struct
{
    int  SpeechIndex, PlayerID;
    char SpeechName[16];
} __attribute__((packed)) WB_EvSpeech;
typedef char wb_evspeech_check[(sizeof(WB_EvSpeech) == 24) ? 1 : -1];

/* Only the three leading bools of GameOverEvent are read; everything past them is movie
 * names and score fields this build has no screen for yet. */
typedef struct
{
    unsigned char Multiplayer, IsHuman, PlayerWins;
} __attribute__((packed)) WB_EvOver;

#define WB_CB_SOUND_EFFECT 0
#define WB_CB_SPEECH       1
#define WB_CB_GAME_OVER    2

static void (*g_hook)(const W98_Event *);
static long g_nsfx, g_nspeech, g_nover;

void wb_set_event_hook(void (*fn)(const W98_Event *)) { g_hook = fn; }
void wb_event_counts(long *sfx, long *speech, long *over)
{
    if (sfx) *sfx = g_nsfx;
    if (speech) *speech = g_nspeech;
    if (over) *over = g_nover;
}

static void wb_event(const void *event)
{
    const unsigned char *p = (const unsigned char *)event;
    const WB_EvHead *h = (const WB_EvHead *)p;
    W98_Event e;
    if (!event) return;
    memset(&e, 0, sizeof e);
    switch (h->EventType)
    {
    case WB_CB_SOUND_EFFECT:
    {
        const WB_EvSfx *s = (const WB_EvSfx *)(p + 12);
        ++g_nsfx;
        e.type = WB_EV_SFX;
        e.index = s->SFXIndex;
        e.variation = s->Variation;
        e.px = s->PixelX;
        e.py = s->PixelY;
        memcpy(e.name, s->SoundEffectName, 16);
        e.name[16] = 0;
        break;
    }
    case WB_CB_SPEECH:
    {
        const WB_EvSpeech *s = (const WB_EvSpeech *)(p + 12);
        ++g_nspeech;
        e.type = WB_EV_SPEECH;
        e.index = s->SpeechIndex;
        memcpy(e.name, s->SpeechName, 16);
        e.name[16] = 0;
        break;
    }
    case WB_CB_GAME_OVER:
    {
        const WB_EvOver *s = (const WB_EvOver *)(p + 12);
        ++g_nover;
        e.type = WB_EV_GAMEOVER;
        e.win = s->PlayerWins ? 1 : 0;
        break;
    }
    default: return;      /* movies, messages, map cells: nothing here consumes them yet */
    }
    if (g_hook) g_hook(&e);
}

int wb_open(const char *dllpath, char *err, int errlen)
{
    char path[MAX_PATH];

    if (!dllpath)
    {
        char *slash;
        GetModuleFileNameA(NULL, path, sizeof path - 24);
        slash = strrchr(path, '\\');
        if (slash) slash[1] = 0; else path[0] = 0;
        strcat(path, "TiberianDawn.dll");
        dllpath = path;
    }

    g_dll = LoadLibraryA(dllpath);
    if (!g_dll)
    {
        _snprintf(err, errlen, "LoadLibraryA(%s) failed, GetLastError()=%lu",
                  dllpath, (unsigned long)GetLastError());
        return 0;
    }

    g_init     = (CNC_Init_t)    GetProcAddress(g_dll, "CNC_Init");
    g_config   = (CNC_Config_t)  GetProcAddress(g_dll, "CNC_Config");
    g_start    = (CNC_Start_t)   GetProcAddress(g_dll, "CNC_Start_Custom_Instance");
    g_advance  = (CNC_Advance_t) GetProcAddress(g_dll, "CNC_Advance_Instance");
    g_getstate = (CNC_GetState_t)GetProcAddress(g_dll, "CNC_Get_Game_State");
    g_dump     = (CNC3D_Dump_t)  GetProcAddress(g_dll, "CNC3D_Dump_Objects");
    g_input    = (CNC_Input_t)   GetProcAddress(g_dll, "CNC_Handle_Input");
    g_select   = (CNC_Select_t)  GetProcAddress(g_dll, "CNC_Select_Object");
    g_clearsel = (CNC_ClearSel_t)GetProcAddress(g_dll, "CNC_Clear_Object_Selection");
    g_cell2px  = (CNC3D_C2P_t)   GetProcAddress(g_dll, "CNC3D_Cell_To_Pixel");
    g_probe    = (CNC3D_Probe_t) GetProcAddress(g_dll, "CNC3D_Probe_Object_At");
    g_sidebarreq = (CNC_Sidebar_t)GetProcAddress(g_dll, "CNC_Handle_Sidebar_Request");

    if (!g_init || !g_config || !g_start || !g_advance || !g_getstate)
    {
        _snprintf(err, errlen, "missing CNC_* exports (init=%d config=%d start=%d adv=%d get=%d)",
                  !!g_init, !!g_config, !!g_start, !!g_advance, !!g_getstate);
        return 0;
    }
    wb_ensure_stdout();

    if (!g_dump)
    {
        /* Not fatal, but nothing will be drawn, so say it rather than render an empty
         * map and let someone conclude the renderer is broken. */
        _snprintf(err, errlen, "CNC3D_Dump_Objects is not exported: the brain was built "
                               "without brain/patches. No objects can be drawn.");
        return 0;
    }
    return 1;
}

int wb_start(const char *content_dir, const char *mission_dir, const char *scenario,
             int build_level, char *err, int errlen)
{
    WB_Rules rules;
    int i;

    memset(&rules, 0, sizeof rules);
    for (i = 0; i < 3; ++i)
    {
        rules.Difficulties[i].FirepowerBias   = 1.0f;
        rules.Difficulties[i].GroundspeedBias = 1.0f;
        rules.Difficulties[i].AirspeedBias    = 1.0f;
        rules.Difficulties[i].ArmorBias       = 1.0f;
        rules.Difficulties[i].ROFBias         = 1.0f;
        rules.Difficulties[i].CostBias        = 1.0f;
        rules.Difficulties[i].BuildSpeedBias  = 1.0f;
        rules.Difficulties[i].RepairDelay     = 0.02f;
        rules.Difficulties[i].BuildDelay      = 0.03f;
        rules.Difficulties[i].IsBuildSlowdown = 0;
        rules.Difficulties[i].IsWallDestroyer = 1;
        rules.Difficulties[i].IsContentScan   = 1;
    }

    g_init("", wb_event);
    g_config(&rules);

    if (!g_start(content_dir, mission_dir, scenario, build_level, 0))
    {
        _snprintf(err, errlen,
                  "CNC_Start_Custom_Instance failed (content=%s dir=%s scen=%s).\n"
                  "On Win98 the usual cause is a brain built without -DWIN9X=ON: the "
                  "common library is then UNICODE, raw_fopen becomes _wfopen, and the "
                  "wide entry points are stubs here, so no file opens.",
                  content_dir, mission_dir, scenario);
        return 0;
    }
    return 1;
}

void wb_tick(void) { if (g_advance) g_advance(0); }

void wb_clear_selection(void) { if (g_clearsel) g_clearsel(0); }

int wb_select(int kind, int id)
{
    if (!g_select || kind <= 0 || id < 0) return 0;
    return g_select(0, kind, id) ? 1 : 0;
}

void wb_build_start(int btype, int bid)
{ if (g_sidebarreq) g_sidebarreq(SB_START_CONSTRUCTION, 0, btype, bid, 0, 0); }
void wb_build_hold(int btype, int bid)
{ if (g_sidebarreq) g_sidebarreq(SB_HOLD_CONSTRUCTION, 0, btype, bid, 0, 0); }
void wb_build_cancel(int btype, int bid)
{ if (g_sidebarreq) g_sidebarreq(SB_CANCEL_CONSTRUCTION, 0, btype, bid, 0, 0); }
void wb_place_begin(int btype, int bid)
{ if (g_sidebarreq) g_sidebarreq(SB_START_PLACEMENT, 0, btype, bid, 0, 0); }
/* cx, cy are GRID coordinates, relative to the expanded map rectangle. See
 * wb_place_origin: the engine adds its own map_cell_x back on, so handing it an absolute
 * world cell places the building tens of cells away or fails silently. */
void wb_place_at(int btype, int bid, int cx, int cy)
{ if (g_sidebarreq) g_sidebarreq(SB_PLACE, 0, btype, bid, (short)cx, (short)cy); }
void wb_place_cancel(int btype, int bid)
{ if (g_sidebarreq) g_sidebarreq(SB_CANCEL_PLACE, 0, btype, bid, 0, 0); }
void wb_repair_click(void)
{ if (g_sidebarreq) g_sidebarreq(SB_CLICK_REPAIR, 0, 0, 0, 0, 0); }

/* ---- what would happen if you clicked there -----------------------------------------
 *
 * The engine's own ActionType for a cell, given the current selection: the whole basis of
 * the 3D cursor. CNC3D_Probe_Object_At is a PURE QUERY -- it changes nothing -- and it
 * returns the answer as its int return value, so there is no dump round trip and nothing
 * to parse back. The handoff recorded this as the cursor's blocker; it is not one.
 *
 * -1 means the engine refused the pixel (outside its tactical view). 0 is ACTION_NONE,
 * which is a real answer and not an error: it is what an empty selection returns.
 *
 * IT PRINTS AND FFLUSHES. Every call writes a PROBE| line to stdout, which in this
 * -mwindows build is a real file beside the exe, so the capture is truncated around the
 * call exactly as wb_objects does. Even so the caller must ask it sparingly. */
int wb_action_at_cell(int cell_x, int cell_y)
{
    int px = 0, py = 0, act;
    if (!g_probe || !g_cell2px) return -1;
    if (!g_cell2px(cell_x, cell_y, &px, &py)) return -1;
    wb_ensure_stdout();
    act = g_probe(px, py);
    wb_truncate_capture();
    return act;
}

int wb_command_at_cell(int cell_x, int cell_y, int ctrl)
{
    int px = 0, py = 0;
    if (!g_input || !g_cell2px) return 0;
    /* The brain owns the cell-to-engine-pixel conversion, and it returns false for a
     * cell outside its own tactical window, so a click off the engine's view is dropped
     * here rather than turned into an order at some other cell. */
    if (!g_cell2px(cell_x, cell_y, &px, &py)) return 0;
    g_input(INPUT_REQUEST_SPECIAL_KEYS, (unsigned char)(ctrl ? 1 : 0), 0, 0, 0, 0, 0);
    g_input(INPUT_REQUEST_COMMAND_AT_POSITION, 0, 0, px, py, 0, 0);
    g_input(INPUT_REQUEST_SPECIAL_KEYS, 0, 0, 0, 0, 0, 0);
    return 1;
}

static int rdi32(const unsigned char *p)
{
    return (int)((unsigned int)p[0] | ((unsigned int)p[1] << 8)
               | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24));
}
static float rdf32b(const unsigned char *p)
{
    union { unsigned int u; float f; } c;
    c.u = (unsigned int)p[0] | ((unsigned int)p[1] << 8)
        | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
    return c.f;
}

int wb_sidebar(W98_Sidebar *out)
{
    static unsigned char buf[64 * 1024];
    int col, i, n, taken = 0;

    memset(out, 0, sizeof *out);
    if (!g_getstate) return 0;
    memset(buf, 0, sizeof buf);
    if (!g_getstate(GAME_STATE_SIDEBAR, 0, buf, sizeof buf)) return 0;

    out->count[0]       = rdi32(buf + 0);
    out->count[1]       = rdi32(buf + 4);
    out->credits        = rdi32(buf + 8);
    out->tiberium       = rdi32(buf + 16);
    out->max_tiberium   = rdi32(buf + 20);
    out->power_produced = rdi32(buf + 24);
    out->power_drained  = rdi32(buf + 28);
    out->repair_enabled = buf[56];
    out->sell_enabled   = buf[57];
    out->radar_active   = buf[58];

    /* The entries are one flat run: the left column's, then the right column's. */
    for (col = 0; col < 2; ++col)
    {
        n = out->count[col];
        if (n < 0) n = 0;
        if (n > WB_MAX_BUILD) n = WB_MAX_BUILD;
        for (i = 0; i < n; ++i)
        {
            const unsigned char *e = buf + SB_HDR + (long)(taken + i) * SB_ENTRY;
            W98_Build *b = &out->item[col][i];
            if ((long)(e - buf) + SB_ENTRY > (long)sizeof buf) break;
            memcpy(b->name, e + SB_OFF_NAME, 15);
            b->name[15]     = 0;
            b->btype        = rdi32(e + SB_OFF_BTYPE);
            b->bid          = rdi32(e + SB_OFF_BID);
            b->cost         = rdi32(e + SB_OFF_COST);
            b->progress     = rdf32b(e + SB_OFF_PROG);
            b->completed    = e[SB_OFF_DONE];
            b->constructing = e[SB_OFF_CONS];
            b->onhold       = e[SB_OFF_HOLD];
            b->busy         = e[SB_OFF_BUSY];
            {   /* the footprint, so the placement preview does not need a table of its
                 * own for every structure type in the game */
                int k, nl = rdi32(e + SB_OFF_PLEN);
                if (nl < 0) nl = 0;
                if (nl > WB_OCCUPY_MAX) nl = WB_OCCUPY_MAX;
                for (k = 0; k < nl; ++k)
                    b->occupy[k] = (short)((int)e[SB_OFF_PLIST + k * 2]
                                        | ((int)e[SB_OFF_PLIST + k * 2 + 1] << 8));
                b->noccupy = nl;
            }
        }
        out->count[col] = n;
        taken += out->count[col];
    }
    return 1;
}

int wb_map(W98_MapInfo *out)
{
    WB_MapHead *m;
    int ok = 0;
    /* 640 KB, comfortably more than the struct's ~590 KB, and heap not stack. */
    unsigned char *buf = (unsigned char *)malloc(640u * 1024u);
    if (!buf) return 0;
    memset(buf, 0, 640u * 1024u);
    if (g_getstate(GAME_STATE_STATIC_MAP, 0, buf, 640u * 1024u))
    {
        m = (WB_MapHead *)buf;
        out->theater = m->Theater;
        out->cellx   = m->MapCellX;
        out->celly   = m->MapCellY;
        out->cellw   = m->MapCellWidth;
        out->cellh   = m->MapCellHeight;
        strncpy(out->scenario, m->ScenarioName, sizeof out->scenario - 1);
        out->scenario[sizeof out->scenario - 1] = 0;
        ok = 1;
    }
    free(buf);
    return ok;
}

/* ---- reading the object dump ------------------------------------------------------ *
 *
 * The brain prints. So stdout goes to a scratch file for the duration of the call and
 * comes straight back. _dup / _dup2 / _chsize are the msvcrt spellings of dup / dup2 /
 * ftruncate; the POSIX names do not all exist here.
 * ----------------------------------------------------------------------------------- */

static FILE *g_cap;

/* Throw away whatever the brain has printed into the capture. The 3D cursor's probe
 * writes a line and fflushes on every call; without this the scratch file grows for the
 * whole session and every probe pays a synchronous write to a Windows 98 disk. */
static void wb_truncate_capture(void)
{
    if (!g_cap) return;
    fflush(stdout);
    _chsize(_fileno(g_cap), 0);
    fseek(g_cap, 0, SEEK_SET);
}

/* A -mwindows program has NO STDOUT. fd 1 is not open, _dup(1) returns -1, and since the
 * brain reports its object list by calling printf, the capture below would quietly hand
 * back nothing at all: the console test finds 60 objects and the windowed game finds
 * zero, which is a maddening way to lose an afternoon. So make stdout real before anyone
 * needs it. The DLL and the exe both import msvcrt.dll dynamically, so they share one CRT
 * and one stdout, which is why redirecting ours redirects the brain's. */
static void wb_ensure_stdout(void)
{
    if (_fileno(stdout) >= 0) return;               /* a console build already has one */
    {
        char path[MAX_PATH], *slash;
        GetModuleFileNameA(NULL, path, sizeof path - 24);
        slash = strrchr(path, '\\');
        if (slash) slash[1] = 0; else path[0] = 0;
        strcat(path, "brainout.tmp");
        freopen(path, "w+b", stdout);
    }
}

static int field_int(const char *line, const char *key, int dflt)
{
    const char *p = strstr(line, key);
    return p ? atoi(p + strlen(key)) : dflt;
}

static void field_str(const char *line, int index, char *out, int outlen)
{
    /* the first four fields are positional: OBJ|kind|name|house|... */
    const char *p = line;
    int i;
    int n = 0;
    for (i = 0; i < index; ++i)
    {
        p = strchr(p, '|');
        if (!p) { out[0] = 0; return; }
        ++p;
    }
    while (p[n] && p[n] != '|' && n < outlen - 1) { out[n] = p[n]; ++n; }
    out[n] = 0;
}

/* CNCShroudStruct is `int Count;` then Count entries of
 *   { char ShadowIndex; bool IsVisible; bool IsMapped; bool IsJamming; }
 * which under the ABI's #pragma pack(1) is exactly four bytes each. Asserted, not
 * assumed, for the same reason every other mirror in this file is. */
typedef struct
{
    signed char   ShadowIndex;
    unsigned char IsVisible, IsMapped, IsJamming;
} __attribute__((packed)) WB_ShroudEntry;
typedef char wb_shroud_check[(sizeof(WB_ShroudEntry) == 4) ? 1 : -1];

/* ---- placement legality -------------------------------------------------------------
 * See wb_placement in the header for what the two arrays mean. The entries are laid out
 * over exactly the same expanded map rectangle GAME_STATE_SHROUD uses, and both exports
 * do the one-cell expansion identically, so the indexing here is the shroud's. */
typedef struct { unsigned char prox, clear; } WB_PlaceEntry;

void wb_place_origin(int *cellx, int *celly)
{
    static W98_MapInfo mi;
    if (mi.cellw < 1) wb_map(&mi);
    if (cellx) *cellx = mi.cellx;
    if (celly) *celly = mi.celly;
}

int wb_placement(unsigned char *prox4096, unsigned char *clear4096)
{
    static unsigned char buf[4 + 128 * 128 * 2 + 1024];
    const WB_PlaceEntry *e;
    static W98_MapInfo mi;
    int count, i, w, h;

    memset(prox4096, 0, 64 * 64);
    memset(clear4096, 0, 64 * 64);
    if (!g_getstate) return 0;
    if (mi.cellw < 1 && !wb_map(&mi)) return 0;
    if (mi.cellw < 1) return 0;
    /* This one answers FALSE unless a building is actually pending placement, so the
     * false return is the "not in placement mode" signal and not an error. */
    if (!g_getstate(GAME_STATE_PLACEMENT, 0, buf, (unsigned int)sizeof buf)) return 0;

    w = mi.cellw;
    h = mi.cellh;
    count = rdi32(buf);
    if (count < 0 || count > w * h) count = w * h;
    e = (const WB_PlaceEntry *)(buf + 4);
    for (i = 0; i < count; ++i)
    {
        int cx = mi.cellx + (i % w);
        int cy = mi.celly + (i / w);
        if (cx < 0 || cx > 63 || cy < 0 || cy > 63) continue;
        prox4096[cy * 64 + cx]  = e[i].prox  ? 1 : 0;
        clear4096[cy * 64 + cx] = e[i].clear ? 1 : 0;
    }
    return count;
}

int wb_shroud(unsigned char *out4096)
{
    /* The engine wants sizeof(struct) + 256 spare before it will fill anything, and it
     * returns FALSE rather than truncating if the buffer runs short, so this is sized
     * generously. Static rather than on the stack: it is read every heartbeat. */
    static unsigned char buf[4 + 64 * 64 * 4 + 1024];
    const WB_ShroudEntry *e;
    /* The map rectangle never changes during a mission, and asking for it means another
     * GAME_STATE_STATIC_MAP, which is a 640 KB read and a full walk of the map. Read once. */
    static W98_MapInfo mi;
    int count, i, w, h;

    memset(out4096, WB_SHROUD_UNMAPPED, 64 * 64);
    if (!g_getstate) return 0;
    if (mi.cellw < 1 && !wb_map(&mi)) return 0;
    if (mi.cellw < 1) return 0;
    if (!g_getstate(GAME_STATE_SHROUD, 0, buf, (unsigned int)sizeof buf)) return 0;

    /* THE ENTRIES ARE THE MAP RECTANGLE, NOT THE 64x64 GRID, and the rectangle is the one
     * GAME_STATE_STATIC_MAP already reports: dllinterface.cpp expands MapCellX/Width by
     * one on every side that has room, in BOTH exports, identically (:2896 and :5529). So
     * entry i is cell (cellx + i mod cellw, celly + i div cellw) and nothing else.
     *
     * Taking the index as a raw cell number instead put the whole shroud in the top-left
     * corner of the map, which on screen looked exactly like "the player can see nothing
     * anywhere" rather than like an indexing mistake: 700 entries laid over 4096 cells
     * covered everything the camera was looking at. The count is what gave it away, 700
     * being precisely 28 x 25. */
    w = mi.cellw;
    h = mi.cellh;
    count = rdi32(buf);
    if (count < 0 || count > w * h) count = w * h;
    e = (const WB_ShroudEntry *)(buf + 4);
    for (i = 0; i < count; ++i)
    {
        int cx = mi.cellx + (i % w);
        int cy = mi.celly + (i / w);
        if (cx < 0 || cx > 63 || cy < 0 || cy > 63) continue;
        out4096[cy * 64 + cx] = e[i].IsVisible ? WB_SHROUD_VISIBLE
                              : e[i].IsMapped  ? WB_SHROUD_MAPPED
                                               : WB_SHROUD_UNMAPPED;
    }
    return count;
}

static W98_Overlays g_ov;
const W98_Overlays *wb_overlays(void) { return &g_ov; }

/* `NAME|a|b|c...`: the nth bar-separated field as an int. The OBJ| lines are key=value
 * and have field_int for that; these lines are positional. */
static int field_pos(const char *line, int n)
{
    const char *p = line;
    int i;
    for (i = 0; i < n; ++i)
    {
        p = strchr(p, '|');
        if (!p) return 0;
        ++p;
    }
    return atoi(p);
}

int wb_objects(W98_Object *out, int max)
{
    int saved, outfd, count = 0;
    char line[1024];

    if (!g_dump) return 0;

    /* Capture on _fileno(stdout), NOT on the literal 1.
     *
     * In a console build they are the same. In the -mwindows build they are NOT: the
     * process starts with no standard handles at all, wb_ensure_stdout freopens stdout
     * onto a file, and freopen is free to hand it any descriptor it likes. Duping the
     * literal 1 then fails, wb_objects returns zero, and the game draws an empty map
     * while the console test built from the same source finds sixty objects. */
    outfd = _fileno(stdout);
    if (outfd < 0) return 0;

    fflush(stdout);
    saved = _dup(outfd);
    if (saved < 0) return 0;

    if (!g_cap)
    {
        /* Our own scratch file beside the exe rather than tmpfile(), which on Win9x
         * wants the root of the current drive and is not always writable. */
        char path[MAX_PATH], *slash;
        GetModuleFileNameA(NULL, path, sizeof path - 24);
        slash = strrchr(path, '\\');
        if (slash) slash[1] = 0; else path[0] = 0;
        strcat(path, "objdump.tmp");
        g_cap = fopen(path, "w+b");
    }
    if (!g_cap) { _close(saved); return 0; }

    rewind(g_cap);
    _chsize(_fileno(g_cap), 0);
    fflush(g_cap);
    _dup2(_fileno(g_cap), outfd);

    g_dump();

    fflush(stdout);
    _dup2(saved, outfd);
    _close(saved);

    rewind(g_cap);
    memset(&g_ov, 0, sizeof g_ov);
    /* NOTE the loop condition is on fgets alone: `count < max` used to be in it, so a map
     * with more objects than the caller's array stopped the scan dead and every overlay
     * line after the last object was never seen. The object store is bounded below
     * instead, which leaves the rest of the capture readable. */
    while (fgets(line, sizeof line, g_cap))
    {
        W98_Object *o;
        if (strncmp(line, "TIB|", 4) == 0)
        {
            if (g_ov.ntib < WB_MAX_TIB)
            {
                W98_Tib *t = &g_ov.tib[g_ov.ntib++];
                t->cx    = (short)field_pos(line, 1);
                t->cy    = (short)field_pos(line, 2);
                t->kind  = (unsigned char)field_pos(line, 3);
                t->stage = (unsigned char)field_pos(line, 4);
            }
            else ++g_ov.tib_dropped;
            continue;
        }
        if (strncmp(line, "SMUDGE|", 7) == 0)
        {
            if (g_ov.nsmudge < WB_MAX_SMUDGE)
            {
                W98_Smudge *m = &g_ov.smudge[g_ov.nsmudge++];
                m->cx    = (short)field_pos(line, 1);
                m->cy    = (short)field_pos(line, 2);
                m->kind  = (unsigned char)field_pos(line, 3);
                m->stage = (unsigned char)field_pos(line, 4);
            }
            else ++g_ov.smudge_dropped;
            continue;
        }
        if (strncmp(line, "WALL|", 5) == 0)
        {
            if (g_ov.nwall < WB_MAX_WALLS)
            {
                W98_Wall *w = &g_ov.wall[g_ov.nwall++];
                w->cx   = (short)field_pos(line, 1);
                w->cy   = (short)field_pos(line, 2);
                field_str(line, 3, w->name, sizeof w->name);
                w->kind = (unsigned char)field_pos(line, 4);
                w->icon = (unsigned char)field_pos(line, 5);
                w->dmg  = (unsigned char)field_pos(line, 6);
            }
            else ++g_ov.wall_dropped;
            continue;
        }
        if (strncmp(line, "EFX|BULLET|", 11) == 0)
        {
            if (g_ov.nbullet < WB_MAX_BULLETS)
            {
                W98_Bullet *b = &g_ov.bullet[g_ov.nbullet++];
                const char *p = strstr(line, "|name=");
                int n = 0;
                if (p) { p += 6; while (p[n] && p[n] != '|' && n < 15) { b->name[n] = p[n]; ++n; } }
                b->name[n] = 0;
                b->lx    = field_int(line, "|lx=", 0);
                b->ly    = field_int(line, "|ly=", 0);
                b->face  = field_int(line, "|face=", 0);
                b->alt   = field_int(line, "|alt=", 0);
                b->invis = field_int(line, "|invis=", 0);
            }
            else ++g_ov.bullet_dropped;
            continue;
        }
        if (strncmp(line, "EFX|ANIM|", 9) == 0)
        {
            if (g_ov.nanim < WB_MAX_ANIMS)
            {
                W98_Anim *a = &g_ov.anim[g_ov.nanim++];
                const char *p = strstr(line, "|name=");
                int n = 0;
                if (p) { p += 6; while (p[n] && p[n] != '|' && n < 15) { a->name[n] = p[n]; ++n; } }
                a->name[n] = 0;
                a->type   = field_int(line, "|type=", -1);
                a->size   = field_int(line, "|size=", 0);
                a->ground = field_int(line, "|ground=", 0);
                a->lx     = field_int(line, "|lx=", 0);
                a->ly     = field_int(line, "|ly=", 0);
                a->stage  = field_int(line, "|stage=", 0);
                a->start  = field_int(line, "|start=", 0);
                a->stages = field_int(line, "|stages=", 0);
            }
            else ++g_ov.anim_dropped;
            continue;
        }
        if (strncmp(line, "OBJ|", 4) != 0) continue;
        if (count >= max) continue;
        o = &out[count];
        memset(o, 0, sizeof *o);
        field_str(line, 1, o->kind,  sizeof o->kind);
        field_str(line, 2, o->name,  sizeof o->name);
        field_str(line, 3, o->house, sizeof o->house);
        {   /* mission=NAME is a string field, so it is read positionally too */
            const char *p = strstr(line, "|mission=");
            int n = 0;
            if (p)
            {
                p += 9;
                while (p[n] && p[n] != '|' && n < (int)sizeof o->mission - 1)
                { o->mission[n] = p[n]; ++n; }
            }
            o->mission[n] = 0;
        }
        o->cell        = field_int(line, "|newcell=", -1);
        o->cx          = field_int(line, "|x=", -1);
        o->cy          = field_int(line, "|y=", -1);
        o->centercell  = field_int(line, "|centercell=", -1);
        o->strength    = field_int(line, "|str=", 0);
        o->maxstrength = field_int(line, "|max=", 0);
        o->limbo       = field_int(line, "|limbo=", 0);
        o->down        = field_int(line, "|down=", 0);
        o->lx          = field_int(line, "|lx=", 0);
        o->ly          = field_int(line, "|ly=", 0);
        o->clx         = field_int(line, "|clx=", 0);
        o->cly         = field_int(line, "|cly=", 0);
        o->fw          = field_int(line, "|fw=", 1);
        o->fh          = field_int(line, "|fh=", 1);
        o->face        = field_int(line, "|face=", 0);
        o->tface       = field_int(line, "|tface=", -1);
        o->id          = field_int(line, "|id=", -1);
        o->sel         = field_int(line, "|sel=", 0);
        o->doing       = field_int(line, "|doing=", 0);
        o->dostage     = field_int(line, "|dostage=", 0);
        o->dying       = field_int(line, "|dying=", 0);
        o->makecnt     = field_int(line, "|makecnt=", -1);
        o->doorstage   = field_int(line, "|door=", -1);
        o->status      = field_int(line, "|status=", -1);
        o->nav         = field_int(line, "|nav=", -1);
        o->tar         = field_int(line, "|tar=", -1);
        ++count;
    }
    return count;
}

void wb_close(void)
{
    if (g_cap) { fclose(g_cap); g_cap = NULL; }
    if (g_dll) { FreeLibrary(g_dll); g_dll = NULL; }
}
