/*
 * launcher.c -- C&C 3D's launcher. The thing a player double-clicks.
 *
 *   C&C3D                      open the launcher
 *   C&C3D --dir <folder>       point it at an install other than its own
 *   C&C3D --shot out.png       render one frame to a PNG and exit (no window)
 *   C&C3D --play               skip the launcher and start the game
 *   C&C3D -- --w 1600 --h 960  pass the rest to the game when Play is pressed
 *
 * WHAT IT IS. One 320x200 dialog drawn with the game's own DOS primitives, over
 * the 1995 title plate out of TITLE.CPS: the build that is installed, the build
 * that is available, that build's changelog in a scrolling panel, and three
 * buttons. Play starts the game and gets out of the way. When the host has a
 * newer build, Play becomes Update. Editor is drawn disabled, in the engine's own
 * BOXSTYLE_GREEN_DIS_RAISED, because there is an editor being built and there is
 * not yet one to launch.
 *
 * WHY IT IS NOT A NEW UI TOOLKIT. game/dosbar.c and menu/dosmenu.c already
 * rasterise every piece of chrome this needs, they need nothing but libc, and
 * they are the same code the shipping menu draws with. A launcher written on top
 * of them cannot drift away from the game's look, cross compiles with the
 * toolchain the game already uses, and adds no runtime the player has to install.
 *
 * TIER 1. Everything here is one 320x200 texture and one
 * textured quad in fixed-function OpenGL 1.1, which is what the Voodoo 2 target
 * can draw through Glide. There are no shaders and no render targets. The only
 * Tier 2 dependency is SDL2 itself, exactly as the game has.
 */

#include "lcfg.h"
#include "lpath.h"
#include "lui.h"
#include "lupdate.h"
#include "pngwrite.h"

#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ------------------------------------------------------------------------ *
 * Layout. Every number is in 320x200 menu pixels, the DOS mode this reproduces.
 *
 * The dialog is wider and taller than the main menu's 152x136 because it carries
 * a changelog rather than six buttons, but it is the same object: the same
 * BOXSTYLE_GREEN_BORDER frame, the same OPTIONS.SHP filigree pair inset from the
 * top corners by the same 12 and 14 pixels, the same 9-pixel button row.
 * ------------------------------------------------------------------------ */

#define L_DLG_X 10
#define L_DLG_Y 6
#define L_DLG_W 300
#define L_DLG_H 188

#define L_TITLE_Y 12   /* the wordmark, 8 point, centred            */
#define L_INFO_Y 30    /* installed / available, 6 point            */

#define L_PANEL_X 18
#define L_PANEL_Y 40
#define L_PANEL_W 284
#define L_PANEL_H 110
#define L_TEXT_X (L_PANEL_X + 4)
#define L_TEXT_Y (L_PANEL_Y + 4)
#define L_TEXT_W 264
#define L_TEXT_H 102
#define L_BAR_X (L_PANEL_X + L_PANEL_W - 10)
#define L_BAR_W 6

#define L_STATUS_Y 154
#define L_GAUGE_X 18
#define L_GAUGE_Y 164
#define L_GAUGE_W 284
#define L_GAUGE_H 7

#define L_BTN_Y 175
#define L_BTN_H 11
#define L_BTN_W 88

enum
{
    L_BTN_PLAY = 0,
    L_BTN_EDITOR,
    L_BTN_QUIT,
    L_BTN_COUNT
};

/* ------------------------------------------------------------------------ *
 * The program.
 * ------------------------------------------------------------------------ */

typedef struct
{
    SDL_Window *win;
    GLuint tex;
    int tw, th;          /* power-of-two texture holding the 320x200 */
    int scale, vpx, vpy; /* letterbox, recomputed every frame        */
    float px, py;        /* drawable pixels per window point         */

    DB_Pack *pack;
    DB_Surface surf;
    unsigned char screen[DM_SCREEN_W * DM_SCREEN_H];
    unsigned char *rgba, *padded;

    const DB_Font *f6, *f8, *grad;

    int mx, my;      /* pointer, in menu pixels          */
    int pressed;     /* button index held down, or -1    */
    int hot;         /* keyboard highlight               */
    int frame;       /* for the barber pole              */

    LUI_Button btn[L_BTN_COUNT];
    LUI_Text notes;

    char dir[1024];      /* the install folder                     */
    char version[64];    /* what is installed, e.g. "0.6.2"        */
    char latest[64];     /* what the host has, once checked        */
    char status[256];
    LU_State *up;
    int quit;
    int play_after; /* leave the loop and start the game */
} L_App;

