/* ====================================================================================
 *  cnc_game.h -- what the application shell is allowed to know about the game.
 *
 *  cnc_eyes.cpp used to be a program: it parsed argv, opened a window, booted the
 *  brain, ran until you pressed ESC and then exited the process. In the shipping
 *  build the menu is what owns the program, and a mission is something that starts
 *  and ends inside a window that already existed and will still exist afterwards.
 *
 *  These five calls are that seam, and nothing else crosses it. The shell owns
 *  SDL, the window and the GL context; the game owns the brain, its packs and its
 *  frames while it is running, and hands every one of them back on the way out.
 *
 *      game_parse_args   argv -> GameOpts. Reads no files, opens nothing.
 *      game_boot         brain + packs + sidebar, into a window that exists.
 *      game_loop         run until the player leaves. Draws and swaps.
 *      game_shutdown     give back every texture and every allocation from boot.
 *
 *  boot -> loop -> shutdown may be repeated any number of times on one context.
 *  That is a tested property, not a hope: see harness.c.
 * ==================================================================================== */

#ifndef CNC_GAME_H
#define CNC_GAME_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the command line decides. Plain pointers into argv, which outlives
   the program, so nothing here owns memory. */
typedef struct GameOpts {
    const char* dylib;      /* NULL -> probe the usual places      */
    const char* dir;        /* mission directory                   */
    const char* content;    /* MIX directory                       */
    const char* scen;       /* SCG01EC and friends                 */
    const char* pack;       /* terrain/model pack; NULL -> scen.pack */
    const char* cam;        /* "X,Z" start position, or NULL       */
    const char* cameos;
    const char* dospack;
    const char* dosinf;     /* NULL = keep the N64 infantry        */
    const char* dosmake;    /* NULL = buildings pop in finished    */
    const char* efx;
    const char* dostib;     /* real tiberium art pack; missing = crystals  */
    const char* tib3d;      /* solid crystal clumps, Enhanced; missing = flat only */
    const char* tree3d;     /* leafy trees, Enhanced; missing = the cartridge model */
    int         texset;     /* --texset: <0 = leave the cartridge's ground alone   */
    int         enhanced;   /* --enhanced: the PLAYER's Enhanced picture, not just the chain */
    const char* doscrate;   /* crate art pack; missing = crates unseen     */
    const char* smudge;     /* scorch marks, craters, building aprons     */
    const char* verdict;    /* N64 MISSION ACCOMPLISHED/FAILED banners     */
    const char* shot;       /* headless PNG mode                   */
    int hidden;              /* a --script run implies this; --showwindow opts out */
    const char* script;     /* headless script mode                */
    const char* posetest;
    /* Sound. Where the 1995 archives are, and where the mix comes out. */
    const char* dosdata;    /* SOUNDS.MIX and friends; NULL -> "dosdata"    */
    const char* audiowav;   /* record the mix to this WAV, open no device   */
    int nosound;            /* open nothing and make no noise               */
    int musicvol, soundvol; /* the 1995 Game Controls sliders, 0..255       */
    /* HARNESS ONLY (--flowtest): after this many ticks with no real verdict,
       synthesize a WIN game-over event so the post-mission flow (win movie,
       score, map, next briefing) can run hands-off. 0 in every real game. */
    int forcewin_ticks;
    int build;              /* build level                         */
    /* A SAVED GAME TO LOAD once the mission is up: the slot number PLUS ONE, so a
       zero-filled GameOpts asks for nothing. game_boot boots the scenario as usual and
       then game_load_slot puts the saved world into it; the shell sets this from the
       slot's own record (scen and pack come from the same record) for the main menu's
       Load Mission and for a pause-dialog load of another mission's slot. */
    int load_slot1;
    /* THE MAP EDITOR. Boots the world and does not start it: the mission loads, the
       pack draws, and the sim stays frozen so the map can be worked on. Play is the
       same window -- game_shutdown() then game_boot() on what was just written, which
       is the pair `remission` already uses. See docs/design-map-editor.md. */
    int edit;
    /* RE-SKIN A BORROWED PACK. A user map has no baked terrain of its own; it borrows a
       pack of its theater and is repainted from its own .BIN and .HGT at boot. The
       atlas in any pack is the whole THEATER's tile bank, so every cell it needs is
       already there -- this is the same thing the editor does to make a blank map. */
    int reskin;
    /* ---- SKIRMISH ---------------------------------------------------------------
       Zero here is the campaign path, which is what every existing caller wants and
       what the whole game did before skirmish existed. When skirmish is set the game
       is started as a multiplayer instance with computer opponents, and the fields
       below describe the lobby. */
    int skirmish;           /* 1 = play a skirmish against the computer     */
    int side;               /* the side the HUMAN plays: 0 GDI, 1 Nod       */
    int ai_count;           /* computer opponents, 1..7 (8-player patch)    */
    int credits;            /* starting credits, every house alike          */
    int tiberium;           /* 1 = tiberium grows and spreads               */
    int crates;             /* 1 = bonus crates are scattered               */
    int ai_takeover;        /* 1 = the computer takes over a player who leaves */
    int short_game;         /* 1 = defences and walls do not keep a player alive */
    int superweapons;       /* 1 = superweapons may be built                */
    /* Bases OFF is a real 1995 mode and it is not offered yet: with no base and no
       MCV the early-win rule declares every house dead one second into the match, so
       the two settings have to be interlocked before it can be. */
    int bases;              /* 1 = everyone starts with an MCV and builds   */
    /* Which map waypoint each player starts on. Always set explicitly rather than
       left to the engine: its own random pick can only reach the first six waypoints,
       and it shuffles with a wall-clock seed, which costs reproducibility for nothing. */
    int start_wp[8];   /* one per player, human first (8-player patch) */
/* THE TWO ANSWERS A SEAT'S START CAN BE BESIDES A WAYPOINT. UNPICKED is what the lobby
   holds for a seat nobody chose a start for; on the way to the engine it becomes RANDOM,
   which is the brain's own RANDOM_START_POSITION (dllinterface.cpp) and makes it deal
   that seat a start out of whatever is left, with the synchronised random stream, so
   every peer deals the same one. The lobby's own copy is SK_START_RANDOM. */
#define CNC3D_START_UNPICKED (-1)
#define CNC3D_START_RANDOM 0x7f
    /* WHO EACH SEAT IS, human first, filled for the first ai_count + 1 entries.
       These carry the lobby's per-player drop down and they are the only reason the
       match is not always "the human's side against everybody else":

         player_house  0 GDI, 1 Nod. It becomes CNCPlayerInfoStruct::House, which
                       CNC_Set_Multiplayer_Data folds into MPlayerID and which
                       GlyphX_Assign_Houses turns into the house's ActLike -- the side
                       its army wears and builds from.
         player_team   ZERO BASED and compared for equality only. It becomes
                       CNCPlayerInfoStruct::Team -> MPlayerTeamIDs[] and
                       GlyphX_Assign_Houses calls Make_Ally for every pair that shares
                       one (dllinterface.cpp:1044-1058). Distinct numbers, which is the
                       lobby's default, mean a free-for-all.
         player_colour PlayerColorType, 0..7, in the engine's own order
                       (defines.h:705-720). It becomes CNCPlayerInfoStruct::ColorIndex,
                       which CNC_Set_Multiplayer_Data packs into MPlayerID together with
                       the house (dllinterface.cpp:750, Build_MPlayerID) and
                       GlyphX_Assign_Houses unpacks again to call HouseClass::Init_Data
                       (:982) -- which is what sets that house's RemapColor, RemapTable,
                       Color and BrightColor. It must be DISTINCT for every seat, and
                       that is exactly what the lobby guarantees by keeping its eight
                       entries a permutation of 0..7: GlyphX_Assign_Houses keeps a
                       color_used[] table, so a duplicate would have two houses claim
                       one slot.
                       IT IS CARRIED HONESTLY AND IT DOES NOT YET PAINT. This renderer
                       takes its texture set and its identity colours from the SIDE
                       (obj_is_gdi, house_colour, band_house_colour), never from the
                       house's remap, so two seats on one side still look alike on the
                       field whatever they picked. The number is right; the pixels are
                       still the 1995 two-texture-set arrangement.

       Zero-filled means "seat 0 GDI, everybody on team 0 and everybody colour 0", which
       is every player allied with every other wearing one colour, and is NOT a sane
       default. Every caller that sets skirmish must therefore set these too;
       game_parse_args does it for the switches and skirmish_apply does it for the
       lobby. */
    int player_house[8];
    int player_team[8];
    int player_colour[8];
    /* WHAT EACH SEAT IS FOR, in NM_SEAT_* terms (0 a person, 1 a computer, 2 open, 3
       blocked). All zero means "not said", and the roster is then the prefix rule it
       always was: humans first, computers after. A network match started from the lobby
       fills this from the setup that travelled, and the engine roster is built seat by
       seat from it -- a BOT can sit at 1 with a person at 2, and a BLOCKed seat is
       simply not handed to the engine at all. */
    int seat_mode[8];
    /* WHO IS IN EACH SEAT, eleven characters plus NUL, which is the engine's own
       MPLAYER_NAME_MAX. Empty means "not said", and the seat falls back to PLAYER or
       COMPUTER exactly as it always did, so a command line that names nobody plays
       as before. */
    char player_name[8][12];
    /* How many escort units each house starts with beside its MCV. It is not a taste
       setting: the engine scatters the escort within about four cells of the MCV, and any
       unit that lands inside the 3x3 Construction Yard pad makes the player's very first
       click do nothing at all. Which counts are safe is a measured property of the engine's
       own scatter, and no value is safe everywhere. Zero is the measured best. */
    int unit_count;
    /* ---- A NETWORK MATCH (Phase 3). 0 is no network, which is every path above. 1 hosts
       on net_port and waits for net_players - 1 joiners; 2 joins net_addr:net_port. Either
       implies skirmish, and the joiner's own skirmish settings lose to what the host sends.
       net_players is the HOST'S alone: a joiner is told how many there are and cannot
       argue, which is the same rule every other field in the setup follows. */
    int net_mode;
    const char* net_addr;
    int net_port;
    int net_players;        /* --players N, humans in the match, 2..8, host only */
    int ticks;              /* --ticks, warm-up for shot/script    */
    int w, h;               /* requested framebuffer size          */
    int dumpobj;
    int picktest;
    int radar_off;
    int radar_strict;
    int radar_force;   /* --forceradar: show the radar without a Comm Center (gates) */
} GameOpts;

