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
 * DOPT_EXIT is therefore OURS, in the same spirit as the main menu's DM_SPECIAL: an
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
    DOPT_PAGE_GAMEPLAY,
    /* The Restate box (scenario.cpp:780 Restate_Mission), appended for the same
       reason. Page 8. */
    DOPT_PAGE_RESTATE,
    /* The slot dialog (loaddlg.cpp LoadOptionsClass), for Load, Save and Delete
       Mission alike. Page 9. */
    DOPT_PAGE_SLOTS,
    /* OURS: a notice with a caption, a few lines of text and a lone OK, for a mission
       start that was refused. The Restate box's geometry and wrap, with no pause page
       under it and the caption supplied by the caller. Page 10. */
    DOPT_PAGE_NOTICE
} DOPT_Page;

/* ------------------------------------------------------------------------ *
 * THE SLOT DIALOG. loaddlg.cpp:106-260 LoadOptionsClass::Process, factor 1: ONE
 * dialog for the three pause buttons, told which it is by its Style (LOAD, SAVE or
 * WWDELETE). A 250x156 box centred on the screen; a caption of Load Mission (53),
 * Save Mission (54) or Delete Mission (55); a ListClass 236 wide and 104 tall (74 in
 * SAVE mode, which gives 30 rows to the edit field); in SAVE mode an EditClass 236x13
 * under a "Mission Description" (217) label; and two 40x13 buttons on the bottom row,
 * Load (56) / Save (57) / Delete (58) on the left of centre and Cancel (27) on the
 * right. Every number below is that routine's, with its line.
 *
 * The rows are "(GDI) description" newest first (loaddlg.cpp:610-690: the house
 * prefix, then the description, sorted on the file's date). SAVE mode puts
 * "[EMPTY SLOT]" (249) first, carrying the lowest unused file number
 * (loaddlg.cpp:661-680), and a click on an existing row copies its description into
 * the edit field with the prefix stripped (loaddlg.cpp:466-495). Save with an empty
 * description is refused: 1995 raises "You must enter a description!" (246); this
 * dialog greys the Save button until something is typed, which is the same refusal
 * without a second box. Delete asks "Delete this file?" (248) with Yes and No, and
 * stays in the dialog afterwards so several can go (loaddlg.cpp:447-461).
 *
 * WHAT THE DIALOG OWNS AND WHAT THE HOST DOES. The dialog holds the rows, the
 * selection, the edit buffer and the geometry. It asks the host for the rows through
 * the `slots` seam in DOPT_Bind when the page opens and after a delete, because the
 * host is the one that can read the slot index, and it reports DOPT_ACT_LOAD,
 * DOPT_ACT_SAVE or DOPT_ACT_DELETE with the chosen file number readable through
 * dopt_slot_pick (and the typed text through dopt_slot_descr). The host does the file
 * work, exactly as it does for every other action here.
 *
 * ListClass has no scroll arrows on this build (BTN-UP.SHP and BTN-DN.SHP are baked
 * into no pack, the same gap the jukebox and the Advanced page have), so a list longer
 * than its well scrolls with the wheel and the keys, the way those two do. Sixteen
 * slots plus the empty row is seventeen against a well of thirteen (nine in SAVE).
 * ------------------------------------------------------------------------ */
