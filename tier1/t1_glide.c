/* t1_glide.c -- see t1_glide.h. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "t1_glide_compat.h"      /* NOT <glide.h>: it pins stdcall. See that header. */
#include "t1_glide.h"

static int   g_open;
static FxU32 g_next;              /* next free TMU address */
static FxU32 g_top;
static long  g_pixels;
static long  g_rejects;
static float g_scrw = 640.0f, g_scrh = 480.0f;

static long glide_tri(SR_Target *t, const SR_Vertex *a, const SR_Vertex *b,
                      const SR_Vertex *cc, const SR_Texture *tex, const T1_Screen *s);
static const SR_Texture *g_bound;

int t1_glide_open(int w, int h, char *err, int errlen)
{
    GrHwConfiguration hw;
    GrScreenResolution_t res;

    if (w == 640 && h == 480)      res = GR_RESOLUTION_640x480;
    else if (w == 800 && h == 600) res = GR_RESOLUTION_800x600;
    else { _snprintf(err, errlen, "%dx%d is not a Voodoo resolution", w, h); return 0; }

    memset(&hw, 0, sizeof hw);
    grGlideInit();
    if (!grSstQueryHardware(&hw))
    { _snprintf(err, errlen, "no 3dfx board found"); grGlideShutdown(); return 0; }
    grSstSelect(0);
    if (!grSstWinOpen(0, res, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                      GR_ORIGIN_UPPER_LEFT, 2, 1))
    { _snprintf(err, errlen, "grSstWinOpen(%dx%d) failed", w, h); grGlideShutdown(); return 0; }

    g_scrw = (float)w;
    g_scrh = (float)h;
    g_next = grTexMinAddress(GR_TMU0);
    g_top  = grTexMaxAddress(GR_TMU0);

    /* The texture modulates the iterated vertex colour, which is how the per-corner
     * terrain light and the per-vertex mesh light reach the picture. Same arrangement
     * the shade table implements in software. */
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    /* Bilinear, which the card does for free and the software renderer cannot afford.
     * This is one of the few places Tier 1 is BETTER than Tier 2's default, because the
     * console's RDP filtered its textures too and our point sampling was our choice. */
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    /* WRAP, not clamp, because that is what the software rasteriser does: it MASKS the
     * texel coordinate. 19% of mesh vertices carry UVs outside the unit square, so
     * clamping here quietly draws the wrong texels on a fifth of the geometry. */
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);

    /* W buffering, not Z: the vertices already carry 1/w, and a W buffer distributes its
     * precision the way a game with a near ground plane and a far horizon needs. */
    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthMask(FXTRUE);
    grCullMode(GR_CULL_DISABLE);   /* the pack's winding is arbitrary; see t1_cam.h */

    t1_tri_hook = glide_tri;
    g_open = 1;
    return 1;
}

void t1_glide_close(void)
{
    if (!g_open) return;
    t1_tri_hook = 0;
    grSstWinClose();
    grGlideShutdown();
    g_open = 0;
}

/* Cutout transparency on the card.
 *
 * The software path skips palette index 0 per texture (SR_Texture.ckey). Glide has no
 * per-texture flag, it has one chroma-key comparison against the resulting colour, so the
 * caller turns it on for the pass that needs it. The terrain must NOT have it on: index 0
 * there is a real colour, the water in its holes. */
/* PER-VERTEX ALPHA, for the shroud and nothing else so far.
 *
 * Every other pass on this card is opaque, so rather than widen SR_Vertex (which every
 * producer would then have to fill, and any that forgot would draw with garbage alpha)
 * the three values are handed to the backend for the duration of one triangle. It is a
 * narrow seam on purpose: set, submit, clear. */
static const float *g_alpha;
void t1_glide_alpha3(const float *a3) { g_alpha = a3; }

