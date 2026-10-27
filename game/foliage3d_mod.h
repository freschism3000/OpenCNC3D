/* ================================================================================== *
 *  foliage3d_mod.h -- bushes and grass clumps scattered over plain grass.
 *
 *  WHAT THIS IS. The cartridge's maps carry trees, and nothing smaller: between the
 *  trees a temperate field is one flat grass texture. This module stands bushes and
 *  clumps of tall grass on it, from a Unity Asset Store pack ("Realistic Grass and Bush
 *  Pack1", two bushes and three grass clumps), under ENHANCED ONLY. Nothing here
 *  is faithful to the console and nothing claims to be: it is decoration, placed by the
 *  renderer, with no counterpart in the engine.
 *
 *  WHERE A PLANT MAY STAND, which is the one rule the whole feature rests on. On a
 *  temperate map, in a cell whose terrain is the clear template (atlas slots 0 to 15,
 *  the same one-compare test the ground patches use, so a road, a bridge, water, a
 *  shore, a cliff or a rough patch is never plain grass), that shows on the map and is
 *  not under the shroud, and that holds none of: tiberium, a wall, a crate, a smudge
 *  (a crater or a building's apron), a building's footprint, or a terrain object (a tree,
 *  a rock, a clump). fol_margin then keeps each plant that far from every cell that
 *  fails the test, so a bush does not lean out over the road beside it.
 *
 *  THE SAME MAP GIVES THE SAME PLANTS EVERY TIME. Which cell carries a plant, which
 *  model, how big, how turned and how tilted are all hashes of the cell and the seed
 *  dial, so two --shot runs of one script produce identical frames. The field that
 *  gathers plants into thickets is value noise over the same hash.
 *
 *  HOW IT IS DRAWN, and why that way. The pack is written in the trees' own format
 *  (tools/art/foliage/bake_foliage3d.py) and drawn with the trees' own GLSL, so a bush is
 *  lit by the same sun, sky, occlusion, transmission and wind as the wood beside it.
 *  What differs is the submission. A map carries a few dozen trees and hundreds to
 *  thousands of these, so each is not a draw: the map is cut into 8x8 cell chunks and
 *  every plant in a chunk is baked into world space in one vertex buffer per model,
 *  drawn with the tree program's placement uniforms at identity. A chunk is rebuilt
 *  only when what it depends on changes (its cells' eligibility, a ring around them for
 *  the margin, the ground's heights, or a layout dial), which is what a building going
 *  down, tiberium spreading or the shroud lifting looks like from here.
 *
 *  THE WIND IS PER CHUNK, NOT PER PLANT. The sway is a shear the tree program takes as
 *  a uniform, so every plant in one chunk and one model sways on one phase. The flutter
 *  term is keyed on each vertex's world position, so neighbours still disagree, and the
 *  sway is small; a chunk seam cannot show because plants are separate objects.
 *
 *  TIER 2 ONLY. Vertex buffers and the tree program; the Win98 build draws none of it.
 *  Listed as absent in docs/tier1-gap.md.
 * ================================================================================== */
#ifndef FOLIAGE3D_MOD_H
#define FOLIAGE3D_MOD_H

/* Where the pack is read from: beside the missions, or --foliage FILE ("" is off). */
static const char* g_folPath = "foliage3d.pack";

static std::vector<Tree3dModel> g_folModel;
static std::vector<Tree3dSet>   g_folSet;
static std::vector<Tree3dCode>  g_folCode;
static bool g_folHave = false;
static std::vector<int> g_folModelKind;   /* per model: 0 plant, 2 rock */
static int  g_folBush[2]  = { -1, -1 };          /* this frame's pick, made in foliage3d_prepare */
static int  g_folBushLod[2][3] = { { -1, -1, -1 }, { -1, -1, -1 } };  /* [bush][LOD0..2] */
static int  g_folGrass[3] = { -1, -1, -1 };

#define FOL_CHUNK 8
/* The tallest a plant can stand, at the top of both the size and the variation dials.
   The sway weight is baked against it, so a clump and a bush move by the same share of
   their own height; see foliage3d_build_chunk. */
#define FOL_HREF 2.7f

static bool foliage3d_load(const char* path)
{
    if (!tree3d_load_into(path, g_folModel, g_folSet, g_folCode, "foliage: foliage3d.pack"))
        return false;
    g_folModelKind.assign(g_folModel.size(), 0);
    for (int i = 0; i < 2; i++) {
        g_folBush[i] = -1;
        for (int l = 0; l < 3; l++) g_folBushLod[i][l] = -1;
    }
    for (int i = 0; i < 3; i++) g_folGrass[i] = -1;
    for (size_t i = 0; i < g_folCode.size(); i++) {
        const char* c = g_folCode[i].code;
        /* BUSH<n>L<lod>: every level of detail the pack ships, each with its own sheets */
        if (!strncmp(c, "BUSH", 4) && c[4] >= '1' && c[4] <= '2' && c[5] == 'L'
            && c[6] >= '0' && c[6] <= '2')
            g_folBushLod[c[4] - '1'][c[6] - '0'] = g_folCode[i].model;
        if (!strncmp(c, "GRASS", 5) && c[5] >= '1' && c[5] <= '3')
            g_folGrass[c[5] - '1'] = g_folCode[i].model;
    }
    bool anyBush = false;
    for (int i = 0; i < 2; i++)
        for (int l = 0; l < 3; l++) anyBush = anyBush || g_folBushLod[i][l] >= 0;
    g_folHave = anyBush || g_folGrass[0] >= 0 || g_folGrass[1] >= 0 || g_folGrass[2] >= 0;
    /* ANISOTROPIC FILTERING ON THE FOLIAGE SHEETS. From above, most leaf cards and every
       grass blade are seen steeply slanted, and a plain mip chain picks its level from the
       worst axis, which is the blur a card seen edge-on gets. The extension is ubiquitous
       on anything that runs the Tier 2 chain; where it is missing the sheets stay
       trilinear, which is what they were. Only these sheets: the trees are untouched. */
    {
        const char* ext = (const char*)glGetString(GL_EXTENSIONS);
        if (ext && strstr(ext, "GL_EXT_texture_filter_anisotropic")) {
            GLfloat mx = 1.0f;
            glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &mx);
            const GLfloat want = mx < 8.0f ? mx : 8.0f;
            for (size_t i = 0; i < g_folSet.size(); i++)
                for (int k = 0; k < 4; k++) {
                    glBindTexture(GL_TEXTURE_2D, g_folSet[i].tex[k]);
                    glTexParameterf(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */,
                                    want);
                }
            glBindTexture(GL_TEXTURE_2D, 0);
            fprintf(stderr, "foliage: anisotropic filtering x%.0f on the foliage sheets\n",
                    (double)want);
        }
    }
    if (!g_folHave)
        fprintf(stderr, "foliage: %s names no BUSH or GRASS model; nothing will be scattered\n",
                path);
    return g_folHave;
}

/* ---- THE ROCKS ---------------------------------------------------------------------
   rock3d.pack (tools/art/rocks/bake_rock3d.py) is fifteen stones in three colourings,
   RK<stone><A|B|C>, each centred on its bounding box with its longest side one. It is
   read with the trees' reader and APPENDED to the foliage's models and sheets, so a rock
   is batched, culled, shadowed and lit by exactly the code the plants are, as bark. What
   a rock has that a plant does not is a body: see the physics below. */
static const char* g_rockPath = "rock3d.pack";
static bool g_rockPathGiven = false;    /* --rocks named a pack: that one, whatever the map */

/* A PACK PER THEATER, WHEN THE FOLDER HAS ONE. rock3d_<THEATER>.pack (the painted stones,
   tools/art/rocks/build_stones.py) is read in place of rock3d.pack on a map of that
   theater, so a desert map lays sandstone and a winter one snow-dusted grey; the snow
   theater takes the winter stones. A folder without the theater's pack, or a run given
   --rocks, reads the one pack as before. */
static const char* rock3d_path_for_theater(void)
{
    static char path[64];
    if (g_rockPathGiven || !g_pack.theater[0])
        return g_rockPath;
    const char* th = !strcmp(g_pack.theater, "SNOW") ? "WINTER" :
                     !strcmp(g_pack.theater, "SAND") ? "DESERT" : g_pack.theater;
    if(g_raModule)th=ra3d_detail_rock_theater();
    snprintf(path, sizeof path, "rock3d_%s.pack", th);
    FILE* f = fopen(path, "rb");
    if (!f) return g_rockPath;
    fclose(f);
    return path;
}

/* THE PAINTED STONES' EXPOSURE. Their albedo is painted in the remaster set's range, the
   light of the form in it, where the experimental pack's was a photographed scan about
   half as bright, and the rock exposure dial (rock_gain) was tuned on that scan. So the
   painted stones, which mark themselves with the code RKPAINT, are drawn at the dial
   times this, measured on the Enhanced desert: at the dial's default the sunlit face of a
   sandstone boulder comes out at the desert ground's own pebbles' tone. One dial still
   moves every rock. */
#define ROCK_PAINTED_EXPOSURE 1.70f
static bool g_rockPainted = false;
static float rock_gain_now(void)
{
    return g_fx.rock_gain * (g_rockPainted ? ROCK_PAINTED_EXPOSURE : 1.0f);
}
static bool rock_cliff_material(void)
{
    return fx_unified_lighting() && fx_texset_get()==FX_TEX_ENH && g_rockPainted
        && (!strcmp(g_pack.theater,"DESERT") || !strcmp(g_pack.theater,"SAND"));
}
struct RockModel {
    int model;                  /* index into g_folModel                               */
    int stone, colour;          /* 1..15 and 0..2                                      */
    bool detail;                /* stones 11..15 are the pack's small detail stones    */
    float minY;                 /* lowest vertex: how far the centre sits above ground */
    std::vector<float> sup;     /* extreme vertices, the hull the physics touches with */
};
static std::vector<RockModel> g_rockModels;
static bool g_rockHave = false;

static void rock_support_points(const Tree3dModel& m, std::vector<float>& out)
{
    /* The vertex furthest along each of 26 directions: the six axes, the twelve edge
       diagonals and the eight corners. A low-poly stone is its own convex hull to within
       a few percent, and 26 points is enough for a lopsided one to rock on its long side
       and roll on its round one. */
    out.clear();
    const size_t nv = m.pos.size() / 3;
    std::vector<int> picked;
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dz = -1; dz <= 1; dz++) {
                if (!dx && !dy && !dz) continue;
                int best = -1; float bd = -1e30f;
                for (size_t i = 0; i < nv; i++) {
                    const float d = m.pos[i*3] * dx + m.pos[i*3+1] * dy + m.pos[i*3+2] * dz;
                    if (d > bd) { bd = d; best = (int)i; }
                }
                bool dup = false;
                for (size_t k = 0; k < picked.size(); k++) dup = dup || picked[k] == best;
                if (best >= 0 && !dup) picked.push_back(best);
            }
    for (size_t k = 0; k < picked.size(); k++)
        for (int c = 0; c < 3; c++) out.push_back(m.pos[picked[k] * 3 + c]);
}

static bool rock3d_load(const char* path)
{
    std::vector<Tree3dModel> M;
    std::vector<Tree3dSet>   S;
    std::vector<Tree3dCode>  C;
    if (!tree3d_load_into(path, M, S, C, "rocks: rock3d.pack"))
        return false;
    const int mOff = (int)g_folModel.size(), sOff = (int)g_folSet.size();
    for (size_t i = 0; i < M.size(); i++) { M[i].set += sOff; g_folModel.push_back(M[i]); }
    for (size_t i = 0; i < S.size(); i++) g_folSet.push_back(S[i]);
    g_folModelKind.resize(g_folModel.size(), 0);
    g_rockModels.clear();
    g_rockPainted = false;
    for (size_t i = 0; i < C.size(); i++) {
        if (!strcmp(C[i].code, "RKPAINT")) { g_rockPainted = true; continue; }
        int stone = 0; char col = 0;
        if (sscanf(C[i].code, "RK%2d%c", &stone, &col) != 2 || col < 'A' || col > 'C') continue;
        RockModel rm;
        rm.model = C[i].model + mOff;
        rm.stone = stone; rm.colour = col - 'A';
        rm.detail = stone >= 11;
        const Tree3dModel& t = M[C[i].model];
        rm.minY = 0.0f;
        for (size_t v = 0; v < t.pos.size() / 3; v++)
            if (t.pos[v*3+1] < rm.minY) rm.minY = t.pos[v*3+1];
        rock_support_points(t, rm.sup);
        g_folModelKind[rm.model] = 2;
        g_rockModels.push_back(rm);
    }
    g_rockHave = !g_rockModels.empty();
    fprintf(stderr, "rocks: %d stones and colourings from %s%s\n", (int)g_rockModels.size(), path,
            g_rockPainted ? " (the painted stones)" : "");
    return g_rockHave;
}

/* One model's plants in one chunk, in world space, in a vertex buffer. */
struct FolBatch { GLuint vbo, ibo; int nidx, model, count; };
struct FolChunk {
    unsigned sig;
    bool     built;
    std::vector<FolBatch> batch;
    int      nbush, nclump, nrock;
    /* where its plants stand at rest, and their lowest root, for the sun's receiver
       window (fol_recv_outside); boxed is false until a build has measured them */
    bool     boxed;
    float    lo[3], hi[3], rootLo;
    /* The pad epoch and ground stamp (g_groundSeq) it was last brought up to date at,
       the water plane its plants were stood on, and what it read that neither its
       signature nor the ground stamps cover. */
    unsigned pad, rev, plane;
    std::vector<unsigned> history;
};
/* One plant, kept for the dump and for nothing else. */
struct FolInst { short cx, cy, model; float x, z, h; int id; };

/* ---- What vehicles do to the plants ----------------------------------------------
   EVERY PLANT HAS AN ID FOR THE WHOLE MISSION, keyed on its cell, its kind and its slot
   in that cell, so a chunk rebuilt because the shroud lifted or a building went down
   hands the same plant the same id and a flattened bush stays flattened. The id indexes
   one texel of g_folCrushTex: red is how far the plant has been laid flat, green how far
   it has faded. The tree program reads it in the vertex stage (TREE_VS, uFol). Time is
   simulated seconds, so a paused game holds every plant where it is and two scripted
   runs are identical. */
