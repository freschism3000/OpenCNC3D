#ifndef CNC_CURSOR3D_H
#define CNC_CURSOR3D_H
/* ---------------------------------------------------------------------------------- *
 *  THE CONSOLE CURSOR
 *
 *  The cartridge's pointer is not a sprite. It is a 3D MODEL drawn in the world, on the
 *  terrain, at the picked ground point -- so it has constant WORLD size, foreshortens
 *  with the camera, shrinks with distance, and gets its "shadow" from being an EXTRUDED
 *  SLAB: a white top face over dark-grey (100,100,100) side walls that show along the
 *  lower-right edge. Underneath it, at y=0, sits a small flat textured triangle, the
 *  ground / hot-spot marker that says which cell the floating pointer is over.
 *
 *  What the ROM says, and where:
 *    - the draw site is resident code ROM 0x4D790. It reads a 4-byte record from the
 *      cursor STATE table at RAM 0x80099920 (ROM 0x9A520) indexed by the cursor's state;
 *      byte +0 is a cursor model code 0x00..0x0D, which indexes a private 16-byte NODE
 *      table at RAM 0x80099750 (ROM 0x9A350), scene-graph node pointer at +0x0C. That
 *      node table is reachable from no model-table walk, which is why the art was never
 *      found before.
 *    - "Cursor Scale" is RAM 0x80097984 = 0.25, and it is a NODE scale, not a screen
 *      size: 1024 mesh units x 0.25 = 256 leptons = exactly one cell. Our MODEL_SCALE of
 *      1/1024 already reproduces that, so the cursor meshes need no extra scale factor.
 *    - the cursor's own debug printf (ROM 0x5478) is
 *      "CX=%d CY=%d TX=%d TY=%d H=%f Tile=%d" -- it carries a FLOAT TERRAIN HEIGHT, so
 *      the console cursor rides the heightfield. screen_to_world already hands us the
 *      same input.
 *    - model-table slot 107, "CURSOR_CIRCLES", is NOT the pointer: it is a flat triangle
 *      plus two 0.05-scaled octagonal rings, almost certainly the selection ring.
 *
 *  The 1995 MOUSE.SHP sprite survives only where the console has no counterpart at all:
 *  our DOS sidebar and the pause dialog. Left open deliberately.
 * ---------------------------------------------------------------------------------- */

/* Cursor STATE table, RAM 0x80099920 / ROM 0x9A520, transcribed byte for byte.
   {model, model2, frameCount, drawNoEntryOverlay}. Byte +3 == 1 makes the draw site at
   ROM 0x4D854 put a SECOND model on top, hard-coded to state_table[3].model -- the red
   no-entry ring -- which is what pins states 14/15/19 to the DOS no-sell / no-repair
   triple and their bases to states 8/6/18. */
static const unsigned char C3D_STATE[20][4] = {
    { 0,  0,   1, 0}, { 0,  0,   1, 0}, { 1,  1,   1, 0}, { 2,  2,   1, 0},
    { 3,  3, 100, 0}, { 4,  4, 100, 0}, { 5,  5, 100, 0}, { 6,  6, 100, 0},
    { 7,  7,   1, 0}, { 8,  8, 100, 0}, { 9,  9, 100, 0}, {10, 10, 100, 0},
    {11, 11, 100, 0}, {12, 12,   1, 0}, { 7,  7,   1, 1}, { 5,  5,   1, 1},
    { 4,  4, 100, 0}, { 6,  6, 100, 0}, {13, 13,   1, 0}, {13, 13,   1, 1},
};
#define C3D_NOENTRY_CODE 2      /* the overlay model, i.e. state_table[3].model */

/* DOS MouseType -> console cursor state.
   NOW READ OUT OF THE ROM, not recognised from the art. The cartridge's own ActionType
   jump table (ROM 0x3FB0, reached from ROM 0x31F08) picks the state, and transcribing it
   fixed three cursors that the old art-derived guess had wrong:

     MOUSE_DEPLOY      12 -> 7    THIS IS THE "SQUEEZED" CURSOR. State 12's model is
                                  code 0x0B, a tall narrow stack of two down-arrows only
                                  240 mesh units wide where every other cursor is
                                  790..1010; drawn at the same 0.25 node scale it reads
                                  as a squashed sliver. The console's deploy state is 7,
                                  model 0x06, the grey four-way diamond (908 x 35 x 904).
                                  Nothing was ever wrong with the bake, the scale or the
                                  aspect: we were drawing the wrong model.
     MOUSE_ENTER        9 -> 12   9 is the AIR STRIKE hexagon.
     MOUSE_AREA_GUARD  11 -> 18   11 is the ion ground ring; 18 is the shield.

   The old comment claimed the 20-entry table holds no superweapon entry. It does:
   demolitions 13, ion cannon 11, nuclear 10, air strike 9, all of which used to fall
   through to the plain arrow. The DOS scroll arrows are a separate matter -- the console
   does have eight of them (model code 0x0E) but it selects them by testing the cursor's
   world position against the map bounds, not through this table at all, and they are
   still unimplemented; left open deliberately.

   ONE case cannot be expressed here: ACTION_TOGGLE_PRIMARY wants state 19, but
   cur_mouse_for_action() folds it into MOUSE_DEPLOY, so through a MouseType it can only
   come out as the deploy diamond. Recorded as a known gap rather than faked.
   (ACTION_HARVEST vs ATTACK and ACTION_SELF over a building vs elsewhere look like two
   more lossy cases but are not: the console's state rows 16/5 and 17/7 are byte-identical
   to each other, so the distinction is invisible.) */
static int c3d_state_for_mouse(int mt)
{
    switch (mt) {
    case MOUSE_CAN_MOVE:      return 2;
    case MOUSE_NO_MOVE:       return 3;
    case MOUSE_CAN_SELECT:    return 4;
    case MOUSE_CAN_ATTACK:    return 5;
    case MOUSE_REPAIR:        return 6;
    case MOUSE_DEPLOY:        return 7;
    case MOUSE_ENTER:         return 12;
    case MOUSE_AREA_GUARD:    return 18;
    case MOUSE_DEMOLITIONS:   return 13;
    case MOUSE_ION_CANNON:    return 11;
    case MOUSE_NUCLEAR_BOMB:  return 10;
    case MOUSE_AIR_STRIKE:    return 9;
    case MOUSE_SELL_BACK:
    case MOUSE_SELL_UNIT:     return 8;
    case MOUSE_NO_SELL_BACK:  return 14;
    case MOUSE_NO_REPAIR:     return 15;
    default:                  return 0;
    }
}

/* ---------------------------------------------------------------------------------- *
 *  THE ANIMATION
 *
 *  Ten of the twenty states above carry a frame count of 100 in byte +2, and until now
 *  every one of them drew STATIC. It is THREE mechanisms, not one, and the whole decode
 *  (with its ROM offsets and its own verification table) lives in
 *  tools/bakery/cursoranim.py -- run that file for the numbers.
 *
 *  THE CLOCK, resident code RAM 0x8004CB10 / ROM 0x04D710 and RAM 0x8004A690:
 *      frame = (frameCounter * 4) mod state[st].frameCount
 *      t     = frame * 160.0 + 1.0
 *  so a cycle is 100 anim frames and `frame` only ever takes the 25 values 0,4,8..96.
 *  Our frameCounter is g_engineFrame, the brain's own tick, for the same reason
 *  mesh_anim_t uses it: it is the only counter that makes two --shot runs identical.
 *  The cartridge's frameCounter is a RENDERED-frame counter whose wall-clock rate was
 *  not recovered from the ROM, so the CYCLE LENGTH IN SECONDS is ours, not the
 *  cartridge's (25 engine ticks = 1.67 s at our 15 Hz). Recorded as a known gap.
 *
 *  1. TRANSLATION / ROTATION (codes 0x03, 0x04, 0x06, 0x08, 0x09). The cursor models are
 *     ordinary scene-graph trees and their child nodes carry the cartridge's own keyframe
 *     tracks (FEEB0005 cubic-Bezier vec3, FEEB0002 quaternion). They ride the PKB node
 *     animation section unchanged, because one baked PackPart is exactly one scene-graph
 *     node. Code 0x06 is THAT CURSOR: the DEPLOY diamond's four arms travel exactly 200
 *     mesh units radially outward while pitching 70 degrees, then snap back when the
 *     clock wraps (the tracks CLAMP at 14400 where the cycle runs to 15361 -- that snap
 *     is the console's own).
 *
 *  2. TEXTURE FLIPBOOK (codes 0x0A, 0x0B). The node's +0x10 payload rewrites the
 *     G_SETTIMG image address every frame. Baked as per-frame MESH VARIANTS carried as
 *     ordinary type codes CUR0AF0..3 / CUR0BF0..2 (see bake_cursor_flipbooks), so no pack
 *     format changed. The time tables below are the cartridge's own, transcribed with
 *     their addresses; the index rule is RAM 0x80080FAC's: tt = (u32)t mod period, then
 *     the first slot whose upper bound is not less than tt.
 *
 *  3. THE FRAME COUNT ITSELF, SPENT AS A YAW. Code 0x05, the repair wrench, is the one
 *     model that carries frameCount 100 and NEITHER a node track NOR a flipbook, so
 *     mechanisms 1 and 2 both have nothing to drive. It used to draw dead still.
 *
 *     MEASURED, all twenty rows of the state table, one cursor3ddump each:
 *       state=4  code=0x03 frames=100 animframes=100  flipvariants=0
 *       state=5  code=0x04 frames=100 animframes=100  flipvariants=0
 *       state=6  code=0x05 frames=100 animframes=0    flipvariants=0   <-- only this one
 *       state=7  code=0x06 frames=100 animframes=100  flipvariants=0
 *       state=9  code=0x08 frames=100 animframes=100  flipvariants=0
 *       state=10 code=0x09 frames=100 animframes=100  flipvariants=0
 *       state=11 code=0x0A frames=100 animframes=0    flipvariants=4
 *       state=12 code=0x0B frames=100 animframes=0    flipvariants=3
 *       state=16 code=0x04 frames=100 animframes=100  flipvariants=0
 *       state=17 code=0x06 frames=100 animframes=100  flipvariants=0
 *     and every one of the remaining twelve rows carries frameCount 1, i.e. does not
 *     animate at all. Rows 16 and 17 repeat rows 5 and 7's models byte for byte, so the
 *     ten animating rows name eight distinct models and exactly one of them is idle.
 *
 *     SO THE FRAME COUNT IS THE SPIN. A row that says 100 and hands the evaluators
 *     nothing to evaluate is still saying a hundred of something, and the one channel
 *     left on a single-part model with no track is its own yaw. c3d_spin_face below
 *     turns the clock's frame into a facing, ONE law at ONE site, spent through the
 *     draw's ordinary facing argument.
 *
 *     WHAT DOES NOT FOLLOW FROM THIS. "The only frames=100 model with no track and no
 *     flipbook" is a property of the BAKED DATA measured above, not a rule anything
 *     enforces: a future bake that dropped a track would silently enlist another cursor
 *     into this. So the law is written as an explicit test on code 0x05 and on nothing
 *     else, and a second spinning cursor can only ever arrive by somebody editing this
 *     line. Row 15 also carries code 0x05 (the no-repair wrench under its red ring) and
 *     its frameCount is 1: it must stay still, which is why the test is on the CLOCK's
 *     answer for this draw, not on the code alone.
 * ---------------------------------------------------------------------------------- */
#define C3D_FRAME_STEP  4       /* sll v0,v0,2 at RAM 0x8004CB28 */
/* THE POINTER'S OWN HEIGHT ABOVE THE GROUND, in cells, and the same nudge the shadow
   triangles get: the ground-marker triangle is modelled at exactly y=0 and z-fights the
   terrain without it. Named here rather than repeated as a literal at the four draws the
   pointer's body and its flattened shadow make, because c3d_draw_one now takes a lift and
   a caller that wants the pointer's own must be able to ask for it by name instead of by
   remembering the number. (The screen-edge arrow and the push pointer keep their own
   spellings of the same value; they are separate draws with separate reasons.) */
#define C3D_CURSOR_LIFT 0.012f

