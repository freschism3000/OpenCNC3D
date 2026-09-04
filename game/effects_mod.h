/* ================================================================================== *
 *  effects_mod.h -- CNC3D combat presentation: anims (muzzle flashes, impacts,
 *  explosions) and bullets in flight.
 *
 *  WHERE THE DATA COMES FROM
 *    The engine's Anims heap IS its effect layer. TechnoClass::Fire_At spawns the
 *    muzzle anim its weapon declares (const.cpp Weapons[]: ANIM_GUN_N for machine
 *    guns, ANIM_MUZZLE_FLASH for cannon), BulletClass spawns its impact anim on
 *    detonation (ANIM_PIFF for small arms), and UnitClass::Take_Damage spawns the
 *    death explosion the unit type declares (ANIM_FRAG1 / ANIM_FBALL1, udata.cpp).
 *    The patched CNC3D_Dump_Objects walks Anims and Bullets and prints one
 *    EFX|ANIM / EFX|BULLET line each; this module parses and draws them.
 *
 *  ART STATUS -- THE CARTRIDGE'S REAL EFFECT ART (round r030)
 *    The N64 game draws combat effects with a particle engine (../src/n64particle.c
 *    per the ROM's own debug strings). Its ten systems (gGameParticleSystems, BSS
 *    0x800EC1B0, name registry at ROM 0x0A2BD4) each own a STATIC texture sequence
 *    in the resident segment; the system init at RAM 0x80056944 hands out image
 *    pointer + format + size + frame count, which is where bake_efx.py reads them
 *    from. None of this art is in the cartridge file system; the FS really does
 *    contain no effect sprites (NEXPLO* is the nuke cutscene, EFFECTS* the sound
 *    menu). This module now draws those sequences as textured billboards, mapped
 *    per engine anim name following the N64's own anim->particle-source recipe
 *    table in n64specialeffect.c (66 entries at CodeOverlay ROM 0x1B6444).
 *
 *    Anything with no N64 counterpart stays a procedural placeholder and is listed
 *    (EFXART lines in efxdump; stderr note at startup). Two deliberate cases:
 *      - CHEM-* spray: the N64 anim table has no chem entries -> procedural.
 *      - GUNFIRE (= ANIM_MUZZLE_FLASH): the N64 recipe is EMPTY by design (the
 *        cartridge shows no cannon muzzle flash) -> we draw nothing, and say so.
 *    Bullets in flight draw as the cartridge's own tiny 3D models (bullet table
 *    ROM 0x1693B0: 120MM/DRAGON/MISSILE/BOMBLET -> model slots 92..95, -1 for the
 *    never-drawn 50CAL/FLAME); the mesh side lives in cnc_eyes.cpp behind the two
 *    forward-declared hooks below, with the old procedural dart as loud fallback.
 *
 *  RENDER RULES (Voodoo2-safe)
 *    GL 1.1 immediate mode only: glBegin quads, glBlendFunc, glBindTexture with
 *    power-of-two textures, GL_CLAMP (the 1.1 token, not CLAMP_TO_EDGE), no
 *    extensions. Additive blending (GL_SRC_ALPHA, GL_ONE) is plain 1.1 and maps to
 *    GR_BLEND_ONE on Glide. Depth test on, depth write off, so effects overlay
 *    units but still sit behind hills.
 * ================================================================================== */

#ifndef CNC3D_EFFECTS_MOD_H
#define CNC3D_EFFECTS_MOD_H

#include <set>

/* The cartridge's own recipe tables, transcribed from data/rom/cnc_eu.z64 by
   game/gen_efx_recipes.py. Three tables: EFX_SRC[65] (the emitter sources),
   EFX_RECIPE[66] (which sources each anim instantiates and where), and
   EFX_ANIM2RECIPE[85] (the engine's AnimType -> the cartridge's table index). */
#include "efx_recipes.h"

struct EfxAnim {
    int  id, type, stage, start, stages, loops, size, ground, attref, attkind, owner;
    /* AnimTypeClass::Biggest, the frame at which the anim is at its largest (adata.cpp),
       exported by the brain on the EFX|ANIM line. It is the only stage count we have for
       an ART-LESS anim, whose Stages resolves through Get_Build_Frame_Count(NULL) to 0.
       See the ghost clock at EfxGhost below. 0 when the brain does not export it. */
    int  biggest;
    /* Whose wreckage this anim's chunks are. -2 = not yet resolved; resolved ONCE at
       the first chunk burst and reused, because the stamp the dead vehicle left on its
       cell ages out while the anim is still bursting. */
    int  chunkHouse;
    char name[16];
    float wx, wz;                /* world cells, straight from the lepton coord */
};

struct EfxBullet {
    int  id, type, face, alt, invis, arcing, homing;
    char name[16];
    float wx, wz;
};

static std::vector<EfxAnim>   g_efxAnims;
static std::vector<EfxBullet> g_efxBullets;

/* The mesh side of bullet drawing, implemented in cnc_eyes.cpp AFTER draw_mesh and
   g_pack exist (this header is included before either). Returns the pack mesh index
   for a bullet name, or -1; the draw wraps draw_mesh with the bullet's altitude and
   restores the effects pass's blend/depth state afterwards. */
static int efx_bullet_model(const char* name);
static void efx_bullet_draw_model(int mi, float wx, float ylift, float wz, int face);

/* THE NUCLEAR STRIKE's mushroom, same split and the same reason: the mesh side needs
   draw_mesh and g_pack, which this header is included before. */
static int  efx_nuke_model(void);
static bool efx_nuke_draw_model(int mi, float wx, float wz, float frame);
/* Must match bake5.py's ION_NSLOT. The snapshot grid covers the strike's transforms
   (finished by clip tick 1600) plus the rings' texture dissolve, at 160 ticks a slot, so
   0..14. The renderer's clamp is DERIVED from this rather than written as a literal,
   which is what let a table of ten and a clamp of nine drift apart in the first cut.
   Defined here rather than in cnc_eyes.cpp because this header is included first. */
#define ION_SNAPSHOTS 15

static int  efx_ion_model(int snapshot);
static void efx_ion_draw_model(int mi, float wx, float wz);
/* THE DEBRIS CHUNKS. Resolved from the pack's type table (DBRV0..6 / DBRS0..6) and drawn
   as real meshes with a free 3-axis tumble, which is what the console does and which a
   billboard cannot express. Implemented in cnc_eyes.cpp beside the bullet model draw,
   because only that file has draw_mesh and the pack. Returns -1 when the pack predates
   the chunk bake, and the caller then keeps the ember sprite. */
static int  efx_chunk_model(int family, int idx);
static void efx_chunk_draw_model(int mi, float wx, float wy, float wz,
                                 float degX, float degY, float degZ,
                                 float scale, int house);
static std::map<std::string, int> g_efxBulletMiss;

/* Cumulative evidence for the harness: how many distinct anim spawns of each name this
   run has seen. A spawn is a (name, heap id) pair that was not live on the previous
   dump -- heap ids are stable for an anim's lifetime, so re-seeing id 3 on consecutive
   dumps is one anim, but id 3 vanishing and returning is a new one. */
static std::map<std::string, int>           g_efxSpawns;
static std::map<std::string, std::set<int> > g_efxLivePrev, g_efxLiveNow;
static int g_efxDeathsSeen = 0;                 /* infantry ever seen with dying=1 */
static std::set<int> g_efxDeathIds;

/* The men dying THIS refresh, for efx_dump: id, world cell, death-Do stage. */
/* house is in the RENDERER'S convention: 1 = GDI (binds the gl_gdi sand table),
   0 = Nod/neutral. Recorded so a death's debris chunks can take the DYING OBJECT'S
   palette -- the anim itself cannot supply it (Anim::OwnerHouse is -1 for death
   anims, measured; see the chunk-house note in efx_emit_chunks). */
struct EfxDying { int id; float wx, wz; int stage; int house; };
static std::vector<EfxDying> g_efxDying;
static bool g_efxAnnounced = false;
static std::map<std::string, int> g_efxUnknown; /* names with no placeholder mapping */

/* ASSEMBLY ADDITION (shroud x effects): optional shroud predicate, set by the host
   after shroud_init. When set, effects standing in shroud-hidden cells are not DRAWN
   (the module predates the shroud pass and would otherwise flash above the black).
   efx_dump still reports them: the dump is engine truth, the draw is the view. */
static int (*g_efxCellHidden)(int cellx, int celly) = 0;

/* THE BUILDING EXPLOSION. Reported: "when a building explodes on the N64 it explodes into
   small pieces; that doesn't happen on the PC version." Our build draws nothing at all,
   and the reason is a mismatch between the two engines rather than a missing effect.

   The DOS engine we run spawns ANIM_FBALL1 once per occupied cell when a building dies
   (building.cpp:1549). The cartridge's recipe for ANIM_FBALL1 is genuinely empty -- that
   decode is correct and was re-checked -- but the console NEVER SPAWNS IT THERE. The N64
   replaced that call with ANIM_FRAG3: ROM 0x404B0 is `addiu $a1, $zero, 4` and the
   constructor at ROM 0x404EC loads AnimTypes[4], whose first slot is SOURCE_Debris2, the
   StructureDamage chunk system. Aircraft take ANIM_FRAG2 the same way (ROM 0x183DB8,
   literal 3). A ROM-wide scan of the encoded `jal 0x800334F8` finds 37 anim-spawn sites
   and not one of them passes a literal 0.

   So FBALL1 is unreachable on the cartridge, and our EFX_ANIM2RECIPE had no entry
   mapping to recipe 4 at all: the console's building-debris effect was dead code.

   This hook lets the renderer answer "was this cell covered by a building on the PREVIOUS
   tick?" -- the building is already out of the heap by the time its FBALL1s appear -- and
   returns a stable key for that building's footprint so a multi-cell structure fires ONE
   FRAG3 rather than one per cell, which is what the console does. Returns -1 for open
   ground, where FBALL1 maps to FRAG2 one for one. */
static int (*g_efxDeadFootprint)(int cellx, int celly) = 0;
/* Same map, the HOUSE half: 1 = GDI, 0 = Nod/neutral, -1 = no dead building here. */
static int (*g_efxDeadHouse)(int cellx, int celly) = 0;
static int efx_hidden(float wx, float wz)
{
    return g_efxCellHidden && g_efxCellHidden((int)wx, (int)wz);
}

/* ASSEMBLY ADDITION (terrain x effects): optional ground-height query, set by the
   host once the PK9 heightfield exists. Every effect's hardcoded lift is relative
   to the GROUND under it, not to the old y = 0 plane. NULL = flat, as before. */
static float (*g_efxGroundY)(float wx, float wz) = 0;
static float efx_ground(float wx, float wz)
{
    return g_efxGroundY ? g_efxGroundY(wx, wz) : 0.0f;
}

/* ASSEMBLY ADDITION (models x effects): WHERE A GUN POINTS. Same shape as the ground
   hook above and for the same reason: this file knows about anims and particles and
   deliberately nothing about meshes, the object list or facings, so the host answers
   the one question it cannot. Given the anim's attref -- the engine heap slot of the
   object the anim is attached to, which the brain already exports -- it fills in the
   world position of that object's weapon and returns 1. 0 means "no such object, or one
   whose weapon the host cannot place", and the caller falls back to what it did before.

   attkind is that object's RTTI, which the brain exports beside attref, and it is not
   decoration: attref is a slot in the object's OWN heap, so an infantryman and a tank
   can carry the same number and only the kind tells them apart. -1 is a brain older than
   the export; the host then matches on the heap id alone, exactly as it did before the
   kind was passed. */
static int (*g_efxMuzzleAt)(int attref, int attkind,
                            float* wx, float* wy, float* wz) = 0;

/* ------------------------------------------------- the N64 effect art (efx.pack) */

/* One texture sequence from the cartridge (see bake_efx.py for provenance). Every
   frame is its own small power-of-two GL texture; uw/uh is the used sub-rect. */
struct EfxSeq {
    char name[9];
    int frames, w, h, uw, uh;
    std::vector<GLuint> gl;
};
static std::vector<EfxSeq> g_efxSeqs;
static bool g_efxHaveArt = false;

static const EfxSeq* efx_seq(const char* name)
{
    for (size_t i = 0; i < g_efxSeqs.size(); i++)
        if (!strcmp(g_efxSeqs[i].name, name))
            return &g_efxSeqs[i];
    return NULL;
}

/* Call once with a live GL context (same rule as sb_init: it uploads textures).
   Missing pack = the old procedural billboards, announced, not an error. */
static bool efx_load_pack(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "effects: no N64 art pack at %s (run bake_efx.py); using "
                        "procedural billboards\n", path);
        return false;
    }
    char magic[8];
    unsigned ver = 0, count = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "CNC3DEFX", 8) != 0) {
        fprintf(stderr, "effects: %s is not an efx pack\n", path);
        fclose(f);
        return false;
    }
    if (fread(&ver, 4, 1, f) != 1 || fread(&count, 4, 1, f) != 1) { fclose(f); return false; }
    std::vector<unsigned char> buf;
    for (unsigned i = 0; i < count; i++) {
        EfxSeq s;
        unsigned d[5];
        memset(s.name, 0, sizeof(s.name));
        if (fread(s.name, 1, 8, f) != 8 || fread(d, 4, 5, f) != 5) { fclose(f); return false; }
        s.frames = (int)d[0]; s.w = (int)d[1]; s.h = (int)d[2];
        s.uw = (int)d[3]; s.uh = (int)d[4];
        buf.resize((size_t)s.w * s.h * 4);
        for (int k = 0; k < s.frames; k++) {
            if (fread(&buf[0], 1, buf.size(), f) != buf.size()) { fclose(f); return false; }
            GLuint id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            /* LINEAR: the N64 filtered these tiny particle textures too; the pad
               ring and frame borders are transparent so bleeding only fades edges. */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.w, s.h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, &buf[0]);
            s.gl.push_back(id);
        }
        g_efxSeqs.push_back(s);
    }
    fclose(f);
    g_efxHaveArt = true;
    fprintf(stderr, "effects: N64 art pack %s: %u sequences (cartridge particle "
                    "textures, resident segment)\n", path, count);
    return true;
}

/* Per-anim recipe: which cartridge sequence draws this engine anim, following the
   N64's own ANIM -> particle-source table (n64specialeffect.c). size = billboard
   half-width in world cells for a Size=24 (one-cell) anim; the anim's own Size
   field scales it. oneshot: frame indexed by life t; else loop on engine stage.
   smoke: add the Burn_Smoke puff layer the N64 recipe also spawns. NULL seq with
   drawn=1 means "the N64 deliberately draws nothing" (cannon muzzle flash). */
struct EfxArt {
    const char* name;
    const char* seq;
    unsigned char oneshot, additive, smoke;
    float size;
    const char* n64src;                     /* provenance note for the dump */
};

static const EfxArt EFX_ART[] = {
    /* explosions: N64 FRAG recipe = Debris + Fire_S + smoke + Explode_*; the core
       flash art is System_FireBall's 12-frame burst. */
    { "FBALL1",   "FIREBALL", 1, 1, 1, 0.62f, "System_FireBall" },
    { "FRAG1",    "FIREBALL", 1, 1, 1, 0.55f, "Debris+Fire_S+Explode_L" },
    { "FRAG2",    "FIREBALL", 1, 1, 1, 0.55f, "Debris+Fire_S+Explode_L" },
    { "FRAG3",    "FIREBALL", 1, 1, 1, 0.55f, "Debris+Fire_S+Explode_L" },
    { "VEH-HIT1", "FIREBALL", 1, 1, 0, 0.42f, "Explode_S+Dirt+Sparks" },
    { "VEH-HIT2", "FIREBALL", 1, 1, 0, 0.42f, "Dirt+Explode_M" },
    { "VEH-HIT3", "FIREBALL", 1, 1, 0, 0.42f, "Explode_S+Dirt+Sparks" },
    { "ART-EXP1", "FIREBALL", 1, 1, 1, 0.50f, "Explode_M+Explode_S+Dirt" },
    { "GRENADE",  "FIREBALL", 1, 1, 0, 0.40f, "Dirt+Explode_S" },
    { "NAPALM1",  "FLAME",    1, 1, 1, 0.50f, "Napalm_S+Napalm_Smoke_S" },
    { "NAPALM2",  "FLAME",    1, 1, 1, 0.60f, "Napalm_M+Napalm_Smoke_M" },
    { "NAPALM3",  "FLAME",    1, 1, 1, 0.72f, "Napalm_L+Napalm_Smoke_L" },
    { "ATOMSFX",  "SHOCK",    1, 1, 1, 1.40f, "ATOM_BLAST 7xFire_L (approx)" },
    /* small-arms impacts and muzzle smoke: the N64 maps these to smoke-puff
       sources (Gun_*, Sam_*), texture System_Burn_Smoke. */
    { "PIFF",     "SMOKE",    1, 0, 0, 0.14f, "Gun_E (smoke tmpl)" },
    { "PIFFPIFF", "SMOKE",    1, 0, 0, 0.18f, "Gun_E+Gun_SE (smoke tmpl)" },
    { "MINIGUN",  "SMOKE",    1, 0, 0, 0.16f, "Gun_<dir> (smoke tmpl)" },
    { "SAMFIRE",  "SMOKE",    1, 0, 0, 0.30f, "Sam_<dir> (smoke tmpl)" },
    /* the cartridge shows NO cannon muzzle flash: ANIM_MUZZLE_FLASH's recipe is
       four empty slots. Honour that: draw nothing, and report it as art=none. */
    { "GUNFIRE",  NULL,       1, 1, 0, 0.0f,  "MUZZLE_FLASH: empty by design" },
    /* lingering fire: System_Burn_Fire's 8-frame flame lick, looping. */
    { "FIRE1",    "FLAME",    0, 1, 0, 0.45f, "Fire_*/Burn_Fire" },
    { "FIRE2",    "FLAME",    0, 1, 0, 0.35f, "Fire_*/Burn_Fire" },
    { "FIRE3",    "FLAME",    0, 1, 0, 0.26f, "Fire_*/Burn_Fire" },
    { "FIRE4",    "FLAME",    0, 1, 0, 0.19f, "Fire_*/Burn_Fire" },
    { "FLMSPT",   "FLAME",    0, 1, 0, 0.35f, "Flame_Spout" },
    { "BURN-S",   "FLAME",    0, 1, 1, 0.30f, "Burn_S+Smoke_Burn_S" },
    { "BURN-M",   "FLAME",    0, 1, 1, 0.45f, "Burn_M+Smoke_Burn_M" },
    { "BURN-L",   "FLAME",    0, 1, 1, 0.60f, "Burn_L+Smoke_Burn_L" },
    /* flamethrower tongues: System_FlameThrower's blue->red ramp. */
    { "FLAME-N",  "FLTHROW",  1, 1, 0, 0.45f, "Flame_N" },
    { "FLAME-NE", "FLTHROW",  1, 1, 0, 0.45f, "Flame_NE" },
    { "FLAME-E",  "FLTHROW",  1, 1, 0, 0.45f, "Flame_E" },
    { "FLAME-SE", "FLTHROW",  1, 1, 0, 0.45f, "Flame_SE" },
    { "FLAME-S",  "FLTHROW",  1, 1, 0, 0.45f, "Flame_S" },
    { "FLAME-SW", "FLTHROW",  1, 1, 0, 0.45f, "Flame_SW" },
    { "FLAME-W",  "FLTHROW",  1, 1, 0, 0.45f, "Flame_W" },
    { "FLAME-NW", "FLTHROW",  1, 1, 0, 0.45f, "Flame_NW" },
    /* smoke: System_Burn_Smoke's 6-frame puff, looping and rising. */
    { "SMOKEY",   "SMOKE",    0, 0, 0, 0.30f, "Smokey" },
    { "SMOKLAND", "SMOKE",    0, 0, 0, 0.55f, "LZ_SMOKE" },
    { "SMOKE_M",  "SMOKE",    0, 0, 0, 0.40f, "Smoke_M" },
    /* ion cannon: the N64 does lightning in code (Lightning_1..7 line templates,
       untextured) plus Ion_Spark glows; we draw the glow + ring, no lightning. */
    { "IONSFX",   "IONSPARK", 1, 1, 0, 0.90f, "Ion_Spark+Shock (partial)" },
};

