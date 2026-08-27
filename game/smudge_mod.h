/* ================================================================================== *
 *  smudge_mod.h -- the cartridge's scorch marks, craters and building aprons.
 *
 *  The pack is bake_smudges.py's output: fifteen strips per theater, laid out as a
 *  fixed grid so no slot table is needed (column = frame, row = art index - 1).
 *
 *  THE ENGINE SIDE IS ALREADY TRUTHFUL, which is why this file is small. A smudge is
 *  per-CELL state (CellClass::Smudge / SmudgeData, cell.h:122) and the brain emits one
 *  SMUDGE|x|y|type|data line per such cell. `data` is the console's frame VERBATIM:
 *  SmudgeClass::Mark computes w + h * Class->Width for the multi-cell aprons and stacks
 *  craters by incrementing then clamping to 4, and the cartridge does the same
 *  arithmetic at ROM 0x16D824. So drawing is a lookup and nothing else.
 *
 *      art  = (type <= 11) ? type + 4 : type - 11
 *
 *  because the engine enumerates CRATER1..6, SCORCH1..6, BIB1..3 while the cartridge's
 *  terrain loader reads the bibs FIRST into slots 1..3 (terrainState+0xC068), then the
 *  craters, then the scorches.
 *
 *  RENDER RULES, all read out of the ROM rather than chosen:
 *
 *   - GEOMETRY: the console reuses the terrain cell's OWN four corner vertices, so the
 *     decal follows the hills exactly and needs no lift of its own.
 *   - DEPTH is ZMODE_DEC (rendermode low half 0x4E50): a hardware depth decal. GL's
 *     equivalent is glPolygonOffset, which is what this uses. There is deliberately NO
 *     invented Y bias; the console does not have one.
 *   - ALPHA: the cartridge sets G_AC_NONE (ROM 0x19CB70) and composites under FORCE_BL,
 *     i.e. it BLENDS rather than testing. We alpha-test at 0.5, which is EXACTLY
 *     equivalent here and only here, because the alpha in all thirty art files is a
 *     strict 1-bit key -- bake_smudges.py asserts that and refuses to bake anything
 *     else. It also costs Tier 1 nothing, where a blend would be one more state change
 *     per cell. Recorded as the swap it is.
 *   - COLOUR: texel * the terrain's own per-corner shade, no CM tint. The combiner is
 *     0xFC15FE2B/0xFFFFF3F9.
 *
 *  ONE DECAL PER CELL, and the cartridge's own priority (ROM 0x35E5C): a BIB beats
 *  everything, then an overlay (tiberium or a wall) beats a crater or a scorch. So a
 *  scorch under a tiberium field is not drawn -- the DOS original drew both, and
 *  following the console here is a decode, not a preference.
 *
 *  A missing pack is announced ONCE and nothing is drawn. Absent art must never look
 *  like working art.
 * ================================================================================== */

#ifndef CNC3D_SMUDGE_MOD_H
#define CNC3D_SMUDGE_MOD_H

struct SmudgeSet {
    unsigned    theaterId;
    int         texw, texh;
    GLuint      tex;
    int         nframes[16];
    /* The sheet as it came off disk, kept so the feathered alpha can be rebuilt when the
       dial moves. 192x360 RGBA per theater, so about 270 KB each: cheap against having to
       re-read the pack, and the alternative (feathering in place) would lose the original
       and make the dial one-way. */
    std::vector<unsigned char> raw;
};

static std::vector<SmudgeSet> g_smSet;
static int  g_smFW = 24, g_smFH = 24, g_smTypes = 0, g_smMax = 0;
static bool g_smHave = false;
static bool g_smOn = true;          /* --nosmudge is the A/B */

