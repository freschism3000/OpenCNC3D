/* ====================================================================================
 *  grass_bake.h -- EVERYTHING THE GRASS DOES ONCE, AT MAP LOAD.
 *
 *  This file reads the art, decides where grass grows, generates every blade in the
 *  map and puts the whole field into ONE static vertex buffer that is never rewritten.
 *  It draws nothing. The draw path is a separate file and is declared, not defined,
 *  at the bottom of this one.
 *
 *  WHY A STATIC BUFFER AND NOT A PER-VIEW REBUILD. Measured on this context: a client
 *  array rebuild of the visible grass costs 5 ms at 268k vertices and 39 ms at 1.34M:
 *  0.86 and 0.55 GB/s at 16 bytes a vertex. The identical geometry drawn from a static
 *  VBO costs 0.07 ms and 0.30 ms. That is 50x to 129x, and it is a property of the bus
 *  rather than of the card, so it transfers to every machine. Everything that
 *  MOVES during a match (wind, the shroud, tread marks, the fade under a new building,
 *  the density dial, the distance LOD) is a uniform or a texture the vertex stage
 *  reads. Nothing that moves is in this buffer.
 *
 *  THE VERTEX, 16 BYTES, three interleaved arrays. Verified with a compile probe:
 *  sizeof 16, offsets 0 / 6 / 12, no padding anywhere.
 *
 *      offset  type            name          meaning
 *       0      GLshort x3      px, py, pz    THE BLADE'S ROOT in world cells scaled by
 *                                            g_grassPosScale. IDENTICAL ON ALL FOUR
 *                                            CORNERS of the blade: the card must fade,
 *                                            sample the ground colour and read the wind
 *                                            as ONE thing, so nothing may vary a
 *                                            lookup between a blade's root and its tip.
 *       6      GLshort         side          -32767 left, +32767 right
 *       8      GLshort         b2t           0 at the root, 32767 at the tip
 *      10      GLshort         sd            distance from the root to the nearest
 *                                            silhouette edge (water, absent cell, the
 *                                            play rectangle's border), in 1/256 cell,
 *                                            saturating at g_grassSdRange, which
 *                                            grass_sd_range_of derives from the height
 *                                            dial's range ceiling and the uniform table
 *                                            below exports as uSdRange. On the default
 *                                            dials that is 1.216 cells, so 311 units.
 *      12      GLubyte         hgt           per blade height and width variation
 *      13      GLubyte         yaw           the card's bearing, 0..255 = 0..2pi
 *      14      GLubyte         phase         the blade's own offset into the wind cycle
 *      15      GLubyte         slot          the thinning slot, (s*255+63)/127, so
 *                                            gl_Color.a IS s/127 and the draw's
 *                                            uSlotCut = sc/127 compares against it
 *                                            directly
 *
 *      glVertexPointer  (3, GL_SHORT,         16, base +  0)
 *      glTexCoordPointer(3, GL_SHORT,         16, base +  6)
 *      glColorPointer   (4, GL_UNSIGNED_BYTE, 16, base + 12)
 *
 *  The type rules are not preference. glTexCoordPointer REFUSES GL_UNSIGNED_BYTE with
 *  GL_INVALID_ENUM and draws nothing; glColorPointer is the only array that takes
 *  bytes, and it NORMALISES, so gl_Color arrives as 0..1. GL_SHORT is legal on both
 *  and does NOT normalise, so the vertex shader scales by the constants exported
 *  below. This is the same smuggling TREE_VS uses to get four per-vertex extras
 *  through gl_Color (cnc_eyes.cpp:7705), for the same reason: fx_gl.h resolves no
 *  glVertexAttribPointer and no glBindAttribLocation, so the fixed-function arrays
 *  are the whole of what is available.
 *
 *  THE TOTALS, at the default bake ceiling of 128 slots a cell, 64 bytes a blade. The
 *  bake below was driven over the shipped packs off disk and these are the bytes it
 *  handed to glBufferData, but MIND THE RECTANGLE: the run supplied one fixed 62x62 play
 *  rectangle to every map instead of reading each map's own g_mapX..g_mapH
 *  (cnc_eyes.cpp:1123), so every row below covers 3,844 cells while its map covers fewer.
 *  SCG03EA's rect is 45x37, SCB31EA's is 60x60, SCB61EA's is 52x58. No row is therefore
 *  that map's own figure, and the first is out by more than a factor of two: 45x37 cells
 *  at 128 slots cannot exceed 213,120 blades = 13.01 MiB however green the art is. Read
 *  them as the cost of a full 62x62 field, which is what they measure.
 *
 *      A SUPPLIED 62x62 PLAY RECTANGLE inside a 64x64 grid
 *         SCG03EA, the densest sampled    452,663 blades   28,970,432 B   27.63 MiB
 *         SCB31EA                         322,957 blades   20,669,248 B   19.71 MiB
 *         SCB61EA, the thinnest sampled   268,277 blades   17,169,728 B   16.37 MiB
 *         256 tiles either way: 65,536 B of cumulative tables and 70,656 B of tile
 *         records, because tiles cover the GRID and the ones outside the rectangle
 *         simply hold no blades. 294,912 B of CPU mask, 16,384 B of per cell colour.
 *      The whole 64x64 grid as the rectangle, for scale: 484,295 / 354,873 / 291,063
 *      blades, 29.56 / 21.66 / 17.77 MiB. The arithmetic ceiling, every cell grass,
 *      is 524,288 blades and 33,554,432 B = 32.00 MiB.
 *
 *      256x256 GRID, 65536 cells, measured on USER50.pack with both gates lifted
 *         8,057,176 blades  515,659,264 B  491.77 MiB
 *         4096 tiles: 1,048,576 B of cumulative tables, 1,130,496 B of tile records,
 *         4,718,592 B of CPU mask, 262,144 B of per cell colour.
 *         The arithmetic ceiling is 8,388,608 blades and 536,870,912 B = 512.00 MiB.
 *
 *      The shared index buffer is 24,576 B (2048 blades x 6 indices x 2 B) for ANY
 *      map size, because every tile has the same blade stride.
 *
 *  SO THE 256 MAP IS REFUSED, LOUDLY, and this file says so at load rather than
 *  allocating half a gigabyte behind the player's back. Two gates, each naming its own
 *  constant and the number that broke it, both fired on real files in the run folder:
 *      GRASS_GRID_MAX     128 cells a side. USER50.pack is 256x256 and is turned away
 *                         before a single texel is read.
 *      GRASS_VBO_BUDGET   48 MiB. A 128x128 map that gets this far is turned away after
 *                         the mask pass, with its blade count, its size and the density
 *                         ceiling that WOULD have fitted, all three printed by the
 *                         refusal itself rather than quoted here. What is arithmetic is
 *                         only the bound above them: 128x128 cells at 128 slots is
 *                         2,097,152 blades = 128.00 MiB, and a play rectangle inside that
 *                         grid always comes in under it. A map whose theater fails the
 *                         gate in the mask pass stops there instead and never reaches
 *                         this check, whatever its size.
 *  In both cases grass is the only thing that stops. The rest of the Enhanced chain is
 *  untouched, which is the same promise the buffer entry points make below.
 *
 *  DETERMINISM. Nothing here reads a clock. Every blade's position, height, bearing and
 *  wind phase is an integer hash of (cell x, cell y, slot), the shape tree3d_hash
 *  (tree3d_mod.h:277) and tib3d_hash (tib3d_mod.h:268) both use and for the reason both
 *  of them state. The bake is a pure function of the pack, the play rectangle, the
 *  drawn atlas and the configuration struct, so G36c's five identical --shot digests
 *  are satisfied by construction.
 *
 *  CLASSIC AND WIN98 ARE UNTOUCHED. Nothing in this file is called unless g_fxActive.
 *  It uses no shader entry point at all: glGetTexImage is GL 1.0 core, and the five
 *  buffer entry points are resolved into their OWN optional group with their own ready
 *  flag. A driver without vertex buffer objects loses grass and keeps bloom, shadows,
 *  water and the trees.
 * ==================================================================================== */

#ifndef CNC3D_GRASS_BAKE_H
#define CNC3D_GRASS_BAKE_H

/* Include AFTER water_mod.h (cnc_eyes.cpp:6979). By then everything this file reads is
   in scope: PackCell and Pack (cnc_eyes.cpp:998, :1022), terrain_y (:1188), cell_shown
   (:6477), terrain_atlas_index (:6642), g_gridW / g_gridH (:1137), the play rectangle
   g_mapX..g_mapH (:1123), and, through remaster_tex.h (:149), tt_table / tt_slot_rect
   (terrain_tiles.h:4462, :4498) and edit_land_of (edit_tables.h:277). No forward
   declaration is needed for any of them. */

/* ---- the shape of the field ------------------------------------------------------ */

/* 4x4 cells to a tile, and this number must not move.
   Not to save draw calls: 24 draws over one buffer measured 0.28 ms against 0.25 ms for
   180 draws, so draw call count is irrelevant here. It is the granularity of the cull,
   and it is what keeps the 16-bit index ceiling out of the density dial's way. 4 verts
   a blade means a tile can hold 65536/4 = 16384 blades, which over 16 cells is 1024
   blades a cell: eight times the ceiling below, so THE TILE SIZE NEVER CHANGES WHEN THE
   DIAL MOVES. An 8x8 tile would cap the dial at 256 and would have had to halve itself
   the first time somebody pushed the slider past it, which is a layout moving under the
   person tuning it. Same idiom as cnc_eyes.cpp:7494, where the tiberium batch says of
   itself that "the batch flushes before it could overrun them". */
#define GRASS_TILE   4

/* The number of thinning slots, which is also the bake ceiling in blades a cell.
   The buffer is sized by THIS and thinned to the live density dial for free, so
   raising it costs memory at load and nothing at all per frame. */
#define GRASS_SLOTS  128

/* Texels of terrain art to a cell. TT_TS is the tile art's own size (terrain_tiles.h:15)
   and the mask is read at exactly that rate, so it can never disagree with the picture
   by more than half a texel. The per cell rule and this per texel rule disagree on
   15.2% of a real map, and 14.5 of those 15.2 points are the grass verges along roads
   and cliffs, which are the most visible boundary in the frame. */
#define GRASS_MASK_RES TT_TS

/* The two refusals. See the header note. */
#ifndef GRASS_GRID_MAX
#define GRASS_GRID_MAX    128
#endif
#ifndef GRASS_VBO_BUDGET
#define GRASS_VBO_BUDGET  (48u * 1024u * 1024u)
#endif

