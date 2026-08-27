/*
 * t1_glidetest.c -- proof of life for the 3dfx Voodoo 2 path, and the first real
 *                   framerate number off the hardware.
 *
 * Before porting the renderer to Glide it is worth knowing three things, and this
 * answers all three on the actual card rather than by argument:
 *
 *   1. Does the toolchain produce a Glide binary this driver will load and run.
 *   2. What does the Voodoo 2 actually fill at 640x480, in triangles and in pixels.
 *   3. Can a frame be got back off the card and looked at.
 *
 * Number three is not a nicety. A Voodoo 2 is a 3D-only add-in card: it takes over the
 * screen and its output never touches the 2D card, so it is INVISIBLE over VNC. The only
 * way to see what it drew is grLfbReadRegion into a buffer and a BMP written to disk.
 * That was learned the expensive way on this same box by an earlier project.
 *
 * It draws a moving textured triangle load, with the same 8-bit palettised textures the
 * software renderer already uses, because Glide has native GR_TEXFMT_P_8 and that is what
 * makes the conversion work already done transfer instead of being thrown away.
 *
 *   w98glidetest [triangles] [frames]
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "t1_glide_compat.h"   /* NOT <glide.h>: see the header, it pins stdcall */

#define SCR_W 640
#define SCR_H 480
#define TEX_SZ 256                     /* the Voodoo 2's maximum; also its sweet spot */

static unsigned char g_tex[TEX_SZ * TEX_SZ];
static FxU32         g_pal[256];
static unsigned short g_read[SCR_W * SCR_H];

static void report(const char *fmt, ...)
{
    /* A Glide app owns the screen, so there is nowhere to print. Everything goes to a
     * file beside the exe, which is also how the result gets back to the Mac. */
    static FILE *f;
    char path[MAX_PATH], *slash;
    va_list ap;
    if (!f)
    {
        GetModuleFileNameA(NULL, path, sizeof path - 24);
        slash = strrchr(path, '\\');
        if (slash) slash[1] = 0; else path[0] = 0;
        strcat(path, "glidetest.txt");
        f = fopen(path, "wb");
        if (!f) return;
    }
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputs("\r\n", f);
    fflush(f);
}

static void build_texture(void)
{
    int x, y, i;
    for (y = 0; y < TEX_SZ; ++y)
        for (x = 0; x < TEX_SZ; ++x)
        {
            /* a checker with a gradient, so both texel fetch and filtering are visible */
            int c = (((x >> 5) ^ (y >> 5)) & 1) ? 1 : 2;
            g_tex[y * TEX_SZ + x] = (unsigned char)(c * 64 + ((x + y) >> 3));
        }
    for (i = 0; i < 256; ++i)
    {
        int r = (i * 3) & 0xFF, g = (i * 5) & 0xFF, b = (i * 7) & 0xFF;
        g_pal[i] = ((FxU32)r << 16) | ((FxU32)g << 8) | (FxU32)b;
    }
}

