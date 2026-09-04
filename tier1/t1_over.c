#include <string.h>
#include <stdio.h>

#include "t1_over.h"

/* ---- walls --------------------------------------------------------------------------
 *
 * THE CARTRIDGE'S OWN 16-WAY TABLE, taken from game/cnc_eyes.cpp where it is transcribed
 * from the console's overlay draw dispatcher: a 16-record table at RAM 0x802100D0 of
 * { variant, facing, shadow11 }, indexed by the engine's 4-bit connectivity nibble
 * (bit0 north, bit1 east, bit2 south, bit3 west). Copied rather than re-derived, because
 * deriving it by eye is exactly how the desktop build got every non-zero facing 180
 * degrees out: a straight wall piece looks identical at 64 and 192 and only the L and T
 * pieces show the difference.
 *
 * The four authored pieces are straight, L (joining east and south), T (missing north)
 * and cross, and the facing spins that arm set onto the arms this cell actually has.
 * ------------------------------------------------------------------------------------ */
typedef struct { unsigned char variant, face; } T1_WallVariant;
static const T1_WallVariant WALL_VARIANT[16] = {
    /* 0  ----    */ { 0,   0 },   /* 1  N       */ { 0, 192 },
    /* 2  E       */ { 0,   0 },   /* 3  N|E     */ { 1,  64 },
    /* 4  S       */ { 0, 192 },   /* 5  N|S     */ { 0, 192 },
    /* 6  E|S     */ { 1,   0 },   /* 7  N|E|S   */ { 2,  64 },
    /* 8  W       */ { 0,   0 },   /* 9  N|W     */ { 1, 128 },
    /* 10 E|W     */ { 0,   0 },   /* 11 N|E|W   */ { 2, 128 },
    /* 12 S|W     */ { 1, 192 },   /* 13 N|S|W   */ { 2, 192 },
    /* 14 E|S|W   */ { 2,   0 },   /* 15 all     */ { 3,   0 },
};

/* ---- THE WALL SHADOW SETS -----------------------------------------------------------
 *
 * Two of the five wall types carry their own shadow geometry as ELEVEN separate
 * UNROTATED pieces -- CYCLS0..10 and WOODS0..10 -- which is exactly the number of
 * distinct orientations a wall cell can have: two straights, four Ls, four Ts and a
 * cross. The body pieces are FOUR meshes spun by facing; the shadows are eleven meshes
 * spun by nothing, because a shadow is the fence sheared toward the north-east by its own
 * height and a sheared shape does not survive being rotated.
 *
 * So this table cannot be derived from WALL_VARIANT above and is its own transcription,
 * the cartridge's own third column, recovered by undoing the shear on every bounding box
 * -- and checked by the CYCL and WOOD sets agreeing piece for piece independently.
 *
 * The other three types' shadows are MODE 3 with a per-vertex alpha the mesh record
 * cannot carry, and the desktop measured them as drawing zero visible pixels because they
 * sit inside their own walls' footprints. Registered in known-gap notes rather than
 * approximated. */
static const unsigned char WALL_SHADOW11[16] = {
    /*  0 ----   */ 0,  /*  1 N      */ 1,  /*  2 E      */ 0,  /*  3 N|E    */ 3,
    /*  4 S      */ 1,  /*  5 N|S    */ 1,  /*  6 E|S    */ 4,  /*  7 N|E|S  */ 9,
    /*  8 W      */ 0,  /*  9 N|W    */ 2,  /* 10 E|W    */ 0,  /* 11 N|E|W  */ 8,
    /* 12 S|W    */ 5,  /* 13 N|S|W  */ 7,  /* 14 E|S|W  */ 6,  /* 15 all    */ 10
};

static const char *const WALL_NAME[5] = { "SBAG", "CYCL", "BRIK", "BARB", "WOOD" };
static int  g_wallmesh[5][4];      /* body */
static int  g_walldmg[5][4];       /* damaged set, BRIK only on the cartridge */
static int  g_wallsha[5][11];      /* the eleven-piece shadow sets; CYCL and WOOD only */
static int  g_shasets;
static int  g_pieces;
static int  g_ready;

