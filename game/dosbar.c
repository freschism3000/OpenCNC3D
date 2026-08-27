/*
 * dosbar.c -- the 1995 MS-DOS Command & Conquer sidebar.
 *
 * Every primitive here is a port of the engine routine named in its comment, and
 * every coordinate comes from dosbar.h, which quotes sidebar.h / sidebar.cpp /
 * radar.cpp / power.cpp / tab.cpp line by line. Nothing is eyeballed.
 *
 * ONE PLACE THE SOURCES DISAGREE, and what we do about it.
 *
 *   sidebar.h's #if 0 block is the literal 1995 layout (the header is stamped
 *   "sidebar.h_v 2.18 16 Oct 1995"):
 *       COLUMN_TWO_X = COLUMN_ONE_X + ((SIDE_WIDTH-16)/2) + 3   ->  283
 *       UP_X_OFFSET  = 2,  DOWN_X_OFFSET = 18
 *   sidebar.cpp's live code is the C&C95 generalisation, evaluated at factor 1:
 *       Column[1].X = Column[0].X + STRIP_WIDTH + spacing - 1   ->  282
 *       DownButton.X = UpButton.X + Width + spacing - 2         ->  X+17
 *   The generalised forms are one pixel short in both cases, and the -1 / -2
 *   terms are hi-res fudges. The 1995 numbers also tile cleanly: two 35 wide
 *   strips at 248 and 283 are exactly contiguous, and two 16 wide scroll buttons
 *   at +2 and +18 are exactly contiguous inside a 35 wide strip. We use the
 *   sidebar.h numbers. Everywhere else the two agree exactly (COLUMN_ONE_X 248,
 *   first row y 91, UP_X_OFFSET 2).
 *
 * WHAT THE 1995 BUILD DOES NOT HAVE, and which we therefore do not draw:
 *   SIDE1.SHP / SIDE2.SHP (the sidebar panel bitmap), REPAIR.SHP / SELL.SHP /
 *   MAP.SHP (the button bitmaps). All five are C&C95 hi-res additions and are
 *   absent from the DOS archives. sidebar.cpp:776 draws the panel with Fill_Rect
 *   + Draw_Box instead, and Init_IO:301 makes the three buttons TextButtonClass
 *   with Set_Text("Repair"/"Sell"/"Map") in 6 point. That is what happens here.
 */

#include "dosbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================== *
 * Pack loading
 * ======================================================================== */

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

DB_Pack *db_pack_load(const char *path, char *err, int errlen)
{
    FILE *fh;
    long size;
    unsigned char *blob;
    const unsigned char *p, *end;
    DB_Pack *pk;
    int i;

#define FAIL(msg)                                                                                                      \
    do {                                                                                                               \
        if (err)                                                                                                       \
            snprintf(err, (size_t)errlen, "%s", msg);                                                                  \
        goto bad;                                                                                                      \
    } while (0)

    fh = fopen(path, "rb");
    if (!fh) {
        if (err)
            snprintf(err, (size_t)errlen, "cannot open %s", path);
        return NULL;
    }
    fseek(fh, 0, SEEK_END);
    size = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    blob = (unsigned char *)malloc((size_t)size);
    if (!blob || fread(blob, 1, (size_t)size, fh) != (size_t)size) {
        fclose(fh);
        free(blob);
        if (err)
            snprintf(err, (size_t)errlen, "short read on %s", path);
        return NULL;
    }
    fclose(fh);

    pk = (DB_Pack *)calloc(1, sizeof(DB_Pack));
    pk->blob = blob;
    pk->blobsize = size;

    p = blob;
    end = blob + size;
    if (size < 24 || memcmp(p, "DOSBAR01", 8) != 0)
        FAIL("bad magic (expected DOSBAR01)");
    p += 8;
    if (rd32(p) != 1)
        FAIL("unsupported pack version");
    pk->nshapes = (int)rd32(p + 4);
    pk->nfonts = (int)rd32(p + 8);
    if (rd32(p + 12) != 256)
        FAIL("palette is not 256 entries");
    p += 16;

    pk->pal6 = p;
    p += 768;
    pk->pal8 = p;
    p += 768;
    pk->clocktab = p;
    p += 512;

    pk->shapes = (DB_Shape *)calloc((size_t)pk->nshapes, sizeof(DB_Shape));
    for (i = 0; i < pk->nshapes; i++) {
        DB_Shape *sh = &pk->shapes[i];
        long need;
        if (p + 24 > end)
            FAIL("truncated shape table");
        memcpy(sh->name, p, 12);
        sh->name[12] = 0;
        sh->frames = (int)rd32(p + 12);
        sh->w = (int)rd32(p + 16);
        sh->h = (int)rd32(p + 20);
        p += 24;
        need = (long)sh->frames * sh->w * sh->h;
        if (need <= 0 || p + need > end)
            FAIL("shape payload runs past end of pack");
        sh->pixels = p;
        p += need;
    }

    pk->fonts = (DB_Font *)calloc((size_t)pk->nfonts, sizeof(DB_Font));
    for (i = 0; i < pk->nfonts; i++) {
        DB_Font *f = &pk->fonts[i];
        long need;
        if (p + 20 > end)
            FAIL("truncated font table");
        memcpy(f->name, p, 8);
        f->name[8] = 0;
        f->nchars = (int)rd32(p + 8);
        f->maxw = (int)rd32(p + 12);
        f->maxh = (int)rd32(p + 16);
        p += 20;
        need = (long)f->nchars * 3 + (long)f->nchars * f->maxw * f->maxh;
        if (need <= 0 || p + need > end)
            FAIL("font payload runs past end of pack");
        f->width = p;
        p += f->nchars;
        f->ytop = p;
        p += f->nchars;
        f->rows = p;
        p += f->nchars;
        f->cells = p;
        p += (long)f->nchars * f->maxw * f->maxh;
    }
    return pk;

bad:
    db_pack_free(pk);
    return NULL;
#undef FAIL
}

