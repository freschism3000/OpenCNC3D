/*
 * w98_proto.c -- the first native Windows 98 prototype of C&C 3D.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT.
 *
 * It is a real Win32 program built for Windows 98 SE, drawing every pixel on the CPU.
 * There is no OpenGL, no Glide, no SDL and no 3D driver anywhere in it. The window is
 * a DIB section and the renderer writes into it directly.
 *
 * The picture is the real one: it links game/dosbar.c UNEDITED, the project's own
 * 1995 MS-DOS sidebar module, and feeds it the project's own dossidebar.pack. The
 * cameos, the fonts, the palette, the raised boxes and the power bar are the ones the
 * Mac build draws, produced by the same code. That is the point. A demo written to
 * look like C&C would prove nothing; this proves the actual code path.
 *
 * Behind the sidebar, the software rasteriser draws a perspective, z-buffered,
 * perspective-correct textured scene, textured with real cameo art out of the same
 * pack, so the 3D path is exercised too rather than promised.
 *
 * It is NOT the game. There is no brain, no simulation, no input beyond the keys
 * below, and the sidebar contents are a fixed arrangement rather than a live
 * production queue. It answers one question, which was the only question worth asking
 * first: can a Pentium II class machine running Windows 98 draw this game's own art
 * with its own code, and how fast.
 *
 * Keys:  ESC quit   F12 write a BMP of the frame   +/- change the 3D load
 *        S toggle the sidebar   G toggle the ground   L toggle the lighting table
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "w98_gfx.h"
#include "softras.h"
#include "dosbar.h"

#define SCR_W 640
#define SCR_H 400
#define SCALE 2                   /* the DOS 320x200 screen, doubled */

#define TEX_W 64
#define TEX_H 64

static unsigned int  g_pal32[256];
static unsigned int  g_shade[SR_SHADES * 256];
static unsigned char g_surf8[DB_SCREEN_W * DB_SCREEN_H];
static int           g_zbuf[SCR_W * SCR_H];
static unsigned char g_tex[TEX_W * TEX_H];

/* ------------------------------------------------------------------------- *
 * A cube, and a ground plane. Enough geometry to make the fill rate mean
 * something without pretending to be a scene.
 * ------------------------------------------------------------------------- */

static const float CUBE_V[8][3] = {
    {-1,-1,-1},{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},
    {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}
};
/* four corner indices per face, wound so that the outside is front facing */
static const int CUBE_F[6][4] = {
    {0,3,2,1}, {4,5,6,7}, {0,1,5,4}, {2,3,7,6}, {0,4,7,3}, {1,2,6,5}
};
static const float CUBE_L[6] = { 0.55f, 0.95f, 0.70f, 0.85f, 0.62f, 0.78f };

static void mat_rotate(float m[9], float ax, float ay)
{
    float ca = (float)cos(ax), sa = (float)sin(ax);
    float cb = (float)cos(ay), sb = (float)sin(ay);
    /* Y then X, written out rather than multiplied, because a 3x3 product here would
     * be three lines of code and one more place for a transposition bug to hide. */
    m[0] =  cb;        m[1] = 0.0f;  m[2] =  sb;
    m[3] =  sa * sb;   m[4] =  ca;   m[5] = -sa * cb;
    m[6] = -ca * sb;   m[7] =  sa;   m[8] =  ca * cb;
}

static void xform(const float m[9], const float in[3], float out[3])
{
    out[0] = m[0]*in[0] + m[1]*in[1] + m[2]*in[2];
    out[1] = m[3]*in[0] + m[4]*in[1] + m[5]*in[2];
    out[2] = m[6]*in[0] + m[7]*in[1] + m[8]*in[2];
}

/* ------------------------------------------------------------------------- *
 * Build a power-of-two texture out of a real cameo, because the rasteriser
 * masks rather than clamps and the pack's art is 32x24.
 * ------------------------------------------------------------------------- */
