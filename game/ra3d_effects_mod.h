/* =====================================================================================
 *  ra3d_effects_mod.h -- Red Alert under Enhanced: OpenCNC3D's own explosions, fire,
 *  smoke and muzzle flashes.
 *
 *  Tiberian Dawn's Enhanced effects are the cartridge's particle recipes, one per engine
 *  anim, wearing the remastered art (effects_mod.h, efx_enh.pack) with Enhanced's own
 *  additions (efx_enhanced.h): the chain-gun and cannon flashes, a building's rebuilt death
 *  blast and its smoke column. The Red Alert engine raises the same anims for the same
 *  things -- most of its anim art IS Tiberian Dawn's (the Red Alert lab's reuse rows) -- but
 *  numbers them its own way, and its 3D view used to draw every one as its 1996 sprite on a
 *  card. Under Enhanced each anim with a Tiberian Dawn counterpart is now that effect,
 *  through the very same emitters, and the card stands down; the rest stay cards.
 *
 *    - THE NUMBERS (ra3d_efx_type): each Red Alert AnimType to the Tiberian Dawn AnimType
 *      that is the same effect, checked against the anim's own name, so a Red Alert brain
 *      numbering its anims otherwise falls back to the card rather than to a wrong effect.
 *    - WHERE THEY START (ra3d_efx_base): Red Alert's objects are cards, and the engine puts
 *      a muzzle flash or a burning tree's flames at the point of the 2D picture where they
 *      show (Fire_Coord lifts a barrel up the screen by its height). That point is found
 *      on the object's card as the sprite pass stood it (ra3d_sprites_mod.h keeps every
 *      body card's placement), so the flash leaves the drawn barrel. A fire on a building
 *      drawn as its model burns on the model's roof over its cell, and a dying building's
 *      blast goes off at the middle of its plot, once.
 *    - THE DAMAGED BUILDING'S SMOKE (ra3d_damage_smoke): Tiberian Dawn's intact-only
 *      remasters keep the cartridge's damage plume, placed for the cartridge's own model.
 *      Red Alert's structures have no such row (the few names they share with Tiberian
 *      Dawn's are other buildings), so below half strength each one drawn by its standing
 *      model takes the same plume from its own roof; one with its own stages has its own.
 *
 *  Classic is untouched (it draws the 1996 anims itself, dosclassic_mod.h), and so is the
 *  3D view without Enhanced effects: there every anim stays its card.
 * ===================================================================================== */
#pragma once

/* Red Alert's AnimType (redalert/defines.h, with its ants and the remaster's companions,
   both of which the brain is built with), its IniName, and the Tiberian Dawn AnimType
   that is the same effect; EFX_TYPE_HOST where nothing here is, and the card stays. */
