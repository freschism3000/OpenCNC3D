/*
 * dosops.h -- the SPECIAL OPS mission list, drawn in the 1995 MS-DOS idiom.
 *
 * "SPECIAL OPS" is the cartridge's own word for these missions: the string lives in
 * the ROM at 0x22384C, beside SPECIAL OPS 2 and the MISSION X1/X2 pair, and the
 * scenario table at 0x20EB80 lists SCG30EA, SCB21EA and SCB22EB outside the campaign
 * run. The PC Covert Operations disc supplies the rest and their names.
 *
 * Same contract as dosmenu.h, and for the same reason: this module RENDERS and
 * HIT-TESTS a 320x200 8-bit surface with the db_* primitives and nothing else. It owns
 * no input, no clock, no file I/O and no GL. The caller builds the mission list (it is
 * the one that can read a directory) and hands it in; dosmenu_shell.c owns the loop.
 *
 * The two box styles come from dosmenu.h rather than being copied, so the list screen
 * and the main menu cannot drift apart.
 */

#ifndef DOSOPS_H
#define DOSOPS_H

#include "dosmenu.h"

/* One row. `name` may be NULL, in which case the row shows the scenario code alone --
 * which is the honest answer for the cartridge-side missions, whose titles are not in
 * any string table we have found. Left open deliberately. */
typedef struct DO_Mission
{
    const char *scen; /* SCG22EA: the scenario the brain is started on */
    const char *name; /* "Blackout", from the mission INI's [Basic] Name=, or NULL */
    unsigned char nod; /* 0 = GDI, 1 = Nod. From the code's second letter. */
} DO_Mission;

/* Geometry. The dialog is the full-screen one dialog.cpp draws for a list, not the
 * menu's 152-wide plate: a mission list needs the width for names. */
#define DO_DLG_X 16
#define DO_DLG_Y 6
#define DO_DLG_W 288
#define DO_DLG_H 188

#define DO_LIST_X 24
#define DO_LIST_Y 26
#define DO_LIST_W 272
#define DO_LIST_H 130
#define DO_ROW_H 8
#define DO_ROWS (DO_LIST_H / DO_ROW_H) /* 16 */

#define DO_BTN_Y 168
#define DO_BTN_H 9
#define DO_PLAY_X 84
#define DO_PLAY_W 60
#define DO_CANCEL_X 176
#define DO_CANCEL_W 60

/* Hit ids. A non-negative value is an index into the mission list. */
#define DO_HIT_NONE (-1)
#define DO_HIT_PLAY (-2)
#define DO_HIT_CANCEL (-3)

typedef struct DO_State
{
    const DO_Mission *list;
    int count;
    int selected; /* index into list, or -1 when the list is empty */
    int top;      /* index of the first visible row                */
    int pressed;  /* a DO_HIT_* id held under the mouse, or DO_HIT_NONE */
} DO_State;

void do_state_init(DO_State *st, const DO_Mission *list, int count);

/* Move the selection by delta rows, scrolling `top` to keep it visible. */
void do_move(DO_State *st, int delta);

/* Scroll without moving the selection (the wheel). */
void do_scroll(DO_State *st, int delta);

/* What is under (mx,my) in 320x200 menu pixels: a mission index, or a DO_HIT_*. */
int do_hit_test(const DO_State *st, int mx, int my);

/* The whole screen, over whatever the caller left in the surface. */
void do_draw(DB_Surface *s, const DB_Pack *p, const DO_State *st);

#endif /* DOSOPS_H */