/* TexAnim time tables, node+0x10 -> TexAnim+0x00. Code 0x0A: RAM 0x801BBE80 / ROM
   0x15F8F0. Code 0x0B: RAM 0x801BBC40 / ROM 0x15F6B0. Last entry is the PERIOD. */
static const int C3D_FLIP_TIMES_0A[5] = {0, 4000, 8000, 12000, 16000};
static const int C3D_FLIP_TIMES_0B[4] = {0, 1600, 3200, 4800};

/* Resolved once, on first use: 14 pack type codes CUR00..CUR0D. */
static int  g_c3dMesh[14];
/* Per-frame flipbook variants, CUR0AF0.. / CUR0BF0..; count 0 = not a flipbook cursor. */
static int  g_c3dFlip[14][8];
static int  g_c3dFlipN[14];
/* CURSOR CODE 0x0E, THE SCREEN-EDGE SCROLL ARROW, and it is deliberately NOT one of the
   fourteen above. Those fourteen are the models the twenty-entry STATE TABLE can name; the
   cartridge never names 0x0E through that table at all. Its own handler (type 2, RAM
   0x8004C890) tests the cursor's lepton position against the map bounds and binds the node
   directly, so it is resolved on its own -- a pack that predates it still reports 14 of 14
   and the pointer keeps working. */
static int  g_c3dEdgeMesh = -1;
static bool g_c3dEdgeArrow = true;      /* --noedgearrow for the A/B */
/* THE FOUR-ARROW PUSH POINTER, set around the ONE c3d_draw call in draw_frame exactly as
   g_c3dEdgeArrow is masked there for the editor. This header is included long before the
   right button's own block is compiled, so the fact travels in a flag rather than in a
   read of the latch. */
static bool g_c3dPush = false;

/* THE GREEN POINTER. A new model for cursor code 0x00, the plain pointer, from the
   approved art in art/cursor (tools/art/cursor_green.py compiles it into
   cursor_green_mesh.h). It keeps the cartridge pointer's footprint and tip exactly, so
   it is a straight swap of one mesh for another: same anchor, same lift, same shadow,
   same depth rules. It carries no ground-marker triangle, so the cutout pass that draws
   the old pointer's marker draws nothing for it.
   It is APPENDED to g_pack as one more mesh rather than drawn by a path of its own, which
   is why it needs nothing a fixed-function card cannot do: untextured triangles with
   their shading in the vertex colour, the form the cartridge's own cursors take.
   Appended at c3d_init, which runs once per loaded pack; -1 until then. */
#include "cursor_green_mesh.h"
static int g_c3dGreenMesh = -1;
#define C3D_GREEN_NAME "CUR00GRN"

static void c3d_green_add(void)
{
    g_c3dGreenMesh = -1;
    for (size_t i = 0; i < g_pack.mesh.size(); i++)
        if (!strcmp(g_pack.mesh[i].name, C3D_GREEN_NAME)) { g_c3dGreenMesh = (int)i; return; }
    PackMesh m;
    memset(m.name, 0, sizeof m.name);
    snprintf(m.name, sizeof m.name, "%s", C3D_GREEN_NAME);
    m.tris.resize(CURSOR_GREEN_NTRIS);
    for (int t = 0; t < CURSOR_GREEN_NTRIS; t++) {
        PackTri& pt = m.tris[t];
        pt.tex = -1;
        pt.mode = MODE_OPAQUE;
        pt.wrap = 0;
        for (int k = 0; k < 3; k++) {
            const CursorGreenVert& g = CURSOR_GREEN_TRIS[t][k];
            PackVert& v = pt.v[k];
            v.x = g.x; v.y = g.y; v.z = g.z;
            v.u = v.v = 0.0f;
            v.r = g.r; v.g = g.g; v.b = g.b; v.a = 255;
        }
    }
    g_pack.mesh.push_back(m);
    g_c3dGreenMesh = (int)g_pack.mesh.size() - 1;
}

/* Drawn only under experimental art, and only when its own row is on. */
static bool c3d_green_on(void)
{
    return g_c3dGreenMesh >= 0 && g_fx.enabled && g_fx.experimental_textures
           && g_fx.curgreen_on;
}

/* ---- THE REMASTERED CURSORS (Enhanced) -----------------------------------------------
   Every cartridge cursor remastered in the lab (tools/art/remaster/cursors, its README the
   recipe): the same shapes in the set's glossy finish, each in its function's colour, with
   a soft glow round it. Drawn under Enhanced with experimental art off, the footing every
   remastered set has (the soldiers, the buildings); under experimental art the green
   pointer and the cartridge's shapes stay.

   ONE FILE, ecur/ecur.bin under the models folder, baked from the approved lab models by
   tools/art/remaster/cursors/bake_ecur.py and committed. Each remastered model is APPENDED
   to the pack as one more mesh, as the green pointer is, and c3d_mesh_for swaps it in, so
   every path that draws a cursor (the body, its shadow, the no-entry overlay, the repair
   wrench over a building) takes it with no second draw path.

   THE ANIMATION IS THE CARTRIDGE'S, BORROWED AT ATTACH. A remastered model keeps the
   cartridge model's part numbers and rest pose, so it is given that mesh's own part table
   and baked node animation (animMat, animVis, the clips), and draw_mesh moves it with the
   same deltas on the same clock. The flipbook cursors carry one remastered mesh per frame,
   swapped by the cartridge's own time table exactly as its variants are, and the spanner
   turns through c3d_spin_face, which asks the code and not the mesh.

   THE GLOW is the model's MODE_XLU triangles: untextured, bright at the outline and black
   at their outer edge, drawn in a pass of their own with additive blending (GL_ONE,
   GL_ONE), depth tested and not written. Black adds nothing, so no alpha is needed.
   Nothing here is beyond a fixed-function card: 256-texel RGBA textures, vertex colour
   and one additive blend. */
struct EcurTex  { int w, h; std::vector<unsigned char> px; };
struct EcurTri  { int tex; unsigned char mode, part; PackVert v[3]; };
struct EcurMesh {
    std::string name; std::vector<EcurTri> tris;
    /* version 2 only (Red Alert's file, below): the model's own part count, whether it
       turns, its flipbook's frame count and sequence over one clock cycle, and its tracks */
    int nparts = 0, flags = 0, nflip = 0, animFrames = 0;
    std::vector<unsigned char> seq;
    std::vector<float> animMat;
};
static std::vector<EcurTex>  g_ecurTex;
static std::vector<EcurMesh> g_ecurFile;
static bool g_ecurLoaded = false;
static bool g_ecurDraw = true;              /* --noecur is the A/B */
/* The appended meshes for the pack now loaded: per state-table code, per flipbook frame,
   the edge arrow and the move marker. -1 where there is none. */
static int  g_ecurMesh[14];
static int  g_ecurFlip[14][8];
static int  g_ecurEdge = -1;
static int  g_ecurCirc = -1;
static int  g_ecurAttached = 0;
/* Move markers drawn from the remastered rings (draw_order_marks), for the gate. */
static int  g_ecurMarkDraws = 0;

/* ---- RED ALERT'S OWN REMASTERED CURSORS (Enhanced, Red Alert only) ------------------
   Red Alert has twenty-eight mouse states to Tiberian Dawn's twenty. Twenty-two of them
   are drawn with models already in ecur.bin (ten reuse an approved cursor as it is, six
   are a reused cursor held still under the no-entry sign, the cartridge's overlay rule);
   six are models of Red Alert's own, in ecur/ecur_ra.bin under the module's folder, baked
   by tools/art/remaster/ra/bake_ecur_ra.py from the Red Alert lab and committed:

     GREPAIR        the repair spanner in gold, turning as the steel one does
     DAMAGE         the enter arrows in red, chasing as they do (a three-frame flipbook)
     CHRONO_SELECT  the select brackets in green, held still and pulsing (a flipbook)
     HEAL           a silver disc and a red cross, pulsing against each other (a flipbook)
     CHRONO_DEST    the move cursor with two green rings closing on it (its own tracks)
     RADAR_CURSOR   the view frame drawn over the radar map, in the sidebar

   None of them was rebuilt from a cartridge model that moves the way it should, so none
   borrows a cartridge mesh's animation as ecur.bin's models do: each carries its own part
   count, its turn, its flipbook's sequence over the clock's 100 frames and its tracks,
   and is attached with a part table of its own. They take model codes past the
   cartridge's (C3D_RA_FIRST on), so every path that draws a cursor by its code (the body,
   its shadow, the overlay, the spin) draws them with no second draw path. Tiberian Dawn
   never loads the file and never reaches a code past 0x0E. */
#define C3D_RA_FIRST          0x10
#define C3D_RA_GREPAIR        0x10
#define C3D_RA_DAMAGE         0x11
#define C3D_RA_CHRONO_SELECT  0x12
#define C3D_RA_HEAL           0x13
#define C3D_RA_CHRONO_DEST    0x14
#define C3D_RA_RADAR          0x15
#define C3D_RA_N              6
#define C3D_RA_MAXFLIP        8
static const char* const C3D_RA_NAME[C3D_RA_N] = {
    "GREPAIR", "DAMAGE", "CHRONO_SELECT", "HEAL", "CHRONO_DEST", "RADAR_CURSOR" };
/* What each draws where the file is absent or Enhanced is off: the cartridge model its
   shape came from (the heal cursor has none, and takes the plain pointer; the radar frame
   is never drawn in the world). */
static const int C3D_RA_CART[C3D_RA_N] = { 0x05, 0x0B, 0x03, 0x00, 0x01, -1 };
static std::vector<EcurTex>  g_ecurRaTex;
static std::vector<EcurMesh> g_ecurRaFile;
static bool g_ecurRaLoaded = false;
/* per model: its appended pack mesh for each flipbook frame (frame 0 is the model's own),
   its frame count and sequence, whether it turns; the run of pack meshes they occupy */
static int  g_ecurRaMesh[C3D_RA_N][C3D_RA_MAXFLIP];
static int  g_ecurRaFlipN[C3D_RA_N];
static const EcurMesh* g_ecurRaDoc[C3D_RA_N];
static int  g_ecurRaFirst = -1, g_ecurRaEnd = -1;
static int  g_ecurRaAttached = 0;

static bool ecur_ra_is(int mi)
{
    return g_ecurRaFirst >= 0 && mi >= g_ecurRaFirst && mi < g_ecurRaEnd;
}

/* One cursor file, read whole: 0 read, 1 missing, 2 refused (short, foreign, or another
   version than `want`). Version 1 is Tiberian Dawn's ecur.bin; version 2 is Red Alert's
   ecur_ra.bin, whose meshes carry their own parts, turn, flipbook and tracks after their
   triangles (tools/art/remaster/ra/bake_ecur_ra.py has the layout). */
