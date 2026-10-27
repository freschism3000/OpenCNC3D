/* =====================================================================================
 *  ra3d_props_mod.h -- Red Alert's own props under Enhanced: the explosive barrels, the
 *  mines, the interior's crate stacks, the ice floes, the water crate, the flag location
 *  and the barbed-wire fence, each drawn as its remastered model (ra3d/ebld, baked by
 *  tools/ra3d/bake-props.py from the Red Alert lab's approved exports and staged with the
 *  structures by tools/ra3d/stage-structures.py).
 *
 *  Each goes the way its engine kind goes, so almost all of it is the remastered buildings'
 *  own machinery (ebld_mod.h):
 *
 *    - THE BARRELS (BARL, BRL3) are one-cell buildings with their stages: drawn by
 *      ebld_draw_object like any Red Alert structure, and at their death the file's burst
 *      (the drums' pieces thrown) and its explosion (the lab's FBALL1) in place of the
 *      engine's fireball, which stands down for it (ebld_anims_filter). The chain to the
 *      neighbouring barrels is the engine's own (the invisible fire bullets of
 *      BuildingClass::Take_Damage).
 *    - THE MINES (MINV, MINP) are one-cell buildings drawn whole. The 1996 engine never
 *      draws a mine another side laid (RULES.INI Invisible=yes: the layer export clears
 *      the player's bit of its VisibleFlags, which the 3D sprite pass never read), so a
 *      mine the player cannot see is neither its model nor its card (ra3d_mine_hidden,
 *      read off the layer export on every parse). A mine that goes off is deleted by the
 *      engine, which raises its own explosion anim; the model is simply gone.
 *    - THE CRATE STACKS (BOXES01..09, interior) and THE ICE FLOES (ICE01..05, snow) are
 *      terrain objects: drawn whole at the engine's Center_Coord, which is the export's
 *      CenterBase origin, as the reused trees are (ebld_ra_ready claims them through
 *      ra3d_prop_claims). A floe's z 0 is the sea's surface, the ground's height there, as
 *      the sea is laid on the terrain's corners.
 *    - THE WATER CRATE (WWCRATE) and THE FLAG LOCATION (FPLS) are cell overlays, no
 *      objects: on every parse the engine's map lists them (ra3d_cells_from_map), and each
 *      is drawn at its cell's middle as a stand-in terrain object of its type, so its idle
 *      clip plays (the crate's bob, the flag location's pulse, the 1996 frames' own),
 *      started a whole number of frames in from a hash of its cell (ra3d_prop_phase) so two
 *      of them do not move together. The crate's rings are not its model's: it presses on
 *      the sea's wave field as it bobs (ra3d_props_wave_sources, water_mod.h), so they are
 *      the sea's own ripples, as a ship's wake is.
 *    - THE FENCE (FENC) is Red Alert's sixth wall kind, drawn per cell by its connection
 *      nibble like the other walls (draw_walls), its strands cut out, worn at its second
 *      damage level, wrecked when the cell is cleared, and raised by its placement when a
 *      cell of the player's appears (eplc_wall_draw).
 *
 *  Classic never reaches any of it, nor Tiberian Dawn: every test below starts with the Red
 *  Alert module and Enhanced's rule, and the types exist only in Red Alert's files.
 * ===================================================================================== */
#pragma once

/* `raprops 0` draws the 1996 pictures again, for an A/B (set before the mission's first
   tick: a prop already in its stages keeps them) */
static bool g_ra3dPropsOn = true;
static bool ra3d_props_on(void) { return g_ra3dPropsOn; }
struct Ra3dPropCell { char type[8]; int x, y; };
static std::vector<Ra3dPropCell> g_ra3dPropCells;      /* the last parse's */
static std::set<int> g_ra3dHiddenMines;                /* building ids, the last parse's */
static int g_ra3dPropCellsDrawn = 0, g_ra3dMinesHidden = 0, g_ra3dMineCardsHidden = 0;

static bool ra3d_prop_cell_type(const char* t)
{
    return t && (!strcmp(t, "WWCRATE") || !strcmp(t, "FPLS"));
}
static bool ra3d_prop_terrain_type(const char* t)
{
    if (!t) return false;
    if (!strncmp(t, "BOXES0", 6)) return t[6] >= '1' && t[6] <= '9' && !t[7];
    if (!strncmp(t, "ICE0", 4)) return t[4] >= '1' && t[4] <= '5' && !t[5];
    return false;
}
static bool ra3d_mine_type(const char* t)
{
    return t && (!strcmp(t, "MINV") || !strcmp(t, "MINP"));
}

