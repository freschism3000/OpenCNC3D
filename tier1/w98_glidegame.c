/*
 * w98_glidegame.c -- the mission, on the 3dfx Voodoo 2.
 *
 * Identical scene to tier1/w98_game.c: the same brain, the same camera, the same terrain
 * and the same N64 models. The ONLY difference is that t1_glide_open() installs itself as
 * t1_tri_hook, so every triangle t1_terrain.c and t1_mesh.c submits goes to the card
 * instead of to the CPU. Neither of those files knows, and neither of them changed.
 *
 * There is no window and no sidebar here. A Voodoo 2 is a 3D-only card: it takes the
 * screen and its output never touches the 2D adapter, so the DOS sidebar (which is a
 * palettised 2D blit) has no home yet and is left out rather than faked. That makes this
 * a measurement of the 3D half against the software renderer's 3D half, which is the
 * comparison worth having. The sidebar becomes two textured quads later.
 *
 *   w98glide [frames]
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "softras.h"
#include "t1_cam.h"
#include "t1_terrain.h"
#include "t1_mesh.h"
#include "t1_anim.h"
#include "t1_place.h"
#include "t1_cursor.h"
#include "t1_menu.h"
#include "t1_movie.h"
#include "t1_glide.h"
#include "w98_brain.h"
#include "dosbar.h"
#include "t1_dosinf.h"
#include "t1_hud.h"
#include "t1_shroud.h"
#include "t1_over.h"
#include "t1_tib.h"
#include "t1_efx.h"
#include "t1_script.h"
#include "cncaudio.h"
#include "mixer.h"
#include "audioboot.h"

#define SCR_W 640
#define SCR_H 480

static T1_Terrain  g_terr;
static T1_Tib      g_tib;
static int         g_tibok;
static T1_Tib      g_smd;
static int         g_smdok;
static T1_Efx      g_efx;
static int         g_efxok;

/* ---- THE VERDICT ---------------------------------------------------------------------
 * The console does not print text at the end of a mission. It blacks the WHOLE screen,
 * sidebar included, and puts a pre-rendered gold bevelled banner on it: MACCOMP when the
 * win flag is set, MFAILED when it is clear, at x = (320 - 200)/2 and y = 99 on the
 * console's 320x240 screen, which is exactly 2x here. 1995 DOS printed two strings over
 * the live battlefield instead; this follows the CONSOLE, because that is what this
 * project is.
 * ------------------------------------------------------------------------------------ */
static unsigned short g_vrdpx[2][256 * 64];
static SR_Texture     g_vrd[2];
static int            g_vrdok;
static int            g_vrduw = 200, g_vrduh = 53;

static int verdict_load(const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    char magic[8];
    unsigned int ver = 0, k;
    if (!f) { _snprintf(err, errlen, "no banner at %s", path); return 0; }
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "T1VRD001", 8) != 0 ||
        fread(&ver, 4, 1, f) != 1 || ver != 1)
    { _snprintf(err, errlen, "%s is not a T1VRD001 file", path); fclose(f); return 0; }
    for (k = 0; k < 2; ++k)
    {
        unsigned int w = 0, h = 0, uw = 0, uh = 0;
        if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 ||
            fread(&uw, 4, 1, f) != 1 || fread(&uh, 4, 1, f) != 1 ||
            w != 256 || h != 64)
        { _snprintf(err, errlen, "banner %u is %ux%u, not 256x64", k, w, h);
          fclose(f); return 0; }
        if (fread(g_vrdpx[k], 2, (size_t)w * h, f) != (size_t)w * h)
        { _snprintf(err, errlen, "banner %u truncated", k); fclose(f); return 0; }
        g_vrduw = (int)uw; g_vrduh = (int)uh;
        sr_texture16(&g_vrd[k], g_vrdpx[k], (int)w, (int)h);
    }
    fclose(f);
    return 1;
}
static T1_MeshBank g_mesh;
static T1_Cam      g_cam;
static T1_Screen   g_scr;
static W98_Object  g_objs[WB_MAX_OBJECTS];

/* ---- THE FACING LAW ---------------------------------------------------------------
 *
 * WHICH WAY A MODEL IS AUTHORED FACING, in DirType units (256 = a full turn), ported
 * from game/cnc_eyes.cpp's g_meshForward table rather than re-derived: that table was
 * MEASURED (PCA of the pack vertices for the hull's long axis, plus a --posetest
 * screenshot to say which end is the nose), and re-measuring it here would only be a
 * second chance to get it wrong.
 *
 * Everything is authored pointing SOUTH except the gunboat, whose hull runs east-west
 * with the bow at -x, and the Nod gun turret's barrel, which points north.
 *
 * THIS IS THE BUG the project owner SAW TWICE. The Tier 1 loop was handing the mesh the engine's raw
 * facing with no bias at all: 128 out on every vehicle, which is a tank driving exactly
 * backwards ("MCV is driving in wrong directions"), and 64 out on the gunboat, which is
 * a boat sailing exactly sideways. One missing term, two symptoms. */
static int model_forward(const char *type)
{
    if (strcmp(type, "BOAT") == 0) return 192;   /* bow west */
    if (strcmp(type, "GUN")  == 0) return 0;     /* barrel north; turret delta only */
    return 128;                                  /* nose south */
}

/* The yaw the mesh is drawn at. Buildings, terrain and everything else are drawn exactly
 * as authored, which is what the console's own Spawn_Model does: it takes no rotation.
 *
 * The one subtlety is a STANDING turreted unit whose mesh has no separate turret part.
 * It has nothing to aim but itself, so the whole model turns to the turret facing and
 * the art points where the gun points. A mesh that DOES carry a turret part keeps its
 * hull on the movement heading and lets the part do the aiming. */
static int draw_facing(const W98_Object *o, int mi)
{
    int face;
    if (strcmp(o->kind, "UNIT") != 0 || o->face < 0) return 0;
    face = o->face;
    if (o->tface >= 0 && o->tface != o->face && o->id >= 0 && t1_anim_still(o)
        && !t1_mesh_has_role(&g_mesh, mi, T1_ROLE_TURRET))
        face = o->tface;
    return (face - model_forward(o->name)) & 255;
}

/* The extra yaw a TURRET part gets. A unit's turret tracks its target while the hull
 * keeps the heading, so the offset is the difference. A building is drawn unrotated, so
 * its turret's rest pose is the mesh's authored forward and the offset from there to the
 * engine facing aims it -- which is why GUN's entry above is 0 and not 128. */
static int turret_delta(const W98_Object *o, int mi)
{
    if (mi < 0 || !t1_mesh_has_role(&g_mesh, mi, T1_ROLE_TURRET)) return 0;
    if (strcmp(o->kind, "UNIT") == 0)
        return (o->face < 0 || o->tface < 0) ? 0 : ((o->tface - o->face) & 255);
    if (strcmp(o->kind, "BUILDING") == 0 && o->face >= 0)
        return (o->face - model_forward(o->name)) & 255;
    return 0;
}
static int         g_objmesh[WB_MAX_OBJECTS];
static unsigned short g_read[SCR_W * SCR_H];
static char        g_base[MAX_PATH];

/* The DOS sidebar, as geometry.
 *
 * On this card there is no 2D at all, so the 1995 sidebar is drawn by game/dosbar.c into
 * its own 8-bit 320x200 surface exactly as always, and then the 80 wide column it
 * occupies is copied into a 256x256 texture and put on screen as one quad. It is the
 * same code, the same art and the same palette; only the last step differs. */
#define SB_TEX 256
static unsigned char g_surf8[DB_SCREEN_W * DB_SCREEN_H];
static unsigned char g_sbtex[SB_TEX * SB_TEX];
static SR_Texture    g_sbsr;

/* The mouse pointer.
 *
 * There is no window and therefore no system cursor: a fullscreen Glide app owns the
 * screen outright and Windows draws nothing on it, which is why the project owner could not test with
 * a mouse at all. So the pointer is art, and it lives in the SPARE CORNER OF THE SIDEBAR
 * PAGE rather than in a texture of its own. The sidebar column is 80 wide in a 256 wide
 * page, so there is a great deal of unused room, and sharing the page means the cursor
 * cannot be lost to a texture that quietly failed to upload or bound the wrong palette:
 * if the sidebar is on screen, so is the cursor. A first attempt with its own texture
 * uploaded without error and drew nothing, which is exactly the failure this avoids. */
#define CUR_X 160          /* where the arrow sits inside the sidebar page */
#define CUR_Y 0
#define CUR_SZ 16

#define DOT_X 200          /* a 4x4 block of solid white, for lines and brackets */
#define DOT_Y 0

static void build_cursor(unsigned char *page, int pitch)
{
    /* The classic arrow wedge: white body, black edge, so it reads on grass and on water. */
    int y, x;
    for (y = 0; y < 14; ++y)
        for (x = 0; x <= y && x < 9; ++x)
            page[(CUR_Y + y) * pitch + (CUR_X + x)] =
                (x == y || x == 0 || y == 13) ? DB_BLACK : DB_WHITE;
    /* Solid white, so a screen-space line is just a very thin quad sampling it. There is
     * no untextured path on this backend and there does not need to be. */
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x)
            page[(DOT_Y + y) * pitch + (DOT_X + x)] = DB_WHITE;
}

/* One screen-space line as a thin quad of the solid block. */
static void hud_rect(float x0, float y0, float x1, float y1, const SR_Texture *tex)
{
    t1_glide_quad(x0, y0, x1, y1,
                  (float)(DOT_X + 1), (float)(DOT_Y + 1),
                  (float)(DOT_X + 3), (float)(DOT_Y + 3), tex, 1.0f);
}
static DB_Pack      *g_pack;
static DB_State      g_state;
static W98_Sidebar   g_sb;
static int           g_credits;
static DB_Surface    g_surf;

/* The DOS palette with index 0 forced to magenta.
 *
 * The pack's own index 0 is black, and so is DB_BLACK (12), the engine's OPAQUE black.
 * Chroma-keying on black therefore cuts out both, and the cursor lost its outline while
 * looking almost right. Index 0 is the only one that should ever be a hole, so it is
 * given a colour nothing else uses, exactly as the mesh converter does. */
static unsigned char g_dospal[768];

/* The 1995 DOS infantry. Its own palette copy, with the two indices that must not draw
 * forced to the key colour: 0 is transparent, and 4 is the DOS engine's shadow ghost,
 * which it used to darken the ground under a man rather than to paint him. */
static T1_Inf        g_inf;
static int           g_infok;
static unsigned char g_infpal[768];
static int           g_inftype[WB_MAX_OBJECTS];

/* Copies the sidebar column out of the DOS surface into the texture page. The column is
 * x 240..319 of a 320 wide screen, which is 80 by 200; the rest of the page is left as
 * index 0, which the chroma key drops. */
static void sidebar_to_texture(void)
{
    int y, x;
    memset(g_sbtex, 0, sizeof g_sbtex);
    for (y = 0; y < DB_SCREEN_H; ++y)
        for (x = 0; x < 80; ++x)
            g_sbtex[y * SB_TEX + x] = g_surf8[y * DB_SCREEN_W + (DB_SIDE_X + x)];
}

/* THE 640x480 HUD. the project owner's direction for this build: use the new bar, not the 1995 one.
 *
 * It is the project's own game/hud640.c, compiled unedited, wrapped by tier1/t1_hud.c
 * which owns the buffers and gets them onto the card. The DOS bar below stays compiled
 * as the FALLBACK: if hud640.pack is missing or refuses to load, the game still has a
 * sidebar rather than a black column, which matters on a machine nobody is sitting at. */
static T1_Hud g_hud;
static int    g_hudok;

/* Scripted input. Off unless a script file is found; see t1_script.h for why it has to
 * exist at all. When it IS on, every read of the pointer, the buttons and the keyboard
 * comes from the file instead of the OS, and nothing below that point knows. */
static T1_Script g_sc;
static int       g_scripted;

/* THE SHROUD. One byte per cell, read out of the engine every heartbeat: the sim knows
 * exactly what the player has seen and this is a projection of that, never a second copy
 * kept in step by hand. */
static unsigned char g_vis[64 * 64];
/* Placement mode: the engine's per-cell verdict, the hovered footprint and the ghost.
 * Static rather than on WinMain's frame because it is 8 KB of grid. */
static T1_Place      g_place;
static T1_Cursor     g_cursor;
static T1_Menu       g_menu;
static int           g_menuok, g_inmenu;
static T1_Movie      g_movie;
static int           g_movieok;
static int           g_briefed;      /* the mission briefing has already played */
/* The left button as the input block last saw it. The menu runs after that block and
 * before the game's own click handling, and it must not disturb either. */
static int           g_lbnow;
/* The engine tick the current object list was READ at. Everything animated from a dumped
 * stage counter carries itself forward from here. */
static long          g_dumptick;
/* The scenario this run is playing, and which side's mesh bank it needs. One name, from
 * one place: three literals that must agree is two chances to load a terrain from one
 * mission and a world from another. */
static char          g_scen[16];
static int           g_side;          /* 0 GDI, 1 Nod */
/* The cells the camera can see this frame live beside in_view, further down. */

/* THE DEBRIS CHUNKS. Fourteen meshes, DBRV0..6 for vehicle wreckage and DBRS0..6 for
 * structure, already in the bank and never drawn. Resolved once at load. */
static int           g_dbr[2][7];
static int           g_dbrok;
static double        g_chunkpx;

/* Drawn through the ordinary mesh path in the ordinary mesh state, because a chunk IS an
 * ordinary model: it is the sprite pass that is special, not this. The tumble is the
 * chunk's own seed and age, so it is deterministic and costs no stored rotation. */
static void chunk_mesh_draw(int family, int idx, float wx, float wy, float wz,
                            float scale, int house, unsigned int seed, int age)
{
    T1_MeshParams mp;
    if (family < 0 || family > 1 || idx < 0 || idx > 6) return;
    if (g_dbr[family][idx] < 0) return;
    memset(&mp, 0, sizeof mp);
    mp.mesh = g_dbr[family][idx];
    mp.wx = wx; mp.wy = wy; mp.wz = wz;
    /* One free axis of tumble, off the hash and the age. The console tumbles on all
     * three; this renderer's mesh path yaws about Y only, so the other two are
     * registered in known-gap notes rather than faked with a second draw. */
    mp.facing = (int)((seed >> 3) + (unsigned)(age * 11)) & 255;
    mp.animT = -1.0f;
    mp.build_frac = 1.0f;
    mp.modemask = T1_MASK_SOLID;
    g_mesh.house = house;
    g_chunkpx += (double)t1_mesh_draw_p(&g_mesh, 0, &g_cam, &g_scr, &mp);
}
/* Diagnostic only: which infantry DoTypes reached the screen, and which strip frames. */
static long          g_doseen[34];
static long          g_procstage[64];
static unsigned char g_frameseen[64];
/* Diagnostic only: which BStates the engine ever reported for a building, and the
 * largest makecnt and dostage seen with them. */
static long          g_bstate_seen[8];
static int           g_makecnt_max, g_dostage_max;
static struct { char name[12]; int stage, mk, tick; } g_bsam[24];
static int           g_nbsam;
static int           g_viscells;

/* The nearest drawable object to a screen point, within the same 26-pixel radius the
 * click path uses. Factored out so the HOVER and the CLICK cannot disagree about what is
 * under the pointer, which is the whole point: the cursor promises what the click will
 * do. Returns an index into g_objs, or -1. */
