/* ---------------------------------------------------------------------------------- *
 *  THE GROUND, KEPT ON THE GPU.
 *
 *  draw_terrain and draw_terrain_seams run once for the frame and once more for the sea's
 *  reflection, and each time they used to recompute every corner of every cell in view:
 *  its height under the strip (terrain_y), its texture coordinates, its light, and send
 *  the lot. None of that changes from one frame to the next except the light, and the
 *  light only where the shroud moved. Measured on two Windows PCs, the reflection alone
 *  was the largest pass of the frame.
 *
 *  So the shape is built once per map into buffers on the GPU, the cells as six corners
 *  each and every seam strip as its TS_LAT shared corners with an index list, in the
 *  order the per-frame loops visit them. Each cell's corner light is remembered; a pass
 *  recomputes the light of each cell it draws, exactly as before, and only a cell whose
 *  light differs from the remembered one has its colours rebuilt and sent again. Then
 *  the cells and strips the pass admits go down in runs of consecutive records, so the
 *  triangles, their corners and their order are those of the loops they replace, and
 *  where blending makes order matter (the strips) it is the same order.
 *
 *  Not in the editor (it rewrites cells and heights in place every frame) and not on a
 *  driver without buffers: those keep the per-frame path. Red Alert's ground is another
 *  map with another sheet, and keeps its own buffers the same way (ra3d_terrain.h, which
 *  uses the stage and array helpers below). OPENCNC3D_TERRAIN_CACHE=0 turns both off for an
 *  A/B.
 * ---------------------------------------------------------------------------------- */
#ifndef OPENCNC3D_TERRAIN_CACHE_H
#define OPENCNC3D_TERRAIN_CACHE_H

#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW         0x88E8
#endif

/* One cell's corner light, NW NE SE SW, as terrain_cell_light gives it. Zeroed whole
   before it is filled, so two of them compare with memcmp. */
struct TcLight { float s[4]; unsigned char t[4][3]; unsigned char tinted, known; };

/* Everything the shape of the cells is computed from. Compared whole. */
struct TcKey {
    const void* cells; size_t ncells; unsigned gen, pad;
    int gridW, gridH, mapX, mapY, mapW, mapH, margin, clip, relief, varOff, enhVar, enhSlots, atlas;
    float base, us, vs;
};
/* And the strips': the seam sheets, their tables and the atlas scale. */
struct TcSeamKey {
    GLuint texL, texT; int cell, margin, colsL, colsT, wL, hL, wT, hT, nslots;
    size_t slotL, slotT; const void* tab; float aus, avs;
};

struct TcStage {                     /* one set of buffers */
    std::vector<float> geo, col;     /* per corner: geometry (5 or 7 floats), colour (4) */
    GLuint geoVbo, colVbo, idxVbo;
    size_t dirtyLo, dirtyHi;         /* corners whose colour changed since the last upload */
};

struct TerrainCache {
    bool cellsBuilt, stripsBuilt, failed;
    TcKey key;
    TcSeamKey seamKey;
    /* the cells: six corners per record, x y z u v */
    std::vector<int> cellRec;        /* record -> pack cell */
    std::vector<int> recOfCell;      /* pack cell -> record, or -1 */
    TcStage A;
    /* the strips, pass 0 (left) then pass 1 (top): TS_LAT corners each, x y z u0 v0 u1 v1 */
    std::vector<int> stripCell[2];   /* strip -> pack cell a */
    std::vector<int> stripBx[2], stripBy[2];
    std::vector<int> stripK[2];      /* strip -> its strip in the pass's sheet */
    std::vector<int> stripOfCell[2]; /* pack cell -> strip, or -1 */
    std::vector<float> fxz;          /* per strip corner: its fraction of the cell */
    std::vector<unsigned> idx;       /* TS_IDX per strip, absolute */
    size_t stripFirst[2];            /* first strip of each pass */
    TcStage B;
    std::vector<TcLight> light;      /* per pack cell */
    unsigned revSeq;                 /* g_groundSeq the shape was last brought up to date at */
    unsigned stripPlane;             /* water_plane_epoch when the strips were last built whole */
};
static TerrainCache g_tc;

static bool tcache_switch_on(void)
{
    static int on = -1;
    if (on < 0) { const char* v = getenv("OPENCNC3D_TERRAIN_CACHE"); on = (!v || strcmp(v, "0")) ? 1 : 0; }
    return on == 1;
}

