/* ================================================================================== *
 *  dostib_mod.h -- the REAL tiberium overlay art, from dostib.pack.
 *
 *  The pack is bake_dostiberium.py's output: the cartridge's own ANY_TI01..12
 *  filmstrips (12 patch types x 12 growth frames of 24x24, one theaterless set --
 *  the "ANY_" prefix is the cartridge's own statement) packed onto RGBA sheets.
 *  The engine side is already truthful: the brain's TIB| lines carry kind (which
 *  of the twelve overlays) and stage (OverlayData, which cell.cpp:1041 uses as the
 *  SHP frame index VERBATIM), so drawing is a straight lookup:
 *
 *      slot = kind * frames + stage    ->  (sheet, x0, y0)
 *
 *  RENDER RULES (Voodoo2-safe): GL 1.1 immediate mode, one ground quad per cell a
 *  hair above the terrain (y = 0.010, same idea as the shroud's layering), alpha
 *  test rather than blending (the baked alpha is a hard 0/255 cutout), depth WRITES
 *  off so the flat decal never occludes anything, depth TEST on so hills still hide
 *  distant fields. Shroud culling is the caller's cell_shown, same as every other
 *  cell-space draw.
 *
 *  A missing or malformed pack is announced loudly ONCE and the caller falls back
 *  to the old procedural crystals -- absent art must never look like working art.
 * ================================================================================== */

#ifndef CNC3D_DOSTIB_MOD_H
#define CNC3D_DOSTIB_MOD_H

struct DostibSlot { unsigned short sheet, x0, y0; };

static std::vector<GLuint>     g_dostibTex;      /* one GL texture per sheet */
static std::vector<DostibSlot> g_dostibSlot;     /* types * frames entries   */
static int  g_dostibTexW = 0, g_dostibTexH = 0;
static int  g_dostibFW = 24, g_dostibFH = 24;
static int  g_dostibTypes = 0, g_dostibFrames = 0;
static bool g_dostibHave = false;

/* Call with a live GL context (it uploads textures). Only set 0 is read: the N64
   bake carries exactly one set ("ANY"); a DOS-theater bake's first set is a valid
   stand-in until per-theater selection is wanted. */
static bool dostib_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "tiberium: no art pack at %s (run bake_dostiberium.py); "
                        "drawing procedural crystals\n", path);
        return false;
    }
    char magic[8];
    unsigned ver = 0, source = 0, sets = 0, fw = 0, fh = 0, types = 0, frames = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "DOSTIB1", 7) != 0) {
        fprintf(stderr, "tiberium: %s is not a dostib pack\n", path);
        fclose(f);
        return false;
    }
    if (fread(&ver, 4, 1, f) != 1 || ver != 1 ||
        fread(&source, 4, 1, f) != 1 || fread(&sets, 4, 1, f) != 1 ||
        fread(&fw, 4, 1, f) != 1 || fread(&fh, 4, 1, f) != 1 ||
        fread(&types, 4, 1, f) != 1 || fread(&frames, 4, 1, f) != 1 ||
        sets < 1 || types < 1 || frames < 1 || types * frames > 4096) {
        fprintf(stderr, "tiberium: %s header unreadable\n", path);
        fclose(f);
        return false;
    }
    char setname[13]; memset(setname, 0, sizeof setname);
    unsigned sheets = 0, texw = 0, texh = 0, cols = 0, rows = 0;
    if (fread(setname, 1, 12, f) != 12 ||
        fread(&sheets, 4, 1, f) != 1 || fread(&texw, 4, 1, f) != 1 ||
        fread(&texh, 4, 1, f) != 1 || fread(&cols, 4, 1, f) != 1 ||
        fread(&rows, 4, 1, f) != 1 ||
        sheets < 1 || sheets > 16 || texw < 1 || texw > 1024 || texh < 1 || texh > 1024) {
        fprintf(stderr, "tiberium: %s set header unreadable\n", path);
        fclose(f);
        return false;
    }
    g_dostibSlot.resize((size_t)types * frames);
    for (size_t i = 0; i < g_dostibSlot.size(); i++) {
        unsigned short d[3];
        if (fread(d, 2, 3, f) != 3) {
            fprintf(stderr, "tiberium: %s slot table truncated\n", path);
            fclose(f);
            g_dostibSlot.clear();
            return false;
        }
        g_dostibSlot[i].sheet = d[0];
        g_dostibSlot[i].x0 = d[1];
        g_dostibSlot[i].y0 = d[2];
    }
    std::vector<unsigned char> px((size_t)texw * texh * 4);
    for (unsigned s = 0; s < sheets; s++) {
        if (fread(&px[0], 1, px.size(), f) != px.size()) {
            fprintf(stderr, "tiberium: %s sheet %u truncated\n", path, s);
            fclose(f);
            for (size_t k = 0; k < g_dostibTex.size(); k++)
                glDeleteTextures(1, &g_dostibTex[k]);
            g_dostibTex.clear();
            g_dostibSlot.clear();
            return false;
        }
        /* Bleed the artwork's colour into the transparent texels before it goes
           up, so bilinear has something to average that is not a key colour.
           No-op for the nearest path. See fx_bleed_rgba. */
        fx_bleed_rgba(&px[0], (int)texw, (int)texh, 4);
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        /* GL_NEAREST, and NOT through fx_filter_mode: tiberium is the one ground
           texture the bilinear switch must never reach, in either visual mode.
           the project owner, 26 Aug 2026: "In enhanced mode and classic mode, bilinear filtering
           should not be applied to tiberium."
           WHY IT IS WORSE HERE than on the terrain it sits on. A tiberium cell is an
           alpha-tested CUTOUT, not an opaque tile: the field's shape comes out of the
           alpha channel at a hard 0.5 threshold. Filtering the alpha ramps it across
           the boundary texels, so the threshold lands somewhere different on every
           edge pixel and the crystal outlines crawl and shimmer as the camera moves.
           The terrain underneath has no such edge and is only softened by it.
           Note it is NOT handed to fx_filter_note either: the register is what the F5
           switch walks, so staying out of it is what makes this stick rather than
           being re-applied on the next toggle (fx_filter.h says so in as many words). */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (int)texw, (int)texh, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
        g_dostibTex.push_back(id);
    }
    fclose(f);
    g_dostibTexW = (int)texw; g_dostibTexH = (int)texh;
    g_dostibFW = (int)fw;     g_dostibFH = (int)fh;
    g_dostibTypes = (int)types; g_dostibFrames = (int)frames;
    g_dostibHave = true;
    fprintf(stderr, "tiberium: %s: set %s, %u sheets %ux%u, %u types x %u frames "
                    "(the cartridge's own ANY_TI art)\n",
            path, setname, sheets, texw, texh, types, frames);
    return true;
}

static void dostib_free(void)
{
    for (size_t i = 0; i < g_dostibTex.size(); i++)
        if (g_dostibTex[i])
            glDeleteTextures(1, &g_dostibTex[i]);
    g_dostibTex.clear();
    g_dostibSlot.clear();
    g_dostibHave = false;
}

#endif /* CNC3D_DOSTIB_MOD_H */
