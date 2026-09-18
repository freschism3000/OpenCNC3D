/* ================================================================================== *
 *  tib3d_mod.h -- solid tiberium crystals, from tib3d.pack.
 *
 *  WHAT THIS IS, AND WHAT IT IS NOT
 *
 *  The console draws tiberium as a flat 24x24 decal on the cell and nothing else:
 *  twelve overlays (TI1..TI12), twelve density frames each, one quad, no height.
 *  dostib_mod.h ships that art and it stays the default. This module stands
 *  geometry on top of the same cells, and every part of it is OURS or IMPORTED --
 *  there is no cartridge model of tiberium to be faithful to, so nothing here can
 *  claim to be a decode.
 *
 *  The clumps come from a third-party Command & Conquer 3 mod, decoded by
 *  bake_tib3d.py, which carries the container and mesh formats. Four of them, in
 *  growth order:
 *
 *      0  a dark rocky vent, the seed a field starts from
 *      1  two crystal shards standing up
 *      2  a fan of shards, and the tallest
 *      3  a dense crystal bush
 *
 *  ENHANCED ONLY, for the same reason the terrain art sets are: the shipped
 *  picture is the console's, and a player who has not asked for anything else
 *  gets it. Under Enhanced the crystals REPLACE the decal rather than standing on
 *  it: the decal is a flat tiberium field of its own, and seen through solid
 *  crystals the two read as one another's shadow. It can be put back under them
 *  with one dial, which is worth having because the decal is the only thing that
 *  states a field's footprint at cell resolution.
 *
 *  HOW A CELL IS FILLED, WHICH IS AN EXTRAPOLATION
 *
 *  The engine gives two numbers per cell and this module spends both. `kind` is
 *  which of the twelve overlays (odata.cpp names TI1..TI12; the brain's TIB| line
 *  sends Overlay - OVERLAY_TIBERIUM1). `stage` is OverlayData, which
 *  CellClass::Tiberium_Adjust sets from _adj[] over the count of adjacent
 *  tiberium cells, so it is a DENSITY in 0..11 and it falls as the cell is
 *  harvested. So: denser cell, more clumps and later ones. The count and the
 *  choice below are a judgement, not a decode, and the twelve overlay shapes are
 *  NOT reproduced -- kind only feeds the hash, so two neighbouring patch types
 *  scatter differently instead of stamping the same arrangement twice.
 *
 *  Placement is a hash of the cell and the clump index, never a random number and
 *  never the clock, so a field looks the same on every run and two --shot runs of
 *  one script stay byte-identical.
 *
 *  THREE PASSES, all GL 1.1 and all Voodoo 2 safe: client vertex arrays and
 *  glDrawElements, no shaders, no buffer objects, two textures in the whole
 *  feature, both powers of two inside the TMU's 256-per-side limit.
 *
 *    1  the GROUND SPLAT, one quad per cell of the sheet's own rock, alpha
 *       blended over the terrain. Drawn WIDER than a cell so neighbours overlap
 *       toward opaque across the middle of a field and feather at its edge
 *       rather than tiling into a grid of squares.
 *    2  the CLUMPS, opaque with depth writes on, modulated by a per-vertex
 *       shade: the clump's own normal against the sun the rest of the Enhanced
 *       chain uses, times the cell's ground shade and shroud.
 *    3  the GLOW, the same geometry added over itself through the emissive mask
 *       in the sheet's alpha. The source shader had an emissive term and this
 *       renderer has no shader to carry one, so the mask is baked instead: see
 *       emissive_alpha in bake_tib3d.py for how it is derived rather than
 *       invented. Additive, depth writes off, so it lights nothing but itself.
 * ================================================================================== */

#ifndef CNC3D_TIB3D_MOD_H
#define CNC3D_TIB3D_MOD_H

struct Tib3dVert { float x, y, z, nx, ny, nz, u, v; };

struct Tib3dClump {
    std::vector<Tib3dVert>    vert;
    std::vector<unsigned int> idx;     /* 3 per triangle */
    float height, radius;              /* pack units: the tallest clump is 1.0 */
    /* HOW HIGH THIS CLUMP'S SURFACE IS AT ITS OWN MIDDLE. Measured at load rather than
       baked, because it is a property of the geometry and nothing else needs it in the
       file. It exists for the POD: the pod is a crater with a dip in the middle, so a
       crystal whose base sits at ground level sits in the HOLE, with the pod's rim
       standing up around it. Seating the crystal on this height puts it on the pod
       instead of in it. */
    float seat;
};

