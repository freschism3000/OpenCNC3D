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
#include <algorithm>
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
/* OUTSIDE the extern "C" block below, deliberately: it pulls in SDL.h, and SDL's
   headers manage their own linkage. Nesting them inside another extern "C" is the
   kind of thing one toolchain forgives and the next one does not. */
#include "../game/brain_path.h"
extern "C" {
#include "dosmenu_shell.h"
#include "dosops.h"
#include "doslobby.h"
#include "dosmp.h"
#include "../net/netmatch.h"
#include "../net/netbeacon.h"
#include "../menu/mpbrowse.h"
#include "../net/roomcode.h"
#include <dlfcn.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "../game/dossave.h"
#include "audioboot.h"
#include "campaign.h"
#include "../video/movieplay.h"
#include "../video/moviesnd.h"
}

/* What the process is actually costing, for the harness only. A boot/shutdown pair that
   leaks shows up here as a staircase, and a staircase is the difference between "you can
   quit to the menu" and "you can quit to the menu twice". Not compiled on Windows: the
   shell there would use GetProcessMemoryInfo, and the shipping build needs neither. */
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>
/* THE NUMBER THE LIMIT IS ASSERTED ON IS THE LIVE HEAP: the bytes this process has asked
   malloc for and not given back, summed over every zone. Nothing else on this list can
   answer the question the gate is asking.

   The old assertion read the RESIDENT SET, which counts every page the kernel happens to
   have left mapped in, the malloc zones' freed-but-not-yet-reclaimed pages included, and
   whether those are still resident is a property of how much memory pressure the machine
   is under rather than of this build. Measured over nine boot/shutdown cycles of one
   unchanged binary: resident grew 161.9 MB while the process's DIRTY total grew 24.3 MB,
   so seven eighths of what the old assertion read was clean pages the kernel could have
   taken back at any moment. That is why three runs of the same binary on an otherwise
   idle machine reported the first cycle's growth as 20288, 33864 and 45936 KiB against a
   30000 KiB limit: one pass and two failures out of identical code.

   THE PHYSICAL FOOTPRINT IS BETTER AND STILL NOT GOOD ENOUGH, which is worth writing down
   because it was tried second. It is what the kernel charges the process for, dirty and
   compressed with no clean file-backed pages, and it is the number Activity Monitor
   shows -- but a leak does not have to add a page to it. The allocator already holds
   dirty pages from the last round's churn, and a leak that fits in them raises nothing.
   Measured on a build with a known 2 MB-a-round leak in it and on the same build with
   that leak fixed: the footprint after ten cycles landed within 1 MB of the same place
   either way, while the live heap told the two apart on the first cycle. It is printed
   beside the assertion as context, not asserted on.

   getrusage's ru_maxrss was tried before any of these and is useless here: it is a PEAK,
   and the harness itself allocates a 2.7 MB readback buffer with two more per screenshot,
   so a peak climbs every round whether anything leaks or not.

   WHAT THIS NUMBER CANNOT SEE is memory that is not malloc's: textures and buffers living
   on the card. A leak of those alone would pass here. In practice every pack this program
   uploads holds a matching CPU-side copy or table, so the leaks that have actually
   happened were visible in both; the gap is real and worth knowing about rather than
   assuming coverage. */
static long heap_kib(void)
{
    malloc_statistics_t st;
    memset(&st, 0, sizeof st);
    malloc_zone_statistics(NULL, &st);   /* NULL = every zone, summed */
    return (long)(st.size_in_use / 1024);
}
static long phys_kib(void)
{
    task_vm_info_data_t vm;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  (task_info_t)&vm, &count) != KERN_SUCCESS)
        return 0;
    return (long)(vm.phys_footprint / 1024);
}
/* The quantity the old assertion used, still printed so a log from before this change and
   a log from after it can be laid against each other. Nothing asserts on it. */
static long res_kib(void)
{
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) != KERN_SUCCESS)
        return 0;
    return (long)(info.resident_size / 1024);
}
#else
static long heap_kib(void) { return 0; }
static long phys_kib(void) { return 0; }
static long res_kib(void) { return 0; }
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
/* --matchshot: draw the end-of-match debrief from a made-up table and photograph it.
   The screen is otherwise reachable only by playing a match to its finish, which no gate
   can do in a second, and a screen no gate can see is a screen that rots. */
static int  g_matchShot = 0;
/* CONTINUE, on the end-of-match screen, goes back where the match came from. A network
   match came from the multiplayer browser and that is where the player wants to be: the
   list of games, ready to join another one. A Skirmish came from the main menu. Set by
   the debrief, consumed by the menu loop on its next pass. */
static int  g_backToBrowser = 0;
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
    /* THE LIST IS FILED, not flat: by faction first, the way the cartridge's own
       FACTION SELECTION screen sits in front of its SPECIAL OPS, and then by where the
       mission came from. `group` is one of SPECOP_G_*; a row with `header` set is the
       heading a section starts with and carries no scenario at all. */
    int group;
    int header;
    SpecOp() : w(0), h(0), starts(0), group(0), header(0) {}
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

/* WHERE EACH MISSION CAME FROM, which is how the list is filed.
 *
 * Two sources and one bin, and the section order on screen is faction first: GDI's two
 * sections, then Nod's two, then the test scenarios of both sides together at the
 * bottom.
 *
 *   COVERT   the 1996 Covert Operations disc. These are the fifteen the disc carried
 *            (fourteen have terrain on the cartridge; SCB30EA does not and cannot be
 *            offered), and they are the rows whose INIs ship a [Basic] Name=.
 *   N64      the cartridge's own extras: the two console-exclusive missions with a
 *            briefing in the ROM's text bank, and the five PlayStation-era Special Ops
 *            the cartridge reuses. Anything found on disk that this table does not
 *            know is filed here too, because the cartridge's asset directory is where
 *            an unknown non-campaign scenario would have come from.
 *   TEST     the ROM's own development table at 0x223B00 (DYNAMIC: SCG73EA, SCG74EA;
 *            STATIC: SCB70EA), SCG32EA which is byte for byte SCG73EA's layout, the two
 *            campaign-flow variants SCG71EB and SCG72EA, and the renderer's own Test
 *            Map SCG90EA, which is always the last row.
 *
 * SCG74EA is filed as a test scenario although its INI names it "Blindsided": it is the
 * Blindsided map with GDI 1's flow bolted on (Action=LANDING, Win=CONSYARD, Brief=GDI1,
 * Percent=0) and the ROM lists it under DYNAMIC beside SCG73EA. The Name= still wins
 * for the row's title; the heading and the code say what it is. */
enum { SPECOP_G_COVERT = 0, SPECOP_G_N64 = 1, SPECOP_G_TEST = 2 };

static const char* const SPECOP_COVERT[] = {
    "SCG22EA", "SCG23EA", "SCG36EA", "SCG38EA", "SCG40EA", "SCG41EA", "SCG50EA",
    "SCB20EA", "SCB21EA", "SCB30EA", "SCB31EA", "SCB32EA", "SCB33EA", "SCB35EA",
    "SCB37EA"
};
static const char* const SPECOP_TEST[] = {
    "SCG32EA", "SCG71EB", "SCG72EA", "SCG73EA", "SCG74EA", "SCB70EA", "SCG90EA"
};

static int specop_group(const char* scen)
{
    size_t i;
    for (i = 0; i < sizeof SPECOP_COVERT / sizeof SPECOP_COVERT[0]; i++)
        if (!strcmp(SPECOP_COVERT[i], scen)) return SPECOP_G_COVERT;
    for (i = 0; i < sizeof SPECOP_TEST / sizeof SPECOP_TEST[0]; i++)
        if (!strcmp(SPECOP_TEST[i], scen)) return SPECOP_G_TEST;
    return SPECOP_G_N64;
}

/* THE TEST MAP. GDI 1's terrain with a different object layout and a full build list,
   so it has no pack of its own and borrows SCG01EA's. It used to be a main menu button
   of its own; it is the last row of this list now. Offered only when its INI and the
   pack it borrows are both installed, the rule every other row follows. */
#define SPECOP_TESTMAP_SCEN "SCG90EA"
#define SPECOP_TESTMAP_PACK "SCG01EA.pack"
#define SPECOP_TESTMAP_NAME "Test Map"

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
   file that is not there. It also fixes the order by construction: ascending within a
   section, the same on every machine.

   The list is then FILED into five sections, each under a heading row, in this order:
   GDI COVERT OPERATIONS, GDI SPECIAL OPS, NOD COVERT OPERATIONS, NOD SPECIAL OPS, TEST
   MAPS. A section with nothing in it gets no heading. The Test Map is appended to the
   last section after every probed row, so it is the last row whatever else is
   installed. g_specops and g_specopsRows stay one to one, headings included, so the
   index the list screen hands back reads the same entry in both. */
static void specops_add_row(std::vector<SpecOp>& out, const char* dir, const char* code,
                            const char* pack, const char* fixed_name)
{
    char path[512];
    FILE* f;
    snprintf(path, sizeof path, "%s%s.INI", dir && *dir ? dir : "missions/", code);
    f = fopen(path, "rb");
    if (!f) return;
    fclose(f);
    /* No pack, no row: a mission the player can select and cannot play is worse than
       one that is not offered. */
    f = fopen(pack, "rb");
    if (!f) return;
    fclose(f);
    SpecOp so;
    so.scen = code;
    so.pack = pack;
    so.name = fixed_name ? std::string(fixed_name) : specop_ini_name(dir, code);
    /* The file's own Name= wins; the table only fills the gaps. */
    if (so.name.empty()) {
        const char* t = specop_title(code);
        if (t) so.name = t;
    }
    so.group = specop_group(code);
    out.push_back(so);
}

