/* softras.c -- see softras.h. The CPU draws the pixels. */

#include <string.h>
#include "softras.h"

static const unsigned int *g_shade;   /* SR_SHADES * 256 */

/* ---------------------------------------------------------------------------
 * f2i: float to int in ONE instruction.
 *
 * This is the most important line in the file. A plain C (int)f cast must truncate
 * toward zero, which x87 cannot do in its default rounding mode, so the compiler wraps
 * every cast in fnstcw / fldcw / fist / fldcw. Each control word write serialises the
 * whole floating point pipeline. Disassembling the previous version of this file with
 * the project's own flags showed SIX fldcw instructions in the per-pixel body, and on
 * the box that cost about 145 cycles per pixel: the rasteriser measured 183.8 cycles
 * per drawn pixel when the memory floor for its two writes is about 36.
 *
 * fistpl rounds using the CURRENT mode (nearest) rather than forcing truncation, so it
 * needs no control word traffic. Rounding to nearest instead of toward zero is harmless
 * here: every use below seeds a fixed point accumulator that is masked to a texture
 * size, and half a texel of bias is invisible.
 *
 * The alternative was -mfpmath=sse, which also removes every fldcw and which this box's
 * Pentium III would run. It is not taken, because tools/win98/build.sh targets Pentium
 * II on purpose and requiring SSE is a change to the Tier 1 hardware contract, not a
 * compiler flag. Fixed point keeps the contract AND is what the era's renderers did.
 * ------------------------------------------------------------------------- */
static __inline int f2i(float f)
{
    int r;
    __asm__ ("fistpl %0" : "=m" (r) : "t" (f) : "st");
    return r;
}

/* 1/w lives in the depth buffer as a 12.20 fixed point integer, so the per-pixel depth
 * test is an integer compare and the step an integer add. The near reject in
 * sr_triangle keeps w above 1e-3, so 1/w stays below 1000 and 1000 << 20 is 1.05e9,
 * comfortably inside a signed 32 bit int. Larger is nearer, as before. */
#define SR_ZSHIFT 20
#define SR_ZSCALE 1048576.0f

/* span is always 1..SR_SPAN, so the four per-span divides by it become one multiply. */
static const float SR_RECIP[SR_SPAN + 1] = {
    0.0f, 1.0f, 1.0f/2, 1.0f/3, 1.0f/4, 1.0f/5, 1.0f/6, 1.0f/7, 1.0f/8
};

void sr_bind(SR_Target *t, int w, int h, unsigned int *px, int *zb)
{
    t->w = w; t->h = h; t->px = px; t->zb = zb;
}

void sr_build_shade(const unsigned char *pal8, unsigned int *out)
{
    int lvl, i;
    for (lvl = 0; lvl < SR_SHADES; ++lvl)
    {
        /* Level SR_SHADES-1 is full brightness and must be EXACTLY the palette, so
         * that unlit 2D art blitted through the top level is bit for bit the colour
         * the pack says. Scaling by lvl/(SR_SHADES-1) guarantees that; scaling by
         * (lvl+1)/SR_SHADES would darken everything by one part in 32 forever. */
        int num = lvl, den = SR_SHADES - 1;
        for (i = 0; i < 256; ++i)
        {
            unsigned int r = (unsigned int)pal8[i * 3 + 0] * num / den;
            unsigned int g = (unsigned int)pal8[i * 3 + 1] * num / den;
            unsigned int b = (unsigned int)pal8[i * 3 + 2] * num / den;
            out[lvl * 256 + i] = (r << 16) | (g << 8) | b;
        }
    }
}

void sr_use_shade(const unsigned int *shade) { g_shade = shade; }

void sr_texture(SR_Texture *t, const unsigned char *idx, int w, int h)
{
    int s = 0;
    while ((1 << s) < w) ++s;
    t->idx = idx; t->w = w; t->h = h;
    t->wmask = w - 1; t->hmask = h - 1; t->wshift = s;
    t->ckey = 0;
    t->gaddr = 0;
    t->guploaded = 0;
    t->gaspect = 0;
    t->gscale = 0.0f;
    /* EVERY OPTIONAL PIXEL POINTER IS CLEARED HERE, and it is not defensive tidying.
     * Texture banks are malloc'd and never memset, so a field this function forgets is
     * whatever was in the heap. The Glide backend picks its upload FORMAT by testing
     * these in order, so one stale non-NULL pointer makes it hand the card a wild address
     * -- which on this hardware is not an error, it is a frozen machine. That is exactly
     * how alpha8 announced itself the day it was added. */
    t->px16 = 0;
    t->alpha8 = 0;
}

