/* ================================================================================== *
 *  doscrate_mod.h -- the DOS world overlays, from doscrate.pack: the two bonus crates
 *  and the repair wrench.
 *
 *  The pack is game/bake_doscrate.py's output: WCRATE.SHP, SCRATE.SHP and SELECT.SHP
 *  frame 2 out of the shipped CONQUER.MIX, coloured through the palette that already
 *  sits inside dossidebar.pack, so the bake needs no 1995 CD mounted. The crates are one
 *  10x11 frame each; the wrench is 24x21, exactly one DOS cell wide.
 *
 *  TWO OR THREE ENTRIES, and the difference is not an error. The wrench was appended to
 *  a pack that had shipped with two, so a pack baked before it still loads and still
 *  draws its crates. What must not happen is the wrench being drawn from a slot that was
 *  never filled, which is what g_doswrenchHave is for.
 *
 *  WHY THERE IS A MODULE FOR TWO LITTLE SPRITES. Tiberian Dawn has had the whole crate
 *  feature all along -- MapClass::Place_Random_Crate scatters them when MPlayerGoodies is
 *  on, and cell.cpp's pickup gives money, a unit, a nuclear missile piece or a change of
 *  shroud. What was missing was the picture: the brain's dump carried tiberium and walls
 *  and nothing else, so a crate was an INVISIBLE pickup and a bonus arrived from nowhere.
 *  The brain's CRATE| line closes the first half of that and this closes the second.
 *
 *  RENDER RULES, the same ones dostib_mod.h lays down and for the same reasons
 *  (Voodoo2-safe, GL 1.1 immediate mode): one flat quad a hair above the terrain,
 *  alpha-tested cutout rather than blending because the baked alpha is a hard 0/255,
 *  depth WRITES off so a ground decal never occludes, depth TEST on so hills still hide
 *  distant ones, and the caller's cell_shown does the shroud culling.
 *
 *  A CRATE IS NOT A WHOLE CELL, which is the one way this differs from tiberium. The DOS
 *  cell is 24 pixels and the sprite is 10x11, so the quad is that fraction of a cell and
 *  is CENTRED in it. Drawing it cell-sized would put a crate the size of a Construction
 *  Yard pad on the ground.
 *
 *  A missing or malformed pack is announced loudly ONCE and nothing is drawn -- absent
 *  art must never look like working art, and here it would look like the crate feature
 *  being off rather than the art being missing.
 * ================================================================================== */

#ifndef CNC3D_DOSCRATE_MOD_H
#define CNC3D_DOSCRATE_MOD_H

/* The DOS cell, in pixels. The sprite's size over this is its size in cells. */
#define DOSCRATE_CELL_PX 24.0f

/* Slots 0 and 1 are the wire order the brain's CRATE| line ends in: 0 = wood,
   1 = steel. Slot 2 is the repair wrench, which carries no wire value at all. */
#define DOSCRATE_WRENCH 2
#define DOSCRATE_SLOTS  3

static GLuint g_doscrateTex[DOSCRATE_SLOTS];
static int    g_doscrateW[DOSCRATE_SLOTS], g_doscrateH[DOSCRATE_SLOTS];
static bool   g_doscrateHave = false;    /* the two crates are loaded            */
static bool   g_doswrenchHave = false;   /* slot 2 is loaded as well             */

static bool doscrate_load(const char* path)
{
    FILE* f = path && *path ? fopen(path, "rb") : NULL;
    char magic[8];
    unsigned int ver = 0, count = 0;
    int i;

    g_doscrateHave = false;
    if (!f) {
        fprintf(stderr, "crates: %s: not installed -- crates will be invisible "
                        "(python3 game/bake_doscrate.py makes it)\n",
                path ? path : "(none)");
        return false;
    }
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "DOSCRT1", 7) != 0) {
        fprintf(stderr, "crates: %s: bad magic; not a crate pack\n", path);
        fclose(f);
        return false;
    }
    if (fread(&ver, 4, 1, f) != 1 || ver != 1 || fread(&count, 4, 1, f) != 1
        || count < 2 || count > (unsigned int)DOSCRATE_SLOTS) {
        fprintf(stderr, "crates: %s: version %u with %u entries, expected 1 and 2 or 3\n",
                path, ver, count);
        fclose(f);
        return false;
    }
    /* The VERSION did not move with the wrench, deliberately: the record layout is
       unchanged and the count already describes the file, so a pack with two entries and
       a pack with three are both honest version 1. Bumping it would turn the one case
       this tolerance exists for -- an older pack -- into a refusal that takes the crates
       down with the wrench. */
    for (i = 0; i < (int)count; i++) {
        char name[12];
        unsigned int w = 0, h = 0;
        std::vector<unsigned char> px;
        if (fread(name, 1, 12, f) != 12 || fread(&w, 4, 1, f) != 1
            || fread(&h, 4, 1, f) != 1 || w == 0 || h == 0 || w > 64 || h > 64) {
            fprintf(stderr, "crates: %s: entry %d has a silly size %ux%u\n", path, i, w, h);
            fclose(f);
            return false;
        }
        px.resize((size_t)w * h * 4);
        if (fread(&px[0], 1, px.size(), f) != px.size()) {
            fprintf(stderr, "crates: %s: entry %d is truncated\n", path, i);
            fclose(f);
            return false;
        }
        glGenTextures(1, &g_doscrateTex[i]);
        glBindTexture(GL_TEXTURE_2D, g_doscrateTex[i]);
        /* NEAREST both ways: this is 1995 pixel art on a decal and any filtering fringes
           the cutout, which is the same call the tiberium and smudge passes make. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
        g_doscrateW[i] = (int)w;
        g_doscrateH[i] = (int)h;
    }
    fclose(f);
    g_doscrateHave = true;
    g_doswrenchHave = (count > (unsigned int)DOSCRATE_WRENCH);
    if (g_doswrenchHave)
        fprintf(stderr, "crates: %s: wood %dx%d, steel %dx%d, wrench %dx%d\n", path,
                g_doscrateW[0], g_doscrateH[0], g_doscrateW[1], g_doscrateH[1],
                g_doscrateW[DOSCRATE_WRENCH], g_doscrateH[DOSCRATE_WRENCH]);
    else
        fprintf(stderr, "crates: %s: wood %dx%d, steel %dx%d, NO WRENCH -- this pack was "
                        "baked before it existed, so a repairing building will show "
                        "nothing (python3 game/bake_doscrate.py re-makes it)\n", path,
                g_doscrateW[0], g_doscrateH[0], g_doscrateW[1], g_doscrateH[1]);
    return true;
}

static void doscrate_free(void)
{
    /* DOSCRATE_SLOTS names, not count: an unfilled slot is still zero here, and
       glDeleteTextures ignores a zero name, so this is correct for a two-entry pack. */
    if (g_doscrateHave)
        glDeleteTextures(DOSCRATE_SLOTS, g_doscrateTex);
    g_doscrateHave = false;
    g_doswrenchHave = false;
}

#endif /* CNC3D_DOSCRATE_MOD_H */