/* SOFTEN THE EDGE OF EVERY DECAL, by blurring the ALPHA channel only.
 *
 * Reported: ground decals cut hard against the terrain, and the edges should blend
 * into the ground below. They are hard because the
 * cartridge's art carries a strict 1-bit alpha key -- bake_smudges.py asserts that and
 * refuses to bake anything else -- and the renderer draws them with a 0.5 alpha TEST,
 * which is exactly equivalent to what the console does and costs Tier 1 nothing.
 *
 * So the softness has to be manufactured, and it is: a small box blur of the alpha, which
 * turns the 1-bit key into a ramp a few texels wide. The RGB is left alone because
 * fx_bleed_rgba has already pushed the artwork's colour outwards into the transparent
 * texels, so the new partially-transparent edge has real colour to show rather than a key.
 *
 * PER FRAME, NEVER ACROSS THE SHEET. The frames are packed in a grid, so a blur that ran
 * over the whole atlas would bleed each crater into its neighbour and every frame would
 * grow a ghost of the one beside it. The loop is bounded to one fw x fh cell at a time.
 *
 * TIER 1: this is a load-time CPU pass and one blend state, both of which the Voodoo 2 has.
 * The DEVIATION is not the technique, it is that the console draws these hard; `decal_soft`
 * at 0 restores that exactly, which is why the raw sheet is kept. */
static void smudge_feather(unsigned char* dst, const unsigned char* src,
                           int w, int h, int fw, int fh, float amount)
{
    memcpy(dst, src, (size_t)w * h * 4);
    if (amount <= 0.0f)
        return;                     /* 0 = the cartridge's own hard key, byte for byte */
    if (amount > 1.0f) amount = 1.0f;
    const int cols = (fw > 0) ? w / fw : 1;
    const int rows = (fh > 0) ? h / fh : 1;
    for (int cy = 0; cy < rows; cy++) {
        for (int cx = 0; cx < cols; cx++) {
            const int x0 = cx * fw, y0 = cy * fh;
            for (int y = 0; y < fh; y++) {
                for (int x = 0; x < fw; x++) {
                    int sum = 0, n = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            const int sx = x + dx, sy = y + dy;
                            if (sx < 0 || sx >= fw || sy < 0 || sy >= fh)
                                continue;   /* clamped to the CELL, not the sheet */
                            sum += src[(((size_t)(y0 + sy) * w) + (x0 + sx)) * 4 + 3];
                            n++;
                        }
                    }
                    if (!n) continue;
                    const size_t o = (((size_t)(y0 + y) * w) + (x0 + x)) * 4 + 3;
                    const float hard = (float)src[o];
                    const float soft = (float)sum / (float)n;
                    dst[o] = (unsigned char)(hard + (soft - hard) * amount + 0.5f);
                }
            }
        }
    }
}

/* Re-upload every theater's sheet when the dial has moved, and only then. The compare is
   one float per frame; the rebuild is 270 KB of box blur and happens when a
   slider is dragged, which is not a frame budget anybody is counting. */
static float g_smSoftApplied = -1.0f;

/* WHAT THE DECALS ARE ACTUALLY DRAWN WITH, as opposed to what the preset holds. Zero
   unless something pushes the dial in, and the only things that do are the Enhanced path
   the shipping binary boots through and the Visuals dialog -- exactly like the bilinear
   filter and the New HUD beside it.

   The reason is the same and it is load-bearing: a bare `cnc_eyes` run is a MEASURING
   INSTRUMENT and must keep the cartridge's own picture, or every pixel gate that has a
   scorch mark or a building apron in frame re-baselines the moment this default moves.
   G35 measures smudges directly. So the default lives in fx_defaults for the player and
   reaches the renderer only when a player's path applies it. */
static float g_smSoftLive = 0.0f;
static void  decal_set_soft(float v) { g_smSoftLive = (v < 0.0f) ? 0.0f : v; }

