// CNC3D: headless SCRIPTED INPUT harness.
//
// ===========================================================================================
//  THE INPUT CONTRACT (measured against this brain, not guessed; see CNC3D_Get_View)
// ===========================================================================================
//
//  Coordinate space of CNC_Handle_Input's x1/y1/x2/y2
//  --------------------------------------------------
//  They are SCREEN PIXELS OF THE ENGINE'S OWN TACTICAL VIEW, at 24 pixels per cell.
//  DisplayClass::Pixel_To_Coord (display.cpp:2715) is the only consumer:
//
//      x -= TacPixelX;  x = Pixel_To_Lepton(x);      // Pixel_To_Lepton: pixel*256/24
//      y -= TacPixelY;  y = Pixel_To_Lepton(y);
//      if (IgnoreViewConstraints || ((unsigned)x < (unsigned)TacLeptonWidth &&
//                                    (unsigned)y < (unsigned)TacLeptonHeight))
//          return Coord_Add(TacticalCoord, XY_Coord(x, y));
//      return 0;                                     // click silently discarded
//
//  So:   coord   = TacticalCoord + XY_Coord(Pixel_To_Lepton(x - TacPixelX), ...)
//        pixel_x = Lepton_To_Pixel(cell_x*256 + 128 - Coord_X(TacticalCoord)) + TacPixelX
//
//  In this build the viewport turns out to cover the WHOLE megamap, so every cell is
//  clickable and no scrolling is ever needed:
//        TacPixelX = TacPixelY = 0            (TabClass::One_Time sets Tab_Height = 0
//                                              under REMASTER_BUILD, tab.cpp:261)
//        TacLeptonWidth = TacLeptonHeight = 32768 = 128 cells
//                                             (GBUFF_INIT_WIDTH is 3072 with MEGAMAPS,
//                                              3072 px / 24 = 128 cells, externs.h:59)
//        TacticalCoord  = XY_Coord(MapCellX*256, MapCellY*256)
//                                             (SCB01EA: cell 21,14 -> 5376,3584)
//  TacticalCoord cannot be moved from outside: DisplayClass::Set_Tactical_Position
//  (display.cpp:4093) throws its argument away under REMASTER_BUILD and always recomputes
//  the same top-left corner, and it only writes TacticalCoord while ScenarioInit is set.
//  CNC_Set_Home_Cell does NOT move it either; it only writes Scen.Views[0].
//
//  Selection
//  ---------
//  Two independent routes, both proven here:
//    * CNC_Select_Object(player_id, DllObjectTypeEnum, heap_id). heap_id is the object's
//      slot in its own heap, i.e. exactly the "id=" field CNC3D_Dump_Objects prints.
//      It refuses anything whose House != PlayerPtr, and returns false in that case.
//    * CNC_Handle_Input(INPUT_REQUEST_SELECT_AT_POSITION | MOUSE_LEFT_CLICK, pixels)
//      and INPUT_REQUEST_MOUSE_AREA (x1,y1,x2,y2 = drag rectangle in the same pixels).
//
//  Orders
//  ------
//  INPUT_REQUEST_COMMAND_AT_POSITION runs DisplayClass::TacticalClass::Command_Object,
//  which asks Best_Object_Action() what the selection would do at that spot and then
//  routes it through Mouse_Left_Release -> Active_Click_With -> Player_Assign_Mission.
//  There is no separate "move" or "attack" entry point: the action is inferred, and the
//  modifier keys (INPUT_REQUEST_SPECIAL_KEYS, 1=CTRL 2=ALT 4=SHIFT) steer the inference,
//  per TechnoClass::What_Action (techno.cpp:2571 and :2696):
//        empty cell, no modifier ......... ACTION_MOVE (1)      -> MISSION_MOVE
//        enemy object, no modifier ....... ACTION_ATTACK (5)    -> MISSION_ATTACK
//        CTRL ............................ force fire: ACTION_ATTACK even on empty ground
//        ALT ............................. force move: ACTION_MOVE even onto an enemy
//        CTRL+ALT ........................ ACTION_GUARD_AREA (19) -> MISSION_GUARD_AREA
//        SHIFT over an object ............ ACTION_TOGGLE_SELECT (add to selection)
//  Orders are queued as events; they take effect at the NEXT CNC_Advance_Instance, not
//  during the CNC_Handle_Input call. Guard/stop for the whole selection go through
//  CNC_Handle_Unit_Request (4 = GUARD_MODE, 5 = STOP), no coordinates involved.
//

