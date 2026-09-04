/*
 * CNC3D -- minimal headless host for the Tiberian Dawn "brain" (CNC_* C ABI).
 *
 * Loads TiberianDawn.dylib / TiberianDawn.dll at runtime, starts a custom (loose-file)
 * scenario, ticks it, and dumps the resulting game state in plain text.
 *
 * Dependencies: libdl + the C library. Nothing else. The only platform-specific code is
 * the three-line dynamic-loader shim below, which already has its Win32 (LoadLibrary)
 * branch written so the same file builds for the Win98 tier.
 *
 * ---------------------------------------------------------------------------------
 * ABI NOTE. Every struct below is a byte-exact C mirror of the C++ structs in
 * tiberiandawn/dllinterface.h, as built with -DMEGAMAPS and _MAX_FNAME=255/_MAX_EXT=8.
 * The sizes and offsets were measured from the real header (see layout_probe.cpp) and
 * are re-checked here with _Static_assert, so a header change breaks the build loudly
 * instead of silently mis-decoding the buffer.
 * ---------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* ================================ loader shim ==================================== */

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE cnc_lib_t;
#define CNC_CALL __cdecl
#define cnc_lib_open(p) LoadLibraryA(p)
#define cnc_lib_sym(h, n) ((void*)GetProcAddress((h), (n)))
static const char* cnc_lib_error(void)
{
    static char buf[64];
    snprintf(buf, sizeof(buf), "GetLastError()=%lu", (unsigned long)GetLastError());
    return buf;
}
#define DEFAULT_BRAIN "TiberianDawn.dll"
#else
#include <dlfcn.h>
typedef void* cnc_lib_t;
#define CNC_CALL
#define cnc_lib_open(p) dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define cnc_lib_sym(h, n) dlsym((h), (n))
#define cnc_lib_error() dlerror()
/* Relative to the working directory, matching the Windows default above. Override it
   with the CNC_BRAIN environment variable or the command line; an absolute path here
   builds an executable that only runs on the machine that compiled it. */
#define DEFAULT_BRAIN "brain/vanilla/build-native/tiberiandawn/TiberianDawn.dylib"
#endif

/* The engine's `bool` is 1 byte on every target we care about (Itanium C++ ABI and MSVC). */
typedef unsigned char cnc_bool;
typedef unsigned long long cnc_u64;
typedef long long cnc_i64;

/* ============================ mirrored ABI structs =============================== */

#define MAX_EXPORT_CELLS  (128 * 128) /* 16384 */
/* _MAX_FNAME + _MAX_EXT, AND IT IS NOT THE SAME NUMBER ON EVERY PLATFORM.
 *
 * These are Microsoft CRT limits. On Windows they come from <stdlib.h> as 256 and 256,
 * so the field is 512 bytes; off Windows the project supplies 255 and 8, so it is 263.
 * CNCMapDataStruct carries ScenarioName inline, so this single constant moves the size
 * of the biggest struct in the ABI (590400 on Windows, 590123 elsewhere) and every
 * offset after it. Hardcoding the non-Windows number is what stopped this file
 * compiling for Windows at all, which was a useful accident: had it compiled, it would
 * have mis-decoded every field past the scenario name and reported an empty map.
 *
 * Measured, not assumed: both numbers were read back out of object files compiled with
 * the DLL's own defines and with the renderer's, and the two agree on each platform. */
#if defined(_WIN32)
#define CNC_MAX_FNAME_EXT (256 + 256)
#else
#define CNC_MAX_FNAME_EXT (255 + 8)
#endif
#define MAX_OCCUPY_CELLS  36
#define MAX_OBJECT_PIPS   18
#define MAX_OBJECT_LINES  3
#define MAX_HOUSES        32
#define CNC_OBJECT_ASSET_NAME_LENGTH 16

/* GameStateRequestEnum */
enum
{
    GAME_STATE_NONE = 0,
    GAME_STATE_STATIC_MAP,
    GAME_STATE_DYNAMIC_MAP,
    GAME_STATE_LAYERS,
    GAME_STATE_SIDEBAR,
    GAME_STATE_PLACEMENT,
    GAME_STATE_SHROUD,
    GAME_STATE_OCCUPIER,
    GAME_STATE_PLAYER_INFO
};

/* EventCallbackType */
enum
{
    CALLBACK_EVENT_INVALID = -1,
    CALLBACK_EVENT_SOUND_EFFECT = 0,
    CALLBACK_EVENT_SPEECH,
    CALLBACK_EVENT_GAME_OVER,
    CALLBACK_EVENT_DEBUG_PRINT,
    CALLBACK_EVENT_MOVIE,
    CALLBACK_EVENT_MESSAGE,
    CALLBACK_EVENT_UPDATE_MAP_CELL,
    CALLBACK_EVENT_ACHIEVEMENT,
    CALLBACK_EVENT_STORE_CARRYOVER_OBJECTS,
    CALLBACK_EVENT_SPECIAL_WEAPON_TARGETTING,
    CALLBACK_EVENT_BRIEFING_SCREEN,
    CALLBACK_EVENT_CENTER_CAMERA,
    CALLBACK_EVENT_PING
};

#pragma pack(push, 1)

typedef struct
{
    char TemplateTypeName[32];
    int IconNumber;
} CNCStaticCellStruct;

typedef struct
{
    int MapCellX;
    int MapCellY;
    int MapCellWidth;
    int MapCellHeight;
    int OriginalMapCellX;
    int OriginalMapCellY;
    int OriginalMapCellWidth;
    int OriginalMapCellHeight;
    int Theater; /* CnCTheaterType */
    char ScenarioName[CNC_MAX_FNAME_EXT];
    CNCStaticCellStruct StaticCells[MAX_EXPORT_CELLS];
} CNCMapDataStruct;

typedef struct
{
    int X;
    int Y;
    int X1;
    int Y1;
    int Frame;
    unsigned char Color;
} CNCObjectLineStruct;

