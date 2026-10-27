/* =====================================================================================
 *  ra3d_ore_mod.h -- Red Alert's ore, gems and ore mine as their remastered models,
 *  under Enhanced (Red Alert only; Tiberian Dawn's Tiberium is tib3d_mod.h and is not
 *  touched by anything here).
 *
 *  WHAT THE ENGINE GIVES A CELL. An ore cell is an overlay GOLD01..GOLD04, a gem cell
 *  GEM01..GEM04: four ARRANGEMENTS of one mineral, picked at random per cell so that
 *  neighbours differ (CellClass::Tiberium_Adjust). Its OverlayData, which the engine's map
 *  export hands over as the entry's ShapeIndex, is the DENSITY FRAME the 1996 game draws:
 *  ore 0..11, gems 0..2. Growth raises it a step at a time (map.cpp), a harvester lowers
 *  it (CellClass::Reduce_Tiberium), and the overlay is cleared when the last of it is
 *  taken. On every parse the map is read (ra3d_cells_from_map) into g_ra3dOreCells.
 *
 *  THE FILES (ra3d/ore/ore3d_TEM.pack on the temperate theater, ore3d_WIN.pack on snow;
 *  written by tools/ra3d/bake-ore.py, which documents the format): per mineral a painted
 *  SET of pieces and a LAYOUT read off the 1996 frames. The set holds, per arrangement and
 *  density frame, the EARTH that frame's dirt streaks trace (a thin draped patch whose
 *  edge fades out through the sheet's alpha), and the pieces that stand on it: six nugget
 *  shapes for the ore, the gem stones themselves. The layout says what stands where: per
 *  arrangement every nugget of gold (its plan point, its plan axes and height, its
 *  bearing, the frame it is born on and its size at every frame) and every stone (its
 *  plan point and the frames it shows in).
 *
 *  THE RULE that lays a cell from them is the lab's own (tools/art/remaster/ra/ore_cells.py),
 *  so the game draws what was approved: the earth of (arrangement, frame) where it lies,
 *  unturned; each nugget whose size at the frame is above 0, one of the six shapes picked
 *  by a hash of the cell and the nugget, scaled on its three axes to its 1996 blob (plan
 *  axes at most ROUND apart, its height NUG_H of its narrow half width) times NUG_SCALE
 *  times its size at the frame, turned to the blob's bearing and a little more by the
 *  hash, set on the earth's height there (built sunk below its base, it lies bedded); a
 *  nugget full grown for three frames gets a small one beside it (0.45 of it) where the
 *  earth is under it, so the field thickens as it fills; each stone shown at the frame at
 *  its 1996 size, unturned, on the earth. The hash is the cell's, so a field is the same
 *  on every run and two --shot runs agree.
 *
 *  GROWING AND SHRINKING. When a cell's frame changes, the cell goes from the old frame to
 *  the new over tib3d_growtime seconds of simulated time, as Tiberium's rings do: each
 *  nugget scales between its sizes at the two frames (a nugget keeps its identity through
 *  the frames), a stone or a companion nugget grows in or shrinks out, and the two earths
 *  cross-fade. A cell the engine seeds starts from nothing; a cell harvested away shrinks
 *  to nothing. The first parse of a mission has no entrance.
 *
 *  HOW IT IS DRAWN, after the ground and its decals (cnc_eyes.cpp, where Tiberian Dawn
 *  draws its crystals): every earth first, BLENDED by the sheet's alpha, depth tested and
 *  not written, so its edge fades into the terrain as Tiberium's mound skirts do; then the
 *  nuggets and the stones, opaque, lit by the sun the rest of the Enhanced chain uses (the
 *  crystals' floor and range), the earth by its paint alone. Nothing of Tiberium's own
 *  look runs here: no glow, no flow, no crack decal, no contact patch, no colour dials.
 *  Each cell's geometry is built once on the CPU and kept in a buffer of its own on the
 *  card; it is rebuilt only while the cell changes (a transition, the sun, the ground).
 *  The 1996 decal of a cell drawn here stands down (ra3d_cell_model).
 *
 *  THE ORE MINE (MINE) is a terrain object, drawn by the remastered-building path as the
 *  trees are (ebld_ra_ready asks ra3d_ore_mine_claims): ra3d/ebld/MINE_TEM.ebld or
 *  MINE_WIN.ebld, the theater's look, at the engine's centre coordinate (the cell's
 *  middle). Its 1996 card stands down with it. It never animates and takes no damage.
 *
 *  `raore` dumps the cells and the mine (RA3DORE| lines); `raore 0` draws the 1996 decals
 *  and the mine's card again, for an A/B.
 *
 *  GL 1.1 plus vertex buffers where the driver has them (client arrays otherwise), no
 *  shaders in the colour pass; one RGBA texture per mineral, 512 a side with its mip chain,
 *  and its specular map beside it. The pieces also draw their normals and their shine for
 *  the post chain's light pass (Tier 2; see ra3d_ore_draw_normals).
 * ===================================================================================== */
#pragma once

static bool g_ra3dOreOn = true;
struct Ra3dOreCell { int x, y; unsigned char kind, frame; };   /* kind 0..3 GOLD01..04, 4..7 GEM01..04 */
static std::vector<Ra3dOreCell> g_ra3dOreCells;              /* the last parse's */
static void ra3d_ore_cells_clear(void) { g_ra3dOreCells.clear(); }
static void ra3d_ore_cell_add(int x, int y, int kind, int frame)
{
    Ra3dOreCell c;
    c.x = x; c.y = y; c.kind = (unsigned char)kind;
    c.frame = (unsigned char)(frame < 0 ? 0 : frame > 255 ? 255 : frame);
    g_ra3dOreCells.push_back(c);
}

/* the lab's rule (ore_cells.py): NUG_SCALE is Tiberian Dawn's mound rule as shipped */
static const float RA3D_ORE_NUG_SCALE = 0.83f;
static const float RA3D_ORE_ROUND = 1.35f, RA3D_ORE_NUG_H = 1.10f;
/* how far the earth's draped patch stands over the ground under it, in cells: it is a
   hair above the terrain's own triangles, so the depth test keeps it */
static const float RA3D_ORE_EARTH_LIFT = 0.006f;
/* the nuggets' and stones' shade: a floor under the sun's lambert, as the crystals'
   (draw_tiberium_solid) */
static const float RA3D_ORE_FLOOR = 0.46f, RA3D_ORE_RANGE = 0.54f;

