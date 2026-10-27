/* =====================================================================================
 *  ra3d_atom_mod.h -- Red Alert under Enhanced: the atom bomb's own blast in 3D.
 *
 *  Tiberian Dawn's nuclear strike under Enhanced is the cartridge's: the ATOM_BLAST
 *  recipe's ring of fire (efx_recipes.h) and the NUKEFX mushroom, four animated script
 *  models baked from the cartridge (effects_mod.h, efx_draw_nuke_meshes). Red Alert's
 *  ATOMSFX used to borrow both, through its anim number (ra3d_effects_mod.h maps its 60
 *  to Tiberian Dawn's 60). It has its own now, drawn here and only here:
 *
 *    - THE FORMS: thirteen mesh parts out of ATOMSFX.efxm (the fireball, its soot, the
 *      cap's glowing heart, the cap's rolling torus and its crown, the stem, the skirt,
 *      the condensation shell and collar, the shock ring, its dust wall, the base surge,
 *      the light on the ground), 111 frames of a tick each, every part carrying per frame
 *      a 3x4 matrix, whether it is drawn, a tint, an alpha, a texture scroll and an
 *      alpha-test level (a rising level dissolves a cloud into puffs, since each
 *      texture's alpha is the cloud's density). The file is written by the effect's own
 *      generator in the Red Alert lab (tools/art/remaster/ra/nuke3d_ATOMSFX.py, layout in
 *      its write_efxm); the lab's effects viewer plays the same parts the same way.
 *    - THE PARTICLES layered on them (the fireball's halo, the sparks, the surge's dust,
 *      the wisps off the cap, the debris drawn up the stem) are Enhanced emitters of a
 *      recipe of its own, ATOM_BLAST_RA, generated from the lab's fx.json into
 *      efx_enhanced.h like every other Enhanced emitter, and fired on their own clock.
 *      The anim's Tiberian Dawn recipe stands down for it (g_efxRecipeOf), and so does
 *      the NUKEFX mushroom (g_efxOwnMeshes).
 *    - THE FLASH: a white overlay over the battlefield driven by the blast's own clock
 *      (fx.json's screen_flash: up to 0.85 one tick after the detonation, gone seven
 *      ticks later), and the 1996 game's screen shake when the damage lands (stage 19,
 *      Do_Atom_Damage's Shake_The_Screen(3)), through the renderer's camera jolt.
 *
 *  THE CLOCK. One frame a tick, from the anim's stage 0, kept here per blast rather than
 *  read off the anim's stage: the blast runs to frame 110 and the anim does not live that
 *  long, so each blast is armed at its first sighting (born = engine frame - stage) and
 *  drawn on (engine frame - born + the tick's fraction) until its last frame, whether or
 *  not the anim is still in the dump. The particles' own clock (efx_linger_step) is
 *  armed the same way. A pure function of the engine's frame, so two --shot runs agree.
 *
 *  NO SHROUD, the superweapon rule (efx_draw_nuke_meshes says why): the player aimed this
 *  strike and the engine fires it whether the cell is mapped or not, so it is neither
 *  culled nor darkened by the shroud.
 *
 *  Red Alert, Enhanced effects only: Classic draws the 1996 anim itself, and the 3D view
 *  without Enhanced effects keeps the anim's card. Tiberian Dawn never reaches any of
 *  this: every hook below is installed by ra3d_efx_install, for a Red Alert mission.
 *
 *  Fixed function throughout (a matrix, a vertex colour, a texture offset, an alpha test,
 *  two blend modes and backface culling), drawn by draw_mesh like every other model.
 *  The file is read, attached and drawn by the EFXM code every Red Alert effect with forms
 *  of its own shares (ra3d_efxm_mod.h), which also reaches this file's hooks.
 * ===================================================================================== */
#pragma once

static const int RA3D_ATOM_TYPE = 60;            /* Red Alert's ANIM_ATOM_BLAST */
static const int RA3D_ATOM_DAMAGE_TICK = 19;     /* AnimTypeClass AtomBomb: Biggest */

/* The blast's file (ra3d_efxm_mod.h reads, attaches and draws EFXM files for every Red
   Alert effect with forms of its own; this one is the atom bomb's). */
static Ra3dEfxm g_atomFx;
static bool g_atomLoaded = false;
static int g_atomMesh = -1;                      /* in this pack, once attached */
static int g_atomFrames = 0;

/* A blast being drawn: its anim (id, born on the engine's frame) and where it went off. */
struct Ra3dAtomBlast {
    int id, born, seen;
    float wx, wz;
    bool shaken;
    int said;                                    /* the last frame reported */
};
static std::vector<Ra3dAtomBlast> g_atomBlasts;
static float g_atomFlashNow = 0.0f;

