#!/usr/bin/env python3
"""IS EACH MAN'S SHADOW AT HIS OWN FEET? Measured per man, not per frame.

    python3 gate_sprshadow.py ON.png OFF.png X0,Y0,X1,Y1 [more boxes...]
        -> BAND|<per-man csv>|MINBAND|<n>|BANDTOTAL|<n>|APRON|<n>

Each box is one man's silhouette, read from the CLICKOBJ line of the SAME run, so the
measurement follows him when the camera or the mission moves. Two zones per man:

  BAND   the 8 pixels immediately BELOW his silhouette, his full width. This is the
         assertion the old base push defeated: the caster's base was lifted by
         `side + 0.02`, so the blob started a sprite-width away and the pixels right
         under his boots stayed lit. Measured at the SHIPPED sun (315/52) on
         SCG01EC's four-man GDI squad: 24, 3, 6, 26 before the fix -- two of the four men
         have all but no shadow under them -- and 101, 93, 48, 66 after it.
  APRON  the band plus the 32 pixels below it and 20 either side: the body of the blob.
         At the low sun (315/14) the band barely moves (137 -> 148, because a long shadow
         reaches the feet either way) while the apron goes 348 -> 964.

Only DARKER pixels count. A shadow removes light; counting "changed" would also score a
pass that merely moved something, and the self-shadow correction brightens a few pixels of
the man himself.

WHAT THIS DELIBERATELY DOES NOT MEASURE, because it would have to be shipped red: the
LENGTH of the blob along the sun bearing. The caster's height is `hgt * g_bbUp[1]`, so at
the N64 camera's 44-degree pitch it is 0.719 of the man's real height and every shadow is
about a third short. That is a live defect at the time of writing, not a gate gap;
it is recorded as a known gap, and the length leg belongs in this file the day it is fixed.
"""
import sys
from PIL import Image

A = Image.open(sys.argv[1]).convert("RGB")
B = Image.open(sys.argv[2]).convert("RGB")
pa, pb = A.load(), B.load()
W, H = A.size
boxes = []
for s in sys.argv[3:]:
    x0, y0, x1, y1 = (int(v) for v in s.replace("..", ",").split(","))
    boxes.append((x0, y0, x1, y1))


def darker(x, y):
    if x < 0 or y < 0 or x >= W or y >= H:
        return False
    return pa[x, y] != pb[x, y] and sum(pa[x, y]) < sum(pb[x, y])


bands, apron = [], 0
for (x0, y0, x1, y1) in boxes:
    b = sum(1 for y in range(y1 + 1, y1 + 9) for x in range(x0, x1 + 1) if darker(x, y))
    a = sum(1 for y in range(y1 + 9, y1 + 41) for x in range(x0 - 20, x1 + 21)
            if darker(x, y))
    bands.append(b)
    apron += a + b
print("BAND|%s|MINBAND|%d|BANDTOTAL|%d|APRON|%d"
      % (",".join(str(b) for b in bands), min(bands) if bands else -1, sum(bands), apron))