static int ra3d_ore_kind(const char* n)
{
    if (!strncmp(n, "GOLD0", 5) && n[5] >= '1' && n[5] <= '4' && !n[6]) return n[5] - '1';
    if (!strncmp(n, "GEM0", 4) && n[4] >= '1' && n[4] <= '4' && !n[5]) return 4 + n[4] - '1';
    return -1;
}

struct Ra3dOreVert { float x, y, z, nx, ny, nz, u, v; };
struct Ra3dOreClump { int role; float ax, ay, top; std::vector<Ra3dOreVert> v; std::vector<unsigned short> idx; };
struct Ra3dOreNug { float x, y, a, b, h, ang; int born; float size[12]; };
struct Ra3dOreStone { int clump; float x, y; unsigned char shown[12]; };
struct Ra3dOreSet {
    int kind, narr, nf;
    GLuint tex;
    GLuint spec;                            /* its specular map (version 2), 0 none */
    std::vector<Ra3dOreClump> clumps;
    std::vector<int> earth;                 /* [arr * nf + frame] */
    std::vector<Ra3dOreNug> nug[4];
    std::vector<Ra3dOreStone> stone[4];
    std::vector<int> nugShape;              /* the clumps of role 1, in order */
};
static Ra3dOreSet g_ra3dOreSet[2];
static int g_ra3dOreHave = -1;              /* the theater loaded (0 temperate, 2 snow), -1 none */
static int g_ra3dOreTried = -2;             /* the theater last tried */
static std::string g_ra3dOreDir;

static void ra3d_ore_dir(const char* dir) { g_ra3dOreDir = dir ? dir : ""; }

/* the pack's theater: 0 temperate, 2 snow, -1 a theater with no ore (the interior) */
static int ra3d_ore_theater(void)
{
    if (!strcmp(g_pack.theater, "RATEMP")) return 0;
    if (!strcmp(g_pack.theater, "RASNOW")) return 2;
    return -1;
}

static void ra3d_ore_free(void)
{
    for (int s = 0; s < 2; s++) {
        if (g_ra3dOreSet[s].tex) glDeleteTextures(1, &g_ra3dOreSet[s].tex);
        if (g_ra3dOreSet[s].spec) glDeleteTextures(1, &g_ra3dOreSet[s].spec);
        g_ra3dOreSet[s] = Ra3dOreSet();
    }
    g_ra3dOreHave = -1;
}

/* The sheet with its mip chain (box filtered), so a field seen from the game camera does
   not shimmer; the earth's fade is a broad gradient, many texels wide even at this size. */
static GLuint ra3d_ore_upload(const std::vector<unsigned char>& px, int side)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    std::vector<unsigned char> cur = px, nxt;
    int s = side, level = 0;
    for (;;) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, &cur[0]);
        if (s == 1) break;
        const int h = s / 2;
        nxt.assign((size_t)h * h * 4, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < h; x++)
            {
                /* the colour weighted by alpha, so a faded texel does not pull its
                   neighbours' colour toward its own */
                const unsigned char* q[4] = {
                    &cur[((size_t)(2 * y) * s + 2 * x) * 4], &cur[((size_t)(2 * y) * s + 2 * x + 1) * 4],
                    &cur[((size_t)(2 * y + 1) * s + 2 * x) * 4], &cur[((size_t)(2 * y + 1) * s + 2 * x + 1) * 4] };
                int wa = 0, sum[3] = { 0, 0, 0 }, plain[3] = { 0, 0, 0 };
                for (int k = 0; k < 4; k++) {
                    wa += q[k][3];
                    for (int c = 0; c < 3; c++) { sum[c] += q[k][c] * q[k][3]; plain[c] += q[k][c]; }
                }
                unsigned char* o = &nxt[((size_t)y * h + x) * 4];
                for (int c = 0; c < 3; c++)
                    o[c] = (unsigned char)(wa > 0 ? (sum[c] + wa / 2) / wa : (plain[c] + 2) / 4);
                o[3] = (unsigned char)((wa + 2) / 4);
            }
        cur.swap(nxt);
        s = h;
        level++;
    }
    return t;
}

