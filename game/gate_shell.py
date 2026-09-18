#!/usr/bin/env python3
"""The bright box in a small window of a shot: the tank shell's on-screen footprint.

    python3 gate_shell.py SHOT.png CX CY [R] [THRESH]

Looks at the (2R+1)-square window centred on pixel (CX, CY), which a gate reads off the
run's own EFXDUMP|BULLETSCR line (where the bullet pass projected the middle of the shell
mesh), marks every pixel whose brightest channel is at least THRESH, and prints one line:

    W H CX CY N

the bounding box of those pixels (width, height, centre column, centre row) and how many
there were, or "0 0 0 0 0" when none. The 120MM box is untextured white (255 on its z
faces, 190 on its x faces) over grass that never passes 80 on the flight line measured,
so the default threshold of 150 separates the two with room on both sides; the window is
kept small so the tank's own hull, the muzzle flash and the buildings west of it cannot
enter the count. R defaults to 14 pixels.

What the numbers prove: with the shell laid along a due-west flight the box comes out
WIDER than tall (7 x 3 measured at 1280x720), standing on end it comes out taller than
wide (3 x 6), and its centre against the projected centre is the centring and the lift
in one number. A run that drew nothing prints zeros and fails every comparison."""
import sys
from PIL import Image

f = sys.argv[1]
cx, cy = int(float(sys.argv[2])), int(float(sys.argv[3]))
r = int(sys.argv[4]) if len(sys.argv) > 4 else 14
thresh = int(sys.argv[5]) if len(sys.argv) > 5 else 150
im = Image.open(f).convert("RGB")
px = im.load()
W, H = im.size
x0, y0 = max(0, cx - r), max(0, cy - r)
x1, y1 = min(W - 1, cx + r), min(H - 1, cy + r)
bx0 = by0 = 10 ** 9
bx1 = by1 = -1
n = 0
for y in range(y0, y1 + 1):
    for x in range(x0, x1 + 1):
        if max(px[x, y]) >= thresh:
            n += 1
            bx0 = min(bx0, x); bx1 = max(bx1, x)
            by0 = min(by0, y); by1 = max(by1, y)
if n == 0:
    print("0 0 0 0 0")
else:
    print("%d %d %.1f %.1f %d" % (bx1 - bx0 + 1, by1 - by0 + 1,
                                  (bx0 + bx1) / 2.0, (by0 + by1) / 2.0, n))