/* Why game_loop came back. The shell decides what to do about it; the game does
   not call exit(). */
enum {
    GAME_EXIT_MENU = 0,     /* ESC, or --run elapsed: show the menu again */
    GAME_EXIT_APP = 1,      /* the window was closed: close the program   */
    GAME_EXIT_ERROR = 2,    /* the brain stopped advancing, or an assertion failed */
    GAME_EXIT_WON = 3,      /* the engine declared the mission won (game-over event) */
    GAME_EXIT_LOST = 4,     /* the engine declared it lost */
    /* Restart Mission from the abort confirmation. The shell re-enters the SAME scenario
       without replaying the mission briefing, which is what 1995's Do_Restart does: it calls
       Start_Scenario with briefing = false (scenario.cpp:728). An enum value rather than
       a field on a diagnostic line, so no gate pattern is disarmed by it. */
    GAME_EXIT_RESTART = 5,
    /* THE MATCH BROKE, WHICH IS NOT THE SAME THING AS THE GAME BREAKING, and folding the
       two into GAME_EXIT_ERROR is why a desync used to close the application. A parted
       simulation and a peer that has said nothing for thirty seconds are ordinary things
       for a network match to end on: the player has just spent twenty minutes on it and
       is owed the same debrief anybody else gets, and then the menu. GAME_EXIT_ERROR
       still means what it always meant -- the brain stopped advancing, an assertion
       failed -- and still returns a failing exit code, because that is the one the gates
       and the harness read. The two had to be separated before either could be handled
       correctly. game_net_fail() carries the sentence to put in front of the player. */
    GAME_EXIT_NETLOST = 6,
    /* LOAD MISSION named a slot saved in ANOTHER mission. This process can only load
       a slot into the mission it booted, so the shell reads game_pending_load_slot,
       looks the slot's scenario and pack up in the index, and boots that mission with
       GameOpts::load_slot1 set. Not an error and not a verdict. */
    GAME_EXIT_LOADSLOT = 7
};