void db_pack_free(DB_Pack *p)
{
    if (!p)
        return;
    free(p->shapes);
    free(p->fonts);
    free(p->blob);
    free(p);
}

const DB_Shape *db_shape(const DB_Pack *p, const char *name)
{
    int i;
    for (i = 0; i < p->nshapes; i++)
        if (strcmp(p->shapes[i].name, name) == 0)
            return &p->shapes[i];
    return NULL;
}

const DB_Font *db_font(const DB_Pack *p, const char *name)
{
    int i;
    for (i = 0; i < p->nfonts; i++)
        if (strcmp(p->fonts[i].name, name) == 0)
            return &p->fonts[i];
    return NULL;
}

/* ======================================================================== *
 * Surface primitives.
 * GraphicViewPortClass::Fill_Rect and Draw_Line are both INCLUSIVE of the end
 * coordinate; keep that, or every bevel is one pixel wrong.
 * ======================================================================== */

void db_surface_init(DB_Surface *s, int w, int h, unsigned char *storage)
{
    s->w = w;
    s->h = h;
    s->px = storage;
    memset(storage, 0, (size_t)w * h);
    db_clip_reset(s);
}

void db_clip_reset(DB_Surface *s)
{
    s->cx0 = 0;
    s->cy0 = 0;
    s->cx1 = s->w - 1;
    s->cy1 = s->h - 1;
}

void db_clip(DB_Surface *s, int x0, int y0, int x1, int y1)
{
    s->cx0 = x0 < 0 ? 0 : x0;
    s->cy0 = y0 < 0 ? 0 : y0;
    s->cx1 = x1 > s->w - 1 ? s->w - 1 : x1;
    s->cy1 = y1 > s->h - 1 ? s->h - 1 : y1;
}

void db_put_pixel(DB_Surface *s, int x, int y, unsigned char c)
{
    if (x >= s->cx0 && x <= s->cx1 && y >= s->cy0 && y <= s->cy1)
        s->px[y * s->w + x] = c;
}

void db_fill_rect(DB_Surface *s, int x0, int y0, int x1, int y1, unsigned char c)
{
    int x, y;
    if (x0 > x1) {
        x = x0;
        x0 = x1;
        x1 = x;
    }
    if (y0 > y1) {
        y = y0;
        y0 = y1;
        y1 = y;
    }
    if (x0 < s->cx0)
        x0 = s->cx0;
    if (y0 < s->cy0)
        y0 = s->cy0;
    if (x1 > s->cx1)
        x1 = s->cx1;
    if (y1 > s->cy1)
        y1 = s->cy1;
    for (y = y0; y <= y1; y++)
        memset(s->px + (size_t)y * s->w + x0, c, (size_t)(x1 - x0 + 1));
}

