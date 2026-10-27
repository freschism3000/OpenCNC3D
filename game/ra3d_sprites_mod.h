/* =====================================================================================
 *  ra3d_sprites_mod.h -- Red Alert in the 3D modes: its own sprites, on the 3D ground.
 *
 *  Tiberian Dawn's 3D modes draw the cartridge's models (and the remastered ones) by
 *  object type. Red Alert has neither, and Tiberian Dawn's look-alikes are the wrong art:
 *  its jeep is not the Ranger and its minigunner is not the Allied rifleman, and every
 *  Red Alert building without a Tiberian Dawn namesake fell back to a pale box. So under
 *  Red Alert, outside Classic, the model passes stand down and every object is drawn as
 *  the very sprite Classic draws -- the engine's own draw record (asset, frame, remap,
 *  sort order, from GAME_STATE_LAYERS and GAME_STATE_DYNAMIC_MAP, exactly what
 *  dos2d_scene reads), stood in the 3D world:
 *
 *    - an object is a card facing the camera (the infantry billboards' own basis,
 *      g_bbRight and g_bbUp), its centre at the object's point on the 3D ground. The 2D
 *      game centres every one of these sprites on the object's coordinate, and the art
 *      is drawn from about the camera's own three-quarter view, so the card reproduces
 *      the Classic picture at the object's place and height on the relief;
 *    - an overlay that lies on the ground (ore, gems, a smudge) is a decal draped over
 *      the terrain's four corner heights; a wall or a fence (the sellable overlays)
 *      stands up like an object;
 *    - the engine's animations (explosions, the move flash, muzzle fire) are cards too,
 *      a ground animation lying flat.
 *
 *  Cards retain the engine's painter order with depth testing against the ground and
 *  imported solid buildings. The shroud follows this pass.
 *
 *  This is a first pass: the sprites keep the 1995 game's pixel art and its eight or
 *  thirty-two facings. 3D models replace them type by type as they are made.
 * ===================================================================================== */
#pragma once

static bool g_ra3dSprites = false;   /* this frame: Red Alert, drawn in a 3D mode */
static int  g_ra3dCards = 0, g_ra3dDecals = 0, g_ra3dMissing = 0;
static int  g_ra3dModelRoots = 0, g_ra3dModelSubs = 0, g_ra3dFallbackBuildings = 0;
/* The vehicle tuning mask replays the exact upright cards the colour frame drew,
   in their painter order. Non-vehicle cards erase any vehicle beneath their opaque
   texels. This separate mask leaves the cards' inherited ground lighting intact. */
struct Ra3dVehicleMaskCard { GLuint tex; float uv[4], v[4][3]; bool vehicle; };
static std::vector<Ra3dVehicleMaskCard> g_ra3dVehicleMaskCards;
static bool g_ra3dVehicleMaskCapture = false, g_ra3dVehicleMaskNext = false;
static void ra3d_draw_vehicle_mask(void);
static bool ra3d_vehicle_type(int type)
{
    return type == UNIT || type == AIRCRAFT || type == VESSEL;
}
/* this frame: the structures going up that have a placement (ebld_place.h, the Construction
   Yard deploying out of its MCV), stood down for it, and drawn as their 1996 card anyway */
static int  g_ra3dPlacedModels = 0, g_ra3dPlacedCards = 0;
/* THE TESLA COIL'S BOLT. TechnoClass::Electric_Zap draws the LITNING pieces through the
   firer's own CC_Draw_Shape(this, ...), so the layer export files every piece as a
   subobject of the coil, and a coil drawn by its imported model used to take its bolt
   away with the rest of its sprite parts. g_ra3dZapCards counts the pieces the last
   frame drew; `ra3dzap 0` leaves them out again, the old drawing, for a gate's control. */
static int  g_ra3dZapCards = 0;
static int  g_ra3dOreDecals = 0, g_ra3dMineCards = 0;   /* the last frame's 1996 ore decals, mine cards */
static bool g_ra3dZapOff = false;
static int  g_ra3dAnimCards = 0;   /* the last frame's engine anims drawn as 1996 cards */
/* and their names, ground and air alike, for the effects' gate (efxmdump) */
static std::string g_ra3dAnimCardNames;
static int  g_ra3dBulletsLifted = 0;   /* and its projectiles stood at their own height */
/* The terrain objects (trees, the ore mine) the last frame stood, for ra3dprobe: the
   engine's cell, its draw flags, the sprite's size and where the card's centre went. A
   tree drawn as its reused model (ebld_terr.h) is recorded too, with where its card would
   have gone, model set and the point the model stands on (mx, mz). */
struct Ra3dTerrainRec { char type[16]; int cellX, cellY, flags, w, h; float cx, cz; bool topLeft;
                        bool model; float mx, mz; };
static std::vector<Ra3dTerrainRec> g_ra3dTerrainRecs;
static bool g_ra3dCentreAll = false;
static int g_ra3dTreeModels = 0;   /* the last frame's trees drawn as their models */
/* the last frame's units and aircraft whose cards stood down for their remastered models
   (evhc_mod.h), and the parts of their pictures (turrets, rotors, shadows) with them */
static int g_ra3dVehicleModels = 0, g_ra3dVehicleCards = 0;
/* The building cards the last frame stood, for footdump: the building's id, the art,
   and the card's four corners in the world (top left, top right, bottom right, bottom
   left of the picture). Only a building's own body card, not its subobjects. */
struct Ra3dCardRec {
    int id; char art[16]; float v[4][3];
    int foot, h;           /* the sprite's foot row (ra3d_foot_row) and its height */
    float cx, cz;          /* the sprite's centre on the 2D ground, in cells */
};
static std::vector<Ra3dCardRec> g_ra3dCardRecs;
static Ra3dCardRec* g_ra3dCardOut = NULL;   /* where ra3d_card writes the next card's corners */

/* WHERE EVERY BODY CARD STOOD ON THE LAST FRAME, for the effects that ride an object drawn
   as its card (ra3d_effects_mod.h): a muzzle flash, a burning tree's flames. Keyed as the
   sprite pass keys its bodies (the record's type and id); cx, cz is the picture's centre on
   the 2D ground, c the card's centre in the world, and the frame's billboard basis is kept
   beside them, so a point of the 2D picture can be found on the card as the pass placed it. */
struct Ra3dBodyCard { float cx, cz, c[3]; };
static std::map<long long, Ra3dBodyCard> g_ra3dBodyCards;
static float g_ra3dBodyR[3] = { 1, 0, 0 }, g_ra3dBodyU[3] = { 0, 1, 0 };

/* HOW MUCH OF EACH CARD THE PLAYER SEES, measured by the card itself: `ra3dcardvis`
   arms the next frame, and every body card (an object's own picture, not its parts) and
   every wall is drawn under two occlusion queries, once with the depth test off and
   nothing written (every fragment its opaque texels cover) and once as drawn. The ratio
   is the share of its own picture on screen. A card sunk into the ground shows about
   half; one standing on open ground shows all of it. Read with ra3dprobe. */
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
struct Ra3dCardVis {
    char art[16];
    int type;              /* the record's DllObjectTypeEnum; -1 a wall or a fence */
    unsigned q[2];         /* the two queries: every fragment, and the ones drawn */
    unsigned full, seen;
};
static std::vector<Ra3dCardVis> g_ra3dVis;
static bool g_ra3dVisArm = false, g_ra3dVisNow = false;
static int  g_ra3dVisNext = -2;            /* the type of the next card to measure; -2 none */
static void (APIENTRY *ra3d_q_gen)(GLsizei, GLuint*) = NULL;
static void (APIENTRY *ra3d_q_del)(GLsizei, const GLuint*) = NULL;
static void (APIENTRY *ra3d_q_begin)(GLenum, GLuint) = NULL;
static void (APIENTRY *ra3d_q_end)(GLenum) = NULL;
static void (APIENTRY *ra3d_q_get)(GLuint, GLenum, GLuint*) = NULL;
static bool ra3d_vis_ready(void)
{
    static int ok = -1;
    if (ok < 0) {
        ra3d_q_gen = (decltype(ra3d_q_gen))SDL_GL_GetProcAddress("glGenQueries");
        ra3d_q_del = (decltype(ra3d_q_del))SDL_GL_GetProcAddress("glDeleteQueries");
        ra3d_q_begin = (decltype(ra3d_q_begin))SDL_GL_GetProcAddress("glBeginQuery");
        ra3d_q_end = (decltype(ra3d_q_end))SDL_GL_GetProcAddress("glEndQuery");
        ra3d_q_get = (decltype(ra3d_q_get))SDL_GL_GetProcAddress("glGetQueryObjectuiv");
        ok = ra3d_q_gen && ra3d_q_del && ra3d_q_begin && ra3d_q_end && ra3d_q_get ? 1 : 0;
        if (!ok) printf("RA3DPROBE|vis|unavailable|no occlusion queries\n");
    }
    return ok == 1;
}

