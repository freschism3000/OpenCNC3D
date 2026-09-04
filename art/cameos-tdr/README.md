# The 128x96 cameo art

54 PNGs, one per Tiberian Dawn buildable, all exactly **128x96**. They are **generated**:
an image model was given the 1995 PDF manual renders and produced these from them.
`provenance/RECIPE.md` names the model and the call. They are not extracted from any
shipped game data, and no command in this repo regenerates them, so they are committed
here rather than left on one machine.

This paragraph used to say "authored source, not extracted or generated", which
contradicted `provenance/RECIPE.md` on the same shelf. Generated is the correct
description of the two.

## Naming, and why there is only one convention

    TD_<Name>_128x96.png

Every file follows it, including the five older survivors described below, so the baker
resolves a filename to a buildable key with one code path and no aliases. `<Name>` is the
unit name used by `provenance/manifest.json`, which is why the directory listing and that
manifest line up file for file.

## Where they came from

**49 of the 54** are remakes traced from the **1995 PDF manual renders** (the greyscale
renders published in the C&C 10th anniversary gallery), quantised to stay inside the
original cameo colour palette. `provenance/RECIPE.md` is the method, down to the fetch
pattern for the source scans and the render-code to cameo mapping;
`provenance/STATUS.md` lists each finished cameo with the render code it came from and
its colour count; `provenance/manifest.json` is the same thing machine readable.

**5 of the 54** are from the earlier art set, delivered as
`provenance/tdr_pixel_art_128x96_final_20260818.zip` and kept beside them unmodified:

    TD_AirStrike_128x96.png       BOMB    Air Strike
    TD_BarbedWire_128x96.png      BARB    Barbed Wire
    TD_IonCannon_128x96.png       ION     Ion Cannon
    TD_NuclearStrike_128x96.png   ATOM    Nuclear Strike
    TD_WoodFence_128x96.png       WOOD    Wood Fence

**Why exactly those five.** The 1995 manual has no render for any of them, so the remake
had nothing to trace and its own status file records all five as blocked. Keeping the
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
`h6_clock_fade` in `game/cnc_sidebar.h`. The HUD is an ENHANCED sub-option and the DOS bar
is unaffected, but Enhanced is what the game BOOTS INTO: `fx_defaults` in `game/fx_state.h`
sets `new_hud = 1` and `app/cnc3d.cpp` calls `game_visuals_default_enhanced` unless
`--classic` is passed. So this is the default sidebar art, not an opt-in.

The baker refuses rather than writing a short pack: a PNG that ignores the naming
convention, a name it cannot map, two files claiming one key, and a mapped key with no
art at all each stop it with the name printed. That matters because a missing cameo does
not crash anything, it just quietly falls back to the DOS art for one cell.

## The backdrop is desert, and that is an open fault

All but a handful put the subject on sandy ground under a pale sky, and the 640x480 HUD is
the default sidebar (above), so on a temperate map the whole build column reads as the wrong
theater. Counted rather than eyeballed: seven have no ground at all (A10, Apache, AirStrike,
IonCannon, NuclearStrike, Gunboat and Transport are sky, orbit and sea) and the Orca is over
rock and greenery; **the other 46 stand on desert**. 46 of the 91 scenario packs in
`playable/` are TEMPERAT against 42 DESERT, so temperate is the majority case.

It is a drift from the source art rather than a chosen look. Over the bottom quarter of the
frame, mean red minus blue is **+35.8** across these 54 and **+18.2** across the same 54 in
the 1995 64x48 cameos they were traced from; 44 of the 54 got warmer. `provenance/RECIPE.md`
rule 9 already asks for the opposite: colour direction comes from the cameo, not from the
greyscale render, and those cameos sit on grass, grey rock and snowy mountains.

**Only new art fixes it.** The pack, the baker and the loader hold one image per buildable
and have no theater in them anywhere, so they cannot carry a second backdrop even if one
were drawn.

### What a replacement has to be

Exactly what is here already, so that no code moves:

- **128x96 PNG**, one per buildable. The baker rejects any other size by name.
- Named **`TD_<Name>_128x96.png`**, with `<Name>` spelled exactly as the file it replaces.
  A new spelling has to be added to `MAP` in `tools/sidebar_redesign/cameos_tdr.py` or the
  bake stops rather than shipping a short pack.
- Dropped into **`art/cameos-tdr/`**, over the old file.
- Still inside the original cameo palette and still matching the original's subject scale
  (`provenance/RECIPE.md` rules 5 and 14).

Then `python3 tools/sidebar_redesign/cameos_tdr.py` rewrites `game/cameos_tdr.pack` (BOX 2:1
to 64x48 RGBA) and `game/make-build.sh` copies any newer `game/*.pack` into the play folder.
The five with no manual render need a ground like the rest of the set, except `AirStrike`
and `NuclearStrike`, which have none to change; `TD_SSMLauncher_128x96.png` is not baked and
need not be redrawn.

### Which backdrop is not decided

The 1995 set these were traced from is one set for all four theaters, and its ground reads
neutral to temperate, so matching it is 54 images. 1995 could also key a cameo to the
theater, but only in its hi-res path: `BuildingTypeClass::Init(TheaterType)` and
`AircraftTypeClass::Init(TheaterType)` re-fetch `<ident>ICNH` with `Theaters[theater].Suffix`
and override the cameo when that file exists. Whether the retail hi-res archives really
carry different art per theater cannot be checked from this tree: our only theater archives
are synthetic and the 1995 DOS `CONQUER.MIX` is lo-res with no `ICNH` in it. That route
multiplies the job by the number of theaters and needs a format change as well, so it is a
project decision and not one the baker should take on its own.

## A note on the carried-over documents

`provenance/RECIPE.md` and `provenance/STATUS.md` are the delivered documents with two
mechanical edits and nothing else: em-dashes replaced with `--` (rule 5), a personal name
removed (rule 10), and the paths in their headers pointed at where the files actually
live in this repo. `provenance/manifest.json` is byte for byte as delivered, so it still
describes the five blocked units as having no file; this README is the authority on what
is actually on disk for them.
