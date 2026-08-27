/*
 * doslobby.h -- the SKIRMISH LOBBY, drawn in the 1995 MS-DOS idiom.
 *
 * Same contract as dosops.h and dosmenu.h, and for the same reason: this module
 * RENDERS and HIT-TESTS a 320x200 8-bit surface with the db_* primitives and nothing
 * else. It owns no input loop, no window, no clock and no GL. The caller builds the
 * map list (it is the one that may read a directory), the shell owns the loop, and
 * this answers one question: what match is being started?
 *
 * The screen replaces the two it used to take -- the campaign's side plate followed by
 * the plain mission list -- so choosing a side, an opponent count and a map is one act.
 *
 * WHAT IS DRAWN IS WHAT THE ENGINE READS. The only path a client has into a match is
 * CNCMultiplayerOptionsStruct plus six CNCPlayerInfoStruct rows, and about half of the
 * 1995 dialog's controls have no field there, no effect in Tiberian Dawn, or no way to
 * reach a CNC3D pixel. Those are not drawn at all rather than drawn dead:
 *
 *   difficulty slider  CNC_Set_Difficulty returns immediately unless the game is
 *                      GAME_NORMAL, so every multiplayer house takes DIFF_NORMAL.
 *   player name box    the name never reaches a CNC3D pixel, and there is no edit widget.
 *   Shadow Regrows     Tiberian Dawn has no such field; the struct member is read by
 *                      nothing in the whole engine.
 *   Capture the Flag   the engine would honour it; there is no flag art and no flag
 *                      handling in the renderer.
 *   start position     the legal index range differs per map because the engine compacts
 *                      waypoints 0..25 and then indexes the compacted list unchecked.
 *   chat               there is nobody to talk to.
 *
 * TEAMS USED TO BE ON THAT LIST AND ARE NOT ANY MORE (by request). The
 * note said "the field works; the renderer has no ally concept, so a 2v2 would be drawn
 * as a 1v3", and the first half was right: CNCPlayerInfoStruct::Team reaches
 * MPlayerTeamIDs (dllinterface.cpp:759) and GlyphX_Assign_Houses calls Make_Ally for
 * every pair that shares one (:1044-1058), so the alliance is real in the simulation.
 * The second half is the honest cost and it is unchanged: the cartridge carries two
 * house texture sets, both chosen by SIDE, so allies on opposite sides look like
 * enemies and two players on one side look like one army. The roster's colours name
 * players in the LOBBY and nowhere else, which is what the footer row says.
 *
 * COLOUR WAS ON THAT LIST TOO AND IS NOT ANY MORE (by request: "make
 * sure theres a colorpicker for each player as well"). The old note said a colour index
 * "changes a number nothing draws", and the second half of that is still true of the
 * TACTICAL VIEW -- CNC3D draws 3D models whose texture set is chosen by SIDE, and the
 * house's remap table never reaches a pixel here. The first half was wrong: ColorIndex
 * is not inert inside the engine. CNC_Set_Multiplayer_Data folds it into MPlayerID
 * (dllinterface.cpp:750), GlyphX_Assign_Houses unpacks it again and passes it to
 * HouseClass::Init_Data (:982, house.cpp:4721), and that is what sets the house's
 * RemapColor, RemapTable, Color and BrightColor. So the field is carried, it is honest,
 * and the day the renderer learns per-house colour it will already be right.
 *
 * THE SQUARES HAVE NO CAPTIONS, and that is a ruling rather than a shortcut. The
 * engine's own enum annotates itself with the colours it does NOT produce
 * (defines.h:707-718: "REMAP_LTBLUE, // Ingame grey color", "REMAP_BLUE, // Ingame dark
 * green color"), so neither the enum name nor MPlayerGColors is a reliable description
 * of what a player will SEE. Looking at the running game, a player reads the theme that
 * paints blue-grey units with red parts on Nod buildings as the one called "Red" -- and
 * he was reading the engine correctly, because in a single player game Nod BUILDINGS
 * take RemapRed while Nod UNITS keep the default bluegrey (house.cpp:2257-2262). His
 * ruling: "Dont call them names. Just show squares with colors." So the picker is eight
 * coloured squares and no text: nothing to be wrong about, and nothing to measure.
 *
 * Two controls the engine DOES read are drawn and locked instead of hidden, because the
 * state they show is true and the reason they will not move is worth saying:
 *
 *   Bases          with bases off no house has a building or an MCV, and the early-win
 *                  rule then declares every house dead about a second into the match.
 *                  MPlayerBases and DestroyStructures have to be interlocked first.
 *   Tiberium       CNC_Set_Multiplayer_Data writes Special.IsTGrowth and
 *   Regrows        Special.IsTSpread, MapClass::Logic reads them with no mode guard and
 *                  nothing restores them, so a skirmish with tiberium off would disable
 *                  tiberium growth for every campaign mission afterwards in the same
 *                  process. Measured, not suspected.
 *
 * The 1995 sources behind the layout: redalert/nulldlg.cpp Com_Scenario_Dialog is the
 * skirmish dialog, tiberiandawn/netdlg.cpp Net_New_Dialog is where the player roster
 * comes from (a ColorListClass, one row per player, "name TAB side"), and
 * tiberiandawn/nulldlg.cpp is the wording for the tiberium option and the build level
 * ceiling of 7.
 */