static void ra3d_atom_load(const char* dir)
{
    if (g_atomLoaded || !g_raModule) return;
    g_atomLoaded = true;
    char path[512];
    snprintf(path, sizeof path, "%sra3d/efx/ATOMSFX.efxm", dir && *dir ? dir : "");
    bool missing = false;
    if (!ra3d_efxm_read(path, g_atomFx, &missing)) {
        fprintf(stderr, missing ? "ra3d atom: no %s; the atom bomb keeps Tiberian Dawn's nuclear "
                                  "strike under Enhanced\n"
                                : "ra3d atom: %s is damaged or of another layout; the atom bomb keeps "
                                  "Tiberian Dawn's nuclear strike under Enhanced\n", path);
        return;
    }
    g_atomFrames = g_atomFx.frames;
    printf("RA3DATOM|loaded|parts=%d|tris=%d|frames=%d|textures=%d\n", (int)g_atomFx.parts.size(),
           (int)(g_atomFx.corners.size() / 3), g_atomFx.frames, (int)g_atomFx.tex.size());
    fflush(stdout);
}

/* Into the pack just loaded, beside the remastered buildings (ra3d_efxm_attach). */
static void ra3d_atom_attach(void)
{
    g_atomMesh = -1;
    g_atomBlasts.clear();
    if (!g_raModule || g_atomFx.parts.empty()) return;
    ra3d_efxm_attach(g_atomFx, "ATOMSFX");
    g_atomMesh = g_atomFx.mesh;
}

/* Whether this anim is a Red Alert atom bomb this file draws. */
static bool ra3d_atom_is(const EfxAnim& a)
{
    return g_raModule && g_efxEnhOn && g_atomMesh >= 0
        && a.etype == RA3D_ATOM_TYPE && !strcmp(a.name, "ATOMSFX");
}

/* g_efxOwnMeshes: the NUKEFX mushroom stands down for it. */
static bool ra3d_atom_owns(const EfxAnim& a)
{
    return ra3d_atom_is(a);
}

/* Arm a blast at the first sighting of its anim, and drop one past its last frame. On the
   engine's tick, beside efx_step, never from the draw: a frame drawn twice or a run of
   ticks with no frame drawn must not change what is armed or when the ground shakes. */
static void ra3d_atom_track(void)
{
    if (g_atomMesh < 0) { g_atomBlasts.clear(); return; }
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        if (!ra3d_atom_is(a)) continue;
        const int born = g_engineFrame - (a.stage > 0 ? a.stage : 0);
        /* the same anim while it stays in the dump; an id seen again after a gap is a new
           anim in a reused slot */
        bool known = false;
        for (size_t b = 0; b < g_atomBlasts.size(); b++)
            if (g_atomBlasts[b].id == a.id && g_engineFrame - g_atomBlasts[b].seen <= 2) {
                g_atomBlasts[b].seen = g_engineFrame;
                known = true;
                break;
            }
        if (known) continue;
        Ra3dAtomBlast B;
        B.id = a.id; B.born = born; B.seen = g_engineFrame; B.wx = a.wx; B.wz = a.wz;
        B.shaken = false; B.said = -1;
        g_atomBlasts.push_back(B);
        printf("RA3DATOM|armed|id=%d|born=%d|stage=%d|cell=%.2f,%.2f\n", a.id, born, a.stage,
               a.wx, a.wz);
        fflush(stdout);
    }
    size_t w = 0;
    for (size_t b = 0; b < g_atomBlasts.size(); b++) {
        Ra3dAtomBlast& B = g_atomBlasts[b];
        const int t = g_engineFrame - B.born;
        if (t < 0 || t > g_atomFrames - 1) continue;          /* its last frame drawn */
        /* THE 1996 SHAKE, when the damage lands: Do_Atom_Damage's Shake_The_Screen(3), three
           shakes as a building worth 1,200 shakes it (Cost_Of / 400), so the jolt a
           three-cell building's death gives (0.028 a cell) */
        if (!B.shaken && t >= RA3D_ATOM_DAMAGE_TICK) {
            B.shaken = true;
            shake_note(0.028f * 3.0f);
            printf("RA3DATOM|shake|id=%d|frame=%d\n", B.id, t);
            fflush(stdout);
        }
        g_atomBlasts[w++] = B;
    }
    g_atomBlasts.resize(w);
}

/* The atom bomb's share of g_efxHostMeshes (ra3d_efxm_host_meshes): phase 0 is drawn
   before the particles (every part ordered below 10: the cut clouds first, writing depth,
   then the soft ones in their order), phase 1 after them (the condensation shell and
   collar), the order the lab's viewer draws them in. */
