/*
 * dosmenu.h -- the 1995 MS-DOS Command & Conquer MAIN MENU, as a lift-and-drop C module.
 *
 * Same contract as sidebar/dosbar.h and it deliberately builds ON that module rather
 * than beside it: the menu is drawn with the identical DOS primitives (the 8-bit
 * surface, Draw_Box, the shape blitter, the 4bpp font printer), so there is exactly
 * one implementation of each and they cannot drift apart. dosmenu.c calls db_* for
 * everything except the two box styles the sidebar never needed (the green ones) and
 * the gradient font palette the menu prints with.
 *
 * Draws into an 8-bit palettised 320x200 surface. Nothing here calls OpenGL, Glide or
 * malloc during drawing; the finished surface becomes one texture upload, which is
 * what the Voodoo 2 target needs.
 *
 * This module renders and hit-tests. It does not own input, a loop, or a clock: the
 * caller owns those and hands back a DM_State. Layout is fixed at 320x200 because
 * Get_Resolution_Factor() == 0 in DOS, which is the mode we are reproducing.
 *
 * Every number below is quoted from the engine with its source. Sources are the GPL
 * Tiberian Dawn tree (menus.cpp, dialog.cpp, textbtn.cpp, goptions.cpp).
 */

#ifndef DOSMENU_H
#define DOSMENU_H

#include "dosbar.h" /* DB_Surface, DB_Pack, DB_Shape, DB_Font and the primitives */

#define DM_SCREEN_W 320
#define DM_SCREEN_H 200

/* menus.cpp:459-491, Main_Menu, with scale_factor == Get_Resolution_Factor()+1 == 1 */
#define DM_DIALOG_X 85
#define DM_DIALOG_Y 0
#define DM_DIALOG_W 152
#define DM_DIALOG_H 136

#define DM_BTN_X 98  /* D_START_X  */
#define DM_BTN_W 125 /* D_START_W  */
#define DM_BTN_H 9   /* D_START_H  */

/* menus.cpp:494 starty. Both the five and the six button layouts start on row 25. */
#define DM_BTN_TOP 25

/* menus.cpp:483/489. Exit is the one button that is neither full width nor left aligned
 * with the others (the GERMAN|FRENCH build widens it; this is the English build). */
#define DM_EXIT_X 128
#define DM_EXIT_W 63

/* defines.h:2882-2885. The green dialog ramp lives high in the palette, well outside
 * the 16 colour GUI ramp in dosbar.h, and is present in TITLE.CPS's own palette. */
#define DM_GREEN_SHADOW 140
#define DM_GREEN_BKGD 141
#define DM_GREEN_CORNERS 141
#define DM_LIGHT_GREEN 159
#define DM_GREEN_BOX 159

/* dialog.cpp:366-368 _textpalmedium[CC_GREEN] and _textpalbright[CC_GREEN].
 * CC_GREEN == GREEN == 3, so the medium/bright button text colours are these. */
#define DM_TEXT_MEDIUM 41
#define DM_TEXT_BRIGHT 4

/* THE DISABLED LOOK, and it is not a dimmed green button. Two independent sources:
 *
 * 1. THE BOX. textbtn.cpp:296-305 Draw_Background: a TPF_6PT_GRAD button that
 *    IsDisabled takes BOXSTYLE_GREEN_DIS_RAISED, not BOXSTYLE_GREEN_RAISED.
 *    That is entry 9 of defines.h:1830-1843's BoxStyleEnum, and row 9 of
 *    dialog.cpp:97-111 ButtonColorsClassic is
 *        {DKGREY, BLACK, LTGREY, DKGREY}   // 9 Button is disabled up.
 *    against the enabled row 7
 *        {CC_GREEN_BKGD, CC_GREEN_SHADOW, CC_LIGHT_GREEN, CC_GREEN_CORNERS}
 *    so all four colours change and the button leaves the green ramp entirely.
 *    The BORDER GEOMETRY does not change: 9 is not GREEN_BOX or GREEN_BORDER, so
 *    dialog.cpp:163-172 still takes the `default:` arm and still draws the same
 *    raised bevel (shadow bottom+right, highlight top+left, two corner pixels).
 *    Only the ink changes. Filler 141 -> 13, shadow 140 -> 12, highlight 159 -> 14,
 *    corners 141 -> 13.
 *
 * 2. THE TEXT. textbtn.cpp:349-350: when IsDisabled, flags = (TextPrintType)0,
 *    so neither TPF_USE_GRAD_PAL nor TPF_MEDIUM_COLOR/TPF_BRIGHT_COLOR is passed,
 *    while colour stays CC_GREEN (textbtn.cpp:347). Walk that through
 *    Simple_Text_Print with back == TBLACK:
 *      dialog.cpp:374  memset(fontpalette, back, 16)        -> all 16 entries 0
 *      dialog.cpp:414-422  no TPF_USE_GRAD_PAL, so the _textfontpal memcpy is
 *                          skipped and memset(&fontpalette[4], fore, 12) writes
 *                          CC_GREEN == 3 into entries 4..15
 *      dialog.cpp:431-433  no MEDIUM/BRIGHT, so fore = fontpalette[1], which is
 *                          still 0 from the memset
 *      dialog.cpp:505-509  TPF_NOSHADOW (menus.cpp:556 keeps it) -> entries 2,3 = back
 *      dialog.cpp:545-546  entry 0 = back = 0, entry 1 = fore = 0
 *    Final palette: entries 0..3 = 0, entries 4..15 = 3. GRAD6FNT's glyphs use
 *    nibble classes 2,3 (the outline) and 11..15 (the stroke core) and never
 *    class 1, so the stroke prints in flat CC_GREEN 3 and the outline stays
 *    transparent, exactly as the enabled button's outline does.
 *
 * The net effect on screen: the label goes from medium green 41 on green 141 to
 * flat green 3 on grey 13. It is not a dim version of the live button, it is a
 * grey slab, which is what a 1995 DOS dialog does. */