#ifndef DOSLOBBY_H
#define DOSLOBBY_H

#include "dosmenu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ *
 * The map list. Strings are the caller's and must outlive the screen.
 * ------------------------------------------------------------------------ */

typedef struct SK_Map
{
    const char *scen;    /* SCM01EA: what the engine is started on            */
    const char *name;    /* the INI's [Basic] Name=, or NULL                  */
    const char *theater; /* the INI's [MAP] Theater=, or NULL                 */
    short w, h;          /* the INI's [MAP] Width= and Height=, in cells      */
    /* START POSITIONS, and the number is a property of the map rather than a
     * constant. Create_Units reads the [Waypoints] section and COMPACTS it, so a gap
     * makes index n mean waypoint n+1; the count is therefore the length of the
     * CONTIGUOUS run of valid entries from 0, capped at MAX_PLAYERS. On the retail
     * maps that is 6, and on at least one of them a naive count says 7 and the
     * seventh lands on an interior marker that is not a start at all. */
    short starts;
    /* 1 when this map came out of the editor rather than off the cartridge. It decides
       which tab the map appears under, and nothing else. */
    unsigned char user;
} SK_Map;

/* ------------------------------------------------------------------------ *
 * What the screen answers: exactly the skirmish block of GameOpts, by value.
 * NO POINTERS INTO THE MAP LIST. The caller's opt.scen outlives this screen (a match
 * can be restarted from the pause dialog) and the next visit to the lobby frees what a
 * pointer would point into, so the caller copies the strings out itself.
 * ------------------------------------------------------------------------ */

typedef struct SK_Lobby
{
    int map;          /* index into the caller's map array                   */
    int side;         /* the side the HUMAN plays: 0 GDI, 1 Nod              */
    int ai_count;     /* computer opponents, 1..SK_AI_MAX                    */
    int build;        /* tech level, 1..7                                    */
    int credits;      /* starting credits, every house alike                 */
    int tiberium;     /* 1 = tiberium grows and spreads                      */
    int crates;       /* 1 = bonus crates are scattered                      */
    int superweapons; /* 1 = superweapons may be built                       */
    int bases;        /* 1 = everyone starts with an MCV and builds          */
    int unit_count;   /* escort units per house beside the MCV               */
    int start_wp[8];  /* one compacted waypoint index per player, human first */
    /* PER PLAYER, human first, one entry per seat up to ai_count + 1. Entries past that
     * are still written (GDI, own team, own colour) and describe nobody.
     *   house  0 GDI, 1 Nod: the side that player's army wears and builds from.
     *   team   ZERO BASED, so the screen prints team + 1. Two players carrying the same
     *          number are allies. The default is team[i] = i -- the original rule, eight
     *          players and eight teams, everybody on their own.
     *   colour PlayerColorType, 0..7, in the ENGINE's own order (defines.h:705-720) so
     *          the number means the same thing on both sides of the handoff. It is
     *          always a PERMUTATION of 0..7 across the eight seats, so no two players
     *          can ever share one however the picker is worked. */
    int house[8];
    int team[8];
    int colour[8];
} SK_Lobby;

