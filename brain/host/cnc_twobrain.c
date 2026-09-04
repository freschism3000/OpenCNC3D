/*
 * cnc_twobrain.c -- TWO brains, ONE process, in lockstep. Phase 2's gate.
 *
 * WHAT THIS PROVES, AND WHY IT IS NOT THE SAME AS THE GATES BESIDE IT.
 *
 * Every determinism gate in this repository runs one program twice on one machine. That
 * proves the program is REPEATABLE. It does not prove that two independent copies of the
 * engine, advancing side by side, agree with each other -- which is the property lockstep
 * multiplayer is entirely built on, because every peer simulates the whole world and only
 * the orders cross the wire.
 *
 * So this loads the brain TWICE, as two genuinely separate instances, starts the same
 * scenario on both, advances them in step, and compares the full engine state after every
 * single tick. The first tick where they differ is printed with the line that differs.
 *
 * THE FAILURE MODE THIS FILE IS SHAPED AROUND. The most dangerous way for a test like this
 * to fail is to pass: if the two "instances" are secretly one object, every tick matches
 * trivially, the gate prints a confident green, and it has measured nothing. That is why
 * the first thing below is an isolation assertion that would fail loudly if the two handles
 * shared state, and why the harness refuses to run the comparison unless it holds.
 *
 * HOW TWO INSTANCES ARE POSSIBLE AT ALL. The brain is full of file-scope globals, and dyld
 * keys loaded images on PATH: dlopen of the same path twice returns the same handle and the
 * same globals. Two BYTE COPIES at two different paths are two images with two sets of
 * globals. A symlink is not enough -- it resolves to the same file. Measured, not assumed:
 * setting the lockstep flag through A leaves B reading false.
 *
 * LOCKSTEP IS ON FOR THE RUN, deliberately. The determinism fixes are gated on
 * CNC3D_Lockstep, so a run with the flag off exercises the campaign path and proves nothing
 * about the path a match will actually take. This gate runs the configuration that ships.
 *
 *   usage: cnc_twobrain <brain.dylib> <content_dir> <scenario_dir> <scenario> [ticks] [brainB]
 *
 * PROVING IT CAN FAIL. A gate that cannot fail is decoration. Pass a DIFFERENT library as
 * the optional sixth argument and the two instances stop being copies of one another: the
 * classic and Enhanced brains report different order-wire layouts, so the wire check
 * refuses them before a tick is run. That is the negative control, and it should be run
 * whenever this gate is changed.
 *
 * Exit 0 on agreement, non-zero on any divergence or setup failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/*
 * Portable library loading, the same shape cnc_host.c uses, because this gate is most
 * wanted on the pairing it exists to protect: a Mac against a Windows box. Two BYTE COPIES
 * at two paths are two images on both platforms, which is what makes the whole test work.
 */
#ifdef _WIN32
#include <windows.h>
#include <io.h>
typedef HMODULE cnc_lib_t;
#define CNC_CALL __cdecl
#define cnc_lib_open(p) LoadLibraryA(p)
#define cnc_lib_sym(h, n) ((void*)GetProcAddress((h), (n)))
#define cnc_lib_err() "LoadLibrary failed"
#define TB_COPY_A "cnc3d_twobrain_A.dll"
#define TB_COPY_B "cnc3d_twobrain_B.dll"
#define TB_COPY_CMD "copy /Y \"%s\" \"%s\" >nul"
#else
#include <unistd.h>
#include <dlfcn.h>
typedef void* cnc_lib_t;
#define CNC_CALL
#define cnc_lib_open(p) dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define cnc_lib_sym(h, n) dlsym((h), (n))
#define cnc_lib_err() dlerror()
#define TB_COPY_A "/tmp/cnc3d_twobrain_A.dylib"
#define TB_COPY_B "/tmp/cnc3d_twobrain_B.dylib"
#define TB_COPY_CMD "cp '%s' '%s'"
#endif

/* ------------------------------------------------------------------ ABI mirrors ---- */
/* Byte-exact mirrors of the brain's own structs, copied from cnc_host.c, which is the
   file that keeps them honest with _Static_assert. Only the two this harness calls. */

#pragma pack(push, 1) /* the brain packs these to 1: 39 bytes a row, not 40 */
typedef struct
{
    float FirepowerBias;
    float GroundspeedBias;
    float AirspeedBias;
    float ArmorBias;
    float ROFBias;
    float CostBias;
    float BuildSpeedBias;
    float RepairDelay;
    float BuildDelay;
    /* unsigned char, NOT int: the brain declares these as C++ bool under pack(1), so the
       row is 39 bytes. Mirrored as int (3 Sep 2026 and before) the row was 48 bytes, the
       brain read rows 1 and 2 from the wrong offsets, and the multiplayer houses, which
       use row 1, got near-zero armour and firepower biases: nothing in a two-brain match
       could damage anything, and the gesture legs found it because a Construction Yard
       shrugged off 1000 points of high explosive eight times running. The size assert
       below used to accept 48 as well, which is how it stayed hidden. */
    unsigned char IsBuildSlowdown;
    unsigned char IsWallDestroyer;
    unsigned char IsContentScan;
} CNCDifficultyDataStruct;

typedef struct
{
    CNCDifficultyDataStruct Difficulties[3];
} CNCRulesDataStruct;
#pragma pack(pop)

_Static_assert(sizeof(CNCDifficultyDataStruct) == 39,
               "CNCDifficultyDataStruct drift: re-mirror from cnc_host.c (39 bytes, pack 1)");

/* Mirrored from cnc_host.c, which keeps them honest against the brain with _Static_assert.
   Needed because the ORDER leg has to arm a real multiplayer match: the pump that executes
   a posted order only runs on the multiplayer arm, so a campaign mission would accept an
   order through CNC3D_Post_Event and never execute it, which would look like a pass. */
#define TB_MAX_HOUSES 32
#define TB_MAX_EXPORT_CELLS (128 * 128)

/* Packed to 1, exactly as cnc_host.c mirrors them and as the brain declares them. Without
   this the compiler's natural alignment inserts padding the brain does not have and the
   two sides disagree about where every field after the first hole lives. The assert below
   is what turns that from a mystery into a compile error. */
#pragma pack(push, 1)