/* WHERE A BOLT LEAVES AN IMPORTED COIL. The engine starts the bolt at the coil's
   Fire_Coord, VerticalOffset (200 leptons for TSLA, bdata.cpp) up its picture from the
   centre, which on the 2D screen is the top of the 1996 sprite. A card stood on the ground
   at that screen point lands most of a cell behind the model, where the model's own body
   hides the first pieces, so the bolt seemed to begin in mid air beyond the coil. For a coil
   drawn by its model the bolt is laid again in 3D: every piece keeps how far along the
   straight line from the source to the target it is (t) and how far it has wandered off
   that line (the engine's random walk), and stands on the 3D line from the model's crown
   (t = 0) down to the target's ground (t = 1). The target is the piece furthest from the
   source, which the walk ends within eight pixels of. */
struct Ra3dBolt {
    float sx, sz;          /* the source, on the 2D game's ground, in cells          */
    float tx, tz;          /* the target, the same way                               */
    float cx, cy, cz;      /* the model's crown in the 3D world                      */
    float far2;            /* the furthest piece's squared distance from the source  */
    float near2;           /* and the nearest's, which the probe reports             */
};
static std::vector<Ra3dBolt> g_ra3dBolts;   /* the last frame's, for ra3dprobe */
static const float RA3D_TESLA_VOFFSET = 200.0f / 256.0f;   /* ClassTesla's VerticalOffset */
static const float RA3D_BOLT_CLEAR = 0.25f;   /* a piece's least height over the ground, cells */

static void ra3d_bolt_place(const Ra3dBolt& b, float px, float pz, float* x, float* z, float* lift)
{
    const float dx = b.tx - b.sx, dz = b.tz - b.sz;
    const float len2 = dx * dx + dz * dz;
    float t = len2 > 1e-6f ? ((px - b.sx) * dx + (pz - b.sz) * dz) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float nx = (px - b.sx) - t * dx, nz = (pz - b.sz) - t * dz;   /* the wander */
    *x = b.cx + t * (b.tx - b.cx) + nx;
    *z = b.cz + t * (b.tz - b.cz) + nz;
    const float y = b.cy + t * (terrain_y(b.tx, b.tz) - b.cy);
    /* never into a rise of the ground between the two: a straight line from the crown to
       a target up a slope passes under the slope, and the ground hid the middle of it */
    *lift = fmaxf(y - terrain_y(*x, *z), RA3D_BOLT_CLEAR);
}

static void ra3d_frame_begin(void)
{
    g_ra3dSprites = g_raModule && !g_fx.dos_classic;
    /* whether OpenCNC3D's effects draw this frame, which decides the anim cards below, read
       now rather than from the last tick: an Enhanced effects switch thrown between two
       ticks left a frame with neither the cards nor the effects */
    if (g_ra3dSprites) efx_enh_follow();
    /* The theater letter that picks a theater's art (S/, T/, I/) reads the brain's static
       map, which only Classic's terrain pass fetched: a mission that never drew a Classic
       frame looked up its interior crates as temperate ones. */
    if (g_ra3dSprites) ra2d_fetch_map();
}

/* ONLY A CONVERTED MISSION LEAVES CLASSIC. A Red Alert mission's 3D scene is its own pack
   (ra3d/3d/<SCEN>.pack, theater RASNOW and its kin); a mission not converted yet is handed
   the shared placeholder, which is Tiberian Dawn's ground and not this mission's. Asked
   for N64 or Enhanced there, the mode itself goes back to Classic -- not just the picture,
   because the click mapping, the camera and the HUD all follow the mode. Called at the top
   of every frame, before anything reads it. */
static void ra3d_enforce_scene(void)
{
    static int said = 0;
    if (!g_raModule || g_fx.dos_classic) return;
    if (g_pack.theater[0] == 'R' && g_pack.theater[1] == 'A') return;
    g_fx.dos_classic = 1;
    g_fx.enabled = 0;
    if (!said) {
        fprintf(stderr, "RA3D|visuals|%s has no 3D scene yet; it stays in Classic\n", g_bootScen);
        said = 1;
    }
}

static bool ra3d_ore_off(void)
{
    static int off = -1;
    if (off < 0) { const char* e = getenv("OPENCNC3D_RA3DORE"); off = (e && !strcmp(e, "off")) ? 1 : 0; }
    return off != 0;
}

/* The shape and its frame rectangle, uploaded for this remap/ghost/flash/shadow. */
static Dos2DShape* ra3d_shape(const char* name, int frame, int remap, bool ghost, bool flash,
                              bool shadow, float uv[4])
{
    Dos2DShape* s = dos2d_shape(name);
    if (!s) {
        static std::set<std::string> missing;
        g_ra3dMissing++;
        if (missing.insert(name).second) fprintf(stderr, "RA3D|sprite|missing original SHP %s\n", name);
        return NULL;
    }
    if (!dos2d_upload(s, remap, ghost, flash, shadow, false)) return NULL;
    const int fr = frame < s->frames && frame >= 0 ? frame : 0;
    const int sx = (fr % s->cols) * s->w, sy = (fr / s->cols) * s->h;
    glBindTexture(GL_TEXTURE_2D, s->tex[dos2d_tex_slot(remap, ghost, flash, shadow, false)]);
    uv[0] = (float)sx / s->tw; uv[1] = (float)sy / s->th;
    uv[2] = (float)(sx + s->w) / s->tw; uv[3] = (float)(sy + s->h) / s->th;
    return s;
}

static void ra3d_blend(bool on)
{
    if (on) {
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glAlphaFunc(GL_GREATER, 0.0f);
    } else {
        glDisable(GL_BLEND); glAlphaFunc(GL_GREATER, 0.5f);
    }
}

/* An upright card: its centre at (cx, gy, cz) in the world, in cells. */
static void ra3d_card_at(const char* name, int frame, float cx, float gy, float cz,
                         int remap = -1, bool ghost = false, bool flash = false,
                         bool shadow = false)
{
    float uv[4];
    Dos2DShape* s = ra3d_shape(name, frame, remap, ghost, flash, shadow, uv);
    if (!s) { g_ra3dVisNext = -2; return; }
    const float hw = s->w / 48.0f, hh = s->h / 48.0f;     /* 24 texels a cell, halved */
    const float R[3] = { g_bbRight[0] * hw, g_bbRight[1] * hw, g_bbRight[2] * hw };
    const float U[3] = { g_bbUp[0] * hh, g_bbUp[1] * hh, g_bbUp[2] * hh };
    if (g_ra3dVehicleMaskCapture) {
        Ra3dVehicleMaskCard card;
        card.tex = s->tex[dos2d_tex_slot(remap, ghost, flash, shadow, false)];
        card.vehicle = g_ra3dVehicleMaskNext && !shadow;
        for (int k = 0; k < 4; k++) card.uv[k] = uv[k];
        const float sR[4] = { -1, 1, 1, -1 }, sU[4] = { 1, 1, -1, -1 };
        for (int k = 0; k < 4; k++) {
            card.v[k][0] = cx + sR[k] * R[0] + sU[k] * U[0];
            card.v[k][1] = gy + sR[k] * R[1] + sU[k] * U[1];
            card.v[k][2] = cz + sR[k] * R[2] + sU[k] * U[2];
        }
        g_ra3dVehicleMaskCards.push_back(card);
    }
    if (g_ra3dCardOut) {
        const float sR[4] = { -1, 1, 1, -1 }, sU[4] = { 1, 1, -1, -1 };
        for (int k = 0; k < 4; k++) {
            g_ra3dCardOut->v[k][0] = cx + sR[k] * R[0] + sU[k] * U[0];
            g_ra3dCardOut->v[k][1] = gy + sR[k] * R[1] + sU[k] * U[1];
            g_ra3dCardOut->v[k][2] = cz + sR[k] * R[2] + sU[k] * U[2];
        }
        g_ra3dCardOut = NULL;
    }
    ra3d_blend(ghost || shadow);
    const auto quad = [&]() {
        glBegin(GL_QUADS);
        glTexCoord2f(uv[0], uv[1]); glVertex3f(cx - R[0] + U[0], gy - R[1] + U[1], cz - R[2] + U[2]);
        glTexCoord2f(uv[2], uv[1]); glVertex3f(cx + R[0] + U[0], gy + R[1] + U[1], cz + R[2] + U[2]);
        glTexCoord2f(uv[2], uv[3]); glVertex3f(cx + R[0] - U[0], gy + R[1] - U[1], cz + R[2] - U[2]);
        glTexCoord2f(uv[0], uv[3]); glVertex3f(cx - R[0] - U[0], gy - R[1] - U[1], cz - R[2] - U[2]);
        glEnd();
    };
    const bool measure = g_ra3dVisNow && g_ra3dVisNext != -2 && !shadow
                      && g_ra3dVis.size() < 1024 && ra3d_vis_ready();
    if (measure) {
        Ra3dCardVis v = {};
        snprintf(v.art, sizeof v.art, "%s", name);
        v.type = g_ra3dVisNext;
        ra3d_q_gen(2, v.q);
        GLboolean mask[4];
        glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        ra3d_q_begin(GL_SAMPLES_PASSED, v.q[0]); quad(); ra3d_q_end(GL_SAMPLES_PASSED);
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        if (depth) glEnable(GL_DEPTH_TEST);
        ra3d_q_begin(GL_SAMPLES_PASSED, v.q[1]); quad(); ra3d_q_end(GL_SAMPLES_PASSED);
        g_ra3dVis.push_back(v);
    } else {
        quad();
    }
    g_ra3dVisNext = -2;
    if (ghost || shadow) ra3d_blend(false);
    g_ra3dCards++;
}