#define DOPT_SL_W 250                       /* d_dialog_w    loaddlg.cpp:112 */
#define DOPT_SL_H 156                       /* d_dialog_h    loaddlg.cpp:113 */
#define DOPT_SL_X ((DOPT_SCREEN_W - DOPT_SL_W) / 2)   /*         loaddlg.cpp:114 */
#define DOPT_SL_Y ((DOPT_SCREEN_H - DOPT_SL_H) / 2)   /*         loaddlg.cpp:115 */
#define DOPT_SL_CX (DOPT_SL_X + DOPT_SL_W / 2)        /* d_dialog_cx  :116     */
#define DOPT_SL_TXT8_H 11                   /* d_txt8_h      loaddlg.cpp:117 */
#define DOPT_SL_MARGIN 7                    /* d_margin      loaddlg.cpp:118 */
#define DOPT_SL_LIST_X (DOPT_SL_X + DOPT_SL_MARGIN)                /* :122 */
#define DOPT_SL_LIST_Y (DOPT_SL_Y + DOPT_SL_MARGIN + DOPT_SL_TXT8_H + DOPT_SL_MARGIN) /* :123 */
#define DOPT_SL_LIST_W (DOPT_SL_W - DOPT_SL_MARGIN * 2)            /* :120 */
#define DOPT_SL_LIST_H 104                  /* d_list_h      loaddlg.cpp:121 */
#define DOPT_SL_LIST_H_SAVE (DOPT_SL_LIST_H - 30)   /* list_ht -= 30 in SAVE, :221 */
#define DOPT_SL_ROW_H 8                     /* one 6 point line and a row of air    */
#define DOPT_SL_EDIT_X DOPT_SL_LIST_X                              /* :127 */
#define DOPT_SL_EDIT_W DOPT_SL_LIST_W                              /* :125 */
#define DOPT_SL_EDIT_H 13                   /* d_edit_h      loaddlg.cpp:126 */
#define DOPT_SL_EDIT_Y (DOPT_SL_LIST_Y + DOPT_SL_LIST_H - 30 + DOPT_SL_MARGIN + DOPT_SL_TXT8_H) /* :128 */
#define DOPT_SL_LABEL_Y (DOPT_SL_EDIT_Y - DOPT_SL_TXT8_H)          /* :369 */
#define DOPT_SL_BTN_W 40                    /* d_button_w    loaddlg.cpp:133 */
#define DOPT_SL_BTN_H 13                    /* d_button_h    loaddlg.cpp:135 */
#define DOPT_SL_BTN_X (DOPT_SL_CX - DOPT_SL_BTN_W - DOPT_SL_MARGIN)   /* :136 */
#define DOPT_SL_BTN_Y (DOPT_SL_Y + DOPT_SL_H - DOPT_SL_BTN_H - DOPT_SL_MARGIN) /* :137 */
#define DOPT_SL_CANCEL_X (DOPT_SL_CX + DOPT_SL_MARGIN)             /* :146 */
#define DOPT_SL_DESCR_MAX 40                /* EditClass(..., game_descr, 40, ...) :199 */
#define DOPT_SL_ROWS_MAX 17                 /* sixteen slots and the empty row */
#define DOPT_SL_ROW_TEXT 64                 /* "(GDI) " and a 44 char description */

#define DOPT_SL_EMPTY_S "[EMPTY SLOT]"      /* TXT_EMPTY_SLOT 249 */
#define DOPT_SL_DESCR_S "Mission Description"   /* TXT_MISSION_DESCRIPTION 217 */
#define DOPT_SL_DELQ_S "Delete this file?"  /* TXT_DELETE_FILE_QUERY 248 */

typedef enum { DOPT_SL_LOAD = 0, DOPT_SL_SAVE = 1, DOPT_SL_DELETE = 2 } DOPT_SlotMode;

typedef enum
{
    DOPT_SL_LIST = 0, /* the whole list well; the row is worked out from the y   */
    DOPT_SL_EDIT,     /* the description field, SAVE mode only                   */
    DOPT_SL_OK,       /* Load / Save / Delete                                    */
    DOPT_SL_CANCEL,
    DOPT_SL_COUNT
} DOPT_SlotItem;

/* One row as the host supplies it: the file number and the text to print, which the
   host builds as "(GDI) description" so the dialog need not know what a house is. */
typedef struct
{
    int slot;
    char text[DOPT_SL_ROW_TEXT];
} DOPT_SlotRow;

typedef struct
{
    int mode;                          /* DOPT_SlotMode                          */
    DOPT_SlotRow rows[DOPT_SL_ROWS_MAX];
    int nrows;
    int sel;                           /* the highlighted row                    */
    int top;                           /* the first visible row                  */
    char descr[DOPT_SL_DESCR_MAX + 1]; /* the edit field, SAVE mode              */
    char deflt[DOPT_SL_DESCR_MAX + 1]; /* what the empty slot's field opens with */
    int prev;                          /* the pause page's selection, for Cancel */
    int pick;                          /* the file number the last action named  */
    int have;                          /* occupied slots at the last count       */
    int from_menu;                     /* opened with no pause page under it     */
} DOPT_Slots;