/* Alpha BLENDING, which is separate from alpha the value. Off everywhere except the
 * shroud pass; a Voodoo 2 blends at full speed but the depth write has to be turned off
 * with it or a translucent quad occludes what is meant to show through it. */
void t1_glide_blend(int on)
{
    if (on) grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                                 GR_BLEND_ONE, GR_BLEND_ZERO);
    else    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
                                 GR_BLEND_ONE, GR_BLEND_ZERO);
}

/* ADDITIVE, for the particle art. The effect textures are premultiplied by their own
 * alpha at conversion time, so a transparent texel is black and adds nothing; that gets
 * the soft edges right without the card carrying an alpha channel for them at all. */
void t1_glide_blend_add(int on)
{
    if (on) grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE,
                                 GR_BLEND_ONE, GR_BLEND_ZERO);
    else    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
                                 GR_BLEND_ONE, GR_BLEND_ZERO);
}

void t1_glide_ckey(int on, unsigned int rgb)
{
    if (on)
    {
        grChromakeyValue((GrColor_t)rgb);
        grChromakeyMode(GR_CHROMAKEY_ENABLE);
    }
    else
    {
        grChromakeyMode(GR_CHROMAKEY_DISABLE);
    }
}

void t1_glide_filter(int bilinear)
{
    GrTextureFilterMode_t m = bilinear ? GR_TEXTUREFILTER_BILINEAR
                                       : GR_TEXTUREFILTER_POINT_SAMPLED;
    grTexFilterMode(GR_TMU0, m, m);
}

/* ---- THE SHADOW STATE ---------------------------------------------------------------
 *
 * The cartridge draws every shadow with ONE RDP state, and this is it term for term:
 * combiner FCFFFFFF/FFFDF2F9, which is "RGB = PRIM, alpha = TEXEL0_A", under render mode
 * 0x005049D8, which is CLR_IN*A_IN + CLR_MEM*(1-A) with the Z compare on and the Z write
 * off. So the colour must come from the ITERATED vertex alone -- the texture must not
 * touch it -- and the alpha must come from the TEXTURE, scaled by the vertex's own alpha
 * so the soldier blob can be lighter than the vehicle plates exactly as the ROM's second
 * combiner makes it.
 *
 * Emphatically not the additive blend: that is the particle pass, and a shadow drawn
 * with it LIGHTENS the ground. */
void t1_glide_shadow_state(int on)
{
    if (on)
    {
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
        grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                       GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    }
    else
    {
        /* Back to what t1_glide_open set: the texture modulates the iterated colour. */
        grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                       GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    }
}

/* CLAMP, for a decal whose UVs overhang its own texture. 184 of the 250 baked shadow
 * faces carry UVs running to -0.148..1.148 and would wrap the opposite edge of the blob
 * into the overhang. */
void t1_glide_clamp(int on)
{
    GrTextureClampMode_t m = on ? GR_TEXTURECLAMP_CLAMP : GR_TEXTURECLAMP_WRAP;
    grTexClampMode(GR_TMU0, m, m);
}

void t1_glide_palette(const unsigned char pal[768])
{
    FxU32 tab[256];
    int i;
    for (i = 0; i < 256; ++i)
        tab[i] = ((FxU32)pal[i*3+0] << 16) | ((FxU32)pal[i*3+1] << 8) | (FxU32)pal[i*3+2];
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, tab);
}

void t1_glide_reset_textures(void) { g_next = grTexMinAddress(GR_TMU0); }

static int lod_for(int n, GrLOD_t *lod)
{
    switch (n) {
    case 256: *lod = GR_LOD_256; return 1;
    case 128: *lod = GR_LOD_128; return 1;
    case  64: *lod = GR_LOD_64;  return 1;
    case  32: *lod = GR_LOD_32;  return 1;
    case  16: *lod = GR_LOD_16;  return 1;
    case   8: *lod = GR_LOD_8;   return 1;
    case   4: *lod = GR_LOD_4;   return 1;
    case   2: *lod = GR_LOD_2;   return 1;
    case   1: *lod = GR_LOD_1;   return 1;
    default: return 0;
    }
}

