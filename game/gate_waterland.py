#!/usr/bin/env python3
"""G367: the water draws nothing over land, and nothing of the frame's background shows
through it, at two Red Alert snow shores.

    python3 game/gate_waterland.py script DIR     write the runs' scripts
    python3 game/gate_waterland.py check DIR      read the logs and shots: "1 message" or "0 message"

TWO HIDDEN RUNS at 1920 x 1080, Enhanced on the Enhanced terrain, the map revealed, the
clouds, the trees and the sea's motion held still, a fixed seed:
  Allied 8 (SCG08EA), the camera on cell 53,75, the land beside the bay whose shore the
      scene's heights put below the sea's plane (cells 49 to 52, 70 to 77);
  Allied 14 (SCG14EA), the camera on cell 84,79, the land beside the ford whose painted foam
      is cut as water (cells 73 to 82, 76 to 78).
The camera stands on land so that its height, which follows the water's surface where it
stands on water, is the same with the water on and off, and the two pictures are
registered pixel for pixel. Each run shoots the scene with the water on, then turns the
water off (gfx water_fx 0) and shoots it again. After each shot the renderer is asked where
a grid of world points (every quarter cell over the view) lands on the screen (`cursorat`).
The water field the run built is dumped (OPENCNC3D_WATER_DUMP: its signed distance to the
coast, from the sheet's own cut) and says which points are land.

  (1) LAND STAYS LAND: of the points at least MARGIN cells inland of the cut (beyond the
      wash that laps up the beach), at most LAND_LIMIT of them may differ between the two
      pictures by more than COLOUR_STEP in any channel (3 x 3 pixel means). The water
      surface, its floor and its wash may only draw where the map is water.
  (2) NO HOLES: inside the play area at most HOLE_LIMIT pixels of the water-on picture are
      the frame's black background (all channels at most BLACK) where the water-off picture
      is not: the floor under a clear texel of the ground has to be there.
  (3) The runs built the water (WATER|field and WATER|plane lines in each log), and the
      camera did not move between the two pictures.

THE CAUSE: a body of water is drawn at one plane, the mode of its open-water corners, and
the scene's heights can put a bank below it. The floor under the water was laid at that
plane over every wet cell, so where the bank was lower it stood above the land and drew
the cell's painting as a flat raised plate with straight edges (white snow and brown
shore rectangles at Allied 8); and neighbouring cells' floors stepped at their shared edge,
so the frame's black background showed through clear texels of the ground (the dark
squares across the ford at Allied 14). Now the floor is one height per corner, never above
the land, and the surface comes down to a bank that lies below the plane
(game/water_plane.h, water_plane_lay_heights).

Measured on the binary before the fix: Allied 8, 20 of 1072 land points changed (1.9
percent) and 297 background pixels; Allied 14, 4 of 1153 and 846 background pixels (it
fails). After: 0 of 1072 and 0; 3 of 1153 (the wash on the ford's far bank, there before
the fix too) and 0. Two runs give the same pictures byte for byte.
NOT COVERED: other maps (Tiberian Dawn shares the code and had the same plates on its
banks); the look of the water itself, by eye.
"""
import os
import re
import sys

MARGIN = 0.5           # cells inland of the cut
COLOUR_STEP = 24.0     # levels of 255, any channel
LAND_LIMIT = 0.01      # fraction of land points
BLACK = 3              # the frame's background, all channels at most this
HOLE_LIMIT = 20        # pixels: a dark crevice of the painted art can read as black
W, H = 1920, 1080
HUD_X = 1640           # the sidebar starts here at 1920 wide
TOP = 18               # the top bar
STILL = ['gfx water_fx 1', 'gfx cloud_on 0', 'gfx water_pan 0', 'gfx water_wave_speed 0', 'gfx water_flow 0',
         'gfx water_flow_rate 0', 'gfx water_swash 0', 'gfx tree3d_wind 0', 'gfx tree3d_flutter 0',
         'gfx grass_wind 0', 'gfx fol_wind 0', 'gfx fol_flutter 0']
SCENES = [('SCG08EA', 53, 75), ('SCG14EA', 84, 79)]
SPAN_X, SPAN_Z, STEP = 10, 6, 0.25


def points(cx, cz):
    n_x, n_z = int(2 * SPAN_X / STEP), int(2 * SPAN_Z / STEP)
    return [(cx - SPAN_X + i * STEP + STEP / 2, cz - SPAN_Z + j * STEP + STEP / 2)
            for j in range(n_z) for i in range(n_x)]


