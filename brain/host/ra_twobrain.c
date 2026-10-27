/*
 * ra_twobrain.c -- TWO Red Alert brains, ONE process, in lockstep.
 *
 * The Red Alert counterpart of cnc_twobrain.c, and the gate for the property a Red Alert network
 * match is built on: two INDEPENDENT copies of the engine, advanced side by side from the same
 * match setup with the same orders, must compute the same world on every tick. One binary run
 * twice proves the engine repeats; it cannot prove that two copies agree.
 *
 * So the brain is loaded twice, as two byte copies at two paths (dyld keys an image on its path;
 * a symlink or the same path twice would be ONE image and every comparison would pass for free).
 * The isolation is asserted before a single tick: the lockstep flag is set through A and B must
 * still read it false.
 *
 * THE TWO INSTANCES ARE ADVANCED UNDER DIFFERENT LOCAL PLAYERS, deliberately. CNC_Advance_Instance
 * takes a player id that selects the engine's local context (PlayerPtr) for the call, and every
 * machine of a real match has a different local player. Anything in the simulation that reads that
 * context makes two peers diverge, and this is where it shows.
 *
 * The match is a real one: CNC_Set_Multiplayer_Data with two human seats and computer seats,
 * started through CNC_Start_Custom_Instance on a 1996 multiplayer map, so the computer players'
 * expert system builds, trains and attacks while the two worlds are compared. Then the order legs:
 * an order made on A is drained off it, posted to both and must change both worlds alike, the same
 * order given to A alone must make them differ (or the comparison cannot see it), and an order is
 * accepted when posted on time and refused when posted after its frame.
 *
 *   usage: ra_twobrain <RedAlert.dylib> <scenario_dir> <scenario> [ticks] [ai_seats]
 *
 * <scenario_dir> holds the map's .INI (or .mpr) and RULES.INI; it is also the content directory.
 * The run is headless and needs no art. Exit 0 on agreement, non-zero on any divergence.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
typedef HMODULE cnc_lib_t;
#define CNC_CALL __cdecl
#define cnc_lib_open(p) LoadLibraryA(p)
#define cnc_lib_sym(h, n) ((void*)GetProcAddress((h), (n)))
#define cnc_lib_err() "LoadLibrary failed"
#define RB_COPY_A "opencnc3d_ra_twobrain_A"
#define RB_COPY_B "opencnc3d_ra_twobrain_B"
#define RB_COPY_CMD "copy /Y \"%s\" \"%s\" >nul"
#define dup _dup
#define dup2 _dup2
#define close _close
#define lseek _lseek
#define read _read
#define fileno _fileno
#else
#include <unistd.h>
#include <dlfcn.h>
typedef void* cnc_lib_t;
#define CNC_CALL
#define cnc_lib_open(p) dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define cnc_lib_sym(h, n) dlsym((h), (n))
#define cnc_lib_err() dlerror()
#define RB_COPY_A "/tmp/opencnc3d_ra_twobrain_A"
#define RB_COPY_B "/tmp/opencnc3d_ra_twobrain_B"
#define RB_COPY_CMD "cp '%s' '%s'"
#endif

/* ------------------------------------------------------------------ ABI mirrors ---- */
/* Byte-exact mirrors of redalert/dllinterface.h, packed to 1 as the brain declares them. */
#define RB_MAX_HOUSES 32
#define RB_MAX_EXPORT_CELLS (128 * 128)
#pragma pack(push, 1)
typedef struct
{
    int Power;
    int Drain;
    int Money;
} RBSpiedInfo;

typedef struct
{
    char Name[64];
    unsigned char House;
    int ColorIndex;
    unsigned long long GlyphxPlayerID;
    int Team;
    int StartLocationIndex;
    unsigned char HomeCellX;
    unsigned char HomeCellY;
    unsigned char IsAI;
    unsigned int AllyFlags;
    unsigned char IsDefeated;
    unsigned int SpiedPowerFlags;
    unsigned int SpiedMoneyFlags;
    RBSpiedInfo SpiedInfo[RB_MAX_HOUSES];
    int SelectedID;
    int SelectedType;
    unsigned char ActionWithSelected[RB_MAX_EXPORT_CELLS];
    unsigned int ActionWithSelectedCount;
    unsigned int ScreenShake;
    unsigned char IsRadarJammed;
} RBPlayerInfo;

typedef struct
{
    int MPlayerCount;
    int MPlayerBases;
    int MPlayerCredits;
    int MPlayerTiberium;
    int MPlayerGoodies;
    int MPlayerGhosts;
    int MPlayerSolo;
    int MPlayerUnitCount;
    unsigned char IsMCVDeploy;
    unsigned char SpawnVisceroids;
    unsigned char EnableSuperweapons;
    unsigned char MPlayerShadowRegrow;
    unsigned char MPlayerAftermathUnits;
    unsigned char CaptureTheFlag;
    unsigned char DestroyStructures;
    unsigned char ShortGame;
    unsigned char ModernBalance;
} RBOptions;
#pragma pack(pop)

_Static_assert(sizeof(RBPlayerInfo) == 16886, "CNCPlayerInfoStruct drift: re-mirror redalert/dllinterface.h");

#define RB_EVENT_MAX 64
/* Red Alert's own RTTIType numbering (redalert/defines.h), which is not Tiberian Dawn's. */
#define RB_RTTI_UNIT 28
#define RB_RTTI_INFANTRY 13
#define RB_RANDOM_START 0x7f

/* Red Alert's countries (HousesType) and the GlyphX ids this harness gives its seats. */
#define RB_HOUSE_GREECE 1
#define RB_HOUSE_USSR 2
#define RB_HOUSE_ENGLAND 3
#define RB_HOUSE_UKRAINE 4
#define RB_HOUSE_GERMANY 5
#define RB_HOUSE_FRANCE 6

typedef void(CNC_CALL* cb_t)(const void* event);
typedef void(CNC_CALL* fn_Init)(const char*, cb_t);
typedef void(CNC_CALL* fn_AddModPath)(const char*);
typedef unsigned char(CNC_CALL* fn_Start)(const char*, const char*, const char*, int, unsigned char);
typedef unsigned char(CNC_CALL* fn_Advance)(unsigned long long);
typedef int(CNC_CALL* fn_Dump)(void);
typedef void(CNC_CALL* fn_SetLockstep)(unsigned char);
typedef unsigned char(CNC_CALL* fn_GetLockstep)(void);
typedef int(CNC_CALL* fn_EventABI)(unsigned int*, int);
typedef void(CNC_CALL* fn_SetAILevels)(const unsigned char*, int);
typedef unsigned(CNC_CALL* fn_AIHash)(void);
typedef unsigned char(CNC_CALL* fn_SetMP)(int, RBOptions*, int, RBPlayerInfo*, int);
typedef int(CNC_CALL* fn_Drain)(void*, int, int);
typedef int(CNC_CALL* fn_Post)(const void*);
typedef void(CNC_CALL* fn_Beacon)(int, unsigned long long, int, int);
typedef void(CNC_CALL* fn_SetSeed)(int);
typedef unsigned char(CNC_CALL* fn_ToAI)(unsigned long long);
typedef int(CNC_CALL* fn_IsAI)(unsigned long long);
typedef unsigned char(CNC_CALL* fn_Hurt)(int, int, int);
typedef unsigned char(CNC_CALL* fn_Blowup)(unsigned long long);
typedef unsigned char(CNC_CALL* fn_SetFlag)(unsigned long long, unsigned char);
typedef unsigned char(CNC_CALL* fn_GetFlag)(unsigned long long);
typedef unsigned char(CNC_CALL* fn_ProxOk)(unsigned long long, const char*, int, int);
typedef int(CNC_CALL* fn_Potential)(unsigned long long, void*, int);
typedef void(CNC_CALL* fn_SetWall)(int);
typedef int(CNC_CALL* fn_GetWall)(void);
typedef unsigned char(CNC_CALL* fn_SetRally)(unsigned long long, int, int, int);
typedef unsigned char(CNC_CALL* fn_GetRally)(int, int*, int*);
typedef unsigned char(CNC_CALL* fn_TrigInfo)(const char*, int*, int*, int*, int*);
typedef void(CNC_CALL* fn_DebugReq)(int, unsigned long long, const char*, int, int, unsigned char, unsigned char);
typedef void(CNC_CALL* fn_Structure)(int, unsigned long long, int);
typedef void(CNC_CALL* fn_Sidebar)(int, unsigned long long, int, int, short, short);
typedef unsigned char(CNC_CALL* fn_GetState)(int, unsigned long long, unsigned char*, unsigned int);

