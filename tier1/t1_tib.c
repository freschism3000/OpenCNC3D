#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t1_tib.h"
#include "t1_glide.h"

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int t1_tib_load(T1_Tib *t, const char *path, char *err, int errlen)
{
    FILE *f;
    long n;
    unsigned char *p;
    int i;

    memset(t, 0, sizeof *t);
    f = fopen(path, "rb");
    if (!f) { _snprintf(err, errlen, "cannot open %s", path); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    t->blob = (unsigned char *)malloc((size_t)n);
    if (!t->blob) { fclose(f); _snprintf(err, errlen, "out of memory (%ld)", n); return 0; }
    if (fread(t->blob, 1, (size_t)n, f) != (size_t)n)
    { fclose(f); _snprintf(err, errlen, "short read on %s", path); return 0; }
    fclose(f);
    t->blobsize = n;

    if (n < 800 || memcmp(t->blob, "T1TIB001", 8) != 0)
    { _snprintf(err, errlen, "%s is not a T1TIB001 file (tools/win98/mktib.py)", path); return 0; }
    p = t->blob + 8;
    if (rd32(p) != 1) { _snprintf(err, errlen, "unknown t1tib version"); return 0; }
    p += 4;
    t->types    = (int)rd32(p); p += 4;
    t->frames   = (int)rd32(p); p += 4;
    t->fw       = (int)rd32(p); p += 4;
    t->fh       = (int)rd32(p); p += 4;
    t->nsheet   = (int)rd32(p); p += 4;
    t->sheet_sz = (int)rd32(p); p += 4;
    if (t->nsheet < 1 || t->nsheet > 8 || t->sheet_sz != 256)
    { _snprintf(err, errlen, "%d sheet(s) of %d is not something this draws",
                t->nsheet, t->sheet_sz); return 0; }
    t->pal = p; p += 768;
    t->slot = (const unsigned short *)p;
    p += (long)t->types * t->frames * 6;
    for (i = 0; i < t->nsheet; ++i)
    {
        sr_texture(&t->tex[i], p, t->sheet_sz, t->sheet_sz);
        /* Index 0 is the hole. Per texture, never global: the terrain's index 0 is its
         * water and must keep drawing. */
        sr_texture_ckey(&t->tex[i], 1);
        p += (long)t->sheet_sz * t->sheet_sz;
    }
    if (p > t->blob + n) { _snprintf(err, errlen, "%s is truncated", path); return 0; }
    t->ok = 1;
    return 1;
}

void t1_tib_free(T1_Tib *t)
{
    if (t->blob) free(t->blob);
    memset(t, 0, sizeof *t);
}

int t1_tib_upload(T1_Tib *t, char *err, int errlen)
{
    int i;
    if (!t->ok) return 0;
    for (i = 0; i < t->nsheet; ++i)
        if (!t1_glide_upload(&t->tex[i], err, errlen)) return 0;
    return 1;
}

long t1_tib_draw(T1_Tib *t, const T1_Terrain *terr, const T1_Cam *cam,
                 const T1_Screen *scr, const W98_Overlays *ov,
                 int (*shown)(int cx, int cz))
{
    return t1_tib_draw_list(t, terr, cam, scr, ov->tib, ov->ntib, shown);
}

long t1_tib_draw_list(T1_Tib *t, const T1_Terrain *terr, const T1_Cam *cam,
                      const T1_Screen *scr, const W98_Tib *cells, int n,
                      int (*shown)(int cx, int cz))
{
    long drawn = 0;
    int i;
    if (!t->ok || n < 1) return 0;

    t1_glide_palette(t->pal);
    t1_glide_ckey(1, 0x00FF00FF);
    /* Point sampled, like every other cutout on this card: a filtered texel next to a
     * transparent one blends TOWARD the key colour and lands near it without matching,
     * so the key stops working and the crystals fringe. */
    t1_glide_filter(0);
    t1_glide_depth_write(0);
    t1_glide_depth_lequal(1);

    for (i = 0; i < n; ++i)
    {
        const W98_Tib *c = &cells[i];
        const unsigned short *sl;
        SR_Vertex nw, sw, ne, se;
        int kind = c->kind, stage = c->stage, cx = c->cx, cz = c->cy;
        float u0, v0, u1, v1, lift;

        if (cx < 0 || cx > 63 || cz < 0 || cz > 63) continue;
        if (shown && !shown(cx, cz)) continue;
        if (kind >= t->types)  kind  = t->types - 1;
        if (stage >= t->frames) stage = t->frames - 1;
        sl = t->slot + ((long)kind * t->frames + stage) * 3;
        if (sl[0] >= (unsigned short)t->nsheet) continue;

        u0 = (float)sl[1];             v0 = (float)sl[2];
        u1 = u0 + (float)t->fw;        v1 = v0 + (float)t->fh;

        /* A hair above the ground. The depth WRITE is off, so this is not there to win a
         * depth fight; it is there because the veil and the ground and the crystals are
         * all exactly co-planar and the card's interpolators are not infinitely precise. */
        lift = 0.010f;

        t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(terr, cx,     cz    ) + lift, (float)cz,     &nw);
        t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(terr, cx,     cz + 1) + lift, (float)cz + 1, &sw);
        t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(terr, cx + 1, cz    ) + lift, (float)cz,     &ne);
        t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(terr, cx + 1, cz + 1) + lift, (float)cz + 1, &se);

        nw.u = u0; nw.v = v0;
        sw.u = u0; sw.v = v1;
        ne.u = u1; ne.v = v0;
        se.u = u1; se.v = v1;
        /* Unlit, which is what the desktop build draws: the art already carries its own
         * shading and the console does not relight it. */
        SR_GREY(nw, 1.0f); SR_GREY(sw, 1.0f);
        SR_GREY(ne, 1.0f); SR_GREY(se, 1.0f);

        drawn += t1_tri(0, &nw, &ne, &sw, &t->tex[sl[0]], scr);
        drawn += t1_tri(0, &ne, &se, &sw, &t->tex[sl[0]], scr);
    }

    t1_glide_depth_lequal(0);
    t1_glide_depth_write(1);
    return drawn;
}
