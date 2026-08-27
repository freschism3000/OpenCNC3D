/*
 * dosmenu_shell.c -- see dosmenu_shell.h.
 *
 * This file is preview.c's window half, lifted out and made re-entrant. The drawing
 * itself is untouched: dosmenu.c still rasterises 320x200 8-bit pixels and this still
 * does one RGBA conversion, one power-of-two texture and one quad, which is exactly
 * what the Voodoo 2 build does through Glide.
 *
 * THE ONE REAL CHANGE, and the reason this file exists rather than a copy of
 * preview.c: PRESENT NOW SETS ITS OWN COMPLETE GL STATE.
 *
 * As a standalone program the menu could assume a fresh context: depth test off,
 * blending off, colour white, no scissor. Sharing a context with the tactical
 * renderer, none of that holds. cnc_eyes.cpp's draw_frame leaves the depth test
 * ENABLED with a depth buffer full of terrain, glColor at whatever the last piece of
 * sidebar text was, and GL_TEXTURE_2D bound to a cameo. A quad drawn under those
 * conditions is depth-rejected against last frame's hills and tinted by a leftover
 * colour: the menu comes back either black or the wrong colour, intermittently,
 * depending on where the camera happened to be. So the menu asserts the state it
 * needs, the same discipline draw_frame already follows, and neither screen has to
 * know anything about the other.
 */

#include "dosmenu_shell.h"
#include "dosops.h"
#include "doslobby.h"
/* The lobby's harness writes its own frames out; the movie player already links this. */
#include "pngwrite.h"
/* dosmenu_shell.h has already picked a GL header; fx_filter.h needs one and
   deliberately does not pick its own. See its header comment. */
#include "fx_filter.h"
/* CMD+F / ALT+ENTER, so the menu can go fullscreen too. */
#include "fullscreen.h"
#include "cncaudio.h"
#include "audioboot.h"
#include "movieplay.h"
#include "moviesnd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* defines.h:2226 FADE_PALETTE_MEDIUM is TIMER_SECOND/4, TIMER_SECOND is 60. */
#define DM_FADE_MS 250

/* One pass of the menu loop. Sixteen milliseconds is the SDL_Delay at the bottom of
   dms_run, and it is also how much sound a recording run renders per pass, so the two
   clocks stay together. */
#define DM_FRAME_MS 16

/* The menu's half of the audio contract, in one place: everything the menu does to the
   score goes through this, and the movie's own ducking is moviesnd.c's business. That
   makes "the menu talks to the shared engine and never to a device" checkable by
   reading three lines rather than the whole file. */
static void dms_music_gain(DMS *s, int permille, int ms)
{
    if (s->cfg.au)
        mixer_bus_gain(cnc_audio_mixer(s->cfg.au), MIX_BUS_MUSIC, permille, ms);
}

static int dms_pot(int v)
{
    int p = 1;
    while (p < v)
        p *= 2;
    return p;
}

/* The letterbox. Largest whole-number scale that fits, centred, so a 320x200 plate
 * never gets resampled into a 1280x720 window. */
static void dms_layout(DMS *s)
{
    int w = 0, h = 0, sc, pw = 0, ph = 0;
    SDL_GL_GetDrawableSize(s->win, &w, &h);
    /* SDL reports mouse positions in window POINTS and the viewport is in DRAWABLE
     * PIXELS. On this build they are the same number, because neither screen asks
     * for SDL_WINDOW_ALLOW_HIGHDPI, but the tactical loop already converts between
     * them and the menu must not be the one place that silently assumes 1:1. */
    SDL_GetWindowSize(s->win, &pw, &ph);
    s->px = pw > 0 ? (float)w / (float)pw : 1.0f;
    s->py = ph > 0 ? (float)h / (float)ph : 1.0f;
    if (w < DM_SCREEN_W || h < DM_SCREEN_H) {
        s->scale = 1;
    } else {
        sc = w / DM_SCREEN_W;
        if (h / DM_SCREEN_H < sc)
            sc = h / DM_SCREEN_H;
        s->scale = sc < 1 ? 1 : sc;
    }
    s->vpx = (w - DM_SCREEN_W * s->scale) / 2;
    s->vpy = (h - DM_SCREEN_H * s->scale) / 2;
}