/* An upright card centred on the ground at (cx, cz), raised by lift. */
static void ra3d_card(const char* name, int frame, float cx, float cz, int remap = -1,
                      bool ghost = false, bool flash = false, bool shadow = false,
                      float lift = 0.0f)
{
    ra3d_card_at(name, frame, cx, terrain_y(cx, cz) + lift, cz, remap, ghost, flash, shadow);
}

/* THE LOWEST ROW A FRAME DRAWS, from its top: the sprite's foot. A frame that draws
   nothing reports its last row. */
static int ra3d_foot_row(const Dos2DShape* s, int frame)
{
    if (!s) return 0;
    static std::map<std::string, std::vector<int> > feet;
    std::vector<int>& rows = feet[s->name];
    if (rows.empty()) {
        const size_t size = (size_t)s->w * s->h * s->frames;
        std::vector<unsigned char> indices(size);
        uLongf bytes = (uLongf)size;
        rows.assign(s->frames, s->h - 1);
        if (uncompress(indices.data(), &bytes, s->zipped.data(), (uLong)s->zipped.size()) == Z_OK
            && bytes == size) {
            for (int f = 0; f < s->frames; f++) {
                bool found = false;
                for (int y = s->h - 1; y >= 0 && !found; y--)
                    for (int x = 0; x < s->w; x++)
                        if (indices[((size_t)f * s->h + y) * s->w + x]) {
                            rows[f] = y; found = true; break;
                        }
            }
        }
    }
    const int f = frame >= 0 && frame < s->frames ? frame : 0;
    return rows[f];
}

/* WHERE A CARD STANDS. The 2D game draws a sprite flat on its 24-pixel grid, so every
   row of the picture lies over a row of the ground: the sprite's centre over the point
   the engine drew it at, its lowest drawn row (its foot: a building's base, a tank's
   tracks, a wall's footing) that many rows further south. A card stood upright in the 3D
   world keeps the picture at 24 texels a cell and must put that foot on the ground where
   the 2D game puts it; everything above the foot then rises from there, as the 1996 art
   rises from its base.

   Two ways of getting this wrong were measured. Centred on the ground point, a card's
   lower half lies under the terrain, which hides it: half of every tank, ship and wall
   was missing. Lifted until its foot touched the ground at the CENTRE point (what
   buildings did), the base stood at the middle of the footprint, half a building north of
   its own cells, and the picture reached a further half building over its northern
   neighbour.

   (cx, cz) is the sprite's centre on the 2D ground, in cells; the card's centre in the
   world is written to c[3]. A hair of lift keeps the foot off the ground's depth. */
static bool g_ra3dStandOff = false;   /* `ra3dstand 0`: the old placement, for a control */
static void ra3d_stand(const Dos2DShape* s, int frame, float cx, float cz, float c[3])
{
    if (!s) { c[0] = cx; c[1] = terrain_y(cx, cz); c[2] = cz; return; }
    const float d = ((ra3d_foot_row(s, frame) + 1.0f) - s->h * 0.5f) / 24.0f;  /* centre to foot */
    const float zf = cz + d;                                                     /* the foot's row */
    c[0] = cx + g_bbUp[0] * d;
    c[1] = terrain_y(cx, zf) + g_bbUp[1] * d + 0.02f;
    c[2] = zf + g_bbUp[2] * d;
}

/* THE OLD BUILDING LIFT, kept for `ra3dstand 0`: the foot raised to the ground at the
   sprite's centre, and that lift given to every subobject. */
static float ra3d_building_lift(const Dos2DShape* s, int frame)
{
    if (!s) return 0.0f;
    return fmaxf(0.0f, g_bbUp[1] * ((ra3d_foot_row(s, frame) + 1.0f) - s->h * 0.5f) / 24.0f) + 0.02f;
}

/* A decal: the sprite's own rectangle (top-left x0, z0 in cells) laid on the ground,
   each corner at the terrain's height there, lifted a hair so it does not fight it. */
static void ra3d_decal(const char* name, int frame, float x0, float z0, bool ghost = false)
{
    float uv[4];
    Dos2DShape* s = ra3d_shape(name, frame, -1, ghost, false, false, uv);
    if (!s) return;
    const float x1 = x0 + s->w / 24.0f, z1 = z0 + s->h / 24.0f, lift = 0.01f;
    ra3d_blend(ghost);
    glBegin(GL_QUADS);
    glTexCoord2f(uv[0], uv[1]); glVertex3f(x0, terrain_y(x0, z0) + lift, z0);
    glTexCoord2f(uv[2], uv[1]); glVertex3f(x1, terrain_y(x1, z0) + lift, z0);
    glTexCoord2f(uv[2], uv[3]); glVertex3f(x1, terrain_y(x1, z1) + lift, z1);
    glTexCoord2f(uv[0], uv[3]); glVertex3f(x0, terrain_y(x0, z1) + lift, z1);
    glEnd();
    if (ghost) ra3d_blend(false);
    g_ra3dDecals++;
}

/* A CRATER OR A SCORCH MARK, as the SmudgeType both games number alike: CR1..CR6 are 0..5,
   SC1..SC6 are 6..11. -1 for anything else, the building aprons BIB1..BIB3 among them. */
static int ra3d_mark_type(const CNCDynamicMapEntryStruct& d)
{
    if (!d.IsSmudge) return -1;
    const char* n = d.AssetName;
    const bool cr = n[0] == 'C' && n[1] == 'R', sc = n[0] == 'S' && n[1] == 'C';
    if ((!cr && !sc) || n[2] < '1' || n[2] > '6' || n[3]) return -1;
    return (sc ? 6 : 0) + (n[2] - '1');
}

/* OpenCNC3D's remastered craters and scorch marks (decalenh_mod.h) for Red Alert's: the
   temperate set on both of Red Alert's outdoor theaters, as the aprons do (the decal pack
   has no winter set). On snow the burn is its grey char without the temperate tint, which
   turned the snow round a crater peach (decalenh_snow_soot); a crater's body is the same
   soil, blasted through the snow. Interior keeps the 1996 art. NULL: the 1996 decals. */
static const DecalEnhTheater* ra3d_marks_set(void)
{
    return (g_deOn && g_deHave && g_fx.enabled && g_fx.decal_enh && ra2d_theater_letter() != 'I')
         ? decalenh_set_for(2) : NULL;
}
static int g_ra3dMarks = 0;   /* the last frame's cells drawn in the remastered art */

