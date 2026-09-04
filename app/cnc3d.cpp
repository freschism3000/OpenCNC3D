/* ====================================================================================
 *  cnc3d.cpp -- THE PROGRAM.
 *
 *  One process, one SDL window, one GL context, and a state machine on top:
 *
 *      MENU  --Start New Game-->  GAME  --ESC-->  MENU  --Exit Game-->  done
 *
 *  Neither screen owns the window. The menu borrows it through dosmenu_shell.h; the
 *  tactical renderer borrows it through cnc_game.h. Both draw into the same back
 *  buffer, and each one sets the GL state it needs at the top of its own frame
 *  rather than trusting what the other left behind. That single rule is what makes
 *  the handoff work in both directions; see the note at the top of dosmenu_shell.c
 *  for what specifically goes wrong without it.
 *
 *  The window is created at the TACTICAL size (1280x720 by default). The menu is a
 *  fixed 320x200 DOS plate, so it is presented letterboxed at the largest whole
 *  number scale that fits, which is 3x in a 1280x720 window. Nothing is resampled,
 *  nothing is stretched, and the window never changes size under the player.
 *
 *  --harness N drives the whole thing with no hands: it clicks Start with real SDL
 *  events, plays N engine ticks, walks out, and does it again, writing a PNG at
 *  every transition. That is the proof, and it is in the shipping binary rather
 *  than in a test build, so it cannot rot.
 * ==================================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exception>   /* the terminate handler below: one log line instead of a silent exit */
#include <string>
#include <vector>
#ifdef _WIN32
/* _dup2 and _fileno, for pointing stdout at the same open file description as stderr
   when the log file is opened at the top of main. */
#include <io.h>
#endif

#include <SDL.h>
#define GL_SILENCE_DEPRECATION 1
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
/* CMD+F / ALT+ENTER, and the --fullscreen flag that starts there.
   OUTSIDE the __APPLE__ branch, and that is the whole point: it landed inside it, so
   the Windows build never saw fs_start_fullscreen while the two lines that USE it are
   unconditional, and cnc3d.cpp failed to compile for Windows with the Mac build green.
   The header itself says both keys are accepted on both platforms; it was only the
   include that was Mac-only. */
#include "fullscreen.h"

#include "cnc_game.h"
/* The build number. Generated from the VERSION file by tools/version.sh before every
   compile, so the number on the menu plate and the number on the release tag cannot
   drift apart. */
#include "cnc3d_build.h"
extern "C" {
#include "dosmenu_shell.h"
#include "dosops.h"
#include "doslobby.h"
#include "../game/dossave.h"
#include "audioboot.h"
#include "campaign.h"
#include "../video/movieplay.h"
#include "../video/moviesnd.h"
}

/* Peak resident memory, for the harness only. A boot/shutdown pair that leaks shows
   up here as a staircase, and a staircase is the difference between "you can quit to
   the menu" and "you can quit to the menu twice". Not compiled on Windows: the shell
   there would use GetProcessMemoryInfo, and the shipping build needs neither. */
#ifdef __APPLE__
#include <mach/mach.h>
/* CURRENT resident size, not the peak. getrusage's ru_maxrss was tried first and is
   useless here: the harness itself allocates a 2.7 MB readback buffer and the PNG
   writer allocates two more per screenshot, so the peak climbs every round whether
   anything leaks or not. Current footprint, sampled at the same point in each round,
   is the only number that answers the question. */
static long peak_kib(void)
{
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) != KERN_SUCCESS)
        return 0;
    return (long)(info.resident_size / 1024);
}
#else
static long peak_kib(void) { return 0; }
#endif

enum AppState { APP_MENU, APP_SIDESELECT, APP_BRIEF, APP_GAME, APP_WINLOSE,
                APP_SCORE, APP_MAPSEL, APP_DONE };

static const char* state_name(AppState s)
{
    switch (s) {
    case APP_MENU: return "MENU";
    case APP_SIDESELECT: return "SIDESELECT";
    case APP_BRIEF: return "BRIEF";
    case APP_GAME: return "GAME";
    case APP_WINLOSE: return "WINLOSE";
    case APP_SCORE: return "SCORE";
    case APP_MAPSEL: return "MAPSEL";
    default: return "DONE";
    }
}

/* --flowtest support, declared before the campaign helpers that read them; the
   full description sits with the flag parsing below. */
static int  g_flowTest = 0;
/* --flowrounds N: how many missions the hands-off flow walks before it stops. The
   default 2 is the old behaviour and proves the LOOP closes (menu -> mission -> win ->
   score -> map -> next mission). Set it to 15 (GDI) or 13 (Nod) to walk the WHOLE
   campaign, which is the only check that every mission in the chain has a pack, a
   .INI, a .BIN and a map-selection row -- 32 of the 36 had never been booted
   once. */
static int  g_flowRounds = 2;
static int  g_movieBound = 0;

/* ---------------------------------------------------------------------------------- *
 *  The campaign (init.cpp SEL_START_NEW_GAME onward, transcribed).
 *
 *  side/scenario/dir/var are ScenPlayer/Scen.Scenario/ScenDir/ScenVar; the name
 *  builder is Set_Scenario_Name's single-player arm. The per-mission movie names
 *  come from the scenario INI's [Basic] section, exactly where Read_Scenario gets
 *  them; the win/lose movie arrives pre-resolved in the engine's game-over event.
 * ---------------------------------------------------------------------------------- */
static struct {
    int  active;
    int  side;          /* 0 GDI, 1 Nod                        */
    int  scenario;      /* 1..15                               */
    char dir;           /* 'E' / 'W'                           */
    char var;           /* 'A'..'C'                            */
} g_camp;

/* ------------------------------------------------------------------------------ *
 *  SPECIAL OPS.
 *
 *  Every scenario that is NOT part of the fifteen-mission GDI run or the thirteen
 *  mission Nod run: the cartridge's own extras (its scenario table at ROM 0x20EB80
 *  lists SCG30EA, SCB21EA and SCB22EB outside the campaign, and the ROM carries
 *  briefing text for SCG30EA and SCB22EB), and the fifteen from the 1996 Covert
 *  Operations disc. The rule is the scenario NUMBER, because that is what separates
 *  them in the cartridge's own directory: campaign missions are 01..15, everything
 *  above 19 is not in the run.
 *
 *  A mission is listed only if BOTH its INI and its pack are installed. A row the
 *  player can select and cannot play is worse than a row that is not there.
 *
 *  The NAME comes from the INI's [Basic] Name=. The Covert Operations INIs carry one;
 *  the cartridge-side INIs do not, and no string table we have found holds their
 *  titles, so those rows show their scenario code. Recorded as a known gap.
 * ------------------------------------------------------------------------------ */
struct SpecOp {
    std::string scen;
    std::string pack;
    std::string name;
    /* The skirmish maps carry three more facts, because the lobby prints them and one
       of them decides how many opponents the map can seat. See skirmish_map_facts. */
    std::string theater;
    int w, h, starts;
    SpecOp() : w(0), h(0), starts(0) {}
};
static std::vector<SpecOp> g_specops;
static std::vector<DO_Mission> g_specopsRows;
static std::vector<SpecOp> g_skirmish;
static std::vector<SK_Map> g_skirmishMaps;

/* THE TITLES THE MISSION FILES DO NOT CARRY.
 *
 * Fifteen of the twenty-seven Special Ops scenarios ship a [Basic] Name= and are read
 * straight out of the file. The other twelve do not, and the first pass showed their
 * scenario code instead. Real names are wanted here, and these are what the evidence
 * supports -- each row says WHY, so a wrong one can be argued with rather than trusted.
 *
 * PROVEN FROM THE CARTRIDGE'S OWN TEXT BANK at ROM 0x960A4D..0x960EB2, which holds four
 * console-exclusive briefings in plain ASCII:
 *   SCG30EA  the mission INI's [Briefing] is the SAME TEXT as the bank's GDI briefing at
 *            0x960C89 ("Nod is experimenting on civilians using Tiberium ... take out the
 *            SAM sites ... the Obelisk ... the BioResearch"). Exact match, so this is the
 *            first console GDI mission and nothing else.
 *   SCB22EB  the ROM carries a briefing keyed to it -- the symbol table at 0x1B7ED0 lists
 *            TXT_SCB22EB_1..9, and only four scenarios in the whole ROM have such symbols.
 *            The bank's Nod briefing at 0x960A4D is a commando sent to lift nuclear
 *            components from a crate while the other troops raid a village as a diversion;
 *            SCB22EB's own layout is ten village buildings, eight civilians and a commando
 *            summoned by trigger rather than placed. Matched on content.
 *
 * BY SCENARIO-CODE CONVENTION, which the C&C community uses consistently for the five
 * PlayStation-era extra missions and which the cartridge reuses:
 *   SCG60EA/61EA/62EA  GDI Special Ops one, two and three
 *   SCB60EA/61EA       Nod Special Ops one and two
 * The cartridge's own menu strings for these are literally "SPECIAL OPS 1" and
 * "SPECIAL OPS 2" (ROM 0x22384C), so the wording here is the cartridge's, not ours.
 *
 * NOT MISSIONS AT ALL, and saying so is more use than inventing a title:
 *   SCG73EA  the ROM's own table at 0x223B00 labels it DYNAMIC
 *   SCB70EA  the same table at 0x223B60 labels it STATIC
 *   SCG32EA  byte-for-byte the same layout as SCG73EA -- 142 structures, 20 village
 *            buildings, 60 civilians, ten commandos and ten engineers. A kitchen-sink map.
 *   SCG71EB  Brief=GDI3, Win=BOMBAWAY: GDI 3's briefing and flow on a different layout
 *   SCG72EA  Brief=GDI1, Action=LANDING, Win=CONSYARD, Percent=0: the same for GDI 1
 *
 * ONE THING THIS TURNED UP, recorded as a known gap: the ROM also carries
 * TXT_SCG99EA_1..9 and the bank's fourth briefing ("Our top Commando has been intercepted
 * ... get an engineer into it at all costs"), but there is no SCG99EA.MAP or .INI anywhere
 * in the cartridge's asset directory. The second console GDI mission's TEXT shipped and its
 * MAP did not, so it cannot be offered here. */
static const struct { const char* scen; const char* name; } SPECOP_TITLES[] = {
    { "SCG30EA", "N64 Special Ops 1" },
    { "SCB22EB", "N64 Special Ops 2" },
    { "SCG60EA", "Special Ops 1" },
    { "SCG61EA", "Special Ops 2" },
    { "SCG62EA", "Special Ops 3" },
    { "SCB60EA", "Special Ops 1" },
    { "SCB61EA", "Special Ops 2" },
    { "SCG32EA", "Test Map: Dynamic" },
    { "SCG73EA", "Test Map: Dynamic" },
    { "SCB70EA", "Test Map: Static" },
    { "SCG71EB", "GDI 3 Variant" },
    { "SCG72EA", "GDI 1 Variant" },
};

static const char* specop_title(const char* scen)
{
    for (size_t i = 0; i < sizeof SPECOP_TITLES / sizeof SPECOP_TITLES[0]; i++)
        if (!strcmp(SPECOP_TITLES[i].scen, scen))
            return SPECOP_TITLES[i].name;
    return NULL;
}

static std::string specop_ini_name(const char* dir, const char* scen)
{
    char path[512];
    snprintf(path, sizeof path, "%s%s.INI", dir ? dir : "missions/", scen);
    FILE* f = fopen(path, "rb");
    if (!f) return std::string();
    char line[256];
    std::string out;
    while (fgets(line, sizeof line, f)) {
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (!strncmp(line, "Name=", 5)) {
            out = line + 5;
            /* Some Covert Operations INIs carry a trailing space on this row. */
            while (!out.empty() && (out[out.size() - 1] == ' ' ||
                                    out[out.size() - 1] == '\t'))
                out.erase(out.size() - 1);
            break;
        }
    }
    fclose(f);
    return out;
}