/* The slot a GAME_EXIT_LOADSLOT asked for, once; -1 when none is pending. */
int game_pending_load_slot(void);

/* THE MAIN MENU'S LOAD MISSION: the slot dialog on the shell's own window, modal,
   the way game_visuals_open runs the Visuals page. Returns the slot the player chose,
   or -1 for Cancel, or -2 when the window was closed. The shell then boots the slot's
   mission with load_slot1 set. `shot` works as it does for game_visuals_open: non-NULL
   draws one frame, writes it when non-empty, and closes without waiting for a hand. */
int game_load_menu_open(SDL_Window* win, const char* dospack, const char* shot);

/* A NOTICE with a caption, a few lines of text and a lone OK, modal on the shell's own
   window over black, drawn by the same dialog module as the pause menu. The shell uses
   it to say why a mission start was refused before it puts the menu back. Returns 0
   when dismissed, -2 when the window was closed. `shot` works as it does for the slot
   dialog above. */
int game_notice_open(SDL_Window* win, const char* dospack, const char* caption,
                     const char* text, const char* shot);

/* WHY THE LAST game_boot RETURNED 0, as one sentence a player can act on, or "" when
   it did not (or has not run). Set by every refusal on the boot path, printed as a
   BOOTFAIL| line on stderr as it is set, and cleared at the start of the next boot. */
