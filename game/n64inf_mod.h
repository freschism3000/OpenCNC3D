/* ================================================================================== *
 *  n64inf_mod.h -- the cartridge's infantry COMBAT strips (n64infantry.pack, "N64INF01")
 *
 *  The cartridge infantry art (FX_INF_N64) draws from the scenario pack's strips, and a
 *  scenario pack carries three per set: stand, run and the first death. A soldier who
 *  fired, lay down or crawled therefore stayed on his feet in his standing frame, and
 *  since the rifle's muzzle flash is painted into the firing frames rather than being an
 *  effect of its own, a firefight drawn with the cartridge art showed nobody shooting.
 *
 *  This pack holds the rest of the cartridge's combat art: SHOOT, CSHOT (prone fire),
 *  CRAWL, and PANIC for the two doctors. game/bake_n64infantry.py reads which strip the
 *  cartridge draws for which engine Do out of the cartridge's own table (RAM 0x802102C4,
 *  read by the sprite accessor at RAM 0x801FB600), so the slot below is chosen the way
 *  the console chose it, and a slot the console draws standing is -1 here: the draw keeps
 *  its own stand/run pick for it.
 *
 *  The strips are laid out exactly as the scenario pack's own (facing-major, the five
 *  stored western facings, frames stacked down the texture), so draw_sprite's facing
 *  and mirror rules apply to them unchanged.
 *
 *  OpenGL 1.1 only: glGenTextures/glTexImage2D, no extensions.
 * ================================================================================== */

#ifndef N64INF_MOD_H
#define N64INF_MOD_H

/* The four combat Dos, in pack order. */
enum N64InfSlot { N64A_PRONE = 0, N64A_FIRE, N64A_CRAWL, N64A_FIRE_PRONE, N64A_COUNT };

/* The engine's DoType (defines.h: 2 PRONE, 4 FIRE_WEAPON, 6 CRAWL, 8 FIRE_PRONE) to a
   slot, -1 for every Do the scenario pack's strips already answer. */
static int n64inf_slot(int doing)
{
    switch (doing) {
    case 2: return N64A_PRONE;
    case 4: return N64A_FIRE;
    case 6: return N64A_CRAWL;
    case 8: return N64A_FIRE_PRONE;
    default: return -1;
    }
}

struct N64InfStrip { char name[17]; int frames, facings, fw, fh, texw, texh; GLuint gl;
                     GLuint gl_livery[7] = {};
                     std::vector<unsigned char> rgba; };
/* lift: the cartridge's own anchor byte for the slot, read with it from the ROM (RAM
   0x80212250): the console lowers the card's ground point by lift/2 and brings it lift
   nearer the camera, in a world of 256 units to the cell, which is how a frame with
   empty rows under the body still lies on the ground. 0 for a -1 slot. */
struct N64InfType { int strip[2][N64A_COUNT]; int lift[N64A_COUNT]; };   /* rows: 0 GDI, 1 Nod */

static std::vector<N64InfStrip> g_n64Strips;
static std::map<std::string, N64InfType> g_n64InfTypes;

static bool n64inf_read(FILE* f, void* dst, size_t n, const char* path)
{
    if (fread(dst, 1, n, f) == n) return true;
    fprintf(stderr, "n64inf: %s is short\n", path);
    fclose(f);
    return false;
}

/* Undo n64inf_load, on the same live GL context the upload used. */
static void n64inf_free(void)
{
    for (size_t i = 0; i < g_n64Strips.size(); i++) {
        for (int c = 0; c < 7; c++)
            if (g_n64Strips[i].gl_livery[c]) {
                fx_filter_forget(g_n64Strips[i].gl_livery[c]);
                glDeleteTextures(1, &g_n64Strips[i].gl_livery[c]);
            }
        if (g_n64Strips[i].gl) {
            fx_filter_forget(g_n64Strips[i].gl);
            glDeleteTextures(1, &g_n64Strips[i].gl);
        }
    }
    g_n64Strips.clear();
    g_n64InfTypes.clear();
}