/* GENERATED, not read off the directory. The scenario code space is small and fully
   known -- SC[GB] nn [EW] [ABC] -- so the list is built by probing every name in it,
   which needs nothing but fopen. A readdir would have been shorter and would have
   dragged dirent.h into the one program that also has to compile for Windows 98 with a
   freestanding mingw; there is no reason to spend the portability on 840 fopens of a
   file that is not there. It also fixes the order by construction: GDI before Nod, then
   ascending, the same on every machine. */
static void specops_scan(const char* dir)
{
    static const char SIDE[2] = { 'G', 'B' };
    static const char WEST[2] = { 'E', 'W' };
    int s, n, w, v;

    g_specops.clear();
    g_specopsRows.clear();
    for (s = 0; s < 2; s++)
        for (n = 20; n < 90; n++)      /* 01..15 is the campaign, 90+ is the harness */
            for (w = 0; w < 2; w++)
                for (v = 0; v < 3; v++) {
                    char code[16], path[512];
                    FILE* f;
                    snprintf(code, sizeof code, "SC%c%02d%c%c", SIDE[s], n, WEST[w],
                             (char)('A' + v));
                    snprintf(path, sizeof path, "%s%s.INI",
                             dir && *dir ? dir : "missions/", code);
                    f = fopen(path, "rb");
                    if (!f) continue;
                    fclose(f);
                    /* No pack, no row: a mission the player can select and cannot play
                       is worse than one that is not offered. */
                    snprintf(path, sizeof path, "%s.pack", code);
                    f = fopen(path, "rb");
                    if (!f) continue;
                    fclose(f);
                    SpecOp so;
                    so.scen = code;
                    so.pack = std::string(code) + ".pack";
                    so.name = specop_ini_name(dir, code);
                    /* The file's own Name= wins; the table only fills the gaps. */
                    if (so.name.empty()) {
                        const char* t = specop_title(code);
                        if (t) so.name = t;
                    }
                    g_specops.push_back(so);
                }
    /* Built after the list is complete, because these point into its strings. */
    for (size_t i = 0; i < g_specops.size(); i++) {
        DO_Mission r;
        r.scen = g_specops[i].scen.c_str();
        r.name = g_specops[i].name.empty() ? NULL : g_specops[i].name.c_str();
        r.nod = (unsigned char)(g_specops[i].scen[2] == 'B' ? 1 : 0);
        g_specopsRows.push_back(r);
    }
    printf("APP|specops|%d missions\n", (int)g_specops.size());
    fflush(stdout);
}


/* ------------------------------------------------------------------------------------
 *  USER MAPS -- the ones made in the editor.
 *
 *  Only the SINGLEPLAYER ones. A multiplayer map has no briefing and no objective, so
 *  offering it here would start a game nobody can win; those belong in the Skirmish map
 *  list, on its own tab.
 *
 *  PROBED, not read off the directory, for the same reason specops_scan is: this program
 *  has to compile for Windows 98 against a freestanding mingw, and dirent.h is not worth
 *  spending that on. The editor writes into a small known name space (USER00..USER99)
 *  precisely so this side can find them with nothing but fopen.
 *
 *  A user map carries no baked pack. It does not need one: a pack's terrain atlas is the
 *  whole THEATER's tile bank, so the map borrows a pack of its own theater and is
 *  re-skinned from its own .BIN and .HGT at boot -- which is exactly what the editor
 *  does when it makes a blank map.
 * ---------------------------------------------------------------------------------- */

/* Where the user's own maps start in g_skirmish: everything from here on is
   theirs, and the lobby's USER MAPS tab shows exactly those. */
/* WHERE THE MISSIONS ARE, remembered once.
 *
 * opt.dir is REWRITTEN when a user map is chosen, because that map lives in user_maps/.
 * Reading the mission root back out of opt.dir afterwards would therefore give
 * ".../user_maps/user_maps/" on the second visit. Every scan and every launch asks this
 * instead. */
static char g_baseDir[512] = "";

static const char* base_dir(const char* fallback)
{
    if (!g_baseDir[0])
        snprintf(g_baseDir, sizeof g_baseDir, "%s",
                 fallback && *fallback ? fallback : "missions/");
    return g_baseDir;
}

static size_t g_skirmishUserFrom = 0;

static std::vector<SpecOp>     g_usermaps;
static std::vector<DO_Mission> g_usermapRows;

/* [BASIC] CNC3DKind, written by the editor. Absent means singleplayer, which is the
   right default for a map made before the switch existed. */
static bool usermap_is_multi(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char line[512];
    int inBasic = 0, multi = 0;
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { inBasic = !strncasecmp(p, "[BASIC]", 7); continue; }
        if (inBasic && !strncasecmp(p, "CNC3DKind", 9)) {
            multi = (strstr(p, "Multi") != NULL);
            break;
        }
    }
    fclose(f);
    return multi != 0;
}

/* Which pack a user map borrows: its theater's, from any mission that ships one. */
/* strcasestr is not in mingw's headers, and this program has to build there. One
   needle, uppercase, against a line uppercased as it is scanned. */
static bool line_has_word(const char* line, const char* upperWord)
{
    const size_t n = strlen(upperWord);
    for (const char* p = line; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && toupper((unsigned char)p[i]) == upperWord[i]) i++;
        if (i == n) return true;
    }
    return false;
}

/* IS THIS A BIG MAP? A 128-wide map is written as [MAP] Version=1 (the brain's own
   Read_Binary_Big format) and its Width/Height exceed 64. Such a map CANNOT borrow a
   donor pack: a donor is a 64x64 bake and the renderer takes its grid dims from the
   pack header, so the map would draw as a quarter of itself. */
static bool usermap_is_big(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char line[512];
    int inMap = 0, big = 0;
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') { inMap = !strncasecmp(p, "[MAP]", 5); continue; }
        if (!inMap) continue;
        if (!strncasecmp(p, "Version=", 8) && atoi(p + 8) >= 1) { big = 1; break; }
        if (!strncasecmp(p, "Width=", 6)  && atoi(p + 6) > 64)  { big = 1; break; }
        if (!strncasecmp(p, "Height=", 7) && atoi(p + 7) > 64)  { big = 1; break; }
    }
    fclose(f);
    return big != 0;
}

/* THE PACK A USER MAP PLAYS ON. Its OWN bake wins whenever one exists -- that is the
   only thing a big map can use, and even for a 64 map its own bake carries its real
   terrain colours instead of a donor's. Otherwise it borrows the donor of its
   theater, which is what every user map did before any of them could be baked. */
static const char* usermap_donor_pack(const char* dir, const char* scen);
static const char* usermap_pack(const char* dir, const char* scen)
{
    static char own[64];
    snprintf(own, sizeof own, "%s.pack", scen);
    FILE* f = fopen(own, "rb");
    if (f) { fclose(f); return own; }
    return usermap_donor_pack(dir, scen);
}

static const char* usermap_donor_pack(const char* dir, const char* scen)
{
    char path[512];
    snprintf(path, sizeof path, "%suser_maps/%s.INI", dir && *dir ? dir : "missions/", scen);
    /* [MAP] Theater=, which is a different section to [BASIC] and reading it from the
       wrong one is a known trap in this engine. */
    FILE* f = fopen(path, "rb");
    /* 0 temperate, 1 desert, 2 winter, 3 snow -- the editor's own order. Keying on
       DESERT alone sent every WINTER user map out with the temperate pack. A
       CNC3DTheater= line wins over Theater=: snow maps tell the TD brain WINTER and
       carry their real look in the CNC3D key. */
    int th = 0, inMap = 0, ours = 0;
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            const char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '[') { inMap = !strncasecmp(p, "[MAP]", 5); continue; }
            if (!inMap) continue;
            const int isOurs = !strncasecmp(p, "CNC3DTheater=", 13);
            if (!isOurs && (ours || strncasecmp(p, "Theater=", 8) != 0)) continue;
            const char* v = strchr(p, '=') + 1;
            if      (line_has_word(p, "DESERT")) th = 1;
            else if (line_has_word(p, "WINTER")) th = 2;
            else if (line_has_word(p, "SNOW"))   th = 3;
            else if (line_has_word(p, "SAND"))   th = 4;
            else                                 th = 0;
            (void)v;
            if (isOurs) { ours = 1; break; }
        }
        fclose(f);
    }
    static const char* const PACKS[5] = { "SCG01EA.pack", "SCB01EA.pack",
                                          "SCW01EA.pack", "SCS01EA.pack",
                                          "SCA01EA.pack" };
    return PACKS[th];
}

static void usermaps_scan(const char* dir)
{
    g_usermaps.clear();
    g_usermapRows.clear();
    for (int i = 0; i < 100; i++) {
        char code[16], path[512];
        snprintf(code, sizeof code, "USER%02d", i);
        snprintf(path, sizeof path, "%suser_maps/%s.INI",
                 dir && *dir ? dir : "missions/", code);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fclose(f);
        if (usermap_is_multi(path)) continue;      /* skirmish's, not this list's */
        SpecOp so;
        so.scen = code;
        so.pack = usermap_pack(dir, code);
        {   /* the map's own Name=, if it has one */
            char nm[128];
            snprintf(nm, sizeof nm, "%suser_maps", dir && *dir ? dir : "missions/");
            so.name = specop_ini_name(nm, code);
        }
        g_usermaps.push_back(so);
    }
    for (size_t i = 0; i < g_usermaps.size(); i++) {
        DO_Mission r;
        r.scen = g_usermaps[i].scen.c_str();
        r.name = g_usermaps[i].name.empty() ? NULL : g_usermaps[i].name.c_str();
        r.nod = 0;
        g_usermapRows.push_back(r);
    }
    printf("APP|usermaps|%d singleplayer maps\n", (int)g_usermaps.size());
    fflush(stdout);
}

/* EVERYTHING THE LOBBY PRINTS ABOUT ONE MAP, out of its INI in a single pass, and
   SECTION AWARE, which the mission list's Name= reader is not:
   Theater lives in [MAP] and not in [Basic], and reading it from the wrong section is a
   known trap in this engine (display.cpp:1291).

   THE START COUNT IS THE ONE THAT NEEDS CARE. Create_Units (scenarioini.cpp:1446-1476)
   reads [Waypoints], COMPACTS the valid ones, and then indexes the compacted list, so a
   gap makes index n mean waypoint n+1. The number of players a map can seat is
   therefore the length of the CONTIGUOUS run from 0, capped at MAX_PLAYERS (6), and not
   the number of waypoints it happens to carry: on SCM04EA a naive count says 7 and the
   seventh is an interior marker, not a start. */
static void skirmish_map_facts(const char* dir, const char* scen, SpecOp* out)
{
    char path[512];
    snprintf(path, sizeof path, "%s%s.INI", dir && *dir ? dir : "missions/", scen);
    FILE* f = fopen(path, "rb");
    if (!f) return;

    enum { S_NONE, S_BASIC, S_MAP, S_WAY } sec = S_NONE;
    bool way[26];
    for (int i = 0; i < 26; i++) way[i] = false;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (line[0] == '[') {
            if (!strncmp(line, "[Basic]", 7))          sec = S_BASIC;
            else if (!strncmp(line, "[MAP]", 5))       sec = S_MAP;
            else if (!strncmp(line, "[Waypoints]", 11)) sec = S_WAY;
            else                                        sec = S_NONE;
            continue;
        }
        if (sec == S_BASIC && !strncmp(line, "Name=", 5)) {
            out->name = line + 5;
            /* Some INIs carry a trailing space on this row. */
            while (!out->name.empty() && (out->name[out->name.size() - 1] == ' ' ||
                                          out->name[out->name.size() - 1] == '\t'))
                out->name.erase(out->name.size() - 1);
        } else if (sec == S_MAP) {
            /* CNC3DTheater= wins for display: a SNOW map says Theater=WINTER to the
               engine, and the lobby should say what the player will actually see. */
            if (!strncmp(line, "CNC3DTheater=", 13)) out->theater = line + 13;
            else if (!strncmp(line, "Theater=", 8) && out->theater.empty())
                out->theater = line + 8;
            else if (!strncmp(line, "Width=", 6))   out->w = atoi(line + 6);
            else if (!strncmp(line, "Height=", 7))  out->h = atoi(line + 7);
        } else if (sec == S_WAY) {
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            const int idx = atoi(line);
            const int cell = atoi(eq + 1);
            if (idx >= 0 && idx < 26 && cell >= 0) way[idx] = true;
        }
    }
    fclose(f);

    int run = 0;
    while (run < 26 && way[run]) run++;
    if (run > 8) run = 8;          /* MAX_PLAYERS with the 8-player patch */
    out->starts = run;
}