#define DM_DIS_FILL 13    /* DKGREY == GREY, dosbar.h DB_DKGREY  */
#define DM_DIS_SHADOW 12  /* BLACK,          dosbar.h DB_BLACK   */
#define DM_DIS_HILITE 14  /* LTGREY,         dosbar.h DB_LTGREY  */
#define DM_DIS_CORNERS 13 /* DKGREY                              */
#define DM_TEXT_DISABLED 3 /* CC_GREEN itself, ungraded          */

/* goptions.cpp:570. Draw_Caption(TXT_NONE, ...) still draws the filigree pair, because
 * TXT_NONE falls into the OPTION_DIALOG case (defines.h:2900, OPTION_DIALOG == 0). */
#define DM_FILIGREE_LEFT 0
#define DM_FILIGREE_RIGHT 1

/* The buttons, in the order menus.cpp:738-746 fills buttons[].
 *
 * DM_TESTMAP is ours and is deliberately FIRST, in the slot the engine itself
 * reserves for a sixth item. menus.cpp:544-552 builds `expandbtn` (TXT_NEW_MISSIONS,
 * Covert Operations) at `starty` BEFORE startbtn, menus.cpp:738 makes it buttons[0],
 * and menus.cpp:731-735 sets curbutton = 0 when it is present, so the extra item is
 * both above Start New Game and the default selection. That is the only six button
 * main menu C&C ever shipped, and Test Map is playing exactly New Missions' part:
 * it is not a footnote under the normal modes, it is the way into the game. With
 * Start New Game dead it also has to be where the cursor lands at boot, which the
 * engine's own curbutton = 0 gives us for free. */
typedef enum
{
    DM_TESTMAP = 0, /* ours; the engine's sixth-item slot (expandbtn)            */
    /* OURS, and it is playing the part the engine's expandbtn was built for: the 1995
       menu grew a New Missions button when the Covert Operations disc was installed
       (menus.cpp:544-552, TXT_NEW_MISSIONS). This is that button. "Special Ops" is the
       cartridge's own word for these -- the string is in the ROM at 0x22384C, beside
       SPECIAL OPS 2 -- and the list behind it holds both the cartridge's own
       non-campaign scenarios and the fifteen from the Covert Operations disc. */
    DM_SPECIAL,     /* ours: the SPECIAL OPS mission list                        */
    /* OURS. The maps made in the editor, singleplayer ones only -- a multiplayer map has
       no briefing and no objective, so offering it here would start a game nobody can
       win. Those appear in the Skirmish map list instead, on its own tab. */
    DM_USERMAPS,    /* ours: the USER MAPS list                                  */
    DM_START,       /* TXT_START_NEW_GAME                                        */
    DM_LOAD,        /* TXT_LOAD_MISSION                                          */
    /* SKIRMISH is the 1995 button's slot and very nearly its label: the engine's own
       TXT_MULTIPLAYER_GAME covered both playing a person and playing the computer,
       because in 1995 one dialog did both. Here they are two different things, only one
       of them exists, and calling the one that exists "Multiplayer" would promise the
       other. */
    DM_SKIRMISH,    /* ours, in TXT_MULTIPLAYER_GAME's slot                      */
    /* Drawn, disabled, and honest: there is no networking. It sits below Skirmish rather
       than above it so the live entry is the one the eye reaches first. */
    DM_MULTIPLAYER, /* TXT_MULTIPLAYER_GAME                                      */
    DM_INTRO,       /* TXT_INTRO, "Intro & Sneak Peek"                           */
    /* OURS, like Test Map. The 1995 menu had no picture settings because there was one
       picture. Eight items still fit the engine's own box: menus.cpp keeps D_DIALOG_H at
       136 on both branches and tightens the step instead, so eight at step 11 from row
       25 occupy rows 25..111, clear of the version line at 116 and inside a box that
       ends at 136. Nothing is grown. */
    DM_VISUALS,     /* ours: CLASSIC / ENHANCED and the element checkboxes        */
    DM_EXIT,        /* TXT_EXIT_GAME                                             */
    DM_ITEM_COUNT
} DM_Item;

