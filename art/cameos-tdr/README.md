# The 128x96 cameo art

54 PNGs, one per Tiberian Dawn buildable, all exactly **128x96**. They are **authored
source, not extracted or generated**, so by the rule in the repository conventions they live in the repo
rather than on one machine's desktop.

## Naming, and why there is only one convention

    TD_<Name>_128x96.png

Every file follows it, including the five older survivors described below, so the baker
resolves a filename to a buildable key with one code path and no aliases. `<Name>` is the
unit name from the source set, so the directory listing and the baker's map line up
file for file.

## Where they came from

**49 of the 54** are remakes traced from the **1995 PDF manual renders** (the greyscale
renders published in the C&C 10th anniversary gallery), quantised to stay inside the
original cameo colour palette.

**5 of the 54** are from the earlier art set, kept beside them unmodified:

    TD_AirStrike_128x96.png       BOMB    Air Strike
    TD_BarbedWire_128x96.png      BARB    Barbed Wire
    TD_IonCannon_128x96.png       ION     Ion Cannon
    TD_NuclearStrike_128x96.png   ATOM    Nuclear Strike
    TD_WoodFence_128x96.png       WOOD    Wood Fence

**Why exactly those five.** The 1995 manual has no render for any of them, so the remake
had nothing to trace, and all five are recorded as blocked. Keeping the
earlier art is a deliberate choice, not a leftover: drop it and the 640x480 HUD falls
back to the 8-bit DOS cameo for those five cells only, so the new sidebar would be true
colour everywhere except five conspicuous holes. `RETAINED_OLD_ART` in
`tools/sidebar_redesign/cameos_tdr.py` names the same five, so the decision is visible
from the code as well as from here. If a render for one of them ever turns up, replace
the file and shorten that set.

Two units changed name between the two sets, and the new names are the ones on disk:

    Light_Scout      -> Humvee    (key JEEP)
    Rocket_Launcher  -> MLRS      (key MSAM)

## The one that is drawn but not shipped

`TD_SSMLauncher_128x96.png` has no buildable in Tiberian Dawn's sidebar, so it is kept in
the repo and left out of the pack. `UNUSED` in the baker names it, so that reads as a
decision rather than an oversight.

## What is done with them

`tools/sidebar_redesign/cameos_tdr.py` bakes 53 of the 54 into `game/cameos_tdr.pack`:
64x48 RGBA, downscaled exactly 2:1 by a BOX filter so every output pixel is the mean of
one 2x2 block. The 640x480 HUD draws them in **true colour**, no palette step; the build
clock that used to force an 8-bit path is decoded rather than approximated, in
`h6_clock_fade` in `game/cnc_sidebar.h`. The HUD is an ENHANCED sub-option, so this art
is what Enhanced mode shows and the DOS bar is unaffected.

The baker refuses rather than writing a short pack: a PNG that ignores the naming
convention, a name it cannot map, two files claiming one key, and a mapped key with no
art at all each stop it with the name printed. That matters because a missing cameo does
not crash anything, it just quietly falls back to the DOS art for one cell.
