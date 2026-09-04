/*
 * dosopt.h -- the 1995 MS-DOS Command & Conquer IN-GAME OPTIONS (pause) dialog, and its
 * Game Controls sub-dialog, as a lift-and-drop C module.
 *
 * Same contract as menu/dosmenu.h, and it builds on the same foundation: everything is
 * drawn with dosbar.h's primitives into one 8-bit palettised 320x200 surface, which
 * becomes one texture upload and one textured quad. No shaders, no render targets, no
 * per-pixel work on the card: OpenGL 1.1 and Glide can both present it, so this screen
 * costs the Win98/Voodoo 2 tier nothing. Tier 1 gap: none.
 *
 * This module renders, hit-tests and holds the settings. It does not own input, a loop,
 * a clock, a window or the mixer. The caller pumps events into dopt_press/dopt_motion/
 * dopt_release/dopt_key and acts on what comes back.
 *
 * Every number below is quoted from the GPL Tiberian Dawn tree with its source line:
 * goptions.cpp (the pause dialog), gamedlg.cpp (Game Controls), sounddlg.cpp (the volume
 * rows), dialog.cpp (boxes, captions, the font palettes), textbtn.cpp (button look),
 * slider.cpp + gauge.cpp (the sliders). Layout is fixed at 320x200 because
 * Get_Resolution_Factor() == 0 in DOS, which is the mode we are reproducing.
 *
 * ---------------------------------------------------------------------------------------
 * WHAT IS THE ENGINE'S AND WHAT IS OURS
 *
 * The 1995 dialog has SEVEN buttons (goptions.cpp:88-96 _constants[]):
 *      Load Mission, Save Mission, Delete Mission, Game Controls, Abort Mission,
 *      Resume Mission, Restate
 * "Abort Mission" is TXT_QUIT_MISSION (conquer.h:80) and it does NOT close the program:
 * goptions.cpp:433 calls Queue_Exit(), which queues EventClass::EXIT and ends the
 * mission, i.e. it returns to the main menu. There is no Exit Game button in the 1995
 * pause dialog; Exit Game lives on the main menu (conquer.h:81 TXT_EXIT_GAME).
 *
 * DOPT_EXIT is therefore OURS, in the same spirit as the main menu's DM_TESTMAP: an
 * eighth item, in the engine's own idiom, doing the thing the engine's main menu button
 * of that name does (close the program).
 *
 * THIS PARAGRAPH USED TO CLAIM IT COST NO GEOMETRY, AND THAT CLAIM WAS THE BUG. reported on
 * v0.5.7: "the EXIT GAME button is almost cutting through the RESUME MISSION and RESTATE
 * buttons". The reasoning was that the engine's five item stack ends at row 123 and its
 * bottom row starts at row 135, so rows 126..134 are a free slot of exactly one button.
 * They are not a slot. goptions.cpp:129 is `if (index < 5)`: five items stack and
 * everything after shares the bottom row, so those eleven rows are the SEPARATION between
 * the stack and the footer, not an unused button. Spending them welded Exit Game to
 * Resume Mission and Restate with a zero pixel gap, where every other pair in the stack
 * has two. What it costs is at DOPT_H below, where it is now paid in full.
 *
 * The engine's Restate button is labelled "Restate", not "Restate Mission Objectives":
 * TXT_RESTATE_MISSION is conquer.h:653 index 636, and CONQUER.ENG entry 636 in the 1995
 * LOCAL.MIX reads exactly "Restate". Every label in this file was read out of that
 * archive rather than typed from memory.
 * ---------------------------------------------------------------------------------------
 */

#ifndef DOSOPT_H
#define DOSOPT_H

#include "dosbar.h" /* DB_Surface, DB_Pack, DB_Shape, DB_Font and the primitives */

