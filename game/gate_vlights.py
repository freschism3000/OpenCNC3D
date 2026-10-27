#!/usr/bin/env python3
"""The vehicle lamps gate's two helpers (game/gates.sh).

    python3 gate_vlights.py missions SRC DST    the private scenario copies the gate plays
    python3 gate_vlights.py check DIR           the claims, read from DIR's logs and shots

THE SCENARIOS. SRC's INI and BIN files copied to DST/lamps and DST/air, SCG90EA's INI edited:
  lamps  the two houses made allies, and beside the GDI base: a Medium Tank a cell and a
         half south of the Weapons Factory facing it, a Light Tank (its searchlight rides
         the turret), a Hum-vee, a Mammoth, a harvester, an APC (the one the script takes
         to half strength) and a buggy in the open facing every way.
  air    a GDI helipad by the Nod base, whose free Orca the script flies.
Line endings and the latin-1 text are kept.

THE CLAIMS, each printed with the numbers it rests on; the last line is OK or FAIL.
"""
import filecmp
import glob
import math
import os
import shutil
import sys

LIT_TYPES = ("APC", "ARTY", "BGGY", "BIKE", "BOAT", "FTNK", "HARV", "HTNK", "JEEP", "LST",
             "LTNK", "MCV", "MHQ", "MSAM", "MTNK", "ORCA")
DARK_TYPES = ("STNK", "HELI", "TRAN", "A10", "C17")
# the units the gate places, in INI order: (type, x, y, facing). Their engine ids follow
# the stock scenario's own units, in this order (read back from the run, not assumed).
PLACED = (("MTNK", 54, 50, 0), ("LTNK", 44, 42, 96), ("JEEP", 45, 40, 64),
          ("HTNK", 48, 42, 128), ("HARV", 51, 40, 0), ("APC", 57, 41, 192),
          ("BGGY", 56, 42, 128))


def cell(x, y):
    return y * 64 + x


def add_rows(t, section, rows):
    i = t.index('[%s]' % section)
    j = t.index('\n\n', i)
    n = len(t[i:j].split('\n')) - 1
    return t[:j] + '\n' + '\n'.join('%03d=%s' % (n + k, r) for k, r in enumerate(rows)) + t[j:]


def missions(src, dst):
    for kind in ("lamps", "air"):
        d = os.path.join(dst, kind)
        os.makedirs(d, exist_ok=True)
        for f in glob.glob(os.path.join(src, '*.INI')) + glob.glob(os.path.join(src, '*.BIN')):
            shutil.copy(f, d)
        p = os.path.join(d, 'SCG90EA.INI')
        t = open(p, 'rb').read().decode('latin-1')
        if kind == "lamps":
            t = t.replace('Allies=GoodGuy,Neutral', 'Allies=GoodGuy,Neutral,BadGuy')
            t = t.replace('Allies=BadGuy\n', 'Allies=BadGuy,GoodGuy\n')
            t = add_rows(t, 'UNITS', ['GoodGuy,%s,256,%d,%d,Guard,None' % (ty, cell(x, y), f)
                                      for ty, x, y, f in PLACED])
        else:
            t = add_rows(t, 'STRUCTURES', ['GoodGuy,HPAD,256,%d,0,None' % cell(44, 37)])
        open(p, 'wb').write(t.encode('latin-1'))
    print("missions written to", dst)


def fields(l):
    return dict(kv.split('=', 1) for kv in l.split('|') if '=' in kv)


def blocks(lines, head, item):
    """Each `head` line with the `item` lines after it, in order: [(head fields, [items])]."""
    out = []
    for l in lines:
        if l.startswith(head):
            out.append((fields(l), []))
        elif l.startswith(item) and out:
            out[-1][1].append(l)
    return out


def lamps_of(block):
    """{(key, name): fields} of one EVHCLAMP block."""
    out = {}
    for l in block[1]:
        f = fields(l)
        f['type'] = l.split('|')[1]
        out[(f['key'], f['name'])] = f
    return out


def vec(s):
    return [float(x) for x in s.split(',')]


def plan_angle(a, b):
    """Degrees between two directions seen from above."""
    x1, z1, x2, z2 = a[0], a[2], b[0], b[2]
    l1, l2 = math.hypot(x1, z1), math.hypot(x2, z2)
    if l1 < 1e-6 or l2 < 1e-6:
        return 0.0
    c = max(-1.0, min(1.0, (x1 * x2 + z1 * z2) / (l1 * l2)))
    return math.degrees(math.acos(c))