static void tcache_stage_free(TcStage& s)
{
    if (g_grassBufReady) {
        if (s.geoVbo) gr_glDeleteBuffers(1, &s.geoVbo);
        if (s.colVbo) gr_glDeleteBuffers(1, &s.colVbo);
        if (s.idxVbo) gr_glDeleteBuffers(1, &s.idxVbo);
    }
    s.geoVbo = s.colVbo = s.idxVbo = 0;
    s.geo.clear(); s.geo.shrink_to_fit();
    s.col.clear(); s.col.shrink_to_fit();
    s.dirtyLo = s.dirtyHi = 0;
}

/* The mission's buffers back to the driver; game_shutdown, beside the other ground stores. */
static void tcache_free(void)
{
    tcache_stage_free(g_tc.A);
    tcache_stage_free(g_tc.B);
    g_tc.cellsBuilt = g_tc.stripsBuilt = g_tc.failed = false;
    g_tc.cellRec.clear(); g_tc.recOfCell.clear(); g_tc.light.clear();
    g_tc.fxz.clear(); g_tc.idx.clear();
    for (int p = 0; p < 2; p++) {
        g_tc.stripCell[p].clear(); g_tc.stripBx[p].clear(); g_tc.stripBy[p].clear();
        g_tc.stripK[p].clear(); g_tc.stripOfCell[p].clear();
    }
}

static bool tcache_usable(void)
{
    return !g_editOn && !g_raModule && !g_tc.failed && tcache_switch_on() && grass_buf_load();
}

/* Send one buffer and read its size back: a driver short of memory may make an empty
   store and say nothing, and the whole ground then keeps the per-frame path. */
static bool tcache_store(GLuint* vbo, GLenum target, const void* data, size_t bytes, GLenum usage)
{
    if (!*vbo) gr_glGenBuffers(1, vbo);
    gr_glBindBuffer(target, *vbo);
    gr_glBufferData(target, (FxGLsizeiptr)bytes, bytes ? data : NULL, usage);
    GLint got = -1;
    gr_glGetBufferParameteriv(target, GL_BUFFER_SIZE, &got);
    gr_glBindBuffer(target, 0);
    if (bytes && (got < 0 || (size_t)got != bytes)) {
        fprintf(stderr, "GROUND|cache refused|the driver made %ld B of a %lu B ground store; "
                        "the ground draws per frame until the next mission\n",
                (long)got, (unsigned long)bytes);
        g_tc.failed = true;
        return false;
    }
    return true;
}

static void tcache_dirty(TcStage& s, size_t lo, size_t hi)
{
    if (s.dirtyLo == s.dirtyHi) { s.dirtyLo = lo; s.dirtyHi = hi; return; }
    if (lo < s.dirtyLo) s.dirtyLo = lo;
    if (hi > s.dirtyHi) s.dirtyHi = hi;
}

static void tcache_upload_colours(TcStage& s)
{
    if (s.dirtyLo == s.dirtyHi) return;
    gr_glBindBuffer(GL_ARRAY_BUFFER, s.colVbo);
    gr_glBufferSubData(GL_ARRAY_BUFFER, (FxGLsizeiptr)(s.dirtyLo * 4 * sizeof(float)),
                       (FxGLsizeiptr)((s.dirtyHi - s.dirtyLo) * 4 * sizeof(float)),
                       &s.col[s.dirtyLo * 4]);
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    s.dirtyLo = s.dirtyHi = 0;
}

/* A cell's six corner colours, as draw_terrain's loop gives them. */
static void tcache_colour_cell(int rec, const TcLight& L)
{
    float* c = &g_tc.A.col[(size_t)rec * 6 * 4];
    /* corners in drawing order: NW, SW, NE, NE, SW, SE = light index 0, 3, 1, 1, 3, 2 */
    static const int order[6] = { 0, 3, 1, 1, 3, 2 };
    const float k = 1.0f / 255.0f;
    for (int j = 0; j < 6; j++) {
        const int q = order[j];
        if (L.tinted) { c[0] = L.t[q][0]*k; c[1] = L.t[q][1]*k; c[2] = L.t[q][2]*k; c[3] = L.s[q]; }
        else          { c[0] = c[1] = c[2] = L.s[q]; c[3] = 1.0f; }
        c += 4;
    }
    tcache_dirty(g_tc.A, (size_t)rec * 6, (size_t)rec * 6 + 6);
}

