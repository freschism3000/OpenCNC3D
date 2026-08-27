/* ------------------------------------------------------------------------------------
 * hud640.c -- the 640x480 sidebar HUD.  Plain C89: no GL, no SDL, no libc beyond
 * stdio/stdlib/string, so the Win98 build compiles it unchanged.
 * ---------------------------------------------------------------------------------- */
#include "hud640.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

H6_Pack *hud640_load(const char *path, char *err, int errlen)
{
    FILE *fh; long size; unsigned char *blob; const unsigned char *p, *end;
    H6_Pack *pk; unsigned int i, n;

    fh = fopen(path, "rb");
    if (!fh) { if (err) snprintf(err, (size_t)errlen, "cannot open %s", path); return NULL; }
    fseek(fh, 0, SEEK_END); size = ftell(fh); fseek(fh, 0, SEEK_SET);
    blob = (unsigned char *)malloc((size_t)size);
    if (!blob || fread(blob, 1, (size_t)size, fh) != (size_t)size) {
        fclose(fh); free(blob);
        if (err) snprintf(err, (size_t)errlen, "short read on %s", path);
        return NULL;
    }
    fclose(fh);

    if (size < 16 || memcmp(blob, "CNCHUD01", 8) != 0) {
        if (err) snprintf(err, (size_t)errlen, "bad magic (expected CNCHUD01)");
        free(blob); return NULL;
    }
    pk = (H6_Pack *)calloc(1, sizeof(H6_Pack));
    pk->blob = blob; pk->blobsize = size;
    p = blob + 8;
    if (rd32(p) != 1) { if (err) snprintf(err, (size_t)errlen, "unsupported version"); goto bad; }
    n = rd32(p + 4);
    if (n > H6_MAX_ASSETS) { if (err) snprintf(err, (size_t)errlen, "too many assets"); goto bad; }
    p += 8; end = blob + size;

    for (i = 0; i < n; i++) {
        long need;
        if (p + H6_NAME_LEN + 12 > end) { if (err) snprintf(err, (size_t)errlen, "truncated table"); goto bad; }
        memcpy(pk->assets[i].name, p, H6_NAME_LEN);
        pk->assets[i].name[H6_NAME_LEN] = 0;
        p += H6_NAME_LEN;
        pk->assets[i].w      = (int)rd32(p);
        pk->assets[i].h      = (int)rd32(p + 4);
        pk->assets[i].frames = (int)rd32(p + 8);
        p += 12;
        need = (long)pk->assets[i].w * pk->assets[i].h * pk->assets[i].frames * 4;
        if (need <= 0 || p + need > end) { if (err) snprintf(err, (size_t)errlen, "asset runs past end"); goto bad; }
        pk->assets[i].rgba = p;
        p += need;
    }
    pk->nassets = (int)n;
    return pk;
bad:
    hud640_free(pk);
    return NULL;
}

void hud640_free(H6_Pack *p)
{
    if (!p) return;
    free(p->blob);
    free(p);
}

const H6_Asset *hud640_asset(const H6_Pack *p, const char *name)
{
    int i;
    if (!p) return NULL;
    for (i = 0; i < p->nassets; i++)
        if (strcmp(p->assets[i].name, name) == 0) return &p->assets[i];
    return NULL;
}

/* Source-over alpha blit of one frame into an RGBA destination. Clipped. */
static void h6_blit(unsigned char *dst, int dw, int dh,
                    const H6_Asset *a, int frame, int dx, int dy)
{
    int x, y, fw, fh;
    const unsigned char *src;
    if (!a) return;
    if (frame < 0) frame = 0;
    if (frame >= a->frames) frame = a->frames - 1;
    fw = a->w; fh = a->h;
    /* frames lie left to right inside one row-major image of width fw*frames */
    for (y = 0; y < fh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= dh) continue;
        src = a->rgba + ((size_t)y * (size_t)fw * a->frames + (size_t)frame * fw) * 4;
        for (x = 0; x < fw; x++) {
            int tx = dx + x;
            unsigned int sa;
            unsigned char *d;
            if (tx < 0 || tx >= dw) continue;
            sa = src[x * 4 + 3];
            if (!sa) continue;
            d = dst + ((size_t)ty * dw + tx) * 4;
            if (sa == 255) {
                d[0] = src[x*4]; d[1] = src[x*4+1]; d[2] = src[x*4+2]; d[3] = 255;
            } else {
                unsigned int ia = 255u - sa;
                d[0] = (unsigned char)((src[x*4]   * sa + d[0] * ia) / 255u);
                d[1] = (unsigned char)((src[x*4+1] * sa + d[1] * ia) / 255u);
                d[2] = (unsigned char)((src[x*4+2] * sa + d[2] * ia) / 255u);
                d[3] = (unsigned char)(sa + (d[3] * ia) / 255u);
            }
        }
    }
}