/* ------------------------------------------------------------------------ *
 * THE MAP PREVIEW PACK, mappreview.pack, magic "MAPPREV1".
 *
 * Neither 1995 game drew a map preview -- the string does not occur once in the whole
 * GPL tree outside a filename table -- so this is ours, and it is honest about being
 * ours: it is the same picture of the same map the in-game radar paints, baked to a
 * fixed 120x120 frame of palette indices in the MENU's own palette, so it blits into a
 * menu plate with no remap and no conversion. The margin around a non-square map is
 * index 0, which the shape blitter does not paint.
 *
 * The pack is optional. Without it the panel says so and every other control still
 * works, which is how every other pack in this program degrades.
 * ------------------------------------------------------------------------ */

typedef struct SK_Prev SK_Prev; /* opaque; one loaded mappreview.pack */

/* Reads the whole file into one allocation and keeps pointers into it, the same shape
 * as db_pack_load. NULL when the file is missing or malformed; `err` (may be NULL) then
 * carries the reason, which is what the caller prints. */
SK_Prev *sk_prev_load(const char *path, char *err, int errlen);
void sk_prev_free(SK_Prev *p);
/* Record index for a scenario code, or -1. Records are fixed size on purpose, so this
 * is arithmetic and a compare, not a walk. */
int sk_prev_find(const SK_Prev *p, const char *scen);

/* ------------------------------------------------------------------------ *
 * Geometry. All of it in 320x200 menu pixels, which is the only resolution the
 * DOS dialogs have (Get_Resolution_Factor() == 0).
 *
 * The plate is the mission list's, unchanged, so the two screens are siblings.
 * ------------------------------------------------------------------------ */

#define SK_DLG_X 16
#define SK_DLG_Y 6
#define SK_DLG_W 288
#define SK_DLG_H 188

/* goptions.cpp:58 CaptionYPos = 5 * factor, and the filigree pair centred on the box's
 * top corners at (x + 12, y + 11) / (x + w - 14, y + 11). */
#define SK_CAPTION_Y (SK_DLG_Y + 5)
#define SK_LABEL_Y 26

#define SK_ROW_H 8

/* The roster: eight rows, which is MAX_PLAYERS with the 8-player patch, so it can
   never need a scrollbar (the plate has the height: 8 rows of SK_ROW_H is 64px).
 * The width is arithmetic and every term is MEASURED in GRAD6FNT, where every glyph is
 * six pixels wide: the longest name this list can hold is "COMPUTER 7" (58), the side is
 * three letters (18), the team is one digit (6), and the row needs a three pixel inset
 * at each end and two pixels between columns. 3 + 58 + 3 + 18 + 2 + 6 + 3 is 93, so 94.
 * The box drawn around it runs SK_ROSTER_X - 2 .. + SK_ROSTER_W + 4, which is 22..119,
 * and the map column's own box starts at 122. Two pixels of daylight, not none.
 *
 * A FOURTH COLUMN went in on 26 Aug 2026 and the 94 did NOT move, because it cannot:
 * 119 and 122 are two pixels apart and the GDI/Nod pair, the status rows and the drop
 * down all start at SK_ROSTER_X. The five pixels the colour swatch needs came out of the
 * insets and the gaps instead, which were three and three and two and three. The row is
 * now, in order and to the pixel:
 *     1 gap | 5 swatch | 1 gap | 58 name | 2 gap | 18 side | 2 gap | 6 team | 1 gap
 * which is 94 exactly. sk_check_layout asserts that sum rather than trusting it. */
#define SK_ROSTER_X 24
#define SK_ROSTER_Y 34
#define SK_ROSTER_W 94
#define SK_ROSTER_ROWS 8
/* The roster gets its own PITCH. Eight seats at the list pitch of 8 would run the
   box from y=34 to y=98 and straight through the GDI/NOD buttons at y=92; at 7 it
   ends at 90 and every other coordinate on this 320x200 plate stays exactly where
   1995 put it. The font is six pixels tall, so seven still leaves a separating row. */
#define SK_ROSTER_ROW_H 7
#define SK_ROSTER_H (SK_ROSTER_ROWS * SK_ROSTER_ROW_H)
/* THE COLOUR SWATCH, at the head of the row, because that is where a 1995 roster put
 * the thing that identifies a player: netdlg.cpp:3063-3070 prints the whole row IN that
 * player's colour through a ColorListClass. Six pixel text can only carry so much of a
 * colour, and it cannot carry it at all for a row nobody is looking at, so the colour
 * gets a solid five pixel square of its own AND still tints the row's ink.
 * Five square is what the 7 pixel row pitch has room for with a pixel above and below. */
