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
#include "dosmp.h"
#include "../net/netmatch.h"
#include "../net/netbeacon.h"
/* net_local_addrs: what the host reads out to the other players. */
#include "../net/net_udp.h"
/* rc_decode: a room code is a relayed host's whole address. */
#include "../net/roomcode.h"
#include "mpbrowse.h"
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

/* The lobby beats the public game list from a loop that sits above where the sender is
   written, so it is named here rather than moved: the sender belongs beside the rest of
   the browser's half of this file. */
static void dms_mp_list_beat(const char *name, const char *scen, const char *mapname,
                             int filled, int wanted, int locked);
/* WHETHER THE ROOM NOW OPEN ASKED TO BE LISTED BY ADDRESS, and on what port; zero means it
   did not. The lobby screen beats the public list and can see nothing of the multiplayer
   screen's controls, so the answer is left here when the room opens, which is the same
   arrangement nm_is_relayed provides for the other half of the question. Cleared whenever
   the room closes, so the next one starts from unticked. */
static unsigned short s_list_direct_port = 0;

/* defines.h:2226 FADE_PALETTE_MEDIUM is TIMER_SECOND/4, TIMER_SECOND is 60. */
#define DM_FADE_MS 250

/* One pass of the menu loop. Sixteen milliseconds is the SDL_Delay at the bottom of
   dms_run, and it is also how much sound a recording run renders per pass, so the two
   clocks stay together. */
#define DM_FRAME_MS 16

/* THE MULTIPLAYER SCREEN RUNS TWICE AS OFTEN, because it is polling a network rather
   than waiting for a hand, and the pair above must hold for it too. Naming the pace
   once and using it for BOTH the sleep and the audio is what keeps them together: when
   this screen slept 8 ms and told the audio 16, a recording run rendered two seconds of
   score for every second spent here, which is a measurement that lies in the direction
   of everything being fine. Nothing a player hears changes either way, because a real
   device pulls at its own rate. */
#define DM_MP_FRAME_MS 8

/* The menu's half of the audio contract, in one place: everything the menu does to the
   score goes through this, and the movie's own ducking is moviesnd.c's business. That
   makes "the menu talks to the shared engine and never to a device" checkable by
   reading three lines rather than the whole file.

   UP MEANS UP TO THE SLIDER, NOT TO UNITY. cnc_audio_set_music_volume
   (Options.ScoreVolume, 0..255) is the one thing that owns MIX_BUS_MUSIC, and the
   player's remembered controls are pushed at the mixer before this shell ever opens.
   Every place the menu brought the score back used to ask for MIX_UNITY regardless,
   which threw that remembered level away before the first note and threw it away again
   on every return from a mission; the level only reappeared once some dialog's bind
   re-applied the settings block, so from the outside the saved volumes looked like they
   were not loaded until a mission was paused. Same rule the movie sink and the
   side-select screen already follow.

   The one other writer of this bus is cnc_music_fade_out (audio/cncaudio.c:488), which
   ramps it to zero for the campaign's side-select and is restored by app/campaign.c:1235.
   It is not in this path; it is named so "everything the menu does goes through here" is
   not read as "nothing else ever touches this bus". */
