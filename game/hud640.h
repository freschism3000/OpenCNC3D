/* ------------------------------------------------------------------------------------
 * hud640.h -- the 640x480 sidebar HUD, as a lift-and-drop C module.
 *
 * Same contract as dosbar.c: plain C89, no GL, no SDL, no allocation past the load.
 * It rasterises into a caller-owned 32-bit RGBA buffer and knows nothing about how that
 * buffer reaches the screen, so the Win98 / Glide port only has to hand it memory.
 *
 * WHY 32-bit and not 8-bit like dosbar.c: the DOS sidebar is only correct in 8-bit,
 * because index 0 is transparent and the art was authored against a fixed 256-entry
 * palette. This HUD is new art at the Tier 1 target (640x480, 16-bit), so it carries
 * thousands of colours and has a real alpha channel. The existing path already uploads
 * RGBA8 to the GPU (cnc_sidebar.h step 2/3), so nothing downstream has to change.
 *
 * Every coordinate comes from hud640_layout.h, which is GENERATED from measurements of
 * the reference art. Nothing here is a hand-typed position.
 * ---------------------------------------------------------------------------------- */
#ifndef HUD640_H
#define HUD640_H

#include "hud640_layout.h"

/* Compiled as C89 but included from C++ (cnc_sidebar.h), same as dosbar.h. */
#ifdef __cplusplus
extern "C" {
#endif

#define H6_MAX_ASSETS 32
#define H6_NAME_LEN   16

/* ------------------------------------------------------------------------------------
 *  MORE ROWS ON A TALLER SCREEN.
 *
 *  The bar is authored 160x480, which is the Tier 1 target exactly. On any screen taller
 *  than a whole multiple of 480 the old rule picked the largest whole-number zoom and
 *  CENTRED what was left, so a 1692-row display drew the bar at 3x = 1440 and left 252
 *  rows of dead black above and below it. reported, first fullscreen run: the sidebar should
 *  grow another row of cameos instead.
 *
 *  HOW, without new art. The chassis already carries every empty well, and the wells sit
 *  on a REGULAR pitch from row 3 down: 353, 401, and the arrows at 453. So one 48-pixel
 *  band of the chassis -- H6_BAND_Y..H6_BAND_Y+H6_ROW_PITCH, which is exactly one row of
 *  wells plus the frame and power channel beside them -- can be repeated to make as many
 *  extra rows as the screen has room for, and everything below H6_SPLIT_Y slides down.
 *
 *  WHAT THAT COSTS, said plainly rather than left to be discovered: the extra rows are a
 *  COPY of row 3's wells. The five delivered
 *  rows are not pixel-identical to each other either, so a screen showing seven rows shows one
 *  authored set and two copies. It is recorded as a known gap. The clean fix is
 *  taller delivered art, which is the to supply; this is what code alone can do.
 *
 *  THESE FOUR NUMBERS ARE DERIVED FROM THE GENERATED TABLE, NOT RE-MEASURED, and
 *  hud640_check_layout() asserts them against it at load. hud640_layout.h says "do not
 *  edit: re-run the baker", so they cannot live there; a silent drift between a baked
 *  H6_ROW_Y and a literal here would put the extra wells half a row out. */
#define H6_ROW_PITCH  48        /* H6_ROW_Y[4] - H6_ROW_Y[3] */
#define H6_SPLIT_Y   401        /* H6_ROW_Y[4]: where copies are inserted        */
#define H6_BAND_Y    353        /* H6_ROW_Y[3]: the band that gets copied        */
#define H6_MAX_ROWS   12        /* a cap, so the buffers can be a fixed size     */
#define H6_MAX_BAR_H  (H6_BAR_H + (H6_MAX_ROWS - H6_ROWS) * H6_ROW_PITCH)

/* The y of a build row in the EXTENDED bar. Rows 0..3 keep their measured positions;
   everything from row 4 down sits on the regular pitch, which is what makes row 4 come
   out at H6_ROW_Y[4] and the arithmetic agree with the five-row case for free. */
int hud640_row_y(int row);

/* Bar height, arrow row and meter height for a given row count. */
int hud640_bar_h(int rows);
int hud640_arrow_y(int rows);
int hud640_meter_h(int rows);

/* How many rows fit in `avail` bar-space pixels (screen height divided by the zoom),
   clamped to H6_ROWS..H6_MAX_ROWS. Never fewer than the five that were authored. */
int hud640_rows_for(int avail);

/* Assert the four derived constants above against the generated table. Prints and
   returns 0 on disagreement; the caller then stays at H6_ROWS. */
int hud640_check_layout(void);

/* ---- THE CAMEO GOES INSIDE THE FRAME ---------------------------------------------
 *  The cameo box is 64x48 against a 61x45 well, so it OVERHANGS by a pixel or two on
 *  every side and buries the frame's inner bevel and its chamfered corners under the
 *  picture. That overhang was deliberate -- the layout comment explains it, C&C95
 *  cameos are 64x48 with the caption running edge to edge and cropping them to the well
 *  took the first and last letter of every name -- but the result is a cameo sitting ON
 *  the frame rather than in it, which is what was changed.
 *
 *  THE FIX USES ART THAT WAS ALREADY BAKED AND NEVER DRAWN. `cell_frame` is in the pack:
 *  68x54 RGBA, an opaque ring with its centre punched out, which is exactly the well's
 *  border and chamfers with a hole where the picture goes. Blit it AFTER the cameo and
 *  the frame is back on top, at its authored thickness, with no inset guessed at and no
 *  pixel of the cameo scaled or cropped. The caption survives; the frame simply overlaps
 *  it the way the surrounding cells always did.
 *
 *  The offset is MEASURED, not chosen: cell_frame was correlated against the chassis's
 *  own baked frames over a 16x16 search and (-3,-4) is where it lands. See
 *  tools/sidebar_redesign/chunks/cell_frame.png. */
/* The A/B, so gate G41 can prove itself: --nocellframe skips the overlay and the
   cameo goes back to sitting on the bevel. Same reason --noshade and --nobilfix
   exist. */
extern int hud640_cell_frame_on;
/* THE 45-DEGREE CORNER CUT. The frame ring closed the four straight edges but not the
 * corners, because cell_frame's own opening is a plain rectangle (measured: every row of
 * it is transparent from x=4 to x=63) while the WELL in the chassis has its corners cut
 * at 45 degrees. So the cameo still filled four little triangles the well does not have,
 * and they were visible.
 *
 * The cut is measured off the chassis, not chosen. Walking the dark bevel line inward
 * from a well's top-left corner it steps one pixel left per pixel down -- (dy=1,dx=6),
 * (2,5), (3,4), (4,3), (5,2), (6,1) -- so the interior is exactly where dx+dy >= 7, in
 * cell-relative pixels, at all four corners. That is also the "~7px" the sidebar notes
 * recorded from the reference art before any of this was built. */
#define H6_CHAMFER   7

#define H6_FRAME_DX  (-3)
#define H6_FRAME_DY  (-4)

typedef struct
{
    char name[H6_NAME_LEN + 1];
    int  w, h, frames;               /* w,h are ONE frame; frames lie left to right */
    const unsigned char *rgba;       /* frames * w * h * 4, straight out of the pack */
} H6_Asset;

typedef struct
{
    unsigned char *blob;
    long           blobsize;
    H6_Asset       assets[H6_MAX_ASSETS];
    int            nassets;
} H6_Pack;

/* One slot in the build column. */
typedef struct
{
    int  used;                       /* 0 = empty; the chassis already shows the well */
    int  progress;                   /* 0..100, -1 when not building */
    /* H6_CAMEO_W * H6_CAMEO_H RGBA, or NULL. The caller converts and scales, because the
     * cameo source is the pack's 8-bit art and this module must not know about it.
     * NOTE this is the CAMEO box (64x48), not the well (61x45) - see hud640_layout.h. */
    const unsigned char *rgba;
} H6_Slot;

/* Everything the HUD needs to draw one frame. The caller fills this; the module never
 * reaches into the engine, exactly as DB_State works for dosbar.c. */
typedef struct
{
    int power_level;                 /* 0..100, drives the meter fill */
    int power_color;                 /* 0 green (healthy), 1 yellow, 2 red.
                                        1995's watt rule: yellow when drain exceeds
                                        output, red when it exceeds twice it. */
    int credits;                     /* printed on the credits tab */
    /* Frame indices into the baked control strips. The order is a contract with
     * tools/sidebar_redesign/states.py, which authors the art:
     *     0 normal   1 hover   2 pressed   3 active
     * Frame 0 is never blitted - the chassis already carries the resting state. The
     * arrows have no frame 3; scrolling is momentary, so there is nothing to engage. */
    int repair_frame, sell_frame, map_frame;
    int arrow_frame[4];
    /* The three window-pinned plates. options_frame existed and was never written --
       h6_fill_state memsets the struct and nothing set it, so the button could not light
       up even once it was hit-testable. */
    int options_frame, credits_frame, sidebar_frame;
    int radar_active;                /* 0 -> draw the faction emblem instead of a map */
    int nod;                         /* which emblem: 0 GDI, 1 Nod */
    /* Sized for the tallest bar we will ever compose. `rows` says how many of them
     * are live this frame; anything past it is ignored. */
    H6_Slot slot[H6_COLUMNS * H6_MAX_ROWS];
    int rows;                        /* H6_ROWS..H6_MAX_ROWS; 0 is read as H6_ROWS */
    /* The radar CONTENTS, if the caller has them: H6_RADAR_W * H6_RADAR_H RGBA, or NULL.
     * The caller is responsible for integer-zooming and centring the minimap; this
     * module will NOT resample it, because a non-integer scale destroys a cell grid. */
    const unsigned char *radar_rgba;
} H6_State;

H6_Pack *hud640_load(const char *path, char *err, int errlen);
void     hud640_free(H6_Pack *p);
const H6_Asset *hud640_asset(const H6_Pack *p, const char *name);

/* Compose the sidebar column: H6_BAR_W * hud640_bar_h(st->rows) * 4 bytes, caller
 * owned. Size the buffer for H6_MAX_BAR_H and it can never be too small. */
void hud640_draw_bar(unsigned char *rgba, const H6_Pack *p, const H6_State *st);

/* Compose one tab plate: H6_BAR_W * H6_TAB_H * 4. `label` is "OPTIONS" for the options
 * tab, or NULL to print `value` as digits (the credits readout). */
void hud640_draw_tab(unsigned char *rgba, const H6_Pack *p, const char *label,
                     int value, int frame);

/* Largest integer zoom whose result still fits the radar surface. Never scale by a
 * fraction: the minimap is a cell grid and a non-integer step aliases it. */
int hud640_radar_zoom(int map_w, int map_h);
/* Fractional variant: equal to the integer zoom while the map fits, below 1.0 when
   even one pixel per cell overflows the surface (128-tall maps). */
float hud640_radar_zoomf(int map_w, int map_h);

#ifdef __cplusplus
}
#endif

#endif /* HUD640_H */
