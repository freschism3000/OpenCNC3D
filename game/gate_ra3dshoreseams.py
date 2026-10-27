#!/usr/bin/env python3
"""G362: Red Alert's snow shore carries no hard-edged seams at two cells of Soviet 4.

    python3 game/gate_ra3dshoreseams.py script DIR     write the run's script
    python3 game/gate_ra3dshoreseams.py check DIR      read the log and shot: "1 message" or "0 message"

One hidden run of SCU04EB (snow) at 1920 x 1080, Enhanced, the map revealed, the camera on
cell 60,60 at distance 1400, the clouds, the trees and the sea's motion held still by their
dials. After the shot the script asks the renderer where six world points land on the
screen (`cursorat`), so the measures below are taken at the cells' real corners and do not
depend on the camera maths.

THE MEASURE is the step at a cell edge itself: along the edge, the mean colour 1 to 3 px on
one side minus the other, averaged with its sign over the whole edge, as a colour distance
in levels of 255. A seam steps the same way all along its edge; rock and water texture
steps both ways and averages out. The same measure on lines 8 and 12 px beside the edge is
printed as the scene's own level.

  (1) THE CLIFF: the shared edge x=59 between the shore templates SH08 (cells 58,59 and
      58,60) and SH53 (cells 59,59 and 59,60), over the rock of row 60. At most
      CLIFF_LIMIT.
  (2) THE SHORE CELL 60,62 (SH17 icon 0, a beach icon painted as a wet bottom): its north
      edge (SH53 icon 7), south edge (SH54 icon 1) and east edge (SH17 icon 1) each at
      most EDGE_LIMIT, and its inner half not snow (luminance at most SNOW_LIMIT).

THE CAUSE, both seams: the daylight processing divides each template's own low-frequency
light out of everything that is not snow-covered ground, then re-exposes it. On a shore
that includes the dark wet shallows, brightened up to 1.6 times inside templates whose rock
footprint covered them and not at all in the beach icon, whose footprint was empty; and it
gave the join strips that carry each template past its edge no opacity, so every processed
template ended on a straight cell-edge line. Shore templates now keep their approved
painting and strips, and every other processed template returns to its approved painting
over its last 48 texels (tools/bakery/terrain_daylight.py, SHORE_BANKS and EDGE_BANKS).

Measured on the packs before the fix: cliff 10.8, north 20.6, south 26.1, east 46.3 (all
four fail); after: 5.1, 9.2, 7.1, 5.7, each at the level of the lines beside it. Interior
luminance 93 both.
NOT COVERED: other maps and other joins (other kinds of join are seen in the shots taken
for the fix, not here; G363 checks every bank's template borders in the packs); the look,
by eye.
"""
import os
import re
import sys

CLIFF_LIMIT = 8.0
EDGE_LIMIT = 14.0
SNOW_LIMIT = 170.0
STILL = ['gfx cloud_on 0', 'gfx water_pan 0', 'gfx water_wave_speed 0', 'gfx water_flow 0',
         'gfx water_flow_rate 0', 'gfx water_swash 0', 'gfx tree3d_wind 0', 'gfx tree3d_flutter 0',
         'gfx grass_wind 0', 'gfx fol_wind 0', 'gfx fol_flutter 0']
POINTS = [(59, 59), (59, 61), (60, 62), (61, 62), (61, 63), (60, 63)]


