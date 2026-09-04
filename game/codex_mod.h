/* ====================================================================================
 *  codex_mod.h -- the DATABASE tab: a full-screen in-game codex of every buildable
 *  structure, soldier, vehicle and aircraft, with the real model turning in it.
 *
 *  WHERE THE TAB COMES FROM. The 1995 pre-release tab strip carried FOUR captions --
 *  TACTICAL, OPTIONS, DATABASE, S.DIGEST -- and shipped with one. This brings the second
 *  one back. It is not an invention: the plate is the same plate, lettered from the same
 *  GRAD6FNT at the same scale as OPTIONS (tools/sidebar_redesign/tabs.py), and it takes
 *  the 160-pixel slot immediately right of OPTIONS, which is the slot the 640 HUD has
 *  been leaving empty since it was laid out.
 *
 *  ENHANCED ONLY, and behind g_fx.enabled exactly like the movement queue in
 *  cnc_eyes.cpp -- there is no second switch for it. Classic is the 1995 game and the
 *  1995 game has no database.
 *
 *  HOW IT DRAWS, and why it is done this way. The page is rasterised into ONE 8-bit DOS
 *  surface and put on the glass as one texture and one quad. That is dosopt_gl.h's
 *  mechanism, verbatim, and the reason to copy it is the text: db_print gives the game's
 *  own 6POINT and 8POINT letterforms for nothing, so not one glyph on this screen is
 *  authored. The alternative -- GL quads plus a font atlas -- would have meant inventing
 *  letterforms for a screen that is mostly words.
 *
 *  The surface is rasterised only when something CHANGES (g_cxDirty). The three moving
 *  things are drawn over it afterwards in their own GL viewports: the model, the faction
 *  emblem and the EVA wordmark. So a still page costs one quad a frame.
 *
 *  IT PAUSES THE WORLD the way the pause dialog does, through the same gate in the tick
 *  loop, and for the same reason: reading is not playing. codex_holds_the_world() is the
 *  one place that decides, and it is written so the networked-multiplayer branch has a
 *  single line to change rather than a search to do.
 *
 *  WHAT IS AND IS NOT REAL DATA. Every number is the 1995 engine's own, decoded out of
 *  EA's GPL source into codex_table.h (see that file's header, and
 *  tools/database_codex/codex_data.py for the reader). The one thing on the page that is
 *  NOT 1995's is the description line: the manual is not in this repository, so the row
 *  carries a sentence derived from the entry's own fields and the design doc records the
 *  manual copy as owed. Nothing here paraphrases a manual nobody has read.
 *
 *  Included by cnc_eyes.cpp AFTER edit_mod.h, because it uses this file's draw_mesh,
 *  terrain_y, begin_overlay and the sidebar's pack.
 * ==================================================================================== */

#ifndef CODEX_MOD_H
#define CODEX_MOD_H

#include "codex_table.h"
extern "C" {
#include "../app/logo3d.h"
}

/* --------------------------------------------------------------------------- state */

static bool     g_cxOpen = false;
static int      g_cxCat = 0;
static int      g_cxSel[CODEX_CATS] = { 0, 0, 0, 0 };
static int      g_cxTop[CODEX_CATS] = { 0, 0, 0, 0 };
static unsigned g_cxHouse = CODEX_HOUSE_NOD;
/* The DRAW counter, not the engine's. The world is stopped while this is up, so the
   engine frame is standing still and cannot turn anything -- the same reason eui_coin
   spins off the editor's own counter. */
static unsigned long g_cxFrame = 0;
static bool     g_cxDirty = true;
static int      g_cxSlideWas = 0;   /* the drawer's position before we folded it away */

/* The page surface. POT because GL 1.1 wants it; see the Tier 1 note in
   docs/design-database-codex.md -- a Voodoo 2 caps at 256x256 and this page will need
   tiling there, which is the Win98 branch's problem and is registered in missing.md. */
#define CODEX_TEX_POT 1024
static int            g_cxW = 0, g_cxH = 0;
static unsigned char* g_cxPx = NULL;
static unsigned char* g_cxRGBA = NULL;
static GLuint         g_cxTex = 0;
static DB_Surface     g_cxSurf;

static Logo3D g_cxLogos;
static bool   g_cxLogosTried = false;

/* Layout in PAGE pixels, filled by codex_layout and read by both the rasteriser and the
   hit test, so a click can never land somewhere the picture is not. */
typedef struct {
    int   scale;                       /* page pixels -> framebuffer pixels */
    int   stripFb;                     /* the tab strip's height in FRAMEBUFFER pixels */
    int   boxX, boxY, boxW, boxH;      /* the page's own dialog box */
    int   catY, catH, catW, catGap;
    int   listX, listY, listW, listH, rowH, rows;
    int   detX, detY, detW, detH;
    int   viewX, viewY, viewW, viewH;
    int   embX, embY, embW, embH;      /* faction emblem, top left */
    int   evaX, evaY, evaW, evaH;      /* EVA wordmark, top right */
} CodexLayout;
static CodexLayout g_cxL;

/* ------------------------------------------------------------------------ the roster */

/* The rows of one category that the CURRENT house can field. Rebuilt on demand rather
   than cached: it is fifty rows and a strcmp, and a cache here would be one more thing
   to invalidate when the house changes. */
static int codex_rows(int cat, int* out, int max)
{
    int n = 0;
    for (int i = 0; i < CODEX_N && n < max; i++)
        if (CODEX[i].cat == cat && (CODEX[i].houses & g_cxHouse))
            out[n++] = i;
    return n;
}

static int codex_count(int cat)
{
    int buf[CODEX_N];
    return codex_rows(cat, buf, CODEX_N);
}

static int codex_current(void)
{
    int buf[CODEX_N];
    const int n = codex_rows(g_cxCat, buf, CODEX_N);
    if (n <= 0) return -1;
    int s = g_cxSel[g_cxCat];
    if (s < 0) s = 0;
    if (s >= n) s = n - 1;
    return buf[s];
}

/* ------------------------------------------------------------------------- geometry */

/* A mesh's bounding box in WORLD units, so the viewport camera can frame any model from
   a rifleman to a Temple without a per-entry number. Computed once per mesh from the
   pack's own triangles and cached; MODEL_SCALE is the same 1/1024 draw_mesh applies, so
   this is the box the draw will actually occupy.

   MODE_SHADOW TRIANGLES ARE EXCLUDED, and that is the whole of the difference between a
   model that sits in the middle of its window and one that does not. A structure carries
   baked shadow faces -- a flat sheet cast out across the ground beside it -- and this
   viewport does not draw them. Measuring them anyway inflated the box by the length of
   the shadow and dragged its centre off the building, so every structure came out small
   and parked in a corner. Measure what you draw. */
