/*
 * mottle_mod.h -- patches of ground variation laid over plain terrain.
 *
 * WHAT A MOTTLE IS. One blended quad on a cell's own four terrain corners: the same four
 * heights draw_terrain used, so it lies exactly in the ground rather than above it, and
 * depth writes stay off so it never hides anything. It is ground, not an overlay.
 *
 * WHY IT IS NOT CALLED A DECAL. That word already has a meaning in this renderer: the
 * cartridge's own scorch marks, craters and building aprons. A second feature under the
 * same name would make every later search, gate name and note ambiguous about which of
 * the two was meant.
 *
 * IT CANNOT PAINT A VEHICLE OR A STRUCTURE, and that falls out of where it is drawn
 * rather than being arranged. It goes down in the band between the shore wash and the
 * tiberium, before anything that stands on the ground is drawn, and it writes no depth.
 * Whatever is drawn afterwards is nearer the camera and simply covers it. There is no
 * projection anywhere in this file, which is the whole reason a unit can never catch one.
 *
 * WHERE THEY GO. Only on plain ground, and only where the eight neighbours are plain
 * ground too, so a patch never reaches over a road, a river, a shore, a bridge or a
 * cliff. The test is one integer compare: in every theater's tile table the clear
 * template occupies slots 0 to 15 and nothing else does.
 *
 * THE SAME MAP GIVES THE SAME PATCHES EVERY TIME. Which cell carries one, which art it
 * uses and how it is turned are all a hash of the cell's own coordinates, so nothing is
 * stored, nothing accumulates, and two runs of one scene produce identical frames, which
 * is a standing requirement of this renderer's gates.
 *
 * TIER 2 ONLY. It is blended, mip mapped RGBA over ground the Win98 build does not draw
 * this way; there, the ground is the cartridge's own art and no mottle is laid on it.
 */

#ifndef MOTTLE_MOD_H
#define MOTTLE_MOD_H

#define MOTTLE_MAGIC "CNC3DMOT"
#define MOTTLE_VER_MIN 1
#define MOTTLE_VER_MAX 1

/* How many of the eligible cells carry a patch, out of 256. Ground that is entirely
 * covered reads as a second texture rather than as variation, and ground with three
 * patches on a screen reads as litter. */
#define MOTTLE_DENSITY 40

struct MottlePatch {
    std::string name;
    GLuint tex;
    int side;      /* texels across the top level; this decides how much ground it covers */
};

static std::vector<MottlePatch> g_mottle;
static std::string g_mottlePath;
static std::vector<unsigned char> g_mottleClear;  /* per grid cell: plain ground?    */
static bool g_mottleReady = false;
static bool g_mottleActive = false;              /* last frame, after postprocessing ends */
static int  g_mottleDrawn = 0;                    /* last frame's count, for the log */

/* A cell's own number, mixed. Nothing here reads a clock or a counter, so the answer for
 * a cell is the same in every frame of every run. */
