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
 *           text number. DM_SPECIAL and the other items marked ours have no number.
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
    {"Special Ops", 0, 0},        /*  --  ours; the expandbtn slot. The Test Map is
                                         the last row of the list behind it. */
    {"User Maps", 0, 0},          /*  --  ours; the editor's own singleplayer maps */
    {"Start New Game", 0, 0},     /*  25  TXT_START_NEW_GAME; live since the campaign
                                         flow landed (side select -> briefing ->
                                         mission -> score -> map) */
    /* LIVE. It opens the slot dialog on the title plate and the shell boots the mission
       the chosen slot was saved in, then loads it. It was drawn disabled for as long as
       there was no save system behind it, because 1995 drew a disabled gadget in the
       disabled colourway and REFUSED the click (gadget.cpp:632 guards the whole
       Clicked_On dispatch with `if (!next_button->IsDisabled)`), and a live button
       that does nothing is worse than that. */
    {"Load Mission", 0, 0},       /*  53  TXT_LOAD_MISSION        */
    {"Skirmish", 0, 0},           /*  --  ours; TXT_MULTIPLAYER_GAME's slot */
    {"Multiplayer", 0, 0},        /* 210  TXT_MULTIPLAYER_GAME    */
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

int dm_state_item_disabled(const DM_State *st, int item)
{
    if (dm_item_disabled(item))
        return 1;
    if (st && st->special_missing && item == DM_SPECIAL)
        return 1;
    return (st && st->intro_missing && item == DM_INTRO) ? 1 : 0;
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
     * is not, i.e. the engine boots onto the first button that is actually there,
     * which is Special Ops, the sixth-item slot. */
    st->selected = dm_first_enabled();
    st->pressed = -1;
    st->version = "";
    st->copyright = DM_COPYRIGHT_DEFAULT;
}