static const EfxArt* efx_art(const char* name)
{
    for (size_t i = 0; i < sizeof(EFX_ART) / sizeof(EFX_ART[0]); i++)
        if (!strcmp(EFX_ART[i].name, name))
            return &EFX_ART[i];
    return NULL;
}

/* -------------------------------------------------------------- tiny line parser */
static int efx_field(char** f, int nf, const char* key, int dflt)
{
    const size_t n = strlen(key);
    for (int i = 0; i < nf; i++)
        if (!strncmp(f[i], key, n) && f[i][n] == '=')
            return atoi(f[i] + n + 1);
    return dflt;
}

static void efx_str_field(char** f, int nf, const char* key, char* out, size_t outn)
{
    const size_t n = strlen(key);
    for (int i = 0; i < nf; i++)
        if (!strncmp(f[i], key, n) && f[i][n] == '=') {
            snprintf(out, outn, "%s", f[i] + n + 1);
            return;
        }
    snprintf(out, outn, "?");
}

static void efx_begin(void)
{
    g_efxAnims.clear();
    g_efxBullets.clear();
    g_efxDying.clear();
    g_efxLivePrev.swap(g_efxLiveNow);
    g_efxLiveNow.clear();
}

/* One EFX| line from the brain dump. The caller may hand the line with its newline
   still attached; strip it here so this module has no ordering contract with the
   OBJ| parser. */
static void efx_parse_line(char* line)
{
    char* nl = strpbrk(line, "\r\n");
    if (nl) *nl = 0;

    char* f[24];
    int nf = 0;
    char* p = line;
    f[nf++] = p;
    while (*p && nf < 24) {
        if (*p == '|') { *p = 0; f[nf++] = p + 1; }
        p++;
    }
    if (nf < 3) return;

    if (!strcmp(f[1], "ANIM")) {
        EfxAnim a;
        a.id      = efx_field(f, nf, "id", -1);
        a.type    = efx_field(f, nf, "type", -1);
        a.stage   = efx_field(f, nf, "stage", 0);
        a.start   = efx_field(f, nf, "start", 0);
        a.stages  = efx_field(f, nf, "stages", 1);
        a.loops   = efx_field(f, nf, "loops", 1);
        a.size    = efx_field(f, nf, "size", 24);
        a.ground  = efx_field(f, nf, "ground", 0);
        a.attref  = efx_field(f, nf, "attref", -1);
        a.attkind = efx_field(f, nf, "attkind", -1);
        a.owner   = efx_field(f, nf, "owner", -1);
        /* Default 0 means "this brain does not export the field", which switches the
           ghost clock off and leaves the old one-emission-per-sighting behaviour exactly
           as it was. A renderer that emitted ten times off a field it never actually read
           would be worse than one that emits once. */
        a.biggest = efx_field(f, nf, "biggest", 0);
        a.chunkHouse = -2;
        efx_str_field(f, nf, "name", a.name, sizeof(a.name));
        a.wx = (float)efx_field(f, nf, "lx", 0) / 256.0f;
        a.wz = (float)efx_field(f, nf, "ly", 0) / 256.0f;
        g_efxAnims.push_back(a);

        std::set<int>& now = g_efxLiveNow[a.name];
        now.insert(a.id);
        if (!g_efxLivePrev[a.name].count(a.id))
            g_efxSpawns[a.name]++;
    } else if (!strcmp(f[1], "BULLET")) {
        EfxBullet b;
        b.id     = efx_field(f, nf, "id", -1);
        b.type   = efx_field(f, nf, "type", -1);
        b.face   = efx_field(f, nf, "face", 0);
        b.alt    = efx_field(f, nf, "alt", 0);
        b.invis  = efx_field(f, nf, "invis", 0);
        b.arcing = efx_field(f, nf, "arcing", 0);
        b.homing = efx_field(f, nf, "homing", 0);
        efx_str_field(f, nf, "name", b.name, sizeof(b.name));
        b.wx = (float)efx_field(f, nf, "lx", 0) / 256.0f;
        b.wz = (float)efx_field(f, nf, "ly", 0) / 256.0f;
        g_efxBullets.push_back(b);

        char key[24];
        snprintf(key, sizeof(key), "BULLET:%s", b.name);
        std::set<int>& now = g_efxLiveNow[key];
        now.insert(b.id);
        if (!g_efxLivePrev[key].count(b.id))
            g_efxSpawns[key]++;
    }
}

/* Called from the OBJ| parse loop for every infantryman the engine reports dying, so
   the harness can prove a death animation was actually on offer this run. */
static void efx_note_dying(int heapid, float wx, float wz, int stage, int house)
{
    if (g_efxDeathIds.insert(heapid).second)
        g_efxDeathsSeen++;
    EfxDying d;
    d.id = heapid;
    d.wx = wx;
    d.wz = wz;
    d.stage = stage;
    d.house = house;
    g_efxDying.push_back(d);
}

/* The house of whatever is dying at (wx,wz), or -1. Death anims spawn on the same
   tick their object is marked dying and at its own coord, so nearest-within-2-cells
   over this dump's dying list identifies the corpse. */
static int efx_dying_house_near(float wx, float wz)
{
    float best = 2.0f * 2.0f;
    int house = -1;
    for (size_t i = 0; i < g_efxDying.size(); i++) {
        const float dx = g_efxDying[i].wx - wx, dz = g_efxDying[i].wz - wz;
        const float d2 = dx * dx + dz * dz;
        if (d2 < best) { best = d2; house = g_efxDying[i].house; }
    }
    return house;
}

/* The house feeding efx_emit_chunks for the anim currently being spawned. Ambient
   rather than threaded through every slot call, the same way g_efxDeadFootprint is. */
static int g_efxSpawnHouse = -1;

/* ------------------------------------------------------------------ classification */
enum EfxClass {
    EFXC_SPARK = 0,     /* PIFF / PIFFPIFF: small-arms impact */
    EFXC_MUZZLE,        /* MINIGUN / GUNFIRE / SAMFIRE / FLAME-* / CHEM-*  */
    EFXC_EXPLOSION,     /* FBALL1 / FRAG* / VEH-HIT* / ART-EXP1 / NAPALM* */
    EFXC_FIRE,          /* FIRE1..4 / BURN-* / FLMSPT: lingering flames */
    EFXC_SMOKE,         /* SMOKEY / SMOKLAND */
    EFXC_UNKNOWN
};

static EfxClass efx_classify(const char* n)
{
    if (!strcmp(n, "PIFF") || !strcmp(n, "PIFFPIFF"))         return EFXC_SPARK;
    if (!strcmp(n, "MINIGUN") || !strcmp(n, "GUNFIRE") ||
        !strcmp(n, "SAMFIRE") || !strncmp(n, "FLAME-", 6) ||
        !strncmp(n, "CHEM-", 5))                              return EFXC_MUZZLE;
    if (!strcmp(n, "FBALL1") || !strncmp(n, "FRAG", 4) ||
        !strncmp(n, "VEH-HIT", 7) || !strcmp(n, "ART-EXP1") ||
        !strncmp(n, "NAPALM", 6) || !strcmp(n, "ATOMSFX"))    return EFXC_EXPLOSION;
    if (!strncmp(n, "FIRE", 4) || !strncmp(n, "BURN-", 5) ||
        !strcmp(n, "FLMSPT"))                                 return EFXC_FIRE;
    if (!strcmp(n, "SMOKEY") || !strcmp(n, "SMOKLAND") ||
        !strcmp(n, "SMOKE_M"))                                return EFXC_SMOKE;
    return EFXC_UNKNOWN;
}

/* Progress through the anim's life, 0..1. The honest denominator is the engine's
   Stages, BUT with no SHP art loaded (this build has none) the engine cannot count
   frames and reports Stages as 0/-1 for many types while the stage counter still
   ticks up forever. Normalize against a per-class nominal length in that case, and
   let anything past its nominal life report t=1 (the draw code fades it to nothing,
   which also hides the immortal stage-hundreds anims an art-less engine keeps). */
static float efx_life(const EfxAnim& a, EfxClass c)
{
    int den = a.stages - 1;
    if (den < 1) {
        switch (c) {
        case EFXC_SPARK:     den = 4;  break;
        case EFXC_MUZZLE:    den = 6;  break;
        case EFXC_EXPLOSION: den = 14; break;
        default:             den = 8;  break;
        }
    }
    float t = (float)a.stage / (float)den;
    return t > 1.0f ? 1.0f : t;
}

/* Looping effects (fire, smoke) pulse instead of dying: phase 0..1 off the stage. */
static float efx_pulse(const EfxAnim& a, int period)
{
    return (float)(a.stage % period) / (float)period;
}

/* ================================================================================== *
 *  THE PARTICLE SYSTEM  (n64particle.c / n64specialeffect.c, recreated)
 *
 *  WHAT THE CONSOLE ACTUALLY DOES, and what this replaces.
 *    Our old draw put ONE textured billboard on screen per live engine anim. The
 *    cartridge treats the anim as nothing but a TRIGGER: it instantiates up to four
 *    emitter "sources" (ten for ION_CANNON / ATOM_BLAST) from EFX_RECIPE[], each of
 *    which spawns trunc(particles_per_tick) INDEPENDENT particles every tick, each
 *    with its own randomised launch angle, speed, life and size, its own gravity and
 *    its own size growth -- and every one of them OUTLIVES the anim that spawned it.
 *    SOURCE_Debris additionally throws solid chunks on ballistic arcs, and each chunk
 *    tows a chain of embers (System_Debris) behind it. That structure is the whole of
 *    what the reference frames show and the whole of what was missing.
 *
 *  UNITS. The cartridge counts WORLD UNITS, 256 to the cell -- the same space its
 *    camera (docs/n64-camera-math.md: cell (cx,cy) sits at (cx*256, 0, cy*256)) and
 *    its terrain vertices (tools/romdump/heightmap_notes.md:12/:69, vtx.x = x*256,
 *    vtx.y = h*4) live in, and the same LEPTONS_PER_CELL cnc_eyes.cpp already uses for
 *    the engine's coordinates. particle_recipes.json's `_meta` string says "cell =
 *    192"; it is WRONG, and nothing but this one divisor ever depended on it.
 *    MODEL_SCALE (1/1024) is the MESH authoring scale and must never appear here.
 *      SOURCE_Debris speed  20..25 u/tick -> 0.078..0.098 cells/tick (1.2..1.5 cells/s)
 *      SOURCE_Explode_L     30.6 + 30.0/tick -> 1.29 cells across after 10 ticks
 *      SOURCE_Smoke_Burn_L  165.5 + 21.0/tick -> 1.63 cells across after 12 ticks
 *      ANIM_BURN_BIG slot 2 dy = 152 -> 0.594 cells above the ground under (x,z)
 *      ANIM_ATOM_BLAST ring |offset| <= 615 -> 2.4 cells radius
 *
 *  THE CLOCK is the engine tick, never the wall clock. efx_step() is called once per
 *    CNC_Advance_Instance from the renderer's deduplicated tick edge; one recipe tick
 *    is one engine tick is 1/15 s. Proven on SCG01EC: an anim's `stage=` advances +1
 *    per engine tick, so the recipes' fire_stage compares directly against it with no
 *    rescaling; and the reference footage's ember trails put one ember every
 *    0.084..0.102 cells, which is SOURCE_Debris's own 20..25 u/tick to within
 *    measurement error. There is no scale factor anywhere in here.
 *
 *  DETERMINISM (load-bearing -- gate G4 compares digests across runs). Every random
 *    draw comes from efx_hash(key, bornFrame, slot/tick, ordinal), a pure function of
 *    engine state:
 *        key       = (AnimType << 16) | the anim's heap id
 *        bornFrame = the engine frame on which that anim was first seen
 *        slot/tick = the recipe slot index and the tick within its burst
 *        ordinal   = the particle's index inside this emission
 *    Nothing is carried between frames, nothing is drawn from a wall clock, and no
 *    accumulating global whose order depends on frame timing takes part. A paused
 *    frame, a re-dump or a second --shot pass therefore cannot perturb one particle,
 *    and two runs of the same input log are identical particle for particle. The pool
 *    is only ever appended to in g_efxAnims order (the brain's own heap walk order)
 *    and evicted oldest-first, both deterministic.
 * ================================================================================== */

/* One cell, in the cartridge's world units. See the units note above. */
static const float EFX_U = 1.0f / 256.0f;

/* ---- THE EFFECT SIZE DIALS -------------------------------------------------------
   OURS, not the cartridge's, and they default to 1.0 = exactly what the ROM says.

   Both numbers below were checked against the ROM before this dial was added, because
   a knob over a decode error is just a bug with a nicer interface:

     SPRITES.  The quad path (RAM 0x80050298) builds each corner as
     `centre + f20 * axis`, and f20 is `lwc1 f20, 0x28(s1)` -- the particle's own size
     field, loaded DIRECTLY with no multiply. So `size` IS the half-extent and there is
     no 0.5 to apply. (An earlier pass had one, which drew every sprite at half its
     linear size and a quarter of its area; removing it was correct.)

     CHUNKS.  Birth scale is 0.25 and 1024 mesh units x 0.25 = 256 leptons = one cell,
     so 0.25 corresponds exactly to the renderer's MODEL_SCALE of 1/1024: a chunk is
     born at its authored size. The -0.005/tick shrink is the console's own
     (visual +0x2C), reaching zero at tick 50 against a 40..50 tick life.

   Both still read as smaller than the console. Since chunks are MESHES and
   sprites are BILLBOARDS, no single shared defect explains both, and the honest
   position is that something upstream of the arithmetic differs (candidates not yet
   eliminated: the console's billboard axes may not be unit vectors, and the recipe
   source records themselves are a transcription). So these are art dials set
   by eye, exactly as --shadowscale was, and the number they land on is evidence for
   whoever chases the real cause. */
/* THREE groups, because they are three different things on screen and they are judged
   them separately. Split by PARTICLE SYSTEM, which is the cartridge's own grouping
   (EFX_SYS, decoded from the ROM) rather than a category invented here:

     BLAST   systems 0,1,4,7 -- GLOW, FIREBALL, SHOCK, IONSPARK.  The bright flash:
                                Explode_S/M/L, Sparks, Dirt, muzzle glows.
     TRAIL   the EMBER TRAILS: the orange dotted arcs that stream behind each flying
                                chunk and stay drawn on the air after it has gone.
                                These are what is meant by "trails" -- the report pointed at
                                a screenshot of exactly these arcs. They are born in
                                the chunk integrator, NOT from a recipe source, which
                                is why an earlier version of this dial keyed on
                                particle system could not touch them at all: pressing
                                it moved Fire_S, a fireball sprite, instead.
     SMOKE   systems 5,6,9   -- FLAME, SMOKE, FLTHROW: the fire and smoke that hang
                                in the air afterwards.
     CHUNK   the ballistic debris MESHES (they live in g_efxChunk).

   All three default to 1.0 = exactly what the ROM says. See the note above about why
   these are dials and not a fix. */