static void tcache_colour_strip(int strip, const TcLight& L)
{
    const size_t v0 = (size_t)strip * TS_LAT;
    for (int j = 0; j < TS_LAT; j++)
        ts_corner_rgba(L.s, L.t, L.tinted != 0, g_tc.fxz[(v0 + j) * 2], g_tc.fxz[(v0 + j) * 2 + 1],
                       &g_tc.B.col[(v0 + j) * 4]);
    tcache_dirty(g_tc.B, v0, v0 + TS_LAT);
}

/* The light of pack cell i now; if it is not the one remembered, everything coloured from
   it is coloured again. */
static void tcache_light(int i, const PackCell& c, bool tinted)
{
    TcLight L;
    memset(&L, 0, sizeof L);
    terrain_cell_light(c, tinted, L.s, L.t);
    L.tinted = tinted ? 1 : 0;
    L.known = 1;
    TcLight& was = g_tc.light[i];
    if (!memcmp(&was, &L, sizeof L)) return;
    memcpy(&was, &L, sizeof L);
    if (g_tc.cellsBuilt && g_tc.recOfCell[i] >= 0) tcache_colour_cell(g_tc.recOfCell[i], L);
    if (g_tc.stripsBuilt)
        for (int p = 0; p < 2; p++)
            if (g_tc.stripOfCell[p][i] >= 0) tcache_colour_strip(g_tc.stripOfCell[p][i], L);
}

static void tcache_key_now(TcKey* k, float us, float vs, bool enhVar, int enhSlots)
{
    memset(k, 0, sizeof *k);
    k->cells = g_pack.cell.empty() ? NULL : &g_pack.cell[0];
    k->ncells = g_pack.cell.size();
    k->gen = g_groundGen; k->pad = g_padEpoch;
    k->gridW = g_gridW; k->gridH = g_gridH;
    k->mapX = g_mapX; k->mapY = g_mapY; k->mapW = g_mapW; k->mapH = g_mapH;
    k->margin = g_terrainMargin; k->clip = g_clip_to_map ? 1 : 0;
    k->relief = cliff_relief_on() ? 1 : 0; k->varOff = enh_variants_off() ? 1 : 0;
    k->enhVar = enhVar ? 1 : 0; k->enhSlots = enhSlots; k->atlas = terrain_atlas_index();
    k->base = g_terrainBase; k->us = us; k->vs = vs;
}

/* One cell record's six corners, x y z u v, as draw_terrain's loop computes them. */
static void tcache_cell_geo(const PackCell& c, float us, float vs, bool enhVar, int enhSlots, float V[30])
{
    const float x = (float)c.x, z = (float)c.y;
    const float y00 = terrain_corner_y(c.x,     c.y);
    const float y10 = terrain_corner_y(c.x + 1, c.y);
    const float y11 = terrain_corner_y(c.x + 1, c.y + 1);
    const float y01 = terrain_corner_y(c.x,     c.y + 1);
    float u0 = c.u0 * us, u1 = c.u1 * us;
    float v0 = c.v0 * vs, v1 = c.v1 * vs;
    if (enhVar) {
        const int slot = ts_slot_of_cell(c, enhSlots);
        const int vk = enh_cell_variant(c, slot);
        if (vk) {
            const EnhVar& ev = g_pack.enhVar[slot];
            u0 += ev.du[vk] * us; u1 += ev.du[vk] * us;
            v0 += ev.dv[vk] * vs; v1 += ev.dv[vk] * vs;
        }
    }
    /* NW, SW, NE then NE, SW, SE: the console's own diagonal */
    const float G[30] = { x,        y00, z,        u0, v0,
                          x,        y01, z + 1.0f, u0, v1,
                          x + 1.0f, y10, z,        u1, v0,
                          x + 1.0f, y10, z,        u1, v0,
                          x,        y01, z + 1.0f, u0, v1,
                          x + 1.0f, y11, z + 1.0f, u1, v1 };
    memcpy(V, G, sizeof G);
}