void db_line_h(DB_Surface *s, int x0, int x1, int y, unsigned char c)
{
    db_fill_rect(s, x0, y, x1, y, c);
}

void db_line_v(DB_Surface *s, int x, int y0, int y1, unsigned char c)
{
    db_fill_rect(s, x, y0, x, y1, c);
}

/* dialog.cpp:95 Draw_Box, ButtonColorsClassic (Get_Resolution_Factor()==0 forces
 * useGoldStyle false, so the classic table is the only one that can apply). */
void db_draw_box(DB_Surface *s, int x, int y, int w, int h, DB_BoxStyle style, int filled)
{
    static const unsigned char tbl[5][4] = {
        /* Filler,    Shadow,     Highlight,  Corner */
        {DB_LTGREY, DB_WHITE, DB_DKGREY, DB_LTGREY},  /* 0 BOXSTYLE_DOWN         */
        {DB_LTGREY, DB_DKGREY, DB_WHITE, DB_LTGREY},  /* 1 BOXSTYLE_RAISED       */
        {DB_LTBLUE, DB_BLUE, DB_LTCYAN, DB_LTBLUE},   /* 2 raised blue           */
        {DB_DKGREY, DB_WHITE, DB_BLACK, DB_DKGREY},   /* 3 BOXSTYLE_DIS_DOWN     */
        {DB_DKGREY, DB_BLACK, DB_WHITE, DB_LTGREY}    /* 4 BOXSTYLE_DIS_RAISED   */
    };
    const unsigned char *st = tbl[(int)style];
    w--;
    h--;
    if (filled)
        db_fill_rect(s, x, y, x + w, y + h, st[0]);
    db_line_h(s, x, x + w, y + h, st[1]);
    db_line_v(s, x + w, y, y + h, st[1]);
    db_line_h(s, x, x + w, y, st[2]);
    db_line_v(s, x, y, y + h, st[2]);
    db_put_pixel(s, x, y + h, st[3]);
    db_put_pixel(s, x + w, y, st[3]);
}

/* keybuff.cpp:76 BF_Trans -- shape blit, index 0 transparent. */
void db_draw_shape(DB_Surface *s, const DB_Shape *sh, int frame, int x, int y)
{
    const unsigned char *src;
    int yy, xx;
    if (!sh || frame < 0 || frame >= sh->frames)
        return;
    src = sh->pixels + (size_t)frame * sh->w * sh->h;
    for (yy = 0; yy < sh->h; yy++) {
        int dy = y + yy;
        if (dy < s->cy0 || dy > s->cy1)
            continue;
        for (xx = 0; xx < sh->w; xx++) {
            unsigned char c = src[yy * sh->w + xx];
            int dx = x + xx;
            if (c && dx >= s->cx0 && dx <= s->cx1)
                s->px[dy * s->w + dx] = c;
        }
    }
}

/* keybuff.cpp:1287  SHAPE_CENTER: x -= width/2; y -= height/2; */
void db_draw_shape_centered(DB_Surface *s, const DB_Shape *sh, int frame, int cx, int cy)
{
    if (!sh)
        return;
    db_draw_shape(s, sh, frame, cx - sh->w / 2, cy - sh->h / 2);
}

/* keybuff.cpp:133 BF_Ghost_Trans -- index 0 transparent, and any source pixel the
 * ghost lookup claims (here only GREEN) replaces itself with a table lookup on the
 * DESTINATION pixel. That is how the build clock and the "cannot build" darken
 * work: CLOCK.SHP is a solid GREEN wedge, and the translucent table turns whatever
 * is underneath it into its 180/256 fade toward LTGREY.
 * (sidebar.cpp:1307 TLucentType ClockCols = {GREEN, LTGREY, 180, 0}) */
