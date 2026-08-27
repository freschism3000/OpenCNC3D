/*
 * t1_hud.c -- the 640x480 HUD on the Voodoo 2. See t1_hud.h for the shape of it.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "t1_hud.h"
#include "t1_glide.h"
#include "dosbar.h"

/* ---------------------------------------------------------------------------------
 * RGBA to the card's 16 bits.
 *
 * 565 rather than 1555: every chunk that hud640.c composites into the bar was measured
 * and is fully opaque (only cell_frame and the digit strip carry alpha, and both are
 * composited INTO the opaque chassis before this sees them), so the alpha bit would buy
 * nothing and the green channel would lose a bit of precision paying for it.
 * --------------------------------------------------------------------------------- */
static void rgba_to_565(unsigned short *dst, int dstpitch,
                        const unsigned char *src, int srcpitch,
                        int w, int h)
{
    int x, y;
    for (y = 0; y < h; ++y)
    {
        const unsigned char *s = src + (long)y * srcpitch * 4;
        unsigned short *d = dst + (long)y * dstpitch;
        for (x = 0; x < w; ++x, s += 4)
            d[x] = (unsigned short)(((unsigned)(s[0] & 0xF8) << 8)
                                  | ((unsigned)(s[1] & 0xFC) << 3)
                                  | ((unsigned)(s[2]) >> 3));
    }
}

