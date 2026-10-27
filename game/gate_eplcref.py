#!/usr/bin/env python3
"""G364: a placement draws the picture its approved version drew, every frame to its last
(game/ebld_place.h: the runs in the model's own order, the card path for rigid runs only).

  gate_eplcref.py script DIR         write the runs' scripts and the Temple's mission into DIR
  gate_eplcref.py check DIR REFDIR   read the runs' logs and shots; print "1 message" or "0 why"
  gate_eplcref.py ref DIR REFDIR     cut the references (gates/g364-<scene>.png) from a run's
                                     shots; only ever from a binary whose placements are the
                                     approved ones

THREE HIDDEN RUNS, at 1280 x 720 with a pinned preset (gates.sh writes gfx_g364.cfg):
  td    the Tiberian Dawn skirmish SCM01EA: the GDI MCV deploys and the Construction Yard's
        placement plays (the MCV is its companion: it unfolds into the yard, its tractor
        drives off); shots at eight moments to the placement's last frame, and at rest;
  ra    the Red Alert snow skirmish SCM03EA, Allied, seeded: the same for its yard (the
        MCV's gantry and drum fold and are squashed flat onto its bed);
  tmpl  a copy of SCM01EA with one Temple of Nod: its placement played (eplcplay), shots
        while the spine slides along the ridge over the roof blades, to its last frame.
The camera stands on the building for every shot and an eplcdump follows each.

CLAIMS, per scene:
  (1) every shot is at the moment it names: the player's EPLCOBJ line after it gives the
      building's placement time within half a tick of the scene's moment (the last moving
      frame among them), and the rest shot comes after the placement's end;
  (2) every shot matches its reference: the shot's crop round the building (halved for the
      Tiberian Dawn scenes), against
      the matching tile of gates/g364-<scene>.png, cut from the binary before the
      placement speed-up. A pixel counts as changed where a channel differs by more than 24 and its
      eight neighbours do too (a part's edge moved by a fraction of a pixel changes a line of
      pixels, which this drops; a face drawn differently changes an area, which it keeps):
      at most CHANGED of them a shot.
PROVEN TO FAIL on the binary of the speed-up as first pushed (the parts drawn in a gathered
order and every motion on the card): the Red Alert yard's squashed gantry and the Temple's
ridge differ from the references; the Tiberian Dawn yard did not.
NOT COVERED: every other placement (the survey that found these two covers all of them, and
the structure gates cover how they play); the frame cost (G361).
"""
import os
import re
import sys

import numpy as np
from PIL import Image

PLAY = os.getcwd()
CHANGED = 24
# scene: the moments (ticks after the placement's start; the last is the last moving frame),
# the rest shot (ticks after the start), the placement's length in ticks, the crop round the
# building in the 1280 x 720 shot (the camera at its nearest, the cartridge's distance), and
# whether the crop is compared halved (Red Alert's yard is compared whole: the MCV's gantry
# squashed onto its bed covers a few hundred pixels at this distance)
SCENES = {
    'td': dict(at=[8, 23, 38, 53, 68, 83, 93, 98], rest=106, length=98.25,
               box=(420, 170, 860, 530), half=True),
    'ra': dict(at=[9, 21, 24, 27, 30, 42, 58], rest=66, length=58.5,
               box=(440, 150, 840, 510), half=False),
    'tmpl': dict(at=[9, 13, 17, 21, 26, 40, 59], rest=66, length=59.1,
                 box=(470, 90, 810, 520), half=True),
}
TYPE = {'td': 'FACT', 'ra': 'FACT', 'tmpl': 'TMPL'}


def shots(scene, start_lines):
    """The script's shooting part: from the placement's start, each moment, then the rest."""
    s = SCENES[scene]
    v = list(start_lines)
    done = 0
    t = TYPE[scene]
    for i, at in enumerate(s['at'] + [s['rest']]):
        if at > done:
            v.append('tick %d' % (at - done))
            done = at
        # (dist clamps to the nearest the camera comes under Enhanced)
        v += ['look %s:@me' % t if scene != 'tmpl' else 'look TMPL', 'dist 1',
              'shot SHOTDIR/g364_%s_%d.png' % (scene, i), 'label G364 %s %d' % (scene, i), 'eplcdump']
    return v + ['quit']