static float g_efxBlastScale = 1.0f;
/* 4.0, NOT 1.0, and this is the one dial that does not default to the cartridge's
   own value. Set it by eye against the console: at the ROM's
   size the ember arcs behind flying debris read far too thin next to the real thing.
   Everything else here stays at 1.0 = exactly what the ROM says, so this is a single
   deliberate art decision rather than a general fudge -- and it is only the EMBER
   size. The explosion sprite that goes off alongside the trails is untouched, which
   is the deliberate choice here. --trailscale 1 restores the cartridge. */
static float g_efxTrailScale = 4.0f;   /* the ember arcs; applied at ember birth */
static float g_efxSmokeScale = 1.0f;
static float g_efxChunkScale = 1.0f;

/* Which dial a RECIPE-SPAWNED sprite's system belongs to. The ember trails are not
   here: they have no recipe source of their own, and are scaled where they are born
   (see THE TRAIL in the chunk integrator). System 8 is the ember ART, used by the
   trail, so it follows the trail dial for consistency if a recipe ever spawns it. */
static float efx_sprite_scale_for_sys(unsigned sys)
{
    switch (sys) {
        case 0: case 1: case 4: case 7: return g_efxBlastScale;
        case 8:                         return g_efxTrailScale;
        default:                        return g_efxSmokeScale;
    }
}

/* How far a bullet floats above the ground, in cells. The cartridge's value is ZERO:
   the object draw at ROM 0x4D01C adds only the engine's own altitude, and the per-model
   offset record for bullet type ids 0x52..0x57 reads (0,0,0) at ROM 0x99AC0..0x99B10.
   The mesh's own vertices (y 30..53 mesh units = 0.029..0.052 cells) are the whole lift
   a rocket gets on hardware, and the cartridge's answer to z-fighting is a per-class
   depth-range bias (table ROM 0x1B6760, mode 7 = near +200 / far +1800), not geometry.
   If a slope ever clips a bullet, raise this to 0.05f and record the residue as a
   known gap. Do not grow it silently: the whole point is that the rocket and its
   own smoke trail must occupy the same place on screen. */
static const float EFX_BULLET_LIFT = 0.0f;

/* Pool sizes, and why these numbers. MEASURED, not guessed: a 900-tick run of the
   SCG01EC skirmish (two burning wrecks plus a running firefight) peaked at 979 live
   sprites and 34 live chunks, and evicted 249 sprites against a 1024 cap. So 1024 was
   too small for an ordinary skirmish, never mind a base assault; 2048 clears the
   measured peak with room for a second front.
   WHAT FILLS IT: chunk-trail embers. Each chunk emits one ember per tick for its
   40 +- 10 tick life, so twenty chunks in the air is eight hundred embers on their own.
   FILL, which is the limit that matters on a Voodoo2: an ember is 0.045 x 0.09 cells,
   about 4 x 7 px at 640x480, so 1800 of them cost ~50 kpix; the expensive quads are
   the few dozen large ones (a spent Explode_L reaches 1.3 cells, ~108 px, 12 kpix
   each) for another ~90 kpix. About 140 kpix of blended fill per frame, 4.2 Mpix/s at
   30 Hz, against a Voodoo2's ~90 Mpix/s -- roughly 5%, and comfortably clear.
   CPU is the tighter budget: 2048 quads is 8192 vertices of immediate mode. Batching
   by (system, frame) keeps it to ~20 texture binds and ~10 blend changes for the whole
   frame; DO NOT let anyone "simplify" the draw into one loop per particle, or the
   Win98 port will crawl.
   DEGRADE: at the cap we evict the OLDEST SPRITES, never a chunk (the chunks are the
   silhouette in question), in one block of EFX_EVICT_BLOCK so the eviction
   memmove is amortised instead of running on every single push; and we print EFXCAP|
   so truncation is visible to the harness instead of being guessed at. */
#ifndef EFX_MAX_PART
#define EFX_MAX_PART  2048
#endif
#ifndef EFX_MAX_CHUNK
#define EFX_MAX_CHUNK   96
#endif
#define EFX_EVICT_BLOCK 64

/* 16 of the 66 cartridge anims have an entirely empty recipe (see efx_recipes.h).
   The console spawns NOTHING for those: the spawn wrapper skips a zero source_id
   (RAM 0x801FC884) and carries no fallback. We honour that, which means ANIM_FBALL1
   (the death explosion of the LST, harvester, MCV and gunboat -- udata.cpp:426/588/
   641/963), ANIM_SMOKE_M and ANIM_MUZZLE_FLASH draw nothing at all. That is
   data-backed and it WILL read as a regression to anyone expecting the old
   billboard, so it is reported as `empty-by-design` in efxdump.

   DEFAULTED TO 1 (keep the billboard) ON PURPOSE, and the reasoning is worth
   keeping because the evidence CONTRADICTS ITSELF. Two facts cannot both be
   right: the recipe table really is empty for FBALL1, and a harvester blowing up
   in silence with no visible effect is not what the cartridge does. The decode
   notes resolve it with "for an empty recipe the anim's own billboard art is the
   whole effect" -- but this module's own header establishes that the cartridge
   file system contains NO effect sprites, so that resolution cannot be right
   either. Something else draws those, and it has not been found yet.
   Until it is, drawing the cartridge's own particle texture (efx.pack, from the
   resident segment -- not invented art) is the honest choice: a real explosion
   we cannot yet source exactly beats an invisible one we can defend on a
   technicality. MUZZLE_FLASH stays invisible regardless: that one was settled
   independently and matches the console. Recorded as a known gap.
   Set to 0 to see the strict data-only reading. */
/* EFX_EMPTY_PROC: draw a procedural billboard for an anim whose cartridge recipe is
   empty. DEFAULTS TO 0 NOW, and the reasoning that used to keep it at 1 is gone.

   It was 1 because the evidence looked self-contradictory: the recipe table really is
   empty for ANIM_FBALL1, ANIM_SMOKE_M and ANIM_MUZZLE_FLASH, and "a harvester blows up
   with no visible effect" is plainly not what the console does. Wave 5 removed the
   contradiction. The console NEVER SPAWNS ANIM_FBALL1: it replaced the building-death
   call with ANIM_FRAG3 (ROM 0x404B0) and the aircraft one with ANIM_FRAG2 (ROM 0x183DB8),
   and this module now substitutes exactly that. So the explosion is drawn, by the right
   recipe, and the only thing this flag still did was paint the OTHER two.

   That was visible and wrong: SMOKE_M drew a hard-edged 0.40-cell smoke quad next to
   every explosion -- the translucent grey rectangle in the screenshots -- while
   it was recorded in as many words that SMOKE_M "stays deliberately silent". The
   code and the register disagreed, and the register was right.

   Set to 1 to see the old behaviour. */
#ifndef EFX_EMPTY_PROC
#define EFX_EMPTY_PROC 0
#endif

/* The ten particle systems (gGameParticleSystems, BSS 0x800EC1B0; names from the ROM's
   own registry at ROM 0xA2BD4), bound to the sequences bake_efx.py pulled out of the
   resident segment.

   THE BLEND MODE IS READ OFF THE REFERENCE PIXELS, NOT OUT OF CODE. The ROM's answer
   is the system-init argument the JSON records as `mode`/`draw_type`, and the blender
   it selects lives in the G_SETOTHERMODE_L at RAM 0x8004F6FC that nobody has decoded.
   Every system that could be OBSERVED turned out to be ORDINARY ALPHA, and three
   independent things say so:
     - Burn_Smoke's art DARKENS TO BLACK across its six frames. Additive would erase it
       entirely; alpha gives the grey-to-dark smoke of p077/p080.
     - Debris's embers are SOLID burnt-orange dots over PALE SAND in p084 -- a
       brightening blend cannot draw a dark dot on a light background.
     - Intensity's art is a plain white blob, and every source using it carries a TINT
       the ROM stored separately (Sam_x/Gun_x grey 150,150,150; Sparks pale blue at
       alpha 78; Dirt orange-red). Additively those tints collapse into the same pale
       wash; rendered that way the FRAG1 smoke turned the grass under it bright pale
       green where the console shows grey smoke sitting ON the grass.
     - FireBall and Burn_Fire were tried BOTH ways against c078 (the
       A/B was rendered and compared by eye). Additive stacks seven coincident
       Explode_L quads at their stored alpha 209 into a saturated white cut-out; alpha
       gives the orange-gold glow the console shows. Alpha wins on the pixels.
   The stored per-template alphas are themselves the argument: 78 for Sparks, 136 for
   Burn_Fire, 140/182 for the smokes, 164 for Shock, 209 for the explosions. That is a
   translucency ladder, which is meaningful under an alpha blender and very nearly
   invisible under an additive one.
   Shock, Ion_Spark and FlameThrower have NOT been seen on screen (no ion
   cannon, no flamethrower unit in the test scenario), so they follow the others by
   consistency rather than by evidence. The flag is kept per system so any one of them
   is a one-character change if the ROM later disagrees.

   Systems 2 and 3 own no texture of their own: they are the explosion systems, whose
   chunks the cartridge draws as little 3D meshes (display lists at RAM 0x80193080..,
   tables ROM 0xA2BA0/0xA2BBC) that are decoded but not baked. We
   draw those chunks with System_Debris's own ember sprite instead, tinted by the chunk
   visual's two variant colours. Real cartridge art, wrong primitive. */
struct EfxSystem { const char* seq; unsigned char additive; };
static const EfxSystem EFX_SYS[10] = {
    { "GLOW",     0 },  /* 0 Intensity       32x32 1f  -- Sam_N..NW, Gun_N..NW,
                                                          Sparks, Dirt, LZ smoke     */
    { "FIREBALL", 0 },  /* 1 FireBall        16x8 12f  -- Explode_S/M/L, LandingSmoke*/
    { "DEBRIS",   0 },  /* 2 VehicleDamage   chunks, drawn from the DEBRIS sequence  */
    { "DEBRIS",   0 },  /* 3 StructureDamage ditto                                   */
    { "SHOCK",    0 },  /* 4 Shock           64x64 1f  -- unobserved, see above     */
    { "FLAME",    0 },  /* 5 Burn_Fire       12x16 8f -- all fire/burn/napalm        */
    { "SMOKE",    0 },  /* 6 Burn_Smoke      16x16 6f -- darkens to black: ALPHA     */
    { "IONSPARK", 0 },  /* 7 Ion_Spark       16x16 1f -- Ion_Spark + Lightning_1..7  */
    { "DEBRIS",   0 },  /* 8 Debris          8x16 8f  -- chunk body AND ember trail  */
    { "FLTHROW",  0 },  /* 9 FlameThrower    8x16 8f  -- Flame_N..NW, unobserved     */
};

/* ------------------------------------------------------------------- the stateless RNG
   A pure hash, so a particle's randomness is a function of WHICH particle it is and
   never of when it was asked for. See the determinism note in the banner. */
static unsigned efx_hash(unsigned a, unsigned b, unsigned c, unsigned d)
{
    unsigned h = 0x9E3779B9u;
    h ^= a; h *= 0x85EBCA6Bu; h ^= h >> 13;
    h ^= b; h *= 0xC2B2AE35u; h ^= h >> 16;
    h ^= c; h *= 0x27D4EB2Fu; h ^= h >> 15;
    h ^= d; h *= 0x165667B1u; h ^= h >> 16;
    return h;
}

/* The console draws rand8/256 (particle_recipes_notes.md line 46, RAM 0x8004F0F8):
   EIGHT bits, not twenty-four. Match it, so the quantisation of every speed, life,
   size and angle is the cartridge's own rather than a finer one of our invention. */
static float efx_r8(unsigned h) { return (float)(h & 0xFFu) / 256.0f; }

/* ------------------------------------------------------------------------- the pool */
struct EfxPart {
    float x, y, z;            /* position, CELLS                                     */
    float vx, vy, vz;         /* velocity, cells per engine tick                     */
    /* ALL THREE ACCELERATION COMPONENTS, not just the vertical one. The recipe table's
       visual sub-record is read for its y acceleration and for a damping term, and two
       further floats in it are still unnamed, so ay was all this struct ever carried. The cartridge's
       PER-BUILDING emitter templates store the full vector at +0x1C..+0x24, and 19 of the
       25 damaged-building nodes lean on it -- 17 of them with the same accel z of -0.1,
       the tip that carries a damage plume off the vertical as it climbs. A pool that held
       only ay could not run those chains without throwing away two thirds of their
       acceleration, which is why the damage smoke sat decoded and unwired. */
    float ax, ay, az;         /* acceleration, cells/tick^2 (template +0x1C..+0x24)  */
    float hw, grow;           /* HALF-width in cells, and its growth per tick        */
    float sx, sy, sz;         /* where it was born, for the "did it move" witness    */
    unsigned char arate;      /* visual +0x31: the ALPHA RAMP SLOPE, not a frame rate */
    short life, age;          /* ticks                                               */
    unsigned char sys;        /* index into EFX_SYS                                  */
    unsigned char r, g, b, a; /* the source's visual colour                          */
    unsigned int  seed;       /* chunks only: seeds their trail                       */
    /* CHUNKS ONLY, and only meaningful for a particle in g_efxChunk.
       mesh 0..6 picks one of the seven display lists of its family; family 0 is
       VehicleDamage (SOURCE_Debris) and 1 is StructureDamage (SOURCE_Debris2). scale is
       the console's own live size: it starts at the source record's 0.25 -- which is
       exactly the renderer's MODEL_SCALE -- and SHRINKS by 0.005 a tick (visual +0x2C),
       reaching zero at tick 50 against a 40..50 tick life. house 1 binds the GDI texture
       variant. */
    unsigned char mesh, family, house;
    float scale;
};

/* The two pools. WHICH ONE a particle is in is the only thing that says whether it is a
   sprite or a debris mesh -- there is deliberately no marker byte for it any more. There
   was one, a sys value of 9 stamped onto every chunk, and 9 is also a real particle
   system (FLTHROW, the flamethrower's), so every flame sprite the engine raised was
   mistaken for a chunk and dropped. See efx_draw_particles. */
static EfxPart g_efxPart[EFX_MAX_PART];        /* sprites: puffs, flames, embers */
static EfxPart g_efxChunk[EFX_MAX_CHUNK];      /* ballistic debris chunks        */
static int  g_efxNPart = 0, g_efxNChunk = 0;
static long g_efxPartSpawned = 0, g_efxChunkSpawned = 0;
static long g_efxEvicted = 0, g_efxChunkDropped = 0;
static float g_efxMaxMove = 0.0f;              /* furthest any particle has travelled */
static std::map<std::string, long> g_efxRecipeParts, g_efxRecipeChunks;
static std::map<std::string, std::string> g_efxRecipeName;  /* art name -> ANIM_* recipe */
static std::map<std::string, int>  g_efxEmpty;  /* anims whose recipe is empty by design */
static std::map<std::string, int>  g_efxNoRecipe;/* anims with no cartridge counterpart  */

/* ---- THE TUNING RANGE (--efxloop) -------------------------------------------------
   Sets off an explosion near the camera every N ticks, for ever, so the effect dials
   can be judged on a moving picture rather than one lucky frame. It exists because the
   dials only take effect on the NEXT spawn -- a particle's half-extent is fixed at
   birth on the console too -- so without a steady supply of fresh explosions, pressing
   a key appears to do nothing at all.

   The anim it injects is a REAL one: the same EfxAnim record the brain's EFX|ANIM line
   would produce, pushed into the same list and fired through the same recipe. Nothing
   about the particle path is special-cased, which is the point: what is being tuned is
   what will ship. Injected anims use NEGATIVE ids so they cannot collide with an engine
   heap id, and are cleared with the pool.

   TEST-ONLY. Off unless --efxloop is passed. */
static int  g_efxLoop = 0;                 /* ticks between explosions; 0 = off       */
/* ANIM_FRAG1 (engine AnimType 2). Chosen because its recipe is the only shape that
   exercises ALL THREE dials at once -- Debris(sys 2) = CHUNK, Fire_S(sys 5) = TRAIL,
   Sam_S(sys 0) and Explode_L(sys 1) = BLAST. ART_EXP1, the obvious first guess, has
   only blast sources, so two of the three keys appeared dead against it: measured,
   401 pixels moved for blast and exactly 0 for trail and debris. */
static int  g_efxLoopType = 2;
static int  g_efxLoopNext = 0;
static int  g_efxLoopId = -1;
static float g_efxLoopCX = 0.0f, g_efxLoopCZ = 0.0f;   /* set from the camera */

static void efx_loop_spawn(int frame)
{
    if (g_efxLoop <= 0 || frame < g_efxLoopNext)
        return;
    g_efxLoopNext = frame + g_efxLoop;
    /* Four of them, spread around the camera centre, so blast, trail and debris are
       all on screen at once and at slightly different ages. */
    static const float OFS[4][2] = { {-2.0f, -1.5f}, {2.0f, -1.5f},
                                     {-2.0f,  1.5f}, {2.0f,  1.5f} };
    for (int k = 0; k < 4; k++) {
        EfxAnim a;
        memset(&a, 0, sizeof a);
        a.id     = g_efxLoopId--;          /* negative: never an engine heap id */
        a.type   = g_efxLoopType;
        a.stage  = 0;
        a.start  = 0;
        a.stages = 1;
        a.loops  = 0;
        a.size   = 0;
        a.ground = 1;
        a.attref = -1;
        a.attkind = -1;
        a.owner  = -1;
        a.chunkHouse = -2;
        snprintf(a.name, sizeof a.name, "LOOPTEST");
        a.wx = g_efxLoopCX + OFS[k][0];
        a.wz = g_efxLoopCZ + OFS[k][1];
        g_efxAnims.push_back(a);
    }
}