/* One pack (both minerals), read whole; false and nothing kept when it cannot be read. */
static bool ra3d_ore_read(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> d;
    {
        unsigned char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    }
    fclose(f);
    size_t o = 0;
    bool ok = true;
    auto u32 = [&](void) -> unsigned {
        if (o + 4 > d.size()) { ok = false; return 0; }
        unsigned v; memcpy(&v, &d[o], 4); o += 4; return v;
    };
    auto f32 = [&](void) -> float {
        if (o + 4 > d.size()) { ok = false; return 0.0f; }
        float v; memcpy(&v, &d[o], 4); o += 4; return v;
    };
    if (d.size() < 16 || memcmp(&d[0], "RAORE1", 6) != 0) return false;
    o = 8;
    const unsigned ver = u32(), nsets = u32();
    if ((ver != 1 && ver != 2) || nsets != 2) return false;
    Ra3dOreSet sets[2];
    std::vector<unsigned char> sheets[2], specs[2];
    int sides[2] = { 0, 0 }, ssides[2] = { 0, 0 };
    for (unsigned s = 0; s < nsets && ok; s++) {
        Ra3dOreSet& S = sets[s];
        S.kind = (int)u32(); S.narr = (int)u32(); S.nf = (int)u32();
        S.tex = 0; S.spec = 0;
        if (!ok || S.kind != (int)s || S.narr != 4 || S.nf < 1 || S.nf > 12) return false;
        const unsigned side = u32();
        if (!ok || side < 1 || side > 2048 || (side & (side - 1))
            || o + (size_t)side * side * 4 > d.size()) return false;
        sheets[s].assign(d.begin() + o, d.begin() + o + (size_t)side * side * 4);
        sides[s] = (int)side;
        o += (size_t)side * side * 4;
        if (ver >= 2) {
            /* THE SPECULAR MAP: red how strongly a texel reflects the sun, green how tight
               its highlight is (the remastered models' maps); the earth's texels none */
            const unsigned ss = u32();
            if (!ok || ss < 1 || ss > 2048 || (ss & (ss - 1))
                || o + (size_t)ss * ss * 4 > d.size()) return false;
            specs[s].assign(d.begin() + o, d.begin() + o + (size_t)ss * ss * 4);
            ssides[s] = (int)ss;
            o += (size_t)ss * ss * 4;
        }
        const unsigned nc = u32();
        if (!ok || nc < 1 || nc > 4096) return false;
        S.clumps.resize(nc);
        for (unsigned c = 0; c < nc && ok; c++) {
            Ra3dOreClump& C = S.clumps[c];
            C.role = (int)u32();
            const unsigned nv = u32(), nt = u32();
            C.ax = f32(); C.ay = f32(); C.top = f32();
            if (!ok || nv > 65535 || nt > 200000 || o + (size_t)nv * 32 + (size_t)nt * 6 > d.size())
                return false;
            C.v.resize(nv);
            if (nv) memcpy(&C.v[0], &d[o], (size_t)nv * 32);
            o += (size_t)nv * 32;
            C.idx.resize((size_t)nt * 3);
            if (nt) memcpy(&C.idx[0], &d[o], (size_t)nt * 6);
            o += (size_t)nt * 6;
            for (size_t k = 0; k < C.idx.size(); k++)
                if (C.idx[k] >= nv) return false;
            if (C.role == 1) S.nugShape.push_back((int)c);
        }
        S.earth.resize((size_t)S.narr * S.nf);
        for (size_t k = 0; k < S.earth.size() && ok; k++) {
            S.earth[k] = (int)u32();
            if (S.earth[k] < -1 || S.earth[k] >= (int)nc) return false;
        }
        for (int a = 0; a < S.narr && ok; a++) {
            const unsigned nn = u32();
            if (!ok || nn > 256) return false;
            for (unsigned k = 0; k < nn && ok; k++) {
                Ra3dOreNug n;
                n.x = f32(); n.y = f32(); n.a = f32(); n.b = f32(); n.h = f32(); n.ang = f32();
                n.born = (int)u32();
                for (int fr = 0; fr < 12; fr++) n.size[fr] = fr < S.nf ? f32() : 0.0f;
                S.nug[a].push_back(n);
            }
            const unsigned ns = u32();
            if (!ok || ns > 256) return false;
            for (unsigned k = 0; k < ns && ok; k++) {
                Ra3dOreStone st;
                st.clump = (int)u32(); st.x = f32(); st.y = f32();
                memset(st.shown, 0, sizeof st.shown);
                if (o + (size_t)S.nf + ((4 - S.nf % 4) % 4) > d.size()) return false;
                for (int fr = 0; fr < S.nf; fr++) st.shown[fr] = d[o + fr];
                o += (size_t)S.nf + ((4 - S.nf % 4) % 4);
                if (st.clump < 0 || st.clump >= (int)nc) return false;
                S.stone[a].push_back(st);
            }
        }
        if (S.kind == 0 && S.nugShape.empty()) return false;
    }
    if (!ok || o != d.size()) return false;
    ra3d_ore_free();
    for (int s = 0; s < 2; s++) {
        g_ra3dOreSet[s] = sets[s];
        g_ra3dOreSet[s].tex = ra3d_ore_upload(sheets[s], sides[s]);
        if (ssides[s]) g_ra3dOreSet[s].spec = ra3d_ore_upload(specs[s], ssides[s]);
    }
    return true;
}

/* The current theater's pack, loaded the first time a frame needs it (a live GL context). */
static bool ra3d_ore_ready(void)
{
    const int th = ra3d_ore_theater();
    if (th < 0) return false;
    if (g_ra3dOreHave == th) return true;
    if (g_ra3dOreTried == th) return false;
    g_ra3dOreTried = th;
    char path[512];
    snprintf(path, sizeof path, "%sra3d/ore/ore3d_%s.pack", g_ra3dOreDir.c_str(), th == 2 ? "WIN" : "TEM");
    if (!ra3d_ore_read(path)) {
        fprintf(stderr, "RA3D|ore|%s not read: the ore and gems keep their 1996 decals\n", path);
        ra3d_ore_free();
        return false;
    }
    g_ra3dOreHave = th;
    size_t tris[2] = { 0, 0 };
    for (int s = 0; s < 2; s++)
        for (size_t c = 0; c < g_ra3dOreSet[s].clumps.size(); c++)
            tris[s] += g_ra3dOreSet[s].clumps[c].idx.size() / 3;
    fprintf(stderr, "RA3D|ore|%s|ore clumps=%zu tris=%zu|gem clumps=%zu tris=%zu\n", path,
            g_ra3dOreSet[0].clumps.size(), tris[0], g_ra3dOreSet[1].clumps.size(), tris[1]);
    return true;
}

/* Whether this frame draws the ore as its models: Red Alert in a 3D mode, Enhanced and its
   solid resources on, the theater's pack read. */
static bool ra3d_ore_drawn(void)
{
    return g_raModule && g_ra3dSprites && g_ra3dOreOn && !ra3d_ore_off() && g_fx.enabled
        && g_fx.tib3d && ra3d_ore_ready();
}

/* ---- each cell's state and geometry ---------------------------------------------- */
struct Ra3dOreGpuV { float p[3], uv[2]; unsigned char c[4]; float n[3]; };   /* n: the world normal */
struct Ra3dOreState {
    int x, y, kind;
    int from, to;          /* density frames, -1 none (a cell growing in or harvested away) */
    float t0;              /* the transition's start (simulated seconds), < 0 none */
    bool live;             /* on the last parse */
    /* the geometry as built, and what it was built for */
    std::vector<Ra3dOreGpuV> v;
    std::vector<unsigned short> ie, is;   /* the earth's triangles, the pieces' */
    int bFrom, bTo; float bU; unsigned bSun, bGround;
    GLuint vbo, ibo;
    int pieces;
};
static std::map<int, Ra3dOreState> g_ra3dOreState;
static std::string g_ra3dOreScen;
static int g_ra3dOreLastFrame = -1;
static bool g_ra3dOreSeen = false;       /* a parse of this mission has been taken */
/* the last frame's counts, for `raore` */
static int g_ra3dOreCellsDrawn = 0, g_ra3dOrePiecesDrawn = 0, g_ra3dOreBuilt = 0;
static int g_ra3dOreFrames[12];          /* the cells drawn at each density frame */
static int g_ra3dOreCast = 0;            /* the cells whose pieces cast into the sun's map */
static long g_ra3dOreTrisDrawn = 0;
static long g_ra3dOreNrmDrawn = 0, g_ra3dOreSpecDrawn = 0;   /* the pieces' triangles in the light pass's targets */
static double g_ra3dOreMs = 0.0;

static void ra3d_ore_drop(Ra3dOreState& s)
{
    if (s.vbo && gr_glDeleteBuffers) { gr_glDeleteBuffers(1, &s.vbo); s.vbo = 0; }
    if (s.ibo && gr_glDeleteBuffers) { gr_glDeleteBuffers(1, &s.ibo); s.ibo = 0; }
}

static float ra3d_ore_now(void) { return engine_time() * (1.0f / 15.0f); }

