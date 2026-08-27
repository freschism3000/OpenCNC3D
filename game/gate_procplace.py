#!/usr/bin/env python3
"""Is the picture STILL across a run of frames, inside one crop?

    python3 gate_procplace.py DIR PREFIX X0,Y0,X1,Y1 FIRST LAST
        -> PAIRS|<adjacent pairs that differ>|CHANGED|<total differing pixels>

Used by G31e for the refinery placement rig. The state leg reads procrig_for's own
answer out of the ANIM line; this leg is the same claim made on the SCREEN, because the
dump could agree while the draw did something else. Both are needed and neither replaces
the other.

FIRST..LAST are the frame numbers gates.sh worked out from the ANIM lines of the SAME
run: the samples with bstate==0 (BSTATE_CONSTRUCTION) and stage>=12, i.e. exactly the
window where the arm's formula would put the rig on screen. The engine's buildup
animation has already reported frac=1.00 well before that window opens (measured: frac
hits 1.00 at buildup stage 10 of 20, the window opens at frame 35), so a still refinery
is what those frames should show, and every changed pixel in the crop is the rig.

Measured over frames 35..55 on SCG01EC: 7 pairs moved and 2146 pixels changed
on the shipped v0.5.7 binary; 0 and 0 after the fix, byte-identical.
"""
import sys, os
from PIL import Image

d, pre, boxs = sys.argv[1], sys.argv[2], sys.argv[3]
first, last = int(sys.argv[4]), int(sys.argv[5])
x0, y0, x1, y1 = (int(v) for v in boxs.replace("..", ",").split(","))
box = (x0, y0, x1 + 1, y1 + 1)

ims = []
for i in range(first, last + 1):
    p = os.path.join(d, "%s_%02d.png" % (pre, i))
    if not os.path.exists(p):
        print("PAIRS|-1|CHANGED|-1")       # a missing frame is a failure, not zero motion
        sys.exit(0)
    ims.append(Image.open(p).convert("RGB").crop(box).load())

w, h = (x1 + 1 - x0, y1 + 1 - y0)
pairs = changed = 0
for i in range(len(ims) - 1):
    n = sum(1 for y in range(h) for x in range(w) if ims[i][x, y] != ims[i + 1][x, y])
    changed += n
    if n:
        pairs += 1
print("PAIRS|%d|CHANGED|%d" % (pairs, changed))
