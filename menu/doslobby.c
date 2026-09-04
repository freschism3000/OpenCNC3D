/* doslobby.c -- see doslobby.h. Renders and hit-tests; owns nothing else. */

#include "doslobby.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CC_BRIGHT_GREEN, defines.h:2886. The underline under a caption, the tick inside a
 * check box and the travelled part of a gauge are all this one colour, and it is the
 * one green dosmenu.h never needed. */
#define SK_BRIGHT_GREEN 167

/* THE EIGHT PLAYER COLOURS, IN THE ENGINE'S OWN ORDER, DERIVED FROM THE ART.
 *
 * WHAT THIS TABLE USED TO BE, and why it was a bug worth a whole comment: eight indices
 * picked for how they looked on this plate, in an order of their own -- yellow, red,
 * white, cyan, grey, orange, green, brown. The ENGINE's eight are a different set in a
 * different order (MPlayerGColors, globals.cpp:469: yellow, blue-grey, red, green,
 * orange, blue-green, grey, brown). While nobody could CHOOSE a colour that only meant
 * the lobby drew seat 1 red and the match played it blue-grey, which was tolerable
 * because a seat index is not a promise. It stops being tolerable the moment a player
 * points at a square, and that is what this screen now lets them do. So the table below
 * IS the engine's colour n for every n, and a chosen index means one thing on both
 * sides of the handoff.
 *
 * WHERE EACH NUMBER COMES FROM. Not from the enum's names, which lie about themselves
 * (defines.h:707-718 annotates REMAP_LTBLUE "Ingame grey color" and REMAP_BLUE "Ingame
 * dark green color"), and not from MPlayerGColors either, which is a set of legible
 * RADAR and TEXT inks rather than a description of the paint. From the ART:
 *
 *   1. The unit and building art is authored in the GOLD house band, palette indices
 *      176..191 -- which is exactly the range every one of the eight remap tables in
 *      const.cpp rewrites and the only range any of them touches.
 *   2. 176..183 is the lit ramp and 184..191 the shadowed one. Index 178 is the third
 *      step of the lit ramp: the body colour of a vehicle, past the specular top of the
 *      ramp (176 remaps to a near-white highlight for half the schemes) and well above
 *      the near-black bottom.
 *   3. So colour n's true paint is Remap<n>[178] read in the GAME palette
 *      (TEMPERAT.PAL), and this screen runs on the MENU's palette (TITLE.CPS), so each
 *      of those RGBs is then matched to the nearest index THAT palette carries.
 *
 * Six of the eight are the identical index in both palettes and need no matching at all;
 * the two that differ are named below with the distance. Three of the eight land exactly
 * on the engine's own MPlayerGColors, which is the cross-check: where the engine picked
 * its radar ink out of the same band, the two derivations agree.
 *
 * menu/tools does the arithmetic; the numbers are pasted here rather than computed at
 * runtime because this module reads no files and owns no palette. */
static const unsigned char SK_PLAYER_COLOUR[8] = {
    /* n  remap table   band 178 ->  game RGB        menu index                        */
    178, /* 0 RemapGold    -> 178   199,170, 93   same index in both palettes          */
    201, /* 1 RemapLtBlue  -> 201   195,195,211   same index in both palettes          */
    125, /* 2 RemapRed     -> 125   199, 40, 20   same index in both palettes          */
    167, /* 3 RemapGreen   -> 166   162,227, 28   menu 167 = 142,203,8, off by 37      */
    26,  /* 4 RemapOrange  ->  26   215,121, 16   same index; == MPlayerGColors[4]     */
    216, /* 5 RemapBlue    -> 118   101,130,138   menu 216 = 113,121,130, off by 17    */
    194, /* 6 RemapGrey    -> 194   162,162,162   same index; == MPlayerGColors[6]     */
    235  /* 7 RemapBrown   -> 209   170,113, 77   menu 235 = 166,113,81, off by 6      */
};
#define SK_PLAYER_COLOUR_N ((int)(sizeof SK_PLAYER_COLOUR / sizeof SK_PLAYER_COLOUR[0]))

unsigned char sk_colour_index(int colour)
{
    if (colour < 0 || colour >= SK_PLAYER_COLOUR_N)
        colour = 0;
    return SK_PLAYER_COLOUR[colour];
}

/* An unused start position: drawn, so the panel shows how much room the map has, and
 * clearly not anybody's. The rim is the baker's own (index 12, opaque black). */
#define SK_START_RIM 12
#define SK_START_IDLE 13

/* EVERY LINE THE STATUS ROWS CAN SAY, in one table, because sk_check_layout measures
 * this table against the width of the row it prints into. A status line that runs off
 * the dialog is exactly the bug db_print cannot save anyone from, and a line written
 * inline could not be measured without being printed first.
 *
 * SK_ST_FOOT is the second row and it never changes: it is the one fact about this
 * screen the picture cannot show. A roster colour names a player in the lobby and
 * nowhere else -- the cartridge carries two house texture sets, both armies take theirs
 * from the SIDE, so two computers on one side look alike on the field. */
enum
{
    SK_ST_FOOT = 0,
    SK_ST_GDI,
    SK_ST_NOD,
    SK_ST_BASES,
    SK_ST_TIBERIUM,
    SK_ST_SEATED,
    SK_ST_ONE_AI,
    SK_ST_SUPER_ON,
    SK_ST_SUPER_OFF,
    SK_ST_CRATES_ON,
    SK_ST_CRATES_OFF,
    SK_ST_COL_HUMAN,
    SK_ST_NO_MAPS,
    SK_ST_UNITS_RISK,
    SK_ST_COUNT
};

static const char *const SK_STATUS[SK_ST_COUNT] = {
    "Each player's army wears that player's colour.",
    "You play GDI; every computer plays Nod.",
    "You play Nod; every computer plays GDI.",
    "Bases stay on: the match would end in a second.",
    "Tiberium stays on: off leaks into the campaign.",
    "Opponents filled to the map; drag for fewer.",
    "This map has room for a single opponent.",
    "Superweapons may be built by every house.",
    "No house may build a superweapon.",
    "Crates are scattered; drive over one to open it.",
    "No crates are scattered on the battlefield.",
    /* THE ONE SQUARE AN AI MAY NOT BE GIVEN. Taking a colour off another computer moves
     * that computer; taking one off the PLAYER would move the player, as a side effect
     * of a change they made to somebody else's row, and that is the one thing this
     * picker must never do. So the player's own square is drawn locked on every other
     * row and says so instead of being silently inert. */
    "That colour is the player's own.",
    /* THE ONE LINE ON THIS SCREEN A PLAYER MAY ACTUALLY NEED. With nothing installed
     * the list says NO MAPS INSTALLED in 102 pixels, which is room for the symptom and
     * none for the remedy, so the remedy goes in the status row where there are 272.
     * It names the FILES rather than the symptom: a folder holding the mission INIs
     * and no packs looks complete to anyone who has not been told what to look for. */
    "No SCM map packs here. Reinstall the game.",
    /* THE GAUGE THAT DAMAGES THE OPENING MINUTE, and the reason it says so rather than
       being capped. docs/design-skirmish-lobby.md 7.5 swept all nine maps at both start
       positions: 0 leaves 17 of 18 starts deployable, 1 leaves 10, 2 leaves 9, 5 leaves
       4, and 6 or more leaves NONE. The engine scatters the escort within about four
       cells of the MCV and a unit standing inside the Construction Yard's 3x3 pad makes
       the player's first click do nothing. That doc ruled the control out of v1 and said
       that if it were ever wanted it belonged behind the same kind of warning the Bases
       box gets; the slider was then asked for, so this is that warning.
       It is a WARNING and not a cap: the range the engine offers is the range the player
       gets, and the risk is stated rather than taken away.
       KEEP IT UNDER 272 PIXELS. The first draft of this line was 52 characters and
       measured 294, and the lobby's own fit check caught it before a human did:
       LOBBY|OVERFLOW|status|294 > 272. The status row is the widest text on this screen
       and it is still a budget. */
    "Extra units can block your MCV. 0 is safest."
};

static const char *sk_status_side(int side)
{
    return SK_STATUS[side ? SK_ST_NOD : SK_ST_GDI];
}

/* ------------------------------------------------------------------------ *
 * mappreview.pack.
 *
 * Same shape as db_pack_load: read the file once into one allocation, check the magic
 * and the version, and then keep POINTERS INTO THE BLOB rather than copying anything
 * out of it. Records are a fixed size on purpose, so the n-th one is at a computable
 * offset and finding a scenario is arithmetic plus a compare.
 *
 *   char  magic[8]      "MAPPREV1"
 *   u32   version, count, frame_w, frame_h, max_starts
 *   u8    pal6[768], pal8[768]     the menu's own palette, both forms
 *   count x {
 *       char scenario[12], name[24], theater[12]
 *       u16  cell_x, cell_y, cell_w, cell_h
 *       u16  img_x, img_y, img_w, img_h
 *       u16  starts, tiberium
 *       max_starts x { u16 cell_x, cell_y, px, py; u8 colour, reserved }
 *       u8   plain [frame_w * frame_h]     terrain only
 *       u8   marked[frame_w * frame_h]     the same with start diamonds drawn on
 *   }
 *
 * All little-endian, read byte by byte so the reader does not care what this machine
 * is. Everything below refuses rather than guesses: a file that is one byte short of
 * what its own header describes is not a preview pack.
 * ------------------------------------------------------------------------ */

#define SK_PREV_HDR 1564L /* 8 + 5*4 + 768 + 768 */
#define SK_PR_SCEN 0
#define SK_PR_NAME 12
#define SK_PR_THEATER 36
#define SK_PR_STARTS 64
#define SK_PR_MARKS 68
#define SK_PR_MARK_BYTES 10

struct SK_Prev
{
    unsigned char *blob;
    long bytes;
    int count;
    int fw, fh;
    int max_starts;
    long rec; /* bytes per record */
};

static unsigned long sk_rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static int sk_rd16(const unsigned char *p)
{
    return (int)p[0] | ((int)p[1] << 8);
}

static void sk_err(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0) {
        strncpy(err, msg, (size_t)errlen - 1);
        err[errlen - 1] = 0;
    }
}

SK_Prev *sk_prev_load(const char *path, char *err, int errlen)
{
    FILE *f;
    SK_Prev *p;
    unsigned char *blob;
    long bytes;
    unsigned long ver, count, fw, fh, ms;

    sk_err(err, errlen, "");
    if (!path || !*path) {
        sk_err(err, errlen, "no path");
        return 0;
    }
    f = fopen(path, "rb");
    if (!f) {
        sk_err(err, errlen, "not installed");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        sk_err(err, errlen, "not seekable");
        return 0;
    }
    bytes = ftell(f);
    rewind(f);
    if (bytes < SK_PREV_HDR) {
        fclose(f);
        sk_err(err, errlen, "shorter than its own header");
        return 0;
    }
    blob = (unsigned char *)malloc((size_t)bytes);
    if (!blob) {
        fclose(f);
        sk_err(err, errlen, "out of memory");
        return 0;
    }
    if (fread(blob, 1, (size_t)bytes, f) != (size_t)bytes) {
        free(blob);
        fclose(f);
        sk_err(err, errlen, "short read");
        return 0;
    }
    fclose(f);

    if (memcmp(blob, "MAPPREV1", 8) != 0) {
        free(blob);
        sk_err(err, errlen, "not a MAPPREV1 pack");
        return 0;
    }
    ver = sk_rd32(blob + 8);
    count = sk_rd32(blob + 12);
    fw = sk_rd32(blob + 16);
    fh = sk_rd32(blob + 20);
    ms = sk_rd32(blob + 24);
    if (ver != 1) {
        free(blob);
        sk_err(err, errlen, "version is not 1");
        return 0;
    }
    if (count == 0 || count > 4096 || fw == 0 || fh == 0 || fw > 512 || fh > 512 ||
        ms > 256) {
        free(blob);
        sk_err(err, errlen, "header describes an impossible pack");
        return 0;
    }

    p = (SK_Prev *)malloc(sizeof *p);
    if (!p) {
        free(blob);
        sk_err(err, errlen, "out of memory");
        return 0;
    }
    p->blob = blob;
    p->bytes = bytes;
    p->count = (int)count;
    p->fw = (int)fw;
    p->fh = (int)fh;
    p->max_starts = (int)ms;
    p->rec = SK_PR_MARKS + (long)ms * SK_PR_MARK_BYTES + 2L * (long)fw * (long)fh;
    /* The size the header describes and the size on disk have to be the same number.
     * They are the only cross-check available without decoding a picture, and the
     * whole point of a fixed stride is that the n-th record is found by arithmetic:
     * if the stride is wrong every record but the first is garbage. */
    if (bytes != SK_PREV_HDR + (long)p->count * p->rec) {
        free(blob);
        free(p);
        sk_err(err, errlen, "size does not match its own record stride");
        return 0;
    }
    return p;
}

void sk_prev_free(SK_Prev *p)
{
    if (!p)
        return;
    free(p->blob);
    free(p);
}

static const unsigned char *sk_prev_rec(const SK_Prev *p, int i)
{
    if (!p || i < 0 || i >= p->count)
        return 0;
    return p->blob + SK_PREV_HDR + (long)i * p->rec;
}

int sk_prev_find(const SK_Prev *p, const char *scen)
{
    int i;
    if (!p || !scen)
        return -1;
    for (i = 0; i < p->count; i++) {
        const unsigned char *r = sk_prev_rec(p, i);
        /* The field is NUL padded to 12, so a bounded compare is the whole test. */
        if (strncmp((const char *)r + SK_PR_SCEN, scen, 12) == 0)
            return i;
    }
    return -1;
}

/* ------------------------------------------------------------------------ *
 * State.
 * ------------------------------------------------------------------------ */