/* The skirt's units and its bounds. How FAR the skirt reaches is not a constant: it is
   derived at bake time from the height dial's RANGE CEILING, because the widest band the
   live height can ever ask for is what the baked distance has to be able to express.
   See grass_sd_range_of. The two bounds here only keep the short and the search cost
   finite.

   GRASS_SD_MAX IS A CEILING ON COST, NOT A FREE ONE. The whole cell radius the range
   rounds up to is the R of two square searches: the near block flag walks (2R+1)
   squared cells for every cell of the map, and the per blade distance walks the same
   square with a square root in it, twice, because the counting pass runs the same
   placement as the writing pass. A bigger radius also flags more cells as near block,
   so the bill grows faster than the square. At the default range of 1.216 cells R is 2
   and 25 tests; at this ceiling it is 289. The bake stays correct either way, so
   grass_bake warns about a large radius rather than refusing. */
#define GRASS_SD_STEP     256.0f      /* sd units a cell */
#define GRASS_SD_MIN      0.25f
#define GRASS_SD_MAX      8.0f

/* What the draw's vertex shader must divide by. Exported rather than repeated, because
   a mismatch here is a silent geometry bug, not a compile error. */
#define GRASS_SIDE_ONE    32767.0f
#define GRASS_B2T_ONE     32767.0f

/* ---- the buffer entry points, IN THEIR OWN OPTIONAL GROUP ------------------------- */

/* fx_gl.h resolves 26 pointers and not one of them is a buffer call: a repo wide search
   for glGenBuffers, glBindBuffer, glBufferData, glBufferSubData and glDeleteBuffers
   over game/ finds nothing. So grass has to resolve its own, and it MUST NOT do it
   through fx_proc. fx_proc's FX_REQ path sets fx_gl_missing, and fx_gl.h:246 turns that
   into fx_gl_ready = 0, which takes down bloom, shadows, water and the tree pass with
   it (fx_gl.h:180-186 puts the decision on the resolve line for exactly this reason).
   Five new required pointers would mean a driver with no vertex buffer objects loses
   the whole Enhanced chain to buy grass it was never going to get. This group is
   separate, is resolved on first use, and reports its own failure. */

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER          0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW           0x88E4
#endif
#ifndef GL_BUFFER_SIZE
#define GL_BUFFER_SIZE          0x8764
#endif

typedef void (APIENTRY *GRPFNGENBUFFERS)(GLsizei, GLuint*);
typedef void (APIENTRY *GRPFNDELETEBUFFERS)(GLsizei, const GLuint*);
typedef void (APIENTRY *GRPFNBINDBUFFER)(GLenum, GLuint);
typedef void (APIENTRY *GRPFNBUFFERDATA)(GLenum, FxGLsizeiptr, const void*, GLenum);
typedef void (APIENTRY *GRPFNBUFFERSUBDATA)(GLenum, FxGLsizeiptr, FxGLsizeiptr, const void*);
typedef void (APIENTRY *GRPFNGETBUFFERPARAMETERIV)(GLenum, GLenum, GLint*);

static GRPFNGENBUFFERS     gr_glGenBuffers;
static GRPFNDELETEBUFFERS  gr_glDeleteBuffers;
static GRPFNBINDBUFFER     gr_glBindBuffer;
static GRPFNBUFFERDATA     gr_glBufferData;
static GRPFNBUFFERSUBDATA  gr_glBufferSubData;
static GRPFNGETBUFFERPARAMETERIV gr_glGetBufferParameteriv;

static int  g_grassBufReady = 0;
static int  g_grassBufTried = 0;
static char g_grassBufWhy[160];

static void* grass_proc(const char* core, const char* alt)
{
    /* Core name first, then the extension spelling, for the reason fx_gl.h:187-203
       gives: a driver that reports a core version below the promotion is entitled to
       export only the suffixed name. */
    void* p = SDL_GL_GetProcAddress(core);
    if (!p && alt) p = SDL_GL_GetProcAddress(alt);
    if (!p && g_grassBufWhy[0] == 0)
        snprintf(g_grassBufWhy, sizeof g_grassBufWhy,
                 "%s is not exported by this driver", core);
    return p;
}

static int grass_buf_load(void)
{
    if (g_grassBufTried) return g_grassBufReady;
    g_grassBufTried = 1;
    g_grassBufWhy[0] = 0;

    gr_glGenBuffers    = (GRPFNGENBUFFERS)    grass_proc("glGenBuffers", "glGenBuffersARB");
    gr_glDeleteBuffers = (GRPFNDELETEBUFFERS) grass_proc("glDeleteBuffers", "glDeleteBuffersARB");
    gr_glBindBuffer    = (GRPFNBINDBUFFER)    grass_proc("glBindBuffer", "glBindBufferARB");
    gr_glBufferData    = (GRPFNBUFFERDATA)    grass_proc("glBufferData", "glBufferDataARB");
    gr_glBufferSubData = (GRPFNBUFFERSUBDATA) grass_proc("glBufferSubData", "glBufferSubDataARB");
    /* The sixth pointer is required with the other five, not optional beside them: it
       arrived in the same core version and the same extension, so a driver that has the
       buffers has this too, and a store nobody can verify is worse than no grass. */
    gr_glGetBufferParameteriv = (GRPFNGETBUFFERPARAMETERIV)
                                grass_proc("glGetBufferParameteriv", "glGetBufferParameterivARB");

    g_grassBufReady = (gr_glGenBuffers && gr_glDeleteBuffers && gr_glBindBuffer
                       && gr_glBufferData && gr_glBufferSubData
                       && gr_glGetBufferParameteriv) ? 1 : 0;
    if (!g_grassBufReady) {
        if (g_grassBufWhy[0] == 0)
            snprintf(g_grassBufWhy, sizeof g_grassBufWhy, "vertex buffer objects are absent");
        fprintf(stderr, "GRASS|unavailable|%s; the rest of the Enhanced chain is unaffected\n",
                g_grassBufWhy);
    }
    return g_grassBufReady;
}

/* DID THE DRIVER ACTUALLY MAKE THE STORE? glBufferData is the only call in this file
   that can fail at runtime, and it carries the largest single allocation the program
   makes: up to GRASS_VBO_BUDGET, and 19.71 MiB on a measured 64x64 mission. A driver
   that cannot find the memory is entitled to leave the error queue empty and create a
   store of size ZERO, and the draw from that store is silent as well: no grass, no GL
   error, and a GRASS|vbo line reporting bytes that were computed on the CPU side and so
   claims an upload that never happened. That is the half loaded chain this file is
   arranged to prevent, and from the frame it is indistinguishable from a shader or a
   draw order bug. So the size is read BACK and compared with what was asked for, the way
   the render target helper compares the framebuffer status before trusting the target
   (fx_gl.h:395-405).

   BOTH TARGETS ARE UNBOUND ON THE FAILURE PATH, and that is the important half. Returning
   early from the middle of the upload skips the unbind at the end of it, and a bound
   ARRAY_BUFFER turns every later host vertex pointer into a byte offset, which silently
   erases the trees and the tiberium for the rest of the run. See the note there. The
   buffers themselves need no deleting here: grass_bake calls grass_free on any false
   return from the geometry build, and that deletes both. */
static bool grass_store_made(GLenum target, unsigned long want, const char* what)
{
    const GLenum err = glGetError();     /* whatever the upload left, the queue was drained */
    GLint got = 0;
    gr_glGetBufferParameteriv(target, GL_BUFFER_SIZE, &got);
    if (err == GL_NO_ERROR && got >= 0 && (unsigned long)got == want) return true;

    fprintf(stderr, "GRASS|refused|scen=%s the driver made %ld B of the %lu B %s store "
                    "(gl error 0x%04X). No grass this map; the rest of the Enhanced "
                    "chain is unaffected.\n",
            g_pack.scen, (long)got, want, what, (unsigned)err);
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return false;
}

/* ---- what the bake produces ------------------------------------------------------ */

struct GrassVert {
    GLshort px, py, pz;          /* the ROOT, identical on all four corners           */
    GLshort side, b2t, sd;       /* corner, and the skirt distance at the root        */
    GLubyte hgt, yaw, phase, slot;
};

/* One 4x4 cell tile. 276 bytes, measured.

   cum[] IS THE ANTI POP, and it is why the density dial and the distance LOD are the
   same mechanism. Blades are written SLOT MAJOR inside a tile (slot 0 of every cell in
   the tile, then slot 1 of every cell, and so on), so the first N blades of a tile are
   always a UNIFORM thinning of the whole tile rather than fully grassed cells beside
   bald ones. cum[i] is the number of blades in the tile whose slot is i or lower, so
   drawing cum[i] blades draws exactly the slots up to i, in every tile, uniform or not.

   The draw picks a FRACTIONAL slot cutoff sc in [0, GRASS_SLOTS-1], sets
       count    = cum[(int)sc]
       uSlotCut = sc / 127.0
   and the vertex's gl_Color.a is its own slot / 127. Count and fade then name the same
   number: the blades that are drawn are exactly those with slot <= floor(sc), and
   uSlotCut says how far through that last slot the cut has travelled, so the shader
   fades it out instead of dropping it. Getting this wrong is not cosmetic. A cutoff
   expressed as a fraction of the tile's ACTUAL blade count puts the fade twenty four
   slots above the highest blade drawn on a uniform tile, and the LOD then pops at every
   band boundary, which is the whole thing the table exists to prevent.

   cum is indexed 0..GRASS_SLOTS-1 and cum[GRASS_SLOTS-1] is the tile total, so full
   density is expressible. A table of "blades with slot strictly less than i" cannot say
   "all of them" without a 129th entry.

   y0 and y1 bracket the BLADES and not the roots. ylo and yhi are gathered from root
   heights, so the top of the bracket has the tallest tip the height dial's RANGE can
   grow added to it, height_ceiling * (1 + vary_ceiling), which is 0.435 cells on the
   defaults. Under a pitched camera a higher point projects higher on screen, the fact
   the shroud blanket's own lid rests on (game/shroud_mod.h:642-645), so a bracket taken
   from the roots alone reports a tile as below the frustum while its grass is in frame,
   and a cull built on it would drop four cells of grass at a time along that edge. */
struct GrassTile {
    unsigned short cx, cy;                   /* tile origin in CELLS                   */
    unsigned int   vert0;                    /* first vertex of this tile in the VBO   */
    unsigned int   blades;                   /* blades in this tile                    */
    float          y0, y1;                   /* world y bracket of its BLADES, for the cull */
    unsigned short cum[GRASS_SLOTS];
};

/* Everything the bake reads that is not the pack. Passed in rather than reached for,
   because the sync guard has to compare it: water_field_sync carries exactly this and
   for exactly this reason (water_mod.h:775 compares g_fx.water_current and
   g_fx.water_river_width, "which exists so the two dials the build reads are live").
   A build time value that is not in this struct is a dead slider in the panel. */