/* Clump 0 is the brown rocky pod. Named here because both the loader and the placement
   need it and it indexes the vector below. */
enum { TIB3D_POD = 0 };

static std::vector<Tib3dClump> g_tib3dClump;
static GLuint g_tib3dTex    = 0;   /* the clump sheet; alpha is the emissive mask */
static GLuint g_tib3dGround = 0;   /* the ground splat; alpha is its coverage     */
static bool   g_tib3dHave   = false;
static bool   g_tib3dHaveGround = false;

/* Call with a live GL context (it uploads the sheet). */
static bool tib3d_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "tiberium: no 3D pack at %s (run bake_tib3d.py); Enhanced "
                        "draws the flat decal alone\n", path);
        return false;
    }
    char magic[8];
    unsigned ver = 0, n = 0, tw = 0, th = 0, gside = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "TIB3D1", 6) != 0) {
        fprintf(stderr, "tiberium: %s is not a tib3d pack\n", path);
        fclose(f);
        return false;
    }
    /* Version 2 added the emissive mask in the sheet's alpha and the ground splat in
       the field that version 1 left reserved, so a v1 pack still reads: its alpha is a
       flat 255 and its reserved zero means no splat. Both are then simply absent. */
    if (fread(&ver, 4, 1, f) != 1 || ver < 1 || ver > 2 ||
        fread(&n, 4, 1, f) != 1 || fread(&tw, 4, 1, f) != 1 ||
        fread(&th, 4, 1, f) != 1 || fread(&gside, 4, 1, f) != 1 ||
        n < 1 || n > 64 || tw < 1 || th < 1 || tw > 4096 || th > 4096 ||
        gside > 1024) {
        fprintf(stderr, "tiberium: %s header unreadable\n", path);
        fclose(f);
        return false;
    }
    g_tib3dClump.resize(n);
    for (unsigned i = 0; i < n; i++) {
        unsigned nv = 0, nt = 0;
        float h = 0.0f, r = 0.0f;
        if (fread(&nv, 4, 1, f) != 1 || fread(&nt, 4, 1, f) != 1 ||
            fread(&h, 4, 1, f) != 1 || fread(&r, 4, 1, f) != 1 ||
            nv < 3 || nt < 1 || nv > 200000 || nt > 200000) {
            fprintf(stderr, "tiberium: %s clump %u unreadable\n", path, i);
            g_tib3dClump.clear();
            fclose(f);
            return false;
        }
        Tib3dClump& c = g_tib3dClump[i];
        c.height = h;
        c.radius = r;
        c.vert.resize(nv);
        c.idx.resize((size_t)nt * 3);
        if (fread(&c.vert[0], sizeof(Tib3dVert), nv, f) != nv ||
            fread(&c.idx[0], 4, c.idx.size(), f) != c.idx.size()) {
            fprintf(stderr, "tiberium: %s clump %u is short\n", path, i);
            g_tib3dClump.clear();
            fclose(f);
            return false;
        }
        /* HOW HIGH THIS CLUMP'S ROCK STANDS, as the 75th percentile of its vertex
           heights. A percentile, and not the height at the middle, because THE POD HAS
           NO MIDDLE: measured, it is a ring with no geometry at all inside 0.17 of its
           0.463 radius, an inner lip at 0.027 and a rim at 0.363. Asking for the
           surface at the axis therefore answered zero, which is the ground, which is
           the bowl -- and a crystal seated there is exactly the crystal-inside-the-pod
           this measure exists to fix. The 75th percentile lands near the rim without
           taking the single highest spike, so a crystal sits ON the rock rather than
           floating over its highest corner. */
        {
            std::vector<float> ys(c.vert.size());
            for (size_t v = 0; v < c.vert.size(); v++)
                ys[v] = c.vert[v].y;
            std::sort(ys.begin(), ys.end());
            c.seat = ys.empty() ? 0.0f : ys[(ys.size() * 3) / 4];
        }
        for (size_t k = 0; k < c.idx.size(); k++)
            if (c.idx[k] >= nv) {
                fprintf(stderr, "tiberium: %s clump %u indexes past its vertices\n",
                        path, i);
                g_tib3dClump.clear();
                fclose(f);
                return false;
            }
    }
    std::vector<unsigned char> px((size_t)tw * th * 4);
    if (fread(&px[0], 1, px.size(), f) != px.size()) {
        fprintf(stderr, "tiberium: %s carries no %ux%u sheet\n", path, tw, th);
        g_tib3dClump.clear();
        fclose(f);
        return false;
    }
    std::vector<unsigned char> gpx;
    if (gside) {
        gpx.resize((size_t)gside * gside * 4);
        if (fread(&gpx[0], 1, gpx.size(), f) != gpx.size()) {
            fprintf(stderr, "tiberium: %s carries no %ux%u ground splat\n",
                    path, gside, gside);
            gpx.clear();
            gside = 0;
        }
    }
    fclose(f);

    glGenTextures(1, &g_tib3dTex);
    glBindTexture(GL_TEXTURE_2D, g_tib3dTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)tw, (GLsizei)th, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);

    if (gside) {
        glGenTextures(1, &g_tib3dGround);
        glBindTexture(GL_TEXTURE_2D, g_tib3dGround);
        /* CLAMP, not repeat: the splat is one soft blob per cell and a wrapped edge
           would fetch the far side of it wherever the quad's own alpha has already
           gone to zero. GL_CLAMP rather than GL_CLAMP_TO_EDGE for the Voodoo 2's
           sake, and the difference is a texel of an already invisible border. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)gside, (GLsizei)gside, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &gpx[0]);
        g_tib3dHaveGround = true;
    }

    size_t tris = 0;
    for (size_t i = 0; i < g_tib3dClump.size(); i++)
        tris += g_tib3dClump[i].idx.size() / 3;
    fprintf(stderr, "tiberium: %s v%u -- %u clumps, %zu triangles, %ux%u sheet, "
                    "ground splat %s, pod rock stands %.3f high\n",
            path, ver, n, tris, tw, th, g_tib3dHaveGround ? "yes" : "none",
            g_tib3dClump[TIB3D_POD].seat);
    g_tib3dHave = true;
    return true;
}

static void tib3d_free(void)
{
    if (g_tib3dTex)
        glDeleteTextures(1, &g_tib3dTex);
    if (g_tib3dGround)
        glDeleteTextures(1, &g_tib3dGround);
    g_tib3dTex = g_tib3dGround = 0;
    g_tib3dClump.clear();
    g_tib3dHave = g_tib3dHaveGround = false;
}

/* ---- how many clumps a cell gets, and which -----------------------------------------
   OURS. `stage` is the engine's density in 0..11 (Tiberium_Adjust's _adj[] only ever
   produces 0,1,3,4,6,7,8,10,11, so the table is written across the whole range rather
   than for the nine values that occur). A thin cell gets one small thing; a full one
   gets four and the big bush among them. Each row is the clump indices a cell at that
   density may draw, repeated to weight the choice. */
struct Tib3dFill { unsigned char count; unsigned char pick[6]; unsigned char npick; };

/* THE POD IS NOT IN THIS TABLE. Clump 0 is the brown rocky pod, and in the source art
   it is what a crystal grows out of rather than one more thing scattered through the
   field -- so it is not picked, it is PAIRED: every clump the table below asks for is
   placed on a pod of its own, sized to it and centred under it. Weighting the pod into
   the mix instead was tried and is wrong, because a cell then draws crystals with
   nothing under them and pods with nothing on them. */

static const Tib3dFill TIB3D_FILL[12] = {
    /*  0 */ { 1, { 1 },                1 },
    /*  1 */ { 1, { 1, 1, 2 },          3 },
    /*  2 */ { 2, { 1, 1, 2 },          3 },
    /*  3 */ { 2, { 1, 1, 2 },          3 },
    /*  4 */ { 2, { 1, 2, 2, 3 },       4 },
    /*  5 */ { 3, { 1, 2, 2, 3 },       4 },
    /*  6 */ { 3, { 1, 2, 2, 3, 3 },    5 },
    /*  7 */ { 3, { 1, 2, 2, 3, 3 },    5 },
    /*  8 */ { 4, { 2, 2, 3, 3 },       4 },
    /*  9 */ { 4, { 2, 2, 3, 3 },       4 },
    /* 10 */ { 4, { 2, 3, 3 },          3 },
    /* 11 */ { 5, { 2, 3, 3 },          3 },
};

/* The same mixer draw_tiberium's procedural fallback uses, so the two arrangements are
   related rather than merely both arbitrary. Deterministic in the cell, which is what
   keeps a field steady under the camera and two --shot runs identical. */
static unsigned tib3d_hash(int x, int y, unsigned k)
{
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ k * 83492791u;
    h ^= h >> 13; h *= 2654435761u; h ^= h >> 16;
    return h;
}

#endif /* CNC3D_TIB3D_MOD_H */
