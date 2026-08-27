// CNC3D: actually run an N64 mission in the TD brain and print the truth.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#include <csignal>

static int g_tick = -1;
static void crash(int sig)
{
    void* bt[48];
    int n = backtrace(bt, 48);
    fprintf(stderr, "\n*** SIGNAL %d during tick %d ***\n", sig, g_tick);
    backtrace_symbols_fd(bt, n, 2);
    _exit(139);
}

#define TIBERIAN_DAWN 1
#define MEGAMAPS 1
#define _MAX_FNAME 256
#define _MAX_EXT 256
typedef uint64_t uint64;
#define __declspec(x)
#define __cdecl

#include "dllinterface.h"
typedef void (*CNC_Event_Callback_Type)(const EventCallbackStruct&);

typedef void (*CNC_Init_t)(const char*, CNC_Event_Callback_Type);
typedef void (*CNC_Config_t)(const CNCRulesDataStruct&);
typedef bool (*CNC_Start_Custom_t)(const char*, const char*, const char*, int, bool);
typedef bool (*CNC_Get_State_t)(GameStateRequestEnum, uint64, unsigned char*, unsigned int);
typedef bool (*CNC_Advance_t)(uint64);
typedef int (*CNC3D_Dump_t)(void);

static int g_events = 0;
static void ev_cb(const EventCallbackStruct& e)
{
    g_events++;
    if (e.EventType == CALLBACK_EVENT_DEBUG_PRINT) {
        const char* s = e.DebugPrint.PrintString;
        if (s && strstr(s, "CNC_Advance_Instance"))
            return; // spam
        printf("  [dbg] %s\n", s ? s : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_MOVIE) {
        printf("  [movie] %s\n", e.Movie.MovieName ? e.Movie.MovieName : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_MESSAGE) {
        printf("  [msg] %s\n", e.Message.Message ? e.Message.Message : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_GAME_OVER) {
        printf("  [GAME OVER] type=%d\n", (int)e.EventType);
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGSEGV, crash);
    signal(SIGBUS, crash);
    signal(SIGILL, crash);
    signal(SIGABRT, crash);

    const char* dylib = argv[1];
    const char* dir = argv[2];
    const char* scen = argv[3];
    int build_level = argc > 4 ? atoi(argv[4]) : 1;
    const char* content = argc > 5 ? argv[5] : dir;
    int nticks = argc > 6 ? atoi(argv[6]) : 300;

    void* h = dlopen(dylib, RTLD_NOW);
    if (!h) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }
    auto Init = (CNC_Init_t)dlsym(h, "CNC_Init");
    auto Config = (CNC_Config_t)dlsym(h, "CNC_Config");
    auto Start = (CNC_Start_Custom_t)dlsym(h, "CNC_Start_Custom_Instance");
    auto GetState = (CNC_Get_State_t)dlsym(h, "CNC_Get_Game_State");
    auto Advance = (CNC_Advance_t)dlsym(h, "CNC_Advance_Instance");
    auto Dump = (CNC3D_Dump_t)dlsym(h, "CNC3D_Dump_Objects");
    printf("symbols: Init=%p Config=%p Start=%p GetState=%p Advance=%p Dump=%p\n",
           (void*)Init, (void*)Config, (void*)Start, (void*)GetState, (void*)Advance, (void*)Dump);
    if (!Init || !Config || !Start || !GetState || !Advance) {
        printf("dlsym FAILED\n");
        return 1;
    }

    printf("== CNC_Init(\"\")\n");
    Init("", ev_cb);

    CNCRulesDataStruct rules;
    memset(&rules, 0, sizeof(rules));
    for (int i = 0; i < 3; i++) {
        rules.Difficulties[i].FirepowerBias = 1.0f;
        rules.Difficulties[i].GroundspeedBias = 1.0f;
        rules.Difficulties[i].AirspeedBias = 1.0f;
        rules.Difficulties[i].ArmorBias = 1.0f;
        rules.Difficulties[i].ROFBias = 1.0f;
        rules.Difficulties[i].CostBias = 1.0f;
        rules.Difficulties[i].BuildSpeedBias = 1.0f;
        rules.Difficulties[i].RepairDelay = 0.02f;
        rules.Difficulties[i].BuildDelay = 0.03f;
        rules.Difficulties[i].IsBuildSlowdown = false;
        rules.Difficulties[i].IsWallDestroyer = true;
        rules.Difficulties[i].IsContentScan = true;
    }
    Config(rules);

    printf("== CNC_Start_Custom_Instance(content=\"%s\", dir=\"%s\", scen=\"%s\", build=%d, mp=false)\n",
           content, dir, scen, build_level);
    bool ok = Start(content, dir, scen, build_level, false);
    printf("START_RESULT=%s\n", ok ? "TRUE" : "FALSE");
    if (!ok)
        return 2;

    /* ---------- static map ---------- */
    static CNCMapDataStruct mapdata;
    memset(&mapdata, 0, sizeof(mapdata));
    bool mok = GetState(GAME_STATE_STATIC_MAP, 0, (unsigned char*)&mapdata, sizeof(mapdata));
    printf("== GAME_STATE_STATIC_MAP -> %s\n", mok ? "true" : "false");
    if (mok) {
        printf("MAP|theater=%d|cell=%d,%d|size=%dx%d|orig=%d,%d %dx%d\n",
               (int)mapdata.Theater, mapdata.MapCellX, mapdata.MapCellY, mapdata.MapCellWidth,
               mapdata.MapCellHeight, mapdata.OriginalMapCellX, mapdata.OriginalMapCellY,
               mapdata.OriginalMapCellWidth, mapdata.OriginalMapCellHeight);
    }

    /* ---------- objects, straight from the engine heaps ---------- */
    printf("=== OBJECT DUMP @ tick 0 ===\n");
    if (Dump)
        Dump();
    else
        printf("(CNC3D_Dump_Objects not exported)\n");

    /* ---------- occupier, parsed CORRECTLY ----------
       NOTE: Get_Occupier_State advances `entry` to (occupier + 1), which skips an
       extra 8-byte CNCOccupierObjectStruct per cell. Stride = 4 + 8*n + 8. */
    static unsigned char buf[16 * 1024 * 1024];
    memset(buf, 0, 4 * 1024 * 1024);
    bool ook = GetState(GAME_STATE_OCCUPIER, 0, buf, sizeof(buf));
    printf("== GAME_STATE_OCCUPIER -> %s\n", ook ? "true" : "false");
    if (ook) {
        unsigned char* p = buf;
        int cellcount = *(int*)p;
        p += 4;
        int occupied = 0, objs = 0, bytype[20];
        memset(bytype, 0, sizeof(bytype));
        for (int c = 0; c < cellcount; c++) {
            int n = *(int*)p;
            p += 4;
            if (n > 0)
                occupied++;
            for (int k = 0; k < n; k++) {
                int t = *(int*)p;
                p += 8;
                objs++;
                if (t >= 0 && t < 20)
                    bytype[t]++;
            }
            p += 8; /* the engine's extra advance */
        }
        const char* tn[20] = {"UNKNOWN", "INFANTRY",  "UNIT",     "AIRCRAFT", "BUILDING",
                              "TERRAIN", "ANIM",      "BULLET",   "OVERLAY",  "SMUDGE",
                              "OBJECT",  "SPECIAL",   "INF_TYPE", "UNIT_TYPE","AIR_TYPE",
                              "BLD_TYPE","VESSEL",    "VES_TYPE", "-",        "-"};
        printf("OCCUPIER|cells=%d|occupied=%d|objects=%d :", cellcount, occupied, objs);
        for (int i = 0; i < 20; i++)
            if (bytype[i])
                printf(" %s=%d", tn[i], bytype[i]);
        printf("\n");
    }

    /* ---------- tick ---------- */
    printf("=== ADVANCING %d TICKS ===\n", nticks);
    int done = 0;
    for (int i = 0; i < nticks; i++) {
        g_tick = i;
        bool a = Advance(0);
        done++;
        if (!a) {
            printf("ADVANCE returned false at tick %d\n", i);
            break;
        }
    }
    printf("TICKS_OK=%d\n", done);

    printf("=== OBJECT DUMP @ tick %d ===\n", done);
    if (Dump)
        Dump();

    printf("== done, %d engine events seen\n", g_events);
    return 0;
}