/* The skirmish maps, found the same way the mission list is found: by looking for the
   files rather than by keeping a table that can go stale. A map with no pack is left out
   for the same reason a mission with no pack is: an entry the player can pick and cannot
   play is worse than one that is not offered.
   SCM is the naming the 1995 discs use for a multiplayer map, and it is kept because the
   scenario code is what the engine is handed. */
static void skirmish_scan(const char* dir)
{
    int n, v;

    g_skirmish.clear();
    g_skirmishMaps.clear();
    for (n = 1; n < 99; n++)
        for (v = 0; v < 3; v++) {
            char code[16], path[512];
            FILE* f;
            snprintf(code, sizeof code, "SCM%02dE%c", n, (char)('A' + v));
            snprintf(path, sizeof path, "%s%s.INI", dir && *dir ? dir : "missions/", code);
            f = fopen(path, "rb");
            if (!f) continue;
            fclose(f);
            snprintf(path, sizeof path, "%s.pack", code);
            f = fopen(path, "rb");
            if (!f) continue;
            fclose(f);
            SpecOp so;
            so.scen = code;
            so.pack = std::string(code) + ".pack";
            skirmish_map_facts(dir, code, &so);
            g_skirmish.push_back(so);
        }
    /* THE USER'S OWN MULTIPLAYER MAPS, appended so the tab has something to show. Only
       the multiplayer ones: a singleplayer map has one start position and would seat a
       skirmish nobody else can join. They keep their own tab, so a long official list
       never buries them. */
    {
        const size_t official = g_skirmish.size();
        for (int i = 0; i < 100; i++) {
            char code[16], path[512], udir[512];
            snprintf(code, sizeof code, "USER%02d", i);
            snprintf(udir, sizeof udir, "%suser_maps/",
                     dir && *dir ? dir : "missions/");
            snprintf(path, sizeof path, "%s%s.INI", udir, code);
            FILE* f = fopen(path, "rb");
            if (!f) continue;
            fclose(f);
            SpecOp so;
            so.scen = code;
            so.name = specop_ini_name(udir, code);
            skirmish_map_facts(udir, code, &so);
            /* SEATS DECIDE, NOT THE FLAG. This used to admit only maps tagged
               CNC3DKind=Multi, and the comment above says why: a map with one start
               would seat a skirmish nobody can join. But the reason is the START
               COUNT, and now that the editor places up to eight of them on any map,
               the count is measurable -- so measure it. the project owner built a 128x128 map
               with eight starts, left the kind at the dialog's default, and watched
               it never appear here. */
            if (so.starts < 2) continue;
            /* A map's OWN bake is better -- it carries real per-cell terrain colours,
               which the radar uses -- but it is NOT required, and demanding it was a
               mistake that hid the project owner's own maps from this list. A borrowed donor pack
               is only a source of ART: edit_reskin_from_bin grows the world to the
               document's own size and repaints every cell from its .BIN, so a
               128x128 map on a 64x64 donor comes up whole (measured: 16384 of 16384
               cells repainted from a 4096-cell pack). Nobody should have to know the
               word "bake" to play a map they just drew. */
            so.pack = usermap_pack(dir, code);
            g_skirmish.push_back(so);
        }
        g_skirmishUserFrom = official;
    }

    /* Built after the list is complete, because these point into its strings. */
    for (size_t i = 0; i < g_skirmish.size(); i++) {
        SK_Map r;
        memset(&r, 0, sizeof r);
        r.user = (unsigned char)(i >= g_skirmishUserFrom ? 1 : 0);
        r.scen = g_skirmish[i].scen.c_str();
        r.name = g_skirmish[i].name.empty() ? NULL : g_skirmish[i].name.c_str();
        r.theater = g_skirmish[i].theater.empty() ? NULL : g_skirmish[i].theater.c_str();
        r.w = (short)g_skirmish[i].w;
        r.h = (short)g_skirmish[i].h;
        r.starts = (short)g_skirmish[i].starts;
        g_skirmishMaps.push_back(r);
        printf("APP|skirmish|%s|%s|%s|%dx%d|%d starts\n", r.scen,
               r.name ? r.name : "-", r.theater ? r.theater : "-", r.w, r.h, r.starts);
    }
    printf("APP|skirmish|%d maps\n", (int)g_skirmish.size());
    fflush(stdout);
}

/* THE MAP PREVIEWS, loaded once for the life of the program and lent to every visit to
   the lobby. Optional, like every other pack here: without it the lobby's panel says it
   has no picture and every control on the screen still works. The reason it could not be
   loaded is printed once rather than swallowed, because "the panel is empty" and "the
   pack is not installed" are different problems and only one of them is a bug. */
static SK_Prev* skirmish_previews(void)
{
    static SK_Prev* prev = NULL;
    static int tried = 0;
    if (!tried) {
        char why[128];
        tried = 1;
        prev = sk_prev_load("mappreview.pack", why, sizeof why);
        if (!prev)
            fprintf(stderr, "lobby: mappreview.pack: %s -- the preview panel will be "
                            "empty (python3 tools/bake_map_previews.py makes it)\n", why);
    }
    return prev;
}

/* ONE LINE PER SEAT, from the SCREEN's side of the handoff, in ONE function because two
   routes print it: --lobbyshot never boots a match and prints this alone, and the real
   route prints it on the way into the engine, which then prints its own matching line
   per seat inside arm_skirmish. A script asserts the two agree rather than asserting
   that the lobby talked to itself.

   colour= is the PlayerColorType the square on the screen was filled from and the number
   that becomes CNCPlayerInfoStruct::ColorIndex, so "the colour you clicked is the colour
   the engine was handed" is one grep and a compare. It is printed as the index and never
   as a name: this screen has no names for colours, by ruling, and inventing one for a log
   line would put back exactly the thing the ruling removed.
   The eight-entry line after it is the UNIQUENESS proof, all eight seats whatever the
   opponent count, because a duplicate that only appears when the AI gauge is dragged up
   is still a duplicate. */
static void lobby_report_seats(const SK_Lobby* lob)
{
    const int players = (lob->ai_count + 1 > 8) ? 8 : lob->ai_count + 1;
    for (int i = 0; i < players; i++)
        printf("APP|lobby|player=%d|house=%s|team=%d|colour=%d\n", i,
               lob->house[i] ? "Nod" : "GDI", lob->team[i] + 1, lob->colour[i]);
    int seen[8], dup = 0;
    for (int i = 0; i < 8; i++) seen[i] = 0;
    for (int i = 0; i < 8; i++) {
        const int c = lob->colour[i];
        if (c < 0 || c >= 8 || seen[c]) dup++;
        else seen[c] = 1;
    }
    printf("APP|lobby|colours=%d,%d,%d,%d,%d,%d,%d,%d|duplicates=%d\n",
           lob->colour[0], lob->colour[1], lob->colour[2], lob->colour[3],
           lob->colour[4], lob->colour[5], lob->colour[6], lob->colour[7], dup);
}

/* The lobby's answer, into the options block the renderer reads. Every field the screen
   offers is copied; nothing is inferred and nothing is left over from a previous match. */
static void skirmish_apply(GameOpts* opt, const SK_Lobby* lob)
{
    opt->side = lob->side;
    opt->ai_count = lob->ai_count;
    opt->build = lob->build;
    opt->credits = lob->credits;
    opt->tiberium = lob->tiberium;
    opt->crates = lob->crates;
    opt->superweapons = lob->superweapons;
    opt->bases = lob->bases;
    opt->unit_count = lob->unit_count;
    for (int i = 0; i < 8; i++) opt->start_wp[i] = lob->start_wp[i];
    /* WHO EACH SEAT IS, carried the same way everything else on this screen is: by value,
       every entry, nothing inferred. The lobby's team is zero based and so is the field;
       only the PRINT adds one, because the roster prints teams from 1. */
    for (int i = 0; i < 8; i++) {
        opt->player_house[i] = lob->house[i] ? 1 : 0;
        opt->player_team[i] = lob->team[i];
        /* The colour index the SQUARE on the screen was filled from, which is the same
           index the engine calls that colour. All eight are copied even when fewer seats
           are in play: the lobby keeps the eight a permutation of 0..7, and a reader
           checking that no two players share one has to see all eight to do it. */
        opt->player_colour[i] = lob->colour[i];
    }
    printf("APP|lobby|side=%s|ai=%d|build=%d|credits=%d|bases=%d|tiberium=%d|crates=%d|"
           "super=%d|units=%d|start0=%d\n",
           lob->side ? "Nod" : "GDI", lob->ai_count, lob->build, lob->credits,
           lob->bases, lob->tiberium, lob->crates, lob->superweapons, lob->unit_count,
           lob->start_wp[0]);
    lobby_report_seats(lob);
    fflush(stdout);
}

static void camp_scen_name(char* out, size_t n)
{
    snprintf(out, n, "SC%c%02d%c%c", g_camp.side ? 'B' : 'G', g_camp.scenario,
             g_camp.dir, g_camp.var);
}

/* [Basic] key reader, the three movie fields only. "x" and "" both mean none,
   which is exactly how the INIs spell "no movie". */
static void camp_ini_movies(const char* dir, const char* scen,
                            char* intro, char* brief, char* action, size_t n)
{
    intro[0] = brief[0] = action[0] = 0;
    char path[512];
    snprintf(path, sizeof path, "%s%s.INI", dir ? dir : "missions/", scen);
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "campaign: cannot read %s\n", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (!strncmp(line, "Intro=", 6))  snprintf(intro, n, "%s", line + 6);
        if (!strncmp(line, "Brief=", 6))  snprintf(brief, n, "%s", line + 6);
        if (!strncmp(line, "Action=", 7)) snprintf(action, n, "%s", line + 7);
    }
    fclose(f);
}

/* One flow movie: dosdata/movies/NAME.VQA through the same player and audio sink
   the menu's logo uses. Skips honestly on "x"/empty/missing file. Returns 0 only
   when the window was closed. */
static int camp_movie(SDL_Window* win, CncAudio* au, const char* name)
{
    if (!name || !name[0] || !strcmp(name, "x") || !strcmp(name, "X"))
        return 1;
    /* Movies play with no pointer at all in 1995, and game_shutdown hands the HOST
       arrow back just before the post-mission movies run -- without this a macOS or
       Windows arrow sits on top of a full-screen VQA. Every campaign screen
       re-asserts DISABLE for itself the same way (camp_show_mouse). */
    SDL_ShowCursor(SDL_DISABLE);
    char path[512];
    snprintf(path, sizeof path, "dosdata/movies/%s.VQA", name);
    MOV_Opts o;
    MOV_Audio a;
    MOV_Sink sink;
    memset(&o, 0, sizeof o);
    o.plate_w = 320;
    o.plate_h = 200;
    o.stop_after = g_movieBound;
    movsnd_init(&sink, au);
    /* moviesnd.c ms_open ducks MIX_BUS_MUSIC to 0 for the movie (theme.cpp
       ThemeClass::Fade_Out) and ms_close only ramps it back when this is set. The menu
       shell can leave it at 0 because dms_run/dms_logo ramp the score back themselves;
       nothing on the campaign path does. With it at 0 every mission after a briefing ran
       with the music bus silent for the rest of the process, so the tactical score was
       mixed and never heard. */
    sink.restore_music = 1;
    movsnd_bind(&sink, &a);
    const int r = mov_play(win, path, &o, &a, NULL);
    printf("CAMPAIGN|movie|%s|%s\n", name,
           r == MOV_DONE ? "played" : r == MOV_SKIPPED ? "skipped"
           : r == MOV_QUIT ? "quit" : "missing");
    fflush(stdout);
    return r != MOV_QUIT;
}

