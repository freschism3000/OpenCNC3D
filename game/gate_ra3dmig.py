#!/usr/bin/env python3
"""G347: Red Alert 3D draws its aircraft as their remastered models under Enhanced: the MiG
and the Yak landing on the Airfield, the Hind on the Helipad, their landing gear down on the
ground and stowed in flight, the Yak's propeller and the Hind's rotors idling on the ground and
at speed in the air, and the Spy Plane flying over (game/evhc_mod.h: the gear driver EVD_GEAR,
evhc_step's THE LANDING GEAR, THE ROTORS; game/cnc_eyes.cpp: RED ALERT'S AIRFIELD, the runway or
landing plate as the deck a parked aircraft stands on; game/evhc_ra/MIG.evhc, YAK.evhc,
HIND.evhc, U2.evhc staged by tools/ra3d/stage-vehicles.py once the lab approves them).

  gate_ra3dmig.py script DIR    write the missions and the run scripts into DIR
  gate_ra3dmig.py check DIR     read the runs' logs and pictures; first line is
                                "1 message" or "0 message"

Run from a play folder with the Red Alert module staged and the aircraft among ra3d/evhc.

THE RUNS. Private copies of Soviet 2 (temperate) and Soviet 1 (snow), an Allied Power Plant
added beside the Soviet base inside the Soviets' sight in each:

  t, s    a Soviet MiG added in the air near the Airfields: it lands on an empty one by
          itself (the engine's own idle mission for a fixed wing aircraft);
  yt      a Soviet Yak added the same way on Soviet 2;
  ys      Soviet 1's own first Yak, parked on its Airfield;
  ht, hs  a Soviet Helipad added beside the base, with the Hind that comes with it, and a
          Soviet Spy Plane added in flight (it flies over: it never lands).

Each aircraft that lands is then ordered at the Power Plant: it takes off, climbs, fires,
comes back and lands again. Each run takes a picture of it parked close up with the models
and with the cards (`evhc 0`), follows the take-off and the landing every tick with the camera
over the Airfield or Helipad, and the flight between in steps; the Spy Plane is pictured over
the first ticks with the models and with the cards.

CLAIMS, for each aircraft in each run:
  (1) it is drawn by its model and its card stands down (RA3DPROBE vehicle model=1), and with
      `evhc 0` it is a card again (model=0); the two frames differ round it;
  (2) parked, it stands on the runway or the landing plate, not in it: Altitude 0, lifted by
      the building's deck (lift over 0.05 of a cell), its gear down (drawn 0, every gear hinge
      at 0 degrees);
  (3) the take-off: as the engine climbs it from 0 to flight level the gear swings up
      through at least three drawn steps between down and stowed, never back down, and is
      stowed (1) before Altitude 13;
  (4) in flight at Altitude 24 the gear is stowed on every tick (the engine-tick state 1,
      every hinge drawn at its full angle when it is drawn), and it fires (its ammunition
      falls);
  (5) the landing: it comes down to Altitude 0 again, the gear swinging down through at
      least two drawn steps, and is down (0) at Altitude 0, standing on its deck again;
  (6) the Yak's propeller and the Hind's rotors: parked, their clock runs at the idle rate
      (0.35 of full, 0.0233 a tick) with no blur disc; at flight level at full rate (0.0667 a
      tick) with the disc fully up;
  (7) the Spy Plane: drawn by its model in flight, its card stood down, a card again with
      `evhc 0`.
PROVEN TO FAIL: the renderer before this work refuses MIG.evhc, YAK.evhc and HIND.evhc (their
gear driver unknown), so they are cards (model=0) with no EVHCGEAR line, and a parked aircraft
on a Red Alert Airfield or Helipad has no deck (lift 0).
  (8) the Badger: added in flight beside the Spy Plane in the Hind's runs, drawn by its model
      at flight level, its card stood down, a card again with `evhc 0` (it has no gear and
      never lands).
NOT COVERED: how the gear looks (the lab's gear sheets and the proof pictures); the deaths, the shared aircraft death G343 holds for the Longbow; the Spy
Plane's own mission from the Airfield (it is added in flight here).
"""
import json
import os
import re
import shutil
import sys

