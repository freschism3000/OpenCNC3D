/* ====================================================================================
 * campaign.h -- the 1995 campaign-flow screens: side select, score, map selection.
 *
 * Everything here is the DOS original transcribed: art from campaign.pack (baked by
 * game/bake_campaign.py out of the CD's TRANSIT/GENERAL/CONQUER MIX files), layout,
 * click regions and sequencing from the GPL sources (intro.cpp Choose_Side,
 * score.cpp ScoreClass::Presentation, mapsel.cpp Map_Selection). The screens borrow
 * the app's window and GL context the same way the menu does: 320x200 indexed plate,
 * one texture, integer-scaled letterbox, full GL state set every frame.
 *
 * The score tally and the map screen are BUILT (wave 2): the full ScoreClass
 * presentation (typed labels, count-ups with BEEPY6, TIME/HISCORE/CREDS shape
 * loops with CLOCK1/CASHTURN, BAR3YLW/BAR3RED kill bars, palette-cycling CLICK TO
 * CONTINUE) and the full Map_Selection (GREYERTH + E-BWTOCL spin-in, EARTH_E with
 * its per-frame sound score, EUROPE territory advances with pixel dissolves, the
 * ContAnim crosshair reel, palette-249..254 territory blink, CLICK_E hit map).
 * Anims ship as the disc's own LCW'd XOR-delta streams and are decoded here.
 *
 * The score screen's hall of fame is BUILT (wave 3): the 140-byte HALLFAME.DAT in
 * the 1995 record layout, the TOP SCORES board, and ScoreClass::Input_Name with its
 * ScoreScaleClass zooming letters, blinking LTBLUE cursor and KEYSTROK.AUD.
 *
 * Simplifications still standing are RECORDED as known gaps (the last-scenario
 * BOSNIA reel, the Nod map reels, Nod score-screen graphs).
 *
 * One deliberate deviation from 1995, also registered: the side select's STRUGGLE.AUD
 * bed is a LOOPING voice (mixer_play_loop) where intro.cpp re-triggers the sample.
 * On a block-based mixer a re-trigger leaves a measured 45.0 ms hole at every seam.
 * ==================================================================================== */

#ifndef CAMPAIGN_H
#define CAMPAIGN_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "logo3d.h"

struct CncAudio;

/* The numbers the ENGINE hands over in its game-over event
   (dllinterface.h GameOverEvent; Calculate_Single_Player_Score fills them).
   `minutes` is the elapsed mission time the score screen's TIME field counts
   (game/cnc_game.h GameOverInfo carries it); 0 means "not wired by the caller"
   and prints the 1995 minimum, which is 1m (score.cpp:659 adds 1). */
typedef struct CampScore {
    int score, leadership, efficiency;
    int nod_killed, gdi_killed, civ_killed;
    int nod_bldg, gdi_bldg, civ_bldg;
    int credits;
    int minutes;
    int win;
    char movie[16];         /* the win/lose movie the event named (no extension) */
} CampScore;

/* One campaign.pack entry (format CNC3DCPN v2, game/bake_campaign.py).
   kind 0 anims carry the disc's own LCW'd XOR-delta chunks and an accumulator
   that camp code advances frame by frame; kinds 1/2 carry raw indexed frames;
   kind 3 is a bare 768-byte palette. */
typedef struct CampEntry {
    char name[16];
    int kind, frames, w, h, x, y;
    unsigned char pal[768];      /* 6-bit VGA as on disc */
    unsigned char *px;           /* kinds 1/2: frames * w * h indices */
    /* kind 0 (delta anim) only: */
    unsigned char *chunks;       /* all LCW'd delta chunks back to back */
    unsigned int  *coff;         /* frames+1 offsets into chunks */
    unsigned char *acc;          /* w*h accumulator = the current frame  */
    int cur;                     /* frame the accumulator holds, -1 fresh */
} CampEntry;

/* The 1995 strings these screens print, read out of CONQUER.ENG in the disc's own
   LOCAL.MIX at camp_open time. The side select's three are cached in txt[]; the
   score/map screens read theirs from the retained blob by TXT_ index. */
enum { CAMP_TXT_GDI = 0, CAMP_TXT_NOD, CAMP_TXT_SEL, CAMP_TXT_COUNT };