/* Glide describes a texture as a LOD (its LARGER dimension) plus an aspect ratio, and it
 * supports up to 8:1 either way. The first version of this only accepted squares and
 * therefore refused every one of the 397 DOS infantry strips, all 8,400 upload attempts,
 * while reporting a tidy count and drawing no soldiers at all. */
static int aspect_for(int w, int h, GrAspectRatio_t *ar, GrLOD_t *lod)
{
    int big = w > h ? w : h, small = w > h ? h : w, r;
    if (small <= 0 || !lod_for(big, lod)) return 0;
    r = big / small;
    if (big != small * r) return 0;              /* not a clean power-of-two ratio */
    if (w >= h)
    {
        switch (r) {
        case 1: *ar = GR_ASPECT_1x1; return 1;
        case 2: *ar = GR_ASPECT_2x1; return 1;
        case 4: *ar = GR_ASPECT_4x1; return 1;
        case 8: *ar = GR_ASPECT_8x1; return 1;
        }
    }
    else
    {
        switch (r) {
        case 2: *ar = GR_ASPECT_1x2; return 1;
        case 4: *ar = GR_ASPECT_1x4; return 1;
        case 8: *ar = GR_ASPECT_1x8; return 1;
        }
    }
    return 0;
}

/* The card's description of one texture, in one place.
 *
 * THE FORMAT FOLLOWS THE ART, not the caller. Everything from the cartridge and from the
 * 1995 CD is palettised and goes up as GR_TEXFMT_P_8, which is what makes a 256x256 page
 * cost 64 KB instead of 128. The 640x480 HUD is new true-colour art with no palette at
 * all, so it carries px16 and goes up as GR_TEXFMT_RGB_565. Four copies of this fill used
 * to be spelled out at the four call sites, which is three chances for the upload and the
 * bind to disagree about what is in texture memory; on this card that disagreement is
 * silent and draws garbage. */
static int tex_info(const SR_Texture *t, GrTexInfo *ti)
{
    GrLOD_t lod;
    GrAspectRatio_t ar;
    if (!aspect_for(t->w, t->h, &ar, &lod)) return 0;
    ti->smallLod = ti->largeLod = lod;
    ti->aspectRatio = ar;
    if (t->px16)        { ti->format = GR_TEXFMT_RGB_565; ti->data = (void *)t->px16; }
    /* An ALPHA plane: no palette, one byte of coverage per texel, colour taken from the
     * iterated vertex. The cartridge's shadow textures are all of this shape. */
    else if (t->alpha8) { ti->format = GR_TEXFMT_ALPHA_8; ti->data = (void *)t->alpha8; }
    else                { ti->format = GR_TEXFMT_P_8;     ti->data = (void *)t->idx;  }
    return 1;
}

int t1_glide_upload(SR_Texture *t, char *err, int errlen)
{
    GrTexInfo ti;
    FxU32 need;
    GrLOD_t lod;
    GrAspectRatio_t ar;

    if (t->guploaded) return 1;
    if (t->w > 256 || t->h > 256)
    { _snprintf(err, errlen, "%dx%d exceeds the Voodoo 2's 256x256 maximum", t->w, t->h); return 0; }
    if (!tex_info(t, &ti))
    { _snprintf(err, errlen, "%dx%d is not a power of two at up to 8:1", t->w, t->h); return 0; }
    lod = ti.largeLod;
    ar  = ti.aspectRatio;
    t->gaspect = (int)ar;
    /* Texels -> Glide's fixed 0..256 coordinate space. See SR_Texture.gscale. */
    t->gscale = 256.0f / (float)(t->w > t->h ? t->w : t->h);

    need = grTexCalcMemRequired(lod, lod, ar, ti.format);
    if (g_next + need > g_top)
    {
        /* The TMU is 4 MB and is NOT virtualised. Overrunning it does not fail loudly on
         * this hardware, it silently draws untextured, so it is caught here instead. */
        _snprintf(err, errlen, "TMU full: %lu bytes needed, %lu free of %lu",
                  (unsigned long)need, (unsigned long)(g_top - g_next),
                  (unsigned long)(g_top - grTexMinAddress(GR_TMU0)));
        return 0;
    }
    grTexDownloadMipMap(GR_TMU0, g_next, GR_MIPMAPLEVELMASK_BOTH, &ti);
    t->gaddr = g_next;
    t->guploaded = 1;
    g_next += need;
    return 1;
}

