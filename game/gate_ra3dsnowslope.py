#!/usr/bin/env python3
"""
G351: Red Alert's snow hillsides under Enhanced keep their painting at the shipped sun:
a slope turned towards the sun is lit brighter than level snow, and it is not clipped
to flat white.

    python3 gate_ra3dsnowslope.py DIR

DIR holds, for Soviet 1 (u) and Allied 3 (a), both on Red Alert's snow with their auto
heightmaps, one camera over a sun-facing hillside, three frames of the same tick:

    <m>_on.png       the shipped dials
    <m>_nobloom.png  the same with the bloom off
    <m>_off.png      the same with the highlight rolloff off (tonemap 0), which is the
                     switch the slope's rolloff answers to (fx_post.h, uSlopeWhite)

and the runs' logs <m>.log.

Pure white is a pixel whose three channels are all 250 or more, counted over the
tactical view (the sidebar and the top strip left out).

  * the logs say each run drew Red Alert's own ground on the SNOW theater with the
    scene's heights, so the frames are of the hillsides this is about;
  * with the shipped dials no more than 5% of the view is pure white. Before the fix
    these two views were 22% and 30% white: the ground was lit twice on a slope, once by
    the console's baked shade and once by the post chain's sun, and the sun's 1.35 times
    on level snow already near the grade's white went straight through it;
  * with the bloom off no more than 1%: what white is left with it on is the bloom
    spreading off the brightest level snow, not the slopes;
  * the control: with the rolloff off the same view is more than 2% white and more
    than twice the shipped frame, so the views really hold a sun-facing pale slope that
    would clip, and the measure can see it.

Prints one line per check and a verdict line "G351 1 ..." or "G351 0 ..."; exits 0 only
if every check passed.
"""

import os
import sys

import numpy as np
from PIL import Image

MISSIONS = (("u", "Soviet 1"), ("a", "Allied 3"))
ON_MAX = 5.0
NOBLOOM_MAX = 1.0
OFF_MIN = 2.0


def white_pct(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(int)
    view = a[40:, :1000]
    return 100.0 * float((view.min(axis=2) >= 250).mean())


def main():
    d = sys.argv[1]
    fails = []
    notes = []
    for m, name in MISSIONS:
        log = os.path.join(d, m + ".log")
        try:
            text = open(log, "rb").read().decode("latin-1")
        except OSError:
            fails.append("%s: no run log" % name)
            continue
        if ("RA3D|scene|ground=red alert|theater=SNOW|" not in text
                or "|heights=scene" not in text):
            fails.append("%s did not draw Red Alert's snow ground on its scene's heights" % name)
            continue
        try:
            on = white_pct(os.path.join(d, m + "_on.png"))
            nb = white_pct(os.path.join(d, m + "_nobloom.png"))
            off = white_pct(os.path.join(d, m + "_off.png"))
        except OSError as e:
            fails.append("%s: a frame is missing (%s)" % (name, e))
            continue
        print("G351 %s: pure white %.2f%% shipped, %.2f%% without bloom, %.2f%% without the rolloff"
              % (name, on, nb, off))
        if on > ON_MAX:
            fails.append("%s: %.2f%% of the view is clipped to white at the shipped sun (at most %.0f%%)"
                         % (name, on, ON_MAX))
        if nb > NOBLOOM_MAX:
            fails.append("%s: %.2f%% clipped with the bloom off (at most %.0f%%)" % (name, nb, NOBLOOM_MAX))
        if off <= OFF_MIN or off <= 2.0 * on:
            fails.append("%s: the control does not clip (%.2f%% with the rolloff off against %.2f%% on), "
                         "so this view proves nothing" % (name, off, on))
        notes.append("%s %.1f%% (%.1f%% without the rolloff)" % (name, on, off))
    if fails:
        print("G351 0 " + "; ".join(fails))
        return 1
    print("G351 1 Red Alert's snow slopes keep their painting at the shipped sun: pure white "
          + ", ".join(notes) + "; with the bloom off at most 1%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