/* ------------------------------------------------------------------------ *
 * RESTATE. goptions.cpp:373 BUTTON_RESTATE calls Restate_Mission(Scen.ScenarioName,
 * TXT_VIDEO, TXT_OPTIONS), scenario.cpp:780-836, which raises
 *     WWMessageBox(TXT_OBJECTIVE).Process(Scen.BriefingText, button1, button2)
 * over the pause dialog: the caption "Mission Objective" (646), the mission briefing text
 * wrapped to 255 pixels (msgbox.cpp:150 Format_Window_String), and two buttons,
 * "Video" (642) on the left and "Options" (65) on the right. Video plays the
 * briefing movie (BriefMovie, or ActionMovie when there is no brief) and then
 * CLOSES the pause dialog (goptions.cpp:375-390 `process = false`); Options puts
 * the pause page back. When NEITHER movie file exists, scenario.cpp:797-803 turns
 * the box into a single "OK" (TXT_OK 37) and nothing plays. When there is no
 * briefing TEXT at all, Restate_Mission returns false and goptions plays the movie
 * straight away, with no box.
 *
 * The text is the engine's Scen.BriefingText, which scenarioini.cpp:472-483 reads
 * from the mission INI's [Briefing] block and, when that is empty, from the block
 * named after the scenario in MISSION.INI. The host reads the same two places
 * (game/brief_mod.h) and hands the joined text over through dopt_set_briefing;
 * the dialog wraps it, sizes the box the way msgbox.cpp sizes it, and reports
 * DOPT_ACT_VIDEO when the player asks for the movie. The buffer is 512 because
 * scenario.h:86 makes it 512: every briefing on both discs fits.
 * ------------------------------------------------------------------------ */
typedef enum
{
    DOPT_R_LEFT = 0, /* "Video" (msgbox button1), or the lone "OK" centred     */
    DOPT_R_RIGHT,    /* "Options" (msgbox button2); no rectangle when OK stands */
    DOPT_R_COUNT
} DOPT_Restate;

#define DOPT_R_CAPTION "Mission Objective"  /* TXT_OBJECTIVE 646 */
#define DOPT_R_VIDEO_S "Video"              /* TXT_VIDEO 642     */
#define DOPT_R_OPTIONS_S "Options"          /* TXT_OPTIONS 65    */
#define DOPT_R_OK_S "OK"                    /* TXT_OK 37         */
#define DOPT_R_WRAP_W 255   /* msgbox.cpp:150 Format_Window_String(buffer, 255, ...) */
#define DOPT_R_TEXT_MAX 512 /* Scen.BriefingText, scenario.h:86                     */
#define DOPT_R_MAX_LINES 24 /* 512 chars at 255 px cannot need more                 */
#define DOPT_N_CAPTION_MAX 48 /* the notice page's caption, drawn where Restate's is */

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
/* A MATCH IS NOT A MISSION, so the row that ends it is not called the same thing and does
   not offer the same choices. There is nothing to restart in a game other people are
   playing, and leaving is not aborting: you resign, and then you may stay and watch. */
#define DOPT_CF_MSG_SURR  "Do you want to surrender?"
/* TWO LINES, BECAUSE ONE DID NOT FIT. msgbox.cpp sizes the box to its text and
   centres it, which works while the question is short and runs the box off both
   edges of a 320 wide screen when it is not: this one drew straight through the
   frame. A second line costs the box one row of height and keeps the 1995
   arithmetic, where widening past the screen cannot. The other two questions are
   short enough to stay on one line and pass NULL for the second. */