struct CodexBounds { float cx, cy, cz, r, dx, dy, dz; };
static std::map<int, CodexBounds> g_cxBounds;

static const CodexBounds* codex_bounds(int mi)
{
    std::map<int, CodexBounds>::iterator it = g_cxBounds.find(mi);
    if (it != g_cxBounds.end()) return &it->second;
    if (mi < 0 || mi >= (int)g_pack.mesh.size()) return NULL;
    const std::vector<PackTri>& tris = g_pack.mesh[mi].tris;
    if (tris.empty()) return NULL;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    bool any = false;
    for (size_t t = 0; t < tris.size(); t++) {
        if (tris[t].mode == MODE_SHADOW) continue;
        any = true;
        for (int k = 0; k < 3; k++) {
            const PackVert& v = tris[t].v[k];
            const float p[3] = { v.x, v.y, v.z };
            for (int a = 0; a < 3; a++) {
                if (p[a] < lo[a]) lo[a] = p[a];
                if (p[a] > hi[a]) hi[a] = p[a];
            }
        }
    }
    if (!any) return NULL;
    CodexBounds b;
    b.cx = (lo[0] + hi[0]) * 0.5f * MODEL_SCALE;
    b.cy = (lo[1] + hi[1]) * 0.5f * MODEL_SCALE;
    b.cz = (lo[2] + hi[2]) * 0.5f * MODEL_SCALE;
    const float dx = (hi[0] - lo[0]) * MODEL_SCALE;
    const float dy = (hi[1] - lo[1]) * MODEL_SCALE;
    const float dz = (hi[2] - lo[2]) * MODEL_SCALE;
    b.dx = dx; b.dy = dy; b.dz = dz;
    b.r = sqrtf(dx * dx + dy * dy + dz * dz) * 0.5f;
    if (b.r < 0.01f) b.r = 0.01f;
    g_cxBounds[mi] = b;
    return &g_cxBounds[mi];
}

static int codex_mesh_for(const char* code)
{
    std::map<std::string, PackType>::iterator it = g_pack.type.find(code);
    if (it == g_pack.type.end() || it->second.conf < 1) return -1;
    return it->second.mesh;
}

/* ------------------------------------------------------------------------ the layout */

/* THE PAGE IS LAID OUT IN A FIXED SPACE AND SCALED UP TO FILL THE WINDOW.
 *
 *  It used to take the HUD's own scale and lay itself out in whatever number of pixels
 *  that left, which is wrong in the one direction nobody tests on: a big display. Every
 *  size on this page is an absolute number of page pixels -- the list is 138 wide, a row
 *  is 10 tall -- so a page that gets LOGICALLY BIGGER with the window puts the same fixed
 *  content in a corner of it. On a 2560-wide screen the whole codex sat in the top left
 *  with two thirds of the screen black.
 *
 *  So the logical width is pinned at CODEX_PAGE_W and the scale is whatever multiple of
 *  it the window holds. The content is then the same fraction of the screen everywhere
 *  and the lettering grows with the display instead of shrinking against it. */
#define CODEX_PAGE_W 640

static void codex_layout(int fbw, int fbh)
{
    int sc = fbw / CODEX_PAGE_W;
    if (sc < 1) sc = 1;
    /* The page is ONE texture, so the logical size has a ceiling as well as a target. */
    while (fbw / sc > CODEX_TEX_POT || fbh / sc > CODEX_TEX_POT) sc++;
    g_cxL.scale = sc;
    const int W = fbw / sc, H = fbh / sc;

    if (W != g_cxW || H != g_cxH) {
        g_cxW = W; g_cxH = H;
        delete[] g_cxPx;   g_cxPx   = new unsigned char[(size_t)W * H];
        delete[] g_cxRGBA; g_cxRGBA = new unsigned char[(size_t)W * H * 4];
        g_cxDirty = true;
    }

    /* THE WHOLE PAGE IS ONE DIALOG BOX, the way the Options screen is one dialog box.
       It starts BELOW the tab strip and is inset from the window edges so the green
       border reads as a border and not as a screen edge.

       THE STRIP'S HEIGHT IS MEASURED, NOT ASSUMED. It is drawn at the HUD's scale and
       this page at its own, and the two are not the same number -- so `H6_TAB_H + 2`
       page pixels is the strip's height only by accident. Where they diverged the page's
       top edge rode UP behind the OPTIONS and DATABASE plates, with the green border and
       the faction emblem passing underneath them. Convert the strip's real framebuffer
       height into this page's pixels and round UP, so the box clears it whatever the two
       scales are doing. */
    g_cxL.stripFb = (int)((float)H6_TAB_H * g_h6Scale + 0.5f);
    const int top = (g_cxL.stripFb + sc - 1) / sc + 2;
    g_cxL.boxX = 4;
    g_cxL.boxY = top;
    g_cxL.boxW = W - 8;
    g_cxL.boxH = H - top - 4;

    /* The caption band: goptions.cpp puts the text at CAPTION_Y and rules it at
       CAPTION_Y + font height + spacing, and the two filigrees sit on row 11. Twenty
       rows clears all three. */
    const int capH = 26;

    /* The emblems sit in the caption band, INBOARD of the dialog's own two filigrees
       rather than on top of them. dopt_style_caption puts those at box x+12 and
       x+w-14; on a full-screen box that leaves a couple of hundred clear pixels each
       side of the centred caption, which is where these go. Placed over them, the GDI
       coin simply ate the left filigree. */
    const int emb = 36;
    g_cxL.embX = g_cxL.boxX + 32;   g_cxL.embY = g_cxL.boxY + 1;
    g_cxL.embW = emb;               g_cxL.embH = emb;
    g_cxL.evaW = emb * 2;           g_cxL.evaH = emb;
    g_cxL.evaX = g_cxL.boxX + g_cxL.boxW - 32 - g_cxL.evaW;
    g_cxL.evaY = g_cxL.boxY + 1;

    g_cxL.catY   = g_cxL.boxY + capH + 12;
    g_cxL.catH   = 13;
    g_cxL.catGap = 3;
    g_cxL.catW   = (g_cxL.boxW - 12 - g_cxL.catGap * (CODEX_CATS - 1)) / CODEX_CATS;

    const int bodyY = g_cxL.catY + g_cxL.catH + 5;
    const int bodyH = g_cxL.boxY + g_cxL.boxH - bodyY - 5;

    g_cxL.listX = g_cxL.boxX + 6;
    g_cxL.listY = bodyY;
    g_cxL.listW = 138;
    g_cxL.listH = bodyH;
    g_cxL.rowH  = 10;
    g_cxL.rows  = (g_cxL.listH - 6) / g_cxL.rowH;
    if (g_cxL.rows < 1) g_cxL.rows = 1;

    g_cxL.detX = g_cxL.listX + g_cxL.listW + 5;
    g_cxL.detY = bodyY;
    g_cxL.detW = g_cxL.boxX + g_cxL.boxW - g_cxL.detX - 6;
    g_cxL.detH = bodyH;

    g_cxL.viewX = g_cxL.detX + 4;
    g_cxL.viewY = g_cxL.detY + 4;
    g_cxL.viewW = g_cxL.detW * 42 / 100;
    /* The well takes the room the stats do not. The stats are four gauge rows, a gap and
       up to three two-line relation rows -- about 125 page pixels at their worst -- and
       what is left goes to the model, which is the thing worth looking at. */
    g_cxL.viewH = g_cxL.detH * 56 / 100;
    if (g_cxL.viewH > g_cxL.detH - 125) g_cxL.viewH = g_cxL.detH - 125;
    if (g_cxL.viewH < 60) g_cxL.viewH = 60;
}

