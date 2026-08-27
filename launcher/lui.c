/*
 * lui.c -- see lui.h. Everything here composes db_* and dm_* primitives; there is
 * no second implementation of a bevel, a font or a palette in this file.
 */

#include "lui.h"

#include <stdlib.h>
#include <string.h>

/* 6POINT's cell is 8 high and the engine prints it with FontYSpacing -1
 * (dosbar.h DB_FONT6_YSPACING), so consecutive rows sit 7 apart. */
#define LUI_LINE_H 7

/* dosbar.h's DB_TBLACK is index 0, which db_print treats as transparent. */
#define LUI_TRANSPARENT 0

/* ------------------------------------------------------------------------ *
 * Text helpers.
 * ------------------------------------------------------------------------ */

/* db_font_palette_grad rather than db_font_palette, and deliberately so: it fills
 * entry 1 AND entries 4..15 with the ink colour. 6POINT's glyphs use nibble class
 * 1, GRAD6FNT's use 4..15, so one palette prints either face correctly and a
 * caller cannot pick the wrong helper for the font it happens to be holding. */
static void lui_pal(unsigned char fp[16], unsigned char colour)
{
    db_font_palette_grad(fp, colour, LUI_TRANSPARENT);
}

void lui_print(DB_Surface *s, const DB_Font *f, const char *text, int x, int y,
               unsigned char colour)
{
    unsigned char fp[16];
    if (!f || !text || !*text)
        return;
    lui_pal(fp, colour);
    db_print(s, f, text, x, y, fp, DB_FONT6_XSPACING);
}

void lui_print_centered(DB_Surface *s, const DB_Font *f, const char *text, int cx, int y,
                        unsigned char colour)
{
    if (!f || !text || !*text)
        return;
    /* dialog.cpp:562 TPF_CENTER: x -= String_Pixel_Width(text) >> 1. */
    lui_print(s, f, text, cx - (db_string_width(f, text, DB_FONT6_XSPACING) >> 1), y, colour);
}

void lui_print_right(DB_Surface *s, const DB_Font *f, const char *text, int rx, int y,
                     unsigned char colour)
{
    if (!f || !text || !*text)
        return;
    /* dialog.cpp:565 TPF_RIGHT: x is the right edge. */
    lui_print(s, f, text, rx - db_string_width(f, text, DB_FONT6_XSPACING), y, colour);
}

void lui_print_shadowed(DB_Surface *s, const DB_Font *f, const char *text, int x, int y,
                        unsigned char colour)
{
    unsigned char fp[16];
    int i;
    if (!f || !text || !*text)
        return;
    /* menus.cpp:814 TPF_FULLSHADOW, dialog.cpp:536-539: classes 2 and 3 become
     * BLACK so the letters keep an outline wherever the art behind is not black. */
    for (i = 0; i < 16; i++)
        fp[i] = LUI_TRANSPARENT;
    for (i = 4; i < 16; i++)
        fp[i] = colour;
    fp[1] = colour;
    fp[2] = DB_BLACK;
    fp[3] = DB_BLACK;
    db_print(s, f, text, x, y, fp, DB_FONT6_XSPACING);
}

/* ------------------------------------------------------------------------ *
 * Buttons.
 * ------------------------------------------------------------------------ */