// Starts a real mission in the TD brain, then runs a semicolon separated script of
// input commands against it, so gameplay can be verified with nobody at the keyboard.
//
//   ./inputtest <dylib> <missiondir/> <SCEN> <buildlevel> <contentdir/> "cmd; cmd; cmd"
//
// Commands:
//   view                  print the engine's tactical viewport (the input coordinate space)
//   dump                  full object dump
//   sel                   dump the current selection (with NavCom / TarCom)
//   select <type> <id>    CNC_Select_Object; type is inf|unit|air|bldg, id is the heap id
//   clearsel              CNC_Clear_Object_Selection
//   keys <n>              CNC_Handle_Input(INPUT_REQUEST_SPECIAL_KEYS, n): 1=CTRL 2=ALT 4=SHIFT
//   probe <cx> <cy>       ask the engine what is at that cell and what the selection would do
//   click <cx> <cy>       INPUT_REQUEST_SELECT_AT_POSITION at the centre of that cell
//   cmd <cx> <cy>         INPUT_REQUEST_COMMAND_AT_POSITION at the centre of that cell
//   lclick <cx> <cy>      INPUT_REQUEST_MOUSE_LEFT_CLICK  at the centre of that cell
//   rclick <cx> <cy>      INPUT_REQUEST_MOUSE_RIGHT_CLICK at the centre of that cell
//   band <x1> <y1> <x2> <y2>   INPUT_REQUEST_MOUSE_AREA drag select, in CELLS
//   unitreq <n>           CNC_Handle_Unit_Request (4=GUARD_MODE, 5=STOP, ...)
//   tick <n>              advance n frames
//   trace <type> <id> <n> advance n frames, printing that object's cell whenever it changes
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
#define MEGAMAPS      1
#define _MAX_FNAME    256
#define _MAX_EXT      256
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
typedef void (*CNC_Handle_Input_t)(InputRequestEnum, unsigned char, uint64, int, int, int, int);
typedef void (*CNC_Handle_Unit_Request_t)(UnitRequestEnum, uint64);
typedef bool (*CNC_Select_Object_t)(uint64, int, int);
typedef bool (*CNC_Clear_Object_Selection_t)(uint64);
typedef void (*CNC_Set_Home_Cell_t)(int, int, uint64);
typedef int (*CNC3D_Dump_t)(void);
typedef void (*CNC3D_Get_View_t)(int*);
typedef bool (*CNC3D_Cell_To_Pixel_t)(int, int, int*, int*);
typedef int (*CNC3D_Dump_Selection_t)(void);
typedef int (*CNC3D_Probe_t)(int, int);
typedef bool (*CNC3D_Sel_Info_t)(int, int*);
typedef const char* (*CNC3D_Mission_Name_t)(int);

static CNC_Advance_t Advance;
static CNC_Handle_Input_t HandleInput;
static CNC_Handle_Unit_Request_t HandleUnitRequest;
static CNC_Select_Object_t SelectObject;
static CNC_Clear_Object_Selection_t ClearSelection;
static CNC3D_Dump_t Dump;
static CNC3D_Get_View_t GetView;
static CNC3D_Cell_To_Pixel_t CellToPixel;
static CNC3D_Dump_Selection_t DumpSel;
static CNC3D_Probe_t Probe;
static CNC3D_Sel_Info_t SelInfo;
static CNC3D_Mission_Name_t MissionName;

static int g_events = 0;
static int g_frames = 0;