/* ---- THE CANNON MUZZLE FLASH -- AUTHORED BY US, NOT DECODED -----------------------
   THE CARTRIDGE DRAWS NO CANNON FLASH. That is not a gap in our decode, it is the
   cartridge's authoring: LTNK(75mm), MTNK(105mm), HTNK(120mm), ARTY(155mm) and the GUN
   turret all name ANIM_MUZZLE_FLASH in the console's own weapon table, and recipe 27
   MUZZLE_FLASH has nslot == 0 -- an empty recipe that draws nothing at all. Sixteen of
   the sixty-six are empty like that, by design. Chain-gun vehicles (Jeep, Buggy, APC,
   Guard Tower) are unaffected: they name ANIM_GUN_N, which the cartridge rotates by
   firing octant into recipes 43..50, and those already work.

   a flash is drawn anyway, "similar to the DOS version", so this is
   a DELIBERATE DEPARTURE from the console and is kept OUT of efx_recipes.h -- that file
   is generated from the ROM and every number in it is the cartridge's own. Mixing an
   invented record into it would destroy exactly the property that makes it worth
   having. It lives here instead, clearly labelled, and --nocannonflash restores the
   console's silence.

   The values are the cartridge's own Gun_N muzzle glow (source 9: system 0 GLOW, the
   same art the chain guns flash with) with two changes, both because a tank cannon is a
   bigger gun than a minigun: size 30.6 -> 46.0, and a 2-tick burst kept so it is a
   FLASH and not a stream. Everything else -- speed, life, cone, colour, alpha ramp --
   is copied unchanged from the source the console really does use, so it sits in the
   same visual family as the flashes around it rather than looking like a different
   game's effect. */
static bool g_cannonFlash = true;
static const EfxSourceRec EFX_CANNON_FLASH = {
    "CannonFlash_OURS", 0, 1,
    6.000000f, 0.800000f, 2,          /* ppt, mult, burst  -- Gun_N's                */
    5.000000f, 4.000000f, 10, 5,      /* spd, spdR, life, lifeR -- Gun_N's           */
    46.000000f, 0.000000f,            /* size: OURS, Gun_N's 30.6 scaled for a cannon */
    -1.000000f, 0.000000f, -0.500000f, 0.500000f,   /* cone -- Gun_N's               */
    0.400000f, 0.000000f,             /* yacc, grow -- Gun_N's                       */
    255, 150, 150, 150, 0x10          /* a,r,g,b, arate -- Gun_N's                   */
};

/* Whether the pool has a clock. efx_step() is called from cnc_eyes.cpp's deduplicated
   engine-tick edge; if that call is absent the pool never runs and efx_draw falls back
   to the old one-billboard-per-anim path, so this header is safe to land on its own. */
static bool g_efxPoolLive = false;
static int  g_efxStepFrame = -1;

/* Which anims are already alive, so a recipe slot fires once per (anim, stage). Keyed
   on (type<<16)|heapId because the engine reuses anim heap ids constantly (a missile's
   ANIM_SMOKE_PUFF trail markers cycle through id 0 every tick). A stage that does NOT
   strictly advance means the slot was reused by a new anim, which restarts bornFrame
   and therefore gives the new anim its own particles. */
struct EfxLiveAnim { int bornFrame, lastStage; };
static std::map<int, EfxLiveAnim> g_efxAnimLive;

/* ---------------------------------------------------- THE ART-LESS ANIM'S OWN CLOCK
   20 Aug: "Flame Trooper still looks like it doesnt have any fire effect when
   shooting. I CAN see a small little orange particle, but not a true fire effect."
   That was literally what the code produced: ONE particle per shot where the cartridge
   emits one per anim STAGE.

   The chain. ANIM_FLAME_* is art-less in our data, so AnimTypeClass::Stages resolves
   through Get_Build_Frame_Count(NULL) to 0, AnimClass ends after its first logic pass,
   and CNC3D_Stage() never leaves 0. The renderer therefore sees each FLAME anim in
   exactly ONE dump (measured: 17 FLAME anims in SCG36EA over 449 ticks produced
   `EFXSPAWN|FLAME-SE|particles=14` against `EFXSEEN|FLAME-SE|14`, one particle each),
   fires its one continuous slot once, and that slot's ppt of 1.0715 truncates to 1.

   The cartridge's own number is Biggest, which the brain already exports: the
   AnimTypeClass for ANIM_FLAME_N is constructed at RAM 0x801884B0 with 9 stored at
   object+0x28 (ROM 0x18872C), and 9 is exactly the Biggest the 1995 adata.cpp gives
   FlameN, whose Delay is 1. Ten stages, one particle each, ten particles per shot.

   So such an anim gets a clock of its own here: the renderer keeps it alive after the
   engine has dropped it and feeds efx_fire_slot a SYNTHETIC stage of (frame - bornFrame)
   for biggest+1 ticks. Both terms are engine ticks and never a wall clock, so two runs
   of one script still produce identical particles (G4).

   THE SCOPE IS NARROWER THAN "art-less", and measurement is why. `stages == 0` is not
   the flamethrower's signature: FRAG3, PIFF, PIFFPIFF, VEH-HIT2, VEH-HIT3, ART-EXP1 and
   SMOKE_M all report Stages 0 as well. Two separate cuts are needed.

     1. Some of them ADVANCE anyway -- measured `EFXDUMP|ANIM|FRAG3|...|stage=22/0`, an
        anim whose stage reached 22 with Stages 0. The engine is driving those and the
        normal path is already right, so the clock disarms the moment a key is seen above
        stage 0.
     2. Of the ones that really do live one tick at stage 0, only a CONTINUOUS source
        (burst == 0) is silenced by it. A continuous source has no clock but the stage:
        the ROM's wrapper re-arms it every stage, which is the whole of its emission, so a
        stage that never advances cuts it to one particle -- the bug. A burst source
        already emits on the stage it arms; whether its REMAINING burst ticks run on the
        anim's stage or on the particle system's own action clock (RAM 0x8004F3F8) is not
        settled by anything anyone has disassembled. Firing them here measured PIFF 294 ->
        482, VEH-HIT3 216 -> 588 and ART-EXP1 9 -> 16 particles on one 449-tick run: a
        visible change to every bullet impact in the game, off an assumption, for a bug
        nobody reported. It is recorded as a known gap instead, with those numbers.

   So: continuous slots only, and only on an anim the engine drops at stage 0. On
   SCG36EA that is every FLAME-* and nothing else -- verified by diffing the whole
   EFXSPAWN table before and after. */
struct EfxGhost {
    EfxAnim an;        /* the last engine sighting: position, name, type, chunkHouse */
    int key;           /* the (type<<16)|id anim key, for the particle hash          */
    int recipe;        /* EFX_RECIPE index, resolved once at registration            */
    int bornFrame;     /* engine tick of that sighting; synthetic stage counts from it */
    int biggest;       /* last synthetic stage that still emits                       */
};
/* KEYED ON THE ANIM KEY AND THE FIRER, not on the anim key alone, and that second half
   is measured rather than defensive. g_efxAnimLive's (type<<16)|heapId is not unique over
   TIME, only within one dump, and a ghost by definition outlives its dump. Walking ticks
   435..465 of SCG36EA: one flamethrower anim at frame 440, `id=0 attref=7`, cell 10.8;
   another at frame 443, `id=0 attref=3`, cell 14.2 -- a second trooper, three cells away,
   in the same anim heap slot. On the anim key alone the second shot overwrote the first
   and cut it from ten particles to three. attref is the firing object's own heap slot, so
   the two are told apart and each gets its whole tongue; a genuine re-sighting of the same
   anim, or the same trooper firing again, still lands on the same entry and re-arms it. */
static std::map<std::pair<int, int>, EfxGhost> g_efxGhost;

/* (anim id, clip-frame bucket) pairs the nuke draw has already reported; see
   efx_draw_nuke_meshes for why the line is emitted from the draw. Cleared with the rest
   of the effects state so a second mission starts silent. */
static std::set<std::pair<int, int> > g_efxNukeSaid;
/* the same, for the ion cannon's snapshot index */
static std::set<std::pair<int, int> > g_efxIonSaid;

/* ANIM HEAP IDS ARE RECYCLED WITHIN A MISSION, which this file already says twice --
   g_efxAnimLive above, and g_efxGhost, which adds attref for exactly this reason. Keying
   the two "already reported" sets on the id alone meant a SECOND strike that landed in
   the same heap slot inherited the first one's entries and printed nothing at all.
   Measured: two nuclear strikes one mission, 20 EFXNUKE lines, every one of them from the
   first strike and zero from the second, which detonated and drew perfectly normally.
   A gate asserting a later strike drew would read zero on a working build.

   attref cannot be the discriminator here the way it is for the ghosts: a superweapon
   anim has no firing object and reports attref=-1. What it does have is a stage that only
   ever ADVANCES within one anim, so a stage that goes backwards on a known id is a new
   anim in a reused slot -- the same rule g_efxAnimLive uses. That bumps a generation
   counter which joins the key. */
static std::map<int, std::pair<int, int> > g_efxFxGen;   /* id -> (last stage, generation) */

static int efx_fx_gen(int id, int stage)
{
    std::map<int, std::pair<int, int> >::iterator it = g_efxFxGen.find(id);
    if (it == g_efxFxGen.end()) {
        g_efxFxGen[id] = std::make_pair(stage, 0);
        return 0;
    }
    if (stage < it->second.first)
        it->second.second++;          /* the stage went backwards: a new anim, same slot */
    it->second.first = stage;
    return it->second.second;
}

/* A slot the ghost clock is allowed to fire: a real source whose burst window is 0, the
   table's own marker for "continuous, re-armed every stage" (efx_recipes.h's EfxSourceRec
   comment on +0x18). Every FLAME-* slot is one of these; Gun_* and Dirt are not. */
static bool efx_slot_continuous(const EfxSlot& sl)
{
    return sl.src != 0 && EFX_SRC[sl.src].burst == 0;
}

static bool efx_recipe_continuous(const EfxRecipe& rec)
{
    for (int i = 0; i < (int)rec.nslot; i++)
        if (efx_slot_continuous(rec.slot[i]))
            return true;
    return false;
}

static void efx_pool_clear(void)
{
    g_efxNPart = g_efxNChunk = 0;
    g_efxAnimLive.clear();
    /* Ghosts are keyed by engine heap ids and dated by the engine frame, both of which
       the next mission reuses from this one, so they go the way g_efxAnimLive does. */
    g_efxGhost.clear();
    g_efxNukeSaid.clear();
    g_efxIonSaid.clear();
    g_efxFxGen.clear();
    g_efxPartSpawned = g_efxChunkSpawned = g_efxEvicted = g_efxChunkDropped = 0;
    g_efxMaxMove = 0.0f;
    g_efxRecipeParts.clear();
    g_efxRecipeChunks.clear();
    g_efxRecipeName.clear();
    g_efxEmpty.clear();
    g_efxNoRecipe.clear();
    g_efxStepFrame = -1;
}

/* Append a sprite. At the cap the OLDEST sprite goes (index 0 is the oldest, because
   the array is only ever appended to and compacted in order). */
static void efx_push(const EfxPart& p)
{
    if (g_efxNPart >= EFX_MAX_PART) {
        memmove(&g_efxPart[0], &g_efxPart[EFX_EVICT_BLOCK],
                sizeof(EfxPart) * (EFX_MAX_PART - EFX_EVICT_BLOCK));
        g_efxNPart = EFX_MAX_PART - EFX_EVICT_BLOCK;
        g_efxEvicted += EFX_EVICT_BLOCK;
    }
    g_efxPart[g_efxNPart++] = p;
    g_efxPartSpawned++;
}

/* ------------------------------------------------------------------------ the spawn
   One transcription of the wrapper at RAM 0x801FC7F0 plus the action integrator at
   RAM 0x8004EFE4. The velocity formula is the ROM's own trig sequence
   (particle_recipes_notes.md lines 52-59):
       a   = uniform(aMin, aMax);   b = uniform(bMin, bMax)
       sp  = speed_base + rand8/256 * speed_rand
       vel = sp * ( sin b, cos b * cos a, sin a )        [x, y up, z]
       pos = emitter + 3.0 * vel                          (vec scale at 0x8004F1B4)
   The ROM's world axes are ours -- x right, y up, z toward the bottom of the screen,
   north = -z (docs/n64-camera-math.md, and SOURCE_Sam_N's a in [-1,0] gives an
   up-and-north plume) -- so this maps across with no sign flips. */
static void efx_emit(const EfxSourceRec& s, float ex, float ey, float ez,
                     int n, unsigned key, int bornFrame, int slot, int k)
{
    const EfxSeq* seq = efx_seq(EFX_SYS[s.sys].seq);
    for (int i = 0; i < n; i++) {
        const unsigned h1 = efx_hash(key, (unsigned)bornFrame,
                                     ((unsigned)slot << 8) | (unsigned)k, (unsigned)i);
        const unsigned h2 = efx_hash(h1, 0x5BF03635u, (unsigned)i, key);

        const float A  = s.aMin + (s.aMax - s.aMin) * efx_r8(h1);
        const float B  = s.bMin + (s.bMax - s.bMin) * efx_r8(h1 >> 8);
        const float sp = (s.spd + efx_r8(h1 >> 16) * s.spdR) * EFX_U;

        EfxPart p;
        p.vx = sp * sinf(B);
        p.vy = sp * cosf(B) * cosf(A);
        p.vz = sp * sinf(A);
        p.x  = ex + 3.0f * p.vx;
        p.y  = ey + 3.0f * p.vy;
        p.z  = ez + 3.0f * p.vz;
        p.sx = p.x; p.sy = p.y; p.sz = p.z;
        /* Zero because a RECIPE particle has never been given a lateral acceleration,
           not because the cartridge says it has none: two floats in that sub-record are
           still unnamed and either could turn out to be one. Set, not left over from the
           stack: EfxPart is a plain struct with no constructor. */
        p.ax = 0.0f;
        p.ay = s.yacc * EFX_U;
        p.az = 0.0f;
        /* size_base/size_rand are the billboard's HALF-EXTENT in world units, NOT its
           diameter. The cartridge builds the quad from UNIT camera axes and offsets each
           corner by size * corner, so the drawn side is 2 * size. The old comment here
           asserted DIAMETER and multiplied by 0.5, which drew every sprite at half the
           cartridge's linear size and a QUARTER of its area: that is the reported "particles
           are too small". +0x2C grows it every tick in the same units. */
        p.hw   = (s.size + efx_r8(h2) * s.sizeR) * EFX_U
                 * efx_sprite_scale_for_sys(s.sys);
        p.grow = s.grow * EFX_U * efx_sprite_scale_for_sys(s.sys);
        if (p.hw < 0.0f) p.hw = 0.0f;
        p.life = (short)(s.life + (int)(efx_r8(h1 >> 24) * (float)s.lifeR));
        if (p.life < 1) p.life = 1;
        p.age  = 0;
        p.sys  = s.sys;
        /* The chunk-only fields, zeroed even though a sprite never reads them. They were
           left uninitialised, and the sentinel bug above then handed that stack garbage
           to efx_chunk_model() -- so the flame sprites were not merely dropped, they were
           dropped on the strength of undefined behaviour. On a different compiler the
           garbage differs and the symptom changes shape rather than going away. */
        p.mesh = p.family = p.house = 0;
        p.scale = 0.0f;
        p.arate = s.arate;                  /* visual +0x31; see efx_recipes.h */
        p.r = s.r; p.g = s.g; p.b = s.b; p.a = s.a;
        p.seed = h2;
        efx_push(p);
    }
}

/* SOURCE_Debris / SOURCE_Debris2: the flying wreckage. The spawn wrapper's own branch
   at RAM 0x801FC9D0 builds a 0x80-byte ParticleExplosion whose constants come from
   RAM 0x801C95F0 = {2.0, 1.0, -2.0, 20.0, 5.0}: y accel -2.0 u/tick^2, launch speed
   20.0 + rand8/256*5.0, elasticity 0.7 (the chunk visual's +0x28), and a child emitter
   chain -- the ember trail -- whose system is &System_Debris (0x800EC350).
   The chunk's LAUNCH DIRECTION is the ONE INVENTED NUMBER in this file. It lives
   inside the explosion updater, which nobody has disassembled. It is not free,
   though: the frames put 14..17 px between consecutive embers at 166.2 px/cell,
   i.e. 0.084..0.102 cells/tick = 21.5..26 world units/tick, which is the FULL 20..25
   speed -- so the launch must be near-horizontal with a free azimuth. Feeding the
   source's own +-0.5 rad cone into the ordinary velocity formula would cap the
   horizontal component at 0.48*speed and halve that spacing. If it ever looks wrong,
   the fix is these three lines, not a redesign. */