/* ------------------------------------------------------- rasterising the page
 *
 *  EVERY MARK ON THIS PAGE IS THE PAUSE DIALOG'S OWN, because the requirement is that
 *  this screen match the game's other menus: the same green, the same font, the same
 *  borders and frames.
 *
 *  So the four widgets come from dosopt.c through dosopt.h -- dopt_style_dialog,
 *  dopt_style_caption, dopt_style_plate and dopt_style_track -- rather than from a
 *  second copy of the same five lines here. The colours are its DOPT_ constants, which
 *  are the 1995 CC_ palette entries with the defines.h line numbers beside them. The
 *  font is GRAD6FNT through db_font_palette_grad, which is what every one of those
 *  screens prints with; the first cut of this page used 6POINT and 8POINT through the
 *  full-shadow palette, which is the SIDEBAR's idiom and reads as a different program.
 *
 *  THE PAGE IS MONOCHROME GREEN ON PURPOSE. The first cut coloured hitpoints green,
 *  damage red, speed blue and the weakness list red, which is informative and is not
 *  what a 1995 C&C dialog does: those screens carry one hue and separate things by
 *  BRIGHTNESS -- DOPT_TEXT_MEDIUM for a label, DOPT_TEXT_BRIGHT for its value. Meaning
 *  is carried by the label, not by the hue.
 */

/* A stat bar, which is the volume slider's own gauge. The fill is a fraction of that
   stat's maximum ACROSS THE WHOLE TABLE (CODEX_MAX_*, computed at generation time),
   which is what makes two entries comparable by eye; the printed figure beside it is
   always the raw one. */
static void codex_bar(DB_Surface* s, int x, int y, int w, int h, float frac)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    dopt_style_track(s, x, y, w, h, x + (int)((float)(w - 2) * frac + 0.5f));
}

/* Word wrap in the FONT'S OWN METRICS, so a line breaks where the game would break it
   rather than where a guess at character width would. */
static int codex_wrap_print(DB_Surface* s, const DB_Font* f, const char* text,
                            int x, int y, int maxw, int lineh, int maxlines,
                            const unsigned char* fp)
{
    char line[256];
    int lines = 0;
    const char* p = text;
    while (*p && lines < maxlines) {
        int n = 0, lastSpace = -1;
        while (p[n] && n < (int)sizeof(line) - 1) {
            line[n] = p[n];
            line[n + 1] = 0;
            if (p[n] == ' ') lastSpace = n;
            if (db_string_width(f, line, DB_FONT6_XSPACING) > maxw) {
                if (lastSpace > 0) n = lastSpace;
                break;
            }
            n++;
        }
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        db_print(s, f, line, x, y + lines * lineh, fp, DB_FONT6_XSPACING);
        lines++;
        p += n;
        while (*p == ' ') p++;
    }
    return lines * lineh;
}

/* A label in the medium green with its value in the bright one, which is the pairing
   every row of the Game Controls page uses. */
static void codex_stat(DB_Surface* s, const DB_Font* f,
                       const unsigned char* fpDim, const unsigned char* fpLit,
                       int x, int y, int labw, int barw,
                       const char* label, int value, int maxv, int invert,
                       const char* unit)
{
    db_print(s, f, label, x, y, fpDim, DB_FONT6_XSPACING);
    if (value < 0) {
        db_print(s, f, "--", x + labw, y, fpDim, DB_FONT6_XSPACING);
        return;
    }
    float frac = (maxv > 0) ? (float)value / (float)maxv : 0.0f;
    /* ATTACK SPEED READS BACKWARDS. The engine's number is the tick DELAY between shots,
       so a small figure is a fast weapon; the gauge is drawn from its complement so a
       long bar means fast, and the printed figure stays the engine's own. */
    if (invert) frac = 1.0f - frac;
    codex_bar(s, x + labw, y - 1, barw, 8, frac);
    char buf[32];
    snprintf(buf, sizeof buf, "%d%s", value, unit ? unit : "");
    db_print(s, f, buf, x + labw + barw + 4, y, fpLit, DB_FONT6_XSPACING);
}

