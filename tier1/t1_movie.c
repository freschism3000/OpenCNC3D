/* t1_movie.c -- see t1_movie.h. */

#include <stdio.h>
#include <string.h>
#include "t1_movie.h"
#include "t1_glide.h"

#define MV_PLATE_W 320
#define MV_PLATE_H 200
#define MV_SCALE   2
#define MV_TOP     ((480 - MV_PLATE_H * MV_SCALE) / 2)

int t1_movie_init(T1_Movie *mv, char *err, int errlen)
{
    memset(mv, 0, sizeof *mv);
    sr_texture(&mv->texA, mv->pageA, 256, 128);
    sr_texture(&mv->texB, mv->pageB,  64, 128);
    sr_texture(&mv->texC, mv->pageC, 256, 128);
    sr_texture(&mv->texD, mv->pageD,  64, 128);
    if (!t1_glide_upload(&mv->texA, err, errlen)) return 0;
    if (!t1_glide_upload(&mv->texB, err, errlen)) return 0;
    if (!t1_glide_upload(&mv->texC, err, errlen)) return 0;
    if (!t1_glide_upload(&mv->texD, err, errlen)) return 0;
    mv->uploaded = 1;
    return 1;
}

int t1_movie_play(T1_Movie *mv, const char *path, double now, char *err, int errlen)
{
    const char *slash;
    if (mv->m) { vq_close(mv->m); mv->m = 0; }
    mv->m = vq_open(path, err, errlen);
    if (!mv->m) return 0;
    mv->w   = vq_width(mv->m);
    mv->h   = vq_height(mv->m);
    mv->fps = vq_fps(mv->m);
    if (mv->fps < 1) mv->fps = 15;
    mv->frame = 0;
    mv->palchanges = 0;
    mv->done = 0;
    mv->t0 = now;
    slash = strrchr(path, '\\');
    strncpy(mv->name, slash ? slash + 1 : path, sizeof mv->name - 1);
    mv->name[sizeof mv->name - 1] = 0;
    /* A frame larger than the plate is refused rather than clipped: every C&C movie is
     * 320 wide, and one that is not would silently lose its right-hand side. */
    if (mv->w > MV_PLATE_W || mv->h > MV_PLATE_H)
    {
        _snprintf(err, errlen, "%dx%d does not fit the 320x200 plate", mv->w, mv->h);
        vq_close(mv->m); mv->m = 0;
        return 0;
    }
    memset(mv->pageA, 0, sizeof mv->pageA);
    memset(mv->pageB, 0, sizeof mv->pageB);
    memset(mv->pageC, 0, sizeof mv->pageC);
    memset(mv->pageD, 0, sizeof mv->pageD);
    return 1;
}

void t1_movie_stop(T1_Movie *mv)
{
    if (mv->m) { vq_close(mv->m); mv->m = 0; }
    mv->done = 1;
}

int t1_movie_playing(const T1_Movie *mv) { return mv->m != 0 && !mv->done; }

/* The decoded frame into the four pages. The movie is centred on the plate, which is
 * what the DOS player does with a 320x156 briefing: black above and below, never
 * stretched. No colour conversion of any kind: these are the card's own texel values. */
static void blit(T1_Movie *mv)
{
    const unsigned char *px = vq_pixels(mv->m);
    int oy = (MV_PLATE_H - mv->h) / 2;
    int y;
    if (!px) return;
    for (y = 0; y < mv->h; ++y)
    {
        int py = oy + y;
        const unsigned char *src = px + (long)y * mv->w;
        if (py < 0 || py >= MV_PLATE_H) continue;
        if (py < 128)
        {
            memcpy(mv->pageA + (long)py * 256, src, (size_t)(mv->w < 256 ? mv->w : 256));
            if (mv->w > 256)
                memcpy(mv->pageB + (long)py * 64, src + 256, (size_t)(mv->w - 256));
        }
        else
        {
            int by = py - 128;
            memcpy(mv->pageC + (long)by * 256, src, (size_t)(mv->w < 256 ? mv->w : 256));
            if (mv->w > 256)
                memcpy(mv->pageD + (long)by * 64, src + 256, (size_t)(mv->w - 256));
        }
    }
}

int t1_movie_frame(T1_Movie *mv, double now,
                   int (*push_audio)(const short *pcm, int samples))
{
    int want, got = 0;
    if (!mv->m || mv->done) return 0;

    /* HOW MANY FRAMES THE MOVIE'S OWN CLOCK IS OWED. The game loop runs at fifty-odd
     * frames a second and the movie at fifteen, so most game frames decode nothing and
     * simply redraw. Catching up is capped at four so a disk stall becomes a dropped
     * frame rather than a freeze. */
    want = (int)((now - mv->t0) * (double)mv->fps) - mv->frame;
    if (want > 4) want = 4;
    while (want-- > 0)
    {
        int r = vq_next_frame(mv->m);
        if (r <= 0) { t1_movie_stop(mv); break; }
        ++mv->frame;
        ++got;
        if (vq_palette_dirty(mv->m)) ++mv->palchanges;
        /* The audio arrives with the picture and goes straight into the mixer's own
         * MOVIE bus, which already runs at 22050 Hz -- the rate every one of these files
         * is in, so nothing resamples. */
        if (push_audio)
        {
            static short pcm[4096];
            int n;
            while ((n = vq_take_audio(mv->m, pcm, (int)sizeof pcm)) > 0)
                push_audio(pcm, n / 2);
        }
    }
    if (mv->done && got == 0) return 0;
    if (got) blit(mv);

    /* The palette is the movie's, and it is downloaded only when the decoder says it
     * changed: measured on the desktop build, that is once in GDI1's 549 frames. */
    if (got && vq_palette_dirty(mv->m)) t1_glide_palette(vq_palette(mv->m));
    else if (mv->frame == 1) t1_glide_palette(vq_palette(mv->m));

    t1_glide_begin(0x00000000);
    t1_glide_depth(0);
    t1_glide_ckey(0, 0);
    t1_glide_filter(0);
    if (got)
    {
        t1_glide_reupload(&mv->texA);
        t1_glide_reupload(&mv->texB);
        t1_glide_reupload(&mv->texC);
        t1_glide_reupload(&mv->texD);
    }
    {
        const float S = (float)MV_SCALE;
        const float T = (float)MV_TOP;
        t1_glide_quad(0.0f, T, 256.0f * S, T + 128.0f * S,
                      0.0f, 0.0f, 256.0f, 128.0f, &mv->texA, 1.0f);
        t1_glide_quad(256.0f * S, T, 320.0f * S, T + 128.0f * S,
                      0.0f, 0.0f, 64.0f, 128.0f, &mv->texB, 1.0f);
        t1_glide_quad(0.0f, T + 128.0f * S, 256.0f * S, T + 200.0f * S,
                      0.0f, 0.0f, 256.0f, 72.0f, &mv->texC, 1.0f);
        t1_glide_quad(256.0f * S, T + 128.0f * S, 320.0f * S, T + 200.0f * S,
                      0.0f, 0.0f, 64.0f, 72.0f, &mv->texD, 1.0f);
    }
    t1_glide_depth(1);
    t1_glide_end();
    return !mv->done;
}