static void efx_emit_chunks(const EfxSourceRec& s, float ex, float ey, float ez,
                            int n, unsigned key, int bornFrame, int slot, int k,
                            int ownerHouse)
{
    for (int i = 0; i < n; i++) {
        if (g_efxNChunk >= EFX_MAX_CHUNK) { g_efxChunkDropped++; continue; }
        const unsigned h1 = efx_hash(key, (unsigned)bornFrame,
                                     ((unsigned)slot << 8) | (unsigned)k, (unsigned)i);
        const unsigned h2 = efx_hash(h1, 0x9E3779B9u, (unsigned)i, key);

        const float sp    = (s.spd + efx_r8(h1 >> 16) * s.spdR) * EFX_U;
        const float theta = 6.2831853f * efx_r8(h1);           /* free azimuth      */
        const float phi   = s.aMax * efx_r8(h1 >> 8);          /* shallow elevation */

        EfxPart c;
        c.vx = sp * cosf(phi) * sinf(theta);
        c.vy = sp * sinf(phi);
        c.vz = sp * cosf(phi) * cosf(theta);
        c.x  = ex + 3.0f * c.vx;
        c.y  = ey + 3.0f * c.vy;
        c.z  = ez + 3.0f * c.vz;
        c.sx = c.x; c.sy = c.y; c.sz = c.z;
        /* Chunks fall straight down and bounce; nothing in the debris path ever gives one
           a sideways acceleration, and the chunk loop above deliberately does not read ax
           or az -- a parked chunk zeroes its velocity and must stay parked. Set anyway,
           because these bytes are dumped and compared. */
        c.ax = 0.0f;
        c.ay = EFX_CHUNK_YACC * EFX_U;
        c.az = 0.0f;
        /* DECODED NOW, replacing the measured-from-reference-frames guess that used to
           live here. The source record's `size` (0.25) is the chunk's MESH SCALE, and
           the console shrinks it by the visual record's +0x2C = -0.005 every tick
           (integrator RAM 0x8004EF90), so it reaches zero at tick 50 against a 40..50
           tick life. At birth 0.25 is exactly the renderer's MODEL_SCALE, so a chunk is
           drawn at its authored size and dwindles. hw is left for the sprite fallback. */
        c.scale = s.size * g_efxChunkScale;
        c.hw   = 0.18f * s.size;
        c.grow = 0.0f;
        /* Which of the seven display lists, and which family. The console draws
           `sys[+0x14][rand() % 7]` (RAM 0x8004E8AC); our substitute is the existing
           deterministic hash, so two --shot runs stay identical where rand() would not. */
        c.mesh   = (unsigned char)(efx_hash(h2, 0x1F123BB5u, (unsigned)i, key) % 7u);
        c.family = (unsigned char)(s.sys == 3 ? 1 : 0);   /* 3 = StructureDamage */
        /* WHICH HOUSE TLUT PAINTS THE CHUNK. All four chunk textures are CI8 and
           decode differently under the two resident house tables (debris_chunks.json
           gdi_differs: true for every one), and this used to be a hardcoded 0 -- which,
           in THIS renderer's convention, is the Nod/neutral blue-grey table (the bind
           at cnc_eyes.cpp is `house == 1 ? gl_gdi : gl`; a comment here used to claim
           the opposite and was wrong). So every chunk in the game drew blue-grey and
           the GDI sand decode baked into the pack was never sampled. That is the
           "the pieces use the wrong textures".

           The console picks the table from the debris VISUAL sub-record's house byte
           (variant 0 ROM 0xA2C2C -> GDI table 0x98F30, variant 1 ROM 0xA2C6C ->
           Nod/neutral 0x99130) -- note the console numbers GDI 0 where we number GDI 1.
           What SELECTS the variant is still undecoded (the spawn wrapper's 5th
           argument; single caller at ROM 0x4EE70, stopped there), so the DYING
           OBJECT'S house is used provisionally: a GDI thing's wreckage is sand-toned,
           a Nod or neutral thing's blue-grey. The anim itself cannot say
           (Anim::OwnerHouse is -1 for death anims -- measured, not assumed), so the
           house arrives from the dead-building footprint map or the dying-object
           list, already in the renderer's 1-means-GDI convention; -1 = unknown, keep
           the old table. If the 5th argument is ever decoded, replace this. */
        c.house  = (unsigned char)(ownerHouse == 1 ? 1 : 0);
        c.life = (short)(s.life + (int)(efx_r8(h1 >> 24) * (float)s.lifeR));
        if (c.life < 1) c.life = 1;
        c.age  = 0;
        c.sys  = s.sys;   /* 2 VehicleDamage / 3 StructureDamage, the real index */
        c.arate = s.arate;
        if (s.nvar > 1 && ((h2 >> 8) & 1u)) {
            c.a = EFX_DEBRIS_VAR1[0]; c.r = EFX_DEBRIS_VAR1[1];
            c.g = EFX_DEBRIS_VAR1[2]; c.b = EFX_DEBRIS_VAR1[3];
        } else {
            c.a = s.a; c.r = s.r; c.g = s.g; c.b = s.b;
        }
        c.seed = h2;
        g_efxChunk[g_efxNChunk++] = c;
        g_efxChunkSpawned++;
    }
}

/* Fire one recipe slot for one anim at one stage. */
static void efx_fire_slot(const EfxAnim& an, const EfxSlot& sl, int slotIdx,
                          unsigned key, int bornFrame, const char* rname)
{
    if (!sl.src)
        return;                                  /* empty slot: the console skips it */
    const EfxSourceRec& s = EFX_SRC[sl.src];
    const int k = an.stage - (int)sl.stage;      /* ticks since this slot armed */
    if (k < 0)
        return;

    float count;
    if (s.burst) {
        /* A burst action lives burst_ticks ticks; each tick it emits trunc(count)
           and then count *= per_tick_multiplier (RAM 0x8004F3F8/0x8004F404). */
        if (k >= (int)s.burst)
            return;
        count = s.ppt;
        for (int j = 0; j < k; j++) count *= s.mult;
    } else {
        /* Continuous: the wrapper re-arms the source every stage and the action's
           remaining_ticks is zero, so it emits trunc(ppt) once per stage forever.
           The multiplier never gets a second tick to apply to -- which is just as
           well, since SOURCE_Flame_* declares a multiplier of 176.8. */
        count = s.ppt;
    }
    const int n = (int)count;                    /* trunc, RAM 0x8004F068 */
    if (n <= 0)
        return;

    /* dy is relative to the TERRAIN HEIGHT under (x,z), not to the anim's own y:
       the wrapper calls the ground-height function at RAM 0x801F7DFC with (x,z) and
       adds dy to its result (disassembly RAM 0x801FC8F0-0x801FC92C). g_efxGroundY is
       the renderer's heightfield sampler, so this lands on the drawn ground. */
    float bx = an.wx, by = efx_ground(an.wx, an.wz), bz = an.wz;

    /* A FLAMETHROWER'S TONGUE STARTS AT THE NOZZLE, NOT UNDER HIS BOOTS. Every FLAME_*
       recipe carries dy 0, so all eight of them took that base unchanged and lit their
       fire at ground level inside the man holding the gun -- the same defect the cannon
       flash had, hiding the same way, because the particle pass is depth TESTED against
       the sprite already drawn.

       ONLY THE HEIGHT IS WRONG, and that is the engine's own arrangement rather than an
       accident: the flame thrower is the one infantry type whose firing coordinate is its
       own centre with no forward shift, and the anim is attached to the firer, so it
       already rides him. The horizontal position is therefore left exactly where the
       engine put it and the host is asked only for the weapon's world position, through
       the same hook the cannon flash uses. Asking every tick also means the tongue
       follows a trooper who walks while he burns, which is what an attached anim does.

       A firer the host cannot place -- it died inside the tongue's own ten-tick life, or
       it is a vehicle with no weapon geometry -- keeps the ground-level base, so the
       fallback is the old behaviour rather than a new one. */
    if (g_efxMuzzleAt && an.attref >= 0 && !strncmp(rname, "FLAME_", 6))
        g_efxMuzzleAt(an.attref, an.attkind, &bx, &by, &bz);

    const float ex = bx + (float)sl.dx * EFX_U;
    const float ey = by + (float)sl.dy * EFX_U;
    const float ez = bz + (float)sl.dz * EFX_U;

    /* Counted under the anim's DOS ART NAME, because that is the key the harness and
       every existing gate already speak (expectefx FRAG1). The unambiguous cartridge
       recipe name is recorded alongside and printed in efxdump, so the two anims that
       share the art name "VEH-HIT2" -- ANIM_GRENADE and ANIM_VEH_HIT2, whose recipes
       are completely different -- are still told apart there. */
    g_efxRecipeName[an.name] = rname;
    if (s.sys == 2 || s.sys == 3) {
        efx_emit_chunks(s, ex, ey, ez, n, key, bornFrame, slotIdx, k, g_efxSpawnHouse);
        g_efxRecipeChunks[an.name] += n;
    } else {
        efx_emit(s, ex, ey, ez, n, key, bornFrame, slotIdx, k);
        g_efxRecipeParts[an.name] += n;
    }
}

/* Which anims still want the old procedural billboard. Everything with a cartridge
   recipe is now drawn by the pool and by nothing else; the -1 rows split in two. */
static bool efx_proc_fallback(const EfxAnim& a)
{
    if (a.type < 0 || a.type >= EFX_ANIM2RECIPE_N)
        return true;                    /* an anim we have never seen: show it, loudly */
    const int r = EFX_ANIM2RECIPE[a.type];
    if (a.type == 0)
        return false;               /* FBALL1 is substituted, never drawn procedurally */
    if (r >= 0)
        return EFX_EMPTY_PROC ? (EFX_RECIPE[r].nslot == 0) : false;
    /* No cartridge counterpart. The eight ANIM_CHEM_* (DOS types 22..29) keep the
       procedural spray, because the cartridge's table has no chem entries at all and
       something must be drawn. The rest -- the five *_VIRTUAL companions, the four
       dinosaur deaths, CHEM_BALL, FLAG, BEACON -- are deliberately invisible. */
    return a.type >= 22 && a.type <= 29;
}

/* Integrate one tick of the whole pool. */
static void efx_integrate_once(void)
{
    /* Chunks first: they bounce, and they leave an ember where they land. */
    int w = 0;
    for (int i = 0; i < g_efxNChunk; i++) {
        EfxPart& c = g_efxChunk[i];
        c.x += c.vx; c.y += c.vy; c.z += c.vz;
        c.vy += c.ay;
        const float gy = efx_ground(c.x, c.z);
        if (c.y <= gy) {
            c.y = gy;
            c.vy = -c.vy * EFX_CHUNK_BOUNCE;
            c.vx *= EFX_CHUNK_BOUNCE;
            c.vz *= EFX_CHUNK_BOUNCE;
            if (c.vy * c.vy + c.vx * c.vx + c.vz * c.vz < 4.0e-6f) {
                c.vx = c.vy = c.vz = 0.0f;       /* parked: |v| < 0.002 cells/tick */
                c.ay = 0.0f;
            }
        }
        c.age++;
        /* The console's own shrink: visual +0x2C = -0.005 a tick, integrated at
           RAM 0x8004EF90 and gated on a non-zero size exactly as it is here. */
        if (c.scale != 0.0f) {
            c.scale += EFX_CHUNK_SHRINK * g_efxChunkScale;   /* same 50-tick life */
            if (c.scale < 0.0f) c.scale = 0.0f;
        }
        {
            const float dx = c.x - c.sx, dz = c.z - c.sz;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d > g_efxMaxMove) g_efxMaxMove = d;
        }
        /* THE TRAIL. One ember per tick at the chunk's own position, with no
           velocity of its own, so the chain marks out the arc the chunk flew and
           stays behind after it has gone. This is the orange trail in p084. */
        if (c.age < c.life) {
            const unsigned h = efx_hash(c.seed, (unsigned)c.age, 0u, 0u);
            EfxPart e;
            e.x = c.x; e.y = c.y; e.z = c.z;
            e.sx = c.x; e.sy = c.y; e.sz = c.z;
            e.vx = e.vy = e.vz = 0.0f;
            e.ax = e.ay = e.az = 0.0f;
            /* Embers are half a chunk (p080/p084). g_efxTrailScale is applied HERE
               and only here, because this is the one place an ember is created --
               it has no recipe source to key a system-based dial off. c.hw itself is
               left unscaled by the chunk dial on purpose: that dial resizes the chunk
               MESH, and the ember is not the mesh. */
            e.hw = c.hw * 0.5f * g_efxTrailScale;
            e.grow = 0.0f;
            e.life = (short)(20 + (int)(efx_r8(h) * 20.0f));
            e.age = 0;
            e.sys = 8;
            e.arate = c.arate;                   /* the ember is the same 8-frame art */
            e.r = c.r; e.g = c.g; e.b = c.b; e.a = c.a;
            e.seed = h;
            efx_push(e);
            g_efxChunk[w++] = c;
        }
    }
    g_efxNChunk = w;

    /* Sprites: pos += vel; vel += accel; size += sizeGrow; life-- (node integrator
       RAM ~0x8004ED00). Compacted in place, which keeps index order = age order. */
    w = 0;
    for (int i = 0; i < g_efxNPart; i++) {
        EfxPart& p = g_efxPart[i];
        p.x += p.vx; p.y += p.vy; p.z += p.vz;
        /* All three components, in the node integrator's own order: position off the old
           velocity first, then velocity off the acceleration. Reading only ay here would
           leave a damaged building's sideways acceleration stored and never applied, which
           looks identical to a correct build in every dump that prints the table and
           different in every frame that draws the plume. */
        p.vx += p.ax; p.vy += p.ay; p.vz += p.az;
        p.hw += p.grow;
        if (p.hw < 0.0f) p.hw = 0.0f;
        p.age++;
        {
            const float dx = p.x - p.sx, dz = p.z - p.sz;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d > g_efxMaxMove) g_efxMaxMove = d;
        }
        if (p.age < p.life)
            g_efxPart[w++] = p;
    }
    g_efxNPart = w;
}

/* THE TICK. Call exactly once per engine tick, from the renderer's deduplicated
   tick edge, AFTER the EFX| lines have been parsed. Everything about the pool --
   spawning and integrating both -- happens here and nowhere else; the draw is pure.

   Self-deduplicating on the frame number as well, so a stray second call on the same
   engine frame cannot double-step the pool. dt is clamped to 4 ticks so that a long
   stall (a movie, a pause dialog, a mission change) cannot make the pool catch up
   over hundreds of ticks inside one frame. */