static unsigned short rgb565(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return (unsigned short)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* The pointer and the solid white block, drawn straight into the aux page.
 *
 * There is no system cursor: a fullscreen Glide app owns the screen outright and Windows
 * draws nothing on it. The arrow is therefore art, and it shares a page with the tab
 * plates for the same reason it used to share one with the DOS sidebar: if the tabs are
 * on screen, so is the pointer, and it cannot be lost to a texture that quietly failed. */
static void build_aux_art(unsigned short *page)
{
    const unsigned short W = rgb565(255, 255, 255);
    const unsigned short K = rgb565(0, 0, 0);
    int x, y;
    /* The classic arrow wedge, exactly the shape the DOS-sidebar build already drew:
     * white body, black edge, so it reads on grass and on water alike. The page around
     * it is MAGENTA, not black, and the chroma key is magenta for the same reason the
     * mesh converter uses it: black is a real colour in this art (it is the arrow's own
     * outline), and keying on it cut the outline away while still looking almost right. */
    for (y = 0; y < T1H_CUR_H; ++y)
        for (x = 0; x <= y && x < T1H_CUR_W; ++x)
            page[(T1H_CUR_Y + y) * T1H_PAGE + (T1H_CUR_X + x)] =
                (x == y || x == 0 || y == T1H_CUR_H - 1) ? K : W;
    for (y = 0; y < T1H_DOT_SZ; ++y)
        for (x = 0; x < T1H_DOT_SZ; ++x)
            page[(T1H_DOT_Y + y) * T1H_PAGE + (T1H_DOT_X + x)] = W;
}

int t1_hud_load(T1_Hud *h, const char *packpath, char *err, int errlen)
{
    memset(h, 0, sizeof *h);
    h->pack = hud640_load(packpath, err, errlen);
    if (!h->pack) return 0;
    /* The four derived row constants against the generated table. If they disagree the
     * extra-row arithmetic is wrong, which at 640x480 cannot bite, but a silent
     * disagreement is exactly the thing that bites later on a different screen. */
    if (!hud640_check_layout())
        _snprintf(err, errlen, "hud640_check_layout disagreed with the baked table");

    h->st.rows = T1H_ROWS;
    h->st.radar_active = 0;
    h->st.nod = 0;
    h->dirty = 1;
    h->last_hit = -1;
    h->ok = 1;

    /* The pages are magenta to start with, so a region that is never composed is loud
     * rather than plausibly black. The same rule the mesh converter follows. */
    {
        long i;
        for (i = 0; i < T1H_PAGE * T1H_PAGE; ++i)
        { h->pageA[i] = rgb565(255, 0, 255); h->pageB[i] = rgb565(255, 0, 255); }
        for (i = 0; i < T1H_PAGE * T1H_AUX_H; ++i) h->pageC[i] = rgb565(255, 0, 255);
    }
    build_aux_art(h->pageC);

    sr_texture16(&h->texA, h->pageA, T1H_PAGE, T1H_PAGE);
    sr_texture16(&h->texB, h->pageB, T1H_PAGE, T1H_PAGE);
    sr_texture16(&h->texC, h->pageC, T1H_PAGE, T1H_AUX_H);
    if (!t1_glide_upload(&h->texA, err, errlen)) return 0;
    if (!t1_glide_upload(&h->texB, err, errlen)) return 0;
    if (!t1_glide_upload(&h->texC, err, errlen)) return 0;
    return 1;
}

void t1_hud_free(T1_Hud *h)
{
    if (h->pack) { hud640_free(h->pack); h->pack = 0; }
    h->ok = 0;
}

/* ---------------------------------------------------------------------------------
 * The minimap's raw material.
 *
 * One averaged colour per terrain cell, taken from the SAME atlas page the ground is
 * drawn with and through the SAME palette, so the radar cannot drift away from the map
 * it claims to show. Index 0 is the terrain's water hole, which is a real colour there
 * and is treated as one here.
 * --------------------------------------------------------------------------------- */
void t1_hud_prepare_radar(T1_Hud *h, const T1_Terrain *t)
{
    int cy, cx, ty, tx;
    if (!t || !t->cells || !t->pages || !t->pal) return;
    for (cy = 0; cy < 64; ++cy)
        for (cx = 0; cx < 64; ++cx)
        {
            const unsigned char *rec = t->cells + ((long)cy * 64 + cx) * 4;
            int page = rec[0], px0 = rec[1] * t->tile, py0 = rec[2] * t->tile;
            long r = 0, g = 0, b = 0, n = 0;
            unsigned char *out = h->cellrgb + ((long)cy * 64 + cx) * 3;
            if (page < 0 || page >= t->npages) { out[0] = out[1] = out[2] = 0; continue; }
            for (ty = 0; ty < t->tile; ty += 3)
                for (tx = 0; tx < t->tile; tx += 3)
                {
                    int idx = t->pages[((long)page * t->page_sz + py0 + ty) * t->page_sz
                                       + px0 + tx];
                    r += t->pal[idx * 3 + 0];
                    g += t->pal[idx * 3 + 1];
                    b += t->pal[idx * 3 + 2];
                    ++n;
                }
            if (!n) n = 1;
            out[0] = (unsigned char)(r / n);
            out[1] = (unsigned char)(g / n);
            out[2] = (unsigned char)(b / n);
        }
    h->cellrgb_ok = 1;
}

/* ---------------------------------------------------------------------------------
 * The cameos.
 *
 * Composed in the ENGINE'S OWN COLOUR SPACE and converted once at the end, which is what
 * game/cnc_sidebar.h does and why: the build clock is a translucent shape drawn through
 * the pack's clocktab, and rebuilding that as an RGB blend would be a guess at a table
 * the pack already carries exactly.
 * --------------------------------------------------------------------------------- */
static void compose_cameo(T1_Hud *h, int slot, const DB_Pack *cp,
                          const char *name, int producing, int completed,
                          int onhold, int darken, int progress)
{
    static unsigned char cellpx[H6_CAMEO_W * H6_CAMEO_H];
    const DB_Shape *cameo, *clock, *pips;
    DB_Surface cell;
    unsigned char *dst = h->cameo[slot];
    int cw, chh, ox, oy, x, y, zoom;

    memset(dst, 0, H6_CAMEO_W * H6_CAMEO_H * 4);
    h->st.slot[slot].used = 0;
    h->st.slot[slot].rgba = 0;
    h->st.slot[slot].progress = -1;
    if (!cp || !name || !name[0]) return;

    cameo = db_shape(cp, name);
    if (!cameo) return;
    clock = db_shape(cp, "CLOCK");
    pips  = db_shape(cp, "PIPS");

    cw  = cameo->w  > H6_CAMEO_W ? H6_CAMEO_W : cameo->w;
    chh = cameo->h > H6_CAMEO_H ? H6_CAMEO_H : cameo->h;
    /* The overlays are cut for the CELL, not for the cameo, so they are centred on it. */
    ox = clock ? -((clock->w - cw) / 2) : 0;
    oy = clock ? -((clock->h - chh) / 2) : 0;

    db_surface_init(&cell, cw, chh, cellpx);
    memset(cellpx, 0, (size_t)cw * chh);
    db_draw_shape(&cell, cameo, 0, 0, 0);

    if (darken && clock)
        db_draw_shape_ghost(&cell, clock, 0, ox, oy, cp->clocktab);

    if (producing)
    {
        if (completed)
        {
            if (pips)
                db_draw_shape_centered(&cell, pips, DB_PIP_READY, cw >> 1, chh - pips->h - 8);
        }
        else
        {
            int stage = progress;
            if (stage < 0) stage = 0;
            if (stage > DB_STEP_COUNT - 1) stage = DB_STEP_COUNT - 1;
            if (clock) db_draw_shape_ghost(&cell, clock, stage + 1, ox, oy, cp->clocktab);
            if (onhold && pips)
                db_draw_shape_centered(&cell, pips, DB_PIP_HOLDING, cw >> 1, chh - pips->h - 8);
        }
    }

    /* Whole-number doubling, so no pixel is resampled. The DOS cameo is 32x24 and the
     * opening is 64x48, which is exactly 2x and is not a coincidence: the art was cut to
     * make this true (tools/sidebar_redesign/HUD-ART-SPEC.md). */
    zoom = (cw * 2 <= H6_CAMEO_W) ? 2 : 1;
    for (y = 0; y < H6_CAMEO_H; ++y)
    {
        int sy = y / zoom;
        if (sy >= chh) break;
        for (x = 0; x < H6_CAMEO_W; ++x)
        {
            int sx = x / zoom;
            unsigned char pi;
            unsigned char *d;
            if (sx >= cw) break;
            pi = cellpx[sy * cw + sx];
            if (!pi) continue;               /* index 0 is the hole, and the well shows */
            d = dst + ((long)y * H6_CAMEO_W + x) * 4;
            d[0] = cp->pal8[pi * 3 + 0];
            d[1] = cp->pal8[pi * 3 + 1];
            d[2] = cp->pal8[pi * 3 + 2];
            d[3] = 255;
        }
    }
    h->st.slot[slot].used = 1;
    h->st.slot[slot].progress = producing ? progress : -1;
    h->st.slot[slot].rgba = dst;
}

void t1_hud_scroll(T1_Hud *h, int column, int delta)
{
    int max;
    if (column < 0 || column >= H6_COLUMNS) return;
    max = h->count[column] - T1H_ROWS;
    if (max < 0) max = 0;
    h->top[column] += delta;
    if (h->top[column] < 0) h->top[column] = 0;
    if (h->top[column] > max) h->top[column] = max;
    h->dirty = 1;
}

void t1_hud_set_state(T1_Hud *h, const W98_Sidebar *sb, const void *dospack)
{
    const DB_Pack *cp = (const DB_Pack *)dospack;
    int c, r;
    if (!h->ok) return;

    h->st.credits = sb->credits;
    h->st.radar_active = sb->radar_active;
    /* The meter is a percentage of PRODUCED power that is spoken for, which is the
     * quantity the 1995 bar shows: a full green channel is a plant with nothing drawing
     * on it, and it goes red as the base catches up with its generators. */
    if (sb->power_produced > 0)
    {
        int lvl = 100 - (sb->power_drained * 100) / sb->power_produced;
        if (lvl < 0) lvl = 0;
        if (lvl > 100) lvl = 100;
        h->st.power_level = lvl;
    }
    else h->st.power_level = 0;

    for (c = 0; c < H6_COLUMNS; ++c)
    {
        h->count[c] = sb->count[c];
        if (h->top[c] > h->count[c] - T1H_ROWS) h->top[c] = h->count[c] - T1H_ROWS;
        if (h->top[c] < 0) h->top[c] = 0;
    }

    for (c = 0; c < H6_COLUMNS; ++c)
        for (r = 0; r < T1H_ROWS; ++r)
        {
            int slot = c + r * H6_COLUMNS;
            int idx  = h->top[c] + r;
            if (idx >= sb->count[c])
            {
                h->st.slot[slot].used = 0;
                h->st.slot[slot].rgba = 0;
                h->st.slot[slot].progress = -1;
                continue;
            }
            {
                const W98_Build *b = &sb->item[c][idx];
                int stage = (int)(b->progress * (float)(DB_STEP_COUNT - 1));
                compose_cameo(h, slot, cp, b->name,
                              b->constructing || b->completed, b->completed,
                              b->onhold, b->busy && !b->constructing, stage);
            }
        }
    h->dirty = 1;
}

/* ---------------------------------------------------------------------------------
 * The minimap.
 * --------------------------------------------------------------------------------- */
static void radar_px(unsigned char *rgba, int x, int y, int r, int g, int b)
{
    unsigned char *d;
    if (x < 0 || y < 0 || x >= H6_RADAR_W || y >= H6_RADAR_H) return;
    d = rgba + ((long)y * H6_RADAR_W + x) * 4;
    d[0] = (unsigned char)r; d[1] = (unsigned char)g; d[2] = (unsigned char)b; d[3] = 255;
}

static void radar_line(unsigned char *rgba, int x0, int y0, int x1, int y1,
                       int r, int g, int b)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int e = dx - dy, guard = 0;
    for (;;)
    {
        int e2;
        radar_px(rgba, x0, y0, r, g, b);
        if ((x0 == x1 && y0 == y1) || ++guard > 4096) break;
        e2 = e * 2;
        if (e2 > -dy) { e -= dy; x0 += sx; }
        if (e2 <  dx) { e += dx; y0 += sy; }
    }
}

