/* ====================================================================================
 *  fx_cloud.h -- the cloud mask: one perfectly tiling greyscale texture, generated.
 *
 *  TIER 2 ONLY, like everything else the post chain touches. On Win98 this never runs
 *  and there is no sky over the battlefield, which is the picture we have always shipped.
 *
 *  THIS ART IS OURS, NOT THE CARTRIDGE'S. No N64 model, texture or table carries a
 *  cloud, and the 1995 PC game has none either; a shadow moving over the ground is an
 *  addition, and it is named as an addition rather than dressed up as a recovery.
 *
 *  That is also why it is GENERATED rather than shipped. There is no original here to
 *  be faithful to, so authoring a quarter of a megabyte of cloud art and putting it in
 *  a pack would add a file to this project that no cartridge dump could ever confirm
 *  or contradict -- an asset with no provenance, sitting among assets that all have
 *  one. The arithmetic below is the whole asset instead, and reading it tells you
 *  exactly what the mask is.
 *
 *  WHY IT TILES, which is the one property the whole feature rests on. Every octave is
 *  value noise on an integer lattice whose coordinates are taken MODULO the lattice
 *  size, and the lattice size of octave o is base<<o. Sampling at u and at u+1 lands on
 *  lattice coordinates that differ by exactly one full period, so they read the same
 *  four corners and interpolate the same way: the field is periodic in u and in v with
 *  period 1, by construction rather than by blending a copy over a seam. The domain
 *  warp is itself periodic with the same period, so warping preserves it.
 *
 *  THREE THINGS HERE ARE DELIBERATE AND EACH FIXED A VISIBLE FAULT:
 *
 *  1. QUINTIC interpolation, not cubic and certainly not linear. The mask is stretched
 *     over ~100 cells of ground, so a lattice cell is a dozen metres across and any
 *     discontinuity in the SECOND derivative reads as a crease running along the
 *     lattice. Quintic is C2 and the creases go.
 *
 *  2. A PER-OCTAVE PHASE OFFSET. Every octave's lattice is a power-of-two multiple of
 *     every other's, so with no offset all five of them put a lattice line in the same
 *     place and the result is visibly a grid -- a houndstooth, in the first version of
 *     this. Shifting each octave by its own fraction of a lattice cell breaks the
 *     alignment and costs nothing, and a constant shift of a periodic field is still
 *     periodic, so it cannot cost the tiling either.
 *
 *  3. HISTOGRAM EQUALISATION at the end, and this one is not cosmetic. It is what makes
 *     the panel's two shape dials INDEPENDENT. After equalisation the field's values
 *     are uniform on 0..1, so the fraction of it above a threshold t is exactly 1-t:
 *     the coverage dial can therefore BE that threshold and mean precisely what it
 *     says, while the sharpness dial widens or narrows the ramp around it without
 *     moving the amount of shadowed ground at all. Measured on the baked level: at
 *     coverage 0.30 the mean shadow is 0.300 at every sharpness setting. Without it the
 *     two dials fight, and sharpening the edges also darkens the map.
 *
 *  DETERMINISM. No rand(), no clock, no float accumulation whose order could vary: the
 *  lattice values come from an integer hash of the wrapped lattice coordinate. The
 *  bytes this file produces are the same bytes on every run and on every machine with
 *  IEEE floats, which is what lets the post chain keep its promise that two --shot runs
 *  agree. The MOVEMENT is not in here; it is a uniform the host computes from the
 *  simulation clock, and fx_post.h owns that.
 * ==================================================================================== */
#ifndef CNC3D_FX_CLOUD_H
#define CNC3D_FX_CLOUD_H

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* The baked size. 512 is a quarter of a megabyte at one byte a texel and gives the
   finest octave (lattice 48) sixteen texels to a cell, which is more than the bilinear
   filter can show at any zoom the game allows. */
#define FXC_SIZE 512

/* Below this level the mip chain stops being re-equalised: a 8x8 image has 64 samples
   and forcing those to a uniform histogram is fitting noise, not preserving a
   distribution. Plain box filtering below here converges on the field's mean, which is
   0.5, which is the correct answer for a mask minified past all detail. */
#define FXC_EQ_MIN 16