/* ---------------------------------------------------------------------------------- *
 *  The harness.
 *
 *  It does not call dm_hit_test, it does not call game_boot behind the state
 *  machine's back, and it does not count "no crash" as a pass. It pushes the same
 *  SDL_MOUSEBUTTONDOWN/UP pair a hand would produce, on the pixel rectangle the menu
 *  itself says the Start button occupies, and then reads what came out.
 * ---------------------------------------------------------------------------------- */

static int  g_harnessTicks = 0;      /* --harness N: engine ticks per visit  */
static int  g_harnessRounds = 2;     /* how many times to enter the game     */
static const char* g_shotDir = ".";
static int  g_harnessFails = 0;

/* --hidden: run with the window never shown, for a measuring run WITH a real audio
   device (the harness is hidden too, but it is also silent, and some measurements --
   the movie tail drain -- only exist when a device is pulling). A visible window
   steals focus, and whoever was typing skips the logo and clicks through the menu
   without meaning to; that is a measured fact, not a guess. */
static int  g_hiddenWin = 0;
static const char* g_visualsShot = NULL;   /* --visualsshot FILE; see the parse below */
/* --lobbyshot DIR: open the SKIRMISH LOBBY at boot, drive every control on it with
   synthetic clicks on the rectangles the module itself reports, write a PNG of each
   frame, print the settings block that came out, and quit. It exists because a lobby is
   a screen made almost entirely of controls, and the only two questions worth asking
   about it -- does every label land inside its own box, and does every control write the
   field it claims to -- are both answerable without a human, and neither is answerable
   by "it did not crash". Implies --hidden and no movies. */
static const char* g_lobbyShot = NULL;
/* --lobbyplay N: the same script, but on the REAL route -- click Multiplayer Game on
   the menu, drive the lobby, press Play, and let the state machine boot the match the
   lobby just described for N ticks. --lobbyshot proves the screen; this proves the
   WIRING, because the only thing that can answer "does pressing Play get exactly that
   match" is the engine's own report of the lobby it was handed. */
static int g_lobbyPlay = 0;
/* The script both harness routes drive, in one place so the two cannot disagree about
   what was clicked. It touches every control the screen has, in the order a player
   would reach them. */
static const DMS_LobbyStep LOBBY_SCRIPT[] = {
    /* The mouse half. */
    { SK_I_NOD,      500, 500, 0 },   /* the latched side pair             */
    { SK_I_MAPS,     500, 595, 0 },   /* the fourth visible row            */
    { SK_I_AI,      1000, 500, 0 },   /* the gauge, hard right             */
    { SK_I_BUILD,    420, 500, 0 },   /* about half travel                 */
    { SK_I_CREDITS,  250, 500, 0 },   /* a quarter, snapped to 500         */
    { SK_I_UNITS,   1000, 500, 0 },   /* the fourth gauge, hard right: 10  */
    /* CRATES BEFORE SUPERWEAPONS, and the order is load-bearing rather than tidy: a
       disabled control does not take the focus (sk_press), so the mouse half has to end
       on Superweapons for the Space below to toggle the box it says it toggles. Putting
       crates last would hand Space the crate box and undo it. */
    { SK_I_CRATES,   100, 500, 0 },   /* a live check box, switched ON     */
    { SK_I_SUPER,    100, 500, 0 },   /* the other live one, switched off  */
    { SK_I_BASES,    100, 500, 0 },   /* locked: it must say why           */
    /* The keyboard half, from wherever the mouse left the focus, which is Superweapons.
       TRACED against sk_next_item rather than assumed, because the account that stood here
       described a walk this script does not take: Space toggles Superweapons back on, UP
       steps over the two locked boxes onto UNIT COUNT (which is enabled, so the walk stops
       there), LEFT nudges Unit Count down an eighth of its travel from 10 to 9, and three
       more UPs reach Credits, Tech Level and AI Players with DOWN coming back to Tech
       Level. The map list is never focused here; it is exercised by the SK_I_MAPS click
       above. Page Up and Page Down are not driven by this half, and Escape is driven by
       the drop down steps below. */
    { 0, 0, 0, SDLK_SPACE },
    { 0, 0, 0, SDLK_UP },             /* over the two locked boxes         */
    { 0, 0, 0, SDLK_LEFT },           /* an eighth of the credit gauge     */
    { 0, 0, 0, SDLK_UP },
    { 0, 0, 0, SDLK_UP },
    { 0, 0, 0, SDLK_UP },             /* now the list has the focus        */
    { 0, 0, 0, SDLK_DOWN },           /* which the arrows now walk         */
    /* THE ROSTER AND ITS DROP DOWN, last because choosing a map RESEATS the opponents
       and would undo any of it. Every step here is deliberately a NON-DEFAULT answer, so
       a value that arrives at the engine cannot be the one it would have had anyway:
         - the gauge is pulled back below the map's own seat count, which proves picking
           fewer opponents still works now that the map sets the number;
         - COMPUTER 1 is put on Nod, the SAME side as this script's human, which the old
           lobby could not express at all;
         - and on team 1, which is the human's team, which is an alliance where the
           default is a free-for-all.
       Escape then shuts the drop down. If it ever stopped being modal that Escape would
       leave the lobby instead, and the run would end with rc=DMS_CANCEL and no settings
       block at all -- so this step is its own assertion. */
    { SK_I_AI,       420, 500, 0 },
    { SK_ROW_ITEM(1), 500, 500, 0 },  /* open COMPUTER 1's drop down       */
    { SK_I_POP_NOD,  500, 500, 0 },   /* faction: the human's own side     */
    { SK_TEAM_ITEM(0), 500, 500, 0 }, /* team 1: allied with the human     */
    /* THE COLOUR PICKER, and the two steps are the two halves of the project owner's rule.
       First the square the PLAYER is wearing, which on any row but the player's own is
       locked: it must change nothing and say why. If that lock ever broke, the human's
       colour would move as a side effect of a change to a computer, and the assignment
       table this run prints at the end would show it.
       Then colour 2, which by the seat-order default belongs to COMPUTER 2, so the swap
       rule runs for real against a seat that is on screen: COMPUTER 2 has to move to the
       lowest free colour and all eight assignments have to stay distinct. */
    { SK_COLOUR_ITEM(0), 500, 500, 0 },  /* locked: PLAYER's own          */
    { SK_COLOUR_ITEM(2), 500, 500, 0 },  /* takes it off COMPUTER 2       */
    { 0, 0, 0, SDLK_ESCAPE },         /* shuts the box, NOT the lobby      */
    /* AND THE HUMAN TAKES A COLOUR AN AI IS ALREADY WEARING, which is the case the project owner
       named in his own words: "If the player picks a color already used by an ai
       opponent, that opponents color should automatically change to another." COMPUTER 3
       still holds colour 3, so PLAYER asking for it must push COMPUTER 3 off rather than
       the two of them sharing. */
    { SK_ROW_ITEM(0), 500, 500, 0 },  /* open PLAYER's own drop down       */
    { SK_COLOUR_ITEM(3), 500, 500, 0 },
    { 0, 0, 0, SDLK_ESCAPE },
    /* The side pair again, on the side that is already lit, so it changes nothing and
       redraws the one line that has to change: with a computer now set by hand, "every
       computer plays Nod" is no longer true and the screen has to say the real split. */
    { SK_I_NOD,      500, 500, 0 },
    { 0, 0, 0, SDLK_RETURN }          /* Enter is Play                     */
};
static DMS_LobbyProbe g_lobbyProbe;
static int  g_visualsFirst = 0;            /* --visualsfirst; see the parse below */
static int  g_wantClassic = 0;   /* --classic: the picture before the chain */

/* --flowtest N (storage above the campaign helpers): run the whole campaign flow
   hands-off -- auto-click Start New Game, autopilot the side select / score / map
   screens, bound every movie to a few frames, and synthesize a WIN after N mission
   ticks (clearly labelled). Ends the program after the SECOND mission boots and
   returns, which proves the full loop: menu -> side select -> briefing -> mission
   -> win -> movie -> score -> map -> next briefing -> next mission. */

/* Resident set after each round's shutdown, so the staircase is ASSERTED on, not just
   printed. The known step is ~19.5 MB per boot/shutdown cycle (a known gap; not
   yet attributed, leading suspect the brain's CNC_* ABI, which has no shutdown call).
   The limit exists to catch a NEW leak stacked on top of the known one; tightening it
   to zero is the day the known staircase is actually fixed. */
#define RSS_MAX_ROUNDS 64
#define RSS_GROWTH_LIMIT_KIB 30000
static long g_rssAfterRound[RSS_MAX_ROUNDS];

static void harness_click(int x, int y)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = SDL_PRESSED;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
    e.type = SDL_MOUSEBUTTONUP;
    e.button.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

static void shot(SDL_Window* win, const char* name)
{
    char path[512];
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(win, &w, &h);
    snprintf(path, sizeof path, "%s/%s", g_shotDir, name);
    if (!game_grab_png(path, w, h)) {
        fprintf(stderr, "HARNESS|FAIL|could not write %s\n", path);
        g_harnessFails++;
        return;
    }
    printf("HARNESS|shot|%s|%dx%d\n", path, w, h);
    fflush(stdout);
}

/* The back buffer, reduced to two numbers: how much of it is lit, and a hash of every
   byte.
 *
 *  "Lit" alone is not a test and this is the case that proves it. The first version
 *  of this harness only measured ink, and it passed a build whose menu came back
 *  multiplied by (0.82, 0.86, 0.92) -- the leftover glColor from the tactical camera
 *  HUD's second line of text, applied through GL_MODULATE to the menu's own texture.
 *  A uniformly 18 percent dark menu has almost exactly as much ink as a correct one.
 *  The HASH catches it, because a correct handoff means the menu after a mission is
 *  byte for byte the menu before any mission existed. */
static unsigned long long frame_digest(SDL_Window* win, double* ink)
{
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(win, &w, &h);
    *ink = 0.0;
    if (w <= 0 || h <= 0) return 0;
    unsigned char* px = (unsigned char*)malloc((size_t)w * h * 3);
    if (!px) return 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    long lit = 0;
    unsigned long long hash = 1469598103934665603ULL;      /* FNV-1a */
    for (long i = 0; i < (long)w * h; i++) {
        if (px[i * 3] > 12 || px[i * 3 + 1] > 12 || px[i * 3 + 2] > 12)
            lit++;
        for (int k = 0; k < 3; k++) {
            hash ^= px[i * 3 + k];
            hash *= 1099511628211ULL;
        }
    }
    free(px);
    *ink = (double)lit / ((double)w * h);
    return hash;
}

/* What one screen actually leaves behind for the next one. Printed rather than
   assumed: the whole handoff question is "does the other screen's leftover state
   break mine", and that is answerable with glGet, not with an opinion. */
static void gl_probe(SDL_Window* win, const char* when)
{
    GLboolean dmask = GL_FALSE;
    GLint dfunc = 0, vp[4] = {0, 0, 0, 0};
    GLfloat col[4] = {0, 0, 0, 0}, drange[2] = {0, 0};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask);
    glGetIntegerv(GL_DEPTH_FUNC, &dfunc);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetFloatv(GL_CURRENT_COLOR, col);
    glGetFloatv(GL_DEPTH_RANGE, drange);

    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(win, &w, &h);
    float dmin = 1.0f, dmax = 0.0f;
    double dsum = 0.0;
    long n = 0;
    if (w > 0 && h > 0) {
        float* dz = (float*)malloc((size_t)w * sizeof(float));
        if (dz) {
            for (int y = 0; y < h; y += 37) {
                glReadPixels(0, y, w, 1, GL_DEPTH_COMPONENT, GL_FLOAT, dz);
                for (int x = 0; x < w; x += 7) {
                    if (dz[x] < dmin) dmin = dz[x];
                    if (dz[x] > dmax) dmax = dz[x];
                    dsum += dz[x];
                    n++;
                }
            }
            free(dz);
        }
    }
    printf("GLSTATE|%s|depth_test=%d|depth_func=0x%04X|depth_mask=%d|depth_range=%.2f..%.2f|"
           "blend=%d|alpha_test=%d|tex2d=%d|cull=%d|scissor=%d|"
           "color=%.2f,%.2f,%.2f,%.2f|viewport=%d,%d,%dx%d|"
           "zbuf min=%.4f mean=%.4f max=%.4f\n",
           when,
           (int)glIsEnabled(GL_DEPTH_TEST), (unsigned)dfunc, (int)dmask,
           drange[0], drange[1],
           (int)glIsEnabled(GL_BLEND), (int)glIsEnabled(GL_ALPHA_TEST),
           (int)glIsEnabled(GL_TEXTURE_2D), (int)glIsEnabled(GL_CULL_FACE),
           (int)glIsEnabled(GL_SCISSOR_TEST),
           col[0], col[1], col[2], col[3], vp[0], vp[1], vp[2], vp[3],
           dmin, n ? dsum / n : 0.0, dmax);
    fflush(stdout);
}

