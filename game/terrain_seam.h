/* terrain_seam.h -- THE JOIN BLEND: the high-resolution terrain's pieces carried across
 * their joins.
 *
 * Every piece is painted on its own, and across the shipped maps one side of a piece
 * meets up to eleven different neighbours, so no single picture per piece can line up
 * with all of them: at a join the rock, the road's ruts or the beach's sand stopped on
 * the tile line. Each piece was painted among its most common neighbours, and the
 * painting runs on past its borders; the strip just outside its LEFT and TOP borders is
 * kept (tools/art/remaster/textures, write_margins for a cliff and write_feature_margins
 * for a road, shore, river or bridge; tools/bakery/pack_terrain_seams.py), with an alpha
 * that is solid at the border and gives out a quarter cell in, irregularly, along the
 * painting's own cracks and ruts. A feature's strip carries only the feature: its plain
 * ground and its water are alpha 0.
 *
 * Drawn after the ground: where a piece's left (top) side is a template border and the
 * cell beyond it is also a non-grass cell, that piece's left (top) strip is laid over the
 * neighbouring cell along their common edge. Across the border the rock is then one
 * painting, and the change to the neighbour's own rock happens at a crack-like line
 * inside its tile. One direction per join (the right or lower piece over the left or
 * upper), so two strips never cross. A feature's strip is never laid over a cliff: it
 * carries whatever the painting has beyond the border that is not plain ground, the
 * painter's boulders and stones as well as the ruts and the sand, and over a cliff's
 * ground those stood as pale ghost rocks. The cliffs' own joins are the cliffs' strips.
 *
 * NEVER ON THE SEA: the strip's alpha is multiplied by the cell's own atlas alpha on a
 * second texture unit, the same cartridge water cut the ground is drawn with, so painted
 * sand or rock cannot land where the renderer draws its own water.
 *
 * Fixed function: the strip is a textured, alpha-blended quad strip on the cell's own
 * surface (terrain_y), drawn with the ground's own texture environment and per-vertex
 * light, a polygon offset, no depth write, and the atlas's alpha on unit 1.
 *
 * Tier 2 only with the high-resolution terrain itself: Win98 draws the 1995 tiles, which
 * meet by design and need no blend.
 */
#ifndef OPENCNC3D_TERRAIN_SEAM_H
#define OPENCNC3D_TERRAIN_SEAM_H

#define TS_MAGIC "C3DSEAM\0"
#define TS_VARS 4     /* a slot's first painting and its variants (enh_cell_variant) */

struct TerrainSeams {
    bool tried = false, ok = false;
    char theater[17] = "";
    int cell = 0, margin = 0;
    GLuint texL = 0, texT = 0;       /* the left strips' sheet and the top strips'     */
    int wL = 0, hL = 0, wT = 0, hT = 0, colsL = 1, colsT = 1;
    std::vector<int> slotL, slotT;   /* bank slot -> strip index in its sheet, or -1    */
    std::vector<unsigned char> featL, featT;   /* bank slot -> 1 if that strip is a feature's */
    std::vector<unsigned char> cliffTmpl;      /* template number -> 1 if a cliff (has a cliff strip) */
};
static TerrainSeams g_seams;

static void terrain_seams_free(void)
{
    if (g_seams.texL) glDeleteTextures(1, &g_seams.texL);
    if (g_seams.texT) glDeleteTextures(1, &g_seams.texT);
    g_seams = TerrainSeams();
}

static int ts_pot(int v) { int p = 1; while (p < v) p *= 2; return p; }

