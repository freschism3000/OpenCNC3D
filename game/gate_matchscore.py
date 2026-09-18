#!/usr/bin/env python3
"""G214's eyes: read the match debrief's bars straight off the photograph.

Prints five numbers on one line -- the leader's bar length in plate pixels, the last
row's, whether every bar is shorter than the one above it, how many different colours the
bars are drawn in, whether the CONTINUE button is drawn, and how many rows have a bar at
all. The gate reads them with `set --`, and runs this twice: once over the finished board
and once over a shot taken part way down it, because the second is the only thing that can
tell whether the commanders arrive one at a time.

The geometry is the screen's own: MS_ROW_Y 70, MS_ROW_H 13, the bar nine rows into
each entry, MS_X0 8 and MS_BAR_W 295 (app/campaign.c).
"""
import sys
from PIL import Image
import numpy as np

im = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(int)
h, w, _ = im.shape
scale = min(w // 320, h // 200)
y0 = (h - 200 * scale) // 2
runs, cols = [], []
for i in range(8):
    py = 79 + 13 * i
    row = im[y0 + py * scale + scale // 2, 9 * scale:303 * scale, :]
    lit = row.sum(axis=1) > 60
    n = 0
    while n < len(lit) and lit[n]:
        n += 1
    runs.append(n // scale)
    if n > 4:
        cols.append(tuple(row[2]))
mono = all(runs[i] >= runs[i + 1] for i in range(7))

# THE CONTINUE BUTTON. Its top edge is one bright run across MS_BTN_X0..MS_BTN_X1
# (115..205) at MS_BTN_Y0 (180), and its label sits inside. A button nobody can see in the
# photograph is a button that can go missing without anything noticing.
top = im[y0 + 180 * scale + scale // 2, 115 * scale:206 * scale, :]
edge = int((top.sum(axis=1) > 60).sum()) // scale
label = im[y0 + 185 * scale:y0 + 190 * scale, 120 * scale:200 * scale, :]
lit = int((label.sum(axis=2) > 60).sum())
btn = 1 if edge >= 80 and lit > 40 else 0

drawn = sum(1 for n in runs if n > 0)
print(runs[0], runs[7], 1 if mono else 0, len(set(cols)), btn, drawn)
