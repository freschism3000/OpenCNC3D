/*
 * movieplay.c -- see movieplay.h.
 *
 * The loop, said once in plain language:
 *
 *   read input  ->  feed the sound device  ->  ask the sound device what time it is  ->
 *   show the newest decoded frame that is due by then  ->  decode further ahead while
 *   there is room.
 *
 * The only thing worth arguing about is what "what time it is" means, and the answer is
 * the sound, because sound cannot be rushed or repeated. Video that is behind can be
 * caught up by throwing frames away and nobody notices; audio that is behind can only
 * be fixed with a click. So the picture chases the sound.
 *
 * THE RING is what makes that possible. A VQA carries its audio inside the video
 * frames, so "decode a frame" and "produce a fifth of a second of sound" are the same
 * act. If the player decoded only the frame it was about to show, the sound device
 * would never hold more than one frame of audio and would stutter on every hiccup. So
 * the decoder runs MOV_RING frames ahead into a small ring of finished frames, which
 * hands the device about a quarter of a second of sound to sit on, and the presenter
 * picks out of that ring by the audio clock. Ring full is the flow control: nothing
 * else limits how far ahead the decoder runs, and nothing needs a thread.
 *
 * Cost of the ring: MOV_RING * w * h bytes plus a palette each, 256 KB for a 320x200
 * movie at four deep. That is affordable on the Windows 98 tier and it is the piece
 * that keeps the sound clean there, where it matters most.
 */

#include "movieplay.h"
#include "vqaplay.h"
#include "pngwrite.h"

#define GL_SILENCE_DEPRECATION 1
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

/* The F5 panel's bilinear toggle reaches the movie plate too. See fx_filter.h. */
#include "fx_filter.h"
#include "fullscreen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOV_DEF_PLATE_W 320
#define MOV_DEF_PLATE_H 200
#define MOV_DEF_FADE_MS 250        /* defines.h FADE_PALETTE_MEDIUM = TIMER_SECOND/4 */
#define MOV_DEF_DRAIN   2000
#define MOV_RING        4          /* frames decoded ahead; see the header comment  */

typedef struct {
    int index;                   /* which frame of the movie this is                */
    unsigned char *pix;          /* w*h palette indices                             */
    unsigned char pal[768];      /* the palette in force for it                     */
} MOV_Slot;

typedef struct {
    SDL_Window *win;
    const MOV_Opts *o;
    const MOV_Audio *a;

    int plate_w, plate_h;
    int tw, th;                  /* power of two texture holding the plate          */
    GLuint tex;
    unsigned char *rgba;         /* plate_w * plate_h * 4                           */
    unsigned char *padded;       /* tw * th * 4, what goes to GL                    */
    unsigned char lut[256 * 4];  /* the current palette, widened once per change    */
    int lut_from;                /* which slot's palette is in the lut              */

    int vpx, vpy, scale;         /* letterbox, recomputed every present             */
} MOV_P;

/* --------------------------------------------------------------------------- layout */

static int mov_pot(int v)
{
    int p = 1;
    while (p < v)
        p *= 2;
    return p;
}

static void mov_layout(MOV_P *p)
{
    int w = 0, h = 0, sc;
    SDL_GL_GetDrawableSize(p->win, &w, &h);
    if (w < p->plate_w || h < p->plate_h) {
        p->scale = 1;
    } else {
        sc = w / p->plate_w;
        if (h / p->plate_h < sc)
            sc = h / p->plate_h;
        p->scale = sc < 1 ? 1 : sc;
    }
    p->vpx = (w - p->plate_w * p->scale) / 2;
    p->vpy = (h - p->plate_h * p->scale) / 2;
}

/* ---------------------------------------------------------------------------- pixels */

/* The palette is already 8-bit RGB out of vqaplay (masked to 6 bits and lifted 15%,
 * vqapalette.cpp). Widened to RGBA once per change rather than once per pixel. */
static void mov_build_lut(MOV_P *p, const unsigned char *pal)
{
    int i;
    for (i = 0; i < 256; i++) {
        p->lut[i * 4 + 0] = pal[i * 3 + 0];
        p->lut[i * 4 + 1] = pal[i * 3 + 1];
        p->lut[i * 4 + 2] = pal[i * 3 + 2];
        p->lut[i * 4 + 3] = 255;
    }
}