void lui_button_draw(DB_Surface *s, const DB_Font *grad, const LUI_Button *b, int pressed,
                     int hot)
{
    unsigned char fp[16];
    int i, tx;

    /* The disabled arm is the engine's, not a dimmed green: textbtn.cpp:297-305
     * tests IsDisabled first, so a disabled button has no pressed state at all
     * and leaves the green ramp entirely for BOXSTYLE_GREEN_DIS_RAISED's
     * {DKGREY, BLACK, LTGREY, DKGREY}. That is what the Editor button is. */
    dm_green_button(s, b->x, b->y, b->w, b->h, pressed && !b->disabled, b->disabled);

    if (!grad || !b->label)
        return;

    if (b->disabled) {
        /* textbtn.cpp:349-350 passes flags = 0. Walked through Simple_Text_Print
         * that leaves entries 0..3 transparent and 4..15 flat CC_GREEN; the same
         * spelling dosmenu.c uses, for the same reason. */
        for (i = 0; i < 16; i++)
            fp[i] = (i >= 4) ? DM_TEXT_DISABLED : LUI_TRANSPARENT;
    } else {
        db_font_palette_grad(fp, (pressed || hot) ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM,
                             LUI_TRANSPARENT);
    }

    /* textbtn.cpp:359 Fancy_Text_Print(text, X + (Width>>1) - 1, Y + 1, TPF_CENTER) */
    tx = b->x + (b->w >> 1) - 1 - (db_string_width(grad, b->label, DB_FONT6_XSPACING) >> 1);
    db_print(s, grad, b->label, tx, b->y + 1, fp, DB_FONT6_XSPACING);
}

int lui_button_hit(const LUI_Button *b, int mx, int my)
{
    /* gadget.cpp:632 guards the whole Clicked_On dispatch with !IsDisabled, so a
     * disabled button is not merely inert when clicked: the mouse cannot reach it. */
    if (b->disabled)
        return 0;
    return mx >= b->x && mx < b->x + b->w && my >= b->y && my < b->y + b->h;
}

/* ------------------------------------------------------------------------ *
 * The inset panel.
 *
 * dialog.cpp:159-161, BOXSTYLE_GREEN_BORDER: a black fill with a single green
 * rectangle inset one pixel, and no bevel. It is the same style the main menu's
 * own dialog is drawn in, which is why a panel inside that dialog reads as part
 * of the same object rather than as a widget stuck onto it.
 * ------------------------------------------------------------------------ */

void lui_panel(DB_Surface *s, int x, int y, int w, int h)
{
    dm_green_dialog(s, x, y, w, h);
}

/* ------------------------------------------------------------------------ *
 * The wrapped, scrollable text block.
 * ------------------------------------------------------------------------ */

static int lui_line_indent(unsigned char kind)
{
    return kind == LUI_LINE_BULLET ? 7 : 0;
}

static int lui_push(LUI_Text *t, const char *text, int len, unsigned char kind,
                    unsigned char cont)
{
    LUI_Line *ln;
    if (t->count == t->cap) {
        int cap = t->cap ? t->cap * 2 : 64;
        LUI_Line *grown = (LUI_Line *)realloc(t->lines, (size_t)cap * sizeof *grown);
        if (!grown)
            return 0;
        t->lines = grown;
        t->cap = cap;
    }
    ln = &t->lines[t->count];
    ln->text = (char *)malloc((size_t)len + 1);
    if (!ln->text)
        return 0;
    memcpy(ln->text, text, (size_t)len);
    ln->text[len] = '\0';
    ln->kind = kind;
    ln->cont = cont;
    t->count++;
    return 1;
}

/* Drop the inline markup the changelog actually uses, in place. `**bold**` and
 * `code` are the only two, and both are decoration rather than content: printing
 * the asterisks would put punctuation on screen that the file's author meant as
 * emphasis. HTML comments go too, which is how the `<!-- discord: ... -->` marker
 * that rides on every version heading stays off the panel. */
static void lui_strip_markup(char *s)
{
    char *w = s;
    const char *r = s;
    while (*r) {
        if (r[0] == '<' && r[1] == '!' && r[2] == '-' && r[3] == '-') {
            const char *end = strstr(r, "-->");
            if (!end)
                break; /* an unterminated comment eats the rest of the line */
            r = end + 3;
            continue;
        }
        if (r[0] == '*' && r[1] == '*') {
            r += 2;
            continue;
        }
        if (r[0] == '`') {
            r++;
            continue;
        }
        *w++ = *r++;
    }
    /* Trailing space is ordinary once a comment has been cut off the end. */
    while (w > s && w[-1] == ' ')
        w--;
    *w = '\0';
}

