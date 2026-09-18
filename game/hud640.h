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

#define H6_MAX_ASSETS 40
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

/* THE METER'S ONE SCALE. hud640_meter_segs is how many whole segments the channel holds;
   hud640_meter_y is the y of the TOP EDGE of the fill a 0..100 level produces.
   Anything that has to be read AGAINST the fill goes through hud640_meter_y and never
   through a fraction of H6_METER_H: the fill is quantised to whole H6_METER_PITCH
   segments tiled up from the channel floor, so it travels segs * H6_METER_PITCH, ten
   pixels short of the channel, and the two are not the same scale. */
int hud640_meter_segs(int rows);
int hud640_meter_y(int rows, int level);

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
    int power_drain_level;           /* 0..100 on the SAME scale as power_level: what the
                                        base is DRAWING. The reported defect is that the
                                        meter is output alone, so a base overdrawing its
                                        plants fills exactly like a base drawing nothing.
                                        0 marks nothing, which is what a caller that does
                                        not know its drain gets. */
    int credits;                     /* printed on the credits tab */
    /* Frame indices into the baked control strips. The order is a contract with
     * tools/sidebar_redesign/states.py, which authors the art:
     *     0 normal   1 hover   2 pressed   3 active
     * Frame 0 is never blitted - the chassis already carries the resting state. The
     * arrows have no frame 3; scrolling is momentary, so there is nothing to engage. */
    /* -1 IS NOT A FRAME, IT IS AN ABSENCE, and only Repair and Sell can carry it: a
     * commander who is out of the match owns nothing to repair or sell, and the art
     * has no disabled plate to draw instead. Map keeps its own, because watching the
     * rest of the match is the one thing a spectator is still doing. */
    int repair_frame, sell_frame, map_frame;
    int arrow_frame[4];
    /* The three window-pinned plates. options_frame existed and was never written --
       h6_fill_state memsets the struct and nothing set it, so the button could not light
       up even once it was hit-testable. */
    int options_frame, credits_frame, sidebar_frame;
    /* The DATABASE plate's frame. A fourth pinned plate, drawn only in Enhanced with the
       Enhanced HUD; see codex_mod.h. */
    int database_frame;
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

/* ---- THE UNIT CARD ------------------------------------------------------------------
 *  The selection readout in the bottom-left corner of the Enhanced HUD: the selected
 *  unit's cameo, name, health and damage, a row of smaller cameos for the rest of the
 *  selection, and the ten control-group tabs under it. Same contract as the bar: the
 *  caller fills H6_Card, this composes an RGBA rectangle, and how it reaches the screen
 *  is the caller's business, so a fixed-function backend only has to blit it.
 *
 *  Every piece of chrome is cut from the shipped HUD art by
 *  tools/sidebar_redesign/unitcard_art.py: the body is the radar bezel widened to 240,
 *  the tabs are slices of the blank tab plate, the lettering is GRAD6FNT at three sizes. The numbers below describe that art and hud640_card_ok checks
 *  them against the pack at load, so a re-cut body cannot silently move the window. */
#define H6_CARD_W        240
#define H6_CARD_BODY_H   140
#define H6_CARD_WIN_X    11        /* the dark window inside the bezel, body-relative */
#define H6_CARD_WIN_Y    13
#define H6_CARD_WIN_W    216
#define H6_CARD_WIN_H    100
#define H6_CARD_TAB_W    24
#define H6_CARD_TAB_H    26
#define H6_CARD_TABS     10
#define H6_CARD_H        (H6_CARD_BODY_H + H6_CARD_TAB_H)
/* the main cameo's well, window-relative: same 61x45 cell, same 64x48 box and same
   68x54 frame ring as a build slot, so it is the picture the player already knows */
#define H6_CARD_CELL_X   4
#define H6_CARD_CELL_Y   19
/* the smaller cameos: 32x24, half the cameo box, each in its own well */
#define H6_CARD_MINI_W   36
#define H6_CARD_MINI_H   28
#define H6_CARD_MINIS    6
#define H6_CARD_MINI_X   3
#define H6_CARD_MINI_Y   71
#define H6_CARD_MINI_PITCH 35
/* the name line across the top of the window, then the stats column right of the
   main cameo */
#define H6_CARD_NAME_X   4
#define H6_CARD_NAME_Y   3
#define H6_CARD_TEXT_X   74
#define H6_CARD_BAR_Y    26
#define H6_CARD_BAR_SEGS 8
#define H6_CARD_NUM_Y    44

typedef struct
{
    const unsigned char *rgba;       /* a 64x48 cameo box, or NULL for an empty well */
    int  frame;                      /* the well: 0 resting, 1 under the pointer */
} H6_CardMini;

typedef struct
{
    const unsigned char *cameo;      /* the main unit, 64x48 RGBA, or NULL */
    char name[40];                   /* printed on the top line; upper-cased here */
    char code[12];                   /* printed in the well when there is no cameo */
    int  str, maxstr;                /* health, engine numbers */
    int  health_color;               /* 0 green, 1 amber, 2 red: the caller's rule */
    int  damage;                     /* primary weapon damage, -1 unarmed */
    int  count;                      /* how many objects the selection holds */
    H6_CardMini mini[H6_CARD_MINIS]; /* the rest of the selection, in order */
    int  nmini;                      /* how many of those slots are live */
    int  overflow;                   /* > 0: the last slot prints +overflow instead */
    int  group_count[H6_CARD_TABS];  /* units in control group 1..9,0 */
    int  group_frame[H6_CARD_TABS];  /* 0 empty, 1 resting, 2 hover, 3 active */
} H6_Card;

/* Does this pack carry the card's chrome, at the sizes the layout above assumes?
   Prints once and answers 0 on an older pack, and the caller draws no card. */
int  hud640_card_ok(const H6_Pack *p);
/* Compose the card: H6_CARD_W * H6_CARD_H * 4 bytes, caller owned. */
void hud640_draw_card(unsigned char *rgba, const H6_Pack *p, const H6_Card *c);
/* Ink width of a string in one of the two baked fonts ("font_big" / "font_small"),
   proportional, for a caller laying text out against the same rule the card uses. */
int  hud640_text_width(const H6_Pack *p, const char *font, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* HUD640_H */