/* The plate starts as OPAQUE black and stays that way. Opaque matters: with blending
 * off the quad's alpha is written straight into the framebuffer, and a transparent
 * border would hand the next thing that composites over this window a hole where the
 * letterbox bars are. It cost an hour: INTRO2's back buffer matched the expected
 * picture in RGB and differed in 126,720 alpha bytes, which is exactly the 320x44 of
 * bar above and below a 320x156 movie on a 320x200 plate. */
static void mov_plate_init(MOV_P *p)
{
    long i, n = (long)p->plate_w * p->plate_h;
    memset(p->rgba, 0, (size_t)n * 4);
    for (i = 0; i < n; i++)
        p->rgba[i * 4 + 3] = 255;
}

/* A movie shorter than the plate is CENTRED on it, not stretched: the DOS player put
 * the 320x156 intro in the middle of a 320x200 screen and left the rest black. Only
 * the movie's own rectangle is written; the border was made black once and never
 * changes, so there is no per-frame clear. */
static void mov_frame_to_rgba(MOV_P *p, const unsigned char *pix, int w, int h)
{
    int y, x, ox = (p->plate_w - w) / 2, oy = (p->plate_h - h) / 2;
    for (y = 0; y < h; y++) {
        unsigned char *d;
        const unsigned char *s;
        if (y + oy < 0 || y + oy >= p->plate_h)
            continue;
        s = pix + (long)y * w;
        d = p->rgba + (((long)(y + oy) * p->plate_w) + ox) * 4;
        for (x = 0; x < w && x + ox < p->plate_w; x++) {
            const unsigned char *c = p->lut + (unsigned)s[x] * 4;
            d[x * 4 + 0] = c[0];
            d[x * 4 + 1] = c[1];
            d[x * 4 + 2] = c[2];
            d[x * 4 + 3] = 255;
        }
    }
}

static void mov_upload(MOV_P *p, const unsigned char *rgba)
{
    int y;
    for (y = 0; y < p->plate_h; y++)
        memcpy(p->padded + (long)y * p->tw * 4, rgba + (long)y * p->plate_w * 4,
               (size_t)p->plate_w * 4);
    glBindTexture(GL_TEXTURE_2D, p->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p->tw, p->th, GL_RGBA, GL_UNSIGNED_BYTE,
                    p->padded);
}

/* THE STATE ASSERTION. Every piece of state this quad depends on is set here.
 *
 * The failure it prevents is not hypothetical: a correct decoder in a wrong GL state
 * shows garbage. Coming from the tactical view GL_DEPTH_TEST is on with a depth buffer
 * full of last frame's hills, so the movie is depth-rejected and the screen stays black;
 * glColor is whatever the last line of sidebar text was, so through GL_MODULATE the
 * movie comes out tinted; a scissor box from the sidebar clips it to a strip. Each of
 * those is turned off below, and movtest --hostile turns all of them on before calling
 * in, so the claim is tested rather than believed. */
static void mov_present(MOV_P *p)
{
    float u, v;
    int w = 0, h = 0;

    mov_layout(p);
    SDL_GL_GetDrawableSize(p->win, &w, &h);

#ifdef MOV_SLOPPY_PRESENT
    /* THE NEGATIVE CONTROL, kept so the paragraph above can be re-tested rather than
     * believed, and mirroring DMS_SLOPPY_PRESENT in dosmenu_shell.c. This is what a
     * movie player that assumed a fresh context would do: clear the colour buffer,
     * set the matrices, enable texturing, draw. Build with -DMOV_SLOPPY_PRESENT and
     * run movtest --hostile: the frames stop matching --expect. */
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
#else
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_STENCIL_TEST);
    glShadeModel(GL_FLAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
#endif

    glViewport(p->vpx, p->vpy, p->plate_w * p->scale, p->plate_h * p->scale);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, p->tex);
    u = (float)p->plate_w / (float)p->tw;
    v = (float)p->plate_h / (float)p->th;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(u, 0); glVertex2f(1, 0);
    glTexCoord2f(u, v); glVertex2f(1, 1);
    glTexCoord2f(0, v); glVertex2f(0, 1);
    glEnd();

    glDepthMask(GL_TRUE);
}

