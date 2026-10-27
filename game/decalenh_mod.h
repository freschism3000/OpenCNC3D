/* ====================================================================================
 *  decalenh_mod.h -- Enhanced's remastered craters and scorch marks.
 *
 *  The 1995 PC game's CR1..CR6 and SC1..SC6, painted again at the resolution Enhanced draws
 *  the ground at (tools/art/remaster/decals, baked into decalenh.pack by bake_decalenh.py).
 *  Under Enhanced, with the decal_enh dial on and the pack loaded, draw_smudges hands every
 *  crater and scorch it has art for to draw_decalenh. Everything else about a smudge is
 *  unchanged: which cell, which frame, the console's priority against tiberium and walls,
 *  and the building aprons, which have their own remaster (bibenh_mod.h).
 *
 *  TWO LAYERS, because a decal laid over the ground never belongs to it: the eye finds the
 *  line where the painting stops and the ground starts.
 *    THE SOOT burns the ground: a doubled multiply (the ground times twice the texel), so
 *    mid-grey leaves it alone, darker chars it and lighter tints it, which is how the ring of
 *    scorched grass round a black heart is drawn. The grass or sand keeps its own grain under
 *    the burn and the burn fades into it with no edge at all. It covers the 3 x 3 cells round the decal's cell, each on
 *    its own four corners, so a burn spreads past its cell and is never cut by it. A scorch
 *    mark is soot alone; a crater lies on a blast.
 *    THE BODY, a crater's soil, is laid over that on its own cell as the cartridge's decal
 *    is laid, lit by the same per-corner shade and shroud, its edge faded into the ground.
 *
 *  THE TEXTURE IS PREMULTIPLIED AND MIPMAPPED, both built here from the pack's straight
 *  RGBA. A 144-texel decal seen from the default camera covers a few dozen pixels, so
 *  without a mip chain it shimmers; and a straight-alpha texture filtered across its soft
 *  edge pulls the colour of the transparent texels into the decal, a dark fringe. Each
 *  level is a 2x2 box of the premultiplied level above, and the pass blends ONE,
 *  ONE_MINUS_SRC_ALPHA. GL 1.1 calls only: the levels are uploaded one by one.
 *
 *  Tier 2 only in practice, as Enhanced is; nothing here needs more than fixed function.
 * ==================================================================================== */
#ifndef OPENCNC3D_DECALENH_MOD_H
#define OPENCNC3D_DECALENH_MOD_H
#include "decal_desert_blend.h"

enum { DECALENH_TYPES = 12, DECALENH_MAXFRAMES = 5 };

enum { DECALENH_BODY = 1, DECALENH_SOOT = 2 };

struct DecalEnhTheater {
    unsigned theaterId;
    int      texw, texh;                /* the body sheet */
    int      sootw, sooth;              /* the soot sheet */
    int      nframes[DECALENH_TYPES];   /* 0 = no remaster: the cartridge's art is drawn */
    int      layers[DECALENH_TYPES];    /* DECALENH_BODY | DECALENH_SOOT */
    GLuint   tex, soot;
    GLuint   sootSnow;                  /* the burn on snow (decalenh_snow_soot), or 0 */
};

static std::vector<DecalEnhTheater> g_deTh;
static int  g_deCell = 144, g_deGutter = 8;
static int  g_deSootCell = 96, g_deSootSpan = 3, g_deSootGutter = 8;
static bool g_deHave = false;
static bool g_deOn   = true;            /* --nodecalenh is the A/B */
static struct { int path, cells; } g_deStat;   /* `decaldump`: this frame's pass */

static void decalenh_free(void)
{
    for (size_t i = 0; i < g_deTh.size(); i++) {
        if (g_deTh[i].tex)  glDeleteTextures(1, &g_deTh[i].tex);
        if (g_deTh[i].soot) glDeleteTextures(1, &g_deTh[i].soot);
        if (g_deTh[i].sootSnow) glDeleteTextures(1, &g_deTh[i].sootSnow);
    }
    g_deTh.clear();
    g_deHave = false;
}