/* The cells' shape, in pack order, as draw_terrain's loop computes it. */
static bool tcache_build_cells(const TcKey& k, float us, float vs, bool enhVar, int enhSlots)
{
    tcache_free();
    g_tc.key = k;
    g_tc.light.assign(g_pack.cell.size(), TcLight());
    for (size_t i = 0; i < g_tc.light.size(); i++) memset(&g_tc.light[i], 0, sizeof(TcLight));
    g_tc.recOfCell.assign(g_pack.cell.size(), -1);
    std::vector<float>& g = g_tc.A.geo;
    for (size_t i = 0; i < g_pack.cell.size(); i++) {
        const PackCell& c = g_pack.cell[i];
        if (cliff_relief_slot(c) >= 0) continue;
        g_tc.recOfCell[i] = (int)g_tc.cellRec.size();
        g_tc.cellRec.push_back((int)i);
        float V[30];
        tcache_cell_geo(c, us, vs, enhVar, enhSlots, V);
        g.insert(g.end(), V, V + 30);
    }
    g_tc.A.col.assign(g_tc.cellRec.size() * 6 * 4, 0.0f);
    if (!tcache_store(&g_tc.A.geoVbo, GL_ARRAY_BUFFER, g.empty() ? NULL : &g[0], g.size() * sizeof(float), GL_STATIC_DRAW)
        || !tcache_store(&g_tc.A.colVbo, GL_ARRAY_BUFFER, NULL, g_tc.A.col.size() * sizeof(float), GL_DYNAMIC_DRAW))
        return false;
    g_tc.A.dirtyLo = g_tc.A.dirtyHi = 0;
    g_tc.cellsBuilt = true;
    g_tc.revSeq = ground_rev_current() ? g_groundSeq : 0;
    return true;
}

/* Every array a previous pass may have left on switched off, the buffers bound, the
   pointers set. The caller pops the client state afterwards. */
static void tcache_arrays_begin(const TcStage& s, int stride, bool unit1)
{
    static GLint units = 0;
    if (!units) {
        glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
        units = units < 2 ? 2 : units > 8 ? 8 : units;
    }
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_INDEX_ARRAY);
    glDisableClientState(GL_EDGE_FLAG_ARRAY);
    for (int u = units - 1; u >= 0; u--) {
        glClientActiveTexture(GL_TEXTURE0 + u);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    gr_glBindBuffer(GL_ARRAY_BUFFER, s.geoVbo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, stride, (const void*)(3 * sizeof(float)));
    if (unit1) {
        glClientActiveTexture(GL_TEXTURE1);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, stride, (const void*)(5 * sizeof(float)));
        glClientActiveTexture(GL_TEXTURE0);
    }
    gr_glBindBuffer(GL_ARRAY_BUFFER, s.colVbo);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_FLOAT, 4 * sizeof(float), (const void*)0);
}

/* ---- A BUILDING MOVES A FEW CELLS, NOT THE MAP ----
   A pad change used to throw the whole cache away: every cell and strip laid again, both
   stores reallocated and every light forgotten, so every cell in view was coloured and
   sent again on the next draw. The heights are the only thing a pad reaches, so when the
   key differs in nothing but the pad epoch, the records of the cells the revision grid
   stamped since the last refresh are laid again with the same functions and sent over
   their old bytes, and everything else stays. A cell's light does not read the pad, so
   the remembered lights and colours stay right. The strips also read the water plane,
   which the key does not hold; a plane that moved since they were laid sends this back
   to the whole rebuild, as before. */
typedef void (APIENTRY *GroundGetBufferSubData)(GLenum, FxGLsizeiptr, FxGLsizeiptr, void*);
static bool ground_read_buffer(GLenum target, GLuint vbo, size_t bytes, void* out)
{
    static GroundGetBufferSubData get = NULL;
    static bool tried = false;
    if (!tried) {
        tried = true;
        get = (GroundGetBufferSubData)SDL_GL_GetProcAddress("glGetBufferSubData");
        if (!get) get = (GroundGetBufferSubData)SDL_GL_GetProcAddress("glGetBufferSubDataARB");
    }
    if (!get) return false;
    if (!bytes) return true;
    gr_glBindBuffer(target, vbo);
    get(target, 0, (FxGLsizeiptr)bytes, out);
    gr_glBindBuffer(target, 0);
    return true;
}
/* Send the corners [lo, hi) of a geometry array over their old bytes. */
static void ground_send_range(GLuint vbo, const std::vector<float>& geo, int stride, size_t lo, size_t hi)
{
    if (lo >= hi) return;
    gr_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gr_glBufferSubData(GL_ARRAY_BUFFER, (FxGLsizeiptr)(lo * stride * sizeof(float)),
                       (FxGLsizeiptr)((hi - lo) * stride * sizeof(float)), &geo[lo * stride]);
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
}
/* Lay a record's corners again; when they differ, write them and widen the pending run
   [runLo, runHi) of corners to cover them. Each call into the driver costs far more than
   the bytes it carries, so records up to a few hundred corners apart share one send, the
   unchanged bytes between them going down again as they are. */