static void save_bmp(const char *name, const unsigned short *src)
{
    /* RGB565 off the card into a 24-bit bottom-up BMP, which is the one format that both
     * Win9x and every tool on the Mac read without arguing. */
    char path[MAX_PATH], *slash;
    FILE *f;
    unsigned char hdr[54], *row;
    int rowbytes = (SCR_W * 3 + 3) & ~3;
    long imgsize = (long)rowbytes * SCR_H, filesize = 54 + imgsize;
    int x, y;

    GetModuleFileNameA(NULL, path, sizeof path - 24);
    slash = strrchr(path, '\\');
    if (slash) slash[1] = 0; else path[0] = 0;
    strcat(path, name);
    f = fopen(path, "wb");
    if (!f) return;
    row = (unsigned char *)malloc((size_t)rowbytes);
    if (!row) { fclose(f); return; }

    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (unsigned char)filesize;        hdr[3] = (unsigned char)(filesize >> 8);
    hdr[4] = (unsigned char)(filesize >> 16); hdr[5] = (unsigned char)(filesize >> 24);
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = (unsigned char)SCR_W; hdr[19] = (unsigned char)(SCR_W >> 8);
    hdr[22] = (unsigned char)SCR_H; hdr[23] = (unsigned char)(SCR_H >> 8);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (y = SCR_H - 1; y >= 0; --y)
    {
        const unsigned short *s = src + (long)y * SCR_W;
        memset(row, 0, (size_t)rowbytes);
        for (x = 0; x < SCR_W; ++x)
        {
            unsigned short p = s[x];                 /* RGB565 */
            row[x*3+0] = (unsigned char)(( p        & 0x1F) << 3);
            row[x*3+1] = (unsigned char)(((p >>  5) & 0x3F) << 2);
            row[x*3+2] = (unsigned char)(((p >> 11) & 0x1F) << 3);
        }
        fwrite(row, 1, (size_t)rowbytes, f);
    }
    fclose(f);
    free(row);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    GrHwConfiguration hw;
    GrTexInfo ti;
    FxU32 texaddr;
    LARGE_INTEGER qf, t0, t1;
    int ntri = 200, nframes = 300, frame, i;
    double secs;
    long tris = 0;
    long pixels = 0;
    int fillmode = 0;

    (void)inst; (void)prev; (void)show;
    if (cmd && *cmd)
    {
        int a = 0, b = 0;
        if (sscanf(cmd, "%d %d", &a, &b) >= 1) { if (a > 0) ntri = a; if (b > 0) nframes = b; }
        if (strstr(cmd, "fill")) fillmode = 1;
    }

    report("CNC3D Glide proof of life");
    report("requested: %d triangles x %d frames at %dx%d", ntri, nframes, SCR_W, SCR_H);

    memset(&hw, 0, sizeof hw);
    grGlideInit();
    if (!grSstQueryHardware(&hw)) { report("grSstQueryHardware FAILED: no 3dfx board"); return 1; }
    report("boards: %d", hw.num_sst);
    grSstSelect(0);

    if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1))
    { report("grSstWinOpen FAILED"); grGlideShutdown(); return 1; }
    report("window opened");

    /* TMU budget, read rather than guessed. The earlier project learned to print this
     * before adding a texture: the Voodoo 2's TMU is exactly 4 MB and it is not
     * virtualised, so overrunning it silently degrades to untextured. */
    report("tmu0 min=%lu max=%lu (%.2f MB)",
           (unsigned long)grTexMinAddress(GR_TMU0),
           (unsigned long)grTexMaxAddress(GR_TMU0),
           (grTexMaxAddress(GR_TMU0) - grTexMinAddress(GR_TMU0)) / 1048576.0);

    build_texture();
    ti.smallLod = ti.largeLod = GR_LOD_256;
    ti.aspectRatio = GR_ASPECT_1x1;
    ti.format = GR_TEXFMT_P_8;                 /* 8-bit palettised, natively */
    ti.data = g_tex;
    texaddr = grTexMinAddress(GR_TMU0);
    grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, g_pal);
    grTexDownloadMipMap(GR_TMU0, texaddr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, texaddr, GR_MIPMAPLEVELMASK_BOTH, &ti);
    report("texture: 256x256 GR_TEXFMT_P_8 uploaded at 0x%lx", (unsigned long)texaddr);

    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthBufferFunction(GR_CMP_LESS);
    grDepthMask(FXTRUE);

    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&t0);

    for (frame = 0; frame < nframes; ++frame)
    {
        float ph = (float)frame * 0.05f;
        grBufferClear(0x00203040, 0, GR_WDEPTHVALUE_FARTHEST);
        if (fillmode)
        {
            /* FILL RATE, exactly. ntri full-screen quads, so the pixel count is known to
             * the pixel instead of estimated from triangle areas: every quad is exactly
             * SCR_W * SCR_H texels of overdraw, all of them textured and depth tested.
             * Depth is stepped toward the viewer so none of them is rejected early. */
            for (i = 0; i < ntri; ++i)
            {
                GrVertex q[4];
                float w = 100.0f - (float)i;      /* nearer each time: never z-culled */
                float oow;
                int k;
                if (w < 1.0f) w = 1.0f;
                oow = 1.0f / w;
                q[0].x = 0.0f;         q[0].y = 0.0f;
                q[1].x = (float)SCR_W; q[1].y = 0.0f;
                q[2].x = (float)SCR_W; q[2].y = (float)SCR_H;
                q[3].x = 0.0f;         q[3].y = (float)SCR_H;
                q[0].tmuvtx[0].sow = 0.0f;   q[0].tmuvtx[0].tow = 0.0f;
                q[1].tmuvtx[0].sow = 255.0f; q[1].tmuvtx[0].tow = 0.0f;
                q[2].tmuvtx[0].sow = 255.0f; q[2].tmuvtx[0].tow = 255.0f;
                q[3].tmuvtx[0].sow = 0.0f;   q[3].tmuvtx[0].tow = 255.0f;
                for (k = 0; k < 4; ++k)
                {
                    q[k].z = 1.0f; q[k].oow = oow; q[k].ooz = 65535.0f / w;
                    q[k].r = 200.0f; q[k].g = 200.0f; q[k].b = 200.0f; q[k].a = 255.0f;
                    q[k].tmuvtx[0].sow *= oow;
                    q[k].tmuvtx[0].tow *= oow;
                    q[k].tmuvtx[0].oow  = oow;
                }
                grDrawTriangle(&q[0], &q[1], &q[2]);
                grDrawTriangle(&q[0], &q[2], &q[3]);
                tris += 2;
                pixels += (long)SCR_W * SCR_H;
            }
            grBufferSwap(0);
            continue;
        }

        for (i = 0; i < ntri; ++i)
        {
            GrVertex v[3];
            float a = ph + (float)i * 0.37f;
            float cxp = (float)SCR_W * 0.5f + (float)cos(a) * 200.0f;
            float cyp = (float)SCR_H * 0.5f + (float)sin(a * 1.3f) * 150.0f;
            float sz = 60.0f + 40.0f * (float)sin(a * 0.7f);
            float w  = 2.0f + (float)i * 0.01f;      /* spread them in depth */
            int k;
            v[0].x = cxp;      v[0].y = cyp - sz;
            v[1].x = cxp + sz; v[1].y = cyp + sz;
            v[2].x = cxp - sz; v[2].y = cyp + sz;
            v[0].tmuvtx[0].sow = 0.0f;   v[0].tmuvtx[0].tow = 0.0f;
            v[1].tmuvtx[0].sow = 255.0f; v[1].tmuvtx[0].tow = 0.0f;
            v[2].tmuvtx[0].sow = 0.0f;   v[2].tmuvtx[0].tow = 255.0f;
            for (k = 0; k < 3; ++k)
            {
                float oow = 1.0f / w;
                v[k].z = 1.0f;
                v[k].oow = oow;
                v[k].ooz = 65535.0f / w;
                /* CLAMP. Glide WRAPS an iterated colour component above 255 to near
                 * black instead of clamping, which is the single nastiest bug this
                 * hardware has: brightened geometry goes dark and the art looks fine. */
                v[k].r = 200.0f; v[k].g = 200.0f; v[k].b = 200.0f; v[k].a = 255.0f;
                v[k].tmuvtx[0].sow *= oow;
                v[k].tmuvtx[0].tow *= oow;
                v[k].tmuvtx[0].oow  = oow;
            }
            grDrawTriangle(&v[0], &v[1], &v[2]);
            ++tris;
        }
        grBufferSwap(0);            /* uncapped: we are measuring, not presenting */
    }

    QueryPerformanceCounter(&t1);
    secs = (double)(t1.QuadPart - t0.QuadPart) / (double)qf.QuadPart;

    grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 0, 0, SCR_W, SCR_H, SCR_W * 2, g_read);
    grSstWinClose();
    grGlideShutdown();

    save_bmp("glidetest.bmp", g_read);

    report("mode=%s", fillmode ? "fillrate (full-screen quads)" : "scattered triangles");
    report("frames=%d triangles=%ld elapsed=%.3fs", nframes, tris, secs);
    if (pixels)
        report("MPIXELS_PER_SEC=%.1f  (%ld pixels drawn)",
               secs > 0 ? (double)pixels / secs / 1.0e6 : 0.0, pixels);
    report("FPS=%.2f", secs > 0 ? nframes / secs : 0.0);
    report("triangles_per_sec=%.0f", secs > 0 ? tris / secs : 0.0);
    report("OK");
    return 0;
}
