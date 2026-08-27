/* ================================================================================== *
 *  dosmake_mod.h -- the 1995 MS-DOS construction animations (dosmake.pack, "DOSMAKE1")
 *
 *  The ENGINE has run the construction state machine since the 21-entry CONQUER.MIX
 *  shipped (it is why selling works); this module is the missing ART pass. While a
 *  building's BState is BSTATE_CONSTRUCTION the renderer suppresses its mesh and
 *  draws the 1995 scaffold instead, on the building's own footprint.
 *
 *  Frame selection is BuildingClass::Draw_It (tiberiandawn/building.cpp:500):
 *      frame = Fetch_Stage()                    -- exported per tick as OBJ dostage
 *      if Mission == MISSION_DECONSTRUCTION:    -- selling: play it backwards
 *          frame = count - 1 - frame
 *  The count used for the reversal is the ENGINE's own (OBJ makecnt, from
 *  Anims[BSTATE_CONSTRUCTION].Count), clamped into the pack's frames so a pack that
 *  disagrees with the engine can garble a frame but never read out of bounds.
 *
 *  House colours were applied at bake time (house.cpp:2186: Nod BUILDINGS remap
 *  through RemapRed in single player; everyone else identity). Index 0 is
 *  transparent and index 4 the DOS shadow ghost; both become alpha 0.
 *
 *  Strips bigger than one Voodoo 2 texture are PAGED across several 256x256 sheets
 *  (frame f -> sheet f / per_sheet), not subsampled: a 5-second one-shot can afford
 *  the extra binds, and TMPL's 36 frames stay 36 frames.
 *
 *  OpenGL 1.1 only: glGenTextures/glTexImage2D/glBegin quads, no extensions.
 * ================================================================================== */

#ifndef DOSMAKE_MOD_H
#define DOSMAKE_MOD_H

#define DOSMAKE_MAX_SHEETS 8

struct DosMakeStrip {
    char name[17];
    int frames, fw, fh, cols, rows, texw, texh, nsheets;
    GLuint gl[DOSMAKE_MAX_SHEETS];
};

struct DosMakeType { int strip[2]; };  /* rows: 0 gold/identity, 1 RemapRed (Nod) */

static std::vector<DosMakeStrip> g_dosMakeStrips;
static std::map<std::string, DosMakeType> g_dosMakeTypes;
static bool g_dosmakeOn = false;

/* defines.h:528: BSTATE_CONSTRUCTION == 0. The OBJ dump exports a building's BState
   in the `doing` field (a building has no DoType; the field is reused, same idea). */
#define DOSMAKE_BSTATE_CONSTRUCTION 0

static bool dosmake_read(FILE* f, void* p, size_t n, const char* path)
{
    if (fread(p, 1, n, f) == n) return true;
    fprintf(stderr, "dosmake: %s ends early at byte %ld -- truncated?\n", path, ftell(f));
    fclose(f);
    return false;
}

static bool dosmake_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "dosmake: cannot open %s -- buildings will pop in finished, "
                        "as before\n", path);
        return false;
    }
    char magic[8];
    if (!dosmake_read(f, magic, 8, path)) return false;
    if (memcmp(magic, "DOSMAKE1", 8) != 0) {
        fprintf(stderr, "dosmake: bad magic in %s (want DOSMAKE1 -- re-run "
                        "bake_dosmake.py)\n", path);
        fclose(f);
        return false;
    }
    uint32_t ver = 0, nstrip = 0, ntype = 0;
    if (!dosmake_read(f, &ver, 4, path)) return false;
    if (!dosmake_read(f, &nstrip, 4, path)) return false;
    if (!dosmake_read(f, &ntype, 4, path)) return false;
    if (ver != 1) {
        fprintf(stderr, "dosmake: %s is pack version %u, this build wants 1 -- "
                        "rebake; buildings pop in finished\n", path, ver);
        fclose(f);
        return false;
    }
    unsigned char pal6[768], pal8[768];
    if (!dosmake_read(f, pal6, 768, path)) return false;
    if (!dosmake_read(f, pal8, 768, path)) return false;

    g_dosMakeStrips.resize(nstrip);
    std::vector<unsigned char> idx, rgba;
    for (uint32_t i = 0; i < nstrip; i++) {
        DosMakeStrip& s = g_dosMakeStrips[i];
        memset(&s, 0, sizeof(s));
        if (!dosmake_read(f, s.name, 16, path)) return false;
        uint32_t q[8];
        if (!dosmake_read(f, q, sizeof(q), path)) return false;
        s.frames = (int)q[0]; s.fw = (int)q[1]; s.fh = (int)q[2];
        s.cols = (int)q[3]; s.rows = (int)q[4];
        s.texw = (int)q[5]; s.texh = (int)q[6]; s.nsheets = (int)q[7];
        if (s.nsheets > DOSMAKE_MAX_SHEETS) {
            fprintf(stderr, "dosmake: %s wants %d sheets (limit %d)\n",
                    s.name, s.nsheets, DOSMAKE_MAX_SHEETS);
            fclose(f);
            return false;
        }
        idx.resize((size_t)s.texw * s.texh);
        rgba.resize(idx.size() * 4);
        for (int sh = 0; sh < s.nsheets; sh++) {
            if (!dosmake_read(f, &idx[0], idx.size(), path)) return false;
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
            glGenTextures(1, &s.gl[sh]);
            glBindTexture(GL_TEXTURE_2D, s.gl[sh]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fx_filter_mode(GL_NEAREST));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fx_filter_mode(GL_NEAREST));
            fx_filter_note(s.gl[sh], GL_NEAREST);   /* the F5 bilinear toggle reaches it */
            /* GL_CLAMP on purpose: same audited-ledger reasoning as dosinf_mod.h --
               grid layout + GL_NEAREST means no sample ever reaches the border. */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.texw, s.texh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
        }
    }

    for (uint32_t i = 0; i < ntype; i++) {
        char ini[9] = {0};
        if (!dosmake_read(f, ini, 8, path)) return false;
        DosMakeType t;
        for (int r = 0; r < 2; r++) {
            int32_t v;
            if (!dosmake_read(f, &v, 4, path)) return false;
            if (v >= (int32_t)nstrip) {
                fprintf(stderr, "dosmake: %s row %d out of range\n", ini, r);
                fclose(f);
                return false;
            }
            t.strip[r] = (int)v;
        }
        g_dosMakeTypes[ini] = t;
    }
    fclose(f);
    fprintf(stderr, "dosmake %s: %u strips, %u building types -- "
                    "1995 construction animation ON\n", path, nstrip, ntype);
    g_dosmakeOn = true;
    return true;
}

static void dosmake_free(void)
{
    for (size_t i = 0; i < g_dosMakeStrips.size(); i++)
        for (int s = 0; s < g_dosMakeStrips[i].nsheets; s++)
            if (g_dosMakeStrips[i].gl[s])
                glDeleteTextures(1, &g_dosMakeStrips[i].gl[s]);
    g_dosMakeStrips.clear();
    g_dosMakeTypes.clear();
    g_dosmakeOn = false;
}

#endif /* DOSMAKE_MOD_H */
