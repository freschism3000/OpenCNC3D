/* ====================================================================================
 *  fx_filter.h -- ONE switch for texture filtering, over the whole program.
 *
 *  Nearest or bilinear, for everything drawn in the WORLD: the 3D pack textures and
 *  terrain atlas, the DOS infantry sprites, the tiberium, the decals and the
 *  construction scaffold.
 *
 *  IT DOES NOT TOUCH THE UI, and that is a reversal worth recording rather than
 *  quietly re-scoping. The first version covered the sidebar, the cameos, the fonts,
 *  the cursors, the pause dialog, the menus and the movies too, because the intent is
 *  one toggle over all of it. He then saw it: "UI / Menus should not be affected by
 *  Bilinear Filtering." Authored pixel art at 1:1 has nothing to gain from a filter and
 *  a great deal to lose, and the sidebar is 1995 art drawn at an integer zoom.
 *
 *  THE EXCLUSION IS BY OMISSION, which is the only kind that cannot rot: a UI texture
 *  is simply never handed to fx_filter_note, so it is not in the register and the
 *  switch has no way to reach it however it is flipped. There is no "is this UI?" test
 *  anywhere for somebody to get wrong later. Gate G36f asserts the sidebar does not
 *  move by one pixel while the world changes underneath it.
 *
 *  HOW IT REACHES TEXTURES THAT WERE UPLOADED AT BOOT. A filter is a property of the
 *  texture OBJECT, set once when the art is uploaded, so flipping a global at frame
 *  4000 would change nothing that already exists. Every upload site therefore does two
 *  things instead of one: it asks fx_filter_mode() what to set, and it hands the
 *  texture name to fx_filter_note(). Flipping the switch then walks the register and
 *  re-applies. That is the whole mechanism.
 *
 *  IT IS NOT A BLANKET GL_LINEAR. Each site passes the filter it wants when the
 *  switch is OFF, so with bilinear off the program sets exactly what it has always
 *  set -- including the handful of textures that are deliberately GL_LINEAR already
 *  (the infantry shadow blob, the particle sprites). Off means "the shipped picture",
 *  byte for byte, and that is what makes this safe to leave in the build.
 *
 *  TIER 1: a Voodoo 2 filters bilinearly in hardware and has done since 1996, so this
 *  is one of the few Tier 2 additions with a real Glide answer rather than a fallback.
 *  See docs/tier1-gap.md.
 *
 *  Plain C, no C++, because menu/dosmenu_shell.c and video/movieplay.c include it.
 * ==================================================================================== */
#ifndef CNC3D_FX_FILTER_H
#define CNC3D_FX_FILTER_H

/* NO GL HEADER OF ITS OWN, on purpose. Every file that includes this one has already
   set OpenGL up its own way -- the Apple framework path, the mingw compat shim, or
   <GL/gl.h> directly -- and a second opinion here would either collide with theirs or
   silently pick a different one. Include this AFTER the GL header. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Included by C files that use only two of these five. -Wall is on and a static
   function nobody calls is a warning, so say out loud that some callers use a subset
   rather than letting new warnings accumulate in a build whose output is read. */
#if defined(__GNUC__) || defined(__clang__)
#define FX_MAYBE_UNUSED __attribute__((unused))
#else
#define FX_MAYBE_UNUSED
#endif

#define FX_FILTER_MAX 1024

typedef struct FxFilterEntry { GLuint tex; GLint off_filter; } FxFilterEntry;

static FxFilterEntry fx_filter_reg[FX_FILTER_MAX];
static int  fx_filter_n = 0;
static int  fx_filter_full = 0;
static int  fx_filter_on = 0;         /* THE switch. 0 = the shipped picture. */

/* The A/B for the two corrections bilinear filtering needs: the alpha bleed below and
   the half-texel atlas inset in draw_terrain. --nobilfix turns BOTH off, which is what
   lets gate G36g prove itself -- it renders the same frame with and without them and
   asserts the fault appears in one and not the other, rather than testing a threshold
   somebody guessed. The same reason --noshade, --nocmtint and --novshadow exist. */
static int  fx_bilfix_on = 1;

/* What to pass to glTexParameteri right now. `off` is what this site wants when the
   switch is off, which is what it always used to pass. */
FX_MAYBE_UNUSED static GLint fx_filter_mode(GLint off)
{
    return fx_filter_on ? GL_LINEAR : off;
}

/* Remember a texture so the switch can reach it later. Called right after the two
   glTexParameteri lines, with the same `off` value they were given. */
FX_MAYBE_UNUSED static void fx_filter_note(GLuint tex, GLint off)
{
    int i;
    if (tex == 0) return;
    for (i = 0; i < fx_filter_n; i++)
        if (fx_filter_reg[i].tex == tex) { fx_filter_reg[i].off_filter = off; return; }
    if (fx_filter_n >= FX_FILTER_MAX) {
        /* LOUD, once. A register that silently stopped taking entries would show up as
           "the toggle works on some of the art", which is the worst kind of bug to
           chase: it looks like a texture problem and it is a bookkeeping problem. */
        if (!fx_filter_full) {
            fx_filter_full = 1;
            fprintf(stderr, "FX|filter|register FULL at %d textures; the bilinear "
                            "toggle will not reach anything uploaded after this\n",
                    FX_FILTER_MAX);
        }
        return;
    }
    fx_filter_reg[fx_filter_n].tex = tex;
    fx_filter_reg[fx_filter_n].off_filter = off;
    fx_filter_n++;
}

