#ifndef CNC_ENHANCED_MENU_H
#define CNC_ENHANCED_MENU_H
#ifdef __cplusplus
extern "C" {
#endif
#include "dosmenu.h"
#include "dosops.h"
#include "doslobby.h"
#include "dosmp.h"
#include "dosopt.h"
/* Art and hit seats share native pixels. The legacy controllers still own actions. */
int game_enhanced_menu_enabled(void);
void game_enhanced_menu_sound(int click);
union SDL_Event;
struct SDL_Window;
/* Shared pointer feedback for every Enhanced menu controller. */
void em_sound_event(const union SDL_Event *event, struct SDL_Window *window);
int em_available(void);
/* Draw Red Alert's Enhanced kit (ra3d/enhanced-menu.pack) instead of Tiberian Dawn's, with
   this version line on the main menu and this legal notice under it. Once per process,
   before the first screen. */
void em_set_red_alert(const char *version, const char *legal);
int em_red_alert(void);
/* The side of the Red Alert mission being played (0 Allied, 1 Soviet): its briefing paper. */
void em_set_mission_side(int soviet);
/* Red Alert's routes whose engine side is not connected yet. A route not marked ready keeps
   its button and answers with em_route_notice's text in a notice box. Always ready for
   Tiberian Dawn. */
enum { EM_ROUTE_SAVES, EM_ROUTE_SKIRMISH, EM_ROUTE_NETWORK, EM_ROUTE_USERMAPS, EM_ROUTE_DATABASE, EM_ROUTE_COUNT };
void em_set_route_ready(int route, int ready);
int em_route_ready(int route);
const char *em_route_notice(int route);
int em_density(void);
void em_main(const DM_State *st);
int em_main_next(const DM_State *st, int item, int delta);
void em_ops(DO_State *st);
void em_ops_move(DO_State *st, int delta);
void em_ops_filter(DO_State *st, int category);
void em_lobby(const SK_State *st, const DB_Pack *p);
void em_browser(const MP_State *st);
void em_options(DOPT_State *st);
void em_title_mode(int enabled);
/* THE LANGUAGE ROW of the Settings screen's Gameplay tab and its download box, answered by
   the game (game/lang_mod.h through cnc_eyes.cpp). Language 0 is English. Without hooks
   (a gate, a tool) the row is not drawn. */
typedef struct {
    int (*count)(void);                         /* languages, English included          */
    const char *(*name)(int i);                 /* "English", "Deutsch"                  */
    int (*current)(void);
    int (*installed)(int i);                    /* its pack complete on the disk         */
    long long (*missing)(int i);                /* bytes still to fetch                  */
    int (*choose)(int i);                       /* use it now (installed or English)     */
    int (*download)(int i);                     /* start fetching its pack               */
    int (*progress)(long long *done, long long *total, char *msg, int msglen);
    void (*cancel)(void);
} EM_LangHooks;
void em_set_language_hooks(const EM_LangHooks *hooks);
void em_viewport(int width, int height);
void em_draw(int width, int height, int clear);
/* Menu-owned overlay; coordinates are drawable pixels, negative hides it. */
void em_menu_overlay(const DB_Pack *pack, int x, int y);
void em_to_legacy(int fx, int fy, int *x, int *y);
int em_item_rect(int item, int *x, int *y, int *w, int *h);
int em_seat_point(const char *label, int *x, int *y);
int em_opt_press(DOPT_State *st, int x, int y);
int em_opt_motion(DOPT_State *st, int x, int y);
int em_opt_release(DOPT_State *st, int x, int y);
int em_sk_press(SK_State *st, int x, int y);
int em_sk_motion(SK_State *st, int x, int y);
int em_sk_release(SK_State *st, int x, int y);
int em_mp_press(MP_State *st, int x, int y);
void em_mp_motion(MP_State *st, int x, int y);
int em_ops_hit(const DO_State *st, int x, int y);
void em_ops_action(DO_State *st, int hit);
/* RGBA witness of the native scene, for the existing hidden menu harness. */
int em_write_shot(const char *path, int width, int height);
void em_scroll(int delta);
void em_opt_scroll(DOPT_State *st, int delta);
int em_opt_key(DOPT_State *st, int key);
int em_mp_row_at(const MP_State *st, int x, int y);
void em_reset(void);
void em_choice(int which, int selected, int pressed);
int em_pick(int fx, int fy);
/* Red Alert's side choice (em_side_choice) and the items em_pick returns on it. */
enum { EM_SIDE_BACK = 0, EM_SIDE_ALLIES = 1, EM_SIDE_SOVIET = 2, EM_SIDE_START = 3,
       EM_SIDE_EASY = 10, EM_SIDE_NORMAL = 11, EM_SIDE_HARD = 12 };
void em_side_choice(int side, int difficulty, int pressed);

/* RED ALERT'S MATCH DEBRIEF, after a skirmish or a network match: the 1996 multiplayer
 * score's three columns (each commander in its colour with its country, battles won this
 * session, kills in each of the session's last four games) in Red Alert's title kit, the
 * result and the map above, this machine's own numbers for the match below. Any click or
 * key continues, as on the 1996 screen; em_pick names EM_DEBRIEF_CONTINUE for its button. */
enum { EM_DEBRIEF_CONTINUE = 1 };
/* The Settings screen's Gameplay and Audio seats: EM_SETTING_SEAT - DOPT_ES_* for a slider,
   EM_GAMEPLAY_SEAT - n for the nth input switch. */
enum { EM_SETTING_SEAT = -300, EM_GAMEPLAY_SEAT = -320 };
/* the language row (EM_LANG_SEAT), its list (EM_LANG_SEAT - 1 - n for language n), the box's
   buttons (EM_LANG_SEAT - 20 .. - 22), and the list's own id in View::drop */
enum { EM_LANG_SEAT = -340, EM_LANG_DROP = -40 };
typedef struct EM_DebriefRow {
    char name[16];
    int country;      /* 0..7: Spain Greece USSR England Ukraine Germany France Turkey */
    int colour;       /* 0..7, the player colours                                      */
    int wins;         /* battles won this session                                      */
    int kills[4];     /* kills in each of the session's last games, oldest first; -1 none */
    int defeated;     /* beaten in this match                                          */
    int is_local;     /* the commander at this machine                                 */
} EM_DebriefRow;
typedef struct EM_Debrief {
    EM_DebriefRow row[8];
    int nrows;
    int games;        /* kills[] columns in use, 1..4; the last is this match          */
    int win, decided; /* this machine won; somebody was beaten (0: the match was left) */
    int seconds;      /* the match's length                                            */
    int net;          /* a network match                                               */
    char mapname[48];
    char ended[96];   /* why it ended, when it broke rather than finished              */
    int killed, razed, lost, harvested;   /* this machine's commander, this match      */
} EM_Debrief;
void em_ra_debrief(const EM_Debrief *d, int pressed);
typedef struct {
 int category, house, selected, top, count, detail;
 const char **names;
 const char *name, *code, *brief, *armour, *strong, *weak, *unlocks;
 int cost, hp, damage, speed, rof, range256, model_available;
 /* Red Alert: the type's 1996 cameo (RGBA), drawn when it has no Enhanced model */
 const unsigned char *cameo; int cameo_w, cameo_h;
} EM_Database;
void em_database(const EM_Database *st);
int em_database_view(int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif
#endif