void t1_hud_radar(T1_Hud *h, const T1_Terrain *terr, const W98_MapInfo *map,
                  const W98_Object *objs, int nobj,
                  const T1_Cam *cam, const T1_Screen *scr)
{
    int zoom, ox, oy, cx, cy, i;
    if (!h->ok || !h->cellrgb_ok || !map || map->cellw < 1) { h->st.radar_rgba = 0; return; }
    (void)terr;

    zoom = hud640_radar_zoom(map->cellw, map->cellh);
    if (zoom < 1) zoom = 1;
    /* Centred, never stretched: the minimap is a cell grid and a fractional step aliases
     * it into a mess. hud640.h says so and hud640_radar_zoom is the arithmetic. */
    ox = (H6_RADAR_W - map->cellw * zoom) / 2;
    oy = (H6_RADAR_H - map->cellh * zoom) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    /* Off-map is the HUD's own near-black, the same colour cnc_sidebar.h letterboxes the
     * bar with, so the surround reads as part of the chassis rather than as a hole. */
    {
        long n = (long)H6_RADAR_W * H6_RADAR_H, k;
        for (k = 0; k < n; ++k)
        {
            h->radar[k*4+0] = 8; h->radar[k*4+1] = 9; h->radar[k*4+2] = 11;
            h->radar[k*4+3] = 255;
        }
    }

    for (cy = 0; cy < map->cellh; ++cy)
        for (cx = 0; cx < map->cellw; ++cx)
        {
            int wx = map->cellx + cx, wy = map->celly + cy;
            const unsigned char *s;
            int px, py, dx, dy;
            if (wx < 0 || wx >= 64 || wy < 0 || wy >= 64) continue;
            s = h->cellrgb + ((long)wy * 64 + wx) * 3;
            px = ox + cx * zoom; py = oy + cy * zoom;
            for (dy = 0; dy < zoom; ++dy)
                for (dx = 0; dx < zoom; ++dx)
                    radar_px(h->radar, px + dx, py + dy, s[0], s[1], s[2]);
        }

    /* Pips. The 1995 radar paints a unit in its house's colour and a building over its
     * whole footprint, which is what makes a base legible at this size. */
    for (i = 0; i < nobj; ++i)
    {
        int r, g, b, fw, fh, dx, dy, isbuild;
        int wx, wy, px, py;
        if (objs[i].limbo) continue;
        if (strcmp(objs[i].kind, "TERRAIN") == 0) continue;
        isbuild = (strcmp(objs[i].kind, "BUILDING") == 0);
        if (strcmp(objs[i].house, "GoodGuy") == 0)      { r = 255; g = 214; b =  32; }
        else if (strcmp(objs[i].house, "BadGuy") == 0)  { r = 224; g =  48; b =  40; }
        else                                            { r = 170; g = 170; b = 170; }
        wx = objs[i].clx / 256 - map->cellx;
        wy = objs[i].cly / 256 - map->celly;
        fw = isbuild && objs[i].fw > 0 ? objs[i].fw : 1;
        fh = isbuild && objs[i].fh > 0 ? objs[i].fh : 1;
        px = ox + (wx - fw / 2) * zoom;
        py = oy + (wy - fh / 2) * zoom;
        for (dy = 0; dy < fh * zoom; ++dy)
            for (dx = 0; dx < fw * zoom; ++dx)
                radar_px(h->radar, px + dx, py + dy, r, g, b);
    }

    /* The view. Four screen corners cast onto the ground plane and joined, so the shape
     * is the TRAPEZOID the perspective camera actually sees rather than a rectangle that
     * would be a lie at this pitch. */
    if (cam && scr)
    {
        static const float sxs[4] = { 0.0f, (float)T1H_FIELD_W, (float)T1H_FIELD_W, 0.0f };
        static const float sys[4] = { 0.0f, 0.0f, 480.0f, 480.0f };
        int vx[4], vy[4], k, ok = 1;
        for (k = 0; k < 4; ++k)
        {
            float wx, wz;
            t1_screen_to_plane(cam, scr, sxs[k], sys[k], cam->at_y, &wx, &wz);
            if (!(wx > -1e5f && wx < 1e5f && wz > -1e5f && wz < 1e5f)) { ok = 0; break; }
            vx[k] = ox + (int)((wx - map->cellx) * zoom);
            vy[k] = oy + (int)((wz - map->celly) * zoom);
        }
        if (ok)
            for (k = 0; k < 4; ++k)
                radar_line(h->radar, vx[k], vy[k], vx[(k+1)&3], vy[(k+1)&3], 255, 255, 255);
    }

    h->st.radar_rgba = h->radar;
    h->rzoom = zoom; h->rox = ox; h->roy = oy;
    h->dirty = 1;
}

