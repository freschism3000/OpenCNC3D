/* ================================================================================== *
 *  dosinf_mod.h -- the 1995 MS-DOS infantry sprites (dosinfantry.pack, "DOSINF01")
 *
 *  The user compared the N64 infantry billboards against the DOS art and chose DOS.
 *  This module loads the pack baked by fb/infantry/bake_dosinfantry.py and gives the
 *  renderer everything it needs to draw a DOS infantryman instead of an N64 strip:
 *  textures, per-type anim slots, and the ENGINE's own facing math.
 *
 *  Frame selection is InfantryClass::Draw_It (tiberiandawn/infantry.cpp:574):
 *      shapenum = Frame + HumanShape[Facing32[dir]] * Jump + stage % Count
 *  The pack stores each (type, house, anim) as one strip, FACING-MAJOR, facenum
 *  0..7 counter-clockwise from north (N, NW, W, SW, S, SE, E, NE), all 8 facings
 *  present, so unlike the N64 path there is NO mirroring. Deaths are
 *  non-directional (facings == 1) and step on the engine's own dostage.
 *
 *  House colours were applied at bake time (house.cpp:2186: gold row for anyone
 *  who is not BadGuy, RemapLtBlue row for BadGuy; civilians one fixed row).
 *
 *  Index 0 is transparent and index 4 is the DOS shadow-ghost colour
 *  (display.cpp:357 UShadowCols {LTGREEN=4, BLACK, 130}); both become alpha 0
 *  here -- the N64 billboards this replaces cast no shadow either. Textures are
 *  power-of-two and <= 256x256 (Voodoo 2 limit), frames on a simple grid.
 *
 *  OpenGL 1.1 only: glGenTextures/glTexImage2D/glBegin quads, no extensions.
 * ================================================================================== */

#ifndef DOSINF_MOD_H
#define DOSINF_MOD_H

struct DosStrip {
    char name[25];   /* 24 on disk since pack v3, + the NUL this never had to spare */
    int frames, facings, stages, fw, fh, cols, texw, texh;
    int src_stages;   /* the engine's DoInfoStruct Count; > stages when the strip
                         was evenly subsampled to fit the Voodoo 2 256x256 limit
                         (v2: only E4's 16-stage FIRE and FPRONE are baked as 8) */
    GLuint gl;
};

/* Anim slots, in pack order (v2). The death slots map 1:1 onto the engine's DoType
   (defines.h:1523): 22 GUN, 23 EXPL, 24 EXPL2, 25 GREN, 26 FIRE. v2 appended the
   live ground-combat family after them (v1's order is a strict prefix): FIRE is
   the standing DO_FIRE_WEAPON pose, then the prone war the engine actually fights
   (a man under fire lies down, fires prone, crawls, gets up; doing/dostage are
   exported per tick, so every step is the engine's own). E6 the engineer has no
   fire art (-1 in the pack). */
enum DosAnim {
    DA_STAND = 0, DA_WALK, DA_D_GUN, DA_D_EXPL, DA_D_EXPL2, DA_D_GREN, DA_D_FIRE,
    DA_FIRE, DA_PRONE, DA_FIRE_PRONE, DA_LIE_DOWN, DA_CRAWL, DA_GET_UP,
    /* The two the baker never took until recently. DoTypes 9 and 10. */
    DA_IDLE1, DA_IDLE2,
    DA_COUNT
};

/* Live (non-death) engine DoType -> anim slot, defines.h:1523: 2 PRONE,
   4 FIRE_WEAPON, 5 LIE_DOWN, 6 CRAWL, 7 GET_UP, 8 FIRE_PRONE. Everything else
   (stand/guard/walk/idles/gestures) returns -1: the caller keeps its own
   movement-based STAND/WALK pick, exactly the v1 behaviour. */
static int dosinf_live_slot(int doing)
{
    switch (doing) {
    case 2: return DA_PRONE;
    case 4: return DA_FIRE;
    case 5: return DA_LIE_DOWN;
    case 6: return DA_CRAWL;
    case 7: return DA_GET_UP;
    case 8: return DA_FIRE_PRONE;
    /* DO_IDLE1 / DO_IDLE2, defines.h rows 9 and 10. The engine picks one out of the
       guard state machine (infantry.cpp Do_Action(DO_IDLE1)) and drives it with the same
       Fetch_Stage() every other live Do uses, so the caller's existing `anim == live`
       stage path already animates them with no further plumbing. */
    case 9: return DA_IDLE1;
    case 10: return DA_IDLE2;
    default: return -1;
    }
}