/* ---------------------------------------------------------------------------------- */

/* WHAT A CRASH SAYS BEFORE IT GOES.
 *
 * This program contains no try/catch anywhere, so any exception that escapes ends the
 * process through std::terminate, which calls abort. On a double-clicked console binary the
 * console window closes with the process, and the whole event reaches the player as the game
 * vanishing and reaches a report as "it crashes".
 *
 * A terminate handler cannot prevent any of that and is not meant to. It buys one line in
 * the log naming what happened, which is the difference between a report that can be worked
 * and one that cannot. It costs nothing when nothing throws.
 *
 * Installed before anything else runs, and it deliberately does not try to continue: the
 * default behaviour follows immediately. */
static void cnc3d_terminate_handler(void)
{
    const char* what = "unknown";
    try {
        std::exception_ptr e = std::current_exception();
        if (e) std::rethrow_exception(e);
    } catch (const std::exception& ex) {
        what = ex.what();
    } catch (...) {
        what = "a non-standard exception";
    }
    fprintf(stderr, "FATAL: terminating on an unhandled exception: %s\n", what);
    fflush(stderr);
    abort();
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    std::set_terminate(cnc3d_terminate_handler);

#ifdef _WIN32
    /* THE LOG THE READ-ME HAS ALWAYS ASKED FOR, and which nothing wrote until v0.6.0.
     *
     * The Windows READ-ME's troubleshooting section says "Send cnc3d-log.txt" and lists
     * the lines worth looking at first (GL_RENDERER, GL_MULTITEX, GL_GOT depth,
     * LoadLibrary errors). No such file was ever created, so every Windows bug report
     * arrived with no log -- including the one that prompted this: a player whose game
     * closes the moment a mission starts, on hardware nobody here can reproduce, with
     * nothing to send but a photograph.
     *
     * The Windows executable is linked CONSOLE subsystem on purpose, so the diagnostics
     * do reach a console when the game is started from a command prompt. What they do
     * not survive is the ordinary way people start it: a double-clicked console program
     * gets a console window that disappears with the process, which is why the early
     * reports were photographs. A file beside the executable is the copy that can be
     * attached to a report.
     *
     * ONE FILE, ONE FILE DESCRIPTION. Both C streams have to end up sharing a single
     * open file description, or they share nothing but a name. Opening the same path
     * twice gives two descriptions with two independent write offsets: the "a" stream
     * always writes at end of file while the "w" stream writes forward from zero
     * through its own cursor, which is at or behind that end, so each stderr line
     * lands on top of whatever stdout appended and every stdout diagnostic is eaten.
     * freopen for stderr and then a descriptor alias for stdout gives one description,
     * one offset, and the two halves interleaved in the order they were printed.
     *
     * Failure here is deliberately silent and non-fatal: if the folder is read-only (an
     * unzipped-in-place download can be), the game must still start. Logging is a
     * convenience, never a precondition. The alias is likewise attempted and not
     * insisted on; if it fails, stdout keeps going to the console it already had rather
     * than to a file it would corrupt. */
    {
        /* THE FOLDER BESIDE THE EXECUTABLE IS TRIED FIRST AND IS NO LONGER THE ONLY PLACE.
           A download unzipped into Program Files, or left on a read-only share, cannot be
           written to. The note above accepted that and moved on, and the cost of accepting
           it is paid by the next crash report: the READ-ME asks the player to send
           cnc3d-log.txt, they cannot find one, and there is nothing to work from. The
           per-user directory the saves already use is always writable, so there is now
           always a log. The path is printed either way, so a player can be told where to
           look instead of being asked to guess. */
        char logpath[1024];
        snprintf(logpath, sizeof logpath, "cnc3d-log.txt");
        FILE* lf = freopen(logpath, "w", stderr);
        if (!lf) {
            /* SDL_GetPrefPath does not require SDL_Init, which has not run at this point. */
            char* pref = SDL_GetPrefPath("Slipgate Ironworks", "CNC3D");
            if (pref) {
                snprintf(logpath, sizeof logpath, "%scnc3d-log.txt", pref);
                SDL_free(pref);
                lf = freopen(logpath, "w", stderr);
            }
        }
        if (lf) {
            setvbuf(stderr, NULL, _IONBF, 0);   /* a crash must not eat the buffer */
            fflush(stdout);
            if (_dup2(_fileno(stderr), _fileno(stdout)) == 0)
                setvbuf(stdout, NULL, _IONBF, 0);
            fprintf(stderr, "C&C 3D %s -- log opened at %s\n", CNC3D_BUILD, logpath);
        } else {
            /* Still not fatal, because logging is a convenience and never a precondition.
               But say so, rather than leaving the absence to be discovered later. */
            fprintf(stdout, "C&C 3D %s -- NO LOG: could not write cnc3d-log.txt beside the "
                            "game or in the per-user folder\n", CNC3D_BUILD);
        }
    }
#endif

    DMS_Config mcfg;
    memset(&mcfg, 0, sizeof mcfg);
    mcfg.pack = "dosmenu.pack";
    /* THE BUILD NUMBER GOES ON THE MENU, in the corner the 1995 engine already used for
       it: menus.cpp:814-820 prints its version bottom right of the dialog in 6 point
       green with a full shadow, and dm_draw_version reproduces that exactly. So this is
       the console's own plate carrying our number rather than a label stuck on top.
       it is there so anyone can always tell which build they are running, which means a
       build made from anything other than the released commit must SAY so: CNC3D_BUILD
       is "v0.5.1" for a release and "v0.5.1+3a1f2c-dirty" for anything else. */
    mcfg.version = "C&C 3D " CNC3D_BUILD;
    /* init.cpp:1054 starts THEME_MAP1 before Select_Game, so the menu has music from
       the moment it appears. A theme BASE NAME, not a path: MAP1 lives in TRANSIT.MIX
       rather than SCORES.MIX and the bank is the thing that knows that. */
    mcfg.music = "MAP1";
    /* The movies play because they are there, not because a flag asked for them:
       init.cpp:796 plays the Westwood logo before Main_Menu and init.cpp:1199 plays
       the intro from inside it, with no option involved. --logo/--intro override the
       paths and --no-logo turns the first one off for anyone who has seen it enough
       times. A missing file is not an error: mov_play says so on stderr and the flow
       carries on, so a build with no movies folder still boots to the menu. */
    mcfg.logo = "dosdata/movies/LOGO.VQA";
    mcfg.intro = "dosdata/movies/INTRO2.VQA";

    /* The shell's own flags are stripped out; everything else is handed to the game
       verbatim, so every existing command line still means what it meant. */
    static char* gargv[256];
    int gargc = 0;
    gargv[gargc++] = argv[0];
    for (int i = 1; i < argc && gargc < 250; i++) {
        if (!strcmp(argv[i], "--menupack") && i + 1 < argc)      mcfg.pack = argv[++i];
        else if (!strcmp(argv[i], "--music") && i + 1 < argc)    mcfg.music = argv[++i];
        else if (!strcmp(argv[i], "--logo") && i + 1 < argc)     mcfg.logo = argv[++i];
        else if (!strcmp(argv[i], "--intro") && i + 1 < argc)    mcfg.intro = argv[++i];
        else if (!strcmp(argv[i], "--no-logo"))                  mcfg.logo = NULL;
        else if (!strcmp(argv[i], "--no-movies"))                mcfg.logo = mcfg.intro = NULL;
        else if (!strcmp(argv[i], "--harness") && i + 1 < argc)  g_harnessTicks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hidden"))                   g_hiddenWin = 1;
        /* Handled HERE and not forwarded, because this is the file that creates the
           window. The standalone cnc_eyes takes the same flag through its own parser. */
        else if (!strcmp(argv[i], "--fullscreen"))               fs_start_fullscreen = 1;
        /* CLASSIC: the picture this project shipped before the chain existed.
           Handled here and NOT forwarded, because whether a run starts enhanced is
           the shell's decision and the library's default has to stay classic. */
        else if (!strcmp(argv[i], "--classic"))                  g_wantClassic = 1;
        else if (!strcmp(argv[i], "--flowrounds") && i + 1 < argc) {
            g_flowRounds = atoi(argv[++i]);
            if (g_flowRounds < 1) g_flowRounds = 1;
        }
        else if (!strcmp(argv[i], "--flowside") && i + 1 < argc) {
            const char* w = argv[++i];
            camp_autopilot_side = (!strcmp(w, "nod") || !strcmp(w, "NOD")
                                   || !strcmp(w, "1")) ? 1 : 0;
        }
        else if (!strcmp(argv[i], "--flowtest") && i + 1 < argc) {
            g_flowTest = atoi(argv[++i]);
            g_hiddenWin = 1;
            g_movieBound = 24;
            camp_autopilot = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        }
        /* --visualsshot FILE: open the MENU's Visuals screen at boot, draw one frame
           into FILE, and quit. The menu had no headless entry point of any kind before
           this, which is the reason its Visuals button could be born broken and stay
           broken with a green suite: G42 drives the PAUSE route and cannot fail on it.
           Gate G57 uses this. Implies --hidden and no movies, like --flowtest. */
        /* --visualsfirst: open the menu's Visuals screen ONCE at boot and then carry on
           normally. It exists to reproduce, in a gate, the exact sequence that lost
           the 3D cursors: touch that screen at menu time, then play a
           mission. Unlike --visualsshot it does not shoot and does not exit. G59. */
        else if (!strcmp(argv[i], "--visualsfirst"))             g_visualsFirst = 1;
        else if (!strcmp(argv[i], "--lobbyplay") && i + 1 < argc) {
            g_lobbyPlay = atoi(argv[++i]);
            if (g_lobbyPlay < 1) g_lobbyPlay = 1;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
            memset(&g_lobbyProbe, 0, sizeof g_lobbyProbe);
            g_lobbyProbe.script = LOBBY_SCRIPT;
            g_lobbyProbe.steps = (int)(sizeof LOBBY_SCRIPT / sizeof LOBBY_SCRIPT[0]);
        }
        else if (!strcmp(argv[i], "--lobbyshot") && i + 1 < argc) {
            g_lobbyShot = argv[++i];
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        }
        else if (!strcmp(argv[i], "--visualsshot") && i + 1 < argc) {
            g_visualsShot = argv[++i];
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        }
        else if (!strcmp(argv[i], "--famefile") && i + 1 < argc) camp_fame_file = argv[++i];
        else if (!strcmp(argv[i], "--rounds") && i + 1 < argc)   g_harnessRounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shotdir") && i + 1 < argc)  g_shotDir = argv[++i];
        else gargv[gargc++] = argv[i];
    }

    /* --harness N means "N engine ticks per visit", so it IS --playticks N. Said
       once here rather than asking the caller to keep two numbers in step. */
    char tickbuf[32];
    if (g_harnessTicks > 0 && gargc < 250) {
        snprintf(tickbuf, sizeof tickbuf, "%d", g_harnessTicks);
        gargv[gargc++] = (char*)"--playticks";
        gargv[gargc++] = tickbuf;
    }
    /* --lobbyplay N carries its own tick count the same way, and deliberately does NOT
       go through g_harnessTicks: that flag means "drive the menu into Test Map and
       measure the handoff", which is a different route through this file. */
    else if (g_lobbyPlay > 0 && gargc < 250) {
        snprintf(tickbuf, sizeof tickbuf, "%d", g_lobbyPlay);
        gargv[gargc++] = (char*)"--playticks";
        gargv[gargc++] = tickbuf;
    }

    GameOpts opt;
    memset(&opt, 0, sizeof opt);
    if (game_parse_args(gargc, gargv, &opt))
        return 2;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    /* WHERE SAVES LIVE. The same per-user directory the hall of fame already uses, so a
       player's two kinds of persistent state sit together and neither lands in whatever
       folder the game happened to be launched from. A NULL from SDL degrades to "no
       persistence" rather than guessing, exactly as camp_fame_path does. --savedir, parsed
       by the renderer's own arg pass, overrides it and is what a gate uses. */
    if (!ds_get_dir()[0]) {
        char* pref = SDL_GetPrefPath("Slipgate Ironworks", "CNC3D");
        if (pref) {
            ds_set_dir(pref);
            SDL_free(pref);
        } else {
            fprintf(stderr, "saves: SDL_GetPrefPath gave nothing; this session cannot "
                            "save or load.\n");
        }
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);

    /* Hidden under --harness for the same reason cnc_eyes hides it under --shot: a
       window nobody is looking at should not steal focus from whoever is working. */
    Uint32 flags = SDL_WINDOW_OPENGL;
    if (g_harnessTicks > 0 || g_hiddenWin) flags |= SDL_WINDOW_HIDDEN;
    if (fs_start_fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window* win = SDL_CreateWindow("Command & Conquer 3D", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, opt.w, opt.h, flags);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(win, ctx);
    fprintf(stderr, "GL_VERSION  = %s\nGL_RENDERER = %s\n",
            glGetString(GL_VERSION), glGetString(GL_RENDERER));

    /* THE GAME STARTS ENHANCED. Not the library and not the gates: see the note over
       game_visuals_default_enhanced in cnc_game.h. --classic opts out, and the Visuals
       screen in either menu switches it live. */
    if (!g_wantClassic)
        game_visuals_default_enhanced("cnc3d-fx.cfg");
    /* WHAT WE ACTUALLY GOT, not what was asked for. Requesting a 24-bit depth buffer
       and a double-buffered visual does not mean the driver handed one over, and on a
       machine that falls back to a software GL the request is granted on paper and the
       depth test then discards nearly every polygon: a black tactical view with a
       handful of surviving quads, which is exactly what Windows showed first. The
       multitexture line is the other half of the same question, because the generic
       Microsoft software renderer is OpenGL 1.1 and has no multitexture at all, while
       this renderer's terrain pass assumes it. Both are cheap to print and neither can
       be inferred from the version string alone. */
    {
        int dbits = 0, sbits = 0, dbl = 0;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &dbits);
        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &sbits);
        SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &dbl);
        const char* ext = (const char*)glGetString(GL_EXTENSIONS);
        fprintf(stderr, "GL_GOT      = depth %d, stencil %d, doublebuffer %d\n",
                dbits, sbits, dbl);
        fprintf(stderr, "GL_MULTITEX = %s\n",
                (ext && strstr(ext, "GL_ARB_multitexture")) ? "yes" : "NO (terrain will not draw)");
        if (dbits < 16)
            fprintf(stderr, "GL WARNING: depth buffer is %d bits. The 3D view needs one;"
                            " without it the depth test throws most of the terrain away.\n", dbits);
    }

    /* SOUND, for the whole program: one bank, one mixer, one device, created before
       the menu and destroyed after it. The menu, the movies and the tactical view all
       push into this and none of them opens a device of its own.
       Under --harness there is no device: the harness runs in CI and on machines with
       no sound card, and it must measure pixels, not speakers. --audiowav still works
       there, because that path renders the mix to a file instead. */
    AudioBootOpts ab;
    memset(&ab, 0, sizeof ab);
    ab.dosdata = opt.dosdata;
    ab.wav = opt.audiowav;
    ab.music_vol255 = opt.musicvol;
    ab.sound_vol255 = opt.soundvol;
    /* EVERY AUTOMATED ENTRY POINT IS SILENT, not just the two that were listed here.
       The rule is the project owner's, 26 Aug 2026, and this is the second time it has been asked
       for: "every time you launch the eyes to test something, music still blasts
       through the speakers. We already agreed that shouldnt happen on tests."
       --harness and --flowtest were covered; --lobbyshot, --lobbyplay and
       --visualsshot were not, and all three are hidden, scripted, gate-driven runs
       that opened a real device and played the menu theme at whoever was sitting
       there. A full gate suite opened four of them.

       --audiowav still wins, because that path renders the mix to a FILE rather than
       to a device and is how the audio gates measure anything at all.

       IF YOU ADD ANOTHER AUTOMATED ENTRY POINT, ADD IT HERE. The renderer's own rule
       (cnc_eyes.cpp: --script, --shot and --picktest are silent) is the same idea;
       the operator should never have to ask for this a third time. */
    const int automated = g_harnessTicks > 0 || g_flowTest > 0 || g_lobbyPlay > 0
                        || g_lobbyShot != NULL || g_visualsShot != NULL;
    ab.silent = opt.nosound || (automated && !opt.audiowav);
    CncAudio* au = audio_boot(&ab);
    mcfg.au = au;
    game_set_audio(au);

    /* THE PLAYER'S GAME CONTROLS COME BACK, and only for a player. Speed, scroll rate and
       the three volumes are read from the working folder here and rewritten when the
       pause dialog closes on a change; game_controls_remember in cnc_game.h has the
       account of why this is a call and not a default.

       `automated` is reused rather than restated because it already names every entry
       point a gate drives, and a remembered SPEED is the engine's tick rate: no gate may
       inherit one from a file somebody left in the run folder. It sits after
       game_set_audio because restoring the volumes pushes them at the mixer. */
    if (!automated)
        game_controls_remember("cnc3d-controls.cfg");
    /* AND THE PLAYER'S PRESET IS WRITTEN BACK FROM THE PAUSE MENU TOO. Until this call
       existed, only the main menu's Visuals screen and the F5 panel ever wrote
       cnc3d-fx.cfg, so a Swapped Mouse Buttons set from the in-mission pause dialog was
       lost on quit. It sits AFTER game_visuals_default_enhanced has loaded that file. */
    if (!automated)
        game_visuals_remember("cnc3d-fx.cfg");

    /* --visualsshot: the menu's Visuals screen, one frame, then out. Placed here on
       purpose -- after the GL context and the audio boot, but BEFORE the menu shell
       opens -- because the screen under test needs a context and nothing else. It
       reports the preset file's state on the way out, which is the half a picture
       cannot show: closing this screen without moving a dial must write nothing. */
    if (g_visualsFirst && !g_visualsShot) {
        /* Open it, close it, keep going. The point is the SIDE EFFECT on everything that
           runs afterwards, not anything about the screen itself. */
        game_visuals_open(win, opt.dospack, "");   /* one frame, no file, then close */
        fprintf(stderr, "visuals: opened and closed at menu time (--visualsfirst)\n");
    }
    if (g_visualsShot) {
        const char* cfg = "cnc3d-fx.cfg";
        FILE* pre = fopen(cfg, "rb");
        long presz = -1;
        if (pre) { fseek(pre, 0, SEEK_END); presz = ftell(pre); fclose(pre); }
        const int rc = game_visuals_open(win, opt.dospack, g_visualsShot);
        FILE* post = fopen(cfg, "rb");
        long postsz = -1;
        if (post) { fseek(post, 0, SEEK_END); postsz = ftell(post); fclose(post); }
        printf("VISUALSSHOT|file=%s|rc=%d|cfg_before=%ld|cfg_after=%ld|cfg_written=%d\n",
               g_visualsShot, rc, presz, postsz, (presz != postsz) ? 1 : 0);
        fflush(stdout);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    DMS menu;
    char err[256];
    if (!dms_open(&menu, win, &mcfg, err, sizeof err)) {
        fprintf(stderr, "menu: %s\n", err);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* --lobbyshot: the skirmish lobby, driven end to end, then out. Placed here on
       purpose -- after the menu shell exists, because the lobby borrows its window, its
       surface and its letterbox, and before anything else, because nothing else is
       involved. The script below touches EVERY control the screen has, in the order a
       player would, and the settings block it prints at the end is the same one the
       renderer would be handed. */
    if (g_lobbyShot) {
        DMS_LobbyProbe probe;
        SK_Lobby lob;
        memset(&probe, 0, sizeof probe);
        memset(&lob, 0, sizeof lob);
        probe.script = LOBBY_SCRIPT;
        probe.steps = (int)(sizeof LOBBY_SCRIPT / sizeof LOBBY_SCRIPT[0]);
        probe.shotdir = g_lobbyShot;
        int lrc = 0;
        skirmish_scan(opt.dir);
        if (g_skirmish.empty()) {
            fprintf(stderr, "lobby: no skirmish maps in '%s'\n",
                    opt.dir ? opt.dir : "missions/");
            lrc = 1;
        } else {
            const int r = dms_lobby(&menu, &g_skirmishMaps[0],
                                    (int)g_skirmishMaps.size(), skirmish_previews(),
                                    &lob, &probe);
            printf("LOBBYSHOT|rc=%d|shots=%d|overflow=%d|map=%d(%s)|side=%s|ai=%d|"
                   "build=%d|credits=%d|bases=%d|tiberium=%d|crates=%d|super=%d|"
                   "units=%d\n",
                   r, probe.shots, probe.overflow, lob.map,
                   (lob.map >= 0 && lob.map < (int)g_skirmish.size())
                       ? g_skirmish[lob.map].scen.c_str() : "-",
                   lob.side ? "Nod" : "GDI", lob.ai_count, lob.build, lob.credits,
                   lob.bases, lob.tiberium, lob.crates, lob.superweapons,
                   lob.unit_count);
            /* The per-seat half of the answer, through the same writer --lobbyplay uses
               so one grep covers both routes and the two cannot drift. --lobbyshot never
               boots a match, so this is the SCREEN's word and nothing more; --lobbyplay
               is where it is checked against the engine's. */
            lobby_report_seats(&lob);
            fflush(stdout);
            if (r != 0 || probe.overflow != 0) lrc = 1;
        }
        dms_close(&menu);
        game_set_audio(NULL);
        audio_boot_shutdown(au);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return lrc;
    }

    /* The campaign screens (side select, score, map). A missing campaign.pack
       degrades loudly: Start New Game then says what is missing and stays put. */
    Camp camp;
    int camp_ok = camp_open(&camp, win, au, "campaign.pack", mcfg.pack, err, sizeof err);
    if (!camp_ok)
        fprintf(stderr, "campaign: %s\n", err);

    int rc = 0;
    int round = 0;
    unsigned long long menu_digest = 0;   /* the menu before any mission ever ran */
    AppState state = APP_MENU;

    /* THE MOVIES, and under the harness they are a gate rather than a courtesy.
     *
     * A movie is the only screen that borrows the window without going through the
     * menu's present or the renderer's, so it is the easiest place to leave GL in a
     * state the next screen draws through.
     *
     * The pristine digest is therefore taken BEFORE the movies run, from a menu frame
     * drawn on a context nothing has touched yet. The old order took it AFTER them,
     * which meant any GL damage a movie caused was baked into the reference itself
     * and the later comparisons proved nothing about the movies at all: menu1 was
     * "corrupted menu equals corrupted menu, PASS". Now menu1 must equal a menu the
     * movies never had a chance to poison, so movie-induced corruption fails the
     * round trip on its own.
     *
     * Bounded to 24 frames under the harness so it costs the gate a few seconds. */
    if (g_harnessTicks > 0) {
        menu.cfg.movie_shotdir = g_shotDir;
        menu.cfg.movie_stop_after = 24;

        menu.mx = DM_SCREEN_W / 2;
        menu.my = DM_SCREEN_H / 2;
        menu.st.pressed = -1;
        menu.st.selected = DM_TESTMAP;
        dms_redraw(&menu);
        dms_draw(&menu);
        gl_probe(win, "pristine-menu-frame");
        double ink0 = 0.0;
        menu_digest = frame_digest(win, &ink0);
        shot(win, "h_menu0.png");
        printf("HARNESS|menu0|ink=%.4f|digest=%016llx (pristine, before any movie)\n",
               ink0, menu_digest);
        if (ink0 < 0.05) {
            printf("HARNESS|FAIL|menu0 is blank (ink %.4f) before any movie ran\n", ink0);
            g_harnessFails++;
        }
        SDL_GL_SwapWindow(win);
    }
    if (mcfg.logo && !dms_logo(&menu))
        state = APP_DONE;
    if (g_harnessTicks > 0 && state != APP_DONE && mcfg.intro) {
        /* Exactly the call the Intro button makes, so this proves the button. */
        if (!dms_intro(&menu))
            state = APP_DONE;
        printf("HARNESS|movies|logo and intro played, shots in %s\n", g_shotDir);
        fflush(stdout);
    }

    while (state != APP_DONE) {
        printf("APP|state|%s\n", state_name(state));
        fflush(stdout);

        if (state == APP_MENU) {
            if (g_harnessTicks > 0) {
                /* Prove the menu is on the glass BEFORE anything is clicked. Round 1
                   is drawn on a context two movies have just used, round 2+ on one a
                   mission has just finished using; each must match the pre-movie
                   pristine digest taken above. */
                char name[64];
                /* The pointer goes back to the middle before the proof shot, so the
                   only thing that could differ between visit 1 and visit 3 is the
                   handoff itself. */
                menu.mx = DM_SCREEN_W / 2;
                menu.my = DM_SCREEN_H / 2;
                menu.st.pressed = -1;
                menu.st.selected = DM_TESTMAP;
                dms_redraw(&menu);
                dms_draw(&menu);
                gl_probe(win, "after-menu-frame");
                double ink = 0.0;
                const unsigned long long dig = frame_digest(win, &ink);
                snprintf(name, sizeof name, "h_menu%d.png", round + 1);
                shot(win, name);
                printf("HARNESS|menu%d|ink=%.4f|digest=%016llx\n", round + 1, ink, dig);
                if (ink < 0.05) {
                    printf("HARNESS|FAIL|menu%d is blank (ink %.4f): the plate did not "
                           "survive the handoff\n", round + 1, ink);
                    g_harnessFails++;
                }
                if (menu_digest == 0) {
                    menu_digest = dig;                  /* only if menu0 could not read back */
                } else if (dig != menu_digest) {
                    printf("HARNESS|FAIL|menu%d differs from the pristine pre-movie menu "
                           "(%016llx vs %016llx): a movie or a mission left GL state "
                           "behind that the menu is drawing through\n",
                           round + 1, dig, menu_digest);
                    g_harnessFails++;
                }
                SDL_GL_SwapWindow(win);

                if (round >= g_harnessRounds) {
                    state = APP_DONE;
                    continue;
                }
                int bx, by, bw, bh;
                dms_item_window_rect(&menu, DM_TESTMAP, &bx, &by, &bw, &bh);
                printf("HARNESS|click|TESTMAP at window rect %d,%d %dx%d\n", bx, by, bw, bh);
                harness_click(bx + bw / 2, by + bh / 2);
            }

            if (g_lobbyPlay && round == 0) {
                /* the rects only exist after a draw laid the menu out */
                menu.mx = DM_SCREEN_W / 2;
                menu.my = DM_SCREEN_H / 2;
                menu.st.pressed = -1;
                menu.st.selected = DM_SKIRMISH;
                dms_redraw(&menu);
                dms_draw(&menu);
                SDL_GL_SwapWindow(win);
                int bx, by, bw, bh;
                dms_item_window_rect(&menu, DM_SKIRMISH, &bx, &by, &bw, &bh);
                printf("LOBBYPLAY|click|%s at %d,%d\n", dm_item_label(DM_SKIRMISH),
                       bx + bw / 2, by + bh / 2);
                fflush(stdout);
                harness_click(bx + bw / 2, by + bh / 2);
            }

            if (g_flowTest && round == 0 && !g_camp.active) {
                /* the rects only exist after a draw laid the menu out */
                menu.mx = DM_SCREEN_W / 2;
                menu.my = DM_SCREEN_H / 2;
                menu.st.pressed = -1;
                menu.st.selected = DM_START;
                dms_redraw(&menu);
                dms_draw(&menu);
                SDL_GL_SwapWindow(win);
                int bx, by, bw, bh;
                dms_item_window_rect(&menu, DM_START, &bx, &by, &bw, &bh);
                printf("FLOWTEST|click|START at %d,%d\n", bx + bw / 2, by + bh / 2);
                fflush(stdout);
                harness_click(bx + bw / 2, by + bh / 2);
            }
            const int choice = dms_run(&menu);
            printf("APP|menu|choice=%d\n", choice);
            fflush(stdout);
            if (choice == DMS_QUIT || choice == DM_EXIT) {
                state = APP_DONE;
            } else if (choice == DM_TESTMAP) {
                /* Every entry below leaves skirmish OFF. The option persists across the
                   menu the way opt.pack does, and a Test Map started after a skirmish
                   would otherwise still be a skirmish. */
                opt.skirmish = 0;
                /* Test Map is how the game is played today: the campaign does not
                   exist yet, so Start New Game is drawn disabled and this is the one
                   live entry. It always loads the test scenario, whatever --scen said,
                   because the menu is the thing choosing the mission now. */
                opt.scen = "SCG90EA";
                /* The test map is GDI 1's terrain with a different object layout, so it
                   has no pack of its own and must borrow SCG01EA's.
                   UNCONDITIONALLY, not `if (!opt.pack)`. opt.pack persists across the
                   menu, so once a campaign mission had set it (SCG02EA's, say) every
                   later Test Map rendered GDI 1's object layout on the WRONG scenario's
                   terrain, tiberium and heightmap. Test Map always wants SCG01EA's pack;
                   there is nothing to preserve here. */
                opt.pack = "SCG01EA.pack";
                /* And leaving the campaign flagged active meant the Test Map's own
                   verdict was taken as a CAMPAIGN verdict below: it played a win movie,
                   scored the mission and advanced the scenario. Test Map is not the
                   campaign. */
                g_camp.active = 0;
                state = APP_GAME;
            } else if (choice == DM_SPECIAL) {
                opt.skirmish = 0;
                /* SPECIAL OPS. The list is built here rather than in the menu module
                   because this is the layer that may read a directory; dosops.c only
                   renders and hit-tests what it is handed. */
                specops_scan(base_dir(opt.dir));
                if (g_specops.empty()) {
                    fprintf(stderr, "menu: no Special Ops missions found in '%s' -- the "
                                    "mission INIs and their packs have to be installed "
                                    "alongside the campaign's\n",
                            opt.dir ? opt.dir : "missions/");
                } else {
                    const int pick = dms_special(&menu, &g_specopsRows[0],
                                                 (int)g_specopsRows.size());
                    if (pick == DMS_QUIT) {
                        state = APP_DONE;          /* the window was closed */
                    } else if (pick == DMS_CANCEL) {
                        /* Backed out of the list. Fall through and the main menu is
                           drawn again on the next pass. This arm exists so that
                           "Cancel" can never be mistaken for "close the program", which
                           is what it was doing while both were -1. */
                    } else if (pick >= 0) {
                        /* Each of these carries a pack of its own -- unlike Test Map,
                           which borrows GDI 1's -- so scenario and pack move together
                           and neither can be left over from a previous mission.
                           COPIED, not pointed at: opt.scen outlives this screen (the
                           mission may be restarted from the pause dialog), and the next
                           visit to the list clears g_specops and frees the string it
                           would have been pointing into. */
                        static char pickscen[16], pickpack[24];
                        snprintf(pickscen, sizeof pickscen, "%s",
                                 g_specops[pick].scen.c_str());
                        snprintf(pickpack, sizeof pickpack, "%s",
                                 g_specops[pick].pack.c_str());
                        opt.scen = pickscen;
                        opt.pack = pickpack;
                        g_camp.active = 0;   /* Special Ops is not the campaign */
                        state = APP_GAME;
                    }
                }
            } else if (choice == DM_USERMAPS) {
                /* USER MAPS. The same list screen Special Ops uses -- the two answer the
                   same question, so they get the same screen rather than a second one
                   that could drift from it. */
                usermaps_scan(base_dir(opt.dir));
                if (g_usermaps.empty()) {
                    fprintf(stderr, "menu: no singleplayer user maps yet. Make one in "
                                    "the editor (MAP > New Map) and save it; they are "
                                    "filed in %suser_maps/\n",
                            opt.dir ? opt.dir : "missions/");
                } else {
                    const int pick = dms_special(&menu, &g_usermapRows[0],
                                                 (int)g_usermapRows.size());
                    if (pick == DMS_QUIT) {
                        state = APP_DONE;
                    } else if (pick == DMS_CANCEL) {
                        /* back to the menu */
                    } else if (pick >= 0) {
                        /* COPIED, for the same reason Special Ops copies: opt.scen
                           outlives this screen and the next visit clears the vector it
                           would otherwise point into. */
                        static char uscen[16], upack[24], udir[512];
                        snprintf(uscen, sizeof uscen, "%s", g_usermaps[pick].scen.c_str());
                        snprintf(upack, sizeof upack, "%s", g_usermaps[pick].pack.c_str());
                        snprintf(udir, sizeof udir, "%suser_maps/", base_dir(opt.dir));
                        opt.scen = uscen;
                        opt.pack = upack;      /* borrowed: see usermaps_scan */
                        opt.dir  = udir;       /* the mission lives in user_maps/ */
                        opt.reskin = 1;        /* re-skin the borrowed pack from its .BIN */
                        g_camp.active = 0;
                        state = APP_GAME;
                    }
                }
            } else if (choice == DM_SKIRMISH) {
                /* SKIRMISH. ONE screen, not two: the campaign's side plate followed by
                   the plain map list has been replaced by the lobby, so picking a side,
                   an opponent count, a map and the match options is one act. The plate
                   itself is untouched and the campaign still opens with it.
                   Every control on the lobby writes a field the engine reads; see
                   menu/doslobby.h for the ones that are deliberately not drawn. */
                skirmish_scan(base_dir(opt.dir));
                /* AN EMPTY LIST STILL OPENS THE SCREEN, and until now it did not.
                   This arm printed one line to stderr and fell straight back to the
                   menu, so a player whose install is missing its SCM packs pressed
                   Skirmish, watched the button click, and got nothing. The button
                   was working; the game simply had nowhere to say so. The lobby
                   draws its own empty state, and its Play button is dead while the
                   list is empty (sk_item_disabled, SK_I_PLAY), so opening it risks
                   nothing and is the only place a player can be told anything at
                   all. The line below stays: it is the half that can name the
                   FOLDER, and it is what cnc3d-log.txt carries. */
                if (g_skirmish.empty())
                    fprintf(stderr, "menu: no skirmish maps found in '%s' -- the SCM "
                                    "mission INIs and their packs have to be installed "
                                    "alongside the campaign's\n",
                            opt.dir ? opt.dir : "missions/");
                {
                    SK_Lobby lob;
                    memset(&lob, 0, sizeof lob);
                    SK_Prev* prev = skirmish_previews();
                    /* &v[0] on an empty vector is undefined and the list is now
                       allowed to be empty; sk_init takes a null list with count 0. */
                    const int r = dms_lobby(&menu,
                                            g_skirmishMaps.empty()
                                                ? NULL : &g_skirmishMaps[0],
                                            (int)g_skirmishMaps.size(), prev, &lob,
                                            g_lobbyPlay ? &g_lobbyProbe : NULL);
                    if (r == DMS_QUIT) {
                        state = APP_DONE;
                    } else if (r == DMS_CANCEL) {
                        /* Backed out. The main menu is drawn again next pass; this arm
                           exists so Cancel can never be mistaken for "close the
                           program", which is what it was doing while both were -1. */
                    } else if (lob.map >= 0 && lob.map < (int)g_skirmish.size()) {
                        /* COPIED, not pointed at: opt.scen outlives this screen because
                           the match can be restarted from the pause dialog, and the next
                           visit to the lobby frees what it would point into. */
                        static char skscen[16], skpack[24];
                        snprintf(skscen, sizeof skscen, "%s",
                                 g_skirmish[lob.map].scen.c_str());
                        snprintf(skpack, sizeof skpack, "%s",
                                 g_skirmish[lob.map].pack.c_str());
                        opt.scen = skscen;
                        opt.pack = skpack;
                        /* A USER MAP lives in user_maps/ and its pack is BORROWED, so it
                           has to be re-skinned from its own .BIN. An official one is
                           read from the mission folder as before -- opt.dir is reset
                           either way, because the previous screen may have moved it. */
                        {
                            static char skdir[512];
                            const bool isUser = (size_t)lob.map >= g_skirmishUserFrom;
                            snprintf(skdir, sizeof skdir, "%s%s", base_dir(opt.dir),
                                     isUser ? "user_maps/" : "");
                            opt.dir = skdir;
                            opt.reskin = isUser ? 1 : 0;
                        }
                        opt.skirmish = 1;
                        skirmish_apply(&opt, &lob);
                        g_camp.active = 0;   /* a skirmish is not the campaign */
                        state = APP_GAME;
                    }
                }
            } else if (choice == DM_START) {
                opt.skirmish = 0;
                if (camp_ok) {
                    state = APP_SIDESELECT;
                } else {
                    fprintf(stderr, "menu: Start New Game needs campaign.pack "
                                    "(run game/bake_campaign.py)\n");
                }
            } else if (choice == DM_VISUALS) {
                /* The screen itself lives in the renderer: it is the same dialog the
                   pause menu walks to, so there is one Visuals page in the program and
                   not two that can drift. */
                if (game_visuals_open(win, opt.dospack, NULL))
                    state = APP_DONE;
            } else {
                /* Load Mission and Multiplayer are not built yet; the DOS menu drew
                   them and so do we, and pressing one puts you back where you were
                   rather than pretending. */
                fprintf(stderr, "menu: %s is not implemented yet\n", dm_item_label(choice));
            }
            continue;
        }

        /* ---- the campaign screens ------------------------------------------------ */
        if (state == APP_SIDESELECT) {
            const int side = camp_side_select(&camp);
            if (side < 0) { state = APP_DONE; continue; }
            g_camp.active = 1;
            g_camp.side = side;
            g_camp.scenario = 1;
            g_camp.dir = 'E';
            g_camp.var = 'A';
            /* Choose_Side itself plays the scenario-1 briefing (intro.cpp): GDI1
               for GDI; NOD1PRE for Nod (that movie is on the Nod disc and is a
               registered gap until CD-2's set is staged). */
            if (!camp_movie(win, au, side ? "NOD1PRE" : "GDI1")) { state = APP_DONE; continue; }
            state = APP_BRIEF;
            continue;
        }

        if (state == APP_BRIEF) {
            static char scen[12];
            camp_scen_name(scen, sizeof scen);
            char intro[16], brief[16], action[16];
            camp_ini_movies(opt.dir, scen, intro, brief, action, sizeof intro);
            printf("CAMPAIGN|brief|%s|intro=%s|brief=%s|action=%s\n",
                   scen, intro, brief, action);
            fflush(stdout);
            /* Start_Scenario's exact exceptions (scenario.cpp): the intro plays
               unless this is Nod scenario 1; the mission briefing plays unless this is GDI
               scenario 1 (Choose_Side already played both sides' first briefing). */
            int ok = 1;
            if (g_camp.scenario != 1 || g_camp.side == 0)
                ok = ok && camp_movie(win, au, intro);
            if (g_camp.scenario > 1 || g_camp.side == 1)
                ok = ok && camp_movie(win, au, brief);
            ok = ok && camp_movie(win, au, action);
            if (!ok) { state = APP_DONE; continue; }

            static char packname[20];
            snprintf(packname, sizeof packname, "%s.pack", scen);
            opt.scen = scen;
            opt.pack = packname;
            state = APP_GAME;
            continue;
        }

        /* ---- APP_GAME ------------------------------------------------------------ */
        round++;
        printf("APP|game|round=%d\n", round);
        fflush(stdout);

        if (g_harnessTicks > 0)
            printf("HARNESS|rss|round=%d|before-boot=%ld KiB\n", round, peak_kib());

        opt.forcewin_ticks = g_flowTest;
        if (!game_boot(win, &opt)) {
            fprintf(stderr, "app: the mission failed to start; back to the menu\n");
            g_harnessFails++;
            state = APP_MENU;
            continue;
        }

        if (g_harnessTicks > 0)
            printf("HARNESS|rss|round=%d|after-boot=%ld KiB\n", round, peak_kib());

        const int reason = game_loop(win, &opt);
        printf("APP|game|round=%d|reason=%d\n", round, reason);
        if (g_harnessTicks > 0) {
            long rss = peak_kib();
            printf("HARNESS|rss|round=%d|after-loop=%ld KiB\n", round, rss);
            if (round >= 1 && round <= RSS_MAX_ROUNDS)
                g_rssAfterRound[round - 1] = rss;
            if (round >= 2 && round <= RSS_MAX_ROUNDS) {
                long grow = rss - g_rssAfterRound[round - 2];
                printf("HARNESS|rss|round=%d|growth=%ld KiB over round %d "
                       "(known staircase ~19500, limit %d)\n",
                       round, grow, round - 1, RSS_GROWTH_LIMIT_KIB);
                if (grow > RSS_GROWTH_LIMIT_KIB) {
                    printf("HARNESS|FAIL|rss grew %ld KiB in one boot/shutdown cycle "
                           "(limit %d KiB): a new leak on top of the known staircase\n",
                           grow, RSS_GROWTH_LIMIT_KIB);
                    g_harnessFails++;
                }
            }
        }

        if (g_harnessTicks > 0) {
            /* One more frame, read back, THEN swapped: the PNG is provably this
               frame and not the one before it. */
            char name[64];
            game_draw(win);
            gl_probe(win, "after-game-frame");
            double ink = 0.0;
            const unsigned long long dig = frame_digest(win, &ink);
            snprintf(name, sizeof name, "h_game%d.png", round);
            shot(win, name);
            printf("HARNESS|game%d|ink=%.4f|digest=%016llx\n", round, ink, dig);
            if (ink < 0.05) {
                printf("HARNESS|FAIL|game%d is blank (ink %.4f)\n", round, ink);
                g_harnessFails++;
            }
            SDL_GL_SwapWindow(win);
        }

        /* The verdict must outlive game_shutdown (it wipes per-mission state, and
           the movie/score/map screens run after it). */
        GameOverInfo gover = *game_over_info();

        game_shutdown();

        if (g_flowTest && round >= g_flowRounds) {
            printf("FLOWTEST|complete|%d missions booted and returned through the "
                   "full flow\n", round);
            fflush(stdout);
            state = APP_DONE;
        }
        else if (g_lobbyPlay) {
            /* THE ONE THING A SCREENSHOT CANNOT SHOW: whether the teams the drop down
               set actually formed inside the brain. game_skirmish_teams_ok compares the
               engine's own Get_Ally_Flags against the seats the lobby put on the human's
               team; a disagreement fails the RUN, so the gate that already drives this
               route catches it without needing to know what an ally flag is. */
            const int tok = game_skirmish_teams_ok();
            printf("LOBBYPLAY|teams|%s\n",
                   tok > 0 ? "the alliance the lobby asked for is the one the engine built"
                           : (tok == 0 ? "MISMATCH between the lobby and the engine"
                                       : "no skirmish was started"));
            if (tok <= 0) rc = 1;
            printf("LOBBYPLAY|complete|the match the lobby described booted and "
                   "returned\n");
            fflush(stdout);
            state = APP_DONE;
        }
        else if (reason == GAME_EXIT_APP) { state = APP_DONE; }
        else if (reason == GAME_EXIT_ERROR) { rc = 1; state = APP_DONE; }
        else if (reason == GAME_EXIT_RESTART) {
            /* RESTART MISSION. Straight back into APP_GAME with the same opt and the
               same g_camp, so a restarted campaign mission is still a campaign mission
               and still knows which one it is. Never APP_BRIEF: 1995's Do_Restart calls
               Start_Scenario with briefing = false (scenario.cpp:728), and a player who
               has just asked to start over does not want the mission briefing again.

               There is no new teardown here and none is needed. game_shutdown has
               already run by this point, and booting the same scenario name again is
               byte for byte the sequence the campaign runs between mission one and
               mission two: CNC_Start_Custom_Instance begins with Clear_Scenario
               (dllinterface.cpp:1413) and then re-reads the INI. */
            printf("RESTART|boot|scen=%s\n", opt.scen ? opt.scen : "");
            fflush(stdout);
            state = APP_GAME;
        }
        else if ((reason == GAME_EXIT_WON || reason == GAME_EXIT_LOST) && g_camp.active) {
            /* Do_Win / Do_Lose, the after-mission half. */
            if (!camp_movie(win, au, gover.movie)) { state = APP_DONE; continue; }
            if (reason == GAME_EXIT_LOST) {
                /* DOS confirms a replay; this build goes straight back into the
                   briefing chain for the same scenario (registered simplification). */
                printf("CAMPAIGN|retry|scenario=%d\n", g_camp.scenario);
                fflush(stdout);
                state = APP_BRIEF;
                continue;
            }
            CampScore cs;
            memset(&cs, 0, sizeof cs);
            cs.score = gover.score;
            cs.leadership = gover.leadership;
            cs.efficiency = gover.efficiency;
            cs.nod_killed = gover.nod_killed;
            cs.gdi_killed = gover.gdi_killed;
            cs.civ_killed = gover.civ_killed;
            cs.nod_bldg = gover.nod_bldg;
            cs.gdi_bldg = gover.gdi_bldg;
            cs.civ_bldg = gover.civ_bldg;
            cs.credits = gover.credits;
            cs.minutes = gover.minutes;   /* the TIME clock on the score screen */
            cs.win = 1;
            if (camp_score(&camp, &cs, g_camp.side, g_camp.scenario) < 0) {
                state = APP_DONE;
                continue;
            }
            /* the last mission has no map screen; the endings are a registered gap */
            const int last = g_camp.side ? 13 : 15;
            if (g_camp.scenario >= last) {
                printf("CAMPAIGN|complete|%s\n", g_camp.side ? "NOD" : "GDI");
                fflush(stdout);
                g_camp.active = 0;
                state = APP_MENU;
                continue;
            }
            char dir = 'E', var = 'A';
            if (camp_mapsel(&camp, g_camp.side, g_camp.scenario, &dir, &var) < 0) {
                state = APP_DONE;
                continue;
            }
            g_camp.dir = dir;
            g_camp.var = var;
            g_camp.scenario++;
            state = APP_BRIEF;
        }
        else if (reason == GAME_EXIT_WON || reason == GAME_EXIT_LOST) {
            /* the Test Map: a verdict just goes back to the menu */
            g_camp.active = 0;
            state = APP_MENU;
        }
        else {
            /* ANY other way out of a mission -- Abort from the pause dialog above all --
               ends the campaign run. This used to fall through with g_camp.active still
               set, so the campaign stayed "active" for the rest of the session and the
               next mission's verdict, Test Map included, was processed as a campaign
               verdict. */
            g_camp.active = 0;
            state = APP_MENU;
        }
    }
    camp_close(&camp);

    dms_close(&menu);
    game_set_audio(NULL);
    audio_boot_shutdown(au);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();

    if (g_harnessTicks > 0) {
        printf("HARNESS|end|%d round(s), %d failure(s)\n", round, g_harnessFails);
        if (g_harnessFails) rc = 1;
    }
    return rc;
}
