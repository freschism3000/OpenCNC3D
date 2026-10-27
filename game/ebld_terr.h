/* ---------------------------------------------------------------------------------- *
 *  THE REMASTERED CELL DECORATIONS, Enhanced only: the bonus crates (SCRATE, WCRATE),
 *  the haystacks and fields placed as overlays (V12..V18) and the crashed Orca lying on
 *  rock patch P04.
 *
 *  None of the three is an object. The crates and the fields are cell overlays the brain
 *  reports as CRATE| and FIELD| lines, and the wreck stands on any cell whose ground is
 *  template P04 (wreck_mod.h). The cartridge draws all three through its one
 *  cell-decoration path, and so does this renderer: draw_crates, draw_fields and
 *  wreck_draw. Each of them asks eterr_cell_draw first; where a remastered model of the
 *  type is attached to the pack and Enhanced is on, it draws that model whole at the
 *  cell and the cartridge's mesh is not drawn. Classic never reaches a remaster, because
 *  ebld_model_of answers nothing outside Enhanced's rule.
 *
 *  THE MODELS ARE INTACT-ONLY .ebld FILES (game/ebld/<TYPE>.ebld, bake_ebld.py), read and
 *  attached by ebld_mod.h like every remastered building: the same residency, the same
 *  mipmapped atlases, the same smooth normals. The field overlays draw the V13..V18
 *  buildings' files: the overlay is never damaged, so only their intact mesh is used.
 *  An overlay V12 draws V13's model, as the console draws its single haystack for it
 *  (FIELD_MESH_KEY); the structure form keeps the two stacks.
 *
 *  WHERE THEY STAND. Every one of these models is authored centred on its cell and
 *  standing on the ground (the lab's frame), the same anchor the cartridge's meshes use:
 *  the cell centre, no facing, no lift. A field (0.88 of a cell, its soil easing under
 *  the ground at the edge) and the wreck (0.88 across, its nose and soil heap in the
 *  ground) take the ground under every vertex, as the walls do, so neither floats one
 *  edge on a slope; a crate is a rigid box and takes the ground under its centre, as the
 *  cartridge's cube does.
 *
 *  THE OTHER PASSES. The sun's shadow casters and the relight bake already draw the field
 *  overlays (draw_fields); eterr_draw_aux adds the crates and the wreck there, which the
 *  cartridge's meshes never reached, so a remastered crate or wreck casts and is relit
 *  like a remastered building. eterr_draw_normals puts all three into the light pass's
 *  normal target with the remasters' own class. Opaque only: no triangle of any of these
 *  models is cut out or translucent (the baker writes the export's modes; measured).
 *
 *  THE TERRAIN OBJECTS (the desert rocks, the blossom trees) are objects, and
 *  ebld_draw_object draws their remasters whole, idle loop and all, from every pass the
 *  object loops reach; only the normal target is added here.
 *
 *  THE REMASTERED TREES (T01, drawn for T16 too: tools/art/remaster/foliage) go the same
 *  way, and two things more are needed for them. The rebuilt photogrammetry tree of
 *  tree3d_mod.h claims every tree under Enhanced, so it stands down for a type with a
 *  remaster (eterr_tree_claims, asked by tree3d_claims); and the tree clumps TC01..TC05
 *  are drawn whole from a model of their own where they have one (eterr_clump_whole: the
 *  approved trees composed as the 1995 art pastes them, at the clump's centre coordinate),
 *  and otherwise composed of single trees on their cells (--clumptrees), a cell whose
 *  pick is a remastered type drawn from its model too (eterr_tree_draw), so a wood is
 *  never half one look and half the other. A tree's crown is alpha-cut spray cards (MODE_CUTOUT, two-sided,
 *  their coverage-held mip chain, the light of where each hangs as its vertex colour),
 *  drawn in the cutout passes the object loops already run for the cartridge's own leaf
 *  cards; its trunk and limbs are opaque. Its wind is the file's weights, played by
 *  draw_mesh (g_meshSwayT, set per tree by ebld_draw_object). The temperate look only: a tree's file is
 *  <TYPE>_TEM, so a desert map (which draws the cactus and scrub meshes for the same type
 *  names) and a winter map keep what they drew.
 *
 *  --noeterr is the A/B: the cartridge's meshes come back for all three cell decorations
 *  under Enhanced (--noebld takes the terrain objects' remasters away with the rest).
 * ---------------------------------------------------------------------------------- */
