/*
 * w98_game.c -- the real Command & Conquer mission, live, on Windows 98.
 *
 * This is not the prototype (tier1/w98_proto.c), which draws a test scene to measure the
 * rasteriser. This one runs the ACTUAL GAME: it loads the Tiberian Dawn brain, starts a
 * mission extracted from the N64 cartridge, ticks it at the engine's native 15 Hz, and
 * draws what the engine says is there, with the project's own 1995 DOS sidebar composited
 * over it by game/dosbar.c compiled unedited.
 *
 * WHAT IT DOES NOT DRAW YET, so nobody mistakes it for finished: the N64 3D art. The
 * terrain tiles and the models live in the scenario packs and reading those is the next
 * piece of work. Until then the tactical view is an honest top-down readout of the live
 * engine state: every object at its exact lepton position, sized by its real footprint,
 * coloured by its real owner, turned to its real facing, with damage shown. It is the
 * radar view of a real mission rather than a picture of one.
 *
 * That distinction matters. Everything on this screen came out of the simulation on this
 * machine this frame. Nothing is staged.
 *
 * Keys:  ESC quit   F12 write a BMP of the frame   SPACE pause the simulation
 *        TAB cycle what the label shows            +/- change the sim speed
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "w98_gfx.h"
#include "softras.h"
#include "t1_cam.h"
#include "t1_terrain.h"
#include "t1_mesh.h"
#include "w98_brain.h"
#include "dosbar.h"

#define SCR_W 640
#define SCR_H 400
#define SCALE 2                  /* the DOS 320x200 screen, doubled */
#define LEPTONS_PER_CELL 256

/* The tactical view, in framebuffer pixels. The DOS sidebar occupies the right 80
 * columns of a 320 wide screen, so at 2x it owns x 480..639 and the map gets the rest. */
#define VIEW_X0 2
#define VIEW_Y0 22
#define VIEW_X1 478
#define VIEW_Y1 362

static unsigned int  g_pal32[256];       /* the DOS sidebar palette, for the 2D layer */
/* TWO shade tables, one per texture bank. A palette belongs to a bank, not to the
 * program: the terrain quantises far better on its own than it would sharing 256 slots
 * with the models, and swapping between the passes is a single pointer store into
 * softras. 32 KB each is a rounding error on a 511 MB machine. */
static unsigned int  g_shade_terr[SR_SHADES * 256];
static unsigned int  g_shade_obj[SR_SHADES * 256];
static unsigned char g_surf8[DB_SCREEN_W * DB_SCREEN_H];
static W98_Object    g_objs[WB_MAX_OBJECTS];
static int           g_zbuf[SCR_W * SCR_H];
static T1_Terrain    g_terr;
static T1_MeshBank   g_mesh;
static int           g_meshok;
/* Resolved mesh index per object slot. t1_mesh_for_type is a linear scan over 211 type
 * codes, and doing it for 64 objects every frame was 13,500 string compares a frame and
 * measured 8.63 ms of an 95 ms budget. The object list only refreshes at 5 Hz, so the
 * lookup belongs there and not in the draw loop. */
static int           g_objmesh[WB_MAX_OBJECTS];
static T1_Cam        g_cam;
static T1_Screen     g_scr;

/* Project a world point (cells) to a screen pixel. Returns 0 if it is behind the eye. */
static int project(float wx, float wy, float wz, float *sx, float *sy, float *w)
{
    SR_Vertex v;
    t1_world_to_eye(&g_cam, wx, wy, wz, &v);
    if (v.w <= 0.05f) return 0;
    *sx = g_scr.cx + v.x * g_scr.fx / v.w;
    *sy = g_scr.cy - v.y * g_scr.fy / v.w;
    *w  = v.w;
    return 1;
}

/* ------------------------------------------------------------------------- *
 * Small 32-bit primitives. The rasteriser draws triangles; this view is
 * rectangles and lines, so it writes the framebuffer directly.
 * ------------------------------------------------------------------------- */

static void fill(SR_Target *t, int x0, int y0, int x1, int y1, unsigned int c)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > t->w) x1 = t->w;
    if (y1 > t->h) y1 = t->h;
    for (y = y0; y < y1; ++y)
    {
        unsigned int *row = t->px + (long)y * t->w;
        for (x = x0; x < x1; ++x) row[x] = c;
    }
}