#define DOPT_CF_MSG_LEAVE  "Do you want to leave"
#define DOPT_CF_MSG_LEAVE2 "the match?"
/* A MATCH ANSWERS YES OR NO. The three-button box is the campaign's -- Abort,
   Restart, Cancel -- and a match has nothing to restart, which left the middle
   button standing there with no meaning. Asked a plain question, the box now
   offers a plain pair, and RESTART is disabled rather than renumbered so that
   every index, rectangle and keyboard stop below stays exactly where it was. */
#define DOPT_CF_YES_S     "Yes"
#define DOPT_CF_NO_S      "No"
#define DOPT_CF_SURR_S    "Surrender"
#define DOPT_CF_LEAVE_S   "Leave"
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
   seventh row went in rather than assumed: DOPT_GC_Y is 29 and
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
    /* THE DISPLAY ROWS, AT THE TOP (5 Sep 2026, by request, in this order). The first
       three are a radio triple over DOPT_Visuals::dispmode -- one lit at a time; the
       Resolution row is a drop list over ::residx, greyed under Windowed Borderless
       because the desktop decides; Reset puts every Advanced row back to fx_defaults;
       UI scaling is a drop list over ::uiscale, Enhanced only. None of them is a boolean
       in elem[], for the reason texset is not. */
    DOPT_VE_FULLSCREEN = 0,
    DOPT_VE_WINDOWED,
    DOPT_VE_BORDERLESS,
    DOPT_VE_RESOLUTION,
    DOPT_VE_RESET,
    DOPT_VE_UISCALE,
    /* PERSPECTIVE (v0.6.8): a two-way drop list, Classic / Isometric. The value lives
       in DOPT_Visuals::perspective, like the other drop rows; see FxState::perspective. */
    DOPT_VE_PERSPECTIVE,
    DOPT_VE_SMOOTH,
    /* A button called New HUD, enabled by default under ENHANCED mode; Classic mode
       keeps the old DOS HUD. Like the two above it this is not a post pass; it selects which sidebar the
       game draws. It was previously reachable only through the CNC3D_HUD=new environment
       variable, which is a developer's switch and not a player's. */
    DOPT_VE_NEWHUD,
    DOPT_VE_BILINEAR,
    /* WATER SHADER: the shore-aware sea (a soft coast, shallows, a flow,
       rivers that run) in place of the cartridge's two-tile wash. A presentation row
       like the HUD and the filter, because it changes what is DRAWN; on by default
       under ENHANCED and, like everything on this page, unreachable under CLASSIC,
       whose sea is the cartridge's. FxState::water_fx. */
    DOPT_VE_WATER,
    /* 3D TREES: the rigged tree models in place of the cartridge's, beside the sea and
       for the same reason, a presentation row that changes what is DRAWN. On by default
       under ENHANCED; CLASSIC draws the cartridge's trees whatever the tick says, and the
       tick stands so choosing ENHANCED again gives them back. FxState::tree3d.

       GRASS AND RAIN HAVE NO ROW HERE, deliberately. Both ship off under ENHANCED as
       well as CLASSIC and are reachable only through the tuning panel until their look
       is settled, which means a release build, whose panel is compiled out, cannot turn
       either of them on. That is the intended state, not an oversight. */
    DOPT_VE_TREES,
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

/* THE RESOLUTION LIST'S TWO SIZES. DOPT_RES_MAX is how many sizes it HOLDS: every size
   the display offers, up to this many, the desktop's own first. DOPT_RES_VIEW is how
   many rows the open list SHOWS at once: seven is what fits between its row and the OK
   button without the open list running through it, and the rest are reached by
   scrolling the list, the way the Advanced well scrolls. They used to be one number,
   which capped the list at the seven largest sizes the driver offered; a driver that
   lists sizes above the panel (a 4K mode on a 1080p display) then pushed every size
   that actually fit off the end. */
enum { DOPT_RES_MAX = 32, DOPT_RES_VIEW = 7 };