#define SK_ROSTER_SW_X 1
#define SK_ROSTER_SW 5
/* netdlg.cpp:3063-3070 prints a roster row as "%s\t%s". Its own tab stop is 78 in a
 * 118 wide list, which is the same proportion of a narrower one. Shifted right of the
 * swatch by exactly the swatch's width plus its two gaps, so the name column keeps the
 * 58 pixels "COMPUTER 7" measures and loses nothing. */
#define SK_ROSTER_NAME 7
#define SK_ROSTER_TAB 67
/* A THIRD COLUMN, which 1995 did not have because 1995 put teams in a separate drop
 * list: the team number, one digit, right of the side. It is only worth six pixels and
 * it is the only place the screen can SHOW a team, so it earns them. */
#define SK_ROSTER_TEAM 87
/* Each column's own room, written once so the draw and the audit measure the same
 * number. The team's is what is left of the row less a one pixel margin. */
#define SK_ROSTER_NAME_W (SK_ROSTER_TAB - SK_ROSTER_NAME - 2)
#define SK_ROSTER_SIDE_W (SK_ROSTER_TEAM - SK_ROSTER_TAB - 2)
#define SK_ROSTER_TEAM_W (SK_ROSTER_W - SK_ROSTER_TEAM - 1)

/* MEASURED, not chosen: the longest map name installed, "MOOSEHEAD BARRENS", prints
 * 100 pixels wide in GRAD6FNT, and db_print does not truncate. 108 of content is that
 * plus the row's own inset on both sides. */
#define SK_MAP_X 124
#define SK_MAP_Y 34
#define SK_MAP_W 108

/* THE TWO TABS, above the list: the maps that shipped, and the ones made in the editor.
 * The split was asked for because a folder of your own maps is worth nothing if it is
 * mixed into a wall of SCM names.
 *
 * The list gives up a row to make space rather than the column growing: the Battlefield
 * column ends where the left column's margin ends (SK_INFO_Y at 94) and that arithmetic
 * is what sizes the preview beside it. Five rows plus the tab row occupy 34..83, which
 * is one row less than the six did and still clear of the info line. */
#define SK_TAB_H 9
/* THE TABS ARE UNEQUAL, and that is a measurement rather than a taste.
 *
 * "OFFICIAL" prints 48 pixels in GRAD6FNT and "USER MAPS" prints 52. The column is 108
 * wide with 2 pixels between the two tabs, so two equal tabs of 53 cannot hold either
 * caption plus the 3 pixel inset the code was using -- and the shipped screen therefore
 * read OFFICIA and USER MAP, silently, because db_print's own truncation is silent and
 * sk_check_layout was never asked to measure these two strings. So each tab is sized to
 * its own caption, 52 + 2 + 54 = 108, at the one pixel inset which is all 108 pixels has
 * left to give, and both captions are now in the audit. */
#define SK_TAB0_W 52
#define SK_TAB1_W 54
#define SK_TAB_INSET 1
#define SK_TAB_W(t) ((t) ? SK_TAB1_W : SK_TAB0_W)
#define SK_TAB_X(t) (SK_MAP_X + ((t) ? (SK_TAB0_W + 2) : 0))
#define SK_TAB_Y SK_MAP_Y
#define SK_MAPLIST_Y (SK_MAP_Y + SK_TAB_H + 2)
#define SK_MAP_ROWS 5
#define SK_MAP_H (SK_MAP_ROWS * SK_ROW_H)

/* The preview panel, and its size is arithmetic rather than taste: the right column is
 * the map list plus this, the longest map name has to stay inside the list, and the
 * whole thing has to end where the left column's margin ends. 56 square is what is
 * left, which is about 0.9 pixels per cell on a 62x62 map -- the resolution the
 * in-game radar draws the same map at. */
#define SK_PREVBOX_X 237
#define SK_PREVBOX_Y 32
#define SK_PREVBOX_W 60
#define SK_PREVBOX_H 60
#define SK_PREV_X (SK_PREVBOX_X + 2)
#define SK_PREV_Y (SK_PREVBOX_Y + 2)
#define SK_PREV_W (SK_PREVBOX_W - 4)
#define SK_PREV_H (SK_PREVBOX_H - 4)