static void dms_music_up(DMS *s, int ms)
{
    if (s->cfg.au)
        mixer_bus_gain(cnc_audio_mixer(s->cfg.au), MIX_BUS_MUSIC,
                       cnc_audio_get_music_volume(s->cfg.au) * MIX_UNITY / 255, ms);
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
static void dms_pump_audio(DMS *s, int ms)
{
    audio_frame(s->cfg.au, ms);
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

        dms_pump_audio(s, DM_FRAME_MS);
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
            dms_music_up(s, 0);
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
    dms_music_up(s, DM_FADE_MS);
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
    dms_music_up(s, DM_FADE_MS);
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
        dms_pump_audio(s, DM_FRAME_MS);
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
                } else if (e.key.keysym.sym == SDLK_HOME) {
                    do_move_home(&ops); dirty = 1;
                } else if (e.key.keysym.sym == SDLK_END) {
                    do_move_end(&ops); dirty = 1;
                } else if (e.key.keysym.sym == SDLK_RETURN ||
                           e.key.keysym.sym == SDLK_KP_ENTER) {
                    if (do_row_playable(&ops, ops.selected)) {
                        result = ops.selected; done = 1;
                    }
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
                    if (hit == ops.pressed && hit == DO_HIT_PLAY
                        && do_row_playable(&ops, ops.selected)) {
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
        dms_pump_audio(s, DM_FRAME_MS);
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

/* Write whatever is in the menu surface to a PNG at `path`, scaled up so the 320x200 is
   readable. Public because more than one screen wants it now, and because a screen that
   can only be seen by being played is a screen nobody reviews. */
int dms_write_shot(DMS *s, const char *path)
{
    unsigned char *big;
    const int scale = 3;
    const int bw = DM_SCREEN_W * scale, bh = DM_SCREEN_H * scale;
    int ok;
    if (!s || !path || !*path) return 0;
    db_surface_to_rgba(&s->surf, s->pack->pal8, s->rgba, 0);
    big = (unsigned char *)malloc((size_t)bw * bh * 4);
    if (!big) return 0;
    png_nearest_scale(s->rgba, DM_SCREEN_W, DM_SCREEN_H, big, scale);
    ok = png_write_rgba(path, big, bw, bh);
    if (ok) printf("SHOT|%s|%dx%d\n", path, bw, bh);
    else fprintf(stderr, "shot: could not write %s\n", path);
    fflush(stdout);
    free(big);
    return ok;
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
    if (!sk_item_rect(st, step->item, &rx, &ry, &rw, &rh)) {
        /* SAID, NOT SWALLOWED: a step whose control has no rectangle used to vanish
           without a line, and a script that did less than it claimed passed on a lower
           frame count. */
        printf("LOBBY|skip|%s|no rectangle\n", sk_item_label(st, step->item));
        fflush(stdout);
        return;
    }
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

/* THE SCREEN'S SEAT WORDS AND THE WIRE'S ARE ONE NUMBERING. doslobby.h restates NM_SEAT_*
   because it includes no network header; this is the only file that sees both, so this
   is where a drift between them fails to compile instead of seating a joiner in a chair
   the host called a computer. */
typedef char dms_seat_modes_agree[(SK_SEAT_HUMAN == NM_SEAT_HUMAN && SK_SEAT_BOT == NM_SEAT_BOT &&
                                   SK_SEAT_OPEN == NM_SEAT_OPEN && SK_SEAT_BLOCK == NM_SEAT_BLOCK)
                                      ? 1 : -1];

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
    /* DELETE is the host's kick; BACKSPACE edits the chat line. They shared SK_KEY_DEL
       until the chat existed, and a host correcting a typo would have removed a player. */
    case SDLK_DELETE: return SK_KEY_DEL;
    case SDLK_BACKSPACE: return SK_KEY_BACK;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return SK_KEY_ENTER;
    default: return 0;
    }
}

/* Push what the HOST'S screen now says into the match, so START sends the settings the
   joiners have been reading rather than the ones the room opened with. Mirrors the
   command line's own roster order, humans first and computers after. */
static void dms_lobby_net_push(const SK_State *lob, const struct SK_Map *maps)
{
    NmSetup ns;
    SK_Lobby r;
    int i;
    if (lob->sel < 0 || !maps) return;
    memset(&ns, 0, sizeof ns);
    sk_result(lob, &r);
    snprintf(ns.scenario, sizeof ns.scenario, "%s", maps[lob->sel].scen);
    ns.credits = r.credits;
    ns.tiberium = (unsigned char)r.tiberium;
    ns.crates = (unsigned char)r.crates;
    ns.aitake = r.aitake;
    ns.shortgame = r.shortgame;
    ns.superweapons = (unsigned char)r.superweapons;
    ns.bases = (unsigned char)r.bases;
    ns.unit_count = r.unit_count;
    /* THE TECH LEVEL, the one rule on this screen that never left the machine (6 Sep
       2026): memset leaves ns.build 0, the sync writes that 0 back over the screen ten
       times a second, so the host's slider snapped to the left and would not move while
       Credits and Unit Count worked. The wire has carried it since version 4. */
    ns.build = r.build;
    /* THE NAMES ARE THE WIRE'S, NOT THE SCREEN'S. This struct is memset and rebuilt from
       the lobby every dirty frame; the handles were learned from each joiner's HELLO and
       live only in the setup, so rebuilding without carrying them across wiped all eight
       ten times a second and every player was called PLAYER again. */
    {
        const NmSetup *cur = nm_lobby_setup();
        if (cur) {
            int k;
            for (k = 0; k < NM_MAX_SEATS; k++)
                snprintf(ns.name[k], sizeof ns.name[k], "%s", cur->name[k]);
        }
    }
    /* THE MATCH'S TICK RATE, pushed on every dirty frame like every other rule on this
       screen. There is no speed row for a player to move yet, so what goes out is the
       wire's own default, which is the same slider index a campaign mission ships at.
       This struct is memset and rebuilt each time, so leaving the field alone would send
       0, and 0 is a legitimate speed -- the slowest one -- rather than "not said": every
       joiner would adopt it and the whole room would crawl. */
    ns.speed = NM_DEFAULT_SPEED;
    /* THE ROOM'S WIDTH AND ITS SEAT MODES, seat by seat. A computer is a BOT seat and
       nothing else: there is no AI gauge on this screen in a room, so the count of
       computers is the count of seats set to BOT. netmatch folds the taken table in and
       counts humans off the result; the numbers written here are what the screen shows. */
    ns.seats = lob->net_seats > 0 ? lob->net_seats : sk_players(lob);
    ns.humans = 0;
    for (i = 0; i < NM_MAX_SEATS && i < SK_ROSTER_ROWS; i++) {
        ns.house[i] = (unsigned char)(r.house[i] ? 1 : 0);
        ns.colour[i] = (unsigned char)((r.colour[i] < 0 || r.colour[i] > 7) ? (i & 7)
                                                                           : r.colour[i]);
        ns.team[i] = (unsigned char)r.team[i];
        /* UNPICKED IS RANDOM, not "seat i": the brain deals the seat a start out of
           what is left, from the synchronised stream, so every peer deals the same one. */
        ns.start[i] = (unsigned char)((r.start_wp[i] >= 0) ? r.start_wp[i] : SK_START_RANDOM);
        ns.mode[i] = (unsigned char)((i < ns.seats) ? r.mode[i] : NM_SEAT_BLOCK);
        ns.is_ai[i] = (unsigned char)(ns.mode[i] == NM_SEAT_BOT ? 1 : 0);
        if (ns.mode[i] == NM_SEAT_HUMAN || ns.mode[i] == NM_SEAT_OPEN) ns.humans++;
    }
    nm_lobby_set_setup(&ns);
}

/* Pull the match's live state onto the roster: who is in, who has readied. Called every
   poll so the lights are the wire's answer and never this screen's memory of it.

   AND, FOR A JOINER, THE WHOLE MATCH. Until 5 Sep 2026 a joiner's screen showed the host's
   seats and ready lights and NOTHING ELSE of the host's: the map, the rules and every
   row's side, team and colour were the joiner's own defaults, drawn refused so they
   looked like the host's decisions. They were not. nm_lobby_setup() is the setup the
   host pushed and the match will run on; a joiner's screen is a picture of THAT, and
   what it hands to mp_play through sk_result has to be that too, or the two peers
   boot two different games and part on the first order. The joiner's own row goes the
   same way: its picks never reached the host and the engine ran the host's roster for
   that seat regardless, so drawing them as chosen was the one lie left on this screen.
   Recorded as a gap; the fix is a wire message, not this function. */
/* THE CHAT'S BOOKKEEPING, per visit: how many wire lines the screen has taken, and the
   seat table as it was at the last sync, so an action line is written exactly once on
   the edge it describes. Reset when a lobby opens. */
static int s_chatTaken = 0;
static int s_chatPrevValid = 0;
/* THIS PEER'S OWN ROW AS LAST SENT, so only a change goes on the wire. */
/* MY OWN ROW: what the WIRE last said it is, not what I last sent. The difference is the
   whole of the joiner's seat working or not. These are the baseline a local
   click is measured against; the sync below writes them, the input path reads them. */
static int s_mySeatValid = 0, s_myHouse = 0, s_myTeam = 0, s_myColour = 0, s_myStart = 0;
/* A pick of mine is in flight: the host has been told and has not said yes or no yet.
   While that is true the sync leaves my row alone, so the screen shows what I chose
   rather than flicking back to the host's older copy for a tenth of a second. */
static int s_prefPending = 0;
static unsigned s_prefSentMs = 0;
static int s_prefHouse = 0, s_prefTeam = 0, s_prefColour = 0, s_prefStart = 0;
static int s_chatPrevTaken[SK_ROSTER_ROWS];
static int s_chatPrevReady[SK_ROSTER_ROWS];

/* Who a seat is, for a chat line: the roster's own words, numbered so that three
   joiners are not three lines all saying PLAYER. */
static void dms_chat_name(const SK_State *lob, int seat, char *out, int outlen)
{
    if (seat == lob->net_seat) snprintf(out, (size_t)outlen, "YOU");
    else if (seat == 0) snprintf(out, (size_t)outlen, "HOST");
    else if (nm_seat_name(seat)[0]) snprintf(out, (size_t)outlen, "%s", nm_seat_name(seat));
    else snprintf(out, (size_t)outlen, "PLAYER %d", seat + 1);
}

/* TELL THE ROOM WHAT I CHANGED, BEFORE ANYTHING CAN OVERWRITE IT.

   The joiner's own row is the one part of this screen the person at this machine
   decides, and dms_lobby_net_sync copies the host's setup over the whole screen. So an
   unsent change must leave for the host BEFORE any sync runs, or it is lost and the
   control looks dead -- which is exactly the bug this pair of functions was written to
   end. Putting the flush at the TOP of the sync itself, rather than only on the frame
   loop's beat, is what makes that true of EVERY caller: three of them (KICK, SAY and
   READY) sync from inside the event drain, before the loop's own send would run, and
   any one of them would have restored the fault through a different door.

   The frame loop calls this as well, straight after input, so a click reaches the room
   at once rather than on the next hundred-millisecond poll. Calling it twice is free:
   it compares against the baseline and does nothing when nothing changed. */
static void lobby_flush_my_seat(SK_State *lob)
{
    int me, hs, tm, cl, stt;
    if (!lob || lob->net != 2 || !s_mySeatValid) return;
    me = lob->net_seat;
    if (me < 0 || me >= SK_ROSTER_ROWS) return;
    hs = sk_row_house(lob, me);
    tm = sk_row_team(lob, me);
    cl = sk_row_colour(lob, me);
    stt = sk_row_start(lob, me);
    if (hs == s_myHouse && tm == s_myTeam && cl == s_myColour && stt == s_myStart)
        return;
    s_myHouse = hs; s_myTeam = tm; s_myColour = cl; s_myStart = stt;
    s_prefHouse = hs; s_prefTeam = tm; s_prefColour = cl; s_prefStart = stt;
    s_prefPending = 1;
    s_prefSentMs = SDL_GetTicks();
    nm_lobby_set_my_seat(hs, tm, cl, stt);
}

static void dms_lobby_net_sync(SK_State *lob, const struct SK_Map *maps, int count)
{
    lobby_flush_my_seat(lob);
    int i;
    lob->net_seat = nm_lobby_seat();
    lob->net_humans = nm_lobby_wanted();
    for (i = 0; i < SK_ROSTER_ROWS; i++) {
        lob->net_taken[i] = nm_lobby_seat_taken(i);
        lob->net_ready[i] = nm_lobby_seat_ready(i);
    }
    /* THE ACTION LINES, synthesised on the edges this screen can see rather than sent as
       text: a ready tick reaches every peer, so "is Ready!" written on the local edge is
       right everywhere, and a joined or left seat is the host's own table and a joiner's
       stamped copy. Never on the first sync -- the room as found is not an event. */
    /* AND NEVER BEFORE THE ROOM ARRIVES. A joiner's own copy of the setup is all zeros
       until the first WELCOME, and zero is NM_SEAT_HUMAN, so every seat reads TAKEN;
       the host's real room then turns six of them into "left the game" for players who
       were never there (reported the moment LAN play first worked, 6 Sep 2026). While a
       joiner has no seat there is nothing to compare, so the snapshot is dropped and
       re-primed on the sync that seats it. */
    if (lob->net == 2 && lob->net_seat < 0) s_chatPrevValid = 0;
    if (s_chatPrevValid) {
        char who[24], line[SK_CHAT_MAX];
        const int rows = sk_players(lob);
        for (i = 0; i < rows && i < SK_ROSTER_ROWS; i++) {
            if (lob->net_taken[i] != s_chatPrevTaken[i]) {
                dms_chat_name(lob, i, who, (int)sizeof who);
                snprintf(line, sizeof line, "%s %s the game", who,
                         lob->net_taken[i] ? "joined" : "left");
                sk_chat_push(lob, -1, line);
            } else if (lob->net_taken[i] && lob->net_ready[i] != s_chatPrevReady[i] && i != 0) {
                dms_chat_name(lob, i, who, (int)sizeof who);
                snprintf(line, sizeof line, "%s is %sReady%s", who,
                         lob->net_ready[i] ? "" : "not ", lob->net_ready[i] ? "!" : "");
                sk_chat_push(lob, -1, line);
            }
        }
    }
    for (i = 0; i < SK_ROSTER_ROWS; i++) {
        s_chatPrevTaken[i] = lob->net_taken[i];
        s_chatPrevReady[i] = lob->net_ready[i];
    }
    if (!(lob->net == 2 && lob->net_seat < 0)) s_chatPrevValid = 1;
    /* And the lines people typed, exactly the ones not yet taken, each prefixed with its
       speaker so the colour and the name agree. */
    while (s_chatTaken < nm_chat_count()) {
        int seat = -1;
        const char *t = nm_chat_line(s_chatTaken, &seat);
        s_chatTaken++;
        if (t) {
            char who[24], line[SK_CHAT_MAX];
            dms_chat_name(lob, seat, who, (int)sizeof who);
            snprintf(line, sizeof line, "%s: %s", who, t);
            sk_chat_push(lob, seat, line);
        }
    }
    /* THE ROOM'S WIDTH AND EVERY SEAT'S MODE, on both ends, off the setup the wire holds.
       On the host that is its own push folded with who has sat down; on a joiner it is
       the host's stamped copy, re-welcomed whenever it changes. */
    /* NOT BEFORE THE SEAT: a joiner's copy of the setup is all zeros until the first
       WELCOME lands, and zeros drawn as a room read as one row called HOST with every
       slider at 0 (the first real-network test, 5 Sep 2026). Until it is seated the
       screen keeps its own defaults and the status line says it is connecting. */
    if (nm_lobby_setup() && (nm_lobby_is_host() || nm_lobby_seat() >= 0)) {
        const NmSetup *ns = nm_lobby_setup();
        int bots = 0;
        lob->net_seats = ns->seats;
        for (i = 0; i < SK_ROSTER_ROWS && i < NM_MAX_SEATS; i++) {
            lob->mode[i] = ns->mode[i];
            snprintf(lob->name[i], sizeof lob->name[i], "%s", ns->name[i]);
            if (ns->mode[i] == NM_SEAT_BOT) bots++;
        }
        (void)bots;   /* the count is read off the modes now; there is no gauge to feed */
    }
    /* BOTH ENDS ADOPT THE SETUP ON THE WIRE, not only a joiner. The host's
       lobby used to open on a fresh screen while the room already held the setup chosen
       in SET UP MATCH, and now that the host's screen is pushed to the wire on every
       change, a fresh screen would have overwritten that setup with defaults on its
       first frame. Reading the wire back is idempotent for the host -- it is its own
       push, folded with who has sat down -- and it is what makes the screen and the
       room one thing. */
    if (nm_lobby_setup() && maps) {
        const NmSetup *ns = nm_lobby_setup();
        if (ns->scenario[0]) {
            for (i = 0; i < count; i++)
                if (maps[i].scen && strcmp(maps[i].scen, ns->scenario) == 0) { lob->sel = i; break; }
        }
        lob->credits = ns->credits;
        lob->build   = ns->build;
        lob->super   = ns->superweapons ? 1 : 0;
        lob->crates  = ns->crates ? 1 : 0;
        lob->aitake  = ns->aitake ? 1 : 0;
        lob->shortgame = ns->shortgame ? 1 : 0;
        lob->units   = ns->unit_count;
        lob->side = ns->house[0] ? 1 : 0;
        for (i = 0; i < SK_ROSTER_ROWS && i < NM_MAX_SEATS; i++) {
            const int st_wire = (ns->start[i] == SK_START_RANDOM) ? -1 : ns->start[i];
            /* MY OWN ROW, WHILE MY PICK IS IN FLIGHT, IS MINE. Everything else on this
               screen is the room's and is copied over the top without argument; my seat
               is the one thing I am deciding, and the host has not answered yet.

               Without this the joiner could not pick a colour or a start AT ALL. The
               order in the frame loop is: handle input, then poll the room. A click
               changed the row, the poll 100 ms later copied the host's older setup over
               it, and the edge test that was supposed to notice the change and send it
               ran AFTER that copy -- so it compared the host's value with the host's
               value, found no change, and sent nothing, for ever. The control looked
               dead because it was: the pick was reverted before anything could report it.

               The hold ends the moment the host's setup agrees with what was sent (the
               pick took) or after a second and a half (the host refused, or a packet was
               lost) -- and then the host's answer stands, which is what makes a refusal
               visible instead of a screen that argues with the room. */
            if (lob->net == 2 && i == lob->net_seat && s_prefPending) {
                const int agreed = ((ns->house[i] ? 1 : 0) == s_prefHouse
                                    && ns->team[i] == s_prefTeam
                                    && ns->colour[i] == s_prefColour
                                    && st_wire == s_prefStart);
                if (!agreed && SDL_GetTicks() - s_prefSentMs < 1500)
                    continue;
                s_prefPending = 0;
            }
            lob->house[i]     = ns->house[i] ? 1 : 0;
            lob->house_set[i] = 1;
            lob->team[i]      = ns->team[i];
            lob->colour[i]    = ns->colour[i];
            /* And the host's start assignment, so a joiner's preview shows the real
               picks rather than its own defaults. RANDOM on the wire is unpicked here. */
            lob->start[i]     = st_wire;
        }
        /* THE BASELINE IS WHATEVER THE WIRE JUST SAID, so the next local click is
           measured against the room and not against the last thing this machine sent.
           Set here rather than in the input path: a value that arrived from the host is
           not a choice of mine and must never be echoed back at it, which is how a
           refused pick turns into two peers sending it back and forth. */
        if (lob->net == 2 && lob->net_seat >= 0 && lob->net_seat < SK_ROSTER_ROWS
            && !s_prefPending) {
            s_myHouse  = sk_row_house(lob, lob->net_seat);
            s_myTeam   = sk_row_team(lob, lob->net_seat);
            s_myColour = sk_row_colour(lob, lob->net_seat);
            s_myStart  = sk_row_start(lob, lob->net_seat);
            s_mySeatValid = 1;
        }
    }
}

int dms_lobby(DMS *s, const struct SK_Map *maps, int count, const struct SK_Prev *prev,
              struct SK_Lobby *out, DMS_LobbyProbe *probe)
{
    return dms_lobby_net(s, maps, count, prev, out, probe, 0);
}

int dms_lobby_net(DMS *s, const struct SK_Map *maps, int count, const struct SK_Prev *prev,
                  struct SK_Lobby *out, DMS_LobbyProbe *probe, int net)
{
    SK_State lob;
    unsigned last_net = 0, last_blink = 0;
    int was_measuring = 0;   /* the host's START was waiting on round trips last poll */
    /* DMS_CANCEL, not -1: if this ever falls out of the loop without a decision,
       backing out to the menu is the safe answer. -1 is DMS_QUIT and closes the game. */
    int result = DMS_CANCEL;
    int shots = 0, stepi = 0;

    sk_init(&lob, maps, count, prev);
    lob.net = net;
    lob.net_seat = -1;
    /* A JOINER'S ROWS ARE EMPTY UNTIL THE HOST'S ROOM ARRIVES. sk_init seats a human in
       row 0 for a skirmish; on a joiner that read as a row called HOST (or PLAYER, on a
       build that adopted the empty wire) before anything had been heard. */
    if (net == 2) { int k; for (k = 0; k < SK_ROSTER_ROWS; k++) lob.mode[k] = SK_SEAT_OPEN; }
    s_chatTaken = 0;
    s_chatPrevValid = 0;
    s_mySeatValid = 0;
    s_prefPending = 0;
    if (net) dms_lobby_net_sync(&lob, maps, count);
    s->lobby = &lob;
    SDL_ShowCursor(SDL_DISABLE); /* the DOS pointer is drawn into the surface */
    dms_layout(s);
    /* Typed text for the chat line, in a room only, and stopped on every way out below
       (the multiplayer screen does the same and the two must match). */
    if (net) SDL_StartTextInput();
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
                if (net) SDL_StopTextInput();
                return DMS_QUIT;

            case SDL_TEXTINPUT:
                if (net && lob.chat_focus) {
                    sk_text(&lob, e.text.text[0]);
                    dirty = 1;
                }
                break;

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
                    sk_press(&lob, cx, cy);
                    /* A DOUBLE CLICK ON A ROW IN THE MAP PICKER IS OK: the 1995 list
                       shortcut (list.cpp:196, ListClass::Action), confined to the window
                       that has a list. The shell is the thing that knows what a double
                       click is; the screen stays SDL-free and sees Enter. Nowhere else
                       on the lobby does a second click mean anything -- the day it meant
                       Play, a host double-clicking a map left the room. */
                    if (e.button.clicks >= 2 && sk_map_row_at(&lob, cx, cy) >= 0)
                        act = sk_key(&lob, SK_KEY_ENTER);
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

            if (act == SK_ACT_KICK) {
                const int seat = sk_net_kick_seat(&lob);
                if (seat >= 0 && nm_lobby_kick(seat)) dms_lobby_net_sync(&lob, maps, count);
                dirty = 1;
            } else if (act == SK_ACT_SAY) {
                /* ON THE WIRE AT ONCE, and the screen's copy is taken on the same sync
                   the speaker's own line comes back through, so the speaker sees it in
                   the same colour and order everybody else does. */
                /* NOT BEFORE THE SEAT: nm_chat_say refuses while unseated and says
                   nothing, so clearing the line threw away what was typed while the room
                   was still arriving. */
                if (nm_lobby_seat() >= 0 || nm_lobby_is_host()) {
                    nm_chat_say(lob.chat_in);
                    lob.chat_in[0] = '\0';
                }
                /* Unseated: the line STAYS in the box rather than vanishing, and the
                   room's own status line already says it is still connecting. */
                dms_lobby_net_sync(&lob, maps, count);
                dirty = 1;
            } else if (act == SK_ACT_READY) {
                /* A JOINER'S OWN TOGGLE. It goes on the wire immediately: the host's
                   START is gated on it, so a ready that sat here until the next poll
                   would be a button that looked pressed and changed nothing. */
                nm_lobby_set_ready(!nm_lobby_my_ready());
                dms_lobby_net_sync(&lob, maps, count);
                dirty = 1;
            } else if (act == SK_ACT_PLAY && lob.sel >= 0) {
                if (net == 1) {
                    /* THE HOST STARTS EVERYONE. This does not leave the screen: the
                       match is armed when every joiner has acknowledged the start, which
                       the poll below notices. Leaving here would drop the room. The
                       screen's settings go first, because START sends what the match
                       holds and the host may have changed the map since it opened.
                       NOR DOES IT START ON THIS FRAME, necessarily: nm_lobby_start holds
                       the press until every seat's round trip is in, and the poll below
                       sends it, so a hand quicker than the first ping cannot start the
                       room on a lookahead nobody measured. */
                    dms_lobby_net_push(&lob, maps);
                    nm_lobby_start();
                    dirty = 1;
                } else {
                    result = 0;
                    done = 1;
                }
            } else if (act == SK_ACT_CANCEL) {
                if (net) {
                    nm_lobby_cancel();
                    nb_announce_close();
                    mb_unpublish();
                }
                result = DMS_CANCEL; /* back to the menu, NOT exit */
                done = 1;
            }
            /* ONE ACTION, ONE EVENT. `act` is declared outside this drain so that a
               keypress and a click can both reach the chain above, and nothing reset it
               once the chain had run: every REMAINING event in the same drain re-fired
               the last action. A mouse-up on READY is normally followed by a stray
               motion event, so READY toggled on and straight back off, the host never
               saw the joiner ready, and START stayed refused -- a button that looked
               pressed and changed nothing. The same staleness re-ran SAY, and on the
               host re-pushed the room and re-sent START, once per trailing event. */
            act = SK_ACT_NONE;
            if (done)
                break;
        }

        if (done)
            break;

        /* THE HOST'S SCREEN IS THE ROOM'S SETUP, continuously and not only at START: a
           seat set to BOT has to reach the wire before the next joiner knocks, or that
           joiner is seated in a chair the host just gave to a computer. nm_lobby_set_setup
           compares and re-welcomes only on a change, so pushing on every dirty frame costs
           a struct copy and nothing on the network. */
        /* AND NOT WHILE THE MATCH IS STARTING: the room is settled when START is pressed
           (netmatch refuses the write too, which is the belt to this brace). */
        if (net == 1 && dirty && !nm_lobby_starting())
            dms_lobby_net_push(&lob, maps);

        /* AND A JOINER'S OWN ROW REACHES THE ROOM ON THE SAME BEAT, for the same reason
           and with the same shape: side, team, colour and start are the seat's own to
           decide, and the room has to hear about it.

           BEFORE THE POLL, NOT AFTER IT. This ran after dms_lobby_net_sync until 6 Sep
           2026, which meant the host's setup had already been copied over the click it
           was meant to notice; it compared the room's value with the room's value and
           sent nothing, every time. A joiner could not change colour or start at all.
           Here it sits where the host's push sits -- on a dirty frame, straight after
           input -- so what it reads is what the person just did.

           s_mySeatValid guards the first pass: before any WELCOME there is no baseline,
           and sending a row this peer has not been told about yet would push the screen's
           defaults at the host as if they were a decision. */
        if (net == 2 && dirty)
            lobby_flush_my_seat(&lob);

        if (net && lob.chat_focus && SDL_GetTicks() - last_blink > 400) {
            last_blink = SDL_GetTicks();
            lob.caret_on = !lob.caret_on;
            dirty = 1;
        }

        /* ---- the match, ten times a second ---- */
        if (net && SDL_GetTicks() - last_net > 100) {
            const int ls = nm_lobby_poll();
            last_net = SDL_GetTicks();
            dms_lobby_net_sync(&lob, maps, count);
            /* THE HOST'S OWN LINE, refreshed every poll rather than written once: the
               room code and the address are both facts about the socket, and asking the
               network each time is what stops this screen and the wire disagreeing.
               Host only -- a joiner's own address is of no use to anybody. */
            if (net == 1) {
                if (nm_is_relayed() && nm_room_code()[0]) {
                    snprintf(lob.net_join, sizeof lob.net_join, "ROOM CODE  %s",
                             nm_room_code());
                } else {
                    char la[4][64];
                    if (net_local_addrs(la, 4) > 0) {
                        snprintf(lob.net_join, sizeof lob.net_join, "OTHERS TYPE  %s:%u",
                                 la[0], (unsigned)NM_PORT_DEFAULT);
                    } else {
                        lob.net_join[0] = '\0';
                    }
                }
            } else {
                lob.net_join[0] = '\0';
            }
            if (net == 1) {
                /* KEEP SAYING SO WHILE THERE IS ROOM. A full game still on either list is
                   a row every browser offers and every joiner is refused by.
                   THE TWO LISTS TAKE OPPOSITE ROOMS, which is why the guard moved off this
                   line and onto the calls. A RELAYED room announces nothing on the LAN:
                   its match socket is an ephemeral port on a tunnel, so a LAN row pointing
                   at NM_PORT_DEFAULT here is a door that is not there. And only a relayed
                   room goes on the PUBLIC list, because what stands there for one is a
                   code that names no machine. Guarding the whole block on one of them, as
                   this line used to, meant the public beat could never run for the only
                   kind of room it was for. */
                /* AND NOT WHILE A PRESSED START IS BEING MEASURED, NOR ONCE IT HAS GONE
                   OUT. The press closes the room: netmatch seats nobody during the wait,
                   so a row still offered then is a door that will not open. The second
                   half was missing and it was the worse half, because the two flags change
                   in the same breath: the pending one clears exactly when the START goes
                   out, and from that moment every open seat has been closed and no knock
                   is answered at all. So the beat came BACK for the whole of the start,
                   which is allowed to run to fifteen seconds, and every browser that
                   offered the row sent somebody into a ten second timeout. */
                const int filled = nm_lobby_filled();
                if (filled < nm_lobby_wanted() && lob.sel >= 0
                    && !nm_lobby_start_pending() && !nm_lobby_starting()) {
                    /* THE LOCK TRAVELS: a passcoded room used to advertise as open, so a
                       joiner could not know it would be refused. */
                    /* THE NAME TRAVELS WITH THE CODE: the browser printed SCM01EA where
                       the map is called Green Acres. */
                    /* THE LAST TWO ARE THE WHOLE POINT OF THE ROW BEING GREY. The
                       browser refuses to offer a game built against a different order
                       wire or holding a different copy of the map -- dms_mp_refill makes
                       exactly that judgement -- and it can only make it if the beacon
                       carries the two numbers to judge. This call passed 0 and 0, and 0
                       means "cannot answer", so the judgement was switched off for every
                       room ever announced from inside a lobby and every such row was
                       drawn joinable. The values are asked of netmatch rather than
                       recomputed here, so what is advertised is by construction the same
                       thing the handshake will refuse against. */
                    if (!nm_is_relayed())
                        nb_announce(nm_lobby_name(), maps[lob.sel].scen, maps[lob.sel].name,
                                    NM_PORT_DEFAULT,
                                    filled, nm_lobby_wanted(), nm_lobby_locked(),
                                    nm_abi(), nm_scen());
                    /* AND THE SAME COUNTS TO THE WORLD, for a relayed room. Both take
                       their numbers from the same call, so a browser on either side of a
                       router sees one game rather than two descriptions of it. */
                    dms_mp_list_beat(nm_lobby_name(), maps[lob.sel].scen,
                                     maps[lob.sel].name, filled, nm_lobby_wanted(),
                                     nm_lobby_locked());
                }
            }
            /* WHAT THE ROOM IS WAITING FOR, in the chat pane's last line, whenever no
               refusal is on it: a click clears the status and the next poll puts the
               room's own sentence back. Both ends, each with its own half. */
            /* THE SAME TEST THE PANE DRAWS BY. Half the status table was blanked to "" by
               the no-explanatory-text ruling, and "" is not NULL: a click on Crates or
               Superweapons in a room set it, printed nothing, and suppressed the room's
               own line for ever. */
            /* A PRESSED START THAT IS STILL TIMING THE ROOM SAYS SO, and the line is
               re-derived on both edges of that wait: a room line otherwise stays until a
               click clears it, so without this "Measuring" would outlive a start that
               was cancelled, and the room's own line would hide a wait that began. */
            {
                const int measuring = (net == 1) && nm_lobby_start_pending();
                if (measuring != was_measuring) {
                    was_measuring = measuring;
                    lob.status = NULL;
                }
                if (!lob.status || !lob.status[0])
                    sk_say_room(&lob, nm_lobby_wanted(), nm_lobby_filled(),
                                nm_lobby_all_ready(), nm_lobby_my_ready(), measuring);
            }
            if (ls == NM_LOBBY_STARTED) {
                /* A GAME THAT HAS STARTED IS NOT AN OPEN GAME. Nothing here withdrew the
                   row, so a relayed game stayed on the public list until its entry expired
                   forty five seconds later, offering every browser a door that refuses
                   everyone who knocks. The LAN row healed itself in four seconds; the
                   public one only ever goes away because something says so. */
                nb_announce_close();
                mb_unpublish();
                result = DMS_MP_PLAY;
                break;
            }
            if (ls == NM_LOBBY_REFUSED || ls == NM_LOBBY_FAILED) {
                /* HAND THE SOCKET BACK on the way out. Leaving it open kept a slot in
                   the layer's fixed pool of six for the rest of the session, so a
                   player who tried a few times ran out of sockets rather than of
                   patience: "could not open a socket to join with" on the sixth try. */
                nm_lobby_cancel();
                result = DMS_CANCEL;
                break;
            }
            dirty = 1;
        }

        if (dirty) {
            dms_redraw(s);
            if (probe && probe->shotdir)
                dms_lobby_shot(s, probe->shotdir, shots++);
        }
        /* One scripted click per pass, and only once the real queue has run dry, so
           each step lands on its own frame and every frame gets its own picture. */
        if (probe && probe->script && stepi < probe->steps)
            dms_lobby_step(s, &lob, &probe->script[stepi++]);
        dms_pump_audio(s, DM_FRAME_MS);
        dms_present(s);
        SDL_Delay(16);
    }

    if ((result == 0 || result == DMS_MP_PLAY) && out)
        sk_result(&lob, out);
    if (probe)
        probe->shots = shots;
    s->lobby = NULL;
    if (net) SDL_StopTextInput();
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

/* ================================================================ MULTIPLAYER ======== *
 *  See dosmenu_shell.h for the contract and dosmp.h for the screen. This file owns the
 *  SDL loop, the text input and the networking; the screen owns no socket at all, which
 *  is what lets it be drawn and hit-tested with no network in the process.
 * ==================================================================================== */

static int dms_mp_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_BACKSPACE: return MP_K_BACK;
    case SDLK_TAB:       return MP_K_TAB;
    case SDLK_ESCAPE:    return MP_K_ESC;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return MP_K_ENTER;
    case SDLK_UP:        return MP_K_UP;
    case SDLK_DOWN:      return MP_K_DOWN;
    /* The four keys that page the game list. None of them is taken by the fullscreen
       handler, which answers only its own two combinations before this is asked. */
    case SDLK_PAGEUP:    return MP_K_PGUP;
    case SDLK_PAGEDOWN:  return MP_K_PGDN;
    case SDLK_HOME:      return MP_K_HOME;
    case SDLK_END:       return MP_K_END;
    default:             return 0;
    }
}