#ifndef GL_TEXTURE4
#define GL_TEXTURE4 0x84C4
#endif
#define FOL_CRUSH_W 256
#define FOL_CRUSH_MAXH 256
static std::map<unsigned long long, int> g_folIdOf;
static std::vector<float> g_folCrushT;              /* per id: when crushed, < 0 never */
static std::vector<float> g_folRootX, g_folRootZ;   /* per id: where its root stands   */
static std::vector<int>   g_folCrushed;             /* ids crushed and not yet gone    */
static std::vector<std::vector<int> > g_folCellIds; /* per cell: ids standing there    */
static std::vector<unsigned char> g_folKindOf;     /* per id: 0 bush, 1 clump, 2 rock  */
static std::vector<int>   g_rockIdModel;            /* per id: the rock's model, or -1 */
static std::vector<float> g_rockIdSize, g_rockIdYaw; /* per id: as it was placed        */
static std::vector<int>   g_rockDynOf;              /* per id: its moving body, or -1  */
static std::vector<unsigned char> g_folCrushPix;
static GLuint g_folCrushTex = 0;
static int    g_folCrushH = 0;                      /* texture rows allocated          */
static bool   g_folCrushDirty = false;

static int fol_id_for(int x, int y, int kind, int k, float px, float pz)
{
    const unsigned long long key = (((unsigned long long)(unsigned)x * 4096ull
                                     + (unsigned)y) * 4ull + (unsigned)kind) * 256ull
                                   + (unsigned)(k & 255);
    std::map<unsigned long long, int>::iterator it = g_folIdOf.find(key);
    int id;
    if (it != g_folIdOf.end()) id = it->second;
    else {
        id = (int)g_folCrushT.size();
        g_folIdOf[key] = id;
        g_folCrushT.push_back(-1.0f);
        g_folRootX.push_back(px);
        g_folRootZ.push_back(pz);
        g_folKindOf.push_back((unsigned char)kind);
        g_rockIdModel.push_back(-1);
        g_rockIdSize.push_back(0.0f);
        g_rockIdYaw.push_back(0.0f);
        g_rockDynOf.push_back(-1);
    }
    g_folRootX[id] = px; g_folRootZ[id] = pz;
    return id;
}

static void rock_dyn_reset(void);
static void foliage3d_crush_reset(void)
{
    g_folIdOf.clear();
    g_folCrushT.clear(); g_folRootX.clear(); g_folRootZ.clear();
    g_folKindOf.clear(); g_rockIdModel.clear(); g_rockIdSize.clear(); g_rockIdYaw.clear();
    g_rockDynOf.clear();
    rock_dyn_reset();
    g_folCrushed.clear();
    g_folCellIds.clear();
    g_folCrushPix.clear();
    if (g_folCrushTex) glDeleteTextures(1, &g_folCrushTex);
    g_folCrushTex = 0; g_folCrushH = 0; g_folCrushDirty = false;
}

static std::vector<FolChunk>      g_folChunks;
static std::vector<FolInst>       g_folInst;       /* every chunk's plants, for foldump */
static std::vector<std::vector<FolInst> > g_folChunkInst;
static int g_folCW = 0, g_folCH = 0, g_folGW = 0, g_folGH = 0;
/* Per cell, this frame: 1 when a plant may stand there. */
static std::vector<unsigned char> g_folOk;
/* Per cell, why it was refused, for the dump: 0 ok, 1 not clear, 2 not shown,
   3 shroud, 4 building, 5 terrain object, 6 tiberium, 7 wall, 8 crate, 9 smudge. */
static std::vector<unsigned char> g_folWhy;
static std::vector<int>           g_folSlot;       /* atlas slot of each cell, -1 none */
static int  g_folBuilt = 0;                        /* chunks rebuilt, since the load  */
static int  g_folDrawInst = 0, g_folDrawTris = 0, g_folDrawBatches = 0;
static int  g_folCastBatches = 0, g_folNrmBatches = 0;
static int  g_folFrame = 0, g_folPrepFrame = -1;

#include "desert_details.h"

/* Whether the settings and the map ask for plants at all; the dump reports this, since
   outside the world pass g_fxActive is always false. */
static bool fol_plants_on(void)
{
    return g_folHave && g_fx.fol_on && (g_raModule ? ra3d_detail_plants() : !strcmp(g_pack.theater, "TEMPERAT"));
}
/* ROCKS ARE NOT TEMPERATE ONLY: a cliff is a cliff in the desert too. */
static bool fol_rocks_on(void)
{
    return g_rockHave && g_fx.rock_on;
}
/* UNDER ENHANCED, with their own switches, which Enhanced's mode row carries on and
   Classic's and N64's carry off. They were the experimental art's, and they did not go
   when that look was retired: bushes, clumps and rocks stand on any ground Enhanced draws. */
static bool foliage3d_wanted(void)
{
    return g_fx.enabled && g_gridW > 0 && g_gridH > 0 && (!g_raModule || ra3d_detail_texels()>0)
           && (fol_plants_on() || fol_rocks_on() || desert_detail_state());
}

static bool foliage3d_available(void)
{
    return g_fxActive && foliage3d_wanted() && grass_buf_load();
}

/* A desert detail landing outside its own chunk (fol_chunk_history): the partial refresh
   stands down for the mission, since a chunk rebuilt alone would clear that plant's id
   from a neighbour's cell where a whole rebuild puts it back. */
static bool g_folSpill = false;
static void foliage3d_free_chunks(void)
{
    g_folSpill = false;
    for (size_t i = 0; i < g_folChunks.size(); i++)
        for (size_t b = 0; b < g_folChunks[i].batch.size(); b++) {
            FolBatch& fb = g_folChunks[i].batch[b];
            if (fb.vbo) gr_glDeleteBuffers(1, &fb.vbo);
            if (fb.ibo) gr_glDeleteBuffers(1, &fb.ibo);
        }
    g_folChunks.clear();
    g_folChunkInst.clear();
    g_folCW = g_folCH = g_folGW = g_folGH = 0;
    foliage3d_crush_reset();
}

/* Everything the pack and the mission hold, handed back at the mission's teardown: the
   pack is loaded again at the next mission's start, as the trees' is. */
static void foliage3d_free(void)
{
    foliage3d_free_chunks();
    tree3d_gpu_release(g_folModel);
    for (size_t i = 0; i < g_folSet.size(); i++)
        glDeleteTextures(4, g_folSet[i].tex);
    g_folSet.clear();
    g_folModel.clear();
    g_folCode.clear();
    g_folHave = false;
    g_rockModels.clear();
    g_folModelKind.clear();
    g_rockHave = false;
    g_rockPainted = false;
    g_folPrepFrame = -1;
}

static unsigned fol_hash(unsigned a, unsigned b, unsigned c)
{
    unsigned h = a * 73856093u ^ b * 19349663u ^ c * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    h *= 0x27d4eb2du; h ^= h >> 16;
    return h;
}
static float fol_rand(unsigned a, unsigned b, unsigned c)
{
    return (float)(fol_hash(a, b, c) & 0xFFFFFFu) / 16777215.0f;
}

/* THE THICKET FIELD: value noise over a lattice fol_cluster_len cells apart, two
   octaves, smoothly interpolated. 0..1 and roughly centred on a half. */
static float fol_noise1(float x, float z, float len, unsigned seed)
{
    const float fx = x / len, fz = z / len;
    const int ix = (int)floorf(fx), iz = (int)floorf(fz);
    float tx = fx - (float)ix, tz = fz - (float)iz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a = fol_rand((unsigned)ix,     (unsigned)iz,     seed);
    const float b = fol_rand((unsigned)ix + 1, (unsigned)iz,     seed);
    const float c = fol_rand((unsigned)ix,     (unsigned)iz + 1, seed);
    const float d = fol_rand((unsigned)ix + 1, (unsigned)iz + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}
static float fol_field(float x, float z)
{
    const unsigned seed = 7001u + (unsigned)g_fx.fol_seed * 131u;
    const float L = g_fx.fol_cluster_len > 0.5f ? g_fx.fol_cluster_len : 0.5f;
    return 0.68f * fol_noise1(x, z, L, seed) + 0.32f * fol_noise1(x, z, L * 0.43f, seed + 17u);
}

/* How many times the average this spot carries. At clustering 0 every cell is the
   average; at 1 the thinnest 60% of the field is bare and the rest carries the plants
   that would have stood there, so the total stays roughly where the amount dials put it. */
static float fol_weight(float x, float z)
{
    const float c = g_fx.fol_cluster;
    if (c <= 0.0f) return 1.0f;
    const float n = fol_field(x, z);
    const float t = 0.30f + 0.30f * c;          /* the share of the field left open */
    float s = (n - (t - 0.10f)) / 0.20f;
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    s = s * s * (3.0f - 2.0f * s);
    const float thick = s / (1.0f - t);
    return 1.0f + (thick - 1.0f) * c;
}

/* ---- Which cells may carry a plant, this frame ---------------------------------- */
static void fol_block(int x, int y, unsigned char why)
{
    if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) return;
    const size_t i = (size_t)y * g_gridW + x;
    if (g_folOk[i]) { g_folOk[i] = 0; g_folWhy[i] = why; }
}

static void fol_block_rect(float cx, float cz, int w, int h, unsigned char why)
{
    if (w <= 0 || h <= 0) return;
    const int x0 = (int)floorf(cx - 0.5f * (float)w + 0.01f);
    const int z0 = (int)floorf(cz - 0.5f * (float)h + 0.01f);
    for (int y = z0; y < z0 + h; y++)
        for (int x = x0; x < x0 + w; x++) fol_block(x, y, why);
}

static void rock_face_update(void);
static void foliage3d_mark(void)
{
    const size_t n = (size_t)g_gridW * g_gridH;
    g_folOk.assign(n, 0);
    g_folWhy.assign(n, 1);
    g_folSlot.assign(n, -1);
    /* THE CLEAR TEMPLATE, read off the atlas slot exactly as the ground patches read it
       (mottle_mark_clear): slots 0 to 15 in every theater's table and nothing else. */
    for (size_t i = 0; i < g_pack.cell.size(); i++) {
        const PackCell& c = g_pack.cell[i];
        if (c.x < 0 || c.y < 0 || c.x >= g_gridW || c.y >= g_gridH) continue;
        const float left = std::min(c.u0, c.u1), top = std::min(c.v0, c.v1);
        const int col = (int)(left * 32.0f), row = (int)(top * 24.0f);
        const size_t k = (size_t)c.y * g_gridW + c.x;
        g_folSlot[k] = row * 32 + col;
        if (row == 0 && col < 16) { g_folOk[k] = 1; g_folWhy[k] = 0; }
    }
    if (g_raModule) {
        // Classify RA's actual map, never the TD donor pack's atlas coordinates.
        for (int y=0;y<g_gridH;y++) for(int x=0;x<g_gridW;x++) {
            const size_t k=(size_t)y*g_gridW+x;
            const int flags=ra3d_detail_cell(x,y,&g_folSlot[k]);
            g_folOk[k]=(flags&1)?1:0;g_folWhy[k]=g_folOk[k]?0:1;
        }
    }
    for (int y = 0; y < g_gridH; y++)
        for (int x = 0; x < g_gridW; x++) {
            const size_t k = (size_t)y * g_gridW + x;
            if (!g_folOk[k]) continue;
            if (!cell_shown(x, y))            { g_folOk[k] = 0; g_folWhy[k] = 2; }
            else if (shroud_cell_hidden(x, y)) { g_folOk[k] = 0; g_folWhy[k] = 3; }
        }
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.limbo) continue;
        if (o.kind == K_BUILDING) {
            fol_block_rect(o.ex, o.ez, o.fw, o.fh, 4);
            fol_block(o.cx, o.cy, 4);
        } else if (o.kind == K_TERRAIN) {
            fol_block(o.cx, o.cy, 5);
            fol_block_rect(o.ex, o.ez, o.fw, o.fh, 5);
            const ClumpDef* cd = clump_def(o.type);
            for (int c = 0; cd && c < clump_def_count(cd); c++) {
                const SimObject part = clump_part_n(o, cd, c);
                fol_block(part.cx, part.cy, 5);
            }
        }
    }
    for (size_t i = 0; i < g_tib.size(); i++)     fol_block(g_tib[i].x, g_tib[i].y, 6);
    for (size_t i = 0; i < g_walls.size(); i++)   fol_block(g_walls[i].x, g_walls[i].y, 7);
    for (size_t i = 0; i < g_crates.size(); i++)  fol_block(g_crates[i].x, g_crates[i].y, 8);
    for (size_t i = 0; i < g_smudges.size(); i++) fol_block(g_smudges[i].x, g_smudges[i].y, 9);
    /* a field or haystack placed as an overlay covers its cell; a bush grew through it */
    for (size_t i = 0; i < g_fields.size(); i++)  fol_block(g_fields[i].x, g_fields[i].y, 10);
    if (fol_rocks_on()) rock_face_update();
}

/* ---- WHERE THE ROCK FACES ARE ------------------------------------------------------
   A rock face is a cell whose template is one of the engine's SLOPE1..SLOPE38 (the
   cliffs and rock walls, template numbers 13 to 50) or BOULDER1..BOULDER6 (82 to 87), or
   one whose ground climbs steeply across it (corners more than 0.35 of a cell apart in
   height), which catches the heightfield's own escarpments where the tile art is plain.
   The template comes from the cell's atlas slot through the theater's own tile table
   (terrain_tiles.h) read backwards. g_rockFace holds each cell's distance in cells to the
   nearest face (a chamfer transform, 1 and 1.414), recomputed only when the map's slots
   change. */
static std::vector<float> g_rockFace;
static std::vector<unsigned char> g_rockWater;   /* per cell: water or shore template */
static unsigned g_rockFaceKey = 0;
static int g_rockFaceCells = 0;