/* The model of a prop drawn whole (a terrain object or a cell decoration), or -1: Red
   Alert, the remasters' rule, not Classic, and an intact-only file of the type staged. */
static int ra3d_prop_model(const char* t)
{
    if (!g_raModule || !g_ra3dPropsOn || g_fx.dos_classic || !ebld_rule_now()) return -1;
    const int m = ebld_model_of(t);
    return m >= 0 && g_ebld[(size_t)m].nset == 1 ? m : -1;
}

/* Whether a terrain object (or a cell decoration's stand-in) is drawn as its prop model. */
static bool ra3d_prop_claims(const SimObject& o)
{
    if (o.kind != K_TERRAIN || !(ra3d_prop_terrain_type(o.type) || ra3d_prop_cell_type(o.type)))
        return false;
    const int m = ra3d_prop_model(o.type);
    return m >= 0 && ebld_resident(g_ebld[(size_t)m]);
}

/* The idle clock's head start of a prop drawn without an entry of its own: a whole number of
   its loop's frames, from its cell, as ebld_step gives a terrain object with an entry. */
static float ra3d_prop_phase(const SimObject& o, const EbldClip& C)
{
    if (!g_raModule || !(ra3d_prop_terrain_type(o.type) || ra3d_prop_cell_type(o.type))
        || C.frames <= 0 || !(C.fps > 0.0f))
        return 0.0f;
    const unsigned h = efx_hash(0x4542494Cu, (unsigned)o.cx, (unsigned)o.cy, 0u);
    return (float)(h % (unsigned)C.frames) / C.fps;
}

/* Whether a mine is hidden from the player (its own side's and its allies' are shown). */
static bool ra3d_mine_hidden(const SimObject& o)
{
    return g_raModule && o.kind == K_BUILDING && ra3d_mine_type(o.type)
        && g_ra3dHiddenMines.count(o.id) != 0;
}

/* ON EVERY PARSE, after the engine's map is read: the mines the player cannot see, off the
   layer export the sprite pass reads (its VisibleFlags, which the engine clears for every
   house not allied to the mine's owner). */
static void ra3d_props_tick(void)
{
    g_ra3dHiddenMines.clear();
    if (!g_raModule || !BrainGetState) return;
    static std::vector<unsigned char> bytes(sizeof(CNCObjectListStruct) + 8192 * sizeof(CNCObjectStruct));
    if (!BrainGetState(GAME_STATE_LAYERS, 0, bytes.data(), (unsigned)bytes.size())) return;
    const CNCObjectListStruct* list = (const CNCObjectListStruct*)bytes.data();
    const int count = list->Count < 8192 ? list->Count : 8192;
    const unsigned int player = dos2d_player_mask();
    for (int i = 0; i < count; i++) {
        const CNCObjectStruct& r = list->Objects[i];
        if (r.Type != BUILDING || r.SubObject || !ra3d_mine_type(r.AssetName)) continue;
        if (!(r.VisibleFlags & player)) g_ra3dHiddenMines.insert(r.ID);
    }
    g_ra3dMinesHidden = (int)g_ra3dHiddenMines.size();
}

/* A cell decoration's stand-in: a terrain object of its type on its cell, at its middle. */
static SimObject ra3d_prop_object(const Ra3dPropCell& c)
{
    SimObject o = SimObject();
    o.kind = K_TERRAIN;
    snprintf(o.type, sizeof o.type, "%s", c.type);
    snprintf(o.house, sizeof o.house, "%s", "Neutral");
    o.cx = o.tcx = c.x; o.cy = o.tcy = c.y;
    o.wx = o.ex = (float)c.x + 0.5f; o.wz = o.ez = (float)c.y + 0.5f;
    o.fw = o.fh = 1;
    o.face = o.tface = -1;
    o.id = -1;
    o.str = o.maxstr = 1;
    o.act = o.pips = o.maxpips = o.doing = o.dostage = o.remap = o.makecnt = -1;
    o.doorstage = o.status = o.flashme = o.group = o.nav = o.tar = -1;
    o.wobmode = -1;
    return o;
}