#ifndef EBLD_TERR_H
#define EBLD_TERR_H

static bool g_eterrOn = true;

/* What eterr_cell_draw was asked to draw (ETERR_CRATE, ETERR_FIELD, ETERR_WRECK are
   defined in cnc_eyes.cpp beside the forward declarations the three passes call). */
static const char* const ETERR_WHAT[ETERR_N] = { "crate", "field", "wreck" };

/* THE WITNESS: one record per cell the lit opaque pass drew as a remaster in the last
   frame, the anchor read back out of draw_mesh (g_meshAnchor*), never recomputed beside
   it. Cleared at the top of every colour frame by eterr_frame_begin. */
struct EterrDraw { int what, x, y, model; float ax, az, ay; };
static std::vector<EterrDraw> g_eterrDrawn;
/* Remastered cell draws in each auxiliary pass of the last frame: casters, bake, normals. */
static long g_eterrAux[3] = { 0, 0, 0 };

/* The remastered model standing in for a cell decoration of this type, or -1: Enhanced's
   rule (ebld_rule_now: the remasters on, Enhanced on), this module on, and a model of the
   type attached to the current pack whose meshes are there. */
static int eterr_model(const char* type)
{
    if (!g_eterrOn || g_ebld.empty() || !ebld_rule_now()) return -1;
    return ebld_model_of(type);
}

/* The model drawn whole at a cell centre, in one pass. Its textures go to the card at the
   first draw (ebld_resident); a model whose file cannot be read back answers false and
   the caller draws the cartridge's mesh. The borrowed cartridge shadow plate rides at
   the end of the intact mesh, so MODE_SHADOW draws the cartridge's plate, as it does
   under a remastered building. */
static bool eterr_draw_at(int m, float cx, float cz, int pass, bool perVertex)
{
    if (m < 0 || m >= (int)g_ebld.size()) return false;
    EbldModel& M = g_ebld[m];
    if (!ebld_resident(M)) return false;
    const int imi = M.mesh[EBM_INTACT];
    if (imi < 0 || imi >= (int)g_pack.mesh.size() || M.shadow1 <= 0) return false;
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) M.drawn++;
    const bool was = g_meshGroundPerVertex;
    g_meshGroundPerVertex = perVertex;
    draw_mesh(imi, cx, cz, -1, pass, 0, 0.0f, 0, 1.0f, 0.0f, false, WOBBLE_NONE, -1.0f,
              NULL, 0, (size_t)M.shadow1);
    g_meshGroundPerVertex = was;
    return true;
}

/* NOTHING OFF THE PICTURE IS SUBMITTED. The cartridge's cell meshes are 10 to 60
   triangles and every pass drew all of them; a remastered field is up to 4,140 and the
   wreck 1,768, and a mission can hold twenty of either. The camera's ground box
   (cell_in_view, two cells of margin) is the main view's in the colour, normal and bake
   passes and the sun's own box while the casters draw (fx_draw_shadow_casters swaps it
   in), so a cell outside it can put nothing on the screen or into the shadow map. */
static bool eterr_cell_off(int x, int y)
{
    return !cell_in_view(x, y);
}

/* THE HOOK the three cartridge passes ask before drawing their own mesh for a cell:
   ETERR_NONE when there is no remaster for it (the caller draws the cartridge's mesh),
   ETERR_DRAWN when the remaster was drawn (draw_mesh's anchor witness is this cell's),
   ETERR_OFF when it is off the picture (nothing is drawn by anyone). */
static int eterr_cell_draw(int what, const char* type, int x, int y, int pass)
{
    const int m = eterr_model(type);
    if (m < 0) return ETERR_NONE;
    if (eterr_cell_off(x, y)) return ETERR_OFF;
    const float cx = (float)x + 0.5f, cz = (float)y + 0.5f;
    if (!eterr_draw_at(m, cx, cz, pass, what != ETERR_CRATE)) return ETERR_NONE;
    if (pass == MODE_OPAQUE && !ebld_aux_pass()) {
        EterrDraw d;
        d.what = what; d.x = x; d.y = y; d.model = m;
        d.ax = g_meshAnchorX; d.az = g_meshAnchorZ; d.ay = g_meshAnchorY;
        g_eterrDrawn.push_back(d);
    }
    return ETERR_DRAWN;
}