/* One sheet of strips of sw x sh texels, each with a one-texel gutter of its own edge. */
static GLuint ts_sheet(const std::vector<const unsigned char*>& strips, int sw, int sh,
                       int* outW, int* outH, int* cols)
{
    const int pw = sw + 2, ph = sh + 2;
    *cols = 4096 / pw; if (*cols < 1) *cols = 1;
    const int rows = ((int)strips.size() + *cols - 1) / *cols;
    *outW = ts_pot(*cols * pw); *outH = ts_pot(rows * ph);
    std::vector<unsigned char> px((size_t)*outW * *outH * 4, 0);
    for (size_t k = 0; k < strips.size(); k++) {
        const int x0 = (int)(k % *cols) * pw, y0 = (int)(k / *cols) * ph;
        for (int y = -1; y <= sh; y++) {
            const int sy = y < 0 ? 0 : y >= sh ? sh - 1 : y;
            for (int x = -1; x <= sw; x++) {
                const int sx = x < 0 ? 0 : x >= sw ? sw - 1 : x;
                memcpy(&px[((size_t)(y0 + 1 + y) * *outW + x0 + 1 + x) * 4],
                       strips[k] + ((size_t)sy * sw + sx) * 4, 4);
            }
        }
    }
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fx_filter_mode(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fx_filter_mode(GL_NEAREST));
    fx_filter_note(t, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *outW, *outH, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

/* The theater's seam pack: OPENCNC3D_TERRAINSEAM if set (or "off"), else
   terrainseam_<TH>.pack beside the other packs, else in the repository (a repo run), the
   way the Enhanced terrain pack itself is found. Only with the high-resolution terrain
   drawing. */
static void water_static_seam_alpha(int tid,int icon,int var,int side,unsigned char* rgba,int cell,int margin);
static void terrain_seams_ensure(void)
{
    if (g_seams.tried && !strcmp(g_seams.theater, g_pack.theater)) return;
    terrain_seams_free();
    g_seams.tried = true;
    snprintf(g_seams.theater, sizeof g_seams.theater, "%s", g_pack.theater);
    char packaged[64], repo_root[160], repo_run[160];
    snprintf(packaged, sizeof packaged, "terrainseam_%s.pack", g_pack.theater);
    snprintf(repo_root, sizeof repo_root, "art/remaster/textures/%s/terrainseam_%s.pack",
             g_pack.theater, g_pack.theater);
    snprintf(repo_run, sizeof repo_run, "../art/remaster/textures/%s/terrainseam_%s.pack",
             g_pack.theater, g_pack.theater);
    const char* path = rt_override_path("OPENCNC3D_TERRAINSEAM", packaged, repo_root, repo_run);
    if (!path) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    std::vector<unsigned char> d;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 20) { d.resize((size_t)n); if (fread(&d[0], 1, d.size(), f) != d.size()) d.clear(); }
    fclose(f);
    if (d.size() < 24 || memcmp(&d[0], TS_MAGIC, 8) != 0) {
        fprintf(stderr, "terrain seams: %s is not a seam pack\n", path);
        return;
    }
    const unsigned C = rt_le32(&d[12]), M = rt_le32(&d[16]), cnt = rt_le32(&d[20]);
    const size_t keys = 24, body = keys + (size_t)cnt * 8;
    if (rt_le32(&d[8]) != 1 || !C || !M || d.size() != body + (size_t)cnt * C * M * 4) {
        fprintf(stderr, "terrain seams: %s has the wrong size\n", path);
        return;
    }
    int nslots = 0;
    tt_table(g_pack.theater, &nslots);
    /* every per-slot table is indexed slot * TS_VARS + variant: a variant's strips are its
       own, keyed with the variant in the icon field as its cells are in the terrain pack */
    const size_t nv = (size_t)nslots * TS_VARS;
    g_seams.slotL.assign(nv, -1); g_seams.slotT.assign(nv, -1);
    g_seams.featL.assign(nv, 0); g_seams.featT.assign(nv, 0);
    g_seams.cliffTmpl.assign(65536, 0);
    std::vector<const unsigned char*> L, T;
    for (unsigned k = 0; k < cnt; k++) {
        const unsigned char* e = &d[keys + (size_t)k * 8];
        /* e[5], the kind, was padding in the first packs: 0 there, a cliff's strip */
        const int tmpl = e[0] | (e[1] << 8), icon16 = e[2] | (e[3] << 8), side = e[4], feat = e[5] == 1;
        const int icon = icon16 & 255, var = icon16 >= 256 ? (icon16 >> 8) & 3 : 0;
        const int slot0 = tt_slot_of(g_pack.theater, tmpl, icon, NULL);
        const unsigned char* px = &d[body + (size_t)k * C * M * 4];
        if (!feat) g_seams.cliffTmpl[tmpl] = 1;
        if (slot0 < 0 || slot0 >= nslots || var >= TS_VARS) continue;
        const int slot = slot0 * TS_VARS + var;
        water_static_seam_alpha(tmpl,icon,var,side,&d[body+(size_t)k*C*M*4],C,M);
        if (side == 0) { g_seams.slotL[slot] = (int)L.size(); g_seams.featL[slot] = feat; L.push_back(px); }
        else           { g_seams.slotT[slot] = (int)T.size(); g_seams.featT[slot] = feat; T.push_back(px); }
    }
    g_seams.cell = (int)C; g_seams.margin = (int)M;
    if (!L.empty()) g_seams.texL = ts_sheet(L, (int)M, (int)C, &g_seams.wL, &g_seams.hL, &g_seams.colsL);
    if (!T.empty()) g_seams.texT = ts_sheet(T, (int)C, (int)M, &g_seams.wT, &g_seams.hT, &g_seams.colsT);
    g_seams.ok = g_seams.texL || g_seams.texT;
    fprintf(stderr, "terrain seams: %s, %zu left and %zu top strips, %u deep at %u a cell\n",
            path, L.size(), T.size(), M, C);
}

/* A cell's bank slot, from its atlas rectangle (the same rule cliff_relief_slot_for keeps). */
static int ts_slot_of_cell(const PackCell& c, int nslots)
{
    const int rows = (nslots + TT_COLS - 1) / TT_COLS;
    const int col = (int)(std::min(c.u0, c.u1) * (float)TT_COLS + 1e-4f);
    const int row = (int)(std::min(c.v0, c.v1) * (float)rows + 1e-4f);
    const int slot = row * TT_COLS + col;
    return (slot >= 0 && slot < nslots) ? slot : -1;
}

/* ---- ONE BLOCK OF TRIANGLES INSTEAD OF A CALL PER CORNER ----
   The ground was sent to GL in immediate mode: a colour, one or two texture coordinates
   and a position per corner, each its own call. Under Enhanced that was about 560,000
   calls a frame on the GDI 1 map, 520,000 of them these strips, and on Windows drivers
   a call costs enough that the strips alone measured 8 to 30 ms a frame. The same
   corners, in the same order, now go into one array and are drawn with one
   glDrawArrays (imm_draw_triangles, defined once the buffer entry points are loaded).
   Client-side vertex arrays are GL 1.1, so Tier 1 keeps an answer for this. */
struct ImmVert { float p[3], t0[2], t1[2], c[4]; };
static std::vector<ImmVert> g_tsBatch;
static void imm_draw_triangles(const std::vector<ImmVert>& v, bool unit1);
static inline void imm_put(std::vector<ImmVert>& v, float x, float y, float z, float u, float t,
                           float r, float g, float b, float a, float u1 = 0.0f, float t1 = 0.0f)
{
    ImmVert q;
    q.p[0] = x; q.p[1] = y; q.p[2] = z;
    q.t0[0] = u; q.t0[1] = t;
    q.t1[0] = u1; q.t1[1] = t1;
    q.c[0] = r; q.c[1] = g; q.c[2] = b; q.c[3] = a;
    v.push_back(q);
}

/* ---- THE DRAWING, shared by both games' terrain (draw_terrain_seams for Tiberian Dawn,
   ra3d_draw_seams for Red Alert): each decides which strip goes over which cell, and
   these lay it. ---- */

/* One pass's strip sheet: 0 the left strips, 1 the top strips. */
struct TsPass {
    GLuint tex;
    const std::vector<int>* idx;
    const std::vector<unsigned char>* feat;
    int sw, sh, cols;
    float W, H;
};

static bool ts_pass(const TerrainSeams& S, int pass, TsPass* p)
{
    p->tex = pass ? S.texT : S.texL;
    if (!p->tex) return false;
    p->idx = pass ? &S.slotT : &S.slotL;
    p->feat = pass ? &S.featT : &S.featL;
    p->sw = pass ? S.cell : S.margin;
    p->sh = pass ? S.margin : S.cell;
    p->cols = pass ? S.colsT : S.colsL;
    p->W = (float)(pass ? S.wT : S.wL);
    p->H = (float)(pass ? S.hT : S.hL);
    return true;
}

/* NEVER ON THE SEA: unit 1 samples the atlas the ground is drawn with at the cell's own
   rectangle and multiplies the strip's alpha by it, the cartridge's water cut (and
   Enhanced's soft coast about it). Colour passes through unchanged: tinted, the stage
   keeps its light multiply; plain, it is a pass-through. A sheet with no alpha (Red
   Alert's) reads as alpha 1 there, which cuts nothing. */
static void ts_strips_begin(bool tinted, GLuint atGL)
{
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, atGL);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    if (!tinted) {
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glAlphaFunc(GL_GREATER, 0.02f);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);
}

