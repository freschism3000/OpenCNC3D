# CNC3D: the DOS main menu

The 1995 MS-DOS Command & Conquer main menu, rebuilt as a standalone module and
running natively on macOS. **No N64 content is used here at all**, by a project decision:
this screen is pure MS-DOS, with one substitution, the title plate.

## Run it

```
./build.sh                 # SDL2 + OpenGL 1.1 window
./preview                  # the menu, live: arrows, return, mouse, ESC to quit
./preview -o shot.png -s 3 # write a PNG at 3x and exit, no window, no GL
```

Useful flags for shots and checks:

```
./preview --no-logo              # skip the logo animation while iterating
./preview --music ../dosdata/music/FWP.AUD   # a different theme, or --no-music
./preview --select LOAD          # move the highlight, as the arrow keys would
./preview --press EXIT           # force the pressed state
./preview --version-text "v0.2"  # the string in the bottom right of the dialog
```

## The movies

`LOGO.VQA` plays before the menu appears, exactly as `init.cpp:1499` does it, and it
ends on the title plate, so the menu simply takes over the screen. "Intro & Sneak Peek"
plays `INTRO2.VQA` (`init.cpp:1199` SEL_INTRO). Both are skippable with any key or
mouse button, which is what `Play_Movie` allows.

Sound works: 22050 Hz mono, decoded from the movie's own SND2 chunks. The decoder is
`../video/vqaplay.c`; see `../video/README.md` for what it does and does not do.

The join is a **dissolve, not a cut**: the movie's last frame fades to black over
250 ms and the menu fades up out of black over another 250 ms. 250 ms is
`FADE_PALETTE_MEDIUM` (`defines.h:2226`, `TIMER_SECOND/4`), the fade the engine itself
uses either side of a movie. DOS faded the VGA palette; multiplying the finished
pixels is the same arithmetic one step later, and on the Voodoo 2 it is a constant
colour modulate on the quad. `./preview -o strip --fade-strip` writes the real frames
out so the dissolve can be checked without a screen.

This also hides what used to be a visible pop: `LOGO.VQA` ends on the 1995 wordmark
and the menu draws the plate with "3D" on it.

**Music** plays under the menu, and ducks to silence under a movie and back up
afterwards, which is `ThemeClass::Fade_Out`. See `../audio/README.md`, including why
the track is a choice rather than a quotation.

Rebake the art pack after changing anything under `art/`:

```
python3 bake_dosmenu.py
```

## What is real

Every layout number, colour and draw order is quoted from the GPL Tiberian Dawn
sources with a file and line, and the art is decoded out of the original 1995 install.
Nothing here is eyeballed.

- **Layout** is `Main_Menu()` (`menus.cpp:455`): dialog at 85,0 152x136, buttons 125x9
  at x 98 stepping 15 from y 25, and the odd one out, Exit, 63 wide at x 128.
- **Chrome** is `Dialog_Box` -> `Draw_Box(BOXSTYLE_GREEN_BORDER)` (`dialog.cpp:64`)
  and the green button styles from `ButtonColorsClassic`. The classic table is the
  only one that can apply: `dialog.cpp:138` forces the gold style off whenever
  `Get_Resolution_Factor() == 0`, which is DOS.
- **The two filigree ornaments** in the top corners are real. `Draw_Caption` is called
  with `TXT_NONE`, and `TXT_NONE` falls into the `OPTION_DIALOG` case
  (`goptions.cpp:527`), so the caption text is skipped but `OPTIONS.SHP` frames 0 and 1
  are still drawn at the corners. Missing them would have been an easy thing to get
  wrong by reading only `menus.cpp`.
- **Button text** is `GRAD6FNT.FNT` through the gradient path in `Simple_Text_Print`.
  With `TPF_USE_GRAD_PAL` plus `TPF_MEDIUM_COLOR` / `TPF_BRIGHT_COLOR` the gradient
  collapses to one flat colour, 41 for a resting button and 4 for the selected or
  pressed one (`dialog.cpp:366-368`, `_textpalmedium` / `_textpalbright` at CC_GREEN).
- **The labels** are read out of `CONQUER.ENG` at bake time, so they are the shipped
  strings, down to "Intro & Sneak Peek".

## The one substitution: the title plate

`art/title.png` replaces `TITLE.CPS`. The baker fits it to the original 320x200 plate
and quantises it back into TITLE.CPS's own 256 colour palette, which is not cosmetic:
the green dialog and the button text address palette INDICES (140, 141, 159, 41, 4),
and those indices only mean what they should while that palette is loaded.

The bake reports what it did, and the numbers are the check:

