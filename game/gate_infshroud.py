"""G326: Enhanced soldiers in the shroud's soft edge are no brighter than the ground under them.

usage: gate_infshroud.py SHOTS_DIR LOG

gate_infshroud.txt shoots one frame of SCG01EA three times: as drawn (g326_shroud), with
the shroud lifted (g326_lit) and with the shroud lifted and no soldiers (g326_bare). The
last two differ only where a soldier is, which is each soldier's mask; `infshroud` lists
every drawn soldier with the env term the soldier shader was handed and the centre of his
silhouette on screen.

WHY THE GROUND IS THE MEASURE. The soldier shader multiplied the shroud into its light
BEFORE its gamma encode, so the byte came out as (1 - e)^(1/2.2) where everything else
under the shroud takes (1 - e) in display values. At the 0.784 ceiling a soldier stood at
twice the light he should, and through the post chain that is the difference between
black and a grey figure on black ground: soldiers near the reveal edge read as standing
on top of the shroud. The post chain is not linear in the dark, so the frame cannot be
held to 1 - e itself; it is held to the thing the eye compares him with, the ground round
his feet.

Claims:
  (1) THE SCENE: at least one soldier on screen in the feather (env at least 0.5) and one
      in the open (env 0), each with at least 100 mask pixels.
  (2) IN THE FEATHER: every such soldier's mean luminance as drawn is at most the mean of
      the ground in a ring 6 to 20 px round him, plus 2 levels.
  (3) IN THE OPEN nothing changes: a soldier with env 0 has the same mean luminance as
      drawn and with the shroud lifted, to within 1%.
"""
import os
import re
import sys

import numpy as np
from PIL import Image

d, log = sys.argv[1], sys.argv[2]


def img(name):
    return np.asarray(Image.open(os.path.join(d, name)).convert("RGB")).astype(np.float64)


def lum(a):
    return a @ np.array([0.2126, 0.7152, 0.0722])


def grow(m, n):
    """n steps of 8-neighbour dilation, numpy only."""
    m = m.copy()
    for _ in range(n):
        g = m.copy()
        g[1:, :] |= m[:-1, :]
        g[:-1, :] |= m[1:, :]
        g[:, 1:] |= m[:, :-1]
        g[:, :-1] |= m[:, 1:]
        g[1:, 1:] |= m[:-1, :-1]
        g[:-1, :-1] |= m[1:, 1:]
        g[1:, :-1] |= m[:-1, 1:]
        g[:-1, 1:] |= m[1:, :-1]
        m = g
    return m


S, L, B = lum(img("g326_shroud.png")), lum(img("g326_lit.png")), img("g326_bare.png")
soldiers_all = np.abs(img("g326_lit.png") - B).sum(2) > 24
H, W = S.shape
bad, notes = [], []
feather = openn = 0
for line in open(log, errors="replace"):
    m = re.match(r"INFSHROUD\|(\w+)\|(\d+)\|.*\|env=([\d.]+)\|screen=([-\d.]+),([-\d.]+)", line)
    if not m:
        continue
    name = "%s#%s" % (m.group(1), m.group(2))
    env, c, r = float(m.group(3)), float(m.group(4)), float(m.group(5))
    if not (20 <= c < W - 20 and 20 <= r < H - 20):
        continue
    box = np.zeros_like(soldiers_all)
    box[max(0, int(r) - 40):int(r) + 30, max(0, int(c) - 25):int(c) + 25] = True
    mk = box & soldiers_all
    if mk.sum() < 100:
        continue
    drawn, lit = S[mk].mean(), L[mk].mean()
    if env >= 0.5:
        ring = grow(mk, 20) & ~grow(soldiers_all, 6)
        ground = S[ring].mean()
        feather += 1
        notes.append("%s env %.3f at %.1f on ground at %.1f" % (name, env, drawn, ground))
        if drawn > ground + 2.0:
            bad.append("%s in the shroud (env %.3f) is drawn at luminance %.1f on ground at %.1f"
                       % (name, env, drawn, ground))
    elif env == 0.0:
        openn += 1
        if abs(drawn - lit) > 0.01 * lit:
            bad.append("%s in the open is at %.1f as drawn and %.1f with the shroud lifted"
                       % (name, drawn, lit))
if feather == 0:
    bad.append("no soldier on screen in the shroud's feather: the scene no longer tests anything")
if openn == 0:
    bad.append("no soldier on screen in the open: the control arm is missing")
if bad:
    print("0 " + "; ".join(bad))
else:
    print("1 %d soldier(s) in the feather as dark as the ground (%s); %d in the open unchanged"
          % (feather, ", ".join(notes), openn))
