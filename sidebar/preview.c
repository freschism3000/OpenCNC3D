/*
 * preview.c -- standalone SDL2 + OpenGL 1.1 viewer for dosbar.c.
 *
 * Draws ONLY the sidebar, at the real 320x200 DOS layout, upscaled by an integer
 * factor with GL_NEAREST so it stays crisp. The GL side is deliberately trivial and
 * is the same thing the Win98 / Voodoo 2 renderer will do: one RGBA texture in a
 * power-of-two box, one ortho-projected quad, GL_NEAREST both ways. No shaders, no
 * GL 2.0, no GLU. Everything else happens on the CPU inside dosbar.c.
 *
 *   ./preview                       open a window at 4x
 *   ./preview -s 6                  open a window at 6x
 *   ./preview -o out.png -s 3       write a PNG at 3x and exit (no window, no GL)
 *   ./preview --full                include the whole 320x200 screen, not just x>=240
 *   ./preview --hold                put the in-progress item on hold (HOLD pip)
 *
 * The PNG writer is 60 lines of stored-deflate so this file has no dependency
 * beyond SDL2 and libc; it exists so the render can be checked without a display.
 */

#include "dosbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DB_NO_SDL
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#endif

/* ======================================================================== *
 * The fake state.
 *
 * The buildable lists and the darkening follow the engine's own rules
 * (sidebar.cpp:1820-1869): `isbusy` is per RTTI, so while a BUILDING is under
 * construction every other structure cameo darkens, and the same for units. That
 * is why column one is mostly grey here: it is what C&C actually looks like
 * mid-production, not a bug.
 * ======================================================================== */

static void build_fake_state(DB_State *st, int hold)
{
    int i;
    /* Column 0: structures.  Column 1: infantry + vehicles + aircraft. */
    static const char *structures[] = {"FACT", "PROC", "SILO", "PYLE", "WEAP", "HQ", "NUKE", "GTWR"};
    static const char *units[] = {"E1", "E2", "E3", "JEEP", "MTNK", "APC", "MSAM", "HARV", "ORCA"};

    db_state_clear(st);
    for (i = 0; i < (int)(sizeof structures / sizeof *structures); i++)
        db_state_add(st, 0, structures[i]);
    for (i = 0; i < (int)(sizeof units / sizeof *units); i++)
        db_state_add(st, 1, units[i]);

    /* Both strips are scrolled, so the scroll arrows mean something. */
    st->top[0] = 1; /* visible: PROC SILO PYLE WEAP */
    st->top[1] = 2; /* visible: E3 JEEP MTNK APC   */

    /* A refinery is 40% built.  Completion() runs 0..107 (factory.h STEP_COUNT=108),
     * so 40% is stage 43, drawn as CLOCK.SHP frame 44. */
    st->items[0][1].producing = 1;
    st->items[0][1].completion = (DB_STEP_COUNT * 40) / 100; /* 43 */
    st->items[0][1].onhold = hold;

    /* ...so every other structure is unavailable and darkens. */
    for (i = 0; i < st->count[0]; i++)
        if (i != 1)
            st->items[0][i].darken = 1;

    /* An infantry squad has finished and is waiting: READY.  Its factory is the
     * infantry one, so the vehicles in the same column stay buildable. */
    st->items[1][2].producing = 1;
    st->items[1][2].ready = 1;
    st->items[1][0].darken = 1; /* E1, E2 scrolled off, but darkened all the same */
    st->items[1][1].darken = 1;

    st->repair_pressed = 0;
    st->repair_on = 0;
    st->sell_pressed = 0;
    st->map_disabled = 0;
    st->radar_active = 1;

    /* power.cpp bounds both to 0..PowHeight-2 == 0..108 */
    st->power_total = 74;
    st->power_drain = 51;
}

/* ------------------------------------------------------------------------ *
 * The radar CONTENTS are invented: this preview has no map, no cells and no
 * houses, so there is nothing real to plot. The radar FRAME above is exact
 * (radar.cpp:1269) and so are the cursor brackets (radar.cpp:1198-1211, four
 * LTGREEN corners, never a full rectangle). The blips below are a stand-in and
 * are the only pixels in this program that are not derived from the engine.
 * ------------------------------------------------------------------------ */
