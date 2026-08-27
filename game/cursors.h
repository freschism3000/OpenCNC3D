/*
 * cursors.h -- the 1995 MS-DOS Command & Conquer mouse cursor, as a lift-and-drop
 * module for the CNC3D renderer.
 *
 * WHAT IT IS. MOUSE.SHP holds the 178 cursor frames the DOS game ever shows; the
 * table that says which frames mean what (start frame, animation length, animation
 * rate, hotspot) is MouseClass::MouseControl, mouse.cpp:304, transcribed below
 * NUMBER FOR NUMBER with its source cited. The frames travel in dossidebar.pack as
 * the shape "MOUSE" (raw 8-bit palette indices, index 0 transparent, exactly like
 * every other shape in the pack; see bake_dossidebar.py).
 *
 * HOW IT DRAWS. One 32x32 RGBA texture is allocated once. Whenever the frame to
 * show changes (cursor changed, or its animation stepped), the new 30x24 frame is
 * converted CPU-side through the pack's own pal8 and glTexSubImage2D'd into that
 * texture; then one textured quad is drawn at the mouse position, hotspot applied,
 * magnified by the same whole-number scale as the DOS sidebar so the cursor's
 * pixels are the same size as every other 1995 pixel on screen. GL 1.1 only:
 * no shaders, no extensions, GL_NEAREST, GL_ALPHA_TEST for the cutout. A frame
 * is 2,880 bytes; even at the fastest animation rate (sell/repair, 30 fps) the
 * upload traffic is under 90 KB/s, nothing for AGP or for Glide's texture memory
 * (the Voodoo2 port keeps the same single 32x32 texture and refills it).
 *
 * ANIMATION TIMING. mouse.cpp AI(): when FrameRate is non-zero, Frame advances
 * (mod FrameCount) every time a countdown timer set to FrameRate expires. The
 * timer counts the 60 Hz system tick, so one FrameRate unit is 1/60 s: rate 4 is
 * 15 fps (attack, select, deploy), rate 2 is 30 fps (sell, repair, radar).
 *
 * WHAT PICKS THE CURSOR. Not this file. The host asks the SAME probe the order
 * path uses (CNC3D_Probe_Object_At -> Best_Object_Action) and maps the ActionType
 * through cur_mouse_for_action(), which is DisplayClass::Mouse_Left_Up's
 * unshrouded switch (display.cpp:3676-3750) transcribed case for case. This
 * renderer draws no shroud, so the shadow branch does not apply.
 *
 * This header is written to be included from cnc_eyes.cpp after cnc_sidebar.h
 * (it needs dosbar.h's DB_Pack/DB_Shape and an OpenGL 1.1 header, nothing else).
 */

#ifndef CNC_CURSORS_H
#define CNC_CURSORS_H

/* ---- MouseType, defines.h:1778 ---------------------------------------------------- */
enum {
    MOUSE_NORMAL = 0,
    MOUSE_N, MOUSE_NE, MOUSE_E, MOUSE_SE, MOUSE_S, MOUSE_SW, MOUSE_W, MOUSE_NW,
    MOUSE_NO_N, MOUSE_NO_NE, MOUSE_NO_E, MOUSE_NO_SE, MOUSE_NO_S, MOUSE_NO_SW,
    MOUSE_NO_W, MOUSE_NO_NW,
    MOUSE_NO_MOVE, MOUSE_CAN_MOVE, MOUSE_ENTER, MOUSE_DEPLOY, MOUSE_CAN_SELECT,
    MOUSE_CAN_ATTACK, MOUSE_SELL_BACK, MOUSE_SELL_UNIT, MOUSE_REPAIR,
    MOUSE_NO_REPAIR, MOUSE_NO_SELL_BACK, MOUSE_RADAR_CURSOR, MOUSE_ION_CANNON,
    MOUSE_NUCLEAR_BOMB, MOUSE_AIR_STRIKE, MOUSE_DEMOLITIONS, MOUSE_AREA_GUARD,
    MOUSE_TYPE_COUNT
};