typedef struct
{
    const char* name;
    cnc_lib_t lib;
    fn_Init Init;
    fn_AddModPath AddModPath;
    fn_Start Start;
    fn_Advance Advance;
    fn_Dump Dump;
    fn_SetLockstep SetLockstep;
    fn_GetLockstep GetLockstep;
    fn_EventABI EventABI;
    fn_SetAILevels SetAILevels;
    fn_AIHash AIPolicy;
    fn_AIHash AIState;
    fn_SetMP SetMP;
    fn_Drain Drain;
    fn_Post Post;
    fn_Beacon Beacon;
    fn_SetSeed SetSeed;
    fn_ToAI ToAI;
    fn_IsAI IsAI;
    fn_Hurt Hurt;
    fn_Blowup Blowup;
    fn_SetFlag SetInvincible;
    fn_GetFlag GetInvincible;
    fn_SetFlag SetBuildAnywhere;
    fn_GetFlag GetBuildAnywhere;
    fn_SetFlag GrantSupers;
    fn_ProxOk ProximityOk;
    fn_Potential Potential;
    fn_SetWall SetWallFill;
    fn_GetWall GetWallFill;
    fn_SetRally SetRally;
    fn_GetRally GetRally;
    fn_TrigInfo TriggerInfo;
    fn_DebugReq DebugReq;
    fn_Structure Structure;
    fn_Sidebar Sidebar;
    fn_GetState GetState;
} Brain;

static void CNC_CALL noop_callback(const void* event)
{
    (void)event;
}

/* ------------------------------------------------------------------- plumbing ------ */

/* The fields that name the CONTEXT that dumped rather than the world: HOUSE|...|player=1 marks
   the house the caller's id resolved to, and an OBJ line's flashme= says whether a target blush
   is that house's to see. The instances run under different ids on purpose, so both differ by
   design and are blanked; the renderer's lockstep world hash leaves out the same fields. */
static void blank_field(char* d, const char* key)
{
    char* p = d;
    const size_t n = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        p += n;
        while (*p >= '0' && *p <= '9') {
            *p++ = '?';
        }
    }
}

static char* capture_dump(Brain* b, size_t* len_out)
{
    FILE* tmp = tmpfile();
    if (tmp == NULL) {
        return (NULL);
    }
    int tmpfd = fileno(tmp);
    int saved = dup(1);

    fflush(NULL);
    dup2(tmpfd, 1);
    b->Dump();
    printf("FAIRAI_STATE|policy=%08x|state=%08x\n", b->AIPolicy(), b->AIState());
    fflush(NULL);
    dup2(saved, 1);
    close(saved);

    long size = lseek(tmpfd, 0, SEEK_END);
    if (size < 0) {
        fclose(tmp);
        return (NULL);
    }
    lseek(tmpfd, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(tmp);
        return (NULL);
    }
    long got = (long)read(tmpfd, buf, (unsigned)size);
    buf[got < 0 ? 0 : got] = '\0';
    fclose(tmp);
    blank_field(buf, "|player=");
    blank_field(buf, "|flashme=");
    *len_out = (size_t)(got < 0 ? 0 : got);
    return (buf);
}

static void report_first_difference(const char* a, const char* b)
{
    const char *la = a, *lb = b;
    int line = 1;
    while (*la && *lb) {
        const char* ea = strchr(la, '\n');
        const char* eb = strchr(lb, '\n');
        size_t na = ea ? (size_t)(ea - la) : strlen(la);
        size_t nb = eb ? (size_t)(eb - lb) : strlen(lb);
        if (na != nb || memcmp(la, lb, na) != 0) {
            size_t col = 0;
            size_t lim = na < nb ? na : nb;
            size_t fa, fb;
            const char *ea2, *eb2;
            while (col < lim && la[col] == lb[col]) {
                col++;
            }
            printf("  first difference is on dump line %d, at column %lu\n", line, (unsigned long)(col + 1));
            printf("    A: %.*s\n", (int)na, la);
            printf("    B: %.*s\n", (int)nb, lb);
            fa = col;
            fb = col;
            while (fa > 0 && la[fa - 1] != '|') {
                fa--;
            }
            while (fb > 0 && lb[fb - 1] != '|') {
                fb--;
            }
            ea2 = (const char*)memchr(la + fa, '|', na - fa);
            eb2 = (const char*)memchr(lb + fb, '|', nb - fb);
            printf("    the field that differs: A %.*s / B %.*s\n",
                   (int)(ea2 ? (size_t)(ea2 - (la + fa)) : na - fa),
                   la + fa,
                   (int)(eb2 ? (size_t)(eb2 - (lb + fb)) : nb - fb),
                   lb + fb);
            return;
        }
        if (!ea || !eb) {
            break;
        }
        la = ea + 1;
        lb = eb + 1;
        line++;
    }
    printf("  the dumps differ in length: A %lu bytes, B %lu bytes\n", (unsigned long)strlen(a), (unsigned long)strlen(b));
}

static int load_brain(Brain* b, const char* src, const char* copy_stem, const char* name)
{
    /* The copy is named per process, so several gates can run side by side. */
    char copy_path[1024];
    char cmd[2400];
#ifdef _WIN32
    snprintf(copy_path, sizeof(copy_path), "%s_%lu.dll", copy_stem, (unsigned long)GetCurrentProcessId());
#else
    snprintf(copy_path, sizeof(copy_path), "%s_%ld.dylib", copy_stem, (long)getpid());
#endif
    snprintf(cmd, sizeof(cmd), RB_COPY_CMD, src, copy_path);
    if (system(cmd) != 0) {
        printf("could not copy %s -> %s\n", src, copy_path);
        return (0);
    }
    b->name = name;
    b->lib = cnc_lib_open(copy_path);
    if (b->lib == NULL) {
        printf("loading %s failed: %s\n", copy_path, cnc_lib_err());
        return (0);
    }
#ifndef _WIN32
    /* A loaded image stays mapped after its file is removed, so the copy need not outlive this. */
    remove(copy_path);
#endif
#define SYM(field, sym)                                                                                                \
    b->field = (void*)cnc_lib_sym(b->lib, sym);                                                                        \
    if (b->field == NULL) {                                                                                            \
        printf("%s: missing export %s\n", name, sym);                                                                  \
        return (0);                                                                                                    \
    }
    SYM(Init, "CNC_Init")
    SYM(AddModPath, "CNC_Add_Mod_Path")
    SYM(Start, "CNC_Start_Custom_Instance")
    SYM(Advance, "CNC_Advance_Instance")
    SYM(Dump, "OpenCNC3D_Dump_Objects")
    SYM(SetLockstep, "OpenCNC3D_Set_Lockstep")
    SYM(GetLockstep, "OpenCNC3D_Get_Lockstep")
    SYM(EventABI, "OpenCNC3D_Event_ABI")
    SYM(SetAILevels, "OpenCNC3D_Set_AI_Difficulties")
    SYM(AIPolicy, "OpenCNC3D_AI_Policy")
    SYM(AIState, "OpenCNC3D_AI_State_Hash")
    SYM(SetMP, "CNC_Set_Multiplayer_Data")
    SYM(Drain, "OpenCNC3D_Drain_Events")
    SYM(Post, "OpenCNC3D_Post_Event")
    SYM(Beacon, "CNC_Handle_Beacon_Request")
    SYM(SetSeed, "OpenCNC3D_Set_Seed")
    SYM(ToAI, "OpenCNC3D_Player_To_AI")
    SYM(IsAI, "OpenCNC3D_Player_Is_AI")
    SYM(Hurt, "OpenCNC3D_Hurt_Object")
    SYM(Blowup, "OpenCNC3D_Player_Blowup")
    SYM(SetInvincible, "OpenCNC3D_Set_Invincible")
    SYM(GetInvincible, "OpenCNC3D_Get_Invincible")
    SYM(SetBuildAnywhere, "OpenCNC3D_Set_Build_Anywhere")
    SYM(GetBuildAnywhere, "OpenCNC3D_Get_Build_Anywhere")
    SYM(GrantSupers, "OpenCNC3D_Grant_Superweapons")
    SYM(ProximityOk, "OpenCNC3D_Proximity_Ok")
    SYM(Potential, "OpenCNC3D_Get_Build_Potential")
    SYM(SetWallFill, "OpenCNC3D_Set_Wall_Fill")
    SYM(GetWallFill, "OpenCNC3D_Get_Wall_Fill")
    SYM(SetRally, "OpenCNC3D_Set_Rally")
    SYM(GetRally, "OpenCNC3D_Get_Rally")
    SYM(TriggerInfo, "OpenCNC3D_Trigger_Info")
    SYM(DebugReq, "CNC_Handle_Debug_Request")
    SYM(Structure, "CNC_Handle_Structure_Request")
    SYM(Sidebar, "CNC_Handle_Sidebar_Request")
    SYM(GetState, "CNC_Get_Game_State")
#undef SYM
    return (1);
}

