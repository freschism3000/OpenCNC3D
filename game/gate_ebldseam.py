#!/usr/bin/env python3
"""The damage overlay's gate: the scene it stages and the claims it checks (ebld_mod.h,
ebld_overlay_begin: the damaged look creeping into a struck side is blended by its zone
weight, not cut by an alpha test).

    python3 gate_ebldseam.py maps DIR            write the test mission and its script into DIR
    python3 gate_ebldseam.py check LOG SHOTS REF read the run's log, the shots and the committed
                                                 reference; print "1 message" or "0 why"
    python3 gate_ebldseam.py ref SHOTS OUT.png    crop the run's damaged shot into a reference
                                                 (gates/g360-damaged-ref.png is the committed one)

THE SCENE is gate_ebldboom.py's SCG88EA (a copy of SCM01EA): the Construction Yard at its
spot with two Medium Tanks a few cells south of it. The tanks are first sent to the yard's
south-east, so their shells come in from that side and the hits land in the south-east
zone (and one in the east): that zone's sector edge runs due south from the yard's centre,
straight down the open concrete in front of the hangar door, so a seam has to cross every
scanline of the open slab there. Then the tanks force-fire until the yard is at 80 percent
(four chip steps: three in the south-east zone, which stands at three quarters, and one in
the east), are stopped, 45 ticks let the weights settle, and the yard is shot and dumped;
then on to below half, for the damaged stage's shot and dump. The shots are 2560 x 1440:
the seam is a few pixels wide, and at this size the open slab is forty scanlines.

CLAIMS, from the shots' pixels and the dump lines:
  (1) the dump after the held shot says HELD with a zone at 0.75 or more, the one after
      the damaged shot says DAMAGED (the overlay is drawn in the first, never in the
      second);
  (2) the held shot is sooted at all: inside the open slab window the darkening (intact
      minus held, in luminance) exceeds 18 on at least SOOTED pixels;
  (3) NO SEAM: on each scanline of the window the largest rise of the darkening over 4 px,
      after a 5 px running mean, is taken, and the median over the scanlines stays under
      SEAM. The alpha test's seam, the whole step of the soot across every scanline at
      one x, measured 20.7 here; the blend measures 6.7, patch edges only;
  (4) the damaged shot matches the committed reference crop: the approved damaged stage,
      which the overlay must leave alone. The crop is the whole yard and its ground as the
      build draws them, so a deliberate change of the Enhanced look (its defaults, its
      ground, its light) stales it, as it stales the facewatch reference: re-cut it with
      `ref` from a run whose damaged stage was looked at.

THE WINDOW is the open concrete between the crane's claw and the hangar's front at this
camera, read off the intact shot: rows 770 to 810, x 1270 to 1390, above the slab's rim
and below the hangar's base. The yard's cell is asserted from the layout, so a moved yard
fails here rather than measuring grass.
"""
import json
import os
import re
import sys

from PIL import Image

SOOTED = 200                             # measured: 434 blended, 300 under the alpha test
SEAM = 12.0
WINDOW = (1270, 770, 1390, 811)          # x0, y0, x1, y1 (exclusive) in the 2560 x 1440 shot
REFBOX = (1010, 460, 1590, 840)          # the yard, for the damaged stage's reference
YARD = (25, 24)                          # the FACT's cell in gate_ebldboom.py's SCG88EA
# The approved damaged reference was authored with these values in e7a26621.
# Pin its lighting; the shared visual defaults remain the player's current choice.
REFERENCE_LIGHT = [
    'gfx unified_lighting 1', 'gfx sky_energy 1.44643',
    'gfx object_shadow_fill 0.41964', 'gfx st_bright 1',
    'gfx st_sun 0.60714', 'gfx st_glow 0.75', 'gfx vh_sat 0.83929']


def maps(out):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gate_ebldboom
    gate_ebldboom.maps(out)
    layout = json.load(open(os.path.join(out, "layout.json")))["SCG88EA"]
    fact = [d for d in layout["types"] if d["type"] == "FACT"][0]
    if (fact["x"], fact["y"]) != YARD:
        sys.exit("gate_ebldseam: the yard is at %d,%d, not %d,%d; the window would be wrong"
                 % (fact["x"], fact["y"], YARD[0], YARD[1]))
    x, y, t6, t7 = fact["x"], fact["y"], fact["tanks"][0], fact["tanks"][1]
    look = "cam %.1f %.1f" % (x + 1.5, y + 1.3)
    aim = (x + 2, y + 1)                      # its south-east cell
    s = REFERENCE_LIGHT + ["tick 3", "zoom max", look, "tick 1", "dist 1200", look, "tick 1",
         "shot shots/g360_intact.png", "ebldump"]
    # the tanks go round to the south-east first
    s += ["dist 2400", "cam %.1f %.1f" % (x + 3.0, y + 4.0), "tick 1",
          "lclickobj MTNK %d" % t6, "actcell %d %d" % (x + 6, y + 4),
          "lclickobj MTNK %d" % t7, "actcell %d %d" % (x + 7, y + 5),
          "deselect", "tick 150", "ebldump"]
    for pct, tag in ((80, "held"), (49, "damaged")):
        s += ["dist 2400", "cam %.1f %.1f" % (x + 3.0, y + 4.0), "tick 1"]
        for n in (t6, t7):
            s += ["lclickobj MTNK %d" % n, "actcell %d %d c" % aim]
        s += ["tickstr FACT 0 %d 1500" % pct]
        for n in (t6, t7):
            s += ["lclickobj MTNK %d" % n, "stop"]
        s += ["deselect", "dist 1200", look, "tick 45", "shot shots/g360_%s.png" % tag, "ebldump"]
    s += ["quit"]
    open(os.path.join(out, "seam.txt"), "w").write("\n".join(s) + "\n")


