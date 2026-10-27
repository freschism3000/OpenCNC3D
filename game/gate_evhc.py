#!/usr/bin/env python3
"""G296's two helpers (game/gates.sh).

    python3 gate_evhc.py missions SRC DST    the private scenario copies the gate plays
    python3 gate_evhc.py check               the claims, read from the runs' logs and shots

THE SCENARIOS. SRC's INI and BIN files copied to DST/all and DST/air, SCG90EA's INI edited:
  all  every remastered ground vehicle and boat placed twice, GDI and Nod side by side in
       two rows in the middle of the map, a hovercraft and a Nod gun boat on the water in
       the south, three GDI riflemen beside the APC, and the two houses made allies so
       nothing fights unless the script says so.
  air  a GDI helipad by the Nod base, whose free Orca the script flies in the SAM site's
       reach and brings down in flight.
Line endings and the latin-1 text are kept.

THE CLAIMS, each printed with the numbers it rests on; the last line is OK or FAIL.
"""
import filecmp
import glob
import os
import re
import shutil
import sys

TYPES = ("HTNK", "MTNK", "LTNK", "FTNK", "STNK", "APC", "ARTY", "MSAM", "HARV", "JEEP",
         "BGGY", "BIKE", "MCV", "MHQ", "BOAT", "LST", "HELI", "ORCA", "TRAN", "A10", "C17")
GROUND = ("HTNK", "MTNK", "LTNK", "FTNK", "STNK", "APC", "ARTY", "MSAM", "HARV", "JEEP",
          "BGGY", "BIKE", "MCV", "MHQ", "BOAT", "LST")
ROW = ("HTNK", "MTNK", "LTNK", "FTNK", "STNK", "APC", "ARTY", "MSAM", "JEEP", "BGGY", "BIKE",
       "MCV", "MHQ", "HARV")


def cell(x, y):
    return y * 64 + x


def add_rows(t, section, rows):
    i = t.index('[%s]' % section)
    j = t.index('\n\n', i)
    n = len(t[i:j].split('\n')) - 1
    return t[:j] + '\n' + '\n'.join('%03d=%s' % (n + k, r) for k, r in enumerate(rows)) + t[j:]