/* ---- ActionType, defines.h:546 (only the values, for the mapping below) ----------- */
enum {
    CUR_ACTION_NONE = 0, CUR_ACTION_MOVE, CUR_ACTION_NOMOVE, CUR_ACTION_ENTER,
    CUR_ACTION_SELF, CUR_ACTION_ATTACK, CUR_ACTION_HARVEST, CUR_ACTION_SELECT,
    CUR_ACTION_TOGGLE_SELECT, CUR_ACTION_CAPTURE, CUR_ACTION_REPAIR,
    CUR_ACTION_SELL, CUR_ACTION_SELL_UNIT, CUR_ACTION_NO_SELL,
    CUR_ACTION_NO_REPAIR, CUR_ACTION_SABOTAGE, CUR_ACTION_ION,
    CUR_ACTION_NUKE_BOMB, CUR_ACTION_AIR_STRIKE, CUR_ACTION_GUARD_AREA,
    CUR_ACTION_TOGGLE_PRIMARY, CUR_ACTION_NO_DEPLOY
};

/* ---- MouseClass::MouseControl, mouse.cpp:304, transcribed number for number ------- *
 * {StartFrame, FrameCount, FrameRate, SmallFrame, HotspotX, HotspotY}.
 * SmallFrame is the radar-mode miniature; this renderer never enters that mode but
 * the column is kept so the table stays diffable against the engine's. */
typedef struct {
    int start, count, rate, small_, hx, hy;
    const char* name;
} CurControl;

static const CurControl CUR_CONTROL[MOUSE_TYPE_COUNT] = {
    {0,   1,  0, 86,  0,  0,  "normal"},       /* MOUSE_NORMAL       */
    {1,   1,  0, -1,  15, 0,  "n"},            /* MOUSE_N            */
    {2,   1,  0, -1,  29, 0,  "ne"},           /* MOUSE_NE           */
    {3,   1,  0, -1,  29, 12, "e"},            /* MOUSE_E            */
    {4,   1,  0, -1,  29, 23, "se"},           /* MOUSE_SE           */
    {5,   1,  0, -1,  15, 23, "s"},            /* MOUSE_S            */
    {6,   1,  0, -1,  0,  23, "sw"},           /* MOUSE_SW           */
    {7,   1,  0, -1,  0,  13, "w"},            /* MOUSE_W            */
    {8,   1,  0, -1,  0,  0,  "nw"},           /* MOUSE_NW           */
    {130, 1,  0, -1,  15, 0,  "no-n"},         /* MOUSE_NO_N         */
    {131, 1,  0, -1,  29, 0,  "no-ne"},        /* MOUSE_NO_NE        */
    {132, 1,  0, -1,  29, 12, "no-e"},         /* MOUSE_NO_E         */
    {133, 1,  0, -1,  29, 23, "no-se"},        /* MOUSE_NO_SE        */
    {134, 1,  0, -1,  15, 23, "no-s"},         /* MOUSE_NO_S         */
    {135, 1,  0, -1,  0,  23, "no-sw"},        /* MOUSE_NO_SW        */
    {136, 1,  0, -1,  0,  13, "no-w"},         /* MOUSE_NO_W         */
    {137, 1,  0, -1,  0,  0,  "no-nw"},        /* MOUSE_NO_NW        */
    {11,  1,  0, 27,  15, 12, "no-move"},      /* MOUSE_NO_MOVE      */
    {10,  1,  0, 26,  15, 12, "move"},         /* MOUSE_CAN_MOVE     */
    {119, 3,  4, 148, 15, 12, "enter"},        /* MOUSE_ENTER        */
    {53,  9,  4, -1,  15, 12, "deploy"},       /* MOUSE_DEPLOY       */
    {12,  6,  4, -1,  15, 12, "select"},       /* MOUSE_CAN_SELECT   */
    {18,  8,  4, 140, 15, 12, "attack"},       /* MOUSE_CAN_ATTACK   */
    {62,  24, 2, -1,  15, 12, "sell"},         /* MOUSE_SELL_BACK    */
    {154, 24, 2, -1,  15, 12, "sell-unit"},    /* MOUSE_SELL_UNIT    */
    {29,  24, 2, -1,  15, 12, "repair"},       /* MOUSE_REPAIR       */
    {126, 1,  0, -1,  15, 12, "no-repair"},    /* MOUSE_NO_REPAIR    */
    {125, 1,  0, -1,  15, 12, "no-sell"},      /* MOUSE_NO_SELL_BACK */
    {87,  1,  0, 151, 0,  0,  "radar"},        /* MOUSE_RADAR_CURSOR */
    {103, 16, 2, -1,  15, 12, "ion"},          /* MOUSE_ION_CANNON   */
    {96,  7,  4, -1,  15, 12, "nuke"},         /* MOUSE_NUCLEAR_BOMB */
    {88,  8,  2, -1,  15, 12, "airstrike"},    /* MOUSE_AIR_STRIKE   */
    {122, 3,  4, 127, 15, 12, "demolitions"},  /* MOUSE_DEMOLITIONS  */
    {153, 1,  0, 152, 15, 12, "guard"},        /* MOUSE_AREA_GUARD   */
};