/* The depth WRITE alone. The shroud needs testing on and writing off: a translucent
 * quad that writes depth stops whatever is meant to show through it from drawing. */
void t1_glide_depth_write(int on) { grDepthMask(on ? FXTRUE : FXFALSE); }

/* LEQUAL for a pass that is CO-PLANAR with what it covers. The shroud is drawn on the
 * terrain's own corners, so under a strict LESS every one of its triangles fails against
 * the ground it is sitting on and nothing appears at all. */
void t1_glide_depth_lequal(int on)
{ grDepthBufferFunction(on ? GR_CMP_LEQUAL : GR_CMP_LESS); }

void t1_glide_depth(int on)
{
    grDepthBufferFunction(on ? GR_CMP_LESS : GR_CMP_ALWAYS);
    grDepthMask(on ? FXTRUE : FXFALSE);
}

void t1_glide_quad(float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1,
                   const SR_Texture *tex, float light)
{
    GrVertex v[4];
    int i;
    float lit = light * 255.0f;
    float ts;
    if (!tex || !tex->guploaded) return;
    ts = tex->gscale > 0.0f ? tex->gscale : 1.0f;
    u0 *= ts; u1 *= ts; v0 *= ts; v1 *= ts;

    /* ---- THE QUAD GUARD, and it is not optional ---------------------------------
     *
     * This function had NO bounds check at all, and the 2D layer it was written for
     * never needed one: the sidebar, the tab plates and the pointer are all at fixed
     * on-screen coordinates. The INFANTRY are not. A soldier is a billboard placed at
     * his projected position, and a soldier off the side of the view projects to tens
     * of thousands of pixels; the sprite's SIZE was clamped, which looked like enough,
     * but its POSITION never was.
     *
     * Handing the Voodoo a vertex that far out is the wedge that was seen: the picture stops
     * with the SLI pair's interleaved scanlines frozen and the process still running.
     * It reproduced only while SCROLLING, only with the infantry pass on, and only once
     * the two soldiers near the yard left the view, which is why three other passes were
     * suspected first.
     *
     * A quad whose box does not touch the framebuffer cannot colour a pixel, so dropping
     * it is exact rather than an approximation. Written as NOT-in-range so that a NaN,
     * which fails every comparison, is dropped too. */
    {
        float bx0 = x0 < x1 ? x0 : x1, bx1 = x0 < x1 ? x1 : x0;
        float by0 = y0 < y1 ? y0 : y1, by1 = y0 < y1 ? y1 : y0;
        const float M = 64.0f;
        if (!(bx1 > -M) || !(bx0 < g_scrw + M) ||
            !(by1 > -M) || !(by0 < g_scrh + M)) { ++g_rejects; return; }
    }
    if (lit < 0.0f) lit = 0.0f;
    if (lit > 255.0f) lit = 255.0f;

    if (tex != g_bound)
    {
        GrTexInfo ti;
        if (!tex_info(tex, &ti)) return;
        grTexSource(GR_TMU0, tex->gaddr, GR_MIPMAPLEVELMASK_BOTH, &ti);
        g_bound = tex;
    }

    v[0].x = x0; v[0].y = y0; v[0].tmuvtx[0].sow = u0; v[0].tmuvtx[0].tow = v0;
    v[1].x = x1; v[1].y = y0; v[1].tmuvtx[0].sow = u1; v[1].tmuvtx[0].tow = v0;
    v[2].x = x1; v[2].y = y1; v[2].tmuvtx[0].sow = u1; v[2].tmuvtx[0].tow = v1;
    v[3].x = x0; v[3].y = y1; v[3].tmuvtx[0].sow = u0; v[3].tmuvtx[0].tow = v1;
    for (i = 0; i < 4; ++i)
    {
        v[i].z = 1.0f; v[i].oow = 1.0f; v[i].ooz = 65535.0f;
        v[i].r = v[i].g = v[i].b = lit; v[i].a = 255.0f;
        v[i].tmuvtx[0].oow = 1.0f;      /* w = 1: no perspective on a screen quad */
    }
    grDrawTriangle(&v[0], &v[1], &v[2]);
    grDrawTriangle(&v[0], &v[2], &v[3]);
    g_pixels += (long)((x1 - x0) * (y1 - y0));
}

