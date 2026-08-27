/*
 * t1_hud.h -- the 640x480 HUD on the 3dfx Voodoo 2.
 *
 * game/hud640.c is a lift-and-drop C89 module that composes the sidebar into a 32-bit
 * RGBA buffer and knows nothing about how that buffer reaches a screen. Its own header
 * says the Win98 port "only has to hand it memory". This file is that hand: it owns the
 * memory, converts the composed pixels to the card's 16-bit format, gets them into
 * texture memory and puts them on screen as quads. hud640.c is compiled UNEDITED, the
 * same way game/dosbar.c is, which is the point: this is the project's HUD, not a Win98
 * imitation of it.
 *
 * THREE THINGS ABOUT THIS CARD SHAPE THE FILE.
 *
 * 1. A Voodoo 2 has no 2D output at all, so the HUD is not a blit. It is geometry, like
 *    the terrain, and it has to live in texture memory to be drawn.
 *
 * 2. THE MAXIMUM TEXTURE IS 256x256 AND THE BAR IS 160x480. So the bar is cut in half at
 *    row 256 and lives in two pages, drawn as two quads that meet on an exact texel
 *    boundary. A third small page carries the two tab plates, the mouse pointer and a
 *    block of solid white for lines.
 *
 * 3. THE ART IS TRUE COLOUR, WHICH NOTHING ELSE ON THIS BRANCH IS. Everything from the
 *    cartridge and from the 1995 CD is palettised and uploads as GR_TEXFMT_P_8. This is
 *    new art with thousands of colours, so it uploads as GR_TEXFMT_RGB_565 through
 *    SR_Texture.px16. Every chunk that is composited into the bar was checked and is
 *    fully opaque, so no alpha channel has to survive the conversion.
 *
 * RECOMPOSING IS NOT FREE and is therefore not done every frame. Composing the bar writes
 * 300 KB, converting it reads that and writes 150 KB more, and the upload sends 256 KB
 * over PCI. On a 534 MHz Pentium III with 119 MB/s of memory bandwidth that is several
 * milliseconds, which at 80 frames a second is most of the budget. So the bar is composed
 * only when something that changes its pixels has changed, and the pointer position is
 * one of those things because the controls light up under it.
 */

#ifndef T1_HUD_H
#define T1_HUD_H

#include "softras.h"
#include "t1_cam.h"
#include "t1_terrain.h"
#include "w98_brain.h"
#include "hud640.h"

/* What the pointer is over. Same verdicts, in the same order, as sb_hit_h6() in
 * game/cnc_sidebar.h, which is the reference implementation of this hit test. */
typedef enum
{
    T1H_NONE = 0,
    T1H_ITEM,          /* a build cell; column and slot are filled in */
    T1H_REPAIR,
    T1H_SELL,
    T1H_MAP,
    T1H_UP,            /* column filled in */
    T1H_DOWN,
    T1H_RADAR,
    T1H_OPTIONS        /* the tab plate at the top left of the screen */
} T1_HudHit;

/* The Tier 1 target is 640x480 exactly, so the bar is always the five authored rows.
 * hud640.h grows the row count on taller screens; there is no taller screen here, and
 * pretending otherwise would mean carrying 800 rows of buffer that can never be used. */
#define T1H_ROWS      H6_ROWS
#define T1H_SLOTS     (H6_COLUMNS * T1H_ROWS)

#define T1H_PAGE      256          /* the two bar pages */
#define T1H_SPLIT     256          /* the bar row the second page starts at */
#define T1H_AUX_H      64          /* the third page: tabs, pointer, white block */

/* Where the pointer art and the solid white block sit inside the aux page. The white
 * block is how every screen-space line is drawn on this backend: there is no untextured
 * path and there does not need to be one. */
/* THE POINTER IS THE GAME'S OWN ART NOW, not a wedge drawn by two loops: MOUSE.SHP
 * frame 0 out of dossidebar.pack, 30x24, which is exactly what the 1995 game put over its
 * sidebar (sidebar.cpp:2252). The slot grew from 9x14 to 30x24 to hold it, so the white
 * block moved to the far end of the page to make room. */
#define T1H_CUR_X       0
#define T1H_CUR_Y      34
#define T1H_CUR_W      30
#define T1H_CUR_H      24
#define T1H_DOT_X     240
#define T1H_DOT_Y      34
#define T1H_DOT_SZ      4

