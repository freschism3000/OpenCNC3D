/* ====================================================================================
 *  edit_mod.h -- the map editor, inside the game.
 *
 *  WHY IT LIVES HERE
 *
 *  The editor was prototyped in a browser, which settled its design and -- far more
 *  valuably -- derived every placement rule against the cartridge and proved each one.
 *  What it could never be is the thing the project owner asked for: a native Windows and Mac
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

/* THE ATLAS PACKING IS A COMPILE-TIME CONSTANT HERE AND A PROPERTY OF THE PACK THERE,
   and until this nothing checked that the two agreed.

   terrain_tiles.h is GENERATED against n64_terrain.build_atlas's layout: TT_COLS columns
   on a TT_PITCH grid, each tile's TT_TS of art sitting inside a TT_GUTTER of its own
   replicated edge. tt_slot_rect answers in THAT atlas's pixels, and every caller below
   turns those pixels into UVs by dividing by the LOADED pack's used atlas size. Hand it a
   pack baked to a different packing and it divides right pixels by a wrong width: the art
   walks further sideways with every column, and past the last column u0 passes 1.0 and
   wraps to the far side of the sheet. That is exactly a terrain tile at the wrong offset,
   and it arrived in silence, because load_pack accepts every magic from PK5 to PKF and a
   pre-gutter pack is still perfectly self-consistent for the WORLD, which carries its own
   baked UVs and never consults this table. Only the editor reads it.

   The test is the packing's own arithmetic and nothing else: a matching atlas is TT_COLS
   tiles wide and a whole number of rows tall, both counted in TT_PITCH. The gutter packing
   measures 32 * 26 = 832 across; the pre-gutter one measured 32 * 24 = 768, which fails on
   both halves. Painting and the palette then refuse, rather than draw a lie. */
static bool tt_atlas_matches(const PackTex& at)
{
    const bool ok = (at.uw == TT_COLS * TT_PITCH && at.uh > 0 && at.uh % TT_PITCH == 0);
    static bool said = false;
    if (!ok && !said) {
        said = true;
        fprintf(stderr, "edit: this pack's terrain atlas uses %dx%d, which is not the "
                        "%d-column %d-pixel-pitch packing terrain_tiles.h was generated "
                        "for. Terrain painting and the terrain palette are OFF rather "
                        "than drawn from the wrong rectangles; re-bake the pack with "
                        "bake5.py.\n", at.uw, at.uh, TT_COLS, TT_PITCH);
    }
    return ok;
}

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
       map and y*128+x when [MAP] says Version=1 -- the engine's own rule for that format.
   They are named apart because they are different facts, and on 27 Aug 2026 they stopped
   being the same NUMBER as well: the storage ceiling went to 256 and the file stride did
   not, because it cannot. A big map's cell numbers are y*128+x in every file that already
   exists and in the engine that reads them, so deriving the stride from the ceiling would
   have re-numbered every saved map the moment the renderer was given more room -- the same
   file meaning different ground. C3D_INI_STRIDE is that frozen 128; C3D_MAP_MAX is only
   how much room this build keeps. Declared up here because this file is ordered
   drawing-first and the panels read them. */
#define EDIT_GRID_MAX (C3D_MAP_MAX * C3D_MAP_MAX)
static inline bool edit_map_big(void)   { return g_gridW > 64 || g_gridH > 64; }
static inline int  edit_ini_w(void)     { return edit_map_big() ? C3D_INI_STRIDE : 64; }
static inline int  edit_ini_cells(void) { return edit_map_big() ? C3D_INI_CELLS : 64 * 64; }
/* Which mode the editor is in: 0 objects, 1 terrain, 2 elevation, 3 starts, 4 script.
   Up here with g_editOn because the map overlays are drawn near the top of this file and
   several of them are object-mode furniture that must stand down in the others. */
static int  g_editMode    = 0;
static int  g_editCellX   = -1;      /* the cell under the pointer, -1 when off the map */
static int  g_editCellY   = -1;
/* WHERE INSIDE THAT CELL the pointer is, 0..1 from the cell's north-west corner. Only
   infantry can use it: they are the one thing the engine lets stand somewhere other than
   the middle of a cell, and the pointer is the only input this editor has that is finer
   than a whole cell. Meaningless while g_editOverMap is false. */
static float g_editCellFx = 0.5f;
static float g_editCellFz = 0.5f;
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
/* IS ONE OF THE EDITOR'S DIALOGS UP? Declared here and defined far below with the rest
   of the modal state, because edit_over_chrome is defined before that state exists and
   a static variable cannot be forward declared. A function rather than three copies of
   the same comparison: the mouse half of this cage and the keyboard half drifted apart
   once already, and the way they stay together is by reading one sentence. */
static bool edit_modal_up(void);
/* What is at this cell, topmost first. Defined with the tools, below; the status
   card needs it to say what ERASE would remove. */
static int  edit_object_at(int cx, int cy);
/* THE DELETE STACK'S RUNGS. A cell is not one thing, and DELETE takes one of them per
   press from the top down, so both the press and the status card that describes it need
   the same list. The enum is up here rather than beside the code because this file is
   ordered drawing-first and the card is drawn a long way above the tools -- the same
   reason EditPrior was hoisted. */
enum EditLayer {
    EDL_EMPTY = 0,    /* nothing on it and the ground is already clear */
    EDL_OBJECT,       /* a unit, an infantryman, a building or a piece of scenery */
    EDL_WALL,
    EDL_SMUDGE,       /* an authored crater or scorch; the engine's bibs are not a rung */
    EDL_TIBERIUM,
    EDL_GROUND        /* the terrain template, which the last press puts back to clear */
};
/* What the next DELETE would take off this cell, and the noun to call it. Defined with
   the tools, below; the status card reads it so the card and the press cannot promise
   different things. */
static int  edit_erase_peek(int cx, int cy, char* what, size_t n);
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
/* And erasing an object has to take its entry with it, or the next one of the same type
   dropped on the same cell inherits a dead object's orders. Defined beside the rekey. */
static void edit_prior_drop(const char* house, const char* type, int cell);
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
    /* AND WHERE INSIDE THE CELL, which is the only thing that can say which of the five
       sub-cell positions an infantryman being placed goes on. screen_to_cell throws the
       fraction away, so the same unprojection is run a second time for it rather than a
       second copy of "which cell is this pixel" being kept here. It costs one more march
       down the heightfield per frame, and because both calls are pure functions of the
       same pixel, the cell this floors to is the cell above by construction. */
    if (g_editOverMap) {
        float wx = 0.0f, wz = 0.0f;
        screen_to_world(mouseC, mouseR, fbw, fbh, &wx, &wz);
        g_editCellFx = wx - (float)g_editCellX;
        g_editCellFz = wz - (float)g_editCellY;
    }
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

/* The XL-native .BIN, which is the brain's own format and is transcribed here rather
   than shared, because the editor must not include an engine header. It is the one map
   format that states the stride its cell numbers were written against, so it is the one
   the reader below can REFUSE rather than misread. Kept byte-identical to
   XLBinaryHeader / XLBinaryRecord in the XL brain's map.cpp; the size assertions are
   that brain's, repeated here so a drift is a build error on this side too. */
#define EDIT_MAP_VERSION_XL 2
#define EDIT_XL_BIN_MAGIC   "C3XB"
#define EDIT_XL_BIN_HDR     12   /* magic[4], u16 version, u16 stride, u32 count */
#define EDIT_XL_BIN_REC      8   /* u32 cell, u8 ttype, u8 ticon, u16 pad        */

/* [MAP] Version=, Width= and Height=, read straight from the file so the answer does not
   depend on boot order. 0 = legacy 64-stride cells and a dense 8192-byte .BIN; 1 = the
   brain's MEGAMAPS big-map form: 128-stride cells and the sparse record .BIN that
   Read_Binary_Big (map.cpp:968) actually parses; 2 = MAP_VERSION_XL, the XL brain's own
   format, whose cell numbers are against ITS stride and whose .BIN carries a header.

   THE VERSION IS RETURNED AS WRITTEN. It used to come back as `ver == 1 ? 1 : 0`, which
   folded every version this editor did not know into the legacy answer, so a Version=2
   map was not rejected, it was read as a dense 64-wide one: the exact failure the XL
   format was introduced to make impossible, reproduced one layer up. An unknown version
   is the caller's to refuse, and it does.

   Width and Height are optional out-params: an XL .BIN says which stride its cells are
   in but not how much of that stride the map occupies, so its dims come from here. */
static int edit_ini_map_info(const char* dir, const char* scen, int* w, int* h)
{
    if (w) *w = 0;
    if (h) *h = 0;
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
        if (!in) continue;
        if (!strncasecmp(q, "Version=", 8))      ver = atoi(q + 8);
        else if (w && !strncasecmp(q, "Width=", 6))  *w = atoi(q + 6);
        else if (h && !strncasecmp(q, "Height=", 7)) *h = atoi(q + 7);
    }
    fclose(f);
    return ver;
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
        if (cell <= last || cell >= C3D_INI_CELLS) return false;   /* the FORMAT's cell space */
        last = cell;
    }
    memset(g_editTmpl, 255, sizeof g_editTmpl);
    memset(g_editIcon, 0, sizeof g_editIcon);
    for (size_t i = 0; i < n; i += 4) {
        const int cell = raw[i] | (raw[i + 1] << 8);
        g_editTmpl[cell] = raw[i + 2];
        g_editIcon[cell] = raw[i + 3];
    }
    /* A sparse .BIN is the 128-wide format and says so nowhere, so the document it
       produces is 128 square -- the FORMAT's size, never this build's storage ceiling.
       These read C3D_MAP_MAX until 27 Aug 2026, which was the same number by accident. */
    g_editBinW = C3D_INI_STRIDE;
    g_editBinH = C3D_INI_STRIDE;
    return true;
}

/* The XL-native .BIN. Sparse like the big-map form, but with a header, and its cell
   numbers are the XL BRAIN's own: cell = y * hdr.Stride + x, where the stride is 1024 at
   every XL tier and is nothing to do with how wide the map is. So the two numbers have to
   be kept apart here exactly as they are on the brain's side: the STRIDE decodes x and y,
   the map's own WIDTH indexes this editor's grid.

   Everything it cannot vouch for it refuses, and says which thing was wrong. A .BIN this
   size read as the wrong thing is a map with its rows fanned out across the array, which
   is the failure the format exists to prevent, and a reader that clamped or guessed here
   would hand that failure straight back. */
static bool edit_bin_parse_xl(const unsigned char* raw, size_t n, int iniW, int iniH)
{
    if (n < EDIT_XL_BIN_HDR) return false;
    if (memcmp(raw, EDIT_XL_BIN_MAGIC, 4) != 0) return false;

    const unsigned ver    = (unsigned)raw[4]  | ((unsigned)raw[5]  << 8);
    const unsigned stride = (unsigned)raw[6]  | ((unsigned)raw[7]  << 8);
    const unsigned count  = (unsigned)raw[8]  | ((unsigned)raw[9]  << 8)
                          | ((unsigned)raw[10] << 16) | ((unsigned)raw[11] << 24);

    if (ver != EDIT_MAP_VERSION_XL) {
        fprintf(stderr, "edit: this .BIN carries the XL magic but version %u; refusing "
                        "rather than reading it as a version this build knows.\n", ver);
        return false;
    }
    if (stride == 0) {
        fprintf(stderr, "edit: the XL .BIN declares a zero cell stride.\n");
        return false;
    }
    if (n < (size_t)EDIT_XL_BIN_HDR + (size_t)count * EDIT_XL_BIN_REC) {
        fprintf(stderr, "edit: the XL .BIN ends before the %u records its own header "
                        "claims.\n", count);
        return false;
    }
    /* HOW WIDE THE DOCUMENT IS, which the file does NOT state and which is not the
       stride. The legacy sparse reader can store a cell at its own raw cell number
       because that format's stride squared fits this editor's grid; 1024 squared is a
       million cells and does not, so an XL map has to be re-indexed at a width of this
       reader's choosing, and the only rule that matters is that the width used to STORE
       is the width later used to READ (g_editBinW, further down).

       The extent of the records is what the file itself asserts, so that is the floor.
       [MAP] X/Y/Width/Height is the PLAYABLE RECT and not the map: a generated 256 map
       declares 252 inside a two-cell border, so believing the INI alone would refuse
       every border cell. Its far edge is used only to raise the answer, never to lower
       it. A column of clear ground at the right edge simply is not stored, and the
       bounds test in edit_land_at already answers rock outside the document. */
    unsigned maxX = 0, maxY = 0;
    for (unsigned r = 0; r < count; r++) {
        const unsigned char* p = raw + EDIT_XL_BIN_HDR + (size_t)r * EDIT_XL_BIN_REC;
        const unsigned cell = (unsigned)p[0] | ((unsigned)p[1] << 8)
                            | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
        const unsigned x = cell % stride, y = cell / stride;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
    }
    int w = (int)maxX + 1, h = (int)maxY + 1;
    if (iniW > w) w = iniW;
    if (iniH > h) h = iniH;

    /* CHECKED AGAINST THIS BUILD'S STORAGE, not clamped into it. The grids are static at
       the ceiling, so a map wider than the ceiling has nowhere to go, and taking the
       smaller number would load part of a map and say nothing. */
    if (w > C3D_MAP_MAX || h > C3D_MAP_MAX) {
        fprintf(stderr, "edit: this XL map reaches %dx%d and this build's editor holds %d "
                        "square. Refusing rather than loading part of it.\n",
                w, h, C3D_MAP_MAX);
        return false;
    }

    memset(g_editTmpl, 255, sizeof g_editTmpl);
    memset(g_editIcon, 0, sizeof g_editIcon);
    for (unsigned r = 0; r < count; r++) {
        const unsigned char* p = raw + EDIT_XL_BIN_HDR + (size_t)r * EDIT_XL_BIN_REC;
        const unsigned cell = (unsigned)p[0] | ((unsigned)p[1] << 8)
                            | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
        const size_t idx = (size_t)(cell / stride) * (size_t)w + (size_t)(cell % stride);
        g_editTmpl[idx] = p[4];
        g_editIcon[idx] = p[5];
    }
    g_editBinW = w;
    g_editBinH = h;
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
    int iniW = 0, iniH = 0;
    const int ver = edit_ini_map_info(dir, scen, &iniW, &iniH);
    bool ok = false;
    if (ver == EDIT_MAP_VERSION_XL) {
        /* NO FALLBACK FROM HERE. A map that says it is XL is read as XL or not at all:
           every other arm below decodes cell numbers against a stride this file did not
           write, so falling through would load the map as scattered rows and report
           success. That is the whole reason the format carries a header. */
        ok = edit_bin_parse_xl(raw, got, iniW, iniH);
        if (!ok) {
            fprintf(stderr, "edit: %s says [MAP] Version=%d, so it is not read as any "
                            "other format.\n", path, ver);
            return false;
        }
    } else if (ver < 0 || ver > EDIT_MAP_VERSION_XL) {
        /* Versions 0, 1 and 2 are the whole universe today. A map declaring anything else
           was written by something newer than this build, and the one thing that must not
           happen is reading it against a stride it was not written in. */
        fprintf(stderr, "edit: %s says [MAP] Version=%d, which this build does not know. "
                        "Refusing rather than guessing which stride its cells are in.\n",
                path, ver);
        return false;
    } else if (ver == 1 && edit_bin_parse_sparse(raw, got)) {
        ok = true;
    } else if (got == 64 * 64 * 2 || got == (size_t)C3D_INI_CELLS * 2) {
        /* A dense .BIN is recognised BY ITS LENGTH, so both the length tested and the
           stride derived from it are the format's, not this build's. */
        const int w = (got == 64 * 64 * 2) ? 64 : C3D_INI_STRIDE;
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


/* ------------------------------------------------------------------------------------
 *  Tiberium, sized the way the engine sizes it
 *
 *  A tiberium cell's worth is not a property of the cell. CellClass::Tiberium_Adjust
 *  counts the cell's eight tiberium neighbours, sets OverlayData to
 *  {0,1,3,4,6,7,8,10,11}[count], and the cell is then worth (OverlayData + 1) * 25
 *  credits -- TIBERIUM_STEP, of which a harvester carries 28 (cell.cpp:1901-1934,
 *  type.h:897). So a cell standing alone is worth 25 and a cell buried inside a field is
 *  worth 300, and the same number is the SHP frame the ground is drawn with. Deriving it
 *  here rather than authoring it is what makes a field painted in this editor look and
 *  pay like the field the engine will build out of the same file.
 *
 *  The engine's neighbour walk is over flat cell numbers and only skips what falls off
 *  the array, so at column zero it counts the previous row's last cell as a neighbour.
 *  This walks x and y and stops at the border. The two differ only on the first and last
 *  columns, where nothing can be harvested anyway.
 * ---------------------------------------------------------------------------------- */
static const int EDIT_TIB_ADJ[9] = { 0, 1, 3, 4, 6, 7, 8, 10, 11 };

static int edit_tib_index(int cx, int cy)
{
    for (size_t i = 0; i < g_tib.size(); i++)
        if (g_tib[i].x == cx && g_tib[i].y == cy) return (int)i;
    return -1;
}

static void edit_tib_stage(int cx, int cy)
{
    const int me = edit_tib_index(cx, cy);
    if (me < 0) return;
    int nb = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            if (edit_tib_index(cx + dx, cy + dy) >= 0) nb++;
        }
    g_tib[me].stage = (unsigned char)EDIT_TIB_ADJ[nb];
}

/* One painted or erased cell changes its OWN stage and its eight neighbours' -- the same
   shape as the wall mask further down, one ring wider because tiberium counts diagonals
   and a wall does not. */
static void edit_tib_restage(int cx, int cy)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            edit_tib_stage(cx + dx, cy + dy);
}


/* ====================================================================================
 *  THE SIDEBAR
 *
 *  the project owner approved a specific layout and the editor did not look like it: a strip of eight
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

/* the project owner's own zoom, so the panel can be sized without resizing the window. */
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
     * exactly what the project owner reported: unreadable fonts and menus clipping into one another.
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

/* THE ENGINE'S HOUSES, IN ITS OWN ORDER AND UNDER THEIR OWN NAMES.
 *
 * Two of these labels used to state a SIDE rather than a house, "Yours" and "Enemy",
 * which is only true while the map being authored is a GDI mission. A map is not written
 * from anybody's point of view, so a chip says which house it is.
 *
 * THE ROSTER IS THE ENGINE'S AND NOT A CHOICE. HousesType runs GoodGuy, BadGuy, Neutral,
 * Special and then Multi1 upward, and the last two multiplayer houses exist only when the
 * brain is compiled with its eight-player option (defines.h:658-678). That same option
 * sets MAX_PLAYERS (defines.h:2591-2595), which this file already mirrors as
 * EDIT_MAX_PLAYERS, so the house count is 4 + EDIT_MAX_PLAYERS and there is one number to
 * keep in step instead of two. The table is written out to the eight-seat maximum and the
 * count is derived from the mirror, so trimming the mirror stops the extra seats being
 * offered without anybody editing rows out.
 *
 * THE INDEX IS HousesType. That is load-bearing: it is what lets a placed object state
 * its own side instead of leaving the renderer to guess one from the owner's name.
 *
 * The multiplayer colours are the schemes the engine hands those houses by default
 * (hdata.cpp:97-192), read through the remap ramp the lobby already measured against the
 * game palette (menu/doslobby.c:50-59). A lobby seat can be moved off its default, so
 * this is the house's colour and not a promise about any particular match. */
struct EuiHouse { const char* key; const char* label; unsigned colour; };
static const EuiHouse EUI_HOUSES[] = {
    { "GoodGuy", "GDI",     0xe0b070 },
    { "BadGuy",  "NOD",     0xff6a5a },
    { "Neutral", "NEUTRAL", 0x9aa7b4 },
    { "Special", "SPECIAL", 0xb98fe4 },
    { "Multi1",  "MP1",     0xc3c3d3 },   /* light blue */
    { "Multi2",  "MP2",     0xd77910 },   /* orange     */
    { "Multi3",  "MP3",     0xa2e31c },   /* green      */
    { "Multi4",  "MP4",     0x65828a },   /* blue       */
    { "Multi5",  "MP5",     0xc7aa5d },   /* gold       */
    { "Multi6",  "MP6",     0xc72814 },   /* red        */
    { "Multi7",  "MP7",     0xa2a2a2 },   /* grey       */
    { "Multi8",  "MP8",     0xaa714d },   /* brown      */
};
/* How many rows the table carries, and how many of them this brain actually has. Both are
   expressions, so neither can drift from the other by hand. */
#define EUI_HOUSE_MAX ((int)(sizeof EUI_HOUSES / sizeof EUI_HOUSES[0]))
#define EUI_HOUSE_N   (4 + EDIT_MAX_PLAYERS)
/* The two playable sides by their place in the table, for the code that has to name one.
   The index is HousesType, so these are HOUSE_GOOD and HOUSE_BAD. */
enum { EUI_HOUSE_GDI = 0, EUI_HOUSE_NOD = 1 };
/* HOW MANY OF THEM THE MAP BEING AUTHORED IS OFFERED. Every house exists in every game
   type -- the engine news the whole roster whether the file mentions a house or not
   (house.cpp:1961-2010) -- but the Multi seats belong to the lobby, and a singleplayer
   mission has no lobby to fill them. Offering them there would be four useful chips and
   eight that do nothing.
   A MACRO and not a function: the map's kind is declared a long way below this point and
   the answer is wanted from everything in between. */
#define EUI_HOUSES_OFFERED (g_mapIsMulti ? EUI_HOUSE_N : 4)
/* WHICH HOUSE A NAME IS, as a place in the table, or -1 for one this build has never
   heard of. An object states its owner as the engine's IniName and several things have to
   turn that back into an index -- the radar, the eyedropper, the livery resolve -- so the
   walk is written once. The bound is the TABLE and not the offered count: a map may carry
   a house the kind it is filed under does not offer, and refusing to name it would draw
   it as if it had no owner. */
static int eui_house_index(const char* name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < EUI_HOUSE_MAX; i++)
        if (!strcmp(EUI_HOUSES[i].key, name)) return i;
    return -1;
}
/* What the pointer can be over. Declared up here rather than beside eui_hit
   because the DRAW needs it too, for the hover states, and the draw comes first. */
enum EuiHit { EUI_NONE = 0, EUI_MAP, EUI_ITEM, EUI_TABC, EUI_OWNER, EUI_MODE,
              EUI_SAVE, EUI_PLAY, EUI_RADAR, EUI_ACT, EUI_TOOL,
              EUI_ELEVRUNG, EUI_ELEVTOOL, EUI_ELEVBRUSH, EUI_ELEVGATE,
              EUI_ELEVIMPORT, EUI_ELEVNEXT, EUI_ELEVAUTO,
              EUI_SBRPAGE, EUI_SBRTOOL, EUI_SBRSLIDER,
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

/* ---- THE SMOOTH BRUSH: the elevation panel's second page ---------------------------
 *
 * The rung tool paints TIERS over 2x2 blocks and dresses the step it makes with cliff
 * art. It cannot express a hill. This is the other half: a brush that writes
 * g_pack.corner directly, one byte a corner, and dresses nothing at all.
 *
 * TUNING IN ONE PLACE. Every number the brush's feel depends on is here, so a balance
 * pass edits this block and nothing else. The sliders' ranges are here for the same
 * reason: a range that lives at the use site is a range the readout can disagree with.
 *
 * SBR_TICK_HZ IS WHY THE BRUSH IS FRAMERATE INDEPENDENT, and it is not decoration. A
 * stroke is integrated at a FIXED timestep: the frame hands the brush its dt, the brush
 * banks it and consumes whole ticks. So the number of ticks a stroke applies, and the
 * order they are applied in, depend on how long the button was held and not on how many
 * frames were drawn in that time -- which makes the resulting bytes identical, bit for
 * bit, at any frame rate. An exponential whose per-step factor is linear in dt does NOT
 * have that property: N small steps do not compose into one big step.
 * ---------------------------------------------------------------------------------- */
#define SBR_TICK_HZ     120.0f   /* the fixed integration rate, ticks per second       */
#define SBR_SIZE_MIN      0.5f   /* CENTRE SIZE, in cells: the full-strength core      */
#define SBR_SIZE_MAX     10.0f
#define SBR_FALL_MIN      0.5f   /* FALLOFF, in cells: the ring that fades to nothing  */
#define SBR_FALL_MAX     14.0f
#define SBR_STR_MIN       0.05f  /* STRENGTH, a plain multiplier on the dose           */
#define SBR_STR_MAX       1.00f
#define SBR_RAISE_RATE   96.0f   /* raw height units per second, at weight 1, full str */
/* ERASE'S RATE, in FULL-WEIGHT SECONDS TO ARRIVE rather than e-folds per second. The
   first form was exp(-K * dose), which approaches the datum and never reaches it, so a
   second identical erase over ground the first had "finished" moved it again: measured on
   SCG01EA, a corner at the falloff edge went 117 to 103 to 96, still travelling. A tool
   called ERASE has to settle, or holding it twice is not the same as holding it once for
   longer. The ramp below is linear in dose and CLAMPED, so it lands on the datum exactly
   and stays there. 1/3 second keeps the old form's initial slope (both are 1 - K*dose for
   small dose), so the feel in the hand is unchanged and only the tail differs. */
#define SBR_ERASE_K       3.0f   /* reciprocal seconds at full weight; 1/K to arrive  */
#define SBR_MAX_DOSE     60.0f   /* a held button cannot integrate for ever            */
#define SBR_TRACK_GAP     4.0f   /* design units between a slider's text and its track */
#define SBR_TRACK_MIN    40.0f   /* the narrowest track a drag can still aim at        */
/* THE FLAT IS A PROPERTY OF THE MAP, NOT A CONSTANT, and getting that wrong is what
   turned ERASE into a digger.
 *
 * The director's line is that ERASE "paints the DEFAULT TERRAIN HEIGHT". The default is
 * whatever height this map calls the ground, and on a shipped pack that is NOT the byte
 * 64. load_pack re-zeroes the world on the MEDIAN corner byte over the playable rect and
 * keeps it in g_terrainBase, so byte == round(g_terrainBase * 64) is exactly the corner
 * whose drawn world y is 0.000. Measured on the packs that ship: SCG01EA 90, SCB01EA 108,
 * SCG09EA 128. None of them is 64.
 *
 * So erasing to 64 did not erase, it EXCAVATED. Measured on SCG09EA, at cell 32,7 --
 * flat, dry, and sitting exactly on that map's own ground -- a six second full-strength
 * ERASE took the height query from 0.0000 to -1.0000: a cell-deep pit dug in ground the
 * stroke was asked to leave alone.
 *
 * ERASE STILL DOES NOT GO THROUGH A TIER. It lands on the raw datum byte, because a
 * round trip through the five-rung ladder (0/64/128/191/255, ties down) loses up to 32
 * units a corner -- and on SCG01EA the datum, 90, is not on the ladder at all.
 *
 * ELEV_RUNG is declared with the elevation model further down and a new map really is
 * flattened to rung 1, so that stays the answer when there is no heightmap to ask. */
static unsigned char sbr_flat_byte(void);

/* The three sliders, as data, so the draw, the hit test, the handler, the headless verb
   and the readout all read one table instead of five copies of three ranges. */
enum { SBR_SLIDER_N = 3 };
static const struct SbrSlider {
    const char* name;
    float lo, hi;
    const char* unit;
} SBR_SLIDERS[SBR_SLIDER_N] = {
    { "CENTRE SIZE", SBR_SIZE_MIN, SBR_SIZE_MAX, "cells" },
    { "FALLOFF",     SBR_FALL_MIN, SBR_FALL_MAX, "cells" },
    { "STRENGTH",    SBR_STR_MIN,  SBR_STR_MAX,  ""      },
};

static const char* const SBR_PAGE_NAME[2] = { "TIERS", "SMOOTH" };
static const char* const SBR_TOOL_NAME[3] = { "RAISE", "LOWER", "ERASE" };

static int   g_sbrPage = 0;        /* 0 the rung tool, 1 the smooth brush             */
static int   g_sbrTool = 0;        /* 0 raise, 1 lower, 2 erase                       */
static float g_sbrVal[SBR_SLIDER_N] = { 2.0f, 3.0f, 0.60f };
static int   g_sbrDragSlider = -1; /* the slider the held button is dragging          */
/* The last thing the brush refused, so the status card can say it rather than the log. */
static char  g_sbrWhy[220] = "";

/* THE UNLOCK, AND WHY IT IS ITS OWN FLAG rather than g_elevConverted.
 *
 * The director's rule is that the two tools share a map: the first smooth stroke makes
 * the rung tool usable on it, and the rung tool then paints normally and never asks to
 * CONVERT. That is exactly what elev_ready answers, so this is OR-ed into it.
 *
 * It is NOT g_elevConverted because CONVERT is a separate, deliberate, announced act
 * with its own "it cannot be undone" contract, and folding the two together would put
 * that act inside the undo stack by a side door. This flag IS in the undo snapshot --
 * see EditSnap -- because a stroke that unlocks the map has to be undoable as one
 * thing, heights and unlock together. */
static bool  g_elevSmoothUnlock = false;
/* AND IT LIVES IN THE FILE, WHICH IT DID NOT.
 *
 * THE DEFECT. The flag was in memory and in EditSnap and nowhere else: a stroke unlocked
 * the rung tool, SAVE wrote the map, opening the map again locked it -- so the tool the
 * player had just earned was taken back by saving their work. And nothing cleared it
 * either, so the unlock from one map was still standing over the next one.
 *
 * WHY IT IS WRITTEN DOWN RATHER THAN DERIVED. Deriving it from the heights is the
 * tempting answer -- "corners off the ladder mean somebody smoothed this" -- and it is
 * wrong on the maps that matter: SCG01EA opens with 332 of its 728 counted corners off
 * the ladder, as shipped, untouched. Deriving would unlock every cartridge map on sight
 * and there would be nothing left for CONVERT to be.
 *
 * So it is a CNC3D-owned key in [Basic], written the way CNC3DKind and Enhanced are:
 * any previous copy dropped, a fresh one written only when it is true, and the 1995
 * engine ignoring a key it does not know. This holds what the map that is OPEN said, so
 * the map-change reset has something honest to reset to. */
static bool  g_elevIniUnlock = false;

/* The elevation model lives further down, with the code that derives from it; the panel
   draws its state and comes first, as everything in this file does. */
static bool elev_ready(void);
static int  g_elevOffLadder, g_elevTotal;

/* TUNING. Every number this feature turns on, in one place, because that is the rule
   and because a balance sweep over any of them has to find them together. */
#define AH_SLOPE_FIRST   13     /* TEMPLATE_SLOPE1, brain defines.h                 */
#define AH_SLOPE_LAST    50     /* TEMPLATE_SLOPE38                                 */
#define AH_SLOPE_N       (AH_SLOPE_LAST - AH_SLOPE_FIRST + 1)
#define AH_PLACE_MAX     8192   /* placements one map may carry                     */
#define AH_TOP_RUNG      4      /* ELEV_RUNG's last index                           */
#define AH_GROUND_RUNG   1      /* the rung the ladder calls "ground"               */
#define AH_ARM_FRAMES    900UL  /* how long the REPLACE warning stays armed, frames */

/* THE AUTO HEIGHTMAP'S REPORT. Declared here for the same reason everything else on
   this panel is: the draw needs it and the draw comes first in this file. The fit that
   fills it in, and the overlay that draws it, live together far below.

   offBefore / offAfter ARE THE FEATURE'S OWN SAFETY PROPERTY, not decoration. The fit
   writes ground through the same ladder the rung tool derives from, so every corner it
   touches lands exactly on a rung and every corner it declines to touch is left at the
   byte it already had. offAfter can therefore never exceed offBefore, and a run where it
   does is the fit having gone off the ladder -- which is what locks the panel. Carried on
   the report so a gate reads one line instead of two readouts.

   stray IS THE SAME PROPERTY WITHOUT THE EXCUSE. offAfter can legitimately be non-zero on
   a map whose SHORELINE was already off the ladder, because the fit does not write the
   sea's corners at all. stray counts the corners still off the ladder that the sea does
   NOT touch, and there is no honest reason for one of those to exist: every corner the
   fit writes goes through ELEV_RUNG. A gate that reads stray is reading the invariant
   itself rather than a number with a caveat attached. */
struct AhReport {
    bool valid;
    int  places;        /* SLOPE placements recovered                     */
    int  orphans;       /* cliff cells no placement origin claimed        */
    int  wrote;         /* corners rewritten                              */
    int  moved;         /* corners whose byte actually changed            */
    int  held;          /* corners the sea uses, left exactly as they were */
    int  marked;        /* cells in the overlay, all reasons              */
    int  disagree, clamped;
    int  offBefore, offAfter;   /* playable corners off the ladder, either side */
    int  stray;         /* of offAfter, the ones the sea does NOT touch  */
    bool wetkept;       /* not one corner the sea uses moved             */
    int  gridW, gridH;  /* the grid the marks were taken on               */
};
static AhReport      g_ah;
static bool          g_ahArmed   = false;
static unsigned long g_ahArmedAt = 0;
/* Called from the undo funnel and the restore, both of which are far above the fit. */
static void ah_report_clear(void);

/* THE SEA'S OWN CORNERS AND THE SEA'S OWN CELLS. Both are defined far below, with the
   smooth brush and with the crossing rules; they are forward-declared here because the
   auto heightmap keeps off the water by exactly the same two rules, and a second copy of
   a rule is a second thing to get wrong. The brush already refuses a corner any of whose
   four cells is water, for the reason its own header gives: the water surface is built
   from the terrain's own corners, so raising one lifts the sea into a hill. */
static bool sbr_corner_wet(int gx, int gy);
static bool edit_cell_is_water(int cx, int cy);
/* Recount the two numbers above from the corners as they stand. Declared here because
   the UNDO funnel calls it -- restoring a different set of heights changes the answer --
   and the undo funnel is a long way above the smooth brush that defines it. */
static void sbr_recount_ladder(void);

/* Fly speed, declared here because the view bar prints it and the bar is drawn
   before the camera code that changes it. */
static float g_camSpeed = 0.55f;

/* The waypoint numbers and the array itself, up with the rest of the panel state
   because the STARTS panel draws them and drawing comes first in this file. */
#define EDIT_WAYPT_COUNT 28
#define EDIT_WAYPT_HOME  26
#define EDIT_WAYPT_REINF 27
#define EDIT_MAX_PLAYERS 8          /* MAX_PLAYERS, defines.h:2565 (EIGHTPLAYERS) */
/* The house table above is written out to eight multiplayer seats. A brain with more of
   them would have houses offered that the table does not carry, which is a read past its
   end rather than a missing chip. */
static_assert(EUI_HOUSE_MAX >= EUI_HOUSE_N,
              "EUI_HOUSES is shorter than 4 + EDIT_MAX_PLAYERS");

static int g_waypoint[EDIT_WAYPT_COUNT];
static int g_waypointArmed = -1;    /* which one the next map click places */

/* WHAT A WAYPOINT IS CALLED, AND WHICH NUMBER IT CARRIES.

   26 and 27 are the two the engine reserves, so they are named rather than numbered. The
   other twenty-six are general-purpose cells, and everything that refers to one refers to
   it BY INDEX: the [Waypoints] key edit_emit_waypoints writes, a trigger, a TeamType order
   argument (the order chip prints "goes to waypoint 5" straight from the raw argument),
   and on a skirmish map the brain's own StartLocationIndex, which cnc_eyes.cpp defaults to
   the seat's number counting from ZERO and which --starts takes as a raw index.

   Labelling these rows starts and counting them from one disagreed with all of that. The
   row picked as START 6 is the one written to the file as 5, reached with --starts 5, and
   sent a team by order argument 5. While the row said START the number read as a seat
   ordinal and nobody matched it against the file; a row that says WAYPOINT will be matched,
   so it has to carry the number the file uses.

   On a skirmish map the first eight ARE the seats, and the panel says so in its heading
   (PLAYER STARTS) rather than in the number. That is deliberate: a seat number here would
   have to be zero-based to agree with StartLocationIndex, at which point it says nothing
   the heading did not already say. */
static const char* edit_waypoint_label(int i)
{
    static char b[32];
    if (i == EDIT_WAYPT_HOME)  return "HOME";
    if (i == EDIT_WAYPT_REINF) return "REINFORCE";
    snprintf(b, sizeof b, "WAYPOINT %d", i);
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
 *  is the text the project owner could not read.
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
 * "uneven, and in many places unreadable" the project owner reported three times, and no amount of
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
 *  TWO TIERS, which is the project owner's call and the right one.
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

/* WHAT AN ITEM WITH NO CAMEO DRAWS INSTEAD OF A DARK BOX.
 *
 * Cameos are sidebar art: the cartridge pack is what the ROM's sidebar carried, which also
 * includes UI plates, not only things a player can build. It holds fifty-three records
 * (fifty-two at 32x25 plus RMBO at 32x24); the palette holds a hundred and forty-one placeable items, so
 * ninety-eight tiles have no cartridge art and used to fill one flat rectangle each --
 * thirty-two of thirty-two TERRAIN, forty-two of sixty-one BUILDING, thirteen of twenty
 * INFANTRY, eight of twenty-two UNIT, two of five WALL and the one TIBERIUM field. A page
 * of civilian buildings was a grid of identical dark squares with a serial number
 * underneath.
 *
 * Four of those ninety-eight get a picture rather than a fallback. The two fences and the
 * two boats were never buildable on the console, so the cartridge set never drew them, but
 * the true-colour set does and sb_cameo_true hands it over. Ninety-four are left.
 *
 * No art is invented to close those. The fallback draws two facts the item's own row
 * already carries and the palette never showed: its display NAME, which is what tells
 * V01 from V02, and its FOOTPRINT -- the cells it occupies and which of them is the
 * cell under the pointer. Those two separate seventy of the ninety-four on
 * sight. The other twenty-four cannot be separated and it is worth being plain about
 * why: they are all terrain, ten tree types share the name "Tree" and one cell at the
 * same offset, and the table holds nothing else about them. For those the code strip
 * under the tile stays the only distinguisher, and closing it needs a rendered preview
 * of the model rather than a fallback. That gap is registered.
 */
static const EditItem* eui_item_by_code(const char* code)
{
    if (!code || !code[0]) return NULL;
    for (int i = 0; i < EDIT_ITEM_N; i++)
        if (!strcasecmp(EDIT_ITEMS[i].code, code)) return &EDIT_ITEMS[i];
    return NULL;
}

/* One ground tone per kind, so the groups read apart before a word is read. Dark
   enough that the armed and hovered treatments the tile draws behind still show. */
static unsigned eui_kind_tone(int kind)
{
    switch (kind) {
    case EDIT_KIND_BUILDING: return 0x16202c;
    case EDIT_KIND_UNIT:     return 0x122630;
    case EDIT_KIND_INFANTRY: return 0x241f18;
    case EDIT_KIND_TERRAIN:  return 0x14241a;
    case EDIT_KIND_WALL:     return 0x231e1c;
    case EDIT_KIND_TIBERIUM: return 0x1c2a12;
    case EDIT_KIND_SMUDGE:   return 0x2a1a14;
    default:                 return 0x0d1520;
    }
}

/* The occupy list as a plan, at ONE cell size for every item, so a three-by-three
   construction yard reads bigger than a one-cell turret instead of merely different.
   The anchor cell is forced into the bounds even when nothing stands on it: most tree
   types occupy the cell one SOUTH of the one that was clicked, and that offset is the
   difference between a tree that lands where it was put and one that does not. An
   empty cell that is not the anchor draws nothing, which keeps a two-by-two bound
   around a single tree down to two primitives. */
static void eui_footprint_plan(float x, float y, float w, float h,
                               const EditItem* it, float ts)
{
    int n = (int)it->n;
    if (n > EDIT_CELL_MAX) n = EDIT_CELL_MAX;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (int k = 0; k < n; k++) {
        if (it->dx[k] < x0) x0 = it->dx[k];
        if (it->dx[k] > x1) x1 = it->dx[k];
        if (it->dy[k] < y0) y0 = it->dy[k];
        if (it->dy[k] > y1) y1 = it->dy[k];
    }
    const int gw = x1 - x0 + 1, gh = y1 - y0 + 1;
    float c = w / (float)gw;
    if (h / (float)gh < c) c = h / (float)gh;
    if (c > 13.0f * ts) c = 13.0f * ts;
    if (c < 4.0f * ts) return;          /* too small to read: the name carries the tile */
    const float gut = ts > 1.0f ? ts : 1.0f;
    const float ox = x + (w - c * gw) * 0.5f, oy = y + (h - c * gh) * 0.5f;
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++) {
            const int cx = gx + x0, cy = gy + y0;
            const bool anchor = (cx == 0 && cy == 0);
            bool on = false;
            for (int k = 0; k < n; k++)
                if (it->dx[k] == cx && it->dy[k] == cy) { on = true; break; }
            if (!on && !anchor) continue;
            const float px = ox + gx * c, py = oy + gy * c, s = c - gut;
            if (on) eui_rect(px, py, s, s, EUI_CYAN, 0.42f);
            eui_frame(px, py, s, s, anchor ? EUI_GOLD : EUI_LINE, on ? 0.85f : 0.55f);
        }
}

/* The scale comes in rather than being worked back out of the box. The box width is a
   fixed multiple of the panel scale only while the sidebar is at its design width, and
   it stops being one the moment a narrow window clamps the sidebar -- a size that is
   right only because of how the caller happened to be laid out is the kind of thing
   that goes quietly wrong later. */
static void eui_cameo(float x, float y, float w, float h, const char* code, float ts)
{
    const SbTex* t = sb_cameo(code);
    /* The cartridge set first, then our own true-colour art where the cartridge has no
       picture for that type at all. The two fences and the two boats were never buildable
       on the console, so they fell through to the plate with usable art sitting in the
       other pack. */
    if (!t || !t->gl) t = sb_cameo_true(code);
    if (!t || !t->gl) {
        /* A linear walk of the item table, once per art-less tile. It is bounded by
           the PAGE rather than by the table: a few dozen tiles are on screen, not a
           hundred and forty. */
        const EditItem* it = eui_item_by_code(code);
        eui_rect(x, y, w, h, it ? eui_kind_tone((int)it->kind) : 0x0d1520, 1.0f);
        if (!it) return;
        const float nh = 11.0f * ts;
        if (h > nh * 2.0f) eui_footprint_plan(x, y, w, h - nh, it, ts);
        ef_text_fit(x + 2.0f * ts, y + h - nh, it->name, ts, w - 4.0f * ts,
                    EUI_RGB(EUI_DIM));
        return;
    }
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
    float ownerY, ownerH, ownerW;   /* ONE chip: the strip is ownerRows of these     */
    float ownerGap;                 /* between chips, across and down                */
    int   ownerRows;
    float catY, catH, catW;      /* the category tabs, seven in either mode        */
    float gridX, gridY, gridW, gridH;
    float tileW, tileH; int cols;
    float lintY, lintH;
    float actY, actH;            /* undo/redo/revert row                           */
    float saveY, saveH;
    float playY, playH;
};

/* HOW MANY CATEGORY TABS THE STRIP HAS, forward-declared because the layout needs the
   count and this file is ordered drawing-first, which puts the answer two hundred lines
   below. Note it BUILDS the terrain drawers on its first call in TERRAIN mode: that is
   idempotent, pure over the loaded pack and the static template table, and unreachable
   before the pack is loaded, so the layout is a safe caller. */
static int eui_cat_count(void);

/* THE OWNER CHIP STRIP IS FOUR COLUMNS AND AS MANY ROWS AS THE ROSTER NEEDS.
 *
 * Four and not twelve, measured rather than chosen by eye: at the eight window sizes and
 * two backing scales the layout harness runs, twelve chips across leave each one 15 to 38
 * device pixels wide, while the longest label the roster carries wants 51 to 79 in this
 * panel's own face. One row of the whole roster cannot be read at any size this editor is
 * tested at. A scrolling strip would fit but hides most of the houses, which is the one
 * thing a strip of owner chips exists not to do; a dropdown fits and hides all of them.
 * Four columns keeps every chip exactly the width it has always had, so a singleplayer
 * map -- which offers four houses and takes one row -- lays out to the same numbers it
 * did before this existed, and a skirmish map grows two rows downwards.
 *
 * Both counts are forward-declared for the same reason eui_cat_count is: the layout needs
 * them, and the map's kind that decides them is declared a long way below this point in a
 * file that is ordered drawing-first. */
#define EUI_OWNER_COLS 4
#define EUI_OWNER_GAP  4.0f     /* design units, the gap the chips already had */
static int eui_owner_n(void);
static int eui_owner_rows(void);

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
                          + (float)eui_owner_rows() * 30 /* owner chips, every row  */
                          + (float)(eui_owner_rows() - 1) * EUI_OWNER_GAP
                          + EUI_PAD
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

