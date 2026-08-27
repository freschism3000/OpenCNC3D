/* t1_place.c -- see t1_place.h. */

#include <string.h>
#include "t1_place.h"
#include "t1_glide.h"

/* The engine's CELL numbering, which the occupy list is expressed in. This build defines
 * MEGAMAPS, so it is 128 wide; at 64 a 2x2 footprint decodes as a 66-cell horizontal
 * line, which looks like a renderer bug rather than an arithmetic one. */
#define T1P_CELL_STRIDE 128

int t1_place_begin(T1_Place *p, const T1_MeshBank *b, const W98_Build *item)
{
    memset(p, 0, sizeof *p);
    p->item = *item;
    p->hoverx = p->hovery = -1;
    p->mesh = t1_mesh_for_type(b, item->name);
    wb_place_origin(&p->gridx, &p->gridy);
    p->active = 1;
    return 1;
}

void t1_place_end(T1_Place *p) { p->active = 0; }

int t1_place_legal(const T1_Place *p, int cellx, int celly)
{
    int k, origin;
    if (!p->active) return 0;
    if (cellx < 0 || cellx > 63 || celly < 0 || celly > 63) return 0;
    if (!p->prox[celly * 64 + cellx]) return 0;
    /* Proximity is answered for the ORIGIN; every occupied cell still has to be clear,
     * and the engine answers that per cell rather than per building. */
    origin = celly * T1P_CELL_STRIDE + cellx;
    for (k = 0; k < p->item.noccupy; ++k)
    {
        int c = origin + p->item.occupy[k];
        int cx = c % T1P_CELL_STRIDE, cy = c / T1P_CELL_STRIDE;
        if (cx < 0 || cx > 63 || cy < 0 || cy > 63) return 0;
        if (!p->clear[cy * 64 + cx]) return 0;
    }
    return 1;
}

void t1_place_update(T1_Place *p, float wx, float wz)
{
    int cx, cz;
    if (!p->active) return;
    /* The engine's verdict is re-read every frame rather than cached: it changes as units
     * drive across the site, and a stale grid that says yes is worse than no grid at all.
     * A false return means the engine has left placement mode, which happens when the
     * building is placed or cancelled from anywhere. */
    if (!wb_placement(p->prox, p->clear)) { p->active = 0; return; }
    cx = (int)wx; cz = (int)wz;
    if (wx < 0.0f || wz < 0.0f || cx > 63 || cz > 63) { p->hoverx = p->hovery = -1; p->legal = 0; return; }
    p->hoverx = cx;
    p->hovery = cz;
    p->legal = t1_place_legal(p, cx, cz);
}

int t1_place_first_legal(const T1_Place *p, int *cellx, int *celly)
{
    int cx, cz;
    if (!p->active) return 0;
    for (cz = 0; cz < 64; ++cz)
        for (cx = 0; cx < 64; ++cx)
            if (t1_place_legal(p, cx, cz))
            { if (cellx) *cellx = cx; if (celly) *celly = cz; return 1; }
    return 0;
}

void t1_place_grid(const T1_Place *p, int *gx, int *gy)
{
    if (gx) *gx = p->hoverx - p->gridx;
    if (gy) *gy = p->hovery - p->gridy;
}

/* One flat quad on a cell's own four terrain corners, so the shading follows the hills
 * instead of hovering over them. Colour comes through the vertex, which is free on this
 * card now that the vertex carries three channels. */
static long place_cell(const T1_Terrain *t, const T1_Cam *cam, const T1_Screen *scr,
                       const SR_Texture *white, float wu, float wv,
                       int cx, int cz, float r, float g, float b, float alpha)
{
    SR_Vertex nw, sw, ne, se;
    float a3[3];
    long drawn = 0;

    if (cx < 0 || cx > 63 || cz < 0 || cz > 63) return 0;
    t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(t, cx,     cz    ), (float)cz,     &nw);
    t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(t, cx,     cz + 1), (float)cz + 1, &sw);
    t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(t, cx + 1, cz    ), (float)cz,     &ne);
    t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(t, cx + 1, cz + 1), (float)cz + 1, &se);
    nw.u = sw.u = ne.u = se.u = wu;
    nw.v = sw.v = ne.v = se.v = wv;
    nw.light = sw.light = ne.light = se.light = 1.0f;
    nw.r = sw.r = ne.r = se.r = r;
    nw.g = sw.g = ne.g = se.g = g;
    nw.b = sw.b = ne.b = se.b = b;
    a3[0] = a3[1] = a3[2] = alpha;
    t1_glide_alpha3(a3);
    /* The terrain's own diagonal, SW to NE, or the shading folds the other way from the
     * ground it is painted on. */
    drawn += t1_tri(0, &nw, &ne, &sw, white, scr);
    drawn += t1_tri(0, &ne, &se, &sw, white, scr);
    return drawn;
}