/* In window POINTS, which is the space SDL mouse events live in. */
void dms_item_window_rect(const DMS *s, int item, int *x, int *y, int *w, int *h)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;
    dm_item_rect(&s->st, item, &rx, &ry, &rw, &rh);
    *x = (int)((s->vpx + rx * s->scale) / s->px);
    *y = (int)((s->vpy + ry * s->scale) / s->py);
    *w = (int)(rw * s->scale / s->px);
    *h = (int)(rh * s->scale / s->py);
}

/* Window points -> menu pixels. Outside the letterbox the answer is deliberately a
 * long way off the plate, so a click on the black bars hits nothing. */
static void dms_to_menu(const DMS *s, int wx, int wy, int *mx, int *my)
{
    int fx = (int)(wx * s->px), fy = (int)(wy * s->py);
    *mx = (fx - s->vpx) / s->scale;
    *my = (fy - s->vpy) / s->scale;
    if (fx < s->vpx || fy < s->vpy)
        *mx = *my = -1000;
}

/* Keep the music fed. The device pulls from the mixer on its own thread; this is the
   game-thread half, which is where the file I/O happens, so it must be called from
   the menu loop and never from the audio callback. */
static void dms_pump_audio(DMS *s)
{
    audio_frame(s->cfg.au, DM_FRAME_MS);
}

static void dms_fade_rgba(unsigned char *dst, const unsigned char *src, int level)
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

static void dms_upload(DMS *s, const unsigned char *rgba)
{
    int y;
    for (y = 0; y < DM_SCREEN_H; y++)
        memcpy(s->padded + (long)y * s->tw * 4, rgba + (long)y * DM_SCREEN_W * 4,
               (size_t)DM_SCREEN_W * 4);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->tw, s->th, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 s->padded);
}

static void dms_upload_faded(DMS *s, const unsigned char *rgba, int level)
{
    if (level >= 256) {
        dms_upload(s, rgba);
        return;
    }
    dms_fade_rgba(s->faded, rgba, level);
    dms_upload(s, s->faded);
}

/* One frame on screen. Every piece of state this quad depends on is set here; see the
 * header comment for why that is not paranoia. All GL 1.1, no extensions. */
