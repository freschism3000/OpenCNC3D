/* ---------------------------------------------------------------------------------- *
 *  THE CELL-DECORATION WRECK: the one structural model in the cartridge that no pack
 *  baked and nothing drew.
 *
 *  WHAT IT IS. Model-table slot 205, display list dl_012DBC8, scene-graph node
 *  RAM 0x801B8518: 74 vertices and 60 triangles of grey faceted airframe lying broken
 *  on the ground. THE CARTRIDGE NEVER NAMES IT, and the shape of that fact was written
 *  down wrong before, so it is stated here as measured, with the measurement itself
 *  committed as tools/bakery/name_catalogue.py rather than left to be re-derived.
 *
 *  The model-name catalogue is a word array of RAM pointers at ROM 0x1DE8FC indexed by
 *  model-table slot, and ITS ENTRY FOR SLOT 205 IS ZERO. It was described in this tree as
 *  "100 names at ROM 0x1B70F8" covering "slots 10..161 only", and that sentence has to be
 *  taken apart rather than simply replaced. 0x1B70F8 is the address of nothing: the name
 *  blob starts at ROM 0x1B7000 and 14 named slots (142, 143, 144 and 151..161) point
 *  BELOW 0x1B70F8. "100 names" is not the count: over slots 10..161 the array points at
 *  83 distinct strings, and over the whole array to slot 512, 115. And "slots 10..161" is
 *  true of the LOOKUP but false of the ARRAY: the model-name lookup at RAM 0x801D19B4
 *  really does stop at 152 entries, which is exactly 10..161 and is recorded elsewhere in
 *  cnc_eyes.cpp, while the array itself keeps naming slots at 249..264, 277..279,
 *  308..314, 319 and 332..334, which are the front end's BRF_* scene roots. Reading the
 *  lookup's bound as the array's extent is what put slot 205 "past the end" rather than
 *  where it is, which is at a zero entry inside the array. Whichever way the tail is
 *  counted, slot 205 stays nameless, which is the point. Reproduce the whole scan with
 *  tools/bakery/name_catalogue.py. So the mesh is baked and referred to here under the
 *  name of the PC TEMPLATE it stands in for, "P04", and the full evidence sits in its
 *  entry in tools/bakery/support/unit_models.json.
 *
 *  WHY IT DRAWS FROM THE TERRAIN AND NOT FROM AN OBJECT. The cartridge keeps one
 *  cell-decoration draw table at RAM 0x8020FF58, 39 records of {mode, descriptor}.
 *  Entry 38 is {195, -1}, and 195 + 10 is model slot 205, the same +10 bias the walls
 *  and terrain use. No OverlayType and no SmudgeType registers draw index 38: the
 *  loader at RAM 0x801E5190 finds the N64 tile slot whose PC (template, icon) pair is
 *  (70, 0), and the draw loop at RAM 0x801FB044 forces index 38 on any cell carrying
 *  that tile. PC template 70 is TEMPLATE_PATCH4, declared in EA's GPL source as
 *  Patch4(TEMPLATE_PATCH4, THEATERF_TEMPERATE | THEATERF_DESERT, "P04", ...), a 1x1
 *  rock patch. So on the PC the cell is a flat grey rock patch and on the console it
 *  is a three-dimensional wreck standing on it.
 *
 *  HOW THIS SIDE FINDS THOSE CELLS. GAME_STATE_STATIC_MAP hands back one
 *  CNCStaticCellStruct per cell with TemplateTypeName built as "<IniName>_i<icon>.tga"
 *  (dllinterface.cpp), which is exactly "P04_i0.tga" here, and IconNumber beside it.
 *  CellClass::Get_Template_Info always returns true, so the array is DENSE and
 *  row-major over the exported rect: index i is cell
 *  (MapCellX + i % MapCellWidth, MapCellY + i / MapCellWidth). Measured against the
 *  shipped brain: SCG10EA yields exactly two, at (18,32) and (38,44), which is what
 *  that map's own baked terrain data says, and SCB33EA yields fourteen.
 *
 *  AND THE READ IS CHECKED BEFORE IT IS BELIEVED. StaticCells is the only field of
 *  CNCMapDataStruct that sits AFTER ScenarioName, and ScenarioName is declared
 *  char[_MAX_FNAME + _MAX_EXT] -- macros the brain takes from its own wwstd.h and this
 *  host has to state for itself, because the brain's public header does not carry
 *  them. Get them wrong and every field before ScenarioName still reads correctly
 *  while this one is skewed by the difference and returns nothing but garbage, with no
 *  crash and no complaint. wreck_layout_ok() therefore locates the array the brain
 *  ACTUALLY wrote, compares it with the offset this build compiled, prints both, and
 *  refuses to scan if they differ. It is a check, not a correction: a hardcoded offset
 *  would paper over a header disagreement that would go on to bite the next field
 *  anybody adds past ScenarioName. Neither this file nor the gate names the number that
 *  offset comes out to, because the number is platform dependent by construction
 *  (255 + 8 off Windows, 256 + 256 on it) and a build that legitimately computes a
 *  different one is still correct. AGREEMENT is the assertion, not a value.
 * ---------------------------------------------------------------------------------- */