/* Where the parse's cells meet the states: a new cell grows in (none on a mission's first
   parse), a cell whose frame moved starts a transition from what it showed, a cell gone
   shrinks out. Called on every draw, cheap when nothing moved. */
static void ra3d_ore_track(void)
{
    if (g_ra3dOreScen != g_pack.scen || g_engineFrame < g_ra3dOreLastFrame) {
        for (std::map<int, Ra3dOreState>::iterator it = g_ra3dOreState.begin(); it != g_ra3dOreState.end(); ++it)
            ra3d_ore_drop(it->second);
        g_ra3dOreState.clear();
        g_ra3dOreScen = g_pack.scen;
        g_ra3dOreSeen = false;
    }
    g_ra3dOreLastFrame = g_engineFrame;
    const float now = ra3d_ore_now();
    const float T = g_fx.tib3d_growtime;
    for (std::map<int, Ra3dOreState>::iterator it = g_ra3dOreState.begin(); it != g_ra3dOreState.end(); ++it)
        it->second.live = false;
    for (size_t i = 0; i < g_ra3dOreCells.size(); i++) {
        const Ra3dOreCell& c = g_ra3dOreCells[i];
        const int key = c.y * 512 + c.x;
        const int nf = c.kind < 4 ? 12 : 3;
        const int fr = c.frame < nf ? c.frame : nf - 1;
        std::map<int, Ra3dOreState>::iterator it = g_ra3dOreState.find(key);
        if (it == g_ra3dOreState.end() || it->second.kind != c.kind) {
            if (it != g_ra3dOreState.end()) ra3d_ore_drop(it->second);
            Ra3dOreState s;
            s.x = c.x; s.y = c.y; s.kind = c.kind;
            s.from = (g_ra3dOreSeen && T > 0.001f && it == g_ra3dOreState.end()) ? -1 : fr;
            s.to = fr;
            s.t0 = s.from == s.to ? -1.0f : now;
            s.bFrom = s.bTo = -2; s.bU = -1.0f; s.bSun = 0; s.bGround = 0;
            s.vbo = s.ibo = 0; s.pieces = 0;
            s.live = true;
            g_ra3dOreState[key] = s;
            continue;
        }
        Ra3dOreState& s = it->second;
        s.live = true;
        if (s.to != fr) {
            /* (a step that comes while the last is still under way goes on from whichever
               of its two frames the cell looks more like now) */
            const bool mid = s.t0 >= 0.0f && T > 0.001f && (now - s.t0) < 0.5f * T;
            s.from = (T > 0.001f) ? (mid ? s.from : s.to) : fr;
            s.to = fr;
            s.t0 = s.from == s.to ? -1.0f : now;
        }
    }
    for (std::map<int, Ra3dOreState>::iterator it = g_ra3dOreState.begin(); it != g_ra3dOreState.end();) {
        Ra3dOreState& s = it->second;
        if (!s.live && s.to >= 0 && g_ra3dOreSeen && T > 0.001f) {
            s.from = s.to; s.to = -1; s.t0 = now;          /* harvested away: shrinks out */
        }
        const bool done = s.t0 < 0.0f || now - s.t0 >= T;
        if (done && s.t0 >= 0.0f) { s.from = s.to; s.t0 = -1.0f; }
        if (!s.live && (s.to < 0 || !g_ra3dOreSeen || T <= 0.001f) && s.t0 < 0.0f) {
            ra3d_ore_drop(s);
            g_ra3dOreState.erase(it++);
            continue;
        }
        ++it;
    }
    g_ra3dOreSeen = true;
}

/* The earth's height over a model point (0 off it), as ore_cells.py's height_on. */
static float ra3d_ore_height_on(const Ra3dOreClump* e, float x, float y)
{
    if (!e) return 0.0f;
    for (size_t t = 0; t + 2 < e->idx.size(); t += 3) {
        const Ra3dOreVert& a = e->v[e->idx[t]];
        const Ra3dOreVert& b = e->v[e->idx[t + 1]];
        const Ra3dOreVert& c = e->v[e->idx[t + 2]];
        const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (fabsf(d) < 1e-12f) continue;
        const float w0 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d;
        const float w1 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
        const float w2 = 1.0f - w0 - w1;
        if (w0 >= -1e-6f && w1 >= -1e-6f && w2 >= -1e-6f)
            return w0 * a.z + w1 * b.z + w2 * c.z;
    }
    return 0.0f;
}

/* The cell's progress through its transition, eased at both ends; 1 when none. */
static float ra3d_ore_u(const Ra3dOreState& s)
{
    if (s.t0 < 0.0f) return 1.0f;
    const float T = g_fx.tib3d_growtime;
    float u = T > 0.001f ? (ra3d_ore_now() - s.t0) / T : 1.0f;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    return u * u * (3.0f - 2.0f * u);
}

struct Ra3dOreBuild {
    Ra3dOreState* s;
    float cx, cz;                /* the cell's centre in the world */
    float lx, ly, lz;            /* towards the sun */
    int pieces;
};

/* A piece (a nugget or a stone) laid as ore_cells.py's place: scaled on its own axes,
   turned by yaw, at the model point (x, y), lifted; rigid, on the ground under its point. */
static void ra3d_ore_place(Ra3dOreBuild& B, const Ra3dOreClump& C, float x, float y, float yaw,
                           float sx, float sy, float sz, float lift)
{
    if (!(sx > 0.0f) || !(sy > 0.0f) || !(sz > 0.0f) || C.v.empty()) return;
    Ra3dOreState& s = *B.s;
    const size_t base = s.v.size();
    if (base + C.v.size() > 65535) return;
    const float cy_ = cosf(yaw), sy_ = sinf(yaw);
    const float wx0 = B.cx + x, wz0 = B.cz - y;
    const float gy = terrain_y(wx0, wz0) + lift;
    for (size_t k = 0; k < C.v.size(); k++) {
        const Ra3dOreVert& q = C.v[k];
        const float px = q.x * sx, py = q.y * sy;
        const float mx = px * cy_ - py * sy_, my = px * sy_ + py * cy_;
        /* the normal through the inverse transpose: scaled by the inverse, then turned */
        float nx = q.nx / sx, ny = q.ny / sy, nz = q.nz / sz;
        const float tx = nx * cy_ - ny * sy_, ty = nx * sy_ + ny * cy_;
        nx = tx; ny = ty;
        const float nl = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nl > 1e-9f) { nx /= nl; ny /= nl; nz /= nl; }
        /* model (east, north, up) to the world (x east, y up, z south) */
        float dd = nx * B.lx + nz * B.ly - ny * B.lz;
        if (dd < 0.0f) dd = 0.0f;
        float sh = RA3D_ORE_FLOOR + RA3D_ORE_RANGE * dd;
        if (sh > 1.0f) sh = 1.0f;
        Ra3dOreGpuV o;
        o.p[0] = wx0 + mx; o.p[1] = gy + q.z * sz; o.p[2] = wz0 - my;
        o.uv[0] = q.u; o.uv[1] = q.v;
        o.c[0] = o.c[1] = o.c[2] = (unsigned char)(sh * 255.0f + 0.5f);
        o.c[3] = 255;
        o.n[0] = nx; o.n[1] = nz; o.n[2] = -ny;
        s.v.push_back(o);
    }
    for (size_t k = 0; k < C.idx.size(); k++) s.is.push_back((unsigned short)(base + C.idx[k]));
    B.pieces++;
}