void db_draw_shape_ghost(DB_Surface *s, const DB_Shape *sh, int frame, int x, int y,
                         const unsigned char *clocktab)
{
    const unsigned char *src;
    const unsigned char *lookup = clocktab;
    const unsigned char *ghost = clocktab + 256;
    int yy, xx;
    if (!sh || frame < 0 || frame >= sh->frames)
        return;
    src = sh->pixels + (size_t)frame * sh->w * sh->h;
    for (yy = 0; yy < sh->h; yy++) {
        int dy = y + yy;
        if (dy < s->cy0 || dy > s->cy1)
            continue;
        for (xx = 0; xx < sh->w; xx++) {
            unsigned char sbyte = src[yy * sh->w + xx];
            int dx = x + xx;
            if (!sbyte || dx < s->cx0 || dx > s->cx1)
                continue;
            {
                unsigned char *dst = &s->px[dy * s->w + dx];
                unsigned char fbyte = lookup[sbyte];
                if (fbyte != 0xFF)
                    sbyte = ghost[*dst + fbyte * 256];
                *dst = sbyte;
            }
        }
    }
}

/* ======================================================================== *
 * Text.  common/font.cpp Buffer_Print + dialog.cpp Simple_Text_Print.
 * ======================================================================== */

/* Simple_Text_Print's fontpalette[16] for TPF_NOSHADOW: classes 2 and 3 collapse
 * to `back`, and Buffer_Print skips any pixel whose looked-up colour is 0, so with
 * back == TBLACK the shadow and outline classes simply do not draw. */
void db_font_palette(unsigned char fp[16], unsigned char fore, unsigned char back)
{
    int i;
    for (i = 0; i < 16; i++)
        fp[i] = back;
    fp[0] = back;
    fp[1] = fore;
    fp[2] = back; /* TPF_NOSHADOW */
    fp[3] = back; /* TPF_NOSHADOW */
}

/* dialog.cpp:407-421, the TPF_6PT_GRAD branch WITHOUT TPF_USE_GRAD_PAL, which is
 * what tab.cpp's lo-res tab captions use: memset(&fontpalette[4], fore, 12) after
 * the whole array was set to `back`, then the NOSHADOW switch resets 2 and 3.
 * The gradient font's glyphs use nibble classes 1 and 4..15, so this flattens the
 * ramp to a single colour, which is exactly what the DOS build shows. */
void db_font_palette_grad(unsigned char fp[16], unsigned char fore, unsigned char back)
{
    int i;
    for (i = 0; i < 16; i++)
        fp[i] = back;
    for (i = 4; i < 16; i++)
        fp[i] = fore;
    fp[2] = back; /* TPF_NOSHADOW */
    fp[3] = back; /* TPF_NOSHADOW */
    fp[0] = back;
    fp[1] = fore;
}

int db_string_width(const DB_Font *f, const char *text, int xspacing)
{
    int w = 0;
    const unsigned char *t = (const unsigned char *)text;
    for (; *t; t++)
        if (*t < f->nchars)
            w += f->width[*t] + xspacing;
    return w;
}

void db_print(DB_Surface *s, const DB_Font *f, const char *text, int x, int y,
              const unsigned char *fontpal, int xspacing)
{
    const unsigned char *t = (const unsigned char *)text;
    for (; *t; t++) {
        int c = *t;
        const unsigned char *cell;
        int gw, yy, xx;
        if (c >= f->nchars)
            continue;
        gw = f->width[c];
        cell = f->cells + (size_t)c * f->maxw * f->maxh;
        for (yy = 0; yy < f->maxh; yy++)
            for (xx = 0; xx < gw && xx < f->maxw; xx++) {
                unsigned char col = fontpal[cell[yy * f->maxw + xx]];
                if (col)
                    db_put_pixel(s, x + xx, y + yy, col);
            }
        x += gw + xspacing;
    }
}

/* textbtn.cpp:282 Draw_Background + :336 Draw_Text, non-TPF_6PT_GRAD branch. */
static void db_text_button(DB_Surface *s, const DB_Font *f6, const char *label, int x, int y, int w, int h,
                           int pressed, int on, int disabled)
{
    unsigned char fp[16];
    unsigned char colour;
    DB_BoxStyle style;
    int tx;

    if (disabled)
        style = DB_BOX_DIS_RAISED;
    else if (pressed)
        style = DB_BOX_DOWN;
    else
        style = DB_BOX_RAISED;
    db_draw_box(s, x, y, w, h, style, 1);

    if (disabled)
        colour = DB_LTGREY;
    else if (pressed)
        colour = DB_DKGREY; /* TPF_NOSHADOW is set, so DKGREY not LTGREY */
    else
        colour = DB_WHITE;
    if (on)
        colour = DB_RED; /* Repair and Sell are IsToggleType */

    db_font_palette(fp, colour, DB_TBLACK);
    /* Fancy_Text_Print(text, X + (Width>>1) - 1, Y + 1, ..., flags|TPF_CENTER)
     * and TPF_CENTER does x -= String_Pixel_Width(text) >> 1. */
    tx = x + (w >> 1) - 1 - (db_string_width(f6, label, DB_FONT6_XSPACING) >> 1);
    db_print(s, f6, label, tx, y + 1, fp, DB_FONT6_XSPACING);
}