typedef struct
{
    void* CNCInternalObjectPointer;
    char TypeName[CNC_OBJECT_ASSET_NAME_LENGTH];
    char AssetName[CNC_OBJECT_ASSET_NAME_LENGTH];
    int Type; /* DllObjectTypeEnum */
    int ID;
    int BaseObjectID;
    int BaseObjectType;
    int PositionX;
    int PositionY;
    int Width;
    int Height;
    int Altitude;
    int SortOrder;
    int Scale;
    int DrawFlags;
    short MaxStrength;
    short Strength;
    unsigned short ShapeIndex;
    unsigned short CellX;
    unsigned short CellY;
    unsigned short CenterCoordX;
    unsigned short CenterCoordY;
    short SimLeptonX;
    short SimLeptonY;
    unsigned char DimensionX;
    unsigned char DimensionY;
    unsigned char Rotation;
    unsigned char MaxSpeed;
    char Owner;
    char RemapColor;
    char SubObject;
    cnc_bool IsSelectable;
    unsigned int IsSelectedMask;
    cnc_bool IsRepairing;
    cnc_bool IsDumping;
    cnc_bool IsTheaterSpecific;
    unsigned int FlashingFlags;
    unsigned char Cloak;
    cnc_bool CanRepair;
    cnc_bool CanDemolish;
    cnc_bool CanDemolishUnit;
    short OccupyList[MAX_OCCUPY_CELLS];
    int OccupyListLength;
    int Pips[MAX_OBJECT_PIPS];
    int NumPips;
    int MaxPips;
    CNCObjectLineStruct Lines[MAX_OBJECT_LINES];
    int NumLines;
    cnc_bool RecentlyCreated;
    cnc_bool IsALoaner;
    cnc_bool IsFactory;
    cnc_bool IsPrimaryFactory;
    cnc_bool IsDeployable;
    cnc_bool IsAntiGround;
    cnc_bool IsAntiAircraft;
    cnc_bool IsSubSurface;
    cnc_bool IsNominal;
    cnc_bool IsDog;
    cnc_bool IsIronCurtain;
    cnc_bool IsInFormation;
    cnc_bool CanMove[MAX_HOUSES];
    cnc_bool CanFire[MAX_HOUSES];
    cnc_bool CanDeploy;
    cnc_bool CanHarvest;
    cnc_bool CanPlaceBombs;
    cnc_bool IsFixedWingedAircraft;
    cnc_bool IsFake;
    unsigned char ControlGroup;
    unsigned int VisibleFlags;
    unsigned int SpiedByFlags;
    char ProductionAssetName[CNC_OBJECT_ASSET_NAME_LENGTH];
    const char* OverrideDisplayName;
    unsigned char ActionWithSelected[MAX_HOUSES]; /* DllActionTypeEnum : unsigned char */
} CNCObjectStruct;

typedef struct
{
    int Count;
    CNCObjectStruct Objects[1]; /* variable length */
} CNCObjectListStruct;

typedef struct
{
    char AssetName[16];
    int PositionX;
    int PositionY;
    int Width;
    int Height;
    short Type;
    char Owner;
    int DrawFlags;
    unsigned char CellX;
    unsigned char CellY;
    unsigned char ShapeIndex;
    cnc_bool IsSmudge;
    cnc_bool IsOverlay;
    cnc_bool IsResource;
    cnc_bool IsSellable;
    cnc_bool IsTheaterShape;
    cnc_bool IsFlag;
} CNCDynamicMapEntryStruct;

typedef struct
{
    cnc_bool VortexActive;
    int VortexX;
    int VortexY;
    int VortexWidth;
    int VortexHeight;
    int Count;
    CNCDynamicMapEntryStruct Entries[1]; /* variable length */
} CNCDynamicMapStruct;

typedef struct
{
    char AssetName[16];
    int BuildableType;
    int BuildableID;
    int Type;            /* DllObjectTypeEnum */
    int SuperWeaponType; /* DllSuperweaponTypeEnum */
    int Cost;
    int PowerProvided;
    int BuildTime;
    float Progress;
    short PlacementList[MAX_OCCUPY_CELLS];
    int PlacementListLength;
    cnc_bool Completed;
    cnc_bool Constructing;
    cnc_bool ConstructionOnHold;
    cnc_bool Busy;
    cnc_bool BuildableViaCapture;
    cnc_bool Fake;
} CNCSidebarEntryStruct;

typedef struct
{
    int EntryCount[2];
    int Credits;
    int CreditsCounter;
    int Tiberium;
    int MaxTiberium;
    int PowerProduced;
    int PowerDrained;
    int MissionTimer;
    unsigned int UnitsKilled;
    unsigned int BuildingsKilled;
    unsigned int UnitsLost;
    unsigned int BuildingsLost;
    unsigned int TotalHarvestedCredits;
    cnc_bool RepairBtnEnabled;
    cnc_bool SellBtnEnabled;
    cnc_bool RadarMapActive;
    CNCSidebarEntryStruct Entries[1]; /* variable length */
} CNCSidebarStruct;

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
    cnc_u64 GlyphxPlayerID;
    int Team;
    int StartLocationIndex;
    unsigned char HomeCellX;
    unsigned char HomeCellY;
    cnc_bool IsAI;
    unsigned int AllyFlags;
    cnc_bool IsDefeated;
    unsigned int SpiedPowerFlags;
    unsigned int SpiedMoneyFlags;
    CNCSpiedInfoStruct SpiedInfo[MAX_HOUSES];
    int SelectedID;
    int SelectedType;
    unsigned char ActionWithSelected[MAX_EXPORT_CELLS];
    unsigned int ActionWithSelectedCount;
    unsigned int ScreenShake;
    cnc_bool IsRadarJammed;
} CNCPlayerInfoStruct;

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
    cnc_bool IsBuildSlowdown;
    cnc_bool IsWallDestroyer;
    cnc_bool IsContentScan;
} CNCDifficultyDataStruct;

typedef struct
{
    CNCDifficultyDataStruct Difficulties[3];
} CNCRulesDataStruct;

typedef struct
{
    cnc_i64 GlyphXPlayerID;
    cnc_bool IsHuman;
    cnc_bool WasHuman;
    cnc_bool IsWinner;
    int ResourcesGathered;
    int TotalUnitsKilled;
    int TotalStructuresKilled;
    int Efficiency;
    int Score;
} GameOverMultiPlayerStatsStruct;

typedef struct
{
    int EventType; /* EventCallbackType */
    cnc_i64 GlyphXPlayerID;
    union
    {
        struct
        {
            int SFXIndex;
            int Variation;
            int PixelX;
            int PixelY;
            int PlayerID;
            char SoundEffectName[16];
            int SoundEffectPriority;
            int SoundEffectContext;
        } SoundEffect;

        struct
        {
            int SpeechIndex;
            int PlayerID;
            char SpeechName[16];
        } Speech;

        struct
        {
            cnc_bool Multiplayer;
            cnc_bool IsHuman;
            cnc_bool PlayerWins;
            const char* MovieName;
            const char* MovieName2;
            const char* MovieName3;
            const char* MovieName4;
            const char* AfterScoreMovieName;
            int Score;
            int Leadership;
            int Efficiency;
            int CategoryTotal;
            int NODKilled;
            int GDIKilled;
            int CiviliansKilled;
            int NODBuildingsDestroyed;
            int GDIBuildingsDestroyed;
            int CiviliansBuildingsDestroyed;
            int RemainingCredits;
            int SabotagedStructureType;
            int TimerRemaining;
            int MultiPlayerTotalPlayers;
            GameOverMultiPlayerStatsStruct MultiPlayerPlayersData[8];
        } GameOver;

        struct
        {
            const char* PrintString;
        } DebugPrint;

        struct
        {
            const char* MovieName;
            int Theme;
            cnc_bool Immediate;
        } Movie;

        struct
        {
            const char* Message;
            float TimeoutSeconds;
            int MessageType;
            cnc_i64 MessageParam1;
        } Message;

        struct
        {
            int CellX;
            int CellY;
            char TemplateTypeName[32];
        } UpdateMapCell;

        struct
        {
            const char* AchievementType;
            const char* AchievementReason;
        } Achievement;

        struct
        {
            void* CarryoverList;
        } StoreCarryoverObjects;

        struct
        {
            int Type;
            int ID;
            char Name[16];
            int WeaponType;
        } SpecialWeaponTargetting;

        struct
        {
            int CoordX;
            int CoordY;
        } CenterCamera;

        struct
        {
            int CoordX;
            int CoordY;
        } Ping;
    };
} EventCallbackStruct;