/* Two humans first (ids 1 and 2), then `ai` computer seats (ids 3...). The humans choose their
   starts; the computers take RANDOM, so the synchronised start shuffle is exercised too. */
static int arm_match(Brain* b, int ai)
{
    static const unsigned char countries[6] = {
        RB_HOUSE_GREECE, RB_HOUSE_USSR, RB_HOUSE_ENGLAND, RB_HOUSE_UKRAINE, RB_HOUSE_GERMANY, RB_HOUSE_FRANCE};
    const int seats = 2 + ai;
    RBOptions opt;
    RBPlayerInfo* roster = (RBPlayerInfo*)calloc((size_t)seats, sizeof(RBPlayerInfo));
    int i, ok;
    if (roster == NULL) {
        return (0);
    }
    memset(&opt, 0, sizeof(opt));
    opt.MPlayerCount = seats;
    opt.MPlayerBases = 1;
    opt.MPlayerCredits = 10000;
    opt.MPlayerTiberium = 1;
    opt.MPlayerGoodies = 0;
    opt.MPlayerGhosts = 0;
    opt.MPlayerSolo = 1;
    opt.MPlayerUnitCount = 0;
    opt.EnableSuperweapons = 1;
    opt.DestroyStructures = 1;
    for (i = 0; i < seats; i++) {
        snprintf(roster[i].Name, sizeof(roster[i].Name), i < 2 ? "seat%d" : "computer%d", i);
        roster[i].House = countries[i % 6];
        roster[i].ColorIndex = i;
        roster[i].GlyphxPlayerID = (unsigned long long)(i + 1);
        roster[i].Team = i;
        roster[i].StartLocationIndex = (i < 2) ? i : RB_RANDOM_START;
        roster[i].IsAI = (i >= 2) ? 1 : 0;
    }
    {
        unsigned char levels[8] = {1,1,1,1,1,1,1,1};
        const char* selected = getenv("OPENCNC3D_TEST_AI_LEVELS");
        int seat = 0;
        while (selected && *selected && seat < 8) {
            int level = atoi(selected);
            levels[seat++] = (unsigned char)(level >= 0 && level <= 3 ? level : 1);
            while (*selected && *selected != ',') ++selected;
            if (*selected == ',') ++selected;
        }
        b->SetAILevels(levels, seats);
    }
    ok = b->SetMP(0, &opt, seats, roster, seats);
    free(roster);
    return (ok);
}

static int count_lines(const char* dump, const char* prefix, const char* also)
{
    const char* p = dump;
    size_t pl = strlen(prefix);
    int count = 0;
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= pl && memcmp(p, prefix, pl) == 0) {
            if (also == NULL) {
                count++;
            } else {
                char line[1024];
                size_t m = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
                memcpy(line, p, m);
                line[m] = '\0';
                if (strstr(line, also) != NULL) {
                    count++;
                }
            }
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return (count);
}

/* Every computer-played house's HOUSE line, summed: how many buildings and units they hold. The
   map's neutral houses are in the sum too, which is why the gate reads the change and not the total. */
static void ai_census(const char* dump, int* buildings, int* units)
{
    const char* p = dump;
    *buildings = 0;
    *units = 0;
    while ((p = strstr(p, "HOUSE|")) != NULL) {
        const char* e = strchr(p, '\n');
        char line[512];
        size_t n = e ? (size_t)(e - p) : strlen(p);
        size_t m = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, p, m);
        line[m] = '\0';
        if (strstr(line, "|human=0|") != NULL) {
            const char* bp = strstr(line, "|buildings=");
            const char* up = strstr(line, "|units=");
            if (bp) {
                *buildings += atoi(bp + 11);
            }
            if (up) {
                *units += atoi(up + 7);
            }
        }
        p += 6;
    }
}

/*
**	A beacon is the order these legs use, because it is the cheapest one the dump can see: it
**	creates an animation. CNC_Handle_Beacon_Request also deletes the caller's previous beacon at
**	once, locally, before any order crosses (one beacon per player), and that local delete is not
**	an order. So the gesture is made on BOTH instances and B's copy of the order is thrown away:
**	the local delete then happens on both, and A's drained bytes are the one order that crosses.
**	The renderer never places beacons, so this is a property of the test, not of a match.
*/
static void make_beacon(Brain* A, Brain* B, unsigned long long id, int x, int y)
{
    unsigned char discard[RB_EVENT_MAX * 64];
    A->Beacon(0, id, x, y);
    B->Beacon(0, id, x, y);
    B->Drain(discard, RB_EVENT_MAX, 3);
}

static int agree(Brain* A, Brain* B, const char* when, char** keep)
{
    size_t la = 0, lb = 0;
    char* da = capture_dump(A, &la);
    char* db = capture_dump(B, &lb);
    int same;
    if (da == NULL || db == NULL) {
        printf("FAILED: could not capture a dump %s\n", when);
        free(da);
        free(db);
        return (0);
    }
    same = (la == lb && memcmp(da, db, la) == 0);
    if (!same) {
        printf("\nFAILED: the two instances differ %s.\n", when);
        report_first_difference(da, db);
    }
    if (keep != NULL && same) {
        *keep = da;
        da = NULL;
    }
    free(da);
    free(db);
    return (same);
}

static int settle(Brain* A, Brain* B, int ticks, const char* when)
{
    int t;
    for (t = 0; t < ticks; t++) {
        A->Advance(1ULL);
        B->Advance(2ULL);
        if (!agree(A, B, when, NULL)) {
            printf("        (%d tick(s) in)\n", t + 1);
            return (0);
        }
    }
    return (1);
}


/* The integer after "|key=" on a dump line, or dflt. */
static int line_field(const char* line, const char* key, int dflt)
{
    char k[64];
    const char* p;
    snprintf(k, sizeof(k), "|%s=", key);
    p = strstr(line, k);
    return (p != NULL) ? atoi(p + strlen(k)) : dflt;
}

/* The first dump line starting `prefix` (and, when `also` is given, containing it), copied. */
static int find_line(const char* dump, const char* prefix, const char* also, char* out, size_t outsz)
{
    const char* p = dump;
    size_t pl = strlen(prefix);
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= pl && memcmp(p, prefix, pl) == 0) {
            size_t m = n < outsz - 1 ? n : outsz - 1;
            memcpy(out, p, m);
            out[m] = '\0';
            if (also == NULL || strstr(out, also) != NULL) {
                return (1);
            }
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return (0);
}

/* The highest heap id among dump lines starting `prefix`, or -1. */
static int newest_id(const char* dump, const char* prefix)
{
    const char* p = dump;
    size_t pl = strlen(prefix);
    int best = -1;
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= pl && memcmp(p, prefix, pl) == 0) {
            char line[1024];
            size_t m = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
            int id;
            memcpy(line, p, m);
            line[m] = '\0';
            id = line_field(line, "id", -1);
            if (id > best) {
                best = id;
            }
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return (best);
}