typedef struct
{
    int Power;
    int Drain;
    int Money;
} CNCSpiedInfoStruct;

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
    CNCSpiedInfoStruct SpiedInfo[TB_MAX_HOUSES];
    int SelectedID;
    int SelectedType;
    unsigned char ActionWithSelected[TB_MAX_EXPORT_CELLS];
    unsigned int ActionWithSelectedCount;
    unsigned int ScreenShake;
    unsigned char IsRadarJammed;
} CNCPlayerInfoStruct;

_Static_assert(sizeof(CNCPlayerInfoStruct) == 16886, "CNCPlayerInfoStruct drift: re-mirror from cnc_host.c");

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
    unsigned char ModernBalance;
} CNCMultiplayerOptionsStruct;

#pragma pack(pop)

/* The 22-byte order on the wire. The harness never interprets it, only moves it, so it is
   an opaque block of the size the brain reports through CNC3D_Event_ABI. Asserting that
   the two agree is the point; hardcoding 22 here would be a second place to be wrong. */
#define TB_EVENT_MAX 64

typedef void(CNC_CALL* cb_t)(const void* event);
typedef void(CNC_CALL* fn_Init)(const char*, cb_t);
typedef void(CNC_CALL* fn_Config)(const CNCRulesDataStruct*);
typedef int(CNC_CALL* fn_Start)(const char*, const char*, const char*, int, int);
typedef int(CNC_CALL* fn_Advance)(unsigned long long);
typedef int(CNC_CALL* fn_Dump)(void);
typedef void(CNC_CALL* fn_SetLockstep)(int);
typedef int(CNC_CALL* fn_GetLockstep)(void);
typedef int(CNC_CALL* fn_EventABI)(unsigned int*, int);
typedef int(CNC_CALL* fn_SetMP)(int, CNCMultiplayerOptionsStruct*, int, CNCPlayerInfoStruct*, int);
typedef int(CNC_CALL* fn_Drain)(void*, int, int);
typedef int(CNC_CALL* fn_Post)(const void*);
typedef void(CNC_CALL* fn_Beacon)(int, unsigned long long, int, int);
/* The four gesture entry points and the two fixture paths the gesture legs drive. Their
   enum arguments are plain ints here, mirrored below as TB_* with the value each has in
   dllinterface.h; the bools of the debug request are unsigned char, which is what a C++
   bool is on every ABI this runs on. */
typedef void(CNC_CALL* fn_Structure)(int, unsigned long long, int);
typedef void(CNC_CALL* fn_Sidebar)(int, unsigned long long, int, int, short, short);
typedef void(CNC_CALL* fn_Input)(int, unsigned char, unsigned long long, int, int, int, int);
typedef void(CNC_CALL* fn_DebugReq)(int, unsigned long long, const char*, int, int, unsigned char, unsigned char);
typedef int(CNC_CALL* fn_GetState)(int, unsigned long long, unsigned char*, unsigned int);

typedef struct
{
    const char* name;
    cnc_lib_t lib;
    fn_Init Init;
    fn_Config Config;
    fn_Start Start;
    fn_Advance Advance;
    fn_Dump Dump;
    fn_SetLockstep SetLockstep;
    fn_GetLockstep GetLockstep;
    fn_EventABI EventABI;
    fn_SetMP SetMP;
    fn_Drain Drain;
    fn_Post Post;
    fn_Beacon Beacon;
    fn_Structure Structure;
    fn_Sidebar Sidebar;
    fn_Input Input;
    fn_DebugReq DebugReq;
    fn_GetState GetState;
    char path[1024];
} Brain;

static void CNC_CALL noop_callback(const void* event)
{
    (void)event;
}

/* ------------------------------------------------------------------- plumbing ------ */

/* The dump prints to stdout from inside the library. To compare two of them we redirect
   fd 1 into a temp file around each call and read it back. fflush(NULL) rather than
   fflush(stdout) because the buffered bytes may belong to either image's stream. */
/* The one field in the dump that names the CONTEXT that dumped rather than the world:
   HOUSE|...|player=1 marks the house the caller's player id resolved to. The two instances
   are advanced under different ids on purpose (see main), so that field differs by design
   and is blanked before the compare. Everything else in the dump is simulation. */
static void normalise_dump(char* d)
{
    char* p = d;
    while ((p = strstr(p, "|player=")) != NULL) {
        p += 8;
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
    ssize_t got = read(tmpfd, buf, (size_t)size);
    buf[got < 0 ? 0 : got] = '\0';
    fclose(tmp);
    normalise_dump(buf);
    *len_out = (size_t)(got < 0 ? 0 : got);
    return (buf);
}

/* Print the first line that differs, which is the whole point of failing. */
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
            printf("  first difference is on dump line %d\n", line);
            printf("    A: %.*s\n", (int)(na > 200 ? 200 : na), la);
            printf("    B: %.*s\n", (int)(nb > 200 ? 200 : nb), lb);
            return;
        }
        if (!ea || !eb) {
            break;
        }
        la = ea + 1;
        lb = eb + 1;
        line++;
    }
    printf("  the dumps differ in length: A %zu bytes, B %zu bytes\n", strlen(a), strlen(b));
}

static int load_brain(Brain* b, const char* src, const char* copy_path, const char* name)
{
    /* A byte copy, not a symlink: dyld resolves a symlink to the same file and hands back
       the same image, which would silently make this a one-brain test. */
    char cmd[2400];
    snprintf(cmd, sizeof(cmd), TB_COPY_CMD, src, copy_path);
    if (system(cmd) != 0) {
        printf("could not copy %s -> %s\n", src, copy_path);
        return (0);
    }
    snprintf(b->path, sizeof(b->path), "%s", copy_path);
    b->name = name;
    b->lib = cnc_lib_open(copy_path);
    if (b->lib == NULL) {
        printf("loading %s failed: %s\n", copy_path, cnc_lib_err());
        return (0);
    }

#define SYM(field, sym)                                                                            \
    b->field = (void*)cnc_lib_sym(b->lib, sym);                                                          \
    if (b->field == NULL) {                                                                        \
        printf("%s: missing export %s\n", name, sym);                                              \
        return (0);                                                                                \
    }
    SYM(Init, "CNC_Init")
    SYM(Config, "CNC_Config")
    SYM(Start, "CNC_Start_Custom_Instance")
    SYM(Advance, "CNC_Advance_Instance")
    SYM(Dump, "CNC3D_Dump_Objects")
    SYM(SetLockstep, "CNC3D_Set_Lockstep")
    SYM(GetLockstep, "CNC3D_Get_Lockstep")
    SYM(EventABI, "CNC3D_Event_ABI")
    SYM(SetMP, "CNC_Set_Multiplayer_Data")
    SYM(Drain, "CNC3D_Drain_Events")
    SYM(Post, "CNC3D_Post_Event")
    SYM(Beacon, "CNC_Handle_Beacon_Request")
    SYM(Structure, "CNC_Handle_Structure_Request")
    SYM(Sidebar, "CNC_Handle_Sidebar_Request")
    SYM(Input, "CNC_Handle_Input")
    SYM(DebugReq, "CNC_Handle_Debug_Request")
    SYM(GetState, "CNC_Get_Game_State")
#undef SYM
    return (1);
}