struct Ra3dAnimMap { int ra; const char* name; int td; };
static const Ra3dAnimMap RA3D_ANIM_MAP[] = {
    {  0, "FBALL1",   0 },   /* the large fireball; a building's death becomes FRAG3 */
    {  1, "FB2",     33 },   /* the flame thrower's fading puff: the tiny flame, FIRE_TINY */
    {  2, "FRAG1",    2 },
    {  3, "VEH-HIT1", 4 },
    {  4, "VEH-HIT2", 5 },
    {  5, "VEH-HIT3", 6 },
    {  6, "ART-EXP1", 7 },
    {  7, "NAPALM1",  8 },
    {  8, "NAPALM2",  9 },
    {  9, "NAPALM3", 10 },
    { 10, "SMOKEY",  11 },   /* a rocket's trail puff, SMOKE_PUFF */
    { 11, "PIFF",    12 },
    { 12, "PIFFPIFF",13 },
    { 13, "FIRE3",   30 },   /* FIRE_SMALL */
    { 14, "FIRE2",   31 },   /* FIRE_MED */
    { 15, "FIRE1",   32 },   /* FIRE_MED2 */
    { 16, "FIRE4",   33 },   /* FIRE_TINY */
    { 17, "GUNFIRE", 34 },   /* the cannon flash, MUZZLE_FLASH */
    { 18, "SMOKE_M", 35 },   /* empty on the cartridge, as in Tiberian Dawn */
    { 19, "BURN-S",  36 }, { 20, "BURN-M", 37 }, { 21, "BURN-L", 38 },
    { 22, "BURN-S",  39 }, { 23, "BURN-M", 40 }, { 24, "BURN-L", 41 },   /* ON_FIRE_* */
    { 25, "SAMFIRE", 42 }, { 26, "SAMFIRE", 43 }, { 27, "SAMFIRE", 44 }, { 28, "SAMFIRE", 45 },
    { 29, "SAMFIRE", 46 }, { 30, "SAMFIRE", 47 }, { 31, "SAMFIRE", 48 }, { 32, "SAMFIRE", 49 },
    { 33, "MINIGUN", 50 }, { 34, "MINIGUN", 51 }, { 35, "MINIGUN", 52 }, { 36, "MINIGUN", 53 },
    { 37, "MINIGUN", 54 }, { 38, "MINIGUN", 55 }, { 39, "MINIGUN", 56 }, { 40, "MINIGUN", 57 },
    { 41, "SMOKLAND",58 },   /* LZ_SMOKE */
    { 52, "FLMSPT",  72 },   /* the oil pump's flame spout: empty, as in Tiberian Dawn */
    { 60, "ATOMSFX", 60 },   /* the atom bomb */
    { 69, "FLAK",     4 },   /* a flak burst against aircraft: the small fireball, VEH_HIT1 */
    { 78, "VEH-HIT2", 5 },   /* a mine going off, drawn by Red Alert with VEH-HIT2's art */
    /* the remaster's invisible companions (FIRE3, FIRE2, FIRE1, FIRE4 and the beacon),
       which no recipe answers in either game */
    { 84, "FIRE3",   80 }, { 85, "FIRE2", 81 }, { 86, "FIRE1", 82 }, { 87, "FIRE4", 83 },
    { 88, "BEACON",  84 },
};

/* (an anim with no counterpart and an effect of its own instead, ra3d_efxm_mod.h) */
static int ra3d_efxm_type(int etype, const char* anim);
static int ra3d_efx_type(int type, const char* name)
{
    for (size_t i = 0; i < sizeof RA3D_ANIM_MAP / sizeof RA3D_ANIM_MAP[0]; i++)
        if (RA3D_ANIM_MAP[i].ra == type && !strcmp(RA3D_ANIM_MAP[i].name, name))
            return RA3D_ANIM_MAP[i].td;
    return ra3d_efxm_type(type, name);
}

/* A remaster companion: drawn by nothing in the 1996 game (AnimClass::Make_Invisible). */
static bool ra3d_anim_companion(const EfxAnim& a)
{
    return a.etype >= 84 && a.etype <= 88 && a.type >= 80 && a.type <= 84;
}

/* WHETHER THE SPRITE PASS STILL DRAWS THIS ANIM AS ITS 1996 CARD (ra3d_sprites_mod.h). */
static bool ebld_ra_anim_played(const EfxAnim& an);
static bool ra3d_efxm_keeps_card(const EfxAnim& a);   /* ra3d_efxm_mod.h */
static bool ra3d_anim_card(const EfxAnim& a)
{
    if (ra3d_anim_companion(a)) return false;
    /* (an anim a structure's model plays as its own clip: the Allied Tech Center's doors) */
    if (ebld_ra_anim_played(a)) return false;
    /* (and an electrocution, and a corpse with no 3D body under it, keep theirs under
       their own effects: ra3d_efxm_mod.h) */
    return !g_efxEnhOn || a.type == EFX_TYPE_HOST || ra3d_efxm_keeps_card(a);
}

/* Red Alert's RTTI (redalert/defines.h) as the object list's kind and the layer export's
   record type, for the objects an anim can ride. */