struct GrassBakeCfg {
    float theater_cut;    /* green excess a bank must clear to grow grass at all       */
    float colour_sd;      /* how many standard deviations below the CLEAR1 mean the cut sits */
    int   den_max;        /* bake ceiling, blades a cell, 1..GRASS_SLOTS               */
    float height_ceiling; /* the TOP OF THE HEIGHT DIAL'S RANGE, cells. Not its value. */
    float vary_ceiling;   /* the top of the height variation dial's range              */
    float pitch_min;      /* the SHALLOWEST pitch the camera can reach, radians        */
    float fovy_max;       /* the WIDEST vertical field of view it can reach, radians   */
};

/* The three build-time dials, READ FROM THE PANEL, and the four range bounds, which are
   not dials at all.

   This function used to hardcode the first three with a note saying they should be read
   from the panel once those rows existed. The rows exist, so they are read. A build time
   value the panel can move but this function does not read is the worst kind of dead
   control there is: it round trips through the preset file perfectly, the slider moves,
   the number is saved and restored, and nothing on screen ever changes. The sync guard
   below compares all seven, so moving any of them rebuilds the field rather than leaving
   a stale one behind.

   The two CEILINGS and the two CAMERA bounds stay constants, because they are the top or
   the bottom of a dial's RANGE and not the dial. Baking the skirt from the range ceiling
   rather than from the live height is what stops the height slider invalidating the
   whole buffer.

   grass_width is deliberately absent. The card is baked at unit half width and scaled
   in the vertex shader, so the width slider is live. A width read at bake time is a
   dead control that still round trips through the cfg file, which is the worst kind. */
static GrassBakeCfg grass_bake_cfg_default(void)
{
    GrassBakeCfg c;
    c.theater_cut    = g_fx.grass_theater_cut;
    c.colour_sd      = g_fx.grass_colour_sd;
    c.den_max        = (int)g_fx.grass_density_max;
    if (c.den_max < 1)            c.den_max = 1;
    if (c.den_max > GRASS_SLOTS)  c.den_max = GRASS_SLOTS;
    c.height_ceiling = 0.30f;
    c.vary_ceiling   = 0.45f;
    /* THE CAMERA PAIR ARE RANGE BOUNDS, exactly as the two ceilings above are. 0.78 rad
       is the console's tilt at the near zoom limit and the shallowest the shipped rig
       reaches; 50 degrees is guPerspectiveF's vertical field of view
       (cnc_eyes.cpp:5457-5459). A rig that can sit SHALLOWER, such as an isometric row
       whose tilt floors at half its own field of view plus two degrees, fills these from
       the BOTTOM of its tilt dial and the TOP of its field of view dial, so moving
       either inside its range never invalidates the buffer. */
    c.pitch_min      = 0.78f;
    c.fovy_max       = 50.0f * 0.01745329f;
    return c;
}

/* ---- the bake's state ------------------------------------------------------------ */

static GLuint g_grassVBO = 0;                 /* one static buffer, the whole map      */
static GLuint g_grassIBO = 0;                 /* one shared index list, any map size   */
static GLuint g_grassGroundTex = 0;           /* per cell RGB = drawn ground, A = cover */

static std::vector<GrassTile>     g_grassTile;
static std::vector<unsigned char> g_grassMask;   /* one BIT a terrain texel            */
static std::vector<unsigned char> g_grassCellCov;/* 0..255 grass fraction of the cell  */
static std::vector<unsigned char> g_grassBlock;  /* 1 = no grass and a silhouette edge */
static std::vector<unsigned char> g_grassNearBlock;

static int    g_grassTilesX = 0, g_grassTilesY = 0;
static int    g_grassMaskW = 0, g_grassMaskH = 0;
static float  g_grassPosScale = 256.0f;       /* vertex units a cell; see below        */
static float  g_grassCut = 0.0f;              /* the learned green fraction cut        */
static float  g_grassSdRange = 1.0f;          /* cells the skirt distance can express  */
static int    g_grassSdCells = 1;             /* the same, as a whole cell radius      */
static unsigned g_grassBlades = 0;
static unsigned g_grassBytes = 0;

/* The sync guard's memory of what it built from. */
static char  g_grassScen[17] = { 0 };
static int   g_grassGridW = 0, g_grassGridH = 0;
static int   g_grassRect[4] = { 0, 0, 0, 0 };
static int   g_grassAtlas = -1;
static GLuint g_grassAtlasGL = 0;
static GrassBakeCfg g_grassCfg;
static int   g_grassHave = 0;
static int   g_grassRefused = 0;   /* the last attempt refused, and printed why */

/* THE POSITION SCALE, and its third step.
   The root is stored in short world units. 1/256 cell is the brain's own lepton
   (cnc_eyes.cpp:5415), and it is the right unit up to 127 cells. At 128 cells the east
   edge reaches 32768, one past the signed short, and wraps to -32768: the last column
   of the map would read its wind phase, its ground colour and its skirt distance out of
   the opposite corner, silently, one column wide. So the scale steps down. It is a
   whole power of two so the shader's multiply is exact.

      grid <= 127   256 units a cell   placement quantum 1/256 cell
      grid <= 255   128 units a cell   placement quantum 1/128 cell
      otherwise      64 units a cell   placement quantum 1/64  cell

   The third step exists to be correct at the ceiling this build actually supports
   (C3D_MAP_MAX is 256, c3d_ceiling.h:45, raised from 128), even though GRASS_GRID_MAX
   currently refuses a grid that would need it. A per tile origin would keep 1/256
   everywhere and the tile record already carries cx and cy for it; that is the change
   to make if the ring below is ever built. */
static float grass_pos_scale_for(int gw, int gh)
{
    const int g = gw > gh ? gw : gh;
    if (g * 256 <= 32767) return 256.0f;
    if (g * 128 <= 32767) return 128.0f;
    return 64.0f;
}

/* THE WIDEST SKIRT THE HEIGHT DIAL CAN EVER ASK FOR, in cells.
   A blade of world height h standing at a silhouette edge covers the ground behind it
   over a band of h * cot(elevation), where elevation is the angle the VIEW RAY makes
   with the ground: the ray that grazes the tip runs on h / sin(elevation) before it
   reaches the ground and lands h * cot(elevation) beyond the root. It is NOT
   h / cos(pitch), which is a length along the ray rather than a distance on the ground
   and which moves the wrong way with the tilt: the cotangent SHRINKS as the camera
   tilts over, so the widest band comes from the SHALLOWEST ray in the frame and not
   from the steepest pitch.
   THE SHALLOWEST RAY IS THE TOP ROW OF THE PICTURE, half a field of view above the
   camera's own tilt, so the band is measured at pitch_min - fovy_max/2. At the console's
   near zoom that is 44.691 - 25 = 19.691 degrees off the ground where the centre of the
   same frame sees 44.691, so the top of the picture needs nearly three times the band
   its centre does (cnc_eyes.cpp:5457-5459).
   The band is baked from the TOP of the height dial's range rather than from its live
   value, which is what stops the height slider invalidating a 20 MB buffer: the draw
   fades over the live band, always narrower than this one, and the baked distance only
   has to be able to EXPRESS it. At the defaults that is
   0.30 * 1.45 * cot(19.691 deg) = 1.216 cells, so the search below walks a radius of
   two cells and 25 rectangle tests.
   This is a floor rather than a bound to the last texel: perspective widens the ink's
   true reach by a further h / (H - h) of itself, H being the eye's height above the
   ground, which is 7% at the shipped rig's lowest eye. It costs no saturation, because
   the draw's band is built the same way from a height that never exceeds this ceiling,
   so the two stay in step. */
static float grass_sd_range_of(const GrassBakeCfg& c)
{
    /* The elevation of the top row, floored just off the horizontal so the cotangent
       stays finite for a camera looking along the ground. A rig that shallow cannot
       express its skirt at all, and the clamp below says so out loud. */
    float e = c.pitch_min - c.fovy_max * 0.5f;
    if (e < 0.02f) e = 0.02f;
    float r = c.height_ceiling * (1.0f + c.vary_ceiling) / tanf(e);
    if (r < GRASS_SD_MIN) r = GRASS_SD_MIN;
    if (r > GRASS_SD_MAX) {
        /* LOUD, the way the grid and the budget refusals are, because this one is a
           picture bug and not a refusal, and nothing downstream can recover it: a blade
           between GRASS_SD_MAX and the real band keeps full alpha over a silhouette
           edge, which is the dark halo the skirt exists to prevent, and the vertex
           distance that would have faded it has already saturated. */
        fprintf(stderr, "GRASS|skirt|clamped %.3f cells to GRASS_SD_MAX %.3f: a tilt of "
                        "%.2f deg with a %.2f deg field of view puts the top of the "
                        "frame %.2f deg off the ground, where a blade of %.3f cells "
                        "reaches further than the baked distance can express. Blades in "
                        "the gap draw at full alpha over silhouette edges.\n",
                (double)r, (double)GRASS_SD_MAX,
                (double)(c.pitch_min * 57.2957795f), (double)(c.fovy_max * 57.2957795f),
                (double)(e * 57.2957795f),
                (double)(c.height_ceiling * (1.0f + c.vary_ceiling)));
        r = GRASS_SD_MAX;
    }
    return r;
}

/* ---- the hash -------------------------------------------------------------------- */

/* The same mixer tib3d_hash uses (tib3d_mod.h:268), with the blade's slot folded in as
   the third term the way draw_tiberium already folds its clump index. Integer, bit
   exact, never a random number and never the clock, so the field is identical on every
   run of the same map and two --shot runs stay byte identical. */
static unsigned grass_hash(int x, int y, unsigned k)
{
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ k * 83492791u;
    h ^= h >> 13; h *= 2654435761u; h ^= h >> 16;
    return h;
}

static inline GLshort grass_short(float v)
{
    long q = lrintf(v);
    if (q >  32767l) q =  32767l;
    if (q < -32767l) q = -32767l;
    return (GLshort)q;
}

/* ---- reading the art ------------------------------------------------------------- */