static void build_texture(const DB_Pack *pack, const char *cameo)
{
    const DB_Shape *sh = db_shape(pack, cameo);
    int x, y, ox, oy;

    /* A flat ground of the sidebar's own mid grey, so the cameo reads against it. */
    memset(g_tex, DB_LTGREY, sizeof g_tex);
    /* a one texel border in dark grey makes the texture seams visible, which is how
     * you can see at a glance whether the perspective correction is actually on */
    for (x = 0; x < TEX_W; ++x) { g_tex[x] = DB_DKGREY; g_tex[(TEX_H-1)*TEX_W + x] = DB_DKGREY; }
    for (y = 0; y < TEX_H; ++y) { g_tex[y*TEX_W] = DB_DKGREY; g_tex[y*TEX_W + TEX_W-1] = DB_DKGREY; }

    if (!sh || !sh->pixels) return;
    ox = (TEX_W - sh->w) / 2;
    oy = (TEX_H - sh->h) / 2;
    for (y = 0; y < sh->h; ++y)
        for (x = 0; x < sh->w; ++x)
        {
            unsigned char p = sh->pixels[y * sh->w + x];
            if (p) g_tex[(oy + y) * TEX_W + (ox + x)] = p;
        }
}

/* ------------------------------------------------------------------------- *
 * The sidebar state. A plausible GDI base mid-game, so the strip has real
 * cameos, a build in progress and a finished item, rather than empty boxes.
 * ------------------------------------------------------------------------- */
static void fill_sidebar_state(DB_State *st)
{
    db_state_clear(st);
    db_state_add(st, 0, "NUKE");   /* power plant     */
    db_state_add(st, 0, "PYLE");   /* barracks        */
    db_state_add(st, 0, "PROC");   /* refinery        */
    db_state_add(st, 0, "WEAP");   /* weapons factory */
    db_state_add(st, 0, "HQ");
    db_state_add(st, 1, "E1");     /* minigunner      */
    db_state_add(st, 1, "E2");
    db_state_add(st, 1, "JEEP");
    db_state_add(st, 1, "MTNK");   /* medium tank     */
    db_state_add(st, 1, "APC");

    st->items[0][2].producing  = 1;      /* the refinery is building */
    st->items[0][2].completion = 61;
    st->items[1][3].producing  = 1;      /* the medium tank is done  */
    st->items[1][3].completion = DB_STEP_COUNT - 1;
    st->items[1][3].ready      = 1;

    st->radar_active = 1;
    st->power_total  = 150;
    st->power_drain  = 95;
    st->nod          = 0;
}