static unsigned mottle_hash(int x, int y)
{
    unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

static bool mottle_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    char magic[8];
    unsigned ver = 0, count = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, MOTTLE_MAGIC, 8)
        || fread(&ver, 4, 1, f) != 1 || fread(&count, 4, 1, f) != 1) {
        fprintf(stderr, "mottle: %s is not a mottle pack\n", path);
        fclose(f);
        return false;
    }
    /* A RANGE, not one number. A reader that accepts exactly the version it was written
       against turns every later pack into "not a mottle pack", which says nothing about
       what is actually wrong. */
    if (ver < MOTTLE_VER_MIN || ver > MOTTLE_VER_MAX) {
        fprintf(stderr, "mottle: %s is version %u; this build reads %d to %d\n",
                path, ver, MOTTLE_VER_MIN, MOTTLE_VER_MAX);
        fclose(f);
        return false;
    }

    for (unsigned i = 0; i < count; i++) {
        unsigned nlen = 0, levels = 0;
        char name[64];
        if (fread(&nlen, 4, 1, f) != 1 || nlen >= sizeof name) break;
        if (fread(name, 1, nlen, f) != nlen) break;
        name[nlen] = '\0';
        if (fread(&levels, 4, 1, f) != 1) break;

        MottlePatch p;
        p.name = name;
        p.side = 0;
        glGenTextures(1, &p.tex);
        glBindTexture(GL_TEXTURE_2D, p.tex);
        bool ok = true;
        for (unsigned l = 0; l < levels && ok; l++) {
            unsigned w = 0, h = 0, raw = 0, comp = 0;
            if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1
                || fread(&raw, 4, 1, f) != 1 || fread(&comp, 4, 1, f) != 1) { ok = false; break; }
            std::vector<unsigned char> cbuf(comp), rbuf(raw);
            if (fread(&cbuf[0], 1, comp, f) != comp) { ok = false; break; }
            uLongf got = raw;
            if (uncompress(&rbuf[0], &got, &cbuf[0], comp) != Z_OK || got != raw) { ok = false; break; }
            if (l == 0) p.side = (int)w;
            glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, &rbuf[0]);
        }
        if (!ok) { glDeleteTextures(1, &p.tex); break; }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        /* CLAMPED, because a mottle is one patch and not a tiling field: a wrapped sample
           at the quad's edge would fetch the far side of the same photograph and put a
           hard seam exactly where the mask is trying to fade out. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)levels - 1);
        g_mottle.push_back(p);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    fclose(f);

    g_mottleReady = !g_mottle.empty();
    g_mottlePath = g_mottleReady ? path : "";
    fprintf(stderr, "mottle: %s -- %d patches\n", path, (int)g_mottle.size());
    return g_mottleReady;
}

/* THE PATCHES GO BACK WITH THE MISSION. game_boot loads them every time, so without this
 * each mission start stacked another full set of textures on the last one: about 60 MB of
 * live heap per mission, which is what the app harness's memory trend caught. The clear
 * map goes too, and not only for the memory: it is rebuilt only when the grid SIZE
 * changes, so a second 64 by 64 map kept the first map's idea of where plain ground was
 * and laid patches over its roads. */
static void mottle_free(void)
{
    for (size_t i = 0; i < g_mottle.size(); i++)
        glDeleteTextures(1, &g_mottle[i].tex);
    g_mottle.clear();
    g_mottlePath.clear();
    g_mottleClear.clear();
    g_mottleReady = false;
    g_mottleDrawn = 0;
    g_mottleActive = false;
}

/* Plain ground, per cell, worked out once for the pack rather than per frame. The clear
 * template is slots 0 to 15 in every theater's table and nothing else reaches under 16,
 * so this is one compare and needs no template lookup and no map read. */
static void mottle_mark_clear(void)
{
    g_mottleClear.assign((size_t)g_gridW * g_gridH, 0);
    if(g_raModule) {
        for(int y=0;y<g_gridH;y++) for(int x=0;x<g_gridW;x++) {
            int slot=-1;g_mottleClear[(size_t)y*g_gridW+x]=(ra3d_detail_cell(x,y,&slot)&1)?1:0;
        }
        return;
    }
    /* Desert has 31 atlas rows, Temperate 24. A fixed 24-row lookup can
       mistake the Desert second row for clear ground. */
    int nslots = 0;
    tt_table(g_pack.theater, &nslots);
    for (size_t i = 0; i < g_pack.cell.size(); i++) {
        const PackCell& c = g_pack.cell[i];
        if (c.x < 0 || c.y < 0 || c.x >= g_gridW || c.y >= g_gridH) continue;
        const int slot = ts_slot_of_cell(c, nslots);
        g_mottleClear[(size_t)c.y * g_gridW + c.x] = (slot >= 0 && slot < 16) ? 1 : 0;
    }
}

static bool mottle_cell_ok(int x, int y)
{
    if (x <= 0 || y <= 0 || x >= g_gridW - 1 || y >= g_gridH - 1) return false;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (!g_mottleClear[(size_t)(y + dy) * g_gridW + (x + dx)]) return false;
    return true;
}

static float mottle_ground_texels(void);

/* Under Enhanced, on a high-resolution ground (mottle_ground_texels), Temperate or Desert/Sand. They
   were the experimental art's, and they did not go when that look was retired. */
static bool mottle_available(void)
{
    return g_mottleReady && g_fxActive && g_fx.enabled
           && g_fx.mottle_on && g_fx.mottle_strength > 0.0f
           && (g_raModule ? ra3d_detail_plants() : (!strcmp(g_pack.theater, "TEMPERAT") || !strcmp(g_pack.theater, "DESERT")
               || !strcmp(g_pack.theater, "SAND"))) && mottle_ground_texels() > 0.0f;
}