/* THE MASK AND THE COLOUR COME FROM DIFFERENT SHEETS, and that split is the design.
 *
 * THE MASK is read from the CARTRIDGE atlas, which the pack always keeps
 * (cnc_eyes.cpp:2096-2098 copies it into p.terrainCart before anything else touches it).
 * On that sheet the CLEAR1 palette sits +0.1723 above neutral in green fraction with a
 * standard deviation of 0.0116, so a cut three deviations below its mean separates
 * grass from road, cliff, rock and shore cleanly. Measured again here, independently:
 * TEMPERAT cartridge mean 0.5056, sd 0.0116, cut 0.4708.
 *
 * THE 1995 DOS SHEET CANNOT DO THIS JOB. Its CLEAR1 mean is only +0.0513 above neutral
 * with a deviation 3.6 times wider, and at zero tolerance its CLEAR1 palette already
 * matches 73.4% of ROAD texels. A mask built from it carpets every road in grass.
 *
 * AND THE CARTRIDGE COPY IS THE RIGHT COPY FOR ANOTHER REASON. It is taken at
 * cnc_eyes.cpp:2096, BEFORE fx_bleed_rgba runs over the same buffer at :2117. The bleed
 * writes colour into alpha 0 texels so bilinear has something to average; the mask
 * therefore reads TRUE holes where the GPU readback of the drawn sheet would read bled
 * colour. That is the right way round and it is load bearing.
 *
 * THE COLOUR is read from whichever atlas is DRAWN, off the GPU, exactly as
 * water_mod.h:673-687 already reads the sand under the sea, and for the reason stated
 * there: only the cartridge sheet is kept and it is not the one on screen. Flipping
 * --texset then repaints the grass and never moves it, which is both correct and what
 * the split buys.
 *
 * TD'S OWN LAND TABLE IS BLIND TO ROADS. Every template D01 through D43 carries
 * Land = EDIT_LAND_CLEAR in both columns (edit_tables.h:150-192), so edit_land_of
 * cannot find a road and anyone reaching for it as the whole classifier will grow grass
 * down the middle of every one of them. It is used here for the WATER veto only, where
 * it is exact.
 */

/* One full atlas off the GPU. The size guard is water_mod.h:682 verbatim: it admits a
   64 MiB transient at the limit, though the typical case is a temperate sheet padded to
   1024x1024, which is 4 MiB. A texset flip currently costs this readback twice, once
   here and once in water_field_sync, on the same sheet; sharing one buffer between the
   two fields is worth doing and is recorded rather than done, because water_mod.h is
   not this file's to change. */
static bool grass_atlas_readback(int ti, std::vector<unsigned char>& out,
                                 int* w, int* h, int* uw, int* uh)
{
    if (ti < 0 || ti >= (int)g_pack.tex.size()) return false;
    const PackTex& t = g_pack.tex[ti];
    if (t.w <= 0 || t.h <= 0 || t.uw <= 0 || t.uh <= 0) return false;
    if ((long)t.w * t.h > 4096L * 4096L) return false;
    out.resize((size_t)t.w * t.h * 4);
    /* THE BOUND TEXTURE ON THE ACTIVE UNIT GOES BACK, rather than a bare 0. A texture
       set flip reaches this readback in the middle of a world pass, and a 0 left behind
       there is the terrain atlas unbound in the middle of the ground draw: the next
       sample reads the default texture object and no GL error is raised to say so. The
       ACTIVE UNIT itself is never changed here, so it is not saved: no glActiveTexture
       is called anywhere in this file. */
    GLint prevBind = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBind);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, &out[0]);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevBind);
    *w = t.w; *h = t.h; *uw = t.uw; *uh = t.uh;
    return true;
}

/* The green fraction of one RGBA texel, and whether it is opaque enough to count.
   g/(r+g+b) rather than a distance to a palette, because the same visual green is
   stored as (48,92,41) inside CLEAR1 and (49,90,41) inside D01: two different 5 bit
   expansions in one sheet, so a set membership rule misses half of it while a ratio
   rule does not. */
static inline bool grass_texel_green(const unsigned char* q, float* g)
{
    if (q[3] < 128) return false;
    const int t = (int)q[0] + (int)q[1] + (int)q[2];
    if (t <= 0) return false;
    *g = (float)q[1] / (float)t;
    return true;
}

/* Learn the cut from template 0, CLEAR1, on the cartridge sheet. Sixteen icons of plain
   ground with nothing drawn on them, 9216 texels, and the bank has them in every
   theater: verified against the generated tables, where template 0 occupies slots 0..15
   in all five banks and every slot in every bank is distinct. Learned per bank rather
   than tabulated, because the runtime atlas is the theater BANK and not a per map
   composite, so one number serves every map of a theater. */
static bool grass_learn_cut(const unsigned char* cart, int cw, int ch,
                            const GrassBakeCfg& cfg, float* cut, float* excess)
{
    int n = 0;
    const TtSlot* tt = tt_table(g_pack.theater, &n);
    double sum = 0.0, sum2 = 0.0;
    long   cnt = 0;
    for (int i = 0; i < n; i++) {
        if (tt[i].tmpl != 0) continue;
        float fx0, fy0;
        tt_slot_rect((int)tt[i].slot, &fx0, &fy0);
        const int x0 = (int)fx0, y0 = (int)fy0;
        for (int y = y0; y < y0 + TT_TS; y++) {
            if (y < 0 || y >= ch) continue;
            for (int x = x0; x < x0 + TT_TS; x++) {
                if (x < 0 || x >= cw) continue;
                float g;
                if (!grass_texel_green(cart + ((size_t)y * cw + x) * 4, &g)) continue;
                sum += g; sum2 += (double)g * g; cnt++;
            }
        }
    }
    if (cnt < 256) return false;
    const double mean = sum / (double)cnt;
    double var = sum2 / (double)cnt - mean * mean;
    /* Floor the deviation as a fraction of the mean rather than at an absolute epsilon,
       so a bank whose CLEAR1 happened to be flat would collapse the cut onto the mean by
       a stated rule instead of by a rounding. No shipped bank is flat: the smallest
       measured deviation is 0.0025, on SNOW, which fails the theater gate anyway. */
    const double floorv = mean * 0.002;
    if (var < floorv * floorv) var = floorv * floorv;
    const double sd = sqrt(var);
    *excess = (float)(mean - 1.0 / 3.0);
    *cut    = (float)(mean - (double)cfg.colour_sd * sd);
    return true;
}

/* ---- the mask, the coverage and the skirt ---------------------------------------- */

static inline int grass_mask_get(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= g_grassMaskW || ty >= g_grassMaskH) return 0;
    const size_t bit = (size_t)ty * g_grassMaskW + tx;
    return (g_grassMask[bit >> 3] >> (bit & 7)) & 1;
}

static inline void grass_mask_set(int tx, int ty)
{
    const size_t bit = (size_t)ty * g_grassMaskW + tx;
    g_grassMask[bit >> 3] |= (unsigned char)(1u << (bit & 7));
}

/* Recover a cell's (template, icon) from the atlas rectangle it draws.
   A cell's rectangle IS its tile slot: the baker packs 32 columns at pitch 26 with a one
   texel gutter (terrain_tiles.h:14-17), so slot = (py-1)/26*32 + (px-1)/26 inverts
   tt_slot_rect exactly. This needs no .BIN and no call into the brain, which matters
   because the .BIN is editor only. A few icons are packed mirrored, with u1 < u0; either
   endpoint of the rectangle floors to the same slot, since (s*26+24)/26 == s. */
static int grass_cell_land(const PackCell& c, int uw, int uh, const int* rev, int nrev)
{
    const float ulo = c.u0 < c.u1 ? c.u0 : c.u1;
    const float vlo = c.v0 < c.v1 ? c.v0 : c.v1;
    const int px = (int)(ulo * (float)uw);
    const int py = (int)(vlo * (float)uh);
    if (px < 1 || py < 1) return EDIT_LAND_CLEAR;
    const int slot = ((py - 1) / TT_PITCH) * TT_COLS + ((px - 1) / TT_PITCH);
    if (slot < 0 || slot >= nrev || rev[slot] < 0) return EDIT_LAND_CLEAR;
    return edit_land_of(rev[slot] >> 8, rev[slot] & 0xff);
}

/* Build the per texel mask, the per cell coverage, and the per cell block flag.
   Returns false when this pack or this theater has no grass at all. */