static bool ra3d_rtti_kind(int rtti, ObjKind* kind, int* layer)
{
    switch (rtti) {
    case 13: *kind = K_INFANTRY; *layer = INFANTRY; return true;
    case 28: *kind = K_UNIT;     *layer = UNIT;     return true;
    case 1:  *kind = K_AIRCRAFT; *layer = AIRCRAFT; return true;
    case 5:  *kind = K_BUILDING; *layer = BUILDING; return true;
    case 30: *kind = K_VESSEL;   *layer = VESSEL;   return true;
    case 24: *kind = K_TERRAIN;  *layer = TERRAIN;  return true;
    default: return false;
    }
}

/* THE TOP OF A MODEL OVER A POINT, in world cells: the highest solid face (neither the
   shadow plate nor glass) whose plan holds (px, pz), the model standing at (cx, cz) with
   no turn, as the sprite pass and draw_mesh stand a building. The ground there when no
   face covers the point. Remembered per model and point, which do not move. */
static float ra3d_model_top(int mi, float cx, float cz, float px, float pz)
{
    const float ground = terrain_y(cx, cz);
    if (mi < 0 || mi >= (int)g_pack.mesh.size()) return terrain_y(px, pz);
    static std::map<long long, float> memo;
    const long long key = ((long long)mi << 40)
                        ^ ((long long)(unsigned)(int)floorf((px - cx) * 64.0f) & 0xFFFFF) << 20
                        ^ ((long long)(unsigned)(int)floorf((pz - cz) * 64.0f) & 0xFFFFF);
    std::map<long long, float>::const_iterator m = memo.find(key);
    if (m != memo.end()) return ground + m->second;
    const float x = (px - cx) / MODEL_SCALE, z = (pz - cz) / MODEL_SCALE;
    const std::vector<PackTri>& tris = g_pack.mesh[mi].tris;
    float best = -1e9f;
    for (size_t i = 0; i < tris.size(); i++) {
        const PackTri& t = tris[i];
        if (t.mode == MODE_SHADOW || t.mode == MODE_XLU) continue;
        const float x0 = t.v[0].x, z0 = t.v[0].z, x1 = t.v[1].x, z1 = t.v[1].z;
        const float x2 = t.v[2].x, z2 = t.v[2].z;
        const float d = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
        if (fabsf(d) < 1e-6f) continue;                          /* edge on */
        const float a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / d;
        const float b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / d;
        const float c = 1.0f - a - b;
        if (a < 0.0f || b < 0.0f || c < 0.0f) continue;
        const float y = a * t.v[0].y + b * t.v[1].y + c * t.v[2].y;
        if (y > best) best = y;
    }
    const float lift = best > -1e8f ? best * MODEL_SCALE : terrain_y(px, pz) - ground;
    if (memo.size() > 4096) memo.clear();
    memo[key] = lift;
    return ground + lift;
}

/* A DEAD BUILDING'S PLOT, as effects_mod.h asks it (g_efxDeadFootprint) to turn the
   engine's fireballs into one building blast: only while the building on it is dying,
   and never a barrel's. Red Alert raises the large fireball for heavy hits
   too (Combat_Anim: a V2's warhead, a tank's shell at full damage), and on a standing
   building each of those took a whole building's death; its explosive barrels (BARL,
   BRL3) are structures of one cell, and a row of them going up took a building's blast
   each, which whited the screen out. Both go up as the large fireball off a building's
   plot does (FRAG2). A dying building is still in the dump, at no strength, on the tick its
   fireballs are raised (each delayed one is listed from that tick on); a plot whose
   building has already left the dump is a fireball landing where one stood, a barrel's
   in a chain of them going up. */
static int ra3d_dead_footprint(int cx, int cy)
{
    const int k = dead_footprint_at(cx, cy);
    if (k < 0) return k;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_BUILDING || o.limbo || o.cy * g_gridW + o.cx != k) continue;
        if (!strcmp(o.type, "BARL") || !strcmp(o.type, "BRL3")) return -1;
        return o.str <= 0 ? k : -1;
    }
    return -1;
}

