/*
 * dosmenu.c -- the 1995 MS-DOS Command & Conquer main menu, drawn exactly the way
 * Main_Menu() (menus.cpp:455) drew it, into an 8-bit 320x200 surface.
 *
 * The drawing order below is the engine's, not a reconstruction:
 *
 *   menus.cpp:802  Load_Title_Screen(TitlePicture, ...)   TITLE.CPS, whole screen
 *   menus.cpp:809  Dialog_Box(85, 0, 152, 136)            BOXSTYLE_GREEN_BORDER
 *   menus.cpp:810  Draw_Caption(TXT_NONE, x, y, w)        OPTIONS.SHP filigree pair
 *   menus.cpp:814  Fancy_Text_Print(VersionText, ...)     6 point, right aligned
 *   menus.cpp:830  startbtn.Draw_All()                    the button list
 *
 * Nothing here is a lookalike: the two green box styles come from dialog.cpp's
 * ButtonColorsClassic table, the button colours from textbtn.cpp Draw_Text, and the
 * font palette from dialog.cpp Simple_Text_Print's gradient branch.
 */

#include "dosmenu.h"

#include <string.h>

/* ------------------------------------------------------------------------ *
 * The item table.
 *
 * One row per button, so a new item is one line here plus one line in the
 * DM_Item enum and nothing else: the geometry, the hit test, the keyboard walk
 * and the draw all read this table rather than testing for particular items.
 *
 * label     Verbatim from CONQUER.ENG in the 1995 LOCAL.MIX; bake_dosmenu.py
 *           prints the same strings out of the archive so these can be checked
 *           against the data rather than trusted. The comment is the conquer.h
 *           text number. DM_TESTMAP is ours and has no number.
 * disabled  GadgetClass::IsDisabled (gadget.h). Drawn, never selectable.
 * narrow    menus.cpp:483/489: Exit is the one button that is neither full width
 *           nor left aligned with the rest. A flag rather than an `item == DM_EXIT`
 *           test, so inserting items above Exit cannot break its geometry.
 * ------------------------------------------------------------------------ */

typedef struct
{
    const char *label;
    unsigned char disabled;
    unsigned char narrow;
} DM_ItemDef;

static const DM_ItemDef dm_items[DM_ITEM_COUNT] = {
    /* label                disabled narrow */
    {"Test Map", 0, 0},           /*  --  ours                    */
    {"Special Ops", 0, 0},        /*  --  ours; the expandbtn slot */
    {"User Maps", 0, 0},          /*  --  ours; the editor's own singleplayer maps */
    {"Start New Game", 0, 0},     /*  25  TXT_START_NEW_GAME; live since the campaign
                                         flow landed (side select -> briefing ->
                                         mission -> score -> map) */
    /* DISABLED, and drawn disabled, because there is no save system behind it. It used
       to be a fully live button that did nothing at all when clicked, which is worse
       than the DOS original: 1995 drew a disabled gadget in the disabled colourway and
       REFUSED the click (gadget.cpp:632 guards the whole Clicked_On dispatch with
       `if (!next_button->IsDisabled)`). Now the same happens here. It is drawn in the
       disabled palette, the keyboard walk steps over it, and the mouse cannot reach it. */
    {"Load Mission", 1, 0},       /*  53  TXT_LOAD_MISSION        */
    {"Skirmish", 0, 0},           /*  --  ours; TXT_MULTIPLAYER_GAME's slot */
    {"Multiplayer", 1, 0},        /* 210  TXT_MULTIPLAYER_GAME    */
    {"Intro & Sneak Peek", 0, 0}, /*  26  TXT_INTRO               */
    {"Visuals", 0, 0},            /*  --  ours                    */
    {"Exit Game", 0, 1}           /*  64  TXT_EXIT_GAME           */
};

const char *dm_item_label(int item)
{
    if (item < 0 || item >= DM_ITEM_COUNT)
        return "";
    return dm_items[item].label;
}

int dm_item_disabled(int item)
{
    if (item < 0 || item >= DM_ITEM_COUNT)
        return 1; /* an item that does not exist is certainly not selectable */
    return dm_items[item].disabled;
}

int dm_first_enabled(void)
{
    int i;
    for (i = 0; i < DM_ITEM_COUNT; i++)
        if (!dm_items[i].disabled)
            return i;
    return -1;
}

