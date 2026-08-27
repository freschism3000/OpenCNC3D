#!/usr/bin/env python3
"""How much of a building's apron is BURIED under the terrain it stands on.

The test map stands the GDI base on the beach ramp -- corner heights 125/113/101/90
across the Construction Yard's three cells -- and a building is a FLAT model at ONE
ground height, so without a levelled pad its uphill quarter sinks under the ground.
What shows there instead is grass, so the measurement is grass: count green pixels
inside a window that the apron covers when the building stands level. High means the
apron is gone.

    python3 gate_pads.py shots/pad_on.png   ->   GREEN|<count>|BOX|<n>
"""
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
W, H = im.size
px = im.load()
# Fractions of the frame, not pixels, so the window follows the shot's resolution.
# Chosen as the densest patch of apron the pad restores (measured, 19 Aug).
x0, x1 = int(W * 0.5227), int(W * 0.5695)
y0, y1 = int(H * 0.4713), int(H * 0.5212)
green = sum(1 for y in range(y0, y1) for x in range(x0, x1)
            if px[x, y][1] > px[x, y][0] + 12 and px[x, y][1] > px[x, y][2] + 12)
print("GREEN|%d|BOX|%d" % (green, (x1 - x0) * (y1 - y0)))
