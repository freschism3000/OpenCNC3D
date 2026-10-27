#!/usr/bin/env python3
"""G335: Red Alert 3D catches up with Tiberian Dawn's Enhanced presentation.

  gate_ra3dparity.py script DIR    write the five run scripts into DIR
  gate_ra3dparity.py check DIR     read the runs' logs and pictures; first line is
                                   "1 message" or "0 message"

The runs (all hidden, Enhanced on each mission's own 3D scene):
  zap    Allied 1 (snow): the three Rangers drive at the Tesla Coil. Every second tick a
         picture with the bolt and one with it left out (ra3dzap 0, the old drawing),
         ra3dprobe after each, tmarkdump at the end.
  tree   Allied 5 (temperate): ra3dprobe's terrain records and footdump's building boxes
         at tick 1; then the stand frames (claims 7 and 8) there and over the base in the 3D
         view without Enhanced.
  ship   Allied 7: the stand frames, a click on a gunboat, then a move order.
  dog    Soviet 2 (temperate), sound into a file: the dog runs south; the footsteps and
         the ambience beds every tick.
  tank   Soviet 6 (temperate), sound into a file: the stand frames, then a heavy tank
         drives east.

Claims:
  (1) the coil's bolt is drawn while the coil fires, and the frame with the bolt differs
      from the same frame without it by at least 150 pixels; the bolt's nearest piece
      lies one step (8 pixels straight, 8 by 8 diagonally) from the source the renderer lays it from (the crown);
  (2) every tree and the ore mine (no SHAPE_CENTER) stand with their card's centre at
      their own cells' centre: the engine cell plus half the sprite (the old centring put
      it at the cell's corner);
  (3) the gunboat is selected and takes the move;
  (4) the Rangers' tracks are drawn (quads > 0) on Red Alert's ground; the cliff
      material repair processes Allied 5's temperate templates while preserving alpha;
  (5) the dog's own steps play; Soviet 2's ambience runs the temperate bed;
  (6) the heavy tank drives with tank_drive;
  (7) every vehicle, ship, aircraft, tree and wall card stands on the ground: on the
      tree, ship and tank frames, measured by occlusion queries (ra3dcardvis), the median
      card shows at least 0.95 of its own picture and nine in ten show 0.85 or more,
      where the old placement (ra3dstand 0, cards centred on their point) showed 0.7 or
      less, about half: the lower half lay under the terrain;
  (8) in the 3D view without Enhanced, where every building on Allied 5 is a card, each
      building card's foot row lies within 3 pixels of the ground row the 2D game lays it
      on (the old placement stood it at the sprite's centre row, at least 8 pixels off);
  (9) every Red Alert building model on Allied 5 stands at its size on the plot
      (art/remaster-ra/scale.json): its drawn height above the ground and its plan (the
      model's world box, footdump's FOOTMODEL) are the approved model's (scale.json's
      _fit) times its scale, within 0.01 of a cell, a turret's plan left out (it turns);
      the approved BARR remodel uses its SHA-bound staged intact geometry instead;
      and at least five types shrunk to 0.9 or less are among them, each of which would
      miss by more than 0.05 at its approved size, so the claim can see a model drawn
      unscaled.
"""
import hashlib
import json
import math
import struct
import os
import re
import sys