int sk_players(const SK_State *st)
{
    int n;
    if (!st)
        return 0;
    n = st->ai + 1;
    if (n > SK_ROSTER_ROWS)
        n = SK_ROSTER_ROWS;
    if (n < 1)
        n = 1;
    return n;
}

/* THE ONE PLACE that knows a computer nobody has touched follows the side buttons.
 *
 * Seat 0 is the human and has no store of its own: its faction IS st->side, so the two
 * cannot drift apart and the drop down on row 0 writes the same field the GDI/Nod pair
 * writes. Every computer starts on the OPPOSITE side, which is what this screen did
 * before the drop down existed, and keeps following the side buttons until it is told
 * otherwise by hand. That is what makes the drop down free to ignore: a player who
 * never opens one gets exactly the lobby that shipped. */
int sk_row_house(const SK_State *st, int seat)
{
    if (!st || seat < 0 || seat >= SK_ROSTER_ROWS)
        return 0;
    if (seat == 0)
        return st->side ? 1 : 0;
    if (st->house_set[seat])
        return st->house[seat] ? 1 : 0;
    return st->side ? 0 : 1;
}

int sk_row_team(const SK_State *st, int seat)
{
    if (!st || seat < 0 || seat >= SK_ROSTER_ROWS)
        return 0;
    return st->team[seat];
}

int sk_row_colour(const SK_State *st, int seat)
{
    if (!st || seat < 0 || seat >= SK_ROSTER_ROWS)
        return 0;
    if (st->colour[seat] < 0 || st->colour[seat] >= SK_PLAYER_COLOUR_N)
        return 0;
    return st->colour[seat];
}

/* Which seat is holding colour c, or -1. Over ALL EIGHT SEATS and not only the seated
 * ones, which is the whole reason raising the opponent count cannot produce a clash: a
 * seat nobody is using still owns its colour, so nothing can be handed out twice. */
static int sk_seat_with_colour(const SK_State *st, int c)
{
    int i;
    for (i = 0; i < SK_ROSTER_ROWS; i++)
        if (st->colour[i] == c)
            return i;
    return -1;
}

/* THE LOWEST COLOUR NOBODY IS USING, which is the project owner's rule stated literally rather than
 * inferred. With the permutation invariant there is exactly one such colour at the
 * moment this is called -- the one the seat that is moving has just let go of -- so the
 * scan cannot fail; it is written as a scan anyway so the rule is in the code and not
 * only in a comment, and so a broken invariant degrades to a sane answer instead of a
 * duplicate. -1 only if all eight are somehow taken, which the caller treats as "leave
 * it alone" rather than as a colour. */
static int sk_lowest_free_colour(const SK_State *st)
{
    int c;
    for (c = 0; c < SK_PLAYER_COLOUR_N; c++)
        if (sk_seat_with_colour(st, c) < 0)
            return c;
    return -1;
}

/* GIVE A SEAT A COLOUR, AND MOVE WHOEVER HAD IT.
 *
 * Returns the seat that was displaced, or -1 if nobody was. Two players can never end up
 * sharing: the seat that held `c` is handed the lowest colour nobody is using, and by
 * the invariant that is exactly the colour `seat` has just vacated. It cannot loop --
 * there is one displacement, not a chain -- and it cannot leave a clash, because the
 * only two writes are a swap of two entries in a permutation.
 *
 * It does NOT enforce "never move the human": that rule belongs one level up, where the
 * player's own square is locked out of every other seat's picker, so this is never asked
 * to displace seat 0 on behalf of an AI in the first place. */
static int sk_set_row_colour(SK_State *st, int seat, int c)
{
    int other, freed;
    if (!st || seat < 0 || seat >= SK_ROSTER_ROWS)
        return -1;
    if (c < 0 || c >= SK_PLAYER_COLOUR_N)
        return -1;
    if (st->colour[seat] == c)
        return -1;
    other = sk_seat_with_colour(st, c);
    st->colour[seat] = c;
    if (other < 0 || other == seat)
        return -1;
    freed = sk_lowest_free_colour(st);
    if (freed < 0)
        return -1;                       /* cannot happen; see the header above */
    st->colour[other] = freed;
    return other;
}

static void sk_set_row_house(SK_State *st, int seat, int house)
{
    if (!st || seat < 0 || seat >= SK_ROSTER_ROWS)
        return;
    if (seat == 0) {
        st->side = house ? 1 : 0;
        return;
    }
    st->house[seat] = house ? 1 : 0;
    st->house_set[seat] = 1;
}

/* How many opponents this map has room for. Create_Units compacts the waypoint list, so
 * the count that matters is the contiguous run from 0, which the caller has already
 * measured and put in the row. One start belongs to the human. */
static int sk_ai_cap(const SK_State *st)
{
    int starts;
    if (!st || st->sel < 0 || st->sel >= st->count)
        return SK_AI_MAX;
    starts = st->maps[st->sel].starts;
    if (starts <= 0)
        return SK_AI_MAX; /* unknown: let the engine's own clamp of 5 decide */
    starts--;
    if (starts < SK_AI_MIN)
        starts = SK_AI_MIN;
    if (starts > SK_AI_MAX)
        starts = SK_AI_MAX;
    return starts;
}

static void sk_gauge_range(const SK_State *st, int ctrl, int *lo, int *hi, int *step)
{
    *lo = 0;
    *hi = 0;
    *step = 1;
    switch (ctrl) {
    case SK_I_AI:
        *lo = SK_AI_MIN;
        *hi = sk_ai_cap(st);
        break;
    case SK_I_BUILD:
        *lo = SK_BUILD_MIN;
        *hi = SK_BUILD_MAX;
        break;
    case SK_I_CREDITS:
        *lo = SK_CREDITS_MIN;
        *hi = SK_CREDITS_MAX;
        *step = SK_CREDITS_STEP;
        break;
    case SK_I_UNITS:
        *lo = SK_UNITS_MIN;
        *hi = SK_UNITS_MAX;
        break;
    default:
        break;
    }
}

static int sk_is_gauge(int item)
{
    return item >= SK_I_AI && item <= SK_I_UNITS;
}

static int sk_value(const SK_State *st, int ctrl)
{
    switch (ctrl) {
    case SK_I_AI: return st->ai;
    case SK_I_BUILD: return st->build;
    case SK_I_CREDITS: return st->credits;
    case SK_I_UNITS: return st->units;
    default: return 0;
    }
}

static void sk_set_value(SK_State *st, int ctrl, int v)
{
    int lo, hi, step;
    sk_gauge_range(st, ctrl, &lo, &hi, &step);
    if (step > 1)
        v = ((v + step / 2) / step) * step;
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    switch (ctrl) {
    case SK_I_AI: st->ai = v; break;
    case SK_I_BUILD: st->build = v; break;
    case SK_I_CREDITS: st->credits = v; break;
    case SK_I_UNITS:
        st->units = v;
        /* Said on every move of this gauge, including back to zero, because a player who
           has just undone the risk should see the line that told them about it rather
           than be left wondering what it said. */
        st->status = SK_STATUS[SK_ST_UNITS_RISK];
        break;
    default: break;
    }
}

/* SEATS FILL THE MAP (26 Aug 2026, the project owner's request). Choosing a map SETS the opponent
 * count to what that map seats rather than only capping it, so picking an eight start
 * map hands you seven opponents instead of a gauge to drag. sk_ai_cap already computed
 * the number -- starts minus the human, clamped to SK_AI_MIN..SK_AI_MAX -- and the only
 * change is that the selection assigns it. The gauge still moves afterwards, so anyone
 * who wants fewer says so once per map instead of once per session. */
static int sk_seat_the_map(SK_State *st)
{
    int starts;
    if (!st)
        return 0;
    starts = (st->sel >= 0 && st->sel < st->count) ? st->maps[st->sel].starts : 0;
    if (starts <= 0) {
        /* A MAP WHOSE SEATS WE DO NOT KNOW IS NOT SEATED. sk_ai_cap answers SK_AI_MAX
         * for one of these -- it means "unknown, let the engine's own clamp decide" and
         * not "room for seven" -- and taking that as a seat count would put eight houses
         * on a map with no start waypoints to put them on. It is clamped and left, which
         * is what selecting a map did before this existed. */
        sk_set_value(st, SK_I_AI, st->ai);
        return 0;
    }
    sk_set_value(st, SK_I_AI, sk_ai_cap(st));
    return 1;
}

void sk_init(SK_State *st, const SK_Map *maps, int count, const SK_Prev *prev)
{
    int i;
    if (!st)
        return;
    memset(st, 0, sizeof *st);
    st->maps = maps;
    st->count = count;
    st->prev = prev;
    st->sel = count > 0 ? 0 : -1;
    st->top = 0;
    /* THE DEFAULTS ARE THE COMMAND LINE'S, with ONE deliberate exception: GDI, 5000
     * credits, superweapons on, and a tech level that opens the whole build list because
     * a skirmish has no campaign behind it to unlock one.
     *
     * The exception is the opponent count. --ai still defaults to one because a switch
     * has no map in front of it when it is read; the lobby does, so it seats the map it
     * is showing (sk_seat_the_map, below) and opens with seven opponents on an eight
     * start map. SK_AI_MIN here is only what stands until that runs. */
    st->side = 0;
    st->ai = SK_AI_MIN;
    st->build = SK_BUILD_MAX;
    st->credits = 5000;
    st->super = 1;
    st->crates = 0;
    st->units = SK_UNITS_MIN;
    st->selected = SK_I_MAPS;
    st->pressed = SK_HIT_NONE;
    st->drag = -1;
    st->popup = -1;
    /* the project owner's RULE, in one line: eight players, eight teams, everybody on their own.
     * Nothing is allied with anything until somebody says so. house_set stays zero, so
     * every computer follows the side buttons onto the opposite side the way it always
     * did, and the drop down changes nothing for a player who never opens one.
     *
     * AND EIGHT COLOURS: seat order straight through PlayerColorType, so every player
     * starts on a different one without anybody choosing. That is the permutation the
     * picker then keeps. */
    for (i = 0; i < SK_ROSTER_ROWS; i++) {
        st->team[i] = i;
        st->colour[i] = i % SK_PLAYER_COLOUR_N;
    }
    st->status = sk_status_side(st->side);
    sk_set_value(st, SK_I_AI, st->ai);
    /* The list starts on a map, so the seats start filled to that map. */
    (void)sk_seat_the_map(st);
}

void sk_result(const SK_State *st, SK_Lobby *out)
{
    int i, players;
    if (!st || !out)
        return;
    memset(out, 0, sizeof *out);
    out->map = st->sel;
    out->side = st->side;
    out->ai_count = st->ai;
    out->build = st->build;
    out->credits = st->credits;
    out->superweapons = st->super ? 1 : 0;
    /* Bases and tiberium are drawn locked ON and these are the values those locks stand
     * for. CRATES ARE NOT LOCKED and this comment said they were: sk_item_disabled
     * un-locks them and the box is drawn and clickable, so the sentence claiming
     * otherwise was false before this change and visibly false after it.
     * STARTING UNITS IS THE PLAYER'S, through a gauge that was fully built and simply
     * never drawn: the draw loop stopped one item short of it, so it took input and
     * showed nothing. It still DEFAULTS to zero, and for a measured reason: the engine scatters the escort within about four
     * cells of the MCV and a unit landing inside the Construction Yard's 3x3 pad makes the
     * player's first click do nothing. That is a good default, not a good prohibition, so
     * the gauge offers 0..10 and the risk is the player's to take. The ASSORTMENT the
     * request asked for -- Mammoth Tank, Stealth Tank, SSM Launcher -- is the Tech Level
     * row's doing already: scenarioini.cpp:1416 maps build level to unit type, level 6 to
     * the SSM launcher and level 7 to Mammoth for GDI and Stealth for Nod. */
    out->bases = 1;
    out->tiberium = 1;
    out->crates = st->crates;
    out->unit_count = st->units;
    /* One compacted waypoint per player, human first, written out rather than left to
     * the engine's own pick: that pick can only reach the first six waypoints and it
     * shuffles with a wall-clock seed, which costs reproducibility for nothing. */
    players = sk_players(st);          /* MAX_PLAYERS: eight since the brain patch */
    /* EIGHT, not six: this loop was still Tiberian Dawn's original MAX_PLAYERS while
     * everything around it had grown to eight, so seats 7 and 8 were handed the zero the
     * memset left instead of their own waypoint. Invisible until now only because the
     * engine rewrites a duplicated zero to the player index; visible from the day the
     * map started seating eight players by itself. */
    for (i = 0; i < 8; i++)
        out->start_wp[i] = (i < players) ? i : -1;
    /* PER SEAT, through the two readouts and never out of the arrays, so a computer the
     * player never touched is reported as following the side buttons rather than as the
     * zero its slot happens to hold. */
    for (i = 0; i < SK_ROSTER_ROWS && i < 8; i++) {
        out->house[i] = sk_row_house(st, i);
        out->team[i] = sk_row_team(st, i);
        /* EVERY SEAT, not only the seated ones, and that is deliberate: the array is a
         * permutation, so writing all eight is what lets the caller assert distinctness
         * on its side without knowing how many players there are. */
        out->colour[i] = sk_row_colour(st, i);
    }
}

/* ------------------------------------------------------------------------ *
 * Labels, disabled rules, rectangles.
 * ------------------------------------------------------------------------ */

/* The roster's own name for a seat, which is also the drop down's header and the name a
 * script drives this screen by. One writer, so the three can never disagree. */
void sk_seat_name(const SK_State *st, int seat, char *out, int outlen)
{
    (void)st;
    if (!out || outlen <= 0)
        return;
    if (seat <= 0)
        snprintf(out, (size_t)outlen, "PLAYER");
    else
        snprintf(out, (size_t)outlen, "COMPUTER %d", seat);
}

