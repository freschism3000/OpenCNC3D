/* t1_cursor.c -- see t1_cursor.h. */

#include <string.h>
#include <stdio.h>
#include "t1_cursor.h"
#include "t1_glide.h"

/* Cursor STATE table, RAM 0x80099920 / ROM 0x9A520, transcribed byte for byte.
 * {model, model2, frameCount, drawNoEntryOverlay}. Byte +3 == 1 makes the console's draw
 * site at ROM 0x4D854 put a SECOND model on top, hard-coded to state_table[3].model --
 * the red no-entry ring. */
static const unsigned char C3D_STATE[20][4] = {
    { 0,  0,   1, 0}, { 0,  0,   1, 0}, { 1,  1,   1, 0}, { 2,  2,   1, 0},
    { 3,  3, 100, 0}, { 4,  4, 100, 0}, { 5,  5, 100, 0}, { 6,  6, 100, 0},
    { 7,  7,   1, 0}, { 8,  8, 100, 0}, { 9,  9, 100, 0}, {10, 10, 100, 0},
    {11, 11, 100, 0}, {12, 12,   1, 0}, { 7,  7,   1, 1}, { 5,  5,   1, 1},
    { 4,  4, 100, 0}, { 6,  6, 100, 0}, {13, 13,   1, 0}, {13, 13,   1, 1}
};
#define C3D_NOENTRY_CODE 2        /* the overlay model, i.e. state_table[3].model */
#define C3D_FRAME_STEP   4        /* sll v0,v0,2 at RAM 0x8004CB28 */

/* TexAnim time tables, node+0x10 -> TexAnim+0x00. Code 0x0A: RAM 0x801BBE80.
 * Code 0x0B: RAM 0x801BBC40. The last entry is the PERIOD. */
static const int C3D_FLIP_TIMES_0A[5] = {0, 4000, 8000, 12000, 16000};
static const int C3D_FLIP_TIMES_0B[4] = {0, 1600, 3200, 4800};

static const char *T1M_NAME[T1M_COUNT] = {
    "normal", "no-move", "move", "enter", "deploy", "select", "attack",
    "sell", "sell-unit", "repair", "no-repair", "no-sell",
    "ion", "nuke", "airstrike", "demolitions", "guard"
};

const char *t1_cursor_name(const T1_Cursor *c)
{
    return (c->mouse >= 0 && c->mouse < T1M_COUNT) ? T1M_NAME[c->mouse] : "?";
}

/* DOS MouseType -> console cursor state, read out of the cartridge's own ActionType jump
 * table at ROM 0x3FB0 rather than recognised from the art. Three of these were wrong when
 * they were guessed from the models: DEPLOY is state 7 (the grey four-way diamond), not
 * 12 (a tall narrow stack that reads as a squashed sliver at cursor scale); ENTER is 12,
 * not 9 (the air-strike hexagon); AREA_GUARD is 18, not 11 (the ion ground ring). */
static int state_for_mouse(int mt)
{
    switch (mt) {
    case T1M_CAN_MOVE:     return 2;
    case T1M_NO_MOVE:      return 3;
    case T1M_CAN_SELECT:   return 4;
    case T1M_CAN_ATTACK:   return 5;
    case T1M_REPAIR:       return 6;
    case T1M_DEPLOY:       return 7;
    case T1M_ENTER:        return 12;
    case T1M_AREA_GUARD:   return 18;
    case T1M_DEMOLITIONS:  return 13;
    case T1M_ION_CANNON:   return 11;
    case T1M_NUCLEAR_BOMB: return 10;
    case T1M_AIR_STRIKE:   return 9;
    case T1M_SELL_BACK:
    case T1M_SELL_UNIT:    return 8;
    case T1M_NO_SELL_BACK: return 14;
    case T1M_NO_REPAIR:    return 15;
    default:               return 0;
    }
}

/* ActionType -> MouseType. DisplayClass::Mouse_Left_Up, display.cpp:3676-3750, the
 * unshrouded branch, case for case. ACTION_NONE falls to the default exactly as the
 * engine's switch does -- and NONE is what the engine returns when nothing is selected,
 * which is 0 and not -1. */