/* A missing or unreadable pack is one line on stderr, and the cartridge soldiers stand
   while they fight, which is what they did before this pack existed. */
static bool n64inf_load(const char* path)
{
    n64inf_free();          /* a second boot without a teardown must not orphan the first */
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "n64inf: cannot open %s -- the cartridge infantry will not "
                        "show firing, prone or crawling\n", path);
        return false;
    }
    char magic[8];
    uint32_t hdr[3];
    if (!n64inf_read(f, magic, 8, path) || !n64inf_read(f, hdr, sizeof hdr, path))
        return false;
    if (memcmp(magic, "N64INF01", 8) != 0 || hdr[0] != 1) {
        fprintf(stderr, "n64inf: %s is not a version 1 N64INF01 pack -- rebake with "
                        "game/bake_n64infantry.py\n", path);
        fclose(f);
        return false;
    }
    const uint32_t nstrip = hdr[1], ntype = hdr[2];
    g_n64Strips.resize(nstrip);
    std::vector<unsigned char> rgba;
    for (uint32_t i = 0; i < nstrip; i++) {
        N64InfStrip& s = g_n64Strips[i];
        memset(s.name, 0, sizeof s.name);
        uint32_t q[6];
        if (!n64inf_read(f, s.name, 16, path) || !n64inf_read(f, q, sizeof q, path))
            return false;
        s.frames = (int)q[0]; s.facings = (int)q[1]; s.fw = (int)q[2]; s.fh = (int)q[3];
        s.texw = (int)q[4]; s.texh = (int)q[5];
        rgba.resize((size_t)s.texw * s.texh * 4);
        if (!n64inf_read(f, &rgba[0], rgba.size(), path)) return false;
        s.rgba = rgba;  /* authored paired decode, before alpha bleed */
        /* Uploaded exactly as load_pack uploads the scenario pack's own strips, so the
           two sets of cartridge frames filter and bleed alike under the F5 switch. */
        fx_bleed_rgba(&rgba[0], s.texw, s.texh, 4);
        glGenTextures(1, &s.gl);
        glBindTexture(GL_TEXTURE_2D, s.gl);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fx_filter_mode(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fx_filter_mode(GL_NEAREST));
        fx_filter_note(s.gl, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.texw, s.texh, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
    }
    for (uint32_t i = 0; i < ntype; i++) {
        char code[9] = {0};
        N64InfType t;
        if (!n64inf_read(f, code, 8, path) || !n64inf_read(f, t.strip, sizeof t.strip, path)
            || !n64inf_read(f, t.lift, sizeof t.lift, path))
            return false;
        for (int r = 0; r < 2; r++)
            for (int a = 0; a < N64A_COUNT; a++)
                if (t.strip[r][a] >= (int)nstrip) {
                    fprintf(stderr, "n64inf: %s slot %d/%d out of range\n", code, r, a);
                    fclose(f);
                    return false;
                }
        g_n64InfTypes[code] = t;
    }
    fclose(f);
    fprintf(stderr, "n64inf %s: %u strips, %u infantry types\n", path, nstrip, ntype);
    return true;
}

/* The cartridge's strip for what the engine has this man doing, or NULL to keep the
   stand/run pick, and its anchor byte in *lift. `row` is sprite_house's, the same row the
   scenario pack is read by. */
static const N64InfStrip* n64inf_strip(const char* type, int row, int doing, int* lift)
{
    *lift = 0;
    const int slot = n64inf_slot(doing);
    if (slot < 0 || row < 0 || row > 1) return NULL;
    std::map<std::string, N64InfType>::const_iterator it = g_n64InfTypes.find(type);
    if (it == g_n64InfTypes.end()) return NULL;
    const int si = it->second.strip[row][slot];
    if (si < 0 || si >= (int)g_n64Strips.size()) return NULL;
    *lift = it->second.lift[slot];
    return &g_n64Strips[si];
}

#endif /* N64INF_MOD_H */
