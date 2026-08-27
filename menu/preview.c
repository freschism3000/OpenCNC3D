/*
 * preview.c -- standalone preview of the DOS main menu.
 *
 *   ./preview                       SDL2 window, OpenGL 1.1, the menu is live
 *   ./preview -o out.png -s 3       write a PNG at 3x and exit (no window, no GL)
 *   ./preview --press START         force a button into its pressed state, for shots
 *   ./preview --select LOAD         move the highlight, as the arrow keys would
 *   ./preview --music FWP           play a different theme, or --no-music for none
 *
 * The windowed half of this program is now three calls into dosmenu_shell.c, which
 * is the SAME code the shipping binary runs: preview and game must not have two
 * menu loops that can drift apart. What is left here is the still-picture half --
 * the PNG dumps and the fade strip -- which needs no window at all.
 *
 * The window renders at 320x200 scaled by an integer factor. DOS ran this in a
 * 320x200 mode that the monitor stretched to 4:3, so the pixels were not square;
 * that correction is a display decision and is deliberately not baked in here.
 */

#include "dosmenu.h"
#include "dosops.h"
#ifndef DB_NO_SDL
#include "dosmenu_shell.h"
#include "audioboot.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* init.cpp:1499 Play_Movie("LOGO", THEME_NONE, false) runs before the menu appears,
 * and LOGO.VQA ends on the title plate, which is why the menu can simply take over
 * the screen when it finishes. init.cpp:1205 Play_Intro() is what the Intro button
 * reaches; on the 1995 discs that is INTRO2.VQA. */
#define DM_MOVIE_LOGO "../dosdata/movies/LOGO.VQA"
#define DM_MOVIE_INTRO "../dosdata/movies/INTRO2.VQA"

/* init.cpp:796 queues THEME_MAP1 immediately before Main_Menu(), so that is what
 * plays here. MAP1.AUD is NOT in SCORES.MIX with the rest of the score: it lives in
 * TRANSIT.MIX, the archive the installer puts on the hard disk, together with
 * WIN1.AUD. That is why a search of both discs' SCORES.MIX came up empty. 60.2
 * seconds, which matches the 61 the theme table claims. Change it with --music. */
#define DM_MUSIC_MENU "MAP1"   /* a theme base name; the bank finds the file */

/* defines.h:2226 FADE_PALETTE_MEDIUM is TIMER_SECOND/4, and TIMER_SECOND is 60, so a
 * quarter of a second. That is the fade the engine uses either side of a movie. */
#define DM_FADE_MS 250

/* ======================================================================== *
 * Minimal PNG writer: stored deflate, so no zlib. Same one the sidebar
 * preview uses; kept local so this tool has no build dependency on it.
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
    ihdr[8] = 8; /* bit depth */
    ihdr[9] = 6; /* RGBA      */
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

/* The fade arithmetic, in one place so the windowed fade and the -o fade strip
 * cannot disagree. `level` is 0..256. The DOS engine faded the VGA palette
 * (Fade_Palette_To); multiplying the finished pixels is the same arithmetic one
 * step later, and on the Voodoo 2 it is a constant colour modulate on the quad. */
static void fade_rgba(unsigned char *dst, const unsigned char *src, int level)
{
    long i;
    if (level < 0)
        level = 0;
    if (level > 256)
        level = 256;
    for (i = 0; i < (long)DM_SCREEN_W * DM_SCREEN_H; i++) {
        dst[i * 4 + 0] = (unsigned char)((src[i * 4 + 0] * level) >> 8);
        dst[i * 4 + 1] = (unsigned char)((src[i * 4 + 1] * level) >> 8);
        dst[i * 4 + 2] = (unsigned char)((src[i * 4 + 2] * level) >> 8);
        dst[i * 4 + 3] = src[i * 4 + 3];
    }
}

/* ======================================================================== */

static int item_by_name(const char *name)
{
    static const char *const names[DM_ITEM_COUNT] = {"START", "LOAD", "MULTI",
                                                     "INTRO", "EXIT"};
    int i;
    for (i = 0; i < DM_ITEM_COUNT; i++)
        if (!strcmp(name, names[i]))
            return i;
    return -1;
}