static void efx_step(int frame)
{
    if (frame == g_efxStepFrame)
        return;
    if (frame < g_efxStepFrame)                  /* new mission / restarted clock */
        efx_pool_clear();
    int dt = (g_efxStepFrame < 0) ? 1 : (frame - g_efxStepFrame);
    if (dt < 1) dt = 1;
    if (dt > 4) dt = 4;
    g_efxStepFrame = frame;
    if (!g_efxPoolLive) {
        g_efxPoolLive = true;
        fprintf(stderr,
                "effects: the N64 particle system is LIVE (pool %d sprites + %d "
                "chunks, clocked on the engine tick). Anims are triggers now: every "
                "explosion, trail and flame on screen is a particle integrated from "
                "the cartridge's own 66-recipe table. %d of those 66 recipes are "
                "empty by design and draw nothing at all -- efxdump lists which.\n",
                EFX_MAX_PART, EFX_MAX_CHUNK, 16);
    }

    efx_loop_spawn(frame);

    /* --- spawn: fire every live anim's recipe for its CURRENT stage ---------- */
    std::map<int, EfxLiveAnim> seen;
    std::set<int> fbFired;          /* footprints that already got their one FRAG3 */
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        EfxAnim& an = g_efxAnims[i];      /* mutable: chunkHouse caches on first burst */
        if (an.type < 0 || an.type >= EFX_ANIM2RECIPE_N) {
            g_efxNoRecipe[an.name]++;
            continue;
        }
        int r = EFX_ANIM2RECIPE[an.type];
        /* ANIM_FBALL1 (engine AnimType 0) never happens on the cartridge; substitute the
           anim the console actually spawns. See g_efxDeadFootprint above. */
        int fbKey = -1;
        if (an.type == 0) {
            fbKey = g_efxDeadFootprint
                  ? g_efxDeadFootprint((int)an.wx, (int)an.wz) : -1;
            r = (fbKey >= 0) ? 4 : 3;    /* 4 = ANIM_FRAG3 rubble, 3 = ANIM_FRAG2 */
        }
        /* Whose wreckage is this? A dead BUILDING owns its footprint cells (the same
           map that picks the rubble recipe); otherwise the nearest object dying this
           tick. -1 when neither knows -- efx_emit_chunks then keeps the old table. */
        if (an.chunkHouse == -2) {
            an.chunkHouse = -1;
            if (g_efxDeadHouse)
                an.chunkHouse = g_efxDeadHouse((int)an.wx, (int)an.wz);
            if (an.chunkHouse < 0)
                an.chunkHouse = efx_dying_house_near(an.wx, an.wz);
        }
        g_efxSpawnHouse = an.chunkHouse;
        if (r < 0) {
            /* No cartridge counterpart at all. The eight ANIM_CHEM_* keep the
               procedural spray; the five *_VIRTUAL companions, the dinosaurs, the
               flag and the beacon are skipped outright -- the 1995 renderer never
               drew the *_VIRTUAL ones either (anim.cpp:684 Make_Invisible), which is
               why every fire used to be drawn twice. */
            g_efxNoRecipe[an.name]++;
            continue;
        }
        const EfxRecipe& rec = EFX_RECIPE[r];
        const int key = (an.type << 16) | (an.id & 0xFFFF);

        std::map<int, EfxLiveAnim>::iterator it = g_efxAnimLive.find(key);
        EfxLiveAnim la;
        if (it == g_efxAnimLive.end() || an.stage <= it->second.lastStage) {
            la.bornFrame = frame;                /* first sight, or the id was reused */
        } else {
            la.bornFrame = it->second.bornFrame;
        }
        la.lastStage = an.stage;
        seen[key] = la;

        if (rec.nslot == 0) {
            /* ANIM_MUZZLE_FLASH is empty on the cartridge; one is drawn anyway.
               Fired through efx_emit directly rather than through efx_fire_slot,
               because a slot indexes the ROM's source table and this source is not in
               it. Position is the anim's own -- the engine already puts a muzzle flash
               anim at the firing coordinate, which is why nothing else is needed. */
            if (g_cannonFlash && !strcmp(rec.name, "MUZZLE_FLASH")) {
                /* BURST-GATED, exactly as efx_fire_slot gates a real slot: emit only
                   for the source's own burst window and decay the count by its
                   multiplier each tick. Without this the flash emitted 6 particles on
                   EVERY stage of a 10-stage anim -- 61 per shot instead of 10, which
                   is a jet, not a flash. It is the same arithmetic as the ROM path so
                   an authored source behaves like a decoded one. */
                const EfxSourceRec& cs = EFX_CANNON_FLASH;
                const int k = an.stage;                 /* ticks since the shot */
                if (k < (int)cs.burst) {
                    float count = cs.ppt;
                    for (int j = 0; j < k; j++) count *= cs.mult;
                    const int n = (int)count;
                    if (n > 0) {
                        /* AT THE MUZZLE, NOT AT THE TANK. This used to emit at
                           (an.wx, ground + 0.10, an.wz), and an anim's own coordinate is
                           the FIRER'S centre: the engine puts ANIM_MUZZLE_FLASH on the
                           object that fired, not at the end of its gun. A Medium Tank's
                           hull spans 0 to 0.167 cells, so every flash was emitted inside
                           it, and the particle pass is depth TESTED against geometry
                           already drawn. 260 flashes in a 400-tick battle, none of them
                           visible, which is what was reported twice.

                           The host resolves the muzzle from the anim's attref (the
                           firer's engine heap slot) and the turret's own geometry. If it
                           cannot -- no such object, or a weapon with no turret -- the old
                           position stands, because a flash at the vehicle is still better
                           than no flash at all. */
                        float mx = an.wx, my = efx_ground(an.wx, an.wz) + 0.10f, mz = an.wz;
                        if (g_efxMuzzleAt && an.attref >= 0)
                            g_efxMuzzleAt(an.attref, an.attkind, &mx, &my, &mz);
                        efx_emit(cs, mx, my, mz,
                                 n, (unsigned)key, la.bornFrame, 0, k);
                        g_efxRecipeParts[an.name] += n;
                    }
                }
                g_efxRecipeName[an.name] = "CannonFlash_OURS";
            } else {
                g_efxEmpty[an.name]++;
            }
            continue;
        }
        /* One FRAG3 per dead building, not one per occupied cell: the DOS engine emits an
           FBALL1 for every cell of the footprint and the console emits a single anim. */
        if (fbKey >= 0) {
            if (fbFired.find(fbKey) != fbFired.end()) continue;
            fbFired.insert(fbKey);
        }

        /* ARM OR DISARM THE GHOST CLOCK (see EfxGhost for the whole account and for why
           the scope is this narrow). Armed on an anim the engine cannot count frames for,
           showing at stage 0, whose recipe holds a continuous source; re-armed on every
           such sighting, so the tail always begins after the LAST one. Disarmed the
           moment the engine shows this key above stage 0, because then the engine is
           driving the anim and the fire loop below is already right.

           THE POSITION IN THIS LOOP IS PART OF THE RULE: a ghost is armed exactly where
           the engine path fires, after both `continue`s above. The rubble dedupe just
           above fires ONE anim per dead building footprint and skips its duplicates, and
           an arm placed before it would have given every skipped duplicate a ghost of its
           own -- reinstating, one tick later, the very multiplication the dedupe exists
           to stop. */
        if (an.stages == 0 && an.biggest > 0 && an.stage == 0
            && efx_recipe_continuous(rec)) {
            EfxGhost g;
            g.an        = an;
            g.key       = key;
            g.recipe    = r;
            g.bornFrame = frame;
            g.biggest   = an.biggest;
            g_efxGhost[std::make_pair(key, an.attref)] = g;
        } else {
            g_efxGhost.erase(std::make_pair(key, an.attref));
        }

        for (int sI = 0; sI < (int)rec.nslot; sI++)
            efx_fire_slot(an, rec.slot[sI], sI, (unsigned)key, la.bornFrame, rec.name);
    }
    g_efxAnimLive.swap(seen);

    /* --- the art-less anims' own clock -------------------------------------- */
    for (std::map<std::pair<int, int>, EfxGhost>::iterator gi = g_efxGhost.begin();
         gi != g_efxGhost.end(); ) {
        EfxGhost& g = gi->second;
        const int st = frame - g.bornFrame;      /* engine ticks, never a wall clock */
        if (st == 0) {                           /* armed this frame: the loop above  */
            ++gi;                                /* already fired the engine's stage 0 */
            continue;
        }
        if (st < 0 || st > g.biggest) {          /* biggest+1 emissions, then done.   */
            g_efxGhost.erase(gi++);              /* st < 0 cannot happen (a backwards */
            continue;                            /* clock clears the pool), so it is  */
        }                                        /* dropped rather than left forever. */
        EfxAnim an = g.an;                       /* the sighting, restaged            */
        an.stage = st;
        g_efxSpawnHouse = an.chunkHouse;
        const EfxRecipe& rec = EFX_RECIPE[g.recipe];
        for (int sI = 0; sI < (int)rec.nslot; sI++) {
            /* CONTINUOUS SLOTS ONLY. A burst slot fired the FIRST tick of its window on
               the engine's stage 0 and is left exactly as it was, truncated; see EfxGhost
               for the measurement that decided this; the evidence that would settle it
               has not been gathered. */
            if (!efx_slot_continuous(rec.slot[sI]))
                continue;
            /* The ANIM key, not the map key: the particle hash must stay the same pure
               function of (anim, bornFrame, slot, tick) it is on the engine path. Two
               ghosts can share an anim key only if they were born on different frames,
               so the hash still separates them. */
            efx_fire_slot(an, rec.slot[sI], sI, (unsigned)g.key, g.bornFrame, rec.name);
        }
        ++gi;
    }

    /* --- integrate ---------------------------------------------------------- */
    for (int t = 0; t < dt; t++)
        efx_integrate_once();
}

/* ------------------------------------------------------------------------- drawing */

/* One camera-facing quad, centred at (x, y, z), half-width hw, half-height hh, in the
   billboard basis the caller passes down (the same basis draw_sprite uses). */
static void efx_quad(const float* R, const float* U,
                     float x, float y, float z, float hw, float hh,
                     float r, float g, float b, float a)
{
    const float rx = R[0] * hw, ry = R[1] * hw, rz = R[2] * hw;
    const float ux = U[0] * hh, uy = U[1] * hh, uz = U[2] * hh;
    glColor4f(r, g, b, a);
    glVertex3f(x - rx - ux, y - ry - uy, z - rz - uz);
    glVertex3f(x + rx - ux, y + ry - uy, z + rz - uz);
    glVertex3f(x + rx + ux, y + ry + uy, z + rz + uz);
    glVertex3f(x - rx + ux, y - ry + uy, z - rz + uz);
}

/* The textured version: binds one frame of a sequence and draws it with the used
   sub-rect's UVs. Own glBegin/glEnd because glBindTexture is illegal inside one. */
static void efx_quad_tex(const EfxSeq* s, int frame, const float* R, const float* U,
                         float x, float y, float z, float hw, float hh, float alpha)
{
    if (frame < 0) frame = 0;
    if (frame >= s->frames) frame = s->frames - 1;
    const float u1 = (float)s->uw / (float)s->w;
    const float v1 = (float)s->uh / (float)s->h;
    const float rx = R[0] * hw, ry = R[1] * hw, rz = R[2] * hw;
    const float ux = U[0] * hh, uy = U[1] * hh, uz = U[2] * hh;
    glBindTexture(GL_TEXTURE_2D, s->gl[frame]);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, v1); glVertex3f(x - rx - ux, y - ry - uy, z - rz - uz);
    glTexCoord2f(u1, v1);   glVertex3f(x + rx - ux, y + ry - uy, z + rz - uz);
    glTexCoord2f(u1, 0.0f); glVertex3f(x + rx + ux, y + ry + uy, z + rz + uz);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x - rx + ux, y - ry + uy, z - rz + uz);
    glEnd();
}

/* ------------------------------------------------------------ the particle draw pass
   Four vertices straight into an already-open glBegin(GL_QUADS) with the texture
   already bound. Batching is the ONLY thing that matters on a Voodoo2: the pass
   buckets every live particle by (system, frame) -- at most 10 x 12 buckets, about 20
   in practice -- so it costs ~20 glBindTexture and ~10 glBlendFunc for the whole
   frame instead of one of each per particle. GL 1.1 immediate mode, GL_CLAMP,
   power-of-two textures, no extensions: the same rules as the rest of this file. */
static void efx_quad_batched(const EfxPart& p, const float* R, const float* U,
                             float u1, float v1, float alpha)
{
    /* SQUARE. The cartridge scales both corner axes by the same size and stretches the
       texture onto that square, whatever the art's own proportions are: the FireBall
       strip is 16x8 texels and the console draws it 2:1 STRETCHED vertically. We used to
       multiply the height by uh/uw, which squashed it 2:1 the other way -- so on top of
       the half-extent bug a fireball came out a quarter of the console's height. */
    const float hw = p.hw, hh = p.hw;
    const float rx = R[0] * hw, ry = R[1] * hw, rz = R[2] * hw;
    const float ux = U[0] * hh, uy = U[1] * hh, uz = U[2] * hh;
    glColor4f((float)p.r / 255.0f, (float)p.g / 255.0f, (float)p.b / 255.0f, alpha);
    glTexCoord2f(0.0f, v1);   glVertex3f(p.x - rx - ux, p.y - ry - uy, p.z - rz - uz);
    glTexCoord2f(u1,   v1);   glVertex3f(p.x + rx - ux, p.y + ry - uy, p.z + rz - uz);
    glTexCoord2f(u1,   0.0f); glVertex3f(p.x + rx + ux, p.y + ry + uy, p.z + rz + uz);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(p.x - rx + ux, p.y - ry + uy, p.z - rz + uz);
}

/* Scratch for the bucket sort. Static so the pass allocates nothing per frame. */
static short g_efxOrder[EFX_MAX_PART + EFX_MAX_CHUNK];
static short g_efxBKey[EFX_MAX_PART + EFX_MAX_CHUNK];
#define EFX_NBUCKET (10 * 16)

/* Draw order: the alpha-blended systems first so the additive fire and glow sit on
   top of the smoke, which is what the reference frames show (p080: the chunks and
   their embers read in FRONT of the smoke column, the gold core in front of both). */
static const unsigned char EFX_DRAW_ORDER[10] = { 6, 2, 3, 8, 0, 1, 4, 5, 7, 9 };

static int g_efxDrawn = 0;             /* particles that survived the cull last frame */

/* THE DEBRIS CHUNKS, drawn as real meshes.

   AFTER the sprite particles, which is the console's own order (RAM 0x80056C48 runs the
   eight sprite systems, then the state restore, then VehicleDamage and StructureDamage
   last). It matters: the chunks WRITE depth and the sprites do not, so drawing chunks
   first would let an ember standing behind one paint straight over it.

   A pack from before the chunk bake resolves no mesh; those chunks keep the ember
   sprite, which is why they stay in the sprite bucket in that case. */
static void efx_draw_chunk_meshes(void)
{
    for (int i = 0; i < g_efxNChunk; i++) {
        const EfxPart& c = g_efxChunk[i];
        if (c.scale <= 0.0f) continue;              /* shrunk away before it died */
        if (efx_hidden(c.x, c.z)) continue;
        const int mi = efx_chunk_model(c.family, c.mesh);
        if (mi < 0) continue;
        /* The cartridge takes three bytes of the particle NODE's own address; we take
           three bytes of its deterministic seed, so the tumble is stable across runs. */
        const float lifeLeft = (float)(c.life - c.age);
        const float bx = (float)((c.seed >> 8)  & 0xFFu);
        const float by = (float)((c.seed >> 4)  & 0xFFu);
        const float bz = (float)((c.seed)       & 0xFFu);
        efx_chunk_draw_model(mi, c.x, c.y, c.z,
                             lifeLeft * 0.05f * (bx - 127.0f),
                             lifeLeft * 0.05f * (by - 127.0f),
                             lifeLeft * 0.05f * (bz - 127.0f),
                             c.scale, c.house);
    }
}

/* ---- THE NUCLEAR STRIKE ------------------------------------------------------------
   ANIM_ATOM_BLAST arrives on the wire as type 60 (our brain's own AnimType ordinal --
   defines.h:1486; efx_recipes.h remaps it to the cartridge's 53 for the particle table,
   which is a DIFFERENT enum and not a contradiction). Until now it drew a light and a
   handful of sparks and nothing else, because the cartridge's mushroom is not sprite art
   at all: it is four animated script models, baked here as NUKEFX.

   THE CLOCK IS THE ENGINE'S, not a timer of ours, and the numbers below are measured off
   a real strike on SCB70EA rather than assumed:

     EFXDUMP|ANIM|ATOMSFX|id=0|type=60|cell=14.5,55.5|stage=0/0 |loops=1|biggest=19
     EFXDUMP|ANIM|ATOMSFX|id=0|type=60|cell=14.5,55.5|stage=9/0 |loops=0|biggest=19
     EFXDUMP|ANIM|ATOMSFX|id=0|type=60|cell=14.5,55.5|stage=19/0|loops=0|biggest=19

   `stages` is 0 because ATOMSFX is ART-LESS -- it has no DOS shape file, so the engine's
   Get_Build_Frame_Count(NULL) returns 0 -- but `stage` still advances and `biggest` (19)
   is the real last frame. So stage/biggest is the strike's own progress in [0,1] and the
   clip frame follows from it. Nothing here counts renderer frames, which keeps two
   --shot runs byte-identical and keeps the mushroom in step with the engine's own blast
   whatever the frame rate does.

   ONE MORE THING THIS MUST NOT DO: the strike is a single event, so a second nuke landing
   while the first is still rising gets its own anim id and its own stage, and this loop is
   stateless precisely so that works without a table to keep in step. */
static void efx_draw_nuke_meshes(void)
{
    const int mi = efx_nuke_model();
    if (mi < 0)
        return;                       /* a pack from before the nuke bake: draw nothing */
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        if (strcmp(a.name, "ATOMSFX"))
            continue;
        /* NO SHROUD CULL ON A SUPERWEAPON. Every other effect in this file is hidden
           when its cell is under shroud, which is right: an explosion in the fog is
           somebody else's business and showing it would leak the enemy's position.
           A super weapon is the opposite case. The player CHOSE this cell, aimed at
           it through the targeting cursor, and spent a charge on it, and the engine
           fires it whether the cell is mapped or not -- Place_Special_Blast has no
           visibility test of any kind (house.cpp). Culling the draw here meant the
           charge was spent, the strike happened, and nothing appeared, which is
           indistinguishable from a dead button. the project owner, 26 Aug 2026: "When trying to
           use a Superweapon on Fog of War, it doesnt work. It should launch the
           superweapon, regardless of wether theres fog of war (Shroud) or not."
           Measured before and after: same script, same camera, same target cell
           30,47, same engine pixels 372,612 -- with --noshroud it drew three EFXION
           frames and with the shroud on it drew none. */
        const int last = a.biggest > 0 ? a.biggest : 19;
        /* THE CARTRIDGE'S OWN CONSTANT, not a normalisation of ours. The kind-12 nuke arm
           multiplies the stage by the float at RAM 0x800049B4 to index its clip -- read
           back from the ROM as 1.1111112 (10/9) at ROM 0x0055B4, with the mul.s at ROM
           0x04EDE8 -- exactly as the ion arm in the same block doubles it.
           This used to be stage/biggest * (animFrames-1) = stage * 30/19 = 1.5789, which
           ran the whole mushroom 42% FAST: the cap swell, the stem stretch and the
           collar's arrival at frame 8 all happened early and the last third of the clip
           was compressed away. The obvious objection -- that our ATOMSFX reports biggest
           19 rather than the console's 27, so the clip would truncate -- does not hold:
           the anim was measured alive to stage 44, and stage * 10/9 reaches frame 30 at
           stage 27 with room to spare. G97 could not see it; it only asserts t1 > t0. */
        const float frame = (float)a.stage * 1.1111112f;
        /* ONE LINE PER STRIKE PER CLIP FRAME, on a prefix of its own so it disarms no
           existing anchored pattern. It is emitted from the DRAW rather than from
           efxdump on purpose: a gate that only read the anim would go green on a build
           where the mesh never reached draw_mesh at all, which is precisely the state
           this commit is fixing. Bounded at one line per integer frame (31 at most) and
           keyed on the anim id, so two strikes at once each get their own run and a
           re-sighting of the same frame stays silent. The bucket comes from the engine's
           own stage, so it is identical across two --shot runs. */
        /* REPORTED ONLY IF IT ACTUALLY DREW, which is why the draw comes first. Past the
           end of its clip the mushroom fades out and then stops being drawn entirely,
           while the anim itself lives on for another dozen stages; a line printed ahead
           of the draw would go on claiming a mushroom that is no longer there, and a gate
           reading it would believe the claim. */
        const int key = a.id | (efx_fx_gen(a.id, a.stage) << 16);
        if (efx_nuke_draw_model(mi, a.wx, a.wz, frame)
            && g_efxNukeSaid.insert(std::make_pair(key, (int)frame)).second)
            printf("EFXNUKE|id=%d|mesh=%d|stage=%d/%d|frame=%.3f\n",
                   a.id, mi, a.stage, last, frame);
    }
}