static bool grass_mask_build(const GrassBakeCfg& cfg)
{
    if (g_pack.terrainCart.empty() || g_pack.terrainCartW <= 0 || g_pack.terrainCartH <= 0) {
        fprintf(stderr, "GRASS|off|the pack kept no cartridge atlas, so the mask has no source\n");
        return false;
    }
    if (g_pack.terrainTex < 0 || g_pack.terrainTex >= (int)g_pack.tex.size()) return false;

    const unsigned char* cart = &g_pack.terrainCart[0];
    const int cw = g_pack.terrainCartW, ch = g_pack.terrainCartH;
    const PackTex& at = g_pack.tex[g_pack.terrainTex];
    if (at.uw <= 0 || at.uh <= 0) return false;

    float cut = 0.0f, excess = 0.0f;
    if (!grass_learn_cut(cart, cw, ch, cfg, &cut, &excess)) {
        fprintf(stderr, "GRASS|off|theater %s has no readable CLEAR1 tiles\n", g_pack.theater);
        return false;
    }
    /* THE THEATER GATE. Measured green excess of the CLEAR1 mean over neutral, on the
       cartridge sheet, for every shipped bank: TEMPERAT +0.1723, WINTER +0.0513,
       SAND +0.0187, SNOW -0.0111, DESERT -0.0182. WINTER's bank is the 1995 DOS
       temperate art, byte for byte, which is why it sits with the DOS numbers and not
       with the cartridge one.

       WHICH BANKS GROW GRASS IS A DECISION AND IT HAS BEEN MADE: temperate and winter.
       The shipped gate of 0.035 sits between winter and sand with room on both sides,
       clearing winter by a factor of 1.5 and turning sand away by 1.9. Desert stays bare
       deliberately rather than for want of a number: a blade takes most of its colour
       from the ground it grows out of, so admitting desert lays down a quarter of a
       million cards that move the picture by an average of six levels a pixel and read
       as sand texture rather than as grass. Snow and desert are BELOW neutral, so no
       positive setting of the dial can reach them at all. */
    if (excess <= cfg.theater_cut) {
        fprintf(stderr, "GRASS|off|theater %s green excess %+0.4f does not clear the gate %.4f\n",
                g_pack.theater, excess, cfg.theater_cut);
        return false;
    }
    g_grassCut = cut;

    /* slot -> (template, icon), for the water veto. Built once, sized from the bank. */
    int n = 0;
    const TtSlot* tt = tt_table(g_pack.theater, &n);
    int maxslot = 0;
    for (int i = 0; i < n; i++) if ((int)tt[i].slot > maxslot) maxslot = (int)tt[i].slot;
    std::vector<int> rev((size_t)maxslot + 1, -1);
    for (int i = 0; i < n; i++)
        rev[(size_t)tt[i].slot] = ((int)tt[i].tmpl << 8) | (int)tt[i].icon;

    g_grassMaskW = g_gridW * GRASS_MASK_RES;
    g_grassMaskH = g_gridH * GRASS_MASK_RES;
    g_grassMask.assign(((size_t)g_grassMaskW * g_grassMaskH + 7) / 8, 0);
    g_grassCellCov.assign((size_t)g_gridW * g_gridH, 0);
    g_grassBlock.assign((size_t)g_gridW * g_gridH, 1);

    long grassTexels = 0, cellsWithGrass = 0;
    for (size_t i = 0; i < g_pack.cell.size(); i++) {
        const PackCell& c = g_pack.cell[i];
        if (c.x < 0 || c.y < 0 || c.x >= g_gridW || c.y >= g_gridH) continue;
        /* ONLY INSIDE THE PLAY RECTANGLE. The margin cells the pack carries beyond it are
           drawn by cell_shown (cnc_eyes.cpp:6477) but they are the edge of the world, not
           ground: what lies past them is the black clear colour, and the light pass
           returns raw albedo there before it ever reaches the sun, the shadow or the
           ambient occlusion (a fragment at the far plane exits early). A blade leaning
           over that boundary is an unlit fringe along the whole map rim. water_field_build
           draws the same line for the same reason at water_mod.h:428-433. */
        const bool inRect = c.x >= g_mapX && c.x < g_mapX + g_mapW
                         && c.y >= g_mapY && c.y < g_mapY + g_mapH;
        if (!inRect) continue;
        if (!cell_shown(c.x, c.y)) continue;
        /* Two water vetoes, and both are needed. c.holes marks a cell the art draws as
           sea (draw_water_quads keys on exactly this, cnc_eyes.cpp:6968-6971), and the
           land table catches a shore tile that carries water without being holed. */
        if (c.holes) continue;
        if (grass_cell_land(c, at.uw, at.uh, rev.empty() ? NULL : &rev[0],
                            (int)rev.size()) == EDIT_LAND_WATER) continue;

        g_grassBlock[(size_t)c.y * g_gridW + c.x] = 0;

        int keep = 0;
        for (int j = 0; j < GRASS_MASK_RES; j++) {
            const float fv = ((float)j + 0.5f) / (float)GRASS_MASK_RES;
            int ty = (int)((c.v0 + (c.v1 - c.v0) * fv) * (float)at.uh);
            if (ty < 0) ty = 0; else if (ty >= ch) ty = ch - 1;
            for (int k = 0; k < GRASS_MASK_RES; k++) {
                const float fu = ((float)k + 0.5f) / (float)GRASS_MASK_RES;
                int tx = (int)((c.u0 + (c.u1 - c.u0) * fu) * (float)at.uw);
                if (tx < 0) tx = 0; else if (tx >= cw) tx = cw - 1;
                float g;
                if (!grass_texel_green(cart + ((size_t)ty * cw + tx) * 4, &g)) continue;
                if (g < cut) continue;
                grass_mask_set(c.x * GRASS_MASK_RES + k, c.y * GRASS_MASK_RES + j);
                keep++;
            }
        }
        grassTexels += keep;
        if (keep > 0) cellsWithGrass++;
        const int cov = (keep * 255) / (GRASS_MASK_RES * GRASS_MASK_RES);
        g_grassCellCov[(size_t)c.y * g_gridW + c.x] = (unsigned char)cov;
    }

    /* The skirt's neighbourhood flag. A blade only has to measure its distance to a
       silhouette edge when there is one within reach; everywhere else the distance
       saturates and the test is one byte. */
    g_grassNearBlock.assign((size_t)g_gridW * g_gridH, 0);
    const int R = g_grassSdCells;
    for (int y = 0; y < g_gridH; y++)
        for (int x = 0; x < g_gridW; x++) {
            if (g_grassBlock[(size_t)y * g_gridW + x]) continue;
            int close = 0;
            for (int dy = -R; dy <= R && !close; dy++)
                for (int dx = -R; dx <= R; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= g_gridW || ny >= g_gridH) { close = 1; break; }
                    if (g_grassBlock[(size_t)ny * g_gridW + nx]) { close = 1; break; }
                }
            g_grassNearBlock[(size_t)y * g_gridW + x] = (unsigned char)close;
        }

    fprintf(stderr, "GRASS|mask|scen=%s|theater=%s|excess=%+0.4f|cut=%.4f|"
                    "grass_cells=%ld|grass_texels=%ld|mask=%dx%d|skirt=%.3f cells"
                    "|skirt_r=%d cells|skirt_tests=%d\n",
            g_pack.scen, g_pack.theater, excess, cut, cellsWithGrass, grassTexels,
            g_grassMaskW, g_grassMaskH, g_grassSdRange,
            R, (2 * R + 1) * (2 * R + 1));
    return cellsWithGrass > 0;
}

/* ---- one blade ------------------------------------------------------------------- */

struct GrassBlade {
    float x, y, z;                 /* the root, world cells                            */
    float sd;                      /* cells to the nearest silhouette edge, saturating  */
    unsigned char hgt, yaw, phase;
};

/* Distance from a point to the nearest blocked cell's rectangle, saturating at the
   skirt range. Searching a radius of g_grassSdCells is EXACT for every distance below
   the saturation and not merely close: a blocked cell at Chebyshev distance R+1 has its
   nearest edge at least R cells from any point of the centre cell, so a search that
   found nothing inside R is entitled to return R. */
static float grass_skirt_dist(int cx, int cy, float wx, float wz)
{
    if (!g_grassNearBlock[(size_t)cy * g_gridW + cx]) return g_grassSdRange;
    float best = g_grassSdRange;
    const int R = g_grassSdCells;
    for (int dy = -R; dy <= R; dy++)
        for (int dx = -R; dx <= R; dx++) {
            const int nx = cx + dx, ny = cy + dy;
            int blocked;
            if (nx < 0 || ny < 0 || nx >= g_gridW || ny >= g_gridH) blocked = 1;
            else blocked = g_grassBlock[(size_t)ny * g_gridW + nx];
            if (!blocked) continue;
            /* the cell occupies [nx, nx+1) x [ny, ny+1) on the ground */
            float ex = 0.0f, ez = 0.0f;
            if (wx < (float)nx)            ex = (float)nx - wx;
            else if (wx > (float)(nx + 1)) ex = wx - (float)(nx + 1);
            if (wz < (float)ny)            ez = (float)ny - wz;
            else if (wz > (float)(ny + 1)) ez = wz - (float)(ny + 1);
            const float d = sqrtf(ex * ex + ez * ez);
            if (d < best) best = d;
        }
    return best;
}

/* Does blade (cx, cy, slot) exist, and where. The whole placement rule, in one place, so
   the counting pass and the writing pass cannot drift apart.

   The position is UNIFORM inside the cell rather than a jittered grid, and that is a
   requirement of the LOD rather than laziness: the first N slots of a cell have to be a
   fair sample of the cell at EVERY N, and the prefix of a jittered grid is a
   corner biased subset unless it is permuted first. A uniform hash is fair at every
   prefix by construction.

   The blade exists only where the mask says the ART is grass under it, so the verge
   along a road or a cliff is cut at the art's own texel and the field thins into it
   instead of stopping on a cell boundary. */
static bool grass_blade_at(int cx, int cy, int slot, GrassBlade* out)
{
    if (g_grassBlock[(size_t)cy * g_gridW + cx]) return false;
    if (g_grassCellCov[(size_t)cy * g_gridW + cx] == 0) return false;

    const unsigned h1 = grass_hash(cx, cy, (unsigned)slot * 2u);
    const unsigned h2 = grass_hash(cx, cy, (unsigned)slot * 2u + 1u);
    const float fx = (float)(h1 & 0xffffu) * (1.0f / 65536.0f);
    const float fz = (float)((h1 >> 16) & 0xffffu) * (1.0f / 65536.0f);

    const int tx = cx * GRASS_MASK_RES + (int)(fx * (float)GRASS_MASK_RES);
    const int ty = cy * GRASS_MASK_RES + (int)(fz * (float)GRASS_MASK_RES);
    if (!grass_mask_get(tx, ty)) return false;

    out->x = (float)cx + fx;
    out->z = (float)cy + fz;
    /* PLANAR over the drawn triangle, not bilinear over the cell. terrain_y
       (cnc_eyes.cpp:1188-1211) samples exactly the two triangles draw_terrain
       rasterises; a bilinear patch is a different surface by up to 0.121 cell on a real
       map, and that gap is precisely what makes things standing on a slope float. */
    out->y  = terrain_y(out->x, out->z);
    out->sd = grass_skirt_dist(cx, cy, out->x, out->z);
    out->hgt   = (unsigned char)(h2 & 0xffu);
    out->yaw   = (unsigned char)((h2 >> 8) & 0xffu);
    out->phase = (unsigned char)((h2 >> 16) & 0xffu);
    return true;
}

/* Write one tile, slot major, into `out`; with `out` NULL it only counts. Returns the
   blade count and fills the tile's cumulative table.

   Slot major is the single inverted loop nest that makes the count based LOD work. Cell
   major would make "draw the first N blades" a set of fully grassed cells beside bald
   ones, which is a visible seam that moves with the camera. */
static unsigned grass_emit_tile(GrassTile& t, int denMax, float tipRise, GrassVert* out)
{
    const int x0 = t.cx, y0 = t.cy;
    const int x1 = (x0 + GRASS_TILE < g_gridW) ? x0 + GRASS_TILE : g_gridW;
    const int y1 = (y0 + GRASS_TILE < g_gridH) ? y0 + GRASS_TILE : g_gridH;
    unsigned n = 0;
    float ylo = 1.0e9f, yhi = -1.0e9f;

    for (int s = 0; s < GRASS_SLOTS; s++) {
        if (s < denMax) {
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    GrassBlade b;
                    if (!grass_blade_at(x, y, s, &b)) continue;
                    if (out) {
                        const float ps = g_grassPosScale;
                        /* The scale above guarantees x and z fit; y is a heightmap byte
                           over 64 minus the base level, so it cannot leave +-4 cells and
                           cannot reach +-1024 units. The clamp is here so that a pack
                           with a corrupt base can only draw a blade in the wrong place,
                           never wrap it to the opposite corner of the map. */
                        GLshort px = grass_short(b.x * ps);
                        GLshort py = grass_short(b.y * ps);
                        GLshort pz = grass_short(b.z * ps);
                        GLshort sd = grass_short(b.sd * GRASS_SD_STEP);
                        /* gl_Color.a must BE slot/127, so the draw's uSlotCut = sc/127
                           compares against it with no scaling in between. The rounding
                           error of the byte is under one part in 510 of the range,
                           which is far inside the fade band. */
                        const GLubyte sb = (GLubyte)((s * 255 + 63) / (GRASS_SLOTS - 1));
                        GrassVert* v = out + (size_t)n * 4;
                        for (int k = 0; k < 4; k++) {
                            v[k].px = px; v[k].py = py; v[k].pz = pz;
                            v[k].sd = sd;
                            v[k].hgt = b.hgt; v[k].yaw = b.yaw;
                            v[k].phase = b.phase; v[k].slot = sb;
                        }
                        /* 0 root left, 1 root right, 2 tip left, 3 tip right. The shared
                           index list below draws (0,1,2) and (2,1,3), the same SW to NE
                           split the console's own terrain uses. Culling is off for grass,
                           so the winding is free. */
                        v[0].side = (GLshort)-32767; v[0].b2t = 0;
                        v[1].side = (GLshort) 32767; v[1].b2t = 0;
                        v[2].side = (GLshort)-32767; v[2].b2t = (GLshort)32767;
                        v[3].side = (GLshort) 32767; v[3].b2t = (GLshort)32767;
                    }
                    if (b.y < ylo) ylo = b.y;
                    if (b.y > yhi) yhi = b.y;
                    n++;
                }
        }
        t.cum[s] = (unsigned short)(n > 65535u ? 65535u : n);
    }
    t.blades = n;
    /* The BLADE bracket, not the root bracket. ylo and yhi were gathered from roots, so
       the top carries the tallest tip the height dial's range can grow; without it a
       cull built from the box drops tiles whose grass is still in frame. */
    t.y0 = (n > 0) ? ylo : 0.0f;
    t.y1 = (n > 0) ? yhi + tipRise : 0.0f;
    return n;
}

