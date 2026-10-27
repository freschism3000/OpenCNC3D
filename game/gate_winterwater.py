#!/usr/bin/env python3
"""G366: Tiberian Dawn's winter and snow seas draw through the Enhanced water, with no square seams.

    python3 game/gate_winterwater.py maps DIR      write the two test maps and their scripts
    python3 game/gate_winterwater.py check DIR     read the runs' logs and shots in DIR and
                                                   print "1 message" or "0 why"

THE SCENES. GDI 1's map (missions/SCG01EA.BIN, its play rectangle 36,39 26x23) painted as a
WINTER map (SCW95EA, the INI of the winter donor SCW01EA) and as a SNOW map (SCS95EA, from
SCS01EA: Theater=WINTER plus OpenCNC3DTheater=SNOW), played as the User Maps menu plays a
map with no pack of its own: the theater's donor pack, repainted from the map's own cells
(--reskin). One hidden run each at 1920 x 1080, Enhanced on the Enhanced terrain (--texset
3), the map revealed, the clouds, the trees and the sea's slide, flow and waves held still
by their dials, the sun glint off (water_spec 0), the sea's shader on (gfx water_fx 1); the camera on cell 48,60 at distance
1400, over GDI 1's open sea. After the shot the script asks the renderer where every corner
of the sea's cells lands on the screen (`cursorat`), so the measure is taken at the cells'
real edges and does not depend on the camera maths.

THE CLAIMS, each run:
  (1) the engine's own lines: the coast coverage loaded for its theater
      (COASTMASK|loaded|theater=WINTER, or SNOW, with cells), the water field built from
      wet cells (WATER|field with holed_cells and wet_texels above zero) and the sea's
      plane (WATER|plane), the sea's base its own bank's painting
      (WATER|td|painting|theater=...);
  (2) on the pixels, no straight cell-edge seam in the open sea: across every edge between
      two cells of rows 59 to 61 (x 36 to 61), the cells the coast coverage marks wholly
      water, the signed colour step 1 to 3 px either side of the edge, averaged along its
      middle three quarters, as a colour distance in levels of 255
      (game/gate_ra3dshoreseams.py's measure). A seam steps the same way all along its
      edge; the water's own texture steps both ways and averages out. Sun glints are
      disabled for this colour-continuity measure: a broad normal-map highlight can
      otherwise align with a sampled edge even when the surface is continuous. The
      production ripple normal, refraction, base textures and depth remain enabled. The same measure on
      lines 8 and 12 px either side of the edge is that edge's own level: the sea's slow
      darkening toward the map's edge, seen in perspective, steps a little across every
      line drawn over it, at the edge and beside it alike. A seam is the step AT the edge
      above that level, and the largest such excess over the edges in frame is at most
      SEAM_LIMIT.

THE CAUSE: these banks' 1995 tiles paint their water and the console has no winter
theater, so no cell carried a sea cut; the water field found no wet cell and was never
built, and the sea stayed the painted tiles, a square seam at every tile's edge. Saved
coast coverage for both banks (tools/art/remaster/textures/coast_masks.py, packed by
tools/bakery/pack_coast_masks.py) now gives their Enhanced sheet its cut, as temperate's
and the desert's do; the sea's base and its floor are each bank's own painted open water.

Measured before the fix: no COASTMASK, WATER|field or WATER|plane line in either run, and
open-sea edges stepping 9.2 (winter) and 13.6 (snow) above their own level, 5 and 12 of 48
edges over the limit (it fails); after: every line, at most 4.0 and 3.9; two runs agree
pixel for pixel.
DEPTH-BED REGRESSION CONTROL: on the closed/deep seabed, the
production sun glint raises a Snow line to 6.8 even though removing only the glint
reduces it to 5.3 (winter 4.9). With glint disabled and the same unchanged 6.0 limit,
turning water_fx off exposes the painted-water seams and fails in both theaters
(winter 9.0, snow 13.3). This isolates the atlas-continuity claim without changing
production water, its default settings, or the limit. Full production captures are
checked separately by the submerged-terrain gate.
NOT COVERED: sun-glint appearance; other maps; the shoreline's look, which only an eye can judge; the editor's
live repaint of a cell to water.
"""
import os
import re
import sys

SEAM_LIMIT = 6.0
RECT = (36, 39, 26, 23)
ROWS = (59, 60, 61)
X0, X1 = 36, 61
CAM = (48, 60)
SCENES = {"SCW95EA": ("SCW01EA", "WINTER"), "SCS95EA": ("SCS01EA", "SNOW")}
STILL = ['gfx water_fx 1', 'gfx water_spec 0', 'gfx cloud_on 0', 'gfx water_pan 0', 'gfx water_wave_speed 0',
         'gfx water_flow 0', 'gfx water_flow_rate 0', 'gfx tree3d_wind 0', 'gfx tree3d_flutter 0',
         'gfx grass_wind 0', 'gfx fol_wind 0', 'gfx fol_flutter 0']


def corners():
    return [(x, y) for y in range(ROWS[0], ROWS[-1] + 2) for x in range(X0, X1 + 2)]


def edges():
    """(name, p0, p1): every edge between two of the measured cells."""
    out = []
    for y in ROWS:
        for x in range(X0 + 1, X1 + 1):
            out.append(("x=%d,row %d" % (x, y), (x, y), (x, y + 1)))
    for y in ROWS[1:]:
        for x in range(X0, X1 + 1):
            out.append(("y=%d,col %d" % (y, x), (x, y), (x + 1, y)))
    return out


