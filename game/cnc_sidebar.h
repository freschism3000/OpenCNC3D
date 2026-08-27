/* ==================================================================================== *
 *  CNC3D -- THE SIDEBAR
 *
 *  Everything the player needs in order to BUILD: the buildable list with its cameo art,
 *  the credits/power readout, construction progress, and the placement cursor with the
 *  engine's own valid/invalid cell overlay.
 *
 *  WHAT THIS TALKS TO
 *    CNC_Get_Game_State(GAME_STATE_SIDEBAR)    -> CNCSidebarStruct  (see below)
 *    CNC_Get_Game_State(GAME_STATE_PLACEMENT)  -> CNCPlacementInfoStruct
 *    CNC_Handle_Sidebar_Request(...)           -> start/hold/cancel/place
 *  Nothing else. No engine internals, no art loading through the brain.
 *
 *  WHAT IT DELIBERATELY DOES NOT TALK TO
 *    SDL, or any window system. It is handed pixel coordinates and a button, and it
 *    draws with OpenGL 1.x immediate mode only: no shaders, no VBOs, no extensions, all
 *    transforms CPU-side. That is what the Win98 + 3dfx Voodoo2 target can run, where
 *    every glBegin(GL_QUADS) here becomes two grDrawTriangles.
 *
 *  ------------------------------------------------------------------------------------
 *  THE FACTS, AS READ OUT OF dllinterface.cpp AND THEN VERIFIED AGAINST A RUNNING GAME
 *  ------------------------------------------------------------------------------------
 *
 *  GAME_STATE_SIDEBAR fills a CNCSidebarStruct followed by a variable-length array of
 *  CNCSidebarEntryStruct. EntryCount[0] is the LEFT column (structures) and EntryCount[1]
 *  the RIGHT column (infantry/vehicles/aircraft/superweapons); the entries are stored
 *  back to back, left column first. Per entry the useful fields are:
 *
 *    AssetName        the INI name, e.g. "NUKE", "PROC", "E1", "MTNK". This is exactly
 *                     the name of the CAM_*.png cameo, so the art lookup is a strcmp.
 *    BuildableType    an RTTIType: 2 INFANTRYTYPE, 4 UNITTYPE, 6 AIRCRAFTTYPE,
 *                     8 BUILDINGTYPE, 25 SPECIAL. Pass it back UNCHANGED.
 *    BuildableID      the type index inside that RTTI. Pass it back UNCHANGED.
 *                     (BuildableType, BuildableID) is the only handle the DLL accepts.
 *    Cost             already multiplied by the house CostBias.
 *    BuildTime        ticks, from TechnoTypeClass::Time_To_Build.
 *    PowerProvided    Power - Drain for structures, so it is signed and it is what the
 *                     power meter should preview. 0 for everything else.
 *    Progress         0..1, factory->Completion()/FactoryClass::STEP_COUNT.
 *    Constructing     the factory is running.
 *    ConstructionOnHold  the factory exists, is not running and has not completed.
 *    Completed        the factory has finished; for a BUILDING this is the moment the
 *                     player may enter placement mode.
 *    Busy             THIS COLUMN's factory is occupied by some other item (or the heap
 *                     is full). It is a per-column interlock, not per-item: while a
 *                     structure is building, every structure in the column reports busy.
 *    PlacementList / PlacementListLength
 *                     only filled in once Completed && BUILDING_TYPE. These are CELL
 *                     OFFSETS to be added to the origin cell with plain integer
 *                     arithmetic (cell + offset), map stride 128 under MEGAMAPS. It is
 *                     BuildingTypeClass::Occupy_List(true), i.e. the foundation PLUS the
 *                     bib, which is why a 2x2 power plant reports 7 cells over 3 rows.
 *
 *  GAME_STATE_PLACEMENT returns false unless a building is pending placement, so it
 *  doubles as "am I in placement mode". When it succeeds it is a Count-prefixed grid of
 *  { PassesProximityCheck, GenerallyClear } over the SAME rectangle that
 *  GAME_STATE_STATIC_MAP reports: origin (MapCellX, MapCellY), size MapCellWidth x
 *  MapCellHeight. Both entry points apply the identical -1/+1 expansion to Map.MapCell*,
 *  so no second correction is needed here; the probe confirmed Count == MapCellWidth *
 *  MapCellHeight (700 == 28*25 on SCG01EA).
 *    PassesProximityCheck is evaluated for the ORIGIN cell and already accounts for the
 *      pending building's whole footprint (Passes_Proximity_Check walks its occupy list),
 *      so it answers "may this building start here", not "is this cell near a building".
 *    GenerallyClear is per cell (CellClass::Is_Generally_Clear).
 *  A cell is a legal origin when it passes proximity AND every cell of the occupy list
 *  is generally clear. That is the rule the cursor draws and the rule used here.
 *
 *  CNC_Handle_Sidebar_Request(request, player_id, buildable_type, buildable_id, cx, cy).
 *  The sequence, from DLLExportClass::Construction_Action / Start_Placement / Place:
 *
 *    SIDEBAR_REQUEST_START_CONSTRUCTION   0   no factory yet -> queues EventClass::PRODUCE
 *                                             and calls Queue_AI() immediately, so the
 *                                             sidebar reflects it on the very next poll.
 *                                             With a SUSPENDED factory -> resumes it.
 *                                             With a RUNNING factory -> refused
 *                                             ("cannot comply"), it is not a toggle.
 *    SIDEBAR_REQUEST_HOLD_CONSTRUCTION    1   EventClass::SUSPEND. Progress is kept.
 *    SIDEBAR_REQUEST_CANCEL_CONSTRUCTION  2   cancels placement first, then
 *                                             EventClass::ABANDON (refunds).
 *    SIDEBAR_REQUEST_START_PLACEMENT      3   only meaningful once Completed and the
 *                                             item is a building. Sets PlacementType,
 *                                             recalculates the proximity distances and
 *                                             calls HouseClass::Manual_Place. AFTER this
 *                                             GAME_STATE_PLACEMENT starts answering.
 *    SIDEBAR_REQUEST_PLACE                4   cell_x/cell_y are NOT engine cells. They
 *                                             are indices into the placement grid above:
 *                                             engine CELL = (MapCellY + cy) * 128 +
 *                                             (MapCellX + cx). Calls Place_Object
 *                                             directly, so success is observable on the
 *                                             next tick as the building leaving limbo.
 *    SIDEBAR_CANCEL_PLACE                 5   back to "completed, awaiting placement".
 *    SIDEBAR_CLICK_REPAIR                 6   (not used here)
 *
 *  Events queued by these calls are applied by the engine's own Queue_AI, so a single
 *  CNC_Advance_Instance after the request is enough to see the effect.
 * ==================================================================================== */
#ifndef CNC_SIDEBAR_H
#define CNC_SIDEBAR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

/* THE SIDEBAR THE PLAYER ACTUALLY SEES is the 1995 MS-DOS one, rasterised on the CPU
   into an 8-bit 320x200 surface by dosbar.c and handed to the GPU as ONE texture drawn
   as ONE quad. Everything below this line that used to draw a panel out of GL rectangles
   is gone; what is left is the engine plumbing (poll, requests, placement, the script
   verbs), a DB_State filler, and hit testing done in DOS pixel coordinates. */
#include "dosbar.h"
/* WHICH SIDEBAR THE GAME DRAWS, asked for by the Visuals dialog or the Enhanced path.
   Safe to call before the pack is loaded: the wish is remembered and applied at load.

   NOTE WHAT DOES **NOT** CALL THIS: `--gfx`. The measuring instruments must keep the 1995
   DOS bar whatever fx_defaults says, and several gates depend on it -- G36f compares a
   --gfx run against a no-gfx one and asserts that ZERO sidebar columns differ between
   them, which a HUD swap would break by tens of thousands of pixels. So the dial is
   pushed into the sidebar only where a PLAYER changed it: the Enhanced path the shipping
   binary boots through, and the Visuals screen. This mirrors bilinear exactly. */
static void sb_set_hud_new(int on);

/* The 640x480 replacement HUD. Both paths are compiled; g_hudNew picks one at runtime so
 * the old DOS bar stays available for comparison until the new one is signed off. */
#include "hud640.h"

/* ---- what the host must provide ---------------------------------------------------- */

struct SbHooks {
    /* the brain, already dlsym'd by the host */
    bool (*GetState)(GameStateRequestEnum, uint64, unsigned char*, unsigned int);
    void (*SidebarReq)(SidebarRequestEnum, uint64, int, int, short, short);
    bool (*Advance)(uint64);
    int (*Dump)(void);
    /* the renderer's own projection, so the placement overlay lands on the right cells
       without this file knowing anything about the camera */
    void (*WorldToScreen)(float wx, float wy, float wz, int fbw, int fbh,
                          float* col, float* row);
    /* The renderer's heightfield, exact CORNER values, which are the same numbers the
       terrain pass emits as its vertices. The placement overlay needs them because it
       draws in world space through WorldToScreen above, and the ground is not the y = 0
       plane. May be left NULL: a host with no terrain then draws flat, which is what this
       file did before the hook existed. */
    float (*TerrainCornerY)(int cx, int cz);
    /* Called before the panel is laid out. The renderer parks its radar inside the
       panel and reports the height it took via sb_reserve_top(), and the layout has to
       agree with that whether it is about to DRAW a frame or merely hit-test a scripted
       click. Without this hook a script that clicks before the first frame is drawn
       tests a layout nobody ever saw. */
    void (*Layout)(int fbw, int fbh);
    /* The radar CONTENTS. dosbar.c draws the RADAR.<house> bezel and then fills the
       interior BLACK exactly as radar.cpp:433 does; what goes inside it is the host's
       map, so the host plots it straight into the same 8-bit surface. `active` is false
       when the player has switched the radar off, in which case the interior stays the
       black the bezel left. */
    void (*RadarPlot)(DB_Surface* s, const DB_Pack* p, int active);
    /* THE SAME RADAR, for the 640x480 art HUD, which wants RGBA in its own 136x120 box
       rather than indices in the DOS bar's 72x69 hole. It needs its own hook because
       sb_draw_panel branches to the new HUD and RETURNS above the RadarPlot call, so
       that one is simply never reached on this path -- which is why the new HUD showed
       the faction emblem for ever: not a broken radar, an unwired one. */
    void (*RadarPlotRGBA)(unsigned char* rgba, int w, int h, const DB_Pack* p, int active);
    /* Repair / Sell are DOS toggle buttons: pressing one puts the engine into that mode.
       CNC_Handle_Structure_Request(request, player_id, object_id). */
    void (*StructureReq)(StructureRequestEnum, uint64, int);
    /* The superweapons. CNC_Handle_SuperWeapon_Request(request, player_id,
       buildable_type, buildable_id, x, y) with x/y in ENGINE PIXELS, which
       DLLExportClass::Place_Super_Weapon turns into a cell and posts as
       EventClass::SPECIAL_PLACE. Present in the shipped brain since the beginning and
       referenced nowhere under game/ until v0.5.9. */
    void (*SuperWeaponReq)(SuperWeaponRequestEnum, uint64, int, int, int, int);
    /* THE TWO THINGS ARMING A SPECIAL DOES BESIDES ARMING IT. sidebar.cpp:2304-2307
       runs three statements in a row when a ready special is clicked: it sets
       IsTargettingMode, it calls Unselect_All(), and it says "select target". The
       selection clear is not cosmetic: the engine reads targeting only in the
       no-selection arm of its cursor logic (display.cpp:3279-3327), so without the
       clear the targeting cursor is unreachable. Both live out here rather than in this
       file because the selection is mirrored from the brain by the renderer and the
       speech queue belongs to the audio engine.
         Unselect: clears the engine's selection and re-syncs the mirror.
         Speak:    an index into the VOX table; 33 is SELECT1, VOX_SELECT_TARGET. */
    void (*Unselect)(void);
    void (*Speak)(int vox_index);
    /* GAME_STATE_STATIC_MAP geometry; also the placement grid origin and size */
    int mapX, mapY, mapW, mapH;
    /* radar.cpp:348 picks the bezel by house: the Brotherhood gets RADARNOD. */
    int nod;
};

enum SbScriptResult { SB_SCRIPT_CONTINUE = 0, SB_SCRIPT_SHOT = 1, SB_SCRIPT_DONE = 2 };

/* ==================================================================================== *
 *  Cameo + font pack (see bake_cameos.py)
 * ==================================================================================== */

struct SbTex {
    char name[9];
    int w, h, uw, uh;
    GLuint gl;
};

static std::vector<SbTex> g_sbCameo;
static SbTex g_sbFont;
static int g_sbFontCellW = 8, g_sbFontCellH = 11, g_sbFontCols = 47, g_sbFontFirst = 33;
static bool g_sbHaveFont = false;

static GLuint sb_upload(int w, int h, const unsigned char* rgba)
{
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    /* GL_NEAREST everywhere: this is pixel art and the Voodoo2 path will want point
       sampling too. No mipmaps, no anisotropy, no extensions. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* NOT registered: UI art is never bilinear. See fx_filter.h. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return id;
}

static bool sb_load_cameos(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sidebar: no cameo pack at %s (run bake_cameos.py); "
                        "falling back to text-only buttons\n", path);
        return false;
    }
    char magic[8];
    unsigned ver = 0, count = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "CNC3DCAM", 8) != 0) {
        fprintf(stderr, "sidebar: %s is not a cameo pack\n", path);
        fclose(f);
        return false;
    }
    if (fread(&ver, 4, 1, f) != 1 || fread(&count, 4, 1, f) != 1) { fclose(f); return false; }
    std::vector<unsigned char> buf;
    for (unsigned i = 0; i < count; i++) {
        SbTex t;
        memset(&t, 0, sizeof(t));
        unsigned d[4];
        if (fread(t.name, 1, 8, f) != 8 || fread(d, 4, 4, f) != 4) { fclose(f); return false; }
        t.name[8] = 0;
        t.w = (int)d[0]; t.h = (int)d[1]; t.uw = (int)d[2]; t.uh = (int)d[3];
        buf.resize((size_t)t.w * t.h * 4);
        if (fread(&buf[0], 1, buf.size(), f) != buf.size()) { fclose(f); return false; }
        t.gl = sb_upload(t.w, t.h, &buf[0]);
        g_sbCameo.push_back(t);
    }
    if (ver >= 2) {
        unsigned d[8];
        if (fread(d, 4, 8, f) == 8) {
            memset(&g_sbFont, 0, sizeof(g_sbFont));
            g_sbFont.w = (int)d[0]; g_sbFont.h = (int)d[1];
            g_sbFont.uw = (int)d[2]; g_sbFont.uh = (int)d[3];
            g_sbFontCellW = (int)d[4]; g_sbFontCellH = (int)d[5];
            g_sbFontCols = (int)d[6]; g_sbFontFirst = (int)d[7];
            buf.resize((size_t)g_sbFont.w * g_sbFont.h * 4);
            if (fread(&buf[0], 1, buf.size(), f) == buf.size()) {
                g_sbFont.gl = sb_upload(g_sbFont.w, g_sbFont.h, &buf[0]);
                g_sbHaveFont = true;
            }
        }
    }
    fclose(f);
    fprintf(stderr, "sidebar: %u cameos%s from %s\n", count,
            g_sbHaveFont ? " + FONT.SHP" : " (no font)", path);
    return true;
}

static const SbTex* sb_cameo(const char* name)
{
    for (size_t i = 0; i < g_sbCameo.size(); i++)
        if (!strcasecmp(g_sbCameo[i].name, name))
            return &g_sbCameo[i];
    return NULL;
}

/* ==================================================================================== *
 *  Primitive 2D drawing. Pixel space, y down; the host has already set up the ortho.
 * ==================================================================================== */

static void sb_quad(float x0, float y0, float x1, float y1)
{
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
}

static void sb_rect(float x0, float y0, float x1, float y1,
                    float r, float g, float b, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS); sb_quad(x0, y0, x1, y1); glEnd();
}

static void sb_frame(float x0, float y0, float x1, float y1,
                     float r, float g, float b, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x0 + 0.5f, y0 + 0.5f); glVertex2f(x1 - 0.5f, y0 + 0.5f);
    glVertex2f(x1 - 0.5f, y1 - 0.5f); glVertex2f(x0 + 0.5f, y1 - 0.5f);
    glEnd();
}

/* C&C's own FONT.SHP, baked to white ink + black outline. GL_MODULATE keeps the outline
   black whatever the tint, because 0 * anything is 0.
   TWO THINGS THAT ARE NOT COSMETIC:
   the scale is snapped to a whole number and the quad corners to whole pixels, and the
   UVs are pulled in by a quarter texel. Without either, a 1.6x glyph sampled GL_NEAREST
   picks up the neighbouring cell of the atlas at its edges and "BUILD" comes out as
   "DLILD". Same hazard on the Voodoo2, where point sampling is the only option. */
static float sb_text(float x, float y, const char* s, float scale,
                     float r, float g, float b)
{
    if (!g_sbHaveFont) return x;
    if (scale < 1.0f) scale = 1.0f;
    scale = floorf(scale + 0.5f);
    x = floorf(x); y = floorf(y);
    const float cw = (float)g_sbFontCellW * scale, ch = (float)g_sbFontCellH * scale;
    const float us = 1.0f / (float)g_sbFont.w, vs = 1.0f / (float)g_sbFont.h;
    const float eu = us * 0.25f, ev = vs * 0.25f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_sbFont.gl);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_QUADS);
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        if (c > ' ' && c >= g_sbFontFirst) {
            int idx = c - g_sbFontFirst;
            int col = idx % g_sbFontCols, row = idx / g_sbFontCols;
            float u0 = (float)(col * g_sbFontCellW) * us + eu;
            float v0 = (float)(row * g_sbFontCellH) * vs + ev;
            float u1 = u0 + (float)g_sbFontCellW * us - 2.0f * eu;
            float v1 = v0 + (float)g_sbFontCellH * vs - 2.0f * ev;
            glTexCoord2f(u0, v0); glVertex2f(x,      y);
            glTexCoord2f(u1, v0); glVertex2f(x + cw, y);
            glTexCoord2f(u1, v1); glVertex2f(x + cw, y + ch);
            glTexCoord2f(u0, v1); glVertex2f(x,      y + ch);
        }
        x += cw;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    return x;
}

static float sb_text_w(const char* s, float scale)
{
    if (scale < 1.0f) scale = 1.0f;
    scale = floorf(scale + 0.5f);
    return (float)strlen(s) * (float)g_sbFontCellW * scale;
}

/* ==================================================================================== *
 *  State mirrored from the brain
 * ==================================================================================== */

struct SbEntry {
    char name[16];
    int btype, bid;
    int type;              /* DllObjectTypeEnum */
    int cost, buildTime, power;
    float progress;
    bool completed, constructing, onHold, busy;
    int column;
    std::vector<short> occupy;
};

struct SbState {
    bool valid;
    int credits, creditsCounter, tiberium, maxTiberium;
    int powerProduced, powerDrained;
    bool repairEnabled, sellEnabled, radarActive;
    std::vector<SbEntry> entry;
};

