#!/usr/bin/env python3
"""Does the flamethrower's tongue reach the SCREEN, at the man holding it?

    python3 gate_flamepix.py ON.png OFF.png X0,Y0,X1,Y1 [more boxes...]
        -> MAX|<n>|SUM|<n>|WHOLE|<n>

Each box is one flame trooper's silhouette from the CLICKOBJ line of the same run; the
count is taken in that box grown by 40 pixels, because the flame is thrown ahead of him
and the shadow of it falls below him. MAX is the busiest trooper's box, SUM all of them,
WHOLE the changed pixels in the entire frame -- so a caller can assert that what changed
is at the men and not somewhere else on a map that has 300 other particles alive.

Measured on SCG36EA at tick 449, 800x600, against an --efxscale 0 arm of the
same frame: the firing trooper's box reads 237 on the shipped v0.5.7 binary (one particle)
and 616 after the fix (ten), with 637 changed in the whole frame either side of which 616
are inside his box.

The second trooper's flame is not visible from this camera at this tick and his box reads
0 in both arms. That is why the assertion is on MAX rather than on every box: one trooper
whose fire is on the glass is the claim, and SUM/WHOLE together are the locality check.
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
    boxes.append((max(0, x0 - 40), max(0, y0 - 40), min(W - 1, x1 + 40), min(H - 1, y1 + 40)))

whole = 0
counts = [0] * len(boxes)
for y in range(H):
    for x in range(W):
        if pa[x, y] != pb[x, y]:
            whole += 1
            for i, (bx0, by0, bx1, by1) in enumerate(boxes):
                if bx0 <= x <= bx1 and by0 <= y <= by1:
                    counts[i] += 1
print("MAX|%d|SUM|%d|WHOLE|%d" % (max(counts) if counts else 0, sum(counts), whole))
