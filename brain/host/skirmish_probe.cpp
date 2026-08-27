/*
 * CNC3D -- skirmish_probe: does the Tiberian Dawn brain actually PLAY a skirmish?
 *
 * The claim under test: CNC_Set_Multiplayer_Data + CNC_Start_Custom_Instance(multiplayer=true)
 * gives us a match in which a house flagged IsAI builds a base with no client help at all.
 * A code read says it should. This program is the run.
 *
 * Modelled on run1.cpp / loadtest.cpp / cnc_host.c, which already solved the compile
 * problems (__declspec, __int64, -fms-extensions -fdeclspec) and already know where the
 * dylib and the content live.
 *
 * Method. CNC3D_Dump_Objects() prints one line per live object straight from the engine's
 * heaps, plus one HOUSE| line per house. It writes to stdout and it is huge, so stdout is
 * redirected to a log file for the whole run and the human-readable report is written to
 * the process's ORIGINAL stdout, which is dup'd before the redirect. A "@@@SAMPLE <tick>"
 * marker is printed into the log before each dump; the log is parsed back at the end and
 * turned into one compact table.
 *
 * Nothing here mutates the sim. It only advances it and counts what is there.
 *
 * Build:
 *   clang++ -std=c++17 -g -O1 -fms-extensions -fdeclspec \
 *       -I brain/vanilla/tiberiandawn -o /tmp/skirmish_probe brain/host/skirmish_probe.cpp
 *
 * Run (SCM01EA is the retail skirmish map, extracted from GENERAL.MIX with menu/tools/mixshp.py;
 * the scenario directory MUST end in a slash, the engine concatenates without one):
 *   cd <a directory holding CONQUER.MIX and content/TEMPERAT.MIX>
 *   /tmp/skirmish_probe <path>/TiberianDawn.dylib /tmp/mp/ SCM01EA content/ /tmp/dump.log 9000 500 7
 *
 * Run it from a SMALL directory. CNC_Init calls Read_Scenario_Descriptions (mplayer.cpp:643),
 * which resolves candidate filenames through Resolve_File -> readdir over the working
 * directory, once per candidate. From playable/, which holds ~350 entries and 1.3 GB of
 * .pack files, that scan can take minutes and looks exactly like a hang.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#include <csignal>

static int g_tick = -1;
static FILE* g_rep = NULL; /* the real stdout, kept aside from the engine's firehose */