def maps(d):
    os.makedirs(d, exist_ok=True)
    binary = open(os.path.join("missions", "SCG01EA.BIN"), "rb").read()
    for name, (donor, theater) in SCENES.items():
        ini = open(os.path.join("missions", donor + ".INI"), encoding="latin-1").read()
        for key, value in (("Name", name), ("X", RECT[0]), ("Y", RECT[1]), ("Width", RECT[2]), ("Height", RECT[3])):
            ini, n = re.subn(r"(?m)^%s=.*$" % key, "%s=%s" % (key, value), ini, count=1)
            if n != 1:
                raise SystemExit("%s.INI has no %s= line" % (donor, key))
        open(os.path.join(d, name + ".INI"), "w", encoding="latin-1", newline="\r\n").write(ini)
        open(os.path.join(d, name + ".BIN"), "wb").write(binary)
        shot = os.path.join(d, name + ".png")
        lines = ["tick 2", "reveal 1", "tick 40"] + STILL + ["cam %d %d" % CAM, "dist 1400", "frames 12",
                                                             "shot " + shot]
        lines += ["cursorat %d %d" % p for p in corners()] + ["quit"]
        open(os.path.join(d, name + ".txt"), "w").write("\n".join(lines) + "\n")


def screen_points(log):
    pts = {}
    for line in open(log, errors="replace"):
        m = re.match(r"CURSORAT\|world=([\d.]+),([\d.]+)\|at=([\d.]+),([\d.]+)", line)
        if m:
            pts[(int(round(float(m.group(1)))), int(round(float(m.group(2)))))] = (float(m.group(3)), float(m.group(4)))
    return pts


def edge_step(rgb, p0, p1, shift=0):
    """The signed colour step 1..3 px either side of the edge (or of a line `shift` px
    beside it), averaged along its middle three quarters, as a distance in levels."""
    import numpy as np
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    length = max(1e-6, (dx * dx + dy * dy) ** 0.5)
    nx, ny = -dy / length, dx / length
    steps = []
    for t in np.linspace(0.12, 0.88, 60):
        x = p0[0] + dx * t + nx * shift
        y = p0[1] + dy * t + ny * shift
        a = np.mean([rgb[int(round(y + ny * k)), int(round(x + nx * k))] for k in (1, 2, 3)], 0)
        b = np.mean([rgb[int(round(y - ny * k)), int(round(x - nx * k))] for k in (1, 2, 3)], 0)
        steps.append(a - b)
    return float(np.linalg.norm(np.mean(steps, 0)))


def in_frame(p, w, h):
    # clear of the screen's border and of the sidebar on the right
    return 20 <= p[0] <= w - 220 and 40 <= p[1] <= h - 20


def check_run(d, name, theater):
    log, png = os.path.join(d, name + ".log"), os.path.join(d, name + ".png")
    if not os.path.exists(log):
        return False, "%s: no log" % theater
    text = open(log, errors="replace").read()
    why = []
    m = re.search(r"COASTMASK\|loaded\|theater=%s\|cells=(\d+)" % theater, text)
    if not m or int(m.group(1)) <= 0:
        why.append("no %s coast coverage loaded" % theater)
    f = re.search(r"WATER\|field\|scen=\w+\|res=\d+\|size=\S+\|holed_cells=(\d+)\|wet_texels=(\d+)", text)
    if not f or int(f.group(1)) <= 0 or int(f.group(2)) <= 0:
        why.append("no water field from wet cells")
    if "WATER|plane|" not in text:
        why.append("no water plane")
    if "WATER|td|painting|theater=%s|" % theater not in text:
        why.append("the sea's base is not %s's own painting" % theater)
    if not os.path.exists(png):
        return False, "%s: %s" % (theater, "; ".join(why + ["no shot"]))
    from PIL import Image
    import numpy as np
    rgb = np.asarray(Image.open(png).convert("RGB")).astype(float)
    h, w = rgb.shape[:2]
    pts = screen_points(log)
    steps, beside = [], []
    for label, a, b in edges():
        if a not in pts or b not in pts or not in_frame(pts[a], w, h) or not in_frame(pts[b], w, h):
            continue
        at = edge_step(rgb, pts[a], pts[b])
        level = sum(edge_step(rgb, pts[a], pts[b], s) for s in (-12, -8, 8, 12)) / 4.0
        steps.append((at - level, label))
        beside.append(at)
    if len(steps) < 40:
        why.append("only %d sea edges in frame" % len(steps))
        worst, where, largest = 0.0, "-", 0.0
    else:
        worst, where = max(steps)
        largest = max(beside)
        if worst > SEAM_LIMIT:
            over = sum(1 for s, _ in steps if s > SEAM_LIMIT)
            why.append("a seam: an open-sea edge steps %.1f above its own level at %s (limit %.1f), "
                       "%d of %d edges over" % (worst, where, SEAM_LIMIT, over, len(steps)))
    detail = ("%s: field %s holed cells, %s wet texels; %d open-sea edges, largest step above the "
              "edge's own level %.1f at %s (limit %.1f), largest step %.1f"
              % (theater, f.group(1) if f else "-", f.group(2) if f else "-", len(steps), worst, where,
                 SEAM_LIMIT, largest))
    return not why, detail + ("" if not why else " -- " + "; ".join(why))


def check(d):
    results = [check_run(d, name, theater) for name, (donor, theater) in SCENES.items()]
    return ("1 " if all(ok for ok, _ in results) else "0 ") + " | ".join(m for _, m in results)


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("maps", "check"):
        print(__doc__)
        sys.exit(2)
    if sys.argv[1] == "maps":
        maps(sys.argv[2])
    else:
        print(check(sys.argv[2]))