/* An earth draped on the ground under every corner, at this alpha. */
static void ra3d_ore_earth(Ra3dOreBuild& B, const Ra3dOreClump* e, float alpha)
{
    if (!e || alpha <= 0.001f || e->v.empty()) return;
    Ra3dOreState& s = *B.s;
    const size_t base = s.v.size();
    if (base + e->v.size() > 65535) return;
    const unsigned char a = (unsigned char)(alpha * 255.0f + 0.5f);
    for (size_t k = 0; k < e->v.size(); k++) {
        const Ra3dOreVert& q = e->v[k];
        Ra3dOreGpuV o;
        o.p[0] = B.cx + q.x; o.p[2] = B.cz - q.y;
        o.p[1] = terrain_y(o.p[0], o.p[2]) + q.z + RA3D_ORE_EARTH_LIFT;
        o.uv[0] = q.u; o.uv[1] = q.v;
        o.c[0] = o.c[1] = o.c[2] = 255; o.c[3] = a;
        o.n[0] = 0.0f; o.n[1] = 1.0f; o.n[2] = 0.0f;
        s.v.push_back(o);
    }
    for (size_t k = 0; k < e->idx.size(); k++) s.ie.push_back((unsigned short)(base + e->idx[k]));
}

/* Whether a nugget has its companion at a frame (ore_cells.py: full grown for three frames). */
static bool ra3d_ore_companion(const Ra3dOreNug& n, int f)
{
    return f >= 0 && f >= n.born + 3 && n.size[f] >= 0.95f;
}

static void ra3d_ore_build(Ra3dOreState& s, float u, float lx, float ly, float lz)
{
    s.v.clear(); s.ie.clear(); s.is.clear();
    const Ra3dOreSet& S = g_ra3dOreSet[s.kind < 4 ? 0 : 1];
    const int arr = s.kind & 3;
    Ra3dOreBuild B;
    B.s = &s; B.cx = (float)s.x + 0.5f; B.cz = (float)s.y + 0.5f;
    B.lx = lx; B.ly = ly; B.lz = lz; B.pieces = 0;
    const int f0 = s.t0 < 0.0f ? s.to : s.from, f1 = s.to;
    const Ra3dOreClump* e0 = (f0 >= 0 && f0 < S.nf && S.earth[(size_t)arr * S.nf + f0] >= 0)
                           ? &S.clumps[(size_t)S.earth[(size_t)arr * S.nf + f0]] : NULL;
    const Ra3dOreClump* e1 = (f1 >= 0 && f1 < S.nf && S.earth[(size_t)arr * S.nf + f1] >= 0)
                           ? &S.clumps[(size_t)S.earth[(size_t)arr * S.nf + f1]] : NULL;
    if (f0 == f1) { e0 = e1; u = 1.0f; }
    /* the earths: the old fading out under the new fading in */
    if (e0 != e1) ra3d_ore_earth(B, e0, 1.0f - u);
    ra3d_ore_earth(B, e1, f0 == f1 ? 1.0f : u);
    if (S.kind == 0) {
        const std::vector<Ra3dOreNug>& nugs = S.nug[arr];
        const int nshapes = (int)S.nugShape.size();
        for (size_t k = 0; k < nugs.size(); k++) {
            const Ra3dOreNug& n = nugs[k];
            const float sz0 = f0 >= 0 ? n.size[f0] : 0.0f, sz1 = f1 >= 0 ? n.size[f1] : 0.0f;
            const float sz = sz0 + (sz1 - sz0) * u;
            if (sz <= 0.0f) continue;
            const unsigned h1 = tib3d_hash(s.x, s.y, (unsigned)k * 3u + 0x7101u);
            const unsigned h2 = tib3d_hash(s.x, s.y, (unsigned)k * 3u + 0x7102u);
            const Ra3dOreClump& v = S.clumps[(size_t)S.nugShape[(h1 >> 8) % (unsigned)nshapes]];
            const float kk = RA3D_ORE_NUG_SCALE * sz;
            const float yaw = n.ang + 0.5f * ((float)(h2 & 1023u) / 1023.0f - 0.5f);
            const float a_ = n.a > n.b ? n.a : n.b;
            const float b_ = n.b > a_ / RA3D_ORE_ROUND ? n.b : a_ / RA3D_ORE_ROUND;
            const float h_ = n.h > RA3D_ORE_NUG_H * b_ ? n.h : RA3D_ORE_NUG_H * b_;
            const float z = ra3d_ore_height_on(e0, n.x, n.y) * (1.0f - u) + ra3d_ore_height_on(e1, n.x, n.y) * u;
            ra3d_ore_place(B, v, n.x, n.y, yaw, a_ * kk / v.ax, b_ * kk / v.ay, h_ * kk / v.top, z);
            /* its companion, where the earth is under it, growing in with the frame that has it */
            const float ang = 6.2831853f * (float)((h2 >> 12) & 1023u) / 1023.0f;
            const float dist = (a_ + 0.6f * b_) * kk * 1.25f;
            const float cx = n.x + dist * cosf(ang), cy = n.y + dist * sinf(ang);
            const float z0 = ra3d_ore_height_on(e0, cx, cy), z1 = ra3d_ore_height_on(e1, cx, cy);
            const float p0 = (ra3d_ore_companion(n, f0) && z0 > 0.0f) ? 1.0f : 0.0f;
            const float p1 = (ra3d_ore_companion(n, f1) && z1 > 0.0f) ? 1.0f : 0.0f;
            const float pc = p0 + (p1 - p0) * u;
            if (pc <= 0.0f) continue;
            const Ra3dOreClump& w = (h1 & 1u) ? v
                : S.clumps[(size_t)S.nugShape[(h1 >> 12) % (unsigned)nshapes]];
            const float k2 = kk * 0.45f * pc;
            ra3d_ore_place(B, w, cx, cy, ang, b_ * k2 / w.ax, b_ * k2 / w.ay, h_ * k2 / w.top,
                           z0 * (1.0f - u) + z1 * u);
        }
    } else {
        const std::vector<Ra3dOreStone>& st = S.stone[arr];
        for (size_t k = 0; k < st.size(); k++) {
            const Ra3dOreStone& q = st[k];
            const float p0 = (f0 >= 0 && q.shown[f0]) ? 1.0f : 0.0f;
            const float p1 = (f1 >= 0 && q.shown[f1]) ? 1.0f : 0.0f;
            const float sc = p0 + (p1 - p0) * u;
            if (sc <= 0.0f) continue;
            const float z = ra3d_ore_height_on(e0, q.x, q.y) * (1.0f - u) + ra3d_ore_height_on(e1, q.x, q.y) * u;
            ra3d_ore_place(B, S.clumps[(size_t)q.clump], q.x, q.y, 0.0f, sc, sc, sc, z);
        }
    }
    s.pieces = B.pieces;
}