typedef struct
{
    int enhanced;                 /* 0 = CLASSIC, 1 = ENHANCED              */
    int elem[DOPT_VE_COUNT];      /* per element, meaningful only when on   */
    /* DOPT_VE_TEXSET's value: one of DOPT_TEX_*. It is here rather than in elem[]
       because elem[] is a boolean array and this row is a three-way choice. */
    int texset;
    /* DOPT_VE_INFSET's value: one of DOPT_TEX_*, the same numbering. */
    int infset;
    /* THE DISPLAY ROWS' VALUES. dispmode is one of DOPT_DISP_*; uiscale is
       0..3 for 1x, -2x, -3x, -4x; res_w/res_h[] is the list the host enumerated from the
       display, nres of them, and residx the chosen one. Entry 0 is the desktop's own
       size and prints as "Desktop"; the rest follow largest first. res_fit[] is the
       host's word on whether a bordered window of that size fits the desktop's usable
       room: an entry that does not is greyed and refused under WINDOWED, with a tip
       that says why, and pickable under FULLSCREEN, where it is a real mode. */
    int dispmode;
    int uiscale;
    int perspective;              /* 0 Classic, 1 Isometric (v0.6.8)             */
    int nres, residx;
    int res_w[DOPT_RES_MAX], res_h[DOPT_RES_MAX], res_fit[DOPT_RES_MAX];
    /* Whether the Remastered entry can be chosen at all -- the host sets this from
       whether it found an install. A false here greys the entry and gives it the
       tooltip; it never hides it, because 1995 draws a disabled gadget rather than
       removing it (gadget.cpp:632) and a player has to be able to see the option
       exists before they understand what would unlock it. */
    int remaster_ok;
    /* Whether the DOS entry can be chosen -- the host sets this from whether THIS MAP'S
       pack actually carries a second terrain atlas. Two theaters (SNOW, SAND) have no
       1995 original at all, and a pack baked before the DOS art existed carries none
       either; in both cases picking DOS silently drew the cartridge and the control
       looked broken. Same rule as remaster_ok: greyed with a reason, never hidden. */
    int dos_tex_ok;
    /* Set when the MAIN MENU opened this screen directly rather than the pause dialog
       walking to it. OK then closes the whole dialog instead of stepping back to a
       pause menu that is not on screen. */
    int from_menu;
} DOPT_Visuals;

/* The drop list's entries, in the order they are drawn. Matches FxState's FX_TEX_*
   numbering one for one, deliberately: the dialog and the renderer must not need a
   translation table between them. */
enum { DOPT_TEX_N64 = 0, DOPT_TEX_DOS = 1, DOPT_TEX_REMASTER = 2, DOPT_TEX_COUNT = 3 };
/* The display modes, the same numbers as fullscreen.h's FS_MODE_*; and the UI scale
   entries. The Resolution list's own two sizes are DOPT_RES_MAX and DOPT_RES_VIEW,
   declared above the struct that holds the list. */
enum { DOPT_DISP_WINDOWED = 0, DOPT_DISP_BORDERLESS = 1, DOPT_DISP_FULLSCREEN = 2 };
enum { DOPT_UI_COUNT = 2 };                     /* 1x and -2x: the two presets the page offers */
enum { DOPT_PERSP_COUNT = 2 };                  /* Classic, Isometric (v0.6.8)          */



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
/* THE VALUE BOX ON A DROP ROW starts DOPT_A_DROP_GAP columns after that row's own
   printed label and runs to the row's right edge (DOPT_A_BOX_X + DOPT_A_ROW_W). The
   label widths are MEASURED at layout time (labw[] in DOPT_State): one right-aligned
   width for every row, sized to the widest art entry, put "Resolution", "UI scaling"
   and "Perspective" under the plate, and no rectangle audit saw it because
   a label's ink is not a rectangle. DOPT_A_DROP_GAP is goptions.cpp:130's own 2, the
   gap gate_optlayout demands between any two controls; DOPT_A_DROP_PAD is the 14 the
   old measure already paid (3 left pad, the 5-wide arrow, its margin, 2 clear), the
   least width a box needs beyond its widest entry, which the audit now checks each box
   against. DOPT_A_DROP_W is ONLY the width used when no font could be loaded. */