const char* game_boot_refusal(void);

/* Why a network match ended, in words, or "" if it did not end that way. Valid from the
   moment game_loop returns GAME_EXIT_NETLOST until the next mission boots. */
const char* game_net_fail(void);

/* The sound engine is owned by the PROGRAM, not by a mission: one bank and one
   device for the whole process, shared by the menu, the movies and the tactical
   view. The shell creates it (audio/audioboot.h) and lends it here, so a mission
   that starts and ends does not take the sound card with it. NULL is legal and
   means silence; every audio call in the renderer is a no-op then. */
struct CncAudio;
void game_set_audio(struct CncAudio* au);

int  game_parse_args(int argc, char** argv, GameOpts* out);   /* 0 ok, 2 usage printed */
int  game_boot(SDL_Window* win, const GameOpts* o);           /* 1 ok, 0 failed        */
int  game_loop(SDL_Window* win, const GameOpts* o);           /* GAME_EXIT_*           */
void game_shutdown(void);

/* The Visuals screen, run modally on the shell's own window. The main menu has no DOS
   pack and no 8-bit surface of its own, so it asks for the screen rather than building a
   second copy of it. Returns 1 if the window was closed while it was up. */
/* The main menu's Visuals screen. `shot`, when non-NULL, makes it a MEASURING
   INSTRUMENT rather than a screen: it draws exactly one frame, then takes the ordinary
   close path without a key being pressed. A non-empty string is also written out as a
   PNG; the EMPTY string means "one frame, close, write nothing", which is what a caller
   wants when it is after the screen's SIDE EFFECTS rather than its picture. NULL is the
   real screen, modal, waiting for a human -- never that in a headless run, or it hangs.
   That is what lets a gate assert both halves of this screen at once -- that it draws
   anything at all, and that closing it without touching a dial writes no preset.
   Before it existed the menu could not be driven headlessly in any way, which is
   precisely why the button stayed broken from 19 to with the suite green. */
int  game_visuals_open(SDL_Window* win, const char* dospack, const char* shot);

/* ---- save and load ------------------------------------------------------------------
   The engine writes the game (CNC_Save_Load -> saveload.cpp); these carry the handful of
   things it does not know it has -- the camera, which .pack the terrain came from, the
   campaign position and the music -- and re-sync the renderer across the discontinuity a
   load is. See game/dossave.h for the slot store. */