/* An 8-bit ALPHA plane, for the Glide backend only. Everything except the pixel pointer
 * is set exactly as sr_texture does it, so the aspect and LOD rules cannot drift apart. */
void sr_texture_alpha(SR_Texture *t, const unsigned char *px, int w, int h)
{
    sr_texture(t, 0, w, h);
    t->alpha8 = px;
}

void sr_texture16(SR_Texture *t, const unsigned short *px, int w, int h)
{
    sr_texture(t, 0, w, h);
    t->px16 = px;
}

void sr_texture_ckey(SR_Texture *t, int on) { t->ckey = on ? 1 : 0; }

void sr_clear(SR_Target *t, unsigned int rgb)
{
    long n = (long)t->w * t->h, i;
    for (i = 0; i < n; ++i) t->px[i] = rgb;
}

void sr_clear_depth(SR_Target *t)
{
    if (!t->zb) return;
    /* 0 is infinitely far, and zero is the one pattern memset can write, so the depth
     * clear runs at whatever the machine's memory can do. That matters more than it
     * looks: this box writes about 119 MB/s, measured, so the two clears alone are
     * already a fifth of the frame and the only way to make them cheaper is to write
     * fewer bytes. */
    memset(t->zb, 0, (size_t)t->w * (size_t)t->h * sizeof(int));
}

void sr_blit8(SR_Target *t, const unsigned char *src, int sw, int sh,
              const unsigned int *pal32, int dx, int dy, int scale, int transparent)
{
    int x, y, sx, sy;
    if (scale < 1) scale = 1;
    for (sy = 0; sy < sh; ++sy)
    {
        const unsigned char *srow = src + (long)sy * sw;
        int y0 = dy + sy * scale;
        for (y = y0; y < y0 + scale; ++y)
        {
            unsigned int *drow;
            if (y < 0 || y >= t->h) continue;
            drow = t->px + (long)y * t->w;
            for (sx = 0; sx < sw; ++sx)
            {
                unsigned char idx = srow[sx];
                int x0 = dx + sx * scale;
                unsigned int c;
                if (transparent && idx == 0) continue;   /* DOS index 0 is see-through */
                c = pal32[idx];
                for (x = x0; x < x0 + scale; ++x)
                    if (x >= 0 && x < t->w) drow[x] = c;
            }
        }
    }
}

/* --------------------------------------------------------------------------- *
 * The triangle.
 *
 * Attributes are interpolated with plane gradients (the value of an attribute is a
 * linear function of screen x and y once it has been divided by w), so there is one
 * setup per triangle and a plain add per pixel. Edges are walked per scanline.
 * --------------------------------------------------------------------------- */

typedef struct { float x, y, oow, uow, vow, low; } SR_Screen;

static float grad_dx(const SR_Screen *v0, const SR_Screen *v1, const SR_Screen *v2,
                     float a0, float a1, float a2, float inv_det)
{
    return ((a1 - a0) * (v2->y - v0->y) - (a2 - a0) * (v1->y - v0->y)) * inv_det;
}
static float grad_dy(const SR_Screen *v0, const SR_Screen *v1, const SR_Screen *v2,
                     float a0, float a1, float a2, float inv_det)
{
    return ((a2 - a0) * (v1->x - v0->x) - (a1 - a0) * (v2->x - v0->x)) * inv_det;
}