/* A texture object that has been deleted must not be re-bound later: its name can be
   handed back out to something else entirely. The movie player creates and destroys
   one per film, so this is not theoretical. */
FX_MAYBE_UNUSED static void fx_filter_forget(GLuint tex)
{
    int i;
    for (i = 0; i < fx_filter_n; i++)
        if (fx_filter_reg[i].tex == tex) {
            fx_filter_reg[i] = fx_filter_reg[--fx_filter_n];
            return;
        }
}

FX_MAYBE_UNUSED static int fx_filter_get(void) { return fx_filter_on; }

/* ---- Alpha bleed -----------------------------------------------------------------
   WHAT IT IS FOR. A transparent texel still has an RGB, and under GL_NEAREST plus an
   alpha test nobody ever sees it, so art pipelines are free to leave anything there.
   Ours do: smudge.pack carries MAGENTA (255,0,255) behind 9827 of its transparent
   texels, the DOS palette's own transparency key. Switch bilinear on and the filter
   averages that magenta into every texel along the edge of a scorch mark, which is
   exactly the purple outline photographed on the first run.

   THE FIX IS IN THE DATA, NOT THE FILTER. Flood the RGB of the nearest opaque texels
   outwards into the transparent ones, leaving alpha alone. The edge then averages a
   colour that belongs to the artwork instead of a key colour, and the alpha test still
   cuts in exactly the same place because alpha is untouched.

   It costs the nearest-neighbour path NOTHING and changes nothing about it: with
   GL_NEAREST plus an alpha test those texels are discarded, and under ordinary
   SRC_ALPHA blending their RGB is multiplied by an alpha of zero. So this runs
   unconditionally at load rather than being switched with the filter, which also means
   the toggle stays a pure filter change and cannot be blamed for a colour shift.

   Four passes: one texel of bleed is all a bilinear magnify can reach, three more are
   free at load time and cover minification. RGBA8 only -- LUMINANCE_ALPHA art (the
   infantry shadow blob) has no colour to bleed. */
FX_MAYBE_UNUSED static long fx_bleed_rgba(unsigned char *px, int w, int h, int passes)
{
    static const int DX[8] = { -1, 1,  0, 0, -1, 1, -1, 1 };
    static const int DY[8] = {  0, 0, -1, 1, -1,-1,  1, 1 };
    long filled_total = 0;
    unsigned char *solid;
    int pass, x, y, k;

    if (!fx_bilfix_on) return 0;
    if (!px || w < 2 || h < 2) return 0;
    solid = (unsigned char *)malloc((size_t)w * h);
    if (!solid) return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            solid[y * w + x] = px[((size_t)y * w + x) * 4 + 3] ? 1 : 0;

    for (pass = 0; pass < passes; pass++) {
        /* The frontier is computed against the state at the START of the pass, so the
           flood advances exactly one texel per pass instead of racing across the whole
           image in raster order and smearing the left edge over everything. */
        unsigned char *next = (unsigned char *)malloc((size_t)w * h);
        long filled = 0;
        if (!next) break;
        memcpy(next, solid, (size_t)w * h);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                unsigned r = 0, g = 0, b = 0, n = 0;
                if (solid[y * w + x]) continue;
                for (k = 0; k < 8; k++) {
                    const int nx = x + DX[k], ny = y + DY[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    if (!solid[ny * w + nx]) continue;
                    r += px[((size_t)ny * w + nx) * 4 + 0];
                    g += px[((size_t)ny * w + nx) * 4 + 1];
                    b += px[((size_t)ny * w + nx) * 4 + 2];
                    n++;
                }
                if (!n) continue;
                px[((size_t)y * w + x) * 4 + 0] = (unsigned char)(r / n);
                px[((size_t)y * w + x) * 4 + 1] = (unsigned char)(g / n);
                px[((size_t)y * w + x) * 4 + 2] = (unsigned char)(b / n);
                /* alpha is NOT touched. The cut stays where the artist put it. */
                next[y * w + x] = 1;
                filled++;
            }
        }
        free(solid);
        solid = next;
        filled_total += filled;
        if (!filled) break;
    }
    free(solid);
    return filled_total;
}

/* Flip it, and make every registered texture agree. Rebinds as it goes and leaves
   texture 0 bound, so no caller can be surprised by what is current afterwards. */
FX_MAYBE_UNUSED static void fx_filter_set(int on)
{
    int i;
    on = on ? 1 : 0;
    if (on == fx_filter_on) return;
    fx_filter_on = on;

    /* NOT ONE GL CALL WHEN THE REGISTER IS EMPTY, and this is not an optimisation.
       --gfx is parsed BEFORE the window and the context exist on the shipping binary,
       and a preset that sets bilinear lands here with nothing uploaded yet. An
       unconditional glBindTexture there is a segfault, which is exactly how this was
       found: the gate for "bilinear reaches the UI" crashed instead of failing.
       An empty register needs no work anyway -- every texture uploaded from here on
       asks fx_filter_mode() and gets the new answer. */
    if (fx_filter_n > 0) {
        for (i = 0; i < fx_filter_n; i++) {
            const GLint f = on ? GL_LINEAR : fx_filter_reg[i].off_filter;
            glBindTexture(GL_TEXTURE_2D, fx_filter_reg[i].tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    fprintf(stderr, "FX|filter|%s, %d textures re-filtered\n",
            on ? "bilinear" : "nearest", fx_filter_n);
}

#ifdef __cplusplus
}
#endif

#endif /* CNC3D_FX_FILTER_H */