/* The cells this scenario places one on, in absolute cell coordinates, the same frame
   g_walls and the shroud use. Filled once at scenario start; the terrain does not
   change under it. */
struct WreckCell { int x, y; };
static std::vector<WreckCell> g_wrecks;

/* Off switches, in the shape the other passes use: one for the feature and one for the
   mesh lookup's one-time cache. */
static bool g_wreckDraw = true;
static int  g_wreckMesh = -2;      /* -2 not looked up yet, -1 looked up and absent */

/* THE PACK CODE. The bake names this mesh after the PC template, for the reason at the
   top of this file: the cartridge has no name of its own to use. */
#define WRECK_TYPE_CODE "P04"

/* HOW FAR ABOVE THE GROUND THE MODEL SITS: not at all, and it is a named constant so
   that the number the pass uses and the number the dump prints cannot drift apart.
   draw_mesh already stands a model on terrain_y under its anchor and adds this on top,
   in WORLD CELLS, so anything but zero floats the airframe. That is not hypothetical:
   a sibling gate once reported green with a crate lifted half a cell into the air,
   because it asserted only that a mesh had been drawn. The gate below asserts where the
   pixels landed relative to the ground point printed here. */
static const float WRECK_YLIFT = 0.0f;

/* WHERE THE BRAIN ACTUALLY PUT StaticCells, found rather than assumed: the first
   NUL-terminated string in the raw buffer that ends in ".tga", walked back to its
   start. Every exported cell name ends that way and nothing before StaticCells does,
   so the first hit IS the array. Returns (size_t)-1 when the buffer holds no cell name
   at all, which is a different failure and is reported as one. */
static size_t wreck_brain_cells_offset(const CNCMapDataStruct& md)
{
    const unsigned char* raw = (const unsigned char*)&md;
    for (size_t k = 0; k + 8 < sizeof md; k++) {
        if (memcmp(raw + k, ".tga", 4) == 0) {
            size_t j = k;
            while (j > 0 && raw[j - 1] != 0) j--;
            return j;
        }
    }
    return (size_t)-1;
}

/* THE PRINTED ASSERTION. One line in every log, whichever way it goes, because a silent
   agreement is worth as much as a silent disagreement is dangerous. The machine-readable
   line carries a VERDICT WORD and the two offsets beside it; the gate reads the verdict
   and never the offsets, so a platform where the pair legitimately comes out elsewhere
   still passes. */
