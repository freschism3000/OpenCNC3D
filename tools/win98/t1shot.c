/*
 * t1shot.c -- render a Tier 1 model on the DEVELOPMENT MACHINE, with no box in the loop.
 *
 * WHY THIS EXISTS. Every visual question on this branch used to cost a full round trip:
 * cross-compile, copy over SMB, run on the Windows 98 machine, copy a BMP back. That is
 * a couple of minutes for a question like "did that texture get the right UVs", and it
 * is the reason a wrong answer can sit in the pack for days.
 *
 * softras.c is pure C89 with no Win32 in it precisely so it can be built here, and
 * t1_mesh.c is the same code the game runs. This host links BOTH UNEDITED and renders
 * with the real camera constants, so a picture from here is the software tier's own
 * answer, not a lookalike. It is not the Voodoo's answer -- the card filters bilinearly
 * and this does not -- but every question about GEOMETRY, UVs, part animation, house
 * colour and draw order is settled here in a second.
 *
 *     t1shot <bank.t1mesh> <TYPECODE> <out.bmp> [key=value ...]
 *
 *     w=640 h=480      output size
 *     zoom=3.0         focal multiplier; 1.0 is the game's own scale at that height
 *     face=0           body facing, 0..255
 *     turret=0         turret delta, 0..255
 *     house=1          1 GDI (the sand table), 0 Nod and neutral
 *     rotor=0          rotor yaw, 0..255
 *     anim=-1          baked animation frame, fractional; -1 is the rest pose
 *     build=1.0        construction fraction: below 1 draws only the first sections
 *     mode=0           triangle mode mask (0 -> opaque|cutout, 4 shadow, 8 translucent)
 *     grid=1           draw a one-cell ground grid under the model for scale
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "softras.h"
#include "t1_cam.h"
#include "t1_mesh.h"

static void put32(FILE *f, unsigned int v)
{ fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }

/* Bottom-up 24-bit, the same shape w98_glidegame.c writes, so the two are comparable. */
static void write_bmp(const char *path, const unsigned int *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    int pad = (4 - (w * 3) % 4) % 4, y, x, i;
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fputc('B', f); fputc('M', f);
    put32(f, 14 + 40 + (w * 3 + pad) * h); put32(f, 0); put32(f, 14 + 40);
    put32(f, 40); put32(f, (unsigned)w); put32(f, (unsigned)h);
    fputc(1, f); fputc(0, f); fputc(24, f); fputc(0, f);
    put32(f, 0); put32(f, 0); put32(f, 0); put32(f, 0); put32(f, 0); put32(f, 0);
    for (y = h - 1; y >= 0; --y)
    {
        for (x = 0; x < w; ++x)
        {
            unsigned int c = px[y * w + x];
            fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f);
        }
        for (i = 0; i < pad; ++i) fputc(0, f);
    }
    fclose(f);
}

static float argf(int argc, char **argv, const char *key, float def)
{
    int i; size_t n = strlen(key);
    for (i = 4; i < argc; ++i)
        if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return (float)atof(argv[i] + n + 1);
    return def;
}