static int g_ra3dEfxCard = 0, g_ra3dEfxRoof = 0, g_ra3dEfxPlot = 0, g_ra3dEfxTrail = 0;

/* THE HOST'S ANSWER to effects_mod.h's g_efxBaseOf, under Red Alert. */
static bool ra3d_efx_base(const EfxAnim& an, float out[3])
{
    if (!g_raModule || g_fx.dos_classic) return false;
    /* a building's death blast: at the middle of its plot */
    if (an.type == 0 && an.attref < 0 && g_efxDeadFootprint) {
        const int k = efx_fb_footprint(an, g_efxStepFrame);
        if (k < 0) return false;
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_BUILDING || o.cy * g_gridW + o.cx != k) continue;
            out[0] = o.ex; out[1] = efx_ground(o.ex, o.ez); out[2] = o.ez;
            g_ra3dEfxPlot++;
            return true;
        }
        return false;
    }
    /* A ROCKET'S TRAIL PUFF AT THE ROCKET'S HEIGHT. BulletClass lays its smoke (and a
       flame ball its fading puffs) at its own ground point, which is where the 2D game
       draws it too, under the rocket it draws that far up the screen; in 3D the rocket
       flies at its height (ra3d_sprites_mod.h), so a puff is laid as high as the nearest
       rocket in the air was when the puff was first seen, and keeps that height; a V2's
       rocket drawn on its own arc (evhc_mod.h) lays it at the arc's height there. A rocket
       fired from the air is drawn off the engine's point (efx_air_shots): it is matched by
       the engine's point, and the puff moved with it as it was when the puff was seen. */
    if (an.attref < 0 && (an.etype == 10 || an.etype == 1)) {
        struct Trail { int seen; float y, dx, dz; };
        static std::map<int, Trail> trails;
        const int key = (an.etype << 16) | (an.id & 0xFFFF);
        std::map<int, Trail>::iterator t = trails.find(key);
        if (t == trails.end() || g_efxStepFrame - t->second.seen > 2) {
            Trail n = { g_efxStepFrame, -1e9f, 0.0f, 0.0f };
            float best = 0.75f * 0.75f;
            for (size_t i = 0; i < g_efxBullets.size(); i++) {
                const EfxBullet& b = g_efxBullets[i];
                if (b.alt <= 0) continue;
                const float dx = b.ewx - an.wx, dz = b.ewz - an.wz, d2 = dx * dx + dz * dz;
                if (d2 < best) {
                    best = d2;
                    float arc;
                    n.dx = b.wx - b.ewx; n.dz = b.wz - b.ewz;
                    n.y = evhc_v2_trail_y(b.id, an.wx, an.wz, &arc)
                        ? arc : efx_ground(an.wx + n.dx, an.wz + n.dz) + efx_bullet_lift(b);
                }
            }
            if (trails.size() > 512) trails.clear();
            t = trails.insert(std::make_pair(key, n)).first;
            t->second = n;
        }
        t->second.seen = g_efxStepFrame;
        if (t->second.y < -1e8f) return false;
        out[0] = an.wx + t->second.dx; out[1] = t->second.y; out[2] = an.wz + t->second.dz;
        g_ra3dEfxTrail++;
        return true;
    }
    ObjKind kind; int layer;
    if (!ra3d_rtti_kind(an.attkind, &kind, &layer)) return false;
    /* A TREE'S FIRE. The dump names the terrain an anim rides by its RTTI alone, with no
       heap id (Catch_Fire's flames on a tree), so the tree is the terrain card nearest the
       flames: they sit on its crown, within a cell of the picture's centre. */
    if (an.attref < 0 && kind == K_TERRAIN) {
        const Ra3dBodyCard* best = NULL;
        float bd = 1.5f * 1.5f;
        for (std::map<long long, Ra3dBodyCard>::const_iterator b = g_ra3dBodyCards.begin();
             b != g_ra3dBodyCards.end(); ++b) {
            if ((int)(b->first >> 32) != layer) continue;
            const float dx = an.wx - b->second.cx, dz = an.wz - b->second.cz;
            if (dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = &b->second; }
        }
        if (!best) return false;
        const float dx = an.wx - best->cx, dz = an.wz - best->cz;
        for (int k = 0; k < 3; k++)
            out[k] = best->c[k] + g_ra3dBodyR[k] * dx - g_ra3dBodyU[k] * dz;
        g_ra3dEfxCard++;
        return true;
    }
    if (an.attref < 0) return false;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.id != an.attref || o.kind != kind) continue;
        /* a building its model draws: on the roof over the anim's point */
        if (kind == K_BUILDING && ebld_ra_ready(o)) {
            out[0] = an.wx; out[2] = an.wz;
            out[1] = ra3d_model_top(ebld_ra_pick_mesh(o), o.ex, o.ez, an.wx, an.wz) + 0.02f;
            g_ra3dEfxRoof++;
            return true;
        }
        break;
    }
    /* an object drawn as its card: the point of the 2D picture, on the card */
    const std::map<long long, Ra3dBodyCard>::const_iterator b =
        g_ra3dBodyCards.find(((long long)layer << 32) | (unsigned)an.attref);
    if (b == g_ra3dBodyCards.end()) return false;
    const float dx = an.wx - b->second.cx, dz = an.wz - b->second.cz;
    for (int k = 0; k < 3; k++)
        out[k] = b->second.c[k] + g_ra3dBodyR[k] * dx - g_ra3dBodyU[k] * dz;
    g_ra3dEfxCard++;
    return true;
}

