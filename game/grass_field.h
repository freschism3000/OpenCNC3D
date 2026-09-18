/* ====================================================================================
 *  grass_field.h -- THE CRUSH FIELD: where the grass is pressed flat, and how dark it
 *  stands. One map-wide RGBA16F target, four channels, no ping-pong.
 *
 *  WHAT THIS FILE IS NOT. It is not the grass MASK and it is not the grass COLOUR.
 *  Those are read once at load from the terrain sheets (the cartridge atlas for the
 *  mask, the DRAWN atlas for the colour) and they never change while a mission runs.
 *  This file holds the half that DOES change: what the battle has done to the ground
 *  since the mission started, plus the shroud, so a blade knows both.
 *
 *      R  TREAD      a vehicle drove here. Decays slowly. Two bands a vehicle, laid
 *                    along the path it actually took.
 *      G  COVER      the ground is CLAIMED right now: a building's footprint, a
 *                    vehicle's hull, plus one blade height of skirt around each.
 *                    Pinned at 1.0 while the claim stands, decays when it is released,
 *                    which is how grass grows back over a razed footprint.
 *      B  SHROUD     shroud_corner_vis at this point, written per CORNER from the same
 *                    coverage array the shroud blanket's own vertex fade uses.
 *      A  reserved, held at 1.0. Nothing reads it.
 *
 *  The blade reads flat = clamp(max(R, G), 0, 1) and multiplies its colour by B.
 *
 *  WHY THE SHROUD IS IN HERE AT ALL, which is the least obvious part of the design.
 *  The shroud blanket rides 0.11 world units above the ground with depth writes OFF.
 *  A blade at the shipped height stands 0.13 to 0.20 cells tall, which is well above
 *  that, so the blade is IN FRONT of the blanket and depth-rejects it: lit grass
 *  standing on unexplored map, and no draw order fixes it because the blade wins the
 *  depth test honestly. So the shroud multiplies the blade in the blade's own shader,
 *  and it is read from HERE rather than from a second sampler because it has to be the
 *  same per-corner coverage the blanket uses (game/shroud_mod.h:567, and the account
 *  at game/cnc_eyes.cpp:6786-6800 of why the terrain takes the same byte). Two systems
 *  reading two different shroud numbers is how a feather band ends up with a bright
 *  fringe of grass along it.
 *
 *  WHY NO PING-PONG. The water field ping-pongs because the wave equation reads its
 *  NEIGHBOURS (the laplacian at game/water_mod.h:855-857). Every update here reads only
 *  its own texel, so the second target and the whole update shader go away. There is no
 *  GLSL program in this file at all: the decay is a blended quad, the stamps are
 *  blended triangle strips, and the shroud is a Gouraud strip per cell row. Fixed
 *  function, immediate mode, one framebuffer.
 *
 *  ENHANCED ONLY, and the two halves are gated on DIFFERENT things because they run at
 *  different moments. The draw half is gated on g_fxActive, which means the chain is
 *  running right now and is only ever true inside the world pass. The TICK half is not:
 *  it is called from the object refresh, between frames, with that flag down, so it is
 *  gated on g_fx.enabled, the tier switch, which is stable across a frame. Gating the
 *  tick half on g_fxActive silently records nothing, ever. Both also need the
 *  framebuffer entry points fx_gl.h resolves. CLASSIC and the Win98 build never reach
 *  either half and must stay byte identical. The one entry point this file needs that fx_gl.h does not resolve is
 *  glBlendEquation, and it is resolved HERE, in its own optional group with its own
 *  ready flag, precisely so that a driver without it loses the grass and keeps bloom,
 *  shadows, water and trees. It must never be added to fx_gl.h at FX_REQ.
 * ==================================================================================== */
#ifndef CNC3D_GRASS_CRUSH_FIELD_H
#define CNC3D_GRASS_CRUSH_FIELD_H

/* ------------------------------------------------------------------------------------
 *  WHERE THIS FILE SITS IN cnc_eyes.cpp, and the three lines that call it.
 *
 *  Include it directly after water_mod.h (game/cnc_eyes.cpp:6979). Everything it reads
 *  is above that point: SimObject and g_objects (2317, 2510), smooth_key (2888),
 *  SMOOTH_JUMP_CELLS (2794), g_bldCellNow (3249), shroud_corner_vis (shroud_mod.h,
 *  included at 438), cell_in_view and the view rectangle (6422, 6468), g_pack.mesh and
 *  MODE_SHADOW (915, 963), water_is_boat (water_mod.h:829). mesh_for is forward
 *  declared at 3283, which is all a call needs.
 *
 *  THE TICK HALF AND THE DRAW HALF ARE SEPARATE ON PURPOSE, and this is the one place
 *  where this file departs from the water precedent deliberately. The running-track
 *  phase already had to learn it (game/cnc_eyes.cpp:2806-2810): "a draw does not happen
 *  on every tick under a script, and a phase that advanced per drawn frame would make
 *  two --shot runs of the same script disagree." A tread mark is exactly that kind of
 *  phase, and it is permanent, so it is worse. So:
 *
 *    1. Forward declare beside the other per-tick observers, around line 3283:
 *           static void grass_crush_note_tick(void);
 *    2. Call it in the once-per-tick block in refresh_objects, next to
 *       bld_footprints_age() (game/cnc_eyes.cpp:4043). It touches NO GL. It records
 *       what the tick did.
 *    3. Call grass_crush_sim() in draw_frame, immediately before the grass draw, which
 *       goes after draw_smudges() and before draw_tiberium_solid()
 *       (game/cnc_eyes.cpp:20390-20394). It replays the recorded ticks into the field
 *       and touches nothing else.
 *
 *  grass_crush_sim() returns 1 when a field exists to be sampled. If it returns 0 THE
 *  GRASS MUST NOT DRAW AT ALL. That is not a preference. Without the field there is no
 *  cover channel, so blades stand through buildings and vehicles, and the keystone of
 *  the whole feature (the blade writes no depth, so the ground's own normal triangle
 *  lights it) inverts wherever a blade overlaps an object: the scene depth belongs to
 *  the object, the ground lambert is skipped, the occlusion weight doubles, and every
 *  building and unit wears a dark halo one blade tall. Grass with no crush field is not
 *  degraded grass, it is broken grass.
 * ---------------------------------------------------------------------------------- */

/* ---- Tokens ---------------------------------------------------------------------
   Spelled out the way fx_gl.h spells its own: a token is a number, and the two
   platforms disagree about which header already carries which. */
#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB              0x881A
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD                 0x8006
#endif
#ifndef GL_MIN
#define GL_MIN                      0x8007
#endif
#ifndef GL_MAX
#define GL_MAX                      0x8008
#endif
#ifndef GL_FUNC_SUBTRACT
#define GL_FUNC_SUBTRACT            0x800A
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#define GL_FUNC_REVERSE_SUBTRACT    0x800B
#endif
#ifndef GL_BLEND_EQUATION_RGB
#define GL_BLEND_EQUATION_RGB       0x8009
#endif
#ifndef GL_BLEND_EQUATION_ALPHA
#define GL_BLEND_EQUATION_ALPHA     0x883D
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB            0x80C8
#endif
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB            0x80C9
#endif
#ifndef GL_BLEND_DST_ALPHA
#define GL_BLEND_DST_ALPHA          0x80CA
#endif
#ifndef GL_BLEND_SRC_ALPHA
#define GL_BLEND_SRC_ALPHA          0x80CB
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING      0x8CA6
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM          0x8B8D
#endif

/* ---- The one optional entry point, in its own group -------------------------------
   glBlendEquation is GL_VERSION_1_4. macOS declares it; the mingw GL/gl.h this file is
   also compiled against is frozen at GL_VERSION_1_1 and declares neither it nor
   glBlendFuncSeparate, so NEITHER may be named directly anywhere below or the Windows
   cross build stops compiling. glBlendFuncSeparate is not needed at all: nothing in
   this tree ever sets a separate RGB and alpha blend function (grep gives zero hits
   across game/), so plain glBlendFunc restores faithfully, and the restore checks that
   assumption and says so out loud if it is ever false.

   ITS OWN READY FLAG, NEVER fx_gl.h's FX_REQ. fx_gl.h:181-186 makes fx_gl_ready
   all-or-nothing: one missing required pointer switches off bloom, shadows, water and
   trees together. A driver with no EXT_blend_minmax should lose exactly one feature. */
typedef void (APIENTRY *GRASSPFNBLENDEQUATION)(GLenum);
static GRASSPFNBLENDEQUATION grass_glBlendEquation = 0;
static int  g_crushGlTried = 0;
static int  g_crushGlReady = 0;

static int grass_crush_gl_load(void)
{
    if (g_crushGlTried) return g_crushGlReady;
    g_crushGlTried = 1;
    void* p = SDL_GL_GetProcAddress("glBlendEquation");
    if (!p) p = SDL_GL_GetProcAddress("glBlendEquationEXT");
    grass_glBlendEquation = (GRASSPFNBLENDEQUATION)p;
    g_crushGlReady = grass_glBlendEquation ? 1 : 0;
    if (!g_crushGlReady)
        fprintf(stderr, "GRASS|crush|glBlendEquation is not exported by this driver; "
                        "the crush field cannot run and the grass stays off\n");
    return g_crushGlReady;
}

/* THE FALLBACK THAT WAS CONSIDERED AND REJECTED, so nobody adds it later thinking it
   was overlooked. Multiplicative decay needs no new entry point at all: GL_FUNC_ADD
   with glBlendFunc(GL_ZERO, GL_SRC_COLOR) gives dst *= src in plain GL 1.1, and
   additive stamps with the reader clamping would replace GL_MAX. It is rejected because
   GL 2.1 does not specify the blend rounding mode. On a driver that truncates, dst *= d
   loses one ulp a step whatever d is; on a driver that rounds to nearest, 1.0 * d rounds
   back to 1.0 for every d above 0.999756 and the texel NEVER CHANGES AGAIN. That is
   tread marks that never fade and, because the same quad carries the cover channel,
   grass that never grows back on any razed footprint, for the rest of the session, with
   no GL error and nothing in the log. The subtractive decay below decreases under every
   rounding mode. One feature switched off loudly beats one feature frozen silently. */