static int ecur_read(const char* path, unsigned int want, std::vector<EcurTex>& texs,
                     std::vector<EcurMesh>& meshes)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 1;
    std::vector<unsigned char> b;
    {
        unsigned char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
        fclose(f);
    }
    size_t o = 0;
    bool bad = false;
    unsigned int ver = 0, ntex = 0, nmesh = 0;
    /* bounds-checked little-endian reads: a short or foreign file is refused whole */
    #define ECUR_NEED(n) do { if (o + (size_t)(n) > b.size()) { bad = true; goto done; } } while (0)
    ECUR_NEED(16);
    if (memcmp(&b[0], "ECUR", 4)) { bad = true; goto done; }
    memcpy(&ver, &b[4], 4); memcpy(&ntex, &b[8], 4); memcpy(&nmesh, &b[12], 4);
    o = 16;
    if (ver != want || ntex > 256 || nmesh > 256) { bad = true; goto done; }
    for (unsigned int t = 0; t < ntex; t++) {
        unsigned short w, h;
        ECUR_NEED(4);
        memcpy(&w, &b[o], 2); memcpy(&h, &b[o + 2], 2); o += 4;
        if (!w || !h || w > 256 || h > 256) { bad = true; goto done; }
        ECUR_NEED((size_t)w * h * 4);
        {
            EcurTex tx; tx.w = w; tx.h = h;
            tx.px.assign(b.begin() + o, b.begin() + o + (size_t)w * h * 4);
            texs.push_back(tx);
        }
        o += (size_t)w * h * 4;
    }
    for (unsigned int m = 0; m < nmesh; m++) {
        ECUR_NEED(20);
        EcurMesh em;
        {
            char name[17]; memcpy(name, &b[o], 16); name[16] = 0; em.name = name;
        }
        unsigned int ntri; memcpy(&ntri, &b[o + 16], 4); o += 20;
        ECUR_NEED((size_t)ntri * (4 + 3 * 24));
        em.tris.resize(ntri);
        for (unsigned int t = 0; t < ntri; t++) {
            EcurTri& et = em.tris[t];
            short tex; memcpy(&tex, &b[o], 2);
            et.tex = tex; et.mode = b[o + 2]; et.part = b[o + 3]; o += 4;
            if (et.tex >= (int)ntex || (et.mode != MODE_OPAQUE && et.mode != MODE_XLU)) { bad = true; goto done; }
            for (int k = 0; k < 3; k++) {
                float fv[5]; memcpy(fv, &b[o], 20);
                PackVert& v = et.v[k];
                v.x = fv[0]; v.y = fv[1]; v.z = fv[2]; v.u = fv[3]; v.v = fv[4];
                v.r = b[o + 20]; v.g = b[o + 21]; v.b = b[o + 22]; v.a = b[o + 23];
                o += 24;
            }
        }
        if (ver == 2) {
            ECUR_NEED(6);
            em.nparts = b[o]; em.flags = b[o + 1]; em.nflip = b[o + 2];
            unsigned short nseq; memcpy(&nseq, &b[o + 4], 2); o += 6;
            ECUR_NEED(nseq);
            em.seq.assign(b.begin() + o, b.begin() + o + nseq); o += nseq;
            ECUR_NEED(4);
            unsigned short na; memcpy(&na, &b[o], 2); o += 4;
            em.animFrames = na;
            const size_t nf = (size_t)na * (size_t)em.nparts * 12;
            ECUR_NEED(nf * 4);
            em.animMat.resize(nf);
            if (nf) memcpy(&em.animMat[0], &b[o], nf * 4);
            o += nf * 4;
            for (size_t t = 0; t < em.tris.size(); t++)
                if ((int)em.tris[t].part >= em.nparts) { bad = true; goto done; }
            for (size_t k = 0; k < em.seq.size(); k++)
                if ((int)em.seq[k] >= (em.nflip > 0 ? em.nflip : 1)) { bad = true; goto done; }
        }
        meshes.push_back(em);
    }
done:
    #undef ECUR_NEED
    if (bad || o != b.size()) {
        meshes.clear(); texs.clear();
        return 2;
    }
    return 0;
}

static void ecur_load(const char* dir)
{
    if (g_ecurLoaded) return;
    g_ecurLoaded = true;
    char path[512];
    snprintf(path, sizeof path, "%secur/ecur.bin", dir && *dir ? dir : "");
    const int r = ecur_read(path, 1, g_ecurTex, g_ecurFile);
    if (r == 1) {
        fprintf(stderr, "ecur: %s is missing -- Enhanced draws the cartridge's cursors\n", path);
        return;
    }
    if (r == 2) {
        fprintf(stderr, "ecur: %s is not a cursor file this renderer reads -- refused, "
                        "Enhanced draws the cartridge's cursors\n", path);
        return;
    }
    size_t ntris = 0;
    for (size_t m = 0; m < g_ecurFile.size(); m++) ntris += g_ecurFile[m].tris.size();
    printf("ECUR|loaded|meshes %d|textures %d|triangles %d\n", (int)g_ecurFile.size(),
           (int)g_ecurTex.size(), (int)ntris);
    fflush(stdout);
}

/* Mipmapped by hand, as the authored models' textures are (m3_upload_tex): no GLU. */
static GLuint ecur_upload(const EcurTex& t)
{
    GLuint gl = 0;
    glGenTextures(1, &gl);
    glBindTexture(GL_TEXTURE_2D, gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    std::vector<unsigned char> cur = t.px;
    int w = t.w, h = t.h, level = 0;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &cur[0]);
    while (w > 1 || h > 1) {
        const int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        std::vector<unsigned char> next((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                const int sx = x * 2, sy = y * 2;
                const int sx1 = (w > 1 ? sx + 1 : sx), sy1 = (h > 1 ? sy + 1 : sy);
                for (int c = 0; c < 4; c++)
                    next[((size_t)y * nw + x) * 4 + c] = (unsigned char)((
                        cur[((size_t)sy * w + sx) * 4 + c] + cur[((size_t)sy * w + sx1) * 4 + c] +
                        cur[((size_t)sy1 * w + sx) * 4 + c] + cur[((size_t)sy1 * w + sx1) * 4 + c] + 2) / 4);
            }
        cur.swap(next);
        w = nw; h = nh; level++;
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &cur[0]);
    }
    return gl;
}

/* The cartridge mesh a remastered model stands in for, by its name in the file. */
static int ecur_cart_mesh(const std::string& n)
{
    if (n == "CUR0E") return g_c3dEdgeMesh;
    if (n == "CURCIRC") {
        std::map<std::string, PackType>::iterator it = g_pack.type.find("CURCIRC");
        return it == g_pack.type.end() ? -1 : it->second.mesh;
    }
    unsigned int code = 0; int fr = -1;
    if (n.size() == 5 && sscanf(n.c_str(), "CUR%2X", &code) == 1 && code < 14)
        return g_c3dMesh[code];
    if (n.size() == 7 && n[5] == 'F' && sscanf(n.c_str(), "CUR%2XF%d", &code, &fr) == 2
        && code < 14 && fr >= 0 && fr < g_c3dFlipN[code])
        return g_c3dFlip[code][fr];
    return -1;
}

/* After c3d_init has resolved the cartridge's models for this pack: append the textures
   and one mesh per remastered model, each given its cartridge model's part table and
   animation. pack_free deletes them with the rest of the pack. */
static void ecur_attach(void)
{
    for (int i = 0; i < 14; i++) {
        g_ecurMesh[i] = -1;
        for (int f = 0; f < 8; f++) g_ecurFlip[i][f] = -1;
    }
    g_ecurEdge = g_ecurCirc = -1;
    g_ecurAttached = 0;
    if (g_ecurFile.empty()) return;
    const int tex0 = (int)g_pack.tex.size();
    for (size_t t = 0; t < g_ecurTex.size(); t++) {
        PackTex pt;
        pt.w = pt.uw = g_ecurTex[t].w;
        pt.h = pt.uh = g_ecurTex[t].h;
        pt.gl = ecur_upload(g_ecurTex[t]);
        pt.gl_gdi = 0;
        for (int e = 0; e < 7; e++) pt.gl_extra[e] = 0;
        g_pack.tex.push_back(pt);
    }
    for (size_t m = 0; m < g_ecurFile.size(); m++) {
        const EcurMesh& em = g_ecurFile[m];
        const int cm = ecur_cart_mesh(em.name);
        if (cm < 0) continue;
        /* copied out before the push_back below can move g_pack.mesh */
        const PackMesh cart = g_pack.mesh[cm];
        const int nparts = (int)cart.parts.size();
        PackMesh pm;
        memset(pm.name, 0, sizeof pm.name);
        snprintf(pm.name, sizeof pm.name, "E%s", em.name.c_str());
        bool fits = true;
        pm.tris.resize(em.tris.size());
        for (size_t t = 0; t < em.tris.size(); t++) {
            const EcurTri& et = em.tris[t];
            if ((int)et.part >= (nparts > 0 ? nparts : 1)) fits = false;
            PackTri& pt = pm.tris[t];
            pt.tex = et.tex >= 0 ? tex0 + et.tex : -1;
            pt.mode = et.mode;
            pt.wrap = 0;
            for (int k = 0; k < 3; k++) pt.v[k] = et.v[k];
        }
        if (!fits) {
            fprintf(stderr, "ecur: %s names a part its cartridge model does not have -- "
                            "drawing the cartridge's\n", em.name.c_str());
            continue;
        }
        /* the file sorts its triangles by part, so each part is one run */
        for (int p = 0; p < nparts; p++) {
            PackPart pp = cart.parts[p];
            int first = -1, n = 0;
            for (size_t t = 0; t < em.tris.size(); t++)
                if (em.tris[t].part == p) { if (first < 0) first = (int)t; n++; }
            if (first < 0) {                     /* empty: placed where it would start */
                first = 0;
                for (size_t t = 0; t < em.tris.size(); t++)
                    if (em.tris[t].part < p) first = (int)t + 1;
            }
            pp.tri0 = first; pp.ntris = n;
            pm.parts.push_back(pp);
        }
        pm.animFrames = cart.animFrames;
        pm.animTicksPerFrame = cart.animTicksPerFrame;
        pm.clips = cart.clips;
        pm.animMat = cart.animMat;
        pm.animVis = cart.animVis;
        g_pack.mesh.push_back(pm);
        const int mi = (int)g_pack.mesh.size() - 1;
        const std::string& n = em.name;
        unsigned int code = 0; int fr = -1;
        if (n == "CUR0E") g_ecurEdge = mi;
        else if (n == "CURCIRC") g_ecurCirc = mi;
        else if (n.size() == 7 && sscanf(n.c_str(), "CUR%2XF%d", &code, &fr) == 2) g_ecurFlip[code][fr] = mi;
        else if (sscanf(n.c_str(), "CUR%2X", &code) == 1) g_ecurMesh[code] = mi;
        g_ecurAttached++;
    }
    printf("ECUR|attached|%d of %d remastered cursor models\n", g_ecurAttached, (int)g_ecurFile.size());
    fflush(stdout);
}

/* Red Alert's file, read once, for the Red Alert module only. */
static void ecur_ra_load(const char* dir)
{
    if (!g_raModule || g_ecurRaLoaded) return;
    g_ecurRaLoaded = true;
    char path[512];
    snprintf(path, sizeof path, "%sra3d/ecur/ecur_ra.bin", dir && *dir ? dir : "");
    const int r = ecur_read(path, 2, g_ecurRaTex, g_ecurRaFile);
    if (r) {
        fprintf(stderr, "ecur: %s is %s -- Red Alert draws its own six cursors with the "
                        "cartridge models they came from\n", path,
                r == 1 ? "missing" : "not a cursor file this renderer reads");
        return;
    }
    size_t ntris = 0;
    for (size_t m = 0; m < g_ecurRaFile.size(); m++) ntris += g_ecurRaFile[m].tris.size();
    printf("ECURRA|loaded|meshes %d|textures %d|triangles %d\n", (int)g_ecurRaFile.size(),
           (int)g_ecurRaTex.size(), (int)ntris);
    fflush(stdout);
}

/* After ecur_attach: append Red Alert's textures and models, each with a part table of its
   own (every pivot at the hot spot: nothing turns about a part) and its own tracks. A
   flipbook model's frames are <NAME>F1.. beside <NAME>, its frame 0. */