static void frame_rect(SR_Target *t, int x0, int y0, int x1, int y1, unsigned int c)
{
    fill(t, x0, y0, x1, y0 + 1, c);
    fill(t, x0, y1 - 1, x1, y1, c);
    fill(t, x0, y0, x0 + 1, y1, c);
    fill(t, x1 - 1, y0, x1, y1, c);
}

static void line(SR_Target *t, int x0, int y0, int x1, int y1, unsigned int c)
{
    int dx = x1 - x0, dy = y1 - y0, steps, i;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    steps = adx > ady ? adx : ady;
    if (steps <= 0) return;
    for (i = 0; i <= steps; ++i)
    {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        if (x >= 0 && x < t->w && y >= 0 && y < t->h) t->px[(long)y * t->w + x] = c;
    }
}

/* ------------------------------------------------------------------------- *
 * House colours.
 *
 * These are the settled ones from the project's own research, quoted rather than
 * invented: GDI draws sand/gold and Nod draws the blue-grey/red table, and neutral
 * genuinely shares Nod's (see engineering notes, "House colours"). Here they are
 * approximated in the DOS palette because the tactical readout is 2D; the 3D path will
 * use the cartridge's own two TLUTs when it draws models.
 * ------------------------------------------------------------------------- */
static unsigned int house_colour(const char *house, int bright)
{
    unsigned int gdi  = bright ? 0x00FFD46AU : 0x00C8A038U;   /* sand / gold   */
    unsigned int nod  = bright ? 0x00FF5A4AU : 0x00C03028U;   /* red           */
    unsigned int neut = bright ? 0x00A8B4C0U : 0x00707C88U;   /* blue-grey     */
    if (strcmp(house, "GoodGuy") == 0) return gdi;
    if (strcmp(house, "BadGuy") == 0)  return nod;
    return neut;
}

static void draw_object(SR_Target *t, const W98_Object *o)
{
    /* Position from the engine's own Center_Coord in LEPTONS (256 per cell), which
     * already includes CenterOffset for the footprint, so nothing here needs a
     * building-size table of its own. Height comes from the terrain under it. */
    float wx = (float)o->clx * (1.0f / 256.0f);
    float wz = (float)o->cly * (1.0f / 256.0f);
    float wy = t1_terrain_corner_y(&g_terr, (int)wx, (int)wz);
    float sx, sy, w;
    int px, py, r;
    int isterrain  = (strcmp(o->kind, "TERRAIN") == 0);
    int isbuilding = (strcmp(o->kind, "BUILDING") == 0);
    unsigned int c;

    if (o->limbo) return;
    if (!project(wx, wy, wz, &sx, &sy, &w)) return;
    px = (int)sx; py = (int)sy;
    if (px < -32 || px > t->w + 32 || py < -32 || py > t->h + 32) return;

    /* Size falls off with depth exactly as the terrain does, so a marker sits on its
     * cell rather than floating at a fixed pixel size. One cell is fx/w pixels across. */
    r = (int)(g_scr.fx / w * (isbuilding ? 0.42f * (float)(o->fw + o->fh) * 0.5f : 0.30f));
    if (r < 2) r = 2;
    if (r > 40) r = 40;

    if (isterrain)
    {
        fill(t, px - r / 2, py - r, px + r / 2, py, 0x00305828U);
        return;
    }

    c = house_colour(o->house, isbuilding);
    fill(t, px - r, py - r, px + r, py + r, c);
    frame_rect(t, px - r, py - r, px + r, py + r, 0x00202020U);

    if (!isbuilding && o->face >= 0)
    {
        double a = (double)o->face * 6.2831853 / 256.0;
        line(t, px, py, px + (int)(sin(a) * r * 2.0), py - (int)(cos(a) * r * 2.0),
             0x00FFFFFFU);
    }

    if (o->maxstrength > 0 && o->strength < o->maxstrength)
    {
        int bw = r * 2, fwpx = bw * o->strength / o->maxstrength;
        fill(t, px - r, py - r - 4, px - r + bw,   py - r - 2, 0x00301010U);
        fill(t, px - r, py - r - 4, px - r + fwpx, py - r - 2,
             (o->strength * 2 > o->maxstrength) ? 0x0040D040U : 0x00E04040U);
    }
}