/* The cell decorations in one pass (the colour passes, the casters, the normals). */
static void ra3d_props_draw_cells(int pass)
{
    if (!g_raModule || g_ra3dPropCells.empty()) return;
    for (size_t i = 0; i < g_ra3dPropCells.size(); i++) {
        const Ra3dPropCell& c = g_ra3dPropCells[i];
        if (!cell_shown(c.x, c.y) || shroud_cell_hidden(c.x, c.y)) continue;
        const SimObject o = ra3d_prop_object(c);
        if (!ra3d_prop_claims(o)) continue;
        if (ebld_draw_object(-1, o, pass) && pass == MODE_OPAQUE && !ebld_aux_pass())
            g_ra3dPropCellsDrawn++;
    }
}

/* THE WATER CRATE'S RINGS. The sea under Enhanced runs a wave field (water_mod.h, the
   ships' wakes), and a water crate drawn by its model presses on it where it floats, in
   step with its bob: hardest when its loop has it riding lowest (the loop's start), easing
   off and pulling back as it rises, so the field throws off one ring a bob and the rings
   travel, fade and bounce off the shore as the sea's own ripples. The press is the boat
   wake dial's (water_wake; 0 draws no rings, as it draws no wake) times WWCRATE_WAVE_PRESS,
   over a Gaussian as wide as the crate, and it never leaves the hull's white water. */
/* (by eye at the game camera on the shipped settings: at 0.28, the strength of a gunboat's
   press, the rings lie inside the water shader's amplitude window but under the sea's own
   ripple and do not read; at 1.5 they read as rings round the crate with their glints, and
   at 3.5 they glare) */
#define WWCRATE_WAVE_PRESS  1.5f    /* the press at its deepest, in water_wake's units */
#define WWCRATE_WAVE_RADIUS 0.17f   /* cells: the crate's half width */
static int g_ra3dWaveSources = 0;   /* the last call's count, for the dump */

/* The press of one water crate at engine frame `frame`, or 0 when it is not pressing (not
   drawn by its model, under the shroud, or no clip). */
static float ra3d_wwcrate_press(const Ra3dPropCell& c, int frame)
{
    if (strcmp(c.type, "WWCRATE") || shroud_cell_hidden(c.x, c.y)) return 0.0f;
    const SimObject o = ra3d_prop_object(c);
    if (!ra3d_prop_claims(o)) return 0.0f;
    const EbldModel& B = g_ebld[(size_t)ra3d_prop_model(o.type)];
    if (B.clipIntact < 0) return 0.0f;
    const EbldClip& C = B.clip[(size_t)B.clipIntact];
    if (C.frames <= 0 || !(C.fps > 0.0f)) return 0.0f;
    const double T = ebld_idle_t((float)frame, ra3d_prop_phase(o, C));
    const double period = (double)C.frames / (double)C.fps;
    return g_fx.water_wake * WWCRATE_WAVE_PRESS * (float)cos(2.0 * M_PI * T / period);
}

/* The water crates pressing the wave field at engine frame `frame` (water_wave_sim asks once
   a step): each its cell's middle and radius in field uv, and its press. */
static int ra3d_props_wave_sources(int frame, float out[][4], int max)
{
    int n = 0;
    if (g_raModule && g_gridW > 0 && g_gridH > 0) {
        for (size_t i = 0; i < g_ra3dPropCells.size() && n < max; i++) {
            const Ra3dPropCell& c = g_ra3dPropCells[i];
            if (strcmp(c.type, "WWCRATE")) continue;
            const float p = ra3d_wwcrate_press(c, frame);
            if (p == 0.0f) continue;
            out[n][0] = ((float)c.x + 0.5f) / (float)g_gridW;
            out[n][1] = ((float)c.y + 0.5f) / (float)g_gridH;
            out[n][2] = p;
            out[n][3] = WWCRATE_WAVE_RADIUS / (float)g_gridW;
            n++;
        }
    }
    g_ra3dWaveSources = n;
    return n;
}

/* Whether a 1996 overlay stands down for its prop model (ra3d_cell_model asks). */
static bool ra3d_prop_cell_model(const char* t)
{
    return ra3d_prop_cell_type(t) && ra3d_prop_model(t) >= 0;
}