def lum(path):
    im = Image.open(path).convert("RGB")
    px = im.load()
    w, h = im.size
    return [[(px[x, y][0] * 299 + px[x, y][1] * 587 + px[x, y][2] * 114) / 1000.0
             for x in range(w)] for y in range(h)], w, h


def darkening(intact, held):
    """intact minus held, luminance, inside WINDOW: a list of rows."""
    (a, w, h), (b, w2, h2) = lum(intact), lum(held)
    if (w, h) != (w2, h2):
        return None
    x0, y0, x1, y1 = WINDOW
    if x1 > w or y1 > h:
        return None
    return [[a[y][x] - b[y][x] for x in range(x0, x1)] for y in range(y0, y1)]


def seam(rows, smooth=5, span=4):
    """the median over the rows of the largest rise over `span` px of the smoothed row"""
    rises = []
    for r in rows:
        s = [sum(r[i:i + smooth]) / smooth for i in range(len(r) - smooth + 1)]
        rises.append(max(s[i + span] - s[i] for i in range(len(s) - span)))
    rises.sort()
    return rises[len(rises) // 2]


def sooted(rows):
    return sum(1 for r in rows for v in r if v > 18)


def ref_diff(shot, ref):
    """mean absolute luminance difference over the yard, and the fraction of its pixels
    differing by more than 24"""
    a = Image.open(shot).convert("RGB").crop(REFBOX)
    b = Image.open(ref).convert("RGB")
    if a.size != b.size:
        return None
    pa, pb = a.load(), b.load()
    n = a.size[0] * a.size[1]
    tot, big = 0.0, 0
    for y in range(a.size[1]):
        for x in range(a.size[0]):
            la = (pa[x, y][0] * 299 + pa[x, y][1] * 587 + pa[x, y][2] * 114) / 1000.0
            lb = (pb[x, y][0] * 299 + pb[x, y][1] * 587 + pb[x, y][2] * 114) / 1000.0
            d = abs(la - lb)
            tot += d
            if d > 24:
                big += 1
    return tot / n, big / float(n)


def check(log, shots, ref):
    try:
        lines = open(log, errors="ignore").read().splitlines()
    except OSError:
        return "0 no log %s" % log
    dumps = [l for l in lines if l.startswith("EBLDOBJ|FACT|")]
    if len(dumps) < 4:
        return "0 %d FACT dump lines, 4 expected (intact, moved, held, damaged)" % len(dumps)
    held, dmg = dumps[2], dumps[3]
    zones = re.search(r"\|zones=([^|]+)", held)
    top = max(float(v) for v in zones.group(1).split(",")) if zones else 0.0
    if "|stage=HELD|" not in held or top < 0.75:
        return "0 the held dump is not HELD with a zone at 0.75 or more: %s" % held
    if "|stage=DAMAGED|" not in dmg:
        return "0 the damaged dump is not DAMAGED: %s" % dmg
    intact, h, d = (os.path.join(shots, "g360_%s.png" % t) for t in ("intact", "held", "damaged"))
    for p in (intact, h, d, ref):
        if not os.path.isfile(p):
            return "0 missing %s" % p
    rows = darkening(intact, h)
    if rows is None:
        return "0 the shots are not 2560 x 1440 or do not hold the window"
    n = sooted(rows)
    if n < SOOTED:
        return "0 the held yard is not sooted: %d window pixels darker by more than 18 (%d needed)" % (n, SOOTED)
    s = seam(rows)
    if s >= SEAM:
        return "0 a seam: the median scanline rise of the darkening is %.1f (under %.0f allowed; the alpha test measured 20.7)" % (s, SEAM)
    rd = ref_diff(d, ref)
    if rd is None:
        return "0 the reference crop is not the yard's size"
    if rd[0] > 1.5 or rd[1] > 0.005:
        return ("0 the damaged stage differs from the approved reference: mean %.2f, %.2f%% of pixels "
                "by more than 24 (if the Enhanced look changed on purpose, re-cut it: gate_ebldseam.py "
                "ref shots ../gates/g360-damaged-ref.png)" % (rd[0], 100 * rd[1]))
    return ("1 HELD with a zone at %.2f then DAMAGED; the held yard sooted on %d window pixels; "
            "median scanline rise %.1f (seam-free, under %.0f); damaged stage as the reference "
            "(mean %.2f, %.2f%% beyond 24)" % (top, n, s, SEAM, rd[0], 100 * rd[1]))


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "maps":
        maps(a[1])
    elif len(a) == 4 and a[0] == "check":
        print(check(a[1], a[2], a[3]))
    elif len(a) == 3 and a[0] == "ref":
        Image.open(os.path.join(a[1], "g360_damaged.png")).convert("RGB").crop(REFBOX).save(a[2])
    else:
        sys.exit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main())