/* Under the whole Battlefield column, list and picture alike. */
#define SK_INFO_X 124
#define SK_INFO_Y 94
#define SK_INFO_W 174

#define SK_SIDE_Y 92
#define SK_SIDE_W 44
#define SK_SIDE_H 9
#define SK_GDI_X 24
#define SK_NOD_X 72

/* Three live rows and a fourth left laid out and empty: it is where Unit Count and
 * Crates go if they ever earn their place, and leaving the slot means adding them
 * later moves nothing. */
/* STARTING UNITS. FR-20260825-77A9E9 asked for "a count up to 10, from an assortment
 * including Mammoth Tank, Stealth Tank, SSM Launcher". The ASSORTMENT is already the Tech
 * Level row's doing: scenarioini.cpp:1416's utable maps build level to unit type, and
 * level 6 is the SSM launcher while level 7 is Mammoth for GDI and Stealth for Nod. So the
 * only control missing was the COUNT, which is this.
 * Zero stays the default and stays the measured-best: the engine scatters the escort within
 * about four cells of the MCV and any unit landing inside the 3x3 Construction Yard pad
 * makes the player's first click do nothing. The player may now choose otherwise. */
#define SK_UNITS_MIN 0
#define SK_UNITS_MAX 10

#define SK_CTRL_Y 110
#define SK_CTRL_STEP 10
#define SK_GAUGE_X 102
#define SK_GAUGE_W 44
#define SK_GAUGE_H 7
#define SK_GLABEL_R 100 /* gauge captions are right aligned to end here */
#define SK_READ_X 150   /* the printed value, sounddlg.cpp's own idea   */
/* Far enough right that the widest printed gauge value, "10000" at 30 pixels, still
 * ends before the box; Red Alert's own check list starts at 171 in the same dialog. */
#define SK_CHK_X 186
#define SK_CHK_BOX 7
#define SK_CHK_LABEL_X (SK_CHK_X + SK_CHK_BOX + 4)
#define SK_CHK_ROW_W 111

/* Two rows, because there are two things to say and neither fits in 272 pixels beside
 * the other: what just happened, and the one fact about this screen the picture cannot
 * show -- that a roster colour is a lobby colour and nothing else. */
#define SK_STATUS_X 24
#define SK_STATUS_Y 150
#define SK_STATUS_Y2 158
#define SK_STATUS_W 272

/* Unchanged from the mission list, so Play and Cancel are in the same place on both. */
#define SK_BTN_Y 168
#define SK_BTN_H 9
#define SK_PLAY_X 84
#define SK_PLAY_W 60
#define SK_CANCEL_X 176
#define SK_CANCEL_W 60

/* ------------------------------------------------------------------------ *
 * THE ROSTER DROP DOWN, opened by clicking a roster row.
 *
 * docs/design-skirmish-lobby-widgets.md 2.3 says not to build a drop list, because the
 * drawing model is one immediate pass into one surface with no z ordering: a popup has
 * to be drawn LAST and hit-tested FIRST. That is the whole cost and it is paid in two
 * places -- sk_draw calls sk_draw_popup after everything else, and sk_hit_test asks the
 * popup before it asks anything -- because a drop down by name was asked for and a
 * per-row faction plus a per-row team is four choices in one place, not one.
 *
 * It is a floating box and the plate is 320x200, so its size is arithmetic, not taste.
 * Every glyph in GRAD6FNT is six pixels wide:
 *
 *   header  "COMPUTER 7" is 58 and sits inside SK_POP_W - 2*PAD = 136.
 *   SIDE    a 24 pixel caption, then two buttons whose label is 18 and whose margin is
 *           textbtn.cpp:81's own 8, so 26 each.
 *   TEAM    the same caption, then eight buttons of 12 for a six pixel digit.
 *   COLOUR  a 36 pixel caption -- which is what widened the caption column from 28 to
 *           40 -- then eight buttons of 12 carrying a filled square and NO TEXT.
 *
 * 4 + 40 + 8*12 + 4 = 144 wide, and 4 + 7 + 3*11 + 4 = 48 tall. Anchored under the row
 * it belongs to and CLAMPED to the dialog, which sk_check_layout asserts for all eight
 * rows rather than trusting the arithmetic above.
 *
 * THE THIRD ROW IS CAPTIONED AND ITS EIGHT BUTTONS ARE NOT. "COLOUR" names the QUESTION,
 * which cannot be the wrong word; naming an ANSWER is the thing ruled out, and no
 * square carries a glyph.
 * ------------------------------------------------------------------------ */