/* ---- Size, and the cap it announces rather than hides -----------------------------
   16 texels a cell is chosen against the measured track width, not copied from the wave
   field's 12. A Medium Tank's single band is 0.113 cells of authored width; at 12 texels
   a cell that is 1.36 texels, at 16 it is 1.81. The gauge between the two tracks
   (0.78 of the beam, 0.40 cells on an MTNK) is 6.4 texels at 16, so the two tracks stay
   separate rather than merging into one swathe.

   THE CAP IS IN BYTES, not in width, because bytes are what actually runs out, and it
   is announced every time it bites. 32 MiB with the table it produces:

       map        res   texels        RGBA16F
       62 x 62     16    992 x 992     7.51 MiB     every shipped mission map
       64 x 64     16   1024 x 1024    8.00 MiB
      128 x 128    16   2048 x 2048   32.00 MiB     exactly at the cap
      256 x 256     8   2048 x 2048   32.00 MiB     HALVED, and it says so

   For scale, the wave field already spends two RGBA16F targets at 768 x 768, 9.00 MiB,
   on a 64-cell map, and there is only one target here because there is no ping-pong.

   WHAT THE HALVE COSTS ON A 256-CELL MAP, stated here rather than discovered: one texel
   becomes 0.125 cells, the one-texel floor below widens every band to 0.25 cells of core
   plus 0.25 of shoulder, and a Mammoth's two tracks (centres 0.547 cells apart) merge
   into a single swathe about a cell wide. Every lighter vehicle merges harder. That is a
   visible quality step between a 64-cell mission and an XL one, and it belongs in the
   dial's help text. Do not assume 64: one shipped pack is 256 x 256, and the renderer's
   ceiling (C3D_MAP_MAX, game/c3d_ceiling.h:45) is 256. */
#ifndef GRASS_CRUSH_RES
#define GRASS_CRUSH_RES        16
#endif
#ifndef GRASS_CRUSH_MAXBYTES
#define GRASS_CRUSH_MAXBYTES   (32 * 1024 * 1024)
#endif
#define GRASS_CRUSH_BPT        8            /* bytes a texel, RGBA16F */

/* THE DECAY GRID. One fp16 ulp in [0.5, 1) is 2^-11. Every per-tick step below is
   quantised to an integer multiple of it, and that one line is what makes the decay
   EXACT: any fp16 value in [0, 1] lies on a grid at least as fine as 2^-11, so
   v - k*2^-11 is exactly representable and no rounding of any mode ever occurs. The
   field is then bit identical on every driver rather than "bit identical on one machine
   and close on another", which is the strongest determinism claim anything in this
   chain makes. The price is a coarse ladder at the slow end (see the table below) and
   it is worth paying. */
#define GRASS_CRUSH_ULP        (1.0f / 2048.0f)
#define GRASS_CRUSH_TICKS_HZ   15.0f        /* the brain's own rate */

/* Ticks of backlog past which the field is CLEARED instead of replayed. The replay is
   one decay quad plus that tick's stamps, per tick, and it must not be capped by
   dropping steps: dropping them is what makes the fade rate follow the frame rate,
   which is the defect this whole file is arranged to avoid. 120 ticks is eight seconds
   of game time with no drawn frame, which means something else has already gone very
   wrong, and a clear is honest where an under-decay is a lie. */
#define GRASS_CRUSH_MAXSTEPS   120

/* Above this many cells the shroud channel is refreshed only over the view rectangle
   plus a margin, rather than over the whole map. THE FULL-MAP REFRESH IS NOT FREE, and
   an earlier note here said it was: at 128 x 128 it is 128 triangle strips of 258
   vertices, so 33,024 vertices in immediate mode, each with a colour call and a
   position call of its own, which makes it the most expensive single thing this file
   draws. It is affordable because crush_shroud_dirty pays it only when the shroud has
   actually changed, rather than once an engine tick. 256 x 256 would be four times the
   vertices and is refused. See crush_shroud_pass. */
#define GRASS_CRUSH_SHROUD_FULL_CELLS  (128 * 128)
#define GRASS_CRUSH_SHROUD_MARGIN      6.0f     /* cells beyond the view rectangle */

/* THE FLAT END OF THE CAMERA, in radians, and it is the SMALL pitch. n64_pitch is an
   elevation, positive looking down (game/cnc_eyes.cpp:5525), so of the cartridge's two
   the near zoom's 0.78 is the flattest tilt the rig reaches and the far zoom's 0.92 is
   the most top-down (game/cnc_eyes.cpp:5457-5458). The clearance a blade needs is
   h*cot(pitch), which GROWS as the camera flattens, so the pitch that binds is the
   SMALLEST one reachable and never the largest.

   The skirt is sized from a FIXED pitch rather than from the live one so that it is a
   constant: a skirt that grew and shrank with the zoom would leave a ring of regrowing
   grass around every building each time the player zoomed in, on the cover channel's
   own timescale.

   This constant is only the cartridge half of the bound. Grass is Enhanced only, and
   there the isometric row replaces the zoom-linked lerp with a tilt dial floored just
   above half the field of view (game/cnc_eyes.cpp:5531-5536), which is 26 degrees at
   the shipped 48 degree fov and flatter than anything the cartridge reaches. That floor
   moves only when a dial moves, never with the zoom, so crush_skirt_cells folds it in
   and the skirt still stands still while a mission runs. The editor's free camera is
   left out on purpose: it is unbounded below, and reading it would put the skirt back
   on the frame. */
#ifndef GRASS_CRUSH_PITCH_FLAT
#define GRASS_CRUSH_PITCH_FLAT  0.78f
#endif

/* ---- The dials -------------------------------------------------------------------
   Kept in a struct with defaults so this file compiles and runs before fx_state.h grows
   a row, and so the balance of the feature is in data rather than in literals scattered
   through the passes. THE ROWS TO ADD, in fx_state.h's own shape, beside the water_wake
   group at fx_state.h:735-745:

     { FXP_FLOAT, "crush_tread_life", "tread mark life (s)", FXO(crush_tread_life),
       0.07f, 136.0f, 0, "how long a vehicle's tracks take to fade to nothing" },
     { FXP_FLOAT, "crush_cover_life", "regrowth (s)", FXO(crush_cover_life),
       0.07f, 136.0f, 0, "how long grass takes to grow back over ground a building or a
       vehicle has left" },
     { FXP_FLOAT, "crush_gauge", "track spacing", FXO(crush_gauge), 0.0f, 1.2f, 0,
       "how far apart a vehicle's two tracks are, as a fraction of its beam" },
     { FXP_FLOAT, "crush_band", "track width", FXO(crush_band), 0.0f, 0.6f, 0,
       "how wide one track is, as a fraction of the beam; never thinner than one texel
       of the field" },
     { FXP_FLOAT, "crush_press", "track depth", FXO(crush_press), 0.0f, 1.0f, 0,
       "how flat a track presses the grass; 1 flattens it completely" },
     { FXP_FLOAT, "crush_skirt", "clearance", FXO(crush_skirt), 0.0f, 2.0f, 0,
       "how far past a building or a hull the grass is cleared, in blade heights" },

   and one bridge line at the top of grass_crush_sim, which is the only place the two
   ever meet:  g_crushDials.tread_life = g_fx.crush_tread_life;  and so on, plus
   g_crushDials.blade_height = g_fx.grass_height.

   THE LIFE DIALS ARE IN SECONDS, not in a per-tick factor, and that is deliberate. A
   slider whose whole useful range is 0.97 to 0.9999 is three hundredths of travel and
   nobody can tune on it. Seconds also state the ceiling honestly: one ulp a tick is the
   slowest step that is guaranteed to move at all, which is 2048 ticks, which is 136.5
   seconds. Anything longer is clamped and announced.

   THE LADDER, because the quantisation is coarse at the slow end and the panel should
   not pretend otherwise. steps of k ulps -> achieved life:
       k=1  136.5 s     k=2  68.3 s     k=3  45.5 s     k=4  34.1 s
       k=6   22.8 s     k=8  17.1 s     k=16  8.5 s     k=32  4.3 s
   The defaults below land on k=2 (68.3 s of tread) and k=8 (17.1 s of regrowth), and
   grass_crush_sim prints the achieved value rather than the asked-for one whenever the
   debug flag is set. */
struct GrassCrushDials {
    float tread_life;      /* seconds for a tread mark to reach nothing            */
    float cover_life;      /* seconds for grass to grow back over released ground  */
    float gauge;           /* track centres apart, as a fraction of the beam       */
    float band;            /* one track's width, as a fraction of the beam         */
    float press;           /* how hard a track presses, 0..1                       */
    float skirt;           /* clearance past a silhouette, in blade heights        */
    float blade_height;    /* cells: g_fx.grass_height. The skirt is measured in it */
};
static GrassCrushDials g_crushDials = {
    60.0f,      /* tread_life:  lands on 2 ulps a tick, an achieved 68.3 s        */
    18.0f,      /* cover_life:  lands on 8 ulps a tick, an achieved 17.1 s        */
    0.78f,      /* gauge                                                          */
    0.22f,      /* band                                                           */
    1.00f,      /* press                                                          */
    1.00f,      /* skirt, in blade heights. See the geometry note at crush_skirt   */
    0.13f       /* blade_height, until g_fx.grass_height exists                    */
};

/* ---- The target ------------------------------------------------------------------ */
static FxRT   g_crushRT;
static int    g_crushRes = 0;               /* texels a cell, after the cap           */
static int    g_crushW = 0, g_crushH = 0;   /* texels                                 */
static int    g_crushGridW = 0, g_crushGridH = 0;
static char   g_crushScen[20] = "";
static int    g_crushFrame = -1;            /* the last engine tick stamped into it    */
static int    g_crushHalveSaid = 0;
static int    g_crushMaxTex = 0;

/* Raised when the driver refuses the half-float target, so that the allocation is
   attempted once and not once a drawn frame for the rest of the session. A driver that
   refuses it refuses it for the same reason every later frame, and the refusal path
   frees the target and leaves g_crushW at zero, which is exactly what the size test in
   the sim reads as a field not built yet: without this the whole create, check and free
   of a texture up to the byte cap would run sixty times a second on the weakest card in
   the range. It lives with the other target state rather than with the entry-point flag
   because it is a fact about the target. Cleared when the field is dropped, so a new
   mission asks the driver once more rather than inheriting the last one's verdict. */
static int    g_crushRTRefused = 0;

/* The shroud half tracks its own reasons to redraw: the tick it was written on, the
   integer view rectangle it covered, and A COPY OF THE SHROUD IT DREW.

   The copy is the load-bearing part. Blue survives every other pass in this file
   untouched, because the decay, the stamps and the floor all run with the colour mask
   on R and G, so rewriting blue with the values it already holds is work with no
   result. A tick counter cannot tell that case from a real one: the corner coverage
   field is dirtied on every shroud fetch, which is once an engine tick whether or not
   a single cell byte moved, so anything keyed to the tick redraws the whole channel
   fifteen times a second to put back exactly what was there.

   What is kept is the per-cell SOURCE the coverage field is averaged out of, not the
   corner floats: a quarter of the bytes, valid without a rebuild, and independent of
   whether the ground pass has already run this frame. It costs one byte a cell of the
   renderer's ceiling. */