static void ecur_ra_attach(void)
{
    for (int r = 0; r < C3D_RA_N; r++) {
        for (int f = 0; f < C3D_RA_MAXFLIP; f++) g_ecurRaMesh[r][f] = -1;
        g_ecurRaFlipN[r] = 0;
        g_ecurRaDoc[r] = NULL;
    }
    g_ecurRaFirst = g_ecurRaEnd = -1;
    g_ecurRaAttached = 0;
    if (!g_raModule || g_ecurRaFile.empty()) return;
    const int tex0 = (int)g_pack.tex.size();
    for (size_t t = 0; t < g_ecurRaTex.size(); t++) {
        PackTex pt;
        pt.w = pt.uw = g_ecurRaTex[t].w;
        pt.h = pt.uh = g_ecurRaTex[t].h;
        pt.gl = ecur_upload(g_ecurRaTex[t]);
        pt.gl_gdi = 0;
        for (int e = 0; e < 7; e++) pt.gl_extra[e] = 0;
        g_pack.tex.push_back(pt);
    }
    g_ecurRaFirst = (int)g_pack.mesh.size();
    for (size_t m = 0; m < g_ecurRaFile.size(); m++) {
        const EcurMesh& em = g_ecurRaFile[m];
        int r = -1, fr = 0;
        for (int k = 0; k < C3D_RA_N && r < 0; k++) {
            const size_t n = strlen(C3D_RA_NAME[k]);
            if (em.name == C3D_RA_NAME[k]) r = k;
            else if (em.name.size() > n + 1 && !em.name.compare(0, n, C3D_RA_NAME[k])
                     && em.name[n] == 'F' && sscanf(em.name.c_str() + n + 1, "%d", &fr) == 1
                     && fr > 0 && fr < C3D_RA_MAXFLIP) r = k;
        }
        if (r < 0) continue;
        if (em.name == C3D_RA_NAME[r]) fr = 0;
        PackMesh pm;
        memset(pm.name, 0, sizeof pm.name);
        snprintf(pm.name, sizeof pm.name, "E%s", em.name.c_str());
        pm.tris.resize(em.tris.size());
        for (size_t t = 0; t < em.tris.size(); t++) {
            const EcurTri& et = em.tris[t];
            PackTri& pt = pm.tris[t];
            pt.tex = et.tex >= 0 ? tex0 + et.tex : -1;
            pt.mode = et.mode;
            pt.wrap = 0;
            for (int k = 0; k < 3; k++) pt.v[k] = et.v[k];
        }
        /* the file sorts its triangles by part, so each part is one run */
        for (int p = 0; p < em.nparts; p++) {
            PackPart pp;
            pp.role = ROLE_STATIC;
            pp.px = pp.py = pp.pz = 0.0f;
            int first = -1, n = 0;
            for (size_t t = 0; t < em.tris.size(); t++)
                if (em.tris[t].part == p) { if (first < 0) first = (int)t; n++; }
            if (first < 0) {
                first = 0;
                for (size_t t = 0; t < em.tris.size(); t++)
                    if (em.tris[t].part < p) first = (int)t + 1;
            }
            pp.tri0 = first; pp.ntris = n;
            pm.parts.push_back(pp);
        }
        if (fr == 0 && em.animFrames > 0) {
            pm.animFrames = em.animFrames;
            pm.animTicksPerFrame = 1;
            PackClip c; c.t0 = 0; c.t1 = em.animFrames; c.loop = 1;
            pm.clips.push_back(c);
            pm.animMat = em.animMat;
        }
        g_pack.mesh.push_back(pm);
        g_ecurRaMesh[r][fr] = (int)g_pack.mesh.size() - 1;
        if (fr == 0) {
            g_ecurRaDoc[r] = &em;
            g_ecurRaFlipN[r] = em.nflip > 0 ? em.nflip : 1;
        }
        g_ecurRaAttached++;
    }
    g_ecurRaEnd = (int)g_pack.mesh.size();
    /* a model missing a frame its flipbook names is drawn as its frame 0 throughout,
       rather than on a sequence that points at nothing */
    for (int r = 0; r < C3D_RA_N; r++)
        for (int f = 1; f < g_ecurRaFlipN[r]; f++)
            if (g_ecurRaMesh[r][f] < 0) {
                fprintf(stderr, "ecur: Red Alert's %s lacks flipbook frame %d -- drawn still\n",
                        C3D_RA_NAME[r], f);
                g_ecurRaFlipN[r] = 1;
                break;
            }
    printf("ECURRA|attached|%d of %d Red Alert cursor meshes\n", g_ecurRaAttached,
           (int)g_ecurRaFile.size());
    fflush(stdout);
}

/* Drawn under Enhanced with experimental art off, when its own row is on. */
static bool ecur_on(void)
{
    return g_ecurDraw && g_ecurAttached > 0 && g_fx.enabled && !g_fx.experimental_textures
           && g_fx.ecur_on;
}

/* Whether a pack mesh is one of the remastered cursors appended above. */
static bool ecur_is(int mi)
{
    return mi >= 0 && mi < (int)g_pack.mesh.size()
           && ((g_pack.mesh[mi].name[0] == 'E' && !strncmp(g_pack.mesh[mi].name + 1, "CUR", 3))
               || ecur_ra_is(mi));
}

/* The size of a remastered model against the cartridge model it stands in for (its own
   dial, 0.75 by request); 1 for every other mesh. */
static float ecur_rescale(int mi)
{
    return ecur_is(mi) ? g_fx.ecur_scale : 1.0f;
}

/* The glow of a remastered model, drawn after its body: additive, depth tested, depth not
   written, so it never hides what is behind it and never cuts the next cursor. */
