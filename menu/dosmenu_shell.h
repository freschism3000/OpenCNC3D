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

/* ------------------------------------------------------------- THE MULTIPLAYER SCREEN *
 *  Host a game, browse for one on the LAN, and wait in the room until everybody is ready.
 *  menu/dosmp.h is the screen; this drives it, owns the SDL loop and does the networking.
 *
 *  IT IS RE-ENTRANT ON PURPOSE. Pressing SET UP MATCH answers DMS_MP_SETUP rather than
 *  opening the skirmish lobby from in here: the caller opens that screen, which it already
 *  knows how to do, and calls this again with the same MP_State. The state lives in the
 *  CALLER so a round trip through another screen cannot lose the game's name, its passcode
 *  or a lobby that is already open and has people sitting in it.
 *
 *  `setup` is the match the skirmish lobby answered with, packed for the wire, or NULL if
 *  nothing has been set up yet. `abi` is the brain's order-wire hash and `scen` the
 *  scenario's own bytes; either may be 0, which means "could not compute one" and disables
 *  that half of the compatibility check rather than failing it.
 * ------------------------------------------------------------------------------------ */
#define DMS_MP_SETUP (-3)  /* open the skirmish lobby, then call this again  */
/* A room is open, hosting or joined. Run dms_lobby_net on it: that screen IS the lobby,
   because a joiner asked to press READY has to be able to read what they are agreeing to. */
#define DMS_MP_LOBBY (-4)
/* NOT ZERO. It was 0, and 0 is also what the skirmish lobby's ordinary OK returns, so "the
   player pressed Play" and "the network says the match started" were the same value from
   the same function, and a joiner's stray Play jumped the app into a game with no match
   armed -- net_mode set, no address, and a two-minute block inside the boot. A value no
   other door uses. */
#define DMS_MP_PLAY (-5)
struct MP_State;
struct NmSetup;
/* --mpplay MS: leave the MULTIPLAYER screen on its own after MS milliseconds, so the one
   screen that CANNOT be reached without a network can still be driven by a gate. Zero, the
   default, means a person is driving and the screen waits for them.

   IT EXISTS BECAUSE THIS SCREEN SHIPPED WITHOUT A WAY TO REACH IT AUTOMATICALLY. --mpshot
   draws it with no network and no clicking, which proves the labels fit and nothing else;
   the real path -- the menu click, nb_browse_open, and the poll that runs ten times a
   second -- had no gate at all, and a freeze on that path reached a release. */
/* `grab`, if given, is called on the LEAVE frame after the screen has been drawn into
   the back buffer and BEFORE it is swapped, so the caller can read the pixels the window
   is about to show. It is a callback rather than a path because the shell has no GL:
   the app owns game_grab_png. This is the witness --mpshot never was, because a
   surface shot reads what was DRAWN and the window can be showing something else. */
typedef void (*DmsGrabFn)(void* user);
void dms_mp_autoleave(unsigned ms, DmsGrabFn grab, void* user);

/* HARNESS ONLY, and NULL in anything a person plays. One synthetic press per loop pass on
   the MULTIPLAYER screen, pushed as a real SDL event so it travels the path a hand does:
   the letterbox conversion, mp_press, mp_release. The skirmish lobby has had this since it
   shipped; this screen had only --mpshot, which draws a state it was handed and presses
   nothing, so "a row highlights and JOIN reaches a lobby" was a claim nothing could check.

   A ROW IS ADDRESSED BY NUMBER, NOT BY PIXEL. DMS_MP_ROW(n) asks the screen where its nth
   row is by walking mp_row_at, which is the same function the live click goes through, so
   a harness click cannot land somewhere a real one would not, whatever the list's
   geometry is. */
#define DMS_MP_ROW_BASE 1000
#define DMS_MP_ROW(n)   (DMS_MP_ROW_BASE + (n))
/* A SNAPSHOT STEP presses nothing: it hands the screen's state to the callback set with
   dms_mp_snap, tagged n, so a script can assert what one press did before the next press
   changes it. Its `wait` still applies, which is what lets a refill land first. */