#pragma pack(pop)

/* ---- The ABI tripwires. Numbers measured from the real C++ header. ----

   The win32 column was MEASURED on Windows, by compiling the DLL's
   own dllinterface.h with the DLL's own defines (REMASTER_BUILD _USRDLL MEGAMAPS) and
   printing sizeof and offsetof from a program that then RUNS, which is available here and
   was not available on the Mac. That is why the guessed CNCMapDataStruct value this file
   carried, 590400, is corrected to 590372.

   These structs are #pragma pack(1), so not one of these differences is alignment. Two
   causes, and both are mechanical:

     * ScenarioName is char[_MAX_FNAME + _MAX_EXT], and that is 512 on Windows against 263
       elsewhere. It accounts for CNCMapDataStruct exactly: StaticCells moves 299 -> 548,
       which is the same 249 bytes, and the 16384 * 36 cell array behind it does not move.
     * every OTHER difference here is a field that is 4 bytes on win32 and 8 on LP64.
       CNCObjectStruct loses 8 over two of them, and EventCallbackStruct loses 20 over
       five. Nothing is packed or aligned differently.
*/
_Static_assert(sizeof(CNCStaticCellStruct) == 36, "CNCStaticCellStruct size drift");
_Static_assert(sizeof(CNCObjectLineStruct) == 21, "CNCObjectLineStruct size drift");
_Static_assert(sizeof(CNCSidebarEntryStruct) == 130, "CNCSidebarEntryStruct size drift");
_Static_assert(sizeof(CNCSidebarStruct) == 189, "CNCSidebarStruct size drift");
_Static_assert(sizeof(CNCDynamicMapEntryStruct) == 48, "CNCDynamicMapEntryStruct size drift");
_Static_assert(sizeof(CNCDynamicMapStruct) == 69, "CNCDynamicMapStruct size drift");
_Static_assert(sizeof(CNCRulesDataStruct) == 117, "CNCRulesDataStruct size drift");
_Static_assert(sizeof(CNCDifficultyDataStruct) == 39, "CNCDifficultyDataStruct size drift");
_Static_assert(sizeof(CNCPlayerInfoStruct) == 16886, "CNCPlayerInfoStruct size drift");

/* Identical on both, and checked so that stays true. */
_Static_assert(offsetof(CNCObjectListStruct, Objects) == 4, "CNCObjectListStruct layout drift");
_Static_assert(offsetof(EventCallbackStruct, SoundEffect) == 12, "EventCallbackStruct layout drift");
_Static_assert(offsetof(CNCSidebarStruct, Entries) == 59, "CNCSidebarStruct layout drift");
_Static_assert(offsetof(CNCDynamicMapStruct, Entries) == 21, "CNCDynamicMapStruct layout drift");

#if defined(_WIN32)
_Static_assert(sizeof(CNCMapDataStruct) == 590372, "CNCMapDataStruct size drift (win32)");
_Static_assert(sizeof(CNCObjectStruct) == 490, "CNCObjectStruct size drift (win32)");
_Static_assert(sizeof(CNCObjectListStruct) == 494, "CNCObjectListStruct size drift (win32)");
_Static_assert(sizeof(EventCallbackStruct) == 339, "EventCallbackStruct size drift (win32)");
_Static_assert(offsetof(CNCMapDataStruct, StaticCells) == 548, "CNCMapDataStruct layout drift (win32)");
_Static_assert(offsetof(CNCObjectStruct, CellX) == 90, "CNCObjectStruct layout drift (win32)");
_Static_assert(offsetof(CNCObjectStruct, Owner) == 106, "CNCObjectStruct layout drift (win32)");
_Static_assert(offsetof(CNCObjectStruct, ActionWithSelected) == 458, "CNCObjectStruct layout drift (win32)");
#else
_Static_assert(sizeof(CNCMapDataStruct) == 590123, "CNCMapDataStruct size drift");
_Static_assert(sizeof(CNCObjectStruct) == 498, "CNCObjectStruct size drift");
_Static_assert(sizeof(CNCObjectListStruct) == 502, "CNCObjectListStruct size drift");
_Static_assert(sizeof(EventCallbackStruct) == 359, "EventCallbackStruct size drift");
_Static_assert(offsetof(CNCMapDataStruct, StaticCells) == 299, "CNCMapDataStruct layout drift");
_Static_assert(offsetof(CNCObjectStruct, CellX) == 94, "CNCObjectStruct layout drift");
_Static_assert(offsetof(CNCObjectStruct, Owner) == 110, "CNCObjectStruct layout drift");
_Static_assert(offsetof(CNCObjectStruct, ActionWithSelected) == 466, "CNCObjectStruct layout drift");
#endif

/* ============================== function pointers ================================ */

typedef void(CNC_CALL* CNC_Event_Callback_Type)(const EventCallbackStruct* event);

typedef unsigned int(CNC_CALL* fn_CNC_Version)(unsigned int);
typedef void(CNC_CALL* fn_CNC_Init)(const char*, CNC_Event_Callback_Type);
typedef void(CNC_CALL* fn_CNC_Config)(const CNCRulesDataStruct*);
typedef cnc_bool(CNC_CALL* fn_CNC_Start_Custom_Instance)(const char*, const char*, const char*, int, cnc_bool);
typedef cnc_bool(CNC_CALL* fn_CNC_Advance_Instance)(cnc_u64);
typedef cnc_bool(CNC_CALL* fn_CNC_Get_Game_State)(int, cnc_u64, unsigned char*, unsigned int);
typedef cnc_bool(CNC_CALL* fn_CNC_Get_Visible_Page)(unsigned char*, unsigned int*, unsigned int*);
typedef void(CNC_CALL* fn_CNC_Handle_Input)(int, unsigned char, cnc_u64, int, int, int, int);
typedef void(CNC_CALL* fn_CNC_Handle_Game_Request)(int);
typedef void(CNC_CALL* fn_CNC_Set_Home_Cell)(int, int, cnc_u64);