static void codex_raster(void)
{
    if (!g_dbPack || !g_cxPx) return;
    /* GRAD6FNT, which is the ONLY font any of these screens prints with: dosopt.c asks
       for it ten times and for nothing else. */
    const DB_Font* f = db_font(g_dbPack, "GRAD6FNT");
    if (!f) return;

    DB_Surface* s = &g_cxSurf;
    db_surface_init(s, g_cxW, g_cxH, g_cxPx);
    /* Index 0 is transparent, so outside the box the darkened battlefield shows through
       exactly as it does around the pause dialog. */
    memset(g_cxPx, DB_TBLACK, (size_t)g_cxW * g_cxH);

    unsigned char fpDim[16], fpLit[16], fpOff[16];
    db_font_palette_grad(fpDim, DOPT_TEXT_MEDIUM, DB_TBLACK);
    db_font_palette_grad(fpLit, DOPT_TEXT_BRIGHT, DB_TBLACK);
    /* The disabled ramp, taken the way dopt_draw_button takes it: every nibble class
       above 3 flattened onto one ungraded green. */
    {
        int i;
        for (i = 0; i < 16; i++) fpOff[i] = (i >= 4) ? DOPT_TEXT_DISABLED : DB_TBLACK;
    }

    /* ---- the page, as one dialog box with its filigreed caption ---- */
    dopt_style_dialog(s, g_cxL.boxX, g_cxL.boxY, g_cxL.boxW, g_cxL.boxH);
    dopt_style_caption(s, g_dbPack, "Database", g_cxL.boxX, g_cxL.boxY, g_cxL.boxW);
    {
        const char* sub = (g_cxHouse == CODEX_HOUSE_NOD)
                        ? "Brotherhood of Nod Field Archive"
                        : "Global Defense Initiative Field Archive";
        const int w = db_string_width(f, sub, DB_FONT6_XSPACING);
        db_print(s, f, sub, g_cxL.boxX + (g_cxL.boxW - w) / 2, g_cxL.boxY + 22,
                 fpDim, DB_FONT6_XSPACING);
    }

    /* ---- the horizontal axis, as green button plates ---- */
    for (int i = 0; i < CODEX_CATS; i++) {
        const int x = g_cxL.boxX + 6 + i * (g_cxL.catW + g_cxL.catGap);
        const int on = (i == g_cxCat);
        dopt_style_plate(s, x, g_cxL.catY, g_cxL.catW, g_cxL.catH, on, 0);
        const char* label = CODEX_CAT_NAME[i];
        const int w = db_string_width(f, label, DB_FONT6_XSPACING);
        db_print(s, f, label, x + (g_cxL.catW - w) / 2, g_cxL.catY + 2,
                 on ? fpLit : fpDim, DB_FONT6_XSPACING);
    }

    /* ---- the vertical axis ---- */
    dopt_style_dialog(s, g_cxL.listX, g_cxL.listY, g_cxL.listW, g_cxL.listH);
    {
        int buf[CODEX_N];
        const int n = codex_rows(g_cxCat, buf, CODEX_N);
        int sel = g_cxSel[g_cxCat];
        if (sel >= n) sel = n > 0 ? n - 1 : 0;
        int top = g_cxTop[g_cxCat];
        if (sel < top) top = sel;
        if (sel >= top + g_cxL.rows) top = sel - g_cxL.rows + 1;
        if (top > n - g_cxL.rows) top = n - g_cxL.rows;
        if (top < 0) top = 0;
        g_cxTop[g_cxCat] = top;
        g_cxSel[g_cxCat] = sel;

        /* CLIPPED TO THE BOX. "Communications Center" and "Advanced Com. Center" are
           longer than any list column this page can afford, and db_print does not
           truncate -- it printed them straight out through the green border and across
           the model. The clip is the engine's own, and it is reset immediately after. */
        db_clip(s, g_cxL.listX + 2, g_cxL.listY + 1,
                g_cxL.listX + g_cxL.listW - 5, g_cxL.listY + g_cxL.listH - 2);
        for (int i = 0; i < g_cxL.rows && top + i < n; i++) {
            const CodexRow* r = &CODEX[buf[top + i]];
            const int y = g_cxL.listY + 4 + i * g_cxL.rowH;
            const int on = (top + i == sel);
            /* The sound-track list's own selected row: filled with the button plate's
               background green, no border, and the text steps up to bright. */
            if (on)
                db_fill_rect(s, g_cxL.listX + 2, y - 1,
                             g_cxL.listX + g_cxL.listW - 3, y + g_cxL.rowH - 3,
                             DOPT_GREEN_BKGD);
            /* TRUNCATED WITH TWO DOTS, not sliced by the clip. The clip is still set
               (a stray glyph must never reach the border) but "Mobile Construction Ya"
               reads as a bug where "Mobile Construction.." reads as a long name. Same
               device edit_font.h's ef_text_fit uses. */
            char nm[64];
            snprintf(nm, sizeof nm, "%s", r->name);
            const int room = g_cxL.listW - 10;
            if (db_string_width(f, nm, DB_FONT6_XSPACING) > room) {
                size_t k = strlen(nm);
                while (k > 2) {
                    nm[--k] = 0;
                    char probe[68];
                    snprintf(probe, sizeof probe, "%s..", nm);
                    if (db_string_width(f, probe, DB_FONT6_XSPACING) <= room) {
                        snprintf(nm, sizeof nm, "%s..", probe[0] ? nm : "");
                        break;
                    }
                }
            }
            db_print(s, f, nm, g_cxL.listX + 5, y, on ? fpLit : fpDim,
                     DB_FONT6_XSPACING);
        }
        db_clip_reset(s);
        /* The scroll thumb, so a list that runs past the box says so. */
        if (n > g_cxL.rows) {
            const int track = g_cxL.listH - 8;
            const int th = track * g_cxL.rows / n;
            const int ty = g_cxL.listY + 4 + track * top / n;
            db_fill_rect(s, g_cxL.listX + g_cxL.listW - 4, ty,
                         g_cxL.listX + g_cxL.listW - 3, ty + (th > 2 ? th : 2),
                         DOPT_BRIGHT_GREEN);
        }
    }

    /* ---- the entry ---- */
    const int ci = codex_current();
    dopt_style_dialog(s, g_cxL.detX, g_cxL.detY, g_cxL.detW, g_cxL.detH);
    if (ci < 0) return;
    const CodexRow* r = &CODEX[ci];

    /* the viewport well, framed the same way. The model is drawn over this in GL. */
    dopt_style_dialog(s, g_cxL.viewX, g_cxL.viewY, g_cxL.viewW, g_cxL.viewH);

    const int tx = g_cxL.viewX + g_cxL.viewW + 8;
    const int tw = g_cxL.detX + g_cxL.detW - 6 - tx;

    db_print(s, f, r->name, tx, g_cxL.detY + 6, fpLit, DB_FONT6_XSPACING);
    {
        char line[96];
        if (r->cost >= 0) snprintf(line, sizeof line, "%s   $%d", r->code, r->cost);
        else              snprintf(line, sizeof line, "%s", r->code);
        db_print(s, f, line, tx, g_cxL.detY + 16, fpDim, DB_FONT6_XSPACING);
        db_line_h(s, tx, tx + tw, g_cxL.detY + 26, DOPT_GREEN_SHADOW);
    }
    {
        const int used = codex_wrap_print(s, f, r->brief, tx, g_cxL.detY + 30, tw, 9, 8,
                                          fpDim);
        /* RANGE IS NOT A GAUGE. Every other stat is a quantity to compare; range is a
           distance in cells and the player reads it against the map. */
        int dy = g_cxL.detY + 30 + used + 6;
        char line[96];
        if (r->range256 > 0) {
            snprintf(line, sizeof line, "Range");
            db_print(s, f, line, tx, dy, fpDim, DB_FONT6_XSPACING);
            snprintf(line, sizeof line, "%.1f cells", r->range256 / 256.0f);
            db_print(s, f, line, tx + 46, dy, fpLit, DB_FONT6_XSPACING);
            dy += 10;
        }
        if (r->armour && r->armour[0]) {
            db_print(s, f, "Armour", tx, dy, fpDim, DB_FONT6_XSPACING);
            db_print(s, f, r->armour, tx + 46, dy, fpLit, DB_FONT6_XSPACING);
        }
    }

    /* ---- the stats, under the well and running the panel's full width ---- */
    int y = g_cxL.viewY + g_cxL.viewH + 8;
    const int sx = g_cxL.detX + 6;
    /* 80, not the 66 the 6POINT layout used: GRAD6FNT is a wider face and "Attack Speed"
       ran under its own gauge at 66. Measured against the longest label on the page. */
    const int labw = 80;
    const int barw = 74;
    codex_stat(s, f, fpDim, fpLit, sx, y, labw, barw, "Hit Points",
               r->hitpoints, CODEX_MAX_HP, 0, "");
    y += 11;
    codex_stat(s, f, fpDim, fpLit, sx, y, labw, barw, "Damage",
               r->damage, CODEX_MAX_DAMAGE, 0, "");
    y += 11;
    codex_stat(s, f, fpDim, fpLit, sx, y, labw, barw, "Speed",
               r->speed, CODEX_MAX_SPEED, 0, " mph");
    y += 11;
    codex_stat(s, f, fpDim, fpLit, sx, y, labw, barw, "Attack Speed",
               r->rof, CODEX_MAX_ROF, 1, " ticks");
    y += 15;

    const int relw = g_cxL.detW - 12 - labw;
    struct { const char* label; const char* text; } REL[3] = {
        { "Strong vs", r->strong },
        { "Weak vs",   r->weak },
        { "Unlocks",   r->unlocks },
    };
    for (int i = 0; i < 3; i++) {
        const int any = REL[i].text && REL[i].text[0];
        if (!any && i == 2) continue;            /* nothing unlocks: say nothing */
        if (y + 9 > g_cxL.detY + g_cxL.detH - 6) break;
        db_print(s, f, REL[i].label, sx, y, fpDim, DB_FONT6_XSPACING);
        y += codex_wrap_print(s, f, any ? REL[i].text : "nothing in the table",
                              sx + labw, y, relw, 9, 2, any ? fpLit : fpOff);
        y += 2;
    }
}

