/* ====================================================================================
 * dosopt_gl.h -- the pause dialog, on the glass.
 *
 * dosopt.c rasterises the 1995 Options dialog into a 320x200 8-bit surface and knows
 * nothing else. This is the twenty lines that put that surface in front of the
 * tactical view: one palette conversion, one texture, one quad, in the same y-down
 * pixel ortho the sidebar draws in.
 *
 * Tier 1 note (Win98 / Voodoo 2): GL 1.1 only, one GL_NEAREST texture, one quad, and
 * the transparency is a plain alpha test on the DOS index 0, done on the CPU during
 * the palette conversion. Nothing here is Tier 2.
 *
 * Included by cnc_eyes.cpp AFTER cnc_sidebar.h, because it borrows that file's pack
 * (g_dbPack) rather than loading a second copy of the same art.
 * ==================================================================================== */

#ifndef DOSOPT_GL_H
#define DOSOPT_GL_H

extern "C" {
#include "dosopt.h"
#include "../menu/enhanced_menu.h"
}

#define OPT_TEX_POT 512   /* 320x200 rounded up to a power of two, for GL 1.1 */

static DOPT_State g_optState;
static bool       g_optOpen = false;
static bool       g_optRedAlert = false;
static bool       g_optEnhanced = false;
/* Title dialogs paint their own backdrop; pause dialogs preserve the battlefield. */
static bool       g_optOnTitle = false;
static GLuint     g_optTex = 0;
static unsigned char g_optScreen[DOPT_SCREEN_W * DOPT_SCREEN_H];
static unsigned char g_optRGBA[DOPT_SCREEN_W * DOPT_SCREEN_H * 4];
static DB_Surface g_optSurf;
/* The letterbox, recomputed every frame so a resize cannot leave it stale. Kept here
   because the hit test needs exactly the same numbers the draw used. */
static int g_optScale = 1, g_optX0 = 0, g_optY0 = 0;

static void opt_scroll(int delta)
{
    if (g_optEnhanced) em_opt_scroll(&g_optState, delta);
    else dopt_scroll(&g_optState, delta);
}

static void opt_layout(int fbw, int fbh)
{
    int sc = fbw / DOPT_SCREEN_W;
    if (fbh / DOPT_SCREEN_H < sc) sc = fbh / DOPT_SCREEN_H;
    g_optScale = sc < 1 ? 1 : sc;
    g_optX0 = (fbw - DOPT_SCREEN_W * g_optScale) / 2;
    g_optY0 = (fbh - DOPT_SCREEN_H * g_optScale) / 2;
}

/* Framebuffer pixels -> DOS pixels. Outside the plate the answer is deliberately far
   off it, so a click on the surrounding battlefield hits nothing in the dialog. */
static void opt_to_dos(int fx, int fy, int* dx, int* dy)
{
    if (g_optEnhanced && em_available()) { em_to_legacy(fx, fy, dx, dy); return; }
    *dx = (fx - g_optX0) / g_optScale;
    *dy = (fy - g_optY0) / g_optScale;
    if (fx < g_optX0 || fy < g_optY0) { *dx = -1000; *dy = -1000; }
}

static void opt_free(void)
{
    if (g_optTex) glDeleteTextures(1, &g_optTex);
    g_optTex = 0;
    g_optOpen = false;
}

/* Red Alert's title screen uses red controls over a dark radar grid. Keep the
   existing dialog geometry and hit boxes, but give its indexed pixels a Red
   Alert palette after rasterisation. This also makes the colour independent of
   the snow, temperate or interior mission palette used by the sidebar pack. */
static void opt_ra_palette(void)
{
    int x, y;
    for (y = 0; y < DOPT_SCREEN_H; y++) for (x = 0; x < DOPT_SCREEN_W; x++) {
        const int i = y * DOPT_SCREEN_W + x;
        const unsigned char c = g_optScreen[i];
        unsigned char *rgb = g_optRGBA + i * 4;
        switch (c) {
        case DB_BLACK:
            rgb[0] = (x % 24 == 0 || y % 18 == 0) ? 39 : 16;
            rgb[1] = 0; rgb[2] = 2; break;
        case DOPT_GREEN_SHADOW:
            rgb[0] = 66; rgb[1] = 0; rgb[2] = 0; break;
        case DOPT_GREEN_BKGD:
            rgb[0] = 122; rgb[1] = 0; rgb[2] = 0; break;
        case DOPT_LIGHT_GREEN:
            rgb[0] = 205; rgb[1] = 18; rgb[2] = 9; break;
        case DOPT_BRIGHT_GREEN:
            rgb[0] = 247; rgb[1] = 67; rgb[2] = 28; break;
        case DOPT_CC_GREEN:
            rgb[0] = 225; rgb[1] = 169; rgb[2] = 69; break;
        case DOPT_TEXT_MEDIUM:
            rgb[0] = 220; rgb[1] = 37; rgb[2] = 20; break;
        case DOPT_TEXT_BRIGHT:
            rgb[0] = 255; rgb[1] = 244; rgb[2] = 221; break;
        case DB_PINK: /* disabled label, separate from the gold caption */
            rgb[0] = 133; rgb[1] = 42; rgb[2] = 33; break;
        case DOPT_DIS_FILL:
            rgb[0] = 51; rgb[1] = 9; rgb[2] = 8; break;
        case DOPT_DIS_HILITE:
            rgb[0] = 103; rgb[1] = 35; rgb[2] = 27; break;
        default: break;
        }
    }
}