static int    g_crushShroudFrame = -2;
static int    g_crushShroudX0 = 0, g_crushShroudZ0 = 0, g_crushShroudX1 = -1, g_crushShroudZ1 = -1;
static unsigned char g_crushShroudCells[sizeof g_shroudCell];
static int    g_crushShroudGate = -1;
static int    g_crushShroudGX = 0, g_crushShroudGY = 0;
static int    g_crushShroudGW = -1, g_crushShroudGH = -1;
static int    g_crushShroudCornW = -1, g_crushShroudCornH = -1;

/* ---- What one tick did ------------------------------------------------------------
   Flat arrays with per-tick ranges rather than a vector of vectors: the queue is drained
   whole on every sim call, so three vectors and two indices are the entire structure.

   A tick's worth of a full skirmish is 150 vehicles and 142 building runs, about 5 KB.
   The backlog is capped at GRASS_CRUSH_MAXSTEPS ticks, so the queue cannot exceed about
   600 KB even in the worst case, and in practice it holds one tick. */
struct CrushVeh {
    float x, z;            /* engine truth at this tick, in cells                  */
    float px, pz;          /* engine truth at the previous tick; equal if none      */
    float beam, len;       /* the hull, in cells, from its own mesh                 */
    float dirx, dirz;      /* unit heading along the path taken                     */
    unsigned char moved;   /* did it travel this tick                               */
    unsigned char haveDir; /* has it ever travelled                                 */
};
struct CrushRect { float x0, z0, x1, z1; };   /* claimed ground, cells, no skirt yet  */
struct CrushTick { int frame, vehFirst, vehCount, rectFirst, rectCount; };

static std::vector<CrushVeh>  g_crushVeh;
static std::vector<CrushRect> g_crushRect;
static std::vector<CrushTick> g_crushTick;
static std::vector<CrushRect> g_crushRectExtra;   /* see grass_crush_note_rect */

/* Where each vehicle stood last tick. KEYED ON smooth_key, NEVER ON o.id.
   game/cnc_eyes.cpp:3308-3312 records why: units and aircraft are separate heaps that
   share one integer id space, and a heap slot is RECYCLED into a new object on the tick
   after the old one died. Keyed on the bare id, the first tick of a replacement tank
   finds its predecessor's last position and sweeps a tread mark from the war factory
   door to wherever that one died. The SMOOTH_JUMP_CELLS guard below is the second
   defence against the same thing and against reinforcement drops, and it is the same
   threshold the movement filter and the running track already use. */
struct CrushPrev { float x, z; float dirx, dirz; int lastFrame; unsigned char haveDir; };
static std::map<int, CrushPrev> g_crushPrev;
static int g_crushNoteFrame = -1;
static int g_crushOverflow  = 0;   /* the backlog passed the replay cap: clear, do not trim */

/* ---- The hull, measured off the mesh ---------------------------------------------
   NOT cart_dimw_for_mesh, and the two reasons are both measured.
   It returns max(halfX, halfZ), which is the hull's LENGTH, and a tread gauge needs the
   BEAM, which is the other one. And it then puts that through the console's health-bar
   arithmetic, *48/1000 with a floor of 8, and that floor exists to keep a health bar
   visible: through it BIKE, ARTY, JEEP and BGGY all collapse to the same number and a
   Recon Bike lays tracks as wide as a Mammoth.

   Measured off SCM01EA.pack by walking the same non-MODE_SHADOW triangles
   cart_dimw_for_mesh walks (game/cnc_eyes.cpp:11565-11576), beam = 2*min(halfX,halfZ)
   in cells, and reproduced independently by the review:

     BIKE 0.289  ARTY 0.355  JEEP 0.373  BGGY 0.414  LTNK 0.418  APC 0.441  FTNK 0.445
     STNK 0.453  MSAM 0.496  MTNK 0.514  HARV 0.543  MCV 0.680   HTNK 0.701

   min() rather than "the X extent" because BOAT is authored across rather than along
   (halfX 846, halfZ 227); boats are excluded from this pass entirely, but a rule that
   only works because of an exclusion is a rule waiting to break.

   AND IT BOUNDS-CHECKS THE MESH INDEX, which is the guard the design dropped and
   cart_dimw_for_mesh keeps at game/cnc_eyes.cpp:11561: mesh_for returns -1 both when the
   type is not in the pack and when it is but at conf < 1, and g_pack.mesh[-1] on a
   std::vector is undefined behaviour on exactly the stale-pack path this project keeps
   hitting. A missing mesh falls back to the median hull instead, so the silhouette is
   still cleared and the marks are still plausible. */
struct CrushHull { float beam, len; };
static std::map<int, CrushHull> g_crushHullCache;

static CrushHull crush_hull_for_mesh(int mi)
{
    CrushHull h;
    /* The median of the table above, for a type whose model the pack cannot resolve. */
    h.beam = 0.441f; h.len = 0.900f;
    if (mi < 0 || mi >= (int)g_pack.mesh.size())
        return h;
    std::map<int, CrushHull>::const_iterator it = g_crushHullCache.find(mi);
    if (it != g_crushHullCache.end())
        return it->second;
    float xs = 0.0f, zs = 0.0f;
    const std::vector<PackTri>& tris = g_pack.mesh[mi].tris;
    for (size_t i = 0; i < tris.size(); i++) {
        if (tris[i].mode == MODE_SHADOW)
            continue;                       /* the shadow blob is not the object */
        for (int k = 0; k < 3; k++) {
            const float ax = fabsf(tris[i].v[k].x), az = fabsf(tris[i].v[k].z);
            if (ax > xs) xs = ax;
            if (az > zs) zs = az;
        }
    }
    if (xs > 0.0f || zs > 0.0f) {
        const float a = 2.0f * (xs < zs ? xs : zs) * MODEL_SCALE;
        const float b = 2.0f * (xs > zs ? xs : zs) * MODEL_SCALE;
        if (a > 0.02f) h.beam = a;
        if (b > 0.02f) h.len  = b;
    }
    g_crushHullCache[mi] = h;
    return h;
}

/* ---- Small helpers ---------------------------------------------------------------- */

static float crush_texel_cells(void)
{
    return g_crushRes > 0 ? 1.0f / (float)g_crushRes : 1.0f / (float)GRASS_CRUSH_RES;
}

/* THE SKIRT: how far past a silhouette the ground must be cleared, in cells.
   The keystone of the whole feature is that a blade writes no depth, so the terrain's
   own triangle still fills the ground-normal buffer behind it at alpha 1.0 and the post
   chain lights the blade exactly as it lights the ground beside it. That inverts
   wherever a blade overlaps an OBJECT: there the scene depth belongs to the object, the
   ground normal's alpha is 0, the ground lambert is skipped and the occlusion weight
   doubles, which draws a dark halo one blade tall around every building and unit. The
   fix is geometric and it is here: the cover footprint is grown so that no blade can
   overlap a silhouette in the first place.

   The number. A blade of height h projects h*cos(pitch) up the screen; a ground offset
   of d toward the camera projects d*sin(pitch). They are equal at d = h*cot(pitch), so
   cot is the factor, and it is taken at the FLATTEST pitch this tier can reach because
   that is where it is largest: 1.01 blade heights at the cartridge's near zoom, 2.05 at
   the 26 degree isometric floor the shipped field of view allows. Sizing it off the
   TOP-DOWN pitch instead, or off 1/cos, understates it at the flat end, which is the
   end that actually costs a halo.

   The isometric floor is folded in whenever the perspective row is on, including at the
   tilt dial's 0 where the console lerp still runs. That is one step wider than the frame
   strictly needs and it is the point: dragging the tilt dial then never resizes the
   skirt, and the constant above stays constant.

   The result is then held at no less than 1.65 blade heights, which is the clearance
   this file has always cut and is worth keeping: a blade that leans in the wind reaches
   further than a blade standing up, and the cost of being generous is a slightly wider
   bare margin, while the cost of being tight is a halo the whole feature gets blamed
   for. */
static float crush_skirt_cells(void)
{
    float pitch = GRASS_CRUSH_PITCH_FLAT;
    if (cam_option_active()) {
        const float isoFloor = (cam_fovy_deg() * 0.5f + 2.0f) * (float)M_PI / 180.0f;
        if (isoFloor < pitch) pitch = isoFloor;
    }
    float clear = 1.0f / tanf(pitch);
    if (clear < 1.65f) clear = 1.65f;   /* never tighter than this file has always cut */
    float s = g_crushDials.blade_height * clear * g_crushDials.skirt;
    if (s < 0.0f) s = 0.0f;
    return s;
}

/* The per-tick decay step, quantised onto the fp16 grid. See GRASS_CRUSH_ULP. */
static float crush_step_for_life(float seconds)
{
    if (seconds < 0.01f) seconds = 0.01f;
    const float raw = 1.0f / (seconds * GRASS_CRUSH_TICKS_HZ);
    int k = (int)(raw / GRASS_CRUSH_ULP + 0.5f);
    if (k < 1) k = 1;                         /* the ceiling: 2048 ticks, 136.5 s */
    if (k > 2048) k = 2048;                   /* the floor:   one tick             */
    return (float)k * GRASS_CRUSH_ULP;
}

static GLuint grass_crush_tex(void)   { return g_crushRT.tex; }
static int    grass_crush_res(void)   { return g_crushRes; }
static int    grass_crush_ready(void) { return g_crushRT.tex != 0 && g_crushW > 0; }

/* The blade samples the field at uv = (root.x / gridW, root.z / gridH), which is the
   same convention the water field already uses (game/water_mod.h:1530). */
static void grass_crush_uv_scale(float* sx, float* sy)
{
    *sx = g_gridW > 0 ? 1.0f / (float)g_gridW : 0.0f;
    *sy = g_gridH > 0 ? 1.0f / (float)g_gridH : 0.0f;
}

/* WHAT THE BLADE SHADER MUST DO WITH THE FOUR CHANNELS. Given here as text so that the
   two sides cannot drift apart, and because two of the three lines are not obvious.

   The clamp on R and G is not decoration. The decay is a subtraction with no clamp of
   its own (a float target does not clamp a blend result the way a fixed-point one does),
   and the field is only pushed back up to zero once per sim call, so a texel that has
   been empty for a while reads slightly negative. One max() absorbs it.

   The FETCH SPELLING DIFFERS BY STAGE in GLSL 1.20: the vertex stage has no implicit
   derivative, so it uses texture2DLod(f, uv, 0.0), and texture2DLod is vertex-only in
   1.20; the fragment stage uses plain texture2D. There is exactly one mip level, so the
   two read the same texels. Vertex texture fetch with GL_LINEAR is measured working on
   this driver (GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 16), and the shroud must be read there
   rather than in the fragment stage so a card fades as one thing. */
static const char* GRASS_CRUSH_GLSL_DECODE =
    "/* f is the RGBA the crush field returned. */\n"
    "float grass_crush_flat(vec4 f)   { return clamp(max(f.r, f.g), 0.0, 1.0); }\n"
    "float grass_crush_shroud(vec4 f) { return clamp(f.b, 0.0, 1.0); }\n";