void dm_state_init(DM_State *st)
{
    memset(st, 0, sizeof(*st));
    /* menus.cpp:731-735: curbutton is 0 when the sixth item is present, 1 when it
     * is not, i.e. the engine boots onto the first button that is actually there.
     * Start New Game is disabled here, so the same rule lands on Test Map. */
    st->selected = dm_first_enabled();
    st->pressed = -1;
    st->version = "";
    st->copyright = DM_COPYRIGHT_DEFAULT;
}

int dm_item_rect(const DM_State *st, int item, int *x, int *y, int *w, int *h)
{
    (void)st;
    if (item < 0 || item >= DM_ITEM_COUNT)
        return 0;

    /* menus.cpp:548-667: starty walks down from 25 in steps of ystep. A disabled
     * item still has a rectangle and is still drawn (gadget.cpp:630 calls Draw_Me
     * before the IsDisabled guard at :632), so this returns one for every item. */
    *y = DM_BTN_TOP + item * DM_BTN_STEP;
    *h = DM_BTN_H;
    if (dm_items[item].narrow) {
        *x = DM_EXIT_X;
        *w = DM_EXIT_W;
    } else {
        *x = DM_BTN_X;
        *w = DM_BTN_W;
    }
    return 1;
}

/* gadget.cpp Clicked_On tests X <= mx < X+Width, Y <= my < Y+Height, but only for
 * gadgets that reach it: gadget.cpp:632 wraps the whole call in
 *     if (!next_button->IsDisabled) { ... Clicked_On(...) ... }
 * so a disabled button is invisible to the mouse even though it is still drawn. */
int dm_hit_test(const DM_State *st, int mx, int my)
{
    int i, x, y, w, h;
    for (i = 0; i < DM_ITEM_COUNT; i++) {
        if (dm_items[i].disabled)
            continue;
        if (!dm_item_rect(st, i, &x, &y, &w, &h))
            continue;
        if (mx >= x && mx < x + w && my >= y && my < y + h)
            return i;
    }
    return -1;
}

/* menus.cpp:902-944 KN_UP / KN_DOWN, wrapping at both ends of the list.
 *
 * The skip is the engine's own, not an invention. When buttons[0] is not available
 * the engine does not walk onto it: menus.cpp:912-914 wraps KN_UP at `curbutton < 1`
 * instead of `< 0`, and :930-935 wraps KN_DOWN back to 1 instead of 0. It clamps the
 * walk to the reachable subset. Here the reachable subset is the enabled items, so
 * the loop steps until it finds one rather than hardcoding an index. */
int dm_next_item(const DM_State *st, int item, int delta)
{
    int idx, guard;
    (void)st;

    if (delta == 0)
        return item;

    idx = item;
    for (guard = 0; guard < DM_ITEM_COUNT; guard++) {
        idx = (idx + delta) % DM_ITEM_COUNT;
        if (idx < 0)
            idx += DM_ITEM_COUNT;
        if (!dm_items[idx].disabled)
            return idx;
    }
    return item; /* every item disabled: the highlight stays put */
}

/* ------------------------------------------------------------------------ *
 * The two box styles the sidebar never needed.
 *
 * dialog.cpp:97-111 ButtonColorsClassic, {Filler, Shadow, Highlight, Corner}:
 *    6  BOXSTYLE_GREEN_DOWN    {CC_GREEN_BKGD, CC_LIGHT_GREEN, CC_GREEN_SHADOW, ...}
 *    7  BOXSTYLE_GREEN_RAISED  {CC_GREEN_BKGD, CC_GREEN_SHADOW, CC_LIGHT_GREEN, ...}
 *   11  BOXSTYLE_GREEN_BORDER  {BLACK,         CC_GREEN_BOX,    CC_GREEN_BOX,    BLACK}
 * The classic table is the only one that can apply: dialog.cpp:138 forces
 * useGoldStyle false whenever Get_Resolution_Factor() == 0, which is DOS.
 * ------------------------------------------------------------------------ */