/* ------------------------------------------------------------------------- */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char err[512], packerr[256], packpath[MAX_PATH], base[MAX_PATH], line[160];
    DB_Pack *pack;
    DB_State state;
    DB_Surface surf;
    SR_Target tgt;
    W98_Frame *fb;
    W98_MapInfo map;
    const DB_Font *font;
    unsigned char fontpal[16];
    char *slash;

    double tprev, acc = 0.0, simhz = 15.0, fps_acc = 0.0, lastdump = -1.0;
    int frames = 0, fps_frames = 0, ticks = 0, nobj = 0, paused = 0;
    long terrpx = 0, objpx = 0;
    int pickx = -1, pickz = -1;
    /* Phase timers. Same discipline as the prototype: the first terrain frame came in
     * at 9.4 FPS and guessing which phase owns that is how afternoons disappear. */
    double ta_clear = 0, ta_terr = 0, ta_objdump = 0, ta_objdraw = 0,
           ta_bar = 0, ta_blit = 0, ta_present = 0, tp;
    double fps = 0.0;
    float ppc;

    (void)inst; (void)prev; (void)cmd; (void)show;

    GetModuleFileNameA(NULL, base, sizeof base - 32);
    slash = strrchr(base, '\\');
    if (slash) slash[1] = 0; else base[0] = 0;

    _snprintf(packpath, sizeof packpath, "%sdossidebar.pack", base);
    pack = db_pack_load(packpath, packerr, sizeof packerr);
    if (!pack)
    {
        _snprintf(err, sizeof err, "Could not load %s\n\n%s", packpath, packerr);
        MessageBoxA(NULL, err, "CNC3D Win98", MB_OK | MB_ICONERROR);
        return 1;
    }
    {
        int i;
        for (i = 0; i < 256; ++i)
            g_pal32[i] = ((unsigned int)pack->pal8[i*3+0] << 16)
                       | ((unsigned int)pack->pal8[i*3+1] <<  8)
                       |  (unsigned int)pack->pal8[i*3+2];
    }
    /* NOTE: the shade table is built from the TERRAIN palette, not this one. There is
     * one global shade table and the 3D path is the only thing that uses it; the 2D
     * blit takes its palette as an argument (g_pal32 below), so the sidebar keeps the
     * DOS colours while the world keeps the cartridge's. Building it from the sidebar
     * palette by mistake draws a perfectly correct heightfield in magenta confetti. */
    font = db_font(pack, "6POINT");
    db_font_palette(fontpal, DB_WHITE, DB_TBLACK);
    db_surface_init(&surf, DB_SCREEN_W, DB_SCREEN_H, g_surf8);

    /* ---- the brain. Everything drawn below comes out of this. ---- */
    if (!wb_open(NULL, err, sizeof err))
    { MessageBoxA(NULL, err, "CNC3D Win98 brain", MB_OK | MB_ICONERROR); return 1; }

    {
        char content[MAX_PATH], missions[MAX_PATH];
        _snprintf(content,  sizeof content,  "%scontent\\",  base);
        _snprintf(missions, sizeof missions, "%smissions\\", base);
        if (!wb_start(content, missions, "SCG01EA", 1, err, sizeof err))
        { MessageBoxA(NULL, err, "CNC3D Win98 brain", MB_OK | MB_ICONERROR); return 1; }
    }
    memset(&map, 0, sizeof map);
    wb_map(&map);
    if (map.cellw < 1) map.cellw = 64;
    if (map.cellh < 1) map.cellh = 64;

    /* ---- the terrain, converted from the scenario pack by tools/win98/mkterrain.py ---- */
    {
        char tp[MAX_PATH];
        _snprintf(tp, sizeof tp, "%sSCG01EA.t1terr", base);
        if (!t1_terrain_load(&g_terr, tp, err, sizeof err))
        {
            char m[700];
            _snprintf(m, sizeof m,
                      "%s\n\nMake it on the Mac with:\n"
                      "  tools/win98/mkterrain.py <SCEN.pack> SCG01EA.t1terr\n"
                      "and copy it in beside the exe.", err);
            MessageBoxA(NULL, m, "CNC3D Win98 terrain", MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    sr_build_shade(g_terr.pal, g_shade_terr);

    /* The model bank. It is per SIDE, not per mission, because the mesh and texture
     * banks are byte-identical across every shipped pack of the same side. Missing it is
     * not fatal: the game falls back to the flat markers and says so on the plate. */
    {
        char mp[MAX_PATH];
        _snprintf(mp, sizeof mp, "%sgdi.t1mesh", base);
        g_meshok = t1_mesh_load(&g_mesh, mp, err, sizeof err);
        if (g_meshok) sr_build_shade(g_mesh.pal, g_shade_obj);
    }

    /* The camera looks at the centre of the playable rect. The scenario's own VIEW| line
     * is the console's starting position but the brain does not report it, so the middle
     * of the map is the honest stand-in until it does. */
    t1_cam_set_dist(&g_cam, T1_DIST_DEF);
    t1_screen_params(&g_scr, SCR_W, SCR_H);
    /* Open on the player's own force rather than the middle of the map. The scenario's
     * VIEW| line is the console's real answer and the brain does not report it, so the
     * centroid of everything GoodGuy owns is the honest stand-in: it is where the mission
     * actually starts and it needs no new brain export. */
    {
        float ax = (float)map.cellx + (float)map.cellw * 0.5f;
        float az = (float)map.celly + (float)map.cellh * 0.5f;
        int n0 = wb_objects(g_objs, WB_MAX_OBJECTS), i, ng = 0;
        float sx = 0.0f, sz = 0.0f;
        for (i = 0; i < n0; ++i)
            if (!g_objs[i].limbo && strcmp(g_objs[i].house, "GoodGuy") == 0
                && strcmp(g_objs[i].kind, "TERRAIN") != 0)
            {
                sx += (float)g_objs[i].clx * (1.0f / 256.0f);
                sz += (float)g_objs[i].cly * (1.0f / 256.0f);
                ++ng;
            }
        if (ng > 0) { ax = sx / (float)ng; az = sz / (float)ng; }
        t1_cam_look_at(&g_cam, ax, az, t1_terrain_corner_y(&g_terr, (int)ax, (int)az));
    }
    (void)ppc;

    if (!w98_open("C&C 3D -- Windows 98, live mission", SCR_W, SCR_H, err, sizeof err))
    { MessageBoxA(NULL, err, "CNC3D Win98", MB_OK | MB_ICONERROR); return 1; }
    fb = w98_framebuffer();
    sr_bind(&tgt, SCR_W, SCR_H, fb->px, g_zbuf);

    db_state_clear(&state);
    state.radar_active = 1;
    state.nod = 0;

    tprev = w98_seconds();

    while (w98_pump())
    {
        double now = w98_seconds();
        double dt = now - tprev;
        tprev = now;
        if (dt > 0.5) dt = 0.5;

        if (w98_key_hit(VK_SPACE)) paused = !paused;

        /* ---- the mouse: pick a cell, then select or order ----
         *
         * The pick intersects the ray with the horizontal plane at the camera's own
         * look-at height rather than raycasting the heightfield. That is what the
         * desktop renderer does for ground clicks too, and the error is at most the
         * local slope times the camera height, which on this terrain is under a cell. */
        {
            int mx, my, hitL, hitR;
            w98_mouse(&mx, &my);
            /* Consume BOTH edges and remember which one fired. Inferring the button
             * from the current down-state instead is wrong and fails silently: a fast
             * click is already released by the time the frame polls, so a right click
             * read as "left is not down and right is not down" and took the select
             * branch. At ten frames a second every click is a fast click. */
            hitL = w98_mouse_hit(0);
            hitR = w98_mouse_hit(1);
            if (mx < VIEW_X1 && (hitL || hitR))
            {
                int left = hitL;
                float wx, wz;
                int cx, cz, i, best = -1;
                float bestd = 2.5f;
                t1_screen_to_plane(&g_cam, &g_scr, (float)mx, (float)my,
                                   g_cam.at_y, &wx, &wz);
                cx = (int)wx; cz = (int)wz;

                for (i = 0; i < nobj; ++i)
                {
                    float ox, oz, dx, dz, d;
                    if (g_objs[i].limbo) continue;
                    if (strcmp(g_objs[i].kind, "TERRAIN") == 0) continue;
                    ox = (float)g_objs[i].clx * (1.0f / 256.0f);
                    oz = (float)g_objs[i].cly * (1.0f / 256.0f);
                    dx = ox - wx; dz = oz - wz;
                    d = dx * dx + dz * dz;
                    if (d < bestd) { bestd = d; best = i; }
                }

                if (left)
                {
                    /* Selection is the ENGINE's, not ours: we clear and re-select
                     * through the brain and then read `sel` back off the next dump, so
                     * what the player sees bracketed is by construction what the engine
                     * will actually take orders about. */
                    wb_clear_selection();
                    if (best >= 0)
                    {
                        const char *k = g_objs[best].kind;
                        int t = (strcmp(k, "INFANTRY") == 0) ? WB_INFANTRY
                              : (strcmp(k, "UNIT") == 0)     ? WB_UNIT
                              : (strcmp(k, "AIRCRAFT") == 0) ? WB_AIRCRAFT
                              : (strcmp(k, "BUILDING") == 0) ? WB_BUILDING : 0;
                        if (t) wb_select(t, g_objs[best].id);
                    }
                }
                else
                {
                    wb_command_at_cell(cx, cz, w98_key(VK_CONTROL));
                }
                lastdump = -1.0;      /* refresh now so the feedback is immediate */
                pickx = cx; pickz = cz;
            }
        }
        {   /* scroll and zoom. The zoom sense is the console's: a bigger distance is
             * further away, so PageUp pulls back. */
            float sc = (float)dt * 12.0f;
            float ax = g_cam.at_x, az = g_cam.at_z;
            if (w98_key(VK_LEFT))  ax -= sc;
            if (w98_key(VK_RIGHT)) ax += sc;
            if (w98_key(VK_UP))    az -= sc;
            if (w98_key(VK_DOWN))  az += sc;
            if (w98_key_hit(VK_PRIOR)) t1_cam_set_dist(&g_cam, g_cam.dist_lep + T1_DIST_STEP);
            if (w98_key_hit(VK_NEXT))  t1_cam_set_dist(&g_cam, g_cam.dist_lep - T1_DIST_STEP);
            /* Clamped to the PLAYABLE RECT, not to the 64x64 grid, and with a margin
             * so the view cannot reach the edge of the terrain patch that gets drawn.
             * This is what makes skipping the colour clear safe: the ground covers every
             * pixel because the camera can never look past it. Widen this and the
             * background becomes last frame's leftovers. */
            {
                const float m = 5.0f;
                float lo_x = (float)map.cellx + m, hi_x = (float)(map.cellx + map.cellw) - m;
                float lo_z = (float)map.celly + m, hi_z = (float)(map.celly + map.cellh) - m;
                if (hi_x < lo_x) { lo_x = hi_x = (float)map.cellx + (float)map.cellw * 0.5f; }
                if (hi_z < lo_z) { lo_z = hi_z = (float)map.celly + (float)map.cellh * 0.5f; }
                if (ax < lo_x) ax = lo_x; if (ax > hi_x) ax = hi_x;
                if (az < lo_z) az = lo_z; if (az > hi_z) az = hi_z;
            }
            t1_cam_look_at(&g_cam, ax, az, t1_terrain_corner_y(&g_terr, (int)ax, (int)az));
        }
        if (w98_key_hit(VK_ADD)      || w98_key_hit(0xBB)) if (simhz < 60.0) simhz *= 2.0;
        if (w98_key_hit(VK_SUBTRACT) || w98_key_hit(0xBD)) if (simhz > 2.0)  simhz /= 2.0;
        if (w98_key_hit(VK_F12))
        {
            char shot[MAX_PATH];
            _snprintf(shot, sizeof shot, "%sw98game.bmp", base);
            w98_save_bmp(shot);
            MessageBeep(0xFFFFFFFF);
        }

        /* The engine's native rate is 15 Hz and the frame rate is independent of it.
         * That relationship is load bearing on this hardware: the renderer may well be
         * slower than the simulation, and the simulation must not slow down to match. */
        if (!paused)
        {
            acc += dt;
            while (acc >= 1.0 / simhz)
            {
                wb_tick();
                ++ticks;
                acc -= 1.0 / simhz;
                if (ticks % 4096 == 0) break;   /* never spin forever after a stall */
            }
        }
        /* The object dump is not free: the brain reports by printf, so every call
         * formats about 20 KB, writes it to a scratch file and parses it back, which
         * measured 11.91 ms a frame. Markers do not need that at the render rate. Five
         * times a second is under the engine's own 15 Hz and is imperceptible on
         * something moving at infantry speed. */
        if (now - lastdump >= 0.2 || nobj == 0)
        {
            tp = w98_seconds();
            nobj = wb_objects(g_objs, WB_MAX_OBJECTS);
            if (g_meshok)
            {
                int i;
                for (i = 0; i < nobj; ++i)
                    g_objmesh[i] = t1_mesh_for_type(&g_mesh, g_objs[i].name);
            }
            ta_objdump += w98_seconds() - tp;
            lastdump = now;
        }

        /* ---- the world ---- */
        tp = w98_seconds();
        /* THE COLOUR BUFFER IS NOT CLEARED. The terrain patch is the playable rect plus
         * a one cell margin, about 30x27 cells, and the camera sees roughly 14 cells
         * across at the default zoom, so the ground covers every pixel of the view and
         * a clear would be one megabyte written and then immediately overwritten. This
         * machine writes 119 MB/s, measured, so that clear cost about 7 ms of a 95 ms
         * frame for nothing.
         *
         * It stops being safe the moment the camera can see past the edge of the map,
         * which is why the scroll is clamped to the terrain and why this comment is
         * here: if the map edge ever comes into view, the fix is a sky pass, not
         * putting the clear back. The DEPTH clear stays; it is not optional. */
        sr_clear_depth(&tgt);
        ta_clear += w98_seconds() - tp;

        tp = w98_seconds();
        sr_use_shade(g_shade_terr);
        terrpx = t1_terrain_draw(&g_terr, &tgt, &g_cam, &g_scr,
                                 map.cellx - 1, map.celly - 1,
                                 map.cellw + 2, map.cellh + 2);
        ta_terr += w98_seconds() - tp;

        tp = w98_seconds();
        objpx = 0;
        if (g_meshok)
        {
            int i;
            sr_use_shade(g_shade_obj);          /* the models' own palette */
            for (i = 0; i < nobj; ++i)
            {
                W98_Object *o = &g_objs[i];
                int mi;
                float wx, wz, wy;
                if (o->limbo) continue;
                mi = g_objmesh[i];
                if (mi < 0) { draw_object(&tgt, o); continue; }  /* no model: marker */
                wx = (float)o->clx * (1.0f / 256.0f);
                wz = (float)o->cly * (1.0f / 256.0f);
                wy = t1_terrain_corner_y(&g_terr, (int)wx, (int)wz);
                objpx += t1_mesh_draw(&g_mesh, &tgt, &g_cam, &g_scr, mi, wx, wy, wz,
                                      o->face < 0 ? 0 : o->face,
                                      (o->tface >= 0 && o->face >= 0)
                                          ? (o->tface - o->face) : 0);
            }
        }
        else
        {
            int i;
            for (i = 0; i < nobj; ++i)
                if (strcmp(g_objs[i].kind, "TERRAIN") == 0) draw_object(&tgt, &g_objs[i]);
            for (i = 0; i < nobj; ++i)
                if (strcmp(g_objs[i].kind, "TERRAIN") != 0) draw_object(&tgt, &g_objs[i]);
        }
        {   /* Selection brackets, straight off the engine's own sel flag. */
            int i;
            for (i = 0; i < nobj; ++i)
            {
                float wx, wz, wy, sx, sy, w;
                int px, py, r;
                if (!g_objs[i].sel || g_objs[i].limbo) continue;
                wx = (float)g_objs[i].clx * (1.0f / 256.0f);
                wz = (float)g_objs[i].cly * (1.0f / 256.0f);
                wy = t1_terrain_corner_y(&g_terr, (int)wx, (int)wz);
                if (!project(wx, wy, wz, &sx, &sy, &w)) continue;
                px = (int)sx; py = (int)sy;
                r = (int)(g_scr.fx / w * 0.45f); if (r < 5) r = 5;
                /* corner ticks, the way the engine draws them, not a full box */
                fill(&tgt, px-r, py-r, px-r+r/2, py-r+2, 0x00FFFFFFU);
                fill(&tgt, px-r, py-r, px-r+2, py-r+r/2, 0x00FFFFFFU);
                fill(&tgt, px+r-r/2, py-r, px+r, py-r+2, 0x00FFFFFFU);
                fill(&tgt, px+r-2, py-r, px+r, py-r+r/2, 0x00FFFFFFU);
                fill(&tgt, px-r, py+r-2, px-r+r/2, py+r, 0x00FFFFFFU);
                fill(&tgt, px-r, py+r-r/2, px-r+2, py+r, 0x00FFFFFFU);
                fill(&tgt, px+r-r/2, py+r-2, px+r, py+r, 0x00FFFFFFU);
                fill(&tgt, px+r-2, py+r-r/2, px+r, py+r, 0x00FFFFFFU);
            }
        }
        ta_objdraw += w98_seconds() - tp;
        tp = w98_seconds();

        /* ---- the real DOS sidebar over the top ---- */
        memset(g_surf8, DB_TBLACK, sizeof g_surf8);
        db_clip_reset(&surf);
        db_draw_sidebar(&surf, pack, &state);
        db_draw_credits_tab(&surf, pack, 2000);

        if (font)
        {
            int gdi = 0, nod = 0, i;
            for (i = 0; i < nobj; ++i)
            {
                if (strcmp(g_objs[i].house, "GoodGuy") == 0) ++gdi;
                else if (strcmp(g_objs[i].house, "BadGuy") == 0) ++nod;
            }
            _snprintf(line, sizeof line, "SCG01EA  TICK %d  %d OBJECTS  GDI %d  NOD %d%s",
                      ticks, nobj, gdi, nod, paused ? "  [PAUSED]" : "");
            /* Down the bottom, not the top: the credits plate owns x 160..239 of the
             * DOS screen and the status line was running straight through it. */
            db_print(&surf, font, line, 3, 184, fontpal, DB_FONT6_XSPACING);
            _snprintf(line, sizeof line,
                      "%.1f FPS  D%.0f P%.1f  CELL %d,%d  LMB SELECT  RMB ORDER",
                      fps, g_cam.dist_lep, g_cam.pitch * 57.29578f, pickx, pickz);
            db_print(&surf, font, line, 3, 192, fontpal, DB_FONT6_XSPACING);
        }
        ta_bar += w98_seconds() - tp;
        tp = w98_seconds();
        sr_blit8(&tgt, g_surf8, DB_SCREEN_W, DB_SCREEN_H, g_pal32, 0, 0, SCALE, 1);
        ta_bar += 0.0; ta_blit += w98_seconds() - tp;

        tp = w98_seconds();
        w98_present();
        ta_present += w98_seconds() - tp;

        ++frames; ++fps_frames; fps_acc += dt;
        if (fps_acc >= 0.5) { fps = fps_frames / fps_acc; fps_acc = 0.0; fps_frames = 0; }
    }

    /* Beside the exe, not in the current directory. Nothing that launches a program on
     * this box sets a working directory worth relying on, and a screenshot that lands
     * somewhere unexpected is a screenshot nobody looks at. */
    _snprintf(packpath, sizeof packpath, "%sw98game.bmp", base);
    w98_save_bmp(packpath);
    {
        FILE *rep;
        _snprintf(packpath, sizeof packpath, "%sw98game.txt", base);
        rep = fopen(packpath, "wb");
        if (rep)
        {
            fprintf(rep, "CNC3D live mission on Windows 98\r\n");
            fprintf(rep, "scenario=SCG01EA ticks=%d objects=%d\r\n", ticks, nobj);
            fprintf(rep, "map theater=%d origin=%d,%d size=%dx%d px_per_cell=%.2f\r\n",
                    map.theater, map.cellx, map.celly, map.cellw, map.cellh, ppc);
            fprintf(rep, "frames=%d last_fps=%.2f sim_hz=%.0f terrain_px=%ld\r\n",
                    frames, fps, simhz, terrpx);
            if (frames > 0)
            {
                double per = 1000.0 / (double)frames;
                fprintf(rep, "ms_clear=%.2f\r\n",   ta_clear   * per);
                fprintf(rep, "ms_terrain=%.2f\r\n", ta_terr    * per);
                fprintf(rep, "ms_objdump=%.2f\r\n", ta_objdump * per);
                fprintf(rep, "ms_objdraw=%.2f\r\n", ta_objdraw * per);
                fprintf(rep, "ms_sidebar=%.2f\r\n", ta_bar     * per);
                fprintf(rep, "ms_blit=%.2f\r\n",    ta_blit    * per);
                fprintf(rep, "ms_present=%.2f\r\n", ta_present * per);
                fprintf(rep, "ms_accounted=%.2f\r\n",
                        (ta_clear+ta_terr+ta_objdump+ta_objdraw+ta_bar+ta_blit+ta_present) * per);
            }
            fprintf(rep, "OK\r\n");
            fclose(rep);
        }
    }
    w98_close();
    wb_close();
    t1_terrain_free(&g_terr);
    if (g_meshok) t1_mesh_free(&g_mesh);
    db_pack_free(pack);
    return 0;
}