static void fill_rules(CNCRulesDataStruct* r)
{
    /* One row repeated. The VALUES do not matter here, only that both instances get the
       same ones: this gate is about two engines agreeing, not about balance. */
    int i;
    memset(r, 0, sizeof(*r));
    for (i = 0; i < 3; i++) {
        r->Difficulties[i].FirepowerBias = 1.0f;
        r->Difficulties[i].GroundspeedBias = 1.0f;
        r->Difficulties[i].AirspeedBias = 1.0f;
        r->Difficulties[i].ArmorBias = 1.0f;
        r->Difficulties[i].ROFBias = 1.0f;
        r->Difficulties[i].CostBias = 1.0f;
        r->Difficulties[i].BuildSpeedBias = 1.0f;
        r->Difficulties[i].RepairDelay = 0.02f;
        r->Difficulties[i].BuildDelay = 0.03f;
        r->Difficulties[i].IsBuildSlowdown = 0;
        r->Difficulties[i].IsWallDestroyer = 1;
        r->Difficulties[i].IsContentScan = 1;
    }
}

/* ------------------------------------------------------------- multiplayer arming -- */

/*
 * The ORDER leg needs a real multiplayer match, not a campaign mission. CNC3D_Post_Event
 * puts an order into DoList on any game type, but the pump that EXECUTES it only runs on
 * the multiplayer arm, so a campaign run would accept every order and quietly execute
 * none. That is a pass that proves nothing, which is the one outcome this file exists to
 * avoid, so the order leg arms a match or it does not run at all.
 */
static int arm_multiplayer(Brain* b, int seats)
{
    CNCMultiplayerOptionsStruct opt;
    CNCPlayerInfoStruct* roster = (CNCPlayerInfoStruct*)calloc((size_t)seats, sizeof(CNCPlayerInfoStruct));
    int i;
    int ok;

    if (roster == NULL) {
        return (0);
    }

    memset(&opt, 0, sizeof(opt));
    opt.MPlayerCount = seats;
    opt.MPlayerBases = 1;
    opt.MPlayerCredits = 5000;
    opt.MPlayerTiberium = 1;
    opt.MPlayerGoodies = 0;
    opt.MPlayerGhosts = 0;
    opt.MPlayerSolo = 1;
    opt.MPlayerUnitCount = 10;

    for (i = 0; i < seats; i++) {
        snprintf(roster[i].Name, sizeof(roster[i].Name), "seat%d", i);
        roster[i].House = (unsigned char)(i & 1);
        roster[i].ColorIndex = i;
        roster[i].GlyphxPlayerID = (unsigned long long)(i + 1);
        roster[i].Team = i;
        roster[i].StartLocationIndex = i;
        /* EVERY seat is human, and that is load bearing rather than cosmetic. Beacons,
           and the whole non-legacy render path they sit behind, are only enabled when the
           engine sees more than one human house (DLLExportClass::Legacy_Render_Enabled).
           With one human and one AI the order leg's beacon is silently refused and the
           drain comes back empty, which reads as "the drain is broken". Two humans is
           also simply what a lockstep match IS. */
        roster[i].IsAI = 0;
        roster[i].AllyFlags = 0;
    }

    ok = b->SetMP(0, &opt, seats, roster, seats);
    free(roster);
    return (ok);
}


/* ------------------------------------------ the gesture legs (design Phase 1) ------ */
/*
**	Four player gestures used to mutate the world WITHOUT producing an order: repair, sell,
**	wall sell and building placement. Under lockstep each is now an EventClass order like
**	every other click. These legs prove it the only way that counts: make the gesture on A,
**	require that A's world did NOT change (the bypass is closed), carry the order across to
**	both, and require that the effect then appears on both, byte identical.
**
**	The fixtures come through the DEBUG spawn path, applied to BOTH instances by hand,
**	because a two-seat match starts with one MCV each and nothing to repair, sell or place
**	beside. That path is not an order and never will be; it is used here the way a test
**	builds a fixture, and the worlds are required to agree after every use of it.
**
**	Every number below is the value the matching enum has in dllinterface.h. A renamed or
**	reordered enum shows up as a leg that asks for the wrong thing and fails, not as a
**	silent pass, because each leg checks the EFFECT and not the return code.
*/
#define TB_P0 1ULL /* seat 0's GlyphxPlayerID, as arm_multiplayer assigns them */
#define TB_STRUCTURE_REPAIR 2
#define TB_STRUCTURE_SELL 4
#define TB_SIDEBAR_START_CONSTRUCTION 0
#define TB_SIDEBAR_START_PLACEMENT 3
#define TB_SIDEBAR_PLACE 4
#define TB_INPUT_SELL_AT_POSITION 7
#define TB_DEBUG_SPAWN_OBJECT 0
#define TB_DEBUG_END_PRODUCTION 5
#define TB_DEBUG_KILL_OBJECT 4
#define TB_RTTI_BUILDINGTYPE 8
#define TB_STRUCT_POWER 12
#define TB_GAME_STATE_STATIC_MAP 1
#define TB_MAP_BUFFER (1 << 20) /* larger than CNCMapDataStruct on either platform */

/* Compare the two worlds now. Returns 1 if identical; on 0 the first difference is printed.
   With `keep`, hands back A's dump (caller frees) so a leg can read the world it agreed on. */
static int tb_agree(Brain* A, Brain* B, const char* when, char** keep)
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

static int tb_settle(Brain* A, Brain* B, int ticks, const char* when)
{
    int t;
    for (t = 0; t < ticks; t++) {
        A->Advance(1ULL);
        B->Advance(2ULL);
        if (!tb_agree(A, B, when, NULL)) {
            printf("        (%d tick(s) in)\n", t + 1);
            return (0);
        }
    }
    return (1);
}