void dm_green_button(DB_Surface *s, int x, int y, int w, int h, int pressed, int disabled)
{
    unsigned char filler, shadow, hilite, corner;

    if (disabled) {
        /* Row 9, BOXSTYLE_GREEN_DIS_RAISED: {DKGREY, BLACK, LTGREY, DKGREY}.
         * textbtn.cpp:298 chooses it, and it beats IsPressed: the engine tests
         * IsDisabled first (textbtn.cpp:297-305) so a disabled button has no
         * pressed state at all. Nothing here is a dimmed green. */
        filler = DM_DIS_FILL;
        shadow = DM_DIS_SHADOW;
        hilite = DM_DIS_HILITE;
        corner = DM_DIS_CORNERS;
    } else {
        filler = DM_GREEN_BKGD;
        shadow = pressed ? DM_LIGHT_GREEN : DM_GREEN_SHADOW;
        hilite = pressed ? DM_GREEN_SHADOW : DM_LIGHT_GREEN;
        corner = DM_GREEN_CORNERS;
    }

    w--; /* dialog.cpp:142-143 */
    h--;
    db_fill_rect(s, x, y, x + w, y + h, filler);
    /* dialog.cpp:163-172, the default arm of the switch. GREEN_DIS_RAISED is entry 9,
     * which is neither GREEN_BOX nor GREEN_BORDER, so it lands here too: the disabled
     * button keeps the identical raised bevel and changes only its four colours. */
    db_line_h(s, x, x + w, y + h, shadow);
    db_line_v(s, x + w, y, y + h, shadow);
    db_line_h(s, x, x + w, y, hilite);
    db_line_v(s, x, y, y + h, hilite);
    db_put_pixel(s, x, y + h, corner);
    db_put_pixel(s, x + w, y, corner);
}

/* dialog.cpp:64 Dialog_Box -> Draw_Box(BOXSTYLE_GREEN_BORDER, filled), and :159-161
 * draws that style as a single inset rectangle, not bevelled edges. */
void dm_green_dialog(DB_Surface *s, int x, int y, int w, int h)
{
    w--;
    h--;
    db_fill_rect(s, x, y, x + w, y + h, DB_BLACK);
    db_line_h(s, x + 1, x + w - 1, y + 1, DM_GREEN_BOX);
    db_line_h(s, x + 1, x + w - 1, y + h - 1, DM_GREEN_BOX);
    db_line_v(s, x + 1, y + 1, y + h - 1, DM_GREEN_BOX);
    db_line_v(s, x + w - 1, y + 1, y + h - 1, DM_GREEN_BOX);
}

/* ------------------------------------------------------------------------ *
 * Text.
 *
 * dialog.cpp:407-435: with TPF_6PT_GRAD | TPF_USE_GRAD_PAL the palette starts as
 * _textfontpal[CC_GREEN], then TPF_MEDIUM_COLOR / TPF_BRIGHT_COLOR overwrite entries
 * 4..15 with _textpalmedium[CC_GREEN] (41) or _textpalbright[CC_GREEN] (4), and
 * :546 then sets entry 1 to that same colour. TPF_NOSHADOW (:508-514) clears 2 and 3.
 * The gradient therefore collapses to one flat colour, which is what the DOS build
 * shows and what db_font_palette_grad already produces.
 * ------------------------------------------------------------------------ */

static void dm_draw_button(DB_Surface *s, const DB_Font *f, const DM_State *st, int item)
{
    unsigned char fp[16];
    const char *label;
    int x, y, w, h, tx, i, pressed, on, disabled;

    if (!dm_item_rect(st, item, &x, &y, &w, &h))
        return;

    label = dm_items[item].label;
    disabled = dm_items[item].disabled;
    pressed = (st->pressed == item);
    on = (st->selected == item); /* menus.cpp:755 buttons[curbutton]->Turn_On() */

    /* textbtn.cpp:296-317 Draw_Background, the TPF_6PT_GRAD arm. */
    dm_green_button(s, x, y, w, h, pressed, disabled);

    if (disabled) {
        /* textbtn.cpp:349-350 passes flags = 0, so Simple_Text_Print takes the
         * dialog.cpp:414-422 arm that skips _textfontpal and writes the raw colour
         * into entries 4..15, and the dialog.cpp:431-433 arm that leaves fore as
         * fontpalette[1], which is still `back`. Entries 0..3 end up transparent
         * and 4..15 flat CC_GREEN. Spelled out rather than routed through
         * db_font_palette_grad, because that helper puts `fore` in entry 1 and the
         * engine puts `back` there. GRAD6FNT never uses class 1 so the two look the
         * same on screen, but this file is meant to be readable against the source. */
        for (i = 0; i < 16; i++)
            fp[i] = (i >= 4) ? DM_TEXT_DISABLED : DB_TBLACK;
    } else {
        /* textbtn.cpp:352-356 Draw_Text: pressed or "on" prints bright, else medium. */
        db_font_palette_grad(fp, (pressed || on) ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM, DB_TBLACK);
    }

    /* textbtn.cpp:359 Fancy_Text_Print(text, X + (Width>>1) - 1, Y + 1, ..., TPF_CENTER)
     * and dialog.cpp:562 TPF_CENTER does x -= String_Pixel_Width(text) >> 1. */
    tx = x + (w >> 1) - 1 - (db_string_width(f, label, DB_FONT6_XSPACING) >> 1);
    db_print(s, f, label, tx, y + 1, fp, DB_FONT6_XSPACING);
}