static SbHooks g_sb;
static SbState g_sbState;
static bool g_sbOn = true;
static bool g_sbReady = false;

/* placement mirror */
static bool g_sbPlacing = false;
static int g_sbPlaceIdx = -1;                 /* which entry is pending */
/* THE PENDING BUILDING'S OWN IDENTITY, not its position in the strip.
   An index alone is not enough. If the Construction Yard dies or is sold while a
   finished building is waiting to be placed, GAME_STATE_PLACEMENT keeps answering, so
   g_sbPlacing stays true, while GAME_STATE_SIDEBAR drops to zero entries -- and the
   index then points past the end of a cleared vector. That cost the player the whole
   mission: sb_cancel_placement routed through sb_request, which bails on an
   out-of-range index, so the engine was never told to cancel, the next poll re-asserted
   placement mode, and because the SDL handler short-circuits the left button into
   sb_place_at while placing, the left mouse button stopped selecting anything at all
   for the rest of the mission with no way back. The two footprint dereferences were
   also reading a std::vector whose destructor had already run.
   Keeping (btype, bid) lets the cancel still reach the engine when the strip is empty,
   and lets the index be re-resolved every poll instead of trusted. */
static int g_sbPlaceBType = -1, g_sbPlaceBId = -1;
static std::vector<unsigned char> g_sbPlaceProx, g_sbPlaceClear;
static int g_sbHoverX = -1, g_sbHoverY = -1;

/* ---- the DOS sidebar ---------------------------------------------------------------
   One 8-bit 320x200 screen, drawn by dosbar.c, converted once, uploaded once per frame
   into one power-of-two texture and drawn as two quads: the sidebar column and the
   credits plate that in DOS sits immediately to its left on the tab bar. Everything the
   Voodoo2 has to do is a texture upload and four vertices. */
/* ---- the 640x480 HUD ---------------------------------------------------------------
   Same idea as the block below it, one step further along: hud640.c composes straight
   into RGBA, so there is no palette conversion between rasterise and upload. */
static H6_Pack*   g_h6Pack = NULL;
static bool       g_hudNew = false;          /* see sb_set_hud_new */
/* -1 = nobody has asked, so the CNC3D_HUD environment variable still decides. 0/1 = the
   Visuals dialog or the Enhanced path has an opinion and it wins. */
static int        g_hudNewWish = -1;
/* CNC3D_HUD IS A STARTING VALUE, NOT A VETO, and this was got wrong once in the obvious
   direction. When G53 went red on the day the New HUD dial landed, the env var was made a
   HARD override so the gate could pin the HUD it wanted. That broke the feature outright
   for the person who asked for it: `tools/launchers/C&C3D.app/.../cnc3d-launch:39` and
   `PLAY-HUD-MENU.command` both set CNC3D_HUD=new, so on the original launcher the new
   checkbox did nothing at all in either direction. He reported it within the hour.

   THE PLAYER'S DIAL ALWAYS WINS. A launcher's environment says what to start with; a
   checkbox the player just clicked says what they want now. G53's real problem was
   different and is fixed where it belongs: that gate runs with the post chain OFF, so the
   pause dialog reads the state as CLASSIC -- correctly, and Classic correctly means the
   DOS bar -- and applying it switched the HUD out from under the gate mid-run. The gate
   now asks for the chain, so the dialog sees ENHANCED and the HUD it set up survives. */
static void sb_set_hud_new(int on)
{
    g_hudNewWish = on ? 1 : 0;
    g_hudNew = (g_h6Pack && g_hudNewWish == 1);
}

/* WHICH SIDEBAR IS ACTUALLY ON SCREEN, as opposed to which one was asked for. The two can
   disagree when hud640.pack is missing, and the whole point of reporting both is that a
   gate asserting only the checkbox would have passed through the week the dial was inert. */
static int sb_hud_is_new(void) { return g_hudNew ? 1 : 0; }
/* Sized for the TALLEST bar, not the authored one: the number of build rows follows the
   screen height now (hud640.h, "MORE ROWS ON A TALLER SCREEN"). g_h6Rows is what it came
   out at this frame and every reader below asks it rather than assuming H6_ROWS. */
static unsigned char g_h6Bar[H6_BAR_W * H6_MAX_BAR_H * 4];
static int g_h6Rows = H6_ROWS;
/* One cameo per visible slot, converted from the pack's 8-bit art. Sized to the CAMEO
   box (64x48), NOT the well (61x45): the cameo deliberately overhangs the opening, see
   hud640_layout.h. Leaving this at the well size overruns each slot into the next by
   1308 bytes and the last one past the end of the array. */
static unsigned char g_h6Cameo[H6_COLUMNS * H6_MAX_ROWS][H6_CAMEO_W * H6_CAMEO_H * 4];
static unsigned char g_h6Radar[H6_RADAR_W * H6_RADAR_H * 4];

/* THE TRUE-COLOUR CAMEOS. 64x48 RGBA, one per buildable, baked by
   tools/sidebar_redesign/cameos_tdr.py from the 128x96 source art in art/cameos-tdr.

   They exist because the 640 HUD's cameo buffer above is RGBA but the path that FILLS it
   was not: the cell is composed in 8-bit index space and only expanded through the DOS
   palette at the last step, which costs full-colour art a visible per-pixel RGB error.
   The reason that path is 8-bit is the build clock, which is a lookup through the
   pack's own translucency table rather than paint -- so going true-colour means doing
   in RGB what that table does in indices. See h6_clock_fade below; it is decoded, not guessed. */
typedef struct { char key[9]; unsigned char* rgba; } H6_TdrCam;
static H6_TdrCam* g_tdrCam = NULL;
static int        g_tdrCamN = -1;   /* -1 = not tried yet, 0 = none */

static void h6_tdr_load(const char* path)
{
    FILE* f;
    char magic[8];
    unsigned int n, i;
    if (g_tdrCamN >= 0) return;
    g_tdrCamN = 0;
    f = fopen(path, "rb");
    if (!f) return;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "CNC3DTDR", 8) != 0
        || fread(&n, 4, 1, f) != 1 || n == 0 || n > 512) { fclose(f); return; }
    g_tdrCam = (H6_TdrCam*)calloc(n, sizeof(H6_TdrCam));
    if (!g_tdrCam) { fclose(f); return; }
    for (i = 0; i < n; i++) {
        unsigned int w, h;
        size_t bytes;
        if (fread(g_tdrCam[i].key, 1, 8, f) != 8
            || fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1
            || w != H6_CAMEO_W || h != H6_CAMEO_H) break;
        bytes = (size_t)w * h * 4;
        g_tdrCam[i].rgba = (unsigned char*)malloc(bytes);
        if (!g_tdrCam[i].rgba || fread(g_tdrCam[i].rgba, 1, bytes, f) != bytes) break;
        g_tdrCam[i].key[8] = 0;
        g_tdrCamN++;
    }
    fclose(f);
    fprintf(stderr, "sidebar: %d true-colour cameos loaded from %s\n", g_tdrCamN, path);
}

static const unsigned char* h6_tdr_find(const char* key)
{
    int i;
    for (i = 0; i < g_tdrCamN; i++)
        if (!strcmp(g_tdrCam[i].key, key)) return g_tdrCam[i].rgba;
    return NULL;
}

/* THE BUILD CLOCK, IN RGB. The DOS engine fades the cell under the clock wedge with
   `TLucentType ClockCols = {GREEN, LTGREY, 180, 0}` -- 180/256 of the way from the pixel
   toward LTGREY -- and bakes that into a 256-entry table because it has to answer in
   palette indices. Both numbers were recovered from the table's own bytes rather than
   taken on trust: fitting `dst*(1-a) + C*a` across all 256 entries returns a = 0.715
   against the engine's literal 180/256 = 0.703, and C = (172,171,167) against palette
   index 14, LTGREY, which is (168,168,168). 192 of the 256 entries are reproduced exactly
   by snapping that ideal blend back to the palette; the other 64 are the DOS table
   builder's own rounding in index space.

   In RGB there is nothing to snap, so this is not an approximation OF the table -- it is
   the operation the table itself approximates. */
static void h6_clock_fade(unsigned char* px)
{
    enum { LTGREY_R = 168, LTGREY_G = 168, LTGREY_B = 168, FADE = 180 };
    px[0] = (unsigned char)((px[0] * (256 - FADE) + LTGREY_R * FADE) >> 8);
    px[1] = (unsigned char)((px[1] * (256 - FADE) + LTGREY_G * FADE) >> 8);
    px[2] = (unsigned char)((px[2] * (256 - FADE) + LTGREY_B * FADE) >> 8);
}
/* The new HUD magnifies in HALF steps, not whole ones. 480 native rows into a 720-row
   window is exactly 1.5, which fills the height and puts the bar at 240 wide, the same
   footprint the DOS bar gets at 3x. A half step is still a regular 2-1-2-1 pixel
   pattern under NEAREST, not a resample. */
static float g_h6Scale = 1.5f;
static unsigned char g_h6Tab[H6_BAR_W * H6_TAB_H * 4];
static GLuint     g_h6TexBar = 0, g_h6TexCred = 0, g_h6TexOpt = 0, g_h6TexSide = 0;
#define H6_POT_W 256
/* 1024, not 512: a twelve-row bar is 816 tall. The power-of-two upload is one texture
   for every bar height, so the atlas is sized once for the maximum and the quad's v
   range does the rest. */
#define H6_POT_H 1024

static DB_Pack*   g_dbPack = NULL;
static DB_Pack*   g_h6CamPack = NULL;      /* C&C95 cameos at 61x45, optional */
static DB_State   g_dbState;
static DB_Surface g_dbSurf;
static unsigned char g_dbScreen[DB_SCREEN_W * DB_SCREEN_H];

/* The strip of the DOS screen we actually put on the GPU: x 160..319, which is the
   credits plate plus the whole sidebar column. */
#define SB_TEX_X   DB_CREDIT_TAB_X
#define SB_TEX_W   (DB_SCREEN_W - SB_TEX_X)     /* 160 */
#define SB_TEX_H   DB_SCREEN_H                  /* 200 */
#define SB_TEX_POT 256                          /* GL 1.1 and Glide both want POT */
static unsigned char g_dbCrop[SB_TEX_W * SB_TEX_H];
static unsigned char g_dbRGBA[SB_TEX_W * SB_TEX_H * 4];
static GLuint g_dbTex = 0;

/* Layout, recomputed every frame. The DOS sidebar is a fixed 80x200 pixel column, so the
   only free parameter is a WHOLE-number magnification; anything else would resample
   1995 pixel art. g_dbY0 is where DOS row 0 lands. */
static int   g_dbScale = 3;
static float g_dbX0 = 240.0f, g_dbY0 = 0.0f, g_dbW = 240.0f, g_dbH = 600.0f;
enum SbHit {
    SBH_NONE = 0, SBH_ITEM, SBH_UP, SBH_DOWN,
    SBH_REPAIR, SBH_SELL, SBH_MAP, SBH_RADAR,
    /* THE THREE SCREEN-EDGE PLATES, added at the END so every existing index and every
       baked frame strip keeps its number. They are not part of the bar: OPTIONS is pinned
       to the LEFT edge of the window and the other two to the right, which is exactly why
       sb_over_panel could not see them and why the pointer went behind OPTIONS. */
    SBH_OPTIONS, SBH_SIDEBAR, SBH_CREDITS
};

/* THE DRAWER. The bar slides out to the right and back, in BAR-LOCAL INTEGER pixels so
   the screen offset stays a whole number: a fractional one would resample 1995 pixel art
   under GL_NEAREST for the whole travel. Driven off the DRAWN FRAME, never a wall clock --
   this renderer already had to rip SDL_GetTicks out of the order markers for exactly that
   reason, and a wall-clock slide would make every --shot gate non-reproducible. */
#define H6_SLIDE_STEP 10          /* bar px per drawn frame; 160/10 = 16 frames */
static int g_h6SlideX = 0;        /* 0 = shown, H6_BAR_W = fully hidden */
static int g_h6SlideTarget = 0;

/* The framebuffer the layout above was computed for. The window-pinned plates are tested
   in SCREEN space and some callers reach that test without an fbw of their own. */
static int g_dbFbw = 0, g_dbFbh = 0;

/* TopIndex per column, exactly StripClass::TopIndex. */
static int g_sbTop[DB_COLUMNS] = {0, 0};
/* The Repair and Sell toggles. TextButtonClass IsToggleType: the caption goes RED while
   the mode is on (textbtn.cpp:336 via the ButtonColorsClassic table). */
static bool g_sbRepairOn = false, g_sbSellOn = false;
/* The MAP button. In DOS this is the radar Zoom toggle; here it turns the radar plot on
   and off, which is also what the M key does.
   The ENGINE decides whether there is a radar at all: RadarMapActive is
   PlayerPtr->Radar == RADAR_ON (dllinterface.cpp:4025), so in 1995 the hole stays black
   until a Comm Center is standing, and neither shipped mission 1 has one. That is
   faithful and it is also useless for a renderer whose whole job is looking at the map,
   so the OVERRIDE IS THE DEFAULT here and --dosradar restores the 1995 rule. This is the
   one place the sidebar deliberately shows more than the DOS build would. */
static bool g_sbRadarShown = true;
/* FALSE by default since v0.6.0. It used to default to TRUE, which made
   sb_radar_shown() ignore RadarMapActive entirely, so the minimap was on from the first
   frame of every mission whether or not the player had built a Communications Center.
   Players reported exactly that. The faithful behaviour is now the default and forcing
   is opt-in through --forceradar, which is the way round it should always have been: a
   diagnostic override that ships enabled is not an override, it is the behaviour. */
static bool g_sbRadarForce = false;

static std::vector<unsigned char> g_sbBuf;

static void sb_poll(void)
{
    if (g_sbBuf.size() < (1u << 20)) g_sbBuf.resize(1u << 20);
    g_sbState.valid = false;
    g_sbState.entry.clear();
    memset(&g_sbBuf[0], 0, 4096);
    if (!g_sb.GetState || !g_sb.GetState(GAME_STATE_SIDEBAR, 0, &g_sbBuf[0],
                                         (unsigned)g_sbBuf.size()))
        return;
    const CNCSidebarStruct* s = (const CNCSidebarStruct*)&g_sbBuf[0];
    g_sbState.valid = true;
    g_sbState.credits = s->Credits;
    g_sbState.creditsCounter = s->CreditsCounter;
    g_sbState.tiberium = s->Tiberium;
    g_sbState.maxTiberium = s->MaxTiberium;
    g_sbState.powerProduced = s->PowerProduced;
    g_sbState.powerDrained = s->PowerDrained;
    g_sbState.repairEnabled = s->RepairBtnEnabled;
    g_sbState.sellEnabled = s->SellBtnEnabled;
    g_sbState.radarActive = s->RadarMapActive;
    int n = s->EntryCount[0] + s->EntryCount[1];
    for (int i = 0; i < n; i++) {
        const CNCSidebarEntryStruct& e = s->Entries[i];
        SbEntry x;
        memset(x.name, 0, sizeof(x.name));
        strncpy(x.name, e.AssetName, sizeof(x.name) - 1);
        x.btype = e.BuildableType;
        x.bid = e.BuildableID;
        x.type = (int)e.Type;
        x.cost = e.Cost;
        x.buildTime = e.BuildTime;
        x.power = e.PowerProvided;
        x.progress = e.Progress;
        x.completed = e.Completed;
        x.constructing = e.Constructing;
        x.onHold = e.ConstructionOnHold;
        x.busy = e.Busy;
        x.column = (i < s->EntryCount[0]) ? 0 : 1;
        for (int k = 0; k < e.PlacementListLength && k < MAX_OCCUPY_CELLS; k++)
            x.occupy.push_back(e.PlacementList[k]);
        g_sbState.entry.push_back(x);
    }

    /* placement mirror: GAME_STATE_PLACEMENT answering at all IS placement mode */
    const int gw = g_sb.mapW, gh = g_sb.mapH;
    g_sbPlacing = false;
    if (g_sb.GetState(GAME_STATE_PLACEMENT, 0, &g_sbBuf[0], (unsigned)g_sbBuf.size())) {
        const CNCPlacementInfoStruct* p = (const CNCPlacementInfoStruct*)&g_sbBuf[0];
        if (p->Count == gw * gh) {
            g_sbPlaceProx.assign((size_t)gw * gh, 0);
            g_sbPlaceClear.assign((size_t)gw * gh, 0);
            for (int i = 0; i < p->Count; i++) {
                g_sbPlaceProx[i] = p->CellInfo[i].PassesProximityCheck ? 1 : 0;
                g_sbPlaceClear[i] = p->CellInfo[i].GenerallyClear ? 1 : 0;
            }
            g_sbPlacing = true;
        } else {
            fprintf(stderr, "sidebar: placement grid is %d cells, static map says %d "
                            "(%dx%d) -- refusing to guess\n", p->Count, gw * gh, gw, gh);
        }
    }
    if (!g_sbPlacing) { g_sbPlaceIdx = -1; g_sbPlaceBType = g_sbPlaceBId = -1; }
    if (g_sbPlacing) {
        /* Re-resolve the index EVERY poll rather than trusting a stored one: the strip
           is rebuilt from scratch on each poll and entries move and disappear. Identity
           first, then the old completed-building heuristic as the fallback. */
        g_sbPlaceIdx = -1;
        if (g_sbPlaceBType >= 0) {
            for (size_t i = 0; i < g_sbState.entry.size(); i++)
                if (g_sbState.entry[i].btype == g_sbPlaceBType
                    && g_sbState.entry[i].bid == g_sbPlaceBId)
                    { g_sbPlaceIdx = (int)i; break; }
        }
    }
    if (g_sbPlacing && g_sbPlaceIdx < 0) {
        /* recover which entry is pending: the one that is completed and a building */
        for (size_t i = 0; i < g_sbState.entry.size(); i++)
            if (g_sbState.entry[i].completed && g_sbState.entry[i].type == BUILDING_TYPE)
                { g_sbPlaceIdx = (int)i;
                  g_sbPlaceBType = g_sbState.entry[i].btype;
                  g_sbPlaceBId   = g_sbState.entry[i].bid; break; }
    }
    g_sbReady = true;
}

static int sb_find(const char* name)
{
    for (size_t i = 0; i < g_sbState.entry.size(); i++)
        if (!strcasecmp(g_sbState.entry[i].name, name))
            return (int)i;
    return -1;
}

/* ---- requests ---------------------------------------------------------------------- */

/* ---- THE SUPERWEAPON NAME COLLISION -------------------------------------------------
 *  The Remaster DLL names the three specials by its own asset strings, "SW_Ion",
 *  "SW_Nuke" and "SW_AirStrike" (dllinterface.cpp:4183-4199). Every cameo pack in this
 *  project is keyed by the 1995 stems, and 1995 names them in one line:
 *
 *      sidebar.cpp:1146   static const char* _file[3] = {"ION", "ATOM", "BOMB"};
 *
 *  in SPC_ION_CANNON / SPC_NUCLEAR_BOMB / SPC_AIR_STRIKE order. So the art was never
 *  missing, the lookup was asking for a name nothing is filed under, and all three
 *  specials drew an empty cell. Translated here, at the one place the art lookup is
 *  handed a name, rather than in the brain: the brain is never forked.
 *
 *  (BOMB really is the A-10 picture in the cartridge's own cameos.pack, so the Air
 *  Strike gets the right art rather than a stand-in.) */