/* ---- THE ION CANNON ------------------------------------------------------------------
   ANIM_ION_CANNON is type 59 on the wire -- our brain's own AnimType ordinal
   (defines.h:1485), not the cartridge's 52, which is what efx_recipes.h remaps it to for
   the particle table. Two enums, not a contradiction.

   Until now this drew nothing at all, and unusually that was CORRECT for the particles:
   recipe index 52 is 160 bytes of zero in the ROM, re-dumped byte for byte, so the ion
   cannon genuinely spawns no particles on the console either. Its whole appearance is
   geometry -- a thin bright column dropping out of the sky while three blue-white shock
   rings fire top down at descending heights and expand and dissolve -- and none of that
   geometry was baked. gates.sh's G85 asserted `firemap=0` for exactly this reason and is
   rewritten in the same commit that made it false.

   Stage doubles into the snapshot index; see efx_ion_model for the measurement. */
static void efx_draw_ion_meshes(void)
{
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        if (strcmp(a.name, "IONSFX"))
            continue;
        /* NO SHROUD CULL ON A SUPERWEAPON. Every other effect in this file is hidden
           when its cell is under shroud, which is right: an explosion in the fog is
           somebody else's business and showing it would leak the enemy's position.
           A super weapon is the opposite case. The player CHOSE this cell, aimed at
           it through the targeting cursor, and spent a charge on it, and the engine
           fires it whether the cell is mapped or not -- Place_Special_Blast has no
           visibility test of any kind (house.cpp). Culling the draw here meant the
           charge was spent, the strike happened, and nothing appeared, which is
           indistinguishable from a dead button. the project owner, 26 Aug 2026: "When trying to
           use a Superweapon on Fog of War, it doesnt work. It should launch the
           superweapon, regardless of wether theres fog of war (Shroud) or not."
           Measured before and after: same script, same camera, same target cell
           30,47, same engine pixels 372,612 -- with --noshroud it drew three EFXION
           frames and with the shroud on it drew none. */
        /* ONE clamp, and the renderer's own, so the printed snapshot and the drawn one
           cannot disagree. They used to be written out twice with a bare literal 9 in the
           printf, which is how a table of ten and a clamp of nine drifted apart. */
        int snap = a.stage * 2;
        if (snap < 0) snap = 0;
        if (snap > ION_SNAPSHOTS - 1) snap = ION_SNAPSHOTS - 1;
        const int mi = efx_ion_model(snap);
        const int key = a.id | (efx_fx_gen(a.id, a.stage) << 16);
        if (g_efxIonSaid.insert(std::make_pair(key, snap)).second)
            printf("EFXION|id=%d|mesh=%d|stage=%d/%d|snap=%d\n",
                   a.id, mi, a.stage, a.stages, snap);
        if (mi < 0)
            continue;      /* reported first, so a pack with no ION snapshots says so */
        efx_ion_draw_model(mi, a.wx, a.wz);
    }
}


static void efx_draw_particles(const float* R, const float* U)
{
    const int nTot = g_efxNPart + g_efxNChunk;
    if (nTot <= 0) { g_efxDrawn = 0; return; }

    static int bucket[EFX_NBUCKET + 1];
    memset(bucket, 0, sizeof(bucket));

    /* Pass one: bucket key per particle, and the shroud cull. A particle standing in
       a shroud-hidden cell is not drawn (the module predates the shroud pass and
       would otherwise flash above the black); the pool still holds it, because the
       pool is engine truth and the draw is the view. */
    int n = 0;
    for (int i = 0; i < nTot; i++) {
        /* WHICH POOL a particle came from is what makes it a chunk, and that is now the
           only thing asked. It used to be asked by stamping sys = 9 on every chunk and
           testing for 9 -- and 9 is ALSO a real particle system, FLTHROW, the
           flamethrower's own. So every flame sprite the engine raised was mistaken for a
           debris chunk, had efx_chunk_model() called on it against uninitialised
           family/mesh bytes, and was skipped. That is why the Flame Troopers threw
           nothing: the particles existed and the draw dropped them. */
        const bool isChunk = (i >= g_efxNPart);
        const EfxPart& p = isChunk ? g_efxChunk[i - g_efxNPart] : g_efxPart[i];
        /* A chunk that RESOLVES to a mesh is drawn after the sprites instead. One that
           does not -- a pack from before the chunk bake -- must stay in this bucket and
           keep the ember sprite, or it would simply vanish. Getting that wrong made
           every chunk invisible on an old pack, which is exactly the kind of silent
           regression the fallback exists to prevent. */
        if (isChunk && efx_chunk_model((int)p.family, (int)p.mesh) >= 0)
            continue;
        if (p.hw <= 0.0f || efx_hidden(p.x, p.z))
            continue;
        /* The ember art, for a chunk falling back to a sprite. */
        const int sysIdx = isChunk ? 8 : (int)p.sys;
        const EfxSeq* s = efx_seq(EFX_SYS[sysIdx].seq);
        if (!s || s->frames <= 0)
            continue;
        /* THE CARTRIDGE STRETCHES THE WHOLE STRIP ACROSS THE PARTICLE'S LIFE. There is
           no per-template frame RATE: the byte we used to read as one (+0x31) is the
           ALPHA RAMP SLOPE, see the alpha law below. Both the triangle path
           (RAM 0x80050060) and the quad path (RAM 0x80050298) compute the frame as
               frame = (lifeTotal - lifeLeft) * nframes / lifeTotal
           and apply it as a T offset of frame * 32 texels down a vertical film strip. */
        int fr = (p.life > 0) ? (p.age * s->frames) / p.life : 0;
        if (fr >= s->frames) fr = s->frames - 1;
        if (fr < 0) fr = 0;
        g_efxBKey[n] = (short)(sysIdx * 16 + fr);
        g_efxOrder[n] = (short)i;
        bucket[g_efxBKey[n]]++;
        n++;
    }
    g_efxDrawn = n;
    if (!n) return;

    /* Counting sort into bucket order: O(n) once, instead of ten scans of the pool. */
    int sum = 0;
    for (int k = 0; k < EFX_NBUCKET; k++) { const int c = bucket[k]; bucket[k] = sum; sum += c; }
    bucket[EFX_NBUCKET] = sum;
    static short sorted[EFX_MAX_PART + EFX_MAX_CHUNK];
    static int   cursor[EFX_NBUCKET];
    memcpy(cursor, bucket, sizeof(cursor));
    for (int i = 0; i < n; i++)
        sorted[cursor[g_efxBKey[i]]++] = g_efxOrder[i];

    glEnable(GL_TEXTURE_2D);
    int lastAdditive = -1;
    for (int oi = 0; oi < 10; oi++) {
        const int sys = EFX_DRAW_ORDER[oi];
        const EfxSeq* s = efx_seq(EFX_SYS[sys].seq);
        if (!s) continue;
        const float u1 = (float)s->uw / (float)s->w;
        const float v1 = (float)s->uh / (float)s->h;
        for (int fr = 0; fr < s->frames && fr < 16; fr++) {
            const int k = sys * 16 + fr;
            const int lo = bucket[k], hi = bucket[k + 1 > EFX_NBUCKET ? EFX_NBUCKET : k + 1];
            if (hi <= lo) continue;
            if ((int)EFX_SYS[sys].additive != lastAdditive) {
                lastAdditive = EFX_SYS[sys].additive;
                glBlendFunc(GL_SRC_ALPHA, lastAdditive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            }
            glBindTexture(GL_TEXTURE_2D, s->gl[fr]);
            glBegin(GL_QUADS);
            for (int j = lo; j < hi; j++) {
                const int i = sorted[j];
                const EfxPart& p = (i < g_efxNPart) ? g_efxPart[i]
                                                    : g_efxChunk[i - g_efxNPart];
                /* The source's own stored alpha, held flat and then ramped off over
                   the last quarter of the life. This is a POP GUARD, not a decay
                   curve: the cartridge's art already carries the decay -- the
                   FireBall burst shrinks to nothing across its twelve frames, the
                   Burn_Smoke puff darkens to black across its six, the Debris ember
                   sooties across its eight -- and an extra falloff on top of that was
                   what turned the FRAG1 fireball into a pale wash where c078 shows a
                   dense gold glow (both were looked at). */
                /* The cartridge's own alpha law (RAM 0x8004FFC8):
                       alpha = min(lifeLeft * slope, cap)
                   with slope = the visual record's +0x31 and cap = its +0x32. So a
                   particle is at FULL alpha until lifeLeft * slope drops below the cap
                   and then fades linearly to nothing. The old code here was a "pop
                   guard" of our own invention -- a flat alpha with a falloff over the
                   last quarter of the life -- which is a different curve entirely. */
                int ai = (int)(p.life - p.age) * (int)p.arate;
                if (ai > (int)p.a) ai = (int)p.a;
                if (ai < 0) ai = 0;
                efx_quad_batched(p, R, U, u1, v1, (float)ai / 255.0f);
            }
            glEnd();
        }
    }
    glDisable(GL_TEXTURE_2D);
}

/* Draw one anim from the cartridge art. Returns the sequence it used (for the
   dump), or NULL when this anim has no textured recipe and the procedural path
   should handle it. A recipe with a NULL sequence draws nothing on purpose.

   LEGACY PATH. This is the pre-particle-system draw: one billboard per anim. It runs
   only when efx_step() has never been called, i.e. when cnc_eyes.cpp has not been
   given its one-line tick hook, so that this header can land on its own without
   changing what is on screen. Once the pool has a clock, efx_draw_particles() above
   replaces it entirely. */
static const char* efx_draw_tex(const EfxAnim& a, const float* R, const float* U)
{
    const EfxArt* art = efx_art(a.name);
    if (!art)
        return NULL;
    if (!art->seq)
        return "none";                       /* the N64 shows nothing, so do we */
    const EfxSeq* s = efx_seq(art->seq);
    if (!s)
        return NULL;
    const EfxClass c = efx_classify(a.name);
    const float t = efx_life(a, c);
    const float cells = (float)a.size / 24.0f;
    const float aspect = (float)s->uh / (float)s->uw;
    const float hw = art->size * (cells > 0.f ? cells : 1.f);
    const float hh = hw * aspect;

    glBlendFunc(GL_SRC_ALPHA, art->additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

    const float gy = efx_ground(a.wx, a.wz);
    if (art->oneshot) {
        const int fr = (int)(t * (float)(s->frames - 1) + 0.5f);
        const float fade = 1.0f - t * t * t;             /* frames carry the decay */
        /* explosions grow a touch as they run; smoke puffs drift up instead */
        const float grow = art->additive ? (0.85f + 0.35f * t) : 1.0f;
        const float rise = art->additive ? 0.05f * t : 0.30f * t;
        efx_quad_tex(s, fr, R, U, a.wx, gy + 0.22f + hh + rise, a.wz,
                     hw * grow, hh * grow, fade);
    } else {
        /* looping: the engine stage drives the cartridge frames directly, so a
           given input log always shows the same flames. Smoke rises per loop. */
        const int fr = a.stage % s->frames;
        if (art->additive) {
            efx_quad_tex(s, fr, R, U, a.wx, gy + 0.18f + hh, a.wz, hw, hh, 0.95f);
        } else {
            const float ph = (float)(a.stage % 16) / 16.0f;
            efx_quad_tex(s, fr, R, U, a.wx, gy + 0.25f + hh + 0.55f * ph, a.wz,
                         hw * (1.0f + 0.3f * ph), hh * (1.0f + 0.3f * ph),
                         0.65f * (1.0f - 0.55f * ph) + 0.15f);
        }
    }

    /* the N64 recipe's companion smoke (Napalm_Smoke_*, Smoke_Burn_*, FRAG smoke
       column): one Burn_Smoke puff above the core, alpha-blended, rising. */
    if (art->smoke) {
        const EfxSeq* sm = efx_seq("SMOKE");
        if (sm) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            const float ph = art->oneshot ? t : (float)(a.stage % 16) / 16.0f;
            const int fr = art->oneshot ? (int)(ph * (float)(sm->frames - 1) + 0.5f)
                                        : (a.stage / 2) % sm->frames;
            const float sh = hw * 0.8f;
            efx_quad_tex(sm, fr, R, U, a.wx, gy + 0.30f + 2.0f * hh + 0.8f * ph, a.wz,
                         sh, sh, 0.55f * (1.0f - 0.6f * ph));
        }
    }
    return art->seq;
}

/* Draw every live effect. Call AFTER the infantry sprite pass with the same billboard
   basis, so effects composite over units. Leaves GL state as it found it (blend off,
   depth write on). */
/* Which art each live anim name resolved to this frame, for efxdump: seq name,
   "none" (N64 draws nothing), or "proc" (procedural placeholder). */
static std::map<std::string, std::string> g_efxArtUsed;

static void efx_draw(const float* bbRight, const float* bbUp)
{
    if (!g_efxAnnounced) {
        g_efxAnnounced = true;
        if (g_efxHaveArt)
            fprintf(stderr,
                    "effects: drawing with the cartridge's particle textures "
                    "(efx.pack). The engine anim is a TRIGGER for the N64 particle "
                    "recipes; only the eight ANIM_CHEM_*, which the cartridge's table "
                    "has no entry for, still draw a procedural billboard, and they are "
                    "listed in efxdump. If the host never calls efx_step() the pool "
                    "has no clock and the old one-billboard-per-anim path runs "
                    "instead (efxdump reports pool=no-clock). Infantry deaths are "
                    "drawn by draw_sprite (the DOS die sequences when "
                    "dosinfantry.pack is loaded, else the N64 DETH strips).\n");
        else
            fprintf(stderr,
                    "effects: drawing engine anims/bullets as APPROXIMATE procedural "
                    "billboards (no efx.pack -- run bake_efx.py for the real N64 "
                    "particle art). Infantry deaths are drawn by draw_sprite (the "
                    "DOS die sequences when dosinfantry.pack is loaded, else the "
                    "N64 DETH strips).\n");
    }
    if (g_efxAnims.empty() && g_efxBullets.empty() &&
        g_efxNPart == 0 && g_efxNChunk == 0)
        return;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    std::vector<const EfxAnim*> leftovers;
    if (g_efxPoolLive && g_efxHaveArt) {
        /* THE PARTICLE PASS. The anims themselves draw nothing at all now: they are
           triggers, and everything on screen is a particle the pool integrated on the
           engine's own tick. Only the anims the cartridge has no recipe for at all
           (the eight ANIM_CHEM_*) still take the procedural path below. */
        efx_draw_particles(bbRight, bbUp);
        efx_draw_chunk_meshes();
        efx_draw_nuke_meshes();
        efx_draw_ion_meshes();
        for (size_t i = 0; i < g_efxAnims.size(); i++) {
            const EfxAnim& a = g_efxAnims[i];
            if (!efx_proc_fallback(a)) {
                const int r = (a.type >= 0 && a.type < EFX_ANIM2RECIPE_N)
                                  ? EFX_ANIM2RECIPE[a.type] : -1;
                g_efxArtUsed[a.name] =
                    (r >= 0 && EFX_RECIPE[r].nslot == 0) ? "empty-by-design"
                                                         : "particles";
                continue;
            }
            g_efxArtUsed[a.name] = "proc";
            if (!efx_hidden(a.wx, a.wz))
                leftovers.push_back(&a);
        }
    } else if (g_efxHaveArt) {
        /* Legacy: one textured billboard per anim. See efx_draw_tex's header -- this
           is what runs when the pool has no engine-tick clock. */
        glEnable(GL_TEXTURE_2D);
        for (size_t i = 0; i < g_efxAnims.size(); i++) {
            const EfxAnim& a = g_efxAnims[i];
            if (efx_hidden(a.wx, a.wz))
                continue;
            const char* used = efx_draw_tex(a, bbRight, bbUp);
            if (used)
                g_efxArtUsed[a.name] = used;
            else {
                g_efxArtUsed[a.name] = "proc";
                leftovers.push_back(&a);
            }
        }
        glDisable(GL_TEXTURE_2D);
    } else {
        for (size_t i = 0; i < g_efxAnims.size(); i++) {
            const EfxAnim& a = g_efxAnims[i];
            if (!efx_hidden(a.wx, a.wz))
                leftovers.push_back(&a);
        }
    }

    /* Additive pass: sparks, muzzle flashes, explosion cores, fire. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_QUADS);
    for (size_t i = 0; i < leftovers.size(); i++) {
        const EfxAnim& a = *leftovers[i];
        const EfxClass c = efx_classify(a.name);
        const float gy = efx_ground(a.wx, a.wz);
        const float t = efx_life(a, c);              /* 0 birth .. 1 spent */
        /* The DOS art cell is 24 px; anim Size is its max dimension in those pixels,
           so Size/24 is the effect's footprint in cells. */
        const float cells = (float)a.size / 24.0f;

        switch (c) {
        case EFXC_SPARK: {
            /* One hot pixel-cluster that pops and dies. */
            const float s = 0.10f + 0.06f * t;
            efx_quad(bbRight, bbUp, a.wx, gy + 0.18f, a.wz, s, s,
                     1.0f, 0.95f, 0.55f, 0.9f * (1.0f - t));
        } break;
        case EFXC_MUZZLE: {
            /* Short bright flash at the gun. Two nested quads read as a star. */
            const float s = 0.16f * cells + 0.10f;
            efx_quad(bbRight, bbUp, a.wx, gy + 0.30f, a.wz, s, s * 0.55f,
                     1.0f, 0.85f, 0.30f, 0.85f * (1.0f - 0.5f * t));
            efx_quad(bbRight, bbUp, a.wx, gy + 0.30f, a.wz, s * 0.45f, s * 0.45f,
                     1.0f, 1.0f, 0.85f, 0.9f);
        } break;
        case EFXC_EXPLOSION: {
            /* Fireball: outer shell grows and cools, core shrinks and stays hot. */
            const float grow = 0.25f + 0.75f * t;
            const float ro = 0.55f * cells * grow;
            const float fade = 1.0f - t * t;
            efx_quad(bbRight, bbUp, a.wx, gy + 0.25f + 0.15f * t, a.wz, ro, ro,
                     1.0f, 0.45f + 0.25f * (1.0f - t), 0.10f, 0.75f * fade);
            efx_quad(bbRight, bbUp, a.wx, gy + 0.22f, a.wz, ro * 0.5f * (1.0f - 0.4f * t),
                     ro * 0.5f * (1.0f - 0.4f * t),
                     1.0f, 0.95f, 0.55f, 0.9f * fade);
        } break;
        case EFXC_FIRE: {
            /* Lingering flame: loops while the engine keeps it alive, flicker keyed
               off the engine stage so it is deterministic for a given input log. */
            const float fl = 0.85f + 0.15f * efx_pulse(a, 7);
            const float s = 0.30f * cells * fl + 0.08f;
            efx_quad(bbRight, bbUp, a.wx, gy + s * 0.6f, a.wz, s * 0.6f, s,
                     1.0f, 0.55f, 0.12f, 0.8f);
            efx_quad(bbRight, bbUp, a.wx, gy + s * 0.45f, a.wz, s * 0.3f, s * 0.6f,
                     1.0f, 0.9f, 0.4f, 0.85f);
        } break;
        default:
            break;                    /* smoke + unknown drawn in the alpha pass */
        }
    }

    glEnd();

    /* Visible bullets. The cartridge draws these as tiny 3D MODELS, not billboards:
       its bullet-type table (CodeOverlay ROM 0x1693B0) maps 120MM/DRAGON/MISSILE/
       BOMBLET to model slots 92..95 (unified enum id + 10, the same rule as units)
       and -1 for 50CAL/FLAME, which are never drawn. The models ride the pack like
       every other mesh; efx_bullet_model/efx_bullet_draw_model live in cnc_eyes.cpp
       (they need draw_mesh and g_pack, which this header predates). A bullet name
       with no model in the pack falls back to the old procedural dart, loudly once,
       so an unexpected projectile is visible rather than missing. */
    for (size_t i = 0; i < g_efxBullets.size(); i++) {
        const EfxBullet& b = g_efxBullets[i];
        if (b.invis)
            continue;                 /* 50cal etc: the N64 table says -1, never drawn */
        if (efx_hidden(b.wx, b.wz))
            continue;
        /* TWO bugs used to live in this one line, and together they are why one saw
           only the smoke trail.
           (1) THE GROUND WAS COUNTED TWICE. efx_bullet_draw_model forwards its third
               argument to draw_mesh's YLIFT, and draw_mesh already stands the model on
               terrain_y under the anchor. Passing an ABSOLUTE height therefore drew the
               rocket a whole terrain height above its own trail -- 19 px apart on the
               measured repro. The rocket was not too small to see; it was not where you
               were looking.
           (2) The +0.30f was invented; see EFX_BULLET_LIFT above for the cartridge's
               own value and its evidence. */
        const float lift = EFX_BULLET_LIFT + (float)b.alt / 256.0f;   /* above ground   */
        const float y    = efx_ground(b.wx, b.wz) + lift;   /* absolute: the dart below */
        const int mi = efx_bullet_model(b.name);
        if (mi >= 0) {
            efx_bullet_draw_model(mi, b.wx, lift, b.wz, b.face);
            continue;
        }
        if (g_efxBulletMiss.find(b.name) == g_efxBulletMiss.end()) {
            g_efxBulletMiss[b.name] = 1;
            fprintf(stderr, "effects: bullet %s has no cartridge model in the pack; "
                            "drawing the procedural dart\n", b.name);
        }
        const float ang = (float)b.face * (float)(M_PI * 2.0 / 256.0);
        const float dx = sinf(ang), dz = -cosf(ang);
        const float s = 0.07f;
        glBegin(GL_QUADS);
        efx_quad(bbRight, bbUp, b.wx, y, b.wz, s, s, 1.0f, 1.0f, 0.85f, 0.95f);
        efx_quad(bbRight, bbUp, b.wx - dx * 0.18f, y, b.wz - dz * 0.18f,
                 s * 0.7f, s * 0.7f, 1.0f, 0.7f, 0.25f, 0.5f);
        glEnd();
    }

    /* Alpha pass: smoke and the honest magenta "no idea what this is" marker. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    for (size_t i = 0; i < leftovers.size(); i++) {
        const EfxAnim& a = *leftovers[i];
        const EfxClass c = efx_classify(a.name);
        const float gy = efx_ground(a.wx, a.wz);
        const float cells = (float)a.size / 24.0f;
        if (c == EFXC_SMOKE) {
            /* Rises and thins, then loops: the engine keeps SMOKE_M alive for as
               long as the vehicle burns, so the smoke must not fade out for good. */
            const float t = efx_pulse(a, 16);
            const float s = 0.20f * cells + 0.10f + 0.15f * t;
            efx_quad(bbRight, bbUp, a.wx, gy + 0.25f + 0.55f * t, a.wz, s, s,
                     0.55f, 0.55f, 0.55f, 0.50f * (1.0f - 0.7f * t) + 0.10f);
        } else if (c == EFXC_UNKNOWN) {
            g_efxUnknown[a.name]++;
            efx_quad(bbRight, bbUp, a.wx, gy + 0.3f, a.wz, 0.2f, 0.2f,
                     1.0f, 0.0f, 1.0f, 0.9f);
        }
    }
    glEnd();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glColor3f(1.0f, 1.0f, 1.0f);
}