void dms_draw(DMS *s)
{
    float u, v;
    int w = 0, h = 0;

    dms_layout(s);
    SDL_GL_GetDrawableSize(s->win, &w, &h);

    /* 1. the whole window goes black, including the letterbox bars. glClear obeys
     *    the scissor box, not the viewport, so the viewport is opened up first and
     *    any scissor a previous screen left behind is turned off. */
#ifdef DMS_SLOPPY_PRESENT
    /* THE NEGATIVE CONTROL, kept so the claim above can be re-tested rather than
     * believed. This is exactly what preview.c did as a standalone program: clear
     * the colour buffer only, set the matrices, enable texturing, draw. Build the
     * app with -DDMS_SLOPPY_PRESENT and run the harness: the FIRST menu is fine and
     * every menu after a mission is wrong, because the depth buffer still holds that
     * mission's terrain and GL_DEPTH_TEST is still on. */
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
#else
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* 2. the state the quad needs, asserted rather than assumed. The depth clear
     *    above plus DEPTH_TEST off means the tactical view's z buffer cannot reject
     *    the menu; colour white means its last glColor cannot tint it. */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glShadeModel(GL_FLAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
#endif

    /* 3. the plate, at a whole-number scale, in the middle. */
    glViewport(s->vpx, s->vpy, DM_SCREEN_W * s->scale, DM_SCREEN_H * s->scale);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    u = (float)DM_SCREEN_W / (float)s->tw;
    v = (float)DM_SCREEN_H / (float)s->th;
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

    /* 4. hand the depth buffer back writable. The tactical renderer sets its own
     *    depth state anyway, but leaving a write mask off is the kind of debt that
     *    turns up three screens later. */
    glDepthMask(GL_TRUE);
}

static void dms_present(DMS *s)
{
    dms_draw(s);
    SDL_GL_SwapWindow(s->win);
}

void dms_redraw(DMS *s)
{
    memset(s->screen, DB_TBLACK, sizeof s->screen);
    if (s->ops) {
        /* The title plate is the menu's, so the list screen is the same picture with a
           different dialog on it -- exactly how the 1995 game moves between Select_Game
           and the map/mission dialogs. */
        dm_draw_plate(&s->surf, s->pack);
        do_draw(&s->surf, s->pack, s->ops);
    } else if (s->lobby) {
        dm_draw_plate(&s->surf, s->pack);
        sk_draw(&s->surf, s->pack, s->lobby);
    } else {
        dm_draw_menu(&s->surf, s->pack, &s->st);
    }
    dm_draw_cursor(&s->surf, s->pack, s->mx, s->my);
    db_surface_to_rgba(&s->surf, s->pack->pal8, s->rgba, 0);
    dms_upload(s, s->rgba);
}

static void dms_fade(DMS *s, const unsigned char *rgba, int to_black)
{
    unsigned int start = SDL_GetTicks();
    for (;;) {
        unsigned int t = SDL_GetTicks() - start;
        int level;
        SDL_Event e;

        while (SDL_PollEvent(&e))
            ; /* a quarter second: swallow input rather than act on it */

        if (t >= DM_FADE_MS)
            level = to_black ? 0 : 256;
        else
            level = to_black ? (int)(256 - t * 256 / DM_FADE_MS) : (int)(t * 256 / DM_FADE_MS);

        dms_pump_audio(s);
        dms_upload_faded(s, rgba, level);
        dms_present(s);
        if (t >= DM_FADE_MS)
            break;
        SDL_Delay(4);
    }
}

/* ONE MOVIE, in this window, on this context, through this mixer.
 *
 * The frame clock, the palette conversion, the present, the abort key and the fade all
 * live in video/movieplay.c now, because the menu is not the only screen that plays a
 * movie: the mission briefings and the win and lose sequences all want the same thing,
 * and the tactical view will call it with GL in a far worse state than this leaves.
 *
 * Two things are different from the loop this replaces and both are audible:
 *
 *  - THE CLOCK IS THE SOUND, not SDL_GetTicks. On a machine that stutters, video
 *    frames are dropped and the movie still ends on the beat it should. The old loop
 *    let the picture fall behind the sound and stay behind.
 *  - the pump never asks the mixer for more movie audio than the movie has decoded,
 *    so no silence is spliced into the movie's own stream while it is starting up.
 *
 * Returns MOV_DONE / MOV_SKIPPED / MOV_QUIT / MOV_ERROR.
 */
static const int dms_movie_shots[] = {0, 8, 20, -1};

static int dms_play_movie(DMS *s, const char *path)
{
    MOV_Opts o;
    MOV_Audio a;
    MOV_Sink sink;

    if (!path)
        return MOV_ERROR;

    memset(&o, 0, sizeof o);
    o.plate_w = DM_SCREEN_W;   /* the movie sits on the same 320x200 plate the menu    */
    o.plate_h = DM_SCREEN_H;   /* does, so a 320x156 intro is letterboxed where the    */
    o.fade_out_ms = DM_FADE_MS;/* DOS player put it rather than stretched              */
    o.shot_dir = s->cfg.movie_shotdir;
    o.shot_frames = s->cfg.movie_shotdir ? dms_movie_shots : NULL;
    o.stop_after = s->cfg.movie_stop_after;

    movsnd_init(&sink, s->cfg.au);
    sink.duck_ms = DM_FADE_MS;   /* theme.cpp ThemeClass::Fade_Out                     */
    sink.restore_music = 0;      /* dms_run and dms_logo ramp the score back themselves */
    movsnd_bind(&sink, &a);

    return mov_play(s->win, path, &o, &a, NULL);
}

/* ======================================================================== */

int dms_open(DMS *s, SDL_Window *win, const DMS_Config *cfg, char *err, int errlen)
{
    char e2[256];

    memset(s, 0, sizeof *s);
    s->win = win;
    s->cfg = *cfg;
    s->mx = DM_SCREEN_W / 2;
    s->my = DM_SCREEN_H / 2;

    s->pack = db_pack_load(cfg->pack, e2, sizeof e2);
    if (!s->pack) {
        snprintf(err, (size_t)errlen, "%s", e2);
        return 0;
    }
    dm_state_init(&s->st);
    s->st.version = cfg->version ? cfg->version : "CNC3D";
    s->st.selected = DM_START;
    db_surface_init(&s->surf, DM_SCREEN_W, DM_SCREEN_H, s->screen);

    s->tw = dms_pot(DM_SCREEN_W);
    s->th = dms_pot(DM_SCREEN_H);
    s->rgba = (unsigned char *)calloc((size_t)DM_SCREEN_W * DM_SCREEN_H * 4, 1);
    s->padded = (unsigned char *)calloc((size_t)s->tw * s->th * 4, 1);
    s->faded = (unsigned char *)calloc((size_t)DM_SCREEN_W * DM_SCREEN_H * 4, 1);

    glGenTextures(1, &s->tex);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* NOT registered: UI art is never bilinear. See fx_filter.h. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->tw, s->th, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 s->padded);

    /* The menu theme. init.cpp:1054 starts THEME_MAP1 before Select_Game and lets it
       run under the whole menu, so it loops. The name is a theme base name, not a
       path: the bank finds MAP1 wherever it lives, which on the 1995 discs is
       TRANSIT.MIX and not SCORES.MIX with the rest of the score. */
    if (cfg->au && cfg->music && *cfg->music) {
        const int t = cnc_music_theme_index(cfg->music);
        if (t < 0 || !cnc_music_play_index(cfg->au, t))
            fprintf(stderr, "menu: no theme %s on the disc; the menu is silent\n",
                    cfg->music);
        else
            dms_music_gain(s, MIX_UNITY, 0);
    }
    (void)e2;
    return 1;
}