static const char* sb_art_name(const char* n)
{
    if (!n) return "";
    if (!strcmp(n, "SW_Ion"))       return "ION";
    if (!strcmp(n, "SW_Nuke"))      return "ATOM";
    if (!strcmp(n, "SW_AirStrike")) return "BOMB";
    return n;
}

/* ---- SUPERWEAPON TARGETING ---------------------------------------------------------
 *  A special is not built and placed, it is FIRED at a cell, so it needs a mode of its
 *  own rather than the building-placement one. Clicking a ready special arms it; the
 *  next click on the map fires it; right click, ESC or clicking it again disarms.
 *
 *  This mirrors what the Remaster front end does with
 *  CALLBACK_EVENT_SPECIAL_WEAPON_TARGETTING: the DLL deliberately does NOT hold the
 *  targeting state, because it is the front end's job, which is why all four targeting
 *  sites in dllinterface.cpp are commented out. We are supplying the half that was
 *  always meant to live out here. */
/*  THE ARMED SPECIAL IS LATCHED BY IDENTITY, NOT BY POSITION IN THE STRIP.
 *  The engine holds targeting as a SpecialWeaponType in Map.IsTargettingMode
 *  (sidebar.cpp:2305), and that shape matters out here as well: g_sbState.entry is
 *  cleared and rebuilt from the DLL on every sb_poll(), so an index into it designates
 *  a DIFFERENT entry the moment the buildable list changes between clicking the cameo
 *  and clicking the map, and designates nothing at all if one GetState call fails. Both
 *  cases read to the player as the reported symptom: no targeting cursor, and a click
 *  that launches nothing while the log still claims a launch. The buildable type and id
 *  below are the engine's own identifiers for the special and survive any rebuild. */
static bool g_sbSuperOn = false;
static char g_sbSuperName[16] = {0};
static int  g_sbSuperBType = 0, g_sbSuperBId = 0;

static bool sb_super_armed(void) { return g_sbSuperOn; }

/* SPC_ION_CANNON 1, SPC_NUCLEAR_BOMB 2, SPC_AIR_STRIKE 3 (defines.h:311-314). Returned
   raw so the renderer can pick the cursor without this file knowing about cursors.h. */
static int sb_super_id(void) { return g_sbSuperOn ? g_sbSuperBId : 0; }

/* True when this strip entry is the one currently armed. */
static bool sb_super_is(const SbEntry& e)
{
    return g_sbSuperOn && !strcmp(g_sbSuperName, e.name);
}

static void sb_super_disarm(const char* why)
{
    if (!g_sbSuperOn) return;
    g_sbSuperOn = false;
    printf("SUPER|disarm|%s|%s\n", g_sbSuperName, why ? why : "");
    fflush(stdout);
}

/* Arming, transcribed from sidebar.cpp:2304-2307: enter targeting mode, clear the
   selection, and say "select target". The clear is what makes the targeting cursor
   reachable at all, because the engine reads IsTargettingMode only in the no-selection
   arm of its cursor logic (display.cpp:3279-3327) and so do we. */
static void sb_super_arm(const SbEntry& e)
{
    g_sbSuperOn = true;
    memset(g_sbSuperName, 0, sizeof(g_sbSuperName));
    strncpy(g_sbSuperName, e.name, sizeof(g_sbSuperName) - 1);
    g_sbSuperBType = e.btype;
    g_sbSuperBId = e.bid;
    printf("SUPER|arm|%s|id=%d\n", g_sbSuperName, g_sbSuperBId);
    fflush(stdout);
    if (g_sb.Unselect) g_sb.Unselect();
    if (g_sb.Speak) g_sb.Speak(33);      /* sfx_vox[33] SELECT1, VOX_SELECT_TARGET */
}

/* Fire at ENGINE PIXELS. The caller owns the pixel conversion because it owns the
   camera; this only knows which special is armed. */
static bool sb_super_fire(int px, int py)
{
    if (!g_sbSuperOn) return false;
    if (!g_sb.SuperWeaponReq) {
        printf("SUPER|fire|%s|NO INPUT EXPORTS\n", g_sbSuperName);
        fflush(stdout);
        g_sbSuperOn = false;
        return false;
    }
    g_sb.SuperWeaponReq(SUPERWEAPON_REQUEST_PLACE_SUPER_WEAPON, 0,
                        g_sbSuperBType, g_sbSuperBId, px, py);
    printf("SUPER|fire|%s|id=%d|enginepx=%d,%d\n", g_sbSuperName, g_sbSuperBId, px, py);
    fflush(stdout);
    g_sbSuperOn = false;           /* one shot arms one launch */
    return true;
}

static void sb_request(SidebarRequestEnum r, int idx, short cx, short cy)
{
    if (idx < 0 || idx >= (int)g_sbState.entry.size() || !g_sb.SidebarReq) return;
    const SbEntry& e = g_sbState.entry[idx];
    g_sb.SidebarReq(r, 0, e.btype, e.bid, cx, cy);
}

static void sb_start(int idx) { sb_request(SIDEBAR_REQUEST_START_CONSTRUCTION, idx, 0, 0); }

/* ================================================================================
 *  BUILD QUEUEING, Red Alert 2 style: click a cameo again and another one is queued.
 *
 *  OURS IN FULL. Tiberian Dawn has no queue and cannot be given one cheaply: a house
 *  holds AT MOST ONE FactoryClass per RTTI category (house.h:359-363, one int each), and
 *  HouseClass::Begin_Production refuses outright when that slot is occupied
 *  (house.cpp:2353-2359). sidebar.h:322-324 says it in prose -- the design "precludes
 *  simultaneous construction of the same object type". A brain queue would be new mutable
 *  state pumped inside the DLL tick, and to be worth having it would have to survive a
 *  save, which means touching saveload.cpp. That is the expensive part and it buys one
 *  engine tick.
 *
 *  EA RESERVED THE VERBS AND IMPLEMENTED NONE OF THEM. dllinterface.h:375-378 declares
 *  SIDEBAR_REQUEST_ENABLE_QUEUE, DISABLE_QUEUE, START_CONSTRUCTION_MULTI and
 *  CANCEL_CONSTRUCTION_MULTI; the handler drops three on `default:` and makes the fourth a
 *  bare fallthrough into plain START_CONSTRUCTION (dllinterface.cpp:3909-3912), and the
 *  export has no repeat-count parameter at all. The Remaster's queue lived in its closed
 *  C# UI, not in this brain. So do NOT send START_CONSTRUCTION_MULTI: it is
 *  indistinguishable from START_CONSTRUCTION and buys nothing.
 *
 *  KEYED ON (btype, bid), NEVER ON THE STRIP INDEX. The strip is rebuilt from scratch on
 *  every poll and entries move and vanish -- sidebar.cpp:1035-1055 memmoves an entry out
 *  the moment Who_Can_Build_Me fails, which is what happens when your last barracks dies.
 *  An index-keyed queue would then be pointing at whatever slid into that slot.
 *
 *  THE SECOND CLICK IS FREE TO REPURPOSE. Measured: clicking a cameo that is already
 *  building does nothing today -- no request, no charge, no diagnostic line, because
 *  the arm below is guarded by `else if (!e.constructing)`.
 * ============================================================================== */
static std::map<std::pair<int,int>, int> g_sbQueue;
static bool g_sbQueueOn = true;            /* --noqueue is the A/B */
#define SB_QUEUE_MAX 9                     /* one digit is what fits the cameo */

/* THE PUMP. Push the next queued item the moment the factory frees itself.
 *
 *  WHEN. Measured on an E1 (107 ticks to build): the unit auto-exits and frees the slot by
 *  itself -- sidebar.cpp:1691-1721 issues a PLACE event for infantry, vehicles and
 *  aircraft, which reaches Place_Object -> Exit_Object -> Completed -> Abandon_Production
 *  and puts the slot back to -1. Exactly ONE tick reads ready=1 busy=1, then busy drops to
 *  0. Firing at ready=1 is impossible: the slot is still occupied and Begin_Production
 *  returns PROD_CANT. So the pump fires on the busy edge and costs one engine tick per
 *  queued item -- 0.9% of an E1's build, invisible in play, and written down rather than
 *  hidden.
 *
 *  WHY `busy` IS THE RIGHT SIGNAL AND NOT JUST A CONVENIENT ONE. It is per-CATEGORY and
 *  shared by every entry of that category (dllinterface.cpp:4144-4172), and it is already
 *  `(factory != -1) | (Avail() <= 0)`. So heap exhaustion is handled for free: when the
 *  unit heap fills, busy stays 1 and the pump simply does not fire. The brain is already
 *  telling us exactly when it will accept the next one.
 *
 *  ONE START PER ENGINE FRAME, MAXIMUM, and it gives up loudly rather than retrying:
 *  Begin_Production contains no affordability test at all (house.cpp:2311-2400) and every
 *  start calls On_Speech(VOX_BUILDING) (dllinterface.cpp:4858-4870), so a naive
 *  `while (count && !busy) start` would announce "Building" fifteen times a second.
 */
static void sb_queue_pump(void)
{
    if (!g_sbQueueOn || g_sbQueue.empty() || !g_sbState.valid) return;
    for (std::map<std::pair<int,int>, int>::iterator it = g_sbQueue.begin();
         it != g_sbQueue.end(); ) {
        if (it->second <= 0) { g_sbQueue.erase(it++); continue; }
        int idx = -1;
        for (size_t i = 0; i < g_sbState.entry.size(); i++)
            if (g_sbState.entry[i].btype == it->first.first &&
                g_sbState.entry[i].bid   == it->first.second) { idx = (int)i; break; }
        if (idx < 0) {
            /* The entry left the strip entirely -- the last building that could make it
               died. Drop the queue and SAY SO; a silent drop looks like the queue quietly
               not working. */
            printf("SBQUEUE|drop|btype=%d|bid=%d|n=%d|reason=gone\n",
                   it->first.first, it->first.second, it->second);
            fflush(stdout);
            g_sbQueue.erase(it++);
            continue;
        }
        const SbEntry& e = g_sbState.entry[idx];
        if (!e.busy && !e.constructing && !e.completed && !e.onHold) {
            sb_start(idx);
            it->second--;
            printf("SBQUEUE|pump|%s|btype=%d|bid=%d|left=%d\n",
                   e.name, e.btype, e.bid, it->second);
            fflush(stdout);
            return;                  /* one per engine frame, maximum */
        }
        ++it;
    }
}

static bool sb_queueable(const SbEntry& e)
{
    /* v1 is infantry, vehicles and aircraft -- what was asked for. BUILDINGS are
       excluded on purpose and it is not a dodge: a finished building does not auto-exit
       its factory (sidebar.cpp:1704-1709 has no PLACE event for RTTI_BUILDING), so it
       sits completed and busy until a human places it. A building queue needs a "hold the
       next one until this one is placed" rung and deserves its own decision. */
    return e.type == INFANTRY_TYPE || e.type == UNIT_TYPE || e.type == AIRCRAFT_TYPE;
}

static int sb_queued(const SbEntry& e)
{
    std::map<std::pair<int,int>, int>::iterator it =
        g_sbQueue.find(std::make_pair(e.btype, e.bid));
    return it == g_sbQueue.end() ? 0 : it->second;
}
static void sb_hold(int idx)  { sb_request(SIDEBAR_REQUEST_HOLD_CONSTRUCTION, idx, 0, 0); }
static void sb_cancel(int idx){ sb_request(SIDEBAR_REQUEST_CANCEL_CONSTRUCTION, idx, 0, 0); }

/* True exactly when GAME_STATE_PLACEMENT is answering, which is the engine's own
   definition of "a building is pending placement". */
static bool sb_placing(void) { return g_sbPlacing; }

static void sb_start_placement(int idx)
{
    sb_request(SIDEBAR_REQUEST_START_PLACEMENT, idx, 0, 0);
    g_sbPlaceIdx = idx;
    if (idx >= 0 && idx < (int)g_sbState.entry.size()) {
        g_sbPlaceBType = g_sbState.entry[idx].btype;
        g_sbPlaceBId   = g_sbState.entry[idx].bid;
    }
}

static void sb_cancel_placement(void)
{
    /* Cancel from the REMEMBERED identity, not from the index, so this still reaches the
       engine when the strip no longer holds the entry -- which is exactly the case that
       used to strand the player in placement mode. */
    if (g_sbPlaceBType >= 0 && g_sb.SidebarReq)
        g_sb.SidebarReq(SIDEBAR_CANCEL_PLACE, 0, g_sbPlaceBType, g_sbPlaceBId, 0, 0);
    else if (g_sbPlaceIdx >= 0)
        sb_request(SIDEBAR_CANCEL_PLACE, g_sbPlaceIdx, 0, 0);
    g_sbPlacing = false;
    g_sbPlaceIdx = -1;
    g_sbPlaceBType = g_sbPlaceBId = -1;
}

/* Absolute engine cell -> placement grid index, or -1 if off the grid. */
static int sb_grid_index(int cellx, int celly)
{
    int gx = cellx - g_sb.mapX, gy = celly - g_sb.mapY;
    if (gx < 0 || gy < 0 || gx >= g_sb.mapW || gy >= g_sb.mapH) return -1;
    return gy * g_sb.mapW + gx;
}

/* Would the pending building be legal with its ORIGIN on this absolute cell?
   proximity on the origin (the engine already folded the footprint into that test)
   AND every occupied cell generally clear. */
static bool sb_legal_origin(int cellx, int celly)
{
    if (!g_sbPlacing || g_sbPlaceIdx < 0
        || g_sbPlaceIdx >= (int)g_sbState.entry.size()) return false;
    int oi = sb_grid_index(cellx, celly);
    if (oi < 0 || !g_sbPlaceProx[oi]) return false;
    const SbEntry& e = g_sbState.entry[g_sbPlaceIdx];
    int origin = celly * 128 + cellx;
    for (size_t k = 0; k < e.occupy.size(); k++) {
        int c = origin + e.occupy[k];
        int i = sb_grid_index(c % 128, c / 128);
        if (i < 0 || !g_sbPlaceClear[i]) return false;
    }
    return true;
}

/* Commit. cell_x/cell_y handed to the DLL are GRID indices, not engine cells. */
static bool sb_place_at(int cellx, int celly)
{
    if (!g_sbPlacing || g_sbPlaceIdx < 0) return false;
    int gx = cellx - g_sb.mapX, gy = celly - g_sb.mapY;
    if (gx < 0 || gy < 0 || gx >= g_sb.mapW || gy >= g_sb.mapH) return false;
    printf("SIDEBAR-PLACE|%s|cell=%d,%d|grid=%d,%d|legal=%d\n",
           g_sbState.entry[g_sbPlaceIdx].name, cellx, celly, gx, gy,
           sb_legal_origin(cellx, celly) ? 1 : 0);
    sb_request(SIDEBAR_REQUEST_PLACE, g_sbPlaceIdx, (short)gx, (short)gy);
    return true;
}

/* First legal origin, scanning the grid in reading order. Used by the script's
   "place NAME auto" so a headless run needs no hand-picked cell. */
static bool sb_first_legal(int* cx, int* cy)
{
    if (!g_sbPlacing) return false;
    for (int y = 0; y < g_sb.mapH; y++)
        for (int x = 0; x < g_sb.mapW; x++)
            if (sb_legal_origin(g_sb.mapX + x, g_sb.mapY + y)) {
                *cx = g_sb.mapX + x; *cy = g_sb.mapY + y; return true;
            }
    return false;
}

/* ==================================================================================== *
 *  Layout and drawing
 * ==================================================================================== */

/* Kept as a no-op so the host's radar code compiles unchanged: the DOS sidebar owns its
   own radar hole and nothing needs to reserve space at the top any more. */
static void sb_reserve_top(float) { }

static bool sb_enabled(void) { return g_sbOn; }
static float sb_panel_w(void) { return g_dbW; }
static float sb_panel_x(int fbw) { return (float)fbw - g_dbW; }

/* THE SCALE.
   The sidebar column is 80x200 DOS pixels. It is magnified by a whole number only, so
   every 1995 pixel stays a hard square of S by S screen pixels under GL_NEAREST; a
   fractional scale would put seams and doubled rows through the cameos and the 6-point
   font. S is the largest whole number whose 200*S still fits the window height, and it
   is never allowed to eat more than a third of the window width.
   At 1280x720 that is S = 3: a 240x600 panel with 120 rows left over, which is
   LETTERBOXED (60 above, 60 below) in the DOS screen's own opaque black. The DOS layout
   is anchored at BOTH ends -- the tab plate at row 0, the scroll arrows at rows 188..199
   -- so stretching it or splitting it would put engine-authored pixels where the engine
   never put them. Bands top and bottom keep the whole 200-row column intact. */
static void sb_layout(int fbw, int fbh)
{
    if (g_sb.Layout) g_sb.Layout(fbw, fbh);
    /* The magnification is WHOLE-NUMBER for both HUDs, for the same reason: anything else
       resamples pixel art. The two differ only in how tall one native screen is, 200 rows
       for the DOS bar and 480 for the 640x480 one.

       This branch used to allow HALF steps, so a 720-row window drew the bar at 1.5x to
       fill the height exactly. GL_NEAREST does not save you there: at 1.5x one source
       pixel in every two is doubled and the other is not, so glyph stems come out
       alternately 1 and 2 screen pixels wide. That reads off the screen as the cameo
       captions being "slightly scrambled, not 1:1", and he was right - it was never the
       cameo art, it was this line.

       It also shrank the cursor: g_dbScale took the whole part of 1.5, so the pointer
       drew at 1x over a 1.5x panel.

       The cost of insisting on whole numbers is letterboxing: 720 rows now draw the bar
       at 1x and centre it. Run the window at a multiple of 480 - 960 is 2x - to fill the
       height AND stay pixel-exact. The launchers do that. */
    if (g_hudNew && g_h6Pack) {
        int s = fbh / H6_BAR_H;
        if (s < 1) s = 1;
        while (s > 1 && H6_BAR_W * s * 3 > fbw) s--;
        g_h6Scale = (float)s;
        g_dbScale = s;                                   /* same scale: the cursor matches */
        /* THE LETTERBOX IS SPENT ON BUILD ROWS INSTEAD OF BLACK. The whole-number zoom
           above is kept exactly as it was -- pixel-exactness is not negotiable, and the
           comment over it explains why -- so what is left over is height the bar can
           GROW into rather than height it has to be centred in. At 1692 rows the zoom is
           3, that leaves 564 bar-pixels against an authored 480, and 84 of those buy one
           more row of two cameos. See hud640.h. */
        g_h6Rows = hud640_rows_for(fbh / s);
        g_dbW = (float)H6_BAR_W * g_h6Scale;
        g_dbH = (float)hud640_bar_h(g_h6Rows) * g_h6Scale;
    } else {
        int s = fbh / DB_SCREEN_H;
        if (s < 1) s = 1;
        while (s > 1 && DB_SIDEBARWIDTH * s * 3 > fbw) s--;
        g_dbScale = s;
        g_dbW = (float)(DB_SIDEBARWIDTH * s);
        g_dbH = (float)(DB_SCREEN_H * s);
    }
    g_dbX0 = (float)fbw - g_dbW;
    /* THE DRAWER OFFSET, applied in exactly ONE place so the hit test, the item rects,
       the minimap hotspot, the wheel and the edge-scroll suppression all follow for free
       -- every one of them reads g_dbX0. Gated on the new HUD: the DOS bar shares g_dbX0
       and is not part of this. */
    if (g_hudNew && g_h6Pack)
        g_dbX0 += (float)g_h6SlideX * g_h6Scale;
    g_dbY0 = floorf(((float)fbh - g_dbH) * 0.5f);
    g_dbFbw = fbw;
    g_dbFbh = fbh;
}

