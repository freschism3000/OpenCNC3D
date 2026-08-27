/*
 * t1_cam.h -- the Nintendo 64 camera, for the Tier 1 software renderer.
 *
 * Every constant here was recovered from the cartridge and is documented in
 * docs/n64-camera.json and docs/n64-camera-math.md. Nothing in this file is tuned or
 * fitted. It is a plain C mirror of what game/cnc_eyes.cpp:2754-3060 already does, so
 * that a tier1 frame and a desktop frame can be diffed against each other.
 *
 * THE CAMERA IS CLOSED FORM AND NEEDS NO MATRIX STACK. Yaw is exactly zero, so world to
 * eye is three subtracts and four multiplies, and the projection reduces to a screen
 * centre plus two focal lengths in pixels, which is precisely sr_triangle's signature.
 *
 * THREE WAYS TO GET THIS WRONG, all of which look almost right:
 *
 *  1. SR_Vertex.w must be the POSITIVE EYE DEPTH, not eye z. World +Z runs SOUTH, which
 *     is toward the viewer. Writing w = ze with a looks-down-+z mental model gives a
 *     north/south mirrored world.
 *  2. Feed the depth buffer w in CELLS, never in leptons. softras stores 1/w as 12.20
 *     fixed point; in cells that spans about 47,000 to 127,000 counts over the visible
 *     range, and in leptons it is 0.116 counts per lepton, which quantises to zero and
 *     collapses the buffer entirely.
 *  3. sr_triangle backface culls on screen-space winding, and cnc_eyes.cpp NEVER enables
 *     culling in any pass. The baker copies the ROM display list's vertex order through
 *     without normalising it, so the pack's winding is arbitrary and cannot be culled
 *     against. Use t1_tri below, which flips and retries, rather than sr_triangle direct.
 *     Measured: the terrain triangles come out det = -1398 under this camera, so a direct
 *     call would discard one hundred percent of the map and draw black.
 */

#ifndef T1_CAM_H
#define T1_CAM_H

#include <math.h>
#include "softras.h"

#define T1_LEPTONS_PER_CELL  256.0f
#define T1_DIST_MIN         2400.0f      /* ROM 0x3E78 */
#define T1_DIST_MAX         3800.0f      /* ROM 0x3E7C */
#define T1_DIST_DEF         3000.0f      /* ROM 0x3E8C */
#define T1_DIST_STEP         100.0f      /* ROM 0x3F24 */
#define T1_PITCH_AT_NEAR      0.78f      /* ROM 0x94344, stored negated */
#define T1_PITCH_AT_FAR       0.92f      /* ROM 0x94348, stored negated */
#define T1_F            2.14450692f      /* 1/tan(25 deg); the fov is 50 deg vertical */
#define T1_AT_Y        (-1.0f/256.0f)    /* ROM 0x402C: the target sits 1 lepton down */

typedef struct
{
    float dist_lep;                 /* the state: 2400..3800 leptons */
    float pitch, sp, cp;            /* derived; pitch is POSITIVE looking down */
    float d_cells;
    float at_x, at_y, at_z;         /* look-at target, in cells */
} T1_Cam;

typedef struct { float cx, cy, fx, fy; } T1_Screen;

/* Pitch is a linear function of distance and is NOT free: 44.69 degrees at the closest
 * zoom, 48.13 at the default, 52.71 at the widest. This is the only place that may
 * write c->pitch. */
static void t1_cam_set_dist(T1_Cam *c, float dist_lep)
{
    float t;
    if (dist_lep < T1_DIST_MIN) dist_lep = T1_DIST_MIN;
    if (dist_lep > T1_DIST_MAX) dist_lep = T1_DIST_MAX;
    c->dist_lep = dist_lep;
    t          = (dist_lep - T1_DIST_MIN) / (T1_DIST_MAX - T1_DIST_MIN);
    c->pitch   = T1_PITCH_AT_NEAR + (T1_PITCH_AT_FAR - T1_PITCH_AT_NEAR) * t;
    c->sp      = (float)sin(c->pitch);
    c->cp      = (float)cos(c->pitch);
    c->d_cells = dist_lep * (1.0f / T1_LEPTONS_PER_CELL);
}