/* ======================================================================== *
 * State helpers
 * ======================================================================== */

void db_state_clear(DB_State *st)
{
    memset(st, 0, sizeof(*st));
}

int db_state_add(DB_State *st, int column, const char *cameo)
{
    int i;
    if (column < 0 || column >= DB_COLUMNS || st->count[column] >= DB_MAX_BUILDABLES)
        return -1;
    i = st->count[column]++;
    memset(&st->items[column][i], 0, sizeof(DB_Slot));
    strncpy(st->items[column][i].cameo, cameo, sizeof(st->items[column][i].cameo) - 1);
    return i;
}

/* ======================================================================== *
 * The strip.  sidebar.cpp:1757 SidebarClass::StripClass::Draw_It
 * ======================================================================== */

static void db_draw_strip(DB_Surface *s, const DB_Pack *p, const DB_State *st, int id, int stripx)
{
    const DB_Shape *logo = db_shape(p, "STRIP");
    const DB_Shape *clock = db_shape(p, "CLOCK");
    const DB_Shape *pips = db_shape(p, "PIPS");
    const DB_Shape *up = db_shape(p, "STRIPUP");
    const DB_Shape *dn = db_shape(p, "STRIPDN");
    int i;

    /* Scroll buttons first, as Draw_It does (:1780). They live outside the strip
     * clip window, flush with the bottom of the screen: 188..199. */
    db_clip_reset(s);
    db_draw_shape(s, up, 0, stripx + DB_UP_X_OFFSET, DB_COLUMN_Y - 1 + DB_UP_Y_OFFSET);
    db_draw_shape(s, dn, 0, stripx + DB_DOWN_X_OFFSET, DB_COLUMN_Y - 1 + DB_UP_Y_OFFSET);

    /* Everything below is clipped to WINDOW_SIDEBAR. */
    db_clip(s, DB_WIN_X, DB_WIN_Y, DB_WIN_X + DB_WIN_W - 1, DB_WIN_Y + DB_WIN_H - 1);

    for (i = 0; i < DB_MAX_VISIBLE; i++) {
        int index = i + st->top[id];
        int x = stripx;
        int y = DB_COLUMN_Y + i * DB_OBJECT_HEIGHT - 1; /* :1798-1799 y = Y + i*h; y--; */
        int sx = x + DB_LEFT_EDGE_OFFSET;
        const DB_Slot *slot = NULL;
        const DB_Shape *cameo = NULL;

        if (index < st->count[id]) {
            slot = &st->items[id][index];
            cameo = db_shape(p, slot->cameo);
        }

        if (!cameo) {
            /* :1916 shapefile = LogoShapes; shapenum = SB_BLANK; */
            db_draw_shape(s, logo, DB_SB_BLANK, sx, y);
            continue;
        }

        db_draw_shape(s, cameo, 0, sx, y);

        /* :1945 darken -- a factory of this type is already busy. CLOCK frame 0
         * is the full-cell wedge, ghosted. */
        if (slot->darken)
            db_draw_shape_ghost(s, clock, 0, sx, y, p->clocktab);

        if (slot->producing) {
            if (slot->ready) {
                /* :1967 PIP_READY, SHAPE_CENTER at
                 *   x + LeftEdgeOffset + (ObjectWidth>>1),
                 *   y + ObjectHeight - Get_Build_Frame_Height(PipShapes) - 8   */
                db_draw_shape_centered(s, pips, DB_PIP_READY, sx + (DB_OBJECT_WIDTH >> 1),
                                       y + DB_OBJECT_HEIGHT - (pips ? pips->h : 0) - 8);
            } else {
                /* :1977 ClockShapes frame stage+1, stage = Completion() 0..107 */
                int stage = slot->completion;
                if (stage < 0)
                    stage = 0;
                if (stage > DB_STEP_COUNT - 1)
                    stage = DB_STEP_COUNT - 1;
                db_draw_shape_ghost(s, clock, stage + 1, sx, y, p->clocktab);

                /* :1990 PIP_HOLDING when the factory exists but is not building */
                if (slot->onhold)
                    db_draw_shape_centered(s, pips, DB_PIP_HOLDING, sx + (DB_OBJECT_WIDTH >> 1),
                                           y + DB_OBJECT_HEIGHT - (pips ? pips->h : 0) - 8);
            }
        }
    }
    db_clip_reset(s);
}