static void crash(int sig)
{
    fflush(stdout);
    void* bt[48];
    int n = backtrace(bt, 48);
    fprintf(stderr, "\n*** SIGNAL %d during tick %d ***\n", sig, g_tick);
    backtrace_symbols_fd(bt, n, 2);
    if (g_rep) {
        fprintf(g_rep, "\n*** SIGNAL %d during tick %d ***\n", sig, g_tick);
        fflush(g_rep);
    }
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
typedef bool (*CNC_Set_MP_t)(int, CNCMultiplayerOptionsStruct&, int, CNCPlayerInfoStruct*, int);

/* ------------------------------------------------------------------ events ---- */

static long g_events = 0;
static long g_ev_by_type[24];
static int g_gameover_count = 0;
static int g_gameover_multiplayer = -1;
static int g_gameover_tick = -1;

static void ev_cb(const EventCallbackStruct& e)
{
    g_events++;
    if (e.EventType >= 0 && e.EventType < 24)
        g_ev_by_type[e.EventType]++;

    if (e.EventType == CALLBACK_EVENT_DEBUG_PRINT) {
        const char* s = e.DebugPrint.PrintString;
        if (s && strstr(s, "CNC_Advance_Instance"))
            return; /* spam, and it stops after frame 10 anyway */
        printf("  [dbg] %s\n", s ? s : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_MESSAGE) {
        printf("  [msg] %s\n", e.Message.Message ? e.Message.Message : "(null)");
        if (g_rep)
            fprintf(g_rep, "  [msg @tick %d] %s\n", g_tick, e.Message.Message ? e.Message.Message : "(null)");
    } else if (e.EventType == CALLBACK_EVENT_GAME_OVER) {
        g_gameover_count++;
        if (g_gameover_multiplayer < 0) {
            g_gameover_multiplayer = (int)e.GameOver.Multiplayer;
            g_gameover_tick = g_tick;
        }
        if (g_rep) {
            fprintf(g_rep,
                    "  [GAME_OVER @tick %d] multiplayer=%d human=%d player_wins=%d "
                    "total_players=%d credits=%d\n",
                    g_tick,
                    (int)e.GameOver.Multiplayer,
                    (int)e.GameOver.IsHuman,
                    (int)e.GameOver.PlayerWins,
                    e.GameOver.MultiPlayerTotalPlayers,
                    e.GameOver.RemainingCredits);
            for (int i = 0; i < e.GameOver.MultiPlayerTotalPlayers && i < 8; i++) {
                fprintf(g_rep,
                        "      player[%d] id=%lld human=%d was_human=%d winner=%d "
                        "gathered=%d units_killed=%d structs_killed=%d\n",
                        i,
                        (long long)e.GameOver.MultiPlayerPlayersData[i].GlyphXPlayerID,
                        (int)e.GameOver.MultiPlayerPlayersData[i].IsHuman,
                        (int)e.GameOver.MultiPlayerPlayersData[i].WasHuman,
                        (int)e.GameOver.MultiPlayerPlayersData[i].IsWinner,
                        e.GameOver.MultiPlayerPlayersData[i].ResourcesGathered,
                        e.GameOver.MultiPlayerPlayersData[i].TotalUnitsKilled,
                        e.GameOver.MultiPlayerPlayersData[i].TotalStructuresKilled);
            }
            fflush(g_rep);
        }
    }
}

/* ------------------------------------------------------------------- parse ---- */

#define MAXH 24
#define MAXS 256
#define K_BUILDING 0
#define K_UNIT 1
#define K_INFANTRY 2
#define K_AIRCRAFT 3
#define K_TERRAIN 4
#define NKIND 5

static char g_hname[MAXH][40];
static int g_nh = 0;

static int house_index(const char* n)
{
    for (int i = 0; i < g_nh; i++)
        if (strcmp(g_hname[i], n) == 0)
            return i;
    if (g_nh >= MAXH)
        return -1;
    snprintf(g_hname[g_nh], sizeof(g_hname[0]), "%s", n);
    return g_nh++;
}

struct Sample
{
    int tick;
    int cnt[MAXH][NKIND];
    long strength[MAXH];
    int credits[MAXH];
    int tiberium[MAXH];
    int curunits[MAXH];
    int curbuild[MAXH];
    int human[MAXH];
    int seen_house[MAXH];
    int bullets;
    int anims;
    int total_objs;
};

static Sample g_s[MAXS];
static int g_ns = 0;

/* pull "key=<int>" out of a dump line; returns def when absent */
static int field_int(const char* line, const char* key, int def)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "|%s=", key);
    const char* p = strstr(line, pat);
    if (!p)
        return def;
    return atoi(p + strlen(pat));
}