#define DOPT_A_DROP_GAP 2
#define DOPT_A_DROP_PAD 14
#define DOPT_A_DROP_W 86
#define DOPT_A_DROP_H (DOPT_A_BOX + 2)

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
    /* ON, the credits readout sounds a tone on every step it takes toward the bank, up
       and down, as the 1995 game and the cartridge both did. ON by default. OFF, the
       readout still steps, silently. A sound rather than a picture, so it lives here and
       not on the Visuals page, for the reason the two above it do. */
    DOPT_G_CASHTICK,
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
#define DOPT_ACT_SURRENDER 20 /* resign the match; the house dies or converts  */
#define DOPT_ACT_LEAVE 21 /* a spectator walks out: score screen, then the menu */
#define DOPT_ACT_EXIT 3   /* close the program                              */
/* THE SAVE LAYER. The dialog does not touch a file: it reports the intent and the host
   calls game_save_slot / game_load_slot, which is the same split every other action here
   uses. Both come out of the slot dialog (DOPT_PAGE_SLOTS), which names the file number
   through dopt_slot_pick and, for a save, the typed description through dopt_slot_descr. */
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
/* Restate's Video button: close the dialog and play the mission briefing movie. The host
   knows which file exists; the dialog only knows one was promised (dopt_set_briefing). */
#define DOPT_ACT_VIDEO 9
/* Delete Mission, answered Yes: the host removes the slot dopt_slot_pick names and then
   calls dopt_slots_reload so the list shows what is left. The dialog stays up. */
#define DOPT_ACT_DELETE 10

/* Keys, so this file does not include SDL. */
#define DOPT_KEY_UP 1
#define DOPT_KEY_DOWN 2
#define DOPT_KEY_LEFT 3
#define DOPT_KEY_RIGHT 4
#define DOPT_KEY_ENTER 5
#define DOPT_KEY_ESC 6
#define DOPT_KEY_BACKSPACE 7   /* the description field, SAVE mode only */

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

/* THE SHIPPED GAME SPEED, one step up from the 1995 default of 3 and the only one of the
   five Game Controls values that stands off the 1995 block above. It is named rather
   than written into dopt_settings_init as a bare literal because two other things have
   to agree with it: the renderer's tick rate before the pause dialog has ever been
   opened, and the speed a network or skirmish match opens at (NM_DEFAULT_SPEED, kept on
   its own side of the network seam and checked against this one at compile time). A
   number that two other places must match wants somewhere to be read from. */