int main(int argc, char **argv)
{
    const char *packpath = "dosmenu.pack";
    const char *outpng = NULL;
    int ops_demo = 0;   /* --ops N: draw the SPECIAL OPS list with row N selected */
    const char *version = "CNC3D";
    const char *logopath = DM_MOVIE_LOGO;
    const char *intropath = DM_MOVIE_INTRO;
    const char *musicpath = DM_MUSIC_MENU;
    const char *dosdata = "../dosdata";   /* SOUNDS.MIX and the loose music/ */
    int scale = 3, cursor = 0, nologo = 0, nomusic = 0, fadestrip = 0, i;
    char err[256];
    DB_Pack *pack;
    DB_Surface surf;
    DM_State st;
    unsigned char screen[DM_SCREEN_W * DM_SCREEN_H];
    unsigned char *rgba;

    dm_state_init(&st);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            packpath = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            outpng = argv[++i];
        else if (!strcmp(argv[i], "--ops"))
            ops_demo = (i + 1 < argc && argv[i + 1][0] != '-') ? atoi(argv[++i]) + 1 : 1;
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--version-text") && i + 1 < argc)
            version = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 1 < argc)
            st.pressed = item_by_name(argv[++i]);
        else if (!strcmp(argv[i], "--select") && i + 1 < argc)
            st.selected = item_by_name(argv[++i]);
        else if (!strcmp(argv[i], "--cursor"))
            cursor = 1;
        else if (!strcmp(argv[i], "--no-logo"))
            nologo = 1;
        else if (!strcmp(argv[i], "--logo") && i + 1 < argc)
            logopath = argv[++i];
        else if (!strcmp(argv[i], "--intro") && i + 1 < argc)
            intropath = argv[++i];
        else if (!strcmp(argv[i], "--music") && i + 1 < argc)
            musicpath = argv[++i];
        else if (!strcmp(argv[i], "--dosdata") && i + 1 < argc)
            dosdata = argv[++i];
        else if (!strcmp(argv[i], "--no-music"))
            nomusic = 1;
        else if (!strcmp(argv[i], "--fade-strip"))
            fadestrip = 1;
        else {
            fprintf(stderr,
                    "usage: %s [-p pack] [-o out.png] [-s scale]\n"
                    "          [--select ITEM] [--press ITEM] [--cursor] [--version-text S]\n"
                    "          [--no-logo] [--logo LOGO.VQA] [--intro INTRO2.VQA]\n"
                    "          [--music THEME] [--no-music] [--dosdata DIR]\n"
                    "   ITEM: START LOAD MULTI INTRO EXIT\n",
                    argv[0]);
            return 2;
        }
    }
    if (scale < 1)
        scale = 1;
    st.version = version;
    if (st.selected < 0)
        st.selected = DM_START;

    pack = db_pack_load(packpath, err, sizeof err);
    if (!pack) {
        fprintf(stderr, "preview: %s\n", err);
        return 1;
    }

    db_surface_init(&surf, DM_SCREEN_W, DM_SCREEN_H, screen);
    memset(screen, DB_TBLACK, sizeof screen);
    if (ops_demo) {
        /* --ops draws the SPECIAL OPS list instead of the menu, over the same plate,
           on a made-up mission list. It is a LOOK check for the screen's layout, not a
           test of the scanner: the real list is built by the app from the missions
           folder (see specops_scan in app/cnc3d.cpp). */
        static const DO_Mission demo[] = {
            {"SCG22EA", "Blackout", 0},        {"SCG23EA", "Hell's Fury", 0},
            {"SCG30EA", "N64 Special Ops 1", 0},{"SCG32EA", "Test Map: Dynamic", 0},
            {"SCG36EA", "Infiltrated!", 0},    {"SCG38EA", "Elemental Imperative", 0},
            {"SCG40EA", "Ground Zero", 0},     {"SCG41EA", "Twist of Fate", 0},
            {"SCG50EA", "Blindsided", 0},      {"SCG60EA", "Special Ops 1", 0},
            {"SCG61EA", "Special Ops 2", 0},   {"SCG62EA", "Special Ops 3", 0},
            {"SCG71EB", "GDI 3 Variant", 0},   {"SCG72EA", "GDI 1 Variant", 0},
            {"SCG73EA", "Test Map: Dynamic", 0},{"SCG74EA", "Blindsided", 0},
            {"SCB20EA", "Bad Neighborhood", 1},{"SCB21EA", "Eviction Notice", 1},
            {"SCB22EB", "N64 Special Ops 2", 1},{"SCB31EA", "The Tiberium Strain", 1},
            {"SCB32EA", "Cloak and Dagger", 1},{"SCB33EA", "Hostile Takeover", 1},
            {"SCB35EA", "Under Siege: C&C", 1},{"SCB37EA", "Nod Death Squad", 1},
            {"SCB60EA", "Special Ops 1", 1},   {"SCB61EA", "Special Ops 2", 1},
            {"SCB70EA", "Test Map: Static", 1}
        };
        DO_State ops;
        do_state_init(&ops, demo, (int)(sizeof demo / sizeof demo[0]));
        ops.selected = ops_demo - 1;
        do_move(&ops, 0);   /* clamp and scroll the selection into view */
        dm_draw_plate(&surf, pack);
        do_draw(&surf, pack, &ops);
    } else {
        dm_draw_menu(&surf, pack, &st);
    }
    if (cursor)
        dm_draw_cursor(&surf, pack, 160, 100);

    rgba = (unsigned char *)malloc((size_t)DM_SCREEN_W * DM_SCREEN_H * 4);
    db_surface_to_rgba(&surf, pack->pal8, rgba, 0);

    if (outpng && fadestrip) {
        /* The same frames the windowed fade produces, written out so the dissolve
         * can be checked without a screen. */
        static const int levels[5] = {256, 192, 128, 64, 0};
        unsigned char *tmp = (unsigned char *)malloc((size_t)DM_SCREEN_W * DM_SCREEN_H * 4);
        unsigned char *big = (unsigned char *)malloc(
            (size_t)DM_SCREEN_W * scale * DM_SCREEN_H * scale * 4);
        char path[512];
        for (i = 0; i < 5; i++) {
            fade_rgba(tmp, rgba, levels[i]);
            nearest_scale(tmp, DM_SCREEN_W, DM_SCREEN_H, big, scale);
            snprintf(path, sizeof path, "%s_fade%d.png", outpng, levels[i]);
            write_png(path, big, DM_SCREEN_W * scale, DM_SCREEN_H * scale);
            printf("wrote %s (level %d/256)\n", path, levels[i]);
        }
        free(tmp);
        free(big);
        free(rgba);
        db_pack_free(pack);
        return 0;
    }

    if (outpng) {
        unsigned char *big = (unsigned char *)malloc(
            (size_t)DM_SCREEN_W * scale * DM_SCREEN_H * scale * 4);
        nearest_scale(rgba, DM_SCREEN_W, DM_SCREEN_H, big, scale);
        if (!write_png(outpng, big, DM_SCREEN_W * scale, DM_SCREEN_H * scale)) {
            fprintf(stderr, "preview: cannot write %s\n", outpng);
            return 1;
        }
        printf("wrote %s  %dx%d (%dx%d at %dx)\n", outpng, DM_SCREEN_W * scale,
               DM_SCREEN_H * scale, DM_SCREEN_W, DM_SCREEN_H, scale);
        free(big);
        free(rgba);
        db_pack_free(pack);
        return 0;
    }