struct DosInfType { int strip[2][DA_COUNT]; };   /* rows: 0 gold, 1 ltblue */

static std::vector<DosStrip> g_dosStrips;
static std::map<std::string, DosInfType> g_dosInfTypes;
static bool g_dosinfOn = false;

/* const.cpp:211 Facing32[256] -- DirType 0..255 to the 32-facing wheel, including
   Westwood's 3D-Studio 45-degree distortion compensation. Transcribed verbatim. */
static const unsigned char DOSINF_Facing32[256] = {
    0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,
    7,  7,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,
    11, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21,
    22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 26,
    26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29,
    30, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,  0,  0,  0,  0,  0};

/* infantry.cpp:84 HumanShape[32] -- 32-facing wheel to the 8 stored sprite facings,
   counter-clockwise: 0 N, 1 NW, 2 W, 3 SW, 4 S, 5 SE, 6 E, 7 NE. */
static const int DOSINF_HumanShape[32] = {0, 0, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 5, 4,
                                          4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0};

static int dosinf_facenum(int dir)
{
    return DOSINF_HumanShape[DOSINF_Facing32[dir & 255]];
}

/* Engine DoType -> death slot. PUNCH_DEATH (17) and KICK_DEATH (20) have no strip in
   the pack (hand-to-hand only); they borrow the gun death rather than vanishing. */
static int dosinf_death_slot(int doing)
{
    switch (doing) {
    case 23: return DA_D_EXPL;
    case 24: return DA_D_EXPL2;      /* differs from EXPL only for E6 */
    case 25: return DA_D_GREN;
    case 26: return DA_D_FIRE;
    default: return DA_D_GUN;        /* 22, and the 17/20 fallback */
    }
}

static bool dosinf_read(FILE* f, void* p, size_t n, const char* path)
{
    if (fread(p, 1, n, f) == n) return true;
    fprintf(stderr, "dosinf: %s ends early at byte %ld -- truncated?\n", path, ftell(f));
    fclose(f);
    return false;
}