/* A debug spawn of `name` for seat 0 on BOTH instances at the first pixel spot that takes it.
   The debug spawn is a fixture, not an order: it is applied to both by hand, the way a test
   builds a fixture, and the worlds must still agree afterwards. Returns the new object's id. */
static int spawn_both_at(Brain* A, Brain* B, const char* name, const char* prefix, int* px, int* py)
{
    static const int spots[][2] = {{320, 240}, {416, 240}, {320, 336}, {224, 240}, {320, 144}, {512, 240},
                                   {320, 432}, {128, 240}, {608, 336}, {416, 144}, {200, 400}, {500, 100}};
    size_t i;
    for (i = 0; i < sizeof(spots) / sizeof(spots[0]); i++) {
        char* dump = NULL;
        int before, after, id;
        if (!agree(A, B, "before a fixture spawn", &dump)) {
            return (-1);
        }
        before = count_lines(dump, prefix, NULL);
        free(dump);
        A->DebugReq(0 /* DEBUG_REQUEST_SPAWN_OBJECT */, 1ULL, name, spots[i][0], spots[i][1], 0, 0);
        B->DebugReq(0 /* DEBUG_REQUEST_SPAWN_OBJECT */, 1ULL, name, spots[i][0], spots[i][1], 0, 0);
        if (!settle(A, B, 1, "after a fixture spawn applied to both")) {
            return (-1);
        }
        if (!agree(A, B, "reading the fixture", &dump)) {
            return (-1);
        }
        after = count_lines(dump, prefix, NULL);
        id = newest_id(dump, prefix);
        free(dump);
        if (after == before + 1) {
            if (px != NULL) {
                *px = spots[i][0];
            }
            if (py != NULL) {
                *py = spots[i][1];
            }
            return (id);
        }
    }
    printf("FAILED: no pixel spot took a %s through the debug spawn\n", name);
    return (-1);
}

static int spawn_both(Brain* A, Brain* B, const char* name, const char* prefix)
{
    return (spawn_both_at(A, B, name, prefix, NULL, NULL));
}

/* Drain A's orders and post the same bytes to both, as a host does with its own. The count is
   checked exactly: a gesture that queues two orders, or none, is a finding. */
static int cross(Brain* A, Brain* B, unsigned event_size, const char* what, int expect)
{
    unsigned char wire[RB_EVENT_MAX * 64];
    int n = A->Drain(wire, RB_EVENT_MAX, 3);
    int i;
    if (n != expect) {
        printf("FAILED: %s: expected %d order(s) on the drain and got %d\n", what, expect, n);
        return (0);
    }
    for (i = 0; i < n; i++) {
        const void* ev = wire + ((size_t)i * event_size);
        if (A->Post(ev) != 1 || B->Post(ev) != 1) {
            printf("FAILED: %s: posting order %d was refused\n", what, i);
            return (0);
        }
    }
    return (1);
}

/*
**	THE EXPORTS LEG. Every switch and query the Tiberian Dawn brain carries and the Red Alert
**	brain now has, asserted by its EFFECT on the engine, and every one applied to both instances
**	alike (or, for the rally point, made on A and carried across as an order) so the worlds still
**	agree afterwards.
*/
static int exports_leg(Brain* A, Brain* B, unsigned event_size)
{
    char* dump = NULL;
    char line[1024];
    int mcv, str0, str1, cx, cy, n, wantx, wanty;
    unsigned char entries[128 * 16];

    printf("\n--- the exports leg ---\n");

    /* Invincibility: seat 0's MCV refuses damage while the switch is on, takes it when off. */
    if (!agree(A, B, "before the exports leg", &dump)) {
        return (0);
    }
    if (!find_line(dump, "OBJ|UNIT|MCV|", "|Multi1|", line, sizeof(line)) && !find_line(dump, "OBJ|BUILDING|FACT|", "|Multi1|", line, sizeof(line))) {
        printf("FAILED: seat 0 has neither an MCV nor a Construction Yard to test invincibility on\n");
        free(dump);
        return (0);
    }
    free(dump);
    if (strncmp(line, "OBJ|UNIT|", 9) != 0) {
        printf("  (seat 0 has deployed; invincibility is checked against the vehicles the harness spawns)\n");
    }
    mcv = line_field(line, "id", -1);
    str0 = line_field(line, "str", -1);
    if (!A->SetInvincible(1ULL, 1) || !B->SetInvincible(1ULL, 1) || A->GetInvincible(1ULL) != 1 || A->GetInvincible(2ULL) != 0) {
        printf("FAILED: OpenCNC3D_Set/Get_Invincible did not take for seat 0 alone\n");
        return (0);
    }
    if (strncmp(line, "OBJ|UNIT|", 9) == 0) {
        A->Hurt(RB_RTTI_UNIT, mcv, 50);
        B->Hurt(RB_RTTI_UNIT, mcv, 50);
        if (!settle(A, B, 1, "after hurting an invincible vehicle") || !agree(A, B, "reading the invincible vehicle", &dump)) {
            return (0);
        }
        find_line(dump, "OBJ|UNIT|MCV|", "|Multi1|", line, sizeof(line));
        free(dump);
        str1 = line_field(line, "str", -1);
        if (str1 != str0) {
            printf("FAILED: an invincible MCV lost strength (%d -> %d)\n", str0, str1);
            return (0);
        }
        A->SetInvincible(1ULL, 0);
        B->SetInvincible(1ULL, 0);
        A->Hurt(RB_RTTI_UNIT, mcv, 50);
        B->Hurt(RB_RTTI_UNIT, mcv, 50);
        if (!settle(A, B, 1, "after hurting the vehicle again") || !agree(A, B, "reading the vehicle again", &dump)) {
            return (0);
        }
        find_line(dump, "OBJ|UNIT|MCV|", "|Multi1|", line, sizeof(line));
        free(dump);
        if (line_field(line, "str", -1) >= str1) {
            printf("FAILED: with invincibility off the MCV still took no damage (%d -> %d)\n", str1, line_field(line, "str", -1));
            return (0);
        }
        printf("  invincibility: seat 0's MCV kept %d strength under 50 damage with the switch on and dropped to %d with it off\n",
               str1, line_field(line, "str", -1));
    } else {
        A->SetInvincible(1ULL, 0);
        B->SetInvincible(1ULL, 0);
    }

    /* Build Anywhere: a Power Plant far from anything seat 0 owns. */
    wantx = 64;
    wanty = 64;
    if (A->ProximityOk(1ULL, "POWR", wantx, wanty)) {
        wantx = 10;
        wanty = 10;
    }
    if (A->ProximityOk(1ULL, "POWR", wantx, wanty) || A->ProximityOk(1ULL, "NOSUCH", wantx, wanty)) {
        printf("FAILED: OpenCNC3D_Proximity_Ok passed a far cell, or an unknown type, before Build Anywhere\n");
        return (0);
    }
    A->SetBuildAnywhere(1ULL, 1);
    B->SetBuildAnywhere(1ULL, 1);
    if (!A->ProximityOk(1ULL, "POWR", wantx, wanty) || !A->GetBuildAnywhere(1ULL) || A->GetBuildAnywhere(2ULL)) {
        printf("FAILED: Build Anywhere did not lift the adjacency rule for seat 0 alone\n");
        return (0);
    }
    A->SetBuildAnywhere(1ULL, 0);
    B->SetBuildAnywhere(1ULL, 0);
    printf("  build anywhere: cell %d,%d refused a Power Plant, passed with the switch on, refused again with it off: %d\n",
           wantx, wanty, A->ProximityOk(1ULL, "POWR", wantx, wanty) ? 0 : 1);

    /* The potential build tree. */
    n = A->Potential(1ULL, entries, 128);
    {
        int i, powr = 0, tent = 0;
        for (i = 0; i < n && i < 128; i++) {
            const char* nm = (const char*)(entries + i * 16);
            if (!strcmp(nm, "POWR")) {
                powr = 1;
            }
            if (!strcmp(nm, "TENT")) {
                tent = 1;
            }
        }
        if (n < 10 || !powr || !tent || A->Potential(99ULL, entries, 128) != -1) {
            printf("FAILED: OpenCNC3D_Get_Build_Potential for a Greek seat listed %d entries (POWR %d, TENT %d)\n", n, powr, tent);
            return (0);
        }
        printf("  build potential: %d things a Greek seat could eventually build, the Power Plant and the Allied barracks among them\n", n);
        printf("RA2B|potential|count=%d|names=", n);
        for (i = 0; i < n && i < 128; i++) {
            printf("%s%s", i ? "," : "", (const char*)(entries + i * 16));
        }
        printf("\n");
    }

    /* The wall line reach. */
    A->SetWallFill(5);
    if (A->GetWallFill() != 5) {
        printf("FAILED: OpenCNC3D_Set/Get_Wall_Fill\n");
        return (0);
    }
    A->SetWallFill(99);
    if (A->GetWallFill() != 16) {
        printf("FAILED: the wall fill reach is not clamped to 16\n");
        return (0);
    }
    A->SetWallFill(0);

    /* A trigger that is not there. */
    if (A->TriggerInfo("ZZZZ", NULL, NULL, NULL, NULL)) {
        printf("FAILED: OpenCNC3D_Trigger_Info found a trigger that does not exist\n");
        return (0);
    }

    /* Super weapons, granted and taken back on both. */
    if (!A->GrantSupers(1ULL, 1) || !B->GrantSupers(1ULL, 1) || !settle(A, B, 3, "after granting super weapons")
        || !A->GrantSupers(1ULL, 0) || !B->GrantSupers(1ULL, 0) || !settle(A, B, 3, "after removing them")
        || A->GrantSupers(99ULL, 1)) {
        printf("FAILED: OpenCNC3D_Grant_Superweapons\n");
        return (0);
    }

    /* The rally point: a barracks for seat 0, a rally cell set on A, carried across as the ARCHIVE
       order, and read back on BOTH. */
    {
        int tent = spawn_both(A, B, "TENT", "OBJ|BUILDING|TENT|");
        int ax = -1, ay = -1, bx = -1, by = -1;
        if (tent < 0) {
            return (0);
        }
        if (!agree(A, B, "reading the barracks", &dump)) {
            return (0);
        }
        {
            char tl[1024];
            char key[32];
            snprintf(key, sizeof(key), "|id=%d|", tent);
            find_line(dump, "OBJ|BUILDING|TENT|", key, tl, sizeof(tl));
            cx = line_field(tl, "x", 0) + 3;
            cy = line_field(tl, "y", 0) + 4;
        }
        free(dump);
        if (A->GetRally(tent, &ax, &ay)) {
            printf("FAILED: a new barracks already has a rally point\n");
            return (0);
        }
        if (!A->SetRally(1ULL, tent, cx, cy) || !cross(A, B, event_size, "rally point", 1)
            || !settle(A, B, 5, "after the rally order executed")) {
            printf("FAILED: the rally point did not cross as one order\n");
            return (0);
        }
        if (!A->GetRally(tent, &ax, &ay) || !B->GetRally(tent, &bx, &by) || ax != cx || ay != cy || bx != cx || by != cy) {
            printf("FAILED: the rally point set on A did not land on both (A %d,%d B %d,%d, want %d,%d)\n", ax, ay, bx, by, cx, cy);
            return (0);
        }
        if (A->SetRally(2ULL, tent, cx, cy)) {
            printf("FAILED: seat 1 was allowed to set a rally point on seat 0's barracks\n");
            return (0);
        }
        printf("  rally point: set on A for a barracks of seat 0, crossed as one order, and read back as %d,%d on both\n", cx, cy);
    }
    printf("RA2B|exports|ok=1\n");
    return (1);
}