/* THE SAME FOR A TERRAIN OBJECT drawn whole by ebld_draw_object: a rock is up to four
   cells long and a blossom tree 2,600 triangles, and the object loops submit every object
   of the map in every pass. Its reach is the model's own (the farthest corner from its
   anchor and its height, cells, measured once per model), plus the box's two cells. Only
   under Enhanced's rule and only for a type with a remaster: the cartridge's objects are
   drawn as they always were. */
static std::vector<float> g_eterrReach;
static float eterr_reach(int m)
{
    if ((int)g_eterrReach.size() <= m) g_eterrReach.resize((size_t)m + 1, -1.0f);
    float& r = g_eterrReach[(size_t)m];
    if (r < 0.0f) {
        float h = 0.0f, top = 0.0f;
        const EbldSet& S = g_ebld[(size_t)m].set[EBS_INTACT];
        for (size_t t = 0; t < S.tri.size(); t++)
            for (int k = 0; k < 3; k++) {
                const EbldVert& v = S.tri[t].v[k];
                const float d = sqrtf(v.x * v.x + v.z * v.z);
                if (d > h) h = d;
                if (v.y > top) top = v.y;
            }
        r = (h + top) / 1024.0f;
    }
    return r;
}
static bool eterr_object_off(const SimObject& o)
{
    if (o.kind != K_TERRAIN || g_ebld.empty() || !ebld_rule_now()) return false;
    if (!g_cullOn || !g_cullValid || !g_clip_to_map) return false;
    const int m = ebld_model_of(o.type);
    if (m < 0) return false;
    const float r = eterr_reach(m) + 2.0f;
    return o.wx + r < g_cullX0 || o.wx - r > g_cullX1 || o.wz + r < g_cullZ0
        || o.wz - r > g_cullZ1;
}

/* Accepted Enhanced trees, controlled by Visuals / 3D Trees, the tree clumps TC01 to TC05
   among them. Rocks and blossom trees retain their separate settings. Unavailable tree
   types keep the cartridge. */
static bool eterr_tree_claims(const SimObject& o)
{
    if (o.kind != K_TERRAIN || o.type[0] != 'T') return false;
    if ((o.type[1] < '0' || o.type[1] > '9') && !ebld_packless_type(o.type)) return false;
    if (!g_fx.tree3d || g_ebld.empty() || !ebld_rule_now()) return false;
    const int m = ebld_model_of(o.type);
    return m >= 0 && g_ebld[(size_t)m].nset == 1;
}

/* One tree, or one cell of a composed clump, drawn whole from its remaster in this pass.
   True: it is this system's (drawn, or off the picture); false: not a remastered tree, or
   its model could not be drawn, and the caller draws what it would have. */
static bool eterr_tree_draw(const SimObject& o, int pass)
{
    if (!eterr_tree_claims(o)) return false;
    if (eterr_object_off(o)) return true;
    return ebld_draw_object(mesh_for(o), o, pass);
}

/* A TREE CLUMP WITH A MODEL OF ITS OWN (TC01 to TC05: the approved trees composed as the
   1995 art pastes them, tools/art/remaster/foliage/compose_clump.py) is drawn whole, that
   one model at the clump's centre coordinate (the engine's point for it, the model's own
   origin), in every pass: no cell of it is composed of single trees (clump_part). False
   for a clump without one, which the callers compose as before. */
static bool eterr_clump_whole(const SimObject& o)
{
    return o.kind == K_TERRAIN && ebld_packless_type(o.type) && eterr_tree_claims(o);
}
/* The clump's intact mesh, for its pick box; -1 for a clump drawn otherwise. */
static int eterr_clump_mesh(const SimObject& o)
{
    if (!eterr_clump_whole(o)) return -1;
    const int m = ebld_model_of(o.type);
    return m >= 0 ? g_ebld[(size_t)m].mesh[EBM_INTACT] : -1;
}
/* The clump drawn whole in this pass while any of its blocked cells is drawn (each cell of
   the composition was drawn on the same test); true when the clump is this system's.
   g_clumpWholeDrawn says whether the lit pass really drew its model (not off the
   picture), for the witness line (clump_whole_log; the flag is cnc_eyes.cpp's). */