#ifdef __cplusplus
extern "C" {
#endif

#define DOPT_SCREEN_W 320
#define DOPT_SCREEN_H 200

/* ------------------------------------------------------------------------ *
 * The pause dialog.
 * goptions.cpp:48-64 GameOptionsClass::Adjust_Variables_For_Resolution, factor == 1.
 * ------------------------------------------------------------------------ */

#define DOPT_W 224 /* (216 + 8) * factor                          goptions.cpp:52 */
/* 100 in 1995 (goptions.cpp:53), and this is THE ONE PLACE THE DIALOG IS GROWN.
 *
 * WHAT THE ENGINE DOES. goptions.cpp:129 reads `if (index < 5)`: exactly FIVE items
 * stack, walking down in steps of OButtonHeight + 2 from row 71, and every item after
 * the fifth shares one bottom row at OptionY + (OptionHeight - 15). At OptionHeight 100
 * the stack ends at row 123 and the bottom row is at 135, so 1995's own dialog carries
 * ELEVEN blank rows between the two. That is not slack and it is not a spare button
 * slot: it is the floor of the stack, five times wider than the 2 row gap the stack
 * keeps between its own buttons, and it is what makes Resume and Restate read as a
 * footer rather than as two more entries in the list.
 *
 * WHAT WE DO. Two of the nine items below are ours (DOPT_VISUALS and DOPT_EXIT) and
 * dosopt.c:394 stacks SEVEN of them, `item <= DOPT_EXIT`. That needs two button pitches
 * of extra height, not one. 111 was one pitch, and it is why v0.5.7 shipped with Exit
 * Game welded to the bottom row at a zero pixel gap: our two extra buttons had eaten the
 * whole eleven row separation and then some.
 *
 * 122 IS SOLVED, NOT CHOSEN. Write Y = (200 - H)/2, which is goptions.cpp:55. The last
 * stacked button is index 6, so goptions.cpp:130 puts it at Y + ButtonY + 6*(9 + 2) =
 * Y + 21 + 66, and it is 9 rows tall, so it ends after row Y + 96. The bottom row is at
 * Y + H - 15 (goptions.cpp:62). Ask for 1995's own eleven rows between them:
 *
 *      (Y + H - 15) - (Y + 21 + 66 + 9) = 11    ->    H - 111 = 11    ->    H = 122
 *
 * Y cancels, so the answer does not depend on where the box lands on the screen. At
 * H = 122: Y = 39, the stack runs rows 60..134, the bottom row is 146..154, the gap is
 * 11, and the dialog occupies rows 39..160 of 200. The scenario and version block keeps
 * its 1995 relationship as well, being pinned at H - 30 against a bottom row at H - 15,
 * which is the same 15 row offset goptions.cpp:62 had.
 *
 * It is declared here rather than absorbed quietly because the rest of this file's
 * claim is that every pixel is 1995's. It no longer is. The alternative considered and
 * rejected: 1995 DID have a Visual Controls button, on the Game Controls page
 * (gamedlg.cpp:82-90), and restoring it there would have cost nothing -- but it is a
 * click further in, and the button belongs on the pause menu itself. Recorded as a known gap,
 * and MEASURED by game/gate_optlayout.c, which reads the separation
 * back out of dopt_item_rect rather than trusting this paragraph. Nothing in the gate
 * suite could see this dialog's geometry until that binary existed. */
#define DOPT_H 133
#define DOPT_X 48  /* (SeenBuff width - OptionWidth) / 2          goptions.cpp:54 */
/* DERIVED, not a literal any more. It used to be 50, which is goptions.cpp:55's formula
   evaluated for a 100-tall box; with the box grown for the Visuals button a literal
   would have left the dialog hanging 6 rows low with its caption clipped. Same for
   DOPT_RESUME_Y below. Both now say what their own comments always said they were. */
#define DOPT_Y ((DOPT_SCREEN_H - DOPT_H) / 2)  /*                   goptions.cpp:55 */

#define DOPT_BTN_H 9      /* OButtonHeight = 9 * factor           goptions.cpp:57 */
#define DOPT_CAPTION_Y 5  /* CaptionYPos   = 5 * factor           goptions.cpp:58 */
#define DOPT_BUTTON_Y 21  /* ButtonY       = 21 * factor          goptions.cpp:59 */
#define DOPT_RESUME_Y (DOPT_H - 15) /* OptionHeight - (15 * factor)  goptions.cpp:62 */
#define DOPT_MIN_BTN_W 90 /* MAX(maxwidth, 90 * resfactor)        goptions.cpp:156 */

/* goptions.cpp:130. The stacked rows walk down in steps of OButtonHeight + 2 from
 * (SeenBuff height - OptionHeight)/2 + ButtonY, which at factor 1 is row 71. */
#define DOPT_STACK_TOP (((DOPT_SCREEN_H - DOPT_H) / 2) + DOPT_BUTTON_Y)
#define DOPT_STACK_STEP (DOPT_BTN_H + 2)

/* goptions.cpp:132. Everything past the stack shares one row at the bottom. */
#define DOPT_BOTTOM_Y (DOPT_Y + DOPT_RESUME_Y)

/* goptions.cpp:163-165 Resume is pinned 5 px in from the left edge and 90 wide.
 * goptions.cpp:168-170 Restate is 90 wide and pinned 5 px in from the right edge. */
#define DOPT_EDGE_MARGIN 5

/* ------------------------------------------------------------------------ *
 * The Game Controls sub-dialog.
 * gamedlg.cpp:56-95 GameControlsClass::Process, factor == 1.
 * ------------------------------------------------------------------------ */

#define DOPT_GC_W 232 /* d_dialog_w = 232 * factor                 gamedlg.cpp:61 */
#define DOPT_GC_H 141 /* d_dialog_h = 141 * factor                 gamedlg.cpp:62 */
#define DOPT_GC_X ((DOPT_SCREEN_W - DOPT_GC_W) / 2)  /*             gamedlg.cpp:63 */
#define DOPT_GC_Y ((DOPT_SCREEN_H - DOPT_GC_H) / 2)  /*             gamedlg.cpp:64 */

#define DOPT_GC_TOP_MARGIN 30 /* d_top_margin                       gamedlg.cpp:66 */
#define DOPT_GC_TXT6_H 7      /* d_txt6_h, height of 6 point text   gamedlg.cpp:68 */
#define DOPT_GC_MARGIN1 5     /* d_margin1                          gamedlg.cpp:69 */

/* gamedlg.cpp:72-75, the game speed slider. */
#define DOPT_GC_SPEED_W (DOPT_GC_W - 20)
#define DOPT_GC_SPEED_H 6
#define DOPT_GC_SPEED_X (DOPT_GC_X + 10)
#define DOPT_GC_SPEED_Y (DOPT_GC_Y + DOPT_GC_TOP_MARGIN + DOPT_GC_MARGIN1 + DOPT_GC_TXT6_H)

/* gamedlg.cpp:77-80, the scroll rate slider. */
#define DOPT_GC_SCROLL_W (DOPT_GC_W - 20)
#define DOPT_GC_SCROLL_H 6
#define DOPT_GC_SCROLL_X (DOPT_GC_X + 10)
#define DOPT_GC_SCROLL_Y                                                                 \
    (DOPT_GC_SPEED_Y + DOPT_GC_SPEED_H + DOPT_GC_TXT6_H + (DOPT_GC_MARGIN1 * 2)          \
     + DOPT_GC_TXT6_H)

/* gamedlg.cpp:82-90, the Visual Controls and Sound Controls buttons.
 *
 * THE ONE PLACEMENT DECISION IN THIS FILE, and it is declared rather than hidden.
 * Those two buttons open dialogs that do not exist here: Visual Controls is
 * brightness/contrast/tint, which are palette operations this renderer has no pipeline
 * for (visudlg.cpp -> OptionsClass::Set_Brightness and friends, options.cpp:299-470),
 * and Sound Controls is the score playlist (sounddlg.cpp) which needs ThemeClass. Rather
 * than draw two dead buttons that lead nowhere, the band they occupied carries the three
 * volume rows, which is what a player opening Game Controls actually wants today.
 *
 * The rows themselves are not invented. They are sounddlg.cpp's own volume row, verbatim:
 * a 108 x 5 slider (MSlider_W / MSlider_Height, sounddlg.cpp:99-102) with its caption
 * printed RIGHT ALIGNED at (slider_x - 5, slider_y - 2) (sounddlg.cpp:332-343), stepping
 * 12 rows at a time (MSlider_Y 28 -> FXSlider_Y 40, sounddlg.cpp:100/105). The only thing
 * chosen here is where the column sits: the sliders are right-aligned to the same edge
 * the two 1995 buttons ended at, d_visual_x + d_visual_w.
 *
 * It fits inside the 1995 box with room to spare: rows at 124, 136 and 148, the last
 * ending at 153, and the Options Menu button starts at 156. The dialog is not grown. */
#define DOPT_GC_VISUAL_W (DOPT_GC_W - 40)
#define DOPT_GC_VISUAL_H 9
#define DOPT_GC_VISUAL_X (DOPT_GC_X + 20)
#define DOPT_GC_VISUAL_Y                                                                 \
    (DOPT_GC_SCROLL_Y + DOPT_GC_SCROLL_H + DOPT_GC_TXT6_H + (DOPT_GC_MARGIN1 * 2))

#define DOPT_GC_VOL_W 108 /* MSlider_W                              sounddlg.cpp:101 */
#define DOPT_GC_VOL_H 5   /* MSlider_Height                         sounddlg.cpp:102 */
#define DOPT_GC_VOL_STEP 12 /* FXSlider_Y - MSlider_Y               sounddlg.cpp:100,105 */
#define DOPT_GC_VOL_X (DOPT_GC_VISUAL_X + DOPT_GC_VISUAL_W - DOPT_GC_VOL_W)
#define DOPT_GC_VOL_Y DOPT_GC_VISUAL_Y
#define DOPT_GC_VOL_LABEL_GAP 5 /* Fancy_Text_Print at X - 5        sounddlg.cpp:333 */
#define DOPT_GC_VOL_LABEL_RISE 2 /* ... and at Y - 2                sounddlg.cpp:334 */

/* gamedlg.cpp:92-95 + :151. The OK button auto-sizes to its label and is centred on the
 * SCREEN, not on the dialog: okbtn.X = (SeenBuff.Get_Width() - okbtn.Width) / 2. That is
 * still what DOPT_V_OK and DOPT_A_OK do on the Visuals and Advanced pages. This page's
 * OK moved, and the row below says why. */
#define DOPT_GC_OK_H 9
#define DOPT_GC_OK_Y (DOPT_GC_Y + DOPT_GC_H - DOPT_GC_OK_H - DOPT_GC_MARGIN1)

/* THE BOTTOM ROW OF GAME CONTROLS: Sound Controls beside Options Menu, as a centred
 * pair. The Sound Controls and Options Menu buttons under GAME CONTROLS belong side by
 * side, centred, and large enough to hold the whole SOUND CONTROLS label.
 *
 * WHAT WAS WRONG. Sound Controls was a hard 64 wide at DOPT_GC_X + 6, a literal that was
 * never derived from the label it has to hold. "Sound Controls" measures 82 pixels in
 * GRAD6FNT, so textbtn.cpp:81's own autosize (String_Pixel_Width + 8) wants 90 and the
 * box was 26 short. The label is NOT truncated when that happens: db_print (dosbar.c
 * :393-413) clips to the surface and never to the button, so the text simply printed
 * outside its box, from x=40 to x=121 -- five columns past the dialog's inner border at
 * x=45, onto the battlefield, with its last column sitting on the OK button's first.
 * Observed: "OUND CONTROLS" welded to "OPTIONS MENU". Widening the box was the fix;
 * nothing was being cut off.
 *
 * WHERE THE NUMBERS COME FROM, because this is a placement neither 1995 nor the
 * cartridge has (see the note at DOPT_C_SOUNDCTRL below):
 *   width 90  goptions.cpp:156's own floor, MAX(maxwidth, 90 * resfactor), and it is
 *             >= 82 + 8 for "Sound Controls" and >= 70 + 8 for "Options Menu", so both
 *             labels clear textbtn.cpp:81's autosize rule with room over.
 *   gap 8     textbtn.cpp:81 again: 8 is the horizontal clearance a button box gives its
 *             own text, so it is this dialog's own unit of air rather than a new one.
 *   x         the pair centred in the DIALOG, which is what is wanted. At 232 wide
 *             that leaves 22 pixels of margin on each side, symmetric.
 * Measured by game/gate_optlayout.c: Sound Controls 66..155, Options Menu 164..253,
 * dialog inner 45..274, both labels inside their own buttons and inside the box. */
#define DOPT_GC_ROW_BTN_W DOPT_MIN_BTN_W
#define DOPT_GC_ROW_GAP 8
#define DOPT_GC_ROW_X                                                                    \
    (DOPT_GC_X + (DOPT_GC_W - (DOPT_GC_ROW_BTN_W * 2 + DOPT_GC_ROW_GAP)) / 2)
#define DOPT_GC_SOUNDCTRL_X DOPT_GC_ROW_X
#define DOPT_GC_ROW_OK_X (DOPT_GC_ROW_X + DOPT_GC_ROW_BTN_W + DOPT_GC_ROW_GAP)

/* ------------------------------------------------------------------------ *
 * THE JUKEBOX -- sounddlg.cpp's SoundControlsClass, which is the 1995 game's score
 * player: a list of every track the campaign has unlocked, its length and its full
 * name, with stop, play, shuffle and repeat.
 *
 * Every number below is SoundControlsClass::Init (sounddlg.cpp:59-104) at
 * factor == 1, i.e. the 320x200 layout, and is quoted with its line. The dialog is
 * 292x146 and centres itself, so it is wider than the Game Controls box and the same
 * kind of thing: Dialog_Box plus a caption plus gadgets.
 *
 * TWO THINGS HERE ARE OURS AND ARE NAMED:
 *   1. Stop and Play are SHAPE buttons in 1995 (BTN-ST.SHP / BTN-PL.SHP, sounddlg.cpp
 *      :152-167). Those shapes are not in any archive we bake, so they are drawn as the
 *      ordinary green button box with a glyph inside -- a filled square for stop, a
 *      right-pointing triangle for play. Same rects, same press behaviour.
 *   2. The list is drawn by this file rather than by a transcribed ListClass. The row
 *      layout is 1995's: "Track %d\t%d:%02d\t%s" against tabs at 55, 72 and 90
 *      (sounddlg.cpp:271-284), so a row reads  Track 3   2:56   Depth Charge.
 * ------------------------------------------------------------------------ */

#define DOPT_SND_W 292 /* Option_Width                            sounddlg.cpp:61 */
#define DOPT_SND_H 146 /* Option_Height                           sounddlg.cpp:62 */
#define DOPT_SND_X ((DOPT_SCREEN_W - DOPT_SND_W) / 2)  /*          sounddlg.cpp:64 */
#define DOPT_SND_Y ((DOPT_SCREEN_H - DOPT_SND_H) / 2)  /*          sounddlg.cpp:65 */

#define DOPT_SND_LIST_X (DOPT_SND_X + 1)   /* Listbox_X            sounddlg.cpp:67 */
#define DOPT_SND_LIST_Y (DOPT_SND_Y + 54)  /* Listbox_Y            sounddlg.cpp:68 */
#define DOPT_SND_LIST_W 290                /* Listbox_W            sounddlg.cpp:69 */
#define DOPT_SND_LIST_H 73                 /* Listbox_H            sounddlg.cpp:70 */
#define DOPT_SND_ROW_H 8                   /* 6 point text plus a line of air        */
#define DOPT_SND_ROWS (DOPT_SND_LIST_H / DOPT_SND_ROW_H)   /* 9 visible             */

/* The three tab stops, relative to the list's left edge.        sounddlg.cpp:284 */
#define DOPT_SND_TAB1 55
#define DOPT_SND_TAB2 72
#define DOPT_SND_TAB3 90

#define DOPT_SND_BTN_W 85                              /* Button_Width sounddlg.cpp:72 */
#define DOPT_SND_BTN_X (DOPT_SND_X + DOPT_SND_W - (DOPT_SND_BTN_W + 7)) /*        :73 */
#define DOPT_SND_BTN_Y (DOPT_SND_Y + 130)              /* Button_Y     sounddlg.cpp:74 */
#define DOPT_SND_BTN_H 9

#define DOPT_SND_STOP_X (DOPT_SND_X + 5)   /* Stop_X               sounddlg.cpp:76 */
#define DOPT_SND_STOP_Y (DOPT_SND_Y + 129) /* Stop_Y               sounddlg.cpp:77 */
#define DOPT_SND_PLAY_X (DOPT_SND_X + 23)  /* Play_X               sounddlg.cpp:79 */
#define DOPT_SND_PLAY_Y (DOPT_SND_Y + 129) /* Play_Y               sounddlg.cpp:80 */
#define DOPT_SND_GLYPH_W 16                /* the shape buttons' own width, ours    */

#define DOPT_SND_ONOFF_W 25                    /* OnOff_Width      sounddlg.cpp:82 */
#define DOPT_SND_SHUFFLE_X (DOPT_SND_X + 91)   /* Shuffle_X        sounddlg.cpp:88 */
#define DOPT_SND_SHUFFLE_Y (DOPT_SND_Y + 130)  /* Shuffle_Y        sounddlg.cpp:90 */
#define DOPT_SND_REPEAT_X (DOPT_SND_X + 166)   /* Repeat_X         sounddlg.cpp:92 */
#define DOPT_SND_REPEAT_Y (DOPT_SND_Y + 130)   /* Repeat_Y         sounddlg.cpp:93 */

#define DOPT_SND_MVOL_X (DOPT_SND_X + 147)  /* MSlider_X           sounddlg.cpp:95 */
#define DOPT_SND_MVOL_Y (DOPT_SND_Y + 28)   /* MSlider_Y           sounddlg.cpp:96 */
#define DOPT_SND_FXVOL_Y (DOPT_SND_Y + 40)  /* FXSlider_Y          sounddlg.cpp:101 */
#define DOPT_SND_VOL_W 108                  /* MSlider_W           sounddlg.cpp:97 */
#define DOPT_SND_VOL_H 5                    /* MSlider_Height      sounddlg.cpp:98 */

/* Sliders first so an index below DOPT_S_LIST is always a slider, the same rule the
 * Game Controls page uses. */
typedef enum
{
    DOPT_S_MUSIC = 0,
    DOPT_S_SOUND,
    DOPT_S_LIST,      /* the whole listbox; the row is worked out from the y        */
    DOPT_S_STOP,
    DOPT_S_PLAY,
    DOPT_S_SHUFFLE,
    DOPT_S_REPEAT,
    DOPT_S_OK,        /* TXT_OPTIONS_MENU, back to Game Controls                   */
    DOPT_S_COUNT
} DOPT_Snd;

/* One row of the list. The caller owns the storage and it must outlive the dialog. */
typedef struct
{
    const char *base;     /* "AIRSTRIK", the archive name                          */
    const char *fullname; /* "Air Strike", CONQUER.ENG via TXT_THEME_*              */
    int seconds;          /* Theme.Track_Length                                    */
    int index;            /* the caller's own id, handed back verbatim on play     */
} DOPT_Track;

/* What the dialog asks the caller to do. The dialog owns no audio device. */
#define DOPT_JB_PLAY 1  /* play the track whose `index` is handed over             */
#define DOPT_JB_STOP 2
#define DOPT_JB_SHUFFLE 3 /* arg is the new on/off                                 */
#define DOPT_JB_REPEAT 4  /* arg is the new on/off                                 */

/* ------------------------------------------------------------------------ *
 * Colours.
 *
 * The green ramp is defines.h:2878-2886, the same indices the main menu uses, and they
 * are palette POSITIONS: this dialog is drawn over the tactical view in the mission's
 * own palette (TEMPERAT.PAL, which dossidebar.pack already carries), exactly as the 1995
 * engine drew it over Map.Render()'s output in the mission palette.
 * ------------------------------------------------------------------------ */

#define DOPT_GREEN_SHADOW 140  /* CC_GREEN_SHADOW                    defines.h:2881 */
#define DOPT_GREEN_BKGD 141    /* CC_GREEN_BKGD                      defines.h:2882 */
#define DOPT_GREEN_CORNERS 141 /* CC_GREEN_CORNERS = CC_GREEN_BKGD   defines.h:2883 */
#define DOPT_LIGHT_GREEN 159   /* CC_LIGHT_GREEN                     defines.h:2884 */
#define DOPT_GREEN_BOX 159     /* CC_GREEN_BOX = CC_LIGHT_GREEN      defines.h:2885 */
#define DOPT_BRIGHT_GREEN 167  /* CC_BRIGHT_GREEN, the underline     defines.h:2886 */
#define DOPT_CC_GREEN 3        /* CC_GREEN = GREEN                   defines.h:2878 */

/* dialog.cpp:366-368 _textpalmedium[CC_GREEN] and _textpalbright[CC_GREEN]. */
#define DOPT_TEXT_MEDIUM 41
#define DOPT_TEXT_BRIGHT 4

/* THE DISABLED LOOK. Identical to the main menu's, and for the same two reasons:
 *   textbtn.cpp:296-305 gives a disabled TPF_6PT_GRAD button BOXSTYLE_GREEN_DIS_RAISED,
 *   which is row 9 of dialog.cpp:97-111 ButtonColorsClassic, {DKGREY, BLACK, LTGREY,
 *   DKGREY}; and textbtn.cpp:349-350 prints its label with flags == 0, which walks
 *   Simple_Text_Print down to a font palette of 0,0,0,0 then CC_GREEN in 4..15.
 * The bevel GEOMETRY is unchanged (row 9 is neither GREEN_BOX nor GREEN_BORDER, so
 * dialog.cpp:163-172 still takes the default arm). Only the ink changes. See
 * menu/dosmenu.h for the full walk-through; these are the same four numbers. */
#define DOPT_DIS_FILL 13     /* DKGREY */
#define DOPT_DIS_SHADOW 12   /* BLACK  */
#define DOPT_DIS_HILITE 14   /* LTGREY */
#define DOPT_DIS_CORNERS 13  /* DKGREY */
#define DOPT_TEXT_DISABLED 3 /* CC_GREEN itself, ungraded */

/* ------------------------------------------------------------------------ *
 * THE DIALOG'S OWN WIDGETS, exported.
 *
 * These four are what makes a screen LOOK like a 1995 C&C dialog: the inset green
 * border on black, the filigreed and underlined caption, the green button plate and the
 * sunken green gauge. They were static here until the DATABASE codex needed the same
 * look (game/codex_mod.h); exporting them rather than copying them is the difference
 * between one dialog style in this program and two that agree until somebody retunes a
 * bevel.
 *
 * All four draw into an 8-bit DB_Surface in the DOS palette, and take a rect as
 * (x, y, w, h) with w and h being SIZES, not last-pixel coordinates.
 * ------------------------------------------------------------------------ */

/* dialog.cpp:64 Dialog_Box -> BOXSTYLE_GREEN_BORDER: black, with a one-pixel green
   rectangle inset by one. */
void dopt_style_dialog(DB_Surface *s, int x, int y, int w, int h);

/* goptions.cpp:507 Draw_Caption: the two OPTIONS.SHP filigrees at the top corners, the
   caption centred in GRAD6FNT through the gradient palette, and the bright rule under
   it the exact width of the text. `w` is the width of the box being captioned. */
void dopt_style_caption(DB_Surface *s, const DB_Pack *p, const char *text,
                        int x, int y, int w);

/* The green button plate. `pressed` swaps the bevel; `disabled` takes it to the grey
   ramp. Print the label over it through db_font_palette_grad with DOPT_TEXT_BRIGHT when
   it is pressed or current and DOPT_TEXT_MEDIUM otherwise. */
void dopt_style_plate(DB_Surface *s, int x, int y, int w, int h, int pressed,
                      int disabled);

/* The sunken green gauge. `fill_to_x` is an ABSOLUTE surface column, not a width:
   anything below x + 1 leaves it empty. */
void dopt_style_track(DB_Surface *s, int x, int y, int w, int h, int fill_to_x);

/* goptions.cpp:249 Draw_Caption(TXT_OPTIONS, ...) -> goptions.cpp:516-519 OPTION_CONTROLS,
 * which is defines.h:2901 frame 2, and the mirrored partner is frame 3.
 * gamedlg.cpp:226 Draw_Caption(TXT_GAME_CONTROLS, ...) lands on the same pair. */
#define DOPT_FILIGREE_LEFT 2
#define DOPT_FILIGREE_RIGHT 3

/* ------------------------------------------------------------------------ *
 * The pause dialog's items.
 *
 * Order is goptions.cpp:88-96 _constants[], with DOPT_EXIT inserted after Abort Mission:
 * ours, and the escalation the 1995 pair never had here (Abort leaves the mission, Exit
 * leaves the program). See the header comment.
 * ------------------------------------------------------------------------ */
typedef enum
{
    DOPT_LOAD = 0, /* TXT_LOAD_MISSION      conquer.h:70  "Load Mission"     */
    DOPT_SAVE,     /* TXT_SAVE_MISSION      conquer.h:71  "Save Mission"     */
    DOPT_DELETE,   /* TXT_DELETE_MISSION    conquer.h:72  "Delete Mission"   */
    DOPT_GAME,     /* TXT_GAME_CONTROLS     conquer.h:76  "Game Controls"    */
    DOPT_VISUALS,  /* ours; the desktop presentation chain. See DOPT_H above.  */
    DOPT_GAMEPLAY, /* ours; how the game is DRIVEN, which is not how it is drawn */
    DOPT_ABORT,    /* TXT_QUIT_MISSION      conquer.h:80  "Abort Mission"    */
    DOPT_EXIT,     /* TXT_EXIT_GAME         conquer.h:81  "Exit Game"  OURS  */
    DOPT_RESUME,   /* TXT_RESUME_MISSION    conquer.h:78  "Resume Mission"   */
    DOPT_RESTATE,  /* TXT_RESTATE_MISSION   conquer.h:653 "Restate"          */
    DOPT_ITEM_COUNT
} DOPT_Item;

/* The Game Controls page. Sliders first, then the one button, so an index below
 * DOPT_C_OK is always a slider. */
typedef enum
{
    DOPT_C_SPEED = 0, /* TXT_SPEED       conquer.h:94  "GAME SPEED:"   */
    DOPT_C_SCROLL,    /* TXT_SCROLLRATE  conquer.h:95  "SCROLL RATE:"  */
    DOPT_C_MUSIC,     /* TXT_MUSIC_VOLUME conquer.h:204 "Music volume:" */
    DOPT_C_SOUND,     /* TXT_SOUND_VOLUME conquer.h:205 "Sound volume:" */
    DOPT_C_SPEECH,    /* ours: the 1995 engine had no separate speech bus */
    /* TXT_SOUND_CONTROLS conquer.h:198. The 1995 dialog had this button and we did not,
       because the page behind it needed ThemeClass and there was none. There is now (the
       score table is in audio/sfxtable.c, generated from theme.cpp), so the button is
       back. WHERE it sits is ours, and it is a THIRD answer rather than a transcription:
         - 1995 stacked Visual Controls and Sound Controls on their own full width rows
           at 192 wide (gamedlg.cpp:82-89) with Options Menu alone on the bottom row.
         - The cartridge has no Sound Controls button inside Game Controls at all: it is
           a sibling page, with its own exit ("Exit Game Controls" at 0x96063e and "Exit
           Sound Controls" at 0x96067a are two separate strings in cnc_eu.z64).
         - Ours shares the bottom row with Options Menu as a centred pair, because that
           was asked for, and because the 1995 band those two buttons
           occupied now carries the three volume rows that replaced them.
       Declared rather than hidden; the geometry and its derivation are at
       DOPT_GC_ROW_BTN_W above, and a known gap covers the deviation. */
    DOPT_C_SOUNDCTRL,
    DOPT_C_OK,        /* TXT_OPTIONS_MENU conquer.h:199 "Options Menu"  */
    DOPT_C_COUNT
} DOPT_Ctrl;

typedef enum
{
    DOPT_PAGE_OPTIONS = 0,
    DOPT_PAGE_CONTROLS,
    DOPT_PAGE_VISUALS,   /* CLASSIC / ENHANCED, and the way into ADVANCED */
    DOPT_PAGE_ADVANCED,  /* one checkbox per element, on/off only         */
    DOPT_PAGE_SOUND,     /* sounddlg.cpp's SoundControlsClass: the jukebox */
    DOPT_PAGE_CHEATS,    /* OURS: the testing switches, opened with *       */
    DOPT_PAGE_CONFIRM,   /* the abort confirmation, goptions.cpp:425        */
    /* OURS: the input switches. APPENDED after CONFIRM rather than inserted beside
       VISUALS because four gates match the page numbers as LITERALS (measured:
       page=3 ADVANCED, page=4 SOUND, page=5 CHEATS, page=6 and page=0 for CONFIRM
       and OPTIONS). Inserting would renumber three of them silently. */
    DOPT_PAGE_GAMEPLAY
} DOPT_Page;

/* THE ABORT CONFIRMATION, which 1995 raises from the same button.
 *
 * goptions.cpp:421-447 is the BUTTON_QUIT arm, and for a normal game it is
 *   WWMessageBox().Process(TXT_CONFIRM_EXIT, TXT_ABORT, TXT_CANCEL, TXT_RESTART)
 * so the question and all three labels are the engine's, not ours. The strings are ids
 * into the disc's own text: TXT_CONFIRM_EXIT 216, TXT_ABORT 704, TXT_RESTART 705,
 * TXT_CANCEL 27, read out of LOCAL.MIX rather than trusted from the truncated header
 * comments.
 *
 * The order below is SCREEN order, which is also the order msgbox.cpp places them in and
 * therefore the order the keyboard walks: button1 left, button3 centred, button2 right
 * (msgbox.cpp:174-196). Index 0 is the default because msgbox.cpp:216 boots curbutton
 * there. */
typedef enum
{
    DOPT_CF_ABORT = 0,   /* TXT_ABORT   704  "Abort"    msgbox button1, left   */
    DOPT_CF_RESTART,     /* TXT_RESTART 705  "Restart"  msgbox button3, centre */
    DOPT_CF_CANCEL,      /* TXT_CANCEL   27  "Cancel"   msgbox button2, right  */
    DOPT_CF_COUNT
} DOPT_Cf;

/* The three strings, quoted from the disc. */
#define DOPT_CF_MSG     "Do you want to abort the mission?"
#define DOPT_CF_ABORT_S "Abort"
#define DOPT_CF_RESTART_S "Restart"
#define DOPT_CF_CANCEL_S  "Cancel"

/* msgbox.cpp's own arithmetic, each constant with the line it comes from, so the box is
 * measured rather than a literal. */
#define DOPT_CF_MIN_W  50   /* msgbox.cpp:157  MAX(w,50)          */
#define DOPT_CF_PAD_W  40   /* msgbox.cpp:157  ... + 40           */
#define DOPT_CF_PAD_H  60   /* msgbox.cpp:159  textheight + 60    */
#define DOPT_CF_EDGE   10   /* msgbox.cpp:177  x+10 / x+w-(bw+10) */
#define DOPT_CF_TEXT_X 20   /* msgbox.cpp:246                     */
#define DOPT_CF_TEXT_Y 25   /* msgbox.cpp:247                     */
#define DOPT_CF_BTN_MIN 30  /* msgbox.cpp:123  MAX(b1+8, 30)      */

/* ------------------------------------------------------------------------ *
 * THE CHEAT PAGE. Ours in full; 1995 had cheats but reached them by typing at the
 * keyboard rather than through a dialog, and the Remaster reaches them through a debug
 * interface with no screen of its own.
 *
 * It is a PAGE of this dialog rather than a module beside it, for the reason the Visuals
 * pages give: the box, the caption, the checkbox, the keyboard walk, the disabled look
 * and the hit test are all already written here, and a second copy of them would be a
 * second thing that can disagree with the first.
 *
 * It is opened directly, not walked to, so it does not appear in the pause menu's list.
 * A testing switch is not a game option and putting it in the same list would invite a
 * player to find it by accident.
 * ------------------------------------------------------------------------ */
typedef enum
{
    /* The order is the order they are read down the page, and it is grouped: the four
       that GIVE you something, then the two that take a rule away. */
    DOPT_CH_MONEY = 0,   /* credits are topped up and never fall            */
    DOPT_CH_INSTANT,     /* anything under construction finishes at once    */
    DOPT_CH_TECH,        /* the whole build list, with no prerequisites     */
    DOPT_CH_SUPER,       /* every super weapon, granted and always ready    */
    DOPT_CH_BUILDANY,    /* build away from your base (terrain still rules) */
    DOPT_CH_FOG,         /* ON is the normal game: the map starts hidden    */
    DOPT_CH_INVULN,      /* your own things cannot be hurt                  */
    DOPT_CH_TOGGLES,     /* how many of the above there are                 */
    /* The two one-shot BUTTONS, which are not switches and so sit outside the count
       above: there is no "on" state to keep, they just happen when pressed. */
    DOPT_CH_WIN = DOPT_CH_TOGGLES,
    DOPT_CH_LOSE,
    DOPT_CH_RESET,       /* put every switch back to its default            */
    DOPT_CH_OK,
    DOPT_CH_COUNT
} DOPT_Cheat;

typedef struct
{
    int on[DOPT_CH_TOGGLES];
} DOPT_Cheats;

/* Geometry: the Game Controls box again, exactly as the Advanced page reuses it. SEVEN
   rows at a step of 11 start at DOPT_CH_TOP and still end clear of the rows below, so
   unlike the Advanced column this one needs no tightened step. Worked through when the
   seventh row went in on 26 Aug 2026 rather than assumed: DOPT_GC_Y is 29 and
   DOPT_GC_TOP_MARGIN is 30, so the rows run 61, 72 ... 127 and the last one ends at
   134. The Instant Win / Instant Lose pair sits at 143 and the OK row at 156, so there
   are nine clear pixels above the buttons and four below them. */
#define DOPT_CH_STEP 11
#define DOPT_CH_TOP (DOPT_V_Y + DOPT_GC_TOP_MARGIN + 2)
/* The bottom row is the Game Controls centred pair, which is where this dialog already
   puts two buttons that share a row. */
#define DOPT_CH_RESET_X DOPT_GC_ROW_X
#define DOPT_CH_OK_X DOPT_GC_ROW_OK_X
/* One button row above the Reset/OK row, DERIVED from it rather than written out, so
   the two cannot drift into each other if the dialog box is ever resized. */
#define DOPT_CH_BTN_Y (DOPT_GC_OK_Y - DOPT_GC_OK_H - 4)

/* ------------------------------------------------------------------------ *
 * The Visuals pages. OURS in full: there is no 1995 counterpart, because there was no
 * desktop presentation chain to switch. They are pages of THIS dialog rather than a
 * module of their own so that the main menu and the pause menu open one screen and not
 * two copies of it, and so that the keyboard walk, the disabled look and the hit test
 * are the ones already written here.
 *
 * ADVANCED IS CHECKBOXES, NOT SLIDERS, on purpose: the F5 panel is where a value gets
 * tuned, and this is where a player turns a thing off. Mixing the two would make the
 * pause menu a second tuning surface that could disagree with the first.
 * ------------------------------------------------------------------------ */
typedef enum
{
    DOPT_V_CLASSIC = 0, /* mutually exclusive with ENHANCED  */
    DOPT_V_ENHANCED,
    DOPT_V_ADVANCED,    /* disabled while CLASSIC is chosen  */
    DOPT_V_OK,
    DOPT_V_COUNT
} DOPT_Vis;

/* One per major element of the chain. The order is the order they run in. */
typedef enum
{
    /* FIRST, and not because it runs first -- it is not a post pass at all. It is the one
       element here that changes how the GAME reads rather than how the picture is graded,
       so it goes where the eye lands. It is a button under ENHANCED called Smooth
       Animations, on by default; with it off the original non-interpolated animations
       are used. */
    DOPT_VE_SMOOTH = 0,
    /* A button called New HUD, enabled by default under ENHANCED mode; Classic mode
       keeps the old DOS HUD. Like the two above it this is not a post pass; it selects which sidebar the
       game draws. It was previously reachable only through the CNC3D_HUD=new environment
       variable, which is a developer's switch and not a player's. */
    DOPT_VE_NEWHUD,
    DOPT_VE_BILINEAR,
    /* WHICH TERRAIN TILE ART DRAWS, and the one row on this page that is not a
       checkbox: it is a three-way drop list. Sits with the other presentation rows
       rather than in the post chain, because like the HUD and the filter it changes
       what is DRAWN and not how the frame is graded. The row's own value lives in
       DOPT_Visuals::texset rather than in elem[], which stays a pure boolean array.
       See FxState::texset. */
    DOPT_VE_TEXSET,
    /* The same three-way drop list for the infantry sprites, immediately below the
       terrain one. Its value lives in DOPT_Visuals::infset for the same reason the
       terrain one lives in ::texset -- elem[] is a boolean array. */
    DOPT_VE_INFSET,
    DOPT_VE_GAMMA,
    DOPT_VE_SUPERSAMPLE,
    DOPT_VE_SHADOWS,
    DOPT_VE_OCCLUSION,
    DOPT_VE_LIGHT,
    DOPT_VE_BLOOM,
    DOPT_VE_GRADE,
    DOPT_VE_CRT,
    DOPT_VE_COUNT
} DOPT_VisElem;

#define DOPT_A_OK DOPT_VE_COUNT          /* the OK button follows the checkboxes */
/* The scroll bar is the LAST item on the page, after OK, and that ordering is
   load-bearing: every index below DOPT_A_OK is still an element row, so dopt_activate's
   `item < DOPT_VE_COUNT` toggle test and dopt_draw_advanced's row loop both keep working
   without learning that the bar exists. */
#define DOPT_A_BAR (DOPT_VE_COUNT + 1)
#define DOPT_A_COUNT (DOPT_VE_COUNT + 2)

typedef struct
{
    int enhanced;                 /* 0 = CLASSIC, 1 = ENHANCED              */
    int elem[DOPT_VE_COUNT];      /* per element, meaningful only when on   */
    /* DOPT_VE_TEXSET's value: one of DOPT_TEX_*. It is here rather than in elem[]
       because elem[] is a boolean array and this row is a three-way choice. */
    int texset;
    /* DOPT_VE_INFSET's value: one of DOPT_TEX_*, the same numbering. */
    int infset;
    /* Whether the Remastered entry can be chosen at all -- the host sets this from
       whether it found an install. A false here greys the entry and gives it the
       tooltip; it never hides it, because 1995 draws a disabled gadget rather than
       removing it (gadget.cpp:632) and a player has to be able to see the option
       exists before they understand what would unlock it. */
    int remaster_ok;
    /* Set when the MAIN MENU opened this screen directly rather than the pause dialog
       walking to it. OK then closes the whole dialog instead of stepping back to a
       pause menu that is not on screen. */
    int from_menu;
} DOPT_Visuals;

/* The drop list's entries, in the order they are drawn. Matches FxState's FX_TEX_*
   numbering one for one, deliberately: the dialog and the renderer must not need a
   translation table between them. */
enum { DOPT_TEX_N64 = 0, DOPT_TEX_DOS = 1, DOPT_TEX_REMASTER = 2, DOPT_TEX_COUNT = 3 };



/* Geometry: the Game Controls box, reused, because it is already the right size and
   already centred. The checkbox column steps 10 rows so nine of them plus the caption
   land clear of the OK button at row 127. */
#define DOPT_V_X DOPT_GC_X
#define DOPT_V_Y DOPT_GC_Y
#define DOPT_V_W DOPT_GC_W
#define DOPT_V_H DOPT_GC_H
#define DOPT_V_BTN_W (DOPT_GC_W - 60)
#define DOPT_V_BTN_H 9
#define DOPT_V_BTN_X (DOPT_V_X + 30)
#define DOPT_V_TOP (DOPT_V_Y + DOPT_GC_TOP_MARGIN)
#define DOPT_V_STEP 11
#define DOPT_V_GAP 8                     /* between ENHANCED and ADVANCED */
#define DOPT_A_TOP (DOPT_V_Y + DOPT_GC_TOP_MARGIN - 4)
/* THE COLUMN'S PITCH, AND WHY IT IS BACK AT NINE.
   The column starts at DOPT_A_TOP (55) and the OK button's top edge is at DOPT_GC_OK_Y
   (156). At a step of 10 the ELEVENTH checkbox would sit at y=155 and end at 162,
   straight through the button, so 9 was the answer for eleven rows: row 10 sits at 145
   and ends at 151, four clear of the button.
   A TWELFTH ROW forced 9 down to 8 for a while, because a twelfth row at a step of 9
   lands at 154 and runs through the button, and the box had to shrink to 6 with it.
   That compromise is now paid off rather than tightened again: this page is a SCROLLING
   LIST, so the number of rows it HOLDS and the number of rows it SHOWS are two different
   numbers, and only the second one has to fit between the caption and the OK button. The
   pitch therefore goes back to the roomier 9 and the well shows DOPT_A_VIEW_ROWS of
   however many elements exist.
   The BOX moves with the step and always has: the layout gate requires at least 2 rows
   between two items sharing a column and that gap is step minus box, so 9-7 is the same
   2 that 8-6 was. 9-6 would waste a row of ink and 8-7 would be 1 and would turn that
   gate red on every adjacent pair. The column top is deliberately left where it is, so
   the clearance under the caption stays the one already checked. */
#define DOPT_A_STEP 9
#define DOPT_A_BOX 7                     /* the checkbox square */
#define DOPT_A_BOX_X (DOPT_V_X + 16)
#define DOPT_A_LABEL_X (DOPT_A_BOX_X + DOPT_A_BOX + 6)

/* THE WELL, AND THE SCROLL BAR DOWN ITS RIGHT-HAND EDGE.
 *
 * This page used to be a fixed budget: twelve rows was everything that fitted between
 * the caption and the OK button, and a thirteenth element had nowhere to go. A list
 * longer than its well is what the 1995 toolkit's ListClass is for, and it answers it
 * with a SliderClass in LIST MODE. That is what this is, and none of it is a new widget:
 *
 *   - list.cpp:82 `ScrollGadget(0, x + w, y, 0, h, true)` puts the bar on the list's own
 *     right edge, and list.cpp:566 `Width -= ScrollGadget.Width` makes the LIST narrower
 *     to pay for it rather than hanging the bar off its side. DOPT_A_ROW_W is that
 *     subtraction, plus the 2 columns the layout gate demands between two items sharing
 *     a row (goptions.cpp:130's own stack gap).
 *   - slider.cpp:68-82: with belong_to_list true the slider builds NO plus/minus shape
 *     gadgets. That matters here, because BTN-PLUS.SHP and BTN-MINS.SHP, which are what
 *     a NON-list slider builds (slider.cpp:69-70), are in no archive this project bakes,
 *     the same gap that leaves the jukebox's Stop and Play as glyphs. BTN-UP.SHP and
 *     BTN-DN.SHP are a different pair again: they are ListClass's own arrows
 *     (list.cpp:80-81), and a list-mode slider does not build those either.
 *     The list-mode slider needs neither, so nothing has to be invented to draw this.
 *   - slider.cpp:339 draws the body BOXSTYLE_GREEN_DOWN and slider.cpp:310 draws the
 *     thumb BOXSTYLE_GREEN_RAISED. Those are exactly the two boxes dopt_draw_slider
 *     already paints for the volume rows, so the bar is this dialog's own slider stood
 *     on end and not a second visual grammar beside it.
 *   - gauge.cpp:59 takes LEFTHELD|LEFTPRESS|LEFTRELEASE and NOT KEYBOARD, which is why
 *     dopt_next_item steps over the bar: it is a pointer gadget, not a stop on the walk.
 *
 * THE NUMBERS, all derived.
 *   The well may not come within 2 rows of the OK button, which is the layout gate's own
 *   minimum, so it ends at DOPT_GC_OK_Y - 2 and DOPT_A_VIEW_H is 99. At a step of 9 that
 *   is exactly 11 rows: the well runs 55..153, its last row sits at 145..151, and OK
 *   starts at 156.
 *   The bar is 5 wide, which is MSlider_Height (sounddlg.cpp:102), the thickness this
 *   dialog's own slider body already has, used on the other axis rather than chosen. The
 *   rows end at 252 and the bar occupies 255..259, inside a box whose inner edge is 274.
 *   MEASURED by game/gate_optlayout.c, which reads these rectangles back at the top AND
 *   at the bottom of travel rather than trusting this paragraph. */
#define DOPT_A_VIEW_H (DOPT_GC_OK_Y - 2 - DOPT_A_TOP)
#define DOPT_A_VIEW_ROWS (DOPT_A_VIEW_H / DOPT_A_STEP)
#define DOPT_A_BAR_W 5   /* MSlider_Height                       sounddlg.cpp:102 */
#define DOPT_A_BAR_GAP 2 /* the layout gate's own minimum        goptions.cpp:130 */
#define DOPT_A_ROW_W (DOPT_V_W - 32 - DOPT_A_BAR_W - DOPT_A_BAR_GAP)
/* The texture row's value box: right-aligned in the row, wide enough for the longest
   entry ("Remastered Textures") at GRAD6FNT's spacing plus the arrow and the padding. */
#define DOPT_A_DROP_W 86
#define DOPT_A_DROP_H (DOPT_A_BOX + 2)
#define DOPT_A_DROP_X (DOPT_A_BOX_X + DOPT_A_ROW_W - DOPT_A_DROP_W)

#define DOPT_A_BAR_X (DOPT_A_BOX_X + DOPT_A_ROW_W + DOPT_A_BAR_GAP)
#define DOPT_A_BAR_Y DOPT_A_TOP
#define DOPT_A_BAR_H (DOPT_A_VIEW_ROWS * DOPT_A_STEP)
#define DOPT_A_THUMB_MIN 4 /* MAX(size, 4)                         slider.cpp:185 */

/* ------------------------------------------------------------------------ *
 * THE GAMEPLAY PAGE. Ours in full, and it is the page for how the game is DRIVEN as
 * against how it is drawn. That distinction is the whole reason it is not a group on the
 * Advanced page: the CLASSIC / ENHANCED master switch governs the PICTURE, and a player
 * who chooses the cartridge's own picture keeps their own mouse. Nothing on this page is
 * gated on that switch, in the dialog or in the host.
 * ------------------------------------------------------------------------ */
#define DOPT_G_HEADING "Enhanced Mode"
#define DOPT_G_HEAD_Y (DOPT_V_Y + DOPT_GC_TOP_MARGIN)   /* under the caption rule */
#define DOPT_G_TOP (DOPT_G_HEAD_Y + 11)                 /* one row under the heading */
#define DOPT_G_STEP DOPT_V_STEP
#define DOPT_G_ROW_W (DOPT_V_W - 32)

typedef enum
{
    /* ON, the button that selects, orders and builds becomes the right one and the button
       that cancels, holds a cameo and navigates the radar becomes the left. OFF by
       default. The exchange happens at the SDL event boundary and nowhere else. */
    DOPT_G_SWAPBTN = 0,
    /* ON, holding the right button and moving past the click threshold PUSHES the view.
       ON by default. OFF, the right button never moves the camera at all. */
    DOPT_G_RPUSH,
    DOPT_G_TOGGLES,
    DOPT_G_OK = DOPT_G_TOGGLES,
    DOPT_G_COUNT
} DOPT_Gp;

typedef struct
{
    int on[DOPT_G_TOGGLES];
} DOPT_Gameplay;

/* What the caller has to do about a click. Everything else is handled internally. */
#define DOPT_ACT_NONE 0
#define DOPT_ACT_RESUME 1 /* close the dialog and let the world tick again  */
#define DOPT_ACT_ABORT 2  /* end the mission, show the main menu            */
#define DOPT_ACT_EXIT 3   /* close the program                              */
/* THE SAVE LAYER. The dialog does not touch a file: it reports the intent and the host
   calls game_save_slot / game_load_slot, which is the same split every other action here
   uses. There is no slot-picker dialog yet -- 1995's LoadOptionsClass is a 250x156 box
   with a ListClass and an EditClass -- so SAVE takes the next free slot and LOAD takes the
   newest slot belonging to the mission that is running. Recorded as a known gap. */
#define DOPT_ACT_SAVE 4
#define DOPT_ACT_LOAD 5
/* Restart the mission from the beginning. 1995's Do_Restart calls Start_Scenario with
   briefing = false (scenario.cpp:728), so the shell goes straight back into the mission
   and does NOT replay the requirement. */
#define DOPT_ACT_RESTART 6
/* The cheat page's two one-shot buttons. They are ACTIONS rather than another entry in
   DOPT_Cheats because there is no state to hold: the host asks the engine to flag the
   verdict and the mission ends on its own a tick later. */
#define DOPT_ACT_CHEAT_WIN 7
#define DOPT_ACT_CHEAT_LOSE 8

/* Keys, so this file does not include SDL. */
#define DOPT_KEY_UP 1
#define DOPT_KEY_DOWN 2
#define DOPT_KEY_LEFT 3
#define DOPT_KEY_RIGHT 4
#define DOPT_KEY_ENTER 5
#define DOPT_KEY_ESC 6

/* ------------------------------------------------------------------------ *
 * Settings, and the one seam the mixer plugs into.
 *
 * Speed and scroll rate are OptionsClass::GameSpeed and OptionsClass::ScrollRate,
 * 0..MAX_*_SETTING-1 (options.h:43-44, both 7). Volumes are OptionsClass::ScoreVolume
 * and OptionsClass::Volume, unsigned char 0..255 (options.h:84-85), and the sliders that
 * drive them are Set_Maximum(255) / Set_Thumb_Size(16) (sounddlg.cpp:231-236), so the
 * top of travel is 255 - 16 = 239, exactly as in 1995.
 *
 * SPEECH IS OURS. The 1995 engine has two volumes, not three: speech is mixed at the
 * sound effects volume (options.cpp:275 Set_Sound_Volume covers both). A mixer with a
 * separate speech bus wants its own control, so there is a third row.
 * ------------------------------------------------------------------------ */

#define DOPT_MAX_SPEED 7   /* OptionsClass::MAX_SPEED_SETTING     options.h:44 */
#define DOPT_MAX_SCROLL 7  /* OptionsClass::MAX_SCROLL_SETTING    options.h:43 */
#define DOPT_VOL_MAX 255   /* sounddlg.cpp:231                                 */
#define DOPT_VOL_THUMB 16  /* sounddlg.cpp:232                                 */
#define DOPT_VOL_TOP (DOPT_VOL_MAX - DOPT_VOL_THUMB) /* the real top of travel */

typedef struct
{
    int speed;      /* 0 .. DOPT_MAX_SPEED-1,  higher is faster  */
    int scrollrate; /* 0 .. DOPT_MAX_SCROLL-1, higher is faster  */
    int music;      /* 0 .. DOPT_VOL_TOP                          */
    int sound;      /* 0 .. DOPT_VOL_TOP                          */
    int speech;     /* 0 .. DOPT_VOL_TOP                          */
} DOPT_Settings;

/* THE MIXER / RENDERER BINDING.
 *
 * The module never calls the mixer, the renderer or anything else. It calls `apply`
 * whenever a value changes and hands over the whole settings block; the host turns that
 * into whatever its audio backend and its camera want. With `apply` NULL the sliders
 * still move, still draw and still remember their values, and nothing is heard: that is
 * the stub case, and dopt_bound() reports it honestly so a caller can print
 * "volume sliders are not wired to anything yet" rather than pretend. */
typedef struct
{
    void *user;
    void (*apply)(void *user, const DOPT_Settings *s);
    /* The same seam for the Visuals pages. NULL is legal and means the toggles move,
       draw and remember, and nothing happens to the picture. */
    void (*applyvis)(void *user, const DOPT_Visuals *v);
    /* And the same seam again for the cheat page. NULL means the switches move, draw and
       remember, and nothing in the game changes. */
    void (*applych)(void *user, const DOPT_Cheats *c);
    /* AND THE SAME SEAM AGAIN for the Gameplay page, a seam of its own rather than
       two more booleans on DOPT_Visuals ON PURPOSE: the host's visuals arm CANNOT
       reach the input settings even by accident. */
    void (*applygp)(void *user, const DOPT_Gameplay *g);
} DOPT_Bind;

/* ------------------------------------------------------------------------ */

typedef struct
{
    int page;     /* DOPT_PAGE_*                                              */
    /* The jukebox. `tracks` is borrowed, never owned. */
    const DOPT_Track *tracks;
    int ntracks;
    int trkSel;   /* the highlighted row, an index into tracks[]               */
    int trkTop;   /* first visible row                                         */
    /* The Advanced page's own first visible row, and the same kind of number: it is
       CurValue on that page's scroll bar (slider.cpp:186) and the element index drawn
       at the top of the well. */
    int advTop;
    /* DOPT_VE_TEXSET's drop list. It is drawn OVER the rows beneath it and hit-tested
       before them, which is the whole of what makes it a drop list rather than another
       row. texhot is the entry under the pointer and is what the tooltip follows; -1
       when the pointer is not on one. */
    /* Which drop list is open: -1 none, else the DOPT_VE_* of the row that owns it.
       One at a time, because a second open list would have to be drawn over the first
       and neither could say which of them a click belongs to. */
    int texdrop;
    int texhot;
    /* The drop control's width, measured from its widest entry at layout time the way
       btnw and okw are, rather than written down. */
    int texw;
    int trkPlaying; /* the row the caller says is sounding, or -1              */
    int shuffle;  /* Options.IsScoreShuffle                                    */
    int repeat;   /* Options.IsScoreRepeat                                     */
    void (*jukebox)(void *, int verb, int arg);
    int lastmy;   /* the pointer's y at the last press: which list row was hit  */
    int selected; /* curbutton on the current page                            */
    int pressed;  /* the item held under the pointer on this page, or -1      */
    int drag;     /* the slider being dragged, or -1                          */
    int dragdiff; /* GaugeClass::ClickDiff, gauge.cpp:299-306                 */

    const char *scenario; /* Scen.ScenarioName, printed bottom right  goptions.cpp:262 */
    const char *version;  /* VersionText, the line under it           goptions.cpp:263 */

    DOPT_Settings set;
    DOPT_Visuals  vis;
    DOPT_Cheats   cheat;
    DOPT_Gameplay gp;
    DOPT_Bind bind;

    /* Cached geometry. goptions.cpp:137-159 sizes every button to the widest label in
     * the list, which needs the font, which needs the pack. dopt_layout fills these in;
     * dopt_open calls it, so a caller that only ever calls dopt_open never sees it. */
    int btnw;   /* MAX(maxwidth, 90) for the pause dialog's stacked buttons */
    int okw;    /* the Game Controls OK button's auto width                 */
    /* The cheat page's bottom pair. "Reset to Defaults" is far wider than the 90 the
       Game Controls pair assumes, and db_print does not truncate: at a fixed 90 the
       label printed straight out through both ends of its own button. Sized to the
       label, exactly as the stacked buttons are. */
    int chresetw;
    /* The confirmation box, measured in dopt_layout the way msgbox.cpp measures it. */
    int cfw, cfh, cfx, cfy;  /* the box                                          */
    int cfbw;                /* Abort and Cancel share msgbox's bwidth           */
    int cf3w;                /* Restart auto sizes (textbtn.cpp:74-84)           */
    int cfprev;              /* the pause page's selection, restored by Cancel   */
    int laidout;
} DOPT_State;

/* Open the dialog. Resets the page, the selection and the drag; KEEPS the settings, so
 * reopening it does not undo what the player set. goptions.cpp:104 `int curbutton = 6;`
 * boots the selection onto Resume Mission, which is what this does. */
void dopt_open(DOPT_State *st, const DB_Pack *p);

/* Settings and binding. Call these before dopt_open, or any time after. dopt_bind fires
 * `apply` once immediately so the host starts in sync with what is drawn. */
void dopt_settings_init(DOPT_Settings *s);
void dopt_bind(DOPT_State *st, void *user, void (*apply)(void *, const DOPT_Settings *));
/* The Visuals seam. Call after dopt_bind (which sets `user`); fires once immediately so
   the host and the checkboxes start in agreement. */
void dopt_bind_visuals(DOPT_State *st, void (*applyvis)(void *, const DOPT_Visuals *));
/* Push the host's current truth INTO the dialog, for the case where something else
   changed it (the F5 panel, a preset loaded from the command line). */
void dopt_set_visuals(DOPT_State *st, const DOPT_Visuals *v);
/* Open straight onto the Visuals page. The main menu uses this; the pause menu walks
   there through its own button. */
void dopt_open_visuals(DOPT_State *st, const DB_Pack *p);

/* Open straight onto the cheat page. There is no route to it from the pause menu on
   purpose; the host binds it to a key. */
void dopt_open_cheats(DOPT_State *st, const DB_Pack *p);

/* The shipped state of every switch, and it is the ordinary game: every cheat off, and
   fog of war on, because ON is what fog of war means when nobody is cheating. */
void dopt_cheats_defaults(DOPT_Cheats *c);

/* Read them back, and be told when one changes. The dialog holds the switches; what they
   MEAN is the host's business, exactly as the volume sliders work. */
void dopt_set_cheats(DOPT_State *st, const DOPT_Cheats *c);
const DOPT_Cheats *dopt_cheats(const DOPT_State *st);
void dopt_bind_cheats(DOPT_State *st, void (*applych)(void *, const DOPT_Cheats *));
/* THE GAMEPLAY PAGE, and the same four calls the cheat page has, for the same reasons. */
void dopt_gameplay_defaults(DOPT_Gameplay *g);
void dopt_set_gameplay(DOPT_State *st, const DOPT_Gameplay *g);
const DOPT_Gameplay *dopt_gameplay(const DOPT_State *st);
void dopt_bind_gameplay(DOPT_State *st, void (*applygp)(void *, const DOPT_Gameplay *));
const char *dopt_gp_label(int item);
/* The one heading on that page, measured HERE so the draw and any dump of it cannot
   disagree about where it lands. 0 when there is no font to measure with. */
int dopt_gp_head_rect(const DOPT_State *st, const DB_Pack *p, int *x, int *y, int *w,
                      int *h);
const char *dopt_cheat_label(int item);

/* THE JUKEBOX. The caller supplies the track list (it is the one that knows which
 * themes the campaign has unlocked) and a callback for the four verbs. Neither is
 * required: with no tracks the page draws an empty list and says so. */
void dopt_set_tracks(DOPT_State *st, const DOPT_Track *tracks, int count, int playing);
void dopt_bind_jukebox(DOPT_State *st, void (*jb)(void *, int verb, int arg));
/* The wheel over a scrolling list: the jukebox's track list, and the Advanced page's
 * element column. Positive scrolls down. Harmless on every other page.
 * IT IS ACTUALLY CALLED NOW, which is worth saying because for a long time it was not:
 * this function shipped with the jukebox and no event loop ever routed a wheel into it,
 * so the line above described a gesture the build did not have. Both dialog loops call
 * it. */
void dopt_scroll(DOPT_State *st, int delta);
/* Which row is highlighted, so the caller can keep the list in step with what the
 * playlist moved on to by itself. -1 for none. */
void dopt_set_playing(DOPT_State *st, int playing);
const char *dopt_vis_elem_label(int elem);
int dopt_bound(const DOPT_State *st); /* 0 = the sliders drive nothing but themselves */

/* Recompute the cached widths. Safe to call every frame; it is a handful of string
 * measurements. */
void dopt_layout(DOPT_State *st, const DB_Pack *p);

/* The item rectangles, in DOS pixels, on the CURRENT page. Returns 0 for an index that
 * is not on this page. For a slider the rectangle is the gauge body, which is what
 * gauge.cpp hit-tests. */
int dopt_item_rect(const DOPT_State *st, int item, int *x, int *y, int *w, int *h);

/* gadget.cpp:632 wraps the whole Clicked_On dispatch in `if (!next_button->IsDisabled)`,
 * so a disabled gadget is drawn and never clicked. Both of these honour that. */
int dopt_item_disabled(const DOPT_State *st, int item);
int dopt_hit_test(const DOPT_State *st, int mx, int my);

/* goptions.cpp:316-349 KN_UP / KN_DOWN, wrapping, skipping what cannot be reached. */
int dopt_next_item(const DOPT_State *st, int item, int delta);
/* How many items the CURRENT page has. Every walker must ask, because with four pages
   a binary page test silently treats an unknown page as the Options page. */
int dopt_page_count(const DOPT_State *st);

/* The verbatim DOS label for an item on the current page. */
const char *dopt_item_label(const DOPT_State *st, int item);

/* The label for one DOPT_VE_TEXSET drop-list entry (a DOPT_TEX_* index). */
const char *dopt_texset_label(int which);

/* The label for one entry of the drop list on `item` -- the terrain row names textures
   and the infantry row names sprites. */
const char *dopt_drop_label(int item, int which);

/* The tooltip shown when the pointer rests on an entry that cannot be chosen, or NULL
   when that entry is selectable. Only Remastered has one. */
const char *dopt_texset_tooltip(const DOPT_State *st, int which);

/* The screen rectangle of one open drop-list entry, or 0 when the list is shut or the
   texture row is scrolled out of the well. For the headless driver: the entries are not
   dialog items, so dopt_item_rect cannot reach them. */
int dopt_texset_item_rect_pub(const DOPT_State *st, int item, int i,
                              int *x, int *y, int *w, int *h);
int dopt_texset_box_rect_pub(const DOPT_State *st, int item,
                             int *x, int *y, int *w, int *h);

/* Input, in DOS pixels. Each returns a DOPT_ACT_*; DOPT_ACT_NONE means it was handled
 * internally (page change, slider move, nothing hit). */
int dopt_press(DOPT_State *st, int mx, int my);
int dopt_motion(DOPT_State *st, int mx, int my);
int dopt_release(DOPT_State *st, int mx, int my);
int dopt_key(DOPT_State *st, int key);

/* Draw the current page into a 320x200 surface. The surface must already hold whatever
 * should show through: this only paints the dialog, and everything it does not touch is
 * left as it was found. Fill with DB_TBLACK first and index 0 comes out transparent. */
void dopt_draw(DB_Surface *s, const DB_Pack *p, const DOPT_State *st);

/* The DOS pointer, MOUSE.SHP frame 0 at its own hotspot (the top left pixel). */
void dopt_draw_cursor(DB_Surface *s, const DB_Pack *p, int mx, int my);

#ifdef __cplusplus
}
#endif

#endif /* DOSOPT_H */