/* ======================================================================== *
 * The power gauge.  power.cpp:170 PowerClass::Draw_It, else branch of `factor`
 * ("Draw MS-DOS power gauge").
 * ======================================================================== */

static void db_draw_power(DB_Surface *s, const DB_Pack *p, const DB_State *st)
{
    const DB_Shape *power = db_shape(p, "POWER");
    int bottom = DB_POW_Y + DB_POW_HEIGHT - 1;
    int x0 = DB_POW_X, x1 = x0 + 7;
    int y0 = DB_POW_Y + 1, y1 = bottom;
    int y;
    int ph = st->power_total, dh = st->power_drain;
    unsigned char power_color;

    if (ph < 0)
        ph = 0;
    if (ph > DB_POW_HEIGHT - 2)
        ph = DB_POW_HEIGHT - 2;
    if (dh < 0)
        dh = 0;
    if (dh > DB_POW_HEIGHT - 2)
        dh = DB_POW_HEIGHT - 2;

    db_fill_rect(s, x0, y0, x1, y1, DB_LTGREY);          /* inside power bar        */
    db_line_v(s, x0, y0, y1, DB_WHITE);                  /* left line               */
    db_line_v(s, x0 + 1, y0, y1, DB_DKGREY);             /* left shadow line        */
    db_line_h(s, x0, x1 - 1, y0, DB_WHITE);              /* upper line              */
    db_line_h(s, x0 + 1, x1 - 1, y0 + 1, DB_DKGREY);     /* upper shadow line       */
    db_line_v(s, x1 - 1, y0, y1, DB_WHITE);              /* right line              */
    db_line_v(s, x1, y0, y1, DB_DKGREY);                 /* right shadow line       */
    db_line_h(s, x0, x1 - 1, y1, DB_WHITE);              /* bottom line             */
    for (y = y0 + 4; y < y1 - 1; y += 5)
        db_line_h(s, x0 + 2, x0 + 4, y, DB_DKGREY);      /* the meter ticks         */

    if (ph) {
        /* WATTS, not the bar pixels above: power.cpp:475-482 tests PlayerPtr->Drain
           against PlayerPtr->Power directly. Power_Height saturates, so comparing the
           heights makes RED nearly unreachable and moves YELLOW's threshold as well. */
        const int pw = st->power_watts, dw = st->drain_watts;
        power_color = DB_GREEN;
        if (dw > pw)
            power_color = DB_YELLOW;
        if (dw > pw * 2)
            power_color = DB_RED;
        db_fill_rect(s, DB_POW_X + 2, bottom - ph + 1, DB_POW_X + 5, bottom - 1, power_color);
    }
    /* CC_Draw_Shape(PowerShape, 0, PowX, bottom - drain_height, ...) */
    db_draw_shape(s, power, 0, DB_POW_X, bottom - dh);
}

/* ======================================================================== *
 * Everything.
 * ======================================================================== */