/* The cameo, clipped to the well's chamfered corners. Same as h6_blit_raw except that a
   pixel is dropped when it falls in one of the four 45-degree cuts, tested in CELL
   coordinates (cellx/celly) rather than the cameo's own, because the cameo box is bigger
   than the well and hangs over it. See H6_CHAMFER in hud640.h. */
static void h6_blit_cameo(unsigned char *dst, int dw, int dh,
                          const unsigned char *src, int sw, int sh, int dx, int dy,
                          int cellx, int celly, int cellw, int cellh)
{
    int x, y;
    if (!src) return;
    for (y = 0; y < sh; y++) {
        const int ty = dy + y;
        const int cy = ty - celly;
        if (ty < 0 || ty >= dh) continue;
        for (x = 0; x < sw; x++) {
            const int tx = dx + x;
            const int cx = tx - cellx;
            unsigned char *d;
            if (tx < 0 || tx >= dw) continue;
            /* Outside the well entirely, or inside one of its four cut corners. The
               frame ring covers the straight edges, so only the corners need this, but
               testing the whole rectangle costs nothing and cannot get out of step. */
            if (hud640_cell_frame_on &&
                (cx < 0 || cy < 0 || cx >= cellw || cy >= cellh)) continue;
            if (hud640_cell_frame_on) {
                if (cx + cy < H6_CHAMFER) continue;
                if ((cellw - 1 - cx) + cy < H6_CHAMFER) continue;
                if (cx + (cellh - 1 - cy) < H6_CHAMFER) continue;
                if ((cellw - 1 - cx) + (cellh - 1 - cy) < H6_CHAMFER) continue;
            }
            d = dst + ((size_t)ty * dw + tx) * 4;
            d[0] = src[(y*sw+x)*4]; d[1] = src[(y*sw+x)*4+1];
            d[2] = src[(y*sw+x)*4+2]; d[3] = 255;
        }
    }
}

/* Raw RGBA rectangle, no frames, used for the caller's minimap. */
static void h6_blit_raw(unsigned char *dst, int dw, int dh,
                        const unsigned char *src, int sw, int sh, int dx, int dy)
{
    int x, y;
    if (!src) return;
    for (y = 0; y < sh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= dh) continue;
        for (x = 0; x < sw; x++) {
            int tx = dx + x;
            unsigned char *d;
            if (tx < 0 || tx >= dw) continue;
            d = dst + ((size_t)ty * dw + tx) * 4;
            d[0] = src[(y*sw+x)*4]; d[1] = src[(y*sw+x)*4+1];
            d[2] = src[(y*sw+x)*4+2]; d[3] = 255;
        }
    }
}

/* ---- the adaptive row block. See the long note in hud640.h. ---------------------- */

/* ONE SWITCH for the whole cameo treatment -- the frame ring put back on top AND the
   45-degree corner cut -- because they are one change: the cameo goes inside the well
   instead of over it. --nocellframe draws it the old way, which is what lets gate G41
   compare two real renders instead of testing a threshold somebody guessed. */
int hud640_cell_frame_on = 1;

int hud640_check_layout(void)
{
    if (H6_ROW_Y[4] - H6_ROW_Y[3] == H6_ROW_PITCH &&
        H6_ROW_Y[4] == H6_SPLIT_Y && H6_ROW_Y[3] == H6_BAND_Y)
        return 1;
    /* LOUD, because the failure is invisible otherwise: the extra wells would simply
       land half a row out and look like an art problem rather than a stale constant. */
    fprintf(stderr,
            "HUD640|layout drift: derived pitch/split/band %d/%d/%d do not match the "
            "baked table %d/%d/%d. Extra rows disabled; re-derive them in hud640.h.\n",
            H6_ROW_PITCH, H6_SPLIT_Y, H6_BAND_Y,
            H6_ROW_Y[4] - H6_ROW_Y[3], H6_ROW_Y[4], H6_ROW_Y[3]);
    return 0;
}

static int h6_clamp_rows(int rows)
{
    if (rows < H6_ROWS) rows = H6_ROWS;
    if (rows > H6_MAX_ROWS) rows = H6_MAX_ROWS;
    return rows;
}

