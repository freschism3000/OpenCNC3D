/* dosops.c -- see dosops.h. Renders and hit-tests; owns nothing else. */

#include "dosops.h"

#include <string.h>

/* ------------------------------------------------------------------------ *
 * State.
 * ------------------------------------------------------------------------ */

int do_row_playable(const DO_State *st, int index)
{
    if (!st || !st->list || index < 0 || index >= st->count)
        return 0;
    return st->list[index].header ? 0 : 1;
}

void do_state_init(DO_State *st, const DO_Mission *list, int count)
{
    int i;
    if (!st)
        return;
    memset(st, 0, sizeof *st);
    st->list = list;
    st->count = count;
    st->top = 0;
    st->pressed = DO_HIT_NONE;
    for (i = 0; i < count; i++)
        if (list[i].header)
            st->grouped = 1;
    /* The selection opens on the first MISSION, which under a heading is row 1. A
       heading can never be the selection: nothing could be played from it. */
    st->selected = -1;
    for (i = 0; i < count; i++)
        if (!list[i].header) { st->selected = i; break; }
}

static void do_show_selected(DO_State *st)
{
    if (st->selected < 0)
        return;
    if (st->selected < st->top)
        st->top = st->selected;
    if (st->selected >= st->top + DO_ROWS)
        st->top = st->selected - DO_ROWS + 1;
    if (st->top < 0)
        st->top = 0;
}

void do_move(DO_State *st, int delta)
{
    int want, dir;
    if (!st || st->count <= 0 || st->selected < 0 || delta == 0)
        return;
    want = st->selected + delta;
    /* No wrap. A list this long is walked, not cycled, and the 1995 ListClass
     * (list.cpp:329-352) clamps at both ends rather than wrapping. */
    if (want < 0)
        want = 0;
    if (want >= st->count)
        want = st->count - 1;
    /* A heading is not a stop: carry on in the direction of travel until a mission
       row is under the cursor, and if there is none that way (the list opens on a
       heading, say) stay put rather than land on something unplayable. */
    dir = delta > 0 ? 1 : -1;
    while (want >= 0 && want < st->count && st->list[want].header)
        want += dir;
    if (want < 0 || want >= st->count)
        return;
    st->selected = want;
    do_show_selected(st);
}

void do_move_home(DO_State *st)
{
    int i;
    if (!st || st->count <= 0)
        return;
    for (i = 0; i < st->count; i++)
        if (!st->list[i].header) { st->selected = i; break; }
    do_show_selected(st);
}

void do_move_end(DO_State *st)
{
    int i;
    if (!st || st->count <= 0)
        return;
    for (i = st->count - 1; i >= 0; i--)
        if (!st->list[i].header) { st->selected = i; break; }
    do_show_selected(st);
}

void do_scroll(DO_State *st, int delta)
{
    int last;
    if (!st || st->count <= 0)
        return;
    last = st->count - DO_ROWS;
    if (last < 0)
        last = 0;
    st->top += delta;
    if (st->top < 0)
        st->top = 0;
    if (st->top > last)
        st->top = last;
}

/* ------------------------------------------------------------------------ *
 * Hit test.
 * ------------------------------------------------------------------------ */

