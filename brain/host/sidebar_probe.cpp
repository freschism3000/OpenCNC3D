/* CNC3D: decode GAME_STATE_SIDEBAR / GAME_STATE_PLACEMENT and drive construction.
 *
 * Everything printed here comes from the public DLL API (CNC_Get_Game_State,
 * CNC_Handle_Sidebar_Request) plus the project's art-free CNC3D_Dump_Objects.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <unistd.h>
#include <csignal>
#include <vector>
#include <string>

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
typedef bool (*CNC_Get_State_t)(GameStateRequestEnum, uint64, unsigned char*, unsigned int);
typedef bool (*CNC_Advance_t)(uint64);
typedef int (*CNC3D_Dump_t)(void);
typedef void (*CNC_Sidebar_t)(SidebarRequestEnum, uint64, int, int, short, short);
typedef void (*CNC_Debug_t)(DebugRequestEnum, uint64, const char*, int, int, bool, bool);
typedef void (*CNC_Home_t)(int, int, uint64);

static CNC_Get_State_t GetState;
static CNC_Advance_t Advance;
static CNC3D_Dump_t Dump;
static CNC_Sidebar_t Sidebar;
static CNC_Debug_t Debug;

static void ev_cb(const EventCallbackStruct& e)
{
    if (e.EventType == CALLBACK_EVENT_DEBUG_PRINT) {
        const char* s = e.DebugPrint.PrintString;
        if (s && strstr(s, "CNC_Advance_Instance"))
            return;
        printf("  [dbg] %s\n", s ? s : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_MESSAGE) {
        printf("  [msg] %s\n", e.Message.Message ? e.Message.Message : "(null)");
    }
}

static const char* type_name(DllObjectTypeEnum t)
{
    switch (t) {
    case INFANTRY_TYPE: return "INFANTRY_TYPE";
    case UNIT_TYPE:     return "UNIT_TYPE";
    case AIRCRAFT_TYPE: return "AIRCRAFT_TYPE";
    case BUILDING_TYPE: return "BUILDING_TYPE";
    case SPECIAL:       return "SPECIAL";
    default:            return "UNKNOWN";
    }
}
static const char* rtti_name(int r)
{
    switch (r) {
    case 2:  return "RTTI_INFANTRYTYPE";
    case 4:  return "RTTI_UNITTYPE";
    case 6:  return "RTTI_AIRCRAFTTYPE";
    case 8:  return "RTTI_BUILDINGTYPE";
    case 25: return "RTTI_SPECIAL";
    default: return "RTTI_?";
    }
}

static std::vector<unsigned char> g_sbbuf(1 << 20);

static CNCSidebarStruct* read_sidebar(void)
{
    memset(&g_sbbuf[0], 0, g_sbbuf.size());
    if (!GetState(GAME_STATE_SIDEBAR, 0, &g_sbbuf[0], (unsigned)g_sbbuf.size()))
        return NULL;
    return (CNCSidebarStruct*)&g_sbbuf[0];
}

static void print_sidebar(const char* tag)
{
    CNCSidebarStruct* sb = read_sidebar();
    if (!sb) {
        printf("SIDEBAR[%s] -> CNC_Get_Game_State(GAME_STATE_SIDEBAR) returned FALSE\n", tag);
        return;
    }
    printf("SIDEBAR[%s] cols=%d/%d credits=%d counter=%d tib=%d/%d power=%d/%d "
           "repair=%d sell=%d radar=%d timer=%d kills=%u/%u lost=%u/%u harvested=%u\n",
           tag, sb->EntryCount[0], sb->EntryCount[1], sb->Credits, sb->CreditsCounter,
           sb->Tiberium, sb->MaxTiberium, sb->PowerProduced, sb->PowerDrained,
           (int)sb->RepairBtnEnabled, (int)sb->SellBtnEnabled, (int)sb->RadarMapActive,
           sb->MissionTimer, sb->UnitsKilled, sb->BuildingsKilled, sb->UnitsLost,
           sb->BuildingsLost, sb->TotalHarvestedCredits);
    int n = sb->EntryCount[0] + sb->EntryCount[1];
    for (int i = 0; i < n; i++) {
        const CNCSidebarEntryStruct& e = sb->Entries[i];
        printf("  SBE|%d|col=%d|%-8s|btype=%d(%s)|bid=%d|type=%s|cost=%d|power=%+d|time=%d"
               "|prog=%.3f|done=%d|build=%d|hold=%d|busy=%d|place=%d|cap=%d\n",
               i, (i < sb->EntryCount[0]) ? 0 : 1, e.AssetName, e.BuildableType,
               rtti_name(e.BuildableType), e.BuildableID, type_name(e.Type), e.Cost,
               e.PowerProvided, e.BuildTime, e.Progress, (int)e.Completed,
               (int)e.Constructing, (int)e.ConstructionOnHold, (int)e.Busy,
               e.PlacementListLength, (int)e.BuildableViaCapture);
        if (e.PlacementListLength) {
            printf("      occupy:");
            for (int k = 0; k < e.PlacementListLength; k++)
                printf(" %d", (int)e.PlacementList[k]);
            printf("\n");
        }
    }
}

/* find an entry by asset name; returns index or -1 */
static int find_entry(CNCSidebarStruct* sb, const char* name, int* btype, int* bid)
{
    int n = sb->EntryCount[0] + sb->EntryCount[1];
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(sb->Entries[i].AssetName, name)) {
            *btype = sb->Entries[i].BuildableType;
            *bid = sb->Entries[i].BuildableID;
            return i;
        }
    }
    return -1;
}