int dm_item_rect(const DM_State *st, int item, int *x, int *y, int *w, int *h)
{
    if (item < 0 || item >= DM_ITEM_COUNT)
        return 0;

    if (st && st->red_alert) {
        /* The Win95 TITLE.PCX is 640x400. Keep the five retail choices and the
         * OpenCNC3D additions on one panel, with live hit boxes at native resolution. */
        int row;
        switch (item) {
        case DM_START:       row = 0; break;
        case DM_LOAD:        row = 1; break;
        case DM_SKIRMISH:    row = 2; break;
        case DM_MULTIPLAYER: row = 3; break;
        case DM_SPECIAL:     row = 4; break;
        case DM_USERMAPS:    row = 5; break;
        case DM_INTRO:       row = 6; break;
        case DM_VISUALS:     row = 7; break;
        case DM_EXIT:        row = 8; break;
        default:             return 0;
        }
        *x = 205;
        *y = 166 + row * 21;
        *w = 230;
        *h = 17;
        return 1;
    }

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
        if (dm_state_item_disabled(st, i))
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
    int idx, guard, x, y, w, h;

    if (delta == 0)
        return item;

    idx = item;
    for (guard = 0; guard < DM_ITEM_COUNT; guard++) {
        idx = (idx + delta) % DM_ITEM_COUNT;
        if (idx < 0)
            idx += DM_ITEM_COUNT;
        if (!dm_state_item_disabled(st, idx) && dm_item_rect(st, idx, &x, &y, &w, &h))
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
    disabled = dm_state_item_disabled(st, item);
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

/* Wrap text at measured font widths, preserving explicit paragraph breaks.
 * Return the first unconsumed character so counting and painting use the same
 * boundaries and every character in the legal notice is accounted for. */
static const char *dm_copyright_line(const DB_Font *f, const char *text,
                                     char *line, int capacity)
{
    int n = 0, cut;
    while (text[n] && text[n] != '\n' && n < capacity - 1) {
        line[n] = text[n];
        n++;
    }
    line[n] = '\0';
    while (n > 1 && db_string_width(f, line, DM_COPYRIGHT_XSPACING)
                      > DM_LEGAL_W - DM_COPYRIGHT_MARGIN) {
        for (cut = n - 1; cut > 0 && line[cut] != ' '; cut--)
            ;
        n = cut > 0 ? cut : n - 1;
        line[n] = '\0';
    }
    text += n;
    while (*text == ' ')
        text++;
    if (*text == '\n')
        text++;
    return text;
}

/* The actual menu font, at its native glyph resolution in a finer presentation
 * layer. Original title/chrome pixels are only duplicated with nearest sampling. */
void dm_draw_legal_rgba(unsigned char *rgba, int width, int height, int stride,
                        const DB_Pack *p, const char *text, int fade)
{
    unsigned char ink[DM_LEGAL_W * DM_LEGAL_H], fp[16];
    DB_Surface surface;
    const DB_Font *f = db_font(p, "GRAD6FNT");
    char line[256];
    const char *rest;
    int i, lines = 0, y, block, x, y0;
    if (!text || !*text || !f || width <= 0 || height <= 0)
        return;
    memset(ink, 0, sizeof ink);
    db_surface_init(&surface, DM_LEGAL_W, DM_LEGAL_H, ink);
    db_font_palette_grad(fp, DM_COPYRIGHT_COLOUR, DB_TBLACK);
    for (rest = text; *rest; lines++)
        rest = dm_copyright_line(f, rest, line, sizeof line);
    block = f->maxh + (lines - 1) * DM_COPYRIGHT_LINE_STEP;
    y = (DM_LEGAL_H - block) / 2;
    if (y < 0) y = 0;
    rest = text;
    while (*rest && y + f->maxh <= DM_LEGAL_H) {
        rest = dm_copyright_line(f, rest, line, sizeof line);
        x = (DM_LEGAL_W - db_string_width(f, line, DM_COPYRIGHT_XSPACING)) / 2;
        db_print(&surface, f, line, x < 0 ? 0 : x, y, fp, DM_COPYRIGHT_XSPACING);
        y += DM_COPYRIGHT_LINE_STEP;
    }
    if (fade < 0) fade = 0;
    if (fade > 256) fade = 256;
    y0 = height * DM_COPYRIGHT_Y / DM_SCREEN_H;
    for (y = y0; y < height; y++) for (x = 0; x < width; x++) {
        int sx = ((2*x+1)*DM_LEGAL_W)/(2*width);
        int sy = ((2*(y-y0)+1)*DM_LEGAL_H)/(2*(height-y0));
        int index = ink[sy*DM_LEGAL_W+sx];
        unsigned char *pixel;
        if (!index) continue;
        pixel = rgba + ((size_t)y*stride+x)*4;
        for (i = 0; i < 3; i++) pixel[i] = (p->pal8[index*3+i]*fade)>>8;
        pixel[3] = 255;
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

/* Red Alert init.cpp:Init_Color_Remaps reads PCOLOR_DIALOG_BLUE from row 10 of
 * PALETTE.CPS. In the original title palette its Background, Shadow, Highlight,
 * Corners, Color and Bright indices are 198, 199, 194, 197, 194 and 160. The
 * bright text core uses WHITE (15). Its Main_Menu draws the button list directly
 * over TITLE.CPS: the red panel and copyright are already painted in that image. */
static void dm_ra_button(DB_Surface *s, const DB_Font *font,
                         const DM_State *st, int item)
{
    unsigned char fp[16];
    const char *label = dm_item_label(item);
    int x, y, w, h, tx, i;
    int pressed = st->pressed == item;
    int on = st->selected == item;
    unsigned char shadow = pressed ? 194 : 199;
    unsigned char hilite = pressed ? 199 : 194;

    if (!dm_item_rect(st, item, &x, &y, &w, &h))
        return;
    if (item == DM_MULTIPLAYER)
        label = "Multiplayer Game";

    db_fill_rect(s, x, y, x + w - 1, y + h - 1, 198);
    db_line_h(s, x, x + w - 1, y + h - 1, shadow);
    db_line_v(s, x + w - 1, y, y + h - 1, shadow);
    db_line_h(s, x, x + w - 1, y, hilite);
    db_line_v(s, x, y, y + h - 1, hilite);
    db_put_pixel(s, x, y + h - 1, 197);
    db_put_pixel(s, x + w - 1, y, 197);

    for (i = 0; i < 16; i++)
        fp[i] = (i >= 4) ? (on || pressed ? DB_WHITE : 194) : DB_TBLACK;
    fp[1] = on || pressed ? 160 : 194;
    /* A button whose movie is not installed is never selected or pressed (the hit test
       and the walk step over it), and its label is printed in the panel's own shadow
       colour so it reads as unavailable. */
    if (dm_state_item_disabled(st, item))
        for (i = 1; i < 16; i++)
            if (fp[i] != DB_TBLACK)
                fp[i] = 199;
    tx = x + (w >> 1) - 1 - (db_string_width(font, label, DB_FONT6_XSPACING) >> 1);
    db_print(s, font, label, tx, y + (h - font->maxh) / 2, fp, DB_FONT6_XSPACING);
}

static void dm_draw_ra_version(DB_Surface *s, const DB_Font *font, const char *text)
{
    unsigned char fp[16];
    int x;
    if (!font || !text || !*text)
        return;
    db_font_palette(fp, 194, DB_TBLACK);
    x = 462 - db_string_width(font, text, DB_FONT6_XSPACING);
    db_print(s, font, text, x, 355, fp, DB_FONT6_XSPACING);
}

/* Red Alert's new-game choice is a WWMessageBox on the title page. The original
 * msgbox.cpp places two buttons 20 pixels inside the left and right edges and
 * puts the prompt 20 pixels below the top of its centred dialog. */
void dm_ra_side_rect(int side, int *x, int *y, int *w, int *h)
{
    *x = side ? 330 : 214;
    *y = 232;
    *w = 96;
    *h = 22;
}

int dm_ra_side_hit(int x, int y)
{
    int side;
    for (side = 0; side < 2; side++) {
        int bx, by, bw, bh;
        dm_ra_side_rect(side, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh)
            return side;
    }
    return -1;
}

void dm_draw_ra_side(DB_Surface *s, const DB_Pack *p, int selected, int pressed)
{
    const DB_Font *body = db_font(p, "6PTHI");
    const DB_Font *button = db_font(p, "GRAD6HI");
    unsigned char fp[16];
    int side, i;

    db_fill_rect(s, 188, 151, 451, 274, 198);
    db_line_h(s, 188, 451, 151, 194);
    db_line_v(s, 188, 151, 274, 194);
    db_line_h(s, 188, 451, 274, 199);
    db_line_v(s, 451, 151, 274, 199);
    db_line_h(s, 194, 445, 157, 197);
    db_line_v(s, 194, 157, 268, 197);
    db_line_h(s, 194, 445, 268, 197);
    db_line_v(s, 445, 157, 268, 197);

    if (body) {
        db_font_palette(fp, DB_WHITE, DB_TBLACK);
        db_print(s, body, "Choose your side.",
                 320 - db_string_width(body, "Choose your side.", 0) / 2,
                 188, fp, 0);
    }
    if (!button) return;
    for (side = 0; side < 2; side++) {
        const char *label = side ? "Soviet" : "Allies";
        int x, y, w, h;
        unsigned char light = pressed == side ? 199 : 194;
        unsigned char dark = pressed == side ? 194 : 199;
        dm_ra_side_rect(side, &x, &y, &w, &h);
        db_fill_rect(s, x, y, x+w-1, y+h-1, 198);
        db_line_h(s, x, x+w-1, y, light);
        db_line_v(s, x, y, y+h-1, light);
        db_line_h(s, x, x+w-1, y+h-1, dark);
        db_line_v(s, x+w-1, y, y+h-1, dark);
        for (i = 0; i < 16; i++)
            fp[i] = i >= 4 ? (selected == side ? DB_WHITE : 194) : DB_TBLACK;
        db_print(s, button, label,
                 x + (w - db_string_width(button, label, DB_FONT6_XSPACING)) / 2,
                 y + (h - button->maxh) / 2, fp, DB_FONT6_XSPACING);
    }
}

/* Red Alert's Fetch_Difficulty presents a three-stop slider when fine
 * difficulty is off. The campaign asks this before Choose_Side. */
int dm_ra_difficulty_hit(int x, int y)
{
    if (x >= 470 && x < 530 && y >= 244 && y < 268) return 3; /* OK */
    if (x >= 103 && x <= 537 && y >= 200 && y <= 240)
        return x < 215 ? 0 : x < 425 ? 1 : 2;
    return -1;
}

void dm_draw_ra_difficulty(DB_Surface *s, const DB_Pack *p, int selected, int pressed)
{
    const DB_Font *body = db_font(p, "6PTHI");
    const DB_Font *button = db_font(p, "GRAD6HI");
    unsigned char fp[16];
    int x;
    db_fill_rect(s, 70, 122, 569, 278, 198);
    db_line_h(s, 70, 569, 122, 194);
    db_line_v(s, 70, 122, 278, 194);
    db_line_h(s, 70, 569, 278, 199);
    db_line_v(s, 569, 122, 278, 199);
    db_line_h(s, 76, 563, 128, 197);
    db_line_v(s, 76, 128, 272, 197);
    db_line_h(s, 76, 563, 272, 197);
    db_line_v(s, 563, 128, 272, 197);
    if (body) {
        db_font_palette(fp, DB_WHITE, DB_TBLACK);
        db_print(s, body, "Please set the difficulty level.",
                 320 - db_string_width(body, "Please set the difficulty level.", 0) / 2,
                 150, fp, 0);
        db_print(s, body, "It will be used throughout this campaign.",
                 320 - db_string_width(body, "It will be used throughout this campaign.", 0) / 2,
                 169, fp, 0);
    }
    /* special.cpp: Fetch_Difficulty places a three-position slider at
     * (x+20, y+h-29), scaled by RESFACTOR=2 for the Win95 screen. */
    if (button) {
        db_font_palette_grad(fp, DB_WHITE, DB_TBLACK);
        db_print(s, button, "EASY", 110, 202, fp, DB_FONT6_XSPACING);
        db_print(s, button, "NORMAL",
                 320 - db_string_width(button, "NORMAL", DB_FONT6_XSPACING) / 2,
                 202, fp, DB_FONT6_XSPACING);
        db_print(s, button, "HARD",
                 530 - db_string_width(button, "HARD", DB_FONT6_XSPACING),
                 202, fp, DB_FONT6_XSPACING);
    }
    db_fill_rect(s, 110, 222, 529, 237, 199);
    db_line_h(s, 110, 529, 222, 198);
    db_line_h(s, 110, 529, 237, 194);
    db_line_v(s, 110, 222, 237, 198);
    db_line_v(s, 529, 222, 237, 194);
    db_fill_rect(s, 114, 227, 525, 233, 198);
    db_line_h(s, 114, 525, 227, 194);
    db_line_h(s, 114, 525, 233, 194);
    x = 110 + selected * 210;
    if (x > 516) x = 516;
    db_fill_rect(s, x, 219, x + 13, 240, 194);
    db_line_h(s, x, x + 13, 219, DB_WHITE);
    db_line_v(s, x, 219, 240, DB_WHITE);
    db_line_h(s, x, x + 13, 240, 199);
    db_line_v(s, x + 13, 219, 240, 199);
    db_fill_rect(s, 470, 244, 529, 267, 198);
    db_line_h(s, 470, 529, pressed == 3 ? 267 : 244, pressed == 3 ? 199 : 194);
    db_line_h(s, 470, 529, pressed == 3 ? 244 : 267, pressed == 3 ? 194 : 199);
    if (button) {
        db_font_palette_grad(fp, DB_WHITE, DB_TBLACK);
        db_print(s, button, "OK", 500 - db_string_width(button, "OK", DB_FONT6_XSPACING) / 2,
                 252, fp, DB_FONT6_XSPACING);
    }
}

void dm_draw_menu(DB_Surface *s, const DB_Pack *p, const DM_State *st)
{
    const DB_Shape *title = db_shape(p, st->red_alert ? "TITLEHI" : "TITLE");
    const DB_Shape *options = db_shape(p, "OPTIONS");
    const DB_Font *grad = db_font(p, st->red_alert ? "GRAD6HI" : "GRAD6FNT");
    const DB_Font *six = db_font(p, st->red_alert ? "6PTHI" : "6POINT");
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

    if (st->red_alert) {
        static const int ra_items[] = {
            DM_START, DM_LOAD, DM_SKIRMISH, DM_MULTIPLAYER, DM_SPECIAL,
            DM_USERMAPS, DM_INTRO, DM_VISUALS, DM_EXIT
        };
        if (grad)
            for (i = 0; i < (int)(sizeof ra_items / sizeof ra_items[0]); i++)
                dm_ra_button(s, grad, st, ra_items[i]);
        dm_draw_ra_version(s, six, st->version);
        return;
    }

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