PLAY = os.getcwd()
T_ = ('SCU02EA', 'Greece', (71, 62), (72, 62))   # temperate: the Power Plant, the cell clicked
S_ = ('SCU01EA', 'France', (46, 74), (47, 74))   # snow
RUNS = {
    # tag: (scenario, house, Power Plant, attack cell), the aircraft ordered, the cell it is
    # added in flight at (None: the mission's own or the Helipad's), the camera cell over
    # where it parks, the Helipad's cell (None: none added), a Spy Plane's start (None: none)
    't': (T_, 'MIG', (70, 62), (76, 57), None, None),
    's': (S_, 'MIG', (44, 78), (40, 78), None, None),
    'yt': (T_, 'YAK', (68, 63), (76, 57), None, None),
    'ys': (S_, 'YAK', None, (40, 78), None, None),
    'ht': (T_, 'HIND', None, (78, 59), (77, 59), (66, 62)),
    'hs': (S_, 'HIND', None, (43, 80), (42, 80), (50, 70)),
}
NAME = {'t': 'the MiG, Soviet 2, temperate', 's': 'the MiG, Soviet 1, snow',
        'yt': 'the Yak, Soviet 2, temperate', 'ys': 'the Yak, Soviet 1, snow',
        'ht': 'the Hind, Soviet 2, temperate', 'hs': 'the Hind, Soviet 1, snow'}
# a Badger added in flight beside each Spy Plane (it has no gear: it flies only)
BADR_AT = {'ht': (62, 66), 'hs': (54, 66)}
LANDED = 320          # ticks by which an added aircraft has landed by itself
# ticks followed every tick after the order (the Hind spins up before it lifts), then passed
# in steps of 20 (the attack and the circling), then followed every tick for the landing
TIMES = {'MIG': (34, 200, 140), 'YAK': (34, 360, 130), 'HIND': (64, 340, 130)}
ROTOR_IDLE, ROTOR_FULL = 0.35 / 15.0, 1.0 / 15.0


def cell(x, y):
    return y * 128 + x


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def add(L, sec, rows):
    if not any(l.strip() == sec for l in L):
        L = L + ['', sec]
    st = next(i for i, l in enumerate(L) if l.strip() == sec)
    en = st + 1
    while en < len(L) and not L[en].startswith('['):
        en += 1
    nums = [int(m.group(1)) for l in L[st + 1:en] for m in [re.match(r'(\d+)=', l)] if m]
    while en > st + 1 and not L[en - 1].strip():
        en -= 1
    n0 = max(nums) + 1 if nums else 0
    return L[:en] + [f'{n0 + i}={r}' for i, r in enumerate(rows)] + L[en:]


def mission(d, tag):
    (scen, house, pp, _), ac, start, _, hpad, u2 = RUNS[tag]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().splitlines()
    air = [f'USSR,{ac},256,{cell(*start)},64,Guard'] if start else []
    if u2:
        air.append(f'USSR,U2,256,{cell(*u2)},64,Hunt')
    if tag in BADR_AT:
        air.append(f'USSR,BADR,256,{cell(*BADR_AT[tag])},64,Hunt')
    if air:
        L = add(L, '[AIRCRAFT]', air)
    bld = [f'{house},POWR,256,{cell(*pp)},0,None,0,0']
    if hpad:
        bld.append(f'USSR,HPAD,256,{cell(*hpad)},0,None,0,0')
    L = add(L, '[STRUCTURES]', bld)
    write(path, L)


