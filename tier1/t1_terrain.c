/* t1_terrain.c -- see t1_terrain.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "t1_terrain.h"

/* Nothing in the pack is aligned: the one-byte GDI flag after every texture knocks the
 * whole rest of the file off a four byte boundary, so every multi-byte field is read
 * byte-wise. This is the same rd32 game/dosbar.c uses and for the same reason. */
static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static float rdf32(const unsigned char *p)
{
    union { unsigned int u; float f; } c;
    c.u = rd32(p);
    return c.f;
}

/* ---------------------------------------------------------------------------
 * The corner light, exactly as the cartridge computes it.
 *
 * Four quadrant face normals summed, normalised to length 127, dotted against the
 * resident light (-64,+64,+64), then ambient 16 plus diffuse 250. Every constant was
 * recovered from resident .data and is cited in game/cnc_eyes.cpp:737-776, which this
 * mirrors. Heights are in the console's own units, raw byte times 4, with a cell pitch
 * of 256, so the slope arithmetic is the console's and not a re-derivation.
 *
 * It is computed once at load for all 4,225 corners rather than per frame. On a 535 MHz
 * Pentium III that is the difference between a load-time cost nobody notices and a
 * per-frame cost nobody can afford.
 * ------------------------------------------------------------------------- */
static void t1_terrain_shade_all(T1_Terrain *t)
{
    static const int QX[4][2] = { { 0, -1 }, { -1,  0 }, { 1,  0 }, { 0,  1 } };
    static const int QZ[4][2] = { { -1, 0 }, {  0,  1 }, { 0, -1 }, { 1,  0 } };
    const unsigned char *c = t->heights;
    int cx, cz, q, v;

    for (cz = 0; cz <= 64; ++cz)
        for (cx = 0; cx <= 64; ++cx)
        {
            float h0 = (float)c[cz * 65 + cx] * 4.0f;
            float nx = 0.0f, ny = 0.0f, nz = 0.0f, ln, dot;
            for (q = 0; q < 4; ++q)
            {
                int ax = cx + QX[q][0], az = cz + QZ[q][0];
                int bx = cx + QX[q][1], bz = cz + QZ[q][1];
                float a0, a1, a2, b0, b1, b2;
                if (ax < 0 || ax > 64 || az < 0 || az > 64) continue;
                if (bx < 0 || bx > 64 || bz < 0 || bz > 64) continue;
                a0 = (float)QX[q][0] * 256.0f;
                a1 = (float)c[az * 65 + ax] * 4.0f - h0;
                a2 = (float)QZ[q][0] * 256.0f;
                b0 = (float)QX[q][1] * 256.0f;
                b1 = (float)c[bz * 65 + bx] * 4.0f - h0;
                b2 = (float)QZ[q][1] * 256.0f;
                nx += a1 * b2 - a2 * b1;
                ny += a2 * b0 - a0 * b2;
                nz += a0 * b1 - a1 * b0;
            }
            ln = (float)sqrt(nx * nx + ny * ny + nz * nz);
            if (ln != 0.0f)                    /* the ROM leaves a zero normal alone */
            {
                float s = 127.0f / ln;
                nx *= s; ny *= s; nz *= s;
            }
            dot = -64.0f * nx + 64.0f * ny + 64.0f * nz;
            /* 110.851257 is |(-64,64,64)|. Truncation, not rounding, as the ROM does. */
            v = (int)(16.0f + 250.0f * dot / (110.851257f * 127.0f));
            if (v < 0) v = 0; else if (v > 255) v = 255;
            t->shade[cz * 65 + cx] = (unsigned char)v;
        }
}

