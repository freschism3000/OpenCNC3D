/*
 * launcher.c -- OpenCNC3D's launcher. The thing a player double-clicks.
 *
 *   OpenCNC3D                      open the launcher
 *   OpenCNC3D --dir <folder>       point it at an install other than its own
 *   OpenCNC3D --shot out.png       render one frame to a PNG and exit (no window)
 *   OpenCNC3D --play               skip the launcher and start the game
 *   OpenCNC3D -- --w 1600 --h 960  pass the rest to the game when Play is pressed
 *
 * WHAT IT IS. One 320x200 dialog drawn with the game's own DOS primitives, over
 * the 1995 title plate out of TITLE.CPS: the build that is installed, the build
 * that is available, the MODULES this install carries, and four buttons. Play
 * starts the game and gets out of the way. When the host has a newer build, Play
 * becomes Update. CHANGELOG swaps the well for that build's notes and back again.
 * EDITOR OPENS THE MAP EDITOR, which is a flag on the renderer rather than a
 * program of its own, so that button runs cnc_eyes with --edit and a starting map
 * instead of running the game.
 *
 * MOVIES AND MUSIC ARE NOT IN THE PACKAGE. lmedia.c downloads them from disc images
 * on archive.org: the 1995 cutscenes from the two Command & Conquer Gold discs, EA's
 * 2007 freeware release, and the music from the Covert Operations disc, the one image
 * that carries the whole score. This file is only the screens for it. The first start
 * of an install opens on SETUP, two ticked boxes and Install; the GEAR in the module
 * grid's heading row opens SETTINGS, the same two boxes showing what the player chose
 * and what is on the disk, where a tick adds a component and an untick removes it
 * after a question that names it. Both hand the job to one DOWNLOADING screen with a
 * gauge, the size so far and the percent. Which files are on the disk is the only
 * switch: the game never reads the choice, it plays what it finds, and it plays
 * cleanly with none of it.
 *
 * A FOLDER THAT HOLDS THE LAUNCHER AND NOT THE GAME, which is what a Windows Setup
 * that was stopped leaves in a folder that already held files, opens on INSTALL: the
 * same two boxes, and a button that installs the newest build and then the ticked
 * movies and music, on the same downloading screen, the way the headless
 * --install-game does. With not even the menu art there to draw that screen, the
 * launcher first asks, in a plain system dialog, to fetch the art on its own.
 *
 * WHAT A MODULE IS. Another game built on this engine, started from this launcher
 * and able to play across to the others. OpenCNC3D is the first, and the grid of four
 * tiles is where the next one goes. Three of the four are empty and say so, and
 * pressing one opens the module browser, which today lists nothing: the browser is
 * the real screen with a real empty list rather than a button that does nothing,
 * because the day a second module exists the only change is what the list reads
 * from. Nothing in the grid is read off the disk yet, because nothing on the disk
 * is a module yet.
 *
 * That button used to be drawn disabled, in the engine's own BOXSTYLE_GREEN_DIS_RAISED,
 * for as long as the editor existed in the tree and not in anything a player was
 * handed. Enabling it is one line here; keeping it honest is a refusal in both
 * packagers, which now stop a build that does not carry the editor binary and the maps
 * it opens. The enable and that refusal are ONE change: an enabled button over a
 * package with no editor behind it fails in front of the player at the moment it is
 * pressed, which is worse than a grey one.
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
#include "lcmdline.h"
#include "lmedia.h"
#include "lmodule.h"
#include "lpath.h"
#include "lui.h"
#include "lskin.h"
#include "lsignal.h"
#include "launcher_logos.h"
#include "lupdate.h"
#include "lzip.h"
#include "pngwrite.h"

/* lsetup_cli.c: the headless --media-install and --install-game modes. main() asks
 * it first and leaves at once when it says the command line was its own. Declared
 * here as well as in whatever header carries it, so this file states the one
 * signature it relies on. */
int lsetup_cli_main(int argc, char **argv, int *handled);

#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "ra_emblem.h"

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

/* THE WELL SHOWS ONE SCREEN AT A TIME. One rectangle, several contents: the module
 * grid, the changelog, the module browser, and the three movies-and-music screens.
 * They are screens rather than separate dialogs because the two numbers above the
 * well and the buttons under it are true on all of them, and a second window would
 * have to repeat them or lose them. */
#define L_VIEW_MODULES 0
#define L_VIEW_NOTES 1
#define L_VIEW_BROWSE 2
#define L_VIEW_SETUP 3     /* first start: download the movies and music?        */
#define L_VIEW_MEDIA_RUN 4 /* a download or removal under way, or stopped by one  */
#define L_VIEW_SETTINGS 5  /* the gear: add or remove them later                  */
#define L_VIEW_INSTALL 6   /* a folder with the launcher and not the game         */

/* The heading row inside the well, shared by every screen. */
#define L_HEAD_Y (L_PANEL_Y + 11)

/* THE MODULE GRID. Four tiles, square, across the well: 4*62 plus three 8 pixel
 * gaps is 272 in a 284 well, which leaves 6 a side. Down the well the heading, the
 * tiles and the name row make one block with the slack split above and below it,
 * so the grid sits in the middle of the well rather than at the top of it. */
#define L_MOD_COUNT 4
#define L_MOD_W 62
#define L_MOD_H 62
#define L_MOD_STEP (L_MOD_W + 8)
#define L_MOD_X0 (L_PANEL_X + 6)
#define L_MOD_Y (L_PANEL_Y + 26)
#define L_MOD_NAME_Y (L_MOD_Y + L_MOD_H + 3)

/* Inside an empty tile: the plus, then the subtitle under it. The two are centred
 * as one block, which is why neither number is simply half the tile. */
#define L_MOD_PLUS_ARM 9
#define L_MOD_PLUS_THICK 3
#define L_MOD_PLUS_DY 24
#define L_MOD_SOON_DY 40

/* THE EMBLEM IN THE C&C TILE IS A PIECE OF THE TITLE PLATE, not a second copy of
 * the artwork. Measured off the plate's own pixels rather than guessed: the ring
 * runs 105 to 210 across and 16 to 108 down, and this is that box with a couple of
 * pixels of black around it so the scale down has something to land on. */
#define L_EMBLEM_X 104
#define L_EMBLEM_Y 14
#define L_EMBLEM_W 108
#define L_EMBLEM_H 96
#define L_LOGO_W 40
#define L_LOGO_H 40

/* The module browser: an empty list in a well of its own, and the button that
 * closes it. */
#define L_BROWSE_LIST_Y (L_PANEL_Y + 22)
#define L_BROWSE_LIST_H 56
#define L_CLOSE_W 64
#define L_CLOSE_Y (L_PANEL_Y + 86)

/* THE GEAR, in the module grid's heading row at its right hand end, where a panel's
 * settings control usually is and where nothing else on this screen goes: the
 * tiles start 5 pixels under it and the "1 OF 4" count moves left to make room.
 * 15 square is the 11 pixel picture inside a one pixel bevel with a pixel of the
 * button's own green either side. */
#define L_GEAR_W 15
#define L_GEAR_H 15
#define L_GEAR_X (L_PANEL_X + L_PANEL_W - 8 - L_GEAR_W)
#define L_GEAR_Y (L_HEAD_Y - 4)
#define L_MOD_COUNT_RX (L_GEAR_X - 6) /* where "1 OF 4" now ends */

/* THE MOVIES AND MUSIC SCREENS. A paragraph under the heading, then the two
 * checkbox rows, then the space line; every number here was checked against a
 * --shot of the screen rather than worked out on paper. */
#define L_MEDIA_TEXT_X (L_PANEL_X + 8)
#define L_MEDIA_TEXT_Y (L_HEAD_Y + 13)
#define L_MEDIA_TEXT_W (L_PANEL_W - 16)
#define L_CHK_X (L_PANEL_X + 22)
#define L_CHK_W (L_PANEL_W - 44)
#define L_CHK_H 11
#define L_CHK_Y0 (L_PANEL_Y + 57)
#define L_CHK_STEP 15
#define L_SPACE_Y (L_PANEL_Y + 93)

/* The downloading screen: the file, a gauge as wide as the checkbox rows, and the
 * size so far and the percent under it. */
#define L_RUN_ITEM_Y (L_PANEL_Y + 33)
#define L_RUN_GAUGE_X L_CHK_X
#define L_RUN_GAUGE_Y (L_PANEL_Y + 45)
#define L_RUN_GAUGE_W L_CHK_W
#define L_RUN_GAUGE_H 9
#define L_RUN_SIZE_Y (L_PANEL_Y + 59)
#define L_RUN_NOTE_Y (L_PANEL_Y + 79)

#define L_STATUS_Y 154
#define L_GAUGE_X 18
#define L_GAUGE_Y 164
#define L_GAUGE_W 284
#define L_GAUGE_H 7

/* FOUR ALONG THE BOTTOM, in the same span the three used to fill: 4*64 plus three
 * 8 pixel gaps is 280, from 20 to 300, which is the dialog's own margin either
 * side. */
#define L_BTN_Y 175
#define L_BTN_H (ls_enabled() ? 14 : 11)
#define L_BTN_W 64
#define L_BTN_STEP (L_BTN_W + 8)
#define L_BTN_X0 20

enum
{
    L_BTN_PLAY = 0,
    L_BTN_NOTES,
    L_BTN_EDITOR,
    L_BTN_QUIT,
    /* The browser's own, and it is not one of the four: it sits in the well it
     * closes and is hidden on every other screen. It is in this table anyway so
     * that the geometry, the hit test, the keyboard walk and the draw read a row
     * for it as they do for the rest. */
    L_BTN_CLOSE,
    /* The two checkboxes of Setup and Settings, and the gear that opens Settings.
     * Rows of the same table for the same reason Close is, and hidden on every
     * screen they are not part of. */
    L_BTN_CHK_MOVIES,
    L_BTN_CHK_MUSIC,
    L_BTN_GEAR,
    L_BTN_COUNT
};
#define L_BTN_ROW 4 /* how many of the above are the row along the bottom */

/* ------------------------------------------------------------------------ *
 * The program.
 * ------------------------------------------------------------------------ */

/* A MODULE IS A GAME ON THIS ENGINE, and the table is the whole model of them: a
 * row with a name is one that is installed, a row without one is an empty slot and
 * draws the add tile. It is a fixed four because the grid is four; a fifth module
 * is a bigger grid and a scroll, which is a decision to take when there is a fifth
 * module rather than a shape to guess at now. */
typedef struct
{
    const char *name;
    int installed;
} L_Module;

typedef struct
{
    SDL_Window *win;
    GLuint tex;
    int tw, th;          /* power-of-two texture holding the 320x200 */
    int scale, vpx, vpy; /* letterbox, recomputed every frame        */
    float px, py;        /* drawable pixels per window point         */

    DB_Pack *pack;
    DB_Pack *ra_pack;
    char ra_missing[96]; /* why Red Alert is not installed, or "" when it is */
    DB_Surface surf;
    unsigned char screen[DM_SCREEN_W * DM_SCREEN_H];
    unsigned char *rgba, *padded;

    const DB_Font *f6, *f8, *grad;

    int mx, my;      /* pointer, in menu pixels          */
    int pressed;     /* button index held down, or -1    */
    int hot;         /* keyboard highlight               */
    int frame;       /* for the barber pole              */
    unsigned signal_ms; /* elapsed monotonic time for the EVA transmission */
    int no_motion;

    LUI_Button btn[L_BTN_COUNT];
    LUI_Text notes;

    /* Which screen the well is showing, and where the highlight is. The highlight
     * lives in one of two rows: the module grid or the button row, and `on_mods`
     * says which, so Left and Right walk the row you are in rather than jumping
     * between two kinds of control. */
    int view;
    int on_mods;
    int mod_hot;     /* module under the highlight                          */
    int mod_pressed; /* module held down by the mouse, or -1                */
    int mod_sel;     /* the module this launcher is in: OpenCNC3D, for now     */
    L_Module mod[L_MOD_COUNT];

    char dir[1024];      /* the install folder                     */
    char version[64];    /* what is installed, e.g. "0.6.2"        */
    char latest[64];     /* what the host has, once checked        */
    char status[256];
    LU_State *up;
    int quit;
    int play_after; /* leave the loop and start something: 1 the game, 2 the editor */

    /* MOVIES AND MUSIC. lmedia owns the files, the worker and the record; this
     * holds only what the screens show and what the player has ticked. */
    LM_State *media;
    int have_movies, have_music;     /* all of it on the disk, re-read when a screen opens and a job ends */
    int any_movies, any_music;       /* any of it on the disk: what an untick would remove */
    long long here_movies, here_music; /* how much that is, for the removal question */
    int rec_ok, rec_movies, rec_music; /* what opencnc3d-media.txt says was chosen  */
    int pend_movies, pend_music;     /* and whether it says the disk had not got there */
    long long size_movies, size_music; /* from the manifest                       */
    int want_movies, want_music;     /* the two ticks                             */
    int job_movies, job_music;       /* what the job in hand was asked for        */
    int had_movies, had_music;       /* what was on the disk when it started      */
    int had_any_movies, had_any_music;
    int m_from;                      /* the screen the job was started from       */
    int m_running;                   /* started, and not yet seen to end          */
    int m_cancelling;                /* Cancel pressed, the worker not stopped yet */
    int m_confirm;                   /* a removal is waiting on Remove or Keep    */
    long long free_bytes;            /* on the install's disk, or -1 when unknown */
    char m_note[2][64];              /* the checkbox rows' right hand column      */
    /* The media screens' own status line, and the one result line the module grid
     * shows when a job has finished, until the player next presses something. */
    char mstatus[256];

    /* THE FIRST INSTALL, from INSTALL in a folder that holds the launcher and not the
     * game. Its own updater state, made when Install is pressed and gone when the job
     * ends; it holds the folder's lock, and the movies and music job runs under it. */
    int install_offer;  /* this folder holds the launcher and not the game */
    LU_State *fresh;
    int i_job;          /* the job on the downloading screen is a first install */
    int i_stage;        /* 0 asking the site, 1 the game package, 2 the movies and music */
    LM_Phase i_phase;   /* where the screen reads it as being */
    long long i_game, i_estimate, i_shown, i_total;
    char i_item[160], i_reason[256];

    /* What the status line said in the frame just drawn, for --shot to report. */
    char drawn_status[256];

    /* --shot only: a picture of the downloading screen at a chosen point, without
     * a job running. The real screens never read these. */
    int shot_media;
    LM_Phase shot_phase;
    long long shot_done, shot_total;
    char shot_reason[256];
} L_App;

/* ------------------------------------------------------------------------ *
 * Finding the install.
 *
 * The launcher must work from two very different places: beside the game in a
 * flat Windows folder, and three levels down inside OpenCNC3D.app on macOS. Rather
 * than encode either shape, it walks up from its own location looking for the one
 * file the game cannot run without. That also makes a development run work from
 * anywhere, which is what --dir is for when it does not.
 * ------------------------------------------------------------------------ */

/* dosmenu.pack, or an install record (lz_is_install). The record counts because an
 * update replaces dosmenu.pack by renaming, and a kill between the two renames
 * leaves the folder without one: a launcher that did not recognise its own folder
 * then would never reach the recovery that puts it back. Finding a folder only
 * decides where to READ: nothing is changed in one that lz_is_install refuses. */