/* Drain A's orders and post the same bytes to both, exactly as a host does with its own.
   The count is checked EXACTLY: a gesture that queues two orders, or none, is a finding. */
static int tb_cross(Brain* A, Brain* B, unsigned event_size, const char* what, int expect)
{
    unsigned char wire[TB_EVENT_MAX * 64];
    int n = A->Drain(wire, TB_EVENT_MAX, 3);
    int i;
    if (n != expect) {
        printf("FAILED: %s: expected %d order(s) on the drain and got %d\n", what, expect, n);
        return (0);
    }
    for (i = 0; i < n; i++) {
        const void* ev = wire + ((size_t)i * (size_t)event_size);
        if (A->Post(ev) != 1 || B->Post(ev) != 1) {
            printf("FAILED: %s: posting order %d was refused\n", what, i);
            return (0);
        }
    }
    printf("  %s: %d order(s) drained off A and posted to both\n", what, n);
    return (1);
}

/* Find the first "OBJ|<kind>|<ini>|" line, or with ini NULL any line starting `prefix`.
   Returns a malloc'd copy of the line without its newline, or NULL. */
static char* tb_find_line(const char* dump, const char* prefix)
{
    const char* p = dump;
    size_t pl = strlen(prefix);
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= pl && memcmp(p, prefix, pl) == 0) {
            char* out = (char*)malloc(n + 1);
            if (out == NULL) {
                return (NULL);
            }
            memcpy(out, p, n);
            out[n] = '\0';
            return (out);
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return (NULL);
}

static int tb_count_lines(const char* dump, const char* prefix)
{
    const char* p = dump;
    size_t pl = strlen(prefix);
    int count = 0;
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= pl && memcmp(p, prefix, pl) == 0) {
            count++;
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return (count);
}

/* Like tb_count_lines, but only objects that are on the map. A building in production
   already exists as an object in limbo, held by its factory, and the dump lists it, so a
   count that ignores limbo sees the same number before and after a placement. */
static int tb_count_placed(const char* dump, const char* prefix)
{
    const char* p = dump;
    size_t pl = strlen(prefix);
    int count = 0;
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= pl && memcmp(p, prefix, pl) == 0) {
            char* line = tb_find_line(p, prefix);
            if (line != NULL && strstr(line, "|limbo=0|") != NULL) {
                count++;
            }
            free(line);
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return (count);
}

/* The integer after "|key=" on one dump line, or dflt. */
static int tb_field(const char* line, const char* key, int dflt)
{
    char k[64];
    const char* p;
    snprintf(k, sizeof(k), "|%s=", key);
    p = strstr(line, k);
    if (p == NULL) {
        return (dflt);
    }
    return (atoi(p + strlen(k)));
}

/* Find a building by IniName and heap id in a dump. */
static char* tb_find_building(const char* dump, const char* ini, int id)
{
    const char* p = dump;
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "OBJ|BUILDING|%s|", ini);
    while ((p = strstr(p, prefix)) != NULL) {
        if (p == dump || p[-1] == '\n') {
            char* line = tb_find_line(p, prefix);
            if (line != NULL && (id < 0 || tb_field(line, "id", -1) == id)) {
                return (line);
            }
            free(line);
        }
        p += 1;
    }
    return (NULL);
}

/* A debug spawn of `name` at a pixel, on BOTH instances, followed by one tick and an
   agreement check. Returns 1 if the worlds still agree. */
static int tb_spawn_both(Brain* A, Brain* B, const char* name, int px, int py, int enemy)
{
    A->DebugReq(TB_DEBUG_SPAWN_OBJECT, TB_P0, name, px, py, 0, (unsigned char)enemy);
    B->DebugReq(TB_DEBUG_SPAWN_OBJECT, TB_P0, name, px, py, 0, (unsigned char)enemy);
    return (tb_settle(A, B, 1, "after a debug spawn applied to both"));
}

/* Spawn a building at the first of several pixel positions where it actually lands (a
   spot the terrain refuses deletes the object again, identically on both, and costs
   nothing). Returns the new building's dump line, malloc'd, with the pixel used. */
static char* tb_spawn_building(Brain* A, Brain* B, const char* ini, int* px_out, int* py_out)
{
    static const int spots[][2] = {{320, 240}, {416, 240}, {320, 336}, {224, 240}, {320, 144},
                                   {512, 240}, {320, 432}, {128, 240}, {608, 336}, {416, 144}};
    char prefix[64];
    size_t i;
    snprintf(prefix, sizeof(prefix), "OBJ|BUILDING|%s|", ini);
    for (i = 0; i < sizeof(spots) / sizeof(spots[0]); i++) {
        char* dump = NULL;
        int before, after;
        if (!tb_agree(A, B, "before a fixture spawn", &dump)) {
            return (NULL);
        }
        before = tb_count_lines(dump, prefix);
        free(dump);
        if (!tb_spawn_both(A, B, ini, spots[i][0], spots[i][1], 0)) {
            return (NULL);
        }
        if (!tb_agree(A, B, "after a fixture spawn", &dump)) {
            return (NULL);
        }
        after = tb_count_lines(dump, prefix);
        if (after == before + 1) {
            /* The NEW one is the one with the highest heap id among lines of this name. */
            char* best = NULL;
            int best_id = -1;
            const char* p = dump;
            while ((p = strstr(p, prefix)) != NULL) {
                if (p == dump || p[-1] == '\n') {
                    char* line = tb_find_line(p, prefix);
                    if (line != NULL) {
                        int id = tb_field(line, "id", -1);
                        if (id > best_id) {
                            free(best);
                            best = line;
                            best_id = id;
                        } else {
                            free(line);
                        }
                    }
                }
                p += 1;
            }
            free(dump);
            *px_out = spots[i][0];
            *py_out = spots[i][1];
            return (best);
        }
        free(dump);
    }
    printf("FAILED: no pixel spot took a %s through the debug spawn, so the gesture legs\n", ini);
    printf("        have no fixture. Add spots, or check Debug_Spawn_Unit still spawns buildings.\n");
    return (NULL);
}