static void rock_face_update(void)
{
    const size_t n = (size_t)g_gridW * g_gridH;
    unsigned key = 2166136261u ^ (unsigned)g_gridW * 131u ^ (unsigned)g_gridH;
    for (size_t i = 0; i < g_folSlot.size(); i++) { key ^= (unsigned)(g_folSlot[i] + 7); key *= 16777619u; }
    for (const char* c = g_pack.theater; *c; c++) { key ^= (unsigned char)*c; key *= 16777619u; }
    if (key == g_rockFaceKey && g_rockFace.size() == n) return;
    g_rockFaceKey = key;
    int tn = 0;
    const TtSlot* tab = tt_table(g_pack.theater, &tn);
    std::vector<int> tmplOf;
    for (int i = 0; tab && i < tn; i++) {
        if ((int)tab[i].slot >= (int)tmplOf.size()) tmplOf.resize(tab[i].slot + 1, -1);
        tmplOf[tab[i].slot] = tab[i].tmpl;
    }
    const float INF = 1.0e9f;
    g_rockFace.assign(n, INF);
    g_rockWater.assign(n, 0);
    g_rockFaceCells = 0;
    for (int y = 0; y < g_gridH; y++)
        for (int x = 0; x < g_gridW; x++) {
            const size_t k = (size_t)y * g_gridW + x;
            const int slot = g_folSlot[k];
            const int t = (slot >= 0 && slot < (int)tmplOf.size()) ? tmplOf[slot] : -1;
            bool face = (t >= 13 && t <= 50) || (t >= 82 && t <= 87);
            /* WATER, WATER2, the rivers and the falls: a thrown rock that lands here has
               sunk. The shores are left out, being half beach. Template numbers from the
               engine's own enum (defines.h). */
            if (t == 1 || t == 2 || (t >= 136 && t <= 160) || t == 163 || t == 164)
                g_rockWater[k] = 1;
            if(g_raModule) {
                int nativeSlot=-1;const int flags=ra3d_detail_cell(x,y,&nativeSlot);
                face=(flags&2)!=0;g_rockWater[k]=(flags&4)?1:0;
            }
            if (!face) {
                float lo = 1e9f, hi = -1e9f;
                for (int c = 0; c < 4; c++) {
                    const float h = terrain_corner_y(x + (c & 1), y + (c >> 1));
                    lo = h < lo ? h : lo; hi = h > hi ? h : hi;
                }
                face = hi - lo > 0.35f;
            }
            if (face) { g_rockFace[k] = 0.0f; g_rockFaceCells++; }
        }
    /* two chamfer passes */
    const float D1 = 1.0f, D2 = 1.41421f;
    for (int y = 0; y < g_gridH; y++)
        for (int x = 0; x < g_gridW; x++) {
            float& v = g_rockFace[(size_t)y * g_gridW + x];
            if (x > 0) v = std::min(v, g_rockFace[(size_t)y * g_gridW + x - 1] + D1);
            if (y > 0) {
                v = std::min(v, g_rockFace[(size_t)(y - 1) * g_gridW + x] + D1);
                if (x > 0) v = std::min(v, g_rockFace[(size_t)(y - 1) * g_gridW + x - 1] + D2);
                if (x < g_gridW - 1) v = std::min(v, g_rockFace[(size_t)(y - 1) * g_gridW + x + 1] + D2);
            }
        }
    for (int y = g_gridH - 1; y >= 0; y--)
        for (int x = g_gridW - 1; x >= 0; x--) {
            float& v = g_rockFace[(size_t)y * g_gridW + x];
            if (x < g_gridW - 1) v = std::min(v, g_rockFace[(size_t)y * g_gridW + x + 1] + D1);
            if (y < g_gridH - 1) {
                v = std::min(v, g_rockFace[(size_t)(y + 1) * g_gridW + x] + D1);
                if (x < g_gridW - 1) v = std::min(v, g_rockFace[(size_t)(y + 1) * g_gridW + x + 1] + D2);
                if (x > 0) v = std::min(v, g_rockFace[(size_t)(y + 1) * g_gridW + x - 1] + D2);
            }
        }
}

static inline bool fol_ok(int x, int y)
{
    if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) return false;
    return g_folOk[(size_t)y * g_gridW + x] != 0;
}

/* Every cell whose nearest point lies closer than m to (px,pz) is plain grass. */
static bool fol_clear_around(float px, float pz, float m)
{
    if (!fol_ok((int)floorf(px), (int)floorf(pz))) return false;
    if (m <= 0.0f) return true;
    const int x0 = (int)floorf(px - m), x1 = (int)floorf(px + m);
    const int z0 = (int)floorf(pz - m), z1 = (int)floorf(pz + m);
    for (int y = z0; y <= z1; y++)
        for (int x = x0; x <= x1; x++) {
            const float nx = px < (float)x ? (float)x : (px > (float)(x + 1) ? (float)(x + 1) : px);
            const float nz = pz < (float)y ? (float)y : (pz > (float)(y + 1) ? (float)(y + 1) : pz);
            const float dx = px - nx, dz = pz - nz;
            if (dx * dx + dz * dz < m * m && !fol_ok(x, y)) return false;
        }
    return true;
}

/* THE LAYOUT DIALS, as one number. Anything that moves where a plant stands or how it
   is built is in it; the look dials (wind, flutter, exposure) are uniforms and are not. */
static unsigned fol_layout_sig(void)
{
    const float v[] = { g_fx.fol_bush_amt, g_fx.fol_grass_amt, g_fx.fol_bush_size,
                        g_fx.fol_grass_size, g_fx.fol_vary, g_fx.fol_cluster,
                        g_fx.fol_cluster_len, g_fx.fol_margin, g_fx.fol_bush_mix,
                        g_fx.fol_g1, g_fx.fol_g2, g_fx.fol_g3, g_fx.fol_tilt, g_fx.fol_seed,
                        g_fx.fol_bush_detail, (float)fol_plants_on(), (float)fol_rocks_on(),
                        g_fx.rock_amt, g_fx.rock_spread, g_fx.rock_falloff, g_fx.rock_size,
                        g_fx.rock_vary, g_fx.rock_shrink, g_fx.rock_detail, g_fx.rock_c1,
                        g_fx.rock_c2, g_fx.rock_c3, g_fx.rock_sink, g_fx.rock_margin,
                        g_fx.rock_seed, (float)(g_rockFaceKey & 0xFFFFu) };
    unsigned h = 2166136261u;
    const unsigned char* p = (const unsigned char*)v;
    for (size_t i = 0; i < sizeof v; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static unsigned fol_chunk_sig(int cx, int cy, unsigned layout, bool withPad = true)
{
    unsigned h = layout ^ 0x9e3779b9u;
    const int R = 2;              /* the margin reaches at most 1.5 cells past the chunk */
    const int x0 = cx * FOL_CHUNK - R, x1 = cx * FOL_CHUNK + FOL_CHUNK + R;
    const int z0 = cy * FOL_CHUNK - R, z1 = cy * FOL_CHUNK + FOL_CHUNK + R;
    for (int y = z0; y < z1; y++)
        for (int x = x0; x < x1; x++) {
            h ^= fol_ok(x, y) ? 1u : 0u;
            h *= 16777619u;
        }
    h^=desert_detail_state() + g_groundGen*31u + (withPad ? g_padEpoch*7919u : 0u);
    /* and the ground under it, which a building pad reshapes */
    for (int y = cy * FOL_CHUNK; y <= cy * FOL_CHUNK + FOL_CHUNK && y <= g_gridH; y++)
        for (int x = cx * FOL_CHUNK; x <= cx * FOL_CHUNK + FOL_CHUNK && x <= g_gridW; x++) {
            const int q = (int)floorf(terrain_corner_y(x, y) * 512.0f);
            h ^= (unsigned)q;
            h *= 16777619u;
        }
    return h ? h : 1u;
}

/* ---- Building a chunk ----------------------------------------------------------- */
/* Interleaved, 28 bytes: position and uv as floats, the normal as signed bytes (GL
   normalises them), then the four baked bytes. A default scatter on the largest
   temperate mission is about a million vertices, so every byte here is a megabyte. */
struct FolVert { float p[3]; signed char n[4]; float uv[2]; unsigned char c[4];
                 float t1[4]; /* the plant's id (-1: none) and its root x, y, z */ };

static int fol_pick_grass(float r)
{
    float w[3] = { g_fx.fol_g1, g_fx.fol_g2, g_fx.fol_g3 };
    float tot = 0.0f;
    for (int i = 0; i < 3; i++) { if (g_folGrass[i] < 0) w[i] = 0.0f; tot += w[i]; }
    if (tot <= 0.0f) return -1;
    r *= tot;
    for (int i = 0; i < 3; i++) { if (r < w[i]) return g_folGrass[i]; r -= w[i]; }
    for (int i = 2; i >= 0; i--) if (w[i] > 0.0f) return g_folGrass[i];
    return -1;
}

static int fol_pick_bush(float r)
{
    const int a = g_folBush[0], b = g_folBush[1];
    if (a < 0) return b;
    if (b < 0) return a;
    return r < g_fx.fol_bush_mix ? b : a;
}

static void fol_emit(std::vector<FolVert>& V, std::vector<unsigned int>& I,
                     const Tree3dModel& m, float px, float py, float pz, float h,
                     float yaw, float tiltA, float tiltDir, int id)
{
    const float tid = (id >= 0 && id < FOL_CRUSH_W * FOL_CRUSH_MAXH) ? (float)id : -1.0f;
    const float cy = cosf(yaw), sy = sinf(yaw);
    /* the tilt: a small rotation about the horizontal axis at right angles to tiltDir */
    const float ax = -sinf(tiltDir), az = cosf(tiltDir);
    const float ct = cosf(tiltA), st = sinf(tiltA);
    const unsigned int base = (unsigned int)V.size();
    const size_t nv = m.pos.size() / 3;
    const float swayScale = h / FOL_HREF;
    for (size_t i = 0; i < nv; i++) {
        float x = m.pos[i*3+0], y = m.pos[i*3+1], z = m.pos[i*3+2];
        float nx = m.nrm[i*3+0], ny = m.nrm[i*3+1], nz = m.nrm[i*3+2];
        float rx = x * cy - z * sy, rz = x * sy + z * cy;
        float rnx = nx * cy - nz * sy, rnz = nx * sy + nz * cy;
        /* Rodrigues about (ax,0,az) */
        {
            const float d = ax * rx + az * rz;
            const float cxv = -az * y, cyv = az * rx - ax * rz, czv = ax * y;  /* k x v */
            const float tx = rx * ct + cxv * st + ax * d * (1.0f - ct);
            const float ty = y  * ct + cyv * st;
            const float tz = rz * ct + czv * st + az * d * (1.0f - ct);
            rx = tx; y = ty; rz = tz;
            const float dn = ax * rnx + az * rnz;
            const float nxv = -az * ny, nyv = az * rnx - ax * rnz, nzv = ax * ny;
            const float ux = rnx * ct + nxv * st + ax * dn * (1.0f - ct);
            const float uy = ny  * ct + nyv * st;
            const float uz = rnz * ct + nzv * st + az * dn * (1.0f - ct);
            rnx = ux; ny = uy; rnz = uz;
        }
        FolVert v;
        v.p[0] = px + rx * h; v.p[1] = py + y * h; v.p[2] = pz + rz * h;
        v.n[0] = (signed char)floorf(rnx * 127.0f + 0.5f);
        v.n[1] = (signed char)floorf(ny  * 127.0f + 0.5f);
        v.n[2] = (signed char)floorf(rnz * 127.0f + 0.5f);
        v.n[3] = 0;
        v.uv[0] = m.uv[i*2+0]; v.uv[1] = m.uv[i*2+1];
        float sw = (float)m.col[i*4+0] * swayScale;
        v.c[0] = (unsigned char)(sw > 255.0f ? 255.0f : sw + 0.5f);
        v.c[1] = m.col[i*4+1]; v.c[2] = m.col[i*4+2]; v.c[3] = m.col[i*4+3];
        v.t1[0] = tid; v.t1[1] = px; v.t1[2] = py; v.t1[3] = pz;
        V.push_back(v);
    }
    for (size_t i = 0; i < m.idx.size(); i++) I.push_back(base + m.idx[i]);
}

/* ---- ROCKS AT THE FOOT OF A FACE ------------------------------------------------
   A plain-grass cell within rock_spread cells of a rock face carries rocks: rock_amt a
   cell right beside the face, thinning out to nothing at the spread's edge along
   (1 - d/spread)^rock_falloff, and smaller the further out (rock_shrink), which is what
   scree does. rock_detail is the share that are the pack's small detail stones rather
   than boulders; rock_c1..c3 weigh its three colourings. */
static int rock_pick(float rStone, float rCol, bool detail)
{
    float w[3] = { g_fx.rock_c1, g_fx.rock_c2, g_fx.rock_c3 };
    float tot = w[0] + w[1] + w[2];
    int col = 0;
    if (tot > 0.0f) {
        float r = rCol * tot;
        for (col = 0; col < 2; col++) { if (r < w[col]) break; r -= w[col]; }
    }
    int cand[15], nc = 0;
    for (size_t i = 0; i < g_rockModels.size(); i++)
        if (g_rockModels[i].detail == detail && g_rockModels[i].colour == col && nc < 15)
            cand[nc++] = (int)i;
    if (!nc)
        for (size_t i = 0; i < g_rockModels.size() && nc < 15; i++) cand[nc++] = (int)i;
    if (!nc) return -1;
    int k = (int)(rStone * (float)nc);
    return cand[k >= nc ? nc - 1 : k];
}

static void rock_place_cell(int x, int y, std::vector<FolInst>& list, FolChunk& ch)
{
    if (g_rockFace.size() != (size_t)g_gridW * g_gridH) return;
    const float d = g_rockFace[(size_t)y * g_gridW + x];
    const float spread = g_fx.rock_spread > 0.05f ? g_fx.rock_spread : 0.05f;
    const float dd = d - 1.0f < 0.0f ? 0.0f : d - 1.0f;   /* 0 right beside the face */
    if (dd > spread) return;
    const float fall = powf(1.0f - dd / spread, g_fx.rock_falloff);
    const float amt = g_fx.rock_amt * fall;
    const unsigned salt = 555u + (unsigned)g_fx.rock_seed * 7919u;
    int n = (int)floorf(amt);
    if (fol_rand((unsigned)x, (unsigned)y, salt) < amt - (float)n) n++;
    for (int k = 0; k < n; k++) {
        const unsigned s = salt + 37u * (unsigned)(k + 1);
        const float px = (float)x + fol_rand((unsigned)x, (unsigned)y, s + 1u);
        const float pz = (float)y + fol_rand((unsigned)x, (unsigned)y, s + 2u);
        if (!fol_clear_around(px, pz, g_fx.rock_margin)) continue;
        const bool detail = fol_rand((unsigned)x, (unsigned)y, s + 3u) < g_fx.rock_detail;
        const int ri = rock_pick(fol_rand((unsigned)x, (unsigned)y, s + 4u),
                                 fol_rand((unsigned)x, (unsigned)y, s + 5u), detail);
        if (ri < 0) continue;
        const float r = fol_rand((unsigned)x, (unsigned)y, s + 6u);
        float size = g_fx.rock_size * (1.0f + g_fx.rock_vary * (r * 2.0f - 1.0f))
                     * (1.0f - g_fx.rock_shrink * dd / spread) * (detail ? 0.45f : 1.0f);
        if (size < 0.02f) size = 0.02f;
        const int id = fol_id_for(x, y, 2, k, px, pz);
        g_rockIdModel[id] = ri;
        g_rockIdSize[id] = size;
        g_rockIdYaw[id] = fol_rand((unsigned)x, (unsigned)y, s + 7u) * 6.2831853f;
        g_folCellIds[(size_t)y * g_gridW + x].push_back(id);
        if (g_rockDynOf[id] >= 0) continue;           /* it has been moved: drawn alone */
        FolInst fi;
        fi.cx = (short)x; fi.cy = (short)y; fi.model = (short)g_rockModels[ri].model;
        fi.x = px; fi.z = pz; fi.h = size; fi.id = id;
        list.push_back(fi);
        ch.nrock++;
    }
}

static void foliage3d_build_chunk(int cx, int cy)
{
    FolChunk& ch = g_folChunks[(size_t)cy * g_folCW + cx];
    std::vector<FolInst>& list = g_folChunkInst[(size_t)cy * g_folCW + cx];
    for (size_t b = 0; b < ch.batch.size(); b++) {
        if (ch.batch[b].vbo) gr_glDeleteBuffers(1, &ch.batch[b].vbo);
        if (ch.batch[b].ibo) gr_glDeleteBuffers(1, &ch.batch[b].ibo);
    }
    ch.batch.clear();
    ch.nbush = ch.nclump = ch.nrock = 0;
    ch.boxed = false;
    list.clear();

    const unsigned seed = 101u + (unsigned)g_fx.fol_seed * 7919u;
    const float m = g_fx.fol_margin;
    if (g_folCellIds.size() != (size_t)g_gridW * g_gridH)
        g_folCellIds.assign((size_t)g_gridW * g_gridH, std::vector<int>());
    for (int y = cy * FOL_CHUNK; y < cy * FOL_CHUNK + FOL_CHUNK && y < g_gridH; y++)
        for (int x = cx * FOL_CHUNK; x < cx * FOL_CHUNK + FOL_CHUNK && x < g_gridW; x++)
            g_folCellIds[(size_t)y * g_gridW + x].clear();
    for (int y = cy * FOL_CHUNK; y < cy * FOL_CHUNK + FOL_CHUNK && y < g_gridH; y++)
        for (int x = cx * FOL_CHUNK; x < cx * FOL_CHUNK + FOL_CHUNK && x < g_gridW; x++) {
            if (!fol_ok(x, y)) continue;
            if (fol_rocks_on()) rock_place_cell(x, y, list, ch);
            if (!fol_plants_on()) continue;
            const float w = fol_weight((float)x + 0.5f, (float)y + 0.5f);
            for (int kind = 0; kind < 2; kind++) {
                const float amt = (kind == 0 ? g_fx.fol_bush_amt : g_fx.fol_grass_amt) * w;
                const unsigned salt = seed + (unsigned)kind * 1000003u;
                int n = (int)floorf(amt);
                if (fol_rand((unsigned)x, (unsigned)y, salt) < amt - (float)n) n++;
                for (int k = 0; k < n; k++) {
                    const unsigned s = salt + 31u * (unsigned)(k + 1);
                    const float px = (float)x + fol_rand((unsigned)x, (unsigned)y, s + 1u);
                    const float pz = (float)y + fol_rand((unsigned)x, (unsigned)y, s + 2u);
                    if (!fol_clear_around(px, pz, m)) continue;
                    const int model = kind == 0
                        ? fol_pick_bush(fol_rand((unsigned)x, (unsigned)y, s + 3u))
                        : fol_pick_grass(fol_rand((unsigned)x, (unsigned)y, s + 3u));
                    if (model < 0 || model >= (int)g_folModel.size()) continue;
                    const float size = kind == 0 ? g_fx.fol_bush_size : g_fx.fol_grass_size;
                    const float r = fol_rand((unsigned)x, (unsigned)y, s + 4u);
                    FolInst fi;
                    fi.cx = (short)x; fi.cy = (short)y; fi.model = (short)model;
                    fi.x = px; fi.z = pz;
                    fi.h = size * (1.0f + g_fx.fol_vary * (r * 2.0f - 1.0f));
                    fi.id = fol_id_for(x, y, kind, k, px, pz);
                    g_folCellIds[(size_t)y * g_gridW + x].push_back(fi.id);
                    list.push_back(fi);
                    if (kind == 0) ch.nbush++; else ch.nclump++;
                }
            }
        }

    desert_details_place_chunk(cx,cy,list,ch);
    /* one buffer pair per model that has any plants here */
    for (size_t mi = 0; mi < g_folModel.size(); mi++) {
        std::vector<FolVert> V;
        std::vector<unsigned int> I;
        int count = 0;
        const Tree3dModel& M = g_folModel[mi];
        {
            size_t n = 0;
            for (size_t i = 0; i < list.size(); i++) if (list[i].model == (int)mi) n++;
            V.reserve(n * (M.pos.size() / 3));
            I.reserve(n * M.idx.size());
        }
        for (size_t i = 0; i < list.size(); i++) {
            const FolInst& fi = list[i];
            if (fi.model != (int)mi) continue;
            const unsigned s = fol_hash((unsigned)(fi.x * 4096.0f), (unsigned)(fi.z * 4096.0f), seed);
            const float yaw = (float)(s & 1023u) * (6.2831853f / 1024.0f);
            const float tr = (float)((s >> 10) & 1023u) / 1023.0f;
            const float td = (float)((s >> 20) & 1023u) * (6.2831853f / 1024.0f);
            if (g_folKindOf[fi.id] == 2) {
                /* A ROCK sits on its own lowest point, sunk by rock_sink of its size, at
                   the yaw it was given, which the physics takes over from if it moves. */
                const RockModel& rm = g_rockModels[g_rockIdModel[fi.id]];
                const float py = terrain_y(fi.x, fi.z) + (-rm.minY - g_fx.rock_sink) * fi.h;
                fol_emit(V, I, M, fi.x, py, fi.z, fi.h, g_rockIdYaw[fi.id], 0.0f, 0.0f, fi.id);
                count++;
                continue;
            }
            /* sunk a little, so a plant on a slope does not show daylight under its root */
            const float py = terrain_y(fi.x, fi.z) - 0.03f * fi.h;
            fol_emit(V, I, M, fi.x, py, fi.z, fi.h, yaw, g_fx.fol_tilt * tr, td, fi.id);
            count++;
        }
        if (!count) continue;
        for (size_t i = 0; i < V.size(); i++) {
            const FolVert& v = V[i];
            if (!ch.boxed) {
                for (int k = 0; k < 3; k++) ch.lo[k] = ch.hi[k] = v.p[k];
                ch.rootLo = v.t1[2];
                ch.boxed = true;
            }
            for (int k = 0; k < 3; k++) {
                if (v.p[k] < ch.lo[k]) ch.lo[k] = v.p[k];
                if (v.p[k] > ch.hi[k]) ch.hi[k] = v.p[k];
            }
            if (v.t1[2] < ch.rootLo) ch.rootLo = v.t1[2];
        }
        FolBatch fb;
        fb.model = (int)mi; fb.count = count; fb.nidx = (int)I.size();
        gr_glGenBuffers(1, &fb.vbo);
        gr_glGenBuffers(1, &fb.ibo);
        gr_glBindBuffer(GL_ARRAY_BUFFER, fb.vbo);
        gr_glBufferData(GL_ARRAY_BUFFER, (FxGLsizeiptr)(V.size() * sizeof(FolVert)), &V[0],
                        GL_STATIC_DRAW);
        gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fb.ibo);
        gr_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (FxGLsizeiptr)(I.size() * sizeof(unsigned int)),
                        &I[0], GL_STATIC_DRAW);
        ch.batch.push_back(fb);
    }
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    ch.built = true;
    g_folBuilt++;
}

