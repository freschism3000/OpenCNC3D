/*
 * softras.h -- the Tier 1 software rasteriser.
 *
 * The CPU draws every pixel. There is no OpenGL and no Glide underneath this: it
 * writes 32-bit pixels into the DIB section w98_gfx.c puts on the screen, and it will
 * write into a Glide framebuffer later without changing shape.
 *
 * Design notes worth knowing before reading the code:
 *
 * - COLOUR IS PALETTISED ALL THE WAY THROUGH. Textures are 8-bit indices and lighting
 *   is a LOOKUP, not a multiply: sr_build_shade() bakes 32 brightness levels of the
 *   256 colour palette into one table, so a lit textured pixel costs one indexed load
 *   rather than three multiplies. That is how the 1990s software renderers did it and
 *   it is the difference between a playable frame rate and a slideshow on a Pentium.
 *   It also happens to be exactly what the art already is: the DOS sidebar, the
 *   cameos and the infantry are all 8-bit palette indices in the packs.
 *
 * - TEXTURING IS PERSPECTIVE CORRECT, subdivided. u/w, v/w and 1/w are interpolated
 *   linearly and the divide happens once every SR_SPAN pixels, with the texture
 *   coordinates stepped linearly in between. One divide per pixel would be correct and
 *   far too slow; pure affine would be fast and visibly wrong on anything large and
 *   near. Subdivision is the era's answer and it is the right trade here.
 *
 * - THERE IS NO NEAR PLANE CLIPPER YET. A triangle with any vertex at or behind the
 *   eye is rejected whole. That is recorded in known-gap notes rather than hidden,
 *   because the symptom when it finally matters (geometry vanishing at the screen
 *   edge as the camera gets close) is confusing if you do not know it is deliberate.
 *
 * Pure C89. No Win32, no CRT beyond string.h, so this file also compiles on the Mac
 * and can be gated there before it ever reaches the box.
 */

#ifndef SOFTRAS_H
#define SOFTRAS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SR_SPAN       8      /* pixels between perspective divides */
#define SR_SHADES    32      /* brightness levels in the shade table */
#define SR_ZFAR       1.0e9f

typedef struct
{
    int            w, h;
    unsigned int  *px;       /* 0x00RRGGBB, top down, borrowed from the platform */
    int           *zb;       /* 1/w as 12.20 fixed point, larger is nearer; may be NULL */
} SR_Target;

typedef struct
{
    const unsigned char *idx;   /* w*h palette indices */
    int w, h;
    int wmask, hmask, wshift;   /* dimensions MUST be powers of two */
    /* ckey: treat palette index 0 as a hole and skip the pixel. This is what the
     * cartridge's CUTOUT mode needs (348 of the mesh triangles) and what the 2D blit has
     * always done. It is a PER TEXTURE flag rather than a global rule because the terrain
     * deliberately uses index 0 as a real colour: its water holes are drawn, not skipped.
     * The test is hoisted to a local before the span loop, so the cost is one predictable
     * branch per pixel and nothing at all for opaque art. */
    int ckey;
    /* Glide handle. Meaningless to the software rasteriser and ignored by it; the Glide
     * backend fills it in at upload time so one texture handle serves both backends and
     * the scene code never learns which one is running. */
    unsigned int gaddr;
    int guploaded;
    int gaspect;
    /* THE GLIDE TEXTURE COORDINATE SCALE, and it is the single most expensive thing this
     * backend got wrong.
     *
     * A Voodoo's texture coordinates are NOT texels. They live in a fixed 0..256 space
     * that is stretched over whatever LOD is bound, so the same s of 8.0 means texel 8 of
     * a 256 wide texture and texel 0.5 of a 16 wide one. Everything in this renderer
     * hands the rasteriser texels, because that is what the software path wants and what
     * the packs store, so the backend has to convert.
     *
     * It went unnoticed for a fortnight because EVERY page-shaped texture in the game is
     * 256 across -- the terrain atlas, the tiberium and smudge sheets, the HUD pages, the
     * DOS infantry strips -- and for those the scale is exactly 1. The art that is NOT
     * page-shaped is the cartridge's own object textures, which are 16x16 and 32x32, and
     * those were drawing ONE TEXEL stretched over the whole polygon. That is why the
     * Construction Yard was a grey lump with no vents, why the sandbag walls were plain
     * brown bars, and why the effect sprites were coloured blobs.
     *
     * A single uniform 256/max(w,h) is the whole rule, and the non-square case is what
     * proves it: a 256x32 infantry strip needs scale 1 on BOTH axes (it draws correctly
     * today with raw texels), which rules out a per-axis 256/w, 256/h. Glide gives the
     * narrower dimension a proportionally shorter range, which is exactly what one
     * uniform scale produces.
     *
     * Filled in at upload, like gaddr. Meaningless to the software rasteriser. */
    float gscale;
    /* TRUE COLOUR, for art that was never palettised.
     *
     * The 640x480 HUD is new art with thousands of colours and no fixed palette, so it
     * cannot go through `idx` at all. When this is non-NULL the Glide backend uploads and
     * sources GR_TEXFMT_RGB_565 instead of GR_TEXFMT_P_8 and ignores `idx`; everything
     * else about the texture, including the aspect and LOD rules, is the same.
     *
     * The software rasteriser does NOT read this. It is palettised to its core (the shade
     * table is 256 entries wide) and a true-colour inner loop is a different renderer, so
     * a 16-bit texture simply does not draw there. That is registered in
     * docs/tier1-gap.md rather than faked with a nearest-colour lookup, which would make
     * the two backends disagree about what the HUD looks like. */
    const unsigned short *px16;
    /* AN ALPHA PLANE, for the cartridge's shadows and nothing else so far.
     *
     * Every shadow texture in the ROM is WHITE RGB with its coverage in the ALPHA
     * channel, and 34 of the 35 peak below 128 -- so the palettised conversion, whose
     * threshold is alpha >= 128, had already turned all of them fully transparent, and
     * the one exception would have drawn a solid white plate. There is no palette to put
     * them in: what matters is one byte of coverage per texel. Uploaded as
     * GR_TEXFMT_ALPHA_8, whose colour comes from the iterated vertex alone.
     *
     * The software rasteriser does not read this, for the same reason it does not read
     * px16: it is palettised to its core. Registered in docs/tier1-gap.md. */
    const unsigned char *alpha8;
} SR_Texture;