/* A movie, then the menu faded up underneath it. Both entry points do exactly the
 * same thing with a different file, which is why they share this: init.cpp:796 plays
 * the logo before Main_Menu and init.cpp:1199 plays the intro from inside it.
 * A missing or broken file is NOT an error: it says so on stderr and the flow carries
 * on, so a build with no movies folder still boots to the menu. */
static int dms_movie_then_menu(DMS *s, const char *path)
{
    int rc;
    if (!path)
        return 1;
    rc = dms_play_movie(s, path);
    if (rc == MOV_QUIT)
        return 0;
    dms_redraw(s);
    dms_fade(s, s->rgba, 0);
    dms_music_gain(s, MIX_UNITY, DM_FADE_MS);
    return 1;
}

int dms_logo(DMS *s) { return dms_movie_then_menu(s, s->cfg.logo); }
int dms_intro(DMS *s) { return dms_movie_then_menu(s, s->cfg.intro); }

int dms_run(DMS *s)
{
    int chosen = DMS_NONE;

    /* Every visit starts with the pointer released and the highlight on Start, which
     * is what walking back out of a mission into menus.cpp:Select_Game looks like. */
    s->st.pressed = -1;
    SDL_ShowCursor(SDL_DISABLE);   /* the DOS pointer is drawn into the surface */
    /* And the menu theme comes back, because the mission took the score away for its
     * own playlist. mapsel.cpp:529 does the same thing on the way back into the map
     * selection: Queue_Song(THEME_MAP1). Starting it only when it is not already the
     * current track keeps a first visit from restarting it a moment after dms_open. */
    if (s->cfg.au && s->cfg.music && *s->cfg.music &&
        cnc_music_index(s->cfg.au) != cnc_music_theme_index(s->cfg.music)) {
        cnc_music_set_playlist(s->cfg.au, 0);
        cnc_music_play_index(s->cfg.au, cnc_music_theme_index(s->cfg.music));
    }
    dms_music_gain(s, MIX_UNITY, DM_FADE_MS);
    dms_layout(s);
    dms_redraw(s);

    for (;;) {
        SDL_Event e;
        int dirty = 0;

        while (SDL_PollEvent(&e)) {
            if (fs_handle_event(&e)) { dirty = 1; continue; }
            switch (e.type) {
            case SDL_QUIT:
                return DMS_QUIT;

            case SDL_KEYDOWN:
                /* menus.cpp:902-940: the arrows walk curbutton, return activates. */
                if (e.key.keysym.sym == SDLK_ESCAPE)
                    return DM_EXIT;
                else if (e.key.keysym.sym == SDLK_UP)
                    s->st.selected = dm_next_item(&s->st, s->st.selected, -1), dirty = 1;
                else if (e.key.keysym.sym == SDLK_DOWN)
                    s->st.selected = dm_next_item(&s->st, s->st.selected, 1), dirty = 1;
                else if (e.key.keysym.sym == SDLK_RETURN)
                    chosen = s->st.selected;
                break;

            case SDL_MOUSEMOTION:
                dms_to_menu(s, e.motion.x, e.motion.y, &s->mx, &s->my);
                dirty = 1;
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int cx, cy, hit;
                    dms_to_menu(s, e.button.x, e.button.y, &cx, &cy);
                    s->mx = cx;
                    s->my = cy;
                    hit = dm_hit_test(&s->st, cx, cy);
                    if (hit >= 0) {
                        s->st.pressed = hit;
                        s->st.selected = hit;
                    }
                    dirty = 1;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int cx, cy, hit;
                    dms_to_menu(s, e.button.x, e.button.y, &cx, &cy);
                    hit = dm_hit_test(&s->st, cx, cy);
                    if (hit >= 0 && hit == s->st.pressed)
                        chosen = hit;
                    s->st.pressed = -1;
                    dirty = 1;
                }
                break;

            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_EXPOSED)
                    dirty = 1;
                break;

            default:
                break;
            }
        }

        if (chosen >= 0) {
            printf("menu: %s\n", dm_item_label(chosen));
            fflush(stdout);
            /* init.cpp:1199 SEL_INTRO -> Play_Intro(). Handled here because it never
             * leaves the menu; every other item is the caller's business. */
            if (chosen == DM_INTRO) {
                s->st.pressed = -1;
                if (!dms_intro(s))
                    return DMS_QUIT;
                chosen = DMS_NONE;
                dirty = 1;
            } else {
                return chosen;
            }
        }

        if (dirty)
            dms_redraw(s);
        dms_pump_audio(s);
        dms_present(s);
        SDL_Delay(16);
    }
}