/* WHAT THE VEHICLES DID THIS FRAME. A vehicle whose centre comes within fol_crush_r of a
   plant's root lays it down; from then on the plant's own clock runs: flat over
   fol_flat_time, then gone over fol_fade_time, and gone for the rest of the mission.
   Infantry walk through, aircraft fly over. */
/* instant: a plant found under a hull is gone already rather than laid down now. That
   is the answer for plants BUILT under a vehicle (the map's first frame, or a chunk
   rebuilt around a parked tank), which were never standing in anything the player saw. */
static void fol_crush_scan(float now, bool instant)
{
    {
        const float m = g_fx.fol_crush_r;
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_UNIT || o.limbo) continue;
            /* ENGINE TRUTH (ex, ez), not the smoothed draw position, so the record is the
               same however many frames were drawn. THE HULL, NOT A CIRCLE. A fixed reach around the centre let the nose of a
               long vehicle (the MCV is 1.6 cells) run into a bush while the bush still
               stood, and the bush showed through the hull until the centre arrived. The
               grass's own measurement of each hull off its mesh is reused, turned to the
               vehicle's facing (0 north, 64 east, as the engine counts it; the world's z
               runs south), and fol_crush_r is the margin around it. */
            const CrushHull hull = crush_hull_for_mesh(mesh_for(o));
            float hx = 0.0f, hz = 1.0f;
            if (o.face >= 0) {
                const float a = (float)o.face * (6.2831853f / 256.0f);
                hx = sinf(a); hz = -cosf(a);
            }
            const float halfL = 0.5f * hull.len + m, halfB = 0.5f * hull.beam + m;
            const float r = o.face >= 0 ? halfL : (halfL > halfB ? halfL : halfB);
            const int x0 = (int)floorf(o.ex - r), x1 = (int)floorf(o.ex + r);
            const int z0 = (int)floorf(o.ez - r), z1 = (int)floorf(o.ez + r);
            for (int y = z0; y <= z1; y++)
                for (int x = x0; x <= x1; x++) {
                    if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) continue;
                    const std::vector<int>& ids = g_folCellIds[(size_t)y * g_gridW + x];
                    for (size_t k = 0; k < ids.size(); k++) {
                        const int id = ids[k];
                        if (g_folKindOf[id] >= 2) continue;      /* rocks roll, see below */
                        if (g_folCrushT[id] >= 0.0f) continue;
                        const float dx = g_folRootX[id] - o.ex, dz = g_folRootZ[id] - o.ez;
                        const float along = dx * hx + dz * hz;
                        const float across = -dx * hz + dz * hx;
                        const bool in = o.face >= 0
                            ? (fabsf(along) < halfL && fabsf(across) < halfB)
                            : (dx * dx + dz * dz < r * r);
                        if (in) {
                            g_folCrushT[id] = instant ? now - 1000.0f : now;
                            g_folCrushed.push_back(id);
                        }
                    }
                }
        }
    }
}

/* ==================================================================================
 *  ROCK PHYSICS
 *
 *  A rock is still until something moves it: a vehicle driving into it, or the ground
 *  pulse of a structure going up nearby. From then on it is a rigid body of its own, and
 *  it leaves the static batch (its texel there goes to "gone") for a list drawn one by
 *  one with its own position and rotation.
 *
 *  THE SHAPE IS THE MESH'S. The body touches the ground only at its 26 extreme vertices
 *  (rock_support_points), each an impulse contact against the heightfield with
 *  restitution and Coulomb friction, so a round stone rolls on and a flat or lopsided
 *  one flops over and stops. Mass goes with the cube of its size and its inertia is a
 *  ball's, so a big rock takes a small kick from the same shove, and the same friction
 *  then stops it sooner in both time and distance.
 *
 *  ON THE ENGINE'S CLOCK. Stepped once per engine tick in four substeps, from engine
 *  truth (a vehicle's ex/ez), so the result is the same however many frames are drawn
 *  and two scripted runs agree; the draw interpolates the last two tick poses with the
 *  tick's own sub-tick phase. It is decoration: nothing it does reaches the engine, and
 *  a rock never blocks a unit.
 * ================================================================================== */
struct RockDyn {
    int   id, rm;                       /* its id, and its entry in g_rockModels      */
    float size, invM, invI;
    float p[3], v[3], w[3], q[4];       /* position, velocity, spin, orientation      */
    float pp[3], pq[4];                 /* the pose at the start of this tick         */
    int   still;
    bool  sleep, gone;
};
static std::vector<RockDyn> g_rockDyn;
struct RockPulse { float cx, cz, hw, hh; };
static std::vector<RockPulse> g_rockPulses;
static std::map<int, std::pair<float, float> > g_rockVehPrev;
static int g_rockHits = 0, g_rockPulsed = 0;

static void rock_dyn_reset(void)
{
    g_rockDyn.clear(); g_rockPulses.clear(); g_rockVehPrev.clear();
    g_rockHits = g_rockPulsed = 0;
}

/* A structure going up: queued by the object refresh (cnc_eyes.cpp), spent by the next
   rock tick. Centre and half extents in cells. */
static void foliage3d_building_placed(float cx, float cz, int fw, int fh)
{
    RockPulse rp;
    rp.cx = cx; rp.cz = cz;
    rp.hw = 0.5f * (float)(fw > 0 ? fw : 1); rp.hh = 0.5f * (float)(fh > 0 ? fh : 1);
    g_rockPulses.push_back(rp);
}

static void q_rot(const float q[4], const float v[3], float out[3])
{
    /* v' = q v q*, q = (w, x, y, z) */
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    const float tx = 2.0f * (y * v[2] - z * v[1]);
    const float ty = 2.0f * (z * v[0] - x * v[2]);
    const float tz = 2.0f * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

static void q_mat3(const float q[4], float m[9])
{
    /* column-major: m[col * 3 + row] */
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    m[0] = 1 - 2*(y*y + z*z); m[3] = 2*(x*y - w*z);     m[6] = 2*(x*z + w*y);
    m[1] = 2*(x*y + w*z);     m[4] = 1 - 2*(x*x + z*z); m[7] = 2*(y*z - w*x);
    m[2] = 2*(x*z - w*y);     m[5] = 2*(y*z + w*x);     m[8] = 1 - 2*(x*x + y*y);
}

/* The static rock with this id becomes a body, in exactly the pose it was drawn in. */
static int rock_wake(int id)
{
    if (id < 0 || id >= (int)g_rockDynOf.size() || g_rockIdModel[id] < 0) return -1;
    if (g_rockDynOf[id] >= 0) {
        RockDyn& d = g_rockDyn[g_rockDynOf[id]];
        d.sleep = false; d.still = 0;
        return g_rockDynOf[id];
    }
    const RockModel& rm = g_rockModels[g_rockIdModel[id]];
    RockDyn d;
    d.id = id; d.rm = g_rockIdModel[id];
    d.size = g_rockIdSize[id];
    const float m = d.size * d.size * d.size;
    const float r = 0.4f * d.size;
    d.invM = 1.0f / m;
    d.invI = 1.0f / (0.4f * m * r * r);
    const float x = g_folRootX[id], z = g_folRootZ[id];
    d.p[0] = x; d.p[2] = z;
    d.p[1] = terrain_y(x, z) + (-rm.minY - g_fx.rock_sink) * d.size;
    /* fol_emit turns (x, z) by (c, -s; s, c): a rotation about +y by minus the yaw */
    const float th = -g_rockIdYaw[id];
    d.q[0] = cosf(0.5f * th); d.q[1] = 0.0f; d.q[2] = sinf(0.5f * th); d.q[3] = 0.0f;
    for (int k = 0; k < 3; k++) { d.v[k] = d.w[k] = 0.0f; d.pp[k] = d.p[k]; }
    for (int k = 0; k < 4; k++) d.pq[k] = d.q[k];
    d.still = 0; d.sleep = false; d.gone = false;
    g_rockDynOf[id] = (int)g_rockDyn.size();
    g_rockDyn.push_back(d);
    return g_rockDynOf[id];
}

/* How hard a shove moves a rock of this size: a big one takes much less. */
static float rock_kick_scale(float size)
{
    /* A SMALL STONE IS NOT KICKED WITHOUT LIMIT. Below 0.6 of the reference size the
       scale stops growing: measured on SCG01EA, an unclamped 0.11-cell pebble took three
       and a half times the push and was thrown thirty cells into the sea. */
    float rel = size / 0.3f;
    if (rel < 0.6f) rel = 0.6f;
    return 1.0f / powf(rel, g_fx.rock_mass);
}

static void rock_ground_normal(float x, float z, float n[3])
{
    const float e = 0.15f;
    const float dx = terrain_y(x + e, z) - terrain_y(x - e, z);
    const float dz = terrain_y(x, z + e) - terrain_y(x, z - e);
    n[0] = -dx; n[1] = 2.0f * e; n[2] = -dz;
    const float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    n[0] /= l; n[1] /= l; n[2] /= l;
}

/* One ground contact of a body, as the solver below sees it. */
struct RockContact {
    float r[3], n[3], t1[3], t2[3];     /* arm from the centre, normal, two tangents   */
    float kn, kt1, kt2;                 /* effective mass along each, inverted         */
    float target;                       /* the normal speed the contact must reach     */
    float jn, jt1, jt2;                 /* impulse accumulated over the iterations     */
};

static void rock_cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1]*b[2] - a[2]*b[1]; o[1] = a[2]*b[0] - a[0]*b[2]; o[2] = a[0]*b[1] - a[1]*b[0];
}