static struct
{
    fn_CNC_Version Version;
    fn_CNC_Init Init;
    fn_CNC_Config Config;
    fn_CNC_Start_Custom_Instance Start_Custom_Instance;
    fn_CNC_Advance_Instance Advance_Instance;
    fn_CNC_Get_Game_State Get_Game_State;
    fn_CNC_Get_Visible_Page Get_Visible_Page;
    fn_CNC_Handle_Input Handle_Input;
    fn_CNC_Handle_Game_Request Handle_Game_Request;
    fn_CNC_Set_Home_Cell Set_Home_Cell;
} Brain;

/* CNC3D_Dump_Objects: the brain's own per-tick state dump, resolved separately from the
   CNC_* set above because it is OURS rather than EA's, and because a brain built without
   it must still run this host. It is the oracle Phase 0 compares across architectures:
   it walks the real object heaps instead of the draw intercept, so it needs no art, no
   window and no renderer, and tools/xl-parity/compare.py already parses exactly what it
   prints. Absent in an older brain, in which case the dump is simply skipped. */
static int (*Dump_Objects)(void);

/* ================================ name tables ==================================== */

static const char* house_name(int owner)
{
    static const char* names[] = {"GDI",    "Nod",    "Neutral", "JP/DCT", "Multi1",
                                  "Multi2", "Multi3", "Multi4",  "Multi5", "Multi6"};
    if (owner < 0) {
        return "none";
    }
    if (owner < (int)(sizeof(names) / sizeof(names[0]))) {
        return names[owner];
    }
    return "?";
}

static const char* object_type_name(int t)
{
    static const char* names[] = {"UNKNOWN",     "INFANTRY",  "UNIT",           "AIRCRAFT",       "BUILDING",
                                  "TERRAIN",     "ANIM",      "BULLET",         "OVERLAY",        "SMUDGE",
                                  "OBJECT",      "SPECIAL",   "INFANTRY_TYPE",  "UNIT_TYPE",      "AIRCRAFT_TYPE",
                                  "BUILDING_TYPE", "VESSEL",  "VESSEL_TYPE"};
    if (t >= 0 && t < (int)(sizeof(names) / sizeof(names[0]))) {
        return names[t];
    }
    return "?";
}

static const char* theater_name(int t)
{
    switch (t) {
    case -1:
        return "NONE";
    case 0:
        return "DESERT";
    case 1:
        return "JUNGLE";
    case 2:
        return "TEMPERATE";
    case 3:
        return "WINTER";
    default:
        return "?";
    }
}

/* A char* that came from the DLL might be NULL; never print a raw NULL. */
static const char* safe_str(const char* s)
{
    return s ? s : "(null)";
}