const char *sk_item_label(const SK_State *st, int item)
{
    /* A row's label is its seat name, and a script clicks by label, so it has to be a
     * string that outlives the call. One buffer is enough: nothing holds two at once. */
    static char rowname[24];
    static char teamnum[4];
    static char colname[12];
    if (SK_IS_ROW(item)) {
        sk_seat_name(st, item - SK_I_ROW0, rowname, (int)sizeof rowname);
        return rowname;
    }
    if (item >= SK_I_POP_T1 && item <= SK_I_POP_T8) {
        snprintf(teamnum, sizeof teamnum, "%d", item - SK_I_POP_T1 + 1);
        return teamnum;
    }
    /* A COLOUR SQUARE HAS NO PRINTED LABEL AND THIS IS NOT ONE. Nothing draws it: the
     * button is a filled square with no text in it, and sk_check_layout skips every
     * popup id. It exists because a harness clicks by label and a log line that said
     * nothing would name eight different controls the same way. It is the INDEX, which
     * is a position in the engine's own order and not a name for a colour. */
    if (SK_IS_POP_COL(item)) {
        snprintf(colname, sizeof colname, "colour %d", item - SK_I_POP_C1 + 1);
        return colname;
    }
    switch (item) {
    case SK_I_POP_GDI: return "GDI";
    case SK_I_POP_NOD: return "Nod";
    case SK_I_GDI: return "GDI";
    case SK_I_NOD: return "Nod";
    case SK_I_MAPS: return "Battlefield";
    case SK_I_AI: return "AI Players:";
    case SK_I_BUILD: return "Tech Level:";
    case SK_I_CREDITS: return "Credits:";
    /* TXT_COUNT's own words, identical in both 1995 games (tiberiandawn/conquer.h:655,
     * redalert/conquer.h:315), and what nulldlg.cpp prints beside this same slider.
     * Measured in GRAD6FNT it is 61 pixels against the caption column's 78; the invented
     * "Starting Units:" it replaces measured 85 and would have printed from x=15, one
     * pixel outside the dialog's left edge. */
    case SK_I_UNITS: return "Unit Count:";
    case SK_I_BASES: return "Bases";
    /* nulldlg.cpp:377's own wording. Red Alert calls the same field Ore Spreads. */
    case SK_I_TIBERIUM: return "Tiberium Regrows";
    case SK_I_SUPER: return "Superweapons";
    case SK_I_CRATES: return "Bonus Crates";
    case SK_I_PLAY: return "Play";
    case SK_I_CANCEL: return "Cancel";
    default: return "";
    }
}

int sk_item_disabled(const SK_State *st, int item)
{
    /* A seat past the current opponent count is not drawn, so it is not reachable by the
     * walk either. It has no rectangle for the mouse (sk_item_rect returns 0), and this
     * is the same fact told to the keyboard. */
    if (SK_IS_ROW(item))
        return (st && (item - SK_I_ROW0) < sk_players(st)) ? 0 : 1;
    /* THE PLAYER'S OWN COLOUR IS LOCKED ON EVERY OTHER ROW. Handing an AI the colour the
     * human is wearing would have to move somebody, and the only somebody available is
     * the human -- so a change to a computer's row would silently repaint the player.
     * The square is drawn locked rather than hidden, and clicking it says why. On the
     * player's OWN row every square is live: taking a colour off a computer moves that
     * computer, which is exactly what the project owner asked for. */
    if (SK_IS_POP_COL(item)) {
        if (!st || st->popup < 0)
            return 1;
        return (st->popup != 0 && (item - SK_I_POP_C1) == sk_row_colour(st, 0)) ? 1 : 0;
    }
    /* The drop down's controls exist only while it is open. */
    if (SK_IS_POP(item))
        return (st && st->popup >= 0) ? 0 : 1;
    switch (item) {
    case SK_I_BASES:
    case SK_I_TIBERIUM:
        return 1; /* drawn, checked, locked; the header says why */
    /* CRATES ARE LIVE as of 26 Aug 2026 and this row is no longer locked. The engine
     * always had the feature -- Place_Random_Crate scatters them under MPlayerGoodies,
     * which our crates flag already drove, and cell.cpp's pickup gives money, a unit, a
     * nuclear missile piece or a change of shroud. What was missing was the PICTURE: the
     * brain dumped tiberium and walls and nothing else, so a crate was an invisible
     * pickup. The CRATE| dump, bake_doscrate.py and doscrate_mod.h close that. */
    case SK_I_PLAY:
        return (st && st->sel >= 0) ? 0 : 1; /* no map, no match */
    case SK_I_MAPS:
        return (st && st->count > 0) ? 0 : 1;
    case SK_I_AI:
        /* A map with room for exactly one opponent leaves this gauge nowhere to go.
         * It is still drawn, still shows the truth, and says so when clicked. */
        return (st && sk_ai_cap(st) <= SK_AI_MIN) ? 1 : 0;
    default:
        return 0;
    }
}

/* WHERE THE DROP DOWN SITS: under the row it belongs to, left aligned with the roster,
 * and clamped so it ends inside the dialog. The clamp cannot fire with today's numbers
 * -- the last row anchors it at 90 and it is 37 tall, well clear of 190 -- and it is
 * written anyway because the day a row pitch or a seat count changes is the day a popup
 * would otherwise draw through the bottom of the plate. sk_check_layout asserts the
 * result for all eight rows rather than trusting this comment. */
static void sk_popup_box(const SK_State *st, int *x, int *y, int *w, int *h)
{
    int py = SK_ROSTER_Y + (st ? st->popup : 0) * SK_ROSTER_ROW_H + SK_ROSTER_ROW_H;
    if (py > SK_POP_Y_MAX)
        py = SK_POP_Y_MAX;
    if (py < SK_DLG_Y + 4)
        py = SK_DLG_Y + 4;
    *x = SK_POP_X;
    *y = py;
    *w = SK_POP_W;
    *h = SK_POP_H;
}

int sk_item_rect(const SK_State *st, int item, int *x, int *y, int *w, int *h)
{
    if (SK_IS_ROW(item)) {
        const int seat = item - SK_I_ROW0;
        if (!st || seat >= sk_players(st)) {
            *x = *y = *w = *h = 0;
            return 0;
        }
        *x = SK_ROSTER_X;
        *y = SK_ROSTER_Y + seat * SK_ROSTER_ROW_H;
        *w = SK_ROSTER_W;
        *h = SK_ROSTER_ROW_H;
        return 1;
    }
    if (SK_IS_POP(item)) {
        int bx, by, bw, bh;
        if (!st || st->popup < 0) {
            *x = *y = *w = *h = 0;
            return 0;
        }
        sk_popup_box(st, &bx, &by, &bw, &bh);
        *h = SK_POP_BTN_H;
        if (item == SK_I_POP_GDI || item == SK_I_POP_NOD) {
            *y = by + SK_POP_PAD + SK_POP_HDR_H;
            *w = SK_POP_SIDE_W;
            *x = bx + SK_POP_PAD + SK_POP_LAB_W +
                 ((item == SK_I_POP_NOD) ? (SK_POP_SIDE_W + SK_POP_SIDE_GAP) : 0);
            return 1;
        }
        if (SK_IS_POP_COL(item)) {
            *y = by + SK_POP_PAD + SK_POP_HDR_H + SK_POP_ROW_H * 2;
            *w = SK_POP_COL_W;
            *x = bx + SK_POP_PAD + SK_POP_LAB_W + (item - SK_I_POP_C1) * SK_POP_COL_W;
            return 1;
        }
        *y = by + SK_POP_PAD + SK_POP_HDR_H + SK_POP_ROW_H;
        *w = SK_POP_TEAM_W;
        *x = bx + SK_POP_PAD + SK_POP_LAB_W + (item - SK_I_POP_T1) * SK_POP_TEAM_W;
        return 1;
    }
    switch (item) {
    case SK_I_GDI:
        *x = SK_GDI_X; *y = SK_SIDE_Y; *w = SK_SIDE_W; *h = SK_SIDE_H;
        return 1;
    case SK_I_NOD:
        *x = SK_NOD_X; *y = SK_SIDE_Y; *w = SK_SIDE_W; *h = SK_SIDE_H;
        return 1;
    case SK_I_MAPS:
        *x = SK_MAP_X; *y = SK_MAP_Y; *w = SK_MAP_W; *h = SK_MAP_H;
        return 1;
    case SK_I_AI:
    case SK_I_BUILD:
    case SK_I_CREDITS:
    case SK_I_UNITS:
        *x = SK_GAUGE_X;
        *y = SK_CTRL_Y + (item - SK_I_AI) * SK_CTRL_STEP;
        *w = SK_GAUGE_W;
        *h = SK_GAUGE_H;
        return 1;
    case SK_I_BASES:
    case SK_I_TIBERIUM:
    case SK_I_SUPER:
    case SK_I_CRATES:
        /* THE WHOLE ROW, not the seven pixel box. dopt_item_rect does the same and it
         * is the right call: 7x7 is what a 1995 dialog drew and what a mouse misses
         * today. The box itself is drawn at the left of this rectangle. */
        *x = SK_CHK_X;
        *y = SK_CTRL_Y + (item - SK_I_BASES) * SK_CTRL_STEP;
        *w = SK_CHK_ROW_W;
        *h = SK_CHK_BOX;
        return 1;
    case SK_I_PLAY:
        *x = SK_PLAY_X; *y = SK_BTN_Y; *w = SK_PLAY_W; *h = SK_BTN_H;
        return 1;
    case SK_I_CANCEL:
        *x = SK_CANCEL_X; *y = SK_BTN_Y; *w = SK_CANCEL_W; *h = SK_BTN_H;
        return 1;
    default:
        *x = *y = *w = *h = 0;
        return 0;
    }
}