/* Back buffer -> PNG, read BEFORE the swap so the picture is provably this frame. GL
 * hands rows back bottom up. */
static int mov_grab(MOV_P *p, const char *path)
{
    int w = 0, h = 0, y, ok;
    unsigned char *px, *flip;

    SDL_GL_GetDrawableSize(p->win, &w, &h);
    if (w <= 0 || h <= 0)
        return 0;
    px = (unsigned char *)malloc((size_t)w * h * 4);
    flip = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px || !flip) {
        free(px);
        free(flip);
        return 0;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (y = 0; y < h; y++)
        memcpy(flip + (long)y * w * 4, px + (long)(h - 1 - y) * w * 4, (size_t)w * 4);
    ok = png_write_rgba(path, flip, w, h);
    free(px);
    free(flip);
    return ok;
}

/* ------------------------------------------------------------------------- the sound */

static void snd_open(const MOV_Audio *a, int rate, int ch)
{
    if (a && a->open) a->open(a->user, rate, ch);
}
static void snd_push(const MOV_Audio *a, const void *pcm, int n)
{
    if (a && a->push) a->push(a->user, pcm, n);
}
static void snd_pump(const MOV_Audio *a)
{
    if (a && a->pump) a->pump(a->user);
}
static void snd_close(const MOV_Audio *a)
{
    if (a && a->close) a->close(a->user);
}
static long snd_played(const MOV_Audio *a)
{
    if (a && a->played) return a->played(a->user);
    return -1;
}
static int snd_drained(const MOV_Audio *a)
{
    if (a && a->drained) return a->drained(a->user);
    return -1; /* cannot say: caller falls back to played-vs-pushed */
}

/* --------------------------------------------------------------------------- probing */

int mov_probe(const char *path, int *w, int *h, int *frames, int *fps, int *rate)
{
    char err[256];
    VQ_Movie *m = vq_open(path, err, sizeof err);
    if (!m) {
        fprintf(stderr, "movie: %s\n", err);
        return 0;
    }
    if (w) *w = vq_width(m);
    if (h) *h = vq_height(m);
    if (frames) *frames = vq_frames(m);
    if (fps) *fps = vq_fps(m);
    if (rate) *rate = vq_sample_rate(m);
    vq_close(m);
    return 1;
}

/* -------------------------------------------------------------------------- the fade */

static void mov_fade_out(MOV_P *p, int ms)
{
    unsigned int start = SDL_GetTicks();
    unsigned char *src;
    long i, n = (long)p->plate_w * p->plate_h;

    if (ms <= 0)
        return;
    src = (unsigned char *)malloc((size_t)n * 4);
    if (!src)
        return;
    memcpy(src, p->rgba, (size_t)n * 4);

    for (;;) {
        unsigned int t = SDL_GetTicks() - start;
        int level = (t >= (unsigned)ms) ? 0 : (int)(256 - (long)t * 256 / ms);
        SDL_Event e;

        while (SDL_PollEvent(&e))
            ;  /* a quarter second: swallow input rather than act on it */

        for (i = 0; i < n; i++) {
            p->rgba[i * 4 + 0] = (unsigned char)((src[i * 4 + 0] * level) >> 8);
            p->rgba[i * 4 + 1] = (unsigned char)((src[i * 4 + 1] * level) >> 8);
            p->rgba[i * 4 + 2] = (unsigned char)((src[i * 4 + 2] * level) >> 8);
            p->rgba[i * 4 + 3] = 255;
        }
        snd_pump(p->a);
        mov_upload(p, p->rgba);
        mov_present(p);
        SDL_GL_SwapWindow(p->win);
        if (t >= (unsigned)ms)
            break;
        SDL_Delay(4);
    }
    free(src);
}

/* --------------------------------------------------------------------------- playing */

/* "…/movies/INTRO2.VQA" -> "INTRO2", so two movies in one run do not overwrite each
 * other's frames. Found out the hard way: the logo's shots were the intro's. */
static void mov_stem(const char *path, char *out, int outlen)
{
    const char *b = path, *p, *dot;
    int n;
    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    dot = strrchr(b, '.');
    n = dot ? (int)(dot - b) : (int)strlen(b);
    if (n > outlen - 1) n = outlen - 1;
    memcpy(out, b, (size_t)n);
    out[n] = 0;
}

