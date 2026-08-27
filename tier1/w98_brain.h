/*
 * w98_brain.h -- tier1's bridge to the Tiberian Dawn brain, on Windows 98.
 *
 * Loads TiberianDawn.dll, starts a real mission, ticks it, and hands back the live
 * object list. This is the same brain and the same call sequence game/cnc_eyes.cpp uses;
 * it is written separately rather than shared because cnc_eyes.cpp is C++ tangled with
 * OpenGL and this has to be plain C with nothing under it but the operating system.
 *
 * WHERE THE OBJECTS COME FROM, because the obvious answer is the wrong one.
 * The vanilla `CNC_Get_Game_State(GAME_STATE_LAYERS)` call returns FALSE with a count of
 * zero for a single player custom instance. That is not a Windows 98 fault: the identical
 * host built natively on macOS does exactly the same thing, measured before it was
 * blamed. The renderer therefore uses `CNC3D_Dump_Objects`, an export our own
 * brain/patches add, which walks the engine's real Infantry/Units/Buildings/Terrains
 * heaps directly and does not go through the Draw_It intercept, so it works with no art
 * loaded at all. It reports by PRINTING to stdout, one "OBJ|" line per object, so this
 * file redirects stdout to a scratch file around the call and parses it back. That is
 * ugly and it is exactly what cnc_eyes.cpp does (see capture_brain_call there); the
 * alternative is changing a GPL interface we have agreed not to fork.
 */

#ifndef W98_BRAIN_H
#define W98_BRAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define WB_MAX_OBJECTS 512

typedef struct
{
    char kind[12];      /* INFANTRY, UNIT, BUILDING, TERRAIN, AIRCRAFT            */
    char name[12];      /* the type's IniName: MTNK, NUKE, E1, ...                */
    char house[12];     /* owner IniName: GoodGuy, BadGuy, Neutral, ...           */
    char mission[16];   /* what it is currently doing                             */

    int cell, cx, cy;   /* map cell, and that cell split into x and y             */
    int centercell;     /* the FOOTPRINT centre, which is where art is drawn      */
    int lx, ly;         /* exact position in leptons, 256 per cell. Not snapped   */
    int clx, cly;       /* the footprint centre, in leptons                       */
    int fw, fh;         /* footprint in cells, from BuildingTypeClass Width/Height */

    int face, tface;    /* body and turret facing, 0..255. tface is -1 if none    */
    int doing, dostage; /* the engine's DoType and its stage. Infantry animation is
                         * selected from these, not from anything we invent: DoType
                         * says prone/firing/crawling/dying and the stage is the frame.
                         * FOR A BUILDING `doing` is the BSTATE, not a DoType, and 0
                         * means BSTATE_CONSTRUCTION -- the building is assembling.    */
    int dying;          /* pre-digested by the brain: this Do is one of the death set */
    int makecnt;        /* BuildingTypeClass Anims[BSTATE_CONSTRUCTION].Count, so the
                         * buildup can be clamped and reversed against the ENGINE's own
                         * count rather than against whatever our pack happens to hold */
    int doorstage;      /* DoorClass::Door_Stage(), 0..9 on the War Factory. The N64's
                         * own draw arm is clipFrame = Door_Stage() * 59/9, so this is
                         * the one fact the door cannot be animated without           */
    int status;         /* MissionClass::Status. The SAM site's arm branches on it     */
    int nav, tar;       /* the engine's navigation and target cells, -1 for none       */
    int strength, maxstrength;
    int limbo, down, sel, id;
} W98_Object;

/* ---- events out of the engine -------------------------------------------------------
 * CNC_Init takes a callback and the engine reports through it: every sound effect, every
 * EVA line, and the end of the mission. The bridge decodes the packed struct once, here,
 * and hands the caller a flat record, so nothing above this file has to mirror a C++
 * union with #pragma pack(1) on it.
 * ----------------------------------------------------------------------------------- */
#define WB_EV_SFX       0
#define WB_EV_SPEECH    1
#define WB_EV_GAMEOVER  2

typedef struct
{
    int  type;
    int  index;          /* SFXIndex or SpeechIndex */
    int  variation;
    int  px, py;         /* the engine's own pixel position for the effect */
    char name[20];       /* SoundEffectName / SpeechName, as the engine spells it */
    int  win;            /* GAME_OVER only: did the player win */
} W98_Event;

/* Install before wb_start. NULL removes it. Called from inside CNC_Advance_Instance, so
 * it must not block and must not re-enter the brain. */
void wb_set_event_hook(void (*fn)(const W98_Event *));

/* How many events of each kind have come out, for the report. */
void wb_event_counts(long *sfx, long *speech, long *gameover);

/* Loads the DLL and resolves the entry points. dllpath may be NULL for
 * "TiberianDawn.dll beside the exe". Returns 0 on failure with a reason in err. */