static bool wreck_layout_ok(const CNCMapDataStruct& md)
{
    const size_t host  = (size_t)offsetof(CNCMapDataStruct, StaticCells);
    const size_t brain = wreck_brain_cells_offset(md);
    if (brain == (size_t)-1) {
        fprintf(stderr, "STATICCELLS|verdict=NOCELLS|host=%lu|brain=-1|skew=0|rec=%lu\n",
                (unsigned long)host, (unsigned long)sizeof(CNCStaticCellStruct));
        fprintf(stderr, "STATICCELLS: the brain returned a map buffer with no cell name "
                        "in it, so its layout cannot be checked and no terrain "
                        "decoration is read.\n");
        return false;
    }
    fprintf(stderr, "STATICCELLS|verdict=%s|host=%lu|brain=%lu|skew=%+ld|rec=%lu\n",
            (host == brain) ? "AGREE" : "DISAGREE",
            (unsigned long)host, (unsigned long)brain,
            (long)host - (long)brain,
            (unsigned long)sizeof(CNCStaticCellStruct));
    fprintf(stderr, "STATICCELLS: brain wrote the array at %lu, this build compiled it "
                    "at %lu (skew %+ld), cell record %lu bytes -- %s\n",
            (unsigned long)brain, (unsigned long)host,
            (long)host - (long)brain,
            (unsigned long)sizeof(CNCStaticCellStruct),
            (host == brain) ? "AGREE" : "DISAGREE");
    if (host != brain) {
        fprintf(stderr,
                "*** THE HOST AND THE BRAIN DISAGREE ABOUT CNCMapDataStruct.\n"
                "*** Only StaticCells is affected: it is the sole field after "
                "ScenarioName, which is char[_MAX_FNAME + _MAX_EXT], and those two "
                "macros are set in cnc_eyes.cpp from the brain's own wwstd.h. Every "
                "field before it (MapCellX..Theater, and ScenarioName's own start) is "
                "at the same offset either way and is still correct.\n"
                "*** Nothing is read through the skew and no terrain decoration is "
                "drawn. Rebuild both sides from one tree.\n");
        return false;
    }
    return true;
}

/* Is this exported cell name the template the wreck stands on? The exported string is
   "<IniName>_i<icon>.tga"; compare the stem case-insensitively and take the icon from
   the struct's own field rather than by re-parsing the string, because the cartridge
   selects on the (template, icon) PAIR and the pair is what has to match. */