def temple_mission(d):
    """A copy of the skirmish map SCM01EA as a mission with one Temple of Nod, the player's."""
    import gate_ebldmg as m
    src = os.path.join(PLAY, 'missions', 'SCM01EA')
    b = open(src + '.BIN', 'rb').read()
    ini = open(src + '.INI', encoding='latin-1').read()
    mapsec = re.search(r'\[MAP\].*?(?=\n\[)', ini, re.S).group(0).replace('\r', '').strip()
    clear = lambda x, y: 3 <= x < 61 and 11 <= y < 60 and b[(y * 64 + x) * 2] in (0, 0xFF)
    fw, fh = m.FOOT['TMPL']
    spot = next((x, y) for y in range(20, 56) for x in range(10, 58)
                if all(clear(xx, yy) for xx in range(x - 2, x + fw + 2) for yy in range(y - 2, y + fh + 3)))
    x, y = spot
    text = m.basic() + [mapsec, ''] + m.houses() + ['[STRUCTURES]', '000=GoodGuy,TMPL,256,%d,0,None' % (y * 64 + x),
                                                     '', '[UNITS]', '', '[INFANTRY]', '', '[OVERLAY]'] + m.TAIL
    open(os.path.join(d, 'SCG63EA.INI'), 'w', encoding='latin-1', newline='').write('\r\n'.join(text))
    open(os.path.join(d, 'SCG63EA.BIN'), 'wb').write(b)
    return x + fw / 2.0, y + fh / 2.0 + 0.5


def script(d):
    os.makedirs(d, exist_ok=True)
    sd = os.path.abspath(d)
    deploy = ['tick 5', 'look MCV:@me', 'tick 1', 'lclickobj MCV:@me', 'actclickobj MCV:@me']
    # (the player's yard goes up on the tick its MCV unfolds: engine frame 29 in the seeded
    # Tiberian Dawn skirmish, 28 in Red Alert's, as the dumps' start fields say; the check
    # asserts each shot's moment against them)
    td = shots('td', deploy + ['tick 23'])
    ra = shots('ra', deploy + ['tick 22'])
    cx, cy = temple_mission(d)
    tm = shots('tmpl', ['tick 3', 'cam %.1f %.1f' % (cx, cy), 'dist 1', 'tick 1', 'eplcplay TMPL'])
    for name, v in (('td', td), ('ra', ra), ('tmpl', tm)):
        open(os.path.join(d, name + '.txt'), 'w').write('\n'.join(v).replace('SHOTDIR', sd) + '\n')


def kv(line):
    out = {}
    for p in line.strip().split('|')[1:]:
        if '=' in p:
            k, val = p.split('=', 1)
            out[k] = val
    return out


def crop(path, box, half=True):
    """The shot's crop round the building, halved by averaging each 2 x 2 block."""
    if not half:
        return np.asarray(Image.open(path).convert('RGB').crop(box))
    a = np.asarray(Image.open(path).convert('RGB').crop(box)).astype(np.float32)
    h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
    a = a[:h, :w]
    return ((a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]) / 4.0 + 0.5).astype(np.uint8)


def changed(a, b):
    """Pixels differing by more than 24 in a channel whose eight neighbours do too."""
    m = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2) > 24
    e = m.copy()
    e[1:, :] &= m[:-1, :]; e[:-1, :] &= m[1:, :]
    e[:, 1:] &= m[:, :-1]; e[:, :-1] &= m[:, 1:]
    e[1:, 1:] &= m[:-1, :-1]; e[:-1, :-1] &= m[1:, 1:]
    e[1:, :-1] &= m[:-1, 1:]; e[:-1, 1:] &= m[1:, :-1]
    return int(e.sum())


def scene_shots(d, scene):
    s = SCENES[scene]
    return [os.path.join(d, 'g364_%s_%d.png' % (scene, i)) for i in range(len(s['at']) + 1)]