/* THE SPECIAL OPS LIST. Its own loop rather than a mode inside dms_run, because the
   two screens answer different questions: dms_run returns a menu item, this returns a
   mission. Everything else -- the plate, the surface, the texture, the pointer, the
   score -- is the same shell, so the screen cannot drift from the menu it opened from. */
int dms_special(DMS *s, const struct DO_Mission *list, int count)
{
    DO_State ops;
    /* DMS_CANCEL, not -1: if this ever falls out of the loop without a decision, backing
       out to the menu is the safe answer. -1 is DMS_QUIT and would close the game. */
    int result = DMS_CANCEL;

    do_state_init(&ops, (const DO_Mission *)list, count);
    s->ops = &ops;
    SDL_ShowCursor(SDL_DISABLE);
    dms_layout(s);
    dms_redraw(s);

    for (;;) {
        SDL_Event e;
        int dirty = 0, done = 0;

        while (SDL_PollEvent(&e)) {
            if (fs_handle_event(&e)) { dirty = 1; continue; }
            switch (e.type) {
            case SDL_QUIT:
                s->ops = NULL;
                return DMS_QUIT;

            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    result = DMS_CANCEL; done = 1;   /* back to the menu, NOT exit */
                } else if (e.key.keysym.sym == SDLK_UP) {
                    do_move(&ops, -1); dirty = 1;
                } else if (e.key.keysym.sym == SDLK_DOWN) {
                    do_move(&ops, 1); dirty = 1;
                } else if (e.key.keysym.sym == SDLK_PAGEUP) {
                    do_move(&ops, -DO_ROWS); dirty = 1;
                } else if (e.key.keysym.sym == SDLK_PAGEDOWN) {
                    do_move(&ops, DO_ROWS); dirty = 1;
                } else if (e.key.keysym.sym == SDLK_RETURN ||
                           e.key.keysym.sym == SDLK_KP_ENTER) {
                    if (ops.selected >= 0) { result = ops.selected; done = 1; }
                }
                break;

            case SDL_MOUSEMOTION:
                dms_to_menu(s, e.motion.x, e.motion.y, &s->mx, &s->my);
                dirty = 1;
                break;

            case SDL_MOUSEWHEEL:
                do_scroll(&ops, -e.wheel.y);
                dirty = 1;
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int cx, cy, hit;
                    dms_to_menu(s, e.button.x, e.button.y, &cx, &cy);
                    s->mx = cx; s->my = cy;
                    hit = do_hit_test(&ops, cx, cy);
                    if (hit >= 0) {
                        ops.selected = hit;
                        ops.pressed = DO_HIT_NONE;
                        /* A double click plays it, the same shortcut the 1995 map and
                           mission lists give (list.cpp:196, ListClass::Action). */
                        if (e.button.clicks >= 2) { result = hit; done = 1; }
                    } else {
                        ops.pressed = hit;
                    }
                    dirty = 1;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int cx, cy, hit;
                    dms_to_menu(s, e.button.x, e.button.y, &cx, &cy);
                    hit = do_hit_test(&ops, cx, cy);
                    if (hit == ops.pressed && hit == DO_HIT_PLAY && ops.selected >= 0) {
                        result = ops.selected; done = 1;
                    } else if (hit == ops.pressed && hit == DO_HIT_CANCEL) {
                        result = DMS_CANCEL; done = 1;   /* back to the menu, NOT exit */
                    }
                    ops.pressed = DO_HIT_NONE;
                    dirty = 1;
                }
                break;

            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_EXPOSED)
                    dirty = 1;
                break;

            default:
                break;
            }
        }

        if (done)
            break;
        if (dirty)
            dms_redraw(s);
        dms_pump_audio(s);
        dms_present(s);
        SDL_Delay(16);
    }

    s->ops = NULL;
    dms_redraw(s);
    if (result >= 0) {
        printf("menu: Special Ops -> %s\n", list[result].scen);
        fflush(stdout);
    }
    return result;
}