#define SK_POP_PAD 4
#define SK_POP_LAB_W 40   /* the SIDE / TEAM / COLOUR caption column, 36 plus a gap */
#define SK_POP_SIDE_W 26
#define SK_POP_SIDE_GAP 4
#define SK_POP_TEAM_W 12
#define SK_POP_TEAMS 8
/* The colour strip: as many buttons as there are player colours, and the same 12 pixel
 * pitch as the team row so the two strips line up under each other. The swatch inside a
 * button is the button less its bevel on all four sides. */
#define SK_POP_COL_W 12
#define SK_POP_COLOURS 8
#define SK_POP_BTN_H 9
#define SK_POP_ROW_H (SK_POP_BTN_H + 2)
#define SK_POP_HDR_H 7
#define SK_POP_W (SK_POP_PAD * 2 + SK_POP_LAB_W + SK_POP_TEAM_W * SK_POP_TEAMS)
#define SK_POP_H (SK_POP_PAD * 2 + SK_POP_HDR_H + SK_POP_ROW_H * 3)
#define SK_POP_X SK_ROSTER_X
/* The lowest the box may start and still end inside the dialog. */
#define SK_POP_Y_MAX (SK_DLG_Y + SK_DLG_H - 4 - SK_POP_H)

/* ------------------------------------------------------------------------ *
 * The controls. The order is the keyboard walk order.
 * ------------------------------------------------------------------------ */

typedef enum
{
    /* THE ROSTER ROWS, first because they are the top left of the screen and the walk
     * order is the reading order. One per seat; a row past the current opponent count
     * has no rectangle and is skipped by the walk. Clicking one opens the drop down. */
    SK_I_ROW0 = 0,
    SK_I_ROW7 = SK_I_ROW0 + SK_ROSTER_ROWS - 1,
    SK_I_GDI,      /* a latched pair: exactly one of the two is lit */
    SK_I_NOD,
    SK_I_MAPS,     /* the list. Arrow keys walk it while it has focus */
    SK_I_AI,       /* gauge */
    SK_I_BUILD,    /* gauge */
    SK_I_CREDITS,  /* gauge */
    SK_I_UNITS,    /* gauge -- LAST of the gauges, sk_is_gauge is a range */
    SK_I_BASES,    /* check box, drawn disabled */
    SK_I_TIBERIUM, /* check box, drawn disabled */
    SK_I_SUPER,    /* check box */
    SK_I_CRATES,   /* check box -- LAST of the boxes, the rect maths is a range */
    SK_I_PLAY,
    SK_I_CANCEL,
    /* THE DROP DOWN'S OWN CONTROLS, last because they exist only while it is open: with
     * it shut they have no rectangle and report themselves disabled, so neither the
     * mouse nor the keyboard walk can reach them. */
    SK_I_POP_GDI,
    SK_I_POP_NOD,
    SK_I_POP_T1,
    SK_I_POP_T8 = SK_I_POP_T1 + SK_POP_TEAMS - 1,
    /* THE COLOUR STRIP. Eight squares, one per PlayerColorType, in the engine's own
     * order, so SK_I_POP_C1 + n is colour index n on both sides of the handoff. */
    SK_I_POP_C1,
    SK_I_POP_C8 = SK_I_POP_C1 + SK_POP_COLOURS - 1,
    SK_I_COUNT
} SK_Item;

/* The row and popup ids are RANGES, and every test on them is written as one so adding
 * a seat is a change to SK_ROSTER_ROWS and nothing else. */
#define SK_IS_ROW(i) ((i) >= SK_I_ROW0 && (i) <= SK_I_ROW7)
#define SK_IS_POP(i) ((i) >= SK_I_POP_GDI && (i) <= SK_I_POP_C8)
#define SK_IS_POP_COL(i) ((i) >= SK_I_POP_C1 && (i) <= SK_I_POP_C8)
#define SK_ROW_ITEM(n) (SK_I_ROW0 + (n))   /* seat n's row control    */
#define SK_TEAM_ITEM(n) (SK_I_POP_T1 + (n)) /* team n, zero based      */
#define SK_COLOUR_ITEM(n) (SK_I_POP_C1 + (n)) /* colour n, PlayerColorType order */

