/*
 * t1_cursor.h -- the cartridge's 3D pointer, and the 2D one that belongs over the panel.
 *
 * the project owner: "Cursors are not the correct mesh. Make sure the 2D cursor shows when moving it
 * over the HUD." Both halves of that are here.
 *
 * THE CONSOLE'S POINTER IS NOT A SPRITE. It is one of fourteen 3D MODELS drawn in the
 * world, on the terrain, at the picked ground point, so it has a constant WORLD size and
 * shrinks with distance. All fourteen have been sitting in this build's mesh bank as type
 * codes CUR00..CUR0D, with CURCIRC and the seven flipbook variants CUR0AF0..3 /
 * CUR0BF0..2 beside them, and nothing drew any of them: Tier 1 drew a white arrow wedge
 * built by hand out of two loops.
 *
 * WHAT PICKS THE MODEL is the engine's own ActionType for the cell under the pointer --
 * "what would happen if you clicked here" -- and the handoff recorded getting that as the
 * blocker. It is not one: CNC3D_Probe_Object_At is already exported by the deployed DLL
 * and RETURNS the ActionType as its int return value. The PROBE| line it prints is a side
 * effect, not the channel, so there is no dump round trip.
 *
 * WHAT STAYS 2D is the pointer over the sidebar, and that is not a shortcut either: a
 * world-space model has no world position under an opaque panel. The 1995 game drew
 * MOUSE_NORMAL there (sidebar.cpp:2252) and the console ships no cursor art for a sidebar
 * it does not have, so the 2D pointer is the right answer rather than a fallback -- and
 * it is now the game's OWN MOUSE.SHP frame out of dossidebar.pack instead of a wedge.
 *
 * EVERY TABLE BELOW IS TRANSCRIBED, with its source, from game/cursors.h and
 * game/cursor3d_mod.h, which are C++ and OpenGL-bound and so cannot be compiled here the
 * way game/dosbar.c and game/hud640.c are. The parts that matter are pure tables and one
 * switch, and the citations come across with them so the two builds can be diffed.
 *
 * THE CLOCK IS THE ENGINE TICK. frame = (ticks * 4) mod frameCount, off the brain's own
 * 15 Hz counter and never a wall clock, so two scripted runs give identical pixels.
 */

#ifndef T1_CURSOR_H
#define T1_CURSOR_H

#include "t1_mesh.h"
#include "t1_terrain.h"
#include "t1_cam.h"
#include "w98_brain.h"

/* ---- MouseType, defines.h:1778. Only the ones a mouse state machine can reach. ----- */
enum {
    T1M_NORMAL = 0,
    T1M_NO_MOVE, T1M_CAN_MOVE, T1M_ENTER, T1M_DEPLOY, T1M_CAN_SELECT,
    T1M_CAN_ATTACK, T1M_SELL_BACK, T1M_SELL_UNIT, T1M_REPAIR,
    T1M_NO_REPAIR, T1M_NO_SELL_BACK, T1M_ION_CANNON, T1M_NUCLEAR_BOMB,
    T1M_AIR_STRIKE, T1M_DEMOLITIONS, T1M_AREA_GUARD,
    T1M_COUNT
};

/* ---- ActionType, defines.h:546 ---------------------------------------------------- */
enum {
    T1A_NONE = 0, T1A_MOVE, T1A_NOMOVE, T1A_ENTER, T1A_SELF, T1A_ATTACK, T1A_HARVEST,
    T1A_SELECT, T1A_TOGGLE_SELECT, T1A_CAPTURE, T1A_REPAIR, T1A_SELL, T1A_SELL_UNIT,
    T1A_NO_SELL, T1A_NO_REPAIR, T1A_SABOTAGE, T1A_ION, T1A_NUKE_BOMB, T1A_AIR_STRIKE,
    T1A_GUARD_AREA, T1A_TOGGLE_PRIMARY, T1A_NO_DEPLOY
};

typedef struct
{
    int  ready;                /* all fourteen models resolved */
    int  mesh[14];             /* CUR00..CUR0D */
    int  flip[14][8];          /* per-frame variants; count 0 = not a flipbook */
    int  flipn[14];
    int  circ;                 /* CURCIRC, or -1 */

    /* what the pointer currently is, and where */
    int   mouse;               /* T1M_* */
    int   onmap;               /* 0 = over the panel, draw the 2D pointer */
    float wx, wz;              /* the picked ground point, in cells */
    int   cellx, cellz;
    int   action;              /* the engine's last verdict, -1 for "not asked" */
    long  probes;              /* how many times the engine was actually asked */
    long  drawn;               /* frames the 3D cursor drew */
    int   state, frame, code;  /* for the report and for the script's assertions */
    int   pin;                 /* a forced state 0..19, or -1. TEST affordance */
    /* The probe cache. The engine's verdict cannot change unless one of these does, and
     * the export prints a line and fflushes to a Windows 98 disk on every call. */
    int   qx, qz, qhover, qsel;
    long  qtick;
} T1_Cursor;

/* Resolve the models. Reports through `rep` (which may be NULL) exactly once. */
void t1_cursor_init(T1_Cursor *c, const T1_MeshBank *b,
                    void (*rep)(const char *fmt, ...));

/* Once a frame, after the pointer position is known.
 *   in_panel   the pointer is over the sidebar, or a placement is in progress
 *   selected   how many of the player's objects are selected
 *   ctrl/alt   modifier state
 *   sell/repair the two latched sidebar modes
 *   shown      cell_shows(cx, cz, 0) for the hovered cell, or 1 when there is no fog
 *   hover_*    the object under the pointer, or NULL
 */
void t1_cursor_update(T1_Cursor *c, const T1_Terrain *t, const T1_Cam *cam,
                      const T1_Screen *scr, int mx, int my,
                      int in_panel, int selected, int ctrl, int alt,
                      int sell, int repair, int shown,
                      const W98_Object *hover, long ticks);

/* The world pass. Draws nothing when the pointer is over the panel. */
long t1_cursor_draw(T1_Cursor *c, T1_MeshBank *b, const T1_Terrain *t,
                    const T1_Cam *cam, const T1_Screen *scr, long ticks);

/* The name of the current mouse type, for the report and for scripted assertions. */
const char *t1_cursor_name(const T1_Cursor *c);

/* SCREEN PIXEL -> THE GROUND POINT UNDER IT, on the heightfield rather than on a plane.
 *
 * Everything that turns a click into a cell must use this and not t1_screen_to_plane,
 * which answers for ONE horizontal plane: over a hill the two disagree by cells, so the
 * cursor drew on one cell and the order went to another. That is what a unit driving in
 * the wrong direction looks like from the chair. */
void t1_cursor_ground(const T1_Terrain *t, const T1_Cam *cam, const T1_Screen *scr,
                      float col, float row, float *wx, float *wz);

#endif /* T1_CURSOR_H */