/* nth '|'-separated token, 0-based, copied into out */
static bool token(const char* line, int n, char* out, size_t outsz)
{
    const char* p = line;
    for (int i = 0; i < n; i++) {
        p = strchr(p, '|');
        if (!p)
            return false;
        p++;
    }
    const char* e = strchr(p, '|');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    while (len && (p[len - 1] == '\n' || p[len - 1] == '\r'))
        len--;
    if (len >= outsz)
        len = outsz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static int kind_index(const char* k)
{
    if (strcmp(k, "BUILDING") == 0)
        return K_BUILDING;
    if (strcmp(k, "UNIT") == 0)
        return K_UNIT;
    if (strcmp(k, "INFANTRY") == 0)
        return K_INFANTRY;
    if (strcmp(k, "AIRCRAFT") == 0)
        return K_AIRCRAFT;
    if (strcmp(k, "TERRAIN") == 0)
        return K_TERRAIN;
    return -1;
}

static void parse_log(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(g_rep, "PARSE: cannot open %s\n", path);
        return;
    }
    char* line = NULL;
    size_t cap = 0;
    ssize_t n;
    int s = -1;
    while ((n = getline(&line, &cap, f)) > 0) {
        if (strncmp(line, "@@@SAMPLE ", 10) == 0) {
            if (g_ns >= MAXS)
                break;
            s = g_ns++;
            memset(&g_s[s], 0, sizeof(g_s[s]));
            g_s[s].tick = atoi(line + 10);
            continue;
        }
        if (s < 0)
            continue;
        if (strncmp(line, "OBJ|", 4) == 0) {
            char kind[32], owner[40];
            if (!token(line, 1, kind, sizeof(kind)))
                continue;
            if (!token(line, 3, owner, sizeof(owner)))
                continue;
            int ki = kind_index(kind);
            int hi = house_index(owner);
            if (ki < 0 || hi < 0)
                continue;
            g_s[s].cnt[hi][ki]++;
            g_s[s].strength[hi] += field_int(line, "str", 0);
            g_s[s].total_objs++;
        } else if (strncmp(line, "HOUSE|", 6) == 0) {
            char owner[40];
            if (!token(line, 1, owner, sizeof(owner)))
                continue;
            int hi = house_index(owner);
            if (hi < 0)
                continue;
            g_s[s].seen_house[hi] = 1;
            g_s[s].credits[hi] = field_int(line, "credits", 0);
            g_s[s].curunits[hi] = field_int(line, "units", 0);
            g_s[s].curbuild[hi] = field_int(line, "buildings", 0);
            g_s[s].tiberium[hi] = field_int(line, "tiberium", 0);
            g_s[s].human[hi] = field_int(line, "human", 0);
        } else if (strncmp(line, "EFX|BULLET", 10) == 0) {
            g_s[s].bullets++;
        } else if (strncmp(line, "EFX|ANIM", 8) == 0) {
            g_s[s].anims++;
        }
    }
    free(line);
    fclose(f);
}

/* ------------------------------------------------------------------- rules ---- */

static void fill_rules(CNCRulesDataStruct& rules)
{
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
}

/* -------------------------------------------------------------------- main ---- */