void t1_glide_reupload(const SR_Texture *t)
{
    GrTexInfo ti;
    if (!t->guploaded) return;
    if (!tex_info(t, &ti)) return;
    grTexDownloadMipMap(GR_TMU0, t->gaddr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    g_bound = 0;                      /* force a rebind: the contents moved under it */
}

void t1_glide_begin(unsigned int clear_rgb)
{
    g_pixels = 0;
    grBufferClear(clear_rgb, 0, GR_WDEPTHVALUE_FARTHEST);
}

/* ---- THE SWAP -----------------------------------------------------------------------
 *
 * ALWAYS INTERVAL 0 ON THIS HARDWARE. Both of the obvious ways to throttle it were tried
 * on the test box and both hang outright:
 *
 *   grBufferSwap(1)                  never returns. The driver waits on a retrace this
 *                                    SLI pair does not signal to it, and the game stops
 *                                    at frame zero with nothing on screen.
 *   spin on grBufferNumPending()     each poll is a driver call, and a spin bound
 *                                    generous enough to be safe is millions of them per
 *                                    frame, which is indistinguishable from a hang.
 *
 * Both are recorded here rather than deleted, because the queue-depth theory is the
 * obvious one to reach for when the card freezes and it costs an hour to rediscover that
 * neither remedy is available. The freeze had a different cause; see the degenerate
 * triangle reject in glide_tri.
 * ------------------------------------------------------------------------------------ */
static int g_vsync;
void t1_glide_vsync(int on) { g_vsync = on ? 1 : 0; }

void t1_glide_end(void) { grBufferSwap(0); }

int t1_glide_readback(unsigned short *out, int w, int h)
{
    if (!g_open) return 0;
    grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 0, 0, (FxU32)w, (FxU32)h, (FxU32)(w * 2), out);
    return 1;
}

long t1_glide_pixels(void) { return g_pixels; }
long t1_glide_rejects(void) { return g_rejects; }
unsigned int t1_glide_tmu_free(void) { return (g_top > g_next) ? (g_top - g_next) : 0; }

/* --------------------------------------------------------------------------- */