/* One frame of the dialog over whatever the tactical view just drew. The mission
   underneath is still on the glass: the 1995 dialog was drawn over Map.Render()'s
   output too, in the mission's own palette, which is why this uses the sidebar's
   pack rather than the menu's. */
static void opt_draw(int fbw, int fbh, int mouse_fx, int mouse_fy)
{
    if (!g_optOpen || !g_dbPack) return;
    g_optState.vis.drawable_w = fbw;
    g_optState.vis.drawable_h = fbh;
    /* The title shell draws in another translation unit and uploads its plate
       between visits. Its raw GL calls can replace the texture binding that our
       cache still associates with the cursor. Forget those answers before drawing
       this dialog, just as draw_frame does before returning from the shell. */
    gls_frame_begin();
    opt_layout(fbw, fbh);
    if (g_optEnhanced && em_available()) {
        em_title_mode(g_optOnTitle ? 1 : 0);
        em_options(&g_optState); em_viewport(fbw, fbh);
        int unused_x, unused_y; em_to_legacy(mouse_fx, mouse_fy, &unused_x, &unused_y);
        em_draw(fbw, fbh, g_optOnTitle ? 2 : 0); g_optScale = em_density(); return;
    }

    /* 1. rasterise. Index 0 is transparent, so the surface starts as all-transparent
          and only the dialog's own pixels end up opaque. */
    db_surface_init(&g_optSurf, DOPT_SCREEN_W, DOPT_SCREEN_H, g_optScreen);
    memset(g_optScreen, DB_TBLACK, sizeof(g_optScreen));
    dopt_draw(&g_optSurf, g_dbPack, &g_optState);
    /* NO cursor in here any more: the dialog used to rasterise its own DOS cursor
       into the plate while draw_frame's cursor pass also ran, and the two pointers
       (one at plate scale, one at framebuffer scale) were the reported "double cursor in
       the pause menu". The ONE cursor is the game's, drawn by the caller on top of
       this dialog (draw_cursor_top), as MOUSE_NORMAL, the way 1995 draws an arrow
       over a modal dialog. mouse_fx/fy stay in the signature: the hit test above
       still wants them. */
    (void)mouse_fx;
    (void)mouse_fy;

    /* 2. one conversion, with alpha: want_alpha makes index 0 come out fully
          transparent so the battlefield shows around the box. */
    db_surface_to_rgba(&g_optSurf, g_dbPack->pal8, g_optRGBA, 1);
    if (g_raModule || g_optRedAlert) opt_ra_palette();

    /* 3. one upload */
    if (!g_optTex) {
        std::vector<unsigned char> zero((size_t)OPT_TEX_POT * OPT_TEX_POT * 4, 0);
        glGenTextures(1, &g_optTex);
        glBindTexture(GL_TEXTURE_2D, g_optTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        /* NOT registered: UI art is never bilinear. See fx_filter.h. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OPT_TEX_POT, OPT_TEX_POT, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &zero[0]);
    }
    glBindTexture(GL_TEXTURE_2D, g_optTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DOPT_SCREEN_W, DOPT_SCREEN_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, g_optRGBA);

    /* 4. one quad */
    begin_overlay(fbw, fbh);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    {
        const float u1 = (float)DOPT_SCREEN_W / (float)OPT_TEX_POT;
        const float v1 = (float)DOPT_SCREEN_H / (float)OPT_TEX_POT;
        const float x0 = (float)g_optX0, y0 = (float)g_optY0;
        const float x1 = x0 + (float)(DOPT_SCREEN_W * g_optScale);
        const float y1 = y0 + (float)(DOPT_SCREEN_H * g_optScale);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
        glTexCoord2f(u1,   0.0f); glVertex2f(x1, y0);
        glTexCoord2f(u1,   v1);   glVertex2f(x1, y1);
        glTexCoord2f(0.0f, v1);   glVertex2f(x0, y1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    end_overlay();
}

#endif /* DOSOPT_GL_H */
