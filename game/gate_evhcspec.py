"""G350 THE REMASTERED VEHICLES' AND BUILDINGS' SPECULAR MAPS (evhc_mod.h evhc_draw_spec,
ebld_mod.h ebld_spec_read, fx_post.h's specular target and term). Reads the three runs
gates.sh makes and prints one line, "OK <what held>" or "FAIL <what did not>".

    python3 gate_evhcspec.py <dir>

<dir>/e.log, e_on_<k>.png and e_off_<k>.png (k 1..6): Enhanced, the same frame drawn with the
specular maps and without (evhcspec 0, the run-time --noevhcspec), the Harvesters harvesting
beside the GDI base; e_panel.png the frame with the F5 switch off (gfx vspec_on 0) and
e_sw.png the same frame with the run-time switch off instead. n_* the same script with
--noevhc --noebld, cl_* in Classic.
"""
import os
import sys

import numpy as np
from PIL import Image

D = sys.argv[1]
K = range(1, 7)
LEVEL = 8                  # a pixel differs by more than this in some channel


def img(name):
    return np.asarray(Image.open(os.path.join(D, name)).convert("RGB")).astype(np.int32)


def log(name):
    p = os.path.join(D, name)
    return open(p, errors="replace").read().splitlines() if os.path.isfile(p) else []


bad, say = [], []
E = log("e.log")
if not any(l.startswith("evhc: HARV carries ") and "specular map" in l for l in E):
    bad.append("the Harvester's file carries no specular map (no 'evhc: HARV carries' line)")
spec = [l for l in E if l.startswith("EVHCSPEC|on=")]
if not spec:
    bad.append("no EVHCSPEC dump line (the renderer has no specular maps)")
else:
    f = dict(kv.split("=", 1) for kv in spec[-1].split("|")[1:])
    types = f.get("types", "-").split(",")
    if (len(types) < 21 or "HARV" not in types or int(f.get("objects", 0)) < 1
            or int(f.get("frames", 0)) < 6 or int(f.get("buildingtypes", 0)) < 10
            or int(f.get("buildings", 0)) < 1):
        bad.append("EVHCSPEC %s: want all 21 vehicle types, objects >= 1, frames >= 6, "
                   "buildingtypes >= 10, buildings >= 1" % spec[-1])
    else:
        say.append("all 21 remastered vehicles and %s building types carry maps; %s vehicles and %s "
                   "buildings were drawn into the target" % (f["buildingtypes"], f["objects"], f["buildings"]))

sets = []
H = W = 0
for k in K:
    try:
        a, b = img("e_on_%d.png" % k), img("e_off_%d.png" % k)
    except OSError:
        bad.append("e pair %d missing" % k)
        continue
    H, W = a.shape[:2]
    d = np.abs(a - b).max(-1) > LEVEL
    n = int(d.sum())
    if not 500 <= n <= 80000:
        bad.append("pair %d: %d pixels differ (want 500..80000)" % (k, n))
        continue
    # ADDED LIGHT ONLY: the term adds to the colour, so where the frames differ the frame
    # with the maps is the brighter in every channel (a level of rounding allowed)
    darker = int(((a - b).min(-1)[d] < -2).sum())
    if darker > n // 100:
        bad.append("pair %d: %d of %d differing pixels are DARKER with the maps" % (k, darker, n))
    sets.append(d)
if len(sets) == len(K):
    moved = sum(1 for i in range(1, len(sets)) if (sets[i] != sets[i - 1]).any())
    if moved < len(sets) - 1:
        bad.append("the highlight stood still between %d pairs as the Harvesters moved" % (len(sets) - 1 - moved))
    say.append("the same frame with and without the maps differs only by added light (%d to %d "
               "pixels), and the highlight moves with the Harvesters"
               % (min(int(s.sum()) for s in sets), max(int(s.sum()) for s in sets)))
try:
    if (img("e_panel.png") != img("e_sw.png")).any():
        bad.append("the F5 switch (vspec_on 0) and the run-time switch draw different frames")
    else:
        say.append("the F5 switch and --noevhcspec draw the same frame")
except OSError:
    bad.append("e_panel / e_sw missing")
quiet = True
for tag, what in (("n", "--noevhc --noebld"), ("cl", "Classic")):
    same = True
    for k in K:
        try:
            if (np.abs(img("%s_on_%d.png" % (tag, k)) - img("%s_off_%d.png" % (tag, k))).max(-1) > 0).any():
                same = False
        except OSError:
            bad.append("%s pair %d missing" % (tag, k))
            same = False
    if not same:
        bad.append("with %s the switch changed the picture" % what)
        quiet = False
if quiet:
    say.append("with --noevhc --noebld and in Classic the switch changes nothing")
print(("FAIL " + "; ".join(bad)) if bad else ("OK " + "; ".join(say)))
