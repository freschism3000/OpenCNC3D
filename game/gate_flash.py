#!/usr/bin/env python3
"""THE TARGET BLUSH, measured inside the blushing object's OWN silhouette.

    python3 gate_flash.py DIR PREFIX_ON PREFIX_OFF X0,Y0,X1,Y1
        -> LIT|<n>|TICKS|<csv>|MIN|<smallest lit count>|OFFMAX|<largest unlit count>

The two series are the same run twice, once with the blush and once with --noflash, so
frame N of one and frame N of the other differ ONLY by the blush: same seed, same tick,
same camera, and the renderer is deterministic (G4 is the standing proof of that). Every
counted pixel is therefore the flash itself, which is why the unlit frames read exactly
zero rather than "under a threshold".

TWO THINGS THIS FIXES ABOUT THE VERSION IT REPLACES, and both are why G51 could not fail:

  1  IT COUNTS INSIDE THE OBJECT. The old helper sampled every second pixel of the WHOLE
     800x500 frame and called a frame lit at 500 differences, so it could not tell a
     Refinery whitening from a harvester driving past. The box comes from the CLICKOBJ
     line of the same run (gates.sh parses `silhouette=X0,Y0..X1,Y1` out of it), so it
     cannot go stale when a camera moves.
  2  IT REPORTS THE MAGNITUDE. LIT alone is a threshold answer; MIN is the smallest count
     among the lit frames and OFFMAX the largest among the unlit ones, so gates.sh can
     assert the separation instead of trusting one cutoff. Measured on the
     fixed tree: Power Plant 6386 lit / 0 unlit, Refinery 12534 / 0, Helipad 6854 / 0.
     On the shipped v0.5.7 binary the Refinery and the Helipad read 0 on ALL TWELVE.

The 2000 cutoff below is not a taste value: it is under a third of the smallest blush any
of the three arms produces, and every unlit frame measured is 0, so nothing sits near it.
"""
import sys, os
from PIL import Image

d, pa, pb, boxs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
x0, y0, x1, y1 = (int(v) for v in boxs.replace("..", ",").split(","))
box = (x0, y0, x1 + 1, y1 + 1)

lit, mn, offmax = [], None, 0
for i in range(1, 13):
    fa = os.path.join(d, "%s_%02d.png" % (pa, i))
    fb = os.path.join(d, "%s_%02d.png" % (pb, i))
    if not (os.path.exists(fa) and os.path.exists(fb)):
        continue
    A = Image.open(fa).convert("RGB").crop(box)
    B = Image.open(fb).convert("RGB").crop(box)
    a, b = A.load(), B.load()
    w, h = A.size
    n = sum(1 for y in range(h) for x in range(w) if a[x, y] != b[x, y])
    if n >= 2000:
        lit.append(i)
        mn = n if mn is None else min(mn, n)
    else:
        offmax = max(offmax, n)
print("LIT|%d|TICKS|%s|MIN|%d|OFFMAX|%d"
      % (len(lit), ",".join(str(i) for i in lit), mn if mn is not None else 0, offmax))