static void ground_patch_record(GLuint vbo, std::vector<float>& geo, int stride, int corners, size_t rec,
                                const float* fresh, size_t& runLo, size_t& runHi, long& patched)
{
    float* at = &geo[rec * corners * stride];
    if (!memcmp(at, fresh, (size_t)corners * stride * sizeof(float))) return;
    memcpy(at, fresh, (size_t)corners * stride * sizeof(float));
    patched++;
    const size_t lo = rec * corners, hi = lo + corners;
    if (runLo == runHi) { runLo = lo; runHi = hi; return; }
    if (lo <= runHi + 4096) { runHi = hi > runHi ? hi : runHi; return; }
    ground_send_range(vbo, geo, stride, runLo, runHi);
    runLo = lo; runHi = hi;
}
static bool tcache_build_strips(const TcSeamKey& sk, float aus, float avs, int nslots);
static bool tcache_patch(const TcKey& k, float us, float vs, bool enhVar, int enhSlots)
{
    if (!g_tc.cellsBuilt || !ground_patch_on() || !ground_rev_current() || g_groundWholeSeq > g_tc.revSeq)
        return false;
    TcKey a = k, b = g_tc.key;
    a.pad = b.pad = 0;
    if (memcmp(&a, &b, sizeof a) != 0) return false;
    if (g_tc.stripsBuilt && g_tc.stripPlane != water_plane_epoch()) return false;
    const Uint64 t0 = SDL_GetPerformanceCounter();
    const unsigned since = g_tc.revSeq;
    long cells = 0, strips = 0;
    size_t lo = 0, hi = 0;
    for (size_t r = 0; r < g_tc.cellRec.size(); r++) {
        const PackCell& c = g_pack.cell[g_tc.cellRec[r]];
        if (!ground_rev_moved(since, c.x, c.y, c.x, c.y)) continue;
        float V[30];
        tcache_cell_geo(c, us, vs, enhVar, enhSlots, V);
        ground_patch_record(g_tc.A.geoVbo, g_tc.A.geo, 5, 6, r, V, lo, hi, cells);
    }
    ground_send_range(g_tc.A.geoVbo, g_tc.A.geo, 5, lo, hi);
    if (g_tc.stripsBuilt) {
        const TcSeamKey& sk = g_tc.seamKey;
        lo = hi = 0;
        for (int pass = 0; pass < 2; pass++) {
            TsPass P;
            if (g_tc.stripCell[pass].empty() || !ts_pass(g_seams, pass, &P)) continue;
            for (size_t r = 0; r < g_tc.stripCell[pass].size(); r++) {
                const PackCell& ca = g_pack.cell[g_tc.stripCell[pass][r]];
                const int bx = g_tc.stripBx[pass][r], by = g_tc.stripBy[pass][r];
                if (!ground_rev_moved(since, ca.x, ca.y, ca.x, ca.y) && !ground_rev_moved(since, bx, by, bx, by)) continue;
                float out[TS_LAT * 7];
                int j = 0;
                for (int ai = 0; ai <= TS_NA; ai++)
                    for (int xi = 0; xi <= TS_NX; xi++, j++) {
                        float fx, fz;
                        ts_corner_f(g_seams, pass, ai, xi, &fx, &fz);
                        ts_corner_geo(g_seams, pass, P, g_tc.stripK[pass][r], ca.x, ca.y, ca.u0, ca.u1, ca.v0, ca.v1,
                                      sk.aus, sk.avs, fx, fz, &out[j * 7]);
                    }
                ground_patch_record(g_tc.B.geoVbo, g_tc.B.geo, 7, TS_LAT, g_tc.stripFirst[pass] + r, out, lo, hi, strips);
            }
        }
        ground_send_range(g_tc.B.geoVbo, g_tc.B.geo, 7, lo, hi);
    }
    g_tc.key = k;
    g_tc.revSeq = g_groundSeq;
    const double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (ground_patch_verify()) {
        /* What the GPU holds must be what the arrays hold, and the arrays must be what a
           whole rebuild lays now. The rebuild is then kept, which is the old path. */
        std::vector<float> gpuA(g_tc.A.geo.size()), gpuB(g_tc.B.geo.size());
        const bool read = ground_read_buffer(GL_ARRAY_BUFFER, g_tc.A.geoVbo, gpuA.size() * sizeof(float), gpuA.data())
            && (!g_tc.stripsBuilt || ground_read_buffer(GL_ARRAY_BUFFER, g_tc.B.geoVbo, gpuB.size() * sizeof(float), gpuB.data()));
        const bool gpuOk = !read || (gpuA == g_tc.A.geo && (!g_tc.stripsBuilt || gpuB == g_tc.B.geo));
        const std::vector<float> wantA = g_tc.A.geo, wantB = g_tc.B.geo;
        const std::vector<int> recA = g_tc.cellRec, recB0 = g_tc.stripCell[0], recB1 = g_tc.stripCell[1];
        const bool hadStrips = g_tc.stripsBuilt;
        const TcSeamKey sk = g_tc.seamKey;
        bool ok = tcache_build_cells(k, us, vs, enhVar, enhSlots);
        if (ok && hadStrips) ok = tcache_build_strips(sk, sk.aus, sk.avs, sk.nslots);
        const bool same = ok && g_tc.A.geo == wantA && g_tc.cellRec == recA
            && (!hadStrips || (g_tc.B.geo == wantB && g_tc.stripCell[0] == recB0 && g_tc.stripCell[1] == recB1));
        fprintf(stderr, "GROUNDPATCH|verify|td cache|%s|gpu=%s|cells=%ld|strips=%ld\n",
                same && gpuOk ? "ok" : "MISMATCH", read ? (gpuOk ? "ok" : "MISMATCH") : "unread", cells, strips);
        return ok;
    }
    fprintf(stderr, "GROUNDPATCH|td cache|cells=%ld|strips=%ld|%.2f ms\n", cells, strips, ms);
    return true;
}