/* ---- Allocation ------------------------------------------------------------------
   fx_rt_init cannot be used: it is RGBA8 by construction and says so at fx_gl.h:330-333.
   This is the same two dozen lines as wave_rt_init (game/water_mod.h:884-908) with the
   format changed and the failure message made specific. GL_LINEAR on both filters
   because the blade samples between texels and because vertex texture fetch is measured
   to filter correctly. */
static int crush_rt_init(FxRT* rt, int w, int h)
{
    if (rt->fbo && rt->w == w && rt->h == h) return 1;
    fx_rt_free(rt);
    glGenTextures(1, &rt->tex);
    glBindTexture(GL_TEXTURE_2D, rt->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F_ARB, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    fx_glGenFramebuffers(1, &rt->fbo);
    fx_glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    fx_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt->tex, 0);
    const GLenum st = fx_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    rt->w = w; rt->h = h; rt->depth = 0;
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        /* Said once, in the same shape as the other refusals in this file. The caller
           stops asking after a refusal, so a repeat could only be a later mission
           meeting the same driver. */
        static int said = 0;
        if (!said) {
            said = 1;
            fprintf(stderr, "GRASS|crush|half-float target %dx%d refused (0x%x); "
                            "the grass stays off\n", w, h, (unsigned)st);
        }
        fx_rt_free(rt);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return 1;
}

static void grass_crush_free(void)
{
    fx_rt_free(&g_crushRT);
    g_crushRTRefused = 0;           /* a new mission may ask for a target once more */
    g_crushW = g_crushH = g_crushRes = 0;
    g_crushGridW = g_crushGridH = 0;
    g_crushScen[0] = 0;
    g_crushFrame = -1;
    g_crushShroudFrame = -2;
    g_crushShroudX1 = -1;
    g_crushVeh.clear(); g_crushRect.clear(); g_crushTick.clear();
    g_crushRectExtra.clear();
    g_crushPrev.clear();
    g_crushNoteFrame = -1;
    g_crushOverflow = 0;
    /* THE HULL CACHE IS KEYED ON A MESH INDEX INTO THE PACK, SO IT CANNOT OUTLIVE ONE.
       Freeing the pack invalidates every index and the next mission's load renumbers
       them, so a surviving entry answers with whatever hull now sits at that index. Too
       wide and a Recon Bike lays Mammoth tracks; too narrow and the vehicle's flanks
       fall outside their own cover rectangle, where the blades overlap the silhouette
       and draw the dark halo this whole mechanism exists to prevent. Cleared in the
       free and not in the reset above it: the reset runs on a save load, while the free
       is what the mission-to-mission teardown calls, and the effects pass's own mesh
       caches record being fixed on the load path alone and leaving the mission change
       broken. */
    g_crushHullCache.clear();
}

/* The whole per-mission state, dropped on a scenario change. The field is renderer
   memory of a battle and a new mission has not had one yet. The hull cache goes with
   it: a hull is measured from a MESH INDEX, and the indices belong to the pack, so
   grass_crush_free drops that too. */
static void grass_crush_reset(void)
{
    grass_crush_free();
}

/* ====================================================================================
 *  THE TICK HALF. No GL. Called once per engine tick from refresh_objects.
 * ==================================================================================== */

/* An extra source of claimed ground, in cells, before the skirt. Craters, scorch marks,
   concrete aprons, walls and tiberium all suppress grass in exactly the same way a
   building does and all of them already arrive per cell in the dump; none of them is in
   this file's scope, so the hook is here and the caller is elsewhere. Call it from the
   per-tick parse BEFORE grass_crush_note_tick, which drains it. */
static void grass_crush_note_rect(float x0, float z0, float x1, float z1)
{
    CrushRect r; r.x0 = x0; r.z0 = z0; r.x1 = x1; r.z1 = z1;
    g_crushRectExtra.push_back(r);
}

static void grass_crush_note_tick(void)
{
    /* CLASSIC AND WIN98 RECORD NOTHING. Grass exists only on the Enhanced tier, so a
       tier that will never draw it must not pay for a grid walk fifteen times a second
       either. Switched off and on again, the field is correct from the tick after,
       because the queue's own backlog cap empties it and the draw half then clears the
       field rather than replaying a partial history: a switched-off field is also a
       freshly cleared one. */
    /* NOT g_fxActive, AND THAT IS THE WHOLE OF A BUG THIS ALMOST SHIPPED WITH.
       g_fxActive means "the chain is running RIGHT NOW", and it is raised by the world
       pass and lowered again when the chain presents. This function is not called from
       the world pass: it is called once per engine tick, from the object refresh, which
       runs between frames with the flag down. Guarded on it, this returned early on
       every tick a mission ever ran, the queue was empty on every drawn frame, and
       nothing was ever stamped into the field. The grass still drew, because the draw
       does not need the queue; what silently did not exist was every tread mark, the
       cover under every building and hull, and the shroud channel. A feature that looks
       finished and does none of the four things it was asked for.
       g_fx.enabled is the tier switch and it is stable between frames, so it carries the
       CLASSIC promise this guard was actually written for: Classic and the Win98 build
       leave it at 0 and never record a thing. */
    if (!g_fx.enabled || !g_fx.grass) { g_crushRectExtra.clear(); return; }

    /* SELF-DEDUPLICATED ON THE FRAME NUMBER. refresh_objects is called more than once
       for some ticks: the shot path re-dumps and the interactive loop re-dumps on a
       pause (game/cnc_eyes.cpp:4029-4034 makes the same point about efx_step). A second
       record for one tick would decay the field twice for it. */
    if (g_engineFrame == g_crushNoteFrame) { g_crushRectExtra.clear(); return; }
    g_crushNoteFrame = g_engineFrame;

    /* A BACKLOG THIS LONG IS CLEARED, NOT TRIMMED, and the difference is the whole
       argument of this file. Trimming, which is what "stop growing the queue" quietly
       amounts to and what the wave sim does on purpose (at most four steps a frame,
       game/water_mod.h:1000-1002), makes the amount of decay a function of how the ticks
       happened to batch into drawn frames: one in four in the interactive loop against
       thirty to one under a script. Measured on the first build of this file: 125 ticks
       of backlog left the mark at 0.8828 where 125 steps should have left 0.8779, i.e.
       five ticks vanished. A wave dies in two seconds and can afford that; a mark that
       is meant to last a minute cannot.

       So past the cap the queue is emptied, the flag is raised, and this tick becomes
       the only one in it. The draw half then clears the field rather than replaying a
       partial history, which is a discontinuity the player can see once instead of a
       quiet lie that never stops. Eight seconds of game time with no drawn frame means
       something else has already gone very wrong. */
    if ((int)g_crushTick.size() >= GRASS_CRUSH_MAXSTEPS) {
        g_crushOverflow = 1;
        g_crushTick.clear();
        g_crushVeh.clear();
        g_crushRect.clear();
    }

    CrushTick t;
    t.frame     = g_engineFrame;
    t.vehFirst  = (int)g_crushVeh.size();
    t.rectFirst = (int)g_crushRect.size();
    t.vehCount  = 0;
    t.rectCount = 0;

    /* ---- CLAIMED GROUND, straight off the footprint map the renderer already keeps.
       g_bldCellNow is stamped for every non-limboed K_BUILDING over its own fw x fh
       (game/cnc_eyes.cpp:3853-3862), sentinel -1 (3530), aged once per tick (4043). It
       is the engine's own answer to "is this cell claimed right now", it is already
       maintained, and walking it costs one pass over the live grid rather than a walk
       of g_objects with the footprint arithmetic repeated.

       Run-length encoded per row, then each run is one rectangle. The skirt is a square
       dilation and dilation distributes over a union, so dilating each run separately
       and letting them overlap gives exactly the dilation of the whole footprint set,
       whatever shape the set is. Overlap costs nothing: the stamps are GL_MAX. */
    for (int z = 0; z < g_gridH; z++) {
        int x = 0;
        while (x < g_gridW) {
            if (g_bldCellNow[z * g_gridW + x] < 0) { x++; continue; }
            const int x0 = x;
            while (x < g_gridW && g_bldCellNow[z * g_gridW + x] >= 0) x++;
            CrushRect r;
            r.x0 = (float)x0; r.z0 = (float)z;
            r.x1 = (float)x;  r.z1 = (float)(z + 1);
            g_crushRect.push_back(r);
            t.rectCount++;
        }
    }
    for (size_t i = 0; i < g_crushRectExtra.size(); i++) {
        g_crushRect.push_back(g_crushRectExtra[i]);
        t.rectCount++;
    }
    g_crushRectExtra.clear();

    /* ---- THE VEHICLES.
       Every exclusion below is a bug that would otherwise be shipped:

         K_UNIT only. K_AIRCRAFT flies (o.alt is in leptons, game/cnc_eyes.cpp:2348), so
           an Orca overhead would grind a trench along its flight path. A landed
           helicopter arguably should press the grass and that is one extra test on
           o.alt, left out because the requirement says vehicles.
         not water_is_boat (game/water_mod.h:829-834). BOAT and LST are on water, where
           there is no grass, and BOAT is the one hull authored across rather than along.
         not o.limbo. game/cnc_eyes.cpp:3310-3313 records that a harvester docking at a
           refinery goes limbo=1 and STAYS IN THE DUMP for the whole 599-tick dock.
           Without this test it sits at the refinery door pinning the field for forty
           seconds and grinding a permanent bald patch there.
         id >= 0, or smooth_key is meaningless.

       POSITION IS ENGINE TRUTH. o.wx is assigned from o.ex in the parse
       (game/cnc_eyes.cpp:3798) and only the DRAW overwrites it with the smoothed value,
       so at this point the two are the same number and the record is free of the
       sub-tick phase. What the player sees is that truth put through a five-tap box
       filter, which lags it by about 1.5 ticks: 0.075 cells at a Medium Tank's 0.05
       cells a tick, or 1.2 texels of the field at 16 a cell, which is inside the band's
       own one-texel floor. Stated rather than left to be discovered, because the
       alternative is stamping from the draw and making the mark a function of the frame
       rate. */
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_UNIT || o.id < 0 || o.limbo) continue;
        if (water_is_boat(o)) continue;

        const int key = smooth_key(o);
        const CrushHull hull = crush_hull_for_mesh(mesh_for(o));

        CrushVeh v;
        v.x = o.ex; v.z = o.ez;
        v.px = o.ex; v.pz = o.ez;
        v.beam = hull.beam; v.len = hull.len;
        v.dirx = 0.0f; v.dirz = 1.0f;
        v.moved = 0; v.haveDir = 0;

        std::map<int, CrushPrev>::iterator p = g_crushPrev.find(key);
        if (p != g_crushPrev.end()) {
            const float dx = o.ex - p->second.x, dz = o.ez - p->second.z;
            const float d  = sqrtf(dx * dx + dz * dz);
            /* A TELEPORT IS NOT A DRIVE. The same threshold and the same reason as the
               running track (game/cnc_eyes.cpp:2830-2834) and the movement filter
               (2794): beyond one cell in one tick the history is not this journey. A
               recycled heap slot, a reinforcement drop and a transport unloading all
               land here, and without the guard each of them paints a tread line across
               the map. */
            if (d > 1.0e-5f && d <= SMOOTH_JUMP_CELLS) {
                v.px = p->second.x; v.pz = p->second.z;
                v.dirx = dx / d; v.dirz = dz / d;
                v.moved = 1; v.haveDir = 1;
            } else if (p->second.haveDir) {
                v.dirx = p->second.dirx; v.dirz = p->second.dirz;
                v.haveDir = 1;
            }
        }
        g_crushVeh.push_back(v);
        t.vehCount++;

        CrushPrev np;
        np.x = o.ex; np.z = o.ez;
        np.dirx = v.dirx; np.dirz = v.dirz;
        np.haveDir = v.haveDir;
        np.lastFrame = g_engineFrame;
        g_crushPrev[key] = np;
    }

    /* PRUNED, or the map grows for the life of the process and a slot reused after a
       long gap inherits a stale position. Thirty ticks is two seconds, the same TTL the
       wake's boat list uses (game/water_mod.h:998-1000). */
    for (std::map<int, CrushPrev>::iterator it = g_crushPrev.begin(); it != g_crushPrev.end(); ) {
        if (g_engineFrame - it->second.lastFrame > 30) {
            std::map<int, CrushPrev>::iterator dead = it++;
            g_crushPrev.erase(dead);
        } else {
            ++it;
        }
    }

    g_crushTick.push_back(t);
}