def write_script(d):
    os.makedirs(d, exist_ok=True)
    shot = os.path.join(d, 'shore.png')
    lines = ['tick 2', 'reveal 1', 'tick 60'] + STILL + ['cam 60 60', 'dist 1400', 'frames 12', 'shot ' + shot]
    lines += ['cursorat %d %d' % p for p in POINTS] + ['quit']
    with open(os.path.join(d, 'shore.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


def picture(png):
    from PIL import Image
    import numpy as np
    return np.asarray(Image.open(png).convert('RGB')).astype(float)


def luma(c):
    return 0.299 * c[..., 0] + 0.587 * c[..., 1] + 0.114 * c[..., 2]


def screen_points(log):
    pts = {}
    for line in open(log, errors='replace'):
        m = re.match(r'CURSORAT\|world=([\d.]+),([\d.]+)\|at=([\d.]+),([\d.]+)', line)
        if m:
            pts[(int(round(float(m.group(1)))), int(round(float(m.group(2)))))] = (float(m.group(3)), float(m.group(4)))
    return pts


def along(p0, p1, t0, t1, n):
    import numpy as np
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    length = max(1e-6, (dx * dx + dy * dy) ** 0.5)
    for t in np.linspace(t0, t1, n):
        yield p0[0] + dx * t, p0[1] + dy * t, -dy / length, dx / length


def interior(lum, corners):
    """The mean luminance of the inner half of the quad (the corners pulled halfway in)."""
    import numpy as np
    cx = sum(p[0] for p in corners) / 4.0
    cy = sum(p[1] for p in corners) / 4.0
    inner = [((p[0] + cx) / 2.0, (p[1] + cy) / 2.0) for p in corners]
    xs = [p[0] for p in inner]; ys = [p[1] for p in inner]
    x0, x1, y0, y1 = int(min(xs)), int(max(xs)), int(min(ys)), int(max(ys))
    return float(np.mean(lum[y0:y1 + 1, x0:x1 + 1]))


def edge_step(rgb, p0, p1, t0, t1, shift=0):
    """The signed colour step 1..3 px either side of the edge (or of a line `shift` px
    beside it), averaged along the edge from t0 to t1, as a distance in levels."""
    import numpy as np
    steps = []
    for x, y, nx, ny in along(p0, p1, t0, t1, 60):
        x += nx * shift; y += ny * shift
        a = np.mean([rgb[int(round(y + ny * k)), int(round(x + nx * k))] for k in (1, 2, 3)], 0)
        b = np.mean([rgb[int(round(y - ny * k)), int(round(x - nx * k))] for k in (1, 2, 3)], 0)
        steps.append(a - b)
    return float(np.linalg.norm(np.mean(steps, 0)))


def beside(rgb, p0, p1, t0, t1):
    return sum(edge_step(rgb, p0, p1, t0, t1, s) for s in (-12, -8, 8, 12)) / 4.0


def check(d):
    log, png = os.path.join(d, 'shore.log'), os.path.join(d, 'shore.png')
    if not os.path.exists(png):
        return '0 no shot was taken'
    pts = screen_points(log)
    if any(p not in pts for p in POINTS):
        return '0 the run did not report the cells\' corners (cursorat)'
    rgb = picture(png)
    lum = luma(rgb)
    edges = {'cliff': (pts[(59, 59)], pts[(59, 61)], 0.35, 0.6),
             'north': (pts[(60, 62)], pts[(61, 62)], 0.12, 0.88),
             'south': (pts[(61, 63)], pts[(60, 63)], 0.12, 0.88),
             'east': (pts[(61, 62)], pts[(61, 63)], 0.12, 0.88),
             'west': (pts[(60, 63)], pts[(60, 62)], 0.12, 0.88)}
    step = {k: edge_step(rgb, *e) for k, e in edges.items()}
    level = {k: beside(rgb, *e) for k, e in edges.items()}
    corners = [pts[(60, 62)], pts[(61, 62)], pts[(61, 63)], pts[(60, 63)]]
    snow = interior(lum, corners)
    ok = (step['cliff'] <= CLIFF_LIMIT and all(step[k] <= EDGE_LIMIT for k in ('north', 'south', 'east'))
          and snow <= SNOW_LIMIT)
    msg = ('cliff edge step %.1f (limit %.1f, beside it %.1f); shore cell edges north %.1f south %.1f '
           'east %.1f (limit %.1f; beside them %.1f %.1f %.1f), west %.1f (reported); interior luminance %.0f '
           '(limit %.0f)' % (step['cliff'], CLIFF_LIMIT, level['cliff'], step['north'], step['south'],
                             step['east'], EDGE_LIMIT, level['north'], level['south'], level['east'],
                             step['west'], snow, SNOW_LIMIT))
    return ('1 ' if ok else '0 ') + msg


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        print(__doc__); sys.exit(2)
    if sys.argv[1] == 'script':
        write_script(sys.argv[2])
    else:
        print(check(sys.argv[2]))