/* ---- the geometry ---------------------------------------------------------------- */

static bool grass_geometry_build(const GrassBakeCfg& cfg)
{
    if (!grass_buf_load()) return false;

    int denMax = cfg.den_max;
    if (denMax < 1) denMax = 1;
    if (denMax > GRASS_SLOTS) denMax = GRASS_SLOTS;

    /* How far a tip stands above its root at the bake ceiling, in cells, which is the
       same unit the world heights are in. Read from the RANGE ceilings and not from the
       live dials, like every other baked quantity here, so moving the height slider
       cannot invalidate the buffer. It is passed to the emit rather than reached for,
       because g_grassCfg is not assigned until after this function returns. */
    const float tipRise = cfg.height_ceiling * (1.0f + cfg.vary_ceiling);

    g_grassPosScale = grass_pos_scale_for(g_gridW, g_gridH);
    g_grassTilesX = (g_gridW + GRASS_TILE - 1) / GRASS_TILE;
    g_grassTilesY = (g_gridH + GRASS_TILE - 1) / GRASS_TILE;
    g_grassTile.assign((size_t)g_grassTilesX * g_grassTilesY, GrassTile());

    /* PASS 1, the count. It exists so the refusal below can name an EXACT number and so
       the allocation that follows is exact and happens once. It costs one extra walk of
       about a million hash pairs, which is single digit milliseconds. */
    unsigned long total = 0;
    for (int ty = 0; ty < g_grassTilesY; ty++)
        for (int tx = 0; tx < g_grassTilesX; tx++) {
            GrassTile& t = g_grassTile[(size_t)ty * g_grassTilesX + tx];
            t.cx = (unsigned short)(tx * GRASS_TILE);
            t.cy = (unsigned short)(ty * GRASS_TILE);
            t.vert0 = (unsigned int)(total * 4);
            total += grass_emit_tile(t, denMax, tipRise, NULL);
        }

    const unsigned long bytes = total * 4ul * (unsigned long)sizeof(GrassVert);
    if (bytes > (unsigned long)GRASS_VBO_BUDGET) {
        /* THE SECOND REFUSAL, and it is loud on purpose. A whole map static buffer is
           the one decision that cannot absorb being wrong about the map size, so when it
           does not fit it says what it would have cost, which constant stopped it, and
           what ceiling WOULD have fitted, rather than silently halving something. */
        const int fits = (int)((unsigned long)GRASS_VBO_BUDGET / (bytes / (unsigned long)denMax));
        fprintf(stderr, "GRASS|refused|scen=%s grid=%dx%d needs %lu blades = %.2f MiB "
                        "at density ceiling %d; GRASS_VBO_BUDGET is %.2f MiB. "
                        "A ceiling of %d would fit. No grass this map; "
                        "the rest of the Enhanced chain is unaffected.\n",
                g_pack.scen, g_gridW, g_gridH, total, (double)bytes / 1048576.0,
                denMax, (double)GRASS_VBO_BUDGET / 1048576.0, fits > 0 ? fits : 1);
        g_grassTile.clear();
        return false;
    }
    if (total == 0) return false;

    /* PASS 2, the write. One allocation, one upload, never touched again. */
    {
        std::vector<GrassVert> v(total * 4);
        for (size_t i = 0; i < g_grassTile.size(); i++) {
            GrassTile& t = g_grassTile[i];
            if (t.blades == 0) continue;
            grass_emit_tile(t, denMax, tipRise, &v[t.vert0]);
        }
        const unsigned long want = (unsigned long)(v.size() * sizeof(GrassVert));
        gr_glGenBuffers(1, &g_grassVBO);
        gr_glBindBuffer(GL_ARRAY_BUFFER, g_grassVBO);
        /* Drain first, so the error read after the upload can only be the upload's. */
        for (int e = 0; e < 16 && glGetError() != GL_NO_ERROR; e++) { }
        gr_glBufferData(GL_ARRAY_BUFFER, (FxGLsizeiptr)want, &v[0], GL_STATIC_DRAW);
        if (!grass_store_made(GL_ARRAY_BUFFER, want, "vertex")) return false;
    }

    /* THE SHARED INDEX LIST. Every tile has the same 4 vertex blade stride, so one list
       of tile relative indices serves all of them, at any map size. A tile is reached by
       moving the three attribute pointers to its first vertex, because GL 2.1 has no
       glDrawElementsBaseVertex: that arrived in 3.2. */
    {
        const int maxBlades = GRASS_TILE * GRASS_TILE * GRASS_SLOTS;
        std::vector<unsigned short> idx((size_t)maxBlades * 6);
        for (int b = 0; b < maxBlades; b++) {
            const unsigned short o = (unsigned short)(b * 4);
            idx[(size_t)b * 6 + 0] = (unsigned short)(o + 0);
            idx[(size_t)b * 6 + 1] = (unsigned short)(o + 1);
            idx[(size_t)b * 6 + 2] = (unsigned short)(o + 2);
            idx[(size_t)b * 6 + 3] = (unsigned short)(o + 2);
            idx[(size_t)b * 6 + 4] = (unsigned short)(o + 1);
            idx[(size_t)b * 6 + 5] = (unsigned short)(o + 3);
        }
        const unsigned long want = (unsigned long)(idx.size() * sizeof(unsigned short));
        gr_glGenBuffers(1, &g_grassIBO);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_grassIBO);
        for (int e = 0; e < 16 && glGetError() != GL_NO_ERROR; e++) { }
        gr_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (FxGLsizeiptr)want,
                        &idx[0], GL_STATIC_DRAW);
        if (!grass_store_made(GL_ELEMENT_ARRAY_BUFFER, want, "index")) return false;
    }

    /* UNBIND BOTH TARGETS BEFORE RETURNING, and this is not tidiness.
       With an ARRAY_BUFFER left bound, a later glVertexPointer given a HOST pointer is
       reinterpreted as a byte OFFSET into that buffer. glGetError returns GL_NO_ERROR
       after both the pointer call and the draw, and the draw puts nothing on screen;
       glBegin/glEnd is immune, which is what makes the failure selective and impossible
       to find by looking at the frame. tree3d_arrays_on (cnc_eyes.cpp:7932-7943) and
       draw_tiberium_solid (cnc_eyes.cpp:8149-8218) both use host pointers and both run
       later in the frame than grass. Leave a buffer bound here and every tree and every
       tiberium crystal in the game disappears, with no error anywhere. */
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    g_grassBlades = (unsigned)total;
    g_grassBytes  = (unsigned)bytes;
    fprintf(stderr, "GRASS|vbo|scen=%s|grid=%dx%d|tiles=%dx%d|den_max=%d|blades=%lu|"
                    "vbo=%.2f MiB|idx=%d B|pos_scale=%.0f\n",
            g_pack.scen, g_gridW, g_gridH, g_grassTilesX, g_grassTilesY, denMax, total,
            (double)bytes / 1048576.0,
            GRASS_TILE * GRASS_TILE * GRASS_SLOTS * 6 * 2, g_grassPosScale);
    return true;
}

/* ---- the ground colour ----------------------------------------------------------- */

/* One texel a CELL, RGB = the drawn ground's own colour, A = the cell's grass coverage.
   GL_LINEAR and GL_CLAMP_TO_EDGE, so a blade's root reads a smooth field interpolated
   between the four surrounding cell centres rather than a step at every cell boundary.
   With uFieldScale = (1/gridW, 1/gridH), sampling at the root's world xz lands on the
   cell centre exactly, which is the convention water_mod.h:1530 already uses.

   THE COLOUR STORED HERE IS THE RAW ATLAS COLOUR, not the ground as drawn. The console's
   combiner is out = (texel - tint) * lit (cnc_eyes.cpp:6498-6501), and BOTH of those
   terms move with the shroud every tick, so they belong in the live per corner texture
   the draw path owns, not in this one. A blade that skips them is about 59% too bright
   on level ground and reads as a sticker.

   AND THE LIGHT TERM IS NOT terrain_shade. Under ENHANCED, which is the only tier grass
   runs in, draw_terrain sends a FLAT 160/255 and never calls terrain_shade at all:
   cnc_eyes.cpp:6809-6815 is
       const bool oneSun = g_fxActive && g_fx.sun_lambert && g_fx.terrain_normals;
       const float flat  = 160.0f / 255.0f;
       const float s00   = (oneSun ? flat : terrain_shade(c.x, c.y)) * k00;
   with both dials defaulting to 1, and the slope arriving later from the post chain. The
   live texture's light term must be that whole expression times shroud_corner_vis, or
   the blade root is darkened by a term the ground beneath it was not drawn with.

   THE SHROUD RIDES IN THAT LIVE TEXTURE TOO, and it is not free from draw order. The
   blanket is translucent by design: a mapped but unseen cell is covered at
   SHROUD_DARK_ALPHA 0.45 and the feather band is capped at SHROUD_SOFT_MAXA 0.784
   (shroud_mod.h:358, :486). Call that per corner alpha a. The ground beneath it has
   ALREADY been multiplied by 1 - a in its own vertex colour, so a ground pixel in the
   feather band ends at albedo * (1 - a)^2, while a blade carrying only the vertex term
   ends at albedo * (1 - a): 1.8x the ground it stands in at a = 0.45 and 4.6x at the
   ceiling, a bright ring of lit grass along the whole reveal perimeter. Neither layer
   writes depth (the blanket's writes are off at shroud_mod.h:400 and :662, and grass
   leaves none behind by design), so ORDER alone decides it: grass_draw must run BEFORE
   shroud_dispatch_draw, and the blanket then covers the blades as it covers the ground.
   shroud_corner_vis (shroud_mod.h:567) is the same per corner coverage array the
   blanket's own fade uses, so a blade multiplied by it cannot disagree with the blanket
   about where the edge is. */