/* ------------------------------------------------------------------------- */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char err[256];
    char packerr[256];
    char packpath[MAX_PATH];
    DB_Pack *pack;
    DB_State state;
    DB_Surface surf;
    SR_Target  tgt;
    SR_Texture tex;
    W98_Frame *fb;
    unsigned char fontpal[16];
    const DB_Font *font;
    FILE *rep;

    /* Phase timers. The whole point of measuring per phase is that the first run's
     * "1014 cycles per pixel" figure charged the rasteriser for the clears, the sidebar,
     * the blit and the BitBlt as well, so it could not be right and could not be acted
     * on. These make the frame add up. */
    double ta_clear = 0.0, ta_raster = 0.0, ta_bar = 0.0, ta_blit = 0.0, ta_present = 0.0;
    double tp;
    int    bench = 0, benchframes = 0;
    double t0, tnow, tprev, fps_acc = 0.0;
    int    frames = 0, fps_frames = 0;
    double fps = 0.0;
    long   pixels_this_frame = 0;
    double mhz = 0.0;
    double mem_dib = 0.0, mem_heap = 0.0;
    int    load = 1;            /* number of cubes across the grid */
    int    show_sidebar = 1, show_ground = 1, lit = 1;
    float  ang = 0.0f;
    char   line[160];

    (void)inst; (void)prev; (void)show;

    /* "bench" on the command line runs a fixed number of frames and exits on its own,
     * so a measurement does not need a keystroke delivered to the box. "novnc" is not a
     * flag: to test whether VNC is distorting the numbers, disconnect VNC and run the
     * same bench again. */
    if (cmd && strstr(cmd, "bench")) { bench = 1; benchframes = 300; }

    /* ---- the pack. Everything drawn comes out of this one file. ----
     *
     * Resolved next to the EXE, not against the current directory. On Win98 a program
     * is very often started with a working directory that has nothing to do with where
     * it lives (the Run box, a shortcut, a remote launcher), and "file not found" for
     * data that is plainly sitting beside the binary is a confusing way to fail. */
    {
        char *slash;
        GetModuleFileNameA(NULL, packpath, sizeof packpath - 32);
        slash = strrchr(packpath, '\\');
        if (slash) slash[1] = 0; else packpath[0] = 0;
        strcat(packpath, "dossidebar.pack");
    }
    pack = db_pack_load(packpath, packerr, sizeof packerr);
    if (!pack)
    {
        char m[512];
        _snprintf(m, sizeof m,
                  "Could not load %s\n\n%s\n\n"
                  "It is a build product, so it is not in git. Copy it in beside the exe.",
                  packpath, packerr);
        MessageBoxA(NULL, m, "CNC3D Win98 prototype", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* pal8 is the DOS palette already widened the way the 1995 engine widened it
     * (v << 2, max 252). Both the 2D blit and the 3D shade table come from it, so
     * the cube and the sidebar are lit out of the same 256 colours. */
    {
        int i;
        for (i = 0; i < 256; ++i)
            g_pal32[i] = ((unsigned int)pack->pal8[i*3+0] << 16)
                       | ((unsigned int)pack->pal8[i*3+1] <<  8)
                       |  (unsigned int)pack->pal8[i*3+2];
    }
    sr_build_shade(pack->pal8, g_shade);
    sr_use_shade(g_shade);

    build_texture(pack, "MTNK");
    sr_texture(&tex, g_tex, TEX_W, TEX_H);

    font = db_font(pack, "6POINT");
    db_font_palette(fontpal, DB_WHITE, DB_TBLACK);

    fill_sidebar_state(&state);
    db_surface_init(&surf, DB_SCREEN_W, DB_SCREEN_H, g_surf8);

    if (!w98_open("C&C 3D -- Windows 98 software renderer", SCR_W, SCR_H, err, sizeof err))
    {
        MessageBoxA(NULL, err, "CNC3D Win98 prototype", MB_OK | MB_ICONERROR);
        db_pack_free(pack);
        return 1;
    }
    fb = w98_framebuffer();
    sr_bind(&tgt, SCR_W, SCR_H, fb->px, g_zbuf);

    /* Measure the clock rate once, by counting CPU cycles across a known QPC
     * interval. Windows 98 does not put ~MHz in the registry, and the number matters:
     * it is what a pixels-per-second figure has to be judged against. */
    {
        unsigned __int64 c0, c1;
        double s0, s1;
        s0 = w98_seconds(); c0 = w98_rdtsc();
        while (w98_seconds() - s0 < 0.20) { /* spin */ }
        c1 = w98_rdtsc(); s1 = w98_seconds();
        if (s1 > s0) mhz = (double)(__int64)(c1 - c0) / (s1 - s0) / 1.0e6;
    }

    /* MEMORY BANDWIDTH, measured before anything else, because it decides which
     * optimisation is the right one. If writing to the DIB section is much slower than
     * writing to ordinary heap memory, then the framebuffer is uncached or write
     * combined, every rasterised pixel is paying for it, and the answer is to render
     * into system RAM and blit once per frame rather than to micro-optimise the loop. */
    {
        unsigned int *heap = (unsigned int *)malloc(SCR_W * SCR_H * 4);
        double a, b, c;
        int rep_i, n = SCR_W * SCR_H, ii;
        a = w98_seconds();
        for (rep_i = 0; rep_i < 8; ++rep_i)
            for (ii = 0; ii < n; ++ii) fb->px[ii] = 0x00203040;
        b = w98_seconds();
        if (heap)
            for (rep_i = 0; rep_i < 8; ++rep_i)
                for (ii = 0; ii < n; ++ii) heap[ii] = 0x00203040;
        c = w98_seconds();
        /* 8 passes over w*h*4 bytes, reported as MB/s */
        mem_dib  = (b > a) ? (8.0 * n * 4.0 / (b - a)) / 1048576.0 : 0.0;
        mem_heap = (c > b) ? (8.0 * n * 4.0 / (c - b)) / 1048576.0 : 0.0;
        if (heap) free(heap);
    }

    t0 = tprev = w98_seconds();

    while (w98_pump())
    {
        double dt;
        long drawn = 0;

        tnow = w98_seconds();
        dt = tnow - tprev;
        tprev = tnow;
        if (dt > 0.25) dt = 0.25;          /* a stall must not fling the cubes */
        ang += (float)dt * 0.7f;

        if (w98_key_hit(VK_ADD)      || w98_key_hit(0xBB)) { if (load < 6) ++load; }
        if (w98_key_hit(VK_SUBTRACT) || w98_key_hit(0xBD)) { if (load > 0) --load; }
        if (w98_key_hit('S')) show_sidebar = !show_sidebar;
        if (w98_key_hit('G')) show_ground  = !show_ground;
        if (w98_key_hit('L')) lit = !lit;
        if (w98_key_hit(VK_F12))
        {
            w98_save_bmp("w98proto.bmp");
            MessageBeep(0xFFFFFFFF);
        }

        /* ---- 3D, drawn first because the sidebar composites over it ---- */
        tp = w98_seconds();
        sr_clear(&tgt, 0x00101418);
        sr_clear_depth(&tgt);
        ta_clear += w98_seconds() - tp;
        tp = w98_seconds();

        {
            float m[9];
            int gx, gz, f, k;
            float cxs = (float)SCR_W * 0.5f, cys = (float)SCR_H * 0.5f;
            float fxs = (float)SCR_W * 0.85f, fys = (float)SCR_W * 0.85f;

            mat_rotate(m, ang * 0.6f, ang);

            if (show_ground)
            {
                /* A flat quad grid under the cubes. It exists to put a known, large
                 * number of textured pixels through the rasteriser every frame, so
                 * the fill rate number below is measured on something and not on an
                 * empty screen. */
                int N = 10;
                for (gz = 0; gz < N; ++gz)
                    for (gx = 0; gx < N; ++gx)
                    {
                        SR_Vertex q[4];
                        float x0 = -10.0f + 20.0f * (float)gx / (float)N;
                        float x1 = -10.0f + 20.0f * (float)(gx+1) / (float)N;
                        float z0 =   3.0f + 30.0f * (float)gz / (float)N;
                        float z1 =   3.0f + 30.0f * (float)(gz+1) / (float)N;
                        float li = lit ? (0.85f - 0.55f * (float)gz / (float)N) : 1.0f;
                        int j;
                        q[0].x = x0; q[0].y = -2.0f; q[0].z = z0; q[0].u = 0;    q[0].v = 0;
                        q[1].x = x1; q[1].y = -2.0f; q[1].z = z0; q[1].u = TEX_W;q[1].v = 0;
                        q[2].x = x1; q[2].y = -2.0f; q[2].z = z1; q[2].u = TEX_W;q[2].v = TEX_H;
                        q[3].x = x0; q[3].y = -2.0f; q[3].z = z1; q[3].u = 0;    q[3].v = TEX_H;
                        for (j = 0; j < 4; ++j) { q[j].w = q[j].z; SR_GREY(q[j], li); }
                        drawn += sr_triangle(&tgt, &q[0], &q[2], &q[1], &tex, cxs, cys, fxs, fys);
                        drawn += sr_triangle(&tgt, &q[0], &q[3], &q[2], &tex, cxs, cys, fxs, fys);
                    }
            }

            for (k = 0; k < load; ++k)
            {
                float ofsx = ((float)(k % 3) - 1.0f) * 3.2f;
                float ofsz = 8.0f + (float)(k / 3) * 4.0f;
                for (f = 0; f < 6; ++f)
                {
                    SR_Vertex q[4];
                    int j;
                    for (j = 0; j < 4; ++j)
                    {
                        float o[3];
                        xform(m, CUBE_V[CUBE_F[f][j]], o);
                        q[j].x = o[0] + ofsx;
                        q[j].y = o[1];
                        q[j].z = o[2] + ofsz;
                        q[j].w = q[j].z;
                        SR_GREY(q[j], lit ? CUBE_L[f] : 1.0f);
                    }
                    q[0].u = 0;     q[0].v = 0;
                    q[1].u = TEX_W; q[1].v = 0;
                    q[2].u = TEX_W; q[2].v = TEX_H;
                    q[3].u = 0;     q[3].v = TEX_H;
                    drawn += sr_triangle(&tgt, &q[0], &q[1], &q[2], &tex, cxs, cys, fxs, fys);
                    drawn += sr_triangle(&tgt, &q[0], &q[2], &q[3], &tex, cxs, cys, fxs, fys);
                }
            }
        }
        ta_raster += w98_seconds() - tp;
        pixels_this_frame = drawn;

        /* ---- 2D: the project's own sidebar, its own code, its own art ---- */
        tp = w98_seconds();
        memset(g_surf8, DB_TBLACK, sizeof g_surf8);   /* index 0 == see through */
        db_clip_reset(&surf);
        if (show_sidebar)
        {
            db_draw_sidebar(&surf, pack, &state);
            db_draw_credits_tab(&surf, pack, 4271);
        }

        if (font)
        {
            _snprintf(line, sizeof line, "%.1f FPS  %ldk px  %.0f MHz",
                      fps, pixels_this_frame / 1000L, mhz);
            db_print(&surf, font, line, 3, 3, fontpal, DB_FONT6_XSPACING);
            _snprintf(line, sizeof line, "SOFTWARE RENDERER  load %d  %s%s%s",
                      load, show_ground ? "ground " : "", lit ? "lit " : "flat ",
                      show_sidebar ? "sidebar" : "");
            db_print(&surf, font, line, 3, 11, fontpal, DB_FONT6_XSPACING);
        }

        ta_bar += w98_seconds() - tp;

        tp = w98_seconds();
        sr_blit8(&tgt, g_surf8, DB_SCREEN_W, DB_SCREEN_H, g_pal32, 0, 0, SCALE, 1);
        ta_blit += w98_seconds() - tp;

        tp = w98_seconds();
        w98_present();
        ta_present += w98_seconds() - tp;

        ++frames; ++fps_frames;
        fps_acc += dt;
        if (fps_acc >= 0.5)
        {
            fps = (double)fps_frames / fps_acc;
            fps_acc = 0.0; fps_frames = 0;
        }
        if (bench && frames >= benchframes) break;
    }

    /* ---- the report, so the result can be read from the Mac without VNC ---- */
    {
        char *slash = strrchr(packpath, '\\');
        if (slash) { slash[1] = 0; strcat(packpath, "w98proto.bmp"); }
        else strcpy(packpath, "w98proto.bmp");
    }
    w98_save_bmp(packpath);
    {
        char *dot = strrchr(packpath, '.');
        if (dot) strcpy(dot, ".txt");
    }
    rep = fopen(packpath, "wb");
    if (rep)
    {
        double elapsed = w98_seconds() - t0;
        fprintf(rep, "CNC3D Windows 98 software renderer prototype\r\n");
        fprintf(rep, "frames=%d elapsed=%.2fs avg_fps=%.2f\r\n",
                frames, elapsed, elapsed > 0 ? frames / elapsed : 0.0);
        fprintf(rep, "last_fps=%.2f last_pixels=%ld\r\n", fps, pixels_this_frame);
        fprintf(rep, "cpu_mhz=%.0f\r\n", mhz);
        fprintf(rep, "write_MBps_dibsection=%.0f\r\n", mem_dib);
        fprintf(rep, "write_MBps_heap=%.0f\r\n", mem_heap);
        fprintf(rep, "fill_pixels_per_sec=%.0f\r\n", fps * (double)pixels_this_frame);
        if (mhz > 0.0 && pixels_this_frame > 0 && fps > 0.0)
            fprintf(rep, "cycles_per_pixel=%.1f\r\n",
                    (mhz * 1.0e6) / (fps * (double)pixels_this_frame));
        if (frames > 0)
        {
            double per = 1000.0 / (double)frames;   /* -> milliseconds per frame */
            double acc = ta_clear + ta_raster + ta_bar + ta_blit + ta_present;
            fprintf(rep, "ms_per_frame_clear=%.2f\r\n",   ta_clear   * per);
            fprintf(rep, "ms_per_frame_raster=%.2f\r\n",  ta_raster  * per);
            fprintf(rep, "ms_per_frame_sidebar=%.2f\r\n", ta_bar     * per);
            fprintf(rep, "ms_per_frame_blit=%.2f\r\n",    ta_blit    * per);
            fprintf(rep, "ms_per_frame_present=%.2f\r\n", ta_present * per);
            fprintf(rep, "ms_per_frame_accounted=%.2f\r\n", acc * per);
            fprintf(rep, "ms_per_frame_total=%.2f\r\n",
                    (w98_seconds() - t0) * per);
            if (ta_raster > 0.0 && pixels_this_frame > 0 && mhz > 0.0)
                fprintf(rep, "raster_cycles_per_pixel=%.1f\r\n",
                        (ta_raster / (double)frames) * mhz * 1.0e6
                        / (double)pixels_this_frame);
        }
        fprintf(rep, "bench=%d\r\n", bench);
        fprintf(rep, "load=%d ground=%d lit=%d sidebar=%d\r\n",
                load, show_ground, lit, show_sidebar);
        fprintf(rep, "screen=%dx%d scale=%d\r\n", SCR_W, SCR_H, SCALE);
        fprintf(rep, "pack_shapes=%d pack_fonts=%d\r\n", pack->nshapes, pack->nfonts);
        fprintf(rep, "OK\r\n");
        fclose(rep);
    }

    w98_close();
    db_pack_free(pack);
    return 0;
}