/* A click inside the radar surface, as a map cell. Returns 0 if it landed off the map,
 * which at any zoom above 1 is most of the surround. */
int t1_hud_radar_to_cell(const T1_Hud *h, const W98_MapInfo *map, int mx, int my,
                         int *cx, int *cy)
{
    int hx = mx - H6_BAR_X - H6_RADAR_X, hy = my - H6_RADAR_Y, ux, uy;
    if (!h->rzoom || !map) return 0;
    ux = (hx - h->rox) / h->rzoom;
    uy = (hy - h->roy) / h->rzoom;
    if (hx < h->rox || hy < h->roy) return 0;
    if (ux < 0 || uy < 0 || ux >= map->cellw || uy >= map->cellh) return 0;
    *cx = map->cellx + ux;
    *cy = map->celly + uy;
    return 1;
}

/* ---------------------------------------------------------------------------------
 * Hit test and hover. A direct port of sb_hit_h6() in game/cnc_sidebar.h, reading the
 * same generated layout, so the two HUDs cannot disagree about where a control is.
 * --------------------------------------------------------------------------------- */
T1_HudHit t1_hud_hit(int mx, int my, int *column, int *slot)
{
    int hx = mx - H6_BAR_X, hy = my, i, c, r;
    *column = -1;
    *slot = -1;

    if (my < H6_TAB_H && mx >= H6_OPT_X && mx < H6_OPT_X + H6_BAR_W) return T1H_OPTIONS;
    if (hx < 0 || hx >= H6_BAR_W || hy < 0 || hy >= H6_BAR_H) return T1H_NONE;

    if (hy >= H6_BTN_REPAIR_Y && hy < H6_BTN_REPAIR_Y + H6_BTN_REPAIR_H)
    {
        if (hx >= H6_BTN_REPAIR_X && hx < H6_BTN_REPAIR_X + H6_BTN_REPAIR_W) return T1H_REPAIR;
        if (hx >= H6_BTN_SELL_X   && hx < H6_BTN_SELL_X   + H6_BTN_SELL_W)   return T1H_SELL;
        if (hx >= H6_BTN_MAP_X    && hx < H6_BTN_MAP_X    + H6_BTN_MAP_W)    return T1H_MAP;
    }

    if (hx >= H6_RADAR_X && hx < H6_RADAR_X + H6_RADAR_W &&
        hy >= H6_RADAR_Y && hy < H6_RADAR_Y + H6_RADAR_H)
        return T1H_RADAR;

    {   /* four arrows on one row: up/down, up/down, one pair per column */
        int arrowY = hud640_arrow_y(T1H_ROWS);
        if (hy >= arrowY && hy < arrowY + H6_ARROW_UP_H)
            for (i = 0; i < 4; ++i)
                if (hx >= H6_ARROW_X[i] && hx < H6_ARROW_X[i] + H6_ARROW_UP_W)
                { *column = i / 2; return (i & 1) ? T1H_DOWN : T1H_UP; }
    }

    for (c = 0; c < H6_COLUMNS; ++c)
    {
        if (hx < H6_COL_X[c] || hx >= H6_COL_X[c] + H6_CELL_W) continue;
        for (r = 0; r < T1H_ROWS; ++r)
        {
            int ry = hud640_row_y(r);
            if (hy >= ry && hy < ry + H6_CELL_H) { *column = c; *slot = r; return T1H_ITEM; }
        }
    }
    return T1H_NONE;
}