/* draw_terrain's cells from the cache. False when the per-frame loop has to run instead. */
static bool tcache_cells_draw(bool tinted, float us, float vs, bool enhVar, int enhSlots)
{
    if (!tcache_usable()) return false;
    TcKey k;
    tcache_key_now(&k, us, vs, enhVar, enhSlots);
    if (!g_tc.cellsBuilt || memcmp(&k, &g_tc.key, sizeof k) != 0)
        if (!tcache_patch(k, us, vs, enhVar, enhSlots) && !tcache_build_cells(k, us, vs, enhVar, enhSlots)) return false;
    static std::vector<GLint> first;
    static std::vector<GLsizei> count;
    first.clear(); count.clear();
    const int n = (int)g_tc.cellRec.size();
    for (int r = 0; r < n; r++) {
        const int i = g_tc.cellRec[r];
        const PackCell& c = g_pack.cell[i];
        if (!cell_shown(c.x, c.y) || !cell_in_view(c.x, c.y) || terrain_surface_has_cell(c.x,c.y)) continue;
        tcache_light(i, c, tinted);
        if (!count.empty() && first.back() + count.back() == r * 6) count.back() += 6;
        else { first.push_back(r * 6); count.push_back(6); }
    }
    if (first.empty()) return true;
    tcache_upload_colours(g_tc.A);
    const GLuint bound = gls_array_binding();
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    tcache_arrays_begin(g_tc.A, 5 * sizeof(float), false);
    for (size_t j = 0; j < first.size(); j++) glDrawArrays(GL_TRIANGLES, first[j], count[j]);
    glPopClientAttrib();
    gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
    /* the current colour and coordinate the last corner left, as the per-corner calls did */
    const size_t last = (size_t)(first.back() + count.back() - 1);
    const float* col = &g_tc.A.col[last * 4];
    const float* geo = &g_tc.A.geo[last * 5];
    glColor4f(col[0], col[1], col[2], col[3]);
    glTexCoord2f(geo[3], geo[4]);
    return true;
}

/* The strips' shape for both passes, in the order draw_terrain_seams visits them, with
   every condition that does not change during a mission already applied. */
