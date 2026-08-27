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
}

#define OPT_TEX_POT 512   /* 320x200 rounded up to a power of two, for GL 1.1 */

static DOPT_State g_optState;
static bool       g_optOpen = false;
static GLuint     g_optTex = 0;
static unsigned char g_optScreen[DOPT_SCREEN_W * DOPT_SCREEN_H];
static unsigned char g_optRGBA[DOPT_SCREEN_W * DOPT_SCREEN_H * 4];
static DB_Surface g_optSurf;
/* The letterbox, recomputed every frame so a resize cannot leave it stale. Kept here
   because the hit test needs exactly the same numbers the draw used. */
static int g_optScale = 1, g_optX0 = 0, g_optY0 = 0;

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

/* One frame of the dialog over whatever the tactical view just drew. The mission
   underneath is still on the glass: the 1995 dialog was drawn over Map.Render()'s
   output too, in the mission's own palette, which is why this uses the sidebar's
   pack rather than the menu's. */
static void opt_draw(int fbw, int fbh, int mouse_fx, int mouse_fy)
{
    if (!g_optOpen || !g_dbPack) return;
    opt_layout(fbw, fbh);

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