static void ecur_glow(int mi, float wx, float wz, int face, float ylift, float animT)
{
    if (!ecur_is(mi)) return;
    /* the caller's state comes back as it was: the on-top cursor has depth writes off */
    GLboolean mask = GL_TRUE;
    GLint src = GL_SRC_ALPHA, dst = GL_ONE_MINUS_SRC_ALPHA;
    const GLboolean blend = glIsEnabled(GL_BLEND);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    glGetIntegerv(GL_BLEND_SRC, &src);
    glGetIntegerv(GL_BLEND_DST, &dst);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    draw_mesh(mi, wx, wz, face, MODE_XLU, 0, 0.0f, 0, 1.0f, ylift, false, WOBBLE_NONE, animT);
    glDepthMask(mask);
    glBlendFunc((GLenum)src, (GLenum)dst);
    if (!blend) glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

static bool g_c3dReady = false;
static bool g_c3dUsable = false;
/* The gate's own control, the same instrument g_wallsDraw and g_ridersDraw are. */
static bool g_c3dDraw = true;
static bool g_c3dAnim = true;    /* the animation's own control leg */
/* -1 = follow the engine's MouseType. >= 0 forces one state, for the gate: several
   animated states (deploy, the superweapons) need game situations no shipped mission can
   reach on demand, and a cursor that cannot be put on screen cannot be asserted. */
static int  g_c3dForceState = -1;
/* Red Alert's counterpart, by MouseType rather than by cartridge state (Red Alert has no
   cartridge states): -1 follows the engine, >= 0 draws that mouse state. The heal, damage
   and chronosphere cursors need situations no shipped mission reaches on demand either. */
static int  g_c3dForceMouse = -1;
/* What the last world draw did at the map's edge, for the dump: the arrow's facing (-1 no
   arrow) and whether the view could not scroll that way (Red Alert's NO_SCROLL). */
static int  g_c3dEdgeFace = -1;
static bool g_c3dEdgeBlocked = false;
/* Red Alert's radar frame drawn over the radar map in the sidebar, counted for the gate. */
static int  g_c3dOverlayDraws = 0;
/* Whether the view can scroll any further toward (dx, dz), a world direction in cells:
   the camera's own clamp, asked by moving its target and putting it back. Defined in
   cnc_eyes.cpp beside the clamp it asks. */
static bool c3d_cam_blocked(float dx, float dz);

static void c3d_init(void)
{
    int have = 0;
    for (int i = 0; i < 14; i++) {
        char key[16];
        snprintf(key, sizeof key, "CUR%02X", i);
        std::map<std::string, PackType>::iterator it = g_pack.type.find(key);
        g_c3dMesh[i] = (it == g_pack.type.end() || it->second.conf < 1) ? -1
                                                                       : it->second.mesh;
        if (g_c3dMesh[i] >= 0) have++;
        g_c3dFlipN[i] = 0;
        for (int f = 0; f < 8; f++) {
            snprintf(key, sizeof key, "CUR%02XF%d", i, f);
            std::map<std::string, PackType>::iterator jt = g_pack.type.find(key);
            if (jt == g_pack.type.end() || jt->second.mesh < 0)
                break;
            g_c3dFlip[i][f] = jt->second.mesh;
            g_c3dFlipN[i] = f + 1;
        }
        /* A PARTIAL flipbook is worse than none, and silently so: c3d_flip_index takes
           the PERIOD from times[count], so three of the ion ring's four variants would
           make the cycle 12000 ticks long instead of 16000 and every index after the
           first would be wrong. The count the cartridge's own time table implies is
           fixed, so demand exactly it or take none. */
        const int wantflip = (i == 0x0A) ? 4 : (i == 0x0B) ? 3 : 0;
        if (g_c3dFlipN[i] != wantflip) {
            if (g_c3dFlipN[i] > 0)
                fprintf(stderr, "cursor3d: cursor 0x%02X has %d of %d flipbook frames in "
                                "this pack -- drawing it STATIC rather than on a wrong "
                                "period. Re-bake with tools/bakery/bake5.py.\n",
                        i, g_c3dFlipN[i], wantflip);
            g_c3dFlipN[i] = 0;
        }
    }
    {
        std::map<std::string, PackType>::iterator it = g_pack.type.find("CUR0E");
        g_c3dEdgeMesh = (it == g_pack.type.end() || it->second.conf < 1) ? -1
                                                                        : it->second.mesh;
        if (g_c3dEdgeMesh < 0)
            fprintf(stderr, "cursor3d: no CUR0E in this pack -- the screen-edge scroll "
                            "arrows will not draw. Re-bake with tools/bakery/bake5.py.\n");
    }
    g_c3dReady = true;
    /* REFUSE on a pack that predates the cursor bake, and say so once. Falling back to
       the flat 1995 sprite is the honest failure here: half a cursor set would leave the
       pointer vanishing over some terrain and not others. */
    g_c3dUsable = (have == 14);
    g_c3dGreenMesh = -1;
    if (g_c3dUsable) {
        c3d_green_add();
        ecur_attach();
        ecur_ra_attach();
        int nanim = 0;
        for (int i = 0; i < 14; i++)
            if (g_c3dMesh[i] >= 0 && g_pack.mesh[g_c3dMesh[i]].animFrames > 0)
                nanim++;
        fprintf(stderr, "cursor3d: 14 of 14 console cursor models resolved from the pack; "
                        "%d carry node animation, %d carry a texture flipbook\n",
                nanim, (g_c3dFlipN[0x0A] ? 1 : 0) + (g_c3dFlipN[0x0B] ? 1 : 0));
    }
    else
        fprintf(stderr, "cursor3d: ONLY %d of 14 console cursor models in this pack -- "
                        "keeping the 1995 DOS sprite everywhere. Re-bake with "
                        "tools/bakery/bake5.py.\n", have);
}

static bool c3d_have(void)
{
    if (!g_c3dReady) c3d_init();
    return g_c3dUsable && g_c3dDraw;
}

/* The console's own clock. `frames` is the state table's byte +2 (1 or 100).
   Returns the anim frame, or -1 when this state does not animate. */
static int c3d_anim_frame(int frames)
{
    if (!g_c3dAnim || frames <= 1)
        return -1;
    const int fc = (g_engineFrame < 0) ? 0 : g_engineFrame;
    return (fc * C3D_FRAME_STEP) % frames;
}

/* ---- THE WRENCH'S SPIN -------------------------------------------------------------
   Mechanism 3 in the header, and the ONLY animation law code 0x05 has. It is the state
   table's own frameCount turned into the draw's ordinary facing argument: a cycle of
   `frames` clock frames is one revolution, so the model's yaw is frame/frames of a turn.

   THE UNITS ARE DirType, 256 to the circle, INCREASING CLOCKWISE (0 north, 64 east, 128
   south, 192 west) -- the same units c3d_edge_face returns and the only thing draw_mesh
   accepts. `frame` climbs by C3D_FRAME_STEP every engine tick, so the yaw climbs too and
   the wrench turns CLOCKWISE seen from above. That is a direction, and it is asserted as
   one: reversing it is a different picture, not a rounding difference.

   HOW LONG A TURN TAKES, in the only unit that was measured: the clock advances the frame
   by 4 per ENGINE TICK and the period is 100, so one revolution is 25 ENGINE TICKS and it
   passes through 25 distinct facings, 0, 10, 20, 30, 40, 51, 61, 71, 81, 92, 102 and so on
   (256*frame/100, truncated). No wall-clock figure is quoted here: what a tick costs in
   seconds is the renderer's business and has not been measured, and the last version of
   this feature shipped a confident "1.6 s" that nothing in its own submission supported.

   frame < 0 is the clock's way of saying "this draw does not animate" -- either the state
   row's frameCount is 1 (row 15, the no-repair wrench under its ring) or the animation
   has been switched off for a gate -- and it returns -1, which is draw_mesh's "no yaw".
   Every code but 0x05 gets -1 as well; see the header for why that is a written test and
   not an inference from the pack. */
#define C3D_WRENCH_CODE  0x05
#define C3D_WRENCH_STATE 6      /* the state row whose byte +2 is this model's period */
/* THE CURSOR KEEPS ITS SCREEN HEADING UNDER THE PERSPECTIVE ROW'S YAW. A cursor is a
   screen thing drawn as a world mesh, and with the world turned it turned with it (the
   first isometric build did exactly that; the director asked for the Classic heading).
   The turn that puts screen-up back at screen-up is the yaw in DirType units, the same
   number the push arrows add; -1 (draw_mesh's "no yaw") becomes that turn, and the
   spinning wrench adds it to its spin. At yaw 0 every value is what it was. The map-edge
   arrow is NOT turned: it points at the map's edge, which is a world thing. */
static int c3d_screen_face(int face)
{
    const float yaw = (g_camMode == CAM_N64) ? g_camYaw : 0.0f;
    if (yaw == 0.0f) return face;
    const int turn = (int)lroundf(yaw * 256.0f / (2.0f * (float)M_PI));
    return ((face < 0 ? 0 : face) + turn) & 255;
}

static int c3d_spin_face(int code, int frame)
{
    /* Red Alert's ground-repair spanner turns by the same law: it is the same spanner in
       gold, and its state row carries the same 100 frames (c3d_ra_row). */
    if ((code != C3D_WRENCH_CODE && code != C3D_RA_GREPAIR) || frame < 0)
        return -1;
    const int frames = C3D_STATE[C3D_WRENCH_STATE][2];
    if (frames <= 1)
        return -1;
    return (frame * 256 / frames) & 255;
}

/* THE ONE MODEL THE REPAIR WRENCH IS, for the draw over a repairing building. Resolved
   through the same lazy c3d_init the pointer uses, so there is one resolve and one latch.

   NOT gated on g_c3dUsable, and that is deliberate rather than sloppy. The POINTER demands
   all fourteen models because half a cursor set makes the pointer vanish over some terrain
   and not others, which is worse than the flat sprite. A wrench over a building needs
   exactly one model; a pack that carries CUR05 can draw it whatever else it is missing,
   and a pack that does not falls back to the 1995 sprite on its own. Returns -1 for
   "this pack has no CUR05". */
static int c3d_wrench_mesh(void)
{
    if (!g_c3dReady) c3d_init();
    return g_c3dMesh[C3D_WRENCH_CODE];
}

/* RAM 0x80080FAC: tt = (u32)t mod period, then the first slot whose upper bound is not
   below tt. t is the same t the tracks use, frame * 160 + 1. */
static int c3d_flip_index(int code, int frame)
{
    const int* times = (code == 0x0A) ? C3D_FLIP_TIMES_0A
                     : (code == 0x0B) ? C3D_FLIP_TIMES_0B : NULL;
    const int n = g_c3dFlipN[code];
    if (!times || n <= 0 || frame < 0)
        return 0;
    const int period = times[n];
    const int tt = (frame * 160 + 1) % period;
    for (int i = 0; i < n; i++)
        if (tt <= times[i + 1])
            return i;
    return 0;
}

/* The mesh a cursor code draws this frame: the flipbook variant for the two flipbook
   cursors, the green pointer for code 0x00 when it is on, else the pack's own. One
   function so the body and its shadow cannot pick different models. */
static int c3d_mesh_for(int code, int frame);

/* RED ALERT'S MODEL CODES. Under Enhanced with its file attached: the model's flipbook frame
   for this clock frame (its own sequence over the cycle; frame 0 when the state does not
   animate). Otherwise the cartridge model its shape came from, by the ordinary rule. */
static int c3d_ra_mesh_for(int code, int frame)
{
    const int r = code - C3D_RA_FIRST;
    if (r < 0 || r >= C3D_RA_N) return -1;
    if (ecur_on() && g_ecurRaMesh[r][0] >= 0) {
        int k = 0;
        const EcurMesh* d = g_ecurRaDoc[r];
        if (g_ecurRaFlipN[r] > 1 && frame >= 0 && d && !d->seq.empty())
            k = d->seq[(size_t)frame % d->seq.size()];
        if (k >= g_ecurRaFlipN[r] || g_ecurRaMesh[r][k] < 0) k = 0;
        return g_ecurRaMesh[r][k];
    }
    return C3D_RA_CART[r] >= 0 ? c3d_mesh_for(C3D_RA_CART[r], frame) : -1;
}

static int c3d_mesh_for(int code, int frame)
{
    if (code >= C3D_RA_FIRST) return c3d_ra_mesh_for(code, frame);
    if (code == 0x00 && c3d_green_on()) return g_c3dGreenMesh;
    int mi = g_c3dMesh[code];
    int fi = -1;
    if (g_c3dFlipN[code] > 0) {
        fi = frame < 0 ? 0 : c3d_flip_index(code, frame);
        mi = g_c3dFlip[code][fi];
    }
    /* Enhanced: the remastered model, the flipbook's frame for the same index */
    if (ecur_on()) {
        const int em = fi >= 0 ? g_ecurFlip[code][fi] : g_ecurMesh[code];
        if (em >= 0) return em;
    }
    return mi;
}

/* THE GREEN POINTER'S OWN SIZE. c3d_draw has already multiplied MODEL_SCALE by the 3D
   cursor size dial for the whole cursor; this is the factor that turns that into the
   green pointer's own dial instead, 1 for every other model. The green pointer only
   draws with the post chain on, which is exactly when that multiply happened. */
static float c3d_green_rescale(int code)
{
    if (code != 0x00 || !c3d_green_on()) return 1.0f;
    const float base = (g_fx.cursor_scale > 0.01f) ? g_fx.cursor_scale : 1.0f;
    return g_fx.curgreen_scale / base;
}

/* wx/wz in cells, from screen_to_world (the terrain hit). draw_mesh already lifts the
   model by terrain_y under the anchor, so the cursor rides the heightfield exactly as
   the console's own H term does. face = -1: the console's cursor node takes no facing.
   ylift is the same +0.012 cell the shadow triangles get -- the ground-marker triangle
   is modelled at exactly y=0 and would z-fight the ground without it.
   whiten_packed is on because ten of the fourteen models carry packed unit normals in
   the vertex-colour slot of their ground marker (and of CUR04/CUR08's hexagon wedges);
   see draw_mesh. */
/* The 3D cursor draws OVER the scene rather than inside it. Both were tested,
   and this one confirmed: depth-tested, the cursor was cut through by any
   building it stood on, because its body sits only 0.10..0.24 cell above the ground
   while a structure is far taller. --cursordepth restores the decoded behaviour (an
   ordinary depth-tested node, which is what the console's scene graph makes it) for
   an A/B. A deliberate deviation: the depth testing itself IS
   decoded, and what the console does about the building case is not. */
static bool g_c3dOnTop = true;

/* ---- THE CURSOR'S GROUND SHADOW -- AUTHORED BY US ---------------------------------
   NOT FOUND IN THE ROM, and the search is written up rather than left implied. What was
   checked: all fourteen cursor meshes carry ZERO shadow triangles in the baked pack, so
   it is not baked into the model; the cursor draw-command handler (type 2, "Cursor" in
   the cartridge's DLSafe registry, RAM 0x8004C890) calls no shadow routine; the one
   cursor-specific routine it calls four times, RAM 0x8004A690, turns out to draw the
   cursor node through the house TLUT selector and the animation evaluator and nothing
   else. The one promising lead -- that 0x8004A690 shares a call with the VEHICLE SHADOW
   routine -- was killed by reading it: RAM 0x8007C81C is a four-instruction setter
   (`sw a1,36(a0); jr ra`) with six callers ROM-wide, a shared accessor and no more.

   So this is AUTHORED, on the report that the console shows one shadow. That is a
   deliberate choice about which evidence to trust: an earlier survey concluded from
   the same kind of absence that the cartridge draws no VEHICLE shadow either, wrote it
   up as settled, and was wrong -- it was disproved with a single screenshot. Failing
   to find a thing is not finding that it is absent.

   It reuses the cartridge's OWN shadow art: the same intensity texture and the same
   black-PRIM-over-TEXEL0_A blend the vehicle shadows use, from the pack's PKD block.
   Only the quad is ours. --nocursorshadow removes it. */
static bool g_c3dShadow = true;
/* The pointer's zoom factor while c3d_draw is drawing it, 1 at every other moment; see
   c3d_draw. */
static float g_c3dZoomK = 1.0f;
/* How dark the cursor's projected shadow is, and how far it is thrown from the cursor.
   BOTH ARE ART, not decode: the ROM search for a cursor shadow found nothing (see the
   note above), so there is no cartridge number to be faithful to here. The first
   version used the infantry shadow's own 80/255 at the global shadow nudge, and against
   dark grass that read as a faint smudge rather than a shadow -- which is what was seen.
   --curshadowalpha 0..255 and --curshadowoff LEPTONS tune them without a rebuild. */
/* 64/255 is NOT a taste value: it is the peak alpha of the cartridge's own vehicle
   shadow textures, read out of the ROM (they are 64, 64, 65 and 72 across the five).
   the cursor's shadow is deliberately the same strength as the vehicles', so it
   takes their number rather than a number that merely looks similar. The infantry's is
   80/255, slightly heavier, which is why matching the vehicles is the tighter answer. */
static float g_c3dShadowAlpha = 64.0f;     /* of 255 -- the vehicles' own peak */
static float g_c3dShadowOff   = 56.0f;     /* leptons, thrown along the shadow diagonal */

static void c3d_draw_shadow(int code, float wx, float wz, int frame)
{
    if (!g_c3dShadow) return;
    const int mi = c3d_mesh_for(code, frame);
    if (mi < 0) return;
    /* THE SHADOW ANIMATES WITH THE CURSOR. This used to pass -1 for the animation time
       while the body passed the real frame, so a spinning or pulsing cursor cast a
       frozen silhouette -- it was seen immediately. The flipbook cursors were already
       right (their frame picks the MESH, above); it was the PKB node animation that was
       switched off. Same expression as c3d_draw_one uses, so the two cannot drift. */
    const float animT = (frame < 0) ? -1.0f : (float)frame;
    /* A REAL PROJECTED SHADOW, not a blob. The cursor's own mesh is drawn a second
       time, FLATTENED onto the ground and filled flat black, so the shape on the
       terrain is the cursor's actual silhouette -- the reported "the N64's were real 3D
       shadows". The blob this replaces used the infantry shadow art and read as a soft
       smudge that had nothing to do with the cursor's shape.

       The flattening is a 3x4 through draw_mesh's extraMat slot -- the same slot the
       debris chunks use for their tumble -- with the Y row zeroed, so every vertex
       collapses to the model's y=0 plane while x and z are untouched. No new draw path,
       no second copy of the vertex pipeline.

       STILL AUTHORED, and still on the report rather than on a decode: the ROM
       search for a cursor shadow came up empty (all fourteen meshes carry no shadow
       triangles; the type-2 handler calls no shadow routine; its one cursor-specific
       routine only draws the node). What is decoded here is the ART -- it is the
       cursor's own geometry -- and what is ours is the projection and the alpha. */
    static const float FLATTEN[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,      /* y row zeroed: the model collapses to y = 0 */
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    g_meshShadowPass  = true;
    g_meshShadowAlpha = (unsigned char)g_c3dShadowAlpha;
    /* Thrown along the SAME diagonal every other shadow in the game uses (+X east,
       -Z north), just further, because a cursor floats well clear of the ground and a
       shadow directly beneath it reads as dirt rather than as a shadow. */
    /* The throw is in world leptons, so it takes the pointer's zoom factor too, or the
       shadow would drift away from a pointer held at constant size. 1 at the default
       distance and always under Classic. */
    /* Enhanced cursors can be much smaller than their cartridge counterparts.
       Their old world-space throw ignored both size dials, leaving a detached
       silhouette. Keep the shadow close and scale its throw with the same body. */
    const float throwScale = g_fx.enabled
        ? 0.25f * g_fx.cursor_scale * c3d_green_rescale(code) * ecur_rescale(mi) : 1.0f;
    const float ox = g_c3dShadowOff / 256.0f * g_c3dZoomK * throwScale;
    const float oz = -g_c3dShadowOff / 256.0f * g_c3dZoomK * throwScale;
    /* THE SPIN REACHES THE SILHOUETTE, for exactly the reason the animation time above
       does: a wrench that turned while its shadow lay still would be the same defect in
       a different channel. Same call, same argument, so the two cannot drift. */
    const int face = c3d_screen_face(c3d_spin_face(code, frame));
    /* The green pointer's own size dial, undone below with nothing between. */
    const float shadowScaleSave = MODEL_SCALE;
    MODEL_SCALE = shadowScaleSave * c3d_green_rescale(code) * ecur_rescale(mi);
    /* Both passes, so a cursor whose marker is cutout still casts its whole shape. */
    draw_mesh(mi, wx + ox, wz + oz, face, MODE_OPAQUE,
              0, 0.0f, 0, 1.0f, C3D_CURSOR_LIFT, false, WOBBLE_NONE, animT, FLATTEN);
    /* Same coverage rule as the body above, or the marker we just stopped drawing would
       still cast a wedge-shaped shadow on the ground. */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    draw_mesh(mi, wx + ox, wz + oz, face, MODE_CUTOUT,
              0, 0.0f, 0, 1.0f, C3D_CURSOR_LIFT, false, WOBBLE_NONE, animT, FLATTEN);
    glDisable(GL_ALPHA_TEST);
    MODEL_SCALE = shadowScaleSave;
    g_meshShadowPass = false;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
}

/* ONE MODEL, ONE DRAW, TWO PLACES IT IS ASKED FOR.
   The pointer asks for it under the mouse; draw_repair_wrenches asks for CUR05 over a
   repairing building. The three arguments after `frame` are the whole difference between
   those two calls, and they are arguments rather than a second copy of this function so
   that the wrench over the building cannot drift from the wrench under the mouse -- the
   spin, the shading flags, the animation time and the two passes are shared by
   construction.

     ylift  height above the terrain under the anchor, in CELLS. C3D_CURSOR_LIFT for the
            pointer (the same nudge the shadow triangles get, because the ground-marker
            triangle is modelled at exactly y=0 and would z-fight); the building's own
            roof plus a clearance for the wrench, so it floats over the roof.
     depth  how the model meets the depth buffer, one of the three C3D_DEPTH_* below.
            The pointer follows g_c3dOnTop, which is a decoded-vs-reported A/B, and so
            passes TESTED or OVER_NO_DEPTH. The wrench passes OVER_WRITE_DEPTH
            unconditionally: it is drawn over a structure that is taller than it by
            design, so depth-testing it would mean drawing nothing at all on most
            buildings, and it leaves its depth behind for the reason given at the enum.
     marker draw the model's ground-marker triangle -- the cutout pass. */
enum C3dDepth {
    /* An ordinary node: tested and writing, occluded by anything nearer. */
    C3D_DEPTH_TESTED = 0,
    /* Drawn over everything and leaving NO trace in the depth buffer. The pointer's
       on-top path, as it was first written. */
    C3D_DEPTH_OVER_NO_DEPTH = 1,
    /* Drawn over everything AND writing its own depth. The test is left enabled with
       GL_ALWAYS as the function, because a depth write only happens while the test is
       on: a mask of GL_TRUE with the test disabled writes nothing. THE ENHANCED PICTURE
       IS WHY THIS EXISTS. Its light pass reconstructs the surface under every pixel
       from the depth buffer, so a wrench that left no depth was lit as the ROOF behind
       it: the building's shading, its shadow and its occlusion painted onto the slab,
       with the roof's own outline running across the wrench wherever a jaw crossed a
       tower. With its depth written the pass lights the slab as the slab. Classic is
       unchanged by construction: this is the last world pass of the frame and nothing
       that follows it tests depth. Fixed function, nothing a Voodoo 2 cannot do. */
    C3D_DEPTH_OVER_WRITE_DEPTH = 2
};
static void c3d_draw_one(int code, float wx, float wz, int frame,
                         float ylift, C3dDepth depth, bool marker)
{
    if (code < 0 || (code >= 14 && (code < C3D_RA_FIRST || code >= C3D_RA_FIRST + C3D_RA_N))) return;
    /* A flipbook cursor draws one of its per-frame variants instead of the base mesh;
       the variants differ only in the image the G_SETTIMG address selects, which is
       exactly what the console's texture animation rewrites. The green pointer replaces
       code 0x00 here too; see c3d_mesh_for. */
    const int mi = c3d_mesh_for(code, frame);
    if (mi < 0) return;
    /* Its own size dial, restored at the end of this function. */
    const float oneScaleSave = MODEL_SCALE;
    MODEL_SCALE = oneScaleSave * c3d_green_rescale(code) * ecur_rescale(mi);
    /* The PKB node animation, driven off the console's own frame index. Static states
       pass -1 and take the identical path they always did. */
    const float animT = (frame < 0) ? -1.0f : (float)frame;
    /* DEPTH. The cursor is an ordinary node in the console's scene, so it is depth
       TESTED and depth WRITING, and a hill in front of the picked point occludes it.
       That is the decoded behaviour and it is still the default.

       It also means a BUILDING the cursor is standing on cuts through it, because the
       cursor body sits only y 100..250 (0.10..0.24 cell) above the ground while a
       structure is far taller. It was reported exactly that, and --cursorontop lifts the
       cursor out of the depth buffer so it always draws over the scene. That is a
       DEVIATION and is off by default: what the console does about it has not been
       decoded, and the cursor's own draw-command handler (type 2, RAM 0x8004C890) has
       not been read far enough to say. Recorded as a known gap. */
    if (depth == C3D_DEPTH_OVER_NO_DEPTH) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    } else if (depth == C3D_DEPTH_OVER_WRITE_DEPTH) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_TRUE);
    }
    /* The spin, and the ONLY place this model's yaw comes from. See c3d_spin_face:
       every code but 0x05 gets -1 back, which is draw_mesh's "no yaw", so nothing that
       drew still before this line moves after it. */
    const int face = c3d_screen_face(c3d_spin_face(code, frame));
    draw_mesh(mi, wx, wz, face, MODE_OPAQUE, 0, 0.0f, 0, 1.0f, ylift, true,
              WOBBLE_NONE, animT);
    ecur_glow(mi, wx, wz, face, ylift, animT);
    /* THE GROUND MARKER OBEYS ITS OWN ALPHA BIT, and that is a REVERSAL. Ten of the
       fourteen models end their display list with one flat triangle at exactly y=0 --
       a wedge under the floating pointer -- and every one of them textures it from the
       same 2x2 CI8 image, palette index 0x82. Through the cartridge's own TLUT that
       index is (165,165,165) with the **RGBA5551 alpha bit CLEAR**, i.e. the cartridge
       itself marks those four texels transparent.

       This used to draw with the alpha test switched OFF, on the reading that the
       console shows the wedge anyway because its rendermode (G_SETOTHERMODE_L
       0x00553078) does not take coverage from that alpha. That reading put a solid
       165,165,165 wedge under every cursor, and it is exactly the "small grey triangle
       connected to every 3D cursor" was reported on 18 Aug -- twice, the second time
       with a photograph, after a first round had gone looking at the slab's side walls
       instead. He is right and the earlier reading was wrong: the only thing that
       argued for drawing it was an interpretation of footage, while the cartridge's own
       texture data says transparent in as many words.

       So the ordinary cutout rule applies here like everywhere else. The alpha test is
       enabled explicitly rather than inherited, because this is the last world pass and
       what the pass before it left behind is not this function's business.

       Every cursor mesh's only cutout triangle IS this marker (audited across all 14),
       except CUR0C, which is entirely cutout and which no state in c3d_state_for_mouse
       ever selects -- so this switch cannot silently remove a cursor anyone can see. */
    /* AND OVER A BUILDING IT IS NOT DRAWN AT ALL, which is a choice and not an
       omission. The triangle is a GROUND marker: it names the map cell the floating
       pointer is over, and it is modelled at exactly y=0 for that job. A wrench standing
       over a roof is over no cell in that sense -- its anchor cell is the building's own,
       which the building already occupies -- so a marker there would be a claim about the
       ground that the ground cannot see anyway, lying inside the structure's mesh.
       WHAT IT COSTS TODAY, MEASURED RATHER THAN ASSUMED: CUR05 bakes 65 triangles, 64
       opaque and exactly 1 cutout, and that one cutout triangle IS the marker (wrenchdump
       prints all three numbers). So this switch removes a real triangle from a real draw
       and is not a no-op dressed up as a decision. What it does NOT do is change what the
       pointer looks like: the pointer still asks for the marker, and the alpha test still
       throws its texels away on this bake for the reason written above. If a later bake
       gives that triangle opaque texels, the pointer grows a wedge and the wrench does
       not, which is the difference this argument is about. */
    if (marker) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        draw_mesh(mi, wx, wz, face, MODE_CUTOUT, 0, 0.0f, 0, 1.0f, ylift, true,
                  WOBBLE_NONE, animT);
        glDisable(GL_ALPHA_TEST);
    }
    if (depth == C3D_DEPTH_OVER_NO_DEPTH) {
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    } else if (depth == C3D_DEPTH_OVER_WRITE_DEPTH) {
        glDepthFunc(GL_LEQUAL);   /* the frame's own function, set once at its start */
    }
    MODEL_SCALE = oneScaleSave;
}