static int mouse_for_action(int action)
{
    switch (action) {
    case T1A_TOGGLE_SELECT:
    case T1A_SELECT:         return T1M_CAN_SELECT;
    case T1A_MOVE:           return T1M_CAN_MOVE;
    case T1A_GUARD_AREA:     return T1M_AREA_GUARD;
    case T1A_HARVEST:
    case T1A_ATTACK:         return T1M_CAN_ATTACK;
    case T1A_SABOTAGE:       return T1M_DEMOLITIONS;
    case T1A_ENTER:
    case T1A_CAPTURE:        return T1M_ENTER;
    case T1A_NOMOVE:         return T1M_NO_MOVE;
    case T1A_NO_SELL:        return T1M_NO_SELL_BACK;
    case T1A_NO_REPAIR:      return T1M_NO_REPAIR;
    case T1A_SELF:           return T1M_DEPLOY;
    case T1A_REPAIR:         return T1M_REPAIR;
    case T1A_SELL_UNIT:      return T1M_SELL_UNIT;
    case T1A_SELL:           return T1M_SELL_BACK;
    case T1A_ION:            return T1M_ION_CANNON;
    case T1A_NUKE_BOMB:      return T1M_NUCLEAR_BOMB;
    case T1A_AIR_STRIKE:     return T1M_AIR_STRIKE;
    case T1A_TOGGLE_PRIMARY: return T1M_DEPLOY;
    default:                 return T1M_NORMAL;
    }
}

void t1_cursor_init(T1_Cursor *c, const T1_MeshBank *b,
                    void (*rep)(const char *fmt, ...))
{
    static const char *CODE[14] = {
        "CUR00","CUR01","CUR02","CUR03","CUR04","CUR05","CUR06",
        "CUR07","CUR08","CUR09","CUR0A","CUR0B","CUR0C","CUR0D"
    };
    int i, k, have = 0, anim = 0, flips = 0;

    memset(c, 0, sizeof *c);
    c->action = -1;
    c->pin = -1;
    c->qx = c->qz = -999;
    c->qhover = -2;
    c->qsel = -1;
    for (i = 0; i < 14; ++i)
    {
        c->mesh[i] = t1_mesh_for_type(b, CODE[i]);
        if (c->mesh[i] >= 0) ++have;
        if (c->mesh[i] >= 0 && t1_mesh_clip(b, c->mesh[i], 0, 0, 0, 0, 0)) ++anim;
    }
    c->circ = t1_mesh_for_type(b, "CURCIRC");

    /* The flipbook variants, ALL OR NOTHING per code. c3d_flip_index reads the PERIOD
     * out of times[count], so a partial set silently shortens the cycle instead of
     * failing. Four for 0x0A, three for 0x0B; nothing else has any. */
    for (i = 0; i < 14; ++i) c->flipn[i] = 0;
    {
        int n0a = 0, n0b = 0;
        char name[10];
        for (k = 0; k < 4; ++k)
        {
            _snprintf(name, sizeof name, "CUR0AF%d", k);
            c->flip[0x0A][k] = t1_mesh_for_type(b, name);
            if (c->flip[0x0A][k] >= 0) ++n0a;
        }
        for (k = 0; k < 3; ++k)
        {
            _snprintf(name, sizeof name, "CUR0BF%d", k);
            c->flip[0x0B][k] = t1_mesh_for_type(b, name);
            if (c->flip[0x0B][k] >= 0) ++n0b;
        }
        if (n0a == 4) { c->flipn[0x0A] = 4; ++flips; }
        if (n0b == 3) { c->flipn[0x0B] = 3; ++flips; }
    }

    /* Unless every base model resolves, the 2D pointer stays everywhere. Half a cursor
     * set is worse than none: the states that resolve would change under the pointer and
     * the ones that do not would blink out. */
    c->ready = (have == 14);
    if (rep)
        rep("cursor3d: %d of 14 models, %d with node animation, %d flipbook set(s), "
            "circle=%s -> %s", have, anim, flips,
            c->circ >= 0 ? "yes" : "no",
            c->ready ? "3D" : "2D ONLY (a model is missing)");
}