/* THE CELL DECORATIONS REUSED FROM OPENCNC3D'S ART: the steel crate (SCRATE) and the
   haystacks and fields placed as overlays (V12 to V18), the same 1995 pictures in both
   games, and the wooden crate (WCRATE), whose 1996 drawing is a recolour of the 1995 one
   and whose lab row reuses OpenCNC3D's approved model all the same. Tiberian Dawn's dump lists them as CRATE| and FIELD| lines; Red Alert's does not,
   so on every parse its engine's map fills the same two lists (g_crates, g_fields), and
   the passes that already read them draw their remastered models: the sun's casters and
   the light pass's normals (eterr_draw_aux, draw_fields, eterr_draw_normals), and the
   colour pass below (ra3d_draw_cells). A type with no reused model is not drawn from the
   lists at all under Red Alert; its 1996 picture stays (ra3d_draw_sprites). */
static int ra3d_field_kind(const char* n)
{
    if (n[0] != 'V' || n[1] != '1' || n[2] < '2' || n[2] > '8' || n[3]) return -1;
    return n[2] - '2';
}
/* THE WALLS, the same way: Tiberian Dawn's dump has a WALL| line per wall cell, Red
   Alert's engine map has the overlay, whose OverlayData is the same pair (cell.cpp:
   Wall_Update writes the connection nibble into its low four bits, N E S W; Reduce_Wall adds
   16 a damage level). The five walls both games share keep Tiberian Dawn's numbers (SBAG 0,
   CYCL 1, BRIK 2, BARB 3, WOOD 4: both engines' OverlayType order), with the same damage
   levels in both odata.cpp (1, 2, 3, 1, 1). Red Alert's own barbed-wire fence (FENC) has no
   counterpart: it is kind 5, which no wall model answers, so it keeps its 1996 picture, but
   it is a wall the player owns for the sell cursor and the wall line (cnc_sidebar.h).
   CellClass::Owner is a house number; the list carries its IniName, as the dump's does. */
static const char* const RA3D_WALL_NAMES[6] = { "SBAG", "CYCL", "BRIK", "BARB", "WOOD", "FENC" };
static const char* const RA3D_HOUSE_NAMES[20] = {
    "Spain", "Greece", "USSR", "England", "Ukraine", "Germany", "France", "Turkey",
    "GoodGuy", "BadGuy", "Neutral", "Special", "Multi1", "Multi2", "Multi3", "Multi4",
    "Multi5", "Multi6", "Multi7", "Multi8" };
static int ra3d_wall_kind(const char* n)
{
    for (int k = 0; k < 6; k++)
        if (!strcmp(n, RA3D_WALL_NAMES[k])) return k;
    return -1;
}
static int g_ra3dCellsCrates = 0, g_ra3dCellsFields = 0, g_ra3dCellsWalls = 0;   /* the last parse's */
/* THE CRATERS AND SCORCH MARKS, read on the same parse, so the stand-down of a reused
   building's death craters (ebld_marks_filter_ra) runs on the tick edge as Tiberian
   Dawn's does on g_smudges, whether or not a frame is drawn in between; the sprite pass
   draws what it leaves (ra3d_draw_sprites). */
static std::vector<SmudgeCell> g_ra3dMarkCells;
/* (the ore and gem cells, drawn as their models: ra3d_ore_mod.h) */
static int ra3d_ore_kind(const char* n);
static void ra3d_ore_cells_clear(void);
static void ra3d_ore_cell_add(int x, int y, int kind, int frame);
static bool ra3d_ore_claims_overlay(const char* name);
static void ra3d_ore_grass_claims(void);
static void ra3d_cells_from_map(void)
{
    g_ra3dCellsCrates = g_ra3dCellsFields = g_ra3dCellsWalls = 0;
    g_ra3dMarkCells.clear();
    g_ra3dPropCells.clear();
    ra3d_ore_cells_clear();
    if (!g_raModule || !BrainGetState) return;
    static std::vector<unsigned char> buf(sizeof(CNCDynamicMapStruct) + 8192 * sizeof(CNCDynamicMapEntryStruct));
    if (!BrainGetState(GAME_STATE_DYNAMIC_MAP, 0, buf.data(), (unsigned)buf.size())) return;
    const CNCDynamicMapStruct* dyn = (const CNCDynamicMapStruct*)buf.data();
    const int count = dyn->Count < 8192 ? dyn->Count : 8192;
    for (int i = 0; i < count; i++) {
        const CNCDynamicMapEntryStruct& d = dyn->Entries[i];
        const int t = ra3d_mark_type(d);
        if (t >= 0) {
            SmudgeCell c = {};
            c.x = d.CellX; c.y = d.CellY;
            c.type = (unsigned char)t; c.data = (unsigned char)d.ShapeIndex;
            g_ra3dMarkCells.push_back(c);
            continue;
        }
        /* (the cells below in the 3D modes only: Classic draws Red Alert's whole map from
           the engine's own records, dosclassic_mod.h, and keeps its behaviour exactly) */
        if (!d.IsOverlay || g_fx.dos_classic) continue;
        const int wk = ra3d_wall_kind(d.AssetName);
        if (wk >= 0) {
            WallCell w;
            w.x = d.CellX; w.y = d.CellY;
            w.kind = (unsigned char)wk;
            w.icon = (unsigned char)(d.ShapeIndex & 15);
            w.dmg = (unsigned char)((d.ShapeIndex >> 4) & 15);
            snprintf(w.owner, sizeof w.owner, "%s",
                     (d.Owner >= 0 && d.Owner < 20) ? RA3D_HOUSE_NAMES[(int)d.Owner] : "None");
            g_walls.push_back(w);
            g_ra3dCellsWalls++;
        } else if (!strcmp(d.AssetName, "SCRATE") || !strcmp(d.AssetName, "WCRATE")) {
            CrateCell c; c.x = d.CellX; c.y = d.CellY; c.steel = d.AssetName[0] == 'S' ? 1 : 0;
            g_crates.push_back(c);
            g_ra3dCellsCrates++;
        } else if (ra3d_field_kind(d.AssetName) >= 0) {
            FieldCell f; f.x = d.CellX; f.y = d.CellY;
            f.kind = (unsigned char)ra3d_field_kind(d.AssetName);
            g_fields.push_back(f);
            g_ra3dCellsFields++;
        } else if (ra3d_ore_kind(d.AssetName) >= 0) {
            /* an ore or gem cell: its arrangement and the density frame the engine draws */
            ra3d_ore_cell_add(d.CellX, d.CellY, ra3d_ore_kind(d.AssetName), d.ShapeIndex);
        } else if (ra3d_prop_cell_type(d.AssetName)) {
            /* (the water crate and the flag location, Red Alert's own: ra3d_props_mod.h) */
            Ra3dPropCell c;
            snprintf(c.type, sizeof c.type, "%s", d.AssetName);
            c.x = d.CellX; c.y = d.CellY;
            g_ra3dPropCells.push_back(c);
        }
    }
    ra3d_ore_grass_claims();
}
/* On every parse, after the deaths are observed and their anims filtered (the dump's own
   order, cnc_eyes.cpp): the marks a reused building's death left stand down. */
static void ra3d_marks_filter_tick(int frame)
{
    if (g_raModule) ebld_marks_filter_ra(g_ra3dMarkCells, frame);
}

/* Whether a 1996 overlay stands down for its reused model (ra3d_draw_cells draws a crate
   or a field, draw_walls a wall: whole, worn or in pieces). */
static bool ra3d_cell_model(const CNCDynamicMapEntryStruct& d)
{
    if (!d.IsOverlay) return false;
    /* (an ore or gem cell drawn as its model, ra3d_ore_draw) */
    if (ra3d_ore_claims_overlay(d.AssetName)) return true;
    const int wk = ra3d_wall_kind(d.AssetName);
    if (wk >= 0) {
        /* (the fence, kind 5, where its own approved model is staged: ra3d_props_mod.h) */
        if (wk > 5 || !g_wallsDraw || g_ebld.empty() || !ebld_rule_now()) return false;
        if (wk == 5 && !ra3d_props_on()) return false;
        ebld_wall_models();
        return g_ebldWallModel[wk] >= 0;
    }
    if (ra3d_prop_cell_type(d.AssetName)) return ra3d_prop_cell_model(d.AssetName);
    if (!strcmp(d.AssetName, "SCRATE") || !strcmp(d.AssetName, "WCRATE"))
        return eterr_model(d.AssetName) >= 0;
    const int k = ra3d_field_kind(d.AssetName);
    return k >= 0 && eterr_model(FIELD_MESH_KEY[k]) >= 0;
}
/* The colour pass's draw of them, in the object passes' pass: each at its cell's centre,
   as Tiberian Dawn draws them (draw_crates, draw_fields); a cell whose type has no reused
   model draws nothing here. */