static float rock_k(const RockDyn& d, const float r[3], const float dir[3])
{
    float rd[3];
    rock_cross(r, dir, rd);
    return d.invM + d.invI * (rd[0]*rd[0] + rd[1]*rd[1] + rd[2]*rd[2]);
}

/* Apply impulse j along dir at arm r. */
static void rock_impulse(RockDyn& d, const float r[3], const float dir[3], float j)
{
    float rd[3];
    rock_cross(r, dir, rd);
    for (int k = 0; k < 3; k++) { d.v[k] += dir[k] * j * d.invM; d.w[k] += rd[k] * j * d.invI; }
}

static float rock_point_speed(const RockDyn& d, const float r[3], const float dir[3])
{
    float wr[3];
    rock_cross(d.w, r, wr);
    return (d.v[0] + wr[0]) * dir[0] + (d.v[1] + wr[1]) * dir[1] + (d.v[2] + wr[2]) * dir[2];
}

static void rock_step(RockDyn& d, float dt)
{
    const RockModel& rm = g_rockModels[d.rm];
    d.v[1] -= g_fx.rock_gravity * dt;
    /* ALL THE CONTACTS AT ONCE, SOLVED TOGETHER. Each support point at or within a hair of
       the ground is a contact, and the set is relaxed over several passes with impulses
       accumulated per contact (the normal one never pulling, the friction one inside its
       cone). One pass per point, applied in turn, left whatever spin the last point's
       impulse put in: a stone resting on three corners was never quite still and crept
       downhill for as long as anyone watched.
       A point still a hair above the ground is a contact too, allowed to close the gap
       this substep and no more, so a resting stone is held where it lies rather than
       falling in and being lifted out again every substep. A fast one is left to strike
       the ground and bounce. */
    RockContact C[32];
    int nc = 0;
    const float margin = 0.005f;
    const int ns = (int)rm.sup.size() / 3;
    for (int i = 0; i < ns && nc < 32; i++) {
        float lp[3] = { rm.sup[i*3] * d.size, rm.sup[i*3+1] * d.size, rm.sup[i*3+2] * d.size };
        RockContact& c = C[nc];
        q_rot(d.q, lp, c.r);
        const float wx = d.p[0] + c.r[0], wy = d.p[1] + c.r[1], wz = d.p[2] + c.r[2];
        const float sep = wy - (terrain_y(wx, wz) - g_fx.rock_sink * d.size);
        if (sep > margin) continue;
        rock_ground_normal(wx, wz, c.n);
        const float vn = rock_point_speed(d, c.r, c.n);
        if (sep > 0.0f) {
            if (vn < -0.6f) continue;               /* a real impact: let it land first */
            c.target = -sep / dt;
        } else {
            /* bounce only off a real impact: restitution on a resting contact makes a
               settled rock buzz on the ground for ever and never sleep */
            c.target = vn < -0.6f ? -g_fx.rock_bounce * vn : 0.0f;
        }
        /* two tangents across the normal */
        const float ax[3] = { fabsf(c.n[0]) < 0.9f ? 1.0f : 0.0f, 0.0f, fabsf(c.n[0]) < 0.9f ? 0.0f : 1.0f };
        rock_cross(c.n, ax, c.t1);
        const float l1 = sqrtf(c.t1[0]*c.t1[0] + c.t1[1]*c.t1[1] + c.t1[2]*c.t1[2]);
        for (int k = 0; k < 3; k++) c.t1[k] /= l1;
        rock_cross(c.n, c.t1, c.t2);
        c.kn  = 1.0f / rock_k(d, c.r, c.n);
        c.kt1 = 1.0f / rock_k(d, c.r, c.t1);
        c.kt2 = 1.0f / rock_k(d, c.r, c.t2);
        c.jn = c.jt1 = c.jt2 = 0.0f;
        nc++;
    }
    for (int it = 0; it < 10 && nc; it++)
        for (int i = 0; i < nc; i++) {
            RockContact& c = C[i];
            float j = (c.target - rock_point_speed(d, c.r, c.n)) * c.kn;
            if (c.jn + j < 0.0f) j = -c.jn;
            c.jn += j;
            rock_impulse(d, c.r, c.n, j);
            /* friction: both tangents toward stopping the point, then held in the cone */
            const float o1 = c.jt1, o2 = c.jt2;
            float a1 = o1 - rock_point_speed(d, c.r, c.t1) * c.kt1;
            float a2 = o2 - rock_point_speed(d, c.r, c.t2) * c.kt2;
            const float lim = g_fx.rock_friction * c.jn;
            const float al = sqrtf(a1 * a1 + a2 * a2);
            if (al > lim) { a1 *= lim / al; a2 *= lim / al; }
            c.jt1 = a1; c.jt2 = a2;
            rock_impulse(d, c.r, c.t1, a1 - o1);
            rock_impulse(d, c.r, c.t2, a2 - o2);
        }
    for (int k = 0; k < 3; k++) d.p[k] += d.v[k] * dt;
    {   /* q += 0.5 * (0, w) * q * dt */
        const float w0 = d.w[0], w1 = d.w[1], w2 = d.w[2];
        const float q0 = d.q[0], q1 = d.q[1], q2 = d.q[2], q3 = d.q[3];
        d.q[0] += 0.5f * dt * (-w0 * q1 - w1 * q2 - w2 * q3);
        d.q[1] += 0.5f * dt * ( w0 * q0 + w1 * q3 - w2 * q2);
        d.q[2] += 0.5f * dt * ( w1 * q0 + w2 * q1 - w0 * q3);
        d.q[3] += 0.5f * dt * ( w2 * q0 + w0 * q2 - w1 * q1);
        const float l = sqrtf(d.q[0]*d.q[0] + d.q[1]*d.q[1] + d.q[2]*d.q[2] + d.q[3]*d.q[3]);
        for (int k = 0; k < 4; k++) d.q[k] /= l;
    }
    /* whatever still went into the ground comes straight back out */
    float lift = 0.0f;
    for (int i = 0; i < ns; i++) {
        float lp[3] = { rm.sup[i*3] * d.size, rm.sup[i*3+1] * d.size, rm.sup[i*3+2] * d.size };
        float r[3];
        q_rot(d.q, lp, r);
        const float wx = d.p[0] + r[0], wy = d.p[1] + r[1], wz = d.p[2] + r[2];
        const float gy = terrain_y(wx, wz) - g_fx.rock_sink * d.size;
        if (gy - wy > lift) lift = gy - wy;
    }
    d.p[1] += lift;
    const int contacts = nc;
    if (contacts) {
        /* ROLLING RESISTANCE: without it a round stone on flat ground rolls for ever,
           since the contact model has no loss while it rolls cleanly. Two parts. A torque
           against the spin in proportion to how hard the ground is pushing back (a
           rolling-friction coefficient of ROCK_ROLL), which stops a rolling stone in a
           second or so on level ground and lets it rest on a slope of up to about
           ROCK_ROLL in tangent, but not on anything steeper; and a light damping
           of both spin and ground speed. The damping alone took several seconds to bring
           a round stone under the sleep threshold, and let it creep for ever down any
           slope at a speed it could never get under it. */
        const float ROCK_ROLL = 0.2f;
        float jsum = 0.0f;
        for (int i = 0; i < nc; i++) jsum += C[i].jn;
        float load = jsum * d.invM / (g_fx.rock_gravity * dt);
        if (load > 1.5f) load = 1.5f;
        const float wl = sqrtf(d.w[0]*d.w[0] + d.w[1]*d.w[1] + d.w[2]*d.w[2]);
        const float dw = 2.5f * ROCK_ROLL * g_fx.rock_gravity * load * dt / (0.4f * d.size);
        const float kr = wl > dw ? 1.0f - dw / wl : 0.0f;
        for (int c = 0; c < 3; c++) d.w[c] *= kr;
        const float k = 1.0f - 1.2f * dt;
        for (int c = 0; c < 3; c++) { d.w[c] *= k; }
        d.v[0] *= k; d.v[2] *= k;
    }
    if (d.p[0] < 0.2f) { d.p[0] = 0.2f; d.v[0] = 0.0f; }
    if (d.p[2] < 0.2f) { d.p[2] = 0.2f; d.v[2] = 0.0f; }
    if (d.p[0] > (float)g_gridW - 0.2f) { d.p[0] = (float)g_gridW - 0.2f; d.v[0] = 0.0f; }
    if (d.p[2] > (float)g_gridH - 0.2f) { d.p[2] = (float)g_gridH - 0.2f; d.v[2] = 0.0f; }
    const float sv = d.v[0]*d.v[0] + d.v[1]*d.v[1] + d.v[2]*d.v[2];
    const float sw = d.w[0]*d.w[0] + d.w[1]*d.w[1] + d.w[2]*d.w[2];
    /* and nothing leaves the ground faster than a fast vehicle */
    const float hv = sqrtf(d.v[0] * d.v[0] + d.v[2] * d.v[2]);
    if (hv > 8.0f) { d.v[0] *= 8.0f / hv; d.v[2] *= 8.0f / hv; }
    if (contacts && sv < 0.05f * 0.05f && sw < 0.5f * 0.5f) d.still++;
    else d.still = 0;
    if (d.still >= 8) {
        d.sleep = true;
        for (int k = 0; k < 3; k++) d.v[k] = d.w[k] = 0.0f;
    }
}

/* The rocks a point's neighbourhood holds: still ones by their cells, moving ones all. */
static void rock_near(float x, float z, float reach, std::vector<int>& out)
{
    out.clear();
    const int x0 = (int)floorf(x - reach), x1 = (int)floorf(x + reach);
    const int z0 = (int)floorf(z - reach), z1 = (int)floorf(z + reach);
    for (int y = z0; y <= z1; y++)
        for (int xx = x0; xx <= x1; xx++) {
            if (xx < 0 || y < 0 || xx >= g_gridW || y >= g_gridH) continue;
            if (g_folCellIds.size() != (size_t)g_gridW * g_gridH) continue;
            const std::vector<int>& ids = g_folCellIds[(size_t)y * g_gridW + xx];
            for (size_t k = 0; k < ids.size(); k++)
                if (g_folKindOf[ids[k]] == 2 && g_rockDynOf[ids[k]] < 0) out.push_back(ids[k]);
        }
    for (size_t i = 0; i < g_rockDyn.size(); i++)
        if (!g_rockDyn[i].gone
            && fabsf(g_rockDyn[i].p[0] - x) <= reach + 1.0f
            && fabsf(g_rockDyn[i].p[2] - z) <= reach + 1.0f)
            out.push_back(g_rockDyn[i].id);
}

static void rock_pos(int id, float* x, float* z)
{
    if (g_rockDynOf[id] >= 0) { *x = g_rockDyn[g_rockDynOf[id]].p[0]; *z = g_rockDyn[g_rockDynOf[id]].p[2]; }
    else { *x = g_folRootX[id]; *z = g_folRootZ[id]; }
}