/* Greedy word wrap. A word wider than the whole column is broken rather than
 * allowed to run past the clip, because a clipped line is a line that lies about
 * what the changelog says. */
static int lui_wrap(LUI_Text *t, const DB_Font *f, const char *text, unsigned char kind,
                    int width)
{
    int avail = width - lui_line_indent(kind);
    int start = 0, n = (int)strlen(text);
    unsigned char cont = 0;

    if (avail < 8)
        avail = 8;
    if (n == 0)
        return lui_push(t, "", 0, kind, 0);

    while (start < n) {
        int take = 0, lastspace = -1, w = 0, i;
        for (i = start; i < n; i++) {
            unsigned char c = (unsigned char)text[i];
            int cw = (c < f->nchars) ? f->width[c] + DB_FONT6_XSPACING : 0;
            if (w + cw > avail)
                break;
            w += cw;
            if (c == ' ')
                lastspace = i;
        }
        if (i >= n) {
            take = n - start;
        } else if (lastspace > start) {
            take = lastspace - start;
        } else {
            take = i - start; /* one unbreakable word: break it */
            if (take < 1)
                take = 1;
        }
        if (!lui_push(t, text + start, take, kind, cont))
            return 0;
        cont = 1;
        start += take;
        while (start < n && text[start] == ' ')
            start++;
    }
    return 1;
}

int lui_text_build(LUI_Text *t, const DB_Font *f, const char *blob, int width)
{
    const char *p;
    char line[1024];

    memset(t, 0, sizeof *t);
    if (!f || !blob)
        return 0;

    for (p = blob; *p;) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        unsigned char kind = LUI_LINE_BODY;
        const char *body;

        if (len > (int)sizeof line - 1)
            len = (int)sizeof line - 1;
        memcpy(line, p, (size_t)len);
        line[len] = '\0';
        p = nl ? nl + 1 : p + strlen(p);

        /* A CRLF file read in binary mode, which is what a download is. */
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
            line[--len] = '\0';

        lui_strip_markup(line);
        body = line;

        if (body[0] == '#') {
            int hashes = 0;
            while (body[hashes] == '#')
                hashes++;
            kind = (hashes <= 2) ? LUI_LINE_HEAD : LUI_LINE_CAT;
            body += hashes;
            while (*body == ' ')
                body++;
        } else if ((body[0] == '-' || body[0] == '*') && body[1] == ' ') {
            kind = LUI_LINE_BULLET;
            body += 2;
        } else if (body[0] == '-' && body[1] == '-' && body[2] == '-' && !body[3]) {
            /* The file's own divider between one entry and the next. On screen the
             * version headings already do that job, so it is dropped rather than
             * drawn as a stray rule under the last bullet. */
            continue;
        } else if (!body[0]) {
            kind = LUI_LINE_BLANK;
        }

        if (kind == LUI_LINE_BLANK) {
            /* Never open the panel on a blank row, and never stack two: a changelog
             * is mostly blank lines and they would eat half the visible rows. */
            if (t->count == 0 || t->lines[t->count - 1].kind == LUI_LINE_BLANK)
                continue;
            if (!lui_push(t, "", 0, LUI_LINE_BLANK, 0))
                return 0;
            continue;
        }

        if (!lui_wrap(t, f, body, kind, width))
            return 0;
    }
    return t->count;
}

void lui_text_free(LUI_Text *t)
{
    int i;
    for (i = 0; i < t->count; i++)
        free(t->lines[i].text);
    free(t->lines);
    memset(t, 0, sizeof *t);
}

void lui_text_scroll(LUI_Text *t, int delta)
{
    int max = t->count - t->visible;
    if (max < 0)
        max = 0;
    t->top += delta;
    if (t->top > max)
        t->top = max;
    if (t->top < 0)
        t->top = 0;
}