#ifdef DB_NO_SDL
    fprintf(stderr, "preview: built without SDL; use -o to write a PNG\n");
    free(rgba);
    db_pack_free(pack);
    return 1;
#else
    /* The live window. Everything below is the shipping menu, not a copy of it: one
     * window, one context, and the same dms_* calls cnc3d.cpp makes. That is the
     * point of the split -- a change to how the menu behaves lands in both places or
     * in neither. */
    free(rgba);
    db_pack_free(pack);
    {
        DMS menu;
        DMS_Config cfg;
        SDL_Window *win;
        SDL_GLContext ctx;
        char e[256];
        int choice;

        AudioBootOpts ab;
        CncAudio *au;

        memset(&cfg, 0, sizeof cfg);
        cfg.pack = packpath;
        cfg.version = version;
        cfg.music = nomusic ? NULL : musicpath;
        cfg.intro = intropath;
        cfg.logo = nologo ? NULL : logopath;

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "preview: SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        win = SDL_CreateWindow("Command & Conquer 3D -- DOS main menu",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               DM_SCREEN_W * scale, DM_SCREEN_H * scale,
                               SDL_WINDOW_OPENGL);
        if (!win) {
            fprintf(stderr, "preview: SDL_CreateWindow: %s\n", SDL_GetError());
            return 1;
        }
        ctx = SDL_GL_CreateContext(win);
        if (!ctx) {
            fprintf(stderr, "preview: SDL_GL_CreateContext: %s\n", SDL_GetError());
            return 1;
        }
        /* The preview is a program too, so it owns the one audio engine exactly the
           way the shipping app does. The menu never opens a device itself. */
        memset(&ab, 0, sizeof ab);
        ab.dosdata = dosdata;
        ab.music_vol255 = -1;   /* -1 is "not specified"; 0 would mean muted */
        ab.sound_vol255 = -1;
        au = audio_boot(&ab);
        cfg.au = au;
        if (!dms_open(&menu, win, &cfg, e, sizeof e)) {
            fprintf(stderr, "preview: %s\n", e);
            return 1;
        }
        if (st.selected >= 0)
            menu.st.selected = st.selected;
        if (st.pressed >= 0)
            menu.st.pressed = st.pressed;

        if (!dms_logo(&menu)) {
            dms_close(&menu);
            audio_boot_shutdown(au);
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 0;
        }

        /* Standalone, there is nothing behind the menu, so anything that is not Exit
         * simply puts it back. In the real program cnc3d.cpp is what decides. */
        for (;;) {
            choice = dms_run(&menu);
            if (choice == DMS_QUIT || choice == DM_EXIT)
                break;
            printf("preview: %s selected (no game in this build)\n",
                   dm_item_label(choice));
        }

        dms_close(&menu);
        audio_boot_shutdown(au);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
    }
    return 0;
#endif
}