/* ------------------------------------------------------------------------ *
 * Finding the install.
 *
 * The launcher must work from two very different places: beside the game in a
 * flat Windows folder, and three levels down inside C&C3D.app on macOS. Rather
 * than encode either shape, it walks up from its own location looking for the one
 * file the game cannot run without. That also makes a development run work from
 * anywhere, which is what --dir is for when it does not.
 * ------------------------------------------------------------------------ */

static int l_has_install(const char *dir)
{
    char probe[1200];
    FILE *f;
    snprintf(probe, sizeof probe, "%s/dosmenu.pack", dir);
    f = fopen(probe, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static void l_find_install(L_App *a)
{
    char cur[1024];
    int level;

    if (a->dir[0] && l_has_install(a->dir))
        return; /* --dir won, and it exists */

    if (!lp_self(cur, sizeof cur))
        snprintf(cur, sizeof cur, "./x");
    lp_dirname(cur);

    /* Up to five levels: MacOS -> Contents -> C&C3D.app -> the folder, with one
     * spare. A miss leaves `dir` at the executable's own folder, which is where
     * the error message will then say it looked. */
    for (level = 0; level < 5; level++) {
        char before[1024];
        if (l_has_install(cur)) {
            snprintf(a->dir, sizeof a->dir, "%s", cur);
            return;
        }
        snprintf(before, sizeof before, "%s", cur);
        lp_dirname(cur);
        if (!strcmp(before, cur))
            break; /* at the root: there is nowhere further up to look */
    }
    if (!a->dir[0])
        snprintf(a->dir, sizeof a->dir, "%s", cur);
}

/* Read the whole of a file. Returns a NUL-terminated buffer the caller owns, or
 * NULL. Binary mode throughout, because a changelog downloaded on one platform
 * and read on another must not have its line endings guessed at. */
static char *l_read_file(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 8 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    if (out_len)
        *out_len = n;
    return buf;
}

/* THE INSTALLED VERSION IS READ, NOT COMPILED IN, and that is the whole point of
 * a launcher that can update the game underneath itself. tools/release.sh writes
 * cnc3d-install.txt into the package; after an update the launcher rewrites it.
 * A folder without one is an install from before this existed, which is a state
 * to report rather than to crash on. */
static void l_read_installed(L_App *a)
{
    char path[1200];
    char *blob, *p;

    /* The launcher's OWN build number, as the fallback. A release package always
     * carries cnc3d-install.txt and that wins, because it describes the install
     * rather than the launcher; a folder built straight out of the tree has no
     * such record, and "v0.6.2+09447d6-dirty" is a far better answer there than
     * a question mark. */
    snprintf(a->version, sizeof a->version, "%s", LCFG_BUILD[0] ? LCFG_BUILD : "?");
    snprintf(path, sizeof path, "%s/cnc3d-install.txt", a->dir);
    blob = l_read_file(path, NULL);
    if (!blob)
        return;
    for (p = blob; p && *p;) {
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = '\0';
        if (!strncmp(p, "version ", 8)) {
            size_t n = strlen(p + 8);
            while (n && (p[8 + n - 1] == '\r' || p[8 + n - 1] == ' '))
                n--;
            if (n >= sizeof a->version)
                n = sizeof a->version - 1;
            memcpy(a->version, p + 8, n);
            a->version[n] = '\0';
        }
        p = nl ? nl + 1 : NULL;
    }
    free(blob);
}

/* Everything after a bare "--" on the launcher's own command line, handed
 * straight to the game. The macOS bundle passes "--w 1600 --h 960" this way,
 * which is not a number the launcher should know: the sidebar magnifies by whole
 * numbers only, so the window height has to be a multiple of 480, and that is a
 * fact about the renderer, recorded as an open gap. Forwarding rather than
 * hardcoding keeps it in the one place it was already written down. */
static char *l_game_args[16];
static int l_game_argc;

/* ------------------------------------------------------------------------ *
 * Starting the game.
 *
 * The launcher gets out of the way rather than sitting behind the game holding a
 * process handle: on POSIX it execs, so the launcher IS the game from that moment
 * and there is no second icon in the dock. On Windows CreateProcess plus an
 * immediate exit does the same job, since a running .exe cannot be replaced by an
 * update while it is still open.
 * ------------------------------------------------------------------------ */

static int l_launch(const char *dir, const char *exe, char *err, int errlen)
{
    char path[1200];
    int i;
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[2048];
    int n;
    snprintf(path, sizeof path, "%s\\%s", dir, exe);
    /* Quoted, because the install path can contain spaces and CreateProcess
     * splits an unquoted command line on them. */
    n = snprintf(cmd, sizeof cmd, "\"%s\"", path);
    for (i = 0; i < l_game_argc && n < (int)sizeof cmd; i++)
        n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", l_game_args[i]);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(path, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        snprintf(err, (size_t)errlen, "could not start %s (error %lu)", exe,
                 (unsigned long)GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
#else
    snprintf(path, sizeof path, "%s/%s", dir, exe);
    if (chdir(dir) != 0) {
        snprintf(err, (size_t)errlen, "could not enter %s", dir);
        return 0;
    }
    {
        char *argv[18];
        argv[0] = path;
        for (i = 0; i < l_game_argc; i++)
            argv[1 + i] = l_game_args[i];
        argv[1 + l_game_argc] = NULL;
        execv(path, argv);
    }
    /* execv only returns on failure. */
    snprintf(err, (size_t)errlen, "could not start %s", exe);
    return 0;
#endif
}

#ifdef _WIN32
#define L_GAME_EXE "cnc3d.exe"
#else
#define L_GAME_EXE "cnc3d"
#endif

/* ------------------------------------------------------------------------ *
 * Presentation: one 320x200 texture, one quad, all OpenGL 1.1.
 *
 * Lifted in shape from menu/dosmenu_shell.c rather than shared with it, because
 * that file owns the game's audio device, its movie player and its menu state
 * machine, and a launcher that linked all three to draw a quad would be a
 * launcher that fails to start when the sound card does.
 * ------------------------------------------------------------------------ */

static int l_pot(int v)
{
    int p = 1;
    while (p < v)
        p *= 2;
    return p;
}

static void l_layout(L_App *a)
{
    int w = 0, h = 0, pw = 0, ph = 0, sc;
    SDL_GL_GetDrawableSize(a->win, &w, &h);
    SDL_GetWindowSize(a->win, &pw, &ph);
    a->px = pw > 0 ? (float)w / (float)pw : 1.0f;
    a->py = ph > 0 ? (float)h / (float)ph : 1.0f;
    if (w < DM_SCREEN_W || h < DM_SCREEN_H) {
        a->scale = 1;
    } else {
        sc = w / DM_SCREEN_W;
        if (h / DM_SCREEN_H < sc)
            sc = h / DM_SCREEN_H;
        a->scale = sc < 1 ? 1 : sc;
    }
    a->vpx = (w - DM_SCREEN_W * a->scale) / 2;
    a->vpy = (h - DM_SCREEN_H * a->scale) / 2;
}

/* Window points -> menu pixels. A click on the letterbox bars lands a long way
 * off the plate on purpose, so it cannot press an edge button. */
static void l_to_menu(const L_App *a, int wx, int wy, int *mx, int *my)
{
    int fx = (int)(wx * a->px), fy = (int)(wy * a->py);
    if (fx < a->vpx || fy < a->vpy) {
        *mx = *my = -1000;
        return;
    }
    *mx = (fx - a->vpx) / a->scale;
    *my = (fy - a->vpy) / a->scale;
}

static void l_present(L_App *a)
{
    float u, v;
    int w = 0, h = 0, y;

    db_surface_to_rgba(&a->surf, a->pack->pal8, a->rgba, 0);
    for (y = 0; y < DM_SCREEN_H; y++)
        memcpy(a->padded + (long)y * a->tw * 4, a->rgba + (long)y * DM_SCREEN_W * 4,
               (size_t)DM_SCREEN_W * 4);
    glBindTexture(GL_TEXTURE_2D, a->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, a->tw, a->th, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 a->padded);

    l_layout(a);
    SDL_GL_GetDrawableSize(a->win, &w, &h);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glShadeModel(GL_FLAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glViewport(a->vpx, a->vpy, DM_SCREEN_W * a->scale, DM_SCREEN_H * a->scale);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    u = (float)DM_SCREEN_W / (float)a->tw;
    v = (float)DM_SCREEN_H / (float)a->th;
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
    SDL_GL_SwapWindow(a->win);
}

/* ------------------------------------------------------------------------ *
 * The screen.
 * ------------------------------------------------------------------------ */

static void l_set_buttons(L_App *a)
{
    LU_Phase ph = a->up ? lu_phase(a->up) : LU_NOTCONFIGURED;
    int i;
    static const char *labels[L_BTN_COUNT] = {"Play", "Editor", "Quit"};

    for (i = 0; i < L_BTN_COUNT; i++) {
        a->btn[i].x = 20 + i * (L_BTN_W + 8);
        a->btn[i].y = L_BTN_Y;
        a->btn[i].w = L_BTN_W;
        a->btn[i].h = L_BTN_H;
        a->btn[i].label = labels[i];
        a->btn[i].disabled = 0;
    }

    /* Reported: "if theres a new version available the play button
     * should say Update". One button, two jobs, and the label is the only place
     * the difference shows: a second button that is usually inert would make the
     * common case look like the exceptional one. */
    if (ph == LU_AVAILABLE)
        a->btn[L_BTN_PLAY].label = "Update";
    else if (ph == LU_DOWNLOADING || ph == LU_APPLYING)
        a->btn[L_BTN_PLAY].label = "Updating";
    else if (ph == LU_DONE)
        a->btn[L_BTN_PLAY].label = "Play";

    /* Drawn, disabled, and honest, the same way the menu's Multiplayer button is:
     * the map editor exists in the tree and does not yet exist as something a
     * player can be handed, so the button says where it will be rather than
     * pretending it is not coming. */
    a->btn[L_BTN_EDITOR].disabled = 1;

    /* Nothing is clickable while a download is in flight except Quit, which stays
     * live on purpose: a player who wants out during a 500 MB transfer should not
     * have to kill the process. */
    if (ph == LU_DOWNLOADING || ph == LU_APPLYING)
        a->btn[L_BTN_PLAY].disabled = 1;
}

static void l_draw(L_App *a)
{
    char line[256];
    LU_Phase ph = a->up ? lu_phase(a->up) : LU_NOTCONFIGURED;
    const DB_Shape *options;
    int i;

    /* CLEAR FIRST, AND THE PLATE IS NOT A CLEAR. dm_draw_plate blits TITLE.CPS with
     * the SHAPE blitter, and the shape blitter treats palette index 0 as
     * transparent. The title art is mostly index 0 around its edges, so those
     * pixels are never written and whatever was drawn there last frame survives:
     * the mouse pointer leaves a trail of arrows across the backdrop, one per
     * position it has rested in. dosmenu.c's own comment says the black regions
     * "come out black either way", and that is true of an UNTOUCHED surface,
     * which is the precondition this restores.
     *
     * The game's menu already does exactly this (dosmenu_shell.c:240,
     * memset(s->screen, DB_TBLACK, ...) immediately before its own draw). Leaving
     * it out here was the whole of the bug. */
    memset(a->screen, DB_TBLACK, sizeof a->screen);

    /* menus.cpp:802 Load_Title_Screen: the CPS is a full 320x200 plate. */
    dm_draw_plate(&a->surf, a->pack);

    /* menus.cpp:809 Dialog_Box, then :810 Draw_Caption's filigree pair. */
    dm_green_dialog(&a->surf, L_DLG_X, L_DLG_Y, L_DLG_W, L_DLG_H);
    options = db_shape(a->pack, "OPTIONS");
    if (options) {
        db_draw_shape_centered(&a->surf, options, DM_FILIGREE_LEFT, L_DLG_X + 12,
                               L_DLG_Y + 11);
        db_draw_shape_centered(&a->surf, options, DM_FILIGREE_RIGHT,
                               L_DLG_X + L_DLG_W - 14, L_DLG_Y + 11);
    }

    lui_print_centered(&a->surf, a->f8, "C&C 3D", DM_SCREEN_W / 2, L_TITLE_Y,
                       DM_TEXT_BRIGHT);

    /* The two numbers, side by side, because the question the player is actually
     * asking is whether they differ. */
    snprintf(line, sizeof line, "INSTALLED  v%s", a->version);
    lui_print(&a->surf, a->f6, line, L_PANEL_X, L_INFO_Y, DM_TEXT_MEDIUM);

    switch (ph) {
    case LU_NOTCONFIGURED:
        snprintf(line, sizeof line, "UPDATES  OFF");
        break;
    case LU_CHECKING:
        snprintf(line, sizeof line, "CHECKING...");
        break;
    case LU_UPTODATE:
        snprintf(line, sizeof line, "UP TO DATE");
        break;
    case LU_AVAILABLE:
    case LU_DOWNLOADING:
    case LU_APPLYING:
        lu_latest(a->up, a->latest, sizeof a->latest);
        snprintf(line, sizeof line, "AVAILABLE  v%s", a->latest);
        break;
    case LU_DONE:
        snprintf(line, sizeof line, "UPDATED");
        break;
    case LU_FAILED:
        snprintf(line, sizeof line, "CHECK FAILED");
        break;
    default:
        line[0] = '\0';
        break;
    }
    lui_print_right(&a->surf, a->f6, line, L_PANEL_X + L_PANEL_W, L_INFO_Y,
                    ph == LU_AVAILABLE ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM);

    /* The changelog. */
    lui_panel(&a->surf, L_PANEL_X, L_PANEL_Y, L_PANEL_W, L_PANEL_H);
    lui_text_draw(&a->surf, a->f6, &a->notes, L_TEXT_X, L_TEXT_Y, L_TEXT_W, L_TEXT_H);
    lui_scrollbar_draw(&a->surf, &a->notes, L_BAR_X, L_TEXT_Y, L_BAR_W, L_TEXT_H);

    /* The status line, and the gauge only while there is something to measure. */
    if (a->up)
        lu_status(a->up, a->status, sizeof a->status);
    lui_print(&a->surf, a->f6, a->status, L_PANEL_X, L_STATUS_Y,
              ph == LU_FAILED ? DB_WHITE : DM_TEXT_MEDIUM);
    if (ph == LU_DOWNLOADING || ph == LU_APPLYING)
        lui_gauge_draw(&a->surf, L_GAUGE_X, L_GAUGE_Y, L_GAUGE_W, L_GAUGE_H,
                       lu_progress(a->up), a->frame);

    for (i = 0; i < L_BTN_COUNT; i++)
        lui_button_draw(&a->surf, a->grad, &a->btn[i], a->pressed == i, a->hot == i);

    dm_draw_cursor(&a->surf, a->pack, a->mx, a->my);
}

/* Rebuild the wrapped panel. Called when the text it shows changes, and not per
 * frame: wrapping allocates, and a launcher that mallocs sixty times a second to
 * draw a static changelog would be a launcher with a leak waiting to happen. */
static void l_set_notes(L_App *a, const char *blob)
{
    lui_text_free(&a->notes);
    lui_text_build(&a->notes, a->f6, blob, L_TEXT_W);
    a->notes.visible = L_TEXT_H / 7;
    a->notes.top = 0;
}

/* ------------------------------------------------------------------------ *
 * Input.
 * ------------------------------------------------------------------------ */

static void l_activate(L_App *a, int item)
{
    LU_Phase ph = a->up ? lu_phase(a->up) : LU_NOTCONFIGURED;

    switch (item) {
    case L_BTN_PLAY:
        if (ph == LU_AVAILABLE) {
            lu_apply(a->up);
        } else {
            a->play_after = 1;
            a->quit = 1;
        }
        break;
    case L_BTN_QUIT:
        a->quit = 1;
        break;
    default:
        break; /* Editor is disabled and cannot get here */
    }
}

static int l_next_enabled(const L_App *a, int from, int delta)
{
    int i, idx = from;
    for (i = 0; i < L_BTN_COUNT; i++) {
        idx += delta;
        if (idx < 0)
            idx = L_BTN_COUNT - 1;
        if (idx >= L_BTN_COUNT)
            idx = 0;
        if (!a->btn[idx].disabled)
            return idx;
    }
    return from;
}

static void l_event(L_App *a, const SDL_Event *e)
{
    int i;
    switch (e->type) {
    case SDL_QUIT:
        a->quit = 1;
        break;
    case SDL_MOUSEMOTION:
        l_to_menu(a, e->motion.x, e->motion.y, &a->mx, &a->my);
        for (i = 0; i < L_BTN_COUNT; i++)
            if (lui_button_hit(&a->btn[i], a->mx, a->my))
                a->hot = i;
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button != SDL_BUTTON_LEFT)
            break;
        l_to_menu(a, e->button.x, e->button.y, &a->mx, &a->my);
        for (i = 0; i < L_BTN_COUNT; i++)
            if (lui_button_hit(&a->btn[i], a->mx, a->my)) {
                a->pressed = i;
                a->hot = i;
            }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT)
            break;
        l_to_menu(a, e->button.x, e->button.y, &a->mx, &a->my);
        /* gadget.cpp releases on the control the press started on, so a drag off
         * a button cancels it rather than firing the one under the cursor. */
        if (a->pressed >= 0 && lui_button_hit(&a->btn[a->pressed], a->mx, a->my))
            l_activate(a, a->pressed);
        a->pressed = -1;
        break;
    case SDL_MOUSEWHEEL:
        lui_text_scroll(&a->notes, -e->wheel.y * 3);
        break;
    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_ESCAPE:
            a->quit = 1;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            l_activate(a, a->hot);
            break;
        case SDLK_LEFT:
            a->hot = l_next_enabled(a, a->hot, -1);
            break;
        case SDLK_RIGHT:
        case SDLK_TAB:
            a->hot = l_next_enabled(a, a->hot, +1);
            break;
        case SDLK_UP:
            lui_text_scroll(&a->notes, -1);
            break;
        case SDLK_DOWN:
            lui_text_scroll(&a->notes, +1);
            break;
        case SDLK_PAGEUP:
            lui_text_scroll(&a->notes, -a->notes.visible);
            break;
        case SDLK_PAGEDOWN:
            lui_text_scroll(&a->notes, +a->notes.visible);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------------ */

/* Drive the update worker from a terminal. `mode` is 1 for a check, 2 for a check
 * followed by an install if one is offered. Returns a shell exit status: 0 when
 * there was nothing to do or the work succeeded, 1 on failure, and 2 for "an
 * update is available and was not installed", so a script can tell the three
 * apart without reading the text. */
static int l_headless(L_App *a, int mode)
{
    char status[256], latest[64];
    LU_Phase ph;
    int guard;

    printf("install:   %s\n", a->dir);
    printf("installed: v%s\n", a->version);
    printf("host:      %s\n", lu_host_summary());

    if (lu_phase(a->up) == LU_NOTCONFIGURED) {
        lu_status(a->up, status, sizeof status);
        printf("result:    %s\n", status);
        return 0;
    }

    lu_check(a->up);
    /* Poll rather than join: the worker owns its own thread and the phase is the
     * contract between them. 120 seconds is well past libcurl's own timeouts, so
     * reaching it means the worker is wedged, which is worth saying out loud. */
    for (guard = 0; guard < 1200; guard++) {
        ph = lu_phase(a->up);
        if (ph != LU_CHECKING)
            break;
        SDL_Delay(100);
    }
    ph = lu_phase(a->up);
    lu_status(a->up, status, sizeof status);
    lu_latest(a->up, latest, sizeof latest);

    if (ph == LU_FAILED) {
        printf("result:    FAILED, %s\n", status);
        return 1;
    }
    if (ph == LU_UPTODATE) {
        printf("latest:    v%s\n", latest);
        printf("result:    up to date\n");
        return 0;
    }
    if (ph != LU_AVAILABLE) {
        printf("result:    the check did not finish (phase %d)\n", (int)ph);
        return 1;
    }

    printf("latest:    v%s\n", latest);
    if (mode < 2) {
        printf("result:    v%s is available\n", latest);
        return 2;
    }

    lu_apply(a->up);
    for (guard = 0; guard < 36000; guard++) {
        ph = lu_phase(a->up);
        if (ph == LU_DONE || ph == LU_FAILED)
            break;
        if (guard % 20 == 0) {
            int pct = lu_progress(a->up);
            lu_status(a->up, status, sizeof status);
            printf("           %s%s", status, pct >= 0 ? "" : "\n");
            if (pct >= 0)
                printf(" %d%%\n", pct / 10);
            fflush(stdout);
        }
        SDL_Delay(100);
    }
    lu_status(a->up, status, sizeof status);
    if (lu_phase(a->up) != LU_DONE) {
        printf("result:    FAILED, %s\n", status);
        return 1;
    }
    printf("result:    installed v%s\n", latest);
    return 0;
}

static void l_usage(void)
{
    printf("C&C 3D launcher\n"
           "  --dir <folder>   the installed game (default: found from this binary)\n"
           "  --shot <file>    render one frame to a PNG and exit\n"
           "  --scale <n>      window scale for --shot and for the window\n"
           "  --play           start the game at once, no window\n"
           "  -- <args>        everything after -- is passed to the game\n"
           "  --check          ask the update host what it has, print it, exit\n"
           "  --update         check and, if there is one, install it. No window.\n");
}

int main(int argc, char **argv)
{
    L_App a;
    char err[512];
    char shot[1024];
    char path[1200];
    char *local_notes;
    int scale = 3, play_now = 0, headless = 0, i;

    memset(&a, 0, sizeof a);
    a.pressed = -1;
    a.hot = L_BTN_PLAY;
    shot[0] = '\0';

#ifdef _WIN32
    /* The launcher is linked -mwindows so that double-clicking it does not put a
     * black console box behind the dialog. That also throws away stdout, which
     * --check and --update are entirely made of. Attaching to the console of
     * whatever started us gives both: silence from Explorer, and real output when
     * run from a cmd prompt. It fails harmlessly when there is no parent console,
     * which is the double-clicked case. */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) {
            /* Everything after this belongs to the game, not to the launcher. */
            while (++i < argc && l_game_argc < (int)(sizeof l_game_args / sizeof *l_game_args))
                l_game_args[l_game_argc++] = argv[i];
            break;
        }
        if (!strcmp(argv[i], "--dir") && i + 1 < argc)
            snprintf(a.dir, sizeof a.dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            snprintf(shot, sizeof shot, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--play"))
            play_now = 1;
        else if (!strcmp(argv[i], "--check"))
            headless = 1;
        else if (!strcmp(argv[i], "--update"))
            headless = 2;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            l_usage();
            return 0;
        }
    }
    if (scale < 1)
        scale = 1;

    l_find_install(&a);
    l_read_installed(&a);

    if (play_now) {
        if (!l_launch(a.dir, L_GAME_EXE, err, sizeof err)) {
            fprintf(stderr, "%s\n", err);
            return 1;
        }
        return 0;
    }

    snprintf(path, sizeof path, "%s/dosmenu.pack", a.dir);
    a.pack = db_pack_load(path, err, sizeof err);
    if (!a.pack) {
        /* A message box rather than stderr: this program is double-clicked, and a
         * double-clicked program that writes to a console nobody opened has said
         * nothing at all. That is the exact defect the .bat launchers left behind
         * ("Seventeen launchers"). */
        char msg[3072];
        snprintf(msg, sizeof msg,
                 "C&C 3D could not find its game files.\n\n"
                 "Looked in:\n%s\n\n%s\n\n"
                 "The launcher belongs in the same folder as the game, or inside "
                 "C&C3D.app in it.",
                 a.dir, err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "C&C 3D", msg, NULL);
        fprintf(stderr, "%s\n", msg);
        return 1;
    }

    db_surface_init(&a.surf, DM_SCREEN_W, DM_SCREEN_H, a.screen);
    a.f6 = db_font(a.pack, "6POINT");
    a.f8 = db_font(a.pack, "8POINT");
    a.grad = db_font(a.pack, "GRAD6FNT");
    a.mx = DM_SCREEN_W / 2;
    a.my = DM_SCREEN_H / 2;

    /* The changelog for what is INSTALLED, read off the disk, so the panel has
     * something true in it before any network call has been made or has failed. */
    snprintf(path, sizeof path, "%s/CHANGELOG.txt", a.dir);
    local_notes = l_read_file(path, NULL);
    l_set_notes(&a, local_notes ? local_notes
                                : "No changelog was found beside the game.\n\n"
                                  "CHANGELOG.txt ships inside the download; an "
                                  "install missing it still plays.");
    free(local_notes);

    a.up = lu_create(a.dir, a.version);
    snprintf(a.status, sizeof a.status, " ");

    /* THE HEADLESS PATH, for the gate suite and for a support question that has to
     * be answered from a terminal. It drives the same worker the window does, so
     * what it proves is what a player's launcher will do, rather than a second
     * implementation that agrees with the first until it does not. */
    if (headless) {
        int rc = l_headless(&a, headless);
        lui_text_free(&a.notes);
        lu_destroy(a.up);
        db_pack_free(a.pack);
        return rc;
    }

    if (shot[0]) {
        /* The still-picture path, for the gate suite and for a design review that
         * wants to look at pixels rather than at a description of them. No window
         * and no GL: the surface IS the evidence. */
        unsigned char *big;
        int bw = DM_SCREEN_W * scale, bh = DM_SCREEN_H * scale, ok, guard;
        a.rgba = (unsigned char *)calloc((size_t)DM_SCREEN_W * DM_SCREEN_H * 4, 1);

        /* A REAL CHECK BEFORE THE PICTURE, when there is a host to ask. A shot of
         * the launcher showing "UPDATES OFF" would be a shot of a state a player
         * with a working host never sees, and this file's whole job is to be
         * evidence. Bounded, so a dead host costs ten seconds and still draws. */
        if (lu_phase(a.up) != LU_NOTCONFIGURED) {
            lu_check(a.up);
            for (guard = 0; guard < 100 && lu_phase(a.up) == LU_CHECKING; guard++)
                SDL_Delay(100);
            if (lu_notes_changed(a.up) && lu_notes(a.up))
                l_set_notes(&a, lu_notes(a.up));
        }
        l_set_buttons(&a);
        a.mx = -100; /* no pointer in a reference shot */
        a.my = -100;
        l_draw(&a);
        db_surface_to_rgba(&a.surf, a.pack->pal8, a.rgba, 0);
        big = (unsigned char *)malloc((size_t)bw * bh * 4);
        png_nearest_scale(a.rgba, DM_SCREEN_W, DM_SCREEN_H, big, scale);
        ok = png_write_rgba(shot, big, bw, bh);
        free(big);
        free(a.rgba);
        printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED to write", shot, bw, bh);
        lui_text_free(&a.notes);
        lu_destroy(a.up);
        db_pack_free(a.pack);
        return ok ? 0 : 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    a.win = SDL_CreateWindow("C&C 3D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             DM_SCREEN_W * scale, DM_SCREEN_H * scale, SDL_WINDOW_OPENGL);
    if (!a.win) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_GL_CreateContext(a.win)) {
        fprintf(stderr, "GL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE); /* the plate draws MOUSE.SHP, the 1995 pointer */

    a.tw = l_pot(DM_SCREEN_W);
    a.th = l_pot(DM_SCREEN_H);
    a.rgba = (unsigned char *)calloc((size_t)DM_SCREEN_W * DM_SCREEN_H * 4, 1);
    a.padded = (unsigned char *)calloc((size_t)a.tw * a.th * 4, 1);
    glGenTextures(1, &a.tex);
    glBindTexture(GL_TEXTURE_2D, a.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    /* Ask the host what it has, at once, so the answer is usually on screen before
     * the player has finished reading the changelog they already had. */
    lu_check(a.up);

    while (!a.quit) {
        SDL_Event e;
        l_set_buttons(&a);
        while (SDL_PollEvent(&e))
            l_event(&a, &e);
        if (lu_notes_changed(a.up)) {
            const char *n = lu_notes(a.up);
            if (n)
                l_set_notes(&a, n);
        }
        a.frame++;
        l_draw(&a);
        l_present(&a);
    }

    lui_text_free(&a.notes);
    lu_destroy(a.up);
    db_pack_free(a.pack);
    SDL_DestroyWindow(a.win);
    SDL_Quit();

    if (a.play_after && !l_launch(a.dir, L_GAME_EXE, err, sizeof err)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "C&C 3D", err, NULL);
        return 1;
    }
    return 0;
}