static int flip_index(const T1_Cursor *c, int code, int frame)
{
    const int *times = (code == 0x0A) ? C3D_FLIP_TIMES_0A
                     : (code == 0x0B) ? C3D_FLIP_TIMES_0B : 0;
    int n = c->flipn[code], i, period, tt;
    if (!times || n <= 0 || frame < 0) return 0;
    period = times[n];
    /* RAM 0x80080FAC's rule: tt = (u32)t mod period, then the first slot whose upper
     * bound is not less than tt. t is the clip time, frame*160 + 1. */
    tt = (frame * 160 + 1) % period;
    for (i = 0; i < n; ++i)
        if (tt <= times[i + 1]) return i;
    return 0;
}

/* THE CLOCK, resident code RAM 0x8004CB10: frame = (frameCounter * 4) mod frameCount, so
 * `frame` only ever takes the 25 values 0, 4, 8 .. 96. Handed to the mesh as an INTEGER,
 * because t1_mesh_draw_p clamps rather than wraps at the last baked frame and a
 * fractional 96..99 would lerp toward a pose the console never shows.
 *
 * The counter is the ENGINE TICK. The cartridge's is a rendered-frame counter whose
 * wall-clock rate was never recovered, so the cycle length in seconds is ours (25 ticks,
 * 1.67 s at 15 Hz) and it matches the desktop build's. */
static int anim_frame(int frames, long ticks)
{
    if (frames <= 1) return -1;
    return (int)((ticks * C3D_FRAME_STEP) % (long)frames);
}

/* Screen pixel -> the ground point, ON THE HEIGHTFIELD rather than on a flat plane.
 * t1_screen_to_plane answers for one horizontal plane; a cursor that used it would sink
 * into hills and float over valleys. March along the ray between the terrain's extremes
 * and then bisect: 24 steps plus 12 halvings is exact to well under a cell and costs
 * nothing once a frame. */
void t1_cursor_ground(const T1_Terrain *t, const T1_Cam *cam, const T1_Screen *scr,
                      float col, float row, float *wx, float *wz)
{
    float lo = -1.0f, hi = 3.0f;     /* cells; the pack's heights sit inside this */
    float x0, z0, x1, z1, h0, h1;
    int i;

    t1_screen_to_plane(cam, scr, col, row, hi, &x0, &z0);
    t1_screen_to_plane(cam, scr, col, row, lo, &x1, &z1);
    /* Walk from the high plane toward the low one until the ray passes under the ground. */
    h0 = 1.0f;
    for (i = 0; i <= 24; ++i)
    {
        float u = (float)i / 24.0f;
        float px = x0 + (x1 - x0) * u, pz = z0 + (z1 - z0) * u;
        float py = hi + (lo - hi) * u;
        float g  = t1_terrain_corner_y(t, (int)px, (int)pz);
        if (py <= g) { h0 = u; break; }
        h0 = u;
    }
    h1 = h0;
    {
        float a = (h0 > 0.0f) ? h0 - (1.0f / 24.0f) : 0.0f, bq = h0;
        for (i = 0; i < 12; ++i)
        {
            float m = (a + bq) * 0.5f;
            float px = x0 + (x1 - x0) * m, pz = z0 + (z1 - z0) * m;
            float py = hi + (lo - hi) * m;
            if (py <= t1_terrain_corner_y(t, (int)px, (int)pz)) bq = m; else a = m;
        }
        h1 = bq;
    }
    *wx = x0 + (x1 - x0) * h1;
    *wz = z0 + (z1 - z0) * h1;
}