/* ======================================================================== *
 * THE SKIRMISH LOBBY.
 *
 * Its own loop for the same reason the mission list has one: the two screens answer
 * different questions, and this one answers with a settings block rather than an index.
 * Everything else -- the plate, the surface, the texture, the pointer, the score, the
 * letterbox -- is the same shell, so the lobby cannot drift from the menu it opened
 * from. See menu/doslobby.h for what is on it and what is deliberately not.
 * ======================================================================== */

/* Menu pixels to window POINTS, the inverse of dms_to_menu, so the harness can put a
   synthetic click exactly where a hand would have to put a real one. */
static void dms_menu_to_window(const DMS *s, int mx, int my, int *wx, int *wy)
{
    *wx = (int)((s->vpx + mx * s->scale) / s->px);
    *wy = (int)((s->vpy + my * s->scale) / s->py);
}

static void dms_lobby_shot(DMS *s, const char *dir, int n)
{
    char path[512];
    unsigned char *big;
    const int scale = 3;
    const int bw = DM_SCREEN_W * scale, bh = DM_SCREEN_H * scale;

    if (!dir || !*dir)
        return;
    big = (unsigned char *)malloc((size_t)bw * bh * 4);
    if (!big)
        return;
    png_nearest_scale(s->rgba, DM_SCREEN_W, DM_SCREEN_H, big, scale);
    snprintf(path, sizeof path, "%s/lobby%02d.png", dir, n);
    if (png_write_rgba(path, big, bw, bh))
        printf("LOBBY|shot|%s|%dx%d\n", path, bw, bh);
    else
        fprintf(stderr, "lobby: could not write %s\n", path);
    fflush(stdout);
    free(big);
}