static void smudge_apply_soft(float amount)
{
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    if (amount == g_smSoftApplied)
        return;
    g_smSoftApplied = amount;
    std::vector<unsigned char> tmp;
    for (size_t i = 0; i < g_smSet.size(); i++) {
        SmudgeSet& s = g_smSet[i];
        if (s.raw.empty() || !s.tex)
            continue;
        tmp.resize(s.raw.size());
        smudge_feather(&tmp[0], &s.raw[0], s.texw, s.texh, g_smFW, g_smFH, amount);
        glBindTexture(GL_TEXTURE_2D, s.tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s.texw, s.texh,
                        GL_RGBA, GL_UNSIGNED_BYTE, &tmp[0]);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    fprintf(stderr, "smudges: edge softness %.2f applied to %d sheet(s)\n",
            amount, (int)g_smSet.size());
}

static bool smudge_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "smudges: no art pack at %s (run game/bake_smudges.py); "
                        "no scorch marks, craters or building aprons will be drawn\n",
                path);
        return false;
    }
    char magic[8];
    unsigned ver = 0, nth = 0, fw = 0, fh = 0, types = 0, maxf = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "SMUDGE1", 7) != 0
        || fread(&ver, 4, 1, f) != 1 || ver != 1
        || fread(&nth, 4, 1, f) != 1 || fread(&fw, 4, 1, f) != 1
        || fread(&fh, 4, 1, f) != 1 || fread(&types, 4, 1, f) != 1
        || fread(&maxf, 4, 1, f) != 1
        || nth < 1 || nth > 8 || types < 1 || types > 16 || maxf < 1 || maxf > 16) {
        fprintf(stderr, "smudges: %s is not a readable smudge pack\n", path);
        fclose(f);
        return false;
    }
    g_smFW = (int)fw; g_smFH = (int)fh; g_smTypes = (int)types; g_smMax = (int)maxf;
    for (unsigned t = 0; t < nth; t++) {
        char nm[13]; memset(nm, 0, sizeof nm);
        SmudgeSet s;
        memset(&s, 0, sizeof s);
        unsigned texw = 0, texh = 0;
        if (fread(nm, 1, 12, f) != 12 || fread(&s.theaterId, 4, 1, f) != 1
            || fread(&texw, 4, 1, f) != 1 || fread(&texh, 4, 1, f) != 1
            || texw < 1 || texw > 2048 || texh < 1 || texh > 2048) {
            fprintf(stderr, "smudges: %s theater %u header unreadable\n", path, t);
            fclose(f);
            return false;
        }
        s.texw = (int)texw; s.texh = (int)texh;
        for (int i = 0; i < g_smTypes; i++) {
            unsigned c = 0;
            if (fread(&c, 4, 1, f) != 1) {
                fprintf(stderr, "smudges: %s frame counts truncated\n", path);
                fclose(f);
                return false;
            }
            s.nframes[i] = (int)c;
        }
        std::vector<unsigned char> px((size_t)texw * texh * 4);
        if (fread(&px[0], 1, px.size(), f) != px.size()) {
            fprintf(stderr, "smudges: %s sheet for %s truncated\n", path, nm);
            fclose(f);
            return false;
        }
        /* Bleed the artwork's colour into the transparent texels before it goes
           up, so bilinear has something to average that is not a key colour.
           No-op for the nearest path. See fx_bleed_rgba. */
        fx_bleed_rgba(&px[0], s.texw, s.texh, 4);
        s.raw = px;                 /* kept so the feather dial can be moved either way */
        glGenTextures(1, &s.tex);
        glBindTexture(GL_TEXTURE_2D, s.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fx_filter_mode(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fx_filter_mode(GL_NEAREST));
        fx_filter_note(s.tex, GL_NEAREST);   /* the F5 bilinear toggle reaches it */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.texw, s.texh, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
        g_smSet.push_back(s);
        fprintf(stderr, "smudges: %s %dx%d, %d strips\n", nm, s.texw, s.texh, g_smTypes);
    }
    fclose(f);
    g_smHave = !g_smSet.empty();
    return g_smHave;
}

/* The set for this scenario's theater, or none. The cartridge loads smudge art for
   TEMPERATE (2) and DESERT (0) only, and draws none in the others -- that is its own
   answer and not a gap, so an unmatched theater draws nothing rather than borrowing. */
static const SmudgeSet* smudge_set_for(int theater)
{
    for (size_t i = 0; i < g_smSet.size(); i++)
        if ((int)g_smSet[i].theaterId == theater)
            return &g_smSet[i];
    return NULL;
}

#endif /* CNC3D_SMUDGE_MOD_H */