#define SK_HIT_NONE (-1)

/* Ranges. MPLAYER_BUILD_LEVEL_MAX is 7 in Tiberian Dawn (defines.h:2581), not Red
 * Alert's 10, and the credit ceiling is nulldlg.cpp's own literal 10000. The 500 step
 * is Red Alert's rounding (nulldlg.cpp:2041), which Tiberian Dawn does not do and which
 * is a kindness on a 44 pixel gauge. */
#define SK_AI_MIN 1
#define SK_AI_MAX 7
#define SK_BUILD_MIN 1
#define SK_BUILD_MAX 7
#define SK_CREDITS_MIN 0
#define SK_CREDITS_MAX 10000
#define SK_CREDITS_STEP 500

/* What the caller has to act on. Everything else is handled inside. */
#define SK_ACT_NONE 0
#define SK_ACT_PLAY 1
#define SK_ACT_CANCEL 2

/* Keys, so this module never includes SDL. */
#define SK_KEY_UP 1
#define SK_KEY_DOWN 2
#define SK_KEY_LEFT 3
#define SK_KEY_RIGHT 4
#define SK_KEY_ENTER 5
#define SK_KEY_ESC 6
#define SK_KEY_TAB 7
#define SK_KEY_SPACE 8
#define SK_KEY_PGUP 9
#define SK_KEY_PGDN 10

typedef struct SK_State
{
    const SK_Map *maps;
    int count;
    const SK_Prev *prev; /* may be NULL: the panel then says so */

    int sel; /* selected map, or -1 when the list is empty */
    int top; /* first visible row                          */
    /* WHICH TAB. 0 = the maps that shipped, 1 = the ones made in the editor. The list
       is filtered by it, so `sel` indexes maps[] and the rows index the filtered view --
       sk_map_at() is the one place that converts, and everything else asks it. */
    int tab;

    int side;    /* 0 GDI, 1 Nod -- the HUMAN's, and seat 0 has no other store */
    int ai;      /* opponents, SK_AI_MIN .. the map's cap   */

    /* PER SEAT. Seat 0 is the human and its faction IS `side` above, so this pair only
     * ever describes computers.
     *
     * `house_set` is what keeps the old behaviour intact for anyone who never opens the
     * drop down: a computer the player has not touched takes the OPPOSITE side to the
     * human and keeps following the side buttons, exactly as before. The moment a row is
     * given a faction by hand it stops following and holds what it was told. Read it
     * through sk_row_house() and never straight out of the array. */
    int house[SK_ROSTER_ROWS];
    int house_set[SK_ROSTER_ROWS];
    /* ZERO BASED; the screen prints team + 1. sk_init writes team[i] = i, which is
     * The rule: eight players, eight teams, everyone on their own. */
    int team[SK_ROSTER_ROWS];

    /* THE PLAYER COLOUR PER SEAT, PlayerColorType 0..7 in the engine's own order.
     *
     * THE INVARIANT, and it is the whole of the request: this array is a PERMUTATION
     * of 0..7 at every moment, over all EIGHT seats and not only the seated ones. Held
     * that way, "no two players share a colour" is true for any opponent count, dragging
     * the AI gauge up cannot surface a duplicate, and a change is a plain transposition
     * that can neither loop nor leave a clash. sk_init writes colour[i] = i and
     * sk_set_row_colour is the only thing that ever writes it again. */
    int colour[SK_ROSTER_ROWS];

    /* WHICH ROW'S DROP DOWN IS OPEN, or -1. There is at most one, it is drawn last and
     * hit-tested first, and any press outside it shuts it. */
    int popup;
    int build;   /* tech level                              */
    int credits; /* starting credits, snapped to 500        */
    int super;   /* superweapons                            */
    int crates;  /* bonus crates scattered on the map       */
    int units;   /* escort units per house beside the MCV   */

    int selected; /* the focused control, for the keyboard walk */
    int pressed;  /* held under the mouse, or SK_HIT_NONE       */
    int drag;     /* the gauge being dragged, or -1             */
    int dragdiff; /* gauge.cpp's grab offset                    */
    int lastmx, lastmy;

    /* The bottom line. Never NULL: it either says why a locked control will not move,
     * or it says the one thing about this screen that the picture cannot. */
    const char *status;
    /* A status line that has to name a seat cannot come out of the fixed table, so it is
     * built here and `status` is pointed at it. sk_check_layout measures the WIDEST form
     * this buffer can hold, which is the only way an assembled string can be audited. */
    char statusbuf[64];
} SK_State;