long t1_place_draw(T1_Place *p, T1_MeshBank *b, const T1_Terrain *t,
                   const T1_Cam *cam, const T1_Screen *scr,
                   const SR_Texture *white, float wu, float wv)
{
    long drawn = 0;
    int cx, cz, k;

    if (!p->active || !white || !white->guploaded) return 0;
    p->cellsdrawn = 0;

    t1_glide_ckey(0, 0);
    t1_glide_blend(1);
    t1_glide_depth_write(0);
    t1_glide_depth_lequal(1);

    /* 1. the legal region, faint, so the player can see where the base can grow at all
     *    rather than hunting for it one click at a time. */
    for (cz = 0; cz < 64; ++cz)
        for (cx = 0; cx < 64; ++cx)
        {
            if (!p->prox[cz * 64 + cx]) continue;
            drawn += place_cell(t, cam, scr, white, wu, wv, cx, cz,
                                0.25f, 0.85f, 0.30f, 0.16f);
            ++p->cellsdrawn;
        }

    /* 2. the footprint under the pointer, per cell, in the engine's verdict. */
    if (p->hoverx >= 0)
    {
        int origin = p->hovery * T1P_CELL_STRIDE + p->hoverx;
        int n = p->item.noccupy > 0 ? p->item.noccupy : 1;
        for (k = 0; k < n; ++k)
        {
            int c  = origin + (p->item.noccupy > 0 ? p->item.occupy[k] : 0);
            int ox = c % T1P_CELL_STRIDE, oz = c / T1P_CELL_STRIDE;
            int ok = (ox >= 0 && ox < 64 && oz >= 0 && oz < 64) ? p->clear[oz * 64 + ox] : 0;
            drawn += place_cell(t, cam, scr, white, wu, wv, ox, oz,
                                ok ? 0.20f : 1.0f, ok ? 1.0f : 0.20f, 0.20f,
                                p->legal ? 0.42f : 0.34f);
            ++p->cellsdrawn;
        }
    }

    t1_glide_depth_lequal(0);

    /* 3. the building itself, translucent, standing where it would stand.
     *
     * The anchor is the footprint's CENTRE, because that is where every other model in
     * this renderer is drawn from: the brain reports clx/cly, the centre of the
     * footprint, and the meshes are authored centred on their own origin. Deriving the
     * centre from the occupy list rather than from a size table means it is right for
     * every structure, including the ones that are not rectangular. */
    if (p->mesh >= 0 && p->hoverx >= 0)
    {
        int lox = 127, hix = -127, loz = 127, hiz = -127;
        float fx, fz, fy;
        T1_MeshParams mp;

        if (p->item.noccupy > 0)
        {
            for (k = 0; k < p->item.noccupy; ++k)
            {
                int c = p->item.occupy[k];
                int dx = c % T1P_CELL_STRIDE, dz = c / T1P_CELL_STRIDE;
                /* The offsets are signed: a cell to the WEST is -1, which comes out of a
                 * 128 modulo as 127 with the row one lower. Fold it back. */
                if (dx > T1P_CELL_STRIDE / 2) { dx -= T1P_CELL_STRIDE; ++dz; }
                if (dx < lox) lox = dx;
                if (dx > hix) hix = dx;
                if (dz < loz) loz = dz;
                if (dz > hiz) hiz = dz;
            }
        }
        else { lox = hix = loz = hiz = 0; }

        fx = (float)p->hoverx + ((float)(lox + hix) + 1.0f) * 0.5f;
        fz = (float)p->hovery + ((float)(loz + hiz) + 1.0f) * 0.5f;
        fy = t1_terrain_corner_y(t, (int)fx, (int)fz);

        memset(&mp, 0, sizeof mp);
        mp.mesh = p->mesh;
        mp.wx = fx; mp.wy = fy; mp.wz = fz;
        mp.animT = -1.0f;          /* the rest pose: a ghost is not playing an idle */
        mp.build_frac = 1.0f;
        /* Cutout triangles are drawn too, but with the chroma key OFF the key colour is
         * a real magenta texel, so the ghost pass keeps the key on for them. Blending is
         * already on, and the two compose: keyed texels are dropped, the rest blends. */
        t1_glide_ckey(1, 0x00FF00FF);
        {
            float a3[3];
            a3[0] = a3[1] = a3[2] = p->legal ? 0.55f : 0.35f;
            t1_glide_alpha3(a3);
        }
        b->house = 0;
        drawn += t1_mesh_draw_p(b, 0, cam, scr, &mp);
    }

    t1_glide_alpha3(0);
    t1_glide_blend(0);
    t1_glide_depth_write(1);
    return drawn;
}