#define DOPT_DEFAULT_SPEED 4

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
    /* RESET TO DEFAULTS on the Advanced page: the host rewrites the block from its ONE
       source of defaults (fx_defaults in game/fx_state.h) and the page then applies it.
       The dialog holds no copy of the numbers; NULL means the button does nothing. */
    void (*resetvis)(void *user, DOPT_Visuals *v);
    /* And the same seam again for the cheat page. NULL means the switches move, draw and
       remember, and nothing in the game changes. */
    void (*applych)(void *user, const DOPT_Cheats *c);
    /* AND THE SAME SEAM AGAIN for the Gameplay page, a seam of its own rather than
       two more booleans on DOPT_Visuals ON PURPOSE: the host's visuals arm CANNOT
       reach the input settings even by accident. */
    void (*applygp)(void *user, const DOPT_Gameplay *g);
    /* THE SLOT LIST. Called when the slot dialog opens and after a delete: the host
       fills `out` with the occupied slots, newest first, each as its file number and
       its "(GDI) description" row, and returns how many. NULL means no save system,
       and the three buttons are then drawn disabled. */
    int (*slots)(void *user, DOPT_SlotRow *out, int max);
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
    /* THE RESOLUTION LIST'S FIRST VISIBLE ENTRY, the same kind of number advTop is for
       the well: the open list holds up to DOPT_RES_MAX entries and shows DOPT_RES_VIEW,
       so the wheel and the slider well down the list's right edge move this. Set so the
       current entry is in view when the list opens; meaningless while it is shut. */
    int restop;
    /* Every Advanced row's printed label width, measured at layout time the way btnw
       and okw are; a drop row's value box starts DOPT_A_DROP_GAP after its own. 0 with
       no font, the one case dopt_texset_box_rect_of falls back to DOPT_A_DROP_W. */
    int labw[DOPT_VE_COUNT];
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
    int cheats_locked;   /* a network match: every switch drawn, none clickable */
    /* WHAT KIND OF GAME THIS IS, and how far through it this player is. A match renames
       the row that ends it and offers a different confirmation; a player who has already
       resigned is a spectator, and the only thing left to do is leave. */
    int match;           /* a Skirmish or a network match, not a campaign mission */
    int surrendered;     /* this player has resigned and is watching */
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
    /* THE RESTATE BOX. `brief` is the text as handed over; `brwrap` is the same
       text with a NUL at every line break, which is exactly what Format_Window_String
       leaves behind, and `brline[]` indexes the start of each line in it. Wrapped in
       dopt_layout because the wrap needs the font. */
    char brief[DOPT_R_TEXT_MAX];
    char brwrap[DOPT_R_TEXT_MAX];
    int brline[DOPT_R_MAX_LINES];
    int brlines;
    int rvideo;              /* a briefing movie can be played: two buttons, not OK */
    int rw, rh, rx, ry;      /* the box, msgbox.cpp:150-158                     */
    int rbw;                 /* both buttons share msgbox's bwidth               */
    int rprev;               /* the pause page's selection, restored by Options  */
    /* THE NOTICE PAGE's caption. The box itself is the Restate box (brief, brwrap,
       rx..rh above), opened with no movie so it carries the lone OK. */
    char ncaption[DOPT_N_CAPTION_MAX];
    /* THE SLOT DIALOG's state, and which question the confirmation box is asking:
       0 the abort question (goptions.cpp:425), 1 "Delete this file?" (loaddlg.cpp:450).
       One box, because msgbox.cpp is one box. */
    DOPT_Slots sl;
    int cfkind;
    /* The font dopt_layout measured with, borrowed from the pack like `tracks`, so a
       box whose question changes later can be measured again without the pack. */
    const DB_Font *font;
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
void dopt_bind_visuals_reset(DOPT_State *st, void (*resetvis)(void *, DOPT_Visuals *));
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
/* A NETWORK MATCH: every switch on the page is drawn and none is clickable; only OK
   answers. The renderer refuses to open the page at all in a match, so this is what
   makes the page correct if that refusal is ever relaxed to "open it read-only". */
void dopt_set_cheats_locked(DOPT_State *st, int locked);
void dopt_set_match(DOPT_State *st, int match, int surrendered);
const char *dopt_abort_label(const DOPT_State *st);
const char *dopt_confirm_msg(const DOPT_State *st);
const char *dopt_confirm_yes(const DOPT_State *st);
const char *dopt_confirm_no(const DOPT_State *st);
/* The question's second line, or NULL when it fits on one. */
const char *dopt_confirm_msg2(const DOPT_State *st);
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

/* RESTATE. The briefing text (joined, at most DOPT_R_TEXT_MAX - 1 characters; NULL or
   empty means the mission has none) and whether a briefing movie exists to be played.
   The Restate button is drawn disabled when neither is there, because the engine's own
   answer to that case is to do nothing at all, and a live button that does nothing is
   what this dialog refuses to draw. Call before dopt_open or any time after. */
void dopt_set_briefing(DOPT_State *st, const char *text, int video);
/* The wrapped lines, for a readout: line `i` of the box, or NULL past the end. */
const char *dopt_brief_line(const DOPT_State *st, int i);

/* THE NOTICE. Open the dialog directly on a captioned box holding `text` wrapped the
   way the objective is, with a lone OK, and nothing under it. OK, Enter and Escape all
   report DOPT_ACT_RESUME, "close me". It is the Restate box lent to a caller that has
   something to say before the menu comes back, which is why it takes the pack and does
   the layout itself: there is no pause page whose open would have done so. */
void dopt_open_notice(DOPT_State *st, const DB_Pack *p, const char *caption,
                      const char *text);