void sk_init(SK_State *st, const SK_Map *maps, int count, const SK_Prev *prev);

/* ------------------------------------------------------------------------ *
 * Per seat readouts. The roster, the drop down, sk_result and the layout audit all
 * ask these two rather than reading the arrays, so there is exactly one place that
 * knows a computer with no hand-picked faction follows the human's side buttons.
 * ------------------------------------------------------------------------ */
/* "PLAYER" or "COMPUTER n", the one writer of a seat's printed name: the roster row,
 * the drop down's header, the control's label and the audit all call it. */
void sk_seat_name(const SK_State *st, int seat, char *out, int outlen);
int sk_row_house(const SK_State *st, int seat); /* 0 GDI, 1 Nod             */
int sk_row_team(const SK_State *st, int seat);  /* ZERO BASED, prints as +1 */
/* PlayerColorType, 0..7. Distinct for every seat, always -- see SK_State::colour. */
int sk_row_colour(const SK_State *st, int seat);
/* The 320x200 MENU palette index a colour paints with, so the caller can draw the same
 * square this screen draws. Out of range answers colour 0 rather than reading off the
 * end. See SK_PLAYER_COLOUR in doslobby.c for where the eight numbers come from. */
unsigned char sk_colour_index(int colour);
/* How many seats are filled: opponents + 1, capped at SK_ROSTER_ROWS. */
int sk_players(const SK_State *st);

/* The printed label of a control, which is also its name for a script that drives this
 * screen by label rather than by pixel. */
const char *sk_item_label(const SK_State *st, int item);
/* Drawn, never clickable: gadget.cpp:632 guards the whole dispatch on !IsDisabled. */
int sk_item_disabled(const SK_State *st, int item);
/* The control's rectangle. Returns 0 for an item that is not drawn. */
int sk_item_rect(const SK_State *st, int item, int *x, int *y, int *w, int *h);
/* What is under (mx,my) in menu pixels: an SK_Item, or SK_HIT_NONE. Honours disabled
 * for the ACTION and not for the hit, so a click on a locked box can still explain
 * itself in the status line. */
int sk_hit_test(const SK_State *st, int mx, int my);
/* Keyboard walk, wrapping, stepping over anything that is not reachable. */
int sk_next_item(const SK_State *st, int item, int delta);

int sk_press(SK_State *st, int mx, int my);
int sk_motion(SK_State *st, int mx, int my);
int sk_release(SK_State *st, int mx, int my);
int sk_key(SK_State *st, int key);
void sk_scroll(SK_State *st, int delta); /* the wheel: scrolls, moves nothing */

/* The map row under a pointer, or -1. Public because a double click plays it and the
 * shell is the thing that knows what a double click is. */
int sk_map_row_at(const SK_State *st, int mx, int my);
/* The two map-source tabs: OFFICIAL and USER MAPS. The caption is a call and not a
 * literal at the draw site so the audit measures the same string the screen prints. */
const char *sk_tab_label(int tab);
int  sk_tab_at(const SK_State *st, int mx, int my);
void sk_set_tab(SK_State *st, int tab);

/* The whole screen, over whatever the caller left in the surface. */
void sk_draw(DB_Surface *s, const DB_Pack *p, const SK_State *st);

/* HARNESS ONLY, and it draws nothing. Measures every string this screen can print --
 * every map name installed, every line either status row can carry, every label on
 * every control -- against the box it has to print it in, names each one that does not
 * fit on stdout, and returns how many did. -1 means it could not measure at all, which
 * is a failure and not a pass. See the block comment over it for why a screen made of
 * labels needs this and cannot be trusted to a glance. */
int sk_check_layout(const DB_Pack *p, const SK_State *st);

/* The answer, by value. */
void sk_result(const SK_State *st, SK_Lobby *out);

#ifdef __cplusplus
}
#endif

#endif /* DOSLOBBY_H */