/* Refill the browser from the beacon. The rows the screen draws are a COPY: the beacon's
   own list is swept for staleness on every poll, and a screen holding pointers into it
   would draw a game that left half a frame ago. */
/* "host" or "host:port" -> a host and a port. There is no parser for this anywhere in
   the tree to reuse: netcheck and the game's own --join both take the two as SEPARATE
   command-line words, and the only other colon-splitting in net/ is netbeacon throwing an
   ephemeral port away. So this is the one place that knows the grammar, because it is the
   one place that needs it.

   IT REFUSES RATHER THAN GUESSES. "192.168.1.9:" and "host:0" and "host:70000" are
   mistakes, and turning a mistake into port 17421 silently sends the player to knock on a
   door they did not name and then tells them nobody answered. A second colon is refused
   outright rather than mis-parsed: net_resolve pins AF_INET today (net/net_udp.c), so an
   IPv6 literal cannot work anyway, and refusing it by name now is better than parsing it
   wrongly the day that changes. Returns 1 on success, 0 on anything else. */
int dms_parse_target(const char *text, char *host, size_t hostcap,
                     unsigned short *port, unsigned long *room_id)
{
    const char *p = text;
    if (!text || !room_id) return DMS_ADDR_NONE;
    *room_id = 0ul;
    while (*p == ' ') p++;
    /* THE SIGIL DECIDES, and it decides before anything else looks at the string. A room
       code must never reach the host parser, and a hostname must never reach rc_decode. */
    if (*p == '#') {
        return rc_decode(p, room_id) ? DMS_ADDR_ROOM : DMS_ADDR_NONE;
    }
    return dms_parse_hostport(text, host, hostcap, port) ? DMS_ADDR_HOST : DMS_ADDR_NONE;
}