int main(int argc, char** argv)
{
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s <dylib> <scenario_dir_ending_in_slash> <scenario> <content_dir> "
                "<logfile> [ticks] [interval] [build_level] [mode]\n"
                "  mode: 'hva' (default) player 0 human, player 1 AI\n"
                "        'ava'           both players AI, so no house that can be defeated is\n"
                "                        IsHuman -- the one route through MPlayer_Defeated that\n"
                "                        does not sprintf a string from the (unloaded) text table\n",
                argv[0]);
        return 1;
    }

    const char* dylib = argv[1];
    const char* dir = argv[2];
    const char* scen = argv[3];
    const char* content = argv[4];
    const char* logpath = argv[5];
    int nticks = argc > 6 ? atoi(argv[6]) : 9000;
    int interval = argc > 7 ? atoi(argv[7]) : 500;
    int build_level = argc > 8 ? atoi(argv[8]) : 7;
    bool ai_vs_ai = (argc > 9 && strcmp(argv[9], "ava") == 0);

    signal(SIGSEGV, crash);
    signal(SIGBUS, crash);
    signal(SIGILL, crash);
    signal(SIGABRT, crash);

    /* Keep the real stdout for the report; give the engine a log file. */
    int real1 = dup(1);
    g_rep = fdopen(real1, "w");
    setvbuf(g_rep, NULL, _IOLBF, 0);
    if (!freopen(logpath, "w", stdout)) {
        fprintf(g_rep, "cannot open log %s\n", logpath);
        return 1;
    }

    fprintf(g_rep, "=== CNC3D skirmish probe ===\n");
    fprintf(g_rep, "dylib   : %s\n", dylib);
    fprintf(g_rep, "dir     : %s\n", dir);
    fprintf(g_rep, "scenario: %s   (reads %s%s.INI and %s%s.BIN)\n", scen, dir, scen, dir, scen);
    fprintf(g_rep, "content : %s\n", content);
    fprintf(g_rep, "ticks   : %d  sample every %d  build_level=%d\n", nticks, interval, build_level);
    fprintf(g_rep, "log     : %s\n\n", logpath);

    void* h = dlopen(dylib, RTLD_NOW);
    if (!h) {
        fprintf(g_rep, "dlopen FAILED: %s\n", dlerror());
        return 1;
    }
    CNC_Init_t Init = (CNC_Init_t)dlsym(h, "CNC_Init");
    CNC_Config_t Config = (CNC_Config_t)dlsym(h, "CNC_Config");
    CNC_Set_MP_t SetMP = (CNC_Set_MP_t)dlsym(h, "CNC_Set_Multiplayer_Data");
    CNC_Start_Custom_t Start = (CNC_Start_Custom_t)dlsym(h, "CNC_Start_Custom_Instance");
    CNC_Get_State_t GetState = (CNC_Get_State_t)dlsym(h, "CNC_Get_Game_State");
    CNC_Advance_t Advance = (CNC_Advance_t)dlsym(h, "CNC_Advance_Instance");
    CNC3D_Dump_t Dump = (CNC3D_Dump_t)dlsym(h, "CNC3D_Dump_Objects");

    fprintf(g_rep,
            "symbols: Init=%p Config=%p SetMP=%p Start=%p GetState=%p Advance=%p Dump=%p\n",
            (void*)Init,
            (void*)Config,
            (void*)SetMP,
            (void*)Start,
            (void*)GetState,
            (void*)Advance,
            (void*)Dump);
    if (!Init || !Config || !SetMP || !Start || !Advance || !Dump) {
        fprintf(g_rep, "dlsym FAILED -- a required export is missing.\n");
        return 1;
    }

    fprintf(g_rep, "\n-- CNC_Init(\"\") --\n");
    Init("", ev_cb);

    fprintf(g_rep, "-- CNC_Config(all biases 1.0) --\n");
    CNCRulesDataStruct rules;
    fill_rules(rules);
    Config(rules);

    /* ---------------- two players: one human, one computer ---------------- */
    CNCMultiplayerOptionsStruct opts;
    memset(&opts, 0, sizeof(opts));
    opts.MPlayerCount = 2;
    opts.MPlayerBases = 1;      /* bases ON: every house gets an MCV */
    opts.MPlayerCredits = 5000; /* starting money for everyone */
    opts.MPlayerTiberium = 1;   /* tiberium grows and spreads */
    opts.MPlayerGoodies = 0;    /* no crates */
    opts.MPlayerGhosts = 0;     /* Set_Multiplayer_Data forces this on for any AI anyway */
    opts.MPlayerSolo = 1;
    opts.MPlayerUnitCount = 10;
    opts.IsMCVDeploy = false;
    opts.SpawnVisceroids = false;
    opts.EnableSuperweapons = true;
    opts.MPlayerShadowRegrow = false;
    opts.MPlayerAftermathUnits = false;
    opts.CaptureTheFlag = false;
    opts.DestroyStructures = true;
    opts.ModernBalance = false;

    const int MAXP = 6; /* MAX_PLAYERS, defines.h:2565 */
    CNCPlayerInfoStruct players[6];
    memset(players, 0, sizeof(players));

    snprintf(players[0].Name, sizeof(players[0].Name), ai_vs_ai ? "COMPUTER1" : "HUMAN");
    players[0].House = 0; /* HOUSE_GOOD / GDI */
    players[0].ColorIndex = 0;
    players[0].GlyphxPlayerID = 0;
    players[0].Team = 0;
    players[0].StartLocationIndex = 0;
    players[0].IsAI = ai_vs_ai;
    players[0].IsDefeated = false;

    snprintf(players[1].Name, sizeof(players[1].Name), ai_vs_ai ? "COMPUTER2" : "COMPUTER");
    players[1].House = 1; /* HOUSE_BAD / Nod */
    players[1].ColorIndex = 1;
    players[1].GlyphxPlayerID = 1;
    players[1].Team = 1;
    players[1].StartLocationIndex = 1;
    players[1].IsAI = true;
    players[1].IsDefeated = false;

    fprintf(g_rep,
            "-- CNC_Set_Multiplayer_Data(scenario_index=1, players=2, max=%d) mode=%s --\n"
            "   p0 '%s' house=%u color=%d team=%d start=%d ai=%d glyphxid=%llu\n"
            "   p1 '%s' house=%u color=%d team=%d start=%d ai=%d glyphxid=%llu\n"
            "   bases=%d credits=%d tiberium=%d goodies=%d unitcount=%d destroy_structures=%d "
            "superweapons=%d modern=%d mcvdeploy=%d\n",
            MAXP,
            ai_vs_ai ? "AI-vs-AI" : "human-vs-AI",
            players[0].Name,
            (unsigned)players[0].House,
            players[0].ColorIndex,
            players[0].Team,
            players[0].StartLocationIndex,
            (int)players[0].IsAI,
            (unsigned long long)players[0].GlyphxPlayerID,
            players[1].Name,
            (unsigned)players[1].House,
            players[1].ColorIndex,
            players[1].Team,
            players[1].StartLocationIndex,
            (int)players[1].IsAI,
            (unsigned long long)players[1].GlyphxPlayerID,
            opts.MPlayerBases,
            opts.MPlayerCredits,
            opts.MPlayerTiberium,
            opts.MPlayerGoodies,
            opts.MPlayerUnitCount,
            (int)opts.DestroyStructures,
            (int)opts.EnableSuperweapons,
            (int)opts.ModernBalance,
            (int)opts.IsMCVDeploy);

    bool mpok = SetMP(1, opts, 2, players, MAXP);
    fprintf(g_rep, "   CNC_Set_Multiplayer_Data RETURNED: %s\n", mpok ? "TRUE" : "FALSE");
    if (!mpok) {
        fprintf(g_rep, "   -> refused the setup; nothing further can be tested.\n");
        return 2;
    }

    fprintf(g_rep, "\n-- CNC_Start_Custom_Instance(multiplayer=TRUE) --\n");
    bool ok = Start(content, dir, scen, build_level, true);
    fprintf(g_rep, "   START_RESULT=%s\n", ok ? "TRUE" : "FALSE");
    if (!ok) {
        fprintf(g_rep,
                "   -> Read_Scenario_Ini_File rejected it. Check that '%s%s.INI' exists and\n"
                "      that directory_path ends in a slash (the engine concatenates with none).\n",
                dir,
                scen);
        return 2;
    }

    /*
    ** Is the language string table loaded?
    **
    ** Not idle curiosity. HouseClass::MPlayer_Defeated (house.cpp:4086) does
    **     sprintf(txt, Text_String(TXT_PLAYER_DEFEATED), MPlayerName);
    ** and Text_String is Extract_String(SystemStrings, n) (function.h:940), which returns
    ** nullptr the moment SystemStrings is NULL (dipthong.cpp:313). A NULL format string is
    ** an immediate segfault, and MPlayer_Defeated is the ONLY route to the multiplayer
    ** GAME_OVER callback. So the value of this one pointer decides whether a skirmish can
    ** ever finish. SystemStrings is an unmangled global in the dylib, so just read it.
    */
    {
        char* const* sysstr = (char* const*)dlsym(h, "SystemStrings");
        if (!sysstr) {
            fprintf(g_rep, "   SystemStrings: symbol not exported, cannot check\n");
        } else {
            fprintf(g_rep,
                    "   SystemStrings = %p  -> Text_String() %s; MPlayer_Defeated (house.cpp:4086) "
                    "would sprintf a %s format string\n",
                    (const void*)*sysstr,
                    *sysstr ? "returns real text" : "returns NULL",
                    *sysstr ? "valid" : "NULL");
        }
    }

    /*
    ** The RNG, because the design doc says a skirmish cannot be reproducible.
    **
    ** CNC_Start_Custom_Instance does Seed = timeGetTime() (dllinterface.cpp:1411), but the
    ** only code that copies Seed into the generators is Init_Random (init.cpp:2547-2548),
    ** and its one caller is Select_Game (init.cpp:1354), which the DLL never runs. So print
    ** both and let the numbers say whether the run can drift.
    */
    {
        const unsigned* randnumb = (const unsigned*)dlsym(h, "RandNumb");
        const int* seed = (const int*)dlsym(h, "Seed");
        fprintf(g_rep,
                "   RNG after start: RandNumb=0x%08x (static initialiser is 0x12349876, "
                "irandom.cpp:37)  Seed=%d\n",
                randnumb ? *randnumb : 0u,
                seed ? *seed : 0);
    }

    /* the map, for the record. MapCellX..Theater all sit before ScenarioName, so their
       offsets do not depend on the host's _MAX_FNAME guess. */
    if (GetState) {
        static CNCMapDataStruct mapdata;
        memset(&mapdata, 0, sizeof(mapdata));
        if (GetState(GAME_STATE_STATIC_MAP, 0, (unsigned char*)&mapdata, sizeof(mapdata))) {
            fprintf(g_rep,
                    "   MAP theater=%d cell=%d,%d size=%dx%d original=%d,%d %dx%d\n",
                    (int)mapdata.Theater,
                    mapdata.MapCellX,
                    mapdata.MapCellY,
                    mapdata.MapCellWidth,
                    mapdata.MapCellHeight,
                    mapdata.OriginalMapCellX,
                    mapdata.OriginalMapCellY,
                    mapdata.OriginalMapCellWidth,
                    mapdata.OriginalMapCellHeight);
        }
    }

    /* ------------------------------- the run ------------------------------- */
    fprintf(g_rep, "\n-- advancing %d ticks, CNC3D_Dump_Objects() every %d --\n", nticks, interval);

    int advance_false_at = -1;
    int done = 0;

    g_tick = 0;
    printf("@@@SAMPLE 0\n");
    Dump();
    fflush(stdout);

    for (int i = 0; i < nticks; i++) {
        g_tick = i;
        bool a = Advance(0);
        done = i + 1;
        if (!a) {
            advance_false_at = i;
            fprintf(g_rep, "   CNC_Advance_Instance(0) returned FALSE at tick %d\n", i);
            break;
        }
        if (done % interval == 0) {
            printf("@@@SAMPLE %d\n", done);
            Dump();
            fflush(stdout);
            fprintf(g_rep, "   ... tick %d sampled\n", done);
        }
    }

    if (done % interval != 0) {
        printf("@@@SAMPLE %d\n", done);
        Dump();
        fflush(stdout);
    }
    fflush(stdout);

    fprintf(g_rep, "   TICKS_COMPLETED=%d  advance_returned_false_at=%d\n", done, advance_false_at);

    /* ------------------------------ the table ------------------------------ */
    parse_log(logpath);

    fprintf(g_rep, "\n=== PER-HOUSE TABLE (B=buildings U=units I=infantry A=aircraft) ===\n");
    fprintf(g_rep, "houses seen: ");
    for (int i = 0; i < g_nh; i++)
        fprintf(g_rep, "%s ", g_hname[i]);
    fprintf(g_rep, "\n\n");

    /* only report houses that ever owned something or appeared as a HOUSE| line */
    int show[MAXH];
    int nshow = 0;
    for (int hi = 0; hi < g_nh; hi++) {
        int any = 0;
        for (int s = 0; s < g_ns; s++) {
            if (g_s[s].seen_house[hi])
                any = 1;
            for (int k = 0; k < NKIND; k++)
                if (g_s[s].cnt[hi][k])
                    any = 1;
        }
        if (any)
            show[nshow++] = hi;
    }

    fprintf(g_rep, "%-7s", "tick");
    for (int j = 0; j < nshow; j++)
        fprintf(g_rep, " | %-34s", g_hname[show[j]]);
    fprintf(g_rep, " | %-6s\n", "bullet");
    fprintf(g_rep, "%-7s", "");
    for (int j = 0; j < nshow; j++)
        fprintf(g_rep, " | %-4s %-4s %-4s %-4s %-8s %-4s", "B", "U", "I", "A", "credits", "tib");
    fprintf(g_rep, " |\n");

    for (int s = 0; s < g_ns; s++) {
        fprintf(g_rep, "%-7d", g_s[s].tick);
        for (int j = 0; j < nshow; j++) {
            int hi = show[j];
            fprintf(g_rep,
                    " | %-4d %-4d %-4d %-4d %-8d %-4d",
                    g_s[s].cnt[hi][K_BUILDING],
                    g_s[s].cnt[hi][K_UNIT],
                    g_s[s].cnt[hi][K_INFANTRY],
                    g_s[s].cnt[hi][K_AIRCRAFT],
                    g_s[s].credits[hi],
                    g_s[s].tiberium[hi]);
        }
        fprintf(g_rep, " | %-6d\n", g_s[s].bullets);
    }

    fprintf(g_rep, "\n=== HOUSE DELTAS, first sample -> last sample ===\n");
    if (g_ns >= 2) {
        const Sample& a = g_s[0];
        const Sample& b = g_s[g_ns - 1];
        for (int j = 0; j < nshow; j++) {
            int hi = show[j];
            fprintf(g_rep,
                    "%-10s human=%d  buildings %d -> %d (%+d)   units %d -> %d (%+d)   "
                    "infantry %d -> %d (%+d)   aircraft %d -> %d (%+d)   credits %d -> %d   "
                    "strength %ld -> %ld\n",
                    g_hname[hi],
                    b.human[hi],
                    a.cnt[hi][K_BUILDING],
                    b.cnt[hi][K_BUILDING],
                    b.cnt[hi][K_BUILDING] - a.cnt[hi][K_BUILDING],
                    a.cnt[hi][K_UNIT],
                    b.cnt[hi][K_UNIT],
                    b.cnt[hi][K_UNIT] - a.cnt[hi][K_UNIT],
                    a.cnt[hi][K_INFANTRY],
                    b.cnt[hi][K_INFANTRY],
                    b.cnt[hi][K_INFANTRY] - a.cnt[hi][K_INFANTRY],
                    a.cnt[hi][K_AIRCRAFT],
                    b.cnt[hi][K_AIRCRAFT],
                    b.cnt[hi][K_AIRCRAFT] - a.cnt[hi][K_AIRCRAFT],
                    a.credits[hi],
                    b.credits[hi],
                    a.strength[hi],
                    b.strength[hi]);
        }
    }

    fprintf(g_rep, "\n=== EVENTS ===\n");
    fprintf(g_rep, "total callback events: %ld\n", g_events);
    static const char* evn[14] = {"SOUND_EFFECT",
                                  "SPEECH",
                                  "GAME_OVER",
                                  "DEBUG_PRINT",
                                  "MOVIE",
                                  "MESSAGE",
                                  "UPDATE_MAP_CELL",
                                  "ACHIEVEMENT",
                                  "STORE_CARRYOVER",
                                  "SW_TARGETTING",
                                  "BRIEFING_SCREEN",
                                  "CENTER_CAMERA",
                                  "PING",
                                  "(13)"};
    for (int i = 0; i < 14; i++)
        if (g_ev_by_type[i])
            fprintf(g_rep, "  %-18s %ld\n", evn[i], g_ev_by_type[i]);
    fprintf(g_rep,
            "GAME_OVER fired %d time(s); first at tick %d with Multiplayer=%d\n",
            g_gameover_count,
            g_gameover_tick,
            g_gameover_multiplayer);

    fflush(stdout);
    fprintf(g_rep, "\ndone. full dump log: %s\n", logpath);
    fflush(g_rep);
    return 0;
}