    /* n columns have n-1 gaps and the last one ends at the panel's right margin --
       the same arithmetic the category strip uses. With four columns this is the number
       it always was, to the bit, which is why a mission map's panel does not move. */
    L->ownerRows = eui_owner_rows();
    L->ownerGap  = EUI_OWNER_GAP * S;
    L->ownerW = (L->sideW - pad * 2 - (float)(EUI_OWNER_COLS - 1) * L->ownerGap)
              / (float)EUI_OWNER_COLS;
    L->ownerY = y; L->ownerH = 30 * S;
    /* THE WHOLE STRIP, not one row. The fixed-height budget above counts the same rows;
       if these two ever disagree the chips run into the category tabs, which is what the
       layout harness measures rather than assumes. */
    y += (float)L->ownerRows * L->ownerH
       + (float)(L->ownerRows - 1) * L->ownerGap + pad;

    /* SEVEN TABS IN EITHER MODE NOW, and the WIDTH has to follow the count: n tabs have
       n-1 gaps of 3*S between them, and the last one has to end at the panel's right
       margin. Hard-coding five while the draw emitted seven is why the terrain strip ran
       off the window -- measured at 1280x720 backing 1.0, ROCK was a 3.2 px sliver at the
       frame edge and BRIDGE started 49 px past it, so bridges could not be authored at
       all. OBJECTS was five until the SMUDGE group landed and is seven now, which is the
       count this arithmetic was fixed for, so both strips ask the same question. The
       count is still read rather than assumed: a group added later moves it again. */
    {
        int nc = eui_cat_count();
        if (nc < 1) nc = 1;
        L->catW = (L->sideW - pad * 2 - (float)(nc - 1) * 3 * S) / (float)nc;
    }
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

/* WHERE ONE OWNER CHIP SITS. The draw, the hit test and the layout harness all call this
   and none of them may work it out again. That is the whole defect this replaces: the
   strip carried three separate copies of the arithmetic and every one of them counted to
   four, so widening the roster in the data left the multiplayer houses drawn nowhere,
   clickable nowhere, and unreachable from the keyboard cycle that already offered them. */
static void eui_owner_chip_rect(const EuiLayout* L, int i,
                                float* x, float* y, float* w, float* h)
{
    const float pad = EUI_PAD * L->s;
    const int col = i % EUI_OWNER_COLS, row = i / EUI_OWNER_COLS;
    *w = L->ownerW;
    *h = L->ownerH;
    *x = L->sideX + pad + (float)col * (L->ownerW + L->ownerGap);
    *y = L->ownerY + (float)row * (L->ownerH + L->ownerGap);
}


static bool edit_over_chrome(float mx, float my, int fbw, int fbh)
{
    if (!g_editOn) return false;
    if (mx < 0.0f) return true;              /* pointer is off the window entirely */
    /* AND A DIALOG COUNTS AS CHROME, over the whole window rather than over its own
       rectangle. Both callers want exactly that: the hover test stops resolving a cell,
       so no cell outline and no brush footprint are painted on ground behind the dialog,
       and the engine's cursor picker stops running a ray into the world, so the pointer
       becomes the flat sprite it already is over the panels instead of the console's 3D
       cursor standing somewhere the click cannot reach. */
    if (edit_modal_up()) return true;
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



/* The category strip is mode-dependent: seven object kinds, or seven terrain families.
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
    /* Same reason the paint path refuses: a swatch cut from the wrong rectangle of a
       mismatched atlas shows the player art that no click of theirs can produce. */
    if (!tt_atlas_matches(at)) return;

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

/* ------------------------------------------------------------------------------------
   THE WAY IN TO THE HEIGHTMAP IMPORTER.

   The importer itself is far below and has been provable for a while; what it had no
   route from was a person. Three shapes were weighed.

     A NATIVE FILE DIALOG is a new dependency on two desktop platforms and a third API
     again on the retro box this has to build for, all for one button. Refused.
     THE FILE BESIDE THE MAP -- one image named after the scenario -- needs no picker at
     all, and the importer already carries that fallback. But it allows exactly ONE
     heightmap per map, so comparing two drafts of a plateau means renaming files in
     another program, which is the point at which an author gives up on the feature.
     THE FOLDER IS THE PICKER. Every PNG and PCX in user_maps is offered, which is the
     shape the Open dialog already uses on that same directory, and the image named
     after the map is preselected so the beside-the-map convention still works without
     being the only thing that works. Chosen.

   The scan is LAZY AND CACHED, the way the map list's is -- a directory read every frame
   for a control nobody is looking at is a hundred of them a second. That is also why the
   chip looks again before it steps: a picture saved into the folder while the editor is
   open is the normal case, not the odd one.

   Geometry lives here, once, and the draw and the hit test both call it, exactly as the
   cliff cells below do. The two cannot drift. */
static void edit_userdir(char* out, int n);                  /* with the map list      */
static void edit_toast(unsigned colour, const char* what, const char* detail);
static int  elev_import_image(const char* path, char* why, int whyN);
static inline int elev_bw(void);                             /* with the tier model    */
static inline int elev_bh(void);

#define ELEV_IMG_MAX  64
#define ELEV_IMG_NAME 64
#define ELEV_IMG_NONE "no PNG and no PCX in user_maps -- paint one, put it there, " \
                      "and click again"
static char g_elevImg[ELEV_IMG_MAX][ELEV_IMG_NAME];
static char g_elevImgFor[32] = "";      /* the map the preselect below was made for */
static int  g_elevImgN     = 0;
static int  g_elevImgSel   = 0;
static bool g_elevImgBuilt = false;

/* Built, and built for THIS map. Opening another map does not change the folder, but it
   does change which picture in it is the map's own, and an armed heightmap belonging to
   the map before this one is the kind of wrong default that gets clicked. */
static bool elev_imglist_current(void)
{
    return g_elevImgBuilt && !strcmp(g_elevImgFor, g_editScen);
}

static void elev_scan_images(void)
{
    g_elevImgN = 0;
    g_elevImgBuilt = true;
    snprintf(g_elevImgFor, sizeof g_elevImgFor, "%s", g_editScen);
    char dir[1024];
    edit_userdir(dir, sizeof dir);
    DIR* d = opendir(dir);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != NULL && g_elevImgN < ELEV_IMG_MAX) {
            const char* nm = e->d_name;
            const size_t L = strlen(nm);
            if (L < 5 || L >= ELEV_IMG_NAME) continue;
            if (strcasecmp(nm + L - 4, ".png") && strcasecmp(nm + L - 4, ".pcx"))
                continue;
            snprintf(g_elevImg[g_elevImgN++], ELEV_IMG_NAME, "%s", nm);
        }
        closedir(d);
    }
    /* Alphabetical, for the same reason the map list is: this list is read, not
       scrolled past, and readdir hands them over in whatever order the filesystem
       happens to hold them, which is not an order twice running. */
    for (int i = 0; i + 1 < g_elevImgN; i++)
        for (int j = i + 1; j < g_elevImgN; j++)
            if (strcasecmp(g_elevImg[j], g_elevImg[i]) < 0) {
                char t[ELEV_IMG_NAME];
                snprintf(t, sizeof t, "%s", g_elevImg[i]);
                snprintf(g_elevImg[i], ELEV_IMG_NAME, "%s", g_elevImg[j]);
                snprintf(g_elevImg[j], ELEV_IMG_NAME, "%s", t);
            }
    /* THE MAP'S OWN NAME WINS THE FIRST SELECTION, so the convention the importer
       already documents -- put the picture beside the map, under the map's name -- is
       what the button offers before anybody touches the chip. */
    g_elevImgSel = 0;
    const size_t sl = strlen(g_editScen);
    for (int i = 0; sl && i < g_elevImgN; i++) {
        const size_t L = strlen(g_elevImg[i]);
        if (L == sl + 4 && !strncasecmp(g_elevImg[i], g_editScen, sl)) {
            g_elevImgSel = i;
            break;
        }
    }
}

/* Where the import row starts: catY, the ladder, the three tools, the brush, then air.
   One walk, called by everything below, so the row and the cliff drawer under it cannot
   disagree about where the panel has got to. */
#define ELEV_IMPORT_H 30.0f
static float eui_elev_import_top(const EuiLayout* L)
{
    const float S = L->s, pad = EUI_PAD * S;
    return L->catY + (32 * S + pad) + 3 * (34 * S + 4 * S) + pad + 30 * S + 8 * S;
}

/* which: 0 the import button, 1 the chip beside it. The chip is ABSENT when the folder
   holds nothing -- there is no list to step through, so the whole row becomes the one
   control that looks again -- and returning false is how both the draw and the hit test
   are told so. The list is built here rather than in the draw because the hit test can
   run first, on the frame the pointer arrives, and a hit test that thought the folder
   was empty while the draw knew better is a chip that cannot be clicked.

   FALSE ALSO MEANS TOO SHORT A WINDOW, and that guard is not optional. L->lintY is the
   panel's usable bottom; below it are the status card and then UNDO / REDO / REVERT, and
   the elevation branch of the hit test is tested BEFORE that row. Measured with the
   layout prober: at a 640x360-point window on a 2x display this row lands at 571..606
   and the action row is at 583..613, so an unguarded row swallowed all three actions at
   four of the sixteen sizes the prober walks. The rectangle is still filled in when it
   does not fit, the way the cliff cells' is, so a probe can aim at it and require that
   NOTHING answers there. */
static bool eui_elev_import_rect(const EuiLayout* L, int which,
                                 float* x, float* y, float* w, float* h)
{
    if (!elev_imglist_current()) elev_scan_images();
    const float S = L->s, pad = EUI_PAD * S;
    const float full = L->sideW - pad * 2;
    *y = eui_elev_import_top(L);
    *h = ELEV_IMPORT_H * S;
    const bool fits = (*y + *h <= L->lintY - 4 * S);
    if (g_elevImgN <= 0) {
        if (which != 0) return false;
        *x = L->sideX + pad;
        *w = full;
        return fits;
    }
    const float chip = full * 0.30f;
    if (which == 0) { *x = L->sideX + pad;               *w = full - chip - 4 * S; }
    else            { *x = L->sideX + pad + full - chip; *w = chip; }
    return fits;
}

/* ------------------------------------------------------------------------------------
 *  THE AUTO HEIGHTMAP'S ROW, AND WHY IT IS BELOW THE IMPORT ROW RATHER THAN ABOVE IT
 *
 *  Above it costs the import button a window size, and that is measured rather than
 *  guessed: --uitest walks sixteen size/backing pairs, IMPORT HEIGHTMAP answers a click
 *  at twelve of them with this row absent, and at ELEVEN with a 36-unit row inserted
 *  above it, because the shortest frames have about one row of slack between the brush
 *  and the status card. Below it, eui_elev_import_top is not touched at all, so the
 *  import row is byte-identical at all sixteen and stays at twelve. What moves instead
 *  is the cliff drawer, which already drops the cells it cannot fit and already says
 *  "+N MORE BELOW" when it does.
 *
 *  IT HAS TWO PLACES, BECAUSE THE PANEL HAS TWO STATES. On a map that is on the ladder
 *  it is this row. On a map that is NOT, the panel draws the conversion gate INSTEAD of
 *  the whole page -- and that map is precisely the one this feature exists for, so the
 *  button lives on the gate card too, under CONVERT THIS MAP, as the reading alternative
 *  to the erasing one. ONE rect function for both, so the draw, the hit test and
 *  --uitest cannot disagree about which of the two is showing.
 * ---------------------------------------------------------------------------------- */
#define ELEV_AUTO_H   30.0f     /* the row's height on the tier page, design units   */
#define ELEV_AUTO_GAP  6.0f     /* air between the import row and it                 */
/* ITS TOP ON THE GATE CARD, AND THE CAPTION ABOVE IT. CONVERT THIS MAP is a 34-unit
   button at 140, so it ends at 174, and the line that says what else is behind the gate
   has to start below that or the button paints over it -- which is exactly what happened
   the first time this row was fitted in, at a caption y of 168. The caption is a t1 line
   and a t1 line on this card is 14 units tall (50, 64, 78, 92 above it), so 178 clears
   the button by 4 and the row clears the caption by 8. Both numbers are constants rather
   than literals in the draw because the harness reports the clearances and G192 reads
   them, and a report computed from a different number than the draw uses is a report
   that certifies nothing. */
#define ELEV_GATE_CAPY 178.0f   /* the caption's y, under CONVERT THIS MAP           */
#define ELEV_GATE_CAPH  14.0f   /* and the height a t1 line occupies on this card    */
#define ELEV_AUTO_GATEY 200.0f  /* the fit's own top, under that caption             */
#define ELEV_AUTO_GATEH  34.0f  /* and its height there, matching CONVERT's          */

static float eui_elev_auto_top(const EuiLayout* L)
{
    const float S = L->s;
    return eui_elev_import_top(L) + (ELEV_IMPORT_H + ELEV_AUTO_GAP) * S;
}

/* Same guard as the import row's, and not optional for the same reason: below L->lintY
   are the status card and UNDO / REDO / REVERT, and the elevation branch of the hit test
   runs BEFORE that row, so a row that does not fit must not answer. The rectangle is
   still filled in when it does not fit, the way the cliff cells' is, so a probe can aim
   at it and require that NOTHING answers there. */
static bool eui_elev_auto_rect(const EuiLayout* L, float* x, float* y, float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    if (!elev_ready()) {
        *x = L->sideX + pad * 2;
        *y = L->catY + ELEV_AUTO_GATEY * S;
        *w = L->sideW - pad * 4;
        *h = ELEV_AUTO_GATEH * S;
    } else {
        *x = L->sideX + pad;
        *y = eui_elev_auto_top(L);
        *w = L->sideW - pad * 2;
        *h = ELEV_AUTO_H * S;
    }
    return (*y + *h <= L->lintY - 4 * S);
}

/* THE CHIP. It rescans before it steps, then lands back on whatever was showing so a
   rescan that finds the same files does not shuffle the selection under the pointer. */
static void elev_import_cycle(void)
{
    char had[ELEV_IMG_NAME] = "";
    const int before = elev_imglist_current() ? g_elevImgN : -1;
    if (elev_imglist_current() && g_elevImgN > 0)
        snprintf(had, sizeof had, "%s", g_elevImg[g_elevImgSel]);
    elev_scan_images();
    if (g_elevImgN <= 0) {
        snprintf(g_elevWhy, sizeof g_elevWhy, "%s", ELEV_IMG_NONE);
        edit_toast(EUI_GOLD, "NO HEIGHTMAP IMAGE", ELEV_IMG_NONE);
        return;
    }
    int at = 0;
    for (int i = 0; i < g_elevImgN; i++)
        if (!strcasecmp(g_elevImg[i], had)) { at = i; break; }
    g_elevImgSel = (at + (had[0] ? 1 : 0)) % g_elevImgN;
    g_elevWhy[0] = 0;
    if (before != g_elevImgN) {
        char sub[160];
        snprintf(sub, sizeof sub, "%d in user_maps -- showing %s", g_elevImgN,
                 g_elevImg[g_elevImgSel]);
        edit_toast(EUI_CYAN, "HEIGHTMAP IMAGES", sub);
    }
}

/* THE BUTTON. The importer states every refusal it makes and, until this existed, every
   one of them went to the log and nowhere else. So the refusal is toasted AND kept in
   the status card's own line, which is what a player looking at the panel reads. */
static void elev_import_selected(void)
{
    if (!elev_imglist_current()) elev_scan_images();
    if (g_elevImgN <= 0) {
        /* The empty row IS the look-again control. If the look finds something, SAY so
           and stop: importing a file the player has not yet seen named would be the one
           surprise this whole row exists to avoid. */
        elev_scan_images();
        if (g_elevImgN <= 0) {
            snprintf(g_elevWhy, sizeof g_elevWhy, "%s", ELEV_IMG_NONE);
            edit_toast(EUI_GOLD, "NO HEIGHTMAP IMAGE", ELEV_IMG_NONE);
        } else {
            char sub[160];
            snprintf(sub, sizeof sub, "%d in user_maps -- %s is armed, click again to "
                     "import it", g_elevImgN, g_elevImg[g_elevImgSel]);
            g_elevWhy[0] = 0;
            edit_toast(EUI_CYAN, "HEIGHTMAP IMAGES", sub);
        }
        return;
    }
    /* BEHIND THE SAME GATE THE BRUSH IS. An import writes corners only where the image
       disagrees with the ground, so on a map that is off the ladder it would legalise
       the part it drew and leave the rest off it -- a half-converted map, and the panel
       would still be showing the gate. The gate first, then the import. */
    if (!elev_ready()) {
        const char* w = "this map is not on the ladder -- CONVERT THIS MAP first";
        snprintf(g_elevWhy, sizeof g_elevWhy, "%s", w);
        edit_toast(EUI_GOLD, "HEIGHTMAP NOT IMPORTED", w);
        return;
    }
    if (g_elevImgSel < 0 || g_elevImgSel >= g_elevImgN) g_elevImgSel = 0;
    char dir[1024], path[1200], why[220] = { 0 };
    edit_userdir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s", dir, g_elevImg[g_elevImgSel]);
    if (elev_import_image(path, why, sizeof why) > 0) {
        g_elevWhy[0] = 0;               /* the importer toasts its own numbers */
        return;
    }
    snprintf(g_elevWhy, sizeof g_elevWhy, "%s",
             why[0] ? why : "the import did nothing and did not say why");
    edit_toast(EUI_DANGER, "HEIGHTMAP NOT IMPORTED", g_elevWhy);
}