void db_draw_sidebar(DB_Surface *s, const DB_Pack *p, const DB_State *st)
{
    const DB_Font *f6 = db_font(p, "6POINT");
    const DB_Font *fgrad = db_font(p, "GRAD6FNT");
    const DB_Shape *tabs = db_shape(p, "TABS");
    const DB_Shape *radar = db_shape(p, st->nod ? "RADARNOD" : "RADARGDI");

    db_clip_reset(s);

    /* --- the tab strip.  tab.cpp:110-114 TabClass::Draw_It ---------------- *
     * Fill_Rect(0, 0, rightx, Tab_Height-2, BLACK); the TABS.SHP plate at the
     * left and at width-Eva_Width; Draw_Line(0, Tab_Height-1, rightx, BLACK).
     * We only own x >= SIDE_X, so only the right hand plate is ours.          */
    db_fill_rect(s, DB_SIDE_X, 0, DB_SCREEN_W - 1, DB_TAB_HEIGHT - 2, DB_BLACK);
    db_draw_shape(s, tabs, 0, DB_SCREEN_W - DB_EVA_WIDTH, 0);
    db_line_h(s, DB_SIDE_X, DB_SCREEN_W - 1, DB_TAB_HEIGHT - 1, DB_BLACK);
    if (fgrad || f6) {
        /* tab.cpp:116  Fancy_Text_Print(TXT_TAB_SIDEBAR, width - Eva_Width/2, 0,
         * WHITE, TBLACK, TPF_6PT_GRAD|TPF_CENTER|TPF_NOSHADOW) in the lo-res branch.
         * TPF_6PT_GRAD selects GradFont6Ptr == GRAD6FNT.FNT (dialog.cpp:449) and,
         * with no TPF_USE_GRAD_PAL, flattens the ramp to `fore`. */
        const DB_Font *ft = fgrad ? fgrad : f6;
        unsigned char fp[16];
        const char *label = "Sidebar";
        int tx = DB_SCREEN_W - (DB_EVA_WIDTH / 2) - (db_string_width(ft, label, DB_FONT6_XSPACING) >> 1);
        if (fgrad)
            db_font_palette_grad(fp, DB_WHITE, DB_TBLACK);
        else
            db_font_palette(fp, DB_WHITE, DB_TBLACK);
        db_print(s, ft, label, tx, 0, fp, DB_FONT6_XSPACING);
    }

    /* --- the radar.  radar.cpp:430-441, RadarClass::Draw_It full redraw --- *
     *   CC_Draw_Shape(RadarAnim, RADAR_ACTIVATED_FRAME, RadX, RadY+1, ...)
     *   Fill_Rect(RadX+RadOffX, RadY+RadOffY, +RadIWidth-1, +RadIHeight-1, BLACK)
     * RadarAnim is RADAR.<house suffix> (radar.cpp:348). It supplies the bezel;
     * there is no Draw_Box on this path. The Draw_Box at radar.cpp:1269 belongs
     * to Radar_Anim, the activation sequence, and is used only as the fallback
     * here if the bezel shape is missing from the pack. */
    if (radar)
        /* Active: frame 22, the bezel with the map hole. Inactive: frame 0, the
         * full house-logo plate -- what 1995 leaves on screen when there is no
         * radar facility or the power is too low (radar.cpp Radar_Activate(0)
         * ends on the anim's first frame, the logo). */
        db_draw_shape(s, radar, st->radar_active ? DB_RADAR_ACTIVATED_FRAME : 0,
                      DB_RAD_X, DB_RAD_Y + 1);
    else
        db_draw_box(s, DB_RAD_X + DB_RAD_OFF_X - 1, DB_RAD_Y + DB_RAD_OFF_Y - 1, DB_RAD_I_WIDTH + 2,
                    DB_RAD_I_HEIGHT + 2, DB_BOX_RAISED, 1);
    if (st->radar_active || !radar)
        db_fill_rect(s, DB_RAD_X + DB_RAD_OFF_X, DB_RAD_Y + DB_RAD_OFF_Y, DB_RAD_X + DB_RAD_OFF_X + DB_RAD_I_WIDTH - 1,
                     DB_RAD_Y + DB_RAD_OFF_Y + DB_RAD_I_HEIGHT - 1, DB_BLACK);

    /* --- the sidebar body.  sidebar.cpp:776-799, factor 0 branch ---------- */
    db_fill_rect(s, DB_SIDE_X + DB_POW_WIDTH, DB_SIDE_Y, DB_SIDE_X + DB_SIDE_WIDTH - 1, DB_SIDE_Y + DB_SIDE_HEIGHT - 1,
                 DB_LTGREY);
    db_fill_rect(s, DB_SIDE_X, DB_SIDE_Y - 1, DB_SIDE_X + DB_SIDE_WIDTH - 1, DB_SIDE_Y + DB_TOP_HEIGHT - 1, DB_LTGREY);
    /* the erase strip between the two columns (:788) */
    db_fill_rect(s, DB_COLUMN_ONE_X + DB_OBJECT_WIDTH + 1, DB_SIDE_Y + DB_TOP_HEIGHT + 1, DB_COLUMN_TWO_X,
                 DB_SIDE_Y + DB_SIDE_HEIGHT - 1, DB_LTGREY);
    /* the raised box around both strips (:794) */
    db_draw_box(s, DB_SIDE_X + DB_POW_WIDTH, DB_SIDE_Y + DB_TOP_HEIGHT, DB_SIDE_WIDTH - DB_POW_WIDTH,
                DB_SIDE_HEIGHT - DB_TOP_HEIGHT, DB_BOX_RAISED, 0);

    /* --- Repair / Sell / Map, TextButtonClass, 6 point ------------------- */
    if (f6) {
        db_text_button(s, f6, "Repair", DB_REPAIR_X, DB_REPAIR_Y, DB_REPAIR_W, DB_BUTTON_HEIGHT, st->repair_pressed,
                       st->repair_on, st->repair_disabled);
        db_text_button(s, f6, "Sell", DB_SELL_X, DB_REPAIR_Y, DB_SELL_W, DB_BUTTON_HEIGHT, st->sell_pressed,
                       st->sell_on, st->sell_disabled);
        db_text_button(s, f6, "Map", DB_MAP_X, DB_REPAIR_Y, DB_MAP_W, DB_BUTTON_HEIGHT, 0, 0, st->map_disabled);
    }

    /* --- the two strips.  Column 0 is structures, column 1 is units ------ */
    db_draw_strip(s, p, st, 0, DB_COLUMN_ONE_X);
    db_draw_strip(s, p, st, 1, DB_COLUMN_TWO_X);

    /* --- the power gauge ------------------------------------------------- */
    db_draw_power(s, p, st);
}