static int l_has_install(const char *dir)
{
    char probe[1200];
    FILE *f;
    snprintf(probe, sizeof probe, "%s/dosmenu.pack", dir);
    f = fopen(probe, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return lz_is_install(dir);
}

static int l_has_named_file(const char *dir, const char *name)
{
    char path[1200];
    FILE *file;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static void l_find_install(L_App *a)
{
    char cur[1024], own[1024];
    int level;

    if (a->dir[0] && l_has_install(a->dir))
        return; /* --dir won, and it exists */

    if (!lp_self(cur, sizeof cur))
        snprintf(cur, sizeof cur, "./x");
    lp_dirname(cur);
    snprintf(own, sizeof own, "%s", cur);

    /* Up to five levels: MacOS -> Contents -> OpenCNC3D.app -> the folder, with one
     * spare. A miss leaves `dir` at the executable's own folder, which is where
     * the error message will then say it looked, and not at whatever folder five
     * levels up the walk stopped in. */
    for (level = 0; level < 5; level++) {
        char before[1024];
        if (l_has_install(cur)) {
            /* SAID, when it overrides a --dir: a folder named on the command line
             * and passed over without a word is how a test meant for one folder
             * started the game in another. */
            if (a->dir[0])
                fprintf(stderr, "--dir %s holds no game; using %s, where this "
                        "launcher is\n", a->dir, cur);
            snprintf(a->dir, sizeof a->dir, "%s", cur);
            return;
        }
        snprintf(before, sizeof before, "%s", cur);
        lp_dirname(cur);
        if (!strcmp(before, cur))
            break; /* at the root: there is nowhere further up to look */
    }
    if (!a->dir[0])
        snprintf(a->dir, sizeof a->dir, "%s", own);
}

/* WHAT MAKES RED ALERT INSTALLED is lmodule.c's test, the one the movies and music
 * also ask before they fetch Red Alert's own, and then a menu pack that loads. */
static void l_find_ra_module(L_App *a)
{
    char path[1200], error[256];
    if (!lmod_ra_present(a->dir, a->ra_missing, (int)sizeof a->ra_missing))
        return;
    snprintf(path, sizeof path, "%s/ra3d/dosmenu.pack", a->dir);
    a->ra_pack = db_pack_load(path, error, sizeof error);
    if (!a->ra_pack) {
        fprintf(stderr, "RA3D menu pack: %s\n", error);
        snprintf(a->ra_missing, sizeof a->ra_missing, "ra3d/dosmenu.pack does not load");
        return;
    }
    a->mod[1].name = "Red Alert 3D";
    a->mod[1].installed = 1;
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
 * opencnc3d-install.txt into the package; after an update the launcher rewrites it.
 * A folder without one is an install from before this existed, which is a state
 * to report rather than to crash on. */
static void l_read_installed(L_App *a)
{
    char path[1200];
    char *blob, *p;

    /* The launcher's OWN build number, as the fallback. A release package always
     * carries opencnc3d-install.txt and that wins, because it describes the install
     * rather than the launcher; a folder built straight out of the tree has no
     * such record, and "v0.6.2+09447d6-dirty" is a far better answer there than
     * a question mark. */
    snprintf(a->version, sizeof a->version, "%s", LCFG_BUILD[0] ? LCFG_BUILD : "?");
    snprintf(path, sizeof path, "%s/opencnc3d-install.txt", a->dir);
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
 * fact about the renderer. Forwarding rather than hardcoding keeps it in the
 * renderer's hands. How many fit, and why a longer list is refused rather than
 * cut, is lcmdline.h's. */
static char *l_game_args[LC_GAME_ARGS];
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

static int l_launch(const char *dir, const char *exe, const char *module,
                    char *err, int errlen)
{
    char path[1200];
    int i;
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    /* AS LONG AS WINDOWS TAKES, 32767 characters with the terminator, and every
     * argument quoted by lc_win_append: the install path can hold spaces, and so
     * can a forwarded save folder or script, and CreateProcess hands the game one
     * string that its C runtime splits on them. A line that does not fit is
     * REFUSED, naming the argument it stopped at, rather than cut. */
    static char cmd[32767];
    size_t n = 0;
    const char *at = path;
    snprintf(path, sizeof path, "%s\\%s", dir, exe);
    cmd[0] = '\0';
    if (lc_win_append(cmd, sizeof cmd, &n, path)
        && (!module || (lc_win_append(cmd, sizeof cmd, &n, at = "--module")
                        && lc_win_append(cmd, sizeof cmd, &n, at = module)))) {
        for (i = 0; i < l_game_argc; i++)
            if (!lc_win_append(cmd, sizeof cmd, &n, at = l_game_args[i]))
                break;
        if (i == l_game_argc)
            at = NULL;
    }
    if (at) {
        snprintf(err, (size_t)errlen, "the command line for %.40s is longer than "
                 "Windows takes; it stops at %.400s", exe, at);
        return 0;
    }
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
    /* dir may be relative. Resolve the executable from the new working directory. */
    snprintf(path, sizeof path, "./%s", exe);
    {
        char *argv[1 + 2 + sizeof l_game_args / sizeof *l_game_args + 1];
        int offset = module ? 2 : 0;
        argv[0] = path;
        if (module) { argv[1] = (char*)"--module"; argv[2] = (char*)module; }
        for (i = 0; i < l_game_argc; i++)
            argv[1 + offset + i] = l_game_args[i];
        argv[1 + offset + l_game_argc] = NULL;
        execv(path, argv);
    }
    /* execv only returns on failure. */
    snprintf(err, (size_t)errlen, "could not start %s", exe);
    return 0;
#endif
}

#ifdef _WIN32
#define L_GAME_EXE "opencnc3d-game.exe"
#define L_RA_GAME_EXE "ra3d\\opencnc3d-game.exe"
#define L_EDIT_EXE "cnc_eyes.exe"
#else
#define L_GAME_EXE "opencnc3d"
#define L_RA_GAME_EXE "ra3d/opencnc3d"
#define L_EDIT_EXE "cnc_eyes"
#endif

/* THE EDITOR IS A FLAG ON THE OTHER BINARY, not a third program. opencnc3d links the
 * same object file, but its entry point goes to the 1995 menu and never reads
 * --edit, so the button has to run cnc_eyes or it runs nothing. Both packages
 * already carried that binary before this button was live; what nothing checked
 * was that they had to.
 *
 * The map is named rather than left to cnc_eyes' own default, which is a Nod
 * campaign mission. This is the first GDI mission: the temperate donor the editor
 * reaches for when it is asked to make a new map, and the map the development app
 * has opened on since it was written. Everything else the editor needs it resolves
 * itself against the working directory l_launch moves to. */
#define L_EDIT_SCEN "SCG01EA"

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

static int l_canvas_w(void) { return ls_enabled() ? ls_width() : DM_SCREEN_W; }
static int l_canvas_h(void) { return ls_enabled() ? ls_height() : DM_SCREEN_H; }
static int l_native_x(int x) { return ls_enabled() ? 28 + x * 3 : x; }
static int l_native_y(int y) { return ls_enabled() ? 18 + y * 3 : y; }
static int l_native_extent(int n) { return ls_enabled() ? n * 3 : n; }

/* Indexed artwork remains original content; chrome never passes through this palette. */
static void l_skin_indexed(L_App *a, const unsigned char *pixels, int width, int height,
                           int dx, int dy, int sx, int sy, int sw, int sh, int dw, int dh)
{
    int x,y,c;
    if (!ls_enabled()) return;
    for (y=0;y<dh*3;y++) for(x=0;x<dw*3;x++) {
        int px=sx+x*sw/(dw*3), py=sy+y*sh/(dh*3);
        if(px<0||py<0||px>=width||py>=height) continue;
        c=pixels[py*width+px];
        if(c) ls_pixel_rect(28+dx*3+x,18+dy*3+y,1,1,
                 (a->pack->pal8[c*3]<<16)|(a->pack->pal8[c*3+1]<<8)|a->pack->pal8[c*3+2]);
    }
}

static void l_layout(L_App *a)
{
    int w = 0, h = 0, pw = 0, ph = 0, sc;
    SDL_GL_GetDrawableSize(a->win, &w, &h);
    SDL_GetWindowSize(a->win, &pw, &ph);
    a->px = pw > 0 ? (float)w / (float)pw : 1.0f;
    a->py = ph > 0 ? (float)h / (float)ph : 1.0f;
    if (w < l_canvas_w() || h < l_canvas_h()) {
        a->scale = 1;
    } else {
        sc = w / l_canvas_w();
        if (h / l_canvas_h() < sc)
            sc = h / l_canvas_h();
        a->scale = sc < 1 ? 1 : sc;
    }
    a->vpx = (w - l_canvas_w() * a->scale) / 2;
    a->vpy = (h - l_canvas_h() * a->scale) / 2;
}

/* Window points -> menu pixels. A click on the letterbox bars lands a long way
 * off the plate on purpose, so it cannot press an edge button. */
static void l_to_menu(const L_App *a, int wx, int wy, int *mx, int *my)
{
    /* NEVER DIVIDE BY THE SCALE WITHOUT LOOKING AT IT FIRST. l_layout is the only
       writer of a->scale and it runs inside l_present, so on the very first pass of
       the frame loop -- which polls events BEFORE anything is presented -- the scale
       is still the zero the memset left. An integer divide by zero is not a wrong
       answer on any machine this ships to; it is SIGFPE, and the app dies before it
       has drawn a pixel.

       That is the whole crash: double-clicking the app in Finder leaves the pointer
       exactly where the window opens, so SDL has a mouse motion queued before the
       first frame, and the launcher quit unexpectedly with no message. Starting it
       with the pointer elsewhere -- or over SSH, with no pointer at all -- ran
       perfectly, which is what made it look like a bad build rather than a bug.

       main now lays out once before the loop as well, so these coordinates are right
       on frame 0 rather than merely safe. This guard stays regardless: a divisor a
       reader has to prove non-zero by tracing two call sites is a divisor that will
       be zero again one day. */
    const int sc = a->scale > 0 ? a->scale : 1;
    int fx = (int)(wx * a->px), fy = (int)(wy * a->py);
    if (fx < a->vpx || fy < a->vpy) {
        *mx = *my = -1000;
        return;
    }
    *mx = (fx - a->vpx) / sc;
    *my = (fy - a->vpy) / sc;
    if (ls_enabled()) {
        if (*mx < 28 || *my < 18 || *mx >= 988 || *my >= 618) { *mx=*my=-1000; return; }
        *mx=(*mx-28)/3; *my=(*my-18)/3;
    }
}

static void l_present(L_App *a)
{
    float u, v;
    int w = 0, h = 0, y;

    if (ls_enabled()) memcpy(a->rgba, ls_pixels(), (size_t)l_canvas_w()*l_canvas_h()*4);
    else db_surface_to_rgba(&a->surf, a->pack->pal8, a->rgba, 0);
    for (y = 0; y < l_canvas_h(); y++)
        memcpy(a->padded + (long)y * a->tw * 4, a->rgba + (long)y * l_canvas_w() * 4,
               (size_t)l_canvas_w() * 4);
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

    glViewport(a->vpx, a->vpy, l_canvas_w() * a->scale, l_canvas_h() * a->scale);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    u = (float)l_canvas_w() / (float)a->tw;
    v = (float)l_canvas_h() / (float)a->th;
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
 * Movies and music: what the screens read.
 *
 * Every question goes through these few functions, so the --shot stand-ins answer
 * in exactly one place and a picture of the downloading screen is drawn by the
 * same code that draws a real one.
 * ------------------------------------------------------------------------ */

/* A picture's stand-in for the file being fetched. Only --shot prints it; a real
 * job's line is lmedia's own. */
#define L_SHOT_ITEM "MOVIES: GDI1.VQA (12 OF 75)"

/* Free space on the install's disk, for the line that says whether the download
 * fits: lpath's answer, the same one lmedia checks before it starts. -1 when the
 * system will not say, and the line then leaves it out. */
static long long l_free_bytes(const char *dir)
{
    long long n = -1;
    return lp_free_bytes(dir, &n) ? n : -1;
}

/* THE PHASE, BYTES, FILE AND REASON THE DOWNLOADING SCREEN SHOWS: a picture's
 * stand-ins, a first install's own account of itself (l_i_tick keeps it), or the
 * movies and music job's. */
static LM_Phase l_m_phase(L_App *a)
{
    if (a->shot_media)
        return a->shot_phase;
    if (a->i_job)
        return a->i_phase;
    return a->media ? lm_phase(a->media) : LM_IDLE;
}

static void l_m_bytes(L_App *a, long long *done, long long *total)
{
    *done = *total = 0;
    if (a->shot_media) {
        *done = a->shot_done;
        *total = a->shot_total;
    } else if (a->i_job) {
        *done = a->i_shown;
        *total = a->i_total;
    } else if (a->media)
        lm_bytes(a->media, done, total);
}

static void l_m_item(L_App *a, char *buf, int len)
{
    buf[0] = '\0';
    if (a->shot_media)
        snprintf(buf, (size_t)len, "%s", a->i_job ? "GAME: OPENCNC3D-WINDOWS-V0.8.0.ZIP" : L_SHOT_ITEM);
    else if (a->i_job)
        snprintf(buf, (size_t)len, "%s", a->i_item);
    else if (a->media)
        lm_item(a->media, buf, len);
}

static void l_m_reason(L_App *a, char *buf, int len)
{
    buf[0] = '\0';
    if (a->shot_media)
        snprintf(buf, (size_t)len, "%s", a->shot_reason);
    else if (a->i_job)
        snprintf(buf, (size_t)len, "%s", a->i_reason);
    else if (a->media)
        lm_status(a->media, buf, len);
}

/* A JOB IS IN HAND from lm_apply until the screen has seen it end, and while it is,
 * nothing that leaves this process or writes the install may start: Play and
 * Editor leave (a download abandoned half way), and Update rewrites the folder the
 * worker is writing into. */
static int l_m_busy(L_App *a)
{
    LM_Phase ph = l_m_phase(a);
    return a->m_running || ph == LM_FETCHING || ph == LM_CHECKING || ph == LM_REMOVING;
}

/* What the disk holds and what the record says, re-read whenever a screen opens or a
 * job ends. Three answers per component, because they differ exactly when it matters:
 * ALL of it there (nothing to download), ANY of it there (something an untick would
 * remove), and what the player last CHOSE, with whether the disk had got there. */
static void l_m_refresh(L_App *a)
{
    a->have_movies = a->have_music = 0;
    a->any_movies = a->any_music = 0;
    a->here_movies = a->here_music = 0;
    a->rec_ok = a->rec_movies = a->rec_music = 0;
    a->pend_movies = a->pend_music = 0;
    if (a->media) {
        lm_installed(a->media, &a->have_movies, &a->have_music);
        lm_present_any(a->media, &a->any_movies, &a->any_music, &a->here_movies,
                       &a->here_music);
        a->rec_ok = lm_wanted(a->media, &a->rec_movies, &a->rec_music, &a->pend_movies,
                              &a->pend_music);
    }
    a->free_bytes = l_free_bytes(a->dir);
}

/* What the ticks would download: each ticked component that is not all on the disk. A
 * component part way down still counts whole, so this is the most the job can need
 * rather than a guess at what is left. */
static long long l_m_need(const L_App *a)
{
    return (a->want_movies && !a->have_movies ? a->size_movies : 0) +
           (a->want_music && !a->have_music ? a->size_music : 0);
}

/* WHAT AN UNTICK WOULD TAKE AWAY: every unticked component with ANY of its files on the
 * disk, not only one that is all there. A download that stopped half way, or the
 * shorter score an older package carried, is not installed and is still the player's
 * to keep, so it is counted here, which is what makes the removal question name it.
 * Counting only complete components is how pressing Apply for the movies once deleted
 * a half-downloaded score without asking. */
static long long l_m_frees(const L_App *a)
{
    return (!a->want_movies && a->any_movies ? (a->here_movies > 0 ? a->here_movies : 1) : 0) +
           (!a->want_music && a->any_music ? (a->here_music > 0 ? a->here_music : 1) : 0);
}

/* Whether Apply has anything to do for one component: download what is not all there,
 * remove what is there and unticked, or finish what the record says was left pending. */
static int l_m_comp_changed(int want, int have, int any, int rec_ok, int rec_want, int pend)
{
    return (want && !have) || (!want && any) || (rec_ok && pend && want == rec_want);
}

static int l_m_changed(const L_App *a)
{
    return l_m_comp_changed(a->want_movies, a->have_movies, a->any_movies, a->rec_ok,
                            a->rec_movies, a->pend_movies) ||
           l_m_comp_changed(a->want_music, a->have_music, a->any_music, a->rec_ok,
                            a->rec_music, a->pend_music);
}

/* A job that downloads nothing and only removes. When one of those fails the files
 * are still there, so its screen speaks of a removal and offers the way back
 * rather than playing without them. */
static int l_m_job_removes_only(const L_App *a)
{
    return !(a->job_movies && !a->had_movies) && !(a->job_music && !a->had_music);
}

/* A size for the removal question and the line under the ticks: lmedia's, except
 * that a few files left by a download that barely started read as what they are
 * rather than as nothing at all. */
static void l_m_size(long long bytes, char *buf, int len)
{
    if (bytes > 0 && bytes < 104858)
        snprintf(buf, (size_t)len, "under 0.1 MB");
    else
        lm_format_size(bytes, buf, len);
}

/* The removal question, in the words the confirm line prints. */
static void l_m_question(const L_App *a, char *buf, int len)
{
    char sz[32];
    const int mo = !a->want_movies && a->any_movies;
    const int mu = !a->want_music && a->any_music;
    l_m_size(l_m_frees(a), sz, sizeof sz);
    snprintf(buf, (size_t)len, "Remove the %s, %s?",
             mo && mu ? "movies and music" : mo ? "movies" : "music", sz);
}

/* What a finished job did, for the module grid's status line. */
static void l_m_result(const L_App *a, char *buf, int len)
{
    const int add_mo = a->job_movies && !a->had_movies, add_mu = a->job_music && !a->had_music;
    const int rem_mo = !a->job_movies && a->had_any_movies;
    const int rem_mu = !a->job_music && a->had_any_music;
    char added[64], removed[64];

    added[0] = removed[0] = '\0';
    if (add_mo || add_mu)
        snprintf(added, sizeof added, "%s installed.",
                 add_mo && add_mu ? "Movies and music" : add_mo ? "Movies" : "Music");
    if (rem_mo || rem_mu)
        snprintf(removed, sizeof removed, "%s removed.",
                 rem_mo && rem_mu ? "Movies and music" : rem_mo ? "Movies" : "Music");

    if (added[0] || removed[0])
        snprintf(buf, (size_t)len, "%s%s%s", added, added[0] && removed[0] ? " " : "",
                 removed);
    else if (a->job_movies || a->job_music)
        snprintf(buf, (size_t)len, "%s installed.",
                 a->job_movies && a->job_music ? "Movies and music"
                 : a->job_movies              ? "Movies"
                                              : "Music");
    else
        snprintf(buf, (size_t)len, "No movies or music. The gear adds them whenever you like.");
}

/* THE START-UP LINE FOR A RECORD LEFT PENDING: a download that was stopped, failed or
 * killed, or a removal that was refused, in an earlier run. Said once on the module
 * grid, with where to finish it, rather than left for the player to find in Settings.
 * Empty when there is nothing pending. */
static void l_m_pending_line(const L_App *a, char *buf, int len)
{
    const int dmo = a->rec_ok && a->pend_movies && a->rec_movies;
    const int dmu = a->rec_ok && a->pend_music && a->rec_music;
    const int rmo = a->rec_ok && a->pend_movies && !a->rec_movies;
    const int rmu = a->rec_ok && a->pend_music && !a->rec_music;
    char down[96], rem[96];

    buf[0] = down[0] = rem[0] = '\0';
    if (dmo || dmu)
        snprintf(down, sizeof down, "%s did not finish downloading",
                 dmo && dmu ? "The movies and music" : dmo ? "The movies" : "The music");
    if (rmo || rmu)
        snprintf(rem, sizeof rem, "%s not all removed",
                 rmo && rmu ? "The movies and music were" : rmo ? "The movies were"
                                                                 : "The music was");
    if (down[0] && rem[0])
        snprintf(buf, (size_t)len, "%s, and %c%s. The gear finishes both.", down,
                 rem[0] + ('a' - 'A'), rem + 1);
    else if (down[0] || rem[0])
        snprintf(buf, (size_t)len, "%s. The gear finishes it.", down[0] ? down : rem);
}

/* A checkbox row's right hand column: the size a tick will download, or what the
 * disk already holds, or what the untick will do to it. */
static const char *l_m_note(L_App *a, int which)
{
    const int have = which ? a->have_music : a->have_movies;
    const int any = which ? a->any_music : a->any_movies;
    const int want = which ? a->want_music : a->want_movies;
    char sz[32];
    lm_format_size(which ? a->size_music : a->size_movies, sz, sizeof sz);
    if (want && have)
        snprintf(a->m_note[which], sizeof a->m_note[which], "%s", "INSTALLED");
    else if (!want && any)
        snprintf(a->m_note[which], sizeof a->m_note[which], "%s", "WILL BE REMOVED");
    else if (a->view == L_VIEW_SETTINGS)
        snprintf(a->m_note[which], sizeof a->m_note[which], "%s, %s",
                 want ? "WILL BE DOWNLOADED" : "NOT INSTALLED", sz);
    else
        snprintf(a->m_note[which], sizeof a->m_note[which], "%s", sz);
    return a->m_note[which];
}

/* ------------------------------------------------------------------------ *
 * The screen.
 * ------------------------------------------------------------------------ */

static void l_set_buttons(L_App *a)
{
    LU_Phase ph = a->up ? lu_phase(a->up) : LU_NOTCONFIGURED;
    const int updating = ph == LU_DOWNLOADING || ph == LU_APPLYING;
    const int busy = l_m_busy(a);
    const int form = a->view == L_VIEW_SETUP || a->view == L_VIEW_SETTINGS ||
                     a->view == L_VIEW_INSTALL;
    int i;
    static const char *labels[L_BTN_COUNT] = {"Play", "Changelog", "Editor", "Quit",
                                              "Close", "", "", ""};

    for (i = 0; i < L_BTN_COUNT; i++) {
        a->btn[i].x = L_BTN_X0 + i * L_BTN_STEP;
        a->btn[i].y = L_BTN_Y;
        a->btn[i].w = L_BTN_W;
        a->btn[i].h = L_BTN_H;
        a->btn[i].label = labels[i];
        a->btn[i].disabled = 0;
        a->btn[i].hidden = 0;
        a->btn[i].kind = LUI_KIND_BUTTON;
        a->btn[i].checked = 0;
        a->btn[i].note = NULL;
    }

    /* The two ticks. The same rows on Setup and on Settings; only the words
     * differ, because Setup is asking whether to download and Settings is saying
     * what is there. Frozen while a removal waits on its question, so the answer
     * is about the ticks the question names. */
    for (i = 0; i < 2; i++) {
        LUI_Button *c = &a->btn[L_BTN_CHK_MOVIES + i];
        c->kind = LUI_KIND_CHECK;
        c->x = L_CHK_X;
        c->y = L_CHK_Y0 + i * L_CHK_STEP;
        c->w = L_CHK_W;
        c->h = L_CHK_H;
        c->hidden = !form;
        c->disabled = a->m_confirm;
        c->checked = i ? a->want_music : a->want_movies;
        if (a->view == L_VIEW_SETTINGS)
            c->label = i ? "MUSIC" : "MOVIES";
        else
            c->label = i ? "DOWNLOAD MUSIC" : "DOWNLOAD MOVIES";
        c->note = form ? l_m_note(a, i) : NULL;
    }

    /* The gear: on the module grid only, and not while either worker is busy, so
     * an update and a media job never run at once. */
    a->btn[L_BTN_GEAR].kind = LUI_KIND_GEAR;
    a->btn[L_BTN_GEAR].label = "Settings";
    a->btn[L_BTN_GEAR].x = L_GEAR_X;
    a->btn[L_BTN_GEAR].y = L_GEAR_Y;
    a->btn[L_BTN_GEAR].w = L_GEAR_W;
    a->btn[L_BTN_GEAR].h = L_GEAR_H;
    a->btn[L_BTN_GEAR].hidden = a->view != L_VIEW_MODULES;
    a->btn[L_BTN_GEAR].disabled = updating || busy || !a->media;

    /* Close is not in the row: it belongs to the browser and stands in it. */
    a->btn[L_BTN_CLOSE].x = L_PANEL_X + (L_PANEL_W - L_CLOSE_W) / 2;
    a->btn[L_BTN_CLOSE].y = L_CLOSE_Y;
    a->btn[L_BTN_CLOSE].w = L_CLOSE_W;
    a->btn[L_BTN_CLOSE].hidden = a->view != L_VIEW_BROWSE;

    /* When a new version is available the Play button says Update. One button, two
     * jobs, and the label is the only place the difference shows: a second button
     * that is usually inert would make the common case look like the exceptional
     * one. The Changelog button works the same way, because it is the way back as
     * well as the way in, and a second button to leave a screen with one thing on
     * it is furniture. */
    if (ph == LU_AVAILABLE)
        a->btn[L_BTN_PLAY].label = "Update";
    else if (ph == LU_DOWNLOADING || ph == LU_APPLYING)
        a->btn[L_BTN_PLAY].label = "Updating";
    else if (ph == LU_DONE)
        a->btn[L_BTN_PLAY].label = a->mod_sel == 1 ? "Launch" : "Play";
    else if (a->mod_sel == 1)
        a->btn[L_BTN_PLAY].label = "Launch";

    /* The map editor still knows TD theaters and objects only. */
    if (a->mod_sel == 1)
        a->btn[L_BTN_EDITOR].disabled = 1;

    if (a->view == L_VIEW_NOTES)
        a->btn[L_BTN_NOTES].label = "Modules";

    /* THE EDITOR BUTTON IS LIVE, and the enable is not the whole of the change.
     * It was drawn disabled because the editor existed in the tree and not in a
     * package, and an enabled button over a package with no editor in it is worse
     * than a grey one: it fails at the moment it is pressed, in front of the
     * player, with no way back. So the Windows packager and the release script now
     * refuse a build that does not carry the editor binary and the maps this opens,
     * under the gate id for this change. The enable and that refusal are one change
     * and must stay one.
     *
     * Nothing is clickable while a download is in flight except Quit, which stays
     * live on purpose: a player who wants out during a 500 MB transfer should not
     * have to kill the process. THE EDITOR BELONGS IN THAT LIST, and it did not
     * need to while it was disabled: pressing it LEAVES this process, execing on
     * POSIX and exiting on Windows, so a press mid-download kills the transfer and
     * there is nothing left running to say what happened to it. */
    if (ph == LU_DOWNLOADING || ph == LU_APPLYING) {
        a->btn[L_BTN_PLAY].disabled = 1;
        a->btn[L_BTN_EDITOR].disabled = 1;
    }

    /* AND THE SAME WHILE A MEDIA JOB RUNS, for the same two reasons plus a third:
     * Play and Editor leave this process with the worker half way through a file,
     * and Update would rewrite the folder it is writing into. The job's own screen
     * hides them anyway; this is the rule for any screen that shows them. */
    if (busy) {
        a->btn[L_BTN_PLAY].disabled = 1;
        a->btn[L_BTN_EDITOR].disabled = 1;
    }

    /* THE MOVIES AND MUSIC SCREENS USE THE SAME FOUR SLOTS, relabelled, the way Play
     * becomes Update: the first slot is the thing to do and the last is the way
     * out, and the two between are hidden rather than greyed, because nothing that
     * belongs to them exists on these screens. */
    if (form || a->view == L_VIEW_MEDIA_RUN) {
        for (i = 0; i < L_BTN_ROW; i++) {
            a->btn[i].disabled = 0;
            a->btn[i].hidden = (i == L_BTN_NOTES || i == L_BTN_EDITOR);
        }
    }
    if (form) {
        if (a->m_confirm) {
            a->btn[L_BTN_PLAY].label = "Remove";
            a->btn[L_BTN_QUIT].label = "Keep";
        } else if (a->view == L_VIEW_SETUP) {
            a->btn[L_BTN_PLAY].label =
                (a->want_movies || a->want_music) ? "Install" : "Continue";
            a->btn[L_BTN_QUIT].label = "Quit";
        } else if (a->view == L_VIEW_INSTALL) {
            a->btn[L_BTN_PLAY].label = "Install"; /* the game, whatever the ticks say */
            a->btn[L_BTN_QUIT].label = "Quit";
        } else {
            a->btn[L_BTN_PLAY].label = "Apply";
            a->btn[L_BTN_PLAY].disabled = !l_m_changed(a);
            a->btn[L_BTN_QUIT].label = "Back";
        }
    } else if (a->view == L_VIEW_MEDIA_RUN) {
        if (l_m_phase(a) == LM_FAILED && !a->m_running) {
            /* Stopped by a failure: try again (lmedia keeps what arrived), start the
             * game without them, or leave. A first install that stopped before the
             * game was in has no game to play without them, so that slot stays
             * hidden until there is one. */
            a->btn[L_BTN_PLAY].label = "Retry";
            a->btn[L_BTN_NOTES].hidden = a->i_job && a->install_offer;
            if (!a->i_job && l_m_job_removes_only(a))
                a->btn[L_BTN_NOTES].label = "Back";
            else {
                a->btn[L_BTN_NOTES].label = "Play without";
                /* Two slots wide: the words overran one in the --shot, and the
                 * slot beside it is empty on this screen. */
                a->btn[L_BTN_NOTES].w = 2 * L_BTN_W + (L_BTN_STEP - L_BTN_W);
            }
            a->btn[L_BTN_QUIT].label = "Quit";
        } else {
            a->btn[L_BTN_PLAY].hidden = 1;
            a->btn[L_BTN_QUIT].label = "Cancel";
            a->btn[L_BTN_QUIT].disabled = a->m_cancelling;
        }
    }
}

/* THE HIGHLIGHT NEVER SITS ON A CONTROL THE SCREEN DOES NOT SHOW. When a screen
 * changes, or a slot is hidden or greyed under it, it moves to the first control
 * the screen offers in the order a player reads it: the first tick, then the row. */
static void l_fix_hot(L_App *a)
{
    static const int order[] = {L_BTN_CHK_MOVIES, L_BTN_CHK_MUSIC, L_BTN_PLAY,
                                L_BTN_NOTES,      L_BTN_EDITOR,    L_BTN_QUIT,
                                L_BTN_CLOSE,      L_BTN_GEAR};
    int i;
    if (a->hot >= 0 && a->hot < L_BTN_COUNT && !a->btn[a->hot].hidden &&
        !a->btn[a->hot].disabled)
        return;
    for (i = 0; i < (int)(sizeof order / sizeof *order); i++)
        if (!a->btn[order[i]].hidden && !a->btn[order[i]].disabled) {
            a->hot = order[i];
            return;
        }
}

/* The well's screens, and the one place the highlight is put back when one of
 * them goes away. Leaving the module grid takes the highlight off a tile that is
 * no longer drawn, and leaving the browser takes it off a Close button that is no
 * longer there: either would leave Return pressing something invisible. A screen
 * whose slot under the highlight is hidden or greyed moves it to what that screen
 * does offer (l_fix_hot).
 *
 * It rebuilds the buttons rather than waiting for the next pass of the loop, which
 * runs BEFORE the events it will answer: without this the frame that closed the
 * browser still drew the browser's own Close button, over the grid. One frame, and
 * exactly the kind of flicker that is never reported clearly enough to find. */
static void l_set_view(L_App *a, int view)
{
    a->view = view;
    a->mod_pressed = -1;
    a->pressed = -1;
    if (view != L_VIEW_MODULES)
        a->on_mods = 0;
    if (view != L_VIEW_BROWSE && a->hot == L_BTN_CLOSE)
        a->hot = L_BTN_PLAY;
    l_set_buttons(a);
    l_fix_hot(a);
}

/* ------------------------------------------------------------------------ *
 * Movies and music: the job.
 * ------------------------------------------------------------------------ */

/* Setup: both boxes ticked, because the 1995 game had both and a player who
 * presses Install without reading should get the whole of it. */
static void l_open_setup(L_App *a)
{
    l_m_refresh(a);
    a->want_movies = a->want_music = 1;
    a->m_confirm = 0;
    a->mstatus[0] = '\0';
    a->hot = L_BTN_PLAY;
    l_set_view(a, L_VIEW_SETUP);
}

/* SETTINGS: THE BOXES START AS WHAT THE PLAYER LEFT, never as a reading that could
 * leave one unticked behind the player's back. A component the record says is still
 * pending is ticked as the player chose it, because the disk has not caught up with
 * that choice yet. Otherwise each box shows whether ANY of its component is on the
 * disk: a half-finished download, or the shorter score an older package carried, is
 * ticked, so Apply pressed for the other box keeps it rather than removing it. Apply
 * with nothing to do is drawn disabled. */
static void l_open_settings(L_App *a)
{
    l_m_refresh(a);
    a->want_movies = (a->rec_ok && a->pend_movies) ? a->rec_movies : a->any_movies;
    a->want_music = (a->rec_ok && a->pend_music) ? a->rec_music : a->any_music;
    a->m_confirm = 0;
    a->mstatus[0] = '\0';
    a->hot = L_BTN_CHK_MOVIES;
    l_set_view(a, L_VIEW_SETTINGS);
}

/* INSTALL: the first screen of a folder that holds the launcher and not the game. The
 * same two ticks as Setup, both on, with Install putting the game in first. */
static void l_open_install(L_App *a)
{
    l_m_refresh(a);
    a->want_movies = a->want_music = 1;
    a->m_confirm = 0;
    a->hot = L_BTN_PLAY;
    l_set_view(a, L_VIEW_INSTALL);
}

/* Hand the ticks to lmedia. A refusal is lmedia's to explain, in its own words,
 * and the screen stays where it is with that reason under the well. */
static void l_m_start(L_App *a)
{
    char why[256];
    if (!a->media)
        return;
    if (a->view == L_VIEW_SETUP || a->view == L_VIEW_SETTINGS)
        a->m_from = a->view;
    a->job_movies = a->want_movies;
    a->job_music = a->want_music;
    a->had_movies = a->have_movies;
    a->had_music = a->have_music;
    a->had_any_movies = a->any_movies;
    a->had_any_music = a->any_music;
    a->m_confirm = 0;
    a->mstatus[0] = '\0';
    if (!lm_apply(a->media, a->job_movies, a->job_music)) {
        lm_status(a->media, why, sizeof why);
        snprintf(a->mstatus, sizeof a->mstatus, "%s",
                 why[0] ? why : "The download could not start.");
        l_set_buttons(a);
        return;
    }
    a->m_running = 1;
    a->m_cancelling = 0;
    a->hot = L_BTN_QUIT;
    l_set_view(a, L_VIEW_MEDIA_RUN);
}

/* Install, Continue or Apply. Anything that removes files asks first; anything
 * that only adds goes straight on. */
static void l_i_start(L_App *a);

static void l_m_request(L_App *a)
{
    if (a->view == L_VIEW_INSTALL) {
        l_i_start(a); /* nothing of the game is there to remove */
        return;
    }
    if (l_m_frees(a) > 0) {
        a->m_confirm = 1;
        l_m_question(a, a->mstatus, sizeof a->mstatus);
        a->hot = L_BTN_QUIT; /* Keep: the answer that changes nothing is the default */
        l_set_buttons(a);
        return;
    }
    l_m_start(a);
}

/* ------------------------------------------------------------------------ *
 * The first install, from the window.
 *
 * THE --install-game JOB, DRIVEN FROM THE SCREEN: ask the site for the newest build,
 * download its full package with resume and unpack it over the folder (lupdate's
 * first-install state, which locks a folder that has no install record yet), then
 * the movies and music job, under that same lock, with the ticks as they stand,
 * both unticked included, so the choice is recorded and Setup does not ask again.
 * One bar for the whole of it, the way the headless mode counts it: the package and
 * the movies and music summed before the first byte, held where it got to while the
 * bytes catch up, never stepping back.
 * ------------------------------------------------------------------------ */

#ifdef _WIN32
#define L_SHOT_GAME_ITEM "GAME: OPENCNC3D-WINDOWS-V0.8.0.ZIP"
#else
#define L_SHOT_GAME_ITEM "GAME: OPENCNC3D-MACOS-V0.8.0.ZIP"
#endif

static void l_i_show(L_App *a, long long done, long long total)
{
    if (total > 0 && done > total)
        done = total;
    a->i_total = total;
    if (done > a->i_shown)
        a->i_shown = done;
    if (a->i_total > 0 && a->i_shown > a->i_total)
        a->i_shown = a->i_total;
}

static void l_i_start(L_App *a)
{
    if (!a->media)
        return;
    lu_destroy(a->fresh);
    a->fresh = lu_create_fresh(a->dir);
    if (!a->fresh) {
        snprintf(a->mstatus, sizeof a->mstatus, "The install could not start.");
        return;
    }
    a->m_from = L_VIEW_INSTALL;
    a->job_movies = a->want_movies;
    a->job_music = a->want_music;
    a->had_movies = a->have_movies;
    a->had_music = a->have_music;
    a->had_any_movies = a->any_movies;
    a->had_any_music = a->any_music;
    a->i_estimate = l_m_need(a);
    a->i_game = 0;
    a->i_shown = 0;
    a->i_total = 0;
    a->i_stage = 0;
    a->i_job = 1;
    a->i_phase = LM_FETCHING;
    a->i_reason[0] = '\0';
    snprintf(a->i_item, sizeof a->i_item, "GAME: ASKING OPENCNC3D.COM FOR THE NEWEST BUILD");
    a->m_confirm = 0;
    a->mstatus[0] = '\0';
    lu_check(a->fresh);
    a->m_running = 1;
    a->m_cancelling = 0;
    a->hot = L_BTN_QUIT;
    l_set_view(a, L_VIEW_MEDIA_RUN);
}

/* THE FOLDER IS AN INSTALL NOW: the window carries on as if it had been started in
 * one. The updater state that could only look is replaced by one that holds the lock
 * an install gets, the version is read off the new record, and the site is asked
 * again, so the two numbers at the top are this install's. From here a movies and
 * music job that is still to finish is the ordinary one, from Setup's screen. */
static void l_read_installed(L_App *a);

static void l_i_promote(L_App *a)
{
    lu_destroy(a->up);
    a->up = lu_create(a->dir, NULL);
    l_read_installed(a);
    lu_set_installed(a->up, a->version);
    lu_check(a->up);
    a->install_offer = 0;
    a->m_from = L_VIEW_SETUP;
}

/* The job's end, either way. The first install's state goes, and with it the lock it
 * held: a Retry makes a new one, and resumes what this one kept. If the game got in
 * before the job stopped, the window becomes an install's. */
static void l_i_stop(L_App *a, LM_Phase how, const char *reason)
{
    a->m_running = 0;
    a->i_phase = how;
    snprintf(a->i_reason, sizeof a->i_reason, "%s", reason ? reason : "");
    lu_destroy(a->fresh);
    a->fresh = NULL;
    if (a->install_offer && lz_is_install(a->dir))
        l_i_promote(a);
    l_m_refresh(a);
}

static void l_i_done(L_App *a)
{
    char media[160];
    l_i_stop(a, LM_DONE, "");
    a->i_job = 0;
    l_m_result(a, media, sizeof media);
    snprintf(a->mstatus, sizeof a->mstatus, "OpenCNC3D v%s is installed. %s", a->version, media);
    a->hot = L_BTN_PLAY;
    l_set_view(a, L_VIEW_MODULES);
}

static void l_i_tick(L_App *a)
{
    char name[300], why[256];
    long long done = 0, total = 0;
    LU_Phase up;
    LM_Phase mp;

    if (!a->i_job || !a->m_running || !a->fresh)
        return;
    if (a->i_stage == 0) {
        up = lu_phase(a->fresh);
        if (up == LU_CHECKING || up == LU_IDLE)
            return;
        if (a->m_cancelling) {
            l_i_stop(a, LM_CANCELLED, "");
            return;
        }
        if (up == LU_FAILED) {
            lu_status(a->fresh, why, sizeof why);
            l_i_stop(a, LM_FAILED, why);
            return;
        }
        if (up == LU_AVAILABLE) {
            lu_apply(a->fresh);
            a->i_stage = 1;
        } else {
            a->i_stage = 2; /* the folder holds the newest build already */
        }
    }
    if (a->i_stage == 1) {
        up = lu_phase(a->fresh);
        lu_bytes(a->fresh, &done, &total);
        lu_download_name(a->fresh, name, sizeof name);
        if (total > 0)
            a->i_game = total;
        if (up == LU_DOWNLOADING) {
            a->i_phase = LM_FETCHING;
            snprintf(a->i_item, sizeof a->i_item, "GAME: %.140s", name);
            l_i_show(a, done, (total > 0 ? total : 0) + a->i_estimate);
            return;
        }
        if (up == LU_APPLYING) {
            int p = lu_progress(a->fresh);
            a->i_phase = LM_CHECKING;
            if (p >= 0)
                snprintf(a->i_item, sizeof a->i_item, "GAME: INSTALLING %.120s (%d%%)", name,
                         p / 10);
            else
                snprintf(a->i_item, sizeof a->i_item, "GAME: CHECKING %.130s", name);
            l_i_show(a, a->i_game, a->i_game + a->i_estimate);
            return;
        }
        if (up == LU_FAILED) {
            lu_status(a->fresh, why, sizeof why);
            l_i_stop(a, a->m_cancelling ? LM_CANCELLED : LM_FAILED, why);
            return;
        }
        if (up != LU_DONE)
            return;
        /* The game is in. A Cancel that came too late for the unpacking to hear it
         * still stops the job here, before the movies and music start. */
        if (a->m_cancelling) {
            l_i_stop(a, LM_CANCELLED, "");
            return;
        }
        a->i_stage = 2;
    }
    if (a->i_stage == 2) {
        /* The movies and music, with the ticks as they stood when Install was pressed. */
        if (!lm_apply(a->media, a->job_movies, a->job_music)) {
            lm_status(a->media, why, sizeof why);
            l_i_stop(a, LM_FAILED, why[0] ? why : "the movies and music could not start");
            return;
        }
        a->i_stage = 3;
    }
    mp = lm_phase(a->media);
    lm_bytes(a->media, &done, &total);
    lm_item(a->media, a->i_item, sizeof a->i_item);
    if (mp == LM_FETCHING || mp == LM_CHECKING || mp == LM_REMOVING) {
        a->i_phase = mp;
        l_i_show(a, a->i_game + done, a->i_game + (total > 0 ? total : a->i_estimate));
        return;
    }
    if (mp == LM_DONE) {
        l_i_done(a);
        return;
    }
    lm_status(a->media, why, sizeof why);
    l_i_stop(a, (mp == LM_CANCELLED || a->m_cancelling) ? LM_CANCELLED : LM_FAILED, why);
}

/* Once a frame: notice the end of a job and leave its screen for the right one. */
static void l_m_tick(L_App *a)
{
    LM_Phase ph;
    if (a->i_job) {
        const int was = a->m_running;
        l_i_tick(a);
        /* Ended, and not by finishing (l_i_done has already left the screen). */
        if (was && !a->m_running && a->i_job) {
            if (a->i_phase == LM_CANCELLED) {
                a->i_job = 0;
                a->want_movies = a->job_movies;
                a->want_music = a->job_music;
                snprintf(a->mstatus, sizeof a->mstatus,
                         "Download stopped. What arrived is kept for the next try.");
                l_set_view(a, a->install_offer ? L_VIEW_INSTALL : L_VIEW_SETUP);
            } else {
                a->hot = L_BTN_PLAY; /* Retry */
                l_set_view(a, L_VIEW_MEDIA_RUN);
            }
        }
        return;
    }
    if (!a->m_running || !a->media)
        return;
    ph = lm_phase(a->media);
    if (ph == LM_DONE) {
        a->m_running = 0;
        l_m_refresh(a);
        l_m_result(a, a->mstatus, sizeof a->mstatus);
        a->hot = L_BTN_PLAY; /* done: the next thing is to play */
        l_set_view(a, L_VIEW_MODULES);
    } else if (ph == LM_CANCELLED || (ph == LM_FAILED && a->m_cancelling)) {
        a->m_running = 0;
        l_m_refresh(a);
        a->want_movies = a->job_movies;
        a->want_music = a->job_music;
        snprintf(a->mstatus, sizeof a->mstatus,
                 "Download stopped. What arrived is kept for the next try.");
        l_set_view(a, a->m_from == L_VIEW_SETTINGS ? L_VIEW_SETTINGS : L_VIEW_SETUP);
    } else if (ph == LM_FAILED) {
        a->m_running = 0;
        l_m_refresh(a);
        a->hot = L_BTN_PLAY; /* Retry */
        l_set_view(a, L_VIEW_MEDIA_RUN);
    }
}


/* ------------------------------------------------------------------------ *
 * The module grid.
 * ------------------------------------------------------------------------ */

static void l_mod_rect(int i, int *x, int *y, int *w, int *h)
{
    *x = L_MOD_X0 + i * L_MOD_STEP;
    *y = L_MOD_Y;
    *w = L_MOD_W;
    *h = L_MOD_H;
}

/* The tile under a pixel, or -1. Only on the grid screen: the tiles are not drawn
 * on the other two, and a control that cannot be seen cannot be pressed. */
static int l_mod_hit(const L_App *a, int mx, int my)
{
    int i, x, y, w, h;
    if (a->view != L_VIEW_MODULES)
        return -1;
    for (i = 0; i < L_MOD_COUNT; i++) {
        l_mod_rect(i, &x, &y, &w, &h);
        if (mx >= x && mx < x + w && my >= y && my < y + h)
            return i;
    }
    return -1;
}

static void l_draw_module(L_App *a, int i)
{
    const L_Module *m = &a->mod[i];
    int x, y, w, h, ex, ey;
    int hot = a->on_mods && a->mod_hot == i;

    l_mod_rect(i, &x, &y, &w, &h);

    if (m->installed) {
        /* Enhanced module emblems keep full-colour RGBA cutouts over the plate.
         * The standalone Classic fallback retains its indexed title emblems. */
        const DB_Shape *title = i == 1 ? NULL : db_shape(a->pack, "TITLE");
        if (ls_enabled()) ls_button(x,y,w,h,a->mod_pressed==i,hot,0,a->mod_sel==i);
        else dm_green_button(&a->surf, x, y, w, h, a->mod_pressed == i, 0);
        if (i == 0 && ls_enabled()) {
            ls_rgba_image(launcher_module_rgba,L_MODULE_LOGO_W,L_MODULE_LOGO_H,
                          l_native_x(x)+(w*3-L_MODULE_LOGO_W)/2,
                          l_native_y(y)+(h*3-L_MODULE_LOGO_H)/2);
        } else if (i == 1 && ls_enabled()) {
            ls_rgba_image(launcher_ra_rgba,L_RA_LOGO_W,L_RA_LOGO_H,
                          l_native_x(x)+(w*3-L_RA_LOGO_W)/2,
                          l_native_y(y)+(h*3-L_RA_LOGO_H)/2);
        } else if (i == 1) {
            const int left = x + (w - 48) / 2, top = y + 5;
            if (ls_enabled()) l_skin_indexed(a,ra_emblem_pixels,48,48,left,top,0,0,48,48,48,48);
            for (ey = 0; ey < 48; ey++)
                for (ex = 0; ex < 48; ex++) {
                    const unsigned char pixel = ra_emblem_pixels[ey * 48 + ex];
                    if (pixel && left + ex >= 0 && left + ex < a->surf.w &&
                        top + ey >= 0 && top + ey < a->surf.h)
                        a->surf.px[(top + ey) * a->surf.w + left + ex] = pixel;
                }
        } else if (title) {
            l_skin_indexed(a,title->pixels,title->w,title->h,x+(w-L_LOGO_W)/2,y+5,
                           L_EMBLEM_X,L_EMBLEM_Y,L_EMBLEM_W,L_EMBLEM_H,L_LOGO_W,L_LOGO_H);
            lui_blit_scaled(&a->surf, title, 0, L_EMBLEM_X, L_EMBLEM_Y, L_EMBLEM_W,
                            L_EMBLEM_H, x + (w - L_LOGO_W) / 2, y + 5,
                            L_LOGO_W, L_LOGO_H);
        }
        lui_print_centered(&a->surf, a->f6, m->name, x + w / 2, L_MOD_NAME_Y,
                           a->mod_sel == i ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM);
    } else {
        /* THE ENGINE'S DISABLED COLOURWAY, the same grey slab the Editor button was
         * drawn in while there was no editor: BOXSTYLE_GREEN_DIS_RAISED and a flat
         * CC_GREEN label, not a dimmed green button. An empty slot says what it is
         * and says when, which is the whole of what it can honestly say. */
        if (ls_enabled()) ls_button(x,y,w,h,0,hot,1,0);
        else dm_green_button(&a->surf, x, y, w, h, 0, 1);
        lui_plus(&a->surf, x + w / 2, y + L_MOD_PLUS_DY, L_MOD_PLUS_ARM,
                 L_MOD_PLUS_THICK, DM_DIS_HILITE);
        lui_print_centered(&a->surf, a->f6, "Coming soon", x + w / 2,
                           y + L_MOD_SOON_DY, DM_TEXT_DISABLED);
    }

    /* THE TILE YOU ARE IN IS RINGED, and so is the tile the highlight is on. A
     * picture tile has no label to brighten the way the engine brightens a live
     * gadget's text, so the frame carries it: light green for the module you are
     * in, the brighter text green when the highlight is on it. */
    if (a->mod_sel == i || hot)
        lui_outline(&a->surf, x + 2, y + 2, w - 4, h - 4,
                    hot ? DM_TEXT_BRIGHT : DM_LIGHT_GREEN);
}

static void l_draw_modules(L_App *a)
{
    char line[64];
    int i, n = 0;

    for (i = 0; i < L_MOD_COUNT; i++)
        if (a->mod[i].installed)
            n++;

    lui_print(&a->surf, a->f6, "MODULES", L_PANEL_X + 8, L_HEAD_Y, DM_TEXT_MEDIUM);
    snprintf(line, sizeof line, "%d OF %d", n, L_MOD_COUNT);
    lui_print_right(&a->surf, a->f6, line, L_MOD_COUNT_RX, L_HEAD_Y, DM_TEXT_MEDIUM);

    for (i = 0; i < L_MOD_COUNT; i++)
        l_draw_module(a, i);
}

/* The browser, with the list it will one day fill. It is drawn as an empty list
 * rather than as a message on the plate because empty is a state of the list, and
 * the screen has to look the same the day there is something in it. */
static void l_draw_browser(L_App *a)
{
    int cx = L_PANEL_X + L_PANEL_W / 2;

    lui_print(&a->surf, a->f6, "MODULE BROWSER", L_PANEL_X + 8, L_HEAD_Y,
              DM_TEXT_MEDIUM);
    lui_panel(&a->surf, L_PANEL_X + 8, L_BROWSE_LIST_Y, L_PANEL_W - 16,
              L_BROWSE_LIST_H);
    lui_print_centered(&a->surf, a->f6, "No modules are available yet.", cx,
                       L_PANEL_Y + 38, DM_TEXT_BRIGHT);
    lui_print_centered(&a->surf, a->f6, "A module is another game on the OpenCNC3D",
                       cx, L_PANEL_Y + 52, DM_TEXT_MEDIUM);
    lui_print_centered(&a->surf, a->f6, "engine, and it plays across to the others.",
                       cx, L_PANEL_Y + 60, DM_TEXT_MEDIUM);
}

/* Setup and Settings: the heading, what this is, the two ticks, and whether it
 * fits on the disk. The ticks themselves are rows of the button table and are
 * drawn with the rest of it. */
static void l_draw_media_form(L_App *a)
{
    char need[32], fre[64], line[96];
    const int install = a->view == L_VIEW_INSTALL;
    const long long game = install && a->up ? lu_package_size(a->up) : 0;
    const long long n = l_m_need(a) + (game > 0 ? game : 0), frees = l_m_frees(a);
    const int short_of_room = a->free_bytes >= 0 && n > a->free_bytes;
    const int setup = a->view == L_VIEW_SETUP;

    /* WHERE THEY COME FROM, said plainly on every screen that downloads them: the
     * movies from the Gold discs EA gave away in 2007, and the music from the Covert
     * Operations disc, which is the only one that carries the whole score. */
    lui_print(&a->surf, a->f6,
              install ? "INSTALL OPENCNC3D" : setup ? "MOVIES AND MUSIC" : "SETTINGS",
              L_PANEL_X + 8, L_HEAD_Y, DM_TEXT_MEDIUM);
    if (!setup)
        lui_print_right(&a->surf, a->f6, install ? "NOT INSTALLED YET" : "MOVIES AND MUSIC",
                        L_PANEL_X + L_PANEL_W - 8, L_HEAD_Y, DM_TEXT_MEDIUM);

    lui_print_wrapped(&a->surf, a->f6,
                      install ? "This folder holds the launcher but not the game. Install "
                                "downloads the newest build from opencnc3d.com, then the "
                                "movies and music ticked below."
                      : setup ? "Movies from the 1995 Gold discs, EA's 2007 freeware "
                                "release. Music from the Covert Operations disc. Add them "
                                "now, or later with the gear."
                              : "Movies from the 1995 Gold discs, EA's 2007 freeware "
                                "release, music from the Covert Operations disc. Tick to "
                                "download, untick to remove.",
                      L_MEDIA_TEXT_X, L_MEDIA_TEXT_Y, L_MEDIA_TEXT_W, DM_TEXT_MEDIUM, 0, 3);

    /* The space line sits under the two rows, its two halves on the rows' own
     * columns: what the ticks need under the words, what is free under the sizes. A
     * first install counts the game's package in, once the site has said its size. */
    if (install && game < 0) {
        lm_format_size(l_m_need(a), need, sizeof need);
        snprintf(line, sizeof line, "DOWNLOAD THE GAME + %s", need);
    } else if (install) {
        lm_format_size(n, need, sizeof need);
        snprintf(line, sizeof line, "DOWNLOAD %s", need);
    } else if (n > 0) {
        lm_format_size(n, need, sizeof need);
        snprintf(line, sizeof line, "SPACE NEEDED %s", need);
    } else if (frees > 0) {
        l_m_size(frees, need, sizeof need);
        snprintf(line, sizeof line, "REMOVING FREES %s", need);
    } else
        snprintf(line, sizeof line, "NOTHING TO DOWNLOAD");
    lui_print(&a->surf, a->f6, line, L_CHK_X, L_SPACE_Y, DM_TEXT_MEDIUM);
    if (a->free_bytes >= 0) {
        lm_format_size(a->free_bytes, need, sizeof need);
        snprintf(fre, sizeof fre, short_of_room ? "ONLY %s FREE" : "FREE %s", need);
        lui_print_right(&a->surf, a->f6, fre, L_CHK_X + L_CHK_W, L_SPACE_Y,
                        short_of_room ? DB_WHITE : DM_TEXT_MEDIUM);
    }
}

/* The downloading screen, and the same screen stopped by a failure. The bar and
 * the two numbers stay where they were when it fails, so the player can see how
 * far it got and that Retry carries on from there. */
static void l_draw_media_run(L_App *a)
{
    char item[160], d[32], t[32], line[128], why[256];
    long long done, total;
    const LM_Phase ph = l_m_phase(a);
    const int failed = ph == LM_FAILED && !a->m_running;
    const char *head;
    int frac = -1, pct = 0;

    l_m_bytes(a, &done, &total);
    if (total > 0) {
        if (done < 0)
            done = 0;
        if (done > total)
            done = total;
        frac = (int)((done * 1000) / total);
        pct = (int)((done * 100) / total);
    } else if (failed)
        frac = 0; /* stopped, and a barber pole would say it is still working */

    if (failed)
        head = a->i_job ? "INSTALL STOPPED"
               : l_m_job_removes_only(a) ? "REMOVAL STOPPED" : "DOWNLOAD STOPPED";
    else if (a->m_cancelling)
        head = "STOPPING";
    else if (a->i_job && ph == LM_CHECKING && a->i_stage == 1)
        head = "INSTALLING";
    else if (ph == LM_CHECKING)
        head = "CHECKING";
    else if (ph == LM_REMOVING)
        head = "REMOVING";
    else
        head = "DOWNLOADING";
    lui_print(&a->surf, a->f6, head, L_PANEL_X + 8, L_HEAD_Y, DM_TEXT_MEDIUM);
    lui_print_right(&a->surf, a->f6, a->i_job ? "OPENCNC3D" : "MOVIES AND MUSIC",
                    L_PANEL_X + L_PANEL_W - 8, L_HEAD_Y, DM_TEXT_MEDIUM);

    if (failed) {
        l_m_reason(a, why, sizeof why);
        lui_print_wrapped(&a->surf, a->f6, why[0] ? why : "The download failed.",
                          L_MEDIA_TEXT_X, L_MEDIA_TEXT_Y, L_MEDIA_TEXT_W, DB_WHITE, 1, 2);
    } else {
        l_m_item(a, item, sizeof item);
        lui_print_centered(&a->surf, a->f6, item, L_PANEL_X + L_PANEL_W / 2, L_RUN_ITEM_Y,
                           DM_TEXT_BRIGHT);
    }

    /* A removal has nothing to count in bytes worth showing, and a job that has not
     * said its size yet has no position: both get the barber pole, which says
     * "working" without claiming how far. */
    if (!(failed && !a->i_job && l_m_job_removes_only(a))) /* a stopped removal has no bar */
        lui_gauge_draw(&a->surf, L_RUN_GAUGE_X, L_RUN_GAUGE_Y, L_RUN_GAUGE_W,
                       L_RUN_GAUGE_H, ph == LM_REMOVING ? -1 : frac, a->frame);
    if (total > 0 && ph != LM_REMOVING) {
        lm_format_size(done, d, sizeof d);
        lm_format_size(total, t, sizeof t);
        snprintf(line, sizeof line, "%s OF %s  %d%%", d, t, pct);
        lui_print_centered(&a->surf, a->f6, line, L_PANEL_X + L_PANEL_W / 2, L_RUN_SIZE_Y,
                           DM_TEXT_MEDIUM);
    }

    lui_print_wrapped(&a->surf, a->f6,
                      failed && a->i_job && a->install_offer
                          ? "Retry carries on from where it stopped. The game is not "
                            "installed yet."
                      : failed && !a->i_job && l_m_job_removes_only(a)
                          ? "Retry tries the removal again; Back leaves the files that "
                            "were not removed where they are."
                      : failed ? "Retry carries on from where it stopped. Play without "
                                 "starts the game with no movies or music, and the gear "
                                 "adds them later."
                      : a->i_job ? "The game first, then the movies and music. Cancel keeps "
                                   "what has arrived, and the next try carries on from there."
                                 : "Cancel keeps what has arrived, and the next try carries on "
                                   "from there.",
                      L_MEDIA_TEXT_X, L_RUN_NOTE_Y, L_MEDIA_TEXT_W, DM_TEXT_MEDIUM, 1, 3);
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
    if (ls_enabled()) ls_begin();
    else dm_draw_plate(&a->surf, a->pack);

    /* menus.cpp:809 Dialog_Box, then :810 Draw_Caption's filigree pair. */
    if (!ls_enabled()) dm_green_dialog(&a->surf, L_DLG_X, L_DLG_Y, L_DLG_W, L_DLG_H);
    options = db_shape(a->pack, "OPTIONS");
    if (options && !ls_enabled()) {
        db_draw_shape_centered(&a->surf, options, DM_FILIGREE_LEFT, L_DLG_X + 12,
                               L_DLG_Y + 11);
        db_draw_shape_centered(&a->surf, options, DM_FILIGREE_RIGHT,
                               L_DLG_X + L_DLG_W - 14, L_DLG_Y + 11);
    }

    if (ls_enabled()) ls_rgba_image(launcher_wordmark_rgba,L_WORDMARK_W,L_WORDMARK_H,
                                    (ls_width()-L_WORDMARK_W)/2,12);
    else lui_print_centered(&a->surf, a->f8, "OpenCNC3D", DM_SCREEN_W / 2, L_TITLE_Y,
                       DM_TEXT_BRIGHT);

    /* THE TWO NUMBERS ARE THE HEADLINE, because the question a player opens this to
     * answer is whether they differ. Both are labelled the same way and set the
     * same way, so the eye compares two numbers rather than reading two sentences.
     * The right hand one carries a word instead of a number only while there is no
     * number to carry: an answer that has not arrived yet is not the same as an
     * answer of "the same as the one you have", and saying so costs one word. */
    if (a->install_offer)
        snprintf(line, sizeof line, "INSTALLED  NOTHING YET");
    else
        snprintf(line, sizeof line, "INSTALLED  v%s", a->version);
    lui_print(&a->surf, a->f6, line, L_PANEL_X, L_INFO_Y, DM_TEXT_MEDIUM);

    switch (ph) {
    case LU_UPTODATE:
    case LU_AVAILABLE:
    case LU_DOWNLOADING:
    case LU_APPLYING:
    case LU_DONE:
        lu_latest(a->up, a->latest, sizeof a->latest);
        snprintf(line, sizeof line, "AVAILABLE  v%s", a->latest);
        break;
    case LU_NOTCONFIGURED:
        snprintf(line, sizeof line, "AVAILABLE  UPDATES OFF");
        break;
    case LU_IDLE:
    case LU_CHECKING:
        snprintf(line, sizeof line, "AVAILABLE  CHECKING...");
        break;
    case LU_FAILED:
        snprintf(line, sizeof line, "AVAILABLE  NOT KNOWN");
        break;
    default:
        line[0] = '\0';
        break;
    }
    lui_print_right(&a->surf, a->f6, line, L_PANEL_X + L_PANEL_W, L_INFO_Y,
                    ph == LU_AVAILABLE ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM);

    /* ONE WELL, THREE SCREENS. The frame is drawn once and only the contents
     * switch, so the dialog does not change shape under the player when they press
     * Changelog or open the browser. */
    lui_panel(&a->surf, L_PANEL_X, L_PANEL_Y, L_PANEL_W, L_PANEL_H);
    switch (a->view) {
    case L_VIEW_NOTES:
        lui_text_draw(&a->surf, a->f6, &a->notes, L_TEXT_X, L_TEXT_Y, L_TEXT_W,
                      L_TEXT_H);
        lui_scrollbar_draw(&a->surf, &a->notes, L_BAR_X, L_TEXT_Y, L_BAR_W, L_TEXT_H);
        break;
    case L_VIEW_BROWSE:
        l_draw_browser(a);
        break;
    case L_VIEW_SETUP:
    case L_VIEW_SETTINGS:
    case L_VIEW_INSTALL:
        l_draw_media_form(a);
        break;
    case L_VIEW_MEDIA_RUN:
        l_draw_media_run(a);
        break;
    default:
        l_draw_modules(a);
        break;
    }

    /* The status line, and the gauge only while there is something to measure.
     *
     * THE MOVIES AND MUSIC SCREENS HAVE THEIR OWN LINE: a refusal, the removal
     * question, or what a stopped download kept. The update check's sentence is
     * about another job and stays off them. Elsewhere the update's line is shown,
     * except that a media job's result stands there until the player presses
     * something, or until an update starts and has progress to report.
     *
     * TWO ROWS WHEN THERE IS NO GAUGE, one when there is, and never past the well's
     * width: an error from the network can be a sentence long, and it used to run
     * straight across the dialog's right hand border. */
    if (a->up)
        lu_status(a->up, a->status, sizeof a->status);
    {
        const int media_view = a->view == L_VIEW_SETUP || a->view == L_VIEW_SETTINGS ||
                               a->view == L_VIEW_MEDIA_RUN || a->view == L_VIEW_INSTALL;
        const int gauge = !media_view && (ph == LU_DOWNLOADING || ph == LU_APPLYING);
        const char *text = a->status;
        unsigned char ink = ph == LU_FAILED ? DB_WHITE : DM_TEXT_MEDIUM;
        if (media_view || (a->mstatus[0] && !gauge)) {
            text = a->mstatus;
            ink = (media_view && a->m_confirm) ? DB_WHITE : DM_TEXT_MEDIUM;
        }
        lui_print_wrapped(&a->surf, a->f6, text, L_PANEL_X, L_STATUS_Y, L_PANEL_W, ink, 0,
                          gauge ? 1 : 2);
        snprintf(a->drawn_status, sizeof a->drawn_status, "%s", text);
        if (gauge)
            lui_gauge_draw(&a->surf, L_GAUGE_X, L_GAUGE_Y, L_GAUGE_W, L_GAUGE_H,
                           lu_progress(a->up), a->frame);
    }

    /* The highlight is in one row at a time, so a button is hot only while the
     * highlight is not sitting on a tile. */
    for (i = 0; i < L_BTN_COUNT; i++)
        lui_control_draw(&a->surf, a->grad, a->f6, &a->btn[i], a->pressed == i,
                         !a->on_mods && a->hot == i);

    if (ls_enabled()) {
        unsigned char cursor[DM_SCREEN_W*DM_SCREEN_H]; DB_Surface cs;
        /* A transmission over the assembled display. Hit seats, text shapes and
         * source material stay still; the original pointer is drawn above it. */
        if (!a->no_motion)
            ls_signal_apply(ls_pixels(),ls_width(),ls_height(),a->signal_ms);
        memset(cursor,0,sizeof cursor); db_surface_init(&cs,DM_SCREEN_W,DM_SCREEN_H,cursor);
        dm_draw_cursor(&cs,a->pack,a->mx,a->my);
        l_skin_indexed(a,cursor,DM_SCREEN_W,DM_SCREEN_H,0,0,0,0,DM_SCREEN_W,DM_SCREEN_H,DM_SCREEN_W,DM_SCREEN_H);
    } else dm_draw_cursor(&a->surf, a->pack, a->mx, a->my);
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

    /* A DISABLED BUTTON IS DISABLED FOR THE KEYBOARD TOO. lui_button_hit refuses a
       disabled button, so the MOUSE could never press one; Return, Enter and Space went
       straight here and pressed it anyway. PLAY and EDITOR are disabled precisely while
       an update is downloading, so the keyboard could leave the launcher mid-download --
       the loop exits, the launcher execs the game, and the half-written update is
       abandoned with nothing to finish it. Refused at the one place both paths pass
       through rather than at the key handler, so a third caller cannot reintroduce it. */
    if (item < 0 || item >= L_BTN_COUNT)
        return;
    if (a->btn[item].disabled || a->btn[item].hidden)
        return;

    /* A job's result stays on the grid until the player does something; this is
     * something. */
    if (a->view == L_VIEW_MODULES || a->view == L_VIEW_NOTES || a->view == L_VIEW_BROWSE)
        a->mstatus[0] = '\0';

    if (item == L_BTN_CHK_MOVIES || item == L_BTN_CHK_MUSIC) {
        if (item == L_BTN_CHK_MOVIES)
            a->want_movies = !a->want_movies;
        else
            a->want_music = !a->want_music;
        a->mstatus[0] = '\0';
        l_set_buttons(a);
        return;
    }
    if (item == L_BTN_GEAR) {
        l_open_settings(a);
        return;
    }

    /* The movies and music screens give the four slots their own jobs. */
    if (a->view == L_VIEW_SETUP || a->view == L_VIEW_SETTINGS || a->view == L_VIEW_INSTALL) {
        if (item == L_BTN_PLAY) {
            if (a->m_confirm)
                l_m_start(a);
            else
                l_m_request(a);
        } else if (item == L_BTN_QUIT) {
            if (a->m_confirm) {
                a->m_confirm = 0;
                a->mstatus[0] = '\0';
                l_set_buttons(a);
            } else if (a->view == L_VIEW_SETUP || a->view == L_VIEW_INSTALL)
                a->quit = 1;
            else {
                /* Back to the grid with the highlight on the gear it came from,
                 * not on Quit, which shares this slot and is one Return away. */
                a->mstatus[0] = '\0';
                a->hot = L_BTN_GEAR;
                l_set_view(a, L_VIEW_MODULES);
            }
        }
        return;
    }
    if (a->view == L_VIEW_MEDIA_RUN) {
        if (l_m_phase(a) == LM_FAILED && !a->m_running) {
            if (item == L_BTN_PLAY && a->i_job && a->install_offer)
                l_i_start(a); /* the package and the media carry on from what they kept */
            else if (item == L_BTN_PLAY) {
                a->i_job = 0; /* the game is in: what is left is the movies and music */
                l_m_start(a); /* lmedia carries on from what it kept */
            } else if (item == L_BTN_NOTES && !a->i_job && l_m_job_removes_only(a)) {
                l_m_refresh(a);
                l_set_view(a, a->m_from == L_VIEW_SETTINGS ? L_VIEW_SETTINGS
                                                           : L_VIEW_SETUP);
            } else if (item == L_BTN_NOTES) {
                a->play_after = 1;
                a->quit = 1;
            } else if (item == L_BTN_QUIT)
                a->quit = 1;
        } else if (item == L_BTN_QUIT && a->m_running && a->media) {
            /* A first install stops whichever half is running: the package, or the
             * movies and music after it. */
            if (a->i_job && a->i_stage < 3)
                lu_cancel(a->fresh);
            else
                lm_cancel(a->media);
            a->m_cancelling = 1;
            l_set_buttons(a);
        }
        return;
    }

    switch (item) {
    case L_BTN_PLAY:
        if (ph == LU_AVAILABLE) {
            lu_apply(a->up);
        } else {
            a->play_after = 1;
            a->quit = 1;
        }
        break;
    case L_BTN_EDITOR:
        /* 2 rather than 1: the same leave-the-loop-and-launch path Play takes, with
         * the editor's executable at the end of it instead of the game's. Leaving
         * rather than spawning keeps the rule Play already follows, so there is no
         * launcher sitting behind the editor in the dock and no running program to
         * stop the next update replacing it. */
        a->play_after = 2;
        a->quit = 1;
        break;
    case L_BTN_NOTES:
        /* The way in and the way back out, which is why the label swaps. */
        l_set_view(a, a->view == L_VIEW_NOTES ? L_VIEW_MODULES : L_VIEW_NOTES);
        break;
    case L_BTN_CLOSE:
        l_set_view(a, L_VIEW_MODULES);
        break;
    case L_BTN_QUIT:
        a->quit = 1;
        break;
    default:
        break;
    }
}

/* Pressing a tile. An installed module is one you can be in, so it is selected;
 * an empty slot is one you might fill, so it opens the browser. The browser
 * currently lists nothing, and that is a fact about what exists rather than about
 * what this does. */
static void l_activate_module(L_App *a, int item)
{
    if (item < 0 || item >= L_MOD_COUNT)
        return;
    if (a->mod[item].installed) {
        a->mod_sel = item;
        l_set_buttons(a);
    } else
        l_set_view(a, L_VIEW_BROWSE);
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
        /* Hidden as well as disabled: the walk must not stop on a control that is
         * not on the screen being looked at. */
        if (!a->btn[idx].disabled && !a->btn[idx].hidden)
            return idx;
    }
    return from;
}

static int l_next_module(int from, int delta)
{
    int idx = from + delta;
    if (idx < 0)
        idx = L_MOD_COUNT - 1;
    if (idx >= L_MOD_COUNT)
        idx = 0;
    return idx;
}

/* Up and Down on Setup and Settings: the movies tick, the music tick, then the
 * button row, top to bottom, stopping at either end rather than wrapping, because
 * a column that wraps puts the highlight somewhere the eye did not follow. A
 * frozen tick (a removal question is waiting) is stepped over. */
static void l_m_step(L_App *a, int delta)
{
    static const int col[3] = {L_BTN_CHK_MOVIES, L_BTN_CHK_MUSIC, -1};
    int pos = a->hot == L_BTN_CHK_MOVIES ? 0 : a->hot == L_BTN_CHK_MUSIC ? 1 : 2;
    int next = pos;
    for (;;) {
        next += delta;
        if (next < 0 || next > 2)
            return;
        if (next == 2) {
            a->hot = !a->btn[L_BTN_PLAY].disabled ? L_BTN_PLAY : L_BTN_QUIT;
            return;
        }
        if (!a->btn[col[next]].disabled && !a->btn[col[next]].hidden) {
            a->hot = col[next];
            return;
        }
    }
}

static void l_event(L_App *a, const SDL_Event *e)
{
    int i, m;
    switch (e->type) {
    case SDL_QUIT:
        a->quit = 1;
        break;
    case SDL_MOUSEMOTION:
        l_to_menu(a, e->motion.x, e->motion.y, &a->mx, &a->my);
        for (i = 0; i < L_BTN_COUNT; i++)
            if (lui_button_hit(&a->btn[i], a->mx, a->my)) {
                a->hot = i;
                a->on_mods = 0;
            }
        m = l_mod_hit(a, a->mx, a->my);
        if (m >= 0) {
            a->mod_hot = m;
            a->on_mods = 1;
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button != SDL_BUTTON_LEFT)
            break;
        l_to_menu(a, e->button.x, e->button.y, &a->mx, &a->my);
        for (i = 0; i < L_BTN_COUNT; i++)
            if (lui_button_hit(&a->btn[i], a->mx, a->my)) {
                a->pressed = i;
                a->hot = i;
                a->on_mods = 0;
            }
        m = l_mod_hit(a, a->mx, a->my);
        if (m >= 0) {
            a->mod_pressed = m;
            a->mod_hot = m;
            a->on_mods = 1;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT)
            break;
        l_to_menu(a, e->button.x, e->button.y, &a->mx, &a->my);
        /* gadget.cpp releases on the control the press started on, so a drag off
         * a button cancels it rather than firing the one under the cursor. The
         * tiles are read the same way, and the press is cleared before the action
         * runs because opening the browser takes the tile off the screen. */
        if (a->pressed >= 0 && lui_button_hit(&a->btn[a->pressed], a->mx, a->my)) {
            i = a->pressed;
            a->pressed = -1;
            l_activate(a, i);
        }
        if (a->mod_pressed >= 0 && l_mod_hit(a, a->mx, a->my) == a->mod_pressed) {
            m = a->mod_pressed;
            a->mod_pressed = -1;
            l_activate_module(a, m);
        }
        a->pressed = -1;
        a->mod_pressed = -1;
        break;
    case SDL_MOUSEWHEEL:
        /* Only the changelog scrolls. A wheel over the grid used to move text
         * nobody could see, which is how a screen ends up remembering a position
         * the player never set. */
        if (a->view == L_VIEW_NOTES)
            lui_text_scroll(&a->notes, -e->wheel.y * 3);
        break;
    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_ESCAPE:
            /* Escape leaves the screen you are on, and quits only from the one
             * there is nothing to leave. ON THE MOVIES AND MUSIC SCREENS IT IS THE
             * LAST SLOT'S ANSWER: Keep to a removal question, Back from Settings,
             * Quit from Setup, which is the first screen there is. On a download
             * it does nothing at all: a key pressed by accident must not throw
             * a long download away, and Cancel is a button of its own. */
            if (a->view == L_VIEW_SETUP || a->view == L_VIEW_SETTINGS ||
                a->view == L_VIEW_INSTALL)
                l_activate(a, L_BTN_QUIT);
            else if (a->view == L_VIEW_MEDIA_RUN) {
                if (!a->m_running && a->i_job) {
                    a->i_job = 0;
                    l_set_view(a, a->install_offer ? L_VIEW_INSTALL : L_VIEW_SETUP);
                } else if (!a->m_running)
                    l_set_view(a, a->m_from == L_VIEW_SETTINGS ? L_VIEW_SETTINGS
                                                               : L_VIEW_SETUP);
            } else if (a->view != L_VIEW_MODULES)
                l_set_view(a, L_VIEW_MODULES);
            else
                a->quit = 1;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (a->on_mods)
                l_activate_module(a, a->mod_hot);
            else
                l_activate(a, a->hot);
            break;
        case SDLK_LEFT:
            if (a->on_mods)
                a->mod_hot = l_next_module(a->mod_hot, -1);
            else
                a->hot = l_next_enabled(a, a->hot, -1);
            break;
        case SDLK_RIGHT:
        case SDLK_TAB:
            if (a->on_mods)
                a->mod_hot = l_next_module(a->mod_hot, +1);
            else
                a->hot = l_next_enabled(a, a->hot, +1);
            break;
        /* UP AND DOWN ARE TWO THINGS AND THE SCREEN DECIDES WHICH. On the
         * changelog they scroll it, which is the only thing there is to do with a
         * panel of text. On the grid there is nothing to scroll and there are
         * three rows of controls, the gear above the tiles, the tiles, and the
         * buttons, so they step between those. On Setup and Settings the two ticks
         * are a column above the button row, and they walk it. */
        case SDLK_UP:
            if (a->view == L_VIEW_NOTES)
                lui_text_scroll(&a->notes, -1);
            else if (a->view == L_VIEW_MODULES) {
                if (a->on_mods) {
                    if (!a->btn[L_BTN_GEAR].hidden && !a->btn[L_BTN_GEAR].disabled) {
                        a->on_mods = 0;
                        a->hot = L_BTN_GEAR;
                    }
                } else if (a->hot != L_BTN_GEAR)
                    a->on_mods = 1;
            } else if (a->view == L_VIEW_SETUP || a->view == L_VIEW_SETTINGS ||
                       a->view == L_VIEW_INSTALL)
                l_m_step(a, -1);
            break;
        case SDLK_DOWN:
            if (a->view == L_VIEW_NOTES)
                lui_text_scroll(&a->notes, +1);
            else if (a->view == L_VIEW_MODULES && !a->on_mods && a->hot == L_BTN_GEAR)
                a->on_mods = 1;
            else if (a->view == L_VIEW_SETUP || a->view == L_VIEW_SETTINGS ||
                     a->view == L_VIEW_INSTALL)
                l_m_step(a, +1);
            else {
                a->on_mods = 0;
                if (a->hot == L_BTN_GEAR) {
                    a->hot = L_BTN_PLAY;
                    l_fix_hot(a);
                }
            }
            break;
        case SDLK_PAGEUP:
            if (a->view == L_VIEW_NOTES)
                lui_text_scroll(&a->notes, -a->notes.visible);
            break;
        case SDLK_PAGEDOWN:
            if (a->view == L_VIEW_NOTES)
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
    /* Two calls rather than one literal: the list crossed the 509 characters a C89
     * compiler is only required to manage, and a usage message is not worth relying
     * on an extension for. */
    printf("OpenCNC3D launcher\n"
           "  --dir <folder>   the installed game (default: found from this binary)\n"
           "  --shot <file>    render one frame to a PNG and exit\n"
           "  --view <screen>  modules (default), changelog, browser, setup,\n"
           "                   setup-progress, settings or install\n"
           "  --scale <n>      window scale for --shot and for the window\n");
    printf("  --no-motion      disable the Enhanced EVA signal animation\n"
           "  --signal-time <ms>  fix its phase for --shot (0..3600000)\n");
    printf("  --play           start the game at once, no window\n"
           "  -- <args>        everything after -- is passed to the game\n"
           "  --check          ask the update host what it has, print it, exit\n"
           "  --update         check and, if there is one, install it. No window.\n");
    printf("  --media-install movies,music|movies|music|none --dir <folder>\n"
           "                   download or remove the movies and music. No window.\n"
           "  --install-game [--build <version>] [--media ...] --dir <folder>\n"
           "                   install the newest build, or that version, into a folder,\n"
           "                   then the media\n"
           "  --fetch-menu-art --dir <folder>  only the menu art, for a folder that\n"
           "                   holds nothing but the launcher\n"
           "  --media-manifest <file>  a media manifest other than the built-in one\n");
    printf("  --media-shot-progress <done>/<total>, --media-shot-failed <reason>,\n"
           "  --media-shot-choice <movies,music|...>, --media-shot-tick <movies|music>,\n"
           "  --media-shot-confirm\n"
           "                   put a --shot of the media screens into one state\n");
}

/* ------------------------------------------------------------------------ *
 * A folder that holds the launcher and not the game.
 * ------------------------------------------------------------------------ */

/* NO INSTALL RECORD AND NO GAME. The game's own executable is what tells this from
 * the two other folders without a record: the development play folder, built
 * straight out of the tree, and an install too old to have one; both have the game,
 * and play. Never a folder inside the macOS app bundle, which is where the walk up
 * from the launcher ends when it finds no game anywhere: a game installed there would
 * live inside the program that installs it. Never a link to a folder either, the same
 * refusal a first install makes. */
static int l_install_offer(const L_App *a)
{
    struct stat st;
    if (!a->dir[0] || lz_is_install(a->dir) || l_has_named_file(a->dir, L_GAME_EXE))
        return 0;
    if (strstr(a->dir, ".app/Contents"))
        return 0;
    return stat(a->dir, &st) == 0 && S_ISDIR(st.st_mode) && !lz_is_symlink(a->dir);
}

/* The window, its GL context and the texture the 320x200 surface goes up through. */
static int l_open_window(L_App *a, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    a->win = SDL_CreateWindow("OpenCNC3D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              l_canvas_w() * scale, l_canvas_h() * scale, SDL_WINDOW_OPENGL);
    if (!a->win) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 0;
    }
    if (!SDL_GL_CreateContext(a->win)) {
        fprintf(stderr, "GL: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GL_SetSwapInterval(1);
    a->tw = l_pot(l_canvas_w());
    a->th = l_pot(l_canvas_h());
    a->rgba = (unsigned char *)calloc((size_t)l_canvas_w() * l_canvas_h() * 4, 1);
    a->padded = (unsigned char *)calloc((size_t)a->tw * a->th * 4, 1);
    glGenTextures(1, &a->tex);
    glBindTexture(GL_TEXTURE_2D, a->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    return 1;
}

/* THE MENU ART, ASKED FOR IN A PLAIN SYSTEM DIALOG, because without it there is nothing
 * to draw the launcher's own screens with. Yes fetches it (lu_fetch_pack) and then the
 * launcher opens on INSTALL as it would have; No quits, having changed nothing. */
static int l_ask_fetch_pack(const char *dir)
{
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Download"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"},
    };
    SDL_MessageBoxData d;
    char msg[1600];
    int hit = 0;
    snprintf(msg, sizeof msg,
             "OpenCNC3D is not installed in this folder yet:\n%s\n\n"
             "The launcher can install it from opencnc3d.com. To show its setup screen it "
             "first downloads its menu art, a few MB; then it asks which of the movies and "
             "music to download with the game.",
             dir);
    memset(&d, 0, sizeof d);
    d.flags = SDL_MESSAGEBOX_INFORMATION;
    d.title = "OpenCNC3D";
    d.message = msg;
    d.numbuttons = 2;
    d.buttons = buttons;
    if (SDL_ShowMessageBox(&d, &hit) != 0)
        return 0;
    return hit == 1;
}

typedef struct
{
    const char *dir;
    SDL_mutex *mx;
    long long got, total;
    volatile int cancel, done;
    int ok, unreachable;
    char err[512];
} L_PackJob;

static int l_pack_cb(void *user, long long done, long long total)
{
    L_PackJob *j = (L_PackJob *)user;
    SDL_LockMutex(j->mx);
    j->got = done;
    j->total = total;
    SDL_UnlockMutex(j->mx);
    return !j->cancel;
}

static int l_pack_thread(void *user)
{
    L_PackJob *j = (L_PackJob *)user;
    j->ok = lu_fetch_pack(j->dir, l_pack_cb, j, &j->unreachable, j->err, sizeof j->err);
    j->done = 1;
    return 0;
}

/* The fetch, with the window up and a plain bar in it, there being no 1995 art to draw
 * a better one with yet: the frame and fill in the dialog's own two greens, and
 * nothing else. Closing the window stops it. Returns 1 with the pack in the folder;
 * 0 having said why, or quietly when the player closed the window. */
static int l_fetch_pack_window(L_App *a)
{
    L_PackJob j;
    SDL_Thread *t;
    memset(&j, 0, sizeof j);
    j.dir = a->dir;
    j.mx = SDL_CreateMutex();
    t = SDL_CreateThread(l_pack_thread, "opencnc3d-menu-art", &j);
    if (!t) {
        SDL_DestroyMutex(j.mx);
        return 0;
    }
    while (!j.done) {
        SDL_Event e;
        long long got, total;
        float f = 0.0f;
        int w = 0, h = 0;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT)
                j.cancel = 1;
        SDL_LockMutex(j.mx);
        got = j.got;
        total = j.total;
        SDL_UnlockMutex(j.mx);
        if (total > 0)
            f = (float)((double)got / (double)total);
        if (f > 1.0f)
            f = 1.0f;
        SDL_GL_GetDrawableSize(a->win, &w, &h);
        glViewport(0, 0, w, h);
        glDisable(GL_TEXTURE_2D);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, DM_SCREEN_W, DM_SCREEN_H, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glColor3ub(48, 84, 44);
        glRectf(58.0f, 96.0f, 262.0f, 106.0f);
        glColor3ub(0, 0, 0);
        glRectf(59.0f, 97.0f, 261.0f, 105.0f);
        glColor3ub(104, 172, 96);
        glRectf(60.0f, 98.0f, 60.0f + 200.0f * f, 104.0f);
        SDL_GL_SwapWindow(a->win);
        SDL_Delay(16);
    }
    SDL_WaitThread(t, NULL);
    SDL_DestroyMutex(j.mx);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (!j.ok && !j.cancel) {
        char msg[1200];
        snprintf(msg, sizeof msg,
                 "The menu art could not be downloaded: %s.\n\n"
                 "Run OpenCNC3D Setup again, or download the game from opencnc3d.com.",
                 j.err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenCNC3D", msg, a->win);
    }
    return j.ok;
}

#ifdef __APPLE__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
/* THE LOCAL NETWORK PROMPT, ASKED FOR HERE, while this window is the one on screen.
 * macOS 15 asks once whether an app may talk to the local network, the first time the
 * app sends to a local address. The game asks that question fullscreen, from a JOIN, and
 * the answer to a dialog nobody saw is silence: the packet is dropped, the host never
 * hears it, and the joiner is told the host did not answer (the first two real-network
 * tests, 5 and 6 Sep 2026). One datagram to the subnet broadcast on the game browser's
 * port, from the launcher, puts the dialog up here, windowed, before any game. Every
 * browser drops it: wrong size, wrong magic. */
static void mac_local_network_probe(void)
{
    /* Sized from the literal, so the length sent cannot drift from the text. */
    static const char probe[] = "OpenCNC3D?";
    struct sockaddr_in a;
    int one = 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(17420);
    a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    (void)sendto(fd, probe, sizeof probe - 1, 0, (struct sockaddr *)&a, sizeof a);
    close(fd);
}
#endif

int main(int argc, char **argv)
{
    L_App a;
    char err[512];
    char shot[1024];
    char path[1200];
    char manifest[1024];
    char *local_notes;
    int scale = 3, play_now = 0, headless = 0, view_given = 0, i;
    int shot_choice = -1, shot_confirm = 0, shot_tick = 0;
    int cli_handled = 0, cli_rc;
    Uint32 signal_start;
    int signal_fixed = 0;

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

    /* THE HEADLESS INSTALL MODES COME FIRST, before anything here reads a folder or
     * draws: --media-install and --install-game are what the Windows setup program
     * runs, into a folder that may hold nothing but this launcher, so neither the
     * menu pack nor the install-finding walk below can be allowed to stand in front
     * of them. lsetup_cli.c says whether the command line was one of its own. */
    cli_rc = lsetup_cli_main(argc, argv, &cli_handled);
    if (cli_handled)
        return cli_rc;


    memset(&a, 0, sizeof a);
    a.pressed = -1;
    a.mod_pressed = -1;
    a.hot = L_BTN_PLAY;
    a.view = L_VIEW_MODULES;

    /* THE ONE MODULE THERE IS. The other three rows stay empty, which is the state
     * the add tile is drawn for, and it is a state rather than a placeholder: the
     * day a second game ships, it is a second row here. */
    a.mod[0].name = "OpenCNC3D";
    a.mod[0].installed = 1;
    a.mod_sel = 0;
    a.mod_hot = 0;
    a.free_bytes = -1;
    shot[0] = '\0';
    manifest[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) {
            /* Everything after this belongs to the game, not to the launcher. ALL
             * of it or none: a list that stopped copying when it filled up handed
             * the game a flag without its value, and the game's usage message was
             * the only sign. Room is kept for the editor's own three. */
            int keep = LC_GAME_ARGS - LC_EDITOR_ARGS;
            if (argc - i - 1 > keep) {
                fprintf(stderr, "%d arguments after --, and the launcher hands the "
                        "game at most %d. Not starting rather than dropping:",
                        argc - i - 1, keep);
                for (i += 1 + keep; i < argc; i++)
                    fprintf(stderr, " %s", argv[i]);
                fprintf(stderr, "\n");
                return 2;
            }
            while (++i < argc)
                l_game_args[l_game_argc++] = argv[i];
            break;
        }
        if (!strcmp(argv[i], "--dir") && i + 1 < argc)
            snprintf(a.dir, sizeof a.dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            snprintf(shot, sizeof shot, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--no-motion"))
            a.no_motion = 1;
        else if (!strcmp(argv[i], "--signal-time")) {
            char *end;
            const char *value;
            unsigned long ms;
            if (i + 1 == argc) {
                fprintf(stderr, "--signal-time needs milliseconds from 0 to 3600000\n");
                return 2;
            }
            value = argv[++i];
            ms = strtoul(value, &end, 10);
            if (!*value || *end || ms > 3600000ul || value[0] == '-') {
                fprintf(stderr, "--signal-time needs milliseconds from 0 to 3600000\n");
                return 2;
            }
            a.signal_ms = (unsigned)ms;
            signal_fixed = 1;
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--view") && i + 1 < argc) {
            /* Which screen the well opens on, so a still picture can be taken of
             * each of them rather than only of the one the launcher starts in. */
            const char *v = argv[++i];
            view_given = 1;
            if (!strcmp(v, "changelog"))
                a.view = L_VIEW_NOTES;
            else if (!strcmp(v, "browser"))
                a.view = L_VIEW_BROWSE;
            else if (!strcmp(v, "setup"))
                a.view = L_VIEW_SETUP;
            else if (!strcmp(v, "setup-progress"))
                a.view = L_VIEW_MEDIA_RUN;
            else if (!strcmp(v, "settings"))
                a.view = L_VIEW_SETTINGS;
            else if (!strcmp(v, "install"))
                a.view = L_VIEW_INSTALL;
            else
                a.view = L_VIEW_MODULES;
        } else if (!strcmp(argv[i], "--media-manifest") && i + 1 < argc)
            snprintf(manifest, sizeof manifest, "%s", argv[++i]);
        /* THE PICTURE-ONLY MEDIA OPTIONS, read by --shot and by nothing else: where
         * the downloading screen's bar stands, the reason a failed one gives, which
         * boxes are ticked, and whether the removal question is up. Each puts one
         * screen into one state without a network or a job behind it, so every
         * state can be looked at in pixels. */
        else if (!strcmp(argv[i], "--media-shot-progress") && i + 1 < argc) {
            const char *v = argv[++i];
            const char *slash = strchr(v, '/');
            a.shot_media = 1;
            a.shot_phase = LM_FETCHING;
            a.shot_done = strtoll(v, NULL, 10);
            a.shot_total = slash ? strtoll(slash + 1, NULL, 10) : 0;
        } else if (!strcmp(argv[i], "--media-shot-failed") && i + 1 < argc) {
            a.shot_media = 1;
            a.shot_phase = LM_FAILED;
            snprintf(a.shot_reason, sizeof a.shot_reason, "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--media-shot-choice") && i + 1 < argc) {
            const char *v = argv[++i];
            shot_choice = (strstr(v, "movies") ? 1 : 0) | (strstr(v, "music") ? 2 : 0);
        } else if (!strcmp(argv[i], "--media-shot-tick") && i + 1 < argc) {
            /* One press on one box, after the screen has set them, as a player's
             * click would be: what Apply then sends is in the LAPPLY line. */
            const char *v = argv[++i];
            shot_tick = !strcmp(v, "movies") ? 1 : !strcmp(v, "music") ? 2 : 0;
        } else if (!strcmp(argv[i], "--media-shot-confirm"))
            shot_confirm = 1;
        else if (!strcmp(argv[i], "--module") && i + 1 < argc) {
            const char *name = argv[++i];
            if (!strcmp(name, "ra3d")) a.mod_sel = 1;
            else if (!strcmp(name, "opencnc3d")) a.mod_sel = 0;
            else { fprintf(stderr, "unknown module: %s\n", name); return 2; }
        } else if (!strcmp(argv[i], "--play"))
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
    l_find_ra_module(&a);
    if (a.mod_sel == 1 && !a.mod[1].installed) {
        fprintf(stderr, "Red Alert 3D is not installed in %s: %s\n", a.dir, a.ra_missing);
        return 1;
    }

    /* AN UNFINISHED UPDATE IS FINISHED BEFORE ANYTHING IS READ, by a launcher that
     * gets the install lock. lu_create takes it and undoes, or completes, whatever
     * update was killed part way. Read first, the version would be that update's
     * half-written claim, and the menu art could be missing or be the half of a pack
     * that refuses to load, so the launcher would stop at the message below and never
     * get this far again. Running this before the pack has been proven to load is
     * safe because it changes nothing in a folder lz_is_install does not recognise as
     * an install.
     *
     * NOT IN EVERY CASE. While another launcher holds the lock (one updating this
     * folder at this moment, say), or while the undo is refused because something
     * still has a file open, nothing is recovered here and the folder is read as it
     * stands: the window says nothing about it, and --play starts the game over it. */
#ifdef __APPLE__
    /* CPU witnesses and hidden checks must never open an OS permission prompt. */
    if (!shot[0] && !headless && !play_now &&
        !SDL_getenv("OPENCNC3D_BACKGROUND") && !SDL_getenv("SDL_MAC_BACKGROUND_APP"))
        mac_local_network_probe();
#endif
    a.up = lu_create(a.dir, NULL);
    l_read_installed(&a);
    lu_set_installed(a.up, a.version);

    if (play_now) {
        lu_destroy(a.up); /* the lock belongs to the launcher, never to the game */
        if (!l_launch(a.dir, a.mod_sel == 1 ? L_RA_GAME_EXE : L_GAME_EXE,
                      a.mod_sel == 1 ? "ra3d" : NULL, err, sizeof err)) {
            fprintf(stderr, "%s\n", err);
            return 1;
        }
        return 0;
    }

    snprintf(path,sizeof path,"%s/enhanced-menu.pack",a.dir);
    ls_load(path);
    if (ls_enabled()) scale = scale >= 3 ? scale / 3 : 1;

    a.install_offer = l_install_offer(&a);
    snprintf(path, sizeof path, "%s/dosmenu.pack", a.dir);
    a.pack = db_pack_load(path, err, sizeof err);
    if (!a.pack && a.install_offer && !shot[0] && !headless && l_ask_fetch_pack(a.dir)) {
        /* A folder holding only the launcher, and the player said yes: the art first,
         * with the window up, and then the launcher carries on to INSTALL. */
        if (l_open_window(&a, scale) && l_fetch_pack_window(&a))
            a.pack = db_pack_load(path, err, sizeof err);
        if (!a.pack) {
            lu_destroy(a.up);
            if (a.win)
                SDL_DestroyWindow(a.win);
            SDL_Quit();
            return 1;
        }
    }
    if (!a.pack) {
        /* Report resource failures in a message box so a desktop shortcut
         * launch explains the failure without an attached console. */
        char msg[3072];
        if (a.install_offer)
            snprintf(msg, sizeof msg,
                     "OpenCNC3D is not installed in this folder yet:\n%s\n\n%s\n\n"
                     "Run OpenCNC3D Setup again, or download the game from opencnc3d.com.",
                     a.dir, err);
        else
            snprintf(msg, sizeof msg,
                     "OpenCNC3D could not find its game files.\n\n"
                     "Looked in:\n%s\n\n%s\n\n"
                     "The launcher belongs in the same folder as the game, or inside "
                     "OpenCNC3D.app in it.",
                     a.dir, err);
        lu_destroy(a.up);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenCNC3D", msg, NULL);
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

    /* MOVIES AND MUSIC. What the manifest says they weigh, what is on the disk,
     * and which screen the window opens on.
     *
     * SETUP OPENS BY ITSELF ONLY ON AN INSTALL THAT HAS NEVER BEEN ASKED: a folder
     * lz_is_install recognises, with no opencnc3d-media.txt in it, started with no
     * --view of its own. The development play folder is not an install, so it
     * never shows Setup and never gets a record; a player who has answered once,
     * either way, is not asked again, and the gear is where the answer changes. */
    a.media = lm_create(a.dir, manifest[0] ? manifest : NULL);
    if (a.media)
        lm_sizes(a.media, &a.size_movies, &a.size_music);
    l_m_refresh(&a);
    /* A FOLDER WITH THE LAUNCHER AND NOT THE GAME OPENS ON INSTALL, and only such a
     * folder can: asked for anywhere else, the screen is the module grid. */
    if (a.view == L_VIEW_INSTALL && !a.install_offer)
        a.view = L_VIEW_MODULES;
    if (!view_given && a.media && a.install_offer)
        a.view = L_VIEW_INSTALL;
    else if (!view_given && a.media && lz_is_install(a.dir) && !lm_choice_recorded(a.dir))
        a.view = L_VIEW_SETUP;
    /* The downloading screen has nothing to show without a job, and the stand-ins
     * that give it one are for pictures: a window asked for it opens on Setup. */
    if (!shot[0]) {
        a.shot_media = 0;
        shot_choice = -1;
        shot_confirm = 0;
        if (a.view == L_VIEW_MEDIA_RUN)
            a.view = L_VIEW_SETUP;
    }
    if (a.view == L_VIEW_SETUP)
        l_open_setup(&a);
    else if (a.view == L_VIEW_SETTINGS)
        l_open_settings(&a);
    else if (a.view == L_VIEW_INSTALL)
        l_open_install(&a);
    else if (a.view == L_VIEW_MEDIA_RUN) {
        /* Only a picture reaches this screen without a job: what it shows is the
         * job the boxes describe, both of them unless told otherwise, as if
         * Install had been pressed on Setup. */
        a.want_movies = a.want_music = 1;
        a.job_movies = a.job_music = 1;
        a.had_movies = a.have_movies;
        a.had_music = a.have_music;
        a.m_from = L_VIEW_SETUP;
        if (!a.shot_media) {
            a.shot_media = 1;
            a.shot_phase = LM_FETCHING;
        }
    }
    if (shot_choice >= 0) {
        a.want_movies = (shot_choice & 1) != 0;
        a.want_music = (shot_choice & 2) != 0;
        a.job_movies = a.want_movies;
        a.job_music = a.want_music;
    }
    if (shot_tick == 1)
        a.want_movies = !a.want_movies;
    else if (shot_tick == 2)
        a.want_music = !a.want_music;
    if (a.view == L_VIEW_MEDIA_RUN && a.shot_total <= 0)
        a.shot_total = l_m_need(&a);
    if (shot_confirm && (a.view == L_VIEW_SETUP || a.view == L_VIEW_SETTINGS) &&
        l_m_frees(&a) > 0) {
        a.m_confirm = 1;
        l_m_question(&a, a.mstatus, sizeof a.mstatus);
        a.hot = L_BTN_QUIT;
    }
    /* A record an earlier run left pending is said on the module grid, once. */
    if (a.view == L_VIEW_MODULES && !a.install_offer && !a.mstatus[0])
        l_m_pending_line(&a, a.mstatus, sizeof a.mstatus);

    if (shot[0]) {
        /* The still-picture path, for the gate suite and for a design review that
         * wants to look at pixels rather than at a description of them. No window
         * and no GL: the surface IS the evidence. */
        unsigned char *big;
        int bw = l_canvas_w() * scale, bh = l_canvas_h() * scale, ok, guard, bi;
        a.rgba = (unsigned char *)calloc((size_t)l_canvas_w() * l_canvas_h() * 4, 1);

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
        if (ls_enabled()) memcpy(a.rgba,ls_pixels(),(size_t)l_canvas_w()*l_canvas_h()*4);
        else db_surface_to_rgba(&a.surf, a.pack->pal8, a.rgba, 0);
        big = (unsigned char *)malloc((size_t)bw * bh * 4);
        png_nearest_scale(a.rgba, l_canvas_w(), l_canvas_h(), big, scale);
        ok = png_write_rgba(shot, big, bw, bh);
        free(big);
        free(a.rgba);
        printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED to write", shot, bw, bh);
        printf("LSIGNAL|enabled=%d|ms=%u|period_ms=%u\n",
               ls_enabled() && !a.no_motion ? 1 : 0,a.signal_ms,LS_SIGNAL_PERIOD_MS);

        /* WHERE THE ROW BUTTONS ARE IN THE PICTURE JUST WRITTEN, and whether each was
         * drawn live. Proving a button is enabled means sampling its fill, and sampling
         * needs a rectangle. A reader that carries its own copy of the geometry holds
         * this screen's layout in a second place, and the row has already changed shape
         * once: a fourth button narrowed all four and moved every index along, after
         * which a sample aimed at one button landed on the label of the one beside it
         * and read a correct build as broken. Reported in the picture's own pixels,
         * scaled here rather than by the reader, because the scale is this program's. */
        for (bi = 0; bi < L_BTN_ROW; bi++)
            printf("LBTN|i=%d|label=%s|x=%d|y=%d|w=%d|h=%d|disabled=%d|hidden=%d\n",
                   bi, a.btn[bi].label ? a.btn[bi].label : "",
                   l_native_x(a.btn[bi].x) * scale, l_native_y(a.btn[bi].y) * scale,
                   l_native_extent(a.btn[bi].w) * scale, l_native_extent(a.btn[bi].h) * scale,
                   a.btn[bi].disabled ? 1 : 0, a.btn[bi].hidden ? 1 : 0);
        /* THE MODULE TABLE, the same way: each tile's rectangle, whether it is a
         * live module or an empty slot, and for Red Alert's slot, why it is empty.
         * drawn=0 when this screen is not the module grid, so a reader never samples
         * a tile that is not in the picture. */
        for (bi = 0; bi < L_MOD_COUNT; bi++) {
            int mx, my, mw, mh;
            l_mod_rect(bi, &mx, &my, &mw, &mh);
            printf("LMOD|i=%d|name=%s|installed=%d|selected=%d|x=%d|y=%d|w=%d|h=%d|drawn=%d",
                   bi, a.mod[bi].name ? a.mod[bi].name : "", a.mod[bi].installed ? 1 : 0,
                   a.mod_sel == bi ? 1 : 0, l_native_x(mx) * scale, l_native_y(my) * scale, l_native_extent(mw) * scale,
                   l_native_extent(mh) * scale, a.view == L_VIEW_MODULES ? 1 : 0);
            if (bi == 1 && a.ra_missing[0])
                printf("|missing=%s", a.ra_missing);
            printf("\n");
        }
        /* The rest of the table that is on this screen, the same way and for the
         * same reason: a checkbox's state and rectangle, and the gear's. A row
         * that is hidden on this screen is not listed, so a reader never samples
         * pixels for a control that was not drawn. */
        for (bi = L_BTN_ROW; bi < L_BTN_COUNT; bi++) {
            if (a.btn[bi].hidden)
                continue;
            if (a.btn[bi].kind == LUI_KIND_CHECK)
                printf("LCHK|label=%s|checked=%d|x=%d|y=%d|w=%d|h=%d|disabled=%d\n",
                       a.btn[bi].label ? a.btn[bi].label : "", a.btn[bi].checked ? 1 : 0,
                       l_native_x(a.btn[bi].x) * scale, l_native_y(a.btn[bi].y) * scale, l_native_extent(a.btn[bi].w) * scale,
                       l_native_extent(a.btn[bi].h) * scale, a.btn[bi].disabled ? 1 : 0);
            else if (a.btn[bi].kind == LUI_KIND_BUTTON)
                printf("LBTN|i=%d|label=%s|x=%d|y=%d|w=%d|h=%d|disabled=%d|hidden=0\n",
                       bi,a.btn[bi].label?a.btn[bi].label:"",
                       l_native_x(a.btn[bi].x)*scale,l_native_y(a.btn[bi].y)*scale,
                       l_native_extent(a.btn[bi].w)*scale,l_native_extent(a.btn[bi].h)*scale,
                       a.btn[bi].disabled?1:0);
            else if (a.btn[bi].kind == LUI_KIND_GEAR)
                printf("LGEAR|label=%s|x=%d|y=%d|w=%d|h=%d|disabled=%d\n",
                       a.btn[bi].label ? a.btn[bi].label : "", l_native_x(a.btn[bi].x) * scale,
                       l_native_y(a.btn[bi].y) * scale, l_native_extent(a.btn[bi].w) * scale, l_native_extent(a.btn[bi].h) * scale,
                       a.btn[bi].disabled ? 1 : 0);
        }
        /* WHAT THE STATUS LINE SAID, and on Setup, Settings and Install what the first
         * button would hand to lmedia, and whether the removal question would come
         * first, so a test can prove what a press would do without pressing it. */
        printf("LSTATUS|text=%s\n", a.drawn_status);
        if (a.view == L_VIEW_SETUP || a.view == L_VIEW_SETTINGS || a.view == L_VIEW_INSTALL)
            printf("LAPPLY|movies=%d|music=%d|asks=%d|game=%d|enabled=%d\n",
                   a.want_movies ? 1 : 0, a.want_music ? 1 : 0,
                   a.view != L_VIEW_INSTALL && l_m_frees(&a) > 0 ? 1 : 0,
                   a.view == L_VIEW_INSTALL ? 1 : 0, a.btn[L_BTN_PLAY].disabled ? 0 : 1);
        lui_text_free(&a.notes);
        if (a.media)
            lm_destroy(a.media);
        lu_destroy(a.up);
        db_pack_free(a.pack);
        return ok ? 0 : 1;
    }

    /* The window may be up already: a folder that needed its menu art fetched first. */
    if (!a.win && !l_open_window(&a, scale))
        return 1;
    SDL_ShowCursor(SDL_DISABLE); /* the plate draws MOUSE.SHP, the 1995 pointer */

    /* THE LAYOUT IS KNOWN BEFORE THE FIRST EVENT IS READ, not after the first frame is
     * drawn. The loop below polls events first and presents last, and l_layout used to
     * run only inside l_present -- so every event in the first pass was mapped through
     * a scale and a viewport origin of zero. Once here, with the window and the GL
     * context up, so the drawable size it asks for is the real one. */
    l_layout(&a);

    /* Ask the host what it has, at once, so the answer is usually on screen before
     * the player has finished reading the changelog they already had. */
    lu_check(a.up);

    signal_start = SDL_GetTicks();
    while (!a.quit) {
        SDL_Event e;
        Uint32 frame_start = SDL_GetTicks(), spent;
        l_m_tick(&a);
        l_set_buttons(&a);
        while (SDL_PollEvent(&e))
            l_event(&a, &e);
        if (lu_notes_changed(a.up)) {
            const char *n = lu_notes(a.up);
            if (n)
                l_set_notes(&a, n);
        }
        if (!signal_fixed)
            a.signal_ms = (unsigned)(frame_start - signal_start);
        /* Stay responsive without rendering into a minimized window. Clock time
         * continues, so restoring resumes the same calm transmission loop. */
        if (SDL_GetWindowFlags(a.win) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }
        a.frame++;
        l_draw(&a);
        l_present(&a);
        /* Thirty Hz is enough for the quiet signal and avoids depending on a
         * driver honouring vsync. Downloads/input are serviced every frame. */
        spent = SDL_GetTicks() - frame_start;
        if (spent < 33u) SDL_Delay(33u - spent);
    }

    /* A window closed in the middle of a download stops it rather than leaving a
     * worker writing into the folder the game is about to be started in. */
    if (a.media) {
        if (l_m_busy(&a))
            lm_cancel(a.media);
        lm_destroy(a.media);
    }
    lu_destroy(a.fresh); /* after the media job, which ran under its lock */
    lui_text_free(&a.notes);
    lu_destroy(a.up);
    db_pack_free(a.pack);
    db_pack_free(a.ra_pack);
    SDL_DestroyWindow(a.win);
    ls_free();
    SDL_Quit();

    /* The editor's own arguments go on the END of whatever was forwarded after
     * `--`, which on macOS is the window size the app bundle passes. Their room
     * was KEPT when the command line was read, which refuses a forwarded list
     * that would leave none: silently dropping --edit would start the renderer in
     * a mission, which looks like the game misbehaving rather than like the
     * editor failing to open. */
    if (a.play_after == 2) {
        l_game_args[l_game_argc++] = "--edit";
        l_game_args[l_game_argc++] = "--scen";
        l_game_args[l_game_argc++] = L_EDIT_SCEN;
    }

    if (a.play_after && !l_launch(a.dir,
                                  a.play_after == 2 ? L_EDIT_EXE :
                                  a.mod_sel == 1 ? L_RA_GAME_EXE : L_GAME_EXE,
                                  a.play_after == 1 && a.mod_sel == 1 ? "ra3d" : NULL,
                                  err, sizeof err)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenCNC3D", err, NULL);
        return 1;
    }
    return 0;
}