/* Upload a sheet with its whole mip chain: the body's straight RGBA premultiplied (nc 4),
   the soot's RGB as it is (nc 3, mid-grey the untouched ground, which every level keeps). */
static GLuint decalenh_upload(std::vector<unsigned char>& rgba, int w, int h, int nc = 4)
{
    if (nc == 4)
        for (size_t k = 0; k < rgba.size(); k += 4) {
            const unsigned a = rgba[k + 3];
            for (int c = 0; c < 3; c++)
                rgba[k + c] = (unsigned char)((rgba[k + c] * a + 127) / 255);
        }
    const GLenum fmt = nc == 4 ? GL_RGBA : GL_RGB;
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::vector<unsigned char> lv = rgba, nx;
    int lw = w, lh = h;
    for (int level = 0; ; level++) {
        glTexImage2D(GL_TEXTURE_2D, level, fmt, lw, lh, 0, fmt, GL_UNSIGNED_BYTE, &lv[0]);
        if (lw == 1 && lh == 1)
            break;
        const int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
        nx.assign((size_t)nw * nh * nc, 0);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < nc; c++) {
                    unsigned s = 0, n = 0;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            const int sx = x * 2 + dx, sy = y * 2 + dy;
                            if (sx < lw && sy < lh) { s += lv[((size_t)sy * lw + sx) * nc + c]; n++; }
                        }
                    nx[((size_t)y * nw + x) * nc + c] = (unsigned char)((s + n / 2) / n);
                }
        lv.swap(nx);
        lw = nw; lh = nh;
    }
    return t;
}

/* THE BURN ON SNOW, for a theater the pack has no set for (Red Alert's snow, which draws
   the temperate set): the temperate burn's char without its tint. The doubled multiply
   tints the ground wherever the painting is lighter than mid-grey, which on grass is the
   ring of scorched growth round a black heart; on white snow the same texels turn the
   snow peach, a glow round the crater rather than a burn. Here each texel is its own
   luminance and never lighter than mid-grey, so on snow a burn only darkens it, grey to
   black, and fades into it with the same soft edge. */
static void decalenh_snow_soot(std::vector<unsigned char>& rgb)
{
    for (size_t k = 0; k + 2 < rgb.size(); k += 3) {
        int l = (299 * rgb[k] + 587 * rgb[k + 1] + 114 * rgb[k + 2] + 500) / 1000;
        if (l > 128) l = 128;
        rgb[k] = rgb[k + 1] = rgb[k + 2] = (unsigned char)l;
    }
}

/* One zlib block of a sheet, inflated to exactly `bytes`. */
static bool decalenh_inflate(FILE* f, std::vector<unsigned char>& out, size_t bytes)
{
    unsigned zb = 0;
    if (fread(&zb, 4, 1, f) != 1 || zb == 0 || zb > (64u << 20))
        return false;
    std::vector<unsigned char> z(zb);
    out.resize(bytes);
    uLongf got = (uLongf)bytes;
    return fread(&z[0], 1, zb, f) == zb
        && uncompress(&out[0], &got, &z[0], zb) == Z_OK && got == bytes;
}

/* snowToo: also make each theater's burn on snow (decalenh_snow_soot), for a game whose snow
   draws the temperate set (Red Alert's); Tiberian Dawn makes none. */