/* The model rotation. cnc_eyes.cpp hands draw_mesh `(256 - face) & 255`, and its
 * facing_rot composes a negative angle with a negated sine row, which works out to
 * EXACTLY the rotation t1_mesh_draw applies for a positive facing. So the conversion
 * transfers unchanged; it was settled by writing both formulas out rather than by
 * looking at a screenshot, because a straight wall gives the same picture either way. */
static int wall_face(unsigned char face) { return (256 - (int)face) & 255; }

void t1_over_init(T1_MeshBank *b)
{
    static const char *const SUF[4] = { "", "_L", "_T", "_X" };
    char key[24];
    int t, v;
    if (g_ready) return;
    g_pieces = 0;
    for (t = 0; t < 5; ++t)
        for (v = 0; v < 4; ++v)
        {
            _snprintf(key, sizeof key, "%s%s", WALL_NAME[t], SUF[v]);
            g_wallmesh[t][v] = t1_mesh_for_type(b, key);
            if (g_wallmesh[t][v] >= 0) ++g_pieces;
            _snprintf(key, sizeof key, "%sD%s", WALL_NAME[t], SUF[v]);
            g_walldmg[t][v] = t1_mesh_for_type(b, key);
        }
    /* The eleven-piece shadow sets, ALL OR NOTHING per type: a partial set would leave
     * some orientations casting a shadow and others not, which reads as a depth bug. */
    for (t = 0; t < 5; ++t)
    {
        int have = 0, k;
        for (k = 0; k < 11; ++k)
        {
            _snprintf(key, sizeof key, "%sS%d", WALL_NAME[t], k);
            g_wallsha[t][k] = t1_mesh_for_type(b, key);
            if (g_wallsha[t][k] >= 0) ++have;
        }
        if (have != 11) { for (k = 0; k < 11; ++k) g_wallsha[t][k] = -1; }
        else ++g_shasets;
    }
    g_ready = 1;
}

int t1_over_wall_pieces(void) { return g_pieces; }

static int g_stat[5];      /* seen, culled, no mesh, submitted, shadow pieces */
void t1_over_stats(int *out5) { int k; for (k = 0; k < 5; ++k) out5[k] = g_stat[k]; }
int  t1_over_shadow_sets(void) { return g_shasets; }

long t1_over_draw_walls(T1_MeshBank *b, const T1_Cam *cam, const T1_Screen *scr,
                        const T1_Terrain *terr, const W98_Overlays *ov,
                        int (*shown)(int cx, int cz))
{
    long drawn = 0;
    int i;
    memset(g_stat, 0, sizeof g_stat);
    for (i = 0; i < ov->nwall; ++i)
    {
        const W98_Wall *w = &ov->wall[i];
        const T1_WallVariant *v;
        int mesh;
        float cx, cz, cy;
        ++g_stat[0];
        if (w->kind > 4) continue;
        if (w->cx < 0 || w->cx > 63 || w->cy < 0 || w->cy > 63) continue;
        if (shown && !shown(w->cx, w->cy)) { ++g_stat[1]; continue; }

        v = &WALL_VARIANT[w->icon & 15];
        mesh = (w->dmg > 0 && g_walldmg[w->kind][v->variant] >= 0)
             ? g_walldmg[w->kind][v->variant]
             : g_wallmesh[w->kind][v->variant];
        if (mesh < 0) { ++g_stat[2]; continue; }
        ++g_stat[3];

        /* Authored CENTRED on the cell: the console's own wall builder computes
         * x = cellX + 128, z = cellY + 128 in leptons and applies no per-variant offset,
         * so the L and T being off-centre in the art is deliberate and is not something
         * the placement is meant to correct. */
        cx = (float)w->cx + 0.5f;
        cz = (float)w->cy + 0.5f;
        cy = t1_terrain_corner_y(terr, w->cx, w->cy);
        drawn += t1_mesh_draw(b, 0, cam, scr, mesh, cx, cy, cz, wall_face(v->face), 0);
    }
    return drawn;
}