def approved_barr_bounds():
    """Read the approved staged remodel's intact geometry in the loader's 1024 units/cell.

    Its geometry replaced the earlier scale-fit model. Bind the approved file before
    using its bounds: a changed or missing asset must fail instead of refreshing the oracle.
    """
    path = os.path.join('ra3d', 'ebld', 'BARR.ebld')
    blob = open(path, 'rb').read()
    if hashlib.sha256(blob).hexdigest() != 'dc20bc5e1c31ef6cb301924293b2054f69f55562152e9946764703424164a08b':
        raise ValueError('BARR does not match the approved staged model')
    offset = 0
    def take(n):
        nonlocal offset
        if n < 0 or offset + n > len(blob):
            raise ValueError('BARR geometry ends early')
        out = blob[offset:offset + n]
        offset += n
        return out
    def fields(fmt):
        return struct.unpack(fmt, take(struct.calcsize(fmt)))
    magic, version, code, ntex, nset, nemit, nparam = fields('<4sI8sIIII')
    if magic != b'EBLD' or version != 6 or code != b'BARR\0\0\0\0' or not (1 <= ntex <= 256 and 1 <= nset <= 3 and nparam <= 2048):
        raise ValueError('BARR header differs from the checked version-6 contract')
    take(nparam * 4)
    for _ in range(ntex):
        take(12)                       # checked version-6 texture descriptor
        compressed, = fields('<I')
        take(compressed)
    pieces, triangles = fields('<II')
    if not (1 <= pieces <= 10000 and 1 <= triangles <= 1000000):
        raise ValueError('BARR intact geometry counts are invalid')
    for _ in range(pieces):
        piece = fields('<32shBB3f3ffIIB3x')
        take(piece[13] * 12)
    low, high = [math.inf] * 3, [-math.inf] * 3
    for _ in range(triangles):
        take(12)
        for _ in range(3):
            xyz = fields('<3f')
            if not all(math.isfinite(v) for v in xyz):
                raise ValueError('BARR contains a non-finite coordinate')
            for axis, value in enumerate(xyz):
                low[axis] = min(low[axis], value)
                high[axis] = max(high[axis], value)
            take(32)                    # UVs, colour and normal, unchanged by the loader
    return [high[1] / 1024.0, (high[0] - low[0]) / 1024.0, (high[2] - low[2]) / 1024.0]


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def script(d):
    os.makedirs(d, exist_ok=True)
    z = ['tick 20', 'cam 69 57', 'dist 2700', 'lclickobj JEEP 0', 'order 69 55',
         'lclickobj JEEP 1', 'order 70 55', 'tick 30']
    for i in range(50):
        z += ['tick 2', f'shot {d}/zap_a{i:02d}.png', 'ra3dprobe', 'ra3dzap 0',
              f'shot {d}/zap_b{i:02d}.png', 'ra3dprobe', 'ra3dzap 1']
    z += ['tmarkdump', 'quit']
    write(f'{d}/zap.txt', z)
    # (7) and (8): the same frame drawn with the old placement and the new one, the
    # cards' share of themselves on screen measured, and in the 3D view without Enhanced
    # (where every building is a card) each building card's foot against its 2D row
    def stand(tag, n64=False):
        out = []
        for v in ('0', '1'):
            out += [f'ra3dstand {v}', 'ra3dcardvis', f'shot {d}/stand_{tag}{v}.png', 'ra3dprobe']
            if n64:
                out += ['footdump']
        return out
    # ((9): the building boxes come before the first ra3dstand, so claim 8 reads none of
    # their card lines)
    write(f'{d}/tree.txt', ['tick 1', 'reveal 1', 'dist 3000', 'cam 46 84', f'shot {d}/tree.png',
                            'ra3dprobe', 'footdump'] + stand('tree') +
                           ['cam 45 50', 'gfx enabled 0', 'tick 1'] + stand('base', True) + ['quit'])
    write(f'{d}/ship.txt', ['tick 3', 'cam 15 53', 'dist 2400'] + stand('ship') + ['lclickobj PT 0',
                            'expectsel 1', 'seldump', 'order 18 48', 'tick 30', 'seldump',
                            f'shot {d}/ship.png', 'quit'])
    dog = ['tick 10', 'look DOG 0', 'lclickobj DOG 0', 'order 77 64']
    for _ in range(60):
        dog += ['tick 1', 'look DOG 0', 'infantryaudio', 'ambience']
    write(f'{d}/dog.txt', dog + ['seldump', 'quit'])
    tank = ['tick 10', 'look 3TNK 0'] + stand('tank') + ['lclickobj 3TNK 0', 'order 64 47']
    for _ in range(40):
        tank += ['tick 1', 'look 3TNK 0', 'vehicleaudio']
    write(f'{d}/tank.txt', tank + ['seldump', 'quit'])


def read(path):
    try:
        return open(path, errors='replace').read().splitlines()
    except OSError:
        return []