static int sb_scale(void) { return g_dbScale; }

/* WHERE THE MAP STOPS. The window's right-hand column belongs to the sidebar, so the
   tactical view ends at the bar's left edge rather than at the window edge. Returns the
   window width when no bar is drawn, and follows the 640x480 bar's drawer out to the
   window edge when that bar is fully retracted, because g_dbX0 already carries the slide
   offset.

   It exists because the RIGHT-hand edge-scroll strip has to be measured against it. A
   strip measured against the window width lands entirely underneath the bar, where the
   panel hit test refuses the scroll, so right edge scrolling was unreachable at every
   window size and in both HUDs. Nothing else in the layout needs the distinction: the
   bar is only ever on the right, so the map's left, top and bottom edges are the
   window's own. */
static float sb_tactical_right(int fbw, int fbh)
{
    if (!g_sbOn) return (float)fbw;
    sb_layout(fbw, fbh);
    return g_dbX0 < (float)fbw ? g_dbX0 : (float)fbw;
}

/* Screen pixel -> DOS pixel, and back. These two are the ONLY place the magnification
   is applied, so what is drawn and what is clicked cannot drift apart. */
static void sb_screen_to_dos(float col, float row, int* dx, int* dy)
{
    *dx = DB_SIDE_X + (int)floorf((col - g_dbX0) / (float)g_dbScale);
    *dy = (int)floorf((row - g_dbY0) / (float)g_dbScale);
}

static void sb_dos_to_screen(int dx, int dy, float* col, float* row)
{
    *col = g_dbX0 + (float)((dx - DB_SIDE_X) * g_dbScale);
    *row = g_dbY0 + (float)(dy * g_dbScale);
}

/* Screen pixel -> the 640x480 HUD's own pixels. The counterpart of sb_screen_to_dos and,
   like it, the ONLY place the magnification is undone, so what is drawn and what is
   clicked cannot drift apart. Coordinates are SIDEBAR-LOCAL, which is the space
   hud640_layout.h is written in. */
static void sb_screen_to_h6(float col, float row, int* hx, int* hy)
{
    *hx = (int)floorf((col - g_dbX0) / g_h6Scale);
    *hy = (int)floorf((row - g_dbY0) / g_h6Scale);
}

/* THE THREE PLATES THAT ARE PINNED TO THE WINDOW, NOT TO THE BAR.
   sb_over_panel asks "is this in the right-hand column", which is the bar. The OPTIONS
   plate sits at the far LEFT of the window and was in no test at all: the click fell
   through to the map, and update_cursor never took its panel branch, so the 1995 pointer
   was suppressed and the cartridge's 3-D cursor drew in the WORLD -- underneath the
   sidebar overlay. That is literally "the mouse cursor goes behind it".

   Deliberately expressed against fbw rather than g_dbX0: the two right-hand plates stay
   pinned to the window edge while the bar slides out from under them. */
static SbHit sb_chrome_hit(float col, float row, int fbw, int fbh)
{
    if (!g_sbOn || !g_hudNew || !g_h6Pack) return SBH_NONE;
    sb_layout(fbw, fbh);
    {
        const float th = (float)H6_TAB_H * g_h6Scale;
        if (row < g_dbY0 || row >= g_dbY0 + th) return SBH_NONE;
        if (col >= 0.0f && col < g_dbW)                            return SBH_OPTIONS;
        if (col >= (float)fbw - 2.0f * g_dbW && col < (float)fbw - g_dbW) return SBH_CREDITS;
        if (col >= (float)fbw - g_dbW && col < (float)fbw)          return SBH_SIDEBAR;
    }
    return SBH_NONE;
}

static bool sb_over_panel(float col, float row, int fbw, int fbh)
{
    if (!g_sbOn) return false;
    sb_layout(fbw, fbh);
    /* The whole right-hand column, letterbox bands included: those bands are the panel's
       own bezel, not tactical view, and a click or an edge-scroll there is not the map's. */
    return (col >= g_dbX0 && col < (float)fbw && row >= 0 && row < (float)fbh)
           || sb_chrome_hit(col, row, fbw, fbh) != SBH_NONE;
}

/* ---- where things are, in DOS pixels ----------------------------------------------- *
 * sidebar.cpp:1260 Init_IO:  SelectButton[id][i].X = strip X, .Y = COLUMN_Y + 24*i,
 * 32 wide by 24 tall. The DRAWN cameo is one pixel right and one pixel up of that
 * (sidebar.cpp:1798 y--, :1794 x + LeftEdgeOffset); the button is what the engine
 * hit-tests, so the button is what we hit-test.
 * ------------------------------------------------------------------------------------ */

static int sb_strip_x(int column) { return column ? DB_COLUMN_TWO_X : DB_COLUMN_ONE_X; }

/* How many entries this column has, and where its first entry sits in g_sbState.entry.
   GAME_STATE_SIDEBAR stores the columns back to back, left column first. */
static int sb_col_base(int column)
{
    if (column == 0) return 0;
    int n = 0;
    for (size_t i = 0; i < g_sbState.entry.size(); i++)
        if (g_sbState.entry[i].column == 0) n++;
    return n;
}

static int sb_col_count(int column)
{
    int n = 0;
    for (size_t i = 0; i < g_sbState.entry.size(); i++)
        if (g_sbState.entry[i].column == column) n++;
    return n;
}

/* The DOS strip shows four rows, the 640x480 one shows five. Scroll clamping and the
   on-screen test both have to ask, or the last row of the new HUD scrolls off by one. */
static int sb_visible_rows(void)
{
    return (g_hudNew && g_h6Pack) ? g_h6Rows : DB_MAX_VISIBLE;
}

static void sb_clamp_top(void)
{
    for (int c = 0; c < DB_COLUMNS; c++) {
        int maxtop = sb_col_count(c) - sb_visible_rows();
        if (maxtop < 0) maxtop = 0;
        if (g_sbTop[c] > maxtop) g_sbTop[c] = maxtop;
        if (g_sbTop[c] < 0) g_sbTop[c] = 0;
    }
}

/* item index -> screen rect; false when it is scrolled out of the four visible slots */
static bool sb_item_rect(int idx, float* x0, float* y0, float* x1, float* y1)
{
    if (idx < 0 || idx >= (int)g_sbState.entry.size()) return false;
    const SbEntry& e = g_sbState.entry[idx];
    sb_clamp_top();
    int rowInCol = idx - sb_col_base(e.column) - g_sbTop[e.column];
    if (rowInCol < 0 || rowInCol >= sb_visible_rows()) return false;
    if (g_hudNew && g_h6Pack) {
        /* the same rectangles sb_hit_h6 tests, so what sbitem reports and what a click
           lands on are one description, not two */
        *x0 = g_dbX0 + (float)H6_COL_X[e.column] * g_h6Scale;
        *y0 = g_dbY0 + (float)hud640_row_y(rowInCol) * g_h6Scale;
        *x1 = *x0 + (float)H6_CELL_W * g_h6Scale;
        *y1 = *y0 + (float)H6_CELL_H * g_h6Scale;
        return true;
    }
    sb_dos_to_screen(sb_strip_x(e.column), DB_COLUMN_Y + rowInCol * DB_OBJECT_HEIGHT, x0, y0);
    *x1 = *x0 + (float)(DB_OBJECT_WIDTH * g_dbScale);
    *y1 = *y0 + (float)(DB_OBJECT_HEIGHT * g_dbScale);
    return true;
}

/* ---- engine state -> DB_State ------------------------------------------------------ *
 * Straight transcription, no interpretation. The one judgement call is `darken`, and
 * sidebar.cpp:1855-1868 makes it for us: an item WITH a factory never darkens, an item
 * without one darkens exactly when a factory of its own RTTI is busy, which is what the
 * DLL reports as Busy.
 * ------------------------------------------------------------------------------------ */
static void sb_fill_dos_state(void)
{
    db_state_clear(&g_dbState);
    g_dbState.nod = g_sb.nod ? 1 : 0;

    for (size_t i = 0; i < g_sbState.entry.size(); i++) {
        const SbEntry& e = g_sbState.entry[i];
        const int col = (e.column && DB_COLUMNS > 1) ? 1 : 0;
        const int slot = db_state_add(&g_dbState, col, sb_art_name(e.name));
        if (slot < 0) continue;
        DB_Slot* s = &g_dbState.items[col][slot];
        /* Buildables[].Factory != -1 is the union of the three states the DLL splits it
           into; SPECIALs (the superweapons) always report as producing. */
        const bool factory = e.constructing || e.onHold || e.completed || (e.type == SPECIAL);
        s->producing = factory ? 1 : 0;
        s->ready = e.completed ? 1 : 0;
        s->onhold = e.onHold ? 1 : 0;
        /* Progress is Completion()/STEP_COUNT, a float in 0..1 (dllinterface.cpp:4205),
           so the wedge stage is Progress * 108 bounded to the 0..107 the engine uses;
           dosbar.c then draws CLOCK.SHP frame stage+1 (sidebar.cpp:1977). */
        int stage = (int)(e.progress * (float)DB_STEP_COUNT + 0.5f);
        if (stage < 0) stage = 0;
        if (stage > DB_STEP_COUNT - 1) stage = DB_STEP_COUNT - 1;
        s->completion = stage;
        s->darken = (!factory && e.busy) ? 1 : 0;
        if (getenv("CNC3D_CLOCKDBG"))
            fprintf(stderr, "CLOCKDBG|%s|prog=%.3f|producing=%d|ready=%d|stage=%d|darken=%d\n",
                    e.name, e.progress, s->producing, s->ready, stage, s->darken);
    }

    sb_clamp_top();
    g_dbState.top[0] = g_sbTop[0];
    g_dbState.top[1] = g_sbTop[1];

    g_dbState.repair_on = g_sbRepairOn ? 1 : 0;
    g_dbState.sell_on = g_sbSellOn ? 1 : 0;
    g_dbState.repair_disabled = g_sbState.repairEnabled ? 0 : 1;
    g_dbState.sell_disabled = g_sbState.sellEnabled ? 0 : 1;
    g_dbState.map_disabled = (g_sbState.radarActive || g_sbRadarForce) ? 0 : 1;
    g_dbState.radar_active = ((g_sbState.radarActive || g_sbRadarForce) && g_sbRadarShown) ? 1 : 0;

    /* power.cpp:425 Power_Height turns watts into bar pixels: six equal fractions of the
       remaining bar per 100 units, so the gauge is logarithmic and never runs off the
       top. Reproduced here because the DLL hands us the raw watts. */
    {
        const int lim = DB_POW_HEIGHT - 2;
        int v[2] = { g_sbState.powerProduced, g_sbState.powerDrained };
        for (int k = 0; k < 2; k++) {
            int value = v[k] < 0 ? 0 : v[k];
            int num = value / 100;                      /* POWER_STEP_LEVEL  power.h:101 */
            int ret = 0;
            for (int lp = 0; lp < num; lp++) {
                ret = ret + ((lim - ret) / 6);          /* POWER_STEP_FACTOR power.h:102 */
                value -= 100;
            }
            if (value) ret = ret + ((((lim - ret) / 6) * value) / 100);
            if (ret < 0) ret = 0;
            if (ret > lim) ret = lim;
            v[k] = ret;
        }
        g_dbState.power_total = v[0];
        g_dbState.power_drain = v[1];
        /* The RAW watts travel alongside the pixels, because the colour rule is a watt
           test and the geometry is a pixel one. See DB_State's own note. */
        g_dbState.power_watts = g_sbState.powerProduced;
        g_dbState.drain_watts = g_sbState.powerDrained;
    }
}

/* ---- pointer state, for the art HUD's hover and pressed frames --------------------
 *
 * The virtual mouse. It lives here rather than in cnc_eyes.cpp because the art HUD has
 * to know where the pointer is in order to pick a frame, and it draws from this header.
 * Every writer of it - the SDL motion handler, push_motion, the script's mouse verbs -
 * sits below the include, so a scripted run drives hover exactly as a hand does.
 */
static float g_mouseScrC = -1.0f, g_mouseScrR = -1.0f;


/* What the pointer is over, and what a held button started on. A control only draws
   pressed while the press that began on it is still over it, which is the behaviour
   every real button has: drag off and it pops back up. */

/* OPTIONS cannot call the pause dialog directly: dosopt_gl.h is included hundreds of
   lines after this header. So the click raises a request and the two places that call
   sb_click drain it. */
static bool g_sbOptionsReq = false;
static bool sb_take_options_request(void)
{
    const bool v = g_sbOptionsReq;
    g_sbOptionsReq = false;
    return v;
}

static SbHit g_sbHover = SBH_NONE;
static int   g_sbHoverCol = -1;
/* The SLOT under the pointer, which sb_pointer_refresh has always computed and thrown
   away. The tooltip is the first thing that needs it. -1 when the pointer is off the
   panel or over no control. */
static int   g_sbHoverSlot = -1;
static SbHit g_sbPress = SBH_NONE;
static int   g_sbPressCol = -1;

/* Defined after the hit tests, called at the top of the panel draw. */
static void sb_pointer_refresh(int fbw, int fbh);
static int  sb_frame_for(SbHit which, int column, int engaged);

/* Frame indices in the baked strips. states.py owns the art for these; the order is a
   contract between that script and this file. */
#define H6_FR_NORMAL  0
#define H6_FR_HOVER   1
#define H6_FR_PRESSED 2
#define H6_FR_ACTIVE  3

/* The whole panel: rasterise on the CPU, one conversion, one upload, two quads.
   The host has already called begin_overlay(), so we are in y-down pixel space with the
   depth test off. */
static void sb_draw_panel_640(int fbw, int fbh);