/* ====================================================================================
 *  THE DRAW HALF. All the GL, and all of the state discipline.
 * ==================================================================================== */

/* Everything the world pass is entitled to get back exactly as it handed it over.
   STRICTLY LARGER THAN THE WAVE SIM'S LIST, and that is the point of writing it down:
   water_wave_sim saves five bits (game/water_mod.h:1046-1051) and relies on
   fx_fullscreen_quad to leave blend off, and it never touches the blend function, the
   blend equation, the colour mask, the scissor or the bound program. This pass sets all
   of them. The bug that shape of mistake already cost this project was three frames
   correct and one drawn flat, over and over, reported as flashing shadows. */
struct CrushGLSave {
    GLboolean depth, blend, alpha, cull, scissor, tex2d;
    GLboolean depthMask;
    GLboolean colMask[4];
    GLint     srcRGB, dstRGB, srcA, dstA;
    GLint     eqRGB, eqA;
    GLint     fbo;
    GLint     bind2d;
    GLint     prog;
    GLint     vp[4];
    GLfloat   col[4];
};

static void crush_gl_save(CrushGLSave* s)
{
    s->depth   = glIsEnabled(GL_DEPTH_TEST);
    s->blend   = glIsEnabled(GL_BLEND);
    s->alpha   = glIsEnabled(GL_ALPHA_TEST);
    s->cull    = glIsEnabled(GL_CULL_FACE);
    s->scissor = glIsEnabled(GL_SCISSOR_TEST);
    s->tex2d   = glIsEnabled(GL_TEXTURE_2D);
    s->depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &s->depthMask);
    /* THE GL DEFAULT GOES IN FIRST, for the same reason the depth mask above gets one.
       A refused query leaves its destination untouched, and CrushGLSave is a local, so
       a refused query hands the restore a stack value to pass to glBlendFunc. That call
       is then refused in turn and is a no-op, which leaves the blend function at this
       pass's own GL_ONE, GL_ONE and composites every translucent draw after the grass
       additively: the shroud blanket, the tiberium decals, the tree cutouts. Exactly the
       symptom the blend equation note below is written to avoid.
       The blend factor tokens are GL 1.4 and the alpha equation is GL 2.0, but the gate
       for reaching this code is only that glBlendEquation resolved, which EXT_blend_minmax
       alone satisfies, and the shader chain accepts the ARB object spellings, so both
       gates can pass on a context older than either. */
    s->colMask[0] = s->colMask[1] = s->colMask[2] = s->colMask[3] = GL_TRUE;
    glGetBooleanv(GL_COLOR_WRITEMASK, s->colMask);
    s->srcRGB = GL_ONE;   s->dstRGB = GL_ZERO;
    s->srcA   = GL_ONE;   s->dstA   = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB,   &s->srcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &s->dstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s->srcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &s->dstA);
    s->eqRGB = GL_FUNC_ADD;   s->eqA = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_EQUATION_RGB,   &s->eqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s->eqA);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s->fbo);
    /* THE BOUND TEXTURE ON THE ACTIVE UNIT, which allocation destroys. crush_rt_init
       has to bind its new texture to upload the level, and the wave field's own
       initialiser leaves a bare 0 behind afterwards (game/water_mod.h:907). Inside a
       world pass that is the terrain atlas unbound in the middle of the ground draw.
       The ACTIVE UNIT itself is never changed by this pass, so it is not saved: no
       glActiveTexture is called anywhere below. */
    s->bind2d = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->bind2d);
    glGetIntegerv(GL_VIEWPORT, s->vp);
    glGetFloatv(GL_CURRENT_COLOR, s->col);
    s->prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &s->prog);
}

static void crush_gl_restore(const CrushGLSave* s)
{
    /* THE FRAMEBUFFER GOES BACK TO THE EXACT NAME THAT WAS BOUND, not to g_fxScene.
       water_wave_sim asserts fx_rt_bind(&g_fxScene) on the way out (water_mod.h:1075),
       which is right only while the chain has a scene target. Restoring what was read
       is also correct when the post chain failed to allocate and the world is drawing
       straight to the default framebuffer. */
    fx_glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s->fbo);
    glViewport(s->vp[0], s->vp[1], s->vp[2], s->vp[3]);
    glBindTexture(GL_TEXTURE_2D, (GLuint)s->bind2d);

    if (s->depth)   glEnable(GL_DEPTH_TEST);   else glDisable(GL_DEPTH_TEST);
    if (s->blend)   glEnable(GL_BLEND);        else glDisable(GL_BLEND);
    if (s->alpha)   glEnable(GL_ALPHA_TEST);   else glDisable(GL_ALPHA_TEST);
    if (s->cull)    glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);
    if (s->scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (s->tex2d)   glEnable(GL_TEXTURE_2D);   else glDisable(GL_TEXTURE_2D);
    glDepthMask(s->depthMask);
    glColorMask(s->colMask[0], s->colMask[1], s->colMask[2], s->colMask[3]);

    /* THE BLEND EQUATION IS THE ONE THAT WOULD NOT LOOK LIKE THIS FILE'S FAULT. Leave
       GL_MAX behind and the next translucent thing the world draws composites with a
       maximum instead of a sum, which reads as a lighting bug somewhere else entirely.
       Both equations are put back, not just the RGB one. */
    if (grass_glBlendEquation) {
        if (s->eqRGB == s->eqA) {
            grass_glBlendEquation((GLenum)s->eqRGB);
        } else {
            /* Nothing in this tree sets them apart, and restoring a split pair would
               need glBlendEquationSeparate, which the frozen Windows header does not
               declare either. Say so rather than silently collapsing it. */
            static int said = 0;
            if (!said) {
                said = 1;
                fprintf(stderr, "GRASS|crush|separate RGB/alpha blend equations were in "
                                "force (0x%x/0x%x); restoring the RGB one to both\n",
                        (unsigned)s->eqRGB, (unsigned)s->eqA);
            }
            grass_glBlendEquation((GLenum)s->eqRGB);
        }
    }
    /* Same story for the factors. glBlendFuncSeparate is absent from the mingw GL 1.1
       header, so it is never named; nothing in game/ sets a separate pair, and the one
       case where that stops being true is reported rather than guessed at. */
    if (s->srcRGB == s->srcA && s->dstRGB == s->dstA) {
        glBlendFunc((GLenum)s->srcRGB, (GLenum)s->dstRGB);
    } else {
        static int said2 = 0;
        if (!said2) {
            said2 = 1;
            fprintf(stderr, "GRASS|crush|separate RGB/alpha blend factors were in force "
                            "(0x%x,0x%x / 0x%x,0x%x); restoring the RGB pair to both\n",
                    (unsigned)s->srcRGB, (unsigned)s->dstRGB,
                    (unsigned)s->srcA,   (unsigned)s->dstA);
        }
        glBlendFunc((GLenum)s->srcRGB, (GLenum)s->dstRGB);
    }

    glColor4f(s->col[0], s->col[1], s->col[2], s->col[3]);
    if (fx_glUseProgram) fx_glUseProgram((GLuint)s->prog);
}

/* THE QUAD IS EMITTED HERE AND NOT BY fx_fullscreen_quad, AND THAT IS NOT A STYLE
   CHOICE. fx_fullscreen_quad (fx_gl.h:419-441) disables GL_DEPTH_TEST, GL_BLEND,
   GL_ALPHA_TEST and GL_CULL_FACE and restores none of them, and forces the depth mask to
   GL_TRUE. THE DECAY PASS IS A BLEND. Routing it through that helper would turn a
   subtraction into a plain overwrite: the whole field would be set to a flat constant
   every tick, every blade would read uniformly half crushed, no tread would ever appear,
   and there would be no GL error and nothing in the log. Silent and total. The helper is
   a trap for whoever calls it from inside the world pass, it has already cost this
   project one shipped bug, and this is the caller it was waiting for. */
static void crush_quad(float x0, float z0, float x1, float z1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, z0);
    glVertex2f(x1, z0);
    glVertex2f(x1, z1);
    glVertex2f(x0, z1);
    glEnd();
}

/* One track band, swept from where the shoulder was to where it is.
   FLAT TOPPED WITH SOFT SHOULDERS, not a tent, and the difference is the whole
   difference between a mark and no mark. A tent has a zero-width peak, and the measured
   bands are 1.02 to 2.47 texels wide at 16 a cell: rasterised as a tent, a Recon Bike
   driving due east leaves a mean of 0.023 against 1.000 at 45 degrees, i.e. nothing at
   all, and a Mammoth leaves a mark 40 per cent weaker east than north-east. C&C units
   move on an eight-direction grid, four of which are cardinal, so every vehicle's track
   would visibly brighten and dim as it turned a corner.

   So: a core at full value whose HALF WIDTH IS FLOORED AT ONE TEXEL, which guarantees a
   fully covered texel centre at any heading, plus a one-texel ramp either side for the
   soft edge. The same lesson the wake had to learn twice, in its own words: "A press
   must never be smaller than the grid that carries it" (game/water_mod.h:805-810) and
   "never below one texel of the field, or the press falls between samples" (991-994).

   Six vertices, one strip, no texture and no uniform array. Because the strip IS the
   swept segment it cannot leave the gaps the wake's segDist was written to close, and
   there is no per-vehicle cap: forty vehicles cost eighty of these, where the wake's
   uniform array (WAVE_MAXBOATS = 8, game/water_mod.h:813) would have dropped thirty-two
   of them on the floor. */
