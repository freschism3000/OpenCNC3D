# Reading the Remastered Collection's terrain art

Reference implementations, written against and verified on a real install (3 Sep 2026).
They exist so the C port has something known-correct to be checked against, and so the
format work does not have to be redone from documentation.

**None of EA's art is in this repository and none ever will be.** These read the player's
own installation. `game/remaster.h` is what finds it.

- `meg.py`   -- MEG V3 container. Validated on `CONFIG.MEG`: the header arithmetic
                `24 + namesSize + 20*N == dataStart` closes to the byte.
- `dds.py`   -- DXT1/DXT5 to RGBA. The terrain tiles are DXT1, 128x128, 8 mips.
- `rmatlas.py` -- the tileset XML to a (template, icon) -> archive path map, and an atlas
                built in the pack's own slot order.

Coverage measured with these: TEMPERAT 758/758, DESERT 973/973, WINTER 838/840.

Quick check against an install:

```
CNC3D_REMASTER_DIR=... python3 -c "import rmatlas; print(rmatlas.build('TEMPERAT')[3])"
```

THE ONE TRAP, and it is the expensive one: **never construct a terrain filename.** Read it
from the XML. `SH1` and friends carry a second suffix that no constructed name reproduces,
and the obvious two-digit rule is wrong for about 88% of the files that have one.