int dms_parse_hostport(const char *text, char *host, size_t hostcap,
                       unsigned short *port)
{
    const char *colon;
    size_t n;
    long v;
    char *end;
    if (!text || !host || hostcap == 0 || !port) return 0;
    while (*text == ' ') text++;
    colon = strchr(text, ':');
    if (colon && strchr(colon + 1, ':')) return 0;      /* IPv6 literal, or a typo */
    n = colon ? (size_t)(colon - text) : strlen(text);
    while (n > 0 && text[n - 1] == ' ') n--;
    if (n == 0 || n >= hostcap) return 0;
    memcpy(host, text, n);
    host[n] = '\0';
    if (!colon) return 1;                               /* no port given: the default */
    if (!colon[1]) return 0;                            /* "host:" is a half-typed port */
    v = strtol(colon + 1, &end, 10);
    while (*end == ' ') end++;
    if (*end) return 0;                                 /* trailing rubbish */
    if (v < 1 || v > 65535) return 0;
    *port = (unsigned short)v;
    return 1;
}

static void dms_mp_refill(MP_State *st, unsigned abi)
{
    int i, n;
    nb_browse_poll();
    n = nb_browse_count();
    if (n > MP_MAX_ROWS) n = MP_MAX_ROWS;
    st->rowcount = n;
    st->rowhidden = 0;
    for (i = 0; i < n; i++) {
        const NbGame *g = nb_browse_get(i);
        MP_Row *r = &st->rows[i];
        if (!g) { st->rowcount = i; break; }
        /* EVERY FIELD, EVERY TIME: a row left over from the internet list would otherwise
           carry its room code and its relay flag onto a LAN row at the same position. */
        memset(r, 0, sizeof *r);
        r->lan = 1;
        snprintf(r->name, sizeof r->name, "%s", g->name);
        /* THE NAME IF THE HOST SENT ONE, THE CODE IF IT DID NOT: only the host can name
           a map this machine does not have. */
        snprintf(r->map, sizeof r->map, "%s", g->mapname[0] ? g->mapname : g->scenario);
        snprintf(r->addr, sizeof r->addr, "%s", g->addr);
        r->port = g->port;
        r->players_now = g->players_now;
        r->players_max = g->players_max;
        r->locked = g->has_password;
        /* JOINABLE IS A JUDGEMENT AND IT IS MADE HERE, not in the browser's drawing code.
           A full game and a game built against a different order wire are both unjoinable
           and are both still LISTED, greyed: a row you cannot use is information, and
           hiding it makes the list look empty for a reason nobody can see. abi 0 on
           either side means "could not compute one", which disables the check rather than
           failing it. */
        r->why = MP_WHY_OK;
        if (r->players_now >= r->players_max) r->why = MP_WHY_FULL;
        if (abi != 0 && g->abi != 0 && g->abi != abi) r->why = MP_WHY_BUILD;
        r->joinable = (r->why == MP_WHY_OK);
        /* A LAN ROW IS KEYED BY WHERE IT ANSWERS, because two games on one network may
           well share a name. */
        mp_row_key(r, NULL);
    }
    /* Sorted, filtered, and the selection put back on its game rather than on whatever
       game now arrived at its old position. */
    mp_view(st, MP_VIEW_REFILL);
}

/* KEEP THIS MACHINE'S OWN GAME ON THE PUBLIC LIST. Called wherever the LAN advert beats,
   and for the same reason: a row that stops being heard from is dropped, so being listed
   is something a host keeps doing rather than something it did once.
 *
 * A RELAYED ROOM IS PUBLISHED WITHOUT ASKING; A DIRECT ONE ONLY IF IT WAS ASKED FOR. What
 * stands in the list for a relayed room is a code that names no machine, so a host loses
 * nothing by being found. A room that is NOT relayed can only be listed by the address it
 * answers on, which is that player's own connection, and that is not something to do to
 * somebody because they left a box unticked. So the screen has a box that says what it
 * costs, and this beat only carries a direct row when a hand has ticked it.
 *
 * THE ADDRESS ITSELF IS STILL NOT SENT. The list takes it from the connection the beat
 * arrives on, so a host can list itself and nothing else. Only the port travels.
 *
 * THE BEAT IS RATE LIMITED HERE rather than inside the sender, because this is called from
 * a loop that runs ten times a second and the list is a server somebody else pays for. */
static void dms_mp_list_beat(const char *name, const char *scen, const char *mapname,
                             int filled, int wanted, int locked)
{
    static unsigned last_beat = 0;
    const unsigned now = SDL_GetTicks();
    const int relayed = nm_is_relayed() && nm_room_code()[0] != '\0';
    MB_Row row;

    if (!relayed && !s_list_direct_port) return;
    if (last_beat != 0 && now - last_beat < MB_BEAT_MS) return;
    last_beat = now ? now : 1;

    memset(&row, 0, sizeof row);
    snprintf(row.name, sizeof row.name, "%s", name ? name : "");
    snprintf(row.map, sizeof row.map, "%s", mapname ? mapname : "");
    snprintf(row.scenario, sizeof row.scenario, "%s", scen ? scen : "");
    if (relayed)
        snprintf(row.room, sizeof row.room, "%s", nm_room_code());
    else
        row.port = s_list_direct_port;
    row.relay = relayed ? 1 : 0;
    row.players_now = filled;
    row.players_max = wanted;
    row.locked = locked;
    /* THE SAME TWO NUMBERS THE HANDSHAKE WILL REFUSE AGAINST, asked of the network rather
       than recomputed here, so a row cannot advertise one thing and refuse another. */
    row.abi = nm_abi();
    row.scen = nm_scen();
    mb_publish(&row);
}