/* ------------------------------------------------------------------- 
/* ------------------------------------------------------------------- the 3D viewport */

/* Frame one mesh in its own GL viewport. draw_mesh works in world space and stands the
   model on terrain_y under its anchor, so the ylift here cancels that ground back out
   and the model sits at the origin whatever map is loaded underneath. Everything the
   world pass leaves set -- the shadow flag, the tint, the alpha -- is off; the model is
   drawn exactly the way the battlefield draws it, which is the point. */
static void codex_draw_model(int mi, int fx, int fy, int fw, int fh, int fbh,
                             float yaw, int house)
{
    const CodexBounds* b = codex_bounds(mi);
    if (!b || fw < 4 || fh < 4) return;

    glViewport(fx, fbh - (fy + fh), fw, fh);
    glScissor(fx, fbh - (fy + fh), fw, fh);
    glEnable(GL_SCISSOR_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    const float asp = (float)fw / (float)fh;
    const float fov = 0.62f;
    /* FIT THE BOX, NOT ITS BOUNDING SPHERE. A sphere around a squat building is mostly
       air, so framing by its radius leaves a Power Plant sitting in the middle of an
       empty well. The horizontal extent used is the DIAGONAL, because the camera orbits
       and a long building must still fit when it is side on; the vertical is the height
       plus what the camera's own pitch adds to it. 1.12 is the margin. */
    const float halfW = sqrtf(b->dx * b->dx + b->dz * b->dz) * 0.5f;
    const float pitch = 0.42f;
    const float halfH = b->dy * 0.5f * cosf(pitch) + halfW * sinf(pitch);
    float need = halfH;
    if (halfW / asp > need) need = halfW / asp;
    const float d = (need / tanf(fov * 0.5f)) * 1.22f;
    const float znear = b->r * 0.05f, zfar = d + b->r * 8.0f;
    const float f = 1.0f / tanf(fov * 0.5f);
    const float proj[16] = {
        f / asp, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (zfar + znear) / (znear - zfar), -1,
        0, 0, 2.0f * zfar * znear / (znear - zfar), 0
    };

    /* THE CAMERA ORBITS, THE MODEL DOES NOT TURN. Spinning it through draw_mesh's own
       `face` was the first try and it is wrong for a structure: a building's anchor is a
       corner of its footprint, not its middle, so the model swings round the room
       instead of turning on the spot. An orbiting camera is centred on the bounding box
       and behaves the same for a rifleman, a tank and a Temple. */
    /* The model is MOVED so its box centre lands on the origin, rather than the camera
       being aimed at wherever the centre happens to be. draw_mesh's cx/cz/ylift are a
       translation and this is what they are for; aiming instead works out the same for a
       static mesh and stops working the moment anything rotates. */
    const float ground = terrain_y(0.0f, 0.0f);
    const float ex = d * cosf(pitch) * sinf(yaw);
    const float ey = d * sinf(pitch);
    const float ez = d * cosf(pitch) * cosf(yaw);
    float zx = ex, zy = ey, zz = ez;
    float L = sqrtf(zx * zx + zy * zy + zz * zz); if (L <= 0.0f) L = 1.0f;
    zx /= L; zy /= L; zz /= L;
    /* x = cross(up, z) with up = (0,1,0) is (zz, 0, -zx). Writing it with the signs the
       other way round flips the basis and turns the picture through 180 degrees, which
       reads as a model that is upside down for no reason a reader could see. */
    float xx = zz, xy = 0.0f, xz = -zx;
    L = sqrtf(xx * xx + xz * xz); if (L <= 0.0f) L = 1.0f;
    xx /= L; xz /= L;
    const float yx = zy * xz - zz * xy;
    const float yy = zz * xx - zx * xz;
    const float yz = zx * xy - zy * xx;
    const float view[16] = {
        xx, yx, zx, 0,
        xy, yy, zy, 0,
        xz, yz, zz, 0,
        -(xx * ex + xy * ey + xz * ez),
        -(yx * ex + yy * ey + yz * ez),
        -(zx * ex + zy * ey + zz * ez), 1
    };

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(view);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    /* THE SHROUD DOES NOT REACH INTO A REFERENCE BOOK, and this is the one line that
       stops the page being useless. draw_mesh subtracts the shroud envelope sampled at
       each vertex's WORLD position, and this draw stands the model at world (0,0) --
       a corner of the map that in a real mission is unexplored, so every vertex came
       back at zero and the model rendered as a black silhouette on a black plate. It
       looked like a lighting bug and it is a location bug. The cursor already opts out
       through this same flag; the codex is the second caller with the same reason.
       g_meshGroundPerVertex goes off with it: per-vertex ground is a terrain query, and
       sampling terrain across a model parked at the map corner warps it. */
    const bool shroudWas = g_meshNoShroud;
    const bool groundWas = g_meshGroundPerVertex;
    g_meshNoShroud = true;
    g_meshGroundPerVertex = false;

    const float ox = -b->cx, oz = -b->cz;
    const float lift = -ground - b->cy;
    draw_mesh(mi, ox, oz, -1, MODE_OPAQUE, 0, 0.0f, house, 1.0f, lift);
    glEnable(GL_TEXTURE_2D);
    draw_mesh(mi, ox, oz, -1, MODE_CUTOUT, 0, 0.0f, house, 1.0f, lift);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    draw_mesh(mi, ox, oz, -1, MODE_XLU, 0, 0.0f, house, 1.0f, lift);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    g_meshNoShroud = shroudWas;
    g_meshGroundPerVertex = groundWas;

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glDisable(GL_SCISSOR_TEST);
}

/* THE INFANTRY HAVE NO MESH, and that is the cartridge's doing rather than a gap in the
   bake: this project draws its infantry from the 1995 MS-DOS art (dosinfantry.pack), and
   the N64 billboards it replaced were billboards too. So an infantry entry gets a
   TURNTABLE OF THE EIGHT BAKED FACINGS instead of an orbit -- the same rotation, out of
   the art that exists, stepped in the engine's own facing order. */
static bool codex_draw_infantry(const char* code, int px, int py, int pw, int ph,
                                int scale, float turn)
{
    std::map<std::string, DosInfType>::iterator ti = g_dosInfTypes.find(code);
    if (ti == g_dosInfTypes.end()) return false;
    const int hs = (g_cxHouse == CODEX_HOUSE_NOD) ? SPRH_NOD : SPRH_GDI;
    int si = ti->second.strip[hs][DA_STAND];
    if (si < 0) si = ti->second.strip[0][DA_STAND];
    if (si < 0) return false;
    const DosStrip& sp = g_dosStrips[si];

    int facenum = (int)turn % 8;
    if (facenum < 0) facenum += 8;
    int frame = facenum * sp.stages;
    if (frame >= sp.frames) frame = sp.frames - 1;

    const int gx = (frame % sp.cols) * sp.fw;
    const int gy = (frame / sp.cols) * sp.fh;
    const float u0 = (float)gx / (float)sp.texw, u1 = (float)(gx + sp.fw) / (float)sp.texw;
    const float v0 = (float)gy / (float)sp.texh, v1 = (float)(gy + sp.fh) / (float)sp.texh;

    /* Fit the frame in the well at a WHOLE-NUMBER magnification: a DOS sprite at 2.5x is
       a smeared DOS sprite. */
    int mag = ph * 3 / (4 * (sp.fh > 0 ? sp.fh : 1));
    if (mag < 1) mag = 1;
    if (mag > 7) mag = 7;   /* past seven a 20-pixel man is a mosaic, not a soldier */
    const float w = (float)(sp.fw * mag * scale), h = (float)(sp.fh * mag * scale);
    const float x0 = (float)px + ((float)pw * scale - w) * 0.5f;
    const float y0 = (float)py + ((float)ph * scale - h) * 0.5f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sp.gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x0 + w, y0);
    glTexCoord2f(u1, v1); glVertex2f(x0 + w, y0 + h);
    glTexCoord2f(u0, v1); glVertex2f(x0, y0 + h);
    glEnd();
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    return true;
}

