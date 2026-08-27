/*
 * playvqa.c -- standalone player for the Westwood movies, used to prove the decoder
 * before it goes anywhere near the menu.
 *
 *   ./playvqa ../dosdata/movies/LOGO.VQA            play it, any key or click skips
 *   ./playvqa LOGO.VQA -o shot -f 0,120,300 -s 3    dump those frames as PNGs
 *   ./playvqa LOGO.VQA --scan                       decode every frame, report, no window
 *
 * Video and audio both come from vqaplay.c. The only thing this file adds is a
 * window, a clock and a keyboard: one texture, one quad, GL_NEAREST, which is what
 * the Voodoo 2 build will do through Glide.
 */

#include "pngwrite.h"
#include "vqaplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VQ_NO_SDL
#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#define SCREEN_W 320
#define SCREEN_H 200

/* The frame is palettised; the screen it lands on is the same 320x200 the menu uses,
 * and a movie that is shorter than 200 rows is centred, the way the DOS player
 * centred the 320x156 intro. */
static void frame_to_rgba(const unsigned char *pix, int w, int h, const unsigned char *pal,
                          unsigned char *rgba)
{
    int y, x, oy = (SCREEN_H - h) / 2, ox = (SCREEN_W - w) / 2;

    memset(rgba, 0, (size_t)SCREEN_W * SCREEN_H * 4);
    for (y = 0; y < SCREEN_H * SCREEN_W; y++)
        rgba[y * 4 + 3] = 255;

    for (y = 0; y < h; y++) {
        if (y + oy < 0 || y + oy >= SCREEN_H)
            continue;
        for (x = 0; x < w; x++) {
            const unsigned char *c = pal + pix[y * w + x] * 3;
            unsigned char *d = rgba + (((long)(y + oy) * SCREEN_W) + x + ox) * 4;
            d[0] = c[0];
            d[1] = c[1];
            d[2] = c[2];
            d[3] = 255;
        }
    }
}

#ifndef VQ_NO_SDL
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
    const char *path = NULL;
    const char *outprefix = NULL;
    const char *wantframes = "0";
    int scale = 3, scan = 0, i;
    char err[256];
    VQ_Movie *m;
    unsigned char *rgba;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            outprefix = argv[++i];
        else if (!strcmp(argv[i], "-f") && i + 1 < argc)
            wantframes = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scan"))
            scan = 1;
        else if (argv[i][0] != '-')
            path = argv[i];
        else {
            fprintf(stderr, "usage: %s MOVIE.VQA [-o prefix -f 0,120,300] [-s scale] [--scan]\n",
                    argv[0]);
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "usage: %s MOVIE.VQA [-o prefix -f 0,120,300] [-s scale] [--scan]\n",
                argv[0]);
        return 2;
    }

    m = vq_open(path, err, sizeof err);
    if (!m) {
        fprintf(stderr, "playvqa: %s\n", err);
        return 1;
    }
    printf("%s: %dx%d, %d frames, %d fps, audio %d Hz x%d %d-bit\n", path, vq_width(m),
           vq_height(m), vq_frames(m), vq_fps(m), vq_sample_rate(m), vq_channels(m), 16);

    rgba = (unsigned char *)malloc((size_t)SCREEN_W * SCREEN_H * 4);

    if (scan || outprefix) {
        int n = 0, audio = 0;
        while (vq_next_frame(m) == 1) {
            char buf[512];
            const char *p = wantframes;
            audio += vq_audio_pending(m);
            {
                char sink[65536];
                while (vq_take_audio(m, sink, sizeof sink) > 0)
                    ;
            }
            if (outprefix) {
                while (*p) {
                    if (atoi(p) == n) {
                        unsigned char *big;
                        frame_to_rgba(vq_pixels(m), vq_width(m), vq_height(m), vq_palette(m), rgba);
                        big = (unsigned char *)malloc((size_t)SCREEN_W * scale * SCREEN_H * scale * 4);
                        png_nearest_scale(rgba, SCREEN_W, SCREEN_H, big, scale);
                        snprintf(buf, sizeof buf, "%s_%04d.png", outprefix, n);
                        png_write_rgba(buf, big, SCREEN_W * scale, SCREEN_H * scale);
                        free(big);
                        printf("wrote %s\n", buf);
                        break;
                    }
                    while (*p && *p != ',')
                        p++;
                    if (*p == ',')
                        p++;
                }
            }
            n++;
        }
        printf("decoded %d frames, %d bytes of PCM\n", n, audio);
        free(rgba);
        vq_close(m);
        return 0;
    }

#ifdef VQ_NO_SDL
    fprintf(stderr, "playvqa: built without SDL; use --scan or -o\n");
    return 1;
#else
    {
        SDL_Window *win;
        SDL_GLContext ctx;
        SDL_AudioSpec want, got;
        SDL_AudioDeviceID dev = 0;
        GLuint tex;
        int tw = pot(SCREEN_W), th = pot(SCREEN_H);
        unsigned char *padded;
        unsigned int start;
        int frame = 0, running = 1;

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "playvqa: SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        win = SDL_CreateWindow("Command & Conquer 3D -- movie", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, SCREEN_W * scale, SCREEN_H * scale,
                               SDL_WINDOW_OPENGL);
        ctx = SDL_GL_CreateContext(win);
        (void)ctx;

        memset(&want, 0, sizeof want);
        want.freq = vq_sample_rate(m);
        want.format = AUDIO_S16SYS;
        want.channels = (Uint8)vq_channels(m);
        want.samples = 1024;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
        if (dev)
            SDL_PauseAudioDevice(dev, 0);

        padded = (unsigned char *)calloc((size_t)tw * th * 4, 1);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, padded);

        start = SDL_GetTicks();
        while (running) {
            SDL_Event e;
            float u = (float)SCREEN_W / (float)tw, v = (float)SCREEN_H / (float)th;
            unsigned int due;

            while (SDL_PollEvent(&e)) {
                /* conquer.cpp Play_Movie: any keystroke or button aborts the movie. */
                if (e.type == SDL_QUIT || e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN)
                    running = 0;
            }
            if (!running)
                break;

            due = start + (unsigned int)((long)frame * 1000 / vq_fps(m));
            if (SDL_GetTicks() < due) {
                SDL_Delay(1);
                continue;
            }

            if (vq_next_frame(m) != 1)
                break;
            frame++;

            if (dev) {
                unsigned char chunk[16384];
                int n;
                while ((n = vq_take_audio(m, chunk, (int)sizeof chunk)) > 0)
                    SDL_QueueAudio(dev, chunk, (Uint32)n);
            }

            frame_to_rgba(vq_pixels(m), vq_width(m), vq_height(m), vq_palette(m), rgba);
            for (i = 0; i < SCREEN_H; i++)
                memcpy(padded + (long)i * tw * 4, rgba + (long)i * SCREEN_W * 4,
                       (size_t)SCREEN_W * 4);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, padded);

            glViewport(0, 0, SCREEN_W * scale, SCREEN_H * scale);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, 1, 1, 0, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glEnable(GL_TEXTURE_2D);
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
        }

        printf("played %d frames\n", frame);
        if (dev)
            SDL_CloseAudioDevice(dev);
        SDL_Quit();
        free(padded);
    }
    free(rgba);
    vq_close(m);
    return 0;
#endif
}