/* The state this draw would use: the engine's MouseType, unless a script has pinned one
   for the gate. One place, so the dump and the draw can never disagree. */
static int c3d_state_now(int mousetype)
{
    return (g_c3dForceState >= 0 && g_c3dForceState < 20) ? g_c3dForceState
                                                          : c3d_state_for_mouse(mousetype);
}

/* ---- RED ALERT'S STATE TABLE ---------------------------------------------------------
   Red Alert's MouseType (redalert/defines.h) is longer than Tiberian Dawn's and ordered
   differently past the shared head; cursors.h carries its extra states after Tiberian
   Dawn's in the one enum the host speaks, and this is the table that turns each into what
   the cartridge's state rows hold: the model, the frame count of its clock (1 still, 100 a
   cycle) and the no-entry overlay flag. The fifteen states Red Alert shares with Tiberian
   Dawn take Tiberian Dawn's own row, as the cross-reference approved them (the same move,
   no-move, deploy, attack, sell, repair, nuke, air strike, demolitions and area guard; the
   sell-unit dollar is the gold one, as on the cartridge). The rest:

     HEAL           its own disc and cross, pulsing          100
     DAMAGE         its own red arrows, chasing              100
     GREPAIR        its own gold spanner, turning            100
     STAY_ATTACK    the attack cursor (CUR04)                100
     NO_DEPLOY      the deploy diamond (CUR06), still, under the sign
     NO_ENTER       the enter arrows (CUR0B), still, under the sign
     NO_GREPAIR     the gold spanner, still, under the sign
     CHRONO_SELECT  its own green brackets, pulsing          100
     CHRONO_DEST    the move cursor and its closing rings    100
     RADAR_CURSOR   its own frame, drawn over the radar map (c3d_ra_overlay), never on the
                    ground

   The refused cursors are the cartridge's own rule (state rows 14, 15 and 19): the model
   held still with the no-entry sign drawn over it. NO_SCROLL is not a row: the scroll arrow
   is chosen by where the cursor stands, and c3d_draw_body answers it at the map's edge. */
static void c3d_ra_row(int mt, int* code, int* frames, int* noentry)
{
    *frames = 1; *noentry = 0;
    switch (mt) {
    case MOUSE_HEAL:          *code = C3D_RA_HEAL;          *frames = 100; return;
    case MOUSE_DAMAGE:        *code = C3D_RA_DAMAGE;        *frames = 100; return;
    case MOUSE_GREPAIR:       *code = C3D_RA_GREPAIR;       *frames = 100; return;
    case MOUSE_STAY_ATTACK:   *code = 0x04;                 *frames = 100; return;
    case MOUSE_NO_DEPLOY:     *code = 0x06;                 *noentry = 1;  return;
    case MOUSE_NO_ENTER:      *code = 0x0B;                 *noentry = 1;  return;
    case MOUSE_NO_GREPAIR:    *code = C3D_RA_GREPAIR;       *noentry = 1;  return;
    case MOUSE_CHRONO_SELECT: *code = C3D_RA_CHRONO_SELECT; *frames = 100; return;
    case MOUSE_CHRONO_DEST:   *code = C3D_RA_CHRONO_DEST;   *frames = 100; return;
    case MOUSE_RADAR_CURSOR:  *code = C3D_RA_RADAR;                        return;
    default: {
        const int st = c3d_state_for_mouse(mt);
        *code = C3D_STATE[st][0]; *frames = C3D_STATE[st][2]; *noentry = C3D_STATE[st][3];
        return;
    }
    }
}