void t1_hud_hover(T1_Hud *h, int mx, int my, int pressed)
{
    int col, slot, i;
    T1_HudHit hit = t1_hud_hit(mx, my, &col, &slot);
    if (!h->ok) return;
    /* Nothing has moved that the bar can see: leave it alone, because recomposing costs
     * more than the whole rest of the 2D layer put together. */
    if (hit == h->last_hit && col == h->last_col && slot == h->last_slot
        && pressed == h->last_pressed)
        return;
    h->last_hit = hit; h->last_col = col; h->last_slot = slot; h->last_pressed = pressed;

    /* 0 normal, 1 hover, 2 pressed, 3 active. Frame 0 is never blitted because the
     * chassis already carries the resting state; that is hud640.h's contract with
     * tools/sidebar_redesign/states.py, not a choice made here. */
    h->st.repair_frame = h->st.sell_frame = h->st.map_frame = 0;
    h->st.options_frame = 0;
    for (i = 0; i < 4; ++i) h->st.arrow_frame[i] = 0;

    switch (hit)
    {
    case T1H_REPAIR:  h->st.repair_frame  = pressed ? 2 : 1; break;
    case T1H_SELL:    h->st.sell_frame    = pressed ? 2 : 1; break;
    case T1H_MAP:     h->st.map_frame     = pressed ? 2 : 1; break;
    case T1H_OPTIONS: h->st.options_frame = pressed ? 2 : 1; break;
    case T1H_UP:      h->st.arrow_frame[col * 2 + 0] = pressed ? 2 : 1; break;
    case T1H_DOWN:    h->st.arrow_frame[col * 2 + 1] = pressed ? 2 : 1; break;
    default: break;
    }
    h->dirty = 1;
}