def missions(src, dst):
    for kind in ("all", "air"):
        d = os.path.join(dst, kind)
        os.makedirs(d, exist_ok=True)
        for f in glob.glob(os.path.join(src, '*.INI')) + glob.glob(os.path.join(src, '*.BIN')):
            shutil.copy(f, d)
        p = os.path.join(d, 'SCG90EA.INI')
        t = open(p, 'rb').read().decode('latin-1')
        if kind == "all":
            t = t.replace('Allies=GoodGuy,Neutral', 'Allies=GoodGuy,Neutral,BadGuy')
            t = t.replace('Allies=BadGuy\n', 'Allies=BadGuy,GoodGuy\n')
            rows = []
            for i, ty in enumerate(ROW):
                x, y = 39 + (i % 7) * 3, 40 + (i // 7) * 3
                rows.append('GoodGuy,%s,256,%d,96,Guard,None' % (ty, cell(x, y)))
                rows.append('BadGuy,%s,256,%d,160,Guard,None' % (ty, cell(x + 1, y + 1)))
            rows.append('GoodGuy,LST,256,%d,64,Guard,None' % cell(49, 59))
            rows.append('BadGuy,BOAT,256,%d,192,Guard,None' % cell(45, 60))
            t = add_rows(t, 'UNITS', rows)
            t = add_rows(t, 'INFANTRY', ['GoodGuy,E1,256,%d,%d,Guard,0,None' % (cell(53, 39), s)
                                         for s in (1, 2, 3)])
        else:
            t = add_rows(t, 'STRUCTURES', ['GoodGuy,HPAD,256,%d,0,None' % cell(44, 37)])
        open(p, 'wb').write(t.encode('latin-1'))
    print("missions written to", dst)


def log(name):
    try:
        return open('shots/g296_%s.log' % name, encoding='latin-1').read().splitlines()
    except OSError:
        return []


def dumps(lines, prefix):
    """The lines of each evhcdump in order: a list of lists."""
    out, cur = [], None
    for l in lines:
        if l.startswith('EVHC|loaded='):
            cur = []
            out.append(cur)
        if cur is not None and l.startswith(prefix):
            cur.append(l)
    return out


def fields(l):
    return dict(kv.split('=', 1) for kv in l.split('|') if '=' in kv)


def screen_points(lines):
    """Each evhcscreen block's points, in order: a list of [(col, row)]."""
    out, cur = [], None
    for l in lines:
        if l.startswith('EVHCSCR|'):
            if cur is None:
                cur = []
                out.append(cur)
            x, y = map(float, fields(l)['scr'].split(','))
            cur.append((x, y))
        else:
            cur = None
    return out


def changed(a, b):
    """The pixels of two shots that differ by more than eight levels: (cols, rows), or
    None when either picture is missing."""
    try:
        from PIL import Image
        import numpy as np
        A = np.asarray(Image.open(a).convert('RGB')).astype(int)
        B = np.asarray(Image.open(b).convert('RGB')).astype(int)
    except (OSError, ImportError):
        return None
    if A.shape != B.shape:
        return None
    d = abs(A - B).max(-1)
    rows, cols = (d > 8).nonzero()
    return cols, rows, A.shape


def normals(A, first, bad):
    """Claim 12, from run a (and n, cl): the smooth normals."""
    # every one of the 21 files is format 2 and carries the export's smooth normals (the
    # engine still reads a version 1 file, its corners on their faces' normals, but none
    # is shipped)
    v2 = [ty for ty in TYPES if first.get(ty, {}).get('fmt') == '2']
    v1 = [ty for ty in TYPES if first.get(ty, {}).get('fmt') == '1']
    fmt = [ty for ty in v2 if int(first.get(ty, {}).get('smooth', '0')) <= 0]
    if fmt or len(v2) != len(TYPES):
        bad.append('normals-format(flat %s, %d of format 2, %d of 1)' % (','.join(fmt), len(v2),
                                                                         len(v1)))
    nl = [fields(l) for l in A if l.startswith('EVHCNRM|on=')]
    # the first dump: every vehicle in view drawn there; the second, switched off: none;
    # the dump after the death: the wreck too
    objs = int(nl[0]['objects']) if nl else 0
    off = nl[1] if len(nl) > 1 else {}
    wrecks = max([int(f['wrecks']) for f in nl[2:]] or [0])
    if objs < 16 or off.get('on') != '0' or off.get('objects') != '0' or wrecks < 1:
        bad.append('normals-drawn(objects %d, off %s, wrecks %d)' % (objs, off, wrecks))
    pts = screen_points(A)
    c = changed('shots/g296_a_1.png', 'shots/g296_a_1n.png')
    far = n = 0
    if c is None or not pts:
        bad.append('normals-picture(missing)')
    else:
        cols, rows, shape = c
        n = len(cols)
        P = [(x, y) for x, y in pts[0] if 0 <= x < shape[1] and 0 <= y < shape[0]]
        for x, y in zip(cols.tolist(), rows.tolist()):
            if not P or min((x - px) ** 2 + (y - py) ** 2 for px, py in P) > 60 * 60:
                far += 1
        if n < 1500 or far:
            bad.append('normals-picture(%d px changed, %d of them far from a vehicle)' % (n, far))
    # switched off and in Classic, the switch has nothing to take away
    for name in ('n', 'cl'):
        cc = changed('shots/g296_%s_1.png' % name, 'shots/g296_%s_1n.png' % name)
        if cc is None or len(cc[0]):
            bad.append('normals-%s(%s)' % (name, 'missing' if cc is None else len(cc[0])))
        L = log(name)
        if any(fields(l).get('objects') != '0' for l in L if l.startswith('EVHCNRM|on=')):
            bad.append('normals-%s-drawn' % name)
    return ('the smooth normals: all %d files format 2, each with them, %d vehicles and the '
            'wreck drawn into the light pass\'s normal target, %d pixels change without them, '
            'all on the vehicles' % (len(v2), objs, n))


def ease01(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3.0 - 2.0 * x)


# THE HARVESTER'S CLAWS, as the approved round 7 declares them: raised 40 degrees over the
# first 60% of a 0.8 s change, the mouth's lower jaw open 56.7 degrees, shut over the last 60%
LIFT, JAW, SHARE = 40.0, 56.7, 0.6


def claws(C, bad):
    """13. The Harvester's claws in run c: lowered with the lower jaw open while it
    harvests, raised with the jaw shut otherwise, caught part way through a change; at
    every drawn phase the lift and the jaw stand where the schedule puts them; the phase
    moves only toward what the harvest test says; the rams' rods keep their eyes on the
    booms' pins; the hub caps roll; docked, the rig draws the claws raised; and the same
    frame with the claws switched off (harvlift 0) is the model at rest, differing only
    round the harvesters."""
    H = [fields(l) for l in C if l.startswith('EVHCHARV|HARV|')]
    on = [f for f in H if f.get('on') == '1']
    if not on:
        bad.append('claws(no EVHCHARV line: the renderer drives no claws)')
        return None
    off = False
    worst, gap, order = 0.0, 0.0, 0
    lowered = raised = mid = 0
    rolls = {}
    last = {}
    for f in H:
        c = float(f['drawnclaw'])
        if f['on'] == '0':
            off = off or (c < 0 and float(f['drawnlift']) == 0.0 and float(f['drawnjaw']) == 0.0)
            continue
        lift, jaw = float(f['drawnlift']), float(f['drawnjaw'])
        worst = max(worst, abs(lift - LIFT * ease01(c / SHARE)),
                    abs(jaw - JAW * (1.0 - ease01((c - (1.0 - SHARE)) / SHARE))))
        gap = max(gap, float(f['ramgap']))
        if c == 0.0 and lift == 0.0 and jaw == JAW:
            lowered += 1
        elif c == 1.0 and lift == LIFT and jaw == 0.0:
            raised += 1
        elif 0.0 < c < 1.0:
            mid += 1
        rolls.setdefault(f['key'], []).append(float(f['roll']))
        k, t, hv = f['key'], float(f['claw']), f['harvesting']
        if k in last and last[k][1] == hv:
            if (hv == '1' and t > last[k][0] + 1e-6) or (hv == '0' and t < last[k][0] - 1e-6):
                order += 1
        last[k] = (t, hv)
    roll = max((max(v) - min(v) for v in rolls.values()), default=0.0)
    docked = [fields(l) for l in C if l.startswith('EVHCART|HARV|')
              and int(fields(l)['rigdrawn']) > 0]
    rig_ok = bool(docked) and all(float(f['riglift']) == LIFT for f in docked)
    ab = changed('shots/g296_c_lift.png', 'shots/g296_c_rest.png')
    npx = len(ab[0]) if ab else 0
    if not (lowered and raised and mid) or worst > 0.15 or order or gap > 0.5 or roll < 1.0 \
            or not rig_ok or not off or not 100 <= npx <= 20000:
        bad.append('claws(lowered %d raised %d mid-change %d, off the schedule by %.2f degrees, '
                   '%d steps against the harvest test, ram gap %.3f, cap roll %.2f, docked %s, '
                   'switched off at rest %s, A/B pixels %d)'
                   % (lowered, raised, mid, worst, order, gap, roll,
                      [f['riglift'] for f in docked][:3] or 'never', off, npx))
        return None
    return ('the Harvester lowers its claws with the lower jaw open to harvest (%d draws) and '
            'raises them with the jaw shut otherwise (%d), %d draws caught part way, every '
            'one within %.2f degrees of the schedule and only ever moving toward what the '
            'harvest test says; the rams\' rods stay on the booms\' pins (%.3f pack units); '
            'the hub caps roll %.1f radians; docked, the claws are raised; switched off, the '
            'same frame is the model at rest (%d pixels)'
            % (lowered, raised, mid, worst, gap, roll, npx))


def hoop(C, bad):
    """14. The Harvester's hoop in run c, when the Harvester file declares one (the hoop
    swing's hinge): docked, it is drawn over the tank on arrival, swung over the cab with the
    tank still at its rest, and the tank leaves its rest only with the hoop fully over (never
    before); undocking, the tank is down before the hoop swings back over it. A file without
    a hoop is said so, not passed silently; a renderer without the hoop driver prints no
    hoop field and fails."""
    D = [fields(l) for l in C if l.startswith('EVHCART|HARV|')]
    if not D or 'hoop' not in D[0]:
        bad.append('hoop(no hoop field on EVHCART|HARV: the renderer has no hoop driver)')
        return None
    if all(f['hoop'] == '0' for f in D):
        return ('the Harvester file declares no hoop swing, so the dock plays the cartridge\'s '
                'rig unchanged (check 14 has nothing to measure until a model with one is '
                'installed)')
    docked = [f for f in D if int(f['rigdrawn']) > 0]
    seq = [(float(f['righoop']), float(f['rigtank'])) for f in docked]
    nbad = sum(int(f['hoopbad']) for f in docked)
    nfull = sum(int(f['hoopfull']) for f in docked)
    arrived = [i for i, (h, t) in enumerate(seq) if h == 0.0 and t == 0.0]
    waiting = [i for i, (h, t) in enumerate(seq) if h == 1.0 and t == 0.0]
    tipping = [i for i, (h, t) in enumerate(seq) if t > 0.0]
    # in order: arrived, swung with the tank waiting, tipping; then down with the hoop still
    # over, and the hoop back
    went = bool(arrived and waiting and tipping) and arrived[0] < waiting[0] < tipping[0]
    back = bool(tipping) and any(i > tipping[-1] for i in waiting) \
        and any(i > tipping[-1] for i in arrived)
    if nbad or not nfull or not went or not back:
        bad.append('hoop(docked draws %d: the tank off its rest before the hoop was over %d times, '
                   'with it over %d; arrived-swung-tipping in order %s; down then the hoop back %s; '
                   'the sequence %s)' % (len(docked), nbad, nfull, went, back, seq[:24]))
        return None
    return ('the Harvester\'s hoop swings over the cab before the dock rig tips the tank (%d draws '
            'with the tank off its rest, every one with the hoop fully over) and back over the '
            'tank once it is down' % nfull)


def check():
    bad, say = [], []
    A = log('a')
    head = [l for l in A if l.startswith('EVHC|loaded=')]
    # 1. every model loads and attaches, the switch and the picture on
    if not head or head[0] != 'EVHC|loaded=21|attached=21|on=1|enhanced=1':
        bad.append('load(%s)' % (head[0] if head else '-'))
    arts = dumps(A, 'EVHCART|')
    if not arts:
        bad.append('no-dump')
        arts = [[]]
    first = {l.split('|')[1]: fields(l) for l in arts[0]}
    far = []
    for ty in TYPES:
        f = first.get(ty)
        if not f:
            bad.append('noart-' + ty)
            continue
        x0, x1 = map(float, f['x'].split(',')); cx0, cx1 = map(float, f['cartx'].split(','))
        z0, z1 = map(float, f['z'].split(',')); cz0, cz1 = map(float, f['cartz'].split(','))
        off = max(abs(x0 - cx0), abs(x1 - cx1), abs(z0 - cz0), abs(z1 - cz1))
        if off > 164.0:             # 0.16 of a cell: the lab keeps the cartridge's plan
            far.append('%s:%.0f' % (ty, off))
        if ty in GROUND and int(f['drawn']) <= 0:
            bad.append('notdrawn-' + ty)
        if int(f['wreck']) <= 0:
            bad.append('nowreck-' + ty)
    if far:
        bad.append('plan(' + ','.join(far) + ')')
    say.append('21 models load and attach, the 16 placed draw, every plan within 0.16 cells of '
               'the cartridge model it stands in for')
    objs = [l for l in A if l.startswith('EVHCOBJ|')]

    def best(ty, key):
        vals = [fields(l) for l in objs if l.split('|')[1] == ty]
        return vals
    # 2. a turret turns: the GDI Mammoth force-fired north of it
    tur = [int(f['tur']) for f in best('HTNK', 'tur') if f['key'] == '1000013']
    if not any(t != 0 for t in tur):
        bad.append('turret(%s)' % tur)
    # 3. tracks: a Medium Tank's two sides travel and its wheel flipbook steps
    mt = [f for f in best('MTNK', 'trav') if f['key'] == '1000015']
    tv = [tuple(map(float, f['trav'].split(','))) for f in mt]
    fr = set(f['frame'] for f in mt)
    if not tv or min(abs(tv[-1][0]), abs(tv[-1][1])) < 0.3 or len(fr) < 2:
        bad.append('tracks(%s,%s)' % (tv[-1] if tv else '-', sorted(fr)))
    # 4. wheels: the Hum-vee drives, rolls and steers
    je = [f for f in best('JEEP', 'trav') if f['key'] == '1000029']
    jt = [tuple(map(float, f['trav'].split(','))) for f in je]
    steer = max([abs(float(f['turn'])) for f in je] or [0.0])
    if not jt or min(abs(jt[-1][0]), abs(jt[-1][1])) < 0.3 or steer < 0.02:
        bad.append('wheels(%s,steer %.3f)' % (jt[-1] if jt else '-', steer))
    # 5. recoil: the Mammoth's shots kick both barrels in turn
    fires = [fields(l) for l in A if l.startswith('EVHC|fire|') and '|type=HTNK|' in l]
    barrels = set(f['barrel'] for f in fires if f['key'] == '1000013')
    kick = max([float(f['kick']) for f in best('HTNK', 'kick') if f['key'] == '1000013'] or [0.0])
    if barrels != {'0', '1'} or kick <= 0.0:
        bad.append('recoil(%s,%.3f)' % (sorted(barrels), kick))
    # 6. the APC's rear door opens for the riflemen
    door = max([float(f['door']) for f in best('APC', 'door') if f['key'] == '1000023'] or [0.0])
    if door < 0.5:
        bad.append('door(%.2f)' % door)
    # 7. a death: the Nod Mammoth breaks into its wreck, the old shatter does not spawn,
    #    and the wreck sinks and goes
    dth = [fields(l) for l in A if l.startswith('EVHC|death|') and '|type=HTNK|' in l]
    wreckn = first.get('HTNK', {}).get('wreck')
    sink = [l for l in A if l.startswith('EVHC|sink|') and '|type=HTNK|' in l]
    gone = [l for l in A if l.startswith('EVHC|gone|') and '|type=HTNK|' in l]
    old = [l for l in A if l.startswith('SHATTERUNIT|HTNK|')]
    if len(dth) != 1 or dth[0]['air'] != '0' or dth[0]['pieces'] != wreckn or not sink or not gone \
            or old or fields(sink[0]).get('wet') != '0':
        bad.append('death(%d deaths, pieces %s of %s, sink %d, gone %d, old %d)'
                   % (len(dth), dth[0]['pieces'] if dth else '-', wreckn, len(sink), len(gone),
                      len(old)))
    say.append('a turret turns (%s), a Medium Tank\'s tracks travel %s with frames %d, a Hum-vee '
               'rolls %s and steers %.2f, the Mammoth kicks barrels %s (%.2f), the APC door '
               'opens %.2f, a Mammoth breaks into %s pieces that sink and go with no old shatter'
               % (sorted(set(tur)), tv[-1] if tv else '-', len(fr), jt[-1] if jt else '-', steer,
                  sorted(barrels), kick, door, wreckn))
    # 8. the air: the Orca's rotors idle on the pad and spin up, the disc comes up in the air
    #    and the ducts tilt; shot down at flight level it falls whole and breaks on landing
    B = log('b')
    orca = [fields(l) for l in B if l.startswith('EVHCOBJ|ORCA|')]
    disc0 = [float(f['disc']) for f in orca[:1]]
    discs = [float(f['disc']) for f in orca]
    ducts = [float(f['duct']) for f in orca]
    rot = [float(f['rotor']) for f in orca]
    bd = [fields(l) for l in B if l.startswith('EVHC|death|') and '|type=ORCA|' in l]
    imp = [fields(l) for l in B if l.startswith('EVHC|impact|') and '|type=ORCA|' in l]
    ok_air = (disc0 and disc0[0] == 0.0 and max(discs or [0]) >= 0.9 and max(ducts or [0]) > 5.0
              and len(rot) > 2 and rot[-1] > rot[0] and len(bd) == 1 and bd[0]['air'] == '1'
              and float(bd[0]['lift']) > 0.5 and len(imp) == 1
              and 3 <= int(imp[0]['frame']) - int(bd[0]['frame']) <= 40)
    if not ok_air:
        bad.append('air(disc %s..%.2f duct %.1f rotor %s deaths %d impacts %d)'
                   % (disc0, max(discs or [0]), max(ducts or [0]), rot[:1] + rot[-1:], len(bd),
                      len(imp)))
    else:
        say.append('the Orca idles, spins up (disc %.2f, ducts %.0f degrees), is shot down at '
                   'lift %s and lands broken %d ticks later'
                   % (max(discs), max(ducts), bd[0]['lift'],
                      int(imp[0]['frame']) - int(bd[0]['frame'])))
    # 8b. water: a gunboat killed on the sea goes down within a second of its death, and
    #     its wreck knows it is on water (the Mammoth on land above sank dry)
    Wl = log('w')
    wd = [fields(l) for l in Wl if l.startswith('EVHC|death|') and '|type=BOAT|' in l]
    ws = [fields(l) for l in Wl if l.startswith('EVHC|sink|') and '|type=BOAT|' in l]
    wg = [l for l in Wl if l.startswith('EVHC|gone|') and '|type=BOAT|' in l]
    if len(wd) != 1 or len(ws) != 1 or ws[0].get('wet') != '1' or not wg \
            or not 1 <= int(ws[0]['frame']) - int(wd[0]['frame']) <= 15:
        bad.append('water(deaths %d sinks %s gone %d)' % (len(wd), ws[:1], len(wg)))
    else:
        say.append('a gunboat sunk on the sea goes down %d ticks after it dies'
                   % (int(ws[0]['frame']) - int(wd[0]['frame'])))
    # 9. the rigs: the MCV unfolds, the harvester empties in the dock. Under Enhanced the
    #    remastered Construction Yard's placement (ebld_place.h) IS the deploy: it draws the
    #    remastered MCV as its companion and unfolds the yard out of it, and the cartridge's
    #    deploy rig stands down for it (mcvrig_for), so in run c the MCV is drawn by the
    #    placement and never by the rig. Run c2 (--noeplc, a yard with no placement) is the
    #    rig's own case, where evhc_deploy_draw puts the remastered MCV in the rig.
    def rigs(L):
        rig = {}
        for l in L:
            if l.startswith('EVHCART|'):
                f = fields(l)
                rig[l.split('|')[1]] = max(rig.get(l.split('|')[1], 0), int(f['rigdrawn']))
        return rig
    C = log('c')
    rig, rig2 = rigs(C), rigs(log('c2'))
    fact = [fields(l) for l in C if l.startswith('EPLCMODEL|FACT|')]
    started = [l for l in C if l.startswith('EPLC|start|') and '|type=FACT|' in l]
    placed = bool(fact) and int(fact[-1].get('companion', '0')) > 0 \
        and int(fact[-1].get('drawn', '0')) > 0 and bool(started)
    clips = [float(fields(l).get('clip', '-1')) for l in C
             if l.startswith('EVHCOBJ|HARV|') and '|drawn=1|' in l]
    if not placed or rig.get('MCV', 0) != 0 or rig2.get('MCV', 0) <= 0 or rig.get('HARV', 0) <= 0:
        bad.append('rigs(placement %s%s, MCV in the rig %d (want 0 beside the placement), '
                   'MCV in the rig with --noeplc %d, HARV %d)'
                   % ('played' if started else 'not started',
                      (' companion %s drawn %s' % (fact[-1].get('companion'), fact[-1].get('drawn')))
                      if fact else ' no eplcdump', rig.get('MCV', 0), rig2.get('MCV', 0),
                      rig.get('HARV', 0)))
    elif not clips or max(clips) < 20.0 or min(clips) != 0.0:
        bad.append('arms(%s)' % sorted(set(clips)))
    else:
        say.append('the harvester works its arms (clip to %.0f); the MCV unfolds in the '
                   'Construction Yard\'s placement (companion of %s runs) and, with no placement, '
                   'in its deploy rig; the harvester is drawn in the refinery dock'
                   % (max(clips), fact[-1].get('companion')))
    # 10. switched off and in Classic: nothing of it draws or claims, and the old shatter
    #     is back for the same death
    for name, what in (('n', '--noevhc'), ('cl', 'Classic')):
        L = log(name)
        h = [l for l in L if l.startswith('EVHC|loaded=')]
        drawn = sum(int(fields(l)['drawn']) for l in L if l.startswith('EVHCART|'))
        ev = [l for l in L if l.startswith(('EVHC|fire', 'EVHC|death'))]
        oldl = [l for l in L if l.startswith('SHATTERUNIT|HTNK|')]
        if not h or '|on=0|' not in h[0] or drawn or ev or len(oldl) != 1:
            bad.append('%s(%s drawn %d events %d old %d)' % (what, h[0] if h else '-', drawn,
                                                              len(ev), len(oldl)))
    say.append('with --noevhc and in Classic nothing draws or claims and the old shatter takes '
               'the Mammoth')
    # 11. two runs are one picture
    for s in ('1', '2', '3', '1n', '3n'):
        a, b = 'shots/g296_a_%s.png' % s, 'shots/g296_a2_%s.png' % s
        if not (os.path.exists(a) and os.path.exists(b) and filecmp.cmp(a, b, shallow=False)):
            bad.append('nondeterministic' + s)
    # 12. the smooth normals: carried by every file, drawn into the normal target by every
    #     vehicle in view and by the wreck, and only the vehicles' pixels change without them
    nrm = normals(A, first, bad)
    if nrm:
        say.append(nrm)
    # 13. the Harvester's claws, their rams and its mouth's lower jaw
    cl = claws(log('c'), bad)
    if cl:
        say.append(cl)
    # 14. the Harvester's hoop in the Refinery's dock
    ho = hoop(log('c'), bad)
    if ho:
        say.append(ho)
    print('\n'.join(say))
    # (the suite keeps only the last line: a check that had nothing to measure says so there)
    idle = '; check 14 measured nothing: ' + ho if ho and 'declares no hoop' in ho else ''
    print(('OK ' + '; '.join(say[:1]) + idle) if not bad else 'FAIL ' + ' '.join(bad))


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'missions':
        missions(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 2 and sys.argv[1] == 'check':
        check()
    else:
        raise SystemExit(__doc__)