/* ------------------------------------------------------------------------------ *
 *  HOW MUCH GROUND A PATCH COVERS IS NOT A CHOICE. IT IS ARITHMETIC.
 *
 *  A patch and the ground under it have to carry texels of the SAME SIZE. Any other
 *  ratio shows: a patch denser than the ground reads as something sharp stuck onto a
 *  soft surface, and one thinner than it reads as a blurred spot, and both are wrong in
 *  the same way. So the world size follows from the art rather than being picked:
 *
 *      cells covered = texels across the patch / texels the ground gives a cell
 *
 *  The ground's figure is not a constant. It is the tile size times the scale the atlas
 *  was built at, and that scale changes when the generated detail art is in use. So the
 *  span is worked out per frame from the live value, and a patch covers less ground by
 *  itself when the ground gets denser.
 *
 *  IT COMES OUT FRACTIONAL AND THAT IS THE POINT. At 1024 texels over a ground giving a
 *  cell 264, a patch covers 3.88 cells. It is not snapped to the grid, it is not fitted
 *  to the shape of the clear area it lands in, and it OVERFLOWS the cells it started in.
 *  Fitting a patch to a region would mean stretching it, and stretching it is exactly
 *  what breaks the one rule this rests on. Size variety comes from baking at more than
 *  one resolution, never from scaling.
 *
 *  ON A COARSER GROUND THE PATCH KEEPS ITS OWN SIZE. The patches were made and tuned on
 *  the generated detail ground, 264 texels a cell. The Enhanced textures give a cell 144,
 *  and at that figure a 1024-texel patch would cover seven cells and need a clear disc
 *  nine cells across, which the playable part of a real map rarely has: on the two maps
 *  measured, every patch that fitted lay off the playable part, outside what is shown,
 *  drawn and counted and never seen. So the figure a patch
 *  is laid at never drops below 264 (mottle_patch_texels): it keeps the size it was tuned
 *  at, and is SHRUNK onto the coarser ground, which is the one direction the rule
 *  allows, because its trilinear mipmaps then draw it at about the ground's own texel
 *  size (the 512 level gives a cell 132). Stretching, the direction that blurs, is still
 *  never done.
 * ------------------------------------------------------------------------------ */

/* Texels the ground gives one cell, live, read off the terrain sheet that is DRAWN: every
 * sheet lays its cells TT_TS * k texels wide on a pitch of TT_PITCH * k, TT_COLS to a row,
 * so its used width says which k it was built at (6 for the Enhanced textures at 144, 5
 * for the Remastered art, 1 for the 1995 and cartridge tiles). 0 when the ground is one of
 * those 24-texel sheets: the patches are high-resolution art, and at 24 texels a cell one
 * would cover forty cells, so none is laid there. */
static float mottle_patch_texels(void);

static float mottle_ground_texels(void)
{
    if(g_raModule)return ra3d_detail_texels();
    const int ai = terrain_atlas_index();
    if (ai < 0 || ai >= (int)g_pack.tex.size()) return 0.0f;
    const int k = g_pack.tex[ai].uw / (TT_COLS * TT_PITCH);
    return k >= RT_K_BASE ? (float)(TT_TS * k) : 0.0f;
}

/* Texels a patch is laid at per cell: the ground's own figure, but never fewer than the
 * detail ground's the patches were made on (TT_TS * RT_K_DETAIL, 264); see ON A COARSER
 * GROUND above. 0 where no patch is laid at all. */
static float mottle_patch_texels(void)
{
    const float g = mottle_ground_texels();
    return g > 0.0f ? std::max(g, (float)(TT_TS * RT_K_DETAIL)) : 0.0f;
}

/* Every cell the blend shape can reach is plain ground. The shape is a blob inscribed in
 * the patch, so whatever the angle it stays inside a circle of half the span; the corners
 * of the square are transparent and need not be tested. One cell of margin covers the
 * feathered edge. */
static bool mottle_disc_ok(float cx, float cz, float r)
{
    const int x0 = (int)floorf(cx - r) - 1, x1 = (int)ceilf(cx + r) + 1;
    const int z0 = (int)floorf(cz - r) - 1, z1 = (int)ceilf(cz + r) + 1;
    int x, y;
    for (y = z0; y <= z1; y++)
        for (x = x0; x <= x1; x++) {
            const float dx = ((float)x + 0.5f) - cx, dz = ((float)y + 0.5f) - cz;
            if (dx*dx + dz*dz > (r + 1.0f) * (r + 1.0f)) continue;
            if (x < 0 || y < 0 || x >= g_gridW || y >= g_gridH) return false;
            if (!g_mottleClear[(size_t)y * g_gridW + x]) return false;
        }
    return true;
}