/* One script step: either a key, or a press/release pair on the item's own rectangle. */
static void dms_lobby_step(DMS *s, const SK_State *st, const DMS_LobbyStep *step)
{
    SDL_Event e;
    int rx = 0, ry = 0, rw = 0, rh = 0, mx, my, wx = 0, wy = 0;

    if (step->key) {
        memset(&e, 0, sizeof e);
        e.type = SDL_KEYDOWN;
        e.key.state = SDL_PRESSED;
        e.key.keysym.sym = (SDL_Keycode)step->key;
        SDL_PushEvent(&e);
        printf("LOBBY|key|%s\n", SDL_GetKeyName((SDL_Keycode)step->key));
        fflush(stdout);
        return;
    }
    if (!sk_item_rect(st, step->item, &rx, &ry, &rw, &rh))
        return;
    mx = rx + (rw - 1) * step->fx / 1000;
    my = ry + (rh - 1) * step->fy / 1000;
    dms_menu_to_window(s, mx, my, &wx, &wy);
    printf("LOBBY|click|%s|menu %d,%d|window %d,%d\n", sk_item_label(st, step->item), mx,
           my, wx, wy);
    fflush(stdout);
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = SDL_PRESSED;
    e.button.clicks = 1;
    e.button.x = wx;
    e.button.y = wy;
    SDL_PushEvent(&e);
    e.type = SDL_MOUSEBUTTONUP;
    e.button.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

static int dms_lobby_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_ESCAPE: return SK_KEY_ESC;
    case SDLK_UP: return SK_KEY_UP;
    case SDLK_DOWN: return SK_KEY_DOWN;
    case SDLK_LEFT: return SK_KEY_LEFT;
    case SDLK_RIGHT: return SK_KEY_RIGHT;
    case SDLK_TAB: return SK_KEY_TAB;
    case SDLK_SPACE: return SK_KEY_SPACE;
    case SDLK_PAGEUP: return SK_KEY_PGUP;
    case SDLK_PAGEDOWN: return SK_KEY_PGDN;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return SK_KEY_ENTER;
    default: return 0;
    }
}

int dms_lobby(DMS *s, const struct SK_Map *maps, int count, const struct SK_Prev *prev,
              struct SK_Lobby *out, DMS_LobbyProbe *probe)
{
    SK_State lob;
    /* DMS_CANCEL, not -1: if this ever falls out of the loop without a decision,
       backing out to the menu is the safe answer. -1 is DMS_QUIT and closes the game. */
    int result = DMS_CANCEL;
    int shots = 0, stepi = 0;

    sk_init(&lob, maps, count, prev);
    s->lobby = &lob;
    SDL_ShowCursor(SDL_DISABLE); /* the DOS pointer is drawn into the surface */
    dms_layout(s);
    dms_redraw(s);
    if (probe) {
        /* Before a single pixel is judged by eye: does every string this screen can
           print fit the box it prints into? It is the one question about a screen made
           of labels that a screenshot answers badly and arithmetic answers exactly. */
        probe->overflow = sk_check_layout(s->pack, &lob);
        if (probe->shotdir)
            dms_lobby_shot(s, probe->shotdir, shots++);
    }