static void rock_tick(void)
{
    std::vector<int> cand;
    /* VEHICLES SHOVE. A vehicle that moved this tick and whose hull (the grass's own
       measurement of it, turned to its facing) reaches a rock sends it the way the
       vehicle is going, plus sideways off whichever flank touched it, at up to the
       vehicle's own speed times rock_push, scaled down for a big rock. A rock already
       going faster than that is left alone, so a tank pushing a stone along does not
       accelerate it without limit. Infantry do not shove. */
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_UNIT || o.limbo || water_is_boat(o)) continue;
        const int key = o.id;
        std::map<int, std::pair<float, float> >::iterator pv = g_rockVehPrev.find(key);
        float mx = 0.0f, mz = 0.0f;
        if (pv != g_rockVehPrev.end()) { mx = o.ex - pv->second.first; mz = o.ez - pv->second.second; }
        g_rockVehPrev[key] = std::make_pair(o.ex, o.ez);
        const float ml = sqrtf(mx * mx + mz * mz);
        if (ml < 1.0e-4f || ml > 1.0f) continue;
        const CrushHull hull = crush_hull_for_mesh(mesh_for(o));
        float hx = mx / ml, hz = mz / ml;
        if (o.face >= 0) {
            const float a = (float)o.face * (6.2831853f / 256.0f);
            hx = sinf(a); hz = -cosf(a);
        }
        const float halfL = 0.5f * hull.len, halfB = 0.5f * hull.beam;
        rock_near(o.ex, o.ez, halfL + 1.0f, cand);
        for (size_t k = 0; k < cand.size(); k++) {
            const int id = cand[k];
            float rx, rz;
            rock_pos(id, &rx, &rz);
            const float rr = 0.4f * g_rockIdSize[id];
            const float dx = rx - o.ex, dz = rz - o.ez;
            const float along = dx * hx + dz * hz, across = -dx * hz + dz * hx;
            if (fabsf(along) > halfL + rr || fabsf(across) > halfB + rr) continue;
            const int di = rock_wake(id);
            if (di < 0) continue;
            RockDyn& d = g_rockDyn[di];
            /* OUT FROM UNDER THE HULL FIRST, by the shorter way. A shove alone left a rock
               half under a vehicle that turned onto it. */
            {
                const float oa = halfL + rr - fabsf(along), ob = halfB + rr - fabsf(across);
                if (oa > 0.0f && ob > 0.0f) {
                    if (oa < ob) {
                        const float sgn = along >= 0.0f ? 1.0f : -1.0f;
                        d.p[0] += hx * oa * sgn; d.p[2] += hz * oa * sgn;
                    } else {
                        const float sgn = across >= 0.0f ? 1.0f : -1.0f;
                        d.p[0] += -hz * ob * sgn; d.p[2] += hx * ob * sgn;
                    }
                }
            }
            const float speed = ml * 15.0f;                    /* cells a second */
            const float want = g_fx.rock_push * speed * rock_kick_scale(d.size);
            const float ux = mx / ml, uz = mz / ml;
            const float side = across >= 0.0f ? 1.0f : -1.0f;
            float dirx = ux + 0.45f * side * -hz, dirz = uz + 0.45f * side * hx;
            const float dl = sqrtf(dirx * dirx + dirz * dirz);
            dirx /= dl; dirz /= dl;
            const float have = d.v[0] * dirx + d.v[2] * dirz;
            if (have < want) {
                d.v[0] += dirx * (want - have);
                d.v[2] += dirz * (want - have);
                d.v[1] += 0.15f * (want - have);
                /* and it starts to turn over the way it is pushed */
                d.w[0] += dirz * (want - have) / (0.4f * d.size);
                d.w[2] -= dirx * (want - have) / (0.4f * d.size);
                g_rockHits++;
            }
        }
    }
    /* A STRUCTURE GOING UP throws everything within rock_pulse_r cells of its footprint
       outward and up from its centre, hardest nearest, and lays the plants in that ring
       flat (the 3D grass keeps its own record of the same ground). */
    const float now = engine_time() * (1.0f / 15.0f);
    for (size_t pi = 0; pi < g_rockPulses.size(); pi++) {
        const RockPulse& P = g_rockPulses[pi];
        const float R = g_fx.rock_pulse_r;
        rock_near(P.cx, P.cz, R + (P.hw > P.hh ? P.hw : P.hh), cand);
        for (size_t k = 0; k < cand.size(); k++) {
            const int id = cand[k];
            float rx, rz;
            rock_pos(id, &rx, &rz);
            const float ex = fabsf(rx - P.cx) - P.hw, ez = fabsf(rz - P.cz) - P.hh;
            const float d = sqrtf((ex > 0 ? ex * ex : 0.0f) + (ez > 0 ? ez * ez : 0.0f));
            if (d > R) continue;
            const int di = rock_wake(id);
            if (di < 0) continue;
            RockDyn& b = g_rockDyn[di];
            float ox = rx - P.cx, oz = rz - P.cz;
            float ol = sqrtf(ox * ox + oz * oz);
            if (ol < 1.0e-4f) { ox = 1.0f; oz = 0.0f; ol = 1.0f; }
            ox /= ol; oz /= ol;
            const float k2 = g_fx.rock_pulse * (1.0f - d / (R > 0.01f ? R : 0.01f))
                             * rock_kick_scale(b.size);
            b.v[0] += ox * k2; b.v[2] += oz * k2; b.v[1] += 0.8f * k2;
            const unsigned h = fol_hash((unsigned)id, 77u, 5u);
            b.w[0] += ((float)(h & 255u) / 255.0f - 0.5f) * 4.0f * k2 / (0.4f * b.size + 0.05f);
            b.w[2] += ((float)((h >> 8) & 255u) / 255.0f - 0.5f) * 4.0f * k2 / (0.4f * b.size + 0.05f);
            g_rockPulsed++;
        }
        /* the bushes and clumps in the same ring go down */
        if (fol_plants_on() && g_folCellIds.size() == (size_t)g_gridW * g_gridH) {
            const int x0 = (int)floorf(P.cx - P.hw - R), x1 = (int)floorf(P.cx + P.hw + R);
            const int z0 = (int)floorf(P.cz - P.hh - R), z1 = (int)floorf(P.cz + P.hh + R);
            for (int y = z0; y <= z1; y++)
                for (int x = x0; x <= x1; x++) {
                    if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) continue;
                    const std::vector<int>& ids = g_folCellIds[(size_t)y * g_gridW + x];
                    for (size_t k = 0; k < ids.size(); k++) {
                        const int id = ids[k];
                        if (g_folKindOf[id] >= 2 || g_folCrushT[id] >= 0.0f) continue;
                        const float ex = fabsf(g_folRootX[id] - P.cx) - P.hw;
                        const float ez = fabsf(g_folRootZ[id] - P.cz) - P.hh;
                        const float d = sqrtf((ex > 0 ? ex * ex : 0.0f) + (ez > 0 ? ez * ez : 0.0f));
                        if (d > R) continue;
                        g_folCrushT[id] = now;
                        g_folCrushed.push_back(id);
                    }
                }
        }
    }
    g_rockPulses.clear();
    /* four substeps a tick */
    const float dt = (1.0f / 15.0f) / 4.0f;
    for (size_t i = 0; i < g_rockDyn.size(); i++) {
        RockDyn& d = g_rockDyn[i];
        for (int k = 0; k < 3; k++) d.pp[k] = d.p[k];
        for (int k = 0; k < 4; k++) d.pq[k] = d.q[k];
        if (d.gone || d.sleep) continue;
        for (int s2 = 0; s2 < 4 && !d.sleep; s2++) rock_step(d, dt);
    }
    /* a rock that comes to rest where a structure now stands is under it, and one
       that lands in water has sunk */
    for (size_t i = 0; i < g_rockDyn.size(); i++) {
        RockDyn& d = g_rockDyn[i];
        if (d.gone) continue;
        const int cx = (int)floorf(d.p[0]), cz = (int)floorf(d.p[2]);
        if (cx < 0 || cz < 0 || cx >= g_gridW || cz >= g_gridH) continue;
        const size_t k = (size_t)cz * g_gridW + cx;
        if (k < g_rockWater.size() && g_rockWater[k]) { d.gone = true; continue; }
        if (d.sleep && !g_folWhy.empty() && g_folWhy[k] == 4) d.gone = true;
    }
}

static int g_folNoteFrame = -1;
static void foliage3d_crush_note(void)
{
    if (!foliage3d_wanted() || g_folCellIds.empty()) return;
    if (g_engineFrame == g_folNoteFrame) return;     /* one record a tick */
    g_folNoteFrame = g_engineFrame;
    if (fol_plants_on() && g_fx.fol_crush)
        fol_crush_scan(engine_time() * (1.0f / 15.0f), false);
    if (fol_rocks_on() && g_fx.rock_physics) rock_tick();
    else g_rockPulses.clear();
}

/* The state texture, brought up to date once a drawn frame: each crushed plant's texel
   from its own clock. GL, so it runs from foliage3d_prepare and never from the tick. */
static void foliage3d_crush_step(void)
{
    const float now = engine_time() * (1.0f / 15.0f);
    const size_t n = g_folCrushT.size();
    if (!n) return;
    /* the texture, one texel a plant, grown a row of 256 at a time */
    const int wantH = (int)((n + FOL_CRUSH_W - 1) / FOL_CRUSH_W);
    const int h = wantH < 1 ? 1 : (wantH > FOL_CRUSH_MAXH ? FOL_CRUSH_MAXH : wantH);
    if (!g_folCrushTex || h > g_folCrushH) {
        const int nh = h < 4 ? 4 : h;
        g_folCrushPix.resize((size_t)FOL_CRUSH_W * nh * 4, 0);
        if (!g_folCrushTex) glGenTextures(1, &g_folCrushTex);
        glBindTexture(GL_TEXTURE_2D, g_folCrushTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FOL_CRUSH_W, nh, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, &g_folCrushPix[0]);
        glBindTexture(GL_TEXTURE_2D, 0);
        g_folCrushH = nh;
    }
    /* a rock that has become a body is drawn on its own: its static copy is gone */
    for (size_t i = 0; i < g_rockDyn.size(); i++) {
        const int id = g_rockDyn[i].id;
        if (id >= FOL_CRUSH_W * g_folCrushH) continue;
        unsigned char* px = &g_folCrushPix[(size_t)id * 4];
        if (px[1] != 255) { px[0] = 0; px[1] = 255; g_folCrushDirty = true; }
    }
    const float ft = g_fx.fol_flat_time > 0.01f ? g_fx.fol_flat_time : 0.01f;
    const float fd = g_fx.fol_fade_time > 0.01f ? g_fx.fol_fade_time : 0.01f;
    size_t w = 0;
    for (size_t k = 0; k < g_folCrushed.size(); k++) {
        const int id = g_folCrushed[k];
        if (id >= FOL_CRUSH_W * g_folCrushH) continue;
        const float age = now - g_folCrushT[id];
        float fl = age / ft;           fl = fl < 0.0f ? 0.0f : (fl > 1.0f ? 1.0f : fl);
        float fa = (age - ft) / fd;    fa = fa < 0.0f ? 0.0f : (fa > 1.0f ? 1.0f : fa);
        /* smooth at both ends, so a plant eases down and eases out */
        fl = fl * fl * (3.0f - 2.0f * fl);
        fa = fa * fa * (3.0f - 2.0f * fa);
        unsigned char* px = &g_folCrushPix[(size_t)id * 4];
        const unsigned char R = (unsigned char)(fl * 255.0f + 0.5f);
        const unsigned char G = (unsigned char)(fa * 255.0f + 0.5f);
        if (px[0] != R || px[1] != G) { px[0] = R; px[1] = G; g_folCrushDirty = true; }
        if (G < 255) g_folCrushed[w++] = id;        /* gone for good: stop tracking it */
    }
    g_folCrushed.resize(w);
    if (g_folCrushDirty && g_folCrushTex) {
        glBindTexture(GL_TEXTURE_2D, g_folCrushTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FOL_CRUSH_W, g_folCrushH, GL_RGBA,
                        GL_UNSIGNED_BYTE, &g_folCrushPix[0]);
        glBindTexture(GL_TEXTURE_2D, 0);
        g_folCrushDirty = false;
    }
}

/* ---- A BUILDING REBUILDS THE CHUNKS AROUND IT, NOT THE MAP ----
   The pad epoch in a chunk's signature used to rebuild every chunk on every placement,
   sale or loss on the map. It stood for three things the signature does not hash: the
   ground under the plants beyond the chunk's corners (the relief lift and the water
   plane), the desert details (filtered by the shroud and blocked by buildings), and the
   rocks a vehicle has woken, whose static copies a rebuild leaves out. With
   OPENCNC3D_GROUND_PATCH the epoch leaves the signature; on a pad change a chunk is
   rebuilt when the ground revision grid stamped a cell of it, the water plane moved
   since it was built, or the history below changed, or anything moved the ground
   between pad changes (the grid then says whole).
   Otherwise its rebuild would lay the same plants with the same ids, so it is kept.
   Chunks still rebuild in the same walk order, so new ids go out in the same order. */
static void fol_chunk_history(int cx, int cy, std::vector<unsigned>& out)
{
    out.clear();
    for (const auto& c : g_desertDetailCells) {
        if (c.x / FOL_CHUNK != cx || c.z / FOL_CHUNK != cy) continue;
        out.push_back((unsigned)c.x); out.push_back((unsigned)c.z); out.push_back(c.key);
        auto it = g_desertDetails.find(c.key);
        if (it == g_desertDetails.end()) continue;
        for (const auto& d : it->second) {
            const int model = g_folBush[d.model];
            out.push_back((unsigned)model);
            if (model < 0) continue;
            const float x = c.x + d.x, z = c.z + d.z;
            bool blocked = false;
            for (const auto& o : g_objects)
                if (o.kind == K_BUILDING && !o.limbo && fabsf(x - o.ex) < o.fw * .5f && fabsf(z - o.ez) < o.fh * .5f) { blocked = true; break; }
            out.push_back(blocked ? 1u : 0u);
            const int gx = (int)floorf(x), gz = (int)floorf(z);
            if (gx >= 0 && gz >= 0 && gx < g_gridW && gz < g_gridH && (gx / FOL_CHUNK != cx || gz / FOL_CHUNK != cy))
                g_folSpill = true;
        }
    }
    out.push_back(0xFFFFFFFFu);
    if (g_folCellIds.size() != (size_t)g_gridW * g_gridH) return;
    for (int y = cy * FOL_CHUNK; y < cy * FOL_CHUNK + FOL_CHUNK && y < g_gridH; y++)
        for (int x = cx * FOL_CHUNK; x < cx * FOL_CHUNK + FOL_CHUNK && x < g_gridW; x++)
            for (int id : g_folCellIds[(size_t)y * g_gridW + x])
                if (id >= 0 && (size_t)id < g_rockDynOf.size() && g_folKindOf[id] == 2 && g_rockDynOf[id] >= 0)
                    out.push_back((unsigned)id);
}
/* OPENCNC3D_GROUND_PATCH_VERIFY: a chunk the patch keeps is rebuilt anyway, and its
   batches, its cells' plant ids and the id count must come back byte for byte. */
static bool fol_chunk_verify(int cx, int cy)
{
    FolChunk& ch = g_folChunks[(size_t)cy * g_folCW + cx];
    struct Copy { int model, count, nidx; std::vector<unsigned char> v, i; };
    const auto grab = [&](std::vector<Copy>& out) {
        out.clear();
        for (const FolBatch& fb : ch.batch) {
            Copy c; c.model = fb.model; c.count = fb.count; c.nidx = fb.nidx;
            GLint vb = 0, ib = 0;
            gr_glBindBuffer(GL_ARRAY_BUFFER, fb.vbo); gr_glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vb);
            gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fb.ibo); gr_glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &ib);
            gr_glBindBuffer(GL_ARRAY_BUFFER, 0); gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            c.v.resize(vb > 0 ? (size_t)vb : 0); c.i.resize(ib > 0 ? (size_t)ib : 0);
            ground_read_buffer(GL_ARRAY_BUFFER, fb.vbo, c.v.size(), c.v.data());
            ground_read_buffer(GL_ELEMENT_ARRAY_BUFFER, fb.ibo, c.i.size(), c.i.data());
            out.push_back(c);
        }
    };
    std::vector<Copy> before, after;
    grab(before);
    std::vector<std::vector<int> > idsBefore;
    for (int y = cy * FOL_CHUNK; y < cy * FOL_CHUNK + FOL_CHUNK && y < g_gridH; y++)
        for (int x = cx * FOL_CHUNK; x < cx * FOL_CHUNK + FOL_CHUNK && x < g_gridW; x++)
            idsBefore.push_back(g_folCellIds[(size_t)y * g_gridW + x]);
    const size_t nid = g_folCrushT.size();
    const int nb = ch.nbush, nc = ch.nclump, nr = ch.nrock;
    foliage3d_build_chunk(cx, cy);
    grab(after);
    bool same = before.size() == after.size() && nid == g_folCrushT.size() && nb == ch.nbush && nc == ch.nclump && nr == ch.nrock;
    for (size_t b = 0; same && b < before.size(); b++)
        same = before[b].model == after[b].model && before[b].count == after[b].count && before[b].nidx == after[b].nidx
            && before[b].v == after[b].v && before[b].i == after[b].i;
    size_t k = 0;
    for (int y = cy * FOL_CHUNK; same && y < cy * FOL_CHUNK + FOL_CHUNK && y < g_gridH; y++)
        for (int x = cx * FOL_CHUNK; same && x < cx * FOL_CHUNK + FOL_CHUNK && x < g_gridW; x++)
            same = idsBefore[k++] == g_folCellIds[(size_t)y * g_gridW + x];
    return same;
}