/* A RELAYED ROW IS JOINED BY ARMING THE RELAY, NOT BY RESOLVING ANYTHING. The code in the
   row is the host's id on the tunnel and names no machine, so there is nothing to look up
   and no port for anybody to forward. This peer draws its OWN id and never tells anyone:
   the host learns it from the first packet, exactly as it learns an address on a LAN.
   Returns 0 having put the reason in the status line. */
static int dms_arm_row_relay(const MP_Row *r, MP_State *st)
{
    unsigned long room = 0ul;
    unsigned long mine;
    if (!rc_decode(r->room, &room)) {
        snprintf(st->status, sizeof st->status, "%s",
                 "THAT GAME'S CODE IS NOT ONE THIS BUILD KNOWS.");
        return 0;
    }
    mine = rc_draw_peer_id();
    if (mine == 0ul) {
        snprintf(st->status, sizeof st->status, "%s",
                 "THIS COMPUTER WOULD NOT GIVE A RANDOM NUMBER.");
        return 0;
    }
    nm_relay_next(NM_RELAY_HOST, NM_RELAY_PORT, mine, room);
    return 1;
}

/* HOW OFTEN THE PUBLIC LIST IS ASKED. The LAN half listens ten times a second because
   listening is free; this half is a round trip to a server somebody else pays for, and a
   game that opened four seconds ago is still news. */
#define MP_LIST_EVERY_MS 4000

/* THE WAITING ROOM'S LINE WHILE A PRESSED START TIMES THE ROOM'S LINKS. One spelling, so
   the edge that wipes it compares against the same bytes that were written. */
#define MP_SAY_MEASURING "MEASURING THE CONNECTION..."

/* ---- THE PING COLUMN -----------------------------------------------------------------
   What netmatch's prober has measured, written into the rows the list draws. The prober
   asks whatever it is handed; WHEN a room is asked is decided here, because that is a
   decision about the player rather than about the wire.

   A RELAYED ROOM IS ASKED WHEN IT FIRST APPEARS, and again after REFRESH. Its probe
   reaches the host from a random tunnel id and says nothing about who is browsing. It is
   not asked again on every fetch of the list: that would be a round of probes at every
   room every four seconds, for a number that does not change that fast.

   A DIRECT ROOM IS ASKED ONLY ONCE THE PLAYER HAS SELECTED IT ON THIS VISIT. A direct
   probe leaves from this player's own address, and a host nobody has clicked has no
   business learning it. Until then its cell is blank and a PING sort puts it with the
   other unknowns, last. A selection this screen opened with, left over from an earlier
   visit, does not count until the player presses a row or moves the selection: it is
   still highlighted, but nobody chose it just now.

   A ROW THIS BUILD WOULD BE REFUSED FROM IS NEVER ASKED, and a LAN row leaves its cell
   empty.

   THIRTY-TWO RELAYED ROOMS ARE ONE RELAY REGISTRATION, NOT THIRTY-TWO. Every relayed probe
   leaves on the prober's single tunnel socket, whose id is one of the few clients a relay
   allows an address, and at most NM_PROBE_RUNNING_MAX rooms are asked at once: the rest
   wait their turn inside the prober and read "..." meanwhile. Clearing keeps that socket,
   so a REFRESH, a change of tab or another visit to this screen asks through the same
   registration instead of spending a new one each time. A registration that stops being
   used lapses at the relay on its own.

   NOTHING HERE WAITS. Starting a target sends at most one datagram, and the relay and
   every listed address are numeric, so no name is looked up on a frame. The prober is
   pumped once a frame so the uneven gaps between its probes survive, and the cells are
   read at the list's own ten times a second.

   NOTHING IS ASKED WHILE A MATCH IS BEING JOINED OR PLAYED. Only this screen's loop pumps
   the prober, the pump runs after a press has been acted on, and every target is
   forgotten on the way out, so no probe leaves once a join or a room has been asked for.
   ---------------------------------------------------------------------------------------- */
#define DMS_PING_KEEP 64

typedef struct DmsPing {
    char target[NM_PROBE_KEY_MAX]; /* "#K7M-3QX" or "a.b.c.d:port"; empty is a free entry */
    int state;                     /* MP_PING_*, as last read                              */
    int ms;
    int started;                   /* the prober has it                                    */
    int done;                      /* final until the next clear                           */
    unsigned listed;               /* the refill that last listed it                       */
} DmsPing;

static DmsPing s_ping[DMS_PING_KEEP];
/* The direct rows selected on this visit, by row key: the only direct rows ever asked. */
static char s_ping_chosen[DMS_PING_KEEP][MP_KEY_MAX];
static int s_ping_chosen_next = 0;
/* The selection this visit opened with, until the player makes one of their own. */
static char s_ping_carried[MP_KEY_MAX];
static unsigned s_ping_refill = 0;
/* A SELECTION THE ARROW KEYS ARE ONLY PASSING THROUGH IS NOT A CHOICE. A press on a row
   chooses it there and then. A key chooses the row the selection comes to REST on: it has
   to stay on one row for DMS_PING_DWELL_MS, and every move starts the wait again, so
   walking down the list to a game further on sends nothing to the direct games walked
   over. s_ping_sel is the selection being timed, since s_ping_sel_ms. */
#define DMS_PING_DWELL_MS 400
static char s_ping_sel[MP_KEY_MAX];
static unsigned s_ping_sel_ms = 0;
static int s_ping_sel_pressed = 0;

/* STOP ASKING AND FORGET EVERY ANSWER: on REFRESH, on leaving the INTERNET list and on
   leaving the screen. The prober's sockets stay open, unread and silent, for the
   registration reason above. */
static void dms_ping_clear(void)
{
    nm_probe_clear_all();
    memset(s_ping, 0, sizeof s_ping);
    memset(s_ping_chosen, 0, sizeof s_ping_chosen);
    s_ping_chosen_next = 0;
}

/* What the prober is handed for a row: the room code of a relayed row, "address:port" of
   a direct one. 0 when the row carries nothing the prober could take. */
static int dms_ping_target(const MP_Row *r, char *out, int cap)
{
    const char *c;
    int n;
    if (r->relay) {
        /* ONE ROOM, ONE TARGET, HOWEVER ITS CODE IS SPELLED. A code is read loosely (either
           case, I and L for 1, O for 0), so one room can stand on a list under several
           spellings; each is handed over as the spelling the room itself prints, and the
           room is asked once. A code that does not read as one is not asked at all. */
        unsigned long id;
        char canon[RC_TEXT_MAX];
        if (!r->room[0] || !rc_decode(r->room, &id) || !rc_encode(id, canon)) return 0;
        n = snprintf(out, (size_t)cap, "%s", canon);
    } else {
        if (!r->addr[0] || r->port == 0) return 0;
        n = snprintf(out, (size_t)cap, "%s:%u", r->addr, (unsigned)r->port);
    }
    if (n <= 0 || n >= cap) return 0;
    /* The prober keeps no record of a key with a byte it could not log, so such a key
       would be offered again on every refill. */
    for (c = out; *c; c++)
        if ((unsigned char)*c < 33 || (unsigned char)*c > 126) return 0;
    return 1;
}

/* WHOSE SELECTION THIS IS, and whether it has come to rest: called once a frame, after the
   presses of that frame have been acted on. `row_pressed` is 1 when a press landed on a
   row this frame. */
static void dms_ping_track(const MP_State *st, int row_pressed)
{
    if (s_ping_carried[0] && strcmp(st->selkey, s_ping_carried) != 0)
        s_ping_carried[0] = '\0';
    if (strcmp(st->selkey, s_ping_sel) != 0) {
        snprintf(s_ping_sel, sizeof s_ping_sel, "%s", st->selkey);
        s_ping_sel_ms = SDL_GetTicks();
        s_ping_sel_pressed = 0;
    }
    if (row_pressed && st->selkey[0]) s_ping_sel_pressed = 1;
}

/* Has the player chosen this direct row on this visit? Becomes true the first time the
   selection is on it by a press, or has rested on it after a key, and stays true until
   the next clear. */
static int dms_ping_chosen(const MP_State *st, const MP_Row *r)
{
    int i;
    for (i = 0; i < DMS_PING_KEEP; i++)
        if (s_ping_chosen[i][0] && !strcmp(s_ping_chosen[i], r->key)) return 1;
    if (!st->selkey[0] || strcmp(st->selkey, r->key) != 0) return 0;
    if (s_ping_carried[0] && !strcmp(s_ping_carried, st->selkey)) return 0;
    if (strcmp(s_ping_sel, st->selkey) != 0) return 0;          /* not timed yet */
    if (!s_ping_sel_pressed && SDL_GetTicks() - s_ping_sel_ms < DMS_PING_DWELL_MS) return 0;
    snprintf(s_ping_chosen[s_ping_chosen_next], MP_KEY_MAX, "%s", r->key);
    s_ping_chosen_next = (s_ping_chosen_next + 1) % DMS_PING_KEEP;
    return 1;
}

/* The entry for a target, made if there is none. When every entry is taken, the one
   listed longest ago goes, and never one this refill has listed. */
static DmsPing *dms_ping_entry(const char *target)
{
    DmsPing *spare = NULL;
    int i;
    for (i = 0; i < DMS_PING_KEEP; i++) {
        DmsPing *p = &s_ping[i];
        if (p->target[0] && !strcmp(p->target, target)) return p;
        if (!p->target[0]) {
            if (!spare || spare->target[0]) spare = p;
        } else if (p->listed != s_ping_refill
                   && (!spare || (spare->target[0] && p->listed < spare->listed))) {
            spare = p;
        }
    }
    if (!spare) return NULL;
    if (spare->target[0] && spare->started && !spare->done) nm_probe_cancel(spare->target);
    memset(spare, 0, sizeof *spare);
    snprintf(spare->target, sizeof spare->target, "%s", target);
    return spare;
}

/* ONE ROW'S CELL, filled while the list is refilled and before it is sorted, so a PING
   sort sees the values it draws. Starts the probe the rules above allow, and no other. */
static void dms_ping_fill(const MP_State *st, MP_Row *r)
{
    char target[NM_PROBE_KEY_MAX];
    NmProbeResult res;
    DmsPing *p;
    r->ping_state = MP_PING_UNKNOWN;
    r->ping_ms = 0;
    if (!r->joinable || r->lan) return;
    if (!dms_ping_target(r, target, (int)sizeof target)) return;
    if (!r->relay && !dms_ping_chosen(st, r)) return;
    p = dms_ping_entry(target);
    if (!p) return;
    p->listed = s_ping_refill;
    if (!p->started) {
        if (nm_probe_start(target)) {
            p->started = 1;
        } else if (nm_probe_poll(target, NULL) == NM_PROBE_INVALID) {
            /* Not a room code or a numeric address: nothing to measure, and not asked
               again. Any other refusal means every slot is busy, and the next refill
               offers it again. */
            p->started = 1;
            p->done = 1;
        }
    }
    if (p->started && !p->done) {
        switch (nm_probe_poll(target, &res)) {
        case NM_PROBE_PROBING:  p->state = MP_PING_PROBING; break;
        case NM_PROBE_ANSWERED: p->state = MP_PING_MS; p->ms = res.rtt_ms; p->done = 1; break;
        case NM_PROBE_SILENT:   p->state = MP_PING_LOST; p->done = 1; break;
        /* Forgotten by the prober: blank, rather than a guess, until the next clear. */
        default:                p->state = MP_PING_UNKNOWN; p->done = 1; break;
        }
    }
    r->ping_state = p->state;
    r->ping_ms = p->ms;
}