/* ---- ActionType -> MouseType ------------------------------------------------------ *
 * DisplayClass::Mouse_Left_Up, display.cpp:3676-3750, the unshrouded branch,
 * transcribed case for case. ACTION_NONE falls to the default, MOUSE_NORMAL,
 * exactly as the engine's switch does. */
static int cur_mouse_for_action(int action)
{
    switch (action) {
    case CUR_ACTION_TOGGLE_SELECT:
    case CUR_ACTION_SELECT:         return MOUSE_CAN_SELECT;
    case CUR_ACTION_MOVE:           return MOUSE_CAN_MOVE;
    case CUR_ACTION_GUARD_AREA:     return MOUSE_AREA_GUARD;
    case CUR_ACTION_HARVEST:
    case CUR_ACTION_ATTACK:         return MOUSE_CAN_ATTACK;
    case CUR_ACTION_SABOTAGE:       return MOUSE_DEMOLITIONS;
    case CUR_ACTION_ENTER:
    case CUR_ACTION_CAPTURE:        return MOUSE_ENTER;
    case CUR_ACTION_NOMOVE:         return MOUSE_NO_MOVE;
    case CUR_ACTION_NO_SELL:        return MOUSE_NO_SELL_BACK;
    case CUR_ACTION_NO_REPAIR:      return MOUSE_NO_REPAIR;
    case CUR_ACTION_SELF:           return MOUSE_DEPLOY;
    case CUR_ACTION_REPAIR:         return MOUSE_REPAIR;
    case CUR_ACTION_SELL_UNIT:      return MOUSE_SELL_UNIT;
    case CUR_ACTION_SELL:           return MOUSE_SELL_BACK;
    case CUR_ACTION_ION:            return MOUSE_ION_CANNON;
    case CUR_ACTION_NUKE_BOMB:      return MOUSE_NUCLEAR_BOMB;
    case CUR_ACTION_AIR_STRIKE:     return MOUSE_AIR_STRIKE;
    case CUR_ACTION_TOGGLE_PRIMARY: return MOUSE_DEPLOY;
    /* The engine's own switch has no case for this and falls to the plain arrow, which
       is faithful because 1995 answered a refused deploy with a spoken line instead. We
       do not play that line, so an unanswered click would be the only feedback there is.
       The no-move cursor is the nearest thing the cartridge already carries: it is what
       every other "the engine will not do this" answer uses. */
    case CUR_ACTION_NO_DEPLOY:      return MOUSE_NO_MOVE;
    default:                        return MOUSE_NORMAL;
    }
}

/* ---- state ------------------------------------------------------------------------ */

#define CUR_TEX_POT 32   /* one 30x24 frame lives in a 32x32 POT texture */