static long glide_tri(SR_Target *t, const SR_Vertex *a, const SR_Vertex *b,
                      const SR_Vertex *cc, const SR_Texture *tex, const T1_Screen *s)
{
    const SR_Vertex *in[3];
    GrVertex v[3];
    int i;
    float area;
    float ts;

    (void)t;
    if (!tex || !tex->guploaded) return 0;

    /* ------------------------------------------------------------------------------
     * THE GUARD. This is what stops the card wedging, and it is not optional.
     *
     * GLIDE DOES NOT CLIP. The Voodoo's triangle setup takes screen coordinates in a
     * limited fixed point range and it is the application's job to keep them there.
     * Hand it a vertex a few tens of thousands of pixels off the edge and the chip stops
     * rasterising, mid frame, with the SLI pair's interleaved scanlines frozen on screen
     * and the process still running. That was hit exactly by scrolling the camera.
     *
     * The software rasteriser never noticed because it clamps its scanline loop to the
     * target; the card has no such mercy.
     *
     * Two holes, both closed here:
     *
     *   1. The near reject was w <= 1e-3 CELLS, which is not a near plane, it is a
     *      divide-by-zero guard. At w = 0.01 a cell just off the side of the view
     *      projects past 40,000 pixels. The reject is now a real distance. Nothing
     *      visible is lost: the nearest ground this camera can see is 6.37 cells deep.
     *
     *   2. NaN passed straight through, because EVERY comparison against NaN is false,
     *      so `w <= 1e-3` did not catch it and 1/NaN then poisoned the whole vertex.
     *      The test is written as `!(w > NEAR)` on purpose, which IS true for NaN.
     * ------------------------------------------------------------------------------ */
/* NEAR IS ONE CELL, NOT A QUARTER, AND THAT IS A W-BUFFER CONSTRAINT.
 *
 * The card is in GR_DEPTHBUFFER_WBUFFER mode, where the depth unit consumes the vertex's
 * 1/w directly. Glide's own contract is that 1/w stays inside the unit interval: a vertex
 * nearer than w = 1 hands the depth unit a value it does not define behaviour for. A
 * quarter of a cell allowed 1/w up to FOUR.
 *
 * Nothing is lost by moving it out: the nearest ground this camera can see is over six
 * cells deep, and the ROM's own near plane is 0.1*D, about 1.17 cells. The old value was
 * a divide-by-zero guard wearing a near plane's name. */
#define T1_GLIDE_NEAR   1.0f       /* cells; keeps 1/w <= 1 for the W buffer */
#define T1_GLIDE_GUARD  1536.0f    /* pixels either side of the framebuffer */
    if (!(a->w > T1_GLIDE_NEAR) || !(b->w > T1_GLIDE_NEAR) || !(cc->w > T1_GLIDE_NEAR))
        { ++g_rejects; return 0; }

    if (tex != g_bound)
    {
        GrTexInfo ti;
        /* COUNTED, because it used to be invisible. A texture the card refused makes
         * every triangle bound to it vanish, and this early return was the one reject in
         * the whole function that did not touch the counter -- so the report could say
         * guard_rejects=0 while a seventh of the cursor set was being dropped. */
        if (!tex_info(tex, &ti)) { ++g_rejects; return 0; }
        grTexSource(GR_TMU0, tex->gaddr, GR_MIPMAPLEVELMASK_BOTH, &ti);
        g_bound = tex;
    }

    in[0] = a; in[1] = b; in[2] = cc;
    ts = tex->gscale > 0.0f ? tex->gscale : 1.0f;
    for (i = 0; i < 3; ++i)
    {
        const float oow = 1.0f / in[i]->w;
        float cr = in[i]->r * 255.0f;
        float cg = in[i]->g * 255.0f;
        float cb = in[i]->b * 255.0f;
        float alp = g_alpha ? g_alpha[i] * 255.0f : 255.0f;
        if (alp < 0.0f) alp = 0.0f;
        if (alp > 255.0f) alp = 255.0f;
        /* CLAMPED, because Glide wraps a component above 255 to near black instead of
         * saturating. That bug reads as "the bright thing went dark" and cost an earlier
         * project on this same box hours to find. */
        if (cr < 0.0f) cr = 0.0f; if (cr > 255.0f) cr = 255.0f;
        if (cg < 0.0f) cg = 0.0f; if (cg > 255.0f) cg = 255.0f;
        if (cb < 0.0f) cb = 0.0f; if (cb > 255.0f) cb = 255.0f;

        v[i].x   = s->cx + in[i]->x * s->fx * oow;
        v[i].y   = s->cy - in[i]->y * s->fy * oow;
        v[i].z   = 1.0f;
        v[i].oow = oow;
        /* CLAMPED to what a 16-bit depth field can hold. It is unused in W-buffer mode,
         * but handing a hardware register a quarter of a million where it takes 65535 is
         * not a thing to leave lying around next to a chip that wedges. */
        v[i].ooz = 65535.0f * oow;
        if (v[i].ooz > 65535.0f) v[i].ooz = 65535.0f;
        if (!(v[i].ooz >= 0.0f)) v[i].ooz = 0.0f;
        v[i].r = cr; v[i].g = cg; v[i].b = cb;
        v[i].a = alp;
        v[i].tmuvtx[0].sow = in[i]->u * ts * oow;
        v[i].tmuvtx[0].tow = in[i]->v * ts * oow;
        v[i].tmuvtx[0].oow = oow;

        /* Written as a NOT of the in-range test so a NaN that survived the near reject
         * still fails here rather than reaching the card. */
        if (!(v[i].x > -T1_GLIDE_GUARD) || !(v[i].x < T1_GLIDE_GUARD) ||
            !(v[i].y > -T1_GLIDE_GUARD) || !(v[i].y < T1_GLIDE_GUARD))
            { ++g_rejects; return 0; }
    }

    /* Culling is off on the card, so nothing has to be flipped, but the screen area is
     * still worth having: it is the only cheap way to know how much fill was asked for,
     * and a fill-bound renderer that cannot say how many pixels it submitted cannot be
     * tuned. Overdraw is included on purpose; that is what the card actually pays. */
    area = 0.5f * ((v[1].x - v[0].x) * (v[2].y - v[0].y)
                 - (v[2].x - v[0].x) * (v[1].y - v[0].y));
    if (area < 0.0f) area = -area;

    /* ---- THE DEGENERATE REJECT, which is what actually stops the card wedging --------
     *
     * A Voodoo's triangle setup divides by the signed area to get its gradients. Hand it
     * a triangle that is edge-on -- area a few thousandths of a pixel, vertices hundreds
     * of pixels apart -- and the reciprocal comes out enormous, the walk parameters
     * follow it, and the chip stops rasterising with the SLI pair's interleaved scanlines
     * frozen on screen and the process still running.
     *
     * WHY IT LOOKED LIKE A RATE BUG, which cost most of the debugging. A slower frame
     * makes the camera move FURTHER each step (the scroll is dt-scaled), so it visits
     * fewer distinct positions and steps straight over the one that produces the
     * degenerate triangle. Turning tracing up therefore "fixed" it, the shroud pass
     * "fixed" it by costing five milliseconds, and a still camera never hit it at all.
     * Every one of those pointed at the frame rate and none of them at the geometry.
     *
     * Sub-pixel triangles contribute nothing to the picture, so this is free. */
    if (!(area > 0.02f)) { ++g_rejects; return 0; }

    /* And a bound on the SPAN, not just on each vertex. Two vertices can each sit just
     * inside the guard band and still be four thousand pixels apart, which is past what
     * the setup unit's fixed point holds. Nothing legitimate here is that big: the
     * nearest ground this camera can see is over six cells deep, which puts a whole
     * terrain cell at about 130 pixels. */
    {
        float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y;
        for (i = 1; i < 3; ++i)
        {
            if (v[i].x < x0) x0 = v[i].x;
            if (v[i].x > x1) x1 = v[i].x;
            if (v[i].y < y0) y0 = v[i].y;
            if (v[i].y > y1) y1 = v[i].y;
        }
        if (!(x1 - x0 < 1536.0f) || !(y1 - y0 < 1536.0f)) { ++g_rejects; return 0; }
    }

    g_pixels += (long)area;
    grDrawTriangle(&v[0], &v[1], &v[2]);
    return (long)area;
}