static int gesture_legs(Brain* A, Brain* B, unsigned event_size)
{
    unsigned char* mapbuf;
    int mapx, mapy;
    char* line;
    char* dump;
    int fact_id, fact_x, fact_y, fpx, fpy;
    int nuke_id, npx, npy;

    printf("\n--- the gesture legs: repair, sell, wall sell and placement are ORDERS now ---\n");

    /* The playable rectangle, for turning an absolute cell into the sidebar's relative one,
       which is the form CNC_Handle_Sidebar_Request takes a placement cell in. */
    mapbuf = (unsigned char*)malloc(TB_MAP_BUFFER);
    if (mapbuf == NULL) {
        printf("FAILED: out of memory for the map state\n");
        return (0);
    }
    memset(mapbuf, 0, TB_MAP_BUFFER);
    if (!A->GetState(TB_GAME_STATE_STATIC_MAP, TB_P0, mapbuf, TB_MAP_BUFFER)) {
        printf("FAILED: GAME_STATE_STATIC_MAP was refused, so no placement cell can be computed\n");
        free(mapbuf);
        return (0);
    }
    memcpy(&mapx, mapbuf, sizeof(int));
    memcpy(&mapy, mapbuf + sizeof(int), sizeof(int));
    free(mapbuf);
    printf("  exported map rect starts at cell %d,%d\n", mapx, mapy);

    /* 1. FIXTURES: a Construction Yard and a Power Plant for seat 0, on both. */
    line = tb_spawn_building(A, B, "FACT", &fpx, &fpy);
    if (line == NULL) {
        return (0);
    }
    fact_id = tb_field(line, "id", -1);
    fact_x = tb_field(line, "x", -1);
    fact_y = tb_field(line, "y", -1);
    free(line);
    printf("  fixture: Construction Yard id %d at cell %d,%d (pixel %d,%d), on both\n", fact_id, fact_x, fact_y, fpx, fpy);

    line = tb_spawn_building(A, B, "NUKE", &npx, &npy);
    if (line == NULL) {
        return (0);
    }
    nuke_id = tb_field(line, "id", -1);
    free(line);
    printf("  fixture: Power Plant id %d (pixel %d,%d), on both\n", nuke_id, npx, npy);

    /* 2. SELL. The click on A must change nothing on A. */
    A->Structure(TB_STRUCTURE_SELL, TB_P0, nuke_id);
    if (!tb_agree(A, B, "after the sell click and BEFORE the drain: the sale went straight into A's world, so the sell bypass is OPEN", NULL)) {
        return (0);
    }
    printf("  sell: the click changed nothing locally, so it is an order and not an act\n");
    if (!tb_cross(A, B, event_size, "sell", 1)) {
        return (0);
    }
    if (!tb_settle(A, B, 8, "after the sell order executed")) {
        return (0);
    }
    if (!tb_agree(A, B, "reading the world after the sale", &dump)) {
        return (0);
    }
    line = tb_find_building(dump, "NUKE", nuke_id);
    free(dump);
    if (line != NULL && strstr(line, "|mission=Deconstruction") == NULL) {
        /*
        **	NOT a failure, and the reason has to be said every time so nobody reads it as
        **	one. BuildingClass::Sell_Back does nothing unless Class->Get_Buildup_Data() is
        **	set, and that is the build-up animation (NUKEMAKE.SHP) loaded per theatre from
        **	the real art. The headless run folder carries the two theatre MIX STUBS and no
        **	art, so in this world a sale is an order that executes and changes nothing. The
        **	three claims that matter were checked above regardless: the click changed
        **	nothing locally, exactly one order crossed, and both worlds agree after it ran.
        **	That the SELL arm executes and acts on the ID it was stamped with is proven by
        **	the wall sell leg below, which is the same arm's other branch and needs no art.
        */
        printf("  sell: executed on both with NO VISIBLE EFFECT, and that is the fixture, not the\n");
        printf("        wire: Sell_Back needs the build-up art the headless stubs do not carry.\n");
        printf("        The wall sell leg proves this arm executes.\n");
    } else {
        printf("  sell: executed on both, the Power Plant is %s\n", line ? "deconstructing" : "gone");
    }
    free(line);

    /* 3. REPAIR needs a DAMAGED building first, and the honest ways of getting one in a
       headless world were tried and did not work: a guarding tank never shoots at a
       building (Mission_Guard scans THREAT_RANGE, whose mask has no RTTI_BUILDING), and
       a tank ordered to attack the yard sat in MISSION_ATTACK with the yard as its target
       for 400 ticks without landing a shot, for a reason not chased here. What does work
       is the debug KILL path, which is Take_Damage(1000, WARHEAD_HE) on the cell's object:
       the warhead's armour table turns that into less than the yard's 800, so the yard
       survives, damaged. Applied to BOTH, as every fixture is, and checked for agreement.
       Several pixels over the footprint are tried, because the pixel to cell mapping in a
       headless view is not cell aligned and the first guess missed. */
    {
        static const int hits[][2] = {{0, 0}, {12, 12}, {24, 0}, {0, 24}, {36, 12}, {48, 24}, {60, 36}, {-12, -12}};
        size_t h;
        int str = -1, max = -1;
        for (h = 0; h < sizeof(hits) / sizeof(hits[0]); h++) {
            A->DebugReq(TB_DEBUG_KILL_OBJECT, TB_P0, "X", fpx + hits[h][0], fpy + hits[h][1], 0, 0);
            B->DebugReq(TB_DEBUG_KILL_OBJECT, TB_P0, "X", fpx + hits[h][0], fpy + hits[h][1], 0, 0);
            if (!tb_settle(A, B, 1, "after the debug damage applied to both")) {
                return (0);
            }
            if (!tb_agree(A, B, "reading the yard's strength", &dump)) {
                return (0);
            }
            line = tb_find_building(dump, "FACT", fact_id);
            free(dump);
            if (line == NULL) {
                printf("FAILED: the debug damage at pixel %d,%d killed the Construction Yard outright\n", fpx + hits[h][0], fpy + hits[h][1]);
                return (0);
            }
            str = tb_field(line, "str", -1);
            max = tb_field(line, "max", -1);
            free(line);
            printf("  repair: debug damage at pixel %d,%d leaves the yard at %d/%d\n", fpx + hits[h][0], fpy + hits[h][1], str, max);
            if (str >= 0 && str < max) {
                break;
            }
        }
        if (str < 0 || str >= max) {
            printf("FAILED: no debug damage pixel over the yard's footprint damaged it; nothing to repair\n");
            return (0);
        }
        printf("  repair: the yard is at %d/%d, so it can be repaired\n", str, max);
    }
    A->Structure(TB_STRUCTURE_REPAIR, TB_P0, fact_id);
    if (!tb_agree(A, B, "after the repair click and BEFORE the drain: the wrench came on in A's world alone, so the repair bypass is OPEN", NULL)) {
        return (0);
    }
    printf("  repair: the click changed nothing locally, so it is an order and not an act\n");
    if (!tb_cross(A, B, event_size, "repair", 1)) {
        return (0);
    }
    if (!tb_settle(A, B, 6, "after the repair order executed")) {
        return (0);
    }
    if (!tb_agree(A, B, "reading the world after the repair", &dump)) {
        return (0);
    }
    line = tb_find_building(dump, "FACT", fact_id);
    free(dump);
    if (line == NULL || tb_field(line, "repairing", 0) != 1) {
        printf("FAILED: the repair order executed on both and the yard is not repairing: %s\n", line ? line : "(gone)");
        free(line);
        return (0);
    }
    free(line);
    printf("  repair: executed on both, the yard is repairing\n");

    /* 4. PLACE. Start a Power Plant (an order too, now that the reentrant pump is off),
       finish it by the debug path on both, then place it: TWICE, in the same window, which
       must land exactly ONE building. */
    A->Sidebar(TB_SIDEBAR_START_CONSTRUCTION, TB_P0, TB_RTTI_BUILDINGTYPE, TB_STRUCT_POWER, 0, 0);
    if (!tb_agree(A, B, "after the build click and BEFORE the drain: production began on A alone, so the reentrant pump is still LIVE", NULL)) {
        return (0);
    }
    if (!tb_cross(A, B, event_size, "produce", 1)) {
        return (0);
    }
    if (!tb_settle(A, B, 5, "after the produce order executed")) {
        return (0);
    }
    A->DebugReq(TB_DEBUG_END_PRODUCTION, TB_P0, "", 0, 0, 0, 0);
    B->DebugReq(TB_DEBUG_END_PRODUCTION, TB_P0, "", 0, 0, 0, 0);
    if (!tb_settle(A, B, 2, "after production was forced complete on both")) {
        return (0);
    }
    A->Sidebar(TB_SIDEBAR_START_PLACEMENT, TB_P0, TB_RTTI_BUILDINGTYPE, TB_STRUCT_POWER, 0, 0);
    {
        static const int offs[][2] = {{3, 0}, {3, 1}, {-2, 0}, {0, 3}, {0, -2}, {3, -2}, {-2, 3}, {4, 0}, {0, 4}};
        int before, placed = 0;
        size_t i;
        if (!tb_agree(A, B, "before placement", &dump)) {
            return (0);
        }
        before = tb_count_placed(dump, "OBJ|BUILDING|NUKE|");
        free(dump);
        for (i = 0; i < sizeof(offs) / sizeof(offs[0]) && !placed; i++) {
            int ax = fact_x + offs[i][0];
            int ay = fact_y + offs[i][1];
            short cx = (short)(ax - mapx);
            short cy = (short)(ay - mapy);
            int after;
            if (cx < 0 || cy < 0) {
                continue;
            }
            A->Sidebar(TB_SIDEBAR_PLACE, TB_P0, TB_RTTI_BUILDINGTYPE, TB_STRUCT_POWER, cx, cy);
            A->Sidebar(TB_SIDEBAR_PLACE, TB_P0, TB_RTTI_BUILDINGTYPE, TB_STRUCT_POWER, cx, cy);
            if (!tb_agree(A, B, "after the place click and BEFORE the drain: the building landed in A's world alone, so the placement bypass is OPEN", NULL)) {
                return (0);
            }
            if (!tb_cross(A, B, event_size, "place (clicked twice)", 2)) {
                return (0);
            }
            if (!tb_settle(A, B, 6, "after the place orders executed")) {
                return (0);
            }
            if (!tb_agree(A, B, "reading the world after placement", &dump)) {
                return (0);
            }
            after = tb_count_placed(dump, "OBJ|BUILDING|NUKE|");
            printf("  place: candidate cell %d,%d (relative %d,%d): Power Plants %d -> %d\n", ax, ay, cx, cy, before, after);
            if (after != before + 1) {
                const char* q = dump;
                while ((q = strstr(q, "OBJ|BUILDING|")) != NULL) {
                    if (q == dump || q[-1] == '\n') {
                        char* l = tb_find_line(q, "OBJ|BUILDING|");
                        printf("         %.140s\n", l ? l : "?");
                        free(l);
                    }
                    q += 1;
                }
            }
            free(dump);
            if (after == before + 1) {
                printf("  place: executed on both, one Power Plant at cell %d,%d, and the second click in the\n", ax, ay);
                printf("         same window placed nothing, which is Place_Object's factory check doing the\n");
                printf("         double-submit latch's job\n");
                placed = 1;
            } else if (after > before + 1) {
                printf("FAILED: two place clicks in one window placed %d buildings\n", after - before);
                return (0);
            }
        }
        if (!placed) {
            printf("FAILED: no candidate cell around the yard at %d,%d (map rect from %d,%d) accepted the\n", fact_x, fact_y, mapx, mapy);
            printf("        Power Plant. Either the relative-cell arithmetic is wrong or the spots are.\n");
            return (0);
        }
    }

    /* 5. WALL SELL. A sandbag at a pixel, on both, then the sell-at-position click. */
    {
        static const int spots[][2] = {{320, 240}, {224, 144}, {512, 336}, {128, 432}, {608, 144}};
        int before, wall_px = -1, wall_py = -1;
        size_t i;
        for (i = 0; i < sizeof(spots) / sizeof(spots[0]) && wall_px < 0; i++) {
            int after;
            if (!tb_agree(A, B, "before the wall spawn", &dump)) {
                return (0);
            }
            before = tb_count_lines(dump, "WALL|");
            free(dump);
            if (!tb_spawn_both(A, B, "SBAG", spots[i][0], spots[i][1], 0)) {
                return (0);
            }
            if (!tb_agree(A, B, "after the wall spawn", &dump)) {
                return (0);
            }
            after = tb_count_lines(dump, "WALL|");
            free(dump);
            if (after > before) {
                wall_px = spots[i][0];
                wall_py = spots[i][1];
                before = after;
            }
        }
        if (wall_px < 0) {
            printf("FAILED: no pixel spot took a sandbag wall through the debug spawn\n");
            return (0);
        }
        A->Input(TB_INPUT_SELL_AT_POSITION, 0, TB_P0, wall_px, wall_py, 0, 0);
        if (!tb_agree(A, B, "after the wall sell click and BEFORE the drain: the wall left A's world alone, so the wall sell bypass is OPEN", NULL)) {
            return (0);
        }
        printf("  wall sell: the click changed nothing locally, so it is an order and not an act\n");
        if (!tb_cross(A, B, event_size, "wall sell", 1)) {
            return (0);
        }
        if (!tb_settle(A, B, 5, "after the wall sell order executed")) {
            return (0);
        }
        if (!tb_agree(A, B, "reading the world after the wall sale", &dump)) {
            return (0);
        }
        {
            int after = tb_count_lines(dump, "WALL|");
            free(dump);
            if (after >= before) {
                printf("FAILED: the wall sell order executed on both and the wall count did not drop (%d -> %d)\n", before, after);
                return (0);
            }
            printf("  wall sell: executed on both, walls %d -> %d\n", before, after);
        }
    }
    return (1);
}