static const DB_Shape*      g_curShape = NULL;   /* "MOUSE" in dossidebar.pack */
static const unsigned char* g_curPal8 = NULL;    /* the pack's widened palette */
static GLuint g_curTex = 0;
static int    g_curType = MOUSE_NORMAL;  /* which cursor (MouseType)            */
static int    g_curFrame = 0;            /* animation frame within that cursor  */
static int    g_curUploaded = -1;        /* absolute MOUSE frame in the texture */
static double g_curStepMs = 0.0;         /* when the animation last stepped     */

/* THE OVERRIDE LATCH: mouse.cpp:137 Override_Mouse_Shape and mouse.cpp:90
   Revert_Mouse_Shape, which is how 1995 pins the plain arrow over a modal dialog
   (conquer.cpp:216-220 brackets Options.Process() with the pair). It has to live HERE and
   not as an `if` at the caller, because the map's cursor logic is reached from the frame
   loop AND from the script verbs (cursor, cursorcell, cursorobj), so a guard on the frame
   loop alone fixes the game and leaves the gate reading the wrong thing.
   ONE OWNER ONLY -- the pause dialog. It deliberately does not nest: if the F5 panel or
   the verdict screen ever wants an override too, give it its own mechanism rather than
   turning this into a stack, or a missed unlock leaves the arrow stuck for the rest of
   the mission. */
static bool   g_curLocked = false;
static int    g_curPrevType = MOUSE_NORMAL;  /* what to revert to */

/* One DOS system-timer tick, mouse.cpp's animation unit. */
#define CUR_TICK_MS (1000.0 / 60.0)

static bool cur_init(const DB_Pack* pack, char* err, int errlen)
{
    g_curShape = pack ? db_shape(pack, "MOUSE") : NULL;
    if (!g_curShape || g_curShape->w != 30 || g_curShape->h != 24 ||
        g_curShape->frames < 178) {
        if (err)
            snprintf(err, (size_t)errlen,
                     "dossidebar.pack has no usable MOUSE shape (rebake with "
                     "bake_dossidebar.py from next/cursors/)");
        g_curShape = NULL;
        return false;
    }
    g_curPal8 = pack->pal8;
    return true;
}

/* Undo cur_init. g_curShape and g_curPal8 point INTO the sidebar's DB_Pack, which
   sb_free is about to release, so they have to be dropped here first or the next
   frame drawn would read a freed pack. */
static void cur_free(void)
{
    if (g_curTex)
        glDeleteTextures(1, &g_curTex);
    g_curTex = 0;
    g_curShape = NULL;
    g_curPal8 = NULL;
    g_curType = MOUSE_NORMAL;
    g_curFrame = 0;
    g_curUploaded = -1;
    g_curStepMs = 0.0;
    /* The mission is over; any override it left up goes with it, or the next mission
       starts with a pointer nothing can change. */
    g_curLocked = false;
    g_curPrevType = MOUSE_NORMAL;
}

static const char* cur_name(int t)
{
    return (t >= 0 && t < MOUSE_TYPE_COUNT) ? CUR_CONTROL[t].name : "?";
}

static int cur_type(void) { return g_curType; }
static int cur_anim_frame(void) { return g_curFrame; }

/* The assignment itself, which the override uses and the map's cursor logic does not. */
static void cur_set_now(int mousetype)
{
    if (mousetype < 0 || mousetype >= MOUSE_TYPE_COUNT) mousetype = MOUSE_NORMAL;
    if (mousetype == g_curType) return;
    g_curType = mousetype;
    /* Override_Mouse_Shape: a shape change resets Frame to 0 and re-arms the timer. */
    g_curFrame = 0;
    g_curStepMs = -1.0;      /* re-arm from the next cur_draw's clock */
}

static void cur_set(int mousetype)
{
    if (g_curLocked) return;             /* an override is up; the map does not get a say */
    cur_set_now(mousetype);
}

/* Override_Mouse_Shape(shape, false): show this shape and hold it until reverted. */
static void cur_lock(int mousetype)
{
    if (!g_curLocked) {
        g_curPrevType = g_curType;
        g_curLocked = true;
    }
    cur_set_now(mousetype);
}