int hud640_row_y(int row)
{
    if (row < 0) row = 0;
    if (row < 4) return H6_ROW_Y[row];
    return H6_SPLIT_Y + (row - 4) * H6_ROW_PITCH;
}

int hud640_bar_h(int rows)
{
    return H6_BAR_H + (h6_clamp_rows(rows) - H6_ROWS) * H6_ROW_PITCH;
}

int hud640_arrow_y(int rows)
{
    return H6_ARROW_Y + (h6_clamp_rows(rows) - H6_ROWS) * H6_ROW_PITCH;
}

int hud640_meter_h(int rows)
{
    return H6_METER_H + (h6_clamp_rows(rows) - H6_ROWS) * H6_ROW_PITCH;
}

int hud640_rows_for(int avail)
{
    int rows;
    if (!hud640_check_layout()) return H6_ROWS;
    if (avail <= H6_BAR_H) return H6_ROWS;
    rows = H6_ROWS + (avail - H6_BAR_H) / H6_ROW_PITCH;
    return h6_clamp_rows(rows);
}

/* One horizontal band of an asset's frame 0, copied straight down. Used only for the
   chassis, which is the opaque base layer, so this is a copy rather than a composite:
   the band is inserted BEFORE anything else lands on the bar. */
static void h6_blit_band(unsigned char *dst, int dw, int dh,
                         const H6_Asset *a, int srcY, int h, int dstY)
{
    int x, y;
    if (!a || h <= 0) return;
    for (y = 0; y < h; y++) {
        const int sy = srcY + y, ty = dstY + y;
        const unsigned char *src;
        if (sy < 0 || sy >= a->h || ty < 0 || ty >= dh) continue;
        src = a->rgba + ((size_t)sy * (size_t)a->w * a->frames) * 4;
        for (x = 0; x < dw && x < a->w; x++) {
            unsigned char *d = dst + ((size_t)ty * dw + x) * 4;
            d[0] = src[x*4]; d[1] = src[x*4+1]; d[2] = src[x*4+2]; d[3] = src[x*4+3];
        }
    }
}

int hud640_radar_zoom(int map_w, int map_h)
{
    int zx, zy, z;
    if (map_w <= 0 || map_h <= 0) return 1;
    zx = H6_RADAR_W / map_w;
    zy = H6_RADAR_H / map_h;
    z = zx < zy ? zx : zy;
    return z < 1 ? 1 : z;
}

float hud640_radar_zoomf(int map_w, int map_h)
{
    /* The integer zoom above floors at 1 px/cell, so a map taller than the 136x120
       surface silently loses rows -- a 128-tall map its bottom eight. When the map
       fits at some whole zoom, the whole zoom is kept (the chunky-cell radar look is
       the cartridge's); only when even 1:1 overflows does the scale drop below one,
       to the largest fraction that shows the WHOLE map. The plotter downsamples by
       nearest cell at that scale. */
    int z;
    float fx, fy;
    if (map_w <= 0 || map_h <= 0) return 1.0f;
    z = hud640_radar_zoom(map_w, map_h);
    if (map_w * z <= H6_RADAR_W && map_h * z <= H6_RADAR_H) return (float)z;
    fx = (float)H6_RADAR_W / (float)map_w;
    fy = (float)H6_RADAR_H / (float)map_h;
    return fx < fy ? fx : fy;
}

