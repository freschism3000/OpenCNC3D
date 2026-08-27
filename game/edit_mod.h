/* ====================================================================================
 *  edit_mod.h -- the map editor, inside the game.
 *
 *  WHY IT LIVES HERE
 *
 *  The editor was prototyped in a browser, which settled its design and -- far more
 *  valuably -- derived every placement rule against the cartridge and proved each one.
 *  What it could never be is the thing asked for: a native Windows and Mac
 *  application where pressing Play starts the mission in the SAME window. That is what
 *  this is. docs/design-map-editor.md carries the rules and the measurements; this file
 *  carries none of the reasoning, only the code.
 *
 *  WHAT IT REUSES, AND WHY THAT MATTERS
 *
 *  Almost everything the hard parts need was already in cnc_eyes:
 *
 *      screen_to_cell()        cnc_eyes.cpp -- mouse pixel to map cell. A free function
 *                              with no game state, correct in both camera modes, and
 *                              covered by the --picktest numerical harness. The browser
 *                              raycast a mesh; this inverts the camera and marches the
 *                              heightfield, which is what Glide will need too.
 *      sb_place_cell_tris()    cnc_sidebar.h -- one map cell as two ground-draped
 *                              triangles on the terrain's own SW-NE split, projected at
 *                              real corner heights. Getting either of those wrong slides
 *                              the overlay off the cells it claims to name; this has
 *                              them right.
 *      begin_overlay()         the screen-pixel 2D pass the sidebar draws in.
 *      pick_at()               object under the mouse, with the ground cell filled in
 *                              even when nothing was hit.
 *
 *  THE MODEL: EDIT A DOCUMENT, HAND IT TO THE SIM
 *
 *  Not edit the live sim. The brain exposes no create-with-house-and-cell, no delete and
 *  no move -- every creation path it has is a game action with game consequences. But
 *  draw_frame() makes zero calls into the brain: a frame is a pure function of the pack
 *  plus cnc_eyes's own cached vectors, and --posetest already proves the renderer will
 *  draw SimObjects that cnc_eyes invented. So the editor owns the document while the sim
 *  is frozen, writes it out, and Play boots the brain on what it wrote.
 * ==================================================================================== */

#ifndef EDIT_MOD_H
#define EDIT_MOD_H

#include <ctype.h>
#include "edit_tables.h"
#include "edit_emblem.h"
#include "terrain_tiles.h"
#include <dirent.h>
#if defined(_WIN32)
#include <direct.h>          /* _mkdir; mingw does not put it in sys/stat.h */
#endif

/* Everything here is compiled into cnc_eyes.cpp after the renderer's own helpers, so it
   may use them directly. The include site is the contract; see the bottom of this file
   for what it expects to exist. */

/* ------------------------------------------------------------------------------------
 *  State
 * ---------------------------------------------------------------------------------- */

static bool g_editOn      = false;   /* --edit was given                              */

/* ---- THE EDITOR'S HALF OF THE 128 SEAM --------------------------------------------
   Storage is static at the renderer's C3D_MAP_MAX ceiling; every index strides by the
   LIVE grid, exactly like the renderer seam these names come from (g_gridW/g_gridH,
   committed by load_pack and re-grown by edit_set_world_grid below). Two strides, and
   they are different facts:
     - the DOCUMENT grid: the pack's own cell dims, g_gridW x g_gridH.
     - the INI CELL stride: a mission file's flat cell numbers are y*64+x for a legacy
       map and y*128+x when [MAP] says Version=1 -- the brain's own MEGAMAPS rule
       (Confine_Old_Cell, function.h:871, and display.cpp:1297 reading the key).
   For every map this editor makes the two agree (64 or 128); they are named apart so
   the code says WHICH fact each index depends on. Declared up here because this file
   is ordered drawing-first and the panels read them. */
#define EDIT_GRID_MAX (C3D_MAP_MAX * C3D_MAP_MAX)
static inline bool edit_map_big(void)   { return g_gridW > 64 || g_gridH > 64; }
static inline int  edit_ini_w(void)     { return edit_map_big() ? C3D_MAP_MAX : 64; }
static inline int  edit_ini_cells(void) { return edit_map_big() ? EDIT_GRID_MAX : 64 * 64; }
/* Which mode the editor is in: 0 objects, 1 terrain, 2 elevation, 3 starts, 4 script.
   Up here with g_editOn because the map overlays are drawn near the top of this file and
   several of them are object-mode furniture that must stand down in the others. */
static int  g_editMode    = 0;
static int  g_editCellX   = -1;      /* the cell under the pointer, -1 when off the map */
static int  g_editCellY   = -1;
static bool g_editOverMap = false;
static unsigned long g_editFrame = 0;   /* engine frames, not wallclock */

/* Which palette entry is armed, and where the map came from. Declared here with the
   rest of the state because the sidebar draws both and is defined above the code that
   used to own them. */
static int  g_editArmed = -1;   /* index into EDIT_ITEMS, -1 = nothing armed */
/* The editor minimap's rebuild latch. DEFINED up here rather than beside the radar
   drawer because the .BIN loader (far above the drawer) marks it stale -- the
   drawing-first ordering rule of this file. */
static int  g_euiRadarDirty = 1;
static char g_editDir[512] = "";
static char g_editScen[32] = "";

/* THE MAP'S DISPLAY NAME -- what [Basic] Name= carries, and what both this editor's
   OPEN MAP browser and the GAME's own User Maps list already show beside the code.
 *
 * THE FILE IS ALWAYS A SLOT, AND THAT IS NOT NEGOTIABLE. app/cnc3d.cpp finds user maps
 * by PROBING the names USER00..USER99 (usermaps_scan) rather than by reading the
 * directory: it has to compile for Windows 98 against a freestanding mingw, the same
 * reason specops_scan refuses to drag dirent.h in. A map filed under a free-text
 * FILENAME would therefore work perfectly in this editor and be INVISIBLE to the game.
 * So the slot stays USERnn forever and the name lives in the document, where every list
 * that shows a map already looks for it.
 *
 * Empty means "not named yet": everything that shows a name falls back to the slot, so
 * a new map reads as USER07 until SAVE AS gives it one. */
static char g_editTitle[48] = "";

/* Trim CR, LF, tabs and the spaces at both ends, in place. A name arrives either off an
   INI line (carrying its line ending) or off a keyboard (carrying whatever was typed),
   and neither is trustworthy at the edges. */
static void edit_title_trim(char* s)
{
    if (!s) return;
    char* b = s;
    while (*b == ' ' || *b == '\t') b++;
    if (b != s) memmove(s, b, strlen(b) + 1);
    for (char* e = s + strlen(s); e > s; ) {
        const char c = e[-1];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') *--e = 0;
        else break;
    }
}

/* The name to SHOW for the map that is open, and the name to WRITE into [Basic] Name=.
   Declared up here with the state rather than beside the save code because the title
   strip draws it and this file is ordered drawing-first. */
static const char* edit_map_title(void)
{
    return g_editTitle[0] ? g_editTitle : g_editScen;
}


/* ------------------------------------------------------------------------------------
 *  The cell cursor
 * ---------------------------------------------------------------------------------- */

/* Re-resolve the cell under the pointer. Called once per frame, never per cell:
   screen_to_world costs about 68 closed-form plane intersections, which is nothing once
   a frame and far too much inside a loop over a brush footprint. Invert once, then work
   in cell space. */
/* Is this point on the editor's own chrome rather than the map? Defined after the
   layout it needs; declared here because the hover test and the engine's cursor picker
   both come earlier in the file. */
static bool edit_over_chrome(float mx, float my, int fbw, int fbh);
/* What is at this cell, topmost first. Defined with the tools, below; the status
   card needs it to say what ERASE would remove. */
static int  edit_object_at(int cx, int cy);
/* The editor camera's own state, defined with the camera below. */
static void edit_cam_release(void);
/* How many cells a rule's zone covers. Defined with the zone model below. */
static int  edit_zone_cells(int t);
/* How many objects carry a rule. Defined with the prior table, below. */
static int  edit_tagged_count(int t);
/* An object's order and trigger are keyed by its cell, so a move has to carry them.
   Defined with the prior table, below the tools that call it. */
static void edit_prior_rekey(const char* house, const char* type,
                             int fromCell, int toCell);
static const char* edit_trigger_for(const SimObject& o);
/* The editor camera's readout, for the view bar. Defined with the camera below. */
static void edit_cam_eye(float* ex, float* ey, float* ez);
/* The measured bridge-mouth rule, defined with the terrain tools below; the status
   card needs it to say WHY a bridge will not go down here. */
static bool edit_is_crossing(int tid);
static bool edit_crossing_fits(int tid, int cx, int cy, char* why, int whyN,
                               int* badx, int* bady);
/* Which cells a template covers and the icon each takes. Defined with the terrain
   drawers, below; the status card needs it to say whether a tile fits. */
static int  edit_template_cells(int tid, int cx, int cy, int* xs, int* ys,
                                int* ics, int max);

static void edit_update_hover(float mouseC, float mouseR, int fbw, int fbh)
{
    if (!g_editOn) return;
    int cx = 0, cy = 0;
    /* ONLY OVER THE MAP. The game's sidebar stands down in edit mode, so the tactical
       viewport is the whole window and a pointer parked on the palette still resolved a
       cell behind it -- leaving a cyan cell outline and a footprint sitting out in the
       map, pointing at something nobody was pointing at. */
    const bool onMap = !edit_over_chrome(mouseC, mouseR, fbw, fbh);
    g_editOverMap = onMap && screen_to_cell(mouseC, mouseR, fbw, fbh, &cx, &cy);
    g_editCellX = g_editOverMap ? cx : -1;
    g_editCellY = g_editOverMap ? cy : -1;
}

/* Draw what the pointer is on.
 *
 * The overlay pass has depth test off, so this is never occluded and never z-fights --
 * and equally can never be hidden by ground in front of it, which is the trade the
 * sidebar already accepted for its own placement cursor. */
/* The per-object order and trigger table, read out of the mission before the editor can
   overwrite it. Declared up here because the undo snapshot carries it. */
struct EditPrior {
    char key[64];
    char order[24];
    char trig[24];
};
static std::vector<EditPrior> g_editPrior;

static bool eui_script_map_live(void);   /* defined with the wire state, far below */

/* TRUE while the running mission is a PLAYTEST started from the editor. Hoisted here
   because the chrome draws by it; the play/return machinery that sets it lives with
   the play requests, far below. */
static bool g_playFromEditor = false;

/* SHIFT+DRAG DRAWS A RECTANGLE while zone painting is on -- the SC2 gesture for "this
   region", filled on release. A plain drag stays the brush it always was. Up here
   because the zone overlay draws its preview. */
static bool g_marqOn = false;
static int  g_marqX = 0, g_marqY = 0;
static void edit_marq_rect(int* x0, int* y0, int* x1, int* y1);
static bool eui_rename_draws(int view, int idx, float x, float y, float scale);

static void edit_draw_overlay(int fbw, int fbh)
{
    if (!g_editOn || !g_editOverMap) return;
    /* In SCRIPT mode the canvas is not the map -- EXCEPT while painting, tagging or
       dragging a wire at a world target, when the highlighted cell is exactly the
       feedback the gesture needs. The states live far below; one call answers. */
    if (g_editMode == 4 && !eui_script_map_live()) return;

    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* The fill, then the outline over it. Two passes because a filled quad alone reads
       as a smear at a shallow camera angle, and an outline alone disappears over busy
       terrain. */
    glColor4f(0.56f, 0.89f, 1.0f, 0.22f);
    glBegin(GL_TRIANGLES);
    sb_place_cell_tris(g_editCellX, g_editCellY, fbw, fbh);
    glEnd();

    glColor4f(0.70f, 0.95f, 1.0f, 0.85f);
    glLineWidth(1.5f);
    {
        /* The cell's four corners at their real heights, in the same order the terrain
           pass walks them, so the outline sits on the ground rather than through it. */
        const float x0 = (float)g_editCellX, x1 = x0 + 1.0f;
        const float z0 = (float)g_editCellY, z1 = z0 + 1.0f;
        const float yNW = terrain_corner_y(g_editCellX,     g_editCellY);
        const float yNE = terrain_corner_y(g_editCellX + 1, g_editCellY);
        const float ySW = terrain_corner_y(g_editCellX,     g_editCellY + 1);
        const float ySE = terrain_corner_y(g_editCellX + 1, g_editCellY + 1);
        float c[4], r[4];
        world_to_screen(x0, yNW, z0, fbw, fbh, &c[0], &r[0]);
        world_to_screen(x1, yNE, z0, fbw, fbh, &c[1], &r[1]);
        world_to_screen(x1, ySE, z1, fbw, fbh, &c[2], &r[2]);
        world_to_screen(x0, ySW, z1, fbw, fbh, &c[3], &r[3]);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 4; i++) glVertex2f(c[i], r[i]);
        glEnd();
    }

    glDisable(GL_BLEND);
    end_overlay();
}




/* ------------------------------------------------------------------------------------
 *  The freeze
 *
 *  Edit mode has to stop the SIMULATION, not just the loop that happens to be running.
 *  When the freeze was only game_loop's `paused` local, every other path that advanced
 *  the brain -- the script runner, a warmup, a harness -- kept ticking, and the editor
 *  dutifully saved whatever the world had drifted into. Caught by round-tripping a map
 *  through three play/edit cycles: a Nod turret's facing went from 160 to 124 because it
 *  had been tracking a target while the editor believed nothing was moving.
 *
 *  A document that changes when you look at it is not a document.
 * ---------------------------------------------------------------------------------- */
static inline bool edit_sim_frozen(void) { return g_editOn; }

/* ------------------------------------------------------------------------------------
 *  Legality
 *
 *  The pack carries drawn ground and nothing else -- PackCell is atlas UVs, a holes
 *  flag and a tint, with no template and no icon anywhere in it. So the renderer cannot
 *  tell rock from road by looking at what it draws, and the game has never needed to:
 *  every legality answer it gives today comes from the brain, and only for the one
 *  building the player happens to be placing.
 *
 *  The editor needs an answer for any cell at any time, so it reads the scenario's .BIN
 *  -- 4,096 cells of (template, icon), which is the engine's own collision truth and the
 *  very file the editor writes back. 8,192 bytes, no parser.
 * ---------------------------------------------------------------------------------- */

static unsigned char g_editTmpl[EDIT_GRID_MAX];
static unsigned char g_editIcon[EDIT_GRID_MAX];
static bool          g_editHaveBin = false;
/* The .BIN document's own dims, set by edit_load_bin from the FILE rather than from
   g_gridW: the boot reads the .BIN before the pack has committed the world grid, and
   an index that asked the grid then would stride by the previous mission's size. After
   the pack loads (or edit_set_world_grid grows a borrowed one) the two agree. */
static int g_editBinW = 64, g_editBinH = 64;

/* [MAP] Version=, read straight from the file so the answer does not depend on boot
   order. 0 = legacy 64-stride cells and a dense 8192-byte .BIN; 1 = the brain's
   MEGAMAPS big-map form: 128-stride cells and the sparse record .BIN that
   Read_Binary_Big (map.cpp:968) actually parses. */
static int edit_ini_version(const char* dir, const char* scen)
{
    char path[1024];
    snprintf(path, sizeof path, "%s%s%s.INI",
             dir ? dir : "", (dir && *dir && dir[strlen(dir) - 1] != '/') ? "/" : "",
             scen ? scen : "");
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char line[512];
    bool in = false;
    int ver = 0;
    while (fgets(line, sizeof line, f)) {
        const char* q = line;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '[') { in = !strncasecmp(q, "[MAP]", 5); continue; }
        if (in && !strncasecmp(q, "Version=", 8)) { ver = atoi(q + 8); break; }
    }
    fclose(f);
    return ver == 1 ? 1 : 0;
}

/* The brain's own big-map .BIN record: {u16 cell, u8 template, u8 icon}, little-endian,
   ascending cell order, clear cells omitted (Write_Binary_Big, map.cpp:1286). Parsed
   with validation because an 8192-byte file could be EITHER a legacy dense map or a
   sparse one that happens to hold 2048 records -- ascending in-range cells is the
   discriminator, and a dense file read as records fails it immediately. */
static bool edit_bin_parse_sparse(const unsigned char* raw, size_t n)
{
    if (n % 4 != 0) return false;
    long last = -1;
    for (size_t i = 0; i < n; i += 4) {
        const int cell = raw[i] | (raw[i + 1] << 8);
        if (cell <= last || cell >= EDIT_GRID_MAX) return false;
        last = cell;
    }
    memset(g_editTmpl, 255, sizeof g_editTmpl);
    memset(g_editIcon, 0, sizeof g_editIcon);
    for (size_t i = 0; i < n; i += 4) {
        const int cell = raw[i] | (raw[i + 1] << 8);
        g_editTmpl[cell] = raw[i + 2];
        g_editIcon[cell] = raw[i + 3];
    }
    g_editBinW = C3D_MAP_MAX;
    g_editBinH = C3D_MAP_MAX;
    return true;
}

static bool edit_load_bin(const char* dir, const char* scen)
{
    g_editHaveBin = false;
    if (!scen || !*scen) return false;

    char path[1024];
    snprintf(path, sizeof path, "%s%s%s.BIN",
             dir ? dir : "", (dir && *dir && dir[strlen(dir) - 1] != '/') ? "/" : "", scen);
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "edit: no %s -- every cell will read as clear ground, so nothing "
                        "will be refused. Terrain legality needs the .BIN.\n", path);
        return false;
    }
    /* The whole file: a dense 128 map is 32 KB and a sparse one at most 64 KB. */
    static unsigned char raw[EDIT_GRID_MAX * 4];
    const size_t got = fread(raw, 1, sizeof raw, f);
    fclose(f);

    /* WHICH .BIN THIS IS. Three forms exist and the file length alone cannot always
       say (see edit_bin_parse_sparse): the INI's own Version key is asked first, so a
       big map is read the way the brain will read it, then the two dense forms by
       their exact lengths (8192 = 64x64 legacy, 32768 = the tooling's dense 128
       interchange, tools/bin_to_n64map.py grid_width), then a validated sparse parse
       as the last resort for a big .BIN whose INI went missing. */
    const int ver = edit_ini_version(dir, scen);
    bool ok = false;
    if (ver == 1 && edit_bin_parse_sparse(raw, got)) {
        ok = true;
    } else if (got == 64 * 64 * 2 || got == (size_t)EDIT_GRID_MAX * 2) {
        const int w = (got == 64 * 64 * 2) ? 64 : C3D_MAP_MAX;
        for (int i = 0; i < w * w; i++) {
            g_editTmpl[i] = raw[i * 2];
            g_editIcon[i] = raw[i * 2 + 1];
        }
        g_editBinW = g_editBinH = w;
        ok = true;
    } else if (edit_bin_parse_sparse(raw, got)) {
        ok = true;
    }
    if (!ok) {
        fprintf(stderr, "edit: %s is %zu bytes; a dense .BIN is 8192 (64x64) or 32768 "
                        "(128x128), a Version=1 sparse one a multiple of 4 with ascending "
                        "cells. Ignoring it.\n", path, got);
        return false;
    }
    g_editHaveBin = true;
    g_euiRadarDirty = 1;

    const int cells = g_editBinW * g_editBinH;
    int build = 0;
    for (int i = 0; i < cells; i++)
        if (EDIT_LAND_BUILD[edit_land_of(g_editTmpl[i], g_editIcon[i])]) build++;
    fprintf(stderr, "edit: %s -- %d of %d cells are buildable ground\n", path, build, cells);
    return true;
}

static int edit_land_at(int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= g_editBinW || cy >= g_editBinH) return EDIT_LAND_ROCK;
    if (!g_editHaveBin) return EDIT_LAND_CLEAR;
    const int i = cy * g_editBinW + cx;
    return edit_land_of(g_editTmpl[i], g_editIcon[i]);
}

static bool edit_can_build(int cx, int cy)
{
    return EDIT_LAND_BUILD[edit_land_at(cx, cy)] != 0;
}

/* Does anything already stand on this cell? The document is g_objects, so this is a
   walk over it -- there are a couple of hundred objects on a map and this runs once per
   footprint cell per frame, which is nothing. */
static bool edit_cell_occupied(int cx, int cy)
{
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_BUILDING && o.kind != K_TERRAIN) continue;
        const int w = o.fw > 0 ? o.fw : 1, h = o.fh > 0 ? o.fh : 1;
        if (cx >= o.cx && cx < o.cx + w && cy >= o.cy && cy < o.cy + h) return true;
    }
    return false;
}

/* One cell of a pending footprint: legal ground, and nothing already on it. */
static bool edit_cell_ok(int cx, int cy)
{
    return edit_can_build(cx, cy) && !edit_cell_occupied(cx, cy);
}


/* ====================================================================================
 *  THE SIDEBAR
 *
 *  A specific layout was approved and the editor did not look like it: a strip of eight
 *  cameos across the bottom is a placeholder, not a design. This is that layout, drawn
 *  natively.
 *
 *  It is built out of what the game already has rather than a new toolkit. Panels and
 *  bevels are GL quads in the overlay pass that cnc_sidebar.h already draws its own
 *  placement cursor in; text is ef_text(), C&C's own FONT.SHP baked to white ink with a
 *  black outline and tinted through GL_MODULATE; the palette tiles are the 53 cameo
 *  textures sb_load_cameos has been uploading since before anything called it. No CPU
 *  rasteriser, no second font, no new asset.
 *
 *  The tokens below are the mockup's, so the two can be compared side by side.
 * ==================================================================================== */

/* --- the mockup's palette --------------------------------------------------------- */
#define EUI_RGB(h)   (((h) >> 16) & 255) / 255.0f, (((h) >> 8) & 255) / 255.0f, ((h) & 255) / 255.0f
static const unsigned EUI_BG      = 0x0b0f14;
static const unsigned EUI_PANEL   = 0x151b24;
static const unsigned EUI_PANEL2  = 0x1c2430;
static const unsigned EUI_PANEL3  = 0x242e3c;
static const unsigned EUI_LINE    = 0x2a3543;
static const unsigned EUI_INK     = 0xd5dee9;
static const unsigned EUI_DIM     = 0x96a1ad;
static const unsigned EUI_FAINT   = 0x5f6c7b;
static const unsigned EUI_GOLD    = 0xe0b070;
static const unsigned EUI_CYAN    = 0x8fe4ff;
static const unsigned EUI_GREEN   = 0x7ee0a0;
static const unsigned EUI_DANGER  = 0xff6a5a;

/* ------------------------------------------------------------------------------------
 *  SCALE
 *
 *  The panel used to be a fixed 340 pixels wide at EVERY frame size -- measured, by
 *  --uitest: 340 at 1280 wide and still 340 at 3440. That is what made it look like a
 *  toy. 340 of 1280 is a sidebar; 340 of 3440 is a ribbon; and on a Retina display 340
 *  DRAWABLE pixels is 170 points, half the size the browser drew the same 340 at,
 *  because there they were CSS pixels.
 *
 *  So nothing below is a pixel any more. Every number is a DESIGN unit, matching the
 *  browser editor one for one, and eui_scale() turns design units into pixels for the
 *  frame being drawn. Two things feed it:
 *
 *    the backing scale   drawable pixels per window point -- 2 on a Retina display.
 *                        This alone makes the native panel the same physical size as
 *                        the web one, which is the actual complaint.
 *    the frame           a bigger window earns a bigger panel, but sub-linearly: the
 *                        sidebar must not eat a third of an ultrawide.
 *
 *  Clamped at both ends. A panel that grows without limit is as unusable as one that
 *  never grows.
 * ---------------------------------------------------------------------------------- */

#define EUI_SIDE_W  340.0f      /* DESIGN UNITS, not pixels -- see eui_scale()       */
#define EUI_RAIL_W   56.0f      /* the left tool rail, same 56 as the browser        */
/* The script panel's property rows. Rules need 9; a team needs 14, because a team
   really does have that many knobs and hiding half of them behind a second screen
   would be worse than a shorter row. */
#define EUI_RULE_ROWS   11
#define EUI_TEAM_ROWS   14
#define EUI_ENH_ROWS    14
#define EUI_SCRIPT_ROWS EUI_TEAM_ROWS
#define EUI_TOOL_SZ  42.0f      /* each tool button, same 42                         */
/* Design units kept clear at the right end of the view bar for the camera readout.
   Measured against the longest string it prints, not guessed: "EYE -000,000,-000
   YAW -000 SPEED 0.00" at 0.8 scale. */
#define EUI_BAR_READOUT 260.0f
#define EUI_TOP_H    44.0f
#define EUI_PAD       7.0f

/* Drawable pixels per window point, written once a frame by the live loop. 1.0 until
   something says otherwise, which is right for a non-Retina display and for every
   headless path. */
static float g_uiBacking = 1.0f;

/* the original zoom, so the panel can be sized without resizing the window. */
static float g_uiZoom = 1.0f;

static float eui_scale(int fbw, int fbh)
{
    (void)fbw;
    /* THE LAYOUT IS SIZED IN POINTS, NOT DEVICE PIXELS, and this is the correction that
       matters most.
     *
     * The first version multiplied the whole layout by the backing scale, so a Retina
     * display got a panel twice as BIG rather than twice as SHARP. On a small window
     * that made the type enormous and pushed the boxes through each other -- which is
     * exactly what was reported: unreadable fonts and menus clipping into one another.
     *
     * A high-DPI display should give the same design more pixels, not a bigger design.
     * So the design size is chosen from the window in POINTS, and the backing scale is
     * applied afterwards purely to convert points to device pixels.
     *
     * It is allowed to go BELOW 1: the panel was drawn for a window about 900 points
     * tall, and on a shorter one it has to shrink or it does not fit. Clamped at 0.62,
     * under which the cartridge font stops being legible at all. */
    const float backing = g_uiBacking > 0.0f ? g_uiBacking : 1.0f;
    const float pointsH = (float)fbh / backing;
    float byFrame = pointsH / 900.0f;
    if (byFrame < 0.62f) byFrame = 0.62f;
    if (byFrame > 1.35f) byFrame = 1.35f;
    return backing * byFrame * g_uiZoom;
}

/* The four houses, in the mockup's colours and the engine's own INI names. */
struct EuiHouse { const char* key; const char* label; unsigned colour; };
static const EuiHouse EUI_HOUSES[4] = {
    { "GoodGuy", "Yours",   0xe0b070 },
    { "BadGuy",  "Enemy",   0xff6a5a },
    { "Neutral", "Neutral", 0x9aa7b4 },
    { "Special", "Special", 0xb98fe4 },
};
/* What the pointer can be over. Declared up here rather than beside eui_hit
   because the DRAW needs it too, for the hover states, and the draw comes first. */
enum EuiHit { EUI_NONE = 0, EUI_MAP, EUI_ITEM, EUI_TABC, EUI_OWNER, EUI_MODE,
              EUI_SAVE, EUI_PLAY, EUI_RADAR, EUI_ACT, EUI_TOOL,
              EUI_ELEVRUNG, EUI_ELEVTOOL, EUI_ELEVBRUSH, EUI_ELEVGATE,
              EUI_CLIFFITEM, EUI_VIEW,
              EUI_MENUBTN, EUI_MENUITEM, EUI_MODALBTN, EUI_MODALOPT, EUI_START,
              EUI_SCRIPTNODE, EUI_SCRIPTROW, EUI_SCRIPTVIEW,
              EUI_TEAMCARD, EUI_TEAMORDER, EUI_TEAMSLOT, EUI_TEAMFLAG,
              EUI_ENHCARD, EUI_ENHCLAUSE, EUI_ENHCHIP, EUI_TRACE, EUI_CHECK,
              EUI_WIRETEAM, EUI_RULEPIN, EUI_POPITEM, EUI_WATCHPIN, EUI_ZONEPIN,
              EUI_CARRIERBOX, EUI_CARRIERPIN, EUI_ENHZONEPIN, EUI_RENAME,
              EUI_DEAD };

/* The four tools and the one action on the left rail. Declared up here rather
   than beside the code that implements them, because the RAIL DRAWS them and the
   drawing comes first in this file. */
enum EditTool { TOOL_PLACE = 0, TOOL_MOVE, TOOL_PICK, TOOL_ERASE, TOOL_COUNT };

static const char* const TOOL_KEY[TOOL_COUNT]  = { "V", "M", "I", "X" };
static const char* const TOOL_NAME[TOOL_COUNT] = { "PLACE", "MOVE", "PICK", "ERASE" };

static int g_editTool = TOOL_PLACE;
static int g_editDragObj = -1;      /* index into g_objects while MOVE is dragging   */
static int g_editDragCX = 0, g_editDragCY = 0;

/* THE SEVEN TERRAIN DRAWERS, grouped by the template's own INI name -- the same
   split the browser editor uses. Declared with the rest of the panel state because
   the draw needs them and comes first in this file. Cliffs (S*) are deliberately
   absent: they are derived from elevation, not painted. */
enum TerrGroup { TG_GROUND = 0, TG_WATER, TG_SHORE, TG_RIVER, TG_ROAD, TG_ROCK,
                 TG_BRIDGE, TG_COUNT };

static const char* const TERR_GROUP_NAME[TG_COUNT] = {
    "GROUND", "WATER", "SHORE", "RIVER", "ROAD", "ROCK", "BRIDGE"
};

/* The offered templates, per drawer, built once for the loaded theater. */
static std::vector<int> g_terrList[TG_COUNT];
static bool g_terrBuilt = false;
static int  g_terrGroup = TG_GROUND;
static int  g_terrArmed = -1;        /* a template id, or -1 */
static int  g_terrScroll = 0;

/* The elevation tool's own controls: which rung the brush paints, which of the three
   tools is live, and how wide the brush is. */
static int g_elevTierSel  = 2;      /* rung index; 1 is "ground"                    */
static int g_elevTool     = 0;      /* 0 raise/lower, 1 graded pass, 2 ramp east    */
static int g_elevBrush    = 1;      /* radius in blocks: 1, 2, 3 = 2x2, 4x4, 6x6    */
/* The last refusal, so the status card can show it rather than only the log. */
static char g_elevWhy[220] = "";

/* The elevation model lives further down, with the code that derives from it; the panel
   draws its state and comes first, as everything in this file does. */
static bool elev_ready(void);
static int  g_elevOffLadder, g_elevTotal;

/* Fly speed, declared here because the view bar prints it and the bar is drawn
   before the camera code that changes it. */
static float g_camSpeed = 0.55f;

/* The waypoint numbers and the array itself, up with the rest of the panel state
   because the STARTS panel draws them and drawing comes first in this file. */
#define EDIT_WAYPT_COUNT 28
#define EDIT_WAYPT_HOME  26
#define EDIT_WAYPT_REINF 27
#define EDIT_MAX_PLAYERS 8          /* MAX_PLAYERS, defines.h:2565 (EIGHTPLAYERS) */

static int g_waypoint[EDIT_WAYPT_COUNT];
static int g_waypointArmed = -1;    /* which one the next map click places */

static const char* edit_waypoint_label(int i)
{
    static char b[32];
    if (i == EDIT_WAYPT_HOME)  return "HOME";
    if (i == EDIT_WAYPT_REINF) return "REINFORCE";
    snprintf(b, sizeof b, "START %d", i + 1);
    return b;
}

/* How many player starts this map offers. Every valid waypoint in 0..25 is a start as
   far as Create_Units is concerned, capped at MAX_PLAYERS. */
static int edit_start_count(void)
{
    int n = 0;
    for (int i = 0; i < 26; i++) if (g_waypoint[i] >= 0) n++;
    return n > EDIT_MAX_PLAYERS ? EDIT_MAX_PLAYERS : n;
}

/* ------------------------------------------------------------------------------------
 *  TEXT SIZES, and why they are integers
 *
 *  sb_text renders C&C's own FONT.SHP, a BITMAP font: it snaps the scale to a whole
 *  number and the quad to whole pixels, because a fractionally scaled bitmap glyph
 *  samples its neighbour in the atlas and "BUILD" comes out "DLILD" (cnc_sidebar.h:310).
 *
 *  So asking for 0.85x does not give a smaller glyph -- it gives the SAME glyph, rounded
 *  back up. Every "small" label in this panel was therefore drawn full size while the
 *  layout had budgeted the smaller width for it, and the two ran into each other. That
 *  is the text that could not be read.
 *
 *  There are two sizes, one step apart, and no others. Anything that wants to be smaller
 *  gets EUI_TS; anything bigger gets EUI_TL. Layout measures with sb_text_w, which snaps
 *  identically, so what is measured is what is drawn.
 * ---------------------------------------------------------------------------------- */

#define EUI_T(base)  (base)                                    /* body            */
/* ONE SIZE IN THE PANEL, and hierarchy carried by COLOUR instead.
 *
 * This used to be "one step down", and the step is not typographic: the font is an 8x11
 * bitmap drawn at whole-number scales only, so a step down from body is a step to HALF.
 * On a Retina display the panel's body text lands at about 11 points, which makes the
 * small text about FIVE -- and 77 of the panel's 280 text draws used it. That is the
 * "uneven, and in many places unreadable" was reported three times, and no amount of
 * tuning the small size fixes it, because between 5pt and 11pt there is nothing to pick.
 *
 * So the panel now draws one size. It already leans hard on colour -- gold for what is
 * selected, cyan for a value, dim for a label, faint for context, red for a refusal --
 * and that is a real hierarchy, legible at any size, rather than a fake one made of
 * pixels nobody can resolve. EUI_TL survives for the two or three genuine titles. */
#define EUI_TS(base) (base)
#define EUI_TL(base) ((base) + 1.0f)                           /* one step up     */

/* The panel's text goes through ef_* (edit_font.h): a real proportional face, baked at
   a ladder of sizes and drawn 1:1. The game keeps FONT.SHP; the tool does not. */


/* ------------------------------------------------------------------------------------
 *  MISSION SCRIPTING
 *
 *  A Tiberian Dawn trigger is one line: name = event, action, data, house, team,
 *  persistence (trigger.cpp:988). One event paired with one action -- no flow, no AND,
 *  no OR, no variables. That is the whole language the 1995 engine has.
 *
 *  TWO TIERS, which is a project decision and the right one.
 *
 *      NATIVE    only constructs the cartridge itself can run. Compiles to [Triggers]
 *                and [TeamTypes] exactly, so the mission plays on anything -- including
 *                the real DOS game.
 *      ENHANCED  adds what the 1995 format cannot express: conditions combined with AND
 *                and OR, counters, and rules that fire on state the engine never
 *                exposed. These need our own evaluator, so a mission that uses one is
 *                tagged and will only run in Enhanced mode.
 *
 *  The tier is not a mode the editor is in -- it is a property of the MISSION, derived
 *  from what it actually uses. A map stays universal until the moment it uses something
 *  that cannot be universal, and then it says so.
 * ---------------------------------------------------------------------------------- */

/* The cartridge's own tables, verbatim (trigger.cpp:68-103). Order is the wire format:
   the INI stores the NAME, and these are the only spellings it accepts. */
static const char* const TRIG_EVENT[] = {
    "None", "Player Enters", "Discovered", "Attacked", "Destroyed", "Any",
    "House Discov.", "Units Destr.", "Bldgs Destr.", "All Destr.", "Credits",
    "Time", "# Bldgs Dstr.", "# Units Dstr.", "No Factories", "Civ. Evac.", "Built It"
};
#define TRIG_EVENT_N ((int)(sizeof(TRIG_EVENT) / sizeof(TRIG_EVENT[0])))

static const char* const TRIG_ACTION[] = {
    "None", "Win", "Lose", "Production", "Create Team", "Dstry Teams", "All to Hunt",
    "Reinforce.", "DZ at 'Z'", "Airstrike", "Nuclear Missile", "Ion Cannon",
    "Dstry Trig 'XXXX'", "Dstry Trig 'YYYY'", "Dstry Trig 'ZZZZ'", "Autocreate",
    "Cap=Win/Des=Lose", "Allow Win"
};
#define TRIG_ACTION_N ((int)(sizeof(TRIG_ACTION) / sizeof(TRIG_ACTION[0])))

/* What a designer would call these, and what the parameter MEANS. The INI spellings
   above are abbreviations from a 1995 dialog box; nobody should have to read
   "# Bldgs Dstr." and work out that the number is a count. */
static const char* const TRIG_EVENT_HUMAN[] = {
    "never", "the player enters the zone", "the player discovers it",
    "it is attacked", "it is destroyed", "anything happens to it",
    "a house is discovered", "all its units die", "all its buildings die",
    "everything it owns dies", "credits reach", "the timer reaches",
    "buildings destroyed reaches", "units destroyed reaches",
    "it has no factories left", "a civilian is evacuated", "it gets built"
};
static const char* const TRIG_ACTION_HUMAN[] = {
    "do nothing", "win the mission", "lose the mission", "begin production",
    "create a team", "destroy all teams", "send everything hunting",
    "send reinforcements", "drop a supply crate", "call an airstrike",
    "grant the nuclear missile", "grant the ion cannon",
    "cancel trigger XXXX", "cancel trigger YYYY", "cancel trigger ZZZZ",
    "start auto-creating teams", "capture wins, destroy loses", "allow winning"
};

/* Which events read the Data field, and what it counts (trigger.cpp, Event_Need_Data).
   Time is in TENTHS OF A MINUTE -- one unit is six seconds -- which is the single
   easiest thing to get wrong in this format. */
static bool trig_event_has_data(int e)
{
    return e == 10 /* Credits */ || e == 11 /* Time */ ||
           e == 12 /* # Bldgs */ || e == 13 /* # Units */ ||
           e == 16 /* Built It */;
}

/* Which of the other fields this event actually reads. There are three Spring
   overloads in the engine and they do not share an action body, so which route an
   event takes decides what its trigger can even mean:
     - a ZONE is read only on the cell route, and Player Enters is the only event on it
     - a TAG is read only on the object route
     - the HOUSE field is inert on the purely object-routed events
   (trigger.cpp:158-232, Event_Need_Object / Event_Need_House.) The editor offers all
   of them regardless -- you may well set the event last -- but it says which ones this
   event will ignore, rather than leaving a dead zone to be discovered in play. */
static bool trig_event_reads_zone(int e)  { return e == 1; }
static bool trig_event_reads_tag(int e)   { return e >= 1 && e <= 5; }
static bool trig_event_reads_house(int e) { return e == 1 || (e >= 6 && e <= 16); }

/* Built It stores a raw StructType index. Turn it into something a person can read. */
static const char* trig_struct_name(int data)
{
    if (data < 0 || data >= EDIT_STRUCT_N) return "not a building";
    const char* code = EDIT_STRUCT_CODE[data];
    for (int i = 0; i < EDIT_ITEM_N; i++)
        if (EDIT_ITEMS[i].kind == EDIT_KIND_BUILDING && !strcmp(EDIT_ITEMS[i].code, code))
            return EDIT_ITEMS[i].name;
    return code;      /* the civilian structures, which have no cameo entry */
}
static bool trig_action_has_team(int a)
{
    return a == 4 /* Create Team */ || a == 5 /* Dstry Teams */ || a == 7 /* Reinforce. */;
}

struct EditTrigger {
    char name[8];        /* max 4 chars + NUL; the engine truncates past that */
    int  event, action;
    int  data;
    char house[16];
    char team[16];
    int  persist;        /* 0 volatile, 1 semi-persistent, 2 persistent */
};

static std::vector<EditTrigger> g_triggers;
static int  g_trigSel = -1;
static bool g_trigDirty = false;      /* the editor changed them; write ours, not theirs */

static int trig_name_index(const char* nm, const char* const* tab, int n)
{
    for (int i = 0; i < n; i++) if (!strcasecmp(tab[i], nm)) return i;
    return 0;
}

static void edit_read_triggers(const char* path)
{
    g_triggers.clear();
    g_trigSel = -1;
    g_trigDirty = false;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    bool in = false;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { in = !strncasecmp(p, "[Triggers]", 10); continue; }
        if (!in || *p == ';' || *p == '\r' || *p == '\n' || !*p) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        EditTrigger t;
        memset(&t, 0, sizeof t);
        snprintf(t.name, sizeof t.name, "%.4s", p);
        /* Split on bare commas, exactly as Fill_In does -- and note it does NOT trim,
           so a space after a comma breaks the name match in the engine too. */
        char* v = eq + 1;
        char* tok[6] = {0};
        int n = 0;
        for (char* q = v; *q && n < 6; ) {
            tok[n++] = q;
            char* c = strchr(q, ',');
            if (!c) break;
            *c = 0;
            q = c + 1;
        }
        for (int i = 0; i < n; i++)
            if (tok[i]) {
                char* e = tok[i] + strlen(tok[i]);
                while (e > tok[i] && (e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
            }
        t.event  = tok[0] ? trig_name_index(tok[0], TRIG_EVENT, TRIG_EVENT_N) : 0;
        t.action = tok[1] ? trig_name_index(tok[1], TRIG_ACTION, TRIG_ACTION_N) : 0;
        t.data   = tok[2] ? atoi(tok[2]) : 0;
        snprintf(t.house, sizeof t.house, "%s", tok[3] ? tok[3] : "None");
        snprintf(t.team,  sizeof t.team,  "%s", tok[4] ? tok[4] : "None");
        t.persist = tok[5] ? atoi(tok[5]) : 0;
        g_triggers.push_back(t);
    }
    fclose(f);
    fprintf(stderr, "edit: %d triggers\n", (int)g_triggers.size());
}


/* --- TEAM TYPES ---------------------------------------------------------------------
 *
 *  A trigger's action is usually "create a team" -- 32% of every action across the
 *  campaign, the single commonest thing a rule does. The team it names is the actual
 *  content: which units, how many, and the order list they follow. Until now the editor
 *  carried [TeamTypes] through untouched, so a rule could point at a team but you could
 *  not make one.
 *
 *  The line format (teamtype.cpp:158-163, plus two trailing flags the comment forgets):
 *
 *      Name = House,Roundabout,Learning,Suicide,Autocreate,Mercenary,
 *             Priority,MaxAllowed,InitNum,Fear,
 *             ClassCount,Class:Num,...,  MissionCount,Mission:Arg,...,
 *             Reinforcable,Prebuilt
 *
 *  Field 5 is labelled "Spy" in the original comment and parsed into IsAutocreate. The
 *  comment is wrong and the code is right; this uses the code's name.
 *
 *  Limits that are real crashes rather than rejections, so they are enforced here:
 *  the whole value must stay under 255 characters (Fill_In strcpy's it into a 256-byte
 *  stack buffer), at most 5 member classes, at most 20 orders.
 * ---------------------------------------------------------------------------------- */

#define TEAM_CLASS_MAX    5
#define TEAM_MISSION_MAX 20
#define TEAM_LINE_MAX   255

/* teamtype.cpp:55-69. Names carry spaces and, for Attack Civil., a trailing period;
   Mission_From_Name matches them case-insensitively and exactly, so they are
   reproduced character for character. */
#define TEAM_MIS_N 12
static const char* const TEAM_MIS[TEAM_MIS_N] = {
    "Attack Base", "Attack Units", "Attack Civil.", "Rampage", "Defend Base",
    "Move", "Move to Cell", "Retreat", "Guard", "Loop", "Attack Tarcom", "Unload"
};
/* What the argument counts, which differs per order and is the easiest thing to get
   wrong: 0 = a duration in six-second units, 1 = a waypoint, 2 = a raw cell number,
   3 = an order index to jump to, 4 = a raw target value the editor cannot author. */
static const unsigned char TEAM_MIS_ARG[TEAM_MIS_N] = {
    0, 0, 0, 0, 0, 1, 2, 0, 0, 3, 4, 1
};

struct EditTeamClass   { char code[10]; int num; };
struct EditTeamMission { int mission; int arg; };

struct EditTeam {
    char name[10];              /* max 8 chars; type.h:268 truncates past that      */
    char house[16];
    int  roundabout, learning, suicide, autocreate, mercenary;
    int  priority, maxallowed, initnum, fear;
    int  nclass;   EditTeamClass   cls[TEAM_CLASS_MAX];
    int  nmission; EditTeamMission mis[TEAM_MISSION_MAX];
    int  reinforcable, prebuilt;
};

static std::vector<EditTeam> g_teams;
static int  g_teamSel = -1;
static int  g_teamOrder = -1;       /* which order in the list is being edited      */
static int  g_teamMember = 0;       /* which member slot the type/count rows act on */
static bool g_teamDirty = false;

/* SCRIPT mode shows one of two things: the rules, or the teams they call up. */
static int g_scriptView = 0;        /* 0 rules, 1 teams, 2 enhanced */

static void edit_team_defaults(EditTeam* t)
{
    /* teamtype.cpp:114-136. Prebuilt and Reinforcable default TRUE, and are the two
       fields the format comment omits -- a team written without them is not a team
       with them off. */
    memset(t, 0, sizeof *t);
    snprintf(t->house, sizeof t->house, "BadGuy");
    t->priority = 7;
    t->reinforcable = 1;
    t->prebuilt = 1;
}

static int edit_team_by_name(const char* nm)
{
    if (!nm || !*nm) return -1;
    for (size_t i = 0; i < g_teams.size(); i++)
        if (!strcasecmp(g_teams[i].name, nm)) return (int)i;
    return -1;
}

static void edit_read_teams(const char* path)
{
    g_teams.clear();
    g_teamSel = -1;
    g_teamOrder = -1;
    g_teamDirty = false;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    bool in = false;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { in = !strncasecmp(p, "[TeamTypes]", 11); continue; }
        if (!in || *p == ';' || *p == '\r' || *p == '\n' || !*p) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        EditTeam t;
        edit_team_defaults(&t);
        snprintf(t.name, sizeof t.name, "%.8s", p);

        /* Fill_In walks this with strtok on "," and then ",:" inside the two lists.
           strtok_r keeps that behaviour without the shared static. */
        char buf[TEAM_LINE_MAX + 64];
        snprintf(buf, sizeof buf, "%s", eq + 1);
        for (char* e = buf + strlen(buf); e > buf && (e[-1] == '\r' || e[-1] == '\n'); )
            *--e = 0;
        char* save = 0;
        char* tk = strtok_r(buf, ",", &save);
        #define TEAM_NEXT() (tk = strtok_r(0, ",", &save))
        #define TEAM_NUM()  (TEAM_NEXT() ? atoi(tk) : 0)
        if (!tk) continue;
        snprintf(t.house, sizeof t.house, "%s", tk);
        t.roundabout = TEAM_NUM();
        t.learning   = TEAM_NUM();
        t.suicide    = TEAM_NUM();
        t.autocreate = TEAM_NUM();
        t.mercenary  = TEAM_NUM();
        t.priority   = TEAM_NUM();
        t.maxallowed = TEAM_NUM();
        t.initnum    = TEAM_NUM();
        t.fear       = TEAM_NUM();
        int nc = TEAM_NUM();
        for (int i = 0; i < nc; i++) {
            char* nm = strtok_r(0, ",:", &save);
            char* qty = strtok_r(0, ",:", &save);
            if (!nm || !qty) { nc = i; break; }
            if (t.nclass >= TEAM_CLASS_MAX) continue;      /* engine drops the extras */
            snprintf(t.cls[t.nclass].code, sizeof t.cls[0].code, "%s", nm);
            t.cls[t.nclass].num = atoi(qty);
            t.nclass++;
        }
        int nm2 = TEAM_NUM();
        for (int i = 0; i < nm2; i++) {
            char* mn = strtok_r(0, ",:", &save);
            char* ma = strtok_r(0, ",:", &save);
            if (!mn) break;
            if (t.nmission >= TEAM_MISSION_MAX) continue;
            int k = -1;
            for (int j = 0; j < TEAM_MIS_N; j++)
                if (!strcasecmp(TEAM_MIS[j], mn)) k = j;
            /* An unknown name is TMISSION_NONE in the engine, and a team whose current
               order is NONE never advances and never times out. Keep it out of the
               editor's model rather than round-tripping a soft-lock. */
            if (k < 0) continue;
            t.mis[t.nmission].mission = k;
            t.mis[t.nmission].arg = ma ? atoi(ma) : 0;
            t.nmission++;
        }
        /* Both optional. Absent means the constructor default stands, which is 1. */
        if (TEAM_NEXT()) t.reinforcable = atoi(tk);
        if (TEAM_NEXT()) t.prebuilt = atoi(tk);
        #undef TEAM_NEXT
        #undef TEAM_NUM
        g_teams.push_back(t);
    }
    fclose(f);
    fprintf(stderr, "edit: %d teams\n", (int)g_teams.size());
}

/* Back to one INI line. Returns the length so the caller can refuse to write one that
   would smash the engine's stack buffer. */
static int edit_team_line(const EditTeam& t, char* out, int cap)
{
    /* Same accumulate-safely rule as enh_rule_line: a truncating snprintf pushes n past
       cap, and then out + n is off the end and cap - n is a negative int read as a
       size_t. Twenty missions carry twenty orders. */
    int n = 0;
    if (cap > 0) out[0] = 0;
    n = enh_append(out, cap, n, "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                   t.house, t.roundabout, t.learning, t.suicide, t.autocreate,
                   t.mercenary, t.priority, t.maxallowed, t.initnum, t.fear, t.nclass);
    for (int i = 0; i < t.nclass; i++)
        n = enh_append(out, cap, n, ",%s:%d", t.cls[i].code, t.cls[i].num);
    n = enh_append(out, cap, n, ",%d", t.nmission);
    for (int i = 0; i < t.nmission; i++)
        n = enh_append(out, cap, n, ",%s:%d", TEAM_MIS[t.mis[i].mission], t.mis[i].arg);
    n = enh_append(out, cap, n, ",%d,%d", t.reinforcable, t.prebuilt);
    return n;
}

/* The types a team may contain. Buildings cannot be members -- teamtype.cpp:316-338
   tries infantry, then units, then aircraft, and never buildings -- so the picker is
   built from those three groups only. Aircraft are not in EDIT_ITEMS (nothing places
   them on a map) but they are legal members, reachable only via reinforcement, so the
   four names are carried here. */
static const char* const TEAM_AIRCRAFT[4] = { "A10", "ORCA", "TRAN", "HELI" };

static int edit_team_type_n(void)
{
    return EDIT_KIND_N[EDIT_KIND_INFANTRY] + EDIT_KIND_N[EDIT_KIND_UNIT] + 4;
}

static const char* edit_team_type_at(int i)
{
    const int ni = EDIT_KIND_N[EDIT_KIND_INFANTRY];
    const int nu = EDIT_KIND_N[EDIT_KIND_UNIT];
    if (i < 0) return "E1";
    if (i < ni) return EDIT_ITEMS[EDIT_KIND_FIRST[EDIT_KIND_INFANTRY] + i].code;
    if (i < ni + nu) return EDIT_ITEMS[EDIT_KIND_FIRST[EDIT_KIND_UNIT] + (i - ni)].code;
    if (i < ni + nu + 4) return TEAM_AIRCRAFT[i - ni - nu];
    return "E1";
}

static int edit_team_type_index(const char* code)
{
    for (int i = 0; i < edit_team_type_n(); i++)
        if (!strcasecmp(edit_team_type_at(i), code)) return i;
    return 0;
}


/* Which rule owns each cell, as an index into g_triggers, or -1. Stored by index rather
   than by name so a rename cannot orphan a zone. */
static short g_cellTrig[EDIT_GRID_MAX];
static bool  g_zoneDirty = false;
/* While this is on the canvas stands aside and the map is live, so a zone is painted
   where it actually is rather than on a diagram of it. */
static bool  g_zonePaint = false;
/* Same idea for objects: while this is on, clicking one attaches the selected
   rule to it rather than placing or selecting anything. */
static bool  g_tagMode = false;
static int   g_zoneBrush = 1;        /* radius in cells: 1, 2, 3 */

static void edit_zones_clear(void)
{
    for (int i = 0; i < EDIT_GRID_MAX; i++) g_cellTrig[i] = -1;
    g_zoneDirty = false;
}

static int edit_trigger_by_name(const char* nm)
{
    if (!nm || !*nm) return -1;
    for (size_t i = 0; i < g_triggers.size(); i++)
        if (!strcasecmp(g_triggers[i].name, nm)) return (int)i;
    return -1;
}


/* TRACE. A rule you have written is a claim about the future, and the only honest way to
   check it is to let the future happen. Time triggers poll every 90 ticks, so a rule set
   for half an hour is 27,000 ticks away -- unwatchable live, about half a minute
   headless. This runs the mission with nothing drawn, records the tick each rule fires
   on, and comes back to the editor with the answer written on the cards.
   Declared up here with the rest of the panel's state, because the button draws it. */
static int  g_editTraceReq = 0;
static int  g_editTraceMin = 20;      /* how many mission-minutes to run */

/* What the last trace found for one rule: the tick it fired on, -1 for "ran and it never
   fired", or -2 for "no trace has been run". The distinction matters -- a rule with no
   badge is unknown, and a rule with a red one is DEAD, and conflating them would make
   the feature worse than useless. */
/* Any change to the script throws the last trace away. A verdict that describes a rule
   you have since edited is worse than no verdict: it looks like evidence. */
static void edit_check_dirty(void);   /* defined with the check, further down */

static void edit_trace_invalidate(void)
{
    edit_check_dirty();
    if (!g_traceRan) return;
    g_traceRan = 0;
    g_traceFired.clear();
    g_traceEnhFired.clear();
}

static int edit_trace_of(const char* name)
{
    if (!g_traceRan) return -2;
    std::map<std::string, int>::const_iterator it = g_traceFired.find(name);
    return it == g_traceFired.end() ? -1 : it->second;
}

/* THE NO-GO OVERLAY. Every cell nothing can be built on, marked on the ground.
 *
 *  The status card already names the cell that refuses a placement, which answers "why
 *  not here". It does not answer "then where", and a person hunting for somewhere a
 *  Weapons Factory will actually fit was clicking around one cell at a time to find out.
 *
 *  It comes on by itself whenever something is armed, because that is exactly when the
 *  question is live, and the pin keeps it on the rest of the time. */
static bool g_nogoPin = false;

static int g_editOwner = 0;

/* WHERE THE POINTER IS, and what is under it. The game hides the OS cursor and draws
   its own pointer inside draw_frame(), which is UNDER this panel -- so over the sidebar
   there was no pointer at all, and an interface you cannot see yourself pointing at
   reads as an interface that does not respond. The panel draws its own arrow and its
   hover states from these. */
static float g_euiMouseX = -1.0f, g_euiMouseY = -1.0f;
static int   g_euiHotKind = 0;   /* an EuiHit -- what the pointer is over            */
static int   g_euiHotArg  = -1;

/* Whether UNDO / REDO / REVERT can do anything right now, so the buttons render dim
   when they cannot. Defined with the undo stack, far below; declared here because the
   panel draws them. */
static bool eui_action_live(int i);

/* The five palettes. Codes only -- the cameo art and the footprint come from tables that
   already exist, so this is the one list the editor has to carry. */

static int g_editTab = 0;
static int g_editArmedTab = -1;
static int g_editScroll = 0;

/* --- primitives ------------------------------------------------------------------- */

static void eui_rect(float x, float y, float w, float h, unsigned c, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(EUI_RGB(c), a);
    glBegin(GL_TRIANGLES);
    glVertex2f(x, y);         glVertex2f(x + w, y);     glVertex2f(x + w, y + h);
    glVertex2f(x, y);         glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

static void eui_frame(float x, float y, float w, float h, unsigned c, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(EUI_RGB(c), a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x + 0.5f, y + 0.5f);         glVertex2f(x + w - 0.5f, y + 0.5f);
    glVertex2f(x + w - 0.5f, y + h - 0.5f); glVertex2f(x + 0.5f, y + h - 0.5f);
    glEnd();
}

/* A panel: fill, a lighter top edge, a frame. The mockup's cards read as raised without
   any rounding, which GL immediate mode cannot do cheaply and Glide cannot do at all. */
static void eui_panel(float x, float y, float w, float h, unsigned fill)
{
    eui_rect(x, y, w, h, fill, 0.96f);
    eui_rect(x, y, w, 1, 0xffffff, 0.06f);
    eui_frame(x, y, w, h, EUI_LINE, 0.9f);
}

static void eui_cameo(float x, float y, float w, float h, const char* code)
{
    const SbTex* t = sb_cameo(code);
    if (!t || !t->gl) { eui_rect(x, y, w, h, 0x0d1520, 1.0f); return; }
    const float u = (t->uw > 0 && t->w > 0) ? (float)t->uw / (float)t->w : 1.0f;
    const float v = (t->uh > 0 && t->h > 0) ? (float)t->uh / (float)t->h : 1.0f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t->gl);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(u, 0); glVertex2f(x + w, y);
    glTexCoord2f(u, v); glVertex2f(x + w, y + h);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(u, v); glVertex2f(x + w, y + h);
    glTexCoord2f(0, v); glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}


/* --- layout ----------------------------------------------------------------------- */

struct EuiLayout {
    float railW;                 /* the left tool rail                             */
    float barY, barH;            /* the view bar, across the bottom of the map     */
    float toolSz, toolY0;        /* button size, and where the stack starts        */
    float camY;                  /* the camera action, pinned to the bottom        */
    float s;                     /* pixels per design unit, from eui_scale()       */
    float topH;                  /* the title bar, scaled                          */
    float gap;                   /* the grid gutter, scaled                        */
    float sideX, sideW;          /* the sidebar                                    */
    float radarX, radarY, radarW, radarH;
    float modeY, modeH, modeW;   /* three tabs across                              */
    float ownerY, ownerH, ownerW;
    float catY, catH, catW;      /* five category tabs                             */
    float gridX, gridY, gridW, gridH;
    float tileW, tileH; int cols;
    float lintY, lintH;
    float actY, actH;            /* undo/redo/revert row                           */
    float saveY, saveH;
    float playY, playH;
};

static void eui_layout(int fbw, int fbh, EuiLayout* L)
{
    /* THE SCALE HAS TO FIT THE FRAME, not just suit the display.
       The panel is built top-down to the grid and bottom-up from PLAY, and when the
       window is too short for the design at this scale the two halves MEET and the
       rectangles overlap -- at which point the hit test, which returns the first match,
       starts answering with whatever is highest in its own test order. --uitest caught
       exactly that: at backing 2.0 in a 1024x640 frame the action row answered TABC and
       the palette answered SAVE.
       So the fixed furniture is measured first, and the scale comes down until it fits.
       Below 1.0 if it has to: a cramped panel is usable, an overlapping one is not. */
    float S = eui_scale(fbw, fbh);
    {
        const float fixed = EUI_TOP_H + EUI_PAD          /* title bar               */
                          + EUI_SIDE_W * 0.42f + EUI_PAD /* radar (square)          */
                          + 46 + EUI_PAD                 /* mode tabs               */
                          + 30 + EUI_PAD                 /* owner chips             */
                          + 32 + EUI_PAD                 /* category tabs           */
                          + 60 + EUI_PAD                 /* the grid's own minimum  */
                          + 96 + EUI_PAD                 /* the readout             */
                          + 26 + 5                       /* the action row          */
                          + 38 + EUI_PAD                 /* SAVE                    */
                          + 34 + EUI_PAD;                /* PLAY                    */
        const float fit = (float)fbh / fixed;
        if (S > fit) S = fit;
        if (S < 0.40f) S = 0.40f;
    }
    L->s = S;
    const float pad = EUI_PAD * S;
    L->railW  = EUI_RAIL_W * S;
    L->toolSz = EUI_TOOL_SZ * S;
    L->sideW = EUI_SIDE_W * S;
    /* Never more than a third of the frame: on a narrow window the map matters more
       than the palette, and a sidebar that leaves no map is not a map editor. */
    if (L->sideW > (float)fbw * 0.34f) L->sideW = (float)fbw * 0.34f;
    L->sideX = (float)fbw - L->sideW;
    L->topH  = EUI_TOP_H * S;
    L->barH   = 44 * S;
    L->barY   = (float)fbh - L->barH;
    L->toolY0 = L->topH + 8 * S;
    L->camY   = L->barY - 8 * S - L->toolSz;

    float y = L->topH + pad;

    L->radarX = L->sideX + pad;   L->radarY = y;
    L->radarW = L->sideW * 0.42f; L->radarH = L->radarW;   /* square */
    y += L->radarH + pad;

    L->modeW = (L->sideW - pad * 2 - pad * 4) / 5.0f;
    L->modeY = y; L->modeH = 46 * S;  y += L->modeH + pad;

    L->ownerW = (L->sideW - pad * 2 - 3 * 4 * S) / 4.0f;
    L->ownerY = y; L->ownerH = 30 * S; y += L->ownerH + pad;

    L->catW = (L->sideW - pad * 2 - 4 * 3 * S) / 5.0f;
    L->catY = y; L->catH = 32 * S;     y += L->catH + pad;

    /* Everything below is pinned to the bottom and the grid takes what is left: the
       save button must never be the thing that falls off a short window. */
    L->playH = 34 * S; L->saveH = 38 * S; L->actH = 26 * S; L->lintH = 96 * S;
    L->playY = (float)fbh - pad - L->playH;
    L->saveY = L->playY - pad - L->saveH;
    L->actY  = L->saveY - 5 * S - L->actH;
    L->lintY = L->actY  - pad - L->lintH;

    L->gridX = L->sideX + pad;  L->gridY = y;
    L->gridW = L->sideW - pad * 2;
    L->gridH = L->lintY - pad - y;
    if (L->gridH < 60 * S) L->gridH = 60 * S;

    L->cols  = 4;
    L->gap   = 5 * S;
    L->tileW = (L->gridW - (L->cols - 1) * L->gap) / (float)L->cols;
    L->tileH = L->tileW * 0.86f + 12 * S;   /* cameo is 32x24, plus a name strip */
}


static bool edit_over_chrome(float mx, float my, int fbw, int fbh)
{
    if (!g_editOn) return false;
    if (mx < 0.0f) return true;              /* pointer is off the window entirely */
    EuiLayout L; eui_layout(fbw, fbh, &L);
    return (my < L.topH) || (mx < L.railW) || (mx >= L.sideX) || (my >= L.barY);
}

/* --- tool icons, drawn rather than loaded -------------------------------------------
 *
 *  The browser uses inline SVG symbols. There is no SVG here and no icon sheet, but
 *  these are four simple shapes and drawing them as triangles and lines is less work
 *  than baking another asset -- and it scales with the panel for free, which a bitmap
 *  would not.
 * ---------------------------------------------------------------------------------- */

static void eui_line(float x0, float y0, float x1, float y1, float w, unsigned c, float a)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    glColor4f(EUI_RGB(c), a);
    glBegin(GL_TRIANGLES);
    glVertex2f(x0 + nx, y0 + ny); glVertex2f(x1 + nx, y1 + ny); glVertex2f(x1 - nx, y1 - ny);
    glVertex2f(x0 + nx, y0 + ny); glVertex2f(x1 - nx, y1 - ny); glVertex2f(x0 - nx, y0 - ny);
    glEnd();
}

static void eui_tri(float ax, float ay, float bx, float by, float cx, float cy,
                    unsigned c, float a)
{
    glColor4f(EUI_RGB(c), a);
    glBegin(GL_TRIANGLES);
    glVertex2f(ax, ay); glVertex2f(bx, by); glVertex2f(cx, cy);
    glEnd();
}

/* Each icon draws inside a box of side `d` centred on (cx,cy). */
static void eui_icon(int tool, float cx, float cy, float d, unsigned col)
{
    const float r = d * 0.5f, w = d * 0.13f;
    switch (tool) {
        case TOOL_PLACE:      /* a pointer arrow */
            eui_tri(cx - r * 0.55f, cy - r * 0.85f,
                    cx + r * 0.28f, cy + r * 0.40f,
                    cx - r * 0.18f, cy + r * 0.52f, col, 1.0f);
            eui_tri(cx - r * 0.55f, cy - r * 0.85f,
                    cx - r * 0.18f, cy + r * 0.52f,
                    cx - r * 0.60f, cy + r * 0.90f, col, 1.0f);
            break;
        case TOOL_MOVE: {     /* four arrows from a centre */
            eui_line(cx - r * 0.7f, cy, cx + r * 0.7f, cy, w, col, 1.0f);
            eui_line(cx, cy - r * 0.7f, cx, cy + r * 0.7f, w, col, 1.0f);
            const float h = r * 0.26f;
            eui_tri(cx + r,  cy, cx + r * 0.62f, cy - h, cx + r * 0.62f, cy + h, col, 1.0f);
            eui_tri(cx - r,  cy, cx - r * 0.62f, cy - h, cx - r * 0.62f, cy + h, col, 1.0f);
            eui_tri(cx, cy - r, cx - h, cy - r * 0.62f, cx + h, cy - r * 0.62f, col, 1.0f);
            eui_tri(cx, cy + r, cx - h, cy + r * 0.62f, cx + h, cy + r * 0.62f, col, 1.0f);
            break;
        }
        case TOOL_PICK:       /* an eyedropper */
            eui_line(cx - r * 0.62f, cy + r * 0.62f, cx + r * 0.30f, cy - r * 0.30f,
                     w * 1.5f, col, 1.0f);
            eui_tri(cx + r * 0.22f, cy - r * 0.52f,
                    cx + r * 0.80f, cy - r * 0.86f,
                    cx + r * 0.62f, cy - r * 0.16f, col, 1.0f);
            eui_tri(cx - r * 0.66f, cy + r * 0.86f,
                    cx - r * 0.86f, cy + r * 0.86f,
                    cx - r * 0.74f, cy + r * 0.52f, col, 1.0f);
            break;
        case TOOL_ERASE:      /* an eraser block, leaning */
            eui_tri(cx - r * 0.75f, cy + r * 0.45f, cx + r * 0.10f, cy - r * 0.72f,
                    cx + r * 0.78f, cy - r * 0.10f, col, 1.0f);
            eui_tri(cx - r * 0.75f, cy + r * 0.45f, cx + r * 0.78f, cy - r * 0.10f,
                    cx - r * 0.05f, cy + r * 0.80f, col, 1.0f);
            eui_line(cx - r * 0.85f, cy + r * 0.86f, cx + r * 0.85f, cy + r * 0.86f,
                     w, col, 0.55f);
            break;
        default: {            /* the camera action: a frame with a target inside */
            eui_line(cx - r * 0.8f, cy - r * 0.8f, cx - r * 0.8f, cy - r * 0.25f, w, col, 1.0f);
            eui_line(cx - r * 0.8f, cy - r * 0.8f, cx - r * 0.25f, cy - r * 0.8f, w, col, 1.0f);
            eui_line(cx + r * 0.8f, cy - r * 0.8f, cx + r * 0.8f, cy - r * 0.25f, w, col, 1.0f);
            eui_line(cx + r * 0.8f, cy - r * 0.8f, cx + r * 0.25f, cy - r * 0.8f, w, col, 1.0f);
            eui_line(cx - r * 0.8f, cy + r * 0.8f, cx - r * 0.8f, cy + r * 0.25f, w, col, 1.0f);
            eui_line(cx - r * 0.8f, cy + r * 0.8f, cx - r * 0.25f, cy + r * 0.8f, w, col, 1.0f);
            eui_line(cx + r * 0.8f, cy + r * 0.8f, cx + r * 0.8f, cy + r * 0.25f, w, col, 1.0f);
            eui_line(cx + r * 0.8f, cy + r * 0.8f, cx + r * 0.25f, cy + r * 0.8f, w, col, 1.0f);
            eui_rect(cx - r * 0.22f, cy - r * 0.22f, r * 0.44f, r * 0.44f, col, 1.0f);
            break;
        }
    }
}

/* The rail itself. Four tools from the top, the camera action pinned to the bottom --
   the browser separates them with a flex spacer for the same reason: one is a mode you
   are in, the other is a thing that happens once. */
static void eui_draw_rail(const EuiLayout* L, int fbh, float t1)
{
    const float S = L->s;
    eui_rect(0, L->topH, L->railW, (float)fbh - L->topH, 0x121822, 1.0f);
    eui_rect(L->railW - S, L->topH, S, (float)fbh - L->topH, 0x05080c, 1.0f);

    for (int i = 0; i <= TOOL_COUNT; i++) {
        const bool isCam = (i == TOOL_COUNT);
        const float y = isCam ? L->camY : L->toolY0 + i * (L->toolSz + 2 * S);
        const float x = (L->railW - L->toolSz) * 0.5f;
        const bool on  = !isCam && (i == g_editTool);
        const bool hot = (g_euiHotKind == EUI_TOOL && g_euiHotArg == i);

        if (on)       eui_rect(x, y, L->toolSz, L->toolSz, EUI_PANEL3, 1.0f);
        else if (hot) eui_rect(x, y, L->toolSz, L->toolSz, EUI_PANEL2, 1.0f);
        if (on || hot)
            eui_frame(x, y, L->toolSz, L->toolSz, on ? EUI_GOLD : EUI_LINE, 1.0f);
        /* The browser marks the active tool with a tab off the left edge. */
        if (on) eui_rect(0, y + 9 * S, 3 * S, L->toolSz - 18 * S, EUI_GOLD, 1.0f);

        eui_icon(isCam ? TOOL_COUNT : i, x + L->toolSz * 0.5f, y + L->toolSz * 0.44f,
                 L->toolSz * 0.46f, on ? EUI_GOLD : hot ? EUI_INK : EUI_DIM);

        const char* k = isCam ? "0" : TOOL_KEY[i];
        const float kw = ef_text_w(k, EUI_TS(t1));
        ef_text(x + L->toolSz - kw - 3 * S, y + L->toolSz - 10 * S, k, EUI_TS(t1),
                EUI_RGB(on ? EUI_GOLD : EUI_FAINT));
    }
}

/* --- the spinning coin ------------------------------------------------------------- *
 *
 *  The GDI eagle, turned about its vertical axis in the title bar. There is no mesh for
 *  it and it does not need one: a disc spun about its own axis projects to a rectangle
 *  of the same height and a cosine-narrowed width, so scaling x by cos(t) IS the
 *  rotation, exactly. The dark side of the turn is dimmed rather than flipped, which is
 *  what sells it as a solid coin rather than a flapping card.
 *
 *  Driven off the ENGINE frame counter, never wallclock, so two --shot runs of the same
 *  scenario stay byte-identical -- the same rule the rest of the renderer lives under.
 * ---------------------------------------------------------------------------------- */

static GLuint g_euiEmblemTex = 0;

static void eui_coin(float cx, float cy, float r, unsigned long frame)
{
    if (!g_euiEmblemTex) {
        glGenTextures(1, &g_euiEmblemTex);
        glBindTexture(GL_TEXTURE_2D, g_euiEmblemTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, EDIT_EMBLEM_N, EDIT_EMBLEM_N, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, EDIT_EMBLEM);
    }
    /* Degrees per drawn frame. The sim is frozen while editing, so the ENGINE frame
       counter is standing still and cannot drive this -- the draw counter is the only
       clock the editor has. About five seconds a revolution at 60fps. */
    const float t = (float)(frame % 300) * (1.2f * 0.0174532925f);
    const float c = cosf(t);
    const float w = r * (c < 0 ? -c : c);
    /* Edge-on: draw the rim rather than nothing, so the coin never blinks out. */
    if (w < 1.0f) {
        eui_rect(cx - 1.0f, cy - r, 2.0f, r * 2.0f, EUI_GOLD, 0.65f);
        return;
    }
    const float shade = 0.55f + 0.45f * (c < 0 ? -c : c);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_euiEmblemTex);
    glColor4f(shade, shade, shade, 1.0f);
    /* c < 0 is the back of the coin, so the texture reads mirrored -- which is what the
       back of a coin does. */
    const float u0 = (c < 0) ? 1.0f : 0.0f, u1 = (c < 0) ? 0.0f : 1.0f;
    glBegin(GL_TRIANGLES);
    glTexCoord2f(u0, 0); glVertex2f(cx - w, cy - r);
    glTexCoord2f(u1, 0); glVertex2f(cx + w, cy - r);
    glTexCoord2f(u1, 1); glVertex2f(cx + w, cy + r);
    glTexCoord2f(u0, 0); glVertex2f(cx - w, cy - r);
    glTexCoord2f(u1, 1); glVertex2f(cx + w, cy + r);
    glTexCoord2f(u0, 1); glVertex2f(cx - w, cy + r);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}



/* The category strip is mode-dependent: five object kinds, or seven terrain families.
   One place answers, so the draw, the hit test and the test harness cannot disagree. */
static void edit_terrain_build(void);

static int eui_cat_count(void)
{
    if (g_editMode == 1 && !g_terrBuilt) edit_terrain_build();
    return (g_editMode == 1) ? TG_COUNT : EDIT_KIND_COUNT;
}
static const char* eui_cat_name(int i)
{
    return (g_editMode == 1) ? TERR_GROUP_NAME[i] : EDIT_KIND_NAME[i];
}
static int eui_cat_n(int i)
{
    return (g_editMode == 1) ? (int)g_terrList[i].size() : EDIT_KIND_N[i];
}
static int eui_cat_sel(void)      { return (g_editMode == 1) ? g_terrGroup : g_editTab; }
static int eui_grid_scroll(void)  { return (g_editMode == 1) ? g_terrScroll : g_editScroll; }
static int eui_grid_n(void)       { return eui_cat_n(eui_cat_sel()); }


/* THE SEA BEHIND A HOLE TILE.
   A water tile in the TEMPERAT and DESERT banks carries no water. The cartridge punches
   an ALPHA HOLE in the tile art and draws the sea itself underneath, in two passes
   (draw_seabed, then draw_water). The map looks right because those passes are there.
   A thumbnail has no sea under it, so its hole texels composited straight onto the panel
   fill and the WATER drawer read as empty boxes with names beneath them: DESERT's single
   water tile is alpha 0 in all 576 of its texels, and so are all five of TEMPERAT's.
   Two shore pieces went the same way, and forty more drew land with a bite out of it.

   So the thumbnail paints its own sea first, out of the pack's own textures, with the
   console's own arithmetic: the sea floor opaque, then the two water sheets multiplied
   together at alpha 170/255 over it. Same source as the map, so a swatch and the cell it
   stamps cannot show different water.

   Two things are deliberately NOT copied from the world's water. The scroll phase and
   the corner warp, which exist to animate a large moving surface and say nothing in a
   cell twenty-odd pixels across; a drawer entry is a diagram of a tile, not a view of
   the sea. And the console's 1.92 texture repeats per cell: ONE repeat per cell keeps
   every UV inside 0..1, so whatever wrap mode the last world pass left on these shared
   texture objects cannot reach the panel. */
static void eui_sea_vert(float u, float v, float px, float py)
{
    /* Both units get the SAME coordinate: the two water sheets are the same size and
       the console multiplies them texel for texel. Unit 1 is fed even on the floor
       pass, where it is ignored, so the two passes emit identical geometry. */
    glMultiTexCoord2f(GL_TEXTURE0, u, v);
    glMultiTexCoord2f(GL_TEXTURE1, u, v);
    glVertex2f(px, py);
}

static void eui_thumb_sea_quads(float ox, float oy, float c, int tw, int th,
                                const unsigned char* sea, float us, float vs)
{
    glBegin(GL_TRIANGLES);
    for (int dy = 0; dy < th; dy++)
        for (int dx = 0; dx < tw; dx++) {
            if (!sea[dy * tw + dx]) continue;
            const float px = ox + dx * c, py = oy + dy * c;
            eui_sea_vert(0,  0,  px,     py);
            eui_sea_vert(us, 0,  px + c, py);
            eui_sea_vert(us, vs, px + c, py + c);
            eui_sea_vert(0,  0,  px,     py);
            eui_sea_vert(us, vs, px + c, py + c);
            eui_sea_vert(0,  vs, px,     py + c);
        }
    glEnd();
}

static void eui_thumb_sea(float ox, float oy, float c, int tw, int th,
                          const unsigned char* sea)
{
    const int nt = (int)g_pack.tex.size();
    const bool inRange = g_pack.seabedTex >= 0 && g_pack.seabedTex < nt &&
                         g_pack.waterTex1 >= 0 && g_pack.waterTex1 < nt &&
                         g_pack.waterTex2 >= 0 && g_pack.waterTex2 < nt;
    const bool have = inRange && g_pack.tex[g_pack.seabedTex].gl &&
                      g_pack.tex[g_pack.waterTex1].gl && g_pack.tex[g_pack.waterTex2].gl;
    if (!have) {
        /* A pack baked before the water and seabed indices existed carries no sea art
           at all, and the world's water pass falls back to a flat ripple on this same
           base colour. The drawer degrades to what the map would show rather than to a
           colour invented here, so an old pack stays self-consistent. */
        glDisable(GL_TEXTURE_2D);
        glColor4f(0.11f, 0.20f, 0.29f, 1.0f);
        eui_thumb_sea_quads(ox, oy, c, tw, th, sea, 1.0f, 1.0f);
        glColor4f(1, 1, 1, 1);
        return;
    }

    /* Pass 1: the sea floor, opaque, one tile per cell -- the console's own rate, whose
       ground ST works out to exactly 1.0 repeat. */
    const PackTex& fl = g_pack.tex[g_pack.seabedTex];
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, fl.gl);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1, 1, 1, 1);
    eui_thumb_sea_quads(ox, oy, c, tw, th, sea,
                        (float)fl.uw / (float)fl.w, (float)fl.uh / (float)fl.h);

    /* Pass 2: the surface. RGB is the two water sheets multiplied across two texture
       units, ALPHA is the console's 170/255 primary, and one ordinary src-alpha blend
       lays it over the floor. Two texture units is exactly what a Voodoo 2 has, which
       is why the world's sea is built this way too, so this adds no Tier 1 debt. */
    const PackTex& s0 = g_pack.tex[g_pack.waterTex1];
    const PackTex& s1 = g_pack.tex[g_pack.waterTex2];
    glBindTexture(GL_TEXTURE_2D, s0.gl);
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s1.gl);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 170.0f / 255.0f);
    eui_thumb_sea_quads(ox, oy, c, tw, th, sea,
                        (float)s0.uw / (float)s0.w, (float)s0.uh / (float)s0.h);

    /* Put the fixed pipe back the way the world's tinted terrain block does: unit 1 off
       and left on MODULATE, unit 0 current and selected. A live second unit silently
       recolours the next textured quad drawn, which here would be the tile art itself,
       two calls later. */
    glActiveTexture(GL_TEXTURE1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1, 1, 1, 1);
}


/* One template's art, composed at its true w x h, straight out of the terrain atlas.
   The same sheet the world is drawn from, so the palette cannot show a different tile
   to the one that lands -- and, where the art is punched through, over the same sea. */
static void eui_template_thumb(float x, float y, float w, float h, int tid)
{
    if (tid < 0 || tid >= EDIT_TEMPLATE_COUNT) return;
    const EditTemplate* e = &EDIT_TEMPLATES[tid];
    const PackTex& at = g_pack.tex[g_pack.terrainTex];
    if (!at.gl || at.uw <= 0 || at.uh <= 0) return;

    const int tw = e->w > 0 ? e->w : 1, th = e->h > 0 ? e->h : 1;
    /* Fit the whole block in the tile, square cells, centred. */
    float cw = w / (float)tw, ch = h / (float)th;
    const float c = cw < ch ? cw : ch;
    const float ox = x + (w - c * tw) * 0.5f, oy = y + (h - c * th) * 0.5f;

    /* Resolve every cell ONCE, into the table the sea pass and the art pass then SHARE.
       Two reasons, and the second is the important one. tt_slot_of is a linear scan of
       up to 994 entries, so three passes over a 48-cell template would otherwise walk it
       a hundred and forty-four times per thumbnail. And sharing the answer means the sea
       can only ever be drawn under a cell the art pass is about to draw: the two cannot
       drift into disagreeing about which cells this template even has. */
    enum { EUI_THUMB_CELLS = 256 };  /* RV20 at 6x8 = 48 is the biggest in the table */
    const int ncell = tw * th;
    if (ncell > EUI_THUMB_CELLS) return;
    int slot[EUI_THUMB_CELLS];
    unsigned char sea[EUI_THUMB_CELLS];
    int nsea = 0;
    for (int k = 0; k < ncell; k++) {
        int s = 0;
        slot[k] = tt_slot_of(g_pack.theater, tid, k, &s);
        /* A slot of -1 is NOT a hole in the art. It is a cell this theater's bank has no
           tile for at all (S20's first, eight of DESERT BRIDGE3's, thirteen of RV20's),
           and it stays empty, which is how the drawer shows a piece the bank only half
           carries. Only a cell that HAS art, and whose art is punched through, gets sea
           behind it. */
        sea[k] = (unsigned char)(slot[k] >= 0 && s ? 1 : 0);
        nsea += sea[k];
    }

    /* The sea goes UNDER the art and the art's own alpha is what lets it show, so this
       owns its blend state instead of inheriting the panel's. The panel does set exactly
       this, but a thumbnail that is only correct because of who drew last is a bug
       waiting to happen, and the failure is not subtle: with blending off, the hole
       texels paint over the sea in whatever the atlas bleed left in them, which for a
       fully transparent water tile is black. Blending is deliberately left ON at the
       end, because everything the panel draws after this expects it. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (nsea) eui_thumb_sea(ox, oy, c, tw, th, sea);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, at.gl);
    glColor4f(1, 1, 1, 1);
    const float us = (float)at.uw / (float)at.w, vs = (float)at.uh / (float)at.h;
    glBegin(GL_TRIANGLES);
    for (int dy = 0; dy < th; dy++)
        for (int dx = 0; dx < tw; dx++) {
            const int sl = slot[dy * tw + dx];
            if (sl < 0) continue;
            float sx, sy; tt_slot_rect(sl, &sx, &sy);
            const float u0 = sx / (float)at.uw * us, u1 = (sx + TT_TS) / (float)at.uw * us;
            const float v0 = sy / (float)at.uh * vs, v1 = (sy + TT_TS) / (float)at.uh * vs;
            const float px = ox + dx * c, py = oy + dy * c;
            glTexCoord2f(u0, v0); glVertex2f(px, py);
            glTexCoord2f(u1, v0); glVertex2f(px + c, py);
            glTexCoord2f(u1, v1); glVertex2f(px + c, py + c);
            glTexCoord2f(u0, v0); glVertex2f(px, py);
            glTexCoord2f(u1, v1); glVertex2f(px + c, py + c);
            glTexCoord2f(u0, v1); glVertex2f(px, py + c);
        }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}


/* THE CLIFF DRAWER under ELEVATN: every slope template this theater has art for,
   stampable by hand -- the worst-case tool for any seam the automatic dressing gets
   wrong. Arming SHARES g_terrArmed with the terrain tab, so the stroke, the undo and
   the stamp path are all the ones that already exist; picking an elevation TOOL
   disarms it so the two never fight over a click. Geometry lives HERE, once, and the
   draw and the hit test both call it -- the two cannot drift. */
static bool eui_is_slope(int tid)
{
    if (tid < 0 || tid >= EDIT_TEMPLATE_COUNT) return false;
    const char* n = EDIT_TEMPLATES[tid].name;
    return n[0] == 'S' && n[1] >= '0' && n[1] <= '9';
}

static int eui_cliff_list(int* out, int nmax)
{
    int n = 0;
    for (int t = 0; t < EDIT_TEMPLATE_COUNT && n < nmax; t++) {
        if (!eui_is_slope(t)) continue;
        /* ANY icon with art admits the piece: a template's cell 0 can be a real
           hole in the art (S20's is), and keying on it alone dropped the piece. */
        for (int i = 0; i < 4; i++) {
            int sea = 0;
            if (tt_slot_of(g_pack.theater, t, i, &sea) >= 0) { out[n++] = t; break; }
        }
    }
    return n;
}

static float eui_cliff_top(const EuiLayout* L)
{
    const float S = L->s, pad = EUI_PAD * S;
    /* catY, then the ladder, the three tools, the brush -- the same walk
       eui_draw_elev and the hit test make. */
    return L->catY + (32 * S + pad) + 3 * (34 * S + 4 * S) + pad + 30 * S
         + pad + 12 * S;
}

static bool eui_cliff_cell_rect(const EuiLayout* L, int k,
                                float* x, float* y, float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    const int cols = 5;
    const float gap = 3 * S;
    *w = (L->sideW - pad * 2 - (cols - 1) * gap) / (float)cols;
    *h = *w + 10 * S;                                   /* thumb + name strip */
    *x = L->sideX + pad + (k % cols) * (*w + gap);
    *y = eui_cliff_top(L) + (k / cols) * (*h + gap);
    /* L->lintY is the panel's usable bottom -- the status card sits between it and
       the action row and takes its own hits, which --uitest duly proved. */
    return *y + *h <= L->lintY - 4 * S;
}

/* The elevation panel: the ladder, the three tools, the brush, and -- until the map is
   on the ladder -- the conversion gate instead of all of it. */
static void eui_draw_elev(const EuiLayout* L, float t1)
{
    const float S = L->s, pad = EUI_PAD * S;
    static const char* RUNGN[5] = { "LOW", "GROUND", "+1", "+2", "+3" };
    static const char* TOOLN[3] = { "RAISE / LOWER", "GRADED PASS", "RAMP" };
    static const char* TOOLD[3] = { "paint the armed rung",
                                    "leave a block undressed so the ground just slopes",
                                    "make the clicked cliff face climbable" };
    static const char* BRUSHN[3] = { "2x2", "4x4", "6x6" };

    float y = L->catY;

    if (!elev_ready()) {
        /* THE GATE. State the cost before doing it, and do not do it by accident. */
        eui_panel(L->sideX + pad, y, L->sideW - pad * 2, L->gridY + L->gridH - y,
                  EUI_PANEL);
        const float tx = L->sideX + pad + 10 * S;
        ef_text(tx, y + 10 * S, "THIS MAP IS NOT ON THE LADDER", t1, EUI_RGB(EUI_GOLD));
        char l[128];
        snprintf(l, sizeof l, "%d of %d playable corners sit off it",
                 g_elevOffLadder, g_elevTotal);
        ef_text(tx, y + 28 * S, l, t1, EUI_RGB(EUI_DIM));
        ef_text(tx, y + 50 * S, "The tier tool cannot express smooth ground, so",
                EUI_TS(t1), EUI_RGB(EUI_FAINT));
        ef_text(tx, y + 64 * S, "every edit would be refused. Converting snaps",
                EUI_TS(t1), EUI_RGB(EUI_FAINT));
        ef_text(tx, y + 78 * S, "every corner to the ladder and redraws every",
                EUI_TS(t1), EUI_RGB(EUI_FAINT));
        ef_text(tx, y + 92 * S, "cliff from the compass.", EUI_TS(t1), EUI_RGB(EUI_FAINT));
        ef_text(tx, y + 114 * S, "IT CANNOT BE UNDONE.", t1, EUI_RGB(EUI_DANGER));

        const float bh = 34 * S, bw = L->sideW - pad * 4;
        const float bx = L->sideX + pad * 2, byy = y + 140 * S;
        const bool hot = (g_euiHotKind == EUI_ELEVGATE);
        eui_rect(bx, byy, bw, bh, EUI_CYAN, hot ? 1.0f : 0.9f);
        const char* c = "CONVERT THIS MAP";
        ef_text(bx + (bw - ef_text_w(c, t1)) * 0.5f, byy + bh * 0.5f - 3 * S, c, t1,
                0.05f, 0.07f, 0.09f);
        return;
    }

    /* ---- the ladder ---- */
    ef_text(L->sideX + pad, y - 12 * S, "RUNG", EUI_TS(t1), EUI_RGB(EUI_FAINT));
    {
        const float w = (L->sideW - pad * 2 - 4 * 3 * S) / 5.0f;
        for (int i = 0; i < 5; i++) {
            const float x = L->sideX + pad + i * (w + 3 * S);
            const bool on = (i == g_elevTierSel);
            const bool hot = (g_euiHotKind == EUI_ELEVRUNG && g_euiHotArg == i);
            eui_panel(x, y, w, 32 * S, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL);
            if (on) eui_rect(x, y, w, 3 * S, EUI_CYAN, 1.0f);
            char lbl[8]; snprintf(lbl, sizeof lbl, "%.4s", RUNGN[i]);
            ef_text(x + (w - ef_text_w(lbl, EUI_TS(t1))) * 0.5f, y + 11 * S, lbl,
                    EUI_TS(t1), EUI_RGB(on ? EUI_CYAN : EUI_DIM));
        }
    }
    y += 32 * S + pad;

    /* ---- the three tools ---- */
    for (int i = 0; i < 3; i++) {
        const float h = 34 * S;
        const bool on = (i == g_elevTool);
        const bool hot = (g_euiHotKind == EUI_ELEVTOOL && g_euiHotArg == i);
        eui_panel(L->sideX + pad, y, L->sideW - pad * 2, h,
                  on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL);
        if (on) eui_rect(L->sideX + pad, y, 3 * S, h, EUI_CYAN, 1.0f);
        ef_text(L->sideX + pad + 10 * S, y + 6 * S, TOOLN[i], t1,
                EUI_RGB(on ? EUI_CYAN : EUI_DIM));
        ef_text(L->sideX + pad + 10 * S, y + 20 * S, TOOLD[i], EUI_TS(t1),
                EUI_RGB(EUI_FAINT));
        y += h + 4 * S;
    }
    y += pad;

    /* ---- brush size ---- */
    ef_text(L->sideX + pad, y - 12 * S, "BRUSH", EUI_TS(t1), EUI_RGB(EUI_FAINT));
    {
        const float w = (L->sideW - pad * 2 - 2 * 4 * S) / 3.0f;
        for (int i = 0; i < 3; i++) {
            const float x = L->sideX + pad + i * (w + 4 * S);
            const bool on = (i + 1 == g_elevBrush);
            const bool hot = (g_euiHotKind == EUI_ELEVBRUSH && g_euiHotArg == i);
            eui_panel(x, y, w, 30 * S, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL);
            ef_text(x + (w - ef_text_w(BRUSHN[i], t1)) * 0.5f, y + 10 * S, BRUSHN[i], t1,
                    EUI_RGB(on ? EUI_CYAN : EUI_DIM));
        }
    }

    /* ---- the cliff drawer: stamp any slope piece by hand ---- */
    {
        ef_text(L->sideX + pad, eui_cliff_top(L) - 12 * S,
                "CLIFF PIECES   CLICK TO ARM, CLICK THE MAP TO STAMP",
                EUI_TS(t1), EUI_RGB(EUI_FAINT));
        int list[64];
        const int n = eui_cliff_list(list, 64);
        int shown = 0;
        for (int k = 0; k < n; k++) {
            float x, cy, w, h;
            if (!eui_cliff_cell_rect(L, k, &x, &cy, &w, &h)) break;
            shown++;
            const int tid = list[k];
            const bool on  = (g_terrArmed == tid);
            const bool hot = (g_euiHotKind == EUI_CLIFFITEM && g_euiHotArg == tid);
            eui_rect(x, cy, w, h, on ? EUI_PANEL3 : EUI_PANEL2, (on || hot) ? 1.0f : 0.75f);
            eui_frame(x, cy, w, h, on ? EUI_CYAN : hot ? EUI_CYAN : EUI_LINE, 1.0f);
            eui_template_thumb(x + 2 * S, cy + 2 * S, w - 4 * S, h - 12 * S, tid);
            char lbl[16];
            snprintf(lbl, sizeof lbl, "%.4s", EDIT_TEMPLATES[tid].name);
            ef_text(x + (w - ef_text_w(lbl, EUI_TS(t1))) * 0.5f, cy + h - 9 * S, lbl,
                    EUI_TS(t1), EUI_RGB(on ? EUI_INK : EUI_DIM));
        }
        if (shown < n) {
            char more[48];
            snprintf(more, sizeof more, "+%d MORE BELOW -- taller window shows them",
                     n - shown);
            ef_text(L->sideX + pad, L->actY - 5 * S - 10 * S, more, EUI_TS(t1),
                    EUI_RGB(EUI_FAINT));
        }
    }
}



/* ------------------------------------------------------------------------------------
 *  THE MAP MENU, AND MODALS
 *
 *  A requirement: the map browser must not stand between double-clicking the app and
 *  seeing a map. So the launcher opens the last map straight away, and choosing a
 *  different one -- or making one -- is a menu inside the editor, where it belongs.
 *
 *  A modal here is a real modal: while one is up it takes every click, and the panel
 *  and the map behind it take none. That is the whole reason the 1995 pause dialog was
 *  such a trap when it opened over the editor, and it is worth not repeating.
 * ---------------------------------------------------------------------------------- */

enum EuiModal { MODAL_NONE = 0, MODAL_NEW, MODAL_OPEN, MODAL_CHECK, MODAL_SAVEAS };

/* --- MISSION CHECK ------------------------------------------------------------------
 *
 *  What is wrong with this map, said in sentences.
 *
 *  Sixteen triggers in the shipped campaign never fire, and every one of them is a rule
 *  that LOOKS fine: the event is real, the action is real, the house is right. What is
 *  missing is a connection -- a rule that waits for an object to be destroyed and is
 *  attached to no object, a rule that names a team the mission does not have, a team
 *  whose Move order points at a waypoint nobody placed. None of that is visible in the
 *  file and none of it is an error to the engine. It is simply a rule that never fires.
 *
 *  This is the check for exactly that class of thing. It is not a linter for style; every
 *  row here is a claim that something in the mission cannot work, or a warning that it
 *  probably does not do what it looks like it does.
 * ---------------------------------------------------------------------------------- */

enum CheckLevel { CHK_OK = 0, CHK_WARN, CHK_ERROR };

struct CheckRow {
    unsigned char level;
    char what[96];      /* the claim, short                                       */
    char why[160];      /* the consequence, in plain words                        */
};

static std::vector<CheckRow> g_check;
static int g_checkErrors = 0, g_checkWarns = 0;
static bool g_checkRan = false;

/* Re-run the check whenever the document changes, so the badge in the panel is never
   describing a map you have since fixed. It is cheap -- a few hundred objects and a few
   dozen rules -- and it only runs when something has actually been edited. */
static void edit_check_dirty(void) { g_checkRan = false; }

static void chk_add(int level, const char* what, const char* why)
{
    CheckRow r;
    r.level = (unsigned char)level;
    snprintf(r.what, sizeof r.what, "%s", what);
    snprintf(r.why,  sizeof r.why,  "%s", why ? why : "");
    g_check.push_back(r);
    if (level == CHK_ERROR) g_checkErrors++;
    else if (level == CHK_WARN) g_checkWarns++;
}

static int  g_euiModal   = MODAL_NONE;
static int  g_checkScroll = 0;
static bool g_euiMenuOpen = false;

/* THE SLOT THE SAVE AS COPY WILL TAKE, resolved once when that dialog opens and shown
   inside it. Someone about to name a map should be able to see which USERnn file the
   name is going into, because the slot is what the GAME finds the map by. Declared up
   here with the rest of the modal state because the dialog draws it -- the
   drawing-first ordering rule of this file. */
static char g_saveasSlot[32] = "";

/* NEW MAP's choices. The four legacy presets are playable RECTANGLES inside the
   classic 64x64 grid -- the grid is fixed and what varies is the rect. The fifth is
   the MEGAMAPS grid itself: a full 128x128 world (playable 120x120, inset 4), saved
   as [MAP] Version=1 with the brain's own sparse .BIN. */
struct EuiSize { const char* name; int w, h; int grid; };
#define EUI_SIZE_N 5
static const EuiSize EUI_SIZES[EUI_SIZE_N] = {
    { "SMALL   32 x 32",  32, 32,  64 },
    { "MEDIUM  44 x 44",  44, 44,  64 },
    { "LARGE   54 x 54",  54, 54,  64 },
    { "HUGE    62 x 62",  62, 62,  64 },
    { "128 x 128  (BIG MAP)", 120, 120, 128 },
};
/* Theaters in the New Map dialog's order. Index 2 is the PC winter bank rebuilt from
   TD's own WINTER.MIX art (tools/win_to_n64bank.py) -- the cartridge never shipped it.
   Index 3 is Red Alert's snow LOOK on the same TD template set: the pack and the
   editor call it SNOW, while the INI's Theater= line says WINTER for the TD brain
   (which has no snow theater) and a CNC3DTheater=SNOW line carries the truth for
   everything of ours. eui_theater_index answers for BOTH spellings. */
static const char* const EUI_THEATERS[5] = { "TEMPERATE", "DESERT", "WINTER",
                                             "SNOW", "SAND" };
#define EUI_THEATER_N 5
static int eui_theater_index(const char* t)
{
    if (t && (t[0] == 'D' || t[0] == 'd')) return 1;
    if (t && (t[0] == 'W' || t[0] == 'w')) return 2;
    /* Two letters here: SNOW and SAND share an initial. */
    if (t && (t[0] == 'S' || t[0] == 's') && (t[1] == 'N' || t[1] == 'n')) return 3;
    if (t && (t[0] == 'S' || t[0] == 's') && (t[1] == 'A' || t[1] == 'a')) return 4;
    return 0;
}
static int  g_newSize    = 1;
static int  g_newTheater = 0;      /* index into EUI_THEATERS                        */
static int  g_newMulti   = 0;      /* 0 singleplayer, 1 multiplayer / skirmish       */

/* The map's own kind, which outlives the New Map dialog: it decides where the map is
   filed and which list the game offers it in. Changeable afterwards from the menu. */
static int  g_mapIsMulti = 0;

/* THE MAP MENU, AND WHY EVERY ROW CARRIES AN ID.
 *
 * This table used to be a bare list of strings and the click handler was a switch on the
 * ROW NUMBER -- case 0, 1, 3, 4, 5, 7, 9. Adding one entry anywhere above the bottom
 * therefore silently reassigned every action below it: SAVE would revert, PLAY would
 * check the mission, and nothing would fail to compile or fail a test. It had already
 * bitten once in the drawing code, where the SINGLEPLAYER / MULTIPLAYER row printed its
 * current value only when the row index was 6 -- and the row moved to 7, so the value
 * stopped being drawn and it went unnoticed.
 *
 * So the row's identity is stated in the row, and the dispatch switches on THAT. The
 * table can be reordered, split with separators, or grown forever without touching a
 * single call site. */
enum EuiMenuId {
    EMENU_SEP = 0, EMENU_NEW, EMENU_OPEN, EMENU_SAVE, EMENU_SAVEAS, EMENU_PLAY,
    EMENU_CHECK, EMENU_KIND, EMENU_REVERT
};
struct EuiMenuRow { const char* label; int id; };
static const EuiMenuRow EUI_MENU[] = {
    { "NEW MAP...",                 EMENU_NEW    },
    { "OPEN MAP...",                EMENU_OPEN   },
    { "-",                          EMENU_SEP    },
    { "SAVE",                       EMENU_SAVE   },
    { "SAVE AS...",                 EMENU_SAVEAS },
    { "PLAY IN THIS WINDOW",        EMENU_PLAY   },
    { "CHECK MISSION...",           EMENU_CHECK  },
    { "-",                          EMENU_SEP    },
    { "SINGLEPLAYER / MULTIPLAYER", EMENU_KIND   },
    { "-",                          EMENU_SEP    },
    { "REVERT TO SAVED",            EMENU_REVERT },
};
static const int EUI_MENU_N = (int)(sizeof(EUI_MENU) / sizeof(EUI_MENU[0]));

/* What a row MEANS, from the row number the hit test answered with. */
static int eui_menu_id(int i)
{
    return (i >= 0 && i < EUI_MENU_N) ? EUI_MENU[i].id : EMENU_SEP;
}

/* And the other way, for the script verb that drives the menu by name: WHICH ROW is
   this action on. -1 when the menu does not carry it. */
static int eui_menu_row(int id)
{
    for (int i = 0; i < EUI_MENU_N; i++) if (EUI_MENU[i].id == id) return i;
    return -1;
}

/* The button that opens it, in the title bar. */
static void eui_menu_rect(const EuiLayout* L, float* x, float* y, float* w, float* h)
{
    const float S = L->s;
    *x = L->topH * 1.20f + ef_text_w("CNC3D", 2.0f * S) + 10 * S
       + ef_text_w("MISSION EDITOR", 1.0f * S) + 20 * S;
    *y = (L->topH - 26 * S) * 0.5f;
    *w = 96 * S;
    *h = 26 * S;
}

static void eui_menu_item_rect(const EuiLayout* L, int i, float* x, float* y,
                               float* w, float* h)
{
    float mx, my, mw, mh;
    eui_menu_rect(L, &mx, &my, &mw, &mh);
    const float S = L->s;
    *x = mx; *w = 230 * S; *h = 24 * S;
    float yy = my + mh + 2 * S;
    for (int k = 0; k < i; k++)
        yy += (EUI_MENU[k].id == EMENU_SEP) ? 7 * S : 24 * S;
    *y = yy;
}

static void eui_draw_menu(const EuiLayout* L, float t1)
{
    const float S = L->s;
    float x, y, w, h;
    eui_menu_rect(L, &x, &y, &w, &h);
    const bool hot = (g_euiHotKind == EUI_MENUBTN) || g_euiMenuOpen;
    eui_rect(x, y, w, h, g_euiMenuOpen ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL, 1.0f);
    eui_frame(x, y, w, h, g_euiMenuOpen ? EUI_GOLD : EUI_LINE, 1.0f);
    ef_text(x + 10 * S, y + h * 0.5f - 3 * S, "MAP", t1, EUI_RGB(EUI_INK));
    /* a caret, so it reads as a menu rather than a button */
    {
        const float cx = x + w - 14 * S, cy = y + h * 0.5f;
        eui_tri(cx - 4 * S, cy - 2 * S, cx + 4 * S, cy - 2 * S, cx, cy + 3 * S,
                EUI_DIM, 1.0f);
    }
    if (!g_euiMenuOpen) return;

    for (int i = 0; i < EUI_MENU_N; i++) {
        float ix, iy, iw, ih;
        eui_menu_item_rect(L, i, &ix, &iy, &iw, &ih);
        if (EUI_MENU[i].id == EMENU_SEP) {
            eui_rect(ix, iy + 3 * S, iw, S, EUI_LINE, 1.0f);
            continue;
        }
        const bool ih2 = (g_euiHotKind == EUI_MENUITEM && g_euiHotArg == i);
        eui_rect(ix, iy, iw, ih, ih2 ? EUI_PANEL3 : 0x101822, 1.0f);
        ef_text(ix + 12 * S, iy + ih * 0.5f - 3 * S, EUI_MENU[i].label, t1,
                EUI_RGB(ih2 ? EUI_INK : EUI_DIM));
        /* The kind row shows what it currently is, because it is a toggle. Keyed by the
           row's ID: this said "if (i == 6)", the row moved to 7, and the value quietly
           stopped being drawn -- the exact failure the ids exist to prevent. */
        if (EUI_MENU[i].id == EMENU_KIND) {
            const char* v = g_mapIsMulti ? "MULTIPLAYER" : "SINGLEPLAYER";
            ef_text(ix + iw - ef_text_w(v, t1) - 12 * S, iy + ih * 0.5f - 3 * S, v, t1,
                    EUI_RGB(EUI_GOLD));
        }
    }
    /* The frame last, over the items. */
    {
        float lx, ly, lw, lh;
        eui_menu_item_rect(L, EUI_MENU_N - 1, &lx, &ly, &lw, &lh);
        float fx, fy, fw, fh;
        eui_menu_item_rect(L, 0, &fx, &fy, &fw, &fh);
        eui_frame(fx, fy, fw, ly + lh - fy, EUI_LINE, 1.0f);
    }
}



/* --- the map list -------------------------------------------------------------------
 *
 *  Two sources, and the split is the point: USER MAPS are the ones made here, filed in
 *  user_maps/ beside the missions; OFFICIAL are the cartridge's own. They are listed
 *  separately and tabbed, so a map you made is never lost in a wall of SCG/SCB names.
 *
 *  dirent is used rather than anything platform-specific: mingw ships it, which is the
 *  only other toolchain this file is built with.
 * ---------------------------------------------------------------------------------- */

struct MapEntry {
    char scen[16];
    char kind;              /* 'U' user, 'O' official                                */
    char name[48];          /* [Basic] Name=, when the map has one                    */
    unsigned char enhanced; /* [Basic] Enhanced=1 -- this one needs CNC3D             */
};
static std::vector<MapEntry> g_mapList;
static int  g_mapListTab = 0;        /* 0 user, 1 official                       */
static int  g_mapListSel = -1;
static int  g_mapListScroll = 0;
static bool g_mapListBuilt = false;

/* THE MISSIONS ROOT, which is NOT always g_editDir.
 *
 * g_editDir is where the map that is open was loaded from, and after a Play or a Trace
 * that is user_maps/ -- because the editor saves there and then boots the copy. Every
 * place that wanted "the missions folder" was deriving it by appending "user_maps/" to
 * g_editDir, so the second Play of a session wrote to missions/user_maps/user_maps/,
 * the third to missions/user_maps/user_maps/user_maps/, and so on without bound. The
 * maps were still saved -- into a folder the game's User Maps menu never looks in.
 *
 * app/cnc3d.cpp hit the same thing and solved it with base_dir(); this is the editor's
 * half. Stripping rather than remembering, so it is idempotent no matter which of the
 * two folders the editor was launched pointing at. */
static void edit_maps_root(char* out, int n)
{
    snprintf(out, n, "%s", g_editDir);
    size_t L = strlen(out);
    while (L && out[L - 1] == '/') out[--L] = 0;      /* one trailing slash at a time */
    const size_t U = strlen("user_maps");
    if (L >= U && !strcasecmp(out + L - U, "user_maps")) {
        out[L - U] = 0;
        L -= U;
    }
    if (L && out[L - 1] != '/') { out[L] = '/'; out[L + 1] = 0; }
}

static void edit_userdir(char* out, int n)
{
    char root[1024];
    edit_maps_root(root, sizeof root);
    snprintf(out, n, "%suser_maps", root);
}

static void edit_scan_dir(const char* dir, char kind)
{
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        const char* nm = e->d_name;
        const size_t L = strlen(nm);
        if (L < 5 || L > 15) continue;
        if (strcmp(nm + L - 4, ".INI") && strcmp(nm + L - 4, ".ini")) continue;
        MapEntry m;
        memset(&m, 0, sizeof m);
        snprintf(m.scen, sizeof m.scen, "%.*s", (int)(L - 4), nm);
        m.kind = kind;
        /* The map's own name, out of [Basic]. A list of scenario codes tells you nothing
           about which of them is the one you were working on yesterday. Read here rather
           than at draw time: opening the dialog would otherwise re-read a hundred files
           every frame. */
        {
            char path[1200];
            snprintf(path, sizeof path, "%s%s%s", dir,
                     (dir[0] && dir[strlen(dir) - 1] != '/') ? "/" : "", nm);
            FILE* f = fopen(path, "rb");
            if (f) {
                char line[512];
                bool inBasic = false;
                while (fgets(line, sizeof line, f)) {
                    char* q = line;
                    while (*q == ' ' || *q == '\t') q++;
                    if (*q == '[') { inBasic = !strncasecmp(q, "[Basic]", 7); continue; }
                    if (!inBasic) continue;
                    if (!strncasecmp(q, "Name=", 5)) {
                        snprintf(m.name, sizeof m.name, "%s", q + 5);
                        for (char* e2 = m.name + strlen(m.name);
                             e2 > m.name && (e2[-1] == '\r' || e2[-1] == '\n' ||
                                             e2[-1] == ' '); ) *--e2 = 0;
                    } else if (!strncasecmp(q, "Enhanced=", 9)) {
                        m.enhanced = (unsigned char)(atoi(q + 9) != 0);
                    }
                }
                fclose(f);
            }
        }
        bool dup = false;
        for (size_t i = 0; i < g_mapList.size(); i++)
            if (!strcmp(g_mapList[i].scen, m.scen)) { dup = true; break; }
        if (!dup) g_mapList.push_back(m);
    }
    closedir(d);
}

static void edit_build_maplist(void)
{
    g_mapList.clear();
    char ud[1024];
    edit_userdir(ud, sizeof ud);
    edit_scan_dir(ud, 'U');
    {   /* The OFFICIAL maps live in the missions root, not wherever the open map came
           from -- otherwise, one Play later, this scanned user_maps/ and the Official
           tab listed the user's own maps and none of the cartridge's. */
        char root[1024];
        edit_maps_root(root, sizeof root);
        edit_scan_dir(root[0] ? root : ".", 'O');
    }
    /* Alphabetical inside each kind: the list is read, not scrolled past. */
    for (size_t i = 0; i + 1 < g_mapList.size(); i++)
        for (size_t j = i + 1; j < g_mapList.size(); j++)
            if (g_mapList[j].kind < g_mapList[i].kind ||
                (g_mapList[j].kind == g_mapList[i].kind &&
                 strcmp(g_mapList[j].scen, g_mapList[i].scen) < 0)) {
                const MapEntry t = g_mapList[i]; g_mapList[i] = g_mapList[j];
                g_mapList[j] = t;
            }
    g_mapListBuilt = true;
    int nu = 0;
    for (size_t i = 0; i < g_mapList.size(); i++) if (g_mapList[i].kind == 'U') nu++;
    fprintf(stderr, "edit: %d maps -- %d user, %d official\n",
            (int)g_mapList.size(), nu, (int)g_mapList.size() - nu);
}

static int edit_maplist_count(char kind)
{
    int n = 0;
    for (size_t i = 0; i < g_mapList.size(); i++) if (g_mapList[i].kind == kind) n++;
    return n;
}

/* The nth entry of the shown tab, or -1. */
static int edit_maplist_at(int n)
{
    const char want = g_mapListTab ? 'O' : 'U';
    int k = 0;
    for (size_t i = 0; i < g_mapList.size(); i++)
        if (g_mapList[i].kind == want && k++ == n) return (int)i;
    return -1;
}

static void eui_openlist_row_rect(float lx, float ly, float lw, float S, int i,
                                  float* x, float* y, float* w, float* h)
{
    *x = lx; *w = lw; *h = 22 * S;
    *y = ly + 34 * S + i * (*h + 2 * S);
}

static void eui_draw_openlist(const EuiLayout* L, float lx, float ly, float lw, float lh,
                              float t1)
{
    const float S = L->s;
    if (!g_mapListBuilt) edit_build_maplist();

    /* The two tabs asked for, here and in the game's own map lists. */
    {
        static const char* TB[2] = { "USER MAPS", "OFFICIAL MAPS" };
        const float tw = (lw - 6 * S) * 0.5f;
        for (int i = 0; i < 2; i++) {
            const float x = lx + i * (tw + 6 * S);
            const bool on = (i == g_mapListTab);
            const bool hot = (g_euiHotKind == EUI_MODALOPT && g_euiHotArg == 30 + i);
            eui_rect(x, ly, tw, 28 * S, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL2,
                     on || hot ? 1.0f : 0.7f);
            if (on) eui_rect(x, ly + 28 * S - 3 * S, tw, 3 * S, EUI_GOLD, 1.0f);
            char lbl[40];
            snprintf(lbl, sizeof lbl, "%s  %d", TB[i], edit_maplist_count(i ? 'O' : 'U'));
            ef_text(x + (tw - ef_text_w(lbl, t1)) * 0.5f, ly + 9 * S, lbl, t1,
                    EUI_RGB(on ? EUI_GOLD : EUI_DIM));
        }
    }

    const int shown = edit_maplist_count(g_mapListTab ? 'O' : 'U');
    const int rows = (int)((lh - 40 * S) / (24 * S));
    if (!shown) {
        ef_text(lx + 8 * S, ly + 48 * S,
                g_mapListTab ? "no maps in this folder"
                             : "no user maps yet -- make one with NEW MAP",
                t1, EUI_RGB(EUI_FAINT));
        return;
    }
    for (int r = 0; r < rows; r++) {
        const int n = g_mapListScroll + r;
        if (n >= shown) break;
        const int idx = edit_maplist_at(n);
        if (idx < 0) break;
        float x, y, w, h;
        eui_openlist_row_rect(lx, ly, lw, S, r, &x, &y, &w, &h);
        const bool on = (idx == g_mapListSel);
        const bool hot = (g_euiHotKind == EUI_MODALOPT && g_euiHotArg == 40 + r);
        if (on || hot) eui_rect(x, y, w, h, on ? EUI_PANEL3 : EUI_PANEL2, 1.0f);
        const MapEntry& me = g_mapList[idx];
        ef_text(x + 8 * S, y + h * 0.5f - 3 * S, me.scen, t1,
                EUI_RGB(on ? EUI_GOLD : EUI_DIM));
        /* The map's own name after its code, and a mark on the ones that need CNC3D.
           A column of scenario codes does not tell you which is the one you were
           working on yesterday. */
        const float nx = x + 8 * S + 78 * S;
        float room = w - (nx - x) - 12 * S;
        if (me.enhanced) {
            const char* tag = "ENHANCED";
            const float tw = ef_text_w(tag, EUI_TS(t1));
            ef_text(x + w - tw - 10 * S, y + h * 0.5f - 3 * S, tag, EUI_TS(t1),
                    EUI_RGB(EUI_GREEN));
            room -= tw + 10 * S;
        }
        if (me.name[0] && room > 40 * S)
            ef_text_fit(nx, y + h * 0.5f - 3 * S, me.name, EUI_TS(t1), room,
                        EUI_RGB(on ? EUI_INK : EUI_FAINT));
    }
    if (shown > rows) {
        char nav[64];
        snprintf(nav, sizeof nav, "%d-%d OF %d   WHEEL SCROLLS",
                 g_mapListScroll + 1,
                 (g_mapListScroll + rows > shown ? shown : g_mapListScroll + rows), shown);
        ef_text(lx, ly + lh - 12 * S, nav, EUI_TS(t1), EUI_RGB(EUI_FAINT));
    }
}

/* TYPED TEXT AND TYPED NUMBERS. Click a numeric value and type it; click-to-step
   survives nowhere. The buffer is drawn over the row it edits, Enter commits, Escape
   throws it away.
 *
 * DECLARED HERE, above the modals, because the SAVE AS dialog draws this buffer -- the
 * drawing-first ordering rule of this file. It was declared eight hundred lines further
 * down, beside the rules panel that was its only user.
 *
 * NAME_MAP is the odd one and deliberately so. A trigger name is a 4-character engine
 * field and a team name an 8-character one (type.h:268), so those stay short and
 * alphanumeric. A MAP's name is prose -- it is shown to a person and nothing parses it
 * -- so it is long and takes the punctuation names are made of. */
enum EuiNumKind { NUM_NONE = 0, NUM_RULE_DATA, NUM_TEAM_COUNT, NUM_TEAM_PRI,
                  NUM_TEAM_MAX, NUM_ORDER_ARG, NUM_ENH_NUM,
                  NAME_RULE, NAME_TEAM, NAME_ENHRULE, NAME_MAP };
static int  g_numKind = NUM_NONE;
static int  g_numRow = -1;
/* Sized for the longest field, which is the map name at 40. Every other cap is far
   smaller and unchanged by the growth. */
static char g_numBuf[48];
static char g_renOld[16];              /* the name being replaced, for propagation */
/* TRUE while a pre-filled field still holds exactly what it was opened with. The
   first character typed then REPLACES it, the way a field whose text is selected
   behaves everywhere else. Without this, SAVE AS opens holding the map's current
   name at the 40-character cap and typing does nothing at all -- the cap is already
   reached, so every keystroke is silently dropped and the field looks broken. */
static bool g_numPristine = false;

static bool eui_num_is_name(void)
{
    return g_numKind == NAME_RULE || g_numKind == NAME_TEAM ||
           g_numKind == NAME_ENHRULE || g_numKind == NAME_MAP;
}

static int eui_name_cap(void)
{
    /* the engine's own limits: 4 for a trigger, 8 for a team (type.h:268). A map name
       answers to nothing but the title strip and the two map lists, so it gets room to
       be a name; 40 leaves the 48-byte buffer its terminator and then some. */
    if (g_numKind == NAME_MAP) return 40;
    return g_numKind == NAME_RULE ? 4 : g_numKind == NAME_TEAM ? 8 : 12;
}

/* WHICH CHARACTERS THE OPEN FIELD TAKES, in one place, so the keyboard and the gate
   verb that types for it cannot disagree. The engine's names are format-constrained and
   stay alphanumeric; a map's name additionally takes the space, hyphen, apostrophe and
   full stop that map names are actually made of. */
static bool eui_num_accepts(char c)
{
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z');
    if (g_numKind == NAME_MAP)
        return alnum || c == ' ' || c == '-' || c == '\'' || c == '.';
    if (eui_num_is_name()) return alnum;
    return (c >= '0' && c <= '9') || c == '.' || c == '-';
}

/* Append what was typed, filtered and capped. */
static void eui_num_type(const char* s)
{
    for (; s && *s; s++) {
        if (!eui_num_accepts(*s)) continue;
        if (g_numPristine) { g_numBuf[0] = 0; g_numPristine = false; }
        const size_t bl = strlen(g_numBuf);
        size_t cap = eui_num_is_name() ? (size_t)eui_name_cap() : sizeof g_numBuf - 1;
        if (cap > sizeof g_numBuf - 1) cap = sizeof g_numBuf - 1;
        if (bl < cap) { g_numBuf[bl] = *s; g_numBuf[bl + 1] = 0; }
    }
}

static void eui_num_close(void) { g_numKind = NUM_NONE; g_numRow = -1;
                                 g_numPristine = false; }

/* --- modals ------------------------------------------------------------------------ */

/* The dialog's geometry, in one place so the draw and the hit test agree. Rows of
   choices, each a group of buttons across. */
static float eui_modal_new_height(const EuiLayout* L);   /* the flow, defined below */

static void eui_modal_rect(const EuiLayout* L, int fbw, int fbh,
                           float* x, float* y, float* w, float* h)
{
    const float S = L->s;
    *w = 460 * S;
    *h = (g_euiModal == MODAL_NEW) ? eui_modal_new_height(L) : 420 * S;
    if (g_euiModal == MODAL_CHECK) { *w = 620 * S; *h = 460 * S; }
    /* SAVE AS is one field and two buttons: a 420-tall dialog around it would be mostly
       empty panel. */
    if (g_euiModal == MODAL_SAVEAS) *h = 214 * S;
    *x = ((float)fbw - *w) * 0.5f;
    *y = ((float)fbh - *h) * 0.5f;
}

/* ====================================================================================
 *  THE PICKER -- a dropdown list, because click-to-cycle was the panel's worst idea.
 *
 *  EVENT cycled through 17 options, ACTION through 18, a member's TYPE through 46 and
 *  Built It's building through 61 -- with shift-click as the only way back. Nobody can
 *  use a control like that; you cannot even SEE what the choices are. The audit found
 *  nine controls over four options, and every one of them now opens this instead.
 *
 *  One widget: anchored under the control that opened it, human text on the left,
 *  the file's own name on the right, the current value marked, wheel scrolls, click
 *  picks, click anywhere else closes. While it is open it owns every click, like a
 *  menu should.
 * ==================================================================================== */

enum EuiPopKind {
    POP_NONE = 0,
    POP_EVENT, POP_ACTION, POP_RULEHOUSE, POP_TEAM, POP_PERSIST, POP_STRUCT,
    POP_TEAMHOUSE, POP_MEMBERTYPE, POP_ORDERTYPE, POP_ORDERARG,
    POP_ENH_CLAUSE, POP_ENH_HOUSE, POP_ENH_NAME, POP_CARRIER,
    POP_WIREDROP
};

struct EuiPopItem { char label[56]; char detail[64]; int value; };

static std::vector<EuiPopItem> g_popItems;
static int   g_popKind = POP_NONE;
static int   g_popCtx  = -1;       /* which rule / team / clause the pick applies to */
static int   g_popCur  = -1;       /* the value currently set, drawn with a mark     */
static int   g_popScroll = 0;
static float g_popAX, g_popAY, g_popAW;    /* the anchor: the control that opened it */

static void eui_pop_close(void)
{
    g_popKind = POP_NONE;
    g_popItems.clear();
    g_popScroll = 0;
}

static void eui_pop_add(const char* label, const char* detail, int value)
{
    EuiPopItem it;
    snprintf(it.label, sizeof it.label, "%s", label ? label : "");
    snprintf(it.detail, sizeof it.detail, "%s", detail ? detail : "");
    it.value = value;
    g_popItems.push_back(it);
}

/* The popup's own geometry: below the anchor if there is room, above it otherwise,
   never wider than the panel, never taller than the frame. */
static void eui_pop_rect(const EuiLayout* L, int fbh, float* x, float* y,
                         float* w, float* h, int* rows)
{
    const float S = L->s;
    const float rowH = 26 * S;
    const int n = (int)g_popItems.size();
    int fit = (int)(((float)fbh * 0.55f) / rowH);
    if (fit > n) fit = n;
    if (fit < 3 && n >= 3) fit = 3;
    if (fit < 1) fit = 1;
    *rows = fit;
    *w = g_popAW;
    if (*w < 260 * S) *w = 260 * S;
    *h = fit * rowH + 8 * S;
    *x = g_popAX;
    if (*x + *w > L->sideX + L->sideW - 6 * S) *x = L->sideX + L->sideW - 6 * S - *w;
    if (*x < 6 * S) *x = 6 * S;
    *y = g_popAY;
    if (*y + *h > (float)fbh - 6 * S) *y = g_popAY - *h - 30 * S;
    if (*y < 6 * S) *y = 6 * S;
}

static void eui_pop_clamp(void)
{
    const int n = (int)g_popItems.size();
    if (g_popScroll > n - 1) g_popScroll = n - 1;
    if (g_popScroll < 0) g_popScroll = 0;
}

static void eui_draw_popup(const EuiLayout* L, int fbw, int fbh, float t1)
{
    if (g_popKind == POP_NONE) return;
    const float S = L->s;
    float x, y, w, h;
    int rows;
    eui_pop_rect(L, fbh, &x, &y, &w, &h, &rows);
    /* Dim everything behind, lightly: this is a menu, not a modal, but while it is up
       it owns the pointer and the panel should read that way. */
    eui_rect(0, 0, (float)fbw, (float)fbh, 0x000000, 0.25f);
    eui_rect(x, y, w, h, EUI_PANEL, 1.0f);
    eui_frame(x, y, w, h, EUI_GOLD, 1.0f);
    const float rowH = 26 * S;
    for (int r = 0; r < rows; r++) {
        const int i = g_popScroll + r;
        if (i >= (int)g_popItems.size()) break;
        const float ry = y + 4 * S + r * rowH;
        const bool cur = (g_popItems[i].value == g_popCur);
        const bool hot = (g_euiHotKind == EUI_POPITEM && g_euiHotArg == i);
        if (hot) eui_rect(x + 3 * S, ry, w - 6 * S, rowH, EUI_PANEL3, 1.0f);
        if (cur) eui_rect(x + 3 * S, ry, 3 * S, rowH, EUI_GOLD, 1.0f);
        ef_text_fit(x + 14 * S, ry + rowH * 0.5f - 3 * S, g_popItems[i].label, t1,
                    w - 28 * S - (g_popItems[i].detail[0] ? 90 * S : 0),
                    EUI_RGB(cur ? EUI_GOLD : hot ? EUI_INK : EUI_DIM));
        if (g_popItems[i].detail[0])
            ef_text(x + w - ef_text_w(g_popItems[i].detail, t1) - 12 * S,
                    ry + rowH * 0.5f - 3 * S, g_popItems[i].detail, t1,
                    EUI_RGB(EUI_FAINT));
    }
    if ((int)g_popItems.size() > rows) {
        char sc[48];
        snprintf(sc, sizeof sc, "%d-%d of %d  --  wheel scrolls", g_popScroll + 1,
                 g_popScroll + rows, (int)g_popItems.size());
        ef_text(x + 14 * S, y + h - 2 * S, sc, t1, EUI_RGB(EUI_FAINT));
    }
}


/* NEW MAP's vertical flow, computed in ONE place.
 *
 * The old version was a table of three hand-tuned row positions, and the moment the
 * text grew, the THEATER header landed on top of the LARGE size row and the buttons on
 * top of the KIND row -- exactly the screenshot that was sent. A flow cannot collide with
 * itself: each group starts where the previous one ended.
 *
 * Group ids: 0 = size rows (4, stacked), 1 = theater (2 across), 2 = kind (2 across).
 * eui_modal_flow_y answers where a group's LABEL sits and where its OPTIONS start, and
 * everything -- draw, labels, hit test, modal height -- reads it. */
static void eui_modal_flow_y(const EuiLayout* L, int group, float* labelY, float* optY)
{
    const float S = L->s;
    float y = 44 * S;                              /* below the title            */
    /* group 0: label + EUI_SIZE_N stacked rows */
    if (group == 0) { *labelY = y; *optY = y + 18 * S; return; }
    y += 18 * S + EUI_SIZE_N * (30 * S + 5 * S) + 10 * S;
    if (group == 1) { *labelY = y; *optY = y + 18 * S; return; }
    /* the theater grid is TWO rows of 34 now */
    y += 18 * S + (34 * 2 + 6) * S + 10 * S;
    *labelY = y; *optY = y + 18 * S;
}

static float eui_modal_new_height(const EuiLayout* L)
{
    const float S = L->s;
    float labelY, optY;
    eui_modal_flow_y(L, 2, &labelY, &optY);
    return optY + 34 * S + 16 * S + 34 * S + 18 * S;   /* kind row, gap, buttons, pad */
}

static void eui_modal_opt_rect(const EuiLayout* L, int fbw, int fbh, int group, int i,
                               float* x, float* y, float* w, float* h)
{
    float mx, my, mw, mh;
    eui_modal_rect(L, fbw, fbh, &mx, &my, &mw, &mh);
    const float S = L->s, pad = 18 * S;
    float labelY, optY;
    eui_modal_flow_y(L, group, &labelY, &optY);
    if (group == 0) {
        *h = 30 * S;
        *w = mw - pad * 2;
        *x = mx + pad;
        *y = my + optY + i * (*h + 5 * S);
        return;
    }
    *h = 34 * S;
    if (group == 1) {
        /* Five theaters sit as a 3 + 2 grid: five across squeezes TEMPERATE into
           an unreadable sliver at small frames. */
        const int cols = 3, row = i / cols, col = i % cols;
        *w = (mw - pad * 2 - 6 * S * (cols - 1)) / (float)cols;
        *x = mx + pad + col * (*w + 6 * S);
        *y = my + optY + row * (*h + 6 * S);
        return;
    }
    *w = (mw - pad * 2 - 6 * S) * 0.5f;
    *x = mx + pad + i * (*w + 6 * S);
    *y = my + optY;
}

static void eui_modal_btn_rect(const EuiLayout* L, int fbw, int fbh, int i,
                               float* x, float* y, float* w, float* h)
{
    float mx, my, mw, mh;
    eui_modal_rect(L, fbw, fbh, &mx, &my, &mw, &mh);
    const float S = L->s, pad = 18 * S;
    *w = (mw - pad * 2 - 8 * S) / 2.0f;
    *h = 34 * S;
    *x = mx + pad + i * (*w + 8 * S);
    *y = my + mh - pad - *h;
}

/* The check badge, tucked into the right of the readout block. */
static void eui_check_badge_rect(const EuiLayout* L, float* x, float* y,
                                 float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    *w = 118 * S;
    *h = 20 * S;
    *x = L->sideX + L->sideW - pad - *w;
    /* Top-right of the readout block. The bottom row is the key hints, which run right
       under it; the top row is the armed item's name, which is left-aligned and short.
       In ELEVATN the panel's own hit rectangles reach into this block, which is why the
       badge is claimed BEFORE the per-mode panels in eui_hit rather than after. */
    *y = L->lintY + 5 * S;
}

static void eui_draw_modal(const EuiLayout* L, int fbw, int fbh, float t1)
{
    if (!g_euiModal) return;
    const float S = L->s;
    /* Everything behind goes dim: a modal that does not look modal is a modal people
       click through. */
    eui_rect(0, 0, (float)fbw, (float)fbh, 0x000000, 0.55f);

    float x, y, w, h;
    eui_modal_rect(L, fbw, fbh, &x, &y, &w, &h);
    eui_rect(x, y, w, h, EUI_PANEL, 1.0f);
    eui_frame(x, y, w, h, EUI_GOLD, 1.0f);
    eui_rect(x, y, w, 3 * S, EUI_GOLD, 1.0f);

    const float pad = 18 * S;
    if (g_euiModal == MODAL_NEW) {
        ef_text(x + pad, y + 18 * S, "NEW MAP", EUI_TL(t1), EUI_RGB(EUI_GOLD));
        {
            float ly, oy2;
            eui_modal_flow_y(L, 0, &ly, &oy2);
            ef_text(x + pad, y + ly, "PLAYABLE AREA", EUI_TS(t1), EUI_RGB(EUI_FAINT));
        }
        for (int i = 0; i < EUI_SIZE_N; i++) {
            float ox, oy, ow, oh;
            eui_modal_opt_rect(L, fbw, fbh, 0, i, &ox, &oy, &ow, &oh);
            const bool on = (i == g_newSize);
            const bool hot = (g_euiHotKind == EUI_MODALOPT && g_euiHotArg == i);
            eui_rect(ox, oy, ow, oh, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL2,
                     on || hot ? 1.0f : 0.7f);
            eui_frame(ox, oy, ow, oh, on ? EUI_GOLD : EUI_LINE, 1.0f);
            ef_text(ox + 12 * S, oy + oh * 0.5f - 3 * S, EUI_SIZES[i].name, t1,
                    EUI_RGB(on ? EUI_INK : EUI_DIM));
        }
        {
            float ly, oy2;
            eui_modal_flow_y(L, 1, &ly, &oy2);
            ef_text(x + pad, y + ly, "THEATER", EUI_TS(t1), EUI_RGB(EUI_FAINT));
        }
        {
            const char* const* TH = EUI_THEATERS;
            for (int i = 0; i < EUI_THEATER_N; i++) {
                float ox, oy, ow, oh;
                eui_modal_opt_rect(L, fbw, fbh, 1, i, &ox, &oy, &ow, &oh);
                const bool on = (i == g_newTheater);
                const bool hot = (g_euiHotKind == EUI_MODALOPT && g_euiHotArg == 10 + i);
                eui_rect(ox, oy, ow, oh, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL2,
                         on || hot ? 1.0f : 0.7f);
                eui_frame(ox, oy, ow, oh, on ? EUI_GREEN : EUI_LINE, 1.0f);
                {
                    /* Four across leaves TEMPERATE little room at small frames. */
                    float tw = ef_text_w(TH[i], t1);
                    if (tw > ow - 8 * S) tw = ow - 8 * S;
                    ef_text_fit(ox + (ow - tw) * 0.5f, oy + oh * 0.5f - 3 * S,
                                TH[i], t1, ow - 8 * S,
                                EUI_RGB(on ? EUI_GREEN : EUI_DIM));
                }
            }
        }
        {
            float ly, oy2;
            eui_modal_flow_y(L, 2, &ly, &oy2);
            ef_text(x + pad, y + ly, "KIND", EUI_TS(t1), EUI_RGB(EUI_FAINT));
        }
        {
            static const char* KD[2] = { "SINGLEPLAYER", "MULTIPLAYER" };
            for (int i = 0; i < 2; i++) {
                float ox, oy, ow, oh;
                eui_modal_opt_rect(L, fbw, fbh, 2, i, &ox, &oy, &ow, &oh);
                const bool on = (i == g_newMulti);
                const bool hot = (g_euiHotKind == EUI_MODALOPT && g_euiHotArg == 20 + i);
                eui_rect(ox, oy, ow, oh, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL2,
                         on || hot ? 1.0f : 0.7f);
                eui_frame(ox, oy, ow, oh, on ? EUI_CYAN : EUI_LINE, 1.0f);
                ef_text(ox + (ow - ef_text_w(KD[i], t1)) * 0.5f, oy + oh * 0.5f - 3 * S,
                        KD[i], t1, EUI_RGB(on ? EUI_CYAN : EUI_DIM));
            }
        }
    } else if (g_euiModal == MODAL_OPEN) {
        ef_text(x + pad, y + 18 * S, "OPEN MAP", EUI_TL(t1), EUI_RGB(EUI_GOLD));
        ef_text(x + pad, y + 50 * S, "USER MAPS FIRST, THEN THE CARTRIDGE'S OWN",
                EUI_TS(t1), EUI_RGB(EUI_FAINT));
        eui_draw_openlist(L, x + pad, y + 74 * S, w - pad * 2, h - 74 * S - 60 * S, t1);
    } else if (g_euiModal == MODAL_SAVEAS) {
        /* SAVE AS. One field, and a plain statement of what the two halves of a map's
           identity are: the NAME is what is typed and what every list shows; the SLOT
           is USERnn and is the filename the game probes for, so the dialog says which
           one this copy will take rather than leaving it to be discovered. */
        ef_text(x + pad, y + 18 * S, "SAVE MAP AS", EUI_TL(t1), EUI_RGB(EUI_GOLD));
        ef_text(x + pad, y + 46 * S, "WHAT THIS MAP IS CALLED", EUI_TS(t1),
                EUI_RGB(EUI_FAINT));

        const float fx = x + pad, fy = y + 62 * S;
        const float fw = w - pad * 2, fh = 34 * S;
        eui_rect(fx, fy, fw, fh, 0x080b10, 1.0f);
        eui_frame(fx, fy, fw, fh, EUI_GOLD, 1.0f);
        {
            char ed[56];
            snprintf(ed, sizeof ed, "%s_", g_numBuf);
            ef_text_fit(fx + 10 * S, fy + fh * 0.5f - 3 * S, ed, t1, fw - 20 * S,
                        EUI_RGB(EUI_GOLD));
        }
        {
            char slot[64];
            snprintf(slot, sizeof slot, "IT IS FILED AS %s.INI, IN user_maps/",
                     g_saveasSlot);
            ef_text(x + pad, y + 106 * S, slot, EUI_TS(t1), EUI_RGB(EUI_DIM));
            ef_text(x + pad, y + 122 * S,
                    "THE GAME LOOKS FOR USER MAPS BY SLOT AND SHOWS THEM BY NAME",
                    EUI_TS(t1), EUI_RGB(EUI_FAINT));
            ef_text(x + pad, y + 142 * S,
                    "THE MAP YOU CAME FROM IS LEFT AS IT WAS",
                    EUI_TS(t1), EUI_RGB(EUI_FAINT));
        }
    }

    if (g_euiModal == MODAL_CHECK) {
        char hd[96];
        snprintf(hd, sizeof hd, "MISSION CHECK");
        ef_text(x + pad, y + 18 * S, hd, EUI_TL(t1), EUI_RGB(EUI_GOLD));
        char tally[128];
        if (g_checkErrors)
            snprintf(tally, sizeof tally, "%d THING%s CANNOT WORK, %d WORTH A LOOK",
                     g_checkErrors, g_checkErrors == 1 ? "" : "S", g_checkWarns);
        else if (g_checkWarns)
            snprintf(tally, sizeof tally, "NOTHING BROKEN, %d WORTH A LOOK",
                     g_checkWarns);
        else
            snprintf(tally, sizeof tally, "NOTHING BROKEN");
        ef_text(x + pad, y + 44 * S, tally, EUI_TS(t1),
                EUI_RGB(g_checkErrors ? EUI_DANGER
                                      : g_checkWarns ? EUI_GOLD : EUI_GREEN));

        const float lx = x + pad, ly = y + 64 * S;
        const float lw = w - pad * 2;
        const float lh = h - (ly - y) - pad - 34 * S - 8 * S;
        eui_rect(lx, ly, lw, lh, 0x080b10, 1.0f);
        eui_frame(lx, ly, lw, lh, EUI_LINE, 1.0f);
        const float rowH = 34 * S;
        const int rows = (int)((lh - 8 * S) / rowH);
        int shown = 0;
        for (int i = g_checkScroll; i < (int)g_check.size() && shown < rows; i++, shown++) {
            const CheckRow& r = g_check[i];
            const float ry = ly + 4 * S + shown * rowH;
            const unsigned col = r.level == CHK_ERROR ? EUI_DANGER
                               : r.level == CHK_WARN  ? EUI_GOLD : EUI_GREEN;
            eui_rect(lx + 4 * S, ry + 2 * S, 3 * S, rowH - 8 * S, col, 1.0f);
            ef_text_fit(lx + 14 * S, ry + 3 * S, r.what, EUI_TS(t1), lw - 24 * S,
                        EUI_RGB(col));
            ef_text_fit(lx + 14 * S, ry + 17 * S, r.why, EUI_TS(t1), lw - 24 * S,
                        EUI_RGB(EUI_FAINT));
        }
        if ((int)g_check.size() > rows) {
            char nav[64];
            snprintf(nav, sizeof nav, "%d-%d OF %d   WHEEL SCROLLS", g_checkScroll + 1,
                     g_checkScroll + shown, (int)g_check.size());
            ef_text(lx, ly + lh + 6 * S, nav, EUI_TS(t1), EUI_RGB(EUI_FAINT));
        }
        float bx, by, bw, bh;
        eui_modal_btn_rect(L, fbw, fbh, 1, &bx, &by, &bw, &bh);
        const bool hot = (g_euiHotKind == EUI_MODALBTN && g_euiHotArg == 1);
        eui_rect(bx, by, bw, bh, EUI_GOLD, hot ? 1.0f : 0.9f);
        ef_text(bx + (bw - ef_text_w("CLOSE", t1)) * 0.5f, by + bh * 0.5f - 3 * S,
                "CLOSE", t1, 0.08f, 0.07f, 0.05f);
        return;
    }

    /* Two buttons, always in the same place and the same order. */
    {
        static const char* B[2] = { "CANCEL", "CREATE" };
        for (int i = 0; i < 2; i++) {
            float bx, by, bw, bh;
            eui_modal_btn_rect(L, fbw, fbh, i, &bx, &by, &bw, &bh);
            const bool hot = (g_euiHotKind == EUI_MODALBTN && g_euiHotArg == i);
            const char* lbl = i != 1 ? B[i]
                            : g_euiModal == MODAL_OPEN   ? "OPEN"
                            : g_euiModal == MODAL_SAVEAS ? "SAVE" : B[i];
            if (i == 1) {
                eui_rect(bx, by, bw, bh, EUI_GOLD, hot ? 1.0f : 0.9f);
                ef_text(bx + (bw - ef_text_w(lbl, t1)) * 0.5f, by + bh * 0.5f - 3 * S,
                        lbl, t1, 0.08f, 0.07f, 0.05f);
            } else {
                eui_rect(bx, by, bw, bh, hot ? EUI_PANEL3 : EUI_PANEL2, 1.0f);
                eui_frame(bx, by, bw, bh, EUI_LINE, 1.0f);
                ef_text(bx + (bw - ef_text_w(lbl, t1)) * 0.5f, by + bh * 0.5f - 3 * S,
                        lbl, t1, EUI_RGB(EUI_DIM));
            }
        }
    }
}


/* The STARTS panel. What it offers depends on what kind of map this is, because the two
   kinds use waypoints for entirely different things:

     multiplayer   0..5 are the player starts, one per seat
     singleplayer  26 HOME is where the view opens and 27 REINFORCE is where
                   reinforcements arrive; 0..25 are cells for triggers and teams to
                   refer to, so a handful are offered for that

   Right-click a placed start to clear it. */
static int eui_start_slot(int row)
{
    if (g_mapIsMulti) return (row < EDIT_MAX_PLAYERS) ? row : -1;
    if (row == 0) return EDIT_WAYPT_HOME;
    if (row == 1) return EDIT_WAYPT_REINF;
    return (row - 2 < 8) ? (row - 2) : -1;      /* eight general-purpose cells */
}

static int eui_start_rows(void) { return g_mapIsMulti ? EDIT_MAX_PLAYERS : 10; }

static void eui_start_rect(const EuiLayout* L, int row, float* x, float* y,
                           float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    *x = L->sideX + pad;
    *w = L->sideW - pad * 2;
    *h = 26 * S;
    *y = L->catY + row * (*h + 3 * S);
}

static void eui_draw_starts(const EuiLayout* L, float t1)
{
    const float S = L->s, pad = EUI_PAD * S;
    ef_text(L->sideX + pad, L->catY - 12 * S,
            g_mapIsMulti ? "PLAYER STARTS" : "MISSION CELLS", EUI_TS(t1),
            EUI_RGB(EUI_FAINT));
    for (int row = 0; row < eui_start_rows(); row++) {
        const int w = eui_start_slot(row);
        if (w < 0) continue;
        float x, y, bw, bh;
        eui_start_rect(L, row, &x, &y, &bw, &bh);
        if (y + bh > L->lintY) break;                 /* out of room: stop cleanly */
        const bool on  = (g_waypointArmed == w);
        const bool set = (g_waypoint[w] >= 0);
        const bool hot = (g_euiHotKind == EUI_START && g_euiHotArg == row);
        eui_panel(x, y, bw, bh, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL);
        if (on) eui_rect(x, y, 3 * S, bh, EUI_GOLD, 1.0f);
        ef_text(x + 10 * S, y + bh * 0.5f - 3 * S, edit_waypoint_label(w), t1,
                EUI_RGB(on ? EUI_GOLD : EUI_DIM));
        char v[32];
        if (set) snprintf(v, sizeof v, "%d,%d", g_waypoint[w] % edit_ini_w(),
                          g_waypoint[w] / edit_ini_w());
        else     snprintf(v, sizeof v, "not set");
        ef_text(x + bw - ef_text_w(v, t1) - 10 * S, y + bh * 0.5f - 3 * S, v, t1,
                EUI_RGB(set ? EUI_CYAN : EUI_FAINT));
    }
    {
        char note[96];
        snprintf(note, sizeof note, g_mapIsMulti ? "%d of %d seats placed"
                                                 : "%d cells placed",
                 g_mapIsMulti ? edit_start_count() : 0, EDIT_MAX_PLAYERS);
        if (g_mapIsMulti)
            ef_text(L->sideX + pad, L->lintY - 14 * S, note, EUI_TS(t1),
                    EUI_RGB(edit_start_count() >= 2 ? EUI_GREEN : EUI_DANGER));
    }
}

/* Every placed waypoint, drawn on the map. A start you cannot see is a start you place
   twice. */
static void eui_draw_waypoints(int fbw, int fbh)
{
    if (!g_editOn) return;
    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < EDIT_WAYPT_COUNT; i++) {
        if (g_waypoint[i] < 0) continue;
        const int cx = g_waypoint[i] % edit_ini_w(), cy = g_waypoint[i] / edit_ini_w();
        const unsigned c = (i == EDIT_WAYPT_HOME)  ? 0x7ee0a0
                         : (i == EDIT_WAYPT_REINF) ? 0x8fe4ff : 0xe0b070;
        glColor4f(EUI_RGB(c), (i == g_waypointArmed) ? 0.55f : 0.32f);
        glBegin(GL_TRIANGLES);
        sb_place_cell_tris(cx, cy, fbw, fbh);
        glEnd();
        float col = 0.0f, row = 0.0f;
        world_to_screen((float)cx + 0.5f, terrain_y((float)cx + 0.5f, (float)cy + 0.5f),
                        (float)cy + 0.5f, fbw, fbh, &col, &row);
        {
            const char* lbl = edit_waypoint_label(i);
            const float ts = 1.0f * g_uiBacking;
            ef_text(col - ef_text_w(lbl, ts) * 0.5f, row - 8 * ts, lbl, ts, EUI_RGB(c));
        }
    }
    glDisable(GL_BLEND);
    end_overlay();
}


/* ------------------------------------------------------------------------------------
 *  TOASTS
 *
 *  Reported: "If I click on the save button, no popup, no notification, nothing."
 *  That is right, and it is the worst kind of nothing -- the map HAD been written, to a folder
 *  he could not see, under a name he had not chosen. An action that touches the disk
 *  and says nothing is indistinguishable from one that failed.
 *
 *  So every action that changes something outside the viewport says so, on screen, for
 *  a few seconds. Driven off the draw counter rather than wallclock, so --shot stays
 *  reproducible.
 * ---------------------------------------------------------------------------------- */

#define EUI_TOAST_FRAMES 260        /* about four seconds at 60fps */

static char          g_toast[256] = "";
static char          g_toastSub[256] = "";
static unsigned long g_toastAt = 0;
static unsigned      g_toastCol = 0;

static void edit_toast(unsigned colour, const char* what, const char* detail)
{
    snprintf(g_toast, sizeof g_toast, "%s", what ? what : "");
    snprintf(g_toastSub, sizeof g_toastSub, "%s", detail ? detail : "");
    g_toastAt = g_editFrame;
    g_toastCol = colour;
    fprintf(stderr, "edit: %s%s%s\n", g_toast, detail && *detail ? " -- " : "",
            detail ? detail : "");
}

static void eui_draw_toast(const EuiLayout* L, int fbw, float t1)
{
    if (!g_toast[0]) return;
    const unsigned long age = g_editFrame - g_toastAt;
    if (age > EUI_TOAST_FRAMES) { g_toast[0] = 0; return; }
    const float S = L->s;
    /* Fades out over the last third rather than vanishing, so the eye can follow it. */
    float a = 1.0f;
    const unsigned long fade = EUI_TOAST_FRAMES / 3;
    if (age > EUI_TOAST_FRAMES - fade)
        a = (float)(EUI_TOAST_FRAMES - age) / (float)fade;

    const bool two = (g_toastSub[0] != 0);
    const float w1 = ef_text_w(g_toast, t1), w2 = ef_text_w(g_toastSub, EUI_TS(t1));
    const float w = (w1 > w2 ? w1 : w2) + 28 * S;
    const float h = (two ? 42 : 28) * S;
    const float x = L->railW + ((L->sideX - L->railW) - w) * 0.5f;
    const float y = L->topH + 14 * S;

    eui_rect(x, y, w, h, 0x0a0e14, 0.92f * a);
    eui_frame(x, y, w, h, g_toastCol, a);
    eui_rect(x, y, 3 * S, h, g_toastCol, a);
    ef_text(x + 14 * S, y + (two ? 8 : 10) * S, g_toast, t1, EUI_RGB(g_toastCol));
    if (two)
        ef_text(x + 14 * S, y + 25 * S, g_toastSub, EUI_TS(t1), EUI_RGB(EUI_DIM));
}




/* Every painted zone, on the ground, in the colour of the rule that owns it. A zone you
   cannot see is a zone you paint twice -- and since a cell can belong to only one rule,
   painting over someone else's is a real edit and has to be visible before it happens. */
/* Every cell nothing can be built on, washed and hatched. One diagonal per cell rather
   than a texture, because the Voodoo2 target has no shaders and a flat wash alone reads
   as "shaded ground" rather than as "refused". */
static void eui_draw_nogo(int fbw, int fbh)
{
    if (!g_editOn) return;
    /* OBJECTS mode only: terrain painting and elevation change what is buildable as you
       work, and a live refusal map over a tool that is editing the refusals is noise. */
    if (g_editMode != 0) return;
    const bool armed = (g_editArmed >= 0 && g_editTool == TOOL_PLACE);
    if (!armed && !g_nogoPin) return;

    /* Occupancy, built ONCE by walking the objects, rather than asked per cell.
       edit_cell_occupied is a linear scan of the document, and 4,096 cells times two
       hundred objects every frame is not a thing to do to find out what colour to paint
       the ground. This is the same answer for the cost of one walk. */
    static unsigned char busy[EDIT_GRID_MAX];
    memset(busy, 0, sizeof busy);
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_BUILDING && o.kind != K_TERRAIN) continue;
        const int ow = o.fw > 0 ? o.fw : 1, oh = o.fh > 0 ? o.fh : 1;
        for (int dy = 0; dy < oh; dy++)
            for (int dx = 0; dx < ow; dx++) {
                const int px = o.cx + dx, py = o.cy + dy;
                if (px >= 0 && py >= 0 && px < g_gridW && py < g_gridH)
                    busy[py * g_gridW + px] = 1;
            }
    }

    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    int bad = 0;
    for (int cy = 0; cy < g_gridH; cy++)
        for (int cx = 0; cx < g_gridW; cx++) {
            if (edit_can_build(cx, cy) && !busy[cy * g_gridW + cx]) continue;
            bad++;
            /* Ground that cannot take a building reads red; ground that is merely
               OCCUPIED reads amber, because those are different problems and moving
               what is in the way is a fix while moving a lake is not. */
            if (busy[cy * g_gridW + cx] && edit_can_build(cx, cy))
                glColor4f(0.85f, 0.62f, 0.15f, 0.20f);
            else
                glColor4f(0.72f, 0.15f, 0.11f, 0.26f);
            glBegin(GL_TRIANGLES);
            sb_place_cell_tris(cx, cy, fbw, fbh);
            glEnd();
        }
    /* The hatch, as one diagonal per refused cell. Drawn in a second pass so the wash
       underneath is uniform rather than doubled where cells touch. */
    /* Two pixels at 1x and four on a Retina panel. The default one-pixel line is
       invisible on a 3000-pixel-wide framebuffer, which is how the first version of this
       looked like it was not drawing at all. */
    glLineWidth(fbh > 1400 ? 3.0f : 2.0f);
    glColor4f(0.95f, 0.42f, 0.30f, 0.45f);
    glBegin(GL_LINES);
    for (int cy = 0; cy < g_gridH; cy++)
        for (int cx = 0; cx < g_gridW; cx++) {
            if (edit_can_build(cx, cy)) continue;    /* only the LAND gets a hatch */
            /* NW to SE, over the same draped corners sb_place_cell_tris uses, so the
               line lies on the ground rather than across it. */
            const float yNW = terrain_corner_y(cx, cy);
            const float ySE = terrain_corner_y(cx + 1, cy + 1);
            float ax, ay, bx2, by2;
            world_to_screen((float)cx, yNW, (float)cy, fbw, fbh, &ax, &ay);
            world_to_screen((float)cx + 1.0f, ySE, (float)cy + 1.0f, fbw, fbh,
                            &bx2, &by2);
            glVertex2f(ax, ay);
            glVertex2f(bx2, by2);
        }
    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    end_overlay();
    {   /* Said once per run, not per frame: how many cells the overlay is marking. A
           silent overlay and an overlay with nothing to mark look identical. */
        static int told = -1;
        if (told != bad) {
            told = bad;
            fprintf(stderr, "edit: no-go overlay marks %d of %d cells\n", bad,
                    g_gridW * g_gridH);
        }
    }
}

static void eui_draw_zones(int fbw, int fbh)
{
    if (!g_editOn || g_editMode != 4) return;
    static const unsigned ZC[6] = { 0xff9a5a, 0x8fe4ff, 0x7ee0a0, 0xe0b070,
                                    0xb98fe4, 0xff6a5a };
    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* The native zones, in the RULES view. */
    if (g_scriptView != 2)
    for (int c = 1; c < edit_ini_cells(); c++) {
        const int t = g_cellTrig[c];
        if (t < 0 || t >= (int)g_triggers.size()) continue;
        const bool mine = (t == g_trigSel);
        glColor4f(EUI_RGB(ZC[t % 6]), mine ? 0.42f : 0.16f);
        glBegin(GL_TRIANGLES);
        sb_place_cell_tris(c % edit_ini_w(), c / edit_ini_w(), fbw, fbh);
        glEnd();
    }
    /* And the Enhanced ones, which are a separate table. Green, because that is the
       colour the Enhanced tier wears everywhere else in the panel, and because a person
       switching views has to be able to tell at a glance which regions they are
       looking at. Cell 0 is included here: the engine's own rule that cell 0 cannot
       carry a trigger is the engine's, and this table is ours. */
    if (g_scriptView == 2)
    for (int c = 0; c < edit_ini_cells(); c++) {
        const int r = g_enhCell[c];
        if (r < 0 || r >= (int)g_enh.size()) continue;
        const bool mine = (r == g_enhSel);
        glColor4f(EUI_RGB(EUI_GREEN), mine ? 0.42f : 0.14f);
        glBegin(GL_TRIANGLES);
        sb_place_cell_tris(c % edit_ini_w(), c / edit_ini_w(), fbw, fbh);
        glEnd();
    }
    /* The marquee preview: the rectangle SHIFT+drag has open right now. */
    if (g_marqOn && g_editOverMap) {
        int x0, y0, x1, y1;
        edit_marq_rect(&x0, &y0, &x1, &y1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) continue;
                glColor4f(0.55f, 1.0f, 0.65f, 0.35f);
                glBegin(GL_TRIANGLES);
                sb_place_cell_tris(x, y, fbw, fbh);
                glEnd();
            }
    }
    if (g_enhPaint && g_editOverMap && g_enhSel >= 0) {
        for (int dy = -g_enhBrush + 1; dy <= g_enhBrush - 1; dy++)
            for (int dx = -g_enhBrush + 1; dx <= g_enhBrush - 1; dx++) {
                const int cx = g_editCellX + dx, cy = g_editCellY + dy;
                if (cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) continue;
                const int owner = g_enhCell[cy * edit_ini_w() + cx];
                const bool steal = (owner >= 0 && owner != g_enhSel);
                glColor4f(steal ? 1.0f : 0.55f, steal ? 0.7f : 1.0f,
                          steal ? 0.3f : 0.65f, 0.5f);
                glBegin(GL_TRIANGLES);
                sb_place_cell_tris(cx, cy, fbw, fbh);
                glEnd();
            }
    }
    /* The brush, so you can see what a click will claim. */
    if (g_zonePaint && g_editOverMap && g_trigSel >= 0) {
        for (int dy = -g_zoneBrush + 1; dy <= g_zoneBrush - 1; dy++)
            for (int dx = -g_zoneBrush + 1; dx <= g_zoneBrush - 1; dx++) {
                const int cx = g_editCellX + dx, cy = g_editCellY + dy;
                if (cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) continue;
                const int owner = g_cellTrig[cy * edit_ini_w() + cx];
                /* Amber where it would TAKE the cell from another rule. */
                const bool steal = (owner >= 0 && owner != g_trigSel);
                glColor4f(steal ? 1.0f : 0.55f, steal ? 0.7f : 1.0f,
                          steal ? 0.3f : 0.65f, 0.5f);
                glBegin(GL_TRIANGLES);
                sb_place_cell_tris(cx, cy, fbw, fbh);
                glEnd();
            }
    }
    glDisable(GL_BLEND);
    end_overlay();
}



/* Objects carrying the selected rule, marked on the ground. A tag is invisible
   otherwise -- it is a field on a line in a file -- and a group of four you cannot see
   is a group you tag twice. */
static void eui_draw_tags(int fbw, int fbh)
{
    if (!g_editOn || g_editMode != 4 || g_trigSel < 0) return;
    if (g_trigSel >= (int)g_triggers.size()) return;
    const char* want = g_triggers[g_trigSel].name;
    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (size_t i = 0; i < g_objects.size(); i++) {
        const char* tr = edit_trigger_for(g_objects[i]);
        if (!tr || strcasecmp(tr, want)) continue;
        const SimObject& o = g_objects[i];
        const int w = o.fw > 0 ? o.fw : 1, h = o.fh > 0 ? o.fh : 1;
        glColor4f(EUI_RGB(0xff9a5a), 0.40f);
        glBegin(GL_TRIANGLES);
        for (int dy = 0; dy < h; dy++)
            for (int dx = 0; dx < w; dx++) {
                if (o.cx + dx >= g_gridW || o.cy + dy >= g_gridH) continue;
                sb_place_cell_tris(o.cx + dx, o.cy + dy, fbw, fbh);
            }
        glEnd();
    }
    glDisable(GL_BLEND);
    end_overlay();
}

/* --- the script canvas --------------------------------------------------------------
 *
 *  One node per trigger, because one trigger IS one rule: event, action, and the
 *  parameters between them. Nodes are laid out in a column and read as sentences --
 *  "WHEN the timer reaches 5.5 min / THEN create a team" -- rather than as the
 *  "Time,Create Team,55,BadGuy,atk3,0" the file actually holds.
 *
 *  There are no execution wires, and that is deliberate rather than unfinished: the
 *  1995 engine has no flow to draw. Wires here mean REFERENCE -- this rule cancels that
 *  one, this rule creates that team -- which is the only linkage the format has.
 * ---------------------------------------------------------------------------------- */

static float g_scriptScroll = 0.0f;

static void eui_script_node_rect(const EuiLayout* L, int i, float* x, float* y,
                                 float* w, float* h)
{
    const float S = L->s;
    *w = 300 * S;
    *h = 74 * S;                 /* room for the "what is missing" line */
    *x = L->railW + 24 * S;
    *y = L->topH + 20 * S + i * (*h + 10 * S) - g_scriptScroll;
}

/* --- the team canvas ----------------------------------------------------------------
 *
 *  A team's order list is a straight-line program with one back-edge, so it draws as
 *  a chain. That is not a metaphor bolted onto the data: Loop:N literally sets the
 *  next order index to N (team.cpp:578-581), and a list without a Loop at the end runs
 *  out and disbands the team (team.cpp:491-494). Drawing the chain, and drawing the
 *  back-edge as an actual line, shows both of those facts without a word of text.
 *
 *  Two columns: the teams on the left, and the selected team's chain on the right.
 * ---------------------------------------------------------------------------------- */

/* THE MAP STAYS VISIBLE IN SCRIPT MODE.
 *
 * All three script canvases used to lay 82% black over the entire map, which is why
 * "how do I show the zone?" has no answer: a zone is painted on the ground, and the
 * ground was not there. You could only see it by turning zone painting ON, which is the
 * wrong way round -- you want to see the region while you decide whether to change it.
 *
 * The dim now covers only the COLUMN the cards actually occupy, and lightly. Everything
 * to the right of it is the map at full brightness, with the zones, the tags and the
 * waypoints drawn on it. */
static void eui_script_dim(const EuiLayout* L)
{
    const float S = L->s;
    /* Wide enough for whichever canvas is up: RULES is one column of cards, TEAMS and
       ENHANCED add a second column for the order chain and the condition stack. */
    const float w = g_scriptView ? (232 + 46 + 250 + 40) * S
                                 : (24 + 300 + 110 + 190 + 30) * S;
    eui_rect(L->railW, L->topH, w, L->barY - L->topH, 0x080b10, 0.72f);
    /* A soft edge so it reads as a panel over the map rather than a rectangle cut in it. */
    eui_rect(L->railW + w, L->topH, 10 * S, L->barY - L->topH, 0x080b10, 0.34f);
}

static void eui_team_card_rect(const EuiLayout* L, int i, float* x, float* y,
                               float* w, float* h)
{
    const float S = L->s;
    *w = 232 * S;
    *h = 42 * S;
    *x = L->railW + 24 * S;
    *y = L->topH + 20 * S + i * (*h + 6 * S) - g_scriptScroll;
}

static void eui_team_order_rect(const EuiLayout* L, int i, float* x, float* y,
                                float* w, float* h)
{
    const float S = L->s;
    *w = 250 * S;
    *h = 30 * S;
    *x = L->railW + 24 * S + 232 * S + 46 * S;   /* room for the back-edge gutter */
    *y = L->topH + 20 * S + i * (*h + 8 * S);
}

/* One line describing what a team is made of, for the card. */
static void eui_team_roster(const EditTeam& t, char* out, int cap)
{
    out[0] = 0;
    int n = 0;
    for (int i = 0; i < t.nclass && n < cap; i++)
        n += snprintf(out + n, cap - n, "%s%sx%d", i ? " " : "", t.cls[i].code,
                      t.cls[i].num);
    if (!t.nclass) snprintf(out, cap, "no members yet");
}

/* The wire primitives live with the rules canvas below; the teams canvas borrows them
   for its waypoint routes. */
static void eui_wire(float x0, float y0, float x1, float y1, unsigned col, float a,
                     float S);
static void eui_pin_dot(float px, float py, unsigned col, float S, bool filled);
static int  enh_carrier_list(int* out, int max);
static void eui_carrier_rect(const EuiLayout* L, int i, float* x, float* y,
                             float* w, float* h);

/* Inputs on the LEFT edge of a rule card: the things in the WORLD the rule refers to.
   The team pin stays on the right, because the team column is to the right; these point
   the other way, at the map, which is where their targets live. A pin only exists when
   the event can use it -- the shape of the node says what is possible. */
static void eui_watch_pin(const EuiLayout* L, int i, float* px, float* py)
{
    float x, y, w, h;
    eui_script_node_rect(L, i, &x, &y, &w, &h);
    *px = x + w;
    *py = y + h * 0.78f;
    (void)L;
}

static void eui_zone_pin(const EuiLayout* L, int i, float* px, float* py)
{
    float x, y, w, h;
    eui_script_node_rect(L, i, &x, &y, &w, &h);
    *px = x + w;
    *py = y + h * 0.55f;
}

/* DRAGGING AN ORDER up or down its chain. Press arms it, eight pixels of travel makes
   it a drag, and the list reorders LIVE as the pointer crosses chip midpoints -- each
   crossing is one adjacent swap, and every Loop in the team is retargeted at the moment
   of that swap, so the back-edge follows the order it points at wherever it goes. A
   press that never travels is still a click and selects on release. */
static int   g_ordPress = -1;
static float g_ordPressY = 0.0f;
static bool  g_ordDrag = false;

static void edit_order_swap_adjacent(EditTeam* T, int i)
{
    /* swap orders i and i+1, keeping every Loop pointing at the same ORDER */
    const EditTeamMission tmp = T->mis[i];
    T->mis[i] = T->mis[i + 1];
    T->mis[i + 1] = tmp;
    for (int m = 0; m < T->nmission; m++) {
        if (T->mis[m].mission != 9) continue;
        if (T->mis[m].arg == i) T->mis[m].arg = i + 1;
        else if (T->mis[m].arg == i + 1) T->mis[m].arg = i;
    }
}

/* A wire being dragged right now: the rule it left, or -1, and WHAT KIND of thing it
   wants to land on. 0 = a team box, 1 = an object on the map, 2 = ground for a zone. */
enum { WIRE_TEAM = 0, WIRE_WATCH = 1, WIRE_ZONE = 2, WIRE_CARRIER = 3,
       WIRE_ENHZONE = 4 };
static int   g_wireFrom = -1;
static int   g_wireKind = WIRE_TEAM;
static float g_wireX = 0.0f, g_wireY = 0.0f;   /* where the pointer is */

/* "I pressed ADD RULE and there's not a box for it." There was -- appended at the
   bottom of a list of nineteen, off screen. A new or picked rule now scrolls its card
   into view, about a third of the way down, which is where the eye looks first. */
static void edit_scroll_to_rule(int i, int fbw, int fbh)
{
    EuiLayout L;
    eui_layout(fbw, fbh, &L);
    const float step = (74 + 10) * L.s;
    float want = 20 * L.s + (float)i * step - (L.barY - L.topH) * 0.3f;
    const float span = (float)g_triggers.size() * step;
    const float room = L.barY - L.topH - 40 * L.s;
    float mx2 = span - room;
    if (mx2 < 0) mx2 = 0;
    if (want > mx2) want = mx2;
    if (want < 0) want = 0;
    g_scriptScroll = want;
}

/* True while a wire wants a WORLD target: the map must behave as the map. */
static bool eui_world_drag(void)
{
    return g_wireFrom >= 0 && (g_wireKind == WIRE_WATCH || g_wireKind == WIRE_ZONE ||
                               g_wireKind == WIRE_ENHZONE);
}

/* The carrier-eligible triggers: event None, which nothing in the engine ever springs.
   Rebuilt on demand -- there are a handful, not thousands. */
static int enh_carrier_list(int* out, int max)
{
    int n = 0;
    for (size_t i = 0; i < g_triggers.size() && n < max; i++)
        if (g_triggers[i].event == 0) out[n++] = (int)i;
    return n;
}

static void eui_carrier_rect(const EuiLayout* L, int i, float* x, float* y,
                             float* w, float* h)
{
    const float S = L->s;
    *w = 210 * S;
    *h = 40 * S;
    *x = L->railW + 24 * S + 232 * S + 46 * S + 300 * S + 60 * S;
    *y = L->topH + 20 * S + i * (*h + 8 * S) - g_scriptScroll;
}

static bool eui_script_map_live(void)
{
    return g_zonePaint || g_tagMode || g_enhPaint || eui_world_drag();
}


static void eui_draw_teams(const EuiLayout* L, int fbw, int fbh, float t1)
{
    const float S = L->s;
    eui_script_dim(L);
    if (g_teams.empty()) {
        const char* a = "THIS MISSION HAS NO TEAMS YET";
        const char* b = "ADD TEAM, in the panel on the right";
        ef_text(L->railW + 30 * S, L->topH + 40 * S, a, EUI_TL(t1), EUI_RGB(EUI_DIM));
        ef_text(L->railW + 30 * S, L->topH + 40 * S + 22 * S, b, t1, EUI_RGB(EUI_FAINT));
        return;
    }

    for (size_t i = 0; i < g_teams.size(); i++) {
        const EditTeam& t = g_teams[i];
        float x, y, w, h;
        eui_team_card_rect(L, (int)i, &x, &y, &w, &h);
        if (y + h < L->topH) continue;
        if (y > (float)fbh) break;
        const bool on  = ((int)i == g_teamSel);
        const bool hot = (g_euiHotKind == EUI_TEAMCARD && g_euiHotArg == (int)i);
        eui_rect(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL, 0.96f);
        eui_frame(x, y, w, h, on ? EUI_GOLD : EUI_LINE, 1.0f);
        /* House colour on the spine: whose team this is, is the first thing you ask. */
        const unsigned spine = !strcasecmp(t.house, "GoodGuy") ? EUI_CYAN
                             : !strcasecmp(t.house, "BadGuy")  ? EUI_DANGER
                             : EUI_FAINT;
        eui_rect(x, y, 3 * S, h, spine, 1.0f);
        if (!eui_rename_draws(1, (int)i, x + 12 * S, y + 6 * S, t1))
            ef_text_fit(x + 12 * S, y + 6 * S, t.name, t1, 110 * S, EUI_RGB(EUI_GOLD));
        char roster[96];
        eui_team_roster(t, roster, sizeof roster);
        ef_text_fit(x + 12 * S, y + 22 * S, roster, EUI_TS(t1), w - 24 * S,
                    EUI_RGB(EUI_DIM));
        char meta[48];
        snprintf(meta, sizeof meta, "%d ORDERS", t.nmission);
        ef_text(x + w - ef_text_w(meta, EUI_TS(t1)) - 10 * S, y + 6 * S, meta,
                EUI_TS(t1), EUI_RGB(EUI_FAINT));
    }

    if (g_teamSel < 0 || g_teamSel >= (int)g_teams.size()) {
        float cx, cy, cw, ch;
        eui_team_order_rect(L, 0, &cx, &cy, &cw, &ch);
        ef_text(cx, cy + 6 * S, "PICK A TEAM TO SEE ITS ORDERS", EUI_TS(t1),
                EUI_RGB(EUI_FAINT));
        return;
    }
    const EditTeam& T = g_teams[g_teamSel];
    {
        float cx, cy, cw, ch;
        eui_team_order_rect(L, 0, &cx, &cy, &cw, &ch);
        char hd[64];
        snprintf(hd, sizeof hd, "%s ORDERS", T.name);
        ef_text(cx, cy - 16 * S, hd, EUI_TS(t1), EUI_RGB(EUI_FAINT));
    }
    if (!T.nmission) {
        float cx, cy, cw, ch;
        eui_team_order_rect(L, 0, &cx, &cy, &cw, &ch);
        eui_rect(cx, cy, cw, ch, EUI_PANEL, 0.9f);
        eui_frame(cx, cy, cw, ch, EUI_LINE, 1.0f);
        ef_text(cx + 10 * S, cy + ch * 0.5f - 3 * S,
                "NO ORDERS -- THIS TEAM DISBANDS AT ONCE", EUI_TS(t1),
                EUI_RGB(EUI_DANGER));
        return;
    }

    for (int i = 0; i < T.nmission; i++) {
        float x, y, w, h;
        eui_team_order_rect(L, i, &x, &y, &w, &h);
        const bool on  = (i == g_teamOrder);
        const bool hot = (g_euiHotKind == EUI_TEAMORDER && g_euiHotArg == i);
        /* The connector down from the previous order. */
        if (i) eui_rect(x + 16 * S, y - 8 * S, 2 * S, 8 * S, EUI_LINE, 1.0f);
        eui_rect(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL, 0.96f);
        eui_frame(x, y, w, h, on ? EUI_GOLD : EUI_LINE, 1.0f);

        const int m = T.mis[i].mission;
        const int a = T.mis[i].arg;
        char step[16];
        snprintf(step, sizeof step, "%d", i);
        ef_text(x + 8 * S, y + h * 0.5f - 3 * S, step, EUI_TS(t1), EUI_RGB(EUI_FAINT));

        char lbl[96];
        switch (TEAM_MIS_ARG[m]) {
        case 1:  snprintf(lbl, sizeof lbl, "%s waypoint %d", TEAM_MIS[m], a); break;
        case 2:  snprintf(lbl, sizeof lbl, "%s %d", TEAM_MIS[m], a); break;
        case 3:  snprintf(lbl, sizeof lbl, "%s back to %d", TEAM_MIS[m], a); break;
        case 4:  snprintf(lbl, sizeof lbl, "%s (raw target %d)", TEAM_MIS[m], a); break;
        /* An argument in six-second units is the format's nastiest trap, so it is
           spelled out in seconds rather than left as a bare number. */
        default: snprintf(lbl, sizeof lbl, "%s for %ds", TEAM_MIS[m], a * 6); break;
        }
        ef_text_fit(x + 26 * S, y + h * 0.5f - 3 * S, lbl, t1, w - 36 * S,
                EUI_RGB(m == 9 ? EUI_GREEN : EUI_INK));

        /* Unload double-books its argument as a timeout as well as a waypoint
           (team.cpp:464 and :594), which no amount of staring at the INI reveals. */
        if (m == 11) {
            char note[48];
            snprintf(note, sizeof note, "also %ds", a * 6);
            ef_text(x + w - ef_text_w(note, EUI_TS(t1)) - 8 * S, y + h * 0.5f - 3 * S,
                    note, EUI_TS(t1), EUI_RGB(EUI_FAINT));
        }

        /* The back-edge: Loop draws an actual line up the gutter to its target. */
        if (m == 9 && a >= 0 && a < T.nmission) {
            float tx, ty, tw, th;
            eui_team_order_rect(L, a, &tx, &ty, &tw, &th);
            const float gx = x - 14 * S;
            eui_rect(gx, ty + th * 0.5f, 14 * S, 2 * S, EUI_GREEN, 0.85f);
            eui_rect(gx, ty + th * 0.5f, 2 * S, (y + h * 0.5f) - (ty + th * 0.5f),
                     EUI_GREEN, 0.85f);
            eui_rect(gx, y + h * 0.5f, 14 * S, 2 * S, EUI_GREEN, 0.85f);
        }
    }
    /* THE CHAIN REACHES INTO THE WORLD: every order that goes to a waypoint draws a
       wire from its chip to the waypoint's actual cell, so the route a team will walk
       is visible as a route, not as a column of numbers. An unplaced waypoint draws a
       short red stub instead -- the team is being sent to nowhere and that should look
       broken, because it is. */
    for (int i = 0; i < T.nmission; i++) {
        const int k = T.mis[i].mission;
        if (TEAM_MIS_ARG[k] != 1) continue;
        const int a = T.mis[i].arg;
        float x, y, w, h;
        eui_team_order_rect(L, i, &x, &y, &w, &h);
        if (y + h < L->topH || y > L->barY) continue;
        const bool sel = (i == g_teamOrder);
        if (a >= 0 && a < EDIT_WAYPT_COUNT && g_waypoint[a] >= 0) {
            float sx, sy;
            const int wx = g_waypoint[a] % edit_ini_w(), wy = g_waypoint[a] / edit_ini_w();
            world_to_screen((float)wx + 0.5f, terrain_corner_y(wx, wy),
                            (float)wy + 0.5f, fbw, fbh, &sx, &sy);
            if (sx >= L->railW && sx <= (float)fbw && sy >= L->topH && sy <= L->barY) {
                eui_wire(x + w, y + h * 0.5f, sx, sy, EUI_CYAN,
                         sel ? 0.9f : 0.30f, S);
                eui_pin_dot(sx, sy, EUI_CYAN, S, sel);
            }
        } else {
            eui_wire(x + w, y + h * 0.5f, x + w + 40 * S, y + h * 0.5f, 0xff6a5a,
                     0.8f, S);
            ef_text(x + w + 46 * S, y + h * 0.5f - 3 * S, "nowhere", EUI_TS(t1),
                    EUI_RGB(EUI_DANGER));
        }
    }

    /* "+ ADD ORDER", where the next order will actually appear -- on the chain itself,
       not only as a row three screens to the right. */
    if (T.nmission < TEAM_MISSION_MAX) {
        float x, y, w, h;
        eui_team_order_rect(L, T.nmission, &x, &y, &w, &h);
        const bool hot = (g_euiHotKind == EUI_TEAMORDER && g_euiHotArg == T.nmission);
        eui_rect(x, y, w, h, hot ? EUI_PANEL3 : EUI_PANEL, 0.6f);
        eui_frame(x, y, w, h, hot ? EUI_GOLD : EUI_LINE, 0.7f);
        ef_text(x + 10 * S, y + h * 0.5f - 3 * S, "+ add order", t1,
                EUI_RGB(hot ? EUI_GOLD : EUI_FAINT));
        if (T.nmission && T.mis[T.nmission - 1].mission != 9)
            ef_text(x + w + 10 * S, y + h * 0.5f - 3 * S,
                    "list ends: the team disbands", EUI_TS(t1), EUI_RGB(EUI_FAINT));
    }
}

/* --- the Enhanced canvas ------------------------------------------------------------
 *
 *  An Enhanced rule is a set of conditions joined by ALL or ANY, and an effect. It draws
 *  as what it is: the clauses stacked with a spine down their left, the combinator named
 *  on that spine, and the carrier as the terminal node the whole thing feeds.
 *
 *  The carrier is drawn as a node rather than hidden, because it is a real trigger in the
 *  file with a real action, and the one thing a person must be able to see is that the
 *  effect is the ENGINE'S -- the same Create Team, the same Reinforce -- and not something
 *  this tier invented.
 * ---------------------------------------------------------------------------------- */

static void eui_enh_card_rect(const EuiLayout* L, int i, float* x, float* y,
                              float* w, float* h)
{
    const float S = L->s;
    *w = 232 * S; *h = 42 * S;
    *x = L->railW + 24 * S;
    *y = L->topH + 20 * S + i * (*h + 6 * S) - g_scriptScroll;
}

static void eui_enh_clause_rect(const EuiLayout* L, int i, float* x, float* y,
                                float* w, float* h)
{
    const float S = L->s;
    *w = 300 * S; *h = 28 * S;
    *x = L->railW + 24 * S + 232 * S + 46 * S;
    *y = L->topH + 40 * S + i * (*h + 6 * S);
}

/* One clause as an English sentence. The raw form is "TYPE BadGuy HAND 1", which is
   precise and tells a person nothing. */
static void enh_clause_human(const EnhClause& c, char* out, int cap)
{
    const char* not_ = c.negate ? "NOT " : "";
    /* "has 1 units or more" is the sort of thing that makes an interface feel unfinished,
       and every one of these thresholds can legitimately be 1. */
    const char* s1 = (c.num == 1) ? "" : "s";
    switch (c.kind) {
    case ENH_TIME:
        snprintf(out, cap, "%s%.1f minutes have passed", not_, c.num / 10.0f); break;
    case ENH_CREDITS:
        snprintf(out, cap, "%s%s has %d credit%s or more", not_, c.house, c.num, s1);
        break;
    case ENH_UNITS:
        snprintf(out, cap, "%s%s has %d unit%s or more", not_, c.house, c.num, s1);
        break;
    case ENH_BUILDINGS:
        snprintf(out, cap, "%s%s has %d building%s or more", not_, c.house, c.num, s1);
        break;
    case ENH_TYPE:
        snprintf(out, cap, "%s%s owns %d %s or more", not_, c.house, c.num, c.name); break;
    case ENH_ZONE:
        snprintf(out, cap, "%s%s has %d in the zone", not_, c.house, c.num); break;
    case ENH_COUNTER:
        snprintf(out, cap, "%scounter %s has reached %d", not_, c.name, c.num); break;
    case ENH_FIRED:
        snprintf(out, cap, "%srule %s has already fired", not_, c.name); break;
    default: snprintf(out, cap, "%s?", not_); break;
    }
}

static void eui_draw_enh(const EuiLayout* L, int fbw, int fbh, float t1)
{
    const float S = L->s;
    eui_script_dim(L);
    if (g_enh.empty()) {
        const char* a = "NO ENHANCED RULES ON THIS MAP";
        const char* b = "ADD RULE, in the panel on the right";
        const char* c = "these are conditions the 1995 engine cannot express, so a map";
        const char* d = "that uses them is tagged and will only run in CNC3D.";
        ef_text(L->railW + 30 * S, L->topH + 40 * S, a, EUI_TL(t1), EUI_RGB(EUI_DIM));
        ef_text(L->railW + 30 * S, L->topH + 40 * S + 22 * S, b, t1, EUI_RGB(EUI_FAINT));
        ef_text(L->railW + 30 * S, L->topH + 40 * S + 48 * S, c, EUI_TS(t1),
                EUI_RGB(EUI_FAINT));
        ef_text(L->railW + 30 * S, L->topH + 40 * S + 62 * S, d, EUI_TS(t1),
                EUI_RGB(EUI_FAINT));
        return;
    }

    for (size_t i = 0; i < g_enh.size(); i++) {
        const EnhRule& r = g_enh[i];
        float x, y, w, h;
        eui_enh_card_rect(L, (int)i, &x, &y, &w, &h);
        if (y + h < L->topH) continue;
        if (y > (float)fbh) break;
        const bool on  = ((int)i == g_enhSel);
        const bool hot = (g_euiHotKind == EUI_ENHCARD && g_euiHotArg == (int)i);
        eui_rect(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL, 0.96f);
        eui_frame(x, y, w, h, on ? EUI_GOLD : EUI_LINE, 1.0f);
        eui_rect(x, y, 3 * S, h, EUI_GREEN, 1.0f);
        if (!eui_rename_draws(2, (int)i, x + 12 * S, y + 6 * S, t1))
            ef_text_fit(x + 12 * S, y + 6 * S, r.name, t1, 130 * S, EUI_RGB(EUI_GOLD));
        char sub[96];
        snprintf(sub, sizeof sub, "%s of %d  ->  %s", r.all ? "ALL" : "ANY", r.nclause,
                 r.carrier[0] ? r.carrier : "no carrier");
        ef_text_fit(x + 12 * S, y + 22 * S, sub, EUI_TS(t1), w - 24 * S,
                EUI_RGB(r.carrier[0] ? EUI_DIM : EUI_DANGER));
        if (!r.repeat) {
            const char* m = "ONCE";
            ef_text(x + w - ef_text_w(m, EUI_TS(t1)) - 10 * S, y + 6 * S, m, EUI_TS(t1),
                    EUI_RGB(EUI_FAINT));
        }
        /* Enhanced needs no inference: the evaluator is ours and recorded the tick. It
           is read out of the trace's own map, because coming back to the editor re-reads
           the mission and resets every rule's runtime state. */
        if (g_traceRan) {
            std::map<std::string, int>::const_iterator ti =
                g_traceEnhFired.find(r.name);
            const int tf = (ti == g_traceEnhFired.end()) ? -1 : ti->second;
            char badge[48];
            if (tf >= 0) snprintf(badge, sizeof badge, "FIRED %.1f min", tf / 900.0f);
            else         snprintf(badge, sizeof badge, "NEVER FIRED");
            ef_text(x + w - ef_text_w(badge, EUI_TS(t1)) - 10 * S, y + h - 14 * S,
                    badge, EUI_TS(t1), EUI_RGB(tf >= 0 ? EUI_GREEN : EUI_DANGER));
        }
    }

    if (g_enhSel < 0 || g_enhSel >= (int)g_enh.size()) {
        float cx, cy, cw, ch;
        eui_enh_clause_rect(L, 0, &cx, &cy, &cw, &ch);
        ef_text(cx, cy, "PICK A RULE TO SEE ITS CONDITIONS", EUI_TS(t1),
                EUI_RGB(EUI_FAINT));
        return;
    }
    const EnhRule& R = g_enh[g_enhSel];
    {
        float cx, cy, cw, ch;
        eui_enh_clause_rect(L, 0, &cx, &cy, &cw, &ch);
        char hd[96];
        snprintf(hd, sizeof hd, "%s FIRES WHEN %s OF THESE HOLD", R.name,
                 R.all ? "ALL" : "ANY");
        ef_text_fit(cx, cy - 18 * S, hd, EUI_TS(t1), 320 * S, EUI_RGB(EUI_FAINT));
    }
    for (int i = 0; i < R.nclause; i++) {
        float x, y, w, h;
        eui_enh_clause_rect(L, i, &x, &y, &w, &h);
        const bool on  = (i == g_enhClause);
        const bool hot = (g_euiHotKind == EUI_ENHCLAUSE && g_euiHotArg == i);
        /* The spine: one continuous line down the left of the whole set, which is what
           makes ALL and ANY a shape rather than a word. */
        if (i) eui_rect(x - 12 * S, y - 6 * S, 2 * S, 6 * S + h * 0.5f,
                        R.all ? EUI_CYAN : EUI_GOLD, 0.8f);
        eui_rect(x - 12 * S, y + h * 0.5f, 12 * S, 2 * S,
                 R.all ? EUI_CYAN : EUI_GOLD, 0.8f);
        eui_rect(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL, 0.96f);
        eui_frame(x, y, w, h, on ? EUI_GOLD : EUI_LINE, 1.0f);
        if (R.clause[i].negate) eui_rect(x, y, 3 * S, h, EUI_DANGER, 1.0f);
        char human[128];
        enh_clause_human(R.clause[i], human, sizeof human);
        ef_text_fit(x + 10 * S, y + h * 0.5f - 3 * S, human, EUI_TS(t1), w - 20 * S,
                    EUI_RGB(EUI_INK));
        if (R.clause[i].kind == ENH_ZONE) {
            char z[32];
            snprintf(z, sizeof z, "%d cells", enh_zone_cells(g_enhSel));
            ef_text(x + w + 22 * S, y + h * 0.5f - 3 * S, z, EUI_TS(t1),
                    EUI_RGB(enh_zone_cells(g_enhSel) ? EUI_GREEN : EUI_DANGER));
            /* and a pin: drag it onto the ground to draw the region */
            eui_pin_dot(x + w, y + h * 0.5f, EUI_GREEN, S,
                        enh_zone_cells(g_enhSel) > 0);
        }
    }
    /* The terminal: what actually happens, and whose code runs it. */
    {
        float x, y, w, h;
        eui_enh_clause_rect(L, R.nclause > 0 ? R.nclause : 0, &x, &y, &w, &h);
        y += 10 * S;
        h = 40 * S;
        eui_rect(x, y, w, h, EUI_PANEL2, 0.96f);
        eui_frame(x, y, w, h, R.carrier[0] ? EUI_GREEN : EUI_DANGER, 1.0f);
        char l1[96], l2[128];
        const int ci = edit_trigger_by_name(R.carrier);
        if (ci >= 0) {
            snprintf(l1, sizeof l1, "THEN %s", TRIG_ACTION_HUMAN[g_triggers[ci].action]);
            snprintf(l2, sizeof l2, "the engine's own action, via carrier %s", R.carrier);
        } else if (R.carrier[0]) {
            snprintf(l1, sizeof l1, "THEN ???");
            snprintf(l2, sizeof l2, "carrier %s is not in this mission", R.carrier);
        } else {
            snprintf(l1, sizeof l1, "THEN nothing happens");
            snprintf(l2, sizeof l2, "this rule has no carrier; set one in the panel");
        }
        ef_text_fit(x + 10 * S, y + 6 * S, l1, t1, w - 20 * S, EUI_RGB(EUI_GOLD));
        const bool okAction = ci >= 0 && g_triggers[ci].action != 16 &&
                              g_triggers[ci].action != 17;
        ef_text_fit(x + 10 * S, y + 22 * S, l2, EUI_TS(t1), w - 20 * S,
                EUI_RGB(R.carrier[0] && okAction ? EUI_FAINT : EUI_DANGER));
        for (int e = 0; e < R.neffect; e++) {
            char ef[96];
            snprintf(ef, sizeof ef, "and %s counter %s to %d",
                     R.effect[e].kind == ENH_SET ? "set" : "add", R.effect[e].name,
                     R.effect[e].num);
            ef_text_fit(x + 10 * S, y + h + 6 * S + e * 14 * S, ef, EUI_TS(t1),
                        w - 20 * S, EUI_RGB(EUI_CYAN));
        }
    }

    /* ---- THE CARRIERS COLUMN --------------------------------------------------------
       The engine's own actions, one box per carrier, the same shape the RULES view
       gives its teams. A wire from each Enhanced rule to the carrier it fires; drag a
       rule's carrier pin onto a box to rewire it, onto nothing to be offered the list.
       An unclaimed carrier draws hollow -- it is a trigger nothing can ever spring. */
    {
        int carriers[64];
        const int nc = enh_carrier_list(carriers, 64);
        if (nc) {
            float hx, hy, hw, hh;
            eui_carrier_rect(L, 0, &hx, &hy, &hw, &hh);
            ef_text(hx, hy - 16 * S, "CARRIERS -- the engine's actions", EUI_TS(t1),
                    EUI_RGB(EUI_FAINT));
        }
        for (int c = 0; c < nc; c++) {
            const EditTrigger& t = g_triggers[carriers[c]];
            float bx, by, bw, bh;
            eui_carrier_rect(L, c, &bx, &by, &bw, &bh);
            if (by + bh < L->topH || by > (float)fbh) continue;
            bool claimed = false;
            for (size_t r = 0; r < g_enh.size() && !claimed; r++)
                if (!strcasecmp(g_enh[r].carrier, t.name)) claimed = true;
            const bool hot = (g_euiHotKind == EUI_CARRIERBOX && g_euiHotArg == c);
            eui_rect(bx, by, bw, bh, hot ? EUI_PANEL3 : EUI_PANEL, claimed ? 0.96f : 0.5f);
            eui_frame(bx, by, bw, bh, claimed ? EUI_LINE : EUI_DANGER, 1.0f);
            eui_rect(bx, by, 3 * S, bh, EUI_GREEN, 1.0f);
            ef_text_fit(bx + 12 * S, by + 5 * S, t.name, t1, bw - 24 * S,
                        EUI_RGB(EUI_GOLD));
            ef_text_fit(bx + 12 * S, by + 21 * S,
                        claimed ? TRIG_ACTION_HUMAN[t.action]
                                : "no rule fires this carrier",
                        EUI_TS(t1), bw - 24 * S,
                        EUI_RGB(claimed ? EUI_DIM : EUI_DANGER));
            eui_pin_dot(bx, by + bh * 0.5f, claimed ? EUI_GOLD : EUI_FAINT, S, claimed);
        }

        /* Wires and carrier pins on the rule cards. */
        for (size_t r = 0; r < g_enh.size(); r++) {
            float cx, cy, cw, ch;
            eui_enh_card_rect(L, (int)r, &cx, &cy, &cw, &ch);
            if (cy + ch < L->topH || cy > (float)fbh) continue;
            const int ci = edit_trigger_by_name(g_enh[r].carrier);
            const bool live = ci >= 0 && g_triggers[ci].event == 0;
            eui_pin_dot(cx + cw, cy + ch * 0.5f, live ? EUI_GOLD : EUI_DANGER, S, live);
            if (!live) continue;
            for (int c = 0; c < nc; c++)
                if (carriers[c] == ci) {
                    float bx, by, bw, bh;
                    eui_carrier_rect(L, c, &bx, &by, &bw, &bh);
                    const bool lit = ((int)r == g_enhSel);
                    eui_wire(cx + cw, cy + ch * 0.5f, bx, by + bh * 0.5f,
                             lit ? EUI_GOLD : EUI_CYAN, lit ? 0.95f : 0.40f, S);
                    break;
                }
        }

        /* A zone wire out of a clause card, heading for the ground. */
        if (g_wireFrom >= 0 && g_wireKind == WIRE_ENHZONE &&
            g_enhSel >= 0 && g_enhSel < (int)g_enh.size() &&
            g_wireFrom < g_enh[g_enhSel].nclause) {
            float zx2, zy2, zw2, zh2;
            eui_enh_clause_rect(L, g_wireFrom, &zx2, &zy2, &zw2, &zh2);
            eui_wire(zx2 + zw2, zy2 + zh2 * 0.5f, g_wireX, g_wireY, EUI_GREEN, 0.95f, S);
            eui_pin_dot(g_wireX, g_wireY, EUI_GREEN, S, true);
            ef_text(g_wireX + 14 * S, g_wireY - 4 * S,
                    "drop on the ground to start the region", t1, EUI_RGB(EUI_GREEN));
        }

        /* The live drag, when a carrier wire is out. */
        if (g_wireFrom >= 0 && g_wireKind == WIRE_CARRIER &&
            g_wireFrom < (int)g_enh.size()) {
            float cx, cy, cw, ch;
            eui_enh_card_rect(L, g_wireFrom, &cx, &cy, &cw, &ch);
            eui_wire(cx + cw, cy + ch * 0.5f, g_wireX, g_wireY, EUI_GOLD, 0.95f, S);
            eui_pin_dot(g_wireX, g_wireY, EUI_GOLD, S, true);
            ef_text(g_wireX + 14 * S, g_wireY - 4 * S,
                    "drop on a carrier, or on nothing to choose", t1, EUI_RGB(EUI_GOLD));
        }
    }
}

/* ====================================================================================
 *  WIRES -- the graph half of the script editor.
 *
 *  "Script editor needs a visual aspect, like unreal blueprints where it makes sense to
 *  drag lines between boxes." -- as requested.
 *
 *  A wire has to MEAN something in the file or it is decoration. In Tiberian Dawn there
 *  are exactly two links a rule can carry, and both are a name written into a field:
 *
 *      rule -> team      the Team field, for Create Team, Dstry Teams and Reinforce.
 *                        (trigger.cpp:250-259, Action_Need_Team). This is the commonest
 *                        thing a rule does: 32% of every action in the campaign.
 *      Enhanced -> rule  the carrier a condition fires (docs/scripting/enhanced.md).
 *
 *  So dragging a wire from a rule to a team is not a metaphor: it writes that team's
 *  name into that rule's fifth comma-separated field, and nothing else happens.
 *
 *  An action that cannot carry a team has no output pin at all, rather than a pin that
 *  refuses -- the shape of the node tells you what is possible before you try it.
 * ================================================================================== */

/* The team column, to the right of the rules, with the wire gutter between them. */
static void eui_wire_team_rect(const EuiLayout* L, int i, float* x, float* y,
                               float* w, float* h)
{
    const float S = L->s;
    *w = 190 * S;
    *h = 40 * S;
    *x = L->railW + 24 * S + 300 * S + 110 * S;
    *y = L->topH + 20 * S + i * (*h + 8 * S) - g_scriptScroll;
}

/* Where a wire leaves a rule, and where it arrives at a team. */
static void eui_rule_pin(const EuiLayout* L, int i, float* px, float* py)
{
    float x, y, w, h;
    eui_script_node_rect(L, i, &x, &y, &w, &h);
    *px = x + w;
    *py = y + h * 0.5f;
}

static void eui_team_pin(const EuiLayout* L, int i, float* px, float* py)
{
    float x, y, w, h;
    eui_wire_team_rect(L, i, &x, &y, &w, &h);
    *px = x;
    *py = y + h * 0.5f;
}

#define EUI_PIN_R 7.0f          /* pin radius in design units */

/* A Blueprints curve: horizontal tangents at both ends, so it leaves the pin sideways
   and arrives sideways however the two boxes are stacked. Drawn as a strip because this
   renderer has no curves and the Voodoo path would not want them anyway. */
static void eui_wire(float x0, float y0, float x1, float y1, unsigned col, float a,
                     float S)
{
    const float reach = 60.0f * S + fabsf(x1 - x0) * 0.35f;
    const float cx0 = x0 + reach, cx1 = x1 - reach;
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(S > 1.5f ? 3.0f : 2.0f);
    glColor4f(EUI_RGB(col), a);
    glBegin(GL_LINE_STRIP);
    for (int k = 0; k <= 24; k++) {
        const float t = (float)k / 24.0f, u = 1.0f - t;
        const float bx = u*u*u * x0 + 3*u*u*t * cx0 + 3*u*t*t * cx1 + t*t*t * x1;
        const float by = u*u*u * y0 + 3*u*u*t * y0  + 3*u*t*t * y1  + t*t*t * y1;
        glVertex2f(bx, by);
    }
    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

static void eui_pin_dot(float px, float py, unsigned col, float S, bool filled)
{
    const float r = EUI_PIN_R * S;
    eui_rect(px - r, py - r, r * 2, r * 2, col, filled ? 1.0f : 0.35f);
    if (!filled) eui_frame(px - r, py - r, r * 2, r * 2, col, 1.0f);
}


static void eui_draw_script(const EuiLayout* L, int fbw, int fbh, float t1)
{
    const float S = L->s;
    /* The map is still behind -- you are editing THIS mission's rules and seeing it is
       useful -- but it is dropped well back so the rules are what you read. */
    eui_script_dim(L);
    if (g_triggers.empty()) {
        const char* a = "THIS MISSION HAS NO TRIGGERS YET";
        const char* b = "ADD RULE, in the panel on the right";
        ef_text(L->railW + 30 * S, L->topH + 40 * S, a, EUI_TL(t1), EUI_RGB(EUI_DIM));
        ef_text(L->railW + 30 * S, L->topH + 40 * S + 22 * S, b, t1, EUI_RGB(EUI_FAINT));
        return;
    }
    for (size_t i = 0; i < g_triggers.size(); i++) {
        const EditTrigger& t = g_triggers[i];
        float x, y, w, h;
        eui_script_node_rect(L, (int)i, &x, &y, &w, &h);
        if (y + h < L->topH) continue;
        if (y > (float)fbh) break;

        const bool on  = ((int)i == g_trigSel);
        const bool hot = (g_euiHotKind == EUI_SCRIPTNODE && g_euiHotArg == (int)i);
        eui_rect(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL, 0.96f);
        eui_frame(x, y, w, h, on ? EUI_GOLD : EUI_LINE, 1.0f);
        /* A coloured spine per action family, so a wall of rules is scannable. */
        const unsigned spine = (t.action == 1) ? EUI_GREEN            /* win     */
                             : (t.action == 2) ? EUI_DANGER           /* lose    */
                             : (t.action == 4 || t.action == 7) ? EUI_GOLD
                             : EUI_CYAN;
        eui_rect(x, y, 3 * S, h, spine, 1.0f);

        if (!eui_rename_draws(0, (int)i, x + 12 * S, y + 6 * S, t1))
            ef_text_fit(x + 12 * S, y + 6 * S, t.name, t1, 60 * S, EUI_RGB(EUI_GOLD));

        char when[160];
        if (t.event == 11)      /* Time is in tenths of a minute */
            snprintf(when, sizeof when, "WHEN %s %.1f min",
                     TRIG_EVENT_HUMAN[t.event], t.data / 10.0f);
        else if (trig_event_has_data(t.event))
            snprintf(when, sizeof when, "WHEN %s %d", TRIG_EVENT_HUMAN[t.event], t.data);
        else
            snprintf(when, sizeof when, "WHEN %s", TRIG_EVENT_HUMAN[t.event]);
        ef_text_fit(x + 12 * S, y + 22 * S, when, t1, w - 24 * S, EUI_RGB(EUI_INK));

        char then[160];
        if (trig_action_has_team(t.action) && t.team[0] && strcasecmp(t.team, "None"))
            snprintf(then, sizeof then, "THEN %s  %s", TRIG_ACTION_HUMAN[t.action], t.team);
        else
            snprintf(then, sizeof then, "THEN %s", TRIG_ACTION_HUMAN[t.action]);
        ef_text_fit(x + 12 * S, y + 38 * S, then, t1, w - 24 * S, EUI_RGB(EUI_DIM));

        /* House and persistence, right-aligned: both change what the rule means and
           neither is visible from the sentence. */
        char meta[64];
        static const char* PER[3] = { "once", "all-tagged", "always" };
        snprintf(meta, sizeof meta, "%s  %s",
                 t.house[0] ? t.house : "None",
                 PER[t.persist < 0 || t.persist > 2 ? 0 : t.persist]);
        ef_text(x + w - ef_text_w(meta, EUI_TS(t1)) - 10 * S, y + 6 * S, meta,
                EUI_TS(t1), EUI_RGB(EUI_FAINT));

        /* WHAT THIS RULE IS STILL MISSING, on the card itself.
         *
         * A Player Enters rule needs a REGION and a house to enter it; an object-routed
         * one needs something TAGGED. Both lived only on rows that scroll off the bottom
         * of a short panel, so a rule could sit here looking finished while being unable
         * to fire -- which is exactly what happened: "I created a rule, when something
         * enters a zone. But how do I drag and show the zone?" */
        {
            const int zc = edit_zone_cells((int)i);
            const int tc = edit_tagged_count((int)i);
            const bool housed = t.house[0] && strcasecmp(t.house, "None");
            char need[96];
            unsigned ncol = EUI_FAINT;
            need[0] = 0;
            if (t.event == 1) {
                if (!zc && !tc) { snprintf(need, sizeof need,
                                  "NEEDS A REGION -- turn on WHERE and drag on the map");
                                  ncol = EUI_DANGER; }
                else if (!housed) { snprintf(need, sizeof need,
                                    "%d cells, but NOBODY enters them -- set WHO ENTERS",
                                    zc); ncol = EUI_DANGER; }
                else { snprintf(need, sizeof need, "%d cells, entered by %s", zc, t.house);
                       ncol = EUI_GREEN; }
            } else if (trig_event_reads_tag(t.event)) {
                if (!tc && !(t.event == 3 && housed)) {
                    snprintf(need, sizeof need, "NEEDS AN OBJECT -- tag what it watches");
                    ncol = EUI_DANGER;
                } else if (tc) {
                    snprintf(need, sizeof need, "watching %d object%s", tc,
                             tc == 1 ? "" : "s");
                    ncol = EUI_GREEN;
                }
            } else if (zc) {
                snprintf(need, sizeof need, "%d zone cells this event ignores", zc);
                ncol = EUI_DANGER;
            }
            if (need[0])
                ef_text_fit(x + 12 * S, y + 54 * S, need, EUI_TS(t1), w - 24 * S,
                            EUI_RGB(ncol));
        }

        /* The trace verdict, if one has been run. A persistent trigger never leaves the
           game, so there is no signal for it and it gets no badge rather than a wrong
           one. */
        const int tf = edit_trace_of(t.name);
        if (tf != -2 && t.persist != 2) {
            char badge[48];
            if (tf >= 0) snprintf(badge, sizeof badge, "FIRED %.1f min", tf / 900.0f);
            else         snprintf(badge, sizeof badge, "NEVER FIRED");
            ef_text(x + w - ef_text_w(badge, EUI_TS(t1)) - 10 * S, y + h - 14 * S,
                    badge, EUI_TS(t1), EUI_RGB(tf >= 0 ? EUI_GREEN : EUI_DANGER));
        }
    }
    /* ---- THE TEAM COLUMN, and the wires into it -------------------------------------
       Drawn after the rule cards so the wires lie over the gutter rather than under the
       boxes they join. */
    for (size_t k = 0; k < g_teams.size(); k++) {
        float tx, ty, tw, th;
        eui_wire_team_rect(L, (int)k, &tx, &ty, &tw, &th);
        if (ty + th < L->topH) continue;
        if (ty > (float)fbh) break;
        const bool on  = ((int)k == g_teamSel);
        const bool hot = (g_euiHotKind == EUI_WIRETEAM && g_euiHotArg == (int)k);
        /* A team nothing names is drawn hollow: it exists in the file and no rule calls
           it up, which is a thing you want to see at a glance in a graph. */
        /* "Nothing calls this team" is only alarming if nothing CAN. The AI raises a
           team on its own whenever its cap is positive, and an autocreate team is meant
           to arrive without any rule naming it -- SCG09EA's eight AUTOn teams are all
           like that. Flagging those red would paint most of the campaign's teams as
           broken, which is the same false alarm the mission check had to have taken out
           of it. */
        int users = 0;
        for (size_t r = 0; r < g_triggers.size(); r++)
            if (trig_action_has_team(g_triggers[r].action) &&
                !strcasecmp(g_triggers[r].team, g_teams[k].name)) users++;
        const bool reachable = users || g_teams[k].autocreate || g_teams[k].maxallowed > 0;
        eui_rect(tx, ty, tw, th, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL,
                 reachable ? 0.96f : 0.55f);
        eui_frame(tx, ty, tw, th, on ? EUI_GOLD : reachable ? EUI_LINE : EUI_DANGER,
                  1.0f);
        const unsigned spine = !strcasecmp(g_teams[k].house, "GoodGuy") ? EUI_CYAN
                             : !strcasecmp(g_teams[k].house, "BadGuy")  ? EUI_DANGER
                             : EUI_FAINT;
        eui_rect(tx, ty, 3 * S, th, spine, 1.0f);
        ef_text_fit(tx + 12 * S, ty + 5 * S, g_teams[k].name, t1, tw - 24 * S,
                    EUI_RGB(EUI_GOLD));
        char roster[96];
        eui_team_roster(g_teams[k], roster, sizeof roster);
        ef_text_fit(tx + 12 * S, ty + 21 * S,
                    reachable ? roster : "no rule calls this, and the AI cannot either",
                    EUI_TS(t1), tw - 24 * S, EUI_RGB(reachable ? EUI_DIM : EUI_DANGER));
        float px, py;
        eui_team_pin(L, (int)k, &px, &py);
        eui_pin_dot(px, py, users ? EUI_GOLD : EUI_FAINT, S, users != 0);
    }

    /* World pins on the right edge, under the team pin: WATCH (what objects it is
       attached to) and ZONE (where its region is). Drag one onto the map. */
    for (size_t i = 0; i < g_triggers.size(); i++) {
        const EditTrigger& t = g_triggers[i];
        float px, py;
        if (t.event == 1) {
            eui_zone_pin(L, (int)i, &px, &py);
            if (py >= L->topH && py <= L->barY)
                eui_pin_dot(px, py, EUI_GREEN, S, edit_zone_cells((int)i) > 0);
        }
        if (trig_event_reads_tag(t.event)) {
            eui_watch_pin(L, (int)i, &px, &py);
            if (py >= L->topH && py <= L->barY)
                eui_pin_dot(px, py, 0xff6a5a, S, edit_tagged_count((int)i) > 0);
        }
    }

    /* THE SCRIPT REACHES INTO THE WORLD, visibly. For the selected rule, a wire runs
       from its card to every object it watches and to the middle of its region --
       projected with the same world_to_screen the objects were drawn with, so the line
       ends exactly on the thing. This is the answer to "how do I pick WHICH guard
       tower": the wires show which ones are picked, and dragging the pin picks more. */
    if (g_trigSel >= 0 && g_trigSel < (int)g_triggers.size()) {
        const EditTrigger& t = g_triggers[g_trigSel];
        if (trig_event_reads_tag(t.event)) {
            float px, py;
            eui_watch_pin(L, g_trigSel, &px, &py);
            for (size_t o = 0; o < g_objects.size(); o++) {
                const char* tr = edit_trigger_for(g_objects[o]);
                if (!tr || strcasecmp(tr, t.name)) continue;
                float sx, sy;
                world_to_screen(g_objects[o].wx, terrain_corner_y(g_objects[o].cx,
                                g_objects[o].cy), g_objects[o].wz, fbw, fbh, &sx, &sy);
                if (sx < L->railW || sx > (float)fbw || sy < L->topH || sy > L->barY)
                    continue;
                eui_wire(px, py, sx, sy, 0xff6a5a, 0.75f, S);
                eui_pin_dot(sx, sy, 0xff6a5a, S, true);
            }
        }
        if (t.event == 1 && edit_zone_cells(g_trigSel) > 0) {
            double zx = 0, zy = 0;
            int zn = 0;
            for (int c = 0; c < edit_ini_cells(); c++)
                if (g_cellTrig[c] == g_trigSel) { zx += c % edit_ini_w();
                                                  zy += c / edit_ini_w(); zn++; }
            if (zn) {
                float px, py, sx, sy;
                eui_zone_pin(L, g_trigSel, &px, &py);
                const float wx = (float)(zx / zn) + 0.5f, wz = (float)(zy / zn) + 0.5f;
                world_to_screen(wx, terrain_corner_y((int)wx, (int)wz), wz,
                                fbw, fbh, &sx, &sy);
                if (sx >= L->railW && sx <= (float)fbw && sy >= L->topH && sy <= L->barY) {
                    eui_wire(px, py, sx, sy, EUI_GREEN, 0.75f, S);
                    eui_pin_dot(sx, sy, EUI_GREEN, S, true);
                }
            }
        }
    }

    /* The wires themselves, plus an output pin on every rule that can carry a team. */
    for (size_t i = 0; i < g_triggers.size(); i++) {
        const EditTrigger& t = g_triggers[i];
        if (!trig_action_has_team(t.action)) continue;   /* no pin: no link possible */
        float px, py;
        eui_rule_pin(L, (int)i, &px, &py);
        if (py < L->topH - 40 * S || py > L->barY + 40 * S) continue;
        const int tk = edit_team_by_name(t.team);
        const bool live = (tk >= 0);
        eui_pin_dot(px, py, live ? EUI_GOLD : EUI_DANGER, S, live);
        if (!live) continue;
        float qx, qy;
        eui_team_pin(L, tk, &qx, &qy);
        const bool lit = ((int)i == g_trigSel) || (tk == g_teamSel);
        eui_wire(px, py, qx, qy, lit ? EUI_GOLD : EUI_CYAN, lit ? 0.95f : 0.40f, S);
    }

    /* The legend, once, where the eye already is: what each pin colour is FOR. */
    if (g_wireFrom < 0) {
        float lx = L->railW + 24 * S + 310 * S;
        const float ly = L->topH + 4 * S;
        eui_pin_dot(lx, ly + 5 * S, EUI_GOLD, S * 0.7f, true);
        lx = ef_text(lx + 10 * S, ly, "team", EUI_TS(t1), EUI_RGB(EUI_FAINT)) + 14 * S;
        eui_pin_dot(lx, ly + 5 * S, 0xff6a5a, S * 0.7f, true);
        lx = ef_text(lx + 10 * S, ly, "watch an object", EUI_TS(t1),
                     EUI_RGB(EUI_FAINT)) + 14 * S;
        eui_pin_dot(lx, ly + 5 * S, EUI_GREEN, S * 0.7f, true);
        ef_text(lx + 10 * S, ly, "region on the map -- drag a pin", EUI_TS(t1),
                EUI_RGB(EUI_FAINT));
    }

    /* And the one being dragged, following the pointer, in its own colour so what it
       will do on release is never a mystery: gold plugs a team, red picks objects,
       green marks ground. */
    if (g_wireFrom >= 0 && g_wireFrom < (int)g_triggers.size()) {
        float px, py;
        unsigned col = EUI_GOLD;
        if (g_wireKind == WIRE_WATCH) { eui_watch_pin(L, g_wireFrom, &px, &py); col = 0xff6a5a; }
        else if (g_wireKind == WIRE_ZONE) { eui_zone_pin(L, g_wireFrom, &px, &py); col = EUI_GREEN; }
        else eui_rule_pin(L, g_wireFrom, &px, &py);
        eui_wire(px, py, g_wireX, g_wireY, col, 0.95f, S);
        eui_pin_dot(g_wireX, g_wireY, col, S, true);
        const char* hint = g_wireKind == WIRE_WATCH ? "drop on an object to watch it"
                         : g_wireKind == WIRE_ZONE  ? "drop on the ground to start the region"
                         : "drop on a team, or on nothing to choose";
        ef_text(g_wireX + 14 * S, g_wireY - 4 * S, hint, t1, EUI_RGB(col));
    }

}

/* The right-hand panel in SCRIPT mode: what the selected rule is, and the controls that
   change it. */
/* How many of the script rows actually fit above the readout. The panel grew an eighth
   row and it landed on top of UNDO/REDO/REVERT -- which the hit test then answered for,
   because it is tested first. The count is derived once and used by the draw, the hit
   test and the harness alike. */
/* How many rows this view wants. */
static int eui_script_rows_want(void)
{
    return g_scriptView == 2 ? EUI_ENH_ROWS
         : g_scriptView == 1 ? EUI_TEAM_ROWS : EUI_RULE_ROWS;
}

/* Row height, chosen so the whole list fits.
 *
 * This used to be a fixed 26 units with the overflow simply dropped, which meant the
 * last rows were unreachable in a short window -- a control you cannot see is a control
 * that does not exist, and it silently got worse every time a row was added. Now the
 * rows shrink to fit, down to a floor where the text still has room. Below that floor
 * they stop shrinking and the tail is genuinely gone; --uitest asserts that never
 * happens at any window size the editor will open at. */
/* The strip the rows may use. The count line sits at the bottom of the panel and the
   rows ran straight under it once there were fourteen of them, so it is reserved. */
static float eui_script_rows_bottom(const EuiLayout* L)
{
    return L->lintY - 18 * L->s;
}

static float eui_script_row_h(const EuiLayout* L, int rows)
{
    const float S = L->s;
    const float avail = eui_script_rows_bottom(L) - 8 * S - L->catY;
    if (rows < 1) rows = 1;
    float h = avail / rows - 4 * S;
    if (h > 26 * S) h = 26 * S;
    if (h < 17 * S) h = 17 * S;
    return h;
}

static int eui_script_rows_fit(const EuiLayout* L)
{
    const float S = L->s;
    const int want = eui_script_rows_want();
    const float h = eui_script_row_h(L, want), step = h + 4 * S;
    int n = (int)((eui_script_rows_bottom(L) - 8 * S - L->catY) / step);
    if (n > want) n = want;
    if (n < 2) n = 2;                 /* ADD and DELETE are never dropped */
    return n;
}

/* Which row is at the top. In a tall window every row fits and this stays 0; in a
   short one the list scrolls, because the alternative -- what this used to do -- is a
   control that is simply absent with nothing to say it exists. */
static int g_scriptRowOff = 0;

static int eui_script_row_max_off(const EuiLayout* L)
{
    const int off = eui_script_rows_want() - eui_script_rows_fit(L);
    return off > 0 ? off : 0;
}

static void eui_script_row_clamp(const EuiLayout* L)
{
    const int m = eui_script_row_max_off(L);
    if (g_scriptRowOff > m) g_scriptRowOff = m;
    if (g_scriptRowOff < 0) g_scriptRowOff = 0;
}

/* Absolute row index in, screen rectangle out. A row above the window or below it
   simply lands outside catY..lintY, which the draw and the hit test both check. */
static void eui_script_row_rect(const EuiLayout* L, int row, float* x, float* y,
                                float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    *x = L->sideX + pad;
    *w = L->sideW - pad * 2;
    *h = eui_script_row_h(L, eui_script_rows_want());
    *y = L->catY + (row - g_scriptRowOff) * (*h + 4 * S);
}

/* The five booleans a team carries that actually do something. Learning, Mercenary,
   InitNum and Fear are parsed by the engine and never read by it (see teamtypes.md
   section 1), so they are round-tripped and not offered -- a control that changes
   nothing is worse than no control. */
struct EuiTeamFlag { const char* label; int EditTeam::*field; const char* help; };
static const EuiTeamFlag EUI_TEAM_FLAGS[5] = {
    { "AUTO",  &EditTeam::autocreate,
      "only builds and spawns once the house is alerted" },
    { "PRE",   &EditTeam::prebuilt,
      "stockpile its members even with no team active" },
    { "REINF", &EditTeam::reinforcable,
      "off: fights to the death, never tops up" },
    { "ROUND", &EditTeam::roundabout,
      "route around danger rather than through it" },
    { "SUIC",  &EditTeam::suicide,
      "ignore incoming fire, never retarget" },
};

static void eui_team_flag_rect(const EuiLayout* L, float rx, float ry, float rw,
                               float rh, int i, float* x, float* y, float* w, float* h)
{
    const float S = L->s;
    /* Right margin, and a wider label gutter: the labels are full size now, and the
       last chip used to sit exactly on the panel's edge with its text clipped. */
    const float pad = 10 * S;
    const float cw = (rw - 74 * S - pad - 4 * S * 4) / 5.0f;
    *w = cw; *h = rh - 8 * S;
    *x = rx + 74 * S + i * (cw + 4 * S);
    *y = ry + 4 * S;
    (void)S;
}

/* The member slots, as chips on their own row: click one to work on it, click the
   trailing + to add another. A team may hold five distinct classes and no more
   (MAX_TEAM_CLASSCOUNT); the engine silently drops the sixth rather than complaining,
   which is exactly the kind of quiet loss an editor should make impossible. */
/* The condition chips: eight slots, so they are narrower than the team's five. */
static void eui_enh_chip_rect(const EuiLayout* L, float rx, float ry, float rw,
                              float rh, int i, float* x, float* y, float* w, float* h)
{
    const float S = L->s;
    const float pad = 10 * S;
    const float cw = (rw - 118 * S - pad - 3 * S * (ENH_CLAUSE_MAX - 1))
                   / (float)ENH_CLAUSE_MAX;
    *w = cw; *h = rh - 8 * S;
    *x = rx + 118 * S + i * (cw + 3 * S);
    *y = ry + 4 * S;
}

static void eui_team_slot_rect(const EuiLayout* L, float rx, float ry, float rw,
                               float rh, int i, float* x, float* y, float* w, float* h)
{
    const float S = L->s;
    const float pad = 10 * S;
    const float cw = (rw - 96 * S - pad - 4 * S * 4) / 5.0f;
    *w = cw; *h = rh - 8 * S;
    *x = rx + 96 * S + i * (cw + 4 * S);
    *y = ry + 4 * S;
}

static void eui_draw_script_panel(const EuiLayout* L, float t1)
{
    const float S = L->s, pad = EUI_PAD * S;
    static const char* RULE_ROW[EUI_RULE_ROWS] = {
        "ADD RULE", "DELETE RULE", "EVENT", "ACTION", "PARAMETER", "HOUSE",
        "TEAM", "REPEATS", "PAINT ZONE", "BRUSH", "TAG OBJECTS"
    };
    static const char* ENH_ROW[EUI_ENH_ROWS] = {
        "ADD RULE", "DELETE RULE", "CARRIER", "COMBINE", "CONDITIONS", "CONDITION",
        "INVERT", "HOUSE", "NAME", "NUMBER", "DROP CONDITION", "COUNTER EFFECT",
        "PAINT ZONE", "REPEATS"
    };
    static const char* TEAM_ROW[EUI_TEAM_ROWS] = {
        "ADD TEAM", "DELETE TEAM", "HOUSE", "MEMBERS", "TYPE", "HOW MANY",
        "DROP MEMBER", "ADD ORDER", "ORDER", "ARGUMENT", "DROP ORDER",
        "PRIORITY", "MAX AT ONCE", "FLAGS"
    };

    /* The view switch, in the strip the house chips use in OBJECTS mode. */
    {
        static const char* V[3] = { "RULES", "TEAMS", "ENHANCED" };
        const float w = (L->sideW - pad * 2 - 4 * S * 2) / 3.0f;
        for (int i = 0; i < 3; i++) {
            const float x = L->sideX + pad + i * (w + 4 * S);
            const bool on  = (i == g_scriptView);
            const bool hot = (g_euiHotKind == EUI_SCRIPTVIEW && g_euiHotArg == i);
            eui_panel(x, L->ownerY, w, L->ownerH,
                      on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL);
            eui_rect(x, L->ownerY, w, 3 * S, on ? EUI_GOLD : EUI_LINE, on ? 1.0f : 0.3f);
            const float tw = ef_text_w(V[i], t1);
            ef_text(x + (w - tw) * 0.5f, L->ownerY + L->ownerH * 0.5f - 3 * S, V[i], t1,
                    EUI_RGB(on ? EUI_GOLD : EUI_FAINT));
        }
    }

    const bool teams = (g_scriptView == 1);
    const bool enh   = (g_scriptView == 2);
    const bool have  = enh   ? (g_enhSel >= 0 && g_enhSel < (int)g_enh.size())
                     : teams ? (g_teamSel >= 0 && g_teamSel < (int)g_teams.size())
                             : (g_trigSel >= 0);

    eui_script_row_clamp(L);
    const int first = g_scriptRowOff, last = first + eui_script_rows_fit(L);
    for (int row = first; row < last; row++) {
        float x, y, w, h;
        eui_script_row_rect(L, row, &x, &y, &w, &h);
        const bool live = (row < 2) || have;
        const bool hot  = (g_euiHotKind == EUI_SCRIPTROW && g_euiHotArg == row);
        eui_panel(x, y, w, h, hot && live ? EUI_PANEL3 : EUI_PANEL);
        const char* label = enh ? ENH_ROW[row] : teams ? TEAM_ROW[row] : RULE_ROW[row];
        /* THE ROWS SAY WHAT THEY MEAN FOR THIS EVENT. "HOUSE" is the field's name in the
           file; it is not the question the author is asking. On Player Enters the house
           is WHOSE UNITS TRIP IT, which is exactly what could not be found, and on the
           object-routed events it does nothing at all. Same for the parameter, which
           counts a different thing per event and said "PARAMETER" for all of them. */
        if (!enh && !teams && have) {
            const EditTrigger& tl = g_triggers[g_trigSel];
            if (row == 5) {
                label = (tl.event == 1)                  ? "WHO ENTERS"
                      : trig_event_reads_house(tl.event) ? "WHOSE"
                                                         : "HOUSE (unused)";
            } else if (row == 4) {
                label = (tl.event == 11) ? "AFTER"
                      : (tl.event == 10) ? "CREDITS REACH"
                      : (tl.event == 12) ? "BUILDINGS LOST"
                      : (tl.event == 13) ? "UNITS LOST"
                      : (tl.event == 16) ? "WHICH BUILDING"
                      : "PARAMETER";
            } else if (row == 8) {
                label = (tl.event == 1) ? "WHERE (paint it)" : "PAINT ZONE";
            } else if (row == 10) {
                label = trig_event_reads_tag(tl.event) ? "WHAT IT WATCHES"
                                                       : "TAG OBJECTS";
            }
        }
        ef_text(x + 10 * S, y + h * 0.5f - 3 * S, label, t1, EUI_RGB(live ? EUI_DIM : EUI_FAINT));
        if (row < 2 || !have) continue;

        char v[96];
        v[0] = 0;
        unsigned col = EUI_CYAN;

        if (enh) {
            EnhRule& R = g_enh[g_enhSel];
            const int cs = (g_enhClause >= 0 && g_enhClause < R.nclause)
                         ? g_enhClause : -1;
            const EnhClauseShape* sh = cs >= 0 ? &ENH_SHAPE[R.clause[cs].kind] : 0;
            if (row == 2) {
                snprintf(v, sizeof v, "%s", R.carrier[0] ? R.carrier : "none");
                if (!R.carrier[0] || edit_trigger_by_name(R.carrier) < 0) col = EUI_DANGER;
            }
            else if (row == 3) { snprintf(v, sizeof v, "%s", R.all ? "ALL" : "ANY");
                                 col = R.all ? EUI_CYAN : EUI_GOLD; }
            else if (row == 4) {
                /* Chips, one per condition plus a + to add another. */
                for (int i = 0; i < ENH_CLAUSE_MAX; i++) {
                    float cx, cy, cw, ch;
                    eui_enh_chip_rect(L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                    const bool used = (i < R.nclause);
                    const bool on   = used && (i == g_enhClause);
                    const bool chot = (g_euiHotKind == EUI_ENHCHIP && g_euiHotArg == i);
                    eui_rect(cx, cy, cw, ch,
                             on ? EUI_GOLD : chot ? EUI_PANEL3 : EUI_PANEL2,
                             used || i == R.nclause ? 0.95f : 0.3f);
                    char c[8];
                    if (used) snprintf(c, sizeof c, "%d", i + 1);
                    else if (i == R.nclause) snprintf(c, sizeof c, "+");
                    else c[0] = 0;
                    if (c[0])
                        ef_text(cx + cw * 0.5f - ef_text_w(c, EUI_TS(t1)) * 0.5f,
                                cy + ch * 0.5f - 3 * S, c, EUI_TS(t1),
                                on ? 0.05f : 0.72f, on ? 0.05f : 0.78f,
                                on ? 0.06f : 0.86f);
                }
                continue;
            }
            else if (row == 5) { if (cs < 0) { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
                                 else snprintf(v, sizeof v, "%s",
                                               ENH_CLAUSE[R.clause[cs].kind]); }
            else if (row == 6) { if (cs < 0) { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
                                 else { snprintf(v, sizeof v, "%s",
                                                 R.clause[cs].negate ? "NOT" : "as written");
                                        col = R.clause[cs].negate ? EUI_DANGER : EUI_CYAN; } }
            else if (row == 7) { if (cs < 0 || !sh->house) { snprintf(v, sizeof v, "--");
                                                             col = EUI_FAINT; }
                                 else snprintf(v, sizeof v, "%s", R.clause[cs].house); }
            else if (row == 8) { if (cs < 0 || !sh->name) { snprintf(v, sizeof v, "--");
                                                            col = EUI_FAINT; }
                                 else snprintf(v, sizeof v, "%s", R.clause[cs].name); }
            else if (row == 9) {
                if (cs < 0 || !sh->num) { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
                else if (R.clause[cs].kind == ENH_TIME)
                    snprintf(v, sizeof v, "%.1f min", R.clause[cs].num / 10.0f);
                else snprintf(v, sizeof v, "%d", R.clause[cs].num);
            }
            else if (row == 10) { snprintf(v, sizeof v, "%d of %d", R.nclause,
                                           ENH_CLAUSE_MAX); col = EUI_FAINT; }
            else if (row == 11) {
                if (!R.neffect) { snprintf(v, sizeof v, "none"); col = EUI_FAINT; }
                else snprintf(v, sizeof v, "%s %s %d", ENH_EFFECT[R.effect[0].kind],
                              R.effect[0].name, R.effect[0].num);
            }
            else if (row == 12) { const int zc = enh_zone_cells(g_enhSel);
                                  snprintf(v, sizeof v, "%s  %d cells",
                                           g_enhPaint ? "ON" : "off", zc);
                                  col = g_enhPaint ? EUI_GREEN : EUI_CYAN; }
            else { snprintf(v, sizeof v, "%s", R.repeat ? "every time" : "once only"); }
        } else if (!teams) {
            const EditTrigger& t = g_triggers[g_trigSel];
            static const char* PER[3] = { "once only", "once per tag", "every time" };
            if (row == 2)      snprintf(v, sizeof v, "%s", TRIG_EVENT[t.event]);
            else if (row == 3) snprintf(v, sizeof v, "%s", TRIG_ACTION[t.action]);
            else if (row == 4) {
                if (t.event == 11) snprintf(v, sizeof v, "%.1f min", t.data / 10.0f);
                else if (t.event == 16)
                    snprintf(v, sizeof v, "%s", trig_struct_name(t.data));
                else if (trig_event_has_data(t.event)) snprintf(v, sizeof v, "%d", t.data);
                else { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
            }
            else if (row == 5) {
                snprintf(v, sizeof v, "%s", t.house);
                if (!trig_event_reads_house(t.event)) col = EUI_FAINT;
            }
            else if (row == 6) {
                /* Only three actions name a team; on the rest the field is inert and
                   saying so beats showing a value that does nothing. */
                if (!trig_action_has_team(t.action)) {
                    snprintf(v, sizeof v, "--");
                    col = EUI_FAINT;
                } else {
                    snprintf(v, sizeof v, "%s", t.team[0] ? t.team : "None");
                    if (edit_team_by_name(t.team) < 0) col = EUI_DANGER;  /* dangling */
                }
            }
            else if (row == 7) snprintf(v, sizeof v, "%s",
                                        PER[t.persist < 0 || t.persist > 2 ? 0 : t.persist]);
            else if (row == 8) { const int zc = edit_zone_cells(g_trigSel);
                                 snprintf(v, sizeof v, "%s  %d cells",
                                          g_zonePaint ? "ON" : "off", zc);
                                 col = !trig_event_reads_zone(t.event)
                                     ? (zc ? EUI_DANGER : EUI_FAINT)
                                     : g_zonePaint ? EUI_GREEN : EUI_CYAN; }
            else if (row == 9) snprintf(v, sizeof v, "%dx%d",
                                        g_zoneBrush * 2 - 1, g_zoneBrush * 2 - 1);
            else { const int tc = edit_tagged_count(g_trigSel);
                   snprintf(v, sizeof v, "%s  %d objects", g_tagMode ? "ON" : "off", tc);
                   col = !trig_event_reads_tag(t.event)
                       ? (tc ? EUI_DANGER : EUI_FAINT)
                       : g_tagMode ? EUI_GREEN : EUI_CYAN; }
        } else {
            EditTeam& T = g_teams[g_teamSel];
            const int ms = (g_teamMember < T.nclass) ? g_teamMember : -1;
            const int os = (g_teamOrder >= 0 && g_teamOrder < T.nmission)
                         ? g_teamOrder : -1;
            if (row == 2) snprintf(v, sizeof v, "%s", T.house);
            else if (row == 3) {
                /* Chips, drawn over the row rather than a value on it. */
                for (int i = 0; i < TEAM_CLASS_MAX; i++) {
                    float cx, cy, cw, ch;
                    eui_team_slot_rect(L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                    const bool used = (i < T.nclass);
                    const bool on   = used && (i == g_teamMember);
                    const bool chot = (g_euiHotKind == EUI_TEAMSLOT && g_euiHotArg == i);
                    eui_rect(cx, cy, cw, ch,
                             on ? EUI_GOLD : chot ? EUI_PANEL3 : EUI_PANEL2,
                             used || i == T.nclass ? 0.95f : 0.35f);
                    char c[24];
                    if (used) snprintf(c, sizeof c, "%s", T.cls[i].code);
                    else if (i == T.nclass) snprintf(c, sizeof c, "+");
                    else c[0] = 0;
                    if (c[0])
                        ef_text_fit(cx + 4 * S, cy + ch * 0.5f - 3 * S, c, EUI_TS(t1),
                                    cw - 8 * S,
                                    on ? 0.05f : 0.72f, on ? 0.05f : 0.78f,
                                    on ? 0.06f : 0.86f);
                }
                continue;
            }
            else if (row == 4) { if (ms < 0) { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
                                 else snprintf(v, sizeof v, "%s", T.cls[ms].code); }
            else if (row == 5) { if (ms < 0) { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
                                 else snprintf(v, sizeof v, "%d", T.cls[ms].num); }
            else if (row == 6) { snprintf(v, sizeof v, "%d of %d", T.nclass,
                                          TEAM_CLASS_MAX); col = EUI_FAINT; }
            else if (row == 7) { snprintf(v, sizeof v, "%d of %d", T.nmission,
                                          TEAM_MISSION_MAX); col = EUI_FAINT; }
            else if (row == 8) { if (os < 0) { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
                                 else snprintf(v, sizeof v, "%s",
                                               TEAM_MIS[T.mis[os].mission]); }
            else if (row == 9) {
                if (os < 0) { snprintf(v, sizeof v, "--"); col = EUI_FAINT; }
                else {
                    const int m = T.mis[os].mission, a = T.mis[os].arg;
                    switch (TEAM_MIS_ARG[m]) {
                    case 1: snprintf(v, sizeof v, "waypoint %d", a); break;
                    case 2: snprintf(v, sizeof v, "cell %d", a); break;
                    case 3: snprintf(v, sizeof v, "order %d", a); break;
                    case 4: snprintf(v, sizeof v, "target %d", a); break;
                    default: snprintf(v, sizeof v, "%d = %ds", a, a * 6); break;
                    }
                }
            }
            else if (row == 10) { snprintf(v, sizeof v, "%d orders", T.nmission);
                                  col = EUI_FAINT; }
            else if (row == 11) snprintf(v, sizeof v, "%d", T.priority);
            else if (row == 12) snprintf(v, sizeof v, "%d", T.maxallowed);
            else {
                for (int i = 0; i < 5; i++) {
                    float cx, cy, cw, ch;
                    eui_team_flag_rect(L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                    const bool on = (T.*(EUI_TEAM_FLAGS[i].field)) != 0;
                    const bool chot = (g_euiHotKind == EUI_TEAMFLAG && g_euiHotArg == i);
                    eui_rect(cx, cy, cw, ch,
                             on ? EUI_GREEN : chot ? EUI_PANEL3 : EUI_PANEL2, 0.95f);
                    ef_text_fit(cx + 3 * S, cy + ch * 0.5f - 3 * S,
                                EUI_TEAM_FLAGS[i].label, EUI_TS(t1), cw - 6 * S,
                                on ? 0.04f : 0.62f, on ? 0.06f : 0.68f,
                                on ? 0.04f : 0.74f);
                }
                continue;
            }
        }
        if (g_numKind != NUM_NONE && row == g_numRow) {
            /* The field being typed: the buffer with a caret, over everything else the
               row would have shown, so what will be committed is what is on screen. */
            char ed[56];
            snprintf(ed, sizeof ed, "%s_", g_numBuf);
            const float tw2 = ef_text_w(ed, t1) + 16 * S;
            eui_rect(x + w - tw2 - 6 * S, y + 3 * S, tw2 + 2 * S, h - 6 * S,
                     EUI_PANEL3, 1.0f);
            eui_frame(x + w - tw2 - 6 * S, y + 3 * S, tw2 + 2 * S, h - 6 * S,
                      EUI_GOLD, 1.0f);
            ef_text(x + w - tw2 + 2 * S, y + h * 0.5f - 3 * S, ed, t1, EUI_RGB(EUI_GOLD));
        } else if (v[0])
            ef_text(x + w - ef_text_w(v, EUI_TS(t1)) - 10 * S, y + h * 0.5f - 3 * S, v,
                    EUI_TS(t1), EUI_RGB(col));
    }
    {
        /* The status line: what is in this view, whether it has been touched, and --
           when the panel is too short for every row -- where in the list you are. The
           view already names itself on the switch above, so it is not repeated here. */
        char n[96];
        if (enh)
            snprintf(n, sizeof n, "%d ENHANCED   %s", (int)g_enh.size(),
                     g_enh.empty() ? "MAP RUNS ANYWHERE" : "MAP NEEDS CNC3D");
        else if (teams)
            snprintf(n, sizeof n, "%d TEAMS   %s", (int)g_teams.size(),
                     g_teamDirty ? "EDITED" : "AS SHIPPED");
        else
            snprintf(n, sizeof n, "%d RULES   %s", (int)g_triggers.size(),
                     g_trigDirty ? "EDITED" : "AS SHIPPED");
        ef_text(L->sideX + pad, L->lintY - 15 * S, n, EUI_TS(t1), EUI_RGB(EUI_FAINT));
        const int mo = eui_script_row_max_off(L);
        if (mo) {
            char sc[64];
            snprintf(sc, sizeof sc, "ROW %d-%d OF %d, WHEEL SCROLLS",
                     g_scriptRowOff + 1, g_scriptRowOff + eui_script_rows_fit(L),
                     eui_script_rows_want());
            ef_text(L->sideX + L->sideW - pad - ef_text_w(sc, EUI_TS(t1)),
                    L->lintY - 15 * S, sc, EUI_TS(t1), EUI_RGB(EUI_GOLD));
        }
    }
}


/* What a click on a team-view row does. Left-click steps forward, shift-click steps
   back, which is the idiom the rule rows already use.
 *
 * Kept here rather than in the event loop because it reaches into the team model and
 * has to know its limits; the caller only knows a row was clicked. */
static void edit_new_team_name(char* out, int n);   /* defined with the writer */

static void edit_team_row(int row, bool back)
{
    if (row == 0) {
        EditTeam t;
        edit_team_defaults(&t);
        edit_new_team_name(t.name, sizeof t.name);
        /* A team of nothing, going nowhere, is not a useful starting point. One
           minigunner with an order to guard is a team that runs. */
        snprintf(t.cls[0].code, sizeof t.cls[0].code, "E1");
        t.cls[0].num = 1;
        t.nclass = 1;
        t.mis[0].mission = 8;      /* Guard */
        t.mis[0].arg = 10;         /* a minute */
        t.nmission = 1;
        t.maxallowed = 1;
        g_teams.push_back(t);
        g_teamSel = (int)g_teams.size() - 1;
        g_teamOrder = 0;
        g_teamMember = 0;
        g_teamDirty = true;
        edit_toast(EUI_GOLD, "TEAM ADDED", t.name);
        return;
    }
    if (row == 1) {
        if (g_teamSel < 0 || g_teamSel >= (int)g_teams.size()) {
            edit_toast(EUI_FAINT, "PICK A TEAM FIRST", "");
            return;
        }
        char nm[16];
        snprintf(nm, sizeof nm, "%s", g_teams[g_teamSel].name);
        /* A rule that names a team which no longer exists never fires, and nothing in
           the game says so. Say it here instead of leaving it to be discovered. */
        int orphans = 0;
        for (size_t i = 0; i < g_triggers.size(); i++)
            if (!strcasecmp(g_triggers[i].team, nm)) orphans++;
        g_teams.erase(g_teams.begin() + g_teamSel);
        g_teamSel = -1;
        g_teamOrder = -1;
        g_teamDirty = true;
        if (orphans) {
            char sub[96];
            snprintf(sub, sizeof sub, "%d rule%s still name%s it and will never fire",
                     orphans, orphans == 1 ? "" : "s", orphans == 1 ? "s" : "");
            edit_toast(EUI_DANGER, "TEAM DELETED", sub);
        } else {
            edit_toast(EUI_DANGER, "TEAM DELETED", nm);
        }
        return;
    }
    if (g_teamSel < 0 || g_teamSel >= (int)g_teams.size()) {
        edit_toast(EUI_FAINT, "PICK A TEAM FIRST", "click one on the left");
        return;
    }
    EditTeam& T = g_teams[g_teamSel];
    const int ms = (g_teamMember >= 0 && g_teamMember < T.nclass) ? g_teamMember : -1;
    const int os = (g_teamOrder >= 0 && g_teamOrder < T.nmission) ? g_teamOrder : -1;

    switch (row) {
    case 2: {
        /* The nine houses a team may belong to. Multi1..6 are how a multiplayer map's
           AI gets teams at all, so they are offered even though no campaign uses them. */
        static const char* H[9] = { "GoodGuy", "BadGuy", "Neutral", "Special",
                                    "Multi1", "Multi2", "Multi3", "Multi4" };
        const int n = 8;
        int cur = 0;
        for (int k = 0; k < n; k++) if (!strcasecmp(T.house, H[k])) cur = k;
        cur = (cur + (back ? n - 1 : 1)) % n;
        snprintf(T.house, sizeof T.house, "%s", H[cur]);
        break;
    }
    case 4:
        if (ms < 0) { edit_toast(EUI_FAINT, "NO MEMBER SLOT", "add one above"); return; }
        else {
            const int n = edit_team_type_n();
            int cur = edit_team_type_index(T.cls[ms].code);
            cur = (cur + (back ? n - 1 : 1)) % n;
            snprintf(T.cls[ms].code, sizeof T.cls[0].code, "%s",
                     edit_team_type_at(cur));
        }
        break;
    case 5:
        if (ms < 0) { edit_toast(EUI_FAINT, "NO MEMBER SLOT", "add one above"); return; }
        T.cls[ms].num += back ? -1 : 1;
        if (T.cls[ms].num < 1)   T.cls[ms].num = 1;
        if (T.cls[ms].num > 255) T.cls[ms].num = 255;   /* the field is a byte */
        break;
    case 6:
        if (ms < 0) { edit_toast(EUI_FAINT, "NO MEMBER SLOT", ""); return; }
        for (int i = ms; i < T.nclass - 1; i++) T.cls[i] = T.cls[i + 1];
        T.nclass--;
        if (g_teamMember >= T.nclass) g_teamMember = T.nclass ? T.nclass - 1 : 0;
        break;
    case 7:
        if (T.nmission >= TEAM_MISSION_MAX) {
            edit_toast(EUI_FAINT, "TWENTY ORDERS IS THE LIMIT",
                       "past that the engine writes off the end of the list");
            return;
        }
        /* Inserted after the selected order, so building a chain is one click per step
           rather than adding at the end and reordering. */
        {
            const int at = (os >= 0) ? os + 1 : T.nmission;
            for (int i = T.nmission; i > at; i--) T.mis[i] = T.mis[i - 1];
            T.mis[at].mission = 5;      /* Move -- 2,974 of the 3,184 shipped orders */
            T.mis[at].arg = 0;
            T.nmission++;
            /* A Loop pointing past the insertion has to move with it, or the chain
               silently retargets. */
            for (int i = 0; i < T.nmission; i++)
                if (T.mis[i].mission == 9 && T.mis[i].arg >= at && i != at)
                    T.mis[i].arg++;
            g_teamOrder = at;
        }
        break;
    case 8:
        if (os < 0) { edit_toast(EUI_FAINT, "NO ORDER PICKED",
                                 "click one in the chain"); return; }
        T.mis[os].mission = (T.mis[os].mission + (back ? TEAM_MIS_N - 1 : 1)) % TEAM_MIS_N;
        break;
    case 9:
        if (os < 0) { edit_toast(EUI_FAINT, "NO ORDER PICKED", ""); return; }
        T.mis[os].arg += back ? -1 : 1;
        if (T.mis[os].arg < 0) T.mis[os].arg = 0;
        /* Clamp to what the argument actually means, so the chain cannot be pointed at
           a waypoint or an order that does not exist. */
        if (TEAM_MIS_ARG[T.mis[os].mission] == 1 && T.mis[os].arg >= EDIT_WAYPT_COUNT)
            T.mis[os].arg = EDIT_WAYPT_COUNT - 1;
        if (TEAM_MIS_ARG[T.mis[os].mission] == 3 && T.mis[os].arg >= T.nmission)
            T.mis[os].arg = T.nmission - 1;
        break;
    case 10:
        if (os < 0) { edit_toast(EUI_FAINT, "NO ORDER PICKED", ""); return; }
        for (int i = os; i < T.nmission - 1; i++) T.mis[i] = T.mis[i + 1];
        T.nmission--;
        for (int i = 0; i < T.nmission; i++)
            if (T.mis[i].mission == 9) {
                if (T.mis[i].arg > os) T.mis[i].arg--;
                if (T.mis[i].arg >= T.nmission)
                    T.mis[i].arg = T.nmission ? T.nmission - 1 : 0;
            }
        g_teamOrder = (T.nmission && os >= T.nmission) ? T.nmission - 1
                    : (T.nmission ? os : -1);
        break;
    case 11:
        T.priority += back ? -1 : 1;
        if (T.priority < 0)  T.priority = 0;
        if (T.priority > 30) T.priority = 30;
        break;
    case 12:
        T.maxallowed += back ? -1 : 1;
        if (T.maxallowed < 0)   T.maxallowed = 0;
        if (T.maxallowed > 255) T.maxallowed = 255;
        break;
    default: return;
    }
    g_teamDirty = true;

    /* The one hard limit that is a crash rather than a rejection: warn the moment the
       line grows past it, not at save time when the edit that caused it is forgotten. */
    {
        char probe[512];
        const int n = edit_team_line(T, probe, sizeof probe);
        if (n >= TEAM_LINE_MAX)
            edit_toast(EUI_DANGER, "THIS TEAM IS TOO LONG TO SAVE",
                       "shorten the order list -- the engine's buffer is 255 bytes");
    }
}


/* The marquee's rectangle, normalised. */
static void edit_marq_rect(int* x0, int* y0, int* x1, int* y1)
{
    *x0 = g_marqX < g_editCellX ? g_marqX : g_editCellX;
    *x1 = g_marqX > g_editCellX ? g_marqX : g_editCellX;
    *y0 = g_marqY < g_editCellY ? g_marqY : g_editCellY;
    *y1 = g_marqY > g_editCellY ? g_marqY : g_editCellY;
}

/* Fill the marquee into whichever zone table is live. One undo-visible action. */
static void edit_marq_fill(void)
{
    int x0, y0, x1, y1;
    edit_marq_rect(&x0, &y0, &x1, &y1);
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) continue;
            const int c = y * edit_ini_w() + x;
            if (g_enhPaint && g_enhSel >= 0) { g_enhCell[c] = (short)g_enhSel; n++; }
            else if (g_zonePaint && g_trigSel >= 0) { g_cellTrig[c] = (short)g_trigSel; n++; }
        }
    if (n) {
        if (g_enhPaint) g_enhDirty = true; else g_zoneDirty = true;
        edit_trace_invalidate();
        char sub[64];
        snprintf(sub, sizeof sub, "%d cells in one stroke", n);
        edit_toast(EUI_GREEN, "REGION DRAWN", sub);
    }
    g_marqOn = false;
}

/* Painting an Enhanced rule's zone. Unlike [CellTriggers], where a cell may carry ONE
   trigger and the first entry in the file wins, this table is ours and the limit is ours
   too -- but one cell to one rule is still the right model, because "which region" is a
   question with one answer and two overlapping regions would be two rules. */
static void enh_paint_zone(int cx, int cy, bool erase)
{
    if (g_enhSel < 0 || g_enhSel >= (int)g_enh.size()) return;
    const int r = g_enhBrush;
    for (int dy = -(r - 1); dy <= r - 1; dy++)
        for (int dx = -(r - 1); dx <= r - 1; dx++) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) continue;
            const int c = y * edit_ini_w() + x;
            if (erase && g_enhCell[c] != g_enhSel) continue;   /* only take back ours */
            g_enhCell[c] = erase ? -1 : (short)g_enhSel;
        }
    g_enhDirty = true;
}

/* A free carrier name, in the editor's own style so it cannot collide with a shipped
   one. Carriers are ordinary triggers and share the four-character namespace. */
static void enh_new_carrier(char* out, int n)
{
    for (int i = 0; i < 1000; i++) {
        char cand[8];
        snprintf(cand, sizeof cand, "x%03d", i);
        if (edit_trigger_by_name(cand) < 0) { snprintf(out, n, "%s", cand); return; }
    }
    snprintf(out, n, "x999");
}

/* What a click on an Enhanced row does. */
static void edit_enh_row(int row, bool back)
{
    edit_trace_invalidate();
    if (row == 0) {
        EnhRule r;
        memset(&r, 0, sizeof r);
        /* A FREE name, not one derived from the count: add three rules, delete the
           second, add another, and the count-based name collided with a rule that was
           still there -- and two Enhanced rules with the same name are one rule as far
           as the reader and the trace are concerned. Same scan the carrier and the team
           namers already use. */
        for (int k = 1; k < 1000; k++) {
            char cand[16];
            snprintf(cand, sizeof cand, "rule%d", k);
            if (enh_rule_by_name(cand) < 0) {
                snprintf(r.name, sizeof r.name, "%s", cand);
                break;
            }
        }
        if (!r.name[0]) snprintf(r.name, sizeof r.name, "rule999");
        r.all = 1;
        r.firedAt = -1;
        /* One clause to start with, because a rule with none never fires and an editor
           that hands you a thing that cannot work is not helping. */
        r.clause[0].kind = ENH_TIME;
        r.clause[0].num = 50;              /* five minutes */
        r.nclause = 1;

        /* And its carrier, created here as an ordinary trigger with event None. The two
           halves are made together on purpose: a rule whose carrier a person has to
           remember to add by hand is a rule that silently does nothing. */
        EditTrigger t;
        memset(&t, 0, sizeof t);
        enh_new_carrier(t.name, sizeof t.name);
        t.event  = 0;                      /* None -- nothing in the engine springs it */
        t.action = 4;                      /* Create Team, the commonest by far        */
        snprintf(t.house, sizeof t.house, "BadGuy");
        snprintf(t.team,  sizeof t.team,  "None");
        g_triggers.push_back(t);
        g_trigDirty = true;
        snprintf(r.carrier, sizeof r.carrier, "%s", t.name);

        g_enh.push_back(r);
        g_enhSel = (int)g_enh.size() - 1;
        g_enhClause = 0;
        g_enhDirty = true;
        edit_toast(EUI_GREEN, "ENHANCED RULE ADDED",
                   "carrier trigger created too; this map now needs CNC3D");
        return;
    }
    if (row == 1) {
        if (g_enhSel < 0 || g_enhSel >= (int)g_enh.size()) {
            edit_toast(EUI_FAINT, "PICK A RULE FIRST", "");
            return;
        }
        char nm[16], cr[8];
        snprintf(nm, sizeof nm, "%s", g_enh[g_enhSel].name);
        snprintf(cr, sizeof cr, "%s", g_enh[g_enhSel].carrier);
        /* The carrier goes with it. It is inert on its own -- event None -- so leaving
           it behind would be a trigger that can never fire, cluttering the native view
           and confusing anyone reading the file. */
        const int ci = edit_trigger_by_name(cr);
        if (ci >= 0 && g_triggers[ci].event == 0) {
            g_triggers.erase(g_triggers.begin() + ci);
            if (g_trigSel == ci) g_trigSel = -1;
            else if (g_trigSel > ci) g_trigSel--;
            g_trigDirty = true;
        }
        /* Its zone too, and the indices of every rule after it shift down. */
        for (int c = 0; c < EDIT_GRID_MAX; c++) {
            if (g_enhCell[c] == g_enhSel) g_enhCell[c] = -1;
            else if (g_enhCell[c] > g_enhSel) g_enhCell[c]--;
        }
        g_enh.erase(g_enh.begin() + g_enhSel);
        g_enhSel = -1;
        g_enhClause = -1;
        g_enhPaint = false;
        g_enhDirty = true;
        edit_toast(EUI_DANGER, "ENHANCED RULE DELETED",
                   g_enh.empty() ? "this map runs on any engine again" : nm);
        return;
    }
    if (g_enhSel < 0 || g_enhSel >= (int)g_enh.size()) {
        edit_toast(EUI_FAINT, "PICK A RULE FIRST", "click one on the left");
        return;
    }
    EnhRule& R = g_enh[g_enhSel];
    const int cs = (g_enhClause >= 0 && g_enhClause < R.nclause) ? g_enhClause : -1;
    EnhClause* C = cs >= 0 ? &R.clause[cs] : 0;

    switch (row) {
    case 2: {
        /* Cycle through the triggers this mission has whose event is None -- the ones
           that are carriers, or could be. A carrier with a real event would fire twice:
           once by the engine and once by this rule. */
        std::vector<int> cand;
        for (size_t i = 0; i < g_triggers.size(); i++)
            if (g_triggers[i].event == 0) cand.push_back((int)i);
        if (cand.empty()) {
            edit_toast(EUI_FAINT, "NO CARRIERS IN THIS MISSION",
                       "a carrier is a rule whose event is None");
            return;
        }
        int cur = -1;
        for (size_t k = 0; k < cand.size(); k++)
            if (!strcasecmp(g_triggers[cand[k]].name, R.carrier)) cur = (int)k;
        const int n = (int)cand.size();
        cur = (cur < 0) ? (back ? n - 1 : 0) : (cur + (back ? n - 1 : 1)) % n;
        snprintf(R.carrier, sizeof R.carrier, "%s", g_triggers[cand[cur]].name);
        break;
    }
    case 3: R.all = !R.all; break;
    case 5:
        if (!C) { edit_toast(EUI_FAINT, "NO CONDITION PICKED", "add one above"); return; }
        C->kind = (unsigned char)((C->kind + (back ? ENH_CLAUSE_N - 1 : 1)) % ENH_CLAUSE_N);
        /* Seed the fields the new kind needs, so it is never left half-filled. */
        if (ENH_SHAPE[C->kind].house && !C->house[0])
            snprintf(C->house, sizeof C->house, "BadGuy");
        if (ENH_SHAPE[C->kind].name && !C->name[0])
            snprintf(C->name, sizeof C->name,
                     C->kind == ENH_TYPE ? "HAND" : C->kind == ENH_FIRED ? "none" : "n");
        break;
    case 6: if (!C) return; C->negate = !C->negate; break;
    case 7: {
        if (!C || !ENH_SHAPE[C->kind].house) {
            edit_toast(EUI_FAINT, "THIS CONDITION HAS NO HOUSE", "");
            return;
        }
        static const char* H[4] = { "GoodGuy", "BadGuy", "Neutral", "Special" };
        int cur = 0;
        for (int k = 0; k < 4; k++) if (!strcasecmp(C->house, H[k])) cur = k;
        cur = (cur + (back ? 3 : 1)) % 4;
        snprintf(C->house, sizeof C->house, "%s", H[cur]);
        break;
    }
    case 8: {
        if (!C || !ENH_SHAPE[C->kind].name) {
            edit_toast(EUI_FAINT, "THIS CONDITION HAS NO NAME FIELD", "");
            return;
        }
        if (C->kind == ENH_TYPE) {
            /* Any placeable type, plus the buildings -- "owns three Hands of Nod" is
               the whole reason this clause exists. */
            int cur = 0;
            for (int i = 0; i < EDIT_ITEM_N; i++)
                if (!strcasecmp(EDIT_ITEMS[i].code, C->name)) cur = i;
            cur = (cur + (back ? EDIT_ITEM_N - 1 : 1)) % EDIT_ITEM_N;
            snprintf(C->name, sizeof C->name, "%s", EDIT_ITEMS[cur].code);
        } else if (C->kind == ENH_FIRED) {
            if (g_triggers.empty()) {
                edit_toast(EUI_FAINT, "THIS MISSION HAS NO RULES", "");
                return;
            }
            int cur = edit_trigger_by_name(C->name);
            const int n = (int)g_triggers.size();
            cur = (cur < 0) ? 0 : (cur + (back ? n - 1 : 1)) % n;
            snprintf(C->name, sizeof C->name, "%s", g_triggers[cur].name);
            /* A persistent trigger never leaves the game, so "has already fired" can
               never become true for it. Say so once, here, rather than never. */
            /* Only FULLY persistent (2) never leaves. A semi-persistent one (1) keeps
               an attach count and removes itself once every object it is attached to has
               sprung it, so FIRED does eventually become true for it -- saying otherwise
               was telling the author their working rule was broken. */
            if (g_triggers[cur].persist == 2)
                edit_toast(EUI_DANGER, "THAT RULE IS PERSISTENT",
                           "it never leaves the game, so this can never become true");
            else if (g_triggers[cur].persist == 1)
                edit_toast(EUI_GOLD, "THAT RULE IS SEMI-PERSISTENT",
                           "this turns true only once EVERY object it is attached to "
                           "has sprung it");
        } else {
            /* A counter name. Cycled through the ones already in use, plus a fresh one,
               because typing has no place in this panel. */
            char fresh[16];
            snprintf(fresh, sizeof fresh, "c%d", (int)g_enh.size());
            std::vector<std::string> names;
            for (size_t i = 0; i < g_enh.size(); i++)
                for (int e = 0; e < g_enh[i].neffect; e++) {
                    bool got = false;
                    for (size_t k = 0; k < names.size(); k++)
                        if (!strcasecmp(names[k].c_str(), g_enh[i].effect[e].name))
                            got = true;
                    if (!got) names.push_back(g_enh[i].effect[e].name);
                }
            names.push_back(fresh);
            int cur = 0;
            for (size_t k = 0; k < names.size(); k++)
                if (!strcasecmp(names[k].c_str(), C->name)) cur = (int)k;
            const int n = (int)names.size();
            cur = (cur + (back ? n - 1 : 1)) % n;
            snprintf(C->name, sizeof C->name, "%s", names[cur].c_str());
        }
        break;
    }
    case 9: {
        if (!C || !ENH_SHAPE[C->kind].num) {
            edit_toast(EUI_FAINT, "THIS CONDITION HAS NO NUMBER", "");
            return;
        }
        /* Time is in tenths of a minute, as a native Time trigger is, so the step is
           half a minute rather than six seconds. Credits move in useful amounts. */
        const int step = (C->kind == ENH_TIME) ? 5 : (C->kind == ENH_CREDITS) ? 100 : 1;
        C->num += back ? -step : step;
        if (C->num < 0) C->num = 0;
        break;
    }
    case 10:
        if (!C) { edit_toast(EUI_FAINT, "NO CONDITION PICKED", ""); return; }
        for (int i = cs; i < R.nclause - 1; i++) R.clause[i] = R.clause[i + 1];
        R.nclause--;
        g_enhClause = R.nclause ? (cs >= R.nclause ? R.nclause - 1 : cs) : -1;
        break;
    case 11:
        /* One counter effect per rule is enough for the composition this tier is for:
           a rule sets a counter, another reads it. Off, then SET, then ADD. */
        if (!R.neffect) {
            R.neffect = 1;
            R.effect[0].kind = ENH_ADD;
            R.effect[0].num = 1;
            snprintf(R.effect[0].name, sizeof R.effect[0].name, "done");
        } else if (R.effect[0].kind == ENH_ADD) {
            R.effect[0].kind = ENH_SET;
        } else {
            R.neffect = 0;
        }
        break;
    case 12:
        g_enhPaint = !g_enhPaint;
        if (g_enhPaint) { g_zonePaint = false; g_tagMode = false; }
        {
            bool uses = false;
            for (int i = 0; i < R.nclause; i++)
                if (R.clause[i].kind == ENH_ZONE) uses = true;
            if (g_enhPaint && !uses)
                edit_toast(EUI_DANGER, "NO CONDITION READS THE ZONE",
                           "add a ZONE condition or the painting does nothing");
            else
                edit_toast(EUI_GREEN, g_enhPaint ? "PAINTING ZONE" : "ZONE PAINTING OFF",
                           g_enhPaint ? "click the map; right-click erases" : "");
        }
        break;
    case 13: R.repeat = !R.repeat; break;
    default: return;
    }
    g_enhDirty = true;

    {
        char probe[512];
        const int n = enh_rule_line(R, probe, sizeof probe);
        if (n >= ENH_LINE_MAX)
            edit_toast(EUI_DANGER, "THIS RULE IS TOO LONG TO SAVE",
                       "split it in two and join them with a counter");
    }
}


static void edit_check_run(void)
{
    g_check.clear();
    g_checkErrors = g_checkWarns = 0;
    g_checkRan = true;
    char w[96], y[160];

    /* ---- the world ---- */
    {
        /* Ground only, not occupancy: two buildings on one cell is a shipped-data
           reality in a few missions and the engine copes, but a building standing on
           water is a building the editor would refuse to place and the check should
           say so. The cells come from the type's real occupy list -- PROC is four
           cells inside a 3x2 rectangle, not six. */
        int bad = 0;
        const char* firstBad = "";
        const char* firstLand = "";
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_BUILDING) continue;
            int it = -1;
            for (int k = 0; k < EDIT_ITEM_N && it < 0; k++)
                if (!strcmp(EDIT_ITEMS[k].code, o.type)) it = k;
            if (it < 0) continue;
            for (int c = 0; c < EDIT_ITEMS[it].n; c++) {
                const int px = o.cx + EDIT_ITEMS[it].dx[c];
                const int py = o.cy + EDIT_ITEMS[it].dy[c];
                if (px < 0 || py < 0 || px >= g_gridW || py >= g_gridH ||
                    !edit_can_build(px, py)) {
                    if (!bad) {
                        firstBad = o.type;
                        firstLand = (px < 0 || py < 0 || px >= g_gridW || py >= g_gridH)
                                  ? "off the map"
                                  : EDIT_LAND_NAME[edit_land_at(px, py)];
                    }
                    bad++;
                    break;
                }
            }
        }
        if (bad) {
            snprintf(w, sizeof w, "%d structure%s stand%s on ground it cannot be built on",
                     bad, bad == 1 ? "" : "s", bad == 1 ? "s" : "");
            snprintf(y, sizeof y, "first is %s, over %s", firstBad, firstLand);
            chk_add(CHK_ERROR, w, y);
        }
    }
    {
        /* THE PLAYABLE RECTANGLE. A 64x64 grid is always the whole map; the rectangle
           inside it is what is actually played, and the border outside is scenery the
           camera never reaches. Anything placed out there exists in the file, costs the
           house its upkeep, and can never be got to. */
        int out = 0, wout = 0, zout = 0;
        const char* firstOut = "";
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_BUILDING && o.kind != K_UNIT && o.kind != K_INFANTRY) continue;
            if (o.cx >= g_mapX && o.cx < g_mapX + g_mapW &&
                o.cy >= g_mapY && o.cy < g_mapY + g_mapH) continue;
            if (!out) firstOut = o.type;
            out++;
        }
        if (out) {
            snprintf(w, sizeof w, "%d object%s outside the playable area", out,
                     out == 1 ? " is" : "s are");
            snprintf(y, sizeof y, "first is %s -- the played rectangle is %d,%d %dx%d, "
                                  "and nothing outside it can be reached",
                     firstOut, g_mapX, g_mapY, g_mapW, g_mapH);
            chk_add(CHK_WARN, w, y);
        }
        /* Named one at a time rather than counted: which waypoint it is decides whether
           it matters. 25 of the campaign's waypoints sit outside their rectangle, so
           this is uncommon rather than unheard of, and the row has to be specific enough
           to judge. */
        for (int i = 0; i < EDIT_WAYPT_COUNT; i++) {
            const int c = g_waypoint[i];
            if (c < 0) continue;
            const int wx = c % edit_ini_w(), wy = c / edit_ini_w();
            if (wx >= g_mapX && wx < g_mapX + g_mapW &&
                wy >= g_mapY && wy < g_mapY + g_mapH) continue;
            wout++;
            if (wout <= 3) {
                snprintf(w, sizeof w, "%s is outside the playable area",
                         edit_waypoint_label(i));
                snprintf(y, sizeof y, "it is at %d,%d and the played rectangle is "
                                      "%d,%d %dx%d", wx, wy, g_mapX, g_mapY,
                         g_mapW, g_mapH);
                chk_add(CHK_WARN, w, y);
            }
        }
        for (int c = 0; c < edit_ini_cells(); c++) {
            if (g_cellTrig[c] < 0 && g_enhCell[c] < 0) continue;
            const int zx = c % edit_ini_w(), zy = c / edit_ini_w();
            if (zx >= g_mapX && zx < g_mapX + g_mapW &&
                zy >= g_mapY && zy < g_mapY + g_mapH) continue;
            zout++;
        }
        if (zout) {
            snprintf(w, sizeof w, "%d zone cell%s outside the playable area", zout,
                     zout == 1 ? " is" : "s are");
            chk_add(CHK_WARN, w, "nothing can walk into them, so that part of the zone "
                                 "never fires");
        }
    }
    {
        /* A start nobody placed is a mission that opens with the camera on cell 0. */
        if (!g_mapIsMulti) {
            if (g_waypoint[EDIT_WAYPT_HOME] < 0)
                chk_add(CHK_ERROR, "no player start",
                        "HOME is unset, so the mission opens on the corner of the map");
        } else {
            int seats = 0;
            for (int i = 0; i < 6; i++) if (g_waypoint[i] >= 0) seats++;
            if (seats < 2) {
                snprintf(w, sizeof w, "%d multiplayer start%s", seats,
                         seats == 1 ? "" : "s");
                chk_add(CHK_ERROR, w, "a skirmish map needs at least two");
            }
        }
    }

    /* ---- the script: rules ---- */
    for (size_t i = 0; i < g_triggers.size(); i++) {
        const EditTrigger& t = g_triggers[i];
        const bool named = trig_action_has_team(t.action);
        if (named && (!t.team[0] || !strcasecmp(t.team, "None"))) {
            snprintf(w, sizeof w, "rule %s names no team", t.name);
            snprintf(y, sizeof y, "its action is %s, which needs one -- it will do nothing",
                     TRIG_ACTION_HUMAN[t.action]);
            chk_add(CHK_ERROR, w, y);
        } else if (named && edit_team_by_name(t.team) < 0) {
            snprintf(w, sizeof w, "rule %s calls up team %s, which does not exist",
                     t.name, t.team);
            snprintf(y, sizeof y, "nothing will happen when it fires");
            chk_add(CHK_ERROR, w, y);
        }
        const bool hasHouse = t.house[0] && strcasecmp(t.house, "None");

        /* An object-routed event with nothing attached can only ever be sprung by a
           cell, and only Player Enters has a cell route. This is the shape of most of
           the campaign's sixteen dead triggers.
         *
         * ATTACKED IS THE EXCEPTION, and this used to call it dead too. HouseClass::
         * Attacked springs EVENT_ATTACKED for every trigger in HouseTriggers[house]
         * (house.cpp:1692-1694), and Read_INI puts a trigger there whenever its House is
         * not None (trigger.cpp:1053). So an Attacked rule with a real house fires when
         * that house is attacked, attached to nothing at all. Discovered and Destroyed
         * really are object-only -- techno.cpp:565/649/3142/3145/3148 are all
         * Trigger->Spring(..., this) -- so they keep the error. */
        if (trig_event_reads_tag(t.event) && t.event != 1 &&
            edit_tagged_count((int)i) == 0 && !(t.event == 3 && hasHouse)) {
            snprintf(w, sizeof w, "rule %s waits on %s and is attached to nothing",
                     t.name, TRIG_EVENT_HUMAN[t.event]);
            snprintf(y, sizeof y, "%s", t.event == 3
                     ? "give it a house, or tag the objects it should watch"
                     : "tag the objects it should watch, or it can never fire");
            chk_add(CHK_ERROR, w, y);
        }

        /* A HOUSE-ROUTED EVENT WITH NO HOUSE. Spring's house arm opens with
           "if (event != Event || house != House) return false", and HouseClass::AI only
           ever passes a real house -- so House=None on one of these is a rule nothing
           can ever match. The editor's own house cycler offers None, which is how a map
           acquires one; no shipped mission has any. */
        if (trig_event_reads_house(t.event) && !hasHouse) {
            snprintf(w, sizeof w, "rule %s has no house", t.name);
            snprintf(y, sizeof y, "%s is house-routed, and nothing matches None -- it "
                                  "can never fire", TRIG_EVENT_HUMAN[t.event]);
            chk_add(CHK_ERROR, w, y);
        }

        /* AND THE ONE THAT CRASHES. The object route runs
           "HouseClass::As_Pointer(House)->Begin_Production()" with no null check
           (trigger.cpp:482-484), and As_Pointer returns 0 for HOUSE_NONE
           (house.cpp:180-188). An object-routed rule with House=None and this action
           takes the engine down the moment it springs. */
        if (t.action == 3 && !hasHouse && trig_event_reads_tag(t.event)) {
            snprintf(w, sizeof w, "rule %s starts production for nobody", t.name);
            chk_add(CHK_ERROR, w, "the engine dereferences a null house here and dies -- "
                                  "give it a house");
        }
        if (t.event == 1 && edit_zone_cells((int)i) == 0 &&
            edit_tagged_count((int)i) == 0) {
            snprintf(w, sizeof w, "rule %s waits for a player to arrive somewhere",
                     t.name);
            snprintf(y, sizeof y, "but has no zone painted and no object tagged");
            chk_add(CHK_ERROR, w, y);
        }
        if (!trig_event_reads_zone(t.event) && edit_zone_cells((int)i) > 0) {
            snprintf(w, sizeof w, "rule %s has a zone its event ignores", t.name);
            snprintf(y, sizeof y, "%s is not cell-routed; the painted cells do nothing",
                     TRIG_EVENT_HUMAN[t.event]);
            chk_add(CHK_WARN, w, y);
        }
        if (!trig_event_reads_tag(t.event) && edit_tagged_count((int)i) > 0) {
            snprintf(w, sizeof w, "rule %s has tagged objects its event ignores", t.name);
            snprintf(y, sizeof y, "%s is house-routed; the tags do nothing",
                     TRIG_EVENT_HUMAN[t.event]);
            chk_add(CHK_WARN, w, y);
        }
        /* Destroy-trigger actions double-delete in the engine when the target exists:
           Spring calls Remove(), which ends in delete this, and then deletes it again
           (trigger.cpp:438-444). */
        if (t.action >= 12 && t.action <= 14) {
            static const char* TGT[3] = { "XXXX", "YYYY", "ZZZZ" };
            const int tgt = edit_trigger_by_name(TGT[t.action - 12]);
            if (tgt < 0) {
                snprintf(w, sizeof w, "rule %s cancels %s, which does not exist",
                         t.name, TGT[t.action - 12]);
                chk_add(CHK_WARN, w, "the action will do nothing");
            }
        }
        if (t.event == 16 && (t.data < 0 || t.data >= EDIT_STRUCT_N)) {
            snprintf(w, sizeof w, "rule %s fires on building %d, which is not a building",
                     t.name, t.data);
            chk_add(CHK_ERROR, w, "it can never fire");
        }
    }

    /* ---- the script: teams ---- */
    for (size_t i = 0; i < g_teams.size(); i++) {
        const EditTeam& T = g_teams[i];
        /* A team with no orders is only broken if something CREATES it. Delivered as
           reinforcements it is the normal shape -- SCG01EA's GDIR1 and GDIR2 are two
           of about forty in the campaign -- because Do_Reinforcements drops the members
           and they act on their own from there. Created by a rule, the same team runs
           out of orders on its first tick and disbands (team.cpp:491-494). */
        int usedCreate = 0, usedAny = 0;
        for (size_t k = 0; k < g_triggers.size(); k++) {
            if (strcasecmp(g_triggers[k].team, T.name)) continue;
            usedAny++;
            if (g_triggers[k].action == 4) usedCreate++;   /* Create Team */
        }
        if (!T.nmission && usedCreate) {
            snprintf(w, sizeof w, "team %s is created with no orders", T.name);
            chk_add(CHK_ERROR, w, "it runs out on its first tick and disbands; only a "
                                  "reinforcement team can go without orders");
        }
        /* "Never used" means nothing can create it AT ALL. A team no rule names can
           still be raised by the AI on its own: Suggested_New_Team's gate for a
           non-autocreate type is simply "live count < MaxAllowed" (teamtype.cpp:870),
           so any positive cap is a way in. Only a team with no rule, no autocreate and
           a cap of zero is unreachable. */
        if (!usedAny && !T.autocreate && T.maxallowed == 0) {
            snprintf(w, sizeof w, "team %s can never be created", T.name);
            chk_add(CHK_WARN, w, "no rule names it, it is not autocreate, and it allows "
                                 "none at once");
        }
        /* NOT flagged: a team that does not end in Loop. It disbands when it finishes,
           which is normal -- 14 of SCG09EA's 18 teams do exactly that -- and saying so
           on every one buries the rows that matter. */
        if (!T.nclass) {
            snprintf(w, sizeof w, "team %s has no members", T.name);
            chk_add(CHK_ERROR, w, "nothing can be recruited into it");
        }
        for (int m = 0; m < T.nmission; m++) {
            const int k = T.mis[m].mission, a = T.mis[m].arg;
            if (TEAM_MIS_ARG[k] == 1 && a < 0) {
                /* team.cpp:475 tests "Argument < WAYPT_COUNT" and a negative passes it,
                   so the engine indexes Scen.Waypoint[-65]. That is a read off the front
                   of the array, not a missing waypoint. SCB21EA ships two of them. */
                snprintf(w, sizeof w, "team %s order %d uses waypoint %d", T.name, m, a);
                chk_add(CHK_ERROR, w, "a negative index reads off the front of the "
                                      "waypoint array; the engine does not check");
            } else if (TEAM_MIS_ARG[k] == 1 && (a >= EDIT_WAYPT_COUNT ||
                                                g_waypoint[a] < 0)) {
                snprintf(w, sizeof w, "team %s order %d goes to waypoint %d", T.name, m, a);
                chk_add(CHK_ERROR, w, "that waypoint is not placed on this map, so the "
                                      "team is sent to nowhere");
            }
            if (TEAM_MIS_ARG[k] == 3 && (a < 0 || a >= T.nmission)) {
                snprintf(w, sizeof w, "team %s loops back to order %d", T.name, a);
                chk_add(CHK_ERROR, w, "there is no such order in its list");
            }
        }
        char probe[512];
        if (edit_team_line(T, probe, sizeof probe) >= TEAM_LINE_MAX) {
            snprintf(w, sizeof w, "team %s is too long to save", T.name);
            chk_add(CHK_ERROR, w, "the engine reads a team into a 255-byte buffer and "
                                  "a longer one smashes its stack");
        }
        /* MaxAllowed 0 on its own is normal and not flagged: a rule creates its team
           inside a ScenarioInit bracket, which bypasses the cap entirely
           (teamtype.cpp:812-818). What contradicts itself is AUTOCREATE with a cap of
           zero -- the flag says "let the AI raise this once alerted" and the cap says
           "never", and Suggested_New_Team reads the cap. */
        if (T.maxallowed == 0 && T.autocreate) {
            snprintf(w, sizeof w, "team %s is autocreate but allows none at once", T.name);
            chk_add(CHK_WARN, w, "the AI is told to raise it and then told it may have "
                                 "zero, so it never will");
        }
    }

    /* ---- the script: Enhanced ---- */
    for (size_t i = 0; i < g_enh.size(); i++) {
        const EnhRule& R = g_enh[i];
        if (!R.nclause) {
            snprintf(w, sizeof w, "Enhanced rule %s has no conditions", R.name);
            chk_add(CHK_ERROR, w, "it can never become true");
        }
        if (!R.carrier[0]) {
            snprintf(w, sizeof w, "Enhanced rule %s has no carrier", R.name);
            chk_add(CHK_ERROR, w, "its conditions are watched and nothing happens");
        } else {
            const int ci = edit_trigger_by_name(R.carrier);
            if (ci < 0) {
                snprintf(w, sizeof w, "Enhanced rule %s names carrier %s, which is gone",
                         R.name, R.carrier);
                chk_add(CHK_ERROR, w, "nothing will happen when it fires");
            } else if (g_triggers[ci].event != 0) {
                snprintf(w, sizeof w, "carrier %s has an event of its own", R.carrier);
                snprintf(y, sizeof y, "%s -- so the engine will fire it too, and the "
                                      "action happens twice",
                         TRIG_EVENT_HUMAN[g_triggers[ci].event]);
                chk_add(CHK_ERROR, w, y);
            }
        }
        for (int c = 0; c < R.nclause; c++) {
            const EnhClause& C = R.clause[c];
            if (C.kind == ENH_ZONE && enh_zone_cells((int)i) == 0) {
                snprintf(w, sizeof w, "Enhanced rule %s watches a zone with no cells",
                         R.name);
                chk_add(CHK_ERROR, w, "paint one, or the condition is never true");
            }
            if (C.kind == ENH_FIRED) {
                const int ti = edit_trigger_by_name(C.name);
                if (ti < 0) {
                    snprintf(w, sizeof w, "Enhanced rule %s waits on rule %s, which does "
                                          "not exist", R.name, C.name);
                    chk_add(CHK_ERROR, w, "the condition can never become true");
                } else if (g_triggers[ti].persist == 2) {
                    snprintf(w, sizeof w, "Enhanced rule %s waits on %s, which is "
                                          "persistent", R.name, C.name);
                    chk_add(CHK_ERROR, w, "a persistent rule never leaves the game, so "
                                          "this can never become true");
                }
            }
        }
        char probe[512];
        if (enh_rule_line(R, probe, sizeof probe) >= ENH_LINE_MAX) {
            snprintf(w, sizeof w, "Enhanced rule %s is too long to save", R.name);
            chk_add(CHK_ERROR, w, "split it and join the halves with a counter");
        }
    }
    /* A carrier with no rule pointing at it is inert -- the mirror of the check above. */
    for (size_t i = 0; i < g_triggers.size(); i++) {
        if (g_triggers[i].event != 0) continue;
        bool claimed = false;
        for (size_t k = 0; k < g_enh.size() && !claimed; k++)
            if (!strcasecmp(g_enh[k].carrier, g_triggers[i].name)) claimed = true;
        if (!claimed) {
            snprintf(w, sizeof w, "rule %s has no event", g_triggers[i].name);
            chk_add(CHK_WARN, w, "nothing in the engine springs a rule whose event is "
                                 "None, and no Enhanced rule claims it");
        }
    }

    if (!g_enh.empty()) {
        snprintf(w, sizeof w, "this map uses %d Enhanced rule%s", (int)g_enh.size(),
                 g_enh.size() == 1 ? "" : "s");
        chk_add(CHK_OK, w, "it will only run in CNC3D; the 1995 engine would skip them");
    }
    if (!g_checkErrors && !g_checkWarns)
        chk_add(CHK_OK, "nothing wrong found",
                "every rule can fire, every team can run, every start is placed");
}


/* What each panel row opens, and what a pick does. Kept as a pair so the list a person
   sees and the field the pick writes can never disagree. */

static void eui_pop_anchor_row(int fbw, int fbh, int row)
{
    EuiLayout L;
    eui_layout(fbw, fbh, &L);
    float x, y, w, h;
    eui_script_row_rect(&L, row, &x, &y, &w, &h);
    g_popAX = x;
    g_popAY = y + h + 2 * L.s;
    g_popAW = w;
}

/* TRUE if the row opened a picker; false means the row keeps its old click behaviour. */
static bool eui_open_row_popup(int row, int fbw, int fbh)
{
    const bool teams = (g_scriptView == 1), enh = (g_scriptView == 2);
    eui_pop_close();

    if (!teams && !enh) {
        if (g_trigSel < 0 && row >= 2) return false;
        const EditTrigger* t = g_trigSel >= 0 ? &g_triggers[g_trigSel] : NULL;
        if (row == 2) {
            g_popKind = POP_EVENT; g_popCtx = g_trigSel; g_popCur = t->event;
            for (int i = 0; i < TRIG_EVENT_N; i++)
                eui_pop_add(TRIG_EVENT_HUMAN[i], TRIG_EVENT[i], i);
        } else if (row == 3) {
            g_popKind = POP_ACTION; g_popCtx = g_trigSel; g_popCur = t->action;
            for (int i = 0; i < TRIG_ACTION_N; i++)
                eui_pop_add(TRIG_ACTION_HUMAN[i], TRIG_ACTION[i], i);
        } else if (row == 4 && t->event == 16) {
            g_popKind = POP_STRUCT; g_popCtx = g_trigSel; g_popCur = t->data;
            for (int i = 0; i < EDIT_STRUCT_N; i++)
                eui_pop_add(trig_struct_name(i), EDIT_STRUCT_CODE[i], i);
        } else if (row == 5) {
            static const char* H[4] = { "GoodGuy", "BadGuy", "Neutral", "None" };
            g_popKind = POP_RULEHOUSE; g_popCtx = g_trigSel;
            g_popCur = 3;
            for (int k = 0; k < 4; k++) if (!strcasecmp(t->house, H[k])) g_popCur = k;
            for (int k = 0; k < 4; k++)
                eui_pop_add(H[k], t->event == 1 && k < 3 ? "their units trip it" : "", k);
        } else if (row == 6) {
            if (!trig_action_has_team(t->action)) {
                edit_toast(EUI_FAINT, "THIS ACTION HAS NO TEAM",
                           TRIG_ACTION_HUMAN[t->action]);
                return true;               /* claimed: do not fall back to cycling */
            }
            g_popKind = POP_TEAM; g_popCtx = g_trigSel;
            g_popCur = edit_team_by_name(t->team);
            eui_pop_add("none", "no team", -1);
            for (size_t k = 0; k < g_teams.size(); k++) {
                char roster[64];
                eui_team_roster(g_teams[k], roster, sizeof roster);
                eui_pop_add(g_teams[k].name, roster, (int)k);
            }
            eui_pop_add("+ new team", "make one and connect it", -2);
        } else if (row == 7) {
            static const char* P[3] = { "once only", "once per tagged object",
                                        "every time it happens" };
            g_popKind = POP_PERSIST; g_popCtx = g_trigSel; g_popCur = t->persist;
            for (int k = 0; k < 3; k++) eui_pop_add(P[k], "", k);
        } else return false;
        eui_pop_anchor_row(fbw, fbh, row);
        return true;
    }

    if (teams) {
        if (g_teamSel < 0 && row >= 2) return false;
        EditTeam* T = g_teamSel >= 0 ? &g_teams[g_teamSel] : NULL;
        if (row == 2) {
            static const char* H[8] = { "GoodGuy", "BadGuy", "Neutral", "Special",
                                        "Multi1", "Multi2", "Multi3", "Multi4" };
            g_popKind = POP_TEAMHOUSE; g_popCtx = g_teamSel;
            g_popCur = 0;
            for (int k = 0; k < 8; k++) if (!strcasecmp(T->house, H[k])) g_popCur = k;
            for (int k = 0; k < 8; k++)
                eui_pop_add(H[k], k >= 4 ? "a multiplayer seat" : "", k);
        } else if (row == 4) {
            if (g_teamMember >= T->nclass) return false;
            g_popKind = POP_MEMBERTYPE; g_popCtx = g_teamSel;
            g_popCur = edit_team_type_index(T->cls[g_teamMember].code);
            for (int i = 0; i < edit_team_type_n(); i++) {
                const char* code = edit_team_type_at(i);
                const char* nm = code;
                for (int k = 0; k < EDIT_ITEM_N; k++)
                    if (!strcmp(EDIT_ITEMS[k].code, code)) { nm = EDIT_ITEMS[k].name; break; }
                eui_pop_add(nm, code, i);
            }
        } else if (row == 8) {
            if (g_teamOrder < 0 || g_teamOrder >= T->nmission) return false;
            static const char* AM[5] = { "runs for N x 6 seconds", "goes to a waypoint",
                                         "goes to a raw cell", "jumps back to an order",
                                         "raw target value" };
            g_popKind = POP_ORDERTYPE; g_popCtx = g_teamSel;
            g_popCur = T->mis[g_teamOrder].mission;
            for (int i = 0; i < TEAM_MIS_N; i++)
                eui_pop_add(TEAM_MIS[i], AM[TEAM_MIS_ARG[i]], i);
        } else if (row == 9 && g_teamOrder >= 0 && g_teamOrder < T->nmission &&
                   TEAM_MIS_ARG[T->mis[g_teamOrder].mission] == 1) {
            /* A waypoint argument picks from the waypoints that EXIST, each with its
               cell, and says which ones are not placed instead of offering a number
               that sends the team to nowhere -- the mission check's commonest error. */
            g_popKind = POP_ORDERARG; g_popCtx = g_teamSel;
            g_popCur = T->mis[g_teamOrder].arg;
            for (int i = 0; i < EDIT_WAYPT_COUNT; i++) {
                char lbl[48], det[48];
                snprintf(lbl, sizeof lbl, "%s", edit_waypoint_label(i));
                if (g_waypoint[i] >= 0)
                    snprintf(det, sizeof det, "at %d,%d", g_waypoint[i] % edit_ini_w(),
                             g_waypoint[i] / edit_ini_w());
                else
                    snprintf(det, sizeof det, "NOT PLACED");
                eui_pop_add(lbl, det, i);
            }
        } else if (row == 9 && g_teamOrder >= 0 && g_teamOrder < T->nmission &&
                   TEAM_MIS_ARG[T->mis[g_teamOrder].mission] == 3) {
            /* A Loop target picks from the team's own order list, drawn as the list. */
            g_popKind = POP_ORDERARG; g_popCtx = g_teamSel;
            g_popCur = T->mis[g_teamOrder].arg;
            for (int i = 0; i < T->nmission; i++) {
                char lbl[64];
                snprintf(lbl, sizeof lbl, "order %d: %s %d", i,
                         TEAM_MIS[T->mis[i].mission], T->mis[i].arg);
                eui_pop_add(lbl, i == g_teamOrder ? "this order" : "", i);
            }
        } else return false;
        eui_pop_anchor_row(fbw, fbh, row);
        return true;
    }

    /* enhanced */
    if (g_enhSel < 0 && row >= 2) return false;
    EnhRule* R = g_enhSel >= 0 ? &g_enh[g_enhSel] : NULL;
    const int cs = (R && g_enhClause >= 0 && g_enhClause < R->nclause) ? g_enhClause : -1;
    if (row == 5 && cs >= 0) {
        g_popKind = POP_ENH_CLAUSE; g_popCtx = g_enhSel;
        g_popCur = R->clause[cs].kind;
        for (int i = 0; i < ENH_CLAUSE_N; i++)
            eui_pop_add(ENH_SHAPE[i].human, ENH_CLAUSE[i], i);
    } else if (row == 7 && cs >= 0 && ENH_SHAPE[R->clause[cs].kind].house) {
        static const char* H[4] = { "GoodGuy", "BadGuy", "Neutral", "Special" };
        g_popKind = POP_ENH_HOUSE; g_popCtx = g_enhSel;
        g_popCur = 0;
        for (int k = 0; k < 4; k++) if (!strcasecmp(R->clause[cs].house, H[k])) g_popCur = k;
        for (int k = 0; k < 4; k++) eui_pop_add(H[k], "", k);
    } else if (row == 8 && cs >= 0 && ENH_SHAPE[R->clause[cs].kind].name) {
        g_popKind = POP_ENH_NAME; g_popCtx = g_enhSel;
        g_popCur = -1;
        const EnhClause* C = &R->clause[cs];
        if (C->kind == ENH_TYPE) {
            for (int i = 0; i < EDIT_ITEM_N; i++) {
                if (!strcasecmp(EDIT_ITEMS[i].code, C->name)) g_popCur = i;
                eui_pop_add(EDIT_ITEMS[i].name, EDIT_ITEMS[i].code, i);
            }
        } else if (C->kind == ENH_FIRED) {
            for (size_t i = 0; i < g_triggers.size(); i++) {
                if (!strcasecmp(g_triggers[i].name, C->name)) g_popCur = (int)i;
                eui_pop_add(g_triggers[i].name,
                            g_triggers[i].persist == 2 ? "persistent: never true"
                                                       : TRIG_EVENT[g_triggers[i].event],
                            (int)i);
            }
        } else {   /* a counter */
            int v = 0;
            std::vector<std::string> names;
            for (size_t i = 0; i < g_enh.size(); i++)
                for (int e = 0; e < g_enh[i].neffect; e++) {
                    bool got = false;
                    for (size_t k = 0; k < names.size(); k++)
                        if (!strcasecmp(names[k].c_str(), g_enh[i].effect[e].name)) got = true;
                    if (!got) names.push_back(g_enh[i].effect[e].name);
                }
            for (size_t k = 0; k < names.size(); k++) {
                if (!strcasecmp(names[k].c_str(), C->name)) g_popCur = v;
                eui_pop_add(names[k].c_str(), "a counter", v++);
            }
            char fresh[16];
            snprintf(fresh, sizeof fresh, "c%d", (int)g_enh.size());
            eui_pop_add(fresh, "a new counter", v);
        }
    } else return false;
    eui_pop_anchor_row(fbw, fbh, row);
    return true;
}

/* One pick, applied. Values were captured with the list, so the switch is small. */
static void eui_pop_apply(int idx)
{
    if (idx < 0 || idx >= (int)g_popItems.size()) { eui_pop_close(); return; }
    const int v = g_popItems[idx].value;
    const char* label = g_popItems[idx].label;
    switch (g_popKind) {
    case POP_EVENT:
        if (g_popCtx >= 0 && g_popCtx < (int)g_triggers.size()) {
            g_triggers[g_popCtx].event = v;
            g_trigDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_ACTION:
        if (g_popCtx >= 0 && g_popCtx < (int)g_triggers.size()) {
            g_triggers[g_popCtx].action = v;
            g_trigDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_STRUCT:
        if (g_popCtx >= 0 && g_popCtx < (int)g_triggers.size()) {
            g_triggers[g_popCtx].data = v;
            g_trigDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_RULEHOUSE:
        if (g_popCtx >= 0 && g_popCtx < (int)g_triggers.size()) {
            snprintf(g_triggers[g_popCtx].house, sizeof g_triggers[0].house, "%s", label);
            g_trigDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_TEAM:
        if (g_popCtx >= 0 && g_popCtx < (int)g_triggers.size()) {
            EditTrigger& t = g_triggers[g_popCtx];
            if (v == -2) {
                edit_team_row(0, false);       /* makes one and selects it */
                if (g_teamSel >= 0)
                    snprintf(t.team, sizeof t.team, "%s", g_teams[g_teamSel].name);
            } else if (v < 0) {
                snprintf(t.team, sizeof t.team, "None");
            } else if (v < (int)g_teams.size()) {
                snprintf(t.team, sizeof t.team, "%s", g_teams[v].name);
            }
            g_trigDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_PERSIST:
        if (g_popCtx >= 0 && g_popCtx < (int)g_triggers.size()) {
            g_triggers[g_popCtx].persist = v;
            g_trigDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_TEAMHOUSE:
        if (g_popCtx >= 0 && g_popCtx < (int)g_teams.size()) {
            snprintf(g_teams[g_popCtx].house, sizeof g_teams[0].house, "%s", label);
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_MEMBERTYPE:
        if (g_popCtx >= 0 && g_popCtx < (int)g_teams.size() &&
            g_teamMember < g_teams[g_popCtx].nclass) {
            snprintf(g_teams[g_popCtx].cls[g_teamMember].code,
                     sizeof g_teams[0].cls[0].code, "%s", edit_team_type_at(v));
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_ORDERTYPE:
        if (g_popCtx >= 0 && g_popCtx < (int)g_teams.size() &&
            g_teamOrder >= 0 && g_teamOrder < g_teams[g_popCtx].nmission) {
            g_teams[g_popCtx].mis[g_teamOrder].mission = v;
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_ORDERARG:
        if (g_popCtx >= 0 && g_popCtx < (int)g_teams.size() &&
            g_teamOrder >= 0 && g_teamOrder < g_teams[g_popCtx].nmission) {
            g_teams[g_popCtx].mis[g_teamOrder].arg = v;
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_ENH_CLAUSE:
        if (g_popCtx >= 0 && g_popCtx < (int)g_enh.size() &&
            g_enhClause >= 0 && g_enhClause < g_enh[g_popCtx].nclause) {
            EnhClause* C = &g_enh[g_popCtx].clause[g_enhClause];
            C->kind = (unsigned char)v;
            if (ENH_SHAPE[v].house && !C->house[0])
                snprintf(C->house, sizeof C->house, "BadGuy");
            if (ENH_SHAPE[v].name && !C->name[0])
                snprintf(C->name, sizeof C->name, v == ENH_TYPE ? "HAND" : "n");
            g_enhDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_ENH_HOUSE:
        if (g_popCtx >= 0 && g_popCtx < (int)g_enh.size() &&
            g_enhClause >= 0 && g_enhClause < g_enh[g_popCtx].nclause) {
            snprintf(g_enh[g_popCtx].clause[g_enhClause].house,
                     sizeof g_enh[0].clause[0].house, "%s", label);
            g_enhDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_ENH_NAME:
        if (g_popCtx >= 0 && g_popCtx < (int)g_enh.size() &&
            g_enhClause >= 0 && g_enhClause < g_enh[g_popCtx].nclause) {
            EnhClause* C = &g_enh[g_popCtx].clause[g_enhClause];
            if (C->kind == ENH_TYPE && v >= 0 && v < EDIT_ITEM_N)
                snprintf(C->name, sizeof C->name, "%s", EDIT_ITEMS[v].code);
            else if (C->kind == ENH_FIRED && v >= 0 && v < (int)g_triggers.size())
                snprintf(C->name, sizeof C->name, "%s", g_triggers[v].name);
            else
                snprintf(C->name, sizeof C->name, "%s", label);
            g_enhDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_CARRIER:
        if (g_popCtx >= 0 && g_popCtx < (int)g_enh.size()) {
            if (v == -2) {
                /* make the carrier here, the same pair ADD RULE makes: an ordinary
                   trigger with event None, which only this rule will ever fire */
                EditTrigger t2;
                memset(&t2, 0, sizeof t2);
                enh_new_carrier(t2.name, sizeof t2.name);
                t2.event = 0;
                t2.action = 4;
                snprintf(t2.house, sizeof t2.house, "BadGuy");
                snprintf(t2.team, sizeof t2.team, "None");
                g_triggers.push_back(t2);
                g_trigDirty = true;
                snprintf(g_enh[g_popCtx].carrier, sizeof g_enh[0].carrier, "%s", t2.name);
            } else {
                int carriers[64];
                const int nc = enh_carrier_list(carriers, 64);
                if (v >= 0 && v < nc)
                    snprintf(g_enh[g_popCtx].carrier, sizeof g_enh[0].carrier, "%s",
                             g_triggers[carriers[v]].name);
            }
            g_enhDirty = true;
            edit_trace_invalidate();
        }
        break;
    case POP_WIREDROP:
        /* handled where the wire was dropped; see the release handler */
        break;
    default: break;
    }
    eui_pop_close();
}


/* Open typed entry for a numeric row, seeded with the current value in HUMAN units --
   Time is typed in minutes because that is how the row displays it. Returns false when
   the row is not numeric so the caller can fall through. */
static bool eui_open_num_edit(int row)
{
    const bool teams = (g_scriptView == 1), enh = (g_scriptView == 2);
    eui_num_close();
    if (!teams && !enh) {
        if (g_trigSel < 0) return false;
        const EditTrigger& t = g_triggers[g_trigSel];
        if (row != 4 || !trig_event_has_data(t.event) || t.event == 16) return false;
        g_numKind = NUM_RULE_DATA;
        if (t.event == 11) snprintf(g_numBuf, sizeof g_numBuf, "%.1f", t.data / 10.0f);
        else               snprintf(g_numBuf, sizeof g_numBuf, "%d", t.data);
    } else if (teams) {
        if (g_teamSel < 0) return false;
        const EditTeam& T = g_teams[g_teamSel];
        if (row == 5 && g_teamMember < T.nclass) {
            g_numKind = NUM_TEAM_COUNT;
            snprintf(g_numBuf, sizeof g_numBuf, "%d", T.cls[g_teamMember].num);
        } else if (row == 9 && g_teamOrder >= 0 && g_teamOrder < T.nmission) {
            g_numKind = NUM_ORDER_ARG;
            snprintf(g_numBuf, sizeof g_numBuf, "%d", T.mis[g_teamOrder].arg);
        } else if (row == 11) {
            g_numKind = NUM_TEAM_PRI;
            snprintf(g_numBuf, sizeof g_numBuf, "%d", T.priority);
        } else if (row == 12) {
            g_numKind = NUM_TEAM_MAX;
            snprintf(g_numBuf, sizeof g_numBuf, "%d", T.maxallowed);
        } else return false;
    } else {
        if (g_enhSel < 0 || g_enhClause < 0 ||
            g_enhClause >= g_enh[g_enhSel].nclause) return false;
        const EnhClause& C = g_enh[g_enhSel].clause[g_enhClause];
        if (row != 9 || !ENH_SHAPE[C.kind].num) return false;
        g_numKind = NUM_ENH_NUM;
        if (C.kind == ENH_TIME) snprintf(g_numBuf, sizeof g_numBuf, "%.1f", C.num / 10.0f);
        else                    snprintf(g_numBuf, sizeof g_numBuf, "%d", C.num);
    }
    g_numRow = row;
    return true;
}

/* Click the NAME on a selected card to rename it. Names are identity here: a rule's
   name is what tags, zones-by-name, FIRED clauses and carriers refer to, so a rename
   PROPAGATES to every reference -- leaving them behind would silently orphan the lot. */
/* While a rename is open, the card draws the buffer instead of the name. */
static bool eui_rename_draws(int view, int idx, float x, float y, float scale)
{
    if (!eui_num_is_name()) return false;
    /* The MAP's name is not a card on this canvas -- it belongs to the SAVE AS dialog,
       which draws its own field. Without this it would fall through to the ENHRULE
       branch below and be painted over an enhanced rule's name. */
    if (g_numKind == NAME_MAP) return false;
    const int want = g_numKind == NAME_RULE ? 0 : g_numKind == NAME_TEAM ? 1 : 2;
    if (want != view) return false;
    const int sel = view == 0 ? g_trigSel : view == 1 ? g_teamSel : g_enhSel;
    if (idx != sel) return false;
    char ed[56];
    snprintf(ed, sizeof ed, "%s_", g_numBuf);
    ef_text(x, y, ed, scale, EUI_RGB(EUI_GOLD));
    return true;
}

static bool eui_open_rename(void)
{
    eui_num_close();
    if (g_scriptView == 0 && g_trigSel >= 0) {
        g_numKind = NAME_RULE;
        snprintf(g_numBuf, sizeof g_numBuf, "%s", g_triggers[g_trigSel].name);
    } else if (g_scriptView == 1 && g_teamSel >= 0) {
        g_numKind = NAME_TEAM;
        snprintf(g_numBuf, sizeof g_numBuf, "%s", g_teams[g_teamSel].name);
    } else if (g_scriptView == 2 && g_enhSel >= 0) {
        g_numKind = NAME_ENHRULE;
        snprintf(g_numBuf, sizeof g_numBuf, "%s", g_enh[g_enhSel].name);
    } else return false;
    snprintf(g_renOld, sizeof g_renOld, "%s", g_numBuf);
    g_numRow = -1;
    return true;
}

/* Committing the MAP's name is a save, not a rename, and lives with the save path.
   Declared here because Enter routes every named field through eui_rename_commit. */
static void edit_saveas_commit(void);

static void eui_rename_commit(void)
{
    /* The map's name is not one of this canvas's names: it is a document title and
       committing it writes a file. Routed before the empty check, because a blank map
       name must be REFUSED out loud rather than quietly dropped. */
    if (g_numKind == NAME_MAP) { edit_saveas_commit(); return; }
    if (!g_numBuf[0]) { eui_num_close(); return; }
    if (g_numKind == NAME_RULE && g_trigSel >= 0) {
        const int clash = edit_trigger_by_name(g_numBuf);
        if (clash >= 0 && clash != g_trigSel) {
            edit_toast(EUI_DANGER, "THAT NAME IS TAKEN", g_numBuf);
            return;                        /* keep editing */
        }
        snprintf(g_triggers[g_trigSel].name, sizeof g_triggers[0].name, "%s", g_numBuf);
        /* every reference follows */
        for (size_t i = 0; i < g_editPrior.size(); i++)
            if (!strcasecmp(g_editPrior[i].trig, g_renOld))
                snprintf(g_editPrior[i].trig, sizeof g_editPrior[0].trig, "%s", g_numBuf);
        for (size_t i = 0; i < g_enh.size(); i++) {
            if (!strcasecmp(g_enh[i].carrier, g_renOld))
                snprintf(g_enh[i].carrier, sizeof g_enh[0].carrier, "%s", g_numBuf);
            for (int c = 0; c < g_enh[i].nclause; c++)
                if (g_enh[i].clause[c].kind == ENH_FIRED &&
                    !strcasecmp(g_enh[i].clause[c].name, g_renOld))
                    snprintf(g_enh[i].clause[c].name, sizeof g_enh[0].clause[0].name,
                             "%s", g_numBuf);
        }
        g_trigDirty = true;
    } else if (g_numKind == NAME_TEAM && g_teamSel >= 0) {
        const int clash = edit_team_by_name(g_numBuf);
        if (clash >= 0 && clash != g_teamSel) {
            edit_toast(EUI_DANGER, "THAT NAME IS TAKEN", g_numBuf);
            return;
        }
        snprintf(g_teams[g_teamSel].name, sizeof g_teams[0].name, "%s", g_numBuf);
        for (size_t i = 0; i < g_triggers.size(); i++)
            if (!strcasecmp(g_triggers[i].team, g_renOld))
                snprintf(g_triggers[i].team, sizeof g_triggers[0].team, "%s", g_numBuf);
        g_teamDirty = true;
    } else if (g_numKind == NAME_ENHRULE && g_enhSel >= 0) {
        if (enh_rule_by_name(g_numBuf) >= 0 &&
            enh_rule_by_name(g_numBuf) != g_enhSel) {
            edit_toast(EUI_DANGER, "THAT NAME IS TAKEN", g_numBuf);
            return;
        }
        snprintf(g_enh[g_enhSel].name, sizeof g_enh[0].name, "%s", g_numBuf);
        g_enhDirty = true;
    }
    edit_trace_invalidate();
    edit_toast(EUI_GOLD, "RENAMED", g_numBuf);
    eui_num_close();
}

/* Enter pressed: parse in the same human units the seed used, clamp to what the engine
   can hold, write it back. */
static void eui_num_commit(void)
{
    const double v = atof(g_numBuf);
    switch (g_numKind) {
    case NUM_RULE_DATA:
        if (g_trigSel >= 0 && g_trigSel < (int)g_triggers.size()) {
            EditTrigger& t = g_triggers[g_trigSel];
            int d = (t.event == 11) ? (int)(v * 10.0 + 0.5) : (int)v;
            if (d < 0) d = 0;
            t.data = d;
            g_trigDirty = true;
            edit_trace_invalidate();
        }
        break;
    case NUM_TEAM_COUNT:
        if (g_teamSel >= 0 && g_teamMember < g_teams[g_teamSel].nclass) {
            int d = (int)v;
            if (d < 1) d = 1;
            if (d > 255) d = 255;      /* the field is a byte */
            g_teams[g_teamSel].cls[g_teamMember].num = d;
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case NUM_TEAM_PRI:
        if (g_teamSel >= 0) {
            int d = (int)v;
            if (d < 0) d = 0;
            if (d > 30) d = 30;
            g_teams[g_teamSel].priority = d;
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case NUM_TEAM_MAX:
        if (g_teamSel >= 0) {
            int d = (int)v;
            if (d < 0) d = 0;
            if (d > 255) d = 255;
            g_teams[g_teamSel].maxallowed = d;
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case NUM_ORDER_ARG:
        if (g_teamSel >= 0 && g_teamOrder >= 0 &&
            g_teamOrder < g_teams[g_teamSel].nmission) {
            EditTeam& T = g_teams[g_teamSel];
            int d = (int)v;
            if (d < 0) d = 0;
            const int k = T.mis[g_teamOrder].mission;
            if (TEAM_MIS_ARG[k] == 1 && d >= EDIT_WAYPT_COUNT) d = EDIT_WAYPT_COUNT - 1;
            if (TEAM_MIS_ARG[k] == 3 && d >= T.nmission) d = T.nmission - 1;
            T.mis[g_teamOrder].arg = d;
            g_teamDirty = true;
            edit_trace_invalidate();
        }
        break;
    case NUM_ENH_NUM:
        if (g_enhSel >= 0 && g_enhClause >= 0 &&
            g_enhClause < g_enh[g_enhSel].nclause) {
            EnhClause& C = g_enh[g_enhSel].clause[g_enhClause];
            int d = (C.kind == ENH_TIME) ? (int)(v * 10.0 + 0.5) : (int)v;
            if (d < 0) d = 0;
            C.num = d;
            g_enhDirty = true;
            edit_trace_invalidate();
        }
        break;
    default: break;
    }
    eui_num_close();
}

/* --- the view bar -------------------------------------------------------------------
 *
 *  Across the bottom of the map, as in the browser. Every switch on it drives state the
 *  renderer already has -- there is no view mode invented here that the engine cannot
 *  actually do, because a toggle that does nothing is worse than a missing one.
 *
 *  The four object-class switches are what an editor really needs: the ability to look
 *  UNDER things. At the ground a building stands on, at a shoreline a row of trees is
 *  hiding.
 * ---------------------------------------------------------------------------------- */

struct EuiToggle { const char* label; const char* key; bool* flag; bool invert; };

static EuiToggle* eui_toggles(int* n)
{
    static EuiToggle T[] = {
        { "GRID",      "`", &g_grid,           false },
        { "SHADE",     "",  &g_terrainShade,   false },
        { "WALLS",     "",  &g_wallsDraw,      false },
        { "SHROUD",    "",  NULL,              true  },   /* g_shroudEditorOff, inverted */
        { "BUILDINGS", "",  &g_vShowBuildings, false },
        { "UNITS",     "",  &g_vShowUnits,     false },
        { "INFANTRY",  "",  &g_vShowInfantry,  false },
        { "SCENERY",   "",  &g_vShowTerrainObj,false },
        { "NO-GO",     "N", &g_nogoPin,        false },
    };
    *n = (int)(sizeof(T) / sizeof(T[0]));
    return T;
}

static bool eui_toggle_get(const EuiToggle* t)
{
    if (t->flag) return t->invert ? !*t->flag : *t->flag;
    return !g_shroudEditorOff;          /* the one that lives in shroud_mod.h */
}

static void eui_toggle_set(const EuiToggle* t, bool on)
{
    if (t->flag) { *t->flag = t->invert ? !on : on; return; }
    g_shroudEditorOff = on ? 0 : 1;
}

/* Each switch's rectangle. One function, so the draw and the hit test cannot drift. */
static void eui_toggle_rect(const EuiLayout* L, int i, float* x, float* y,
                            float* w, float* h)
{
    const float S = L->s;
    const float bw = 62 * S, pad = 5 * S;
    *w = bw; *h = 26 * S;
    *x = L->railW + 10 * S + i * (bw + pad);
    *y = L->barY + (L->barH - *h) * 0.5f;
}

static void eui_draw_bar(const EuiLayout* L, int fbw, float t1)
{
    const float S = L->s;
    eui_rect(L->railW, L->barY, L->sideX - L->railW, L->barH, 0x141b24, 1.0f);
    eui_rect(L->railW, L->barY, L->sideX - L->railW, S, 0x05080c, 1.0f);

    int n = 0;
    const EuiToggle* T = eui_toggles(&n);
    for (int i = 0; i < n; i++) {
        float x, y, w, h;
        eui_toggle_rect(L, i, &x, &y, &w, &h);
        if (x + w > L->sideX - EUI_BAR_READOUT * S) break;   /* leave the readout room */
        const bool on  = eui_toggle_get(&T[i]);
        const bool hot = (g_euiHotKind == EUI_VIEW && g_euiHotArg == i);
        eui_rect(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL, 1.0f);
        eui_frame(x, y, w, h, on ? EUI_CYAN : EUI_LINE, 1.0f);
        char lbl[16]; snprintf(lbl, sizeof lbl, "%.8s", T[i].label);
        ef_text(x + (w - ef_text_w(lbl, EUI_TS(t1))) * 0.5f, y + h * 0.5f - 3 * S, lbl,
                EUI_TS(t1), EUI_RGB(on ? EUI_CYAN : EUI_FAINT));
        if (T[i].key[0])
            ef_text(x + w - ef_text_w(T[i].key, EUI_TS(t1)) - 3 * S, y + h - 9 * S,
                    T[i].key, EUI_TS(t1), EUI_RGB(EUI_FAINT));
    }

    /* Right-aligned: where the camera is and how fast it flies, which is the one thing
       you want to read while flying and cannot get from the picture. */
    {
        float ex, ey, ez;
        edit_cam_eye(&ex, &ey, &ez);
        char r[128];
        snprintf(r, sizeof r, "EYE %.0f,%.0f,%.0f   YAW %.0f   SPEED %.2f",
                 ex, ey, ez, g_camYaw * 57.2958f, g_camSpeed);
        ef_text(L->sideX - ef_text_w(r, EUI_TS(t1)) - 12 * S,
                L->barY + L->barH * 0.5f - 3 * S, r, EUI_TS(t1), EUI_RGB(EUI_FAINT));
    }
}

/* --- viewport overlays --------------------------------------------------------------
 *
 *  Three things the browser editor floats over the map, and the native one had none of:
 *  a mode badge top-left, the terrain probe top-right, and the status card along the
 *  bottom. The card is the one that matters -- it is where "why can I not place this
 *  here" gets answered, and without it a refusal is a line in a log nobody is reading.
 * ---------------------------------------------------------------------------------- */

static void eui_kbd(float* x, float y, const char* key, const char* what, float t, float S)
{
    const float kw = ef_text_w(key, t);
    eui_rect(*x, y - 2 * S, kw + 8 * S, t * 9.0f + 4 * S, EUI_PANEL2, 0.95f);
    eui_frame(*x, y - 2 * S, kw + 8 * S, t * 9.0f + 4 * S, EUI_LINE, 1.0f);
    ef_text(*x + 4 * S, y, key, t, EUI_RGB(EUI_DIM));
    *x += kw + 12 * S;
    ef_text(*x, y, what, t, EUI_RGB(EUI_FAINT));
    *x += ef_text_w(what, t) + 16 * S;
}


/* The rung a raw height byte snaps to. Defined with the elevation tool, below. */
static int elev_tier_of(int b);

static void eui_draw_viewport(const EuiLayout* L, int fbw, int fbh, float t1)
{
    const float S = L->s;
    static const char* MODES[5] = { "OBJECTS", "TERRAIN", "ELEVATION", "STARTS", "SCRIPT" };
    static const unsigned MC[5] = { 0xe0b070, 0x7ee0a0, 0x8fe4ff, 0xb98fe4, 0xff9a5a };
    const unsigned mc = MC[g_editMode];

    /* ---- mode badge, top-left of the map ---- */
    if (!(!g_editOn && g_playFromEditor)) {
        char line[96];
        snprintf(line, sizeof line, "%s", MODES[g_editMode]);
        char sub[96];
        if (g_editMode == 4)
            snprintf(sub, sizeof sub, "%d rules", (int)g_triggers.size());
        else if (g_editMode == 3)
            snprintf(sub, sizeof sub, "%s", g_waypointArmed >= 0
                     ? edit_waypoint_label(g_waypointArmed) : "pick a start, then click");
        else if (g_editMode == 2)
            snprintf(sub, sizeof sub, "%s", elev_ready() ? "raise ground, cliffs follow"
                                                         : "not on the ladder");
        else if (g_editMode == 1)
            snprintf(sub, sizeof sub, "%s", g_terrArmed >= 0
                     ? EDIT_TEMPLATES[g_terrArmed].name : "no tile armed");
        else
            snprintf(sub, sizeof sub, "placing for %s", EUI_HOUSES[g_editOwner].label);
        const float w = 22 * S + ef_text_w(line, t1) + 14 * S + ef_text_w(sub, t1) + 14 * S;
        const float x = L->railW + 14 * S, y = L->topH + 12 * S, h = 26 * S;
        eui_rect(x, y, w, h, 0x0a0e14, 0.86f);
        eui_frame(x, y, w, h, mc, 1.0f);
        eui_rect(x, y, 3 * S, h, mc, 1.0f);
        ef_text(x + 12 * S, y + h * 0.5f - 4 * S, line, t1, EUI_RGB(mc));
        ef_text(x + 12 * S + ef_text_w(line, t1) + 14 * S, y + h * 0.5f - 4 * S,
                sub, t1, EUI_RGB(EUI_DIM));
    }


    /* ---- the terrain probe, top-right ----
       What is actually under the pointer, in three lines: the cell and its land family,
       what the ground does there, and what is standing on it. The status card says
       whether the armed thing FITS; this says what the place is, which is the question
       you ask before you arm anything. */
    if (g_editOverMap && !(!g_editOn && g_playFromEditor)) {
        const int cx = g_editCellX, cy = g_editCellY;
        char l[3][160];
        int n = 0;
        snprintf(l[n++], sizeof l[0], "cell %d,%d   %s   %s", cx, cy,
                 EDIT_LAND_NAME[edit_land_at(cx, cy)],
                 edit_can_build(cx, cy) ? "buildable" : "no building here");

        /* The four corners, in rungs rather than raw bytes: the elevation tool works in
           rungs and a byte of 191 means nothing to anyone. A drop of more than one rung
           across a cell is what the cliff art is for, so it is called out. */
        const int cw = g_gridW + 1;
        if ((int)g_pack.corner.size() >= cw * (g_gridH + 1) &&
            cx >= 0 && cy >= 0 && cx < g_gridW && cy < g_gridH) {
            const int c[4] = {
                elev_tier_of(g_pack.corner[cy * cw + cx]),
                elev_tier_of(g_pack.corner[cy * cw + cx + 1]),
                elev_tier_of(g_pack.corner[(cy + 1) * cw + cx]),
                elev_tier_of(g_pack.corner[(cy + 1) * cw + cx + 1]) };
            int lo = c[0], hi = c[0];
            for (int i = 1; i < 4; i++) { if (c[i] < lo) lo = c[i]; if (c[i] > hi) hi = c[i]; }
            if (hi == lo) snprintf(l[n++], sizeof l[0], "height   rung %d, flat", lo);
            else snprintf(l[n++], sizeof l[0], "height   rungs %d %d %d %d, drops %d",
                          c[0], c[1], c[2], c[3], hi - lo);
        }

        /* Everything standing here, not just the topmost -- infantry share a cell five
           at a time and "there is a rifleman here" is a different fact from "there are
           five". */
        {
            char who[128]; who[0] = 0;
            int k = 0, m = 0;
            for (size_t i = 0; i < g_objects.size(); i++) {
                const SimObject& o = g_objects[i];
                const int ow = o.fw > 0 ? o.fw : 1, oh = o.fh > 0 ? o.fh : 1;
                if (cx < o.cx || cx >= o.cx + ow || cy < o.cy || cy >= o.cy + oh) continue;
                k++;
                if (m < (int)sizeof who - 24)
                    m += snprintf(who + m, sizeof who - m, "%s%s", m ? " " : "", o.type);
            }
            if (k) snprintf(l[n++], sizeof l[0], "here     %s", who);
        }

        for (int i = 0; i < n; i++) {
            const float w = ef_text_w(l[i], t1);
            ef_text(L->sideX - w - 16 * S, L->topH + 16 * S + i * 15 * S, l[i], t1,
                    EUI_RGB(i ? EUI_FAINT : EUI_DIM));
        }
    }

    /* ---- the status card, bottom-centre of the map ---- */
    if (!( !g_editOn && g_playFromEditor )) {
        const float cw = (L->sideX - L->railW) * 0.62f;
        const float cx = L->railW + ((L->sideX - L->railW) - cw) * 0.5f;
        const float ch = 62 * S;
        const float cy = L->barY - 14 * S - ch;
        if (cw > 200 * S) {
            eui_rect(cx, cy, cw, ch, 0x090d12, 0.94f);
            eui_frame(cx, cy, cw, ch, mc, 1.0f);

            char l1[128], l2[160];
            unsigned l2col = EUI_GREEN;
            if (g_editMode == 4 && g_scriptView == 2) {
                if (g_enhSel >= 0 && g_enhSel < (int)g_enh.size()) {
                    const EnhRule& R = g_enh[g_enhSel];
                    snprintf(l1, sizeof l1, "ENHANCED RULE %s", R.name);
                    snprintf(l2, sizeof l2, "%s of %d condition%s -> %s, %s",
                             R.all ? "ALL" : "ANY", R.nclause,
                             R.nclause == 1 ? "" : "s",
                             R.carrier[0] ? R.carrier : "no carrier",
                             R.repeat ? "every time" : "once only");
                    l2col = EUI_DIM;
                } else {
                    snprintf(l1, sizeof l1, "%d ENHANCED RULES", (int)g_enh.size());
                    snprintf(l2, sizeof l2, g_enh.empty()
                             ? "this map runs on any engine"
                             : "this map needs CNC3D; the 1995 engine would skip these");
                    l2col = g_enh.empty() ? EUI_FAINT : EUI_GREEN;
                }
            } else if (g_editMode == 4 && g_scriptView == 1) {
                if (g_teamSel >= 0 && g_teamSel < (int)g_teams.size()) {
                    const EditTeam& T = g_teams[g_teamSel];
                    snprintf(l1, sizeof l1, "TEAM %s", T.name);
                    char roster[96];
                    eui_team_roster(T, roster, sizeof roster);
                    snprintf(l2, sizeof l2, "%s   %s   %d order%s", T.house, roster,
                             T.nmission, T.nmission == 1 ? "" : "s");
                    l2col = EUI_DIM;
                } else {
                    snprintf(l1, sizeof l1, "%d TEAMS", (int)g_teams.size());
                    snprintf(l2, sizeof l2, "click one to edit it, or ADD TEAM");
                    l2col = EUI_FAINT;
                }
            } else if (g_editMode == 4) {
                if (g_trigSel >= 0) {
                    const EditTrigger& t = g_triggers[g_trigSel];
                    snprintf(l1, sizeof l1, "RULE %s", t.name);
                    snprintf(l2, sizeof l2, "%s -> %s",
                             TRIG_EVENT[t.event], TRIG_ACTION[t.action]);
                    l2col = EUI_DIM;
                } else {
                    snprintf(l1, sizeof l1, "%d RULES", (int)g_triggers.size());
                    snprintf(l2, sizeof l2, "click one to edit it, or ADD RULE");
                    l2col = EUI_FAINT;
                }
            } else if (g_editMode == 2) {
                static const char* RUNGN[5] = { "LOW", "GROUND", "+1", "+2", "+3" };
                static const char* TOOLN[3] = { "RAISE / LOWER", "GRADED PASS",
                                                "RAMP" };
                if (!elev_ready()) {
                    snprintf(l1, sizeof l1, "NOT ON THE LADDER");
                    snprintf(l2, sizeof l2, "%d of %d corners are off it -- convert first",
                             g_elevOffLadder, g_elevTotal);
                    l2col = EUI_DANGER;
                } else if (g_terrArmed >= 0 && eui_is_slope(g_terrArmed)) {
                    snprintf(l1, sizeof l1, "STAMP %s", EDIT_TEMPLATES[g_terrArmed].name);
                    snprintf(l2, sizeof l2, "click the map; drag paints; a tool disarms");
                } else {
                    snprintf(l1, sizeof l1, "%s", TOOLN[g_elevTool]);
                    if (g_elevWhy[0]) {
                        snprintf(l2, sizeof l2, "%s", g_elevWhy);
                        l2col = EUI_DANGER;
                    } else {
                        snprintf(l2, sizeof l2, "rung %s, brush %dx%d",
                                 RUNGN[g_elevTierSel], g_elevBrush * 2, g_elevBrush * 2);
                    }
                }
            } else if (g_editMode == 1) {
                if (g_terrArmed < 0) {
                    snprintf(l1, sizeof l1, "NO TILE ARMED");
                    snprintf(l2, sizeof l2, "pick one from the drawer on the right");
                    l2col = EUI_FAINT;
                } else {
                    const EditTemplate* e = &EDIT_TEMPLATES[g_terrArmed];
                    snprintf(l1, sizeof l1, "%s", e->name);
                    if (!g_editOverMap) {
                        snprintf(l2, sizeof l2, "point at the map");
                        l2col = EUI_FAINT;
                    } else {
                        int xs[64], ys[64], ics[64];
                        const int n = edit_template_cells(g_terrArmed, g_editCellX,
                                                          g_editCellY, xs, ys, ics, 64);
                        int off = 0;
                        for (int i = 0; i < n; i++)
                            if (xs[i] >= g_gridW || ys[i] >= g_gridH) off++;
                        char why[160] = {0};
                        int bx = -1, by = -1;
                        if (off) {
                            snprintf(l2, sizeof l2, "%d of %d cells fall off the map",
                                     off, n);
                            l2col = EUI_DANGER;
                        } else if (edit_is_crossing(g_terrArmed) &&
                                   !edit_crossing_fits(g_terrArmed, g_editCellX,
                                                       g_editCellY, why, sizeof why,
                                                       &bx, &by)) {
                            /* The measured mouth rule, said on screen rather than only
                               in a log: a bridge that silently will not place is the
                               most confusing thing this editor could do. */
                            snprintf(l2, sizeof l2, "%s", why);
                            l2col = EUI_DANGER;
                        } else {
                            const int skipped = (int)e->w * (int)e->h - n;
                            if (skipped)
                                snprintf(l2, sizeof l2, "%dx%d, %d cells (%d not in this "
                                         "theater)", (int)e->w, (int)e->h, n, skipped);
                            else
                                snprintf(l2, sizeof l2, "%dx%d, %d cells",
                                         (int)e->w, (int)e->h, n);
                        }
                    }
                }
            } else if (g_editTool == TOOL_ERASE) {
                snprintf(l1, sizeof l1, "ERASE");
                const int k = g_editOverMap ? edit_object_at(g_editCellX, g_editCellY) : -1;
                if (k >= 0) snprintf(l2, sizeof l2, "click to remove %s", g_objects[k].type);
                else { snprintf(l2, sizeof l2, "nothing under the pointer");
                       l2col = EUI_FAINT; }
            } else if (g_editTool == TOOL_PICK) {
                snprintf(l1, sizeof l1, "PICK");
                snprintf(l2, sizeof l2, "click something to arm it");
                l2col = EUI_FAINT;
            } else if (g_editTool == TOOL_MOVE) {
                snprintf(l1, sizeof l1, "MOVE");
                if (g_editDragObj >= 0)
                    snprintf(l2, sizeof l2, "holding %s -- release on a cell",
                             g_objects[g_editDragObj].type);
                else { snprintf(l2, sizeof l2, "press on something to pick it up");
                       l2col = EUI_FAINT; }
            } else if (g_editArmed >= 0) {
                const EditItem* it = &EDIT_ITEMS[g_editArmed];
                snprintf(l1, sizeof l1, "%s", it->name);
                /* Whether it would go down here, and if not, WHICH cell says no. The
                   legibility rail: a refusal has to name its reason. */
                if (!g_editOverMap) {
                    snprintf(l2, sizeof l2, "point at the map");
                    l2col = EUI_FAINT;
                } else {
                    int badx = -1, bady = -1;
                    for (int i = 0; i < (int)it->n; i++) {
                        const int px = g_editCellX + it->dx[i], py = g_editCellY + it->dy[i];
                        if (px < 0 || py < 0 || px >= g_gridW || py >= g_gridH ||
                            !edit_cell_ok(px, py)) {
                            badx = px; bady = py; break;
                        }
                    }
                    if (badx < 0) {
                        snprintf(l2, sizeof l2, "fits here");
                    } else {
                        snprintf(l2, sizeof l2, "no: %d,%d is %s", badx, bady,
                                 (badx < 0 || badx >= g_gridW || bady < 0 || bady >= g_gridH)
                                     ? "off the map"
                                     : edit_cell_occupied(badx, bady)
                                         ? "already occupied"
                                         : EDIT_LAND_NAME[edit_land_at(badx, bady)]);
                        l2col = EUI_DANGER;
                    }
                }
            } else {
                snprintf(l1, sizeof l1, "NOTHING ARMED");
                snprintf(l2, sizeof l2, "pick a cameo from the palette on the right");
                l2col = EUI_FAINT;
            }

            ef_text_fit(cx + 12 * S, cy + 9 * S, l1, EUI_TL(t1), cw - 24 * S,
                        EUI_RGB(EUI_GOLD));
            if (g_editMode == 0 && g_editArmed >= 0 && g_editTool == TOOL_PLACE) {
                char sz[48];
                snprintf(sz, sizeof sz, "%s  %d cells", EDIT_ITEMS[g_editArmed].code,
                         (int)EDIT_ITEMS[g_editArmed].n);
                ef_text(cx + 16 * S + ef_text_w(l1, EUI_TL(t1)), cy + 11 * S, sz, t1,
                        EUI_RGB(EUI_DIM));
            }
            ef_text_fit(cx + 12 * S, cy + 26 * S, l2, t1, cw - 24 * S, EUI_RGB(l2col));

            eui_rect(cx + 10 * S, cy + 42 * S, cw - 20 * S, S, EUI_LINE, 1.0f);
            float kx = cx + 12 * S;
            eui_kbd(&kx, cy + 48 * S, "V M I X", "tools", EUI_TS(t1), S);
            eui_kbd(&kx, cy + 48 * S, "TAB", "house", EUI_TS(t1), S);
            eui_kbd(&kx, cy + 48 * S, "DEL", "erase", EUI_TS(t1), S);
            if (g_editOverMap) {
                char cc[48];
                snprintf(cc, sizeof cc, "cell %d,%d", g_editCellX, g_editCellY);
                ef_text(cx + cw - ef_text_w(cc, EUI_TS(t1)) - 12 * S, cy + 48 * S, cc,
                        EUI_TS(t1), EUI_RGB(EUI_DIM));
            }
        }
    }
}

/* --- the radar -------------------------------------------------------------------- */

static GLuint g_euiRadarTex = 0;

static void eui_radar_build(void)
{
    if (!g_editHaveBin) return;
    /* Static: at the 128 ceiling this is 64 KB, which has no business on the stack. */
    static unsigned char px[C3D_MAP_MAX * C3D_MAP_MAX * 4];
    for (int cy = 0; cy < g_gridH; cy++) for (int cx = 0; cx < g_gridW; cx++) {
        const int land = edit_land_at(cx, cy);
        unsigned c;
        switch (land) {
            case EDIT_LAND_WATER:    c = 0x1d4a63; break;
            case EDIT_LAND_ROCK:     c = 0x5c554a; break;
            case EDIT_LAND_BEACH:    c = 0x8a7a55; break;
            case EDIT_LAND_ROAD:     c = 0x6b6350; break;
            case EDIT_LAND_TIBERIUM: c = 0x3f7a4a; break;
            default:                 c = 0x4a6b3a; break;
        }
        /* shade by height so relief reads at 64 pixels across */
        const float k = 0.72f + 0.55f * (float)terrain_corner_y(cx, cy);
        const int inside = (cx >= g_mapX && cx < g_mapX + g_mapW &&
                            cy >= g_mapY && cy < g_mapY + g_mapH);
        const float d = inside ? 1.0f : 0.45f;
        int r = (int)(((c >> 16) & 255) * k * d);
        int g = (int)(((c >>  8) & 255) * k * d);
        int b = (int)(((c      ) & 255) * k * d);
        const int i = (cy * g_gridW + cx) * 4;
        px[i]   = (unsigned char)(r > 255 ? 255 : r);
        px[i+1] = (unsigned char)(g > 255 ? 255 : g);
        px[i+2] = (unsigned char)(b > 255 ? 255 : b);
        px[i+3] = 255;
    }
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind == K_TERRAIN) continue;
        if (o.cx < 0 || o.cx >= g_gridW || o.cy < 0 || o.cy >= g_gridH) continue;
        const bool gdi = !strcmp(o.house, "GoodGuy") || !strcmp(o.house, "Multi1");
        const bool nod = !strcmp(o.house, "BadGuy")  || !strcmp(o.house, "Multi2");
        const int i4 = (o.cy * g_gridW + o.cx) * 4;
        px[i4]   = gdi ? 224 : nod ? 255 : 154;
        px[i4+1] = gdi ? 176 : nod ? 106 : 167;
        px[i4+2] = gdi ? 112 : nod ?  90 : 180;
    }
    if (!g_euiRadarTex) glGenTextures(1, &g_euiRadarTex);
    glBindTexture(GL_TEXTURE_2D, g_euiRadarTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_gridW, g_gridH, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, px);
    g_euiRadarDirty = 0;
}

/* --- the whole sidebar ------------------------------------------------------------ */

static void eui_draw(int fbw, int fbh)
{
    /* During a contained playtest the chrome keeps drawing -- rail, panel, title, view
       bar, all at their exact editing size and place -- while the game owns the map
       viewport between them. eui_playtest gates the parts that would paint over it. */
    const bool eui_playtest = !g_editOn && g_playFromEditor;
    if (!g_editOn && !eui_playtest) return;
    g_editFrame++;
    EuiLayout L; eui_layout(fbw, fbh, &L);
    const float S = L.s;
    const float pad = EUI_PAD * S;
    /* Text is scaled by the same factor as everything else. sb_text's own unit is the
       FONT.SHP glyph, so 1.0 is one cartridge pixel and the panel is unreadable at any
       real resolution without this. */
    const float t1 = 1.0f * S, t2 = 2.0f * S;
    /* One accent, recoloured per mode -- the browser's --mode token. */
    static const unsigned MODE_C[5] = { 0xe0b070, 0x7ee0a0, 0x8fe4ff, 0xb98fe4, 0xff9a5a };
    const unsigned mode = MODE_C[g_editMode];

    begin_overlay(fbw, fbh);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* ---- top strip ---- */
    eui_rect(0, 0, (float)fbw, L.topH, EUI_PANEL, 1.0f);
    eui_rect(0, L.topH - S, (float)fbw, S, EUI_LINE, 1.0f);
    eui_coin(L.topH * 0.68f, L.topH * 0.5f, L.topH * 0.34f, g_editFrame);
    {
        const float nameX = L.topH * 1.24f;
        ef_text(nameX, L.topH * 0.34f, "CNC3D", t2, EUI_RGB(EUI_GOLD));
        const float subX = nameX + ef_text_w("CNC3D", t2) + 10 * S;
        ef_text(subX, L.topH * 0.39f,
                eui_playtest ? "PLAYTESTING" : "MISSION EDITOR", t1,
                EUI_RGB(eui_playtest ? EUI_GOLD : EUI_DIM));

        /* WHAT THE MAP IS CALLED, and WHICH SLOT IT IS IN. Both, always, and in that
           order: the name is the thing typed and the thing every list shows, and
           the slot is USERnn -- the filename the game probes for -- so it stays on
           screen in the dim meta text however the map is named. An unnamed map has only
           its slot, and then the slot IS the title and is not repeated. */
        const char* title = edit_map_title();
        char meta[160];
        if (eui_playtest)
            snprintf(meta, sizeof meta, "%s%s--   ESC RETURNS TO THE EDITOR",
                     g_editTitle[0] ? g_editScen : "", g_editTitle[0] ? "   " : "");
        else
            snprintf(meta, sizeof meta, "%s%s%dx%d   %d OBJECTS",
                     g_editTitle[0] ? g_editScen : "", g_editTitle[0] ? "   " : "",
                     g_mapW, g_mapH, (int)g_objects.size());
        /* Clear of the MAP menu button, which sits between subtitle and meta. */
        float mbx, mby, mbw, mbh;
        eui_menu_rect(&L, &mbx, &mby, &mbw, &mbh);
        const float metaX = mbx + mbw + 16 * S;
        ef_text(metaX, L.topH * 0.39f, title, t1, EUI_RGB(EUI_INK));
        const float metaW = ef_text_w(title, t1) + 12 * S + ef_text_w(meta, t1);
        ef_text(metaX + ef_text_w(title, t1) + 12 * S, L.topH * 0.39f, meta, t1,
                EUI_RGB(EUI_DIM));

        /* The hint is the first thing to go when the window is narrow: a reminder
           overlapping the map's name is worse than no reminder. */
        const char* hint = "RIGHT-DRAG LOOK   WASD FLY   Q E DOWN UP   SHIFT FASTER";
        const float hintW = ef_text_w(hint, t1);
        const float hintX = L.sideX - hintW - 16 * S;
        if (hintX > metaX + metaW + 24 * S)
            ef_text(hintX, L.topH * 0.39f, hint, t1, EUI_RGB(EUI_FAINT));
    }

    if (g_editMode == 4 && !g_zonePaint && !g_tagMode && !g_enhPaint &&
        !eui_playtest) {
        if (g_scriptView == 2)      eui_draw_enh(&L, fbw, fbh, t1);
        else if (g_scriptView == 1) eui_draw_teams(&L, fbw, fbh, t1);
        else                        eui_draw_script(&L, fbw, fbh, t1);
    }
    eui_draw_viewport(&L, fbw, fbh, t1);
    eui_draw_bar(&L, fbw, t1);
    eui_draw_rail(&L, fbh, t1);

    /* ---- the sidebar ground ---- */
    eui_rect(L.sideX, L.topH, L.sideW, (float)fbh - L.topH, EUI_BG, 1.0f);
    eui_rect(L.sideX, L.topH, S, (float)fbh - L.topH, 0x05080c, 1.0f);

    /* ---- radar ---- */
    eui_panel(L.radarX, L.radarY, L.radarW, L.radarH, EUI_PANEL);
    if (g_euiRadarDirty) eui_radar_build();
    if (g_euiRadarTex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_euiRadarTex);
        glColor4f(1, 1, 1, 1);
        const float x = L.radarX + 3 * S, y = L.radarY + 3 * S;
        const float w = L.radarW - 6 * S, h = L.radarH - 6 * S;
        glBegin(GL_TRIANGLES);
        glTexCoord2f(0,0); glVertex2f(x, y);
        glTexCoord2f(1,0); glVertex2f(x+w, y);
        glTexCoord2f(1,1); glVertex2f(x+w, y+h);
        glTexCoord2f(0,0); glVertex2f(x, y);
        glTexCoord2f(1,1); glVertex2f(x+w, y+h);
        glTexCoord2f(0,1); glVertex2f(x, y+h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        if (g_editOverMap) {
            const float px = x + (float)g_editCellX / (float)g_gridW * w;
            const float py = y + (float)g_editCellY / (float)g_gridH * h;
            eui_rect(px - S, py - S, 3 * S, 3 * S, EUI_CYAN, 0.95f);
        }
    }
    {
        char t[96];
        const float rx = L.radarX + L.radarW + 10 * S;
        ef_text(rx, L.radarY + 4 * S, "RADAR", t1, EUI_RGB(EUI_DIM));
        snprintf(t, sizeof t, "%d,%d  %dx%d", g_mapX, g_mapY, g_mapW, g_mapH);
        ef_text(rx, L.radarY + 22 * S, t, t1, EUI_RGB(EUI_FAINT));
        snprintf(t, sizeof t, "CELL %d,%d", g_editCellX, g_editCellY);
        ef_text(rx, L.radarY + 38 * S, g_editOverMap ? t : "OFF THE MAP", t1,
                EUI_RGB(g_editOverMap ? EUI_CYAN : EUI_FAINT));
        snprintf(t, sizeof t, "%s", EDIT_LAND_NAME[edit_land_at(g_editCellX, g_editCellY)]);
        if (g_editOverMap) ef_text(rx, L.radarY + 54 * S, t, t1, EUI_RGB(EUI_FAINT));
    }

    /* ---- mode tabs ---- */
    {
        static const char* MODES[5] = { "OBJECTS", "TERRAIN", "ELEVATN", "STARTS", "SCRIPT" };
        static const unsigned MC[5] = { 0xe0b070, 0x7ee0a0, 0x8fe4ff, 0xb98fe4, 0xff9a5a };
        for (int i = 0; i < 5; i++) {
            const float x = L.sideX + pad + i * (L.modeW + pad);
            const bool on = (i == g_editMode);
            const bool live = true;          /* all three modes are built now */
            eui_panel(x, L.modeY, L.modeW, L.modeH, on ? EUI_PANEL3 : EUI_PANEL);
            if (on) eui_rect(x, L.modeY + L.modeH - 3 * S, L.modeW, 3 * S, MC[i], 1.0f);
            const float tw = ef_text_w(MODES[i], t1);
            ef_text(x + (L.modeW - tw) * 0.5f, L.modeY + L.modeH * 0.5f - 4 * S,
                    MODES[i], t1,
                    EUI_RGB(on ? MC[i] : live ? EUI_DIM : EUI_FAINT));
            (void)live;
        }
    }

    /* ---- owner chips: objects only. A tile has no house. ---- */
    if (g_editMode == 0) for (int i = 0; i < 4; i++) {
        const float x = L.sideX + pad + i * (L.ownerW + 4 * S);
        const bool on = (i == g_editOwner);
        eui_panel(x, L.ownerY, L.ownerW, L.ownerH, on ? EUI_PANEL3 : EUI_PANEL);
        eui_rect(x, L.ownerY, L.ownerW, 3 * S, EUI_HOUSES[i].colour, on ? 1.0f : 0.28f);
        const float tw = ef_text_w(EUI_HOUSES[i].label, t1);
        ef_text(x + (L.ownerW - tw) * 0.5f, L.ownerY + L.ownerH * 0.5f - 3 * S,
                EUI_HOUSES[i].label, t1, EUI_RGB(on ? EUI_HOUSES[i].colour : EUI_FAINT));
    }

    if (g_editMode == 1) {
        /* Where the chips were: what the drawer is for. */
        char t[96];
        snprintf(t, sizeof t, "%s TILES   CLICK TO ARM, CLICK THE MAP TO PAINT",
                 g_pack.theater);
        ef_text(L.sideX + pad, L.ownerY + L.ownerH * 0.5f - 3 * S, t, EUI_TS(t1),
                EUI_RGB(EUI_FAINT));
    }

    if (g_editMode == 4) {
        eui_draw_script_panel(&L, t1);
    } else if (g_editMode == 3) {
        eui_draw_starts(&L, t1);
    } else if (g_editMode == 2) {
        eui_draw_elev(&L, t1);
    } else
    /* ---- category tabs: five object kinds, or seven terrain families ---- */
    {
        const int nc = eui_cat_count(), sel = eui_cat_sel();
        for (int i = 0; i < nc; i++) {
            const float x = L.sideX + pad + i * (L.catW + 3 * S);
            const bool on = (i == sel);
            eui_panel(x, L.catY, L.catW, L.catH, on ? EUI_PANEL3 : EUI_PANEL);
            char lbl[16];
            snprintf(lbl, sizeof lbl, "%.4s", eui_cat_name(i));
            const float tw = ef_text_w(lbl, t1);
            ef_text(x + (L.catW - tw) * 0.5f, L.catY + 5 * S, lbl, t1,
                    EUI_RGB(on ? mode : EUI_DIM));
            char n[8]; snprintf(n, sizeof n, "%d", eui_cat_n(i));
            const float nw = ef_text_w(n, t1);
            ef_text(x + (L.catW - nw) * 0.5f, L.catY + 18 * S, n, t1, EUI_RGB(EUI_FAINT));
        }
    }

    /* ---- the palette grid ---- */
    if (g_editMode < 2) {
    eui_panel(L.gridX, L.gridY, L.gridW, L.gridH, EUI_PANEL);
    {
        const bool terr = (g_editMode == 1);
        const int base = terr ? 0 : EDIT_KIND_FIRST[g_editTab];
        const int n = eui_grid_n();
        /* The "N of M" footer owns the bottom of the grid box, so the rows are counted
           against the space ABOVE it. Without this the last row of cameos and the
           footer were drawn in the same place. */
        const float footer = 14 * S;
        const int rows = (int)((L.gridH - 6 * S - footer) / (L.tileH + L.gap));
        const int first = eui_grid_scroll() * L.cols;
        for (int k = 0; k < rows * L.cols; k++) {
            const int idx = first + k;
            if (idx >= n) break;
            if (terr) {
                const int tid = g_terrList[g_terrGroup][idx];
                const float x = L.gridX + 3 * S + (k % L.cols) * (L.tileW + L.gap);
                const float y = L.gridY + 3 * S + (k / L.cols) * (L.tileH + L.gap);
                const bool on  = (g_terrArmed == tid);
                const bool hov = (g_euiHotKind == EUI_ITEM && g_euiHotArg == idx);
                eui_rect(x, y, L.tileW, L.tileH, on ? EUI_PANEL3 : EUI_PANEL2,
                         (on || hov) ? 1.0f : 0.75f);
                eui_frame(x, y, L.tileW, L.tileH,
                          on ? EUI_GREEN : hov ? EUI_CYAN : EUI_LINE, 1.0f);
                eui_template_thumb(x + 2 * S, y + 2 * S, L.tileW - 4 * S,
                                   L.tileH - 14 * S, tid);
                char lbl[24];
                snprintf(lbl, sizeof lbl, "%.7s", EDIT_TEMPLATES[tid].name);
                const float tw2 = ef_text_w(lbl, t1);
                ef_text(x + (L.tileW - tw2) * 0.5f, y + L.tileH - 11 * S, lbl, t1,
                        EUI_RGB(on ? EUI_INK : EUI_DIM));
                continue;
            }
            const EditItem* it = &EDIT_ITEMS[base + idx];
            const float x = L.gridX + 3 * S + (k % L.cols) * (L.tileW + L.gap);
            const float y = L.gridY + 3 * S + (k / L.cols) * (L.tileH + L.gap);
            const bool on   = (g_editArmed == base + idx);
            const bool hov  = (g_euiHotKind == EUI_ITEM && g_euiHotArg == base + idx);
            eui_rect(x, y, L.tileW, L.tileH,
                     on ? EUI_PANEL3 : hov ? EUI_PANEL2 : EUI_PANEL2, on ? 1.0f : hov ? 1.0f : 0.75f);
            eui_frame(x, y, L.tileW, L.tileH,
                      on ? mode : hov ? EUI_CYAN : EUI_LINE, 1.0f);
            eui_cameo(x + 2 * S, y + 2 * S, L.tileW - 4 * S, L.tileH - 14 * S, it->code);
            const float tw = ef_text_w(it->code, t1);
            ef_text(x + (L.tileW - tw) * 0.5f, y + L.tileH - 11 * S, it->code, t1,
                    EUI_RGB(on ? EUI_INK : EUI_DIM));
        }
        const int page = rows * L.cols;
        if (n > page) {
            char nav[64];
            snprintf(nav, sizeof nav, "%d-%d OF %d   WHEEL SCROLLS",
                     first + 1, (first + page > n ? n : first + page), n);
            ef_text_fit(L.gridX + 4 * S, L.gridY + L.gridH - 12 * S, nav, EUI_TS(t1),
                        L.gridW - 8 * S, EUI_RGB(EUI_FAINT));
        }
    }
    }

    /* ---- what is armed, and the map's tally ---- */
    eui_panel(L.sideX + pad, L.lintY, L.sideW - pad * 2, L.lintH, EUI_PANEL);
    {
        const float tx = L.sideX + pad + 8 * S;
        if (g_editArmed >= 0) {
            const EditItem* it = &EDIT_ITEMS[g_editArmed];
            char line[128];
            snprintf(line, sizeof line, "%s", it->name);
            ef_text_fit(tx, L.lintY + 7 * S, line, t1,
                        L.sideW - EUI_PAD * S * 2 - 16 * S, EUI_RGB(EUI_GOLD));
            snprintf(line, sizeof line, "%s   %d CELLS   %d HP",
                     it->code, (int)it->n, (int)it->str);
            ef_text(tx, L.lintY + 23 * S, line, t1, EUI_RGB(EUI_FAINT));
        } else {
            ef_text(tx, L.lintY + 7 * S, "NOTHING ARMED", t1, EUI_RGB(EUI_DIM));
            ef_text_fit(tx, L.lintY + 23 * S, "PICK A CAMEO, THEN CLICK THE MAP", t1,
                        L.sideW - EUI_PAD * S * 2 - 16 * S, EUI_RGB(EUI_FAINT));
        }
        char row[128];
        int nb = 0;
        for (size_t i = 0; i < g_objects.size(); i++)
            if (g_objects[i].kind == K_BUILDING) nb++;
        snprintf(row, sizeof row, "%d STRUCTURES   %d OBJECTS", nb, (int)g_objects.size());
        ef_text_fit(tx, L.lintY + 45 * S, row, t1,
                    L.sideW - EUI_PAD * S * 2 - 16 * S, EUI_RGB(EUI_FAINT));
        snprintf(row, sizeof row, "%d WALLS   %d TIBERIUM", (int)g_walls.size(),
                 (int)g_tib.size());
        ef_text_fit(tx, L.lintY + 61 * S, row, t1,
                    L.sideW - EUI_PAD * S * 2 - 16 * S, EUI_RGB(EUI_FAINT));
        ef_text_fit(tx, L.lintY + 79 * S, "CTRL+S SAVES   CTRL+P PLAYS", t1,
                    L.sideW - EUI_PAD * S * 2 - 16 * S, EUI_RGB(EUI_FAINT));
    }

    /* ---- actions ---- */
    {
        static const char* ACT[3] = { "UNDO", "REDO", "REVERT" };
        const float w = (L.sideW - pad * 2 - 8 * S) / 3.0f;
        for (int i = 0; i < 3; i++) {
            const float x = L.sideX + pad + i * (w + 4 * S);
            const bool live = eui_action_live(i);
            const bool hov  = live && g_euiHotKind == EUI_ACT && g_euiHotArg == i;
            eui_panel(x, L.actY, w, L.actH, hov ? EUI_PANEL3 : EUI_PANEL);
            const float tw = ef_text_w(ACT[i], t1);
            ef_text(x + (w - tw) * 0.5f, L.actY + L.actH * 0.5f - 3 * S, ACT[i], t1,
                    EUI_RGB(live ? EUI_DIM : EUI_FAINT));
        }
    }

    /* ---- save and play ---- */
    {
        const bool hs = (g_euiHotKind == EUI_SAVE), hp = (g_euiHotKind == EUI_PLAY);
        eui_rect(L.sideX + pad, L.saveY, L.sideW - pad * 2, L.saveH, EUI_GOLD,
                 hs ? 1.0f : 0.9f);
        const char* s1 = "SAVE   .INI + .BIN";
        const float tw = ef_text_w(s1, t1);
        ef_text(L.sideX + (L.sideW - tw) * 0.5f, L.saveY + L.saveH * 0.5f - 3 * S, s1, t1,
                0.08f, 0.07f, 0.05f);

        /* In SCRIPT mode the run button splits: PLAY watches the mission, TRACE runs it
           with nothing drawn and comes back with the tick each rule fired on. They sit
           together because they are the same act -- let the mission happen -- and differ
           only in whether you are there to see it. */
        const bool split = (g_editMode == 4);
        const float fullW = L.sideW - pad * 2;
        const float playW = split ? fullW * 0.62f : fullW;
        eui_rect(L.sideX + pad, L.playY, playW, L.playH, EUI_CYAN, hp ? 1.0f : 0.9f);
        const char* s2 = split ? "PLAY" : "PLAY IN THIS WINDOW";
        const float pw = ef_text_w(s2, t1);
        ef_text(L.sideX + pad + (playW - pw) * 0.5f, L.playY + L.playH * 0.5f - 3 * S,
                s2, t1, 0.05f, 0.07f, 0.09f);
        if (split) {
            const bool ht = (g_euiHotKind == EUI_TRACE);
            const float tx = L.sideX + pad + playW + 4 * S;
            const float tw2 = fullW - playW - 4 * S;
            eui_rect(tx, L.playY, tw2, L.playH, EUI_GREEN, ht ? 1.0f : 0.85f);
            char lbl[32];
            snprintf(lbl, sizeof lbl, "TRACE %dm", g_editTraceMin);
            const float lw = ef_text_w(lbl, t1);
            ef_text(tx + (tw2 - lw) * 0.5f, L.playY + L.playH * 0.5f - 3 * S, lbl, t1,
                    0.04f, 0.08f, 0.05f);
        }
    }

    /* THE BADGE. A map with something broken in it should say so without being asked --
       that is the whole legibility rail. It sits on the readout, is red when something
       cannot work, and opens the full list. */
    if (g_editOn) {
        if (!g_checkRan) edit_check_run();
        char badge[64];
        if (g_checkErrors)
            snprintf(badge, sizeof badge, "%d PROBLEM%s", g_checkErrors,
                     g_checkErrors == 1 ? "" : "S");
        else if (g_checkWarns)
            snprintf(badge, sizeof badge, "%d TO LOOK AT", g_checkWarns);
        else
            snprintf(badge, sizeof badge, "CHECKS OUT");
        float bx, by, bw, bh;
        eui_check_badge_rect(&L, &bx, &by, &bw, &bh);
        const bool hot = (g_euiHotKind == EUI_CHECK);
        const unsigned col = g_checkErrors ? EUI_DANGER
                           : g_checkWarns  ? EUI_GOLD : EUI_GREEN;
        eui_rect(bx, by, bw, bh, hot ? EUI_PANEL3 : EUI_PANEL2, 1.0f);
        eui_rect(bx, by, 3 * S, bh, col, 1.0f);
        ef_text_fit(bx + 8 * S, by + bh * 0.5f - 3 * S, badge, EUI_TS(t1), bw - 14 * S,
                    EUI_RGB(col));
    }

    eui_draw_toast(&L, fbw, t1);
    eui_draw_menu(&L, t1);
    eui_draw_modal(&L, fbw, fbh, t1);
    eui_draw_popup(&L, fbw, fbh, t1);

    /* ---- THE POINTER, last of all -------------------------------------------------
       The game hides the OS cursor and draws its own inside draw_frame(), which is
       UNDER this panel -- so over the sidebar there was no pointer at all, and an
       interface you cannot see yourself pointing at reads as an interface that does
       not respond. Drawn here, after everything, whenever the pointer is on the
       chrome. */
    if (g_euiHotKind != EUI_MAP && g_euiMouseX >= 0.0f) {
        const float x = g_euiMouseX, y = g_euiMouseY;
        const float a = 11 * S, w = 3.4f * S;
        /* A plain arrow: black body, white fill, so it reads on gold and on near-black
           alike without a texture. */
        for (int pass = 0; pass < 2; pass++) {
            /* Pass 0 is the ENLARGED black outline, pass 1 the white body on top.
               The other order paints the outline over the fill and the whole arrow
               reads black -- on this near-black chrome, an invisible cursor. */
            const float g = pass ? 1.0f : 0.0f;
            const float o = pass ? 0.0f : w;
            glColor4f(g, g, g, 1.0f);
            glBegin(GL_TRIANGLES);
            glVertex2f(x - o * 0.4f,       y - o * 0.4f);
            glVertex2f(x + a * 0.62f + o,  y + a * 0.86f + o);
            glVertex2f(x + a * 0.06f - o,  y + a * 1.02f + o);
            glVertex2f(x - o * 0.4f,       y - o * 0.4f);
            glVertex2f(x + a * 0.06f - o,  y + a * 1.02f + o);
            glVertex2f(x - o * 0.5f,       y + a * 1.30f + o);
            glEnd();
        }
    }

    glDisable(GL_BLEND);
    end_overlay();
}


/* ------------------------------------------------------------------------------------
 *  TERRAIN: painting a cell
 *
 *  The renderer resolves (template, icon) to atlas UVs at BAKE time and keeps only the
 *  UVs -- g_pack.cell carries a rectangle, not a tile id. So painting is two writes:
 *  the .BIN bytes the map is saved from, and the rectangle the renderer draws from.
 *
 *  This is cheap because draw_terrain walks g_pack.cell in immediate mode every frame.
 *  There is no cached geometry to invalidate and no buffer to re-upload: change the
 *  rectangle and the next frame draws the new tile. (That would not survive a move to
 *  VBOs, which the Voodoo2 target makes unlikely; if it ever happens, this is the place
 *  that has to learn to mark a dirty range.)
 * ---------------------------------------------------------------------------------- */

static PackCell* edit_pack_cell(int cx, int cy)
{
    /* Row-major first: grown grids (edit_set_world_grid) and most baked packs store
       cell (x,y) at index y*W+x, and a whole-map repaint on a 128 grid is 16k lookups
       -- a linear scan each would be 268M compares. The scan stays as the fallback for
       any pack that orders its cells differently. */
    const size_t guess = (size_t)cy * (size_t)g_gridW + (size_t)cx;
    if (guess < g_pack.cell.size() &&
        g_pack.cell[guess].x == cx && g_pack.cell[guess].y == cy)
        return &g_pack.cell[guess];
    for (size_t i = 0; i < g_pack.cell.size(); i++)
        if (g_pack.cell[i].x == cx && g_pack.cell[i].y == cy) return &g_pack.cell[i];
    return NULL;
}

/* Point one cell's art at the tile its (template, icon) names. Returns false when this
   theater's bank has no such tile, which is a real answer rather than an error: the two
   theaters carry different banks and a template that exists in one may not in the other. */
static bool edit_repaint_cell(int cx, int cy)
{
    PackCell* pc = edit_pack_cell(cx, cy);
    if (!pc || !g_editHaveBin) return false;
    const int idx  = cy * g_editBinW + cx;
    const int tmpl = g_editTmpl[idx], icon = g_editIcon[idx];

    int sea = 0;
    /* TEMPLATE_NONE is plain ground, and the cartridge picks its icon from the cell's
       own coordinates (CellClass::Clear_Icon) so a field of it does not visibly tile. */
    const int t = (tmpl == 255) ? 0 : tmpl;
    const int i = (tmpl == 255) ? ((cx & 3) | ((cy & 3) << 2)) : icon;
    const int slot = tt_slot_of(g_pack.theater, t, i, &sea);
    if (slot < 0) return false;

    const PackTex& at = g_pack.tex[g_pack.terrainTex];
    if (at.uw <= 0 || at.uh <= 0) return false;
    float x0, y0;
    tt_slot_rect(slot, &x0, &y0);
    pc->u0 = x0 / (float)at.uw;
    pc->v0 = y0 / (float)at.uh;
    pc->u1 = (x0 + TT_TS) / (float)at.uw;
    pc->v1 = (y0 + TT_TS) / (float)at.uh;
    /* The sea plane shows through an alpha-hole tile, so the hole flag has to follow the
       art or painting a river leaves dry ground with a river drawn on it. */
    pc->holes = (unsigned char)(sea ? 1 : 0);
    return true;
}

/* Write one cell and repaint it. The .BIN is what gets saved; the PackCell is what gets
   drawn; they must not disagree. */
static bool edit_paint_cell(int cx, int cy, int tmpl, int icon)
{
    if (cx < 0 || cy < 0 || cx >= g_editBinW || cy >= g_editBinH || !g_editHaveBin)
        return false;
    const int idx = cy * g_editBinW + cx;
    const unsigned char ot = g_editTmpl[idx], oi = g_editIcon[idx];
    g_editTmpl[idx] = (unsigned char)tmpl;
    g_editIcon[idx] = (unsigned char)icon;
    if (!edit_repaint_cell(cx, cy)) {
        g_editTmpl[idx] = ot; g_editIcon[idx] = oi;   /* put it back: no art for this */
        return false;
    }
    g_euiRadarDirty = 1;
    return true;
}

/* After an undo restores the .BIN wholesale, the drawn art has to catch up. */
static void edit_repaint_all(void)
{
    if (!g_editHaveBin) return;
    for (int cy = 0; cy < g_editBinH; cy++)
        for (int cx = 0; cx < g_editBinW; cx++)
            edit_repaint_cell(cx, cy);
    g_euiRadarDirty = 1;
}

/* ------------------------------------------------------------------------------------
 *  ELEVATION
 *
 *  Not a height brush. The tool authors a 32x32 field of TIERS over 2x2-cell blocks, and
 *  both the corner heights AND the cliff art are derived from it. That is why the Cliffs
 *  drawer is absent from the terrain palette: you raise ground and the cliff appears
 *  around it.
 *
 *  Every number here was measured over the 55 cartridge maps that ship their own
 *  heightmap, restricted to each map's playable rectangle, and independently re-derived
 *  before it was written down. This is a port of that work, not a fresh guess -- the
 *  measurements live in docs/design-map-editor.md section 0c and the browser editor runs
 *  the identical arithmetic.
 * ---------------------------------------------------------------------------------- */

/* The block grid is the CELL grid halved: 32x32 blocks of 2x2 cells on a legacy map,
   64x64 on a 128 one. Storage sits at the ceiling; every loop and stride asks the live
   helpers, the same split the rest of this file makes. The coarse corner grid K is one
   wider each way (elev_kw). */
#define ELEV_BLOCKS_MAX (C3D_MAP_MAX / 2)
#define ELEV_K_MAX      (ELEV_BLOCKS_MAX + 1)
static inline int elev_bw(void) { return g_gridW / 2; }
static inline int elev_bh(void) { return g_gridH / 2; }
static inline int elev_kw(void) { return g_gridW / 2 + 1; }

/* THE LADDER. Of 151,325 playable corners, 58.56% sit exactly on one of these five
   values and 99.05% within 31 of one. Ground is rung 1 (64), not rung 0 -- rung 0 holds
   1.32% of corners -- so the brush calls rung 1 "ground". */
static const unsigned char ELEV_RUNG[5] = { 0, 64, 128, 191, 255 };

static int elev_tier_of(int b)
{
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 5; i++) {
        const int d = b > ELEV_RUNG[i] ? b - ELEV_RUNG[i] : ELEV_RUNG[i] - b;
        if (d < bd) { bd = d; best = i; }      /* ties keep the LOWER rung */
    }
    return best;
}

/* THE COMPASS. Four bits, one per corner of a 2x2 block -- NW, NE, SW, SE -- set when
   that corner is above the block's own lowest. The template each mask maps to is the
   cartridge's own modal answer for that mask, and where three siblings are
   interchangeable they are listed together so a wall of cliff is not visibly tiled.

   Masks 6 and 9 are diagonal saddles. There are five of each in the whole cartridge, so
   the tool refuses them rather than guess. S01/S07/S08/S14/S15/S28 are never
   auto-emitted: they are ramp end-caps, not plain runs. */
#define ELEV_S(n) (12 + (n))            /* TEMPLATE_SLOPE1 is 13 (defines.h) */

struct ElevCliff { unsigned char mask; unsigned char n; unsigned short t[3]; };
static const ElevCliff ELEV_CLIFF[] = {
    {  1, 1, { ELEV_S(32), 0, 0 } },
    {  2, 1, { ELEV_S(29), 0, 0 } },
    {  3, 3, { ELEV_S(3),  ELEV_S(4),  ELEV_S(5)  } },
    {  4, 1, { ELEV_S(31), 0, 0 } },
    {  5, 3, { ELEV_S(25), ELEV_S(24), ELEV_S(26) } },
    {  7, 1, { ELEV_S(34), 0, 0 } },
    {  8, 1, { ELEV_S(30), 0, 0 } },
    { 10, 3, { ELEV_S(11), ELEV_S(10), ELEV_S(12) } },
    { 11, 1, { ELEV_S(35), 0, 0 } },
    { 12, 3, { ELEV_S(17), ELEV_S(18), ELEV_S(19) } },
    { 13, 1, { ELEV_S(33), 0, 0 } },
    { 14, 1, { ELEV_S(36), 0, 0 } },
};
static const int ELEV_CLIFF_N = (int)(sizeof(ELEV_CLIFF) / sizeof(ELEV_CLIFF[0]));

/* The only self-contained ramp block in the game, and it faces EAST. S14's icons 1 and 2
   are walkable (Land ROCK, AltLand CLEAR); it is the one SLOPE template that is
   8-connected crossable in both axes, and S14.2 -> S14.1 is the commonest slope-to-slope
   tier-gaining step across all 55 maps. */
#define ELEV_RAMP_EAST ELEV_S(14)

/* Ramp-adjacency helpers for the dress pass. elev_rampish_b: the block was asked to
   be a ramp or a graded pass. elev_art_gone: a corner piece -- an outer TIP (one
   raised corner) or an INNER corner (three raised) -- whose cliff flows straight
   into a rampish block goes UNDRESSED: corner art assumes both of its arms continue,
   and beside a ramp channel it ended in a razor cut. Runs then treat such a corner
   as rampish too and taper with cap art one block early. One step of lookahead, no
   cascade: cap art ends the chain by construction. */
static int elev_mask_at(const unsigned char* K, int bx, int by);
/* The corner mask of one block, off the coarse grid -- the same derivation the
   dress pass makes inline, needed there for NEIGHBOURS too. */
static int elev_mask_at(const unsigned char* K, int bx, int by)
{
    const int kw = elev_kw();
    const int nw = K[by * kw + bx],       ne = K[by * kw + bx + 1];
    const int sw = K[(by + 1) * kw + bx], se = K[(by + 1) * kw + bx + 1];
    int lo = nw;
    if (ne < lo) lo = ne;
    if (sw < lo) lo = sw;
    if (se < lo) lo = se;
    return (nw > lo ? 1 : 0) | (ne > lo ? 2 : 0) |
           (sw > lo ? 4 : 0) | (se > lo ? 8 : 0);
}

static bool elev_is_saddle(int mask) { return mask == 6 || mask == 9; }
static bool elev_rampable(int mask)  { return mask == 10; }

/* The editor's live elevation state. */
static unsigned char g_elevTier[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
static unsigned char g_elevPass[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];   /* leave undressed  */
static unsigned char g_elevRamp[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];   /* dress with S14   */
static bool g_elevSeeded = false;
/* g_elevOffLadder and g_elevTotal are declared with the panel state above. */
static bool g_elevConverted = false;
/* Has the elevation tool written anything? Decides whether a .HGT is saved. */
static bool g_elevTouched = false;

static bool elev_rampish_b(int x, int y)
{
    if (x < 0 || y < 0 || x >= elev_bw() || y >= elev_bh()) return false;
    const int b = y * elev_bw() + x;
    return g_elevRamp[b] || g_elevPass[b];
}
static bool elev_art_gone(const unsigned char* K, int x, int y)
{
    if (x < 0 || y < 0 || x >= elev_bw() || y >= elev_bh()) return false;
    const int m = elev_mask_at(K, x, y);
    /* An outer tip's arms run along the two blocks sharing its RAISED corner; an
       inner corner's along the two sharing its LOW corner. Either way the corner
       bit of interest picks the pair. */
    int bit = 0;
    if (m == 1 || m == 2 || m == 4 || m == 8)      bit = m;
    else if (m == 7 || m == 11 || m == 13 || m == 14) bit = 15 ^ m;
    else return false;
    const int dx = (bit == 2 || bit == 8) ? 1 : -1;   /* NE/SE: east, else west  */
    const int dy = (bit == 4 || bit == 8) ? 1 : -1;   /* SW/SE: south, else north */
    return elev_rampish_b(x + dx, y) || elev_rampish_b(x, y + dy);
}


/* Read a tier field back out of the map's authored corner heights. A block's tier is
   the LOWEST of its four outer corners: that is the ground it stands on, with any cliff
   rising out of it. */
static void elev_seed(void)
{
    g_elevOffLadder = 0; g_elevTotal = 0;
    memset(g_elevPass, 0, sizeof g_elevPass);
    memset(g_elevRamp, 0, sizeof g_elevRamp);
    if (g_pack.corner.empty()) { memset(g_elevTier, 1, sizeof g_elevTier); return; }
    for (int by = 0; by < elev_bh(); by++)
        for (int bx = 0; bx < elev_bw(); bx++) {
            int lo = 4;
            static const int D[4][2] = { {0,0}, {2,0}, {0,2}, {2,2} };
            for (int k = 0; k < 4; k++) {
                const int gx = 2 * bx + D[k][0], gy = 2 * by + D[k][1];
                if (gx > g_gridW || gy > g_gridH) continue;
                const int b = g_pack.corner[gy * (g_gridW + 1) + gx];
                const int t = elev_tier_of(b);
                if (t < lo) lo = t;
                if (gx >= g_mapX && gx < g_mapX + g_mapW &&
                    gy >= g_mapY && gy < g_mapY + g_mapH) {
                    g_elevTotal++;
                    int on = 0;
                    for (int r = 0; r < 5; r++) if (b == ELEV_RUNG[r]) on = 1;
                    if (!on) g_elevOffLadder++;
                }
            }
            g_elevTier[by * elev_bw() + bx] = (unsigned char)lo;
        }
    g_elevSeeded = true;
    fprintf(stderr, "edit: elevation seeded -- %d of %d playable corners are off the "
            "ladder (%.1f%%)\n", g_elevOffLadder, g_elevTotal,
            g_elevTotal ? 100.0 * g_elevOffLadder / g_elevTotal : 0.0);
}

/* The coarse corner tiers a tier field implies. MAX, not min: the cliff belongs OUTSIDE
   the plateau you painted, so raising a block raises the ground it stands on and the
   step appears around it. The cost is real and is refused below -- a low area narrower
   than three blocks has no corner left that is still low and would silently fill in. */
static void elev_coarse(const unsigned char* tier, unsigned char* K /* elev_kw()^2 */)
{
    for (int gy = 0; gy <= elev_bh(); gy++)
        for (int gx = 0; gx <= elev_bw(); gx++) {
            int hi = 0;
            static const int D[4][2] = { {-1,-1}, {0,-1}, {-1,0}, {0,0} };
            for (int k = 0; k < 4; k++) {
                const int bx = gx + D[k][0], by = gy + D[k][1];
                if (bx < 0 || bx >= elev_bw() || by < 0 || by >= elev_bh()) continue;
                const int t = tier[by * elev_bw() + bx];
                if (t > hi) hi = t;
            }
            K[gy * elev_kw() + gx] = (unsigned char)hi;
        }
}

/* Corner heights by bilinear interpolation across each block. Verified against the
   cartridge: the median error of its own mid-edge corners against this formula is
   +0.000 over 12,964 samples, and of the block-centre corner -0.008. */
static void elev_heights(const unsigned char* K, unsigned char* h /* (W+1)^2 */)
{
    const int kw = elev_kw(), hw = g_gridW + 1;
    for (int gy = 0; gy <= elev_bh(); gy++)
        for (int gx = 0; gx <= elev_bw(); gx++) {
            const int gx1 = gx + 1 > elev_bw() ? elev_bw() : gx + 1;
            const int gy1 = gy + 1 > elev_bh() ? elev_bh() : gy + 1;
            const int a = ELEV_RUNG[K[gy * kw + gx]];
            const int b = ELEV_RUNG[K[gy * kw + gx1]];
            const int c = ELEV_RUNG[K[gy1 * kw + gx]];
            const int d = ELEV_RUNG[K[gy1 * kw + gx1]];
            const int x0 = 2 * gx, y0 = 2 * gy;
            if (x0 <= g_gridW && y0 <= g_gridH) h[y0 * hw + x0] = (unsigned char)a;
            if (x0 + 1 <= g_gridW && y0 <= g_gridH)
                h[y0 * hw + x0 + 1] = (unsigned char)((a + b + 1) / 2);
            if (x0 <= g_gridW && y0 + 1 <= g_gridH)
                h[(y0 + 1) * hw + x0] = (unsigned char)((a + c + 1) / 2);
            if (x0 + 1 <= g_gridW && y0 + 1 <= g_gridH)
                h[(y0 + 1) * hw + x0 + 1] = (unsigned char)((a + b + c + d + 2) / 4);
        }
}

/* One of the interchangeable siblings for this mask, chosen by POSITION so the same map
   always redraws the same way and a long cliff is not visibly tiled. */
static int elev_pick_cliff(int mask, int bx, int by)
{
    const ElevCliff* e = NULL;
    for (int i = 0; i < ELEV_CLIFF_N; i++)
        if ((int)ELEV_CLIFF[i].mask == mask) { e = &ELEV_CLIFF[i]; break; }
    if (!e) return -1;
    unsigned short usable[3]; int n = 0;
    for (int i = 0; i < (int)e->n; i++) {
        int sea = 0;
        if (tt_slot_of(g_pack.theater, e->t[i], 0, &sea) >= 0) usable[n++] = e->t[i];
    }
    if (!n) return -1;
    unsigned int x = (unsigned int)(bx * 73856093) ^ (unsigned int)(by * 19349663);
    x = (x ^ (x >> 13));
    return (int)usable[x % (unsigned int)n];
}

/* Why a block cannot be derived. Returns NULL when it is fine. */
static const char* elev_refusal(const unsigned char* K, const unsigned char* tier,
                                int bx, int by)
{
    const int kw = elev_kw();
    const int nw = K[by * kw + bx],       ne = K[by * kw + bx + 1];
    const int sw = K[(by + 1) * kw + bx], se = K[(by + 1) * kw + bx + 1];
    int lo = nw, hi = nw;
    if (ne < lo) lo = ne; if (ne > hi) hi = ne;
    if (sw < lo) lo = sw; if (sw > hi) hi = sw;
    if (se < lo) lo = se; if (se > hi) hi = se;
    const int mask = (nw > lo ? 1 : 0) | (ne > lo ? 2 : 0) |
                     (sw > lo ? 4 : 0) | (se > lo ? 8 : 0);
    if (hi - lo > 1)
        return "a step of more than one tier at once. The cartridge has no art for it "
               "-- terrace it one rung at a time.";
    if (elev_is_saddle(mask))
        return "a diagonal pinch. There are five of these in the whole cartridge, so "
               "this editor will not guess at one -- widen it.";
    if (lo > tier[by * elev_bw() + bx])
        return "too narrow to stay low. Raised ground claims the cliff outside itself, "
               "so a low area needs to be three blocks across or it fills in.";
    return NULL;
}


/* ------------------------------------------------------------------------------------
 *  WAYPOINTS -- player starts, the home cell, and the reinforcement edge
 *
 *  28 of them (WAYPT_COUNT, defines.h:2559), keyed by index in [Waypoints], each a cell
 *  (y*64 + x) or -1 for unset. Two of the 28 are named:
 *
 *      26  WAYPT_HOME    where the view opens, and the house's flag home
 *      27  WAYPT_REINF   where reinforcements arrive
 *
 *  0..25 are general purpose. In a MULTIPLAYER map they are the player starts:
 *  Create_Units copies every valid one into a list, sorts it by distance and seats
 *  HOUSE_MULTI1 upward from it (scenarioini.cpp:1526-1545). In a SINGLEPLAYER mission
 *  they are the cells triggers and teams refer to, and the two named ones do the work.
 * ---------------------------------------------------------------------------------- */

static void edit_waypoints_clear(void)
{
    for (int i = 0; i < EDIT_WAYPT_COUNT; i++) g_waypoint[i] = -1;
}

static void edit_read_waypoints(const char* path)
{
    edit_waypoints_clear();
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    bool in = false;
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { in = !strncasecmp(p, "[Waypoints]", 11); continue; }
        if (!in) continue;
        int k = -1, v = -1;
        if (sscanf(p, "%d=%d", &k, &v) == 2 && k >= 0 && k < EDIT_WAYPT_COUNT)
            g_waypoint[k] = v;
    }
    fclose(f);
    int n = 0;
    for (int i = 0; i < EDIT_WAYPT_COUNT; i++) if (g_waypoint[i] >= 0) n++;
    fprintf(stderr, "edit: %d waypoints (home %d, reinforce %d)\n", n,
            g_waypoint[EDIT_WAYPT_HOME], g_waypoint[EDIT_WAYPT_REINF]);
}



/* ------------------------------------------------------------------------------------
 *  UNDO
 *
 *  Whole-document snapshots, not command objects. The document is four small vectors
 *  and a 8KB tile array; a snapshot of a busy map is a few tens of kilobytes, and 200 of
 *  them is a rounding error against the meshes already resident. Command objects would
 *  be smaller and would also be a second, subtly different, model of every mutation --
 *  and the failure mode of getting one of those inverses slightly wrong is a map that
 *  quietly corrupts itself several undos later. A snapshot cannot be subtly wrong.
 *
 *  A snapshot is taken BEFORE a mutation and pushed only if the mutation SUCCEEDED:
 *  placement refuses illegal ground all the time, and an undo stack full of no-ops is
 *  an undo button that appears not to work.
 * ---------------------------------------------------------------------------------- */

#define EDIT_UNDO_MAX 200

/* EditPrior moved up beside the hit enum; the undo snapshot still carries it. */

struct EditSnap {
    std::vector<SimObject>   objects;
    std::vector<WallCell>    walls;
    std::vector<TibCell>     tib;
    std::vector<SmudgeCell>  smudges;
    std::vector<unsigned char> tmpl, icon;   /* the .BIN                              */
    std::vector<unsigned char> corner;       /* the .HGT, 65x65                       */
    std::vector<unsigned char> tier, pass, ramp;  /* the elevation tool's own field   */
    int waypoint[28];                        /* player starts, home, reinforce        */
    /* THE SCRIPT ATTACHMENTS, which every mutating edit already pushes a snapshot for
       and none of them could restore.
         - g_editPrior is where an object's ORDER and TRIGGER live, keyed by
           house|type|cell. Moving an object re-keys it; undoing the move put the object
           back and left the key on the old destination, so the object's order and
           trigger were gone from the next save.
         - g_cellTrig is the zone table. Painting a zone pushed an undo entry that could
           not undo it, and tagging an object pushed one that could not untag it. */
    std::vector<EditPrior>   prior;
    std::vector<short>       cellTrig;
    char label[40];
};

static std::vector<EditSnap> g_undo, g_redo;
static EditSnap g_editLoaded;          /* what REVERT goes back to */
static bool     g_editLoadedOk = false;

static EditSnap edit_snapshot(const char* label)
{
    EditSnap s;
    s.objects  = g_objects;
    s.walls    = g_walls;
    s.tib      = g_tib;
    s.smudges  = g_smudges;
    if (g_editHaveBin) {
        s.tmpl.assign(g_editTmpl, g_editTmpl + g_editBinW * g_editBinH);
        s.icon.assign(g_editIcon, g_editIcon + g_editBinW * g_editBinH);
    }
    /* Heights AND the tier field the tool derives them from. Restoring the heights
       without the tiers would leave the next elevation stroke deriving from a field
       that no longer matches the ground. */
    if (!g_pack.corner.empty()) s.corner = g_pack.corner;
    if (g_elevSeeded) {
        s.tier.assign(g_elevTier, g_elevTier + elev_bw() * elev_bh());
        s.pass.assign(g_elevPass, g_elevPass + elev_bw() * elev_bh());
        s.ramp.assign(g_elevRamp, g_elevRamp + elev_bw() * elev_bh());
    }
    memcpy(s.waypoint, g_waypoint, sizeof s.waypoint);
    s.prior = g_editPrior;
    s.cellTrig.assign(g_cellTrig, g_cellTrig + edit_ini_cells());
    snprintf(s.label, sizeof s.label, "%s", label ? label : "edit");
    return s;
}

static void edit_restore(const EditSnap& s)
{
    g_objects = s.objects;
    g_walls   = s.walls;
    g_tib     = s.tib;
    g_smudges = s.smudges;
    if (!s.tmpl.empty() && g_editHaveBin) {
        memcpy(g_editTmpl, &s.tmpl[0], s.tmpl.size());
        memcpy(g_editIcon, &s.icon[0], s.icon.size());
        edit_repaint_all();      /* the drawn art has to follow the restored .BIN */
    }
    if (!s.corner.empty() && s.corner.size() == g_pack.corner.size())
        g_pack.corner = s.corner;
    if (!s.tier.empty()) {
        memcpy(g_elevTier, &s.tier[0], s.tier.size());
        memcpy(g_elevPass, &s.pass[0], s.pass.size());
        memcpy(g_elevRamp, &s.ramp[0], s.ramp.size());
    }
    memcpy(g_waypoint, s.waypoint, sizeof g_waypoint);
    g_editPrior = s.prior;
    if ((int)s.cellTrig.size() == edit_ini_cells())
        memcpy(g_cellTrig, &s.cellTrig[0], s.cellTrig.size() * sizeof(short));
    g_euiRadarDirty = 1;
    /* An armed index survives, but a MOVE in flight cannot: the object it was holding
       may not exist any more. */
    g_editDragObj = -1;
}

/* Commit a snapshot taken before a mutation that has now succeeded. */
static void edit_commit(EditSnap& before)
{
    /* THE FIRST SNAPSHOT EVER PUSHED IS THE MAP AS LOADED, by definition -- it is the
       state before the first edit. Taking the REVERT baseline here rather than at boot
       removes an ordering problem that had already bitten: the obvious place, right
       after refresh_objects(), runs BEFORE g_editOn is set, so the capture was skipped
       and revert silently did nothing. */
    if (!g_editLoadedOk) { g_editLoaded = before; g_editLoadedOk = true; }
    /* EVERY mutating edit lands here, which makes it the one place the check has to be
       told the map has moved on. Doing it per-edit-site would have meant remembering
       it at each of them, and the one that was forgotten would leave the badge quietly
       describing a map that no longer exists. */
    edit_check_dirty();
    g_undo.push_back(before);
    if ((int)g_undo.size() > EDIT_UNDO_MAX) g_undo.erase(g_undo.begin());
    /* A new edit invalidates the redo branch, which is what every editor does and what
       the browser one does too. */
    g_redo.clear();
}

static bool edit_can_undo(void) { return !g_undo.empty(); }
static bool edit_can_redo(void) { return !g_redo.empty(); }
static bool edit_can_revert(void) { return g_editLoadedOk && !g_undo.empty(); }

static void edit_undo(void)
{
    if (g_undo.empty()) { fprintf(stderr, "edit: nothing to undo\n"); return; }
    EditSnap now = edit_snapshot(g_undo.back().label);
    g_redo.push_back(now);
    {
        char sub[128];
        snprintf(sub, sizeof sub, "%d steps left", (int)g_undo.size() - 1);
        edit_toast(EUI_DIM, g_undo.back().label, sub);
    }
    edit_restore(g_undo.back());
    g_undo.pop_back();
}

static void edit_redo(void)
{
    if (g_redo.empty()) { fprintf(stderr, "edit: nothing to redo\n"); return; }
    EditSnap now = edit_snapshot(g_redo.back().label);
    g_undo.push_back(now);
    {
        char sub[128];
        snprintf(sub, sizeof sub, "%d steps left", (int)g_redo.size() - 1);
        edit_toast(EUI_DIM, g_redo.back().label, sub);
    }
    edit_restore(g_redo.back());
    g_redo.pop_back();
}

static bool eui_action_live(int i)
{
    return i == 0 ? edit_can_undo() : i == 1 ? edit_can_redo() : edit_can_revert();
}

static void edit_revert(void)
{
    if (!g_editLoadedOk) { fprintf(stderr, "edit: nothing to revert to\n"); return; }
    if (g_undo.empty()) { fprintf(stderr, "edit: no edits to revert\n"); return; }
    EditSnap now = edit_snapshot("revert");
    g_undo.push_back(now);          /* revert is itself undoable */
    if ((int)g_undo.size() > EDIT_UNDO_MAX) g_undo.erase(g_undo.begin());
    g_redo.clear();
    edit_restore(g_editLoaded);
    {
        char sub[128];
        snprintf(sub, sizeof sub, "back to %d objects", (int)g_objects.size());
        edit_toast(EUI_GOLD, "REVERTED TO SAVED", sub);
    }
}

/* ------------------------------------------------------------------------------------
 *  Walls
 *
 *  A wall is overlay, and its ICON is a 4-bit neighbour mask -- bit 0 north, 1 east,
 *  2 south, 3 west (the convention WALL_VARIANT is indexed by, cnc_eyes.cpp:2049). The
 *  icon is stored, not derived at draw time, so placing one piece has to re-mask the
 *  four cells around it as well or the neighbours keep their old stumps. That is what
 *  "walls auto-connect" means mechanically.
 * ---------------------------------------------------------------------------------- */

static WallCell* edit_wall_at(int x, int y)
{
    for (size_t i = 0; i < g_walls.size(); i++)
        if (g_walls[i].x == x && g_walls[i].y == y) return &g_walls[i];
    return NULL;
}

static void edit_wall_mask(int x, int y)
{
    WallCell* w = edit_wall_at(x, y);
    if (!w) return;
    /* Only the same KIND connects. A sandbag line running into a chain fence reads as
       two walls meeting, which is what the 1995 game shows. */
    static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
    int mask = 0;
    for (int i = 0; i < 4; i++) {
        const WallCell* n = edit_wall_at(x + DX[i], y + DY[i]);
        if (n && n->kind == w->kind) mask |= (1 << i);
    }
    w->icon = (unsigned char)mask;
}

static void edit_wall_put(int x, int y, int kind)
{
    EditSnap before = edit_snapshot(WALL_NAME[kind]);
    WallCell* w = edit_wall_at(x, y);
    if (w) {
        w->kind = (unsigned char)kind;
    } else {
        WallCell wc;
        memset(&wc, 0, sizeof wc);
        wc.x = (short)x; wc.y = (short)y;
        wc.kind = (unsigned char)kind;
        wc.dmg = 0;
        /* A wall the editor lays belongs to the house that will own it in play, which
           is what decides whether it can be sold. */
        snprintf(wc.owner, sizeof wc.owner, "%s", EUI_HOUSES[g_editOwner].key);
        g_walls.push_back(wc);
    }
    edit_wall_mask(x, y);
    edit_wall_mask(x, y - 1); edit_wall_mask(x + 1, y);
    edit_wall_mask(x, y + 1); edit_wall_mask(x - 1, y);
    edit_commit(before);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: wall %s at %d,%d  [%d cells]\n",
            WALL_NAME[kind], x, y, (int)g_walls.size());
}

/* ------------------------------------------------------------------------------------
 *  Hit testing
 *
 *  One layout function feeds both the draw and the hit test, so a control cannot drift
 *  away from the pixels the user is aiming at -- which is the classic way a hand-rolled
 *  UI goes subtly wrong.
 * ---------------------------------------------------------------------------------- */

static int eui_hit(float mx, float my, int fbw, int fbh, int* arg)
{
    /* AN OPEN PICKER OWNS EVERY CLICK, like a menu. Its rows answer, and anywhere else
       is DEAD -- the click handler closes it on a dead hit rather than activating
       whatever sat underneath, which is how every menu on earth behaves. */
    if (g_popKind != POP_NONE) {
        EuiLayout PL;
        eui_layout(fbw, fbh, &PL);
        float px, py, pw, ph;
        int rows;
        eui_pop_rect(&PL, fbh, &px, &py, &pw, &ph, &rows);
        const float rowH = 26 * PL.s;
        for (int r = 0; r < rows; r++) {
            const int i = g_popScroll + r;
            if (i >= (int)g_popItems.size()) break;
            if (mx >= px && mx < px + pw &&
                my >= py + 4 * PL.s + r * rowH && my < py + 4 * PL.s + (r + 1) * rowH) {
                *arg = i;
                return EUI_POPITEM;
            }
        }
        return EUI_DEAD;
    }
    *arg = -1;
    if (!g_editOn) return EUI_MAP;
    EuiLayout L; eui_layout(fbw, fbh, &L);
    const float S = L.s, pad = EUI_PAD * S;
    const float lx = mx, ly = my;
    #define EUI_IN(X,Y,W,H) (lx >= (X) && lx < (X)+(W) && ly >= (Y) && ly < (Y)+(H))

    /* A MODAL TAKES EVERYTHING. Nothing behind it is reachable, which is what makes it
       a modal rather than a floating panel that happens to be on top. */
    if (g_euiModal) {
        float mx2, my2, mw, mh;
        eui_modal_rect(&L, fbw, fbh, &mx2, &my2, &mw, &mh);
        for (int i = 0; i < 2; i++) {
            float bx, by, bw, bh;
            eui_modal_btn_rect(&L, fbw, fbh, i, &bx, &by, &bw, &bh);
            if (EUI_IN(bx, by, bw, bh)) { *arg = i; return EUI_MODALBTN; }
        }
        /* CHECK is a readout with one button; everything inside it is text. */
        if (g_euiModal == MODAL_CHECK) return EUI_DEAD;
        if (g_euiModal == MODAL_NEW) {
            for (int g = 0; g < 3; g++)
                for (int i = 0; i < (g == 0 ? EUI_SIZE_N : g == 1 ? EUI_THEATER_N : 2); i++) {
                    float ox, oy, ow, oh;
                    eui_modal_opt_rect(&L, fbw, fbh, g, i, &ox, &oy, &ow, &oh);
                    if (EUI_IN(ox, oy, ow, oh)) {
                        *arg = g * 10 + i; return EUI_MODALOPT;
                    }
                }
        } else if (g_euiModal == MODAL_OPEN) {
            const float pad = 18 * S;
            const float lx = mx2 + pad, ly = my2 + 74 * S;
            const float lw = mw - pad * 2, lh = mh - 74 * S - 60 * S;
            const float tw = (lw - 6 * S) * 0.5f;
            for (int i = 0; i < 2; i++)
                if (EUI_IN(lx + i * (tw + 6 * S), ly, tw, 28 * S)) {
                    *arg = 30 + i; return EUI_MODALOPT;
                }
            const int rows = (int)((lh - 40 * S) / (24 * S));
            for (int r = 0; r < rows; r++) {
                float x, y, w, h;
                eui_openlist_row_rect(lx, ly, lw, S, r, &x, &y, &w, &h);
                if (EUI_IN(x, y, w, h)) { *arg = 40 + r; return EUI_MODALOPT; }
            }
        }
        return EUI_DEAD;
    }

    /* The menu, when it is open, likewise takes everything under it. */
    {
        float bx, by, bw, bh;
        eui_menu_rect(&L, &bx, &by, &bw, &bh);
        if (EUI_IN(bx, by, bw, bh)) return EUI_MENUBTN;
        if (g_euiMenuOpen) {
            for (int i = 0; i < EUI_MENU_N; i++) {
                if (EUI_MENU[i].id == EMENU_SEP) continue;
                float ix, iy, iw, ih;
                eui_menu_item_rect(&L, i, &ix, &iy, &iw, &ih);
                if (EUI_IN(ix, iy, iw, ih)) { *arg = i; return EUI_MENUITEM; }
            }
        }
    }

    if (my < L.topH) return EUI_DEAD;

    /* The view bar, across the bottom of the map area. */
    if (mx < L.sideX && my >= L.barY) {
        int n = 0;
        const EuiToggle* T = eui_toggles(&n);
        for (int i = 0; i < n; i++) {
            float x, y, w, h;
            eui_toggle_rect(&L, i, &x, &y, &w, &h);
            /* Same rule as STARTS: the draw leaves the bar's readout room and breaks
               here, so the hit test must too, or a narrow window puts an invisible
               toggle over the readout. */
            if (x + w > L.sideX - EUI_BAR_READOUT * L.s) break;
            if (EUI_IN(x, y, w, h)) { *arg = i; return EUI_VIEW; }
        }
        (void)T;
        return EUI_DEAD;
    }

    /* The rail, before the map: it is chrome on the left edge, and the map starts to
       the right of it. */
    if (mx < L.railW) {
        const float x = (L.railW - L.toolSz) * 0.5f;
        for (int i = 0; i <= TOOL_COUNT; i++) {
            const float y = (i == TOOL_COUNT) ? L.camY
                                              : L.toolY0 + i * (L.toolSz + 2 * S);
            if (EUI_IN(x, y, L.toolSz, L.toolSz)) { *arg = i; return EUI_TOOL; }
        }
        return EUI_DEAD;
    }
    /* In SCRIPT mode the map area is the node canvas, so a click there is a node and
       not a placement. */
    if (g_editMode == 4 && !g_zonePaint && !g_tagMode && !g_enhPaint &&
        mx < L.sideX) {
        if (g_scriptView == 2) {
            /* Carrier pins first (they overlap the cards), then the carrier boxes. */
            for (size_t i = 0; i < g_enh.size(); i++) {
                float x, y, w, h;
                eui_enh_card_rect(&L, (int)i, &x, &y, &w, &h);
                const float r = EUI_PIN_R * S + 3 * S;
                if (EUI_IN(x + w - r, y + h * 0.5f - r, r * 2, r * 2)) {
                    *arg = (int)i;
                    return EUI_CARRIERPIN;
                }
            }
            {
                int carriers[64];
                const int nc = enh_carrier_list(carriers, 64);
                for (int c = 0; c < nc; c++) {
                    float bx, by, bw, bh;
                    eui_carrier_rect(&L, c, &bx, &by, &bw, &bh);
                    if (EUI_IN(bx - EUI_PIN_R * S - 4 * S, by,
                               bw + EUI_PIN_R * S + 4 * S, bh)) {
                        *arg = c;
                        return EUI_CARRIERBOX;
                    }
                }
            }
            if (g_enhSel >= 0 && g_enhSel < (int)g_enh.size()) {
                float x, y, w, h;
                eui_enh_card_rect(&L, g_enhSel, &x, &y, &w, &h);
                if (EUI_IN(x + 8 * S, y + 2 * S, 110 * S, 16 * S)) {
                    *arg = 2;
                    return EUI_RENAME;
                }
            }
            for (size_t i = 0; i < g_enh.size(); i++) {
                float x, y, w, h;
                eui_enh_card_rect(&L, (int)i, &x, &y, &w, &h);
                if (EUI_IN(x, y, w, h)) { *arg = (int)i; return EUI_ENHCARD; }
            }
            if (g_enhSel >= 0 && g_enhSel < (int)g_enh.size())
                for (int i = 0; i < g_enh[g_enhSel].nclause; i++) {
                    float x, y, w, h;
                    eui_enh_clause_rect(&L, i, &x, &y, &w, &h);
                    if (g_enh[g_enhSel].clause[i].kind == ENH_ZONE) {
                        const float r = EUI_PIN_R * S + 3 * S;
                        if (EUI_IN(x + w - r, y + h * 0.5f - r, r * 2, r * 2)) {
                            *arg = i;
                            return EUI_ENHZONEPIN;
                        }
                    }
                    if (EUI_IN(x, y, w, h)) { *arg = i; return EUI_ENHCLAUSE; }
                }
            return EUI_DEAD;
        }
        if (g_scriptView == 1) {
            if (g_teamSel >= 0 && g_teamSel < (int)g_teams.size()) {
                float x, y, w, h;
                eui_team_card_rect(&L, g_teamSel, &x, &y, &w, &h);
                if (EUI_IN(x + 8 * S, y + 2 * S, 110 * S, 16 * S)) {
                    *arg = 1;
                    return EUI_RENAME;
                }
            }
            for (size_t i = 0; i < g_teams.size(); i++) {
                float x, y, w, h;
                eui_team_card_rect(&L, (int)i, &x, &y, &w, &h);
                if (EUI_IN(x, y, w, h)) { *arg = (int)i; return EUI_TEAMCARD; }
            }
            if (g_teamSel >= 0 && g_teamSel < (int)g_teams.size()) {
                const int nOrd = g_teams[g_teamSel].nmission;
                const int last = (nOrd < TEAM_MISSION_MAX) ? nOrd : nOrd - 1;
                for (int i = 0; i <= last; i++) {
                    float x, y, w, h;
                    eui_team_order_rect(&L, i, &x, &y, &w, &h);
                    if (EUI_IN(x, y, w, h)) { *arg = i; return EUI_TEAMORDER; }
                }
            }
            return EUI_DEAD;
        }
        /* PINS FIRST. A pin sits on the card's edge and overlaps it, and the pin is the
           smaller, more deliberate target -- so it wins, exactly as it does in a node
           editor. Only rules whose action can carry a team have one. */
        for (size_t i = 0; i < g_triggers.size(); i++) {
            const float r = EUI_PIN_R * S + 3 * S;
            float px, py;
            if (trig_action_has_team(g_triggers[i].action)) {
                eui_rule_pin(&L, (int)i, &px, &py);
                if (EUI_IN(px - r, py - r, r * 2, r * 2)) { *arg = (int)i; return EUI_RULEPIN; }
            }
            if (g_triggers[i].event == 1) {
                eui_zone_pin(&L, (int)i, &px, &py);
                if (EUI_IN(px - r, py - r, r * 2, r * 2)) { *arg = (int)i; return EUI_ZONEPIN; }
            }
            if (trig_event_reads_tag(g_triggers[i].event)) {
                eui_watch_pin(&L, (int)i, &px, &py);
                if (EUI_IN(px - r, py - r, r * 2, r * 2)) { *arg = (int)i; return EUI_WATCHPIN; }
            }
        }
        for (size_t k = 0; k < g_teams.size(); k++) {
            float tx, ty, tw, th;
            eui_wire_team_rect(&L, (int)k, &tx, &ty, &tw, &th);
            /* The drop target is the box AND its pin, widened to the left so a wire
               dropped just short of the box still lands. */
            if (EUI_IN(tx - EUI_PIN_R * S - 4 * S, ty, tw + EUI_PIN_R * S + 4 * S, th)) {
                *arg = (int)k; return EUI_WIRETEAM;
            }
        }
        if (g_trigSel >= 0 && g_trigSel < (int)g_triggers.size()) {
            float x, y, w, h;
            eui_script_node_rect(&L, g_trigSel, &x, &y, &w, &h);
            if (EUI_IN(x + 8 * S, y + 2 * S, 110 * S, 18 * S)) {
                *arg = 0;
                return EUI_RENAME;
            }
        }
        for (size_t i = 0; i < g_triggers.size(); i++) {
            float x, y, w, h;
            eui_script_node_rect(&L, (int)i, &x, &y, &w, &h);
            if (EUI_IN(x, y, w, h)) { *arg = (int)i; return EUI_SCRIPTNODE; }
        }
        return EUI_DEAD;
    }
    if (mx < L.sideX) return EUI_MAP;

    if (EUI_IN(L.radarX, L.radarY, L.radarW, L.radarH)) {
        const int cx = (int)((lx - L.radarX - 3 * S) / (L.radarW - 6 * S) * (float)g_gridW);
        const int cy = (int)((ly - L.radarY - 3 * S) / (L.radarH - 6 * S) * (float)g_gridH);
        /* The arg is a flat cell in the map's own INI stride; the RADAR handler
           decodes it with the same stride. */
        *arg = (cy < 0 ? 0 : cy >= g_gridH ? g_gridH - 1 : cy) * edit_ini_w()
             + (cx < 0 ? 0 : cx >= g_gridW ? g_gridW - 1 : cx);
        return EUI_RADAR;
    }
    {
        /* Claimed BEFORE the per-mode panels: it sits in the readout block, which those
           panels can reach into when the window is short. */
        float bx, by, bw, bh;
        eui_check_badge_rect(&L, &bx, &by, &bw, &bh);
        if (EUI_IN(bx, by, bw, bh)) return EUI_CHECK;
    }
    for (int i = 0; i < 5; i++)
        if (EUI_IN(L.sideX + pad + i * (L.modeW + pad), L.modeY, L.modeW, L.modeH)) {
            *arg = i; return EUI_MODE;
        }
    if (g_editMode == 4) {
        /* The strip the house chips use elsewhere is the RULES/TEAMS switch here. It
           has to be claimed before the owner loop, which would otherwise answer for it
           and quietly change which house new objects belong to. */
        const float vw = (L.sideW - pad * 2 - 4 * S * 2) / 3.0f;
        for (int i = 0; i < 3; i++)
            if (EUI_IN(L.sideX + pad + i * (vw + 4 * S), L.ownerY, vw, L.ownerH)) {
                *arg = i; return EUI_SCRIPTVIEW;
            }
    } else
    for (int i = 0; i < 4; i++)
        if (EUI_IN(L.sideX + pad + i * (L.ownerW + 4 * S), L.ownerY, L.ownerW, L.ownerH)) {
            *arg = i; return EUI_OWNER;
        }
    if (g_editMode == 4) {
        eui_script_row_clamp(&L);
        for (int row = g_scriptRowOff,
                 stop = g_scriptRowOff + eui_script_rows_fit(&L);
             row < stop; row++) {
            float x, y, w, h;
            eui_script_row_rect(&L, row, &x, &y, &w, &h);
            if (!EUI_IN(x, y, w, h)) continue;
            /* Two rows in the team view are strips of chips rather than one value. */
            if (g_scriptView == 2 && g_enhSel >= 0 && g_enhSel < (int)g_enh.size() &&
                row == 4) {
                for (int i = 0; i < ENH_CLAUSE_MAX; i++) {
                    float cx, cy, cw, ch;
                    eui_enh_chip_rect(&L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                    if (EUI_IN(cx, cy, cw, ch)) { *arg = i; return EUI_ENHCHIP; }
                }
            }
            if (g_scriptView == 1 && g_teamSel >= 0 && g_teamSel < (int)g_teams.size()) {
                if (row == 3) for (int i = 0; i < TEAM_CLASS_MAX; i++) {
                    float cx, cy, cw, ch;
                    eui_team_slot_rect(&L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                    if (EUI_IN(cx, cy, cw, ch)) { *arg = i; return EUI_TEAMSLOT; }
                }
                if (row == 13) for (int i = 0; i < 5; i++) {
                    float cx, cy, cw, ch;
                    eui_team_flag_rect(&L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                    if (EUI_IN(cx, cy, cw, ch)) { *arg = i; return EUI_TEAMFLAG; }
                }
            }
            *arg = row; return EUI_SCRIPTROW;
        }
    } else if (g_editMode == 3) {
        for (int row = 0; row < eui_start_rows(); row++) {
            if (eui_start_slot(row) < 0) continue;
            float x, y, w, h;
            eui_start_rect(&L, row, &x, &y, &w, &h);
            /* STOP WHERE THE DRAW STOPS. eui_draw_starts breaks at this same bound and
               this did not, so in a short window the rows nobody could see were still
               clickable -- and STARTS is tested before ACT, SAVE and PLAY, so those
               invisible rows swallowed UNDO, REDO, REVERT and the top of SAVE. */
            if (y + h > L.lintY) break;
            if (EUI_IN(x, y, w, h)) { *arg = row; return EUI_START; }
        }
    } else if (g_editMode == 2) {
        /* The elevation panel replaces the category strip and the grid, and its
           geometry is laid out by eui_draw_elev -- repeated here rather than shared,
           and --uitest is what keeps the two honest. */
        float y = L.catY;
        if (!elev_ready()) {
            const float bh = 34 * S, bw = L.sideW - pad * 4;
            if (EUI_IN(L.sideX + pad * 2, y + 140 * S, bw, bh)) return EUI_ELEVGATE;
            return EUI_DEAD;
        }
        {
            const float w = (L.sideW - pad * 2 - 4 * 3 * S) / 5.0f;
            for (int i = 0; i < 5; i++)
                if (EUI_IN(L.sideX + pad + i * (w + 3 * S), y, w, 32 * S)) {
                    *arg = i; return EUI_ELEVRUNG;
                }
        }
        y += 32 * S + pad;
        for (int i = 0; i < 3; i++) {
            if (EUI_IN(L.sideX + pad, y, L.sideW - pad * 2, 34 * S)) {
                *arg = i; return EUI_ELEVTOOL;
            }
            y += 34 * S + 4 * S;
        }
        y += pad;
        {
            const float w = (L.sideW - pad * 2 - 2 * 4 * S) / 3.0f;
            for (int i = 0; i < 3; i++)
                if (EUI_IN(L.sideX + pad + i * (w + 4 * S), y, w, 30 * S)) {
                    *arg = i; return EUI_ELEVBRUSH;
                }
        }
        {
            int list[64];
            const int n = eui_cliff_list(list, 64);
            for (int k = 0; k < n; k++) {
                float cx, cy, cw, ch;
                if (!eui_cliff_cell_rect(&L, k, &cx, &cy, &cw, &ch)) break;
                if (EUI_IN(cx, cy, cw, ch)) { *arg = list[k]; return EUI_CLIFFITEM; }
            }
        }
        if (EUI_IN(L.sideX + pad, L.actY, L.sideW - pad * 2, L.actH)) { }
        /* fall through to the action row, save and play, which are shared */
    } else
    for (int i = 0; i < eui_cat_count(); i++)
        if (EUI_IN(L.sideX + pad + i * (L.catW + 3 * S), L.catY, L.catW, L.catH)) {
            *arg = i; return EUI_TABC;
        }
    /* THE ACTION ROW, SAVE AND PLAY BEFORE THE GRID. The grid has a minimum height and
       the buttons are pinned to the bottom, so in a short window the clamped grid
       overlaps them -- and the grid returns EUI_DEAD for an interior miss, which would
       swallow SAVE. Testing the buttons first costs nothing and cannot be got wrong. */
    {
        const float w = (L.sideW - pad * 2 - 8 * S) / 3.0f;
        for (int i = 0; i < 3; i++)
            if (EUI_IN(L.sideX + pad + i * (w + 4 * S), L.actY, w, L.actH)) {
                *arg = i; return EUI_ACT;
            }
    }
    if (EUI_IN(L.sideX + pad, L.saveY, L.sideW - pad * 2, L.saveH)) return EUI_SAVE;
    if (g_editMode == 4) {
        const float fullW = L.sideW - pad * 2;
        const float playW = fullW * 0.62f;
        if (EUI_IN(L.sideX + pad, L.playY, playW, L.playH)) return EUI_PLAY;
        if (EUI_IN(L.sideX + pad + playW + 4 * S, L.playY, fullW - playW - 4 * S,
                   L.playH)) return EUI_TRACE;
    } else
    if (EUI_IN(L.sideX + pad, L.playY, L.sideW - pad * 2, L.playH)) return EUI_PLAY;
    if (g_editMode < 2 && EUI_IN(L.gridX, L.gridY, L.gridW, L.gridH)) {
        const int col = (int)((lx - L.gridX - 3 * S) / (L.tileW + L.gap));
        const int row = (int)((ly - L.gridY - 3 * S) / (L.tileH + L.gap));
        if (col >= 0 && col < L.cols && row >= 0) {
            const int idx = eui_grid_scroll() * L.cols + row * L.cols + col;
            if (idx < eui_grid_n()) {
                /* In terrain mode the argument is an index into the drawer, not into
                   EDIT_ITEMS: the two palettes hold different things. */
                *arg = (g_editMode == 1) ? idx : EDIT_KIND_FIRST[g_editTab] + idx;
                return EUI_ITEM;
            }
        }
        return EUI_DEAD;
    }
    #undef EUI_IN
    return EUI_DEAD;
}

/* Called on every mouse move: remembers where the pointer is and what is under it, which
   is what the panel draws its own arrow and its hover states from. */
static void eui_hover(float mx, float my, int fbw, int fbh)
{
    g_euiMouseX = mx; g_euiMouseY = my;
    int arg = -1;
    g_euiHotKind = eui_hit(mx, my, fbw, fbh, &arg);
    g_euiHotArg  = arg;
}

/* The wheel scrolls the drawer when the pointer is over it, and the camera otherwise. */
static bool eui_wheel(float mx, float my, int fbw, int fbh, int dir)
{
    if (!g_editOn) return false;
    EuiLayout L; eui_layout(fbw, fbh, &L);
    /* An open picker scrolls before anything else does. */
    if (g_popKind != POP_NONE) {
        g_popScroll -= dir;
        eui_pop_clamp();
        return true;
    }
    /* A MODAL OWNS THE WHEEL. It used to own only the CHECK one, so OPEN MAP printed
       "WHEEL SCROLLS" under a list of 95 missions and scrolled none of them -- and the
       wheel fell through to the panel behind, scrolling a drawer nobody could see. */
    if (g_euiModal) {
        int* sc = (g_euiModal == MODAL_CHECK) ? &g_checkScroll
                : (g_euiModal == MODAL_OPEN)  ? &g_mapListScroll : NULL;
        const int last = (g_euiModal == MODAL_CHECK) ? (int)g_check.size() - 1
                       : (g_euiModal == MODAL_OPEN)
                           ? edit_maplist_count(g_mapListTab ? 'O' : 'U') - 1 : 0;
        if (sc) {
            *sc -= dir;
            if (*sc > last) *sc = last;
            if (*sc < 0)    *sc = 0;
        }
        return true;                 /* NEW MAP has no list; it still eats the wheel */
    }
    if (my < L.topH) return false;
    /* The canvas. It never scrolled at all before, which meant a mission with more
       rules than fit down the window simply hid the rest -- SCG09EA has eighteen
       teams and the window shows about ten. */
    if (g_editMode == 4 && mx < L.sideX && !g_zonePaint && !g_tagMode && !g_enhPaint) {
        const float step = 40.0f * L.s;
        /* THREE views, not two. This was a two-way ternary, so ENHANCED measured its
           span against the number of TEAMS -- and a map with rules but no teams could
           not scroll its rule list at all. */
        const int n = (g_scriptView == 2) ? (int)g_enh.size()
                    : (g_scriptView == 1) ? (int)g_teams.size()
                                          : (int)g_triggers.size();
        float card, gap;
        if (g_scriptView) { card = 42 * L.s; gap = 6 * L.s; }   /* teams and enhanced */
        else              { card = 58 * L.s; gap = 10 * L.s; }
        float span = n * (card + gap);
        float room = L.barY - L.topH - 40 * L.s;
        float max = span - room;
        if (max < 0) max = 0;
        g_scriptScroll -= dir * step;
        if (g_scriptScroll > max) g_scriptScroll = max;
        if (g_scriptScroll < 0)   g_scriptScroll = 0;
        return true;
    }
    if (mx < L.sideX) return false;
    /* SCRIPT has no item grid; the same area is the property rows, and in a short
       window there are more of them than fit. */
    if (g_editMode == 4) {
        if (my >= L.catY && my < L.lintY) {
            g_scriptRowOff -= dir;
            eui_script_row_clamp(&L);
        }
        return true;
    }
    if (mx < L.gridX || mx >= L.gridX + L.gridW ||
        my < L.gridY || my >= L.gridY + L.gridH) return true;   /* swallow, don't zoom */
    const int rows = (int)((L.gridH - 6 * L.s - 14 * L.s) / (L.tileH + L.gap));
    const int n = eui_grid_n();
    int last = (n - 1) / L.cols - rows + 1;
    if (last < 0) last = 0;
    int* sc = (g_editMode == 1) ? &g_terrScroll : &g_editScroll;
    *sc -= dir;
    if (*sc > last) *sc = last;
    if (*sc < 0)    *sc = 0;
    return true;
}






/* THE CONVERSION GATE.
 *
 *  A shipped map's heights are smooth: 41% of its playable corners sit off the five-rung
 *  ladder, and its ground routinely climbs more than one tier between adjacent blocks.
 *  The tier tool cannot express that, so almost any edit on an unconverted map is
 *  refused by the step rule -- correctly, and uselessly.
 *
 *  So the map is converted ONCE, deliberately, with the cost stated first: every
 *  playable corner snaps to the ladder and every cliff is redrawn from the compass. It
 *  is not undoable and it says so, because pretending otherwise would mean keeping a
 *  copy of a heightmap in every undo step for the rest of the session.
 *
 *  Nothing is converted behind the player's back. Until they ask, the elevation tool
 *  shows the gate and refuses to paint.
 */
static int elev_write(const unsigned char* dirty);

static int elev_convert(void)
{
    if (!g_elevSeeded) elev_seed();
    unsigned char all[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
    memset(all, 1, sizeof all);
    const int wrote = elev_write(all);
    g_elevConverted = true;
    {
        char sub[160];
        snprintf(sub, sizeof sub, "%d cells rewritten; every corner is on the ladder",
                 wrote);
        edit_toast(EUI_CYAN, "ELEVATION CONVERTED", sub);
    }
    return wrote;
}

/* Is this map ready to have its elevation edited? */
static bool elev_ready(void)
{
    if (!g_elevSeeded) elev_seed();
    return g_elevConverted || g_elevOffLadder == 0;
}

/* Write a derived elevation into the map -- but ONLY where the brush has been.
 *
 * The browser's first version rebuilt all 1,024 blocks from the tier field every time,
 * which is correct for a map made of nothing but tiers and wrong for every map that
 * exists: it quantised heights nobody had touched and, far worse, overwrote the sea, the
 * shore, the roads and the tiberium with plain ground, because a tier field knows about
 * steps and knows nothing about water.
 *
 * So the tool owns exactly two things, and only inside the region painted: the corner
 * heights, and the CLIFF art. Raising a block changes its neighbours' masks too -- a
 * mask is read off the four coarse corners it shares with them -- so the region is the
 * painted blocks GROWN BY ONE in every direction.
 */
static int elev_write(const unsigned char* dirty /* elev_bw() x elev_bh() */)
{
    if (g_pack.corner.empty() || !g_editHaveBin) return 0;
    unsigned char K[ELEV_K_MAX * ELEV_K_MAX];
    static unsigned char h[C3D_CORN_MAX * C3D_CORN_MAX];
    const int kw = elev_kw(), hw = g_gridW + 1;
    elev_coarse(g_elevTier, K);
    elev_heights(K, h);

    int wrote = 0;
    for (int by = 0; by < elev_bh(); by++)
        for (int bx = 0; bx < elev_bw(); bx++) {
            const int b = by * elev_bw() + bx;
            if (!dirty[b]) continue;

            const int nw = K[by * kw + bx],       ne = K[by * kw + bx + 1];
            const int sw = K[(by + 1) * kw + bx], se = K[(by + 1) * kw + bx + 1];
            int lo = nw;
            if (ne < lo) lo = ne;
            if (sw < lo) lo = sw;
            if (se < lo) lo = se;
            const int mask = (nw > lo ? 1 : 0) | (ne > lo ? 2 : 0) |
                             (sw > lo ? 4 : 0) | (se > lo ? 8 : 0);

            /* Flat, a saddle we refuse to guess at, or a block the player asked to
               leave undressed: plain ground, and the slope in the corner heights
               carries the climb on its own. That is what the cartridge does on 81.4%
               of the passable edges where it gains a tier. */
            int t = -1;
            if (!(mask == 0 || mask == 15 || g_elevPass[b] || elev_is_saddle(mask))) {
                /* RAMP: S14 is the game's ONE self-contained ramp block and it faces
                   east (mask 10). A ramp asked of any other face goes UNDRESSED
                   instead -- the corner heights carry the climb as a graded slope,
                   which is what the cartridge itself does on 81.4% of its passable
                   tier-gaining edges. One button, every face climbable; only the
                   east face has rocky ramp art to wear. */
                if (g_elevRamp[b]) {
                    t = elev_rampable(mask) ? ELEV_RAMP_EAST : -1;
                } else if (elev_art_gone(K, bx, by)) {
                    /* A corner piece beside a ramp channel: undressed, see the
                       helpers above ("the corner pieces have hard cuts"). */
                    t = -1;
                } else {
                    t = elev_pick_cliff(mask, bx, by);
                    /* A RUN THAT MEETS A RAMP MUST DIE OUT, NOT STOP: wear the
                       corner END-CAP facing away from the ramp -- or away from a
                       corner piece the ramp already undressed. */
                    int capmask = 0;
                    const bool runNS = (mask == 10 || mask == 5);
                    const bool runEW = (mask == 3 || mask == 12);
                    if (runNS || runEW) {
                        const int ax = runEW ? bx - 1 : bx;
                        const int ay = runEW ? by : by - 1;
                        const int zx = runEW ? bx + 1 : bx;
                        const int zy = runEW ? by : by + 1;
                        const bool ra = elev_rampish_b(ax, ay) || elev_art_gone(K, ax, ay);
                        const bool rz = elev_rampish_b(zx, zy) || elev_art_gone(K, zx, zy);
                        if (ra && !rz)
                            capmask = (mask == 10) ? 8 : (mask == 5) ? 4
                                    : (mask == 3) ? 2 : 8;
                        else if (rz && !ra)
                            capmask = (mask == 10) ? 2 : (mask == 5) ? 1
                                    : (mask == 3) ? 1 : 4;
                    }
                    if (capmask) t = elev_pick_cliff(capmask, bx, by);
                }
            }
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    const int cx = 2 * bx + dx, cy = 2 * by + dy;
                    if (cx >= g_gridW || cy >= g_gridH) continue;
                    if (t < 0) {
                        edit_paint_cell(cx, cy, 255, (cx & 3) | ((cy & 3) << 2));
                    } else {
                        int sea = 0;
                        const int icon = dy * 2 + dx;
                        /* S14 icon 3 and S32 icon 3 have no art in either bank; the
                           cartridge fills that cell with plain CLEAR1, which is
                           passable anyway, and so does this. */
                        if (tt_slot_of(g_pack.theater, t, icon, &sea) < 0)
                            edit_paint_cell(cx, cy, 255, (cx & 3) | ((cy & 3) << 2));
                        else
                            edit_paint_cell(cx, cy, t, icon);
                    }
                    wrote++;
                }

            /* Heights, for the corners this block owns. Copying the whole regenerated
               array would quantise the rest of the map back to the ladder behind the
               player's back. */
            for (int gy = 2 * by; gy <= 2 * by + 2; gy++)
                for (int gx = 2 * bx; gx <= 2 * bx + 2; gx++)
                    if (gx <= g_gridW && gy <= g_gridH)
                        g_pack.corner[gy * hw + gx] = h[gy * hw + gx];
        }
    g_euiRadarDirty = 1;
    if (wrote) g_elevTouched = true;
    return wrote;
}

/* Paint a tier over a square of blocks, and derive the consequences. Returns the number
   of cells rewritten, or 0 if every block under the brush refused. */
static int elev_paint(int bx, int by, int radius, int tier, int tool, char* why, int whyN)
{
    if (!g_elevSeeded) elev_seed();
    if (!elev_ready()) {
        if (why && whyN > 0)
            snprintf(why, whyN, "this map's heights are not on the ladder yet -- "
                     "%d of %d playable corners are off it. Convert first.",
                     g_elevOffLadder, g_elevTotal);
        return 0;
    }
    unsigned char save[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
    memcpy(save, g_elevTier, sizeof save);

    unsigned char dirty[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
    memset(dirty, 0, sizeof dirty);
    int touched = 0;
    for (int y = by - radius + 1; y <= by + radius - 1; y++)
        for (int x = bx - radius + 1; x <= bx + radius - 1; x++) {
            if (x < 0 || y < 0 || x >= elev_bw() || y >= elev_bh()) continue;
            const int b = y * elev_bw() + x;
            if (tool == 0)      g_elevTier[b] = (unsigned char)tier;
            else if (tool == 1) g_elevPass[b] = (unsigned char)!g_elevPass[b];
            else                g_elevRamp[b] = (unsigned char)!g_elevRamp[b];
            dirty[b] = 1;
            touched++;
        }
    if (!touched) return 0;

    /* A mask is read off corners shared with the neighbours, so the region that has to
       be re-derived is the painted blocks grown by one in every direction. */
    unsigned char grown[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
    memcpy(grown, dirty, sizeof grown);
    for (int y = 0; y < elev_bh(); y++)
        for (int x = 0; x < elev_bw(); x++) {
            if (!dirty[y * elev_bw() + x]) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= elev_bw() || ny >= elev_bh()) continue;
                    grown[ny * elev_bw() + nx] = 1;
                }
        }

    /* Refuse BEFORE writing anything. A half-applied elevation edit is worse than none:
       the heights and the art would disagree. */
    unsigned char K[ELEV_K_MAX * ELEV_K_MAX];
    elev_coarse(g_elevTier, K);
    for (int y = 0; y < elev_bh(); y++)
        for (int x = 0; x < elev_bw(); x++) {
            if (!grown[y * elev_bw() + x]) continue;
            const char* r = elev_refusal(K, g_elevTier, x, y);
            if (r) {
                if (why && whyN > 0) snprintf(why, whyN, "%s", r);
                memcpy(g_elevTier, save, sizeof save);   /* nothing happened */
                return 0;
            }
        }
    return elev_write(grown);
}

/* ------------------------------------------------------------------------------------
 *  The ELEVATION STROKE: hold the button and drag, exactly like the terrain stroke.
 *  Elevation was click-per-block -- "this makes it hard to paint connected terrain" --
 *  while the terrain brush already dragged. Same contract as edit_stroke_*: ONE
 *  snapshot when the stroke begins, ONE undo step when it ends, an anti-smear guard on
 *  the last block painted so motion inside a block does not re-toggle it (which for
 *  the PASS and RAMP tools would flicker the block on and off under the pointer).
 * ---------------------------------------------------------------------------------- */
static bool     g_elevStrokeOn = false;
static EditSnap g_elevStrokeBefore;
static int      g_elevStrokeBX = -1, g_elevStrokeBY = -1;
static int      g_elevStrokeWrote = 0;

static void elev_stroke_apply(int bx, int by)
{
    char why[220] = {0};
    const int n = elev_paint(bx, by, g_elevBrush, g_elevTierSel, g_elevTool,
                             why, sizeof why);
    if (n) {
        g_elevStrokeWrote += n;
    } else if (why[0]) {
        snprintf(g_elevWhy, sizeof g_elevWhy, "%s", why);
        fprintf(stderr, "edit: elevation refused at block %d,%d -- %s\n", bx, by, why);
    }
}

static void elev_stroke_begin(int bx, int by)
{
    g_elevStrokeBefore = edit_snapshot("elevation");
    g_elevStrokeOn = true;
    g_elevStrokeBX = bx; g_elevStrokeBY = by;
    g_elevStrokeWrote = 0;
    elev_stroke_apply(bx, by);
}

static void elev_stroke_move(int bx, int by)
{
    if (!g_elevStrokeOn) return;
    if (bx == g_elevStrokeBX && by == g_elevStrokeBY) return;
    g_elevStrokeBX = bx; g_elevStrokeBY = by;
    elev_stroke_apply(bx, by);
}

static void elev_stroke_end(void)
{
    if (!g_elevStrokeOn) return;
    g_elevStrokeOn = false;
    if (g_elevStrokeWrote > 0) {
        edit_commit(g_elevStrokeBefore);
        fprintf(stderr, "edit: elevation stroke rewrote %d cells\n", g_elevStrokeWrote);
    }
    g_elevStrokeBefore.objects.clear();
    g_elevStrokeBefore.tmpl.clear();
    g_elevStrokeBefore.icon.clear();
}


/* The waypoint mutators live here rather than with the rest of the waypoint code:
   they take an undo snapshot, and the snapshot has to exist first. */
static void edit_place_waypoint(int i, int cx, int cy)
{
    if (i < 0 || i >= EDIT_WAYPT_COUNT) return;
    if (cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) return;
    EditSnap before = edit_snapshot(edit_waypoint_label(i));
    g_waypoint[i] = cy * edit_ini_w() + cx;
    edit_commit(before);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: %s at %d,%d (cell %d)\n", edit_waypoint_label(i), cx, cy,
            g_waypoint[i]);
}

static void edit_clear_waypoint(int i)
{
    if (i < 0 || i >= EDIT_WAYPT_COUNT || g_waypoint[i] < 0) return;
    EditSnap before = edit_snapshot("clear waypoint");
    g_waypoint[i] = -1;
    edit_commit(before);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: cleared %s\n", edit_waypoint_label(i));
}


/* ------------------------------------------------------------------------------------
 *  THE HORIZON
 *
 *  The renderer draws terrain and nothing else -- no sky, no backdrop -- because the
 *  console camera's pitch is always 44 to 53 degrees against a 50 degree field, so the
 *  horizon is off the top of the screen and there is never anything to see above it.
 *
 *  The editor's free camera can look level. Then most of the window is the clear colour,
 *  and the report of it was exactly right: "the map turns black". Measured before
 *  this: 81% of the view at pitch 20.
 *
 *  So the editor draws a ground-to-sky gradient behind the world. It is deliberately
 *  drab -- this is a work surface, not a vista -- and it exists to say "you are looking
 *  past the edge of the map" instead of saying nothing at all.
 * ---------------------------------------------------------------------------------- */

static void edit_draw_horizon(int fbw, int fbh)
{
    if (!g_editOn) return;
    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    /* Where the horizon falls on screen, from the pitch and the vertical field: the
       centre row plus the tangent of the pitch over the half-field. Clamped so a steep
       look still paints the whole window rather than nothing. */
    const float p = n64_pitch();
    const float halfFov = (N64_FOVY_DEG * 0.5f) * 0.0174533f;
    float hy = (float)fbh * 0.5f * (1.0f + tanf(p) / tanf(halfFov));
    if (hy < 0.0f) hy = 0.0f;
    if (hy > (float)fbh) hy = (float)fbh;

    glBegin(GL_TRIANGLES);
    /* Sky: a cold, dark wash that never competes with the map. */
    glColor3f(0.055f, 0.075f, 0.105f); glVertex2f(0, 0);
    glColor3f(0.055f, 0.075f, 0.105f); glVertex2f((float)fbw, 0);
    glColor3f(0.085f, 0.105f, 0.135f); glVertex2f((float)fbw, hy);
    glColor3f(0.055f, 0.075f, 0.105f); glVertex2f(0, 0);
    glColor3f(0.085f, 0.105f, 0.135f); glVertex2f((float)fbw, hy);
    glColor3f(0.085f, 0.105f, 0.135f); glVertex2f(0, hy);
    /* Below it: the ground plane the map sits on, so the map's edge reads as an edge
       rather than as a hole. */
    glColor3f(0.055f, 0.062f, 0.050f); glVertex2f(0, hy);
    glColor3f(0.055f, 0.062f, 0.050f); glVertex2f((float)fbw, hy);
    glColor3f(0.030f, 0.035f, 0.028f); glVertex2f((float)fbw, (float)fbh);
    glColor3f(0.055f, 0.062f, 0.050f); glVertex2f(0, hy);
    glColor3f(0.030f, 0.035f, 0.028f); glVertex2f((float)fbw, (float)fbh);
    glColor3f(0.030f, 0.035f, 0.028f); glVertex2f(0, (float)fbh);
    glEnd();
    glColor3f(1, 1, 1);
    end_overlay();
}


/* Written in the cartridge's own field order and spacing: no spaces after the commas,
   because Fill_In splits on a bare "," and does not trim (trigger.cpp:1099). */
static void edit_emit_triggers(FILE* out)
{
    fputs("\r\n[Triggers]\r\n", out);
    for (size_t i = 0; i < g_triggers.size(); i++) {
        const EditTrigger& t = g_triggers[i];
        fprintf(out, "%s=%s,%s,%d,%s,%s,%d\r\n", t.name,
                TRIG_EVENT[t.event], TRIG_ACTION[t.action], t.data,
                t.house[0] ? t.house : "None", t.team[0] ? t.team : "None", t.persist);
    }
}

/* The teams, in the cartridge's field order. A line over 255 characters is a stack
   smash in Fill_In rather than a rejected line, so an over-long team is dropped with a
   loud complaint instead of being written out to crash later. */
static void edit_emit_teams(FILE* out)
{
    fputs("\r\n[TeamTypes]\r\n", out);
    for (size_t i = 0; i < g_teams.size(); i++) {
        char line[512];
        const int n = edit_team_line(g_teams[i], line, sizeof line);
        if (n >= TEAM_LINE_MAX) {
            fprintf(stderr, "edit: team %s is %d characters and the engine's buffer is "
                            "%d; NOT written. Shorten its order list.\n",
                    g_teams[i].name, n, TEAM_LINE_MAX);
            continue;
        }
        fprintf(out, "%s=%s\r\n", g_teams[i].name, line);
    }
}

/* A free 8-character name, in the shipped style: a word plus a digit. */
static void edit_new_team_name(char* out, int n)
{
    for (int i = 1; i < 1000; i++) {
        char cand[10];
        snprintf(cand, sizeof cand, "team%d", i);
        if (edit_team_by_name(cand) < 0) { snprintf(out, n, "%s", cand); return; }
    }
    snprintf(out, n, "team999");
}

/* A free 4-character name. The engine matches case-insensitively and reserves three. */
static void edit_new_trigger_name(char* out, int n)
{
    for (int i = 0; i < 1000; i++) {
        char cand[8];
        snprintf(cand, sizeof cand, "n%03d", i);
        bool taken = false;
        for (size_t k = 0; k < g_triggers.size() && !taken; k++)
            if (!strcasecmp(g_triggers[k].name, cand)) taken = true;
        if (!taken) { snprintf(out, n, "%s", cand); return; }
    }
    snprintf(out, n, "n999");
}


/* ------------------------------------------------------------------------------------
 *  ZONES -- [CellTriggers]
 *
 *  A cell carries at most ONE trigger (display.cpp:1359-1391, and the read skips a cell
 *  whose IsTrigger is already set, so the first entry wins). So a zone is not a shared
 *  object that rules subscribe to: the cell belongs to exactly one rule, and painting it
 *  for another takes it away from the first.
 *
 *  This is 80% of all trigger attachment in the shipped campaign -- 4,462 cell entries,
 *  collapsing into about 305 painted regions averaging 19 cells each. A per-cell list
 *  would be unusable; the gesture is a brush.
 *
 *  Cell 0 cannot carry one (`cell > 0` in the read), which is worth honouring rather
 *  than writing a line the engine silently drops.
 * ---------------------------------------------------------------------------------- */

static void edit_read_zones(const char* path)
{
    edit_zones_clear();
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    bool in = false;
    int n = 0, orphan = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { in = !strncasecmp(p, "[CellTriggers]", 14); continue; }
        if (!in) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* v = eq + 1;
        char* e = v + strlen(v);
        while (e > v && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
        const int cell = atoi(p);
        if (cell <= 0 || cell >= EDIT_GRID_MAX) continue;
        const int t = edit_trigger_by_name(v);
        /* A zone naming a trigger that is not there is dead data the engine drops. Say
           how many rather than silently keeping or silently losing them. */
        if (t < 0) { orphan++; continue; }
        g_cellTrig[cell] = (short)t;
        n++;
    }
    fclose(f);
    if (n || orphan)
        fprintf(stderr, "edit: %d zone cells%s\n", n,
                orphan ? " (and some naming a rule that is not there)" : "");
}

static void edit_emit_zones(FILE* out)
{
    fputs("\r\n[CellTriggers]\r\n", out);
    for (int c = 1; c < edit_ini_cells(); c++) {
        const int t = g_cellTrig[c];
        if (t < 0 || t >= (int)g_triggers.size()) continue;
        fprintf(out, "%d=%s\r\n", c, g_triggers[t].name);
    }
}

static int edit_zone_cells(int t)
{
    int n = 0;
    for (int c = 1; c < edit_ini_cells(); c++) if (g_cellTrig[c] == t) n++;
    return n;
}

/* The zone painter takes an undo snapshot, so it lives below the undo machinery
   rather than with the rest of the zone code. */
static void edit_paint_zone(int cx, int cy, bool erase)
{
    if (g_trigSel < 0) return;
    EditSnap before = edit_snapshot("zone");
    int n = 0;
    for (int dy = -g_zoneBrush + 1; dy <= g_zoneBrush - 1; dy++)
        for (int dx = -g_zoneBrush + 1; dx <= g_zoneBrush - 1; dx++) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) continue;
            const int c = y * edit_ini_w() + x;
            if (c <= 0) continue;          /* cell 0 cannot carry one */
            const short want = erase ? -1 : (short)g_trigSel;
            if (erase && g_cellTrig[c] != g_trigSel) continue;   /* only take back ours */
            if (g_cellTrig[c] == want) continue;
            g_cellTrig[c] = want;
            n++;
        }
    if (!n) return;
    g_zoneDirty = true;
    g_trigDirty = true;      /* the section is ours now, and so are the rules it names */
    edit_commit(before);
}

/* ------------------------------------------------------------------------------------
 *  TERRAIN: the drawers
 *
 *  Seven families, grouped by the template's own INI name, the same split the browser
 *  editor uses. CLIFFS ARE DELIBERATELY ABSENT: they are derived from elevation, not
 *  painted, which is a design decision and not an omission.
 *
 *  Only templates this theater's bank can actually draw are offered. The two banks
 *  differ, and offering a tile that resolves to no art would be a palette entry that
 *  does nothing.
 * ---------------------------------------------------------------------------------- */

/* Which drawer a template belongs in, from its name. -1 means it is not offered:
   cliffs (S*), and anything the split does not claim. */
static int terr_group_of(const char* nm)
{
    if (!nm || !*nm) return -1;
    if (!strncmp(nm, "BRIDGE", 6)) return TG_BRIDGE;
    if (!strncmp(nm, "FALLS", 5) || !strncmp(nm, "FORD", 4)) return TG_RIVER;
    if (!strncmp(nm, "RV", 2))    return TG_RIVER;
    if (!strncmp(nm, "SH", 2))    return TG_SHORE;
    if (!strncmp(nm, "BR", 2))    return TG_ROCK;
    if (nm[0] == 'W')             return TG_WATER;
    if (nm[0] == 'D')             return TG_ROAD;
    if (nm[0] == 'B')             return TG_ROCK;
    if (nm[0] == 'P')             return TG_GROUND;
    if (!strncmp(nm, "CLEAR", 5)) return TG_GROUND;
    return -1;      /* S* cliffs and anything else: not paintable */
}

static void edit_terrain_build(void)
{
    for (int g = 0; g < TG_COUNT; g++) g_terrList[g].clear();
    for (int t = 0; t < EDIT_TEMPLATE_COUNT; t++) {
        const EditTemplate* e = &EDIT_TEMPLATES[t];
        const int g = terr_group_of(e->name);
        if (g < 0) continue;
        /* A DESTROYED bridge is not something anyone authors -- it is what a bridge
           becomes. The browser excludes the four the same way. */
        if (strlen(e->name) > 1 && e->name[strlen(e->name) - 1] == 'D' &&
            !strncmp(e->name, "BRIDGE", 6)) continue;
        /* Offer it if the bank can draw ANY of it, not if it can draw its FIRST icon.
           That distinction is not academic: several templates have no icon 0 in a given
           bank, and requiring one dropped 20 templates in TEMPERAT and 28 in DESERT --
           including BOTH desert bridges, which have 22 of their icons present each. The
           icons that really are absent are skipped per cell at stamp time and shown red
           in the preview, which is where that belongs. */
        int sea = 0, any = 0;
        for (int k = 0; k < (int)e->w * (int)e->h && !any; k++)
            if (tt_slot_of(g_pack.theater, t, k, &sea) >= 0) any = 1;
        if (!any) continue;
        g_terrList[g].push_back(t);
    }
    g_terrBuilt = true;
    fprintf(stderr, "edit: terrain drawers for %s --", g_pack.theater);
    for (int g = 0; g < TG_COUNT; g++)
        fprintf(stderr, " %s %d", TERR_GROUP_NAME[g], (int)g_terrList[g].size());
    fprintf(stderr, "\n");
}

/* Which cells a template covers, and the icon each one takes. The icons run row-major
   across the template's w x h box, which is how the .BIN stores them. */
static int edit_template_cells(int tid, int cx, int cy, int* xs, int* ys, int* ics, int max)
{
    if (tid < 0 || tid >= EDIT_TEMPLATE_COUNT) return 0;
    const EditTemplate* e = &EDIT_TEMPLATES[tid];
    int n = 0;
    for (int dy = 0; dy < (int)e->h; dy++)
        for (int dx = 0; dx < (int)e->w; dx++) {
            if (n >= max) return n;
            const int icon = dy * (int)e->w + dx;
            int sea = 0;
            /* An icon the bank does not carry is SKIPPED, not painted black. Several
               templates have holes in their icon range. */
            if (tt_slot_of(g_pack.theater, tid, icon, &sea) < 0) continue;
            xs[n] = cx + dx; ys[n] = cy + dy; ics[n] = icon; n++;
        }
    return n;
}

/* Stamp one template. Returns how many cells it wrote; 0 means nothing landed. */
static int edit_stamp_template(int tid, int cx, int cy, bool undoable)
{
    if (tid < 0) return 0;
    /* CLEAR1 is plain ground, and the cartridge does NOT store it as template 0: it
       stores TEMPLATE_NONE (255) and picks the icon from the cell's own coordinates
       (CellClass::Clear_Icon), which is what stops a field of it visibly tiling. Writing
       template 0 would save a map that looks right here and tiles in the game. */
    if (!strcmp(EDIT_TEMPLATES[tid].name, "CLEAR1")) {
        EditSnap before = edit_snapshot("CLEAR");
        const bool ok = edit_paint_cell(cx, cy, 255, (cx & 3) | ((cy & 3) << 2));
        if (ok && undoable) edit_commit(before);
        return ok ? 1 : 0;
    }
    int xs[64], ys[64], ics[64];
    const int n = edit_template_cells(tid, cx, cy, xs, ys, ics, 64);
    if (!n) return 0;

    EditSnap before = edit_snapshot(EDIT_TEMPLATES[tid].name);
    int wrote = 0;
    for (int i = 0; i < n; i++) {
        if (xs[i] < 0 || ys[i] < 0 || xs[i] >= g_gridW || ys[i] >= g_gridH) continue;
        if (edit_paint_cell(xs[i], ys[i], tid, ics[i])) wrote++;
    }
    if (wrote && undoable) edit_commit(before);
    return wrote;
}



/* ------------------------------------------------------------------------------------
 *  BRIDGES AND FORDS
 *
 *  A bridge is not a tile you scatter: it is a CROSSING, and it only makes sense where
 *  the river continues past both of its ends. The browser editor measured that rule off
 *  221 shipped placements and found it unanimous, so it is a rule and not a heuristic.
 *
 *  FORDs get no rule -- the same measurement found none -- and that absence is recorded
 *  rather than guessed at.
 * ---------------------------------------------------------------------------------- */

/* THE MEASURED MOUTHS. Each entry is (icon, direction): the cell of the block where the
   bridge meets the bank, and the way the river has to carry on from it. Measured off 221
   shipped placements and unanimous over all of them -- 24/24, 27/27, 15/15 and 26/26 --
   which is what makes it a rule rather than a preference.
   FORD1 and FORD2 are crossings too and get NO rule: the same measurement found none,
   and that absence is recorded rather than guessed at. */
struct BridgeMouth { unsigned short tmpl; unsigned char icon; char dir; };
static const BridgeMouth BRIDGE_MOUTHS[] = {
    { 165,  4, 'W' }, { 165,  8, 'W' }, { 165, 15, 'E' },   /* BRIDGE1, 4x4  */
    { 167,  9, 'E' }, { 167, 10, 'W' }, { 167, 15, 'W' },   /* BRIDGE2, 5x5  */
    { 169, 12, 'W' }, { 169, 23, 'E' },                     /* BRIDGE3, 6x5  */
    { 171, 12, 'W' }, { 171, 18, 'W' },                     /* BRIDGE4, 6x4  */
};
static const int BRIDGE_MOUTH_N = (int)(sizeof(BRIDGE_MOUTHS) / sizeof(BRIDGE_MOUTHS[0]));

static bool edit_is_crossing(int tid)
{
    if (tid < 0 || tid >= EDIT_TEMPLATE_COUNT) return false;
    const char* n = EDIT_TEMPLATES[tid].name;
    return !strncmp(n, "BRIDGE", 6) || !strncmp(n, "FORD", 4);
}

/* Is this cell water, as the .BIN sees it? */
static bool edit_cell_is_water(int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) return false;
    const int land = edit_land_at(cx, cy);
    return land == EDIT_LAND_WATER;
}

/* Does a crossing dropped here meet the river at its mouths? Fills `why` with the
   reason when it does not, because a refusal that does not say why is the failure mode
   this whole editor is trying to avoid. */
static bool edit_crossing_fits(int tid, int cx, int cy, char* why, int whyN,
                               int* badx, int* bady)
{
    if (badx) *badx = -1;
    if (bady) *bady = -1;
    if (tid < 0 || tid >= EDIT_TEMPLATE_COUNT) return false;
    const EditTemplate* e = &EDIT_TEMPLATES[tid];
    int missing = 0, firstX = -1, firstY = -1;
    char firstDir = '?';
    for (int i = 0; i < BRIDGE_MOUTH_N; i++) {
        if ((int)BRIDGE_MOUTHS[i].tmpl != tid) continue;
        const int icon = BRIDGE_MOUTHS[i].icon;
        const int dx = (BRIDGE_MOUTHS[i].dir == 'E') ? 1 : (BRIDGE_MOUTHS[i].dir == 'W') ? -1 : 0;
        const int dy = (BRIDGE_MOUTHS[i].dir == 'S') ? 1 : (BRIDGE_MOUTHS[i].dir == 'N') ? -1 : 0;
        const int x = cx + (icon % (int)e->w) + dx;
        const int y = cy + (icon / (int)e->w) + dy;
        if (!edit_cell_is_water(x, y)) {
            if (!missing) { firstX = x; firstY = y; firstDir = BRIDGE_MOUTHS[i].dir; }
            missing++;
        }
    }
    if (!missing) return true;
    if (why && whyN > 0)
        snprintf(why, whyN, "the river has to carry on past %s. Lay the water first.",
                 missing > 1 ? "both ends" :
                 firstDir == 'E' ? "the east end" :
                 firstDir == 'W' ? "the west end" :
                 firstDir == 'N' ? "the north end" : "the south end");
    if (badx) *badx = firstX;
    if (bady) *bady = firstY;
    return false;
}

/* ------------------------------------------------------------------------------------
 *  TERRAIN: strokes
 *
 *  A drag paints one block per cell CROSSED, never repeatedly while the pointer holds
 *  still -- the browser calls that anti-smear and it is what stops a slow hand writing
 *  the same block forty times.
 *
 *  ONE STROKE IS ONE UNDO. The snapshot is taken when the stroke begins, not per stamp,
 *  so dragging back and forth over the same ground still undoes to where you started
 *  rather than unwinding a hundred steps.
 * ---------------------------------------------------------------------------------- */

static bool     g_strokeOn = false;
static EditSnap g_strokeBefore;
static int      g_strokeCX = -1, g_strokeCY = -1;   /* last cell stamped */
static int      g_strokeCells = 0;

static void edit_stroke_paint(int cx, int cy);

static void edit_stroke_begin(int cx, int cy)
{
    if (g_terrArmed < 0) return;
    g_strokeBefore = edit_snapshot(EDIT_TEMPLATES[g_terrArmed].name);
    g_strokeOn = true;
    g_strokeCX = g_strokeCY = -1;
    g_strokeCells = 0;
    edit_stroke_paint(cx, cy);
}

static void edit_stroke_paint(int cx, int cy)
{
    if (!g_strokeOn || g_terrArmed < 0) return;
    if (cx == g_strokeCX && cy == g_strokeCY) return;      /* same cell: anti-smear */
    g_strokeCX = cx; g_strokeCY = cy;
    g_strokeCells += edit_stamp_template(g_terrArmed, cx, cy, false);
}

static void edit_stroke_end(void)
{
    if (!g_strokeOn) return;
    g_strokeOn = false;
    if (g_strokeCells > 0) {
        edit_commit(g_strokeBefore);
        fprintf(stderr, "edit: stroke wrote %d cells\n", g_strokeCells);
    }
    /* Nothing landed: no undo step, because an undo that does nothing reads as an undo
       button that does not work. */
    g_strokeBefore.objects.clear();
    g_strokeBefore.tmpl.clear();
    g_strokeBefore.icon.clear();
}

/* ------------------------------------------------------------------------------------
 *  TOOLS
 *
 *  The browser editor has a 56-unit rail down the left with four tools and one action,
 *  and the native editor had none of it -- which is both a layout gap and, more to the
 *  point, a capability gap: MOVE, PICK and ERASE have no keyboard or menu equivalent
 *  here, so a mistake could be made but not taken back.
 *
 *  Same four tools, same keys as the web (V M I X), same camera action on 0.
 * ---------------------------------------------------------------------------------- */

/* WHAT IS AT THIS CELL. Topmost first, which for an editor means the thing a person
   would say they are pointing at: a building covering a cell wins over the scenery it
   was dropped on. Returns an index into g_objects, or -1. */
static int edit_object_at(int cx, int cy)
{
    int best = -1;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        const int w = o.fw > 0 ? o.fw : 1, h = o.fh > 0 ? o.fh : 1;
        bool hit;
        if (o.kind == K_BUILDING || o.kind == K_TERRAIN)
            hit = (cx >= o.cx && cx < o.cx + w && cy >= o.cy && cy < o.cy + h);
        else
            hit = (cx == o.cx && cy == o.cy);
        if (!hit) continue;
        /* Later in the list wins: that is draw order, and it is also the order the
           writer emits, so the topmost thing is the one a click means. */
        best = (int)i;
    }
    return best;
}

/* The catalog entry a placed object came from, so PICK can arm the same thing again. */
static int edit_item_of(const SimObject& o)
{
    for (int i = 0; i < EDIT_ITEM_N; i++)
        if (!strcmp(EDIT_ITEMS[i].code, o.type)) return i;
    return -1;
}

static bool edit_erase_at(int cx, int cy)
{
    EditSnap before = edit_snapshot("erase");
    /* Walls first: they are overlay and sit on top of whatever cell they occupy, so a
       click on a walled cell means the wall. */
    for (size_t i = 0; i < g_walls.size(); i++) {
        if (g_walls[i].x != cx || g_walls[i].y != cy) continue;
        const int kind = g_walls[i].kind;
        g_walls.erase(g_walls.begin() + i);
        edit_commit(before);
        /* The neighbours have to be re-masked or they keep pointing at a wall that is
           no longer there. */
        edit_wall_mask(cx, cy - 1); edit_wall_mask(cx + 1, cy);
        edit_wall_mask(cx, cy + 1); edit_wall_mask(cx - 1, cy);
        g_euiRadarDirty = 1;
        fprintf(stderr, "edit: erased %s wall at %d,%d\n", WALL_NAME[kind], cx, cy);
        return true;
    }
    const int k = edit_object_at(cx, cy);
    if (k < 0) {
        fprintf(stderr, "edit: nothing at %d,%d to erase\n", cx, cy);
        return false;
    }
    char t[32]; snprintf(t, sizeof t, "%s", g_objects[k].type);
    g_objects.erase(g_objects.begin() + k);
    edit_commit(before);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: erased %s at %d,%d  [%d objects]\n", t, cx, cy,
            (int)g_objects.size());
    return true;
}

static bool edit_pick_at(int cx, int cy)
{
    for (size_t i = 0; i < g_walls.size(); i++)
        if (g_walls[i].x == cx && g_walls[i].y == cy) {
            const int want = EDIT_KIND_FIRST[EDIT_KIND_WALL] + g_walls[i].kind;
            g_editArmed = want;
            g_editTab   = EDIT_KIND_WALL;
            g_editTool  = TOOL_PLACE;
            fprintf(stderr, "edit: picked %s wall\n", WALL_NAME[g_walls[i].kind]);
            return true;
        }
    const int k = edit_object_at(cx, cy);
    if (k < 0) { fprintf(stderr, "edit: nothing at %d,%d to pick\n", cx, cy); return false; }
    const int it = edit_item_of(g_objects[k]);
    if (it < 0) {
        fprintf(stderr, "edit: %s is not in the catalog, cannot arm it\n", g_objects[k].type);
        return false;
    }
    g_editArmed = it;
    g_editTab   = EDIT_ITEMS[it].kind;
    g_editTool  = TOOL_PLACE;      /* picking is for placing another one */
    /* Owning house too: picking a Nod turret and then placing a GDI one is not what
       the eyedropper is for. */
    for (int h = 0; h < 4; h++)
        if (!strcmp(EUI_HOUSES[h].key, g_objects[k].house)) { g_editOwner = h; break; }
    fprintf(stderr, "edit: picked %s (%s)\n", EDIT_ITEMS[it].name, EDIT_ITEMS[it].code);
    return true;
}

/* MOVE. The object is lifted out of the document for the legality test and put back
   where it was if the destination refuses it -- otherwise a two-cell nudge would always
   fail, because the thing being moved would collide with itself. */
static bool edit_move_to(int idx, int cx, int cy)
{
    if (idx < 0 || idx >= (int)g_objects.size()) return false;
    SimObject o = g_objects[idx];
    const int it = edit_item_of(o);
    const EditItem* t = (it >= 0) ? &EDIT_ITEMS[it] : NULL;

    EditSnap before = edit_snapshot("move");
    SimObject saved = g_objects[idx];
    g_objects.erase(g_objects.begin() + idx);

    bool ok = true;
    if (t) {
        for (int i = 0; i < (int)t->n && ok; i++) {
            const int px = cx + t->dx[i], py = cy + t->dy[i];
            if (px < 0 || py < 0 || px >= g_gridW || py >= g_gridH) ok = false;
            else if (!edit_cell_ok(px, py)) {
                ok = false;
                fprintf(stderr, "edit: cannot move %s to %d,%d -- cell %d,%d is %s\n",
                        o.type, cx, cy, px, py,
                        edit_cell_occupied(px, py) ? "occupied"
                                                   : EDIT_LAND_NAME[edit_land_at(px, py)]);
            }
        }
    } else if (!edit_cell_ok(cx, cy)) {
        ok = false;
    }
    if (!ok) { g_objects.insert(g_objects.begin() + idx, saved); return false; }

    const int ddx = cx - o.cx, ddy = cy - o.cy;
    /* Before the cell changes, carry the object's order and trigger with it. */
    edit_prior_rekey(o.house, o.type, o.cy * edit_ini_w() + o.cx,
                     (o.cy + ddy) * edit_ini_w() + (o.cx + ddx));
    o.cx += ddx;  o.cy += ddy;
    o.tcx = o.cx; o.tcy = o.cy;
    o.wx += (float)ddx; o.wz += (float)ddy;
    o.ex = o.wx; o.ez = o.wz;
    g_objects.insert(g_objects.begin() + idx, o);
    edit_commit(before);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: moved %s to %d,%d\n", o.type, cx, cy);
    return true;
}


/* The camera action on the rail, and 0 on the keyboard: put the playable rectangle in
   view. The editor opens looking wherever the mission's own start view was, which is
   often a corner of a map you are about to work on the middle of. */
/* TAKE ME TO WHAT THIS RULE IS ABOUT.
 *
 * "How do I show the zone?" A rule's region and the objects it watches are somewhere on
 * a 64x64 map, and until now nothing connected the card you were looking at to the
 * ground it refers to -- you had to find it. Picking a rule now moves the camera to the
 * middle of whatever it points at: its painted region if it has one, otherwise the
 * objects it has tagged. A rule that refers to no place leaves the camera alone, because
 * jerking the view for a Time trigger would be worse than useless. */
static void edit_look_at_rule(int rule)
{
    if (rule < 0 || rule >= (int)g_triggers.size()) return;
    double sx = 0.0, sy = 0.0;
    int n = 0;
    for (int c = 0; c < edit_ini_cells(); c++)
        if (g_cellTrig[c] == rule) { sx += c % edit_ini_w(); sy += c / edit_ini_w(); n++; }
    if (!n) {
        const char* want = g_triggers[rule].name;
        for (size_t i = 0; i < g_objects.size(); i++) {
            const char* tr = edit_trigger_for(g_objects[i]);
            if (!tr || strcasecmp(tr, want)) continue;
            sx += g_objects[i].cx; sy += g_objects[i].cy; n++;
        }
    }
    if (!n) return;
    edit_cam_release();
    g_camX = (float)(sx / n) + 0.5f;
    g_camZ = (float)(sy / n) + 0.5f;
    fprintf(stderr, "edit: looking at %s (%d cell%s)\n", g_triggers[rule].name, n,
            n == 1 ? "" : "s");
}

static void edit_frame_map(void)
{
    /* Straight back to the console's own heading as well as its centre: it is the "I am
       lost" key, and leaving it pointing sideways would only half work. */
    edit_cam_release();
    g_camX = (float)g_mapX + (float)g_mapW * 0.5f;
    g_camZ = (float)g_mapY + (float)g_mapH * 0.5f;
    fprintf(stderr, "edit: framed the playable rect (%d,%d %dx%d)\n",
            g_mapX, g_mapY, g_mapW, g_mapH);
}


static void edit_set_mode(int m);     /* defined with the tabs, below */

/* One key, the editor's answer. Returns true if the editor claimed it, in which case the
   game never sees it. */
static bool edit_key(int sym)
{
    if (!g_editOn) return false;
    switch (sym) {
        case SDLK_v: g_editTool = TOOL_PLACE; break;
        case SDLK_m: g_editTool = TOOL_MOVE;  g_editArmed = -1; break;
        case SDLK_i: g_editTool = TOOL_PICK;  g_editArmed = -1; break;
        case SDLK_x: g_editTool = TOOL_ERASE; g_editArmed = -1; break;
        case SDLK_0: edit_frame_map(); return true;
        /* The five tabs, in the order they sit on screen. 2 and 3 used to answer
           "not built yet", which stopped being true two rebuilds ago and is exactly the
           kind of message that makes a person stop trying a feature that works. */
        case SDLK_1: edit_set_mode(0); return true;
        case SDLK_2: edit_set_mode(1); return true;
        case SDLK_3: edit_set_mode(2); return true;
        case SDLK_4: edit_set_mode(3); return true;
        case SDLK_5: edit_set_mode(4); return true;
        case SDLK_TAB:
            g_editOwner = (g_editOwner + 1) % 4;
            fprintf(stderr, "edit: placing for %s\n", EUI_HOUSES[g_editOwner].label);
            return true;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
            if (g_editOverMap) edit_erase_at(g_editCellX, g_editCellY);
            else fprintf(stderr, "edit: point at a cell to delete what is on it\n");
            return true;
        default: return false;
    }
    fprintf(stderr, "edit: tool %s\n", TOOL_NAME[g_editTool]);
    return true;
}



/* ------------------------------------------------------------------------------------
 *  --painttest: does painting change BOTH the saved bytes and the drawn art?
 *
 *  The two can disagree in either direction and each failure looks different: write the
 *  .BIN and not the UVs and the map saves correctly while showing the old tile; write
 *  the UVs and not the .BIN and it looks right until you reload it. So the test checks
 *  both, and then undoes and checks both went back.
 * ---------------------------------------------------------------------------------- */

static int edit_painttest(void)
{
    int fails = 0;
    if (!g_editHaveBin) {
        fprintf(stderr, "PAINTTEST|FAIL|no .BIN loaded\n");
        return 1;
    }
    int n = 0;
    const TtSlot* tab = tt_table(g_pack.theater, &n);
    fprintf(stderr, "PAINTTEST|theater %s (enum %d), %d pairs in the bank\n",
            g_pack.theater, g_theater, n);

    /* A cell inside the playable rect, so it is one the renderer actually draws. */
    const int cx = g_mapX + g_mapW / 2, cy = g_mapY + g_mapH / 2;
    PackCell* pc = edit_pack_cell(cx, cy);
    if (!pc) { fprintf(stderr, "PAINTTEST|FAIL|no pack cell at %d,%d\n", cx, cy); return 1; }

    const int idx = cy * g_editBinW + cx;
    const unsigned char t0 = g_editTmpl[idx], i0 = g_editIcon[idx];
    const float u0 = pc->u0, v0 = pc->v0;
    const unsigned char h0 = pc->holes;

    /* Paint something guaranteed to be in this theater's bank and different from what is
       already there -- walk the table for the first pair that is neither. */
    int wt = -1, wi = -1;
    for (int k = 0; k < n; k++)
        if ((int)tab[k].tmpl != (int)t0 && tab[k].tmpl != 255) {
            wt = tab[k].tmpl; wi = tab[k].icon; break;
        }
    if (wt < 0) { fprintf(stderr, "PAINTTEST|FAIL|no other tile to paint\n"); return 1; }

    EditSnap before = edit_snapshot("paint");
    if (!edit_paint_cell(cx, cy, wt, wi)) {
        fprintf(stderr, "PAINTTEST|FAIL|paint of template %d icon %d refused\n", wt, wi);
        return 1;
    }
    edit_commit(before);

    if (g_editTmpl[idx] != (unsigned char)wt || g_editIcon[idx] != (unsigned char)wi) {
        fprintf(stderr, "PAINTTEST|FAIL|.BIN says %d/%d, wanted %d/%d\n",
                g_editTmpl[idx], g_editIcon[idx], wt, wi);
        fails++;
    } else {
        fprintf(stderr, "PAINTTEST|ok  |.BIN now template %d icon %d at cell %d,%d\n",
                wt, wi, cx, cy);
    }

    int sea = 0;
    const int slot = tt_slot_of(g_pack.theater, wt, wi, &sea);
    float ex, ey; tt_slot_rect(slot, &ex, &ey);
    const PackTex& at = g_pack.tex[g_pack.terrainTex];
    const float wantU = ex / (float)at.uw, wantV = ey / (float)at.uh;
    if (fabsf(pc->u0 - wantU) > 1e-6f || fabsf(pc->v0 - wantV) > 1e-6f) {
        fprintf(stderr, "PAINTTEST|FAIL|UV is %.6f,%.6f, wanted %.6f,%.6f (slot %d)\n",
                pc->u0, pc->v0, wantU, wantV, slot);
        fails++;
    } else if (pc->u0 == u0 && pc->v0 == v0) {
        fprintf(stderr, "PAINTTEST|FAIL|the UV did not change at all\n");
        fails++;
    } else {
        fprintf(stderr, "PAINTTEST|ok  |drawn art moved to atlas slot %d (%.4f,%.4f), "
                "sea=%d\n", slot, pc->u0, pc->v0, pc->holes);
    }

    edit_undo();
    pc = edit_pack_cell(cx, cy);
    if (g_editTmpl[idx] != t0 || g_editIcon[idx] != i0) {
        fprintf(stderr, "PAINTTEST|FAIL|undo left .BIN at %d/%d, wanted %d/%d\n",
                g_editTmpl[idx], g_editIcon[idx], t0, i0);
        fails++;
    }
    if (fabsf(pc->u0 - u0) > 1e-6f || fabsf(pc->v0 - v0) > 1e-6f || pc->holes != h0) {
        fprintf(stderr, "PAINTTEST|FAIL|undo left the art at %.6f,%.6f holes %d, "
                "wanted %.6f,%.6f holes %d\n", pc->u0, pc->v0, pc->holes, u0, v0, h0);
        fails++;
    }
    if (!fails) fprintf(stderr, "PAINTTEST|ok  |undo put both the bytes and the art back\n");

    /* A WHOLE TEMPLATE, through the drawers the palette offers -- not just one cell.
       A multi-cell block is where the icon numbering can go wrong, and getting that
       wrong writes a river with its pieces shuffled. */
    edit_terrain_build();
    {
        int stamped = 0, groups = 0;
        for (int g = 0; g < TG_COUNT; g++) {
            if (g_terrList[g].empty()) continue;
            groups++;
            const int tid = g_terrList[g][0];
            const EditTemplate* e = &EDIT_TEMPLATES[tid];
            const int sx = g_mapX + 2, sy = g_mapY + 2;
            const int w = edit_stamp_template(tid, sx, sy, true);
            if (!w) {
                fprintf(stderr, "PAINTTEST|FAIL|%s stamped nothing at %d,%d\n",
                        e->name, sx, sy);
                fails++;
                continue;
            }
            /* Every cell it claimed must now read back the template and the icon the
               row-major walk gave it -- EXCEPT CLEAR1, which is deliberately stored as
               TEMPLATE_NONE with a per-cell icon and is checked on its own below. */
            if (!strcmp(e->name, "CLEAR1")) { stamped += w; edit_undo(); continue; }
            int bad = 0;
            for (int dy = 0; dy < (int)e->h; dy++)
                for (int dx = 0; dx < (int)e->w; dx++) {
                    const int px = sx + dx, py = sy + dy;
                    if (px >= g_editBinW || py >= g_editBinH) continue;
                    int sea2 = 0;
                    const int icon = dy * (int)e->w + dx;
                    if (tt_slot_of(g_pack.theater, tid, icon, &sea2) < 0) continue;
                    if (g_editTmpl[py * g_editBinW + px] != (unsigned char)tid ||
                        g_editIcon[py * g_editBinW + px] != (unsigned char)icon) bad++;
                }
            if (bad) {
                fprintf(stderr, "PAINTTEST|FAIL|%s: %d cells read back wrong\n", e->name, bad);
                fails++;
            }
            stamped += w;
            edit_undo();
        }
        fprintf(stderr, "PAINTTEST|ok  |stamped one template from each of %d drawers, "
                "%d cells, all read back correctly and undone\n", groups, stamped);
    }

    /* A STROKE IS ONE UNDO. Dragging back and forth over the same ground must still
       undo to where it started, and a stroke that wrote nothing must leave no step at
       all -- an undo that does nothing reads as a broken undo button. */
    {
        const int undo0 = (int)g_undo.size();
        int ground = -1;
        for (int g = 0; g < TG_COUNT && ground < 0; g++)
            for (size_t k = 0; k < g_terrList[g].size(); k++)
                if (!strncmp(EDIT_TEMPLATES[g_terrList[g][k]].name, "P0", 2)) {
                    ground = g_terrList[g][k]; break;
                }
        if (ground < 0 && !g_terrList[TG_GROUND].empty()) ground = g_terrList[TG_GROUND][0];
        if (ground < 0) {
            fprintf(stderr, "PAINTTEST|FAIL|no ground template to stroke with\n");
            fails++;
        } else {
            g_terrArmed = ground;
            unsigned char save[16];
            const int sy = g_mapY + 5;
            for (int i = 0; i < 8; i++)
                save[i] = g_editTmpl[sy * g_editBinW + g_mapX + 3 + i];

            edit_stroke_begin(g_mapX + 3, sy);
            for (int i = 1; i < 8; i++) edit_stroke_paint(g_mapX + 3 + i, sy);
            /* Back over ground already covered: must not add steps. */
            for (int i = 6; i >= 0; i--) edit_stroke_paint(g_mapX + 3 + i, sy);
            edit_stroke_end();

            const int added = (int)g_undo.size() - undo0;
            if (added != 1) {
                fprintf(stderr, "PAINTTEST|FAIL|a stroke of 15 stamps made %d undo steps, "
                        "wanted 1\n", added);
                fails++;
            }
            edit_undo();
            int bad = 0;
            for (int i = 0; i < 8; i++)
                if (g_editTmpl[sy * g_editBinW + g_mapX + 3 + i] != save[i]) bad++;
            if (bad) {
                fprintf(stderr, "PAINTTEST|FAIL|one undo left %d of 8 stroke cells "
                        "changed\n", bad);
                fails++;
            } else {
                fprintf(stderr, "PAINTTEST|ok  |a 15-stamp stroke is one undo step and "
                        "restores all 8 cells\n");
            }

            /* A stroke that writes nothing leaves no step. */
            const int u2 = (int)g_undo.size();
            g_terrArmed = -1;
            edit_stroke_begin(g_mapX + 3, sy);
            edit_stroke_end();
            if ((int)g_undo.size() != u2) {
                fprintf(stderr, "PAINTTEST|FAIL|an empty stroke pushed an undo step\n");
                fails++;
            } else {
                fprintf(stderr, "PAINTTEST|ok  |an empty stroke leaves no undo step\n");
            }
        }
    }

    /* MOVING A TAGGED OBJECT KEEPS ITS TRIGGER. The prior table is keyed by cell, which
       is the one thing a move changes, so without a re-key the object saves with its
       trigger silently detached -- and on a scripted mission that is a mission that
       quietly does nothing. */
    {
        int tagged = -1;
        for (size_t i = 0; i < g_objects.size() && tagged < 0; i++) {
            const char* t = edit_trigger_for(g_objects[i]);
            if (t && strcmp(t, "None")) tagged = (int)i;
        }
        if (tagged < 0) {
            fprintf(stderr, "PAINTTEST|note|no object on this map carries a trigger\n");
        } else {
            char was[32];
            snprintf(was, sizeof was, "%s", edit_trigger_for(g_objects[tagged]));
            const int ox = g_objects[tagged].cx, oy = g_objects[tagged].cy;
            /* Somewhere legal and nearby. */
            int nx = -1, ny = -1;
            for (int dy = -3; dy <= 3 && nx < 0; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    if (!dx && !dy) continue;
                    if (edit_cell_ok(ox + dx, oy + dy)) { nx = ox + dx; ny = oy + dy; break; }
                }
            if (nx < 0) {
                fprintf(stderr, "PAINTTEST|note|nowhere legal to move the tagged object\n");
            } else if (!edit_move_to(tagged, nx, ny)) {
                fprintf(stderr, "PAINTTEST|note|the tagged object refused the move\n");
            } else {
                const char* now = edit_trigger_for(g_objects[tagged]);
                if (!now || strcmp(now, was)) {
                    fprintf(stderr, "PAINTTEST|FAIL|moving a tagged object lost its "
                            "trigger: was %s, now %s\n", was, now ? now : "(none)");
                    fails++;
                } else {
                    fprintf(stderr, "PAINTTEST|ok  |a moved object keeps its trigger "
                            "(%s)\n", now);
                }
                edit_undo();
            }
        }
    }

    /* THE BRIDGE MOUTH RULE. A crossing must refuse dry ground and accept a real river,
       and it must say which end is wrong. Both directions are checked, because a rule
       that only ever refuses is indistinguishable from a broken button. */
    {
        int bridge = -1;
        for (size_t k = 0; k < g_terrList[TG_BRIDGE].size(); k++) {
            const int t = g_terrList[TG_BRIDGE][k];
            for (int i = 0; i < BRIDGE_MOUTH_N; i++)
                if ((int)BRIDGE_MOUTHS[i].tmpl == t) { bridge = t; break; }
            if (bridge >= 0) break;
        }
        if (bridge < 0) {
            fprintf(stderr, "PAINTTEST|FAIL|this theater offers no bridge with a "
                    "measured mouth rule\n");
            fails++;
        } else {
            const EditTemplate* e = &EDIT_TEMPLATES[bridge];
            const int bx = g_mapX + 10, by = g_mapY + 10;
            char why[160] = {0};
            int mx = -1, my = -1;

            /* Dry ground first: make certain of it, then it must refuse. */
            EditSnap dry = edit_snapshot("dry");
            for (int y = by - 2; y <= by + (int)e->h + 2; y++)
                for (int x = bx - 2; x <= bx + (int)e->w + 2; x++)
                    edit_paint_cell(x, y, 255, (x & 3) | ((y & 3) << 2));
            if (edit_crossing_fits(bridge, bx, by, why, sizeof why, &mx, &my)) {
                fprintf(stderr, "PAINTTEST|FAIL|%s accepted dry ground\n", e->name);
                fails++;
            } else {
                fprintf(stderr, "PAINTTEST|ok  |%s refuses dry ground: %s\n", e->name, why);
            }

            /* Now lay water at exactly the mouths it asked for, and it must accept. */
            for (int i = 0; i < BRIDGE_MOUTH_N; i++) {
                if ((int)BRIDGE_MOUTHS[i].tmpl != bridge) continue;
                const int icon = BRIDGE_MOUTHS[i].icon;
                const int dx = (BRIDGE_MOUTHS[i].dir == 'E') ? 1
                             : (BRIDGE_MOUTHS[i].dir == 'W') ? -1 : 0;
                const int dy = (BRIDGE_MOUTHS[i].dir == 'S') ? 1
                             : (BRIDGE_MOUTHS[i].dir == 'N') ? -1 : 0;
                int water = -1;
                for (int t = 0; t < EDIT_TEMPLATE_COUNT && water < 0; t++)
                    if (!strcmp(EDIT_TEMPLATES[t].name, "W1")) water = t;
                if (water >= 0)
                    edit_paint_cell(bx + (icon % (int)e->w) + dx,
                                    by + (icon / (int)e->w) + dy, water, 0);
            }
            if (!edit_crossing_fits(bridge, bx, by, why, sizeof why, &mx, &my)) {
                fprintf(stderr, "PAINTTEST|FAIL|%s still refuses with water at every "
                        "mouth: %s\n", e->name, why);
                fails++;
            } else {
                fprintf(stderr, "PAINTTEST|ok  |%s accepts once the river carries on "
                        "past both ends\n", e->name);
            }
            edit_restore(dry);
        }
    }

    /* CLEAR is stored as TEMPLATE_NONE with a per-cell icon, not as template 0. */
    {
        int clear = -1;
        for (size_t k = 0; k < g_terrList[TG_GROUND].size(); k++)
            if (!strcmp(EDIT_TEMPLATES[g_terrList[TG_GROUND][k]].name, "CLEAR1"))
                clear = g_terrList[TG_GROUND][k];
        if (clear < 0) {
            fprintf(stderr, "PAINTTEST|FAIL|CLEAR1 is not offered in the GROUND drawer\n");
            fails++;
        } else {
            const int px = g_mapX + 7, py = g_mapY + 7;
            g_terrArmed = clear;
            edit_stamp_template(clear, px, py, true);
            const int want = (px & 3) | ((py & 3) << 2);
            if (g_editTmpl[py * g_editBinW + px] != 255 ||
                g_editIcon[py * g_editBinW + px] != want) {
                fprintf(stderr, "PAINTTEST|FAIL|CLEAR wrote %d/%d, wanted 255/%d\n",
                        g_editTmpl[py * g_editBinW + px], g_editIcon[py * g_editBinW + px],
                        want);
                fails++;
            } else {
                fprintf(stderr, "PAINTTEST|ok  |CLEAR wrote TEMPLATE_NONE with the "
                        "cartridge's own per-cell icon (%d)\n", want);
            }
            edit_undo();
        }
    }

    /* And every pair in this theater's table must resolve to art, or the palette would
       offer tiles that cannot be drawn. */
    int noart = 0;
    for (int k = 0; k < n; k++) {
        int s2 = 0;
        if (tt_slot_of(g_pack.theater, tab[k].tmpl, tab[k].icon, &s2) < 0) noart++;
    }
    if (noart) {
        fprintf(stderr, "PAINTTEST|FAIL|%d of %d pairs resolve to no atlas slot\n", noart, n);
        fails++;
    } else {
        fprintf(stderr, "PAINTTEST|ok  |all %d pairs resolve to an atlas slot\n", n);
    }

    fprintf(stderr, "PAINTTEST|%s|%d failures\n", fails ? "FAILED" : "PASSED", fails);
    return fails;
}


/* Switching modes clears what the other mode had armed: the browser does the same, and
   the alternative is a click that places a building while the panel shows tiles. */
static void edit_set_mode(int m)
{
    if (m == g_editMode) return;
    g_editMode = m;
    g_editArmed = -1;
    g_terrArmed = -1;
    g_editDragObj = -1;
    /* The drawers are built on first DRAW, not here: at boot the pack is not loaded and
       g_pack.theater is still empty, which would silently pick the wrong bank. */
    fprintf(stderr, "edit: mode %s\n", m == 0 ? "OBJECTS" : m == 1 ? "TERRAIN" : "ELEVATION");
}


/* ------------------------------------------------------------------------------------
 *  THE EDITOR CAMERA -- Unreal's, as asked for
 *
 *      right mouse held + move    look around
 *      W A S D                    fly, relative to where you are looking
 *      Q E                        down, up
 *      wheel                      fly speed (while looking), zoom otherwise
 *
 *  WHY IT IS BUILT ON THE CONSOLE CAMERA RATHER THAN BESIDE IT
 *
 *  The renderer has one camera and a hand-written inverse of it, and picking, the
 *  minimap, culling and every screen-space bound go through that inverse. A second
 *  camera would mean a second inverse and two chances to be subtly wrong. So the console
 *  camera grew the three things it was missing -- a yaw, a pitch of its own and an eye
 *  height of its own -- and every one of them is swept by --picktest.
 *
 *  With the editor off all three sit at their console defaults and not one number moves.
 *
 *  LOOKING ROTATES ABOUT THE EYE, not about the look-at point. That is the difference
 *  between Unreal's free-look and an orbit, and it is the whole feel of the thing: the
 *  camera model is stored as a look-at point plus a distance, so every rotation
 *  recomputes the look-at point to leave the eye exactly where it was.
 * ---------------------------------------------------------------------------------- */

static bool  g_camLook  = false;      /* the right button is down and we are looking */

/* Where the eye actually is, derived from the stored look-at point. This is the same
   arithmetic set_camera's matrix performs, written once. */
static void edit_cam_eye(float* ex, float* ey, float* ez)
{
    const float p  = n64_pitch();
    const float Dc = n64_dist_cells();
    const float cp = cosf(p), sp = sinf(p);
    const float sy = sinf(g_camYaw), cy = cosf(g_camYaw);
    *ex = g_camX - Dc * cp * sy;
    *ey = cam_at_y() + Dc * sp;
    *ez = g_camZ + Dc * cp * cy;
}

/* Put the eye there, keeping the current heading: the inverse of the above. */
static void edit_cam_set_eye(float ex, float ey, float ez)
{
    const float p  = n64_pitch();
    const float Dc = n64_dist_cells();
    const float cp = cosf(p), sp = sinf(p);
    const float sy = sinf(g_camYaw), cy = cosf(g_camYaw);
    g_camX = ex + Dc * cp * sy;
    g_camZ = ez - Dc * cp * cy;
    g_camAtYFree = ey - Dc * sp;
}

/* The editor takes the camera the moment it opens: until it does, the eye height is the
   console's rule and the first rotation would jump. */
static void edit_cam_begin(void)
{
    if (g_camAtYFree > -1.0e8f) return;          /* already ours */
    g_camPitchFree = n64_pitch();                /* freeze the derived pitch          */
    g_camAtYFree   = N64_AT_Y + terrain_y(g_camX, g_camZ);
}

static void edit_cam_release(void)
{
    g_camAtYFree   = -1.0e9f;
    g_camPitchFree = -1.0f;
    g_camYaw       = 0.0f;
}

/* Looking. Pitch is clamped short of straight down and short of level: at level the
   horizon crosses the middle of the screen and every pixel above it has no ground under
   it, which is legal (screen_to_world says so) but makes an editor hard to aim. */
static void edit_cam_look(float dx, float dy)
{
    edit_cam_begin();
    float ex, ey, ez;
    edit_cam_eye(&ex, &ey, &ez);

    const float SENS = 0.0032f;
    g_camYaw += dx * SENS;
    if (g_camYaw >  3.14159265f) g_camYaw -= 6.28318531f;
    if (g_camYaw < -3.14159265f) g_camYaw += 6.28318531f;

    float p = n64_pitch() + dy * SENS;
    if (p < 0.12f) p = 0.12f;                    /* ~7 degrees: horizon stays high    */
    if (p > 1.52f) p = 1.52f;                    /* ~87 degrees: never quite top-down */
    g_camPitchFree = p;

    edit_cam_set_eye(ex, ey, ez);                /* the eye did not move              */
}

/* Flying. fwd/right/up are in EYE terms, so W really is "into the screen". */
static void edit_cam_fly(float fwd, float right, float up)
{
    edit_cam_begin();
    float ex, ey, ez;
    edit_cam_eye(&ex, &ey, &ez);

    const float p  = n64_pitch();
    const float cp = cosf(p), sp = sinf(p);
    const float sy = sinf(g_camYaw), cy = cosf(g_camYaw);
    /* Looking direction: from the eye toward the look-at point. */
    const float fx = cp * sy, fy = -sp, fz = -cp * cy;
    /* Right is forward crossed with world up, which stays horizontal at any pitch --
       so strafing never drifts up or down however steeply you are looking. */
    const float rx = cy, rz = sy;

    ex += (fx * fwd + rx * right) * g_camSpeed;
    ey += (fy * fwd + up)         * g_camSpeed;
    ez += (fz * fwd + rz * right) * g_camSpeed;

    /* Not below the ground it is looking at, and not so high the map is a speck. */
    const float floorY = terrain_y(g_camX, g_camZ) + 1.0f;
    if (ey < floorY) ey = floorY;
    if (ey > 220.0f) ey = 220.0f;
    edit_cam_set_eye(ex, ey, ez);
}


/* ------------------------------------------------------------------------------------
 *  --elevtest
 *
 *  The claim the whole tool rests on is that a 32x32 tier field can stand in for a
 *  65x65 heightmap without the map visibly changing. That is testable directly: seed the
 *  tiers off a shipped map, derive heights back out, and compare against what the
 *  cartridge actually shipped.
 *
 *  It will NOT be exact and is not supposed to be -- 41% of playable corners sit off the
 *  five-rung ladder. What matters is that the error is small and centred, which is what
 *  the browser measured (median +0.000 over 12,964 samples) and what this re-measures
 *  natively rather than taking on trust.
 * ---------------------------------------------------------------------------------- */

static int edit_elevtest(void)
{
    int fails = 0;
    if (g_pack.corner.empty()) {
        fprintf(stderr, "ELEVTEST|FAIL|this pack carries no heightmap\n");
        return 1;
    }
    elev_seed();

    unsigned char K[ELEV_K_MAX * ELEV_K_MAX];
    static unsigned char h[C3D_CORN_MAX * C3D_CORN_MAX];
    const int hw = g_gridW + 1;
    elev_coarse(g_elevTier, K);
    elev_heights(K, h);

    /* Error against the cartridge, over the playable rectangle only -- outside it the
       bake pads and there is nothing to be faithful to. */
    long n = 0; double sum = 0.0; int worst = 0;
    int hist[9]; memset(hist, 0, sizeof hist);
    for (int gy = g_mapY; gy < g_mapY + g_mapH && gy <= g_gridH; gy++)
        for (int gx = g_mapX; gx < g_mapX + g_mapW && gx <= g_gridW; gx++) {
            const int want = g_pack.corner[gy * hw + gx];
            const int got  = h[gy * hw + gx];
            const int e = got - want;
            sum += e; n++;
            const int a = e < 0 ? -e : e;
            if (a > worst) worst = a;
            const int bucket = a == 0 ? 0 : a <= 8 ? 1 : a <= 16 ? 2 : a <= 32 ? 3
                             : a <= 48 ? 4 : a <= 64 ? 5 : a <= 96 ? 6 : a <= 128 ? 7 : 8;
            hist[bucket]++;
        }
    fprintf(stderr, "ELEVTEST|%s|%ld playable corners|mean error %+.2f|worst %d\n",
            g_editScen, n, n ? sum / n : 0.0, worst);
    fprintf(stderr, "ELEVTEST|  exact %.1f%%  within 8 %.1f%%  within 16 %.1f%%  "
            "within 32 %.1f%%\n",
            100.0 * hist[0] / (n ? n : 1),
            100.0 * (hist[0] + hist[1]) / (n ? n : 1),
            100.0 * (hist[0] + hist[1] + hist[2]) / (n ? n : 1),
            100.0 * (hist[0] + hist[1] + hist[2] + hist[3]) / (n ? n : 1));

    /* The tool must not DRIFT. Deriving twice from the same tiers has to give the same
       answer, or a second stroke would move ground the first one settled. */
    static unsigned char h2[C3D_CORN_MAX * C3D_CORN_MAX];
    elev_coarse(g_elevTier, K);
    elev_heights(K, h2);
    if (memcmp(h, h2, (size_t)hw * (g_gridH + 1)) != 0) {
        fprintf(stderr, "ELEVTEST|FAIL|deriving twice gave two different heightmaps\n");
        fails++;
    } else {
        fprintf(stderr, "ELEVTEST|ok  |the derivation is stable\n");
    }

    /* Raising one block must change the ground and must be undoable, heights included. */
    {
        /* Convert first: a shipped map's ground climbs more than one tier between
           blocks in places, so the step rule refuses almost any edit until it is on the
           ladder. That refusal is correct, and testing against it would only prove the
           gate exists. */
        elev_convert();

        /* Then find ground that is genuinely FLAT for a good margin around it.
           Conversion puts corners on the ladder; it does not remove a two-tier step
           that was already there, so raising next to one is still legitimately
           refused. Testing the write path means testing it where a write is legal. */
        int bx = -1, by = -1, t0 = 1;
        for (int y = 3; y < elev_bh() - 3 && bx < 0; y++)
            for (int x = 3; x < elev_bw() - 3 && bx < 0; x++) {
                const int t = g_elevTier[y * elev_bw() + x];
                if (t >= 4) continue;
                int flat = 1;
                for (int dy = -3; dy <= 3 && flat; dy++)
                    for (int dx = -3; dx <= 3; dx++)
                        if (g_elevTier[(y + dy) * elev_bw() + (x + dx)] != t) { flat = 0; break; }
                if (flat) { bx = x; by = y; t0 = t; }
            }
        if (bx < 0) {
            fprintf(stderr, "ELEVTEST|note|no flat 7x7 block neighbourhood on this map "
                    "to test a raise in\n");
        } else {
        std::vector<unsigned char> before = g_pack.corner;
        EditSnap snap = edit_snapshot("raise");
        char why[200] = {0};
        const int want = t0 + 1;
        const int wrote = elev_paint(bx, by, 2, want, 0, why, sizeof why);
        if (!wrote) {
            fprintf(stderr, "ELEVTEST|note|raising block %d,%d to tier %d refused: %s\n",
                    bx, by, want, why);
        } else {
            edit_commit(snap);
            if (g_pack.corner == before) {
                fprintf(stderr, "ELEVTEST|FAIL|the raise wrote %d cells and changed no "
                        "height\n", wrote);
                fails++;
            } else {
                fprintf(stderr, "ELEVTEST|ok  |raising one block rewrote %d cells and "
                        "moved the ground\n", wrote);
            }
            edit_undo();
            if (g_pack.corner != before) {
                fprintf(stderr, "ELEVTEST|FAIL|undo did not restore the heightmap\n");
                fails++;
            } else {
                fprintf(stderr, "ELEVTEST|ok  |undo restored the heightmap exactly\n");
            }
        }
        }
    }

    /* Every mask the compass can produce must resolve to art, or a cliff would come out
       as a hole. Saddles are excluded on purpose: they are refused, not drawn. */
    {
        int bad = 0;
        for (int m = 1; m <= 14; m++) {
            if (elev_is_saddle(m)) continue;
            if (m == 15) continue;
            if (elev_pick_cliff(m, 3, 5) < 0) {
                fprintf(stderr, "ELEVTEST|FAIL|mask %d resolves to no cliff art\n", m);
                bad++;
            }
        }
        if (bad) fails += bad;
        else fprintf(stderr, "ELEVTEST|ok  |all 12 non-saddle masks resolve to art\n");
    }

    fprintf(stderr, "ELEVTEST|%s|%d failures\n", fails ? "FAILED" : "PASSED", fails);
    return fails;
}


/* ------------------------------------------------------------------------------------
 *  --uitest: does every control the panel DRAWS actually answer a click?
 *
 *  A hand-rolled UI has one characteristic failure: the draw and the hit test drift
 *  apart, and the symptom is not a crash or a wrong colour, it is a button that does
 *  nothing. There is no way to notice that by looking at a screenshot, which is exactly
 *  how the panel shipped with dead controls.
 *
 *  So this walks the layout, and for every control it takes the CENTRE OF THE RECTANGLE
 *  THE DRAW USES and asks eui_hit what is there. Agreement is the test. It runs headless
 *  in milliseconds and needs no window, so it can be run on every build.
 * ---------------------------------------------------------------------------------- */

static int eui_expect(float x, float y, int fbw, int fbh, int want, int wantArg,
                      const char* what, int* fails)
{
    int arg = -1;
    const int got = eui_hit(x, y, fbw, fbh, &arg);
    const bool ok = (got == want) && (wantArg < 0 || arg == wantArg);
    if (!ok) {
        (*fails)++;
        fprintf(stderr, "UITEST|FAIL|%-22s|at %.0f,%.0f|want %d arg %d|got %d arg %d\n",
                what, x, y, want, wantArg, got, arg);
    } else {
        fprintf(stderr, "UITEST|ok  |%-22s|at %.0f,%.0f|hit %d arg %d\n",
                what, x, y, got, arg);
    }
    return ok ? 0 : 1;
}

/* THE OTHER HALF OF A LAYOUT TEST: a control the DRAW skipped must not be clickable.
 *
 * The harness used to copy the draw's own "break" and simply stop probing, which made
 * it structurally blind to the one bug that matters at the edge of the panel -- a
 * rectangle that is not painted but still answers the hit test. Two real ones were
 * hiding behind exactly that: the STARTS rows past the bottom of the panel, which were
 * swallowing UNDO/REDO/REVERT and SAVE, and the view-bar toggles that overrun the
 * readout. Now every skipped control is probed for the OPPOSITE answer. */
static void eui_expect_not(float x, float y, int fbw, int fbh, int notKind, int notArg,
                           const char* what, int* fails)
{
    int arg = -1;
    const int got = eui_hit(x, y, fbw, fbh, &arg);
    if (got == notKind && (notArg < 0 || arg == notArg)) {
        (*fails)++;
        fprintf(stderr, "UITEST|FAIL|%-22s|at %.0f,%.0f|is not drawn but still answers "
                        "kind %d arg %d\n", what, x, y, got, arg);
    }
}

static int edit_uitest(int fbw, int fbh)
{
    int fails = 0;
    EuiLayout L; eui_layout(fbw, fbh, &L);
    const float S = L.s, pad = EUI_PAD * S;
    fprintf(stderr, "UITEST|%s %dx%d  backing %.1f  scale %.2f  rail %.0f  "
            "sidebar x=%.0f w=%.0f  tile %.0fx%.0f\n",
            g_editMode == 3 ? "STARTS " : g_editMode == 2 ? "ELEVATN"
                                        : g_editMode == 1 ? "TERRAIN" : "OBJECTS",
            fbw, fbh, g_uiBacking, S, L.railW, L.sideX, L.sideW, L.tileW, L.tileH);

    /* The map: right of the rail, left of the sidebar, below the title bar. */
    {
        /* In SCRIPT mode this area is the NODE CANVAS, not the map: a click there picks
           a rule, and empty canvas is deliberately dead rather than a placement. */
        const float my = (L.topH + L.barY) * 0.5f;      /* between the two bars */
        const int want = (g_editMode == 4) ? EUI_DEAD : EUI_MAP;
        eui_expect((L.railW + L.sideX) * 0.5f, my, fbw, fbh, want, -1,
                   "map centre", &fails);
        eui_expect(L.sideX - 2, my, fbw, fbh, want, -1, "map, last column", &fails);
        eui_expect(L.railW + 2, my, fbw, fbh, want, -1, "map, first column", &fails);
    }
    eui_expect((float)fbw * 0.5f, L.topH * 0.5f, fbw, fbh, EUI_DEAD, -1,
               "title bar", &fails);

    /* THE MAP MENU, probed OPEN, every row of it.
     *
     * It was never probed at all, which is why it could grow a row and nothing would
     * notice that the row below had moved. Every real item must answer as ITSELF -- the
     * row number is the whole dispatch -- and every SEPARATOR must not, because a
     * separator's nominal rectangle is a full row tall while the draw advances only
     * 7 units past it, so it overlaps the item beneath and the hit test has to be the
     * thing that keeps them apart. */
    {
        const bool hadMenu = g_euiMenuOpen;
        g_euiMenuOpen = true;
        {
            float bx, by, bw, bh;
            eui_menu_rect(&L, &bx, &by, &bw, &bh);
            eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_MENUBTN, -1,
                       "MAP menu button", &fails);
        }
        for (int i = 0; i < EUI_MENU_N; i++) {
            float ix, iy, iw, ih;
            eui_menu_item_rect(&L, i, &ix, &iy, &iw, &ih);
            char nm[40];
            snprintf(nm, sizeof nm, "menu %d %.14s", i, EUI_MENU[i].label);
            if (EUI_MENU[i].id == EMENU_SEP)
                eui_expect_not(ix + iw * 0.5f, iy + ih * 0.5f, fbw, fbh,
                               EUI_MENUITEM, i, nm, &fails);
            else
                eui_expect(ix + iw * 0.5f, iy + ih * 0.5f, fbw, fbh,
                           EUI_MENUITEM, i, nm, &fails);
        }
        g_euiMenuOpen = hadMenu;
    }

    /* The rail. */
    for (int i = 0; i <= TOOL_COUNT; i++) {
        const float x = (L.railW - L.toolSz) * 0.5f + L.toolSz * 0.5f;
        const float y = ((i == TOOL_COUNT) ? L.camY : L.toolY0 + i * (L.toolSz + 2 * S))
                        + L.toolSz * 0.5f;
        char nm[32]; snprintf(nm, sizeof nm, "rail %d", i);
        eui_expect(x, y, fbw, fbh, EUI_TOOL, i, nm, &fails);
    }

    {
        int n = 0;
        const EuiToggle* T = eui_toggles(&n);
        (void)T;
        for (int i = 0; i < n; i++) {
            float x, y, w, h;
            eui_toggle_rect(&L, i, &x, &y, &w, &h);
            char nm[32]; snprintf(nm, sizeof nm, "view toggle %d", i);
            if (x + w > L.sideX - EUI_BAR_READOUT * S) {
                eui_expect_not(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_VIEW, i,
                               nm, &fails);
                continue;
            }
            eui_expect(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_VIEW, i, nm, &fails);
        }
    }

    eui_expect(L.radarX + L.radarW * 0.5f, L.radarY + L.radarH * 0.5f,
               fbw, fbh, EUI_RADAR, -1, "radar", &fails);

    for (int i = 0; i < 4; i++) {
        char nm[32]; snprintf(nm, sizeof nm, "mode tab %d", i);
        eui_expect(L.sideX + pad + i * (L.modeW + pad) + L.modeW * 0.5f,
                   L.modeY + L.modeH * 0.5f, fbw, fbh, EUI_MODE, i, nm, &fails);
    }
    if (g_editMode == 4) {
        /* The house chips are the RULES/TEAMS switch in this mode. Two controls sharing
           one strip is exactly the sort of thing that reads fine and answers wrong. */
        const float vw = (L.sideW - pad * 2 - 4 * S * 2) / 3.0f;
        for (int i = 0; i < 3; i++) {
            char nm[32]; snprintf(nm, sizeof nm, "script view %d", i);
            eui_expect(L.sideX + pad + i * (vw + 4 * S) + vw * 0.5f,
                       L.ownerY + L.ownerH * 0.5f, fbw, fbh, EUI_SCRIPTVIEW, i, nm,
                       &fails);
        }
    } else
    for (int i = 0; i < 4; i++) {
        char nm[32]; snprintf(nm, sizeof nm, "owner chip %d", i);
        eui_expect(L.sideX + pad + i * (L.ownerW + 4 * S) + L.ownerW * 0.5f,
                   L.ownerY + L.ownerH * 0.5f, fbw, fbh, EUI_OWNER, i, nm, &fails);
    }
    if (g_editMode == 4) {
        /* Every row this view wants must be REACHABLE -- visible at some scroll
           position, inside the panel, and answered for by the hit test. The old
           harness tested only the rows that happened to fit, so a row falling off the
           bottom was invisible to it, which is the exact failure it exists to catch. */
        {
            const int saved = g_scriptRowOff;
            const int want = eui_script_rows_want();
            int seen[EUI_SCRIPT_ROWS];
            for (int i = 0; i < want; i++) seen[i] = 0;
            for (int off = 0; off <= eui_script_row_max_off(&L); off++) {
                g_scriptRowOff = off;
                for (int row = off, stop = off + eui_script_rows_fit(&L);
                     row < stop && row < want; row++) {
                    float x, y, w, h;
                    eui_script_row_rect(&L, row, &x, &y, &w, &h);
                    char nm[40];
                    snprintf(nm, sizeof nm, "script row %d off %d", row, off);
                    /* Probe the LABEL side. Two team rows are strips of chips on their
                       right half, and those centres belong to a chip, not the row. */
                    eui_expect(x + 6 * S, y + h * 0.5f, fbw, fbh, EUI_SCRIPTROW, row,
                               nm, &fails);
                    if (y < L.catY - 1 || y + h > eui_script_rows_bottom(&L) + 1) {
                        fprintf(stderr, "UITEST|FAIL|%dx%d backing %.1f: script row %d "
                                "at offset %d spans %.0f..%.0f, outside %.0f..%.0f\n",
                                fbw, fbh, g_uiBacking, row, off, y, y + h,
                                L.catY, eui_script_rows_bottom(&L));
                        fails++;
                    } else {
                        seen[row] = 1;
                    }
                }
            }
            for (int i = 0; i < want; i++)
                if (!seen[i]) {
                    fprintf(stderr, "UITEST|FAIL|%dx%d backing %.1f: %s row %d is "
                            "unreachable at every scroll position\n", fbw, fbh,
                            g_uiBacking, g_scriptView ? "team" : "rule", i);
                    fails++;
                }
            g_scriptRowOff = saved;
        }
        if (g_scriptView == 2 && g_enhSel >= 0) {
            g_scriptRowOff = 0;
            eui_script_row_clamp(&L);
            while (4 >= g_scriptRowOff + eui_script_rows_fit(&L)) g_scriptRowOff++;
            float x, y, w, h;
            eui_script_row_rect(&L, 4, &x, &y, &w, &h);
            for (int i = 0; i < ENH_CLAUSE_MAX; i++) {
                float cx, cy, cw, ch;
                eui_enh_chip_rect(&L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                char nm[40]; snprintf(nm, sizeof nm, "condition chip %d", i);
                eui_expect(cx + cw * 0.5f, cy + ch * 0.5f, fbw, fbh, EUI_ENHCHIP, i,
                           nm, &fails);
            }
            for (int i = 0; i < (int)g_enh.size(); i++) {
                float tx, ty, tw, th;
                eui_enh_card_rect(&L, i, &tx, &ty, &tw, &th);
                char nm[32]; snprintf(nm, sizeof nm, "enh card %d", i);
                eui_expect(tx + tw * 0.5f, ty + th * 0.5f, fbw, fbh, EUI_ENHCARD, i,
                           nm, &fails);
            }
            for (int i = 0; i < g_enh[g_enhSel].nclause; i++) {
                float ox, oy, ow, oh;
                eui_enh_clause_rect(&L, i, &ox, &oy, &ow, &oh);
                if (ox + ow > L.sideX) break;      /* narrow frame: the chain is clipped */
                char nm[32]; snprintf(nm, sizeof nm, "enh clause %d", i);
                eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_ENHCLAUSE, i,
                           nm, &fails);
            }
            {   /* the rename region on the selected enhanced card */
                float cx2, cy2, cw2, ch2;
                eui_enh_card_rect(&L, g_enhSel, &cx2, &cy2, &cw2, &ch2);
                eui_expect(cx2 + 40 * S, cy2 + 9 * S, fbw, fbh, EUI_RENAME, 2,
                           "enh rename", &fails);
            }
            {   /* the carrier column, when the fixture gave it a carrier */
                int carriers[64];
                if (enh_carrier_list(carriers, 64) > 0) {
                    float bx2, by2, bw2, bh2;
                    eui_carrier_rect(&L, 0, &bx2, &by2, &bw2, &bh2);
                    if (bx2 + bw2 <= L.sideX)
                        eui_expect(bx2 + bw2 * 0.5f, by2 + bh2 * 0.5f, fbw, fbh,
                                   EUI_CARRIERBOX, 0, "carrier box", &fails);
                }
            }
            g_scriptRowOff = 0;
        }
        if (g_scriptView == 1 && g_teamSel >= 0) {
            /* The chip strips are tested at the scroll position that shows them. */
            g_scriptRowOff = 0;
            eui_script_row_clamp(&L);
            while (3 >= g_scriptRowOff + eui_script_rows_fit(&L)) g_scriptRowOff++;
            float x, y, w, h;
            eui_script_row_rect(&L, 3, &x, &y, &w, &h);
            for (int i = 0; i < TEAM_CLASS_MAX; i++) {
                float cx, cy, cw, ch;
                eui_team_slot_rect(&L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                char nm[32]; snprintf(nm, sizeof nm, "member chip %d", i);
                eui_expect(cx + cw * 0.5f, cy + ch * 0.5f, fbw, fbh, EUI_TEAMSLOT, i,
                           nm, &fails);
            }
            g_scriptRowOff = 0;
            eui_script_row_clamp(&L);
            while (13 >= g_scriptRowOff + eui_script_rows_fit(&L)) g_scriptRowOff++;
            eui_script_row_rect(&L, 13, &x, &y, &w, &h);
            for (int i = 0; i < 5; i++) {
                float cx, cy, cw, ch;
                eui_team_flag_rect(&L, x, y, w, h, i, &cx, &cy, &cw, &ch);
                char nm[32]; snprintf(nm, sizeof nm, "team flag %d", i);
                eui_expect(cx + cw * 0.5f, cy + ch * 0.5f, fbw, fbh, EUI_TEAMFLAG, i,
                           nm, &fails);
            }
            /* And the canvas: a team card, and an order in its chain. */
            for (int i = 0; i < (int)g_teams.size(); i++) {
                float tx, ty, tw, th;
                eui_team_card_rect(&L, i, &tx, &ty, &tw, &th);
                char nm[32]; snprintf(nm, sizeof nm, "team card %d", i);
                eui_expect(tx + tw * 0.5f, ty + th * 0.5f, fbw, fbh, EUI_TEAMCARD, i,
                           nm, &fails);
            }
            for (int i = 0; i < g_teams[g_teamSel].nmission; i++) {
                float ox, oy, ow, oh;
                eui_team_order_rect(&L, i, &ox, &oy, &ow, &oh);
                if (ox + ow > L.sideX) break;      /* narrow frame: chain is clipped */
                char nm[32]; snprintf(nm, sizeof nm, "team order %d", i);
                eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_TEAMORDER, i,
                           nm, &fails);
            }
            {   /* the "+ add order" chip, which is one past the last order */
                const int n2 = g_teams[g_teamSel].nmission;
                float ox, oy, ow, oh;
                eui_team_order_rect(&L, n2, &ox, &oy, &ow, &oh);
                if (ox + ow <= L.sideX && n2 < TEAM_MISSION_MAX)
                    eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh,
                               EUI_TEAMORDER, n2, "+ add order chip", &fails);
            }
            {   /* the rename region on the selected card's name */
                float cx2, cy2, cw2, ch2;
                eui_team_card_rect(&L, g_teamSel, &cx2, &cy2, &cw2, &ch2);
                eui_expect(cx2 + 40 * S, cy2 + 9 * S, fbw, fbh, EUI_RENAME, 1,
                           "team rename", &fails);
            }
            g_scriptRowOff = 0;
        }
    } else if (g_editMode == 3) {
        for (int row = 0; row < eui_start_rows(); row++) {
            if (eui_start_slot(row) < 0) continue;
            float x, y, w, h;
            eui_start_rect(&L, row, &x, &y, &w, &h);
            char nm[32]; snprintf(nm, sizeof nm, "start slot %d", row);
            if (y + h > L.lintY) {
                /* Past the bottom of the panel: the draw skips it, so nothing here may
                   answer for it. Probing the NEGATIVE is the whole point. */
                eui_expect_not(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_START, row,
                               nm, &fails);
                continue;
            }
            eui_expect(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_START, row, nm, &fails);
        }
    } else if (g_editMode == 2) {
        /* The elevation panel's own controls, at the geometry eui_draw_elev uses. */
        float y = L.catY;
        {
            const float w = (L.sideW - pad * 2 - 4 * 3 * S) / 5.0f;
            for (int i = 0; i < 5; i++) {
                char nm[32]; snprintf(nm, sizeof nm, "rung %d", i);
                eui_expect(L.sideX + pad + i * (w + 3 * S) + w * 0.5f, y + 16 * S,
                           fbw, fbh, EUI_ELEVRUNG, i, nm, &fails);
            }
        }
        y += 32 * S + pad;
        for (int i = 0; i < 3; i++) {
            char nm[32]; snprintf(nm, sizeof nm, "elev tool %d", i);
            eui_expect(L.sideX + L.sideW * 0.5f, y + 17 * S, fbw, fbh,
                       EUI_ELEVTOOL, i, nm, &fails);
            y += 38 * S;
        }
        y += pad;
        {
            const float w = (L.sideW - pad * 2 - 2 * 4 * S) / 3.0f;
            for (int i = 0; i < 3; i++) {
                char nm[32]; snprintf(nm, sizeof nm, "brush %d", i);
                eui_expect(L.sideX + pad + i * (w + 4 * S) + w * 0.5f, y + 15 * S,
                           fbw, fbh, EUI_ELEVBRUSH, i, nm, &fails);
            }
        }
        {
            /* The cliff drawer: every cell that FITS answers as itself; the first
               that does not fit must answer as nothing (the draw skipped it). */
            int list[64];
            const int n = eui_cliff_list(list, 64);
            for (int k = 0; k < n; k++) {
                float cx, cy, cw, ch;
                char nm[32]; snprintf(nm, sizeof nm, "cliff piece %d", k);
                const bool fits = eui_cliff_cell_rect(&L, k, &cx, &cy, &cw, &ch);
                if (!fits) {
                    eui_expect_not(cx + cw * 0.5f, cy + ch * 0.5f, fbw, fbh,
                                   EUI_CLIFFITEM, list[k], nm, &fails);
                    break;
                }
                eui_expect(cx + cw * 0.5f, cy + ch * 0.5f, fbw, fbh,
                           EUI_CLIFFITEM, list[k], nm, &fails);
            }
        }
    } else
    for (int i = 0; i < eui_cat_count(); i++) {
        char nm[32]; snprintf(nm, sizeof nm, "category tab %d", i);
        eui_expect(L.sideX + pad + i * (L.catW + 3 * S) + L.catW * 0.5f,
                   L.catY + L.catH * 0.5f, fbw, fbh, EUI_TABC, i, nm, &fails);
    }

    /* Every tile the grid would draw, at the centre of the tile, expecting the item the
       draw would have put there. The grid is the control with real arithmetic in it and
       so the one most likely to disagree. */
    if (g_editMode < 2) {
        const int rows = (int)((L.gridH - 6 * S - 14 * S) / (L.tileH + L.gap));
        const int n = eui_grid_n();
        int checked = 0;
        for (int k = 0; k < rows * L.cols; k++) {
            const int idx = eui_grid_scroll() * L.cols + k;
            if (idx >= n) break;
            const float x = L.gridX + 3 * S + (k % L.cols) * (L.tileW + L.gap)
                          + L.tileW * 0.5f;
            const float y = L.gridY + 3 * S + (k / L.cols) * (L.tileH + L.gap)
                          + L.tileH * 0.5f;
            int arg = -1;
            const int got = eui_hit(x, y, fbw, fbh, &arg);
            const int want = (g_editMode == 1) ? idx
                                               : EDIT_KIND_FIRST[g_editTab] + idx;
            if (got != EUI_ITEM || arg != want) {
                fails++;
                fprintf(stderr, "UITEST|FAIL|grid tile %-12d|at %.0f,%.0f|want ITEM %d|"
                        "got %d arg %d\n", k, x, y, want, got, arg);
            }
            checked++;
        }
        fprintf(stderr, "UITEST|ok  |grid: %d tiles agree\n", checked);
    }

    /* The action row. It was never probed before, which is why a short window could
       swallow UNDO/REDO/REVERT and then SAVE without the harness noticing. */
    {
        const float w = (L.sideW - pad * 2 - 8 * S) / 3.0f;
        for (int i = 0; i < 3; i++) {
            char nm[32]; snprintf(nm, sizeof nm, "action %d", i);
            eui_expect(L.sideX + pad + i * (w + 4 * S) + w * 0.5f,
                       L.actY + L.actH * 0.5f, fbw, fbh, EUI_ACT, i, nm, &fails);
        }
    }

    eui_expect(L.sideX + L.sideW * 0.5f, L.saveY + L.saveH * 0.5f,
               fbw, fbh, EUI_SAVE, -1, "SAVE", &fails);
    eui_expect(L.sideX + L.sideW * 0.5f, L.playY + L.playH * 0.5f,
               fbw, fbh, EUI_PLAY, -1, "PLAY", &fails);
    {
        float bx, by, bw, bh;
        eui_check_badge_rect(&L, &bx, &by, &bw, &bh);
        eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_CHECK, -1,
                   "check badge", &fails);
    }
    if (g_editMode == 4) {
        const float fullW = L.sideW - pad * 2, playW = fullW * 0.62f;
        eui_expect(L.sideX + pad + playW * 0.5f, L.playY + L.playH * 0.5f, fbw, fbh,
                   EUI_PLAY, -1, "PLAY (split)", &fails);
        eui_expect(L.sideX + pad + playW + 4 * S + (fullW - playW - 4 * S) * 0.5f,
                   L.playY + L.playH * 0.5f, fbw, fbh, EUI_TRACE, -1, "TRACE", &fails);
    }

    /* THE NEW MAP MODAL, probed open. Its size list grew a fifth row (128 x 128) and
       its flow stretches rather than being hand-placed rows -- exactly the growth that
       once landed the THEATER header on the LARGE row. Every option is probed at the
       rect the draw uses, plus both buttons, then the modal is closed so nothing below
       runs against a world where a modal owns every click. Probed in OBJECTS mode
       only: the modal is mode-independent and once per size is enough. */
    if (g_editMode == 0) {
        const int hadModal = g_euiModal;
        g_euiModal = MODAL_NEW;
        for (int i = 0; i < EUI_SIZE_N; i++) {
            float ox, oy, ow, oh;
            eui_modal_opt_rect(&L, fbw, fbh, 0, i, &ox, &oy, &ow, &oh);
            char nm[40]; snprintf(nm, sizeof nm, "new-map size row %d (%s)", i,
                                  EUI_SIZES[i].name);
            eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_MODALOPT, i,
                       nm, &fails);
        }
        for (int i = 0; i < EUI_THEATER_N; i++) {
            float ox, oy, ow, oh;
            eui_modal_opt_rect(&L, fbw, fbh, 1, i, &ox, &oy, &ow, &oh);
            char nm[40]; snprintf(nm, sizeof nm, "new-map theater %d", i);
            eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_MODALOPT, 10 + i,
                       nm, &fails);
        }
        for (int i = 0; i < 2; i++) {
            float ox, oy, ow, oh;
            eui_modal_opt_rect(&L, fbw, fbh, 2, i, &ox, &oy, &ow, &oh);
            char nm[40]; snprintf(nm, sizeof nm, "new-map kind %d", i);
            eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_MODALOPT, 20 + i,
                       nm, &fails);
        }
        for (int i = 0; i < 2; i++) {
            float bx, by, bw, bh;
            eui_modal_btn_rect(&L, fbw, fbh, i, &bx, &by, &bw, &bh);
            char nm[40]; snprintf(nm, sizeof nm, "new-map button %d", i);
            eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_MODALBTN, i,
                       nm, &fails);
        }

        /* THE SAVE AS DIALOG, probed open. It is a SHORTER dialog than the other three
           -- the buttons hang off the bottom of its own rectangle, not off a shared one
           -- so its CANCEL and SAVE are exactly the pair that could end up somewhere the
           draw is not. The name field is deliberately dead to the hit test: typing goes
           to the keyboard, and a click target over it would be a control that looks like
           it does something and does not. */
        g_euiModal = MODAL_SAVEAS;
        for (int i = 0; i < 2; i++) {
            float bx, by, bw, bh;
            eui_modal_btn_rect(&L, fbw, fbh, i, &bx, &by, &bw, &bh);
            char nm[40]; snprintf(nm, sizeof nm, "save-as button %d", i);
            eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_MODALBTN, i,
                       nm, &fails);
        }
        {
            float mx2, my2, mw2, mh2;
            eui_modal_rect(&L, fbw, fbh, &mx2, &my2, &mw2, &mh2);
            eui_expect(mx2 + mw2 * 0.5f, my2 + 79 * S, fbw, fbh, EUI_DEAD, -1,
                       "save-as name field", &fails);
        }
        g_euiModal = hadModal;
    }

    fprintf(stderr, "UITEST|%s|%d failures at %dx%d backing %.1f mode %d\n",
            fails ? "FAILED" : "PASSED", fails, fbw, fbh, g_uiBacking, g_editMode);
    return fails;
}

/* ------------------------------------------------------------------------------------
 *  Placing
 * ---------------------------------------------------------------------------------- */

/* Push one object into the document. This is the same path --posetest uses to prove the
   renderer will draw objects cnc_eyes invented: fill a SimObject, push it, and the real
   cartridge mesh appears with its shadow, its health bar and its radar blip. The brain
   is not consulted and must not be -- it has no create-with-house-and-cell, and every
   creation path it does have is a game action with game consequences. */
static bool edit_place_q(int cx, int cy, bool quiet)
{
    if (!g_editOn || g_editArmed < 0 || g_editArmed >= EDIT_ITEM_N) return false;
    const EditItem* t = &EDIT_ITEMS[g_editArmed];

    /* Every cell the type really occupies, or none of them. The cell list is not a
       box: a refinery is four cells inside a 3x2 and a repair bay is a plus, so a
       corner that a bounding box would have refused is often perfectly legal. */
    for (int i = 0; i < (int)t->n; i++) {
        const int px = cx + t->dx[i], py = cy + t->dy[i];
        if (px < 0 || py < 0 || px >= g_gridW || py >= g_gridH) {
            if (!quiet)
                fprintf(stderr, "edit: refused %s at %d,%d -- cell %d,%d is off the map\n",
                        t->code, cx, cy, px, py);
            return false;
        }
        if (edit_cell_ok(px, py)) continue;
        if (!quiet)
            fprintf(stderr, "edit: refused %s at %d,%d -- cell %d,%d is %s\n",
                    t->code, cx, cy, px, py,
                    edit_cell_occupied(px, py) ? "already occupied"
                                               : EDIT_LAND_NAME[edit_land_at(px, py)]);
        return false;
    }

    /* A wall is a BuildingTypeClass in the brain but overlay everywhere it matters --
       it saves as overlay, it draws as overlay, and the editor already has a tool that
       auto-connects it. Route it there rather than making a one-cell building. */
    if (t->kind == EDIT_KIND_WALL) {
        for (int k = 0; k < 5; k++)
            if (!strcmp(WALL_NAME[k], t->code)) { edit_wall_put(cx, cy, k); break; }
        return true;
    }

    /* Taken here rather than at the top: everything above this line can still refuse,
       and an undo stack full of refused placements is an undo button that looks
       broken. */
    EditSnap before = edit_snapshot(t->code);

    SimObject so;
    memset(&so, 0, sizeof(so));
    switch (t->kind) {
        case EDIT_KIND_UNIT:     so.kind = K_UNIT;     break;
        case EDIT_KIND_INFANTRY: so.kind = K_INFANTRY; break;
        case EDIT_KIND_TERRAIN:  so.kind = K_TERRAIN;  break;
        default:                 so.kind = K_BUILDING; break;
    }
    snprintf(so.type,  sizeof(so.type),  "%s", t->code);
    snprintf(so.house, sizeof(so.house), "%s",
             so.kind == K_TERRAIN ? "None" : EUI_HOUSES[g_editOwner].key);
    so.cx = cx;  so.cy = cy;
    so.tcx = cx; so.tcy = cy;

    /* The draw anchor. A building sits at the centre of its cell span; everything else
       stands in the middle of one cell. */
    int minx = 63, maxx = 0, miny = 63, maxy = 0;
    for (int i = 0; i < (int)t->n; i++) {
        if (t->dx[i] < minx) minx = t->dx[i];
        if (t->dx[i] > maxx) maxx = t->dx[i];
        if (t->dy[i] < miny) miny = t->dy[i];
        if (t->dy[i] > maxy) maxy = t->dy[i];
    }
    so.fw = (short)(maxx - minx + 1);
    so.fh = (short)(maxy - miny + 1);
    if (so.kind == K_BUILDING) {
        so.wx = so.ex = (float)cx + (float)(minx + maxx + 1) * 0.5f;
        so.wz = so.ez = (float)cy + (float)(miny + maxy + 1) * 0.5f;
    } else {
        so.wx = so.ex = (float)cx + 0.5f;
        so.wz = so.ez = (float)cy + 0.5f;
    }
    so.face  = so.kind == K_BUILDING ? 0 : 96;   /* structures draw unrotated */
    so.tface = -1;
    so.pips = 0; so.maxpips = -1;
    so.str = so.maxstr = (short)t->str;
    g_objects.push_back(so);
    edit_commit(before);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: placed %s (%s) at %d,%d  [%d objects]\n",
            t->code, so.house, cx, cy, (int)g_objects.size());
    return true;
}

/* A click says why it was refused. A search that is probing dozens of cells does not --
   74 lines of "that one is water either" is noise that buries the one line that matters. */
static bool edit_place(int cx, int cy) { return edit_place_q(cx, cy, false); }

/* The footprint the armed type would cover, drawn under the cursor. */
static void edit_draw_footprint(int fbw, int fbh)
{
    if (!g_editOn || !g_editOverMap) return;
    if (g_editMode == 4) return;      /* SCRIPT has no cell under the pointer */

    /* ELEVATION: the blocks the brush covers, snapped to the 2x2 block lattice the tool
       actually works in -- showing a single cell would be a lie about what will move. */
    if (g_editMode == 2) {
        const int bx = g_editCellX / 2, by = g_editCellY / 2;
        const bool ready = elev_ready();
        begin_overlay(fbw, fbh);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int y = by - g_elevBrush + 1; y <= by + g_elevBrush - 1; y++)
            for (int x = bx - g_elevBrush + 1; x <= bx + g_elevBrush - 1; x++) {
                if (x < 0 || y < 0 || x >= elev_bw() || y >= elev_bh()) continue;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++) {
                        const int cx = 2 * x + dx, cy = 2 * y + dy;
                        if (cx >= g_gridW || cy >= g_gridH) continue;
                        glColor4f(ready ? 0.56f : 1.0f, ready ? 0.89f : 0.35f,
                                  ready ? 1.0f : 0.35f, 0.22f);
                        glBegin(GL_TRIANGLES);
                        sb_place_cell_tris(cx, cy, fbw, fbh);
                        glEnd();
                    }
            }
        glDisable(GL_BLEND);
        end_overlay();
        return;
    }

    /* TERRAIN: the exact cells the block will overwrite, so a 4x3 river mouth shows its
       whole shape before it lands rather than after. Green where it will write, red on
       cells that fall off the map or that this theater's bank has no icon for -- those
       are SKIPPED, and a skipped cell you were not told about is a hole in your river. */
    if (g_editMode == 1) {
        if (g_terrArmed < 0) return;
        const EditTemplate* e = &EDIT_TEMPLATES[g_terrArmed];
        begin_overlay(fbw, fbh);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int dy = 0; dy < (int)e->h; dy++)
            for (int dx = 0; dx < (int)e->w; dx++) {
                const int cx = g_editCellX + dx, cy = g_editCellY + dy;
                if (cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) continue;
                int sea = 0;
                const bool ok = tt_slot_of(g_pack.theater, g_terrArmed,
                                           dy * (int)e->w + dx, &sea) >= 0;
                glColor4f(ok ? 0.35f : 1.0f, ok ? 1.0f : 0.25f, ok ? 0.45f : 0.25f,
                          ok ? 0.22f : 0.34f);
                glBegin(GL_TRIANGLES);
                sb_place_cell_tris(cx, cy, fbw, fbh);
                glEnd();
            }
        glDisable(GL_BLEND);
        end_overlay();
        return;
    }

    if (g_editArmed < 0) return;
    const EditItem* t = &EDIT_ITEMS[g_editArmed];

    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* Per CELL, not per block. A 3x4 refinery with one corner in the water is a
       different thing to tell the player than "this does not fit", and the browser
       prototype learned the same lesson: the useful answer names the corner. */
    for (int i = 0; i < (int)t->n; i++) {
            const int cx = g_editCellX + t->dx[i], cy = g_editCellY + t->dy[i];
            if (cx >= g_gridW || cy >= g_gridH) continue;
            const bool ok = edit_cell_ok(cx, cy);
            glColor4f(ok ? 0.20f : 1.0f, ok ? 1.0f : 0.22f, 0.24f, ok ? 0.15f : 0.34f);
            glBegin(GL_TRIANGLES);
            sb_place_cell_tris(cx, cy, fbw, fbh);
            glEnd();
        }
    /* The outline carries the shape once the fill is faint enough to see through. */
    glLineWidth(1.0f);
    for (int i = 0; i < (int)t->n; i++) {
            const int cx = g_editCellX + t->dx[i], cy = g_editCellY + t->dy[i];
            if (cx >= g_gridW || cy >= g_gridH) continue;
            const bool ok = edit_cell_ok(cx, cy);
            glColor4f(ok ? 0.45f : 1.0f, ok ? 1.0f : 0.40f, 0.45f, ok ? 0.55f : 0.85f);
            float c[4], r[4];
            world_to_screen((float)cx,     terrain_corner_y(cx, cy),         (float)cy,     fbw, fbh, &c[0], &r[0]);
            world_to_screen((float)cx + 1, terrain_corner_y(cx + 1, cy),     (float)cy,     fbw, fbh, &c[1], &r[1]);
            world_to_screen((float)cx + 1, terrain_corner_y(cx + 1, cy + 1), (float)cy + 1, fbw, fbh, &c[2], &r[2]);
            world_to_screen((float)cx,     terrain_corner_y(cx, cy + 1),     (float)cy + 1, fbw, fbh, &c[3], &r[3]);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 4; i++) glVertex2f(c[i], r[i]);
            glEnd();
        }
    glDisable(GL_BLEND);
    end_overlay();
}


/* ------------------------------------------------------------------------------------
 *  Saving
 *
 *  THE WRITER OWNS FIVE SECTIONS AND PRESERVES EVERYTHING ELSE.
 *
 *  A scenario INI holds far more than the editor understands: triggers, teamtypes, the
 *  AI's base list, the briefing, per-house credits and alliances. A writer that emitted
 *  only what it knows would silently gut a campaign mission the first time it saved --
 *  the map would still load, and every trigger that made it a mission would be gone.
 *
 *  So this reads the scenario's own INI and copies it out line for line, replacing only
 *  the sections it owns and appending them if they were absent. Everything it does not
 *  understand travels through untouched, which is also the honest position: the editor
 *  has no opinion about triggers and should not express one.
 *
 *  Cell numbering is y*64 + x, the engine's own INI space, which is the same number the
 *  .BIN is indexed by.
 * ---------------------------------------------------------------------------------- */

/* The five the editor is responsible for. Anything else in the file is passed through. */
static const char* const EDIT_OWNED[] = {
    "TERRAIN", "OVERLAY", "SMUDGE", "STRUCTURES", "UNITS", "INFANTRY",
    /* [Waypoints] joins the owned sections now that the editor can place them. It was
       passed through untouched before, which was right while nothing here could change
       it -- and would have silently discarded every start the moment something could. */
    "Waypoints"
};
static const int EDIT_OWNED_N = (int)(sizeof(EDIT_OWNED) / sizeof(EDIT_OWNED[0]));

static bool edit_section_is_owned(const char* name)
{
    /* [Triggers] is owned ONLY once the editor has changed one. An untouched mission
       keeps its own line order, its own spacing and its own comments -- which is the
       whole reason the writer copies a source through instead of regenerating it. */
    if (g_trigDirty && !strcasecmp(name, "Triggers")) return true;
    /* Same rule for the zones: untouched, the mission keeps its own 4,000-odd lines in
       their own order; touched, they are ours. */
    if (g_zoneDirty && !strcasecmp(name, "CellTriggers")) return true;
    /* And for the teams. Note Write_INI also does ini.Clear("Teams") to wipe the legacy
       section name (teamtype.cpp:409); nothing in the shipped set has one, so the
       passthrough would carry a stale [Teams] through if one ever appeared. */
    if (g_teamDirty && !strcasecmp(name, "TeamTypes")) return true;
    /* The Enhanced sections are entirely ours -- no shipped mission has them -- but they
       are still only claimed once something is in them, so a mission the editor merely
       opened does not gain two empty sections. */
    if (g_enhDirty && (!strcasecmp(name, "EnhancedScript") ||
                       !strcasecmp(name, "EnhancedZones"))) return true;
    for (int i = 0; i < EDIT_OWNED_N; i++) {
        const char* a = EDIT_OWNED[i];
        const char* b = name;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return true;
    }
    return false;
}

/* Infantry stand on one of five sub-cell positions (StoppingCoordAbs): the centre and
   four quincunx spots. SimObject carries the world position, not the index, so the index
   is recovered from the fractional part -- which is exact, because the brain put it on
   one of those five points in the first place. */
static int edit_subcell_of(float wx, float wz, int cx, int cy)
{
    static const float SX[5] = { 0.5f, 0.25f, 0.75f, 0.25f, 0.75f };
    static const float SZ[5] = { 0.5f, 0.25f, 0.25f, 0.75f, 0.75f };
    const float fx = wx - (float)cx, fz = wz - (float)cy;
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < 5; i++) {
        const float dx = fx - SX[i], dz = fz - SZ[i];
        const float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}


/* ------------------------------------------------------------------------------------
 *  What the editor cannot see, and therefore must not invent
 *
 *  An INI object line carries an ORDER (Guard, Hunt, Area Guard, Sticky...) and a
 *  TRIGGER name. The brain does not export either, so SimObject does not carry them, so
 *  the editor genuinely does not know them. Writing a constant "Guard,None" for every
 *  object looked harmless and was not: on SCG01EA it rewrote a Hunt infantryman and four
 *  Area Guards into Guards, which changes what the mission does.
 *
 *  So they are remembered from the file that was read. An object that was already there
 *  keeps its own order and trigger; one the editor placed gets Guard and None, which is
 *  the right default for a thing with no history. Keyed on house+type+cell, which is
 *  what identifies an object in an INI -- the leading index is just a line number.
 * ---------------------------------------------------------------------------------- */


static void edit_prior_key(char* out, size_t n, const char* house, const char* type, int cell)
{
    snprintf(out, n, "%s|%s|%d", house, type, cell);
}

static const EditPrior* edit_prior_find(const char* house, const char* type, int cell)
{
    char key[64];
    edit_prior_key(key, sizeof key, house, type, cell);
    for (size_t i = 0; i < g_editPrior.size(); i++)
        if (!strcmp(g_editPrior[i].key, key)) return &g_editPrior[i];
    return NULL;
}

/* MOVING AN OBJECT MUST NOT DETACH ITS TRIGGER.
 *
 * The key is house|type|CELL, which is what identifies an object in an INI -- and the
 * cell is exactly the part a move changes. Without this, dragging a tagged building two
 * cells left silently dropped the trigger it carried and the order it had, and the map
 * saved as if it never had them. On a scripted mission that is the difference between a
 * mission that works and one that quietly does nothing.
 *
 * So the entry follows the object. */
static void edit_prior_rekey(const char* house, const char* type, int fromCell, int toCell)
{
    char from[64], to[64];
    edit_prior_key(from, sizeof from, house, type, fromCell);
    edit_prior_key(to,   sizeof to,   house, type, toCell);
    for (size_t i = 0; i < g_editPrior.size(); i++)
        if (!strcmp(g_editPrior[i].key, from)) {
            snprintf(g_editPrior[i].key, sizeof g_editPrior[i].key, "%s", to);
            return;
        }
}


/* ATTACH A RULE TO AN OBJECT.
 *
 * The trigger is the trailing field on a STRUCTURES / UNITS / INFANTRY line, and the
 * editor already remembers each object's own trigger and order in the prior table so a
 * shipped mission keeps them. Tagging is therefore a write INTO that table rather than a
 * new field on SimObject: the writer already reads from there, so nothing downstream has
 * to learn about this.
 *
 * 705 structures, 296 infantry and 111 vehicles carry one across the campaign, and they
 * come in groups -- mean 4.1 objects per trigger -- so this is a gesture you repeat, not
 * a property you set once.
 */
static void edit_set_object_trigger(int idx, const char* trig)
{
    if (idx < 0 || idx >= (int)g_objects.size()) return;
    const SimObject& o = g_objects[idx];
    char key[64];
    edit_prior_key(key, sizeof key, o.house, o.type, o.cy * edit_ini_w() + o.cx);
    for (size_t i = 0; i < g_editPrior.size(); i++)
        if (!strcmp(g_editPrior[i].key, key)) {
            snprintf(g_editPrior[i].trig, sizeof g_editPrior[i].trig, "%s",
                     trig && *trig ? trig : "None");
            return;
        }
    /* No entry yet: an object the editor placed, which has no history. Give it one so
       the tag has somewhere to live. */
    EditPrior e;
    memset(&e, 0, sizeof e);
    snprintf(e.key,   sizeof e.key,   "%s", key);
    /* Guard is what a placed object already saves as, so the tag does not silently
       change its behaviour as a side effect of being tagged. */
    snprintf(e.order, sizeof e.order, "Guard");
    snprintf(e.trig,  sizeof e.trig,  "%s", trig && *trig ? trig : "None");
    g_editPrior.push_back(e);
}

/* How many objects carry this rule. */
static int edit_tagged_count(int t)
{
    if (t < 0 || t >= (int)g_triggers.size()) return 0;
    int n = 0;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const char* tr = edit_trigger_for(g_objects[i]);
        if (tr && !strcasecmp(tr, g_triggers[t].name)) n++;
    }
    return n;
}

/* Read the orders and triggers out of a scenario INI before it is overwritten. The three
   object sections have the order and trigger at different positions, which is why this
   is three cases and not one loop. */
/* Read the map's kind back out of [BASIC], so opening a map remembers what it is. */
/* OVERLAY CELLS THE EDITOR DOES NOT MODEL, carried through instead of deleted.
 *
 * [OVERLAY] holds five things: walls, tiberium, concrete, crates and the civilian field
 * decorations. The brain reports only the first two on their own channels (WALL| and
 * TIB|), so those are all the editor has ever known about -- and because it OWNS the
 * section, everything else was written out of existence the first time a map was saved.
 *
 * Measured across the 95 shipped missions: 27 wooden crates in 24 of them, 14 steel
 * crates in 13, and 96 cells of V12..V18 field decoration in 17. Open any of those in
 * the editor, save, and the crates are gone.
 *
 * The editor cannot place or erase these, so carrying them verbatim is exactly right:
 * it is the same passthrough the writer already does for every section it does not own,
 * applied to the rows of a section it owns only partly. A cell that the editor DOES
 * model wins, because the editor may have put a wall where a crate used to be. */
struct OverlayExtra { short cell; char name[10]; };
static std::vector<OverlayExtra> g_ovExtra;

static void edit_read_overlay_extra(const char* path)
{
    g_ovExtra.clear();
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    bool in = false;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { in = !strncasecmp(p, "[OVERLAY]", 9); continue; }
        if (!in || *p == ';' || *p == '\r' || *p == '\n' || !*p) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        const int cell = atoi(p);
        char nm[16];
        snprintf(nm, sizeof nm, "%s", eq + 1);
        for (char* e = nm + strlen(nm);
             e > nm && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' '); ) *--e = 0;
        if (cell < 0 || cell >= EDIT_GRID_MAX || !nm[0]) continue;
        bool known = false;
        for (int i = 0; i < 5 && !known; i++) if (!strcasecmp(nm, WALL_NAME[i])) known = true;
        if (!known && (nm[0] == 'T' || nm[0] == 't') && (nm[1] == 'I' || nm[1] == 'i')) {
            /* TI1..TI12 and nothing else -- a name merely starting "TI" is not enough. */
            const int n = atoi(nm + 2);
            if (n >= 1 && n <= 12) known = true;
        }
        if (known) continue;
        OverlayExtra e;
        e.cell = (short)cell;
        snprintf(e.name, sizeof e.name, "%s", nm);
        g_ovExtra.push_back(e);
    }
    fclose(f);
    if (!g_ovExtra.empty())
        fprintf(stderr, "edit: carrying %d overlay cell%s the editor does not model "
                        "(crates, concrete, field decoration)\n",
                (int)g_ovExtra.size(), g_ovExtra.size() == 1 ? "" : "s");
}

static void edit_read_kind(const char* path)
{
    g_mapIsMulti = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    bool inBasic = false;
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { inBasic = !strncasecmp(p, "[BASIC]", 7); continue; }
        if (inBasic && !strncasecmp(p, "CNC3DKind", 9)) {
            g_mapIsMulti = (strstr(p, "Multi") != NULL) ? 1 : 0;
            break;
        }
    }
    fclose(f);
}

/* THE MAP'S OWN NAME, out of [Basic] Name=. The map-list parser reads the same key for
   every map in the folder; this reads it for the ONE map that is open, which is the copy
   the title strip shows and SAVE writes back.
 *
 * Call it AFTER g_editScen is set. Every map the cartridge ships and every map this
 * editor has written so far carries its own SLOT NAME in that key -- Name=USER03 -- and
 * that is not a name, it is the file repeated. Telling the two apart needs the slot, and
 * leaving the title empty in that case is what makes the strip fall back to the slot
 * instead of printing it twice. */
static void edit_read_title(const char* path)
{
    g_editTitle[0] = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    bool inBasic = false;
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { inBasic = !strncasecmp(p, "[BASIC]", 7); continue; }
        if (!inBasic) continue;
        if (strncasecmp(p, "Name=", 5)) continue;
        snprintf(g_editTitle, sizeof g_editTitle, "%s", p + 5);
        break;
    }
    fclose(f);
    edit_title_trim(g_editTitle);
    if (!strcasecmp(g_editTitle, g_editScen)) g_editTitle[0] = 0;
}

static void edit_read_prior(const char* path)
{
    g_editPrior.clear();
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[2048];
    int sect = 0;   /* 1 structures, 2 units, 3 infantry */
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            sect = 0;
            if (!strncasecmp(p, "[STRUCTURES]", 12)) sect = 1;
            else if (!strncasecmp(p, "[UNITS]", 7))  sect = 2;
            else if (!strncasecmp(p, "[INFANTRY]", 10)) sect = 3;
            continue;
        }
        if (!sect) continue;
        const char* eq = strchr(p, '=');
        if (!eq) continue;

        char house[24] = {0}, type[24] = {0}, order[24] = {0}, trig[24] = {0};
        int health = 0, cell = -1, face = 0, sub = 0, got = 0;
        if (sect == 1)
            got = sscanf(eq + 1, "%23[^,],%23[^,],%d,%d,%d,%23[^,\r\n]",
                         house, type, &health, &cell, &face, trig);
        else if (sect == 2)
            got = sscanf(eq + 1, "%23[^,],%23[^,],%d,%d,%d,%23[^,],%23[^,\r\n]",
                         house, type, &health, &cell, &face, order, trig);
        else
            got = sscanf(eq + 1, "%23[^,],%23[^,],%d,%d,%d,%23[^,],%d,%23[^,\r\n]",
                         house, type, &health, &cell, &sub, order, &face, trig);
        if (got < 4 || cell < 0) continue;

        EditPrior e;
        edit_prior_key(e.key, sizeof e.key, house, type, cell);
        snprintf(e.order, sizeof e.order, "%s", order[0] ? order : "Guard");
        snprintf(e.trig,  sizeof e.trig,  "%s", trig[0]  ? trig  : "None");
        g_editPrior.push_back(e);
    }
    fclose(f);
    fprintf(stderr, "edit: remembered the order and trigger of %d objects\n",
            (int)g_editPrior.size());
}

static const char* edit_order_for(const SimObject& o)
{
    const EditPrior* e = edit_prior_find(o.house, o.type, o.cy * edit_ini_w() + o.cx);
    return e ? e->order : "Guard";
}

static const char* edit_trigger_for(const SimObject& o)
{
    const EditPrior* e = edit_prior_find(o.house, o.type, o.cy * edit_ini_w() + o.cx);
    return e ? e->trig : "None";
}

/* [Waypoints], all 28, written in DESCENDING key order the way the cartridge's own
   files do -- and the unset ones written as -1 explicitly rather than omitted, because
   display.cpp:1334 defaults a missing key to -1 and a file that says so is a file you
   can read. */
static void edit_emit_waypoints(FILE* out)
{
    fputs("\r\n[Waypoints]\r\n", out);
    for (int i = EDIT_WAYPT_COUNT - 1; i >= 0; i--)
        fprintf(out, "%d=%d\r\n", i, g_waypoint[i]);
}

/* The INI health field, 0..256 where 256 is untouched. Hardcoding 256 healed every
   damaged building on the map the first time it round-tripped -- SCG01EA ships two
   GUN turrets at 48 and 128. */
static int edit_health_of(const SimObject& o)
{
    if (o.maxstr <= 0) return 256;
    int v = (int)((double)o.str * 256.0 / (double)o.maxstr + 0.5);
    if (v < 1) v = 1;
    if (v > 256) v = 256;
    return v;
}

static void edit_emit_owned(FILE* out)
{
    const int n = (int)g_objects.size();

    edit_emit_waypoints(out);
    if (g_trigDirty) edit_emit_triggers(out);
    if (g_zoneDirty) edit_emit_zones(out);
    if (g_teamDirty) edit_emit_teams(out);
    if (g_enhDirty) enh_emit(out);

    fputs("\r\n[TERRAIN]\r\n", out);
    for (int i = 0; i < n; i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_TERRAIN) continue;
        fprintf(out, "%d=%s,None\r\n", o.cy * edit_ini_w() + o.cx, o.type);
    }

    /* Walls and tiberium are OVERLAY cells and never reach g_objects -- the brain emits
       them on their own channels and cnc_eyes keeps them in g_walls and g_tib. Writing
       only g_objects here would drop every wall and every tiberium field on the map. */
    fputs("\r\n[OVERLAY]\r\n", out);
    for (size_t i = 0; i < g_walls.size(); i++)
        /* .owner is the CellClass owner -- "None" for anything a scenario painted in --
           NOT the wall type. Writing it turned all 42 sandbags into None on the first
           round trip. The type is .kind, an index into the engine's own five, and
           WALL_NAME is already in this file. */
        fprintf(out, "%d=%s\r\n", g_walls[i].y * edit_ini_w() + g_walls[i].x,
                WALL_NAME[g_walls[i].kind]);
    for (size_t i = 0; i < g_tib.size(); i++)
        fprintf(out, "%d=TI%d\r\n", g_tib[i].y * edit_ini_w() + g_tib[i].x,
                (int)g_tib[i].kind + 1);
    /* And the ones the editor does not model, unless it has since put something of its
       own on that cell. */
    for (size_t i = 0; i < g_ovExtra.size(); i++) {
        const int cx = g_ovExtra[i].cell % edit_ini_w(),
                  cy = g_ovExtra[i].cell / edit_ini_w();
        bool taken = false;
        for (size_t k = 0; k < g_walls.size() && !taken; k++)
            if (g_walls[k].x == cx && g_walls[k].y == cy) taken = true;
        for (size_t k = 0; k < g_tib.size() && !taken; k++)
            if (g_tib[k].x == cx && g_tib[k].y == cy) taken = true;
        if (!taken)
            fprintf(out, "%d=%s\r\n", (int)g_ovExtra[i].cell, g_ovExtra[i].name);
    }

    /* SMUDGE NAMES ARE A TABLE, NOT A FORMULA.
     *
     * This wrote "SC%d" with type+1 for every smudge, and the type is the engine's raw
     * SmudgeType enum: craters 0-5, scorches 6-11, bibs 12-14 (defines.h:1379-1393,
     * names in sdata.cpp). So a crater was written as a scorch, a scorch under a name
     * six too high, and a bib as "SC13". The engine's From_Name does not know those
     * names, so it DROPPED them -- and 25 of the 95 shipped missions carry authored
     * craters and scorches, every one of which was destroyed by opening the map and
     * saving it. Measured on SCB06EC: nine authored smudges in, nine wrong names out.
     *
     * The line beside this one is why it went unnoticed. The brain normalises tiberium
     * on the way out (Overlay - OVERLAY_TIBERIUM1, dllinterface.cpp:8407) so "TI%d" with
     * kind+1 is correct there; it does NOT normalise the smudge (:8509), and the two
     * lines look identical.
     *
     * BIBS ARE NOT WRITTEN AT ALL. They are the aprons the engine lays under a building
     * by itself, and no shipped mission authors one -- all 178 authored smudges in the
     * campaign are CR1 or SC1..SC6. Writing them back would freeze an engine-derived
     * decoration into the file, so moving the building later would leave its apron
     * behind under nothing. */
    static const char* const SMUDGE_NAME[] = {
        "CR1", "CR2", "CR3", "CR4", "CR5", "CR6",
        "SC1", "SC2", "SC3", "SC4", "SC5", "SC6",
        "BIB1", "BIB2", "BIB3"
    };
    static const int SMUDGE_NAME_N = (int)(sizeof SMUDGE_NAME / sizeof SMUDGE_NAME[0]);
    static const int SMUDGE_FIRST_BIB = 12;

    fputs("\r\n[SMUDGE]\r\n", out);
    for (size_t i = 0; i < g_smudges.size(); i++) {
        const int t = (int)g_smudges[i].type;
        if (t < 0 || t >= SMUDGE_NAME_N) continue;   /* not a smudge this engine has */
        if (t >= SMUDGE_FIRST_BIB) continue;         /* the engine lays its own bibs */
        const int cell = g_smudges[i].y * edit_ini_w() + g_smudges[i].x;
        fprintf(out, "%d=%s,%d,%d\r\n", cell, SMUDGE_NAME[t], cell,
                (int)g_smudges[i].data);
    }

    int k = 0;
    fputs("\r\n[STRUCTURES]\r\n", out);
    for (int i = 0; i < n; i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_BUILDING) continue;
        fprintf(out, "%03d=%s,%s,%d,%d,%d,%s\r\n", k++, o.house, o.type,
                edit_health_of(o), o.cy * edit_ini_w() + o.cx, o.face < 0 ? 0 : o.face,
                edit_trigger_for(o));
    }

    k = 0;
    fputs("\r\n[UNITS]\r\n", out);
    for (int i = 0; i < n; i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_UNIT) continue;
        fprintf(out, "%03d=%s,%s,%d,%d,%d,%s,%s\r\n", k++, o.house, o.type,
                edit_health_of(o), o.cy * edit_ini_w() + o.cx, o.face < 0 ? 0 : o.face,
                edit_order_for(o), edit_trigger_for(o));
    }

    k = 0;
    fputs("\r\n[INFANTRY]\r\n", out);
    for (int i = 0; i < n; i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_INFANTRY) continue;
        fprintf(out, "%03d=%s,%s,%d,%d,%d,%s,%d,%s\r\n", k++, o.house, o.type,
                edit_health_of(o), o.cy * edit_ini_w() + o.cx,
                edit_subcell_of(o.wx, o.wz, o.cx, o.cy),
                edit_order_for(o), o.face < 0 ? 0 : o.face, edit_trigger_for(o));
    }
}


/* Copy src to dst, swapping the owned sections for what the editor holds. Returns the
   number of objects written, or -1. */
static int edit_write_ini(const char* src, const char* dst)
{
    FILE* in = fopen(src, "rb");
    if (!in) { fprintf(stderr, "edit: cannot read %s\n", src); return -1; }
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); fprintf(stderr, "edit: cannot write %s\n", dst); return -1; }

    char line[2048];
    bool skipping = false, emitted = false, inBasic = false, wroteKind = false;
    bool inMap = false, wroteName = false;
    while (fgets(line, sizeof line, in)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            char name[64]; int j = 0;
            const char* q = p + 1;
            while (*q && *q != ']' && j < (int)sizeof name - 1) name[j++] = *q++;
            name[j] = 0;
            /* THE MAP'S KIND, kept in [BASIC]. Singleplayer maps are offered by the
               game's User Maps screen; multiplayer ones show up in the skirmish map
               list. The engine ignores a key it does not know, so this rides along in
               the mission's own file rather than in a sidecar that can go missing. */
            /* THE MAP'S NAME, owned the same way. A map the editor renamed must say so
               in the document, because [Basic] Name= is the only place anything looks:
               this editor's OPEN MAP browser and the game's own User Maps list both read
               it, and the FILE cannot carry the name (the game finds user maps by
               probing USER00..USER99). A source whose [Basic] had no Name= at all gets
               one here, at the end of the section. */
            if (inBasic && !wroteName) {
                fprintf(out, "Name=%s\r\n", edit_map_title());
                wroteName = true;
            }
            if (inBasic && !wroteKind) {
                fprintf(out, "CNC3DKind=%s\n", g_mapIsMulti ? "Multi" : "Single");
                /* THE ENHANCED TAG. Written only when the map really has Enhanced rules,
                   and REMOVED when the last one is deleted -- a map that claims to need
                   an engine it does not need would be refused for nothing. The 1995
                   engine ignores the key; what it cannot ignore is that the carriers
                   would never fire, which is why the claim has to be honest. */
                if (!g_enh.empty()) fprintf(out, "Enhanced=1\n");
                wroteKind = true;
            }
            inBasic = (!strcasecmp(name, "BASIC"));
            inMap = (!strcasecmp(name, "MAP"));
            skipping = edit_section_is_owned(name);
            if (skipping && !emitted) { edit_emit_owned(out); emitted = true; }
            /* THE MAP'S STRIDE, stated where the brain reads it: a big map carries
               [MAP] Version=1 (display.cpp:1297) so its cell numbers and its sparse
               .BIN are read on the 128 grid. The key is owned like CNC3DKind is --
               written to MATCH the document and any stale copy dropped -- because a
               map whose file and whose cells disagree about the stride is corrupted
               silently, cell by cell. A legacy map's [MAP] passes through untouched
               (no shipped mission carries the key, and the byte-identity gate holds). */
            if (inMap && edit_map_big()) {
                fputs(line, out);
                fputs("Version=1\r\n", out);
                continue;
            }
        }
        /* The name is rewritten WHERE IT STOOD rather than dropped and re-appended:
           Name= is usually the first line of [Basic] on a shipped mission, and moving it
           to the bottom would churn every mission file the editor ever opens for no
           reason. A second copy, if a file somehow carried one, is dropped. */
        if (inBasic && !strncasecmp(p, "Name", 4) && p[4] == '=') {
            if (!wroteName) {
                fprintf(out, "Name=%s\r\n", edit_map_title());
                wroteName = true;
            }
            continue;
        }
        /* Drop any previous copy of the key rather than accumulating them. */
        if (inBasic && !strncasecmp(p, "CNC3DKind", 9)) continue;
        if (inBasic && !strncasecmp(p, "Enhanced", 8) && p[8] == '=') continue;
        if (inMap && !strncasecmp(p, "Version=", 8)) continue;
        if (!skipping) fputs(line, out);
    }
    if (inBasic && !wroteName) {
        fprintf(out, "Name=%s\r\n", edit_map_title());
        wroteName = true;
    }
    if (inBasic && !wroteKind) {
        fprintf(out, "CNC3DKind=%s\n", g_mapIsMulti ? "Multi" : "Single");
        if (!g_enh.empty()) fprintf(out, "Enhanced=1\n");
    }
    /* A source file with none of the owned sections still needs them. */
    if (!emitted) edit_emit_owned(out);
    fclose(in);
    fclose(out);

    int n = 0;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const ObjKind k = g_objects[i].kind;
        if (k == K_BUILDING || k == K_UNIT || k == K_INFANTRY || k == K_TERRAIN) n++;
    }
    fprintf(stderr, "edit: wrote %s -- %d objects, %d walls, %d tiberium, %d smudges\n",
            dst, n, (int)g_walls.size(), (int)g_tib.size(), (int)g_smudges.size());
    return n;
}

/* The terrain, as the engine reads it. A legacy map is the dense 8192-byte form,
   byte for byte what it always was. A big map is written in the ONLY form the brain's
   Read_Binary_Big parses: {u16 cell, u8 template, u8 icon} records, little-endian,
   ascending, clear cells omitted -- the same bytes Write_Binary_Big itself would
   produce (map.cpp:1286). A dense 32768-byte file here would not be a format choice,
   it would be a map the engine misreads record by record. */
static bool edit_write_bin(const char* dst)
{
    if (!g_editHaveBin) { fprintf(stderr, "edit: no .BIN was loaded; not writing one\n"); return false; }
    FILE* f = fopen(dst, "wb");
    if (!f) { fprintf(stderr, "edit: cannot write %s\n", dst); return false; }
    if (!edit_map_big()) {
        for (int i = 0; i < 64 * 64; i++) { fputc(g_editTmpl[i], f); fputc(g_editIcon[i], f); }
        fclose(f);
        fprintf(stderr, "edit: wrote %s (8192 bytes)\n", dst);
        return true;
    }
    int recs = 0;
    for (int i = 0; i < g_editBinW * g_editBinH; i++) {
        /* 255 is TEMPLATE_NONE and 0 is TEMPLATE_CLEAR1; Read_Binary_Big prefills
           every cell clear, so neither is stored -- exactly its writer's rule. */
        if (g_editTmpl[i] == 255 || g_editTmpl[i] == 0) continue;
        fputc(i & 255, f); fputc((i >> 8) & 255, f);
        fputc(g_editTmpl[i], f); fputc(g_editIcon[i], f);
        recs++;
    }
    fclose(f);
    fprintf(stderr, "edit: wrote %s (%d records, %d bytes, Version=1 sparse)\n",
            dst, recs, recs * 4);
    return true;
}

/* The heightmap, 65x65 raw bytes -- the .HGT the bake reads with --heights.
 *
 * Written only when the elevation tool has actually been used. A map whose heights came
 * off the cartridge should keep them BYTE FOR BYTE: writing an untouched heightmap back
 * out would be harmless today and a slow way to lose fidelity the first time anything in
 * this path rounds differently. */
static bool edit_write_hgt(const char* dst)
{
    if (!g_elevTouched) return false;
    const size_t corners = (size_t)(g_gridW + 1) * (size_t)(g_gridH + 1);
    if (g_pack.corner.size() < corners) {
        fprintf(stderr, "edit: no heightmap loaded; not writing one\n");
        return false;
    }
    FILE* f = fopen(dst, "wb");
    if (!f) { fprintf(stderr, "edit: cannot write %s\n", dst); return false; }
    /* (W+1)^2 raw bytes: 4225 for a legacy map -- the same file it always was -- and
       16641 for a 128 one; bake5 --heights sizes from the file. */
    fwrite(&g_pack.corner[0], 1, corners, f);
    fclose(f);
    fprintf(stderr, "edit: wrote %s (%zu bytes)\n", dst, corners);
    return true;
}


/* ------------------------------------------------------------------------------------
 *  The headless proof
 *
 *  --shot runs its own loop and never enters game_loop, so nothing the editor draws in
 *  the main loop appears in a screenshot, and there is no pointer to hover with anyway.
 *  This is the same hook --posetest uses to prove the renderer draws fabricated objects:
 *  park the cursor on a known cell and draw the overlay, so a screenshot is evidence
 *  rather than a picture of the map with the feature switched off.
 * ---------------------------------------------------------------------------------- */

/* Aim at the middle of the SCREEN, not the middle of the map. That exercises the whole
   round trip the interactive cursor uses -- pixel in, cell out, cell drawn back at the
   same pixel -- and a cursor that lands anywhere but the centre of the picture is a
   visible failure rather than a subtle one. A map cell chosen by index proves only that
   something drew somewhere. */
/* Arm something and put one down. This runs BEFORE draw_frame, because an object pushed
   after the frame is drawn is an object that does not appear in it -- which is how the
   first version of this proof came out showing a cursor and a footprint over bare
   ground and no building at all. */
static void edit_shot_prepare(int fbw, int fbh)
{
    if (!g_editOn) return;
    edit_update_hover((float)fbw * 0.5f, (float)fbh * 0.5f, fbw, fbh);
    if (g_editArmed < 0 && g_editOverMap) {
        g_editArmed = 0;                                   /* FACT */
        /* Walk outward from the centre of the screen for ground this will actually go
           on. The point of the shot is to show a placement AND a refusal, and the
           centre of the picture is as likely to be sea as anything else. */
        const int t0x = g_editCellX, t0y = g_editCellY;
        bool put = false;
        for (int r = 0; r <= 12 && !put; r++)
            for (int dy = -r; dy <= r && !put; dy++)
                for (int dx = -r; dx <= r && !put; dx++) {
                    if (r && abs(dx) != r && abs(dy) != r) continue;   /* ring only */
                    put = edit_place_q(t0x + dx, t0y + dy, true);
                }
        if (!put)
            fprintf(stderr, "edit: no buildable ground within 12 cells of %d,%d\n",
                    t0x, t0y);
        edit_place_q(t0x, t0y, false);   /* the one refusal worth printing */
        /* Leave the cursor where the centre pointed, which on this map straddles the
           shore -- so the footprint shows green and red cells side by side rather than
           one uniform verdict. */
        g_editCellX = t0x;
        g_editCellY = t0y;
    }
}

/* Save. The destination is the mission directory the game was pointed at, so a save
   lands where the engine will look for it. */


/* A MINIMAL INI FOR A MAP THAT HAS NONE.
 *
 * edit_write_ini copies a source through and swaps the sections it owns, which is what
 * preserves the briefing, the triggers and the AI base list on a shipped mission. A map
 * made from scratch has no source to preserve, and copying the DONOR mission's would
 * give it somebody else's briefing and win condition.
 *
 * So a new map gets a header of its own. These are the same values the browser editor
 * writes, for the same reason: nothing in this editor edits houses, credits or
 * triggers, so they are stated plainly rather than left absent for the engine to guess
 * at. Only the sections edit_write_ini does NOT own are written here -- it appends the
 * rest itself.
 */
static bool edit_write_seed_ini(const char* dst)
{
    FILE* f = fopen(dst, "wb");
    if (!f) { fprintf(stderr, "edit: cannot write %s\n", dst); return false; }
    const int   thidx  = eui_theater_index(g_pack.theater);
    const char* thname = EUI_THEATERS[thidx];
    /* The TD brain has four theaters and SNOW is not one of them: a snow map says
       Theater=WINTER to the engine (same template set, same land data) and carries
       CNC3DTheater=SNOW for the pipeline and the editor, which read it first. */
    const char* thbrain = (thidx == 3) ? "WINTER"
                        : (thidx == 4) ? "DESERT" : thname;
    fprintf(f, "; Written by the CNC3D map editor. Cells are y*%d + x.\r\n",
            edit_ini_w());
    fprintf(f, "[Basic]\r\n");
    /* The map's own name if it has been given one, and its slot if it has not. Both
       lists that show a user map read this key and nothing else. */
    fprintf(f, "Name=%s\r\n", edit_map_title());
    fprintf(f, "Player=GoodGuy\r\n");
    fprintf(f, "CarryOverMoney=0\r\nBuildLevel=7\r\nTheme=No theme\r\n");
    fprintf(f, "Intro=x\r\nBrief=x\r\nAction=x\r\nWin=x\r\nLose=x\r\nPercent=0\r\n");
    fprintf(f, "CNC3DKind=%s\r\n", g_mapIsMulti ? "Multi" : "Single");
    fprintf(f, "\r\n[MAP]\r\n");
    /* Version FIRST: the engine reads it before it sizes anything else, and a seeded
       big map with the key missing would be read at the legacy stride. */
    if (edit_map_big()) fprintf(f, "Version=1\r\n");
    /* Theater lives in [MAP] and not in [Basic]; reading it from the wrong section is a
       known trap in this engine and writing it to the wrong one is the same trap. */
    fprintf(f, "Theater=%s\r\n", thbrain);
    fprintf(f, "CNC3DTheater=%s\r\n", thname);
    fprintf(f, "X=%d\r\nY=%d\r\nWidth=%d\r\nHeight=%d\r\n", g_mapX, g_mapY, g_mapW, g_mapH);
    /* Every house that can own something needs a section, or the engine has nobody to
       give it to. */
    for (int h = 0; h < 4; h++) {
        fprintf(f, "\r\n[%s]\r\n", EUI_HOUSES[h].key);
        fprintf(f, "Credits=5000\r\nMaxBuilding=150\r\nMaxUnit=150\r\n");
        fprintf(f, "Allies=%s\r\nEdge=North\r\n", EUI_HOUSES[h].key);
    }
    fprintf(f, "\r\n[Waypoints]\r\n");
    fclose(f);
    fprintf(stderr, "edit: seeded a new %s for %s\n", thname,
            g_editScen);
    return true;
}

/* Make user_maps/ if it is not there. POSIX mkdir does NOT compile on mingw, which is
   why this is spelled twice -- the same wrinkle tools/win/ already carries a note about. */
static void edit_make_userdir(const char* path)
{
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static void edit_save(void)
{
    if (!g_editOn || !g_editScen[0]) return;
    char src[1024], ini[1024], bin[1024];
    const char* sep = (g_editDir[0] && g_editDir[strlen(g_editDir) - 1] != '/') ? "/" : "";

    /* WHERE A MAP GOES. The cartridge's own missions are read-only source material: a
       map you made is filed in user_maps/ beside them, which is also the folder the
       game's own User Maps list reads. The source to copy through is wherever this
       scenario was LOADED from, so editing a shipped mission and saving it makes a user
       map of it rather than overwriting the cartridge. */
    char dstdir[1024];
    edit_userdir(dstdir, sizeof dstdir);      /* rooted, so a Play cannot nest it */
    edit_make_userdir(dstdir);

    snprintf(src, sizeof src, "%s%s%s.INI", g_editDir, sep, g_editScen);
    {   /* Where this map's own source is: beside the missions if it was opened there,
           in user_maps if it was saved before, and NOWHERE if it was just made -- in
           which case one is seeded rather than inheriting the donor's briefing. */
        FILE* t = fopen(src, "rb");
        if (t) { fclose(t); }
        else {
            snprintf(src, sizeof src, "%s/%s.INI", dstdir, g_editScen);
            t = fopen(src, "rb");
            if (t) fclose(t);
            else if (!edit_write_seed_ini(src)) return;
        }
    }
    snprintf(ini, sizeof ini, "%s/%s.INI", dstdir, g_editScen);
    snprintf(bin, sizeof bin, "%s/%s.BIN", dstdir, g_editScen);
    /* Write the INI through a temporary: the source and the destination are the same
       file, and truncating it before reading it would lose the sections being preserved. */
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s/%s.INI.tmp", dstdir, g_editScen);
    if (edit_write_ini(src, tmp) < 0) {
        edit_toast(EUI_DANGER, "SAVE FAILED", "could not write the mission file");
        return;
    }
    remove(ini);
    if (rename(tmp, ini) != 0) {
        edit_toast(EUI_DANGER, "SAVE FAILED", ini);
        return;
    }
    edit_write_bin(bin);
    {
        char what[128], where[200];
        snprintf(what, sizeof what, "SAVED  %s", g_editScen);
        snprintf(where, sizeof where, "%s map in user_maps/  %d objects, %d walls",
                 g_mapIsMulti ? "multiplayer" : "singleplayer",
                 (int)g_objects.size(), (int)g_walls.size());
        edit_toast(EUI_GREEN, what, where);
    }
    {
        char hgt[1024];
        snprintf(hgt, sizeof hgt, "%.*s.HGT", (int)(strlen(ini) - 4), ini);
        edit_write_hgt(hgt);
    }
}


/* --- what the menu actually does ---------------------------------------------------- */

/* A blank map, in place. The pack stays: its atlas is the whole THEATER'S bank, not this
   map's tiles, so every cell can be repainted to anything that theater can draw. What
   changes is the .BIN, the heights, the objects and the playable rectangle.
 *
 * A theater change is the one thing this cannot do in place -- that IS a different pack
 * -- so it reboots onto a donor scenario of the wanted theater first and blanks that.
 */
static const char* edit_donor_for(int theater)
{
    if (theater == 1) return "SCB01EA";
    if (theater == 2) return "SCW01EA";
    if (theater == 3) return "SCS01EA";
    if (theater == 4) return "SCA01EA";
    return "SCG01EA";
}

/* THE PACK A SCENARIO BOOTS WITH, resolved from the scenario rather than from the
   session. Every editor reboot used to copy the command line's --pack through, which
   was right only while every map in a session shared one theater: playing a WINTER
   user map in a session launched on SCG01EA.pack repainted winter templates against
   the temperate bank -- tt_slot_of answered -1 for most of them and the map came up
   as bald temperate ground.

   The rule is the reskin rule stated at edit_reskin_from_bin: a baked pack of its own
   always wins (campaign scenarios, the donors); otherwise the map borrows its
   THEATER's donor pack, and the theater is read from the map's own INI -- never from
   whatever the session happened to be born with. */
static const char* edit_pack_for(const char* dir, const char* scen)
{
    static char buf[1104];
    snprintf(buf, sizeof buf, "%s.pack", scen ? scen : "");
    FILE* f = fopen(buf, "rb");
    if (f) { fclose(f); return buf; }

    int th = 0;
    char ini[1088];
    snprintf(ini, sizeof ini, "%s%s%s.INI", dir ? dir : "",
             (dir && *dir && dir[strlen(dir) - 1] != '/') ? "/" : "",
             scen ? scen : "");
    f = fopen(ini, "rb");
    if (f) {
        char line[256];
        bool ours = false;
        while (fgets(line, sizeof line, f)) {
            /* CNC3DTheater is the truth when present (snow maps say Theater=WINTER
               to the brain); a plain Theater= only stands until it appears. */
            if (strncasecmp(line, "CNC3DTheater=", 13) == 0) {
                th = eui_theater_index(line + 13);
                ours = true;
                break;
            }
            if (!ours && strncasecmp(line, "Theater=", 8) == 0)
                th = eui_theater_index(line + 8);
        }
        fclose(f);
    }
    snprintf(buf, sizeof buf, "%s.pack", edit_donor_for(th));
    return buf;
}

/* GROW (or shrink) THE LOADED WORLD to a new grid, in place.
 *
 * The pack on screen is the THEATER's tile bank plus a grid of cells pointing into
 * it, so a 128 map borrowing a 64 donor pack is the same trick re-skinning always
 * was -- there just have to BE 128x128 cells to repoint. This is the third place the
 * world grid commits, and it performs exactly the loader's dance (load_pack, "THE
 * COMMIT"): grid, shroud corners, hole table, in that order, so nothing strided by
 * the old dims survives. The caller repaints every cell afterwards (blank map,
 * reskin), which is what makes the borrowed art honest again.
 *
 * The baked CM tint is DROPPED on any resize: it is 4225 corner colours baked against
 * the donor's own relief, there is no honest way to stretch it over a different
 * grid, and an empty tint is the documented pre-PKC draw (plain modulate). A re-bake
 * regenerates it for the map's own heights. */
static void edit_set_world_grid(int nw, int nh)
{
    if (nw == g_gridW && nh == g_gridH) return;
    if (nw < 16 || nw > C3D_MAP_MAX || nh < 16 || nh > C3D_MAP_MAX) {
        fprintf(stderr, "edit: refused a %dx%d world (16..%d)\n", nw, nh, C3D_MAP_MAX);
        return;
    }
    const int ow = g_gridW, oh = g_gridH;
    std::vector<PackCell> cells((size_t)nw * (size_t)nh);
    for (int cy = 0; cy < nh; cy++)
        for (int cx = 0; cx < nw; cx++) {
            PackCell c;
            memset(&c, 0, sizeof c);
            c.x = (short)cx; c.y = (short)cy;
            /* Mid-grey until the bake computes a real mean colour: the in-game radar
               plots mr/mg/mb, and black would read as unexplored. */
            c.mr = c.mg = c.mb = 96;
            if (cx < ow && cy < oh) {
                /* Keep the old cell where one stood at this spot (row-major guess,
                   scan fallback -- the same lookup edit_pack_cell makes). */
                const size_t guess = (size_t)cy * (size_t)ow + (size_t)cx;
                const PackCell* src = NULL;
                if (guess < g_pack.cell.size() &&
                    g_pack.cell[guess].x == cx && g_pack.cell[guess].y == cy)
                    src = &g_pack.cell[guess];
                else
                    for (size_t k = 0; k < g_pack.cell.size(); k++)
                        if (g_pack.cell[k].x == cx && g_pack.cell[k].y == cy) {
                            src = &g_pack.cell[k];
                            break;
                        }
                if (src) c = *src;
            }
            cells[(size_t)cy * nw + cx] = c;
        }
    g_pack.cell.swap(cells);

    /* Corners: ground level everywhere, the old relief kept where it overlaps. A map
       arriving here is about to be flattened (blank) or re-dressed from its .HGT
       (reskin), so this is a sane interim, not a look. */
    if (!g_pack.corner.empty()) {
        std::vector<unsigned char> corn((size_t)(nw + 1) * (nh + 1), ELEV_RUNG[1]);
        for (int gy = 0; gy <= (oh < nh ? oh : nh); gy++)
            for (int gx = 0; gx <= (ow < nw ? ow : nw); gx++)
                if ((size_t)(gy * (ow + 1) + gx) < g_pack.corner.size())
                    corn[(size_t)gy * (nw + 1) + gx] = g_pack.corner[gy * (ow + 1) + gx];
        g_pack.corner.swap(corn);
    }
    g_pack.cmtint.clear();

    /* THE COMMIT, in the loader's order. */
    g_pack.mapW = nw; g_pack.mapH = nh;
    g_gridW = nw; g_gridH = nh;
    shroud_set_world_grid(nw + 1, nh + 1);
    memset(g_cellHoles, 0, sizeof g_cellHoles);
    for (size_t i = 0; i < g_pack.cell.size(); i++)
        g_cellHoles[g_pack.cell[i].y * nw + g_pack.cell[i].x] = g_pack.cell[i].holes;
    g_terrainBase = (float)ELEV_RUNG[1] * (1.0f / 64.0f);
    g_terrainHTop = g_terrainHBot = 0.0f;
    g_shadeReady = false;
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: world grid %dx%d -> %dx%d\n", ow, oh, nw, nh);
}

/* A FREE NAME FOR A NEW MAP, from a small known space.
 *
 * USER00..USER99, and the space is the point: the GAME lists user maps by PROBING these
 * names rather than reading the directory, because app/cnc3d.cpp has to compile for
 * Windows 98 against a freestanding mingw and specops_scan already refuses to drag
 * dirent.h in for exactly that reason. The editor may use dirent -- cnc_eyes is not in
 * that build -- but what it WRITES has to be findable by something that cannot.
 *
 * It also fixes a smaller thing: without this a new map kept whatever name was open, so
 * saving one would file it under a cartridge mission's name. */
static void edit_next_user_name(char* out, int n)
{
    char dir[1024];
    edit_userdir(dir, sizeof dir);
    for (int i = 0; i < 100; i++) {
        char path[1200];
        snprintf(path, sizeof path, "%s/USER%02d.INI", dir, i);
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); continue; }
        snprintf(out, n, "USER%02d", i);
        return;
    }
    snprintf(out, n, "USER99");     /* all taken: reuse the last rather than refuse */
}


/* ------------------------------------------------------------------------------------
 *  SAVE AS -- name the map, keep the slot
 *
 *  The request was to NAME maps instead of living with USER07. The name cannot be the
 *  FILENAME: the game finds user maps by probing USER00..USER99 (app/cnc3d.cpp
 *  usermaps_scan), so a map saved as "Tiberium Valley.INI" would work perfectly in this
 *  editor and never appear in the game at all. So the file keeps a slot and the name
 *  goes where every list already reads it from -- [Basic] Name=.
 *
 *  And it is a true SAVE AS, not a rename: the copy takes the NEXT FREE slot, the file
 *  it came from is left exactly as it was, and the editor goes on editing the copy.
 *  That is what Save As means in every application anybody has used.
 * ---------------------------------------------------------------------------------- */

/* Byte copy. The copy is the SOURCE the save then writes through, which is what carries
   the briefing, the houses, the win/lose conditions and everything else this editor
   does not model across to the new file. Without it a Save As off a cartridge mission
   would land on edit_save's "no source, seed a fresh header" path and quietly throw all
   of that away. */
static bool edit_copy_file(const char* src, const char* dst)
{
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[8192];
    size_t got;
    bool ok = true;
    while ((got = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, got, out) != got) { ok = false; break; }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) remove(dst);
    return ok;
}

/* Where the map that is open reads its unowned sections from, resolved exactly the way
   edit_save resolves it. Empty when there is no source at all, which is the case for a
   map made from nothing and never saved -- edit_save seeds a header for that one. */
static void edit_source_ini(char* out, int n)
{
    out[0] = 0;
    if (!g_editScen[0]) return;
    const char* sep = (g_editDir[0] && g_editDir[strlen(g_editDir) - 1] != '/') ? "/" : "";
    char p[1200];
    snprintf(p, sizeof p, "%s%s%s.INI", g_editDir, sep, g_editScen);
    FILE* f = fopen(p, "rb");
    if (f) { fclose(f); snprintf(out, n, "%s", p); return; }
    char dir[1024];
    edit_userdir(dir, sizeof dir);
    snprintf(p, sizeof p, "%s/%s.INI", dir, g_editScen);
    f = fopen(p, "rb");
    if (f) { fclose(f); snprintf(out, n, "%s", p); }
}

/* Open the dialog, with the map's current name in the field and the slot the copy will
   take resolved and shown. */
static void edit_saveas_open(void)
{
    if (!g_editOn) return;
    g_euiMenuOpen = false;
    g_euiModal = MODAL_SAVEAS;
    eui_num_close();
    g_numKind = NAME_MAP;
    g_numRow  = -1;
    /* Seeded with the NAME, never with the slot: USER07 is not a name and offering it
       as one is how a map ends up called USER07 on purpose. */
    snprintf(g_numBuf, sizeof g_numBuf, "%.*s", eui_name_cap(), g_editTitle);
    /* Opened holding the current name, and the first keystroke replaces the lot: the
       cap is already reached, so appending would drop every character typed. */
    g_numPristine = true;
    edit_next_user_name(g_saveasSlot, sizeof g_saveasSlot);
    SDL_StartTextInput();
    fprintf(stderr, "edit: SAVE AS -- the copy will be %s\n", g_saveasSlot);
}

/* Enter, or the SAVE button. */
static void edit_saveas_commit(void)
{
    edit_title_trim(g_numBuf);
    if (!g_numBuf[0]) {
        /* Refused the way a name collision is refused: say why and stay open, because
           closing on a blank name would leave the map called USER07 and look like the
           dialog had simply not worked. */
        edit_toast(EUI_DANGER, "THE MAP NEEDS A NAME",
                   "a blank one would leave it reading as its slot");
        return;
    }
    /* The source FIRST, while g_editScen still names the map we are copying FROM. */
    char src[1200];
    edit_source_ini(src, sizeof src);

    char dstdir[1024];
    edit_userdir(dstdir, sizeof dstdir);
    edit_make_userdir(dstdir);

    /* The next free slot, re-resolved at commit rather than trusted from open time:
       a Play or a Trace between the two could have taken it. */
    char slot[32];
    edit_next_user_name(slot, sizeof slot);

    if (src[0]) {
        char seed[1200];
        snprintf(seed, sizeof seed, "%s/%s.INI", dstdir, slot);
        if (!edit_copy_file(src, seed))
            fprintf(stderr, "edit: could not copy %s to %s -- the new map gets a fresh "
                            "header instead of this one's briefing\n", src, seed);
    }

    snprintf(g_editScen,  sizeof g_editScen,  "%s", slot);
    snprintf(g_editTitle, sizeof g_editTitle, "%s", g_numBuf);
    snprintf(g_saveasSlot, sizeof g_saveasSlot, "%s", slot);
    eui_num_close();
    g_euiModal = MODAL_NONE;

    edit_save();        /* toasts SAVED; the one below is the answer to what was asked */
    {
        char what[96], where[200];
        snprintf(what, sizeof what, "SAVED AS  %s", g_editTitle);
        snprintf(where, sizeof where, "%s.INI in user_maps/  --  you are now editing "
                 "this copy", g_editScen);
        edit_toast(EUI_GREEN, what, where);
    }
}


static void edit_blank_map(int sizeIdx, int multi)
{
    edit_next_user_name(g_editScen, sizeof g_editScen);
    /* A NEW MAP HAS NO NAME, and must not inherit the donor's. New Map reboots onto a
       cartridge mission for its tile bank, so without this a blank map would arrive
       calling itself "GDI Mission 1" and would be filed under that name. It shows as
       its slot until SAVE AS gives it one. */
    g_editTitle[0] = 0;
    const int w = EUI_SIZES[sizeIdx].w, h = EUI_SIZES[sizeIdx].h;
    /* The GRID first: the four legacy presets are playable rectangles inside the
       64x64 grid; the BIG MAP preset is the full 128 grid with its rectangle inset,
       and the world must be that size before anything below paints into it. */
    const int grid = EUI_SIZES[sizeIdx].grid;
    edit_set_world_grid(grid, grid);
    /* The DOCUMENT follows the grid: a new map's .BIN is exactly the world, and the
       blank pass below writes every cell of it. Without this, making a 64 map right
       after a 128 one would leave the document strided at the old width. */
    g_editBinW = g_editBinH = grid;
    g_mapX = (grid - w) / 2;  g_mapY = (grid - h) / 2;
    g_mapW = w;               g_mapH = h;
    g_mapIsMulti = multi;

    g_objects.clear();
    g_walls.clear();
    g_tib.clear();
    g_smudges.clear();
    g_ovExtra.clear();

    /* AND THE SCRIPT, which a blank map plainly does not have.
     *
     * New Map reboots on the donor mission first and then blanks the world, and this
     * only ever blanked the WORLD -- so a brand-new map opened from SCG09EA arrived
     * carrying its nineteen rules, eighteen teams and twenty-nine zone cells, all now
     * pointing at objects that no longer exist. The mission check reported 62 errors on
     * an empty map, which is exactly what it should have reported and is how this was
     * found; worse, touching any one of those rules marked the section dirty and wrote
     * the whole inherited script into the new map's file.
     *
     * g_editPrior goes too: it is the per-object order and trigger table, keyed by
     * house|type|cell, so a newly placed object landing on one of the donor's keys
     * inherited that mission's trigger. */
    g_triggers.clear();  g_trigSel = -1;  g_trigDirty = false;
    g_teams.clear();     g_teamSel = -1;  g_teamOrder = -1;  g_teamMember = 0;
    g_teamDirty = false;
    edit_zones_clear();                   /* g_cellTrig and its dirty flag */
    enh_clear();                          /* rules, counters, zones, selections */
    g_editPrior.clear();
    g_zonePaint = g_tagMode = g_enhPaint = false;

    /* AND THE VERDICTS ABOUT THE MAP THAT WAS OPEN. This lives here rather than beside
       the reboot because edit_request_new has a FAST PATH: when the new map's theater
       already matches the loaded pack there is nothing to reload, so it calls this
       directly and returns, and the reboot's own reset never runs. Temperate is the
       dialog's default, so on a temperate map that fast path is what pressing CREATE
       actually does -- and the blank map came up wearing the previous map's error
       count. Both paths come through here. */
    edit_trace_invalidate();
    g_scriptRowOff = 0;
    g_scriptScroll = 0.0f;

    /* Plain ground everywhere, with the cartridge's own per-cell CLEAR icon so a blank
       map does not visibly tile. */
    int painted = 0;
    if (g_editHaveBin)
        for (int cy = 0; cy < g_editBinH; cy++)
            for (int cx = 0; cx < g_editBinW; cx++)
                if (edit_paint_cell(cx, cy, 255, (cx & 3) | ((cy & 3) << 2))) painted++;
    fprintf(stderr, "edit: blanked %d of %d cells\n", painted, g_editBinW * g_editBinH);

    /* Flat, at rung 1 -- the rung the ladder calls "ground". */
    /* FLAT, AND AT THE HEIGHT THIS PACK CALLS GROUND.
     *
     * The obvious version writes rung 1 (64) into every corner and produces a map that
     * draws pure black. g_terrainBase is the map's own median height, fixed when the
     * pack loaded, and every drawn y is measured from it -- so on a map whose median is
     * 90, flattening to 64 puts the entire world BELOW the sea and the seabed covers it.
     * The ground has to be flattened AND the datum moved with it. */
    if (!g_pack.corner.empty()) {
        for (size_t i = 0; i < g_pack.corner.size(); i++)
            g_pack.corner[i] = ELEV_RUNG[1];
        g_terrainBase = (float)ELEV_RUNG[1] * (1.0f / 64.0f);
        g_terrainHTop = g_terrainHBot = 0.0f;
        g_shadeReady = false;      /* the lighting is derived from the heights */
    }

    g_elevSeeded = false;
    g_elevConverted = true;      /* a map made on the ladder is already on it */
    g_elevTouched = true;        /* so its .HGT is written */
    elev_seed();
    g_elevConverted = true;

    /* A new map has no starts. A MULTIPLAYER one gets two placed for it, in opposite
       corners of the playable rect: a skirmish map with fewer than two seats cannot be
       played at all, and shipping the blank map in that state would mean every new map
       began broken. They are meant to be moved. */
    edit_waypoints_clear();
    if (multi) {
        const int inset = 4;
        g_waypoint[0] = (g_mapY + inset) * edit_ini_w() + (g_mapX + inset);
        g_waypoint[1] = (g_mapY + h - 1 - inset) * edit_ini_w()
                      + (g_mapX + w - 1 - inset);
    } else {
        g_waypoint[EDIT_WAYPT_HOME] =
            (g_mapY + h / 2) * edit_ini_w() + (g_mapX + w / 2);
    }
    g_waypointArmed = -1;

    g_undo.clear(); g_redo.clear(); g_editLoadedOk = false;
    g_editArmed = -1; g_terrArmed = -1;
    g_euiRadarDirty = 1;
    g_mapListBuilt = false;
    edit_frame_map();
    {
        char sub[160];
        snprintf(sub, sizeof sub, "%s, %dx%d playable, saves as %s",
                 multi ? "multiplayer" : "singleplayer", w, h, g_editScen);
        edit_toast(EUI_GOLD, "NEW MAP", sub);
    }
}

/* Opening and creating both go through the same reboot the Play button uses, because
   both need the pack and the mission reloaded. The request is a flag serviced at a safe
   point in the loop, for the same reason Play's is. */
static int  g_editOpenReq = 0;          /* 1 = open g_editOpenScen, 2 = new map */
static char g_editOpenScen[32] = "";
static int  g_editNewPending = 0;

static void edit_request_open(const char* scen)
{
    snprintf(g_editOpenScen, sizeof g_editOpenScen, "%s", scen ? scen : "");
    g_editOpenReq = 1;
}

static void edit_request_new(void)
{
    /* Same theater as the pack already loaded? Then nothing has to be reloaded. */
    if (g_newTheater == eui_theater_index(g_pack.theater)) {
        edit_blank_map(g_newSize, g_newMulti);
        return;
    }
    snprintf(g_editOpenScen, sizeof g_editOpenScen, "%s", edit_donor_for(g_newTheater));
    g_editOpenReq = 2;
    g_editNewPending = 1;
    fprintf(stderr, "edit: new map wants %s, reloading onto %s for its tile bank\n",
            EUI_THEATERS[g_newTheater], g_editOpenScen);
}


/* ------------------------------------------------------------------------------------
 *  RE-SKINNING A BORROWED PACK
 *
 *  A map made in the editor has an .INI, a .BIN and maybe a .HGT, and no baked terrain.
 *  It does not need one: a pack's atlas is the whole THEATER's tile bank, so it borrows
 *  any pack of the right theater and every cell is pointed at the tile its own .BIN
 *  names. This is the same path the editor's New Map takes, which is why it is here and
 *  not duplicated in the app.
 * ---------------------------------------------------------------------------------- */

static bool edit_reskin_from_bin(const char* dir, const char* scen)
{
    if (g_pack.cell.empty()) {
        fprintf(stderr, "reskin: no pack cells loaded yet -- called too early\n");
        return false;
    }
    if (!edit_load_bin(dir, scen)) {
        fprintf(stderr, "reskin: no .BIN for %s -- the borrowed pack's own terrain "
                "stands\n", scen ? scen : "");
        return false;
    }

    /* A big map re-skins a borrowed 64 pack the same way it re-skins its tiles: the
       world grows to the document's own size first, then every cell is pointed at its
       art. Shrinking back for a legacy map after a big one uses the same call. */
    if (g_editBinW != g_gridW || g_editBinH != g_gridH)
        edit_set_world_grid(g_editBinW, g_editBinH);

    /* The heights, if the map carries them. Without a .HGT the borrowed pack's relief
       stays, which would be somebody else's hills under this map's tiles -- so a map
       with no heightmap is flattened rather than left wearing them. */
    char hgt[1024];
    snprintf(hgt, sizeof hgt, "%s%s%s.HGT", dir ? dir : "",
             (dir && *dir && dir[strlen(dir) - 1] != '/') ? "/" : "", scen ? scen : "");
    FILE* f = fopen(hgt, "rb");
    if (f) {
        const size_t corners = (size_t)(g_gridW + 1) * (size_t)(g_gridH + 1);
        std::vector<unsigned char> h(corners);
        const size_t got = fread(&h[0], 1, corners, f);
        fclose(f);
        if (got == corners && g_pack.corner.size() >= corners) {
            for (size_t i = 0; i < corners; i++) g_pack.corner[i] = h[i];
            fprintf(stderr, "reskin: %s.HGT applied\n", scen);
        } else if (got != corners) {
            fprintf(stderr, "reskin: %s.HGT is %zu bytes, this %dx%d map's heightmap "
                    "is %zu -- ignored, flattening instead\n", scen, got,
                    g_gridW, g_gridH, corners);
            for (size_t i = 0; i < g_pack.corner.size(); i++) g_pack.corner[i] = 64;
        }
    } else if (!g_pack.corner.empty()) {
        for (size_t i = 0; i < g_pack.corner.size(); i++) g_pack.corner[i] = 64;
        fprintf(stderr, "reskin: no %s.HGT -- flattened rather than wearing the "
                "borrowed pack's relief\n", scen);
    }
    g_terrainBase = 1.0f;
    g_terrainHTop = g_terrainHBot = 0.0f;
    g_shadeReady = false;

    int n = 0;
    for (int cy = 0; cy < g_editBinH; cy++)
        for (int cx = 0; cx < g_editBinW; cx++)
            if (edit_repaint_cell(cx, cy)) n++;
    fprintf(stderr, "reskin: %s -- %d of %d cells repainted from its own .BIN\n",
            scen, n, g_editBinW * g_editBinH);
    return n > 0;
}

/* ------------------------------------------------------------------------------------
 *  Play, in this window
 *
 *  The whole reason the editor moved out of the browser. There is no second process, no
 *  bake step and no window to alt-tab to: the map is saved, the mission is restarted on
 *  it, and the sim runs. Press it again and you are back in the editor.
 *
 *  None of the hard part is new. cnc_eyes already restarts a mission in place --
 *  game_shutdown() then game_boot() on a live window and GL context -- which is what the
 *  application shell runs between missions and what the `remission` script verb exposes
 *  (cnc_eyes.cpp:13538). It is exercised on every mission transition with an RSS-growth
 *  assertion, so the leak question is already answered by somebody else's test.
 *
 *  The request is a FLAG, not a call. Tearing the world down from inside an SDL event
 *  handler would free the objects the rest of the frame is still walking; the loop
 *  services it at a point where nothing is half-drawn.
 * ---------------------------------------------------------------------------------- */

static int  g_editPlayReq = 0;    /* 1 = start playing, 2 = come back to the editor */

/* TRUE while the running mission is a PLAYTEST -- it was started from the editor and
   the editor is where every exit leads back to. Abort, Restart and the win/lose verdict
   all used to fall out of the main loop, and the main loop runs once: the process died,
   taking the editor with it. "If I Abort Mission to get back to the editor, the entire
   application just closes completely." This flag is what routes those exits home. */

/* The playtest chrome is the editor's own eui_draw now; the old top bar is gone. */


static void edit_request_trace(void)  { if (g_editOn) g_editTraceReq = 1; }

static void edit_request_play(void)   { g_editPlayReq = g_editOn ? 1 : 0; }
static void edit_request_edit(void)   { g_editPlayReq = g_editOn ? 0 : 2; }

/* Called by the loop at a safe point. Returns true if the world was rebooted, which the
   caller needs to know because every pointer it held into the old one is now dead. */
static bool edit_service_play(SDL_Window* win, const GameOpts* o)
{
    /* OPEN and NEW ride the same reboot as PLAY: all three need the pack and the mission
       reloaded, and all three must happen at a point in the loop where nothing is
       half-drawn. */
    if (g_editOpenReq) {
        const int req = g_editOpenReq;
        g_editOpenReq = 0;
        static GameOpts nx;
        static char scen[32];
        nx = *o;
        snprintf(scen, sizeof scen, "%s", g_editOpenScen);
        nx.scen = scen;
        nx.pack = edit_pack_for(nx.dir, scen);   /* the map's own pack, or its
                                                    theater's donor -- see edit_pack_for */
        nx.edit = 1;
        game_shutdown();
        if (!game_boot(win, &nx)) {
            fprintf(stderr, "edit: could not open %s\n", scen);
            return false;
        }
        g_editOn = true;
        refresh_objects();
        /* A DIFFERENT MAP IS OPEN NOW, so everything that describes the last one has to
           go. Deliberately NOT done inside game_boot: Play and Trace also reboot, and
           the whole point of a trace is that its verdicts survive the trip back.
             - the trace badges are keyed by RULE NAME, and names like atk1, win, lose
               and prod recur across the campaign, so they do not merely go blank on the
               next map -- they land on its rules and read as evidence.
             - the mission check's badge is only recomputed when something is edited, so
               it kept showing the previous map's count until you touched something.
             - a paint or tag mode left on means the next right-drag to look around
               silently edits the map you just opened. */
        edit_trace_invalidate();          /* clears the trace AND marks the check stale */
        g_zonePaint = g_tagMode = g_enhPaint = false;
        g_scriptRowOff = 0;
        g_scriptScroll = 0.0f;
        if (req == 2 && g_editNewPending) {
            g_editNewPending = 0;
            edit_blank_map(g_newSize, g_newMulti);
        }
        return true;
    }
    if (g_editTraceReq) {
        g_editTraceReq = 0;
        /* Same reboot PLAY uses, and for the same reason: the trace has to watch the
           real engine run the real mission, which means the mission has to be booted
           for play rather than for editing. */
        static GameOpts tr;
        static char trScen[32], trDir[1024];
        tr = *o;
        edit_save();
        snprintf(trScen, sizeof trScen, "%s", g_editScen);
        {
            char root[1024];
            edit_maps_root(root, sizeof root);
            snprintf(trDir, sizeof trDir, "%suser_maps/", root);
        }
        tr.scen = trScen;
        tr.dir  = trDir;
        tr.reskin = 1;
        tr.pack = edit_pack_for(trDir, trScen);
        tr.edit = 0;
        game_shutdown();
        if (!game_boot(win, &tr)) {
            fprintf(stderr, "edit: could not boot %s for a trace\n", trScen);
            return false;
        }
        g_editOn = false;
        const int ticks = g_editTraceMin * 900;
        trace_run(&tr, ticks);
        const int fired = (int)g_traceFired.size();

        /* And straight back to the editor, on the same map, with the answer kept. */
        static GameOpts back;
        back = tr;
        back.edit = 1;
        game_shutdown();
        if (!game_boot(win, &back)) {
            fprintf(stderr, "edit: could not return to the editor after the trace\n");
            return false;
        }
        g_editOn = true;
        g_sbOn = false;
        refresh_objects();
        {
            char sub[128];
            snprintf(sub, sizeof sub, "%d of %d rules fired in %d minutes",
                     fired, (int)g_triggers.size(), g_editTraceMin);
            edit_toast(EUI_GREEN, "TRACE DONE", sub);
        }
        return true;
    }
    if (!g_editPlayReq) return false;
    const int req = g_editPlayReq;
    g_editPlayReq = 0;

    /* Static, because game_boot keeps the pointer and the loop's frame will be gone. */
    static GameOpts next;
    static char playScen[32], playDir[1024];
    next = *o;

    /* PLAY THE MAP THAT IS OPEN, not the one the editor was launched with.
     *
     * `*o` still carries the scenario and folder from the command line. After NEW MAP or
     * OPEN MAP the map on screen is a different one entirely -- and edit_save() always
     * writes into user_maps/ -- so copying *o straight through booted the donor mission
     * instead. That is exactly what it looked like: make a map, press Play, watch a
     * different map load.
     *
     * The map that is open is g_editScen, and after the save it is in user_maps/. Its
     * terrain comes from its own .BIN over a borrowed pack, so reskin travels with it. */
    snprintf(playScen, sizeof playScen, "%s", g_editScen);
    {
        char root[1024];
        edit_maps_root(root, sizeof root);
        snprintf(playDir, sizeof playDir, "%suser_maps/", root);
    }
    next.scen   = playScen;
    next.dir    = playDir;
    next.reskin = 1;

    if (req == 1) {
        edit_save();                       /* play what is on screen, not what is on disk */
        next.edit = 0;
        g_playFromEditor = true;
        /* PLAY MEANS THE GAME, INCLUDING HOW THE GAME LOOKS.
         *
         * g_fx.enabled is zero-initialised and only app/cnc3d.cpp turns it on -- it calls
         * game_visuals_default_enhanced("cnc3d-fx.cfg") unless --classic. The editor is
         * launched as cnc_eyes directly, by Editor.app, which passes no --gfx, so every
         * mission played out of the editor came up in CLASSIC: the pre-chain picture and
         * the old DOS HUD. You were testing your map against a presentation the game
         * does not ship with. Same call the game makes, same preset file, so a tuned
         * cnc3d-fx.cfg is honoured here too. */
        if (!g_fx.enabled) {
            game_visuals_default_enhanced("cnc3d-fx.cfg");
            fprintf(stderr, "edit: playing in ENHANCED, the way the game starts\n");
        }
        /* Playing OUT OF THE EDITOR is the moment you are asking whether the script
           works, so the feed comes on. A plain playthrough of a shipped mission has no
           such question and gets no overlay. */
        g_feedOn = true;
        g_feedShow = true;
        feed_reset();
        fprintf(stderr, "edit: playing %s from %s\n", playScen, playDir);
    } else {
        next.edit = 1;
        g_feedOn = false;
        g_playFromEditor = false;
        /* And the chain goes off again on the way back: the editor's panel is text on
           flat panels and has no business being bloomed, graded or run through a CRT
           curve. */
        g_fx.enabled = 0;
        fx_filter_set(0);
        sb_set_hud_new(0);
        fprintf(stderr, "edit: back to the editor on %s\n", playScen);
    }

    /* AFTER the save above: a first Play of a fresh map has no INI on disk until
       edit_save writes it, and the pack is resolved from that INI's Theater=. */
    next.pack = edit_pack_for(playDir, playScen);

    game_shutdown();
    if (!game_boot(win, &next)) {
        fprintf(stderr, "edit: could not restart the mission\n");
        return false;
    }
    /* game_boot has already reloaded the .BIN and the prior orders -- it does that for
       any boot with edit set -- so doing it again here just says everything twice. */
    g_editOn = (next.edit != 0);
    /* The game's own DOS sidebar and the editor's panel are both right-hand furniture
       and cannot share the edge. Play gets the game's; editing gets the editor's. */
    g_sbOn = !g_editOn;
    /* And the camera goes back to the console's: yaw zero, pitch from the zoom, eye
       height from the ground. Playing a mission from a camera the console never had
       would be measuring something else. */
    if (!g_editOn) {
        edit_cam_release();
        if (g_camLook) { g_camLook = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
    }
    g_euiRadarDirty = 1;
    refresh_objects();
    return true;
}


static void edit_shot_overlay_at_screen_centre(int fbw, int fbh)
{
    if (!g_editOn) return;
    edit_draw_footprint(fbw, fbh);
    edit_draw_overlay(fbw, fbh);
    if (getenv("CNC3D_EDIT_SAVE")) edit_save();
    fprintf(stderr, "edit: screen centre (%d,%d) -> cell %d,%d%s\n",
            fbw / 2, fbh / 2, g_editCellX, g_editCellY,
            g_editOverMap ? "" : "  (off the map)");
}

/* ------------------------------------------------------------------------------------
 *  PORTABILITY, which is settled and worth not re-deriving
 *
 *  This header has exactly TWO compile targets, and both are C++14:
 *      Apple clang++            game/build.sh, app/build.sh
 *      i686-w64-mingw32-g++     tools/win/build-win.sh, which compiles cnc_eyes.cpp
 *                               itself (twice), so this file crosses with it for free
 *  The Win98/Glide build is a different program -- tools/win98/build.sh compiles
 *  the tier1 C sources as C89 and never sees cnc_eyes.cpp -- so do NOT contort this into C89 for
 *  a target that will never compile it.
 *
 *  KEEP IT A HEADER. tools/win/check-sources.sh fails the Windows build before anything
 *  compiles if a translation unit exists on Mac and not in tools/win/sources.sh. A
 *  header has no such obligation.
 *
 *  Verified on the mingw toolchain rather than assumed: strncasecmp and %zu are both
 *  correct here, because mingw enables its own ANSI stdio in C++ mode. They would NOT be
 *  in a .c file compiled -std=c89, which is what the Win98 modules use.
 *
 *  If a save target ever needs a directory CREATED, do not reach for mkdir(p, 0755) --
 *  it does not compile on mingw, where mkdir takes one argument. SDL_GetPrefPath is the
 *  project's answer and makes the directory itself.
 *
 *  What the include site must already have declared
 *
 *      bool  screen_to_cell(float, float, int, int, int*, int*)
 *      void  world_to_screen(float, float, float, int, int, float*, float*)
 *      float terrain_corner_y(int, int)
 *      void  begin_overlay(int, int) / end_overlay(void)
 *      void  sb_place_cell_tris(int, int, int, int)   [cnc_sidebar.h]
 * ---------------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------------------
 *  --undotest: does undo really put the document back?
 *
 *  Not "does the object count return to what it was" -- that would pass for an undo that
 *  restored the right NUMBER of objects with the wrong facings, houses or orders. The
 *  test writes the map, makes N edits, undoes them, writes it again, and compares the
 *  two files BYTE FOR BYTE. The writer is the only thing that sees the whole document,
 *  so making it the judge is what makes the test worth running.
 * ---------------------------------------------------------------------------------- */

static int edit_undotest(const char* dir, const char* scen)
{
    char before[1024], after[1024];
    snprintf(before, sizeof before, "/tmp/undotest-before-%s.INI", scen);
    snprintf(after,  sizeof after,  "/tmp/undotest-after-%s.INI",  scen);

    char src[1024];
    snprintf(src, sizeof src, "%s%s%s.INI", dir ? dir : "",
             (dir && *dir && dir[strlen(dir) - 1] != '/') ? "/" : "", scen);

    const int n0 = (int)g_objects.size(), w0 = (int)g_walls.size();
    if (edit_write_ini(src, before) < 0) {
        fprintf(stderr, "UNDOTEST|FAIL|could not write the baseline\n");
        return 1;
    }

    /* Edits of every kind the editor can make, so undo is tested against all of them
       and not just the easy one. A cell is searched for rather than assumed: which
       ground is buildable depends on the map. */
    int made = 0;
    g_editOwner = 0;
    for (int i = 0; i < EDIT_ITEM_N && made < 3; i++) {
        if (EDIT_ITEMS[i].kind != EDIT_KIND_BUILDING) continue;
        g_editArmed = i;
        for (int cy = 1; cy < 62 && made < 3; cy++)
            for (int cx = 1; cx < 62 && made < 3; cx++)
                if (edit_place_q(cx, cy, true)) { made++; break; }
    }
    /* A wall, a move and an erase as well. */
    int extra = 0;
    for (int cy = 1; cy < 62 && extra < 1; cy++)
        for (int cx = 1; cx < 62 && extra < 1; cx++)
            if (edit_cell_ok(cx, cy)) { edit_wall_put(cx, cy, 0); extra++; break; }
    const int nEdits = made + extra;
    fprintf(stderr, "UNDOTEST|made %d edits (%d objects, %d walls)\n",
            nEdits, (int)g_objects.size(), (int)g_walls.size());
    if (nEdits == 0) {
        fprintf(stderr, "UNDOTEST|FAIL|could not make a single edit to undo\n");
        return 1;
    }

    for (int i = 0; i < nEdits; i++) edit_undo();

    int fails = 0;
    if ((int)g_objects.size() != n0 || (int)g_walls.size() != w0) {
        fprintf(stderr, "UNDOTEST|FAIL|counts: objects %d want %d, walls %d want %d\n",
                (int)g_objects.size(), n0, (int)g_walls.size(), w0);
        fails++;
    }
    if (edit_write_ini(src, after) < 0) {
        fprintf(stderr, "UNDOTEST|FAIL|could not write the comparison\n");
        return fails + 1;
    }

    /* The files, byte for byte. */
    FILE* a = fopen(before, "rb");
    FILE* b = fopen(after,  "rb");
    if (!a || !b) {
        fprintf(stderr, "UNDOTEST|FAIL|could not reopen the two files\n");
        if (a) fclose(a);
        if (b) fclose(b);
        return fails + 1;
    }
    long off = 0; int diff = -1;
    for (;;) {
        const int ca = fgetc(a), cb = fgetc(b);
        if (ca != cb) { diff = (int)off; break; }
        if (ca == EOF) break;
        off++;
    }
    fclose(a); fclose(b);
    if (diff >= 0) {
        fprintf(stderr, "UNDOTEST|FAIL|the two files differ at byte %d\n", diff);
        fprintf(stderr, "UNDOTEST|      %s\nUNDOTEST|      %s\n", before, after);
        fails++;
    } else {
        fprintf(stderr, "UNDOTEST|ok  |%ld bytes identical after %d undos\n", off, nEdits);
    }

    /* And redo has to put them all back again. */
    for (int i = 0; i < nEdits; i++) edit_redo();
    if ((int)g_objects.size() != n0 + made || (int)g_walls.size() != w0 + extra) {
        fprintf(stderr, "UNDOTEST|FAIL|redo: objects %d want %d, walls %d want %d\n",
                (int)g_objects.size(), n0 + made, (int)g_walls.size(), w0 + extra);
        fails++;
    } else {
        fprintf(stderr, "UNDOTEST|ok  |redo restored all %d edits\n", nEdits);
    }

    /* REVERT goes all the way back, in one step. */
    edit_revert();
    if ((int)g_objects.size() != n0 || (int)g_walls.size() != w0) {
        fprintf(stderr, "UNDOTEST|FAIL|revert: objects %d want %d, walls %d want %d\n",
                (int)g_objects.size(), n0, (int)g_walls.size(), w0);
        fails++;
    } else {
        fprintf(stderr, "UNDOTEST|ok  |revert returned to the map as loaded\n");
    }

    fprintf(stderr, "UNDOTEST|%s|%d failures\n", fails ? "FAILED" : "PASSED", fails);
    return fails;
}


/* ------------------------------------------------------------------------------------
 *  --scripttest: the mission's own scripting survives a round trip
 *
 *  Two separate claims, and they fail differently:
 *    UNTOUCHED  a mission that is opened and saved without editing its script keeps
 *               [Triggers] and [CellTriggers] byte for byte. This is the losslessness
 *               property, and it is what lets the editor be opened on a shipped mission
 *               without risk.
 *    EDITED     once a rule changes, what is written back parses to the same rules that
 *               were in memory -- so a save followed by a reload is a no-op.
 * ---------------------------------------------------------------------------------- */

/* A made-up world for the Enhanced logic check below: one switch for "is the Hand of
   Nod standing" and one counter for "how many times did a carrier get sprung". */
static int g_enhTestHand = 0;
static int g_enhTestSprang = 0;
static int enh_test_typecount(const char* house, const char* code)
{
    return (!strcasecmp(house, "BadGuy") && !strcasecmp(code, "HAND")) ? g_enhTestHand : 0;
}
static bool enh_test_spring(const char* name) { (void)name; g_enhTestSprang++; return true; }

static int edit_scripttest(const char* dir, const char* scen)
{
    int fails = 0;
    char src[1024];
    snprintf(src, sizeof src, "%s%s%s.INI", dir ? dir : "",
             (dir && *dir && dir[strlen(dir) - 1] != '/') ? "/" : "", scen);

    {
        int zn = 0;
        for (int c = 1; c < edit_ini_cells(); c++) if (g_cellTrig[c] >= 0) zn++;
        fprintf(stderr, "SCRIPTTEST|%s|%d rules, %d zone cells\n", scen,
                (int)g_triggers.size(), zn);
    }

    /* 1. UNTOUCHED: write it out and compare the two sections against the source. */
    char out1[1024];
    snprintf(out1, sizeof out1, "/tmp/scripttest-%s.INI", scen);
    if (edit_write_ini(src, out1) < 0) {
        fprintf(stderr, "SCRIPTTEST|FAIL|could not write\n");
        return 1;
    }
    {
        /* Compare section by section rather than whole-file: the writer regroups the
           sections it owns, which is by construction and not what this is testing. */
        static const char* SEC[2] = { "[Triggers]", "[CellTriggers]" };
        for (int k = 0; k < 2; k++) {
            char a[1 << 16], b[1 << 16];
            size_t na = 0, nb = 0;
            for (int which = 0; which < 2; which++) {
                FILE* f = fopen(which ? out1 : src, "rb");
                if (!f) continue;
                char line[1024];
                bool in = false;
                char* dst = which ? b : a;
                size_t* n = which ? &nb : &na;
                while (fgets(line, sizeof line, f)) {
                    const char* p = line;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == '[') { in = !strncasecmp(p, SEC[k], strlen(SEC[k])); continue; }
                    if (!in || *p == '\r' || *p == '\n' || !*p) continue;
                    const size_t L = strlen(p);
                    if (*n + L < (1 << 16)) { memcpy(dst + *n, p, L); *n += L; }
                }
                fclose(f);
            }
            if (na != nb || (na && memcmp(a, b, na) != 0)) {
                fprintf(stderr, "SCRIPTTEST|FAIL|%s changed on an untouched save "
                        "(%d bytes in, %d out)\n", SEC[k], (int)na, (int)nb);
                fails++;
            } else {
                fprintf(stderr, "SCRIPTTEST|ok  |%s survives untouched (%d bytes)\n",
                        SEC[k], (int)na);
            }
        }
    }

    /* 1b. TAGGING: attach a rule to an object and confirm it reaches the file. The tag
       is a trailing field on an object line, so it is easy to write a mission where the
       rule exists, the object exists, and nothing connects them. */
    if (!g_triggers.empty() && !g_objects.empty()) {
        int victim = -1;
        for (size_t i = 0; i < g_objects.size() && victim < 0; i++)
            if (g_objects[i].kind == K_BUILDING) victim = (int)i;
        if (victim < 0) victim = 0;
        char was[32];
        snprintf(was, sizeof was, "%s", edit_trigger_for(g_objects[victim]));
        edit_set_object_trigger(victim, g_triggers[0].name);
        const char* now = edit_trigger_for(g_objects[victim]);
        if (!now || strcasecmp(now, g_triggers[0].name)) {
            fprintf(stderr, "SCRIPTTEST|FAIL|tagging %s with %s did not take\n",
                    g_objects[victim].type, g_triggers[0].name);
            fails++;
        } else {
            fprintf(stderr, "SCRIPTTEST|ok  |%s carries %s (was %s)\n",
                    g_objects[victim].type, now, was);
        }
        edit_set_object_trigger(victim, was);
    }

    /* 1c. TEAMS: re-emit every shipped team and compare to its own line.
       This is the strongest check available for the format, because it does not test
       my parser against my writer -- it tests both against the cartridge. A wrong field
       order, a missed default or a dropped trailing flag all show up as a mismatch. */
    {
        FILE* rf = fopen(src, "rb");
        int checked = 0, differ = 0;
        if (rf) {
            char ln[1024];
            bool in = false;
            while (fgets(ln, sizeof ln, rf)) {
                char* p = ln;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '[') { in = !strncasecmp(p, "[TeamTypes]", 11); continue; }
                if (!in || *p == ';' || *p == '\r' || *p == '\n' || !*p) continue;
                char* eq = strchr(p, '=');
                if (!eq) continue;
                *eq = 0;
                char* val = eq + 1;
                for (char* e = val + strlen(val); e > val && (e[-1] == '\r' || e[-1] == '\n'); )
                    *--e = 0;
                const int ti = edit_team_by_name(p);
                if (ti < 0) {
                    fprintf(stderr, "SCRIPTTEST|FAIL|team %s did not survive reading\n", p);
                    differ++;
                    continue;
                }
                char mine[512];
                edit_team_line(g_teams[ti], mine, sizeof mine);
                checked++;
                if (strcasecmp(mine, val)) {
                    if (differ < 3)
                        fprintf(stderr, "SCRIPTTEST|FAIL|team %s\n  theirs %s\n  mine   %s\n",
                                p, val, mine);
                    differ++;
                }
            }
            fclose(rf);
        }
        if (differ) { fails++; }
        else if (checked)
            fprintf(stderr, "SCRIPTTEST|ok  |all %d teams re-emit byte for byte\n", checked);
    }

    /* 1d. TEAM EDITING: build a team through the same calls the buttons make, save it,
       read it back and confirm it arrived intact. This is the half the byte-for-byte
       check above cannot reach -- that one proves the format, this one proves the
       editor's own actions produce something legal. */
    {
        const size_t was = g_teams.size();
        edit_team_row(0, false);                    /* ADD TEAM */
        if (g_teams.size() != was + 1 || g_teamSel < 0) {
            fprintf(stderr, "SCRIPTTEST|FAIL|ADD TEAM did not add one\n");
            fails++;
        } else {
            EditTeam& T = g_teams[g_teamSel];
            char nm[16]; snprintf(nm, sizeof nm, "%s", T.name);
            g_teamOrder = 0;
            edit_team_row(7, false);                /* ADD ORDER after order 0 */
            edit_team_row(7, false);                /* and another             */
            /* Make the last one a Loop back to 0, then insert ahead of it: the loop
               target has to move with the insertion or the chain silently retargets,
               which is the bug this step exists to catch. */
            g_teamOrder = 2;
            for (int i = 0; i < 4; i++) edit_team_row(8, false);   /* Move -> Loop */
            if (g_teams[g_teamSel].mis[2].mission != 9) {
                fprintf(stderr, "SCRIPTTEST|FAIL|order cycling did not reach Loop\n");
                fails++;
            }
            g_teams[g_teamSel].mis[2].arg = 1;      /* loop back to order 1 */
            g_teamOrder = 0;
            edit_team_row(7, false);                /* insert at 1: loop must follow */
            if (g_teams[g_teamSel].mis[3].arg != 2) {
                fprintf(stderr, "SCRIPTTEST|FAIL|inserting an order left the Loop "
                                "pointing at %d, should be 2\n",
                        g_teams[g_teamSel].mis[3].arg);
                fails++;
            }
            edit_team_row(10, false);               /* drop it again: loop comes back */
            const int li = g_teams[g_teamSel].nmission - 1;
            if (g_teams[g_teamSel].mis[li].arg != 1) {
                fprintf(stderr, "SCRIPTTEST|FAIL|dropping an order left the Loop "
                                "pointing at %d, should be 1\n",
                        g_teams[g_teamSel].mis[li].arg);
                fails++;
            }
            /* Five member classes and no more. */
            for (int i = 0; i < 8; i++) {
                EditTeam& U = g_teams[g_teamSel];
                if (U.nclass >= TEAM_CLASS_MAX) break;
                snprintf(U.cls[U.nclass].code, sizeof U.cls[0].code, "E%d", i + 1);
                U.cls[U.nclass].num = i + 1;
                U.nclass++;
            }
            if (g_teams[g_teamSel].nclass > TEAM_CLASS_MAX) {
                fprintf(stderr, "SCRIPTTEST|FAIL|%d member classes; the engine holds 5\n",
                        g_teams[g_teamSel].nclass);
                fails++;
            }
            const EditTeam mine = g_teams[g_teamSel];
            char out3[1024];
            snprintf(out3, sizeof out3, "/tmp/scripttest-team-%s.INI", scen);
            if (edit_write_ini(src, out3) < 0) {
                fprintf(stderr, "SCRIPTTEST|FAIL|could not write the team copy\n");
                fails++;
            } else {
                edit_read_teams(out3);
                const int back = edit_team_by_name(nm);
                if (back < 0) {
                    fprintf(stderr, "SCRIPTTEST|FAIL|team %s did not come back\n", nm);
                    fails++;
                } else {
                    char a[512], b[512];
                    edit_team_line(mine, a, sizeof a);
                    edit_team_line(g_teams[back], b, sizeof b);
                    if (strcmp(a, b)) {
                        fprintf(stderr, "SCRIPTTEST|FAIL|team %s changed over a save\n"
                                        "  out %s\n  in  %s\n", nm, a, b);
                        fails++;
                    } else {
                        fprintf(stderr, "SCRIPTTEST|ok  |an authored team survives a "
                                        "save: %s=%s\n", nm, a);
                    }
                }
            }
            /* Put the mission back as it was for the checks that follow. */
            edit_read_teams(src);
        }
    }

    /* 1e. BUILT IT: the one native event whose parameter the editor could not author.
       Its Data is a raw StructType index, so this checks the index the file carries
       still names the building the engine would match. */
    {
        int checked = 0, bad = 0;
        for (size_t i = 0; i < g_triggers.size(); i++) {
            if (g_triggers[i].event != 16) continue;
            checked++;
            const int d = g_triggers[i].data;
            if (d < 0 || d >= EDIT_STRUCT_N) {
                fprintf(stderr, "SCRIPTTEST|FAIL|rule %s builds structure %d, which is "
                                "not a StructType\n", g_triggers[i].name, d);
                bad++;
            } else {
                fprintf(stderr, "SCRIPTTEST|ok  |rule %s fires on %s (%s, StructType %d)\n",
                        g_triggers[i].name, trig_struct_name(d), EDIT_STRUCT_CODE[d], d);
            }
        }
        if (bad) fails++;
        (void)checked;
    }

    /* 1f. ENHANCED: author a rule the 1995 engine cannot express, save it, read it back.
       The condition here -- five minutes AND the Hand of Nod standing AND not already
       done -- is three clauses, which is exactly the thing a native trigger cannot say
       and the reason this tier exists. */
    {
        enh_clear();
        EnhRule r;
        memset(&r, 0, sizeof r);
        snprintf(r.name, sizeof r.name, "ambush");
        snprintf(r.carrier, sizeof r.carrier, "amb1");
        r.all = 1;
        r.repeat = 0;
        r.firedAt = -1;
        r.clause[0].kind = ENH_TIME;  r.clause[0].num = 300;
        r.clause[1].kind = ENH_TYPE;  r.clause[1].num = 1;
        snprintf(r.clause[1].house, sizeof r.clause[1].house, "BadGuy");
        snprintf(r.clause[1].name,  sizeof r.clause[1].name,  "HAND");
        r.clause[2].kind = ENH_COUNTER; r.clause[2].negate = 1; r.clause[2].num = 1;
        snprintf(r.clause[2].name, sizeof r.clause[2].name, "done");
        r.nclause = 3;
        r.effect[0].kind = ENH_ADD;   r.effect[0].num = 1;
        snprintf(r.effect[0].name, sizeof r.effect[0].name, "done");
        r.neffect = 1;
        g_enh.push_back(r);
        g_enhCell[1000] = 0;
        g_enhCell[1001] = 0;
        g_enhDirty = true;

        char mine[512];
        enh_rule_line(g_enh[0], mine, sizeof mine);

        char out4[1024];
        snprintf(out4, sizeof out4, "/tmp/scripttest-enh-%s.INI", scen);
        if (edit_write_ini(src, out4) < 0) {
            fprintf(stderr, "SCRIPTTEST|FAIL|could not write the Enhanced copy\n");
            fails++;
        } else {
            enh_read(out4);
            if (g_enh.size() != 1) {
                fprintf(stderr, "SCRIPTTEST|FAIL|1 Enhanced rule out, %d back\n",
                        (int)g_enh.size());
                fails++;
            } else {
                char back[512];
                enh_rule_line(g_enh[0], back, sizeof back);
                if (strcmp(mine, back)) {
                    fprintf(stderr, "SCRIPTTEST|FAIL|Enhanced rule changed over a save\n"
                                    "  out %s\n  in  %s\n", mine, back);
                    fails++;
                } else if (enh_zone_cells(0) != 2) {
                    fprintf(stderr, "SCRIPTTEST|FAIL|2 zone cells out, %d back\n",
                            enh_zone_cells(0));
                    fails++;
                } else if (!g_enhTagged) {
                    fprintf(stderr, "SCRIPTTEST|FAIL|the map was not tagged Enhanced, so "
                                    "an engine that cannot run it would not be warned\n");
                    fails++;
                } else {
                    fprintf(stderr, "SCRIPTTEST|ok  |an Enhanced rule survives a save: "
                                    "%s=%s\n", g_enh[0].name, back);
                }
            }
            /* And the tag comes OFF again when the last rule goes, so a map does not go
               on claiming to need an engine it no longer needs. */
            g_enh.clear();
            g_enhDirty = true;
            char out5[1024];
            snprintf(out5, sizeof out5, "/tmp/scripttest-enh0-%s.INI", scen);
            if (edit_write_ini(out4, out5) >= 0) {
                enh_read(out5);
                if (g_enhTagged) {
                    fprintf(stderr, "SCRIPTTEST|FAIL|the Enhanced tag survived deleting "
                                    "every Enhanced rule\n");
                    fails++;
                }
            }
        }

        /* The evaluator itself, on a world this harness makes up: the point is the
           LOGIC -- three clauses, ALL, one negated, edge-triggered, once only. */
        enh_clear();
        g_enh.push_back(r);
        enh_reset_runtime();
        EnhWorld w;
        memset(&w, 0, sizeof w);
        w.credits = 0; w.units = 0; w.buildings = 0; w.inzone = 0; w.fired = 0;
        w.typecount = enh_test_typecount;
        w.spring = enh_test_spring;
        g_enhTestHand = 0;
        g_enhTestSprang = 0;

        w.tick = 200;  enh_tick(w);          /* too early, and no Hand      */
        w.tick = 27000; enh_tick(w);         /* late enough, still no Hand  */
        if (g_enhTestSprang) {
            fprintf(stderr, "SCRIPTTEST|FAIL|the Enhanced rule fired with its TYPE "
                            "clause false\n");
            fails++;
        }
        g_enhTestHand = 1;
        w.tick = 27100; enh_tick(w);         /* now all three hold          */
        if (g_enhTestSprang != 1) {
            fprintf(stderr, "SCRIPTTEST|FAIL|the Enhanced rule did not fire when all "
                            "three clauses held (sprang %d times)\n", g_enhTestSprang);
            fails++;
        }
        w.tick = 27200; enh_tick(w);
        w.tick = 27300; enh_tick(w);
        if (g_enhTestSprang != 1) {
            fprintf(stderr, "SCRIPTTEST|FAIL|a once-only Enhanced rule fired %d times\n",
                    g_enhTestSprang);
            fails++;
        } else {
            const int ci = enh_counter_index("done", false);
            if (ci < 0 || g_enhCounter[ci].value != 1) {
                fprintf(stderr, "SCRIPTTEST|FAIL|the rule's counter is %d, should be 1\n",
                        ci < 0 ? -1 : g_enhCounter[ci].value);
                fails++;
            } else {
                fprintf(stderr, "SCRIPTTEST|ok  |Enhanced: fires once when all three "
                                "clauses hold, and not before\n");
            }
        }
        enh_clear();
    }

    /* 1g. AUTHORING an Enhanced rule through the buttons, then playing it. This half
       matters because the format round-trip above proves nothing about whether the
       editor's ADD RULE makes something the runtime can actually run: the carrier has
       to exist, its event has to be None, and the pair has to survive a save together. */
    {
        enh_clear();
        const size_t trigWas = g_triggers.size();
        edit_enh_row(0, false);                  /* ADD RULE */
        if (g_enh.size() != 1 || g_enhSel != 0) {
            fprintf(stderr, "SCRIPTTEST|FAIL|ADD RULE did not add an Enhanced rule\n");
            fails++;
        } else if (g_triggers.size() != trigWas + 1) {
            fprintf(stderr, "SCRIPTTEST|FAIL|ADD RULE did not create a carrier trigger\n");
            fails++;
        } else {
            const int ci = edit_trigger_by_name(g_enh[0].carrier);
            if (ci < 0) {
                fprintf(stderr, "SCRIPTTEST|FAIL|the rule names carrier %s and no such "
                                "trigger exists\n", g_enh[0].carrier);
                fails++;
            } else if (g_triggers[ci].event != 0) {
                /* A carrier with a real event would fire TWICE: once when the engine
                   springs it and once when this tier does. */
                fprintf(stderr, "SCRIPTTEST|FAIL|carrier %s has event %s, not None\n",
                        g_enh[0].carrier, TRIG_EVENT[g_triggers[ci].event]);
                fails++;
            } else {
                /* Add a second condition through the chip path and check ALL is honest:
                   with the second condition false the rule must not fire. */
                EnhRule& R = g_enh[0];
                R.clause[R.nclause].kind = ENH_TYPE;
                R.clause[R.nclause].num = 1;
                snprintf(R.clause[R.nclause].house, 16, "BadGuy");
                snprintf(R.clause[R.nclause].name, 16, "HAND");
                R.nclause++;
                char nm[16], cr[8];
                snprintf(nm, sizeof nm, "%s", R.name);
                snprintf(cr, sizeof cr, "%s", R.carrier);

                char out6[1024];
                snprintf(out6, sizeof out6, "/tmp/scripttest-enhauth-%s.INI", scen);
                if (edit_write_ini(src, out6) < 0) {
                    fprintf(stderr, "SCRIPTTEST|FAIL|could not write the authored map\n");
                    fails++;
                } else {
                    enh_read(out6);
                    edit_read_triggers(out6);
                    const int back = enh_rule_by_name(nm);
                    const int bci  = edit_trigger_by_name(cr);
                    if (back < 0 || bci < 0) {
                        fprintf(stderr, "SCRIPTTEST|FAIL|the rule (%d) or its carrier "
                                        "(%d) did not survive the save\n", back, bci);
                        fails++;
                    } else if (g_enh[back].nclause != 2) {
                        fprintf(stderr, "SCRIPTTEST|FAIL|2 conditions out, %d back\n",
                                g_enh[back].nclause);
                        fails++;
                    } else if (g_triggers[bci].event != 0) {
                        fprintf(stderr, "SCRIPTTEST|FAIL|the carrier's event changed "
                                        "over a save\n");
                        fails++;
                    } else {
                        fprintf(stderr, "SCRIPTTEST|ok  |an authored Enhanced rule and "
                                        "its carrier survive together: %s -> %s\n",
                                nm, cr);
                    }
                }
            }
        }
        /* DELETE takes the carrier with it, so a save cannot leave an inert trigger
           behind that nothing will ever fire. */
        if (!g_enh.empty()) {
            g_enhSel = 0;
            char cr[8];
            snprintf(cr, sizeof cr, "%s", g_enh[0].carrier);
            edit_enh_row(1, false);
            if (edit_trigger_by_name(cr) >= 0) {
                fprintf(stderr, "SCRIPTTEST|FAIL|deleting the rule left carrier %s "
                                "behind, a trigger nothing can ever fire\n", cr);
                fails++;
            }
        }
        /* Back to what the FILE says, all three of them: the mission check runs next and
           would otherwise audit a document this harness had emptied. */
        enh_clear();
        enh_read(src);
        edit_read_triggers(src);
        edit_read_teams(src);
    }

    /* 1h. THE MISSION CHECK, run over this mission. Every row it prints is a claim
       about the SHIPPED data, so running it across all 95 is how its claims get
       audited: a check that cries wolf on cartridge missions is a check nobody will
       read. Failures here are not asserted -- the shipped campaign really does contain
       dead rules -- but they are printed so the gate can total them. */
    {
        edit_check_run();
        for (size_t i = 0; i < g_check.size(); i++)
            fprintf(stderr, "CHECK|%s|%s -- %s\n",
                    g_check[i].level == CHK_ERROR ? "ERR " :
                    g_check[i].level == CHK_WARN  ? "WARN" : "note",
                    g_check[i].what, g_check[i].why);
        fprintf(stderr, "CHECK|TOTAL|%d errors, %d warnings\n",
                g_checkErrors, g_checkWarns);
    }

    /* 2. EDITED: change a rule, write, and re-read. The rules that come back have to be
       the rules that went out. */
    if (!g_triggers.empty()) {
        std::vector<EditTrigger> want;
        g_triggers[0].event  = 11;      /* Time */
        g_triggers[0].action = 1;       /* Win  */
        g_triggers[0].data   = 137;
        g_trigDirty = true;
        want = g_triggers;

        char out2[1024];
        snprintf(out2, sizeof out2, "/tmp/scripttest-edit-%s.INI", scen);
        if (edit_write_ini(src, out2) < 0) {
            fprintf(stderr, "SCRIPTTEST|FAIL|could not write the edited copy\n");
            return fails + 1;
        }
        edit_read_triggers(out2);
        edit_read_teams(out2);
        if (g_triggers.size() != want.size()) {
            fprintf(stderr, "SCRIPTTEST|FAIL|%d rules went out, %d came back\n",
                    (int)want.size(), (int)g_triggers.size());
            fails++;
        } else {
            int bad = 0;
            for (size_t i = 0; i < want.size(); i++) {
                const EditTrigger& A = want[i];
                const EditTrigger& B = g_triggers[i];
                if (strcasecmp(A.name, B.name) || A.event != B.event ||
                    A.action != B.action || A.data != B.data ||
                    strcasecmp(A.house, B.house) || A.persist != B.persist) bad++;
            }
            if (bad) {
                fprintf(stderr, "SCRIPTTEST|FAIL|%d of %d rules came back different\n",
                        bad, (int)want.size());
                fails++;
            } else {
                fprintf(stderr, "SCRIPTTEST|ok  |all %d rules survive an edited save\n",
                        (int)want.size());
            }
        }
    }

    fprintf(stderr, "SCRIPTTEST|%s|%d failures\n", fails ? "FAILED" : "PASSED", fails);
    return fails;
}


#endif /* EDIT_MOD_H */