void lui_text_draw(DB_Surface *s, const DB_Font *f, const LUI_Text *t, int x, int y, int w,
                   int h)
{
    int i, row;
    unsigned char colour;

    if (!f)
        return;

    /* Clip to the panel rather than trusting the wrap: a font swap or a stray
     * long token would otherwise paint over the dialog border. */
    db_clip(s, x, y, x + w - 1, y + h - 1);

    for (row = 0; row < t->visible; row++) {
        const LUI_Line *ln;
        int ly = y + row * LUI_LINE_H;
        i = t->top + row;
        if (i >= t->count)
            break;
        ln = &t->lines[i];
        if (ln->kind == LUI_LINE_BLANK)
            continue;

        switch (ln->kind) {
        case LUI_LINE_HEAD:
            colour = DM_TEXT_BRIGHT;
            break;
        case LUI_LINE_CAT:
            colour = DB_WHITE;
            break;
        default:
            colour = DM_TEXT_MEDIUM;
            break;
        }

        if (ln->kind == LUI_LINE_BULLET && !ln->cont) {
            /* A two pixel square rather than a glyph: the DOS fonts have no bullet
             * character, and a hyphen reads as a minus sign in a list of changes. */
            db_fill_rect(s, x + 1, ly + 3, x + 2, ly + 4, DM_TEXT_MEDIUM);
        }

        lui_print(s, f, ln->text, x + lui_line_indent(ln->kind), ly, colour);
    }

    db_clip_reset(s);
}

void lui_scrollbar_draw(DB_Surface *s, const LUI_Text *t, int x, int y, int w, int h)
{
    int span = t->count - t->visible;
    int th, ty;

    if (span <= 0)
        return; /* everything fits: a thumb that cannot move is furniture */

    /* The well, then the thumb. Same two greens as the dialog border so the bar
     * belongs to the panel rather than to the button row. */
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, DB_BLACK);
    db_line_v(s, x, y, y + h - 1, DM_GREEN_SHADOW);
    db_line_v(s, x + w - 1, y, y + h - 1, DM_GREEN_SHADOW);

    th = (h * t->visible) / t->count;
    if (th < 6)
        th = 6;
    if (th > h)
        th = h;
    ty = y + ((h - th) * t->top) / span;
    db_fill_rect(s, x + 1, ty, x + w - 2, ty + th - 1, DM_GREEN_BOX);
}

/* ------------------------------------------------------------------------ *
 * The gauge.
 * ------------------------------------------------------------------------ */

void lui_gauge_draw(DB_Surface *s, int x, int y, int w, int h, int frac, int phase)
{
    int inner = w - 2, fill;

    /* gauge.cpp draws the well as a sunken box and the bar inside it. */
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, DB_BLACK);
    db_line_h(s, x, x + w - 1, y, DM_GREEN_SHADOW);
    db_line_h(s, x, x + w - 1, y + h - 1, DM_GREEN_SHADOW);
    db_line_v(s, x, y, y + h - 1, DM_GREEN_SHADOW);
    db_line_v(s, x + w - 1, y, y + h - 1, DM_GREEN_SHADOW);

    if (inner < 1)
        return;

    if (frac < 0) {
        /* Length unknown. A barber pole says "working" without claiming a
         * position, which a bar creeping to 90% and stopping does not. */
        int i;
        for (i = 0; i < inner; i++)
            if (((i + phase) / 3) % 2 == 0)
                db_line_v(s, x + 1 + i, y + 1, y + h - 2, DM_GREEN_SHADOW);
        return;
    }

    if (frac > 1000)
        frac = 1000;
    fill = (inner * frac) / 1000;
    if (fill > 0)
        db_fill_rect(s, x + 1, y + 1, x + fill, y + h - 2, DM_LIGHT_GREEN);
}