static void crush_band(float ax, float az, float bx, float bz,
                       float nx, float nz, float core, float soft, float press)
{
    const float o1 = core, o2 = core + soft;
    glBegin(GL_TRIANGLE_STRIP);
    glColor4f(0.0f, 0.0f, 0.0f, 0.0f);
    glVertex2f(ax - nx * o2, az - nz * o2);
    glVertex2f(bx - nx * o2, bz - nz * o2);
    glColor4f(press, 0.0f, 0.0f, 0.0f);
    glVertex2f(ax - nx * o1, az - nz * o1);
    glVertex2f(bx - nx * o1, bz - nz * o1);
    glVertex2f(ax + nx * o1, az + nz * o1);
    glVertex2f(bx + nx * o1, bz + nz * o1);
    glColor4f(0.0f, 0.0f, 0.0f, 0.0f);
    glVertex2f(ax + nx * o2, az + nz * o2);
    glVertex2f(bx + nx * o2, bz + nz * o2);
    glEnd();
}

/* The hull's own claim on the ground, in the COVER channel and at full value.
   Separate from the tread and not optional: without it, "stamped only where a vehicle
   MOVED this tick" means a tank that holds position for a minute is standing in lush
   grass and a defensive line reads as untouched meadow. It is also the vehicle half of
   the silhouette clearance, so it carries the same skirt a building's footprint does.

   Oriented along the path taken when there is one, and a SQUARE when there is not: a
   vehicle that has never moved has no heading this pass is entitled to (o.face is the
   hull's own heading, but for a unit that is not turret-equipped the brain writes the
   secondary facing exactly once at Unlimbo and never again, and the primary is not
   worth a second source of truth here), and a square of the hull's LENGTH is guaranteed
   to contain the true footprint at any heading. */
static void crush_hull_cover(const CrushVeh& v, float skirt)
{
    const float hl = 0.5f * v.len  + skirt;
    const float hb = 0.5f * v.beam + skirt;
    glColor4f(0.0f, 1.0f, 0.0f, 0.0f);
    if (!v.haveDir) {
        /* The half-diagonal, not the half-length: a rectangle of half-extents (a, b)
           turned by atan(b/a) has an axis-aligned half-width of sqrt(a*a + b*b), which
           for a Medium Tank is 0.626 cells against a half-length of 0.571. A square
           built on the length alone would leave 0.055 cells of the hull's corner
           uncovered at a diagonal facing, which is a blade and a half of overlap and
           therefore a dark notch on exactly two corners of a parked tank. */
        const float a = 0.5f * v.len, b = 0.5f * v.beam;
        const float r = sqrtf(a * a + b * b) + skirt;
        crush_quad(v.x - r, v.z - r, v.x + r, v.z + r);
        return;
    }
    const float fx = v.dirx, fz = v.dirz;
    const float nx = -fz,    nz = fx;
    glBegin(GL_QUADS);
    glVertex2f(v.x - fx * hl - nx * hb, v.z - fz * hl - nz * hb);
    glVertex2f(v.x + fx * hl - nx * hb, v.z + fz * hl - nz * hb);
    glVertex2f(v.x + fx * hl + nx * hb, v.z + fz * hl + nz * hb);
    glVertex2f(v.x - fx * hl + nx * hb, v.z - fz * hl + nz * hb);
    glEnd();
}

/* ---- The shroud channel ----------------------------------------------------------
   Under the SOFT blanket, written per CORNER from shroud_corner_vis, which is one minus
   the same per-corner coverage array the blanket's own vertex fade uses
   (game/shroud_mod.h:559-585). That sharing is the entire point: the blade and the
   blanket cannot then disagree about where the reveal edge is, exactly as the terrain's
   own vertex alpha cannot (game/cnc_eyes.cpp:6786-6800). The HARD blanket is a different
   shape, one flat square a cell, and is written per cell to match: crush_shroud_cell_vis
   below, and the account there of why the corner value cannot be used with it.

   ONE STRIP A CELL ROW, emitted (x, z) then (x, z+1), which makes the two triangles
   (NW, SW, NE) and (NE, SW, SE): the SW-NE diagonal, which is the split draw_terrain
   chose for itself at game/cnc_eyes.cpp:6755-6760 and the one terrain_y samples. The
   interpolation inside a cell therefore matches the ground under it rather than merely
   resembling it.

   THE HALF-TEXEL BIAS, said out loud. A corner sits on a texel BOUNDARY of this field,
   not on a texel centre, so a bilinear read of what was rasterised is offset by half a
   texel: 0.031 cells at 16 texels a cell, 0.063 at 8. The shroud is a feathered field
   several cells across, so this is invisible, but it is a real half-texel and somebody
   measuring the edge should know it is there.

   WHY THE WHOLE MAP IS NOT ALWAYS REWRITTEN. On 128 x 128 cells and below it is: 128
   strips of 258 vertices is exact everywhere, including ground the camera has not
   looked at yet, and crush_shroud_dirty pays for it only on the ticks the shroud
   actually moved. Above that, only the view rectangle plus a margin is
   refreshed, and a cell that has not been refreshed since the shroud lifted there reads
   DARKER than the truth for one refresh. That is the safe direction of staleness and it
   is the one the contract cares about: the failure this channel exists to prevent is lit
   grass on unexplored map, never dark grass on explored map. */
static void crush_shroud_rect(int* ox0, int* oz0, int* ox1, int* oz1)
{
    int x0 = 0, z0 = 0, x1 = g_gridW, z1 = g_gridH;
    const int cells = g_gridW * g_gridH;
    if (cells > GRASS_CRUSH_SHROUD_FULL_CELLS && g_viewValid) {
        x0 = (int)floorf(g_viewX0 - GRASS_CRUSH_SHROUD_MARGIN);
        z0 = (int)floorf(g_viewZ0 - GRASS_CRUSH_SHROUD_MARGIN);
        x1 = (int)ceilf (g_viewX1 + GRASS_CRUSH_SHROUD_MARGIN);
        z1 = (int)ceilf (g_viewZ1 + GRASS_CRUSH_SHROUD_MARGIN);
        if (x0 < 0) x0 = 0;
        if (z0 < 0) z0 = 0;
        if (x1 > g_gridW) x1 = g_gridW;
        if (z1 > g_gridH) z1 = g_gridH;
    }
    *ox0 = x0; *oz0 = z0; *ox1 = x1; *oz1 = z1;
}

/* THE SWITCHES shroud_corner_vis tests before it consults the coverage field, as one
   word: the editor override, the terrain-light switch, the hard shroud path, the rim,
   the master switch, whether a snapshot is valid, and the end-of-match reveal. Any of
   them moving changes what this pass would draw without changing one cell byte. */
static int crush_shroud_gate(void)
{
    return (g_shroudEditorOff ?  1 : 0) | (g_shroudTerrainLight ?  2 : 0)
         | (g_shroudSoft      ?  4 : 0) | (g_shroudRimOn        ?  8 : 0)
         | (g_shroudOn        ? 16 : 0) | (g_shroudValid        ? 32 : 0)
         | (g_shroudRevealed  ? 64 : 0);
}

/* How many cell bytes of the snapshot this pass depends on, clamped to the mirror. */
static int crush_shroud_cells(void)
{
    const int n = (g_shroudGW > 0 && g_shroudGH > 0) ? g_shroudGW * g_shroudGH : 0;
    return n > (int)sizeof g_crushShroudCells ? (int)sizeof g_crushShroudCells : n;
}

/* HAS THE SHROUD ITSELF MOVED since the blue channel was written? Everything
   shroud_corner_vis can return is a function of four things: the gate word above, the
   exported grid rectangle the unliftable rim is cut from, the corner grid's stride, and
   the per-cell snapshot the coverage field is averaged out of. Compare all four and the
   answer is EXACT. A comparison rather than a hash, on purpose: a hash that collided
   would leave the channel holding the previous reveal, and lit grass on ground the
   player has not uncovered is the one failure this channel exists to prevent. */
static int crush_shroud_changed(void)
{
    const int n = crush_shroud_cells();
    if (crush_shroud_gate() != g_crushShroudGate) return 1;
    if (g_shroudGX != g_crushShroudGX || g_shroudGY != g_crushShroudGY ||
        g_shroudGW != g_crushShroudGW || g_shroudGH != g_crushShroudGH) return 1;
    if (g_shroudCornW != g_crushShroudCornW ||
        g_shroudCornH != g_crushShroudCornH) return 1;
    return n > 0 && memcmp(g_crushShroudCells, g_shroudCell, (size_t)n) != 0;
}

static void crush_shroud_remember(void)
{
    const int n = crush_shroud_cells();
    g_crushShroudGate  = crush_shroud_gate();
    g_crushShroudGX    = g_shroudGX;    g_crushShroudGY    = g_shroudGY;
    g_crushShroudGW    = g_shroudGW;    g_crushShroudGH    = g_shroudGH;
    g_crushShroudCornW = g_shroudCornW; g_crushShroudCornH = g_shroudCornH;
    if (n > 0) memcpy(g_crushShroudCells, g_shroudCell, (size_t)n);
}

/* Nothing to do when neither the shroud nor the covered rectangle has moved since the
   last write, which is now what this actually tests. It used to compare the engine's
   tick counter, and that counter advances whether the shroud moved or not, so the
   whole channel was rewritten fifteen times a second with the values it already held. */
static int crush_shroud_dirty(void)
{
    int x0, z0, x1, z1;
    crush_shroud_rect(&x0, &z0, &x1, &z1);
    if (x1 <= x0 || z1 <= z0) return 0;
    if (g_crushShroudFrame < 0) return 1;      /* nothing written into blue yet */
    if (g_crushShroudX0 != x0 || g_crushShroudZ0 != z0 ||
        g_crushShroudX1 != x1 || g_crushShroudZ1 != z1) return 1;
    return crush_shroud_changed();
}