/* ------------------------------------------------------------------------- the frame */

static void codex_logos_open(void)
{
    if (g_cxLogosTried) return;
    g_cxLogosTried = true;
    char err[256];
    if (logo3d_open(&g_cxLogos, "logos.pack", err, sizeof err)) return;
    /* Beside the BINARY, not just beside the working directory: a build started from a
       shortcut has a working directory somewhere else entirely. Same reasoning and same
       call as find_brain above. */
    char* base = SDL_GetBasePath();
    if (base) {
        char beside[1024];
        snprintf(beside, sizeof beside, "%slogos.pack", base);
        SDL_free(base);
        if (logo3d_open(&g_cxLogos, beside, err, sizeof err)) return;
    }
    fprintf(stderr, "codex: no logos.pack (%s); the emblems will be absent\n", err);
}

static void codex_draw(int fbw, int fbh)
{
    if (!g_cxOpen) return;
    codex_layout(fbw, fbh);
    if (g_cxDirty) {
        codex_raster();
        g_cxDirty = false;
        if (!g_cxTex) {
            std::vector<unsigned char> zero((size_t)CODEX_TEX_POT * CODEX_TEX_POT * 4, 0);
            glGenTextures(1, &g_cxTex);
            glBindTexture(GL_TEXTURE_2D, g_cxTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CODEX_TEX_POT, CODEX_TEX_POT, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, &zero[0]);
        }
        db_surface_to_rgba(&g_cxSurf, g_dbPack->pal8, g_cxRGBA, 1);
        glBindTexture(GL_TEXTURE_2D, g_cxTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_cxW, g_cxH,
                        GL_RGBA, GL_UNSIGNED_BYTE, g_cxRGBA);
    }
    if (!g_cxTex) return;

    const int sc = g_cxL.scale;
    const int top = g_cxL.boxY * sc;   /* the measured strip, in framebuffer pixels */

    begin_overlay(fbw, fbh);

    /* 1. THE WASH. The battlefield stays on the glass and goes dark under it, which is
       the whole reason this is a screen over the game and not a screen instead of it. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.02f, 0.78f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, (float)top);
    glVertex2f((float)fbw, (float)top);
    glVertex2f((float)fbw, (float)fbh);
    glVertex2f(0.0f, (float)fbh);
    glEnd();

    /* 2. the page */
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, g_cxTex);
    {
        const float u1 = (float)g_cxW / (float)CODEX_TEX_POT;
        const float v1 = (float)g_cxH / (float)CODEX_TEX_POT;
        const float x1 = (float)(g_cxW * sc), y1 = (float)(g_cxH * sc);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(u1, 0.0f);   glVertex2f(x1, 0.0f);
        glTexCoord2f(u1, v1);     glVertex2f(x1, y1);
        glTexCoord2f(0.0f, v1);   glVertex2f(0.0f, y1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);

    /* 3. the selection's own picture, in the well the page left for it */
    const int ci = codex_current();
    /* One turn every twelve seconds at 60fps: slow enough to read a silhouette by, which
       is what the screen is for. */
    const float turn = (float)(g_cxFrame % 720) * (2.0f * (float)M_PI / 720.0f);
    if (ci >= 0) {
        const CodexRow* r = &CODEX[ci];
        const int mi = codex_mesh_for(r->code);
        if (mi >= 0) {
            end_overlay();
            /* INSET BY THE FRAME. The viewport is also the scissor, so a viewport the
               size of the well lets the model paint over the well's own green border --
               which read as a broken frame under the Hum-vee's nose. */
            codex_draw_model(mi, (g_cxL.viewX + 2) * sc, (g_cxL.viewY + 2) * sc,
                             (g_cxL.viewW - 4) * sc, (g_cxL.viewH - 4) * sc, fbh, turn,
                             (g_cxHouse == CODEX_HOUSE_NOD) ? 0 : 1);
            glViewport(0, 0, fbw, fbh);
            begin_overlay(fbw, fbh);
        } else if (g_dosinfOn) {
            codex_draw_infantry(r->code, g_cxL.viewX * sc, g_cxL.viewY * sc,
                                g_cxL.viewW, g_cxL.viewH, sc,
                                (float)(g_cxFrame / 45));
        }
    }
    end_overlay();

    /* 4. the two emblems. logo3d owns its own GL state and restores what it found. */
    codex_logos_open();
    if (g_cxLogos.n > 0) {
        const int which = (g_cxHouse == CODEX_HOUSE_NOD) ? LOGO3D_NOD : LOGO3D_GDI;
        const float deg = (float)(g_cxFrame % 900) * (360.0f / 900.0f);
        logo3d_draw(&g_cxLogos, which, fbh,
                    g_cxL.embX * sc, g_cxL.embY * sc, g_cxL.embW * sc, g_cxL.embH * sc,
                    deg, 256, 0.0f, 0.0f, 1.0f, 1.0f, LOGO3D_TILT_DEFAULT);
        if (g_cxLogos.n > LOGO3D_EVA)
            logo3d_draw(&g_cxLogos, LOGO3D_EVA, fbh,
                        g_cxL.evaX * sc, g_cxL.evaY * sc,
                        g_cxL.evaW * sc, g_cxL.evaH * sc,
                        deg, 256, 0.0f, 0.0f, 1.0f, 1.0f, LOGO3D_TILT_DEFAULT);
    }
    glViewport(0, 0, fbw, fbh);
}