/* menus.cpp:814-820: TPF_6POINT | TPF_FULLSHADOW | TPF_RIGHT, GREEN on TBLACK.
 * FULLSHADOW (dialog.cpp:536-539) puts BLACK in classes 2 and 3 so the text stays
 * readable over the title art; TPF_RIGHT (dialog.cpp:565) makes x the right edge. */
static void dm_draw_version(DB_Surface *s, const DB_Font *f, const char *text)
{
    unsigned char fp[16];
    int i, x, y;

    if (!text || !*text)
        return;

    for (i = 0; i < 16; i++)
        fp[i] = DB_TBLACK;
    fp[2] = DB_BLACK;
    fp[3] = DB_BLACK;
    fp[0] = DB_TBLACK;
    fp[1] = DB_GREEN;

    x = DM_DIALOG_X + DM_DIALOG_W - 5 * 2;
    y = DM_VERSION_Y;
    x -= db_string_width(f, text, DB_FONT6_XSPACING);
    db_print(s, f, text, x, y, fp, DB_FONT6_XSPACING);
}

/* The bottom line, printed rather than painted into the plate. Centred, and with the
 * same FULLSHADOW treatment the engine uses for text over picture: classes 2 and 3
 * become BLACK so the letters keep an outline wherever the art is not black. */
static void dm_draw_copyright(DB_Surface *s, const DB_Pack *p, const char *text)
{
    unsigned char fp[16];
    const DB_Font *f = db_font(p, "SCOREFNT");
    char line[128];
    const char *rest = text;
    int i, y = DM_COPYRIGHT_Y;

    if (!text || !*text || !f)
        return;

    /* SCOREFNT is a SHADED face: its glyphs use nibble classes 2, 4, 6, 8 and 14, not
     * the 1/2/3 that 6POINT uses. Mapping only class 1 prints nothing, which is the
     * trap here. The 1995 line is flat anyway, so every ink class resolves to the one
     * grey the art used and class 0 stays transparent. */
    for (i = 0; i < 16; i++)
        fp[i] = DM_COPYRIGHT_COLOUR;
    fp[0] = DB_TBLACK;

    /* Greedy word wrap, centred. The 1995 line was 41 characters and fitted on one
     * row; anything longer takes the last space that still fits and continues on the
     * next. Nothing is clipped silently.
     *
     * The wrapped block is then centred vertically in the blanked strip, so a credit
     * that fits on one row sits where the 1995 line sat rather than riding high. */
    {
        const char *scan = rest;
        int lines = 0;
        while (*scan) {
            int n = (int)strlen(scan), cut;
            if (n > (int)sizeof line - 1)
                n = (int)sizeof line - 1;
            memcpy(line, scan, (size_t)n);
            line[n] = '\0';
            while (db_string_width(f, line, DM_COPYRIGHT_XSPACING)
                   > DM_SCREEN_W - DM_COPYRIGHT_MARGIN) {
                for (cut = n - 1; cut > 0 && line[cut] != ' '; cut--)
                    ;
                if (cut <= 0)
                    cut = n - 1;
                if (cut <= 0)
                    break;
                n = cut;
                line[n] = '\0';
            }
            lines++;
            scan += n;
            while (*scan == ' ')
                scan++;
        }
        if (lines > 0) {
            int block = lines * f->maxh + (lines - 1) * (DM_COPYRIGHT_LINE_STEP - f->maxh);
            int room = DM_SCREEN_H - DM_COPYRIGHT_Y - 1;
            y = DM_COPYRIGHT_Y + (room - block) / 2;
            if (y < DM_COPYRIGHT_Y)
                y = DM_COPYRIGHT_Y;
        }
    }

    while (*rest && y + f->maxh <= DM_SCREEN_H) {
        int n = (int)strlen(rest), x, cut;

        if (n > (int)sizeof line - 1)
            n = (int)sizeof line - 1;
        memcpy(line, rest, (size_t)n);
        line[n] = '\0';

        while (db_string_width(f, line, DM_COPYRIGHT_XSPACING) > DM_SCREEN_W - DM_COPYRIGHT_MARGIN) {
            for (cut = n - 1; cut > 0 && line[cut] != ' '; cut--)
                ;
            if (cut <= 0) {
                cut = n - 1; /* one unbreakable word: hard break rather than clip */
                if (cut <= 0)
                    break;
            }
            n = cut;
            line[n] = '\0';
        }

        x = (DM_SCREEN_W - db_string_width(f, line, DM_COPYRIGHT_XSPACING)) / 2;
        if (x < 0)
            x = 0;
        db_print(s, f, line, x, y, fp, DM_COPYRIGHT_XSPACING);

        rest += n;
        while (*rest == ' ')
            rest++;
        y += DM_COPYRIGHT_LINE_STEP;
    }
}