static int sk_in_rect(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

int sk_hit_test(const SK_State *st, int mx, int my)
{
    int i, x, y, w, h;
    /* THE DROP DOWN IS ASKED FIRST AND ANSWERS ALONE. It floats over controls that are
     * still drawn under it, so an ordinary walk of the item list would hand a click on
     * the popup to whatever it happens to cover. While it is open nothing else on the
     * screen is hit at all: a press that misses it shuts it and does nothing more, which
     * is what every drop list since 1995 does. */
    if (st && st->popup >= 0) {
        for (i = SK_I_POP_GDI; i <= SK_I_POP_C8; i++)
            if (sk_item_rect(st, i, &x, &y, &w, &h) && sk_in_rect(mx, my, x, y, w, h))
                return i;
        return SK_HIT_NONE;
    }
    for (i = 0; i < SK_I_COUNT; i++) {
        if (!sk_item_rect(st, i, &x, &y, &w, &h))
            continue;
        if (sk_in_rect(mx, my, x, y, w, h))
            return i;
    }
    return SK_HIT_NONE;
}

/* THE FILTERED VIEW. `sel` and `maps[]` are absolute; the rows on screen are the maps of
   the current tab only. Every conversion goes through these two, so the draw, the hit
   test and the keyboard cannot disagree about which map row 3 is. */
static int sk_tab_count(const SK_State *st)
{
    int i, n = 0;
    for (i = 0; i < st->count; i++)
        if ((int)st->maps[i].user == st->tab) n++;
    return n;
}

/* Where an absolute index sits within its own tab, or -1 if it is on the other one.
   The inverse of sk_map_at, and the reason both exist: `sel` is absolute so the answer
   the screen returns is a real map, while `top` counts ROWS of the filtered list. Mixing
   the two scrolls to the wrong place the moment a tab has fewer maps than the other. */
static int sk_row_of(const SK_State *st, int ix)
{
    int i, k = 0;
    if (ix < 0 || ix >= st->count) return -1;
    if ((int)st->maps[ix].user != st->tab) return -1;
    for (i = 0; i < ix; i++)
        if ((int)st->maps[i].user == st->tab) k++;
    return k;
}

/* The nth map of the current tab, absolute index, or -1. */
static int sk_map_at(const SK_State *st, int n)
{
    int i, k = 0;
    if (n < 0) return -1;
    for (i = 0; i < st->count; i++)
        if ((int)st->maps[i].user == st->tab && k++ == n) return i;
    return -1;
}

/* Which tab is under the pointer, or -1. Separate from the row test because a click on
   a tab means something different to a click on a map. */
const char *sk_tab_label(int tab)
{
    return tab ? "USER MAPS" : "OFFICIAL";
}

int sk_tab_at(const SK_State *st, int mx, int my)
{
    int t;
    if (!st) return -1;
    /* The drop down floats over this column, so a click that the popup owns must not
     * also be read as a change of map source. */
    if (st->popup >= 0) return -1;
    for (t = 0; t < 2; t++)
        if (sk_in_rect(mx, my, SK_TAB_X(t), SK_TAB_Y, SK_TAB_W(t), SK_TAB_H))
            return t;
    return -1;
}

/* Switch tabs and land somewhere sensible: the first map of the new tab, or nothing
   selected if it is empty. Leaving `sel` pointing into the other tab would show a
   preview and a set of facts for a map that is no longer in the list. */
void sk_set_tab(SK_State *st, int tab)
{
    if (!st || tab == st->tab) return;
    st->tab = tab;
    st->top = 0;
    st->sel = sk_map_at(st, 0);
}

int sk_map_row_at(const SK_State *st, int mx, int my)
{
    int row, ix;
    if (!st || st->count <= 0)
        return -1;
    /* The drop down overlaps this list, and the shell turns a double click here into
     * Play. A click the popup owns is the popup's and starts nothing. */
    if (st->popup >= 0)
        return -1;
    if (!sk_in_rect(mx, my, SK_MAP_X, SK_MAPLIST_Y, SK_MAP_W, SK_MAP_H))
        return -1;
    row = (my - SK_MAPLIST_Y) / SK_ROW_H;
    ix = sk_map_at(st, st->top + row);
    return ix;
}


/* menus.cpp:902-944: the walk wraps and steps over anything disabled. A locked check
 * box is disabled, so the keyboard skips it; the mouse can still click it and be told
 * why it will not move, which is what the status line is for. */
int sk_next_item(const SK_State *st, int item, int delta)
{
    int i, idx = item;
    if (delta == 0)
        delta = 1;
    for (i = 0; i < SK_I_COUNT; i++) {
        idx += delta;
        while (idx < 0)
            idx += SK_I_COUNT;
        idx %= SK_I_COUNT;
        if (!sk_item_disabled(st, idx))
            return idx;
    }
    return item;
}

/* ------------------------------------------------------------------------ *
 * Gauge arithmetic. gauge.cpp's own, in integers.
 * ------------------------------------------------------------------------ */

static int sk_value_to_pixel(const SK_State *st, int ctrl, int value)
{
    int x, y, w, h, span, lo, hi, step;
    if (!sk_item_rect(st, ctrl, &x, &y, &w, &h))
        return 0;
    sk_gauge_range(st, ctrl, &lo, &hi, &step);
    span = w - 2;
    if (hi <= lo)
        return x;
    if (value < lo)
        value = lo;
    if (value > hi)
        value = hi;
    return x + (int)(((long)span * (value - lo)) / (hi - lo));
}

static int sk_pixel_to_value(const SK_State *st, int ctrl, int pixel)
{
    int x, y, w, h, span, lo, hi, step;
    if (!sk_item_rect(st, ctrl, &x, &y, &w, &h))
        return 0;
    sk_gauge_range(st, ctrl, &lo, &hi, &step);
    span = w - 2;
    pixel -= x + 1;
    if (pixel < 0)
        pixel = 0;
    if (pixel > span)
        pixel = span;
    if (span <= 0 || hi <= lo)
        return lo;
    return lo + (int)(((long)(hi - lo) * pixel) / span);
}

/* ------------------------------------------------------------------------ *
 * Input.
 * ------------------------------------------------------------------------ */

/* Forward: choosing a map can retire the seat whose drop down is open, and the two
 * live at opposite ends of this section because one is list handling and the other is
 * the widget. */
static void sk_popup_close(SK_State *st);

static void sk_select_map(SK_State *st, int ix)
{
    int row;
    if (ix < 0 || ix >= st->count)
        return;
    st->sel = ix;
    /* Scroll in ROWS of the tab being shown, not in absolute indices. */
    row = sk_row_of(st, ix);
    if (row >= 0) {
        if (row < st->top)
            st->top = row;
        if (row >= st->top + SK_MAP_ROWS)
            st->top = row - SK_MAP_ROWS + 1;
        if (st->top < 0)
            st->top = 0;
    }
    /* THE MAP DECIDES THE SEATS. It used to only clamp -- a map with fewer starts took
     * opponents away and said so, and a map with more left the gauge where it was, so an
     * eight start map opened with one opponent on it and the player had to notice.
     * Selecting a map now SETS the count to what the map seats, which is the project owner's request
     * and the answer nine times out of ten. The gauge is untouched and still moves. */
    if (sk_seat_the_map(st))
        st->status = SK_STATUS[SK_ST_SEATED];
    /* A row that vanished cannot keep a drop down open over it. */
    if (st->popup >= sk_players(st))
        sk_popup_close(st);
}

void sk_scroll(SK_State *st, int delta)
{
    int last;
    if (!st || st->count <= 0)
        return;
    /* Against the SHOWN tab, not the whole list: scrolling past the end of a short
       User Maps tab because the Official one is long is exactly the bug that mixing
       these two indices produces. */
    last = sk_tab_count(st) - SK_MAP_ROWS;
    if (last < 0)
        last = 0;
    st->top += delta;
    if (st->top < 0)
        st->top = 0;
    if (st->top > last)
        st->top = last;
}

/* THE ONE LINE THAT SAYS WHAT A SEAT NOW IS, built rather than looked up because it has
 * to name a seat. It goes into the state's own buffer and `status` is pointed at it;
 * sk_check_layout measures the widest form it can take. The wording is deliberately the
 * whole causal chain -- who, which side, which team, and who that allies them with --
 * because a drop down that changes two things at once is exactly where a player loses
 * track of what they just did. */
static void sk_say_seat(SK_State *st, int seat)
{
    char name[24];
    int i, mates = 0, players;
    if (!st || seat < 0 || seat >= SK_ROSTER_ROWS)
        return;
    players = sk_players(st);
    for (i = 0; i < players; i++)
        if (i != seat && sk_row_team(st, i) == sk_row_team(st, seat))
            mates++;
    sk_seat_name(st, seat, name, (int)sizeof name);
    if (mates > 0)
        snprintf(st->statusbuf, sizeof st->statusbuf, "%s: %s, team %d with %d other%s.",
                 name, sk_row_house(st, seat) ? "Nod" : "GDI", sk_row_team(st, seat) + 1,
                 mates, mates == 1 ? "" : "s");
    else
        snprintf(st->statusbuf, sizeof st->statusbuf, "%s: %s, team %d, allied with "
                                                      "nobody.",
                 name, sk_row_house(st, seat) ? "Nod" : "GDI",
                 sk_row_team(st, seat) + 1);
    st->status = st->statusbuf;
}

/* WHAT A COLOUR CHANGE JUST DID, which is the one place on this screen where touching
 * one row can move another. The legibility rail says a consequence has to say its own
 * cause, so it names both seats: who took the colour and who was moved off it. It cannot
 * name the COLOURS -- there are no names, by ruling -- and it does not need to, because
 * the square that just changed is on screen in both rows.
 *
 * `moved` is sk_set_row_colour's answer: the displaced seat, or -1 when the colour was
 * free and nobody had to move. */
static void sk_say_colour(SK_State *st, int seat, int moved)
{
    char name[24], other[24];
    if (!st || seat < 0 || seat >= SK_ROSTER_ROWS)
        return;
    sk_seat_name(st, seat, name, (int)sizeof name);
    if (moved >= 0) {
        sk_seat_name(st, moved, other, (int)sizeof other);
        snprintf(st->statusbuf, sizeof st->statusbuf, "%s takes that colour; %s moves.",
                 name, other);
    } else {
        snprintf(st->statusbuf, sizeof st->statusbuf,
                 "%s changes colour; nobody else moves.", name);
    }
    st->status = st->statusbuf;
}

/* WHAT THE SIDE BUTTONS NOW MEAN, and the reason this is computed rather than looked up.
 *
 * "You play GDI; every computer plays Nod." was the whole truth while the sides were
 * forced opposite, and it stopped being the whole truth the day one row could be given a
 * faction by hand. The table line is still used while nobody has done that, because then
 * it IS exactly true and it is the shorter sentence; the moment a row has been set the
 * screen counts the seats and says the real split instead of repeating a rule it is no
 * longer following. */
static void sk_say_sides(SK_State *st)
{
    int i, players, nod = 0, gdi = 0, custom = 0;
    if (!st)
        return;
    players = sk_players(st);
    for (i = 1; i < players; i++) {
        if (st->house_set[i])
            custom++;
        if (sk_row_house(st, i))
            nod++;
        else
            gdi++;
    }
    if (custom == 0) {
        st->status = sk_status_side(st->side);
        return;
    }
    snprintf(st->statusbuf, sizeof st->statusbuf, "You play %s. Computers: %d Nod, %d GDI.",
             st->side ? "Nod" : "GDI", nod, gdi);
    st->status = st->statusbuf;
}

/* Opening and shutting the drop down are one place each, because both of them have to
 * move the keyboard focus as well: an open popup owns the walk, and a shut one gives it
 * back to the row it belonged to. */
static void sk_popup_open(SK_State *st, int seat)
{
    if (!st || seat < 0 || seat >= sk_players(st))
        return;
    st->popup = seat;
    st->selected = SK_I_POP_GDI;
    sk_say_seat(st, seat);
}

static void sk_popup_close(SK_State *st)
{
    if (!st || st->popup < 0)
        return;
    st->selected = SK_ROW_ITEM(st->popup);
    st->popup = -1;
}

static int sk_activate(SK_State *st, int item)
{
    if (SK_IS_ROW(item)) {
        const int seat = item - SK_I_ROW0;
        /* A second click on the row that is already open shuts it, which is the only
         * way a click can reach this case: while the popup is up the hit test refuses
         * everything outside it, so this arm is the keyboard's. */
        if (st->popup == seat)
            sk_popup_close(st);
        else
            sk_popup_open(st, seat);
        return SK_ACT_NONE;
    }
    if (SK_IS_POP(item)) {
        const int seat = st->popup;
        if (seat < 0)
            return SK_ACT_NONE;
        if (SK_IS_POP_COL(item)) {
            /* The player's own square on somebody else's row: locked, and it says why
             * rather than doing nothing. sk_hit_test deliberately still reports a
             * disabled control so this line can be reached. */
            if (sk_item_disabled(st, item)) {
                st->status = SK_STATUS[SK_ST_COL_HUMAN];
                return SK_ACT_NONE;
            }
            sk_say_colour(st, seat, sk_set_row_colour(st, seat, item - SK_I_POP_C1));
            return SK_ACT_NONE;
        }
        if (item == SK_I_POP_GDI || item == SK_I_POP_NOD)
            sk_set_row_house(st, seat, item == SK_I_POP_NOD ? 1 : 0);
        else
            st->team[seat] = item - SK_I_POP_T1;
        /* It STAYS OPEN. There are three questions on it and closing after the first
         * would make setting them all a matter of opening the same row three times. Any
         * press outside, Escape, Enter or a second click on the row shuts it. */
        sk_say_seat(st, seat);
        return SK_ACT_NONE;
    }
    switch (item) {
    case SK_I_GDI:
    case SK_I_NOD:
        /* A LATCHED PAIR wearing the dialog's own button look, because the 1995
         * toolkit has no radio widget: netdlg.cpp:893-901 does the same with two
         * TextButtonClass gadgets and Turn_On. Clicking the one already lit does
         * nothing rather than turning both off. */
        st->side = (item == SK_I_NOD) ? 1 : 0;
        sk_say_sides(st);
        return SK_ACT_NONE;
    case SK_I_SUPER:
        st->super = !st->super;
        st->status = SK_STATUS[st->super ? SK_ST_SUPER_ON : SK_ST_SUPER_OFF];
        return SK_ACT_NONE;
    case SK_I_CRATES:
        st->crates = !st->crates;
        st->status = SK_STATUS[st->crates ? SK_ST_CRATES_ON : SK_ST_CRATES_OFF];
        return SK_ACT_NONE;
    case SK_I_BASES:
        st->status = SK_STATUS[SK_ST_BASES];
        return SK_ACT_NONE;
    case SK_I_TIBERIUM:
        st->status = SK_STATUS[SK_ST_TIBERIUM];
        return SK_ACT_NONE;
    case SK_I_AI:
        if (sk_item_disabled(st, SK_I_AI))
            st->status = SK_STATUS[SK_ST_ONE_AI];
        return SK_ACT_NONE;
    case SK_I_MAPS:
        return SK_ACT_NONE;
    case SK_I_PLAY:
        return (st->sel >= 0) ? SK_ACT_PLAY : SK_ACT_NONE;
    case SK_I_CANCEL:
        return SK_ACT_CANCEL;
    default:
        return SK_ACT_NONE;
    }
}

int sk_press(SK_State *st, int mx, int my)
{
    int hit;
    if (!st)
        return SK_ACT_NONE;
    hit = sk_hit_test(st, mx, my);
    st->lastmx = mx;
    st->lastmy = my;
    st->drag = -1;
    if (hit == SK_HIT_NONE) {
        /* A press that misses the open drop down shuts it and is spent doing so. The
         * click does NOT then fall through to whatever was underneath, which is the
         * behaviour of every drop list this dialog is imitating. */
        sk_popup_close(st);
        st->pressed = SK_HIT_NONE;
        return SK_ACT_NONE;
    }

    if (hit == SK_I_MAPS) {
        const int row = sk_map_row_at(st, mx, my);
        if (row >= 0) {
            sk_select_map(st, row);
            st->selected = SK_I_MAPS;
        }
        st->pressed = SK_HIT_NONE;
        return SK_ACT_NONE;
    }

    if (sk_is_gauge(hit) && !sk_item_disabled(st, hit)) {
        /* gauge.cpp:288-316: a click ON the thumb drags it from where it was grabbed,
         * a click anywhere else jumps the value to the pointer. The grab offset is then
         * walked back until the pointer converts to the value that is ALREADY set, so
         * taking hold of a slider does not shift it by a pixel of rounding before it
         * has moved at all. */
        const int cur = sk_value(st, hit);
        const int curpix = sk_value_to_pixel(st, hit, cur);
        st->dragdiff = (mx > curpix && mx - curpix < 4) ? mx - curpix : 0;
        while (st->dragdiff > 0 && sk_pixel_to_value(st, hit, mx - st->dragdiff) < cur)
            st->dragdiff--;
        st->drag = hit;
        st->selected = hit;
        sk_set_value(st, hit, sk_pixel_to_value(st, hit, mx - st->dragdiff));
        return SK_ACT_NONE;
    }

    st->pressed = hit;
    if (!sk_item_disabled(st, hit))
        st->selected = hit;
    return SK_ACT_NONE;
}

int sk_motion(SK_State *st, int mx, int my)
{
    if (!st)
        return SK_ACT_NONE;
    st->lastmx = mx;
    st->lastmy = my;
    if (st->drag >= 0)
        sk_set_value(st, st->drag, sk_pixel_to_value(st, st->drag, mx - st->dragdiff));
    return SK_ACT_NONE;
}

/* The action of a control happens on RELEASE over the same control the press went down
 * on, which is gadget.cpp's LEFTRELEASE and what every other screen here does. */
int sk_release(SK_State *st, int mx, int my)
{
    int hit, act = SK_ACT_NONE;
    if (!st)
        return SK_ACT_NONE;
    st->lastmx = mx;
    st->lastmy = my;
    if (st->drag >= 0) {
        st->drag = -1;
        st->pressed = SK_HIT_NONE;
        return SK_ACT_NONE;
    }
    hit = sk_hit_test(st, mx, my);
    if (hit != SK_HIT_NONE && hit == st->pressed)
        act = sk_activate(st, hit);
    st->pressed = SK_HIT_NONE;
    return act;
}

/* The walk INSIDE the drop down, which is its own small ring rather than a pass over the
 * whole screen: while it is open it owns the keyboard exactly as it owns the mouse, so
 * Tab cannot wander off onto a gauge the player cannot see under the box. */
static int sk_pop_walk(const SK_State *st, int item, int delta)
{
    const int n = SK_I_POP_C8 - SK_I_POP_GDI + 1;
    int k = SK_IS_POP(item) ? item - SK_I_POP_GDI : 0;
    int i;
    if (delta == 0)
        delta = 1;
    /* STEPS OVER WHAT IS LOCKED, the same rule sk_next_item follows on the screen behind
     * it: the player's own colour square is disabled on every other row, and a walk that
     * stopped on it would leave the focus somewhere Space does nothing. */
    for (i = 0; i < n; i++) {
        k = (k + delta % n + n) % n;
        if (!sk_item_disabled(st, SK_I_POP_GDI + k))
            return SK_I_POP_GDI + k;
    }
    return item;
}

int sk_key(SK_State *st, int key)
{
    int lo, hi, step, nudge;
    if (!st)
        return SK_ACT_NONE;

    /* MODAL WHILE OPEN. Escape and Enter shut the box rather than leaving the lobby or
     * starting the match, which is the one thing a player pressing Escape over an open
     * drop down means and the one thing they would be most annoyed to have misread. */
    if (st->popup >= 0) {
        switch (key) {
        case SK_KEY_ESC:
        case SK_KEY_ENTER:
            sk_popup_close(st);
            return SK_ACT_NONE;
        case SK_KEY_SPACE:
            return sk_activate(st, st->selected);
        case SK_KEY_TAB:
        case SK_KEY_RIGHT:
        case SK_KEY_DOWN:
            st->selected = sk_pop_walk(st, st->selected, 1);
            return SK_ACT_NONE;
        case SK_KEY_LEFT:
        case SK_KEY_UP:
            st->selected = sk_pop_walk(st, st->selected, -1);
            return SK_ACT_NONE;
        default:
            return SK_ACT_NONE;
        }
    }

    switch (key) {
    case SK_KEY_ESC:
        return SK_ACT_CANCEL;
    case SK_KEY_ENTER:
        return (st->sel >= 0) ? SK_ACT_PLAY : SK_ACT_NONE;
    case SK_KEY_TAB:
        st->selected = sk_next_item(st, st->selected, 1);
        return SK_ACT_NONE;
    case SK_KEY_SPACE:
        return sk_activate(st, st->selected);
    case SK_KEY_UP:
    case SK_KEY_DOWN: {
        const int d = (key == SK_KEY_UP) ? -1 : 1;
        /* The arrows walk the FOCUSED LIST while the list has focus, and the control
         * block otherwise. Tab is the way out of the list either way. */
        /* Step by ROWS OF THE SHOWN TAB. sel + d walks the absolute list and would
           step straight from the last official map onto the first user one, past a
           divider the screen is drawing. */
        if (st->selected == SK_I_MAPS && sk_tab_count(st) > 0)
            sk_select_map(st, sk_map_at(st, sk_row_of(st, st->sel) + d));
        else
            st->selected = sk_next_item(st, st->selected, d);
        return SK_ACT_NONE;
    }
    case SK_KEY_PGUP:
    case SK_KEY_PGDN:
        if (sk_tab_count(st) > 0) {
            int r = sk_row_of(st, st->sel)
                  + (key == SK_KEY_PGUP ? -SK_MAP_ROWS : SK_MAP_ROWS);
            const int n = sk_tab_count(st);
            if (r < 0) r = 0;
            if (r >= n) r = n - 1;
            sk_select_map(st, sk_map_at(st, r));
        }
        return SK_ACT_NONE;
    case SK_KEY_LEFT:
    case SK_KEY_RIGHT:
        if (!sk_is_gauge(st->selected) || sk_item_disabled(st, st->selected))
            return SK_ACT_NONE;
        sk_gauge_range(st, st->selected, &lo, &hi, &step);
        /* An eighth of travel, which is what dopt_key nudges by, floored at one step
         * so the coarsest gauge still moves. */
        nudge = (hi - lo) / 8;
        if (nudge < step)
            nudge = step;
        sk_set_value(st, st->selected,
                     sk_value(st, st->selected) + (key == SK_KEY_LEFT ? -nudge : nudge));
        return SK_ACT_NONE;
    default:
        return SK_ACT_NONE;
    }
}

/* ------------------------------------------------------------------------ *
 * Draw.
 * ------------------------------------------------------------------------ */

/* db_print CLIPS TO THE SURFACE AND NEVER TO A CALLER'S BOX, so a label wider than its
 * column prints across whatever is beside it and off the dialog. That has shipped as a
 * visible bug in this project once already. Every string on this screen that is not a
 * fixed word goes through here: it measures, and drops characters off the end until
 * what is left fits. */
static void sk_print_fit(DB_Surface *s, const DB_Font *f, const char *text, int x, int y,
                         int maxw, const unsigned char *fp)
{
    char buf[96];
    int n;

    if (!s || !f || !text || !*text || maxw <= 0)
        return;
    n = (int)strlen(text);
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    memcpy(buf, text, (size_t)n);
    buf[n] = 0;
    while (n > 0 && db_string_width(f, buf, DB_FONT6_XSPACING) > maxw) {
        n--;
        buf[n] = 0;
    }
    if (n > 0)
        db_print(s, f, buf, x, y, fp, DB_FONT6_XSPACING);
}

/* The same, but a cut name SAYS it was cut. Only for text the PLAYER authored --
   a hard clip ends such a name mid-word and reads as its real name, which is the
   failure the fit audit exists to catch. Fixed labels keep the plain fit: two dots
   turned OFFICIAL into OFFIC.. and lost more than they explained. */
static void sk_print_fit_cut(DB_Surface *s, const DB_Font *f, const char *text,
                             int x, int y, int maxw, const unsigned char *fp)
{
    char buf[96];
    int n;

    if (!s || !f || !text || !*text || maxw <= 0)
        return;
    n = (int)strlen(text);
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    memcpy(buf, text, (size_t)n);
    buf[n] = 0;
    if (db_string_width(f, buf, DB_FONT6_XSPACING) <= maxw) {
        db_print(s, f, buf, x, y, fp, DB_FONT6_XSPACING);
        return;
    }
    while (n > 0 && db_string_width(f, buf, DB_FONT6_XSPACING) > maxw)
        buf[--n] = 0;
    while (n > 0 && buf[n - 1] == ' ')
        buf[--n] = 0;
    if (n > 2) {
        buf[n - 2] = '.';
        buf[n - 1] = '.';
    }
    if (n > 0)
        db_print(s, f, buf, x, y, fp, DB_FONT6_XSPACING);
}

/* Centred on a point, which is how every 1995 caption and column heading is placed. */
static void sk_centre(DB_Surface *s, const DB_Font *f, const char *text, int cx, int y,
                      const unsigned char *fp)
{
    if (!s || !f || !text || !*text)
        return;
    db_print(s, f, text, cx - (db_string_width(f, text, DB_FONT6_XSPACING) >> 1), y, fp,
             DB_FONT6_XSPACING);
}

static void sk_button(DB_Surface *s, const DB_Font *f, const char *label, int x, int y,
                      int w, int h, int pressed, int lit, int disabled)
{
    unsigned char fp[16];
    int i, tx;

    dm_green_button(s, x, y, w, h, pressed || lit, disabled);
    if (disabled) {
        for (i = 0; i < 16; i++)
            fp[i] = (i >= 4) ? DM_TEXT_DISABLED : DB_TBLACK;
    } else {
        db_font_palette_grad(fp, (pressed || lit) ? DM_TEXT_BRIGHT : DM_TEXT_MEDIUM,
                             DB_TBLACK);
    }
    if (!f)
        return;
    /* textbtn.cpp:359's own centring expression, verbatim. */
    tx = x + (w >> 1) - 1 - (db_string_width(f, label, DB_FONT6_XSPACING) >> 1);
    db_print(s, f, label, tx, y + 1, fp, DB_FONT6_XSPACING);
}

/* goptions.cpp:507 Draw_Caption: the filigree pair on the box's top corners, the text
 * centred, and a bright rule under it the exact width of the text. The pair is the main
 * menu's (frames 0 and 1), not the Options dialog's, because this screen is opened from
 * the main menu and stands on its plate. */
static void sk_caption(DB_Surface *s, const DB_Pack *p, const DB_Font *f, const char *text)
{
    const DB_Shape *sh = db_shape(p, "OPTIONS");
    unsigned char fp[16];
    int tw, tx;

    if (sh) {
        db_draw_shape_centered(s, sh, DM_FILIGREE_LEFT, SK_DLG_X + 12, SK_DLG_Y + 11);
        db_draw_shape_centered(s, sh, DM_FILIGREE_RIGHT, SK_DLG_X + SK_DLG_W - 14,
                               SK_DLG_Y + 11);
    }
    if (!f)
        return;
    db_font_palette_grad(fp, DM_TEXT_BRIGHT, DB_TBLACK);
    tw = db_string_width(f, text, DB_FONT6_XSPACING);
    tx = SK_DLG_X + (SK_DLG_W >> 1) - (tw >> 1);
    db_print(s, f, text, tx, SK_CAPTION_Y, fp, DB_FONT6_XSPACING);
    /* goptions.cpp:585-589: the rule sits FontHeight + FontYSpacing under the text. */
    db_line_h(s, tx, tx + tw, SK_CAPTION_Y + f->maxh + DB_FONT6_YSPACING,
              SK_BRIGHT_GREEN);
}

/* A column heading, centred over its box the way the 1995 dialogs centre theirs. */
static void sk_column_label(DB_Surface *s, const DB_Font *f, const char *text, int cx)
{
    unsigned char fp[16];
    if (!f)
        return;
    db_font_palette_grad(fp, DM_TEXT_MEDIUM, DB_TBLACK);
    sk_centre(s, f, text, cx, SK_LABEL_Y, fp);
}

/* ONE COLOUR SQUARE, which is the whole of the picker's vocabulary: a filled rectangle
 * of that colour, and a RIM when it is the chosen one. No glyph goes near it -- the project owner's
 * ruling is that a colour is shown and never named -- so the rim is the only thing a
 * square can say about itself and it has to be unmistakable.
 *
 * THE RIM IS WHITE AND NOT THE DIALOG'S OWN BRIGHT GREEN, which was the first thing
 * tried and is a measured mistake: SK_BRIGHT_GREEN is index 167, and index 167 is also
 * player colour 3, so the mark on the green square was drawn in the colour of the green
 * square and vanished. White is the one ink in this palette that is far from all eight
 * -- the nearest of them is the pale blue-grey of colour 1 at 195,195,211 -- and the
 * button under it is drawn held down as well, so the mark has two independent cues.
 * A locked square would take the disabled grey; it never can be marked, because the one
 * square a row locks is the colour that row is NOT wearing.
 * The rim sits OUTSIDE the fill rather than over it, so a marked square shows exactly as
 * much colour as an unmarked one. */
static void sk_swatch(DB_Surface *s, int x, int y, int w, int h, int colour, int marked,
                      int disabled)
{
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, sk_colour_index(colour));
    if (!marked)
        return;
    {
        const unsigned char rim = disabled ? DM_TEXT_DISABLED : DB_WHITE;
        db_line_h(s, x - 1, x + w, y - 1, rim);
        db_line_h(s, x - 1, x + w, y + h, rim);
        db_line_v(s, x - 1, y - 1, y + h, rim);
        db_line_v(s, x + w, y - 1, y + h, rim);
    }
}