static void mottle_draw(void)
{
    g_mottleDrawn = 0;
    g_mottleActive = mottle_available();
    if (!g_mottleActive) return;
    if (g_mottleClear.size() != (size_t)g_gridW * g_gridH) mottle_mark_clear();

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const bool oneSun = g_fxActive && g_fx.sun_lambert && g_fx.terrain_normals;
    const float flat = 160.0f / 255.0f;
    const float groundTexels = mottle_patch_texels();
    int ax, ay, gx, gz, k, tix;

    for (ay = 0; ay < g_gridH; ay++)
    for (ax = 0; ax < g_gridW; ax++) {
        const unsigned h = mottle_hash(ax, ay);
        if ((h & 255u) >= (unsigned)(g_fx.mottle_density * 255.0f)) continue;

        const MottlePatch& p = g_mottle[(h >> 24) % g_mottle.size()];
        if (p.side <= 0 || groundTexels <= 0.0f) continue;

        /* THE ONE LINE THE WHOLE FEATURE RESTS ON. */
        const float span = (float)p.side / groundTexels;
        const float r = span * 0.5f;

        /* Anchored anywhere inside its own cell, so the field keeps no grid. */
        const float cx = (float)ax + (float)((h >> 11) & 255u) / 255.0f;
        const float cz = (float)ay + (float)((h >> 19) & 255u) / 255.0f;
        if (!mottle_disc_ok(cx, cz, r)) continue;

        {
            const float ang = (float)((h >> 13) & 1023u) * (6.2831853f / 1024.0f);
            const float ca = (float)cos(ang), sa = (float)sin(ang);
            const int x0 = (int)floorf(cx - r) - 1, x1 = (int)ceilf(cx + r) + 1;
            const int z0 = (int)floorf(cz - r) - 1, z1 = (int)ceilf(cz + r) + 1;
            bool seen = false;
            for (gz = z0; gz <= z1 && !seen; gz++)
                for (gx = x0; gx <= x1 && !seen; gx++)
                    if (gx >= 0 && gz >= 0 && gx < g_gridW && gz < g_gridH
                        && cell_in_view(gx, gz)) seen = true;
            if (!seen) continue;

            glBindTexture(GL_TEXTURE_2D, p.tex);
            glBegin(GL_TRIANGLES);
            for (gz = z0; gz <= z1; gz++)
            for (gx = x0; gx <= x1; gx++) {
                static const int QX[4] = { 0, 1, 1, 0 };
                static const int QZ[4] = { 0, 0, 1, 1 };
                static const int TRI[6] = { 0, 3, 1, 1, 3, 2 };
                float vx[4], vy[4], vz[4], vu[4], vv[4], vsh[4];
                if (gx < 0 || gz < 0 || gx >= g_gridW || gz >= g_gridH) continue;
                if (!cell_shown(gx, gz)) continue;
                for (k = 0; k < 4; k++) {
                    const int wxi = gx + QX[k], wzi = gz + QZ[k];
                    const float lx = ((float)wxi - cx) / span;
                    const float lz = ((float)wzi - cz) / span;
                    vx[k] = (float)wxi; vz[k] = (float)wzi;
                    vy[k] = terrain_corner_y(wxi, wzi);
                    vsh[k] = (oneSun ? flat : terrain_shade(wxi, wzi))
                             * shroud_corner_vis(wxi, wzi);
                    /* Offset from the patch centre in patch widths, turned, then moved
                       into 0..1. Outside that the mask is transparent and the clamped
                       edge keeps it so, which is what lets a patch overflow its cells. */
                    vu[k] = (lx * ca - lz * sa) + 0.5f;
                    vv[k] = (lx * sa + lz * ca) + 0.5f;
                }
                for (tix = 0; tix < 6; tix++) {
                    k = TRI[tix];
                    glColor4f(vsh[k], vsh[k], vsh[k], g_fx.mottle_strength);
                    glTexCoord2f(vu[k], vv[k]);
                    glVertex3f(vx[k], vy[k], vz[k]);
                }
            }
            glEnd();
            g_mottleDrawn++;
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glColor4f(1, 1, 1, 1);
}

#endif /* MOTTLE_MOD_H */