```
title plate: art/title.png (1024x656), crop top 0 bottom 13,
align residual 3.31, quantise error 1.47, 60 rows differ from the 1995 plate
```

An align residual of 3.31 over the top 55% means the new plate really is the old plate
plus an edit, not a different picture. Quantise error 1.47 means the palette absorbed
it almost exactly. 60 changed rows is the redrawn wordmark block.

**The wordmark refit.** Making room for "3D" pushed the block up to row 127, and the
menu panel's bottom edge is row 135, so nine rows of "COMMAND" ended up behind the
panel. The panel geometry is engine layout and does not move, so the baker refits the
block instead: `WORDMARK_TOP = 138`, the row the 1995 plate used, with the block scaled
uniformly to fit between there and the copyright strip.

It cannot be done by moving a rectangle, because the top of the block overlaps the
tails of the two fireballs, which belong to the backdrop. The wordmark is neutral grey
and the fire is strongly orange, so the ink is separated by **chroma**: a pixel is
wordmark when its channels are close together and it is not black. The old position is
erased through the same mask, dilated one pixel to take the antialiased edges, or a
grey ghost of "COMMAND" survives behind the panel. Pass `--wordmark-top ROW` to move
it, or `None` in the source to leave the art exactly as delivered.

**The copyright line** is no longer part of the picture. Rows 186 and below are blanked
at bake time and the line is printed at runtime by `dm_draw_copyright`, so changing it
is a string, not an art edit.

The font is **SCOREFNT**, which is the face the 1995 line was set in. That was
established rather than guessed: rendering the original string in every font on the
discs and correlating each against the art's own pixels scores SCOREFNT at one pixel of
negative letter spacing at IoU 0.378, against 0.273 for the next best candidate, and
its width comes out at 198 pixels against the art's 193. 6 point and 3 point are both
visibly wrong, which is what was spotted in the preview.

Two things about that font are traps. It is a **shaded** face: its glyphs use nibble
classes 2, 4, 6, 8 and 14, not the 1/2/3 that 6POINT uses, so a font palette that only
fills class 1 prints nothing at all. And the 1995 line is **flat**, one colour: 355 of
its 371 ink pixels are palette index 246, (97,97,97). So every ink class resolves to
that index and there is no drop shadow.

A long credit is word wrapped rather than clipped, and the wrapped block is centred
vertically in the blanked strip, so a line that fits sits where the 1995 one sat. The
wordmark block is refitted into 138..185 to leave room for two rows if it needs them.

The engine's sixth button, "New Missions", is **deliberately not implemented**. It is
shown only when `EXPAND.DAT` is present (Covert Operations), and CNC3D has no expansion
disc to detect, so the layout is always the five button one, which is what `menus.cpp`
draws when `Expansion_Present()` is false.

## What this does not do yet

Stated plainly, because a menu that draws is not a menu that works:

- **Nothing is behind the buttons.** A click prints the label and Exit closes the
  window. There is no faction select, no mission list, no load dialog, no options.
- **No sound effects.** Music and movie audio work; nothing clicks when a button is
  pressed, and there is no EVA.
- **No attract mode.** `Main_Menu(timeout)` falls back to the intro movie when the
  player idles. There is no movie playback yet, so the timeout is not implemented.
- **Square pixels.** DOS ran 320x200 stretched to 4:3, so the original was slightly
  taller on a CRT. That correction is a display decision and is deliberately not baked
  into the surface.

## Files

```
bake_dosmenu.py   art -> dosmenu.pack (same "DOSBAR01" format as the sidebar pack)
dosmenu.h/.c      the menu: layout, hit testing, drawing. No I/O, no GL, no malloc
preview.c         SDL2 + OpenGL 1.1 window and the PNG dump mode
build.sh          builds preview.c + dosmenu.c + ../sidebar/dosbar.c
art/title.png     the replacement title plate
tools/            mixshp.py, decode_fnt.py, mixlib.py: MIX, SHP, FNT decoders
dosmenu.pack      the baked art, 374 KB
dosmenu.manifest.json   what went into the pack and how
```

`../sidebar/dosbar.c` is compiled in rather than copied. The menu and the sidebar
share one implementation of the DOS drawing primitives (the 8-bit surface, Draw_Box,
the shape blitter, the 4bpp font printer) so they cannot drift apart. The two green
box styles and the gradient font palette the sidebar never needed live in `dosmenu.c`.

Source data lives in `../dosdata` (`LOCAL.MIX`, `cd1/CONQUER.MIX`), copied from an
original 1995 install, so the pipeline does not depend on a mounted disc image.