/* The dump line of one building, found by type and heap id. */
static int building_line(const char* dump, const char* ini, int id, char* out, size_t outsz)
{
    char prefix[64], key[32];
    snprintf(prefix, sizeof(prefix), "OBJ|BUILDING|%s|", ini);
    snprintf(key, sizeof(key), "|id=%d|", id);
    return (find_line(dump, prefix, key, out, outsz));
}

/* How many of a building type one house has standing on the map (not in limbo inside a
   factory). By house, because the computer players build Power Plants of their own while a
   placement is being counted. */
static int count_placed(const char* dump, const char* ini, const char* owner)
{
    char prefix[96];
    snprintf(prefix, sizeof(prefix), "OBJ|BUILDING|%s|%s|", ini, owner);
    return (count_lines(dump, prefix, "|limbo=0|"));
}

/*
**	THE GESTURE LEGS. Repair, the power switch, sale and placement used to change the world on
**	the machine that clicked and nowhere else. Under lockstep each is an order. Every leg makes
**	the gesture on A, requires that A's world did NOT change (the bypass is closed), carries the
**	order across to both, and requires the effect on both, byte identical.
*/
#define RB_STRUCTURE_REPAIR 2
#define RB_STRUCTURE_SELL 4
#define RB_STRUCTURE_POWER 6
#define RB_SIDEBAR_START_CONSTRUCTION 0
#define RB_SIDEBAR_START_PLACEMENT 3
#define RB_SIDEBAR_PLACE 4
#define RB_DEBUG_KILL_OBJECT 4
#define RB_DEBUG_END_PRODUCTION 5
#define RB_RTTI_BUILDINGTYPE 6
#define RB_STRUCT_POWER 17