static bool decalenh_load(const char* path, bool snowToo = false)
{
    decalenh_free();
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "decalenh: no pack at %s; Enhanced draws the cartridge's craters "
                        "and scorch marks\n", path);
        return false;
    }
    char magic[8];
    unsigned hd[9];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "DECALEN3", 8) != 0
        || fread(hd, 4, 9, f) != 9 || hd[0] != 3 || hd[1] < 1 || hd[1] > 8
        || hd[2] < 8 || hd[2] > 512 || hd[3] > 64
        || hd[4] != DECALENH_TYPES || hd[5] != DECALENH_MAXFRAMES
        || hd[6] < 8 || hd[6] > 512 || hd[7] < 1 || hd[7] > 5 || hd[8] > 64) {
        fprintf(stderr, "decalenh: %s is not a readable decal pack (version 3)\n", path);
        fclose(f);
        return false;
    }
    g_deCell = (int)hd[2];
    g_deGutter = (int)hd[3];
    g_deSootCell = (int)hd[6];
    g_deSootSpan = (int)hd[7];
    g_deSootGutter = (int)hd[8];
    int drawn = 0;
    for (unsigned t = 0; t < hd[1]; t++) {
        DecalEnhTheater T;
        memset(&T, 0, sizeof T);
        char nm[13]; memset(nm, 0, sizeof nm);
        unsigned wh[4] = {0, 0, 0, 0}, nf[DECALENH_TYPES], ly[DECALENH_TYPES];
        bool ok = fread(nm, 1, 12, f) == 12 && fread(&T.theaterId, 4, 1, f) == 1
               && fread(wh, 4, 4, f) == 4 && fread(nf, 4, DECALENH_TYPES, f) == DECALENH_TYPES
               && fread(ly, 4, DECALENH_TYPES, f) == DECALENH_TYPES
               && wh[0] > 0 && wh[1] > 0 && wh[0] <= 4096 && wh[1] <= 4096
               && wh[2] > 0 && wh[3] > 0 && wh[2] <= 4096 && wh[3] <= 4096;
        std::vector<unsigned char> body, soot;
        ok = ok && decalenh_inflate(f, body, (size_t)wh[0] * wh[1] * 4)
                && decalenh_inflate(f, soot, (size_t)wh[2] * wh[3] * 3);
        if (!ok) {
            fprintf(stderr, "decalenh: %s theater %u unreadable\n", path, t);
            fclose(f);
            decalenh_free();
            return false;
        }
        T.texw = (int)wh[0]; T.texh = (int)wh[1];
        T.sootw = (int)wh[2]; T.sooth = (int)wh[3];
        int have = 0;
        for (int k = 0; k < DECALENH_TYPES; k++) {
            T.nframes[k] = nf[k] <= DECALENH_MAXFRAMES ? (int)nf[k] : 0;
            T.layers[k] = T.nframes[k] ? (int)(ly[k] & 3u) : 0;
            have += T.layers[k] != 0;
        }
        /* Desert's depression/rim colours stay authored; blend their coverage and
           soot density into the real sand instead of painting an opaque black card. */
        decal_desert_blend(T.theaterId,body.data(),T.texw,T.texh,g_deCell,g_deGutter,
                           soot.data(),soot.size());
        T.tex = decalenh_upload(body, T.texw, T.texh, 4);
        T.soot = decalenh_upload(soot, T.sootw, T.sooth, 3);
        if (snowToo && T.theaterId == 2) {          /* (the temperate set's, TEMPERATE 2) */
            decalenh_snow_soot(soot);
            T.sootSnow = decalenh_upload(soot, T.sootw, T.sooth, 3);
        }
        g_deTh.push_back(T);
        drawn += have;
        fprintf(stderr, "decalenh: %s, %d of %d craters and scorch marks remastered\n",
                nm, have, DECALENH_TYPES);
    }
    fclose(f);
    fprintf(stderr, "decalenh: the body %d texels a cell, the soot %d over %d x %d cells\n",
            g_deCell, g_deSootCell, g_deSootSpan, g_deSootSpan);
    g_deHave = drawn > 0;
    return g_deHave;
}

static const DecalEnhTheater* decalenh_set_for(int theater)
{
    for (size_t i = 0; i < g_deTh.size(); i++)
        if ((int)g_deTh[i].theaterId == theater)
            return &g_deTh[i];
    return NULL;
}

/* Whether the remaster has art for this smudge: a crater or scorch (types 0..11) whose
   frame is in the strip. Anything else stays with the cartridge's pass. */
static bool decalenh_has(const DecalEnhTheater* de, int type, int frame)
{
    return de && type >= 0 && type < DECALENH_TYPES && frame >= 0
        && frame < de->nframes[type] && de->layers[type] != 0;
}

#endif