def sample(d, tag):
    return ['tick 1', 'airdump', f'shot {d}/{tag}_run.png', 'ra3dprobe', 'evhcdump']


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, ((scen, house, pp, act), ac, start, (ax, ay), hpad, u2) in RUNS.items():
        mission(d, tag)
        climb, gap, appr = TIMES[ac]
        v = ['reveal 1']
        if u2:
            # the Spy Plane over its first ticks, with the models and with the cards
            v += ['tick 4', f'cam {u2[0] + 1} {u2[1]}', 'airdump', f'shot {d}/{tag}_u2.png', 'ra3dprobe',
                  'evhc 0', f'shot {d}/{tag}_u2cards.png', 'ra3dprobe', 'evhc 1']
            if tag in BADR_AT:
                # the Badger in the same ticks, the camera on it
                bx, by = BADR_AT[tag]
                v += ['echo GATE|badr', f'cam {bx + 1} {by}', 'airdump', f'shot {d}/{tag}_badr.png',
                      'ra3dprobe', 'evhc 0', f'shot {d}/{tag}_badrcards.png', 'ra3dprobe', 'evhc 1']
            v += ['echo GATE|u2end']
        v += [f'tick {LANDED}', f'cam {ax} {ay}', 'zoom max', 'airdump',
              f'shot {d}/{tag}_parked.png', 'ra3dprobe', 'evhcdump',
              'evhc 0', f'shot {d}/{tag}_cards.png', 'ra3dprobe', 'evhc 1']
        # parked, its rotors idling: a few ticks every tick
        for k in range(4):
            v += sample(d, tag)
        v += [f'lclickobj {ac}:USSR 0', 'select', f'cam {act[0]} {act[1]}', f'actcell {act[0]} {act[1]}',
              f'cam {ax + (0 if ac == "HIND" else 2)} {ay}', 'echo GATE|climb']
        for k in range(climb):
            v += sample(d, tag)
        v += ['echo GATE|gap']
        for k in range(gap // 20):
            v += ['tick 20', 'airdump']
        v += [f'cam {ax} {ay}', 'echo GATE|approach']
        for k in range(appr):
            v += sample(d, tag)
        v += [f'shot {d}/{tag}_parked2.png', 'ra3dprobe', 'evhcdump', 'quit']
        write(f'{d}/{tag}.txt', v)


def read(path):
    try:
        return open(path, errors='replace').read()
    except OSError:
        return ''


def kv(s):
    return dict(p.split('=', 1) for p in s.split('|') if '=' in p)


def ticks(lines, ac, ident):
    """per tick sample of the aircraft `ac` with the engine's id `ident`: the engine frame, its
    altitude, ammunition and lift, the gear's tick state, what was drawn (or None), the
    hinges' drawn angles, the probe's model flag, and the rotor clock and blur disc"""
    out, cur, frame = [], None, None
    tag = f'|{ac}|'
    for l in lines:
        if l.startswith('TICK|'):
            frame = int(kv(l).get('frame', -1))
        elif l.startswith('AIR' + tag):
            a = kv(l)
            if a.get('id') != ident:
                continue
            cur = {'frame': frame, 'alt': int(a['alt']), 'pips': int(a['pips'].split('/')[0]),
                   'gear': None, 'drawn': None, 'angles': None, 'model': None,
                   'lift': float(a['lift']), 'rotor': None, 'disc': None}
            out.append(cur)
        elif cur is None:
            continue
        elif l.startswith('RA3DPROBE|vehicle' + tag) and kv(l).get('id') == ident:
            cur['model'] = kv(l).get('model')
        elif l.startswith('EVHCOBJ' + tag) and kv(l).get('id') == ident:
            o = kv(l)
            cur['isdrawn'] = o.get('drawn') == '1'
            cur['rotor'], cur['disc'] = float(o['rotor']), float(o['disc'])
        elif l.startswith('EVHCGEAR' + tag) and kv(l).get('id') == ident:
            g = kv(l)
            cur['gear'] = float(g['gear'])
            if cur.get('isdrawn'):
                cur['drawn'] = float(g['drawngear'])
                cur['angles'] = [float(x) for x in g['angles'].split(',') if x]
    return out


def rotor_steps(samples):
    """(altitude, rotor clock advance over one engine tick, disc) for consecutive ticks"""
    out = []
    for p, q in zip(samples, samples[1:]):
        if p['rotor'] is not None and q['rotor'] is not None and p['frame'] is not None \
           and q['frame'] == p['frame'] + 1:
            out.append((q['alt'], q['rotor'] - p['rotor'], q['disc']))
    return out


def diff_round(a, b):
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return -1
    try:
        A = Image.open(a).convert('RGB')
        B = Image.open(b).convert('RGB')
    except OSError:
        return 0
    d = ImageChops.difference(A, B).convert('L').point(lambda v: 255 if v > 24 else 0)
    return d.histogram()[255]


def check_one(d, tag, text):
    (scen, house, pp, act), ac, start, cam, hpad, u2 = RUNS[tag]
    name = NAME[tag]
    if 'GAMEOVER|' in text:
        return None, f'{name}: the mission ended during the run'
    L = text.splitlines()
    sel = next((kv(l) for l in L if l.startswith('SELECT|') and f'type={ac}|' in l and '|selected' in l), None)
    if not sel:
        return None, f'{name}: the {ac} was not selected to be ordered'
    ident = sel['id']
    if f'EVHCGEAR|{ac}|' not in text:
        return None, f'{name}: no gear state (EVHCGEAR): its model is not attached'
    mark = {k: next((i for i, l in enumerate(L) if l == f'ECHO|GATE|{k}'), None)
            for k in ('u2end', 'climb', 'gap', 'approach')}
    if None in (mark['climb'], mark['gap'], mark['approach']):
        return None, f'{name}: the run did not reach its sections'
    head = L[mark['u2end'] or 0:mark['climb']]
    climb, gap, appr = L[mark['climb']:mark['gap']], L[mark['gap']:mark['approach']], L[mark['approach']:]
    extra = ''
    # (7) the Spy Plane flying over
    if u2:
        pr = [kv(l) for l in L[:mark['u2end'] or 0] if l.startswith('RA3DPROBE|vehicle|U2|')]
        if len(pr) < 2 or pr[0].get('model') != '1' or pr[-1].get('model') != '0' or pr[0].get('alt') != '24':
            return None, (f'{name}: the Spy Plane in flight drawn as model={pr[0].get("model") if pr else None}, '
                          f'with evhc 0 model={pr[-1].get("model") if pr else None}')
        upx = diff_round(f'{d}/{tag}_u2.png', f'{d}/{tag}_u2cards.png')
        if 0 <= upx < 800:
            return None, f'{name}: the Spy Plane model and card frames differ on {upx} pixels only'
        extra = f'; the Spy Plane flies over as its model ({upx} pixels from its card)'
    # (8) the Badger flying over: its model in flight, a card again with evhc 0
    if tag in BADR_AT:
        b0 = next((i for i, l in enumerate(L) if l == 'ECHO|GATE|badr'), None)
        pr = [kv(l) for l in L[b0 or 0:mark['u2end'] or 0] if l.startswith('RA3DPROBE|vehicle|BADR|')]
        if b0 is None or len(pr) < 2 or pr[0].get('model') != '1' or pr[-1].get('model') != '0' \
           or pr[0].get('alt') != '24':
            return None, (f'{name}: the Badger in flight drawn as model={pr[0].get("model") if pr else None} '
                          f'at altitude {pr[0].get("alt") if pr else None}, with evhc 0 '
                          f'model={pr[-1].get("model") if pr else None}')
        bpx = diff_round(f'{d}/{tag}_badr.png', f'{d}/{tag}_badrcards.png')
        if 0 <= bpx < 800:
            return None, f'{name}: the Badger model and card frames differ on {bpx} pixels only'
        extra += f'; the Badger flies over as its model ({bpx} pixels from its card)'
    # (1) the model, its card stood down, and the card again with evhc 0
    probes = [kv(l) for l in head if l.startswith(f'RA3DPROBE|vehicle|{ac}|') and kv(l).get('id') == ident]
    if len(probes) < 2 or probes[0].get('model') != '1' or probes[1].get('model') != '0':
        return None, (f'{name}: parked, drawn as model={probes[0].get("model") if probes else None}, '
                      f'with evhc 0 model={probes[1].get("model") if len(probes) > 1 else None}')
    px = diff_round(f'{d}/{tag}_parked.png', f'{d}/{tag}_cards.png')
    if 0 <= px < 1500:
        return None, f'{name}: the model and the card frames differ on {px} pixels only'
    # (2) parked on its deck, gear down
    park = ticks(head, ac, ident)
    pg = [kv(l) for l in head if l.startswith(f'EVHCGEAR|{ac}|') and kv(l).get('id') == ident]
    if not park or park[0]['alt'] != 0 or park[0]['lift'] < 0.05 or not pg \
       or float(pg[0]['drawngear']) != 0.0 or any(float(a) != 0.0 for a in pg[0]['angles'].split(',')):
        return None, (f'{name}: parked: alt {park and park[0]["alt"]}, lift {park and park[0]["lift"]}, '
                      f'gear {pg and pg[0].get("drawngear")} angles {pg and pg[0].get("angles")}')
    # (3) the take-off
    up = [t for t in ticks(climb, ac, ident)]
    drawn = [t['drawn'] for t in up if t['drawn'] is not None]
    mids = sorted({round(g, 3) for g in drawn if 0.0 < g < 1.0})
    back = any(b < a - 1e-6 for a, b in zip(drawn, drawn[1:]))
    stowed_at = next((t['alt'] for t in up if t['drawn'] == 1.0), None)
    if not up or up[0]['alt'] > 1 or len(mids) < 3 or back or stowed_at is None or stowed_at > 12:
        return None, (f'{name}: the take-off: first alt {up and up[0]["alt"]}, drawn steps {mids}, '
                      f'swung back {back}, stowed at alt {stowed_at}')
    # (4) flight level: stowed on every tick, full angles where drawn, and it fires
    allt = ticks(L, ac, ident)
    fl = [t for t in allt if t['alt'] == 24 and t['gear'] is not None]
    full = [t for t in fl if t['angles']]
    if not fl or any(t['gear'] != 1.0 for t in fl) or not full \
       or any(a <= 45.0 for t in full for a in t['angles']) or any(t['drawn'] != 1.0 for t in full):
        return None, (f'{name}: in flight: {len(fl)} ticks at flight level, gear states '
                      f'{sorted({t["gear"] for t in fl})}, {len(full)} drawn')
    if min(t['pips'] for t in allt) >= allt[0]['pips']:
        return None, f'{name}: it never fired (ammunition {allt[0]["pips"]} throughout)'
    # (5) the landing
    dn = ticks(appr, ac, ident)
    i0 = next((i for i, t in enumerate(dn) if t['alt'] < 24), None)
    land = next((i for i, t in enumerate(dn) if i0 is not None and i > i0 and t['alt'] == 0), None)
    if i0 is None or land is None:
        return None, f'{name}: the landing: it did not come down to Altitude 0 again'
    seq = [t['drawn'] for t in dn[i0:land + 1] if t['drawn'] is not None]
    dmid = sorted({round(g, 3) for g in seq if 0.0 < g < 1.0})
    after = [t for t in dn[land:] if t['drawn'] is not None]
    if len(dmid) < 2 or not after or any(t['drawn'] != 0.0 or t['alt'] != 0 for t in after) \
       or after[-1]['lift'] < 0.05:
        return None, (f'{name}: the landing: drawn steps {dmid}, after touchdown '
                      f'{sorted({(t["alt"], t["drawn"]) for t in after})}, lift {after and after[-1]["lift"]}')
    msg = (f'{name}: parked on its deck (lift {park[0]["lift"]:.3f}) gear down, up in {len(mids)} steps by '
           f'Altitude {stowed_at}, stowed for {len(fl)} ticks at flight level, fired, down in {len(dmid)} '
           f'steps, landed; {px} pixels from its card')
    # (6) the propeller and the rotors
    if ac in ('YAK', 'HIND'):
        st = rotor_steps(park) + rotor_steps(up) + rotor_steps(dn)
        idle = [(r, c) for a, r, c in rotor_steps(park)]
        top = [(r, c) for a, r, c in st if a == 24]
        if not idle or any(abs(r - ROTOR_IDLE) > 0.002 or c != 0.0 for r, c in idle) \
           or not top or any(abs(r - ROTOR_FULL) > 0.002 or c < 0.999 for r, c in top):
            return None, (f'{name}: the rotors: parked {[(round(r, 4), c) for r, c in idle]}, '
                          f'at flight level {[(round(r, 4), c) for r, c in top][:4]}')
        msg += (f'; {"its propeller" if ac == "YAK" else "its rotors"} idle on the ground '
                f'({len(idle)} ticks) and at speed with the disc up in flight ({len(top)} ticks)')
    return True, msg + extra


def check(d):
    fails, notes = [], []
    for tag in RUNS:
        ok, msg = check_one(d, tag, read(f'{d}/{tag}.log'))
        (notes if ok else fails).append(msg)
    if fails:
        print('0 ' + '; '.join(fails))
    else:
        print('1 Red Alert 3D draws its aircraft as their models, gear down on the ground and up in flight: '
              + '; '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit(__doc__)