static void ra3d_draw_cells(int pass)
{
    if (!g_ra3dSprites) return;
    /* (and Red Alert's own cell decorations, the water crate and the flag location) */
    if (pass == MODE_OPAQUE) g_ra3dPropCellsDrawn = 0;
    ra3d_props_draw_cells(pass);
    for (size_t i = 0; g_cratesDraw && i < g_crates.size(); i++) {
        const CrateCell& c = g_crates[i];
        if (!cell_shown(c.x, c.y) || shroud_cell_hidden(c.x, c.y)) continue;
        eterr_cell_draw(ETERR_CRATE, c.steel ? "SCRATE" : "WCRATE", c.x, c.y, pass);
    }
    for (size_t i = 0; g_fieldsDraw && i < g_fields.size(); i++) {
        const FieldCell& f = g_fields[i];
        if (!cell_shown(f.x, f.y) || shroud_cell_hidden(f.x, f.y)) continue;
        eterr_cell_draw(ETERR_FIELD, FIELD_MESH_KEY[f.kind], f.x, f.y, pass);
    }
}

/* Whether an engine anim is drawn here as its 1996 card (ra3d_effects_mod.h): not when
   Enhanced draws it as one of OpenCNC3D's effects, nor a remaster's invisible companion. */
static bool ra3d_anim_card(const EfxAnim& a);

static bool ra3d_standing_overlay(const CNCDynamicMapEntryStruct& d)
{
    static const char* const walls[] = { "SBAG", "CYCL", "BRIK", "BARB", "WOOD", "FENC", NULL };
    if (d.IsSellable) return true;
    for (int i = 0; walls[i]; i++)
        if (!strcmp(d.AssetName, walls[i])) return true;
    return false;
}