/* Once per frame, before the first pass that draws: bring every chunk up to date.
   g_folFrame is stepped where the world pass zeroes the trees' own per-frame counters,
   which is before the sun map draws its casters (g_folFrame and g_folPrepFrame are
   declared with the other globals, above). */
static void foliage3d_prepare(int frame)
{
    if (frame == g_folPrepFrame) return;
    g_folPrepFrame = frame;
    const int cw = (g_gridW + FOL_CHUNK - 1) / FOL_CHUNK;
    const int chh = (g_gridH + FOL_CHUNK - 1) / FOL_CHUNK;
    if (cw != g_folCW || chh != g_folCH || g_gridW != g_folGW || g_gridH != g_folGH) {
        foliage3d_free_chunks();
        g_folCW = cw; g_folCH = chh; g_folGW = g_gridW; g_folGH = g_gridH;
        g_folChunks.assign((size_t)cw * chh, FolChunk());
        for (size_t i = 0; i < g_folChunks.size(); i++) {
            g_folChunks[i].sig = 0; g_folChunks[i].built = false;
            g_folChunks[i].nbush = g_folChunks[i].nclump = g_folChunks[i].nrock = 0;
        }
        g_folChunkInst.assign((size_t)cw * chh, std::vector<FolInst>());
    }
    foliage3d_mark();
    desert_details_prepare();
    /* WHICH LEVEL OF DETAIL, from the dial: 0 the pack's LOD2, 2 its LOD0. A level the
       pack does not carry falls to the nearest one it does. */
    {
        const int want = 2 - (int)(g_fx.fol_bush_detail + 0.5f);
        for (int b = 0; b < 2; b++) {
            g_folBush[b] = -1;
            for (int d = 0; d < 3 && g_folBush[b] < 0; d++) {
                const int l1 = want - d, l2 = want + d;
                if (l1 >= 0 && l1 < 3 && g_folBushLod[b][l1] >= 0) g_folBush[b] = g_folBushLod[b][l1];
                else if (l2 >= 0 && l2 < 3 && g_folBushLod[b][l2] >= 0) g_folBush[b] = g_folBushLod[b][l2];
            }
        }
    }
    const unsigned layout = fol_layout_sig();
    int rebuilt = 0;
    const Uint32 t0 = SDL_GetTicks();
    /* A SLIDER BEING DRAGGED REBUILDS THE WHOLE MAP EVERY FRAME, which measured 77 ms at
       the default amounts on the largest temperate mission and 300 at the heavy end, so
       the panel would drop to a handful of frames a second under the mouse. While the
       panel is open the rebuild takes a budget of 12 ms a frame and the rest of the map
       catches up over the next frames; a chunk not yet rebuilt keeps its old plants,
       which is what "catching up" looks like. With the panel shut (a game, or a script
       with its own gfx lines) every stale chunk is rebuilt before the frame draws, so a
       scripted shot never photographs half a change. */
    /* THE WALK STARTS WHERE THE LAST ONE STOPPED, or a slider held in motion would
       rebuild the first chunks every frame and never reach the far side of the map. */
    const bool budget = fxp_is_open();
    static int s_next = 0;
    const int nch = g_folCW * g_folCH;
    static const bool patchOn = ground_patch_on();
    const bool revNow = patchOn && ground_rev_current();
    bool padSeen = false;
    int kept = 0, verified = 0, verifyBad = 0;
    static std::vector<unsigned> history;
    for (int k = 0; k < nch; k++) {
        const int ci = (s_next + k) % nch;
        const int cx = ci % g_folCW, cy = ci / g_folCW;
        FolChunk& ch = g_folChunks[(size_t)ci];
        const unsigned sig = fol_chunk_sig(cx, cy, layout, !patchOn);
        if (ch.built && ch.sig == sig) {
            if (!patchOn || ch.pad == g_padEpoch) continue;
            padSeen = true;
            const int x0 = cx * FOL_CHUNK, z0 = cy * FOL_CHUNK;
            if (revNow && !g_folSpill && ch.plane == water_plane_epoch()
                && !ground_rev_moved(ch.rev, x0 - 1, z0 - 1, x0 + FOL_CHUNK, z0 + FOL_CHUNK)) {
                fol_chunk_history(cx, cy, history);
                if (!g_folSpill && history == ch.history) {
                    ch.pad = g_padEpoch; ch.rev = g_groundSeq; kept++;
                    if (ground_patch_verify()) { verified++; if (!fol_chunk_verify(cx, cy)) verifyBad++; }
                    continue;
                }
            }
        }
        if (budget && rebuilt > 0 && SDL_GetTicks() - t0 > 12u) { s_next = ci; break; }
        ch.sig = sig;
        foliage3d_build_chunk(cx, cy);
        if (patchOn) {
            ch.pad = g_padEpoch; ch.rev = revNow ? g_groundSeq : 0; ch.plane = water_plane_epoch();
            fol_chunk_history(cx, cy, ch.history);
        }
        rebuilt++;
    }
    if (verified)
        fprintf(stderr, "GROUNDPATCH|verify|foliage|%s|kept=%d|bad=%d\n", verifyBad ? "MISMATCH" : "ok", verified, verifyBad);
    else if (kept)
        fprintf(stderr, "GROUNDPATCH|foliage|kept=%d|rebuilt=%d\n", kept, rebuilt);
    /* A pad change rebuilt every chunk before, and so always ran the instant scan. */
    if ((rebuilt || padSeen) && g_fx.fol_crush)
        fol_crush_scan(engine_time() * (1.0f / 15.0f), true);
    foliage3d_crush_step();
    if (rebuilt) {
        int nb = 0, nc = 0, nok = 0;
        for (size_t i = 0; i < g_folChunks.size(); i++) {
            nb += g_folChunks[i].nbush; nc += g_folChunks[i].nclump;
        }
        for (size_t i = 0; i < g_folOk.size(); i++) nok += g_folOk[i];
        fprintf(stderr, "FOLIAGE|built|chunks=%d/%d|bushes=%d|clumps=%d|cells=%d|ms=%u\n",
                rebuilt, (int)g_folChunks.size(), nb, nc, nok,
                (unsigned)(SDL_GetTicks() - t0));
    }
}

/* ---- Drawing -------------------------------------------------------------------- */
enum { FOL_LIT = 0, FOL_CAST = 1, FOL_NRM = 2, FOL_LIGHT_SPLIT = 3 };

static bool fol_chunk_in(int cx, int cy, float pad, float x0, float x1, float z0, float z1)
{
    const float ax = (float)(cx * FOL_CHUNK), az = (float)(cy * FOL_CHUNK);
    return !(ax + FOL_CHUNK < x0 - pad || ax > x1 + pad ||
             az + FOL_CHUNK < z0 - pad || az > z1 + pad);
}

/* THE SUN'S RECEIVER WINDOW (fx_post.h): a chunk whose plants, as far as the vertex
   stage can ever carry them, all fall outside every texel the light pass reads casts
   nothing that is read, and is left out of the sun's map like a chunk outside its box.
   The reach is TREE_VS's own with the placement this file gives it (identity origin,
   scale and yaw, no rig, no lean): the wind and the flutter sideways, each at most its
   amplitude since the sway weight and the material are at most one; the flutter's 0.35
   up and down; and a crushed plant swung out from its root by at most 0.92 of its height
   above that root. */
static bool fol_recv_outside(const FolChunk& ch)
{
    if (!g_fxRecvArmed || !g_fxRecvArmed->on || !ch.boxed) return false;
    const float flut = fabsf(g_fx.fol_flutter) * FOL_HREF;
    const float up = 0.35f * flut + 0.01f;
    const float splay = 0.92f * (ch.hi[1] + up - ch.rootLo);
    const float side = fabsf(g_fx.fol_wind) * FOL_HREF + flut + (splay > 0.0f ? splay : 0.0f) + 0.01f;
    const float lo[3] = { ch.lo[0] - side, (ch.lo[1] < ch.rootLo ? ch.lo[1] : ch.rootLo) - up,
                          ch.lo[2] - side };
    const float hi[3] = { ch.hi[0] + side, ch.hi[1] + up, ch.hi[2] + side };
    g_fxRecvChunkTested++;
    if (!fx_recv_box_outside(lo, hi, gls_clip())) return false;
    g_fxRecvChunkCulled++;
    return true;
}
/* A moving rock under the same window: a tumbled stone is its model turned and scaled
   about its position, with no wind, flutter or crush, so the ball of its farthest
   vertex holds it. */
static bool rock_recv_outside(const Tree3dModel& M, const float p[3], float size)
{
    if (!g_fxRecvArmed || !g_fxRecvArmed->on) return false;
    float r2 = 0.0f;
    for (size_t i = 0; i + 2 < M.pos.size(); i += 3) {
        const float d = M.pos[i] * M.pos[i] + M.pos[i + 1] * M.pos[i + 1] + M.pos[i + 2] * M.pos[i + 2];
        if (d > r2) r2 = d;
    }
    const float r = sqrtf(r2) * fabsf(size) + 0.01f;
    const float lo[3] = { p[0] - r, p[1] - r, p[2] - r }, hi[3] = { p[0] + r, p[1] + r, p[2] + r };
    g_fxRecvChunkTested++;
    if (!fx_recv_box_outside(lo, hi, gls_clip())) return false;
    g_fxRecvChunkCulled++;
    return true;
}

static void fol_bind_batch(const FolBatch& fb)
{
    gr_glBindBuffer(GL_ARRAY_BUFFER, fb.vbo);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fb.ibo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    const GLsizei st = (GLsizei)sizeof(FolVert);
    glVertexPointer(3, GL_FLOAT, st, (const void*)offsetof(FolVert, p));
    glNormalPointer(GL_BYTE, st, (const void*)offsetof(FolVert, n));
    glTexCoordPointer(2, GL_FLOAT, st, (const void*)offsetof(FolVert, uv));
    glColorPointer(4, GL_UNSIGNED_BYTE, st, (const void*)offsetof(FolVert, c));
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, st, (const void*)offsetof(FolVert, t1));
    glClientActiveTexture(GL_TEXTURE0);
}