static bool grass_colour_build(void)
{
    if (g_grassCellCov.empty()) return false;
    const int ai = terrain_atlas_index();
    std::vector<unsigned char> atl;
    int aw = 0, ah = 0, auw = 0, auh = 0;
    const bool have = grass_atlas_readback(ai, atl, &aw, &ah, &auw, &auh);

    std::vector<unsigned char> px((size_t)g_gridW * g_gridH * 4, 0);
    std::vector<unsigned char> written((size_t)g_gridW * g_gridH, 0);
    long sr = 0, sg = 0, sb = 0, sn = 0;

    for (size_t i = 0; i < g_pack.cell.size(); i++) {
        const PackCell& c = g_pack.cell[i];
        if (c.x < 0 || c.y < 0 || c.x >= g_gridW || c.y >= g_gridH) continue;
        const size_t ci = (size_t)c.y * g_gridW + c.x;
        unsigned char* o = &px[ci * 4];
        o[3] = g_grassCellCov[ci];
        if (!have) {
            /* No readback (a sheet past the size guard, or a driver that refused). Fall
               back to the cell's own average, which the pack already carries per texture
               set for the radar (cnc_eyes.cpp:998-1001, filled at :2084 and :6623). It is
               a coarser answer, not a wrong one. */
            const int ts = terrain_texset_drawn();
            const int ix = (ts >= 0 && ts < 3) ? ts : 0;
            o[0] = c.avg[ix][0]; o[1] = c.avg[ix][1]; o[2] = c.avg[ix][2];
            written[ci] = 1;
            continue;
        }
        /* Average the DRAWN sheet over this cell's GRASS texels only, so the blade takes
           the colour of the grass beside it and not the average of the grass and the road
           it shares a tile with. Cells with no grass texels fall back to every opaque
           texel, so a GL_LINEAR sample taken near the boundary has something real to
           interpolate towards instead of black. */
        long ar = 0, ag = 0, ab = 0, an = 0, br = 0, bg = 0, bb = 0, bn = 0;
        for (int j = 0; j < GRASS_MASK_RES; j++) {
            const float fv = ((float)j + 0.5f) / (float)GRASS_MASK_RES;
            int ty = (int)((c.v0 + (c.v1 - c.v0) * fv) * (float)auh);
            if (ty < 0) ty = 0; else if (ty >= ah) ty = ah - 1;
            for (int k = 0; k < GRASS_MASK_RES; k++) {
                const float fu = ((float)k + 0.5f) / (float)GRASS_MASK_RES;
                int tx = (int)((c.u0 + (c.u1 - c.u0) * fu) * (float)auw);
                if (tx < 0) tx = 0; else if (tx >= aw) tx = aw - 1;
                const unsigned char* q = &atl[((size_t)ty * aw + tx) * 4];
                if (q[3] < 128) continue;
                br += q[0]; bg += q[1]; bb += q[2]; bn++;
                if (!grass_mask_get(c.x * GRASS_MASK_RES + k, c.y * GRASS_MASK_RES + j))
                    continue;
                ar += q[0]; ag += q[1]; ab += q[2]; an++;
            }
        }
        if (an > 0) {
            o[0] = (unsigned char)(ar / an); o[1] = (unsigned char)(ag / an);
            o[2] = (unsigned char)(ab / an);
            sr += ar / an; sg += ag / an; sb += ab / an; sn++;
            written[ci] = 1;
        } else if (bn > 0) {
            o[0] = (unsigned char)(br / bn); o[1] = (unsigned char)(bg / bn);
            o[2] = (unsigned char)(bb / bn);
            written[ci] = 1;
        }
    }
    /* Cells the pack does not list keep the map's mean grass colour rather than black,
       for the same interpolation reason. */
    if (sn > 0) {
        const unsigned char mr = (unsigned char)(sr / sn), mg = (unsigned char)(sg / sn),
                            mb = (unsigned char)(sb / sn);
        for (size_t i = 0; i < (size_t)g_gridW * g_gridH; i++) {
            if (written[i]) continue;
            unsigned char* o = &px[i * 4];
            o[0] = mr; o[1] = mg; o[2] = mb;
        }
    }

    /* Same contract as the readback above, and for the same reason: the colour build is
       the one thing here a texture set flip runs mid frame, so the upload of the field
       puts the caller's binding back instead of a bare 0. */
    GLint prevBind = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBind);
    if (!g_grassGroundTex) glGenTextures(1, &g_grassGroundTex);
    glBindTexture(GL_TEXTURE_2D, g_grassGroundTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_gridW, g_gridH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevBind);

    g_grassAtlas = ai;
    g_grassAtlasGL = (ai >= 0 && ai < (int)g_pack.tex.size()) ? g_pack.tex[ai].gl : 0;
    fprintf(stderr, "GRASS|colour|scen=%s|atlas=%d|readback=%s|field=%dx%d\n",
            g_pack.scen, ai, have ? "gpu" : "cell averages", g_gridW, g_gridH);
    return true;
}

/* ---- the public face ------------------------------------------------------------- */

static void grass_free(void)
{
    if (g_grassBufReady) {
        if (g_grassVBO) gr_glDeleteBuffers(1, &g_grassVBO);
        if (g_grassIBO) gr_glDeleteBuffers(1, &g_grassIBO);
    }
    g_grassVBO = g_grassIBO = 0;
    if (g_grassGroundTex) glDeleteTextures(1, &g_grassGroundTex);
    g_grassGroundTex = 0;
    g_grassTile.clear();
    g_grassMask.clear();
    g_grassCellCov.clear();
    g_grassBlock.clear();
    g_grassNearBlock.clear();
    g_grassTilesX = g_grassTilesY = 0;
    g_grassMaskW = g_grassMaskH = 0;
    g_grassSdRange = 1.0f; g_grassSdCells = 1;
    g_grassBlades = g_grassBytes = 0;
    g_grassScen[0] = 0;
    g_grassGridW = g_grassGridH = 0;
    g_grassAtlas = -1; g_grassAtlasGL = 0;
    g_grassHave = 0;
    g_grassRefused = 0;
}

/* What the last attempt was made FROM, which is not the same thing as what the field IS.
   A successful bake commits this as the field's identity; a REFUSED bake records it too,
   so the guard below can tell a map that has already said no from a map nothing has been
   tried on yet. Without that record the guard has only g_grassHave to read, and a map
   that refuses itself rebuilds from scratch on every frame for ever: two of the three
   refusals fire only AFTER the mask pass, so the whole cost is paid and thrown away each
   time. grass_buf_load latches itself the same way and for the same reason. It is one
   helper rather than two copies because the two call sites have to stamp exactly the
   same fields the guard compares, and a field added to one copy and not the other is a
   stale bake nothing complains about. */
static void grass_bake_stamp(const GrassBakeCfg& cfg)
{
    snprintf(g_grassScen, sizeof g_grassScen, "%s", g_pack.scen);
    g_grassGridW = g_gridW; g_grassGridH = g_gridH;
    g_grassRect[0] = g_mapX; g_grassRect[1] = g_mapY;
    g_grassRect[2] = g_mapW; g_grassRect[3] = g_mapH;
    g_grassCfg = cfg;
}

/* Build the whole field for the loaded pack. False means no grass, for a reason already
   printed: no cartridge atlas, a theater that fails the gate, a grid past the ceiling, a
   buffer past the budget, or no buffer entry points. */
static bool grass_bake(const GrassBakeCfg& cfg)
{
    grass_free();

    if (g_gridW <= 0 || g_gridH <= 0) return false;
    if (g_gridW > GRASS_GRID_MAX || g_gridH > GRASS_GRID_MAX) {
        /* THE FIRST REFUSAL, before a single texel is read. Do not assume 64x64: the run
           folder holds 91 mission packs at 64x64, two user maps at 128x128 and one at
           256x256, and the 256 one at this bake ceiling is 512 MiB of vertices for a
           buffer that is never rewritten. C3D_MAP_MAX is 256 (c3d_ceiling.h:45), so the
           renderer will happily load it; grass is the thing that has to say no. The
           alternative is a residency ring over a window of tiles, which is a change to
           the residency policy alone and touches neither this layout, the index list, the
           shader nor the draw path. */
        fprintf(stderr, "GRASS|refused|scen=%s grid=%dx%d exceeds GRASS_GRID_MAX %d. "
                        "A whole map static buffer is not the right shape for a map this "
                        "size; it needs a residency ring. No grass this map; the rest of "
                        "the Enhanced chain is unaffected.\n",
                g_pack.scen, g_gridW, g_gridH, GRASS_GRID_MAX);
        return false;
    }

    /* The buffer entry points, before a single texel is read. They are a property of the
       driver and not of the pack, so a driver without them refuses here instead of paying
       for a mask pass and a count pass it is about to throw away. The geometry build asks
       again and the loader latches, so the second ask is one branch. */
    if (!grass_buf_load()) return false;

    g_grassSdRange = grass_sd_range_of(cfg);
    g_grassSdCells = (int)ceilf(g_grassSdRange);
    if (g_grassSdCells < 1) g_grassSdCells = 1;

    /* The number that drives what the bake COSTS is this radius, not the height dial
       that was moved to get it. Both skirt searches are square in it, the per blade one
       is paid twice because the counting pass runs the same placement as the writing
       pass, and a bigger radius also flags more cells as near block, so the bill grows
       faster than the square. A radius of 2 measures as free; past that it is worth
       saying out loud, because otherwise a taller blade buys a slower load with no
       other symptom. This warns and does not refuse: the bake is correct at every
       radius, and the fault worth fixing is the silence, not the cost. */
    if (g_grassSdCells > 2) {
        fprintf(stderr, "GRASS|slow|scen=%s skirt range %.3f cells rounds to a search "
                        "radius of %d, so each search is %d tests a cell against 25 at "
                        "the default radius of 2, and the per blade search is paid "
                        "twice. Expect a longer bake. Lower the height range ceiling "
                        "or the height variation range ceiling to bring it down.\n",
                g_pack.scen, g_grassSdRange, g_grassSdCells,
                (2 * g_grassSdCells + 1) * (2 * g_grassSdCells + 1));
    }

    if (!grass_mask_build(cfg))     { grass_free(); return false; }
    if (!grass_geometry_build(cfg)) { grass_free(); return false; }
    if (!grass_colour_build())      { grass_free(); return false; }

    grass_bake_stamp(cfg);
    g_grassHave = 1;
    return true;
}