/* ---------------------------------------------------------------------------------
 * Compose, convert, upload. Only when something changed.
 * --------------------------------------------------------------------------------- */
int t1_hud_refresh(T1_Hud *h)
{
    if (!h->ok || !h->dirty) return 0;
    h->dirty = 0;

    h->st.rows = T1H_ROWS;
    hud640_draw_bar(h->bar, h->pack, &h->st);

    /* Cut at row 256, which is a texel boundary in both pages, so the two quads meet
     * with no seam and no half-texel fudge. */
    rgba_to_565(h->pageA, T1H_PAGE, h->bar, H6_BAR_W, H6_BAR_W, T1H_SPLIT);
    rgba_to_565(h->pageB, T1H_PAGE, h->bar + (long)T1H_SPLIT * H6_BAR_W * 4, H6_BAR_W,
                H6_BAR_W, H6_BAR_H - T1H_SPLIT);

    hud640_draw_tab(h->tab, h->pack, NULL, h->st.credits, 0);
    rgba_to_565(h->pageC, T1H_PAGE, h->tab, H6_BAR_W, H6_BAR_W, H6_TAB_H);
    hud640_draw_tab(h->tab, h->pack, "OPTIONS", 0, h->st.options_frame);
    rgba_to_565(h->pageC + (long)H6_TAB_H * T1H_PAGE, T1H_PAGE, h->tab, H6_BAR_W,
                H6_BAR_W, H6_TAB_H);

    t1_glide_reupload(&h->texA);
    t1_glide_reupload(&h->texB);
    t1_glide_reupload(&h->texC);
    return 1;
}