/* ======================================================================== *
 * The credits plate.  tab.cpp:129 TabClass::Draw_Credits_Tab draws TABS.SHP
 * at x = 160 in the lo-res branch, and credits.cpp:112 prints the number at
 * xx = SeenBuff width - 120 == 200, y 0, TPF_6PT_GRAD|TPF_CENTER|TPF_NOSHADOW
 * in WHITE.  It is a TAB BAR item, not a sidebar item: it sits to the LEFT of
 * the sidebar column and is drawn here only because it belongs to the same
 * 320x200 surface.
 * ======================================================================== */

void db_draw_credits_tab(DB_Surface *s, const DB_Pack *p, int credits)
{
    const DB_Shape *tabs = db_shape(p, "TABS");
    const DB_Font *fgrad = db_font(p, "GRAD6FNT");
    const DB_Font *f6 = db_font(p, "6POINT");
    const DB_Font *ft = fgrad ? fgrad : f6;
    char text[16];
    unsigned char fp[16];
    int tx;

    db_clip_reset(s);
    db_fill_rect(s, DB_CREDIT_TAB_X, 0, DB_CREDIT_TAB_X + DB_EVA_WIDTH - 1, DB_TAB_HEIGHT - 2, DB_BLACK);
    db_draw_shape(s, tabs, 0, DB_CREDIT_TAB_X, 0);
    db_line_h(s, DB_CREDIT_TAB_X, DB_CREDIT_TAB_X + DB_EVA_WIDTH - 1, DB_TAB_HEIGHT - 1, DB_BLACK);
    if (!ft)
        return;
    snprintf(text, sizeof text, "%d", credits);
    if (fgrad)
        db_font_palette_grad(fp, DB_WHITE, DB_TBLACK);
    else
        db_font_palette(fp, DB_WHITE, DB_TBLACK);
    tx = DB_CREDIT_TEXT_X - (db_string_width(ft, text, DB_FONT6_XSPACING) >> 1);
    db_print(s, ft, text, tx, 0, fp, DB_FONT6_XSPACING);
}

/* ======================================================================== *
 * The one and only conversion to RGBA.
 * ======================================================================== */

void db_surface_to_rgba(const DB_Surface *s, const unsigned char *pal8, unsigned char *rgba, int want_alpha)
{
    int i, n = s->w * s->h;
    for (i = 0; i < n; i++) {
        unsigned char c = s->px[i];
        rgba[i * 4 + 0] = pal8[c * 3 + 0];
        rgba[i * 4 + 1] = pal8[c * 3 + 1];
        rgba[i * 4 + 2] = pal8[c * 3 + 2];
        rgba[i * 4 + 3] = (want_alpha && c == 0) ? 0 : 255;
    }
}