static bool clump_draw_whole(const SimObject& o, int pass)
{
    g_clumpWholeDrawn = false;
    if (!eterr_clump_whole(o)) return false;
    const ClumpDef* cd = clump_def(o.type);
    bool shown = false;
    for (int c = 0; cd && c < clump_def_count(cd) && !shown; c++) {
        const SimObject part = clump_part_n(o, cd, c);
        shown = clump_cell_drawn(part.cx, part.cy);
    }
    if (!shown) return true;
    const int m = ebld_model_of(o.type);
    const long before = m >= 0 ? (long)g_ebld[(size_t)m].drawn : 0;
    eterr_tree_draw(o, pass);
    g_clumpWholeDrawn = m >= 0 && (long)g_ebld[(size_t)m].drawn > before;
    return true;
}

static void eterr_frame_begin(void)
{
    g_eterrDrawn.clear();
}

/* The crates and the wreck, remastered ones only, for a pass the cartridge's own meshes
   of them never joined: the sun's casters and the relight bake (the fields are already
   there through draw_fields). Classic and --noeterr draw nothing here. */
static void eterr_draw_aux(int pass, int slot)
{
    if (!g_eterrOn || g_ebld.empty() || !ebld_rule_now()) return;
    long n = 0;
    if (g_cratesDraw)
        for (size_t i = 0; i < g_crates.size(); i++) {
            const CrateCell& cc = g_crates[i];
            if (!cell_shown(cc.x, cc.y) || shroud_cell_hidden(cc.x, cc.y)) continue;
            if (eterr_cell_draw(ETERR_CRATE, cc.steel ? "SCRATE" : "WCRATE", cc.x, cc.y, pass)
                == ETERR_DRAWN)
                n++;
        }
    if (g_wreckDraw && wreck_mesh() >= 0)
        for (size_t i = 0; i < g_wrecks.size(); i++) {
            const WreckCell& c = g_wrecks[i];
            if (!cell_shown(c.x, c.y) || shroud_cell_hidden(c.x, c.y)) continue;
            if (eterr_cell_draw(ETERR_WRECK, WRECK_TYPE_CODE, c.x, c.y, pass) == ETERR_DRAWN) n++;
        }
    /* one call a frame for the casters (0) and the bake (1); the normals add theirs */
    if (slot == 2) g_eterrAux[2] += n;
    else if (slot >= 0 && slot < 2) g_eterrAux[slot] = n;
}

/* The three into the light pass's normal target, with the remasters' class
   (ebld_draw_normals' program and state; opaque only, as every triangle here is). */