/* ------------------------------------------------------------------- the music duck
 *
 *  THE RULE: opening the page takes the music down by 40% over a second and a half, and
 *  closing it takes it back up to the volume set in Sound Controls over the same second
 *  and a half.
 *
 *  TWO THINGS MAKE THIS MORE THAN A LERP.
 *
 *  1. THE CEILING IS THE SOUND CONTROLS SETTING, not whatever the mixer happened to hold
 *     when the screen opened. Reading the live volume as the base would ratchet: open,
 *     close early, open again, and each pass would duck 25% off the ducked figure until
 *     the music was gone. g_optState.set.music is the player's own number and it is the
 *     only thing this fades back TO.
 *
 *  2. IT RUNS ON THE WALL CLOCK, NOT THE TICK. The world is stopped while the screen is
 *     up, which is exactly when the fade DOWN has to happen, so a fade driven by the
 *     engine frame would never move. And the fade UP outlives the screen -- the player
 *     closes it and the music has another second and a half to climb -- so the stepper
 *     is called every frame from draw_frame whether or not the codex is open.
 */
#define CODEX_DUCK_TO 0.60f            /* turn the music down by 40% */
#define CODEX_DUCK_MS 1500.0

static float  g_cxDuck = 1.0f;         /* where the fade is now, 0..1 of the setting */
static int    g_cxCeilingSeen = -1;    /* the mixer's own level, before we touched it */
static float  g_cxDuckFrom = 1.0f;
static float  g_cxDuckTo = 1.0f;
static double g_cxDuckT0 = -1.0;       /* wall ms the current fade started; <0 = idle */
static int    g_cxDuckApplied = -1;    /* the last 0..255 written, so a still fade is silent */

static void codex_duck_to(float target, double now_ms)
{
    if (target == g_cxDuckTo && g_cxDuckT0 >= 0.0) return;
    /* Retarget from WHERE IT IS, not from where the last fade began: reversing halfway
       must not jump. */
    g_cxDuckFrom = g_cxDuck;
    g_cxDuckTo = target;
    g_cxDuckT0 = now_ms;
}

/* THE CEILING THE FADE RETURNS TO, and it is not always in the place you would look.
 *
 *  g_optState.set.music is the Sound Controls slider and it is the right answer -- but it
 *  is SEEDED INSIDE opt_show(), the first time the pause dialog opens, or by the shell
 *  restoring a settings file. A player who launches, starts a mission and clicks DATABASE
 *  without ever pressing ESC has neither: the struct is still zeroed, so reading it as the
 *  ceiling multiplies the music by nothing, and closing the page fades it back up to
 *  nothing as well. The music would go off and stay off, and the last thing anyone would
 *  suspect is the codex.
 *
 *  So until the dialog has run, the MIXER is the authority -- captured once, before the
 *  first duck writes anything, which is the only moment it is still the player's own
 *  number. After it has run, the slider wins, including when the player moves it. */
static int codex_music_ceiling(void)
{
    if (g_optSeeded) return g_optState.set.music * 255 / DOPT_VOL_TOP;
    if (g_cxCeilingSeen < 0 && g_au) g_cxCeilingSeen = cnc_audio_get_music_volume(g_au);
    return g_cxCeilingSeen < 0 ? 255 : g_cxCeilingSeen;
}