/* menus.cpp:539 `int ystep = 15 * scale_factor;` then :542-543
 *      if (expansions) ystep -= 2 * 2;
 * That is the whole of the engine's six item geometry. Note what it does NOT do:
 * D_DIALOG_H stays 136 * scale_factor at menus.cpp:459, a literal, on both branches.
 * The engine never grows the dialog to fit the extra button, it tightens the step
 * from 15 to 11 and drops the block into the same box, still starting at row 25 and
 * still inside the same 85/152 centring. So the step is what is derived from the item
 * count here, and the box is left alone.
 *
 * Six items at step 11 from row 25 occupy rows 25..88; five at step 15 occupy 25..93.
 * The taller list is the shorter block, which is the engine's arithmetic, not a slip. */
#define DM_BTN_STEP_BASE 15
#define DM_BTN_STEP_EXPANDED (DM_BTN_STEP_BASE - 2 * 2)

/* THE STEP IS DERIVED FROM THE COUNT, not chosen per list.
 *
 * The engine had two answers because it had two lists: 15 for five items, 11 for six
 * (menus.cpp:539-543), and the box never grew on either branch. Our list has kept
 * growing past both, and a tenth item at step 11 puts the last button's last row at
 * 25 + 9*11 + 9 = 133 -- straight through the version line at 125 and into the plate's
 * inner edge at 133. The engine's own answer to "the list got longer" was to tighten
 * the step and leave the box alone, so that is what this does, with the arithmetic
 * written out rather than another magic number appended to a ladder.
 *
 * The stack has to fit between DM_BTN_TOP and the version line, leaving a clear row:
 *
 *      DM_BTN_TOP + (n-1)*step + DM_BTN_H  <  DM_VERSION_Y
 *
 * which gives step < (DM_VERSION_Y - DM_BTN_TOP - DM_BTN_H) / (n-1) = 91/(n-1).
 * Never wider than the engine's own 15, and never tighter than 9, at which point the
 * buttons touch and the answer would be a taller box rather than a smaller gap. */
#define DM_BTN_FIT ((int)DM_ITEM_COUNT > 1 ? (91 / ((int)DM_ITEM_COUNT - 1)) : 15)
#define DM_BTN_STEP (DM_BTN_FIT > DM_BTN_STEP_BASE ? DM_BTN_STEP_BASE \
                     : (DM_BTN_FIT < 9 ? 9 : DM_BTN_FIT))

/* WHERE THE VERSION LINE SITS, and it is the one number on this plate that is no longer
 * the engine's.
 *
 * menus.cpp:814-820 prints it at OptionY + OptionHeight - (10 * factor), which at factor
 * 1 is row 116, and that was right for every list this menu has had until now. The ninth
 * button is what changed it: the stack starts at row 25 and walks in steps of 11, so item
 * 8 sits at 25 + 8*11 = 113 and ends after row 122, six rows THROUGH a version line at
 * 116. That is the same collision the pause dialog hit when its own list grew, and the
 * answer there was to solve for the number rather than nudge it.
 *
 * Solve it here too. The last button ends at row DM_BTN_TOP + 8*DM_BTN_STEP + DM_BTN_H,
 * which is 122. The 6 point line is 7 rows tall and the plate's inner edge is at
 * DM_DIALOG_H - 3. Ask for a clear gap under the buttons and a clear one above the
 * border:
 *
 *      122 < Y   and   Y + 7 <= 136 - 3    ->   122 < Y <= 126
 *
 * 125 takes the middle of that window: three rows under the stack, two above the border.
 * Written as an offset from the box so it still moves with the box.
 *
 * The alternative, tightening the step to 10 and leaving the line at 116, was rejected:
 * it costs every button its 2 row separation and leaves the last one 2 rows off the
 * version line, which is how a stack starts reading as one grey mass. */