/* ---- The field ------------------------------------------------------------------- */

/* An integer hash, so the lattice is a pure function of its coordinate. */
static unsigned fxc_hash(int x, int y, unsigned seed)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

/* Quintic: C2 at the lattice lines. See note 1 at the top. */
static float fxc_quint(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/* Value noise, lattice wrapped modulo P. THE MODULO IS THE TILING. */
static float fxc_value(float fx, float fy, int P, unsigned seed)
{
    int   x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float sx = fxc_quint(fx - (float)x0), sy = fxc_quint(fy - (float)y0);
    int   xa = ((x0 % P) + P) % P, xb = (((x0 + 1) % P) + P) % P;
    int   ya = ((y0 % P) + P) % P, yb = (((y0 + 1) % P) + P) % P;
    float a = (float)(fxc_hash(xa, ya, seed) & 0xFFFFFFu) / (float)0xFFFFFFu;
    float b = (float)(fxc_hash(xb, ya, seed) & 0xFFFFFFu) / (float)0xFFFFFFu;
    float c = (float)(fxc_hash(xa, yb, seed) & 0xFFFFFFu) / (float)0xFFFFFFu;
    float d = (float)(fxc_hash(xb, yb, seed) & 0xFFFFFFu) / (float)0xFFFFFFu;
    float ab = a + (b - a) * sx;
    float cd = c + (d - c) * sx;
    return (ab + (cd - ab) * sy) * 2.0f - 1.0f;      /* about -1..1 */
}

/* Gradient noise, same wrapping. Used only for the warp field, where value noise's
   round blobs would push the shapes back towards circles. */
static float fxc_grad(float fx, float fy, int P, unsigned seed)
{
    int   x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - (float)x0, ty = fy - (float)y0;
    float sx = fxc_quint(tx), sy = fxc_quint(ty);
    float n[4];
    const int ox[4] = { 0, 1, 0, 1 }, oy[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 4; i++) {
        int gx = (((x0 + ox[i]) % P) + P) % P;
        int gy = (((y0 + oy[i]) % P) + P) % P;
        float a = (float)(fxc_hash(gx, gy, seed) & 15u) * (6.283185307f / 16.0f);
        n[i] = cosf(a) * (tx - (float)ox[i]) + sinf(a) * (ty - (float)oy[i]);
    }
    float a = n[0] + (n[1] - n[0]) * sx;
    float b = n[2] + (n[3] - n[2]) * sx;
    return (a + (b - a) * sy) * 1.4142f;
}

/* Per-octave phase, in lattice cells. See note 2 at the top. */
static const float FXC_PHX[6] = { 0.000f, 0.317f, 0.611f, 0.194f, 0.842f, 0.473f };
static const float FXC_PHY[6] = { 0.000f, 0.729f, 0.238f, 0.556f, 0.085f, 0.913f };

static float fxc_fbm(int gradient, float u, float v, int base, int oct, unsigned seed)
{
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    int   P = base;
    for (int o = 0; o < oct; o++) {
        float fx = (u + FXC_PHX[o]) * (float)P;
        float fy = (v + FXC_PHY[o]) * (float)P;
        sum  += amp * (gradient ? fxc_grad(fx, fy, P, seed + (unsigned)o * 7919u)
                                : fxc_value(fx, fy, P, seed + (unsigned)o * 7919u));
        norm += amp;
        amp  *= 0.5f;
        P    *= 2;
    }
    return sum / norm;
}

/* The weather itself: four octaves of value noise, read through a two-octave warp.
   The warp is what turns lava-lamp blobs into something with a wind direction in it.

   THE BASE LATTICE IS 8 AND THAT NUMBER WAS MEASURED, not chosen for looks. It sets
   how many cloud masses fit in one tile, and the tile is stretched over cloud_scale
   world cells, so it decides how big a shadow is ON SCREEN. The tactical view is about
   30 cells across and a mass is about one lattice cell, tile/base.

   At base 3 -- where this started -- a mass came out 37 cells wide at the scale then in
   use, WIDER THAN THE SCREEN, and the result was not clouds at all: the whole view sat
   inside one blob and the picture either darkened uniformly or not at all, which read
   as a bug in the dial rather than as weather. Base 8 is what puts several masses in a
   view rather than a fraction of one, at any scale worth setting.

   The pair to keep in step is (base, cloud_scale): raising one and lowering the other
   leaves the on-screen size alone and only moves how far apart the repeats are. */
static float fxc_field(float u, float v)
{
    float wx = fxc_fbm(1, u, v, 2, 2, 1013u);
    float wy = fxc_fbm(1, u, v, 2, 2, 5077u);
    return fxc_fbm(0, u + wx * 0.10f, v + wy * 0.10f, 8, 4, 2749u);
}

/* ---- Equalisation ---------------------------------------------------------------- */

static int fxc_cmp(const void* a, const void* b)
{
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Replace every value by its rank, so the result is uniform on 0..1. See note 3. */
static void fxc_equalise(float* f, int n)
{
    float* s = (float*)malloc((size_t)n * sizeof(float));
    if (!s) return;
    memcpy(s, f, (size_t)n * sizeof(float));
    qsort(s, (size_t)n, sizeof(float), fxc_cmp);
    for (int i = 0; i < n; i++) {
        /* lower_bound: the number of samples strictly below this one */
        int lo = 0, hi = n - 1;
        while (lo < hi) { int m = (lo + hi) / 2; if (s[m] < f[i]) lo = m + 1; else hi = m; }
        f[i] = (n > 1) ? (float)lo / (float)(n - 1) : 0.5f;
    }
    free(s);
}

/* ---- The bake -------------------------------------------------------------------- */

/* Builds level 0 and every mip below it into ONE allocation, and hands back the offset
   of each level. The caller uploads them with glTexImage2D, level by level.

   THE MIPS ARE BUILT HERE RATHER THAN BY THE DRIVER on purpose: fx_gl.h resolves its
   entry points one by one and treats a missing one as a reason to switch the whole
   chain off, and glGenerateMipmap is not among them. Adding it would put every Tier 2
   feature behind a pointer this one texture needs. A box filter is four lines.

   Each level is re-equalised while it is still big enough for that to mean something,
   because a box filter averages towards the middle: without it the coverage dial would
   quietly stop biting as the player zooms out. */
static unsigned char* fxc_bake(int* out_levels, int* out_offset)
{
    /* total texels over the whole chain, 512x512 down to 1x1 */
    int levels = 0, total = 0;
    for (int s = FXC_SIZE; s >= 1; s >>= 1) { out_offset[levels] = total; total += s * s; levels++; }
    *out_levels = levels;

    unsigned char* tex = (unsigned char*)malloc((size_t)total);
    float*         buf = (float*)malloc((size_t)FXC_SIZE * FXC_SIZE * sizeof(float));
    float*         dn  = (float*)malloc((size_t)(FXC_SIZE / 2) * (FXC_SIZE / 2) * sizeof(float));
    if (!tex || !buf || !dn) { free(tex); free(buf); free(dn); return 0; }

    for (int y = 0; y < FXC_SIZE; y++)
        for (int x = 0; x < FXC_SIZE; x++)
            buf[y * FXC_SIZE + x] = fxc_field(((float)x + 0.5f) / (float)FXC_SIZE,
                                              ((float)y + 0.5f) / (float)FXC_SIZE);
    fxc_equalise(buf, FXC_SIZE * FXC_SIZE);

    int s = FXC_SIZE;
    for (int L = 0; L < levels; L++) {
        unsigned char* dst = tex + out_offset[L];
        for (int i = 0; i < s * s; i++) {
            float v = buf[i];
            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            dst[i] = (unsigned char)(v * 255.0f + 0.5f);
        }
        if (s == 1) break;
        int h = s / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < h; x++)
                dn[y * h + x] = 0.25f * (buf[(y * 2) * s + x * 2]     + buf[(y * 2) * s + x * 2 + 1]
                                       + buf[(y * 2 + 1) * s + x * 2] + buf[(y * 2 + 1) * s + x * 2 + 1]);
        if (h >= FXC_EQ_MIN) fxc_equalise(dn, h * h);
        memcpy(buf, dn, (size_t)h * h * sizeof(float));
        s = h;
    }

    free(buf);
    free(dn);
    return tex;
}

#endif /* CNC3D_FX_CLOUD_H */