void hud640_draw_bar(unsigned char *rgba, const H6_Pack *p, const H6_State *st)
{
    const H6_Asset *chassis, *well, *lit, *radaroff, *frame;
    (void)0;
    int i, k, nseg, on, e;
    int rows, barh, extra, arrowY, meterH;

    if (!rgba || !p || !st) return;
    rows  = h6_clamp_rows(st->rows > 0 ? st->rows : H6_ROWS);
    if (rows > H6_ROWS && !hud640_check_layout()) rows = H6_ROWS;
    extra = rows - H6_ROWS;
    barh  = hud640_bar_h(rows);
    arrowY = hud640_arrow_y(rows);
    meterH = hud640_meter_h(rows);
    memset(rgba, 0, (size_t)H6_BAR_W * barh * 4);

    /* 1. the chassis, in three pieces when the bar has been grown. Everything above
     *    H6_SPLIT_Y is the delivered art untouched; then `extra` copies of one row band;
     *    then everything from the split down, slid below them. With extra == 0 the
     *    second piece is empty and the first and third are the whole delivered bitmap,
     *    which is byte for byte what the single blit used to produce. */
    chassis = hud640_asset(p, "chassis");
    frame   = hud640_asset(p, "cell_frame");   /* baked long ago, drawn for the first
                                                  time now; see hud640.h */
    if (extra == 0) {
        h6_blit(rgba, H6_BAR_W, barh, chassis, 0, 0, 0);
    } else {
        h6_blit_band(rgba, H6_BAR_W, barh, chassis, 0, H6_SPLIT_Y, 0);
        for (e = 0; e < extra; e++)
            h6_blit_band(rgba, H6_BAR_W, barh, chassis, H6_BAND_Y, H6_ROW_PITCH,
                         H6_SPLIT_Y + e * H6_ROW_PITCH);
        h6_blit_band(rgba, H6_BAR_W, barh, chassis, H6_SPLIT_Y,
                     H6_BAR_H - H6_SPLIT_Y, H6_SPLIT_Y + extra * H6_ROW_PITCH);
    }

    /* 2. the radar. The bezel opening in the chassis is OPAQUE dark, not a hole, so the
     *    contents go on AFTER the chassis or they are hidden. */
    if (st->radar_active && st->radar_rgba)
        h6_blit_raw(rgba, H6_BAR_W, barh, st->radar_rgba,
                    H6_RADAR_W, H6_RADAR_H, H6_RADAR_X, H6_RADAR_Y);
    else {
        /* Faction emblem when the radar is down. Nod's is a separate asset, not a
         * recolour: the two shields are different shapes. */
        radaroff = hud640_asset(p, st->nod ? "radar_off_nod" : "radar_off");
        if (!radaroff) radaroff = hud640_asset(p, "radar_off");
        h6_blit(rgba, H6_BAR_W, barh, radaroff, 0, H6_RADAR_X, H6_RADAR_Y);
    }

    /* 3. the power meter: one lit segment tiled upward from the channel floor. The
     *    delivered art only carries green over part of the channel, so tiling a single
     *    segment is what lets 0% and 100% both render. */
    lit = hud640_asset(p, "meter_lit");
    nseg = meterH / H6_METER_PITCH;
    on = (nseg * (st->power_level < 0 ? 0 : st->power_level > 100 ? 100 : st->power_level) + 50) / 100;
    for (k = 0; k < on; k++) {
        int y = H6_METER_Y0 + meterH - (k + 1) * H6_METER_PITCH;
        if (y < H6_METER_Y0) break;
        h6_blit(rgba, H6_BAR_W, barh, lit, 0, H6_METER_X, y);
    }
    /* THE OVERDRAW WARNING. Until v0.5.9 this meter was one green channel whatever the
       drain was doing, so the art HUD could not tell the player his base was browning
       out while the DOS bar beside it could. The rule is 1995's own watt test, handed in
       as st->power_color, and it is applied by ROTATING the delivered green towards
       amber and red rather than by blitting a second asset: the art carries exactly one
       segment for this channel (meter_lit) and no coloured variants, so a second asset
       would have to be authored here, in the renderer, which is not this file's job.
       Green is left untouched, so a healthy base is byte-identical to before. */
    if (st->power_color > 0 && on > 0) {
        const int y1 = H6_METER_Y0 + meterH;
        const int y0 = y1 - on * H6_METER_PITCH;
        int px, py;
        for (py = (y0 < H6_METER_Y0 ? H6_METER_Y0 : y0); py < y1 && py < barh; py++) {
            for (px = H6_METER_X; px < H6_METER_X + H6_METER_W && px < H6_BAR_W; px++) {
                unsigned char *q = rgba + ((size_t)py * H6_BAR_W + px) * 4;
                if (!q[3]) continue;                  /* untouched channel pixels */
                if (st->power_color == 1) {           /* amber: lift red to the green */
                    if (q[0] < q[1]) q[0] = q[1];
                } else {                              /* red: drop the green away    */
                    if (q[0] < q[1]) q[0] = q[1];
                    q[1] = (unsigned char)(q[1] / 4);
                    q[2] = (unsigned char)(q[2] / 4);
                }
            }
        }
    }

    /* 4. the build slots. The chassis ALREADY carries all ten empty wells, so an empty
     *    slot needs nothing drawn: painting cell_well over it would stamp row 0's well
     *    art onto every row, and the wells are not pixel-identical row to row. The
     *    cell_well asset exists only to ERASE a cameo when a slot empties without a
     *    full chassis redraw. A filled slot gets its cameo, which the caller supplies
     *    already clipped to the well's chamfer. */
    (void)well;
    for (i = 0; i < H6_COLUMNS * rows; i++) {
        int cx, cy;
        if (!st->slot[i].used || !st->slot[i].rgba) continue;
        cx = H6_COL_X[i % H6_COLUMNS];
        cy = hud640_row_y(i / H6_COLUMNS);
        h6_blit_cameo(rgba, H6_BAR_W, barh, st->slot[i].rgba,
                      H6_CAMEO_W, H6_CAMEO_H, cx + H6_CAMEO_DX, cy + H6_CAMEO_DY,
                      cx, cy, H6_CELL_W, H6_CELL_H);
        /* ...then the frame back on top, so the picture sits INSIDE the well instead of
         * over its bevel and chamfers. Only for a FILLED slot: an empty one is still
         * showing the chassis's own frame, untouched, and stamping this over it would
         * replace one row's authored bevel with another's. See hud640.h. */
        if (frame && hud640_cell_frame_on)
            h6_blit(rgba, H6_BAR_W, barh, frame, 0,
                    cx + H6_FRAME_DX, cy + H6_FRAME_DY);
    }

    /* 5. the buttons and arrows, by FRAME index. This is db_draw_shape's mechanism and
     *    the reason it ports: a frame blit is all the Voodoo has to do.
     *
     *    FRAME 0 IS DRAWN, and that is a change. It used to be skipped, because the
     *    strips were cut FROM the chassis at one position: stamping frame 0 back down was
     *    a no-op at best, and for the arrows it painted arrow 1's pixels over arrow 3.
     *    The strips now come from an external sheet, so frame 0 is a real resting plate
     *    belonging to the same design as frames 1..3, and it is position-independent.
     *    Skipping it left the chassis's OLD plate showing at rest and the new one on
     *    hover, so the button visibly changed shape under the pointer. */
    h6_blit(rgba, H6_BAR_W, barh, hud640_asset(p, "btn_repair"),
            st->repair_frame, H6_BTN_REPAIR_X, H6_BTN_REPAIR_Y);
    h6_blit(rgba, H6_BAR_W, barh, hud640_asset(p, "btn_sell"),
            st->sell_frame, H6_BTN_SELL_X, H6_BTN_SELL_Y);
    h6_blit(rgba, H6_BAR_W, barh, hud640_asset(p, "btn_map"),
            st->map_frame, H6_BTN_MAP_X, H6_BTN_MAP_Y);
    for (i = 0; i < 4; i++) {
        const H6_Asset *a;
        a = hud640_asset(p, (i & 1) ? "arrow_down" : "arrow_up");
        h6_blit(rgba, H6_BAR_W, barh, a, st->arrow_frame[i], H6_ARROW_X[i], arrowY);
    }
}