/* The roster, which is a CONTROL now and not only a readout (26 Aug 2026): each row is
 * clickable and opens the drop down that sets that seat's faction, team and colour. It
 * can never exceed eight rows, so there is still no scroll.
 *
 * Row format is Tiberian Dawn's own, name then a tab stop then the side, with a colour
 * square in FRONT of the name and one column added on the end for the team number. 1995
 * tinted the whole row and drew no square (netdlg.cpp:3063-3070 prints through a
 * ColorListClass), which is legible right up until two of the eight are colours that six
 * pixel text cannot tell apart. The row is still tinted; the square is what actually
 * carries the colour, and it is what the player clicks. */
static void sk_draw_roster(DB_Surface *s, const DB_Font *f, const SK_State *st)
{
    unsigned char fp[16];
    char name[24], num[8];
    int i, y, players;

    dm_green_dialog(s, SK_ROSTER_X - 2, SK_ROSTER_Y - 2, SK_ROSTER_W + 4,
                    SK_ROSTER_H + 4);
    if (!f)
        return;
    players = sk_players(st);
    for (i = 0; i < players; i++) {
        /* A computer nobody has touched still takes the opposite side to the human and
         * still follows the side buttons; sk_row_house is the only thing that knows. */
        const int side = sk_row_house(st, i);
        const int col = sk_row_colour(st, i);
        y = SK_ROSTER_Y + i * SK_ROSTER_ROW_H;
        /* list.cpp:236-243's own selected row: the focused seat, or the one whose drop
         * down is open, is filled the way a selected map row is. */
        if (st->popup == i || st->selected == SK_ROW_ITEM(i))
            db_fill_rect(s, SK_ROSTER_X, y, SK_ROSTER_X + SK_ROSTER_W - 1,
                         y + SK_ROSTER_ROW_H - 1, DM_GREEN_BKGD);
        sk_swatch(s, SK_ROSTER_X + SK_ROSTER_SW_X, y + 1, SK_ROSTER_SW, SK_ROSTER_SW,
                  col, 0, 0);
        db_font_palette_grad(fp, sk_colour_index(col), DB_TBLACK);
        sk_seat_name(st, i, name, (int)sizeof name);
        sk_print_fit(s, f, name, SK_ROSTER_X + SK_ROSTER_NAME, y, SK_ROSTER_NAME_W, fp);
        sk_print_fit(s, f, side ? "NOD" : "GDI", SK_ROSTER_X + SK_ROSTER_TAB, y,
                     SK_ROSTER_SIDE_W, fp);
        snprintf(num, sizeof num, "%d", sk_row_team(st, i) + 1);
        sk_print_fit(s, f, num, SK_ROSTER_X + SK_ROSTER_TEAM, y, SK_ROSTER_TEAM_W, fp);
    }
}