typedef struct
{
    H6_Pack      *pack;
    H6_State      st;
    int           ok;

    /* Composed, by hud640.c, in its own RGBA */
    unsigned char bar[H6_BAR_W * H6_BAR_H * 4];
    unsigned char tab[H6_BAR_W * H6_TAB_H * 4];
    unsigned char cameo[T1H_SLOTS][H6_CAMEO_W * H6_CAMEO_H * 4];
    unsigned char radar[H6_RADAR_W * H6_RADAR_H * 4];

    /* Converted, in the card's format */
    unsigned short pageA[T1H_PAGE * T1H_PAGE];
    unsigned short pageB[T1H_PAGE * T1H_PAGE];
    unsigned short pageC[T1H_PAGE * T1H_AUX_H];
    SR_Texture     texA, texB, texC;

    /* The minimap's raw material: one averaged colour per terrain cell, computed once at
     * load from the atlas the terrain is actually drawn with, so the radar and the ground
     * cannot disagree about what colour a cell is. */
    unsigned char  cellrgb[64 * 64 * 3];
    int            cellrgb_ok;

    /* Which buildable sits at the top of each column. The DOS bar shows four rows and
     * the new one shows five, so the two do NOT scroll in step and this cannot be read
     * off DB_State; the arrows below the cells drive it. */
    int            top[H6_COLUMNS];
    int            count[H6_COLUMNS];

    /* Where the minimap ended up inside its surface, so a click on it can be turned
     * back into a map cell without recomputing the centring. */
    int            rzoom, rox, roy;

    /* Recompose bookkeeping. See the note at the top of the file. */
    int            dirty;
    int            last_hit, last_col, last_slot, last_pressed;
} T1_Hud;

/* hud640.pack, and the three texture pages. 0 on failure with a reason in err. */
int  t1_hud_load(T1_Hud *h, const char *packpath, char *err, int errlen);
void t1_hud_free(T1_Hud *h);

/* Averages every terrain cell's tile down to one colour, for the minimap. Cheap, once. */
void t1_hud_prepare_radar(T1_Hud *h, const T1_Terrain *t);

/* Fill the HUD state from the engine. `surf8` is the DOS sidebar surface that
 * db_draw_sidebar has just drawn into, and `pal8` its palette: the cameos, their build
 * clocks, their hold pips and their darkening are lifted out of it and doubled to the
 * 64x48 the new art wants. That is the cameo path game/cnc_sidebar.h falls back to when
 * true-colour cameo art is absent, so it is the reference behaviour and not a shortcut
 * invented here. */
void t1_hud_set_state(T1_Hud *h, const W98_Sidebar *sb, const void *dospack);

/* Scroll one column by the arrows. Clamped so the last page cannot scroll past itself. */
void t1_hud_scroll(T1_Hud *h, int column, int delta);

/* Redraw the minimap into st.radar_rgba. Call at whatever rate suits; it is not free. */
void t1_hud_radar(T1_Hud *h, const T1_Terrain *terr, const W98_MapInfo *map,
                  const W98_Object *objs, int nobj,
                  const T1_Cam *cam, const T1_Screen *scr);

/* A click inside the radar surface, as a map cell. 0 if it landed off the map. */
int t1_hud_radar_to_cell(const T1_Hud *h, const W98_MapInfo *map, int mx, int my,
                         int *cx, int *cy);

/* Light the control under the pointer. mx,my are SCREEN pixels. Marks the HUD dirty if
 * the answer changed, which is what stops the bar being recomposed every frame. */
void t1_hud_hover(T1_Hud *h, int mx, int my, int pressed);

/* Recompose and re-upload, but only if something changed. Returns 1 if it did work. */
int  t1_hud_refresh(T1_Hud *h);

/* The quads: bar, both tab plates. Depth testing is the caller's business. */
void t1_hud_draw(T1_Hud *h);

/* The pointer, and one screen-space line, both off the aux page. */
void t1_hud_cursor(T1_Hud *h, int mx, int my);

/* Put the 1995 pointer into the aux page. `idx` is one MOUSE.SHP frame in 8-bit indices
 * and `pal8` the palette it belongs to; index 0 becomes the chroma key. Called once, after
 * the DOS pack is open. Without it the hand-drawn wedge stays, which is a legible
 * fallback rather than a hole. */
void t1_hud_set_pointer(T1_Hud *h, const unsigned char *idx, int w, int hgt,
                        const unsigned char *pal8);
void t1_hud_rect(T1_Hud *h, float x0, float y0, float x1, float y1);

/* Where a screen pixel lands. column and slot are -1 when they do not apply. */
T1_HudHit t1_hud_hit(int mx, int my, int *column, int *slot);

/* Screen geometry, so the caller does not retype it. */
#define T1H_BAR_X   H6_BAR_X                    /* 480 */
#define T1H_FIELD_W H6_BAR_X                    /* the playfield is what is left */

#endif /* T1_HUD_H */