void t1_hud_draw(T1_Hud *h)
{
    if (!h->ok) return;
    t1_glide_ckey(0, 0);            /* the bar is opaque art; nothing in it is a hole */
    t1_glide_filter(0);             /* 1:1 texels to pixels: filtering can only blur it */

    t1_glide_quad((float)H6_BAR_X, 0.0f, (float)(H6_BAR_X + H6_BAR_W), (float)T1H_SPLIT,
                  0.0f, 0.0f, (float)H6_BAR_W, (float)T1H_SPLIT, &h->texA, 1.0f);
    t1_glide_quad((float)H6_BAR_X, (float)T1H_SPLIT,
                  (float)(H6_BAR_X + H6_BAR_W), (float)H6_BAR_H,
                  0.0f, 0.0f, (float)H6_BAR_W, (float)(H6_BAR_H - T1H_SPLIT),
                  &h->texB, 1.0f);

    /* CREDITS sits immediately left of the bar and OPTIONS is pinned to the left edge of
     * the screen, which is where the 1995 tab bar puts them (tab.cpp draws the plate at
     * x = 0 and again at width - Eva_Width). */
    t1_glide_quad((float)(H6_BAR_X - H6_BAR_W), 0.0f, (float)H6_BAR_X, (float)H6_TAB_H,
                  0.0f, 0.0f, (float)H6_BAR_W, (float)H6_TAB_H, &h->texC, 1.0f);
    t1_glide_quad((float)H6_OPT_X, 0.0f, (float)(H6_OPT_X + H6_BAR_W), (float)H6_TAB_H,
                  0.0f, (float)H6_TAB_H, (float)H6_BAR_W, (float)(H6_TAB_H * 2),
                  &h->texC, 1.0f);
}

/* The 1995 pointer, straight out of the DOS pack, into the aux page. Index 0 is the hole
 * and becomes magenta -- not black, because black is a real colour in this art (it is the
 * pointer's own outline) and keying on it would cut the outline away while still looking
 * almost right. Anything that does not fit the slot is clipped rather than refused: a
 * pointer one row short beats no pointer at all. */
void t1_hud_set_pointer(T1_Hud *h, const unsigned char *idx, int w, int hgt,
                        const unsigned char *pal8)
{
    int x, y;
    if (!h->ok || !idx || !pal8 || w <= 0 || hgt <= 0) return;
    for (y = 0; y < T1H_CUR_H; ++y)
        for (x = 0; x < T1H_CUR_W; ++x)
        {
            unsigned short c = rgb565(255, 0, 255);
            if (x < w && y < hgt)
            {
                unsigned char v = idx[(long)y * w + x];
                if (v) c = rgb565(pal8[v * 3], pal8[v * 3 + 1], pal8[v * 3 + 2]);
            }
            h->pageC[(T1H_CUR_Y + y) * T1H_PAGE + (T1H_CUR_X + x)] = c;
        }
    /* Straight back to the card: this runs once at startup, and the page's other users
     * only re-upload when something they own changes. */
    t1_glide_reupload(&h->texC);
}

void t1_hud_cursor(T1_Hud *h, int mx, int my)
{
    if (!h->ok) return;
    t1_glide_ckey(1, 0x00FF00FF);   /* magenta is the hole; black is the arrow's outline */
    t1_glide_quad((float)mx, (float)my,
                  (float)(mx + T1H_CUR_W), (float)(my + T1H_CUR_H),
                  (float)T1H_CUR_X, (float)T1H_CUR_Y,
                  (float)(T1H_CUR_X + T1H_CUR_W), (float)(T1H_CUR_Y + T1H_CUR_H),
                  &h->texC, 1.0f);
    t1_glide_ckey(0, 0);
}

void t1_hud_rect(T1_Hud *h, float x0, float y0, float x1, float y1)
{
    if (!h->ok) return;
    t1_glide_quad(x0, y0, x1, y1,
                  (float)(T1H_DOT_X + 1), (float)(T1H_DOT_Y + 1),
                  (float)(T1H_DOT_X + 3), (float)(T1H_DOT_Y + 3), &h->texC, 1.0f);
}