/* TypeName/AssetName are fixed-size and not guaranteed NUL-terminated. */
static void copy_fixed(char* dst, const char* src, size_t n)
{
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ================================ event callback ================================= */

static unsigned long EventCounts[16];
static unsigned long TotalEvents;

static void CNC_CALL Event_Callback(const EventCallbackStruct* ev)
{
    TotalEvents++;
    if (ev == NULL) {
        printf("[event] <NULL event pointer>\n");
        return;
    }
    if (ev->EventType >= 0 && ev->EventType < 16) {
        EventCounts[ev->EventType]++;
    }

    switch (ev->EventType) {
    case CALLBACK_EVENT_SOUND_EFFECT: {
        char name[17];
        copy_fixed(name, ev->SoundEffect.SoundEffectName, 16);
        printf("[event] SOUND_EFFECT  sfx=%d var=%d name='%s' at px(%d,%d) prio=%d ctx=%d\n",
               ev->SoundEffect.SFXIndex,
               ev->SoundEffect.Variation,
               name,
               ev->SoundEffect.PixelX,
               ev->SoundEffect.PixelY,
               ev->SoundEffect.SoundEffectPriority,
               ev->SoundEffect.SoundEffectContext);
        break;
    }
    case CALLBACK_EVENT_SPEECH: {
        char name[17];
        copy_fixed(name, ev->Speech.SpeechName, 16);
        printf("[event] SPEECH        index=%d name='%s'\n", ev->Speech.SpeechIndex, name);
        break;
    }
    case CALLBACK_EVENT_GAME_OVER:
        printf("[event] GAME_OVER     multiplayer=%d human=%d player_wins=%d score=%d "
               "nod_killed=%d gdi_killed=%d civ_killed=%d credits=%d timer=%d movie='%s'\n",
               (int)ev->GameOver.Multiplayer,
               (int)ev->GameOver.IsHuman,
               (int)ev->GameOver.PlayerWins,
               ev->GameOver.Score,
               ev->GameOver.NODKilled,
               ev->GameOver.GDIKilled,
               ev->GameOver.CiviliansKilled,
               ev->GameOver.RemainingCredits,
               ev->GameOver.TimerRemaining,
               safe_str(ev->GameOver.MovieName));
        break;
    case CALLBACK_EVENT_DEBUG_PRINT:
        printf("[event] DEBUG_PRINT   %s\n", safe_str(ev->DebugPrint.PrintString));
        break;
    case CALLBACK_EVENT_MOVIE:
        printf("[event] MOVIE         name='%s' theme=%d immediate=%d\n",
               safe_str(ev->Movie.MovieName),
               ev->Movie.Theme,
               (int)ev->Movie.Immediate);
        break;
    case CALLBACK_EVENT_MESSAGE:
        printf("[event] MESSAGE       type=%d timeout=%.1f text='%s'\n",
               ev->Message.MessageType,
               (double)ev->Message.TimeoutSeconds,
               safe_str(ev->Message.Message));
        break;
    case CALLBACK_EVENT_UPDATE_MAP_CELL: {
        char name[33];
        copy_fixed(name, ev->UpdateMapCell.TemplateTypeName, 32);
        printf("[event] UPDATE_CELL   cell(%d,%d) template='%s'\n",
               ev->UpdateMapCell.CellX,
               ev->UpdateMapCell.CellY,
               name);
        break;
    }
    case CALLBACK_EVENT_ACHIEVEMENT:
        printf("[event] ACHIEVEMENT   type='%s' reason='%s'\n",
               safe_str(ev->Achievement.AchievementType),
               safe_str(ev->Achievement.AchievementReason));
        break;
    case CALLBACK_EVENT_STORE_CARRYOVER_OBJECTS:
        printf("[event] CARRYOVER     list=%p\n", ev->StoreCarryoverObjects.CarryoverList);
        break;
    case CALLBACK_EVENT_SPECIAL_WEAPON_TARGETTING: {
        char name[17];
        copy_fixed(name, ev->SpecialWeaponTargetting.Name, 16);
        printf("[event] SW_TARGETTING type=%d id=%d name='%s' weapon=%d\n",
               ev->SpecialWeaponTargetting.Type,
               ev->SpecialWeaponTargetting.ID,
               name,
               ev->SpecialWeaponTargetting.WeaponType);
        break;
    }
    case CALLBACK_EVENT_BRIEFING_SCREEN:
        printf("[event] BRIEFING_SCREEN\n");
        break;
    case CALLBACK_EVENT_CENTER_CAMERA:
        printf("[event] CENTER_CAMERA coord(%d,%d)\n", ev->CenterCamera.CoordX, ev->CenterCamera.CoordY);
        break;
    case CALLBACK_EVENT_PING:
        printf("[event] PING          coord(%d,%d)\n", ev->Ping.CoordX, ev->Ping.CoordY);
        break;
    default:
        printf("[event] <unknown type %d>\n", ev->EventType);
        break;
    }
    fflush(stdout);
}

/* ================================ symbol loading ================================= */

static int resolve_count = 0;
static int resolve_failures = 0;

static void* resolve(cnc_lib_t lib, const char* name)
{
    void* sym = cnc_lib_sym(lib, name);
    resolve_count++;
    if (sym == NULL) {
        resolve_failures++;
        printf("  %-28s  *** NOT FOUND ***\n", name);
    } else {
        printf("  %-28s  %p\n", name, sym);
    }
    return sym;
}

/* ================================== state dumps ================================== */

static void dump_static_map(cnc_u64 player_id)
{
    CNCMapDataStruct* map = (CNCMapDataStruct*)malloc(sizeof(CNCMapDataStruct));
    if (map == NULL) {
        printf("  (out of memory allocating %zu bytes for CNCMapDataStruct)\n", sizeof(CNCMapDataStruct));
        return;
    }
    memset(map, 0, sizeof(*map));
    map->MapCellWidth = -1; /* sentinel: proves the DLL actually wrote the buffer */

    cnc_bool ok = Brain.Get_Game_State(GAME_STATE_STATIC_MAP, player_id, (unsigned char*)map, (unsigned int)sizeof(*map));
    printf("  CNC_Get_Game_State(GAME_STATE_STATIC_MAP) -> %s\n", ok ? "true" : "FALSE");
    if (!ok) {
        if (map->MapCellWidth == -1) {
            printf("    buffer untouched -- request rejected (buffer too small?)\n");
        }
        free(map);
        return;
    }

    char scen[CNC_MAX_FNAME_EXT + 1];
    copy_fixed(scen, map->ScenarioName, CNC_MAX_FNAME_EXT);
    printf("    scenario name : '%s'\n", scen);
    printf("    theater       : %d (%s)\n", map->Theater, theater_name(map->Theater));
    printf("    map cell rect : x=%d y=%d w=%d h=%d  (exported, includes 1-cell border)\n",
           map->MapCellX,
           map->MapCellY,
           map->MapCellWidth,
           map->MapCellHeight);
    printf("    original rect : x=%d y=%d w=%d h=%d  (playable area from the INI)\n",
           map->OriginalMapCellX,
           map->OriginalMapCellY,
           map->OriginalMapCellWidth,
           map->OriginalMapCellHeight);

    /*
    ** NOTE: StaticCells is a COMPACTED list, not a grid. The engine only appends a cell
    ** when Get_Template_Info() succeeds, so index != x + y*w. Report it as a list.
    */
    long populated = 0;
    long cells_scanned = (long)map->MapCellWidth * (long)map->MapCellHeight;
    if (cells_scanned > MAX_EXPORT_CELLS) {
        cells_scanned = MAX_EXPORT_CELLS;
    }
    for (long i = 0; i < cells_scanned; i++) {
        if (map->StaticCells[i].TemplateTypeName[0] != '\0') {
            populated++;
        } else {
            break;
        }
    }
    printf("    templated cells: %ld of %ld scanned (list is compacted, not a grid)\n", populated, cells_scanned);
    long show = populated < 16 ? populated : 16;
    for (long i = 0; i < show; i++) {
        char n[33];
        copy_fixed(n, map->StaticCells[i].TemplateTypeName, 32);
        printf("      [%3ld] icon=%-4d '%s'\n", i, map->StaticCells[i].IconNumber, n);
    }
    if (populated > show) {
        printf("      ... %ld more\n", populated - show);
    }
    free(map);
}

static void dump_layers(cnc_u64 player_id, int max_objects)
{
    size_t bytes = sizeof(CNCObjectListStruct) + (size_t)max_objects * sizeof(CNCObjectStruct);
    CNCObjectListStruct* list = (CNCObjectListStruct*)malloc(bytes);
    if (list == NULL) {
        printf("  (out of memory allocating %zu bytes for the object list)\n", bytes);
        return;
    }
    memset(list, 0, bytes);
    list->Count = -1; /* sentinel */

    cnc_bool ok = Brain.Get_Game_State(GAME_STATE_LAYERS, player_id, (unsigned char*)list, (unsigned int)bytes);
    printf("  CNC_Get_Game_State(GAME_STATE_LAYERS) -> %s  (buffer %zu bytes / room for %d objects)\n",
           ok ? "true" : "FALSE",
           bytes,
           max_objects);
    if (!ok) {
        /*
        ** Get_Layer_State returns false BOTH when the buffer is too small (it bails out
        ** before writing Count) AND when there are simply zero objects (it writes Count=0
        ** then returns false). The sentinel tells the two apart -- do not conflate them.
        */
        if (list->Count == -1) {
            printf("    buffer NEVER written -- the object list overflowed the buffer. Raise max_objects.\n");
        } else {
            printf("    Count=%d -- the engine reported ZERO objects in all %d layers.\n", list->Count, 3);
        }
        free(list);
        return;
    }

    printf("    object count  : %d\n", list->Count);
    printf("    %-4s %-10s %-10s %-14s %-8s %-9s %-13s %-11s %s\n",
           "#",
           "TypeName",
           "AssetName",
           "Type",
           "Owner",
           "Cell",
           "Health",
           "LeptonPos",
           "flags");
    for (int i = 0; i < list->Count && i < max_objects; i++) {
        const CNCObjectStruct* o = &list->Objects[i];
        char tname[CNC_OBJECT_ASSET_NAME_LENGTH + 1];
        char aname[CNC_OBJECT_ASSET_NAME_LENGTH + 1];
        char cell[16];
        char health[24];
        char pos[24];
        char flags[64];

        copy_fixed(tname, o->TypeName, CNC_OBJECT_ASSET_NAME_LENGTH);
        copy_fixed(aname, o->AssetName, CNC_OBJECT_ASSET_NAME_LENGTH);
        snprintf(cell, sizeof(cell), "%u,%u", (unsigned)o->CellX, (unsigned)o->CellY);
        if (o->MaxStrength > 0) {
            snprintf(health,
                     sizeof(health),
                     "%d/%d %3d%%",
                     (int)o->Strength,
                     (int)o->MaxStrength,
                     (int)((100 * (int)o->Strength) / (int)o->MaxStrength));
        } else {
            snprintf(health, sizeof(health), "%d/%d", (int)o->Strength, (int)o->MaxStrength);
        }
        snprintf(pos, sizeof(pos), "%u,%u", (unsigned)o->CenterCoordX, (unsigned)o->CenterCoordY);
        flags[0] = '\0';
        if (o->SubObject) {
            strncat(flags, "sub ", sizeof(flags) - strlen(flags) - 1);
        }
        if (o->IsSelectable) {
            strncat(flags, "sel ", sizeof(flags) - strlen(flags) - 1);
        }
        if (o->IsFactory) {
            strncat(flags, "factory ", sizeof(flags) - strlen(flags) - 1);
        }
        if (o->IsPrimaryFactory) {
            strncat(flags, "primary ", sizeof(flags) - strlen(flags) - 1);
        }
        if (o->CanHarvest) {
            strncat(flags, "harvester ", sizeof(flags) - strlen(flags) - 1);
        }
        if (o->IsRepairing) {
            strncat(flags, "repairing ", sizeof(flags) - strlen(flags) - 1);
        }

        printf("    %-4d %-10s %-10s %-14s %-8s %-9s %-13s %-11s %s\n",
               i,
               tname,
               aname,
               object_type_name(o->Type),
               house_name((int)o->Owner),
               cell,
               health,
               pos,
               flags);
    }
    free(list);
}

static void dump_dynamic_map(cnc_u64 player_id, int max_entries)
{
    size_t bytes = sizeof(CNCDynamicMapStruct) + (size_t)max_entries * sizeof(CNCDynamicMapEntryStruct);
    CNCDynamicMapStruct* dyn = (CNCDynamicMapStruct*)malloc(bytes);
    if (dyn == NULL) {
        printf("  (out of memory allocating %zu bytes for the dynamic map)\n", bytes);
        return;
    }
    memset(dyn, 0, bytes);
    dyn->Count = -1;

    cnc_bool ok = Brain.Get_Game_State(GAME_STATE_DYNAMIC_MAP, player_id, (unsigned char*)dyn, (unsigned int)bytes);
    printf("  CNC_Get_Game_State(GAME_STATE_DYNAMIC_MAP) -> %s\n", ok ? "true" : "FALSE");
    if (!ok) {
        if (dyn->Count == -1) {
            printf("    buffer NEVER written -- overflow. Raise max_entries.\n");
        } else {
            printf("    Count=%d -- no smudges/overlays reported.\n", dyn->Count);
        }
        free(dyn);
        return;
    }

    printf("    entry count   : %d  (smudges, overlays, tiberium, walls)\n", dyn->Count);
    int tiberium = 0;
    int overlays = 0;
    int smudges = 0;
    for (int i = 0; i < dyn->Count && i < max_entries; i++) {
        if (dyn->Entries[i].IsResource) {
            tiberium++;
        }
        if (dyn->Entries[i].IsOverlay) {
            overlays++;
        }
        if (dyn->Entries[i].IsSmudge) {
            smudges++;
        }
    }
    printf("    of which      : %d tiberium, %d overlay, %d smudge\n", tiberium, overlays, smudges);
    int show = dyn->Count < 12 ? dyn->Count : 12;
    for (int i = 0; i < show; i++) {
        const CNCDynamicMapEntryStruct* e = &dyn->Entries[i];
        char n[17];
        copy_fixed(n, e->AssetName, 16);
        printf("      [%3d] '%-10s' cell(%u,%u) type=%d owner=%s%s%s%s\n",
               i,
               n,
               (unsigned)e->CellX,
               (unsigned)e->CellY,
               (int)e->Type,
               house_name((int)e->Owner),
               e->IsResource ? " resource" : "",
               e->IsOverlay ? " overlay" : "",
               e->IsSmudge ? " smudge" : "");
    }
    if (dyn->Count > show) {
        printf("      ... %d more\n", dyn->Count - show);
    }
    free(dyn);
}

static void dump_sidebar(cnc_u64 player_id, int max_entries)
{
    size_t bytes = sizeof(CNCSidebarStruct) + (size_t)max_entries * sizeof(CNCSidebarEntryStruct);
    CNCSidebarStruct* sb = (CNCSidebarStruct*)malloc(bytes);
    if (sb == NULL) {
        printf("  (out of memory allocating the sidebar)\n");
        return;
    }
    memset(sb, 0, bytes);
    sb->EntryCount[0] = -1;

    cnc_bool ok = Brain.Get_Game_State(GAME_STATE_SIDEBAR, player_id, (unsigned char*)sb, (unsigned int)bytes);
    printf("  CNC_Get_Game_State(GAME_STATE_SIDEBAR) -> %s\n", ok ? "true" : "FALSE");
    if (!ok) {
        printf("    (sidebar unavailable; EntryCount[0]=%d)\n", sb->EntryCount[0]);
        free(sb);
        return;
    }
    printf("    credits=%d tiberium=%d/%d power=%d/%d timer=%d\n",
           sb->Credits,
           sb->Tiberium,
           sb->MaxTiberium,
           sb->PowerDrained,
           sb->PowerProduced,
           sb->MissionTimer);
    printf("    kills: %u units / %u buildings   losses: %u units / %u buildings   harvested=%u\n",
           sb->UnitsKilled,
           sb->BuildingsKilled,
           sb->UnitsLost,
           sb->BuildingsLost,
           sb->TotalHarvestedCredits);
    int total = sb->EntryCount[0] + sb->EntryCount[1];
    printf("    buildable entries: %d left + %d right = %d\n", sb->EntryCount[0], sb->EntryCount[1], total);
    for (int i = 0; i < total && i < max_entries; i++) {
        char n[17];
        copy_fixed(n, sb->Entries[i].AssetName, 16);
        printf("      [%2d] '%-9s' %-14s cost=%-5d power=%-5d progress=%.2f%s\n",
               i,
               n,
               object_type_name(sb->Entries[i].Type),
               sb->Entries[i].Cost,
               sb->Entries[i].PowerProvided,
               (double)sb->Entries[i].Progress,
               sb->Entries[i].Constructing ? " CONSTRUCTING" : "");
    }
    free(sb);
}

static void dump_player_info(cnc_u64 player_id)
{
    CNCPlayerInfoStruct* pi = (CNCPlayerInfoStruct*)malloc(sizeof(CNCPlayerInfoStruct));
    if (pi == NULL) {
        printf("  (out of memory allocating player info)\n");
        return;
    }
    memset(pi, 0, sizeof(*pi));
    pi->House = 0xFF;

    cnc_bool ok = Brain.Get_Game_State(GAME_STATE_PLAYER_INFO, player_id, (unsigned char*)pi, (unsigned int)sizeof(*pi));
    printf("  CNC_Get_Game_State(GAME_STATE_PLAYER_INFO) -> %s\n", ok ? "true" : "FALSE");
    if (!ok) {
        printf("    (player info unavailable -- expected in single player, which has no GlyphX player registry)\n");
        free(pi);
        return;
    }
    char name[65];
    copy_fixed(name, pi->Name, 64);
    printf("    name='%s' house=%u (%s) color=%d team=%d start=%d home_cell=(%u,%u) ai=%d defeated=%d\n",
           name,
           (unsigned)pi->House,
           house_name((int)pi->House),
           pi->ColorIndex,
           pi->Team,
           pi->StartLocationIndex,
           (unsigned)pi->HomeCellX,
           (unsigned)pi->HomeCellY,
           (int)pi->IsAI,
           (int)pi->IsDefeated);
    free(pi);
}

static void dump_all(const char* label, cnc_u64 player_id, int max_objects)
{
    printf("\n=================== GAME STATE: %s ===================\n", label);
    dump_static_map(player_id);
    printf("\n");
    dump_layers(player_id, max_objects);
    printf("\n");
    dump_dynamic_map(player_id, 8192);
    printf("\n");
    dump_sidebar(player_id, 128);
    printf("\n");
    dump_player_info(player_id);
    printf("======================================================================\n");
}

/* ==================================== rules ====================================== */

/*
** Vanilla Tiberian Dawn difficulty biases (RULES.INI [Easy]/[Normal]/[Difficult]).
** Index order matches the engine's Rule.Diff[]: 0=Easy, 1=Normal, 2=Hard.
*/
static void fill_rules(CNCRulesDataStruct* rules)
{
    static const float easy[9] = {1.2f, 1.2f, 1.2f, 0.3f, 0.8f, 0.8f, 0.8f, 0.001f, 0.001f};
    static const float norm[9] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.02f, 0.03f};
    static const float hard[9] = {0.9f, 0.9f, 0.9f, 1.05f, 1.05f, 1.0f, 1.0f, 0.03f, 0.03f};
    const float* src[3] = {easy, norm, hard};
    const cnc_bool slowdown[3] = {0, 1, 1};
    const cnc_bool walldestroyer[3] = {1, 1, 1};
    const cnc_bool contentscan[3] = {1, 1, 0};

    memset(rules, 0, sizeof(*rules));
    for (int i = 0; i < 3; i++) {
        rules->Difficulties[i].FirepowerBias = src[i][0];
        rules->Difficulties[i].GroundspeedBias = src[i][1];
        rules->Difficulties[i].AirspeedBias = src[i][2];
        rules->Difficulties[i].ArmorBias = src[i][3];
        rules->Difficulties[i].ROFBias = src[i][4];
        rules->Difficulties[i].CostBias = src[i][5];
        rules->Difficulties[i].BuildSpeedBias = src[i][6];
        rules->Difficulties[i].RepairDelay = src[i][7];
        rules->Difficulties[i].BuildDelay = src[i][8];
        rules->Difficulties[i].IsBuildSlowdown = slowdown[i];
        rules->Difficulties[i].IsWallDestroyer = walldestroyer[i];
        rules->Difficulties[i].IsContentScan = contentscan[i];
    }
}