#define DMS_MP_SNAP_BASE 2000
#define DMS_MP_SNAP(n)   (DMS_MP_SNAP_BASE + (n))
typedef struct DMS_MpStep {
    int item;   /* an MP_I_*, DMS_MP_ROW(n) or DMS_MP_SNAP(n); ignored when key or ch is set */
    int fx, fy; /* 0..1000 across the item's rectangle                        */
    int key;    /* an SDL keycode instead of a click                          */
    /* A printable character, pushed as SDL_TEXTINPUT rather than handed to mp_text, so
       the typed ROOM CODE travels the same route a keyboard's does. */
    int ch;
    /* HOLD THIS LONG BEFORE THE STEP, in milliseconds. Not tidiness: this screen asks a
       service on a timer, so a script that clicked a row on the pass after arriving would
       click an empty list every time and report that rows do not work. */
    int wait;
} DMS_MpStep;
/* The steps are the caller's and must outlive the visit. Count of 0 clears the script. */
void dms_mp_script(const DMS_MpStep* steps, int count);
/* HOW MANY STEPS ACTUALLY RAN in the visit that just ended. A script whose last press was
   refused leaves the screen sitting there until the watchdog fires, and the watchdog's
   answer is DMS_CANCEL -- which is also what a correct ESC gives, so without this a leg
   could pass on the timeout it was meant to fail on. */
int  dms_mp_script_ran(void);
/* HARNESS ONLY: who is handed the state at each DMS_MP_SNAP step. NULL clears it. */
typedef void (*DmsMpSnapFn)(const struct MP_State* st, int n, void* user);
void dms_mp_snap(DmsMpSnapFn fn, void* user);

/* REMEMBER THE GAME LIST'S SORT, ITS HIDE BOXES AND THE PLAYER'S NAME ACROSS LAUNCHES, in
   the settings file at `path`. Reads it into `st` now, if there is one, and from then on
   every visit to the screen writes it when one of those values has changed, and only
   then. A missing or damaged file leaves the defaults in place and says nothing. NULL or
   "" turns remembering off, which is the default: the program asks for this on a player's
   launch and never on a harness's. */
void dms_mp_remember(const char* path, struct MP_State* st);

int  dms_multiplayer(DMS* s, struct MP_State* st, const struct NmSetup* setup,
                     unsigned abi, unsigned scen);

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

/* THE LOBBY, RUN AS A NETWORK LOBBY. Same screen, same controls, same layout: a joiner
   who is being asked to press READY has to be able to READ what they are agreeing to, so
   the map and the rules are drawn for everybody and refused for everybody but the host.
   `net` is 0 for an ordinary skirmish, 1 hosting, 2 joined. With a non-zero net this polls
   the match while it draws, keeps the roster's ready lights in step with the wire, and
   answers DMS_MP_PLAY when the host starts. */
int  dms_lobby_net(DMS* s, const struct SK_Map* maps, int count, const struct SK_Prev* prev,
                   struct SK_Lobby* out, DMS_LobbyProbe* probe, int net);
/* Write the menu surface to a PNG, scaled up. Returns 1 on success. */
int  dms_write_shot(DMS* s, const char* path);
void dms_close(DMS* s);

/* The rectangle an item occupies IN WINDOW PIXELS, for a harness that wants to
   click it with a real SDL event rather than call the hit test directly. */
void dms_item_window_rect(const DMS* s, int item, int* x, int* y, int* w, int* h);

/* Draw a frame WITHOUT swapping. The harness reads the back buffer between these
   two so the PNG it writes is provably the frame it is describing. */
/* "host" or "host:port" -> a host and a port, for the address a player types on the
   INTERNET sub-tab. Returns 1 on success, 0 on anything it will not guess at: an empty
   host, a half-typed "host:", a port outside 1..65535, trailing rubbish, or a second
   colon (an IPv6 literal, which net_resolve cannot use today and should be refused by
   name rather than mis-parsed). `port` is left alone when no port is given, so the caller
   seeds it with the default. Public so a self-check can drive it; nothing else calls it
   from outside. */
int dms_parse_hostport(const char* text, char* host, size_t hostcap, unsigned short* port);

/* WHAT THE PLAYER TYPED, sorted into the two things it can be. A leading '#' is a ROOM
   CODE and is decoded here; anything else is a host[:port]. The two grammars are disjoint
   by construction rather than by guesswork -- "K7M3QX" is a valid DNS label, so without
   the sigil a room code would reach getaddrinfo and die as a failed name lookup, which is
   a baffling error for a correct code.
   Returns DMS_ADDR_NONE when it is neither, and the caller says so rather than sending
   the player to knock on a door they did not name. */
enum { DMS_ADDR_NONE = 0, DMS_ADDR_HOST, DMS_ADDR_ROOM };
int dms_parse_target(const char* text, char* host, size_t hostcap,
                     unsigned short* port, unsigned long* room_id);

void dms_redraw(DMS* s);   /* 320x200 surface -> texture */
void dms_draw(DMS* s);     /* texture -> back buffer, full GL state set */

#ifdef __cplusplus
}
#endif

#endif /* DOSMENU_SHELL_H */