void t1_cursor_update(T1_Cursor *c, const T1_Terrain *t, const T1_Cam *cam,
                      const T1_Screen *scr, int mx, int my,
                      int in_panel, int selected, int ctrl, int alt,
                      int sell, int repair, int shown,
                      const W98_Object *hover, long ticks)
{
    int cx, cz, mt;

    /* Over the panel, or picking a building site: the 2D pointer, and nothing else. The
     * placement preview is already answering the "can it go here" question and a second
     * answer on top of it would only argue with the first. */
    if (in_panel)
    { c->onmap = 0; c->mouse = T1M_NORMAL; c->action = -1; return; }

    t1_cursor_ground(t, cam, scr, (float)mx, (float)my, &c->wx, &c->wz);
    cx = (int)c->wx;
    cz = (int)c->wz;
    if (c->wx < 0.0f || c->wz < 0.0f || cx > 63 || cz > 63)
    { c->onmap = 0; c->mouse = T1M_NORMAL; c->action = -1; return; }
    c->onmap = 1;
    c->cellx = cx;
    c->cellz = cz;

    /* The two latched sidebar modes come before anything the engine could say, because
     * they are modes of the UI rather than of the world. */
    if (sell)   { c->mouse = selected ? T1M_SELL_UNIT : T1M_SELL_BACK; goto done; }
    if (repair) { c->mouse = T1M_REPAIR; goto done; }

    /* Nothing selected: the question is only ever "can I pick this up". */
    if (selected <= 0)
    { c->mouse = hover ? T1M_CAN_SELECT : T1M_NORMAL; c->action = -1; goto done; }

    /* THE SHROUD GATE COMES BEFORE THE PROBE. An unmapped cell is a cell the player is
     * not allowed to know anything about, and asking the engine what would happen there
     * would leak exactly that. */
    if (!shown) { c->mouse = T1M_CAN_MOVE; c->action = -1; goto done; }

    if (ctrl && alt)      { c->mouse = T1M_AREA_GUARD; c->action = -1; goto done; }
    if (ctrl)             { c->mouse = T1M_CAN_ATTACK; c->action = -1; goto done; }

    /* THE ENGINE'S OWN VERDICT. Asked about the cell the pointer is over -- or, when the
     * pointer is over an object, about THAT object's own cell, because an infantryman
     * standing near the edge of his cell must still answer for himself. The engine prints
     * a line and fflushes on every call, so it is asked once per object dump rather than
     * once per frame; the answer cannot change without the world changing. */
    {
        int qx = cx, qz = cz;
        int hid = hover ? hover->id : -1;
        if (hover) { qx = hover->clx / 256; qz = hover->cly / 256; }
        /* CACHED. The answer cannot change unless the cell, the object under the pointer,
         * the size of the selection or the modifier state changes -- and the pointer
         * crosses a cell boundary many times a second. Re-asked anyway once a second so a
         * world that changed underneath a still pointer (a unit driving into the cell, a
         * building finishing) is not answered from a stale verdict. */
        if (qx != c->qx || qz != c->qz || hid != c->qhover
            || selected != c->qsel || ticks - c->qtick >= 15)
        {
            c->action = wb_action_at_cell(qx, qz);
            c->qx = qx; c->qz = qz; c->qhover = hid;
            c->qsel = selected; c->qtick = ticks;
            ++c->probes;
        }
    }
    if (c->action < 0) c->mouse = T1M_CAN_MOVE;
    else               c->mouse = mouse_for_action(c->action);

done:
    mt = c->mouse;
    c->state = (c->pin >= 0 && c->pin < 20) ? c->pin : state_for_mouse(mt);
    c->code  = C3D_STATE[c->state][0];
    c->frame = anim_frame(C3D_STATE[c->state][2], ticks);
}