/* THE HARD BLANKET HAS NO PER-CORNER TERM, so the corner value cannot be used with it.
   shroud_corner_vis returns 1.0 for every corner in that mode (game/shroud_mod.h:577),
   and that is right for the GROUND: the hard path covers a cell with one flat square,
   and a corner average would dim explored ground whose neighbour merely happens to be
   unseen, with no blanket over it to explain the dimming. It is wrong for a BLADE, which
   stands above the blanket rather than under it. Taken as written the channel reads 1.0
   over the whole map while every unexplored cell is painted opaque black beneath it:
   fully lit grass on unexplored ground at maximum contrast, which is the one failure
   this channel exists to prevent, and silent, with no GL error to find it by.

   So on that path the channel is written per CELL, flat, from the same state the
   blanket's own two passes test (game/shroud_mod.h:402-438): HIDDEN is painted opaque
   and leaves nothing, DARK is painted at SHROUD_DARK_ALPHA and leaves the rest, CLEAR is
   not painted at all. Read from shroud_state_at rather than from shroud_opacity because
   this has to mirror what is actually DRAWN: the hard path carries no reveal test of its
   own, so with the rim on and the map revealed it still paints, and a value that called
   that ground open would put the leak straight back.

   The two guards below are the ones shroud_corner_vis carries (game/shroud_mod.h:575-582)
   and they are here for the same reason: with the editor's override on, or with the
   shroud and its rim both out of play, no blanket is drawn over this cell at all, and
   grass dimmed under nothing is the same defect the other way round. */
static float crush_shroud_cell_vis(int x, int z)
{
    if (g_shroudEditorOff)
        return 1.0f;
    if (!g_shroudRimOn && (!g_shroudOn || !g_shroudValid || g_shroudRevealed))
        return 1.0f;
    switch (shroud_state_at(x, z)) {
        case SHROUD_HIDDEN: return 0.0f;
        case SHROUD_DARK:   return 1.0f - SHROUD_DARK_ALPHA;
        default:            return 1.0f;
    }
}

/* Assumes the caller has bound the field, set the cell-space projection and saved the
   state it is about to change. Leaves blending DISABLED and the colour mask on blue. */
static void crush_shroud_write(int x0, int z0, int x1, int z1)
{
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE);
    if (!g_shroudSoft) {
        /* Cells [x0,x1) x [z0,z1), which is the same ground the corner strip spans. */
        glBegin(GL_QUADS);
        for (int z = z0; z < z1; z++) {
            for (int x = x0; x < x1; x++) {
                const float v = crush_shroud_cell_vis(x, z);
                glColor4f(0.0f, 0.0f, v, 0.0f);
                glVertex2f((float)x,       (float)z);
                glVertex2f((float)(x + 1), (float)z);
                glVertex2f((float)(x + 1), (float)(z + 1));
                glVertex2f((float)x,       (float)(z + 1));
            }
        }
        glEnd();
    } else {
        for (int z = z0; z < z1; z++) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int x = x0; x <= x1; x++) {
                const float a = shroud_corner_vis(x, z);
                const float b = shroud_corner_vis(x, z + 1);
                glColor4f(0.0f, 0.0f, a, 0.0f); glVertex2f((float)x, (float)z);
                glColor4f(0.0f, 0.0f, b, 0.0f); glVertex2f((float)x, (float)(z + 1));
            }
            glEnd();
        }
    }
    g_crushShroudFrame = g_engineFrame;
    g_crushShroudX0 = x0; g_crushShroudZ0 = z0;
    g_crushShroudX1 = x1; g_crushShroudZ1 = z1;
    crush_shroud_remember();
}

static void crush_shroud_pass(void)
{
    int x0, z0, x1, z1;
    crush_shroud_rect(&x0, &z0, &x1, &z1);
    if (x1 <= x0 || z1 <= z0) return;
    if (!crush_shroud_dirty()) return;
    crush_shroud_write(x0, z0, x1, z1);
}