/* The target rides the terrain height under the scroll centre. That is a deliberate
 * deviation from the ROM, which keeps it flat, and cnc_eyes.cpp:2773-2776 makes the same
 * one for the same reason: over a heightfield a flat target makes hills swim. */
static void t1_cam_look_at(T1_Cam *c, float wx, float wz, float ground_y)
{
    c->at_x = wx;
    c->at_z = wz;
    c->at_y = T1_AT_Y + ground_y;
}

/* Resolution changes nothing but H. fx and fy are equal: 428.90 at 640x400,
 * 514.68 at 640x480, 257.34 at 320x240. */
static void t1_screen_params(T1_Screen *s, int w, int h)
{
    s->cx = (float)w * 0.5f;
    s->cy = (float)h * 0.5f;
    s->fx = s->fy = (float)h * 0.5f * T1_F;
}

/* World (cells) -> what sr_triangle wants. This is the whole transform. */
static void t1_world_to_eye(const T1_Cam *c, float wx, float wy, float wz, SR_Vertex *v)
{
    const float ax = wx - c->at_x;      /* world +X is EAST,  screen RIGHT */
    const float ay = wy - c->at_y;      /* height above the TARGET, not the ground */
    const float az = wz - c->at_z;      /* world +Z is SOUTH, screen DOWN, toward us */
    v->x = ax;
    v->y = ay * c->cp - az * c->sp;
    v->w = c->d_cells - ay * c->sp - az * c->cp;   /* positive depth, this is -ze */
    v->z = v->w;
}

/* Screen pixel -> the point on the horizontal plane y = h that projects there.
 *
 * Do NOT invert a 4x4 to do this. Build the ray and intersect the plane: the round trip
 * is exact to float rounding, and diry can never reach zero inside the legal pitch range
 * because the horizon is off the top of the screen at every zoom the console allows. */
static void t1_screen_to_plane(const T1_Cam *c, const T1_Screen *s,
                               float col, float row, float h, float *wx, float *wz)
{
    const float dvx  = (col - s->cx) / s->fx;
    const float dvy  = (s->cy - row) / s->fy;
    const float diry = dvy * c->cp - c->sp;          /* always negative: we look down */
    const float dirz = -dvy * c->sp - c->cp;
    const float eyeY = c->at_y + c->d_cells * c->sp;
    const float t    = (h - eyeY) / diry;
    *wx = c->at_x + t * dvx;
    *wz = c->at_z + c->d_cells * c->cp + t * dirz;
}

/* THE BACKEND SEAM.
 *
 * Every 3D draw in tier1 goes through t1_tri, so this one pointer is the whole switch
 * between the software rasteriser and the Voodoo 2. Scene assembly (t1_terrain.c,
 * t1_mesh.c) does not know which is running and does not change: the camera already
 * produces eye-space x, y, w plus texels and a light, which is exactly what a Glide
 * vertex wants once divided through.
 *
 * NULL means software. tier1/t1_draw.c defines it, so it exists whether or not the
 * Glide backend was compiled in. */
typedef long (*T1_TriFn)(SR_Target *, const SR_Vertex *, const SR_Vertex *,
                         const SR_Vertex *, const SR_Texture *, const T1_Screen *);
extern T1_TriFn t1_tri_hook;

/* MANDATORY on the software path. See risk 3 in the header: flip and retry rather than
 * cull. The Glide backend does its own winding fix, because Glide culls on its own terms. */
static long t1_tri(SR_Target *t, const SR_Vertex *a, const SR_Vertex *b,
                   const SR_Vertex *cc, const SR_Texture *tex, const T1_Screen *s)
{
    long n;
    if (t1_tri_hook) return t1_tri_hook(t, a, b, cc, tex, s);
    n = sr_triangle(t, a, b, cc, tex, s->cx, s->cy, s->fx, s->fy);
    if (n == 0)
        n = sr_triangle(t, a, cc, b, tex, s->cx, s->cy, s->fx, s->fy);
    return n;
}

#endif /* T1_CAM_H */