int wb_open(const char *dllpath, char *err, int errlen);

/* CNC_Init + CNC_Config + CNC_Start_Custom_Instance. Directories need their trailing
 * separator, exactly as the brain expects them. */
int wb_start(const char *content_dir, const char *mission_dir, const char *scenario,
             int build_level, char *err, int errlen);

/* One CNC_Advance_Instance. The engine's native rate is 15 Hz; the renderer's frame rate
 * is independent of it and slower is fine, which matters on this hardware. */
void wb_tick(void);

/* Fills `out` with up to `max` live objects and returns how many. */
int wb_objects(W98_Object *out, int max);

/* ---- the PLAY interface: selection and orders --------------------------------------
 *
 * The engine takes clicks in pixels of ITS OWN tactical view, 24 px to a cell with the
 * top left corner at TacticalCoord. Our camera has nothing whatever to do with that
 * space, so a mouse pixel is NEVER handed straight through. tier1 picks a CELL with its
 * own inverse camera and the brain converts that cell into engine input pixels through
 * CNC3D_Cell_To_Pixel, which round-trips its own answer through Pixel_To_Coord so it
 * cannot lie about a rejected click. This mirrors game/cnc_eyes.cpp:6566-6578.
 *
 * Orders queue as engine events and land on the NEXT tick, never inside the call.
 * ---------------------------------------------------------------------------------- */

/* Object kinds, matching DllObjectTypeEnum in tiberiandawn/dllinterface.h:117. */
#define WB_INFANTRY 1
#define WB_UNIT     2
#define WB_AIRCRAFT 3
#define WB_BUILDING 4

void wb_clear_selection(void);
int  wb_select(int kind, int id);          /* kind is one of the WB_* above */

/* Right click: order the current selection to act at this cell. ctrl forces attack. */
int  wb_command_at_cell(int cell_x, int cell_y, int ctrl);

/* The engine's own ActionType for a cell, given the current selection: "what would happen
 * if you clicked here". This is what picks the 3D cursor's model. -1 means the engine
 * refused the pixel; 0 is ACTION_NONE, which is a real answer (an empty selection) and
 * not an error. The export prints a line and flushes on every call, so ask it on a change
 * rather than on a frame. */
int  wb_action_at_cell(int cell_x, int cell_y);

/* ---- the live sidebar ---------------------------------------------------------------
 *
 * Credits, power and the buildable list, straight out of the engine. Read BYTE-WISE from
 * the returned buffer rather than by casting a struct over it, and the offsets below are
 * not guessed: they were measured by compiling a probe against the real header. The whole
 * CNC_* ABI is #pragma pack(1), which cost a day the one time it was assumed instead.
 *
 *   CNCSidebarStruct       header 59 bytes, then the entries
 *   CNCSidebarEntryStruct  130 bytes each
 * -------------------------------------------------------------------------------------- */

#define WB_MAX_BUILD 32

/* CNCSidebarEntryStruct::PlacementList is short[MAX_OCCUPY_CELLS] and the record is 130
 * bytes packed, which pins MAX_OCCUPY_CELLS at 36: 48 + 2*36 + 4 + 6 = 130. */
#define WB_OCCUPY_MAX 36

typedef struct
{
    char  name[16];        /* the cameo stem: NUKE, PYLE, MTNK, E1 ...          */
    int   btype, bid;      /* hand these back to start or cancel construction   */
    int   cost;
    float progress;        /* 0..1                                              */
    int   completed, constructing, onhold, busy;
    /* THE FOOTPRINT, as the engine's own occupy list: cell offsets from the placement
     * origin, in the engine's CELL numbering. This build defines MEGAMAPS, so that
     * numbering is 128 wide -- a 2x2 building is {0, 1, 128, 129}, not {0, 1, 64, 65}.
     * Decoding it at stride 64 turns a compact footprint into a 66-cell horizontal line,
     * which is the kind of wrong that looks like a renderer bug. */
    short occupy[WB_OCCUPY_MAX];
    int   noccupy;
} W98_Build;

typedef struct
{
    int credits, tiberium, max_tiberium;
    int power_produced, power_drained;
    int repair_enabled, sell_enabled, radar_active;
    int count[2];                       /* left column, right column */
    W98_Build item[2][WB_MAX_BUILD];
} W98_Sidebar;

int wb_sidebar(W98_Sidebar *out);

/* ---- building ----------------------------------------------------------------------
 * CNC_Handle_Sidebar_Request(request, player, buildable_type, buildable_id, cx, cy).
 * The type and id come straight back off the W98_Build the sidebar reported, which is
 * the engine's own handle for the thing and not a name we matched.
 * ----------------------------------------------------------------------------------- */