/* THE STRIP'S LATTICE. A strip is TS_NA segments along the edge by TS_NX across, so
   (TS_NA + 1) x (TS_NX + 1) corners; the two triangles of each segment share them. Corner
   (ai along, xi across) in cell fractions: fx along x, fz along z. */
enum { TS_NA = 8, TS_NX = 2, TS_LAT = (TS_NA + 1) * (TS_NX + 1), TS_IDX = TS_NA * TS_NX * 6 };

static inline void ts_corner_f(const TerrainSeams& S, int pass, int ai, int xi, float* fx, float* fz)
{
    const float mf = (float)S.margin / (float)S.cell;
    const int NA = TS_NA, NX = TS_NX;
    const float p = (float)ai / NA;
    const float q = 1 - mf + mf * xi / NX;
    if (!pass) { *fx = q; *fz = p; }
    else       { *fx = p; *fz = q; }
}

/* A corner's position, its strip-sheet coordinate (unit 0) and its coordinate in the
   cell's own atlas rectangle (unit 1): x y z u0 v0 u1 v1. */
static inline void ts_corner_geo(const TerrainSeams& S, int pass, const TsPass& P, int k,
                                 int ax, int ay, float au0, float au1, float av0, float av1,
                                 float aus, float avs, float fx, float fz, float out[7])
{
    const float mf = (float)S.margin / (float)S.cell;
    const int sw = P.sw, sh = P.sh;
    const float W = P.W, H = P.H;
    const float sx0 = (float)((k % P.cols) * (sw + 2) + 1), sy0 = (float)((k / P.cols) * (sh + 2) + 1);
    float u, v;
    if (!pass) { u = sx0 + (fx - (1 - mf)) / mf * (float)sw; v = sy0 + fz * (float)sh; }
    else       { u = sx0 + fx * (float)sw; v = sy0 + (fz - (1 - mf)) / mf * (float)sh; }
    const float wx = (float)ax + fx, wz = (float)ay + fz;
    out[0] = wx; out[1] = terrain_y(wx, wz); out[2] = wz;
    out[3] = u / W; out[4] = v / H;
    out[5] = (au0 + (au1 - au0) * fx) * aus;
    out[6] = (av0 + (av1 - av0) * fz) * avs;
}