/* `raprops`: one line per prop the engine has (its objects and the map's cells) and
   whether its model draws it, for a gate. */
static void ra3d_props_dump(void)
{
    int n = 0;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        const bool prop = (o.kind == K_TERRAIN && ra3d_prop_terrain_type(o.type))
                       || (o.kind == K_BUILDING && (ra3d_mine_type(o.type)
                           || !strcmp(o.type, "BARL") || !strcmp(o.type, "BRL3")));
        if (!prop) continue;
        const int m = ebld_model_of(o.type);
        printf("RA3DPROP|obj|%s|id=%d|cell=%d,%d|at=%.3f,%.3f|house=%s|str=%d|model=%d|"
               "nset=%d|ready=%d|hidden=%d|spec=%d\n",
               o.type, o.id, o.cx, o.cy, o.wx, o.wz, o.house, o.str, m,
               m >= 0 ? g_ebld[(size_t)m].nset : 0, ebld_ra_ready(o) ? 1 : 0,
               ra3d_mine_hidden(o) ? 1 : 0, m >= 0 && g_ebld[(size_t)m].hasSpec ? 1 : 0);
        n++;
    }
    for (size_t i = 0; i < g_ra3dPropCells.size(); i++) {
        const Ra3dPropCell& c = g_ra3dPropCells[i];
        const SimObject o = ra3d_prop_object(c);
        const int m = ebld_model_of(c.type);
        int clip = -1, frame = -1;
        if (m >= 0 && g_ebld[(size_t)m].clipIntact >= 0) {
            const EbldClip& C = g_ebld[(size_t)m].clip[(size_t)g_ebld[(size_t)m].clipIntact];
            clip = C.frames;
            frame = ebld_clip_frame(C, -1.0f, ebld_idle_t(engine_time(), ra3d_prop_phase(o, C)));
        }
        int parts = -1;
        if (m >= 0 && g_ebld[(size_t)m].clipIntact >= 0)
            parts = g_ebld[(size_t)m].clip[(size_t)g_ebld[(size_t)m].clipIntact].nparts;
        /* (the water crate: its press on the sea's wave field now, and the field's tallest
           ripple 0.35 to 1.2 cells out, where its rings are; -1 with no field) */
        const bool ww = !strcmp(c.type, "WWCRATE");
        printf("RA3DPROP|cell|%s|cell=%d,%d|model=%d|claims=%d|clipframes=%d|frame=%d|parts=%d|"
               "press=%.5f|ring=%.5f\n",
               c.type, c.x, c.y, m, ra3d_prop_claims(o) ? 1 : 0, clip, frame, parts,
               ww ? ra3d_wwcrate_press(c, g_engineFrame) : 0.0f,
               ww ? water_wave_ring((float)c.x + 0.5f, (float)c.y + 0.5f, 0.35f, 1.2f) : 0.0f);
        n++;
    }
    int fence = 0, fenceModel = 0;
    ebld_wall_models();
    for (size_t i = 0; i < g_walls.size(); i++) {
        const WallCell& w = g_walls[i];
        if (w.kind != 5) continue;
        fence++;
        /* (a fence cell going up: its rise, and whether the fence's placement draws it) */
        const float t = wall_rise_secs(w.x, w.y);
        if (t >= 0.0f)
            printf("RA3DPROP|rise|FENC|cell=%d,%d|owner=%s|icon=%d|t=%.3f|placement=%d\n",
                   w.x, w.y, w.owner, (int)w.icon, t, eplc_model_of("FENC") >= 0 ? 1 : 0);
    }
    fenceModel = g_ebldWallModel[5];
    printf("RA3DPROP|end|props=%d|cells=%d|mineshidden=%d|minecards=%d|celldrawn=%d|fence=%d|"
           "fencemodel=%d|wavesources=%d|wavebobs=%d|wavesteps=%d|frame=%d\n", n,
           (int)g_ra3dPropCells.size(), g_ra3dMinesHidden, g_ra3dMineCardsHidden,
           g_ra3dPropCellsDrawn, fence, fenceModel, g_ra3dWaveSources, g_waveBobs, g_waveStepped,
           g_engineFrame);
    fflush(stdout);
}
