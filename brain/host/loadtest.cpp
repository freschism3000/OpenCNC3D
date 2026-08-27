// Honest load test: dlopen the TD brain, feed it a loose-file N64 mission,
// then PRINT the resulting game state and read it.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#include <csignal>
static void crash(int sig){ void* bt[40]; int n=backtrace(bt,40); fprintf(stderr,"\n*** SIGNAL %d ***\n",sig); backtrace_symbols_fd(bt,n,2); _exit(139);} 

#define TIBERIAN_DAWN 1
#define MEGAMAPS 1
#define _MAX_FNAME 256
#define _MAX_EXT   256
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

static void ev_cb(const EventCallbackStruct& e)
{
    if (e.EventType == CALLBACK_EVENT_DEBUG_PRINT) {
        printf("  [dbg] %s\n", e.DebugPrint.PrintString ? e.DebugPrint.PrintString : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_MOVIE) {
        printf("  [movie] %s\n", e.Movie.MovieName ? e.Movie.MovieName : "(null)");
    } else {
        printf("  [event] type=%d\n", (int)e.EventType);
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGSEGV, crash); signal(SIGBUS, crash); signal(SIGILL, crash); signal(SIGABRT, crash);
    const char* dylib = argv[1];
    const char* dir   = argv[2];   // must end in '/'
    const char* scen  = argv[3];
    int build_level   = argc > 4 ? atoi(argv[4]) : 1;
    const char* content = argc > 5 ? argv[5] : dir;

    void* h = dlopen(dylib, RTLD_NOW);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); return 1; }

    auto Init    = (CNC_Init_t)        dlsym(h, "CNC_Init");
    auto Config  = (CNC_Config_t)      dlsym(h, "CNC_Config");
    auto Start   = (CNC_Start_Custom_t)dlsym(h, "CNC_Start_Custom_Instance");
    auto GetState= (CNC_Get_State_t)   dlsym(h, "CNC_Get_Game_State");
    auto Advance = (CNC_Advance_t)     dlsym(h, "CNC_Advance_Instance");
    if (!Init || !Config || !Start || !GetState) { printf("dlsym FAILED\n"); return 1; }

    printf("== CNC_Init\n");
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
    printf("== CNC_Config\n");
    Config(rules);

    printf("== CNC_Start_Custom_Instance(content=\"%s\", dir=\"%s\", scen=\"%s\", build=%d, mp=false)\n",
           content, dir, scen, build_level);
    bool ok = Start(content, dir, scen, build_level, false);
    printf("   returned %s\n", ok ? "TRUE" : "FALSE");
    if (!ok) return 2;

    /* ---- static map ---- */
    static CNCMapDataStruct mapdata;
    memset(&mapdata, 0, sizeof(mapdata));
    bool mok = GetState(GAME_STATE_STATIC_MAP, 0, (unsigned char*)&mapdata, sizeof(mapdata));
    printf("== GAME_STATE_STATIC_MAP -> %s\n", mok ? "true" : "false");
    if (mok) {
        printf("   Scenario '%s' Theater=%d\n", mapdata.ScenarioName, (int)mapdata.Theater);
        printf("   Cell X=%d Y=%d W=%d H=%d  (original %d,%d %dx%d)\n",
               mapdata.MapCellX, mapdata.MapCellY, mapdata.MapCellWidth, mapdata.MapCellHeight,
               mapdata.OriginalMapCellX, mapdata.OriginalMapCellY,
               mapdata.OriginalMapCellWidth, mapdata.OriginalMapCellHeight);
        /* Locate StaticCells empirically: our _MAX_FNAME/_MAX_EXT guess may not
           match the DLL's, so find the first template name in the raw buffer. */
        {
            unsigned char* raw = (unsigned char*)&mapdata;
            size_t base = 0;
            for (size_t k = 0; k + 8 < sizeof(mapdata); k++) {
                if (memcmp(raw + k, ".tga", 4) == 0) {
                    /* walk back to the start of this NUL-terminated string */
                    size_t j = k;
                    while (j > 0 && raw[j-1] != 0) j--;
                    base = j;
                    printf("   [first .tga string at %zu -> record start %zu: '%s']\n", k, j, (char*)raw+j);
                    break;
                }
            }
            printf("   [StaticCells found at byte offset %zu; my struct says %zu]\n",
                   base, (size_t)((unsigned char*)&mapdata.StaticCells[0] - raw));
            struct Cell36 { char name[32]; int icon; };
            Cell36* cells = (Cell36*)(raw + base);
            int tot = mapdata.MapCellWidth * mapdata.MapCellHeight;
            int nc = 0;
            for (int i = 0; i < tot; i++) {
                if (cells[i].name[0] && strncasecmp(cells[i].name, "clear1", 6) != 0) nc++;
            }
            printf("   cells=%d  non-clear=%d\n", tot, nc);
            printf("   sample: ");
            int shown2 = 0;
            for (int i = 0; i < tot && shown2 < 14; i++) {
                if (cells[i].name[0] && strncasecmp(cells[i].name, "clear1", 6) != 0) {
                    printf("%s(i=%d) ", cells[i].name, cells[i].icon); shown2++;
                }
            }
            printf("\n");
        }

    }

    /* ---- object layers ---- */
    static unsigned char buf[8 * 1024 * 1024];
    memset(buf, 0, sizeof(buf));
    bool lok = GetState(GAME_STATE_LAYERS, 0, buf, sizeof(buf));
    printf("== GAME_STATE_LAYERS -> %s\n", lok ? "true" : "false");
    if (lok) {
        CNCObjectListStruct* list = (CNCObjectListStruct*)buf;
        printf("   object count = %d\n", list->Count);
        int shown = 0;
        int counts[20]; memset(counts, 0, sizeof(counts));
        for (int i = 0; i < list->Count; i++) {
            CNCObjectStruct& o = list->Objects[i];
            if (o.Type >= 0 && o.Type < 20) counts[o.Type]++;
            if (shown < 25) {
                printf("   #%-3d type=%-2d name=%-8s owner=%-2d cell=(%d,%d) str=%d/%d\n",
                       i, (int)o.Type, o.TypeName, (int)o.Owner, o.CellX, o.CellY, o.Strength, o.MaxStrength);
                shown++;
            }
        }
        const char* tn[20] = {"UNKNOWN","INFANTRY","UNIT","AIRCRAFT","BUILDING","TERRAIN","ANIM",
                              "BULLET","OVERLAY","SMUDGE","OBJECT","SPECIAL","INF_TYPE","UNIT_TYPE",
                              "AIR_TYPE","BLD_TYPE","VESSEL","VESSEL_TYPE","-","-"};
        printf("   by type:");
        for (int i = 0; i < 20; i++) if (counts[i]) printf(" %s=%d", tn[i], counts[i]);
        printf("\n");
    }

    /* ---- occupiers: proves the INI objects were instantiated, no art needed ---- */
    memset(buf, 0, 2*1024*1024);
    bool ook = GetState(GAME_STATE_OCCUPIER, 0, buf, sizeof(buf));
    printf("== GAME_STATE_OCCUPIER -> %s\n", ook ? "true" : "false");
    if (ook) {
        int* p = (int*)buf;
        int cellcount = *p++;
        int occupied = 0, objs = 0;
        int bytype[20]; memset(bytype, 0, sizeof(bytype));
        for (int c = 0; c < cellcount; c++) {
            int n = *p++;
            occupied += (n > 0);
            for (int k = 0; k < n; k++) {
                int t = *p++; int id = *p++; (void)id;
                objs++; if (t >= 0 && t < 20) bytype[t]++;
            }
        }
        const char* tn[20] = {"UNKNOWN","INFANTRY","UNIT","AIRCRAFT","BUILDING","TERRAIN","ANIM",
                              "BULLET","OVERLAY","SMUDGE","OBJECT","SPECIAL","INF_TYPE","UNIT_TYPE",
                              "AIR_TYPE","BLD_TYPE","VESSEL","VESSEL_TYPE","-","-"};
        printf("   cells=%d occupied=%d objects=%d :", cellcount, occupied, objs);
        for (int i = 0; i < 20; i++) if (bytype[i]) printf(" %s=%d", tn[i], bytype[i]);
        printf("\n");
    }

    if (Advance) {
        printf("== advancing 30 frames\n");
        for (int i = 0; i < 30; i++) Advance(0);
        memset(buf, 0, 1024 * 1024);
        if (GetState(GAME_STATE_LAYERS, 0, buf, sizeof(buf))) {
            CNCObjectListStruct* list = (CNCObjectListStruct*)buf;
            printf("   object count after 30 frames = %d\n", list->Count);
        }
    }
    printf("== done\n");
    return 0;
}