static int gesture_legs(Brain* A, Brain* B, unsigned event_size)
{
    unsigned char* mapbuf;
    int mapx, mapy, fact, fpx, fpy, fx, fy, powr;
    char* dump = NULL;
    char line[1024];

    printf("\n--- the gesture legs: repair, the power switch, sale and placement are ORDERS ---\n");
    mapbuf = (unsigned char*)calloc(1, 1 << 20);
    if (mapbuf == NULL || !A->GetState(1 /* GAME_STATE_STATIC_MAP */, 1ULL, mapbuf, 1 << 20)) {
        printf("FAILED: the static map state was refused\n");
        free(mapbuf);
        return (0);
    }
    memcpy(&mapx, mapbuf, sizeof(int));
    memcpy(&mapy, mapbuf + sizeof(int), sizeof(int));
    free(mapbuf);

    fact = spawn_both_at(A, B, "FACT", "OBJ|BUILDING|FACT|", &fpx, &fpy);
    powr = spawn_both(A, B, "POWR", "OBJ|BUILDING|POWR|");
    if (fact < 0 || powr < 0) {
        return (0);
    }
    if (!agree(A, B, "reading the fixtures", &dump)) {
        return (0);
    }
    building_line(dump, "FACT", fact, line, sizeof(line));
    free(dump);
    fx = line_field(line, "x", -1);
    fy = line_field(line, "y", -1);
    printf("  fixtures: a Construction Yard (id %d, cell %d,%d) and a Power Plant (id %d) for seat 0, on both\n", fact, fx, fy, powr);

    /* SALE. The click must change nothing on A. Red Alert asks Can_Demolish at click time, and a
       building can be sold only when its build-up art is loaded (the sale plays it backwards),
       which a headless world does not do: the click is then refused before any order exists.
       With the art, one order crosses. Both cases leave the two worlds agreeing. */
    {
        unsigned char wire[RB_EVENT_MAX * 64];
        int n, i;
        A->Structure(RB_STRUCTURE_SELL, 1ULL, powr);
        if (!agree(A, B, "after the sell click and BEFORE the drain: the sale went straight into A's world", NULL)) {
            return (0);
        }
        n = A->Drain(wire, RB_EVENT_MAX, 3);
        if (n < 0 || n > 1) {
            printf("FAILED: sell: %d orders on the drain\n", n);
            return (0);
        }
        for (i = 0; i < n; i++) {
            if (A->Post(wire) != 1 || B->Post(wire) != 1) {
                printf("FAILED: sell: the order was refused\n");
                return (0);
            }
        }
        if (!settle(A, B, 8, "after the sell order executed")) {
            return (0);
        }
        printf("  sell: the click changed nothing locally; %s\n",
               n ? "one order crossed and both worlds agree" :
                   "it was refused at click time (no build-up art in a headless world), and both agree");
    }

    /* REPAIR: damage the yard on both through the debug kill path (100 points, forced), then the
       repair click. */
    {
        static const int hits[][2] = {{0, 0}, {12, 12}, {24, 0}, {0, 24}, {36, 12}, {-12, -12}};
        size_t h;
        int str = -1, max = -1;
        for (h = 0; h < sizeof(hits) / sizeof(hits[0]); h++) {
            A->DebugReq(RB_DEBUG_KILL_OBJECT, 1ULL, "X", fpx + hits[h][0], fpy + hits[h][1], 0, 0);
            B->DebugReq(RB_DEBUG_KILL_OBJECT, 1ULL, "X", fpx + hits[h][0], fpy + hits[h][1], 0, 0);
            if (!settle(A, B, 1, "after the debug damage applied to both") || !agree(A, B, "reading the yard", &dump)) {
                return (0);
            }
            building_line(dump, "FACT", fact, line, sizeof(line));
            free(dump);
            str = line_field(line, "str", -1);
            max = line_field(line, "max", -1);
            if (str >= 0 && str < max) {
                break;
            }
        }
        if (str < 0 || str >= max) {
            printf("FAILED: no debug damage over the yard's footprint damaged it; nothing to repair\n");
            return (0);
        }
        printf("  repair: the yard stands at %d/%d\n", str, max);
    }
    A->Structure(RB_STRUCTURE_REPAIR, 1ULL, fact);
    if (!agree(A, B, "after the repair click and BEFORE the drain: the repair began in A's world alone", NULL)
        || !cross(A, B, event_size, "repair", 1) || !settle(A, B, 6, "after the repair order executed")
        || !agree(A, B, "reading the world after the repair", &dump)) {
        return (0);
    }
    building_line(dump, "FACT", fact, line, sizeof(line));
    free(dump);
    if (line_field(line, "repairing", 0) != 1) {
        printf("FAILED: the repair order executed on both and the yard is not repairing: %s\n", line);
        return (0);
    }
    printf("  repair: one order crossed and the yard is repairing on both\n");

    /* THE POWER SWITCH. A yard still in its build-up refuses the switch on purpose; that refusal
       is asserted first, then the build-up runs out and the switch is thrown off and back on. */
    {
        int wait, off;
        int building_up;
        if (!agree(A, B, "before the power switch", &dump)) {
            return (0);
        }
        building_line(dump, "FACT", fact, line, sizeof(line));
        free(dump);
        building_up = strstr(line, "|mission=Construction|") != NULL;
        if (building_up) {
            A->Structure(RB_STRUCTURE_POWER, 1ULL, fact);
            if (!agree(A, B, "after the power click on a yard still being built and BEFORE the drain", NULL)
                || !cross(A, B, event_size, "power switch during build-up", 1)
                || !settle(A, B, 3, "after the power switch during build-up executed")
                || !agree(A, B, "reading the yard after it", &dump)) {
                return (0);
            }
            building_line(dump, "FACT", fact, line, sizeof(line));
            free(dump);
            if (line_field(line, "poweroff", -1) != 0) {
                printf("FAILED: a power switch thrown while the yard was being built was not refused: %s\n", line);
                return (0);
            }
            printf("  power: a switch thrown during the yard's build-up crossed and was refused on both\n");
        }
        for (wait = 0;; wait++) {
            if (!agree(A, B, "while the yard's build-up runs out", &dump)) {
                return (0);
            }
            building_line(dump, "FACT", fact, line, sizeof(line));
            free(dump);
            if (strstr(line, "|mission=Construction|") == NULL) {
                break;
            }
            if (wait >= 600 || !settle(A, B, 1, "while the yard's build-up runs out")) {
                printf("FAILED: the yard was still under construction %d ticks later\n", wait);
                return (0);
            }
        }
        for (off = 1; off >= 0; off--) {
            A->Structure(RB_STRUCTURE_POWER, 1ULL, fact);
            if (!agree(A, B, "after the power click and BEFORE the drain: the switch went straight into A's world", NULL)
                || !cross(A, B, event_size, off ? "power off" : "power on", 1)
                || !settle(A, B, 6, "after the power switch executed") || !agree(A, B, "reading the power state", &dump)) {
                return (0);
            }
            building_line(dump, "FACT", fact, line, sizeof(line));
            free(dump);
            if (line_field(line, "poweroff", -1) != off) {
                printf("FAILED: the power switch did not land on both (want poweroff=%d): %s\n", off, line);
                return (0);
            }
        }
        printf("  power: the yard switched off and back on through one order each, identical on both\n");
    }

    /* PLACEMENT: a Power Plant started (an order), finished on both by the debug path, then placed
       twice in one window, which must land exactly ONE building. */
    A->Sidebar(RB_SIDEBAR_START_CONSTRUCTION, 1ULL, RB_RTTI_BUILDINGTYPE, RB_STRUCT_POWER, 0, 0);
    if (!agree(A, B, "after the build click and BEFORE the drain: production began on A alone", NULL)
        || !cross(A, B, event_size, "produce", 1) || !settle(A, B, 5, "after the produce order executed")) {
        return (0);
    }
    A->DebugReq(RB_DEBUG_END_PRODUCTION, 1ULL, "", 0, 0, 0, 0);
    B->DebugReq(RB_DEBUG_END_PRODUCTION, 1ULL, "", 0, 0, 0, 0);
    if (!settle(A, B, 2, "after production was forced complete on both")) {
        return (0);
    }
    A->Sidebar(RB_SIDEBAR_START_PLACEMENT, 1ULL, RB_RTTI_BUILDINGTYPE, RB_STRUCT_POWER, 0, 0);
    {
        static const int offs[][2] = {{3, 0}, {3, 1}, {-2, 0}, {0, 3}, {0, -2}, {3, -2}, {-2, 3}, {4, 0}, {0, 4}};
        int before, placed = 0;
        size_t i;
        char owner[32] = "";
        if (!agree(A, B, "before placement", &dump)) {
            return (0);
        }
        /* The house the yard belongs to: the one whose Power Plant is being placed. */
        building_line(dump, "FACT", fact, line, sizeof(line));
        {
            const char* o = strncmp(line, "OBJ|BUILDING|FACT|", 18) == 0 ? line + 18 : "";
            size_t n = strcspn(o, "|");
            if (n >= sizeof(owner)) n = sizeof(owner) - 1;
            memcpy(owner, o, n);
            owner[n] = '\0';
        }
        before = count_placed(dump, "POWR", owner);
        free(dump);
        for (i = 0; i < sizeof(offs) / sizeof(offs[0]) && !placed; i++) {
            short cx = (short)(fx + offs[i][0] - mapx);
            short cy = (short)(fy + offs[i][1] - mapy);
            int after;
            if (cx < 0 || cy < 0) {
                continue;
            }
            A->Sidebar(RB_SIDEBAR_PLACE, 1ULL, RB_RTTI_BUILDINGTYPE, RB_STRUCT_POWER, cx, cy);
            A->Sidebar(RB_SIDEBAR_PLACE, 1ULL, RB_RTTI_BUILDINGTYPE, RB_STRUCT_POWER, cx, cy);
            if (!agree(A, B, "after the place click and BEFORE the drain: the building landed in A's world alone", NULL)
                || !cross(A, B, event_size, "place (clicked twice)", 2)
                || !settle(A, B, 6, "after the place orders executed") || !agree(A, B, "reading placement", &dump)) {
                return (0);
            }
            after = count_placed(dump, "POWR", owner);
            free(dump);
            if (after == before + 1) {
                printf("  place: two clicks in one window crossed as two orders and placed exactly one Power Plant, on both\n");
                placed = 1;
            } else if (after > before + 1) {
                printf("FAILED: two place clicks in one window placed %d buildings\n", after - before);
                return (0);
            }
        }
        if (!placed) {
            printf("FAILED: no cell around the yard at %d,%d accepted the Power Plant\n", fx, fy);
            return (0);
        }
    }
    printf("RA2B|gestures|ok=1\n");
    return (1);
}