long sr_triangle(SR_Target *t, const SR_Vertex *a, const SR_Vertex *b,
                 const SR_Vertex *c, const SR_Texture *tex,
                 float cx, float cy, float fx, float fy)
{
    const SR_Vertex *in[3];
    SR_Screen s[3];
    const SR_Screen *v0, *v1, *v2;
    float det, inv_det;
    float doow_dx, doow_dy, duow_dx, duow_dy, dvow_dx, dvow_dy, dlow_dx, dlow_dy;
    int i, ytop, ybot, y;
    long drawn = 0;
    float edge_y0[3], edge_y1[3], edge_x0[3], edge_slope[3];
    int   edge_valid[3];

    /* Hoisted out of the pixel loop on purpose. Reading them through `tex` inside the
     * loop makes the compiler reload all five after every store to drow[x], because it
     * cannot prove the store does not alias the struct. That was five dependent loads
     * per pixel, visible in the disassembly. */
    const unsigned char *tidx;
    const unsigned int  *shade = g_shade;
    int twmask, thmask, twshift, tckey;

    /* NEAR REJECT. No clipper yet: see the header and known-gap notes.
     *
     * Written as !(w > x) rather than (w <= x) so that a NaN is rejected too: every
     * comparison against NaN is false, so the obvious spelling lets NaN through and then
     * 1/NaN poisons every gradient. This rasteriser survives that because it clamps its
     * scanline bounds; the Voodoo does not, and it wedged. Same spelling, both backends. */
    if (!(a->w > 1.0e-3f) || !(b->w > 1.0e-3f) || !(c->w > 1.0e-3f)) return 0;

    tidx = tex->idx; twmask = tex->wmask; thmask = tex->hmask; twshift = tex->wshift;
    tckey = tex->ckey;

    in[0] = a; in[1] = b; in[2] = c;
    for (i = 0; i < 3; ++i)
    {
        float oow = 1.0f / in[i]->w;
        s[i].x   = cx + in[i]->x * fx * oow;
        s[i].y   = cy - in[i]->y * fy * oow;   /* screen y grows downward */
        s[i].oow = oow;
        s[i].uow = in[i]->u * oow;
        s[i].vow = in[i]->v * oow;
        s[i].low = in[i]->light * oow;
    }

    v0 = &s[0]; v1 = &s[1]; v2 = &s[2];

    /* Backface cull by signed area. Positive area is counter-clockwise on screen,
     * which after the y flip above means front facing. */
    det = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
    if (det <= 0.0f) return 0;
    inv_det = 1.0f / det;

    doow_dx = grad_dx(v0, v1, v2, v0->oow, v1->oow, v2->oow, inv_det);
    doow_dy = grad_dy(v0, v1, v2, v0->oow, v1->oow, v2->oow, inv_det);
    duow_dx = grad_dx(v0, v1, v2, v0->uow, v1->uow, v2->uow, inv_det);
    duow_dy = grad_dy(v0, v1, v2, v0->uow, v1->uow, v2->uow, inv_det);
    dvow_dx = grad_dx(v0, v1, v2, v0->vow, v1->vow, v2->vow, inv_det);
    dvow_dy = grad_dy(v0, v1, v2, v0->vow, v1->vow, v2->vow, inv_det);
    dlow_dx = grad_dx(v0, v1, v2, v0->low, v1->low, v2->low, inv_det);
    dlow_dy = grad_dy(v0, v1, v2, v0->low, v1->low, v2->low, inv_det);

    ytop = (int)(s[0].y < s[1].y ? (s[0].y < s[2].y ? s[0].y : s[2].y)
                                 : (s[1].y < s[2].y ? s[1].y : s[2].y));
    ybot = (int)(s[0].y > s[1].y ? (s[0].y > s[2].y ? s[0].y : s[2].y)
                                 : (s[1].y > s[2].y ? s[1].y : s[2].y)) + 1;
    if (ytop < 0) ytop = 0;
    if (ybot > t->h) ybot = t->h;

    /* Edge slopes, computed ONCE per triangle rather than once per scanline.
     *
     * The previous version divided by (yb - ya) inside the scanline loop, three times per
     * row. That is invisible on the big triangles the test scene drew and expensive on
     * real terrain, where a frame is about 1,600 triangles of roughly 157 pixels each and
     * the per-scanline divides came to a larger cost than the pixels did. Now each edge
     * carries its dx/dy and the scanline does one multiply-add. */
    {
        int e;
        for (e = 0; e < 3; ++e)
        {
            const SR_Screen *p = &s[e], *q = &s[(e + 1) % 3];
            if (p->y == q->y) { edge_valid[e] = 0; continue; }
            if (p->y < q->y) { edge_y0[e] = p->y; edge_y1[e] = q->y; edge_x0[e] = p->x;
                               edge_slope[e] = (q->x - p->x) / (q->y - p->y); }
            else             { edge_y0[e] = q->y; edge_y1[e] = p->y; edge_x0[e] = q->x;
                               edge_slope[e] = (p->x - q->x) / (p->y - q->y); }
            edge_valid[e] = 1;
        }
    }

    for (y = ytop; y < ybot; ++y)
    {
        float fyc = (float)y + 0.5f;
        float xl = 1.0e30f, xr = -1.0e30f;
        int e, x, xs, xe;
        unsigned int *drow;
        int *zrow;
        float oow, uow, vow, low, w0, u0, v0f;

        /* Two of the three edges cross any scanline inside the triangle; a horizontal
         * edge crosses none. One multiply-add each, no divide. */
        for (e = 0; e < 3; ++e)
        {
            float xi;
            if (!edge_valid[e]) continue;
            if (fyc < edge_y0[e] || fyc >= edge_y1[e]) continue;
            xi = edge_x0[e] + (fyc - edge_y0[e]) * edge_slope[e];
            if (xi < xl) xl = xi;
            if (xi > xr) xr = xi;
        }
        if (xr <= xl) continue;

        /* f2i, not (int), for the same reason the pixel loop uses it: this runs twice
         * per scanline and a plain cast is two FPU control word rewrites each time. */
        xs = f2i(xl);
        xe = f2i(xr);
        if (xs < 0) xs = 0;
        if (xe > t->w) xe = t->w;
        if (xs >= xe) continue;

        drow = t->px + (long)y * t->w;
        zrow = t->zb ? t->zb + (long)y * t->w : 0;

        {
            float ddx = ((float)xs + 0.5f) - v0->x;
            float ddy = fyc - v0->y;
            oow = v0->oow + ddx * doow_dx + ddy * doow_dy;
            uow = v0->uow + ddx * duow_dx + ddy * duow_dy;
            vow = v0->vow + ddx * dvow_dx + ddy * dvow_dy;
            low = v0->low + ddx * dlow_dx + ddy * dlow_dy;
        }

        x   = xs;
        w0  = (oow != 0.0f) ? 1.0f / oow : 0.0f;
        u0  = uow * w0;
        v0f = vow * w0;

        /* ONE perspective divide per SR_SPAN pixels. Inside a span everything is
         * integer: u and v step as 16.16, depth as 12.20, and the shade level is
         * resolved once for the whole span, which is what the era's renderers did with
         * their light spans and which is invisible against a 32 level palette. */
        while (x < xe)
        {
            int span = xe - x;
            int ufx, vfx, dufx, dvfx, zfx, dzfx, lbase;
            float oow_e, w_e, u1, v1f, l0, l1, inv;
            int k;
            if (span > SR_SPAN) span = SR_SPAN;
            inv = SR_RECIP[span];

            oow_e = oow + doow_dx * (float)span;
            w_e   = (oow_e != 0.0f) ? 1.0f / oow_e : 0.0f;
            u1    = (uow + duow_dx * (float)span) * w_e;
            v1f   = (vow + dvow_dx * (float)span) * w_e;
            l0    = low * w0;
            l1    = (low + dlow_dx * (float)span) * w_e;

            ufx  = f2i(u0  * 65536.0f);
            vfx  = f2i(v0f * 65536.0f);
            dufx = f2i((u1  - u0 ) * 65536.0f * inv);
            dvfx = f2i((v1f - v0f) * 65536.0f * inv);
            zfx  = f2i(oow * SR_ZSCALE);
            dzfx = f2i((oow_e - oow) * SR_ZSCALE * inv);
            {
                /* the span's midpoint light, so a gradient does not lean one way */
                int lv = f2i((l0 + l1) * 0.5f * (float)(SR_SHADES - 1));
                if (lv < 0) lv = 0;
                if (lv > SR_SHADES - 1) lv = SR_SHADES - 1;
                lbase = lv << 8;
            }

            if (zrow)
            {
                for (k = 0; k < span; ++k, ++x)
                {
                    if (zfx > zrow[x])
                    {
                        int ui = (ufx >> 16) & twmask;
                        int vi = (vfx >> 16) & thmask;
                        int tl = tidx[(vi << twshift) + ui];
                        if (tl || !tckey)
                        {
                            drow[x] = shade[lbase | tl];
                            zrow[x] = zfx;
                            ++drawn;
                        }
                    }
                    ufx += dufx; vfx += dvfx; zfx += dzfx;
                }
            }
            else
            {
                for (k = 0; k < span; ++k, ++x)
                {
                    int ui = (ufx >> 16) & twmask;
                    int vi = (vfx >> 16) & thmask;
                    int tl = tidx[(vi << twshift) + ui];
                    if (tl || !tckey)
                    {
                        drow[x] = shade[lbase | tl];
                        ++drawn;
                    }
                    ufx += dufx; vfx += dvfx;
                }
            }

            oow  = oow_e;
            uow += duow_dx * (float)span;
            vow += dvow_dx * (float)span;
            low += dlow_dx * (float)span;
            w0 = w_e; u0 = u1; v0f = v1f;
        }
    }
    return drawn;
}