static void ra3d_atom_draw(int phase)
{
    if (g_atomMesh < 0 || g_atomBlasts.empty() || !g_efxEnhOn) return;
    const int np = (int)g_atomFx.parts.size();
    int seq[64];
    const int n = ra3d_efxm_sequence(g_atomFx, phase, seq, 64);
    ra3d_efxm_begin(true);
    for (size_t b = 0; b < g_atomBlasts.size(); b++) {
        const Ra3dAtomBlast& B = g_atomBlasts[b];
        float t = (float)(g_engineFrame - B.born) + g_tickAlpha;
        if (t < 0.0f) t = 0.0f;
        if (t > (float)(g_atomFrames - 1)) t = (float)(g_atomFrames - 1);
        const long before = g_atomFx.partDraws;
        for (int i = 0; i < n; i++)
            ra3d_efxm_draw_part(g_atomFx, seq[i], B.wx, B.wz, 0.0f, t);
        /* ONE LINE A BLAST A FRAME, from the draw, so a gate reading it knows the parts
           reached draw_mesh rather than that an anim existed */
        const int fi = (int)t;
        if (phase == 0 && fi != B.said) {
            g_atomBlasts[b].said = fi;
            std::string on;
            for (int p = 0; p < np; p++)
                if (ra3d_efxm_on(g_atomFx, fi, p)) {
                    if (!on.empty()) on += ",";
                    on += g_atomFx.parts[p].name;
                }
            printf("RA3DATOM|draw|id=%d|frame=%d|parts=%ld|on=%s\n", B.id, fi,
                   g_atomFx.partDraws - before, on.c_str());
        }
    }
    ra3d_efxm_end();
}

/* THE SCREEN FLASH, below the seam and under the sidebar: up to its peak one tick after the
   detonation and back to nothing seven ticks later (fx.json's screen_flash, which the lab's
   viewer plays the same way), the brightest blast's. */
static const float RA3D_ATOM_FLASH_PEAK = 0.85f, RA3D_ATOM_FLASH_UP = 1.0f,
                   RA3D_ATOM_FLASH_DOWN = 7.0f;
static void ra3d_atom_flash_draw(int fbw, int fbh)
{
    g_atomFlashNow = 0.0f;
    if (g_atomMesh < 0 || g_atomBlasts.empty() || !g_efxEnhOn) return;
    float a = 0.0f;
    for (size_t b = 0; b < g_atomBlasts.size(); b++) {
        const float t = (float)(g_engineFrame - g_atomBlasts[b].born) + g_tickAlpha;
        float v = 0.0f;
        if (t >= 0.0f && t < RA3D_ATOM_FLASH_UP) v = RA3D_ATOM_FLASH_PEAK * t / RA3D_ATOM_FLASH_UP;
        else if (t >= RA3D_ATOM_FLASH_UP && t < RA3D_ATOM_FLASH_UP + RA3D_ATOM_FLASH_DOWN)
            v = RA3D_ATOM_FLASH_PEAK * (1.0f - (t - RA3D_ATOM_FLASH_UP) / RA3D_ATOM_FLASH_DOWN);
        if (v > a) a = v;
    }
    g_atomFlashNow = a;
    if (a <= 0.0f) return;
    begin_overlay(fbw, fbh);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, a);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f); glVertex2f((float)fbw, 0.0f);
    glVertex2f((float)fbw, (float)fbh); glVertex2f(0.0f, (float)fbh);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDisable(GL_BLEND);
    end_overlay();
}

/* "atomdump": the blasts being drawn and the flash, for the gate. */
static void ra3d_atom_dump(void)
{
    printf("RA3DATOM|dump|mesh=%d|enh=%d|blasts=%zu|flash=%.3f|partdraws=%ld|tridraws=%ld\n",
           g_atomMesh, g_efxEnhOn ? 1 : 0, g_atomBlasts.size(), g_atomFlashNow,
           g_atomFx.partDraws, g_atomFx.triDraws);
    for (size_t b = 0; b < g_atomBlasts.size(); b++)
        printf("RA3DATOM|blast|id=%d|frame=%d|cell=%.2f,%.2f\n", g_atomBlasts[b].id,
               g_engineFrame - g_atomBlasts[b].born, g_atomBlasts[b].wx, g_atomBlasts[b].wz);
    fflush(stdout);
}

/* The hook of its own, beside ra3d_efx_install's, for a Red Alert mission; its recipe and
   its parts' draw are reached through ra3d_efxm_install's. */
static void ra3d_atom_install(void)
{
    if (!g_raModule) return;
    g_efxOwnMeshes = ra3d_atom_owns;
}