static void fake_radar(DB_Surface *s)
{
    /* The map area is the whole radar interior: RadX+RadOffX .. +RadIWidth-1 by
     * RadY+RadOffY .. +RadIHeight-1, which radar.cpp:433 has just filled BLACK.
     * Terrain indices 60/64/69 and tiberium 41 are real TEMPERAT.PAL entries, but
     * WHICH cell gets which colour is made up. */
    const int cw = DB_RAD_I_WIDTH, ch = DB_RAD_I_HEIGHT;
    const int ox = DB_RAD_X + DB_RAD_OFF_X;
    const int oy = DB_RAD_Y + DB_RAD_OFF_Y;
    unsigned int seed = 0x1995u;
    int x, y, i;

    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            unsigned char c;
            seed = seed * 1103515245u + 12345u;
            {
                unsigned int r = (seed >> 16) & 0xFF;
                int shrouded = (x < 3 || x > cw - 4 || y < 3 || y > ch - 4);
                if (shrouded)
                    c = DB_BLACK;
                else if (r < 26)
                    c = 60;
                else if (r < 40)
                    c = 69;
                else
                    c = 64;
                if (!shrouded && x > 34 && x < 52 && y > 14 && y < 30 && (r & 3) == 0)
                    c = 41; /* a tiberium field reads green on the radar */
            }
            db_put_pixel(s, ox + x, oy + y, c);
        }
    /* Your base, GDI yellow; the enemy, Nod red. Cell_Color returns the owner's
     * house colour, and lo-res plots one pixel per cell. */
    for (i = 0; i < 34; i++) {
        seed = seed * 1103515245u + 12345u;
        db_put_pixel(s, ox + 14 + (int)((seed >> 17) % 13), oy + 36 + (int)((seed >> 9) % 13), DB_YELLOW);
    }
    for (i = 0; i < 18; i++) {
        seed = seed * 1103515245u + 12345u;
        db_put_pixel(s, ox + 46 + (int)((seed >> 17) % 11), oy + 8 + (int)((seed >> 9) % 11), DB_RED);
    }

    /* radar.cpp:1198-1211 Radar_Cursor: four LTGREEN corner brackets. */
    {
        int x1 = ox + 10, y1 = oy + 28, x2 = ox + 33, y2 = oy + 50, barlen = 4;
        db_line_h(s, x1, x1 + barlen, y1, DB_LTGREEN);
        db_line_v(s, x1, y1, y1 + barlen, DB_LTGREEN);
        db_line_h(s, x2 - barlen, x2, y1, DB_LTGREEN);
        db_line_v(s, x2, y1, y1 + barlen, DB_LTGREEN);
        db_line_v(s, x1, y2 - barlen, y2, DB_LTGREEN);
        db_line_h(s, x1, x1 + barlen, y2, DB_LTGREEN);
        db_line_v(s, x2, y2 - barlen, y2, DB_LTGREEN);
        db_line_h(s, x2 - barlen, x2, y2, DB_LTGREEN);
    }
}

/* ======================================================================== *
 * Minimal PNG writer: stored deflate, so no zlib.
 * ======================================================================== */

static unsigned int crc_table[256];
static int crc_ready = 0;