static void sb_draw_panel(int fbw, int fbh)
{
    if (!g_sbOn) return;
    if (g_hudNew && g_h6Pack) { sb_draw_panel_640(fbw, fbh); return; }
    if (!g_dbPack) return;
    sb_layout(fbw, fbh);
    /* THE DOS PATH REFRESHES THE HOVER TOO. Only sb_draw_panel_640 did, so on the DOS bar
       -- which is what bare cnc_eyes draws, and what every gate that does not set
       CNC3D_HUD=new sees -- g_sbHover was whatever the last 640 frame or the last script
       verb left it. Nothing depended on it before the tooltip, which is why it was never
       noticed. */
    sb_pointer_refresh(fbw, fbh);

    /* 1. rasterise --------------------------------------------------------------- */
    db_surface_init(&g_dbSurf, DB_SCREEN_W, DB_SCREEN_H, g_dbScreen);
    memset(g_dbScreen, DB_BLACK, sizeof(g_dbScreen));
    sb_fill_dos_state();
    db_draw_sidebar(&g_dbSurf, g_dbPack, &g_dbState);
    if (g_sb.RadarPlot) g_sb.RadarPlot(&g_dbSurf, g_dbPack, g_dbState.radar_active);
    /* CreditsCounter, not Credits: the engine's own animated display value, which is
       CreditClass tracking Available_Money() = credits PLUS harvested tiberium in
       storage, with the 1995 count-up/count-down motion. Plain Credits excludes
       tiberium, so a harvester unloading never moved the number on screen. */
    db_draw_credits_tab(&g_dbSurf, g_dbPack,
                        g_sbState.valid ? g_sbState.creditsCounter : 0);

    /* 2. one conversion ---------------------------------------------------------- */
    for (int y = 0; y < SB_TEX_H; y++)
        memcpy(g_dbCrop + (size_t)y * SB_TEX_W,
               g_dbScreen + (size_t)y * DB_SCREEN_W + SB_TEX_X, SB_TEX_W);
    {
        DB_Surface crop;
        crop.w = SB_TEX_W; crop.h = SB_TEX_H; crop.px = g_dbCrop;
        db_clip_reset(&crop);
        db_surface_to_rgba(&crop, g_dbPack->pal8, g_dbRGBA, 0);
    }

    /* 3. one upload -------------------------------------------------------------- */
    if (!g_dbTex) {
        std::vector<unsigned char> zero((size_t)SB_TEX_POT * SB_TEX_POT * 4, 0);
        glGenTextures(1, &g_dbTex);
        glBindTexture(GL_TEXTURE_2D, g_dbTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        /* NOT registered: UI art is never bilinear. See fx_filter.h. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SB_TEX_POT, SB_TEX_POT, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &zero[0]);
    }
    glBindTexture(GL_TEXTURE_2D, g_dbTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SB_TEX_W, SB_TEX_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_dbRGBA);

    /* 4. the letterbox, in the DOS screen's own opaque black ---------------------- */
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor3ub(g_dbPack->pal8[DB_BLACK * 3 + 0], g_dbPack->pal8[DB_BLACK * 3 + 1],
               g_dbPack->pal8[DB_BLACK * 3 + 2]);
    glBegin(GL_QUADS);
    sb_quad(g_dbX0, 0.0f, (float)fbw, (float)fbh);
    glEnd();

    /* 5. two quads --------------------------------------------------------------- */
    const float u = 1.0f / (float)SB_TEX_POT, v = 1.0f / (float)SB_TEX_POT;
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    {   /* the sidebar column: DOS x 240..319, the full 200 rows */
        const float su0 = (float)(DB_SIDE_X - SB_TEX_X) * u, su1 = (float)SB_TEX_W * u;
        const float sv0 = 0.0f, sv1 = (float)SB_TEX_H * v;
        const float x0 = g_dbX0, y0 = g_dbY0, x1 = g_dbX0 + g_dbW, y1 = g_dbY0 + g_dbH;
        glTexCoord2f(su0, sv0); glVertex2f(x0, y0);
        glTexCoord2f(su1, sv0); glVertex2f(x1, y0);
        glTexCoord2f(su1, sv1); glVertex2f(x1, y1);
        glTexCoord2f(su0, sv1); glVertex2f(x0, y1);
    }
    {   /* the credits plate: DOS x 160..239, rows 0..7. In DOS it butts up against the
           left edge of the sidebar tab, and that is where it goes here too. */
        const float cu0 = 0.0f, cu1 = (float)(DB_SIDE_X - SB_TEX_X) * u;
        const float cv0 = 0.0f, cv1 = (float)DB_TAB_HEIGHT * v;
        const float x1 = g_dbX0, x0 = g_dbX0 - (float)(DB_EVA_WIDTH * g_dbScale);
        const float y0 = g_dbY0, y1 = g_dbY0 + (float)(DB_TAB_HEIGHT * g_dbScale);
        glTexCoord2f(cu0, cv0); glVertex2f(x0, y0);
        glTexCoord2f(cu1, cv0); glVertex2f(x1, y0);
        glTexCoord2f(cu1, cv1); glVertex2f(x1, y1);
        glTexCoord2f(cu0, cv1); glVertex2f(x0, y1);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/* ---- the 640x480 HUD draw path -----------------------------------------------------
   Three quads instead of two: the sidebar column, the credits tab immediately left of
   it, and the OPTIONS tab at the far left of the strip, which is where the DOS tab bar
   puts it. Each is its own small texture, so nothing has to be packed into one atlas and
   the Voodoo still only sees NEAREST-filtered POT textures.

   SCALE: the HUD is authored at 640x480, so at the default 1280x720 window an integer
   step of 1 makes the bar 160 wide, NARROWER on screen than the 240 the DOS bar gets at
   3x. That is a consequence of the art being native at the Tier 1 size, not a bug, but
   it is a real look change and is flagged as such. */
static void h6_upload(GLuint* tex, const unsigned char* rgba, int w, int h)
{
    if (!*tex) {
        std::vector<unsigned char> zero((size_t)H6_POT_W * H6_POT_H * 4, 0);
        glGenTextures(1, tex);
        glBindTexture(GL_TEXTURE_2D, *tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        /* NOT registered: UI art is never bilinear. See fx_filter.h. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, H6_POT_W, H6_POT_H, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &zero[0]);
    }
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static void h6_tex_quad(GLuint tex, int tw, int th, float x0, float y0, float x1, float y1)
{
    const float u = (float)tw / (float)H6_POT_W, v = (float)th / (float)H6_POT_H;
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
    glTexCoord2f(u,    0.0f); glVertex2f(x1, y0);
    glTexCoord2f(u,    v);    glVertex2f(x1, y1);
    glTexCoord2f(0.0f, v);    glVertex2f(x0, y1);
    glEnd();
}

/* Fill H6_State from the same engine snapshot the DOS path uses, so the two never
   disagree about what is on. */
static void h6_fill_state(H6_State* st)
{
    memset(st, 0, sizeof *st);
    /* How tall the bar is this frame. sb_layout has already decided it from the screen
       height; the module is told rather than left to work it out, so the composite, the
       hit test and the upload are all reading one number. */
    st->rows = g_h6Rows;
    /* The meter shows OUTPUT against the WHOLE BAR, on the scale the DOS bar already
       computed into g_dbState (power.cpp:425 Power_Height, six equal fractions of what
       is left per 100 watts), so the two sidebars agree on what "full" means instead of
       each inventing a scale.
       IT USED TO NORMALISE AGAINST ITSELF -- span = max(total, drain), level =
       total/span -- which meant that the moment output equalled or beat drain the meter
       read 100%, whatever the numbers were. A base with two power plants and almost no
       draw showed a completely full bar, which is the "far too much power in the meter"
       players reported. That was the ORIGINAL defect on the board ("power_level is 100
       whenever output >= drain, so the meter is pinned full") and v0.5.9 only added the
       overdraw colour beside it without ever fixing the fill. Against the full bar,
       two power plants read about a third, which is what the DOS gauge shows. */
    {
        const int lim = DB_POW_HEIGHT - 2;          /* the same ceiling Power_Height uses */
        int total = g_dbState.power_total;
        st->power_level = lim > 0 ? (total * 100) / lim : 0;
        if (st->power_level < 0)   st->power_level = 0;
        if (st->power_level > 100) st->power_level = 100;
        /* The overdraw warning, on the SAME watt rule the DOS bar uses (power.cpp:
           475-482), so the two sidebars cannot disagree about whether the base is in
           trouble. Until v0.5.9 the art HUD had no warning at all: one green meter
           whatever the drain was doing.
           STILL OWED, and registered rather than bodged: a distinct DRAIN THRESHOLD
           tick. The delivered art carries one asset for this channel, meter_lit, and
           nothing to mark a level with, so inventing one would be authoring art in the
           renderer. The colour is the honest half that needs no new asset. */
        const int pw = g_dbState.power_watts, dw = g_dbState.drain_watts;
        st->power_color = (dw > pw * 2) ? 2 : (dw > pw ? 1 : 0);
    }
    st->credits      = g_sbState.valid ? g_sbState.creditsCounter : 0;
    st->radar_active = g_dbState.radar_active;
    /* The pixels, without which radar_active means nothing: hud640_draw_bar tests BOTH
       and falls back to the emblem if either is missing. */
    if (st->radar_active && g_sb.RadarPlotRGBA && g_dbPack) {
        g_sb.RadarPlotRGBA(g_h6Radar, H6_RADAR_W, H6_RADAR_H, g_dbPack, 1);
        st->radar_rgba = g_h6Radar;
    }
    st->nod          = g_dbState.nod;
    /* Repair and Sell are IsToggleType, so they have an engaged look on top of the two
       pointer states; DOS did the same job by turning the caption red
       (textbtn.cpp:336). Map is momentary, so it never reaches H6_FR_ACTIVE. */
    st->repair_frame = sb_frame_for(SBH_REPAIR, -1, g_sbRepairOn ? 1 : 0);
    st->sell_frame   = sb_frame_for(SBH_SELL,   -1, g_sbSellOn   ? 1 : 0);
    st->map_frame    = sb_frame_for(SBH_MAP,    -1, 0);
    st->options_frame = sb_frame_for(SBH_OPTIONS, -1, 0);
    st->credits_frame = sb_frame_for(SBH_CREDITS, -1, 0);
    /* The handle reads "engaged" while the drawer is closed, which is the one piece of
       state a player cannot otherwise see once the bar is off screen. */
    st->sidebar_frame = sb_frame_for(SBH_SIDEBAR, -1, g_h6SlideTarget ? 1 : 0);
    /* Four arrows, two per column, laid out up/down/up/down. The arrow strips carry no
       engaged frame - scrolling is momentary. */
    {
        int a;
        for (a = 0; a < 4; a++)
            st->arrow_frame[a] = sb_frame_for((a & 1) ? SBH_DOWN : SBH_UP, a / 2, 0);
    }

    /* The build slots. The engine's own list, scrolled by TopIndex exactly as the DOS
       strip does, so both HUDs show the same items in the same order.

       THE CELL IS COMPOSED IN 8-BIT FIRST, on a 32x24 surface, with dosbar.c's own
       routines: db_draw_shape for the cameo, db_draw_shape_ghost for the build clock and
       the "factory already busy" darken, db_draw_shape_centered for the READY and HOLDING
       pips. That matters. CLOCK.SHP is a solid GREEN wedge and the wedge is not painted,
       it is a LOOKUP on the destination pixel through the pack's own translucency table
       (sidebar.cpp:1307, TLucentType ClockCols = {GREEN, LTGREY, 180, 0}). Rebuilding
       that as an alpha blend in RGBA would be a guess at the table. Composing in the
       engine's own colour space and converting once at the end is exact.

       TWO CAMEO SOURCES, ONE COMPOSITOR. If cameos95.pack is loaded and carries this
       buildable, the cell is composed at the full 61x45 opening from the C&C95 art and
       written out 1:1. Otherwise it falls back to the DOS 32x24 cameo, composed at 32x24
       and doubled on the way out. Doubling is whole-number, so no pixel is resampled.
       The fallback is per-item, not per-run: eight buildables have no C&C95 cameo (see
       tools/sidebar_redesign/cameos95.py) and they keep their DOS art rather than show
       something that is not them.

       The 2x pack carries its own CLOCK and PIPS at 2x so the overlays match the bigger
       cell. Its CLOCK is 64x48 against a 61x45 cell, so it is drawn at (-1,-1) to centre
       it; db_draw_shape_ghost clips negative coordinates, so that costs nothing. */
    for (int c = 0; c < H6_COLUMNS && c < DB_COLUMNS; c++) {
        for (int r = 0; r < g_h6Rows; r++) {
            const int slot = c + r * H6_COLUMNS;
            const int idx  = g_dbState.top[c] + r;
            const DB_Slot* it;
            const DB_Shape *cameo, *clock, *pips;
            const DB_Pack* cp;
            static unsigned char cellpx[H6_CAMEO_W * H6_CAMEO_H];
            static unsigned char refpx[H6_CAMEO_W * H6_CAMEO_H];
            const unsigned char* tdr;
            DB_Surface cell;
            unsigned char* dst;
            int cw, chh, ox, oy, y, x;

            if (idx >= g_dbState.count[c]) continue;
            it = &g_dbState.items[c][idx];
            if (!it->cameo[0]) continue;

            cp = NULL;
            cameo = NULL;
            if (g_h6CamPack) {
                cameo = db_shape(g_h6CamPack, it->cameo);
                if (cameo) cp = g_h6CamPack;
            }
            if (!cameo) {
                cameo = db_shape(g_dbPack, it->cameo);
                cp = g_dbPack;
            }
            if (!cameo || !cp) continue;
            /* The supplied art is keyed by the buildable's own ident, so it is independent of
               which DOS/C&C95 pack supplied the shape and the overlays. NULL simply means
               he has not drawn this one and the old path stands. */
            tdr = h6_tdr_find(it->cameo);
            clock = db_shape(cp, "CLOCK");
            pips  = db_shape(cp, "PIPS");

            cw  = cameo->w;
            chh = cameo->h;
            if (cw > H6_CAMEO_W)  cw  = H6_CAMEO_W;
            if (chh > H6_CAMEO_H) chh = H6_CAMEO_H;
            /* The overlays are cut for the cell, not for the cameo, so centre them on it. */
            ox = clock ? -((clock->w - cw) / 2) : 0;
            oy = clock ? -((clock->h - chh) / 2) : 0;

            db_surface_init(&cell, cw, chh, cellpx);
            memset(cellpx, 0, (size_t)cw * chh);
            db_draw_shape(&cell, cameo, 0, 0, 0);
            /* The cameo ALONE, kept so the expansion below can tell an untouched cameo
               pixel from one an overlay changed. Only needed on the true-colour path,
               and costs one 64x48 memcpy. */
            memcpy(refpx, cellpx, (size_t)cw * chh);

            if (it->darken && clock)
                db_draw_shape_ghost(&cell, clock, 0, ox, oy, cp->clocktab);

            if (it->producing) {
                if (it->ready) {
                    if (pips)
                        db_draw_shape_centered(&cell, pips, DB_PIP_READY,
                                               cw >> 1, chh - pips->h - 8);
                } else {
                    int stage = it->completion;
                    if (stage < 0) stage = 0;
                    if (stage > DB_STEP_COUNT - 1) stage = DB_STEP_COUNT - 1;
                    if (clock)
                        db_draw_shape_ghost(&cell, clock, stage + 1, ox, oy, cp->clocktab);
                    if (it->onhold && pips)
                        db_draw_shape_centered(&cell, pips, DB_PIP_HOLDING,
                                               cw >> 1, chh - pips->h - 8);
                }
            }

            /* 1:1 when the cell art already fills the opening, 2x when it is the DOS
               cameo. `zoom` is the only difference between the two paths. */
            {
            const int zoom = (cw * 2 <= H6_CAMEO_W) ? 2 : 1;
            /* THE TRUE-COLOUR PATH. If the supplied art has this buildable, the cameo pixels
               come from it and only the OVERLAYS come from the palette. Which is which is
               decided per pixel against the cameo-only reference:

                 cellpx == refpx                -> nothing touched it, use his colour
                 cellpx == ghost[refpx]         -> the clock wedge faded it, so fade HIS
                                                   colour by the same rule, in RGB
                 anything else                  -> a pip or the darken drew real art here,
                                                   which is the DOS pack's and stays its
                                                   own palette colour

               The middle case is the whole point: fading the 8-bit cameo and then looking
               it up would hand back the OLD palette's colours inside the clock wedge, and
               the cell would change colour as a build progressed. */
            const unsigned char* ghost = cp->clocktab ? cp->clocktab + 256 : NULL;
            dst = g_h6Cameo[slot];
            memset(dst, 0, H6_CAMEO_W * H6_CAMEO_H * 4);
            for (y = 0; y < H6_CAMEO_H; y++) {
                const int sy = y / zoom;
                if (sy >= chh) break;
                for (x = 0; x < H6_CAMEO_W; x++) {
                    const int sx = x / zoom;
                    unsigned char pi, ri;
                    unsigned char* d;
                    if (sx >= cw) break;
                    pi = cellpx[sy * cw + sx];
                    if (!pi) continue;
                    ri = refpx[sy * cw + sx];
                    d = dst + ((size_t)y * H6_CAMEO_W + x) * 4;
                    if (tdr && ri) {
                        const unsigned char* sp =
                            tdr + ((size_t)y * H6_CAMEO_W + x) * 4;
                        if (pi == ri || (ghost && pi == ghost[ri])) {
                            d[0] = sp[0]; d[1] = sp[1]; d[2] = sp[2]; d[3] = 255;
                            if (pi != ri)
                                h6_clock_fade(d);
                            continue;
                        }
                    }
                    d[0] = cp->pal8[pi * 3 + 0];
                    d[1] = cp->pal8[pi * 3 + 1];
                    d[2] = cp->pal8[pi * 3 + 2];
                    d[3] = 255;
                }
            }
            }
            st->slot[slot].used = 1;
            st->slot[slot].progress = it->producing ? it->completion : -1;
            st->slot[slot].rgba = dst;
        }
    }
}

/* One step of the drawer, per DRAWN FRAME. Deliberately not in sb_layout: that is called
   from sb_over_panel, sb_hit_at, minimap_layout, sb_click and several script verbs, so
   stepping there would advance the animation a variable number of times per frame and make
   the slide depend on how often the mouse was probed. */
static void sb_slide_step(void)
{
    if (g_h6SlideX == g_h6SlideTarget) return;
    if (g_h6SlideX < g_h6SlideTarget) {
        g_h6SlideX += H6_SLIDE_STEP;
        if (g_h6SlideX > g_h6SlideTarget) g_h6SlideX = g_h6SlideTarget;
    } else {
        g_h6SlideX -= H6_SLIDE_STEP;
        if (g_h6SlideX < g_h6SlideTarget) g_h6SlideX = g_h6SlideTarget;
    }
}

/* ================================================================================
 *  THE TOOLTIP.  Hover a control, read what it is and what it costs.
 *
 *  NOT A DEVIATION. 1995 shipped exactly this and the code is still in the tree:
 *  sidebar.cpp:2343 calls Map.Help_Text(choice->Full_Name(), X, Y, CC_GREEN, true,
 *  choice->Cost_Of()*CostBias), and help.cpp:259-285 draws it as two lines -- the name,
 *  then "$%d" -- on black, with a one-pixel coloured rect around each line, right-aligned
 *  flush to the LEFT of the cameo (help.cpp:315-318, DrawX = X - Width). It is dead here
 *  only because this renderer draws the sidebar instead of the brain.
 *
 *  WHERE THE TWO STRINGS COME FROM.
 *    the PRICE is the brain's, already exported (CNCSidebarEntryStruct::Cost) and already
 *      mirrored into SbEntry::cost. Nothing new.
 *    the NAME cannot be. The export carries the INI ident -- "E1", "NUKE" -- and the
 *      readable name is behind ObjectTypeClass::Full_Name(), which returns a TEXT ID that
 *      only Text_String() can resolve, against a table the DLL front end never
 *      initialises (init.cpp:289-291; this project already recorded it at
 *      house.cpp:4018-4025). An additive export would return NULL for all 113 of them. So
 *      the join is baked: game/bake_dosnames.py reads the game's own CONQUER.ENG out of
 *      LOCAL.MIX and pairs it to the idents through the TXT_ ids on the type
 *      constructors. Every string is the game's, not typed from memory.
 *
 *  OURS, and said out loud: the four CHROME tooltips (Repair / Sell or Demolish / Map /
 *  Options). 1995 has the strings and even the calls, but they sit behind #ifdef NEVER at
 *  sidebar.cpp:902-923, so showing them is our decision.
 *
 *  NO DWELL TIMER, which is also 1995's behaviour: its Help_Text call passes quick=true
 *  (help.cpp:229-233, CountDownTimer.Set(1)). If one is ever wanted it must count DRAWN
 *  FRAMES and never milliseconds, the rule ORDER_MARK_TICKS already follows, because a
 *  wallclock in the draw would break two --shot runs being byte-identical.
 * ============================================================================== */
#include "dosnames.h"

static bool g_sbTips = true;              /* --notooltips is the A/B */
#define SBTIP_W 192                        /* DOS px: wider than "Construction Yard" */
#define SBTIP_H 24
static unsigned char g_tipPx[SBTIP_W * SBTIP_H];
static unsigned char g_tipRGBA[SBTIP_W * SBTIP_H * 4];
static GLuint g_tipTex = 0;
/* What the last draw resolved, so a script can assert it without reading pixels. */
static char g_tipName[32] = {0};
static int  g_tipCost = -1;

static const char* sb_title_for(const char* asset)
{
    if (!asset || !*asset) return 0;
    for (int i = 0; i < DOSNAMES_COUNT; i++)
        if (!strcmp(DOSNAMES[i].asset, asset)) return DOSNAMES[i].title;
    return 0;
}

/* -> the asset key for whatever the pointer is over, and its cost through *cost.
   cost < 0 means "no price line". */
static const char* sb_hover_subject(int* cost)
{
    *cost = -1;
    if (g_sbHover == SBH_REPAIR)  return g_sb.nod ? "SB_DEMOLISH" : "SB_REPAIR";
    if (g_sbHover == SBH_SELL)    return g_sb.nod ? "SB_DEMOLISH" : "SB_SELL";
    if (g_sbHover == SBH_MAP)     return "SB_MAP";
    if (g_sbHover == SBH_OPTIONS) return "SB_OPTIONS";
    if (g_sbHover != SBH_ITEM || g_sbHoverCol < 0 || g_sbHoverSlot < 0) return 0;
    /* THE CLICK PATH'S OWN EXPRESSION, copied rather than re-derived. g_sbTop is the
       term a second copy gets wrong, and the bug only shows after the player scrolls. */
    const int idx = sb_col_base(g_sbHoverCol) + g_sbTop[g_sbHoverCol] + g_sbHoverSlot;
    if (idx < 0 || idx >= (int)g_sbState.entry.size()) return 0;
    if (g_sbState.entry[idx].column != g_sbHoverCol) return 0;
    const SbEntry& e = g_sbState.entry[idx];
    /* Cost 0 is not a free unit, it is a SPECIAL: Fetch_Techno_Type yields NULL for
       RTTI_SPECIAL and the export leaves Cost at 0 (dllinterface.cpp:4131-4139). 1995
       calls Help_Text with no cost argument for exactly those three
       (sidebar.cpp:2274-2286), so the price line is omitted rather than shown as $0. */
    *cost = e.cost > 0 ? e.cost : -1;
    return sb_art_name(e.name);
}

/* Resolve only, no drawing, so the sbtip verb can report the truth right after a cursor
   move with no shot in between -- the same split sbstate already makes at :2549. */
static bool sb_tip_resolve(void)
{
    g_tipName[0] = 0; g_tipCost = -1;
    if (!g_sbTips) return false;
    int cost = -1;
    const char* asset = sb_hover_subject(&cost);
    if (!asset) return false;
    const char* title = sb_title_for(asset);
    if (!title) return false;
    snprintf(g_tipName, sizeof g_tipName, "%s", title);
    g_tipCost = cost;
    return true;
}

static void sb_draw_tooltip(int fbw, int fbh)
{
    if (!sb_tip_resolve() || !g_dbPack) return;
    const int cost = g_tipCost;
    const char* title = g_tipName;

    const DB_Font* f = db_font(g_dbPack, "6POINT");
    if (!f) return;
    char price[16]; price[0] = 0;
    if (cost > 0) snprintf(price, sizeof price, "$%d", cost);

    const int wName = db_string_width(f, title, DB_FONT6_XSPACING);
    const int wCost = price[0] ? db_string_width(f, price, DB_FONT6_XSPACING) : 0;
    const int lh    = 10;
    const int w     = (wName > wCost ? wName : wCost) + 4;
    const int h     = (price[0] ? lh * 2 : lh) + 2;
    if (w > SBTIP_W || h > SBTIP_H) return;

    DB_Surface surf; surf.w = SBTIP_W; surf.h = SBTIP_H; surf.px = g_tipPx;
    db_clip_reset(&surf);
    memset(g_tipPx, DB_BLACK, sizeof g_tipPx);
    unsigned char fp[16];
    db_font_palette(fp, DB_GREEN, DB_TBLACK);
    db_print(&surf, f, title, 2, 1, fp, DB_FONT6_XSPACING);
    if (price[0]) db_print(&surf, f, price, 2, 1 + lh, fp, DB_FONT6_XSPACING);
    /* the 1995 box: one pixel of the text colour around the plate */
    db_line_h(&surf, 0, w - 1, 0,     DB_GREEN);
    db_line_h(&surf, 0, w - 1, h - 1, DB_GREEN);
    db_line_v(&surf, 0,     0, h - 1, DB_GREEN);
    db_line_v(&surf, w - 1, 0, h - 1, DB_GREEN);

    {   /* one conversion, into the scratch's own top-left w x h */
        DB_Surface c; c.w = SBTIP_W; c.h = SBTIP_H; c.px = g_tipPx;
        db_clip_reset(&c);
        db_surface_to_rgba(&c, g_dbPack->pal8, g_tipRGBA, 0);
    }
    if (!g_tipTex) {
        glGenTextures(1, &g_tipTex);
        glBindTexture(GL_TEXTURE_2D, g_tipTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, g_tipTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SBTIP_W, SBTIP_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, g_tipRGBA);

    /* WHERE IT SITS. Right-aligned flush to the LEFT of the hovered control, top edges
       level, which is help.cpp:315-318's own placement. Clamped on screen after, the way
       help.cpp:325-331 clamps. */
    float ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
    if (g_sbHover == SBH_ITEM) {
        const int idx = sb_col_base(g_sbHoverCol) + g_sbTop[g_sbHoverCol] + g_sbHoverSlot;
        if (!sb_item_rect(idx, &ix0, &iy0, &ix1, &iy1)) return;
    } else {
        /* The chrome controls have a hit test but no rect helper (the hit test works in
           DOS/HUD coordinates directly), so anchor to the POINTER for those rather than
           add a second geometry source that could drift from the first. */
        ix0 = g_mouseScrC; iy0 = g_mouseScrR;
        ix1 = ix0; iy1 = iy0;
    }
    const float sc = (float)fbh / (float)DB_SCREEN_H;
    const float tw = (float)w * sc, th = (float)h * sc;
    float x1 = ix0 - 2.0f, x0 = x1 - tw;
    float y0 = iy0,        y1 = y0 + th;
    if (x0 < 0.0f) { x0 = 0.0f; x1 = tw; }
    if (y1 > (float)fbh) { y1 = (float)fbh; y0 = y1 - th; }

    const float u1 = (float)w / (float)SBTIP_W, v1 = (float)h / (float)SBTIP_H;
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);          /* begin_overlay leaves blending off; opaque is also
                                     what 1995 draws -- a black plate, not a wash */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
    glTexCoord2f(u1,   0.0f); glVertex2f(x1, y0);
    glTexCoord2f(u1,   v1);   glVertex2f(x1, y1);
    glTexCoord2f(0.0f, v1);   glVertex2f(x0, y1);
    glEnd();
}

/* THE QUEUE COUNT, drawn on the cameo it belongs to.
 *
 *  Without it the feature is invisible: the player clicks three times and has no way to
 *  tell whether anything was registered until units start appearing a minute later.
 *
 *  Drawn as its own small quad off sb_item_rect rather than rasterised into the DOS
 *  sidebar surface, because sb_item_rect already answers for BOTH huds -- the DOS bar and
 *  the 640 plate have different columns, rows, scroll state and scale -- and a second
 *  geometry path is a second thing to keep in step with the first.
 */
#define SBQD_W 16
#define SBQD_H 12
static unsigned char g_qdPx[SBQD_W * SBQD_H];
static unsigned char g_qdRGBA[SBQD_W * SBQD_H * 4];
static GLuint g_qdTex = 0;

static void sb_draw_queue_digits(int fbw, int fbh)
{
    if (!g_sbQueueOn || g_sbQueue.empty() || !g_dbPack || !g_sbState.valid) return;
    const DB_Font* f = db_font(g_dbPack, "6POINT");
    if (!f) return;
    if (!g_qdTex) {
        glGenTextures(1, &g_qdTex);
        glBindTexture(GL_TEXTURE_2D, g_qdTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    for (size_t i = 0; i < g_sbState.entry.size(); i++) {
        const SbEntry& e = g_sbState.entry[i];
        const int n = sb_queued(e);
        if (n <= 0) continue;
        float x0, y0, x1, y1;
        if (!sb_item_rect((int)i, &x0, &y0, &x1, &y1)) continue;
        char d[4]; snprintf(d, sizeof d, "%d", n > 9 ? 9 : n);
        DB_Surface surf; surf.w = SBQD_W; surf.h = SBQD_H; surf.px = g_qdPx;
        db_clip_reset(&surf);
        memset(g_qdPx, DB_BLACK, sizeof g_qdPx);
        unsigned char fp[16];
        db_font_palette(fp, DB_GREEN, DB_TBLACK);
        const int w = db_string_width(f, d, DB_FONT6_XSPACING) + 3;
        const int hh = 9;                     /* hug the glyph; 6POINT is 6 tall */
        db_print(&surf, f, d, 2, 1, fp, DB_FONT6_XSPACING);
        db_line_h(&surf, 0, w - 1, 0,      DB_GREEN);
        db_line_h(&surf, 0, w - 1, hh - 1, DB_GREEN);
        db_line_v(&surf, 0,     0, hh - 1, DB_GREEN);
        db_line_v(&surf, w - 1, 0, hh - 1, DB_GREEN);
        {
            DB_Surface c; c.w = SBQD_W; c.h = SBQD_H; c.px = g_qdPx;
            db_clip_reset(&c);
            db_surface_to_rgba(&c, g_dbPack->pal8, g_qdRGBA, 0);
        }
        glBindTexture(GL_TEXTURE_2D, g_qdTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SBQD_W, SBQD_H, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, g_qdRGBA);
        /* bottom-right of the cameo, the corner the build clock does not use */
        const float sc = (float)fbh / (float)DB_SCREEN_H;
        const float dw = (float)w * sc, dh = (float)hh * sc;
        const float qx1 = x1 - 1.0f, qx0 = qx1 - dw;
        const float qy1 = y1 - 1.0f, qy0 = qy1 - dh;
        const float u1 = (float)w / (float)SBQD_W, v1 = (float)hh / (float)SBQD_H;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(qx0, qy0);
        glTexCoord2f(u1,   0.0f); glVertex2f(qx1, qy0);
        glTexCoord2f(u1,   v1);   glVertex2f(qx1, qy1);
        glTexCoord2f(0.0f, v1);   glVertex2f(qx0, qy1);
        glEnd();
    }
}

static void sb_draw_panel_640(int fbw, int fbh)
{
    H6_State st;
    if (!g_h6Pack) return;
    sb_slide_step();
    sb_layout(fbw, fbh);
    sb_pointer_refresh(fbw, fbh);
    sb_fill_dos_state();
    h6_fill_state(&st);

    hud640_draw_bar(g_h6Bar, g_h6Pack, &st);
    const int barh = hud640_bar_h(g_h6Rows);
    h6_upload(&g_h6TexBar, g_h6Bar, H6_BAR_W, barh);

    const float bw = (float)H6_BAR_W * g_h6Scale, bh = (float)barh * g_h6Scale;
    /* READ THE LAYOUT, do not recompute it: g_dbX0 is where the drawer has slid to, and
       the hit test, the item rects and the minimap hotspot all read the same value. A
       local `fbw - bw` would leave the picture behind while the clicks moved. */
    const float bx = g_dbX0;
    const float by = g_dbY0;
    /* The three plates are PINNED to the window edge and do not slide: the bar comes out
       from under its own handle, and the handle stays in the corner it is clicked in. */
    const float px = (float)fbw - bw;
    const float th = (float)H6_TAB_H * g_h6Scale;

    /* the letterbox, in the HUD's own near-black rather than the DOS palette's */
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor3ub(8, 9, 11);
    glBegin(GL_QUADS); sb_quad(bx, 0.0f, (float)fbw, (float)fbh); glEnd();

    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    h6_tex_quad(g_h6TexBar, H6_BAR_W, barh, bx, by, bx + bw, by + bh);

    hud640_draw_tab(g_h6Tab, g_h6Pack, NULL, st.credits, st.credits_frame);
    h6_upload(&g_h6TexCred, g_h6Tab, H6_BAR_W, H6_TAB_H);
    h6_tex_quad(g_h6TexCred, H6_BAR_W, H6_TAB_H, px - bw, by, px, by + th);

    /* OPTIONS anchors to the LEFT edge of the window, which is where the DOS tab bar
       puts it (tab.cpp draws the TABS plate at x=0 and again at width-Eva_Width). At
       640x480 the strip spans the screen; on a wider window the two ends stay pinned to
       their own edges rather than floating in the middle of the battlefield. */
    hud640_draw_tab(g_h6Tab, g_h6Pack, "OPTIONS", 0, st.options_frame);
    h6_upload(&g_h6TexOpt, g_h6Tab, H6_BAR_W, H6_TAB_H);
    h6_tex_quad(g_h6TexOpt, H6_BAR_W, H6_TAB_H, 0.0f, by, bw, by + th);

    /* THE HANDLE, drawn LAST so it sits over the bar for the whole travel. Its word used
       to be baked into the chassis art, which meant it could not survive the bar sliding
       out from under it -- so it is its own quad now, lettered from the game's own
       GRAD6FNT exactly the way OPTIONS is. */
    hud640_draw_tab(g_h6Tab, g_h6Pack, "SIDEBAR", 0, st.sidebar_frame);
    h6_upload(&g_h6TexSide, g_h6Tab, H6_BAR_W, H6_TAB_H);
    h6_tex_quad(g_h6TexSide, H6_BAR_W, H6_TAB_H, px, by, px + bw, by + th);

    glDisable(GL_TEXTURE_2D);
}

/* The engine's own placement verdict, painted on the ground. Drawn in the same pixel
   ortho as the panel by projecting each cell's four corners through the renderer's
   world_to_screen, so it needs no second copy of the camera maths. */
/* ONE PLACEMENT CELL, DRAPED ON THE GROUND rather than laid on the y = 0 plane.
 *
 * The overlay used to project every corner at a hard-coded world height of zero, and zero
 * is not the ground: the renderer re-zeroes the world on the median corner height of the
 * playable rect, so y = 0 is the mainland level only and every plateau, ramp and shoreline
 * sits somewhere else. Under a pitched camera a wrong height does not read as "too high",
 * it slides the whole footprint diagonally up-screen off the cells it is naming, which is
 * what "building projection ignores the height of the level" describes.
 *
 * The cell is split into two triangles on the SW-NE diagonal, which is the same split the
 * terrain pass uses (its two triangles are NW,SW,NE and NE,SW,SE, the console's own G_TRI2
 * order). That has to match: on a cell whose four corners are not coplanar the other
 * diagonal is a genuinely different surface, and the overlay would lift off the ground it
 * is meant to be describing. No depth offset is needed, because this pass runs with the
 * depth test off. */
static void sb_place_cell_tris(int cx, int cz, int fbw, int fbh)
{
    const float yNW = g_sb.TerrainCornerY ? g_sb.TerrainCornerY(cx,     cz)     : 0.0f;
    const float ySW = g_sb.TerrainCornerY ? g_sb.TerrainCornerY(cx,     cz + 1) : 0.0f;
    const float yNE = g_sb.TerrainCornerY ? g_sb.TerrainCornerY(cx + 1, cz)     : 0.0f;
    const float ySE = g_sb.TerrainCornerY ? g_sb.TerrainCornerY(cx + 1, cz + 1) : 0.0f;
    const float x = (float)cx, z = (float)cz;
    float cNW, rNW, cSW, rSW, cNE, rNE, cSE, rSE;
    g_sb.WorldToScreen(x,        yNW, z,        fbw, fbh, &cNW, &rNW);
    g_sb.WorldToScreen(x,        ySW, z + 1.0f, fbw, fbh, &cSW, &rSW);
    g_sb.WorldToScreen(x + 1.0f, yNE, z,        fbw, fbh, &cNE, &rNE);
    g_sb.WorldToScreen(x + 1.0f, ySE, z + 1.0f, fbw, fbh, &cSE, &rSE);
    glVertex2f(cNW, rNW); glVertex2f(cSW, rSW); glVertex2f(cNE, rNE);
    glVertex2f(cNE, rNE); glVertex2f(cSW, rSW); glVertex2f(cSE, rSE);
}

static void sb_draw_placement(int fbw, int fbh)
{
    if (!g_sbOn || !g_sbPlacing || g_sbPlaceIdx < 0 || !g_sb.WorldToScreen) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);

    /* 1. the whole legal region, faint. This is the decoded GAME_STATE_PLACEMENT grid,
          shown so the overlay can be read rather than trusted. */
    glBegin(GL_TRIANGLES);
    for (int y = 0; y < g_sb.mapH; y++) {
        for (int x = 0; x < g_sb.mapW; x++) {
            int i = y * g_sb.mapW + x;
            if (!g_sbPlaceProx[i]) continue;
            bool ok = sb_legal_origin(g_sb.mapX + x, g_sb.mapY + y);
            glColor4f(ok ? 0.15f : 0.95f, ok ? 1.0f : 0.55f, 0.15f, ok ? 0.32f : 0.20f);
            sb_place_cell_tris(g_sb.mapX + x, g_sb.mapY + y, fbw, fbh);
        }
    }
    glEnd();

    /* 2. the cursor: the pending building's actual footprint under the mouse */
    if (g_sbHoverX >= 0 && g_sbPlaceIdx >= 0
        && g_sbPlaceIdx < (int)g_sbState.entry.size()) {
        const SbEntry& e = g_sbState.entry[g_sbPlaceIdx];
        bool ok = sb_legal_origin(g_sbHoverX, g_sbHoverY);
        int origin = g_sbHoverY * 128 + g_sbHoverX;
        glBegin(GL_TRIANGLES);
        for (size_t k = 0; k < e.occupy.size(); k++) {
            int c = origin + e.occupy[k];
            int cxx = c % 128, cyy = c / 128;
            int gi = sb_grid_index(cxx, cyy);
            bool cellok = (gi >= 0 && g_sbPlaceClear[gi]);
            glColor4f(cellok ? 0.2f : 1.0f, cellok ? 1.0f : 0.2f, 0.2f, ok ? 0.45f : 0.35f);
            sb_place_cell_tris(cxx, cyy, fbw, fbh);
        }
        glEnd();
    }
    glDisable(GL_BLEND);
}

/* ==================================================================================== *
 *  Input (pixel coordinates and a button; no window system in here)
 * ==================================================================================== */

/* ---- hit testing, in DOS pixels ---------------------------------------------------- */

/* (enum SbHit is declared further up, next to the pointer state that uses it.) */

/* dx,dy are DOS screen pixels. For SBH_ITEM/UP/DOWN *column is the strip and, for
   SBH_ITEM, *slot is 0..3, the visible row. */
static SbHit sb_hit_dos(int dx, int dy, int* column, int* slot)
{
    *column = -1;
    *slot = -1;

    /* the three text buttons.  sidebar.cpp:371-384 Init_IO, lo-res branch */
    if (dy >= DB_REPAIR_Y && dy < DB_REPAIR_Y + DB_BUTTON_HEIGHT) {
        if (dx >= DB_REPAIR_X && dx < DB_REPAIR_X + DB_REPAIR_W) return SBH_REPAIR;
        if (dx >= DB_SELL_X   && dx < DB_SELL_X   + DB_SELL_W)   return SBH_SELL;
        if (dx >= DB_MAP_X    && dx < DB_MAP_X    + DB_MAP_W)    return SBH_MAP;
    }

    /* the radar interior.  radar.cpp:141-144 */
    if (dx >= DB_RAD_X + DB_RAD_OFF_X && dx < DB_RAD_X + DB_RAD_OFF_X + DB_RAD_I_WIDTH &&
        dy >= DB_RAD_Y + DB_RAD_OFF_Y && dy < DB_RAD_Y + DB_RAD_OFF_Y + DB_RAD_I_HEIGHT)
        return SBH_RADAR;

    for (int c = 0; c < DB_COLUMNS; c++) {
        const int sx = sb_strip_x(c);
        /* the scroll pair, drawn flush with the bottom of the screen */
        if (dy >= DB_COLUMN_Y - 1 + DB_UP_Y_OFFSET &&
            dy < DB_COLUMN_Y - 1 + DB_UP_Y_OFFSET + DB_SBUTTON_HEIGHT) {
            if (dx >= sx + DB_UP_X_OFFSET && dx < sx + DB_UP_X_OFFSET + DB_BUTTON_WIDTH)
                { *column = c; return SBH_UP; }
            if (dx >= sx + DB_DOWN_X_OFFSET && dx < sx + DB_DOWN_X_OFFSET + DB_BUTTON_WIDTH)
                { *column = c; return SBH_DOWN; }
        }
        /* the four cameo buttons.  SelectButton[c][i] at (sx, COLUMN_Y + 24*i), 32x24 */
        if (dx >= sx && dx < sx + DB_OBJECT_WIDTH &&
            dy >= DB_COLUMN_Y && dy < DB_COLUMN_Y + DB_MAX_VISIBLE * DB_OBJECT_HEIGHT) {
            *column = c;
            *slot = (dy - DB_COLUMN_Y) / DB_OBJECT_HEIGHT;
            return SBH_ITEM;
        }
    }
    return SBH_NONE;
}

/* The same verdicts as sb_hit_dos, read off the generated layout instead of the DOS
   constants. Every rectangle here was measured on the reference art. */
static SbHit sb_hit_h6(int hx, int hy, int* column, int* slot)
{
    int i, c, r;
    *column = -1;
    *slot = -1;

    if (hy >= H6_BTN_REPAIR_Y && hy < H6_BTN_REPAIR_Y + H6_BTN_REPAIR_H) {
        if (hx >= H6_BTN_REPAIR_X && hx < H6_BTN_REPAIR_X + H6_BTN_REPAIR_W) return SBH_REPAIR;
        if (hx >= H6_BTN_SELL_X   && hx < H6_BTN_SELL_X   + H6_BTN_SELL_W)   return SBH_SELL;
        if (hx >= H6_BTN_MAP_X    && hx < H6_BTN_MAP_X    + H6_BTN_MAP_W)    return SBH_MAP;
    }

    if (hx >= H6_RADAR_X && hx < H6_RADAR_X + H6_RADAR_W &&
        hy >= H6_RADAR_Y && hy < H6_RADAR_Y + H6_RADAR_H)
        return SBH_RADAR;

    /* four arrows on one row: up/down, up/down, one pair per column */
    const int arrowY = hud640_arrow_y(g_h6Rows);
    if (hy >= arrowY && hy < arrowY + H6_ARROW_UP_H) {
        for (i = 0; i < 4; i++) {
            if (hx >= H6_ARROW_X[i] && hx < H6_ARROW_X[i] + H6_ARROW_UP_W) {
                *column = i / 2;
                return (i & 1) ? SBH_DOWN : SBH_UP;
            }
        }
    }

    for (c = 0; c < H6_COLUMNS; c++) {
        if (hx < H6_COL_X[c] || hx >= H6_COL_X[c] + H6_CELL_W) continue;
        for (r = 0; r < g_h6Rows; r++) {
            const int ry = hud640_row_y(r);
            if (hy >= ry && hy < ry + H6_CELL_H) {
                *column = c;
                *slot = r;
                return SBH_ITEM;
            }
        }
    }
    return SBH_NONE;
}

/* One seam for both HUDs, so every caller stays HUD-agnostic. */
static SbHit sb_hit_at(float col, float row, int* c, int* slot)
{
    int ax, ay;
    /* The window-pinned plates first: they are in SCREEN space and converting them to
       bar-local coordinates would put OPTIONS at a negative x. Bar-local rows 0..16 hit
       nothing today, so claiming them costs no existing control. */
    {
        const SbHit ch = sb_chrome_hit(col, row, g_dbFbw, g_dbFbh);
        if (ch != SBH_NONE) { if (c) *c = -1; if (slot) *slot = -1; return ch; }
    }
    if (g_hudNew && g_h6Pack) {
        sb_screen_to_h6(col, row, &ax, &ay);
        return sb_hit_h6(ax, ay, c, slot);
    }
    sb_screen_to_dos(col, row, &ax, &ay);
    return sb_hit_dos(ax, ay, c, slot);
}

/* Called once at the top of the panel draw, off the same virtual mouse the click path
   reads, so what lights up under the pointer and what a click does can never disagree. */
static void sb_pointer_refresh(int fbw, int fbh)
{
    int c, slot;
    g_sbHover = SBH_NONE;
    g_sbHoverCol = -1;
    g_sbHoverSlot = -1;
    if (g_mouseScrC < 0.0f || !sb_over_panel(g_mouseScrC, g_mouseScrR, fbw, fbh))
        return;
    g_sbHover = sb_hit_at(g_mouseScrC, g_mouseScrR, &c, &slot);
    g_sbHoverCol = c;
    g_sbHoverSlot = slot;
}

/* The button came up. Whatever it was holding down is no longer held; the control it is
   still over goes back to hover on the next frame. */
static void sb_pointer_up(void)
{
    g_sbPress = SBH_NONE;
    g_sbPressCol = -1;
}

/* One control's frame index: pressed beats engaged beats hover beats resting. Pressed
   only wins while the pointer is still on the control the press started on. */
static int sb_frame_for(SbHit which, int column, int engaged)
{
    if (g_sbPress == which && (column < 0 || g_sbPressCol == column) &&
        g_sbHover == which && (column < 0 || g_sbHoverCol == column))
        return H6_FR_PRESSED;
    if (engaged)
        return H6_FR_ACTIVE;
    if (g_sbHover == which && (column < 0 || g_sbHoverCol == column))
        return H6_FR_HOVER;
    return H6_FR_NORMAL;
}

static void sb_toggle_repair(void)
{
    if (!g_sbState.repairEnabled) { printf("SIDEBAR-INPUT|repair|DISABLED\n"); return; }
    g_sbRepairOn = !g_sbRepairOn;
    if (g_sbRepairOn) g_sbSellOn = false;
    if (g_sb.StructureReq)
        g_sb.StructureReq(g_sbRepairOn ? INPUT_STRUCTURE_REPAIR_START : INPUT_STRUCTURE_CANCEL,
                          0, 0);
    printf("SIDEBAR-INPUT|repair|%s\n", g_sbRepairOn ? "on" : "off");
}

static void sb_toggle_sell(void)
{
    if (!g_sbState.sellEnabled) { printf("SIDEBAR-INPUT|sell|DISABLED\n"); return; }
    g_sbSellOn = !g_sbSellOn;
    if (g_sbSellOn) g_sbRepairOn = false;
    if (g_sb.StructureReq)
        g_sb.StructureReq(g_sbSellOn ? INPUT_STRUCTURE_SELL_START : INPUT_STRUCTURE_CANCEL,
                          0, 0);
    printf("SIDEBAR-INPUT|sell|%s\n", g_sbSellOn ? "on" : "off");
}

/* returns true when the click was consumed by the panel */
static bool sb_click(float col, float row, int fbw, int fbh, bool right)
{
    if (!sb_over_panel(col, row, fbw, fbh)) return false;

    /* THE THREE PINNED PLATES, handled ABOVE the g_sbState.valid guard: OPTIONS has to
       work before the first sidebar poll, and the credits readout has to eat its click
       either way rather than letting it fall through to the map. */
    {
        const SbHit ch = sb_chrome_hit(col, row, fbw, fbh);
        if (ch == SBH_OPTIONS) {
            if (!right) {
                g_sbPress = ch; g_sbHover = ch;
                g_sbOptionsReq = true;
                printf("SIDEBAR-INPUT|options|open\n");
                fflush(stdout);
            }
            return true;
        }
        if (ch == SBH_CREDITS)
            return true;              /* a readout, not a button */
        if (ch == SBH_SIDEBAR) {
            if (!right) {
                g_sbPress = ch; g_sbHover = ch;
                g_h6SlideTarget = g_h6SlideTarget ? 0 : H6_BAR_W;
                printf("SIDEBAR-INPUT|slide|%s\n", g_h6SlideTarget ? "hide" : "show");
                fflush(stdout);
            }
            return true;
        }
    }

    if (!g_sbState.valid) return false;

    int c, slot;
    const SbHit hit = sb_hit_at(col, row, &c, &slot);

    /* Remember what the press landed on so the art HUD can draw it held. Cleared by
       sb_pointer_up() on the button-up event. Right-clicks do not depress a control:
       in this UI they mean hold/cancel on a cameo, not "push this button". */
    if (!right) {
        g_sbPress = hit;
        g_sbPressCol = c;
        g_sbHover = hit;
        g_sbHoverCol = c;
    }

    if (hit == SBH_UP || hit == SBH_DOWN) {
        /* sidebar.cpp:1521-1541 Scroll: one row per press, clamped at both ends. */
        const int before = g_sbTop[c];
        g_sbTop[c] += (hit == SBH_UP) ? -1 : 1;
        sb_clamp_top();
        printf("SIDEBAR-INPUT|scroll|col=%d|%s|top=%d->%d\n", c,
               hit == SBH_UP ? "up" : "down", before, g_sbTop[c]);
        return true;
    }
    if (hit == SBH_REPAIR) { if (!right) sb_toggle_repair(); return true; }
    if (hit == SBH_SELL)   { if (!right) sb_toggle_sell();   return true; }
    if (hit == SBH_MAP) {
        if (!right) {
            if (!g_sbState.radarActive && !g_sbRadarForce)
                { printf("SIDEBAR-INPUT|map|DISABLED\n"); return true; }
            g_sbRadarShown = !g_sbRadarShown;
            printf("SIDEBAR-INPUT|map|radar %s\n", g_sbRadarShown ? "on" : "off");
        }
        return true;
    }
    if (hit != SBH_ITEM) return true;   /* the panel ate it, but there is nothing there */

    const int idx = sb_col_base(c) + g_sbTop[c] + slot;
    if (idx < 0 || idx >= (int)g_sbState.entry.size()) return true;
    if (g_sbState.entry[idx].column != c) return true;   /* past the end of this column */

    {
        const size_t i = (size_t)idx;
        const SbEntry& e = g_sbState.entry[i];
        if (right) {
            /* C&C's right-click ladder, in this order:
                 mid-build            -> HOLD (progress and money are kept)
                 held, or done but
                 not being placed     -> CANCEL (ABANDON, which refunds)
                 done AND being placed-> just leave placement mode, still READY.
               The last one matters: a misclick on the map should not cost the building. */
            if (e.type == SPECIAL) {
                /* There is nothing to hold or refund on a special: 1995's right click
                   over one does nothing at all. The only state it can have out here is
                   armed, so this is a disarm and nothing else. */
                if (sb_super_is(e)) sb_super_disarm("right click");
            } else if (g_sbPlacing && (int)i == g_sbPlaceIdx) {
                sb_cancel_placement();
                printf("SIDEBAR-INPUT|cancelplace|%s\n", e.name);
            } else if (g_sbQueueOn && sb_queued(e) > 0) {
                /* ONE RUNG ABOVE THE EXISTING LADDER, which is left intact below:
                   placing -> leave placement, constructing -> HOLD, onHold/completed ->
                   CANCEL. Taking one off the queue before touching the thing actually
                   building is what EA's own surviving comment says their UI did
                   (dllinterface.cpp:3906-3908, "subsequenct right-clicks to decrement
                   that queue count"), so this rung is faithful rather than invented. */
                int& n = g_sbQueue[std::make_pair(e.btype, e.bid)];
                if (--n <= 0) g_sbQueue.erase(std::make_pair(e.btype, e.bid));
                printf("SBQUEUE|sub|%s|btype=%d|bid=%d|n=%d\n", e.name, e.btype, e.bid,
                       n > 0 ? n : 0);
                fflush(stdout);
            } else if (e.constructing) {
                sb_hold((int)i);
                printf("SIDEBAR-INPUT|hold|%s\n", e.name);
            } else if (e.onHold || e.completed) {
                sb_cancel((int)i);
                printf("SIDEBAR-INPUT|cancel|%s\n", e.name);
            }
        } else {
            /* A SPECIAL is fired, not built. Before v0.5.9 it fell into the sb_start
               arm below and went out as a construction request, which the engine has
               nothing to do with, so the icon read as dead: the whole click path after
               `spc = buildable_id` was guarded by `if (spc == 0 && choice)` with no
               `else if (spc)` anywhere. Clicking a READY special now arms targeting and
               the next map click fires it; clicking one that is still charging says so
               rather than doing nothing silently. */
            if (e.type == SPECIAL) {
                if (sb_super_is(e)) {
                    sb_super_disarm("clicked again");
                } else if (e.completed) {
                    sb_cancel_placement();          /* the two modes are exclusive */
                    sb_super_arm(e);
                } else {
                    printf("SUPER|notready|%s|progress=%.2f\n", e.name, e.progress);
                    fflush(stdout);
                }
            } else if (e.completed && e.type == BUILDING_TYPE) {
                sb_start_placement((int)i);
                printf("SIDEBAR-INPUT|startplace|%s\n", e.name);
            } else if (!e.constructing) {
                sb_start((int)i);
                printf("SIDEBAR-INPUT|build|%s\n", e.name);
            } else if (g_sbQueueOn && sb_queueable(e)) {
                /* Already building, and clicking it again is free (see the note by
                   g_sbQueue): today this arm does not exist and the click is swallowed. */
                int& n = g_sbQueue[std::make_pair(e.btype, e.bid)];
                if (n < SB_QUEUE_MAX) n++;
                printf("SBQUEUE|add|%s|btype=%d|bid=%d|n=%d\n", e.name, e.btype, e.bid, n);
                fflush(stdout);
            }
        }
    }
    return true;    /* clicked the panel background: still consumed */
}

/* The wheel scrolls the strip the pointer is over; over anything else in the panel it
   scrolls both, which is what a wheel over the frame ought to do. */
static void sb_scroll_at(float col, float row, int fbw, int fbh, int dir)
{
    sb_layout(fbw, fbh);
    int c, slot;
    const SbHit hit = sb_hit_at(col, row, &c, &slot);
    if (hit == SBH_ITEM || hit == SBH_UP || hit == SBH_DOWN) {
        g_sbTop[c] += dir;
    } else {
        g_sbTop[0] += dir;
        g_sbTop[1] += dir;
    }
    sb_clamp_top();
}

static void sb_set_hover(int cellx, int celly) { g_sbHoverX = cellx; g_sbHoverY = celly; }

/* The radar toggle: one flag, shared by the MAP button, the M key, --nominimap and the
   minimapon/minimapoff script verbs. */
static bool sb_radar_shown(void)
{
    return g_sbRadarShown && (g_sbState.radarActive || g_sbRadarForce);
}
static void sb_toggle_radar(void) { g_sbRadarShown = !g_sbRadarShown; }
static void sb_set_radar(bool on) { g_sbRadarShown = on; }
static void sb_force_radar(bool f) { g_sbRadarForce = f; }

/* ==================================================================================== *
 *  Machine-readable state dump, so a headless run can be diffed rather than eyeballed
 * ==================================================================================== */

static void sb_report(const char* tag)
{
    if (!g_sbState.valid) { printf("SIDEBAR|%s|INVALID\n", tag); return; }
    printf("SIDEBAR|%s|credits=%d|tib=%d/%d|power=%d/%d|entries=%d|placing=%d\n",
           tag, g_sbState.credits, g_sbState.tiberium, g_sbState.maxTiberium,
           g_sbState.powerProduced, g_sbState.powerDrained,
           (int)g_sbState.entry.size(), g_sbPlacing ? 1 : 0);
    for (size_t i = 0; i < g_sbState.entry.size(); i++) {
        const SbEntry& e = g_sbState.entry[i];
        printf("SBITEM|%d|col=%d|%s|btype=%d|bid=%d|cost=%d|time=%d|power=%+d"
               "|prog=%.3f|constructing=%d|hold=%d|ready=%d|busy=%d|occupy=%d\n",
               (int)i, e.column, e.name, e.btype, e.bid, e.cost, e.buildTime, e.power,
               e.progress, e.constructing ? 1 : 0, e.onHold ? 1 : 0,
               e.completed ? 1 : 0, e.busy ? 1 : 0, (int)e.occupy.size());
    }
    fflush(stdout);
}

/* ==================================================================================== *
 *  SCRIPTED INPUT
 *
 *  Mandatory, not a convenience: it is how "the player can build" gets proved with no
 *  human at the keyboard. These verbs slot into the renderer's existing --script
 *  interpreter (script_line), so there is ONE script language, not two.
 *
 *    sidebar [TAG]        print the decoded sidebar state (SIDEBAR|/SBITEM| lines)
 *    build NAME           SIDEBAR_REQUEST_START_CONSTRUCTION
 *    hold NAME            SIDEBAR_REQUEST_HOLD_CONSTRUCTION
 *    cancelbuild NAME     SIDEBAR_REQUEST_CANCEL_CONSTRUCTION
 *    ready NAME [MAX]     tick until that entry reports Completed (default max 6000)
 *    startplace NAME      SIDEBAR_REQUEST_START_PLACEMENT (placement mode on)
 *    place X Y            SIDEBAR_REQUEST_PLACE at absolute engine cell X,Y
 *    place auto           ... at the first cell the decoded overlay calls legal
 *    cancelplace          SIDEBAR_CANCEL_PLACE
 *    hover X Y            move the placement cursor, so a screenshot shows it
 *    sbclick C R          left-click the PANEL at drawable pixel C,R (the real UI path)
 *    sbrclick C R         right-click the panel
 *    sbclickitem NAME     left-click that button at its own drawn centre
 *    sbrclickitem NAME    right-click it
 *    sbitem NAME          print the on-screen rectangle of that button, for sbclick
 *    sbhold C R           press and HOLD there (also parks the pointer on it)
 *    sbrelease            let the held button back up
 *    sbstates             print the frame each control would draw: normal/hover/
 *                         pressed/active. Pair with `cursor C R` to test hover.
 *    placemap             ASCII dump of the decoded GAME_STATE_PLACEMENT grid
 *
 *  Everything that mutates the game goes through the same functions the mouse handler
 *  uses, so a green script is evidence about the real UI and not about a side door.
 * ==================================================================================== */

/* the host advances the sim and refreshes its own object list; the sidebar only asks */
typedef void (*SbTickFn)(int nticks);

static void sb_placemap(void)
{
    if (!g_sbPlacing) { printf("PLACEMAP|not in placement mode\n"); return; }
    int prox = 0, clear = 0, legal = 0;
    for (int i = 0; i < g_sb.mapW * g_sb.mapH; i++) {
        if (g_sbPlaceProx[i]) prox++;
        if (g_sbPlaceClear[i]) clear++;
    }
    for (int y = 0; y < g_sb.mapH; y++)
        for (int x = 0; x < g_sb.mapW; x++)
            if (sb_legal_origin(g_sb.mapX + x, g_sb.mapY + y)) legal++;
    printf("PLACEMAP|origin=%d,%d|size=%dx%d|cells=%d|prox=%d|clear=%d|legal=%d\n",
           g_sb.mapX, g_sb.mapY, g_sb.mapW, g_sb.mapH, g_sb.mapW * g_sb.mapH,
           prox, clear, legal);
    printf("PLACEMAP|  # legal origin, p proximity only, c clear only, . neither\n");
    for (int y = 0; y < g_sb.mapH; y++) {
        printf("PLACEMAP|%3d ", g_sb.mapY + y);
        for (int x = 0; x < g_sb.mapW; x++) {
            int i = y * g_sb.mapW + x;
            char ch;
            if (sb_legal_origin(g_sb.mapX + x, g_sb.mapY + y)) ch = '#';
            else if (g_sbPlaceProx[i]) ch = 'p';
            else if (g_sbPlaceClear[i]) ch = 'c';
            else ch = '.';
            putchar(ch);
        }
        putchar('\n');
    }
    fflush(stdout);
}

/* Every refusal below bumps this, and the host folds it into its own script failure
   count after each verb. Without it a sidebar verb could print FAIL and the script would
   still exit 0, which is exactly the sort of quiet lie this project forbids: that really
   happened once, when a raw-pixel sbclick aimed at a 1280x720 panel missed the 640x480
   one, the build never started, "ready" timed out, "place" printed FAIL, and the run
   still reported "0 failures". */
static int g_sbFails = 0;

/* Returns true when the verb belonged to the sidebar (handled or refused with a printed
   reason). The host's script_line should try this first and fall through otherwise. */
static bool sb_script_verb(const char* v, const char* a1, const char* a2, const char* a3,
                           SbTickFn tick, int fbw, int fbh)
{
    (void)a3;
    if (!strcasecmp(v, "sidebar")) {
        sb_poll();
        sb_report((a1 && *a1) ? a1 : "script");
        return true;
    }
    if (!strcasecmp(v, "placemap")) { sb_poll(); sb_placemap(); return true; }
    if (!strcasecmp(v, "objdump")) {
        /* The brain's own art-free enumerator, straight to stdout. This is the evidence
           a placement really happened: the new building shows up with limbo=0 at the
           cell that was asked for. */
        if (g_sb.Dump) g_sb.Dump();
        else printf("OBJDUMP|CNC3D_Dump_Objects not exported\n");
        return true;
    }

    if (!strcasecmp(v, "queue")) {
        /* queue NAME [N] -- add N to the queue for that buildable, exactly as N extra
           left clicks on its cameo would. Drives the same map the clicks drive, so a gate
           tests the feature rather than a parallel path. */
        sb_poll();
        int i = sb_find(a1);
        if (i < 0) { printf("SCRIPT|queue|%s|FAIL not in sidebar\n", a1 ? a1 : "");
                     g_sbFails++; return true; }
        int n = a2 ? atoi(a2) : 1;
        const SbEntry& e = g_sbState.entry[i];
        if (!sb_queueable(e)) {
            printf("SBQUEUE|refused|%s|type=%d|reason=not-a-unit\n", e.name, e.type);
            fflush(stdout); return true;
        }
        int& q = g_sbQueue[std::make_pair(e.btype, e.bid)];
        for (int k = 0; k < n && q < SB_QUEUE_MAX; k++) q++;
        printf("SBQUEUE|add|%s|btype=%d|bid=%d|n=%d\n", e.name, e.btype, e.bid, q);
        fflush(stdout);
        return true;
    }
    if (!strcasecmp(v, "queuedump")) {
        sb_poll();
        printf("SBQUEUEDUMP|entries=%d\n", (int)g_sbQueue.size());
        for (std::map<std::pair<int,int>, int>::iterator it = g_sbQueue.begin();
             it != g_sbQueue.end(); ++it) {
            const char* nm = "-";
            for (size_t i = 0; i < g_sbState.entry.size(); i++)
                if (g_sbState.entry[i].btype == it->first.first &&
                    g_sbState.entry[i].bid   == it->first.second) {
                    nm = g_sbState.entry[i].name; break;
                }
            printf("SBQUEUEITEM|%s|btype=%d|bid=%d|n=%d\n",
                   nm, it->first.first, it->first.second, it->second);
        }
        fflush(stdout);
        return true;
    }
    if (!strcasecmp(v, "build") || !strcasecmp(v, "hold")
        || !strcasecmp(v, "cancelbuild") || !strcasecmp(v, "startplace")) {
        sb_poll();
        int i = sb_find(a1);
        if (i < 0) {
            printf("SCRIPT|%s|%s|FAIL not in sidebar\n", v, a1 ? a1 : "");
            g_sbFails++;
            return true;
        }
        printf("SCRIPT|%s|%s|btype=%d|bid=%d|cost=%d|credits=%d\n", v, a1,
               g_sbState.entry[i].btype, g_sbState.entry[i].bid,
               g_sbState.entry[i].cost, g_sbState.credits);
        if (!strcasecmp(v, "build")) sb_start(i);
        else if (!strcasecmp(v, "hold")) sb_hold(i);
        else if (!strcasecmp(v, "cancelbuild")) sb_cancel(i);
        else sb_start_placement(i);
        if (tick) tick(1);
        sb_poll();
        return true;
    }

    if (!strcasecmp(v, "ready")) {
        int maxt = (a2 && *a2) ? atoi(a2) : 6000;
        int n = 0;
        sb_poll();
        while (n < maxt) {
            int i = sb_find(a1);
            if (i >= 0 && g_sbState.entry[i].completed) break;
            if (!tick) break;
            tick(1);
            n++;
            sb_poll();
        }
        int i = sb_find(a1);
        bool ok = (i >= 0 && g_sbState.entry[i].completed);
        printf("SCRIPT|ready|%s|%s after %d ticks|progress=%.3f\n", a1,
               ok ? "COMPLETED" : "FAIL TIMED OUT", n,
               i >= 0 ? g_sbState.entry[i].progress : -1.0f);
        if (!ok) g_sbFails++;
        return true;
    }

    if (!strcasecmp(v, "place")) {
        sb_poll();
        if (!g_sbPlacing) {
            printf("SCRIPT|place|FAIL not in placement mode\n");
            g_sbFails++;
            return true;
        }
        int cx, cy;
        if (a1 && !strcasecmp(a1, "auto")) {
            if (!sb_first_legal(&cx, &cy)) {
                printf("SCRIPT|place|FAIL no legal cell in the overlay\n");
                g_sbFails++;
                return true;
            }
            printf("SCRIPT|place|auto -> cell %d,%d\n", cx, cy);
        } else {
            cx = a1 ? atoi(a1) : -1;
            cy = a2 ? atoi(a2) : -1;
        }
        sb_set_hover(cx, cy);
        sb_place_at(cx, cy);
        if (tick) tick(1);
        sb_poll();
        printf("SCRIPT|place|placementmode now %d\n", g_sbPlacing ? 1 : 0);
        return true;
    }

    if (!strcasecmp(v, "cancelplace")) {
        sb_cancel_placement();
        if (tick) tick(1);
        sb_poll();
        printf("SCRIPT|cancelplace|placementmode now %d\n", g_sbPlacing ? 1 : 0);
        return true;
    }

    if (!strcasecmp(v, "hover")) {
        sb_set_hover(a1 ? atoi(a1) : -1, a2 ? atoi(a2) : -1);
        printf("SCRIPT|hover|%d,%d|legal=%d\n", g_sbHoverX, g_sbHoverY,
               sb_legal_origin(g_sbHoverX, g_sbHoverY) ? 1 : 0);
        return true;
    }

    /* Every buildable the brain is offering, with the art stem the lookup will use.
       Written for the superweapon work: "the icon is missing" and "the icon is there
       under a name nothing is filed under" look identical on screen, and only this
       tells them apart. */
    if (!strcasecmp(v, "sbdump")) {
        sb_poll();
        printf("SBDUMP|entries=%d\n", (int)g_sbState.entry.size());
        for (size_t i = 0; i < g_sbState.entry.size(); i++) {
            const SbEntry& e = g_sbState.entry[i];
            printf("SBENT|%d|name=%s|art=%s|type=%d|btype=%d|bid=%d|col=%d"
                   "|constructing=%d|completed=%d|progress=%.2f|special=%d\n",
                   (int)i, e.name, sb_art_name(e.name), e.type, e.btype, e.bid,
                   e.column, e.constructing ? 1 : 0, e.completed ? 1 : 0,
                   e.progress, e.type == SPECIAL ? 1 : 0);
        }
        /* The armed special is printed by NAME as well as id because the latch is an
           identity now: a gate that only reads the id cannot tell a correct latch from
           one that has drifted onto whatever entry happens to share the id. */
        printf("SBDUMP|armed=%d|armedid=%d|armedname=%s\n",
               sb_super_armed() ? 1 : 0, sb_super_id(),
               sb_super_armed() ? g_sbSuperName : "-");
        return true;
    }

    if (!strcasecmp(v, "sbitem")) {
        sb_poll();
        sb_layout(fbw, fbh);
        int i = sb_find(a1);
        float x0, y0, x1, y1;
        if (i >= 0 && sb_item_rect(i, &x0, &y0, &x1, &y1))
            printf("SBRECT|%s|%d|%.0f,%.0f..%.0f,%.0f|centre=%.0f,%.0f\n",
                   a1, i, x0, y0, x1, y1, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        else
            printf("SBRECT|%s|not visible\n", a1 ? a1 : "");
        return true;
    }

    if (!strcasecmp(v, "sbclickitem") || !strcasecmp(v, "sbrclickitem")) {
        /* Same path as sbclick, but the script does not have to know the pixel layout:
           the rectangle comes from sb_item_rect, which is the rectangle that was drawn. */
        sb_poll();
        sb_layout(fbw, fbh);
        int i = sb_find(a1);
        float x0, y0, x1, y1;
        if (i < 0 || !sb_item_rect(i, &x0, &y0, &x1, &y1)) {
            printf("SCRIPT|%s|%s|FAIL not a visible button\n", v, a1 ? a1 : "");
            g_sbFails++;
            return true;
        }
        float c = (x0 + x1) * 0.5f, r = (y0 + y1) * 0.5f;
        bool right = !strcasecmp(v, "sbrclickitem");
        bool hit = sb_click(c, r, fbw, fbh, right);
        printf("SCRIPT|%s|%s|at %.0f,%.0f|onpanel=%d%s\n", v, a1, c, r, hit ? 1 : 0,
               hit ? "" : "|FAIL click missed the panel");
        if (!hit) g_sbFails++;
        if (tick) tick(1);
        sb_poll();
        return true;
    }

    if (!strcasecmp(v, "sbslide")) {
        /* Drive and read the drawer in text. `settle` is what keeps every EXISTING
           sidebar gate byte-identical: the default is 0/0, so a script that never says
           sbslide moves nothing at all. */
        if (a1 && !strcasecmp(a1, "hide"))        g_h6SlideTarget = H6_BAR_W;
        else if (a1 && !strcasecmp(a1, "show"))   g_h6SlideTarget = 0;
        else if (a1 && !strcasecmp(a1, "toggle")) g_h6SlideTarget = g_h6SlideTarget ? 0 : H6_BAR_W;
        else if (a1 && !strcasecmp(a1, "settle")) g_h6SlideX = g_h6SlideTarget;
        sb_layout(fbw, fbh);
        printf("SBSLIDE|x=%d|target=%d|dbx0=%.0f\n",
               g_h6SlideX, g_h6SlideTarget, (double)g_dbX0);
        return true;
    }
    if (!strcasecmp(v, "sbclick") || !strcasecmp(v, "sbrclick")) {
        sb_poll();
        float c = a1 ? (float)atof(a1) : -1.0f, r = a2 ? (float)atof(a2) : -1.0f;
        bool right = !strcasecmp(v, "sbrclick");
        bool hit = sb_click(c, r, fbw, fbh, right);
        /* A click is down THEN up. Without the release the control would stay drawn
           held for the rest of the run, which is not what a click looks like. Use
           sbhold/sbrelease when you want to photograph the pressed frame. */
        sb_pointer_up();
        printf("SCRIPT|%s|%.0f,%.0f|onpanel=%d%s\n", v, c, r, hit ? 1 : 0,
               hit ? "" : "|FAIL click missed the panel");
        if (!hit) g_sbFails++;
        if (tick) tick(1);
        sb_poll();
        return true;
    }

    /* sbhold C R / sbrelease: the two halves of a click, so a script can stop between
       them and screenshot a control while it is actually held down. sbhold also parks
       the virtual mouse on the control, because a press only draws pressed while the
       pointer is still over what it started on. */
    if (!strcasecmp(v, "sbhold")) {
        sb_poll();
        float c = a1 ? (float)atof(a1) : -1.0f, r = a2 ? (float)atof(a2) : -1.0f;
        g_mouseScrC = c; g_mouseScrR = r;
        bool hit = sb_click(c, r, fbw, fbh, false);
        printf("SCRIPT|sbhold|%.0f,%.0f|onpanel=%d%s\n", c, r, hit ? 1 : 0,
               hit ? "" : "|FAIL hold missed the panel");
        if (!hit) g_sbFails++;
        sb_poll();
        return true;
    }
    if (!strcasecmp(v, "sbrelease")) {
        sb_pointer_up();
        printf("SCRIPT|sbrelease|ok\n");
        return true;
    }

    /* What the HUD would draw right now for each control, so button states can be
       asserted in text instead of eyeballed in a screenshot. */
    if (!strcasecmp(v, "sbstates")) {
        static const char* NAME[4] = { "normal", "hover", "pressed", "active" };
        int a;
        sb_poll();
        /* The draw path is what normally refreshes hover; do it here too so the verb
           reports the truth right after a `cursor` move, with no `shot` in between. */
        sb_pointer_refresh(fbw, fbh);
        printf("SBSTATE|repair=%s|sell=%s|map=%s",
               NAME[sb_frame_for(SBH_REPAIR, -1, g_sbRepairOn ? 1 : 0)],
               NAME[sb_frame_for(SBH_SELL,   -1, g_sbSellOn   ? 1 : 0)],
               NAME[sb_frame_for(SBH_MAP,    -1, 0)]);
        for (a = 0; a < 4; a++)
            printf("|arrow%d=%s", a,
                   NAME[sb_frame_for((a & 1) ? SBH_DOWN : SBH_UP, a / 2, 0)]);
        printf("\n");
        return true;
    }
    return false;
}

/* ==================================================================================== */

static bool sb_init(const SbHooks& h, const char* cameopack, const char* dospack)
{
    char err[256];
    g_sb = h;
    /* The N64 cameo pack is still loaded: FONT.SHP out of it is what the camera HUD and
       the debug overlays print with. The SIDEBAR's art now comes entirely from the DOS
       pack, which is the one the player sees. */
    sb_load_cameos(cameopack);

    /* The 640x480 HUD is OPTIONAL and OFF by default: it is signed off on look but not
       yet on behaviour, so the DOS bar stays the one the player gets until it is.
       CNC3D_HUD=new switches it on. A missing pack is not fatal; it just means the new
       path is unavailable and we say so once. */
    {
        const char* want = getenv("CNC3D_HUD");
        char h6err[256];
        std::string h6path(dospack ? dospack : "");
        size_t slash = h6path.find_last_of("/\\");
        h6path = (slash == std::string::npos ? std::string() : h6path.substr(0, slash + 1))
                 + "hud640.pack";
        g_h6Pack = hud640_load(h6path.c_str(), h6err, sizeof h6err);
        if (!g_h6Pack)
            fprintf(stderr, "sidebar: 640x480 HUD unavailable (%s); using the DOS bar\n", h6err);
        else
            fprintf(stderr, "sidebar: 640x480 HUD loaded, %d assets\n", g_h6Pack->nassets);
        /* The dial, if anything has set one; otherwise the environment; otherwise off. */
        if (g_hudNewWish < 0 && want && *want)
            g_hudNewWish = (strcmp(want, "new") == 0) ? 1 : 0;
        g_hudNew = (g_h6Pack && g_hudNewWish == 1);
        if (g_hudNew)
            fprintf(stderr, "sidebar: drawing the 640x480 HUD\n");
        /* The true-colour cameos, beside the HUD pack. Absent is not an error: every
           buildable he has not drawn keeps the palette path it had. */
        {
            std::string tdr(h6path);
            size_t sl = tdr.find_last_of("/\\");
            tdr = (sl == std::string::npos ? std::string() : tdr.substr(0, sl + 1))
                  + "cameos_tdr.pack";
            h6_tdr_load(tdr.c_str());
        }

        /* The C&C95 cameos, 61x45 to fill the opening, in the same DOSBAR01 container so
           db_pack_load reads them with no new code. Optional: without it every cell falls
           back to its DOS 32x24 cameo doubled, which is what shipped before. */
        h6path.replace(h6path.size() - strlen("hud640.pack"), std::string::npos,
                       "cameos95.pack");
        g_h6CamPack = db_pack_load(h6path.c_str(), h6err, sizeof h6err);
        if (g_h6CamPack)
            fprintf(stderr, "sidebar: C&C95 cameos loaded, %d shapes\n", g_h6CamPack->nshapes);
        else
            fprintf(stderr, "sidebar: no C&C95 cameo pack (%s); using the DOS cameos\n", h6err);
    }

    db_state_clear(&g_dbState);
    g_dbPack = db_pack_load(dospack, err, sizeof err);
    if (!g_dbPack) {
        fprintf(stderr, "sidebar: FATAL: %s\n", err);
        fprintf(stderr, "sidebar: the DOS sidebar cannot be drawn without dossidebar.pack; "
                        "pass --dospack PATH\n");
        return false;
    }
    fprintf(stderr, "sidebar: DOS pack %s (%d shapes, %d fonts)\n", dospack,
            g_dbPack->nshapes, g_dbPack->nfonts);
    return true;
}

/* Undo sb_init, exactly. Every glGenTextures in this file has its glDeleteTextures
   here, the DOS pack goes back to db_pack_free, and every latch that describes a
   MISSION rather than the program is reset: the build columns' scroll position, the
   pending placement, the repair/sell toggles and the radar's forced state. Leaving
   any of those set would carry mission one's sidebar into mission two.

   cur_free() must run BEFORE this: the cursor holds pointers into g_dbPack. */
static void sb_free(void)
{
    for (size_t i = 0; i < g_sbCameo.size(); i++)
        if (g_sbCameo[i].gl)
            glDeleteTextures(1, &g_sbCameo[i].gl);
    g_sbCameo.clear();
    if (g_sbFont.gl)
        glDeleteTextures(1, &g_sbFont.gl);
    memset(&g_sbFont, 0, sizeof(g_sbFont));
    g_sbHaveFont = false;
    if (g_dbTex)
        glDeleteTextures(1, &g_dbTex);
    g_dbTex = 0;

    if (g_dbPack)
        db_pack_free(g_dbPack);
    g_dbPack = NULL;
    db_state_clear(&g_dbState);

    memset(&g_sb, 0, sizeof(g_sb));
    g_sbState.valid = false;
    g_sbState.entry.clear();
    g_sbReady = false;
    g_sbPlacing = false;
    g_sbPlaceIdx = -1;
    g_sbPlaceProx.clear();
    g_sbPlaceClear.clear();
    g_sbHoverX = g_sbHoverY = -1;
    g_sbTop[0] = g_sbTop[1] = 0;
    g_sbRepairOn = g_sbSellOn = false;
    g_sbRadarShown = true;
    g_sbRadarForce = false;      /* see the declaration: faithful by default */
    g_sbFails = 0;
    /* THE ARMED SPECIAL IS A MISSION LATCH TOO, and it is the one that bites hardest.
       While the latch was an INDEX into g_sbState.entry a leftover was inert: both
       readers bailed on `index >= entry.size()` and the next mission starts with an
       empty entry list. The identity latch has no such guard, so an armed special left
       behind here boots the next mission with the targeting cursor painted over the
       whole map, and its first left click is eaten and fired at the clicked cell
       carrying the PREVIOUS mission's building type and id. */
    g_sbSuperOn = false;
    memset(g_sbSuperName, 0, sizeof(g_sbSuperName));
    g_sbSuperBType = g_sbSuperBId = 0;
}

#endif /* CNC_SIDEBAR_H */
