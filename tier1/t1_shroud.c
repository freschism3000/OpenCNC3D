#include <string.h>

#include "t1_shroud.h"
#include "t1_glide.h"

/* The alpha ceiling. A fully unmapped cell is not quite black, which is the cartridge's
 * own answer and a good one: total black reads as a hole in the screen, while 200/255
 * still hides everything and leaves the ground's shape faintly legible at the frontier. */
#define SH_CEIL   200.0f
/* A cell that has been SEEN but is not currently watched. The 1995 game shows the
 * terrain and hides the units; this build has no per-object visibility yet, so the veil
 * is what marks the difference and the gap is registered rather than papered over. */
#define SH_MAPPED  90.0f

/* Alpha at one CORNER: the average over the four cells that meet there. Out-of-map cells
 * count as unmapped, which keeps the border opaque instead of fading into nothing. */
static float corner_alpha(const unsigned char *vis, int cx, int cz)
{
    int dx, dz;
    float sum = 0.0f;
    for (dz = -1; dz <= 0; ++dz)
        for (dx = -1; dx <= 0; ++dx)
        {
            int x = cx + dx, z = cz + dz;
            int s = (x < 0 || x > 63 || z < 0 || z > 63) ? 0 : vis[z * 64 + x];
            sum += (s == 2) ? 0.0f : (s == 1) ? SH_MAPPED : SH_CEIL;
        }
    return sum * (0.25f / 255.0f);
}

long t1_shroud_draw(const T1_Terrain *t, const T1_Cam *cam, const T1_Screen *scr,
                    const unsigned char *vis, const SR_Texture *white,
                    float wu, float wv,
                    int x0, int z0, int w, int h)
{
    long drawn = 0;
    int cx, cz;

    if (!white || !white->guploaded) return 0;
    t1_glide_blend(1);
    /* Depth TESTING stays on so the veil is occluded by nothing (it is on the ground),
     * but the depth WRITE goes off: a translucent quad that writes depth stops whatever
     * is meant to show through it from drawing at all. */
    t1_glide_depth_write(0);
    t1_glide_depth_lequal(1);

    for (cz = z0; cz < z0 + h; ++cz)
    {
        if (cz < 0 || cz > 63) continue;
        for (cx = x0; cx < x0 + w; ++cx)
        {
            SR_Vertex nw, sw, ne, se;
            float anw, asw, ane, ase, a3[3];

            if (cx < 0 || cx > 63) continue;
            if (vis[cz * 64 + cx] == 2)
            {
                /* A visible cell can still need drawing, because its CORNERS may be
                 * shared with shrouded neighbours and that is where the soft edge lives.
                 * Only a cell whose four corners are all clear can be skipped. */
                if (corner_alpha(vis, cx, cz) <= 0.0f &&
                    corner_alpha(vis, cx + 1, cz) <= 0.0f &&
                    corner_alpha(vis, cx, cz + 1) <= 0.0f &&
                    corner_alpha(vis, cx + 1, cz + 1) <= 0.0f)
                    continue;
            }

            anw = corner_alpha(vis, cx,     cz);
            ane = corner_alpha(vis, cx + 1, cz);
            asw = corner_alpha(vis, cx,     cz + 1);
            ase = corner_alpha(vis, cx + 1, cz + 1);
            if (anw <= 0.0f && ane <= 0.0f && asw <= 0.0f && ase <= 0.0f) continue;

            t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(t, cx,     cz    ), (float)cz,     &nw);
            t1_world_to_eye(cam, (float)cx,     t1_terrain_corner_y(t, cx,     cz + 1), (float)cz + 1, &sw);
            t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(t, cx + 1, cz    ), (float)cz,     &ne);
            t1_world_to_eye(cam, (float)cx + 1, t1_terrain_corner_y(t, cx + 1, cz + 1), (float)cz + 1, &se);

            /* One texel of the solid white block, modulated to black by the light. */
            nw.u = sw.u = ne.u = se.u = wu;
            nw.v = sw.v = ne.v = se.v = wv;
            SR_GREY(nw, 0.0f); SR_GREY(sw, 0.0f);
            SR_GREY(ne, 0.0f); SR_GREY(se, 0.0f);

            /* The terrain's own split, so the veil folds exactly as the ground does.
             * The alpha triple is indexed in the order the vertices are SUBMITTED. That
             * is safe here because the Glide backend never flips a triangle (culling is
             * off on the card), so t1_tri lands on its first call; if that ever changes,
             * a flip would swap b and c and the corner alphas with them. */
            a3[0] = anw; a3[1] = ane; a3[2] = asw;
            t1_glide_alpha3(a3);
            drawn += t1_tri(0, &nw, &ne, &sw, white, scr);
            a3[0] = ane; a3[1] = ase; a3[2] = asw;
            t1_glide_alpha3(a3);
            drawn += t1_tri(0, &ne, &se, &sw, white, scr);
        }
    }

    t1_glide_alpha3(0);
    t1_glide_blend(0);
    t1_glide_depth_lequal(0);
    t1_glide_depth_write(1);
    return drawn;
}