static void codex_audio_step(double now_ms)
{
    if (g_cxDuckT0 >= 0.0) {
        double t = (now_ms - g_cxDuckT0) / CODEX_DUCK_MS;
        if (t >= 1.0) { t = 1.0; g_cxDuckT0 = -1.0; }
        if (t < 0.0) t = 0.0;
        g_cxDuck = g_cxDuckFrom + (g_cxDuckTo - g_cxDuckFrom) * (float)t;
    } else if (g_cxDuck == g_cxDuckTo && g_cxDuckTo == 1.0f && g_cxDuckApplied < 0) {
        return;                        /* never touched the mixer, never will unasked */
    }
    if (!g_au) return;
    const int base = codex_music_ceiling();
    int v = (int)((float)base * g_cxDuck + 0.5f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    if (v == g_cxDuckApplied) return;
    g_cxDuckApplied = v;
    cnc_audio_set_music_volume(g_au, v);
}

/* ---------------------------------------------------------------------------- input */

/* WHO IS ALLOWED TO STOP TIME. One place answers, so the tick loop's gate and anything
   else that asks cannot drift apart.

   THE RULE: opening the page pauses the game, EXCEPT in multiplayer. There is no
   networked game on this branch to except -- the lockstep work lives on `multiplayer` --
   so the condition is written as one named predicate with the network case spelled out,
   returning the same answer the pause DIALOG gives today. When that branch lands this is
   the single line it changes, and a codex left open in a network game becomes a screen
   you can read while the war carries on without you. */
static bool codex_holds_the_world(void)
{
    return g_cxOpen;
}

/* IS THERE A DATABASE TAB AT ALL. Two conditions, and both are the point rather than
   belt and braces:

   g_fx.enabled is ENHANCED. Classic is the 1995 game and the 1995 game shipped no
   database, so the plate must not appear there; this is the same switch the movement
   queue is behind and no new one is added for it.

   g_hudNew is the ENHANCED HUD. The tab strip this plate lives in is the 640 HUD's; the
   classic DOS bar has no plate row to put it in and its own tab art is the cartridge's,
   which carries no DATABASE caption. The requirement is explicit: this is built on the
   Enhanced HUD and is not available in Classic mode.

   Everything else -- the hit test, the plate draw, the key, the script verbs -- asks this
   one function, so the tab cannot be clickable on a screen that is not drawing it. */
static bool codex_available(void)
{
    return g_fx.enabled != 0 && g_hudNew && g_h6Pack != NULL;
}

static void codex_set_house_from_player(void)
{
    g_cxHouse = (g_playerHouse[0] && !strcmp(g_playerHouse, "BadGuy"))
                ? CODEX_HOUSE_NOD : CODEX_HOUSE_GDI;
}

static void codex_open(void)
{
    if (g_cxOpen || !codex_available()) return;
    g_cxOpen = true;
    codex_set_house_from_player();
    /* FOLD THE DRAWER AWAY and remember where it was, so closing the screen gives the
       player back the sidebar they had rather than the one this decided they should
       have. The three plates are pinned to the window and stay. */
    g_cxSlideWas = g_h6SlideTarget;
    g_h6SlideTarget = H6_BAR_W;
    g_cxDirty = true;
    codex_duck_to(CODEX_DUCK_TO, (double)SDL_GetTicks());
    printf("CODEX|open|house=%s|cat=%d|frame=%d\n",
           g_cxHouse == CODEX_HOUSE_NOD ? "NOD" : "GDI", g_cxCat, g_engineFrame);
    fflush(stdout);
}

static void codex_close(void)
{
    if (!g_cxOpen) return;
    g_cxOpen = false;
    g_h6SlideTarget = g_cxSlideWas;
    codex_duck_to(1.0f, (double)SDL_GetTicks());
    printf("CODEX|close|frame=%d\n", g_engineFrame);
    fflush(stdout);
}

static void codex_toggle(void)
{
    if (g_cxOpen) codex_close(); else codex_open();
}

static void codex_select(int cat, int sel)
{
    const int n = codex_count(cat);
    if (n <= 0) return;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    g_cxCat = cat;
    g_cxSel[cat] = sel;
    g_cxDirty = true;
    const int ci = codex_current();
    printf("CODEX|select|cat=%s|row=%d|code=%s\n", CODEX_CAT_NAME[cat], sel,
           ci >= 0 ? CODEX[ci].code : "-");
    fflush(stdout);
}

/* Returns true when the click belonged to this screen, which is EVERY click inside it
   below the tab strip: a modal that lets clicks fall through to the battlefield is a
   modal that gives orders while the player is reading. */
static bool codex_click(float col, float row, int fbw, int fbh)
{
    if (!g_cxOpen) return false;
    codex_layout(fbw, fbh);
    const int sc = g_cxL.scale;
    const int x = (int)col / sc, y = (int)row / sc;
    /* The strip belongs to the sidebar, and its height is the MEASURED one for the same
       reason the box's top edge is: the two are drawn at different scales. */
    if ((int)row < g_cxL.stripFb) return false;

    for (int i = 0; i < CODEX_CATS; i++) {
        const int bx = g_cxL.boxX + 6 + i * (g_cxL.catW + g_cxL.catGap);
        if (x >= bx && x < bx + g_cxL.catW &&
            y >= g_cxL.catY && y < g_cxL.catY + g_cxL.catH) {
            codex_select(i, g_cxSel[i]);
            return true;
        }
    }
    if (x >= g_cxL.listX && x < g_cxL.listX + g_cxL.listW &&
        y >= g_cxL.listY && y < g_cxL.listY + g_cxL.listH) {
        const int i = (y - g_cxL.listY - 4) / g_cxL.rowH;
        if (i >= 0 && i < g_cxL.rows)
            codex_select(g_cxCat, g_cxTop[g_cxCat] + i);
        return true;
    }
    return true;
}

static bool codex_wheel(int delta)
{
    if (!g_cxOpen) return false;
    codex_select(g_cxCat, g_cxSel[g_cxCat] + (delta < 0 ? 1 : -1));
    return true;
}

static bool codex_key(int sym)
{
    if (!g_cxOpen) return false;
    switch (sym) {
    case SDLK_ESCAPE:
    case SDLK_RETURN:
        codex_close();
        return true;
    case SDLK_UP:
        codex_select(g_cxCat, g_cxSel[g_cxCat] - 1);
        return true;
    case SDLK_DOWN:
        codex_select(g_cxCat, g_cxSel[g_cxCat] + 1);
        return true;
    case SDLK_LEFT:
        codex_select((g_cxCat + CODEX_CATS - 1) % CODEX_CATS, 0);
        return true;
    case SDLK_RIGHT:
    case SDLK_TAB:
        codex_select((g_cxCat + 1) % CODEX_CATS, 0);
        return true;
    default:
        break;
    }
    /* Every other key is swallowed. A hotkey that builds a barracks while the player is
       reading about barracks is the bug this line exists to prevent. */
    return true;
}

#endif /* CODEX_MOD_H */