/* THE SLOT DIALOG. The host binds the row supplier; the dialog opens the page itself
   from the three pause buttons. After a DOPT_ACT_DELETE the host removes the file and
   calls dopt_slots_reload, which asks for the rows again and, when none are left, puts
   the pause page back (loaddlg.cpp:456-458 `if (listbtn.Count() == 0) process = false`). */
void dopt_bind_slots(DOPT_State *st, int (*slots)(void *, DOPT_SlotRow *, int));
void dopt_slots_reload(DOPT_State *st);
/* What the description field opens with when the empty slot is chosen (the host names
   the mission and how far in it is); the player types over it. */
void dopt_slots_default(DOPT_State *st, const char *text);
/* Open the slot dialog directly on one mode, for a caller with no pause page under it
   (the main menu's Load Mission). Cancel then reports DOPT_ACT_RESUME, "close me". */
void dopt_open_slots(DOPT_State *st, const DB_Pack *p, int mode);
/* What the last DOPT_ACT_LOAD / SAVE / DELETE named: the file number, and the typed
   description (SAVE). */
int dopt_slot_pick(const DOPT_State *st);
const char *dopt_slot_descr(const DOPT_State *st);
/* The row under the highlight, for a readout: its text, or NULL. */
const char *dopt_slot_row_text(const DOPT_State *st, int row);
/* Typed characters for the description field: printable ASCII, appended under
   EditClass::Handle_Key's own rules (letters, digits and inner spaces; at most 40;
   never wider than the field). Ignored on every other page. */
void dopt_text(DOPT_State *st, const char *utf8);

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
/* The same for EVERY drop row, resolution and UI scale included, and how many entries a
   row has: the art rows have DOPT_TEX_COUNT, the UI scale DOPT_UI_COUNT, the resolution
   row as many sizes as the host enumerated. */
const char *dopt_drop_text(const DOPT_State *st, int item, int which);
int dopt_drop_count(const DOPT_State *st, int item);

/* The tooltip shown when the pointer rests on an entry that cannot be chosen, or NULL
   when that entry is selectable: Remastered without an install, DOS on a map with no
   1995 tiles, and a resolution too large for a window under WINDOWED. */
const char *dopt_texset_tooltip(const DOPT_State *st, int which);
/* The same tip, as its three separate lines. The drawing needs them apart; the script
   diagnostic wants them joined, which is what the function above is for. */
int dopt_texset_tip_lines(const DOPT_State *st, int which,
                          const char **l1, const char **l2, const char **l3);
/* A tip that only INFORMS: the Desktop entry of the resolution list names the size it
   stands for. Not a refusal, so it is kept apart from the lines above, whose presence
   is what makes an entry unpickable. Same joined form as dopt_texset_tooltip. */
const char *dopt_texset_infotip(const DOPT_State *st, int which);

/* The screen rectangle of one open drop-list entry, or 0 when the list is shut, the
   texture row is scrolled out of the well, or the entry is outside the resolution list's
   window of DOPT_RES_VIEW rows. For the headless driver: the entries are not dialog
   items, so dopt_item_rect cannot reach them. */
int dopt_texset_item_rect_pub(const DOPT_State *st, int item, int i,
                              int *x, int *y, int *w, int *h);
int dopt_texset_box_rect_pub(const DOPT_State *st, int item,
                             int *x, int *y, int *w, int *h);
/* OPEN A DROP LIST the way a click on its row does, with the resolution list scrolled so
   its current entry is in view. For the headless driver, which used to poke texdrop. */
void dopt_drop_open(DOPT_State *st, int item);
/* THE RESOLUTION LIST'S SLIDER WELL, or 0 when the list is shut or holds no more than it
   shows (then there is no well). For the driver, which has to press it. */
int dopt_res_bar_rect(const DOPT_State *st, int *x, int *y, int *w, int *h);
/* The least width a drop row's box needs: its widest entry at GRAD6FNT plus the arrow
   and the padding (DOPT_A_DROP_PAD). For the layout audit and the ADVDROP readout. */
int dopt_drop_need_pub(const DOPT_State *st, const DB_Pack *p, int item);

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
