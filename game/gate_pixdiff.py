#!/usr/bin/env python3
"""Sampled pixel difference between two shots. Prints one integer and nothing else, so a
gate can use it inline. Samples every second pixel: the answer is a threshold, not a
census, and halving the work halves the gate's wall clock.

    python3 gate_pixdiff.py A.png B.png [X0,Y0,X1,Y1]

The optional fourth argument restricts the count to one rectangle, and callers are
expected to read it out of the run they are measuring (a CLICKOBJ silhouette) rather than
type it in. Whole-frame counting is what let G56 announce "the helipad's texture book"
while measuring a map with no helipad on it: any two pixels moving anywhere satisfied it.
The sampling stride applies inside the box as well, so a box's answer is about a quarter
of its true pixel count -- thresholds are calibrated against measured numbers, never
against an area."""
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
pa, pb = a.load(), b.load()
W, H = a.size
x0, y0, x1, y1 = 0, 0, W - 1, H - 1
if len(sys.argv) > 3:
    x0, y0, x1, y1 = (int(v) for v in sys.argv[3].replace("..", ",").split(","))
    x0 = max(0, x0); y0 = max(0, y0); x1 = min(W - 1, x1); y1 = min(H - 1, y1)
print(sum(1 for y in range(y0, y1 + 1, 2) for x in range(x0, x1 + 1, 2)
          if pa[x, y] != pb[x, y]))
