/*
 * t1_glide.h -- the 3dfx Voodoo 2 backend for tier1.
 *
 * Same scene, same camera, same textures; the triangles go to the card instead of to the
 * CPU. t1_glide_open() installs itself as t1_tri_hook, after which t1_terrain.c and
 * t1_mesh.c draw through the Voodoo without a line of change.
 *
 * Two things about this card shape the code and are worth stating up front.
 *
 * A VOODOO 2 IS 3D ONLY. It takes over the screen and its output never reaches the 2D
 * card, so there is no window, nothing to BitBlt, and nothing visible over VNC. The only
 * way to look at a frame is t1_glide_readback() into a buffer and a BMP on disk.
 *
 * GLIDE WRAPS COLOUR, IT DOES NOT CLAMP. An iterated colour component above 255 comes out
 * near black rather than saturated, which presents as geometry going dark exactly when it
 * is brightest. Every colour handed to the card here is clamped by the caller.
 */

#ifndef T1_GLIDE_H
#define T1_GLIDE_H

#include "softras.h"
#include "t1_cam.h"

/* Opens the card at 640x480 and installs the backend. 0 on failure, reason in err. */
int  t1_glide_open(int w, int h, char *err, int errlen);
void t1_glide_close(void);

/* The palette every subsequent upload is read through. One per texture BANK: call it
 * again between the terrain pass and the object pass. */
void t1_glide_palette(const unsigned char pal[768]);

/* Chroma-key: the card's answer to SR_Texture.ckey. On for the object pass whose palette
 * index 0 is transparent, OFF for the terrain whose index 0 is water. */
void t1_glide_ckey(int on, unsigned int rgb);

/* Filtering is per PASS, not global, and the reason is the chroma key.
 *
 * Bilinear is free on this card and the cartridge's own RDP filtered too, so it is the
 * faithful choice and the terrain uses it. But a filtered texel next to a transparent one
 * blends TOWARD the key colour and lands near it without matching it, so the key stops
 * working and the cutout edges fringe. On the object pass, which is the pass with the
 * cutouts, point sampling is used instead. That matches the software rasteriser exactly,
 * which also makes the two backends comparable frame for frame.
 *
 * The real fixes, neither taken yet, are in docs/tier1-gap.md: bleed the opaque colour
 * into the transparent texels at conversion time, or carry alpha with GR_TEXFMT_AP_88
 * and use an alpha test instead of a colour key. */
void t1_glide_filter(int bilinear);
/* The cartridge's one shadow state: colour from the iterated vertex alone, alpha from the
 * texture scaled by the vertex alpha. See the long note at the definition. */
void t1_glide_shadow_state(int on);
/* Texture clamp, for decals whose UVs overhang their own texture. */
void t1_glide_clamp(int on);

/* Uploads t->idx (t->w x t->h, power of two, at most 256x256) into texture memory and
 * fills in t->gaddr. Returns 0 if the TMU is full, which is a real limit: 4 MB, not
 * virtualised, and overrunning it silently draws untextured. */
int  t1_glide_upload(SR_Texture *t, char *err, int errlen);
void t1_glide_reset_textures(void);      /* rewind the TMU allocator */

/* Re-send an already-placed texture's pixels without moving it. The sidebar changes
 * every frame (credits, build clocks, text), so it is uploaded again rather than being
 * treated as static art. 64 KB a frame over PCI is a few percent, not a problem. */
void t1_glide_reupload(const SR_Texture *t);

/* A 2D screen-space textured quad, for the parts of the game that are not a world: the
 * DOS sidebar, the credits plate, the cursor. Drawn at w = 1 with depth testing off, so
 * it sits on top of whatever the 3D pass left and costs nothing to sort.
 *
 * A Voodoo 2 has no 2D output of its own, so on this card the sidebar is not a blit at
 * all. It is geometry like everything else. */
void t1_glide_quad(float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1,
                   const SR_Texture *tex, float light);

void t1_glide_depth(int on);
void t1_glide_depth_write(int on);
void t1_glide_depth_lequal(int on);

/* Alpha blending, and the three per-vertex alphas the next triangle will use. Both are
 * for the shroud; every other pass on this card is opaque. Pass NULL to t1_glide_alpha3
 * to go back to fully opaque, and do it as soon as the pass ends. */
void t1_glide_blend(int on);
void t1_glide_blend_add(int on);
void t1_glide_alpha3(const float *a3);

void t1_glide_begin(unsigned int clear_rgb);
void t1_glide_end(void);                 /* buffer swap */

/* Swap on the vertical retrace, and never let more than one swap be outstanding. ON by
 * default: with it off, a camera scrolling at 80 FPS wedges this SLI pair. Turn it off
 * only to measure raw throughput. */
void t1_glide_vsync(int on);

/* 640x480 of RGB565 straight off the front buffer. */
int  t1_glide_readback(unsigned short *out, int w, int h);

long t1_glide_pixels(void);              /* estimated pixels submitted this frame */

/* How many triangles the near/guard-band test threw away. If this is zero during a
 * camera sweep that used to wedge the card, the guard is not doing what it claims. */
long t1_glide_rejects(void);

/* Texture memory left, in bytes. The TMU is 4 MB and is NOT virtualised, so running out
 * is a real and silent failure: it draws untextured or not at all. */
unsigned int t1_glide_tmu_free(void);

#endif /* T1_GLIDE_H */