def write_scripts(d):
    os.makedirs(d, exist_ok=True)
    for scen, cx, cz in SCENES:
        ask = ['cursorat %.3f %.3f' % p for p in points(cx, cz)]
        lines = ['tick 2', 'reveal 1', 'tick 40'] + STILL + ['cam %d %d' % (cx, cz), 'dist 1400', 'frames 12',
                 'shot ' + os.path.join(d, scen + '_on.png')] + ask
        lines += ['gfx water_fx 0', 'frames 12', 'shot ' + os.path.join(d, scen + '_off.png')] + ask + ['quit']
        with open(os.path.join(d, scen + '.txt'), 'w') as f:
            f.write('\n'.join(lines) + '\n')


def picture(png):
    from PIL import Image
    import numpy as np
    return np.asarray(Image.open(png).convert('RGB')).astype(float)


def check_scene(d, scen, cx, cz):
    import numpy as np
    log = open(os.path.join(d, scen + '.log'), errors='replace').read()
    field = re.search(r'WATER\|field\|scen=[^|]*\|res=(\d+)\|size=(\d+)x(\d+)', log)
    if not field or 'WATER|plane|' not in log:
        return None, '%s: the water was not built (no WATER|field or WATER|plane line)' % scen
    res, fw, fh = int(field.group(1)), int(field.group(2)), int(field.group(3))
    raw = open(os.path.join(d, scen + '.field'), 'rb').read()
    if len(raw) != fw * fh * 4:
        return None, '%s: the field dump is %d bytes, not %d' % (scen, len(raw), fw * fh * 4)
    sd = (np.frombuffer(raw, dtype=np.uint8).reshape(fh, fw, 4)[..., 0].astype(float) / 255.0 - 0.5) * 8.0
    rect = re.search(r'RA3D\|scene\|ground=red alert\|theater=[A-Z]+\|rect=(\d+),(\d+) (\d+)x(\d+)', log)
    rx, rz, rw, rh = (int(v) for v in rect.groups()) if rect else (0, 0, 10 ** 6, 10 ** 6)
    at = [(float(m.group(1)), float(m.group(2))) for m in
          re.finditer(r'CURSORAT\|world=[-\d.]+,[-\d.]+\|at=([-\d.]+),([-\d.]+)', log)]
    pts = points(cx, cz)
    if len(at) != 2 * len(pts):
        return None, '%s: %d screen points answered, %d asked' % (scen, len(at), 2 * len(pts))
    on, off = picture(os.path.join(d, scen + '_on.png')), picture(os.path.join(d, scen + '_off.png'))

    def inside(p):
        return TOP + 2 <= p[1] < H - 2 and 2 <= p[0] < min(W, HUD_X) - 2

    def mean3(img, p):
        x, y = int(round(p[0])), int(round(p[1]))
        return img[y - 1:y + 2, x - 1:x + 2].reshape(-1, 3).mean(axis=0)

    land = changed = 0
    for k, (wx, wz) in enumerate(pts):
        a, b = at[k], at[len(pts) + k]
        if not (rx <= wx < rx + rw and rz <= wz < rz + rh):
            continue
        tx, tz = int(wx * res), int(wz * res)
        if not (0 <= tx < fw and 0 <= tz < fh) or sd[tz, tx] > -MARGIN or not (inside(a) and inside(b)):
            continue
        land += 1
        if np.abs(mean3(on, a) - mean3(off, b)).max() > COLOUR_STEP:
            changed += 1
    # The camera stands on land, so the two pictures are registered pixel for pixel.
    moved = max(abs(at[k][0] - at[len(pts) + k][0]) + abs(at[k][1] - at[len(pts) + k][1]) for k in range(len(pts)))
    if moved > 0.5:
        return None, '%s: the camera moved %.1f px between the two pictures' % (scen, moved)
    play = np.zeros(on.shape[:2], dtype=bool)
    play[TOP:, :min(W, HUD_X)] = True
    holes = int((play & (on.max(axis=2) <= BLACK) & (off.max(axis=2) > BLACK + 20)).sum())
    return (land, changed, holes), None


def check(d):
    out, ok = [], True
    for scen, cx, cz in SCENES:
        r, err = check_scene(d, scen, cx, cz)
        if err:
            return '0 ' + err
        land, changed, holes = r
        frac = changed / float(land) if land else 1.0
        good = land >= 200 and frac <= LAND_LIMIT and holes <= HOLE_LIMIT
        ok &= good
        out.append('%s %d of %d land points changed (%.1f%%, at most %.1f%%), %d background pixels (at most %d)'
                   % (scen, changed, land, 100.0 * frac, 100.0 * LAND_LIMIT, holes, HOLE_LIMIT))
    return ('1 ' if ok else '0 ') + '; '.join(out)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        write_scripts(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        print(check(sys.argv[2]))
    else:
        sys.exit(__doc__)