/* THE DROP DOWN. Drawn LAST of everything on the screen, which with one immediate pass
 * into one surface and no z ordering is the whole of what "on top" means here. */
static void sk_draw_popup(DB_Surface *s, const DB_Font *f, const SK_State *st)
{
    unsigned char fp[16];
    char name[24];
    int bx, by, bw, bh, x, y, w, h, i;

    if (!st || st->popup < 0)
        return;
    sk_popup_box(st, &bx, &by, &bw, &bh);
    dm_green_dialog(s, bx, by, bw, bh);
    if (!f)
        return;

    /* WHOSE SEAT THIS IS, in that seat's own roster colour, so the box and the row it
     * came out of are one thing and not two. */
    db_font_palette_grad(fp, sk_colour_index(sk_row_colour(st, st->popup)), DB_TBLACK);
    sk_seat_name(st, st->popup, name, (int)sizeof name);
    sk_print_fit(s, f, name, bx + SK_POP_PAD, by + SK_POP_PAD,
                 SK_POP_W - 2 * SK_POP_PAD, fp);

    db_font_palette_grad(fp, DM_TEXT_MEDIUM, DB_TBLACK);
    sk_print_fit(s, f, "SIDE", bx + SK_POP_PAD,
                 by + SK_POP_PAD + SK_POP_HDR_H, SK_POP_LAB_W - 2, fp);
    sk_print_fit(s, f, "TEAM", bx + SK_POP_PAD,
                 by + SK_POP_PAD + SK_POP_HDR_H + SK_POP_ROW_H, SK_POP_LAB_W - 2, fp);
    /* The caption names the QUESTION. The eight answers under it carry no text at all,
     * which is the ruling: a colour is shown, never named. */
    sk_print_fit(s, f, "COLOUR", bx + SK_POP_PAD,
                 by + SK_POP_PAD + SK_POP_HDR_H + SK_POP_ROW_H * 2, SK_POP_LAB_W - 2, fp);

    /* All three rows are latched button strips: the chosen one is drawn held down, which
     * is Turn_On and the pressed colourway, the same grammar the GDI/Nod pair uses. The
     * colour row is the same button with a filled square where the caption would go. */
    for (i = SK_I_POP_GDI; i <= SK_I_POP_C8; i++) {
        int lit;
        if (!sk_item_rect(st, i, &x, &y, &w, &h))
            continue;
        if (SK_IS_POP_COL(i)) {
            const int c = i - SK_I_POP_C1;
            const int dis = sk_item_disabled(st, i);
            lit = (sk_row_colour(st, st->popup) == c);
            dm_green_button(s, x, y, w, h, st->pressed == i || lit, dis);
            /* Inside the bevel: 8 wide by 5 tall on a 12x9 button, which is the button
             * less its two pixel border on every side. The rim the mark draws sits in
             * that border and so cannot eat into the colour. */
            sk_swatch(s, x + 2, y + 2, w - 4, h - 4, c, lit, dis);
            continue;
        }
        if (i == SK_I_POP_GDI || i == SK_I_POP_NOD)
            lit = (sk_row_house(st, st->popup) == (i == SK_I_POP_NOD ? 1 : 0));
        else
            lit = (sk_row_team(st, st->popup) == i - SK_I_POP_T1);
        sk_button(s, f, sk_item_label(st, i), x, y, w, h, st->pressed == i, lit, 0);
    }
}

static void sk_draw_maplist(DB_Surface *s, const DB_Font *f, const SK_State *st)
{
    unsigned char fp[16], fpsel[16], fpdim[16];
    int row, i, y;

    dm_green_dialog(s, SK_MAP_X - 2, SK_MAPLIST_Y - 2, SK_MAP_W + 4, SK_MAP_H + 4);
    if (!f)
        return;
    db_font_palette_grad(fp, DM_TEXT_MEDIUM, DB_TBLACK);
    db_font_palette_grad(fpsel, DM_TEXT_BRIGHT, DB_TBLACK);
    db_font_palette_grad(fpdim, DM_TEXT_DISABLED, DB_TBLACK);

    /* The two tabs. The inactive one is drawn dim rather than hidden, so the split is
       visible before it is used and an empty User Maps tab still says it exists. */
    {
        int t;
        for (t = 0; t < 2; t++) {
            const int tx = SK_TAB_X(t);
            const int tw = SK_TAB_W(t);
            const int on = (t == st->tab);
            if (on)
                db_fill_rect(s, tx, SK_TAB_Y, tx + tw - 1,
                             SK_TAB_Y + SK_TAB_H - 1, DM_GREEN_BKGD);
            sk_print_fit(s, f, sk_tab_label(t), tx + SK_TAB_INSET, SK_TAB_Y + 1,
                         tw - 2 * SK_TAB_INSET, on ? fpsel : fpdim);
        }
    }

    if (sk_tab_count(st) <= 0) {
        sk_print_fit(s, f,
                     st->tab ? "NO USER MAPS YET" : "NO MAPS INSTALLED",
                     SK_MAP_X + 3, SK_MAPLIST_Y + 2, SK_MAP_W - 6, fpdim);
        return;
    }
    for (row = 0; row < SK_MAP_ROWS; row++) {
        const SK_Map *m;
        const unsigned char *pal;
        i = sk_map_at(st, st->top + row);
        if (i < 0)
            break;
        m = &st->maps[i];
        y = SK_MAPLIST_Y + row * SK_ROW_H;
        /* list.cpp:236-243 fills the selected line and prints it bright. */
        if (i == st->sel) {
            db_fill_rect(s, SK_MAP_X, y, SK_MAP_X + SK_MAP_W - 1, y + SK_ROW_H - 1,
                         DM_GREEN_BKGD);
            pal = fpsel;
        } else {
            pal = fp;
        }
        sk_print_fit_cut(s, f, (m->name && *m->name) ? m->name : m->scen, SK_MAP_X + 3, y,
                     SK_MAP_W - 6, pal);
    }
}

/* Three facts about the selected map, read out of the same INI the list read its name
 * from: theater, size in cells, and how many players it can seat. */
static void sk_draw_info(DB_Surface *s, const DB_Font *f, const SK_State *st)
{
    unsigned char fp[16];
    char line[80];
    const SK_Map *m;

    if (!f || st->sel < 0 || st->sel >= st->count)
        return;
    m = &st->maps[st->sel];
    db_font_palette_grad(fp, DM_TEXT_MEDIUM, DB_TBLACK);
    if (m->w > 0 && m->h > 0 && m->starts > 0)
        snprintf(line, sizeof line, "%s  %dx%d  %d starts",
                 (m->theater && *m->theater) ? m->theater : "?", m->w, m->h, m->starts);
    else if (m->w > 0 && m->h > 0)
        snprintf(line, sizeof line, "%s  %dx%d",
                 (m->theater && *m->theater) ? m->theater : "?", m->w, m->h);
    else
        snprintf(line, sizeof line, "%s", m->scen);
    sk_print_fit(s, f, line, SK_INFO_X, SK_INFO_Y, SK_INFO_W, fp);
}

/* One start marker: the preview baker's own shape, a filled diamond of radius 2 inside
 * an opaque black rim of radius 3, so it reads on ground of any colour. */
static void sk_draw_marker(DB_Surface *s, int cx, int cy, unsigned char colour)
{
    int dx, dy, d, px, py;
    for (dy = -3; dy <= 3; dy++)
        for (dx = -3; dx <= 3; dx++) {
            d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (d > 3)
                continue;
            px = cx + dx;
            py = cy + dy;
            /* CLIPPED TO THE PANEL, not to the surface. A start on the very edge of the
             * map puts its centre one pixel inside the frame, and db_put_pixel would
             * happily paint the rest of the diamond over the panel's own border. */
            if (px < SK_PREV_X || px >= SK_PREV_X + SK_PREV_W || py < SK_PREV_Y ||
                py >= SK_PREV_Y + SK_PREV_H)
                continue;
            db_put_pixel(s, px, py, (unsigned char)(d > 2 ? SK_START_RIM : colour));
        }
}

/* THE PREVIEW PANEL.
 *
 * The frame in the pack is a fixed square whatever shape the map is, with the margin in
 * index 0, which the blitter treats as transparent -- so the map simply does not paint
 * where it is not. The frame is bigger than the panel, so this is a point sample down;
 * at this size a cell is about one pixel, which is the resolution the in-game radar
 * draws the same map at.
 *
 * The markers are drawn HERE rather than taken from the pack's second, pre-marked plane,
 * because the pack records each start's pixel position and the lobby knows who is
 * sitting in it: the first players+1 diamonds are the roster's own colours, in the
 * roster's own order, and the rest are the room the map has left. That is the whole
 * reason the baker writes two planes.
 */
static void sk_draw_preview(DB_Surface *s, const DB_Pack *p, const DB_Font *f,
                            const SK_State *st)
{
    unsigned char fpdim[16];
    const unsigned char *r, *plane;
    const SK_Prev *pv = st->prev;
    int rec = -1, sx, sy, dx, dy, starts, shown, i, players;

    (void)p;
    dm_green_dialog(s, SK_PREVBOX_X, SK_PREVBOX_Y, SK_PREVBOX_W, SK_PREVBOX_H);

    if (pv && st->sel >= 0 && st->sel < st->count)
        rec = sk_prev_find(pv, st->maps[st->sel].scen);
    if (rec < 0) {
        /* A missing pack degrades to an empty panel that SAYS it is empty. It never
         * degrades to a silent grey rectangle, and it never stops the lobby. */
        if (f) {
            db_font_palette_grad(fpdim, DM_TEXT_DISABLED, DB_TBLACK);
            sk_centre(s, f, "no map", SK_PREV_X + SK_PREV_W / 2,
                      SK_PREV_Y + (SK_PREV_H / 2) - 8, fpdim);
            sk_centre(s, f, "preview", SK_PREV_X + SK_PREV_W / 2,
                      SK_PREV_Y + (SK_PREV_H / 2), fpdim);
        }
        return;
    }

    r = sk_prev_rec(pv, rec);
    plane = r + SK_PR_MARKS + (long)pv->max_starts * SK_PR_MARK_BYTES;
    for (dy = 0; dy < SK_PREV_H; dy++) {
        sy = dy * pv->fh / SK_PREV_H;
        for (dx = 0; dx < SK_PREV_W; dx++) {
            unsigned char v;
            sx = dx * pv->fw / SK_PREV_W;
            v = plane[(long)sy * pv->fw + sx];
            if (v)
                db_put_pixel(s, SK_PREV_X + dx, SK_PREV_Y + dy, v);
        }
    }

    starts = sk_rd16(r + SK_PR_STARTS);
    if (starts > pv->max_starts)
        starts = pv->max_starts;
    /* Never more diamonds than the map has legal starts: the pack lists every waypoint
     * under 26 and the engine can only reach the contiguous run from 0, so the caller's
     * measured count is the one that decides. */
    shown = starts;
    if (st->sel >= 0 && st->maps[st->sel].starts > 0 &&
        st->maps[st->sel].starts < shown)
        shown = st->maps[st->sel].starts;
    players = st->ai + 1;
    for (i = 0; i < shown; i++) {
        const unsigned char *mk = r + SK_PR_MARKS + (long)i * SK_PR_MARK_BYTES;
        const int px = sk_rd16(mk + 4);
        const int py = sk_rd16(mk + 6);
        /* THE SEAT'S CHOSEN COLOUR, not the seat index: the diamond on the map and the
         * square in the roster are the same player, so they have to move together. A
         * start with nobody in it stays the idle grey. */
        const unsigned char col = (i < players && i < SK_ROSTER_ROWS)
                                     ? sk_colour_index(sk_row_colour(st, i))
                                     : SK_START_IDLE;
        sk_draw_marker(s, SK_PREV_X + px * SK_PREV_W / pv->fw,
                       SK_PREV_Y + py * SK_PREV_H / pv->fh, col);
    }
}

/* gauge.cpp:205-240 Draw_Me: a GREEN_DOWN body, the travelled part filled bright, then
 * a 4 pixel raised thumb pulled back at the top of travel so it cannot hang off. */