/* ------------------------------------------------------------------ script support */

static void efx_part_dump(void);

/* "efxdump": everything live right now plus the cumulative spawn table, one
   machine-readable line each, so a harness can assert on effects without pixels. */
static void efx_dump(void)
{
    printf("EFXDUMP-BEGIN live_anims=%zu live_bullets=%zu deaths_seen=%d\n",
           g_efxAnims.size(), g_efxBullets.size(), g_efxDeathsSeen);
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        printf("EFXDUMP|ANIM|%s|id=%d|type=%d|cell=%.1f,%.1f|stage=%d/%d|loops=%d|"
               "attref=%d|owner=%d|class=%d|biggest=%d\n",
               a.name, a.id, a.type, a.wx, a.wz, a.stage, a.stages, a.loops,
               a.attref, a.owner, (int)efx_classify(a.name), a.biggest);
    }
    /* The art-less anims the renderer is still clocking after the engine dropped them.
       stage=k/biggest here is SYNTHETIC (frame - bornFrame); the engine's own stage on
       the line above is 0/0 for exactly these, which is the bug this exists to fix. A
       gate can read biggest off either line and go red if the brain stops exporting it. */
    for (std::map<std::pair<int, int>, EfxGhost>::const_iterator it = g_efxGhost.begin();
         it != g_efxGhost.end(); ++it)
        printf("EFXGHOST|%s|id=%d|type=%d|cell=%.1f,%.1f|stage=%d/%d|born=%d|attref=%d\n",
               it->second.an.name, it->second.an.id, it->second.an.type,
               it->second.an.wx, it->second.an.wz,
               g_efxStepFrame - it->second.bornFrame, it->second.biggest,
               it->second.bornFrame, it->second.an.attref);
    for (size_t i = 0; i < g_efxBullets.size(); i++) {
        const EfxBullet& b = g_efxBullets[i];
        printf("EFXDUMP|BULLET|%s|id=%d|cell=%.1f,%.1f|face=%d|alt=%d|invis=%d\n",
               b.name, b.id, b.wx, b.wz, b.face, b.alt, b.invis);
    }
    for (size_t i = 0; i < g_efxDying.size(); i++) {
        const EfxDying& d = g_efxDying[i];
        printf("EFXDUMP|DYING|id=%d|cell=%.1f,%.1f|dostage=%d\n",
               d.id, d.wx, d.wz, d.stage);
    }
    for (std::map<std::string, int>::const_iterator it = g_efxSpawns.begin();
         it != g_efxSpawns.end(); ++it)
        printf("EFXSEEN|%s|%d\n", it->first.c_str(), it->second);
    /* Which art each drawn anim name resolved to: a cartridge sequence name,
       "none" (the N64 deliberately draws nothing there), or "proc" (no N64
       counterpart, procedural placeholder). The task's honest-remainders list. */
    for (std::map<std::string, std::string>::const_iterator it = g_efxArtUsed.begin();
         it != g_efxArtUsed.end(); ++it) {
        const EfxArt* art = efx_art(it->first.c_str());
        printf("EFXART|%s|%s|%s\n", it->first.c_str(), it->second.c_str(),
               art ? art->n64src : "no N64 counterpart");
    }

    /* ---- the particle pool ---- */
    printf("EFXPART|pool=%s|live=%d|chunks=%d|drawn=%d|spawned=%ld|chunkspawned=%ld|"
           "evicted=%ld|chunkdropped=%ld|maxmove=%.3f|frame=%d\n",
           g_efxPoolLive ? "live" : "no-clock",
           g_efxNPart, g_efxNChunk, g_efxDrawn, g_efxPartSpawned, g_efxChunkSpawned,
           g_efxEvicted, g_efxChunkDropped, g_efxMaxMove, g_efxStepFrame);
    {
        int per[10];
        memset(per, 0, sizeof(per));
        for (int i = 0; i < g_efxNPart; i++)  per[g_efxPart[i].sys]++;
        for (int i = 0; i < g_efxNChunk; i++) per[g_efxChunk[i].sys]++;
        for (int s = 0; s < 10; s++)
            if (per[s])
                printf("EFXPART|sys=%d|%s|live=%d\n", s, EFX_SYS[s].seq, per[s]);
    }
    if (g_efxEvicted || g_efxChunkDropped)
        printf("EFXCAP|the pool hit its cap: %ld sprites evicted oldest-first, "
               "%ld chunks refused (caps %d/%d)\n",
               g_efxEvicted, g_efxChunkDropped, EFX_MAX_PART, EFX_MAX_CHUNK);
    for (std::map<std::string, long>::const_iterator it = g_efxRecipeParts.begin();
         it != g_efxRecipeParts.end(); ++it) {
        std::map<std::string, long>::const_iterator ct = g_efxRecipeChunks.find(it->first);
        printf("EFXSPAWN|%s|recipe=ANIM_%s|particles=%ld|chunks=%ld\n",
               it->first.c_str(), g_efxRecipeName[it->first].c_str(), it->second,
               ct == g_efxRecipeChunks.end() ? 0L : ct->second);
    }
    for (std::map<std::string, long>::const_iterator it = g_efxRecipeChunks.begin();
         it != g_efxRecipeChunks.end(); ++it)
        if (g_efxRecipeParts.find(it->first) == g_efxRecipeParts.end())
            printf("EFXSPAWN|%s|recipe=ANIM_%s|particles=0|chunks=%ld\n",
                   it->first.c_str(), g_efxRecipeName[it->first].c_str(), it->second);
    /* An empty cartridge recipe and a missing mapping are completely different
       verdicts and must never be confused for one another. */
    for (std::map<std::string, int>::const_iterator it = g_efxEmpty.begin();
         it != g_efxEmpty.end(); ++it)
        printf("EFXEMPTY|%s|empty-by-design|ticks=%d\n", it->first.c_str(), it->second);
    for (std::map<std::string, int>::const_iterator it = g_efxNoRecipe.begin();
         it != g_efxNoRecipe.end(); ++it)
        printf("EFXNOREC|%s|no cartridge counterpart|ticks=%d\n",
               it->first.c_str(), it->second);
    /* The per-particle listing rides the existing verb rather than a new one, so a
       gate can prove motion and determinism with the script vocabulary that is
       already there. It is verbose on purpose: this is the dump. */
    if (g_efxPoolLive)
        efx_part_dump();
    printf("EFXDUMP-END\n");
}

/* "efxpartdump": one line per live particle. This is the determinism oracle and the
   motion oracle at once -- two runs of one script must produce identical EFXP| text,
   and two dumps a few ticks apart must show the same ids at different positions. It
   fails on the first divergent particle instead of on a changed pixel, which is
   sharper and far cheaper to debug than diffing PNGs.
   The id is the particle's own seed: a pure function of which particle it is, stable
   for its whole life and across runs, unlike its index in a pool that compacts. */
static void efx_part_dump(void)
{
    printf("EFXPDUMP-BEGIN live=%d chunks=%d frame=%d\n",
           g_efxNPart, g_efxNChunk, g_efxStepFrame);
    for (int i = 0; i < g_efxNPart + g_efxNChunk; i++) {
        const bool ch = (i >= g_efxNPart);
        const EfxPart& p = ch ? g_efxChunk[i - g_efxNPart] : g_efxPart[i];
        printf("EFXP|%08x|sys=%d|%s|chunk=%d|p=%.4f,%.4f,%.4f|v=%.4f,%.4f,%.4f|"
               "age=%d/%d|hw=%.4f|rgba=%d,%d,%d,%d\n",
               p.seed, p.sys, EFX_SYS[p.sys].seq, ch ? 1 : 0,
               p.x, p.y, p.z, p.vx, p.vy, p.vz, p.age, p.life, p.hw,
               p.r, p.g, p.b, p.a);
    }
    printf("EFXPDUMP-END\n");
}

/* "expectpart NAME any|sprite|chunk [N]": at least N particles of that kind must have
   been spawned by that anim's recipe this run. NAME is the DOS art name, the same key
   expectefx already takes. */
static bool efx_expect_part(const char* name, const char* kind, int atleast)
{
    std::map<std::string, long>::const_iterator ps = g_efxRecipeParts.find(name);
    std::map<std::string, long>::const_iterator cs = g_efxRecipeChunks.find(name);
    const long np = (ps == g_efxRecipeParts.end())  ? 0 : ps->second;
    const long nc = (cs == g_efxRecipeChunks.end()) ? 0 : cs->second;
    long have = np + nc;
    if (!strcmp(kind, "sprite")) have = np;
    else if (!strcmp(kind, "chunk")) have = nc;
    const bool ok = have >= (long)atleast;
    printf("EXPECTPART|%s|%s|want>=%d|have=%ld|%s\n", name, kind, atleast, have,
           ok ? "PASS" : "FAIL");
    return ok;
}

/* "expectmove CELLS": some particle must have travelled that far, in the ground
   plane, from where it was born. This is the verb that catches a pool which spawns
   but never integrates -- the failure mode one level up from the one being fixed, and
   the one a single screenshot cannot tell apart from success. */
static bool efx_expect_move(float cells)
{
    const bool ok = g_efxMaxMove >= cells;
    printf("EXPECTMOVE|want>=%.3f|have=%.3f|%s\n", cells, g_efxMaxMove,
           ok ? "PASS" : "FAIL");
    return ok;
}

/* "expectefx NAME [N]": at least N (default 1) spawns of that anim name must have been
   seen this run. Returns false on failure so the caller can bump its fail counter. */
static bool efx_expect(const char* name, int atleast)
{
    std::map<std::string, int>::const_iterator it = g_efxSpawns.find(name);
    const int have = (it == g_efxSpawns.end()) ? 0 : it->second;
    const bool ok = have >= atleast;
    printf("EXPECTEFX|%s|want>=%d|have=%d|%s\n", name, atleast, have,
           ok ? "PASS" : "FAIL");
    return ok;
}

/* "expectdeath N": at least N infantry deaths (dying=1 men) must have been seen. */
static bool efx_expect_death(int atleast)
{
    const bool ok = g_efxDeathsSeen >= atleast;
    printf("EXPECTDEATH|want>=%d|have=%d|%s\n", atleast, g_efxDeathsSeen,
           ok ? "PASS" : "FAIL");
    return ok;
}

/* Undo efx_load_pack AND drop the per-mission bookkeeping.

   Two reasons this cannot be skipped between missions. The obvious one: every
   sequence frame is its own texture name, and efx_load_pack APPENDS to g_efxSeqs,
   so a second load without this would double the list and leak the first set.
   The quiet one: g_efxLivePrev/g_efxLiveNow/g_efxDeathIds are keyed by engine
   object id, and mission two reuses mission one's ids, so a stale set would make
   the spawner think units that just appeared had already died. */
static void efx_free(void)
{
    for (size_t i = 0; i < g_efxSeqs.size(); i++)
        for (size_t k = 0; k < g_efxSeqs[i].gl.size(); k++)
            if (g_efxSeqs[i].gl[k])
                glDeleteTextures(1, &g_efxSeqs[i].gl[k]);
    g_efxSeqs.clear();
    g_efxHaveArt = false;
    g_efxAnnounced = false;

    g_efxAnims.clear();
    g_efxBullets.clear();
    g_efxDying.clear();
    g_efxSpawns.clear();
    g_efxLivePrev.clear();
    g_efxLiveNow.clear();
    g_efxDeathIds.clear();
    g_efxUnknown.clear();
    g_efxArtUsed.clear();
    g_efxDeathsSeen = 0;
    g_efxCellHidden = 0;
    g_efxDeadFootprint = 0;
    g_efxDeadHouse = 0;

    /* The pool is keyed by engine object id and engine frame, both of which mission
       two reuses from mission one, so it has to go the same way the spawn bookkeeping
       does. g_efxPoolLive stays as it is: the tick hook is a property of the host, not
       of the mission, and clearing it would drop the whole system back to the legacy
       billboards on the second scenario. */
    efx_pool_clear();
}

#endif /* CNC3D_EFFECTS_MOD_H */