/* ---------------------------------------------------------------------- main -------- */

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 4) {
        printf("usage: %s <RedAlert.dylib> <scenario_dir> <scenario> [ticks] [ai_seats]\n", argv[0]);
        return (2);
    }
    const char* src = argv[1];
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s%s", argv[2], (argv[2][0] && argv[2][strlen(argv[2]) - 1] == '/') ? "" : "/");
    const char* scenario = argv[3];
    const int ticks = (argc > 4) ? atoi(argv[4]) : 1500;
    const int ai = (argc > 5) ? atoi(argv[5]) : 2;

    Brain A, B;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    printf("OpenCNC3D Red Alert two-brain lockstep gate\n");
    printf("  brain    : %s\n", src);
    printf("  scenario : %s in %s   ticks: %d   computer seats: %d\n\n", scenario, dir, ticks, ai);

    if (!load_brain(&A, src, RB_COPY_A, "A") || !load_brain(&B, src, RB_COPY_B, "B")) {
        return (2);
    }
    if (A.lib == B.lib) {
        printf("FAILED: dlopen returned the SAME handle for both copies; they are one instance.\n");
        return (1);
    }
    A.SetLockstep(1);
    if (A.GetLockstep() != 1 || B.GetLockstep() != 0) {
        printf("FAILED: the two instances SHARE GLOBALS (A=%d B=%d after setting only A).\n", A.GetLockstep(), B.GetLockstep());
        return (1);
    }
    printf("  isolation: setting the flag on A left B untouched. Two real instances.\n");
    B.SetLockstep(1);
    if (A.GetLockstep() != 1 || B.GetLockstep() != 1) {
        printf("FAILED: could not put both instances into lockstep mode.\n");
        return (1);
    }

    unsigned int abiA[32], abiB[32];
    int nA = A.EventABI(abiA, 32);
    int nB = B.EventABI(abiB, 32);
    if (nA != nB || nA < 11 || memcmp(abiA, abiB, (size_t)nA * sizeof(unsigned int)) != 0) {
        printf("FAILED: the two instances report different order-wire layouts.\n");
        return (1);
    }
    const unsigned int event_size = abiA[1];
    printf("  wire     : layout hash %08X, event %u bytes, %u event types\n\n", abiA[10], event_size, abiA[9]);
    if (event_size == 0 || event_size > 64) {
        printf("FAILED: an event of %u bytes does not fit this harness's wire buffer\n", event_size);
        return (1);
    }

    A.AddModPath(dir);
    B.AddModPath(dir);
    A.Init("-HEADLESS", noop_callback);
    B.Init("-HEADLESS", noop_callback);

    /*
    **	THE WARM START. OPENCNC3D_RB_WARM=<SCEN> makes A play that match first and throw it away
    **	while B plays nothing, so the two differ in exactly what their process has already done:
    **	a player who hosts after a skirmish against a joiner who has just launched. Anything that
    **	survives a scenario change and is then read by the simulation shows up as a divergence.
    */
    {
        const char* warm = getenv("OPENCNC3D_RB_WARM");
        if (warm != NULL && *warm != '\0') {
            int w;
            A.SetSeed(0);
            if (!arm_match(&A, ai) || !A.Start(dir, dir, warm, 10, 1)) {
                printf("FAILED: the warm-up match %s did not start on A\n", warm);
                return (1);
            }
            for (w = 0; w < 300; w++) {
                A.Advance(1ULL);
            }
            printf("  warm start: A has played 300 ticks of %s and is about to start %s over it\n", warm, scenario);
        }
    }

    /*
    **	No seed is agreed: under lockstep the brain then uses zero, the same on every peer.
    **	OPENCNC3D_RB_SEED_B gives B another seed, the negative control: the comparison must then
    **	report a divergence, or it cannot see a different world at all.
    */
    A.SetSeed(0);
    B.SetSeed(getenv("OPENCNC3D_RB_SEED_B") ? atoi(getenv("OPENCNC3D_RB_SEED_B")) : 0);

    if (!arm_match(&A, ai) || !arm_match(&B, ai)) {
        printf("FAILED: CNC_Set_Multiplayer_Data refused the match on an instance\n");
        return (1);
    }
    int okA = A.Start(dir, dir, scenario, 10, 1);
    int okB = B.Start(dir, dir, scenario, 10, 1);
    if (!okA || !okB) {
        printf("FAILED: the scenario did not start (A=%d B=%d)\n", okA, okB);
        return (1);
    }
    printf("--- %s started on both as a %d seat match (2 human, %d computer) ---\n", scenario, 2 + ai, ai);
    printf("  A advances as seat 0's player, B as seat 1's: the local context differs on purpose\n");

    char* first = NULL;
    if (!agree(&A, &B, "at tick 0", &first)) {
        return (1);
    }
    int b0 = 0, u0 = 0;
    ai_census(first, &b0, &u0);
    const int objs0 = count_lines(first, "OBJ|", NULL);
    free(first);

    /* ---- 1. THE OPENING: the computer seats deploy and start building, the humans idle ---- */
    const int opening = 150;
    if (!settle(&A, &B, opening, "in the opening")) {
        return (1);
    }
    printf("  %d ticks of the opening identical\n", opening);

    /* ---- 2. THE ORDER LEG: an order made on A must land in B, through the wire only ---- */
    {
        unsigned char wire[RB_EVENT_MAX * 64];
        int drained, i, t;
        char* dump = NULL;
        int before, after;

        printf("\n--- the order leg ---\n");
        drained = A.Drain(wire, RB_EVENT_MAX, 3);
        if (drained != 0) {
            printf("FAILED: the drain returned %d with no order outstanding, expected 0\n", drained);
            return (1);
        }
        if (!agree(&A, &B, "before the order", &dump)) {
            return (1);
        }
        before = count_lines(dump, "EFX|ANIM|", "|name=BEACON|");
        free(dump);

        make_beacon(&A, &B, 1ULL, 320, 200);
        drained = A.Drain(wire, RB_EVENT_MAX, 3);
        if (drained < 1) {
            printf("FAILED: A produced no order to drain (drain returned %d)\n", drained);
            return (1);
        }
        if (!agree(&A, &B, "after the order was made and drained, before it was posted", NULL)) {
            return (1);
        }
        for (i = 0; i < drained; i++) {
            const void* ev = wire + ((size_t)i * event_size);
            int ra = A.Post(ev);
            int rb = B.Post(ev);
            if (ra != 1 || rb != 1) {
                printf("FAILED: posting order %d was refused (A=%d B=%d)\n", i, ra, rb);
                return (1);
            }
        }
        if (!settle(&A, &B, 10, "after the order was posted to both")) {
            return (1);
        }
        if (!agree(&A, &B, "reading the world after the order", &dump)) {
            return (1);
        }
        after = count_lines(dump, "EFX|ANIM|", "|name=BEACON|");
        free(dump);
        if (after <= before) {
            printf("FAILED: the order crossed and executed on both, but no beacon appeared (%d -> %d)\n", before, after);
            return (1);
        }
        printf("  a beacon placed on A crossed the wire, executed on both and left them identical (beacons %d -> %d)\n",
               before, after);
        printf("RA2B|order|crossed=1|beacons=%d\n", after);

        /* The lookahead: stamped for 5 turns ahead it is accepted 5 frames later, on both; stamped
           for 3 it is refused 5 frames later, on both. Both worlds receive the same, so they agree. */
        make_beacon(&A, &B, 1ULL, 300, 200);
        drained = A.Drain(wire, RB_EVENT_MAX, 5);
        for (t = 0; t < 5; t++) {
            A.Advance(1ULL);
            B.Advance(2ULL);
        }
        if (drained < 1 || A.Post(wire) != 1 || B.Post(wire) != 1) {
            printf("FAILED: an order stamped 5 turns ahead was refused 5 frames later\n");
            return (1);
        }
        make_beacon(&A, &B, 1ULL, 310, 210);
        drained = A.Drain(wire, RB_EVENT_MAX, 3);
        for (t = 0; t < 5; t++) {
            A.Advance(1ULL);
            B.Advance(2ULL);
        }
        if (drained < 1 || A.Post(wire) != -3 || B.Post(wire) != -3) {
            printf("FAILED: an order stamped 3 turns ahead was not refused as stale 5 frames later\n");
            return (1);
        }
        if (!settle(&A, &B, 5, "after the lookahead leg")) {
            return (1);
        }
        printf("  lookahead: an order is accepted on its stamped frame and refused (-3) once that has passed\n");
    }

    if (!exports_leg(&A, &B, event_size)) {
        return (1);
    }
    if (!gesture_legs(&A, &B, event_size)) {
        return (1);
    }

    /* ---- 3. THE TAKEOVER: both humans handed to the computer, then every seat plays ----
    **
    **	This is the harshest test of the local context the gate can make. After it every house is
    **	played by Red Alert's expert system, so the bases grow and the armies fight, and the two
    **	instances still name DIFFERENT houses as their local player. Any simulation path that asks
    **	"is this the local player's house" makes them part here.
    */
    printf("\n--- the takeover: both human seats handed to the computer on both instances ---\n");
    if (!A.ToAI(1ULL) || !A.ToAI(2ULL) || !B.ToAI(1ULL) || !B.ToAI(2ULL)) {
        printf("FAILED: OpenCNC3D_Player_To_AI refused a human seat\n");
        return (1);
    }
    if (A.IsAI(1ULL) != 1 || A.IsAI(2ULL) != 1 || B.IsAI(1ULL) != 1 || B.IsAI(2ULL) != 1 || A.IsAI(99ULL) != -1) {
        printf("FAILED: OpenCNC3D_Player_Is_AI does not report the takeover (A %d %d, B %d %d, unknown %d)\n",
               A.IsAI(1ULL), A.IsAI(2ULL), B.IsAI(1ULL), B.IsAI(2ULL), A.IsAI(99ULL));
        return (1);
    }
    if (A.ToAI(1ULL)) {
        printf("FAILED: a second takeover of the same seat was accepted\n");
        return (1);
    }
    printf("  both seats now report the computer, and a second takeover is refused\n");

    int tick;
    int fight_peak = 0;
    int fight_ticks = 0;
    for (tick = 1; tick <= ticks; tick++) {
        unsigned char contA = A.Advance(1ULL);
        unsigned char contB = B.Advance(2ULL);
        if (contA != contB) {
            printf("\nFAILED at tick %d: one instance wants to stop and the other does not (A=%d B=%d)\n", tick, contA, contB);
            return (1);
        }
        size_t la = 0, lb = 0;
        char* da = capture_dump(&A, &la);
        char* db = capture_dump(&B, &lb);
        if (da == NULL || db == NULL) {
            printf("\nFAILED at tick %d: could not capture a dump\n", tick);
            return (2);
        }
        if (la != lb || memcmp(da, db, la) != 0) {
            printf("\nFAILED: the two instances DIVERGED at tick %d of the takeover.\n", tick);
            report_first_difference(da, db);
            free(da);
            free(db);
            return (1);
        }
        {
            /* An army in action: units, soldiers, ships and aircraft with a target to shoot. */
            int fighting = count_lines(da, "OBJ|UNIT|", NULL) - count_lines(da, "OBJ|UNIT|", "|tar=-1|")
                           + count_lines(da, "OBJ|INFANTRY|", NULL) - count_lines(da, "OBJ|INFANTRY|", "|tar=-1|")
                           + count_lines(da, "OBJ|VESSEL|", NULL) - count_lines(da, "OBJ|VESSEL|", "|tar=-1|")
                           + count_lines(da, "OBJ|AIRCRAFT|", NULL) - count_lines(da, "OBJ|AIRCRAFT|", "|tar=-1|");
            if (fighting > fight_peak) {
                fight_peak = fighting;
            }
            if (fighting > 0) {
                fight_ticks++;
            }
        }
        if (tick == ticks || !contA) {
            int b1 = 0, u1 = 0;
            ai_census(da, &b1, &u1);
            printf("  the computer-played houses held %d buildings and %d units at the start and %d buildings and %d units after %d ticks\n",
                   b0, u0, b1, u1, tick);
            printf("RA2B|census|ticks=%d|ai_buildings0=%d|ai_buildings=%d|ai_units0=%d|ai_units=%d|objects0=%d|objects=%d"
                   "|fight_peak=%d|fight_ticks=%d\n",
                   tick, b0, b1, u0, u1, objs0, count_lines(da, "OBJ|", NULL), fight_peak, fight_ticks);
        }
        free(da);
        free(db);
        if (!contA) {
            printf("  the scenario ended itself at tick %d, on both instances\n", tick);
            break;
        }
        if ((tick % 1000) == 0) {
            printf("  %d ticks identical\n", tick);
        }
    }
    printf("RA2B|agree|ticks=%d\n", tick > ticks ? ticks : tick);

    /* ---- 4. THE CONTROL: one instance alone is changed, and the comparison must see it ----
       Damage applied to A only through OpenCNC3D_Hurt_Object. Without this the agreement above
       could equally mean the comparison was blind. It leaves the worlds different, so it is last. */
    {
        char* dump = NULL;
        int id = -1, t, diverged = 0;
        const char* p;
        if (!agree(&A, &B, "before the control", &dump)) {
            return (1);
        }
        /* A vehicle or soldier standing on the map (not one still inside its factory) with
           strength to lose. */
        int rtti = -1;
        static const char* const kinds[2] = {"OBJ|UNIT|", "OBJ|INFANTRY|"};
        static const int kind_rtti[2] = {RB_RTTI_UNIT, RB_RTTI_INFANTRY};
        int k;
        for (k = 0; k < 2 && id < 0; k++) {
            p = dump;
            while ((p = strstr(p, kinds[k])) != NULL) {
                const char* e = strchr(p, '\n');
                char line[1024];
                size_t n = e ? (size_t)(e - p) : strlen(p);
                const char* q;
                if (n >= sizeof(line)) {
                    n = sizeof(line) - 1;
                }
                memcpy(line, p, n);
                line[n] = '\0';
                q = strstr(line, "|str=");
                if (strstr(line, "|limbo=0|") != NULL && q != NULL && atoi(q + 5) > 20 && strstr(line, "|id=") != NULL) {
                    id = atoi(strstr(line, "|id=") + 4);
                    rtti = kind_rtti[k];
                    break;
                }
                p += n;
            }
        }
        free(dump);
        int window = 5;
        const char* what = "ten points of damage to one object";
        if (id < 0 || !A.Hurt(rtti, id, 10)) {
            /* A base with no army yet: the house of the third seat is told to die, on A only. */
            if (!A.Blowup(3ULL)) {
                printf("FAILED: neither a hurt object nor a doomed house could be applied as the one-sided control\n");
                return (1);
            }
            window = 300;
            what = "telling one house to die";
        }
        for (t = 0; t < window && !diverged; t++) {
            size_t la = 0, lb = 0;
            char *da, *db;
            A.Advance(1ULL);
            B.Advance(2ULL);
            da = capture_dump(&A, &la);
            db = capture_dump(&B, &lb);
            if (da && db && (la != lb || memcmp(da, db, la) != 0)) {
                diverged = 1;
            }
            free(da);
            free(db);
        }
        if (!diverged) {
            printf("FAILED: %s on A alone did NOT make the dumps differ,\n", what);
            printf("        so the agreement above proves nothing.\n");
            return (1);
        }
        printf("  control: %s on A alone made the two dumps differ (%d tick(s) later)\n", what, t);
        printf("RA2B|control|diverged=1\n");
    }

    printf("\n  PASSED. Two independent Red Alert brains in lockstep played %s with %d seats: an order\n", scenario, 2 + ai);
    printf("          crossed between them and executed on both; then, with every seat handed to the computer\n");
    printf("          and each instance still naming a different local player, %d ticks byte identical after\n",
           tick > ticks ? ticks : tick);
    printf("          every tick, while the computer players built and fought.\n");
    return (0);
}