static bool tcache_build_strips(const TcSeamKey& sk, float aus, float avs, int nslots)
{
    tcache_stage_free(g_tc.B);
    g_tc.stripsBuilt = false;
    g_tc.seamKey = sk;
    g_tc.fxz.clear(); g_tc.idx.clear();
    std::vector<float>& g = g_tc.B.geo;
    int nstrips = 0;
    for (int pass = 0; pass < 2; pass++) {
        g_tc.stripCell[pass].clear(); g_tc.stripBx[pass].clear(); g_tc.stripBy[pass].clear();
        g_tc.stripK[pass].clear(); g_tc.stripOfCell[pass].assign(g_pack.cell.size(), -1);
        g_tc.stripFirst[pass] = (size_t)nstrips;
        TsPass P;
        if (!ts_pass(g_seams, pass, &P)) continue;
        for (size_t i = 0; i < g_pack.cell.size(); i++) {
            const PackCell& a = g_pack.cell[i];
            const int sa = ts_slot_of_cell(a, nslots);
            if (sa < 0 || g_tsSlotTmpl[sa] <= 0) continue;
            const int bx = a.x + (pass ? 0 : 1), by = a.y + (pass ? 1 : 0);
            if (bx >= g_gridW || by >= g_gridH) continue;
            const int bi = g_tsGrid[(size_t)by * g_gridW + bx];
            if (bi < 0) continue;
            const int sb = ts_slot_of_cell(g_pack.cell[bi], nslots);
            if (sb < 0 || g_tsSlotTmpl[sb] <= 0) continue;
            const int sbv = sb * TS_VARS + enh_cell_variant(g_pack.cell[bi], sb);
            if ((*P.idx)[sbv] < 0) continue;
            if ((*P.feat)[sbv] && g_seams.cliffTmpl[g_tsSlotTmpl[sa] & 0xFFFF]) continue;
            const int kk = (*P.idx)[sbv];
            g_tc.stripOfCell[pass][i] = nstrips;
            g_tc.stripCell[pass].push_back((int)i);
            g_tc.stripBx[pass].push_back(bx);
            g_tc.stripBy[pass].push_back(by);
            g_tc.stripK[pass].push_back(kk);
            const unsigned v0 = (unsigned)nstrips * TS_LAT;
            for (int ai = 0; ai <= TS_NA; ai++)
                for (int xi = 0; xi <= TS_NX; xi++) {
                    float fx, fz, out[7];
                    ts_corner_f(g_seams, pass, ai, xi, &fx, &fz);
                    ts_corner_geo(g_seams, pass, P, kk, a.x, a.y, a.u0, a.u1, a.v0, a.v1,
                                  aus, avs, fx, fz, out);
                    g.insert(g.end(), out, out + 7);
                    g_tc.fxz.push_back(fx); g_tc.fxz.push_back(fz);
                }
            for (int ia = 0; ia < TS_NA; ia++)
                for (int ix = 0; ix < TS_NX; ix++) {
                    int q[6];
                    ts_quad_corners(pass, ia, ix, q);
                    for (int j = 0; j < 6; j++) g_tc.idx.push_back(v0 + (unsigned)q[j]);
                }
            nstrips++;
        }
    }
    g_tc.B.col.assign((size_t)nstrips * TS_LAT * 4, 0.0f);
    if (!tcache_store(&g_tc.B.geoVbo, GL_ARRAY_BUFFER, g.empty() ? NULL : &g[0], g.size() * sizeof(float), GL_STATIC_DRAW)
        || !tcache_store(&g_tc.B.colVbo, GL_ARRAY_BUFFER, NULL, g_tc.B.col.size() * sizeof(float), GL_DYNAMIC_DRAW)
        || !tcache_store(&g_tc.B.idxVbo, GL_ELEMENT_ARRAY_BUFFER, g_tc.idx.empty() ? NULL : &g_tc.idx[0],
                         g_tc.idx.size() * sizeof(unsigned), GL_STATIC_DRAW))
        return false;
    g_tc.B.dirtyLo = g_tc.B.dirtyHi = 0;
    g_tc.stripPlane = water_plane_epoch();
    /* every strip's colour comes from its cell's remembered light, which the strips have
       not been coloured from yet: forget the lights so the passes colour them */
    for (size_t i = 0; i < g_tc.light.size(); i++) memset(&g_tc.light[i], 0, sizeof(TcLight));
    g_tc.stripsBuilt = true;
    return true;
}