/* One model of the cursor, both triangle modes, at the picked point. */
static long draw_one(T1_Cursor *c, T1_MeshBank *b, const T1_Terrain *t,
                     const T1_Cam *cam, const T1_Screen *scr,
                     int code, int frame, float ox, float oz,
                     const float *extra, float ylift)
{
    T1_MeshParams mp;
    int mi;
    if (code < 0 || code >= 14) return 0;
    mi = c->mesh[code];
    /* A flipbook cursor draws one of its per-frame variants instead of the base mesh; the
     * variants differ only in the image the console's G_SETTIMG rewrite selects. */
    if (c->flipn[code] > 0) mi = c->flip[code][frame < 0 ? 0 : flip_index(c, code, frame)];
    if (mi < 0) return 0;

    memset(&mp, 0, sizeof mp);
    mp.mesh = mi;
    mp.wx = c->wx + ox;
    mp.wz = c->wz + oz;
    /* The ground marker triangle is modelled at exactly y = 0 and would z-fight the
     * terrain without the same lift the shadow decals get. */
    mp.wy = t1_terrain_corner_y(t, (int)mp.wx, (int)mp.wz) + ylift;
    mp.facing = 0;                 /* the console's cursor node takes no facing */
    mp.animT = (frame < 0) ? -1.0f : (float)frame;
    mp.build_frac = 1.0f;
    mp.extra = extra;
    mp.modemask = T1_MASK_SOLID;
    return t1_mesh_draw_p(b, 0, cam, scr, &mp);
}

long t1_cursor_draw(T1_Cursor *c, T1_MeshBank *b, const T1_Terrain *t,
                    const T1_Cam *cam, const T1_Screen *scr, long ticks)
{
    /* The shadow's flattening, through the mesh draw's `extra` slot: the Y row zeroed, so
     * every vertex collapses onto the model's own y = 0 plane while x and z are untouched
     * and the silhouette on the ground is the cursor's real shape. */
    static const float FLATTEN[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    long drawn = 0;
    int hold;

    if (!c->ready || !c->onmap) return 0;

    /* THIS PASS SETS ITS OWN STATE AND INHERITS NOTHING. By the time the frame reaches
     * here the infantry pass has swapped the TMU palette to the sprite one and the
     * effects pass has turned the chroma key OFF and not put it back, so a cursor drawn
     * on inherited state would come out in the infantry palette with its cutout holes
     * filled solid. */
    t1_glide_palette(b->pal);
    t1_glide_ckey(1, 0x00FF00FF);
    t1_glide_filter(0);
    b->house = 0;

    /* THE CURSOR DRAWS OVER THE SCENE rather than inside it. the project owner tested both on the
     * desktop build and confirmed this one: depth-tested, the cursor is cut through by
     * any building it stands on, because its body sits only a tenth of a cell above the
     * ground. Done by turning the depth test off, never by nudging w, which on this card
     * would walk the vertex into the W-buffer's one-cell near reject. */
    t1_glide_depth(0);
    hold = 1;

    /* The shadow first, so the body composites over it, and once for the whole cursor
     * rather than per model or the no-entry ring would double the darkness.
     *
     * STILL AUTHORED, and inherited as such from the desktop build: the ROM search for a
     * cursor shadow came up empty. What is decoded is the ART, which is the cursor's own
     * geometry; the projection and the alpha are ours. Thrown along the same diagonal
     * every other shadow in the game uses, 56 leptons, just further, because a cursor
     * floats clear of the ground and a shadow directly beneath it reads as dirt. */
    {
        float a3[3];
        a3[0] = a3[1] = a3[2] = 64.0f / 255.0f;   /* the vehicles' own peak */
        t1_glide_blend(1);
        t1_glide_alpha3(a3);
        drawn += draw_one(c, b, t, cam, scr, c->code, c->frame,
                          56.0f / 256.0f, -56.0f / 256.0f, FLATTEN, 0.012f);
        t1_glide_alpha3(0);
        t1_glide_blend(0);
    }

    drawn += draw_one(c, b, t, cam, scr, c->code, c->frame, 0.0f, 0.0f, 0, 0.012f);
    /* The overlay ring is state 3's model, whose own state row carries frameCount 1: it
     * is static on the cartridge and stays static here. */
    if (C3D_STATE[c->state][3])
        drawn += draw_one(c, b, t, cam, scr, C3D_NOENTRY_CODE, -1, 0.0f, 0.0f, 0, 0.012f);

    if (hold) t1_glide_depth(1);
    ++c->drawn;
    return drawn;
}