/* A corner's colour: the cell's corner light s and tint t (NW, NE, SE, SW), blended at
   the corner's position in the cell. */
static inline void ts_corner_rgba(const float s[4], const unsigned char t[4][3], bool tinted,
                                  float fx, float fz, float rgba[4])
{
    const float w00 = (1 - fx) * (1 - fz), w10 = fx * (1 - fz), w11 = fx * fz, w01 = (1 - fx) * fz;
    const float sh_ = s[0] * w00 + s[1] * w10 + s[2] * w11 + s[3] * w01;
    rgba[0] = rgba[1] = rgba[2] = sh_;
    rgba[3] = 1.0f;                                   /* glColor3f's alpha is 1 */
    if (tinted) {
        for (int ch = 0; ch < 3; ch++)
            rgba[ch] = (t[0][ch] * w00 + t[1][ch] * w10 + t[2][ch] * w11 + t[3][ch] * w01) / 255.0f;
        rgba[3] = sh_;
    }
}

/* The six corners of segment (ia, ix) in drawing order, two triangles, as lattice
   indices ai * (TS_NX + 1) + xi. The left strips (pass 0) run along z, the top strips
   (pass 1) along x, which swaps which neighbour is the second and third corner. */
static inline void ts_quad_corners(int pass, int ia, int ix, int out[6])
{
    const int W = TS_NX + 1;
    const int c0 = ia * W + ix, c3 = (ia + 1) * W + ix + 1;
    const int c1 = pass ? (ia + 1) * W + ix : ia * W + ix + 1;
    const int c2 = pass ? ia * W + ix + 1 : (ia + 1) * W + ix;
    out[0] = c0; out[1] = c2; out[2] = c1; out[3] = c1; out[4] = c2; out[5] = c3;
}