void hud640_draw_tab(unsigned char *rgba, const H6_Pack *p, const char *label,
                     int value, int frame)
{
    const H6_Asset *plate, *digits;
    char buf[16];
    int n, i, cell, x;

    if (!rgba || !p) return;
    memset(rgba, 0, (size_t)H6_BAR_W * H6_TAB_H * 4);

    if (label) {
        /* A pre-lettered plate, chosen by the label. It USED to ignore the label
           entirely and always blit tab_options, which was fine while OPTIONS was the
           only word on screen -- the SIDEBAR word was baked into the chassis art. It is
           its own plate now, lettered from the same GRAD6FNT at the same size, because a
           word baked into the bar cannot survive the bar sliding away from under it. */
        const char *asset = (strcmp(label, "SIDEBAR") == 0) ? "tab_sidebar"
                                                            : "tab_options";
        h6_blit(rgba, H6_BAR_W, H6_TAB_H, hud640_asset(p, asset), frame, 0, 0);
        return;
    }

    plate  = hud640_asset(p, "tab_plate");
    digits = hud640_asset(p, "digits");
    h6_blit(rgba, H6_BAR_W, H6_TAB_H, plate, frame, 0, 0);
    if (!digits) return;

    if (value < 0) value = 0;
    if (value > 999999) value = 999999;
    sprintf(buf, "%d", value);
    n = (int)strlen(buf);
    cell = digits->w;
    x = (H6_BAR_W - n * cell) / 2;
    for (i = 0; i < n; i++)
        h6_blit(rgba, H6_BAR_W, H6_TAB_H, digits, buf[i] - '0', x + i * cell,
                (H6_TAB_H - digits->h) / 2);
}