int main(int argc, char **argv)
{
    T1_MeshBank bank;
    T1_Cam cam;
    T1_Screen scr;
    SR_Target tgt;
    unsigned int *px, *shade;
    int *zb;
    char err[256];
    int w, h, mi, face, turret, house, rotor, grid;
    float zoom;
    long drawn;

    if (argc < 4)
    { fprintf(stderr, "usage: t1shot <bank.t1mesh> <TYPECODE> <out.bmp> [w= h= zoom= face= turret= house= rotor= grid=]\n"); return 2; }

    w      = (int)argf(argc, argv, "w", 640);
    h      = (int)argf(argc, argv, "h", 480);
    zoom   = argf(argc, argv, "zoom", 3.0f);
    face   = (int)argf(argc, argv, "face", 0);
    turret = (int)argf(argc, argv, "turret", 0);
    house  = (int)argf(argc, argv, "house", 1);
    rotor  = (int)argf(argc, argv, "rotor", 0);
    grid   = (int)argf(argc, argv, "grid", 0);

    if (!t1_mesh_load(&bank, argv[1], err, sizeof err))
    { fprintf(stderr, "%s\n", err); return 1; }
    bank.house = house;
    bank.rotor = rotor;

    mi = t1_mesh_for_type(&bank, argv[2]);
    if (mi < 0) { fprintf(stderr, "no model for type '%s'\n", argv[2]); return 1; }

    px    = (unsigned int *)calloc((size_t)(w * h), sizeof *px);
    zb    = (int *)calloc((size_t)(w * h), sizeof *zb);
    shade = (unsigned int *)malloc(sizeof(unsigned int) * SR_SHADES * 256);
    sr_bind(&tgt, w, h, px, zb);
    sr_build_shade(bank.pal, shade);
    sr_use_shade(shade);
    sr_clear(&tgt, 0x101014);
    sr_clear_depth(&tgt);

    t1_cam_set_dist(&cam, T1_DIST_DEF);
    t1_cam_look_at(&cam, 0.0f, 0.0f, 0.0f);
    t1_screen_params(&scr, w, h);
    scr.fx *= zoom; scr.fy *= zoom;

    if (grid)
    {
        /* A one-cell chequer under the model, so a size or a pivot error is obvious. */
        int gx, gz;
        SR_Texture gt;
        static unsigned char gpx[4];
        gpx[0] = 1; gpx[1] = 2; gpx[2] = 2; gpx[3] = 1;
        sr_texture(&gt, gpx, 2, 2);
        for (gz = -2; gz < 2; ++gz) for (gx = -2; gx < 2; ++gx)
        {
            SR_Vertex q[4]; int k;
            float x0 = (float)gx, x1 = x0 + 1.0f, z0 = (float)gz, z1 = z0 + 1.0f;
            t1_world_to_eye(&cam, x0, 0.0f, z0, &q[0]);
            t1_world_to_eye(&cam, x1, 0.0f, z0, &q[1]);
            t1_world_to_eye(&cam, x1, 0.0f, z1, &q[2]);
            t1_world_to_eye(&cam, x0, 0.0f, z1, &q[3]);
            q[0].u = 0; q[0].v = 0; q[1].u = 2; q[1].v = 0;
            q[2].u = 2; q[2].v = 2; q[3].u = 0; q[3].v = 2;
            for (k = 0; k < 4; ++k) SR_GREY(q[k], 0.35f);
            t1_tri(&tgt, &q[0], &q[1], &q[2], &gt, &scr);
            t1_tri(&tgt, &q[0], &q[2], &q[3], &gt, &scr);
        }
    }

    {
        T1_MeshParams p;
        int frames = 0, tpf = 0, t0 = 0, t1v = 0, loop = 0;
        memset(&p, 0, sizeof p);
        p.mesh = mi;
        p.facing = face; p.tdelta = turret;
        p.modemask = (int)argf(argc, argv, "mode", 0);
        p.animT = argf(argc, argv, "anim", -1.0f);
        p.build_frac = argf(argc, argv, "build", 1.0f);
        drawn = t1_mesh_draw_p(&bank, &tgt, &cam, &scr, &p);
        if (t1_mesh_clip(&bank, mi, &frames, &tpf, &t0, &t1v, &loop))
            printf("%s: mesh %d, %ld pixels, clip %d frames tpf=%d [%d,%d) loop=%d, %d sections\n",
                   argv[2], mi, drawn, frames, tpf, t0, t1v, loop, t1_mesh_sections(&bank, mi));
        else
            printf("%s: mesh %d, %ld pixels, no clip, %d sections\n",
                   argv[2], mi, drawn, t1_mesh_sections(&bank, mi));
    }
    write_bmp(argv[3], px, w, h);
    t1_mesh_free(&bank);
    return 0;
}
