/*
 * lui.h -- the launcher's widgets, drawn with the game's own 1995 DOS primitives.
 *
 * There is no new look here and that is the point. Everything below composes
 * game/dosbar.c and menu/dosmenu.c: the same 8-bit 320x200 surface, the same
 * Draw_Box bevels out of dialog.cpp's ButtonColorsClassic table, the same 4bpp
 * font printer, the same green ramp out of TITLE.CPS's palette. The launcher is
 * therefore a C&C dialog rather than a program that resembles one, and the
 * greyed-out Editor button is the engine's real BOXSTYLE_GREEN_DIS_RAISED
 * (dm_green_button's disabled arm) rather than a dimmed green.
 *
 * What this file adds is only what a main menu never needed: a scrolling text
 * panel and a progress gauge.
 */

#ifndef LUI_H
#define LUI_H

#include "dosmenu.h" /* which includes dosbar.h: DB_Surface, DB_Pack, DB_Font */

/* ------------------------------------------------------------------------ *
 * Buttons.
 *
 * A table-driven button rather than a widget tree, for the same reason
 * dosmenu.c uses one: a new button is one row, and the geometry, hit test,
 * keyboard walk and draw all read the row instead of testing for an item.
 * ------------------------------------------------------------------------ */

typedef struct
{
    int x, y, w, h;
    const char *label;
    int disabled; /* GadgetClass::IsDisabled: drawn, never selectable */
} LUI_Button;

/* `hot` is the keyboard highlight (menus.cpp:755 Turn_On), `pressed` the mouse. */
void lui_button_draw(DB_Surface *s, const DB_Font *grad, const LUI_Button *b, int pressed,
                     int hot);
int lui_button_hit(const LUI_Button *b, int mx, int my);

/* ------------------------------------------------------------------------ *
 * An inset panel: the framed well a text block sits in.
 *
 * dialog.cpp:159-161, BOXSTYLE_GREEN_BORDER: a black fill with one green
 * rectangle inset a pixel, no bevel. It is the style the main menu's own dialog
 * is drawn in, so a panel inside that dialog reads as part of the same object
 * rather than as a widget stuck onto it.
 * ------------------------------------------------------------------------ */

void lui_panel(DB_Surface *s, int x, int y, int w, int h);

/* ------------------------------------------------------------------------ *
 * A wrapped, scrollable text block.
 *
 * Built once from a blob of text, then drawn as a window onto it. The changelog
 * is markdown, so the small amount of markdown it actually uses is recognised
 * and given a colour rather than printed as punctuation: `## ` is a version
 * heading, `### ` a category, `- ` a bullet. Anything else is body text. HTML
 * comments are dropped, which is how the `<!-- discord: ... -->` marker that
 * rides on every version heading stays off the screen.
 *
 * Nothing is clipped silently: a line too wide for the panel is word-wrapped,
 * and a word too long to wrap is broken rather than run off the edge.
 * ------------------------------------------------------------------------ */

#define LUI_LINE_HEAD 0 /* ## a version heading   */
#define LUI_LINE_CAT 1  /* ### a category heading */
#define LUI_LINE_BULLET 2
#define LUI_LINE_BODY 3
#define LUI_LINE_BLANK 4

typedef struct
{
    char *text;         /* owned */
    unsigned char kind; /* LUI_LINE_*  */
    unsigned char cont; /* a wrapped continuation, so a bullet is not re-marked */
} LUI_Line;

typedef struct
{
    LUI_Line *lines;
    int count, cap;
    int top;     /* first visible line          */
    int visible; /* rows the panel can show     */
} LUI_Text;

/* `width` is the pixel width available for the widest line. Returns 0 and leaves
 * an empty block if the text is NULL, which is a legitimate state: it is what a
 * launcher shows before it has read a changelog. */
int lui_text_build(LUI_Text *t, const DB_Font *f, const char *blob, int width);
void lui_text_free(LUI_Text *t);
void lui_text_scroll(LUI_Text *t, int delta);
void lui_text_draw(DB_Surface *s, const DB_Font *f, const LUI_Text *t, int x, int y, int w,
                   int h);

/* The scrollbar beside the panel. Drawn only when there is more text than fits,
 * because a full-height thumb that can never move is furniture, not information. */
void lui_scrollbar_draw(DB_Surface *s, const LUI_Text *t, int x, int y, int w, int h);

/* ------------------------------------------------------------------------ *
 * A progress gauge.
 *
 * The engine's own is GaugeClass (gauge.cpp): a BOXSTYLE_DOWN well with a filled
 * bar in it. `frac` is thousandths so a caller with byte counts does not have to
 * carry a float, and -1 means "working, length unknown", which draws a barber
 * pole rather than a lie about how far along it is.
 * ------------------------------------------------------------------------ */

void lui_gauge_draw(DB_Surface *s, int x, int y, int w, int h, int frac, int phase);

/* ------------------------------------------------------------------------ *
 * Text helpers.
 * ------------------------------------------------------------------------ */

void lui_print(DB_Surface *s, const DB_Font *f, const char *text, int x, int y,
               unsigned char colour);
void lui_print_centered(DB_Surface *s, const DB_Font *f, const char *text, int cx, int y,
                        unsigned char colour);
void lui_print_right(DB_Surface *s, const DB_Font *f, const char *text, int rx, int y,
                     unsigned char colour);
/* Over the title art rather than over a dialog: classes 2 and 3 become BLACK so
 * the letters keep an outline (menus.cpp:814 TPF_FULLSHADOW). */
void lui_print_shadowed(DB_Surface *s, const DB_Font *f, const char *text, int x, int y,
                        unsigned char colour);

#endif /* LUI_H */