/* The whole pass: called from draw_frame in place of the model and infantry passes. */
static void ra3d_draw_sprites(void)
{
    g_fxVehicleCards = g_raModule ? ra3d_draw_vehicle_mask : NULL;
    g_ra3dVehicleMaskCards.clear();
    g_ra3dVehicleMaskCapture = g_ra3dSprites && fx_vehicle_active(&g_fx);
    g_ra3dVehicleMaskNext = false;
    g_ra3dCards = g_ra3dDecals = g_ra3dZapCards = g_ra3dAnimCards = g_ra3dBulletsLifted = 0;
    g_ra3dOreDecals = g_ra3dMineCards = 0;
    g_ra3dAnimCardNames.clear();
    g_ra3dMineCardsHidden = 0;
    g_ra3dTerrainRecs.clear();
    g_ra3dCardRecs.clear();
    g_ra3dCardRecs.reserve(512);           /* never moved while a card writes into it */
    g_ra3dVisNow = g_ra3dVisArm;
    g_ra3dVisArm = false;
    g_ra3dVisNext = -2;
    if (g_ra3dVisNow) g_ra3dVis.clear();
    g_ra3dCardOut = NULL;
    if (!dos2d_load() || !BrainGetState || !BrainGetView) {
        g_ra3dVehicleMaskCapture = false;
        return;
    }
    static std::vector<unsigned char> bytes(sizeof(CNCObjectListStruct) + 8192 * sizeof(CNCObjectStruct));
    static std::vector<unsigned char> dynbytes(sizeof(CNCDynamicMapStruct) + 8192 * sizeof(CNCDynamicMapEntryStruct));
    int view[11]; BrainGetView(view);
    const float vx = (float)((view[4] * 24) / 256), vy = (float)((view[5] * 24) / 256);

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(1, 1, 1, 1);

    /* the ground layer: overlays, ore and smudges, then the ground animations */
    if (BrainGetState(GAME_STATE_DYNAMIC_MAP, 0, dynbytes.data(), (unsigned)dynbytes.size())) {
        const CNCDynamicMapStruct* dyn = (const CNCDynamicMapStruct*)dynbytes.data();
        const int count = dyn->Count < 8192 ? dyn->Count : 8192;
        /* Reuse OpenCNC3D's approved g3s gravel, on RA's own cell/frame layout. RA has
           no desert theater; its exterior aprons use the temperate set on grass and
           snow. Interior keeps its original art. No apron cells are invented. */
        const BibEnhTheater* bib = (g_beOn && g_beHave && g_fx.enabled && g_fx.bib_enh
                                   && ra2d_theater_letter() != 'I') ? bibenh_set_for(2) : NULL;
        if (bib) {
            std::vector<SmudgeCell> cells;
            for (int i = 0; i < count; i++) {
                const CNCDynamicMapEntryStruct& d = dyn->Entries[i];
                if (!d.IsSmudge || shroud_cell_hidden(d.CellX, d.CellY)
                    || strncmp(d.AssetName, "BIB", 3) || d.AssetName[3] < '1'
                    || d.AssetName[3] > '3' || d.AssetName[4] != 0) continue;
                SmudgeCell cell = {};
                cell.x = d.CellX; cell.y = d.CellY;
                cell.type = 12 + d.AssetName[3] - '1'; cell.data = d.ShapeIndex;
                cells.push_back(cell);
            }
            draw_bibenh(bib, cells, ra2d_theater_letter() == 'S');
            /* draw_bibenh restores TD pass defaults; resume this sprite pass. */
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
            glDepthMask(GL_FALSE);
            static bool said = false;
            if (!said && !cells.empty()) {
                fprintf(stderr, "RA3D|gravel|OpenCNC3D g3s|cells=%zu|texels=%d\n", cells.size(), g_beCell);
                said = true;
            }
        }
        /* THE CRATERS AND SCORCH MARKS, in OpenCNC3D's remastered art: every one the pack
           has a frame for leaves the 1996 decals below for draw_decalenh, on the same cell
           and frame the engine gives (its own burn round it, its body on the cell). Drawn
           here, before the ore and the overlays, which Red Alert draws over a smudge on the
           same cell (cell.cpp draws the smudge first). */
        const DecalEnhTheater* marks = ra3d_marks_set();
        g_ra3dMarks = 0;
        if (marks) {
            /* (the parse's list, the death craters of a reused building already held back) */
            std::vector<SmudgeCell> cells;
            for (size_t i = 0; i < g_ra3dMarkCells.size(); i++) {
                const SmudgeCell& c = g_ra3dMarkCells[i];
                if (decalenh_has(marks, c.type, c.data) && !shroud_cell_hidden(c.x, c.y))
                    cells.push_back(c);
            }
            if (!cells.empty()) {
                draw_decalenh(marks, cells, ra2d_theater_letter() == 'S', false);
                /* draw_decalenh restores TD pass defaults; resume this sprite pass. */
                glEnable(GL_TEXTURE_2D);
                glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
                glDepthMask(GL_FALSE);
                g_ra3dMarks = (int)cells.size();
                static bool said = false;
                if (!said) {
                    fprintf(stderr, "RA3D|marks|OpenCNC3D remastered craters and scorches|cells=%zu\n",
                            cells.size());
                    said = true;
                }
            }
        }
        for (int pass = 0; pass < 2; pass++)            /* flat first, then the walls */
            for (int i = 0; i < count; i++) {
                const CNCDynamicMapEntryStruct& d = dyn->Entries[i];
                if (shroud_cell_hidden(d.CellX, d.CellY)) continue;
                if (bib && d.IsSmudge && !strncmp(d.AssetName, "BIB", 3)
                    && d.AssetName[3] >= '1' && d.AssetName[3] <= '3' && !d.AssetName[4]) continue;
                /* (a crater or scorch the remastered set drew above, or held back as a
                   remastered building's death mark: ebld_marks_filter_ra) */
                if (marks && ra3d_mark_type(d) >= 0
                    && decalenh_has(marks, ra3d_mark_type(d), d.ShapeIndex)) continue;
                /* (a steel crate or a field drawn as its reused model, ra3d_draw_cells) */
                if (ra3d_cell_model(d)) continue;
                /* OPENCNC3D_RA3DORE=off leaves the ore out. The Red Alert engine seeds its random
                   numbers from the clock, so the same mission's ore patches take different
                   densities from run to run; a gate comparing two runs' ground uses this. */
                if (ra3d_ore_off() && (!strncmp(d.AssetName, "GOLD", 4) || !strncmp(d.AssetName, "GEM", 3)))
                    continue;
                const bool stand = ra3d_standing_overlay(d);
                if (stand != (pass == 1)) continue;
                char key[24];
                snprintf(key, sizeof key, "%c/%s", ra2d_theater_letter(), d.AssetName);
                const char* art = dos2d_shape(key) ? key : d.AssetName;
                const Dos2DShape* s = dos2d_shape(art);
                if (!s) { g_ra3dMissing++; continue; }
                /* PLACED BY ITS CELL, not by its screen position: PositionX/Y are relative
                   to the engine's tactical view, which the engine moves on its own schedule,
                   so the same ore drew a cell off from one run to the next. An overlay or a
                   flag is centred on its cell, anything else hangs from its top left. */
                const bool centred = d.IsOverlay || d.IsFlag;
                const float x0 = centred ? (float)d.CellX + 0.5f - s->w / 48.0f : (float)d.CellX;
                const float z0 = centred ? (float)d.CellY + 0.5f - s->h / 48.0f : (float)d.CellY;
                const bool ghost = (d.DrawFlags & 0x1000) != 0;
                if (stand && g_ra3dStandOff) {
                    g_ra3dVisNext = -1;
                    ra3d_card(art, d.ShapeIndex, x0 + s->w / 48.0f, z0 + s->h / 48.0f, -1, ghost);
                } else if (stand) {
                    /* a wall or a fence stands on its own foot, as an object does */
                    float c[3];
                    ra3d_stand(s, d.ShapeIndex, x0 + s->w / 48.0f, z0 + s->h / 48.0f, c);
                    g_ra3dVisNext = -1;
                    ra3d_card_at(art, d.ShapeIndex, c[0], c[1], c[2], -1, ghost);
                } else {
                    ra3d_decal(art, d.ShapeIndex, x0, z0, ghost);
                    if (!strncmp(d.AssetName, "GOLD", 4) || !strncmp(d.AssetName, "GEM", 3)) g_ra3dOreDecals++;
                }
            }
    }
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        if (!a.ground || a.stages <= 0 || a.stage < 0 || a.stage >= a.stages) continue;
        if (!ra3d_anim_card(a)) continue;
        if (shroud_cell_hidden((int)floorf(a.wx), (int)floorf(a.wz))) continue;
        char key[24];
        snprintf(key, sizeof key, "%c/%s", ra2d_theater_letter(), a.name);
        const char* art = dos2d_shape(key) ? key : a.name;
        const Dos2DShape* s = dos2d_shape(art);
        if (!s || a.start + a.stage < 0 || a.start + a.stage >= s->frames) continue;
        ra3d_decal(art, a.start + a.stage, a.wx - s->w / 48.0f, a.wz - s->h / 48.0f, true);
        g_ra3dAnimCardNames += g_ra3dAnimCardNames.empty() ? "" : ",";
        g_ra3dAnimCardNames += a.name;
    }

    /* Imported intact buildings replace all engine subobjects of that same heap entry,
       including WEAP2 and the sprite shadow. Keep fallback cards in the depth test so a
       soldier or vehicle behind a solid building does not paint through it. */
    glEnable(GL_DEPTH_TEST);
    /* the objects, in the engine's own painter order */
    if (BrainGetState(GAME_STATE_LAYERS, 0, bytes.data(), (unsigned)bytes.size())) {
        const CNCObjectListStruct* list = (const CNCObjectListStruct*)bytes.data();
        const int count = list->Count < 8192 ? list->Count : 8192;
        std::vector<int> order;
        for (int i = 0; i < count; i++) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [list](int a, int b) {
            return list->Objects[a].SortOrder < list->Objects[b].SortOrder;
        });
        const unsigned int playerMask = dos2d_player_mask();
        std::set<int> models;
        std::map<int, float> lifts;
        g_ra3dModelRoots = g_ra3dModelSubs = g_ra3dFallbackBuildings = 0;
        g_ra3dPlacedModels = g_ra3dPlacedCards = 0;
        std::map<int, size_t> modelObject;
        std::map<int, size_t> buildingObject;
        /* (a civilian building reused from OpenCNC3D's art also while it is in pieces or
           wrecked, ebld_ra_owns; a reused tree while its model, burning stage or snag
           stands, its terrain heap id apart from the buildings') */
        std::set<int> trees;
        g_ra3dTreeModels = 0;
        /* (a unit, aircraft or ship drawn by its remastered model, evhc_mod.h: its body and
           every part of its picture, the turret, the rotors and the shadow, keyed as the
           parts name their body) */
        std::set<long long> vehicles;
        g_ra3dVehicleModels = g_ra3dVehicleCards = 0;
        for (size_t bi = 0; bi < g_objects.size(); bi++) {
            const SimObject& b = g_objects[bi];
            if (b.kind == K_BUILDING && b.id >= 0) buildingObject[b.id] = bi;
            if (b.kind == K_BUILDING && ebld_ra_owns(b)) {
                models.insert(b.id);
                if (ebld_ra_ready(b)) modelObject[b.id] = bi;
            } else if (b.kind == K_TERRAIN && ebld_ra_ready(b)) {
                trees.insert(b.id);
            } else if ((b.kind == K_UNIT || b.kind == K_AIRCRAFT || b.kind == K_VESSEL)
                       && b.id >= 0 && evhc_ra_ready(b)) {
                vehicles.insert(((long long)(b.kind == K_UNIT ? UNIT
                                             : b.kind == K_VESSEL ? VESSEL : AIRCRAFT) << 32)
                                | (unsigned)b.id);
            }
        }
        /* the bolts of the coils a model draws, laid from the model's crown */
        std::map<int, Ra3dBolt> bolts;
        for (int pass = 0; pass < 2 && !g_ra3dZapOff; pass++)
            for (int i = 0; i < count; i++) {
                const CNCObjectStruct& o = list->Objects[i];
                if (o.BaseObjectType != BUILDING || strcmp(o.AssetName, "LITNING")
                    || !modelObject.count(o.BaseObjectID)) continue;
                const float px = (o.PositionX + vx) / 24.0f, pz = (o.PositionY + vy) / 24.0f;
                if (pass == 0) {
                    if (bolts.count(o.BaseObjectID)) continue;
                    const SimObject& c = g_objects[modelObject[o.BaseObjectID]];
                    Ra3dBolt b;
                    b.sx = c.ex; b.sz = c.ez - RA3D_TESLA_VOFFSET;
                    b.tx = px; b.tz = pz; b.far2 = -1.0f; b.near2 = 1e9f;
                    b.cx = c.ex; b.cz = c.ez; b.cy = terrain_y(c.ex, c.ez) + 1.0f;
                    const WorldBox w = mesh_world_box(ebld_ra_pick_mesh(c), c.ex, c.ez, draw_facing(c));
                    if (w.ok) { b.cx = (w.x0 + w.x1) * 0.5f; b.cz = (w.z0 + w.z1) * 0.5f; b.cy = w.y1; }
                    bolts[o.BaseObjectID] = b;
                } else {
                    Ra3dBolt& b = bolts[o.BaseObjectID];
                    const float d2 = (px - b.sx) * (px - b.sx) + (pz - b.sz) * (pz - b.sz);
                    if (d2 > b.far2) { b.far2 = d2; b.tx = px; b.tz = pz; }
                    if (d2 < b.near2) b.near2 = d2;
                }
            }
        g_ra3dBolts.clear();
        for (std::map<int, Ra3dBolt>::const_iterator it = bolts.begin(); it != bolts.end(); ++it)
            g_ra3dBolts.push_back(it->second);
        /* WHICH ART A RECORD DRAWS AND WHERE ITS CENTRE LIES ON THE 2D GROUND.
           Interior's crates (BOXES01..09) exist only as interior art, yet the brain
           does not flag them theater specific: a name with no generic shape takes the
           theater's, as Classic's layer path does.
           THE SPRITE'S OWN ANCHOR, as Classic draws it (dosclassic_mod.h). The engine
           hands every record the point it drew at, and only a record drawn with
           SHAPE_CENTER (0x0020) is centred on it; a tree or the ore mine
           (TerrainClass::Draw_It, SHAPE_NORMAL) hangs from its top left. Centring
           those stood every tree half its picture north west of its own cells.
           `ra3danchor 0` centres every record again, the old drawing. */
        auto place2d = [&](const CNCObjectStruct& o, std::string* art, const Dos2DShape** drawnOut,
                           float* cx, float* cz, bool* topLeftOut) {
            char key[24];
            snprintf(key, sizeof key, "%c/%s", ra2d_theater_letter(), o.AssetName);
            const bool theater = o.IsTheaterSpecific || !dos2d_shape(o.AssetName);
            *art = theater && dos2d_shape(key) ? key : o.AssetName;
            const Dos2DShape* drawn = dos2d_shape(art->c_str());
            const bool topLeft = !(o.DrawFlags & 0x0020) && !g_ra3dCentreAll && drawn;
            *cx = (o.PositionX + vx) / 24.0f;
            *cz = (o.PositionY + vy) / 24.0f;
            if (topLeft) { *cx += drawn->w / 48.0f; *cz += drawn->h / 48.0f; }
            *drawnOut = drawn;
            *topLeftOut = topLeft;
        };
        /* EVERY OBJECT'S BODY STANDS ON ITS OWN FOOT (ra3d_stand), and its parts (a
           turret, a factory's door, a flag, a shadow) lie on the body's own card at the
           offset the 2D picture gives them, so the composite stays the one picture the
           2D game draws. Keyed by the record's type and id, as the parts name their
           body. */
        struct Ra3dBody { float cx, cz, c[3]; };
        std::map<long long, Ra3dBody> bodies;
        /* A PROJECTILE FLIES AT ITS OWN HEIGHT. The 2D game draws a bullet that far up the
           screen from its ground point (BulletClass::Draw_It), so its picture stood on the
           ground north of where it really was; the brain's EFX|BULLET line gives the ground
           point and the height, and the card is centred there, as high as a 3D projectile
           flies (efx_bullet_lift's cell per 256 leptons). */
        std::map<int, const EfxBullet*> flying;
        for (size_t bi = 0; bi < g_efxBullets.size(); bi++) flying[g_efxBullets[bi].id] = &g_efxBullets[bi];
        for (int i = 0; i < count; i++) {
            const CNCObjectStruct& o = list->Objects[i];
            if (o.Type == ANIM || o.SubObject) continue;
            if (o.Type == BUILDING && models.count(o.ID)) continue;
            if (g_ra3dStandOff) {
                if (o.Type != BUILDING) continue;
                char key[24];
                snprintf(key, sizeof key, "%c/%s", ra2d_theater_letter(), o.AssetName);
                const Dos2DShape* shape = dos2d_shape(o.IsTheaterSpecific ? key : o.AssetName);
                if (!shape) shape = dos2d_shape(key);
                lifts[o.ID] = ra3d_building_lift(shape, o.ShapeIndex);
                continue;
            }
            std::string art; const Dos2DShape* drawn; bool topLeft;
            Ra3dBody b;
            place2d(o, &art, &drawn, &b.cx, &b.cz, &topLeft);
            ra3d_stand(drawn, o.ShapeIndex, b.cx, b.cz, b.c);
            const std::map<int, const EfxBullet*>::const_iterator fl = flying.find(o.ID);
            if (o.Type == BULLET && fl != flying.end()) {
                const EfxBullet& eb = *fl->second;
                b.c[0] = eb.wx;
                b.c[1] = terrain_y(eb.wx, eb.wz) + efx_bullet_lift(eb) + 0.05f;
                b.c[2] = eb.wz;
                g_ra3dBulletsLifted++;
                /* a V2's rocket leaves from the nose on its raised rail (evhc_mod.h) */
                evhc_v2_bullet_from(eb.id, b.c);
            }
            bodies[((long long)o.Type << 32) | (unsigned)o.ID] = b;
        }
        g_ra3dBodyCards.clear();
        for (std::map<long long, Ra3dBody>::const_iterator bi = bodies.begin(); bi != bodies.end(); ++bi) {
            Ra3dBodyCard k;
            k.cx = bi->second.cx; k.cz = bi->second.cz;
            for (int m = 0; m < 3; m++) k.c[m] = bi->second.c[m];
            g_ra3dBodyCards[bi->first] = k;
        }
        for (int m = 0; m < 3; m++) { g_ra3dBodyR[m] = g_bbRight[m]; g_ra3dBodyU[m] = g_bbUp[m]; }
        for (size_t j = 0; j < order.size(); j++) {
            const CNCObjectStruct& o = list->Objects[order[j]];
            if (o.Type == ANIM) continue;
            const bool vehicleCard = ra3d_vehicle_type(o.Type)
                || (o.SubObject && ra3d_vehicle_type(o.BaseObjectType));
            if (vehicleCard && !g_vShowUnits) continue;
            /* A UNIT OR AIRCRAFT ITS REMASTERED MODEL DRAWS stands down whole, its parts with
               it (the model turns its own turret and rotors, and the sun casts its shadow) */
            if (!vehicles.empty()) {
                const long long vk = o.SubObject
                    ? (((long long)o.BaseObjectType << 32) | (unsigned)o.BaseObjectID)
                    : (((long long)o.Type << 32) | (unsigned)o.ID);
                if (vehicles.count(vk)) {
                    if (o.SubObject) g_ra3dVehicleCards++; else g_ra3dVehicleModels++;
                    continue;
                }
            }
            /* A V2'S ROCKET IN FLIGHT that its model's rocket draws (evhc_mod.h) stands down
               whole; any V2 projectile still drawn as its card under Enhanced is counted */
            if (o.Type == BULLET || (o.SubObject && o.BaseObjectType == BULLET)) {
                const int bid = o.SubObject ? o.BaseObjectID : o.ID;
                if (evhc_v2_flight_has(bid)) {
                    if (!o.SubObject) g_evhcV2StandDown++;
                    continue;
                }
                const std::map<int, const EfxBullet*>::const_iterator fb = flying.find(bid);
                if (!o.SubObject && evhc_rule_now() && fb != flying.end()
                    && !strcmp(fb->second->img, "V2"))
                    g_evhcV2Cards++;
            }
            const bool buildingRoot = o.Type == BUILDING && !o.SubObject;
            const bool buildingPart = o.BaseObjectType == BUILDING;
            const int buildingID = buildingRoot ? o.ID : o.BaseObjectID;
            /* the bolt is the coil's shot, not a part of its picture: it draws whatever
               draws the coil */
            const bool zap = buildingPart && !strcmp(o.AssetName, "LITNING") && !g_ra3dZapOff;
            if (buildingRoot && buildingObject.count(buildingID)) {
                const SimObject& so = g_objects[buildingObject[buildingID]];
                if (building_constructing(so) && eplc_model_of(so.type) >= 0) {
                    if (models.count(buildingID)) g_ra3dPlacedModels++;
                    else g_ra3dPlacedCards++;
                }
            }
            if ((buildingRoot || buildingPart) && models.count(buildingID) && !zap) {
                if (buildingRoot) g_ra3dModelRoots++; else g_ra3dModelSubs++;
                continue;
            }
            /* a soldier with a remastered model is drawn by the infantry pass, not here */
            if (o.Type == INFANTRY && m3inf_find(o.AssetName) >= 0) continue;
            /* A MINE ANOTHER SIDE LAID is not shown to the player, as the 1996 engine never
               drew it (the layer export clears the player's bit; ra3d_props_mod.h) */
            if (buildingRoot && ra3d_mine_type(o.AssetName) && !(o.VisibleFlags & playerMask)) {
                g_ra3dMineCardsHidden++;
                continue;
            }
            /* (and the parts the engine files under it, its shadow among them) */
            if (buildingPart && !buildingRoot && g_ra3dHiddenMines.count(buildingID)) continue;
            if (shroud_cell_hidden(o.CellX, o.CellY) && o.Type != BUILDING && o.Type != TERRAIN)
                continue;
            const bool flash = o.IsBlushing && (o.FlashingFlags & playerMask);
            const bool rotor = !strcmp(o.AssetName, "RROTOR") || !strcmp(o.AssetName, "LROTOR");
            const bool shadow = o.SubObject && (o.DrawFlags & 0x0300) == 0x0300;
            const int remap = flash || rotor || shadow ? -1
                            : (o.Type == BUILDING && o.Owner == 1 ? 2 : o.RemapColor);
            std::string art; const Dos2DShape* drawn; bool topLeft;
            float cx, cz;
            place2d(o, &art, &drawn, &cx, &cz, &topLeft);
            const bool treeModel = o.Type == TERRAIN && trees.count(o.ID) != 0;
            /* WHERE THE CARD'S CENTRE GOES IN THE WORLD */
            float c[3];
            if (zap && bolts.count(buildingID)) {
                /* the bolt is laid in 3D from the model's crown (ra3d_bolt_place), from the
                   point the engine drew the piece at */
                float bx = (o.PositionX + vx) / 24.0f, bz = (o.PositionY + vy) / 24.0f;
                float lift = lifts[buildingID];
                ra3d_bolt_place(bolts[buildingID], bx, bz, &bx, &bz, &lift);
                if (topLeft) { bx += drawn->w / 48.0f; bz += drawn->h / 48.0f; }
                c[0] = bx; c[1] = terrain_y(bx, bz) + lift; c[2] = bz;
            } else if (g_ra3dStandOff) {
                const float lift = (buildingRoot || buildingPart) ? lifts[buildingID] : 0.0f;
                c[0] = cx; c[1] = terrain_y(cx, cz) + lift; c[2] = cz;
            } else {
                const long long bodyKey = o.SubObject
                    ? (((long long)o.BaseObjectType << 32) | (unsigned)o.BaseObjectID)
                    : (((long long)o.Type << 32) | (unsigned)o.ID);
                const std::map<long long, Ra3dBody>::const_iterator b = bodies.find(bodyKey);
                if (b != bodies.end()) {
                    /* on the body's card: across it and down it by the 2D offset */
                    const float dx = cx - b->second.cx, dz = cz - b->second.cz;
                    for (int k = 0; k < 3; k++)
                        c[k] = b->second.c[k] + g_bbRight[k] * dx - g_bbUp[k] * dz;
                } else {
                    ra3d_stand(drawn, o.ShapeIndex, cx, cz, c);
                }
            }
            if (o.Type == TERRAIN && drawn && g_ra3dTerrainRecs.size() < 512) {
                Ra3dTerrainRec r;
                snprintf(r.type, sizeof r.type, "%s", o.TypeName);
                r.cellX = o.CellX; r.cellY = o.CellY; r.flags = o.DrawFlags;
                r.w = drawn->w; r.h = drawn->h; r.cx = cx; r.cz = cz; r.topLeft = topLeft;
                r.model = treeModel; r.mx = r.mz = -1.0f;
                for (size_t k = 0; treeModel && k < g_objects.size(); k++)
                    if (g_objects[k].kind == K_TERRAIN && g_objects[k].id == o.ID) {
                        r.mx = g_objects[k].wx; r.mz = g_objects[k].wz;
                        break;
                    }
                g_ra3dTerrainRecs.push_back(r);
            }
            /* A REUSED TREE IS ITS MODEL (ebld_terr.h): drawn whole by the object passes at
               the engine's Center_Coord, the CenterBase both games' tree types share, so it
               stands where Tiberian Dawn's stands on the same cells; the card stands down */
            if (treeModel) { g_ra3dTreeModels++; continue; }
            if (o.Type == TERRAIN && !o.SubObject && !strcmp(o.TypeName, "MINE")) g_ra3dMineCards++;
            if (buildingRoot) g_ra3dFallbackBuildings++;
            if (buildingRoot && g_ra3dCardRecs.size() < 512) {
                Ra3dCardRec r = {};
                r.id = o.ID;
                r.foot = ra3d_foot_row(drawn, o.ShapeIndex);
                r.h = drawn ? drawn->h : 0;
                r.cx = cx; r.cz = cz;
                snprintf(r.art, sizeof r.art, "%s", art.c_str());
                g_ra3dCardRecs.push_back(r);
                g_ra3dCardOut = &g_ra3dCardRecs.back();
            }
            const int before = g_ra3dCards;
            if (!o.SubObject && !zap) g_ra3dVisNext = (int)o.Type;
            g_ra3dVehicleMaskNext = vehicleCard && !shadow;
            GLint gradeWas=0;
            if(zap && fx_gl_ready)glGetIntegerv(GL_CURRENT_PROGRAM,&gradeWas);
            const bool gradedZap=zap && efx_grade_begin(true);
            ra3d_card_at(art.c_str(), o.ShapeIndex, c[0], c[1], c[2], remap,
                         (o.DrawFlags & 0x1000) != 0, flash, shadow);
            if(gradedZap)fx_glUseProgram((GLuint)gradeWas);
            g_ra3dVehicleMaskNext = false;
            g_ra3dCardOut = NULL;
            if (!strcmp(o.AssetName, "LITNING")) g_ra3dZapCards += g_ra3dCards - before;
        }
    }

    /* the air: explosions, muzzle flashes and the move marker, whichever stay cards. Each
       stands on its own foot as an object's card does (ra3d_stand): centred on its point,
       the lower half of an explosion lay under the ground. */
    for (size_t i = 0; i < g_efxAnims.size(); i++) {
        const EfxAnim& a = g_efxAnims[i];
        if (a.ground || a.stages <= 0 || a.stage < 0 || a.stage >= a.stages) continue;
        if (!ra3d_anim_card(a)) continue;
        if (shroud_cell_hidden((int)floorf(a.wx), (int)floorf(a.wz))) continue;
        char key[24];
        snprintf(key, sizeof key, "%c/%s", ra2d_theater_letter(), a.name);
        const char* art = dos2d_shape(key) ? key : a.name;
        const Dos2DShape* s = dos2d_shape(art);
        if (!s || a.start + a.stage < 0 || a.start + a.stage >= s->frames) continue;
        const bool translucent = !strcmp(a.name, "MOVEFLSH") || !strcmp(a.name, "GUNFIRE")
            || !strcmp(a.name, "CORPSE1") || !strcmp(a.name, "CORPSE2") || !strcmp(a.name, "CORPSE3");
        if (g_ra3dStandOff) {
            ra3d_card(art, a.start + a.stage, a.wx, a.wz, -1, translucent);
        } else {
            float c[3];
            ra3d_stand(s, a.start + a.stage, a.wx, a.wz, c);
            ra3d_card_at(art, a.start + a.stage, c[0], c[1], c[2], -1, translucent);
        }
        g_ra3dAnimCards++;
        g_ra3dAnimCardNames += g_ra3dAnimCardNames.empty() ? "" : ",";
        g_ra3dAnimCardNames += a.name;
    }
    g_ra3dVehicleMaskCapture = false;
    glPopAttrib();
    glAlphaFunc(GL_GREATER, 0.5f);
    if (g_ra3dVisNow) {
        for (size_t i = 0; i < g_ra3dVis.size(); i++) {
            Ra3dCardVis& v = g_ra3dVis[i];
            ra3d_q_get(v.q[0], GL_QUERY_RESULT, &v.full);
            ra3d_q_get(v.q[1], GL_QUERY_RESULT, &v.seen);
            ra3d_q_del(2, v.q);
            v.q[0] = v.q[1] = 0;
        }
        g_ra3dVisNow = false;
    }
    static char reported[16] = "";
    if (g_ra3dCards > 0 && strcmp(reported, g_bootScen)) {
        snprintf(reported, sizeof reported, "%s", g_bootScen);
        fprintf(stderr, "RA3D|sprites3d|%s|cards=%d|decals=%d|missing=%d|theater=%s"
                "|modelroots=%d|modelsubs=%d|fallbackbuildings=%d\n",
                g_bootScen, g_ra3dCards, g_ra3dDecals, g_ra3dMissing, g_pack.theater,
                g_ra3dModelRoots, g_ra3dModelSubs, g_ra3dFallbackBuildings);
    }
}