static int in_rect(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

int do_hit_test(const DO_State *st, int mx, int my)
{
    int row, ix;

    if (!st)
        return DO_HIT_NONE;
    if (in_rect(mx, my, DO_PLAY_X, DO_BTN_Y, DO_PLAY_W, DO_BTN_H))
        return DO_HIT_PLAY;
    if (in_rect(mx, my, DO_CANCEL_X, DO_BTN_Y, DO_CANCEL_W, DO_BTN_H))
        return DO_HIT_CANCEL;
    if (in_rect(mx, my, DO_LIST_X, DO_LIST_Y, DO_LIST_W, DO_LIST_H)) {
        row = (my - DO_LIST_Y) / DO_ROW_H;
        ix = st->top + row;
        /* A heading answers as nothing, the way a disabled gadget does. */
        if (ix >= 0 && ix < st->count && !st->list[ix].header)
            return ix;
    }
    return DO_HIT_NONE;
}

/* ------------------------------------------------------------------------ *
 * Draw.
 * ------------------------------------------------------------------------ */

static void do_draw_button(DB_Surface *s, const DB_Font *f, const char *label,
                           int x, int y, int w, int pressed)
{
    unsigned char fp[16];
    int tx;

    dm_green_button(s, x, y, w, DO_BTN_H, pressed, 0);
    db_font_palette_grad(fp, pressed ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM, DB_TBLACK);
    tx = x + (w >> 1) - 1 - (db_string_width(f, label, DB_FONT6_XSPACING) >> 1);
    db_print(s, f, label, tx, y + 1, fp, DB_FONT6_XSPACING);
}

void do_draw(DB_Surface *s, const DB_Pack *p, const DO_State *st)
{
    const DB_Font *grad;
    unsigned char fp[16], fpsel[16], fpdim[16];
    int i, row, y, tx, nx;

    if (!s || !p || !st)
        return;
    grad = db_font(p, "GRAD6FNT");
    if (!grad)
        return;

    db_font_palette_grad(fp, DM_TEXT_MEDIUM, DB_TBLACK);
    db_font_palette_grad(fpsel, DM_TEXT_BRIGHT, DB_TBLACK);
    db_font_palette_grad(fpdim, DM_TEXT_DISABLED, DB_TBLACK);

    /* The plate stays where it is; the list sits in its own dialog on top. */
    dm_green_dialog(s, DO_DLG_X, DO_DLG_Y, DO_DLG_W, DO_DLG_H);

    /* Title. The cartridge's own words, ROM 0x22384C. */
    tx = DO_DLG_X + (DO_DLG_W >> 1) - (db_string_width(grad, "SPECIAL OPS",
                                                       DB_FONT6_XSPACING) >> 1);
    db_print(s, grad, "SPECIAL OPS", tx, DO_DLG_Y + 8, fpsel, DB_FONT6_XSPACING);

    /* The list well: BOXSTYLE_GREEN_BORDER again, one size down. */
    dm_green_dialog(s, DO_LIST_X - 2, DO_LIST_Y - 2, DO_LIST_W + 4, DO_LIST_H + 4);

    for (row = 0; row < DO_ROWS; row++) {
        const DO_Mission *m;
        const unsigned char *pal;
        i = st->top + row;
        if (i < 0 || i >= st->count)
            break;
        m = &st->list[i];
        y = DO_LIST_Y + row * DO_ROW_H;

        /* A SECTION HEADING: bright, flush left, no fill, never the selection. The
           bright ramp is what the selected row prints in, so a heading reads as a
           label and not as a greyed-out mission. */
        if (m->header) {
            db_print(s, grad, m->name ? m->name : "", DO_LIST_X + 3, y, fpsel,
                     DB_FONT6_XSPACING);
            continue;
        }

        /* list.cpp:236-243 fills the selected line and prints it bright. */
        if (i == st->selected) {
            db_fill_rect(s, DO_LIST_X, y, DO_LIST_X + DO_LIST_W - 1, y + DO_ROW_H - 1,
                         DM_GREEN_BKGD);
            pal = fpsel;
        } else {
            pal = fp;
        }

        /* Under a heading the side is already said, so the row is indented instead
           of prefixed; a flat list (the user maps) keeps its GDI/NOD column. */
        if (st->grouped) {
            nx = DO_LIST_X + 14;
        } else {
            db_print(s, grad, m->nod ? "NOD" : "GDI", DO_LIST_X + 3, y, pal,
                     DB_FONT6_XSPACING);
            nx = DO_LIST_X + 28;
        }
        if (m->name && *m->name) {
            db_print(s, grad, m->name, nx, y, pal, DB_FONT6_XSPACING);
            /* The scenario code, right aligned: what the player types into a bug
             * report, and dim so it does not compete with the title. */
            tx = DO_LIST_X + DO_LIST_W - 3 - db_string_width(grad, m->scen,
                                                             DB_FONT6_XSPACING);
            db_print(s, grad, m->scen, tx, y, i == st->selected ? pal : fpdim,
                     DB_FONT6_XSPACING);
        } else {
            /* No title anywhere in the data, so the code IS the name and stands in the
             * title column at full strength rather than being printed twice. */
            db_print(s, grad, m->scen, nx, y, pal, DB_FONT6_XSPACING);
        }
    }

    if (st->count <= 0)
        db_print(s, grad, "NO SPECIAL OPS MISSIONS INSTALLED", DO_LIST_X + 3,
                 DO_LIST_Y + 2, fpdim, DB_FONT6_XSPACING);

    do_draw_button(s, grad, "Play", DO_PLAY_X, DO_BTN_Y, DO_PLAY_W,
                   st->pressed == DO_HIT_PLAY);
    do_draw_button(s, grad, "Cancel", DO_CANCEL_X, DO_BTN_Y, DO_CANCEL_W,
                   st->pressed == DO_HIT_CANCEL);
}