static int mov_want_shot(const MOV_Opts *o, int frame)
{
    int i;
    if (!o->shot_dir || !o->shot_frames)
        return 0;
    for (i = 0; o->shot_frames[i] >= 0; i++)
        if (o->shot_frames[i] == frame)
            return 1;
    return 0;
}

int mov_play(SDL_Window *win, const char *path, const MOV_Opts *opts,
             const MOV_Audio *audio, MOV_Stats *stats)
{
    static const MOV_Opts zero_opts;
    MOV_Opts o;
    MOV_P p;
    MOV_Stats st;
    MOV_Slot ring[MOV_RING];
    char err[256];
    VQ_Movie *m;
    FILE *log = NULL;
    unsigned char chunk[32768];

    int rc = MOV_DONE;
    int fps, rate, channels;
    char stem[64];
    int head = 0, count = 0;          /* the ring                                   */
    int decoded = 0, shown = -1, eof = 0, use_audio_clock = 0;
    long pushed = 0;
    unsigned int wall0, video_end_ms = 0, drain_start_ms = 0;
    int i;

    if (!opts) opts = &zero_opts;
    o = *opts;
    if (!o.plate_w) o.plate_w = MOV_DEF_PLATE_W;
    if (!o.plate_h) o.plate_h = MOV_DEF_PLATE_H;
    if (!o.drain_ms) o.drain_ms = MOV_DEF_DRAIN;
    if (o.fade_out_ms == 0) o.fade_out_ms = MOV_DEF_FADE_MS;
    if (o.fade_out_ms < 0) o.fade_out_ms = 0;

    memset(&st, 0, sizeof st);
    memset(&p, 0, sizeof p);
    memset(ring, 0, sizeof ring);

    /* conquer.cpp:2190 -- Play_Movie calls Keyboard->Clear() here, immediately before
       VQA_Alloc/VQA_Open/VQA_Play, so that whatever the player did on the screen that
       led into this movie cannot end the movie itself. intro.cpp:250 does the same for
       Choose_Side, which plays the briefing with VQA_Play directly. SDL_QUIT is
       deliberately NOT flushed: closing the window during a movie must still work. */
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
    SDL_FlushEvent(SDL_MOUSEBUTTONUP);

    m = vq_open(path, err, sizeof err);
    if (!m) {
        fprintf(stderr, "movie: %s\n", err);
        if (stats) *stats = st;
        return MOV_ERROR;
    }
    fps = vq_fps(m);
    rate = vq_sample_rate(m);
    channels = vq_channels(m);
    mov_stem(path, stem, (int)sizeof stem);

    /* A movie bigger than the plate grows the plate rather than being cropped. Nothing
       on the discs needs it, but a silent crop would be a lie about what was decoded. */
    if (vq_width(m) > o.plate_w) o.plate_w = vq_width(m);
    if (vq_height(m) > o.plate_h) o.plate_h = vq_height(m);

    p.win = win;
    p.o = &o;
    p.a = audio;
    p.plate_w = o.plate_w;
    p.plate_h = o.plate_h;
    p.tw = mov_pot(p.plate_w);
    p.th = mov_pot(p.plate_h);
    p.lut_from = -1;
    p.rgba = (unsigned char *)calloc((size_t)p.plate_w * p.plate_h * 4, 1);
    p.padded = (unsigned char *)calloc((size_t)p.tw * p.th * 4, 1);
    for (i = 0; i < MOV_RING; i++) {
        ring[i].pix = (unsigned char *)calloc((size_t)vq_width(m) * vq_height(m), 1);
        ring[i].index = -1;
    }
    if (!p.rgba || !p.padded) {
        rc = MOV_ERROR;
        goto cleanup;
    }
    mov_plate_init(&p);

    glGenTextures(1, &p.tex);
    glBindTexture(GL_TEXTURE_2D, p.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* NOT registered with the bilinear switch, so it cannot reach this however it is
       flipped. A movie is neither UI nor world -- it is a 320x200 VQA upscaled to the
       window, and a filter would arguably help it -- so this one was a judgement call
       rather than a rule. Settled: excluded. See fx_filter.h. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p.tw, p.th, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 p.padded);

    if (o.log_path) {
        log = fopen(o.log_path, "w");
        if (log)
            fprintf(log, "pass,wall_ms,clock_ms,clock_src,target,shown,due_ms,late_ms,"
                         "dropped,ring,pushed,played\n");
    }

    snd_open(audio, rate, channels);
    wall0 = SDL_GetTicks();

    for (;;) {
        SDL_Event e;
        long clock_ms, played;
        int target, presented_this_pass = 0;
        const char *csrc;

        while (SDL_PollEvent(&e)) {
            /* CMD+F / ALT+ENTER. It is NOT the 1995 abort test below and must not be
               mistaken for one: the film keeps playing, the window just changes size. */
            if (fs_handle_event(&e)) continue;
            if (e.type == SDL_QUIT) { rc = MOV_QUIT; goto done; }
            /* conquer.cpp:2935 VQ_Call_Back is the 1995 abort test, and it is ESC and
               ONLY ESC:

                   if (Keyboard->Check()) { key = Keyboard->Get(); Keyboard->Clear(); }
                   ...
                   if ((BreakoutAllowed || Debug_Flag) && key == KN_ESC) { ... }

               Every other key, and every mouse click (KN_LMOUSE), is read and thrown
               away by that Keyboard->Clear() without ending the movie. The test that
               used to live here ended on any key or any button, so the first click a
               player made after the side select killed the 37 second GDI1 briefing at
               frame 0. That is why the briefing looked like it never played. */
            if (!o.no_skip && e.type == SDL_KEYDOWN &&
                e.key.keysym.sym == SDLK_ESCAPE) {
                rc = MOV_SKIPPED;
                goto done;
            }
            /* anything else polled is discarded here, which is what Keyboard->Clear()
               does in VQ_Call_Back. */
        }

        snd_pump(audio);

        /* ---- 1. what time is it ------------------------------------------------- */
        played = snd_played(audio);
        if (played >= 0 && pushed > 0) {
            use_audio_clock = 1;
            clock_ms = played * 1000 / ((long)rate * channels * 2);
            csrc = "audio";
        } else {
            clock_ms = (long)(SDL_GetTicks() - wall0);
            csrc = "wall";
        }
        target = (int)(clock_ms * fps / 1000);

        /* ---- 2. show the newest frame that is due -------------------------------- */
        while (count > 0 && ring[head].index <= target) {
            MOV_Slot *s = &ring[head];
            int is_last = !(count > 1 && ring[(head + 1) % MOV_RING].index <= target);
            if (!is_last) {
                /* Superseded before it was ever on screen. This is the whole point of
                   clocking off the sound: when the machine cannot keep up, the picture
                   loses frames and the movie still ends on the same beat. */
                st.frames_dropped++;
                head = (head + 1) % MOV_RING;
                count--;
                continue;
            }
            if (p.lut_from != s->index) {
                mov_build_lut(&p, s->pal);
                p.lut_from = s->index;
            }
            mov_frame_to_rgba(&p, s->pix, vq_width(m), vq_height(m));
            mov_upload(&p, p.rgba);
            mov_present(&p);
            if (mov_want_shot(&o, s->index)) {
                char shotpath[512];
                snprintf(shotpath, sizeof shotpath, "%s/%s_f%05d.png", o.shot_dir, stem,
                         s->index);
                if (!mov_grab(&p, shotpath))
                    fprintf(stderr, "movie: could not write %s\n", shotpath);
            }
            SDL_GL_SwapWindow(p.win);
            st.frames_presented++;
            presented_this_pass = 1;
            shown = s->index;
            {
                long due = (long)s->index * 1000 / fps;
                int late = (int)(clock_ms - due);
                if (late > st.worst_late_ms) st.worst_late_ms = late;
                if (log)
                    fprintf(log, "%d,%ld,%ld,%s,%d,%d,%ld,%d,%d,%d,%ld,%ld\n",
                            st.frames_presented, (long)(SDL_GetTicks() - wall0), clock_ms,
                            csrc, target, s->index, due, late, st.frames_dropped,
                            count, pushed, played);
            }
            head = (head + 1) % MOV_RING;
            count--;
            /* The wall time of the LAST frame to reach the glass, which is the honest
               answer to "how long did the movie take". The decoder finishes a ring
               earlier than that and must not be the one that reports it. */
            video_end_ms = SDL_GetTicks() - wall0;
            break;
        }

        /* ---- 3. run the decoder ahead into the spare slots ----------------------- */
        while (!eof && count < MOV_RING) {
            MOV_Slot *s = &ring[(head + count) % MOV_RING];
            int n, r = vq_next_frame(m);
            if (r != 1) {
                eof = 1;
                video_end_ms = SDL_GetTicks() - wall0;
                break;
            }
            memcpy(s->pix, vq_pixels(m), (size_t)vq_width(m) * vq_height(m));
            memcpy(s->pal, vq_palette(m), 768);
            s->index = decoded;
            decoded++;
            count++;
            while ((n = vq_take_audio(m, chunk, (int)sizeof chunk)) > 0) {
                snd_push(audio, chunk, n);
                pushed += n;
            }
        }

        /* ---- 4. finished? ------------------------------------------------------- */
        if (o.stop_after > 0 && st.frames_presented >= o.stop_after)
            break;
        if (eof && count == 0) {
            /* Hold the last frame while the tail of the sound plays out: at this point
               the sink still has a ring's worth queued and cutting it would chop the
               end off every movie. Bounded, so a sink that never drains cannot hang
               the game.

               Ask the sink directly when it can answer: `played` subtracts the device
               latency for pacing, so `played < pushed` holds FOREVER once the ring is
               empty and the old test spent the entire 1500 ms window after every
               movie as pure silence. What is still in the DEVICE buffer needs no
               waiting at all: whatever plays next queues behind it in the same FIFO. */
            {
                int dr = snd_drained(audio);
                int tail_left = (dr >= 0) ? !dr
                                          : (played >= 0 && pushed > 0 && played < pushed);
                if (tail_left &&
                    (long)(SDL_GetTicks() - wall0) < (long)video_end_ms + o.drain_ms) {
                    if (!drain_start_ms)
                        drain_start_ms = SDL_GetTicks();
                    SDL_Delay(4);
                    continue;
                }
            }
            /* Measured, not asserted: this number was 1490 ms of dead air after every
               movie before the sink could answer "drained?" itself. */
            if (drain_start_ms)
                fprintf(stderr, "movie: %s tail drain took %u ms\n", path,
                        SDL_GetTicks() - drain_start_ms);
            break;
        }

        /* The late frame test: pretend the machine choked on this frame. */
        if (presented_this_pass && o.stall_every > 0 &&
            st.frames_presented % o.stall_every == 0)
            SDL_Delay((Uint32)(o.stall_ms > 0 ? o.stall_ms : 100));

        if (!presented_this_pass)
            SDL_Delay(1);
    }

done:
    st.frames_decoded = decoded;
    st.audio_bytes = pushed;
    st.used_audio_clock = use_audio_clock;
    st.wall_ms = video_end_ms ? (long)video_end_ms : (long)(SDL_GetTicks() - wall0);
    {
        long played = snd_played(audio);
        st.clock_ms = (played >= 0 && pushed > 0)
                          ? played * 1000 / ((long)rate * channels * 2)
                          : (long)(SDL_GetTicks() - wall0);
    }
    (void)shown;

    /* Fade the last frame down rather than cutting to black, which is what
       Fade_Palette(FADE_PALETTE_MEDIUM) did after Play_Movie. Skipped on QUIT: the
       window is going away and a quarter second of politeness is a quarter second of
       an unresponsive program. */
    if (rc != MOV_QUIT)
        mov_fade_out(&p, o.fade_out_ms);

    snd_close(audio);

cleanup:
    if (log) fclose(log);

    /* Give the window back exactly as tidy as we would like to have found it. */
    glBindTexture(GL_TEXTURE_2D, 0);
    if (p.tex) { fx_filter_forget(p.tex); glDeleteTextures(1, &p.tex); }
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_TRUE);

    for (i = 0; i < MOV_RING; i++)
        free(ring[i].pix);
    free(p.rgba);
    free(p.padded);
    vq_close(m);
    if (stats) *stats = st;
    return rc;
}