/* One pass of draw_terrain_seams from the cache, the caller's texture state already set
   (ts_strips_begin and the pass's sheet on unit 0). False when its loop has to run. */
static bool tcache_strips_draw(int pass, bool tinted, float aus, float avs, int nslots, const TtSlot* tab)
{
    if (!tcache_usable() || !g_tc.cellsBuilt) return false;
    TcKey k;
    tcache_key_now(&k, g_tc.key.us, g_tc.key.vs, g_tc.key.enhVar != 0, g_tc.key.enhSlots);
    if (memcmp(&k, &g_tc.key, sizeof k) != 0) return false;   /* the cells are stale: their loop runs */
    TcSeamKey sk;
    memset(&sk, 0, sizeof sk);
    sk.texL = g_seams.texL; sk.texT = g_seams.texT; sk.cell = g_seams.cell; sk.margin = g_seams.margin;
    sk.colsL = g_seams.colsL; sk.colsT = g_seams.colsT;
    sk.wL = g_seams.wL; sk.hL = g_seams.hL; sk.wT = g_seams.wT; sk.hT = g_seams.hT;
    sk.nslots = nslots; sk.slotL = g_seams.slotL.size(); sk.slotT = g_seams.slotT.size();
    sk.tab = tab; sk.aus = aus; sk.avs = avs;
    if (!g_tc.stripsBuilt || memcmp(&sk, &g_tc.seamKey, sizeof sk) != 0)
        if (!tcache_build_strips(sk, aus, avs, nslots)) return false;
    static std::vector<GLsizei> count;
    static std::vector<size_t> first;
    first.clear(); count.clear();
    const std::vector<int>& cellOf = g_tc.stripCell[pass];
    const int n = (int)cellOf.size();
    const size_t base = g_tc.stripFirst[pass];
    for (int r = 0; r < n; r++) {
        const int i = cellOf[r];
        const PackCell& a = g_pack.cell[i];
        if (!cell_shown(a.x, a.y) || !cell_in_view(a.x, a.y)) continue;
        if (!cell_shown(g_tc.stripBx[pass][r], g_tc.stripBy[pass][r])) continue;
        const int bi=g_tsGrid[(size_t)g_tc.stripBy[pass][r]*g_gridW+g_tc.stripBx[pass][r]];
        const int sb=bi>=0?ts_slot_of_cell(g_pack.cell[bi],nslots):-1;
        if(sb>=0 && ((desert_detail_replaces(g_tsSlotTmpl[sb]) && !(g_tsSlotTmpl[sb]>=149 && g_tsSlotTmpl[sb]<=160))
             || (fx_unified_lighting() && g_tsSlotTmpl[sb]>=13 && g_tsSlotTmpl[sb]<=50)))continue;
        tcache_light(i, a, tinted);
        const size_t f = (base + (size_t)r) * TS_IDX;
        if (!count.empty() && first.back() + (size_t)count.back() == f) count.back() += TS_IDX;
        else { first.push_back(f); count.push_back(TS_IDX); }
    }
    if (first.empty()) return true;
    tcache_upload_colours(g_tc.B);
    const GLuint bound = gls_array_binding();
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    tcache_arrays_begin(g_tc.B, 7 * sizeof(float), true);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_tc.B.idxVbo);
    for (size_t j = 0; j < first.size(); j++)
        glDrawElements(GL_TRIANGLES, count[j], GL_UNSIGNED_INT,
                       (const void*)(first[j] * sizeof(unsigned)));
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glPopClientAttrib();
    gr_glBindBuffer(GL_ARRAY_BUFFER, bound);
    /* the current colour and coordinates the last corner left, as the per-corner calls did */
    const size_t last = g_tc.idx[first.back() + (size_t)count.back() - 1];
    const float* col = &g_tc.B.col[last * 4];
    const float* geo = &g_tc.B.geo[last * 7];
    glColor4f(col[0], col[1], col[2], col[3]);
    glTexCoord2f(geo[3], geo[4]);
    glMultiTexCoord2f(GL_TEXTURE1, geo[5], geo[6]);
    return true;
}

#endif /* OPENCNC3D_TERRAIN_CACHE_H */