static int c3d_ra_mouse_now(int mousetype)
{
    return (g_c3dForceMouse >= 0 && g_c3dForceMouse < MOUSE_TYPE_COUNT) ? g_c3dForceMouse
                                                                        : mousetype;
}

/* The row this draw uses: Red Alert's table in the Red Alert module, the cartridge's
   twenty states everywhere else. One place, so the dump and the draw agree. */
static void c3d_row_now(int mousetype, int* code, int* frames, int* noentry)
{
    if (g_raModule) {
        c3d_ra_row(c3d_ra_mouse_now(mousetype), code, frames, noentry);
        return;
    }
    const int st = c3d_state_now(mousetype);
    *code = C3D_STATE[st][0]; *frames = C3D_STATE[st][2]; *noentry = C3D_STATE[st][3];
}

/* THE SCREEN-EDGE SCROLL ARROW, transcribed from ROM 0x4D504..0x4D620.
   The handler tests the cursor's LEPTON position against the map bounds held at RAM
   0x80091EF0 +0x38/+0x3C/+0x40/+0x44 with a 16-lepton margin, and the compare is SIGNED,
   so a cursor already past the boundary trips it too. The order matters and is the
   cartridge's: west first, then east, then north, then south -- which is why a corner
   reads as NW rather than N.

   ONE ARROW MODEL, NOT EIGHT. The eight headings are eight pure Y rotations applied at the
   draw site from the constants at RAM 0x80004964+: 0 N, 0.785398 NW, 1.5708 W, 2.35619 SW,
   3.14159 S, 3.92699 SE, 4.71239 E, 5.49779 NE. The model points -Z, which is north in this
   renderer's convention, unrotated.

   Returns the heading in DEGREES, COUNTER-CLOCKWISE, which is the cartridge's own
   handedness and NOT the engine's. This is not a DirType facing and must never be handed
   to a draw as one; c3d_edge_face below is the only sanctioned way to spend it. Or -1 for
   "not at an edge". */
#define C3D_EDGE_LEPTONS 16
/* THE PUSH POINTER'S THREE NUMBERS, in the config layer beside the constant above rather
   than as literals at the draw site.
   SPREAD is how far each of the four arrows stands out from the centre, in world cells.
   CUR0E is a SOLID FILLED WEDGE, wider than it is long, so four of them with their fat
   tails converging fill the middle in: 0.2579 photographed as one white diamond blob and
   0.3647 as a diamond outline. 0.5500 is the first value at which the four arms read as
   four separate arrows, and G146 asserts that in PIXELS.
   HALF is the size, and it is spent through MODEL_SCALE because draw_mesh HAS NO SCALE
   PARAMETER: the 0.012f in the edge-arrow call below is ylift, its tenth positional
   argument. glScalef is not an option either, because the yaw is applied per vertex on
   the CPU so the Glide port needs no matrix stack. */
#define C3D_PUSH_SPREAD 0.5500f
#define C3D_PUSH_HALF   0.5f
#define C3D_PUSH_LIFT   0.012f
static float c3d_edge_heading(float wx, float wz)
{
    /* Cells to leptons, the engine's own 256 per cell. The bounds are the map rect the
       renderer already keeps: cells g_mapX..g_mapX+g_mapW-1 inclusive. */
    const float lx = wx * 256.0f, lz = wz * 256.0f;
    const float x0 = (float)g_mapX * 256.0f;
    const float y0 = (float)g_mapY * 256.0f;
    const float x1 = (float)(g_mapX + g_mapW - 1) * 256.0f + 255.0f;
    const float y1 = (float)(g_mapY + g_mapH - 1) * 256.0f + 255.0f;
    const float m = (float)C3D_EDGE_LEPTONS;

    if (lx - x0 < m) {                       /* west edge */
        if (lz - y0 < m) return 45.0f;       /* NW */
        if (y1 - lz < m) return 135.0f;      /* SW */
        return 90.0f;                        /* W  */
    }
    if (x1 - lx < m) {                       /* east edge */
        if (lz - y0 < m) return 315.0f;      /* NE */
        if (y1 - lz < m) return 225.0f;      /* SE */
        return 270.0f;                       /* E  */
    }
    if (lz - y0 < m) return 0.0f;            /* N */
    if (y1 - lz < m) return 180.0f;          /* S */
    return -1.0f;
}

/* The same edge, in the units the renderer actually turns a model by. TWO CONVENTIONS MEET
   HERE AND THEY RUN OPPOSITE WAYS, which is the whole reason this is a named function and
   not a multiply at the draw site:

     - c3d_edge_heading hands back the CARTRIDGE's number, a counter-clockwise rotation about
       Y in degrees. Its table reads 90 for west and 270 for east.
     - facing_rot() takes the engine's DirType, which is CLOCKWISE: 0 north, 64 east,
       128 south, 192 west.

   So the conversion is a NEGATION and not a plain scale: face = (360 - degrees) * 256 / 360.
   A plain scale mirrors the arrow east for west, and it does it invisibly, because 0 and 180
   are their own negatives around the circle: north and south come out right and the other six
   headings come out backwards. Anyone reworking this should check a diagonal, not a cardinal.

   Returns -1 for "not at an edge", matching c3d_edge_heading. */
static int c3d_edge_face(float wx, float wz)
{
    const float head = c3d_edge_heading(wx, wz);
    if (head < 0.0f) return -1;
    return (int)((360.0f - head) * 256.0f / 360.0f + 0.5f) & 255;
}

/* THE PUSH POINTER: the cartridge's own scroll arrow, four of them, at half size, pointing
   out of the picked ground point. It is the edge arrow's model because it is the same
   idea -- the view is travelling -- and because it is the one arrow mesh the pack carries.
   THE HALVING IS DONE ON MODEL_SCALE and restored immediately, with no other call between
   the two, because draw_mesh takes no scale and this renderer has no matrix stack to push.
   The four facings are DirType, 0 north 64 east 128 south 192 west; the four offsets are
   the matching directions in cell space, where +x is east and +z is south. */
/* The edge arrow's model: the remastered one under Enhanced. */
static int c3d_edge_mesh(void)
{
    return (ecur_on() && g_ecurEdge >= 0) ? g_ecurEdge : g_c3dEdgeMesh;
}

static void c3d_draw_push(float wx, float wz)
{
    static const int   FACE[4] = { 0, 64, 128, 192 };
    static const float OFF[4][2] = { { 0.0f, -1.0f }, { 1.0f, 0.0f },
                                     { 0.0f,  1.0f }, { -1.0f, 0.0f } };
    const float save = MODEL_SCALE;
    int i;
    const int em = c3d_edge_mesh();
    if (em < 0) return;
    glColor3f(1.0f, 1.0f, 1.0f);
    if (g_c3dOnTop) { glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); }
    MODEL_SCALE = save * C3D_PUSH_HALF * ecur_rescale(em);
    /* UNDER THE PERSPECTIVE ROW'S YAW the four arrows follow the SCREEN's edges, which
       is what they illustrate (rpush_step pushes in screen space): each heading gains
       the yaw in DirType units and each offset is turned by it. The sign runs the other
       way from sprite_facing_bias, because this turns a screen direction into a world
       one where that turns a world direction into a screen one; check a diagonal, not a
       cardinal, as the note above says. At yaw 0 every number here is what it was. */
    {
        const float yaw = (g_camMode == CAM_N64) ? g_camYaw : 0.0f;
        const float c = cosf(yaw), s = sinf(yaw);
        const int   turn = (int)lroundf(yaw * 256.0f / (2.0f * (float)M_PI));
        for (i = 0; i < 4; i++) {
            const float ox = OFF[i][0] * c - OFF[i][1] * s;
            const float oz = OFF[i][0] * s + OFF[i][1] * c;
            const float px = wx + ox * C3D_PUSH_SPREAD * g_c3dZoomK;
            const float pz = wz + oz * C3D_PUSH_SPREAD * g_c3dZoomK;
            draw_mesh(em, px, pz, (FACE[i] + turn) & 255, MODE_OPAQUE, 0, 0.0f, 0, 1.0f,
                      C3D_PUSH_LIFT, true, WOBBLE_NONE, -1.0f);
            ecur_glow(em, px, pz, (FACE[i] + turn) & 255, C3D_PUSH_LIFT, -1.0f);
        }
    }
    MODEL_SCALE = save;
    if (g_c3dOnTop) { glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); }
    glDisable(GL_TEXTURE_2D);
}

static void c3d_draw_body(int mousetype, float wx, float wz);

/* THE CURSOR'S SIZE UNDER ENHANCED: the cursor_scale dial (0.7 by request, 5 Sep 2026).
   MODEL_SCALE is the one knob draw_mesh has, and the push pointer already nests a scale
   of its own inside it, so the multiply wraps the whole draw and is undone on the way
   out. Classic never multiplies, so it never rounds. */
/* AND THROUGH THE ZOOM IT HOLDS ITS SIZE, under Enhanced: cursor_zoom_factor scales the
   model by the ratio of the ground's depth under this screen row now to its depth at the
   default distance, which is exactly 1 there and always under Classic. The tip is the
   model's anchor and a uniform scale about the anchor leaves it on the picked point, so
   picking is untouched. g_c3dZoomK carries the same number to the two things that are
   measured in WORLD units rather than through MODEL_SCALE -- the shadow's throw and the
   push pointer's spread -- and is 1 outside this call, so the repair wrench over a
   building (which draws through c3d_draw_one directly and is a world marker, not the
   pointer) keeps its world size. */
static void c3d_draw(int mousetype, float wx, float wz)
{
    const float save = MODEL_SCALE;
    g_c3dZoomK = cursor_zoom_factor(wx, wz);
    if (g_fx.enabled) MODEL_SCALE = save * g_fx.cursor_scale * g_c3dZoomK;
    c3d_draw_body(mousetype, wx, wz);
    MODEL_SCALE = save;
    g_c3dZoomK = 1.0f;
}

static void c3d_draw_body(int mousetype, float wx, float wz)
{
    if (!c3d_have()) return;

    /* THE PUSH POINTER WINS, and it is asked BEFORE the map-edge arrow rather than folded
       into it. The edge arrow's own block below is left exactly as it was, so with the
       push inactive this function behaves byte for byte as it did. */
    if (g_c3dPush) { c3d_draw_push(wx, wz); return; }

    /* AT THE EDGE the cartridge draws the scroll arrow INSTEAD of the ordinary cursor --
       its handler binds cursorNodeTable[0x0E] and returns, never reaching the state
       table's own model. */
    if (g_c3dEdgeArrow && g_c3dEdgeMesh >= 0) {
        /* DirType units, 256 to the turn, already turned round out of the cartridge's
           counter-clockwise degrees. draw_mesh spends this through facing_rot() with no
           model_face_bias in the way, so what c3d_edge_face returns is what the model turns
           by. */
        const int face = c3d_edge_face(wx, wz);
        g_c3dEdgeFace = face;
        g_c3dEdgeBlocked = false;
        if (face >= 0) {
            glColor3f(1.0f, 1.0f, 1.0f);
            if (g_c3dOnTop) { glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); }
            const int em = c3d_edge_mesh();
            const float esave = MODEL_SCALE;
            MODEL_SCALE = esave * ecur_rescale(em);
            draw_mesh(em, wx, wz, face, MODE_OPAQUE, 0, 0.0f, 0, 1.0f, 0.012f,
                      true, WOBBLE_NONE, -1.0f);
            ecur_glow(em, wx, wz, face, 0.012f, -1.0f);
            MODEL_SCALE = esave;
            if (g_c3dOnTop) { glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); }
            /* RED ALERT'S NO_SCROLL: the view cannot go any further the way the arrow
               points, so the arrow is refused as the cartridge refuses a cursor, held still
               with the no-entry sign over it. The arrow points the way the cartridge's
               counter-clockwise degrees say (0 north, 90 west), which in world cells is
               (-sin, -cos). Tiberian Dawn keeps the plain arrow, as the cartridge does. */
            if (g_raModule) {
                const float h = c3d_edge_heading(wx, wz) * (float)M_PI / 180.0f;
                g_c3dEdgeBlocked = c3d_cam_blocked(-sinf(h), -cosf(h));
                if (g_c3dEdgeBlocked)
                    c3d_draw_one(C3D_NOENTRY_CODE, wx, wz, -1, C3D_CURSOR_LIFT,
                                 g_c3dOnTop ? C3D_DEPTH_OVER_NO_DEPTH : C3D_DEPTH_TESTED, true);
            }
            glDisable(GL_TEXTURE_2D);
            return;
        }
    }

    int code = 0, frames = 1, noentry = 0;
    c3d_row_now(mousetype, &code, &frames, &noentry);
    const int frame = c3d_anim_frame(frames);
    /* Shadow first, so the cursor body composites over it. Drawn once for the whole
       cursor rather than per model, or the no-entry ring would double the darkness. */
    c3d_draw_shadow(code, wx, wz, frame);
    glColor3f(1.0f, 1.0f, 1.0f);
    const C3dDepth pdepth = g_c3dOnTop ? C3D_DEPTH_OVER_NO_DEPTH : C3D_DEPTH_TESTED;
    c3d_draw_one(code, wx, wz, frame, C3D_CURSOR_LIFT, pdepth, true);
    /* The overlay ring is state 3's model, whose own state row carries frameCount 1: it
       is static on the cartridge and stays static here. */
    if (noentry)                       /* no-sell / no-repair: the ring goes on top */
        c3d_draw_one(C3D_NOENTRY_CODE, wx, wz, -1, C3D_CURSOR_LIFT, pdepth, true);
    glDisable(GL_TEXTURE_2D);
}