/* ---- The sim ---------------------------------------------------------------------- */
static int grass_crush_sim(void)
{
    if (!g_fxActive) return 0;
    if (!fx_gl_ready) return 0;
    if (!grass_crush_gl_load()) return 0;
    /* A target this driver has already refused is not asked for again. A refusal leaves
       g_crushW and g_crushH at zero, which is exactly what makes the size test below
       read as fresh, so without this gate the whole allocate, check and free path would
       run again on every drawn frame for the life of the mission, along with the state
       save and restore around it. It sits after the two cheaper gates above so the
       early-outs still read in cost order. */
    if (g_crushRTRefused) return 0;
    if (g_gridW <= 0 || g_gridH <= 0) return 0;

    /* THE ONE PLACE THE PANEL AND THIS FILE MEET. Copied per frame rather than watched,
       because every one of them is cheap and a dial that is read once at boot is a dial
       the panel cannot move. blade_height is the grass's own height dial: the clearance
       is measured in blade heights, so the two cannot be allowed to disagree. */
    g_crushDials.tread_life   = g_fx.crush_tread_life;
    g_crushDials.cover_life   = g_fx.crush_cover_life;
    g_crushDials.gauge        = g_fx.crush_gauge;
    g_crushDials.band         = g_fx.crush_band;
    g_crushDials.press        = g_fx.crush_press;
    g_crushDials.skirt        = g_fx.crush_skirt;
    g_crushDials.blade_height = g_fx.grass_height;

    /* ---- size, cap, and the announcement -------------------------------------- */
    if (!g_crushMaxTex) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_crushMaxTex);
        if (g_crushMaxTex < 64) g_crushMaxTex = 2048;   /* a driver that will not say */
    }
    int res = GRASS_CRUSH_RES;
    while (res > 1 &&
           ((double)g_gridW * res * (double)g_gridH * res * (double)GRASS_CRUSH_BPT
                > (double)GRASS_CRUSH_MAXBYTES
            || g_gridW * res > g_crushMaxTex || g_gridH * res > g_crushMaxTex))
        res /= 2;
    const int w = g_gridW * res, h = g_gridH * res;
    if (w > g_crushMaxTex || h > g_crushMaxTex) {
        static int said = 0;
        if (!said) {
            said = 1;
            fprintf(stderr, "GRASS|crush|a %dx%d cell map needs a %dx%d field and this "
                            "driver's texture ceiling is %d; the grass stays off\n",
                    g_gridW, g_gridH, w, h, g_crushMaxTex);
        }
        return 0;
    }

    const int fresh = (w != g_crushW || h != g_crushH ||
                       g_crushGridW != g_gridW || g_crushGridH != g_gridH ||
                       strcmp(g_crushScen, g_pack.scen) != 0);

    if (!fresh && !g_crushRT.tex) return 0;

    /* ---- how much of the queue is actually replayed ----------------------------
       Every recorded tick is replayed, and the only cap is the one note_tick already
       applied by emptying the queue and raising the flag. There is deliberately no
       "at most N steps this frame" here: that is the shape of trimming, and trimming is
       what makes the fade follow the frame rate. */
    int nticks = (int)g_crushTick.size();
    int clearFirst = fresh || g_crushOverflow;
    if (g_crushOverflow) {
        static int said = 0;
        if (!said) {
            said = 1;
            fprintf(stderr, "GRASS|crush|more than %d engine ticks passed with no drawn "
                            "frame; the field is cleared rather than under-decayed\n",
                    GRASS_CRUSH_MAXSTEPS);
        }
        g_crushOverflow = 0;
    }

    /* Nothing at all to do: no fresh target, no tick to replay, and the shroud neither
       moved nor changed. Return without touching one bit of GL state. This is the
       common case, three drawn frames in four. */
    if (!clearFirst && nticks == 0 && !crush_shroud_dirty())
        return 1;

    const float sTread = crush_step_for_life(g_crushDials.tread_life);
    const float sCover = crush_step_for_life(g_crushDials.cover_life);
    const float texel  = crush_texel_cells();
    const float skirt  = crush_skirt_cells();
    const float press  = g_crushDials.press < 0.0f ? 0.0f :
                        (g_crushDials.press > 1.0f ? 1.0f : g_crushDials.press);

    /* EVERYTHING FROM HERE DOWN TOUCHES GL, INCLUDING THE ALLOCATION, so the save comes
       first. Allocating after the save rather than before it is not tidiness: the
       initialiser has to bind the new framebuffer and the new texture to build them, and
       the wave field's own initialiser leaves both behind (game/water_mod.h:884-908). On
       the FIRST frame of a mission that meant the world pass carried on drawing the
       ground into the crush field, once, with no error and nothing in the log. Measured
       on this GPU before it was moved. */
    CrushGLSave sv;
    crush_gl_save(&sv);

    if (fresh) {
        if (!crush_rt_init(&g_crushRT, w, h)) {
            g_crushRTRefused = 1;
            crush_gl_restore(&sv);
            return 0;
        }
        g_crushW = w; g_crushH = h; g_crushRes = res;
        g_crushGridW = g_gridW; g_crushGridH = g_gridH;
        /* THE QUEUE SURVIVES A FRESH FIELD, and only a change of MISSION empties it.
           Emptying it here unconditionally threw away every tick recorded before the
           first drawn frame, and still reported success, so the cover channel read 0.0
           under every building footprint and every hull for that frame: the blades stand
           through the silhouette, the scene depth belongs to the object, the ground
           lambert is skipped and the occlusion weight doubles, which is the dark halo
           one blade tall this channel exists to prevent. In the interactive loop that is
           the first frames of every mission. Under a script it is EVERY shot: the
           warm-up runs to completion with nothing drawn, so the first sim call is always
           the fresh one and there is never a second, and the one frame a visual gate
           sees is that one. The field then comes up uniformly zero, which is the state
           this file's own header calls broken grass rather than degraded grass.

           Nothing in the queue is stale on a resize either: every position in it is in
           CELLS, recorded against the grid that was live at that tick, no field size
           changes what a cell is, and the target has just been cleared for the replay to
           land on. Replaying onto a freshly cleared field is what the overflow path
           above already does.

           A queue carried over from the PREVIOUS mission is the one thing that really is
           stale, because its cells belong to another map, so that case is tested for by
           name rather than inferred from the field being new. The test has to read
           g_crushScen before the new name is written over it. An empty recorded name
           means no field has been built yet, which is this mission's own history and is
           kept. */
        if (g_crushScen[0] && strcmp(g_crushScen, g_pack.scen) != 0) {
            g_crushVeh.clear(); g_crushRect.clear(); g_crushTick.clear();
            g_crushPrev.clear();
            nticks = 0;
        }
        snprintf(g_crushScen, sizeof g_crushScen, "%s", g_pack.scen);
        g_crushShroudFrame = -2;
        g_crushShroudX1 = -1;
        if (res != GRASS_CRUSH_RES && !g_crushHalveSaid) {
            g_crushHalveSaid = 1;
            /* LOUD, because the alternative is a quality cliff nobody can account for.
               grass_build's own 16-bit index abort is the precedent for refusing rather
               than degrading quietly, and this one degrades, so it says exactly what it
               cost. */
            fprintf(stderr, "GRASS|crush|a %dx%d cell map at %d texels a cell would be "
                            "%.1f MiB, over the %.0f MiB cap; using %d texels a cell "
                            "instead. One texel is now %.3f cells, so every track band "
                            "is floored at %.3f cells wide and a vehicle's two tracks "
                            "merge into one swathe.\n",
                    g_gridW, g_gridH, GRASS_CRUSH_RES,
                    (double)g_gridW * GRASS_CRUSH_RES * (double)g_gridH * GRASS_CRUSH_RES
                        * GRASS_CRUSH_BPT / (1024.0 * 1024.0),
                    (double)GRASS_CRUSH_MAXBYTES / (1024.0 * 1024.0),
                    res, 1.0 / (double)res, 4.0 / (double)res);
        }
    }

    /* THE PASS'S OWN STATE. Written out rather than assumed, because this runs in the
       middle of the world pass: draw_smudges has just finished and the object meshes
       have not started, so every one of these was left in whatever shape the ground
       decals wanted. glShadeModel is NOT set: draw_frame sets GL_SMOOTH once a frame
       (game/cnc_eyes.cpp:20321), fx_post.h:846 sets it again, and nothing in the tree
       ever sets GL_FLAT, so setting it here would only add a restore that can be got
       wrong. GL_SCISSOR_TEST IS disabled: the editor's playtest containment leaves a
       scissor rectangle in WINDOW coordinates (game/cnc_eyes.cpp:138-141) and a scissor
       applies to any framebuffer, so without this the field would only ever be written
       inside a rectangle the size of the map viewport. */
    if (fx_glUseProgram) fx_glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_TEXTURE_2D);

    fx_rt_bind(&g_crushRT);

    /* CELL COORDINATES ALL THE WAY THROUGH. An orthographic projection over the live
       grid means every vertex below is a world position in cells and nothing has to be
       converted to clip space or to texels by hand. Bottom = 0 puts cell z 0 at texture
       row 0, which is the same orientation an uploaded array would have and the same
       one the water field is sampled with, uv = world.xz / (gridW, gridH). */
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0.0, (double)g_gridW, 0.0, (double)g_gridH, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

    if (clearFirst) {
        /* THE CLEAR COLOUR IS GLOBAL STATE AND IT IS PUT BACK, which is this tree's own
           idiom rather than an invention: both fx_post.h:910-914 and water_mod.h:215-219
           read GL_COLOR_CLEAR_VALUE, set their own and restore it, because the frame's
           own clear colour is chosen once in draw_frame and a pass that left a different
           one behind would paint the next frame's background. */
        GLfloat clearc[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearc);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(clearc[0], clearc[1], clearc[2], clearc[3]);
        /* The shroud is written over the WHOLE map on a fresh field whatever its size:
           a cleared blue channel reads as fully shrouded, and a mission that starts with
           a base in view must not spend a frame with black grass in it. */
        crush_shroud_write(0, 0, g_gridW, g_gridH);
    }

    /* ---- the replay: one decay step and that tick's stamps, per tick ------------- */
    glEnable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE);   /* R and G only. B is the shroud */

    for (int t = 0; t < nticks; t++) {
        const CrushTick& tk = g_crushTick[(size_t)t];

        /* DECAY. dst = dst - s, once, at a FIXED step. Reverse subtract with both
           factors GL_ONE.
           Two things are load-bearing here and both are measured.
           (1) IT IS SUBTRACTIVE, NOT MULTIPLICATIVE. GL 2.1 does not specify the blend
               rounding mode. dst *= d on a driver that rounds to nearest freezes solid
               for every d above 0.999756, which is inside the range a "how long does a
               mark last" dial wants; a subtraction of at least half an ulp decreases
               under every rounding mode there is.
           (2) IT IS ONE STEP A TICK, NEVER N TICKS BATCHED INTO ONE. Measured on this
               GPU with the multiplicative form: eight single steps at 0.9990 leave
               0.988281 where one batched step leaves 0.991699, and thirty-two leave
               0.953125 against 0.968262, so the batched form is 41 to 48 per cent less
               decayed. That would make the field a function of how the ticks batch into
               drawn frames, which is one to four in the interactive loop and thirty to
               one under a script. The step here is quantised onto the fp16 grid so the
               subtraction is exact, which removes the same class of error at the root
               rather than bounding it. */
        grass_glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(GL_ONE, GL_ONE);
        glColor4f(sTread, sCover, 0.0f, 0.0f);
        crush_quad(0.0f, 0.0f, (float)g_gridW, (float)g_gridH);

        /* STAMPS. GL_MAX, so a weaker pass can never lower a stronger one: a vehicle
           parked on its own trail cannot erase it, and two vehicles crossing leave the
           deeper of the two marks rather than a brighter sum. Measured on this GPU: a
           1.0 stamp then an overlapping 0.5 stamp read exactly the same value as the
           1.0-only region after nine hundred decays. */
        grass_glBlendEquation(GL_MAX);
        glBlendFunc(GL_ONE, GL_ONE);      /* ignored by a min/max equation; set anyway */

        /* Claimed ground, dilated by the skirt. See crush_skirt_cells. */
        glColor4f(0.0f, 1.0f, 0.0f, 0.0f);
        for (int i = 0; i < tk.rectCount; i++) {
            const CrushRect& r = g_crushRect[(size_t)(tk.rectFirst + i)];
            crush_quad(r.x0 - skirt, r.z0 - skirt, r.x1 + skirt, r.z1 + skirt);
        }

        for (int i = 0; i < tk.vehCount; i++) {
            const CrushVeh& v = g_crushVeh[(size_t)(tk.vehFirst + i)];
            /* THE HULL CLAIMS GROUND ONLY WHILE IT IS STANDING ON IT, and that one word
               is the difference between wheel marks and a mown strip. This stamp is the
               COVER channel, which does not press grass down, it removes it; it is
               written at full strength with GL_MAX, so nothing can lower it; and the
               cover clock is a slow one, seventeen seconds, because its real job is
               grass growing back over a razed footprint. Stamped every tick for a vehicle
               that is MOVING, those three facts compound into a continuous swathe of dead
               ground wider than the hull itself, laid down along the entire route and
               taking seventeen seconds to recover, with the two tread bands buried inside
               it where nobody can see them: a tank mows a lawn.
               So a moving vehicle now lays tracks and nothing else, and a stationary one
               still claims the ground it is parked on, which is what stops blades growing
               up through a hull that is sitting still. Driving away releases the claim and
               the regrowth clock runs from there, which is what that clock was written
               for. The cost of the simple form is that a blade rooted just in front of a
               moving hull can lean over its lower edge for the frames it takes to pass;
               giving the hull its own channel and a quarter-second release would remove
               even that, and is written down as the fuller answer if this one reads
               wrong in motion. */
            if (!v.moved) { crush_hull_cover(v, skirt); continue; }
            /* TWO BANDS, ONE EITHER SIDE OF THE CENTRE LINE, and the direction comes
               from the PATH rather than from the hull's facing: treads lie along the
               ground a vehicle covered, and a tank turning on the spot must not smear a
               mark sideways. The wake reached the same shape for the neighbouring
               reason, one source at each shoulder rather than one under the keel
               (game/water_mod.h:961-963). */
            const float nx = -v.dirz, nz = v.dirx;
            const float half = 0.5f * g_crushDials.gauge * v.beam;
            float core = 0.5f * g_crushDials.band * v.beam;
            if (core < texel) core = texel;      /* the one-texel floor. See crush_band */
            for (int side = 0; side < 2; side++) {
                const float sgn = side ? 1.0f : -1.0f;
                crush_band(v.px + nx * half * sgn, v.pz + nz * half * sgn,
                           v.x  + nx * half * sgn, v.z  + nz * half * sgn,
                           nx, nz, core, texel, press);
            }
        }
    }

    /* THE FLOOR, once per call rather than once per tick. A float target does not clamp
       a blend result, so a texel that is already empty goes one step negative for every
       tick of decay. One GL_MAX against zero puts it back. Doing it per call rather than
       per tick bounds the excursion at MAXSTEPS steps, which the reader's own
       max(f.r, f.g) then absorbs anyway; doing it at all is what stops a session left
       running for days from walking the channel down towards the half-float floor. */
    if (nticks > 0 || clearFirst) {
        grass_glBlendEquation(GL_MAX);
        glColor4f(0.0f, 0.0f, 0.0f, 0.0f);
        crush_quad(0.0f, 0.0f, (float)g_gridW, (float)g_gridH);
    }

    g_crushFrame = g_engineFrame;
    g_crushVeh.clear();
    g_crushRect.clear();
    g_crushTick.clear();

    /* ---- and the shroud, which is a picture of NOW rather than of the ticks ------- */
    crush_shroud_pass();

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();

    crush_gl_restore(&sv);

    if (getenv("CNC3D_GRASS_CRUSHDBG")) {
        fprintf(stderr, "CRUSH|%dx%d|res=%d|%.2f MiB|ticks=%d|clear=%d|"
                        "tread %.1fs (%d ulp)|cover %.1fs (%d ulp)|"
                        "texel %.4f cells|skirt %.3f cells|err=0x%x\n",
                g_crushW, g_crushH, g_crushRes,
                (double)g_crushW * g_crushH * GRASS_CRUSH_BPT / (1024.0 * 1024.0),
                nticks, clearFirst,
                1.0 / (sTread * GRASS_CRUSH_TICKS_HZ), (int)(sTread * 2048.0f + 0.5f),
                1.0 / (sCover * GRASS_CRUSH_TICKS_HZ), (int)(sCover * 2048.0f + 0.5f),
                (double)texel, (double)skirt, (unsigned)glGetError());
    }
    return 1;
}

/* ====================================================================================
 *  THE GATE THIS WANTS, and it should be written in the same commit as the first pixel.
 *  The trees shipped with none and paid for it with four visual rounds.
 *
 *  Every failure this file is arranged against is SILENT: a decay routed through
 *  fx_fullscreen_quad flattens the field with no error, a multiplicative decay freezes
 *  it with no error, a dropped state leaves the next pass composited with GL_MAX and
 *  looks like a lighting bug elsewhere, and an axis-aligned band that falls between
 *  texels simply is not there. So the gate must read the field, not the screen:
 *
 *    1. drive one vehicle due EAST for twenty ticks, then read the field back with
 *       glGetTexImage. Assert the tread channel's maximum along the path is above 0.9.
 *       That one line catches the tent, the missing one-texel floor and the flattened
 *       field at once. Repeat at 45 degrees and require the two means within 20 per
 *       cent of each other: the whole point is that a track does not dim as a unit
 *       turns a corner.
 *    2. step thirty ticks with nothing moving and assert the same maximum has fallen,
 *       and fallen by the amount the dial promises within one ulp. That catches the
 *       freeze and any batching.
 *    3. assert the field is neither all zero nor constant.
 *    4. stand a building on grass and assert the cover channel is 1.0 a skirt's width
 *       OUTSIDE its footprint, which is contract B measured directly rather than
 *       inferred from a screenshot of a halo.
 *    5. take five --shot runs of the same script and require identical digests, which
 *       is G36c's own rule applied to a channel that now remembers things.
 * ==================================================================================== */

#endif /* CNC3D_GRASS_CRUSH_FIELD_H */