    for (;;) {
        SDL_Event e;
        int dirty = 0, done = 0, act = SK_ACT_NONE;

        while (SDL_PollEvent(&e)) {
            if (fs_handle_event(&e)) { dirty = 1; continue; }
            switch (e.type) {
            case SDL_QUIT:
                s->lobby = NULL;
                return DMS_QUIT;

            case SDL_KEYDOWN: {
                const int k = dms_lobby_key(e.key.keysym.sym);
                if (k) {
                    act = sk_key(&lob, k);
                    dirty = 1;
                }
                break;
            }

            case SDL_MOUSEMOTION:
                dms_to_menu(s, e.motion.x, e.motion.y, &s->mx, &s->my);
                sk_motion(&lob, s->mx, s->my);
                dirty = 1;
                break;

            case SDL_MOUSEWHEEL:
                sk_scroll(&lob, -e.wheel.y);
                dirty = 1;
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int cx, cy;
                    dms_to_menu(s, e.button.x, e.button.y, &cx, &cy);
                    s->mx = cx;
                    s->my = cy;
                    {   /* The map-source tabs come first: a click on one is a change of
                           list, never a change of map. */
                        const int tab = sk_tab_at(&lob, cx, cy);
                        if (tab >= 0) {
                            sk_set_tab(&lob, tab);
                            dirty = 1;
                            break;
                        }
                    }
                    sk_press(&lob, cx, cy);
                    /* A double click on a map row plays it, the same shortcut the 1995
                       map and mission lists give (list.cpp:196, ListClass::Action). */
                    if (e.button.clicks >= 2 && sk_map_row_at(&lob, cx, cy) >= 0)
                        act = SK_ACT_PLAY;
                    dirty = 1;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int cx, cy, a;
                    dms_to_menu(s, e.button.x, e.button.y, &cx, &cy);
                    a = sk_release(&lob, cx, cy);
                    if (a != SK_ACT_NONE)
                        act = a;
                    dirty = 1;
                }
                break;

            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_EXPOSED)
                    dirty = 1;
                break;

            default:
                break;
            }

            if (act == SK_ACT_PLAY && lob.sel >= 0) {
                result = 0;
                done = 1;
            } else if (act == SK_ACT_CANCEL) {
                result = DMS_CANCEL; /* back to the menu, NOT exit */
                done = 1;
            }
            if (done)
                break;
        }

        if (done)
            break;
        if (dirty) {
            dms_redraw(s);
            if (probe && probe->shotdir)
                dms_lobby_shot(s, probe->shotdir, shots++);
        }
        /* One scripted click per pass, and only once the real queue has run dry, so
           each step lands on its own frame and every frame gets its own picture. */
        if (probe && probe->script && stepi < probe->steps)
            dms_lobby_step(s, &lob, &probe->script[stepi++]);
        dms_pump_audio(s);
        dms_present(s);
        SDL_Delay(16);
    }

    if (result == 0 && out)
        sk_result(&lob, out);
    if (probe)
        probe->shots = shots;
    s->lobby = NULL;
    dms_redraw(s);
    if (result == 0 && lob.sel >= 0)
        printf("menu: Multiplayer Game -> %s\n", maps[lob.sel].scen);
    fflush(stdout);
    return result;
}

void dms_close(DMS *s)
{
    if (s->tex)
        fx_filter_forget(s->tex);
        glDeleteTextures(1, &s->tex);
    s->tex = 0;
    /* The audio engine is NOT closed here: the program owns it, not the menu, and
       the menu is opened and closed once per program while missions come and go. */
    if (s->pack)
        db_pack_free(s->pack);
    s->pack = NULL;
    free(s->rgba);
    free(s->padded);
    free(s->faded);
    s->rgba = s->padded = s->faded = NULL;
}