static void specops_scan(const char* dir)
{
    static const char SIDE[2] = { 'G', 'B' };
    static const char WEST[2] = { 'E', 'W' };
    int s, n, w, v;
    std::vector<SpecOp> found;

    g_specops.clear();
    g_specopsRows.clear();
    for (s = 0; s < 2; s++)
        for (n = 20; n < 90; n++)      /* 01..15 is the campaign, 90+ is the harness */
            for (w = 0; w < 2; w++)
                for (v = 0; v < 3; v++) {
                    char code[16], pack[24];
                    snprintf(code, sizeof code, "SC%c%02d%c%c", SIDE[s], n, WEST[w],
                             (char)('A' + v));
                    snprintf(pack, sizeof pack, "%s.pack", code);
                    specops_add_row(found, dir, code, pack, NULL);
                }
    specops_add_row(found, dir, SPECOP_TESTMAP_SCEN, SPECOP_TESTMAP_PACK,
                    SPECOP_TESTMAP_NAME);

    /* File them. The probe order above is GDI then Nod, ascending, so within a
       section the rows are already in code order and only the membership is tested. */
    static const struct { const char* heading; int nod; int group; } SECTION[] = {
        { "GDI COVERT OPERATIONS", 0, SPECOP_G_COVERT },
        { "GDI SPECIAL OPS",       0, SPECOP_G_N64 },
        { "NOD COVERT OPERATIONS", 1, SPECOP_G_COVERT },
        { "NOD SPECIAL OPS",       1, SPECOP_G_N64 },
        { "TEST MAPS",            -1, SPECOP_G_TEST },
    };
    for (size_t k = 0; k < sizeof SECTION / sizeof SECTION[0]; k++) {
        bool any = false;
        for (size_t i = 0; i < found.size(); i++) {
            const int nod = found[i].scen[2] == 'B' ? 1 : 0;
            if (found[i].group != SECTION[k].group) continue;
            if (SECTION[k].nod >= 0 && nod != SECTION[k].nod) continue;
            if (!any) {
                SpecOp h;
                h.name = SECTION[k].heading;
                h.header = 1;
                h.group = SECTION[k].group;
                g_specops.push_back(h);
                any = true;
            }
            g_specops.push_back(found[i]);
        }
    }

    /* Built after the list is complete, because these point into its strings. */
    int missions = 0;
    for (size_t i = 0; i < g_specops.size(); i++) {
        DO_Mission r;
        memset(&r, 0, sizeof r);
        r.scen = g_specops[i].scen.c_str();
        r.name = g_specops[i].name.empty() ? NULL : g_specops[i].name.c_str();
        r.nod = (unsigned char)(g_specops[i].scen.size() > 2 && g_specops[i].scen[2] == 'B'
                                    ? 1 : 0);
        r.header = (unsigned char)(g_specops[i].header ? 1 : 0);
        if (!r.header) missions++;
        g_specopsRows.push_back(r);
    }
    printf("APP|specops|%d missions|%d rows\n", missions, (int)g_specops.size());
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

/* ------------------------------------------------------------------------------ *
 *  LOADING A SAVED GAME FROM OUTSIDE THE MISSION IT WAS SAVED IN.
 *
 *  A slot records which scenario and which .pack it was saved from, and the campaign
 *  position at the time (game/dossave.h). A process can only load a slot into the
 *  mission it booted, so the shell boots THAT mission and asks game_boot to load the
 *  slot the moment the world is up (GameOpts::load_slot1). Two callers: the main menu's
 *  Load Mission, and a pause-dialog load that named another mission's slot
 *  (GAME_EXIT_LOADSLOT). Returns 1 when the slot could be described, 0 when the index
 *  no longer has it.
 *
 *  A user map's slot is booted the way User Maps boots it: the record's pack is the
 *  borrowed theater pack, the INI lives in user_maps/, and the pack is re-skinned from
 *  the map's own .BIN. The scenario name says which kind it is (USERnn).
 * ------------------------------------------------------------------------------ */
static int load_slot_prepare(GameOpts* opt, int slot)
{
    DS_Slot tab[DS_SLOTS];
    static char lscen[16], lpack[24], ldir[512];
    if (slot < 0 || slot >= DS_SLOTS) return 0;
    ds_read_index(tab);
    if (!tab[slot].used) {
        fprintf(stderr, "load: slot %d is empty\n", slot);
        return 0;
    }
    snprintf(lscen, sizeof lscen, "%s", tab[slot].scen);
    snprintf(lpack, sizeof lpack, "%s", tab[slot].pack);
    opt->scen = lscen;
    opt->pack = lpack;
    opt->skirmish = 0;
    opt->reskin = 0;
    opt->dir = base_dir(opt->dir);
    if (!strncmp(lscen, "USER", 4)) {
        snprintf(ldir, sizeof ldir, "%suser_maps/", base_dir(opt->dir));
        opt->dir = ldir;
        opt->reskin = 1;
    }
    opt->build = tab[slot].build;
    opt->load_slot1 = slot + 1;
    g_camp.active = tab[slot].camp_active;
    g_camp.side = tab[slot].camp_side;
    g_camp.scenario = tab[slot].camp_scenario;
    g_camp.dir = tab[slot].camp_dir ? (char)tab[slot].camp_dir : 'E';
    g_camp.var = tab[slot].camp_var ? (char)tab[slot].camp_var : 'A';
    printf("APP|load|slot=%d|scen=%s|pack=%s|dir=%s|campaign=%d/%d/%d\n", slot, lscen,
           lpack, opt->dir, g_camp.active, g_camp.side, g_camp.scenario);
    fflush(stdout);
    return 1;
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
/* The lobby's word for an unpicked start on the wire and the engine's are one number. */
static_assert(SK_START_RANDOM == CNC3D_START_RANDOM, "doslobby.h and cnc_game.h disagree on RANDOM");

static void lobby_report_seats(const SK_Lobby* lob)
{
    /* ALL EIGHT, not ai_count + 1: with a BLOCKed seat in the middle (mode 3) the seats
       in play are not a prefix, and a gate filters them by mode. */
    const int players = 8;
    for (int i = 0; i < players; i++)
        printf("APP|lobby|player=%d|house=%s|team=%d|colour=%d|mode=%d|start=%d\n", i,
               lob->house[i] ? "Nod" : "GDI", lob->team[i] + 1, lob->colour[i],
               lob->mode[i], lob->start_wp[i]);
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
/* THE BRAIN'S ORDER-WIRE HASH, read WITHOUT starting a game.
   Two peers whose engines lay an order out differently cannot exchange one, and the
   handshake refuses that pair by name. But the handshake now happens in the LOBBY, before
   any mission is booted, so the number has to be available before the brain is running.
   CNC3D_Event_ABI is a plain export that touches no game state, so opening the library and
   calling one function is enough and costs a few milliseconds once.

   RETURNS 0 IF IT CANNOT BE READ, AND ZERO IS NOW A REFUSAL RATHER THAN A PASS. It used
   to mean "could not compute one", and the check was skipped: two builds that had each
   failed to read their own engine compared 0 against 0, matched, and played with the
   compatibility check silently off. The handshake now refuses a zero on either side, on
   its own reason, ahead of the mismatch and the map. A build with no readable brain still
   OPENS the multiplayer screen; what it can no longer do is be seated. */
static unsigned mp_brain_abi(const char* dylib)
{
    static unsigned cached = 0;
    static bool tried = false;
    if (tried) return cached;
    tried = true;
    /* NO PATH GIVEN (the launcher and the .app pass none): ask the ONE search the game
       itself uses. This function used to carry its own list of three hardcoded ".dylib"
       names, which is why a Windows build found nothing, announced an order-wire hash of
       0, and was refused by every Mac host as an incompatible engine -- while two Windows
       builds, both announcing 0, matched each other and switched the check off. The list
       lives in game/brain_path.h now and that header carries the whole account. */
    if (!dylib || !*dylib)
        dylib = cnc3d_find_brain(NULL);
    if (dylib && *dylib) {
        void* h = dlopen(dylib, RTLD_LAZY | RTLD_LOCAL);
        if (h) {
            typedef int (*abifn)(unsigned*, int);
            abifn fn = (abifn)dlsym(h, "CNC3D_Event_ABI");
            if (fn) {
                unsigned slots[32];
                memset(slots, 0, sizeof slots);
                const int n = fn(slots, 32);
                if (n > 10) cached = slots[10];
            }
            dlclose(h);
        }
    }
    if (!cached) {
        fprintf(stderr, "menu: could not read the brain's order-wire layout from '%s'; "
                        "games will not be checked for engine compatibility\n",
                dylib ? dylib : "(none)");
    }
    return cached;
}

/* The index of a skirmish map by its scenario name, or -1. */
static int mp_map_index(const char* scen)
{
    for (int i = 0; i < (int)g_skirmish.size(); i++)
        if (g_skirmish[i].scen == scen) return i;
    return -1;
}

/* The scenario's own bytes, the .INI and the .BIN, hashed the same way the renderer
   hashes them so the two agree. 0 means the files could not be read. */
static unsigned mp_scen_hash(const char* dir, const char* scen)
{
    static const char* const EXT[2] = { ".INI", ".BIN" };
    unsigned h = 2166136261u;
    int got = 0, i;
    if (!scen || !*scen) return 0u;
    for (i = 0; i < 2; i++) {
        char path[1024];
        unsigned char buf[8192];
        size_t n;
        FILE* f;
        snprintf(path, sizeof path, "%s%s%s", dir ? dir : "", scen, EXT[i]);
        f = fopen(path, "rb");
        if (!f) continue;
        for (n = 0; EXT[i][n]; n++) { h ^= (unsigned char)EXT[i][n]; h *= 16777619u; }
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            size_t k;
            for (k = 0; k < n; k++) { h ^= buf[k]; h *= 16777619u; }
        }
        fclose(f);
        got++;
    }
    return got ? (h ? h : 1u) : 0u;
}

/* THE HASHER netmatch ASKS THROUGH, and the one place the mission directory is parked
   for it. netmatch cannot open a file, so it cannot answer "have I got this map, and is
   my copy the host's copy" without being handed a way to look; this is that way. Zero
   from mp_scen_hash means neither the .INI nor the .BIN is there, which is exactly the
   "I do not have that map" the joiner needs to be told in words instead of finding out
   on the first frame that reads a cell. */
static char g_mpMissionDir[1024];

static unsigned mp_map_hash_hook(const char* scenario, void* user)
{
    (void)user;
    return mp_scen_hash(g_mpMissionDir, scenario);
}

/* Installed once, from main, before any room can be opened or joined. Both ends use it:
   a joiner to answer the WELCOME's map, a host to keep the fingerprint it advertises and
   refuses against following the map it actually has selected. */
static void mp_install_map_hasher(const char* missiondir)
{
    snprintf(g_mpMissionDir, sizeof g_mpMissionDir, "%s", missiondir ? missiondir : "");
    nm_set_map_hasher(mp_map_hash_hook, NULL);
}

/* Pack what the skirmish lobby answered into the wire's own setup, which is what a joiner
   adopts wholesale. The roster is people first and computers after, exactly as
   net_match_prepare builds it for the command line, so the two paths cannot disagree. */
static NmSetup* mp_build_setup(NmSetup* out, const SK_Lobby* lob, const MP_State* mp,
                               const char* missiondir, unsigned* scen_out)
{
    int i;
    memset(out, 0, sizeof *out);
    if (lob->map < 0 || lob->map >= (int)g_skirmish.size()) return NULL;
    snprintf(out->scenario, sizeof out->scenario, "%s",
             g_skirmish[lob->map].scen.c_str());
    out->credits = lob->credits;
    out->tiberium = lob->tiberium;
    out->crates = lob->crates;
    snprintf(out->name[0], sizeof out->name[0], "%s", nm_player_name());
    out->aitake = lob->aitake;
    out->shortgame = lob->shortgame;
    out->superweapons = lob->superweapons;
    out->bases = lob->bases;
    out->unit_count = lob->unit_count;
    out->build = lob->build;
    /* THE ROOM'S TICK RATE. There is no speed row on the skirmish screen, so the room
       opens on the wire's own default, which is the same slider index the Game Controls
       page ships: a player meets one pace in a campaign mission, a skirmish and a
       network game. Written as the constant rather than as a number because three
       builders of an NmSetup have to say the same thing, and a literal here is a copy
       that can be left behind. */
    out->speed = NM_DEFAULT_SPEED;
    /* THE ROOM IS AS WIDE AS THE MAP HAS STARTS (no player count on the host
       tab), and every seat in it carries the mode the lobby screen last gave it. A fresh
       lobby says 0 (HUMAN) for every seat, which netmatch reads as "open until somebody
       sits down"; seats past the room are BLOCKED, not bots: they are not in the match
       and nobody waits on them. A map whose starts are not known opens all eight. */
    {
        int starts = (lob->map < (int)g_skirmishMaps.size()) ? g_skirmishMaps[lob->map].starts : 0;
        if (starts <= 0) starts = NM_MAX_SEATS;
        if (starts < 2) starts = 2;
        if (starts > NM_MAX_SEATS) starts = NM_MAX_SEATS;
        out->seats = starts;
    }
    (void)mp;
    out->humans = 0;
    for (i = 0; i < NM_MAX_SEATS; i++) {
        out->house[i] = (unsigned char)(lob->house[i] ? 1 : 0);
        out->colour[i] = (unsigned char)((lob->colour[i] < 0 || lob->colour[i] > 7)
                                             ? (i & 7) : lob->colour[i]);
        out->team[i] = (unsigned char)lob->team[i];
        out->start[i] = (unsigned char)((lob->start_wp[i] >= 0) ? lob->start_wp[i]
                                                                  : CNC3D_START_RANDOM);
        /* A SKIRMISH ANSWER BECOMING A ROOM: this is the one place SET UP MATCH's screen
           (and the headless host's) hands over, and a hosted room starts EMPTY, so a
           skirmish's computers become open seats; a BLOCK carries. Without this every
           seat of the room is a computer and no joiner can sit. */
        out->mode[i] = (unsigned char)((i == 0) ? NM_SEAT_HUMAN
                                     : (i < out->seats
                                            ? (lob->mode[i] == NM_SEAT_BOT ? NM_SEAT_OPEN
                                                                            : lob->mode[i])
                                            : NM_SEAT_BLOCK));
        out->is_ai[i] = (unsigned char)(out->mode[i] == NM_SEAT_BOT ? 1 : 0);
        if (out->mode[i] == NM_SEAT_HUMAN || out->mode[i] == NM_SEAT_OPEN) out->humans++;
    }
    if (scen_out) *scen_out = mp_scen_hash(missiondir, out->scenario);
    return out;
}

static void skirmish_apply(GameOpts* opt, const SK_Lobby* lob)
{
    opt->side = lob->side;
    opt->ai_count = lob->ai_count;
    opt->build = lob->build;
    opt->credits = lob->credits;
    opt->tiberium = lob->tiberium;
    opt->crates = lob->crates;
    opt->ai_takeover = lob->aitake;
    opt->short_game = lob->shortgame;
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
        opt->seat_mode[i] = lob->mode[i];
        /* The colour index the SQUARE on the screen was filled from, which is the same
           index the engine calls that colour. All eight are copied even when fewer seats
           are in play: the lobby keeps the eight a permutation of 0..7, and a reader
           checking that no two players share one has to see all eight to do it. */
        opt->player_colour[i] = lob->colour[i];
        snprintf(opt->player_name[i], sizeof opt->player_name[i], "%s", lob->name[i]);
    }
    printf("APP|lobby|side=%s|ai=%d|build=%d|credits=%d|bases=%d|tiberium=%d|crates=%d|"
           "super=%d|units=%d|start0=%d\n",
           lob->side ? "Nod" : "GDI", lob->ai_count, lob->build, lob->credits,
           lob->bases, lob->tiberium, lob->crates, lob->superweapons, lob->unit_count,
           lob->start_wp[0]);
    lobby_report_seats(lob);
    fflush(stdout);
}

/* THE MATCH DEBRIEF, or nothing at all. Answers 1 to carry on and 0 only when the window
   was closed, so both call sites read the same way; a campaign abort leaves the table
   empty and falls straight through. */
static int app_match_score(Camp* camp, const MatchStats* ms)
{
    CampMatch cm;
    int i;
    if (!ms || !ms->valid || ms->nrows <= 0) return 1;
    memset(&cm, 0, sizeof cm);
    cm.nrows = ms->nrows > 8 ? 8 : ms->nrows;
    cm.win = ms->win;
    cm.seconds = ms->seconds;
    cm.net = ms->net;
    snprintf(cm.map, sizeof cm.map, "%s", ms->map);
    snprintf(cm.ended, sizeof cm.ended, "%s", ms->ended);
    for (i = 0; i < cm.nrows; i++) {
        const MatchStatRow* r = &ms->row[i];
        snprintf(cm.row[i].name, sizeof cm.row[i].name, "%s", r->name);
        cm.row[i].colour = r->colour;
        cm.row[i].killed = r->killed;
        cm.row[i].razed = r->razed;
        cm.row[i].lost = r->lost;
        cm.row[i].harvested = r->harvested;
        cm.row[i].score = r->score;
        cm.row[i].defeated = r->defeated;
        cm.row[i].is_local = r->is_local;
    }
    /* THE EMBLEM AND THE MUSIC FOLLOW THE FACTION THE PLAYER PLAYED, which in a match is
       not g_camp.side: that is the CAMPAIGN's side and is whatever the last campaign left
       behind, or zero. A commander who joined a game as Nod gets the Nod medallion
       turning in the corner and the Nod score theme, which is the track 1995 pressed and
       never played. */
    if (camp_match_score(camp, &cm, ms->side) < 0) return 0;
    /* And CONTINUE takes a network player back to the list of games rather than to the
       main menu, which is where they were when they chose this one. */
    if (ms->net) g_backToBrowser = 1;
    return 1;
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
static int camp_movie(SDL_Window* win, CncAudio* au, const char* name);

/* THE SAME PLAYER, LENT TO THE MISSION for Restate's Video button (game_set_movie_player
   in cnc_game.h). The window and the sound engine outlive every mission, so the pair is
   kept here for the length of the process and the callback reads them rather than
   being handed them. */
static struct { SDL_Window* win; CncAudio* au; } g_lentPlayer;
static int shell_play_movie(void* user, const char* name)
{
    (void)user;
    if (!g_lentPlayer.win) return 1;
    return camp_movie(g_lentPlayer.win, g_lentPlayer.au, name);
}

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
/* --loadmenushot FILE: the main menu's Load Mission dialog, one frame, then out. The
   same instrument --visualsshot is, for the same reason: the screen needs a context
   and nothing else, and a picture is the only proof it draws. */
static const char* g_loadMenuShot = NULL;
/* --bootfailshot FILE: attempt the mission start the command line describes, and when
   it is refused, draw the "Unable to start mission" box once, write it, and exit. The
   instrument for the refusal box, the way --loadmenushot is for the slot dialog: the
   box is what a player sees when a mission will not start, and a picture is the only
   proof it draws. The usual way to make the start refuse is a --dylib that does not
   exist. */
static const char* g_bootFailShot = NULL;
/* --lobbyshot DIR: open the SKIRMISH LOBBY at boot, drive every control on it with
   synthetic clicks on the rectangles the module itself reports, write a PNG of each
   frame, print the settings block that came out, and quit. It exists because a lobby is
   a screen made almost entirely of controls, and the only two questions worth asking
   about it -- does every label land inside its own box, and does every control write the
   field it claims to -- are both answerable without a human, and neither is answerable
   by "it did not crash". Implies --hidden and no movies. */
static const char* g_lobbyShot = NULL;
/* --specopsshot DIR: open the SPECIAL OPS list at boot, write a PNG of its first page
   and one of its last, print every heading in the order it is drawn, and quit. It
   exists so the list's filing can be asserted from outside: which sections are there,
   in what order, and that the Test Map is the last row. */
static const char* g_specopsShot = NULL;
/* --harnessscen CODE: the Special Ops row the harness plays instead of the Test Map.
   HOME and then DOWN once per mission row above it, so the list is walked the way a
   hand walks it; a code that is not in the list falls back to END, the Test Map. */
static const char* g_harnessScen = NULL;
/* --harnessload: the harness clicks LOAD MISSION instead of SPECIAL OPS and answers the
   slot dialog with RETURN, which loads its first row, the newest save. The mission that
   boots is whichever the slot was saved in, so this is the one route that proves the
   menu's Load Mission end to end: the dialog, the record, the boot and the load. */
static int g_harnessLoad = 0;
/* --mpshot FILE: draw the MULTIPLAYER screen and write it, with no network and no
   clicking. It exists for the same reason --lobbyshot does: a screen made of labels is
   checked by arithmetic and by eye, and neither can happen if the only way to reach it is
   to have a second computer on the desk. */
static const char* g_mpShot = NULL;
/* --mpaddr: drive the multiplayer screen's own logic for the typed-address path, with no
   window, no network and no second process. What it covers is exactly what changed when
   that field was added -- the two focus chains, the Tab walk, the enablement rules and the
   host:port grammar -- and every one of those is a pure function of state, which is the
   kind of thing a screenshot answers badly and a call answers exactly. */
static int g_mpAddrTest = 0;
/* --mpclick: drive the multiplayer screen with synthetic clicks through its real SDL
   loop. --mpaddr asks the screen's pure functions directly and --mpshot draws a state it
   was handed; neither presses anything, so "a row highlights, and clicking it and JOIN
   reaches a lobby" was a claim about this screen that nothing in the tree could check. */
static int g_mpClickTest = 0;
/* --mpsort: the game list's order, identity, filters and scroll, asked of the screen's
   pure functions with no network. Sorting and a refill ten times a second are exactly
   where a selection quietly lands on another game, which a call answers exactly and a
   screenshot cannot. */
static int g_mpSortTest = 0;
/* --mpsortclick: the same list driven through the real screen loop against a game list on
   this machine, which the caller has seeded with rows. */
static int g_mpSortClick = 0;
/* --mpprefs PATH: have --mpsortclick remember the list's settings in PATH, as a player's
   launch remembers them beside the game. --mpprefsread PATH: read PATH the way a player's
   launch does, print what came back, and quit. The two together are a relaunch. */
static const char* g_mpPrefsPath = NULL;
static const char* g_mpPrefsRead = NULL;
/* --mpping: THE PING COLUMN, measured for real, through the real screen loop against a game
   list, a relay stand-in and two headless rooms on this machine, all started by the
   caller. --mprelay names the stand-in and must be on this machine.
   --mppingdirect PATH / --mppingrelayed PATH: the two rooms' logs, read to count the probes
   each room answered and when. --mppingshot PREFIX: write the live list as pictures. */
static int g_mpPing = 0;
static const char* g_mpPingDirectLog = NULL;
static const char* g_mpPingRelayedLog = NULL;
static const char* g_mpPingShot = NULL;
/* --mppublic: put a room that is NOT relayed on the public list, at the address it
   answers on. The screen asks for this with a box that says what it costs; on the command
   line the flag itself is the deliberate act. Without it this path lists relayed rooms
   only, which for a while was written down as the command line already being able to do
   both and was never true. */
static int g_mpPublic = 0;
/* --mpclickhost: add the HOST leg to --mpclick. Opt-in and not the default, because
   pressing HOST with INTERNET GAME ticked ARMS A RELAY, and the screen dials the built-in
   one rather than anything --mprelay names, so this leg reaches a public server somebody
   else pays for. Everything else --mpclick does stays on this machine. */
static int g_mpClickHost = 0;
/* --lobbyplay N: the same script, but on the REAL route -- click Multiplayer Game on
   the menu, drive the lobby, press Play, and let the state machine boot the match the
   lobby just described for N ticks. --lobbyshot proves the screen; this proves the
   WIRING, because the only thing that can answer "does pressing Play get exactly that
   match" is the engine's own report of the lobby it was handed. */
static int g_lobbyPlay = 0;
/* --mpplay MS: click MULTIPLAYER on the menu and let the REAL screen run for MS
   milliseconds, with its network live, then leave. --mpshot proves the labels fit; this
   proves the PATH -- the click, nb_browse_open, and the poll that runs ten times a second
   against a network that may have nothing on it. That path had no gate, and a freeze on it
   reached a release. */
static int g_mpPlay = 0;
/* --mphost TICKS / --mpjoin TICKS: THE LOBBY PATH, END TO END, WITH NO HANDS. One process
   opens a room on the real lobby state machine with the shipped skirmish map, the other
   joins it at 127.0.0.1, readies, the host starts, both boot the match the lobby armed
   and play TICKS engine ticks in lockstep, printing the world hash netmatch already
   prints. Given --script as well, a side plays that script instead of the tick bound, so
   the match has orders in it and the hashes compare two worlds that were actually
   played. It exists because the lobby path had no gate at all, and shipped with the
   brain's lockstep switch never thrown -- G199 proves eight peers through the
   command-line handshake, which is a different door into the same engine. */
static int g_mpHost = 0, g_mpJoin = 0;
/* --mpscreenstart, beside --mphost: THE SAME HEADLESS ROOM, BUT START IS THE LOBBY SCREEN'S
   OWN BUTTON. The headless host presses START from this file, which is not the door a
   person uses. With this flag the host waits only until the room is full and everyone in
   it is ready, then hands the room to the real lobby screen and clicks START GAME on the
   first frame the button can take it: the quickest hand there is, and so the moment a
   room has had the least chance to measure its link. The match that follows boots from
   what the screen hands back, exactly as a player's does, so NETSYNC on both sides checks
   that path as well. A tail of idle frames and an ESCAPE end a room that never starts,
   and that failure is said by name rather than left to a watchdog. */
static int g_mpScreenStart = 0;
/* --mpwait SECONDS / --mpname NAME: the same headless room, but ADVERTISED on
   the LAN and held open this long, so a person on another machine can find it in the
   browser and join it while the host end is driven from a shell. Without --mpwait the
   room is G205's: thirty seconds, no advert, a joiner at 127.0.0.1.
   Without --mpname the room has no name of its own and netmatch gives it the product's
   default, exactly as the multiplayer screen does for an empty name field. It used to be
   named "G205" here, which meant a headless room opened by a second front end was named
   as a gate's room rather than as that front end's, and the one gate that reads the name
   a front end's room carries could never see the product's default on this route. */
static int g_mpWaitMs = 30000;
static const char* g_mpName = NULL;
/* --mprelay HOST[:PORT] / --mproom #CODE / --mppass WORD: THE SAME HEADLESS ROOM, RUN
   THROUGH A RELAY. Until these existed the relayed path had exactly one door, the two
   click handlers on the multiplayer screen, so the only way to play a relayed match was
   by hand and nothing could gate it. The blocking doors cannot stand in: nm_host and
   nm_join refuse to be relayed because they have no passcode check, and a room with no
   door does not belong on the internet.
   --mprelay also names the server, which matters for more than convenience: the compiled
   in address is one machine in one country, and the lookahead a room agrees is measured
   off the round trip. A relay far enough away is the only way a single machine can
   reproduce a slow link, and a slow link is the one condition under which the lookahead
   rises above the built-in default at all. */
static const char*    g_mpRelay = NULL;
static unsigned short g_mpRelayPort = NM_RELAY_PORT;
static const char*    g_mpRoom = NULL;
/* --mpfromlist: the joiner takes the first game it can join out of the public list,
   instead of being told a code. It is how the browser is driven with no hand on it. */
static int            g_mpFromList = 0;
static const char*    g_mpPass = NULL;
/* --lobbyhost SECONDS / --lobbyjoin SECONDS: THE JOINER'S OWN ROW, ON THE
   REAL SCREEN, AGAINST A REAL HOST.

   Between them these two close the one hole the other lobby routes leave. --mphost and
   --mpjoin drive the lobby STATE MACHINE and draw no screen at all; --lobbyshot drives the
   real SCREEN and opens no socket. Neither can see the thing a joiner does that nobody
   else does: reach into its OWN row and change it, which is a click on a screen that has
   to become a packet. There was no gate on that at all, and three separate faults on that
   row were found by playing rather than by testing -- a joiner drawn with its own
   defaults instead of the host's,
   a row that was refused because it was somebody else's, and finally a colour and a start
   that could not be changed AT ALL because the send was read after the room was copied
   over the click it was meant to notice.

   --lobbyhost opens a room and holds it for SECONDS. It never presses START: what is under
   test is what the ROOM ends up holding, so the host's whole job is to be a real host,
   apply what arrives under its own rules, and say what its setup now says. --lobbyjoin
   joins that room at 127.0.0.1, waits up to SECONDS to be seated, and then opens the REAL
   lobby screen as a joiner with a script that opens its own row's drop down and picks a
   colour and a start position on it. Both ends print LOBBYSEAT| lines, and the assertion
   is the comparison between them: what the joiner asked for on its screen, and what the
   host's own setup holds afterwards. A pick that never left the joiner is exactly the
   shape of that comparison failing. */
static int g_lobbyHostMs = 0, g_lobbyJoinMs = 0;
/* THE ROOM --lobbyhost OPENS, AND THE TWO PICKS --lobbyjoin MAKES ON ITS OWN ROW.

   FOUR SEATS, NOT THE MAP'S EIGHT, and that is a measurement rather than a preference.
   netmatch's lobby_apply_seat refuses a colour that another seat in the room already
   holds, and a room as wide as SCM01EA's eight starts deals seats 0..7 the colours 0..7 --
   so there is no free colour left for anybody to ask for, and this would measure a
   refusal working rather than a pick arriving. A four seat room is what a host blocking
   the other four makes, and it leaves colours 4..7 spoken for by nobody. Starts need no
   such care: every seat's is RANDOM until somebody picks one.

   NEITHER PICK IS A DEFAULT. Seat 1 is dealt colour 1 and no start at all, so colour 5 and
   the fourth start cannot be the values that row would have had anyway -- which is the
   whole point of reading them off the far end of a wire. */
#define LOBBYSEAT_MAP    "SCM01EA"
#define LOBBYSEAT_ROOM   4
#define LOBBYSEAT_COLOUR 5
#define LOBBYSEAT_START  3   /* zero based: the fourth start position */
/* THE SCRIPT'S LENGTH, which is also its clock: dms_lobby runs exactly one step per frame
   and a frame is at least the loop's own SDL_Delay(16), so the steps between the last pick
   and the ESCAPE that leaves are how long the screen is left running for the room to
   answer. Four picks, then a settle, then the way out. The room polls ten times a second
   and the wire is loopback, so ~700 ms is roughly seven chances to be told; anything that
   needs more than that is a fault and should read as one. */
#define LOBBYSEAT_STEPS  48

/* A SK_Lobby FROM THE MATCH THE LOBBY AGREED ON, which is what a joiner has to boot with:
   the same mapping dms_lobby_net_sync makes on the screen, made once more here for the
   headless path that never draws one. Returns 0 when the setup names a map this machine
   does not have. */
static int mp_lobby_from_match(SK_Lobby* out)
{
    const NmSetup* ns = nm_lobby_setup();
    int i;
    if (!ns || !ns->scenario[0]) return 0;
    memset(out, 0, sizeof *out);
    out->map = -1;
    for (i = 0; i < (int)g_skirmish.size(); i++)
        if (g_skirmish[i].scen == ns->scenario) { out->map = i; break; }
    if (out->map < 0) return 0;
    out->credits = ns->credits;
    out->superweapons = ns->superweapons ? 1 : 0;
    out->crates = ns->crates ? 1 : 0;
    out->bases = ns->bases ? 1 : 0;
    out->tiberium = ns->tiberium ? 1 : 0;
    out->unit_count = ns->unit_count;
    out->build = ns->build;
    out->side = ns->house[0] ? 1 : 0;
    {
        int bots = 0, any = 0;
        for (i = 0; i < NM_MAX_SEATS; i++) {
            out->house[i] = ns->house[i] ? 1 : 0;
            out->team[i] = ns->team[i];
            out->colour[i] = ns->colour[i];
            /* Map the wire's RANDOM back to unpicked, and resolve NOTHING here: this
               runs on every peer independently, and a local draw is exactly the desync
               the whole match is built to avoid. The engine deals it. */
            out->start_wp[i] = (ns->start[i] == CNC3D_START_RANDOM) ? -1 : ns->start[i];
            out->mode[i] = ns->mode[i];
            snprintf(out->name[i], sizeof out->name[i], "%s", ns->name[i]);
            if (ns->mode[i]) any = 1;
            if (ns->mode[i] == NM_SEAT_BOT) bots++;
        }
        /* The computers are the BOT seats, wherever they sit; a setup with no modes
           (an older peer) is the prefix arithmetic it always was. */
        out->ai_count = any ? bots : (ns->seats > ns->humans ? ns->seats - ns->humans : 0);
    }
    return 1;
}

/* THE THREE PICTURES --mpplay LEAVES IN g_shotDir, and why there are three.
   mpplay_menu.png    the back buffer with the main menu on it, just before the click
   mpplay_screen.png  the back buffer on the multiplayer screen's leave frame -- what
                      the WINDOW shows
   mpplay_surface.png the 8-bit surface the screen drew into -- what --mpshot shows
   A screen that draws and never uploads makes the second identical to the first while
   the third looks perfect. That is the bug this reached a release with, and the reason
   the gate compares the window to the menu rather than trusting any surface. */
static void mpplay_grab(void* user)
{
    SDL_Window* win = (SDL_Window*)user;
    char path[512];
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(win, &w, &h);
    snprintf(path, sizeof path, "%s/mpplay_screen.png", g_shotDir);
    if (!game_grab_png(path, w, h))
        fprintf(stderr, "MPPLAY|FAIL|could not write %s\n", path);
    else
        printf("MPPLAY|grab|screen|%s|%dx%d\n", path, w, h);
    fflush(stdout);
}

/* IS SOMETHING OTHER THAN A HAND DRIVING THIS RUN? The shell's half of the list the
   renderer keeps in confine_automated, and it decides the same two things here: the
   window is hidden and the mixer is silent.

   A TEST RUN MUST NOT TAKE THE SCREEN OR THE SPEAKERS. The sound half of that was already
   right here; the WINDOW half was not. It hid only under --harness, while the suite drives
   this binary with --flowtest, --lobbyplay, --lobbyshot and --visualsshot, so the
   campaign-flow, movie and lobby gates each opened a real window that took the focus.
   IF YOU ADD AN AUTOMATED ENTRY POINT, ADD IT HERE, and to confine_automated in
   cnc_eyes.cpp. */
static int shell_automated(void)
{
    return g_harnessTicks > 0 || g_flowTest > 0 || g_lobbyPlay > 0 || g_mpPlay > 0
        || g_mpHost > 0 || g_mpJoin > 0
        || g_lobbyHostMs > 0 || g_lobbyJoinMs > 0
        || g_lobbyShot != NULL || g_visualsShot != NULL || g_hiddenWin != 0
        || g_specopsShot != NULL || g_loadMenuShot != NULL || g_bootFailShot != NULL
        || g_mpShot != NULL || g_mpAddrTest || g_mpClickTest
        || g_mpSortTest || g_mpSortClick || g_mpPrefsRead != NULL;
}
/* The script both harness routes drive, in one place so the two cannot disagree about
   what was clicked. It touches every control the screen has, in the order a player
   would reach them. */
static const DMS_LobbyStep LOBBY_SCRIPT[] = {
    /* The mouse half. */
    { SK_I_NOD,      500, 500, 0 },   /* the latched side pair             */
    /* THE MAP PICKER: the list is a window behind CHANGE MAP. Open it,
       take the FOURTH official row (my = 45 + 119 * 235 / 1000 = 72, row 3, SCM04EA on
       the staged tree, six starts), OK. Then open it again, walk both tabs, highlight
       the second row and ESCAPE: the run must still end on map 3, which is what proves
       that Escape and Cancel discard. If the picker ever stopped being modal that Escape
       would leave the lobby and the run would end with no settings block. */
    { SK_I_CHANGEMAP,  500, 500, 0 },
    { SK_I_PICK_LIST,  500, 235, 0 },
    { SK_I_PICK_OK,    500, 500, 0 },
    { SK_I_CHANGEMAP,  500, 500, 0 },
    { SK_I_PICK_TAB1,  500, 500, 0 },
    { SK_I_PICK_TAB0,  500, 500, 0 },
    { SK_I_PICK_LIST,  500, 100, 0 },   /* the second row, abandoned */
    { 0, 0, 0, SDLK_ESCAPE },           /* shuts the picker, NOT the lobby */
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
       more UPs reach Credits, Tech Level and the CHANGE MAP button (the AI Players gauge
       is gone, 5 Sep 2026), with DOWN coming back to Tech Level. No Space lands on the
       button, so the picker never opens from this half; the arrows cannot change the
       map any more, because the list lives in the picker. Page Up and Page Down are not
       driven by this half, and Escape is driven by the drop down steps below. */
    { 0, 0, 0, SDLK_SPACE },
    { 0, 0, 0, SDLK_UP },             /* over the two locked boxes         */
    { 0, 0, 0, SDLK_LEFT },           /* an eighth of the credit gauge     */
    { 0, 0, 0, SDLK_UP },
    { 0, 0, 0, SDLK_UP },
    { 0, 0, 0, SDLK_UP },             /* CHANGE MAP has the focus          */
    { 0, 0, 0, SDLK_DOWN },           /* back to Tech Level                */
    /* THE ROSTER AND ITS DROP DOWN, last because choosing a map RESEATS the opponents
       and would undo any of it. Every step here is deliberately a NON-DEFAULT answer, so
       a value that arrives at the engine cannot be the one it would have had anyway:
         - COMPUTER 4 is BLOCKED through its SEAT row, a hole in the MIDDLE of the roster
           (seats 0..3 and 5 play), which proves fewer opponents than the map seats still
           works with no gauge, and that the engine is handed the seats around the hole;
         - COMPUTER 1 is put on Nod, the SAME side as this script's human, which the old
           lobby could not express at all;
         - and on team 1, which is the human's team, which is an alliance where the
           default is a free-for-all.
       Escape then shuts the drop down. If it ever stopped being modal that Escape would
       leave the lobby instead, and the run would end with rc=DMS_CANCEL and no settings
       block at all -- so this step is its own assertion. */
    { SK_ROW_ITEM(4), 500, 500, 0 },  /* open COMPUTER 4's drop down       */
    { SK_I_POP_BLOCK, 500, 500, 0 },  /* BLOCK it                          */
    { 0, 0, 0, SDLK_ESCAPE },
    { SK_ROW_ITEM(1), 500, 500, 0 },  /* open COMPUTER 1's drop down       */
    { SK_I_POP_NOD,  500, 500, 0 },   /* faction: the human's own side     */
    { SK_TEAM_ITEM(0), 500, 500, 0 }, /* team 1: allied with the human     */
    /* THE START PICKER: COMPUTER 1 takes start 4, a start it would never have been dealt
       by seat order, so the number that reaches the engine cannot be the default. */
    { SK_START_ITEM(3), 500, 500, 0 },
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
    /* THE START RULE, both halves: the start COMPUTER 1 holds is refused to PLAYER and
       must change nothing (the settings block shows seat 1 still on it), then PLAYER
       takes start 1, which nobody holds. Seat 2 picks none and stays -1: the engine deals
       it a start, which is the fourth item on the request. */
    { SK_START_ITEM(3), 500, 500, 0 },   /* refused: COMPUTER 1 holds it   */
    { SK_START_ITEM(0), 500, 500, 0 },   /* PLAYER takes start 1           */
    { 0, 0, 0, SDLK_ESCAPE },
    /* The side pair again, on the side that is already lit, so it changes nothing and
       redraws the one line that has to change: with a computer now set by hand, "every
       computer plays Nod" is no longer true and the screen has to say the real split. */
    { SK_I_NOD,      500, 500, 0 },
    { 0, 0, 0, SDLK_RETURN }          /* Enter is Play                     */
};
static DMS_LobbyProbe g_lobbyProbe;
/* --mpscreenstart's script, filled in once: START GAME, then MP_SCREEN_IDLE frames of a
   key the lobby does not translate, then ESCAPE. A room that starts leaves the screen long
   before the tail runs out; one that never starts is walked out of rather than held open
   for ever. The tail is sized to outlast the longest a START can legitimately take before
   the match is armed: the round trip wait, a far relay's acknowledgements, and margin. */
#define MP_SCREEN_IDLE 400
static DMS_LobbyStep g_mpScreenScript[MP_SCREEN_IDLE + 2];
static int  g_visualsFirst = 0;            /* --visualsfirst; see the parse below */
static int  g_wantClassic = 0;   /* --classic: the picture before the chain */

/* --flowtest N (storage above the campaign helpers): run the whole campaign flow
   hands-off -- auto-click Start New Game, autopilot the side select / score / map
   screens, bound every movie to a few frames, and synthesize a WIN after N mission
   ticks (clearly labelled). Ends the program after the SECOND mission boots and
   returns, which proves the full loop: menu -> side select -> briefing -> mission
   -> win -> movie -> score -> map -> next briefing -> next mission. */

/* The live heap left standing after each round's teardown, so a leak is ASSERTED on and
   not just printed. There is no "known staircase" to make room for any more: the three
   allocations that made one have been given their frees (the 640x480 HUD pack and the
   C&C95 cameo pack, neither of which sb_free released, and the smudge sheets, which had
   no free at all, 2.0 MB a cycle between them), and what is left does not trend upwards
   at all.

   The limit is on the DRIFT across a whole run rather than on one cycle, for the reason
   set out beside the assertion itself. */
#define RSS_MAX_ROUNDS 64
#define RSS_GROWTH_LIMIT_KIB 6000
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
#else
    /* THE SAME LOG ON A MAC, for the same reason: a game started from Finder (the .app,
       or the launcher it execs) has /dev/null for a standard output and every NET|,
       LOBBY| and GL line vanishes, which is how the first real-network test (5 Sep
       2026) arrived as two screenshots and no lines. Only a /dev/null-like standard
       output is redirected: a terminal keeps its lines, and a pipe or a file (the gate
       suite reads stdout through one) is left exactly alone. Same file as Windows,
       under the per-user folder the saves already use; the path is the first line. */
    {
        struct stat sb;
        const int r = fstat(fileno(stdout), &sb);
        if (r != 0 || (S_ISCHR(sb.st_mode) && !isatty(fileno(stdout)))) {
            char* pref = SDL_GetPrefPath("Slipgate Ironworks", "CNC3D");
            if (pref) {
                char logpath[1024];
                snprintf(logpath, sizeof logpath, "%scnc3d-log.txt", pref);
                SDL_free(pref);
                FILE* lf = freopen(logpath, "w", stderr);
                if (lf) {
                    setvbuf(stderr, NULL, _IONBF, 0);
                    fflush(stdout);
                    if (dup2(fileno(stderr), fileno(stdout)) >= 0)
                        setvbuf(stdout, NULL, _IONBF, 0);
                    fprintf(stderr, "C&C 3D %s -- log opened at %s\n", CNC3D_BUILD, logpath);
                }
            }
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
            /* THESE FIVE BELONG TO --flowtest AND A NEW ARM MUST NOT STEAL THEM. Read the
               same warning on --lobbyplay below: it is written there because somebody
               split that arm, and this one was split the same way by a
               `} else if (--matchshot)` inserted after the line above. Everything under
               it moved into the new arm, so --flowtest stopped setting camp_autopilot,
               the campaign flow sat at the side select waiting for a hand that never
               came, and G14 hung for its full five minute watchdog. Add an arm AFTER the
               closing brace of this one. */
            g_flowTest = atoi(argv[++i]);
            g_hiddenWin = 1;
            g_movieBound = 24;
            camp_autopilot = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        }
        /* --matchshot: draw the end-of-match debrief from a made-up table and photograph
           it. Hidden and on autopilot for the same reasons --flowtest is: nobody is
           watching and nobody is going to press anything. */
        else if (!strcmp(argv[i], "--matchshot")) {
            g_matchShot = 1;
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
            /* THESE FIVE BELONG TO --lobbyplay AND A NEW ARM MUST NOT STEAL THEM. On
               5 Sep 2026 a `} else if (--mpplay)` was inserted after the two lines above,
               which left the rest of this arm inside the NEW one: --lobbyplay stopped
               hiding its window, stopped skipping the movies, and -- the part that
               mattered -- stopped installing its probe script, so it drove nothing and sat
               in the lobby waiting for a hand that never came. G87 went red and stayed red
               through two commits. If you add an arm here, add it AFTER the closing brace
               of this one and read what you are splitting. */
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
            memset(&g_lobbyProbe, 0, sizeof g_lobbyProbe);
            g_lobbyProbe.script = LOBBY_SCRIPT;
            g_lobbyProbe.steps = (int)(sizeof LOBBY_SCRIPT / sizeof LOBBY_SCRIPT[0]);
        } else if ((!strcmp(argv[i], "--mphost") || !strcmp(argv[i], "--mpjoin"))
                   && i + 1 < argc) {
            const int host = !strcmp(argv[i], "--mphost");
            const int t = atoi(argv[++i]);
            if (host) g_mpHost = t < 1 ? 1 : t; else g_mpJoin = t < 1 ? 1 : t;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mprelay") && i + 1 < argc) {
            /* HOST or HOST:PORT, split here because every other address this program
               takes on the command line is written that way. */
            static char relaybuf[128];
            const char* a = argv[++i];
            const char* c = strrchr(a, ':');
            if (c && c[1]) {
                size_t n = (size_t)(c - a);
                if (n > sizeof relaybuf - 1) n = sizeof relaybuf - 1;
                memcpy(relaybuf, a, n);
                relaybuf[n] = '\0';
                g_mpRelayPort = (unsigned short)atoi(c + 1);
            } else {
                snprintf(relaybuf, sizeof relaybuf, "%s", a);
            }
            g_mpRelay = relaybuf;
        } else if (!strcmp(argv[i], "--mplist") && i + 1 < argc) {
            /* WHERE THE PUBLIC GAME LIST LIVES. There is a built-in address; this points
               the game at another one, which is how it is developed and gated against a
               stand-in with no service to stand up. A plain-HTTP address is refused
               unless it is this machine, because a direct row carries a host's own
               address and that goes where they were told and nowhere else. */
            if (!mb_set_url(argv[++i])) {
                fprintf(stderr, "MPLIST|FAIL|refused '%s': the game list must be https, "
                                "or on this machine\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(argv[i], "--mpfromlist")) {
            g_mpFromList = 1;
        } else if (!strcmp(argv[i], "--mpscreenstart")) {
            g_mpScreenStart = 1;
        } else if (!strcmp(argv[i], "--mppublic")) {
            g_mpPublic = 1;
        } else if (!strcmp(argv[i], "--mpclickhost")) {
            g_mpClickHost = 1;
        } else if (!strcmp(argv[i], "--mproom") && i + 1 < argc) {
            g_mpRoom = argv[++i];
        } else if (!strcmp(argv[i], "--mppass") && i + 1 < argc) {
            g_mpPass = argv[++i];
        } else if (!strcmp(argv[i], "--mpwait") && i + 1 < argc) {
            g_mpWaitMs = atoi(argv[++i]) * 1000;
            if (g_mpWaitMs < 30000) g_mpWaitMs = 30000;
        } else if (!strcmp(argv[i], "--mpname") && i + 1 < argc) {
            g_mpName = argv[++i];
        } else if (!strcmp(argv[i], "--mpplay") && i + 1 < argc) {
            g_mpPlay = atoi(argv[++i]);
            if (g_mpPlay < 1) g_mpPlay = 1;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
            memset(&g_lobbyProbe, 0, sizeof g_lobbyProbe);
            g_lobbyProbe.script = LOBBY_SCRIPT;
            g_lobbyProbe.steps = (int)(sizeof LOBBY_SCRIPT / sizeof LOBBY_SCRIPT[0]);
        }
        else if (!strcmp(argv[i], "--lobbyhost") && i + 1 < argc) {
            /* SECONDS on the command line, milliseconds in here, and a floor of one
               second so a typo cannot open a room that is shut before the joiner's
               first packet arrives and then blame the joiner for it. */
            g_lobbyHostMs = atoi(argv[++i]) * 1000;
            if (g_lobbyHostMs < 1000) g_lobbyHostMs = 1000;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--lobbyjoin") && i + 1 < argc) {
            g_lobbyJoinMs = atoi(argv[++i]) * 1000;
            if (g_lobbyJoinMs < 1000) g_lobbyJoinMs = 1000;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        }
        else if (!strcmp(argv[i], "--lobbyshot") && i + 1 < argc) {
            g_lobbyShot = argv[++i];
        }
        else if (!strcmp(argv[i], "--specopsshot") && i + 1 < argc) {
            g_specopsShot = argv[++i];
        }
        else if (!strcmp(argv[i], "--harnessscen") && i + 1 < argc) {
            g_harnessScen = argv[++i];
        }
        else if (!strcmp(argv[i], "--harnessload")) {
            g_harnessLoad = 1;
        } else if (!strcmp(argv[i], "--mpaddr")) {
            /* ITS OWN ARM, closed before the next `else if`. Twice now a new flag has been
               written INSIDE a neighbour's arm and quietly stolen its behaviour; the cost
               both times was a gate that hung for its whole watchdog. */
            g_mpAddrTest = 1;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mpshot") && i + 1 < argc) {
            g_mpShot = argv[++i];
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mpclick")) {
            /* ITS OWN ARM. See the note on --mpaddr above about what writing a new flag
               inside a neighbour's arm has cost twice. */
            g_mpClickTest = 1;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mpsort")) {
            /* Its own arm, for the same reason. */
            g_mpSortTest = 1;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mpsortclick")) {
            g_mpSortClick = 1;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mpprefs") && i + 1 < argc) {
            g_mpPrefsPath = argv[++i];
        } else if (!strcmp(argv[i], "--mpprefsread") && i + 1 < argc) {
            g_mpPrefsRead = argv[++i];
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mpping")) {
            g_mpPing = 1;
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        } else if (!strcmp(argv[i], "--mppingdirect") && i + 1 < argc) {
            g_mpPingDirectLog = argv[++i];
        } else if (!strcmp(argv[i], "--mppingrelayed") && i + 1 < argc) {
            g_mpPingRelayedLog = argv[++i];
        } else if (!strcmp(argv[i], "--mppingshot") && i + 1 < argc) {
            g_mpPingShot = argv[++i];
        }
        else if (!strcmp(argv[i], "--loadmenushot") && i + 1 < argc) {
            g_loadMenuShot = argv[++i];
            g_hiddenWin = 1;
            mcfg.logo = NULL;
            mcfg.intro = NULL;
        }
        else if (!strcmp(argv[i], "--bootfailshot") && i + 1 < argc) {
            g_bootFailShot = argv[++i];
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
       go through g_harnessTicks: that flag means "drive the menu through Special Ops
       into the Test Map and measure the handoff", which is a different route through
       this file. */
    else if ((g_mpHost > 0 || g_mpJoin > 0) && gargc < 250) {
        snprintf(tickbuf, sizeof tickbuf, "%d", g_mpHost > 0 ? g_mpHost : g_mpJoin);
        gargv[gargc++] = (char*)"--playticks";
        gargv[gargc++] = tickbuf;
    }
    else if (g_lobbyPlay > 0 && gargc < 250) {
        snprintf(tickbuf, sizeof tickbuf, "%d", g_lobbyPlay);
        gargv[gargc++] = (char*)"--playticks";
        gargv[gargc++] = tickbuf;
    }

    GameOpts opt;
    memset(&opt, 0, sizeof opt);
    if (game_parse_args(gargc, gargv, &opt))
        return 2;

    /* Before anything can host or join. See mp_install_map_hasher. */
    mp_install_map_hasher(base_dir(opt.dir));

    /* AND DO NOT BECOME THE ACTIVE APPLICATION. A hidden window is not enough on macOS:
       SDL gives the process the Regular activation policy, so it takes the menu bar and
       the keyboard focus the moment it starts. Must be set BEFORE SDL_Init. */
    if (shell_automated()) {
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    }
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

    /* Hidden for EVERY automated run, for the reason cnc_eyes hides its own: a window
       nobody is looking at should not steal focus from whoever is working. This used to
       name --harness alone, which left the campaign-flow, movie and lobby gates opening
       real windows. See shell_automated. */
    Uint32 flags = SDL_WINDOW_OPENGL;
    /* THE DISPLAY MODE THE PLAYER CHOSE, read off the preset before the window exists
      : windowed with a size, borderless at the desktop's size, or a true
       fullscreen mode. --fullscreen still decides when there is no preset yet. An
       automated run keeps its hidden window and takes no mode at all: a hidden window is
       never given the display's size, and a fullscreen one would measure the wrong thing. */
    int dispMode = FS_MODE_BORDERLESS, dispW = 0, dispH = 0;
    const int havePreset = game_display_wanted("cnc3d-fx.cfg", &dispMode, &dispW, &dispH);
    if (!havePreset && !fs_start_fullscreen) dispMode = FS_MODE_WINDOWED;
    /* A WINDOWED SIZE IS RESOLVED AND CLAMPED BEFORE THE WINDOW IS BORN, on display 0,
       which is where a centred window is born. A preset's 0x0 under WINDOWED is the
       Resolution list's Desktop entry and means the desktop's usable room; a saved size
       larger than that room (a mode the driver lists above the panel, or a size chosen
       on another display) is shrunk to it, and says so. Done in every run, hidden ones
       included, so the clamp can be read from an automated run; only the hidden window
       itself keeps its default size. */
    if (havePreset && dispMode == FS_MODE_WINDOWED) {
        if (dispW <= 0 || dispH <= 0)
            fs_usable_size(0, &dispW, &dispH);
        if (dispW > 0 && dispH > 0)
            fs_clamp_windowed(0, &dispW, &dispH);
    }
    if (shell_automated()) {
        flags |= SDL_WINDOW_HIDDEN;
    } else {
        flags |= fs_initial_flags(dispMode);
        if (dispMode == FS_MODE_WINDOWED && dispW > 0 && dispH > 0) { opt.w = dispW; opt.h = dispH; }
    }
    SDL_Window* win = SDL_CreateWindow("Command & Conquer 3D", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, opt.w, opt.h, flags);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    /* A true fullscreen window is born borderless-sized and then switched, so the mode
       change goes through the one function that knows how (fullscreen.h). */
    if (!shell_automated() && dispMode == FS_MODE_FULLSCREEN)
        fs_apply_mode(win, dispMode, dispW, dispH);
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
    const int automated = shell_automated();
    ab.silent = opt.nosound || (automated && !opt.audiowav);
    CncAudio* au = audio_boot(&ab);
    mcfg.au = au;
    game_set_audio(au);
    /* And the movie player, for Restate's Video button. */
    g_lentPlayer.win = win;
    g_lentPlayer.au = au;
    game_set_movie_player(shell_play_movie, NULL);

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
    if (g_loadMenuShot) {
        const int sl = game_load_menu_open(win, opt.dospack, g_loadMenuShot);
        printf("LOADMENUSHOT|file=%s|slot=%d\n", g_loadMenuShot, sl);
        fflush(stdout);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }
    if (g_bootFailShot) {
        /* A REAL boot attempt through the real refusal path, so the box carries the
           sentence a player would read and not a stand-in. A start that succeeds is
           reported as refused=0 and shut down again; the box is drawn only for a
           refusal, since that is the only time a player sees it. */
        const int booted = game_boot(win, &opt);
        if (booted) {
            game_shutdown();
        } else {
            game_notice_open(win, opt.dospack, "Unable to start mission",
                             game_boot_refusal(), g_bootFailShot);
        }
        printf("BOOTFAILSHOT|file=%s|refused=%d|%s\n", g_bootFailShot, booted ? 0 : 1,
               game_boot_refusal());
        fflush(stdout);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
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
    if (g_mpAddrTest) {
        MP_State mp;
        int fails = 0, checks = 0;
        char host[MP_ADDR_MAX];
        unsigned short port;
        int i;
        /* Every case is one line: what was asked, what came back, and whether that is the
           answer. A gate greps for the failures rather than counting the passes, so a case
           that stops running is a case that stops being asserted -- which is why the total
           is printed too. */
        #define MPA_OK(cond, what) do { checks++; \
            if (!(cond)) { fails++; printf("MPADDR|FAIL|%s\n", (what)); } \
            else printf("MPADDR|ok|%s\n", (what)); } while (0)

        /* ---- the grammar ---- */
        struct { const char* in; int ok; const char* host; unsigned port; } P[] = {
            { "192.168.1.9",        1, "192.168.1.9", 17421 },
            { "192.168.1.9:17435",  1, "192.168.1.9", 17435 },
            { "host.example.com",   1, "host.example.com", 17421 },
            { "a.b.c:1",            1, "a.b.c", 1 },
            { "a.b.c:65535",        1, "a.b.c", 65535 },
            { "  spaced.host  ",    1, "spaced.host", 17421 },
            { "",                   0, "", 0 },     /* nothing typed          */
            { "host:",              0, "", 0 },     /* half-typed port        */
            { "host:0",             0, "", 0 },     /* not a port             */
            { "host:65536",         0, "", 0 },     /* past the top           */
            { "host:12x",           0, "", 0 },     /* trailing rubbish       */
            { ":17421",             0, "", 0 },     /* no host                */
            { "::1",                0, "", 0 },     /* IPv6: refused by name  */
        };
        for (i = 0; i < (int)(sizeof P / sizeof P[0]); i++) {
            char label[160];
            int got;
            port = NM_PORT_DEFAULT;
            host[0] = '\0';
            got = dms_parse_hostport(P[i].in, host, sizeof host, &port);
            snprintf(label, sizeof label, "parse '%s' -> %s host='%s' port=%u",
                     P[i].in, got ? "yes" : "no", host, (unsigned)port);
            MPA_OK(got == P[i].ok
                   && (!got || (!strcmp(host, P[i].host) && port == P[i].port)), label);
        }

        /* ---- the two grammars, and that they never meet ---- */
        {
            struct { const char* in; int kind; } T[] = {
                { "192.168.1.9",       DMS_ADDR_HOST },
                { "host.example.com",  DMS_ADDR_HOST },
                { "a.b.c:17435",       DMS_ADDR_HOST },
                { "#K7M-3QX",          DMS_ADDR_ROOM },
                { "#k7m3qx",           DMS_ADDR_ROOM },
                { "  #K7M-3QX",        DMS_ADDR_ROOM },
                { "K7M3QX",            DMS_ADDR_HOST },  /* no sigil: a hostname, and it
                                                            must NEVER reach rc_decode */
                { "#K7M-3Q",           DMS_ADDR_NONE },
                { "#",                 DMS_ADDR_NONE },
                { "",                  DMS_ADDR_NONE },
                { "host:0",            DMS_ADDR_NONE }
            };
            for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
                char label[160];
                unsigned long room = 0ul;
                int got;
                port = NM_PORT_DEFAULT;
                host[0] = '\0';
                got = dms_parse_target(T[i].in, host, sizeof host, &port, &room);
                snprintf(label, sizeof label, "'%s' is %s", T[i].in,
                         got == DMS_ADDR_ROOM ? "a room code"
                       : got == DMS_ADDR_HOST ? "an address" : "neither");
                MPA_OK(got == T[i].kind, label);
            }
            /* The sigil is the whole distinction, so prove the two never cross: a code
               yields a room id and no host, an address yields a host and no room id. */
            {
                unsigned long room = 0ul;
                host[0] = '\0';
                MPA_OK(dms_parse_target("#K7M-3QX", host, sizeof host, &port, &room)
                       == DMS_ADDR_ROOM && room != 0ul && host[0] == '\0',
                       "a room code yields an id and never a hostname");
                room = 0ul;
                MPA_OK(dms_parse_target("K7M3QX", host, sizeof host, &port, &room)
                       == DMS_ADDR_HOST && room == 0ul && !strcmp(host, "K7M3QX"),
                       "and the same six letters without a sigil are a hostname");
            }
        }

        /* ---- the screen ---- */
        mp_init(&mp);
        mp.tab = MP_TAB_JOIN;
        mp.net = MP_NET_LAN;
        MPA_OK(!mp_item_visible(&mp, MP_I_ADDR),
               "the address field is not on the LAN sub-tab");
        mp.net = MP_NET_INTERNET;
        MPA_OK(mp_item_visible(&mp, MP_I_ADDR),
               "the address field IS on the INTERNET sub-tab");
        MPA_OK(mp_item_disabled(&mp, MP_I_JOIN),
               "JOIN is refused with no row and nothing typed");

        /* Type, correct a typo, and read it back: the backspace chain used to know about
           fewer fields than the typing chain did. */
        mp.focus = MP_I_ADDR;
        mp_text(&mp, '1'); mp_text(&mp, '2'); mp_text(&mp, '7'); mp_text(&mp, 'X');
        mp_key(&mp, MP_K_BACK);
        mp_text(&mp, '.'); mp_text(&mp, '0'); mp_text(&mp, '.'); mp_text(&mp, '0');
        mp_text(&mp, '.'); mp_text(&mp, '1');
        MPA_OK(!strcmp(mp.addr, "127.0.0.1"), "typing and backspace reach the address");

        MPA_OK(!mp_item_disabled(&mp, MP_I_JOIN),
               "JOIN goes live once an address is typed, with no row selected");
        MPA_OK(!mp_item_disabled(&mp, MP_I_PASS),
               "the passcode is offered on a typed join, which cannot show a padlock");

        /* An INTERNET game may be OPEN. PRIVATE GAME is left where the player put it: a
           game meant to be found by strangers is the ordinary case for a public list, and
           the passcode is for the other one. This briefly worked the other way -- the box
           forced PRIVATE on and held it there -- which is right only in a world where a
           room code is the sole way in, and wrong the moment games are listed. */
        {
            MP_State h;
            int x, y, w, h2;
            mp_init(&h);
            h.tab = MP_TAB_HOST;
            MPA_OK(h.relay_game == 0, "a new room is not relayed");
            if (mp_item_rect(&h, MP_I_RELAY, &x, &y, &w, &h2)) {
                mp_press(&h, x + 1, y + 1);
                MPA_OK(h.relay_game == 1, "ticking INTERNET GAME arms a relayed room");
                MPA_OK(h.private_game == 0,
                       "and does NOT force PRIVATE on: a public game may be open");
                MPA_OK(!mp_item_disabled(&h, MP_I_PRIVATE),
                       "and PRIVATE GAME stays the player's own choice");
            } else {
                MPA_OK(0, "the INTERNET GAME box has no rectangle");
            }
        }

        /* ---- LIST PUBLICLY, and that an address is never published by inheritance ---- */
        {
            MP_State h;
            int x, y, w, h2;
            mp_init(&h);
            h.tab = MP_TAB_HOST;
            MPA_OK(h.list_game == 0, "a new room is not on the public list");
            MPA_OK(mp_item_visible(&h, MP_I_PUBLIC),
                   "LIST PUBLICLY is offered on a room that is not relayed");
            if (mp_item_rect(&h, MP_I_PUBLIC, &x, &y, &w, &h2)) {
                mp_press(&h, x + 1, y + 1);
                MPA_OK(h.list_game == 1, "and ticking it asks for the room to be listed");
            } else {
                MPA_OK(0, "the LIST PUBLICLY box has no rectangle");
            }
            /* THE ROW UNDER THE ROWS: the box sits below the relay box and must not
               overlap it, which is the one thing arithmetic answers exactly. */
            {
                int rx, ry, rw, rh, px, py, pw, ph;
                if (mp_item_rect(&h, MP_I_RELAY, &rx, &ry, &rw, &rh)
                    && mp_item_rect(&h, MP_I_PUBLIC, &px, &py, &pw, &ph)) {
                    MPA_OK(py >= ry + rh,
                           "and it begins below the INTERNET GAME box rather than on it");
                } else {
                    MPA_OK(0, "one of the two checkboxes has no rectangle");
                }
            }
            /* Ticking the relay hides this box, so a tick left in it would be a decision
               about a different kind of room that the player can no longer see or undo. */
            if (mp_item_rect(&h, MP_I_RELAY, &x, &y, &w, &h2)) {
                mp_press(&h, x + 1, y + 1);
                MPA_OK(h.relay_game == 1 && h.list_game == 0,
                       "ticking INTERNET GAME drops a tick left in LIST PUBLICLY");
                MPA_OK(!mp_item_visible(&h, MP_I_PUBLIC),
                       "and takes the box off the screen, because a relayed room is "
                       "listed as a code anyway");
            } else {
                MPA_OK(0, "the INTERNET GAME box has no rectangle on the second pass");
            }
        }

        /* The handle prompt is the field the backspace chain used to miss entirely. */
        mp.focus = MP_I_HANDLE;
        mp_text(&mp, 'A'); mp_text(&mp, 'B'); mp_key(&mp, MP_K_BACK);
        MPA_OK(!strcmp(mp.handle, "A"), "backspace works in the YOUR NAME prompt");

        /* Tab walks the join tab's two fields; it used to give up on this tab. */
        mp.focus = MP_I_ADDR;
        mp_key(&mp, MP_K_TAB);
        MPA_OK(mp.focus == MP_I_PASS, "TAB walks from the address to the passcode");
        mp_key(&mp, MP_K_TAB);
        MPA_OK(mp.focus == MP_I_ADDR, "and back again");

        /* Switching sub-tabs must drop the stale row, or JOIN acts on a game that is no
           longer drawn. */
        {
            MP_State s2;
            int x, y, w, h;
            mp_init(&s2);
            s2.tab = MP_TAB_JOIN;
            s2.net = MP_NET_LAN;
            s2.rowcount = 1; s2.rowsel = 0;
            s2.rows[0].joinable = 1;
            snprintf(s2.rows[0].key, sizeof s2.rows[0].key, "%s", "192.168.1.9:17421");
            snprintf(s2.selkey, sizeof s2.selkey, "%s", s2.rows[0].key);
            MPA_OK(!mp_item_disabled(&s2, MP_I_JOIN), "a LAN row makes JOIN live");
            if (mp_item_rect(&s2, MP_I_NET_NET, &x, &y, &w, &h)) {
                MPA_OK(mp_press(&s2, x + 1, y + 1) == MP_ACT_REFRESH,
                       "choosing INTERNET asks the shell to empty the list");
                MPA_OK(s2.rowsel == -1, "and drops the selected row itself");
                /* THE KEY GOES TOO. The list holds its selection by key, so a key left
                   standing would select the same game again on the first refill of the
                   other list, if a game there answered to it. */
                MPA_OK(s2.selkey[0] == '\0', "and the key the selection was held by");
            } else {
                MPA_OK(0, "the INTERNET sub-tab has no rectangle");
            }
        }
        /* THE SIGIL HAS TO HAVE INK. A room code is written "#K7M-3QX" and the '#'
           carries the whole distinction between a code and a hostname -- but no string
           this menu draws has ever contained one, so that glyph in GRAD6FNT is unproven.
           db_print draws a blank for a cell that is not there and says nothing about it,
           so a missing glyph would ship as a code the host reads out with its first
           character invisible. Width alone is not enough: an empty cell can still carry
           an advance. */
        {
            const DB_Font* f6 = db_font(menu.pack, "GRAD6FNT");
            if (!f6) f6 = db_font(menu.pack, "6POINT");
            MPA_OK(f6 != NULL, "the menu font loads");
            if (f6) {
                const int wHash = db_string_width(f6, "#", DB_FONT6_XSPACING);
                const int wRef  = db_string_width(f6, "0", DB_FONT6_XSPACING);
                MPA_OK(wHash > 0, "the room code's '#' has a width");
                MPA_OK(wHash >= wRef - 3 && wHash <= wRef + 3,
                       "and it is about as wide as a digit, so it is a glyph and not a gap");
            }
        }
        printf("MPADDR|done|checks=%d|fails=%d\n", checks, fails);
        fflush(stdout);
        #undef MPA_OK
        dms_close(&menu);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fails ? 1 : 0;
    }

    /* --mpsort: THE GAME LIST'S ORDER, IDENTITY, FILTERS AND SCROLL, asked of the screen's
       own functions with no network and no second process.

       WHY A CALL AND NOT A PICTURE. The list is sorted, and refilled from the network ten
       times a second, and both of those used to hold the selection by POSITION: a refill
       that brought one more game above the selected one moved the highlight onto a
       different game, and JOIN went with it. Whether the same game is still selected is a
       question about identity, which a screenshot cannot see and a key comparison answers
       exactly. The rows below are built to make a weak order show itself: two names that
       differ only in case, one name on two keys, rows tied on players, a padlocked row
       from another build, and round trips with some not known. */
    if (g_mpSortTest) {
        int fails = 0, checks = 0;
        int i, j;
        #define MPS_OK(cond, what) do { checks++; \
            if (!(cond)) { fails++; printf("MPSORT|FAIL|%s\n", (what)); } \
            else printf("MPSORT|ok|%s\n", (what)); } while (0)

        struct SortSeed { const char* key; const char* name; const char* map;
                          int now, max, locked, why, relay, ping; };
        static const SortSeed SEED[14] = {
            { "k01", "ALPHA",   "GREEN ACRES",       2, 4, 0, MP_WHY_OK,    1,  40 },
            { "k02", "alpha",   "BLUE LAKES",        3, 4, 0, MP_WHY_OK,    0,  -1 },
            { "k03", "BRAVO",   "MOOSEHEAD BARRENS", 2, 4, 1, MP_WHY_OK,    1,  12 },
            { "k04", "BRAVO",   "RIVER RAID",        5, 8, 0, MP_WHY_OK,    0,  90 },
            { "k05", "CHARLIE", "GREEN ACRES",       2, 4, 0, MP_WHY_OK,    1,  -1 },
            { "k06", "DELTA",   "ALPINE",            1, 2, 0, MP_WHY_OK,    0,  40 },
            { "k07", "ECHO",    "ZEBRA",             7, 8, 0, MP_WHY_OK,    1, 300 },
            { "k08", "foxtrot", "canyon",            4, 6, 1, MP_WHY_OK,    0,  -1 },
            { "k09", "GOLF",    "DESERT",            2, 8, 0, MP_WHY_BUILD, 1,  -1 },
            { "k10", "HOTEL",   "ALPINE",            6, 8, 1, MP_WHY_BUILD, 0,  25 },
            { "k11", "INDIA",   "BAYOU",             4, 4, 0, MP_WHY_FULL,  1,  -1 },
            { "k12", "JULIET",  "GREEN ACRES",       3, 6, 0, MP_WHY_OK,    0,  55 },
            { "k13", "KILO",    "HILLS",             1, 4, 0, MP_WHY_OK,    1,  -1 },
            { "k14", "LIMA",    "MESA",              3, 8, 0, MP_WHY_OK,    0,   5 },
        };
        static const char* const COLNAME[4] = { "GAME", "MAP", "PING", "PLAYERS" };
        static MP_State S;
        int perms[6][14];

        auto ci = [](const char* a, const char* b) -> int {
            for (;; a++, b++) {
                int ca = (unsigned char)*a, cb = (unsigned char)*b;
                if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
                if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
                if (ca != cb) return ca < cb ? -1 : 1;
                if (!ca) return 0;
            }
        };
        auto fill = [](MP_Row* r, const SortSeed& s) {
            memset(r, 0, sizeof *r);
            snprintf(r->key, sizeof r->key, "%s", s.key);
            snprintf(r->name, sizeof r->name, "%s", s.name);
            snprintf(r->map, sizeof r->map, "%s", s.map);
            r->players_now = s.now;
            r->players_max = s.max;
            r->locked = s.locked;
            r->why = s.why;
            r->joinable = (s.why == MP_WHY_OK);
            r->relay = s.relay;
            if (s.ping >= 0) { r->ping_state = MP_PING_MS; r->ping_ms = s.ping; }
        };
        /* The rows in a given arrival order, written at the front the way a refill writes
           them. It starts from mp_init, so it also forgets any selection. */
        auto load = [&](const int* order, int n) {
            mp_init(&S);
            S.tab = MP_TAB_JOIN;
            S.net = MP_NET_INTERNET;
            for (int k = 0; k < n; k++) fill(&S.rows[k], SEED[order[k]]);
            S.rowcount = n;
            S.rowhidden = 0;
        };
        auto keyseq = [&]() -> std::string {
            std::string out;
            for (int k = 0; k < S.rowcount; k++) { out += S.rows[k].key; out += ' '; }
            return out;
        };
        auto pos = [&](const char* key) -> int {
            for (int k = 0; k < S.rowcount; k++)
                if (!strcmp(S.rows[k].key, key)) return k;
            return -1;
        };
        /* The pixel a hand would press to reach a row, found through mp_row_at. */
        auto rowPoint = [&](int row, int* px, int* py) -> bool {
            for (int yy = 0; yy < DM_SCREEN_H; yy++)
                for (int xx = 0; xx < DM_SCREEN_W; xx++)
                    if (mp_row_at(&S, xx, yy) == row) { *px = xx; *py = yy; return true; }
            return false;
        };
        auto centre = [&](int item, int* px, int* py) -> bool {
            int x, y, w, h;
            if (!mp_item_rect(&S, item, &x, &y, &w, &h)) return false;
            *px = x + w / 2;
            *py = y + h / 2;
            return true;
        };
        auto visible = [&]() -> bool {
            return S.rowsel >= S.rowtop && S.rowsel < S.rowtop + MP_LIST_ROWS_VISIBLE;
        };

        /* SIX ARRIVAL ORDERS: as listed, reversed, rotated by one and by five, the list
           service's own order (relayed rows first, then name as Python compares it), and a
           fixed shuffle. */
        {
            static const int SHUF[14] = { 7, 2, 12, 0, 9, 4, 13, 1, 10, 5, 3, 11, 8, 6 };
            std::vector<int> svc(14);
            for (i = 0; i < 14; i++) {
                perms[0][i] = i;
                perms[1][i] = 13 - i;
                perms[2][i] = (i + 1) % 14;
                perms[3][i] = (i + 5) % 14;
                perms[5][i] = SHUF[i];
                svc[i] = i;
            }
            std::stable_sort(svc.begin(), svc.end(), [](int a, int b) {
                if (SEED[a].relay != SEED[b].relay) return SEED[a].relay > SEED[b].relay;
                return strcmp(SEED[a].name, SEED[b].name) < 0;
            });
            for (i = 0; i < 14; i++) perms[4][i] = svc[i];
        }

        /* ---- 1 and 2: one order whatever order the rows arrived in, joinable first ---- */
        for (int col = 0; col < 4; col++) {
            for (int desc = 0; desc < 2; desc++) {
                std::string ref;
                bool same = true, grouped = true;
                char label[200];
                for (int p = 0; p < 6; p++) {
                    bool seenGrey = false;
                    load(perms[p], 14);
                    S.sortcol = col;
                    S.sortdesc = desc;
                    mp_view(&S, MP_VIEW_REFILL);
                    const std::string got = keyseq();
                    if (p == 0) ref = got;
                    else if (got != ref) {
                        same = false;
                        printf("MPSORT|why|%s %s|arrival %d gave %s|arrival 0 gave %s\n",
                               COLNAME[col], desc ? "down" : "up", p, got.c_str(), ref.c_str());
                    }
                    for (j = 0; j < S.rowcount; j++) {
                        if (!S.rows[j].joinable) seenGrey = true;
                        else if (seenGrey) grouped = false;
                    }
                }
                snprintf(label, sizeof label, "%s %s: six arrival orders give one order",
                         COLNAME[col], desc ? "descending" : "ascending");
                MPS_OK(same, label);
                snprintf(label, sizeof label, "%s %s: every joinable row comes before every "
                         "row that cannot be joined", COLNAME[col], desc ? "descending" : "ascending");
                MPS_OK(grouped, label);
            }
        }

        /* ---- 3: reversing flips only the sort column ---- */
        {
            int a1, a3, a5;
            load(perms[0], 14);
            S.sortcol = MP_SORT_PLAYERS;
            S.sortdesc = 0;
            mp_view(&S, MP_VIEW_REFILL);
            a1 = pos("k01"); a3 = pos("k03"); a5 = pos("k05");
            S.sortdesc = 1;
            mp_view(&S, MP_VIEW_PLAYER);
            MPS_OK(a1 < a3 && a3 < a5 && pos("k01") < pos("k03") && pos("k03") < pos("k05"),
                   "rows tied on PLAYERS stay name A to Z in both directions");
            load(perms[1], 14);
            S.sortcol = MP_SORT_GAME;
            S.sortdesc = 1;
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(pos("k03") < pos("k04") && pos("k01") < pos("k02"),
                   "one name on two keys, and two names differing only in case, keep key "
                   "order while the names run Z to A");
        }

        /* ---- the PING column: unknown last both ways, and what each cell says ---- */
        {
            bool okDir[2] = { true, true };
            for (int desc = 0; desc < 2; desc++) {
                int prev = -1, group = 1;
                bool unknownSeen = false;
                load(perms[5], 14);
                S.sortcol = MP_SORT_PING;
                S.sortdesc = desc;
                mp_view(&S, MP_VIEW_REFILL);
                for (j = 0; j < S.rowcount; j++) {
                    const int ms = mp_row_ping_ms(&S.rows[j]);
                    if (S.rows[j].joinable != group) {
                        group = S.rows[j].joinable;
                        unknownSeen = false;
                        prev = -1;
                    }
                    if (ms < 0) { unknownSeen = true; continue; }
                    if (unknownSeen) okDir[desc] = false;
                    if (prev >= 0 && (desc ? ms > prev : ms < prev)) okDir[desc] = false;
                    prev = ms;
                }
            }
            MPS_OK(okDir[0], "PING fastest first puts every unmeasured row after every measured one");
            MPS_OK(okDir[1], "and PING slowest first still puts the unmeasured ones last");
        }
        {
            MP_Row r;
            char t[32];
            memset(&r, 0, sizeof r);
            r.joinable = 1;
            mp_row_ping_text(&r, t, sizeof t);
            MPS_OK(t[0] == '\0' && mp_row_ping_ms(&r) == -1,
                   "a row nothing has measured has a blank PING cell and no number to sort by");
            r.ping_state = MP_PING_PROBING;
            mp_row_ping_text(&r, t, sizeof t);
            MPS_OK(!strcmp(t, "...") && mp_row_ping_ms(&r) == -1, "one being measured reads ...");
            r.ping_state = MP_PING_MS; r.ping_ms = 12;
            mp_row_ping_text(&r, t, sizeof t);
            MPS_OK(!strcmp(t, "12 MS") && mp_row_ping_ms(&r) == 12, "a measured one reads 12 MS");
            r.ping_ms = 5000;
            mp_row_ping_text(&r, t, sizeof t);
            MPS_OK(!strcmp(t, "999 MS"), "and a round trip past the cell's three digits reads 999 MS");
            r.ping_state = MP_PING_LOST;
            mp_row_ping_text(&r, t, sizeof t);
            MPS_OK(!strcmp(t, "--") && mp_row_ping_ms(&r) == -1, "one nothing answered reads --");
            /* A LAN ROW'S CELL IS EMPTY, and it is asked with a measured round trip in its
               state fields, so a cell that printed or sorted by that number would show it. */
            r.lan = 1; r.ping_state = MP_PING_MS; r.ping_ms = 12;
            mp_row_ping_text(&r, t, sizeof t);
            MPS_OK(t[0] == '\0' && mp_row_ping_ms(&r) == -1,
                   "a LAN row has an empty PING cell and no number to sort by, even carrying one");
            r.lan = 0; r.ping_state = MP_PING_MS; r.ping_ms = 12; r.joinable = 0;
            mp_row_ping_text(&r, t, sizeof t);
            MPS_OK(t[0] == '\0', "and a row this build would be refused from is blank");
        }
        /* THE PING SORT ON THE LAN LIST IS ALL TIES, so it comes out in the order GAME A to Z
           gives. Every row carries a different measured round trip, in an order unrelated to
           the names, so a sort that still read those numbers would not match. */
        {
            auto lanLoad = [&](int col, int desc) -> std::string {
                load(perms[5], 14);
                S.net = MP_NET_LAN;
                for (int k = 0; k < S.rowcount; k++) {
                    S.rows[k].lan = 1;
                    S.rows[k].relay = 0;
                    S.rows[k].ping_state = MP_PING_MS;
                    S.rows[k].ping_ms = 10 + (k * 37) % 400;
                }
                S.sortcol = col;
                S.sortdesc = desc;
                mp_view(&S, MP_VIEW_REFILL);
                return keyseq();
            };
            const std::string byName = lanLoad(MP_SORT_GAME, 0);
            const std::string up = lanLoad(MP_SORT_PING, 0);
            const std::string down = lanLoad(MP_SORT_PING, 1);
            if (up != byName || down != byName)
                printf("MPSORT|why|LAN PING|up gave %s|down gave %s|GAME A to Z gave %s\n",
                       up.c_str(), down.c_str(), byName.c_str());
            MPS_OK(up == byName, "on the LAN list PING fastest first leaves every row tied, so the names decide");
            MPS_OK(down == byName, "and PING slowest first gives the same order");
        }

        /* ---- 4: header presses ---- */
        {
            int px, py;
            bool ok;
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(S.sortcol == MP_SORT_GAME && !S.sortdesc, "a fresh screen sorts by GAME, A to Z");
            if (centre(MP_I_COL_MAP, &px, &py)) {
                mp_press(&S, px, py); mp_release(&S);
                ok = (S.sortcol == MP_SORT_MAP && !S.sortdesc);
                for (j = 1; j < S.rowcount; j++)
                    if (S.rows[j].joinable == S.rows[j - 1].joinable
                        && ci(S.rows[j - 1].map, S.rows[j].map) > 0) ok = false;
                MPS_OK(ok, "pressing MAP sorts by map, A to Z");
                mp_press(&S, px, py); mp_release(&S);
                ok = (S.sortcol == MP_SORT_MAP && S.sortdesc);
                for (j = 1; j < S.rowcount; j++)
                    if (S.rows[j].joinable == S.rows[j - 1].joinable
                        && ci(S.rows[j - 1].map, S.rows[j].map) < 0) ok = false;
                MPS_OK(ok, "pressing it again runs Z to A");
            } else {
                MPS_OK(0, "MAP has a header cell");
            }
            if (centre(MP_I_COL_PLAYERS, &px, &py)) {
                mp_press(&S, px, py); mp_release(&S);
                ok = (S.sortcol == MP_SORT_PLAYERS && S.sortdesc);
                for (j = 1; j < S.rowcount; j++)
                    if (S.rows[j].joinable == S.rows[j - 1].joinable
                        && S.rows[j - 1].players_now < S.rows[j].players_now) ok = false;
                MPS_OK(ok, "pressing PLAYERS sorts most players first");
            } else {
                MPS_OK(0, "PLAYERS has a header cell");
            }
            if (centre(MP_I_COL_PING, &px, &py)) {
                mp_press(&S, px, py); mp_release(&S);
                MPS_OK(S.sortcol == MP_SORT_PING && !S.sortdesc, "pressing PING sorts fastest first");
            } else {
                MPS_OK(0, "PING has a header cell");
            }
            if (centre(MP_I_COL_GAME, &px, &py)) {
                mp_press(&S, px, py); mp_release(&S);
                MPS_OK(S.sortcol == MP_SORT_GAME && !S.sortdesc, "and pressing GAME goes back to A to Z");
            } else {
                MPS_OK(0, "GAME has a header cell");
            }
        }

        /* ---- 5 and 6: the selection follows its game, not its position ---- */
        {
            static MP_Row tmp[MP_MAX_ROWS];
            static const SortSeed NEWROW = { "k00", "AARDVARK", "ALPINE", 1, 4, 0, MP_WHY_OK, 0, -1 };
            char K[MP_KEY_MAX];
            int px = 0, py = 0, n = 0;
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            S.rowtop = 2;
            const bool pressed = rowPoint(5, &px, &py);
            if (pressed) mp_press(&S, px, py);
            snprintf(K, sizeof K, "%s", S.rows[5].key);
            MPS_OK(pressed && S.rowsel == 5 && !strcmp(S.selkey, K),
                   "a click on screen line 3 selects that row, by its key");
            for (j = 0; j < 14; j++) tmp[j] = S.rows[13 - j];
            memcpy(S.rows, tmp, sizeof(MP_Row) * 14);
            S.rowcount = 14;
            S.rowhidden = 0;
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(S.rowsel >= 0 && !strcmp(S.rows[S.rowsel].key, K) && S.rowsel - S.rowtop == 3,
                   "a refill in the opposite order keeps the same game selected, on the same line");
            /* THE CASE A POSITION GETS WRONG: a new game arrives that sorts above it. */
            fill(&tmp[0], NEWROW);
            for (j = 0; j < 14; j++) fill(&tmp[j + 1], SEED[perms[5][j]]);
            memcpy(S.rows, tmp, sizeof(MP_Row) * 15);
            S.rowcount = 15;
            S.rowhidden = 0;
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(S.rowsel >= 0 && !strcmp(S.rows[S.rowsel].key, K) && S.rowsel - S.rowtop == 3,
                   "a game arriving above it moves it down a row, and the highlight goes with "
                   "its game and keeps its screen line");
            for (j = 0; j < 14; j++)
                if (strcmp(SEED[j].key, K)) fill(&tmp[n++], SEED[j]);
            memcpy(S.rows, tmp, sizeof(MP_Row) * (size_t)n);
            S.rowcount = n;
            S.rowhidden = 0;
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(S.rowsel == -1 && !strcmp(S.selkey, K),
                   "a fetch without that game leaves nothing selected, and remembers which game it was");
            MPS_OK(mp_item_disabled(&S, MP_I_JOIN), "and JOIN is grey while it is gone");
            for (j = 0; j < 14; j++) fill(&S.rows[j], SEED[perms[3][j]]);
            S.rowcount = 14;
            S.rowhidden = 0;
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(S.rowsel >= 0 && !strcmp(S.rows[S.rowsel].key, K),
                   "and selects it again the moment it is back");
        }

        /* ---- 7: the HIDE boxes ---- */
        {
            int px, py;
            bool noLocked = true, heldLocked = true, allJoinable = true;
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            if (centre(MP_I_HIDE_LOCKED, &px, &py)) mp_press(&S, px, py);
            for (j = 0; j < S.rowcount; j++) if (S.rows[j].locked) noLocked = false;
            for (j = S.rowcount; j < S.rowcount + S.rowhidden; j++) if (!S.rows[j].locked) heldLocked = false;
            MPS_OK(S.hide_locked && S.rowcount == 11 && S.rowhidden == 3 && noLocked && heldLocked,
                   "HIDE LOCKED holds back exactly the three locked rows, and counts them");
            S.hide_locked = 0;
            S.hide_grey = 1;
            mp_view(&S, MP_VIEW_PLAYER);
            for (j = 0; j < S.rowcount; j++) if (!S.rows[j].joinable) allJoinable = false;
            MPS_OK(S.rowcount == 11 && S.rowhidden == 3 && allJoinable,
                   "HIDE GREYED leaves only rows that can be joined");
            S.hide_locked = 1;
            mp_view(&S, MP_VIEW_PLAYER);
            MPS_OK(S.rowcount == 9 && S.rowhidden == 5,
                   "both together count a locked row from another build once");
            S.hide_locked = 0;
            S.hide_grey = 0;
            mp_view(&S, MP_VIEW_PLAYER);
            MPS_OK(S.rowcount == 14 && S.rowhidden == 0, "and with both clear every row is back");
        }

        /* ---- 8: the thumb ---- */
        {
            int bx = 0, by = 0, bw = 0, bh = 0, ty = 0, th = 0;
            mp_init(&S);
            S.tab = MP_TAB_JOIN;
            MPS_OK(mp_item_rect(&S, MP_I_LIST_BAR, &bx, &by, &bw, &bh), "the list has a scroll bar");
            S.rowcount = 14; S.rowtop = 0;
            mp_bar_thumb(&S, &ty, &th);
            MPS_OK(ty == by && th == bh * 10 / 14,
                   "fourteen rows at the top: the thumb starts at the top and is ten fourteenths of the track");
            S.rowtop = 4;
            mp_bar_thumb(&S, &ty, &th);
            MPS_OK(ty + th == by + bh, "at the last page it ends exactly at the bottom of the track");
            S.rowcount = 32; S.rowtop = 22;
            mp_bar_thumb(&S, &ty, &th);
            MPS_OK(th == bh * 10 / 32 && ty + th == by + bh,
                   "thirty-two rows at the last page: a smaller thumb, still flush with the bottom");
            S.rowcount = 11; S.rowtop = 0;
            mp_bar_thumb(&S, &ty, &th);
            MPS_OK(ty == by && th == bh * 10 / 11,
                   "eleven rows, one more than fit: there is a thumb, ten elevenths of the track");
            S.rowcount = 10; S.rowtop = 0;
            mp_bar_thumb(&S, &ty, &th);
            MPS_OK(th == 0, "ten rows, every one in view: there is no thumb");
            S.rowcount = 0;
            mp_bar_thumb(&S, &ty, &th);
            MPS_OK(th == 0, "no rows at all: there is no thumb, and nothing divides by zero");
        }

        /* ---- 8b: with nothing to scroll, the track does nothing ----
           Every shown game in view, whether ten of them or none because both HIDE boxes
           emptied the list. A press on the top, the middle and the bottom of the track,
           each one dragged the length of it, must take hold of nothing, scroll nothing and
           move no selection. All three places are pressed because a thumb that filled the
           track would have been under every one of them. */
        {
            int bx = 0, by = 0, bw = 0, bh = 0, ty = 0, th = 0;
            for (int pass = 0; pass < 2; pass++) {
                const bool none = (pass == 1);
                const char* who = none ? "every game hidden, none shown" : "ten games shown, all in view";
                char K[MP_KEY_MAX];
                char what[240];
                load(perms[0], none ? 14 : 10);
                mp_view(&S, MP_VIEW_REFILL);
                mp_key(&S, MP_K_HOME); mp_key(&S, MP_K_DOWN); mp_key(&S, MP_K_DOWN);
                snprintf(K, sizeof K, "%s", S.selkey);
                if (none) {
                    for (j = 0; j < S.rowcount; j++) if (S.rows[j].joinable) S.rows[j].locked = 1;
                    S.hide_locked = 1;
                    S.hide_grey = 1;
                    mp_view(&S, MP_VIEW_PLAYER);
                }
                const int sel = S.rowsel, top = S.rowtop, count = S.rowcount;
                mp_item_rect(&S, MP_I_LIST_BAR, &bx, &by, &bw, &bh);
                mp_bar_thumb(&S, &ty, &th);
                snprintf(what, sizeof what, "%s: there is no thumb", who);
                MPS_OK(count == (none ? 0 : 10) && K[0] && th == 0, what);
                int held = 0, moved = 0;
                const int ys[3] = { by, by + bh / 2, by + bh - 1 };
                for (int q = 0; q < 3; q++) {
                    mp_press(&S, bx + 2, ys[q]);
                    held |= S.drag;
                    if (S.rowtop != top) moved = 1;
                    mp_motion(&S, bx + 2, by + bh - 1);
                    if (S.rowtop != top) moved = 1;
                    mp_motion(&S, bx + 2, by);
                    if (S.rowtop != top) moved = 1;
                    held |= S.drag;
                    mp_release(&S);
                }
                snprintf(what, sizeof what, "%s: a press on the top, middle and bottom of the track, "
                         "each dragged its length, takes hold of nothing, scrolls nothing and selects nothing", who);
                MPS_OK(!held && !moved && S.rowtop == top && S.rowsel == sel && !strcmp(S.selkey, K), what);
            }
            /* A THUMB HELD WHILE A REFILL SHRINKS THE LIST until every game fits is let go by
               the next move, rather than left marked as held over a track with no thumb. */
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            mp_item_rect(&S, MP_I_LIST_BAR, &bx, &by, &bw, &bh);
            mp_bar_thumb(&S, &ty, &th);
            mp_press(&S, bx + 2, ty + 1);
            const int heldBefore = S.drag;
            S.rowcount = 8;
            S.rowhidden = 0;
            mp_view(&S, MP_VIEW_REFILL);
            mp_motion(&S, bx + 2, by + bh - 1);
            MPS_OK(heldBefore && !S.drag && S.rowtop == 0,
                   "a thumb still held when a refill leaves eight games is let go by the next move");
            mp_release(&S);
        }

        /* ---- 9, 10, 11: the track, the drag and the wheel ---- */
        {
            int bx = 0, by = 0, bw = 0, bh = 0, ty = 0, th = 0, t1, t2, t3, t4;
            mp_init(&S);
            S.tab = MP_TAB_JOIN;
            S.rowcount = 32;
            mp_item_rect(&S, MP_I_LIST_BAR, &bx, &by, &bw, &bh);
            mp_press(&S, bx + 2, by + bh - 1); mp_release(&S); t1 = S.rowtop;
            mp_press(&S, bx + 2, by + bh - 1); mp_release(&S); t2 = S.rowtop;
            mp_press(&S, bx + 2, by + bh - 1); mp_release(&S); t3 = S.rowtop;
            mp_press(&S, bx + 2, by);          mp_release(&S); t4 = S.rowtop;
            MPS_OK(t1 == 10 && t2 == 20, "a press on the track below the thumb pages down ten rows");
            MPS_OK(t3 == 22, "and stops at the last page");
            MPS_OK(t4 == 12, "a press above the thumb pages back up ten");
            MPS_OK(S.rowsel == -1 && S.selkey[0] == '\0', "and none of it selected a row");

            S.rowtop = 0;
            mp_bar_thumb(&S, &ty, &th);
            mp_press(&S, bx + 2, ty + 1);
            const int held = S.drag;
            mp_motion(&S, bx + 2, by + bh - 1);
            const int atBottom = S.rowtop;
            mp_motion(&S, bx + 2, by);
            const int atTop = S.rowtop;
            mp_release(&S);
            mp_motion(&S, bx + 2, by + bh - 1);
            MPS_OK(held, "a press on the thumb takes hold of it");
            MPS_OK(atBottom == 22, "dragged to the bottom of the track, the list is at its last page");
            MPS_OK(atTop == 0, "dragged to the top, at its first");
            MPS_OK(S.rowtop == 0 && !S.drag, "and once it is let go, moving the pointer scrolls nothing");

            mp_scroll(&S, 100); t1 = S.rowtop;
            mp_scroll(&S, -100); t2 = S.rowtop;
            MPS_OK(t1 == 22 && t2 == 0, "the wheel runs to the last page and back to the first, and no further");
        }

        /* ---- 12: the keys ---- */
        {
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            mp_key(&S, MP_K_HOME);
            MPS_OK(S.rowsel == 0 && visible() && !strcmp(S.selkey, S.rows[0].key),
                   "HOME selects the first row, by key");
            snprintf(S.status, sizeof S.status, "%s", "THIS GAME IS LOCKED: TYPE THE HOST'S PASSCODE.");
            mp_key(&S, MP_K_END);
            MPS_OK(S.rowsel == 13 && S.rowtop == 4 && visible(), "END selects the last row and scrolls to show it");
            MPS_OK(S.status[0] == '\0', "and a key that moves the selection clears a refusal about the last one");
            mp_key(&S, MP_K_HOME);
            mp_key(&S, MP_K_PGDN);
            MPS_OK(S.rowsel == 10 && visible(), "PAGE DOWN moves ten rows and shows the row");
            mp_key(&S, MP_K_PGUP);
            MPS_OK(S.rowsel == 0 && visible(), "PAGE UP moves back ten");
        }

        /* ---- 13: only the rows band selects ---- */
        {
            int px = 0, py = 0, bx = 0, by = 0, bw = 0, bh = 0, hx = 0, hy = 0, hw = 0, hh = 0;
            char K[MP_KEY_MAX];
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            mp_key(&S, MP_K_HOME); mp_key(&S, MP_K_DOWN); mp_key(&S, MP_K_DOWN);
            snprintf(K, sizeof K, "%s", S.selkey);
            rowPoint(2, &px, &py);
            mp_item_rect(&S, MP_I_LIST_BAR, &bx, &by, &bw, &bh);
            mp_item_rect(&S, MP_I_COL_GAME, &hx, &hy, &hw, &hh);
            mp_press(&S, bx - 2, py); mp_release(&S);
            mp_press(&S, bx - 1, py); mp_release(&S);
            MPS_OK(S.rowsel == 2 && !strcmp(S.selkey, K),
                   "a press in the gap between the rows and the bar selects nothing");
            mp_press(&S, bx + 2, py); mp_release(&S);
            MPS_OK(S.rowsel == 2 && !strcmp(S.selkey, K), "a press on the bar, level with a row, selects nothing");
            MPS_OK(mp_row_at(&S, hx + 20, hy + 4) < 0 && mp_row_at(&S, bx + 2, hy + 4) < 0,
                   "the header row is not a row");
            mp_press(&S, bx + 2, hy + 4); mp_release(&S);
            MPS_OK(S.rowsel == 2 && !strcmp(S.selkey, K), "a press on the header above the bar selects nothing");
            mp_press(&S, hx + 20, hy + 4); mp_release(&S);
            MPS_OK(!strcmp(S.selkey, K) && S.rowsel >= 0 && !strcmp(S.rows[S.rowsel].key, K),
                   "a press on a header re-sorts, and the same game stays selected");
        }

        /* ---- 14: keys, LAN and internet ---- */
        {
            static MP_Row tmp[2];
            mp_init(&S);
            S.tab = MP_TAB_JOIN;
            S.net = MP_NET_LAN;
            for (j = 0; j < 2; j++) {
                MP_Row* r = &S.rows[j];
                memset(r, 0, sizeof *r);
                snprintf(r->name, sizeof r->name, "%s", "LAN GAME");
                snprintf(r->addr, sizeof r->addr, "192.168.1.%d", 5 + j);
                r->port = 17421;
                r->lan = 1;
                r->joinable = 1;
                mp_row_key(r, NULL);
            }
            S.rowcount = 2;
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(strcmp(S.rows[0].key, S.rows[1].key) != 0 && !strcmp(S.rows[1].key, "192.168.1.6:17421"),
                   "two LAN games with one name are two keys, by the address each answers on");
            mp_key(&S, MP_K_END);
            tmp[0] = S.rows[1];
            tmp[1] = S.rows[0];
            memcpy(S.rows, tmp, sizeof tmp);
            S.rowcount = 2;
            S.rowhidden = 0;
            mp_view(&S, MP_VIEW_REFILL);
            MPS_OK(S.rowsel >= 0 && !strcmp(S.rows[S.rowsel].addr, "192.168.1.6"),
                   "and the one selected stays selected when they arrive the other way round");
            {
                MP_Row r;
                memset(&r, 0, sizeof r);
                snprintf(r.room, sizeof r.room, "%s", "#850-RZ2");
                snprintf(r.addr, sizeof r.addr, "%s", "81.2.69.160");
                r.port = 17421;
                r.relay = 1;
                mp_row_key(&r, "0123456789abcdef0123456789abcdef");
                MPS_OK(!strcmp(r.key, "0123456789abcdef0123456789abcdef"),
                       "an internet row is keyed by the list's own id when it has one");
                mp_row_key(&r, NULL);
                MPS_OK(!strcmp(r.key, "#850-RZ2"), "a relayed row without one, by its room code");
                r.relay = 0;
                mp_row_key(&r, "");
                MPS_OK(!strcmp(r.key, "81.2.69.160:17421"), "a direct row without one, by address and port");
            }
        }

        /* ---- 15: the status line's sentences ---- */
        {
            char t[128], label[200];
            int px = 0, py = 0;
            auto say = [&](const char* want, const char* what) {
                mp_status_line(&S, t, sizeof t);
                snprintf(label, sizeof label, "status, %s: \"%s\"", what, t);
                MPS_OK(!strcmp(t, want), label);
            };
            mp_init(&S);
            S.tab = MP_TAB_JOIN;
            S.net = MP_NET_INTERNET;
            S.rowcount = 1;
            S.rowsel = 0;
            MP_Row* r = &S.rows[0];
            memset(r, 0, sizeof *r);
            r->players_now = 2; r->players_max = 4;
            r->why = MP_WHY_BUILD; r->joinable = 0;
            say("ANOTHER BUILD HOSTS THIS GAME.", "another build");
            r->why = MP_WHY_FULL; r->players_now = 4;
            say("FULL: 4 OF 4 PLAYERS.", "full");
            r->why = MP_WHY_OK; r->joinable = 1; r->players_now = 2; r->locked = 1;
            say("LOCKED: TYPE THE HOST'S 4 DIGIT PASSCODE.", "locked");
            r->locked = 0; r->relay = 1;
            snprintf(r->room, sizeof r->room, "%s", "#850-RZ2");
            say("RELAYED. ROOM #850-RZ2. NO PORTS NEEDED.", "relayed");
            r->relay = 0;
            snprintf(r->addr, sizeof r->addr, "%s", "255.255.255.255");
            r->port = 65535;
            say("DIRECT TO 255.255.255.255:65535.", "direct");
            r->lan = 1;
            snprintf(r->addr, sizeof r->addr, "%s", "192.168.100.200");
            say("ON THIS NETWORK, 192.168.100.200.", "LAN");
            S.rowsel = -1; S.rowhidden = 3;
            say("3 GAMES HIDDEN.", "three held back and nothing selected");
            S.rowhidden = 1;
            say("1 GAME HIDDEN.", "one held back");
            S.rowhidden = 0;
            say("", "nothing selected and nothing held back");
            S.focus = MP_I_ADDR;
            say("OR TYPE A CODE. #K7M-3QX NEEDS NO PORTS.", "the ROOM CODE field has the caret");
            S.focus = -1;
            snprintf(S.status, sizeof S.status, "%s", "THIS GAME IS LOCKED: TYPE THE HOST'S PASSCODE.");
            const bool got = rowPoint(0, &px, &py);
            if (got) mp_press(&S, px, py);
            MPS_OK(got && S.rowsel == 0 && S.status[0] == '\0',
                   "clicking a row clears a refusal that described the last one");
        }

        /* ---- 16: the YOUR NAME box is modal, and a text field keeps its own keys ----
           While the box is up the list behind it cannot be pressed or keyed: either would
           change which game the box's OK joins while the box hides the list. And with the
           caret in a field, HOME and END belong to the field. */
        {
            int px = 0, py = 0, jx = 0, jy = 0, ax = 0, ay = 0, ox = 0, oy = 0, act;
            char K[MP_KEY_MAX];
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            mp_key(&S, MP_K_HOME); mp_key(&S, MP_K_DOWN); mp_key(&S, MP_K_DOWN);
            snprintf(K, sizeof K, "%s", S.selkey);
            /* Found before the box opens: while it is up, mp_row_at answers no row at all. */
            const bool havePoint = rowPoint(6, &px, &py);
            const bool haveJoin = centre(MP_I_JOIN, &jx, &jy);
            act = haveJoin ? mp_press(&S, jx, jy) : -1;
            mp_release(&S);
            MPS_OK(act == MP_ACT_NONE && S.prompt == MP_ACT_JOIN && S.focus == MP_I_HANDLE,
                   "JOIN with no name opens the YOUR NAME box in front of the selected game");
            if (havePoint) { mp_press(&S, px, py); mp_release(&S); }
            MPS_OK(havePoint && S.rowsel == 2 && !strcmp(S.selkey, K) && S.prompt == MP_ACT_JOIN,
                   "a press on a row behind the box selects nothing");
            mp_key(&S, MP_K_END); mp_key(&S, MP_K_PGDN); mp_key(&S, MP_K_HOME);
            mp_key(&S, MP_K_PGUP); mp_key(&S, MP_K_UP); mp_key(&S, MP_K_DOWN);
            MPS_OK(S.rowsel == 2 && !strcmp(S.selkey, K) && S.rowtop == 0,
                   "and END, PAGE DOWN, HOME, PAGE UP, UP and DOWN behind the box move nothing");
            /* The press behind the box landed on nothing, which takes the caret out of the
               field as any such press does; the player clicks back into it. */
            {
                int fx = 0, fy = 0;
                if (centre(MP_I_HANDLE, &fx, &fy)) { mp_press(&S, fx, fy); mp_release(&S); }
            }
            mp_text(&S, 'Z');
            act = mp_key(&S, MP_K_ENTER);
            MPS_OK(act == MP_ACT_JOIN && S.prompt == 0 && !strcmp(S.handle, "Z") && S.rowsel == 2,
                   "Enter in the box is its OK: the name is taken and JOIN goes ahead, for the same game");

            /* THE GAME LEAVES THE LIST WHILE THE BOX IS UP. */
            S.handle[0] = '\0';
            mp_press(&S, jx, jy); mp_release(&S);
            const int reopened = S.prompt;
            {
                static MP_Row keep[MP_MAX_ROWS];
                int n = 0;
                for (j = 0; j < S.rowcount + S.rowhidden; j++)
                    if (strcmp(S.rows[j].key, K)) keep[n++] = S.rows[j];
                memcpy(S.rows, keep, sizeof(MP_Row) * (size_t)n);
                S.rowcount = n;
                S.rowhidden = 0;
                mp_view(&S, MP_VIEW_REFILL);
            }
            mp_text(&S, 'Y');
            const bool haveOk = centre(MP_I_HANDLE_OK, &ox, &oy);
            act = haveOk ? mp_press(&S, ox, oy) : -1;
            mp_release(&S);
            MPS_OK(reopened == MP_ACT_JOIN && act == MP_ACT_NONE && S.prompt == 0
                   && !strcmp(S.handle, "Y") && S.rowsel == -1,
                   "if that game leaves the list while the box is up, OK keeps the name and joins nothing");

            /* A TYPED CODE WAS JOIN'S DESTINATION, AND THE SELECTED GAME COMES BACK. */
            S.handle[0] = '\0';
            snprintf(S.addr, sizeof S.addr, "%s", "#K7M-3QX");
            mp_press(&S, jx, jy); mp_release(&S);
            const int typedPrompt = S.prompt;
            for (j = 0; j < 14; j++) fill(&S.rows[j], SEED[perms[1][j]]);
            S.rowcount = 14;
            S.rowhidden = 0;
            mp_view(&S, MP_VIEW_REFILL);
            mp_text(&S, 'X');
            act = mp_key(&S, MP_K_ENTER);
            MPS_OK(typedPrompt == MP_ACT_JOIN && S.rowsel >= 0 && act == MP_ACT_NONE && S.prompt == 0,
                   "and if a typed code was JOIN's destination and the selected game comes back under "
                   "the box, OK joins nothing either");

            /* ESC. */
            S.addr[0] = '\0';
            S.handle[0] = '\0';
            mp_press(&S, jx, jy); mp_release(&S);
            const int escOpened = S.prompt;
            mp_text(&S, 'Q');
            act = mp_key(&S, MP_K_ESC);
            MPS_OK(escOpened == MP_ACT_JOIN && act == MP_ACT_NONE && S.prompt == 0
                   && S.handle[0] == '\0' && S.focus == -1,
                   "ESC shuts the box and forgets the name nobody confirmed, without leaving the screen");
            MPS_OK(mp_key(&S, MP_K_ESC) == MP_ACT_CANCEL, "and a second ESC leaves");
            snprintf(S.handle, sizeof S.handle, "%s", "A");
            mp_prompt_close(&S);
            MPS_OK(!strcmp(S.handle, "A"), "shutting a box that is not open leaves the name alone");

            /* THE CARET IN ROOM CODE. */
            load(perms[0], 14);
            mp_view(&S, MP_VIEW_REFILL);
            const bool haveAddr = centre(MP_I_ADDR, &ax, &ay);
            if (haveAddr) { mp_press(&S, ax, ay); mp_release(&S); }
            mp_text(&S, '#');
            mp_key(&S, MP_K_HOME); mp_key(&S, MP_K_END); mp_key(&S, MP_K_PGDN);
            mp_key(&S, MP_K_PGUP); mp_key(&S, MP_K_DOWN); mp_key(&S, MP_K_UP);
            MPS_OK(haveAddr && S.focus == MP_I_ADDR && S.rowsel == -1 && S.selkey[0] == '\0'
                   && !strcmp(S.addr, "#"),
                   "with the caret in ROOM CODE, HOME, END and the page and arrow keys select no game");
            mp_key(&S, MP_K_ESC);
            mp_key(&S, MP_K_HOME);
            MPS_OK(S.focus == -1 && S.rowsel == 0, "ESC takes the caret out, and HOME then selects the first game");
        }

        /* ---- 17: a held thumb whose release never arrived scrolls nothing ---- */
        {
            int bx = 0, by = 0, bw = 0, bh = 0, ty = 0, th = 0, hx = 0, hy = 0;
            mp_init(&S);
            S.tab = MP_TAB_JOIN;
            S.rowcount = 32;
            mp_item_rect(&S, MP_I_LIST_BAR, &bx, &by, &bw, &bh);
            mp_bar_thumb(&S, &ty, &th);
            mp_press(&S, bx + 2, ty + 1);
            const int held = S.drag;
            const bool haveHost = centre(MP_I_TAB_HOST, &hx, &hy);
            if (haveHost) mp_press(&S, hx, hy);          /* no release in between */
            mp_motion(&S, bx + 2, by + bh - 1);
            MPS_OK(held && haveHost && S.tab == MP_TAB_HOST && !S.drag && S.rowtop == 0,
                   "a thumb still held when HOST GAME is pressed is let go, and moving the pointer scrolls nothing");
            S.tab = MP_TAB_JOIN;
            mp_press(&S, bx + 2, ty + 1);
            const int held2 = S.drag;
            S.tab = MP_TAB_HOST;                          /* its list went off screen, button down */
            mp_motion(&S, bx + 2, by + bh - 1);
            MPS_OK(held2 && !S.drag && S.rowtop == 0,
                   "one still marked held once its list is off screen is let go by the first move, not followed");
            S.tab = MP_TAB_JOIN;
            mp_press(&S, bx + 2, ty + 1);
            const int held3 = S.drag;
            S.prompt = MP_ACT_HOST;
            mp_motion(&S, bx + 2, by + bh - 1);
            MPS_OK(held3 && !S.drag && S.rowtop == 0, "nor does a thumb behind the YOUR NAME box follow the pointer");
            S.prompt = 0;
        }

        /* ---- 18: what a settings write holds while the YOUR NAME box is open ---- */
        {
            MP_Prefs saved, out;
            mp_init(&S);
            memset(&saved, 0, sizeof saved);
            snprintf(saved.handle, sizeof saved.handle, "%s", "ACE");
            S.sortcol = MP_SORT_PLAYERS;
            S.sortdesc = 1;
            S.hide_grey = 1;
            S.prompt = MP_ACT_JOIN;
            snprintf(S.handle, sizeof S.handle, "%s", "AC");
            mp_prefs_to_save(&S, &saved, &out);
            MPS_OK(out.sortcol == MP_SORT_PLAYERS && out.sortdesc == 1 && out.hide_grey == 1
                   && !strcmp(out.handle, "ACE"),
                   "with the YOUR NAME box open, a settings write takes the sort and the HIDE box but not the half typed name");
            S.prompt = 0;
            mp_prefs_to_save(&S, &saved, &out);
            MPS_OK(!strcmp(out.handle, "AC"), "and once the box is shut, the name as it stands");
        }

        printf("MPSORT|done|checks=%d|fails=%d\n", checks, fails);
        fflush(stdout);
        #undef MPS_OK
        dms_close(&menu);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fails ? 1 : 0;
    }

    /* --mpprefsread PATH: READ THE SETTINGS FILE THE WAY A PLAYER'S LAUNCH DOES, through the
       same call, into a screen straight out of mp_init, and say what came back. Nothing is
       written. A separate process from whatever wrote the file, which is what makes it a
       relaunch rather than a round trip through memory. */
    if (g_mpPrefsRead) {
        static MP_State mp;
        static const char* const WORD[4] = { "game", "map", "ping", "players" };
        MP_Prefs p;
        FILE* f = fopen(g_mpPrefsRead, "r");
        const int had = (f != NULL);
        if (f) fclose(f);
        mp_init(&mp);
        dms_mp_remember(g_mpPrefsRead, &mp);
        mp_prefs_get(&mp, &p);
        printf("MPPREFS|file=%d|sort=%s|order=%s|hide_locked=%d|hide_greyed=%d|handle=%s\n",
               had, WORD[(p.sortcol >= 0 && p.sortcol < 4) ? p.sortcol : 0],
               p.sortdesc ? "down" : "up", p.hide_locked, p.hide_grey, p.handle);
        fflush(stdout);
        dms_mp_remember(NULL, NULL);
        dms_close(&menu);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    /* --mpsortclick: THE SAME LIST, PRESSED, through the real dms_multiplayer loop against a
       game list on this machine that the caller has seeded with fourteen games. --mpsort
       proves the rules; this proves they are the rules the screen runs, with the network's
       own refill landing between the presses.

       WHAT THE CALLER SEEDS, and the harness checks rather than assumes: fourteen rows,
       three of them locked, two from another build (abi other than the one passed below)
       and one full, arranged so that fewest-players-first ends on a locked row from
       another build. The expected orders are worked out here from the list itself, by the
       rule written out a second time, so the screen is compared with an independent
       statement of it rather than with its own output.

       A SNAPSHOT STEP between presses hands the state back mid-visit, so each press is
       judged before the next one changes things. With --mpprefs, the visits also prove that
       a visit that changes nothing writes no settings file and one that does writes it. */
    if (g_mpSortClick) {
        static MP_State mp;
        static MP_State snap[16];
        static int took[16];
        /* The third visit's settings file, and what it held at each snapshot. */
        static std::string p2;
        static int p2Had[16];
        static MP_Prefs p2Seen[16];
        int fails = 0, checks = 0, r, i;
        /* NONZERO, so the build check is live: the seed's two rows from another build
           must be grey. */
        const unsigned abi = 0x5EED0001u;
        #define MPK_OK(cond, what) do { checks++; \
            if (!(cond)) { fails++; printf("MPSORTCLICK|FAIL|%s\n", (what)); } \
            else printf("MPSORTCLICK|ok|%s\n", (what)); } while (0)

        if (strncmp(mb_url(), "http://127.0.0.1", 16) != 0
            && strncmp(mb_url(), "http://localhost", 16) != 0) {
            fprintf(stderr, "MPSORTCLICK|FAIL|--mpsortclick needs --mplist pointing at a "
                            "game list on this machine, and was given %s\n", mb_url());
            dms_close(&menu);
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 1;
        }
        /* The seed's relayed rows are asked how far away they are through a relay; see
           --mpclick for why that relay is a closed port on this machine here. */
        nm_probe_set_relay("127.0.0.1", 9);

        MB_Row seed[MB_MAX_ROWS];
        int nseed;
        mb_forget();
        mb_refresh();
        for (i = 0; i < 300 && mb_phase() != MB_READY && mb_phase() != MB_FAILED; i++)
            SDL_Delay(50);
        nseed = mb_rows(seed, MB_MAX_ROWS);
        mb_forget();
        MPK_OK(nseed == 14, "the fourteen seeded games are on the list before the screen opens");

        struct Ref { std::string key, name, map; int now, max, joinable, locked; };
        std::vector<Ref> ref;
        int locked = 0, grey = 0, ids = 0;
        for (i = 0; i < nseed; i++) {
            Ref x;
            x.key = seed[i].id;
            x.name = seed[i].name;
            x.map = seed[i].map[0] ? seed[i].map : seed[i].scenario;
            x.now = seed[i].players_now;
            x.max = seed[i].players_max;
            x.joinable = !(seed[i].abi != 0 && seed[i].abi != abi) && x.now < x.max;
            x.locked = seed[i].locked;
            locked += x.locked;
            grey += !x.joinable;
            ids += (strlen(seed[i].id) == 32);
            ref.push_back(x);
        }
        MPK_OK(locked == 3 && grey == 3 && ids == nseed,
               "and the seed is the one this run needs: three locked, three grey, every row with an id");
        auto ci = [](const std::string& a, const std::string& b) -> int {
            size_t k;
            for (k = 0;; k++) {
                int ca = k < a.size() ? (unsigned char)a[k] : 0;
                int cb = k < b.size() ? (unsigned char)b[k] : 0;
                if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
                if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
                if (ca != cb) return ca < cb ? -1 : 1;
                if (!ca) return 0;
            }
        };
        auto expect = [&](int col, int desc) -> std::vector<std::string> {
            std::vector<Ref> v = ref;
            std::sort(v.begin(), v.end(), [&](const Ref& a, const Ref& b) {
                int d = 0;
                if (a.joinable != b.joinable) return a.joinable > b.joinable;
                if (col == MP_SORT_MAP) d = ci(a.map, b.map);
                else if (col == MP_SORT_PLAYERS)
                    d = (a.now != b.now) ? (a.now < b.now ? -1 : 1)
                      : (a.max != b.max) ? (a.max < b.max ? -1 : 1) : 0;
                else d = ci(a.name, b.name);
                if (desc) d = -d;
                if (d) return d < 0;
                d = ci(a.name, b.name);
                if (d) return d < 0;
                return a.key < b.key;
            });
            std::vector<std::string> out;
            for (size_t k = 0; k < v.size(); k++) out.push_back(v[k].key);
            return out;
        };
        auto order = [](const MP_State& s) -> std::vector<std::string> {
            std::vector<std::string> out;
            for (int k = 0; k < s.rowcount; k++) out.push_back(s.rows[k].key);
            return out;
        };
        auto onScreen = [](const MP_State& s) -> bool {
            return s.rowsel >= s.rowtop && s.rowsel < s.rowtop + MP_LIST_ROWS_VISIBLE;
        };

        memset(took, 0, sizeof took);
        dms_mp_snap([](const MP_State* st, int n, void*) {
            if (n >= 0 && n < 16) {
                snap[n] = *st;
                took[n] = 1;
                if (!p2.empty()) {
                    memset(&p2Seen[n], 0, sizeof p2Seen[n]);
                    p2Had[n] = mp_prefs_read(p2.c_str(), &p2Seen[n]);
                }
            }
        }, NULL);
        dms_mp_autoleave(40000, NULL, NULL);
        mp_init(&mp);
        if (g_mpPrefsPath) dms_mp_remember(g_mpPrefsPath, &mp);

        const std::vector<std::string> byGame = expect(MP_SORT_GAME, 0);

        /* ---- one: JOIN, INTERNET, and the list in its default order ---- */
        {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN,  500, 500, 0, 0,    0 },
                { MP_I_NET_NET,   500, 500, 0, 0,  200 },
                { DMS_MP_SNAP(0),   0,   0, 0, 0, 2500 },
                { 0, 0, 0, SDLK_ESCAPE, 0,         300 }
            };
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, abi, 0u);
            dms_mp_script(NULL, 0);
            MPK_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]), "every step of the first visit ran");
            MPK_OK(r == DMS_CANCEL, "and ESC left the screen");
            MPK_OK(took[0] && snap[0].rowcount == 14 && snap[0].rowhidden == 0,
                   "JOIN, INTERNET: the screen's own poll drew all fourteen games");
            MPK_OK(took[0] && order(snap[0]) == byGame,
                   "in the default order, GAME A to Z, as worked out here from the list itself");
            if (g_mpPrefsPath) {
                FILE* pf = fopen(g_mpPrefsPath, "r");
                MPK_OK(pf == NULL, "a visit that changed no setting wrote no settings file");
                if (pf) fclose(pf);
            }
        }

        /* ---- two: select, sort both ways, a refetch, END, the track, both HIDE boxes,
               and the player's name ---- */
        {
            static const DMS_MpStep S[] = {
                { DMS_MP_SNAP(1),     0,   0, 0, 0, 2500 },
                { DMS_MP_ROW(2),      0,   0, 0, 0,  100 },
                { DMS_MP_SNAP(2),     0,   0, 0, 0,  250 },
                { MP_I_COL_PLAYERS, 500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(3),     0,   0, 0, 0,  250 },
                { MP_I_COL_PLAYERS, 500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(4),     0,   0, 0, 0,  250 },
                /* LONGER THAN ONE FETCH INTERVAL, so a real refetch lands in the wait. */
                { DMS_MP_SNAP(5),     0,   0, 0, 0, 4500 },
                { 0, 0, 0, SDLK_END, 0,                100 },
                { DMS_MP_SNAP(6),     0,   0, 0, 0,  250 },
                { MP_I_LIST_BAR,    500,   0, 0, 0,  100 },
                { DMS_MP_SNAP(7),     0,   0, 0, 0,  250 },
                { MP_I_HIDE_LOCKED, 500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(8),     0,   0, 0, 0,  250 },
                { MP_I_HIDE_LOCKED, 500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(9),     0,   0, 0, 0,  250 },
                { MP_I_HIDE_GREY,   500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(10),    0,   0, 0, 0,  250 },
                /* THE NAME, through the first-run YOUR NAME box. HOST with no match set up
                   refuses in words and stays on the screen, which is what lets the name be
                   typed here without opening a room. */
                { MP_I_TAB_HOST,    500, 500, 0, 0,  100 },
                { MP_I_HOST,        500, 500, 0, 0,  200 },
                { 0, 0, 0, 0, 'A',                     150 },
                { 0, 0, 0, 0, 'C',                      60 },
                { 0, 0, 0, 0, 'E',                      60 },
                { MP_I_HANDLE_OK,   500, 500, 0, 0,  200 },
                { DMS_MP_SNAP(11),    0,   0, 0, 0,  250 },
                { 0, 0, 0, SDLK_ESCAPE, 0,             250 }
            };
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, abi, 0u);
            dms_mp_script(NULL, 0);
            MPK_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]), "every step of the second visit ran");
            MPK_OK(r == DMS_CANCEL, "and ESC left the screen");

            const std::string K = byGame.size() > 2 ? byGame[2] : std::string();
            MPK_OK(took[1] && snap[1].rowcount == 14, "back on the screen, the fourteen games are there again");
            MPK_OK(took[2] && snap[2].rowsel == 2 && K == snap[2].selkey,
                   "clicking row 2 selects that game, by the list's own id");
            MPK_OK(took[3] && order(snap[3]) == expect(MP_SORT_PLAYERS, 1),
                   "PLAYERS: most players first, joinable games before the grey ones");
            MPK_OK(took[3] && snap[3].rowsel >= 0 && K == snap[3].rows[snap[3].rowsel].key && onScreen(snap[3]),
                   "and the same game is still selected, and on screen");
            MPK_OK(took[4] && order(snap[4]) == expect(MP_SORT_PLAYERS, 0),
                   "PLAYERS again: fewest first");
            MPK_OK(took[4] && snap[4].rowsel >= 0 && K == snap[4].rows[snap[4].rowsel].key && onScreen(snap[4]),
                   "and still the same game, on screen");
            MPK_OK(took[5] && order(snap[5]) == order(snap[4]) && K == snap[5].selkey
                   && snap[5].rowsel >= 0 && K == snap[5].rows[snap[5].rowsel].key,
                   "four and a half seconds and a real refetch later, the order and the selection are unchanged");
            MPK_OK(took[6] && snap[6].rowsel == 13 && snap[6].rowtop == 4 && snap[6].rows[13].locked,
                   "END selects the last row, a locked one, and scrolls to show it");
            const std::string LK = took[6] ? snap[6].rows[13].key : "";
            MPK_OK(took[7] && snap[7].rowtop == 0 && snap[7].rowsel == 13,
                   "a press at the top of the scroll bar pages back to the top and leaves the selection alone");
            {
                bool noneLocked = true;
                for (i = 0; took[8] && i < snap[8].rowcount; i++) if (snap[8].rows[i].locked) noneLocked = false;
                MPK_OK(took[8] && snap[8].rowcount == 11 && snap[8].rowhidden == 3 && noneLocked,
                       "HIDE LOCKED: eleven games shown, three held back, none of them locked");
                MPK_OK(took[8] && snap[8].rowsel == -1 && mp_item_disabled(&snap[8], MP_I_JOIN),
                       "the selected game was one of the three, so nothing is selected and JOIN is grey");
            }
            MPK_OK(took[9] && snap[9].rowcount == 14 && snap[9].rowsel >= 0 && LK == snap[9].rows[snap[9].rowsel].key,
                   "HIDE LOCKED again: all fourteen back, and the locked game is selected again");
            {
                bool allJoinable = true;
                for (i = 0; took[10] && i < snap[10].rowcount; i++) if (!snap[10].rows[i].joinable) allJoinable = false;
                MPK_OK(took[10] && snap[10].rowcount == 11 && allJoinable,
                       "HIDE GREYED: only the eleven games this build can join");
            }
            MPK_OK(took[11] && !strcmp(snap[11].handle, "ACE") && snap[11].prompt == 0,
                   "the YOUR NAME box took a typed name and OK closed it");
            if (g_mpPrefsPath) {
                MP_Prefs p;
                memset(&p, 0, sizeof p);
                /* Seeded WRONG on purpose, so only the file can make every value right. */
                p.sortcol = MP_SORT_GAME; p.sortdesc = 1; p.hide_locked = 1; p.hide_grey = 0;
                MPK_OK(mp_prefs_read(g_mpPrefsPath, &p),
                       "leaving after changing the sort, a HIDE box and the name wrote the settings file");
                MPK_OK(p.sortcol == MP_SORT_PLAYERS && p.sortdesc == 0 && p.hide_locked == 0
                       && p.hide_grey == 1 && !strcmp(p.handle, "ACE"),
                       "and it holds PLAYERS fewest first, HIDE GREYED ticked, HIDE LOCKED clear and the name ACE");
            }
        }

        /* ---- three: the YOUR NAME box, shut by ESC, and left open when the screen goes ----
           A name typed into the box and never confirmed is not a name: ESC shuts the box
           and drops it, and so does leaving the screen with the box still up, which is the
           way out this visit takes, by the watchdog, so nothing is pressed to leave. The
           settings are remembered in a file of this visit's own, so the first file keeps
           what the relaunch check reads. */
        {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN,    500, 500, 0, 0,    0 },
                { MP_I_HIDE_LOCKED, 500, 500, 0, 0, 1500 },
                { MP_I_TAB_HOST,    500, 500, 0, 0,  150 },
                { MP_I_HOST,        500, 500, 0, 0,  150 },
                { 0, 0, 0, 0, 'Z',                     150 },
                { DMS_MP_SNAP(12),    0,   0, 0, 0,  250 },
                { 0, 0, 0, SDLK_ESCAPE, 0,             150 },
                { DMS_MP_SNAP(13),    0,   0, 0, 0,  250 },
                { MP_I_HOST,        500, 500, 0, 0,  150 },
                { 0, 0, 0, 0, 'Q',                     150 },
                { DMS_MP_SNAP(14),    0,   0, 0, 0,  250 }
            };
            if (g_mpPrefsPath) {
                p2 = std::string(g_mpPrefsPath) + ".prompt";
                remove(p2.c_str());
            }
            mp.handle[0] = '\0';
            if (!p2.empty()) dms_mp_remember(p2.c_str(), &mp);
            dms_mp_autoleave(6000, NULL, NULL);
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, abi, 0u);
            dms_mp_script(NULL, 0);
            MPK_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]), "every step of the third visit ran");
            MPK_OK(r == DMS_CANCEL, "and the watchdog took it off the screen with the box still open");
            MPK_OK(took[12] && snap[12].prompt == MP_ACT_HOST && !strcmp(snap[12].handle, "Z"),
                   "HOST with no name opened the YOUR NAME box, and a letter went into it");
            if (!p2.empty())
                MPK_OK(took[12] && p2Had[12] && p2Seen[12].hide_locked == 1 && p2Seen[12].handle[0] == '\0',
                       "the HIDE box ticked before it was written at once, and the letter in the open box was not");
            MPK_OK(took[13] && snap[13].prompt == 0 && snap[13].handle[0] == '\0' && snap[13].focus == -1,
                   "ESC shut the box and dropped the letter, and the screen stayed");
            MPK_OK(took[14] && snap[14].prompt == MP_ACT_HOST && !strcmp(snap[14].handle, "Q"),
                   "HOST again opened the box again, and another letter went in");
            MPK_OK(mp.prompt == 0 && mp.handle[0] == '\0',
                   "leaving the screen with the box open shut it, and kept no name nobody confirmed");
            if (!p2.empty()) {
                MP_Prefs p;
                memset(&p, 0, sizeof p);
                const int had = mp_prefs_read(p2.c_str(), &p);
                MPK_OK(had && p.hide_locked == 1 && p.hide_grey == 1 && p.sortcol == MP_SORT_PLAYERS
                       && p.sortdesc == 0 && p.handle[0] == '\0',
                       "and the settings file holds the sort and both HIDE boxes, and no name");
                remove(p2.c_str());
            }
        }

        dms_mp_snap(NULL, NULL);
        dms_mp_remember(NULL, NULL);
        printf("MPSORTCLICK|done|checks=%d|fails=%d\n", checks, fails);
        fflush(stdout);
        #undef MPK_OK
        dms_close(&menu);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fails ? 1 : 0;
    }

    /* --mpping: THE PING COLUMN, MEASURED, through the real dms_multiplayer loop.

       WHAT THE CALLER STARTS, and this checks rather than assumes: a game list and a relay
       stand-in on this machine (--mplist, --mprelay); a DIRECT headless room listed at its
       address as "A DIRECT HOST", whose log is --mppingdirect; a RELAYED headless room
       listed by its code as "B RELAYED ROOM", whose log is --mppingrelayed; and eleven
       seeded rows: nine relayed rooms nobody holds ("GONE 1" to "GONE 9"), a direct row on a
       port that only counts what reaches it ("C QUIET DIRECT") and a full relayed room
       ("D FULL ROOM").

       TWO VISITS. The first selects nothing for nine seconds, across two fetches of the
       list: the relayed room reads a number, the rooms nobody holds read "--", the full
       room and both direct rooms stay blank and are never handed to the prober, no more
       than NM_PROBE_RUNNING_MAX rooms are asked at once while the rest wait, and a PING
       sort puts the unknowns last both ways. Between the visits the direct room's own log
       must show no probe at all, and the relayed room's exactly one round. The second visit
       selects the direct room: its log shows nothing up to the moment of the click and one
       round after it, and its cell reads a number. Then REFRESH asks the relayed room again
       and forgets the direct one, which is no longer selected.

       What this cannot see, the caller counts: the registrations the relay saw, and every
       datagram that reached the quiet port. */
    if (g_mpPing) {
        static MP_State mp;
        static MP_State snap[24];
        static int took[24], running[24], queued[24];
        static int directPoll[24], quietPoll[24], fullPoll[24];
        static long directBytes = -1;
        static long directBytes3 = -1;          /* the direct room's log as the third visit began */
        static int directLinesBefore = -1;
        static std::string directKey, relayedRoom, quietKey, fullRoom;
        static std::vector<std::string> goneRooms;
        int fails = 0, checks = 0, r, i;
        #define MPP_OK(cond, what) do { checks++; \
            if (!(cond)) { fails++; printf("MPPING|FAIL|%s\n", (what)); } \
            else printf("MPPING|ok|%s\n", (what)); } while (0)

        const bool localRelay = g_mpRelay && !strcmp(g_mpRelay, "127.0.0.1");
        if ((strncmp(mb_url(), "http://127.0.0.1", 16) != 0
             && strncmp(mb_url(), "http://localhost", 16) != 0)
            || !localRelay || !g_mpPingDirectLog || !g_mpPingRelayedLog) {
            fprintf(stderr, "MPPING|FAIL|--mpping needs --mplist on this machine, --mprelay "
                            "127.0.0.1:PORT, --mppingdirect and --mppingrelayed\n");
            dms_close(&menu);
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 2;
        }
        nm_probe_set_relay(g_mpRelay, g_mpRelayPort);

        /* THE NET|probes LINES A ROOM'S LOG HOLDS between two byte offsets (to < 0 is the
           end), and how many probes they say were answered. `size` is where the last whole
           line ends, so an offset taken from it never cuts a line a room is still writing.
           -1 when the log cannot be read. */
        static auto probeLines = [](const char* path, long from, long to, int* answered,
                                    long* size) -> int {
            FILE* f = fopen(path, "rb");
            char line[512];
            int lines = 0, ans = 0;
            long start = 0, whole = 0;
            if (answered) *answered = 0;
            if (size) *size = 0;
            if (!f) return -1;
            while (fgets(line, sizeof line, f)) {
                const long end = ftell(f);
                const size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') {
                    if (start >= from && (to < 0 || start < to)
                        && !strncmp(line, "NET|probes|", 11)) {
                        const char* a = strstr(line, "answered=");
                        lines++;
                        if (a) ans += atoi(a + 9);
                    }
                    whole = end;
                    start = end;
                }
            }
            fclose(f);
            if (answered) *answered = ans;
            if (size) *size = whole;
            return lines;
        };

        /* ---- the list, before the screen opens: every row this run needs, by name ---- */
        MB_Row got[MB_MAX_ROWS];
        int ngot = 0;
        const unsigned tlist = SDL_GetTicks();
        for (;;) {
            mb_forget();
            mb_refresh();
            for (i = 0; i < 100 && mb_phase() != MB_READY && mb_phase() != MB_FAILED; i++)
                SDL_Delay(50);
            ngot = mb_rows(got, MB_MAX_ROWS);
            directKey.clear(); relayedRoom.clear(); quietKey.clear(); fullRoom.clear();
            goneRooms.clear();
            for (i = 0; i < ngot; i++) {
                const MB_Row& g = got[i];
                char at[96];
                snprintf(at, sizeof at, "%s:%u", g.addr, (unsigned)g.port);
                if (!g.relay && !strcmp(g.name, "A DIRECT HOST")) directKey = at;
                else if (g.relay && !strcmp(g.name, "B RELAYED ROOM")) relayedRoom = g.room;
                else if (!g.relay && !strcmp(g.name, "C QUIET DIRECT")) quietKey = at;
                else if (g.relay && !strcmp(g.name, "D FULL ROOM")) fullRoom = g.room;
                else if (g.relay && !strncmp(g.name, "GONE ", 5)) goneRooms.push_back(g.room);
            }
            if ((!directKey.empty() && !relayedRoom.empty() && !quietKey.empty()
                 && !fullRoom.empty() && goneRooms.size() == 9)
                || SDL_GetTicks() - tlist > 30000u)
                break;
            SDL_Delay(500);
        }
        mb_forget();
        printf("MPPING|list|rows=%d|direct=%s|relayed=%s|quiet=%s|full=%s|gone=%d\n", ngot,
               directKey.c_str(), relayedRoom.c_str(), quietKey.c_str(), fullRoom.c_str(),
               (int)goneRooms.size());
        MPP_OK(ngot == 13 && !directKey.empty() && !relayedRoom.empty() && !quietKey.empty()
               && !fullRoom.empty() && goneRooms.size() == 9,
               "the list carries both live rooms and the eleven seeded rows");
        if (fails) {
            printf("MPPING|done|checks=%d|fails=%d\n", checks, fails);
            fflush(stdout);
            dms_close(&menu);
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 1;
        }

        /* AT EVERY SNAPSHOT the prober itself is asked about the rows, not only the cells:
           a cell can be blank while a probe is on its way, and "never handed to the prober"
           is the claim. Asking is safe; nm_probe_poll starts nothing. */
        memset(took, 0, sizeof took);
        dms_mp_snap([](const MP_State* st, int n, void*) {
            NmProbeResult pr;
            size_t k;
            if (n < 0 || n >= 24) return;
            snap[n] = *st;
            took[n] = 1;
            running[n] = queued[n] = 0;
            for (k = 0; k <= goneRooms.size(); k++) {
                const std::string& room = k < goneRooms.size() ? goneRooms[k] : relayedRoom;
                if (nm_probe_poll(room.c_str(), &pr) == NM_PROBE_PROBING) {
                    if (pr.sent > 0) running[n]++;
                    else queued[n]++;
                }
            }
            directPoll[n] = nm_probe_poll(directKey.c_str(), NULL);
            quietPoll[n] = nm_probe_poll(quietKey.c_str(), NULL);
            fullPoll[n] = nm_probe_poll(fullRoom.c_str(), NULL);
            /* THE MOMENT BEFORE THE CLICK: what the direct room had logged by then. */
            if (n == 11)
                directLinesBefore = probeLines(g_mpPingDirectLog, 0, -1, NULL, &directBytes);
            if (n == 16)
                probeLines(g_mpPingDirectLog, 0, -1, NULL, &directBytes3);
        }, NULL);
        dms_mp_autoleave(40000, NULL, NULL);
        mp_init(&mp);

        auto rowOf = [](const MP_State& s, const char* name) -> int {
            for (int k = 0; k < s.rowcount + s.rowhidden; k++)
                if (!strcmp(s.rows[k].name, name)) return k;
            return -1;
        };
        auto cellOf = [](const MP_Row& row) -> std::string {
            char b[16];
            mp_row_ping_text(&row, b, (int)sizeof b);
            return b;
        };
        auto measured = [&](const MP_State& s, int k) -> bool {
            char want[16];
            if (k < 0 || s.rows[k].ping_state != MP_PING_MS) return false;
            if (s.rows[k].ping_ms < 0 || s.rows[k].ping_ms > 50) return false;
            snprintf(want, sizeof want, "%d MS", s.rows[k].ping_ms);
            return cellOf(s.rows[k]) == want;
        };
        auto blank = [&](const MP_State& s, int k) -> bool {
            return k >= 0 && s.rows[k].ping_state == MP_PING_UNKNOWN && cellOf(s.rows[k]).empty();
        };
        /* Unknown values last: every row with a number comes before every row without. */
        auto unknownsLast = [](const MP_State& s) -> bool {
            bool seenUnknown = false;
            for (int k = 0; k < s.rowcount; k++) {
                if (!s.rows[k].joinable) continue;
                const bool known = mp_row_ping_ms(&s.rows[k]) >= 0;
                if (known && seenUnknown) return false;
                if (!known) seenUnknown = true;
            }
            return true;
        };
        auto sayRows = [&](const MP_State& s, const char* when) {
            for (int k = 0; k < s.rowcount + s.rowhidden; k++)
                printf("MPPING|row|%s|%d|%s|state=%d|cell=%s\n", when, k, s.rows[k].name,
                       s.rows[k].ping_state, cellOf(s.rows[k]).c_str());
        };

        /* ---- one: nothing selected, for nine seconds ---- */
        {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN,  500, 500, 0, 0,    0 },
                { MP_I_NET_NET,   500, 500, 0, 0,  200 },
                { DMS_MP_SNAP(0),   0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(1),   0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(2),   0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(3),   0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(4),   0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(5),   0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(6),   0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(7),   0,   0, 0, 0,  250 },
                /* LONGER THAN TWO FETCH INTERVALS, so two refetches land in the wait and a
                   probe per fetch would show in the relayed room's log. */
                { DMS_MP_SNAP(8),   0,   0, 0, 0, 7000 },
                { MP_I_COL_PING,  500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(9),   0,   0, 0, 0,  250 },
                { MP_I_COL_PING,  500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(10),  0,   0, 0, 0,  250 },
                { 0, 0, 0, SDLK_ESCAPE, 0,         300 }
            };
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, 0u, 0u);
            dms_mp_script(NULL, 0);
            MPP_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]), "every step of the first visit ran");
            MPP_OK(r == DMS_CANCEL, "and ESC left the screen");
        }
        {
            const MP_State& s = snap[8];
            const int rel = rowOf(s, "B RELAYED ROOM");
            bool gone = true, directNever = true, quietNever = true, fullNever = true;
            bool bounded = true, sawQueue = false, sawDots = false;
            if (took[8]) sayRows(s, "settled");
            MPP_OK(took[8] && s.rowcount == 13 && s.rowhidden == 0, "the screen drew all thirteen rows");
            MPP_OK(took[8] && measured(s, rel),
                   "the relayed room reads a round trip in milliseconds, as \"NN MS\", with nobody selecting it");
            for (size_t k = 0; k < goneRooms.size(); k++) {
                char name[16];
                snprintf(name, sizeof name, "GONE %d", (int)k + 1);
                const int g = rowOf(s, name);
                if (g < 0 || s.rows[g].ping_state != MP_PING_LOST || cellOf(s.rows[g]) != "--") gone = false;
            }
            MPP_OK(took[8] && gone, "all nine relayed rooms nobody holds read \"--\"");
            for (i = 0; i <= 8; i++) {
                if (!took[i]) continue;
                if (directPoll[i] != NM_PROBE_UNKNOWN) directNever = false;
                if (quietPoll[i] != NM_PROBE_UNKNOWN) quietNever = false;
                if (fullPoll[i] != NM_PROBE_UNKNOWN) fullNever = false;
            }
            const int full = rowOf(s, "D FULL ROOM");
            MPP_OK(took[8] && full >= 0 && !s.rows[full].joinable && blank(s, full) && fullNever,
                   "the full room is blank and was never handed to the prober");
            MPP_OK(took[8] && blank(s, rowOf(s, "A DIRECT HOST")) && blank(s, rowOf(s, "C QUIET DIRECT")),
                   "both direct rooms are blank while neither has been selected");
            MPP_OK(directNever && quietNever,
                   "and neither was handed to the prober at any of nine snapshots across nine seconds");
            for (i = 0; i <= 7; i++) {
                if (!took[i]) continue;
                printf("MPPING|queue|snap=%d|running=%d|waiting=%d\n", i, running[i], queued[i]);
                if (running[i] > NM_PROBE_RUNNING_MAX) bounded = false;
                if (running[i] == NM_PROBE_RUNNING_MAX && queued[i] > 0) sawQueue = true;
                for (int k = 0; k < snap[i].rowcount; k++)
                    if (cellOf(snap[i].rows[k]) == "...") sawDots = true;
            }
            MPP_OK(bounded, "ten relayed rooms were never asked more than eight at a time");
            MPP_OK(sawQueue, "and while eight were being asked, the others waited their turn");
            MPP_OK(sawDots, "a room being asked, or waiting to be, reads \"...\"");

            const MP_State& up = snap[9];
            const MP_State& down = snap[10];
            MPP_OK(took[9] && up.sortcol == MP_SORT_PING && !up.sortdesc && up.rowcount > 0
                   && !strcmp(up.rows[0].name, "B RELAYED ROOM") && unknownsLast(up),
                   "PING, fastest first: the measured room leads and every unknown comes after it");
            MPP_OK(took[10] && down.sortcol == MP_SORT_PING && down.sortdesc && down.rowcount > 0
                   && !strcmp(down.rows[0].name, "B RELAYED ROOM") && unknownsLast(down),
                   "PING, slowest first: the unknowns are still last, not first");
            MPP_OK(took[9] && took[10] && rowOf(up, "A DIRECT HOST") > rowOf(up, "B RELAYED ROOM")
                   && rowOf(down, "A DIRECT HOST") > rowOf(down, "B RELAYED ROOM"),
                   "and a direct room nobody selected sorts with the unknowns both ways");
        }

        /* ---- between the visits: what the two rooms logged ---- */
        SDL_Delay(1500);
        {
            int ans = 0;
            long sz = 0;
            const int dl = probeLines(g_mpPingDirectLog, 0, -1, &ans, &sz);
            printf("MPPING|between|direct-log|probe-lines=%d|answered=%d|bytes=%ld\n", dl, ans, sz);
            MPP_OK(dl == 0, "the direct room's own log shows no probe at all while nobody selected it");
            const int rl = probeLines(g_mpPingRelayedLog, 0, -1, &ans, &sz);
            printf("MPPING|between|relayed-log|probe-lines=%d|answered=%d\n", rl, ans);
            MPP_OK(rl >= 1 && ans == NM_PROBE_COUNT,
                   "the relayed room answered exactly one round of probes in a visit that spanned two fetches of the list");
        }

        /* ---- two: select the direct room, then REFRESH ---- */
        {
            const int di = took[8] ? rowOf(snap[8], "A DIRECT HOST") : -1;
            /* THE SAME ORDER AS THE FIRST VISIT'S NINE SECONDS, GAME A to Z, so the row's
               number is known before the visit and the click is not aimed at a moving list. */
            mp.sortcol = MP_SORT_GAME;
            mp.sortdesc = 0;
            mp.rowtop = 0;
            /* AND FROM THE HOST TAB ON THE LAN, as the first visit began. A screen reopened on
               the internet list asks its relayed rooms on its first frame, and the two presses
               below are each a REFRESH that asks them again, so the relayed room's count would
               depend on how quickly the list came back rather than on the rule. */
            mp.tab = MP_TAB_HOST;
            mp.net = MP_NET_LAN;
            const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN,     500, 500, 0, 0,    0 },
                { MP_I_NET_NET,      500, 500, 0, 0,  200 },
                { DMS_MP_SNAP(11),     0,   0, 0, 0, 2500 },
                { DMS_MP_ROW(di),      0,   0, 0, 0,  100 },
                { DMS_MP_SNAP(12),     0,   0, 0, 0,  250 },
                { DMS_MP_SNAP(13),     0,   0, 0, 0, 2500 },
                { MP_I_REFRESH,      500, 500, 0, 0,  100 },
                { DMS_MP_SNAP(14),     0,   0, 0, 0,  300 },
                { DMS_MP_SNAP(15),     0,   0, 0, 0, 3000 },
                { 0, 0, 0, SDLK_ESCAPE, 0,            300 }
            };
            MPP_OK(di >= 0 && di < MP_LIST_ROWS_VISIBLE, "the direct room's row is on the first screen of the list");
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, 0u, 0u);
            dms_mp_script(NULL, 0);
            MPP_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]), "every step of the second visit ran");
            MPP_OK(r == DMS_CANCEL, "and ESC left the screen");
        }
        {
            const int d11 = took[11] ? rowOf(snap[11], "A DIRECT HOST") : -1;
            const int d12 = took[12] ? rowOf(snap[12], "A DIRECT HOST") : -1;
            const int d13 = took[13] ? rowOf(snap[13], "A DIRECT HOST") : -1;
            const int d14 = took[14] ? rowOf(snap[14], "A DIRECT HOST") : -1;
            const int r14 = took[14] ? rowOf(snap[14], "B RELAYED ROOM") : -1;
            const int r15 = took[15] ? rowOf(snap[15], "B RELAYED ROOM") : -1;
            bool quietNever = true, fullNever = true;
            printf("MPPING|direct-log|before-select|bytes=%ld|probe-lines=%d\n", directBytes,
                   directLinesBefore);
            MPP_OK(took[11] && blank(snap[11], d11) && directPoll[11] == NM_PROBE_UNKNOWN,
                   "back on the list, before the click: the direct room is blank and not handed to the prober");
            MPP_OK(directLinesBefore == 0 && directBytes >= 0,
                   "and its own log, read at that moment, shows no probe");
            MPP_OK(took[12] && d12 >= 0 && !strcmp(snap[12].selkey, snap[12].rows[d12].key)
                   && directPoll[12] != NM_PROBE_UNKNOWN
                   && (snap[12].rows[d12].ping_state == MP_PING_PROBING
                       || snap[12].rows[d12].ping_state == MP_PING_MS),
                   "clicking it selects it, and it is asked at once");
            if (took[13]) sayRows(snap[13], "selected");
            MPP_OK(took[13] && measured(snap[13], d13) && snap[13].rowsel == d13,
                   "the selected direct room reads a round trip in milliseconds");
            MPP_OK(took[14] && !snap[14].selkey[0] && blank(snap[14], d14)
                   && directPoll[14] == NM_PROBE_UNKNOWN,
                   "REFRESH forgets the selection, and with it the direct room's number");
            MPP_OK(took[14] && r14 >= 0 && snap[14].rows[r14].ping_state == MP_PING_PROBING,
                   "and the relayed room is being asked again");
            MPP_OK(took[15] && measured(snap[15], r15) && blank(snap[15], rowOf(snap[15], "A DIRECT HOST")),
                   "which reads a number once more, while the direct room, no longer selected, stays blank");
            for (i = 11; i <= 15; i++) {
                if (!took[i]) continue;
                if (quietPoll[i] != NM_PROBE_UNKNOWN) quietNever = false;
                if (fullPoll[i] != NM_PROBE_UNKNOWN) fullNever = false;
            }
            MPP_OK(quietNever && fullNever,
                   "the other direct room and the full room were never handed to the prober on this visit either");
        }

        SDL_Delay(1500);
        {
            int ansBefore = 0, ansAfter = 0, ansRelayed = 0;
            long sz = 0;
            const int before = directBytes >= 0
                ? probeLines(g_mpPingDirectLog, 0, directBytes, &ansBefore, &sz) : -1;
            const int after = directBytes >= 0
                ? probeLines(g_mpPingDirectLog, directBytes, -1, &ansAfter, &sz) : -1;
            const int rl = probeLines(g_mpPingRelayedLog, 0, -1, &ansRelayed, &sz);
            printf("MPPING|direct-log|after|bytes-before=%ld|lines-before=%d|lines-after=%d|answered-after=%d\n",
                   directBytes, before, after, ansAfter);
            printf("MPPING|relayed-log|after|probe-lines=%d|answered=%d\n", rl, ansRelayed);
            MPP_OK(before == 0 && after >= 1 && ansAfter == NM_PROBE_COUNT,
                   "the direct room's log: nothing before the click, one round of probes after it, and no more");
            MPP_OK(ansRelayed == 3 * NM_PROBE_COUNT,
                   "the relayed room's log: three rounds, one per visit and one for the REFRESH");
        }

        /* ---- three: keys that pass over a direct room, and keys that belong to a field ----
           THE ARROW KEYS WALK DOWN OVER BOTH DIRECT ROOMS to a relayed one and rest there. A
           row the selection only passed through was not chosen, so neither direct room may be
           handed to the prober. Then REFRESH, a code typed into ROOM CODE, and HOME with the
           caret still in the field: HOME belongs to the field and selects nothing, so nothing
           is asked, and Enter joins the typed address rather than a row. The address is a
           port nobody listens on, so the join goes nowhere and the lobby is shut after. */
        {
            mp.sortcol = MP_SORT_GAME;
            mp.sortdesc = 0;
            mp.rowtop = 0;
            mp.tab = MP_TAB_HOST;
            mp.net = MP_NET_LAN;
            mp.selkey[0] = '\0';
            mp.addr[0] = '\0';
            static const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN,     500, 500, 0, 0,    0 },
                { MP_I_NET_NET,      500, 500, 0, 0,  200 },
                { DMS_MP_SNAP(16),     0,   0, 0, 0, 2500 },
                { 0, 0, 0, SDLK_DOWN, 0,               100 },
                { 0, 0, 0, SDLK_DOWN, 0,               120 },
                { 0, 0, 0, SDLK_DOWN, 0,               120 },
                { 0, 0, 0, SDLK_DOWN, 0,               120 },
                { DMS_MP_SNAP(17),     0,   0, 0, 0, 1200 },
                { MP_I_REFRESH,      500, 500, 0, 0,  100 },
                { MP_I_ADDR,         500, 500, 0, 0, 1500 },
                { 0, 0, 0, 0, '1', 100 }, { 0, 0, 0, 0, '2', 40 }, { 0, 0, 0, 0, '7', 40 },
                { 0, 0, 0, 0, '.', 40 },  { 0, 0, 0, 0, '0', 40 }, { 0, 0, 0, 0, '.', 40 },
                { 0, 0, 0, 0, '0', 40 },  { 0, 0, 0, 0, '.', 40 }, { 0, 0, 0, 0, '1', 40 },
                { 0, 0, 0, 0, ':', 40 },  { 0, 0, 0, 0, '9', 40 },
                { DMS_MP_SNAP(18),     0,   0, 0, 0,  250 },
                { 0, 0, 0, SDLK_HOME, 0,               100 },
                { DMS_MP_SNAP(19),     0,   0, 0, 0,  800 },
                { 0, 0, 0, SDLK_RETURN, 0,             100 }
            };
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, 0u, 0u);
            dms_mp_script(NULL, 0);
            MPP_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]), "every step of the third visit ran");
            MPP_OK(r == DMS_MP_LOBBY, "and Enter left the screen for a lobby");
            if (r == DMS_MP_LOBBY) nm_lobby_cancel();
            const int g17 = took[17] ? rowOf(snap[17], "GONE 1") : -1;
            MPP_OK(took[17] && g17 >= 0 && snap[17].rowsel == g17
                   && directPoll[17] == NM_PROBE_UNKNOWN && quietPoll[17] == NM_PROBE_UNKNOWN,
                   "walking the arrow keys down over both direct rooms, to rest on a relayed one, handed "
                   "neither direct room to the prober");
            MPP_OK(took[18] && snap[18].focus == MP_I_ADDR && !strcmp(snap[18].addr, "127.0.0.1:9")
                   && snap[18].selkey[0] == '\0' && snap[18].rowsel == -1,
                   "after REFRESH, a click on ROOM CODE took a typed address, with no game selected");
            MPP_OK(took[19] && snap[19].selkey[0] == '\0' && snap[19].rowsel == -1
                   && snap[19].focus == MP_I_ADDR && directPoll[19] == NM_PROBE_UNKNOWN,
                   "HOME with the caret in the field selected no game, and the direct room was not asked");
            MPP_OK(!strcmp(mp.name, "127.0.0.1:9"),
                   "and Enter joined the typed address, not a row of the list");
            if (strcmp(mp.name, "127.0.0.1:9")) printf("MPPING|why|joined=%s\n", mp.name);
        }
        SDL_Delay(1500);
        {
            int ans = 0;
            long sz = 0;
            const int pl = directBytes3 >= 0 ? probeLines(g_mpPingDirectLog, directBytes3, -1, &ans, &sz) : -1;
            int joins = -1;
            FILE* f = directBytes3 >= 0 ? fopen(g_mpPingDirectLog, "rb") : NULL;
            if (f) {
                char line[512];
                long start = 0;
                joins = 0;
                while (fgets(line, sizeof line, f)) {
                    const long end = ftell(f);
                    const size_t len = strlen(line);
                    if (len > 0 && line[len - 1] == '\n') {
                        if (start >= directBytes3 && (!strncmp(line, "NET|joiner|", 11)
                                                      || !strncmp(line, "NET|refused|", 12)))
                            joins++;
                        start = end;
                    }
                }
                fclose(f);
            }
            printf("MPPING|direct-log|third-visit|from=%ld|probe-lines=%d|answered=%d|joins=%d\n",
                   directBytes3, pl, ans, joins);
            MPP_OK(pl == 0 && joins == 0,
                   "the direct room's own log shows no probe and no one joining across the whole third visit");
        }

        /* ---- the pictures, drawn from the states the loop itself held ---- */
        if (g_mpPingShot) {
            int best = -1, bestDots = 0, bad = 0, shots = 0;
            for (i = 0; i <= 7; i++) {
                int dots = 0;
                if (!took[i]) continue;
                for (int k = 0; k < snap[i].rowcount; k++)
                    if (cellOf(snap[i].rows[k]) == "...") dots++;
                if (dots > bestDots) { bestDots = dots; best = i; }
            }
            const int pick[3] = { best, 8, 13 };
            static const char* const PNAME[3] = { "join-net-probing", "join-net-live", "join-net-live-direct" };
            for (i = 0; i < 3; i++) {
                char path[512];
                if (pick[i] < 0 || !took[pick[i]]) continue;
                bad += mp_check_layout(menu.pack, &snap[pick[i]]);
                db_fill_rect(&menu.surf, 0, 0, menu.surf.w - 1, menu.surf.h - 1, 0);
                dm_draw_plate(&menu.surf, menu.pack);
                mp_draw(&menu.surf, menu.pack, &snap[pick[i]]);
                snprintf(path, sizeof path, "%s_%s.png", g_mpPingShot, PNAME[i]);
                if (dms_write_shot(&menu, path)) shots++;
            }
            printf("MPPING|shots=%d|overflow=%d\n", shots, bad);
            MPP_OK(shots == 3 && bad == 0, "three pictures of the live list, and every string in them fits");
        }

        dms_mp_snap(NULL, NULL);
        nm_probe_shutdown();
        printf("MPPING|done|checks=%d|fails=%d\n", checks, fails);
        fflush(stdout);
        #undef MPP_OK
        dms_close(&menu);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fails ? 1 : 0;
    }

    /* --mpclick: THE MULTIPLAYER SCREEN, PRESSED. Three visits through the real SDL loop
       in dms_multiplayer, with synthetic mouse and keyboard events pushed onto the same
       queue a hand fills, against a game list running on this machine.

       WHY IT EXISTS. --mpaddr calls the screen's pure functions and --mpshot draws a
       state it was handed; neither one presses anything. So the three questions that
       matter about the browser -- does a row highlight when it is clicked, does clicking
       one and pressing JOIN reach a lobby, and does the ROOM CODE field under the rows
       still take a typed code -- had no answer in this tree at all, and the honest
       account of the screen said so.

       IT REFUSES TO RUN AGAINST ANYTHING BUT THIS MACHINE. The row it publishes is not a
       real game, so putting one on a list other people read would be advertising a door
       that is not there. */
    if (g_mpClickTest) {
        MP_State mp;
        int fails = 0, checks = 0;
        int r;

        #define MPC_OK(cond, what) do { checks++; \
            if (!(cond)) { fails++; printf("MPCLICK|FAIL|%s\n", (what)); } \
            else printf("MPCLICK|ok|%s\n", (what)); } while (0)

        if (strncmp(mb_url(), "http://127.0.0.1", 16) != 0
            && strncmp(mb_url(), "http://localhost", 16) != 0) {
            fprintf(stderr, "MPCLICK|FAIL|--mpclick needs --mplist pointing at a game "
                            "list on this machine, and was given %s\n", mb_url());
            dms_close(&menu);
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 1;
        }
        /* THE PING COLUMN'S RELAY IS NOBODY. The internet list asks relayed rooms how far
           away they are through a relay, and the built-in one is a public server; a gate
           that runs on every push has no business sending it anything. Relayed probes go
           to a closed port on this machine instead and read as unanswered. */
        nm_probe_set_relay("127.0.0.1", 9);

        /* A ROW TO CLICK ON, put on the real list through the real sender so the screen
           finds it the way it finds any other: a POST, a GET and a parse, with nothing
           planted in the screen's own array. It is a DIRECT row on this machine, because
           that is the one kind of row a joiner can be pointed at with no relay and no
           second machine, which is what keeps this gate offline.

           IT IS PUT BACK BEFORE EVERY LEG, because leaving the screen withdraws whatever
           this program had listed and that is correct behaviour rather than a nuisance.
           The withdrawal has to be allowed to LAND first: the sender carries one request
           at a time and silently skips a second, so a publish issued while the withdrawal
           was still in flight did nothing at all and the next leg opened on an empty list
           -- which reads exactly like the click being broken, and cost a run here. */
        static const char* const MPC_NAME = "CLICKED GAME";
        #define MPC_LIST_ROW() do { \
            MB_Row pub; \
            int tries; \
            MB_Row got[MB_MAX_ROWS]; \
            SDL_Delay(400); \
            memset(&pub, 0, sizeof pub); \
            snprintf(pub.name, sizeof pub.name, "%s", MPC_NAME); \
            snprintf(pub.map, sizeof pub.map, "%s", "GREEN ACRES"); \
            snprintf(pub.scenario, sizeof pub.scenario, "%s", "SCM01EA"); \
            pub.relay = 0; \
            pub.port = NM_PORT_DEFAULT; \
            pub.players_now = 1; \
            pub.players_max = 4; \
            pub.locked = 0; \
            pub.abi = 0u; \
            pub.scen = 0u; \
            mb_publish(&pub); \
            for (tries = 0; tries < 60; tries++) { \
                SDL_Delay(50); \
                mb_refresh(); \
                SDL_Delay(50); \
                if (mb_rows(got, MB_MAX_ROWS) > 0) break; \
            } \
            MPC_OK(mb_rows(got, MB_MAX_ROWS) > 0, \
                   "the game is on the list before the screen is opened"); \
        } while (0)
        /* ZERO abi AND scen ON PURPOSE above: zero means "cannot answer" on either side of
           the compatibility check, so the row stays joinable whatever this build hashes to
           and this gate is testing the CLICK rather than the handshake. */

        MPC_LIST_ROW();

        /* A WATCHDOG, NOT THE MECHANISM. Every leg leaves by pressing something; this is
           what stops a leg whose last press was refused from sitting on the screen for
           ever. It answers DMS_CANCEL, which is also what a correct ESC answers, so each
           leg also checks that every one of its steps ran. */
        dms_mp_autoleave(25000, NULL, NULL);

        mp_init(&mp);

        /* ---- one: a row is clicked, and it takes the highlight ---- */
        {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN, 500, 500, 0, 0,    0 },
                { MP_I_NET_NET,  500, 500, 0, 0,  200 },
                { DMS_MP_ROW(0),   0,   0, 0, 0, 2500 },
                { 0, 0, 0, SDLK_ESCAPE, 0,        400 }
            };
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, 0u, 0u);
            dms_mp_script(NULL, 0);
            MPC_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]),
                   "every step of the first script ran");
            MPC_OK(mp.tab == MP_TAB_JOIN && mp.net == MP_NET_INTERNET,
                   "the tabs walk to JOIN and then to INTERNET");
            MPC_OK(mp.rowcount >= 1, "the internet list fills from the service");
            MPC_OK(mp.rowcount >= 1 && !strcmp(mp.rows[0].name, MPC_NAME),
                   "and the row on it is the game that was published");
            MPC_OK(mp.rowsel == 0, "clicking a row selects it, which is what draws it lit");
            MPC_OK(r == DMS_CANCEL, "and ESC leaves the screen");
        }

        /* ---- two: that row, JOIN, the name prompt, and a lobby ---- */
        {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN, 500, 500, 0, 0,    0 },
                { MP_I_NET_NET,  500, 500, 0, 0,  200 },
                { DMS_MP_ROW(0),   0,   0, 0, 0, 2500 },
                /* JOIN with no handle yet opens the YOUR NAME box, which is the first
                   thing a new player meets and had never been driven. */
                { MP_I_JOIN,     500, 500, 0, 0,  300 },
                { 0, 0, 0, 0, 'A', 200 },
                { 0, 0, 0, 0, 'C', 60 },
                { 0, 0, 0, 0, 'E', 60 },
                { MP_I_HANDLE_OK, 500, 500, 0, 0, 200 }
            };
            /* The row went when the last visit ended, and correctly. Put it back. */
            MPC_LIST_ROW();
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, 0u, 0u);
            dms_mp_script(NULL, 0);
            MPC_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]),
                   "every step of the join script ran");
            MPC_OK(!strcmp(mp.handle, "ACE"),
                   "the YOUR NAME box takes a typed handle and OK closes it");
            MPC_OK(r == DMS_MP_LOBBY, "clicking a row and pressing JOIN reaches a lobby");
            if (r != DMS_MP_LOBBY)
                printf("MPCLICK|why|status=%s|lobby=%s\n", mp.status, nm_lobby_error());
            nm_lobby_cancel();
        }

        /* ---- three: the ROOM CODE field under the rows still takes a code ---- */
        {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_JOIN, 500, 500, 0, 0,    0 },
                { MP_I_NET_NET,  500, 500, 0, 0,  200 },
                /* THE FIELD IS CLICKED, not focused by hand. It sits four pixels under the
                   last row a click can land on, and this is the leg that would fail if
                   the two ever drifted: the press would select the row behind it and the
                   characters below would go nowhere. */
                { MP_I_ADDR,     500, 500, 0, 0,  300 },
                { 0, 0, 0, 0, '#', 150 },
                { 0, 0, 0, 0, 'K', 60 },
                { 0, 0, 0, 0, '7', 60 },
                { 0, 0, 0, 0, 'M', 60 },
                { 0, 0, 0, 0, '-', 60 },
                { 0, 0, 0, 0, '3', 60 },
                { 0, 0, 0, 0, 'Q', 60 },
                { 0, 0, 0, 0, 'X', 60 },
                { 0, 0, 0, SDLK_ESCAPE, 0, 250 },   /* drops the caret                */
                { 0, 0, 0, SDLK_ESCAPE, 0, 250 }    /* and then leaves the screen     */
            };
            /* WITH A ROW ON THE LIST, deliberately: the field four pixels under the last
               clickable row is only interesting while there is a row to steal the press. */
            MPC_LIST_ROW();
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, 0u, 0u);
            dms_mp_script(NULL, 0);
            MPC_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]),
                   "every step of the room code script ran");
            MPC_OK(mp.focus != MP_I_ADDR, "ESC drops the caret out of the field");
            MPC_OK(!strcmp(mp.addr, "#K7M-3QX"),
                   "clicking the ROOM CODE field and typing a code fills it");
            if (strcmp(mp.addr, "#K7M-3QX"))
                printf("MPCLICK|why|addr=%s\n", mp.addr);
            MPC_OK(r == DMS_CANCEL, "and a second ESC leaves the screen");
        }

        /* ---- four: LIST PUBLICLY, and that it cannot publish an address by accident ---- */
        {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_HOST, 500, 500, 0, 0,   0 },
                { MP_I_PUBLIC,   500, 500, 0, 0, 250 },
                { MP_I_RELAY,    500, 500, 0, 0, 250 },
                { MP_I_RELAY,    500, 500, 0, 0, 250 },
                { 0, 0, 0, SDLK_ESCAPE, 0,       250 }
            };
            dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
            r = dms_multiplayer(&menu, &mp, NULL, 0u, 0u);
            dms_mp_script(NULL, 0);
            MPC_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]),
                   "every step of the listing script ran");
            /* The middle two steps tick the relay and untick it again. What must survive
               that round trip is NOTHING: a tick left in a box the relay hid is a decision
               about a different kind of room that the player can no longer see, and it
               would put a home address on a public list without anybody choosing to. */
            MPC_OK(mp.relay_game == 0 && mp.list_game == 0,
                   "ticking LIST PUBLICLY and then walking the relay box in and out "
                   "leaves the address unlisted");
            MPC_OK(mp_item_visible(&mp, MP_I_PUBLIC),
                   "and the box is back on the screen with the relay off again");
            MPC_OK(r == DMS_CANCEL, "and ESC leaves the screen");
        }

        /* ---- five: HOST with INTERNET GAME ticked ----
           THE ACTION A CRASH WAS REPORTED ON, and the reason a driver for this screen was
           worth building at all: it could not be reproduced from a picture, because every
           page of it renders clean, so it needs the press itself. Opt-in, because it arms
           a relay and the screen dials the built-in one.

           WHAT IS ASSERTED IS SURVIVAL, not success. A machine with no route to the relay
           must refuse in words and stay on the screen; a machine with one must open a
           room. Either is a pass. Reaching the assertion at all is the answer to the
           question this leg exists for. */
        if (g_mpClickHost) {
            static const DMS_MpStep S[] = {
                { MP_I_TAB_HOST, 500, 500, 0, 0,    0 },
                { MP_I_RELAY,    500, 500, 0, 0,  250 },
                { MP_I_HOST,     500, 500, 0, 0,  250 }
            };
            SK_State tmp;
            SK_Lobby lob;
            NmSetup ns;
            unsigned hscen = 0;
            skirmish_scan(opt.dir);
            if (g_skirmishMaps.empty()) {
                MPC_OK(0, "there is a multiplayer map to host, which HOST needs");
            } else {
                sk_init(&tmp, &g_skirmishMaps[0], (int)g_skirmishMaps.size(), NULL);
                tmp.sel = 0;
                sk_result(&tmp, &lob);
                if (!mp_build_setup(&ns, &lob, &mp, base_dir(opt.dir), &hscen)) {
                    MPC_OK(0, "a match setup can be packed for the wire");
                } else {
                    dms_mp_script(S, (int)(sizeof S / sizeof S[0]));
                    r = dms_multiplayer(&menu, &mp, &ns, 0u, 0u);
                    dms_mp_script(NULL, 0);
                    MPC_OK(dms_mp_script_ran() == (int)(sizeof S / sizeof S[0]),
                           "every step of the host script ran");
                    MPC_OK(mp.relay_game == 1,
                           "INTERNET GAME is ticked when HOST is pressed");
                    MPC_OK(r == DMS_MP_LOBBY || mp.status[0] != '\0',
                           "HOST with INTERNET GAME ticked either opens a room or says "
                           "why, and does not take the program down");
                    printf("MPCLICK|host|rc=%d|room=%s|status=%s\n",
                           r, nm_room_code(), mp.status);
                    nm_lobby_cancel();
                    mb_unpublish();
                }
            }
        }

        mb_unpublish();
        SDL_Delay(200);
        printf("MPCLICK|done|checks=%d|fails=%d\n", checks, fails);
        fflush(stdout);
        #undef MPC_LIST_ROW
        #undef MPC_OK
        dms_close(&menu);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fails ? 1 : 0;
    }

    if (g_mpShot) {
        /* Both pages and both tabs, so one command answers "does every string fit" for the
           whole screen rather than for the page that happened to be showing. */
        MP_State mp;
        int bad = 0, n = 0;
        mp_init(&mp);
        {
            /* FIVE PAGES, NOT FOUR. The INTERNET sub-tab was never drawn here, so the
               one page carrying a typed address -- the longest string this screen can be
               asked to render -- was neither pictured nor measured by mp_check_layout.
               A page that no audit visits is a page whose labels can quietly outgrow
               their boxes. */
            /* SIX PAGES, NOT FIVE. The host page draws INTERNET GAME ticked, which
               HIDES the LIST PUBLICLY box under it, so the warning that box carries was
               drawn nowhere and pictured nowhere. A sixth page holds the other half of
               that choice: the relay off and the room listed by its address. */
            /* SEVEN. join-hidden is the list emptied by its HIDE boxes, the one state in
               which the list has to say WHY it is empty rather than that it is waiting. */
            static const char* const NAMES[7] = {
                "host", "join", "join-net", "wait-host", "wait-join", "host-direct",
                "join-hidden"
            };
            /* FOURTEEN ROWS ON EACH LIST PAGE, more than the ten that fit, so the scroll
               bar has somewhere to go. Built for the audit as much as the picture: a
               thirty-one character name of the widest letter, the widest official map
               name, a padlock, a row from another build and a full one, a direct row on
               the longest IPv4 address, and relayed rows. The PING cells are set here by
               hand, so that every form a cell can take is pictured and measured on one
               page whatever a live run happens to find; --mpping pictures the measured
               ones. */
            struct ShotRow { const char* name; const char* map; int now, max, locked, why, relay;
                             const char* room; const char* addr; const char* lanaddr;
                             int ping, ms; };
            static const ShotRow SHOT[14] = {
                { "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", "GREEN ACRES", 7, 8, 0, MP_WHY_OK, 1,
                  "#K7M-3QX", "", "192.168.1.20", MP_PING_MS, 12 },
                { "ECHO ROOM",    "ALPINE",            6, 8, 0, MP_WHY_OK,    1, "#Q2Z-9WX", "", "192.168.1.21", MP_PING_MS, 48 },
                { "LOCKED ROOM",  "RIVER RAID",        5, 8, 1, MP_WHY_OK,    1, "#850-RZ2", "", "192.168.1.22", MP_PING_PROBING, 0 },
                { "RELAYED ROOM", "DESERT",            5, 6, 0, MP_WHY_OK,    1, "#7HT-4KD", "", "192.168.1.23", MP_PING_MS, 1400 },
                { "MOOSE FANS",   "MOOSEHEAD BARRENS", 4, 8, 0, MP_WHY_OK,    1, "#M00-5E1", "", "192.168.1.24", MP_PING_UNKNOWN, 0 },
                { "DIRECT DAVE",  "CANYON",            4, 6, 0, MP_WHY_OK,    0, "", "255.255.255.255", "192.168.100.200", MP_PING_LOST, 0 },
                { "HOTEL",        "HILLS",             3, 8, 0, MP_WHY_OK,    0, "", "81.2.69.160", "192.168.1.26", MP_PING_UNKNOWN, 0 },
                { "JULIET",       "BLUE LAKES",        3, 4, 1, MP_WHY_OK,    1, "#JUL-137", "", "192.168.1.27", MP_PING_MS, 77 },
                { "KILO",         "LAGOON",            2, 8, 0, MP_WHY_OK,    1, "#K1L-000", "", "192.168.1.28", MP_PING_MS, 5 },
                { "LIMA",         "MESA",              2, 4, 0, MP_WHY_OK,    0, "", "203.0.113.9", "192.168.1.29", MP_PING_MS, 999 },
                { "MIKE",         "SWAMP",             1, 2, 0, MP_WHY_OK,    1, "#M1K-E00", "", "192.168.1.30", MP_PING_MS, 31 },
                { "FULL HOUSE",   "BAYOU",             4, 4, 0, MP_WHY_FULL,  1, "#FUL-444", "", "192.168.1.31", MP_PING_MS, 20 },
                { "OTHER BUILD",  "TUNDRA",            3, 8, 0, MP_WHY_BUILD, 0, "", "198.51.100.4", "192.168.1.32", MP_PING_MS, 20 },
                { "OLD BUILD",    "RAVINE",            1, 8, 1, MP_WHY_BUILD, 1, "#0LD-BLD", "", "192.168.1.33", MP_PING_UNKNOWN, 0 },
            };
            auto fillList = [](MP_State* s, int lan) {
                s->rowcount = 14;
                s->rowhidden = 0;
                for (int q = 0; q < 14; q++) {
                    MP_Row* r = &s->rows[q];
                    const ShotRow& d = SHOT[q];
                    memset(r, 0, sizeof *r);
                    snprintf(r->name, sizeof r->name, "%s", d.name);
                    snprintf(r->map, sizeof r->map, "%s", d.map);
                    r->players_now = d.now;
                    r->players_max = d.max;
                    r->locked = d.locked;
                    r->why = d.why;
                    r->joinable = (d.why == MP_WHY_OK);
                    if (lan) {
                        r->lan = 1;
                        snprintf(r->addr, sizeof r->addr, "%s", d.lanaddr);
                        r->port = 17421;
                    } else {
                        r->relay = d.relay;
                        snprintf(r->room, sizeof r->room, "%s", d.room);
                        snprintf(r->addr, sizeof r->addr, "%s", d.addr);
                        r->port = d.relay ? 0 : 65535;
                        r->ping_state = d.ping;
                        r->ping_ms = d.ms;
                    }
                    mp_row_key(r, NULL);
                }
            };
            int k;
            for (k = 0; k < 7; k++) {
                char path[512];
                const int list = (k == 1 || k == 2 || k == 6);
                mp.page = (k == 3 || k == 4) ? MP_PAGE_WAIT : MP_PAGE_PICK;
                mp.tab = list ? MP_TAB_JOIN : MP_TAB_HOST;
                mp.net = (k == 2 || k == 6) ? MP_NET_INTERNET : MP_NET_LAN;
                mp.wait_ishost = (k == 3);
                mp.wait_humans = 4;
                mp.wait_myseat = (k == 3) ? 0 : 1;
                /* The host's readout, on the page the host actually sits on. */
                snprintf(mp.myaddr, sizeof mp.myaddr, "%s", "192.168.1.204:17421");
                /* A LONG one on purpose: it is past the old thirty-two character cap, so
                   this page is also the regression test for the field keeping its tail. */
                /* The INTERNET page carries a LONG address on purpose -- it is past the
                   old thirty-two character cap, so this page is the regression test for
                   the field keeping its tail -- and the host's own line carries a ROOM
                   CODE, so the sigil that distinguishes a code from a hostname appears in
                   a picture rather than only in an assertion. */
                snprintf(mp.addr, sizeof mp.addr, "%s",
                         (k == 2) ? "somebody-longish.duckdns.org:17421" : "");
                mp.wait_taken[0] = mp.wait_taken[1] = 1;
                mp.wait_ready[0] = 1;
                mp.private_game = 1;
                /* The host page shows the relay checkbox TICKED, so its own line and the
                   forced PRIVATE GAME are both in a picture and both measured. */
                mp.relay_game = (k == 0);
                /* THE WARNING LINE, on the one page that can show it. */
                mp.list_game = (k == 5);
                snprintf(mp.pass, sizeof mp.pass, "%s", "1234");
                /* THE LIST PAGES CARRY NO SHELL STATUS, so the status line shows what it
                   says about the selected row, which is the sentence worth measuring. */
                snprintf(mp.status, sizeof mp.status, "%s",
                         list ? "" : "WAITING FOR EVERYONE TO PRESS READY.");
                /* EVERY PAGE STARTS FROM AN EMPTY LIST, so no page draws rows, a sort or a
                   ticked HIDE box it inherited from the page before it in this loop. */
                mp.rowcount = 0;
                mp.rowhidden = 0;
                mp.rowsel = -1;
                mp.rowtop = 0;
                mp.selkey[0] = '\0';
                mp.sortcol = MP_SORT_GAME;
                mp.sortdesc = 0;
                mp.hide_locked = 0;
                mp.hide_grey = 0;
                if (list) {
                    fillList(&mp, k == 1);
                    /* Sorted PLAYERS most first, so the header shows its downward marker. */
                    mp.sortcol = MP_SORT_PLAYERS;
                    mp.sortdesc = 1;
                    if (k == 6) {
                        /* Every row either padlocked or grey, and both boxes ticked: the
                           list is empty because of the filters, and says so. Sorted GAME
                           A to Z, so the upward marker is in a picture as well as the
                           downward one. */
                        for (int q = 0; q < 14; q++)
                            if (mp.rows[q].joinable) mp.rows[q].locked = 1;
                        mp.hide_locked = 1;
                        mp.hide_grey = 1;
                        mp.sortcol = MP_SORT_GAME;
                        mp.sortdesc = 0;
                    }
                    mp_view(&mp, MP_VIEW_REFILL);
                    if (k != 6) {
                        /* Scrolled three rows, so the thumb is mid-travel, with the direct
                           row on the third visible line selected, so its sentence is drawn. */
                        mp.rowtop = 3;
                        mp.rowsel = 5;
                        snprintf(mp.selkey, sizeof mp.selkey, "%s", mp.rows[5].key);
                    }
                }
                bad += mp_check_layout(menu.pack, &mp);
                db_fill_rect(&menu.surf, 0, 0, menu.surf.w - 1, menu.surf.h - 1, 0);
                dm_draw_plate(&menu.surf, menu.pack);
                mp_draw(&menu.surf, menu.pack, &mp);
                snprintf(path, sizeof path, "%s_%s.png", g_mpShot, NAMES[k]);
                if (dms_write_shot(&menu, path)) n++;
                /* WHERE THE SCROLL BAR AND ITS THUMB ARE on each list page, in menu pixels,
                   so a gate can measure the thumb against its well in the picture itself.
                   thumb_h=0 says no thumb is drawn, because every shown game fits. */
                if (list) {
                    int bx, by, bw, bh, ty, th;
                    if (mp_item_rect(&mp, MP_I_LIST_BAR, &bx, &by, &bw, &bh)) {
                        mp_bar_thumb(&mp, &ty, &th);
                        printf("MPSHOT|bar|%s|x=%d|y=%d|w=%d|h=%d|thumb_y=%d|thumb_h=%d\n",
                               NAMES[k], bx, by, bw, bh, ty, th);
                    }
                }
            }
            /* THE TRACK WITH NOTHING TO SCROLL, read off the drawn surface rather than a
               picture, because no page shows a list of ten or fewer games that is not
               empty. Each state is drawn exactly as a page is, and the bar's rectangle is
               compared pixel by pixel with an empty track: the well's green, a shadow line
               on the top and left, a lit line on the bottom and right. off counts the
               pixels that differ. Eleven games, one more than fit, is the control: it has
               a thumb, so a probe that could not see one would say so. No picture is
               written, and every page after this clears the surface before it draws. */
            {
                static const int PROBE[3] = { 11, 10, 0 };
                for (int q = 0; q < 3; q++) {
                    int bx, by, bw, bh, ty, th, off = 0;
                    mp.page = MP_PAGE_PICK;
                    mp.tab = MP_TAB_JOIN;
                    mp.net = MP_NET_INTERNET;
                    mp.prompt = 0;
                    mp.drag = 0;
                    mp.hover = -1;
                    mp.pressed = -1;
                    mp.addr[0] = '\0';
                    mp.status[0] = '\0';
                    mp.rowsel = -1;
                    mp.rowtop = 0;
                    mp.selkey[0] = '\0';
                    mp.sortcol = MP_SORT_GAME;
                    mp.sortdesc = 0;
                    mp.hide_locked = 0;
                    mp.hide_grey = 0;
                    fillList(&mp, 0);
                    mp.rowcount = PROBE[q];
                    mp.rowhidden = 0;
                    mp_view(&mp, MP_VIEW_REFILL);
                    if (!mp_item_rect(&mp, MP_I_LIST_BAR, &bx, &by, &bw, &bh)) {
                        printf("MPSHOT|track|rows=%d|nobar\n", PROBE[q]);
                        continue;
                    }
                    mp_bar_thumb(&mp, &ty, &th);
                    db_fill_rect(&menu.surf, 0, 0, menu.surf.w - 1, menu.surf.h - 1, 0);
                    dm_draw_plate(&menu.surf, menu.pack);
                    mp_draw(&menu.surf, menu.pack, &mp);
                    for (int yy = by; yy < by + bh; yy++) {
                        for (int xx = bx; xx < bx + bw; xx++) {
                            unsigned char want = DM_GREEN_BKGD;
                            if (xx == bx + bw - 1 || yy == by + bh - 1) want = DM_LIGHT_GREEN;
                            else if (xx == bx || yy == by) want = DM_GREEN_SHADOW;
                            if (menu.surf.px[yy * menu.surf.w + xx] != want) off++;
                        }
                    }
                    printf("MPSHOT|track|rows=%d|thumb_h=%d|off=%d\n", mp.rowcount, th, off);
                }
            }
        }
        /* AND THE LOBBY AS A NETWORK LOBBY, both sides, because that screen is the
           waiting room now and it is the one with the most to get wrong: a joiner must
           see the map and the rules and be refused them, and see its own row and not be. */
        skirmish_scan(opt.dir);
        if (!g_skirmishMaps.empty()) {
            /* THREE PAGES: the host's room, the joiner's, and the host's with the map
               PICKER open -- the window nothing else pictures, opened through the public
               path so the state is the one a hand makes. */
            static const char* const NETNAMES[3] = { "lobby-host", "lobby-join", "lobby-pick" };
            int k;
            for (k = 0; k < 3; k++) {
                SK_State lob;
                char path[512];
                sk_init(&lob, &g_skirmishMaps[0], (int)g_skirmishMaps.size(),
                        skirmish_previews());
                lob.net = (k == 1) ? 2 : 1;
                lob.net_seat = (k == 1) ? 1 : 0;
                lob.net_humans = 2;
                lob.net_seats = 4;                 /* a four-start map's room */
                lob.net_taken[0] = lob.net_taken[1] = 1;
                lob.net_ready[0] = 1;
                lob.mode[2] = SK_SEAT_BOT;         /* one computer, one empty seat */
                /* A POPULATED CHAT, so the audit measures a pane with lines in it and the
                   picture shows the per-speaker colours rather than an empty well. */
                sk_chat_push(&lob, -1, "PLAYER 2 joined the game");
                sk_chat_push(&lob, 0, "HOST: ready when you are");
                sk_chat_push(&lob, 1, "PLAYER 2: one moment");
                sk_chat_push(&lob, -1, "PLAYER 2 is Ready!");
                snprintf(lob.chat_in, sizeof lob.chat_in, "%s", "good luck");
                lob.chat_focus = 1;
                lob.caret_on = 1;
                /* The host's join line, which is the only place a room code is ever shown.
                   Only the host page carries it, which is also the rule on screen. */
                if (k == 0)
                    snprintf(lob.net_join, sizeof lob.net_join, "%s", "ROOM CODE  #K7M-3QX");
                if (k == 2) {
                    int px, py, pw, ph;
                    if (sk_item_rect(&lob, SK_I_CHANGEMAP, &px, &py, &pw, &ph)) {
                        sk_press(&lob, px + 1, py + 1);
                        sk_release(&lob, px + 1, py + 1);
                    }
                }
                bad += sk_check_layout(menu.pack, &lob);
                db_fill_rect(&menu.surf, 0, 0, menu.surf.w - 1, menu.surf.h - 1, 0);
                dm_draw_plate(&menu.surf, menu.pack);
                sk_draw(&menu.surf, menu.pack, &lob);
                snprintf(path, sizeof path, "%s_%s.png", g_mpShot, NETNAMES[k]);
                if (dms_write_shot(&menu, path)) n++;
            }
        }
        printf("MPSHOT|shots=%d|overflow=%d\n", n, bad);
        fflush(stdout);
        dms_close(&menu);
        return bad ? 1 : 0;
    }
    if (g_specopsShot) {
        /* THE SPECIAL OPS LIST, TWICE: its first page and, after END, its last. The
           second picture is where the TEST MAPS section and the Test Map row live, and
           a picture of the first page alone could not show that the list ends where it
           should. dms_special is not driven here because it is a loop waiting for a
           hand; the list state is built and drawn through the same redraw the loop
           uses, so the pixels are the loop's pixels. */
        int src = 0;
        specops_scan(base_dir(opt.dir));
        if (g_specops.empty()) {
            fprintf(stderr, "specops: no missions in '%s'\n", opt.dir ? opt.dir : "missions/");
            src = 1;
        } else {
            DO_State ops;
            char path[512];
            int dw = 0, dh = 0, k;
            do_state_init(&ops, &g_specopsRows[0], (int)g_specopsRows.size());
            menu.ops = &ops;
            menu.mx = DM_SCREEN_W / 2;
            menu.my = DM_SCREEN_H / 2;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            for (k = 0; k < 2; k++) {
                if (k == 1) do_move_end(&ops);
                dms_redraw(&menu);
                dms_draw(&menu);          /* lays the letterbox out itself */
                snprintf(path, sizeof path, "%s/specops_%s.png", g_specopsShot,
                         k == 0 ? "top" : "end");
                if (!game_grab_png(path, dw, dh)) src = 1;
                printf("SPECOPSSHOT|%s|%s|top=%d|selected=%d\n", k == 0 ? "top" : "end",
                       path, ops.top, ops.selected);
                SDL_GL_SwapWindow(win);
            }
            menu.ops = NULL;
            /* The filing, as text, so a gate can read it without a picture. */
            {
                int missions = 0, headings = 0;
                std::string order;
                for (size_t i = 0; i < g_specops.size(); i++) {
                    if (g_specops[i].header) {
                        headings++;
                        if (!order.empty()) order += ",";
                        order += g_specops[i].name;
                        printf("SPECOPS|heading|%d|%s\n", (int)i, g_specops[i].name.c_str());
                    } else {
                        missions++;
                        printf("SPECOPS|row|%d|%s|%s|%s\n", (int)i, g_specops[i].scen.c_str(),
                               g_specops[i].pack.c_str(), g_specops[i].name.c_str());
                    }
                }
                printf("SPECOPSSHOT|rows=%d|missions=%d|headings=%d|order=%s|last=%s|"
                       "selected=%d|rc=%d\n",
                       (int)g_specops.size(), missions, headings, order.c_str(),
                       g_specops.empty() ? "" : g_specops.back().scen.c_str(),
                       ops.selected, src);
            }
            fflush(stdout);
        }
        dms_close(&menu);
        game_set_audio(NULL);
        audio_boot_shutdown(au);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return src;
    }
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

    /* --lobbyhost / --lobbyjoin: the two halves of one measurement, and see the flags'
       own comment above for what it is and why it exists. Placed here beside --lobbyshot
       because the joining half needs exactly what that one needs and nothing else: the
       menu shell open (the lobby borrows its window, its surface and its letterbox) and
       the skirmish list scanned. Neither half ever reaches the main menu.

       ONE ARM FOR BOTH ROLES, because the two are read as a pair and a reader who has to
       hold one of them in their head while finding the other in a different file will
       eventually change one and not the other. */
    if (g_lobbyHostMs > 0 || g_lobbyJoinMs > 0) {
        int lsrc = 0;
        skirmish_scan(opt.dir);
        if (g_skirmishMaps.empty() || mp_map_index(LOBBYSEAT_MAP) < 0) {
            fprintf(stderr, "LOBBYSEAT|FAIL|%s is not in '%s', so there is no room to open\n",
                    LOBBYSEAT_MAP, base_dir(opt.dir));
            lsrc = 1;
        } else if (g_lobbyHostMs > 0) {
            /* THE HOST. Every call here is one the lobby screen makes; the only things
               missing are the drawing and START, and START is missing on purpose.

               IT REPORTS ON THE EDGE, not on every poll. A table of eight seats printed
               ten times a second would bury the one line that matters under six thousand
               that say nothing happened, and a line written when the setup CHANGES is the
               room's own account of what reached it, in the order it reached it. The
               final table below is the answer, and it is printed whatever happened: a
               gate that could only read edges would pass a room that changed and changed
               back again. */
            NmSetup hs;
            SK_Lobby hlob;
            SK_State tmp;
            unsigned hscen = 0, t0;
            unsigned char seen_c[NM_MAX_SEATS], seen_s[NM_MAX_SEATS];
            int i;
            sk_init(&tmp, &g_skirmishMaps[0], (int)g_skirmishMaps.size(), NULL);
            tmp.sel = mp_map_index(LOBBYSEAT_MAP);
            sk_result(&tmp, &hlob);
            if (!mp_build_setup(&hs, &hlob, NULL, base_dir(opt.dir), &hscen)) {
                fprintf(stderr, "LOBBYSEAT|FAIL|host|could not describe %s\n", LOBBYSEAT_MAP);
                lsrc = 1;
            } else {
                /* THE ROOM IS NARROWED TO LOBBYSEAT_ROOM SEATS, which is the same room a
                   host makes by setting the seats it does not want to BLOCK. See the
                   constant: an eight seat room has every colour spoken for. */
                hs.seats = (unsigned char)LOBBYSEAT_ROOM;
                for (i = LOBBYSEAT_ROOM; i < NM_MAX_SEATS; i++) {
                    hs.mode[i] = NM_SEAT_BLOCK;
                    hs.is_ai[i] = 0;
                }
                if (!nm_lobby_host(NM_PORT_DEFAULT, &hs, mp_brain_abi(opt.dylib), hscen,
                                   LOBBYSEAT_ROOM, "G211", NULL)) {
                    fprintf(stderr, "LOBBYSEAT|FAIL|host|%s\n", nm_lobby_error());
                    lsrc = 1;
                } else {
                    printf("LOBBYSEAT|host|open|map=%s|seats=%d\n", hs.scenario,
                           (int)hs.seats);
                    fflush(stdout);
                    /* 0xFF is not a colour and not a start, so the first pass through the
                       loop prints the whole table as the room opened it. That baseline is
                       evidence too: it is what says seat 1 began on colour 1 with no
                       start, which is what makes the picks below non-defaults. */
                    for (i = 0; i < NM_MAX_SEATS; i++) { seen_c[i] = 0xFF; seen_s[i] = 0xFF; }
                    t0 = SDL_GetTicks();
                    while (SDL_GetTicks() - t0 < (unsigned)g_lobbyHostMs) {
                        const NmSetup* ns;
                        nm_lobby_poll();
                        ns = nm_lobby_setup();
                        for (i = 0; i < NM_MAX_SEATS; i++) {
                            if (ns->colour[i] == seen_c[i] && ns->start[i] == seen_s[i])
                                continue;
                            seen_c[i] = ns->colour[i];
                            seen_s[i] = ns->start[i];
                            printf("LOBBYSEAT|host|seat=%d|colour=%d|start=%d|mode=%d|taken=%d\n",
                                   i, (int)ns->colour[i],
                                   (ns->start[i] == CNC3D_START_RANDOM) ? -1 : (int)ns->start[i],
                                   (int)ns->mode[i], nm_lobby_seat_taken(i));
                            fflush(stdout);
                        }
                        SDL_Delay(10);
                    }
                    {
                        const NmSetup* ns = nm_lobby_setup();
                        for (i = 0; i < NM_MAX_SEATS; i++)
                            printf("LOBBYSEAT|hostfinal|seat=%d|colour=%d|start=%d|mode=%d\n",
                                   i, (int)ns->colour[i],
                                   (ns->start[i] == CNC3D_START_RANDOM) ? -1 : (int)ns->start[i],
                                   (int)ns->mode[i]);
                    }
                    printf("LOBBYSEAT|host|done\n");
                    fflush(stdout);
                    nm_lobby_cancel();
                }
            }
        } else {
            /* THE JOINER, AND THE REAL SCREEN. */
            DMS_LobbyProbe probe;
            DMS_LobbyStep script[LOBBYSEAT_STEPS];
            SK_Lobby jlob;
            const NmSetup* ns;
            unsigned t0;
            int seat = -1, n = 0, lr = 0;
            if (!nm_lobby_join("127.0.0.1", NM_PORT_DEFAULT, mp_brain_abi(opt.dylib),
                               mp_scen_hash(base_dir(opt.dir), LOBBYSEAT_MAP), NULL)) {
                fprintf(stderr, "LOBBYSEAT|FAIL|join|%s\n", nm_lobby_error());
                lsrc = 1;
            } else {
                /* SEATED FIRST, THEN THE SCREEN, and the order is not tidiness. The row
                   the script has to reach is the seat the HOST hands out, which nothing on
                   this machine knows until the first WELCOME lands. A script built before
                   that would click row 0 -- the host's own -- be refused for entirely the
                   right reason, and prove nothing while looking like it ran. */
                printf("LOBBYSEAT|join|asked\n");
                fflush(stdout);
                t0 = SDL_GetTicks();
                while (SDL_GetTicks() - t0 < (unsigned)g_lobbyJoinMs) {
                    const int ls = nm_lobby_poll();
                    if (nm_lobby_seat() >= 0) break;
                    if (ls == NM_LOBBY_REFUSED || ls == NM_LOBBY_FAILED) break;
                    SDL_Delay(10);
                }
                seat = nm_lobby_seat();
                if (seat < 1) {
                    /* SEAT 0 IS THE HOST'S AND -1 IS NOBODY'S: either way this process is
                       not a joiner and anything it did to a row would be a different
                       test. Said by name rather than measured as a silent zero. */
                    fprintf(stderr, "LOBBYSEAT|FAIL|join|never seated (seat=%d): %s\n",
                            seat, nm_lobby_error());
                    nm_lobby_cancel();
                    lsrc = 1;
                }
            }
            if (lsrc == 0) {
                ns = nm_lobby_setup();
                printf("LOBBYSEAT|join|seat=%d|was-colour=%d|was-start=%d"
                       "|want-colour=%d|want-start=%d\n",
                       seat, (int)ns->colour[seat],
                       (ns->start[seat] == CNC3D_START_RANDOM) ? -1 : (int)ns->start[seat],
                       LOBBYSEAT_COLOUR, LOBBYSEAT_START);
                fflush(stdout);
                /* THE SCRIPT, BUILT HERE RATHER THAN WRITTEN OUT, because the seat is only
                   now known. Three clicks and two Escapes: open my own row's drop down,
                   take a colour, take a start, shut the drop down, wait, leave. The first
                   Escape is its own small assertion -- if the drop down ever stopped being
                   modal it would leave the LOBBY instead and the run would end early with
                   no report at all. */
                memset(script, 0, sizeof script);
                script[n].item = SK_ROW_ITEM(seat);              script[n].fx = 500; script[n].fy = 500; n++;
                script[n].item = SK_COLOUR_ITEM(LOBBYSEAT_COLOUR); script[n].fx = 500; script[n].fy = 500; n++;
                script[n].item = SK_START_ITEM(LOBBYSEAT_START);  script[n].fx = 500; script[n].fy = 500; n++;
                script[n].key = SDLK_ESCAPE; n++;   /* shuts the drop down, NOT the lobby */
                /* THE SETTLE. F13 is a key dms_lobby_key does not translate, so each of
                   these costs a frame and changes nothing -- which is exactly what is
                   wanted: time, spent inside the real loop, with the real poll running. */
                while (n < LOBBYSEAT_STEPS - 1) { script[n].key = SDLK_F13; n++; }
                script[n].key = SDLK_ESCAPE; n++;   /* and now the lobby: the way out */
                memset(&probe, 0, sizeof probe);
                probe.script = script;
                probe.steps = n;
                memset(&jlob, 0, sizeof jlob);
                lr = dms_lobby_net(&menu, &g_skirmishMaps[0], (int)g_skirmishMaps.size(),
                                   skirmish_previews(), &jlob, &probe, 2);
                /* WHAT THE ROOM SAYS MY ROW IS, read after the screen has closed. This is
                   this peer's copy of the HOST's setup -- the last WELCOME it was sent --
                   so it is the host's answer coming back, and not this machine's opinion
                   of its own click. nm_lobby_cancel drops the socket and the seat on the
                   way out and deliberately leaves the setup alone, which is what makes it
                   readable from here. */
                ns = nm_lobby_setup();
                printf("LOBBYSEAT|join|left|rc=%d|seat=%d|colour=%d|start=%d|overflow=%d\n",
                       lr, seat, (int)ns->colour[seat],
                       (ns->start[seat] == CNC3D_START_RANDOM) ? -1 : (int)ns->start[seat],
                       probe.overflow);
                printf("LOBBYSEAT|join|done\n");
                fflush(stdout);
                if (probe.overflow != 0) lsrc = 1;
            }
        }
        dms_close(&menu);
        game_set_audio(NULL);
        audio_boot_shutdown(au);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return lsrc;
    }

    /* The campaign screens (side select, score, map). A missing campaign.pack
       degrades loudly: Start New Game then says what is missing and stays put. */
    Camp camp;
    int camp_ok = camp_open(&camp, win, au, "campaign.pack", mcfg.pack, err, sizeof err);
    if (!camp_ok)
        fprintf(stderr, "campaign: %s\n", err);

    if (g_matchShot) {
        /* Four commanders with unmistakably different numbers, so the sort, the bar
           lengths and the six ramps are all being exercised rather than merely drawn. */
        /* EIGHT, the worst case, on purpose: a shot of a two player game proves nothing
           about whether a full room still fits between the header and the panel's floor,
           and eight is what the lobby seats. */
        static const CampMatchRow SAMPLE[8] = {
            { "COMMANDER", 0, 41, 12,  7, 9200, 0, 0, 1 },
            { "VEGA",      2, 28,  6, 19, 6100, 0, 0, 0 },
            { "GREINER",   5, 22,  9, 12, 5400, 0, 0, 0 },
            { "MOEBIUS",   4, 17,  3, 25, 4100, 0, 0, 0 },
            { "NIKOOMBA",  3, 12,  1, 31, 2400, 0, 1, 0 },
            { "SEPH",      1,  4,  0, 44,  800, 0, 1, 0 },
            { "GIDEON",    6,  2,  0, 51,  300, 0, 1, 0 },
            { "COMPUTER",  7,  0,  0, 58,    0, 0, 1, 0 },
        };
        CampMatch cm;
        int i, r;
        memset(&cm, 0, sizeof cm);
        cm.nrows = 8; cm.win = 1; cm.seconds = 1182; cm.net = 1;
        /* --flowside nod shoots the Nod board, which is the only way to see that the
           emblem and the theme follow the player rather than the campaign. */
        cm.side = camp_autopilot_side ? 1 : 0;
        snprintf(cm.map, sizeof cm.map, "%s", "GREEN ACRES");
        for (i = 0; i < 8; i++) {
            cm.row[i] = SAMPLE[i];
            cm.row[i].score = cm.row[i].killed * 2 + cm.row[i].razed * 5
                            + cm.row[i].harvested / 10;
        }
        camp_autopilot = 1;
        r = camp_match_score(&camp, &cm, cm.side);
        printf("MATCHSHOT|rc=%d|rows=%d\n", r, cm.nrows);
        fflush(stdout);
        camp_close(&camp);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

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
        g_movieBound = 24;   /* and the in-mission one Restate's Video can play */

        menu.mx = DM_SCREEN_W / 2;
        menu.my = DM_SCREEN_H / 2;
        menu.st.pressed = -1;
        menu.st.selected = DM_SPECIAL;
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
                menu.st.selected = DM_SPECIAL;
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
                /* The Test Map is the last row of the Special Ops list now, so the
                   route is the button and then the list: this click, and the two keys
                   pushed where the list opens. Under --harnessload the button is Load
                   Mission and the answer is RETURN on its dialog, pushed now so the
                   dialog's own loop finds it waiting. */
                int bx, by, bw, bh;
                if (g_harnessLoad) {
                    dms_item_window_rect(&menu, DM_LOAD, &bx, &by, &bw, &bh);
                    printf("HARNESS|click|LOAD MISSION at window rect %d,%d %dx%d\n",
                           bx, by, bw, bh);
                    harness_click(bx + bw / 2, by + bh / 2);
                } else {
                    dms_item_window_rect(&menu, DM_SPECIAL, &bx, &by, &bw, &bh);
                    printf("HARNESS|click|SPECIAL OPS at window rect %d,%d %dx%d\n", bx, by, bw, bh);
                    harness_click(bx + bw / 2, by + bh / 2);
                }
            }

            static bool mpplay_clicked = false;
            if ((g_mpPlay || g_mpHost || g_mpJoin) && round == 0 && !mpplay_clicked) {
                /* ONCE, and the flag is why. --lobbyplay can guard on `round == 0` alone
                   because pressing Play leaves the menu for a game and the round moves on;
                   leaving the MULTIPLAYER screen comes straight back to the menu with the
                   round unchanged, so the same guard clicks it again for ever. That is a
                   harness that never ends, and it looks exactly like the freeze it was
                   written to find -- which is worth the two lines to not be fooled by. */
                mpplay_clicked = true;
                /* the rects only exist after a draw laid the menu out */
                menu.mx = DM_SCREEN_W / 2;
                menu.my = DM_SCREEN_H / 2;
                menu.st.pressed = -1;
                menu.st.selected = DM_MULTIPLAYER;
                dms_redraw(&menu);
                dms_draw(&menu);
                {
                    /* The menu, off the back buffer, before the swap: the reference the
                       multiplayer picture must DIFFER from. */
                    char path[512];
                    int w = 0, h = 0;
                    SDL_GL_GetDrawableSize(win, &w, &h);
                    snprintf(path, sizeof path, "%s/mpplay_menu.png", g_shotDir);
                    if (game_grab_png(path, w, h))
                        printf("MPPLAY|grab|menu|%s|%dx%d\n", path, w, h);
                }
                SDL_GL_SwapWindow(win);
                {
                    int bx, by, bw, bh;
                    dms_item_window_rect(&menu, DM_MULTIPLAYER, &bx, &by, &bw, &bh);
                    printf("MPPLAY|click|%s at %d,%d\n", dm_item_label(DM_MULTIPLAYER),
                           bx + bw / 2, by + bh / 2);
                    fflush(stdout);
                    dms_mp_autoleave((unsigned)g_mpPlay, mpplay_grab, win);
                    harness_click(bx + bw / 2, by + bh / 2);
                }
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
            /* STRAIGHT BACK TO THE BROWSER after a network match, without the main
               menu flashing past on the way. Consumed here so a second visit behaves
               normally. */
            int choice;
            if (g_backToBrowser) {
                g_backToBrowser = 0;
                choice = DM_MULTIPLAYER;
                printf("APP|menu|after-match|to-multiplayer\n");
            } else {
                choice = dms_run(&menu);
            }
            printf("APP|menu|choice=%d\n", choice);
            fflush(stdout);
            if (choice == DMS_QUIT || choice == DM_EXIT) {
                state = APP_DONE;
            } else if (choice == DM_SPECIAL) {
                /* Every entry below leaves skirmish OFF. The option persists across the
                   menu the way opt.pack does, and a mission started after a skirmish
                   would otherwise still be a skirmish. */
                opt.skirmish = 0;
                /* SPECIAL OPS. The list is built here rather than in the menu module
                   because this is the layer that may read a directory; dosops.c only
                   renders and hit-tests what it is handed. The Test Map is its last
                   row (see SPECOP_TESTMAP_SCEN), which is how the harness reaches it. */
                specops_scan(base_dir(opt.dir));
                if (g_specops.empty()) {
                    fprintf(stderr, "menu: no Special Ops missions found in '%s' -- the "
                                    "mission INIs and their packs have to be installed "
                                    "alongside the campaign's\n",
                            opt.dir ? opt.dir : "missions/");
                } else {
                    if (g_harnessTicks > 0) {
                        /* THE HARNESS PLAYS THE TEST MAP, which is the last row. The
                           two keys a hand would press: END to the last mission, RETURN
                           to play it. Pushed as events so the list's own loop handles
                           them, the same rule as harness_click on the menu. With
                           --harnessscen it walks to that row instead: HOME, then DOWN
                           once per mission row above it. */
                        SDL_Event k;
                        int downs = -1;
                        memset(&k, 0, sizeof k);
                        k.type = SDL_KEYDOWN;
                        k.key.state = SDL_PRESSED;
                        if (g_harnessScen) {
                            int n = 0;
                            for (size_t i = 0; i < g_specops.size(); i++) {
                                if (g_specops[i].header) continue;
                                if (g_specops[i].scen == g_harnessScen) { downs = n; break; }
                                n++;
                            }
                        }
                        if (downs >= 0) {
                            int d;
                            k.key.keysym.sym = SDLK_HOME;
                            SDL_PushEvent(&k);
                            k.key.keysym.sym = SDLK_DOWN;
                            for (d = 0; d < downs; d++) SDL_PushEvent(&k);
                            printf("HARNESS|keys|HOME, DOWN x%d, RETURN on the Special Ops "
                                   "list (%s)\n", downs, g_harnessScen);
                        } else {
                            k.key.keysym.sym = SDLK_END;
                            SDL_PushEvent(&k);
                            printf("HARNESS|keys|END RETURN on the Special Ops list\n");
                        }
                        k.key.keysym.sym = SDLK_RETURN;
                        SDL_PushEvent(&k);
                        fflush(stdout);
                    }
                    const int pick = dms_special(&menu, &g_specopsRows[0],
                                                 (int)g_specopsRows.size());
                    if (pick == DMS_QUIT) {
                        state = APP_DONE;          /* the window was closed */
                    } else if (pick == DMS_CANCEL) {
                        /* Backed out of the list. Fall through and the main menu is
                           drawn again on the next pass. This arm exists so that
                           "Cancel" can never be mistaken for "close the program", which
                           is what it was doing while both were -1. */
                    } else if (pick >= 0 && !g_specops[pick].header) {
                        /* Scenario and pack move TOGETHER, so neither can be left over
                           from a previous mission: every row names its own pack, and the
                           Test Map names the one it borrows (SCG01EA's). Setting only
                           the scenario once put GDI 1's object layout on whatever
                           terrain the last mission had loaded.
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
                        /* Not the campaign, and it has to be said: a campaign left
                           flagged active took the next mission's verdict as a campaign
                           verdict, played a win movie, scored it and advanced. */
                        g_camp.active = 0;
                        printf("APP|specops|play|%s|%s\n", pickscen, pickpack);
                        fflush(stdout);
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
            } else if (choice == DM_MULTIPLAYER) {
                /* MULTIPLAYER. The screen is menu/dosmp.c and the loop that drives it is
                   dms_multiplayer; this arm owns the ROUND TRIP, because pressing SET UP
                   MATCH bounces out to the skirmish lobby and comes back with the state
                   intact. mp lives across the whole visit for exactly that reason: the
                   game's name, its passcode and an open lobby with people already sitting
                   in it must all survive a trip through another screen. */
                static MP_State mp;
                static bool mp_ready = false;
                static SK_Lobby mplob;
                static bool mp_have_setup = false;
                if (!mp_ready) {
                    mp_init(&mp);
                    /* THE LIST'S SORT, ITS HIDE BOXES AND THE PLAYER'S NAME COME BACK, and
                       only for a player, for the reason the controls file gives: no gate
                       may inherit an order from a file somebody left in the run folder.
                       Beside the game, like cnc3d-controls.cfg, and never packaged. */
                    if (!automated)
                        dms_mp_remember("cnc3d-multiplayer.cfg", &mp);
                    mp_ready = true;
                }

                skirmish_scan(base_dir(opt.dir));
                for (;;) {
                    NmSetup ns;
                    NmSetup* nsp = NULL;
                    unsigned abi = 0, scen = 0;
                    /* THE ROOM OPENS ON A DEFAULT MATCH (there is no SET UP
                       MATCH; HOST goes straight to the multiplayer game screen, where the
                       map and every rule are set and pushed to the wire as they change).
                       The default is the lobby's own: the first map and its own rules. */
                    if (!mp_have_setup && !g_skirmishMaps.empty()) {
                        SK_State tmp;
                        sk_init(&tmp, &g_skirmishMaps[0], (int)g_skirmishMaps.size(), NULL);
                        sk_result(&tmp, &mplob);
                        mp_have_setup = (mplob.map >= 0);
                    }
                    if (mp_have_setup && mplob.map >= 0
                        && mplob.map < (int)g_skirmish.size()) {
                        nsp = mp_build_setup(&ns, &mplob, &mp, base_dir(opt.dir), &scen);
                        snprintf(mp.mapname, sizeof mp.mapname, "%s",
                                 g_skirmish[mplob.map].scen.c_str());
                    }
                    abi = mp_brain_abi(opt.dylib);
                    int r = DMS_CANCEL;
                    if (g_mpHost || g_mpJoin) {
                        /* THE HEADLESS LOBBY. Every call here is one the screens make;
                           the only thing missing is the drawing. */
                        const unsigned t0 = SDL_GetTicks();
                        int started = 0;
                        /* --mpscreenstart: the room is full and ready and START is the
                           lobby screen's to press, not this loop's. */
                        int handed = 0;
                        /* Zero means "not yet", and the first pass therefore publishes at
                           once rather than after a beat's silence. */
                        unsigned lastBeat = 0;
                        const int idx = mp_map_index("SCM01EA");
                        if (idx < 0) { fprintf(stderr, "MPLOBBY|FAIL|no SCM01EA\n"); state = APP_DONE; break; }
                        if (g_mpHost) {
                            SK_State tmp;
                            NmSetup hs;
                            unsigned hscen = 0;
                            sk_init(&tmp, g_skirmishMaps.empty() ? NULL : &g_skirmishMaps[0],
                                    (int)g_skirmishMaps.size(), NULL);
                            tmp.sel = idx;
                            sk_result(&tmp, &mplob);
                            /* THE ROOM IS AS WIDE AS THE MAP SEATS, left that way on
                               purpose: START waits for the people who are
                               HERE and nm_lobby_start closes the seats nobody took. This
                               used to block every seat past the joiner's or the room never
                               started, which was the fault and not the fix. */
                            /* ARMED BEFORE THE DOOR, because the door takes the request
                               and disarms it as its first statement. A host draws an id
                               that fits a room code; the host id is 0 because it IS the
                               host. */
                            if (g_mpRelay) {
                                const unsigned long rid = rc_draw_host_id();
                                if (rid == 0ul) {
                                    fprintf(stderr, "MPLOBBY|FAIL|host|no random number for a room code\n");
                                    state = APP_DONE; break;
                                }
                                nm_relay_next(g_mpRelay, g_mpRelayPort, rid, 0ul);
                            }
                            if (!mp_build_setup(&hs, &mplob, &mp, base_dir(opt.dir), &hscen)
                                || !nm_lobby_host(NM_PORT_DEFAULT, &hs, abi, hscen, 2, g_mpName, g_mpPass)) {
                                fprintf(stderr, "MPLOBBY|FAIL|host|%s\n", nm_lobby_error());
                                state = APP_DONE; break;
                            }
                            printf("MPLOBBY|host|open|%s\n", hs.scenario); fflush(stdout);
                            /* THE ROOM'S OWN CODE, read back off the network rather than
                               kept a second time here, so the two cannot drift. It is the
                               only thing a joiner needs. */
                            if (nm_is_relayed() && nm_room_code()[0]) {
                                printf("MPLOBBY|host|room=%s\n", nm_room_code()); fflush(stdout);
                            }
                            /* THE ADVERT, only when a person is expected: G205's joiner
                               comes by address and the gate keeps its silence. A relayed
                               room never advertises, because the LAN browser would carry
                               a port that nothing is listening on. */
                            if (g_mpWaitMs > 30000 && !nm_is_relayed()) nb_announce_open();
                        } else {
                            const unsigned jscen = mp_scen_hash(base_dir(opt.dir), "SCM01EA");
                            /* A ROOM CODE REPLACES THE ADDRESS, it does not accompany it:
                               a relayed joiner has nothing to resolve, because the code
                               names who to ask the relay for. */
                            const char* jaddr = "127.0.0.1";
                            /* AND WHICH PORT, because a direct row off the list names one
                               and it is not always the default: the line below used to
                               pass NM_PORT_DEFAULT whatever the row said, which is right
                               for a room code (there is no port at all) and wrong for
                               every direct row hosted anywhere else. */
                            unsigned short jport = NM_PORT_DEFAULT;
                            char jaddrbuf[MB_ADDR_MAX];
                            char picked[MB_ROOM_MAX];
                            /* PICK A GAME OUT OF THE PUBLIC LIST, which is the whole point
                               of there being one. Without this the list could only ever be
                               driven by a hand on a screen, and the one path that most
                               needs a gate would be the one path nothing could reach. */
                            if (g_mpFromList) {
                                MB_Row found[MB_MAX_ROWS];
                                int n, k, got = -1, waited;
                                mb_forget();
                                mb_refresh();
                                for (waited = 0; waited < 15000; waited += 50) {
                                    const int ph = mb_phase();
                                    if (ph == MB_READY || ph == MB_FAILED) break;
                                    SDL_Delay(50);
                                }
                                if (mb_phase() != MB_READY) {
                                    fprintf(stderr, "MPLOBBY|FAIL|join|the game list: %s\n",
                                            mb_error());
                                    state = APP_DONE; break;
                                }
                                n = mb_rows(found, MB_MAX_ROWS);
                                for (k = 0; k < n; k++) {
                                    /* The same judgement the screen makes: a full game and
                                       a game built against another order wire are both
                                       there to be seen and neither is there to be joined. */
                                    /* EITHER KIND OF ROW, which is what the list carries
                                       and what the screen's own JOIN has always handled.
                                       This loop took relayed rows only, so a direct game
                                       could be published and drawn and never joined from
                                       here -- and the one arrangement that needs no relay
                                       at all, and so is the only one a gate can run
                                       offline, was the one it refused. */
                                    if (found[k].relay ? !found[k].room[0]
                                                       : !found[k].addr[0]) continue;
                                    if (found[k].players_now >= found[k].players_max) continue;
                                    if (abi != 0 && found[k].abi != 0 && found[k].abi != abi)
                                        continue;
                                    got = k;
                                    break;
                                }
                                if (got < 0) {
                                    fprintf(stderr, "MPLOBBY|FAIL|join|no game in the list "
                                                    "this build can join (%d listed)\n", n);
                                    state = APP_DONE; break;
                                }
                                if (found[got].relay) {
                                    snprintf(picked, sizeof picked, "%s", found[got].room);
                                    g_mpRoom = picked;
                                    printf("MPLOBBY|join|from-list|%s|%s\n",
                                           found[got].name, picked);
                                } else {
                                    snprintf(jaddrbuf, sizeof jaddrbuf, "%s", found[got].addr);
                                    jaddr = jaddrbuf;
                                    if (found[got].port) jport = found[got].port;
                                    printf("MPLOBBY|join|from-list|%s|%s:%u\n",
                                           found[got].name, jaddr, (unsigned)jport);
                                }
                                fflush(stdout);
                            }
                            if (g_mpRoom) {
                                unsigned long room = 0ul;
                                if (!rc_decode(g_mpRoom, &room)) {
                                    fprintf(stderr, "MPLOBBY|FAIL|join|'%s' is not a room code. "
                                            "They look like #K7M-3QX.\n", g_mpRoom);
                                    state = APP_DONE; break;
                                }
                                nm_relay_next(g_mpRelay ? g_mpRelay : NM_RELAY_HOST,
                                              g_mpRelayPort, rc_draw_peer_id(), room);
                                jaddr = NULL;
                            }
                            if (!nm_lobby_join(jaddr, jport, abi, jscen, g_mpPass)) {
                                fprintf(stderr, "MPLOBBY|FAIL|join|%s\n", nm_lobby_error());
                                state = APP_DONE; break;
                            }
                            printf("MPLOBBY|join|asked\n"); fflush(stdout);
                        }
                        while (SDL_GetTicks() - t0 < (unsigned)g_mpWaitMs) {
                            const int ls = nm_lobby_poll();
                            /* AND NOT WHILE A PRESSED START IS IN THE AIR. Both lobby
                               screens learned this guard and this door did not, which is
                               the door the relayed gates drive: from the press until the
                               match begins netmatch seats nobody, so every row still
                               offered is a door that will not open. */
                            const bool mpOpen = !nm_lobby_start_pending() && !nm_lobby_starting();
                            if (g_mpHost && mpOpen && g_mpWaitMs > 30000 && !nm_is_relayed())
                                nb_announce(nm_lobby_name(), nm_lobby_setup()->scenario, NULL,
                                            NM_PORT_DEFAULT,
                                            nm_lobby_filled(), nm_lobby_wanted(),
                                            nm_lobby_locked(), 0, 0);
                            /* AND ONTO THE PUBLIC LIST, for a relayed room. The screen
                               does this from its own lobby; this path has no screen, so
                               it says the same thing here. It is what lets a whole
                               relayed game be opened, listed, found and joined without a
                               hand on the machine, which is the only way any of it can be
                               gated. */
                            const int pubRelayed = nm_is_relayed() && nm_room_code()[0];
                            if (g_mpHost && mpOpen && (pubRelayed || g_mpPublic)
                                && (lastBeat == 0
                                    || SDL_GetTicks() - lastBeat > MB_BEAT_MS)) {
                                MB_Row pub;
                                lastBeat = SDL_GetTicks();
                                memset(&pub, 0, sizeof pub);
                                snprintf(pub.name, sizeof pub.name, "%s", nm_lobby_name());
                                snprintf(pub.scenario, sizeof pub.scenario, "%s",
                                         nm_lobby_setup()->scenario);
                                snprintf(pub.map, sizeof pub.map, "%s",
                                         nm_lobby_setup()->scenario);
                                /* ONE OF THE TWO, NEVER BOTH: a relayed row carries a code
                                   and no address, a direct one a port the list pairs with
                                   the address it saw the beat come from. */
                                if (pubRelayed)
                                    snprintf(pub.room, sizeof pub.room, "%s", nm_room_code());
                                else
                                    pub.port = NM_PORT_DEFAULT;
                                pub.relay = pubRelayed ? 1 : 0;
                                pub.players_now = nm_lobby_filled();
                                pub.players_max = nm_lobby_wanted();
                                pub.locked = nm_lobby_locked();
                                pub.abi = nm_abi();
                                pub.scen = nm_scen();
                                mb_publish(&pub);
                            }
                            if (ls == NM_LOBBY_STARTED) { started = 1; break; }
                            if (ls == NM_LOBBY_REFUSED || ls == NM_LOBBY_FAILED) {
                                fprintf(stderr, "MPLOBBY|FAIL|%s\n", nm_lobby_error()); break;
                            }
                            if (g_mpHost) {
                                /* THE PEOPLE WHO ARE HERE, the same rule the screen's
                                   START now uses: an eight seat room does not wait for
                                   six players who are never coming. */
                                /* AND PRESSED AS OFTEN AS IT IS TRUE, which costs nothing:
                                   nm_lobby_start holds the press until every seat's round
                                   trip is in and ignores a press while one is held. The
                                   wait for the measurement used to live HERE, as a grace
                                   this loop counted for itself, which fixed this door and
                                   left both screens starting rooms on a lookahead nobody
                                   had measured. It lives behind the button now. */
                                if (nm_lobby_all_ready()) {
                                    if (g_mpScreenStart) { handed = 1; break; }
                                    nm_lobby_start();
                                }
                            } else if (nm_lobby_seat() >= 0 && !nm_lobby_my_ready()) {
                                nm_lobby_set_ready(1);
                                printf("MPLOBBY|join|seat=%d|ready\n", nm_lobby_seat()); fflush(stdout);
                            }
                            SDL_Delay(10);
                        }
                        if (handed) {
                            /* THE ROOM IS THE SCREEN'S NOW. Nothing above pressed START, so
                               the lobby screen below is the only thing that can, and its
                               button is the door under test. */
                            printf("MPLOBBY|host|screen-start|seated=%d\n", nm_lobby_filled());
                            fflush(stdout);
                            r = DMS_MP_LOBBY;
                        } else {
                            if (!started) {
                                fprintf(stderr, "MPLOBBY|FAIL|the room never started in %d s\n", g_mpWaitMs / 1000);
                                if (g_mpWaitMs > 30000 && !nm_is_relayed()) nb_announce_close();
                                mb_unpublish();
                                nm_lobby_cancel(); state = APP_DONE; break;
                            }
                            /* A GAME THAT HAS STARTED IS NOT AN OPEN GAME. Leaving it listed
                               offers every browser a row that refuses everyone who clicks it. */
                            mb_unpublish();
                            /* BOTH SIDES BOOT FROM THE SETUP THE ROOM AGREED ON, including
                               the host. Letting the host boot from its own screen instead was
                               the first desync this gate caught: the screen's AI count was its
                               default while the setup on the wire said two humans and no
                               computers, so the host made three players and the joiner two,
                               and frame 0 already differed. There is one description of a
                               match and it is the one that travelled. */
                            if (!mp_lobby_from_match(&mplob)) {
                                fprintf(stderr, "MPLOBBY|FAIL|the agreed map is not here\n");
                                nm_lobby_cancel(); state = APP_DONE; break;
                            }
                            printf("MPLOBBY|started|seat=%d|host=%d|map=%s\n", nm_lobby_seat(),
                                   nm_lobby_is_host(), g_skirmish[mplob.map].scen.c_str());
                            fflush(stdout);
                            mp_have_setup = true;
                            /* r, not a goto: DMS_MP_PLAY falls through every arm below to the
                               mp_play label on its own, and a goto from here would jump over
                               the declarations those arms make -- which C++ refuses and which
                               is the right refusal. */
                            r = DMS_MP_PLAY;
                        }
                    } else {
                        r = dms_multiplayer(&menu, &mp, nsp, abi, scen);
                    }
                    {
                        if (g_mpPlay) {
                            /* The surface, for the record, then out: a harness that came
                               back to the menu and sat there waiting for a hand would
                               look exactly like the freeze it exists to catch. */
                            char path[512];
                            snprintf(path, sizeof path, "%s/mpplay_surface.png", g_shotDir);
                            dms_write_shot(&menu, path);
                            state = APP_DONE;
                            break;
                        }
                        if (r == DMS_QUIT) { state = APP_DONE; break; }
                        if (r == DMS_CANCEL) break;
                        if (r == DMS_MP_LOBBY) {
                            /* A ROOM IS OPEN. The lobby screen is the waiting room: the
                               same map list, the same rules, the same roster, refused for
                               a joiner everywhere except its own seat. */
                            SK_Prev* prev = skirmish_previews();
                            const int net = nm_lobby_is_host() ? 1 : 2;
                            /* --mpscreenstart: the one scripted press, on the host only. */
                            DMS_LobbyProbe sprobe;
                            const int scripted = g_mpScreenStart && g_mpHost && net == 1;
                            memset(&sprobe, 0, sizeof sprobe);
                            if (scripted) {
                                int k;
                                memset(g_mpScreenScript, 0, sizeof g_mpScreenScript);
                                g_mpScreenScript[0].item = SK_I_PLAY;
                                g_mpScreenScript[0].fx = 500;
                                g_mpScreenScript[0].fy = 500;
                                for (k = 1; k <= MP_SCREEN_IDLE; k++)
                                    g_mpScreenScript[k].key = SDLK_F13;
                                g_mpScreenScript[MP_SCREEN_IDLE + 1].key = SDLK_ESCAPE;
                                sprobe.script = g_mpScreenScript;
                                sprobe.steps = MP_SCREEN_IDLE + 2;
                            }
                            const int lr = dms_lobby_net(&menu,
                                                         g_skirmishMaps.empty()
                                                             ? NULL : &g_skirmishMaps[0],
                                                         (int)g_skirmishMaps.size(), prev,
                                                         &mplob, scripted ? &sprobe : NULL,
                                                         net);
                            if (lr == DMS_QUIT) { state = APP_DONE; break; }
                            if (scripted && lr != DMS_MP_PLAY) {
                                fprintf(stderr, "MPLOBBY|FAIL|START GAME on the lobby screen "
                                                "never started the room (rc=%d)\n", lr);
                                nm_lobby_cancel(); state = APP_DONE; break;
                            }
                            if (scripted) {
                                printf("MPLOBBY|started|seat=%d|host=%d|door=screen\n",
                                       nm_lobby_seat(), nm_lobby_is_host());
                                fflush(stdout);
                            }
                            if (lr == DMS_MP_PLAY) {
                                mp_have_setup = true;
                                goto mp_play;
                            }
                            /* Cancelled or refused: the room is closed, back to the tabs,
                               and a refusal or a timeout goes with it onto the browser's
                               status line, by name. A plain CANCEL leaves no error. */
                            if (nm_lobby_error() && nm_lobby_error()[0])
                                snprintf(mp.status, sizeof mp.status, "%s", nm_lobby_error());
                            continue;
                        }
                        if (r == DMS_MP_PLAY) {
                        mp_play:
                            /* The lobby armed the match. Everything the engine needs is
                               already decided, so this is the skirmish path with the net
                               match already live: net_match_prepare sees nm_active() and
                               leaves the handshake alone -- and boot_brain throws the
                               lockstep switch itself on that path (net_arm_lockstep). */
                            if (!nm_active() || mplob.map < 0
                                || mplob.map >= (int)g_skirmish.size()) {
                                /* Two refusals, both of which used to be undefined
                                   behaviour: a play with no live match would boot with
                                   net_mode set and no address; a play with no map would
                                   index g_skirmish past its end. */
                                fprintf(stderr, "menu: refusing to start a match with %s\n",
                                        nm_active() ? "no map chosen" : "no room open");
                                nm_lobby_cancel();
                                nb_announce_close();
                                continue;
                            }
                            static char mpscen[16], mppack[24], mpdir[512];
                            snprintf(mpscen, sizeof mpscen, "%s",
                                     g_skirmish[mplob.map].scen.c_str());
                            snprintf(mppack, sizeof mppack, "%s",
                                     g_skirmish[mplob.map].pack.c_str());
                            snprintf(mpdir, sizeof mpdir, "%s", base_dir(opt.dir));
                            opt.scen = mpscen;
                            opt.pack = mppack;
                            opt.dir = mpdir;
                            opt.reskin = 0;
                            opt.skirmish = 1;
                            opt.net_mode = nm_lobby_is_host() ? 1 : 2;
                            opt.net_players = nm_lobby_wanted();
                            skirmish_apply(&opt, &mplob);
                            /* THIS MACHINE'S OWN SEAT. skirmish_apply takes the side from
                               the roster's seat 0, which is the HOST's; a joiner is in
                               another seat and plays that seat's house, exactly as the
                               command-line handshake sets it (net_match_prepare's joiner
                               arm). Without this every joiner booted as seat 0's house
                               and two peers ran two different matches from tick one. */
                            if (!nm_lobby_is_host() && nm_lobby_setup()
                                && nm_lobby_seat() >= 0 && nm_lobby_seat() < NM_MAX_SEATS)
                                opt.side = nm_lobby_setup()->house[nm_lobby_seat()];
                            g_camp.active = 0;
                            state = APP_GAME;
                            break;
                        }
                    }
                }
            } else if (choice == DM_VISUALS) {
                /* The screen itself lives in the renderer: it is the same dialog the
                   pause menu walks to, so there is one Visuals page in the program and
                   not two that can drift. */
                if (game_visuals_open(win, opt.dospack, NULL))
                    state = APP_DONE;
            } else if (choice == DM_LOAD) {
                /* LOAD MISSION. The slot dialog is the renderer's (the same one the
                   pause menu opens), run on this window; the slot it names is booted
                   through load_slot_prepare, which is also what a pause-dialog load of
                   another mission's slot goes through. */
                if (g_harnessTicks > 0 && g_harnessLoad) {
                    /* RETURN on the dialog loads its first row, the newest save. Pushed
                       HERE, after the menu loop has returned, because that loop drains
                       the whole queue before it acts on a click and would have eaten a
                       key pushed beside it. */
                    SDL_Event k;
                    memset(&k, 0, sizeof k);
                    k.type = SDL_KEYDOWN;
                    k.key.state = SDL_PRESSED;
                    k.key.keysym.sym = SDLK_RETURN;
                    SDL_PushEvent(&k);
                    printf("HARNESS|keys|RETURN on the Load Mission dialog\n");
                    fflush(stdout);
                }
                const int sl = game_load_menu_open(win, opt.dospack, NULL);
                if (sl == -2) {
                    state = APP_DONE;                 /* the window was closed */
                } else if (sl >= 0) {
                    if (load_slot_prepare(&opt, sl))
                        state = APP_GAME;
                }
            } else {
                /* Anything else the DOS menu drew and this build has not built puts
                   you back where you were rather than pretending. */
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
            printf("HARNESS|rss|round=%d|before-boot=%ld KiB|footprint=%ld KiB|resident=%ld KiB\n",
                   round, heap_kib(), phys_kib(), res_kib());

        opt.forcewin_ticks = g_flowTest;
        /* THE CAMPAIGN POSITION GOES DOWN WITH THE MISSION, so a save made in it
           records where the campaign has got to, and comes back up after the mission
           so a load can move it. Both calls existed for as long as the save layer has;
           neither was made, and every slot recorded a campaign of nothing. */
        game_set_campaign(g_camp.active, g_camp.side, g_camp.scenario, g_camp.dir,
                          g_camp.var);
        if (!game_boot(win, &opt)) {
            /* A REFUSED START SAYS WHY, in the log for the harness and on the glass for
               the player. Before this the menu simply came back, which reads as a Play
               button that does nothing, and the report that describes it cannot be
               worked. The sentence is the boot path's own (game_boot_refusal); the
               shell only carries it. Automated runs never open a window that waits for
               a hand, which is the same rule every other modal on this shell obeys. */
            const char* why = game_boot_refusal();
            fprintf(stderr, "app: the mission failed to start; back to the menu\n");
            printf("BOOTFAIL|%s|%s\n", opt.scen ? opt.scen : "", why);
            fflush(stdout);
            g_harnessFails++;
            opt.load_slot1 = 0;
            if (!shell_automated()) {
                if (game_notice_open(win, opt.dospack, "Unable to start mission", why,
                                     NULL) == -2) {
                    state = APP_DONE;
                    continue;
                }
            }
            state = APP_MENU;
            continue;
        }
        /* Consumed by the boot. A Restart from the pause dialog boots the scenario
           again from the beginning, which is what 1995's Do_Restart does. */
        opt.load_slot1 = 0;

        if (g_harnessTicks > 0)
            printf("HARNESS|rss|round=%d|after-boot=%ld KiB|footprint=%ld KiB|resident=%ld KiB\n",
                   round, heap_kib(), phys_kib(), res_kib());

        /* A LOBBY MATCH GIVEN --script IS PLAYED BY THE SCRIPT, not by the tick bound. A
           match that only ticks gives no orders, so the world hashes it compares belong to
           two worlds nobody touched, and agreeing proves only that both sides booted the
           same map. game_run_script is the function the command-line net gates play their
           scripts through, so an order given here crosses the same wire a click does, and
           the script's own quit ends the match. */
        const int reason = ((g_mpHost || g_mpJoin) && opt.script)
                               ? (game_run_script(win, &opt) ? GAME_EXIT_ERROR : GAME_EXIT_MENU)
                               : game_loop(win, &opt);
        printf("APP|game|round=%d|reason=%d\n", round, reason);
        {
            /* The other half of the campaign position: a slot loaded in the mission
               carries the position it was saved at, and the verdict below must judge
               that campaign, not the one this round was started as. */
            int ca = 0, cs = 0, cn = 0, cd = 0, cv = 0;
            game_get_campaign(&ca, &cs, &cn, &cd, &cv);
            g_camp.active = ca;
            g_camp.side = cs;
            g_camp.scenario = cn;
            g_camp.dir = cd ? (char)cd : 'E';
            g_camp.var = cv ? (char)cv : 'A';
        }
        if (g_mpHost || g_mpJoin) {
            /* The match the room armed has been played for its tick bound. Coming back to
               the menu and waiting for a hand is what --mpplay's first draft did, and it
               looks exactly like a hang to the gate that is timing it. */
            /* AND THE MATCH IS CLOSED BEFORE THE PROCESS STOPS. The `continue` below jumps
               over game_shutdown, which is the one place that closes the match down, so
               this door left without saying goodbye to anybody: the other peers waited out
               the full thirty second silence instead of being told, and the line that says
               what the match cost and how often a stuck turn had to be asked for and
               answered was never printed on the door every relayed run uses. */
            nm_shutdown();
            printf("MPLOBBY|done|reason=%d\n", reason);
            fflush(stdout);
            state = APP_DONE;
            continue;
        }
        if (g_harnessTicks > 0)
            printf("HARNESS|rss|round=%d|after-loop=%ld KiB|footprint=%ld KiB|resident=%ld KiB\n",
                   round, heap_kib(), phys_kib(), res_kib());

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
        /* And so must the match's table, for exactly the same reason and one more: the
           renderer fills it INSIDE game_shutdown, at the last moment the engine still
           holds the numbers, so this copy has to be taken after that call rather than
           before it. */
        MatchStats mstats;

        game_shutdown();
        mstats = *game_match_stats();

        /* THE SAMPLE THE LIMIT IS ASSERTED ON IS TAKEN HERE, AFTER THE TEARDOWN, and it
           used to be taken before it, the moment game_loop returned. The question this
           gate asks is "what did the process KEEP once the mission was torn down", and a
           sample taken with the mission still fully loaded answers a different one: it
           carries every allocation the match itself was holding, which varies with how
           the match went, and buries the thing being looked for under that variation.
           The block's own comment claimed the shutdown position all along. */
        if (g_harnessTicks > 0) {
            const long kept = heap_kib();
            printf("HARNESS|rss|round=%d|after-shutdown=%ld KiB|footprint=%ld KiB"
                   "|resident=%ld KiB\n", round, kept, phys_kib(), res_kib());
            if (round >= 1 && round <= RSS_MAX_ROUNDS)
                g_rssAfterRound[round - 1] = kept;
            /* Printed, not asserted on: one cycle against the one before it is inside its
               own noise either way round. The assertion is the trend across the whole
               run, at the end of this function. */
            if (round >= 2 && round <= RSS_MAX_ROUNDS)
                printf("HARNESS|rss|round=%d|growth=%ld KiB over round %d (live heap)\n",
                       round, kept - g_rssAfterRound[round - 2], round - 1);
        }

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
        else if (reason == GAME_EXIT_NETLOST) {
            /* A MATCH THAT BROKE STILL ENDS ON THE DEBRIEF. Until 7 Sep 2026 a desync or
               a peer that went quiet came back as GAME_EXIT_ERROR and took this branch's
               neighbour: rc = 1 and APP_DONE, which is the application closing. The
               player had been playing for twenty minutes, the window vanished off the
               desktop, and the only account of it was a line in a log file they have to
               be told how to find. It reads as a crash because it is indistinguishable
               from one.

               Same screen as every other ending, with the reason on it, and then the
               menu -- so the next thing a player does is host another game rather than
               find the icon again. rc stays 0: the game did not fail, a match did. */
            /* The line itself is printed by the renderer, at the point the decision is
               made, so cnc_eyes and cnc3d say the same thing once each rather than the
               app saying it a second time. */
            g_camp.active = 0;
            if (!app_match_score(&camp, &mstats))
                state = APP_DONE;
            else
                state = APP_MENU;
        }
        else if (reason == GAME_EXIT_LOADSLOT) {
            /* LOAD MISSION named another mission's slot: boot that mission with the
               slot to load, exactly as the main menu's Load Mission does. */
            const int sl = game_pending_load_slot();
            if (sl >= 0 && load_slot_prepare(&opt, sl)) {
                state = APP_GAME;
            } else {
                fprintf(stderr, "app: the slot to load (%d) could not be described; back "
                                "to the menu\n", sl);
                g_camp.active = 0;
                state = APP_MENU;
            }
        }
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
            /* A MATCH ENDS ON ITS OWN SCREEN. Same plate, same medallion, same music as
               the campaign's, with a table of what every commander did instead of a
               tally against a designer's par. The Test Map has no table and still just
               goes back to the menu. */
            g_camp.active = 0;
            if (!app_match_score(&camp, &mstats))
                state = APP_DONE;
            else
                state = APP_MENU;
        }
        else {
            /* ANY other way out of a mission -- Abort from the pause dialog above all --
               ends the campaign run. This used to fall through with g_camp.active still
               set, so the campaign stayed "active" for the rest of the session and the
               next mission's verdict, Test Map included, was processed as a campaign
               verdict. */
            g_camp.active = 0;
            /* LEAVE MATCH comes out this door, and a commander who sat and watched the
               rest of a match has as much right to the debrief as one who was still
               standing at the end of it. The table is empty for a campaign abort, and
               app_match_score answers at once when it is. */
            if (!app_match_score(&camp, &mstats))
                state = APP_DONE;
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

    /* THE LEAK ASSERTION IS A TREND ACROSS THE WHOLE RUN, NOT ONE ROUND AGAINST THE ONE
       BEFORE IT, and that is the second half of why this gate used to go red at random.
       One boot/shutdown cycle's live heap moves by several MB in either direction for
       reasons that have nothing to do with leaking -- how much audio was buffered when
       the sample was taken, how much the screenshot path happened to be holding -- so a
       single difference is a coin toss. Measured on this build, per-cycle differences ran
       from -6876 to +6135 KiB with no leak in the code at all.

       So the early rounds are averaged and the late rounds are averaged, and the gate
       asks how far the floor moved between them. Measured over sixteen-round runs of two
       builds, one carrying the three pack leaks and one with them freed:

           leaking    +16003   +23433 KiB
           clean        -114     +954   -2473 KiB

       THE ROUND COUNT IS THE MEASUREMENT, and eight rounds was not enough. At eight the
       same clean build drifted -1817, +1497 and +6762 KiB over three runs, and the top of
       that spread sits inside the range a leaking build gives, so no limit in it could
       tell the two apart. The noise does not grow with the round count and a leak does:
       by sixteen rounds the clean spread is about 3 MB wide while a leak of roughly 2 MB
       a mission has moved 16 MB or more. A third of the rounds at each end, so the two
       samples are independent and the middle is thrown away; it needs six rounds to mean
       anything and says so rather than asserting on two. */
    if (g_harnessTicks > 0 && round >= 6 && round <= RSS_MAX_ROUNDS) {
        const int k = round / 3;
        long early = 0, late = 0;
        for (int i = 0; i < k; i++) early += g_rssAfterRound[i];
        for (int i = round - k; i < round; i++) late += g_rssAfterRound[i];
        early /= k;
        late  /= k;
        printf("HARNESS|rss|trend|first %d round(s) %ld KiB -> last %d round(s) %ld KiB"
               "|drift=%ld KiB over %d cycles (live heap, limit %d)\n",
               k, early, k, late, late - early, round, RSS_GROWTH_LIMIT_KIB);
        if (late - early > RSS_GROWTH_LIMIT_KIB) {
            printf("HARNESS|FAIL|the live heap after a mission is torn down is %ld KiB "
                   "higher at the end of %d boot/shutdown cycles than at the start "
                   "(limit %ld KiB): something a mission allocates is not being given "
                   "back\n", late - early, round, (long)RSS_GROWTH_LIMIT_KIB);
            g_harnessFails++;
        }
    } else if (g_harnessTicks > 0 && round < 6) {
        printf("HARNESS|rss|trend|not asserted: %d round(s) is too few, six are needed "
               "for the early and late averages to mean anything\n", round);
    }

    if (g_harnessTicks > 0) {
        printf("HARNESS|end|%d round(s), %d failure(s)\n", round, g_harnessFails);
        if (g_harnessFails) rc = 1;
    }
    return rc;
}