/* One cell's buffers on the card, written when its geometry was rebuilt. */
static void ra3d_ore_upload_cell(Ra3dOreState& s)
{
    if (!grass_buf_load() || s.v.empty()) return;
    if (!s.vbo) gr_glGenBuffers(1, &s.vbo);
    if (!s.ibo) gr_glGenBuffers(1, &s.ibo);
    gr_glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    gr_glBufferData(GL_ARRAY_BUFFER, (FxGLsizeiptr)(s.v.size() * sizeof(Ra3dOreGpuV)), &s.v[0], GL_STATIC_DRAW);
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    std::vector<unsigned short> all(s.ie);
    all.insert(all.end(), s.is.begin(), s.is.end());
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ibo);
    gr_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (FxGLsizeiptr)(all.size() * sizeof(unsigned short)),
                    all.empty() ? NULL : &all[0], GL_STATIC_DRAW);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* The cell's earth (part 0) or its pieces (part 1). */
static long ra3d_ore_draw_cell(const Ra3dOreState& s, int part)
{
    const std::vector<unsigned short>& ix = part ? s.is : s.ie;
    if (ix.empty() || s.v.empty()) return 0;
    const GLsizei stride = (GLsizei)sizeof(Ra3dOreGpuV);
    if (s.vbo && s.ibo) {
        gr_glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ibo);
        glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
        glTexCoordPointer(2, GL_FLOAT, stride, (const void*)(sizeof(float) * 3));
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, (const void*)(sizeof(float) * 5));
        glDrawElements(GL_TRIANGLES, (GLsizei)ix.size(), GL_UNSIGNED_SHORT,
                       (const void*)(part ? s.ie.size() * sizeof(unsigned short) : 0));
    } else {
        const unsigned char* b = (const unsigned char*)&s.v[0];
        glVertexPointer(3, GL_FLOAT, stride, b);
        glTexCoordPointer(2, GL_FLOAT, stride, b + sizeof(float) * 3);
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, b + sizeof(float) * 5);
        glDrawElements(GL_TRIANGLES, (GLsizei)ix.size(), GL_UNSIGNED_SHORT, &ix[0]);
    }
    return (long)ix.size() / 3;
}

/* THE FRAME'S CELLS: the states brought up to the parse, and the geometry of every cell in
   the view rebuilt where it changed. Once a frame, by whichever pass asks first (the sun's
   casters run before the colour pass). */