static unsigned int crc32_buf(unsigned int crc, const unsigned char *b, long n)
{
    long i;
    if (!crc_ready) {
        int k, j;
        for (k = 0; k < 256; k++) {
            unsigned int c = (unsigned int)k;
            for (j = 0; j < 8; j++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[k] = c;
        }
        crc_ready = 1;
    }
    for (i = 0; i < n; i++)
        crc = crc_table[(crc ^ b[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void be32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void png_chunk(FILE *f, const char *tag, const unsigned char *data, long n)
{
    unsigned char hdr[4];
    unsigned int crc;
    be32(hdr, (unsigned int)n);
    fwrite(hdr, 1, 4, f);
    fwrite(tag, 1, 4, f);
    if (n)
        fwrite(data, 1, (size_t)n, f);
    crc = crc32_buf(0xFFFFFFFFu, (const unsigned char *)tag, 4);
    crc = crc32_buf(crc, data, n) ^ 0xFFFFFFFFu;
    be32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

static int write_png(const char *path, const unsigned char *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    unsigned char ihdr[13];
    unsigned char *raw, *z;
    long rawlen, zlen, pos, i;
    unsigned int a = 1, b = 0;

    if (!f)
        return 0;
    fwrite("\211PNG\r\n\032\n", 1, 8, f);
    be32(ihdr, (unsigned int)w);
    be32(ihdr + 4, (unsigned int)h);
    ihdr[8] = 8;  /* bit depth  */
    ihdr[9] = 6;  /* RGBA       */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);

    rawlen = (long)h * (1 + (long)w * 4);
    raw = (unsigned char *)malloc((size_t)rawlen);
    for (i = 0; i < h; i++) {
        raw[i * (1 + (long)w * 4)] = 0; /* filter: none */
        memcpy(raw + i * (1 + (long)w * 4) + 1, rgba + i * (long)w * 4, (size_t)w * 4);
    }
    for (i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }

    zlen = 2 + rawlen + 5 * ((rawlen + 65534) / 65535) + 4;
    z = (unsigned char *)malloc((size_t)zlen);
    pos = 0;
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    for (i = 0; i < rawlen;) {
        long n = rawlen - i;
        int final;
        if (n > 65535)
            n = 65535;
        final = (i + n >= rawlen);
        z[pos++] = (unsigned char)final;
        z[pos++] = (unsigned char)(n & 0xFF);
        z[pos++] = (unsigned char)(n >> 8);
        z[pos++] = (unsigned char)(~n & 0xFF);
        z[pos++] = (unsigned char)((~n >> 8) & 0xFF);
        memcpy(z + pos, raw + i, (size_t)n);
        pos += n;
        i += n;
    }
    be32(z + pos, (b << 16) | a);
    pos += 4;
    png_chunk(f, "IDAT", z, pos);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}

/* ======================================================================== *
 * main
 * ======================================================================== */

static void nearest_scale(const unsigned char *src, int sw, int sh, unsigned char *dst, int scale)
{
    int y, x, k;
    for (y = 0; y < sh * scale; y++)
        for (x = 0; x < sw * scale; x++) {
            const unsigned char *s = src + ((long)(y / scale) * sw + (x / scale)) * 4;
            unsigned char *d = dst + ((long)y * sw * scale + x) * 4;
            for (k = 0; k < 4; k++)
                d[k] = s[k];
        }
}

#ifndef DB_NO_SDL
/* GL 1.1 and Glide both want power-of-two textures. */
static int pot(int v)
{
    int p = 1;
    while (p < v)
        p *= 2;
    return p;
}
#endif

int main(int argc, char **argv)
{
    const char *packpath = "dossidebar.pack";
    const char *outpng = NULL;
    int scale = 4, full = 0, hold = 0;
    char err[256];
    DB_Pack *pack;
    DB_Surface surf;
    DB_State st;
    unsigned char screen[DB_SCREEN_W * DB_SCREEN_H];
    unsigned char *rgba;
    int i, cx, cw, ch;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            packpath = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            outpng = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--full"))
            full = 1;
        else if (!strcmp(argv[i], "--hold"))
            hold = 1;
        else {
            fprintf(stderr, "usage: %s [-p pack] [-o out.png] [-s scale] [--full] [--hold]\n", argv[0]);
            return 2;
        }
    }
    if (scale < 1)
        scale = 1;

    pack = db_pack_load(packpath, err, sizeof err);
    if (!pack) {
        fprintf(stderr, "preview: %s\n", err);
        return 1;
    }

    db_surface_init(&surf, DB_SCREEN_W, DB_SCREEN_H, screen);
    /* The tactical area is not ours; leave it the black the engine leaves. */
    db_fill_rect(&surf, 0, 0, DB_SCREEN_W - 1, DB_SCREEN_H - 1, DB_BLACK);

    build_fake_state(&st, hold);
    db_draw_sidebar(&surf, pack, &st);
    fake_radar(&surf);

    cx = full ? 0 : DB_SIDE_X;
    cw = DB_SCREEN_W - cx;
    ch = DB_SCREEN_H;

    rgba = (unsigned char *)malloc((size_t)cw * ch * 4);
    {
        DB_Surface crop;
        unsigned char *cpx = (unsigned char *)malloc((size_t)cw * ch);
        int y;
        for (y = 0; y < ch; y++)
            memcpy(cpx + (long)y * cw, screen + (long)y * DB_SCREEN_W + cx, (size_t)cw);
        crop.w = cw;
        crop.h = ch;
        crop.px = cpx;
        db_surface_to_rgba(&crop, pack->pal8, rgba, 0);
        free(cpx);
    }

    if (outpng) {
        unsigned char *big = (unsigned char *)malloc((size_t)cw * scale * ch * scale * 4);
        nearest_scale(rgba, cw, ch, big, scale);
        if (!write_png(outpng, big, cw * scale, ch * scale)) {
            fprintf(stderr, "preview: cannot write %s\n", outpng);
            return 1;
        }
        printf("wrote %s  %dx%d (%dx%d at %dx)\n", outpng, cw * scale, ch * scale, cw, ch, scale);
        free(big);
        free(rgba);
        db_pack_free(pack);
        return 0;
    }

#ifdef DB_NO_SDL
    fprintf(stderr, "preview: built without SDL; use -o to write a PNG\n");
    return 1;
#else
    {
        SDL_Window *win;
        SDL_GLContext ctx;
        GLuint tex;
        int tw = pot(cw), th = pot(ch);
        unsigned char *padded;
        int running = 1;

        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "preview: SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        win = SDL_CreateWindow("C&C 1995 DOS sidebar", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cw * scale,
                               ch * scale, SDL_WINDOW_OPENGL);
        ctx = SDL_GL_CreateContext(win);
        (void)ctx;

        /* Power-of-two staging buffer: GL 1.1 and Glide both require it. */
        padded = (unsigned char *)calloc((size_t)tw * th * 4, 1);
        for (i = 0; i < ch; i++)
            memcpy(padded + (long)i * tw * 4, rgba + (long)i * cw * 4, (size_t)cw * 4);

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, padded);

        while (running) {
            SDL_Event e;
            float u = (float)cw / (float)tw, v = (float)ch / (float)th;
            while (SDL_PollEvent(&e))
                if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                    running = 0;

            glViewport(0, 0, cw * scale, ch * scale);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, 1, 1, 0, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex2f(0, 0);
            glTexCoord2f(u, 0);
            glVertex2f(1, 0);
            glTexCoord2f(u, v);
            glVertex2f(1, 1);
            glTexCoord2f(0, v);
            glVertex2f(0, 1);
            glEnd();
            SDL_GL_SwapWindow(win);
            SDL_Delay(16);
        }
        SDL_Quit();
        free(padded);
    }
    free(rgba);
    db_pack_free(pack);
    return 0;
#endif
}