/* A DAMAGED STRUCTURE'S SMOKE. The cartridge's black damage plume (its Communications
   Center row: every fourth tick, 31 units across growing 3.1 a tick, 25 ticks, leaning as
   it climbs), from the roof of every Red Alert structure its model draws once it is below
   half strength: over the middle of a plot up to two cells wide, and over two points a
   quarter of the plot either side of it on a wider one. A civilian building reused from
   OpenCNC3D's art has its own damage stages and keeps them, and so does a Red Alert
   structure staged from its lab (ebld_ra_staged). Enhanced effects only. */
static long g_ra3dSmokeSpawned = 0;
static void ra3d_damage_smoke(int frame)
{
    if (!g_raModule || !g_efxEnhOn || ((unsigned)frame & 3u)) return;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_BUILDING || o.limbo || o.id < 0 || o.str <= 0) continue;
        if (!cart_is_damaged(o) || building_constructing(o) || ebld_ra_civil(o.type)) continue;
        /* (a structure with its stages burns and smokes from its own damaged model) */
        if (ebld_ra_stages(o)) continue;
        if (!ebld_ra_ready(o) || shroud_cell_hidden(o.cx, o.cy)) continue;
        const int mi = ebld_ra_pick_mesh(o);
        const int fw = o.fw > 0 ? o.fw : 1;
        const int n = fw > 2 ? 2 : 1;
        for (int p = 0; p < n; p++) {
            const float px = o.ex + (n == 2 ? (p ? 0.25f : -0.25f) * (float)fw : 0.0f), pz = o.ez;
            const unsigned h = efx_hash(0x52414453u, (unsigned)o.id, (unsigned)p, (unsigned)frame);
            const float j0 = ((float)(int)( h        & 0xFFu) - 127.0f) * 0.004f;
            const float j2 = ((float)(int)((h >> 16) & 0xFFu) - 127.0f) * 0.004f;
            EfxPart q;
            memset(&q, 0, sizeof q);
            q.x = px; q.z = pz;
            q.y = ra3d_model_top(mi, o.ex, o.ez, px, pz) + 0.05f;
            q.sx = q.x; q.sy = q.y; q.sz = q.z;
            q.vx = j0 * EFX_U;
            q.vy = 8.1f * EFX_U;
            q.vz = j2 * EFX_U;
            q.ay = 0.5f * EFX_U;
            q.az = -0.1f * EFX_U;
            q.hw = 31.0f * EFX_U;
            q.grow = 3.1f * EFX_U;
            q.life = 25;
            q.arate = 8;
            q.a = 255; q.r = 0; q.g = 0; q.b = 0;
            q.seed = h;
            q.sys = 0;                     /* the cartridge's plume: GLOW, tinted black */
            efx_push(q);
            g_ra3dSmokeSpawned++;
        }
    }
}

