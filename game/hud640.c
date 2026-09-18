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

/* How many whole segments of a level are lit. The fill is tiled segments, so this is the
   ONLY quantisation the meter has: 238 / 12 is 19 segments and 228 pixels of travel, ten
   short of the channel's own 238, and every extra row adds 48 = 4 * 12 so the shortfall
   stays ten whatever the bar height. A mark placed as a fraction of H6_METER_H would sit
   up to a whole segment away from the fill it is there to be compared against. */
static int h6_meter_on(int nseg, int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    return (nseg * level + 50) / 100;
}

int hud640_meter_segs(int rows)
{
    return hud640_meter_h(rows) / H6_METER_PITCH;
}

int hud640_meter_y(int rows, int level)
{
    const int meterH = hud640_meter_h(rows);
    return H6_METER_Y0 + meterH
         - h6_meter_on(meterH / H6_METER_PITCH, level) * H6_METER_PITCH;
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
    nseg = hud640_meter_segs(rows);
    on = h6_meter_on(nseg, st->power_level);
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

    /* THE DRAIN MARKER, and it goes on AFTER the colour rotation above so the rotation
       cannot eat it: the marker is blue, the one hue the fill never takes, so it reads
       the same on green, on amber and on red.

       It is placed by hud640_meter_y, not by a fraction of the channel height, because
       the fill is whole segments tiled up from the floor and only travels
       segs * H6_METER_PITCH of H6_METER_H. On its own lattice, drain equal to output puts
       the marker exactly on the fill's top edge, which is the whole point of it.
       `meter_drain` is an overlay the full width of the channel with its reading on its
       CENTRE row, so the blit is offset by half its height; keep that height odd.
       The art is the DOS gauge's own POWER shape redrawn at this scale, so both sidebars
       mark one level with one mark. The `mark` test stays because an older pack does not
       carry the asset, and such a pack must still draw a meter rather than nothing. */
    if (st->power_drain_level > 0) {
        const H6_Asset *mark = hud640_asset(p, "meter_drain");
        if (mark) {
            const int floory = H6_METER_Y0 + meterH;
            const int ceily  = floory - nseg * H6_METER_PITCH;
            int my = hud640_meter_y(rows, st->power_drain_level) - mark->h / 2;
            if (my < ceily) my = ceily;
            if (my > floory - mark->h) my = floory - mark->h;
            h6_blit(rgba, H6_BAR_W, barh, mark, 0, H6_METER_X, my);
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
    if (st->repair_frame >= 0)
        h6_blit(rgba, H6_BAR_W, barh, hud640_asset(p, "btn_repair"),
                st->repair_frame, H6_BTN_REPAIR_X, H6_BTN_REPAIR_Y);
    if (st->sell_frame >= 0)
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
        const char *asset = (strcmp(label, "SIDEBAR") == 0)  ? "tab_sidebar"
                          : (strcmp(label, "DATABASE") == 0) ? "tab_database"
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

/* ==================================================================================
 *  THE UNIT CARD. See the block in hud640.h.
 * ================================================================================== */

/* Proportional text out of a fixed-cell font strip. The strip carries ASCII 32..95,
   one glyph per cell against the cell's left edge, and the advance is the glyph's own
   ink width plus one: measured off the alpha the first time a font is used, then kept.
   Two fonts, so two small caches, keyed on the asset they were measured from. */
#define H6_FONT_FIRST 32
#define H6_FONT_LAST  95
#define H6_FONT_N     (H6_FONT_LAST - H6_FONT_FIRST + 1)

typedef struct { const H6_Asset *a; unsigned char adv[H6_FONT_N]; } H6_FontCache;
static H6_FontCache h6_fonts[2];

static const H6_FontCache *h6_font_measure(const H6_Asset *a)
{
    int i, c, x, y, ink;
    H6_FontCache *fc = NULL;
    if (!a || a->frames < H6_FONT_N) return NULL;
    for (i = 0; i < 2; i++) if (h6_fonts[i].a == a) return &h6_fonts[i];
    for (i = 0; i < 2; i++) if (!h6_fonts[i].a) { fc = &h6_fonts[i]; break; }
    if (!fc) fc = &h6_fonts[0];
    fc->a = a;
    for (c = 0; c < H6_FONT_N; c++) {
        ink = 0;
        for (y = 0; y < a->h; y++) {
            const unsigned char *row = a->rgba + ((size_t)y * a->w * a->frames + (size_t)c * a->w) * 4;
            for (x = 0; x < a->w; x++)
                if (row[x * 4 + 3] && x + 1 > ink) ink = x + 1;
        }
        /* A space has no ink; give it a third of a cell so words stay words. */
        fc->adv[c] = (unsigned char)(ink ? ink + 1 : a->w / 3);
    }
    return fc;
}

static int h6_text_width(const H6_Asset *a, const char *text)
{
    const H6_FontCache *fc = h6_font_measure(a);
    int w = 0;
    if (!fc || !text) return 0;
    for (; *text; text++) {
        int c = (unsigned char)*text;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < H6_FONT_FIRST || c > H6_FONT_LAST) continue;
        w += fc->adv[c - H6_FONT_FIRST];
    }
    return w > 0 ? w - 1 : 0;
}

int hud640_text_width(const H6_Pack *p, const char *font, const char *text)
{
    return h6_text_width(hud640_asset(p, font), text);
}

/* Print, upper-casing on the way: the font carries no lower case, which is also true
   of every word the 1995 sidebar prints. Returns the x after the last glyph. */
static int h6_print(unsigned char *dst, int dw, int dh, const H6_Asset *a,
                    const char *text, int x, int y)
{
    const H6_FontCache *fc = h6_font_measure(a);
    if (!fc || !text) return x;
    for (; *text; text++) {
        int c = (unsigned char)*text;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < H6_FONT_FIRST || c > H6_FONT_LAST) continue;
        h6_blit(dst, dw, dh, a, c - H6_FONT_FIRST, x, y);
        x += fc->adv[c - H6_FONT_FIRST];
    }
    return x;
}

int hud640_card_ok(const H6_Pack *p)
{
    static int said = 0;
    const H6_Asset *body = hud640_asset(p, "card_body");
    const H6_Asset *tab  = hud640_asset(p, "card_tab");
    const H6_Asset *mini = hud640_asset(p, "card_mini");
    const H6_Asset *big  = hud640_asset(p, "font_big");
    const H6_Asset *mid  = hud640_asset(p, "font_mid");
    const H6_Asset *sml  = hud640_asset(p, "font_small");
    if (body && tab && mini && big && mid && sml
        && body->w == H6_CARD_W && body->h == H6_CARD_BODY_H
        && tab->w == H6_CARD_TAB_W && tab->h == H6_CARD_TAB_H && tab->frames >= 4
        && mini->w == H6_CARD_MINI_W && mini->h == H6_CARD_MINI_H && mini->frames >= 2
        && big->frames >= H6_FONT_N && mid->frames >= H6_FONT_N && sml->frames >= H6_FONT_N)
        return 1;
    if (!said) {
        said = 1;
        fprintf(stderr, "HUD640|no unit card: this hud640.pack %s\n",
                body ? "carries card art of another size" : "predates the card art");
    }
    return 0;
}

/* A 64x48 cameo box at half size, 2x2 box filtered. The source is opaque art, so
   the average is over four full pixels and no alpha weighting is needed. */
static void h6_blit_half(unsigned char *dst, int dw, int dh,
                         const unsigned char *src, int dx, int dy)
{
    int x, y;
    if (!src) return;
    for (y = 0; y < H6_CAMEO_H / 2; y++) {
        const int ty = dy + y;
        if (ty < 0 || ty >= dh) continue;
        for (x = 0; x < H6_CAMEO_W / 2; x++) {
            const int tx = dx + x;
            const unsigned char *s0 = src + ((size_t)(y * 2) * H6_CAMEO_W + (size_t)(x * 2)) * 4;
            const unsigned char *s1 = s0 + (size_t)H6_CAMEO_W * 4;
            unsigned char *d;
            if (tx < 0 || tx >= dw) continue;
            d = dst + ((size_t)ty * dw + tx) * 4;
            d[0] = (unsigned char)((s0[0] + s0[4] + s1[0] + s1[4]) / 4);
            d[1] = (unsigned char)((s0[1] + s0[5] + s1[1] + s1[5]) / 4);
            d[2] = (unsigned char)((s0[2] + s0[6] + s1[2] + s1[6]) / 4);
            d[3] = 255;
        }
    }
}

/* The power meter's own colour rotation, applied to a rectangle of the card: green is
   left alone, amber lifts red to the green, red drops the green away. One rule for
   the meter and the health bar, so a base browning out and a tank on its last legs
   read as the same warning. */
static void h6_rotate_green(unsigned char *rgba, int dw, int dh,
                            int x0, int y0, int x1, int y1, int color)
{
    int px, py;
    if (color <= 0) return;
    for (py = (y0 < 0 ? 0 : y0); py < y1 && py < dh; py++) {
        for (px = (x0 < 0 ? 0 : x0); px < x1 && px < dw; px++) {
            unsigned char *q = rgba + ((size_t)py * dw + px) * 4;
            if (!q[3]) continue;
            if (q[0] < q[1]) q[0] = q[1];
            if (color >= 2) {
                q[1] = (unsigned char)(q[1] / 4);
                q[2] = (unsigned char)(q[2] / 4);
            }
        }
    }
}

void hud640_draw_card(unsigned char *rgba, const H6_Pack *p, const H6_Card *c)
{
    const H6_Asset *body, *tab, *mini, *mid, *sml, *frame, *lit, *unlit;
    char buf[32];
    int i, x, y, wx, wy;
    const int W = H6_CARD_W, H = H6_CARD_H;

    if (!rgba || !p || !c) return;
    if (!hud640_card_ok(p)) return;
    body  = hud640_asset(p, "card_body");
    tab   = hud640_asset(p, "card_tab");
    mini  = hud640_asset(p, "card_mini");
    mid   = hud640_asset(p, "font_mid");
    sml   = hud640_asset(p, "font_small");
    /* The card's own frame and segments where the pack carries them: the ring cut to
     * the well's octagon, and a segment closed at the top. An older pack falls back
     * to the sidebar's pieces, which are the same art with square corners and an
     * open top. */
    frame = hud640_asset(p, "card_frame");
    if (!frame) frame = hud640_asset(p, "cell_frame");
    lit   = hud640_asset(p, "card_seg_lit");
    unlit = hud640_asset(p, "card_seg_unlit");
    if (!lit || !unlit) {
        lit   = hud640_asset(p, "meter_lit");
        unlit = hud640_asset(p, "meter_unlit");
    }
    memset(rgba, 0, (size_t)W * H * 4);

    /* 1. the bezel, whose window is opaque dark: everything else goes on after it */
    h6_blit(rgba, W, H, body, 0, 0, 0);
    wx = H6_CARD_WIN_X;
    wy = H6_CARD_WIN_Y;

    /* 2. the main unit's well: cameo, then the frame ring back over it, exactly as a
     *    build slot is drawn, at the same offsets (H6_CAMEO_DX/DY, H6_FRAME_DX/DY) */
    x = wx + H6_CARD_CELL_X;
    y = wy + H6_CARD_CELL_Y;
    if (c->cameo) {
        h6_blit_cameo(rgba, W, H, c->cameo, H6_CAMEO_W, H6_CAMEO_H,
                      x + H6_CAMEO_DX, y + H6_CAMEO_DY, x, y, H6_CELL_W, H6_CELL_H);
    } else if (c->code[0]) {
        /* No art for this type: its code in the well, so the well is never blank. */
        const int tw = h6_text_width(sml, c->code);
        h6_print(rgba, W, H, sml, c->code, x + (H6_CELL_W - tw) / 2,
                 y + (H6_CELL_H - sml->h) / 2);
    }
    if (frame) h6_blit(rgba, W, H, frame, 0, x + H6_FRAME_DX, y + H6_FRAME_DY);

    /* 3. the name across the top of the window, then health and damage beside the
     *    well. The name is fitted: a long one loses its tail rather than running into
     *    the bezel, and the size of a multiple selection follows it. */
    {
        const int avail = H6_CARD_WIN_W - 2 * H6_CARD_NAME_X;
        char name[48];
        int n;
        if (c->count > 1) sprintf(name, "%.32s  x%d", c->name, c->count);
        else              sprintf(name, "%.40s", c->name);
        /* The middle size first; a name that will not fit at that size drops to the
           small one whole rather than losing its tail, so "Mobile Construction Yard"
           stays a name and not a fragment. Only then is anything cut. */
        const H6_Asset *nf = (h6_text_width(mid, name) > avail) ? sml : mid;
        n = (int)strlen(name);
        while (n > 0 && h6_text_width(nf, name) > avail) name[--n] = 0;
        h6_print(rgba, W, H, nf, name, wx + H6_CARD_NAME_X,
                 wy + H6_CARD_NAME_Y + (mid->h - nf->h) / 2);
    }
    x = wx + H6_CARD_TEXT_X;
    if (lit && unlit && c->maxstr > 0) {
        /* Whole segments, like the power meter: lit from the left, unlit after. The
           rounding is UP so a unit that still has any health at all shows one lit
           segment, which is the 1995 bar's one-pixel sliver in this vocabulary. */
        const int segw = lit->w;
        int on = (c->str * H6_CARD_BAR_SEGS + c->maxstr - 1) / c->maxstr;
        if (on > H6_CARD_BAR_SEGS) on = H6_CARD_BAR_SEGS;
        if (c->str <= 0) on = 0;
        for (i = 0; i < H6_CARD_BAR_SEGS; i++)
            h6_blit(rgba, W, H, i < on ? lit : unlit, 0,
                    x + i * segw, wy + H6_CARD_BAR_Y);
        h6_rotate_green(rgba, W, H, x, wy + H6_CARD_BAR_Y,
                        x + on * segw, wy + H6_CARD_BAR_Y + lit->h, c->health_color);
    }
    sprintf(buf, "%d/%d", c->str < 0 ? 0 : c->str, c->maxstr < 0 ? 0 : c->maxstr);
    h6_print(rgba, W, H, sml, buf, x, wy + H6_CARD_NUM_Y);
    if (c->damage >= 0) sprintf(buf, "DMG %d", c->damage);
    else                strcpy(buf, "UNARMED");
    h6_print(rgba, W, H, sml, buf, x + 78, wy + H6_CARD_NUM_Y);
    (void)hud640_asset(p, "font_big");

    /* 4. the rest of the selection: a row of smaller wells, filled left to right, the
     *    last one carrying "+N" when the row is too short for the selection */
    for (i = 0; i < H6_CARD_MINIS && i < c->nmini; i++) {
        const H6_CardMini *m = &c->mini[i];
        x = wx + H6_CARD_MINI_X + i * H6_CARD_MINI_PITCH;
        y = wy + H6_CARD_MINI_Y;
        h6_blit(rgba, W, H, mini, m->frame, x, y);
        if (c->overflow > 0 && i == c->nmini - 1) {
            int tw;
            sprintf(buf, "+%d", c->overflow);
            tw = h6_text_width(sml, buf);
            h6_print(rgba, W, H, sml, buf, x + (H6_CARD_MINI_W - tw) / 2,
                     y + (H6_CARD_MINI_H - sml->h) / 2);
        } else if (m->rgba) {
            h6_blit_half(rgba, W, H, m->rgba, x + 2, y + 2);
        }
    }

    /* 5. the ten control-group tabs, keyed the way the keyboard is: 1..9 then 0 */
    for (i = 0; i < H6_CARD_TABS; i++) {
        int f = c->group_frame[i];
        x = i * H6_CARD_TAB_W;
        y = H6_CARD_BODY_H;
        if (f < 0) f = 0;
        if (c->group_count[i] <= 0) f = 0;
        h6_blit(rgba, W, H, tab, f, x, y);
        /* The key in the top-left corner, clear of the tab's own left bevel (three
         * columns) and top lip, the count centred on the row below it. */
        sprintf(buf, "%d", (i + 1) % 10);
        h6_print(rgba, W, H, sml, buf, x + 5, y + 4);
        if (c->group_count[i] > 0) {
            int tw;
            sprintf(buf, "%d", c->group_count[i] > 99 ? 99 : c->group_count[i]);
            tw = h6_text_width(sml, buf);
            h6_print(rgba, W, H, sml, buf, x + (H6_CARD_TAB_W - tw) / 2,
                     y + H6_CARD_TAB_H - 3 - sml->h);
        }
    }
}
