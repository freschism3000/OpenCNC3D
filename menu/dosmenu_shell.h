/* ====================================================================================
 * dosmenu_shell.h -- the DOS main menu as something a program can VISIT, not as a
 * program of its own.
 *
 * preview.c used to be the menu: it opened the window, made the context, owned the
 * event loop and returned to the shell only by exiting. In the shipping build the
 * menu is one screen among several, so everything it used to own now belongs to the
 * caller and it borrows:
 *
 *      dms_open   attach to a window whose GL context is already current
 *      dms_logo   play LOGO.VQA once, at program start, and fade into the menu
 *      dms_run    one visit: draw, take input, return the item that was chosen
 *      dms_close  give back the texture, the audio device and the pack
 *
 * dms_run may be called any number of times. Coming back from a mission is just
 * another call.
 *
 * The 320x200 surface is presented letterboxed at the largest whole-number scale
 * that fits the window, so the menu and a 1280x720 tactical view can share one
 * window without either of them being resampled.
 * ==================================================================================== */

#ifndef DOSMENU_SHELL_H
#define DOSMENU_SHELL_H

#include "dosmenu.h"

#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* dms_run returns one of the DM_* items, or one of these. */
#define DMS_QUIT (-1)  /* the window was closed: close the program        */
/* CANCEL IS NOT QUIT, and until v0.6.0 it was the same number. The Special Ops list
   returned -1 for its Cancel button and for ESC, the caller tested that against
   DMS_QUIT, and pressing Cancel therefore closed the whole game -- exactly as reported.
   The comment below this line has always said the two are different; only the values
   disagreed. */
#define DMS_CANCEL (-2) /* the user backed out: return to the menu, do NOT exit */
#define DMS_NONE (-2)  /* dms_run_frame only: nothing decided this frame  */

typedef struct DMS_Config {
    const char* pack;      /* dosmenu.pack                                  */
    const char* music;     /* theme BASE NAME, e.g. "MAP1"; NULL = silence  */
    const char* intro;     /* INTRO2.VQA for the Intro button, or NULL      */
    const char* logo;      /* LOGO.VQA for dms_logo, or NULL                */
    const char* version;   /* the version string printed on the plate       */
    /* Harness only: dump the movie's own frames as PNG, and stop it early so a gate
       does not spend two minutes watching the intro. NULL/0 in any build anyone plays. */
    const char* movie_shotdir;
    int movie_stop_after;
    /* The one audio engine the program owns, from audio/audioboot.h. The menu does
       NOT open a device of its own. It did once, and a second device alongside the
       tactical view's is how you end up with two mixers fighting over one sound
       card and a menu whose music survives into the mission. NULL is legal and
       means the menu is silent. */
    struct CncAudio* au;
} DMS_Config;

struct DO_State;
struct SK_State;
struct SK_Map;
struct SK_Prev;
struct SK_Lobby;

typedef struct DMS {
    SDL_Window* win;
    GLuint tex;
    int tw, th;                 /* power of two texture holding the 320x200 */
    int scale, vpx, vpy;        /* letterbox, recomputed every frame        */
    float px, py;               /* drawable pixels per window point         */
    DB_Pack* pack;
    DB_Surface surf;
    DM_State st;
    unsigned char screen[DM_SCREEN_W * DM_SCREEN_H];
    unsigned char* rgba;
    unsigned char* padded;
    unsigned char* faded;
    int mx, my;                 /* pointer, in 320x200 menu pixels          */
    DMS_Config cfg;
    /* When set, dms_redraw draws the SPECIAL OPS list over the plate instead of the
       main menu's buttons. Owned by dms_special for the length of its own loop and
       NULL everywhere else, so the ordinary menu path is untouched. */
    const struct DO_State* ops;
    /* The same seam again for the skirmish lobby, and a second pointer rather than a
       mode number for the same reason: whichever one is set is the screen, and with
       both NULL the ordinary menu path cannot be affected by either. */
    const struct SK_State* lobby;
} DMS;

int  dms_open(DMS* s, SDL_Window* win, const DMS_Config* cfg, char* err, int errlen);
int  dms_logo(DMS* s);         /* 1 played/skipped, 0 the window was closed */
int  dms_intro(DMS* s);        /* the same, for the Intro & Sneak Peek button */
int  dms_run(DMS* s);          /* DM_* item, or DMS_QUIT                    */

/* The SPECIAL OPS mission list. Returns the index of the mission the player chose,
   DMS_CANCEL for Cancel/ESC, or DMS_QUIT if the window was closed. The caller owns the list
   (it is the one that can read a directory); see menu/dosops.h. */
struct DO_Mission;
int  dms_special(DMS* s, const struct DO_Mission* list, int count);

/* THE SKIRMISH LOBBY (menu/doslobby.h). Unlike every other screen here it does not
   answer with an index: it answers with a whole settings block, because it asks ten
   questions rather than one. Returns 0 with *out filled, DMS_CANCEL for Cancel or ESC,
   or DMS_QUIT if the window was closed.

   `prev` is a loaded mappreview.pack or NULL; the panel then says it has no picture and
   everything else on the screen still works. The caller owns both the map list and the
   pack, the same way it owns the mission list. */

/* HARNESS ONLY, and NULL in anything a human plays. One synthetic click per loop pass,
   pushed as a real SDL event on the item's own rectangle so it travels the same path a
   hand does -- the letterbox conversion, the press, the release over the same control.
   fx and fy are thousandths across that rectangle, so a gauge can be driven to a
   fraction of travel and a list to a row. */
typedef struct DMS_LobbyStep {
    int item;   /* an SK_Item; ignored when `key` is set               */
    int fx, fy; /* 0..1000 across the item's rectangle                 */
    /* An SDL keycode instead of a click, so the keyboard walk is driven through the
       same SDL_KEYDOWN a keyboard produces rather than by calling sk_key directly. */
    int key;
} DMS_LobbyStep;

typedef struct DMS_LobbyProbe {
    const DMS_LobbyStep* script;
    int steps;
    /* A PNG of every frame the lobby draws, at 3x, from the 320x200 surface itself
       rather than from the back buffer: what is written is provably the pixels the
       module rasterised. NULL writes nothing. */
    const char* shotdir;
    int shots;    /* out: how many were written                                */
    int overflow; /* out: strings that do not fit their box; -1 = unmeasurable  */
} DMS_LobbyProbe;

int  dms_lobby(DMS* s, const struct SK_Map* maps, int count, const struct SK_Prev* prev,
               struct SK_Lobby* out, DMS_LobbyProbe* probe);
void dms_close(DMS* s);

/* The rectangle an item occupies IN WINDOW PIXELS, for a harness that wants to
   click it with a real SDL event rather than call the hit test directly. */
void dms_item_window_rect(const DMS* s, int item, int* x, int* y, int* w, int* h);

/* Draw a frame WITHOUT swapping. The harness reads the back buffer between these
   two so the PNG it writes is provably the frame it is describing. */
void dms_redraw(DMS* s);   /* 320x200 surface -> texture */
void dms_draw(DMS* s);     /* texture -> back buffer, full GL state set */

#ifdef __cplusplus
}
#endif

#endif /* DOSMENU_SHELL_H */