struct DS_Slot;
int  game_save_slot(int slot, const char* descr);   /* 1 ok */
int  game_load_slot(int slot);                      /* 1 ok */
int  game_slot_list(struct DS_Slot* out, int max);  /* -> count filled */
/* The campaign position lives in the shell, so it is handed down before a save and read
   back after a load. */
void game_set_campaign(int active, int side, int scenario, int dir, int var);
void game_get_campaign(int* active, int* side, int* scenario, int* dir, int* var);

/* THE MOVIE PLAYER, LENT BY THE SHELL. The pause dialog's Restate offers a Video
   button that replays the mission's briefing movie in the game window; the renderer
   has no decoder of its own, so the shell hands it the one it plays every campaign
   movie with. `play` is called with the movie's bare name (GDI2, no extension) while
   the mission is paused, and returns 1 when it played or skipped and 0 when the window
   was closed. With no player registered the box offers no Video button, which is what
   1995 does when the movie file is absent. */
void game_set_movie_player(int (*play)(void* user, const char* name), void* user);

/* ENHANCED BY DEFAULT, AND IT IS THE SHELL THAT DECIDES.
 *
 * A player launching the game gets the desktop picture and their own saved preset; the
 * measuring instrument does not. That split is the whole reason this is a call the SHELL
 * makes rather than a value in fx_defaults(): 71 of the gate suite's invocations drive
 * cnc_eyes and assert against the picture this project has always shipped, and three
 * drive cnc3d and assert state rather than pixels. Moving the default into the library
 * would have changed what all 71 of them measure.
 *
 * `preset` is loaded if it exists and ignored in silence if it does not, because a
 * first run has no saved config and that is not an error. */
void game_visuals_default_enhanced(const char* preset);
/* WHAT WINDOW TO MAKE, before there is one: the display mode and size the preset asks
   for (fullscreen.h's FS_MODE_*), or the compiled defaults with no preset. Returns 1
   when a preset file was read. app/cnc3d.cpp asks this before SDL_CreateWindow. */
int game_display_wanted(const char* preset, int* mode, int* rw, int* rh);

/* THE PLAYER'S GAME CONTROLS, REMEMBERED, AND THE SHELL DECIDES THAT TOO.
 *
 * Speed, scroll rate, music, sound and speech used to live in a static for the length of
 * one process and were then thrown away. `path` names a small text file in the working
 * folder, beside the Visuals preset and treated the same way: read here if it exists,
 * ignored in silence if it does not, and rewritten when the player closes the pause
 * dialog having actually moved something.
 *
 * CALLING THIS IS AN OPT IN, and that is not tidiness. A remembered SPEED sets the engine
 * tick rate, so a settings file that every run picked up merely by being in the folder
 * would retime the whole gate suite from whatever happened to be lying there. cnc_eyes
 * never calls it, so its gates cannot see one; the shell calls it on a player's launch
 * and not on an automated one. NULL or "" turns remembering back off.
 *
 * Call it AFTER game_set_audio: restoring the volumes pushes them straight at the mixer.
 */
void game_controls_remember(const char* path);
/* AND THE SAME FOR THE VISUALS AND INPUT PRESET. An empty path means remember nothing:
   no load, no save, not one fopen. Only the shell's non-automated launch and the explicit
   --visualscfg turn it on, so no automated run can inherit a preset. */
void game_visuals_remember(const char* path);

/* Headless modes, kept out of the shell: they are measuring instruments, and each
   one ends the process. Call after game_boot. */
int  game_run_picktest(const GameOpts* o);
int  game_run_script(SDL_Window* win, const GameOpts* o);
int  game_run_shot(SDL_Window* win, const GameOpts* o);

/* One tactical frame into the back buffer, no swap. The harness draws, reads the
   pixels back and only then swaps, which is the only way to be sure the PNG it
   writes is the frame it is talking about. */
void game_draw(SDL_Window* win);

/* The back buffer, as a PNG. Shared with the menu so both screens are proved with
   the same readback and the same writer. Returns 1 on success. */
int  game_grab_png(const char* path, int w, int h);