/* THE SAME ROWS FROM THE OTHER SIDE OF A ROUTER. The LAN half listens for beacons; this
   half asks a service, because a broadcast does not leave the building. The judgement
   about what is joinable is the same judgement, made in the same place and for the same
   reason, so the two lists behave alike once they are on the screen. */
static void dms_mp_refill_net(MP_State *st, unsigned abi)
{
    MB_Row got[MB_MAX_ROWS];
    int i, n;

    if (mb_phase() == MB_FAILED) {
        /* The reason is already a sentence this build owns and mp_check_layout measures. */
        snprintf(st->status, sizeof st->status, "%s", mb_error());
        /* The selection's KEY survives a failed fetch: the game is re-selected if the
           next one brings it back. */
        st->rowcount = 0;
        st->rowhidden = 0;
        st->rowsel = -1;
        st->rowtop = 0;
        return;
    }
    n = mb_rows(got, MB_MAX_ROWS);
    if (n > MP_MAX_ROWS) n = MP_MAX_ROWS;
    st->rowcount = n;
    st->rowhidden = 0;
    s_ping_refill++;
    for (i = 0; i < n; i++) {
        const MB_Row *g = &got[i];
        MP_Row *r = &st->rows[i];
        memset(r, 0, sizeof *r);
        snprintf(r->name, sizeof r->name, "%s", g->name);
        snprintf(r->map, sizeof r->map, "%s", g->map[0] ? g->map : g->scenario);
        snprintf(r->addr, sizeof r->addr, "%s", g->addr);
        snprintf(r->room, sizeof r->room, "%s", g->room);
        r->relay = g->relay;
        r->port = g->port;
        r->players_now = g->players_now;
        r->players_max = g->players_max;
        r->locked = g->locked;
        r->why = MP_WHY_OK;
        if (r->players_now >= r->players_max) r->why = MP_WHY_FULL;
        if (abi != 0 && g->abi != 0 && g->abi != abi) r->why = MP_WHY_BUILD;
        r->joinable = (r->why == MP_WHY_OK);
        /* KEYED BY THE LIST'S OWN ID, which the host drew for this game: two direct hosts
           behind one router share an address and a port, and a room code is only what a
           row without an id falls back to. */
        mp_row_key(r, g->id);
        /* Last, because it reads the key and the judgement above. */
        dms_ping_fill(st, r);
    }
    mp_view(st, MP_VIEW_REFILL);
}

/* Copy the lobby's live seat state onto the screen so the waiting room draws what the
   network actually says, rather than what this screen last hoped. */
static void dms_mp_sync_wait(MP_State *st)
{
    int i;
    st->wait_ishost = nm_lobby_is_host();
    st->wait_humans = nm_lobby_wanted();
    st->wait_myseat = nm_lobby_seat();
    for (i = 0; i < MP_MAX_SEATS; i++) {
        st->wait_taken[i] = nm_lobby_seat_taken(i);
        st->wait_ready[i] = nm_lobby_seat_ready(i);
    }
}

/* See the header: 0 means a person is driving. */
static unsigned  s_mp_autoleave_ms = 0;
static DmsGrabFn s_mp_grab = 0;
static void*     s_mp_grab_user = 0;
void dms_mp_autoleave(unsigned ms, DmsGrabFn grab, void* user)
{
    s_mp_autoleave_ms = ms;
    s_mp_grab = grab;
    s_mp_grab_user = user;
}

/* See the header. NULL in anything a person plays. */
static const DMS_MpStep *s_mp_script = 0;
static int s_mp_steps = 0;
static int s_mp_ran = 0;

void dms_mp_script(const DMS_MpStep *steps, int count)
{
    s_mp_script = (count > 0) ? steps : 0;
    s_mp_steps = (count > 0) ? count : 0;
}

int dms_mp_script_ran(void) { return s_mp_ran; }

/* See the header. NULL in anything a person plays. */
static DmsMpSnapFn s_mp_snap = 0;
static void *s_mp_snap_user = 0;

void dms_mp_snap(DmsMpSnapFn fn, void *user)
{
    s_mp_snap = fn;
    s_mp_snap_user = user;
}

/* THE REMEMBERED SETTINGS. The path is empty unless dms_mp_remember set it, and empty
   means no read and no write: the program turns this on for a player's launch and never
   for a harness, so no gate can inherit a sort order from a file left in the run folder.
   s_mp_prefs_disk is what the file holds now, so a visit that changes nothing writes
   nothing. */
static char s_mp_prefs_path[512] = "";
static MP_Prefs s_mp_prefs_disk;

void dms_mp_remember(const char *path, struct MP_State *stv)
{
    MP_State *st = (MP_State *)stv;
    MP_Prefs p;
    if (!path || !*path || !st) { s_mp_prefs_path[0] = '\0'; return; }
    snprintf(s_mp_prefs_path, sizeof s_mp_prefs_path, "%s", path);
    mp_prefs_get(st, &p);
    if (mp_prefs_read(s_mp_prefs_path, &p))
        mp_prefs_put(st, &p);
    mp_prefs_get(st, &s_mp_prefs_disk);
}

/* WRITE ONLY WHAT CHANGED. The name is the one value held back while the YOUR NAME box is
   still being typed into, because a name half typed is not a name and a file per
   keystroke is not a settings file; the sort and the HIDE boxes are written whatever box
   is open, so no way of leaving the screen can lose them. */
static void dms_mp_prefs_sync(const MP_State *st)
{
    MP_Prefs now;
    if (!s_mp_prefs_path[0]) return;
    mp_prefs_to_save(st, &s_mp_prefs_disk, &now);
    if (mp_prefs_equal(&now, &s_mp_prefs_disk)) return;
    if (!mp_prefs_write(s_mp_prefs_path, &now))
        fprintf(stderr, "multiplayer: cannot write %s, so the list's order, its filters "
                        "and the player's name will not be remembered\n", s_mp_prefs_path);
    /* Taken as written either way, for the reason the controls file gives: a folder that
       cannot be written to will not become writable, and a line every frame buries the
       log. */
    s_mp_prefs_disk = now;
}

/* WHERE THE NTH ROW IS, asked of mp_row_at rather than computed beside it. The list's
   geometry is private to the screen and should stay that way; what a harness needs is one
   pixel inside a given row, and the honest way to find it is to ask the same function the
   live click asks. Scanning is free at this size and it cannot drift: a row this returns
   nothing for is a row a hand could not click either. Returns 0 when the row is not on
   screen, which for a browser is an ordinary answer rather than a fault. */
static int dms_mp_row_point(const MP_State *st, int row, int *mx, int *my)
{
    int x, y;
    for (y = 0; y < DM_SCREEN_H; y++) {
        for (x = 0; x < DM_SCREEN_W; x++) {
            if (mp_row_at(st, x, y) == row) {
                *mx = x;
                *my = y;
                return 1;
            }
        }
    }
    return 0;
}

/* One script step: a typed character, a key, or a press/release pair on the item's own
   rectangle. Every one of them prints, because a step that quietly did nothing is how a
   script comes to claim more than it drove. */