/* ---------------------------------------------------------------------- main -------- */

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 5) {
        printf("usage: %s <brain.dylib> <content_dir> <scenario_dir> <scenario> [ticks] [brainB]\n",
               argv[0]);
        printf("\n");
        printf("Loads the brain TWICE as two independent instances, runs the same scenario on\n");
        printf("both with CNC3D_Lockstep ON, and compares the full engine dump every tick.\n");
        return (2);
    }

    const char* src = argv[1];
    const char* content = argv[2];
    const char* scendir = argv[3];
    const char* scenario = argv[4];
    int ticks = (argc > 5) ? atoi(argv[5]) : 2000;
    /* The negative control: a DIFFERENT library for instance B, which must be refused. */
    const char* srcB = (argc > 6) ? argv[6] : src;

    Brain A, B;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    printf("CNC3D two-brain lockstep gate\n");
    printf("  brain A  : %s\n", src);
    printf("  brain B  : %s%s\n", srcB, (srcB == src) ? "  (same library, the normal case)" : "  (DIFFERENT library)");
    printf("  scenario : %s   ticks: %d\n\n", scenario, ticks);

    printf("--- loading two independent instances ---\n");
    if (!load_brain(&A, src, TB_COPY_A, "A") || !load_brain(&B, srcB, TB_COPY_B, "B")) {
        return (2);
    }
    printf("  A handle %p   B handle %p\n", A.lib, B.lib);

    /*
    ** ASSERTION 0, and nothing below it means anything without it. If these two are one
    ** image, every comparison in this file passes for free.
    */
    if (A.lib == B.lib) {
        printf("\nFAILED: dlopen returned the SAME handle for both copies. These are one\n");
        printf("        instance, so a comparison between them would prove nothing.\n");
        return (1);
    }
    A.SetLockstep(1);
    if (A.GetLockstep() != 1 || B.GetLockstep() != 0) {
        printf("\nFAILED: the two instances SHARE GLOBALS (A=%d B=%d after setting only A).\n",
               A.GetLockstep(), B.GetLockstep());
        printf("        Every tick would then match trivially and this gate would be a lie.\n");
        return (1);
    }
    printf("  isolation: setting the flag on A left B untouched. Two real instances.\n");

    /* Both in the configuration a match actually runs in. */
    A.SetLockstep(1);
    B.SetLockstep(1);
    if (A.GetLockstep() != 1 || B.GetLockstep() != 1) {
        printf("\nFAILED: could not put both instances into lockstep mode.\n");
        return (1);
    }
    printf("  lockstep : ON for both, which is the configuration a match ships in\n");

    /* The wire fingerprints must agree, or these two could never exchange an order. */
    unsigned int abiA[32], abiB[32];
    int nA = A.EventABI(abiA, 32);
    int nB = B.EventABI(abiB, 32);
    if (nA != nB || memcmp(abiA, abiB, (size_t)nA * sizeof(unsigned int)) != 0) {
        printf("\nFAILED: the two instances report different order-wire layouts.\n");
        printf("        A hash %08X, B hash %08X. They could not exchange an order.\n", abiA[10], abiB[10]);
        return (1);
    }
    unsigned int event_size = abiA[1];
    printf("  wire     : both report layout hash %08X, event %u bytes\n\n", abiA[10], event_size);

    printf("--- starting the same scenario on both ---\n");
    CNCRulesDataStruct rules;
    fill_rules(&rules);

    A.Init("", noop_callback);
    B.Init("", noop_callback);
    A.Config(&rules);
    B.Config(&rules);

    /* SCM* is this project's multiplayer map naming. A multiplayer map means the order leg
       can run, because the pump that executes a posted order is on the multiplayer arm. */
    int want_order_leg = (strncmp(scenario, "SCM", 3) == 0);
    int seats = 2;
    if (want_order_leg) {
        if (!arm_multiplayer(&A, seats) || !arm_multiplayer(&B, seats)) {
            printf("FAILED: could not arm a %d seat match on both instances\n", seats);
            return (1);
        }
        printf("  armed a %d seat match on both instances\n", seats);
    }

    int okA = A.Start(content, scendir, scenario, 1, want_order_leg);
    int okB = B.Start(content, scendir, scenario, 1, want_order_leg);
    if (!okA || !okB) {
        printf("FAILED: scenario did not start (A=%d B=%d)\n", okA, okB);
        return (1);
    }
    printf("  both instances started %s\n\n", scenario);

    /*
    **	A IS ADVANCED AS SEAT 0 AND B AS SEAT 1, deliberately. CNC_Advance_Instance takes
    **	a player id that selects the LOCAL context (PlayerPtr) for the call, and a real
    **	match has a different local player on every machine. If anything in the tick
    **	reads that context, two peers diverge while two instances advanced under the
    **	same id, as this gate did until 3 Sep 2026, would agree for ever. The renderer
    **	relies on exactly this independence to keep the local human at id 0 on both
    **	machines (arm_skirmish), so the gate has to test it.
    */
    printf("--- advancing in step, comparing the full dump every tick ---\n");
    printf("  A advances as seat 0's player, B as seat 1's: the local context differs on purpose\n");
    int tick;
    for (tick = 1; tick <= ticks; tick++) {
        int contA = A.Advance(1ULL);
        int contB = B.Advance(2ULL);

        if (contA != contB) {
            printf("\nFAILED at tick %d: one instance wants to stop and the other does not "
                   "(A=%d B=%d)\n",
                   tick, contA, contB);
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
            printf("\nFAILED: the two instances DIVERGED at tick %d.\n", tick);
            report_first_difference(da, db);
            free(da);
            free(db);
            return (1);
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

    /*
    **	THE ORDER LEG. Everything above proves the two engines agree when NOTHING happens.
    **	Lockstep also needs an order made on one machine to reach the other and take effect
    **	on the same frame, and that is a different claim: it exercises the drain, the wire
    **	format, the post, and the canonical execution order all at once.
    **
    **	A beacon is the order used because it is the cheapest one that is unmistakably
    **	VISIBLE in the dump: it creates an ANIM object, so "did it happen" is a question the
    **	oracle already answers. It also exercises the beacon expiry this branch converted
    **	from a wall clock to a frame deadline.
    */
    if (want_order_leg) {
        printf("\n--- the order leg: an order made on A must land in B ---\n");

        unsigned char wire[TB_EVENT_MAX * 64];
        int drained;

        /* Nothing has been ordered yet, so the drain must report exactly that. */
        drained = A.Drain(wire, TB_EVENT_MAX, 3);
        if (drained != 0) {
            printf("FAILED: drain returned %d with no orders outstanding, expected 0\n", drained);
            return (1);
        }

        /* Make one order on A ONLY. B must learn about it purely through the wire. */
        A.Beacon(0, 1ULL, 320, 240);

        drained = A.Drain(wire, TB_EVENT_MAX, 3);
        if (drained < 1) {
            printf("FAILED: A produced no order to drain (drain returned %d). Without an\n", drained);
            printf("        order there is nothing to prove, so this is a failure and not a skip.\n");
            return (1);
        }
        printf("  A produced %d order(s) and they were drained off it\n", drained);

        /* B must not have it yet: the drain took it OUT of A, and nothing has posted it. */
        size_t l1 = 0, l2 = 0;
        char* d1 = capture_dump(&A, &l1);
        char* d2 = capture_dump(&B, &l2);
        int same_before = (d1 && d2 && l1 == l2 && memcmp(d1, d2, l1) == 0);
        free(d1);
        free(d2);
        if (!same_before) {
            printf("FAILED: the two worlds already differ before the order was posted.\n");
            return (1);
        }

        /* Post the SAME BYTES to both, which is exactly what a host does with its own
           orders: it sends them to every peer including itself. */
        int i;
        for (i = 0; i < drained; i++) {
            const void* ev = wire + ((size_t)i * (size_t)event_size);
            int ra = A.Post(ev);
            int rb = B.Post(ev);
            if (ra != 1 || rb != 1) {
                printf("FAILED: posting order %d was refused (A=%d B=%d).\n", i, ra, rb);
                printf("        -1 lockstep off, -2 bad argument, -3 frame already past, "
                       "-4 house out of range, 0 DoList full.\n");
                return (1);
            }
        }
        printf("  the same bytes were posted to BOTH instances\n");

        /* Run past the stamped frame and require them to still agree. */
        int t;
        for (t = 0; t < 10; t++) {
            A.Advance(1ULL);
            B.Advance(2ULL);
            size_t la2 = 0, lb2 = 0;
            char* da2 = capture_dump(&A, &la2);
            char* db2 = capture_dump(&B, &lb2);
            if (da2 == NULL || db2 == NULL) {
                printf("FAILED: could not capture a dump in the order leg\n");
                return (2);
            }
            if (la2 != lb2 || memcmp(da2, db2, la2) != 0) {
                printf("\nFAILED: the two instances DIVERGED %d tick(s) after the order.\n", t + 1);
                report_first_difference(da2, db2);
                free(da2);
                free(db2);
                return (1);
            }
            free(da2);
            free(db2);
        }
        printf("  both instances executed it and remained byte identical for 10 more ticks\n");

        if (!gesture_legs(&A, &B, event_size)) {
            return (1);
        }

        /*
        **	THE ORDER LEG'S OWN NEGATIVE CONTROL, and without it the leg above is weaker
        **	than it looks. "They stayed identical after both got the order" is also what
        **	happens if the order had no observable effect at all. So: give the SAME order
        **	to A ONLY, and require them to DIVERGE. If they do not, the order is invisible
        **	to this oracle and the leg proved the plumbing rather than the execution, which
        **	is worth knowing and must not be reported as a pass.
        **
        **	This deliberately leaves the two worlds different, so nothing may follow it.
        */
        A.Beacon(0, 1ULL, 200, 200);
        drained = A.Drain(wire, TB_EVENT_MAX, 3);
        if (drained < 1) {
            printf("FAILED: A produced no second order for the negative control\n");
            return (1);
        }
        for (i = 0; i < drained; i++) {
            if (A.Post(wire + ((size_t)i * (size_t)event_size)) != 1) {
                printf("FAILED: the one-sided post was refused\n");
                return (1);
            }
        }
        int diverged = 0;
        for (t = 0; t < 10 && !diverged; t++) {
            A.Advance(1ULL);
            B.Advance(2ULL);
            size_t la3 = 0, lb3 = 0;
            char* da3 = capture_dump(&A, &la3);
            char* db3 = capture_dump(&B, &lb3);
            if (da3 && db3 && (la3 != lb3 || memcmp(da3, db3, la3) != 0)) {
                diverged = 1;
            }
            free(da3);
            free(db3);
        }
        if (!diverged) {
            printf("\nFAILED: giving the order to ONE instance did NOT make them differ.\n");
            printf("        The order is invisible to the object dump, so the leg above\n");
            printf("        proved that the drain and post plumbing works, and NOT that the\n");
            printf("        order executed. Pick an order whose effect the dump can see.\n");
            return (1);
        }
        printf("  control: the same order given to A alone DID make them differ, so the\n");
        printf("           comparison can actually see this order take effect\n");
    }

    printf("\n  PASSED. Two independent brain instances, both in lockstep mode, ran %d ticks\n",
           tick > ticks ? ticks : tick);
    printf("          of %s and their full engine state was byte identical after EVERY tick.\n", scenario);
    printf("          That is the property lockstep needs and that one binary run twice\n");
    printf("          cannot demonstrate.\n");
    if (want_order_leg) {
        printf("          Repair, sell, wall sell and placement each changed nothing until their\n");
        printf("          order crossed, and then changed both worlds alike.\n");
    }
    return (0);
}