static bool dosinf_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "dosinf: cannot open %s -- keeping the N64 infantry\n", path);
        return false;
    }
    char magic[8];
    if (!dosinf_read(f, magic, 8, path)) return false;
    if (memcmp(magic, "DOSINF01", 8) != 0) {
        fprintf(stderr, "dosinf: bad magic in %s (want DOSINF01)\n", path);
        fclose(f);
        return false;
    }
    uint32_t ver = 0, nstrip = 0, ntype = 0;
    if (!dosinf_read(f, &ver, 4, path)) return false;
    if (!dosinf_read(f, &nstrip, 4, path)) return false;
    if (!dosinf_read(f, &ntype, 4, path)) return false;
    if (ver != 4) {
        /* v2 added the FIRE slot and the per-strip src_stages field; v3 widened the
           per-strip name from 16 to 24 bytes (MOEBIUS_D_EXPL2#0 is 17) when the three
           named characters landed. Either older pack would misparse from here on.
           Loud fallback, never a quiet garble. */
        fprintf(stderr, "dosinf: %s is pack version %u, this build wants 4 -- "
                        "rebake with bake_dosinfantry.py; keeping the N64 infantry\n",
                path, ver);
        fclose(f);
        return false;
    }
    unsigned char pal6[768], pal8[768];
    if (!dosinf_read(f, pal6, 768, path)) return false;
    if (!dosinf_read(f, pal8, 768, path)) return false;

    g_dosStrips.resize(nstrip);
    std::vector<unsigned char> idx, rgba;
    for (uint32_t i = 0; i < nstrip; i++) {
        DosStrip& s = g_dosStrips[i];
        memset(s.name, 0, sizeof(s.name));
        if (!dosinf_read(f, s.name, 24, path)) return false;
        uint32_t q[9];
        if (!dosinf_read(f, q, sizeof(q), path)) return false;
        s.frames = (int)q[0]; s.facings = (int)q[1]; s.stages = (int)q[2];
        s.fw = (int)q[3]; s.fh = (int)q[4]; s.cols = (int)q[5];
        s.texw = (int)q[6]; s.texh = (int)q[7];
        s.src_stages = (int)q[8];
        if (s.src_stages < s.stages) s.src_stages = s.stages;
        idx.resize((size_t)s.texw * s.texh);
        if (!dosinf_read(f, &idx[0], idx.size(), path)) return false;

        /* 8-bit palette indices -> RGBA once, at load. Index 0 transparent, index 4
           the DOS ghost shadow: both alpha 0 (see the header comment). */
        rgba.resize(idx.size() * 4);
        for (size_t p = 0; p < idx.size(); p++) {
            const unsigned v = idx[p];
            unsigned char* o = &rgba[p * 4];
            if (v == 0 || v == 4) {
                o[0] = o[1] = o[2] = o[3] = 0;
            } else {
                o[0] = pal8[v * 3];
                o[1] = pal8[v * 3 + 1];
                o[2] = pal8[v * 3 + 2];
                o[3] = 255;
            }
        }
        /* Bleed the artwork's colour into the transparent texels before it goes
           up, so bilinear has something to average that is not a key colour.
           No-op for the nearest path. See fx_bleed_rgba. */
        fx_bleed_rgba(&rgba[0], s.texw, s.texh, 4);
        glGenTextures(1, &s.gl);
        glBindTexture(GL_TEXTURE_2D, s.gl);
        /* NEAREST, ALWAYS, AND NOT REGISTERED WITH THE BILINEAR SWITCH: infantry
           sprites are deliberately left out of bilinear filtering.
           These are 1995 DOS SHP frames -- a rifleman is about twenty pixels
           tall -- and smoothing them does to a man what it would do to the sidebar:
           there is not enough art there to interpolate, so it reads as a smear rather
           than as detail. That is the same judgement the sidebar, the cameos, the fonts,
           the cursors and the menus already got.

           The exclusion is BY OMISSION, which is fx_filter.h's own design: a texture
           that is never handed to fx_filter_note is not in the register, and the switch
           cannot reach it however it is flipped. There is no "is this a sprite?"
           predicate for a later change to get wrong.

           fx_bleed_rgba above stays. It is a no-op for the nearest path and costs
           nothing, and leaving it means the decision here is one line to revisit rather
           than two. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        /* GL_CLAMP, not GL_CLAMP_TO_EDGE: the Win98/Voodoo2 GL debt ledger (see the
           wrap_mode comment in cnc_eyes.cpp) is a closed set of four audited call
           sites and this loader must not grow it. With GL_NEAREST sampling and
           frame rects laid out on a grid the two are pixel-identical here: no
           sample point ever reaches the texture border, so the border colour that
           GL_CLAMP could theoretically expose never participates. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.texw, s.texh, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
    }

    for (uint32_t i = 0; i < ntype; i++) {
        char ini[9] = {0};
        if (!dosinf_read(f, ini, 8, path)) return false;
        DosInfType t;
        for (int r = 0; r < 2; r++)
            for (int a = 0; a < DA_COUNT; a++) {
                int32_t v;
                if (!dosinf_read(f, &v, 4, path)) return false;
                if (v >= (int32_t)nstrip) {
                    fprintf(stderr, "dosinf: %s slot %d/%d out of range\n", ini, r, a);
                    fclose(f);
                    return false;
                }
                t.strip[r][a] = (int)v;
            }
        g_dosInfTypes[ini] = t;
    }
    fclose(f);
    fprintf(stderr, "dosinf %s: %u strips, %u infantry types -- DOS infantry ON\n",
            path, nstrip, ntype);
    g_dosinfOn = true;
    return true;
}

/* Undo dosinf_load. One texture per strip was uploaded, so one glDeleteTextures per
   strip goes back. Needs the SAME live GL context the upload happened on: the app
   shell keeps exactly one, for exactly this reason. */
static void dosinf_free(void)
{
    for (size_t i = 0; i < g_dosStrips.size(); i++)
        if (g_dosStrips[i].gl)
            glDeleteTextures(1, &g_dosStrips[i].gl);
    g_dosStrips.clear();
    g_dosInfTypes.clear();
    g_dosinfOn = false;
}

#endif /* DOSINF_MOD_H */