typedef struct Camp {
    CampEntry e[24];
    int n;
    SDL_Window *win;
    struct CncAudio *au;
    void *fonts;                 /* DB_Pack* of dosmenu.pack, for its fonts */
    unsigned int tex;            /* GLuint */
    unsigned char plate[320 * 200]; /* PseudoSeenBuff: what the player sees   */
    unsigned char back[320 * 200];  /* SysMemPage. The score screen's name entry
                                       snapshots the plate here and the zooming
                                       letter restores its previous square out of
                                       it every stage (score.cpp:463's own
                                       commented DOS line). Anything else that
                                       starts using `back` inside camp_score will
                                       make the zoom smear.                     */
    const unsigned char *pal;    /* active 6-bit palette */
    unsigned char palbuf[768];   /* mutable copy for cycling; pal points here
                                    whenever a screen cycles or fades         */
    int fade;                    /* 0..256 brightness (palette fade), 256 lit */
    char mapdir;                 /* ScenDir ('E'/'W'): mapsel.cpp indexes
                                    CountryArray[scenario][ScenDir] with the
                                    direction chosen LAST time. Reset to 'E'
                                    by camp_side_select (campaign start).     */
    unsigned char *eng;          /* CONQUER.ENG blob (string table), or NULL  */
    long englen;
    char txt[CAMP_TXT_COUNT][48];/* the disc's strings; empty when txt_ok is 0 */
    int txt_ok;
    Uint32 fakenow;              /* autopilot: synthetic 60 Hz clock (advanced
                                    one tick per camp_pump) so the flow gates
                                    run the full 1995 sequence fast AND
                                    deterministically                          */
    int mx, my;                  /* the DOS pointer in 320x200 PLATE coords;
                                    -1 means "never seen the mouse yet"        */
    int curon;                   /* 1995's Show_Mouse/Hide_Mouse state for the
                                    screen running now. The HOST pointer is off
                                    for the whole app; these screens draw
                                    MOUSE.SHP into the plate copy themselves.  */
    /* Keyboard->Get()'s buffer, drained ONLY by the score screen's name entry;
       every other screen keeps using camp_pump's `keyed` flag. camp_pump pushes
       printable bytes from SDL_TEXTINPUT and '\b'/'\r' from SDL_KEYDOWN -- SDL
       never delivers those two as text, and taking printables from KEYDOWN as
       well would enter every letter twice. */
    char keyq[32];
    int  keyqn;

    /* THE SPINNING FACTION LOGO the score screen lays over its finished plate.
       logo_side is -1 everywhere else, which is what keeps it off the side
       select and the map screen -- camp_draw runs for all three. logo_t0 is the
       camp_now() the score screen opened at, so the angle is a function of that
       screen's OWN clock: under --flowtest camp_now is the synthetic 60 Hz
       fakenow, so a flow run spins the logo to the same angle every time and the
       shot gates stay byte-comparable. */
    Logo3D       logo;
    int          logo_ok;
    int          logo_side;
    Uint32       logo_t0;
} Camp;

/* Harness autopilot: when set, the screens drive themselves (side select picks GDI
   after a beat, the score screen continues after its animation, the map screen picks
   choice 0) so the whole flow can run hands-off in CI. Zero in normal play. */
extern int camp_autopilot;
/* 0 = GDI, 1 = Nod: which rectangle the side-select autopilot clicks (--flowside). */
extern int camp_autopilot_side;

/* --famefile PATH: where HALLFAME.DAT lives. NULL (the default) means the platform's
   per-user data directory via SDL_GetPrefPath -- the playable directory is read-only
   on a packaged build and shared by concurrent gate runs.

   A plain camp_autopilot run reads and writes NO fame file at all (the board's length
   would otherwise depend on the player's history and the flow PNGs would stop being
   reproducible). Setting this explicitly opts the persistent path back in even under
   autopilot, so a gate can drive it against its own scratch file. */
extern const char *camp_fame_file;

int  camp_open(Camp *c, SDL_Window *win, struct CncAudio *au,
               const char *campack, const char *menupack, char *err, int errlen);
void camp_close(Camp *c);

/* Choose_Side: returns 0 GDI, 1 Nod, -1 window closed. */
int camp_side_select(Camp *c);

/* ScoreClass::Presentation: the side's full score tally with the engine's numbers.
   Returns 0 on click/key at the end, -1 window closed. */
int camp_score(Camp *c, const CampScore *s, int side, int scenario);

/* Map_Selection: globe spin-in, EARTH_E, EUROPE advances, territory blink, click.
   scenario is the one JUST WON (its CountryArray row names the choices for the
   NEXT mission). Fills *dir ('E'/'W') and *var ('A'..'C'). The CURRENT direction
   (1995's ScenDir) is tracked inside Camp because the caller passes a fresh 'E'
   each time. Returns 0 chosen, -1 window closed. */
int camp_mapsel(Camp *c, int side, int scenario, char *dir, char *var);

#ifdef __cplusplus
}
#endif

#endif /* CAMPAIGN_H */