static int g_mapX, g_mapY, g_mapW, g_mapH;
static int g_pgX, g_pgY, g_pgW, g_pgH; /* placement grid origin+size in absolute cells */

static std::vector<unsigned char> g_pbuf(4 << 20);

/* returns true and fills the grid, using the exact geometry Get_Placement_State uses */
static CNCPlacementInfoStruct* read_placement(void)
{
    memset(&g_pbuf[0], 0, 64 * 1024);
    if (!GetState(GAME_STATE_PLACEMENT, 0, &g_pbuf[0], (unsigned)g_pbuf.size()))
        return NULL;
    return (CNCPlacementInfoStruct*)&g_pbuf[0];
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    const char* dylib = argv[1];
    const char* dir = argv[2];
    const char* scen = argv[3];
    int build_level = argc > 4 ? atoi(argv[4]) : 1;
    const char* content = argc > 5 ? argv[5] : dir;
    const char* want = argc > 6 ? argv[6] : "NUKE"; /* what to build */
    int warmup = argc > 7 ? atoi(argv[7]) : 5;

    void* h = dlopen(dylib, RTLD_NOW);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); return 1; }
    auto Init = (CNC_Init_t)dlsym(h, "CNC_Init");
    auto Config = (CNC_Config_t)dlsym(h, "CNC_Config");
    auto Start = (CNC_Start_Custom_t)dlsym(h, "CNC_Start_Custom_Instance");
    GetState = (CNC_Get_State_t)dlsym(h, "CNC_Get_Game_State");
    Advance = (CNC_Advance_t)dlsym(h, "CNC_Advance_Instance");
    Dump = (CNC3D_Dump_t)dlsym(h, "CNC3D_Dump_Objects");
    Sidebar = (CNC_Sidebar_t)dlsym(h, "CNC_Handle_Sidebar_Request");
    Debug = (CNC_Debug_t)dlsym(h, "CNC_Handle_Debug_Request");
    printf("symbols: Sidebar=%p Debug=%p\n", (void*)Sidebar, (void*)Debug);

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
    if (!Start(content, dir, scen, build_level, false)) { printf("START FAILED\n"); return 2; }

    static CNCMapDataStruct mapdata;
    memset(&mapdata, 0, sizeof(mapdata));
    GetState(GAME_STATE_STATIC_MAP, 0, (unsigned char*)&mapdata, sizeof(mapdata));
    g_mapX = mapdata.MapCellX; g_mapY = mapdata.MapCellY;
    g_mapW = mapdata.MapCellWidth; g_mapH = mapdata.MapCellHeight;
    printf("MAP origin=%d,%d size=%dx%d\n", g_mapX, g_mapY, g_mapW, g_mapH);
    /* GAME_STATE_STATIC_MAP already applies the same -1/+1 expansion that
       Get_Placement_State does, so the placement grid IS the static-map grid. */
    g_pgX = g_mapX; g_pgY = g_mapY; g_pgW = g_mapW; g_pgH = g_mapH;
    printf("PLACEGRID origin=%d,%d size=%dx%d (cells %d)\n", g_pgX, g_pgY, g_pgW, g_pgH, g_pgW * g_pgH);

    printf("=== tick 0 dump ===\n");
    if (Dump) Dump();
    print_sidebar("t0");

    /* Warm up: the sidebar is rebuilt during HouseClass AI, so it needs a few frames. */
    for (int i = 0; i < warmup; i++) Advance(0);
    printf("=== after %d ticks ===\n", warmup);
    print_sidebar("warm");

    /* Placement state with nothing pending should be refused. */
    printf("PLACEMENT with nothing pending -> %s\n", read_placement() ? "TRUE" : "FALSE (expected)");

    /* ---------------------------------------------------------------- */
    CNCSidebarStruct* sb = read_sidebar();
    if (!sb) { printf("no sidebar, stopping\n"); return 3; }
    int btype = 0, bid = 0;
    int idx = find_entry(sb, want, &btype, &bid);
    if (idx < 0) { printf("RESULT: '%s' is not in the sidebar; nothing to build.\n", want); return 4; }
    printf("TARGET %s -> BuildableType=%d BuildableID=%d cost=%d time=%d\n",
           want, btype, bid, sb->Entries[idx].Cost, sb->Entries[idx].BuildTime);

    /* --- 1. start --- */
    printf("\n>>> SIDEBAR_REQUEST_START_CONSTRUCTION\n");
    Sidebar(SIDEBAR_REQUEST_START_CONSTRUCTION, 0, btype, bid, 0, 0);
    Advance(0);
    print_sidebar("started");

    /* --- 2. hold after a few ticks --- */
    for (int i = 0; i < 20; i++) Advance(0);
    print_sidebar("t+20");
    printf("\n>>> SIDEBAR_REQUEST_HOLD_CONSTRUCTION\n");
    Sidebar(SIDEBAR_REQUEST_HOLD_CONSTRUCTION, 0, btype, bid, 0, 0);
    Advance(0);
    print_sidebar("held");
    for (int i = 0; i < 20; i++) Advance(0);
    print_sidebar("held+20");

    /* --- 3. resume --- */
    printf("\n>>> SIDEBAR_REQUEST_START_CONSTRUCTION (resume from hold)\n");
    Sidebar(SIDEBAR_REQUEST_START_CONSTRUCTION, 0, btype, bid, 0, 0);
    Advance(0);
    print_sidebar("resumed");

    /* --- 4. tick to completion --- */
    int t;
    for (t = 0; t < 6000; t++) {
        Advance(0);
        sb = read_sidebar();
        if (!sb) break;
        int i2 = find_entry(sb, want, &btype, &bid);
        if (i2 >= 0 && sb->Entries[i2].Completed) break;
    }
    printf("\ncompleted after %d extra ticks\n", t);
    print_sidebar("complete");

    /* --- 5. placement mode --- */
    printf("\n>>> SIDEBAR_REQUEST_START_PLACEMENT\n");
    Sidebar(SIDEBAR_REQUEST_START_PLACEMENT, 0, btype, bid, 0, 0);
    CNCPlacementInfoStruct* pi = read_placement();
    if (!pi) {
        printf("RESULT: GAME_STATE_PLACEMENT still FALSE after START_PLACEMENT\n");
        return 5;
    }
    printf("PLACEMENT count=%d (grid %dx%d = %d)\n", pi->Count, g_pgW, g_pgH, g_pgW * g_pgH);
    int nprox = 0, nclear = 0, nboth = 0;
    for (int i = 0; i < pi->Count; i++) {
        if (pi->CellInfo[i].PassesProximityCheck) nprox++;
        if (pi->CellInfo[i].GenerallyClear) nclear++;
        if (pi->CellInfo[i].PassesProximityCheck && pi->CellInfo[i].GenerallyClear) nboth++;
    }
    printf("PLACEMENT prox=%d clear=%d both=%d\n", nprox, nclear, nboth);

    /* ASCII map of the overlay: '#' both, 'p' proximity only, 'c' clear only, '.' neither */
    printf("PLACEMENT-MAP (rows are absolute cell Y from %d, cols absolute cell X from %d)\n",
           g_pgY, g_pgX);
    for (int y = 0; y < g_pgH; y++) {
        printf("  %3d ", g_pgY + y);
        for (int x = 0; x < g_pgW; x++) {
            const CNCPlacementCellInfoStruct& c = pi->CellInfo[y * g_pgW + x];
            putchar(c.PassesProximityCheck ? (c.GenerallyClear ? '#' : 'p')
                                           : (c.GenerallyClear ? 'c' : '.'));
        }
        putchar('\n');
    }

    /* find the occupy list for the pending building */
    sb = read_sidebar();
    int i3 = find_entry(sb, want, &btype, &bid);
    std::vector<short> occ;
    for (int k = 0; k < sb->Entries[i3].PlacementListLength; k++)
        occ.push_back(sb->Entries[i3].PlacementList[k]);
    printf("occupy list length=%d\n", (int)occ.size());

    /* pick the first grid cell where every occupied cell is clear and the origin passes
       the proximity check; this mirrors what the real cursor does. */
    int gx = -1, gy = -1;
    for (int y = 0; y < g_pgH && gy < 0; y++) {
        for (int x = 0; x < g_pgW; x++) {
            const CNCPlacementCellInfoStruct& c = pi->CellInfo[y * g_pgW + x];
            if (!c.PassesProximityCheck) continue;
            bool ok = true;
            int origin_cell = (g_pgY + y) * 128 + (g_pgX + x);
            for (size_t k = 0; k < occ.size(); k++) {
                /* the engine does plain CELL arithmetic: cell + offset */
                int oc = origin_cell + occ[k];
                int ox = oc % 128, oy = oc / 128;
                int ix = ox - g_pgX, iy = oy - g_pgY;
                if (ix < 0 || iy < 0 || ix >= g_pgW || iy >= g_pgH) { ok = false; break; }
                if (!pi->CellInfo[iy * g_pgW + ix].GenerallyClear) { ok = false; break; }
            }
            if (ok) { gx = x; gy = y; break; }
        }
    }
    if (gx < 0) { printf("RESULT: no legal placement cell found\n"); return 6; }
    printf("chosen placement grid cell=%d,%d -> absolute cell x=%d y=%d (engine CELL %d)\n",
           gx, gy, g_pgX + gx, g_pgY + gy, (g_pgY + gy) * 128 + (g_pgX + gx));

    /* --- 6. place --- */
    printf("\n>>> SIDEBAR_REQUEST_PLACE at grid %d,%d\n", gx, gy);
    Sidebar(SIDEBAR_REQUEST_PLACE, 0, btype, bid, (short)gx, (short)gy);
    for (int i = 0; i < 3; i++) Advance(0);
    print_sidebar("placed");
    printf("=== dump after placement ===\n");
    if (Dump) Dump();
    printf("EXPECT a %s owned by the player at cell x=%d y=%d\n", want, g_pgX + gx, g_pgY + gy);
    return 0;
}