static void eterr_draw_normals(void)
{
    if (!g_eterrOn || g_ebld.empty() || !ebld_rule_now() || !g_ebldNrmOn) return;
    const GLuint prog = ebld_nrm_prog();
    if (!prog) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    fx_glUseProgram(prog);
    fx_set1i(prog, "uTex", 0);
    fx_set1f(prog, "uClass", fx_unified_lighting()?0.36f:EBLD_NORMAL_CLASS);
    fx_set1f(prog, "uEmitClass", EBLD_SELFLIT_CLASS);
    fx_set1i(prog, "uCut", 0);
    const int was3 = g_m3NormalPass;
    g_m3NormalPass = 1;
    g_meshNrmOut = 1;
    g_eterrAux[2] = 0;
    eterr_draw_aux(MODE_OPAQUE, 2);
    if (g_fieldsDraw)
        for (size_t i = 0; i < g_fields.size(); i++) {
            const FieldCell& fc = g_fields[i];
            if (!cell_shown(fc.x, fc.y) || shroud_cell_hidden(fc.x, fc.y)) continue;
            if (eterr_cell_draw(ETERR_FIELD, FIELD_MESH_KEY[fc.kind], fc.x, fc.y, MODE_OPAQUE)
                == ETERR_DRAWN)
                g_eterrAux[2]++;
        }
    /* AND THE REMASTERED TERRAIN OBJECTS (the rocks, the blossom trees, drawn whole by
       ebld_draw_object, whose normal pass takes the buildings only); a remastered tree's
       spray cards too, in a cutout pass alpha-tested in the program (uCut), with the
       composed clumps' remastered cells. The rocks and the blossom trees have no cutout
       triangle, so that pass draws nothing of theirs. */
    /* uClass's location, looked up once for the walk rather than by name for every
       object (OPENCNC3D_PROGRAM_SHADOW=0 looks it up by name again). */
    const bool knownLocation = gls_program_shadow();
    const GLint locClass = knownLocation ? fx_glGetUniformLocation(prog, "uClass") : -1;
    for (int pi = 0; pi < 2; pi++) {
        const int pass = pi ? MODE_CUTOUT : MODE_OPAQUE;
        fx_set1i(prog, "uCut", pi);
        for (size_t i = 0; i < g_objects.size(); i++) {
            const SimObject& o = g_objects[i];
            if (o.kind != K_TERRAIN || !visible(o)) continue;
            if(fx_unified_lighting()) {
                // These placed stones now emit neutral albedo at the terrain's
                // reference exposure, so the common terrain BRDF owns their sun.
                const bool stone=fx_texset_get()==FX_TEX_ENH && ebld_pack_theater()==1 && !strncmp(o.type,"ROCK",4);
                if(!knownLocation)fx_set1f(prog,"uClass",stone?.875f:o.type[0]=='T'?.50f:.36f);
                else if(locClass>=0)fx_glUniform1f(locClass,stone?.875f:o.type[0]=='T'?.50f:.36f);
            }
            if (mesh_for(o) < 0) {
                if (g_clumpTrees && clump_draw_whole(o, pass)) {
                    if (!pi) g_eterrAux[2]++;
                    continue;
                }
                const ClumpDef* cd = g_clumpTrees ? clump_def(o.type) : NULL;
                for (int c = 0; cd && c < clump_def_count(cd); c++) {
                    const SimObject part = clump_part_n(o, cd, c);
                    if (!clump_cell_drawn(part.cx, part.cy)) continue;
                    if (eterr_tree_draw(part, pass) && !pi) g_eterrAux[2]++;
                }
                continue;
            }
            if (eterr_object_off(o)) continue;
            if (ebld_draw_object(mesh_for(o), o, pass) && !pi) g_eterrAux[2]++;
        }
    }
    fx_set1i(prog, "uCut", 0);
    g_meshNrmOut = 0;
    g_m3NormalPass = was3;
    fx_glUseProgram((GLuint)was);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ================================================================================
 * THE STAIN UNDER A BLOSSOM TREE, Enhanced only: green Tiberium veins spreading out from
 * the roots and fading into the ground, so the tree grows out of the terrain rather than
 * standing on it. It IS the crystals' ground crack (draw_tiberium_ground): the same tile
 * (recoloured with the crystals by tibglow_mod.h), the same additive blend over the ground
 * (GL_ONE, GL_ONE, depth test on, no depth written), the same brightness dial
 * (tib3d_ground), the same slow breathing (tib3d_pulse) and the same shroud term (the
 * cell's four corners averaged), drawn in the same slot of the frame.
 *
 * WHERE: about the roots, which are measured from the model (the centre and the reach of
 * everything under a quarter of a cell's height), so it follows the tree whatever its look.
 * SHAPE: not one disc but the crack quads of ETERR_STAIN laid in a ragged spread, the
 * two on the roots brightest and the outer ones dimmer and smaller, each stretched a little
 * its own way, reaching about a third beyond the roots; every tree turns the spread by a
 * hash of its cell so a grove is not stamped.
 * The lab draws the same table under its tree (index.html, `stainLayer`).
 * ============================================================================== */
/* Each quad of the spread: its centre dx, dz from the roots' centre, its half width east
   and south (all in root reaches), its brightness, and the turn of the tile (radians). The
   tile's veins fill the middle two thirds of it, so the first two quads, centred on the
   roots and turned apart, reach about a third past them; the four smaller ones are the
   veins running on beyond on four uneven sides. */
static const float ETERR_STAIN[6][6] = {
    {  0.00f,  0.00f, 2.10f, 1.80f, 1.00f, 0.0f },
    {  0.15f,  0.10f, 1.60f, 1.90f, 0.80f, 1.9f },
    {  0.85f,  0.35f, 0.90f, 0.80f, 0.55f, 3.1f },
    { -0.75f,  0.70f, 0.80f, 0.95f, 0.50f, 4.4f },
    { -0.55f, -0.85f, 0.90f, 0.75f, 0.50f, 0.8f },
    {  0.60f, -0.75f, 0.70f, 0.70f, 0.45f, 5.6f },
};
#define ETERR_NSTAIN 6
/* How much brighter than one crystal's crack the spread is drawn: one tree's spread is a
   single layer where a field's cracks lie four deep. */
static const float ETERR_STAIN_GAIN = 3.0f;
static bool g_eterrStainOn = true;            /* --noeterrstain, the A/B */
struct EterrRoots { float x, z, r; bool done; };
static std::vector<EterrRoots> g_eterrRoots;
static EterrRoots eterr_roots(int m)
{
    if ((int)g_eterrRoots.size() <= m) {
        EterrRoots z0 = { 0.0f, 0.0f, 0.0f, false };
        g_eterrRoots.resize((size_t)m + 1, z0);
    }
    EterrRoots& R = g_eterrRoots[(size_t)m];
    if (!R.done) {
        R.done = true;
        const EbldSet& S = g_ebld[(size_t)m].set[EBS_INTACT];
        double sx = 0.0, sz = 0.0;
        long n = 0;
        for (size_t t = 0; t < S.tri.size(); t++)
            for (int k = 0; k < 3; k++) {
                const EbldVert& v = S.tri[t].v[k];
                if (v.y < 256.0f) { sx += v.x; sz += v.z; n++; }
            }
        if (n) { R.x = (float)(sx / n) / 1024.0f; R.z = (float)(sz / n) / 1024.0f; }
        float r = 0.0f;
        for (size_t t = 0; t < S.tri.size(); t++)
            for (int k = 0; k < 3; k++) {
                const EbldVert& v = S.tri[t].v[k];
                if (v.y >= 256.0f) continue;
                const float dx = v.x / 1024.0f - R.x, dz = v.z / 1024.0f - R.z;
                const float d = sqrtf(dx * dx + dz * dz);
                if (d > r) r = d;
            }
        R.r = r;
    }
    return R;
}
/* The trees whose spread the last colour frame drew, for the dump: cell, centre, reach,
   quads drawn. */
struct EterrStainSeen { int x, y, quads; float cx, cz, r, lit; };
static std::vector<EterrStainSeen> g_eterrStainSeen;

static void eterr_draw_stains(void)
{
    g_eterrStainSeen.clear();
    if (!g_eterrStainOn || !g_fx.enabled || g_fx.tib3d_ground <= 0.01f) return;
    if (!g_eterrOn || g_ebld.empty() || !ebld_rule_now() || !g_tib3dHaveGround) return;
    bool begun = false;
    for (size_t i = 0; i < g_objects.size(); i++) {
        const SimObject& o = g_objects[i];
        if (o.kind != K_TERRAIN || strncmp(o.type, "SPLIT", 5) || !visible(o)) continue;
        if (shroud_cell_hidden(o.cx, o.cy) || eterr_object_off(o)) continue;
        const int m = ebld_model_of(o.type);
        if (m < 0) continue;
        const EterrRoots R = eterr_roots(m);
        if (R.r <= 0.0f) continue;
        const float rx = o.wx + R.x, rz = o.wz + R.z;
        const int gx = (int)floorf(rx), gz = (int)floorf(rz);
        float lit = g_fx.tib3d_ground * ETERR_STAIN_GAIN
                  * tib3d_pulse(tib3d_hash(o.cx, o.cy, 0x5354u)) * 0.25f
                  * (shroud_corner_vis(gx, gz) + shroud_corner_vis(gx + 1, gz)
                     + shroud_corner_vis(gx, gz + 1) + shroud_corner_vis(gx + 1, gz + 1));
        if (lit <= 0.001f) continue;
        if (!begun) {
            begun = true;
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, g_tib3dGround);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glBegin(GL_TRIANGLES);
        }
        /* the spread's own turn for this tree */
        const float turn = (float)(tib3d_hash(o.cx, o.cy, 0x7475u) & 1023) * (6.2831853f / 1024.0f);
        const float ct = cosf(turn), st = sinf(turn);
        int quads = 0;
        for (int q = 0; q < ETERR_NSTAIN; q++) {
            const float* T = ETERR_STAIN[q];
            const float qx = rx + (T[0] * ct - T[1] * st) * R.r;
            const float qz = rz + (T[0] * st + T[1] * ct) * R.r;
            const float hx = T[2] * R.r, hz = T[3] * R.r;
            float c = lit * T[4];
            if (c > 1.0f) c = 1.0f;
            const float ca = cosf(T[5] + turn), sa = sinf(T[5] + turn);
            /* A GRID, NOT ONE QUAD: its corners follow the ground, and its vertex colour
               falls to nothing toward the quad's rim, so the faint glow the tile carries
               outside its veins never shows a square edge once the spread is brighter
               than a crystal's crack, and the veins fade into the terrain. */
            const int N = 6;
            for (int gj = 0; gj < N; gj++)
                for (int gi = 0; gi < N; gi++) {
                    const int ci[4] = { gi, gi + 1, gi + 1, gi }, cj[4] = { gj, gj, gj + 1, gj + 1 };
                    float vx[4], vz[4], vu[4], vv[4], vc[4];
                    for (int k = 0; k < 4; k++) {
                        const float a = (float)ci[k] / N - 0.5f, b = (float)cj[k] / N - 0.5f;
                        vx[k] = qx + 2.0f * a * hx;
                        vz[k] = qz + 2.0f * b * hz;
                        vu[k] = 0.5f + a * ca - b * sa;
                        vv[k] = 0.5f + a * sa + b * ca;
                        float f = 1.0f - 4.0f * (a * a + b * b);     /* 1 at the middle, 0 at the rim */
                        if (f < 0.0f) f = 0.0f;
                        vc[k] = c * f;
                    }
                    const int tri[6] = { 0, 3, 1, 1, 3, 2 };   /* the ground's own SW-NE split */
                    for (int k = 0; k < 6; k++) {
                        const int v = tri[k];
                        glColor4f(vc[v], vc[v], vc[v], 1.0f);
                        glTexCoord2f(vu[v], vv[v]);
                        glVertex3f(vx[v], terrain_y(vx[v], vz[v]) + 0.012f, vz[v]);
                    }
                }
            quads++;
        }
        EterrStainSeen e = { o.cx, o.cy, quads, rx, rz, R.r, lit };
        g_eterrStainSeen.push_back(e);
    }
    if (begun) {
        glEnd();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
    }
}

/* eterrstain FILE: the crack tile as the game draws it now (recoloured with the crystals),
   written as a PNG over black, which is what an additive tile adds to. The lab lays this
   file under its blossom trees (tools/art/remaster/make_tibstain.sh writes it there); it
   is third-party art and never committed. */
static bool write_png(const char* path, int w, int h, const unsigned char* rgb);
static void eterr_stain_png(const char* path)
{
    if (!g_tib3dHaveGround || g_tib3dGroundPx.empty() || !g_tib3dGroundSide) {
        printf("ETERRSTAIN|none\n");
        return;
    }
    const int n = g_tib3dGroundSide;
    std::vector<unsigned char> px(g_tib3dGroundPx.size());
    tibcol_repaint(&g_tib3dGroundPx[0], &px[0], px.size() / 4, false);
    std::vector<unsigned char> rgb((size_t)n * n * 3);
    for (size_t i = 0; i < (size_t)n * n; i++)
        for (int c = 0; c < 3; c++) rgb[i * 3 + c] = px[i * 4 + c];
    const bool ok = write_png(path, n, n, &rgb[0]);
    printf("ETERRSTAIN|%s|side=%d|ok=%d\n", path, n, ok ? 1 : 0);
}

/* The model attached for a type now, without reading one at first sight (the dump must
   not change what the game holds). */
static int eterr_model_peek(const char* type)
{
    for (size_t i = 0; i < g_ebld.size(); i++) {
        const EbldModel& M = g_ebld[i];
        if (!M.attached || M.type != type || (M.flags & EBX_WALL) || M.wallMask >= 0) continue;
        return ebld_meshes_ok(M) ? (int)i : -1;
    }
    return -1;
}

/* eterrdump -- run after a `shot`. One ETERR line (the switches, each type's model and
   how many of its cells there are), then one ETERRAT line per cell the last colour frame
   drew as a remaster: which cell, which type, the anchor draw_mesh was handed and the
   lift over the ground under it (a remaster centred on its cell must give x+0.5, y+0.5
   and 0). */
static void eterr_dump(void)
{
    static const char* const TYPES[] = { "WCRATE", "SCRATE", "V13", "V14", "V15", "V16",
                                         "V17", "V18", WRECK_TYPE_CODE };
    printf("ETERR|on=%d|ebld=%d|enhanced=%d|crates=%zu|fields=%zu|wrecks=%zu|drawn=%zu"
           "|aux=%ld,%ld,%ld",
           g_eterrOn ? 1 : 0, g_ebldOn ? 1 : 0, g_fx.enabled ? 1 : 0, g_crates.size(),
           g_fields.size(), g_wrecks.size(), g_eterrDrawn.size(),
           g_eterrAux[0], g_eterrAux[1], g_eterrAux[2]);
    for (size_t i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++) {
        const int m = (g_eterrOn && ebld_rule_now()) ? eterr_model_peek(TYPES[i]) : -1;
        printf("|%s=%d", TYPES[i], m);
        /* the look that attached: the type, or the type and the theater whose file it is */
        if (m >= 0) {
            const EbldModel& M = g_ebld[(size_t)m];
            printf(":v%d:%s%s%s", M.version, M.type.c_str(), M.theater >= 0 ? "_" : "",
                   M.theater >= 0 ? EBLD_THEATERS[M.theater % 3] : "");
        }
    }
    printf("\n");
    int per[ETERR_N] = { 0, 0, 0 };
    for (size_t i = 0; i < g_eterrDrawn.size(); i++) {
        const EterrDraw& d = g_eterrDrawn[i];
        per[d.what]++;
        const EbldModel& M = g_ebld[(size_t)d.model];
        printf("ETERRAT|%s|%d|%d|%s%s%s|ax=%.4f|az=%.4f|lift=%.4f\n", ETERR_WHAT[d.what], d.x,
               d.y, M.type.c_str(), M.theater >= 0 ? "_" : "",
               M.theater >= 0 ? EBLD_THEATERS[M.theater % 3] : "", d.ax, d.az,
               d.ay - terrain_y(d.ax, d.az));
    }
    for (size_t i = 0; i < g_eterrStainSeen.size(); i++) {
        const EterrStainSeen& e = g_eterrStainSeen[i];
        printf("ETERRSTAIN|tree|%d|%d|quads=%d|at=%.3f,%.3f|reach=%.3f|lit=%.3f\n", e.x, e.y,
               e.quads, e.cx, e.cz, e.r, e.lit);
    }
    /* THE TREES' STAGES (etree_* in ebld_mod.h): every tree entry that is burning or burnt
       out, and each stage model's whole draws in the colour picture since the last dump
       (counted where they are submitted, as EBLDART's are; the snags drawn on their own
       entries are counted apart too) */
    for (std::map<long long, EbldBld>::const_iterator it = g_ebldBld.begin();
         it != g_ebldBld.end(); ++it) {
        const EbldBld& E = it->second;
        if (E.treeStage <= 0 || E.model < 0 || E.model >= (int)g_ebld.size()) continue;
        const EbldModel& M = g_ebld[(size_t)E.model];
        long emitted = 0;
        for (size_t e = 0; e < E.es.size(); e++) emitted += E.es[e].spawned;
        printf("ETREE|tree|%s|%d|%d|stage=%s|model=%s%s%s|orphan=%d|since=%d|sink=%.3f"
               "|emitted=%ld\n", E.type, E.cx, E.cy, E.treeStage == 1 ? "burning" : "snag",
               M.type.c_str(), M.theater >= 0 ? "_" : "",
               M.theater >= 0 ? EBLD_THEATERS[M.theater % 3] : "", E.orphan ? 1 : 0,
               g_engineFrame - E.treeTick, E.orphan ? etree_sink(E, (float)g_engineFrame) : 0.0f,
               emitted);
    }
    for (size_t i = 0; i < g_ebld.size(); i++) {
        EbldModel& M = g_ebld[i];
        if (M.stageOf < 0) continue;
        int nflip = 0;
        for (size_t t = 0; t < M.tex.size(); t++) if (M.tex[t].grid > 1) nflip++;
        printf("ETREESTAGE|%s%s%s|attached=%d|tris=%d|emit=%d|flipbooks=%d|drawn=%ld\n",
               M.type.c_str(), M.theater >= 0 ? "_" : "",
               M.theater >= 0 ? EBLD_THEATERS[M.theater % 3] : "", M.attached ? 1 : 0,
               (int)M.set[EBS_INTACT].tri.size(), (int)M.emit.size(),
               nflip,
               M.drawn);
        M.drawn = 0;
    }
    printf("ETREEDUMP|on=%d|burning=%zu|stood=%ld|orphandrawn=%ld\n", g_etreeOn ? 1 : 0,
           g_etreeFire.size(), g_etreeStood, g_etreeOrphanDrawn);
    g_etreeOrphanDrawn = 0;
    printf("ETERRDUMP|crate=%d|field=%d|wreck=%d|stains=%zu\n", per[0], per[1], per[2],
           g_eterrStainSeen.size());
    fflush(stdout);
}

#endif /* EBLD_TERR_H */