/* Called by the post chain with the scene's depth attached read-only to its
   separate mask target. No scene colour, depth, or normal data is written. */
static void ra3d_draw_vehicle_mask(void)
{
    if (!g_ra3dSprites || !fx_vehicle_active(&g_fx) || g_ra3dVehicleMaskCards.empty()) return;
    const GLuint prog = str_class_prog();
    if (!prog) return;
    GLint was = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &was);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
                 | GL_TEXTURE_BIT | GL_CURRENT_BIT);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    fx_glUseProgram(prog);
    fx_set1i(prog, "uTex", 0);
    fx_set1i(prog, "uCut", 1);
    for (size_t i = 0; i < g_ra3dVehicleMaskCards.size(); i++) {
        const Ra3dVehicleMaskCard& card = g_ra3dVehicleMaskCards[i];
        glBindTexture(GL_TEXTURE_2D, card.tex);
        fx_set1f(prog, "uClass", card.vehicle ? 1.0f : 0.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(card.uv[0], card.uv[1]); glVertex3fv(card.v[0]);
        glTexCoord2f(card.uv[2], card.uv[1]); glVertex3fv(card.v[1]);
        glTexCoord2f(card.uv[2], card.uv[3]); glVertex3fv(card.v[2]);
        glTexCoord2f(card.uv[0], card.uv[3]); glVertex3fv(card.v[3]);
        glEnd();
    }
    fx_glUseProgram((GLuint)was);
    glPopAttrib();
}