/* The wall shadows, drawn INSIDE the caller's shadow state: same blend, same depth rule,
 * same alpha plane bank as every other shadow. Unrotated -- the shear is baked into each
 * piece -- and lifted the same 0.012 cell everything else on the ground is, because
 * WOOD's own pieces sit slightly BELOW the ground plane and would sink without it. */
long t1_over_draw_wall_shadows(T1_MeshBank *b, const T1_Cam *cam, const T1_Screen *scr,
                               const T1_Terrain *terr, const W98_Overlays *ov,
                               int (*shown)(int cx, int cz))
{
    long drawn = 0;
    int i;
    if (!g_shasets) return 0;
    for (i = 0; i < ov->nwall; ++i)
    {
        const W98_Wall *w = &ov->wall[i];
        T1_MeshParams mp;
        int mesh;
        if (w->kind > 4) continue;
        if (w->cx < 0 || w->cx > 63 || w->cy < 0 || w->cy > 63) continue;
        if (shown && !shown(w->cx, w->cy)) continue;
        mesh = g_wallsha[w->kind][WALL_SHADOW11[w->icon & 15]];
        if (mesh < 0) continue;
        memset(&mp, 0, sizeof mp);
        mp.mesh = mesh;
        mp.wx = (float)w->cx + 0.5f;
        mp.wz = (float)w->cy + 0.5f;
        mp.wy = t1_terrain_corner_y(terr, w->cx, w->cy) + 0.012f;
        mp.facing = 0;                  /* the shear is in the piece; do not rotate it */
        mp.animT = -1.0f;
        mp.build_frac = 1.0f;
        mp.modemask = T1_MASK_SOLID | T1_MASK_SHADOW;
        drawn += t1_mesh_draw_p(b, 0, cam, scr, &mp);
        ++g_stat[4];
    }
    return drawn;
}

/* ---- bullets ------------------------------------------------------------------------
 *
 * The cartridge's bullet table draws 120MM, DRAGON, MISSILE and BOMBLET as real models;
 * 50CAL and FLAME carry no model at all and arrive with the dump's `invis` flag set. The
 * engine's IniNames are case-inconsistent and two bullets fly under a different image
 * name, exactly as the DOS data does: the grenade flies as BOMB and both halves of the
 * nuke as ATOMICUP.
 * ------------------------------------------------------------------------------------ */
static int bullet_mesh(T1_MeshBank *b, const char *name)
{
    char up[16];
    int n = 0;
    const char *key;
    for (; name[n] && n < 15; ++n)
        up[n] = (name[n] >= 'a' && name[n] <= 'z') ? (char)(name[n] - 32) : name[n];
    up[n] = 0;
    key = up;
    if (!strcmp(up, "GRENADE")) key = "BOMB";
    else if (!strcmp(up, "ATOMIC") || !strcmp(up, "ATOMICDN")) key = "ATOMICUP";
    return t1_mesh_for_type(b, key);
}

long t1_over_draw_bullets(T1_MeshBank *b, const T1_Cam *cam, const T1_Screen *scr,
                          const T1_Terrain *terr, const W98_Overlays *ov,
                          int (*shown)(int cx, int cz))
{
    long drawn = 0;
    int i;
    for (i = 0; i < ov->nbullet; ++i)
    {
        const W98_Bullet *bu = &ov->bullet[i];
        int mesh, cx, cz;
        float wx, wz, wy;
        if (bu->invis) continue;
        cx = bu->lx / 256;
        cz = bu->ly / 256;
        if (cx < 0 || cx > 63 || cz < 0 || cz > 63) continue;
        if (shown && !shown(cx, cz)) continue;
        mesh = bullet_mesh(b, bu->name);
        if (mesh < 0) continue;
        wx = bu->lx / 256.0f;
        wz = bu->ly / 256.0f;
        /* Altitude is leptons, the same 256-to-a-cell unit the position is in. */
        wy = t1_terrain_corner_y(terr, cx, cz) + bu->alt / 256.0f;
        /* +128: the authored-south convention the unit hulls use, so the DRAGON's dark
         * nosecone leads toward the target rather than trailing it. */
        drawn += t1_mesh_draw(b, 0, cam, scr, mesh, wx, wy, wz, (bu->face + 128) & 255, 0);
    }
    return drawn;
}