/* RED ALERT'S RADAR FRAME, OVER THE RADAR MAP. The pointer over the sidebar is a sprite
   everywhere else (the console has no cursor there), and over Red Alert's radar map the
   1996 game shows its radar cursor, a white frame. Under Enhanced the remastered frame is
   drawn in its place: the same model, seen from the game camera's own tilt (55 degrees
   down, from the south) in a pixel-space orthographic view, as large as the 1996 sprite
   it replaces (pxPerCell is the sprite's 24 pixels a cell times its magnification), its
   hot spot on the pointer. Depth is off and the body and glow land in the order the bake
   sorted them, as the world pointer's do on top. Fixed function: two matrices, the
   ordinary mesh draw and the additive glow. False when there is nothing of Red Alert's to
   draw, and the caller then draws the sprite. */
static bool c3d_ra_overlay(int code, float col, float row, float pxPerCell, int fbw, int fbh)
{
    if (!c3d_have() || pxPerCell <= 0.0f) return false;
    const int mi = c3d_mesh_for(code, -1);
    if (mi < 0 || !ecur_ra_is(mi)) return false;
    const float ax = (float)g_mapX + 0.5f, az = (float)g_mapY + 0.5f;
    const float ay = terrain_y(ax, az) + C3D_CURSOR_LIFT;
    const float t = 55.0f * (float)M_PI / 180.0f, c = cosf(t), sn = sinf(t);
    /* world (x east, y up, z south) to screen (x right, y down): column-major */
    const GLfloat tilt[16] = { 1.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, -c,   -sn,  0.0f,
                               0.0f, sn,   -c,   0.0f,
                               0.0f, 0.0f, 0.0f, 1.0f };
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)fbw, (double)fbh, 0.0, -4096.0, 4096.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(col, row, 0.0f);
    glScalef(pxPerCell, pxPerCell, pxPerCell);
    glMultMatrixf(tilt);
    glTranslatef(-ax, -ay, -az);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColor3f(1.0f, 1.0f, 1.0f);
    const float save = MODEL_SCALE;
    const bool noShroud = g_meshNoShroud;    /* a sidebar picture: no fog of war on it */
    g_meshNoShroud = true;
    MODEL_SCALE = 1.0f / 1024.0f;
    draw_mesh(mi, ax, az, -1, MODE_OPAQUE, 0, 0.0f, 0, 1.0f, C3D_CURSOR_LIFT, true,
              WOBBLE_NONE, -1.0f);
    ecur_glow(mi, ax, az, -1, C3D_CURSOR_LIFT, -1.0f);
    MODEL_SCALE = save;
    g_meshNoShroud = noShroud;
    glDepthMask(GL_TRUE);
    if (depth) glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    g_c3dOverlayDraws++;
    return true;
}

/* What the cursor animation is ACTUALLY doing this frame, straight out of the pack and
   the clock. This exists because a screenshot alone cannot tell "the cursor moved" from
   "the camera moved", and an exit code cannot tell either: the per-node delta printed
   here is the same array draw_mesh multiplies the vertices by, so a stationary cursor
   prints zeros no matter how green anything else is. */
static void c3d_ra_dump(int mousetype);

static void c3d_dump(int mousetype)
{
    if (!g_c3dReady) c3d_init();
    if (g_raModule) { c3d_ra_dump(mousetype); return; }
    const int st = c3d_state_now(mousetype);
    const int code = C3D_STATE[st][0];
    const int frames = C3D_STATE[st][2];
    const int frame = c3d_anim_frame(frames);
    const int flip = (g_c3dFlipN[code] > 0 && frame >= 0)
                     ? c3d_flip_index(code, frame) : -1;
    const int mi = c3d_mesh_for(code, frame);
    const int nf = (mi >= 0) ? g_pack.mesh[mi].animFrames : 0;
    const int np = (mi >= 0) ? (int)g_pack.mesh[mi].parts.size() : 0;
    printf("CURSOR3D|usable=%d|draw=%d|anim=%d|state=%d|code=0x%02X|frames=%d|frame=%d"
           "|flipvariants=%d|flip=%d|mesh=%d|animframes=%d|parts=%d\n",
           g_c3dUsable ? 1 : 0, g_c3dDraw ? 1 : 0, g_c3dAnim ? 1 : 0, st, code, frames,
           frame, g_c3dFlipN[code], flip, mi, nf, np);
    /* The green pointer's own line, so a gate can tell the swap happened from the mesh
       index and the size it was drawn at, not from the colour of some pixels. */
    printf("CURSOR3DGREEN|have=%d|on=%d|drawn=%d|mesh=%d|tris=%d|size=%.2f\n",
           g_c3dGreenMesh >= 0 ? 1 : 0, c3d_green_on() ? 1 : 0,
           (code == 0x00 && mi >= 0 && mi == g_c3dGreenMesh) ? 1 : 0, g_c3dGreenMesh,
           g_c3dGreenMesh >= 0 ? (int)g_pack.mesh[g_c3dGreenMesh].tris.size() : 0,
           (g_fx.enabled ? g_fx.cursor_scale : 1.0f) * c3d_green_rescale(code));
    /* The remastered cursors' own line: which mesh this state draws, whether it is the
       remastered one, its body and glow triangles, the size it is drawn at against the
       cartridge's, and the cartridge mesh whose animation it borrowed. */
    {
        int body = 0, glow = 0;
        if (mi >= 0)
            for (size_t t = 0; t < g_pack.mesh[mi].tris.size(); t++)
                (g_pack.mesh[mi].tris[t].mode == MODE_XLU ? glow : body)++;
        printf("CURSOR3DECUR|have=%d|on=%d|drawn=%d|name=%s|body=%d|glow=%d|size=%.4f|scale=%.4f"
               "|cart=%d|cartframes=%d|markdraws=%d\n",
               g_ecurAttached, ecur_on() ? 1 : 0, ecur_is(mi) ? 1 : 0,
               mi >= 0 ? g_pack.mesh[mi].name : "-", body, glow,
               (g_fx.enabled ? g_fx.cursor_scale : 1.0f) * ecur_rescale(mi), ecur_rescale(mi),
               g_c3dMesh[code], g_c3dMesh[code] >= 0 ? g_pack.mesh[g_c3dMesh[code]].animFrames : 0,
               g_ecurMarkDraws);
    }
    if (mi < 0 || nf <= 0 || frame < 0)
        return;
    const PackMesh& m = g_pack.mesh[mi];
    const int f = (frame < nf) ? frame : nf - 1;
    for (int pi = 0; pi < np; pi++) {
        const float* a = &m.animMat[((size_t)f * np + pi) * 12];
        const PackPart& p = m.parts[pi];
        /* The node's own rest anchor carried through this frame's delta. |moved| is the
           distance that node has travelled from its rest pose in MESH units, which is
           the number the cartridge's own tracks are quoted in (cursor 0x06's arms are
           authored to travel 200). The raw translation column is printed too because it
           is what draw_mesh adds, and for a rotating node the two differ. */
        const float mx = a[0] * p.px + a[1] * p.py + a[2]  * p.pz + a[3];
        const float my = a[4] * p.px + a[5] * p.py + a[6]  * p.pz + a[7];
        const float mz = a[8] * p.px + a[9] * p.py + a[10] * p.pz + a[11];
        const float dx = mx - p.px, dy = my - p.py, dz = mz - p.pz;
        printf("CURSOR3DPART|p=%d|tri0=%d|ntris=%d|pivot=%.1f,%.1f,%.1f"
               "|moved=%.2f|t=%.2f,%.2f,%.2f\n",
               pi, p.tri0, p.ntris, p.px, p.py, p.pz,
               sqrtf(dx * dx + dy * dy + dz * dz), a[3], a[7], a[11]);
    }
}

/* Red Alert's line: the mouse state (forced or the engine's), the row it took, the mesh
   drawn for it this frame and whether that mesh is a remastered one (ecur) and Red Alert's
   own (own), the flipbook frame of its count, the spanner's facing, the body and glow
   triangles, and what the last world draw did at the map's edge. A model with tracks
   prints each part's scale this frame. */
static void c3d_ra_dump(int mousetype)
{
    const int mt = c3d_ra_mouse_now(mousetype);
    int code = 0, frames = 1, noentry = 0;
    c3d_ra_row(mt, &code, &frames, &noentry);
    const int frame = c3d_anim_frame(frames);
    const int mi = c3d_mesh_for(code, frame);
    int flip = -1, nflip = 0;
    if (code >= C3D_RA_FIRST && code < C3D_RA_FIRST + C3D_RA_N) {
        const int r = code - C3D_RA_FIRST;
        nflip = g_ecurRaFlipN[r];
        for (int k = 0; k < C3D_RA_MAXFLIP; k++)
            if (g_ecurRaMesh[r][k] == mi && mi >= 0) { flip = k; break; }
    }
    int body = 0, glow = 0;
    if (mi >= 0)
        for (size_t t = 0; t < g_pack.mesh[mi].tris.size(); t++)
            (g_pack.mesh[mi].tris[t].mode == MODE_XLU ? glow : body)++;
    printf("CURSOR3DRA|mouse=%s|forced=%d|code=0x%02X|frames=%d|frame=%d|noentry=%d|mesh=%s"
           "|ecur=%d|own=%d|flip=%d/%d|spin=%d|body=%d|glow=%d|animframes=%d|size=%.4f"
           "|attached=%d|edge=%d|blocked=%d|overlays=%d\n",
           cur_name(mt), g_c3dForceMouse >= 0 ? 1 : 0, code, frames, frame, noentry,
           mi >= 0 ? g_pack.mesh[mi].name : "-", ecur_is(mi) ? 1 : 0, ecur_ra_is(mi) ? 1 : 0,
           flip, nflip, c3d_spin_face(code, frame), body, glow,
           mi >= 0 ? g_pack.mesh[mi].animFrames : 0,
           (g_fx.enabled ? g_fx.cursor_scale : 1.0f) * ecur_rescale(mi), g_ecurRaAttached,
           g_c3dEdgeFace, g_c3dEdgeBlocked ? 1 : 0, g_c3dOverlayDraws);
    if (mi < 0 || frame < 0 || g_pack.mesh[mi].animFrames <= 0) return;
    const PackMesh& m = g_pack.mesh[mi];
    const int np = (int)m.parts.size();
    const int f = frame < m.animFrames ? frame : m.animFrames - 1;
    if (m.animMat.size() < (size_t)m.animFrames * np * 12) return;
    for (int pi = 0; pi < np; pi++)
        printf("CURSOR3DRAPART|p=%d|ntris=%d|scale=%.3f\n", pi, m.parts[pi].ntris,
               m.animMat[((size_t)f * np + pi) * 12]);
}

#endif /* CNC_CURSOR3D_H */
