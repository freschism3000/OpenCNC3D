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

static bool g_c3dReady = false;
static bool g_c3dUsable = false;
/* The gate's own control, the same instrument g_wallsDraw and g_ridersDraw are. */
static bool g_c3dDraw = true;
static bool g_c3dAnim = true;    /* the animation's own control leg */
/* -1 = follow the engine's MouseType. >= 0 forces one state, for the gate: several
   animated states (deploy, the superweapons) need game situations no shipped mission can
   reach on demand, and a cursor that cannot be put on screen cannot be asserted. */
static int  g_c3dForceState = -1;

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
    if (g_c3dUsable) {
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
static int c3d_spin_face(int code, int frame)
{
    if (code != C3D_WRENCH_CODE || frame < 0)
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
    int mi = g_c3dMesh[code];
    if (g_c3dFlipN[code] > 0)
        mi = g_c3dFlip[code][frame < 0 ? 0 : c3d_flip_index(code, frame)];
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
    const float ox = g_c3dShadowOff / 256.0f, oz = -g_c3dShadowOff / 256.0f;
    /* THE SPIN REACHES THE SILHOUETTE, for exactly the reason the animation time above
       does: a wrench that turned while its shadow lay still would be the same defect in
       a different channel. Same call, same argument, so the two cannot drift. */
    const int face = c3d_spin_face(code, frame);
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
            half-extent for the wrench, so it floats over the roof.
     ontop  lift the model out of the depth buffer. The pointer follows g_c3dOnTop, which
            is a decoded-vs-reported A/B. The wrench passes true unconditionally: it is
            drawn over a structure that is taller than it by design, so depth-testing it
            would mean drawing nothing at all on most buildings.
     marker draw the model's ground-marker triangle -- the cutout pass. */
static void c3d_draw_one(int code, float wx, float wz, int frame,
                         float ylift, bool ontop, bool marker)
{
    if (code < 0 || code >= 14) return;
    int mi = g_c3dMesh[code];
    /* A flipbook cursor draws one of its per-frame variants instead of the base mesh;
       the variants differ only in the image the G_SETTIMG address selects, which is
       exactly what the console's texture animation rewrites. */
    if (g_c3dFlipN[code] > 0)
        mi = g_c3dFlip[code][frame < 0 ? 0 : c3d_flip_index(code, frame)];
    if (mi < 0) return;
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
    if (ontop) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
    /* The spin, and the ONLY place this model's yaw comes from. See c3d_spin_face:
       every code but 0x05 gets -1 back, which is draw_mesh's "no yaw", so nothing that
       drew still before this line moves after it. */
    const int face = c3d_spin_face(code, frame);
    draw_mesh(mi, wx, wz, face, MODE_OPAQUE, 0, 0.0f, 0, 1.0f, ylift, true,
              WOBBLE_NONE, animT);
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
    if (ontop) {
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }
}

/* The state this draw would use: the engine's MouseType, unless a script has pinned one
   for the gate. One place, so the dump and the draw can never disagree. */
static int c3d_state_now(int mousetype)
{
    return (g_c3dForceState >= 0 && g_c3dForceState < 20) ? g_c3dForceState
                                                          : c3d_state_for_mouse(mousetype);
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
static void c3d_draw_push(float wx, float wz)
{
    static const int   FACE[4] = { 0, 64, 128, 192 };
    static const float OFF[4][2] = { { 0.0f, -1.0f }, { 1.0f, 0.0f },
                                     { 0.0f,  1.0f }, { -1.0f, 0.0f } };
    const float save = MODEL_SCALE;
    int i;
    if (g_c3dEdgeMesh < 0) return;
    glColor3f(1.0f, 1.0f, 1.0f);
    if (g_c3dOnTop) { glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); }
    MODEL_SCALE = save * C3D_PUSH_HALF;
    for (i = 0; i < 4; i++)
        draw_mesh(g_c3dEdgeMesh,
                  wx + OFF[i][0] * C3D_PUSH_SPREAD,
                  wz + OFF[i][1] * C3D_PUSH_SPREAD,
                  FACE[i], MODE_OPAQUE, 0, 0.0f, 0, 1.0f, C3D_PUSH_LIFT,
                  true, WOBBLE_NONE, -1.0f);
    MODEL_SCALE = save;
    if (g_c3dOnTop) { glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); }
    glDisable(GL_TEXTURE_2D);
}

static void c3d_draw(int mousetype, float wx, float wz)
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
        if (face >= 0) {
            glColor3f(1.0f, 1.0f, 1.0f);
            if (g_c3dOnTop) { glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); }
            draw_mesh(g_c3dEdgeMesh, wx, wz, face, MODE_OPAQUE, 0, 0.0f, 0, 1.0f, 0.012f,
                      true, WOBBLE_NONE, -1.0f);
            if (g_c3dOnTop) { glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); }
            glDisable(GL_TEXTURE_2D);
            return;
        }
    }

    const int st = c3d_state_now(mousetype);
    const int frame = c3d_anim_frame(C3D_STATE[st][2]);
    /* Shadow first, so the cursor body composites over it. Drawn once for the whole
       cursor rather than per model, or the no-entry ring would double the darkness. */
    c3d_draw_shadow(C3D_STATE[st][0], wx, wz, frame);
    glColor3f(1.0f, 1.0f, 1.0f);
    c3d_draw_one(C3D_STATE[st][0], wx, wz, frame, C3D_CURSOR_LIFT, g_c3dOnTop, true);
    /* The overlay ring is state 3's model, whose own state row carries frameCount 1: it
       is static on the cartridge and stays static here. */
    if (C3D_STATE[st][3])              /* no-sell / no-repair: the ring goes on top */
        c3d_draw_one(C3D_NOENTRY_CODE, wx, wz, -1, C3D_CURSOR_LIFT, g_c3dOnTop, true);
    glDisable(GL_TEXTURE_2D);
}

/* What the cursor animation is ACTUALLY doing this frame, straight out of the pack and
   the clock. This exists because a screenshot alone cannot tell "the cursor moved" from
   "the camera moved", and an exit code cannot tell either: the per-node delta printed
   here is the same array draw_mesh multiplies the vertices by, so a stationary cursor
   prints zeros no matter how green anything else is. */
static void c3d_dump(int mousetype)
{
    if (!g_c3dReady) c3d_init();
    const int st = c3d_state_now(mousetype);
    const int code = C3D_STATE[st][0];
    const int frames = C3D_STATE[st][2];
    const int frame = c3d_anim_frame(frames);
    const int flip = (g_c3dFlipN[code] > 0 && frame >= 0)
                     ? c3d_flip_index(code, frame) : -1;
    int mi = g_c3dMesh[code];
    if (g_c3dFlipN[code] > 0)
        mi = g_c3dFlip[code][flip < 0 ? 0 : flip];
    const int nf = (mi >= 0) ? g_pack.mesh[mi].animFrames : 0;
    const int np = (mi >= 0) ? (int)g_pack.mesh[mi].parts.size() : 0;
    printf("CURSOR3D|usable=%d|draw=%d|anim=%d|state=%d|code=0x%02X|frames=%d|frame=%d"
           "|flipvariants=%d|flip=%d|mesh=%d|animframes=%d|parts=%d\n",
           g_c3dUsable ? 1 : 0, g_c3dDraw ? 1 : 0, g_c3dAnim ? 1 : 0, st, code, frames,
           frame, g_c3dFlipN[code], flip, mi, nf, np);
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

#endif /* CNC_CURSOR3D_H */