def times(d, scene):
    """Per shot, the placement time the dump after it gave (-1 when the building was not
    placing), and the placement's start frame."""
    text = open(os.path.join(d, scene + '.log'), errors='replace').read().splitlines()
    t = TYPE[scene]
    start = None
    for l in text:
        if l.startswith('EPLCOBJ|%s|' % t) and '|mine=1|' in l and start is None:
            start = int(kv(l)['start'])
    out = {}
    cur = None
    for l in text:
        m = re.match(r'LABEL\|G364 %s (\d+)' % scene, l)
        if m:
            cur = int(m.group(1))
            out[cur] = None
            continue
        if cur is not None and l.startswith('EPLCOBJ|%s|' % t) and '|mine=1|' in l and out[cur] is None:
            k = kv(l)
            out[cur] = (float(k['t']), k.get('mode', ''))
        if l.startswith('EPLCSHAKE|'):
            cur = None
    return start, out


def ref(d, refdir):
    os.makedirs(refdir, exist_ok=True)
    for scene, s in SCENES.items():
        tiles = [crop(p, s['box'], s['half']) for p in scene_shots(d, scene)]
        Image.fromarray(np.concatenate(tiles, axis=1)).save(os.path.join(refdir, 'g364-%s.png' % scene),
                                                             optimize=True)
        print('wrote', os.path.join(refdir, 'g364-%s.png' % scene))


def check(d, refdir):
    notes = []
    for scene, s in SCENES.items():
        log = os.path.join(d, scene + '.log')
        if not os.path.isfile(log) or 'SCRIPT|begin' not in open(log, errors='replace').read():
            return '0 %s: the run did not start (%s)' % (scene, log)
        start, ts = times(d, scene)
        if start is None:
            return '0 %s: the player\'s %s placement never started (no EPLCOBJ line of it)' % (scene, TYPE[scene])
        for i, at in enumerate(s['at']):
            got = ts.get(i)
            if got is None:
                return '0 %s: shot %d has no EPLCOBJ line after it: the %s was not placing' % (scene, i, TYPE[scene])
            if abs(got[0] * 15.0 - at) > 0.5 or got[0] * 15.0 >= s['length']:
                return '0 %s: shot %d is at %.2f s into the placement, not at tick %d of %.2f' % (
                    scene, i, got[0], at, s['length'] / 15.0)
        rest = ts.get(len(s['at']))
        if rest is not None and rest[0] >= 0.0 and rest[1] != 'REST':
            return '0 %s: the rest shot is still mid-placement (%s)' % (scene, rest)
        ref_path = os.path.join(refdir, 'g364-%s.png' % scene)
        if not os.path.isfile(ref_path):
            return '0 %s: no reference %s' % (scene, ref_path)
        R = np.asarray(Image.open(ref_path).convert('RGB'))
        paths = scene_shots(d, scene)
        tw = R.shape[1] // len(paths)
        worst = (0, -1)
        for i, p in enumerate(paths):
            if not os.path.isfile(p):
                return '0 %s: shot %d was not written' % (scene, i)
            c = crop(p, s['box'], s['half'])
            if c.shape[1] != tw or c.shape[0] != R.shape[0]:
                return '0 %s: the reference is not this scene\'s size (re-cut it: gate_eplcref.py ref)' % scene
            n = changed(c, R[:, i * tw:(i + 1) * tw])
            worst = max(worst, (n, i))
        if worst[0] > CHANGED:
            last = len(s['at']) - 1
            what = 'its last frame' if worst[1] == last else ('at rest' if worst[1] == last + 1 else
                                                              'tick %d of the placement' % s['at'][worst[1]])
            return ('0 %s: the %s placement draws differently from the approved one at %s: %d changed '
                    'pixels (%d allowed). If the look changed on purpose, re-cut the reference from a binary '
                    'showing the approved look: gate_eplcref.py ref DIR ../gates' % (
                        scene, TYPE[scene], what, worst[0], CHANGED))
        notes.append('%s: %d shots to the last frame (%.2f s) as the reference, worst %d changed pixels' % (
            scene, len(paths), s['at'][-1] / 15.0, worst[0]))
    return '1 ' + '; '.join(notes)


if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == 'script':
        script(a[1])
    elif len(a) == 3 and a[0] == 'check':
        print(check(a[1], a[2]))
    elif len(a) == 3 and a[0] == 'ref':
        ref(a[1], a[2])
    else:
        print(__doc__)
        sys.exit(2)