def picture(a, b):
    """(changed, brighter, reddish, darker) between shots a and b by more than eight levels,
    or None when either is missing: how many pixels changed, how many of those are brighter
    in a, how many of those took more red than green and blue, how many are darker in a."""
    try:
        from PIL import Image
        import numpy as np
        A = np.asarray(Image.open(a).convert('RGB')).astype(int)
        B = np.asarray(Image.open(b).convert('RGB')).astype(int)
    except (OSError, ImportError):
        return None
    if A.shape != B.shape:
        return None
    d = A - B
    ch = abs(d).max(-1) > 8
    up = ch & (d.sum(-1) > 0)
    red = up & (d[..., 0] > d[..., 1] + 8) & (d[..., 0] > d[..., 2] + 8)
    down = ch & (d.sum(-1) < 0)
    return int(ch.sum()), int(up.sum()), int(red.sum()), int(down.sum())


def check(where):
    bad, say = [], []

    def log(name):
        try:
            return open(os.path.join(where, 'vl_%s.log' % name), encoding='latin-1').read().splitlines()
        except OSError:
            return []

    def shot(name):
        return os.path.join(where, 'vl_%s.png' % name)
    E = log('e')
    # 1. the files carry their lamps: every type the lab gave lights.json, in a file of
    #    format 2 (version 1 has no light table), none the rest
    arts = {l.split('|')[1]: fields(l) for l in E if l.startswith('EVHCART|')}
    lit = [t for t in LIT_TYPES if arts.get(t, {}).get('fmt') == '2']
    nolamp = [t for t in LIT_TYPES if t not in lit or int(arts.get(t, {}).get('lights', '0')) <= 0]
    stray = [t for t in arts if t not in lit and int(arts[t].get('lights', '1')) != 0]
    if nolamp or stray or len(arts) < 21:
        bad.append('files(no lamps %s, lamps where none %s, %d models)' % (nolamp, stray, len(arts)))
    else:
        say.append('%d types carry their lamps (%d in all), the other %d none'
                   % (len(lit), sum(int(arts[t]['lights']) for t in lit), len(arts) - len(lit)))
    # 2. the lamp scene: lamps posed, the best in the slots, headlights casting shadows
    B = blocks(E, 'EVHCLAMP|on=', 'EVHCLAMPL|')
    if len(B) < 5:
        bad.append('dumps(%d of 5)' % len(B))
        print('FAIL ' + ' '.join(bad))
        return
    h0 = B[0][0]
    if h0.get('on') != '1' or not 10 <= int(h0['lit']) <= int(h0['slots']) or int(h0['shadowed']) < 1 \
            or int(h0['glows']) < 10:
        bad.append('lamps(%s)' % h0)
    else:
        say.append('%s lamps posed, %s lit in %s slots, %s of them casting shadows, %s lenses '
                   'glowing' % (h0['lamps'], h0['lit'], h0['slots'], h0['shadowed'], h0['glows']))
    # 3. the light only adds, warm in front and red behind: the same frame with the lamps out
    tot = [0, 0, 0, 0]
    for v in ('a', 'b'):
        p = picture(shot('e_%s_on' % v), shot('e_%s_off' % v))
        if p is None:
            bad.append('picture-%s(missing)' % v)
            continue
        tot = [x + y for x, y in zip(tot, p)]
    if tot[0] < 20000 or tot[1] < 0.98 * tot[0] or tot[2] < 200:
        bad.append('light(%d changed, %d brighter, %d redder)' % tuple(tot[:3]))
    else:
        say.append('the lamps light %d pixels of two views, %.1f%% of them brighter and none of '
                   'consequence darker, %d red behind' % (tot[0], 100.0 * tot[1] / tot[0], tot[2]))
    # 4. the headlights' shadows only take light away
    p = picture(shot('e_a_on'), shot('e_a_nosh'))
    if p is None or p[0] < 300 or p[3] < 0.98 * p[0]:
        bad.append('shadows(%s)' % (p,))
    else:
        say.append('the headlights\' shadows darken %d pixels and lighten none of consequence' % p[0])
    # 5. the Light Tank's searchlight rides its turret: with its hull lamps at rest, away from
    #    them once the turret turns to fire
    def ltnk(block):
        L = lamps_of(block)
        head = [f for (k, n), f in L.items() if f['type'] == 'LTNK' and n == 'head_l']
        srch = [f for (k, n), f in L.items() if f['type'] == 'LTNK' and n == 'searchlight'
                and any(h['key'] == f['key'] for h in head)]
        pairs = [(plan_angle(vec(s['dir']), vec(h['dir'])), s['key']) for s in srch
                 for h in head if h['key'] == s['key']]
        return dict((k, a) for a, k in pairs)
    rest, fired = ltnk(B[1]), ltnk(B[2])
    keys = [k for k in fired if k in rest and rest[k] < 10.0 and fired[k] > 20.0]
    if not keys:
        bad.append('turret(at rest %s, firing %s)' % (rest, fired))
    else:
        say.append('the Light Tank\'s searchlight follows its turret (%.0f degrees off its '
                   'headlights at rest, %.0f firing)' % (rest[keys[0]], fired[keys[0]]))
    # 6. the Hum-vee's lamps drive with it, still lit
    def lamp_at(block, key, name):
        f = lamps_of(block).get((key, name))
        return (vec(f['at']), float(f['w'])) if f else (None, 0.0)
    # (the Hum-vee ordered off is the one whose headlight moved furthest)
    jkey, moved, w1 = None, 0.0, 0.0
    for (k, n), f in lamps_of(B[1]).items():
        if f['type'] != 'JEEP' or n != 'head_l':
            continue
        a1, w = lamp_at(B[3], k, 'head_l')
        if a1 and math.dist(vec(f['at']), a1) > moved:
            jkey, moved, w1 = k, math.dist(vec(f['at']), a1), w
    if moved < 0.5 or w1 < 0.99:
        bad.append('drive(jeep %s moved %.2f weight %.2f)' % (jkey, moved, w1))
    else:
        say.append('a Hum-vee\'s headlights drive %.1f cells with it, still lit' % moved)
    # 7. a vehicle at half its strength or less has its lamps out, the others keep theirs
    hurt = [fields(l) for l in E if l.startswith('HURTUNIT|')]
    if not hurt:
        bad.append('damage(no hurtunit)')
    else:
        hk = str(1000000 + int(hurt[-1]['id']))
        s, m = map(int, hurt[-1]['str-before'].split('/'))
        before = set(k for k, n in lamps_of(B[3]))
        after = set(k for k, n in lamps_of(B[4]))
        if 2 * s > m or hk not in before or hk in after or len(after) < len(before) - 1 or not after:
            bad.append('damage(key %s str %d/%d before %d after %d)' % (hk, s, m, len(before), len(after)))
        else:
            say.append('a unit at %d of %d strength puts its lamps out, %d others keep theirs'
                       % (s, m, len(after)))
    # 8. --novehiclelights is the same frame as the lamps switched out in the run; Classic
    #    has none
    N = log('n')
    nh = [fields(l) for l in N if l.startswith('EVHCLAMP|on=')]
    same = os.path.exists(shot('n_a')) and filecmp.cmp(shot('n_a'), shot('e_a_off'), shallow=False)
    if not nh or nh[0].get('on') != '0' or nh[0].get('lit') != '0' or not same:
        bad.append('novehiclelights(%s, same frame %s)' % (nh[:1], same))
    K = log('cl')
    kh = [fields(l) for l in K if l.startswith('EVHCLAMP|on=')]
    if not kh or kh[0].get('on') != '0' or kh[0].get('lit') != '0' or kh[0].get('glows') != '0':
        bad.append('classic(%s)' % kh[:1])
    say.append('--novehiclelights draws the frame the lamps switched out draw; Classic has none')
    # 9. the air: the Orca's lamps fly with it
    A = log('air')
    AB = blocks(A, 'EVHCLAMP|on=', 'EVHCLAMPL|')
    orca = [f for (k, n), f in lamps_of(AB[-1]).items() if f['type'] == 'ORCA'] if AB else []
    hi = [vec(f['at'])[1] - float(f['ground']) for f in orca]
    if not orca or min(hi) < 0.5:
        bad.append('air(%s)' % hi)
    else:
        say.append('the Orca\'s %d lamps fly %.2f cells above the ground' % (len(orca), min(hi)))
    # 10. two runs are one picture
    for v in ('a_on', 'b_on'):
        a, b = shot('e_%s' % v), shot('e2_%s' % v)
        if not (os.path.exists(a) and os.path.exists(b) and filecmp.cmp(a, b, shallow=False)):
            bad.append('nondeterministic-' + v)
    print('\n'.join(say))
    print(('OK ' + '; '.join(say[1:3])) if not bad else 'FAIL ' + ' '.join(bad))


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'missions':
        missions(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        raise SystemExit(__doc__)