static void fol_unbind(void)
{
    glClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    gr_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gr_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* The placement uniforms at identity: every vertex is already where it stands. These
   are the same for every batch, so they are set once per call, after the program is
   bound. Never once per frame: the trees draw with the same programs between one
   foliage call and the next and overwrite every one of them. */
static void fol_place_constants(GLuint prog)
{
    tfx_set3f(prog, TFX_uOrigin, 0.0f, 0.0f, 0.0f);
    tfx_set1f(prog, TFX_uScale, 1.0f);
    tfx_set1f(prog, TFX_uYawC, 1.0f);
    tfx_set1f(prog, TFX_uYawS, 0.0f);
    tfx_set3f(prog, TFX_uLean, 0.0f, 0.0f, 0.0f);
    tfx_set1f(prog, TFX_uFlutter, g_fx.fol_flutter * FOL_HREF);
    tfx_set1f(prog, TFX_uTime, engine_time() * (1.0f / 15.0f));
    tfx_set1f(prog, TFX_uAnim, 0.0f);
    tfx_set1f(prog, TFX_uRigWind, 1.0f);
}

/* And the two that are not: the wind is per chunk and per model, on a phase from both. */
static void fol_place(GLuint prog, int cx, int cy, int model)
{
    const float t = engine_time() * (1.0f / 15.0f);
    const float phase = (float)(fol_hash((unsigned)cx, (unsigned)cy, (unsigned)model + 5u)
                                & 1023u) * (6.2831853f / 1024.0f);
    const float amp = g_fx.fol_wind * FOL_HREF
                      * sinf(6.2831853f * g_fx.tree3d_wind_speed * t + phase);
    const float wdir = g_fx.tree3d_wind_dir * 0.01745329f;
    tfx_set3f(prog, TFX_uWind, cosf(wdir) * amp, 0.0f, sinf(wdir) * amp);
    tfx_set1f(prog, TFX_uPhase, phase);
}

/* purpose FOL_LIT: the cutout pass, framed by the trees' own view box.
   purpose FOL_CAST: the sun map or the water's reflection; the caller passes the box
   that pass draws into (boxOn 0 means the whole map).
   purpose FOL_NRM: the ground-normal buffer, as the trees write theirs. */
static void foliage3d_draw(int purpose, int boxOn, float bx0, float bx1, float bz0, float bz1)
{
    if (purpose == FOL_LIT) { g_folDrawInst = g_folDrawTris = g_folDrawBatches = 0; }
    if (purpose == FOL_CAST) g_folCastBatches = 0;
    if (purpose == FOL_NRM)  g_folNrmBatches = 0;
    if (!foliage3d_available())
        return;
    const bool sunCast = (purpose == FOL_CAST && g_fxCasterPass == FXCAST_SUN);
    if (sunCast && !g_fx.fol_shadows && !g_fx.fol_grass_shadows && !g_fx.rock_shadows)
        return;
    foliage3d_prepare(g_folFrame);

    const GLuint prog = (purpose == FOL_NRM) ? tree3d_nrm_prog() : tree3d_prog();
    if (!prog)
        return;
    /* the tallest plant, for the view box's pad */
    const float pad = 1.0f + FOL_HREF;

    fx_glUseProgram(prog);
    tfx_set1i(prog, TFX_uLightSplit, purpose==FOL_LIGHT_SPLIT?1:0);
    /* this call leaves its own values in the trees' program, so the next tree drawn
       sets the pass's constants again rather than trusting them */
    tree3d_pass_reset();
    if (purpose == FOL_NRM) {
        tfx_set1i(prog, TFX_uAtlas, 0);tfx_set1i(prog, TFX_uAtlasN, 1);tfx_set1i(prog, TFX_uBarkN, 3);
        tfx_set3f(prog, TFX_uCam, g_fxCamPos[0], g_fxCamPos[1], g_fxCamPos[2]);
        tfx_set1f(prog, TFX_uNormAlpha, 0.5f);
        tfx_set1f(prog, TFX_uCut, 0.34f);
        glEnable(GL_TEXTURE_2D);
    } else {
        fx_sun_dir();
        tfx_set1i(prog, TFX_uAtlas, 0);  tfx_set1i(prog, TFX_uAtlasN, 1);
        tfx_set1i(prog, TFX_uBark,  2);  tfx_set1i(prog, TFX_uBarkN,  3);
        tfx_set3f(prog, TFX_uSun, -g_fxSunDir[0], -g_fxSunDir[1], -g_fxSunDir[2]);
        tfx_set3f(prog, TFX_uCam, g_fxCamPos[0], g_fxCamPos[1], g_fxCamPos[2]);
        tfx_set3f(prog, TFX_uSunCol, 1.00f*fx_sun_energy(), 0.97f*fx_sun_energy(), 0.90f*fx_sun_energy());
        tfx_set3f(prog, TFX_uSkyCol, 0.52f*fx_sky_energy(), 0.62f*fx_sky_energy(), 0.78f*fx_sky_energy());
        tfx_set3f(prog, TFX_uGndCol, 0.30f*fx_sky_energy(), 0.31f*fx_sky_energy(), 0.22f*fx_sky_energy());
        tfx_set1f(prog, TFX_uCut, 0.34f);
        tfx_set1f(prog, TFX_uBurn, 0.0f);
        tfx_set1f(prog, TFX_uAOAmt, g_fx.tree3d_ao);
        tfx_set1f(prog, TFX_uWrap, g_fx.tree3d_wrap);
        tfx_set1f(prog, TFX_uAoFloor, g_fx.tree3d_ao_floor);
        tfx_set1f(prog, TFX_uObjectShadowFill, g_fx.object_shadow_fill);
        tfx_set1f(prog, TFX_uAoGamma, g_fx.tree3d_ao_gamma);
        tfx_set1f(prog, TFX_uAoDirect, g_fx.tree3d_ao_direct);
        /* uGain is set per batch below: a bush and a clump have separate exposures */
        tfx_set1f(prog, TFX_uTrans, g_fx.tree3d_trans);
        tfx_set1f(prog, TFX_uSheen, g_fx.tree3d_sheen);
        /* the ground-shade share a tree takes from its own cell, at its mean */
        tfx_set1f(prog, TFX_uAmb, g_fx.tree3d_ambient * 0.85f);
        tfx_set1f(prog, TFX_uShroud, 1.0f);
    }

    fol_place_constants(prog);
    tfx_set1f(prog, TFX_uFol, 1.0f);
    tfx_set1i(prog, TFX_uCrush, 4);
    tfx_set2f(prog, TFX_uCrushSize, (float)FOL_CRUSH_W, (float)(g_folCrushH > 0 ? g_folCrushH : 1));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, g_folCrushTex);
    glActiveTexture(GL_TEXTURE0);

    int lastSet = -1;
    for (int cy = 0; cy < g_folCH; cy++)
        for (int cx = 0; cx < g_folCW; cx++) {
            const FolChunk& ch = g_folChunks[(size_t)cy * g_folCW + cx];
            if (ch.batch.empty()) continue;
            if (purpose == FOL_CAST) {
                if (boxOn && !fol_chunk_in(cx, cy, pad, bx0, bx1, bz0, bz1)) continue;
                if (sunCast && fol_recv_outside(ch)) continue;
            } else if (g_cullOn && g_clip_to_map) {
                if ((purpose == FOL_LIT || purpose == FOL_LIGHT_SPLIT) && g_treeCullValid &&
                    !fol_chunk_in(cx, cy, pad, g_treeCullX0, g_treeCullX1,
                                  g_treeCullZ0, g_treeCullZ1)) continue;
                if (purpose == FOL_NRM && g_cullValid &&
                    !fol_chunk_in(cx, cy, pad, g_cullX0, g_cullX1, g_cullZ0, g_cullZ1))
                    continue;
            }
            for (size_t b = 0; b < ch.batch.size(); b++) {
                const FolBatch& fb = ch.batch[b];
                const Tree3dModel& M = g_folModel[fb.model];
                bool bush = false;
                for (int bi = 0; bi < 2; bi++)
                    for (int l = 0; l < 3; l++) bush = bush || fb.model == g_folBushLod[bi][l];
                const bool rock = fb.model < (int)g_folModelKind.size()
                                  && g_folModelKind[fb.model] >= 2;
                const bool stone=rock && rock_cliff_material();
                if(purpose==FOL_NRM){tfx_set1f(prog,TFX_uNormAlpha,stone?0.875f:0.5f);tfx_set1f(prog,TFX_uStone,rock?1.0f:0.0f);}
                else tfx_set1f(prog,TFX_uStone,stone?1.0f:0.0f);
                if (sunCast && !(rock ? g_fx.rock_shadows
                                 : (bush ? g_fx.fol_shadows : g_fx.fol_grass_shadows))) continue;
                if (purpose != FOL_NRM)
                    tfx_set1f(prog, TFX_uGain, rock ? rock_gain_now()
                                            : (bush ? (desert_detail_state()?g_fx.tree3d_gain*(g_fx.fol_gain/.10f):g_fx.fol_gain) : g_fx.fol_grass_gain));
                if (M.set < 0 || (size_t)M.set >= g_folSet.size()) continue;
                if (M.set != lastSet) {
                    const Tree3dSet& ts = g_folSet[M.set];
                    for (int u = 3; u >= 0; u--) {
                        glActiveTexture(GL_TEXTURE0 + u);glBindTexture(GL_TEXTURE_2D, ts.tex[u]);
                    }
                    lastSet = M.set;
                }
                fol_place(prog, cx, cy, fb.model);
                fol_bind_batch(fb);
                glDrawElements(GL_TRIANGLES, (GLsizei)fb.nidx, GL_UNSIGNED_INT, (const void*)0);
                if (purpose == FOL_LIT) {
                    g_folDrawInst += fb.count;
                    g_folDrawTris += fb.nidx / 3;
                    g_folDrawBatches++;
                } else if (purpose == FOL_CAST) g_folCastBatches++;
                else g_folNrmBatches++;
            }
        }
    fol_unbind();
    /* THE ROCKS THAT HAVE MOVED, one draw each, at the pose between their last two ticks.
       Client arrays from the model itself (trees' own helpers), so no buffer per rock. */
    if (!g_rockDyn.empty() && !(sunCast && !g_fx.rock_shadows)) {
        tfx_set1f(prog, TFX_uFol, 0.0f);
        tfx_set1f(prog, TFX_uRock, 1.0f);
        if(purpose==FOL_NRM){tfx_set1f(prog,TFX_uNormAlpha,rock_cliff_material()?0.875f:0.5f);tfx_set1f(prog,TFX_uStone,1.0f);}
        else tfx_set1f(prog,TFX_uStone,rock_cliff_material()?1.0f:0.0f);
        tfx_set3f(prog, TFX_uWind, 0.0f, 0.0f, 0.0f);
        tfx_set3f(prog, TFX_uLean, 0.0f, 0.0f, 0.0f);
        tfx_set1f(prog, TFX_uFlutter, 0.0f);
        tfx_set1f(prog, TFX_uAnim, 0.0f);
        tfx_set1f(prog, TFX_uRigWind, 1.0f);
        tfx_set1f(prog, TFX_uYawC, 1.0f);
        tfx_set1f(prog, TFX_uYawS, 0.0f);
        if (purpose != FOL_NRM) tfx_set1f(prog, TFX_uGain, rock_gain_now());
        const float a = g_tickAlpha < 0.0f ? 0.0f : (g_tickAlpha > 1.0f ? 1.0f : g_tickAlpha);
        for (size_t i = 0; i < g_rockDyn.size(); i++) {
            const RockDyn& d = g_rockDyn[i];
            if (d.gone) continue;
            float p[3], q[4];
            for (int k = 0; k < 3; k++) p[k] = d.pp[k] + (d.p[k] - d.pp[k]) * a;
            float dot = 0.0f;
            for (int k = 0; k < 4; k++) dot += d.pq[k] * d.q[k];
            const float sgn = dot < 0.0f ? -1.0f : 1.0f;
            float ql = 0.0f;
            for (int k = 0; k < 4; k++) { q[k] = d.pq[k] + (sgn * d.q[k] - d.pq[k]) * a; ql += q[k] * q[k]; }
            ql = sqrtf(ql);
            for (int k = 0; k < 4; k++) q[k] /= ql;
            const int cxl = (int)floorf(p[0]), czl = (int)floorf(p[2]);
            if (purpose == FOL_CAST) {
                if (boxOn && !cell_in_box(cxl, czl, 1.0f, bx0, bx1, bz0, bz1)) continue;
            } else if (!tree3d_cell_in_view(cxl, czl)) continue;
            const Tree3dModel& M = g_folModel[g_rockModels[d.rm].model];
            if (sunCast && rock_recv_outside(M, p, d.size)) continue;
            if (M.set < 0 || (size_t)M.set >= g_folSet.size()) continue;
            /* as the tree callers do: a model with nothing to draw is skipped rather than
               handed to the element draw, whose array path indexes its first element */
            if (M.pos.empty() || M.idx.empty()) continue;
            const Tree3dSet& ts = g_folSet[M.set];
            for (int u = 3; u >= 0; u--) {
                glActiveTexture(GL_TEXTURE0 + u);glBindTexture(GL_TEXTURE_2D, ts.tex[u]);
            }
            float m9[9];
            q_mat3(q, m9);
            const float rows[12] = { m9[0], m9[3], m9[6], 0.0f,
                                     m9[1], m9[4], m9[7], 0.0f,
                                     m9[2], m9[5], m9[8], 0.0f };
            tfx_set4fv(prog, TFX_uRot, 3, rows);
            tfx_set3f(prog, TFX_uOrigin, p[0], p[1], p[2]);
            tfx_set1f(prog, TFX_uScale, d.size);
            tree3d_arrays_on(M);
            tree3d_draw_elements(M);
            tree3d_arrays_off();
        }
        tfx_set1f(prog, TFX_uRock, 0.0f);
    }
    /* THE PROGRAM IS THE TREES' TOO: hand it back with the foliage branch shut. */
    tfx_set1f(prog, TFX_uFol, 0.0f);
    tfx_set1f(prog, TFX_uStone, 0.0f);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    fx_glUseProgram(0);
    for (int u = 3; u >= 1; u--) {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (purpose == FOL_NRM) glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* What the scatter holds and did on the last frame, for a gate: the summary line, then
   one line per plant with the cell it stands in, that cell's atlas slot, and why the
   renderer thinks it is plain grass. A plant on a slot of 16 or more, or on a cell the
   engine reports tiberium, a wall or a building in, is the bug this module exists to
   not have, and the dump is written so a reader can find one without trusting this file. */
static void foliage3d_dump(int perPlant)
{
    int nb = 0, nc = 0, nok = 0;
    for (size_t i = 0; i < g_folChunks.size(); i++) {
        nb += g_folChunks[i].nbush; nc += g_folChunks[i].nclump;
    }
    for (size_t i = 0; i < g_folOk.size(); i++) nok += g_folOk[i];
    int ncr = 0;
    for (size_t i = 0; i < g_folCrushT.size(); i++) if (g_folCrushT[i] >= 0.0f) ncr++;
    int nrk = 0, nawake = 0;
    for (size_t i = 0; i < g_folChunks.size(); i++) nrk += g_folChunks[i].nrock;
    for (size_t i = 0; i < g_rockDyn.size(); i++) if (!g_rockDyn[i].sleep && !g_rockDyn[i].gone) nawake++;
    printf("FOLDUMP|on=%d|have=%d|theater=%s|bushes=%d|clumps=%d|cells=%d|built=%d"
           "|drawn=%d|tris=%d|batches=%d|cast=%d|nrm=%d|ids=%d|crushed=%d|fading=%d"
           "|rocks=%d|faces=%d|moving=%d|awake=%d|hits=%d|pulsed=%d\n",
           foliage3d_wanted() ? 1 : 0, g_folHave ? 1 : 0, g_pack.theater, nb, nc, nok,
           g_folBuilt, g_folDrawInst, g_folDrawTris, g_folDrawBatches,
           g_folCastBatches, g_folNrmBatches, (int)g_folCrushT.size(), ncr,
           (int)g_folCrushed.size(), nrk, g_rockFaceCells, (int)g_rockDyn.size(), nawake,
           g_rockHits, g_rockPulsed);
    /* why the rest were refused, in the order foliage3d_mark tests them */
    int why[11] = { 0 };
    for (size_t i = 0; i < g_folWhy.size(); i++) if (g_folWhy[i] < 11) why[g_folWhy[i]]++;
    printf("FOLWHY|ok=%d|notclear=%d|notshown=%d|shroud=%d|building=%d|terrain=%d|tib=%d"
           "|wall=%d|crate=%d|smudge=%d|field=%d\n", why[0], why[1], why[2], why[3], why[4],
           why[5], why[6], why[7], why[8], why[9], why[10]);
    if (!perPlant) return;
    for (size_t i = 0; i < g_rockDyn.size(); i++) {
        const RockDyn& d = g_rockDyn[i];
        float gn[3];
        rock_ground_normal(d.p[0], d.p[2], gn);
        printf("ROCKDYN|%d|size=%.3f|at=%.3f,%.3f,%.3f|v=%.3f,%.3f,%.3f|sleep=%d|gone=%d"
               "|slope=%.1f\n",
               d.id, d.size, d.p[0], d.p[1], d.p[2], d.v[0], d.v[1], d.v[2],
               d.sleep ? 1 : 0, d.gone ? 1 : 0, acosf(gn[1]) * 57.29578f);
    }
    for (int y = 0; y < g_gridH; y++)
        for (int x = 0; x < g_gridW; x++) {
            const size_t k = (size_t)y * g_gridW + x;
            if (k < g_folWhy.size() && g_folWhy[k] != 2)
                printf("FOLCELL|%d|%d|%d|%d|face=%.2f\n", x, y, g_folWhy[k], g_folSlot[k],
                       k < g_rockFace.size() ? g_rockFace[k] : -1.0f);
        }
    for (size_t c = 0; c < g_folChunkInst.size(); c++)
        for (size_t i = 0; i < g_folChunkInst[c].size(); i++) {
            const FolInst& fi = g_folChunkInst[c][i];
            const size_t k = (size_t)fi.cy * g_gridW + fi.cx;
            printf("FOLINST|%d|%d|%d|%.3f|%.3f|%.3f|slot=%d|ok=%d\n", fi.model, fi.cx, fi.cy,
                   fi.x, fi.z, fi.h, k < g_folSlot.size() ? g_folSlot[k] : -1,
                   k < g_folOk.size() ? g_folOk[k] : 0);
        }
}

#endif /* FOLIAGE3D_MOD_H */