void wb_build_start(int btype, int bid);      /* begin construction               */
void wb_build_hold(int btype, int bid);       /* pause it                         */
void wb_build_cancel(int btype, int bid);     /* abandon it                       */
void wb_place_begin(int btype, int bid);      /* a finished building: pick a spot */
void wb_place_at(int btype, int bid, int cx, int cy);
void wb_place_cancel(int btype, int bid);
void wb_repair_click(void);

/* ---- placement legality -------------------------------------------------------------
 *
 * GAME_STATE_PLACEMENT answers only while a building is pending placement, so a false
 * return doubles as "not in placement mode". It reports two bools per cell over the SAME
 * expanded map rectangle GAME_STATE_SHROUD uses:
 *
 *   prox   would placing the structure with its ORIGIN here satisfy the proximity rule
 *          for the whole building (the engine walks the occupy list itself)
 *   clear  is this one cell free of obstructions
 *
 * A cell is a legal origin when prox is set there AND clear is set on every cell of the
 * building's occupy list. Both arrays are 64x64, indexed [cy*64+cx] like the shroud.
 * Returns the number of cells the engine reported, or 0. */
int wb_placement(unsigned char *prox4096, unsigned char *clear4096);

/* The placement grid origin, which is what SIDEBAR_REQUEST_PLACE wants: the cell handed
 * to the engine is RELATIVE to the expanded map rectangle, not an absolute world cell.
 * Passing an absolute cell puts the building tens of cells away or fails silently. */
void wb_place_origin(int *cellx, int *celly);

/* ---- the shroud ---------------------------------------------------------------------
 * GAME_STATE_SHROUD is a flat per-cell array in the engine's own cell order. One byte per
 * cell out of this bridge: 0 unmapped (never seen), 1 mapped (seen, not currently in
 * anyone's sight), 2 visible. That is the same three-state answer the desktop build
 * draws, and it is READ, never guessed: the sim always knows the truth and the shroud is
 * a projection of it.
 * ----------------------------------------------------------------------------------- */
#define WB_SHROUD_UNMAPPED 0
#define WB_SHROUD_MAPPED   1
#define WB_SHROUD_VISIBLE  2

/* Fills a 64*64 array. Returns the cell count the engine reported, 0 if unavailable. */
int wb_shroud(unsigned char *out4096);

/* ---- overlay cells and effects ------------------------------------------------------
 * Tiberium, walls, bullets and animations are not techno objects and live in none of the
 * heaps the object dump walks, so the engine reports them on their own lines. They are
 * parsed out of the SAME capture wb_objects already takes, so asking for them costs
 * nothing: a second CNC3D_Dump_Objects would be another full walk of the map.
 * ----------------------------------------------------------------------------------- */
#define WB_MAX_TIB     2048
#define WB_MAX_WALLS    512
#define WB_MAX_BULLETS   96
#define WB_MAX_ANIMS    192
#define WB_MAX_SMUDGE   512

/* kind is Overlay - OVERLAY_TIBERIUM1 (which growth set), stage is OverlayData (which
 * frame of it). The 1995 engine uses OverlayData as the SHP frame index with no
 * arithmetic at all, so it transfers verbatim. */
typedef struct { short cx, cy; unsigned char kind, stage; } W98_Tib;
typedef struct { short cx, cy; char name[8]; unsigned char kind, icon, dmg; } W98_Wall;
/* Same shape as W98_Tib on purpose: a smudge draws exactly as a tiberium cell does,
 * so one renderer serves both. kind is the engine's SmudgeType, stage its SmudgeData. */
typedef struct { short cx, cy; unsigned char kind, stage; } W98_Smudge;
typedef struct { int lx, ly, face, alt, invis; char name[16]; } W98_Bullet;
typedef struct { int type, lx, ly, stage, start, stages, size, ground; char name[16]; } W98_Anim;

typedef struct
{
    W98_Tib    tib[WB_MAX_TIB];        int ntib;
    W98_Wall   wall[WB_MAX_WALLS];     int nwall;
    W98_Bullet bullet[WB_MAX_BULLETS]; int nbullet;
    W98_Anim   anim[WB_MAX_ANIMS];     int nanim;
    W98_Smudge smudge[WB_MAX_SMUDGE];  int nsmudge;
    int        tib_dropped, wall_dropped, bullet_dropped, anim_dropped, smudge_dropped;
} W98_Overlays;

/* Valid until the next wb_objects(). Never NULL. */
const W98_Overlays *wb_overlays(void);

/* The static map, read once after wb_start. */
typedef struct { int theater, cellx, celly, cellw, cellh; char scenario[36]; } W98_MapInfo;
int wb_map(W98_MapInfo *out);

void wb_close(void);

#ifdef __cplusplus
}
#endif

#endif /* W98_BRAIN_H */