/* A vertex after the model and view transform but BEFORE the perspective divide.
 * x,y,z are eye space; w is the value to divide by (normally the eye space depth). */
typedef struct
{
    float x, y, z, w;
    float u, v;      /* texels, not normalised: 0..tex->w */
    float light;     /* 0 dark, 1 full bright. The SOFTWARE path's only colour term:
                      * it indexes the 32 level shade table, which is what makes a lit
                      * textured pixel one load instead of three multiplies. */
    float r, g, b;   /* the same light in three channels, for the CARD, which modulates
                      * a full RGB iterated colour for free. 17.5% of the cartridge's
                      * mesh vertices are NOT grey -- untextured wheels baked from the
                      * RDP's PRIM colour, muzzle flashes, coloured trim -- and every one
                      * of them used to desaturate on both backends because the scalar
                      * was the only thing carried. EVERY producer must set all four;
                      * there is no default, on purpose, so a new pass that forgets is
                      * obvious rather than subtly grey. */
} SR_Vertex;

/* The common case: a GREY vertex. One brightness into all four colour fields, so a
 * producer cannot set the scalar and silently leave the three channels at zero. Every
 * pass except the mesh bank uses this. */
#define SR_GREY(v, l) do { (v).light = (l); (v).r = (l); (v).g = (l); (v).b = (l); } while (0)

/* --- setup ------------------------------------------------------------------- */

void sr_bind(SR_Target *t, int w, int h, unsigned int *px, int *zb);

/* Builds the SR_SHADES x 256 lighting lookup from a 768 byte 8-bit RGB palette (the
 * pal8 the packs already carry). `out` must hold SR_SHADES * 256 unsigned ints. */
void sr_build_shade(const unsigned char *pal8, unsigned int *out);
void sr_use_shade(const unsigned int *shade);

void sr_texture(SR_Texture *t, const unsigned char *idx, int w, int h);
/* The same, for 16-bit RGB565 art. Glide only; see SR_Texture.px16. */
void sr_texture16(SR_Texture *t, const unsigned short *px, int w, int h);
/* The same, for an 8-bit ALPHA plane. Glide only; see SR_Texture.alpha8. */
void sr_texture_alpha(SR_Texture *t, const unsigned char *px, int w, int h);
void sr_texture_ckey(SR_Texture *t, int on);

/* --- clearing ---------------------------------------------------------------- */

void sr_clear(SR_Target *t, unsigned int rgb);
void sr_clear_depth(SR_Target *t);

/* --- 2D: the path the sidebar and every piece of DOS art takes ---------------- */

/* Blits 8-bit palette indices with index 0 transparent, scaled by an integer factor.
 * pal32 is 256 entries of 0x00RRGGBB. This is the whole 2D layer of the game. */
void sr_blit8(SR_Target *t, const unsigned char *src, int sw, int sh,
              const unsigned int *pal32, int dx, int dy, int scale, int transparent);

/* --- 3D ----------------------------------------------------------------------- */

/* Projects and rasterises one triangle. cx,cy are the screen centre and fx,fy the
 * focal lengths in pixels. Returns the number of pixels actually written, which is
 * how the fill rate gets measured rather than guessed. */
long sr_triangle(SR_Target *t, const SR_Vertex *a, const SR_Vertex *b,
                 const SR_Vertex *c, const SR_Texture *tex,
                 float cx, float cy, float fx, float fy);

#ifdef __cplusplus
}
#endif

#endif /* SOFTRAS_H */