static void dms_mp_step(DMS *s, MP_State *st, const DMS_MpStep *step)
{
    SDL_Event e;
    int rx = 0, ry = 0, rw = 0, rh = 0, mx = 0, my = 0, wx = 0, wy = 0;

    if (step->ch) {
        /* A TYPED CHARACTER IS HANDED STRAIGHT TO THE FIELD, and this is the one step
           that does NOT travel through the event queue. A synthetic SDL_TEXTINPUT cannot:
           the SDL2 this build loads on macOS is the compatibility shim over SDL3, where
           the text event carries a POINTER to its characters rather than an array of
           them, and pushing an SDL2-shaped one segfaults inside the conversion. Presses
           and keys go through the queue unharmed, which is why the skirmish lobby's
           driver never met this.

           WHAT IS STILL BEING TESTED, and what is not. The route from the event to the
           field is one line with one condition, and that condition is repeated here, so
           the thing worth proving -- that clicking a field gave it the caret and that a
           character lands in THAT field and no other -- is proved exactly as before. What
           is no longer covered is SDL's own delivery, which is not ours. */
        if (st->focus >= 0)
            mp_text(st, (char)step->ch);
        printf("MPCLICK|type|%c|focus=%d\n", (char)step->ch, st->focus);
        fflush(stdout);
        return;
    }
    if (step->item >= DMS_MP_SNAP_BASE) {
        /* A LOOK, NOT A PRESS: the harness is handed the state as it stands between two
           steps, so a script can assert what a press did before the next one changes it. */
        const int n = step->item - DMS_MP_SNAP_BASE;
        printf("MPCLICK|snap|%d|rows=%d|hidden=%d|sel=%d|top=%d\n", n, st->rowcount,
               st->rowhidden, st->rowsel, st->rowtop);
        fflush(stdout);
        if (s_mp_snap) s_mp_snap(st, n, s_mp_snap_user);
        return;
    }
    if (step->key) {
        memset(&e, 0, sizeof e);
        e.type = SDL_KEYDOWN;
        e.key.state = SDL_PRESSED;
        e.key.keysym.sym = (SDL_Keycode)step->key;
        SDL_PushEvent(&e);
        printf("MPCLICK|key|%s\n", SDL_GetKeyName((SDL_Keycode)step->key));
        fflush(stdout);
        return;
    }
    if (step->item >= DMS_MP_ROW_BASE) {
        const int row = step->item - DMS_MP_ROW_BASE;
        if (!dms_mp_row_point(st, row, &mx, &my)) {
            printf("MPCLICK|skip|row %d|not on screen\n", row);
            fflush(stdout);
            return;
        }
        printf("MPCLICK|click|row %d|menu %d,%d\n", row, mx, my);
    } else {
        if (!mp_item_rect(st, step->item, &rx, &ry, &rw, &rh)) {
            /* SAID, NOT SWALLOWED, exactly as the lobby's driver says it: a step whose
               control is not on this page did nothing, and a script that did less than it
               claimed is the one failure a harness must never hide. */
            printf("MPCLICK|skip|item %d|no rectangle\n", step->item);
            fflush(stdout);
            return;
        }
        mx = rx + (rw - 1) * step->fx / 1000;
        my = ry + (rh - 1) * step->fy / 1000;
        printf("MPCLICK|click|item %d|menu %d,%d\n", step->item, mx, my);
    }
    fflush(stdout);
    dms_menu_to_window(s, mx, my, &wx, &wy);
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

int dms_multiplayer(DMS *s, struct MP_State *stv, const struct NmSetup *setup,
                    unsigned abi, unsigned scen)
{
    MP_State *st = (MP_State *)stv;
    int result = DMS_CANCEL;
    unsigned last_poll = 0, last_blink = 0, last_list = 0;
    const unsigned t_enter = SDL_GetTicks();
    unsigned frames = 0;
    unsigned last_step = SDL_GetTicks();
    int stepi = 0;
    int first_list = 1;
    int was_pinging = 0;

    s_mp_ran = 0;
    SDL_ShowCursor(SDL_DISABLE);
    SDL_StartTextInput();          /* the two typed fields; stopped on every exit below */
    dms_layout(s);
    /* THE PING COLUMN STARTS EMPTY, and a direct row still selected from last time is not
       asked about until the player selects something on this visit. */
    dms_ping_clear();
    snprintf(s_ping_carried, sizeof s_ping_carried, "%s", st->selkey);
    snprintf(s_ping_sel, sizeof s_ping_sel, "%s", st->selkey);
    s_ping_sel_ms = SDL_GetTicks();
    s_ping_sel_pressed = 0;
    /* NOTHING HELD OVER FROM A VISIT THAT ENDED MID-GESTURE: a thumb whose button came up
       somewhere else, or a YOUR NAME box nobody answered. */
    st->drag = 0;
    st->pressed = -1;
    mp_prompt_close(st);

    /* The browser listens for the whole visit, not only while the JOIN tab is up: a
       player who opens HOST, thinks better of it and switches to JOIN should see the
       games that were announced while they were deciding, not an empty list and a wait. */
    if (!nb_browse_open()) {
        snprintf(st->status, sizeof st->status,
                 "COULD NOT LISTEN FOR GAMES ON THIS NETWORK.");
    }

    for (;;) {
        SDL_Event e;
        int dirty = 0, act = MP_ACT_NONE, row_pressed = 0;
        const unsigned now = SDL_GetTicks();

        while (SDL_PollEvent(&e)) {
            if (fs_handle_event(&e)) { dirty = 1; continue; }
            switch (e.type) {
            case SDL_QUIT:
                result = DMS_QUIT;
                goto done;
            case SDL_TEXTINPUT:
                if (st->focus >= 0) {
                    mp_text(st, e.text.text[0]);
                    dirty = 1;
                }
                break;
            case SDL_KEYDOWN: {
                const int k = dms_mp_key(e.key.keysym.sym);
                if (k) { act = mp_key(st, k); dirty = 1; }
                break;
            }
            case SDL_MOUSEMOTION:
                dms_to_menu(s, e.motion.x, e.motion.y, &s->mx, &s->my);
                mp_motion(st, s->mx, s->my);
                dirty = 1;
                break;
            case SDL_MOUSEWHEEL:
                mp_scroll(st, -e.wheel.y);
                dirty = 1;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int cx, cy;
                    dms_to_menu(s, e.button.x, e.button.y, &cx, &cy);
                    s->mx = cx; s->my = cy;
                    /* A press on a row is a selection made now, even of the row that was
                       already highlighted. */
                    if (mp_row_at(st, cx, cy) >= 0) {
                        s_ping_carried[0] = '\0';
                        row_pressed = 1;
                    }
                    act = mp_press(st, cx, cy);
                    /* A double click on a row joins it, the shortcut every list in this
                       program gives and the one a browser most obviously wants. */
                    if (e.button.clicks >= 2 && mp_row_at(st, cx, cy) >= 0
                        && !mp_item_disabled(st, MP_I_JOIN)) {
                        act = MP_ACT_JOIN;
                    }
                    dirty = 1;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) { mp_release(st); dirty = 1; }
                break;
            default:
                break;
            }
        }

        /* ---- what the press asked for ---- */
        switch (act) {
        case MP_ACT_CANCEL:
            result = DMS_CANCEL;
            goto done;
        case MP_ACT_REFRESH:
            nb_browse_clear();
            st->rowcount = 0;
            st->rowhidden = 0;
            st->rowsel = -1;
            st->selkey[0] = '\0';
            st->status[0] = '\0';
            /* Every relayed room is asked again as the list comes back, and a direct one
               only once it is selected again. */
            dms_ping_clear();
            dirty = 1;
            break;
        case MP_ACT_HOST:
            nm_set_player_name(st->handle);
            if (!setup) {
                snprintf(st->status, sizeof st->status,
                         "SET THE MATCH UP FIRST: PICK A MAP AND THE RULES.");
            } else {
                /* A RELAYED ROOM IS ARMED BEFORE THE DOOR IS OPENED, because the arming
                   is what decides which kind of socket nm_open makes. The host draws an
                   id small enough to become a six-character room code; a joiner draws
                   from the full width, because a joiner's id is never written down. */
                if (st->relay_game) {
                    const unsigned long id = rc_draw_host_id();
                    if (id == 0ul) {
                        snprintf(st->status, sizeof st->status, "%s",
                                 "THIS COMPUTER WOULD NOT GIVE A RANDOM NUMBER.");
                        dirty = 1;
                        break;
                    }
                    nm_relay_next(NM_RELAY_HOST, NM_RELAY_PORT, id, 0ul);
                }
                if (!nm_lobby_host(NM_PORT_DEFAULT, setup, abi, scen, setup->seats,
                                   st->name, st->private_game ? st->pass : NULL)) {
                    snprintf(st->status, sizeof st->status, "%s", nm_lobby_error());
                    dirty = 1;
                    break;
                }
                /* WHAT TO READ OUT, and it is a different sentence for each kind of room.
                   Through a relay it is the room code and there is no port to forward,
                   because both ends are talking outbound. On this network it is an
                   address: net_local_addrs is best effort by design, so an empty answer
                   is left empty and the screen says it could not tell rather than
                   printing something that might send a friend to the wrong machine. */
                if (nm_is_relayed() && nm_room_code()[0]) {
                    snprintf(st->myaddr, sizeof st->myaddr, "%s", nm_room_code());
                } else {
                    char addrs[4][64];
                    const int na = net_local_addrs(addrs, 4);
                    if (na > 0) {
                        snprintf(st->myaddr, sizeof st->myaddr, "%s:%u",
                                 addrs[0], (unsigned)NM_PORT_DEFAULT);
                    } else {
                        st->myaddr[0] = '\0';
                    }
                }
                /* THE LAN ADVERT IS FOR THE LAN. A relayed room's match socket is an
                   ephemeral port on a tunnel, so a browser row pointing at
                   NM_PORT_DEFAULT on this machine would send a neighbour to knock on a
                   door that is not there. */
                if (!nm_is_relayed()) nb_announce_open();
                /* AND WHETHER THIS ROOM GOES ON THE PUBLIC LIST BY ADDRESS, decided here
                   once, where both the tick and the socket are in view, rather than asked
                   again from a screen that cannot see either. */
                s_list_direct_port = (!nm_is_relayed() && st->list_game)
                                       ? NM_PORT_DEFAULT : 0;
                st->status[0] = '\0';
                result = DMS_MP_LOBBY;
                goto done;
            }
            dirty = 1;
            break;
        case MP_ACT_JOIN:
            nm_set_player_name(st->handle);
            /* A TYPED ADDRESS ONLY WINS WHEN NOTHING IS SELECTED, and it did not used to
               have to say so: the field and the list lived on different sub-tabs, only
               one could be on screen, and testing the field first could not steal a
               click. The internet sub-tab now carries BOTH, so the old rule would make
               every row in it unjoinable the moment a code was left in the field -- and
               silently, because the join would go ahead down the other path and fail
               somewhere else entirely. Whatever the player last pointed at is what they
               meant. */
            if (st->net == MP_NET_INTERNET && st->addr[0]
                && !(st->rowsel >= 0 && st->rowsel < st->rowcount)) {
                char host[MP_ADDR_MAX];
                unsigned short port = NM_PORT_DEFAULT;
                unsigned long room = 0ul;
                const int kind = dms_parse_target(st->addr, host, sizeof host,
                                                  &port, &room);
                if (kind == DMS_ADDR_ROOM) {
                    /* A ROOM CODE IS NOT A PLACE. It names the host's id on the relay, so
                       there is no address to resolve and no port for anyone to forward.
                       This peer draws its OWN id and never tells anybody: the host learns
                       it from the first packet, exactly as it learns an IP on a LAN. */
                    nm_relay_next(NM_RELAY_HOST, NM_RELAY_PORT, rc_draw_peer_id(), room);
                }
                if (kind == DMS_ADDR_NONE) {
                    snprintf(st->status, sizeof st->status, "%s",
                             "NOT AN ADDRESS OR A ROOM CODE.");
                } else if (!nm_lobby_join(kind == DMS_ADDR_ROOM ? NULL : host, port, abi, 0u,
                                          st->pass[0] ? st->pass : NULL)) {
                    snprintf(st->status, sizeof st->status, "%s", nm_lobby_error());
                } else {
                    /* The room has no name until the WELCOME brings one, so show where
                       we are knocking rather than the last room's name. */
                    snprintf(st->name, sizeof st->name, "%s", st->addr);
                    st->status[0] = '\0';
                    result = DMS_MP_LOBBY;
                    goto done;
                }
            } else if (st->rowsel >= 0 && st->rowsel < st->rowcount) {
                const MP_Row *r = &st->rows[st->rowsel];
                /* THE PASSCODE THE JOINER TYPED ON THE HOST TAB IS REUSED. There is no
                   separate prompt yet, and saying so is better than a dialog that does
                   not exist: a locked row with no passcode typed is refused by the host
                   and the refusal is printed in the status line, by name. */
                /* THE MAP HASH IS NOT SENT ON THIS PATH. What `scen` holds is the hash of
                   THIS machine's first skirmish map, and what the host would compare it
                   with is the hash of ITS first map at the moment it opened the room,
                   which CHANGE MAP never refreshes: two installs whose first map differs
                   (a user map sorting first, say) were refused for a map nobody was
                   about to play. Zero skips the check on both ends. Hashing the map the
                   WELCOME actually names is the right check and is a recorded gap; the
                   command-line path (--host/--join) keeps its own check unchanged. */
                if (r->locked && !st->pass[0]) {
                    /* REFUSED HERE, BY NAME, rather than sent to be refused there:
                       the host's answer is the same word but arrives after a trip
                       through the room and reads as the join having failed. */
                    snprintf(st->status, sizeof st->status, "%s",
                             "THIS GAME IS LOCKED: TYPE THE HOST'S PASSCODE.");
                    /* THE ZERO MAP FINGERPRINT IS DELIBERATE NOW, and was not before.
                       A joiner clicking a row knows an address; it does not yet know
                       which map that room is on, so there is nothing honest to put here
                       -- and what used to be put here was the hash of whatever map this
                       screen happened to be showing, compared against the host's as
                       though the two were about the same thing. The check moved to where
                       it is answerable: netmatch asks the installed hasher the moment the
                       WELCOME names the host's map, and refuses in words if this machine
                       has not got it or has a different copy. See nm_set_map_hasher. */
                } else if (r->relay && !dms_arm_row_relay(r, st)) {
                    /* dms_arm_row_relay has already said why in the status line. */
                } else if (!nm_lobby_join(r->relay ? NULL : r->addr, r->port, abi, 0u,
                                   st->pass[0] ? st->pass : NULL)) {
                    snprintf(st->status, sizeof st->status, "%s", nm_lobby_error());
                } else {
                    snprintf(st->name, sizeof st->name, "%s", r->name);
                    st->status[0] = '\0';
                    result = DMS_MP_LOBBY;
                    goto done;
                }
            }
            dirty = 1;
            break;
        case MP_ACT_READY:
            nm_lobby_set_ready(!nm_lobby_my_ready());
            dms_mp_sync_wait(st);
            dirty = 1;
            break;
        case MP_ACT_START:
            /* THE SAME DOOR AS THE LOBBY SCREEN'S, and so the same wait: the press may be
               held while the room's round trips come in, and the poll below says which. */
            if (nm_lobby_start()) {
                snprintf(st->status, sizeof st->status, "%s", nm_lobby_start_pending()
                         ? MP_SAY_MEASURING : "STARTING...");
            }
            dirty = 1;
            break;
        case MP_ACT_LEAVE:
            nm_lobby_cancel();
            nb_announce_close();
            mb_unpublish();
            s_list_direct_port = 0;
            st->page = MP_PAGE_PICK;
            st->status[0] = '\0';
            dirty = 1;
            break;
        default:
            break;
        }

        /* ---- the PING column: whose selection this is, and the prober's frame ---- */
        dms_ping_track(st, row_pressed);
        {
            /* The same three conditions the internet list is refilled under, so the prober
               runs exactly while that list is on screen, and is cleared the frame it is not. */
            const int pinging = st->page == MP_PAGE_PICK && st->tab == MP_TAB_JOIN
                                && st->net == MP_NET_INTERNET;
            if (pinging) nm_probe_pump();
            else if (was_pinging) dms_ping_clear();
            was_pinging = pinging;
        }

        /* ---- the network, ten times a second ---- */
        if (now - last_poll > 100) {
            last_poll = now;
            if (st->page == MP_PAGE_PICK) {
                if (st->net == MP_NET_LAN) dms_mp_refill(st, abi);
                /* THE TAB IS ASKED FOR AS WELL AS THE SUB-TAB, and that is not belt and
                   braces. `net` is a JOIN-tab control and it KEEPS its value after the
                   player switches back to HOST, so testing it alone would leave the game
                   asking a service across the internet every few seconds while somebody
                   filled in a host form. The LAN half survives that mistake because its
                   poll is a local socket read; a round trip to a server does not. */
                else if (st->tab == MP_TAB_JOIN && st->net == MP_NET_INTERNET) {
                    /* THE FIRST ASK IS NOT ON THE CLOCK. `last_list` starts at zero and
                       the tick count is milliseconds since the program started, so this
                       test used to read "has this BUILD been running four seconds",
                       not "is this list stale": a player who reached the INTERNET tab in
                       the first four seconds sat in front of an empty list waiting for a
                       timer that had nothing to do with them. */
                    if (first_list || now - last_list > MP_LIST_EVERY_MS) {
                        first_list = 0;
                        last_list = now;
                        mb_refresh();
                    }
                    dms_mp_refill_net(st, abi);
                }
            } else {   /* unreachable: the lobby screen owns the room now */
                const int ls = nm_lobby_poll();
                dms_mp_sync_wait(st);
                if (nm_lobby_is_host()) {
                    /* KEEP ANNOUNCING WHILE THERE IS ROOM, and stop the moment there is
                       not: a full game still listed is a row every browser offers and
                       every joiner is refused by. A pressed START that is still being
                       measured is not room either, nor is one that has gone out: nobody is
                       seated from the press until the match begins. */
                    const int filled = nm_lobby_filled();
                    if (filled < nm_lobby_wanted()
                        && !nm_lobby_start_pending() && !nm_lobby_starting()) {
                        nb_announce(st->name, setup ? setup->scenario : st->mapname, NULL,
                                    NM_PORT_DEFAULT, filled, nm_lobby_wanted(),
                                    st->private_game && st->pass[0], abi, scen);
                    }
                }
                if (ls == NM_LOBBY_STARTED) {
                    /* A GAME THAT HAS STARTED IS NOT AN OPEN GAME; see the lobby screen. */
                    nb_announce_close();
                    mb_unpublish();
                    result = DMS_MP_PLAY;
                    goto done;
                }
                if (ls == NM_LOBBY_REFUSED || ls == NM_LOBBY_FAILED) {
                    snprintf(st->status, sizeof st->status, "%s", nm_lobby_error());
                    nm_lobby_cancel();
                    nb_announce_close();
                    mb_unpublish();
                    s_list_direct_port = 0;
                    st->page = MP_PAGE_PICK;
                }
                /* THE WAIT FOR ROUND TRIPS, on both edges: the line is written when the
                   wait begins and wiped when it ends, whether in the START going out or in
                   the room changing under it, so it never outlives what it describes. */
                if (nm_lobby_is_host() && nm_lobby_start_pending()) {
                    snprintf(st->status, sizeof st->status, "%s", MP_SAY_MEASURING);
                } else if (strcmp(st->status, MP_SAY_MEASURING) == 0) {
                    st->status[0] = '\0';
                }
                /* THE GREYED START SAYS WHY. A control that refuses without a reason is
                   indistinguishable from one that is broken, which is the complaint every
                   disabled button in this program is written to avoid. */
                if (nm_lobby_is_host() && st->status[0] == '\0') {
                    if (nm_lobby_filled() < nm_lobby_wanted()) {
                        snprintf(st->status, sizeof st->status,
                                 "WAITING FOR %d MORE PLAYER%s TO JOIN.",
                                 nm_lobby_wanted() - nm_lobby_filled(),
                                 (nm_lobby_wanted() - nm_lobby_filled()) == 1 ? "" : "S");
                    } else if (!nm_lobby_all_ready()) {
                        snprintf(st->status, sizeof st->status,
                                 "WAITING FOR EVERYONE TO PRESS READY.");
                    } else {
                        snprintf(st->status, sizeof st->status, "EVERYONE IS READY.");
                    }
                }
            }
            dirty = 1;
        }

        if (now - last_blink > 400) { last_blink = now; st->caret_on = !st->caret_on; dirty = 1; }

        /* THE GATE'S OWN DOOR OUT, and its heartbeat. The count is what makes a freeze
           legible: a screen that is alive prints a rising number of frames and then
           leaves, and one that has stopped prints nothing more, so the log says WHERE it
           stopped instead of only that the run never ended. */
        frames++;

        /* THE SCRIPT, ONE STEP PER PASS AND NEVER FASTER THAN THE STEP ASKED FOR. Placed
           after the poll on purpose: a step that waits for the list has to be judged
           against a list that has had a chance to arrive. */
        if (s_mp_script && stepi < s_mp_steps
            && now - last_step >= (unsigned)s_mp_script[stepi].wait) {
            dms_mp_step(s, st, &s_mp_script[stepi++]);
            s_mp_ran = stepi;
            last_step = now;
            dirty = 1;
        }

        dms_mp_prefs_sync(st);

        if (s_mp_autoleave_ms && now - t_enter >= s_mp_autoleave_ms) {
            /* THE LEAVE FRAME IS DRAWN IN FULL AND THEN READ BACK, off the back buffer,
               before the swap: that is the window's next picture, and the only place the
               question "is the screen showing what was drawn" can be answered. */
            db_fill_rect(&s->surf, 0, 0, s->surf.w - 1, s->surf.h - 1, 0);
            dm_draw_plate(&s->surf, s->pack);
            mp_draw(&s->surf, s->pack, st);
            dm_draw_cursor(&s->surf, s->pack, s->mx, s->my);
            db_surface_to_rgba(&s->surf, s->pack->pal8, s->rgba, 0);
            dms_upload(s, s->rgba);
            dms_draw(s);
            if (s_mp_grab) s_mp_grab(s_mp_grab_user);
            SDL_GL_SwapWindow(s->win);
            printf("MPPLAY|left|after=%ums|frames=%u|rows=%d|status=%s\n",
                   now - t_enter, frames, st->rowcount, st->status);
            fflush(stdout);
            result = DMS_CANCEL;
            goto done;
        }

        /* THE MUSIC IS FED HERE TOO, and its absence was the whole of the fault where
           the menu score stopped as soon as this screen opened. Every other screen loop
           in this file pumps once per pass; this one drew, polled the network and slept,
           and never did. Two things live in that pump: the decode that tops up the music
           ring, which runs on this thread, and the check that starts the next track when
           one ends. So the ring ran dry a few seconds in and the score stopped.

           THE SILENCE IS CONFINED TO THIS SCREEN, and a first account of this bug had it
           lasting the rest of the session, which reading the rest of the menu does not
           support. Nothing here closes the music stream, so leaving resumes the track
           where it stopped. Nor is a missing second MUSIC|play line evidence of anything:
           dms_run restarts the theme only when the current one differs from the menu's,
           which a trip through here never changes, so that line is absent whether the
           music worked or not. Neither is a device's frame count, which counts silence
           as readily as sound.

           WHAT DOES MEASURE IT is the recording tap, which renders on this thread and so
           stops exactly when this loop stops feeding it. Fifteen seconds on this screen:
           without the pump 0.00 s and silence, with it 14.38 s at a normal level.

           OUTSIDE THE REDRAW, deliberately. Audio has to be fed on every pass, and this
           screen only redraws when something changed: a player reading the room list
           and touching nothing is exactly the case that must not fall silent. */
        dms_pump_audio(s, DM_MP_FRAME_MS);

        if (dirty) {
            db_fill_rect(&s->surf, 0, 0, s->surf.w - 1, s->surf.h - 1, 0);
            dm_draw_plate(&s->surf, s->pack);
            mp_draw(&s->surf, s->pack, st);
            dm_draw_cursor(&s->surf, s->pack, s->mx, s->my);
            /* THE TWO LINES THAT WERE MISSING, and the whole of BUG "MULTIPLAYER freezes
               the menu". Drawing goes into s->surf, an 8-bit surface; what the window
               shows is a GL texture that is only ever written by dms_upload. Every other
               screen reaches that through dms_redraw. This one drew its picture and then
               called dms_present, which draws the TEXTURE and swaps -- so the window kept
               the last thing uploaded, the main menu, with the hardware cursor hidden and
               the software one drawn into a surface nobody looked at. A screen that is
               alive, polling the network ten times a second, and invisible. --mpshot never
               saw it because dms_write_shot reads the surface, not the screen. */
            db_surface_to_rgba(&s->surf, s->pack->pal8, s->rgba, 0);
            dms_upload(s, s->rgba);
            dms_present(s);
        }
        SDL_Delay(DM_MP_FRAME_MS);
    }

done:
    SDL_StopTextInput();
    /* A YOUR NAME BOX STILL OPEN IS SHUT UNANSWERED, and a held thumb is let go, whichever
       way the screen was left: neither belongs to the next visit. Before the write below,
       so the name it discards is not the one written. */
    mp_prompt_close(st);
    st->drag = 0;
    st->pressed = -1;
    /* A header pressed on the same pass as the ESC that left is still a change. */
    dms_mp_prefs_sync(st);
    /* NO ROOM IS ASKED ANYTHING ONCE THE SCREEN HAS GONE, whether it went to a join, a room
       of this player's own, the match setup or the main menu. See THE PING COLUMN. */
    dms_ping_clear();
    /* The browser stops listening; a lobby that is still open does NOT, because
       DMS_MP_SETUP and DMS_MP_PLAY both mean "we are coming back or going to play". */
    nb_browse_close();
    /* AND THE PUBLIC ONE FORGETS WHATEVER IS STILL IN FLIGHT. Nothing waits for it: a
       reply that arrives after this screen has gone is dropped rather than written into
       rows nobody is looking at, which is also what stops the next visit opening on a
       list the player never asked for. */
    mb_forget();
    /* A room that is OPEN survives every exit but a cancel: DMS_MP_SETUP and DMS_MP_LOBBY
       both mean "we are coming back", and closing it would drop the people in it. */
    if (result == DMS_CANCEL || result == DMS_QUIT) {
        nm_lobby_cancel();
        nb_announce_close();
        mb_unpublish();
        s_list_direct_port = 0;
    }
    return result;
}
