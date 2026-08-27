/* ====================================================================================
 *  edit_font.h -- the editor's text, drawn with a real face.
 *
 *  WHY. The panel used to borrow the game's FONT.SHP through sb_text: an 8x11
 *  fixed-cell bitmap, caps only, whole-number scales only. Every glyph the same width,
 *  nothing available between 1x and 2x, and "one step down" from body meant HALF. The
 *  result was reported three times as unreadable, because it was.
 *
 *  HOW. DejaVu Sans baked at a ladder of pixel sizes (tools/gen_editor_font.py ->
 *  edit_font_data.h) and drawn 1:1 -- never scaled. A call asks in the OLD units (the
 *  scale that meant "glyphs 11*s px tall"), the ladder snaps it to the nearest rung,
 *  and measuring and drawing snap identically, so the misalignment class the old font
 *  suffered -- measure at one scale, draw at another -- cannot exist here.
 *
 *  Alignment contract: ef_text(x, y, s, scale) places the CAPITAL'S CENTRE where the
 *  old font's capital centre sat (y + 5.2*scale), so the hundreds of existing
 *  "y + h*0.5 - 3*S" centrings in the panel keep working unchanged.
 *
 *  The GAME keeps FONT.SHP everywhere: the sidebar, the HUD, the menus. That font is
 *  the 1995 look and belongs there. This face is for the tool.
 * ==================================================================================== */

#ifndef EDIT_FONT_H
#define EDIT_FONT_H

#include "edit_font_data.h"

static GLuint ef_tex_reg[EF_N_REG];
static GLuint ef_tex_bold[EF_N_BOLD];

static GLuint ef_upload(const EfSize* sz)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    /* GL_ALPHA: the texture carries coverage only and glColor supplies the ink, which
       is exactly what MODULATE does with an alpha-only format on fixed function. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, sz->aw, sz->ah, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, sz->atlas);
    return t;
}

/* scale (old units) -> ladder index. Both weights share the mapping through their own
   ladders, so a bold title next to a body line sits on the same optical size. */
static int ef_pick(const EfSize* const* ladder, int n, float scale)
{
    const float want = 10.5f * scale;
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
        const float d = fabsf((float)ladder[i]->px - want);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static float ef_measure(const EfSize* sz, const char* s)
{
    float w = 0.0f;
    for (const char* p = s; *p; p++) {
        const unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) { w += sz->glyph[0].adv; continue; }
        w += sz->glyph[c - 32].adv;
    }
    return w;
}

static float ef_draw(const EfSize* sz, GLuint tex, float x, float y, const char* s,
                     float scale, float r, float g, float b)
{
    /* Cap-centre lands where FONT.SHP's cap-centre sat. lineTop is where PIL's draw
       origin was, so a glyph's stored offsets apply directly. */
    const float capCentre = y + 5.2f * scale;
    const float lineTop = capCentre - ((float)sz->ascent - (float)sz->caph * 0.5f);
    const float ty = floorf(lineTop + 0.5f);
    float tx = floorf(x + 0.5f);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, 1.0f);
    const float us = 1.0f / (float)sz->aw, vs = 1.0f / (float)sz->ah;
    glBegin(GL_QUADS);
    for (const char* p = s; *p; p++) {
        const unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) { tx += sz->glyph[0].adv; continue; }
        const EfGlyph* gl = &sz->glyph[c - 32];
        if (gl->w && gl->h) {
            const float gx = floorf(tx + (float)gl->xo + 0.5f);
            const float gy = ty + (float)gl->yo;
            const float u0 = (float)gl->ax * us, v0 = (float)gl->ay * vs;
            const float u1 = ((float)gl->ax + gl->w) * us;
            const float v1 = ((float)gl->ay + gl->h) * vs;
            glTexCoord2f(u0, v0); glVertex2f(gx, gy);
            glTexCoord2f(u1, v0); glVertex2f(gx + gl->w, gy);
            glTexCoord2f(u1, v1); glVertex2f(gx + gl->w, gy + gl->h);
            glTexCoord2f(u0, v1); glVertex2f(gx, gy + gl->h);
        }
        tx += gl->adv;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    return tx;
}

/* ---- the four calls the panel uses --------------------------------------------------
   Signatures mirror sb_text/sb_text_w/sb_text_fit exactly, so the swap in edit_mod.h is
   a rename and nothing else. */

static float ef_text(float x, float y, const char* s, float scale,
                     float r, float g, float b)
{
    const int i = ef_pick(ef_reg, EF_N_REG, scale);
    if (!ef_tex_reg[i]) ef_tex_reg[i] = ef_upload(ef_reg[i]);
    return ef_draw(ef_reg[i], ef_tex_reg[i], x, y, s, scale, r, g, b);
}

static float ef_textb(float x, float y, const char* s, float scale,
                      float r, float g, float b)
{
    const int i = ef_pick(ef_bold, EF_N_BOLD, scale);
    if (!ef_tex_bold[i]) ef_tex_bold[i] = ef_upload(ef_bold[i]);
    return ef_draw(ef_bold[i], ef_tex_bold[i], x, y, s, scale, r, g, b);
}

static float ef_text_w(const char* s, float scale)
{
    return ef_measure(ef_reg[ef_pick(ef_reg, EF_N_REG, scale)], s);
}

static float ef_textb_w(const char* s, float scale)
{
    return ef_measure(ef_bold[ef_pick(ef_bold, EF_N_BOLD, scale)], s);
}

/* Truncate with an ellipsis rather than let a long name run under its neighbour. */
static void ef_text_fit(float x, float y, const char* s, float scale, float maxw,
                        float r, float g, float b)
{
    if (ef_text_w(s, scale) <= maxw) { ef_text(x, y, s, scale, r, g, b); return; }
    char buf[256];
    snprintf(buf, sizeof buf, "%s", s);
    size_t n = strlen(buf);
    while (n > 1) {
        buf[--n] = 0;
        char probe[260];
        snprintf(probe, sizeof probe, "%s...", buf);
        if (ef_text_w(probe, scale) <= maxw) {
            ef_text(x, y, probe, scale, r, g, b);
            return;
        }
    }
    ef_text(x, y, "...", scale, r, g, b);
}

#endif /* EDIT_FONT_H */
