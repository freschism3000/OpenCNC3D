# data/dosdata — the 1995 MS-DOS Command & Conquer data

**Empty on purpose.** This is where the working set from your own 1995 MS-DOS release of
Command & Conquer goes. The repository ships none of it.

The N64 cartridge gives this project its 3D. This directory gives it everything 2D and
everything audible: the sidebar, cursors, infantry sprites, fonts, the palette, the music,
the speech and the cinematics.

## What goes here

```
data/dosdata/
  CONQUER.MIX     sidebar art, cursors, infantry sprites, cameos, buildup animations
  LOCAL.MIX       fonts and the authentic palette
  GENERAL.MIX     shared game data
  SOUNDS.MIX      sound effects
  SPEECH.MIX      the EVA lines
  SCORES.MIX      the music score
  AUD.MIX         further audio
  ZOUNDS.MIX      further audio
  movies/         the campaign cinematics, .VQA, 75 files
  music/          extracted score tracks
  transit/        TRANSIT.MIX
```

The `.MIX` files sit at the top of your CD or install directory and can be copied straight
across. `movies/` is the one that needs assembling.

## Getting the movies

The campaign names 72 distinct `.VQA` files and they are split across the two discs:
**the GDI and Nod `MOVIES.MIX` files are different and you need both.**

`tools/extract_movies.py` reconstructs the set from either disc's `MOVIES.MIX`.

The discs are the freeware **C&C95 Gold** release, which Electronic Arts published at no
charge: archive.org item `gdi-95`, files `GDI95.ISO` and `NOD95.ISO`. You do not have to
download either whole. `MOVIES.MIX` is stored contiguously, so reading the ISO9660 volume
descriptor at byte 32768 plus the root directory (about 4 KB) gives its exact extent, and
an HTTP range request fetches only that.

Eight tracks in `SCORES.MIX` are 1996 Covert Operations content and are on that disc
rather than the base game. Without it those eight are simply absent; nothing else changes.

## Where the project reads it from

`data/dosdata/` relative to the repository root, or set `CNC3D_ROOT` if you are running a
tool from outside the tree. A handful of sounds and two EVA lines are not on the 1995
discs at all; they stay silent and are listed rather than substituted.

## Why it is not in the repository

It is Electronic Arts' and Westwood's work, and the movie set alone is over 400 MB.
Use your own copy.