/* ------------------------------------------------------------------------ */

/* menus.cpp:802 Load_Title_Screen, on its own, so a second screen (the Special Ops
 * list) can stand on the same plate rather than carry a copy of this. */
void dm_draw_plate(DB_Surface *s, const DB_Pack *p)
{
    const DB_Shape *title = db_shape(p, "TITLE");

    db_clip_reset(s);
    if (title)
        db_draw_shape(s, title, 0, 0, 0);
    else
        db_fill_rect(s, 0, 0, s->w - 1, s->h - 1, DB_TBLACK);
}

void dm_draw_menu(DB_Surface *s, const DB_Pack *p, const DM_State *st)
{
    const DB_Shape *title = db_shape(p, "TITLE");
    const DB_Shape *options = db_shape(p, "OPTIONS");
    const DB_Font *grad = db_font(p, "GRAD6FNT");
    const DB_Font *six = db_font(p, "6POINT");
    int i;

    db_clip_reset(s);

    /* menus.cpp:802 Load_Title_Screen. The CPS is a full 320x200 plate, so this both
     * clears the screen and sets the backdrop. Index 0 is transparent to the shape
     * blitter, and index 0 is also what an untouched surface holds, so the black
     * regions of the title art come out black either way. */
    if (title)
        db_draw_shape(s, title, 0, 0, 0);
    else
        db_fill_rect(s, 0, 0, s->w - 1, s->h - 1, DB_TBLACK);

    /* menus.cpp:809 */
    dm_green_dialog(s, DM_DIALOG_X, DM_DIALOG_Y, DM_DIALOG_W, DM_DIALOG_H);

    /* menus.cpp:810 -> goptions.cpp:570-571, SHAPE_CENTER at both top corners. */
    if (options) {
        db_draw_shape_centered(s, options, DM_FILIGREE_LEFT, DM_DIALOG_X + 12, DM_DIALOG_Y + 11);
        db_draw_shape_centered(s, options, DM_FILIGREE_RIGHT, DM_DIALOG_X + DM_DIALOG_W - 14,
                               DM_DIALOG_Y + 11);
    }

    /* menus.cpp:812-820 */
    if (six) {
        dm_draw_version(s, six, st->version);
        dm_draw_copyright(s, p, st->copyright);
    }

    /* menus.cpp:830 startbtn.Draw_All() walks the list in creation order. */
    if (grad)
        for (i = 0; i < DM_ITEM_COUNT; i++)
            dm_draw_button(s, grad, st, i);
}

void dm_draw_cursor(DB_Surface *s, const DB_Pack *p, int mx, int my)
{
    /* mouse.h MouseType: MOUSE_NORMAL is frame 0 and its hotspot is the top left
     * pixel, so the shape is drawn at the pointer position unshifted. */
    const DB_Shape *mouse = db_shape(p, "MOUSE");
    if (mouse)
        db_draw_shape(s, mouse, 0, mx, my);
}
