#!/usr/bin/env python3
"""A placement plays to its end when the building is hit while it goes up.

    python3 game/gate_eplchit.py script DIR      write the two runs' scripts
    python3 game/gate_eplchit.py check DIR       read the logs and shots: "1 message" or "0 message"

TWO HIDDEN RUNS of a Tiberian Dawn skirmish (SCM01EA, GDI, a fixed seed), identical but for
one thing: the player's MCV deploys into a Construction Yard, and in the HIT run a Medium Tank
spawned beside it is ordered to force-fire on the yard 0.4 s into its placement and stopped
1.2 s later (the yard loses a few percent of its strength); in the CALM run nothing fires.
Shots of the yard every 0.4 s from then until long after its placement is over, and the
placement's own dump (eplcdump) at each. Claims:

  (1) the hit is seen while the placement plays (EPLC|hit for the player's yard);
  (2) the placement is not cut short: it ends `why=done`, on the same engine frame as the
      calm run's, and its clock (EPLCOBJ t) runs on past the hit to the placement's length;
  (3) the yard is drawn as it is when not hit: from 1 s after the hit to the end, every shot
      matches the calm run's within FRAME_LIMIT percent of the yard's area (the impact's flash
      and the damage smoke are the only differences), so neither a cut to the finished
      building nor the damaged stage drawn over the placement passes.

The 1995 engine's build-up runs on when the building is damaged (BuildingClass::Take_Damage
never touches the build state). Measured on the build before the fix: the placement ended
`why=hit` 1.73 s into its 6.55 s and the yard stood finished at once (the shots differ from the
calm run's on up to 14 percent of the yard); after, `why=done` on the calm run's frame and every
shot from 1 s after the hit within 1.3 percent.
NOT COVERED: a Red Alert placement hit while it goes up (the same code, ebld_place.h); a hit
that destroys the building, which ends the placement as before (checked once: the pictures
are the same as before the fix, pixel for pixel).
"""
import os
import re
import sys

FRAME_LIMIT = 3.0
BOX = (380, 150, 900, 500)
SHOTS = 20


def write_scripts(d):
    os.makedirs(d, exist_ok=True)
    for mode in ('hit', 'calm'):
        v = ['spawnobj MTNK 9 21', 'tick 5', 'look MCV:@me', 'tick 1', 'lclickobj MCV:@me',
             'actclickobj MCV:@me', 'tick 29']
        if mode == 'hit':
            v += ['lclickobj MTNK 0', 'actcell 5 19 c']
        for i in range(1, SHOTS + 1):
            if mode == 'hit' and i == 4:
                v += ['lclickobj MTNK 0', 'stop']
            v += ['tick 6', 'look FACT:@me', 'dist 1',
                  'shot %s' % os.path.join(d, '%s_%02d.png' % (mode, i)), 'eplcdump']
        v.append('quit')
        with open(os.path.join(d, '%s.txt' % mode), 'w') as f:
            f.write('\n'.join(v) + '\n')


def placement(log):
    """The player's yard: its key, start, hit and end lines, and its clock at each dump."""
    key = start = None
    hit = end = None
    clock = []
    for line in open(log, errors='replace'):
        m = re.match(r'EPLCOBJ\|FACT\|key=(\d+)\|.*\|t=([-\d.]+)\|.*\|cut=(\d)\|mine=1\|', line)
        if m:
            key = key or m.group(1)
            if m.group(1) == key:
                clock.append((float(m.group(2)), int(m.group(3))))
    if key is None:
        return None
    for line in open(log, errors='replace'):
        if re.match(r'EPLC\|start\|key=%s\|' % key, line):
            start = int(re.search(r'frame=(\d+)', line).group(1))
        elif re.match(r'EPLC\|hit\|key=%s\|' % key, line):
            hit = line.strip()
        elif re.match(r'EPLC\|end\|key=%s\|' % key, line):
            end = line.strip()
            if 'why=hit' in line and hit is None:
                hit = line.strip()      # (a build that still ends a placement on a hit)
    return dict(key=key, start=start, hit=hit, end=end, clock=clock)


def changed(a, b):
    from PIL import Image
    import numpy as np
    x = np.asarray(Image.open(a).convert('RGB').crop(BOX)).astype(int)
    y = np.asarray(Image.open(b).convert('RGB').crop(BOX)).astype(int)
    return float((np.abs(x - y).max(axis=2) > 24).mean() * 100)


def check(d):
    H, C = placement(os.path.join(d, 'hit.log')), placement(os.path.join(d, 'calm.log'))
    if not H or not C:
        return '0 a run reported no placement for the player\'s Construction Yard (EPLCOBJ)'
    if not H['hit']:
        return '0 the tank\'s fire never hit the yard while it went up (no EPLC|hit): the scene did not play'
    hf = int(re.search(r'frame=(\d+)', H['hit']).group(1))
    ht = float(re.search(r'\|t=([\d.]+)', H['hit']).group(1))
    endf = lambda e: int(re.search(r'frame=(\d+)', e).group(1)) if e else -1
    why = re.search(r'why=(\w+)', H['end']).group(1) if H['end'] else 'none'
    ran_on = max([t for t, cut in H['clock'] if cut == 0] or [0.0])
    worst, shot = 0.0, 0
    first = 1 + int((hf - (H['start'] or 0)) / 6.0) + 3 if H['start'] is not None else 8
    for i in range(max(first, 1), SHOTS + 1):
        a, b = os.path.join(d, 'hit_%02d.png' % i), os.path.join(d, 'calm_%02d.png' % i)
        if not (os.path.exists(a) and os.path.exists(b)):
            return '0 shot %d is missing' % i
        c = changed(a, b)
        if c > worst:
            worst, shot = c, i
    ok = (why == 'done' and endf(H['end']) == endf(C['end']) and ran_on >= 6.0 and worst <= FRAME_LIMIT)
    msg = ('hit %.2f s into its placement; it ended why=%s on frame %d (the calm run on %d); its clock ran on to '
           '%.2f s; from 1 s after the hit the yard differs from the calm run on at most %.1f%% of its area '
           '(shot %d, limit %.1f%%)' % (ht, why, endf(H['end']), endf(C['end']), ran_on, worst, shot, FRAME_LIMIT))
    return ('1 ' if ok else '0 ') + msg


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        print(__doc__)
        sys.exit(2)
    if sys.argv[1] == 'script':
        write_scripts(sys.argv[2])
    else:
        print(check(sys.argv[2]))
