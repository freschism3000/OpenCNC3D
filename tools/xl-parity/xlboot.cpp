/*
 * xlboot -- the smallest thing that can boot a scenario on a brain and say what happened.
 *
 * WHY IT IS NOT THE GAME. game/cnc_eyes is the host, and the host still carries a
 * compile-time 128 cell ceiling in its own grids, so it cannot open an XL-native map at
 * all until that work lands. The BRAIN can, and the two questions should not be tangled:
 * this answers "does the engine run this map", and nothing else. It opens no window, reads
 * no pack, and draws nothing.
 *
 * WHAT IT DELIBERATELY DOES NOT ASK FOR. GAME_STATE_STATIC_MAP fills a
 * CNCMapDataStruct whose StaticCells array is MAX_EXPORT_CELLS long, and MAX_EXPORT_CELLS
 * is still the classic 128*128 on both brains. A 256 wide map has four times more cells
 * than that array holds, so requesting it is how you get a buffer overrun rather than a
 * measurement. Widening that pair in lockstep is the host phase's job. Until then this
 * probe reads only the text dump, which carries per-axis coordinates and no fixed arrays.
 *
 *   xlboot --dylib PATH --dir DIR --scen NAME [--content DIR] [--ticks N] [--build N] [--mp]
 *
 * Prints one receipt line per stage and exits non-zero on the first thing that fails, so
 * a gate can read the exit status and a person can read the log.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <dlfcn.h>
#include <unistd.h>

static int g_tick = -1;
static const char* g_stage = "startup";

static void crash(int sig)
{
    /* Which tick and which stage, on the way out. A segfault with no context is how the
       mega format's own breakage stayed a mystery for a phase. */
    fprintf(stderr, "\nXLBOOT|CRASH|signal=%d|stage=%s|tick=%d\n", sig, g_stage, g_tick);
    _exit(139);
}

#define TIBERIAN_DAWN 1
#define MEGAMAPS 1
#define _MAX_FNAME 256
#define _MAX_EXT 256
typedef uint64_t uint64;
#include "dllinterface.h"

typedef void (*CNC_Event_Callback_Type)(const EventCallbackStruct&);
typedef void (*CNC_Init_t)(const char*, CNC_Event_Callback_Type);
typedef void (*CNC_Config_t)(const CNCRulesDataStruct&);
typedef bool (*CNC_Start_Custom_t)(const char*, const char*, const char*, int, bool);
typedef bool (*CNC_Advance_t)(uint64);
typedef int (*CNC3D_Dump_t)(void);
typedef bool (*CNC3D_AbiFacts_t)(void*);

static int g_events = 0;
static int g_brainMsgs = 0;

/* PRINT WHAT THE BRAIN SAYS, which this did not do and which cost a gate its whole point.
 *
 * A refused map binary is reported through GlyphX_Debug_Print, which the brain turns into a
 * CALLBACK_EVENT_DEBUG_PRINT rather than writing anywhere. Counting events without printing
 * them made the difference between a map the engine loaded and one it REFUSED exactly one
 * digit on a line no gate reads: events=14 against events=15. A deliberately corrupted .BIN
 * scored full marks on every leg, because the scenario still comes up from its INI and the
 * object dump carries no terrain channel at all.
 *
 * So every message the brain emits is printed, with a prefix a gate can grep for in both
 * directions: assert it is ABSENT for a map that should load, and PRESENT for a control that
 * should not. A diagnostic nothing prints is not a diagnostic. */
static void ev_cb(const EventCallbackStruct& e)
{
    g_events++;
    if (e.EventType == CALLBACK_EVENT_DEBUG_PRINT && e.DebugPrint.PrintString != NULL) {
        g_brainMsgs++;
        printf("XLBOOT|brain-says|%s\n", e.DebugPrint.PrintString);
    }
}

int main(int argc, char** argv)
{
    signal(SIGSEGV, crash);
    signal(SIGBUS, crash);
    signal(SIGABRT, crash);

    const char* dylib = NULL;
    const char* dir = NULL;
    const char* scen = NULL;
    const char* content = NULL;
    int nticks = 300;
    int build_level = 7;
    bool mp = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dylib") && i + 1 < argc) dylib = argv[++i];
        else if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--scen") && i + 1 < argc) scen = argv[++i];
        else if (!strcmp(argv[i], "--content") && i + 1 < argc) content = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) nticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--build") && i + 1 < argc) build_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mp")) mp = true;
        else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    if (!dylib || !dir || !scen) {
        fprintf(stderr, "usage: xlboot --dylib PATH --dir DIR --scen NAME "
                        "[--content DIR] [--ticks N] [--build N] [--mp]\n");
        return 2;
    }
    if (!content) content = dir;

    g_stage = "dlopen";
    void* hnd = dlopen(dylib, RTLD_NOW);
    if (!hnd) { printf("XLBOOT|FAIL|dlopen|%s\n", dlerror()); return 1; }

    CNC_Init_t Init = (CNC_Init_t)dlsym(hnd, "CNC_Init");
    CNC_Config_t Config = (CNC_Config_t)dlsym(hnd, "CNC_Config");
    CNC_Start_Custom_t Start = (CNC_Start_Custom_t)dlsym(hnd, "CNC_Start_Custom_Instance");
    CNC_Advance_t Advance = (CNC_Advance_t)dlsym(hnd, "CNC_Advance_Instance");
    CNC3D_Dump_t Dump = (CNC3D_Dump_t)dlsym(hnd, "CNC3D_Dump_Objects");
    CNC3D_AbiFacts_t Facts = (CNC3D_AbiFacts_t)dlsym(hnd, "CNC3D_ABI_Facts");
    if (!Init || !Config || !Start || !Advance) {
        printf("XLBOOT|FAIL|dlsym|a required export is missing\n");
        return 1;
    }
    printf("XLBOOT|brain|%s|abifacts=%d|dump=%d\n", dylib, Facts ? 1 : 0, Dump ? 1 : 0);

    g_stage = "CNC_Init";
    Init("", ev_cb);

    g_stage = "CNC_Config";
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

    g_stage = "CNC_Start_Custom_Instance";
    if (!Start(content, dir, scen, build_level, mp)) {
        printf("XLBOOT|FAIL|start|scen=%s dir=%s content=%s build=%d mp=%d\n",
               scen, dir, content, build_level, (int)mp);
        return 1;
    }
    printf("XLBOOT|started|scen=%s|build=%d|mp=%d\n", scen, build_level, (int)mp);

    g_stage = "advance";
    int done = 0;
    for (int i = 0; i < nticks; i++) {
        g_tick = i;
        if (!Advance(0)) {
            printf("XLBOOT|advance-stopped|tick=%d\n", i);
            break;
        }
        done++;
    }
    printf("XLBOOT|ticks|%d of %d\n", done, nticks);

    g_stage = "dump";
    if (Dump) Dump();

    printf("XLBOOT|end|ticks=%d|events=%d|brainmsgs=%d\n", done, g_events, g_brainMsgs);
    return (done == nticks) ? 0 : 1;
}
