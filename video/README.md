# CNC3D: the movies

A player for Westwood VQA version 2, the format every Command & Conquer movie is in.
This is what makes the intro sequence and the main menu logo animation possible, and
the same decoder will play the mission briefings when there is somewhere to put them.

## Run it

```
./build.sh
./playvqa ../dosdata/movies/LOGO.VQA           # any key or click skips, like DOS
./playvqa ../dosdata/movies/INTRO2.VQA
./playvqa ../dosdata/movies/LOGO.VQA --scan    # decode every frame, no window
./playvqa ../dosdata/movies/LOGO.VQA -o shot -f 0,120,300 -s 3   # dump PNGs
```

The menu uses it directly: `menu/preview` plays `LOGO.VQA` before the menu appears and
`INTRO2.VQA` when you pick "Intro & Sneak Peek".

## What is implemented

`vqaplay.c` is a port of the GPL engine's own player, not a guess at the format:

| piece | source |
|---|---|
| `VQAHeader`, 42 bytes, little endian inside a big endian IFF | `common/vqafile.h` |
| chunk walk, and the partial codebook rule | `common/vqaloader.cpp` |
| `UnVQ_4x2` block decode | `common/unvqbuff.cpp` |
| palette handling | `common/vqapalette.cpp` |
| SND2 audio | `common/soscodec.cpp` |
| LCW ("format80") for `VPTZ`, `CBFZ`, `CPLZ` | `common/lcw.cpp` |

Three details that would be easy to get wrong and are worth stating:

- **A block whose second pointer plane byte is 15 is not a codebook reference.** It is a
  solid fill in colour `pointer1` (`unvqbuff.cpp:42`). Treating it as an index gives a
  frame that is mostly right and speckled with garbage.
- **The codebook arrives in pieces.** `CBP0` chunks accumulate, and only after
  `Header.Groupsize` of them (8 here) does the accumulation become the codebook, taking
  effect from the following frame. Swap it a frame early and motion smears.
- **The palette is not what it looks like.** The bytes are 6-bit values with rubbish in
  the top two bits: `VQA_SetPalette` masks with `0x3F`, then lifts luminance by 15%
  capped at 63 (`Increase_Palette_Luminance`). Skip the mask and the picture is a mess
  of wrong colours; skip the lift and it is noticeably dark.

It **streams**. Only the current frame is held, which matters because `INTRO2.VQA` is
21 MB and the Windows 98 tier cannot hold it in memory.

Verified by decoding both movies end to end: `LOGO.VQA` 418 frames and 1,250,968 bytes
of PCM (27.9 s at 22050 Hz, which is exactly 418 frames at 15 fps), `INTRO2.VQA` 1,934
frames and 5,708,008 bytes. Frames were rendered and looked at, not just counted.

## What it does not do

- **VQA version 1 and 3 are refused**, and any block size other than 4x2. Tiberian Dawn
  only ships version 2 at 4x2, so this covers every movie on the discs.
- **No caption track** (`vqacaption.cpp`) and no alternative audio stream.
- **Audio is queued, not clocked.** Frames are paced off `SDL_GetTicks` at the header
  fps and audio is queued as it arrives. That is stable for a 30 second logo, and the
  DOS player did the opposite: it clocked video off the audio. If long movies drift,
  that is the thing to change.
- **No mission briefings yet**, because there is no place in the game flow that asks
  for one. The decoder plays them today: `./playvqa GDI1.VQA` works if you extract it.

## Where the movies live

`../dosdata/movies/` holds `LOGO.VQA` and `INTRO2.VQA`, extracted from `MOVIES.MIX` on
CD-1 of the original 1995 disc set. The full `MOVIES.MIX` is 100+ MB and is deliberately
not copied in whole; extract more with `menu/tools/mixshp.py` when they are needed.

## Files

```
vqaplay.h/.c   the decoder: libc only, no SDL, no GL, no threads
playvqa.c      the standalone player: window, clock, abort key
pngwrite.h/.c  minimal PNG writer for the frame dumps
vqaprobe.py    the Python prototype the C was written against, kept as a second
               opinion: python3 vqaprobe.py ../dosdata/movies/LOGO.VQA 0,120,300
build.sh       builds playvqa; `./build.sh nosdl` for the decode-only build
```