/* Strip k of the pass laid over the cell at (ax, ay): its left strip along that cell's
   right edge, its top strip along its bottom edge. (au0..av1) is the cell's own rectangle
   in the ground's atlas, scaled by (aus, avs), for unit 1; s and t its corner light and
   tint, NW, NE, SE, SW. Appended to g_tsBatch, which the caller draws per pass. The
   cached ground (terrain_cache.h) lays the same corners through the same three helpers. */
static void ts_strip_emit(const TerrainSeams& S, int pass, const TsPass& P, int k,
                          int ax, int ay, float au0, float au1, float av0, float av1,
                          float aus, float avs, const float s[4], const unsigned char t[4][3],
                          bool tinted)
{
    float geo[TS_LAT][7], fxz[TS_LAT][2];
    for (int ai = 0; ai <= TS_NA; ai++)
        for (int xi = 0; xi <= TS_NX; xi++) {
            const int c = ai * (TS_NX + 1) + xi;
            ts_corner_f(S, pass, ai, xi, &fxz[c][0], &fxz[c][1]);
            ts_corner_geo(S, pass, P, k, ax, ay, au0, au1, av0, av1, aus, avs,
                          fxz[c][0], fxz[c][1], geo[c]);
        }
    for (int ia = 0; ia < TS_NA; ia++)
        for (int ix = 0; ix < TS_NX; ix++) {
            int q[6];
            ts_quad_corners(pass, ia, ix, q);
            for (int j = 0; j < 6; j++) {
                const float* g = geo[q[j]];
                float rgba[4];
                ts_corner_rgba(s, t, tinted, fxz[q[j]][0], fxz[q[j]][1], rgba);
                imm_put(g_tsBatch, g[0], g[1], g[2], g[3], g[4], rgba[0], rgba[1], rgba[2], rgba[3],
                        g[5], g[6]);
            }
        }
}

/* The state back as the ground pass left it. Unit 1: tinted, its light stage (alpha
   passed on, atlGl bound but never sampled); plain, off. Unit 0 left with atGL bound. */
static void ts_strips_end(bool tinted, bool softCoast, GLuint atlGl, GLuint atGL)
{
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    if (!softCoast) glDisable(GL_BLEND);
    glAlphaFunc(GL_GREATER, softCoast ? 0.02f : 0.5f);
    glActiveTexture(GL_TEXTURE1);
    if (tinted) {
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, atlGl);
    } else {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atGL);
}

/* The ground kept on the GPU (terrain_cache.h, defined once the buffer entry points are
   loaded): draw_terrain's cells and draw_terrain_seams' strips, false when the per-frame
   loop has to run instead. */
static bool tcache_cells_draw(bool tinted, float us, float vs, bool enhVar, int enhSlots);
static bool tcache_strips_draw(int pass, bool tinted, float aus, float avs, int nslots, const TtSlot* tab);
static void tcache_free(void);

#endif /* OPENCNC3D_TERRAIN_SEAM_H */
