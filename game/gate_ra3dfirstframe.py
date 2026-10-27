#!/usr/bin/env python3
"""G359: Red Alert's ground under Enhanced is finished on the mission's first drawn frame: the
picture of the ground does not change once the game has been running a while (ra3d_terrain.h,
ra3d_ground_normals).

    python3 game/gate_ra3dfirstframe.py script DIR     write the runs' scripts
    python3 game/gate_ra3dfirstframe.py check DIR      read the logs and shots: "1 message" or "0 message"

The symptom this guards: on a snow mission, rectangular patches of the snow several cells
across, lit differently from the ground round them and edged by a dark seam, stood over the
slopes for the first few seconds and then all vanished on one frame. The relief surface
(terrain_surface.h) drew its cells with their true slope into the light pass's normal target
while the ground beside them was drawn level, because the ground's corner normals were only
built when something first asked for the corner shade, and under one sun nothing on Red
Alert's ground did until a building apron was drawn (about tick 91 on Allied 1). Temperate
missions hid it because the grass asks for the shade on the first frame; a snow map has no
grass.

Run from a play folder with a staged Red Alert module. Hidden runs, every one Enhanced, the
moving parts of the picture held still by dials (cloud shadows, winds, the sea's animation):

  as   Allied 1 (snow), the shroud on as a player has it, the mission's own start view
  at   Allied 5 (temperate), no shroud (its start view is dark under it)
  us   Soviet 1 (snow), no shroud
  ut   Soviet 2 (temperate), no shroud

Each run shoots the start view: `early` on the first frames (tick 1), `settled`
at the same tick after `cmdump` has read a corner's shade (which builds the ground's light
grid if nothing has yet, so it is the picture the game would settle on), and `late` 150
ticks on (ten seconds of game time). Allied 1 additionally captures `stable` after
revealing the map at the same tick. Its real shrouded early/settled equality remains
checked; late stability compares two fully revealed views so the opening aircraft's
newly revealed snow does not count as changing terrain illumination.

Claims:
  (1) every run: the first normal pass drew the ground with its corner normals
      (RA3D|normals|first pass ... grid=built or grid=ready), never level;
  (2) every run: `early` and `settled` are the same picture, byte for byte;
  (3) Allied 1, Allied 5 and Soviet 2: `early` and `late` differ only where units moved:
      fewer changed pixels in the world view (more than 16 levels) than the run's limit.
      Measured on the fixed renderer: about 14,000 on Allied 1 with its shroud, about 4,000 on
      the two temperate runs; the old renderer changed about 195,000 on Allied 1 (the patches
      going and the ground's light with them). Not Soviet 1, whose opening air raid and fires
      change some 56,000 pixels of its view; claims 1 and 2 cover it.
PROVEN TO FAIL: the renderer before the fix prints no first-pass line (1), Allied 1's early
and settled pictures differ by about 256,000 pixels (2), and its early and late by about
195,000 (3).
NOT COVERED: Tiberian Dawn (its own branch of fx_draw_terrain_normals always built the grid;
its pictures are compared byte for byte outside this gate); how the ground looks (by eye).
"""
import os
import re
import sys

STILL = ['gfx cloud_on 0', 'gfx tree3d_wind 0', 'gfx tree3d_flutter 0', 'gfx water_fx 0',
         'gfx grass_wind 0', 'gfx fol_wind 0', 'gfx fol_flutter 0']
# tag: (scenario, theater, shroud on, the early/late limit in changed pixels, or None where
# the mission's own action fills the view: Soviet 1 opens with an air raid on the village)
RUNS = {
    'as': ('SCG01EA', 'snow', True, 40000),
    'at': ('SCG05EA', 'temperate', False, 20000),
    'us': ('SCU01EA', 'snow', False, None),
    'ut': ('SCU02EA', 'temperate', False, 20000),
}
WORLD = (0, 40, 1060, 720)        # the 1280 x 720 view left of the sidebar, below the tabs


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag in RUNS:
        v = ['tick 1'] + STILL + ['frames 2', f'shot {d}/{tag}_early.png',
             'cmdump 64 64', 'frames 2', f'shot {d}/{tag}_settled.png']
        # Keep the actual player shroud for the first/settled equality proof.
        # Hold visibility fixed for the later stability comparison: the opening
        # aircraft reveals new ground, which is not a terrain-lighting change.
        if tag == 'as':
            v += ['reveal 1', 'frames 2', f'shot {d}/{tag}_stable.png']
        v += ['tick 150', 'frames 2', f'shot {d}/{tag}_late.png', 'quit']
        write(os.path.join(d, tag + '.txt'), v)


def changed(a, b, box=None, floor=0):
    from PIL import Image, ImageChops
    A = Image.open(a).convert('RGB')
    B = Image.open(b).convert('RGB')
    if box:
        A, B = A.crop(box), B.crop(box)
    return sum(ImageChops.difference(A, B).convert('L').histogram()[floor + 1:])


def check(d):
    try:
        import PIL  # noqa: F401
    except ImportError:
        return 0, 'G359 needs Python Imaging (PIL) to compare the shots'
    fails, notes = [], []
    for tag, (scen, theater, shroud, limit) in RUNS.items():
        log = os.path.join(d, tag + '.log')
        if not os.path.isfile(log):
            fails.append(f'{tag}: no log')
            continue
        text = open(log, errors='replace').read()
        if not re.search(r'^SCRIPT\|end .*: \d+ lines, 0 failures$', text, re.M):
            fails.append(f'{tag} ({scen}): its script did not run cleanly')
        m = re.search(r'^RA3D\|normals\|first pass\|scene=' + scen + r'\|grid=(\w+)\|', text, re.M)
        if not m:
            fails.append(f'{tag} ({scen}): no first-pass line (a renderer that builds the ground\'s normals late)')
        elif m.group(1) not in ('built', 'ready'):
            fails.append(f'{tag} ({scen}): the first normal pass drew the ground {m.group(1)}')
        shots = [os.path.join(d, f'{tag}_{k}.png') for k in ('early', 'settled', 'late')]
        if not all(os.path.isfile(s) for s in shots):
            fails.append(f'{tag} ({scen}): a shot is missing')
            continue
        same = changed(shots[0], shots[1])
        stable = os.path.join(d, f'{tag}_stable.png')
        moved = changed(stable if os.path.isfile(stable) else shots[0], shots[2], WORLD, 16)
        if same:
            fails.append(f'{tag} ({scen}, {theater}): the first frames and the settled ground differ '
                         f'in {same} pixels')
        if limit is not None and moved >= limit:
            fails.append(f'{tag} ({scen}, {theater}): {moved} pixels changed between tick 1 and tick 151 '
                         f'(limit {limit})')
        notes.append(f'{scen} {theater} {m.group(1) if m else "?"} '
                     + (f'{moved}px moved' if limit is not None else 'its raid not compared'))
    if fails:
        return 0, 'G359 Red Alert\'s first frame: ' + '; '.join(fails)
    return 1, ('G359 Red Alert\'s ground is finished on its first frame (normals at the first pass, '
               'the same picture settled, only units moving 150 ticks on): ' + ', '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        sys.stderr.write(__doc__)
        sys.exit(2)
    if sys.argv[1] == 'script':
        script(sys.argv[2])
    else:
        ok, msg = check(sys.argv[2])
        print(f'{ok} {msg}')