/* The guard the draw path calls on its first line every frame, the way water_fx_draw
   calls water_field_sync (water_mod.h:1464).

   THREE OUTCOMES, not two, and the middle one is the point. A --texset flip changes only
   the sheet the ground is DRAWN from, so it must repaint the grass and MUST NOT rebuild
   the mask: the mask comes from the cartridge sheet and rebuilding it off the DOS sheet
   would carpet every road in grass. So an atlas change rebuilds the colour alone, which
   is also four fifths cheaper than a full bake.

   Every value the build READS is compared, including the two dials, because a build time
   value that is not compared is a dead slider in the panel and the cfg file round trips
   it perfectly while it does nothing. water_field_sync carries exactly this at
   water_mod.h:775 and says so. The grid HEIGHT is compared as well as the width: a pack
   that changes only in H would otherwise keep a stale field.

   AND A REFUSAL IS REMEMBERED. Every failure exit leaves the field empty, so with no
   memory of it this guard would re-run the whole refused bake on the next frame, and on
   every frame after that, for the rest of the mission: the buffer budget is only reached
   after the full texel mask, 3072 by 3072 on a 128 cell map, and the entire count pass,
   and the refusal prints its line again each time, on a map where grass draws nothing.
   So a refused bake stamps what it read exactly as a built one does, and the comparison
   below stays in charge of when to try again. A new pack, a new play rectangle or a
   dragged dial gets a fresh attempt and a fresh message; nothing else does. The density
   ceiling is the one that has to keep working, because the budget refusal names the
   ceiling that WOULD have fitted. */
static bool grass_bake_sync(const GrassBakeCfg& cfg)
{
    /* What the last ATTEMPT read, whether it built or refused. */
    const bool changed =
           strcmp(g_grassScen, g_pack.scen) != 0
        || g_grassGridW != g_gridW || g_grassGridH != g_gridH
        || g_grassRect[0] != g_mapX || g_grassRect[1] != g_mapY
        || g_grassRect[2] != g_mapW || g_grassRect[3] != g_mapH
        || g_grassCfg.theater_cut    != cfg.theater_cut
        || g_grassCfg.colour_sd      != cfg.colour_sd
        || g_grassCfg.den_max        != cfg.den_max
        || g_grassCfg.height_ceiling != cfg.height_ceiling
        || g_grassCfg.vary_ceiling   != cfg.vary_ceiling
        || g_grassCfg.pitch_min      != cfg.pitch_min
        || g_grassCfg.fovy_max       != cfg.fovy_max;

    if (!g_grassHave || changed) {
        if (!changed && g_grassRefused) return false;
        if (grass_bake(cfg)) { g_grassRefused = 0; return true; }
        /* Something the bake READS has changed, so an attempt was made. It refused, and
           freeing the field cleared the very keys the comparison above reads, so they are
           recorded again here; that is what makes the next frame one branch rather than
           another mask pass. */
        g_grassRefused = 1;
        grass_bake_stamp(cfg);
        return false;
    }

    const int ai = terrain_atlas_index();
    const GLuint gl = (ai >= 0 && ai < (int)g_pack.tex.size()) ? g_pack.tex[ai].gl : 0;
    /* The GL NAME as well as the index, because terrain_remaster_ensure
       (cnc_eyes.cpp:6568) builds the Remaster atlas LAZILY, minutes into a session, and
       the index does not move when it does. water_mod.h:774 makes the same comparison. */
    if (ai != g_grassAtlas || gl != g_grassAtlasGL)
        return grass_colour_build();
    return true;
}

/* ---- what the draw path must supply ---------------------------------------------- */

/* Declared here, defined by the grass draw file, which is included after this one. Same
   shape as the water and remaster forward declarations at cnc_eyes.cpp:6669-6670.

   grass_draw() belongs after the opaque and cutout passes so a blade depth tests against
   hulls, and BEFORE shroud_dispatch_draw so the blanket covers the blades as well as the
   ground under them. That second bound is the load bearing one: placed after the blanket
   instead, grass over mapped but unseen ground comes out 1 / (1 - a) brighter than the
   ground it stands in, which is the bright reveal ring the shroud note above measures.
   Any slot in that window is still a long way before fx_world_end, so the whole post
   chain runs over the finished picture. It draws ALPHA BLENDED with GL_DEPTH_TEST on,
   glDepthMask(GL_FALSE), no alpha test and no culling, and it writes NOTHING into the
   ground normal pass. That is the keystone: because grass leaves no depth behind, the
   terrain's own normal triangle still fills the pixel at alpha 1.0, and the post chain
   lights a blade exactly as it lights the ground beside it. There is therefore no N dot L
   anywhere in the grass shader, and no second grass pass.

   THE CRUSH FILE owns the LIVE half of the field, the half this file deliberately
   does not bake: the per corner light term (oneSun ? 160/255 : terrain_shade) times
   shroud_corner_vis, the per corner CM TINT that terrain_tint_shrouded returns for
   that same shroud fraction, and the crush field. Once per engine tick, never per
   frame.

   THE TINT IS HALF OF THAT HANDOFF, not an extra. The layer is on unless --nocmtint
   turns it off, and draw_terrain applies it to every corner it draws whenever
   terrain_tint_live() is true (cnc_eyes.cpp:6813, :6927-6930). So the blade's root
   colour is

       (stored atlas RGB - tint) * light

   with the subtraction BEFORE the multiply, which is the ground's own combiner
   (cnc_eyes.cpp:6600-6628) and is also why the ground cannot fold the tint into a
   vertex colour and pays for a second texture stage instead: the subtracted amount
   does not depend on the texel. A root that keeps what the ground beside it subtracted
   holds up to 25 of 255 on level ground on the shipped packs, about 16 output levels
   once the light is applied, and the tint is per channel rather than grey, so the
   blade reads warm as well as bright exactly where the colour map varies. That is the
   sticker the ground colour comment above names, at a fifth of its size.

   THE UNIFORMS THIS BAKE DEFINES, and which the draw must not invent for itself:
       uPosScale   1.0 / g_grassPosScale                world cells a vertex unit
       uSideScale  1.0 / GRASS_SIDE_ONE
       uB2TScale   1.0 / GRASS_B2T_ONE
       uSdScale    1.0 / GRASS_SD_STEP                  cells a sd unit
       uSdRange    g_grassSdRange                       where the baked distance saturates
       uFieldScale (1.0/g_gridW, 1.0/g_gridH)           g_grassGroundTex, at the ROOT
       uSlotCut    sc / (GRASS_SLOTS - 1)               with count = tile.cum[(int)sc]
       uSkirt      live height * cot(top row)           the band sd fades over

   uSkirt IS A COTANGENT, AND ITS ANGLE IS THE TOP ROW'S. A blade of live height h covers
   h * cot(elevation) of ground behind it, so the band widens as the view flattens;
   h / cos(pitch) is a length along the ray rather than a distance on the ground and
   moves the wrong way. Elevation varies down the screen, so one uniform for the whole
   frame has to take the top row's, pitch - fovy/2, or the skirt is short by nearly a
   factor of three exactly where the ground is seen most obliquely.

   THE SKIRT IS WHY sd IS IN THE VERTEX. Where a blade overlaps an object or the void the
   keystone inverts: the pixel's depth belongs to the object, the ground normal buffer's
   alpha is 0, the ground's own sun term is skipped and the ambient occlusion weight
   doubles against ssao_ground, which is a dark halo one blade tall around the base of
   every structure and every unit. WATER AND THE PLAY RECTANGLE'S BORDER ARE NOT ON THAT
   LIST, and nothing here may be sized from a halo at either of them. The ground normal
   pass (fx_draw_terrain_normals) redraws every shown cell with the alpha test off and
   does not skip the holed ones, and the sea floor under them is built from the same
   corner heights, so that quad passes its own depth and a sea pixel reaches the light
   pass at alpha 1, lit exactly as the sand beside it. The terrain margin is four cells
   and those cells are drawn and lit the same way, so the void that does invert the
   keystone begins four cells OUTSIDE the play rectangle, further out than a blade
   reaches. The fix is that no blade may put ink over an object edge. The STATIC skirt,
   water and the play rectangle's border, is kept for its own reason instead, which is
   that a blade has no business standing in the sea or on the map's outer ring: this file
   measures the distance and the draw fades the blade to nothing across it, and that band
   is sized from the blade's own reach. For the MOVING edges, buildings and vehicles, the
   same skirt has to be added to the flatten field's footprint, and that field is not this
   file's. */
static bool grass_draw(void);
/* The live half of the field is the crush file's, and it is two functions rather than
   one: grass_crush_note_tick() records the tick with no GL, grass_crush_sim() replays
   it. Neither is declared here, because neither is called from this file. */

/* ---- what this file does not do, stated rather than left to be found -------------- *
 *
 * 1. A BUILDING PAD MOVES THE GROUND UNDER BAKED VERTICES. g_padOn and g_padH level the
 *    corners under a structure (corner_raw, cnc_eyes.cpp:1182-1186), so terrain_y changes
 *    after this bake ran and the feet of blades on a tile that straddles a pad no longer
 *    touch the ground. The same is true of the editor's height brush, which invalidates
 *    g_shadeReady in six places for the same underlying reason. There is no incremental
 *    path here. The mask, the coverage and the block flags are deliberately KEPT rather
 *    than freed after the bake, and grass_emit_tile is written to take a destination
 *    pointer, so rewriting the affected tiles through gr_glBufferSubData is a caller and
 *    not a rewrite. On a 64x64 map keeping them costs 296 KB against a 26 MB buffer.
 *
 * 2. A TEXSET FLIP READS THE SAME ATLAS OFF THE GPU TWICE, once here and once in
 *    water_field_sync. Sharing one readback is worth doing and needs a change in
 *    water_mod.h, which this file does not own.
 *
 * 3. THE SKIRT IS BAKED FROM THE HEIGHT DIAL'S RANGE CEILING, not from its value, so the
 *    height slider stays live. The consequence is that sd saturates at 1.216 cells on
 *    the defaults, and the draw's fade band, computed from the LIVE height and the
 *    frame's own top row, is what actually decides the skirt. Widening the height dial's
 *    RANGE, as opposed to moving it, does invalidate the buffer, which is why
 *    height_ceiling and vary_ceiling are in the configuration the sync guard compares.
 *
 * 4. NO RESIDENCY RING. A grid above GRASS_GRID_MAX is refused rather than windowed.
 *    One shipped pack is 256x256 and two are 128x128, so this is a real refusal on real
 *    files, not a hypothetical one.
 */

#endif /* CNC3D_GRASS_BAKE_H */