static int pick_at(const W98_Object *objs, int n, const T1_Terrain *terr,
                   const T1_Cam *cam, const T1_Screen *scr, int mx, int my)
{
    int i, best = -1;
    float bestd = 26.0f * 26.0f;
    for (i = 0; i < n; ++i)
    {
        float ox, oz, oy, sx, sy, dx, dy, d;
        SR_Vertex pv;
        const char *k = objs[i].kind;
        if (objs[i].limbo) continue;
        if (strcmp(k, "TERRAIN") == 0) continue;
        if (strcmp(k, "INFANTRY") && strcmp(k, "UNIT")
            && strcmp(k, "AIRCRAFT") && strcmp(k, "BUILDING")) continue;
        ox = objs[i].clx / 256.0f;
        oz = objs[i].cly / 256.0f;
        oy = t1_terrain_corner_y(terr, (int)ox, (int)oz);
        t1_world_to_eye(cam, ox, oy, oz, &pv);
        if (!(pv.w > 0.25f)) continue;
        sx = scr->cx + pv.x * scr->fx / pv.w;
        sy = scr->cy - pv.y * scr->fy / pv.w;
        dx = sx - (float)mx; dy = sy - (float)my;
        d = dx * dx + dy * dy;
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

/* The rectangle of cells the camera can see, in cells, inclusive.
 *
 * The four screen corners are intersected with the horizontal plane at the terrain's own
 * HIGHEST and LOWEST points, which brackets every height a view ray can cross, and the
 * union of those eight ground points is the box. That makes the bound conservative by
 * construction rather than by a margin somebody guessed: a hill cannot poke into the
 * frame from outside it, because a hill IS the high plane.
 *
 * TWO cells of slop on each side, and the second one was earned rather than chosen. With
 * one, an A/B against the un-culled frame differed by 55 pixels in a ten-pixel square at
 * the extreme top-left -- the FARTHEST corner of a pitched view, where a fraction of a
 * degree is a long way on the ground. One cell is right for the near edge and short for
 * the far one; two covers both and costs four extra rows and columns out of twenty. */
static void visible_cells(const T1_Cam *cam, const T1_Screen *scr, const T1_Terrain *t,
                          int *x0, int *z0, int *x1, int *z1)
{
    static const float CORNER[4][2] = {
        { 0.0f, 0.0f }, { (float)SCR_W, 0.0f },
        { 0.0f, (float)SCR_H }, { (float)SCR_W, (float)SCR_H }
    };
    float lo = t->ylo, hi = t->yhi;
    float mnx = 1e9f, mnz = 1e9f, mxx = -1e9f, mxz = -1e9f;
    int i, k;

    for (i = 0; i < 4; ++i)
        for (k = 0; k < 2; ++k)
        {
            float wx, wz;
            t1_screen_to_plane(cam, scr, CORNER[i][0], CORNER[i][1], k ? hi : lo, &wx, &wz);
            if (wx < mnx) mnx = wx;
            if (wx > mxx) mxx = wx;
            if (wz < mnz) mnz = wz;
            if (wz > mxz) mxz = wz;
        }
    *x0 = (int)mnx - 2; *z0 = (int)mnz - 2;
    *x1 = (int)mxx + 2; *z1 = (int)mxz + 2;
    if (*x0 < 0) *x0 = 0;
    if (*z0 < 0) *z0 = 0;
    if (*x1 > 63) *x1 = 63;
    if (*z1 > 63) *z1 = 63;
}

/* Is this cell inside the box the camera can see, with a margin?
 *
 * The terrain cull's own box is exact for CELLS, and a cell's contents are not a cell: a
 * Weapons Factory is three cells across and stands a cell and a half tall, so an object
 * anchored outside the box can still have geometry inside the frame. The margin is
 * therefore generous where the terrain's is tight -- four cells is wider than anything
 * the pack draws -- and it is still a twentieth of a full-sized map.
 *
 * Costs one comparison per object and per overlay cell, against a draw that otherwise
 * submits geometry the guard band will reject one triangle at a time. */
static int g_viewx, g_viewz, g_vieww, g_viewh;
static int in_view(int cx, int cz, int margin)
{
    return cx >= g_viewx - margin && cx < g_viewx + g_vieww + margin
        && cz >= g_viewz - margin && cz < g_viewz + g_viewh + margin;
}

/* Is this cell's contents allowed on screen?
 *
 * The 1995 rule, and it is two rules, not one: a BUILDING you have seen stays drawn from
 * memory even when nobody is watching it, because the player is remembering a base that
 * does not move. A UNIT needs live sight, because it does. Anything on a cell that has
 * never been mapped is not drawn at all, which is also what makes the veil honest: it is
 * hiding something that is really there rather than covering a picture of it. */
/* The same test with the overlay renderer's signature. Walls have HEIGHT, so they are
 * culled the way objects are rather than hidden under the veil: a flat decal can lie
 * under the blanket, a wall would poke straight through it. A wall you have seen stays
 * drawn, like a building, because it is one. */
static int cell_shows(int cx, int cz, int is_building);
static int cell_shows_building(int cx, int cz)
{ return in_view(cx, cz, 4) && cell_shows(cx, cz, 1); }
static int cell_shows_unit(int cx, int cz)
{ return in_view(cx, cz, 4) && cell_shows(cx, cz, 0); }
/* The same view test WITHOUT the fog, for the passes that run with the shroud off: the
 * two are independent questions and `noshroud` must not also turn the cull off. */
static int cell_in_view_only(int cx, int cz)   { return in_view(cx, cz, 4); }

static int cell_shows(int cx, int cz, int is_building)
{
    int s;
    if (cx < 0 || cx > 63 || cz < 0 || cz > 63) return 1;
    s = g_vis[cz * 64 + cx];
    return is_building ? (s >= WB_SHROUD_MAPPED) : (s == WB_SHROUD_VISIBLE);
}

/* ---- SOUND ---------------------------------------------------------------------------
 * The audio engine is the project's own, platform-free C89 with one backend file, and
 * audio/audio_win98.c was written for exactly this machine and had never been compiled
 * for it. It is taken unedited. The engine reports every effect and every EVA line
 * through the brain's event callback, which tier1/w98_brain.c decodes; this is the other
 * end of that wire.
 *
 * 24 pixels to a cell, because that is the unit the engine's PixelX/PixelY arrive in
 * (cnc_eyes.cpp:3211, TD_PIXELS_PER_CELL), and the listener has to speak the same one or
 * every sound pans the wrong way.
 * ------------------------------------------------------------------------------------ */
#define TD_PIXELS_PER_CELL 24
static CncAudio *g_au;

/* The movie's own audio into the mixer's MOVIE bus, which already exists and already runs
 * at 22050 Hz -- the rate every one of these files is in, so nothing resamples. */
static long g_moviepcm, g_moviepcm_taken;
static int movie_push(const short *pcm, int samples)
{
    Mixer *mx = g_au ? cnc_audio_mixer(g_au) : 0;
    int took;
    g_moviepcm += samples;
    if (!mx) return 0;
    took = mixer_movie_push(mx, pcm, samples);
    g_moviepcm_taken += took;
    return took;
}
static int       g_over, g_won;
static long      g_sfxplayed, g_speechplayed;

static void on_engine_event(const W98_Event *e)
{
    if (e->type == WB_EV_GAMEOVER) { g_over = 1; g_won = e->win; return; }
    if (!g_au) return;
    if (e->type == WB_EV_SFX)
    {
        if (cnc_audio_on_sound_effect(g_au, e->index, e->variation, e->px, e->py) >= 0)
            ++g_sfxplayed;
    }
    else if (e->type == WB_EV_SPEECH)
    {
        if (cnc_audio_on_speech(g_au, e->index) >= 0) ++g_speechplayed;
    }
}

static int key_down(int vk)
{
    if (g_scripted) return t1_script_key(&g_sc, vk);
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

/* Does this screen pixel belong to the HUD rather than to the battlefield?
 *
 * With the new bar that is not just "right of x = 480" any more: the OPTIONS and CREDITS
 * tab plates sit on the top 17 rows over what would otherwise be playfield, and a click
 * that fell through one of them would order units to a spot the player cannot see. */
static int in_hud(int mx, int my)
{
    if (g_hudok)
    {
        if (mx >= H6_BAR_X) return 1;
        if (my < H6_TAB_H && (mx < H6_BAR_W || mx >= H6_BAR_X - H6_BAR_W)) return 1;
        return 0;
    }
    return mx >= SCR_W - 160;
}

/* One screen-space line, whichever bar is live. Both draw it as a thin quad of a solid
 * white block, because this backend has no untextured path and does not need one. */
static void ui_rect(float x0, float y0, float x1, float y1)
{
    if (g_hudok) t1_hud_rect(&g_hud, x0, y0, x1, y1);
    else         hud_rect(x0, y0, x1, y1, &g_sbsr);
}

/* ---- selection brackets, from the engine's own sel flag ----
 *
 * This was missing entirely on this backend, so the engine selected the unit and the
 * screen said nothing, and the only reasonable conclusion from the outside was that
 * clicking did not work. */
static void draw_brackets(const W98_Object *objs, int n, const T1_Cam *cam,
                          const T1_Screen *scr, const T1_Terrain *terr)
{
    int i;
    for (i = 0; i < n; ++i)
    {
        float ox, oz, oy, sx2, sy2, r, t2, w2;
        SR_Vertex pv;
        if (!objs[i].sel || objs[i].limbo) continue;
        ox = objs[i].clx / 256.0f;
        oz = objs[i].cly / 256.0f;
        oy = t1_terrain_corner_y(terr, (int)ox, (int)oz);
        t1_world_to_eye(cam, ox, oy, oz, &pv);
        if (!(pv.w > 0.25f)) continue;
        sx2 = scr->cx + pv.x * scr->fx / pv.w;
        sy2 = scr->cy - pv.y * scr->fy / pv.w;
        r = scr->fx / pv.w * 0.45f;
        if (r < 6.0f) r = 6.0f;
        if (r > 60.0f) r = 60.0f;
        /* corner ticks, the way the engine draws them, not a full box */
        t2 = r * 0.45f; w2 = 2.0f;
        ui_rect(sx2-r,    sy2-r,    sx2-r+t2, sy2-r+w2);
        ui_rect(sx2-r,    sy2-r,    sx2-r+w2, sy2-r+t2);
        ui_rect(sx2+r-t2, sy2-r,    sx2+r,    sy2-r+w2);
        ui_rect(sx2+r-w2, sy2-r,    sx2+r,    sy2-r+t2);
        ui_rect(sx2-r,    sy2+r-w2, sx2-r+t2, sy2+r);
        ui_rect(sx2-r,    sy2+r-t2, sx2-r+w2, sy2+r);
        ui_rect(sx2+r-t2, sy2+r-w2, sx2+r,    sy2+r);
        ui_rect(sx2+r-w2, sy2+r-t2, sx2+r,    sy2+r);
    }
}

static FILE *g_rep;
static void report(const char *fmt, ...)
{
    char p[MAX_PATH];
    va_list ap;
    if (!g_rep)
    {
        _snprintf(p, sizeof p, "%sw98glide.txt", g_base);
        g_rep = fopen(p, "wb");
        if (!g_rep) return;
    }
    va_start(ap, fmt); vfprintf(g_rep, fmt, ap); va_end(ap);
    fputs("\r\n", g_rep); fflush(g_rep);
}

static double now_s(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}

static void save_bmp(const char *name)
{
    char path[MAX_PATH];
    FILE *f;
    unsigned char hdr[54], *row;
    int rowbytes = (SCR_W * 3 + 3) & ~3, x, y;
    long imgsize = (long)rowbytes * SCR_H, filesize = 54 + imgsize;

    _snprintf(path, sizeof path, "%s%s", g_base, name);
    f = fopen(path, "wb");
    if (!f) return;
    row = (unsigned char *)malloc((size_t)rowbytes);
    if (!row) { fclose(f); return; }
    memset(hdr, 0, sizeof hdr);
    hdr[0]='B'; hdr[1]='M';
    hdr[2]=(unsigned char)filesize;       hdr[3]=(unsigned char)(filesize>>8);
    hdr[4]=(unsigned char)(filesize>>16); hdr[5]=(unsigned char)(filesize>>24);
    hdr[10]=54; hdr[14]=40;
    hdr[18]=(unsigned char)SCR_W; hdr[19]=(unsigned char)(SCR_W>>8);
    hdr[22]=(unsigned char)SCR_H; hdr[23]=(unsigned char)(SCR_H>>8);
    hdr[26]=1; hdr[28]=24;
    fwrite(hdr,1,54,f);
    for (y = SCR_H-1; y >= 0; --y)
    {
        const unsigned short *s = g_read + (long)y * SCR_W;
        memset(row, 0, (size_t)rowbytes);
        for (x = 0; x < SCR_W; ++x)
        {
            unsigned short p = s[x];                     /* RGB565 */
            row[x*3+0] = (unsigned char)(( p        & 0x1F) << 3);
            row[x*3+1] = (unsigned char)(((p >>  5) & 0x3F) << 2);
            row[x*3+2] = (unsigned char)(((p >> 11) & 0x1F) << 3);
        }
        fwrite(row,1,(size_t)rowbytes,f);
    }
    fclose(f); free(row);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char err[512], path[MAX_PATH], *slash;
    /* Zero means run until ESC, which is what a playable build wants and what launching
     * with no argument should give you. A number benchmarks that many frames and exits.
     * This defaulted to 400 and silently made every interactive test a four second one. */
    int frames = 0, frame, i, nobj = 0, ticks = 0;
    W98_MapInfo map;
    double t0, tprev, acc = 0.0, tterr = 0.0, tobj = 0.0, tsim = 0.0, tswap = 0.0, tp;
    double tbar = 0.0, tinf = 0.0;
    int infdrawn = 0, infup = 0, infskip = 0;
    char infwhy[200];
    int mx = SCR_W / 2, my = SCR_H / 2;
    int wasLB = 0, wasRB = 0, wasPU = 0, wasPD = 0, redump = 0;
    int wasUp = 0, wasDown = 0, wasEnter = 0, wasMenuLB = 0;
    int wasEsc = 0, escEdge = 0, escUsed = 0;
    int nclick = 0, norder = 0, sweep = 0, nbuild = 0, nplace = 0, nradar = 0, recwav = 0;
    int nplacebad = 0;
    /* Edge scroll and middle-drag state. rawmb is the RAW button, separate from what the
     * drag did with it, for the same reason rawlb is: a dead button and a dead handler
     * look identical from the report otherwise. */
    long nedge = 0, ndrag = 0, rawmb = 0;
    int  dragging = 0;
    float dragx = 0.0f, dragz = 0.0f;
    int noshroud = 0, noover = 0, trace = 0, stage = 0, novsync = 0;
    int noobj = 0, noinf = 0, nohud = 0, noterr = 0, freecam = 0, nohouse = 0;
    int noanim = 0, nobuild = 0, nocursor = 0, noshadow = 0, nocull = 0;
    int forceradar = 0;
    /* Raw input evidence, separate from what the game DID with it. Without this a dead
     * click and a dead click-handler look identical from the report, which is exactly the
     * confusion that cost an evening earlier on this branch. */
    long rawlb = 0, rawrb = 0, rawd = 0;
    int g_sellmode = 0;
    int g_placing = 0, g_place_t = 0, g_place_i = 0;
    int wasD = 0, ndeploy = 0;
    int nsel = 0;
    int bandx = 0, bandy = 0, banding = 0, bandactive = 0;
    double lb_seen = -1.0, rb_seen = -1.0;
    int downx = 0, downy = 0;
    /* double, not long: 3000 frames of a megapixel each overflows a 32-bit counter and
     * the sweep duly reported terrain_px_per_frame as NEGATIVE. */
    double terrpx = 0.0, objpx = 0.0, shroudpx = 0.0, overpx = 0.0, tibpx = 0.0;
    double efxpx = 0.0, placepx = 0.0, curpx = 0.0, shadpx = 0.0;
    long hidden = 0;
    /* How often the two substitutions actually fired. A rig that never draws and a rig
     * that draws every frame in the wrong place look identical from a screenshot taken
     * at the wrong moment, and the deploy lasts about two seconds. */
    long rigdraws = 0, procdraws = 0, buildups = 0;

    (void)inst; (void)prev; (void)show;
    GetModuleFileNameA(NULL, g_base, sizeof g_base - 32);
    slash = strrchr(g_base, '\\');
    if (slash) slash[1] = 0; else g_base[0] = 0;
    if (cmd && *cmd) { int a = atoi(cmd); frames = (a > 0) ? a : 0; }
    if (cmd && strstr(cmd, "sweep")) sweep = 1;
    if (cmd && strstr(cmd, "recwav")) recwav = 1;
    /* Bisection switches. A hang on this card leaves nothing behind but a frozen screen,
     * so being able to turn one pass off from the command line is the difference between
     * finding the cause in two runs and guessing at it. */
    if (cmd && strstr(cmd, "noshroud")) noshroud = 1;
    if (cmd && strstr(cmd, "noover"))   noover = 1;
    if (cmd && strstr(cmd, "trace"))    trace = 1;
    if (cmd && strstr(cmd, "trace2"))   trace = 2;
    if (cmd && strstr(cmd, "novsync"))  novsync = 1;
    if (cmd && strstr(cmd, "noobj"))    noobj = 1;
    if (cmd && strstr(cmd, "noinf"))    noinf = 1;
    if (cmd && strstr(cmd, "nohud"))    nohud = 1;
    if (cmd && strstr(cmd, "noterr"))   noterr = 1;
    /* freecam lifts the scroll clamp and draws the whole 64x64 grid. SCG01EA's tiberium
     * and its sandbag compound both sit NORTH of the playable rect, where the engine
     * itself culls them and the camera cannot legally go, so this is the only way to look
     * at that art at all. Not a gameplay mode: the clamp is what stops the player
     * scrolling off the world. */
    if (cmd && strstr(cmd, "freecam")) freecam = 1;
    if (cmd && strstr(cmd, "nohouse")) nohouse = 1;
    /* noanim holds every model at its rest pose, which is the picture this renderer drew
     * before the pack's baked clips were converted. nobuild skips the section-by-section
     * assembly and puts finished buildings down whole. Both exist so a regression in the
     * animation path can be separated from one in the geometry. */
    if (cmd && strstr(cmd, "noanim"))  noanim = 1;
    if (cmd && strstr(cmd, "nobuild")) nobuild = 1;
    /* nocursor puts the flat 2D pointer back everywhere, which is the picture this build
     * drew before the cartridge's own fourteen cursor models were wired up. */
    if (cmd && strstr(cmd, "nocursor")) nocursor = 1;
    if (cmd && strstr(cmd, "noshadow")) noshadow = 1;
    /* nocull draws the ground and the veil over the whole PLAYABLE RECT again, which is
     * what this build did before the camera cull. It exists to be diffed against: a cull
     * that removes something the player can see is invisible in a single screenshot and
     * obvious in a pair. */
    if (cmd && strstr(cmd, "nocull"))   nocull = 1;
    /* `radar` forces the minimap surface on. GDI mission 1 has no Communications Centre
     * and never will, so the engine reports radar_active = 0 for the whole mission and
     * the plate correctly shows the GDI eagle instead. That makes the minimap unseeable
     * on the one scenario this build ships, which is not the same thing as untested. */
    if (cmd && strstr(cmd, "radar"))   forceradar = 1;

    infwhy[0] = 0;
    report("CNC3D on the 3dfx Voodoo 2");

    /* Loaded FIRST, because everything below asks whether input is scripted. */
    _snprintf(path, sizeof path, "%sinput.script", g_base);
    g_scripted = t1_script_load(&g_sc, path) > 0;
    if (g_scripted) report("input: SCRIPTED, %d commands from input.script", g_sc.n);

    /* ---- WHICH SCENARIO ------------------------------------------------------------
     *
     * This build shipped exactly one, GDI mission 1, and its name was a literal in three
     * places: the terrain file, the mesh bank and the brain's start call. Three literals
     * that must agree is two chances to load a terrain from one mission and a world from
     * another, which on this card looks like a renderer bug rather than a mismatch.
     *
     * One name now, from one of three sources in order: `scen=NAME` on the command line
     * (for a scripted run), a `scenario.txt` beside the exe (one line, so the box can be
     * pointed at a different mission without a rebuild), and failing both, SCG01EA.
     *
     * The SIDE comes out of the name's own third character, because the mesh bank is
     * byte-identical across every pack of a side -- 'G' takes gdi.t1mesh and 'B' takes
     * nod.t1mesh -- so a Nod mission needs a second bank and says so at load rather than
     * drawing GDI models for Nod units. */
    {
        const char *p2 = cmd ? strstr(cmd, "scen=") : 0;
        strcpy(g_scen, "SCG01EA");
        if (p2)
        {
            int k = 0;
            p2 += 5;
            while (p2[k] && p2[k] != ' ' && k < (int)sizeof g_scen - 1)
            { g_scen[k] = (char)toupper((unsigned char)p2[k]); ++k; }
            g_scen[k] = 0;
        }
        else
        {
            FILE *sf;
            _snprintf(path, sizeof path, "%sscenario.txt", g_base);
            sf = fopen(path, "r");
            if (sf)
            {
                char lineb[64];
                if (fgets(lineb, sizeof lineb, sf))
                {
                    int k = 0;
                    while (lineb[k] && lineb[k] > ' ' && k < (int)sizeof g_scen - 1)
                    { g_scen[k] = (char)toupper((unsigned char)lineb[k]); ++k; }
                    if (k) g_scen[k] = 0; else strcpy(g_scen, "SCG01EA");
                }
                fclose(sf);
            }
        }
        g_side = (g_scen[2] == 'B') ? 1 : 0;
        report("scenario: %s (%s)", g_scen, g_side ? "Nod" : "GDI");
    }

    _snprintf(path, sizeof path, "%s%s.t1terr", g_base, g_scen);
    if (!t1_terrain_load(&g_terr, path, err, sizeof err)) { report("terrain: %s", err); return 1; }
    report("terrain: %d page(s) of %dx%d, %d cells", g_terr.npages, g_terr.page_sz,
           g_terr.page_sz, g_terr.ncells);

    _snprintf(path, sizeof path, "%s%s.t1mesh", g_base, g_side ? "nod" : "gdi");
    if (!t1_mesh_load(&g_mesh, path, err, sizeof err)) { report("mesh: %s", err); return 1; }
    report("meshes: %d, textures %d, triangles %d, house variants %d",
           g_mesh.nmesh, g_mesh.ntex, g_mesh.ntri, g_mesh.ngdi);
    {   /* What the bank can ANIMATE, said at load rather than discovered by watching. A
         * pack baked before the clips were converted loads fine and simply never moves,
         * which is indistinguishable from a broken driver from the outside. */
        int k, nclip = 0, nsec = 0;
        for (k = 0; k < g_mesh.nmesh; ++k)
        {
            if (t1_mesh_clip(&g_mesh, k, 0, 0, 0, 0, 0)) ++nclip;
            if (t1_mesh_sections(&g_mesh, k) > 1) ++nsec;
        }
        report("animation: %d mesh(es) with baked clips, %d assemblable, %ld KB of pose;"
               " mcvrig=%s procrig=%s",
               nclip, nsec, g_mesh.animsize / 1024,
               t1_mesh_for_type(&g_mesh, "MCVANIM") >= 0 ? "yes" : "ABSENT",
               t1_mesh_for_type(&g_mesh, "PROCANIM") >= 0 ? "yes" : "ABSENT");
    }

    t1_cursor_init(&g_cursor, &g_mesh, report);

    if (!wb_open(NULL, err, sizeof err)) { report("brain: %s", err); return 1; }

    /* Sound, before the mission starts, so the opening EVA line is not missed. A device
     * that refuses to open is not an error anywhere: audio_boot hands back an engine that
     * makes no noise and the game carries on, which is the contract audioboot.h states. */
    {
        AudioBootOpts ab;
        char dd[MAX_PATH], wav[MAX_PATH];
        memset(&ab, 0, sizeof ab);
        _snprintf(dd, sizeof dd, "%sdosdata", g_base);
        ab.dosdata = dd;
        ab.music_vol255 = -1;
        ab.sound_vol255 = -1;
        /* `recwav` on the command line RECORDS the mix to disk instead of playing it.
         * That is the only way to CHECK the sound from here: there is nobody in the room,
         * and a WAV can be fetched and measured where a speaker cannot. It is opt-in
         * rather than automatic on a scripted run, so a scripted demo still makes noise
         * for whoever is watching it. */
        if (recwav)
        {
            _snprintf(wav, sizeof wav, "%smix.wav", g_base);
            ab.wav = wav;
        }
        g_au = audio_boot(&ab);
        wb_set_event_hook(on_engine_event);
        report("audio: backend=%s device=%d dir=%s%s",
               audio_backend_name(), audio_boot_have_device(), dd,
               ab.wav ? " (recording to mix.wav)" : "");
    }
    {
        char content[MAX_PATH], missions[MAX_PATH];
        _snprintf(content,  sizeof content,  "%scontent\\",  g_base);
        _snprintf(missions, sizeof missions, "%smissions\\", g_base);
        if (!wb_start(content, missions, g_scen, 1, err, sizeof err))
        { report("start: %s", err); return 1; }
    }
    report("brain: %s started", g_scen);
    t1_over_init(&g_mesh);
    {   /* the debris chunk models, DBRV0..6 (vehicle) and DBRS0..6 (structure) */
        int f, k, have = 0;
        char code[8];
        for (f = 0; f < 2; ++f)
            for (k = 0; k < 7; ++k)
            {
                _snprintf(code, sizeof code, "DBR%c%d", f ? 'S' : 'V', k);
                g_dbr[f][k] = t1_mesh_for_type(&g_mesh, code);
                if (g_dbr[f][k] >= 0) ++have;
            }
        g_dbrok = (have == 14);
        report("debris: %d of 14 chunk models -> %s", have,
               g_dbrok ? "flying wreckage" : "sprites only");
    }
    report("walls: %d of 20 pieces in the pack", t1_over_wall_pieces());


    _snprintf(path, sizeof path, "%sdossidebar.pack", g_base);
    g_pack = db_pack_load(path, err, sizeof err);
    if (!g_pack) report("sidebar: %s (it will not be drawn)", err);
    else
    {
        memcpy(g_dospal, g_pack->pal8, 768);
        g_dospal[0] = 255; g_dospal[1] = 0; g_dospal[2] = 255;
        db_surface_init(&g_surf, DB_SCREEN_W, DB_SCREEN_H, g_surf8);
        db_state_clear(&g_state);
        g_state.radar_active = 1;
        g_credits = 0;
        report("sidebar: %d shapes, %d fonts", g_pack->nshapes, g_pack->nfonts);
    }

    _snprintf(path, sizeof path, "%ssmudge.t1smd", g_base);
    g_smdok = t1_tib_load(&g_smd, path, err, sizeof err);
    if (!g_smdok) report("smudges: %s (no scorch marks or craters)", err);
    else report("smudges: %d types x %d frames", g_smd.types, g_smd.frames);

    _snprintf(path, sizeof path, "%sefx.t1efx", g_base);
    g_efxok = t1_efx_load(&g_efx, path, err, sizeof err);
    if (!g_efxok) report("effects: %s (there will be no explosions)", err);
    else report("effects: %d art sequences", g_efx.nseq);

    _snprintf(path, sizeof path, "%sverdict.t1vrd", g_base);
    g_vrdok = verdict_load(path, err, sizeof err);
    if (!g_vrdok) report("verdict: %s (the mission will end without a banner)", err);

    _snprintf(path, sizeof path, "%sdostib.t1tib", g_base);
    g_tibok = t1_tib_load(&g_tib, path, err, sizeof err);
    if (!g_tibok) report("tiberium: %s (fields will not be drawn)", err);
    else report("tiberium: %d types x %d frames, %d sheet(s)",
                g_tib.types, g_tib.frames, g_tib.nsheet);

    _snprintf(path, sizeof path, "%sdosinfantry.pack", g_base);
    g_infok = t1_inf_load(&g_inf, path, err, sizeof err);
    if (!g_infok) report("infantry: %s (they will not be drawn)", err);
    else
    {
        memcpy(g_infpal, g_inf.pal8, 768);
        g_infpal[0]  = 255; g_infpal[1]  = 0; g_infpal[2]  = 255;
        g_infpal[12] = 255; g_infpal[13] = 0; g_infpal[14] = 255;   /* index 4: shadow */
        report("infantry: %d strips, %d types, %d reshaped for the 8:1 aspect limit",
               g_inf.nstrip, g_inf.ntype, g_inf.reshaped);
    }

    if (!t1_glide_open(SCR_W, SCR_H, err, sizeof err)) { report("glide: %s", err); return 1; }
    t1_glide_vsync(!novsync);
    report("glide: open at %dx%d, vsync=%d", SCR_W, SCR_H, !novsync);

    /* Upload both banks. One TMU palette at a time, so the passes swap it; whether that
     * costs anything measurable is one of the things this run answers. */
    t1_glide_palette(g_terr.pal);
    for (i = 0; i < g_terr.npages; ++i)
        if (!t1_glide_upload(&g_terr.tex[i], err, sizeof err))
        { report("terrain page %d: %s", i, err); return 1; }
    t1_glide_palette(g_mesh.pal);
    {
        /* THE SQUARE TEST THAT WAS HERE REFUSED 54 OF 359 OBJECT TEXTURES.
         *
         * The Voodoo takes any power-of-two up to 256 in either direction at up to 8:1,
         * which t1_glide_upload's aspect_for already works out; insisting on w == h threw
         * away every oblong one. What that looks like from the outside is not a missing
         * texture, it is a MISSING OBJECT: glide_tri drops a triangle whose texture never
         * uploaded, so all 42 of SCG01EA's sandbag cells were submitted every frame and
         * drew three pixels between them.
         *
         * The identical mistake was made and fixed once already for the DOS infantry, and
         * this copy survived because the object bank is mostly square and nothing that
         * mattered had gone missing yet. */
        int ok = 0, fail = 0, oblong = 0;
        char why[160];
        why[0] = 0;
        for (i = 0; i < g_mesh.ntex; ++i)
        {
            if (g_mesh.tex[i].w != g_mesh.tex[i].h) ++oblong;
            if (t1_glide_upload(&g_mesh.tex[i], err, sizeof err)) ++ok;
            else
            {
                ++fail;
                if (!why[0]) _snprintf(why, sizeof why, "%dx%d: %s",
                                       g_mesh.tex[i].w, g_mesh.tex[i].h, err);
                if (strstr(err, "TMU full")) break;
            }
        }
        {   /* The shadow alpha planes, which are their own bank and their own format.
             * 18 KB in total, and without them the shadow pass draws nothing at all:
             * every one of these textures is white RGB with its coverage in ALPHA, and
             * the palettised bank's alpha >= 128 threshold had already turned 34 of the
             * 35 completely transparent. */
            int sk, sok = 0, sfail = 0;
            for (sk = 0; sk < g_mesh.nshtex; ++sk)
            {
                if (t1_glide_upload(&g_mesh.shtex[sk], err, sizeof err)) ++sok;
                else ++sfail;
            }
            report("shadow planes: %d of %d uploaded%s%s", sok, g_mesh.nshtex,
                   sfail ? " -- first refusal: " : "", sfail ? err : "");
            t1_inf_shadow_init();
            report("soldier shadow: %s",
                   t1_inf_shadow_ready() ? "16x16 alpha, mirrored from the ROM quadrant"
                                         : "REFUSED by the card");
        }
        report("textures uploaded: terrain %d, objects %d of %d (%d refused, %d oblong)",
               g_terr.npages, ok, g_mesh.ntex, fail, oblong);
        if (fail) report("  first refusal: %s", why);
    }

    /* The new HUD goes into texture memory BEFORE the infantry, which upload lazily and
     * would otherwise be able to fill the TMU and leave the sidebar with nowhere to live.
     * Three pages: 256x256 for the top half of the bar, 256x256 for the bottom, 256x64
     * for the two tab plates and the pointer. 288 KB of a 4 MB TMU. */
    if (g_vrdok)
    {
        if (!t1_glide_upload(&g_vrd[0], err, sizeof err) ||
            !t1_glide_upload(&g_vrd[1], err, sizeof err))
        { report("verdict upload: %s", err); g_vrdok = 0; }
    }

    if (g_tibok && !t1_tib_upload(&g_tib, err, sizeof err))
    { report("tiberium upload: %s", err); g_tibok = 0; }
    if (g_smdok && !t1_tib_upload(&g_smd, err, sizeof err))
    { report("smudge upload: %s", err); g_smdok = 0; }
    if (g_efxok && !t1_efx_upload(&g_efx, err, sizeof err))
    { report("effects upload: %s", err); g_efxok = 0; }

    _snprintf(path, sizeof path, "%shud640.pack", g_base);
    {   /* THE MAIN MENU's own pack. Same container as the sidebar's, so the same loader
         * reads it: the title plate, the dialog filigree, the DOS pointer and five fonts.
         * If it is not on the box the game boots into the mission the way it always has,
         * which is a legible fallback rather than a hole. */
        char mpath[MAX_PATH];
        _snprintf(mpath, sizeof mpath, "%sdosmenu.pack", g_base);
        g_menuok = t1_menu_load(&g_menu, mpath, err, sizeof err);
        g_inmenu = g_menuok;
        /* UPLOADED HERE AND NOT WITH THE REST OF THE TEXTURES, because the pack is
         * opened after that block runs: putting the upload beside the others left the
         * menu permanently un-uploaded and drew a black screen with a pointer on it. */
        if (g_menuok && !t1_menu_upload(&g_menu, err, sizeof err))
        { report("menu upload: %s", err); g_menuok = 0; g_inmenu = 0; }
        /* The movie plate: four pages of texture memory, allocated once. A movie that is
         * never played costs 80 KB of a 4 MB TMU and nothing else. */
        g_movieok = t1_movie_init(&g_movie, err, sizeof err);
        if (!g_movieok) report("movies: %s", err);
        report("menu: %s", g_menuok ? "dosmenu.pack loaded, booting to the title screen"
                                    : err);
    }
    g_hudok = t1_hud_load(&g_hud, path, err, sizeof err);
    /* THE 1995 POINTER, into the HUD's aux page. MOUSE.SHP frame 0 is what the DOS game
     * drew over its sidebar (sidebar.cpp:2252), and it is the right art for the one place
     * a world-space 3D cursor cannot go: there is no ground under an opaque panel. Both
     * the pack and the HUD have to be open, which is why it happens here and not beside
     * either of them. */
    if (g_hudok && g_pack)
    {
        const DB_Shape *ms = db_shape(g_pack, "MOUSE");
        if (ms && ms->frames > 0)
            t1_hud_set_pointer(&g_hud, ms->pixels, ms->w, ms->h, g_dospal);
        report("pointer: %s",
               ms ? "MOUSE.SHP frame 0, 30x24" : "the built-in wedge (no MOUSE shape)");
    }
    if (!g_hudok) report("hud640: %s (falling back to the 1995 bar)", err);
    else
    {
        t1_hud_prepare_radar(&g_hud, &g_terr);
        report("hud640: loaded, %d rows, radar cells averaged", T1H_ROWS);
    }

    if (g_pack)
    {
        sr_texture(&g_sbsr, g_sbtex, SB_TEX, SB_TEX);
        if (!t1_glide_upload(&g_sbsr, err, sizeof err))
            report("sidebar texture: %s", err);

    }

    t1_cam_set_dist(&g_cam, T1_DIST_DEF);
    t1_screen_params(&g_scr, SCR_W, SCR_H);
    {
        float ax, az, sx = 0.0f, sz = 0.0f;
        int n0, ng = 0;
        memset(&map, 0, sizeof map);
        wb_map(&map);
        if (map.cellw < 1) { map.cellw = 64; map.cellh = 64; }
        ax = (float)map.cellx + map.cellw * 0.5f;
        az = (float)map.celly + map.cellh * 0.5f;
        n0 = wb_objects(g_objs, WB_MAX_OBJECTS);
        for (i = 0; i < n0; ++i)
            if (!g_objs[i].limbo && strcmp(g_objs[i].house, "GoodGuy") == 0
                && strcmp(g_objs[i].kind, "TERRAIN") != 0)
            { sx += g_objs[i].clx / 256.0f; sz += g_objs[i].cly / 256.0f; ++ng; }
        if (ng) { ax = sx / ng; az = sz / ng; }
        t1_cam_look_at(&g_cam, ax, az, t1_terrain_corner_y(&g_terr, (int)ax, (int)az));
        report("camera at %.1f,%.1f  map %d,%d %dx%d",
               ax, az, map.cellx, map.celly, map.cellw, map.cellh);
        g_terr.base_y = g_terr.base_y;   /* keep the loaded base */
        for (i = 0; i < n0; ++i)
        {
            g_objmesh[i] = t1_mesh_for_type(&g_mesh, g_objs[i].name);
            g_inftype[i] = g_infok ? t1_inf_type(&g_inf, g_objs[i].name) : -1;
        }
        nobj = n0;
    }

    t0 = tprev = now_s();
    /* frames == 0 means run until ESC, which is what a playable build wants. A number
     * means benchmark that many and exit, which is what a measurement wants. */
    /* THE SCORE, and WHICH SCORE, because they are two different pieces of music.
     *
     * The MENU has its own: init.cpp:1054 starts THEME_MAP1 before Select_Game and lets
     * it loop under the whole title screen. The MISSION opens on Act on Instinct
     * (scenario.cpp:117/:143/:174 queue THEME_AOI as the scenario comes up).
     *
     * This build started Act on Instinct at LOAD, so the mission's theme played under the
     * menu -- which is the project owner's "wrong music for the main menu", and half of his "test map
     * plays behind the main menu" as well. The menu gets MAP1 and the mission theme now
     * waits until there is a mission. */
    if (g_au)
    {
        int want = -1;
        cnc_music_set_playlist(g_au, 1);
        if (g_inmenu && g_menuok) want = cnc_music_theme_index("MAP1");
        if (want < 0) want = cnc_music_theme_index("AOI");
        if (want < 0 || !cnc_music_play_index(g_au, want))
        {
            int first = cnc_music_next_theme(g_au, -1);
            if (first >= 0) cnc_music_play_index(g_au, first);
            else report("music: no score on this disc; the game is silent");
        }
        report("music: %s (%s)", cnc_music_current(g_au) ? cnc_music_current(g_au) : "(none)",
               (g_inmenu && g_menuok) ? "the menu's own theme" : "the mission");
    }

    for (frame = 0; frames <= 0 || frame < frames; ++frame)
    {
        /* A SCRIPTED RUN USES A FIXED CLOCK, and it is not a nicety.
         *
         * Everything downstream of dt is timed off it: the sim tick, the scroll, the
         * particle step, the movie. With a wall clock, two runs of the SAME script reach
         * a given frame number at different sim times, so two screenshots taken at frame
         * 420 are of different moments and cannot be diffed -- which is exactly what a
         * paired switch like nocull or noshadow exists to do. Measured: the same script
         * ran 1,226 ticks in one run and about half that in the next, purely because one
         * of them was faster.
         *
         * 1/60 rather than the real rate because it is a round number that no measurement
         * depends on: nothing here is a benchmark when a script is driving it. */
        double n = now_s(), dt = g_scripted ? (1.0 / 60.0) : (n - tprev);
        tprev = n;
        if (dt > 0.25) dt = 0.25;

        /* A HEARTBEAT, so a wedge says where it happened. The card can stop rasterising
         * mid frame with the process still running and nothing else on disk to read; a
         * line every 64 frames costs almost nothing and turns "it hung" into "it hung
         * between here and here". */
        /* stage is set as the frame walks its passes and reported at the START of the
         * next one, so a frozen screen says which pass it froze in. If the report keeps
         * growing after the picture stops, the CARD wedged; if the report stops too, the
         * CPU is stuck in our own code. Those are different bugs and look identical from
         * the outside. */
        if (trace) report("f%d stage=%d cam=%.1f,%.1f", frame, stage, g_cam.at_x, g_cam.at_z);
        stage = 1; if (trace > 1) report("  s%d", 1);
        if (g_scripted)
        {
            t1_script_step(&g_sc, frame);
            if (g_sc.overset) { g_over = 1; g_won = g_sc.over; }
            /* `place` commits the pending building at the first cell the ENGINE will
             * accept. A test affordance: a script has no way to know which cell that is,
             * and a placement path that has never actually placed anything is not a
             * tested one -- this branch shipped for a day with the wrong coordinate
             * space and every placement silently refused. */
            if (g_sc.place && g_placing)
            {
                int lx = 0, lz = 0;
                t1_place_update(&g_place, (float)g_place.hoverx, (float)g_place.hovery);
                if (t1_place_first_legal(&g_place, &lx, &lz))
                {
                    wb_place_at(g_place_t, g_place_i, lx - g_place.gridx, lz - g_place.gridy);
                    ++nplace;
                    report("scripted place: %s at cell %d,%d (grid %d,%d)",
                           g_place.item.name, lx, lz, lx - g_place.gridx, lz - g_place.gridy);
                }
                else { ++nplacebad; report("scripted place: no legal origin anywhere"); }
                g_placing = 0;
                t1_place_end(&g_place);
            }
        }

        /* Sound, once a frame, on this thread: audio_frame does file I/O (it refills the
         * music ring) and must never run in the waveOut callback. */
        if (g_au)
        {
            int vw = (int)((float)T1H_FIELD_W / g_scr.fx * g_cam.d_cells
                           * (float)TD_PIXELS_PER_CELL);
            int vh = (int)((float)SCR_H / g_scr.fy * g_cam.d_cells
                           * (float)TD_PIXELS_PER_CELL);
            if (vw < TD_PIXELS_PER_CELL) vw = TD_PIXELS_PER_CELL;
            if (vh < TD_PIXELS_PER_CELL) vh = TD_PIXELS_PER_CELL;
            cnc_audio_set_listener(g_au, (int)(g_cam.at_x * TD_PIXELS_PER_CELL),
                                   (int)(g_cam.at_z * TD_PIXELS_PER_CELL), vw, vh);
            cnc_audio_update(g_au);
            audio_frame(g_au, (int)(dt * 1000.0 + 0.5));
        }

        tp = now_s();
        acc += dt;
        /* THE SIM DOES NOT RUN BEHIND THE TITLE SCREEN. The brain is started at load
         * because it owns the map and the sidebar the renderer needs, but a mission
         * ticking away while the player reads a menu is not what the 1995 game does --
         * and it showed up here as a hundred particles spawned before anyone had chosen
         * to play. */
        if (g_inmenu && g_menuok) acc = 0.0;
        /* A NEW TICK IS A NEW SOUND WINDOW. Everything this advance raises reaches
         * on_engine_event and then the mixer before the next block is rendered, so copies
         * of one clip raised here start at the same sample and sum coherently. The audio
         * engine caps how many of one clip may do that; this is where it is told the
         * window moved. Without the call the tier is simply uncapped, as it is today. */
        while (acc >= 1.0 / 15.0)
        {
            cnc_audio_begin_tick(g_au);
            wb_tick();
            ++ticks;
            acc -= 1.0 / 15.0;
        }
        if ((frame % 8) == 0 || redump)
        {
            nobj = wb_objects(g_objs, WB_MAX_OBJECTS);
            g_dumptick = ticks;
            /* THE PARTICLE SIMULATION steps on the ENGINE TICK and nowhere else, so two
             * runs of the same script produce the same pixels. */
            if (g_efxok) t1_efx_step(&g_efx, &g_terr, wb_overlays(), ticks);
            /* Birthdays, for the buildup clock. Must run on every dump and before
             * anything asks for a build fraction. */
            t1_anim_track(g_objs, nobj, ticks);
            for (i = 0; i < nobj; ++i)
            {
                g_objmesh[i] = t1_mesh_for_type(&g_mesh, g_objs[i].name);
                g_inftype[i] = g_infok ? t1_inf_type(&g_inf, g_objs[i].name) : -1;
            }
            /* The sidebar rides the same heartbeat as the object list, so a frame never
             * shows a build queue from one tick beside a map from another. */
            g_viscells = wb_shroud(g_vis);
            /* noshroud means NO FOG AT ALL, not "draw the fog but keep hiding things".
             * Half a switch is worse than none when it is being used to find out whether
             * something is missing or merely covered up. */
            if (noshroud) g_viscells = 0;
            if (g_hudok && wb_sidebar(&g_sb))
            {
                t1_hud_set_state(&g_hud, &g_sb, g_pack);
                if (forceradar) g_hud.st.radar_active = 1;
                t1_hud_radar(&g_hud, &g_terr, &map, g_objs, nobj, &g_cam, &g_scr);
                g_credits = g_sb.credits;
            }
            else if (g_pack && wb_sidebar(&g_sb))
            {
                int c, k;
                db_state_clear(&g_state);
                for (c = 0; c < 2; ++c)
                    for (k = 0; k < g_sb.count[c] && k < DB_MAX_BUILDABLES; ++k)
                    {
                        W98_Build *bb = &g_sb.item[c][k];
                        DB_Slot *sl;
                        if (!bb->name[0]) continue;
                        db_state_add(&g_state, c, bb->name);
                        sl = &g_state.items[c][k];
                        sl->producing  = bb->constructing;
                        sl->completion = (int)(bb->progress * (float)(DB_STEP_COUNT - 1));
                        sl->ready      = bb->completed;
                        sl->onhold     = bb->onhold;
                        /* A factory of this type already busy elsewhere: the engine says
                         * so and the 1995 sidebar draws that slot darkened. */
                        sl->darken     = bb->busy && !bb->constructing;
                    }
                g_state.radar_active    = g_sb.radar_active;
                g_state.power_total     = g_sb.power_produced;
                g_state.power_drain     = g_sb.power_drained;
                g_state.repair_disabled = !g_sb.repair_enabled;
                g_state.sell_disabled   = !g_sb.sell_enabled;
                g_credits = g_sb.credits;
            }
            redump = 0;
        }
        tsim += now_s() - tp;
        stage = 2; if (trace > 1) report("  s%d", 2);

        /* ---- input ----
         *
         * No window, so no WM_ messages: a fullscreen Glide app gets its input by asking.
         * GetCursorPos works correctly under fullscreen Glide on this hardware, which was
         * established by an earlier project on this same box, and it reports DESKTOP
         * pixels, so it is scaled into the 640x480 the card is showing. */
        {
            POINT pt;
            int lb, rb;

            /* ABSOLUTE, 1:1, over a centred window of the desktop. NO SetCursorPos.
             *
             * Three attempts, and the reasoning is worth keeping because each failed for
             * a different reason.
             *
             * 1. Scale the desktop into the view. The desktop is 1280x1024, so the
             *    pointer crawled at half speed, and squashing 5:4 into 4:3 skews vertical
             *    against horizontal, so it does not go where the hand sends it.
             *
             * 2. Track motion and recentre the OS cursor each frame, which is what a
             *    fullscreen game normally does. On the desktop SetCursorPos applies
             *    immediately with zero drift, measured over 20,000 polls. Under Glide it
             *    does NOT stick, so every frame contributed a phantom delta and at eighty
             *    frames a second the pointer walked away on its own. Clicking repeatedly
             *    at the desktop centre left it at 537,437 instead of 320,240.
             *
             * 3. This. Take the OS cursor's position and subtract a fixed offset, so one
             *    mouse pixel is one screen pixel and the pointer is a pure function of
             *    where the cursor actually is. Nothing is written back, so nothing can
             *    fight it and nothing can drift. The cost is that only a 640x480 window
             *    of the desktop maps onto the picture, which on a 1280x1024 desktop is
             *    a great deal of desk and no real limit. */
            if (g_scripted) { mx = g_sc.mx; my = g_sc.my; }
            else
            {
                int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
                int ox, oy;
                if (sw < SCR_W) sw = SCR_W;
                if (sh < SCR_H) sh = SCR_H;
                ox = (sw - SCR_W) / 2;
                oy = (sh - SCR_H) / 2;
                GetCursorPos(&pt);
                mx = pt.x - ox;
                my = pt.y - oy;
            }
            if (mx < 0) mx = 0; if (mx >= SCR_W) mx = SCR_W - 1;
            if (my < 0) my = 0; if (my >= SCR_H) my = SCR_H - 1;

            /* THE BUTTON SIGNAL PULSES. HOLD IT.
             *
             * GetAsyncKeyState's high bit is documented as the CURRENT state, and on this
             * machine, polled from a windowless fullscreen Glide app, it does not behave
             * like a level: one physical press produces a train of ones and zeroes. Taken
             * at face value that turns a single click into a dozen. Measured: two presses
             * registered as 13 clicks and 21 selections, which from the outside looks
             * exactly like clicking not working, because every selection is undone by the
             * next spurious click a frame later.
             *
             * So the raw read is treated as evidence that the button is down RIGHT NOW,
             * and the button is considered held until it has been quiet for a while. That
             * is robust whether the signal is a level or a pulse train, where debouncing
             * by counting consecutive equal reads would fail on the pulses. 120 ms is
             * far longer than any gap inside one press and far shorter than a deliberate
             * double click. */
            if (g_scripted)
            {
                /* The script says what the button is doing, with no pulse to defeat, so
                 * the hold below is skipped rather than applied to a clean signal: the
                 * hold delays the release edge by 120 ms and would smear a scripted click
                 * across frames it was never meant to span. */
                lb = g_sc.lb; rb = g_sc.rb;
                if (lb) { downx = mx; downy = my; ++rawlb; }
                if (rb) ++rawrb;
            }
            else
            {
                const double HOLD = 0.120;
                if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
                {
                    ++rawlb;
                    lb_seen = now_s();
                    /* Where the pointer was while the button was ACTUALLY down. The hold
                     * above keeps `lb` true for 120 ms after the last pulse, and a real
                     * hand keeps moving in that window, so using the live pointer at
                     * release time made every click look like a small drag. */
                    downx = mx; downy = my;
                }
                if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) { ++rawrb; rb_seen = now_s(); }
                lb = (now_s() - lb_seen) < HOLD;
                rb = (now_s() - rb_seen) < HOLD;
            }
            g_lbnow = lb;

            /* ESCAPE IS AN EDGE, NOT A LEVEL. A hand holds a key for a fifth of a
             * second, which at 60 FPS is a dozen frames -- so ESC to skip the mission briefing
             * skipped it AND, on the very next frame, quit the game. From the player's
             * side that reads as "GDI 1 cannot be started". One press is one event, and
             * whichever block consumes it says so. */
            escEdge = key_down(VK_ESCAPE) && !wasEsc;
            wasEsc  = key_down(VK_ESCAPE);
            escUsed = 0;

            {   /* scroll and zoom */
                float sc = (float)dt * 12.0f;
                float ax = g_cam.at_x, az = g_cam.at_z;
                if (sweep)
                {
                    /* SWEEP: drive the camera the way a player does when they are trying
                     * to break something. Round the map to every clamp limit while the
                     * zoom walks its whole range, which is the pattern that wedged the
                     * card. No human, so it is a regression test rather than an anecdote. */
                    float ph = (float)frame * 0.035f;
                    ax = map.cellx + map.cellw * 0.5f + (float)cos(ph) * map.cellw;
                    az = map.celly + map.cellh * 0.5f + (float)sin(ph * 0.7f) * map.cellh;
                    t1_cam_set_dist(&g_cam,
                        T1_DIST_MIN + (T1_DIST_MAX - T1_DIST_MIN)
                                      * (0.5f + 0.5f * (float)sin(ph * 0.31f)));
                }
                int pu = (key_down(VK_PRIOR)) ? 1 : 0;
                int pd = (key_down(VK_NEXT)) ? 1 : 0;
                if (key_down(VK_LEFT)) ax -= sc;
                if (key_down(VK_RIGHT)) ax += sc;
                if (key_down(VK_UP)) az -= sc;
                if (key_down(VK_DOWN)) az += sc;

                /* ---- EDGE SCROLL, which every RTS has and this did not ---------------
                 *
                 * The pointer within a band of a screen edge scrolls the map, and the
                 * corners scroll both ways at once because the two tests are independent
                 * rather than a chain of else-ifs. The band is generous at 24 pixels: a
                 * fullscreen Glide app has no window edge to catch the pointer against,
                 * so a thin band is a band the player keeps missing.
                 *
                 * Never while the pointer is over the sidebar, or the map would run away
                 * every time somebody reached for a build cameo. */
                if (!in_hud(mx, my) && !g_inmenu)
                {
                    const int BAND = 24;
                    if (mx < BAND)                ax -= sc;
                    if (mx > SCR_W - 1 - BAND)    ax += sc;
                    if (my < BAND)                az -= sc;
                    if (my > SCR_H - 1 - BAND)    az += sc;
                    if (mx < BAND || mx > SCR_W - 1 - BAND
                     || my < BAND || my > SCR_H - 1 - BAND) ++nedge;
                }

                /* ---- MIDDLE-BUTTON DRAG ---------------------------------------------
                 *
                 * Hold the middle button and the map follows the pointer, one screen
                 * pixel to one screen pixel. The conversion is the camera's own inverse,
                 * not a tuned pixels-per-cell number: the ground point under the pointer
                 * when the drag began has to stay under the pointer, so the delta is the
                 * difference between two ground picks and is exact at every zoom.
                 *
                 * GetAsyncKeyState is what the left and right buttons already use on this
                 * machine; the middle one behaves the same way. It goes through key_down
                 * so a script can hold it too -- `key MBUTTON 1` -- while rawmb keeps
                 * counting the real hardware button underneath, which is the number that
                 * says a physical middle button reached the process at all. */
                {
                    int mb = key_down(VK_MBUTTON);
                    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ++rawmb;
                    if (mb && !in_hud(mx, my) && !g_inmenu)
                    {
                        float gx, gz;
                        t1_cursor_ground(&g_terr, &g_cam, &g_scr, (float)mx, (float)my,
                                         &gx, &gz);
                        if (dragging)
                        {
                            /* Drag the WORLD under the pointer, so the map moves with the
                             * hand rather than against it. */
                            ax -= gx - dragx;
                            az -= gz - dragz;
                            ++ndrag;
                        }
                        else { dragx = gx; dragz = gz; dragging = 1; }
                    }
                    else dragging = 0;
                }
                if (pu && !wasPU) t1_cam_set_dist(&g_cam, g_cam.dist_lep + T1_DIST_STEP);
                if (pd && !wasPD) t1_cam_set_dist(&g_cam, g_cam.dist_lep - T1_DIST_STEP);
                wasPU = pu; wasPD = pd;
                if (freecam)
                {
                    if (ax < 1.0f) ax = 1.0f;
                    if (ax > 62.0f) ax = 62.0f;
                    if (az < 1.0f) az = 1.0f;
                    if (az > 62.0f) az = 62.0f;
                }
                else
                {
                    const float m = 5.0f;
                    float lox = map.cellx + m, hix = map.cellx + map.cellw - m;
                    float loz = map.celly + m, hiz = map.celly + map.cellh - m;
                    if (hix < lox) lox = hix = map.cellx + map.cellw * 0.5f;
                    if (hiz < loz) loz = hiz = map.celly + map.cellh * 0.5f;
                    if (ax < lox) ax = lox; if (ax > hix) ax = hix;
                    if (az < loz) az = loz; if (az > hiz) az = hiz;
                }
                /* A scripted `cam x z` overrides the scroll for that frame, so a test
                 * can put the camera exactly where the thing it wants to look at is
                 * rather than counting on a key hold and a frame rate. */
                if (g_scripted && g_sc.camset)
                { ax = (float)g_sc.camx; az = (float)g_sc.camz; }
                t1_cam_look_at(&g_cam, ax, az, t1_terrain_corner_y(&g_terr, (int)ax, (int)az));
            }

            /* D deploys. In the 1995 game you deploy an MCV by clicking it while it is
             * selected, which means aiming at a 24 pixel sprite; this issues the same
             * engine command at the selected object's OWN cell, so it cannot miss. The
             * engine decides what "the default action here" means, and for an MCV on its
             * own cell that is deploy. */
            {
                int d = (key_down('D')) ? 1 : 0;
                if (d) ++rawd;
                if (d && !wasD)
                {
                    for (i = 0; i < nobj; ++i)
                        if (g_objs[i].sel && !g_objs[i].limbo)
                        {
                            wb_command_at_cell(g_objs[i].clx / 256, g_objs[i].cly / 256, 0);
                            ++ndeploy;
                            redump = 1;
                            break;
                        }
                }
                wasD = d;
            }

            /* ---- the NEW HUD: hover, then clicks ----
             *
             * The hit test is t1_hud_hit, which is a direct port of sb_hit_h6() in
             * game/cnc_sidebar.h reading the same generated layout, so the desktop build
             * and this one cannot disagree about where a control is. Hover is refreshed
             * every frame off the same pointer the click path reads, which is what stops
             * what lights up and what a click does from drifting apart. */
            if (g_hudok)
            {
                t1_hud_hover(&g_hud, mx, my, lb);
                if (in_hud(mx, my) && lb && !wasLB)
                {
                    int col = -1, slot = -1;
                    switch (t1_hud_hit(mx, my, &col, &slot))
                    {
                    case T1H_ITEM:
                    {
                        int idx = (col >= 0) ? g_hud.top[col] + slot : -1;
                        if (col >= 0 && idx >= 0 && idx < g_sb.count[col])
                        {
                            W98_Build *bb = &g_sb.item[col][idx];
                            /* A finished item is PLACED, not started again. Everything
                             * else is a start and the engine decides if it is legal. */
                            if (bb->completed)
                            { wb_place_begin(bb->btype, bb->bid); g_placing = 1;
                              g_place_t = bb->btype; g_place_i = bb->bid;
                              t1_place_begin(&g_place, &g_mesh, bb); }
                            else wb_build_start(bb->btype, bb->bid);
                            ++nbuild; redump = 1;
                        }
                        break;
                    }
                    case T1H_REPAIR: wb_repair_click(); ++nbuild; redump = 1; break;
                    case T1H_SELL:   g_sellmode = !g_sellmode; break;
                    case T1H_MAP:    break;      /* radar is always on when powered */
                    case T1H_UP:     t1_hud_scroll(&g_hud, col, -1); break;
                    case T1H_DOWN:   t1_hud_scroll(&g_hud, col, +1); break;
                    case T1H_RADAR:
                    {
                        /* Click the minimap, go there. The 1995 radar does this and it is
                         * the only way to cross a 64 cell map quickly with no edge scroll. */
                        int rcx, rcy;
                        if (t1_hud_radar_to_cell(&g_hud, &map, mx, my, &rcx, &rcy))
                        {
                            t1_cam_look_at(&g_cam, (float)rcx, (float)rcy,
                                           t1_terrain_corner_y(&g_terr, rcx, rcy));
                            ++nradar;
                        }
                        break;
                    }
                    default: break;
                    }
                }
            }

            /* ---- a click in the 1995 SIDEBAR column (the fallback bar only) ----
             *
             * The strip's geometry is the 1995 engine's, quoted in game/dosbar.h: two
             * columns at DOS x 248 and 283, rows 24 apart from y 92, cameos 32 by 24.
             * The sidebar is drawn as one quad from DOS x 240..319 into screen
             * 480..639, doubled, so the inverse is exact and needs no fudge factor. */
            if (!g_hudok && mx >= SCR_W - 160 && lb && !wasLB)
            {
                int dx = (mx - (SCR_W - 160)) / 2 + DB_SIDE_X;
                int dy = my / 2;
                int col = -1, row;
                if (dx >= DB_COLUMN_ONE_X && dx < DB_COLUMN_ONE_X + DB_OBJECT_WIDTH) col = 0;
                else if (dx >= DB_COLUMN_TWO_X && dx < DB_COLUMN_TWO_X + DB_OBJECT_WIDTH) col = 1;
                row = (dy - DB_COLUMN_Y) / DB_OBJECT_HEIGHT;
                if (col >= 0 && dy >= DB_COLUMN_Y && row >= 0 && row < DB_MAX_VISIBLE
                    && row < g_sb.count[col])
                {
                    W98_Build *bb = &g_sb.item[col][row];
                    /* A finished item is placed, not started again. Everything else is a
                     * start, and the engine decides whether it is legal. */
                    if (bb->completed) { wb_place_begin(bb->btype, bb->bid); g_placing = 1;
                                         g_place_t = bb->btype; g_place_i = bb->bid;
                                         t1_place_begin(&g_place, &g_mesh, bb); }
                    else wb_build_start(bb->btype, bb->bid);
                    ++nbuild;
                    redump = 1;
                }
                else if (dy >= DB_REPAIR_Y && dy < DB_REPAIR_Y + 8
                         && dx >= DB_REPAIR_X && dx < DB_REPAIR_X + DB_REPAIR_W)
                { wb_repair_click(); ++nbuild; redump = 1; }
            }

            /* ---- the map: press starts, release decides ----
             *
             * A press only remembers where it began. The RELEASE chooses: if the pointer
             * moved more than a few pixels it was a band and everything of the player's
             * inside the rectangle is selected, otherwise it was a click and the nearest
             * single object wins. That is how the 1995 game behaves and it is also the
             * only way a drag can exist at all.
             *
             * The band is tested in SCREEN space against the drawn rectangle, not in cell
             * space. The two enclose different sets of units, and the player is aiming at
             * the screen. */
            if (!in_hud(mx, my) && lb && !wasLB)
            { bandx = mx; bandy = my; banding = 0; bandactive = 1; }

            if (lb && bandactive)
            {
                /* Measured against the last position the button was SEEN down at, not
                 * against the live pointer, so drift after the finger lifts cannot turn
                 * a click into a drag. Six pixels rather than four for the same reason. */
                int adx = downx - bandx, ady = downy - bandy;
                if (adx < 0) adx = -adx;
                if (ady < 0) ady = -ady;
                if (adx > 6 || ady > 6) banding = 1;
            }

            if (!lb && wasLB && bandactive)
            {
                /* Everything below uses the position the button was last seen down at,
                 * which is where the player actually clicked, not where the pointer has
                 * wandered to since. */
                int px2 = downx, py2 = downy;
                int bx0 = bandx < px2 ? bandx : px2, bx1 = bandx < px2 ? px2 : bandx;
                int by0 = bandy < py2 ? bandy : py2, by1 = bandy < py2 ? py2 : bandy;
                float wx, wz;
                /* THE HEIGHTFIELD, not a plane. See t1_cursor_ground: over a hill the
                 * two answers differ by whole cells, so the cursor pointed at one and the
                 * order went to another. */
                t1_cursor_ground(&g_terr, &g_cam, &g_scr, (float)px2, (float)py2, &wx, &wz);

                if (g_placing)
                {
                    /* THE ENGINE WANTS GRID COORDINATES, NOT WORLD CELLS. Place() adds
                     * the expanded map rectangle's own origin back on
                     * (dllinterface.cpp: cell = map_cell_x + cell_x ...), so an absolute
                     * cell lands tens of cells away or is refused outright with no
                     * diagnostic. This is why placements never worked on this build. */
                    int gx = 0, gy = 0;
                    t1_place_update(&g_place, wx, wz);
                    t1_place_grid(&g_place, &gx, &gy);
                    if (g_place.legal)
                    {
                        wb_place_at(g_place_t, g_place_i, gx, gy);
                        ++nplace;
                    }
                    else ++nplacebad;
                    g_placing = 0;
                    t1_place_end(&g_place);
                }
                else
                {
                    int best = -1, took = 0;
                    float bestd = 26.0f * 26.0f;
                    wb_clear_selection();
                    for (i = 0; i < nobj; ++i)
                    {
                        float ox, oz, oy, sx2, sy2, ddx, ddy, d;
                        SR_Vertex pv;
                        const char *k;
                        int ty;
                        if (g_objs[i].limbo) continue;
                        k = g_objs[i].kind;
                        if (strcmp(k, "TERRAIN") == 0) continue;
                        ox = g_objs[i].clx / 256.0f;
                        oz = g_objs[i].cly / 256.0f;
                        oy = t1_terrain_corner_y(&g_terr, (int)ox, (int)oz);
                        t1_world_to_eye(&g_cam, ox, oy, oz, &pv);
                        if (!(pv.w > 0.25f)) continue;
                        sx2 = g_scr.cx + pv.x * g_scr.fx / pv.w;
                        sy2 = g_scr.cy - pv.y * g_scr.fy / pv.w;
                        ty = (strcmp(k, "INFANTRY") == 0) ? WB_INFANTRY
                           : (strcmp(k, "UNIT") == 0)     ? WB_UNIT
                           : (strcmp(k, "AIRCRAFT") == 0) ? WB_AIRCRAFT
                           : (strcmp(k, "BUILDING") == 0) ? WB_BUILDING : 0;
                        if (!ty) continue;

                        if (banding)
                        {
                            /* A band takes only what the player owns, and only mobile
                             * things, which is what the engine will accept orders for. */
                            if (strcmp(g_objs[i].house, "GoodGuy") != 0) continue;
                            if (ty == WB_BUILDING) continue;
                            if (sx2 < (float)bx0 || sx2 > (float)bx1) continue;
                            if (sy2 < (float)by0 || sy2 > (float)by1) continue;
                            if (wb_select(ty, g_objs[i].id)) { ++took; ++nsel; }
                        }
                        else
                        {
                            ddx = sx2 - (float)px2;
                            ddy = sy2 - (float)py2;
                            d = ddx * ddx + ddy * ddy;
                            if (d < bestd) { bestd = d; best = i; }
                        }
                    }
                    if (!banding && best >= 0)
                    {
                        const char *k = g_objs[best].kind;
                        int ty = (strcmp(k, "INFANTRY") == 0) ? WB_INFANTRY
                               : (strcmp(k, "UNIT") == 0)     ? WB_UNIT
                               : (strcmp(k, "AIRCRAFT") == 0) ? WB_AIRCRAFT
                               : (strcmp(k, "BUILDING") == 0) ? WB_BUILDING : 0;
                        if (ty && wb_select(ty, g_objs[best].id)) ++nsel;
                    }
                    ++nclick;
                    (void)took;
                }
                bandactive = 0;
                banding = 0;
                redump = 1;
            }

            /* Right click orders the current selection, on the press. */
            if (!in_hud(mx, my) && rb && !wasRB)
            {
                float wx, wz;
                t1_cursor_ground(&g_terr, &g_cam, &g_scr, (float)mx, (float)my, &wx, &wz);
                wb_command_at_cell((int)wx, (int)wz,
                                   (key_down(VK_CONTROL)) ? 1 : 0);
                ++norder;
                redump = 1;
            }

            wasLB = lb; wasRB = rb;
        }
        stage = 21; if (trace > 1) report("  s21");

        /* ---- A MOVIE IS PLAYING ------------------------------------------------------
         *
         * It owns the whole screen and the whole frame: nothing else draws, the sim does
         * not tick, and any key or click skips it. That is what the 1995 game does with a
         * briefing and it is what makes an unskippable movie a bug rather than a feature.
         */
        if (t1_movie_playing(&g_movie))
        {
            if (!t1_movie_frame(&g_movie, now_s(), g_au ? movie_push : 0))
            {
                report("movie: %s ended at frame %d (%d palette change(s)), "
                       "%ld PCM samples decoded, %ld taken by the mixer",
                       g_movie.name, g_movie.frame, g_movie.palchanges,
                       g_moviepcm, g_moviepcm_taken);
                t1_movie_stop(&g_movie);
            }
            else if (g_lbnow || escEdge || key_down(VK_SPACE)
                     || key_down(VK_RETURN))
            {
                escUsed = escEdge;
                report("movie: %s skipped at frame %d, %ld PCM samples decoded, "
                       "%ld taken by the mixer",
                       g_movie.name, g_movie.frame, g_moviepcm, g_moviepcm_taken);
                t1_movie_stop(&g_movie);
            }
            if (g_scripted && g_sc.shot[0])
            {
                t1_glide_readback(g_read, SCR_W, SCR_H);
                save_bmp(g_sc.shot);
                report("shot %s at frame %d (movie)", g_sc.shot, frame);
            }
            /* THE DEVICE STILL HAS TO BE FED. This block skips the rest of the frame,
             * and the rest of the frame is where the mixer is pumped -- so the first
             * version of it played a silent movie and starved the music under the menu
             * too. audio_frame does file I/O and must stay on this thread. */
            if (g_au)
            {
                cnc_audio_update(g_au);
                audio_frame(g_au, (int)(dt * 1000.0 + 0.5));
            }
            if (g_scripted && g_sc.quit) { ++frame; break; }
            continue;
        }

        /* ---- THE MAIN MENU ----------------------------------------------------------
         *
         * The game used to boot straight into SCG01EA with no title screen, no way to
         * choose anything and no way out but closing the process. menu/dosmenu.c draws
         * the 1995 menu into an 8-bit 320x200 surface and hit-tests it, exactly as
         * game/dosbar.c draws the sidebar, and is compiled UNEDITED like it. This block
         * is the presentation and the input, and nothing else: while it is up the sim is
         * not ticked and no world pass runs.
         *
         * The DOS mouse pointer over it is the HUD's own aux page, which is true colour
         * and so survives the menu owning the card's palette. */
        if (g_inmenu && g_menuok)
        {
            int pick, kd = 0;
            int mlb = g_lbnow;
            if (key_down(VK_UP) && !wasUp)     kd = -1;
            if (key_down(VK_DOWN) && !wasDown) kd = +1;
            wasUp = key_down(VK_UP);
            wasDown = key_down(VK_DOWN);
            pick = t1_menu_step(&g_menu, mx, my, mlb, wasMenuLB, kd,
                                key_down(VK_RETURN) && !wasEnter);
            wasEnter = key_down(VK_RETURN);
            wasMenuLB = mlb;

            t1_glide_begin(0x00000000);
            t1_menu_draw(&g_menu, mx, my);
            if (g_hudok) t1_hud_cursor(&g_hud, mx, my);
            t1_glide_end();

            if (g_scripted && g_sc.shot[0])
            {
                t1_glide_readback(g_read, SCR_W, SCR_H);
                save_bmp(g_sc.shot);
                report("shot %s at frame %d (menu)", g_sc.shot, frame);
            }
            /* TEST MAP and START NEW GAME both mean "play the one scenario this build
             * carries". Load, Multiplayer and Intro are drawn DISABLED by the module
             * itself and cannot be picked; Exit leaves. Everything the menu cannot yet
             * do is a grey slab on screen rather than a live button that does nothing,
             * which is the 1995 dialog's own way of saying it. */
            if (pick == DM_EXIT) { report("menu: exit"); ++frame; break; }
            if (pick == DM_TESTMAP || pick == DM_START)
            {
                g_inmenu = 0;
                report("menu: %s -> %s", dm_item_label(pick), g_scen);
                /* Act on Instinct, now that there is a mission for it to open. */
                if (g_au)
                {
                    int aoi = cnc_music_theme_index("AOI");
                    if (aoi >= 0) cnc_music_play_index(g_au, aoi);
                    report("music: %s (the mission)",
                           cnc_music_current(g_au) ? cnc_music_current(g_au) : "(none)");
                }
                /* THE MISSION BRIEFING. GDI mission 1's own movie, out of the 1995 CD, played
                 * once before the mission. A missing file is never fatal: the game just
                 * starts. */
                if (g_movieok && !g_briefed)
                {
                    char mp[MAX_PATH];
                    _snprintf(mp, sizeof mp, "%sdosdata\\movies\\GDI1.VQA", g_base);
                    g_briefed = 1;
                    if (t1_movie_play(&g_movie, mp, now_s(), err, sizeof err))
                        report("movie: GDI1.VQA %dx%d at %d fps",
                               g_movie.w, g_movie.h, g_movie.fps);
                    else report("movie: %s", err);
                }
            }
            /* The console's own "Intro & Sneak Peek": the Westwood logo reel. */
            if (pick == DM_INTRO && g_movieok)
            {
                char mp[MAX_PATH];
                _snprintf(mp, sizeof mp, "%sdosdata\\movies\\LOGO.VQA", g_base);
                if (t1_movie_play(&g_movie, mp, now_s(), err, sizeof err))
                    report("movie: LOGO.VQA %dx%d at %d fps",
                           g_movie.w, g_movie.h, g_movie.fps);
                else report("movie: %s", err);
            }
            if (g_au)
            {
                cnc_audio_update(g_au);
                audio_frame(g_au, (int)(dt * 1000.0 + 0.5));
            }
            if ((escEdge && !escUsed) || (g_scripted && g_sc.quit))
            { report("menu: escape"); ++frame; break; }
            continue;
        }

        /* THE WHOLE SCREEN GOES BLACK, sidebar included, which is what the console
         * does and is not what the DOS original did. Nothing else is drawn: no terrain,
         * no units, no HUD. */
        if (g_over && g_vrdok)
        {
            t1_glide_begin(0x00000000);
            t1_glide_depth(0);
            t1_glide_ckey(0, 0);
            t1_glide_filter(0);
            {
                /* 200x53 at 2x, centred horizontally, at the console's own y = 99. */
                float bw = (float)(g_vrduw * 2), bh = (float)(g_vrduh * 2);
                float bx = ((float)SCR_W - bw) * 0.5f, by = 198.0f;
                t1_glide_quad(bx, by, bx + bw, by + bh,
                              0.0f, 0.0f, (float)g_vrduw, (float)g_vrduh,
                              &g_vrd[g_won ? 0 : 1], 1.0f);
            }
            t1_glide_depth(1);
            t1_glide_end();
            if (g_scripted && g_sc.shot[0])
            {
                t1_glide_readback(g_read, SCR_W, SCR_H);
                save_bmp(g_sc.shot);
                report("shot %s at frame %d (verdict)", g_sc.shot, frame);
            }
            if ((escEdge && !escUsed) || (g_scripted && g_sc.quit)) { ++frame; break; }
            continue;
        }

        t1_glide_begin(0x00202830);
        stage = 22; if (trace > 1) report("  s22");

        tp = now_s();
        t1_glide_palette(g_terr.pal);
        stage = 23; if (trace > 1) report("  s23");
        /* The PLAYABLE RECT plus a margin, not a hardcoded patch. Getting this wrong is
         * silent: the frame rate looks wonderful because most of the map is never drawn. */
        if (!noterr)
        /* ---- WHAT THE CAMERA CAN ACTUALLY SEE ---------------------------------------
         *
         * The ground and the veil used to be drawn over the whole PLAYABLE RECTANGLE,
         * every frame, whatever the camera was looking at. On GDI mission 1 that is 700
         * cells and the failure was missed; the first map with a full-sized playable area is
         * 62 x 50, which is 3,100 -- and the frame went from 19 ms to 44 ms, almost all
         * of it terrain and shroud, for cells that are nowhere near the screen.
         *
         * The camera can see about twenty cells across at the widest zoom, so this is a
         * seven-fold cut on a big map and still worth having on a small one. The bound is
         * conservative by construction rather than by a fudge factor: the four screen
         * corners are intersected with the ground plane at the terrain's own HIGHEST and
         * LOWEST points, which brackets every height the ray can cross, and the union of
         * those eight points is the box. A hill cannot poke in from outside it. */
        {
            int vx0, vz0, vx1, vz1;
            visible_cells(&g_cam, &g_scr, &g_terr, &vx0, &vz0, &vx1, &vz1);
            if (!freecam)
            {
                /* Never outside the playable rect: the engine culls there itself and the
                 * art beyond it is not meant to be seen. */
                if (vx0 < map.cellx - 1) vx0 = map.cellx - 1;
                if (vz0 < map.celly - 1) vz0 = map.celly - 1;
                if (vx1 > map.cellx + map.cellw + 1) vx1 = map.cellx + map.cellw + 1;
                if (vz1 > map.celly + map.cellh + 1) vz1 = map.celly + map.cellh + 1;
            }
            if (nocull)
            {   /* The old behaviour, for the A/B: the whole playable rect every frame. */
                vx0 = map.cellx - 1; vz0 = map.celly - 1;
                vx1 = map.cellx + map.cellw; vz1 = map.celly + map.cellh;
            }
            g_viewx = vx0; g_viewz = vz0;
            g_vieww = vx1 - vx0 + 1; g_viewh = vz1 - vz0 + 1;
            if (g_vieww < 0) g_vieww = 0;
            if (g_viewh < 0) g_viewh = 0;
        }
        terrpx += (double)t1_terrain_draw(&g_terr, 0, &g_cam, &g_scr,
                                  g_viewx, g_viewz, g_vieww, g_viewh);
        /* THE SHROUD, immediately after the ground and before anything stands on it. It
         * is drawn on the terrain's own corners with the terrain's own diagonal, so it is
         * watertight against the hills by construction rather than by a depth bias. */
        stage = 3; if (trace > 1) report("  s%d", 3);
        if (g_hudok && g_viscells && !noshroud)
            shroudpx += (double)t1_shroud_draw(&g_terr, &g_cam, &g_scr, g_vis,
                                       &g_hud.texC,
                                       (float)(T1H_DOT_X + 2), (float)(T1H_DOT_Y + 2),
                                       g_viewx, g_viewz, g_vieww, g_viewh);
        tterr += now_s() - tp;
        stage = 4; if (trace > 1) report("  s%d", 4);

        tp = now_s();
        /* Tiberium goes down after the ground and the veil and BEFORE anything stands on
         * it, so a harvester drives over the crystals rather than under them. */
        /* Smudges go down FIRST: the ground is scorched, then the tiberium grows over
         * it. That is also the 1995 order and it is why a crater under a tiberium field
         * disappears rather than showing through. */
        if (g_smdok)
            tibpx += (double)t1_tib_draw_list(&g_smd, &g_terr, &g_cam, &g_scr,
                                              (const W98_Tib *)wb_overlays()->smudge,
                                              wb_overlays()->nsmudge,
                                              g_viscells ? cell_shows_building : 0);
        if (g_tibok)
            tibpx += (double)t1_tib_draw(&g_tib, &g_terr, &g_cam, &g_scr, wb_overlays(),
                                         g_viscells ? cell_shows_building : 0);
        t1_glide_palette(g_mesh.pal);
        t1_glide_ckey(1, 0x00FF00FF);        /* models: magenta index 0 is a hole */
        t1_glide_filter(0);                  /* point sampled, or the filter blends INTO
                                              * the key colour and the cutouts fringe */
        for (i = 0; i < nobj && !noobj; ++i)
        {
            float wx, wz, wy;
            if (g_objs[i].limbo || g_objmesh[i] < 0) continue;
            /* The cartridge's own selector: HOUSE_GOOD takes the sand table, everyone
             * else the blue-grey one. Set before the draw, read inside it. */
            g_mesh.house = !nohouse && (strcmp(g_objs[i].house, "GoodGuy") == 0);
            /* The blades turn off the ENGINE's tick, never the wall clock, so a scripted
             * screenshot of the same frame is the same picture every time. Roughly three
             * and a half turns a second at the engine's 15 Hz, which is what the console
             * reads like; the cartridge's own rate is not recovered yet. */
            g_mesh.rotor = (ticks * 60) & 255;
            if (!in_view(g_objs[i].clx / 256, g_objs[i].cly / 256, 4)) continue;
            if (g_viscells && !cell_shows(g_objs[i].clx / 256, g_objs[i].cly / 256,
                                          strcmp(g_objs[i].kind, "BUILDING") == 0
                                       || strcmp(g_objs[i].kind, "TERRAIN") == 0))
            { ++hidden; continue; }
            wx = g_objs[i].clx / 256.0f;
            wz = g_objs[i].cly / 256.0f;
            wy = t1_terrain_corner_y(&g_terr, (int)wx, (int)wz);
            {
                T1_MeshParams mp;
                float rigT = -1.0f;
                int rig;
                memset(&mp, 0, sizeof mp);
                mp.mesh   = g_objmesh[i];
                mp.wx = wx; mp.wy = wy; mp.wz = wz;
                mp.facing = draw_facing(&g_objs[i], g_objmesh[i]);
                /* Only things that DRIVE lean. A building does not rock and a wall does
                 * not roll; boats get the ship field, which is roll only. */
                if (strcmp(g_objs[i].kind, "UNIT") == 0)
                    mp.wobble = (strcmp(g_objs[i].name, "BOAT") == 0
                              || strcmp(g_objs[i].name, "LST") == 0)
                              ? T1_WOBBLE_SHIP : T1_WOBBLE_VEHICLE;
                mp.tdelta = turret_delta(&g_objs[i], g_objmesh[i]);
                /* A TURRETED BUILDING'S `face` IS ITS TURRET, NOT ITS HULL: the
                 * engine exports tface only for RTTI_UNIT and RTTI_AIRCRAFT, so every
                 * building reports -1, and building.cpp:548 settles the rest -- for a
                 * building with IsTurretEquipped the draw picks its shape from
                 * PrimaryFacing, so `face` is where the gun is pointing. The base stands
                 * still (draw_facing returns 0 for it) and turret_delta carries the aim.
                 * The role gate is what keeps that from spinning a barracks. */
                if (strcmp(g_objs[i].kind, "BUILDING") == 0)
                {
                    int bs = g_objs[i].doing;
                    if (bs >= 0 && bs < 8) ++g_bstate_seen[bs];
                    if (g_objs[i].makecnt > g_makecnt_max) g_makecnt_max = g_objs[i].makecnt;
                    if (g_objs[i].dostage > g_dostage_max) g_dostage_max = g_objs[i].dostage;
                    /* THE REFINERY'S OWN STAGE. Its second model is selected purely from
                     * this (the cartridge's arm at RAM 0x8003DFD8 branches on 6/12/19/24/
                     * 29/30), so a rig that never fires and a stage that never leaves the
                     * idle band look identical from outside. */
                    if (!strcmp(g_objs[i].name, "PROC") && g_objs[i].dostage >= 0
                        && g_objs[i].dostage < 64)
                        ++g_procstage[g_objs[i].dostage];
                    /* Every distinct (type, stage) seen while the engine says a building
                     * is CONSTRUCTING, so the buildup can be driven off a measurement
                     * rather than off an assumption about what advances. */
                    if (bs == 0 && g_nbsam < 24)
                    {
                        int q, dup = 0;
                        for (q = 0; q < g_nbsam; ++q)
                            if (!strcmp(g_bsam[q].name, g_objs[i].name)
                                && g_bsam[q].stage == g_objs[i].dostage) { dup = 1; break; }
                        if (!dup)
                        {
                            strncpy(g_bsam[g_nbsam].name, g_objs[i].name, 11);
                            g_bsam[g_nbsam].name[11] = 0;
                            g_bsam[g_nbsam].stage = g_objs[i].dostage;
                            g_bsam[g_nbsam].mk    = g_objs[i].makecnt;
                            g_bsam[g_nbsam].tick  = (int)ticks;
                            ++g_nbsam;
                        }
                    }
                }
                mp.animT      = noanim ? -1.0f
                              : t1_anim_object(&g_mesh, mp.mesh, &g_objs[i], ticks);
                mp.build_frac = nobuild ? 1.0f : t1_anim_build_frac(&g_objs[i], ticks);

                /* THE MCV DEPLOY RIG REPLACES THE YARD'S OWN MESH while it unfolds, and
                 * that substitution is the cartridge's, not ours: the engine deletes the
                 * MCV and creates the Construction Yard on the SAME tick, and the rig
                 * carries its own MCV hull because the console authors it as a separate
                 * model for exactly this. */
                rig = noanim ? -1 : t1_anim_mcvrig(&g_mesh, &g_objs[i], ticks, &rigT);
                if (rig >= 0)
                { mp.mesh = rig; mp.animT = rigT; mp.build_frac = 1.0f; mp.tdelta = 0; ++rigdraws; }
                else if (mp.build_frac < 1.0f) ++buildups;

                objpx += (double)t1_mesh_draw_p(&g_mesh, 0, &g_cam, &g_scr, &mp);

                /* The refinery's SECOND model, drawn AS WELL AS the building rather than
                 * instead of it: the cartridge issues both commands from one arm. Its own
                 * tracks are flat on every frame, so all of PROC's motion is in here. A
                 * half-built refinery is still assembling and gets neither. */
                if (!noanim && mp.build_frac >= 1.0f)
                {
                    rig = t1_anim_procrig(&g_mesh, &g_objs[i], &rigT);
                    if (rig >= 0)
                    {
                        mp.mesh = rig; mp.animT = rigT; mp.tdelta = 0;
                        objpx += (double)t1_mesh_draw_p(&g_mesh, 0, &g_cam, &g_scr, &mp);
                        ++procdraws;
                    }
                }
            }
        }
        /* Walls and bullets: overlay cells and things in flight, out of the same mesh
         * bank and the same palette the units are drawn with, so they go in this pass. */
        if (!noover)
        {
        g_mesh.house = 0;      /* overlay art is the neutral set on the cartridge */
        overpx += (double)t1_over_draw_walls(&g_mesh, &g_cam, &g_scr, &g_terr,
                                             wb_overlays(),
                                             g_viscells ? cell_shows_building : 0);
        overpx += (double)t1_over_draw_bullets(&g_mesh, &g_cam, &g_scr, &g_terr,
                                               wb_overlays(),
                                               g_viscells ? cell_shows_unit : 0);
        }

        /* ---- THE SHADOWS, one contiguous block because all of them want one state ----
         *
         * The cartridge draws every shadow with a single RDP state: colour from the
         * primitive alone, coverage from the texture's alpha, blended over what is
         * already there with the depth test on and the depth WRITE off. So this is one
         * pass over the object list rather than a shadow folded into the body draw, and
         * it goes after the bodies and the walls and before the infantry sprites, so
         * every man composites over his own shadow rather than under it.
         *
         * The models' own MODE 2 faces are what draw here -- the cartridge's baked
         * shadow geometry, 250 triangles across 63 meshes -- and they carry the same
         * animation frame and the same construction fraction the body used, so an
         * animated node's shadow moves with it and a half-built building casts half a
         * shadow. */
        if (!noshadow)
        {
            t1_glide_shadow_state(1);
            t1_glide_blend(1);
            t1_glide_depth_write(0);
            t1_glide_depth_lequal(1);
            t1_glide_ckey(0, 0);
            t1_glide_clamp(1);
            t1_glide_filter(1);
            t1_glide_palette(g_mesh.pal);
            for (i = 0; i < nobj && !noobj; ++i)
            {
                T1_MeshParams mp;
                float wx, wz, wy;
                if (g_objs[i].limbo || g_objmesh[i] < 0) continue;
                if (!in_view(g_objs[i].clx / 256, g_objs[i].cly / 256, 4)) continue;
                if (g_viscells && !cell_shows(g_objs[i].clx / 256, g_objs[i].cly / 256,
                                              strcmp(g_objs[i].kind, "BUILDING") == 0
                                           || strcmp(g_objs[i].kind, "TERRAIN") == 0))
                    continue;
                wx = g_objs[i].clx / 256.0f;
                wz = g_objs[i].cly / 256.0f;
                wy = t1_terrain_corner_y(&g_terr, (int)wx, (int)wz);
                memset(&mp, 0, sizeof mp);
                mp.mesh = g_objmesh[i];
                /* The global nudge and the z-fight lift, both OURS and both the desktop
                 * build's numbers, so the two agree. */
                mp.wx = wx + 24.0f / 256.0f;
                mp.wz = wz - 24.0f / 256.0f;
                mp.wy = wy + 0.012f;
                mp.facing = draw_facing(&g_objs[i], g_objmesh[i]);
                mp.tdelta = turret_delta(&g_objs[i], g_objmesh[i]);
                mp.animT = noanim ? -1.0f
                         : t1_anim_object(&g_mesh, mp.mesh, &g_objs[i], ticks);
                mp.build_frac = nobuild ? 1.0f : t1_anim_build_frac(&g_objs[i], ticks);
                mp.modemask = T1_MASK_SHADOW;
                shadpx += (double)t1_mesh_draw_p(&g_mesh, 0, &g_cam, &g_scr, &mp);
            }
            /* The wall shadow sets, in the same state: the cartridge's own eleven
             * unrotated pieces for chain-link and wood fencing. */
            if (!noover)
                shadpx += (double)t1_over_draw_wall_shadows(&g_mesh, &g_cam, &g_scr,
                                                            &g_terr, wb_overlays(),
                                                            g_viscells ? cell_shows_building : 0);
            /* And a blob under every soldier, before the sprites so each man composites
             * over his own rather than under it. */
            if (g_infok && !noinf && t1_inf_shadow_ready())
                for (i = 0; i < nobj; ++i)
                {
                    if (g_objs[i].limbo || g_inftype[i] < 0) continue;
                    if (strcmp(g_objs[i].kind, "INFANTRY") != 0) continue;
                    if (!in_view(g_objs[i].lx / 256, g_objs[i].ly / 256, 2)) continue;
                    if (g_viscells && !cell_shows(g_objs[i].lx / 256, g_objs[i].ly / 256, 0))
                        continue;
                    shadpx += (double)t1_inf_shadow_draw(&g_terr, &g_cam, &g_scr,
                                                         g_objs[i].lx / 256.0f,
                                                         g_objs[i].ly / 256.0f);
                }
            t1_glide_clamp(0);
            t1_glide_filter(0);
            t1_glide_depth_lequal(0);
            t1_glide_depth_write(1);
            t1_glide_blend(0);
            t1_glide_shadow_state(0);
            t1_glide_ckey(1, 0x00FF00FF);
        }
        tobj += now_s() - tp;
        stage = 5; if (trace > 1) report("  s%d", 5);

        /* ---- infantry: 2D billboards, which is what the console did ---- */
        tp = now_s();
        if (g_infok && !noinf)
        {
            t1_glide_palette(g_infpal);
            t1_glide_ckey(1, 0x00FF00FF);
            t1_glide_filter(0);
            for (i = 0; i < nobj; ++i)
            {
                const T1_InfStrip *st;
                float wx, wz, wy, sx, sy, w, hpx, wpx, u0, v0, u1, v1;
                int si, fr, house, moving;
                if (g_objs[i].limbo || g_inftype[i] < 0) continue;
                if (strcmp(g_objs[i].kind, "INFANTRY") != 0) continue;
                if (!in_view(g_objs[i].lx / 256, g_objs[i].ly / 256, 2)) continue;
                if (g_viscells && !cell_shows(g_objs[i].lx / 256, g_objs[i].ly / 256, 0))
                { ++hidden; continue; }

                house  = (strcmp(g_objs[i].house, "BadGuy") == 0) ? 1 : 0;
                /* The MISSION NAME is the last resort and not the first. Only one of the
                 * engine's twenty-two mission names is "Move", so testing it drew every
                 * man walking under Attack, Hunt, Area Guard, Capture or Retreat as
                 * standing still while he slid across the ground. t1_inf_pick now reads
                 * the engine's own DoType first and only falls back to this. */
                moving = (strcmp(g_objs[i].mission, "Move") == 0);
                {
                    /* THE STAGE, CARRIED FORWARD TO THIS FRAME. The object dump runs every
                     * eighth rendered frame -- about six samples a second -- against a
                     * counter the engine steps up to fifteen times a second, so more than
                     * half the frames of a rate-1 animation were being aliased away. This
                     * is not interpolation and not a guess: StageClass advances Stage once
                     * every Rate ticks (stage.h:88) and Graphic_Logic runs once per tick
                     * for every non-building (techno.cpp:2043), so the missing stages are
                     * exactly (ticks since the dump) / Rate. */
                    int rate = t1_inf_rate(g_objs[i].doing);
                    int st = g_objs[i].dostage;
                    if (rate > 0) st += (int)((ticks - g_dumptick) / rate);
                    si = t1_inf_pick(&g_inf, g_inftype[i], house, g_objs[i].doing,
                                     st, g_objs[i].face, moving, g_objs[i].dying, &fr);
                    if (g_objs[i].doing >= 0 && g_objs[i].doing < 34)
                        ++g_doseen[g_objs[i].doing];
                    if (si >= 0 && fr >= 0) g_frameseen[fr & 63] = 1;
                }
                if (si < 0) continue;
                st = &g_inf.strip[si];

                /* Lazily uploaded: the pack is 5 MB and the TMU is 4, but one mission
                 * touches only a handful of strips. A refusal here is not fatal, the man
                 * simply does not draw, and the count is reported at the end. */
                if (!st->tex.guploaded)
                {
                    if (!t1_glide_upload((SR_Texture *)&st->tex, err, sizeof err))
                    {
                        ++infskip;
                        _snprintf(infwhy, sizeof infwhy, "%s (%dx%d)",
                                  err, st->tex.w, st->tex.h);
                        continue;
                    }
                    ++infup;
                }

                wx = g_objs[i].lx / 256.0f;
                wz = g_objs[i].ly / 256.0f;
                wy = t1_terrain_corner_y(&g_terr, (int)wx, (int)wz);
                {
                    SR_Vertex v;
                    t1_world_to_eye(&g_cam, wx, wy, wz, &v);
                    if (!(v.w > 0.25f)) continue;
                    sx = g_scr.cx + v.x * g_scr.fx / v.w;
                    sy = g_scr.cy - v.y * g_scr.fy / v.w;
                    w = v.w;
                }
                /* DOS art is 24 pixels to a cell, which is the same scale the terrain
                 * atlas uses, so the sprite goes up at native size with no fudge. */
                hpx = (float)st->fh / 24.0f * g_scr.fy / w;
                wpx = (float)st->fw / 24.0f * g_scr.fx / w;
                if (hpx < 2.0f || hpx > 400.0f) continue;
                t1_inf_frame_uv(st, fr, &u0, &v0, &u1, &v1);
                /* Anchored at the FEET: the man stands on his cell, he does not float
                 * centred on it. */
                t1_glide_quad(sx - wpx * 0.5f, sy - hpx, sx + wpx * 0.5f, sy,
                              u0, v0, u1, v1, &st->tex, 1.0f);
                ++infdrawn;
            }
        }
        /* Effects last in the world pass, over the units they are happening to. */
        if (g_efxok)
        {
            efxpx += (double)t1_efx_draw(&g_efx, &g_terr, &g_cam, &g_scr, wb_overlays(),
                                         g_viscells ? cell_shows_unit : 0,
                                         g_dbrok ? chunk_mesh_draw : 0);
            /* The chunk pass leaves the mesh state behind it; put the sprite state back
             * for whatever comes next. */
            t1_glide_ckey(1, 0x00FF00FF);
        }

        /* ---- the pointer: which cursor, and where it stands on the ground ----
         *
         * Asked once a frame, and the ENGINE is asked only when something it could care
         * about has changed: the probe prints a line and flushes on every call, and the
         * pointer crosses a cell boundary far more often than the answer changes. */
        {
            const W98_Object *hover = 0;
            int hi = -1, shown = 1, cx, cz;
            if (!in_hud(mx, my) && !g_placing)
            {
                hi = pick_at(g_objs, nobj, &g_terr, &g_cam, &g_scr, mx, my);
                if (hi >= 0) hover = &g_objs[hi];
            }
            cx = g_cursor.cellx; cz = g_cursor.cellz;
            if (g_viscells && cx >= 0 && cx < 64 && cz >= 0 && cz < 64)
                shown = cell_shows(cx, cz, 0);
            t1_cursor_update(&g_cursor, &g_terr, &g_cam, &g_scr, mx, my,
                             in_hud(mx, my) || g_placing, nsel,
                             key_down(VK_CONTROL), key_down(VK_MENU),
                             g_sellmode, 0, shown, hover, ticks);
        }

        /* ---- the placement preview, over the world and under the HUD ----
         *
         * Last in the world pass on purpose: it is an answer to a question the player is
         * asking RIGHT NOW ("can it go here"), so nothing in the scene may hide it. */
        if (g_placing && g_hudok)
        {
            float pwx, pwz;
            t1_cursor_ground(&g_terr, &g_cam, &g_scr, (float)mx, (float)my, &pwx, &pwz);
            t1_place_update(&g_place, pwx, pwz);
            if (!g_place.active) g_placing = 0;   /* the engine left placement mode */
            else
                placepx += (double)t1_place_draw(&g_place, &g_mesh, &g_terr, &g_cam, &g_scr,
                                                 &g_hud.texC,
                                                 (float)(T1H_DOT_X + 2), (float)(T1H_DOT_Y + 2));
        }

        /* THE 3D CURSOR IS THE LAST THING IN THE WORLD PASS, which is where the desktop
         * build puts it too. It sets its own Glide state and inherits none: by this point
         * the infantry pass has swapped the TMU palette and the effects pass has turned
         * the chroma key off without putting it back. */
        if (!nocursor)
            curpx += (double)t1_cursor_draw(&g_cursor, &g_mesh, &g_terr, &g_cam, &g_scr, ticks);
        tinf += now_s() - tp;
        stage = 6; if (trace > 1) report("  s%d", 6);

        /* ---- the 2D layer ---- */
        tp = now_s();
        if (g_hudok && !nohud)
        {
            t1_glide_depth(0);
            /* Recompose only if something the bar can SEE has changed. Composing writes
             * 300 KB, converting reads it and writes 150 KB more and the upload sends
             * 288 KB over PCI: on a 534 MHz machine with 119 MB/s that is most of a
             * frame, so doing it unconditionally would halve the frame rate to redraw
             * pixels that did not move. */
            t1_hud_refresh(&g_hud);
            t1_hud_draw(&g_hud);
            draw_brackets(g_objs, nobj, &g_cam, &g_scr, &g_terr);
            if (banding)
            {
                float bx0 = (float)(bandx < downx ? bandx : downx);
                float bx1 = (float)(bandx < downx ? downx : bandx);
                float by0 = (float)(bandy < downy ? bandy : downy);
                float by1 = (float)(bandy < downy ? downy : bandy);
                ui_rect(bx0, by0, bx1, by0 + 1.0f);
                ui_rect(bx0, by1 - 1.0f, bx1, by1);
                ui_rect(bx0, by0, bx0 + 1.0f, by1);
                ui_rect(bx1 - 1.0f, by0, bx1, by1);
            }
            /* THE 2D POINTER IS FOR THE PANEL. When the 3D cursor is standing on the
             * ground it IS the pointer, and drawing a flat arrow on top of it would be
             * two pointers in two places at once. */
            if (!(g_cursor.ready && g_cursor.onmap && !nocursor))
                t1_hud_cursor(&g_hud, mx, my);
            t1_glide_depth(1);
        }
        else if (g_pack && g_sbsr.guploaded)
        {
            memset(g_surf8, DB_TBLACK, sizeof g_surf8);
            db_clip_reset(&g_surf);
            db_draw_sidebar(&g_surf, g_pack, &g_state);
            db_draw_credits_tab(&g_surf, g_pack, g_credits);
            /* A readout inside the sidebar column, so the person holding the mouse can
             * see what the program thinks the mouse is doing. Without it the only way to
             * tell a dead pointer from a dead click from a bad pick is to guess. */
            {
                const DB_Font *fnt = db_font(g_pack, "6POINT");
                if (fnt)
                {
                    unsigned char fp[16];
                    char ln[48];
                    db_font_palette(fp, DB_WHITE, DB_TBLACK);
                    _snprintf(ln, sizeof ln, "%d,%d", mx, my);
                    db_print(&g_surf, fnt, ln, DB_SIDE_X + 2, 186, fp, DB_FONT6_XSPACING);
                    _snprintf(ln, sizeof ln, "L%d R%d S%d", nclick, norder, nsel);
                    db_print(&g_surf, fnt, ln, DB_SIDE_X + 2, 193, fp, DB_FONT6_XSPACING);
                }
            }
            sidebar_to_texture();
            build_cursor(g_sbtex, SB_TEX);
            t1_glide_palette(g_dospal);
            t1_glide_ckey(1, 0x00FF00FF);    /* only index 0 is magenta, and it is the hole */
            t1_glide_filter(0);
            t1_glide_depth(0);
            t1_glide_reupload(&g_sbsr);
            /* 80x200 of DOS pixels, doubled, hard against the right edge. */
            t1_glide_quad((float)(SCR_W - 160), 0.0f, (float)SCR_W, 400.0f,
                          0.0f, 0.0f, 80.0f, 200.0f, &g_sbsr, 1.0f);
            draw_brackets(g_objs, nobj, &g_cam, &g_scr, &g_terr);

            /* ---- the band, while it is being dragged ---- */
            if (banding)
            {
                float bx0 = (float)(bandx < downx ? bandx : downx);
                float bx1 = (float)(bandx < downx ? downx : bandx);
                float by0 = (float)(bandy < downy ? bandy : downy);
                float by1 = (float)(bandy < downy ? downy : bandy);
                ui_rect(bx0, by0, bx1, by0 + 1.0f);
                ui_rect(bx0, by1 - 1.0f, bx1, by1);
                ui_rect(bx0, by0, bx0 + 1.0f, by1);
                ui_rect(bx1 - 1.0f, by0, bx1, by1);
            }

            /* The pointer, last, so nothing covers it. Same texture and same palette as
             * the sidebar it shares a page with. */
            t1_glide_quad((float)mx, (float)my, (float)(mx + 20), (float)(my + 28),
                          (float)CUR_X, (float)CUR_Y,
                          (float)(CUR_X + 10), (float)(CUR_Y + 14), &g_sbsr, 1.0f);
            t1_glide_depth(1);
        }
        tbar += now_s() - tp;
        stage = 7; if (trace > 1) report("  s%d", 7);

        tp = now_s();
        t1_glide_end();
        tswap += now_s() - tp;
        stage = 8; if (trace > 1) report("  s%d", 8);

        /* A scripted screenshot: the front buffer, after the swap, so it is exactly the
         * frame that was shown. This is the only way to SEE this card's output; a Voodoo
         * bypasses the 2D adapter entirely and VNC shows the desktop underneath it. */
        if (g_scripted && g_sc.shot[0])
        {
            t1_glide_readback(g_read, SCR_W, SCR_H);
            save_bmp(g_sc.shot);
            report("shot %s at frame %d", g_sc.shot, frame);
        }
        if (g_scripted && g_sc.quit) { ++frame; break; }

        /* IN THE MISSION, ESCAPE IS "BACK", NOT "QUIT". The 1995 game opens its
         * options dialog here; this build has one screen to go back to, so it goes
         * there, the sim stops ticking, and picking the mission again resumes it
         * exactly where it was. Leaving for good is EXIT GAME on that screen, which is
         * where a player looks for it. */
        if (escEdge && !escUsed)
        {
            g_inmenu = 1;
            escUsed = 1;
            report("escape: back to the main menu at frame %d", frame);
            if (g_au)
            {
                int m1 = cnc_music_theme_index("MAP1");
                if (m1 >= 0) cnc_music_play_index(g_au, m1);
                report("music: %s (back on the menu)",
                       cnc_music_current(g_au) ? cnc_music_current(g_au) : "(none)");
            }
        }
        if (frames > 0 && frame + 1 >= frames) { ++frame; break; }
    }
    {
        double secs = now_s() - t0;
        double per = frame > 0 ? 1000.0 / frame : 0.0;
        t1_glide_readback(g_read, SCR_W, SCR_H);
        t1_glide_close();
        save_bmp("w98glide.bmp");
        report("frames=%d ticks=%d elapsed=%.3fs", frame, ticks, secs);
        report("FPS=%.2f", secs > 0 ? frame / secs : 0.0);
        report("terrain_px_per_frame=%.0f  object_px_per_frame=%.0f",
               frame ? terrpx / frame : 0.0, frame ? objpx / frame : 0.0);
        report("ms_sim=%.2f ms_terrain=%.2f ms_objects=%.2f ms_infantry=%.2f "
               "ms_sidebar=%.2f ms_swap=%.2f",
               tsim * per, tterr * per, tobj * per, tinf * per, tbar * per, tswap * per);
        {   /* the DoTypes the engine actually put on screen, and how many distinct
             * frames of any strip were drawn. A walk cycle that never advances and a walk
             * cycle that is never selected look identical in a screenshot; these two
             * numbers tell them apart without one. */
            static const char *DON[34] = {
                "stand","guard","prone","WALK","fire","liedown","crawl","getup",
                "fireprone","idle1","idle2","onguard","fightready","punch","kick",
                "phit1","phit2","pdeath","khit1","khit2","kdeath","ready","gundeath",
                "expl","expl2","grenade","firedeath","gest1","sal1","gest2","sal2",
                "pullgun","plead","pleaddeath"
            };
            char dl[220];
            int dk, nfr = 0;
            dl[0] = 0;
            for (dk = 0; dk < 34; ++dk)
                if (g_doseen[dk])
                {
                    char one[40];
                    _snprintf(one, sizeof one, "%s=%ld ", DON[dk], g_doseen[dk]);
                    strncat(dl, one, sizeof dl - strlen(dl) - 1);
                }
            for (dk = 0; dk < 64; ++dk) if (g_frameseen[dk]) ++nfr;
            report("infantry_dotypes: %s", dl[0] ? dl : "(none)");
            report("infantry_frames_distinct=%d", nfr);
        }
        report("infantry_drawn=%d strips_uploaded=%d refused=%d",
               infdrawn, infup, infskip);
        if (infskip) report("  infantry refusal: %s", infwhy);
        report("tmu_free=%u bytes", t1_glide_tmu_free());
        report("ms_accounted=%.2f ms_total=%.2f",
               (tsim + tterr + tobj + tinf + tbar + tswap) * per, secs * per);
        report("clicks=%d selected=%d orders=%d  last_mouse=%d,%d",
               nclick, nsel, norder, mx, my);
        report("build_clicks=%d placements=%d refused=%d placing=%d deploys=%d radar_jumps=%d",
               nbuild, nplace, nplacebad, g_placing, ndeploy, nradar);
        report("view_cull: last box %d,%d %dx%d of the %dx%d playable rect (nocull=%d)",
               g_viewx, g_viewz, g_vieww, g_viewh, map.cellw, map.cellh, nocull);
        report("shadow_px_per_frame=%.0f (noshadow=%d)",
               frame ? shadpx / frame : 0.0, noshadow);
        report("place_px_per_frame=%.0f cursor_px_per_frame=%.0f",
               frame ? placepx / frame : 0.0, frame ? curpx / frame : 0.0);
        report("cursor3d: %s state=%d code=%d frame=%d onmap=%d action=%d "
               "probes=%ld frames_drawn=%ld",
               t1_cursor_name(&g_cursor), g_cursor.state, g_cursor.code, g_cursor.frame,
               g_cursor.onmap, g_cursor.action, g_cursor.probes, g_cursor.drawn);
        report("place_preview: item=%s occupy=%d mesh=%d hover=%d,%d legal=%d cells=%ld",
               g_place.item.name[0] ? g_place.item.name : "-", g_place.item.noccupy,
               g_place.mesh, g_place.hoverx, g_place.hovery, g_place.legal,
               g_place.cellsdrawn);
        report("hud=%s sell_mode=%d verdict=%d over=%d won=%d",
               g_hudok ? "hud640" : "dos1995", g_sellmode, g_vrdok, g_over, g_won);
        report("camera: edge_scroll_frames=%ld drag_frames=%ld raw_mbutton=%ld",
               nedge, ndrag, rawmb);
        report("raw_input_frames: lbutton=%ld rbutton=%ld dkey=%ld of %d",
               rawlb, rawrb, rawd, frame);
        report("scripted=%d commands_fired=%d of %d", g_scripted, g_sc.fired, g_sc.n);
        report("sidebar_live: credits=%d power=%d/%d radar=%d buildables=%d+%d",
               g_sb.credits, g_sb.power_drained, g_sb.power_produced,
               g_sb.radar_active, g_sb.count[0], g_sb.count[1]);
        report("shroud: cells=%d px_per_frame=%.0f objects_hidden_per_frame=%.1f",
               g_viscells, frame ? shroudpx / frame : 0.0,
               frame ? (double)hidden / frame : 0.0);
        {
            const W98_Overlays *ov = wb_overlays();
            { int st[5]; t1_over_stats(st);
              report("wall_pass: seen=%d culled=%d nomesh=%d submitted=%d "
                     "shadow_pieces=%d (%d of 5 sets)",
                     st[0], st[1], st[2], st[3], st[4], t1_over_shadow_sets()); }
            {   /* WHICH art reached the screen, not just how much of it. A count alone
                 * cannot tell a run full of explosions from a run full of glows. */
                int k;
                char line[200];
                line[0] = 0;
                for (k = 0; k < g_efx.nseq; ++k)
                {
                    char one[40];
                    if (!g_efx.seqdrawn[k]) continue;
                    _snprintf(one, sizeof one, "%s=%ld ", g_efx.seq[k].name,
                              g_efx.seqdrawn[k]);
                    strncat(line, one, sizeof line - strlen(line) - 1);
                }
                report("effects_by_art: %s", line[0] ? line : "(nothing drew)");
            }
            report("effects: sprites_drawn=%ld px_per_frame=%.0f chunk_px_per_frame=%.0f",
                   g_efx.drawn, frame ? efxpx / frame : 0.0,
                   frame ? g_chunkpx / frame : 0.0);
            report("particles: live=%d/%d peak=%d spawned=%ld evicted=%ld; "
                   "chunks live=%d/%d peak=%d spawned=%ld dropped=%ld",
                   g_efx.npart, T1EFX_MAX_PART, g_efx.peak_part, g_efx.spawned,
                   g_efx.evicted, g_efx.nchunk, T1EFX_MAX_CHUNK, g_efx.peak_chunk,
                   g_efx.chunks_spawned, g_efx.chunk_dropped);
            report("overlay_px_per_frame=%.0f tiberium_px_per_frame=%.0f",
                   frame ? overpx / frame : 0.0, frame ? tibpx / frame : 0.0);
            {   /* The two substitutions and the two PART ROLES, separately. A rotor that
                 * is drawn and never turned reports parts and no turns, which is the one
                 * thing a screenshot of a helicopter cannot tell you. */
                long tsp = 0, rsp = 0;
                t1_mesh_spins(&tsp, &rsp);
                report("anim: mcv_rig=%ld proc_rig=%ld buildups=%ld "
                       "turret_turns=%ld rotor_turns=%ld (noanim=%d nobuild=%d)",
                       rigdraws, procdraws, buildups, tsp, rsp, noanim, nobuild);
            }
            {   /* DOES THE ART POINT WHERE THE THING IS GOING. Not a screenshot's
                 * opinion: the mean angle, in DirType units, between where each unit
                 * MOVED and the facing it was drawn at. 0 is right, 64 is sideways,
                 * 128 is backwards -- which is exactly the pair of numbers this build
                 * was reporting before model_forward existed. */
                long an, asum, amax; const char *nm;
                int q;
                t1_anim_face_audit(&an, &asum, &amax);
                report("facing: samples=%ld mean_err=%.1f max_err=%ld (0=right, "
                       "64=sideways, 128=backwards)",
                       an, an ? (double)asum / (double)an : 0.0, amax);
                for (q = 0; t1_anim_face_audit_type(q, &nm, &an, &asum, &amax); ++q)
                    report("facing:   %-5s n=%-5ld mean=%.1f max=%ld",
                           nm, an, an ? (double)asum / (double)an : 0.0, amax);
            }
            {   /* WHAT THE ENGINE ACTUALLY SAID about every building it reported. A
                 * buildup that never plays and a BState that never reaches CONSTRUCTION
                 * look identical from a screenshot, and the second one is a bridge bug. */
                char bl[200];
                int bk;
                bl[0] = 0;
                for (bk = 0; bk < 8; ++bk)
                    if (g_bstate_seen[bk])
                    {
                        char one[32];
                        _snprintf(one, sizeof one, "bstate%d=%ld ", bk, g_bstate_seen[bk]);
                        strncat(bl, one, sizeof bl - strlen(bl) - 1);
                    }
                report("building_states: %s makecnt_max=%d dostage_max=%d",
                       bl[0] ? bl : "(no buildings)", g_makecnt_max, g_dostage_max);
                bl[0] = 0;
                for (bk = 0; bk < g_nbsam; ++bk)
                {
                    char one[40];
                    _snprintf(one, sizeof one, "%s:s%d/mk%d@t%d ", g_bsam[bk].name,
                              g_bsam[bk].stage, g_bsam[bk].mk, g_bsam[bk].tick);
                    strncat(bl, one, sizeof bl - strlen(bl) - 1);
                }
                report("constructing_samples: %s", bl[0] ? bl : "(none)");
                bl[0] = 0;
                for (bk = 0; bk < 64; ++bk)
                    if (g_procstage[bk])
                    {
                        char one[24];
                        _snprintf(one, sizeof one, "%d:%ld ", bk, g_procstage[bk]);
                        strncat(bl, one, sizeof bl - strlen(bl) - 1);
                    }
                report("refinery_stages: %s (the rig wants 12..28)",
                       bl[0] ? bl : "(no refinery seen)");
            }
            {   /* where they actually are, so a pass that draws nothing can be told
                 * apart from a camera that is not looking at anything. */
                int k;
                char line[200];
                line[0] = 0;
                for (k = 0; k < ov->ntib && k < 10; ++k)
                {
                    char one[24];
                    _snprintf(one, sizeof one, "%d,%d(%d/%d) ", ov->tib[k].cx,
                              ov->tib[k].cy, ov->tib[k].kind, ov->tib[k].stage);
                    strncat(line, one, sizeof line - strlen(line) - 1);
                }
                report("tiberium_cells: %s", line);
                line[0] = 0;
                for (k = 0; k < ov->nwall && k < 10; ++k)
                {
                    char one[24];
                    _snprintf(one, sizeof one, "%d,%d ", ov->wall[k].cx, ov->wall[k].cy);
                    strncat(line, one, sizeof line - strlen(line) - 1);
                }
                report("wall_cells: %s", line);
            }
            report("smudges: %d cells (dropped %d)", ov->nsmudge, ov->smudge_dropped);
            report("overlays: tiberium=%d walls=%d bullets=%d anims=%d "
                   "(dropped %d/%d/%d/%d)",
                   ov->ntib, ov->nwall, ov->nbullet, ov->nanim,
                   ov->tib_dropped, ov->wall_dropped, ov->bullet_dropped, ov->anim_dropped);
        }
        report("guard_rejects=%ld  sweep=%d", t1_glide_rejects(), sweep);
        {
            long a = 0, b = 0, c = 0;
            wb_event_counts(&a, &b, &c);
            report("audio: sfx_events=%ld played=%ld  speech_events=%ld played=%ld  "
                   "gameover=%ld won=%d", a, g_sfxplayed, b, g_speechplayed, c, g_won);
            report("music: playing=%d track=%s", g_au ? cnc_music_playing(g_au) : 0,
                   (g_au && cnc_music_current(g_au)) ? cnc_music_current(g_au) : "(none)");
        }
        report("OK");
    }
    audio_boot_shutdown(g_au);
    g_au = 0;
    wb_close();
    if (g_pack) db_pack_free(g_pack);
    if (g_infok) t1_inf_free(&g_inf);
    if (g_tibok) t1_tib_free(&g_tib);
    if (g_smdok) t1_tib_free(&g_smd);
    if (g_efxok) t1_efx_free(&g_efx);
    t1_mesh_free(&g_mesh);
    t1_terrain_free(&g_terr);
    return 0;
}