def check(d):
    from PIL import Image, ImageChops
    fails, notes = [], []
    # (1) the bolt
    z = read(f'{d}/zap.log')
    probes = [l for l in z if l.startswith('RA3DPROBE|frame|')]
    hit = None
    for i in range(0, len(probes) - 1, 2):
        on = int(re.search(r'\|zap=(\d+)', probes[i]).group(1))
        off = int(re.search(r'\|zap=(\d+)', probes[i + 1]).group(1))
        if on > 0 and off == 0:
            hit = (i // 2, on)
            break
    if hit is None:
        fails.append('the Tesla Coil fired no bolt that was drawn in 100 ticks')
    else:
        a = Image.open(f'{d}/zap_a{hit[0]:02d}.png').convert('RGB')
        b = Image.open(f'{d}/zap_b{hit[0]:02d}.png').convert('RGB')
        diff = ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v > 24 else 0)
        px = diff.histogram()[255]
        if px < 150:
            fails.append(f'the bolt ({hit[1]} pieces) changed only {px} pixels')
        bolts = [l for l in z if l.startswith('RA3DPROBE|bolt|')]
        near = [float(re.search(r'\|near=([0-9.]+)', l).group(1)) for l in bolts]
        # one step of the walk is 8 pixels straight or 8 by 8 diagonally: 1/3 to 0.47 cell
        if not near or any(n < 0.30 or n > 0.50 for n in near):
            fails.append(f'the bolt does not start one step from the coil\'s source: near={near}')
        notes.append(f'bolt of {hit[1]} pieces, {px} px against the old drawing')
    # (4a) tracks
    tm = [l for l in z if l.startswith('TMARK|on=')]
    quads = int(re.search(r'\|quads=(\d+)', tm[-1]).group(1)) if tm else 0
    if quads <= 0:
        fails.append('no tread mark was drawn behind the Rangers')
    else:
        notes.append(f'{quads} track quads')
    # (2) the trees
    t = read(f'{d}/tree.log')
    recs = [l for l in t if l.startswith('RA3DPROBE|terrain|')]
    bad = 0
    for l in recs:
        m = re.search(r'cell=(\d+),(\d+)\|flags=(\d+)\|size=(\d+)x(\d+)\|topleft=(\d)\|centre=([0-9.]+),([0-9.]+)', l)
        cx, cy, fl, w, h, tl, x, y = m.groups()
        if int(fl) & 0x20:
            continue
        want = (int(cx) + int(w) / 48.0, int(cy) + int(h) / 48.0)
        if tl != '1' or abs(float(x) - want[0]) > 0.05 or abs(float(y) - want[1]) > 0.05:
            bad += 1
    if len(recs) < 10:
        fails.append(f'only {len(recs)} terrain objects reached the 3D sprite pass')
    elif bad:
        fails.append(f'{bad} of {len(recs)} trees do not stand on their own cells')
    else:
        notes.append(f'{len(recs)} trees on their cells')
    material = [l for l in t if l.startswith('RA3D|cliff-material|theater=T|')]
    repaired = any(re.search(r'\|templates=([1-9]\d*)\|repaired-rgb=([1-9]\d*)'
                             r'\|alpha-unchanged=1\|whole-template=1$', l) for l in material)
    if not repaired:
        fails.append('Allied 5 has no positive temperate cliff-material repair with alpha preserved')
    # (3) the gunboat
    s = read(f'{d}/ship.log')
    if not any('EXPECTSEL|want=1|have=1|PASS' in l for l in s):
        fails.append('a click on the gunboat selected nothing')
    elif not any(l.startswith('SEL|VESSEL|PT|') and 'mission=Move' in l for l in s):
        fails.append('the selected gunboat did not take the move')
    else:
        notes.append('gunboat selected and moving')
    # (5) the dog and the bed
    g = read(f'{d}/dog.log')
    dog_id = None
    for l in g:
        m = re.match(r'SEL\|INFANTRY\|DOG\|id=(\d+)\|', l)
        if m:
            dog_id = m.group(1)
    steps = sum(1 for l in g if dog_id and l.startswith(f'INFANTRYAUDIO|voice|id={dog_id}|'))
    if not steps:
        fails.append('the running dog made no step')
    bed = [l for l in g if l.startswith('AMBIENCE|loaded|temperate_')]
    if not bed:
        fails.append('Soviet 2 (RATEMP) ran no temperate ambience bed')
    if steps and bed:
        notes.append('dog steps and the temperate bed heard')
    # (6) the heavy tank
    k = read(f'{d}/tank.log')
    tank_id = None
    for l in k:
        m = re.match(r'SEL\|UNIT\|3TNK\|id=(\d+)\|', l)
        if m:
            tank_id = m.group(1)
    if not tank_id or not any(l.startswith(f'VEHICLEAUDIO|emitter|id={tank_id}|clip=tank_drive|')
                              for l in k):
        fails.append('the heavy tank drove without its tank_drive sound')
    # (7) every card stands on the ground: the share of its own picture on screen
    shares = {'0': [], '1': []}
    for log in ('tree', 'ship', 'tank'):
        stand = None
        for l in read(f'{d}/{log}.log'):
            if l.startswith('RA3DSTAND|'):
                stand = l.split('|')[1]
            m = re.match(r'RA3DPROBE\|vis\|[^|]*\|type=(-?\d+)\|full=(\d+)\|seen=(\d+)\|', l)
            # the objects' own pictures and the walls; buildings are claim (8)'s
            if m and stand in shares and int(m.group(1)) != 4 and int(m.group(2)) >= 30:
                shares[stand].append(int(m.group(3)) / int(m.group(2)))
    new, old = sorted(shares['1']), sorted(shares['0'])
    if len(new) < 10 or len(old) < 10:
        fails.append(f'too few cards measured for the stand ({len(new)} new, {len(old)} old)')
    else:
        med_new, med_old = new[len(new) // 2], old[len(old) // 2]
        good = sum(1 for s in new if s >= 0.85) / len(new)
        if med_new < 0.95 or good < 0.9:
            fails.append(f'the cards do not stand on the ground: median share {med_new:.2f}, '
                         f'{good:.0%} of {len(new)} at 0.85 or more')
        elif med_old > 0.7:
            fails.append(f'the old placement (ra3dstand 0) showed {med_old:.2f} of its cards, '
                         f'so the measure cannot see a card sunk into the ground')
        else:
            notes.append(f'{len(new)} cards stand ({med_new:.2f} of each on screen, '
                         f'{med_old:.2f} the old way)')
    # (8) a building card's foot lies on its own 2D row
    offs = {'0': [], '1': []}
    stand = None
    for l in read(f'{d}/tree.log'):
        if l.startswith('RA3DSTAND|'):
            stand = l.split('|')[1]
        m = re.search(r'\|footscr=(-?[0-9.]+),(-?[0-9.]+)\|ground2d=(-?[0-9.]+),(-?[0-9.]+)', l)
        if l.startswith('FOOTCARD|') and m and stand in offs:
            c0, r0, c1, r1 = (float(v) for v in m.groups())
            if 0 <= c1 < 1040 and 0 <= r1 < 720:
                offs[stand].append(max(abs(c0 - c1), abs(r0 - r1)))
    new, old = sorted(offs['1']), sorted(offs['0'])
    if len(new) < 3:
        fails.append(f'only {len(new)} building cards were on screen for the foot test')
    elif max(new) > 3.0:
        fails.append(f'a building card\'s foot lies {max(new):.1f} px from its own 2D row')
    elif old[len(old) // 2] < 8.0:
        fails.append(f'the old placement put the feet {old[len(old) // 2]:.1f} px off, '
                     f'too close to tell the two apart')
    else:
        notes.append(f'{len(new)} building cards on their own rows (within {max(new):.1f} px; '
                     f'the old way {old[len(old) // 2]:.0f} px off)')
    # (9) every Red Alert building model at its size on the plot
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        sc = json.load(open(os.path.join(here, '..', 'art', 'remaster-ra', 'scale.json')))
    except (OSError, ValueError):
        sc = None
    if not sc:
        fails.append('art/remaster-ra/scale.json could not be read')
    else:
        try:
            barr_bounds = approved_barr_bounds()
        except (OSError, ValueError, struct.error) as error:
            barr_bounds = None
            fails.append('the approved BARR bounds could not be verified: ' + str(error))
        decoy = {'FACF': 'FACT', 'WEAF': 'WEAP', 'DOMF': 'DOME', 'SYRF': 'SYRD', 'SPEF': 'SPEN'}
        turret = {'SAM', 'AGUN', 'GUN'}
        seen, off, shrunk = set(), [], set()
        stand = None
        for l in read(f'{d}/tree.log'):
            if l.startswith('RA3DSTAND|'):
                stand = l
            m = re.match(r'FOOTMODEL\|(\w+)\|\d+\|mesh=\d+\|box=[^|]*\|top=(-?[0-9.]+)\|'
                         r'wide=([0-9.]+)\|deep=([0-9.]+)', l)
            if not m or stand is not None:
                continue
            t = decoy.get(m.group(1), m.group(1))
            f = sc['_fit'].get(t)
            if not f:
                continue                       # (a reused OpenCNC3D model: its own size)
            s, mod = float(sc[t]), f['model']
            want = [mod['height'] * s] + ([] if t in turret else [b * s for b in mod['box']])
            if t == 'BARR' and barr_bounds is not None:
                want = barr_bounds
            got = [float(m.group(2))] + ([] if t in turret else
                                         [float(m.group(3)), float(m.group(4))])
            seen.add(t)
            if any(abs(g - w) > 0.01 for g, w in zip(got, want)):
                off.append(f'{t} {got} against {[round(w, 3) for w in want]} at {s}')
            if s <= 0.9 and mod['height'] * (1 - s) > 0.05:
                shrunk.add(t)
        if len(seen) < 6:
            fails.append(f'only {len(seen)} Red Alert building types drew a model on Allied 5')
        elif off:
            fails.append('a building is not drawn at its size on the plot: ' + '; '.join(off[:4]))
        elif len(shrunk) < 5:
            fails.append(f'only {len(shrunk)} shrunk types on Allied 5, too few to see a model '
                         f'drawn at its approved size')
        else:
            notes.append(f'{len(seen)} building types at their scale.json size '
                         f'({len(shrunk)} shrunk: {" ".join(sorted(shrunk))})')
    if fails:
        print('0 ' + '; '.join(fails))
    else:
        print('1 ' + ', '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        sys.exit(__doc__)
    (script if sys.argv[1] == 'script' else check)(sys.argv[2])