/* DID THE TEAMS THE LOBBY ASKED FOR ACTUALLY FORM?
 *
 * A per-player faction and team that move on screen and change nothing in the match is
 * the exact failure the lobby exists to avoid, and it cannot be seen in a screenshot:
 * the alliance is built during the scenario load, inside the brain, out of
 * CNCPlayerInfoStruct::Team. So the renderer reads HouseClass::Get_Ally_Flags back off
 * the engine after the match starts and compares how many houses are allied with the
 * human against how many seats the lobby put on the human's team.
 *
 *    1  they agree           0  they disagree           -1  no skirmish has started
 *
 * The shell turns a 0 into a non-zero exit on the --lobbyplay route, so the gate that
 * already runs that route catches a regression without knowing any of the above. */
int game_skirmish_teams_ok(void);

/* What the engine's game-over event said, valid after game_loop returns
   GAME_EXIT_WON or GAME_EXIT_LOST: the win/lose movie name (no extension) and the
   engine's own single-player score payload (Calculate_Single_Player_Score). */
typedef struct GameOverInfo {
    int  valid, win;
    char movie[16];
    int  score, leadership, efficiency;
    int  nod_killed, gdi_killed, civ_killed;
    int  nod_bldg, gdi_bldg, civ_bldg;
    int  credits;
    /* Elapsed mission time in whole minutes, for the score screen's TIME field.
       The DLL's GameOverEvent does not carry it, so we derive it the way 1995 does:
       conquer.cpp:1684 adds 4 to Score.ElapsedTime once per game frame, and
       score.cpp:659 prints ElapsedTime / TIMER_MINUTE (3600) + 1. cnc_eyes runs the
       brain at TICK_HZ 15, one engine tick per game frame, so
       minutes = ticks * 4 / 3600 + 1 = ticks / 900 + 1. */
    int  minutes;
} GameOverInfo;
const GameOverInfo* game_over_info(void);

/* ---- WHAT EVERY COMMANDER DID, for the screen a match ends on ----------------------
 *
 * The campaign's score screen reports one player against a designer's par. A match has no
 * par: it has other people, and the only question worth answering is how you did against
 * THEM. So this is a table rather than a tally, one row per commander, and the screen
 * draws it as one shared grid with every number a bar measured against whoever leads that
 * column.
 *
 * Every field here is something the engine already counts per player and hands out
 * through its own sidebar and player-info state; none of it needed a new export. There is
 * deliberately no "built" column, which the first sketch of this screen had: the engine
 * keeps no such tally, and a number nobody counts is a number that would have to be
 * invented. LOST takes its place, and reads better anyway -- killed against lost is the
 * whole shape of a match.
 *
 * Captured while the mission is still standing, because game_shutdown pulls the engine
 * down and the screen runs after it. */
typedef struct MatchStatRow {
    char name[16];
    int colour;        /* PlayerColorType 0..7, the seat's own livery */
    int house;         /* HousesType, the join key to everything else */
    int killed;        /* enemy units destroyed          */
    int razed;         /* enemy structures destroyed     */
    int lost;          /* own units and structures lost  */
    int harvested;     /* credits gained over the match, excluding the start          */
    int score;         /* the derived total the SCORE bar is drawn from               */
    int defeated;      /* knocked out or resigned                                     */
    int is_local;      /* the player sitting at this machine                           */
    int faction;       /* 0 GDI, 1 Nod: what they PLAYED, not which multi house they got */
} MatchStatRow;

typedef struct MatchStats {
    MatchStatRow row[8];
    int nrows;
    int valid;         /* 0 outside a match: the campaign path must not read this      */
    int win;           /* did the local player win                                      */
    int seconds;       /* elapsed match time                                            */
    int net;           /* a network match rather than a Skirmish                        */
    int side;          /* the LOCAL player's faction, for the debrief's emblem and music  */
    char map[24];
    /* WHY IT ENDED, when it did not end by somebody winning: the simulations stopped
       agreeing, or nothing has been heard from a seat for thirty seconds. Empty for an
       ordinary finish, which is every campaign path and every match that reached a
       verdict. */
    char ended[96];
} MatchStats;
const MatchStats* game_match_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* CNC_GAME_H */