static bool wreck_is_our_cell(const CNCStaticCellStruct& c)
{
    if (c.IconNumber != 0)
        return false;
    const char* n = c.TemplateTypeName;
    const char* w = WRECK_TYPE_CODE;
    size_t i = 0;
    for (; w[i]; i++) {
        char a = n[i], b = w[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return n[i] == '_' && n[i + 1] == 'i';
}

/* Read the scenario's terrain once. Safe to call on a brain that failed the layout
   check: it simply records nothing. */
static void wreck_scan(const CNCMapDataStruct& md)
{
    g_wrecks.clear();
    g_wreckMesh = -2;
    if (!wreck_layout_ok(md))
        return;
    const int w = md.MapCellWidth, h = md.MapCellHeight;
    if (w <= 0 || h <= 0)
        return;
    long total = (long)w * (long)h;
    if (total > (long)MAX_EXPORT_CELLS)
        total = (long)MAX_EXPORT_CELLS;
    for (long i = 0; i < total; i++) {
        if (!wreck_is_our_cell(md.StaticCells[i]))
            continue;
        WreckCell c;
        c.x = md.MapCellX + (int)(i % w);
        c.y = md.MapCellY + (int)(i / w);
        g_wrecks.push_back(c);
    }
    fprintf(stderr, "wreck: %d cell%s carry N64 draw index 38 (PC template 70 "
                    "\"%s\", icon 0) on this map\n",
            (int)g_wrecks.size(), g_wrecks.size() == 1 ? "" : "s", WRECK_TYPE_CODE);
}

/* The mesh, looked up once. A pack baked before this mesh existed simply has no such
   type, and that is said once rather than silently drawing nothing forever. */
static int wreck_mesh(void)
{
    if (g_wreckMesh != -2)
        return g_wreckMesh;
    std::map<std::string, PackType>::iterator it = g_pack.type.find(WRECK_TYPE_CODE);
    g_wreckMesh = (it == g_pack.type.end()) ? -1 : it->second.mesh;
    if (g_wreckMesh < 0 && !g_wrecks.empty())
        fprintf(stderr, "wreck: this pack carries no \"%s\" mesh, so %d cell%s that "
                        "should show one show bare ground. Re-bake the packs.\n",
                WRECK_TYPE_CODE, (int)g_wrecks.size(),
                g_wrecks.size() == 1 ? "" : "s");
    return g_wreckMesh;
}

/* The one predicate this pass applies, written once so that the draw and the dump can
   never answer differently about the same cell. */
static bool wreck_cell_visible(const WreckCell& c, int mi)
{
    return g_wreckDraw && mi >= 0 && cell_shown(c.x, c.y)
           && !shroud_cell_hidden(c.x, c.y);
}

/* Drawn with the walls' own two tests and the walls' own anchor. The cartridge's
   cell-decoration path passes yaw 0 and takes its piece offset from entry 0 of the
   connectivity table at RAM 0x802100D0, which is offset 0, so the model is UNROTATED on
   the cell centre; face -1 is how this renderer spells that. Per-vertex ground for the
   same reason the walls take it: the airframe is 0.81 x 0.67 cells and overhangs its
   own cell, so a single ground height under the anchor would float one end of it on
   any slope. */
static void wreck_draw(void)
{
    if (!g_wreckDraw || g_wrecks.empty())
        return;
    const int mi = wreck_mesh();
    if (mi < 0)
        return;
    g_meshGroundPerVertex = true;
    for (size_t i = 0; i < g_wrecks.size(); i++) {
        const WreckCell& c = g_wrecks[i];
        if (!wreck_cell_visible(c, mi))
            continue;
        draw_mesh(mi, (float)c.x + 0.5f, (float)c.y + 0.5f, -1, MODE_OPAQUE,
                  0, 0.0f, 0, 1.0f, WRECK_YLIFT);
        g_nMesh++;
    }
    g_meshGroundPerVertex = false;
}

/* THE GATE'S OWN WINDOW ON THIS PASS, built like walldump's beside it and then carried
   one step further, because walldump's shape is exactly what let a sibling gate report
   green with a floating crate. A mesh index of -1 says the pack was not re-baked, and
   drawn=0 on a cell the camera is looking at says the cull is wrong rather than the art
   -- but neither says WHERE the model went. So this also prints, per cell, the anchor
   the pass hands draw_mesh, the terrain height under that anchor, the lift added on top
   of it, and the SCREEN POINT that ground position projects to under the camera as it
   stands. The gate holds that screen point against the pixels that actually changed,
   which is a claim about position that drawing in the wrong place cannot satisfy. */
static void wreck_dump(int fbw, int fbh)
{
    const int mi = wreck_mesh();
    int drawn = 0;
    for (size_t i = 0; i < g_wrecks.size(); i++) {
        const WreckCell& c = g_wrecks[i];
        /* The SAME predicate the pass itself applies, evaluated here rather than read
           back off the last frame, so the dump answers even before a frame has been
           drawn, which is how walldump beside it behaves. */
        const int shown = wreck_cell_visible(c, mi) ? 1 : 0;
        drawn += shown;
        const float ax = (float)c.x + 0.5f, az = (float)c.y + 0.5f;
        const float gy = terrain_y(ax, az);
        /* THE GROUND POINT, NOT THE MODEL'S ORIGIN, and the difference is the whole
           value of this line. Projecting gy + WRECK_YLIFT would move this reference by
           exactly the amount any lift moves the pixels, and the gate comparing the two
           would agree with itself no matter how far off the ground the model floated.
           gy alone is a fact about the terrain that the draw cannot influence. */
        float col = -1.0f, row = -1.0f;
        world_to_screen(ax, gy, az, fbw, fbh, &col, &row);
        printf("WRECKDUMP|%d,%d|mesh=%d|drawn=%d|anchor=%.3f,%.3f|ground=%.3f|"
               "lift=%.3f|screen=%.1f,%.1f\n",
               c.x, c.y, mi, shown, ax, az, gy, WRECK_YLIFT, col, row);
    }
    printf("WRECKDUMP-END|cells=%d|drawn=%d|mesh=%d|code=%s|fb=%dx%d\n",
           (int)g_wrecks.size(), drawn, mi, WRECK_TYPE_CODE, fbw, fbh);
}