/* Revert_Mouse_Shape: back to whatever was showing when the override went up. The next
   update_cursor would re-pick it anyway in the live loop, but restoring it here is what
   1995 does and it is also what lets a script assert the pointer immediately after the
   dialog closes, without a frame having been drawn. */
static void cur_unlock(void)
{
    if (!g_curLocked) return;
    g_curLocked = false;
    cur_set_now(g_curPrevType);
}

/* Advance the animation the way MouseClass::AI does: when the cursor animates
   (FrameRate != 0), Frame steps by one each time FrameRate 60ths of a second pass. */
static void cur_animate(double now_ms)
{
    const CurControl* c = &CUR_CONTROL[g_curType];
    if (!c->rate || c->count <= 1) return;
    if (g_curStepMs < 0.0) { g_curStepMs = now_ms; return; }
    const double period = (double)c->rate * CUR_TICK_MS;
    while (now_ms - g_curStepMs >= period) {
        g_curFrame = (g_curFrame + 1) % c->count;
        g_curStepMs += period;
    }
}

/* Draw the cursor at screen pixel (col, row), hotspot applied, magnified `scale`
   times (the DOS sidebar's whole-number scale, so 1995 pixels stay square and all
   the same size). Call inside begin_overlay()'s pixel-space ortho. */
/* `scale` is a FLOAT, not an int. It has to be: the sprite stands in for the 3D cursor
   when the pointer leaves the map, and the 3D cursor is a world-space mesh whose screen
   size varies continuously with camera distance. Snapping the stand-in to whole numbers
   would make it jump a visible step against the thing it is imitating. */
static void cur_draw(float col, float row, float scale, double now_ms)
{
    if (!g_curShape || scale <= 0.0f) return;
    cur_animate(now_ms);

    const CurControl* c = &CUR_CONTROL[g_curType];
    const int absframe = c->start + g_curFrame;
    if (absframe >= g_curShape->frames) return;    /* cannot happen with the real table */

    /* One-time texture; per-frame-change refill. 8-bit indices -> RGBA through the
       pack's own palette, index 0 transparent, exactly like db_surface_to_rgba's
       want_alpha path. */
    if (!g_curTex) {
        unsigned char zero[CUR_TEX_POT * CUR_TEX_POT * 4];
        memset(zero, 0, sizeof(zero));
        glGenTextures(1, &g_curTex);
        glBindTexture(GL_TEXTURE_2D, g_curTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        /* NOT registered: UI art is never bilinear. See fx_filter.h. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CUR_TEX_POT, CUR_TEX_POT, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, zero);
        g_curUploaded = -1;
    }
    glBindTexture(GL_TEXTURE_2D, g_curTex);
    if (g_curUploaded != absframe) {
        const int w = g_curShape->w, h = g_curShape->h;
        const unsigned char* src = g_curShape->pixels + (size_t)absframe * w * h;
        unsigned char rgba[30 * 24 * 4];
        for (int i = 0; i < w * h; i++) {
            const unsigned char v = src[i];
            rgba[i * 4 + 0] = g_curPal8[v * 3 + 0];
            rgba[i * 4 + 1] = g_curPal8[v * 3 + 1];
            rgba[i * 4 + 2] = g_curPal8[v * 3 + 2];
            rgba[i * 4 + 3] = v ? 255 : 0;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        g_curUploaded = absframe;
    }

    const float x0 = col - (float)c->hx * scale;
    const float y0 = row - (float)c->hy * scale;
    const float x1 = x0 + (float)g_curShape->w * scale;
    const float y1 = y0 + (float)g_curShape->h * scale;
    const float u1 = (float)g_curShape->w / (float)CUR_TEX_POT;
    const float v1 = (float)g_curShape->h / (float)CUR_TEX_POT;

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
    glTexCoord2f(u1,   0.0f); glVertex2f(x1, y0);
    glTexCoord2f(u1,   v1);   glVertex2f(x1, y1);
    glTexCoord2f(0.0f, v1);   glVertex2f(x0, y1);
    glEnd();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
}

#endif /* CNC_CURSORS_H */