static void sk_draw_gauge(DB_Surface *s, const DB_Font *f, const SK_State *st, int ctrl)
{
    unsigned char fp[16];
    char num[16];
    const char *label = sk_item_label(st, ctrl);
    int x, y, w, h, mid, thumb, top, lo, hi, step;
    const int dis = sk_item_disabled(st, ctrl);

    if (!sk_item_rect(st, ctrl, &x, &y, &w, &h))
        return;
    sk_gauge_range(st, ctrl, &lo, &hi, &step);

    /* BOXSTYLE_GREEN_DOWN, dialog.cpp row 6: filler BKGD, shadow LIGHT, hilite SHADOW. */
    db_fill_rect(s, x, y, x + w - 1, y + h - 1, dis ? DM_DIS_FILL : DM_GREEN_BKGD);
    db_line_h(s, x, x + w - 1, y + h - 1, dis ? DM_DIS_HILITE : DM_LIGHT_GREEN);
    db_line_v(s, x + w - 1, y, y + h - 1, dis ? DM_DIS_HILITE : DM_LIGHT_GREEN);
    db_line_h(s, x, x + w - 1, y, dis ? DM_DIS_SHADOW : DM_GREEN_SHADOW);
    db_line_v(s, x, y, y + h - 1, dis ? DM_DIS_SHADOW : DM_GREEN_SHADOW);

    mid = sk_value_to_pixel(st, ctrl, sk_value(st, ctrl));
    if (mid >= x + 1 && !dis)
        db_fill_rect(s, x + 1, y + 1, mid, y + h - 2, SK_BRIGHT_GREEN);
    top = sk_value_to_pixel(st, ctrl, hi);
    thumb = mid;
    if (thumb + 4 > top)
        thumb = top - 2;
    /* AND NEVER LEFT OF THE BOX. When a gauge's range collapses to a single value, which
     * is what the opponent count does on a map with room for one opponent, top is the
     * box's own left edge and the pull-back above puts the thumb two pixels OUTSIDE the
     * control, in the gutter its right-aligned caption ends in. Measured on a two-start
     * map, not suspected: the thumb painted from x-2 while the box began at x. */
    if (thumb < x)
        thumb = x;
    dm_green_button(s, thumb, y, 4, h, 0, dis);

    if (!f)
        return;
    /* sounddlg.cpp:332-343 labels a narrow slider RIGHT ALIGNED to its left and two
     * rows up; the printed value is the StaticButtonClass Red Alert puts to the right
     * of every one of these gauges (nulldlg.cpp:1560, :1565, :1570, :1575). */
    db_font_palette_grad(fp, (st->selected == ctrl && !dis) ? DM_TEXT_BRIGHT
                                                            : DM_TEXT_MEDIUM,
                         DB_TBLACK);
    db_print(s, f, label, SK_GLABEL_R - db_string_width(f, label, DB_FONT6_XSPACING),
             y - 1, fp, DB_FONT6_XSPACING);
    snprintf(num, sizeof num, "%d", sk_value(st, ctrl));
    sk_print_fit(s, f, num, SK_READ_X, y - 1, SK_CHK_X - SK_READ_X - 2, fp);
}

/* A CHECK BOX, and there is no checkbox primitive in the 1995 toolkit because 1995 had
 * none: this is the dialog's own inset well -- the same pressed button box the gauge
 * thumb uses -- with the bright green the gauge fills its travel with. A ticked box
 * therefore reads like a pressed control and an unticked one like a raised one, which
 * is the only visual grammar this screen has. */
static void sk_draw_check(DB_Surface *s, const DB_Font *f, const SK_State *st, int item,
                          int on)
{
    unsigned char fp[16];
    int x, y, w, h, i;
    const int dis = sk_item_disabled(st, item);

    if (!sk_item_rect(st, item, &x, &y, &w, &h))
        return;
    dm_green_button(s, x, y, SK_CHK_BOX, SK_CHK_BOX, on, dis);
    if (on)
        db_fill_rect(s, x + 2, y + 2, x + SK_CHK_BOX - 3, y + SK_CHK_BOX - 3,
                     dis ? DM_DIS_HILITE : SK_BRIGHT_GREEN);
    if (!f)
        return;
    if (dis) {
        for (i = 0; i < 16; i++)
            fp[i] = (i >= 4) ? DM_TEXT_DISABLED : DB_TBLACK;
    } else {
        db_font_palette_grad(fp, (st->selected == item) ? DM_TEXT_BRIGHT
                                                        : DM_TEXT_MEDIUM,
                             DB_TBLACK);
    }
    sk_print_fit(s, f, sk_item_label(st, item), SK_CHK_LABEL_X, y - 1,
                 SK_CHK_X + SK_CHK_ROW_W - SK_CHK_LABEL_X, fp);
}

void sk_draw(DB_Surface *s, const DB_Pack *p, const SK_State *st)
{
    const DB_Font *grad;
    unsigned char fp[16];
    int i;

    if (!s || !p || !st)
        return;
    grad = db_font(p, "GRAD6FNT");

    /* The plate stays where the caller left it; the lobby sits in its own dialog on
     * top, which is the same dialog the mission list draws. */
    dm_green_dialog(s, SK_DLG_X, SK_DLG_Y, SK_DLG_W, SK_DLG_H);
    sk_caption(s, p, grad, "MULTIPLAYER GAME");

    sk_column_label(s, grad, "Players", (SK_ROSTER_X - 2) + ((SK_ROSTER_W + 4) >> 1));
    sk_column_label(s, grad, "Battlefield",
                    ((SK_MAP_X - 2) + (SK_PREVBOX_X + SK_PREVBOX_W)) >> 1);

    sk_draw_roster(s, grad, st);
    sk_draw_maplist(s, grad, st);
    sk_draw_preview(s, p, grad, st);
    sk_draw_info(s, grad, st);

    /* The side pair. Both are ordinary buttons; the chosen one is drawn held down,
     * which is Turn_On plus the pressed colourway and nothing else. */
    sk_button(s, grad, sk_item_label(st, SK_I_GDI), SK_GDI_X, SK_SIDE_Y, SK_SIDE_W,
              SK_SIDE_H, st->pressed == SK_I_GDI, st->side == 0, 0);
    sk_button(s, grad, sk_item_label(st, SK_I_NOD), SK_NOD_X, SK_SIDE_Y, SK_SIDE_W,
              SK_SIDE_H, st->pressed == SK_I_NOD, st->side == 1, 0);

    /* EVERY GAUGE, and the bound is sk_is_gauge's own. It used to stop at Credits while
     * SK_I_UNITS already had a range, a rectangle, a caption, a mouse arm, a keyboard arm
     * and a field in the result, so Unit Count was a live control that drew nothing: Tab
     * reached it, the arrows moved it, and the value was visible nowhere. */
    for (i = SK_I_AI; i <= SK_I_UNITS; i++)
        sk_draw_gauge(s, grad, st, i);
    sk_draw_check(s, grad, st, SK_I_BASES, 1);
    sk_draw_check(s, grad, st, SK_I_TIBERIUM, 1);
    sk_draw_check(s, grad, st, SK_I_SUPER, st->super);
    /* THE SAME OMISSION ONE COLUMN OVER. SK_I_CRATES is not locked, takes clicks and Space,
     * and writes MPlayerGoodies, so leaving it undrawn made crates a setting the player
     * could switch by accident and could not read. */
    sk_draw_check(s, grad, st, SK_I_CRATES, st->crates);

    if (grad) {
        db_font_palette_grad(fp, DM_TEXT_MEDIUM, DB_TBLACK);
        /* WITH NOTHING TO PLAY, NOTHING ELSE THIS ROW COULD SAY IS WORTH SAYING. Every
         * other status line describes a match, and on an empty list there is no match
         * to describe: the row would open talking about sides and then answer clicks
         * about gauges while the only fact that matters sat in the map box. So the
         * empty state owns the row and no control on the screen can push it off. */
        if (st->count <= 0)
            sk_print_fit(s, grad, SK_STATUS[SK_ST_NO_MAPS], SK_STATUS_X, SK_STATUS_Y,
                         SK_STATUS_W, fp);
        else if (st->status)
            sk_print_fit(s, grad, st->status, SK_STATUS_X, SK_STATUS_Y, SK_STATUS_W, fp);
        db_font_palette_grad(fp, DM_TEXT_DISABLED, DB_TBLACK);
        sk_print_fit(s, grad, SK_STATUS[SK_ST_FOOT], SK_STATUS_X, SK_STATUS_Y2,
                     SK_STATUS_W, fp);
    }

    sk_button(s, grad, sk_item_label(st, SK_I_PLAY), SK_PLAY_X, SK_BTN_Y, SK_PLAY_W,
              SK_BTN_H, st->pressed == SK_I_PLAY, st->selected == SK_I_PLAY,
              sk_item_disabled(st, SK_I_PLAY));
    sk_button(s, grad, sk_item_label(st, SK_I_CANCEL), SK_CANCEL_X, SK_BTN_Y,
              SK_CANCEL_W, SK_BTN_H, st->pressed == SK_I_CANCEL,
              st->selected == SK_I_CANCEL, 0);

    /* LAST, over everything, because that is what "a drop down floats" means in a
     * renderer with one pass and no z buffer. Its hit test is first for the same
     * reason, and the two are the whole cost of the widget. */
    sk_draw_popup(s, grad, st);
}

/* ------------------------------------------------------------------------ *
 * THE LAYOUT CHECK.
 *
 * db_print clips to the SURFACE and never to a caller's box: a string wider than the
 * column it is printed in runs across whatever is beside it and off the dialog, and
 * this project has shipped exactly that once already, on a dialog where every
 * individual number was right. sk_print_fit truncates rather than overflowing, so the
 * failure mode here is not a mess on screen -- it is a map name that silently reads
 * "LAKEFRONT CL" and nobody notices.
 *
 * So the widths are ASSERTED rather than eyeballed. Every string this screen can print
 * is measured against the room it has, including all nine map names and every line
 * either status row can carry, and every one that does not fit is named. The count of
 * measurements is reported too, because a check that quietly measures nothing passes.
 *
 * It draws nothing and it is not on any path a player takes; the harness calls it.
 * ------------------------------------------------------------------------ */
static int sk_fits(const DB_Font *f, const char *what, const char *text, int room,
                   int *checked)
{
    int w;
    if (!text || !*text)
        return 0;
    (*checked)++;
    w = db_string_width(f, text, DB_FONT6_XSPACING);
    if (w <= room)
        return 0;
    printf("LOBBY|OVERFLOW|%s|%d > %d|%s\n", what, w, room, text);
    return 1;
}

/* The same measurement for text the screen ABBREVIATES rather than clips: it is
   reported, so a long name is still visible in the log, but it does not count as a
   fault. The audit above exists to catch a name that "silently reads LAKEFRONT CL
   and nobody notices" -- its own words. A name drawn through sk_print_fit_cut ends
   in ".." and so is not silent, which is the condition the audit was asking for.
   Fixed labels and buttons stay strict: nothing abbreviates those, and a button
   whose caption does not fit is still a bug. */
static int sk_fits_abbrev(const DB_Font *f, const char *what, const char *text,
                          int room, int *checked)
{
    int w;
    if (!text || !*text)
        return 0;
    (*checked)++;
    w = db_string_width(f, text, DB_FONT6_XSPACING);
    if (w > room)
        printf("LOBBY|abbrev|%s|%d > %d|%s\n", what, w, room, text);
    return 0;
}