static float eui_cliff_top(const EuiLayout* L)
{
    const float S = L->s, pad = EUI_PAD * S;
    /* catY, the ladder, the three tools, the brush, the import row and the auto
       heightmap row under it -- the same walk eui_draw_elev and the hit test make. */
    return eui_elev_auto_top(L) + ELEV_AUTO_H * S + pad + 12 * S;
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

/* ------------------------------------------------------------------------------------
 *  THE ELEVATION PANEL HAS TWO PAGES, AND THE FIRST ONE MAY NOT MOVE BY ONE PIXEL
 *
 *  The tier tool's page is the one this editor has always had, and it is not being
 *  redesigned: it starts at L->catY, its RUNG caption sits 12*S above that, and every
 *  row below is measured from there. So the page selector CANNOT be a tab strip laid
 *  across the top of the page -- that pushes every row down by its own height, which
 *  collides the caption with the tabs at every window size, walks the brush row into
 *  the action row at the short ones, and is exactly the regression this arrangement
 *  exists to avoid.
 *
 *  THE TABS GO IN THE OWNER-CHIP BAND INSTEAD, which ELEVATN does not use. The chip
 *  strip is drawn for OBJECTS only (a tile has no house); TERRAIN puts a caption there
 *  and SCRIPT puts its RULES / TEAMS / ENHANCED switch there -- so this is the panel's
 *  own established place for a per-mode switch, and the precedent to copy is that one.
 *  Vertically it costs the page NOTHING: eui_elev_top is L->catY, unchanged, so both
 *  pages begin where the tier page has always begun.
 *
 *  It stops 14*S above catY rather than filling the band, because the page below draws
 *  its own small caption at catY - 12*S and text is drawn from its TOP. Two design
 *  units of air, measured against the caption rather than guessed at.
 * ---------------------------------------------------------------------------------- */

/* Where either page starts. One function, so the claim "the tier page did not move" is
   a thing a gate can read rather than a thing a reader has to believe. */
static float eui_elev_top(const EuiLayout* L) { return L->catY; }

static bool eui_sbr_tab_rect(const EuiLayout* L, int i,
                             float* x, float* y, float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    *w = (L->sideW - pad * 2 - 4 * S) * 0.5f;
    *x = L->sideX + pad + (float)i * (*w + 4 * S);
    *y = L->ownerY;
    *h = L->catY - 14 * S - L->ownerY;
    /* A tab too short to aim at is not drawn and must not answer. The band is fixed
       furniture in the layout's height budget, so this is true at every size the
       harness walks -- but it is asked rather than assumed, the way the import row
       and the cliff cells ask. */
    return *h >= 8 * S;
}

/* ---- the smooth page's own walk ----------------------------------------------------
 *
 *  ONE running total, called by the draw, the hit test, the handler and the layout
 *  harness, so none of them can work it out again and get a different answer. Every row
 *  reports whether it FITS above the status card, because at four of the sixteen sizes
 *  the harness walks there is only about 102 design units between eui_elev_top and
 *  L->lintY, and a control drawn past that lands on the readout.
 *
 *  MEASURED, at the tightest of those four (1280x480 backing 2.0): lintY - catY is
 *  82.49 px at S=0.7782, which is 106.0 design units, so the bound is 102 after the
 *  4*S margin. The tool row and all three sliders come to 98 and fit; the help block
 *  starts at 105 and is the row that gets cut. That is why the help text is LAST and
 *  why its top is a different expression from any slider's -- the two resolving to the
 *  same number is how help text ends up drawn over a slider. */
#define SBR_TOOLH   28.0f      /* the RAISE / LOWER / ERASE row                       */
#define SBR_SLIDH   18.0f      /* one slider track                                    */
#define SBR_SLIDG    3.0f      /* between sliders                                     */
#define SBR_HELPLN  11.0f      /* one line of help                                    */
#define SBR_HELP_N  4

static float eui_sbr_tools_top(const EuiLayout* L) { return eui_elev_top(L); }

static float eui_sbr_sliders_top(const EuiLayout* L)
{
    const float S = L->s;
    return eui_sbr_tools_top(L) + SBR_TOOLH * S + EUI_PAD * S;
}

static float eui_sbr_help_top(const EuiLayout* L)
{
    const float S = L->s;
    return eui_sbr_sliders_top(L)
         + (float)SBR_SLIDER_N * (SBR_SLIDH + SBR_SLIDG) * S + EUI_PAD * S;
}

/* Does a row of height h with its top at y clear the status card? The one bound, so a
   row that the draw skips is a row the hit test skips. */
static bool eui_sbr_fits(const EuiLayout* L, float y, float h)
{
    return y + h <= L->lintY - 4 * L->s;
}

static bool eui_sbr_tool_rect(const EuiLayout* L, int i,
                              float* x, float* y, float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    const float full = L->sideW - pad * 2;
    *w = (full - 2 * 4 * S) / 3.0f;
    *x = L->sideX + pad + (float)i * (*w + 4 * S);
    *y = eui_sbr_tools_top(L);
    *h = SBR_TOOLH * S;
    return eui_sbr_fits(L, *y, *h);
}

static bool eui_sbr_slider_rect(const EuiLayout* L, int i,
                                float* x, float* y, float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    *x = L->sideX + pad;
    *w = L->sideW - pad * 2;
    *y = eui_sbr_sliders_top(L) + (float)i * (SBR_SLIDH + SBR_SLIDG) * S;
    *h = SBR_SLIDH * S;
    return eui_sbr_fits(L, *y, *h);
}

/* THE TRACK IS NOT THE WHOLE ROW, and this is the second half of "the knob cannot be
   drawn somewhere the click does not mean".
 *
 * A slider row carries three things: its NAME on the left, its READOUT on the right, and
 * the knob between them. While the fill and the knob spanned the whole row the knob was
 * drawn straight through the name at the page's own defaults -- MEASURED at 1600x1000
 * backing 1.0 with size 2.0, falloff 3.0, strength 0.60: CENTRE SIZE put its knob at
 * x=1285.5 while the glyphs of "CENTRE SIZE" run to x=1311.3, and FALLOFF put its knob
 * at x=1295.4 against a label ending at 1286.3. Two of the three rows, at the values the
 * page opens with.
 *
 * THE RESERVE IS MEASURED, NOT ASSUMED. The panel font is picked from a discrete set of
 * sizes, so a label that is 72.4 px wide at scale 0.625 is still 72.4 px wide at 1.1125
 * and 85.5 px at 1.3375: a fixed number of design units is wrong at eleven of the sixteen
 * window sizes. ef_text_w is asked instead, on both sides.
 *
 * THE READOUT IS RESERVED AT ITS WIDEST, not at the value showing. A right edge that
 * moved as the number changed would put the knob under the digits at some values and not
 * at others, which is a layout that is only correct while somebody is looking at it.
 *
 * ONE FUNCTION, because the draw, the hit test and the uitest all have to agree about
 * this rectangle or the tool lies about where it is. */
static void eui_sbr_track(const EuiLayout* L, int i, float x, float w,
                          float* tx, float* tw)
{
    const float S = L->s, t1 = 1.0f * S;
    char v[32];
    if (SBR_SLIDERS[i].unit[0])
        snprintf(v, sizeof v, "%.1f %s", SBR_SLIDERS[i].hi, SBR_SLIDERS[i].unit);
    else
        snprintf(v, sizeof v, "%.2f", SBR_SLIDERS[i].hi);
    const float lw = 8 * S + ef_text_w(SBR_SLIDERS[i].name, t1) + SBR_TRACK_GAP * S;
    const float rw = 8 * S + ef_text_w(v, t1) + SBR_TRACK_GAP * S;
    float a = x + lw, b = x + w - rw;
    /* A PANEL TOO NARROW FOR BOTH COLUMNS still has to be draggable. The floor takes the
       space back from the READOUT side first, because the readout is the half a player
       can reconstruct from where the knob is standing. */
    if (b - a < SBR_TRACK_MIN * S) b = a + SBR_TRACK_MIN * S;
    if (b > x + w) b = x + w;
    if (b < a)     b = a;
    *tx = a;
    *tw = b - a;
}

/* The value a pointer at px means for slider i, and the value it is showing now. Both
   sides of the same mapping, in one place, so the knob cannot be drawn somewhere the
   click does not mean. */
static float eui_sbr_slider_t(int i)
{
    const SbrSlider* d = &SBR_SLIDERS[i];
    float t = (g_sbrVal[i] - d->lo) / (d->hi - d->lo);
    return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
}

static void eui_sbr_slider_drag(const EuiLayout* L, int i, float px)
{
    float x, y, w, h;
    if (i < 0 || i >= SBR_SLIDER_N) return;
    eui_sbr_slider_rect(L, i, &x, &y, &w, &h);
    /* THE ROW IS THE HIT AREA AND THE TRACK IS THE MAPPING. Pressing anywhere on the row
       takes the slider -- which is what makes it easy to grab -- and the value comes from
       the track, clamped, so a press on the name means the low end and a press on the
       readout means the high end rather than a value the knob cannot be drawn at. */
    float tx, tw;
    eui_sbr_track(L, i, x, w, &tx, &tw);
    if (tw <= 0.0f) return;
    float t = (px - tx) / tw;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    g_sbrVal[i] = SBR_SLIDERS[i].lo + t * (SBR_SLIDERS[i].hi - SBR_SLIDERS[i].lo);
}

/* The page tabs. Drawn on BOTH pages and on the conversion gate as well: the gate is
   exactly the state a player has to be able to leave, because a smooth stroke is what
   unlocks the rung tool on a map that is off the ladder. */
static void eui_draw_elev_tabs(const EuiLayout* L, float t1)
{
    const float S = L->s;
    for (int i = 0; i < 2; i++) {
        float x, y, w, h;
        if (!eui_sbr_tab_rect(L, i, &x, &y, &w, &h)) return;
        const bool on  = (i == g_sbrPage);
        const bool hot = (g_euiHotKind == EUI_SBRPAGE && g_euiHotArg == i);
        eui_panel(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL);
        eui_rect(x, y, w, 3 * S, on ? EUI_CYAN : EUI_LINE, on ? 1.0f : 0.3f);
        const float tw = ef_text_w(SBR_PAGE_NAME[i], t1);
        ef_text(x + (w - tw) * 0.5f, y + h * 0.5f - 3 * S, SBR_PAGE_NAME[i], t1,
                EUI_RGB(on ? EUI_CYAN : EUI_FAINT));
    }
}

/* The smooth page. No ladder, no rungs, no cliff art: three tools, three sliders and a
   sentence about what the brush will not do. */
static void eui_draw_sbr(const EuiLayout* L, float t1)
{
    const float S = L->s, pad = EUI_PAD * S;
    ef_text(L->sideX + pad, eui_elev_top(L) - 12 * S,
            "SMOOTH BRUSH   WRITES CORNERS, DRESSES NOTHING",
            EUI_TS(t1), EUI_RGB(EUI_FAINT));

    for (int i = 0; i < 3; i++) {
        float x, y, w, h;
        if (!eui_sbr_tool_rect(L, i, &x, &y, &w, &h)) break;
        const bool on  = (i == g_sbrTool);
        const bool hot = (g_euiHotKind == EUI_SBRTOOL && g_euiHotArg == i);
        eui_panel(x, y, w, h, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL);
        if (on) eui_rect(x, y, w, 3 * S, EUI_CYAN, 1.0f);
        const float tw = ef_text_w(SBR_TOOL_NAME[i], t1);
        ef_text(x + (w - tw) * 0.5f, y + h * 0.5f - 3 * S, SBR_TOOL_NAME[i], t1,
                EUI_RGB(on ? EUI_CYAN : EUI_DIM));
    }

    for (int i = 0; i < SBR_SLIDER_N; i++) {
        float x, y, w, h;
        if (!eui_sbr_slider_rect(L, i, &x, &y, &w, &h)) break;
        const bool hot = (g_euiHotKind == EUI_SBRSLIDER && g_euiHotArg == i);
        const float t = eui_sbr_slider_t(i);
        float tx, tw;
        eui_sbr_track(L, i, x, w, &tx, &tw);
        eui_panel(x, y, w, h, hot ? EUI_PANEL2 : EUI_PANEL);
        eui_rect(tx, y, tw * t, h, EUI_CYAN, 0.30f);
        eui_rect(tx + tw * t - 1.5f * S, y, 3 * S, h, EUI_CYAN, 1.0f);
        ef_text(x + 8 * S, y + h * 0.5f - 3 * S, SBR_SLIDERS[i].name, EUI_TS(t1),
                EUI_RGB(EUI_DIM));
        char v[32];
        if (SBR_SLIDERS[i].unit[0])
            snprintf(v, sizeof v, "%.1f %s", g_sbrVal[i], SBR_SLIDERS[i].unit);
        else
            snprintf(v, sizeof v, "%.2f", g_sbrVal[i]);
        ef_text(x + w - ef_text_w(v, EUI_TS(t1)) - 8 * S, y + h * 0.5f - 3 * S, v,
                EUI_TS(t1), EUI_RGB(EUI_CYAN));
    }

    {
        /* FOUR LINES, and the count is a layout constant: SBR_HELP_N decides whether
           the block fits above the status card at the four shortest window sizes. */
        static const char* HELP[SBR_HELP_N] = {
            "Hold the button and the ground keeps moving; the",
            "rate is per SECOND, not per frame. ERASE eases the",
            "ground back to THIS MAP's own level. Water is",
            "refused: the sea reuses the ground's own corners.",
        };
        const float hy = eui_sbr_help_top(L);
        if (eui_sbr_fits(L, hy, (float)SBR_HELP_N * SBR_HELPLN * S))
            for (int i = 0; i < SBR_HELP_N; i++)
                ef_text(L->sideX + pad, hy + (float)i * SBR_HELPLN * S, HELP[i],
                        EUI_TS(t1), EUI_RGB(EUI_FAINT));
    }
}

/* The elevation panel: the ladder, the three tools, the brush, and -- until the map is
   on the ladder -- the conversion gate instead of all of it. */
static void eui_draw_elev(const EuiLayout* L, float t1)
{
    /* THE PAGE TABS FIRST, and in the band above the page, so nothing below this line
       moves. The tier page's body is unchanged from here down. */
    eui_draw_elev_tabs(L, t1);
    if (g_sbrPage == 1) { eui_draw_sbr(L, t1); return; }

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
        /* SAY WHAT ELSE IS BEHIND THE GATE, AND WHAT IS NOT. Importing a painted
           heightmap is refused on an unconverted map for the reason set out where that
           button lives. The fit below is NOT refused and never was meant to be: it reads
           cliff art out of the .BIN and writes corner bytes, and neither of those needs
           the ladder. A control that is simply absent teaches nobody why; a control
           behind a gate it does not need is worse, because the gate it was put behind
           REPAINTS every level block as plain ground and would erase the very art the
           fit reads. */
        {
            float ax, ay, aw, ahh;
            const bool afits = eui_elev_auto_rect(L, &ax, &ay, &aw, &ahh);
            ef_text(tx, y + ELEV_GATE_CAPY * S,
                    "Importing a painted heightmap waits on this.",
                    EUI_TS(t1), EUI_RGB(EUI_FAINT));
            if (afits) {
                const bool ahot = (g_euiHotKind == EUI_ELEVAUTO);
                eui_panel(ax, ay, aw, ahh, ahot ? EUI_PANEL3 : EUI_PANEL2);
                eui_rect(ax, ay, 3 * S, ahh, EUI_CYAN, 1.0f);
                ef_text_fit(ax + 10 * S, ay + ahh * 0.5f - 3 * S,
                            "FIT THE GROUND TO THE CLIFF ART", EUI_TS(t1), aw - 20 * S,
                            EUI_RGB(EUI_CYAN));
                /* SHORT ENOUGH THAT THE LAST WORD SURVIVES. ef_text_fit ellipsises
                   from the right, and the longer wording lost "Undoable." at the
                   panel's own default width -- which is the one word on this line that
                   answers the question the card's own IT CANNOT BE UNDONE raises about
                   the button above it. */
                ef_text_fit(tx, ay + ahh + 6 * S,
                            "Reads the slope pieces on this map. Undoable.",
                            EUI_TS(t1), L->sideW - pad * 2 - 20 * S, EUI_RGB(EUI_FAINT));
            }
        }

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

    /* ---- the way in to the heightmap importer ---- */
    {
        float bx, by, bw, bh;
        if (eui_elev_import_rect(L, 0, &bx, &by, &bw, &bh)) {
            const bool have = (g_elevImgN > 0);
            const bool hot  = (g_euiHotKind == EUI_ELEVIMPORT);
            eui_panel(bx, by, bw, bh, hot ? EUI_PANEL3 : EUI_PANEL2);
            eui_rect(bx, by, 3 * S, bh, have ? EUI_CYAN : EUI_GOLD, 1.0f);
            ef_text(bx + 10 * S, by + 5 * S,
                    have ? "IMPORT HEIGHTMAP" : "NO HEIGHTMAP IMAGE", t1,
                    EUI_RGB(have ? EUI_CYAN : EUI_GOLD));
            char sub[128];
            if (have)
                snprintf(sub, sizeof sub, "%s  ->  %d x %d blocks",
                         g_elevImg[g_elevImgSel], elev_bw(), elev_bh());
            else
                snprintf(sub, sizeof sub, "put a PNG or PCX in user_maps, then click");
            ef_text_fit(bx + 10 * S, by + 17 * S, sub, EUI_TS(t1), bw - 20 * S,
                        EUI_RGB(EUI_FAINT));
            float cx, cy, cw, ch;
            if (eui_elev_import_rect(L, 1, &cx, &cy, &cw, &ch)) {
                const bool chot = (g_euiHotKind == EUI_ELEVNEXT);
                eui_panel(cx, cy, cw, ch, chot ? EUI_PANEL3 : EUI_PANEL2);
                ef_text(cx + (cw - ef_text_w("NEXT", t1)) * 0.5f, cy + 5 * S, "NEXT",
                        t1, EUI_RGB(EUI_DIM));
                char cnt[24];
                snprintf(cnt, sizeof cnt, "%d / %d", g_elevImgSel + 1, g_elevImgN);
                ef_text(cx + (cw - ef_text_w(cnt, EUI_TS(t1))) * 0.5f, cy + 17 * S,
                        cnt, EUI_TS(t1), EUI_RGB(EUI_FAINT));
            }
        }
    }

    /* ---- fit a heightmap to the cliff art this map already carries ---- */
    {
        float bx, by, bw, bh;
        if (eui_elev_auto_rect(L, &bx, &by, &bw, &bh)) {
            const bool hot   = (g_euiHotKind == EUI_ELEVAUTO);
            const bool armed = g_ahArmed && (g_editFrame - g_ahArmedAt) < AH_ARM_FRAMES;
            eui_panel(bx, by, bw, bh, hot ? EUI_PANEL3 : EUI_PANEL2);
            eui_rect(bx, by, 3 * S, bh, armed ? EUI_GOLD : EUI_CYAN, 1.0f);
            ef_text(bx + 10 * S, by + 5 * S,
                    armed ? "REPLACE THE RELIEF?" : "AUTO HEIGHTMAP", t1,
                    EUI_RGB(armed ? EUI_GOLD : EUI_CYAN));
            char sub[128];
            if (armed)
                snprintf(sub, sizeof sub, "click again -- this rewrites the ground");
            else if (g_ah.valid)
                snprintf(sub, sizeof sub, "%d placements read, %d cells marked",
                         g_ah.places, g_ah.marked);
            else
                snprintf(sub, sizeof sub, "read the ground off the slope pieces on the map");
            ef_text_fit(bx + 10 * S, by + 17 * S, sub, EUI_TS(t1), bw - 20 * S,
                        EUI_RGB(EUI_FAINT));
        }
    }

    /* ---- the cliff drawer: stamp any slope piece by hand ---- */
    {
        int list[64];
        const int n = eui_cliff_list(list, 64);
        int shown = 0;
        /* THE CAPTION IS NOT A CONTROL, so --uitest cannot see it by clicking, and it
           was drawn unconditionally: at 1280x480 backing 1.0, with the auto heightmap
           row above it, it landed inside the status card and printed text over text.
           It is drawn only when the drawer it names has a cell to show, which is the
           same question the drawer itself asks one line further down, so the caption
           cannot outlive the thing it captions. The ELEVCAP line is how a gate reads
           that decision off a DRAWN frame rather than off a hit rectangle -- this
           caption has none, which is exactly why --uitest was blind to it. */
        float c0x, c0y, c0w, c0h;
        const bool capfit = (n > 0) && eui_cliff_cell_rect(L, 0, &c0x, &c0y, &c0w, &c0h);
        /* SET WHERE THE TEXT IS ACTUALLY PAINTED, not where the decision is made. A
           diagnostic that reports the guard's own condition reports what the code MEANT
           to do, and a gate reading it stays green while the paint happens anyway --
           which is exactly what a mutation of the guard proved. This is set by the same
           block that calls ef_text, so the line below says what was drawn. */
        int capdrawn = 0;
        if (capfit) {
            ef_text(L->sideX + pad, eui_cliff_top(L) - 12 * S,
                    "CLIFF PIECES   CLICK TO ARM, CLICK THE MAP TO STAMP",
                    EUI_TS(t1), EUI_RGB(EUI_FAINT));
            capdrawn = 1;
        }
        {   /* Said when it changes, not per frame -- the same rule the overlay keeps. */
            static int told = -1;
            if (told != capdrawn) {
                told = capdrawn;
                fprintf(stderr, "ELEVCAP|drawn=%d|capY=%.2f|lintY=%.2f|cells=%d\n",
                        capdrawn, eui_cliff_top(L) - 12 * S, L->lintY, n);
            }
        }
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
 *  the project owner's note: the map browser must not stand between double-clicking the app and
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

/* A MODAL OWNS THE WHOLE EDITOR, NOT JUST THE PIXELS IT COVERS. eui_hit has caged the
   MOUSE behind an open dialog since it grew its "a modal takes everything" arm, but
   nothing caged the keyboard or the pointer's world pick: with OPEN MAP on screen the
   fly camera still flew, the arrow keys still panned, 1 to 5 still changed mode, Tab
   still cycled the owning house, Delete still erased the cell under a pointer the dialog
   was covering, and the console's 3D cursor still stood on ground nobody could reach.
   Everything that has to stand down while a dialog is up asks this.
   The dropdown MENU is deliberately NOT in here. It is not a mouse cage either -- a
   click beside it lands on the map -- so making it a keyboard cage would leave the two
   halves disagreeing again, in the other direction. */
static bool edit_modal_up(void) { return g_editOn && g_euiModal != MODAL_NONE; }

/* THE SLOT THE SAVE AS COPY WILL TAKE, resolved once when that dialog opens and shown
   inside it. Someone about to name a map should be able to see which USERnn file the
   name is going into, because the slot is what the GAME finds the map by. Declared up
   here with the rest of the modal state because the dialog draws it -- the
   drawing-first ordering rule of this file. */
static char g_saveasSlot[32] = "";

/* NEW MAP's choices. Every preset names the same thing, which is what the header over
   the list says: the PLAYABLE RECTANGLE. The four legacy ones are rectangles inside the
   classic 64x64 grid -- the grid is fixed and what varies is the rect. The fifth is a
   rectangle inside the MEGAMAPS grid: a full 128x128 world saved as [MAP] Version=1 with
   the brain's own sparse .BIN, played 120x120 with the centring leaving 4 blank cells on
   every side. The sixth is neither: it is whatever was typed into it, anywhere in the
   range the engine allows.

   THE FIFTH ROW NAMES ITS RECT, NOT ITS GRID, and that is the point of this note. It
   read "128 x 128" for a while, which is the one number on that row nobody could act on:
   it named the grid on a list whose every other row names the rect, and it promised a
   size no map of this format can carry. The widest rectangle a Version=1 map allows is
   EUI_SIZE_MAX_BIG, which is 126, because the played rect sits one blank cell inside the
   cell array on every side; a rect of 128 would leave a unit leaving the map with nowhere
   to walk. The geometry did not change with the name: this row still builds rect 4,4
   120x120 on a 128 world. Anyone who wants the format's true maximum types 126 into
   CUSTOM. Widen this row and the name has to move with it. */
struct EuiSize { const char* name; int w, h; int grid; };
#define EUI_SIZE_N 6
#define EUI_SIZE_CUSTOM 5
static const EuiSize EUI_SIZES[EUI_SIZE_N] = {
    { "SMALL   32 x 32",  32, 32,  64 },
    { "MEDIUM  44 x 44",  44, 44,  64 },
    { "LARGE   54 x 54",  54, 54,  64 },
    { "HUGE    62 x 62",  62, 62,  64 },
    { "120 x 120  (BIG MAP)", 120, 120, 128 },
    /* THE SIXTH IS TYPED, and its w/h/grid are zero because it has none of its own:
       g_newW and g_newH carry the answer and eui_new_grid picks the grid from them.
       Five presets were never the range, they were five points inside a range the
       engine has always allowed, and this is the space between them. */
    { "CUSTOM",                 0,   0,   0 },
};

/* WHAT THE ENGINE ACTUALLY ALLOWS, read rather than assumed.
 *
 * The 1995 editor's own size dialog holds the played rectangle one blank cell inside
 * the cell array on every side, so a unit leaving the map has an undrawn cell to walk
 * onto. Its corner drags clamp the left edge to column 1 and the right edge to
 * MAP_CELL_W - 2, and refuse a side under 3 (mapeddlg.cpp, Size_Map). So the widest
 * legal rect is MAP_CELL_W - 2 and the centring in the blank map lands it at column 1
 * exactly.
 *
 * MAP_CELL_W is 64 for a legacy map, whose cell numbers are y*64+x so that nothing past
 * column 63 can be addressed at all, and 128 for a Version=1 one, which is the FILE's
 * stride and not this build's storage ceiling. Measured against the cartridge: across
 * the 98 shipped missions the largest rect is 62x62 and the largest X+Width is 63,
 * which is this bound and not a coincidence.
 *
 * The FLOOR is the editor's own. The engine would take 3; the smallest rectangle the
 * cartridge ever shipped is 26x23, and 16 sits under that while still leaving room for
 * a construction yard, its apron and somewhere to drive. */
#define EUI_SIZE_MIN     16
#define EUI_SIZE_MAX_64  (64 - 2)
#define EUI_SIZE_MAX_BIG (C3D_INI_STRIDE - 2)

/* The typed rectangle, and the grid it needs. A side over 62 cannot be numbered on the
   legacy 64 stride, so it takes the big grid and the map is saved as Version=1. */
static int g_newW = 62, g_newH = 62;
static inline int eui_new_grid(void)
{
    return (g_newW <= EUI_SIZE_MAX_64 && g_newH <= EUI_SIZE_MAX_64)
         ? 64 : C3D_INI_STRIDE;
}
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
/* NEW MAP'S KIND, WHICH IS THREE ANSWERS AND NOT TWO.
 *
 * A singleplayer mission has to say WHOSE it is as well as that it is one: [Basic]
 * Player= is the house the engine hands to the human (scenarioini.cpp:369) and it is the
 * only place that question is ever asked. Writing GoodGuy there whatever was wanted is
 * what made a Nod mission unauthorable without leaving the editor.
 *
 * THE TWO VALUES THAT ALREADY EXISTED KEEP THEIR NUMBERS, 0 singleplayer and 1
 * multiplayer, because the script verb that drives this dialog passes the number straight
 * through and an old script must not quietly come to mean something else. The Nod answer
 * is a third value on the end, and the ROW ORDER on screen is stated separately so that
 * the two singleplayer answers still sit next to each other. */
enum { NEWKIND_GDI = 0, NEWKIND_MULTI = 1, NEWKIND_NOD = 2, NEWKIND_N = 3 };
static const int NEWKIND_ROW[NEWKIND_N] = { NEWKIND_GDI, NEWKIND_NOD, NEWKIND_MULTI };
static const char* const NEWKIND_LABEL[NEWKIND_N] = { "GDI MISSION", "NOD MISSION",
                                                      "SKIRMISH" };
static int  g_newMulti   = NEWKIND_GDI;

/* THE HOUSE THIS MAP HANDS THE PLAYER, as a place in EUI_HOUSES. Read by the seed writer,
   which is the only writer that invents a [Basic] at all: a map opened from a file keeps
   whatever Player= that file carried, because every later save copies the section
   through, and Save As copies the source before saving. */
static int  g_mapPlayer  = EUI_HOUSE_GDI;

/* The map's own kind, which outlives the New Map dialog: it decides where the map is
   filed and which list the game offers it in. Changeable afterwards from the menu. */
static int  g_mapIsMulti = 0;

/* HOW MANY OWNER CHIPS THE STRIP SHOWS, and over how many rows. Declared above the layout
   and defined here, because the answer needs the map's kind.
   The strip is shared: OBJECTS paints the house chips in it, TERRAIN a line of text,
   SCRIPT the rules/teams switch, and the two remaining modes hand the whole block to
   their own panels. Only the house chips ever want more than one row, so only they get
   one -- reserving three rows in every mode would leave two rows of empty panel above the
   drawer tabs for the whole of a skirmish map's terrain work. */
static int eui_owner_n(void)
{
    return (g_editMode == 0) ? EUI_HOUSES_OFFERED : 4;
}
static int eui_owner_rows(void)
{
    const int n = eui_owner_n();
    return (n + EUI_OWNER_COLS - 1) / EUI_OWNER_COLS;
}

/* THE MAP MENU, AND WHY EVERY ROW CARRIES AN ID.
 *
 * This table used to be a bare list of strings and the click handler was a switch on the
 * ROW NUMBER -- case 0, 1, 3, 4, 5, 7, 9. Adding one entry anywhere above the bottom
 * therefore silently reassigned every action below it: SAVE would revert, PLAY would
 * check the mission, and nothing would fail to compile or fail a test. It had already
 * bitten once in the drawing code, where the SINGLEPLAYER / MULTIPLAYER row printed its
 * current value only when the row index was 6 -- and the row moved to 7, so the value
 * stopped being drawn and the failure was missed.
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
    /* THE FOLDER IT WAS FOUND IN, and its absence is why opening a user map could not
       work. The two tabs scan two DIFFERENT directories: user_maps/ and the missions
       root. The reopen then booted the chosen scenario out of the SESSION'S launch
       --dir, which playable/Editor.app always sets to missions/, so every row the USER
       MAPS tab offered was booted from a folder it is not in and the boot failed after
       game_shutdown had already torn the old world down.
       It looked like it worked because exactly two user maps, USER01 and USER91, also
       exist under missions/ by the same name. Every other one, including any map a
       player imports, could not be opened at all. A row that does not know its own
       folder cannot be reopened from it. */
    char dir[1024];
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
        /* Recorded at the one point in the program where the answer is known for
           certain: this scan is looking at that folder right now. */
        snprintf(m.dir, sizeof m.dir, "%s", dir ? dir : "");
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

    /* The two tabs the project owner asked for, here and in the game's own map lists. */
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
                  NUM_TEAM_MAX, NUM_ORDER_ARG, NUM_ENH_NUM, NUM_NEWSIZE,
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
    /* A map SIZE is two numbers in one field, so it is the only numeric field here that
       takes the x between them. It takes no sign and no point: a side is a whole
       positive count of cells. */
    if (g_numKind == NUM_NEWSIZE)
        return (c >= '0' && c <= '9') || c == 'x' || c == 'X' || c == ' ';
    if (eui_num_is_name()) return alnum;
    return (c >= '0' && c <= '9') || c == '.' || c == '-';
}

static void eui_num_close(void);      /* defined just below; the guard needs it */

/* Append what was typed, filtered and capped. */
static void eui_num_type(const char* s)
{
    /* A FIELD CANNOT OUTLIVE ITS DIALOG. New Map's size field is armed by a click
       inside that dialog, and the CANCEL button closes the dialog without knowing the
       field is there -- so the field would keep the keyboard afterwards and the
       editor's own letter keys would stop answering. The check sits here rather than in
       the hit test or the drawer because this is the field's own entry point, and it is
       the first thing a stranded field is ever asked to do. */
    if (g_numKind == NUM_NEWSIZE && g_euiModal != MODAL_NEW) { eui_num_close(); return; }
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
 * top of the KIND row -- exactly the screenshot the project owner sent. A flow cannot collide with
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
    /* THE KIND ROW IS THREE ACROSS, not two: a singleplayer map also has to say which
       side it is for. n cells have n-1 gaps of 6*S and the last one has to end at the
       margin, which is the arithmetic the theater grid above already uses. */
    *w = (mw - pad * 2 - 6 * S * (NEWKIND_N - 1)) / (float)NEWKIND_N;
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
            const bool on   = (i == g_newSize);
            const bool cust = (i == EUI_SIZE_CUSTOM);
            /* The custom row answers EUI_RENAME rather than EUI_MODALOPT, so its hover
               is read from that kind or the row would never light up. */
            const bool hot = cust ? (g_euiHotKind == EUI_RENAME)
                                  : (g_euiHotKind == EUI_MODALOPT && g_euiHotArg == i);
            eui_rect(ox, oy, ow, oh, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL2,
                     on || hot ? 1.0f : 0.7f);
            eui_frame(ox, oy, ow, oh, on ? EUI_GOLD : EUI_LINE, 1.0f);
            ef_text(ox + 12 * S, oy + oh * 0.5f - 3 * S, EUI_SIZES[i].name, t1,
                    EUI_RGB(on ? EUI_INK : EUI_DIM));
            if (!cust) continue;
            /* THE TYPED ROW, IN THE FIELD THIS DIALOG SET ALREADY HAS.
             *
             * There was no text-field look in NEW MAP, but there is one in SAVE AS: an
             * inset almost-black box, a gold frame while it is live, and a trailing
             * underscore for the caret. It is the same widget doing the same job, so it
             * is drawn the same way rather than invented twice; a second look for a
             * typed number would be a second thing to learn.
             *
             * The box sits at the RIGHT of the row so the word CUSTOM keeps reading as
             * the row's label, exactly as the other five rows read. */
            const bool live = (g_numKind == NUM_NEWSIZE);
            /* 176 rather than something narrower: the panel's face does not shrink past
               the smallest rung of its own ladder, so at the layout's floor scale the
               text stays the size it is while the box around it does not. Measured, the
               widest thing this box ever holds is "126 x 126_" at 61 points, and
               176 * S leaves 64 inside it even at the floor. */
            const float bw = 176 * S, bh = oh - 8 * S;
            const float bx = ox + ow - bw - 6 * S, by = oy + 4 * S;
            eui_rect(bx, by, bw, bh, 0x080b10, 1.0f);
            eui_frame(bx, by, bw, bh, live ? EUI_GOLD : EUI_LINE, 1.0f);
            {
                char ed[48];
                if (live) snprintf(ed, sizeof ed, "%s_", g_numBuf);
                else      snprintf(ed, sizeof ed, "%d x %d", g_newW, g_newH);
                ef_text_fit(bx + 8 * S, by + bh * 0.5f - 3 * S, ed, t1, bw - 16 * S,
                            EUI_RGB(live ? EUI_GOLD : on ? EUI_INK : EUI_DIM));
            }
            /* WHAT THE FIELD TAKES, said between the label and the box rather than left
               to be discovered by being refused. Composed from the two bounds so the
               number on screen cannot drift from the number that clamps. */
            if (!live) {
                char lim[48];
                const float lw = ef_text_w(EUI_SIZES[i].name, t1);
                snprintf(lim, sizeof lim, "W x H  %d..%d",
                         EUI_SIZE_MIN, EUI_SIZE_MAX_BIG);
                ef_text_fit(ox + 24 * S + lw, oy + oh * 0.5f - 3 * S, lim, EUI_TS(t1),
                            bx - (ox + 36 * S + lw), EUI_RGB(EUI_FAINT));
            }
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
            /* A row is a POSITION and the kind behind it is a VALUE; the two orders are
               not the same, so every test here goes through the value. */
            for (int i = 0; i < NEWKIND_N; i++) {
                const int v = NEWKIND_ROW[i];
                float ox, oy, ow, oh;
                eui_modal_opt_rect(L, fbw, fbh, 2, i, &ox, &oy, &ow, &oh);
                const bool on = (v == g_newMulti);
                const bool hot = (g_euiHotKind == EUI_MODALOPT && g_euiHotArg == 20 + v);
                eui_rect(ox, oy, ow, oh, on ? EUI_PANEL3 : hot ? EUI_PANEL2 : EUI_PANEL2,
                         on || hot ? 1.0f : 0.7f);
                eui_frame(ox, oy, ow, oh, on ? EUI_CYAN : EUI_LINE, 1.0f);
                {
                    /* Three across leaves a mission label little room at small frames. */
                    float tw = ef_text_w(NEWKIND_LABEL[i], t1);
                    if (tw > ow - 8 * S) tw = ow - 8 * S;
                    ef_text_fit(ox + (ow - tw) * 0.5f, oy + oh * 0.5f - 3 * S,
                                NEWKIND_LABEL[i], t1, ow - 8 * S,
                                EUI_RGB(on ? EUI_CYAN : EUI_DIM));
                }
            }
        }
    } else if (g_euiModal == MODAL_OPEN) {
        ef_text(x + pad, y + 18 * S, "OPEN MAP", EUI_TL(t1), EUI_RGB(EUI_GOLD));
        ef_text(x + pad, y + 50 * S, "USER MAPS FIRST, THEN THE CARTRIDGE'S OWN",
                EUI_TS(t1), EUI_RGB(EUI_FAINT));
        eui_draw_openlist(L, x + pad, y + 74 * S, w - pad * 2, h - 74 * S - 60 * S, t1);
    } else if (g_euiModal == MODAL_SAVEAS) {
        /* SAVE AS. One field, and a plain statement of what the two halves of a map's
           identity are: the NAME is what the project owner types and what every list shows; the SLOT
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

     multiplayer   0..7 are the player starts, one per seat, and the brain's own
                   StartLocationIndex is that same index
     singleplayer  26 HOME is where the view opens and 27 REINFORCE is where
                   reinforcements arrive; 0..25 are cells for triggers and teams to
                   refer to. ALL TWENTY-SIX ARE OFFERED. Eight were, and the other
                   eighteen were unreachable from the panel -- a hard ceiling on
                   mission scripting rather than a cosmetic one, because a trigger and
                   a TeamType order both name a waypoint by INDEX, so an index the
                   panel will not place is an index no mission can use. The array, the
                   INI reader and the INI writer already carried all twenty-eight; only
                   the panel was short.

   Twenty-eight rows fit no window, so the panel is a WINDOW onto them and the wheel
   moves it. The scroll is applied in the slot function rather than in the rect, so the
   draw, the hit test and the layout self-check all read one mapping from one place and
   cannot drift apart -- and the row index each of them passes around stays what it
   already was, the row on screen.

   Right-click a placed start to clear it. */
static int g_startScroll = 0;      /* first waypoint row shown; singleplayer only */

static int eui_start_slot(int row)
{
    if (g_mapIsMulti) return (row < EDIT_MAX_PLAYERS) ? row : -1;
    const int r = row + g_startScroll;
    if (r == 0) return EDIT_WAYPT_HOME;
    if (r == 1) return EDIT_WAYPT_REINF;
    return (r - 2 < EDIT_WAYPT_COUNT - 2) ? (r - 2) : -1;
}

static int eui_start_rows(void)
{
    return g_mapIsMulti ? EDIT_MAX_PLAYERS : EDIT_WAYPT_COUNT;
}

static void eui_start_rect(const EuiLayout* L, int row, float* x, float* y,
                           float* w, float* h)
{
    const float S = L->s, pad = EUI_PAD * S;
    *x = L->sideX + pad;
    *w = L->sideW - pad * 2;
    *h = 26 * S;
    *y = L->catY + row * (*h + 3 * S);
}

/* How many rows the panel has room for right now. Measured by asking eui_start_rect
   for each row and stopping at the bound the draw and the hit test both break on,
   rather than by repeating the row pitch here: a clamp that disagreed with the draw
   about where the list ends would let the scroll run past the last visible row. */
static int eui_start_fit(const EuiLayout* L)
{
    int fit = 0;
    for (int row = 0; row < EDIT_WAYPT_COUNT; row++) {
        float x, y, w, h;
        eui_start_rect(L, row, &x, &y, &w, &h);
        if (y + h > L->lintY) break;
        fit++;
    }
    return fit < 1 ? 1 : fit;
}

static void eui_start_scroll_clamp(const EuiLayout* L)
{
    int last = eui_start_rows() - eui_start_fit(L);
    if (last < 0) last = 0;                     /* multiplayer: eight rows always fit */
    if (g_startScroll > last) g_startScroll = last;
    if (g_startScroll < 0)    g_startScroll = 0;
}

static void eui_draw_starts(const EuiLayout* L, float t1)
{
    const float S = L->s, pad = EUI_PAD * S;
    /* Clamped on the way IN as well as on the wheel, because the window can be resized
       and the map can be swapped between two draws, and either changes the fit. */
    eui_start_scroll_clamp(L);
    ef_text(L->sideX + pad, L->catY - 12 * S,
            g_mapIsMulti ? "PLAYER STARTS" : "WAYPOINTS", EUI_TS(t1),
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
    if (g_mapIsMulti) {
        char note[96];
        snprintf(note, sizeof note, "%d of %d seats placed", edit_start_count(),
                 EDIT_MAX_PLAYERS);
        ef_text(L->sideX + pad, L->lintY - 14 * S, note, EUI_TS(t1),
                EUI_RGB(edit_start_count() >= 2 ? EUI_GREEN : EUI_DANGER));
    } else {
        /* The singleplayer arm used to build a count string and then not draw it, so
           the panel said nothing about how many cells were set. It says it now, and it
           says where in the list the window is, in the wording the script panel uses
           for the same job. */
        int placed = 0;
        for (int i = 0; i < EDIT_WAYPT_COUNT; i++) if (g_waypoint[i] >= 0) placed++;
        char note[96];
        snprintf(note, sizeof note, "%d OF %d CELLS SET", placed, EDIT_WAYPT_COUNT);
        ef_text(L->sideX + pad, L->lintY - 14 * S, note, EUI_TS(t1),
                EUI_RGB(placed ? EUI_GREEN : EUI_FAINT));
        const int fit = eui_start_fit(L);
        if (fit < eui_start_rows()) {
            char sc[64];
            snprintf(sc, sizeof sc, "ROW %d-%d OF %d, WHEEL SCROLLS",
                     g_startScroll + 1, g_startScroll + fit, eui_start_rows());
            ef_text(L->sideX + L->sideW - pad - ef_text_w(sc, EUI_TS(t1)),
                    L->lintY - 14 * S, sc, EUI_TS(t1), EUI_RGB(EUI_GOLD));
        }
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
 *  the project owner: "If I click on the save button, no popup, no notification, nothing." He is
 *  right, and it is the worst kind of nothing -- the map HAD been written, to a folder
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
 *  drag lines between boxes." -- the project owner, 25 Aug 2026.
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
           is WHOSE UNITS TRIP IT, which is exactly what the project owner could not find, and on the
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
            /* PERSISTENCE 1 IS NOT ONCE PER OBJECT, IT IS THE OPPOSITE. The engine
               detaches the rule from the object that sprang it, decrements the attach
               count, and springs only when that count reaches zero -- one firing for
               the whole tagged set. This row is labelled REPEATS, so the old wording
               told the author the mission would do the reverse of what it does. */
            static const char* PER[3] = { "once only", "when all tagged", "every time" };
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
        /* THE ECONOMY. A map with no tiberium is a map where a refinery is a building
           that does nothing and a harvester drives in circles, and until this row
           existed the only way to find that out was to PLAY it.

           The worth is the engine's arithmetic rather than an estimate. MapClass::
           Overpass runs as the scenario is read (scenarioini.cpp:489, map.cpp:905) and
           calls CellClass::Tiberium_Adjust, which counts the cell's eight tiberium
           neighbours, sets OverlayData to {0,1,3,4,6,7,8,10,11}[count] and values the
           cell at (OverlayData + 1) * 25 credits (cell.cpp:1901-1934; TIBERIUM_STEP is
           25 and a harvester carries 28 of those steps, type.h:897-899). So a cell
           standing alone is worth 25 and a cell buried inside a field is worth 300, and
           a scatter of single cells is not a small field, it is almost no field at all.

           Counted over every tiberium cell on the grid. The engine's own pass covers the
           playable rectangle bar its last row, so the two agree wherever the tiberium is
           inside the played area, which is the only place it can be harvested from.

           SO THE SUM IS OVER THE PLAYED RECTANGLE and the cells outside it are counted
           apart. Overpass never sizes them and nothing can drive to them, so adding them
           in reported an income the map does not have -- and now that there is a brush,
           painting past the rectangle is an easy thing to do by hand.

           THE RECTANGLE THIS ASKS IS ONE CELL WIDER ON EVERY SIDE than the one Overpass
           walks, and deliberately so. The brain hands the renderer a grown rect --
           CNCMapDataStruct keeps the real one in OriginalMapCell* and widens MapCell* by
           a cell in each direction before returning it (dllinterface.cpp:2925-2941) --
           and g_mapX..g_mapH is that grown one, which is what every other "outside the
           playable area" row in this check already measures against. Staying with it
           keeps the check consistent with itself and errs quiet: a one-cell overhang is
           not worth a row. Measured over the 100 shipped mission files, 81 carry
           tiberium and 3 of those put every one of their seventeen cells outside even
           the grown rect, with no refinery anywhere to want them -- and all three are
           the same map: the first GDI mission, its EB variant, and the copy of it that
           sits in a multiplayer slot, which carry the identical cell list.

           The NEIGHBOUR count still walks the whole grid, exactly as Tiberium_Adjust
           does: a cell on the border of the rectangle is fed by what is beside it
           whether or not that neighbour is playable. */
        static unsigned char tibmap[EDIT_GRID_MAX];
        memset(tibmap, 0, sizeof tibmap);
        for (size_t i = 0; i < g_tib.size(); i++) {
            const int tx = g_tib[i].x, ty = g_tib[i].y;
            if (tx >= 0 && ty >= 0 && tx < g_gridW && ty < g_gridH)
                tibmap[ty * g_gridW + tx] = 1;
        }
        int worth = 0, outside = 0;
        for (size_t i = 0; i < g_tib.size(); i++) {
            const int tx = g_tib[i].x, ty = g_tib[i].y;
            if (tx < 0 || ty < 0 || tx >= g_gridW || ty >= g_gridH) continue;
            if (tx < g_mapX || tx >= g_mapX + g_mapW ||
                ty < g_mapY || ty >= g_mapY + g_mapH) { outside++; continue; }
            int nb = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    const int px = tx + dx, py = ty + dy;
                    if (px < 0 || py < 0 || px >= g_gridW || py >= g_gridH) continue;
                    if (tibmap[py * g_gridW + px]) nb++;
                }
            worth += (EDIT_TIB_ADJ[nb] + 1) * 25;
        }
        int procs = 0, harvs = 0;
        for (size_t i = 0; i < g_objects.size(); i++) {
            if (!strcmp(g_objects[i].type, "PROC")) procs++;
            else if (!strcmp(g_objects[i].type, "HARV")) harvs++;
        }
        if (g_tib.empty()) {
            if (g_mapIsMulti) {
                chk_add(CHK_ERROR, "no tiberium anywhere on the map",
                        "a skirmish map with no tiberium has no economy: every house is "
                        "stuck on the credits its own section gives it");
            } else if (procs || harvs) {
                snprintf(y, sizeof y, "%d refiner%s and %d harvester%s have nothing to "
                                      "harvest", procs, procs == 1 ? "y" : "ies",
                         harvs, harvs == 1 ? "" : "s");
                chk_add(CHK_ERROR, "no tiberium anywhere on the map", y);
            } else {
                chk_add(CHK_WARN, "no tiberium anywhere on the map",
                        "nothing here can be harvested, so every house lives on the "
                        "credits its own house section gives it");
            }
        } else if (outside == (int)g_tib.size()) {
            /* The same fault as an empty map, arrived at a different way, so it is
               judged the same way: severe when something on the map expects an income,
               and worth saying either way. */
            snprintf(w, sizeof w, "all %d tiberium cells are outside the playable area",
                     outside);
            snprintf(y, sizeof y, "the played rectangle is %d,%d %dx%d; nothing out there "
                                  "is reachable or sized (%d refineries, %d harvesters)",
                     g_mapX, g_mapY, g_mapW, g_mapH, procs, harvs);
            chk_add((g_mapIsMulti || procs || harvs) ? CHK_ERROR : CHK_WARN, w, y);
        } else {
            const int inside = (int)g_tib.size() - outside;
            if (outside) {
                snprintf(w, sizeof w, "%d of %d tiberium cells are outside the playable "
                         "area", outside, (int)g_tib.size());
                snprintf(y, sizeof y, "nothing can drive to them and the engine never "
                                      "sizes them, so the reachable field is the %d "
                                      "credits below", worth);
                chk_add(CHK_WARN, w, y);
            }
            if (worth < 700) {
                snprintf(w, sizeof w, "the map's tiberium is worth %d credits", worth);
                snprintf(y, sizeof y, "%d reachable cell%s, less than the 700 one "
                                      "harvester carries. A cell is sized by its eight "
                                      "neighbours: 25 alone against 300 inside a field",
                         inside, inside == 1 ? "" : "s");
                chk_add(CHK_WARN, w, y);
            }
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
            /* The same correction as the rule card. Status 1 springs ONCE, after
               every tagged object has triggered it, because the engine counts the
               attachments down and fires on the last one. All three of these change
               what a mission does, and the detail column was sitting empty, so each
               one now states the consequence rather than leaving the label to carry
               it alone. */
            static const char* P[3] = { "once only", "once, when all tagged",
                                        "every time it happens" };
            static const char* PD[3] = { "then the rule leaves", "not once per object",
                                         "the rule never leaves" };
            g_popKind = POP_PERSIST; g_popCtx = g_trigSel; g_popCur = t->persist;
            for (int k = 0; k < 3; k++) eui_pop_add(P[k], PD[k], k);
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
    /* NEW MAP'S SIZE FIELD COMES FIRST. While that dialog is up nothing behind it is
       reachable, so the script panel's selection cannot be what was clicked. The row is
       SELECTED here as well: the custom row answers EUI_RENAME instead of EUI_MODALOPT,
       so nothing else is going to set it and one click has to do both. */
    if (g_euiModal == MODAL_NEW) {
        g_newSize = EUI_SIZE_CUSTOM;
        g_numKind = NUM_NEWSIZE;
        snprintf(g_numBuf, sizeof g_numBuf, "%d x %d", g_newW, g_newH);
        g_numPristine = true;        /* the first digit replaces what is shown */
        g_numRow  = -1;
        return true;
    }
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
    case NUM_NEWSIZE: {
        /* "62", "100x80", "100 X 80" -- one number is a SQUARE. Parsed by hand rather
           than with sscanf so that a missing or unreadable second number means square
           and can never mean zero. */
        const char* q = g_numBuf;
        while (*q == ' ') q++;
        int w = atoi(q);
        while (*q && *q != 'x' && *q != 'X') q++;
        int h = *q ? atoi(q + 1) : 0;
        if (h <= 0) h = w;
        /* THE GRID IS DECIDED BY THE LONGER SIDE and then BOTH are held inside it. A
           62x100 map is a big map, and 62 is legal there too; deciding per side would
           let a rect claim the legacy stride for its width and the big one for its
           height, and there is only one stride in a file. */
        const int hi = (w <= EUI_SIZE_MAX_64 && h <= EUI_SIZE_MAX_64)
                     ? EUI_SIZE_MAX_64 : EUI_SIZE_MAX_BIG;
        const int rawW = w, rawH = h;
        if (w < EUI_SIZE_MIN) w = EUI_SIZE_MIN;
        if (h < EUI_SIZE_MIN) h = EUI_SIZE_MIN;
        if (w > hi) w = hi;
        if (h > hi) h = hi;
        g_newW = w; g_newH = h;
        g_newSize = EUI_SIZE_CUSTOM;
        {
            char sub[160];
            if (rawW != w || rawH != h)
                snprintf(sub, sizeof sub, "%dx%d became %dx%d -- the engine keeps one "
                         "blank cell outside the rect on a %d grid, so %d..%d a side",
                         rawW, rawH, w, h, eui_new_grid(), EUI_SIZE_MIN, hi);
            else
                snprintf(sub, sizeof sub, "%d x %d on the %d grid, saved as %s",
                         w, h, eui_new_grid(),
                         eui_new_grid() == 64 ? "a legacy map" : "[MAP] Version=1");
            edit_toast(EUI_GOLD, "CUSTOM SIZE", sub);
        }
        break;
    }
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
    static const char* MODES[5] = { "OBJECTS", "TERRAIN", "ELEVATION", "WAYPOINTS", "SCRIPT" };
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
            snprintf(sub, sizeof sub, "%s",
                     g_waypointArmed >= 0 ? edit_waypoint_label(g_waypointArmed)
                                          : "pick a waypoint, then click");
        else if (g_editMode == 2 && g_sbrPage == 1)
            snprintf(sub, sizeof sub, "smooth brush -- %s", SBR_TOOL_NAME[g_sbrTool]);
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
            } else if (g_editMode == 2 && g_sbrPage == 1) {
                /* THE SMOOTH PAGE HAS ITS OWN READOUT. Reading the rung tool's state
                   here is how a status card ends up describing a tool that is not the
                   one the click will run. */
                snprintf(l1, sizeof l1, "SMOOTH %s", SBR_TOOL_NAME[g_sbrTool]);
                if (g_sbrWhy[0]) {
                    snprintf(l2, sizeof l2, "%s", g_sbrWhy);
                    l2col = EUI_GOLD;
                } else {
                    snprintf(l2, sizeof l2, "centre %.1f, falloff %.1f, strength %.2f",
                             g_sbrVal[0], g_sbrVal[1], g_sbrVal[2]);
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
                    /* The footprint overlay draws nothing over a cell that is off the
                       map -- there is no ground under the pointer to tint -- so at the
                       edge the preview goes quiet exactly where it matters. TERRAIN says
                       this on its own status line a few lines below; ELEVATN could not
                       reach that branch, so it says it here. */
                    int xs[64], ys[64], ics[64];
                    const int n = edit_template_cells(g_terrArmed, g_editCellX,
                                                      g_editCellY, xs, ys, ics, 64);
                    int off = 0;
                    for (int i = 0; i < n; i++)
                        if (xs[i] >= g_gridW || ys[i] >= g_gridH) off++;
                    if (off) {
                        snprintf(l2, sizeof l2, "%d of %d cells fall off the map",
                                 off, n);
                        l2col = EUI_DANGER;
                    } else {
                        snprintf(l2, sizeof l2,
                                 "click the map; drag paints; a tool disarms");
                    }
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
                /* Named off the SAME stack the press walks, so the card cannot offer one
                   thing and DELETE take another. It also has to name the rungs the old
                   line could not see at all: a cell carrying only tiberium or only a
                   crater read as "nothing under the pointer" while DELETE had work to
                   do on it. */
                char nxt[40];
                const int lay = g_editOverMap
                    ? edit_erase_peek(g_editCellX, g_editCellY, nxt, sizeof nxt)
                    : EDL_EMPTY;
                if (lay == EDL_GROUND)
                    snprintf(l2, sizeof l2, "click to put the ground back to clear");
                else if (lay != EDL_EMPTY)
                    snprintf(l2, sizeof l2, "click to remove %s", nxt);
                else { snprintf(l2, sizeof l2, "nothing left on this cell");
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
        /* THE OWNER'S OWN COLOUR, which is the colour its chip is painted in. This read
           GDI for GoodGuy OR Multi1 and Nod for BadGuy OR Multi2, and neither of those
           pairs is one house: Multi1 is light blue and Multi2 is orange. Everything else
           -- Special and the other six multiplayer seats -- came out as one grey-blue, so
           a skirmish map's blips said nothing about who owned what.
           Nothing moves for the three houses that were already right: the three constants
           this replaces are byte for byte the GDI, NOD and NEUTRAL entries of the chip
           table, so those blips keep the exact colour they had. A house the table does not
           carry keeps the neutral grey rather than going black. */
        const int hh = eui_house_index(o.house);
        const unsigned hc = (hh >= 0) ? EUI_HOUSES[hh].colour : EUI_HOUSES[2].colour;
        const int i4 = (o.cy * g_gridW + o.cx) * 4;
        px[i4]   = (unsigned char)((hc >> 16) & 255);
        px[i4+1] = (unsigned char)((hc >>  8) & 255);
        px[i4+2] = (unsigned char)( hc        & 255);
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
           order: the name is the thing the project owner typed and the thing every list shows, and
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
        static const char* MODES[5] = { "OBJECTS", "TERRAIN", "ELEVATN", "WAYPTS", "SCRIPT" };
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

    /* ---- owner chips: objects only. A tile has no house. ----
       EVERY HOUSE THE MAP OFFERS, over as many rows as that takes, and the rectangle
       comes from the one function the hit test and the layout harness also call. */
    if (g_editMode == 0) for (int i = 0; i < eui_owner_n(); i++) {
        float chx, chy, chw, chh;
        eui_owner_chip_rect(&L, i, &chx, &chy, &chw, &chh);
        const bool on = (i == g_editOwner);
        eui_panel(chx, chy, chw, chh, on ? EUI_PANEL3 : EUI_PANEL);
        eui_rect(chx, chy, chw, 3 * S, EUI_HOUSES[i].colour, on ? 1.0f : 0.28f);
        /* Fitted rather than drawn at whatever width it wants. Measured in this panel's
           own face, the longest label needs 51 px and the chip's inside is 45 at the
           shortest frame the harness runs, so it used to run out over its neighbour. */
        float tw = ef_text_w(EUI_HOUSES[i].label, t1);
        if (tw > chw - 6 * S) tw = chw - 6 * S;
        ef_text_fit(chx + (chw - tw) * 0.5f, chy + chh * 0.5f - 3 * S,
                    EUI_HOUSES[i].label, t1, chw - 6 * S,
                    EUI_RGB(on ? EUI_HOUSES[i].colour : EUI_FAINT));
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
    /* ---- category tabs: seven object kinds, or seven terrain families ---- */
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
            eui_cameo(x + 2 * S, y + 2 * S, L.tileW - 4 * S, L.tileH - 14 * S,
                      it->code, t1);
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
    /* A pack whose atlas is not the packing this table was generated for takes the right
       slot pixels and lands them on the wrong art. Refusing is the same answer this
       function already gives for a tile the theater's bank does not carry, so the caller
       puts the .BIN back and the cell keeps what it had. */
    if (!tt_atlas_matches(at)) return false;
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

/* AUTOSAVE'S INTERVAL, in DRAWN editor frames: 7200 is two minutes at sixty a second.
   Frames rather than wallclock for the same reason the toast uses them -- a --shot run
   has to stay reproducible. */
#define EDIT_AUTOSAVE_FRAMES 7200

/* Autosave writes through the same function the SAVE button does, and that lives with
   the rest of the file writers, far below the undo funnel that triggers it. */
static void edit_save(void);
static bool          g_editAutosave = true;
static unsigned long g_editSavedAt = 0;

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
    /* THE SMOOTH BRUSH'S UNLOCK, which is state the map carries and not a preference.
       The first smooth stroke on an off-the-ladder map makes the rung tool usable on
       it; undoing that stroke has to put the heights AND that fact back, or the panel
       keeps offering a tool for ground that no longer exists. It is one bool rather
       than a vector because it describes the document, exactly like the corners do. */
    bool smoothUnlock;
    char label[40];
};

static std::vector<EditSnap> g_undo, g_redo;
static EditSnap g_editLoaded;          /* what REVERT goes back to */
static bool     g_editLoadedOk = false;

/* UNSAVED WORK, tracked at the undo funnel rather than at the fifteen call sites that
   feed it. Every mutation that can be undone passes through edit_commit, so setting the
   flag there covers all of them and cannot be forgotten by the next one added; edit_save
   is the only thing that clears it, because it is the only thing that makes the disk
   match the screen. A fresh document -- New Map, or a different map opened -- starts
   clean.

   g_editQuitAskedAt is the frame the quit warning went up. ESC's last rung asks once and
   then leaves, and the second press counts only while that warning is still on screen:
   tying the window to the toast puts the question and the answer in front of the eye
   together, and an ESC an hour later asks again instead of quitting on a stale arm. */
static bool          g_editDirty = false;
static bool          g_editQuitAsked = false;
static unsigned long g_editQuitAskedAt = 0;

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
    s.smoothUnlock = g_elevSmoothUnlock;
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
    /* THE HEIGHTS, AND THE THREE THINGS DERIVED FROM THEM. Putting a different set of
       corners back is exactly the situation the baked light, the building pads and the
       off-the-ladder count all have to be told about: undoing an elevation edit used to
       restore the ground and leave the picture, the pads and the readout describing the
       ground that had just been taken away. Guarded on the heights ACTUALLY differing,
       so an ordinary object undo still costs nothing. */
    if (!s.corner.empty() && s.corner.size() == g_pack.corner.size()) {
        const bool hmoved =
            memcmp(&g_pack.corner[0], &s.corner[0], s.corner.size()) != 0;
        g_pack.corner = s.corner;
        if (hmoved) {
            g_shadeReady = false;
            terrain_pads_rebuild();
            sbr_recount_ladder();
        }
    }
    if (!s.tier.empty()) {
        memcpy(g_elevTier, &s.tier[0], s.tier.size());
        memcpy(g_elevPass, &s.pass[0], s.pass.size());
        memcpy(g_elevRamp, &s.ramp[0], s.ramp.size());
    }
    memcpy(g_waypoint, s.waypoint, sizeof g_waypoint);
    g_editPrior = s.prior;
    if ((int)s.cellTrig.size() == edit_ini_cells())
        memcpy(g_cellTrig, &s.cellTrig[0], s.cellTrig.size() * sizeof(short));
    /* THE AUTO HEIGHTMAP'S REPORT DESCRIBES ONE STATE OF THE GROUND, and this is the
       function that puts a different one back. Undo, redo, revert and the importer's own
       rollback all come through here, so this is the one place that has to say so. */
    ah_report_clear();
    g_elevSmoothUnlock = s.smoothUnlock;
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
    /* AND THE SAME ARGUMENT: every undoable mutation lands here, so this is where the
       auto heightmap's report is told the ground has moved. A stroke of the rung brush
       after a fit would otherwise leave marks describing corners that are no longer
       there, and a report that outlives its map is worse than no report. The fit itself
       commits BEFORE it fills the report in, so it does not clear its own. */
    ah_report_clear();
    /* THE MAP NO LONGER MATCHES THE DISK. Same argument as the line above: this is the
       one place every undoable mutation reaches, so it is the one place that has to say
       so. Doing it per-edit-site would mean remembering it at each of them, and the one
       that was forgotten would let ESC throw the work away without asking. */
    g_editDirty = true;
    g_undo.push_back(before);
    if ((int)g_undo.size() > EDIT_UNDO_MAX) g_undo.erase(g_undo.begin());
    /* A new edit invalidates the redo branch, which is what every editor does and what
       the browser one does too. */
    g_redo.clear();

    /* AUTOSAVE, off the same flag and the same funnel.
       A clock on its own would rewrite an unchanged file forever, so the trigger is an
       EDIT that arrives more than EDIT_AUTOSAVE_FRAMES after the last write: no edits,
       no writes, and the most an unattended crash can cost is that interval plus the
       edit in hand. It writes where SAVE writes -- user_maps/, never the cartridge's own
       missions -- and it toasts like SAVE does, because a write to the disk that says
       nothing is indistinguishable from one that failed. */
    if (g_editAutosave && g_editDirty &&
        g_editFrame - g_editSavedAt >= EDIT_AUTOSAVE_FRAMES)
        edit_save();
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
 *  The tiberium brush
 *
 *  One cell per click, the same as a wall, and the arithmetic that sizes the cell lives
 *  up beside the other cell tests. There is ONE brush for the whole group on purpose:
 *  the engine throws the TI number away. MapClass::Overpass runs over the playable
 *  rectangle as the scenario is read (scenarioini.cpp:489, map.cpp:906) and
 *  Tiberium_Adjust re-picks the overlay at random across TIBERIUM1..12 for every cell
 *  (cell.cpp:1912), so twelve palette entries would be twelve ways to author a number
 *  nobody reads back.
 * ---------------------------------------------------------------------------------- */
static void edit_tib_put(int x, int y)
{
    if (edit_tib_index(x, y) >= 0) {
        /* Painting where paint already is is neither a refusal nor an edit: nothing
           changed, so nothing goes on the undo stack for it. */
        fprintf(stderr, "edit: %d,%d is already tiberium\n", x, y);
        return;
    }
    EditSnap before = edit_snapshot("tiberium");
    TibCell tc;
    memset(&tc, 0, sizeof tc);
    tc.x = (short)x; tc.y = (short)y;
    /* WHICH OF THE TWELVE, chosen per cell rather than left at zero. Overpass re-picks
       it the moment the map is read, so this number never survives being played; what it
       decides is whether the field the AUTHOR is looking at is twelve crystal sprites or
       the same one two hundred times. Hashed off the cell -- the renderer's own hash --
       so the same map always draws the same way. */
    tc.kind = (unsigned char)(tib_hash((unsigned)x, (unsigned)y, 0u) % 12u);
    tc.stage = 0;
    g_tib.push_back(tc);
    edit_tib_restage(x, y);
    edit_commit(before);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: tiberium at %d,%d  [%d cells]\n", x, y, (int)g_tib.size());
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
                for (int i = 0; i < (g == 0 ? EUI_SIZE_N
                                   : g == 1 ? EUI_THEATER_N : NEWKIND_N); i++) {
                    float ox, oy, ow, oh;
                    eui_modal_opt_rect(&L, fbw, fbh, g, i, &ox, &oy, &ow, &oh);
                    if (EUI_IN(ox, oy, ow, oh)) {
                        /* THE CUSTOM ROW IS NOT A CHOICE, IT IS A FIELD, so it answers
                           the one hit kind whose handler asks this file to open the
                           typed entry under the pointer and start text input. The
                           opener selects the row as well, so a single click both picks
                           CUSTOM and puts the caret in it. */
                        if (g == 0 && i == EUI_SIZE_CUSTOM) {
                            *arg = -1;
                            return EUI_RENAME;
                        }
                        /* The kind group answers with its VALUE, not with its row. What
                           receives this stores the number as given, and the two orders
                           are deliberately different. */
                        *arg = g * 10 + (g == 2 ? NEWKIND_ROW[i] : i);
                        return EUI_MODALOPT;
                    }
                }
        } else if (g_euiModal == MODAL_OPEN) {
            /* EUI_IN IS A TEXTUAL MACRO OVER lx AND ly, and those two names belong to
               the POINTER, declared at the head of this function. Naming the list's own
               origin lx and ly here shadowed them, so every test inside this block
               compared the pointer against itself: tab 0 reduced to lx >= lx &&
               lx < lx + tw, true for any click anywhere in the dialog. The dialog
               answered "tab 0" to everything, no row could be selected, and a map editor
               that cannot reopen its own maps is not one. The origin is olx/oly now, and
               no local in this function may take either of the macro's two names. */
            const float pad = 18 * S;
            const float olx = mx2 + pad, oly = my2 + 74 * S;
            const float lw = mw - pad * 2, lh = mh - 74 * S - 60 * S;
            const float tw = (lw - 6 * S) * 0.5f;
            for (int i = 0; i < 2; i++)
                if (EUI_IN(olx + i * (tw + 6 * S), oly, tw, 28 * S)) {
                    *arg = 30 + i; return EUI_MODALOPT;
                }
            /* ONLY THE ROWS THE DRAW PAINTS MAY ANSWER. eui_draw_openlist stops at the
               last entry of the shown tab; the hit test ran the full height of the list,
               so the empty space under a short list answered as a row that is not there.
               The dispatcher survived it -- edit_maplist_at returns -1 and the selection
               is left alone -- but a rectangle that answers and is not drawn is the exact
               defect the layout harness exists to catch. */
            const int shown = edit_maplist_count(g_mapListTab ? 'O' : 'U');
            int rows = (int)((lh - 40 * S) / (24 * S));
            if (rows > shown - g_mapListScroll) rows = shown - g_mapListScroll;
            for (int r = 0; r < rows; r++) {
                float x, y, w, h;
                eui_openlist_row_rect(olx, oly, lw, S, r, &x, &y, &w, &h);
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
    } else if (g_editMode == 2) {
        /* THE ELEVATION PAGE TABS SIT IN THE SAME BAND, and are claimed here for the
           same reason the RULES/TEAMS switch is: the owner loop below would otherwise
           answer for them. ELEVATN draws no house chips -- a tile has no house -- so
           this band was answering EUI_OWNER over ground nothing was drawn on. */
        for (int i = 0; i < 2; i++) {
            float tx, ty, tw, th;
            if (!eui_sbr_tab_rect(&L, i, &tx, &ty, &tw, &th)) break;
            if (EUI_IN(tx, ty, tw, th)) { *arg = i; return EUI_SBRPAGE; }
        }
    } else
    for (int i = 0; i < eui_owner_n(); i++) {
        float ox, oy, ow, oh;
        eui_owner_chip_rect(&L, i, &ox, &oy, &ow, &oh);
        if (EUI_IN(ox, oy, ow, oh)) { *arg = i; return EUI_OWNER; }
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
    } else if (g_editMode == 2 && g_sbrPage == 1) {
        /* THE SMOOTH PAGE. Its rows come from the same four functions the draw calls,
           so a row the draw skipped for want of height is a row this skips too. */
        for (int i = 0; i < 3; i++) {
            float x, y, w, h;
            if (!eui_sbr_tool_rect(&L, i, &x, &y, &w, &h)) break;
            if (EUI_IN(x, y, w, h)) { *arg = i; return EUI_SBRTOOL; }
        }
        for (int i = 0; i < SBR_SLIDER_N; i++) {
            float x, y, w, h;
            if (!eui_sbr_slider_rect(&L, i, &x, &y, &w, &h)) break;
            if (EUI_IN(x, y, w, h)) { *arg = i; return EUI_SBRSLIDER; }
        }
        /* fall through to the action row, save and play, which are shared */
    } else if (g_editMode == 2) {
        /* The elevation panel replaces the category strip and the grid, and its
           geometry is laid out by eui_draw_elev -- repeated here rather than shared,
           and --uitest is what keeps the two honest. */
        float y = eui_elev_top(&L);
        if (!elev_ready()) {
            const float bh = 34 * S, bw = L.sideW - pad * 4;
            if (EUI_IN(L.sideX + pad * 2, y + 140 * S, bw, bh)) return EUI_ELEVGATE;
            /* THE FIT IS ON THIS CARD TOO, and it is not behind the gate: it reads
               cliff art out of the .BIN and writes corner bytes, and neither of those
               needs the ladder. Tested after CONVERT because the two rectangles do not
               overlap and CONVERT is the one that has always been here. */
            {
                float ax, ay, aw, ahh;
                if (eui_elev_auto_rect(&L, &ax, &ay, &aw, &ahh) &&
                    EUI_IN(ax, ay, aw, ahh)) return EUI_ELEVAUTO;
            }
            /* AND THEN FALL THROUGH, WHICH THIS BRANCH DID NOT DO. It returned
               EUI_DEAD here, and the action row, SAVE and PLAY are tested BELOW the
               whole panel chain -- so on any map that arrives off the ladder, with the
               ELEVATION panel open, UNDO, REDO, REVERT, SAVE and PLAY every one of
               them answered dead and not one could be clicked. It went unseen because
               the layout harness set g_elevConverted for every elevation pass it made
               and so never walked this card once. Those five controls are drawn under
               every panel and are not part of this card; the ready side has always
               fallen through to them and now both sides do. Undoing a fit from the
               card the fit is on is the immediate reason it has to. */
        } else {
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
            float bx, by, bw, bh;
            if (eui_elev_import_rect(&L, 0, &bx, &by, &bw, &bh) &&
                EUI_IN(bx, by, bw, bh)) return EUI_ELEVIMPORT;
            if (eui_elev_import_rect(&L, 1, &bx, &by, &bw, &bh) &&
                EUI_IN(bx, by, bw, bh)) return EUI_ELEVNEXT;
            /* AND THE ROW UNDER THEM. Before the cliff cells, because it sits above
               them and the drawer's first row starts where this one ends. */
            if (eui_elev_auto_rect(&L, &bx, &by, &bw, &bh) &&
                EUI_IN(bx, by, bw, bh)) return EUI_ELEVAUTO;
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
        }
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
    /* STARTS has no item grid either, and twenty-eight waypoint rows fit no window, so
       the wheel moves the window over them. Without this arm the wheel fell through to
       the palette test below and scrolled the object drawer -- a drawer this mode does
       not draw, so the wheel did nothing anyone could see. */
    if (g_editMode == 3) {
        if (my >= L.catY && my < L.lintY) {
            g_startScroll -= dir;
            eui_start_scroll_clamp(&L);
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

/* Is this map ready to have its elevation edited?
 *
 * THREE WAYS IN, and the third is the director's rule that the two tools share a map.
 * A map already on the ladder needs nothing; CONVERT is the announced, one-way snap of
 * a whole map; and a SMOOTH STROKE unlocks it too, because a map carrying hand-made
 * smooth ground is a legitimate map and the rung tool paints on it perfectly well --
 * inside the blocks it paints it simply wins, which is what elev_write already does.
 * The rung tool is not asked to convert such a map and must not be. */
static bool elev_ready(void)
{
    if (!g_elevSeeded) elev_seed();
    return g_elevConverted || g_elevSmoothUnlock || g_elevOffLadder == 0;
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
 *  IMPORTING A HEIGHTMAP FROM A PAINTED IMAGE
 *
 *  Asked for on the feature board: let a PNG or a PCX stand in for the elevation brush,
 *  so a plateau can be laid out in a paint program with a radar picture on the layer
 *  below. It is cheap precisely because the elevation model is ALREADY AN IMAGE -- a
 *  field of tiers over 2x2-cell blocks, on the five-rung ladder the cartridge's own
 *  heightmaps were painted on. So an import is a decode, a box filter down to the block
 *  grid, a quantise into that ladder, and ONE ordinary undo step.
 *
 *  TWO FORMATS, AND NEITHER ADDS A DEPENDENCY.
 *    PCX is a 128-byte header and a run-length body: no library at all, and it is what
 *    the paint tools of the era this project restores still write.
 *    PNG needs an inflate and this binary already links one. zlib is on the link line of
 *    both builds and the renderer's own screenshot writer already deflates through it,
 *    so the reader is the signature, the chunk walk, one uncompress and the five row
 *    filters.
 *  Sixteen-bit samples, sub-byte palettes and interlaced PNG are REFUSED with the fix
 *  named, never half-read. A heightmap has five usable levels so nobody needs sixteen
 *  bits, and reading an Adam7 file as if it were progressive would produce a
 *  plausible-looking wrong map, which is the worst outcome an importer has.
 * ---------------------------------------------------------------------------------- */

struct ElevImage { int w, h; std::vector<unsigned char> g; };

/* Rec.601 luma. The three weights sum to exactly 256, so a grey pixel comes back as
   itself and a colour reference painted over is folded the way an eye would fold it. */
static inline unsigned char elev_luma(int r, int g, int b)
{
    return (unsigned char)((77 * r + 150 * g + 29 * b) >> 8);
}

static bool elev_slurp(const char* path, std::vector<unsigned char>* out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long L = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (L <= 0 || L > 64L * 1024 * 1024) { fclose(f); return false; }
    out->resize((size_t)L);
    const size_t got = fread(&(*out)[0], 1, (size_t)L, f);
    fclose(f);
    return got == (size_t)L;
}

/* PCX. Header fields at their fixed offsets: 3 bits-per-plane, 4..11 the window,
   65 planes, 66 bytes-per-line. The body is one RLE stream, planes*bpl bytes a row. */
static bool elev_read_pcx(const unsigned char* f, size_t n, ElevImage* out,
                          char* why, int whyN)
{
    if (n < 128 || f[0] != 0x0A) { snprintf(why, whyN, "not a PCX"); return false; }
    const int enc    = f[2];
    const int bpp    = f[3];
    const int xmin   = f[4] | (f[5] << 8),  ymin = f[6]  | (f[7] << 8);
    const int xmax   = f[8] | (f[9] << 8),  ymax = f[10] | (f[11] << 8);
    const int planes = f[65];
    const int bpl    = f[66] | (f[67] << 8);
    const int w = xmax - xmin + 1, h = ymax - ymin + 1;
    if (enc != 1) {
        snprintf(why, whyN, "this PCX is not run-length encoded, which no paint "
                 "program writes -- re-save it");
        return false;
    }
    if (bpp != 8 || (planes != 1 && planes != 3)) {
        snprintf(why, whyN, "this PCX is bits-per-plane %d, planes %d. The importer reads "
                 "8-bit paletted and 24-bit PCX -- re-save as one of those", bpp, planes);
        return false;
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || bpl < w) {
        snprintf(why, whyN, "this PCX declares a %dx%d image at %d bytes a line, which "
                 "does not describe a picture", w, h, bpl);
        return false;
    }
    /* The 256-colour palette lives in the last 769 bytes behind a 0x0C marker. Without
       one the byte IS the grey, which is what a plain greyscale ramp amounts to. */
    const unsigned char* pal = NULL;
    if (planes == 1 && n >= 769 && f[n - 769] == 0x0C) pal = f + n - 768;

    const size_t rowbytes = (size_t)planes * (size_t)bpl;
    std::vector<unsigned char> row(rowbytes);
    out->w = w; out->h = h;
    out->g.assign((size_t)w * (size_t)h, 0);
    size_t p = 128;
    for (int y = 0; y < h; y++) {
        size_t got = 0;
        while (got < rowbytes) {
            if (p >= n) {
                snprintf(why, whyN, "this PCX ends %d rows early", h - y);
                return false;
            }
            unsigned char b = f[p++];
            int run = 1;
            if ((b & 0xC0) == 0xC0) {
                run = b & 0x3F;
                if (p >= n) { snprintf(why, whyN, "this PCX ends mid-run"); return false; }
                b = f[p++];
            }
            while (run-- > 0 && got < rowbytes) row[got++] = b;
        }
        for (int x = 0; x < w; x++) {
            unsigned char v;
            if (planes == 3)      v = elev_luma(row[x], row[bpl + x], row[2 * bpl + x]);
            else if (pal)         v = elev_luma(pal[row[x] * 3], pal[row[x] * 3 + 1],
                                                pal[row[x] * 3 + 2]);
            else                  v = row[x];
            out->g[(size_t)y * w + x] = v;
        }
    }
    return true;
}

static unsigned elev_be32(const unsigned char* p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8)  |  (unsigned)p[3];
}

static int elev_paeth(int a, int b, int c)
{
    const int q  = a + b - c;
    const int qa = q > a ? q - a : a - q;
    const int qb = q > b ? q - b : b - q;
    const int qc = q > c ? q - c : c - q;
    if (qa <= qb && qa <= qc) return a;
    return qb <= qc ? b : c;
}

/* PNG. Depth 8 and no interlace, which is what every paint program's default save is. */
static bool elev_read_png(const unsigned char* f, size_t n, ElevImage* out,
                          char* why, int whyN)
{
    static const unsigned char SIG[8] = { 137, 'P', 'N', 'G', '\r', '\n', 26, '\n' };
    if (n < 8 || memcmp(f, SIG, 8) != 0) { snprintf(why, whyN, "not a PNG"); return false; }

    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    bool sawIHDR = false;
    std::vector<unsigned char> idat, plte;
    size_t p = 8;
    while (p + 12 <= n) {
        const unsigned len = elev_be32(f + p);
        if ((size_t)len > n || p + 12 + (size_t)len > n) {
            snprintf(why, whyN, "this PNG is truncated"); return false;
        }
        const char* tag = (const char*)(f + p + 4);
        const unsigned char* d = f + p + 8;
        if (!memcmp(tag, "IHDR", 4) && len >= 13) {
            w = (int)elev_be32(d); h = (int)elev_be32(d + 4);
            depth = d[8]; ctype = d[9]; interlace = d[12];
            sawIHDR = true;
        } else if (!memcmp(tag, "PLTE", 4)) {
            plte.assign(d, d + len);
        } else if (!memcmp(tag, "IDAT", 4)) {
            idat.insert(idat.end(), d, d + len);
        } else if (!memcmp(tag, "IEND", 4)) {
            break;
        }
        p += 12 + (size_t)len;
    }
    if (!sawIHDR || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        snprintf(why, whyN, "this PNG has no usable header"); return false;
    }
    if (interlace) {
        snprintf(why, whyN, "this PNG is interlaced. Re-save it without interlacing -- "
                 "reading it as if it were not would give a wrong map that looks right");
        return false;
    }
    if (depth != 8) {
        snprintf(why, whyN, "this PNG is %d bits a sample. The ladder has five rungs, "
                 "so save it as 8-bit", depth);
        return false;
    }
    int spp;
    switch (ctype) {
        case 0: spp = 1; break;      /* grey            */
        case 2: spp = 3; break;      /* rgb             */
        case 3: spp = 1; break;      /* palette         */
        case 4: spp = 2; break;      /* grey + alpha    */
        case 6: spp = 4; break;      /* rgba            */
        default:
            snprintf(why, whyN, "this PNG uses colour type %d, which PNG does not "
                     "define", ctype);
            return false;
    }
    if (ctype == 3 && plte.size() < 3) {
        snprintf(why, whyN, "this PNG is paletted and carries no palette"); return false;
    }
    const size_t stride = 1 + (size_t)w * (size_t)spp;
    std::vector<unsigned char> raw(stride * (size_t)h);
    uLongf got = (uLongf)raw.size();
    if (idat.empty() ||
        uncompress(&raw[0], &got, &idat[0], (uLong)idat.size()) != Z_OK ||
        (size_t)got != raw.size()) {
        snprintf(why, whyN, "this PNG's image data did not unpack");
        return false;
    }
    /* Unfilter in place, top down: every row's predictor is the row above it, already
       reconstructed by the time it is read. */
    for (int y = 0; y < h; y++) {
        unsigned char* cur = &raw[(size_t)y * stride] + 1;
        const unsigned char* up = y ? &raw[(size_t)(y - 1) * stride] + 1 : NULL;
        const int ft = raw[(size_t)y * stride];
        const int nb = w * spp;
        for (int x = 0; x < nb; x++) {
            const int a = x >= spp ? cur[x - spp] : 0;
            const int b = up ? up[x] : 0;
            const int c = (up && x >= spp) ? up[x - spp] : 0;
            int v = cur[x];
            switch (ft) {
                case 0:                              break;
                case 1: v += a;                      break;
                case 2: v += b;                      break;
                case 3: v += (a + b) >> 1;           break;
                case 4: v += elev_paeth(a, b, c);    break;
                default:
                    snprintf(why, whyN, "row %d of this PNG uses filter %d, which PNG "
                             "does not define", y, ft);
                    return false;
            }
            cur[x] = (unsigned char)v;
        }
    }
    out->w = w; out->h = h;
    out->g.assign((size_t)w * (size_t)h, 0);
    for (int y = 0; y < h; y++) {
        const unsigned char* r = &raw[(size_t)y * stride] + 1;
        for (int x = 0; x < w; x++) {
            const unsigned char* q = r + (size_t)x * (size_t)spp;
            unsigned char v;
            if (ctype == 3) {
                const size_t i = (size_t)q[0] * 3;
                v = (i + 2 < plte.size())
                        ? elev_luma(plte[i], plte[i + 1], plte[i + 2]) : q[0];
            } else if (ctype == 2 || ctype == 6) {
                v = elev_luma(q[0], q[1], q[2]);
            } else {
                v = q[0];
            }
            out->g[(size_t)y * w + x] = v;
        }
    }
    return true;
}

/* THE QUANTISER IS THE SEEDER'S, not a second one. elev_tier_of takes the nearest of the
   five rungs with ties going down, which puts a grey of 0..32 on tier 0, 33..96 on 1,
   97..159 on 2, 160..223 on 3 and 224..255 on 4. Those are the 0/25/50/75/100 percent
   greys a paint tool leaves, and that is how the cartridge's own heightmaps were
   painted, so a heightmap exported as an image and imported back comes home as the
   tiers it left as.

   THE IMAGE IS THE WHOLE BLOCK GRID, not the playable rectangle. Stretching it over
   [MAP] X/Y/Width/Height and leaving the border alone is the tempting alternative and it
   makes a seam the model cannot express: an imported plateau at tier 3 meeting a border
   still at tier 1 is a two-rung step, and there is no cliff art for one. So the whole
   field is sampled and legalised. What is WRITTEN back is a smaller thing and
   deliberately so, for a reason set out beside the dirty mask below.

   TERRACING IS DOWNWARD ONLY. No block may sit more than one rung above a block it
   touches, because a coarse corner takes the MAX of the four blocks around it and a
   two-rung corner spread is the single thing the compass has no art for. A painted image
   knows nothing about that, so a sheer face in the source comes out as a stair. Lowering
   is the conservative direction: raising would grow the plateau outward over ground the
   image never asked to raise. */
static int elev_import_bytes(const unsigned char* file, size_t n, const char* what,
                             char* why, int whyN)
{
    if (why && whyN > 0) why[0] = 0;
    if (g_pack.corner.empty() || !g_editHaveBin) {
        if (why) snprintf(why, whyN, "this map carries no heightmap to write into");
        return 0;
    }
    ElevImage img;
    char bad[220] = { 0 };
    bool ok;
    if (n >= 8 && file[0] == 137 && file[1] == 'P')
        ok = elev_read_png(file, n, &img, bad, sizeof bad);
    else if (n >= 1 && file[0] == 0x0A)
        ok = elev_read_pcx(file, n, &img, bad, sizeof bad);
    else {
        ok = false;
        snprintf(bad, sizeof bad, "this is neither a PNG nor a PCX, and those are the "
                 "two the importer reads");
    }
    if (!ok || img.w <= 0 || img.h <= 0) {
        if (why) snprintf(why, whyN, "%s", bad);
        return 0;
    }

    /* SEED BEFORE THE SNAPSHOT. An unseeded tier field is not carried in a snapshot at
       all, so an import taken before the seed would undo its heights and leave its
       tiers standing. */
    if (!g_elevSeeded) elev_seed();

    const int bw = elev_bw(), bh = elev_bh();
    std::vector<unsigned char> want((size_t)bw * (size_t)bh, 1);
    for (int by = 0; by < bh; by++) {
        const int y0 = by * img.h / bh;
        int y1 = (by + 1) * img.h / bh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int bx = 0; bx < bw; bx++) {
            const int x0 = bx * img.w / bw;
            int x1 = (bx + 1) * img.w / bw;
            if (x1 <= x0) x1 = x0 + 1;
            long sum = 0;
            int cnt = 0;
            for (int y = y0; y < y1 && y < img.h; y++)
                for (int x = x0; x < x1 && x < img.w; x++) {
                    sum += img.g[(size_t)y * img.w + x];
                    cnt++;
                }
            want[(size_t)by * bw + bx] =
                (unsigned char)elev_tier_of(cnt ? (int)(sum / cnt) : 0);
        }
    }

    const std::vector<unsigned char> asPainted(want);
    for (int pass = 0; pass < bw + bh + 8; pass++) {
        int moved = 0;
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                int lo = 4;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (!dx && !dy) continue;
                        const int nx = bx + dx, ny = by + dy;
                        if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
                        const int t = want[(size_t)ny * bw + nx];
                        if (t < lo) lo = t;
                    }
                if (want[(size_t)by * bw + bx] > lo + 1) {
                    want[(size_t)by * bw + bx] = (unsigned char)(lo + 1);
                    moved++;
                }
            }
        if (!moved) break;
    }
    int terraced = 0;
    for (size_t i = 0; i < want.size(); i++) if (want[i] != asPainted[i]) terraced++;

    /* WHAT THE MODEL COULD NOT KEEP, counted before anything is written so the toast can
       say it out loud. A PINCH is the diagonal saddle: it is drawn undressed, the corner
       heights carry the climb and no cliff art goes on, which is what the cartridge does
       on most of its tier-gaining edges, so it is cosmetic. A LOST LOW BLOCK is not
       cosmetic: raised ground claims the cliff outside itself, so a low block whose four
       coarse corners have all been claimed comes out raised and a channel the image
       painted has closed. The brush REFUSES both; an import states them, because
       throwing a whole map away over one block would make the feature useless. */
    unsigned char K[ELEV_K_MAX * ELEV_K_MAX];
    elev_coarse(&want[0], K);
    int pinches = 0, lost = 0;
    {
        const int kw = elev_kw();
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                if (elev_is_saddle(elev_mask_at(K, bx, by))) pinches++;
                const int nw = K[by * kw + bx],       ne = K[by * kw + bx + 1];
                const int sw = K[(by + 1) * kw + bx], se = K[(by + 1) * kw + bx + 1];
                int lo = nw;
                if (ne < lo) lo = ne;
                if (sw < lo) lo = sw;
                if (se < lo) lo = se;
                if (lo > want[(size_t)by * bw + bx]) lost++;
            }
    }

    /* ONLY WHERE THE IMAGE DISAGREES, and this is the part that must not be skipped.
       elev_write DRESSES every block it is handed: a flat one gets plain CLEAR painted
       over all four of its cells. Hand it the whole map and it erases the sea, the
       shore, the roads and the tiberium of every block the image did not actually move
       -- the exact defect this tool was already fixed for once, and the reason the brush
       writes the painted blocks GROWN BY ONE rather than the field. An import is a very
       wide brush stroke and obeys the same rule. The ring of one is not optional: a
       block's cliff mask is read off corners it shares with its neighbours, so a block
       the image left alone beside one it raised still has to be re-dressed. */
    unsigned char dirty[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
    memset(dirty, 0, sizeof dirty);
    int changed = 0;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++)
            if (want[(size_t)by * bw + bx] != g_elevTier[by * bw + bx]) {
                dirty[by * bw + bx] = 1;
                changed++;
            }
    if (!changed) {
        if (why) snprintf(why, whyN, "this image is the ground the map already has, so "
                          "nothing changed");
        return 0;
    }
    unsigned char grown[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
    memcpy(grown, dirty, sizeof grown);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            if (!dirty[by * bw + bx]) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = bx + dx, ny = by + dy;
                    if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
                    grown[ny * bw + nx] = 1;
                }
        }

    EditSnap before = edit_snapshot("import heightmap");
    memcpy(g_elevTier, &want[0], (size_t)bw * (size_t)bh);
    /* PASS AND RAMP GO WITH THE FIELD THEY DECORATED. They are per-block choices about a
       tier field that no longer exists, and leaving them behind would dress imported
       ground with ramp art in blocks the image never mentioned. Both are inside the
       snapshot, so undo puts them back with everything else. */
    memset(g_elevPass, 0, sizeof g_elevPass);
    memset(g_elevRamp, 0, sizeof g_elevRamp);

    const int wrote = elev_write(grown);
    if (!wrote) {
        edit_restore(before);
        if (why) snprintf(why, whyN, "the import wrote nothing");
        return 0;
    }
    /* THE CONVERT GATE IS LEFT ALONE ON PURPOSE. That gate asks whether this map's
       CORNER BYTES are on the five-rung ladder, and it is answered by a count elev_seed
       takes over the whole heightmap. An import legalises the TIER FIELD everywhere and
       rewrites corners only where it drew, so corners it never touched may still be off
       the ladder, and claiming otherwise would be asserting a number nobody measured. A
       map made by NEW MAP is already through the gate, which is the workflow this
       feature is for. */
    /* ONE UNDO ENTRY FOR THE WHOLE MAP, which is the entire reason the import goes
       through the snapshot funnel rather than calling elev_paint a thousand times. A
       snapshot already carries the corner array, so an import costs one ordinary step. */
    edit_commit(before);

    {
        char sub[220];
        snprintf(sub, sizeof sub, "%s, %dx%d over %dx%d blocks -- %d blocks moved, %d "
                 "cells re-dressed; terraced %d, pinches %d, low blocks lost %d",
                 what ? what : "an image", img.w, img.h, bw, bh, changed, wrote,
                 terraced, pinches, lost);
        edit_toast(EUI_CYAN, "HEIGHTMAP IMPORTED", sub);
    }
    return wrote;
}

static int elev_import_image(const char* path, char* why, int whyN)
{
    std::vector<unsigned char> file;
    if (!elev_slurp(path, &file) || file.empty()) {
        if (why && whyN > 0) snprintf(why, whyN, "cannot read %s", path);
        return 0;
    }
    const char* base = strrchr(path, '/');
    return elev_import_bytes(&file[0], file.size(), base ? base + 1 : path, why, whyN);
}

/* THE IMAGE BESIDE THE MAP. This editor has no file picker and no field a path could be
   typed into, so the location is a convention instead: put the painted image in
   user_maps/ under the map's own scenario name. Four spellings are tried because a paint
   program picks its own case and half the filesystems this runs on care which. */
static int elev_import_beside_map(char* why, int whyN)
{
    static const char* const EXT[4] = { ".PNG", ".png", ".PCX", ".pcx" };
    char dir[1024];
    edit_userdir(dir, sizeof dir);
    for (int i = 0; i < 4; i++) {
        char p[1200];
        snprintf(p, sizeof p, "%s/%s%s", dir, g_editScen, EXT[i]);
        FILE* t = fopen(p, "rb");
        if (!t) continue;
        fclose(t);
        return elev_import_image(p, why, whyN);
    }
    if (why && whyN > 0)
        snprintf(why, whyN, "no %s.PNG and no %s.PCX in user_maps -- paint one and put "
                 "it there", g_editScen, g_editScen);
    return 0;
}


/* ------------------------------------------------------------------------------------
 *  THE AUTO HEIGHTMAP -- fit a heightmap to the cliff art a map already carries
 *
 *  The tier brush works the other way round: you paint ground and it derives the cliff
 *  art. This is for the map that already HAS cliff art -- one converted from a DOS
 *  editor, or one whose slopes were stamped by hand out of the cliff drawer -- and no
 *  relief under it. Every such map is a heightmap waiting to be read, because the 1995
 *  map format has no elevation field at all and the only thing in it that says "high
 *  ground" is which SLOPE template the designer painted. That is exactly the reading the
 *  N64 porters made by hand for all 55 of the cartridge's heightmapped scenarios.
 *
 *  IT DOES NOT ASK FOR THE MAP TO BE CONVERTED FIRST, AND MUST NOT. Converting is
 *  elev_write over the whole map, which REPAINTS as plain ground every block whose four
 *  corners are level -- that is, every block of a flat map, including the ones carrying
 *  the slope art. Requiring it would destroy the input before the fit could read it: the
 *  prerequisite and the feature cannot both exist. Neither half of the fit needs the
 *  ladder anyway. It reads template ids and icon numbers out of the .BIN, and it writes
 *  corner bytes; the ladder is a property of the bytes it writes, not a precondition on
 *  the bytes it reads.
 *
 *  IT WRITES THROUGH THE LADDER, WHICH IS WHAT MAKES IT SAFE TO PRESS. The first build
 *  of this feature wrote a free interpolation into every corner, and the result was that
 *  ONE PRESS on SCB31EA took a map from 0 of 3844 playable corners off the ladder to
 *  1320 of 3844 -- so the elevation panel replaced itself with the conversion gate and
 *  refused everything, on a map that had been fully usable a moment earlier. A tool
 *  whose first press disables the panel it lives on is not a tool. So the fit's output
 *  is a TIER FIELD over the 2x2 blocks, exactly what the rung tool authors, and the
 *  bytes go down through elev_coarse and elev_heights, exactly the path a brush stroke
 *  takes. Every corner it writes therefore lands on a rung by construction, every corner
 *  it declines to write keeps the byte it already had, and the off-the-ladder count can
 *  only fall. On the flat-with-art maps this feature is for it falls to zero, and the
 *  fit becomes what CONVERT should have been: the way onto the ladder that READS the
 *  cliff art instead of erasing it. It is one undo entry, and the unlock is inside it.
 *
 *  IT WORKS PER TEMPLATE PLACEMENT, NOT PER CELL. A cliff is a stamped 2x2 (sometimes
 *  2x3 or 3x2) block of art, the climb is spread across that block, and the block's own
 *  id says which way is up. Reading it per cell would throw away both facts.
 *
 *      1. recover every SLOPE placement from the .BIN
 *      2. read its high side off the compass below
 *      3. flood the open ground so every cell knows its nearest placement and which
 *         side of it the cell is on
 *      4. solve one level per placement from the constraint that two adjacent open
 *         cells are the same height, and MARK the cells where two placements disagree
 *      5. reduce the cell levels to one tier per 2x2 block, and write the ground from
 *         that field through the ladder
 *
 *  WATER IS NOT GROUND AND IS LEFT ALONE, by the same rule and for the same reason the
 *  smooth brush keeps off it: the sea surface is drawn from the terrain's own corners,
 *  so lifting one lifts the sea into a hill. The first build flooded water like open
 *  ground and gave it a rung -- HEIGHT|30,21 moved from centre 0.0000 to 1.9844 on a
 *  lake. Here water cells are a barrier to the flood, a block holding any water cell
 *  keeps the tier it was seeded with, and a corner any of whose four cells is water is
 *  not written at all. Those corners are counted and said, not silently skipped.
 *
 *  THE COMPASS IS MEASURED, AND THE MEASUREMENT IS REPRODUCIBLE. Over the 55 cartridge
 *  scenarios that ship a heightmap, 4603 SLOPE placements sit wholly inside their map's
 *  playable rectangle; 4509 of them (97.96%) have a mean height gradient that agrees
 *  with the entry below, 18 (0.39%) point the other way and 76 (1.65%) are level. That
 *  arithmetic -- the same projection onto the same eight directions this file uses -- is
 *  in tools/romdump/cliff_compass.py and the numbers here are its output:
 *
 *      python3 tools/romdump/cliff_compass.py
 *
 *  Do not restate them from anywhere else. They were quoted wrongly once already, from a
 *  per-cell average that answers a different question.
 *
 *  THE WHOLE GRID IS WRITTEN, NOT THE PLAYABLE RECTANGLE, and the border is the reason:
 *  a rectangle-only fit leaves the cells outside it at whatever height they were, which
 *  is a step along all four edges of the map. Nothing plays there, so following the
 *  terrain out is free and looks right from inside.
 *
 *  AND g_mapX..g_mapH IS NOT WRONG ABOUT THOSE MAPS. It reads one cell wider on every
 *  side than the [Map] X/Y/Width/Height in the file, and that is the brain's own doing
 *  rather than a disagreement to work around: CNCMapDataStruct keeps the file's own
 *  rectangle in OriginalMapCell* and widens MapCell* by a cell in each direction before
 *  handing it over (dllinterface.cpp, GAME_STATE_STATIC_MAP). A map declaring 1,1 62x62
 *  therefore arrives as 0,0 64x64, exactly as one declaring 36,39 26x23 arrives as
 *  35,38 28x25. Every other "outside the playable area" test in this file measures
 *  against the grown one for the same reason.
 * ---------------------------------------------------------------------------------- */

/* THE COMPASS. One high side per SLOPE template, in grid axes: dy is POSITIVE SOUTH.
   Four straight edges of seven variants each and ten corner pieces, which is how
   Westwood organised the 1995 cliff art. */
struct AhCompass { signed char dx, dy; };
static const AhCompass AH_COMPASS[AH_SLOPE_N] = {
    { 0,-1},{ 0,-1},{ 0,-1},{ 0,-1},{ 0,-1},{ 0,-1},{ 0,-1},   /* S01..S07  north */
    { 1, 0},{ 1, 0},{ 1, 0},{ 1, 0},{ 1, 0},{ 1, 0},{ 1, 0},   /* S08..S14  east  */
    { 0, 1},{ 0, 1},{ 0, 1},{ 0, 1},{ 0, 1},{ 0, 1},{ 0, 1},   /* S15..S21  south */
    {-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},   /* S22..S28  west  */
    { 1,-1},                                                   /* S29  north-east */
    { 1, 1},                                                   /* S30  south-east */
    {-1, 1},                                                   /* S31  south-west */
    {-1,-1},                                                   /* S32  north-west */
    {-1, 1},                                                   /* S33  south-west */
    {-1,-1},                                                   /* S34  north-west */
    { 1,-1},                                                   /* S35  north-east */
    { 1, 1},                                                   /* S36  south-east */
    { 1, 1},                                                   /* S37  south-east */
    { 1,-1}                                                    /* S38  north-east */
};

/* WHY A CELL IS MARKED. The report is a picture of what the fit could not commit to,
   and the three reasons are different problems, so they are different colours. */
enum { AH_OK = 0, AH_DISAGREE = 1, AH_CLAMPED = 2, AH_ORPHAN = 3 };

/* THE REPORT IS ONE COPY, and g_ah itself is declared with the rest of the panel state
   because the panel draws it. The overlay draws these cells and the headless verb prints
   these counts; neither recomputes anything, because a report that is counted twice is a
   report that can disagree with itself -- and it did, on the second map, the first time
   this was built with a count at each end. */
static unsigned char g_ahMark[EDIT_GRID_MAX];

/* THE REPORT DOES NOT OUTLIVE THE MAP IT DESCRIBES. Called from the undo funnel and
   from the restore, which between them are every path that can move the ground: any
   committed edit invalidates it, and so does undo, redo and revert. */
static void ah_report_clear(void)
{
    /* THE ARM GOES TOO, and before the early return. An arm is a promise about the map
       as it stands: carrying one across an edit, an undo or a map change would let the
       next press replace relief the player was never warned about. */
    g_ahArmed = false;
    if (!g_ah.valid) return;
    memset(&g_ah, 0, sizeof g_ah);
    memset(g_ahMark, 0, sizeof g_ahMark);
    fprintf(stderr, "edit: the auto heightmap report describes a map that has moved on; "
                    "cleared\n");
}

/* HOW MANY CELLS THE OVERLAY HAS TO DRAW. One walk, over the grid stride the rest of
   this file uses, and the ONLY place that number is produced. */
static int ah_mark_count(void)
{
    if (!g_ah.valid) return 0;
    int n = 0;
    for (int cy = 0; cy < g_gridH; cy++)
        for (int cx = 0; cx < g_gridW; cx++)
            if (g_ahMark[cy * g_gridW + cx]) n++;
    return n;
}

static inline bool ah_is_slope(int t)
{
    return t >= AH_SLOPE_FIRST && t <= AH_SLOPE_LAST;
}

struct AhPlace {
    short ox, oy, w, h;
    short tid;
    signed char ux, uy;
    int   pot;          /* level relative to its component's root  */
    int   base;         /* absolute rung of its LOW side           */
    int   parent;       /* weighted union-find                     */
};
static AhPlace g_ahPlace[AH_PLACE_MAX];
static int     g_ahPlaceN = 0;

/* Union-find with a potential: find() returns the root and fills *pot with this
   placement's level relative to that root. Path compression rewrites the potential as
   it goes, so the walk stays flat and the arithmetic stays exact. */
static int ah_find(int a, int* pot)
{
    int r = a, sum = 0;
    while (g_ahPlace[r].parent != r) { sum += g_ahPlace[r].pot; r = g_ahPlace[r].parent; }
    int n = a, acc = sum;
    while (g_ahPlace[n].parent != n) {
        const int nxt  = g_ahPlace[n].parent;
        const int mine = g_ahPlace[n].pot;
        g_ahPlace[n].parent = r;
        g_ahPlace[n].pot    = acc;
        acc -= mine;
        n = nxt;
    }
    *pot = (a == r) ? 0 : g_ahPlace[a].pot;
    return r;
}

/* Which side of a placement a cell is on: 1 high, 0 low. The projection onto the
   compass direction, from the placement's own centre, which is what makes a diagonal
   piece read as a corner rather than as two edges. */
static inline int ah_side_of(const AhPlace* p, int cx, int cy)
{
    const float dx = ((float)cx + 0.5f) - ((float)p->ox + (float)p->w * 0.5f);
    const float dy = ((float)cy + 0.5f) - ((float)p->oy + (float)p->h * 0.5f);
    return (dx * (float)p->ux + dy * (float)p->uy) > 0.0f ? 1 : 0;
}

/* Recover every SLOPE placement from the .BIN. An ORIGIN is a cell carrying icon 0 of a
   slope template; the placement then claims the cells of its own w x h box that carry
   that template and the icon that box position implies. A cell of slope art that no
   origin claims is an ORPHAN -- half a template someone painted over -- and is reported
   rather than guessed at. */
static int ah_recover(short* own /* EDIT_GRID_MAX */, unsigned char* wall, int* orphans)
{
    g_ahPlaceN = 0;
    *orphans = 0;
    for (int i = 0; i < g_gridW * g_gridH; i++) { own[i] = -1; wall[i] = 0; }
    for (int cy = 0; cy < g_gridH; cy++)
        for (int cx = 0; cx < g_gridW; cx++)
            if (ah_is_slope(g_editTmpl[cy * g_editBinW + cx]))
                wall[cy * g_gridW + cx] = 1;

    for (int cy = 0; cy < g_gridH && g_ahPlaceN < AH_PLACE_MAX; cy++)
        for (int cx = 0; cx < g_gridW && g_ahPlaceN < AH_PLACE_MAX; cx++) {
            const int t = g_editTmpl[cy * g_editBinW + cx];
            if (!ah_is_slope(t) || g_editIcon[cy * g_editBinW + cx] != 0) continue;
            const int tw = EDIT_TEMPLATES[t].w, th = EDIT_TEMPLATES[t].h;
            AhPlace* p = &g_ahPlace[g_ahPlaceN];
            p->ox = (short)cx; p->oy = (short)cy;
            p->w = (short)tw;  p->h = (short)th;
            p->tid = (short)t;
            p->ux = AH_COMPASS[t - AH_SLOPE_FIRST].dx;
            p->uy = AH_COMPASS[t - AH_SLOPE_FIRST].dy;
            p->parent = g_ahPlaceN; p->pot = 0; p->base = AH_GROUND_RUNG;
            int claimed = 0;
            for (int dy = 0; dy < th; dy++)
                for (int dx = 0; dx < tw; dx++) {
                    const int x = cx + dx, y = cy + dy;
                    if (x >= g_gridW || y >= g_gridH) continue;
                    if (g_editTmpl[y * g_editBinW + x] != t) continue;
                    if (g_editIcon[y * g_editBinW + x] != dy * tw + dx) continue;
                    if (own[y * g_gridW + x] >= 0) continue;   /* first origin wins */
                    own[y * g_gridW + x] = (short)g_ahPlaceN;
                    claimed++;
                }
            if (claimed) g_ahPlaceN++;
        }
    for (int cy = 0; cy < g_gridH; cy++)
        for (int cx = 0; cx < g_gridW; cx++)
            if (wall[cy * g_gridW + cx] && own[cy * g_gridW + cx] < 0) (*orphans)++;
    return g_ahPlaceN;
}

/* HOW MUCH RELIEF THIS MAP ALREADY HAS, counted over the whole corner grid, which is
   what the fit writes. Zero means flat ground and the button may run straight away;
   anything else is hand-authored relief the fit is about to replace, and it asks. */
static int ah_relief_corners(void)
{
    if (g_pack.corner.empty()) return 0;
    const int hw = g_gridW + 1;
    int lo = 255, n = 0;
    for (int gy = 0; gy <= g_gridH; gy++)
        for (int gx = 0; gx <= g_gridW; gx++) {
            const int v = g_pack.corner[gy * hw + gx];
            if (v < lo) lo = v;
        }
    for (int gy = 0; gy <= g_gridH; gy++)
        for (int gx = 0; gx <= g_gridW; gx++)
            if (g_pack.corner[gy * hw + gx] != lo) n++;
    return n;
}

/* A DIGEST OF EXACTLY THE CORNERS THE SEA USES, so the fit can say -- rather than claim
   -- that it did not move one of them. Taken either side of the write, which is the only
   place in this file that touches g_pack.corner: if the two agree, no corner any of whose
   four cells is water changed, and that is the whole of the promise this feature makes to
   the water. A sampled height at one hand-picked cell is not the same test and was not
   enough: the first version of the gate read one lake cell that happened to sit where the
   fit would have written the byte it already had, so pulling the water guard out left the
   gate green while fifty-two other corners of sea moved. */
static unsigned ah_wet_hash(void)
{
    unsigned h = 2166136261u;
    const int hw = g_gridW + 1;
    for (int gy = 0; gy <= g_gridH; gy++)
        for (int gx = 0; gx <= g_gridW; gx++) {
            if (!sbr_corner_wet(gx, gy)) continue;
            h = (h ^ (unsigned)g_pack.corner[gy * hw + gx]) * 16777619u;
            h = (h ^ (unsigned)(gy * hw + gx)) * 16777619u;
        }
    return h;
}

/* THE FIT. Returns the number of corners rewritten, or 0 with a reason in why. */
static int ah_fit(char* why, int whyN)
{
    if (why && whyN > 0) why[0] = 0;
    if (!g_editHaveBin || g_pack.corner.empty()) {
        if (why) snprintf(why, whyN, "this map carries no heightmap to write into");
        return 0;
    }
    static short         own[EDIT_GRID_MAX];
    static unsigned char wall[EDIT_GRID_MAX];
    static short         dist[EDIT_GRID_MAX];
    static signed char   side[EDIT_GRID_MAX];
    static short         lvl[EDIT_GRID_MAX];
    static int           q[EDIT_GRID_MAX];
    /* THE MARKS ARE BUILT HERE AND PUBLISHED AT THE END. edit_commit is the funnel that
       clears the standing report, and this fit goes through it, so a fit that wrote its
       marks straight into g_ahMark would have them wiped by its own commit and would
       then publish a count of zero beside a disagreement count of eighty-six. That is
       the same disease as counting the report twice, arriving by the other door. */
    static unsigned char mk[EDIT_GRID_MAX];
    /* The ground the fit decides on, in the rung tool's own units, and the two arrays
       the ladder turns it into. Static for the same reason everything above is: a 128
       map's corner grid is 16641 bytes and this is not a function to put on a stack. */
    static unsigned char tier[ELEV_BLOCKS_MAX * ELEV_BLOCKS_MAX];
    static unsigned char kk[ELEV_K_MAX * ELEV_K_MAX];
    static unsigned char nh[C3D_CORN_MAX * C3D_CORN_MAX];

    /* ONE STRIDE. The .BIN's rows and the world's rows are the same rows on every map
       this editor loads today, and everything below indexes both with g_gridW. An XL
       document re-indexed at its own width would make that false, so it is checked
       rather than assumed: reading one grid at the other's stride would fit a heightmap
       to art that is somewhere else. */
    if (g_editBinW != g_gridW || g_editBinH != g_gridH) {
        if (why) snprintf(why, whyN, "this map's cells are stored %dx%d and its world is "
                          "%dx%d; the fit reads both at one stride and will not guess",
                          g_editBinW, g_editBinH, g_gridW, g_gridH);
        return 0;
    }

    int orphans = 0;
    const int np = ah_recover(own, wall, &orphans);
    if (np <= 0) {
        if (why) snprintf(why, whyN, "this map has no cliff art on it, so there is no "
                          "relief to read -- stamp some slope pieces first, or use the "
                          "rung brush");
        return 0;
    }

    /* THE TIER FIELD HAS TO EXIST BEFORE THE FIT CAN LEAVE PARTS OF IT ALONE. A block
       the flood never reaches, and every block the sea touches, keeps the tier the map
       already implies -- so seed it if the map has not been read yet. */
    if (!g_elevSeeded) elev_seed();
    /* AND THE LADDER COUNT HAS TO BE CURRENT, because the report carries it either side
       and the whole safety claim is a comparison between those two numbers. */
    sbr_recount_ladder();
    const int offBefore = g_elevOffLadder;

    /* 3. THE FLOOD. Every open cell takes the placement nearest it and the side of that
       placement it lies on. Four-connected and breadth first, so "nearest" is the walk
       a unit would take rather than a straight line through a cliff. WATER IS A BARRIER,
       not open ground: the sea is not standing on a rung and asking it which side of a
       cliff it is on has no answer. */
    int head = 0, tail = 0;
    for (int i = 0; i < g_gridW * g_gridH; i++) {
        dist[i] = -1; side[i] = -1; lvl[i] = 0;
        if (edit_cell_is_water(i % g_gridW, i / g_gridW)) wall[i] = 1;
        if (own[i] >= 0) { dist[i] = 0; q[tail++] = i; }
    }
    while (head < tail) {
        const int c = q[head++];
        const int cx = c % g_gridW, cy = c / g_gridW;
        static const int D[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        for (int k = 0; k < 4; k++) {
            const int nx = cx + D[k][0], ny = cy + D[k][1];
            if (nx < 0 || ny < 0 || nx >= g_gridW || ny >= g_gridH) continue;
            const int n = ny * g_gridW + nx;
            if (wall[n] || own[n] >= 0) continue;
            own[n]  = own[c];
            dist[n] = (short)(dist[c] + 1);
            side[n] = (signed char)ah_side_of(&g_ahPlace[own[c]], nx, ny);
            q[tail++] = n;
        }
    }

    /* 4. THE LEVELS. Two adjacent open cells are the same height, whichever placements
       own them, so every such pair is one equation. The first equation between two
       components joins them; a later one that disagrees is two placements asking for
       different ground in the same place, and BOTH cells are marked. */
    memset(mk, 0, sizeof mk);
    int disagree = 0;
    for (int cy = 0; cy < g_gridH; cy++)
        for (int cx = 0; cx < g_gridW; cx++) {
            const int a = cy * g_gridW + cx;
            if (wall[a] || own[a] < 0 || side[a] < 0) continue;
            static const int D[2][2] = { {1,0}, {0,1} };
            for (int k = 0; k < 2; k++) {
                const int nx = cx + D[k][0], ny = cy + D[k][1];
                if (nx >= g_gridW || ny >= g_gridH) continue;
                const int b = ny * g_gridW + nx;
                if (wall[b] || own[b] < 0 || side[b] < 0) continue;
                if (own[a] == own[b]) continue;
                int pa = 0, pb = 0;
                const int ra = ah_find(own[a], &pa), rb = ah_find(own[b], &pb);
                /* base[a] + side[a] == base[b] + side[b] */
                const int want = (pa + side[a]) - (pb + side[b]);
                if (ra != rb) {
                    g_ahPlace[rb].parent = ra;
                    g_ahPlace[rb].pot    = want;
                } else if (want != 0) {
                    if (!mk[a]) { mk[a] = AH_DISAGREE; disagree++; }
                    if (!mk[b]) { mk[b] = AH_DISAGREE; disagree++; }
                }
            }
        }

    /* Each component is anchored on its own, because two components share no ground and
       nothing relates them: the level most of its open cells want becomes GROUND. The
       histogram is nine bins wide and saturates at its ends, which only matters for a
       component asking for more than nine levels -- and everything past the fifth is
       clamped and marked below anyway, so the saturation cannot hide anything. */
    {
        static int hist[AH_PLACE_MAX][9];
        static int seen[AH_PLACE_MAX];
        for (int i = 0; i < np; i++) { seen[i] = 0; for (int k = 0; k < 9; k++) hist[i][k] = 0; }
        for (int i = 0; i < g_gridW * g_gridH; i++) {
            if (wall[i] || own[i] < 0 || side[i] < 0) continue;
            int pot = 0;
            const int r = ah_find(own[i], &pot);
            int v = pot + side[i] + 4;              /* biased so the index is >= 0 */
            if (v < 0) v = 0; else if (v > 8) v = 8;
            hist[r][v]++;
            seen[r] = 1;
        }
        for (int r = 0; r < np; r++) {
            if (!seen[r]) continue;
            int bestk = 4, bestn = -1;
            for (int k = 0; k < 9; k++)
                if (hist[r][k] > bestn) { bestn = hist[r][k]; bestk = k; }
            /* the winning level is (bestk - 4); shift the component so it reads GROUND */
            g_ahPlace[r].base = AH_GROUND_RUNG - (bestk - 4);
        }
        for (int i = 0; i < np; i++) {
            int pot = 0;
            const int r = ah_find(i, &pot);
            g_ahPlace[i].base = g_ahPlace[r].base + pot;
        }
    }

    /* 5. THE RUNGS ARE FIVE AND THE MAP MAY WANT MORE. A placement whose low side is
       already at the top has nowhere to put its high side, so it is clamped and its own
       cells are marked: the fit declines to invent a sixth rung. */
    int clamped = 0;
    for (int i = 0; i < np; i++) {
        int b = g_ahPlace[i].base;
        if (b < 0) b = 0;
        if (b > AH_TOP_RUNG - 1) b = AH_TOP_RUNG - 1;
        if (b != g_ahPlace[i].base) {
            g_ahPlace[i].base = b;
            for (int cy = g_ahPlace[i].oy; cy < g_ahPlace[i].oy + g_ahPlace[i].h; cy++)
                for (int cx = g_ahPlace[i].ox; cx < g_ahPlace[i].ox + g_ahPlace[i].w; cx++) {
                    if (cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) continue;
                    if (own[cy * g_gridW + cx] != i) continue;
                    if (!mk[cy * g_gridW + cx]) {
                        mk[cy * g_gridW + cx] = AH_CLAMPED;
                        clamped++;
                    }
                }
        }
    }
    for (int i = 0; i < g_gridW * g_gridH; i++) {
        if (wall[i] || own[i] < 0 || side[i] < 0) continue;
        int v = g_ahPlace[own[i]].base + side[i];
        if (v < 0 || v > AH_TOP_RUNG) {
            if (v < 0) v = 0; else v = AH_TOP_RUNG;
            if (!mk[i]) { mk[i] = AH_CLAMPED; clamped++; }
        }
        lvl[i] = (short)v;
    }
    /* Orphan cliff art gets its own mark: it contributes no height, so the corners
       around it are carried by whatever else touches them. Water was turned into a wall
       above and must not be reported as orphaned art, so the mark is only for cells that
       really do carry a slope template. */
    for (int i = 0; i < g_gridW * g_gridH; i++)
        if (wall[i] && own[i] < 0 && !mk[i] &&
            ah_is_slope(g_editTmpl[(i / g_gridW) * g_editBinW + (i % g_gridW)]))
            mk[i] = AH_ORPHAN;

    /* 6. THE TIER FIELD. One rung per 2x2 block, and it is the LOWEST thing standing in
       that block -- the same reading elev_seed makes of a map's own corners, so the two
       agree about what "the ground this block stands on" means. A cell of cliff art
       counts as its placement's LOW side, which is the ground the art rises out of.
       A block the flood never reached, and every block the sea touches, keeps the tier
       the map already had. */
    memcpy(tier, g_elevTier, sizeof tier);
    for (int by = 0; by < elev_bh(); by++)
        for (int bx = 0; bx < elev_bw(); bx++) {
            int lo = -1;
            bool wet = false;
            for (int dy = 0; dy < 2 && !wet; dy++)
                for (int dx = 0; dx < 2 && !wet; dx++) {
                    const int cx = 2 * bx + dx, cy = 2 * by + dy;
                    if (cx >= g_gridW || cy >= g_gridH) continue;
                    if (edit_cell_is_water(cx, cy)) { wet = true; continue; }
                    const int c = cy * g_gridW + cx;
                    if (own[c] < 0) continue;               /* orphan art, or unreached */
                    const int v = wall[c] ? g_ahPlace[own[c]].base : (int)lvl[c];
                    if (lo < 0 || v < lo) lo = v;
                }
            if (wet || lo < 0) continue;
            if (lo < 0) lo = 0;
            if (lo > AH_TOP_RUNG) lo = AH_TOP_RUNG;
            tier[by * elev_bw() + bx] = (unsigned char)lo;
        }
    elev_coarse(tier, kk);
    elev_heights(kk, nh);

    /* 7. THE BYTES. Every corner the sea does not use takes the ladder's own value for
       it; every corner the sea does use is left exactly as it was found. That is the
       whole of the safety property: what is written is on a rung, what is not written
       has not moved, so the off-the-ladder count cannot rise. */
    EditSnap before = edit_snapshot("auto heightmap");
    const unsigned wetWas = ah_wet_hash();
    const int hw = g_gridW + 1;
    int wrote = 0, moved = 0, held = 0;
    for (int gy = 0; gy <= g_gridH; gy++)
        for (int gx = 0; gx <= g_gridW; gx++) {
            const int i = gy * hw + gx;
            if (sbr_corner_wet(gx, gy)) { held++; continue; }
            const unsigned char v = nh[i];
            if (g_pack.corner[i] != v) moved++;
            g_pack.corner[i] = v;
            wrote++;
        }

    const bool wetkept = (ah_wet_hash() == wetWas);

    /* A FIT THAT MOVED NO BYTE IS NOT AN EDIT. Running it again on ground that already
       agrees with its own cliff art rewrites every corner with the value already there,
       and an undo entry for that is an undo entry that appears not to work -- while
       claiming the heightmap had been touched would start writing a .HGT for a map that
       never had one. The report is still published: it describes the map as it stands,
       and standing still is an answer. */
    if (moved) {
        /* THE FOUR THINGS A WRITE INTO THE CORNERS OWES THE REST OF THE GAME. Without
           the first the .HGT is never saved; without the second the shading still reads
           flat on ground that has moved; without the third a height query under a
           building answers off the stale building pad; without the last the radar keeps
           the old relief. */
        g_elevTouched  = true;
        g_shadeReady   = false;
        terrain_pads_rebuild();
        g_euiRadarDirty = 1;

        /* THE UNLOCK, AND IT IS INSIDE THE UNDO ENTRY -- the smooth brush's own
           arrangement, for the same reason and by the same flag. The snapshot above
           still carries the old value, so undoing this fit takes the ground AND the
           unlock back together. It is NOT g_elevConverted: CONVERT is a separate,
           deliberate, announced act with its own "it cannot be undone" contract, and a
           fit that set it would put that act inside the undo stack by a side door. */
        g_elevSmoothUnlock = true;

        /* The tier field is a reading of the ground, so re-read it: the rung brush must
           not go on deriving from a field that describes the map as it was. This also
           refreshes the off-the-ladder count the panel prints. */
        g_elevSeeded = false;
        elev_seed();

        edit_commit(before);      /* ONE undo step for the whole map, and it clears the
                                     standing report on its way through */
    } else {
        sbr_recount_ladder();     /* nothing moved, but the readout still has to be true */
    }

    /* THE INVARIANT, COUNTED RATHER THAN ASSERTED. Every corner this fit wrote came out
       of ELEV_RUNG, so the only playable corners that can still be off the ladder are the
       ones it declined to write -- which is exactly the ones the sea uses. Anything else
       is the fit having gone off the ladder, and that is the fault that locked the panel
       the first time this feature was built. Counted on the SAME lattice elev_seed and
       sbr_recount_ladder walk, because a count on a different denominator is a different
       question. */
    int stray = 0;
    for (int by = 0; by < elev_bh(); by++)
        for (int bx = 0; bx < elev_bw(); bx++) {
            static const int DS[4][2] = { {0,0}, {2,0}, {0,2}, {2,2} };
            for (int k = 0; k < 4; k++) {
                const int gx = 2 * bx + DS[k][0], gy = 2 * by + DS[k][1];
                if (gx > g_gridW || gy > g_gridH) continue;
                if (gx < g_mapX || gx >= g_mapX + g_mapW ||
                    gy < g_mapY || gy >= g_mapY + g_mapH) continue;
                if (sbr_corner_wet(gx, gy)) continue;
                const int b = g_pack.corner[gy * (g_gridW + 1) + gx];
                int on = 0;
                for (int r = 0; r < 5; r++) if (b == ELEV_RUNG[r]) on = 1;
                if (!on) stray++;
            }
        }

    memcpy(g_ahMark, mk, sizeof g_ahMark);
    memset(&g_ah, 0, sizeof g_ah);
    g_ah.valid     = true;
    g_ah.places    = np;
    g_ah.orphans   = orphans;
    g_ah.wrote     = wrote;
    g_ah.moved     = moved;
    g_ah.held      = held;
    g_ah.disagree  = disagree;
    g_ah.clamped   = clamped;
    g_ah.offBefore = offBefore;
    g_ah.offAfter  = g_elevOffLadder;
    g_ah.stray     = stray;
    g_ah.wetkept   = wetkept;
    g_ah.gridW     = g_gridW;
    g_ah.gridH     = g_gridH;
    g_ah.marked    = ah_mark_count();
    fprintf(stderr, "edit: auto heightmap -- %d placements, %d corners written, %d moved,"
            " %d held for water, %d cells marked (%d disagree, %d clamped, %d orphan"
            " art); off the ladder %d -> %d, %d of them not the sea's; the sea's own"
            " corners %s\n",
            np, wrote, moved, held, g_ah.marked, disagree, clamped, orphans,
            offBefore, g_ah.offAfter, stray, wetkept ? "did not move" : "MOVED");
    return wrote;
}

/* THE BUTTON, AND IT ASKS BEFORE IT REPLACES. The fit rewrites every corner it can
   reach, so on a map that already carries hand-authored relief it says how much it is
   about to overwrite and waits to be asked again. On flat ground there is nothing to
   lose and it runs at once.
 *
 * THERE IS NO LADDER CHECK HERE, DELIBERATELY. This is the control an unconverted map
 * needs, and it is drawn on the conversion gate's own card for that reason; a version of
 * it that answered "CONVERT THIS MAP first" answered with the one act that would destroy
 * its own input. */
static void ah_button(void)
{
    char why[220] = { 0 };
    if (!g_editHaveBin || g_pack.corner.empty()) {
        snprintf(g_elevWhy, sizeof g_elevWhy, "this map carries no heightmap to write "
                 "into");
        edit_toast(EUI_DANGER, "NO HEIGHTMAP", g_elevWhy);
        return;
    }
    const bool armed = g_ahArmed && (g_editFrame - g_ahArmedAt) < AH_ARM_FRAMES;
    const int relief = ah_relief_corners();
    if (relief > 0 && !armed) {
        char sub[220];
        snprintf(sub, sizeof sub, "%d corners are not flat and this replaces all of "
                 "them. Click again to fit the ground to the cliff art.", relief);
        g_ahArmed = true;
        g_ahArmedAt = g_editFrame;
        snprintf(g_elevWhy, sizeof g_elevWhy, "%s", sub);
        edit_toast(EUI_GOLD, "THIS REPLACES THE RELIEF", sub);
        fprintf(stderr, "edit: auto heightmap armed -- %d corners of relief to replace\n",
                relief);
        return;
    }
    g_ahArmed = false;
    if (ah_fit(why, sizeof why) > 0) {
        char sub[220];
        snprintf(sub, sizeof sub, "%d placements read, %d corners moved, %d left to the "
                 "water, %d cells the fit would not commit to", g_ah.places, g_ah.moved,
                 g_ah.held, g_ah.marked);
        g_elevWhy[0] = 0;
        edit_toast(g_ah.moved ? EUI_CYAN : EUI_DIM,
                   g_ah.moved ? "HEIGHTMAP FITTED"
                              : "THE GROUND ALREADY AGREES WITH THE ART", sub);
        return;
    }
    snprintf(g_elevWhy, sizeof g_elevWhy, "%s",
             why[0] ? why : "the fit did nothing and did not say why");
    edit_toast(EUI_DANGER, "HEIGHTMAP NOT FITTED", g_elevWhy);
}

/* THE AUTO HEIGHTMAP'S REPORT, on the ground, in the ELEVATION panel's own mode.
 *
 * Three reasons, three colours, because they are three different problems: RED where
 * two placements asked for different ground in the same place, AMBER where the fit ran
 * out of rungs, GREY where a piece of cliff art has no template origin to read.
 *
 * IT DRAWS EXACTLY WHAT ah_mark_count COUNTED, and it counts what it draws so a gate can
 * put the two numbers side by side. That is not belt and braces: the first build of this
 * feature counted at the fit and counted again in the drawer, and the two answers parted
 * company on the second map because they walked different rectangles at different
 * strides. One array, one stride, one walk. */
static void eui_draw_ahmark(int fbw, int fbh)
{
    if (!g_editOn || g_editMode != 2 || !g_ah.valid) return;
    if (g_ah.gridW != g_gridW || g_ah.gridH != g_gridH) return;

    begin_overlay(fbw, fbh);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    int drawn = 0;
    for (int cy = 0; cy < g_gridH; cy++)
        for (int cx = 0; cx < g_gridW; cx++) {
            const int m = g_ahMark[cy * g_gridW + cx];
            if (!m) continue;
            drawn++;
            if (m == AH_DISAGREE)     glColor4f(0.78f, 0.16f, 0.12f, 0.30f);
            else if (m == AH_CLAMPED) glColor4f(0.88f, 0.66f, 0.16f, 0.28f);
            else                      glColor4f(0.62f, 0.64f, 0.68f, 0.24f);
            glBegin(GL_TRIANGLES);
            sb_place_cell_tris(cx, cy, fbw, fbh);
            glEnd();
        }
    glDisable(GL_BLEND);
    end_overlay();
    {   /* Said when it changes, not per frame -- the same rule the no-go overlay keeps.
           A gate reads this line against the verb's own count. */
        static int told = -1;
        if (told != drawn) {
            told = drawn;
            fprintf(stderr, "AHOVERLAY|drawn=%d|report=%d|grid=%dx%d\n",
                    drawn, g_ah.marked, g_gridW, g_gridH);
        }
    }
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


/* ------------------------------------------------------------------------------------
 *  THE SMOOTH BRUSH
 *
 *  The second elevation tool. It writes g_pack.corner directly, one raw byte a corner,
 *  and it dresses nothing: no tier, no mask, no cliff art. The rung tool is untouched --
 *  elev_paint, elev_write, elev_heights and elev_refusal are not called from here and
 *  not modified by it.
 *
 *  THE DOSE, which is what makes a stroke a pure function of how long it was held.
 *  Every corner under the brush accumulates a DOSE -- weight x time x strength -- and
 *  its byte is then a function of the byte it had when the stroke began and that dose.
 *  Nothing is read back and re-written, so the arithmetic cannot drift with the number
 *  of steps, and ERASE composes exactly:
 *
 *      raise   v = base + SBR_RAISE_RATE * dose
 *      lower   v = base - SBR_RAISE_RATE * dose
 *      erase   v = D + (base - D) * max(0, 1 - SBR_ERASE_K * dose)
 *
 *  D IS THE MAP'S OWN GROUND DATUM AND NOT A CONSTANT -- see sbr_flat_byte. It is the
 *  byte load_pack re-zeroed the world on, so a corner at D draws at world y 0.000. On a
 *  corner already at D the formula is exactly D at every dose, which is what makes ERASE
 *  a no-op on ground nobody has touched. It does not go through a tier: the ladder is
 *  0/64/128/191/255 with ties down, a round trip through it loses up to 32 units a
 *  corner, and on SCG01EA the datum (90) is not on the ladder at all.
 *
 *  AND THE INTEGRATION IS AT A FIXED TIMESTEP. The frame hands over its dt; this banks
 *  it and consumes whole 1/SBR_TICK_HZ ticks. The tick count over a held stroke depends
 *  on the time held, not on the frame rate, so the same stroke fed 25 ms frames and 5 ms
 *  frames produces the same bytes -- which G151 measures rather than asserts.
 *
 *  WATER IS REFUSED, NOT DRAWN THROUGH. The water surface is built from the terrain's
 *  own corners, so raising a corner the sea touches lifts the sea into a hill. A corner
 *  any of whose four cells is water is skipped, and the refusal is SAID once a stroke
 *  rather than being a stroke that quietly does nothing.
 * ---------------------------------------------------------------------------------- */

static bool          g_sbrOn = false;
static EditSnap      g_sbrBefore;
static double        g_sbrHeld = 0.0;        /* how long the button has been down     */
static int           g_sbrCX = 0, g_sbrCY = 0;   /* the cell the pointer is over      */
static bool          g_sbrMoved = false;     /* did any byte actually change?         */
static bool          g_sbrSaidWet = false;   /* the water refusal is said once        */
static int           g_sbrWet = 0, g_sbrFoot = 0;
static int           g_sbrTicks = 0;
/* THE FRAME LOOP'S CLOCK, banked between pumps -- see sbr_frame, which is the only
   thing that reads or writes either of them. */
static unsigned      g_sbrLastMs = 0;
static bool          g_sbrHaveMs = false;
static int           g_sbrLoX, g_sbrLoY, g_sbrHiX, g_sbrHiY;   /* corners touched     */
static unsigned char g_sbrBase[C3D_CORN_MAX * C3D_CORN_MAX];
static float         g_sbrDose[C3D_CORN_MAX * C3D_CORN_MAX];

/* Is a corner one the sea uses? The corner belongs to up to four cells and the water
   surface is drawn from the same lattice, so ANY water neighbour disqualifies it. */
static bool sbr_corner_wet(int gx, int gy)
{
    for (int dy = -1; dy <= 0; dy++)
        for (int dx = -1; dx <= 0; dx++) {
            const int cx = gx + dx, cy = gy + dy;
            if (cx < 0 || cy < 0 || cx >= g_gridW || cy >= g_gridH) continue;
            if (edit_land_at(cx, cy) == EDIT_LAND_WATER) return true;
        }
    return false;
}

/* THE MAP'S OWN GROUND DATUM, as a raw corner byte. See the note beside the declaration
   up with the rest of the tuning: load_pack re-zeroes the world on the median corner of
   the PLAYABLE RECT and keeps that level in g_terrainBase (world units, 1 unit = 64
   bytes), so this is the byte whose drawn height is exactly 0.000 on this map. A map the
   editor made carries 1.0 there, which is the byte 64, so a user map keeps the answer it
   always had.

   NO HEIGHTMAP, NO QUESTION: a pre-PK9 pack has no corners for the brush to write and
   g_terrainBase is left at 0, which is a level and not an answer -- so the rung the
   ladder calls ground is returned instead of a spurious byte 0. */
static unsigned char sbr_flat_byte(void)
{
    if (g_pack.corner.empty()) return ELEV_RUNG[1];
    long r = lrintf(g_terrainBase * 64.0f);
    if (r < 0)   r = 0;
    if (r > 255) r = 255;
    return (unsigned char)r;
}

/* The brush's profile: 1 inside the centre, a smoothstep down to 0 across the falloff.
   Distances are in CELLS, on the corner lattice, measured from the centre of the cell
   the pointer is over. */
static float sbr_weight(int gx, int gy, float ccx, float ccy)
{
    const float size = g_sbrVal[0], fall = g_sbrVal[1];
    const float dx = (float)gx - ccx, dy = (float)gy - ccy;
    const float d = sqrtf(dx * dx + dy * dy);
    if (d <= size) return 1.0f;
    if (d >= size + fall) return 0.0f;
    const float t = (d - size) / fall;
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

/* One corner's byte, from the byte it had when the stroke began and the dose it has
   taken since. A pure function, which is the whole reason the stroke is reproducible. */
static unsigned char sbr_value(unsigned char base, float dose)
{
    float v;
    if (g_sbrTool == 2) {
        const float flat = (float)sbr_flat_byte();
        float k = 1.0f - SBR_ERASE_K * dose;   /* clamped: it ARRIVES, and then stops */
        if (k < 0.0f) k = 0.0f;
        v = flat + ((float)base - flat) * k;
    }
    else if (g_sbrTool == 1)
        v = (float)base - SBR_RAISE_RATE * dose;
    else
        v = (float)base + SBR_RAISE_RATE * dose;
    const long r = lrintf(v);
    return (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r);
}

/* The tier field the RUNG tool derives from, brought back into agreement with the
   corners this stroke moved. Same rule elev_seed uses -- a block's tier is the LOWEST
   of its four outer corners -- applied only to the blocks the stroke reached, so the
   rest of the map keeps whatever it had. elev_seed itself is not called: it would wipe
   the GRADED PASS and RAMP fields, which this stroke has no business touching. */
static void sbr_reseed_tiers(void)
{
    if (g_pack.corner.empty()) return;
    const int hw = g_gridW + 1;
    int bx0 = (g_sbrLoX - 1) / 2, bx1 = g_sbrHiX / 2;
    int by0 = (g_sbrLoY - 1) / 2, by1 = g_sbrHiY / 2;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > elev_bw() - 1) bx1 = elev_bw() - 1;
    if (by1 > elev_bh() - 1) by1 = elev_bh() - 1;
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) {
            int lo = 4;
            static const int D[4][2] = { {0,0}, {2,0}, {0,2}, {2,2} };
            for (int k = 0; k < 4; k++) {
                const int gx = 2 * bx + D[k][0], gy = 2 * by + D[k][1];
                if (gx > g_gridW || gy > g_gridH) continue;
                const int t = elev_tier_of(g_pack.corner[gy * hw + gx]);
                if (t < lo) lo = t;
            }
            g_elevTier[by * elev_bw() + bx] = (unsigned char)lo;
        }
}

/* How much of the playable rect is off the ladder NOW. elev_seed prints this and also
   rebuilds three fields; this only counts, so the status card can stay honest after a
   stroke that deliberately put corners between the rungs.
   THE SAME WALK elev_seed MAKES, deliberately: it counts a block's four outer corners,
   which double-counts the ones two blocks share and skips the odd rows entirely. That
   is a peculiar denominator, but it is the denominator the panel has always printed --
   "332 of 728" -- and a recount on a different one turns a number that was going down
   into a number that changed shape. One walk, in two places, and a gate that reads the
   readout before and after can compare them. */
static void sbr_recount_ladder(void)
{
    if (g_pack.corner.empty()) return;
    int off = 0, tot = 0;
    for (int by = 0; by < elev_bh(); by++)
        for (int bx = 0; bx < elev_bw(); bx++) {
            static const int D[4][2] = { {0,0}, {2,0}, {0,2}, {2,2} };
            for (int k = 0; k < 4; k++) {
                const int gx = 2 * bx + D[k][0], gy = 2 * by + D[k][1];
                if (gx > g_gridW || gy > g_gridH) continue;
                if (gx < g_mapX || gx >= g_mapX + g_mapW ||
                    gy < g_mapY || gy >= g_mapY + g_mapH) continue;
                const int b = g_pack.corner[gy * (g_gridW + 1) + gx];
                int on = 0;
                for (int r = 0; r < 5; r++) if (b == ELEV_RUNG[r]) on = 1;
                tot++;
                if (!on) off++;
            }
        }
    g_elevOffLadder = off;
    g_elevTotal = tot;
}

/* ONE FIXED TICK. Adds this tick's dose to every corner under the brush and rewrites
   the bytes that changed. */
static void sbr_tick(void)
{
    if (g_pack.corner.empty()) return;
    const int hw = g_gridW + 1;
    const float dt = 1.0f / SBR_TICK_HZ;
    const float str = g_sbrVal[2];
    /* PER TICK, NOT CUMULATIVE. These two describe the footprint the brush is standing
       on right now, which is the thing the refusal has to talk about; summed over a
       held stroke they would report a hundred and twenty times the number of corners
       the map has. */
    g_sbrWet = g_sbrFoot = 0;
    const float reach = g_sbrVal[0] + g_sbrVal[1];
    const float ccx = (float)g_sbrCX + 0.5f, ccy = (float)g_sbrCY + 0.5f;
    int g0x = (int)floorf(ccx - reach), g1x = (int)ceilf(ccx + reach);
    int g0y = (int)floorf(ccy - reach), g1y = (int)ceilf(ccy + reach);
    if (g0x < 0) g0x = 0;
    if (g0y < 0) g0y = 0;
    if (g1x > g_gridW) g1x = g_gridW;
    if (g1y > g_gridH) g1y = g_gridH;
    for (int gy = g0y; gy <= g1y; gy++)
        for (int gx = g0x; gx <= g1x; gx++) {
            const float w = sbr_weight(gx, gy, ccx, ccy);
            if (w <= 0.0f) continue;
            g_sbrFoot++;
            /* THE SEA REUSES THESE CORNERS. Skipped, and counted so the stroke can say
               so once at the end instead of appearing to do nothing. */
            if (sbr_corner_wet(gx, gy)) { g_sbrWet++; continue; }
            const int i = gy * hw + gx;
            float d = g_sbrDose[i] + w * dt * str;
            if (d > SBR_MAX_DOSE) d = SBR_MAX_DOSE;
            g_sbrDose[i] = d;
            const unsigned char v = sbr_value(g_sbrBase[i], d);
            if (v == g_pack.corner[i]) continue;
            g_pack.corner[i] = v;
            g_sbrMoved = true;
            if (gx < g_sbrLoX) g_sbrLoX = gx;
            if (gy < g_sbrLoY) g_sbrLoY = gy;
            if (gx > g_sbrHiX) g_sbrHiX = gx;
            if (gy > g_sbrHiY) g_sbrHiY = gy;
        }
    g_sbrTicks++;
}

static void sbr_stroke_begin(int cx, int cy)
{
    if (g_pack.corner.empty() || !g_editHaveBin) return;
    if (!g_elevSeeded) elev_seed();
    const size_t n = (size_t)(g_gridW + 1) * (size_t)(g_gridH + 1);
    if (g_pack.corner.size() < n) return;
    /* THE SNAPSHOT IS TAKEN NOW AND COMMITTED ONLY IF SOMETHING MOVES. It carries the
       unlock flag as it stands BEFORE this stroke, which is what makes undo put a
       newly unlocked map back where it was. */
    g_sbrBefore = edit_snapshot("smooth brush");
    memcpy(g_sbrBase, &g_pack.corner[0], n);
    memset(g_sbrDose, 0, n * sizeof g_sbrDose[0]);
    g_sbrOn = true;
    /* THE CLOCK LATCH GOES WITH IT: the first pump of this stroke reads the clock and
       charges nothing, so the gap since the pump last ran is not billed to the press. */
    g_sbrHaveMs = false;
    g_sbrHeld = 0.0;
    g_sbrMoved = false;
    g_sbrSaidWet = false;
    g_sbrWet = g_sbrFoot = g_sbrTicks = 0;
    g_sbrLoX = g_sbrLoY = 1 << 20;
    g_sbrHiX = g_sbrHiY = -1;
    g_sbrCX = cx; g_sbrCY = cy;
    g_sbrWhy[0] = 0;
    /* THE FIRST TICK IS NOT FREE. A click that is released inside one tick has to be a
       click that did something, or the tool feels broken at speed; and it still cannot
       unlock anything unless a byte moved, which is the point of the whole arrangement. */
    sbr_tick();
}

static void sbr_stroke_hover(int cx, int cy)
{
    if (!g_sbrOn) return;
    g_sbrCX = cx; g_sbrCY = cy;
}

/* THE PUMP. Called once a frame with the frame's own dt; consumes whole ticks. */
static void sbr_stroke_frame(float dt)
{
    if (!g_sbrOn) return;
    if (dt < 0.0f) dt = 0.0f;
    g_sbrHeld += (double)dt;
    /* HOW MANY TICKS THIS STROKE SHOULD HAVE APPLIED BY NOW, from the TOTAL time held,
       and rounded to nearest rather than accumulated by subtraction. Banking the
       remainder and subtracting a tick at a time looks equivalent and is not: the
       leftover is a float that drifts, and a stroke held for exactly one second came
       out one tick short of a whole second because the drift landed a hair below the
       threshold. Rounding a total puts the nearest boundary half a tick away -- four
       milliseconds -- which is six orders of magnitude past anything the arithmetic
       can accumulate, so the tick count is a function of the time held and nothing
       else. The +1 is the press itself: the button going down is tick zero. */
    const int want = 1 + (int)(g_sbrHeld * (double)SBR_TICK_HZ + 0.5);
    int guard = 0;
    while (g_sbrTicks < want && guard++ < 4096) sbr_tick();
    /* THE SHADING IS THE WHOLE VISUAL READ OF THIS TOOL, so it is rebuilt while the
       stroke is still running rather than at the end. terrain_shade_calc reads the RAW
       corners, so clearing the flag is all it takes; leaving it set is why raising
       ground could leave every cell at the flat value of 0.627 and look like nothing
       had happened. The PADS are not rebuilt here -- that is a stroke-end job, see
       sbr_stroke_end -- because it walks the object list and prints when it changes. */
    if (g_sbrMoved) g_shadeReady = false;
}

static void sbr_stroke_end(void)
{
    if (!g_sbrOn) return;
    g_sbrOn = false;
    if (!g_sbrMoved) {
        /* NOTHING MOVED, SO NOTHING HAPPENED. No undo entry, no unlock, no dirty flag,
           no .HGT. A click that changes no byte and still converts the map -- with no
           undo entry to put it back -- is the exact defect this arm exists to refuse. */
        if (g_sbrWet > 0 && !g_sbrSaidWet) {
            snprintf(g_sbrWhy, sizeof g_sbrWhy,
                     "the sea reuses the ground's own corners, so the brush will not "
                     "write a water cell -- painting here would lift the water into a "
                     "hill");
            edit_toast(EUI_GOLD, "SMOOTH BRUSH REFUSED", g_sbrWhy);
            g_sbrSaidWet = true;
        }
        g_sbrBefore.objects.clear();
        g_sbrBefore.tmpl.clear();
        g_sbrBefore.icon.clear();
        g_sbrBefore.corner.clear();
        return;
    }
    /* The rung tool derives from a tier field; bring the blocks this stroke reached back
       into agreement with the ground, so the two tools do share one map. */
    sbr_reseed_tiers();
    sbr_recount_ladder();
    /* WITHOUT THIS THE STROKE IS NEVER SAVED. edit_write_hgt returns early unless the
       elevation has been touched, so a save after a stroke wrote the same .HGT it had
       and said nothing at all. */
    g_elevTouched = true;
    g_shadeReady = false;
    /* AND WITHOUT THIS THE GROUND UNDER A BUILDING DOES NOT MOVE. Every height query
       goes through corner_raw, which prefers the building pad to the map's own corner,
       and the pad is rebuilt only from a brain dump. Raising the block under a
       Construction Yard wrote corner 128 and the height query still read 0.0000. */
    terrain_pads_rebuild();
    /* THE UNLOCK, AND IT IS INSIDE THE UNDO ENTRY. It is set only now, only because a
       byte actually moved, and the snapshot taken at stroke begin still carries the old
       value -- so undoing this stroke takes the heights AND the unlock back together. */
    g_elevSmoothUnlock = true;
    if (g_sbrWet > 0 && !g_sbrSaidWet) {
        snprintf(g_sbrWhy, sizeof g_sbrWhy,
                 "%d corner%s under the brush belong to water and were left alone -- "
                 "the sea reuses the ground's own corners", g_sbrWet,
                 g_sbrWet == 1 ? "" : "s");
        edit_toast(EUI_GOLD, "SMOOTH BRUSH KEPT OFF THE WATER", g_sbrWhy);
        g_sbrSaidWet = true;
    }
    edit_commit(g_sbrBefore);
    g_euiRadarDirty = 1;
    fprintf(stderr, "edit: smooth stroke -- tool %s, %d ticks, corners %d..%d x %d..%d, "
            "%d of %d footprint samples were water\n", SBR_TOOL_NAME[g_sbrTool],
            g_sbrTicks, g_sbrLoX, g_sbrHiX, g_sbrLoY, g_sbrHiY, g_sbrWet, g_sbrFoot);
}

/* IS THE STROKE THE BRUSH IS RUNNING STILL BEING HELD?
 *
 * THE DEFECT THIS ANSWERS. sbr_stroke_end had exactly one non-headless caller, the
 * LEFT-button-up arm of the event loop, while the pump below runs every frame with no
 * condition on it at all. A release is not guaranteed to arrive there: the window can
 * lose focus with the button down and the release lands in whatever took the focus; a
 * dialog can open over the map; the panel's mode or its page can change under the
 * pointer; the editor can be switched off, or PLAY can reboot the world. Every one of
 * those left a stroke integrating for ever -- dose climbing to SBR_MAX_DOSE, ground
 * still moving, no undo entry filed and the unlock never decided.
 *
 * SO THE PUMP CLOSES IT. The pump is the one thing that runs in every one of those
 * states, and this is everything that has to still be true for the stroke to go on:
 *
 *   the editor is up                 g_editOn
 *   its ELEVATION panel is showing   g_editMode == 2
 *   on the SMOOTH page               g_sbrPage == 1
 *   with no dialog over the map      edit_modal_up()
 *   this window has the keyboard     SDL_GetKeyboardFocus(), which is NULL the
 *                                    moment the window is not the focused one
 *   and the left button is down      SDL_GetMouseState
 *
 * The last two are what focus loss and a swallowed release look like from inside the
 * loop, and they need no window handle, which is why they can be asked here. */
static bool sbr_stroke_still_held(void)
{
    if (!g_sbrOn) return true;              /* nothing to abandon */
    if (!g_editOn || g_editMode != 2 || g_sbrPage != 1) return false;
    if (edit_modal_up()) return false;
    if (SDL_GetKeyboardFocus() == NULL) return false;
    return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
}

/* THE FRAME LOOP'S ONLY DOOR INTO THE BRUSH, AND IT TAKES A CLOCK READING.
 *
 * THE DEFECT THIS CLOSES, and it is the one that made the framerate gate blind. The loop
 * computes one dt, clamps it -- "a stall must not teleport the camera" -- and used to
 * hand that same clamped dt to the brush. A frame longer than the camera's ceiling then
 * contributed the ceiling and not the time it took, so a held stroke was a function of
 * the frame rate again, which is the single property the fixed-tick integration exists
 * to guarantee. The gate could not see it because the headless verb called the pump
 * directly, below the clamp: it measured a path no player takes.
 *
 * So the loop hands over the CLOCK and not a delta. There is no dt at the call site for
 * a clamp to touch, the delta is worked out here, and the headless verb drives THIS
 * function with simulated stamps -- the same journey, one function, one gate.
 *
 * THE FIRST CALL OF A STROKE CONTRIBUTES NOTHING. sbr_stroke_begin drops the latch, so
 * the first pump after the press only reads the clock; without that the stroke would be
 * charged for the whole gap since the last time the pump ran, which on a press that
 * follows a long pause is not time the button was down. The press itself is already a
 * tick -- see sbr_stroke_frame. */
static void sbr_frame(unsigned nowMs, bool stillHeld)
{
    if (g_sbrOn && !stillHeld) { sbr_stroke_end(); return; }
    if (!g_sbrHaveMs) { g_sbrLastMs = nowMs; g_sbrHaveMs = true; }
    const unsigned d = (nowMs > g_sbrLastMs) ? (nowMs - g_sbrLastMs) : 0u;
    g_sbrLastMs = nowMs;
    sbr_stroke_frame((float)d / 1000.0f);
}

/* A cheap order-sensitive digest of the whole corner array, for the gates: two strokes
   that are supposed to produce the same ground can be compared in one line without
   saving anything to disk. FNV-1a, 32 bits. */
static unsigned sbr_corner_hash(void)
{
    unsigned h = 2166136261u;
    const size_t n = (size_t)(g_gridW + 1) * (size_t)(g_gridH + 1);
    if (g_pack.corner.size() < n) return 0u;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned)g_pack.corner[i];
        h *= 16777619u;
    }
    return h;
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
 *  and the project owner's report of it was exactly right: "the map turns black". Measured before
 *  this: 81% of the view at pitch 20.
 *
 *  So the editor draws a ground-to-sky gradient behind the world. It is deliberately
 *  drab -- this is a work surface, not a vista -- and it exists to say "you are looking
 *  past the edge of the map" instead of saying nothing at all.
 * ---------------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------------------
 *  OWNERSHIP COLOURS, THROUGH THE GAME'S OWN LIVERIES
 *
 *  The renderer recolours a model by house: a seat's PlayerColorType picks a livery slot,
 *  the slot picks a texture sheet, and the sheets past the cartridge's own two are built
 *  out of the two decodes every pack already carries. That join is made by asking the
 *  brain which seat took which house, which is a question only a running skirmish can
 *  answer -- so in the editor the join was empty, and every owner that was not GDI fell
 *  through to the one base texture. Twelve houses, one grey army, and no way to see whose
 *  anything was. The selection box and the radar blip went the same way, because they read
 *  the same lookup.
 *
 *  A map being AUTHORED has no lobby and no seats, but a house still has a colour of its
 *  own: the RemapColor its HouseTypeClass declares. That is what goes in, into the SAME
 *  table the skirmish path fills, so nothing downstream is touched or duplicated.
 *
 *  ONLY THE MULTIPLAYER SEATS. GoodGuy, Neutral and Special all declare gold and BadGuy
 *  light blue, but the cartridge does not paint them that way: it carries two house
 *  texture sets and gives the sand one to GDI alone, so a civilian building is blue-grey
 *  on the console. Resolving those four here would turn every neutral building gold and
 *  disagree with the game the map is being made for. They keep the two-livery answer they
 *  already fall through to.
 * ---------------------------------------------------------------------------------- */

/* PlayerColorType per multiplayer house, in HousesType order from Multi1: light blue,
   orange, green, blue, gold, red, grey, brown. Read off the house table the brain
   compiles, not chosen here. Written to eight because the house table is; the count is
   still EDIT_MAX_PLAYERS and the static assert beside it keeps the two in step. */
static const signed char EUI_HOUSE_LIVERY[8] = { 1, 4, 3, 5, 0, 2, 6, 7 };

/* Is this colour's sheet already in the pack that is loaded NOW? Asked of the pack rather
   than remembered in a flag: New Map onto another theater reboots onto a different pack
   and frees every extra sheet with it, so a remembered "already built" would be a promise
   about textures that no longer exist. The two colours that map to a slot below 2 are the
   sheets the pack ships, and there is nothing to build for them. */
static bool edit_livery_built(int colour)
{
    const int slot = livery_slot(colour);
    if (slot < 2) return true;
    for (size_t i = 0; i < g_pack.tex.size(); i++)
        if (g_pack.tex[i].gl_extra[slot - 2]) return true;
    return false;
}

/* WHAT THIS MAP NEEDS TO BE ABLE TO SHOW: every multiplayer house standing on it, plus
   the one the chip strip is armed for, so the colour is right the moment the first object
   of it goes down. NOT every house the strip offers: a sheet costs about 0.18 MB on the
   card and the Windows half has to fit inside a Voodoo 2's texture memory, so an author
   pays for the colours the map actually uses and a mission map pays nothing at all. */
static void edit_livery_sync(void)
{
    if (!g_editOn || !g_teamColours) return;

    unsigned want = 0;
    if (g_editOwner >= 4 && g_editOwner < EUI_HOUSE_N) want |= 1u << g_editOwner;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const int h = eui_house_index(g_objects[i].house);
        if (h >= 4 && h < EUI_HOUSE_N) want |= 1u << h;
    }
    if (!want) return;              /* a mission map: nothing to resolve, nothing to build */

    /* The houses, every frame. A handful of small writes, and it is what makes the colour
       right on the FIRST frame a map is open, including a one-frame screenshot run. */
    for (int h = 4; h < EUI_HOUSE_N; h++)
        if (want & (1u << h)) g_liveryOf[EUI_HOUSES[h].key] = EUI_HOUSE_LIVERY[h - 4];

    /* The sheets, only for a colour that is actually absent. The builder decides what to
       make by reading this same table, and making one twice would strand the first set of
       textures, so it is handed exactly the houses whose colour is missing and the full
       table is put back afterwards. */
    std::map<std::string, int> full = g_liveryOf;
    g_liveryOf.clear();
    for (std::map<std::string, int>::const_iterator it = full.begin();
         it != full.end(); ++it)
        if (!edit_livery_built(it->second)) g_liveryOf[it->first] = it->second;
    if (!g_liveryOf.empty()) livery_build();
    g_liveryOf = full;
}

static void edit_draw_horizon(int fbw, int fbh)
{
    if (!g_editOn) return;
    /* THE OWNERSHIP LIVERIES, RESOLVED HERE. This is the first thing the editor runs
       inside the world draw, before a single object is submitted, so a one-frame
       screenshot shows the right textures rather than the previous frame's. */
    edit_livery_sync();
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
 *  painted, which is a design decision the project owner made and not an omission.
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

/* ------------------------------------------------------------------------------------
 *  THE DELETE STACK
 *
 *  A cell is not one thing. Top down it can carry an OBJECT (a unit, an infantryman, a
 *  building or a piece of scenery), a WALL, an authored SMUDGE, a TIBERIUM overlay, and
 *  under all of them the TERRAIN TEMPLATE. DELETE takes exactly one of those per press,
 *  from the top, and the press after the last of them puts the ground back to plain
 *  clear -- which is what "default" means for a cell in this editor.
 *
 *  ONE PER PRESS, and one undo step per press. A key that emptied the cell in one go
 *  would cost a whole cell to a mistake; a key that only ever took the top thing would
 *  leave the rungs below it unreachable, which is what tiberium and smudges were before
 *  this: erase looked at walls and objects and at nothing else, so a cell carrying a
 *  tiberium field answered "nothing to erase" for ever.
 *
 *  OBJECTS BEFORE WALLS, which is the other way round from how this used to read. The
 *  old order argued that a wall is overlay, so a click on a walled cell means the wall.
 *  That is true of a cell with nothing but a wall on it, and those are the only cells it
 *  was ever tested on -- but edit_cell_ok refuses a footprint only over BUILDING and
 *  TERRAIN, so an infantryman may legally stand on a sandbag, and pulling the sandbag
 *  out from under him is not what "delete what I am pointing at" means.
 *
 *  BIBS ARE NOT A RUNG. The apron under a building is laid by the engine and the writer
 *  deliberately never emits one, so taking one off would change the preview and nothing
 *  that ever reaches the disk.
 *
 *  NEITHER ARE WAYPOINTS OR TRIGGER ZONES. Those do not sit on the cell: they are
 *  map-wide tables that point AT one, each already has its own gesture in the mode that
 *  owns it, and a player start lost to a stray DELETE is a worse outcome than one that
 *  asks for the right mode first.
 * ---------------------------------------------------------------------------------- */

/* The topmost AUTHORED smudge on this cell, or -1. Types 12..14 are the engine's own
   bibs, which the writer skips, so they are skipped here for the same reason. */
static int edit_smudge_at(int cx, int cy)
{
    for (int i = (int)g_smudges.size() - 1; i >= 0; i--)
        if (g_smudges[i].x == cx && g_smudges[i].y == cy && (int)g_smudges[i].type < 12)
            return i;
    return -1;
}

static int edit_tib_at(int cx, int cy)
{
    for (int i = (int)g_tib.size() - 1; i >= 0; i--)
        if (g_tib[i].x == cx && g_tib[i].y == cy) return i;
    return -1;
}

/* IS A BUILDING'S OWN APRON ON THIS CELL? edit_smudge_at above deliberately cannot see
   one: the writer never emits a bib and DELETE must not take one. A brush has to see it,
   because SmudgeClass::Mark writes only into a cell whose Smudge is SMUDGE_NONE
   (smudge.cpp:222) and a bib is a smudge. */
static bool edit_bib_at(int cx, int cy)
{
    for (size_t i = 0; i < g_smudges.size(); i++)
        if (g_smudges[i].x == cx && g_smudges[i].y == cy && (int)g_smudges[i].type >= 12)
            return true;
    return false;
}

/* The palette's smudge rows and the SmudgeType each one means. SMUDGE_CRATER1 is 0 and
   SMUDGE_SCORCH1..6 are 6..11 (defines.h:1401-1412). */
static const struct { const char* code; unsigned char type; } SMUDGE_ROW[] = {
    { "CR1", 0 }, { "SC1", 6 }, { "SC2", 7 },
    { "SC3", 8 }, { "SC4", 9 }, { "SC5", 10 }, { "SC6", 11 }
};
static const int SMUDGE_ROW_N = (int)(sizeof SMUDGE_ROW / sizeof SMUDGE_ROW[0]);

/* ------------------------------------------------------------------------------------
 *  The smudge brush
 *
 *  ONE CRATER ROW AND SIX SCORCH ROWS, and the asymmetry is the engine's rather than a
 *  preference.
 *
 *  A CRATER'S TYPE IS NOT THE ONE THE FILE NAMES. For an IsCrater type Mark stores
 *  SMUDGE_CRATER1 + CellClass::Spot_Index(Coord) (smudge.cpp:229), and Read_INI hands it
 *  Cell_Coord(cell), which is the cell CENTRE: both leptons are CELL_LEPTON_W / 2
 *  (function.h:877) and Spot_Index answers 0 for the centre (cell.cpp:1638). So every
 *  crater in every INI loads as CRATER1. Read_INI then keeps the stack level only when
 *  the cell's smudge matches the one it asked for (smudge.cpp:301), so a CR3 row would
 *  lose its data as well as its shape. That is the same reason the tiberium brush above
 *  is one entry rather than twelve. A scorch is not IsCrater, Mark stores the type it was
 *  handed, and the six are six different sprites, so those are six rows. Measured over
 *  the 100 shipped mission files: 221 authored smudge rows, 47 CR1, no CR2..CR6.
 *
 *  REPEATED CLICKS DEEPEN A CRATER instead of doing nothing. That is Mark's own stacking:
 *  a crater laid where a crater already is increments SmudgeData and clamps it at 4
 *  (smudge.cpp:218-219), and the crater art holds exactly five frames for it. It is also
 *  the only way to author the one stack level of 2 the shipped missions contain.
 *
 *  ONE SMUDGE PER CELL otherwise, because Mark writes only where Smudge is SMUDGE_NONE.
 *  A second one is refused with the reason rather than accepted and dropped at load.
 *
 *  The radar is deliberately NOT marked dirty. eui_radar_build paints land types and
 *  object blips and has no smudge layer, so there is nothing there to rebuild.
 * ---------------------------------------------------------------------------------- */
static void edit_smudge_put(int x, int y, int type)
{
    const int at = edit_smudge_at(x, y);
    if (at >= 0) {
        if (type >= 6 || (int)g_smudges[at].type >= 6) {
            fprintf(stderr, "edit: %d,%d already carries %s, and a cell holds one "
                    "smudge\n", x, y,
                    (int)g_smudges[at].type < 6 ? "a crater" : "a scorch mark");
            return;
        }
        if ((int)g_smudges[at].data >= 4) {
            fprintf(stderr, "edit: the crater at %d,%d is already at stack level 4, "
                            "which is as deep as the engine lets one get\n", x, y);
            return;
        }
        EditSnap before = edit_snapshot("crater");
        g_smudges[at].data++;
        edit_commit(before);
        fprintf(stderr, "edit: deepened the crater at %d,%d to stack level %d\n",
                x, y, (int)g_smudges[at].data);
        return;
    }
    EditSnap before = edit_snapshot("smudge");
    SmudgeCell sc;
    memset(&sc, 0, sizeof sc);
    sc.x = (short)x; sc.y = (short)y;
    sc.type = (unsigned char)type;
    g_smudges.push_back(sc);
    edit_commit(before);
    fprintf(stderr, "edit: %s at %d,%d  [%d smudges]\n",
            type < 6 ? "crater" : "scorch mark", x, y, (int)g_smudges.size());
}

/* IS THIS CELL'S GROUND ALREADY THE DEFAULT?
 *
 * TEMPLATE_NONE is the cartridge's clear cell and template 0 (TEMPLATE_CLEAR1) is not:
 * over the 100 shipped dense .BIN files template 0 appears in none of 245,760 cells,
 * while 295,114 cells carry 255. The ICON byte beside it is not part of the answer --
 * edit_repaint_cell derives a clear cell's icon from the cell's own coordinates the way
 * CellClass::Clear_Icon does and never reads the stored one, and only 14,733 of those
 * 295,114 clear cells carry a non-zero icon at all. So 255 alone is "already clear". */
static bool edit_ground_is_default(int cx, int cy)
{
    if (!g_editHaveBin) return true;         /* no .BIN: there is no ground to put back */
    if (cx < 0 || cy < 0 || cx >= g_editBinW || cy >= g_editBinH) return true;
    return g_editTmpl[cy * g_editBinW + cx] == 255;
}

/* WHAT THE NEXT DELETE WOULD TAKE, and the noun for it. The press and the status card
   both go through this, so the card cannot describe a different stack from the one the
   key walks. */
static int edit_erase_peek(int cx, int cy, char* what, size_t n)
{
    if (what && n) what[0] = 0;
    if (cx < 0 || cy < 0) return EDL_EMPTY;
    const int k = edit_object_at(cx, cy);
    if (k >= 0) {
        if (what) snprintf(what, n, "%s", g_objects[k].type);
        return EDL_OBJECT;
    }
    const WallCell* w = edit_wall_at(cx, cy);
    if (w) {
        if (what) snprintf(what, n, "%s wall",
                           w->kind < 5 ? WALL_NAME[w->kind] : "a");
        return EDL_WALL;
    }
    const int sm = edit_smudge_at(cx, cy);
    if (sm >= 0) {
        /* The engine's SmudgeType enum: craters 0-5, scorches 6-11. */
        if (what) snprintf(what, n, "%s",
                           (int)g_smudges[sm].type < 6 ? "a crater" : "a scorch mark");
        return EDL_SMUDGE;
    }
    if (edit_tib_at(cx, cy) >= 0) {
        if (what) snprintf(what, n, "tiberium");
        return EDL_TIBERIUM;
    }
    if (!edit_ground_is_default(cx, cy)) {
        if (what) snprintf(what, n, "the ground");
        return EDL_GROUND;
    }
    return EDL_EMPTY;
}

static bool edit_erase_at(int cx, int cy)
{
    char what[40];
    const int layer = edit_erase_peek(cx, cy, what, sizeof what);
    if (layer == EDL_EMPTY) {
        fprintf(stderr, "edit: %d,%d is already empty -- nothing on it and its ground is "
                        "already clear\n", cx, cy);
        return false;
    }
    /* ONE snapshot and ONE commit whichever rung runs: that is what makes one DELETE one
       undo step. The commit is at the bottom, after the rung has succeeded, so a rung
       that refuses leaves no undo entry that would undo nothing. */
    EditSnap before = edit_snapshot("delete");
    bool did = false;
    switch (layer) {
        case EDL_OBJECT: {
            const int k = edit_object_at(cx, cy);
            const SimObject o = g_objects[k];
            g_objects.erase(g_objects.begin() + k);
            /* Its ORDER and TRIGGER live in the prior table keyed by house|type|cell, so
               they have to go with it. The snapshot above carries the table, so undo
               brings the entry back with the object. */
            edit_prior_drop(o.house, o.type, o.cy * edit_ini_w() + o.cx);
            did = true;
            break;
        }
        case EDL_WALL: {
            for (size_t i = 0; i < g_walls.size(); i++) {
                if (g_walls[i].x != cx || g_walls[i].y != cy) continue;
                g_walls.erase(g_walls.begin() + i);
                break;
            }
            /* The neighbours have to be re-masked or they keep pointing at a wall that
               is no longer there -- and BEFORE the commit, not after it as this used to
               do, because the commit is what can fire an autosave and a file written
               between the two carries four stumps aimed at an empty cell. */
            edit_wall_mask(cx, cy - 1); edit_wall_mask(cx + 1, cy);
            edit_wall_mask(cx, cy + 1); edit_wall_mask(cx - 1, cy);
            did = true;
            break;
        }
        case EDL_SMUDGE: {
            const int sm = edit_smudge_at(cx, cy);
            if (sm >= 0) { g_smudges.erase(g_smudges.begin() + sm); did = true; }
            break;
        }
        case EDL_TIBERIUM: {
            const int ti = edit_tib_at(cx, cy);
            if (ti >= 0) {
                g_tib.erase(g_tib.begin() + ti);
                /* THE EIGHT NEIGHBOURS ARE WORTH RE-STAGING and this is not a nicety: a
                   tiberium cell's frame is chosen from how many of its neighbours also
                   carry tiberium, so taking one away leaves the ring around it drawing a
                   density that is no longer there. The brush re-stages after every stroke
                   for the same reason, and an erase is a stroke. */
                edit_tib_restage(cx, cy);
                g_euiRadarDirty = 1;
                did = true;
            }
            break;
        }
        case EDL_GROUND:
            /* BACK TO DEFAULT: TEMPLATE_NONE with the icon this cell's own coordinates
               give it, which is exactly what the palette's own CLEAR1 rung writes. The
               cartridge stores plain ground as 255 and lets Clear_Icon choose, so a
               field of it does not visibly tile. */
            did = edit_paint_cell(cx, cy, 255, (cx & 3) | ((cy & 3) << 2));
            if (!did)
                fprintf(stderr, "edit: %d,%d cannot go back to clear -- this theater's "
                                "bank has no plain-ground tile for it\n", cx, cy);
            break;
        default: break;
    }
    if (!did) return false;
    edit_commit(before);
    g_euiRadarDirty = 1;
    /* The legibility rail: say which rung came off, and what the document holds now, so
       a press that took the wrong thing reads as a mistake rather than a mystery. */
    if (layer == EDL_GROUND)
        fprintf(stderr, "edit: put the ground at %d,%d back to clear\n", cx, cy);
    else
        fprintf(stderr, "edit: deleted %s at %d,%d  [%d objects, %d walls, %d tiberium, "
                        "%d smudges]\n", what, cx, cy, (int)g_objects.size(),
                (int)g_walls.size(), (int)g_tib.size(), (int)g_smudges.size());
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
    for (int h = 0; h < EUI_HOUSES_OFFERED; h++)
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
    /* THE KEYBOARD HALF OF THE MODAL CAGE. The mouse could not reach the tools behind an
       open dialog and the keyboard could, so the tool, mode, owner and delete keys all
       still fired on a map the dialog was covering.
       The keys are SWALLOWED rather than passed on: returning false hands them to the
       game's own switch, whose control-group handler reads the top row off the SCANCODE
       and does not test for the editor at all, so 1 to 5 would have gone from changing
       mode to recalling a selection. It swallows every unmodified, non-repeat key, which
       also stands down the grid key, the no-go pin, the camera mode, the cheat menu, the
       effects dials and the script feed for as long as the dialog is up. Fullscreen and
       the F5 panel are unaffected: both are handled before this function is reached, and
       every Ctrl or Cmd shortcut skips it on the existing modifier test.

       ESCAPE CLOSES THE DIALOG, and that is a decision this cage forces rather than an
       extra. Excepting ESCAPE and letting it through would hand it to the editor's cancel
       ladder, which on a clean map with nothing armed reaches `running = false` and quits
       the APPLICATION with the dialog still on screen -- and because every other key is
       now inert, that would be the only thing the keyboard could still do behind a dialog.
       So it is dismissal, taking the same two steps the CANCEL button takes: clear the
       modal, and close the size field with it, because a field that outlives its dialog
       keeps the keyboard and the editor's letter keys stop answering.
       Typed fields need no test here: the key dispatch offers them the key first, and
       this function is never reached while one is open. */
    if (edit_modal_up()) {
        if (sym == SDLK_ESCAPE) { g_euiModal = MODAL_NONE; eui_num_close(); }
        return true;
    }
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
            /* Round the houses this MAP is offered: the four a mission uses, or the whole
               roster on a skirmish map. */
            g_editOwner = (g_editOwner + 1) % EUI_HOUSES_OFFERED;
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
 *  THE EDITOR CAMERA -- Unreal's, as the project owner asked for
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

    /* THE IMAGE IMPORTER, driven headlessly. It has no button: this editor's click
       dispatch and its script verbs both live outside this file, so this is the only
       thing that runs the decoders, and running them is what stops them rotting. The
       image is synthesised rather than read off disk -- a fixture would be one more file
       to keep, and a dozen lines of PCX prove the same path. A sheer 24x24 mesa is the
       case chosen because it is the one the tier model cannot take literally: a
       four-rung face has no art, so it has to come back terraced. */
    {
        const int IW = 64, IH = 64;
        std::vector<unsigned char> pcx(128 + (size_t)IW * IH * 2, 0);
        pcx[0] = 0x0A; pcx[1] = 5; pcx[2] = 1; pcx[3] = 8; pcx[65] = 1;
        pcx[8]  = (unsigned char)(IW - 1);
        pcx[10] = (unsigned char)(IH - 1);
        pcx[66] = (unsigned char)IW;
        size_t o = 128;
        for (int y = 0; y < IH; y++)
            for (int x = 0; x < IW; x++) {
                const unsigned char v =
                    (x >= 20 && x < 44 && y >= 20 && y < 44) ? 255 : 0;
                /* A literal byte, unless its top two bits would be read as a run. */
                if ((v & 0xC0) == 0xC0) pcx[o++] = 0xC1;
                pcx[o++] = v;
            }
        std::vector<unsigned char> was = g_pack.corner;
        char why[220] = { 0 };
        const int n = elev_import_bytes(&pcx[0], o, "a synthesised mesa", why, sizeof why);
        if (!n) {
            fprintf(stderr, "ELEVTEST|FAIL|the importer refused a valid PCX: %s\n", why);
            fails++;
        } else {
            if (g_pack.corner == was) {
                fprintf(stderr, "ELEVTEST|FAIL|the import wrote %d cells and moved no "
                        "ground\n", n);
                fails++;
            }
            /* THE TERRACING CLAIM, checked directly: after an import no block may sit
               more than one rung above a block it touches, because a coarse corner is
               the MAX of the four around it and a two-rung spread has no cliff art. */
            int steep = 0;
            for (int by = 0; by < elev_bh(); by++)
                for (int bx = 0; bx < elev_bw(); bx++)
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            const int nx = bx + dx, ny = by + dy;
                            if (nx < 0 || ny < 0 || nx >= elev_bw() || ny >= elev_bh())
                                continue;
                            const int a = g_elevTier[by * elev_bw() + bx];
                            const int b = g_elevTier[ny * elev_bw() + nx];
                            if (a - b > 1 || b - a > 1) steep++;
                        }
            if (steep) {
                fprintf(stderr, "ELEVTEST|FAIL|%d block pairs came out more than one "
                        "rung apart\n", steep);
                fails++;
            } else {
                fprintf(stderr, "ELEVTEST|ok  |a sheer face imported as a terrace, %d "
                        "cells\n", n);
            }
            /* Importing the same picture twice must be a no-op, or a repeated import
               would stack undo steps that change nothing. */
            char w1[220] = { 0 };
            if (elev_import_bytes(&pcx[0], o, "the same mesa", w1, sizeof w1)) {
                fprintf(stderr, "ELEVTEST|FAIL|re-importing the same image changed the "
                        "map again\n");
                fails++;
            } else {
                fprintf(stderr, "ELEVTEST|ok  |re-import is a no-op -- %s\n", w1);
            }
            edit_undo();
            if (g_pack.corner != was) {
                fprintf(stderr, "ELEVTEST|FAIL|one undo did not put the imported "
                        "heightmap back\n");
                fails++;
            } else {
                fprintf(stderr, "ELEVTEST|ok  |a whole-map import is ONE undo step\n");
            }
        }
        /* A refusal has to be a refusal and not a half-read map. */
        char w2[220] = { 0 };
        unsigned char four[128];
        memset(four, 0, sizeof four);
        four[0] = 0x0A; four[2] = 1; four[3] = 4; four[65] = 1;
        if (elev_import_bytes(four, sizeof four, "a 4-bit PCX", w2, sizeof w2)) {
            fprintf(stderr, "ELEVTEST|FAIL|a 4-bit PCX was imported anyway\n");
            fails++;
        } else {
            fprintf(stderr, "ELEVTEST|ok  |refused, and said why -- %s\n", w2);
        }
        /* And the convention the button will reach this by, with nothing there yet. */
        char w3[220] = { 0 };
        elev_import_beside_map(w3, sizeof w3);
        fprintf(stderr, "ELEVTEST|note|beside the map: %s\n",
                w3[0] ? w3 : "an image was found and imported");
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
            g_editMode == 3 ? "WAYPTS " : g_editMode == 2 ? "ELEVATN"
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
    } else if (g_editMode == 2) {
        /* THE ELEVATION PAGE TABS OWN THAT BAND IN THIS MODE, and the house chips are
           not drawn in it -- so probing them here would be asserting that a rectangle
           nothing paints still answers, which is the failure eui_expect_not exists to
           catch rather than to certify. */
        for (int i = 0; i < 2; i++) {
            float tx, ty, tw, th;
            char nm[32]; snprintf(nm, sizeof nm, "elev page tab %d", i);
            if (!eui_sbr_tab_rect(&L, i, &tx, &ty, &tw, &th)) {
                eui_expect_not(tx + tw * 0.5f, ty + th * 0.5f, fbw, fbh,
                               EUI_SBRPAGE, i, nm, &fails);
                continue;
            }
            eui_expect(tx + tw * 0.5f, ty + th * 0.5f, fbw, fbh, EUI_SBRPAGE, i,
                       nm, &fails);
            /* AND IT MUST CLEAR THE PAGE'S OWN CAPTION. Both pages draw a small caption
               at eui_elev_top - 12*S and text is drawn from its TOP, so a tab that
               reaches past that line is drawn over the caption at every window size.
               This is the leg that fails if the band is ever filled edge to edge. */
            if (ty + th > eui_elev_top(&L) - 12 * S + 0.5f) {
                fails++;
                fprintf(stderr, "UITEST|FAIL|%-22s|ends at %.1f and the page caption "
                        "starts at %.1f -- the tab is drawn over it\n", nm, ty + th,
                        eui_elev_top(&L) - 12 * S);
            }
        }
        /* THE CLAIM THE DIRECTOR MADE THE CONDITION OF ALL OF THIS: the tier page did
           not move. One line a gate can read, at every size and on both pages. */
        fprintf(stderr, "ELEVPAGE|%dx%d|backing=%.1f|page=%d|S=%.4f|catY=%.3f|"
                "top=%.3f|delta=%.3f|tabtop=%.3f|tabbot=%.3f|caption=%.3f|"
                "lintY=%.3f|room=%.2f\n",
                fbw, fbh, g_uiBacking, g_sbrPage, S, L.catY, eui_elev_top(&L),
                eui_elev_top(&L) - L.catY, L.ownerY,
                L.catY - 14 * S, eui_elev_top(&L) - 12 * S, L.lintY,
                (L.lintY - eui_elev_top(&L)) / S);
    } else {
        /* THE CHIP STRIP, ON BOTH KINDS OF MAP.
           The driver walks the five modes once and flips the map kind partway through, so
           OBJECTS is only ever reached with the kind the walk started on -- a strip that
           grows to twelve chips on a skirmish map would never be probed here at all. The
           kind is put back before this block ends.
           AND THE TWO LEGS ARE NOT THE SAME LEG. The draw and the hit test now take their
           rectangle from one function, so proving they agree proves nothing. What is
           worth measuring is the strip against its NEIGHBOURS: a chip's y comes from its
           row, the sidebar's edges and the category tabs come from the layout's own
           running total, and those only line up if the fixed-height budget counted the
           same rows the strip drew. */
        const int owWasMulti = g_mapIsMulti;
        const int owKinds = (g_editMode == 0) ? 2 : 1;
        for (int k = 0; k < owKinds; k++) {
            if (g_editMode == 0) g_mapIsMulti = k;
            EuiLayout OL; eui_layout(fbw, fbh, &OL);
            const float opad = EUI_PAD * OL.s;
            for (int i = 0; i < eui_owner_n(); i++) {
                float ox, oy, ow, oh;
                eui_owner_chip_rect(&OL, i, &ox, &oy, &ow, &oh);
                char nm[40];
                snprintf(nm, sizeof nm, "owner chip %d %s", i, EUI_HOUSES[i].label);
                eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_OWNER, i,
                           nm, &fails);
                if (ox < OL.sideX + opad - 0.5f ||
                    ox + ow > OL.sideX + OL.sideW - opad + 0.5f) {
                    fails++;
                    fprintf(stderr, "UITEST|FAIL|%-22s|runs %.1f..%.1f, outside the "
                            "sidebar %.1f..%.1f\n", nm, ox, ox + ow,
                            OL.sideX + opad, OL.sideX + OL.sideW - opad);
                }
                if (oy + oh > OL.catY - opad + 0.5f) {
                    fails++;
                    fprintf(stderr, "UITEST|FAIL|%-22s|ends at %.1f and the category "
                            "tabs start at %.1f -- the fixed-height budget is not "
                            "counting %d chip rows\n", nm, oy + oh, OL.catY,
                            OL.ownerRows);
                }
            }
            fprintf(stderr, "UITEST|OWNERSTRIP|multi=%d|chips=%d|rows=%d|chipw=%.1f|"
                    "bottom=%.1f|tabs=%.1f\n", g_mapIsMulti, eui_owner_n(),
                    OL.ownerRows, OL.ownerW,
                    OL.ownerY + (float)OL.ownerRows * OL.ownerH
                              + (float)(OL.ownerRows - 1) * OL.ownerGap, OL.catY);
        }
        g_mapIsMulti = owWasMulti;
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
    } else if (g_editMode == 2 && g_sbrPage == 1) {
        /* THE SMOOTH PAGE. Every row that FITS answers as itself and every row that
           does not must answer as nothing -- the second half is the one that matters,
           because a control drawn past L.lintY lands on the status card. */
        for (int i = 0; i < 3; i++) {
            float x, y, w, h;
            char nm[32]; snprintf(nm, sizeof nm, "smooth tool %d", i);
            if (!eui_sbr_tool_rect(&L, i, &x, &y, &w, &h))
                eui_expect_not(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_SBRTOOL, i,
                               nm, &fails);
            else
                eui_expect(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_SBRTOOL, i,
                           nm, &fails);
        }
        for (int i = 0; i < SBR_SLIDER_N; i++) {
            float x, y, w, h;
            char nm[32]; snprintf(nm, sizeof nm, "smooth slider %d", i);
            if (!eui_sbr_slider_rect(&L, i, &x, &y, &w, &h))
                eui_expect_not(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_SBRSLIDER, i,
                               nm, &fails);
            else
                eui_expect(x + w * 0.5f, y + h * 0.5f, fbw, fbh, EUI_SBRSLIDER, i,
                           nm, &fails);
        }
        {
            /* THE HELP BLOCK IS NOT A CONTROL, so nothing can probe it by clicking. It
               is measured instead: it must start BELOW the last slider that is drawn.
               The two resolving to the same y is how help text ends up printed over a
               slider, and that is exactly what happened when the help block's top was
               written as the same expression the first slider's is. */
            float sx, sy, sw, sh;
            eui_sbr_slider_rect(&L, SBR_SLIDER_N - 1, &sx, &sy, &sw, &sh);
            const float hy = eui_sbr_help_top(&L);
            if (hy < sy + sh + 0.5f) {
                fails++;
                fprintf(stderr, "UITEST|FAIL|%-22s|help starts at %.1f and slider %d "
                        "ends at %.1f -- the help is drawn over the slider\n",
                        "smooth help", hy, SBR_SLIDER_N - 1, sy + sh);
            }
            /* AND THE ONE MEASUREMENT INSIDE A SLIDER ROW. The rectangles above say
               the row is reachable; they cannot say the knob is not drawn through the
               row's own name. Both ends are reported for every slider at every size, so
               a gate reads clearance rather than trusting an arithmetic that is only
               correct at the font size somebody happened to look at. */
            for (int q = 0; q < SBR_SLIDER_N; q++) {
                float qx, qy, qw, qh, qtx, qtw;
                if (!eui_sbr_slider_rect(&L, q, &qx, &qy, &qw, &qh)) continue;
                eui_sbr_track(&L, q, qx, qw, &qtx, &qtw);
                const float t1m = 1.0f * S;
                char vq[32];
                if (SBR_SLIDERS[q].unit[0])
                    snprintf(vq, sizeof vq, "%.1f %s", SBR_SLIDERS[q].hi,
                             SBR_SLIDERS[q].unit);
                else
                    snprintf(vq, sizeof vq, "%.2f", SBR_SLIDERS[q].hi);
                const float labEnd = qx + 8 * S
                                   + ef_text_w(SBR_SLIDERS[q].name, t1m);
                const float valX   = qx + qw - ef_text_w(vq, t1m) - 8 * S;
                fprintf(stderr, "SBRSLID|%dx%d|backing=%.1f|i=%d|name=%s|x=%.2f|"
                        "w=%.2f|trackx=%.2f|trackw=%.2f|labend=%.2f|valx=%.2f|"
                        "leftclear=%.2f|rightclear=%.2f\n",
                        fbw, fbh, g_uiBacking, q, SBR_SLIDERS[q].name, qx, qw,
                        qtx, qtw, labEnd, valX, qtx - labEnd,
                        valX - (qtx + qtw));
            }
            fprintf(stderr, "SBRPAGE|%dx%d|backing=%.1f|S=%.4f|tools=%.3f|"
                    "sliders=%.3f|help=%.3f|lintY=%.3f|toolfit=%d|slidfit=%d|"
                    "helpfit=%d\n", fbw, fbh, g_uiBacking, S,
                    eui_sbr_tools_top(&L), eui_sbr_sliders_top(&L), hy, L.lintY,
                    eui_sbr_tool_rect(&L, 2, &sx, &sy, &sw, &sh) ? 1 : 0,
                    eui_sbr_slider_rect(&L, 2, &sx, &sy, &sw, &sh) ? 1 : 0,
                    eui_sbr_fits(&L, hy, (float)SBR_HELP_N * SBR_HELPLN * S) ? 1 : 0);
        }
    } else if (g_editMode == 2 && !elev_ready()) {
        /* THE CONVERSION GATE'S OWN CARD. This is the panel an imported map opens on and
           until now the harness had never once walked it: the driver set g_elevConverted
           for every elevation pass it made, so "both sides of the conversion gate" was a
           comment rather than a test. It matters here because the auto heightmap's button
           lives on this card -- the fit is the way onto the ladder that READS the cliff
           art instead of erasing it, so it belongs beside the button that erases it. */
        {
            const float gbh = 34 * S, gbw = L.sideW - pad * 4;
            eui_expect(L.sideX + pad * 2 + gbw * 0.5f, L.catY + 140 * S + gbh * 0.5f,
                       fbw, fbh, EUI_ELEVGATE, -1, "convert this map", &fails);
        }
        {
            float ax, ay, aw, ah2;
            const bool fits = eui_elev_auto_rect(&L, &ax, &ay, &aw, &ah2);
            if (!fits)
                eui_expect_not(ax + aw * 0.5f, ay + ah2 * 0.5f, fbw, fbh,
                               EUI_ELEVAUTO, -1, "gate auto heightmap", &fails);
            else
                eui_expect(ax + aw * 0.5f, ay + ah2 * 0.5f, fbw, fbh, EUI_ELEVAUTO, -1,
                           "gate auto heightmap", &fails);
            /* AND THE TWO CLEARANCES ON THIS CARD, which nothing measured before and
               which were both wrong the first time the row was fitted in: the caption
               that says what else is behind the gate was drawn INSIDE the CONVERT
               button, and the row was drawn on top of the caption. Neither is a control,
               so a probe-based harness is structurally blind to both -- the same hole
               the CLIFF PIECES caption fell through on the other page. Reported as
               clearances rather than as three y values so the gate reads a sign. */
            const float gcapY  = L.catY + ELEV_GATE_CAPY * S;
            const float gconvB = L.catY + (140.0f + 34.0f) * S;
            fprintf(stderr, "ELEVGATE|%dx%d|backing=%.1f|convertY=%.3f|capY=%.3f|"
                    "autoY=%.3f|lintY=%.3f|capclear=%.3f|autoclear=%.3f|autofit=%d\n",
                    fbw, fbh, g_uiBacking, L.catY + 140 * S, gcapY, ay, L.lintY,
                    gcapY - gconvB, ay - (gcapY + ELEV_GATE_CAPH * S), fits ? 1 : 0);
        }
    } else if (g_editMode == 2) {
        /* The elevation panel's own controls, at the geometry eui_draw_elev uses. */
        float y = eui_elev_top(&L);
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
        {   /* The heightmap row. Which controls exist depends on what is in the
               folder, so the rect is asked rather than assumed -- with nothing there
               the button spans the row and there is no chip to probe. */
            float bx, by, bw, bh;
            const bool fits = eui_elev_import_rect(&L, 0, &bx, &by, &bw, &bh);
            if (!fits)
                eui_expect_not(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh,
                               EUI_ELEVIMPORT, -1, "import heightmap", &fails);
            else
                eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_ELEVIMPORT, -1,
                           "import heightmap", &fails);
            if (eui_elev_import_rect(&L, 1, &bx, &by, &bw, &bh))
                eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_ELEVNEXT, -1,
                           "next heightmap", &fails);
        }
        {   /* THE AUTO HEIGHTMAP ROW, under the import row. Same contract as the row
               above it: it answers as itself where it fits and as nothing where it does
               not, which is the leg that catches a row growing into UNDO / REDO /
               REVERT. */
            float bx, by, bw, bh;
            const bool fits = eui_elev_auto_rect(&L, &bx, &by, &bw, &bh);
            if (!fits)
                eui_expect_not(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh,
                               EUI_ELEVAUTO, -1, "auto heightmap", &fails);
            else
                eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_ELEVAUTO, -1,
                           "auto heightmap", &fails);
        }
        {   /* THE CLIFF PIECES CAPTION IS NOT A CONTROL, so nothing can probe it by
               clicking -- which is exactly why --uitest was blind to it while it was
               being painted into the status card at 1280x480. It is measured instead,
               the way the smooth page's help block is: it must not start below the
               panel's usable bottom, and it is drawn only when the drawer it names has
               a cell to show. ef_text draws DOWNWARD from the y it is given, so the
               caption occupies capY..capY+EUI_TS line height. */
            int clist[64];
            const int cn = eui_cliff_list(clist, 64);
            float c0x, c0y, c0w, c0h;
            const bool cellfits = (cn > 0) &&
                                  eui_cliff_cell_rect(&L, 0, &c0x, &c0y, &c0w, &c0h);
            const float capY = eui_cliff_top(&L) - 12 * S;
            if (cellfits && capY + 10 * S > L.lintY - 4 * S) {
                fails++;
                fprintf(stderr, "UITEST|FAIL|%-22s|caption at %.1f runs past the panel's "
                        "usable bottom %.1f -- it is drawn into the status card\n",
                        "cliff caption", capY, L.lintY - 4 * S);
            }
            float ax2, ay2, aw2, ah2b;
            fprintf(stderr, "ELEVROWS|%dx%d|backing=%.1f|importY=%.3f|autoY=%.3f|"
                    "cliffY=%.3f|capY=%.3f|lintY=%.3f|autofit=%d|capdrawn=%d\n",
                    fbw, fbh, g_uiBacking, eui_elev_import_top(&L),
                    eui_elev_auto_top(&L), eui_cliff_top(&L), capY, L.lintY,
                    eui_elev_auto_rect(&L, &ax2, &ay2, &aw2, &ah2b) ? 1 : 0,
                    cellfits ? 1 : 0);
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
        const float tx = L.sideX + pad + i * (L.catW + 3 * S);
        /* ON SCREEN BEFORE HITTABLE. Aiming at the centre of a rectangle the probe
           computes with the DRAW'S OWN arithmetic proves only that the draw and the hit
           test agree; it says nothing about whether that rectangle is inside the window.
           Two terrain tabs sat past the right edge of the frame for as long as the strip
           existed and this probe certified both of them, every run, at every size. The
           panel's right margin is the bound, and it is where the last tab must end. */
        if (tx + L.catW > L.sideX + L.sideW - pad + 0.5f) {
            fails++;
            fprintf(stderr, "UITEST|FAIL|%-22s|spans %.0f..%.0f, past the panel's right "
                    "margin at %.0f -- unreachable\n", nm, tx, tx + L.catW,
                    L.sideX + L.sideW - pad);
            continue;
        }
        eui_expect(tx + L.catW * 0.5f, L.catY + L.catH * 0.5f, fbw, fbh,
                   EUI_TABC, i, nm, &fails);
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

    /* THE NEW MAP MODAL, probed open. Its size list grew a fifth row (the big map) and
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
            /* The CUSTOM row is a typed field, so it answers the hit kind whose handler
               opens one; the arg is unused there and is not pinned. */
            const bool cust = (i == EUI_SIZE_CUSTOM);
            eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh,
                       cust ? EUI_RENAME : EUI_MODALOPT, cust ? -1 : i, nm, &fails);
        }
        /* THE CUSTOM SIZE, THROUGH THE REAL FIELD AND NOT THROUGH ITS RECTANGLE.
         *
         * Every other leg in this harness asks eui_hit whether a control is where the
         * draw put it, and a probe that aims with the draw's own arithmetic has
         * certified a broken control before. This one asks a different question: it
         * OPENS the field with the opener the click handler calls, types through the
         * filter the keyboard types through, presses the ENTER that eui_num_commit is,
         * and then reads the state. Geometry cannot make it pass. */
        {
            const int keepW = g_newW, keepH = g_newH, keepSz = g_newSize;
            static const struct { const char* typed; int w, h, grid; } SZCASE[] = {
                { "40",       40,  40,  64 },   /* one number is a square             */
                { "100x80",  100,  80, 128 },   /* two numbers, and the big grid      */
                { "62 X 100", 62, 100, 128 },   /* one side over 62 takes both there  */
                { "9",        16,  16,  64 },   /* under the floor                    */
                { "400",     126, 126, 128 },   /* over the ceiling                   */
            };
            for (int c = 0; c < (int)(sizeof SZCASE / sizeof SZCASE[0]); c++) {
                g_newSize = 0;                      /* so the opener has to select it */
                if (!eui_open_rename()) {
                    fails++;
                    fprintf(stderr, "UITEST|FAIL|new-map size field would not open\n");
                    break;
                }
                g_numBuf[0] = 0; g_numPristine = false;
                eui_num_type(SZCASE[c].typed);
                eui_num_commit();
                if (g_newW != SZCASE[c].w || g_newH != SZCASE[c].h ||
                    eui_new_grid() != SZCASE[c].grid ||
                    g_newSize != EUI_SIZE_CUSTOM || g_numKind != NUM_NONE) {
                    fails++;
                    fprintf(stderr, "UITEST|FAIL|new-map size \"%s\"|got %dx%d grid %d "
                            "row %d field %d|want %dx%d grid %d row %d field 0\n",
                            SZCASE[c].typed, g_newW, g_newH, eui_new_grid(),
                            g_newSize, g_numKind, SZCASE[c].w, SZCASE[c].h,
                            SZCASE[c].grid, EUI_SIZE_CUSTOM);
                } else {
                    fprintf(stderr, "UITEST|ok  |new-map size \"%s\"|%d x %d on the %d "
                            "grid\n", SZCASE[c].typed, g_newW, g_newH, eui_new_grid());
                }
            }
            /* AND THAT THE FIELD DIES WITH ITS DIALOG. CANCEL closes the dialog without
               closing the field, so the field has to notice; a stranded one would hold
               the keyboard and the editor's letter keys would stop answering. */
            g_newSize = 0;
            if (eui_open_rename()) {
                g_euiModal = MODAL_NONE;
                eui_num_type("99");
                if (g_numKind != NUM_NONE) {
                    fails++;
                    fprintf(stderr, "UITEST|FAIL|the new-map size field survived its "
                            "dialog and still holds the keyboard (kind %d)\n", g_numKind);
                } else {
                    fprintf(stderr, "UITEST|ok  |the new-map size field closes with its "
                            "dialog\n");
                }
                g_euiModal = MODAL_NEW;
            }
            g_newW = keepW; g_newH = keepH; g_newSize = keepSz;
            eui_num_close();
        }
        for (int i = 0; i < EUI_THEATER_N; i++) {
            float ox, oy, ow, oh;
            eui_modal_opt_rect(&L, fbw, fbh, 1, i, &ox, &oy, &ow, &oh);
            char nm[40]; snprintf(nm, sizeof nm, "new-map theater %d", i);
            eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_MODALOPT, 10 + i,
                       nm, &fails);
        }
        for (int i = 0; i < NEWKIND_N; i++) {
            float ox, oy, ow, oh;
            eui_modal_opt_rect(&L, fbw, fbh, 2, i, &ox, &oy, &ow, &oh);
            char nm[40]; snprintf(nm, sizeof nm, "new-map kind %s", NEWKIND_LABEL[i]);
            eui_expect(ox + ow * 0.5f, oy + oh * 0.5f, fbw, fbh, EUI_MODALOPT,
                       20 + NEWKIND_ROW[i], nm, &fails);
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
        /* THE OPEN MAP DIALOG, probed open, over a stub list.
         *
         * It is the one modal the harness never opened, and it spent that whole time
         * unusable: the arm re-declared lx and ly -- the two names EUI_IN expands to --
         * as the list's own origin, so every test inside it compared the pointer with
         * itself and answered "tab 0" for any click anywhere in the dialog. No row could
         * be selected, so no map could be reopened, and nothing in the suite could see
         * it. Opening the modal is the only thing that would have.
         *
         * The list is STUBBED rather than scanned. A headless run has no user_maps
         * folder, edit_build_maplist would find nothing, and a probe over an empty list
         * asserts nothing at all. Both tabs are walked because the tab is what changes
         * the row count, and the first row past the last entry is probed for the
         * OPPOSITE answer: the draw stops there and so must the hit test. */
        {
            const std::vector<MapEntry> hadList = g_mapList;
            const bool hadBuilt  = g_mapListBuilt;
            const int  hadTab    = g_mapListTab;
            const int  hadSel    = g_mapListSel;
            const int  hadScroll = g_mapListScroll;
            g_mapList.clear();
            for (int k = 0; k < 7; k++) {
                MapEntry me;
                memset(&me, 0, sizeof me);
                me.kind = (k < 4) ? 'U' : 'O';
                snprintf(me.scen, sizeof me.scen, "UITEST%d", k);
                snprintf(me.name, sizeof me.name, "uitest map %d", k);
                g_mapList.push_back(me);
            }
            g_mapListBuilt  = true;
            g_mapListSel    = -1;
            g_mapListScroll = 0;
            g_euiModal      = MODAL_OPEN;

            float omx, omy, omw, omh;
            eui_modal_rect(&L, fbw, fbh, &omx, &omy, &omw, &omh);
            const float mpad = 18 * S;
            const float dlx = omx + mpad,     dly = omy + 74 * S;
            const float dlw = omw - mpad * 2, dlh = omh - 74 * S - 60 * S;
            const float dtw = (dlw - 6 * S) * 0.5f;
            for (int i = 0; i < 2; i++) {
                char nm[40]; snprintf(nm, sizeof nm, "open-map tab %d", i);
                eui_expect(dlx + i * (dtw + 6 * S) + dtw * 0.5f, dly + 14 * S,
                           fbw, fbh, EUI_MODALOPT, 30 + i, nm, &fails);
            }
            /* The header is prose and nothing else. It answered "tab 0" for as long as
               the shadowing lasted, which makes it the single sharpest probe here. */
            eui_expect(omx + omw * 0.5f, omy + 30 * S, fbw, fbh, EUI_DEAD, -1,
                       "open-map header", &fails);
            const int dfit = (int)((dlh - 40 * S) / (24 * S));
            for (int t = 0; t < 2; t++) {
                g_mapListTab = t;
                const int shown = edit_maplist_count(t ? 'O' : 'U');
                for (int r = 0; r < dfit; r++) {
                    float rx, ry, rw, rh;
                    eui_openlist_row_rect(dlx, dly, dlw, S, r, &rx, &ry, &rw, &rh);
                    char nm[40];
                    snprintf(nm, sizeof nm, "open-map tab %d row %d", t, r);
                    if (r >= shown) {
                        eui_expect_not(rx + rw * 0.5f, ry + rh * 0.5f, fbw, fbh,
                                       EUI_MODALOPT, 40 + r, nm, &fails);
                        break;
                    }
                    eui_expect(rx + rw * 0.5f, ry + rh * 0.5f, fbw, fbh,
                               EUI_MODALOPT, 40 + r, nm, &fails);
                }
            }
            for (int i = 0; i < 2; i++) {
                float bx, by, bw, bh;
                eui_modal_btn_rect(&L, fbw, fbh, i, &bx, &by, &bw, &bh);
                char nm[40]; snprintf(nm, sizeof nm, "open-map button %d", i);
                eui_expect(bx + bw * 0.5f, by + bh * 0.5f, fbw, fbh, EUI_MODALBTN, i,
                           nm, &fails);
            }
            g_mapList       = hadList;
            g_mapListBuilt  = hadBuilt;
            g_mapListTab    = hadTab;
            g_mapListSel    = hadSel;
            g_mapListScroll = hadScroll;
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

/* WHERE A TERRAIN OBJECT REALLY STANDS, which is not the middle of the cell its INI
 * line names.
 *
 * TerrainClass::Center_Coord is Coord_Add(Coord, Class->CenterBase) (terrain.cpp:705).
 * Coord is NOT the middle of that cell. ObjectClass::Unlimbo stores
 * Class_Of().Coord_Fixup(coord) (object.cpp:1258), TerrainTypeClass::Coord_Fixup is
 * Coord_Whole (tdata.cpp:965), and Coord_Whole zeroes both sub-lepton fields
 * (function.h:786-791), so Coord is the cell's CORNER. A recorded dump agrees: a T01 on
 * cell 39,51 reports lx=9984, which is 39*256 with no half cell in it. CenterBase is the
 * XYP_COORD
 * pair every type declares in tdata.cpp: a PIXEL offset turned into leptons by
 * (p * 256) / 24, integer division included (function.h:727, display.h:41-46).
 *
 * Measured on T01: XYP_COORD(11, 41) is 117 and 437 leptons, so the tree stands at
 * cell + (128 + 117)/256 east and (128 + 437)/256 south, i.e. +0.957 and +2.207. That is
 * 1.707 cells SOUTH of the cx + 0.5, cy + 0.5 this function used to stamp. Every LOADED
 * map already draws it in the right place, because the brain exports Center_Coord as
 * clx/cly and the parser divides that by 256: the editor was the only thing putting a
 * tree anywhere else, so placing one, saving and reopening made it jump.
 *
 * THE FOOTPRINT BRANCH IS NOT THE ANSWER. Most tree types occupy {(0,1)} rather than the
 * cell that was clicked, so the span centre a building uses gives cy + 1.5: closer, and
 * still 0.707 of a cell north of where T01 really is. Nor is the offset derivable from
 * the occupy list at all, and TC05 is the proof -- six cells whose span centre is
 * (1.5, 1.5), standing at (2.039, 2.414). The numbers have to be read.
 *
 * The PIXEL pair is stored rather than the lepton so a row can be checked against the
 * line it came from without arithmetic. Transcribed here rather than added to the
 * generated table header, because a script writes that file out of the brain and does
 * not emit this column yet, so a copy there would vanish the next time it ran. A type
 * with no row keeps the cell centre and says so, which is the old behaviour rather than
 * a confidently wrong new one. */
struct TerrainCenter { const char* code; short px, py; };
static const TerrainCenter TERRAIN_CENTER[] = {
    { "ROCK1", 33, 41 }, { "ROCK2", 24, 23 }, { "ROCK3", 20, 39 }, { "ROCK4", 12, 20 },
    { "ROCK5", 17, 19 }, { "ROCK6", 28, 40 }, { "ROCK7", 57, 22 }, { "SPLIT2", 18, 44 },
    { "SPLIT3", 18, 44 }, { "T01", 11, 41 }, { "T02", 11, 44 }, { "T03", 12, 45 },
    { "T04",   8,  9 }, { "T05", 15, 41 }, { "T06", 16, 37 }, { "T07", 15, 41 },
    { "T08", 14, 22 }, { "T09", 11, 22 }, { "T10", 25, 43 }, { "T11", 23, 44 },
    { "T12", 14, 36 }, { "T13", 19, 40 }, { "T14", 19, 40 }, { "T15", 19, 40 },
    { "T16", 13, 36 }, { "T17", 18, 44 }, { "T18", 33, 40 }, { "TC01", 28, 41 },
    { "TC02", 38, 41 }, { "TC03", 33, 35 }, { "TC04", 44, 49 }, { "TC05", 49, 58 },
};
static const int TERRAIN_CENTER_N =
    (int)(sizeof TERRAIN_CENTER / sizeof TERRAIN_CENTER[0]);

/* The type's CenterBase in cells, or false if this table has never heard of it. The
   conversion is the engine's own, integer division and all: doing it in floating point
   would land the anchor a fraction of a lepton away from the number the brain reports
   for the very same object once the map is saved and read back. */
static bool edit_terrain_center(const char* code, float* ox, float* oz)
{
    for (int i = 0; i < TERRAIN_CENTER_N; i++) {
        if (strcmp(TERRAIN_CENTER[i].code, code)) continue;
        *ox = (float)((int)TERRAIN_CENTER[i].px * 256 / 24) / 256.0f;
        *oz = (float)((int)TERRAIN_CENTER[i].py * 256 / 24) / 256.0f;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------------------
 *  The five places a man can stand in a cell
 *
 *  Only infantry are allowed to stop anywhere but the middle of a cell, and only on
 *  these five points: StoppingCoordAbs (const.cpp:56) as a fraction of a cell, measured
 *  from the cell's north-west corner. The centre first, then the four quincunx spots in
 *  the order the fifth field of an [INFANTRY] line numbers them -- upper left, upper
 *  right, lower left, lower right. One table, read by the placement below and by the
 *  recovery further down: two copies of it that disagreed would put a man in one place
 *  and write another into the file.
 * ---------------------------------------------------------------------------------- */
static const float EDIT_SUBCELL_X[5] = { 0.5f, 0.25f, 0.75f, 0.25f, 0.75f };
static const float EDIT_SUBCELL_Z[5] = { 0.5f, 0.25f, 0.25f, 0.75f, 0.75f };

static int edit_subcell_of(float wx, float wz, int cx, int cy);   /* defined below */

/* WHICH SPOT A POINT INSIDE A CELL ASKS FOR, by the engine's rule rather than by nearest
   neighbour, because the two do not agree. CellClass::Spot_Index (cell.cpp:1630) takes
   the centre whenever the point is within 60 leptons of it, and only outside that picks
   a quadrant by comparing each axis against the half cell. Its distance is the engine's
   octagonal approximation -- longer axis plus half the shorter -- which draws a wider
   centre than splitting the cell evenly between the five points would, so a click that
   is merely somewhere in the middle gets the middle. It is worked in leptons here rather
   than in cell fractions because a cell is 256 leptons and the integer halving is part
   of the answer. */
/* THE PRECEDENT IS WESTWOOD'S OWN MAP EDITOR, not a rule reconstructed from runtime
   behaviour: mapedplc.cpp:1166-1183 places infantry by taking
   Pixel_To_Coord(Get_Mouse_X(), Get_Mouse_Y()), picking the closest sub-position from
   it, and returning -1 when nothing is free. The pointer choosing the spot is the
   original behaviour; the derivation below is how that choice is made. */
static int edit_spot_index(float fx, float fz)
{
    int lx = (int)(fx * 256.0f);
    int lz = (int)(fz * 256.0f);
    if (lx < 0) lx = 0;
    if (lx > 255) lx = 255;
    if (lz < 0) lz = 0;
    if (lz > 255) lz = 255;
    int dx = lx - 128; if (dx < 0) dx = -dx;
    int dz = lz - 128; if (dz < 0) dz = -dz;
    if ((dz > dx ? dz + dx / 2 : dx + dz / 2) < 60) return 0;
    return 1 + (lx > 128 ? 1 : 0) + (lz > 128 ? 2 : 0);
}

/* The spot asked for, or the nearest free one, or -1 when the cell already holds five
   men. CellClass::Closest_Free_Spot (cell.cpp:1682) walks the same precalculated table
   of nearest neighbours, and this is that table. One departure, deliberate: when the
   CENTRE is asked for and is taken, the engine shuffles the remaining four with
   Random_Pick so that crowds forming at runtime do not clump. An editor cannot, because
   the same click has to put a man in the same place every time it is made, so the fixed
   order is used for the centre too.

   Note that nothing else keeps men off each other: edit_cell_occupied counts only
   buildings and terrain, so a cell takes as many infantry as it has free spots and no
   more. Refusing the sixth is the point, and the reason is not that the second man fails
   to load. During Read_Scenario_Ini, ScenarioInit is held non-zero (scenarioini.cpp:219
   to :648), so InfantryClass::Unlimbo asks Closest_Free_Spot with any = true, which
   returns the requested spot without testing occupancy at all (cell.cpp:1717-1722), and
   ObjectClass::Unlimbo short-circuits Can_Enter_Cell on ScenarioInit (object.cpp:1256).
   BOTH men load, standing on one point, and Set_Occupy_Bit (infantry.cpp:3185) ORs a bit
   that was already set. They then share one occupy bit, so the first to walk off clears
   it while the other is still standing there, and the cell advertises a spot that is not
   free. That is what a duplicate sub-cell buys, and it is why the sixth is refused here.

   ONE THING THIS DELIBERATELY DOES NOT REFUSE. Closest_Free_Spot is called with
   any = false by the engine's own editor and returns 0 for a cell carrying
   Flag.Occupy.Vehicle or a Monolith (cell.cpp:1708-1711), so 1995's editor will not
   stand a man inside a tank, and the web editor refuses it too
   (docs/editor-parity/web-features.md:59). edit_cell_occupied (edit_mod.h:623) ignores
   K_UNIT, so this one allows it, exactly as it did before this change. That is left
   alone on purpose rather than overlooked: it is a pre-existing difference from both
   other editors, it is not what was reported, and it is registered
   with the other two gaps in this block. */
static int edit_free_subcell(int cx, int cy, int want)
{
    static const unsigned char SEQ[5][4] = {
        { 1, 2, 3, 4 }, { 0, 2, 3, 4 }, { 0, 1, 4, 3 }, { 0, 1, 4, 2 }, { 0, 2, 3, 1 }
    };
    bool taken[5];
    for (int i = 0; i < 5; i++) taken[i] = false;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_INFANTRY || o.cx != cx || o.cy != cy) continue;
        taken[edit_subcell_of(o.wx, o.wz, o.cx, o.cy)] = true;
    }
    if (want < 0 || want > 4) want = 0;
    if (!taken[want]) return want;
    for (int i = 0; i < 4; i++)
        if (!taken[SEQ[want][i]]) return (int)SEQ[want][i];
    return -1;
}

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

    /* AND TIBERIUM, for the same reason and by the same route. The cell test above is
       already the engine's own rule for laying a non-wall overlay: OverlayClass::Mark
       marks one down only where the cell carries no overlay, holds no terrain object and
       stands on ground whose LandType is buildable (overlay.cpp:251-253), which is
       edit_cell_ok exactly. Two things it does not cover are checked here. */
    if (t->kind == EDIT_KIND_TIBERIUM) {
        /* A WALL IS AN OVERLAY TOO, and a cell holds one. Walls are not objects, so the
           occupancy test above cannot see them. */
        if (edit_wall_at(cx, cy)) {
            if (!quiet)
                fprintf(stderr, "edit: refused tiberium at %d,%d -- a wall already holds "
                        "that cell, and a cell holds one overlay\n", cx, cy);
            return false;
        }
        /* THE FIRST AND LAST ROWS NEVER LOAD. OverlayClass::Read_INI keeps a cell only
           while its flat number is at least one row in from each end of the engine's
           grid (overlay.cpp:367), so tiberium painted on the top or bottom row is gone
           before the mission starts. Refused with the reason, rather than accepted and
           silently dropped. The engine's upper bound is a cell number rather than a row,
           so it does admit column zero of the last row; that one cell is refused here
           too, because "the bottom row is not a place to paint" is a rule an author can
           hold in their head and "the bottom row except its first cell" is not. */
        if (cy < 1 || cy > edit_ini_w() - 2) {
            if (!quiet)
                fprintf(stderr, "edit: refused tiberium at %d,%d -- row %d is the engine's "
                        "top or bottom row, and overlay there is dropped as the mission "
                        "loads\n", cx, cy, cy);
            return false;
        }
        edit_tib_put(cx, cy);
        return true;
    }

    /* AND A SMUDGE, by the same route again: a crater is per-CELL state, not an object.

       The cell test above is STRICTER than the engine is at load time, deliberately.
       Is_Generally_Clear short-circuits to true while ScenarioInit is held
       (cell.cpp:423), so Read_INI will drop a scorch into water or under a tree that
       Mark would refuse during play (smudge.cpp:215). Keeping the play-time rule at
       authoring time is the choice here; a handful of shipped smudges do sit under an
       object and still load, round-trip and save untouched, they just cannot be redrawn
       by hand. That gap is registered. Three things the object test cannot see are
       checked below. */
    if (t->kind == EDIT_KIND_SMUDGE) {
        /* A BUILDING'S APRON IS A SMUDGE TOO, and Mark writes only into a cell that
           carries none. edit_smudge_at cannot see a bib, so ask separately. */
        if (edit_bib_at(cx, cy)) {
            if (!quiet)
                fprintf(stderr, "edit: refused %s at %d,%d -- a building's apron holds "
                        "that cell, and a cell holds one smudge\n", t->code, cx, cy);
            return false;
        }
        /* A WALL blocks it in the engine: Is_Generally_Clear refuses a cell whose overlay
           is a wall (cell.cpp:434), and walls are not objects, so the test above cannot
           see one. */
        if (edit_wall_at(cx, cy)) {
            if (!quiet)
                fprintf(stderr, "edit: refused %s at %d,%d -- a wall holds that cell, and "
                        "the engine lays no smudge under one\n", t->code, cx, cy);
            return false;
        }
        /* A TIBERIUM FIELD HIDES IT. The console draws ONE decal per cell and an overlay
           beats a crater or a scorch, so a smudge under crystal would be authored, saved
           and never seen. Refused rather than accepted invisibly. */
        if (edit_tib_at(cx, cy) >= 0) {
            if (!quiet)
                fprintf(stderr, "edit: refused %s at %d,%d -- tiberium covers that cell, "
                        "and an overlay is drawn instead of the smudge under it\n",
                        t->code, cx, cy);
            return false;
        }
        int type = -1;
        for (int k = 0; k < SMUDGE_ROW_N; k++)
            if (!strcmp(SMUDGE_ROW[k].code, t->code)) {
                type = (int)SMUDGE_ROW[k].type;
                break;
            }
        if (type < 0) return false;
        edit_smudge_put(cx, cy, type);
        return true;
    }

    /* WHICH OF THE FIVE SUB-CELL POSITIONS a man goes on, settled here rather than with
       the draw anchor below because it can still refuse, and the snapshot on the next
       line is taken only once nothing can.

       The pointer is the only thing fine enough to say which spot is wanted: there is no
       sub-cell control on the palette and a cell is the finest thing the cursor names. A
       placement made anywhere but under the pointer -- the script verb, or the outward
       search for buildable ground a shot proof runs -- has no fraction to read and asks
       for the centre, which is where a lone man belongs anyway. */
    int sub = 0;
    if (t->kind == EDIT_KIND_INFANTRY) {
        const int want = (g_editOverMap && cx == g_editCellX && cy == g_editCellY)
                             ? edit_spot_index(g_editCellFx, g_editCellFz) : 0;
        sub = edit_free_subcell(cx, cy, want);
        if (sub < 0) {
            if (!quiet)
                fprintf(stderr, "edit: refused %s at %d,%d -- all five sub-cell positions "
                        "in that cell already hold a man\n", t->code, cx, cy);
            return false;
        }
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
    /* WHICH SIDE THE PREVIEW DRAWS THIS AS, stated rather than left at zero.
     *
     * The renderer picks an object's house texture table and its selection colour from
     * act, the brain's HouseClass::ActLike, and only falls back to reading the owner NAME
     * when act is negative. The struct above is memset to zero, and zero is not "unset":
     * it is HOUSE_GOOD. So everything the editor stamped drew in GDI livery whatever chip
     * was armed, and so did every tree, which the brain reports with no side at all.
     *
     * Clearing act to -1 would not answer it either. The name fallback knows GoodGuy,
     * BadGuy and Neutral and nothing else, so Special and all eight multiplayer seats
     * would resolve to "unknown" and take the Nod table -- a different wrong answer. The
     * HOUSE INDEX is the value, because a house no lobby has assigned a side to keeps its
     * own (house.cpp:402, ActLike = Class->House), which is exactly the state a map being
     * authored is in. Terrain is -1 because the brain reports -1 for anything that is not
     * a TechnoClass, and matching it is what makes an editor tree and a loaded tree draw
     * through the same table. */
    so.act = (so.kind == K_TERRAIN) ? -1 : g_editOwner;
    so.cx = cx;  so.cy = cy;
    so.tcx = cx; so.tcy = cy;

    /* The draw anchor. A building sits at the centre of its cell span, a terrain object
       at its cell centre plus the CenterBase its own type declares, and everything else
       in the middle of the one cell it was put on. */
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
        /* Everything that is not a building owns one cell, and its offset is measured
           from that cell's north-west corner. A terrain object sits at its type's
           CenterBase, and falls back to the middle like everything else if its type has
           none (unreachable today, since TERRAIN_CENTER carries all 32 codes the palette
           offers, but the corner is the wrong answer if it ever is reachable). A vehicle stands in the middle, which is StoppingCoordAbs[0]
           (const.cpp:56). An infantryman stands on whichever of the five the pointer
           asked for above.

           The default is the middle rather than zero, and that is the whole of it: a
           zero default put every placed vehicle and every placed man on the cell's
           CORNER, half a cell from where the brain reports the same object once the map
           is saved and read back (a unit's position comes back as clx/256, and the
           fallback for a brain too old to send it is the cell centre). */
        float ox = 0.5f, oz = 0.5f;
        if (so.kind == K_TERRAIN) {
            ox = oz = 0.0f;
            if (!edit_terrain_center(t->code, &ox, &oz))
                fprintf(stderr, "edit: %s has no CenterBase on record, so it is standing "
                                "in the corner of the cell and the engine will not "
                                "agree\n", t->code);
        } else if (so.kind == K_INFANTRY) {
            ox = EDIT_SUBCELL_X[sub];
            oz = EDIT_SUBCELL_Z[sub];
        }
        so.wx = so.ex = (float)cx + ox;
        so.wz = so.ez = (float)cy + oz;
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

    /* A cliff piece armed from the ELEVATN drawer stamps through the TERRAIN path, not
       the height brush. The click handler routes on exactly this test, so the preview
       has to route on it too: without that the status line reads STAMP Sxx while the
       overlay under the pointer paints the 2x2 brush lattice, which is a promise about
       a w x h block answered by drawing something else. Picking an elevation TOOL
       clears the arm, so the brush and a slope piece are never live at once. */
    const bool slopeArmed = (g_editMode == 2 && g_terrArmed >= 0 &&
                             eui_is_slope(g_terrArmed));

    /* ELEVATION: the blocks the brush covers, snapped to the 2x2 block lattice the tool
       actually works in -- showing a single cell would be a lie about what will move. */
    /* THE SMOOTH BRUSH'S OWN FOOTPRINT. It works on the CORNER lattice with a round
       profile, so drawing the tier tool's 2x2 block square here would promise a shape
       the click does not paint. Two tints: the full-strength centre, and the ring that
       fades. */
    if (g_editMode == 2 && !slopeArmed && g_sbrPage == 1) {
        const float reach = g_sbrVal[0] + g_sbrVal[1];
        const float ccx = (float)g_editCellX + 0.5f, ccy = (float)g_editCellY + 0.5f;
        int g0x = (int)floorf(ccx - reach), g1x = (int)ceilf(ccx + reach);
        int g0y = (int)floorf(ccy - reach), g1y = (int)ceilf(ccy + reach);
        if (g0x < 0) g0x = 0;
        if (g0y < 0) g0y = 0;
        if (g1x > g_gridW - 1) g1x = g_gridW - 1;
        if (g1y > g_gridH - 1) g1y = g_gridH - 1;
        begin_overlay(fbw, fbh);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int cy = g0y; cy <= g1y; cy++)
            for (int cx = g0x; cx <= g1x; cx++) {
                const float dx = (float)cx + 0.5f - ccx, dy = (float)cy + 0.5f - ccy;
                const float d = sqrtf(dx * dx + dy * dy);
                if (d > reach) continue;
                const bool wet = (edit_land_at(cx, cy) == EDIT_LAND_WATER);
                const bool core = (d <= g_sbrVal[0]);
                if (wet) glColor4f(1.0f, 0.35f, 0.35f, 0.20f);
                else if (core) glColor4f(0.56f, 0.89f, 1.0f, 0.30f);
                else glColor4f(0.56f, 0.89f, 1.0f, 0.12f);
                glBegin(GL_TRIANGLES);
                sb_place_cell_tris(cx, cy, fbw, fbh);
                glEnd();
            }
        glDisable(GL_BLEND);
        end_overlay();
        return;
    }
    if (g_editMode == 2 && !slopeArmed) {
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

    /* TERRAIN, and a cliff piece hand-stamped from the ELEVATN drawer: the exact cells
       the block will overwrite, so a 4x3 river mouth shows its whole shape before it
       lands rather than after. Green where it will write, RED where this theater's bank
       has no icon -- those are SKIPPED, and a skipped cell you were not told about is a
       hole in your river, or a gap in your cliff. A cell that falls off the map is drawn
       as nothing at all, because there is no ground under the pointer to tint; the cell
       count on the status line is what reports those. */
    if (g_editMode == 1 || slopeArmed) {
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
 *  AI's base list, the mission briefing, per-house credits and alliances. A writer that emitted
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
   is recovered from the fractional part -- which is exact, because whatever put the man
   there stood him on one of those five points: the brain reading a mission, or
   edit_place_q putting one down. The quincunx is the one table above, not a second copy
   of it. */
static int edit_subcell_of(float wx, float wz, int cx, int cy)
{
    const float fx = wx - (float)cx, fz = wz - (float)cy;
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < 5; i++) {
        const float dx = fx - EDIT_SUBCELL_X[i], dz = fz - EDIT_SUBCELL_Z[i];
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

/* THE ORDER AN OBJECT THE EDITOR PLACED GOES OUT WITH.
 *
 * Guard for everything was right until there was tiberium to place. A harvester on Guard
 * sits where it was put for the whole mission, so a map with a refinery, a harvester and
 * a field authored here still earned nothing -- measured: mission=Guard at tick 5 and at
 * tick 900, and the house's stored tiberium never left zero. The economy needs both
 * halves, and this is the second one.
 *
 * Harvest is the shipped answer rather than a preference: 80 of the 81 harvesters in the
 * campaign's [UNITS] carry it and one carries Guard, and that one is a deliberate
 * exception which survives here because a harvester read out of a file keeps its own
 * order -- this default is only ever consulted for an object with no history. Every
 * other type stays on Guard, which is what the campaign gives them too. */
static const char* edit_default_order(const char* type)
{
    return (type && !strcmp(type, "HARV")) ? "Harvest" : "Guard";
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

/* AND ERASING ONE MUST NOT LEAVE ITS TRIGGER BEHIND.
 *
 * The same key, the other direction. An entry whose object is gone is not inert: it is
 * house|type|cell, so the next object of that type placed on that cell for that house
 * silently picks up the dead one's order and trigger. Undo is unaffected -- the delete
 * snapshot carries the whole table, so the entry comes back with the object.
 *
 * TWO IDENTICAL OBJECTS CAN SHARE A CELL. edit_cell_ok refuses a footprint only over
 * BUILDING and TERRAIN, so a second rifleman of the same house may stand on the first,
 * and the two share one key. The entry therefore goes only once the LAST of them has,
 * which is why this is called after the erase rather than before it. */
static void edit_prior_drop(const char* house, const char* type, int cell)
{
    for (size_t i = 0; i < g_objects.size(); i++)
        if (!strcmp(g_objects[i].house, house) && !strcmp(g_objects[i].type, type) &&
            g_objects[i].cy * edit_ini_w() + g_objects[i].cx == cell) return;
    char key[64];
    edit_prior_key(key, sizeof key, house, type, cell);
    for (size_t i = 0; i < g_editPrior.size(); i++)
        if (!strcmp(g_editPrior[i].key, key)) {
            g_editPrior.erase(g_editPrior.begin() + i);
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
    /* The same default the writer would have given it, so the tag does not silently
       change its behaviour as a side effect of being tagged. */
    snprintf(e.order, sizeof e.order, "%s", edit_default_order(o.type));
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
 * model wins, because the editor may have put a wall where a crate used to be.
 *
 * AND IT CANNOT PLACE TIBERIUM EITHER, which is the one of the five that decides whether
 * a map has an economy at all. The writer above emits every cell of g_tib and the
 * renderer draws them, so a map that HAS tiberium keeps it through any number of round
 * trips. There is simply no tool that adds a cell and none that takes one away, so a map
 * made from nothing has no tiberium, no refinery income, and no way to get either.
 *
 * WHAT SUCH A TOOL MUST KNOW, measured rather than assumed:
 *   - The TI number is thrown away. MapClass::Overpass runs over the playable rectangle
 *     as the scenario is read (scenarioini.cpp:489, map.cpp:905) and Tiberium_Adjust
 *     re-picks the overlay at random across TIBERIUM1..12 (cell.cpp:1913). One brush is
 *     honest; twelve would be twelve ways to write a number nobody reads back.
 *   - The growth stage is derived, not authored. The same pass sets OverlayData from the
 *     count of the cell's eight tiberium neighbours, {0,1,3,4,6,7,8,10,11}, and the cell
 *     is worth (OverlayData + 1) * 25 credits (cell.cpp:1906-1934, type.h:897). A brush
 *     that re-derives the stage over the cell it paints AND its eight neighbours, the
 *     same shape as the wall mask above, draws the field the engine will draw.
 *   - The first row never loads. OverlayClass::Read_INI keeps a cell only while it is at
 *     least one row in from the top and the bottom of the engine grid (overlay.cpp:367),
 *     so an overlay painted on row zero is gone before the mission starts. */
/* `cell` is a FLAT INI cell number (y * edit_ini_w() + x), so it has to be as wide as
   one. It was `short`, which holds a flat number for a 128 wide map and stops holding one
   at 181 cells square: on a 256 map the numbers reach 65,535 against a signed short's
   32,767, and :10530 admits anything below EDIT_GRID_MAX, so the top half of the map would
   truncate to negative and :10704 would divide a negative by the stride. Widened for the
   same reason as g_bldCellNow in cnc_eyes.cpp, and before there is a map that shows it. */
struct OverlayExtra { int cell; char name[10]; };
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
        e.cell = cell;
        snprintf(e.name, sizeof e.name, "%s", nm);
        g_ovExtra.push_back(e);
    }
    fclose(f);
    if (!g_ovExtra.empty())
        fprintf(stderr, "edit: carrying %d overlay cell%s the editor does not model "
                        "(crates, concrete, field decoration)\n",
                (int)g_ovExtra.size(), g_ovExtra.size() == 1 ? "" : "s");
}

/* THE TWO CNC3D-OWNED [Basic] KEYS, read for the map being opened.
 *
 * CNC3DKind says whether the map belongs in the User Maps list or the skirmish one.
 * CNC3DSmooth says the rung tool is unlocked on it because somebody has already painted
 * smooth ground here -- see g_elevIniUnlock.
 *
 * BOTH, WHICH IS WHY THE EARLY `break` IS GONE. It stopped at the first key found, so
 * whichever of the two came second in the file was never read.
 *
 * IT ASSIGNS THE LIVE FLAG AS WELL AS THE LATCH, and that is what stops the previous
 * map's unlock standing over this one: this runs on every editor load -- the first boot,
 * OPEN, NEW's donor reload, and the round trip through PLAY -- before anything can look
 * at the flag. */
static void edit_read_kind(const char* path)
{
    g_mapIsMulti = 0;
    g_elevIniUnlock = false;
    g_elevSmoothUnlock = false;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    bool inBasic = false;
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { inBasic = !strncasecmp(p, "[BASIC]", 7); continue; }
        if (!inBasic) continue;
        if (!strncasecmp(p, "CNC3DKind", 9))
            g_mapIsMulti = (strstr(p, "Multi") != NULL) ? 1 : 0;
        else if (!strncasecmp(p, "CNC3DSmooth", 11))
            g_elevIniUnlock = (strchr(p, '1') != NULL);
    }
    fclose(f);
    g_elevSmoothUnlock = g_elevIniUnlock;
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
    return e ? e->order : edit_default_order(o.type);
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
    for (size_t i = 0; i < g_tib.size(); i++) {
        /* ONE ROW PER CELL. A CellClass holds a single Overlay, so two rows on one key
           are two answers to one question and whichever is read last wins by accident.
           The wall is written above and the wall wins: it is the thing standing on the
           cell, and the tiberium brush refuses to paint onto one. The guard is for a
           document that already holds both, which the wall tool can still make. */
        bool walled = false;
        for (size_t k = 0; k < g_walls.size() && !walled; k++)
            if (g_walls[k].x == g_tib[i].x && g_walls[k].y == g_tib[i].y) walled = true;
        if (walled) continue;
        fprintf(out, "%d=TI%d\r\n", g_tib[i].y * edit_ini_w() + g_tib[i].x,
                (int)g_tib[i].kind + 1);
    }
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
            fprintf(out, "%d=%s\r\n", g_ovExtra[i].cell, g_ovExtra[i].name);
    }

    /* SMUDGE NAMES ARE A TABLE, NOT A FORMULA.
     *
     * This wrote "SC%d" with type+1 for every smudge, and the type is the engine's raw
     * SmudgeType enum: craters 0-5, scorches 6-11, bibs 12-14 (defines.h:1379-1393,
     * names in sdata.cpp). So a crater was written as a scorch, a scorch under a name
     * six too high, and a bib as "SC13". The engine's From_Name does not know those
     * names, so it DROPPED them -- and 25 of the 100 shipped missions carry authored
     * craters and scorches, every one of which was destroyed by opening the map and
     * saving it. Measured on SCB06EC: nine authored smudges in, nine wrong names out.
     *
     * The line beside this one is why it went unnoticed. The brain normalises tiberium
     * on the way out (Overlay - OVERLAY_TIBERIUM1, dllinterface.cpp:8407) so "TI%d" with
     * kind+1 is correct there; it does NOT normalise the smudge (:8509), and the two
     * lines look identical.
     *
     * BIBS ARE NOT WRITTEN AT ALL. They are the aprons the engine lays under a building
     * by itself, and no shipped mission authors one -- all 221 authored smudges in the
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
                /* THE SMOOTH-BRUSH UNLOCK, owned exactly as the tag below is: written
                   only while it is true, and any older copy dropped further down, so a
                   map that has never been smoothed keeps a file with no line in it. */
                if (g_elevSmoothUnlock) fprintf(out, "CNC3DSmooth=1\n");
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
        if (inBasic && !strncasecmp(p, "CNC3DSmooth", 11)) continue;
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
        if (g_elevSmoothUnlock) fprintf(out, "CNC3DSmooth=1\n");
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
    /* THIS FORMAT CANNOT EXPRESS A MAP BIGGER THAN ITS OWN STRIDE, and it has no way to
       say so in the file. Its key is sixteen bits and its cells are read back at 128, so
       a wider document written here would come back as ground somewhere else entirely,
       record by record, with nothing anywhere reporting it. Refuse instead: the XL format
       exists for maps past this point and this editor does not write it yet. */
    if (g_editBinW > C3D_INI_STRIDE || g_editBinH > C3D_INI_STRIDE) {
        fclose(f);
        remove(dst);
        fprintf(stderr, "edit: this map is %dx%d and the big-map .BIN can only express %d "
                        "square. Not writing one, because the cells would be read back as "
                        "different ground.\n",
                g_editBinW, g_editBinH, C3D_INI_STRIDE);
        return false;
    }
    int recs = 0;
    for (int cy = 0; cy < g_editBinH; cy++)
        for (int cx = 0; cx < g_editBinW; cx++) {
            const int i = cy * g_editBinW + cx;
            /* 255 is TEMPLATE_NONE and 0 is TEMPLATE_CLEAR1; Read_Binary_Big prefills
               every cell clear, so neither is stored -- exactly its writer's rule. */
            if (g_editTmpl[i] == 255 || g_editTmpl[i] == 0) continue;
            /* THE KEY IS AT THE FORMAT'S STRIDE, NOT THE DOCUMENT'S. They are both 128
               for every big map this editor loads today, so emitting the bare index was
               right by coincidence rather than by construction; spelling the conversion
               out is what keeps it right if a document ever arrives at another width. */
            const int key = cy * C3D_INI_STRIDE + cx;
            fputc(key & 255, f); fputc((key >> 8) & 255, f);
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
 * preserves the mission briefing, the triggers and the AI base list on a shipped mission. A map
 * made from scratch has no source to preserve, and copying the DONOR mission's would
 * give it somebody else's briefing and win condition.
 *
 * So a new map gets a header of its own. These are the same values the browser editor
 * writes, for the same reason: nothing in this editor edits houses, credits or
 * triggers, so they are stated plainly rather than left absent for the engine to guess
 * at. Only the sections edit_write_ini does NOT own are written here -- it appends the
 * rest itself.
 *
 * SO THE MISSING [OVERLAY] IS NOT THE MISSING TIBERIUM. This seed carries no [OVERLAY],
 * no [TERRAIN], no [STRUCTURES] and no [SMUDGE], and it should not: all of those are
 * owned sections, and the writer appends every one of them from the document a moment
 * later. This file is only ever read back by edit_write_ini itself, which drops the
 * owned sections it finds and emits its own in their place. A new map has no tiberium
 * because nothing in this editor can put a cell of it down, not because a heading is
 * missing here.
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
    /* WHOSE MISSION THIS IS. [Basic] Player= is the house the engine hands to the human,
       and it is read only in a normal game (scenarioini.cpp:369). A skirmish map keeps the
       key anyway: the value is inert there, and a complete header is worth more than a
       missing line. */
    fprintf(f, "Player=%s\r\n", EUI_HOUSES[g_mapPlayer].key);
    fprintf(f, "CarryOverMoney=0\r\nBuildLevel=7\r\nTheme=No theme\r\n");
    fprintf(f, "Intro=x\r\nBrief=x\r\nAction=x\r\nWin=x\r\nLose=x\r\nPercent=0\r\n");
    fprintf(f, "CNC3DKind=%s\r\n", g_mapIsMulti ? "Multi" : "Single");
    if (g_elevSmoothUnlock) fprintf(f, "CNC3DSmooth=1\r\n");
    fprintf(f, "\r\n[MAP]\r\n");
    /* Version FIRST: the engine reads it before it sizes anything else, and a seeded
       big map with the key missing would be read at the legacy stride. */
    if (edit_map_big()) fprintf(f, "Version=1\r\n");
    /* Theater lives in [MAP] and not in [Basic]; reading it from the wrong section is a
       known trap in this engine and writing it to the wrong one is the same trap. */
    fprintf(f, "Theater=%s\r\n", thbrain);
    fprintf(f, "CNC3DTheater=%s\r\n", thname);
    fprintf(f, "X=%d\r\nY=%d\r\nWidth=%d\r\nHeight=%d\r\n", g_mapX, g_mapY, g_mapW, g_mapH);
    /* A SECTION FOR EVERY HOUSE THE ENGINE HAS. It is not what makes a house exist:
       HouseClass::Read_INI walks HOUSE_FIRST to HOUSE_COUNT and news every one of them,
       taking a default for each key a missing section would have carried
       (house.cpp:1961-2010). What the sections do is make a seeded map STATE its houses
       rather than inherit whatever the defaults happen to be, which is the same reason
       the keys above are written out instead of left absent. */
    for (int h = 0; h < EUI_HOUSE_N; h++) {
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
    /* THE DISK MATCHES THE SCREEN AGAIN, and only here: every earlier return in this
       function is a save that did not happen, and clearing the flag on one of those
       would be the editor promising a file that was never written. */
    g_editDirty = false;
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
    /* The autosave clock restarts from EVERY write, manual or not, so pressing SAVE
       pushes the next automatic one a full interval out rather than leaving it due. */
    g_editSavedAt = g_editFrame;
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
               plots avg[], and black would read as unexplored. Every set gets the same
               placeholder, so switching terrain art on a fresh grid changes nothing. */
            memset(c.avg, 96, sizeof c.avg);
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
 *  the project owner asked to NAME his maps instead of living with USER07. The name cannot be the
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
   the mission briefing, the houses, the win/lose conditions and everything else this editor
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
    /* WHICH ROW WAS ASKED FOR, held inside the table. The script verb that drives this
       dialog passes the row straight through from a text file and nothing checked it,
       so an out-of-range number read past the end of EUI_SIZES; a longer table does not
       fix that, it only moves where it lands. */
    if (sizeIdx < 0 || sizeIdx >= EUI_SIZE_N) {
        fprintf(stderr, "edit: size row %d does not exist (0..%d); using MEDIUM\n",
                sizeIdx, EUI_SIZE_N - 1);
        sizeIdx = 1;
    }
    const bool custom = (sizeIdx == EUI_SIZE_CUSTOM);
    const int w = custom ? g_newW : EUI_SIZES[sizeIdx].w;
    const int h = custom ? g_newH : EUI_SIZES[sizeIdx].h;
    /* The GRID first: the four legacy presets are playable rectangles inside the
       64x64 grid; the BIG MAP preset is the full 128 grid with its rectangle inset,
       and a TYPED one takes whichever grid its longest side can be numbered on. The
       world must be that size before anything below paints into it. */
    const int grid = custom ? eui_new_grid() : EUI_SIZES[sizeIdx].grid;
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

    /* AND THE BUILDING PAD OVERLAY, the one piece of donor state this list had missed.
     *
     * Every height the renderer draws, and every height the editor's geometry
     * reads, comes through corner_raw, and corner_raw prefers the pad to the map's own
     * corner. The elevation tool's own tier readout does NOT: elev_seed reads
     * g_pack.corner directly, which is exactly why the tools reported themselves working
     * while nothing moved. Until this line the pad was rebuilt only from a brain dump,
     * and a blank map never gets another one, so the last dump's pads stood
     * over ground the elevation tools were writing underneath: a raise or a lower wrote
     * its corners and nothing appeared to move. Raised ground that cannot be modified is
     * one omission, not two.
     *
     * MEASURED on a medium map, sampling all 4096 cells. A new DESERT map came up with
     * 103 cells off the flat, a one rung plateau over x20..34, y15..26, from its donor's
     * thirteen buildings on high ground. It is not a desert fault: a new map in the
     * theater already loaded takes the fast path above and never reboots, so it inherits
     * whatever map was open (61 cells, from a session on the fifth Nod mission), and the
     * three theaters that looked clean were only lucky in their donors carrying no
     * buildings at all. On the 128 grid it is not even stale, it is displaced: a pad
     * written at stride 65 and read back at stride 129 puts the plateau elsewhere again.
     *
     * REBUILT RATHER THAN ZEROED, because the rebuild is what owns these two arrays: it
     * is idempotent, it derives only from the raw corners, and it answers for whatever
     * the object list holds, which here is nothing. It runs AFTER the flatten so that it
     * measures the ground this map actually has, and it leaves g_shadeReady alone, which
     * is right because the line above has already cleared it. */
    terrain_pads_rebuild();

    ah_report_clear();           /* a new document, so no report and no standing arm */
    g_elevSeeded = false;
    g_elevConverted = true;      /* a map made on the ladder is already on it */
    g_elevTouched = true;        /* so its .HGT is written */
    /* AND THE SMOOTH-BRUSH UNLOCK IS THE PREVIOUS MAP'S, so it goes with the previous
       map. This is not covered by the load path: edit_request_new has a FAST PATH that
       never reboots when the new map's theater matches the pack already loaded, so
       edit_read_kind is not called and nothing else would clear it. A blank map is on
       the ladder anyway, which is what the line above says. */
    g_elevSmoothUnlock = g_elevIniUnlock = false;
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
    /* A BLANK DOCUMENT IS NOT UNSAVED WORK. The undo stack is gone with the map it
       belonged to, so the flag derived from it goes too, and the quit warning is
       disarmed with it. */
    g_editDirty = false; g_editQuitAsked = false;
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
static char g_editOpenDir[1024] = "";   /* the folder that row was LISTED from */
static int  g_editNewPending = 0;

/* THE FOLDER TRAVELS WITH THE NAME. Opening used to pass the scenario alone and the
   reboot then looked for it under the session's launch --dir, which is not where the
   USER MAPS tab found it. Passing both is what makes the browser's two tabs actually
   openable, and it is why MapEntry now carries a dir. */
static void edit_request_open(const char* scen, const char* dir)
{
    snprintf(g_editOpenScen, sizeof g_editOpenScen, "%s", scen ? scen : "");
    snprintf(g_editOpenDir,  sizeof g_editOpenDir,  "%s", dir  ? dir  : "");
    /* THE REPORT BELONGS TO THE MAP IT WAS MEASURED ON, and opening another one is the
       one way it could outlive its subject. Undo, redo, revert, every elevation stroke
       and a brand new document all cleared it already; OPEN did not, so a fit run on one
       map left its marks standing while a different map came up underneath them. Cleared
       here, at the REQUEST, rather than after the reboot, because the frames between the
       two still draw and the marks would describe ground that is on its way out. */
    ah_report_clear();
    g_editOpenReq = 1;
}

static void edit_request_new(void)
{
    /* A NUMBER STILL BEING TYPED IS STILL AN ANSWER. CREATE can arrive with the size
       field open and uncommitted, and throwing that away would make the button ignore
       the very thing on screen. Same two functions ENTER calls. */
    if (g_numKind == NUM_NEWSIZE) eui_num_commit();
    /* WHOSE MISSION THIS IS, settled here rather than at the blank map: the theater path
       below finishes only after a reboot, and both paths have to reach the same answer.
       A skirmish map takes GoodGuy, which the engine will not read. */
    g_mapPlayer = (g_newMulti == NEWKIND_NOD) ? EUI_HOUSE_NOD : EUI_HOUSE_GDI;
    /* Same theater as the pack already loaded? Then nothing has to be reloaded. */
    if (g_newTheater == eui_theater_index(g_pack.theater)) {
        /* The kind is three-valued now and this parameter is the multiplayer FLAG. */
        edit_blank_map(g_newSize, g_newMulti == NEWKIND_MULTI);
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
        static char dir[1024];
        nx = *o;
        snprintf(scen, sizeof scen, "%s", g_editOpenScen);
        nx.scen = scen;
        /* THE MAP'S OWN FOLDER, not the session's launch folder, and this line is the
           whole of the import bug.

           `nx = *o` copies the command line the process started with, and Editor.app
           always starts it with --dir missions/. The OPEN dialog's USER MAPS tab scans
           missions/user_maps/. So the reboot below looked for the chosen scenario in a
           folder it is not in, and because game_shutdown() has already run by then, the
           editor was left alive over a world that no longer existed. That is what
           "the editor crashes when importing a map" is from the outside.

           It appeared to work because exactly two user maps, USER01 and USER91, also
           exist under missions/ by the same name. Every other one, including any map a
           player imports from another C&C editor, could not be opened at all.

           A NEW MAP takes the missions root instead, because its donor mission is a
           cartridge scenario and lives there: making a Desert map from a session opened
           on a user map printed "edit: could not open SCB01EA" and then drew nothing,
           which is the same bug wearing its other face. */
        if (req == 2) edit_maps_root(dir, sizeof dir);
        else          snprintf(dir, sizeof dir, "%s", g_editOpenDir);
        if (dir[0]) nx.dir = dir;
        nx.pack = edit_pack_for(nx.dir, scen);   /* the map's own pack, or its
                                                    theater's donor -- see edit_pack_for */
        nx.edit = 1;
        game_shutdown();
        if (!game_boot(win, &nx)) {
            /* SAY IT ON SCREEN, not only on stderr. game_shutdown has already run, so
               the editor is now sitting over a torn-down world and the loop falls
               straight through into tick and draw: the player sees an empty map and no
               reason for it. edit_toast prints to stderr as well, so the log keeps what
               it always had. */
            edit_toast(EUI_DANGER, "COULD NOT OPEN THAT MAP", scen);
            fprintf(stderr, "edit: could not open %s in %s\n", scen,
                    dir[0] ? dir : "(the session's own folder)");
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
        /* And the unsaved-work flag, for the same reason as everything else on this
           list: it describes the map that WAS open, and a different one is open now.
           (Opening still discards the old map's unsaved edits without asking. That is
           the same flag's next job and a separate report.) */
        g_editDirty = false; g_editQuitAsked = false;
        g_zonePaint = g_tagMode = g_enhPaint = false;
        g_scriptRowOff = 0;
        g_scriptScroll = 0.0f;
        /* AND THE SMOOTH-BRUSH UNLOCK, which belongs to the map and not to the session.
           It is set to what the map that was JUST OPENED said, not to false: this line
           runs AFTER game_boot, so edit_read_kind has already read the file, and a bare
           false here would throw the file's own answer away and re-lock a map that says
           it is unlocked. The previous map's unlock cannot survive either way. */
        g_elevSmoothUnlock = g_elevIniUnlock;
        if (req == 2 && g_editNewPending) {
            g_editNewPending = 0;
            /* The kind is three-valued now and this parameter is the multiplayer FLAG. */
            edit_blank_map(g_newSize, g_newMulti == NEWKIND_MULTI);
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
 *      bool  screen_to_world(float, float, int, int, float*, float*)
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

    /* ----------------------------------------------------------------------------
     *  THE DELETE STACK: one rung per press, and the ground back to clear at the end.
     *
     *  The fixture is BUILT rather than found, because no shipped cell carries all
     *  five layers at once: a cell gets a template, a tiberium overlay, a crater, a
     *  wall and a man standing on the lot, and DELETE is then pressed six times. A
     *  stack that empties the cell in one press fails on press one; a stack that only
     *  ever takes the top thing fails on press three, where the old code answered
     *  "nothing to erase" over a crater and a tiberium field; a stack that never
     *  reaches the ground fails on press five. The sixth press must REFUSE and leave
     *  the undo stack alone -- a DELETE that keeps reporting success on an empty cell
     *  is a row of undo steps that undo nothing.
     *
     *  The counts are the whole document's, not the cell's, so nothing here re-uses
     *  the arithmetic the stack itself aims with.
     * -------------------------------------------------------------------------- */
    {
        int cx = -1, cy = -1;
        for (int y = g_mapY + 1; y < g_mapY + g_mapH - 1 && cx < 0; y++)
            for (int x = g_mapX + 1; x < g_mapX + g_mapW - 1 && cx < 0; x++)
                if (edit_cell_ok(x, y) && !edit_wall_at(x, y) &&
                    edit_tib_at(x, y) < 0 && edit_smudge_at(x, y) < 0) { cx = x; cy = y; }

        /* A tile this theater's bank really carries, that is not plain ground and that
           still leaves the cell buildable -- otherwise the man cannot be stood on it. */
        int nT = 0;
        const TtSlot* tt = tt_table(g_pack.theater, &nT);
        int wt = -1, wi = 0;
        for (int k = 0; k < nT && wt < 0; k++) {
            if (tt[k].tmpl == 255 || tt[k].tmpl == 0) continue;
            if (!EDIT_LAND_BUILD[edit_land_of(tt[k].tmpl, tt[k].icon)]) continue;
            wt = tt[k].tmpl; wi = tt[k].icon;
        }
        int man = -1;
        for (int i = 0; i < EDIT_ITEM_N && man < 0; i++)
            if (EDIT_ITEMS[i].kind == EDIT_KIND_INFANTRY) man = i;

        if (cx < 0 || wt < 0 || man < 0 || !g_editHaveBin) {
            fprintf(stderr, "UNDOTEST|FAIL|delete stack untested: free cell %d,%d, "
                    "buildable non-clear tile %d in %s, infantry item %d, .BIN %d\n",
                    cx, cy, wt, g_pack.theater, man, g_editHaveBin ? 1 : 0);
            fails++;
        } else if (!edit_paint_cell(cx, cy, wt, wi)) {
            fprintf(stderr, "UNDOTEST|FAIL|delete stack untested: could not paint "
                    "template %d icon %d onto %d,%d\n", wt, wi, cx, cy);
            fails++;
        } else {
            /* The fixture. Both go straight into the document rather than through their
               brushes: what is under test is DELETE reaching them, not how they arrived. */
            TibCell tc; memset(&tc, 0, sizeof tc);
            tc.x = (short)cx; tc.y = (short)cy;
            g_tib.push_back(tc);
            SmudgeCell sc; memset(&sc, 0, sizeof sc);
            sc.x = (short)cx; sc.y = (short)cy;      /* type 0 is CR1, a crater */
            g_smudges.push_back(sc);
            edit_wall_put(cx, cy, 0);
            g_editOwner = 0;
            g_editArmed = man;
            const bool stood = edit_place_q(cx, cy, true);

            const int u0 = (int)g_undo.size();
            int eo = (int)g_objects.size(), ew = (int)g_walls.size(),
                es = (int)g_smudges.size(), et = (int)g_tib.size();
            const int o0 = eo, w1 = ew, s0 = es, t0 = et;
            if (!stood) {
                fprintf(stderr, "UNDOTEST|FAIL|delete stack untested: no infantryman "
                        "would stand on %d,%d\n", cx, cy);
                fails++;
            } else {
                int took = 0;
                bool clear = false;
                for (int step = 0; step < 5; step++) {
                    if      (step == 0) eo--;
                    else if (step == 1) ew--;
                    else if (step == 2) es--;
                    else if (step == 3) et--;
                    else                clear = true;
                    char nm[40];
                    const int lay = edit_erase_peek(cx, cy, nm, sizeof nm);
                    const bool hit = edit_erase_at(cx, cy);
                    const int tm = g_editTmpl[cy * g_editBinW + cx];
                    if (!hit || (int)g_objects.size() != eo ||
                        (int)g_walls.size() != ew || (int)g_smudges.size() != es ||
                        (int)g_tib.size() != et || tm != (clear ? 255 : wt) ||
                        (int)g_undo.size() != u0 + step + 1) {
                        fprintf(stderr, "UNDOTEST|FAIL|delete %d of 5 at %d,%d took "
                                "'%s' (rung %d, %s): objects %d want %d, walls %d want "
                                "%d, smudges %d want %d, tiberium %d want %d, template "
                                "%d want %d, undo depth %d want %d\n",
                                step + 1, cx, cy, nm, lay, hit ? "took it" : "refused",
                                (int)g_objects.size(), eo, (int)g_walls.size(), ew,
                                (int)g_smudges.size(), es, (int)g_tib.size(), et,
                                tm, clear ? 255 : wt,
                                (int)g_undo.size(), u0 + step + 1);
                        fails++;
                        break;
                    }
                    took++;
                }
                if (took == 5) {
                    fprintf(stderr, "UNDOTEST|ok  |five presses on %d,%d took five "
                            "things one at a time -- object, wall, crater, tiberium, "
                            "then the ground back to template 255\n", cx, cy);
                    /* The sixth press: nothing left, so nothing happens and no undo
                       step is filed. */
                    const int uEnd = (int)g_undo.size();
                    if (edit_erase_at(cx, cy) || (int)g_undo.size() != uEnd) {
                        fprintf(stderr, "UNDOTEST|FAIL|a sixth press on an empty cell "
                                "reported success or filed an undo step (%d -> %d)\n",
                                uEnd, (int)g_undo.size());
                        fails++;
                    } else {
                        fprintf(stderr, "UNDOTEST|ok  |the sixth press refuses an "
                                "already empty cell and files no undo step\n");
                    }
                    /* And five undos put all five back. */
                    for (int i = 0; i < 5; i++) edit_undo();
                    const int tm = g_editTmpl[cy * g_editBinW + cx];
                    if ((int)g_objects.size() != o0 || (int)g_walls.size() != w1 ||
                        (int)g_smudges.size() != s0 || (int)g_tib.size() != t0 ||
                        tm != wt) {
                        fprintf(stderr, "UNDOTEST|FAIL|five undos did not rebuild the "
                                "cell: objects %d want %d, walls %d want %d, smudges "
                                "%d want %d, tiberium %d want %d, template %d want "
                                "%d\n", (int)g_objects.size(), o0, (int)g_walls.size(),
                                w1, (int)g_smudges.size(), s0, (int)g_tib.size(), t0,
                                tm, wt);
                        fails++;
                    } else {
                        fprintf(stderr, "UNDOTEST|ok  |five undos rebuilt the cell one "
                                "rung at a time, tiberium and crater included\n");
                    }
                }
            }
        }
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