static void ev_cb(const EventCallbackStruct& e)
{
    g_events++;
    if (e.EventType == CALLBACK_EVENT_DEBUG_PRINT) {
        const char* s = e.DebugPrint.PrintString;
        if (s && strstr(s, "CNC_Advance_Instance"))
            return;
        printf("  [dbg] %s\n", s ? s : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_MESSAGE) {
        printf("  [msg] %s\n", e.Message.Message ? e.Message.Message : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_GAME_OVER) {
        printf("  [GAME OVER]\n");
    }
}

static void print_view(void)
{
    if (!GetView) {
        printf("VIEW| (CNC3D_Get_View not exported)\n");
        return;
    }
    int v[11];
    memset(v, 0, sizeof(v));
    GetView(v);
    printf("VIEW|TacPixelX=%d|TacPixelY=%d|TacLeptonW=%d|TacLeptonH=%d"
           "|TacCoordX=%d|TacCoordY=%d|(cell %d,%d)|MapCell=%d,%d %dx%d|LegacyRender=%d\n",
           v[0],
           v[1],
           v[2],
           v[3],
           v[4],
           v[5],
           v[4] / 256,
           v[5] / 256,
           v[6],
           v[7],
           v[8],
           v[9],
           v[10]);
}

/* ---- object state readback straight from the dump, for the trace command ---- */
struct ObjState
{
    int cell, cx, cy, lx, ly;
    char mission[32];
    bool found;
};

/* We cannot read the heaps directly from here, so trace uses GAME_STATE_OCCUPIER-free
** path: we simply re-run CNC3D_Dump_Objects and grep our own stdout. Instead of that
** hack, the harness prints cells by asking the dylib for the selection dump, which
** carries cell + navcom for exactly the objects we ordered. */

static bool cell_pixels(int cx, int cy, int* px, int* py)
{
    if (!CellToPixel) {
        /* fall back to the documented mapping using the view block */
        int v[11];
        GetView(v);
        *px = ((cx * 256 + 128 - v[4]) * 24 + 128) / 256 + v[0];
        *py = ((cy * 256 + 128 - v[5]) * 24 + 128) / 256 + v[1];
        return true;
    }
    return CellToPixel(cx, cy, px, py);
}

static void do_tick(int n)
{
    for (int i = 0; i < n; i++) {
        g_tick = g_frames;
        if (!Advance(0)) {
            printf("ADVANCE returned false at frame %d\n", g_frames);
            return;
        }
        g_frames++;
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGSEGV, crash);
    signal(SIGBUS, crash);
    signal(SIGILL, crash);
    signal(SIGABRT, crash);

    if (argc < 7) {
        printf("usage: %s <dylib> <dir/> <scen> <build> <content/> \"script\"\n", argv[0]);
        return 1;
    }
    const char* dylib = argv[1];
    const char* dir = argv[2];
    const char* scen = argv[3];
    int build_level = atoi(argv[4]);
    const char* content = argv[5];
    const char* script = argv[6];

    void* h = dlopen(dylib, RTLD_NOW);
    if (!h) {
        printf("dlopen FAILED: %s\n", dlerror());
        return 1;
    }
    auto Init = (CNC_Init_t)dlsym(h, "CNC_Init");
    auto Config = (CNC_Config_t)dlsym(h, "CNC_Config");
    auto Start = (CNC_Start_Custom_t)dlsym(h, "CNC_Start_Custom_Instance");
    Advance = (CNC_Advance_t)dlsym(h, "CNC_Advance_Instance");
    HandleInput = (CNC_Handle_Input_t)dlsym(h, "CNC_Handle_Input");
    HandleUnitRequest = (CNC_Handle_Unit_Request_t)dlsym(h, "CNC_Handle_Unit_Request");
    SelectObject = (CNC_Select_Object_t)dlsym(h, "CNC_Select_Object");
    ClearSelection = (CNC_Clear_Object_Selection_t)dlsym(h, "CNC_Clear_Object_Selection");
    Dump = (CNC3D_Dump_t)dlsym(h, "CNC3D_Dump_Objects");
    GetView = (CNC3D_Get_View_t)dlsym(h, "CNC3D_Get_View");
    CellToPixel = (CNC3D_Cell_To_Pixel_t)dlsym(h, "CNC3D_Cell_To_Pixel");
    DumpSel = (CNC3D_Dump_Selection_t)dlsym(h, "CNC3D_Dump_Selection");
    Probe = (CNC3D_Probe_t)dlsym(h, "CNC3D_Probe_Object_At");
    SelInfo = (CNC3D_Sel_Info_t)dlsym(h, "CNC3D_Sel_Info");
    MissionName = (CNC3D_Mission_Name_t)dlsym(h, "CNC3D_Mission_Name");

    printf("symbols: HandleInput=%p Select=%p GetView=%p CellToPixel=%p DumpSel=%p Probe=%p\n",
           (void*)HandleInput,
           (void*)SelectObject,
           (void*)GetView,
           (void*)CellToPixel,
           (void*)DumpSel,
           (void*)Probe);
    if (!Init || !Config || !Start || !Advance || !HandleInput || !SelectObject) {
        printf("dlsym FAILED\n");
        return 1;
    }

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

    bool ok = Start(content, dir, scen, build_level, false);
    printf("START_RESULT=%s\n", ok ? "TRUE" : "FALSE");
    if (!ok)
        return 2;

    /* ------------------------------------------------------------------ */
    char* buf = strdup(script);
    char* saveptr = NULL;
    for (char* line = strtok_r(buf, ";", &saveptr); line; line = strtok_r(NULL, ";", &saveptr)) {
        while (*line == ' ' || *line == '\n' || *line == '\t')
            line++;
        if (!*line)
            continue;

        char verb[32] = {0};
        char a1[32] = {0};
        int n = sscanf(line, "%31s %31s", verb, a1);
        (void)n;
        printf("--- CMD: %s\n", line);

        if (!strcmp(verb, "view")) {
            print_view();

        } else if (!strcmp(verb, "dump")) {
            if (Dump)
                Dump();

        } else if (!strcmp(verb, "sel")) {
            if (DumpSel)
                DumpSel();

        } else if (!strcmp(verb, "select")) {
            char type[16];
            int id;
            if (sscanf(line, "%*s %15s %d", type, &id) == 2) {
                int t = 0;
                if (!strcmp(type, "inf"))
                    t = INFANTRY;
                else if (!strcmp(type, "unit"))
                    t = UNIT;
                else if (!strcmp(type, "air"))
                    t = AIRCRAFT;
                else if (!strcmp(type, "bldg"))
                    t = BUILDING;
                bool r = SelectObject(0, t, id);
                printf("CNC_Select_Object(player=0, type=%d, id=%d) -> %s\n", t, id, r ? "true" : "false");
            }

        } else if (!strcmp(verb, "clearsel")) {
            bool r = ClearSelection ? ClearSelection(0) : false;
            printf("CNC_Clear_Object_Selection -> %s\n", r ? "true" : "false");

        } else if (!strcmp(verb, "keys")) {
            int flags = atoi(a1);
            HandleInput(INPUT_REQUEST_SPECIAL_KEYS, (unsigned char)flags, 0, 0, 0, 0, 0);
            printf("special key flags = %d (ctrl=%d alt=%d shift=%d)\n",
                   flags,
                   (flags & 1) != 0,
                   (flags & 2) != 0,
                   (flags & 4) != 0);

        } else if (!strcmp(verb, "probe") || !strcmp(verb, "click") || !strcmp(verb, "cmd")
                   || !strcmp(verb, "lclick") || !strcmp(verb, "rclick")) {
            int cx, cy;
            if (sscanf(line, "%*s %d %d", &cx, &cy) != 2) {
                printf("bad args\n");
                continue;
            }
            int px = 0, py = 0;
            bool inview = cell_pixels(cx, cy, &px, &py);
            printf("cell (%d,%d) -> input pixels (%d,%d) inview=%d\n", cx, cy, px, py, (int)inview);
            if (!strcmp(verb, "probe")) {
                if (Probe)
                    Probe(px, py);
            } else if (!strcmp(verb, "click")) {
                HandleInput(INPUT_REQUEST_SELECT_AT_POSITION, 0, 0, px, py, 0, 0);
            } else if (!strcmp(verb, "cmd")) {
                HandleInput(INPUT_REQUEST_COMMAND_AT_POSITION, 0, 0, px, py, 0, 0);
            } else if (!strcmp(verb, "lclick")) {
                HandleInput(INPUT_REQUEST_MOUSE_LEFT_CLICK, 0, 0, px, py, 0, 0);
            } else {
                HandleInput(INPUT_REQUEST_MOUSE_RIGHT_CLICK, 0, 0, px, py, 0, 0);
            }

        } else if (!strcmp(verb, "band")) {
            int x1, y1, x2, y2;
            if (sscanf(line, "%*s %d %d %d %d", &x1, &y1, &x2, &y2) != 4) {
                printf("bad args\n");
                continue;
            }
            int px1, py1, px2, py2;
            cell_pixels(x1, y1, &px1, &py1);
            cell_pixels(x2, y2, &px2, &py2);
            printf("band cells (%d,%d)-(%d,%d) -> pixels (%d,%d)-(%d,%d)\n", x1, y1, x2, y2, px1, py1, px2, py2);
            HandleInput(INPUT_REQUEST_MOUSE_AREA, 0, 0, px1, py1, px2, py2);

        } else if (!strcmp(verb, "unitreq")) {
            int r = atoi(a1);
            if (HandleUnitRequest)
                HandleUnitRequest((UnitRequestEnum)r, 0);
            printf("CNC_Handle_Unit_Request(%d)\n", r);

        } else if (!strcmp(verb, "tick")) {
            int nt = atoi(a1);
            do_tick(nt);
            printf("frame now %d\n", g_frames);

        } else if (!strcmp(verb, "trace")) {
            /* advance N frames, printing the selection (cell + navcom) every 'every' frames */
            int nt = 0, every = 10;
            sscanf(line, "%*s %d %d", &nt, &every);
            if (every <= 0)
                every = 10;
            for (int i = 0; i < nt; i++) {
                do_tick(1);
                if ((i % every) == 0 || i == nt - 1) {
                    printf("[frame %d] ", g_frames);
                    if (DumpSel)
                        DumpSel();
                }
            }

        } else if (!strcmp(verb, "path")) {
            /* advance up to N frames, printing selected object 0's cell only when it CHANGES.
            ** Stops early once it is idle (no NavCom) and has been still for a while. */
            int nt = atoi(a1);
            int prev[8];
            memset(prev, -1, sizeof(prev));
            int still = 0;
            int steps = 0;
            if (!SelInfo) {
                printf("CNC3D_Sel_Info missing\n");
                continue;
            }
            int cur[8];
            if (SelInfo(0, cur)) {
                printf("PATH| frame=%d cell=%d (%d,%d) lep=%d,%d mission=%s nav=%d tar=%d\n",
                       g_frames,
                       cur[0],
                       cur[1],
                       cur[2],
                       cur[3],
                       cur[4],
                       MissionName ? MissionName(cur[5]) : "?",
                       cur[6],
                       cur[7]);
                memcpy(prev, cur, sizeof(cur));
            }
            for (int i = 0; i < nt; i++) {
                do_tick(1);
                if (!SelInfo(0, cur))
                    break;
                bool changed = (cur[0] != prev[0]) || (cur[5] != prev[5]) || (cur[6] != prev[6]);
                if (changed) {
                    printf("PATH| frame=%d cell=%d (%d,%d) lep=%d,%d mission=%s nav=%d tar=%d\n",
                           g_frames,
                           cur[0],
                           cur[1],
                           cur[2],
                           cur[3],
                           cur[4],
                           MissionName ? MissionName(cur[5]) : "?",
                           cur[6],
                           cur[7]);
                    if (cur[0] != prev[0])
                        steps++;
                    memcpy(prev, cur, sizeof(cur));
                    still = 0;
                } else {
                    still++;
                }
                if (cur[6] == -1 && still > 60)
                    break;
            }
            printf("PATH-END| frames=%d cells_entered=%d final=(%d,%d)\n", g_frames, steps, cur[1], cur[2]);

        } else {
            printf("unknown command '%s'\n", verb);
        }
    }
    free(buf);

    printf("== done, frames=%d events=%d\n", g_frames, g_events);
    return 0;
}