int sk_check_layout(const DB_Pack *p, const SK_State *st)
{
    const DB_Font *f;
    char line[80];
    int bad = 0, checked = 0, i, x, y, w, h;

    if (!p || !st)
        return -1;
    f = db_font(p, "GRAD6FNT");
    if (!f) {
        /* A missing font is exit code territory, not a default: a check that cannot
         * measure has not passed. */
        printf("LOBBY|OVERFLOW|font|GRAD6FNT is not in the pack\n");
        return -1;
    }

    /* The caption sits between the two filigree ornaments, which are centred 12 in from
     * each end and are about 20 wide. */
    bad += sk_fits(f, "caption", "MULTIPLAYER GAME", SK_DLG_W - 60, &checked);
    bad += sk_fits(f, "label.players", "Players", SK_ROSTER_W + 4, &checked);
    bad += sk_fits(f, "label.battlefield", "Battlefield",
                   SK_PREVBOX_X + SK_PREVBOX_W - SK_MAP_X, &checked);

    /* THE ROSTER, every seat and every column, at the widest each can be. Eight rows
     * since the 8-player patch, so the longest name is COMPUTER 7 and not COMPUTER 5 --
     * the loop that stopped at five was measuring a roster two rows shorter than the one
     * being drawn. Each row is a name, a three letter side and a team digit, and each of
     * the three has its own box. */
    for (i = 0; i < SK_ROSTER_ROWS; i++) {
        sk_seat_name(st, i, line, (int)sizeof line);
        bad += sk_fits(f, "roster.name", line, SK_ROSTER_NAME_W, &checked);
    }
    bad += sk_fits(f, "roster.side", "NOD", SK_ROSTER_SIDE_W, &checked);
    bad += sk_fits(f, "roster.side", "GDI", SK_ROSTER_SIDE_W, &checked);
    for (i = 1; i <= SK_POP_TEAMS; i++) {
        snprintf(line, sizeof line, "%d", i);
        bad += sk_fits(f, "roster.team", line, SK_ROSTER_TEAM_W, &checked);
    }
    /* THE ROW ADDS UP, which no string measurement can catch: a swatch went in front of
     * three columns that already filled the row to the pixel, and the five it needed came
     * out of the insets. Every column has to start after the one before it ends and the
     * last one has to end inside the row. Written as the sum rather than as four
     * comparisons so the failure names the number that is wrong. */
    {
        const int used = SK_ROSTER_SW_X + SK_ROSTER_SW;
        checked++;
        if (used > SK_ROSTER_NAME ||
            SK_ROSTER_NAME + SK_ROSTER_NAME_W > SK_ROSTER_TAB ||
            SK_ROSTER_TAB + SK_ROSTER_SIDE_W > SK_ROSTER_TEAM ||
            SK_ROSTER_TEAM + SK_ROSTER_TEAM_W > SK_ROSTER_W) {
            printf("LOBBY|OVERFLOW|roster.row|swatch ends %d, name %d+%d, side %d+%d, "
                   "team %d+%d, row is %d wide\n",
                   used, SK_ROSTER_NAME, SK_ROSTER_NAME_W, SK_ROSTER_TAB,
                   SK_ROSTER_SIDE_W, SK_ROSTER_TEAM, SK_ROSTER_TEAM_W, SK_ROSTER_W);
            bad++;
        }
    }

    /* THE DROP DOWN. Every string on it, then the box itself.
     *
     * The box is measured because a popup is the one thing on this screen whose position
     * is computed rather than written down, and a string that fits a box which hangs off
     * the plate has passed a check that proved nothing. Every seat is tried, because the
     * anchor is the row and the last row is the one that can fall out of the dialog. */
    for (i = 0; i < SK_ROSTER_ROWS; i++) {
        sk_seat_name(st, i, line, (int)sizeof line);
        bad += sk_fits(f, "popup.header", line, SK_POP_W - 2 * SK_POP_PAD, &checked);
    }
    bad += sk_fits(f, "popup.caption", "SIDE", SK_POP_LAB_W - 2, &checked);
    bad += sk_fits(f, "popup.caption", "TEAM", SK_POP_LAB_W - 2, &checked);
    /* The one new caption on this screen. The eight squares under it have no string to
     * measure, which is the point of them. */
    bad += sk_fits(f, "popup.caption", "COLOUR", SK_POP_LAB_W - 2, &checked);
    bad += sk_fits(f, "popup.side", "GDI", SK_POP_SIDE_W - 4, &checked);
    bad += sk_fits(f, "popup.side", "Nod", SK_POP_SIDE_W - 4, &checked);
    for (i = 1; i <= SK_POP_TEAMS; i++) {
        snprintf(line, sizeof line, "%d", i);
        bad += sk_fits(f, "popup.team", line, SK_POP_TEAM_W - 4, &checked);
    }
    {
        SK_State probe = *st;
        for (i = 0; i < SK_ROSTER_ROWS; i++) {
            int bx, by, bw, bh;
            probe.popup = i;
            sk_popup_box(&probe, &bx, &by, &bw, &bh);
            checked++;
            if (bx >= SK_DLG_X + 2 && by >= SK_DLG_Y + 2 &&
                bx + bw <= SK_DLG_X + SK_DLG_W - 2 &&
                by + bh <= SK_DLG_Y + SK_DLG_H - 2)
                continue;
            printf("LOBBY|OVERFLOW|popup.box|row %d at %d,%d %dx%d leaves the dialog\n",
                   i, bx, by, bw, bh);
            bad++;
        }
        /* And every control inside it lands inside it. */
        probe.popup = SK_ROSTER_ROWS - 1;
        for (i = SK_I_POP_GDI; i <= SK_I_POP_C8; i++) {
            int bx, by, bw, bh, cx, cy, cw, ch;
            sk_popup_box(&probe, &bx, &by, &bw, &bh);
            if (!sk_item_rect(&probe, i, &cx, &cy, &cw, &ch))
                continue;
            checked++;
            if (cx >= bx && cy >= by && cx + cw <= bx + bw && cy + ch <= by + bh)
                continue;
            printf("LOBBY|OVERFLOW|popup.item|%d at %d,%d %dx%d leaves its box\n", i, cx,
                   cy, cw, ch);
            bad++;
        }
    }

    /* THE TWO MAP-SOURCE TABS, which nothing measured until 26 Aug 2026 and which did
     * not fit: this audit's own words are that the failure it exists to catch is a label
     * that "silently reads LAKEFRONT CL and nobody notices", and OFFICIA and USER MAP
     * were on the screen the whole time. */
    for (i = 0; i < 2; i++)
        bad += sk_fits(f, "tab.label", sk_tab_label(i), SK_TAB_W(i) - 2 * SK_TAB_INSET,
                       &checked);
    bad += sk_fits(f, "maplist.empty", "NO USER MAPS YET", SK_MAP_W - 6, &checked);
    bad += sk_fits(f, "maplist.empty", "NO MAPS INSTALLED", SK_MAP_W - 6, &checked);

    /* Every map that is actually installed, and its info line with it. */
    for (i = 0; i < st->count; i++) {
        const SK_Map *m = &st->maps[i];
        const char *nm = (m->name && *m->name) ? m->name : m->scen;
        bad += sk_fits_abbrev(f, "map.name", nm, SK_MAP_W - 6, &checked);
        snprintf(line, sizeof line, "%s  %dx%d  %d starts",
                 (m->theater && *m->theater) ? m->theater : "?", m->w, m->h, m->starts);
        bad += sk_fits(f, "map.info", line, SK_INFO_W, &checked);
    }

    /* The gauge captions are right aligned to end at SK_GLABEL_R, so their room is the
     * distance from the dialog's own content edge to that column. */
    for (i = SK_I_AI; i <= SK_I_UNITS; i++)
        bad += sk_fits(f, "gauge.label", sk_item_label(st, i),
                       SK_GLABEL_R - (SK_DLG_X + 6), &checked);
    /* The printed value at the widest either gauge can show. */
    bad += sk_fits(f, "gauge.value", "10000", SK_CHK_X - SK_READ_X - 2, &checked);

    for (i = SK_I_BASES; i <= SK_I_CRATES; i++)
        bad += sk_fits(f, "check.label", sk_item_label(st, i),
                       SK_CHK_X + SK_CHK_ROW_W - SK_CHK_LABEL_X, &checked);

    /* textbtn.cpp:81 sizes a button to its label plus 8, so a label needs the button's
     * width less that margin. */
    for (i = 0; i < SK_I_COUNT; i++) {
        /* Rows and drop down controls are measured above, against the columns and the
         * box they actually print into rather than against a button's margin. */
        if (i == SK_I_MAPS || sk_is_gauge(i) || (i >= SK_I_BASES && i <= SK_I_CRATES))
            continue;
        if (SK_IS_ROW(i) || SK_IS_POP(i))
            continue;
        if (!sk_item_rect(st, i, &x, &y, &w, &h))
            continue;
        bad += sk_fits(f, "button.label", sk_item_label(st, i), w - 8, &checked);
    }

    for (i = 0; i < SK_ST_COUNT; i++)
        bad += sk_fits(f, "status", SK_STATUS[i], SK_STATUS_W, &checked);

    /* THE ASSEMBLED STATUS LINE, which is not in the table above because it names a
     * seat. It is measured by BUILDING it -- the audit runs the same sk_say_seat the
     * screen runs -- at both of its widest: the longest seat name on the highest team
     * with every other seat allied to it, and the same with nobody allied to it. A
     * hand-copied worst case here would be a second copy of the wording to keep in
     * step, which is the failure this whole function exists to prevent. */
    {
        SK_State probe = *st;
        int k;
        probe.ai = SK_AI_MAX;
        probe.side = 1;
        for (k = 0; k < SK_ROSTER_ROWS; k++)
            probe.team[k] = SK_POP_TEAMS - 1;
        probe.house_set[SK_ROSTER_ROWS - 1] = 1;
        probe.house[SK_ROSTER_ROWS - 1] = 1;
        sk_say_seat(&probe, SK_ROSTER_ROWS - 1);
        bad += sk_fits(f, "status.seat.allied", probe.statusbuf, SK_STATUS_W, &checked);
        for (k = 0; k < SK_ROSTER_ROWS; k++)
            probe.team[k] = k;
        probe.team[SK_ROSTER_ROWS - 1] = SK_POP_TEAMS - 1;
        sk_say_seat(&probe, SK_ROSTER_ROWS - 1);
        bad += sk_fits(f, "status.seat.alone", probe.statusbuf, SK_STATUS_W, &checked);
        /* And the side line, which is assembled for the same reason once any row has
         * been given a faction by hand. Every count on it is one digit, so any filled
         * roster is its widest form. */
        sk_say_sides(&probe);
        bad += sk_fits(f, "status.sides", probe.statusbuf, SK_STATUS_W, &checked);
        /* THE COLOUR LINES, both of them, at their widest: two of the longest seat name
         * in the swap form, and one in the no-swap form. Built by running the same
         * sk_say_colour the screen runs, for the same reason as the two above. */
        sk_say_colour(&probe, SK_ROSTER_ROWS - 1, SK_ROSTER_ROWS - 1);
        bad += sk_fits(f, "status.colour.moved", probe.statusbuf, SK_STATUS_W, &checked);
        sk_say_colour(&probe, SK_ROSTER_ROWS - 1, -1);
        bad += sk_fits(f, "status.colour.free", probe.statusbuf, SK_STATUS_W, &checked);
    }

    /* ------------------------------------------------------------------ *
     * THE COLOUR RULE, which is a STATE check and not a string one, and which lives here
     * because this is the one function the harness already calls to be told that
     * something it cannot see is still true.
     *
     * It is proved twice, and the first is the case the project owner asked for IN HIS OWN TERMS: "If
     * the player picks a color already used by an ai opponent, that opponents color
     * should automatically change to another." So the table is printed before, the
     * player is given a colour a computer is holding, and the table is printed after --
     * legibly, as eight numbers, so the swap can be read rather than believed.
     * ------------------------------------------------------------------ */
    {
        SK_State probe = *st;
        int k, seen[SK_PLAYER_COLOUR_N], dup = 0, moved, want, hadit;
        char before[64], after[64];
        /* From the defaults, so the line reads the same every run. Seat order through
         * the eight, which is what sk_init writes. */
        for (k = 0; k < SK_ROSTER_ROWS; k++)
            probe.colour[k] = k % SK_PLAYER_COLOUR_N;
        for (k = 0; k < SK_ROSTER_ROWS; k++)
            snprintf(before + k * 2, sizeof before - (size_t)k * 2, "%d%s",
                     probe.colour[k], k + 1 < SK_ROSTER_ROWS ? "," : "");
        /* The colour the LAST seat is wearing, which is the furthest thing from the
         * human's own and so cannot be a swap that happens to be a no-op. */
        want = probe.colour[SK_ROSTER_ROWS - 1];
        hadit = SK_ROSTER_ROWS - 1;
        moved = sk_set_row_colour(&probe, 0, want);
        for (k = 0; k < SK_ROSTER_ROWS; k++)
            snprintf(after + k * 2, sizeof after - (size_t)k * 2, "%d%s",
                     probe.colour[k], k + 1 < SK_ROSTER_ROWS ? "," : "");
        for (k = 0; k < SK_PLAYER_COLOUR_N; k++)
            seen[k] = 0;
        for (k = 0; k < SK_ROSTER_ROWS; k++) {
            const int v = probe.colour[k];
            if (v < 0 || v >= SK_PLAYER_COLOUR_N || seen[v])
                dup++;
            else
                seen[v] = 1;
        }
        checked++;
        printf("LOBBY|colour|seat 0 takes colour %d|before %s|after %s|moved seat %d|"
               "duplicates %d\n", want, before, after, moved, dup);
        if (dup || moved != hadit || probe.colour[0] != want) {
            printf("LOBBY|OVERFLOW|colour.swap|seat 0 asked for colour %d, holds %d; "
                   "seat %d was holding it and seat %d moved; %d duplicate(s)\n",
                   want, probe.colour[0], hadit, moved, dup);
            bad++;
        }
    }

    /* AND THE SAME RULE UNDER EXHAUSTION, because one case proved is one case proved.
     * This drives the real sk_set_row_colour over every seat and every colour -- every
     * change the picker can make, on a copy of the live state, nothing drawn, nothing
     * kept -- and after each one re-derives whether all eight assignments are still
     * distinct AND whether the human moved. The human moving as a side effect of a
     * change to a computer is the one failure that would look harmless on screen. */
    {
        SK_State probe = *st;
        int seat, c, k, seen[SK_PLAYER_COLOUR_N], dup = 0, humanmoved = 0, moves = 0;
        for (seat = 0; seat < SK_ROSTER_ROWS; seat++) {
            for (c = 0; c < SK_PLAYER_COLOUR_N; c++) {
                const int was0 = probe.colour[0];
                /* The screen never offers the human's own colour on another seat's row,
                 * so neither does this: asserting a rule the picker cannot reach would
                 * prove nothing about the picker. */
                if (seat != 0 && c == probe.colour[0])
                    continue;
                if (sk_set_row_colour(&probe, seat, c) >= 0)
                    moves++;
                if (seat != 0 && probe.colour[0] != was0)
                    humanmoved++;
                for (k = 0; k < SK_PLAYER_COLOUR_N; k++)
                    seen[k] = 0;
                for (k = 0; k < SK_ROSTER_ROWS; k++) {
                    const int v = probe.colour[k];
                    if (v < 0 || v >= SK_PLAYER_COLOUR_N || seen[v])
                        dup++;
                    else
                        seen[v] = 1;
                }
            }
        }
        checked++;
        if (dup || humanmoved || moves < 8) {
            printf("LOBBY|OVERFLOW|colour.unique|%d duplicate assignment(s), the human "
                   "was moved %d time(s), %d swap(s) happened\n", dup, humanmoved, moves);
            bad++;
        } else {
            printf("LOBBY|colour|%d swaps driven, 0 duplicates, the human never moved\n",
                   moves);
        }
    }

    bad += sk_fits(f, "preview.empty.1", "no map", SK_PREV_W, &checked);
    bad += sk_fits(f, "preview.empty.2", "preview", SK_PREV_W, &checked);

    printf("LOBBY|fit|%d strings measured|%d overflow\n", checked, bad);
    /* ANTI-VACUITY. If the map list were empty and the labels vanished, the loop above
     * would measure almost nothing and report a clean bill of health. */
    if (checked < 30) {
        printf("LOBBY|OVERFLOW|coverage|only %d strings measured\n", checked);
        bad++;
    }
    fflush(stdout);
    return bad;
}