/* THE TESLA COIL'S GLOW. The bolt stays the 1996 pieces laid from the model's crown
   (ra3d_sprites_mod.h); under Enhanced each end of it also takes OpenCNC3D's electric
   spark, the remastered IONSPARK sheet the cartridge's lightning sources wear, additive
   so the bloom picks it up: on the crown the bolt leaves and where it lands, flickering
   through the sheet's frames on the engine's clock. Drawn from the bolts the frame's
   sprite pass laid, after the particles; nothing is emitted. */
static int g_ra3dBoltGlows = 0;
static void ra3d_bolt_glow(void)
{
    g_ra3dBoltGlows = 0;
    if (!g_ra3dSprites || !g_efxEnhOn || g_ra3dBolts.empty()) return;
    const EfxSeq* s = efx_seq("IONSPARK");
    if (!s || s->frames <= 0) return;
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    GLint gradeWas=0; if(fx_gl_ready)glGetIntegerv(GL_CURRENT_PROGRAM,&gradeWas);
    const bool graded=efx_grade_begin(true);
    for (size_t i = 0; i < g_ra3dBolts.size(); i++) {
        const Ra3dBolt& b = g_ra3dBolts[i];
        if (shroud_cell_hidden((int)floorf(b.cx), (int)floorf(b.cz))) continue;
        const int f = (g_engineFrame + (int)i) % s->frames;
        efx_quad_tex(s, f, g_bbRight, g_bbUp, b.cx, b.cy + 0.05f, b.cz, 0.45f, 0.45f, 0.9f);
        const float ty = terrain_y(b.tx, b.tz) + 0.25f;
        efx_quad_tex(s, (f + 1) % s->frames, g_bbRight, g_bbUp, b.tx, ty, b.tz, 0.35f, 0.35f, 0.85f);
        g_ra3dBoltGlows += 2;
    }
    if(graded)fx_glUseProgram((GLuint)gradeWas);
    glPopAttrib();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* What the last frame's Red Alert effects were, for ra3dprobe. */
static void ra3d_efx_probe(void)
{
    int host = 0, particles = 0, companions = 0;
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        if (ra3d_anim_companion(a)) companions++;
        else if (a.type == EFX_TYPE_HOST) host++;
        else particles++;
    }
    printf("RA3DPROBE|efx|enh=%d|anims=%zu|particleanims=%d|hostanims=%d|companions=%d"
           "|animcards=%d|bullets=%zu|bulletslifted=%d|pool=%d|drawn=%d|cardbases=%d"
           "|roofbases=%d|plotbases=%d|trailbases=%d|smoke=%ld|fires=%d|boltglows=%d"
           "|blasts=%ld|irflash=%d\n",
           g_efxEnhOn ? 1 : 0, g_efxAnims.size(), particles, host, companions,
           g_ra3dAnimCards, g_efxBullets.size(), g_ra3dBulletsLifted, g_efxNPart, g_efxDrawn,
           g_ra3dEfxCard, g_ra3dEfxRoof, g_ra3dEfxPlot, g_ra3dEfxTrail, g_ra3dSmokeSpawned,
           g_efxFiresSeen, g_ra3dBoltGlows, g_efxFootBlasts, g_irFlashes);
}

/* The hooks, set where the effects module's other hooks are, for a Red Alert mission. */
static void ra3d_efx_install(void)
{
    if (!g_raModule) return;
    g_efxTypeOf = ra3d_efx_type;
    g_efxBaseOf = ra3d_efx_base;
    g_efxBlastPerFootprint = true;
    g_efxDrawBullets = false;
}