#define DM_VERSION_Y (DM_DIALOG_Y + DM_DIALOG_H - 11)

typedef struct
{
    int selected;   /* curbutton: the one button that is Turn_On()            */
    int pressed;    /* the item held under the mouse, or -1                   */
    const char *version;   /* menus.cpp:814, printed bottom right of the dialog   */
    const char *copyright; /* the bottom line; NULL for none. See below.          */
} DM_State;

/* The 1995 plate had its copyright line painted into the art. The replacement plate
 * has that strip blanked at bake time and the line is printed here instead, so it can
 * be changed without touching the picture.
 *
 * The font is SCOREFNT, which is the font the 1995 plate's own copyright line was set
 * in: correlated against the art's own pixels it beats every other font on the discs.
 * 6 point and 3 point were both wrong, which is what was seen.
 *
 * Letter spacing is +1 rather than the -1 that matched the art's ink density. The art
 * is a rendered image and its glyphs are a shade narrower than the font's, so at the
 * density that matched, real SCOREFNT glyphs touch and the line turns to mush. +1 is
 * the spacing at which it reads, and it is closer to how the 1995 line looks.
 *
 * SCOREFNT is a wide face, so a long credit is word wrapped onto a second row rather
 * than clipped. Each row is 6 pixels; two of them occupy 186..198. */
/* Index 246, (97,97,97): the exact flat grey the 1995 plate's copyright line is
 * painted in. That line is one colour, not a shaded one; 355 of its 371 ink pixels
 * are this index and the rest are stray antialiasing. */
#define DM_COPYRIGHT_COLOUR 246
#define DM_COPYRIGHT_Y 186
#define DM_COPYRIGHT_LINE_STEP 7
#define DM_COPYRIGHT_XSPACING 1
#define DM_COPYRIGHT_MARGIN 4
#define DM_COPYRIGHT_DEFAULT                                                             \
    "Copyright 1995, 2026 Westwood Studios Inc. Created by freschism"

void dm_state_init(DM_State *st);

/* The button's rectangle in screen pixels. Returns 0 for an item that is not drawn. */
int dm_item_rect(const DM_State *st, int item, int *x, int *y, int *w, int *h);

/* The item under a screen pixel, or -1. Matches the engine's inclusive edges, and
 * refuses a disabled item: gadget.cpp:632 guards the whole Clicked_On dispatch with
 * `if (!next_button->IsDisabled)`, so a disabled gadget is drawn and never clicked. */
int dm_hit_test(const DM_State *st, int mx, int my);

/* Keyboard walk, menus.cpp:902-944. Wraps, and steps over disabled items: the engine
 * does the same thing at menus.cpp:912-914 and :930-935, where the walk is clamped to
 * 1..5 rather than 0..5 whenever buttons[0] is not available. Returns `item` unchanged
 * if nothing else is reachable. */
int dm_next_item(const DM_State *st, int item, int delta);

/* The verbatim DOS label. The five stock ones are resolved from CONQUER.ENG at bake
 * time; DM_TESTMAP is ours and has no CONQUER.ENG number. */
const char *dm_item_label(int item);

/* GadgetClass::IsDisabled for this item: drawn, never selectable. Non-zero if the
 * item cannot be reached by mouse or keyboard. */
int dm_item_disabled(int item);

/* The first item that is not disabled, or -1 if the whole menu is dead. This is the
 * boot selection, the engine's curbutton at menus.cpp:731-735. */
int dm_first_enabled(void);

/* The two box styles, shared so the Special Ops screen cannot drift from the menu:
 * dm_green_button is textbtn.cpp's TPF_6PT_GRAD background (raised bevel, or the
 * disabled colourway), dm_green_dialog is dialog.cpp's BOXSTYLE_GREEN_BORDER. */
void dm_green_button(DB_Surface *s, int x, int y, int w, int h, int pressed, int disabled);
void dm_green_dialog(DB_Surface *s, int x, int y, int w, int h);

/* The title plate alone (TITLE.CPS, the whole 320x200), so a second screen can stand
 * on the same backdrop. dm_draw_menu draws it first and then the dialog. */
void dm_draw_plate(DB_Surface *s, const DB_Pack *p);

/* Draws the whole 320x200 menu: title screen, dialog, filigree, version, buttons. */
void dm_draw_menu(DB_Surface *s, const DB_Pack *p, const DM_State *st);

/* The DOS mouse pointer (MOUSE.SHP frame 0), drawn at its hotspot. Optional: the
 * renderer may prefer the host cursor. */
void dm_draw_cursor(DB_Surface *s, const DB_Pack *p, int mx, int my);

#endif /* DOSMENU_H */