static unsigned g_ra3dOrePrepared = 0;
static std::vector<Ra3dOreState*> g_ra3dOreView;
static double g_ra3dOrePrepMs = 0.0;
static void ra3d_ore_prepare(void)
{
    if (g_ra3dOrePrepared == g_drawFrameSerial + 1u) return;
    g_ra3dOrePrepared = g_drawFrameSerial + 1u;
    g_ra3dOreView.clear();
    g_ra3dOreBuilt = 0;
    memset(g_ra3dOreFrames, 0, sizeof g_ra3dOreFrames);
    const Uint64 t0 = SDL_GetPerformanceCounter();
    ra3d_ore_track();
    fx_sun_dir();
    const float lx = -g_fxSunDir[0], ly = -g_fxSunDir[1], lz = -g_fxSunDir[2];
    unsigned sun = 2166136261u;
    {
        const float q[3] = { lx, ly, lz };
        for (int k = 0; k < 3; k++) sun = (sun ^ (unsigned)(int)(q[k] * 4096.0f)) * 16777619u;
    }
    for (std::map<int, Ra3dOreState>::iterator it = g_ra3dOreState.begin(); it != g_ra3dOreState.end(); ++it) {
        Ra3dOreState& s = it->second;
        if (!g_viewValid || !cell_in_view(s.x, s.y) || !cell_shown(s.x, s.y) || shroud_cell_hidden(s.x, s.y))
            continue;
        const float u = ra3d_ore_u(s);
        const int f0 = s.t0 < 0.0f ? s.to : s.from;
        if (s.bFrom != f0 || s.bTo != s.to || s.bU != u || s.bSun != sun || s.bGround != g_groundGen) {
            ra3d_ore_build(s, u, lx, ly, lz);
            s.bFrom = f0; s.bTo = s.to; s.bU = u; s.bSun = sun; s.bGround = g_groundGen;
            ra3d_ore_upload_cell(s);
            g_ra3dOreBuilt++;
        }
        if (s.v.empty()) continue;
        if (s.to >= 0 && s.to < 12) g_ra3dOreFrames[s.to]++;
        g_ra3dOreView.push_back(&s);
    }
    g_ra3dOrePrepMs = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/* THE PASS, after the ground's decals (cnc_eyes.cpp). */
static void ra3d_ore_draw(void)
{
    g_ra3dOreCellsDrawn = g_ra3dOrePiecesDrawn = 0;
    g_ra3dOreTrisDrawn = 0;
    g_ra3dOreNrmDrawn = g_ra3dOreSpecDrawn = 0;   /* (the light pass's two, later this frame) */
    if (!ra3d_ore_drawn()) {
        g_ra3dOreBuilt = 0;
        memset(g_ra3dOreFrames, 0, sizeof g_ra3dOreFrames);
        return;
    }
    const Uint64 t0 = SDL_GetPerformanceCounter();
    ra3d_ore_prepare();
    const std::vector<Ra3dOreState*>& draw = g_ra3dOreView;
    if (!draw.empty()) {
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT
                     | GL_TEXTURE_BIT | GL_CURRENT_BIT);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_DEPTH_TEST);
        /* THE EARTHS, blended by the sheet's alpha (their faded edges), over the ground */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -2.0f);
        for (int set = 0; set < 2; set++) {
            glBindTexture(GL_TEXTURE_2D, g_ra3dOreSet[set].tex);
            for (size_t i = 0; i < draw.size(); i++)
                if ((draw[i]->kind < 4 ? 0 : 1) == set) g_ra3dOreTrisDrawn += ra3d_ore_draw_cell(*draw[i], 0);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        /* THE NUGGETS AND THE STONES, solid */
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        for (int set = 0; set < 2; set++) {
            glBindTexture(GL_TEXTURE_2D, g_ra3dOreSet[set].tex);
            for (size_t i = 0; i < draw.size(); i++)
                if ((draw[i]->kind < 4 ? 0 : 1) == set) {
                    g_ra3dOreTrisDrawn += ra3d_ore_draw_cell(*draw[i], 1);
                    g_ra3dOrePiecesDrawn += draw[i]->pieces;
                }
        }
        if (grass_buf_load()) {
            gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
            gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
        glPopClientAttrib();
        glPopAttrib();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
    g_ra3dOreCellsDrawn = (int)draw.size();
    g_ra3dOreMs = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (g_ra3dOrePrepMs > g_ra3dOreMs) g_ra3dOreMs = g_ra3dOrePrepMs;
}

/* THE NUGGETS AND THE STONES CAST into the sun's shadow map, as the models standing round
   them do (fx_draw_shadow_casters, its opaque half): the pieces of every cell in the view
   (its margin round it), depth only; the earths, which lie on the ground, cast nothing. */
static void ra3d_ore_cast(void)
{
    g_ra3dOreCast = 0;
    if (!ra3d_ore_drawn()) return;
    ra3d_ore_prepare();
    if (g_ra3dOreView.empty()) return;
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    const GLsizei stride = (GLsizei)sizeof(Ra3dOreGpuV);
    for (size_t i = 0; i < g_ra3dOreView.size(); i++) {
        const Ra3dOreState& s = *g_ra3dOreView[i];
        if (s.is.empty()) continue;
        if (s.vbo && s.ibo) {
            gr_glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
            gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ibo);
            glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
            glDrawElements(GL_TRIANGLES, (GLsizei)s.is.size(), GL_UNSIGNED_SHORT,
                           (const void*)(s.ie.size() * sizeof(unsigned short)));
        } else {
            glVertexPointer(3, GL_FLOAT, stride, &s.v[0]);
            glDrawElements(GL_TRIANGLES, (GLsizei)s.is.size(), GL_UNSIGNED_SHORT, &s.is[0]);
        }
        g_ra3dOreCast++;
    }
    if (grass_buf_load()) {
        gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    glPopClientAttrib();
}

/* THE PIECES' OWN NORMALS AND SHINE, for the post chain's light pass (fx_post.h), Tier 2
   only. The nuggets and the stones draw their smooth (gold) and flat (crystal facet)
   normals into the normal target, as the remastered buildings do (ebld_draw_normals), in
   a class of their own: an authored object's band, apart from the vehicles' and the
   structures' groups, so the light pass lights them on their real surface and no group's
   dials reach them. Then, when the frame wants the specular target (evhc_spec_want), they
   draw their specular map into it (evhc_draw_spec): the gold a bright tight glint, every
   crystal facet hard and glassy, so the sun's highlight lies on them where the sun, the
   piece and the camera line up and moves with the sun. The earths draw into neither:
   the dirt stays matte. */
static const float RA3D_ORE_NORMAL_CLASS = 0.36f;

/* The pieces of every cell in the view, with their normals and texture coordinates, under
   whatever program and texture the caller set; the colour array off (the programs read
   gl_Color.a as "self-lit", which no piece is). */
static long ra3d_ore_draw_pieces(GLuint* texOf)
{
    long tris = 0;
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor4f(1.0f, 1.0f, 1.0f, 0.0f);
    const GLsizei stride = (GLsizei)sizeof(Ra3dOreGpuV);
    for (int set = 0; set < 2; set++) {
        if (texOf && !texOf[set]) continue;
        if (texOf) glBindTexture(GL_TEXTURE_2D, texOf[set]);
        for (size_t i = 0; i < g_ra3dOreView.size(); i++) {
            const Ra3dOreState& s = *g_ra3dOreView[i];
            if ((s.kind < 4 ? 0 : 1) != set || s.is.empty()) continue;
            if (s.vbo && s.ibo) {
                gr_glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
                gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ibo);
                glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
                glTexCoordPointer(2, GL_FLOAT, stride, (const void*)(sizeof(float) * 3));
                glNormalPointer(GL_FLOAT, stride, (const void*)(sizeof(float) * 5 + 4));
                glDrawElements(GL_TRIANGLES, (GLsizei)s.is.size(), GL_UNSIGNED_SHORT,
                               (const void*)(s.ie.size() * sizeof(unsigned short)));
            } else {
                const unsigned char* b = (const unsigned char*)&s.v[0];
                glVertexPointer(3, GL_FLOAT, stride, b);
                glTexCoordPointer(2, GL_FLOAT, stride, b + sizeof(float) * 3);
                glNormalPointer(GL_FLOAT, stride, b + sizeof(float) * 5 + 4);
                glDrawElements(GL_TRIANGLES, (GLsizei)s.is.size(), GL_UNSIGNED_SHORT, &s.is[0]);
            }
            tris += (long)s.is.size() / 3;
        }
    }
    if (grass_buf_load()) {
        gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    glPopClientAttrib();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    return tris;
}

/* Into the normal target (fx_draw_terrain_normals, Red Alert's branch). */
static void ra3d_ore_draw_normals(void)
{
    g_ra3dOreNrmDrawn = 0;
    if (!ra3d_ore_drawn()) return;
    ra3d_ore_prepare();
    if (g_ra3dOreView.empty()) return;
    const GLuint prog = ebld_nrm_prog();
    if (!prog) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    fx_glUseProgram(prog);
    fx_set1i(prog, "uTex", 0);
    fx_set1i(prog, "uCut", 0);
    fx_set1f(prog, "uClass", RA3D_ORE_NORMAL_CLASS);
    fx_set1f(prog, "uEmitClass", RA3D_ORE_NORMAL_CLASS);
    GLuint tex[2] = { g_ra3dOreSet[0].tex, g_ra3dOreSet[1].tex };
    g_ra3dOreNrmDrawn = ra3d_ore_draw_pieces(tex);
    fx_glUseProgram((GLuint)was);
}

/* Whether the specular target should be drawn for the ore this frame: a set with a map
   and pieces in the view. */
static bool ra3d_ore_spec_want(void)
{
    if (!g_raModule || !ra3d_ore_drawn()) return false;
    if (!g_ra3dOreSet[0].spec && !g_ra3dOreSet[1].spec) return false;
    ra3d_ore_prepare();
    return !g_ra3dOreView.empty();
}

/* Into the specular target, under the target's own program (evhc_draw_spec). */
static void ra3d_ore_draw_spec(void)
{
    g_ra3dOreSpecDrawn = 0;
    if (!ra3d_ore_spec_want()) return;
    GLuint tex[2] = { g_ra3dOreSet[0].spec, g_ra3dOreSet[1].spec };
    g_ra3dOreSpecDrawn = ra3d_ore_draw_pieces(tex);
}

/* THE GRASS DOES NOT GROW THROUGH THE ORE. Enhanced's grass blades (grass_field.h) stand on
   every grassy cell, and they stood up through the dirt beds and the gold; a cell drawn
   here claims its ground for the tick as a building's footprint does (one rectangle a run
   of cells along a row), so the blades are cleared under the field and its edge. Called on
   every parse, before the grass records the tick. */
static void ra3d_ore_grass_claims(void)
{
    if (!g_raModule || g_fx.dos_classic || !g_ra3dOreOn || ra3d_ore_off() || !g_fx.enabled || !g_fx.tib3d
        || g_ra3dOreCells.empty() || !ra3d_ore_ready()) return;
    std::vector<int> keys;
    keys.reserve(g_ra3dOreCells.size());
    for (size_t i = 0; i < g_ra3dOreCells.size(); i++)
        keys.push_back(g_ra3dOreCells[i].y * 512 + g_ra3dOreCells[i].x);
    std::sort(keys.begin(), keys.end());
    for (size_t i = 0; i < keys.size();) {
        size_t j = i + 1;
        while (j < keys.size() && keys[j] == keys[j - 1] + 1) j++;
        const int y = keys[i] / 512, x0 = keys[i] % 512, x1 = keys[j - 1] % 512 + 1;
        grass_crush_note_rect((float)x0, (float)y, (float)x1, (float)(y + 1));
        i = j;
    }
}

/* Whether a 1996 ore or gem overlay stands down for its model (ra3d_draw_sprites). */
static bool ra3d_ore_claims_overlay(const char* name)
{
    return ra3d_ore_kind(name) >= 0 && ra3d_ore_drawn();
}

/* THE ORE MINE's claim for the remastered-building path (ebld_ra_ready): its model is
   staged and resident for the theater, and the ore is drawn as its models. */
static bool ra3d_ore_mine_claims(const SimObject& o)
{
    if (o.kind != K_TERRAIN || strcmp(o.type, "MINE") || !g_ra3dOreOn) return false;
    const int m = ebld_model_of(o.type);
    return m >= 0 && ebld_resident(g_ebld[(size_t)m]);
}

static const char* const RA3D_ORE_NAMES[8] = {
    "GOLD01", "GOLD02", "GOLD03", "GOLD04", "GEM01", "GEM02", "GEM03", "GEM04" };

/* `raore`: a line per cell the engine's map holds (its arrangement, the frame the engine
   draws, what is shown and whether this frame drew it), a line per ore mine, and the
   totals. */
static void ra3d_ore_dump(void)
{
    const bool on = ra3d_ore_drawn();
    for (size_t i = 0; i < g_ra3dOreCells.size(); i++) {
        const Ra3dOreCell& c = g_ra3dOreCells[i];
        std::map<int, Ra3dOreState>::const_iterator it = g_ra3dOreState.find(c.y * 512 + c.x);
        const Ra3dOreState* s = it == g_ra3dOreState.end() ? NULL : &it->second;
        printf("RA3DORE|cell|%s|cell=%d,%d|frame=%d|shown=%d|from=%d|u=%.2f|pieces=%d|tris=%zu|built=%d|view=%d\n",
               RA3D_ORE_NAMES[c.kind & 7], c.x, c.y, c.frame, s ? s->to : -1, s ? s->from : -1,
               s ? ra3d_ore_u(*s) : 0.0f, s ? s->pieces : 0, s ? (s->ie.size() + s->is.size()) / 3 : 0,
               s && s->bTo >= 0 ? 1 : 0, cell_in_view(c.x, c.y) ? 1 : 0);
    }
    int mines = 0, minesDrawn = 0;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_TERRAIN || strcmp(o.type, "MINE")) continue;
        const int m = ebld_model_of(o.type);
        const bool ready = ebld_ra_ready(o);
        mines++;
        if (ready) minesDrawn++;
        printf("RA3DORE|mine|cell=%d,%d|at=%.3f,%.3f|model=%d|theater=%d|ready=%d|drawn=%ld\n",
               o.cx, o.cy, o.wx, o.wz, m, m >= 0 ? g_ebld[(size_t)m].theater : -9, ready ? 1 : 0,
               m >= 0 ? (long)g_ebld[(size_t)m].drawn : 0L);
    }
    std::string fr;
    for (int k = 0; k < 12; k++) {
        char b[16];
        snprintf(b, sizeof b, "%s%d", k ? "," : "", g_ra3dOreFrames[k]);
        fr += b;
    }
    printf("RA3DORE|end|on=%d|theater=%s|loaded=%d|cells=%zu|states=%zu|drawn=%d|pieces=%d|tris=%ld|built=%d|ms=%.3f|frames=%s|cast=%d|mines=%d|minesdrawn=%d|minecards=%d|oredecals=%d|specmaps=%d|nrmtris=%ld|spectris=%ld\n",
           on ? 1 : 0, g_pack.theater, g_ra3dOreHave, g_ra3dOreCells.size(), g_ra3dOreState.size(),
           g_ra3dOreCellsDrawn, g_ra3dOrePiecesDrawn, g_ra3dOreTrisDrawn, g_ra3dOreBuilt, g_ra3dOreMs,
           fr.c_str(), g_ra3dOreCast, mines, minesDrawn, g_ra3dMineCards, g_ra3dOreDecals,
           (g_ra3dOreSet[0].spec ? 1 : 0) + (g_ra3dOreSet[1].spec ? 1 : 0), g_ra3dOreNrmDrawn, g_ra3dOreSpecDrawn);
    fflush(stdout);
}
