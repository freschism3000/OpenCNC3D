#!/usr/bin/env python3
"""How many pixels of the Construction Yard change between two frames.

The window is the yard itself, framed by the gate's own `cam 53 51` + `zoom max`, so it
follows the shot's resolution rather than hard pixels. Two calls: one pair while the yard
is idle (must be near zero apart from the fans) and one pair while it is building (must be
clearly moving).

    python3 gate_crane.py A.png B.png  ->  CHANGED|<n>|BOX|<n>
"""
import sys
from PIL import Image

a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
W, H = a.size
pa, pb = a.load(), b.load()
# The crane arm only: right of the fan deck, above the apron. Fractions of the frame.
x0, x1 = int(W * 0.50), int(W * 0.68)
y0, y1 = int(H * 0.47), int(H * 0.63)
n = sum(1 for y in range(y0, y1) for x in range(x0, x1) if pa[x, y] != pb[x, y])
print("CHANGED|%d|BOX|%d" % (n, (x1 - x0) * (y1 - y0)))
