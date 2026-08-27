/*
 * t1_movie.h -- Westwood VQA movies, on the Voodoo.
 *
 * The handoff filed movies as "big and not attempted", and half of that was right. The
 * DECODER is not the hard half at all: video/vqaplay.c is pure C89 over libc with no SDL,
 * no GL and no threads, and it is compiled here UNEDITED exactly as game/dosbar.c,
 * game/hud640.c and menu/dosmenu.c are. It streams -- one frame in memory at a time --
 * which is what makes a 21 MB INTRO2 playable on a machine with 388 MB free.
 *
 * What could NOT come across is video/movieplay.c, the presentation loop: SDL owns its
 * window, its clock, its event queue and even its function signature. This file is the
 * Tier 1 replacement for that, and it is smaller than the thing it replaces because the
 * card does most of the work.
 *
 * THE CARD TAKES THE FRAME AS PALETTE INDICES. A decoded VQA frame is 8-bit indices plus
 * 768 bytes of palette, which is exactly GR_TEXFMT_P_8 plus grTexDownloadTable -- both of
 * which this backend already does for the terrain, the sidebar and the meshes. So the
 * whole RGBA expansion the desktop build does on the CPU every frame simply does not
 * happen here: the decoded rows are memcpy'd into the pages and uploaded as they are.
 *
 * FOUR PAGES, because a 320x200 plate does not fit the card's 256x256 limit: 256x128 and
 * 64x128 across, twice down. All four aspects are legal and the set is 80 KB of texture
 * memory, allocated once and re-uploaded per frame -- less per frame than the 640x480 HUD
 * already re-uploads in a build that runs at 52 to 58 FPS.
 *
 * The audio goes into the mixer's MOVIE bus, which already exists and already runs at
 * 22050 Hz, which is the rate every one of these files is in.
 */

#ifndef T1_MOVIE_H
#define T1_MOVIE_H

#include "softras.h"
#include "vqaplay.h"

typedef struct
{
    VQ_Movie     *m;
    int           w, h, fps;
    int           frame, palchanges;
    double        t0;             /* when playback started, in seconds */
    int           done;

    /* 320x200 in four legal pages: 256x128 + 64x128, twice down. */
    unsigned char pageA[256 * 128], pageB[64 * 128];
    unsigned char pageC[256 * 128], pageD[64 * 128];
    SR_Texture    texA, texB, texC, texD;
    int           uploaded;
    char          name[16];
} T1_Movie;

/* Allocates the four pages on the card. Once, at startup. */
int  t1_movie_init(T1_Movie *mv, char *err, int errlen);

/* Opens a movie and starts its clock. 0 and a reason if the file is not there, in which
 * case the caller carries on without it -- a missing movie is never fatal. */
int  t1_movie_play(T1_Movie *mv, const char *path, double now, char *err, int errlen);
void t1_movie_stop(T1_Movie *mv);
int  t1_movie_playing(const T1_Movie *mv);

/* One frame of playback: decodes as many frames as the movie's own clock is owed, pushes
 * their audio, uploads the last one and draws it. Returns 0 when the movie has ended.
 * `push_audio` may be NULL for a silent movie. */
int  t1_movie_frame(T1_Movie *mv, double now,
                    int (*push_audio)(const short *pcm, int samples));

#endif /* T1_MOVIE_H */