/* ===================================== main ====================================== */

static void usage(const char* argv0)
{
    printf("usage: %s <content_dir> <scenario_dir> <scenario_name> [ticks] [build_level]\n", argv0);
    printf("       %s                (no args: dlopen + symbol resolution self-test only)\n", argv0);
    printf("\n");
    printf("  content_dir    directory the engine treats as the game content root\n");
    printf("  scenario_dir   directory holding <scenario_name>.INI and <scenario_name>.BIN\n");
    printf("  scenario_name  e.g. SCB01EA  (no extension -- the engine appends .INI/.BIN)\n");
    printf("  ticks          number of CNC_Advance_Instance() calls, default 1\n");
    printf("  build_level    tech level passed to CNC_Start_Custom_Instance, default 7\n");
    printf("\n");
    printf("  env CNC3D_BRAIN   override the path to the shared library\n");
    printf("  env CNC3D_TICKDUMP  dump full engine state every tick (Phase 0 determinism runs)\n");
}

int main(int argc, char** argv)
{
    /* Unbuffered, because the single most useful run of this tool is the one that CRASHES,
       and a buffered stdout throws away every line that would have said where. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* CNC3D_TICKDUMP prints about 32 KB per tick, and on Windows an unbuffered stdout
       under msvcrt is one WriteFile syscall PER CHARACTER, the same trap that once cost
       the renderer its framerate. Measured on a 32 bit Windows build, 3 Sep 2026: 112 ms
       per dumped tick against 1.3 ms for the simulation itself,
       so a 20,000 tick capture took 37 minutes. A crash mid-capture is not the run this
       tool keeps stdout unbuffered for, so buffer only while dumping; every other run
       keeps its crash-proof stdout. */
    if (getenv("CNC3D_TICKDUMP") != NULL) {
        setvbuf(stdout, NULL, _IOFBF, 1 << 20);
    }

    const char* libpath = getenv("CNC3D_BRAIN");
    if (libpath == NULL || libpath[0] == '\0') {
        libpath = DEFAULT_BRAIN;
    }

    printf("CNC3D headless host\n");
    printf("host struct sizes (must match dllinterface.h): object=%zu list_hdr=%zu map=%zu event=%zu rules=%zu\n",
           sizeof(CNCObjectStruct),
           (size_t)offsetof(CNCObjectListStruct, Objects),
           sizeof(CNCMapDataStruct),
           sizeof(EventCallbackStruct),
           sizeof(CNCRulesDataStruct));

    printf("\n--- loading brain ---\n");
    printf("  path: %s\n", libpath);
    cnc_lib_t lib = cnc_lib_open(libpath);
    if (lib == NULL) {
        printf("  dlopen FAILED: %s\n", safe_str(cnc_lib_error()));
        return 2;
    }
    printf("  handle: %p\n", (void*)lib);

    printf("\n--- resolving symbols ---\n");
    Brain.Version = (fn_CNC_Version)resolve(lib, "CNC_Version");
    Brain.Init = (fn_CNC_Init)resolve(lib, "CNC_Init");
    Brain.Config = (fn_CNC_Config)resolve(lib, "CNC_Config");
    Brain.Start_Custom_Instance = (fn_CNC_Start_Custom_Instance)resolve(lib, "CNC_Start_Custom_Instance");
    Brain.Advance_Instance = (fn_CNC_Advance_Instance)resolve(lib, "CNC_Advance_Instance");
    /* Not through resolve(): a missing one is not a missing symbol in the sense that
       function reports, and it must not count against the resolved tally. */
    Dump_Objects = (int (*)(void))cnc_lib_sym(lib, "CNC3D_Dump_Objects");
    Brain.Get_Game_State = (fn_CNC_Get_Game_State)resolve(lib, "CNC_Get_Game_State");
    Brain.Get_Visible_Page = (fn_CNC_Get_Visible_Page)resolve(lib, "CNC_Get_Visible_Page");
    Brain.Handle_Input = (fn_CNC_Handle_Input)resolve(lib, "CNC_Handle_Input");
    Brain.Handle_Game_Request = (fn_CNC_Handle_Game_Request)resolve(lib, "CNC_Handle_Game_Request");
    Brain.Set_Home_Cell = (fn_CNC_Set_Home_Cell)resolve(lib, "CNC_Set_Home_Cell");
    printf("  resolved %d/%d symbols (%d missing)\n",
           resolve_count - resolve_failures,
           resolve_count,
           resolve_failures);

    /* Only the first six are load-bearing for driving a scenario. */
    int fatal = (Brain.Init == NULL) || (Brain.Config == NULL) || (Brain.Start_Custom_Instance == NULL)
                || (Brain.Advance_Instance == NULL) || (Brain.Get_Game_State == NULL);
    if (fatal) {
        printf("\nFATAL: a required CNC_* entry point is missing. Cannot drive the brain.\n");
        return 3;
    }

    if (argc < 4) {
        printf("\n--- self-test only (no scenario arguments given) ---\n");
        printf("  dlopen + symbol resolution: %s\n", resolve_failures == 0 ? "OK" : "PARTIAL");
        printf("  NOTE: CNC_Init was deliberately NOT called -- it runs the engine's full\n");
        printf("        startup (font/MIX loading), which needs real content. Pass a scenario\n");
        printf("        to exercise it.\n\n");
        usage(argv[0]);
        return resolve_failures == 0 ? 0 : 4;
    }

    const char* content_dir = argv[1];
    const char* scenario_dir_in = argv[2];
    const char* scenario_name = argv[3];
    int ticks = (argc > 4) ? atoi(argv[4]) : 1;
    int build_level = (argc > 5) ? atoi(argv[5]) : 7;
    if (ticks < 1) {
        ticks = 1;
    }

    /*
    ** The engine builds the path as "%s%s.INI" -- a plain concatenation, no separator
    ** inserted. Append one if the caller left it off, and say so.
    */
    char scenario_dir[1024];
    size_t sdl = strlen(scenario_dir_in);
    if (sdl == 0 || (scenario_dir_in[sdl - 1] != '/' && scenario_dir_in[sdl - 1] != '\\')) {
        snprintf(scenario_dir, sizeof(scenario_dir), "%s/", scenario_dir_in);
        printf("\nnote: appended a trailing '/' to scenario_dir (engine concatenates without one)\n");
    } else {
        snprintf(scenario_dir, sizeof(scenario_dir), "%s", scenario_dir_in);
    }

    printf("\n--- CNC_Init ---\n");
    /*
    ** The command line is tokenised by DLL_Startup and then strupr()'d by
    ** Parse_Command_Line, and an unrecognised switch makes startup bail out entirely.
    ** So: pass nothing. The content root is set by CNC_Start_Custom_Instance instead.
    */
    const char* cmdline = getenv("CNC3D_CMDLINE");
    if (cmdline == NULL) {
        cmdline = "";
    }
    printf("  command line: '%s'\n", cmdline);
    Brain.Init(cmdline, Event_Callback);
    printf("  CNC_Init returned (events so far: %lu)\n", TotalEvents);

    if (Brain.Version != NULL) {
        printf("  CNC_Version(0) = %u\n", Brain.Version(0));
    }

    printf("\n--- CNC_Config ---\n");
    CNCRulesDataStruct rules;
    fill_rules(&rules);
    Brain.Config(&rules);
    printf("  CNC_Config returned (Easy/Normal/Hard firepower biases %.2f/%.2f/%.2f)\n",
           (double)rules.Difficulties[0].FirepowerBias,
           (double)rules.Difficulties[1].FirepowerBias,
           (double)rules.Difficulties[2].FirepowerBias);

    printf("\n--- CNC_Start_Custom_Instance ---\n");
    printf("  content_directory : '%s'\n", content_dir);
    printf("  directory_path    : '%s'\n", scenario_dir);
    printf("  scenario_name     : '%s'\n", scenario_name);
    printf("  -> will read      : '%s%s.INI' and '%s%s.BIN'\n",
           scenario_dir,
           scenario_name,
           scenario_dir,
           scenario_name);
    printf("  build_level       : %d\n", build_level);
    printf("  multiplayer       : false\n");

    cnc_bool started = Brain.Start_Custom_Instance(content_dir, scenario_dir, scenario_name, build_level, 0);
    printf("  CNC_Start_Custom_Instance RETURNED: %s\n", started ? "TRUE" : "FALSE");
    if (!started) {
        printf("\nFAILED to start the scenario. Read_Scenario_Ini_File() rejected it.\n");
        printf("Most likely: the .INI is missing/unreadable at that exact path, or its\n");
        printf("[Basic] section is malformed. Nothing was simulated.\n");
        return 5;
    }

    cnc_u64 player_id = 0; /* single player: Set_Player_Context is a no-op for GAME_NORMAL */

    printf("\n--- CNC_Advance_Instance x%d ---\n", ticks);
    int completed = 0;
    for (int i = 0; i < ticks; i++) {
        cnc_bool cont = Brain.Advance_Instance(player_id);
        completed++;

        /* CNC3D_TICKDUMP: one dump per tick, for the cross architecture determinism run.
           OFF unless the variable is set, because it prints roughly 32 KB per tick and
           every other user of this host wants the two dumps it already produces. The
           TICK| line is what makes the output splittable per tick, and it is what
           compare.py keys the first divergent tick on. */
        if (Dump_Objects != NULL && getenv("CNC3D_TICKDUMP") != NULL) {
            printf("TICK|%d\n", completed);
            Dump_Objects();
        }
        if (i == 0) {
            printf("  tick 1 -> %s\n", cont ? "true" : "false");
            dump_all("AFTER TICK 1", player_id, 8192);
            printf("\n--- continuing ticks ---\n");
        }
        if (!cont) {
            printf("  tick %d returned FALSE -- game over or logic halt; stopping.\n", completed);
            break;
        }
    }
    printf("  advanced %d tick(s)\n", completed);

    if (completed > 1) {
        char label[64];
        snprintf(label, sizeof(label), "AFTER TICK %d", completed);
        dump_all(label, player_id, 8192);
    }

    printf("\n--- event summary ---\n");
    printf("  total events: %lu\n", TotalEvents);
    static const char* evnames[14] = {"SOUND_EFFECT",
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
    for (int i = 0; i < 14; i++) {
        if (EventCounts[i]) {
            printf("    %-18s %lu\n", evnames[i], EventCounts[i]);
        }
    }

    printf("\ndone.\n");
    return 0;
}