int t1_terrain_load(T1_Terrain *t, const char *path, char *err, int errlen)
{
    FILE *f;
    long n;
    unsigned char *p;

    memset(t, 0, sizeof *t);
    f = fopen(path, "rb");
    if (!f) { _snprintf(err, errlen, "cannot open %s", path); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    t->blob = (unsigned char *)malloc((size_t)n);
    if (!t->blob) { fclose(f); _snprintf(err, errlen, "out of memory for %ld bytes", n); return 0; }
    if (fread(t->blob, 1, (size_t)n, f) != (size_t)n)
    { fclose(f); _snprintf(err, errlen, "short read on %s", path); return 0; }
    fclose(f);
    t->blobsize = n;

    if (n < 32 || memcmp(t->blob, "T1TERR02", 8) != 0)
    { _snprintf(err, errlen, "%s is not a T1TERR02 file (re-run tools/win98/mkterrain.py)", path); return 0; }

    p = t->blob + 8;
    if (rd32(p) != 2) { _snprintf(err, errlen, "unknown t1terr version %u", rd32(p)); return 0; }
    p += 4;
    t->npages  = (int)rd32(p); p += 4;
    t->page_sz = (int)rd32(p); p += 4;
    t->tile    = (int)rd32(p); p += 4;
    t->per     = (int)rd32(p); p += 4;
    if (t->npages < 1 || t->npages > 8)
    { _snprintf(err, errlen, "%d atlas pages, expected 1..8", t->npages); return 0; }

    t->pal = p;   p += 768;
    t->pages = p; p += (long)t->npages * t->page_sz * t->page_sz;
    t->ncells = (int)rd32(p); p += 4;
    t->cells = p; p += (long)t->ncells * 4;
    t->heights = p; p += 4225;
    t->cmtint = p;  p += 8450;

    if (p + 8 > t->blob + n)
    { _snprintf(err, errlen, "%s is truncated (want %ld, have %ld)",
                path, (long)(p + 8 - t->blob), n); return 0; }

    {
        int i;
        for (i = 0; i < t->npages; ++i)
            sr_texture(&t->tex[i], t->pages + (long)i * t->page_sz * t->page_sz,
                       t->page_sz, t->page_sz);
    }
    t1_terrain_shade_all(t);
    {   /* The map's own height extremes, for the view cull. Measured once here rather
         * than assumed, because a map whose hills are outside the assumed range would
         * have cells culled that the camera can see. */
        int k;
        int lo = 255, hi = 0;
        for (k = 0; k < 65 * 65; ++k)
        {
            int v = t->heights[k];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        t->ylo = (float)lo * (1.0f / 64.0f) - t->base_y;
        t->yhi = (float)hi * (1.0f / 64.0f) - t->base_y;
    }


    /* The base height. The console's terrain sits wherever the scenario put it, so a
     * per-scenario base is subtracted to bring the map back to y = 0. The median is used
     * rather than the mean so that one mountain does not sink the whole map. */
    {
        static unsigned char sorted[65 * 65];
        int i, j;
        memcpy(sorted, t->heights, 65 * 65);
        /* insertion sort: 4,225 items once at load, and it keeps the code honest */
        for (i = 1; i < 65 * 65; ++i)
        {
            unsigned char k = sorted[i];
            for (j = i - 1; j >= 0 && sorted[j] > k; --j) sorted[j + 1] = sorted[j];
            sorted[j + 1] = k;
        }
        t->base_y = (float)sorted[(65 * 65) / 2] * (1.0f / 64.0f);
    }
    return 1;
}

void t1_terrain_free(T1_Terrain *t)
{
    if (t->blob) free(t->blob);
    memset(t, 0, sizeof *t);
}

float t1_terrain_corner_y(const T1_Terrain *t, int cx, int cz)
{
    if (cx < 0) cx = 0; else if (cx > 64) cx = 64;
    if (cz < 0) cz = 0; else if (cz > 64) cz = 64;
    /* one height unit is 1/64 of a world cell */
    return (float)t->heights[cz * 65 + cx] * (1.0f / 64.0f) - t->base_y;
}

long t1_terrain_draw(T1_Terrain *t, SR_Target *tg, const T1_Cam *cam,
                     const T1_Screen *scr, int x0, int z0, int w, int h)
{
    long drawn = 0;
    int cx, cz;

    for (cz = z0; cz < z0 + h; ++cz)
    {
        if (cz < 0 || cz > 63) continue;
        for (cx = x0; cx < x0 + w; ++cx)
        {
            const unsigned char *rec;
            float u0, v0, u1, v1;
            SR_Vertex nw, sw, ne, se;
            float lnw, lsw, lne, lse;
            const SR_Texture *tex;

            if (cx < 0 || cx > 63) continue;
            /* page, tile x, tile y, holes. The grid is dense row major, verified over
             * all 4,096 records, so the index IS the position and no x,y is stored. */
            rec = t->cells + (long)(cz * 64 + cx) * 4;
            tex = &t->tex[rec[0]];
            u0 = (float)(rec[1] * t->tile);
            v0 = (float)(rec[2] * t->tile);
            u1 = u0 + (float)t->tile;
            v1 = v0 + (float)t->tile;

            lnw = (float)t->shade[(cz    ) * 65 + (cx    )] * (1.0f / 255.0f);
            lsw = (float)t->shade[(cz + 1) * 65 + (cx    )] * (1.0f / 255.0f);
            lne = (float)t->shade[(cz    ) * 65 + (cx + 1)] * (1.0f / 255.0f);
            lse = (float)t->shade[(cz + 1) * 65 + (cx + 1)] * (1.0f / 255.0f);

            t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(t, cx,     cz    ), (float)cz,     &nw);
            t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(t, cx,     cz + 1), (float)cz + 1, &sw);
            t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(t, cx + 1, cz    ), (float)cz,     &ne);
            t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(t, cx + 1, cz + 1), (float)cz + 1, &se);

            nw.u = u0; nw.v = v0; SR_GREY(nw, lnw);
            sw.u = u0; sw.v = v1; SR_GREY(sw, lsw);
            ne.u = u1; ne.v = v0; SR_GREY(ne, lne);
            se.u = u1; se.v = v1; SR_GREY(se, lse);

            /* THE SPLIT IS THE CARTRIDGE'S OWN and is not the rasteriser's to choose:
             * corners load NW, SW, NE, SE and the two triangles are (NW,SW,NE) and
             * (NE,SW,SE), so the shared diagonal runs SW to NE. It matters because 133
             * of the 700 cells in GDI 1's playable rect have non-coplanar corners, and
             * the other diagonal folds those the wrong way.
             * See the display list emitter at ROM 0x196FFC, via cnc_eyes.cpp:3600-3607. */
            /* Emitted in the winding that is already front facing under this camera,
             * so t1_tri lands on its FIRST call. The cartridge's order (NW,SW,NE) comes
             * out det = -1398 here, and letting t1_tri discover that per triangle pays
             * the whole gradient setup twice for every one of the ~1,600 terrain
             * triangles in a frame. Swapping the last two vertices negates det and
             * changes nothing else: same plane, same texels, same lighting. */
            drawn += t1_tri(tg, &nw, &ne, &sw, tex, scr);
            drawn += t1_tri(tg, &ne, &se, &sw, tex, scr);
        }
    }
    return drawn;
}
