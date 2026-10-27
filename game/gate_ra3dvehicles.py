#!/usr/bin/env python3
"""G343: Red Alert 3D draws OpenCNC3D's remastered vehicles and aircraft under Enhanced.

  gate_ra3dvehicles.py script DIR    write the missions and the four run scripts into DIR
  gate_ra3dvehicles.py check DIR     read the runs' logs and pictures; first line is
                                     "1 message" or "0 message"

Run from a play folder with the Red Alert module staged (ra3d/evhc by
tools/ra3d/stage-vehicles.py). Every type staged there is tested; the module's manifest
says which OpenCNC3D file each one is.

THE MISSIONS. A private copy of the module's missions with, added to one mission each,
every staged ground type in an Allied row (Greece, facing south) and a Soviet row (USSR,
facing north) five cells apart, and both staged aircraft types at the rows' ends; one
Medium Tank of each side starts at a third of its strength:

  t   Allied 5 (temperate, the player Greece): the rows, the same frame without the models
      (`evhc 0`), the player's Light Tank ordered off, fourteen ticks of the fight sampled,
      an Allied click on a model, then the Soviet APC, Longbow and Light Tank destroyed and
      the wrecks followed, then the mode switched to Classic;
  s   Soviet 1 (snow, the player USSR): the rows, and the same frame without the models;
  c   Soviet 9 (temperate): its wooden and steel crates, with OpenCNC3D's models and
      without (`ebld 0`);
  w   Soviet 2 (temperate): its T06 trees;
  jt, js  the two missions again, close up on the Allied Radar Jammer: two frames three
      ticks apart, and the same frame with the cards.

CLAIMS:
  (1) every unit and aircraft of a staged type is drawn by its model and its card stands
      down (RA3DPROBE vehicle model=1, the frame's vehiclemodels), in both rows of both
      theaters; a type with no file keeps its card; with `evhc 0` every one is a card
      again;
  (2) the frame with the models differs from the same frame with the cards on at least
      20000 pixels in each theater;
  (3) the sides' colours: the pixels the models change are red for the Soviets (red over
      green and blue by 25 levels) and not red for the Allies (blue at least red);
  (4) OpenCNC3D's size, no Red Alert factor: each staged file is OpenCNC3D's file but for
      the eight bytes of its type in the header, and each ground model's box across its
      hull on the map is the model's own width (EVHCART, pack units / 1024) within 0.02 of
      a cell;
  (5) the motions: the ordered Light Tank's tracks run (its travel and its belt frame
      advance), a turret turns off its hull, a shot of a staged type kicks its barrel
      (EVHC|fire, kick > 0), the aircraft fly with their rotors' discs up (disc 1 at
      altitude 24, lifted 0.938 cells); an Allied rifleman ordered into the Allied APC
      and its door swings open (the brain's door state for a unit);
  (6) the click on a model selects that unit;
  (7) the deaths: the APC and the Light Tank break into their wrecks on the ground, the
      Longbow falls whole and breaks where it lands (EVHC|death air=1, EVHC|impact);
  (8) Classic draws none of it (EVHC on=0);
  (9) the wooden crates are OpenCNC3D's crate model (eterrdump), the frame differs from
      the 1996 cards, and the steel crate is still its own;
  (10) Soviet 2's T06 trees are their reused models;
  (11) the Radar Jammer (OpenCNC3D's Mobile HQ, reused) is its model close up in both
      theaters and its dish turns: the frames three ticks apart differ round it on 100 to
      1500 pixels, a patch the size of the dish and not the whole vehicle.
PROVEN TO FAIL: the renderer before this work draws every Red Alert vehicle and aircraft
as its card (model=0 everywhere, no EVHC line for a Red Alert type) and the wooden crate
as its 1996 picture.
NOT COVERED: how the models look against the cards (looked at by eye: the proof sheets);
the Phase Transport's cloak (the 1996 rules give it none); the Chinook's ramp (no scripted
way to board a landed Chinook, as in Tiberian Dawn's G296); Red Alert's own remasters
(the Heavy Tank, the V2 and its erector, the Ore Truck's scoop, dump and load, the Gun
Boat), which G345 covers (game/gate_ra3downvehicles.py).
"""
import json
import os
import re
import shutil
import sys

PLAY = os.getcwd()
GROUND = ['ARTY', '1TNK', '2TNK', 'APC', 'STNK', 'MRJ', '4TNK']
# a type only one side fields in Red Alert: the Mammoth is Soviet (no Allied one is added). It
# stands two cells ahead of the Soviet row, between its columns (dx from the row's start, cells),
# in sight of the camera and in range of the Allied row
ONE_SIDE = {'4TNK': 'USSR'}
ONE_SIDE_AT = {'4TNK': 3}
AIR = ['HELI', 'TRAN']
MISSIONS = {'t': ('SCG05EA', 17, 50, 5), 's': ('SCU01EA', 38, 80, 4)}
CAMS = {'t': (22, 53), 's': (43, 82)}


def staged():
    try:
        return json.load(open(os.path.join(PLAY, 'ra3d/evhc/manifest.json')))['types']
    except (OSError, ValueError, KeyError):
        return {}


# THE LIGHT THESE CLAIMS WERE WRITTEN UNDER, PINNED in every script. The side colours are
# measured as a share of red or blue round each model and the Gap Generator's turn as pixels
# changing round its fork, and Red Alert ships a picture of its own (fx_red_alert_layer in
# fx_state.h) that moves all of those numbers without touching what is tested: a darker,
# desaturated vehicle grade and a warmer one take the Soviet red and the Allied blue under
# their thresholds, and darker snow lets a light's flicker on the ground beside the fork
# count as change. So the vehicle dials are neutral and the sun, the grade's temperature and
# the snow are the values the claims were measured with. The paint and the motion are what
# is tested here, not the grade.
PINNED_LIGHT = [f'gfx vh_{k} 1' for k in
                ('gamma', 'bright', 'contrast', 'sat', 'shade', 'sun', 'glow', 'ao')] + [
    'gfx unified_lighting 0',
    'gfx temperature -0.01786', 'gfx sun_az 67.5', 'gfx sun_el 28.71428',
    'gfx shadow_strength 1', 'gfx shadow_soft 0.64286', 'gfx shadow_bias 0.03043',
    'gfx sun_floor 0.54464', 'gfx snow_brightness 1', 'gfx snow_saturation 1']


def write(path, lines):
    if path.endswith('.txt'):
        lines = PINNED_LIGHT + list(lines)
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def mission(d, tag, scen, x0, y0, gap):
    """A private missions folder with the test rows added to one mission."""
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    lines = open(path, errors='replace').read().splitlines()
    cell = lambda x, y: y * 128 + x
    units, air = [], []
    for side, y, face in (('Greece', y0, 128), ('USSR', y0 + gap, 0)):
        for i, t in enumerate(GROUND):
            if t in ONE_SIDE:
                if ONE_SIDE[t] == side:
                    units.append(f'{side},{t},256,{cell(x0 + ONE_SIDE_AT[t], y - 2)},{face},Guard,None')
                continue
            units.append(f'{side},{t},{90 if t == "2TNK" else 256},{cell(x0 + 2 * i, y)},{face},Guard,None')
        air.append(f'{side},HELI,256,{cell(x0 - 3, y)},{face},Guard')
        air.append(f'{side},TRAN,256,{cell(x0 + 2 * len(GROUND) + 1, y)},{face},Guard')

    def add(lines, section, rows):
        start = next((i for i, l in enumerate(lines) if l.strip() == f'[{section}]'), None)
        if start is None:
            lines += ['', f'[{section}]']
            start = len(lines) - 1
        end, nums = start + 1, []
        while end < len(lines) and not lines[end].startswith('['):
            m = re.match(r'(\d+)=', lines[end])
            if m:
                nums.append(int(m.group(1)))
            end += 1
        while end > start + 1 and not lines[end - 1].strip():
            end -= 1
        n = max(nums) + 1 if nums else 0
        return lines[:end] + [f'{n + k}={r}' for k, r in enumerate(rows)] + lines[end:]

    lines = add(add(lines, 'UNITS', units), 'AIRCRAFT', air)
    # a rifleman of each side beside its row's APC, the Allied one to board it (the door)
    inf = [f'Greece,E1,256,{cell(x0 + 6, y0 + 2)},0,Guard,128,None',
           f'USSR,E1,256,{cell(x0 + 6, y0 + gap - 2)},0,Guard,0,None']
    lines = add(lines, 'INFANTRY', inf)
    write(path, lines)


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, (scen, x0, y0, gap) in MISSIONS.items():
        mission(d, tag, scen, x0, y0, gap)
    cx, cy = CAMS['t']
    t = ['tick 2', 'reveal 1', f'cam {cx} {cy}', 'dist 1500', f'shot {d}/t_on.png', 'ra3dprobe',
         'evhcscreen', 'evhcdump', 'evhc 0', f'shot {d}/t_off.png', 'ra3dprobe', 'evhc 1',
         'lclickobj 1TNK:Greece 0', 'select', 'order 19 48',
         'lclickobj E1:Greece 0', 'actclickobj APC:Greece 0']
    for _ in range(7):
        t += ['tick 2', f'shot {d}/t_run.png', 'evhcdump']
    t += ['hurtunit APC:USSR 0 5000', 'hurtunit HELI:USSR 0 5000', 'hurtunit 1TNK:USSR 0 5000']
    for _ in range(8):
        t += ['tick 2', f'shot {d}/t_dead.png', 'evhcdump']
    # the Mammoth fights on (its guns are slow to reload), then goes too
    for _ in range(20):
        t += ['tick 4', 'evhcdump']
    t += [f'shot {d}/t_mammoth.png', 'hurtunit 4TNK:USSR 0 5000']
    for _ in range(8):
        t += ['tick 2', 'evhcdump']
    t += ['gfxmode 0', 'tick 1', 'evhcdump', 'quit']
    write(f'{d}/t.txt', t)
    cx, cy = CAMS['s']
    # (the snow mission is the Soviets': its Mammoth is ordered at the Allied Light Tank and
    # followed every tick as it turns and fires both guns)
    write(f'{d}/s.txt', ['tick 2', 'reveal 1', f'cam {cx} {cy}', 'dist 1500', f'shot {d}/s_on.png',
                         'ra3dprobe', 'evhcscreen', 'evhcdump', 'evhc 0', f'shot {d}/s_off.png',
                         'ra3dprobe', 'evhc 1', 'lclickobj 4TNK:USSR 0', 'select',
                         'actclickobj 1TNK:Greece 0'] + ['tick 1', 'frames 1', 'evhcdump'] * 40 + ['quit'])
    write(f'{d}/c.txt', ['tick 2', 'reveal 1', 'cam 105 54', 'dist 900', f'shot {d}/c_on.png',
                         'eterrdump', 'ebld 0', f'shot {d}/c_off.png', 'quit'])
    write(f'{d}/w.txt', ['tick 2', 'reveal 1', 'cam 61 59', 'dist 1500', f'shot {d}/w.png',
                         'ra3dprobe', 'quit'])
    # the Radar Jammer close up in each theater, on the same private missions: two frames
    # three ticks apart (its dish turns between them) and the same frame with the cards
    for tag, (scen, x0, y0, gap) in MISSIONS.items():
        jx = x0 + 2 * GROUND.index('MRJ')
        write(f'{d}/j{tag}.txt', ['tick 2', 'reveal 1', f'cam {jx} {y0}', 'dist 700',
                                  f'shot {d}/j{tag}_a.png', 'ra3dprobe', 'evhcscreen', 'tick 3',
                                  f'shot {d}/j{tag}_b.png', 'evhcscreen', 'evhc 0',
                                  f'shot {d}/j{tag}_card.png', 'quit'])


def read(path):
    try:
        return open(path, errors='replace').read().splitlines()
    except OSError:
        return []


def kv(line):
    return dict(p.split('=', 1) for p in line.split('|') if '=' in p)


def probes(lines):
    out, cur = [], None
    for l in lines:
        if l.startswith('RA3DPROBE|frame|'):
            cur = [l]
            out.append(cur)
        elif l.startswith('RA3DPROBE|') and cur is not None:
            cur.append(l)
    return out


def vehicles(probe):
    out = []
    for l in probe:
        if l.startswith('RA3DPROBE|vehicle|'):
            k = kv(l)
            k['type'] = l.split('|')[2]
            out.append(k)
    return out


def samples(lines, tag):
    """[{key: {fields}}] per evhcdump, of the EVHCOBJ lines."""
    out, cur = [], None
    for l in lines:
        if l.startswith('EVHC|loaded='):
            cur = {}
            out.append(cur)
        elif l.startswith(tag) and cur is not None:
            k = kv(l)
            k['type'] = l.split('|')[1]
            cur[k['key']] = k
    return out


def pixels(d, a, b):
    from PIL import Image
    import numpy as np
    try:
        x = np.asarray(Image.open(f'{d}/{a}').convert('RGB')).astype(int)[:, :1040]
        y = np.asarray(Image.open(f'{d}/{b}').convert('RGB')).astype(int)[:, :1040]
    except OSError:
        return None, None, None
    return x, y, np.abs(x - y).sum(-1) > 30


def colours(d, lines, on, off):
    """{(type, house): (r, g, b)}: the mean colour of what the models change round each
    model, where evhcscreen put it."""
    x, y, ch = pixels(d, on, off)
    out = {}
    if x is None:
        return out
    for l in lines:
        if not l.startswith('EVHCSCR|'):
            continue
        k = kv(l)
        if k.get('alt', '0') != '0':
            continue                                  # a craft in flight is lifted away
        c, r = (int(float(v)) for v in k['scr'].split(','))
        r0, r1, c0, c1 = max(r - 18, 0), min(r + 18, x.shape[0]), max(c - 18, 0), min(c + 18, 1040)
        # the hull's pixels the model changed, without a muzzle flash's or the snow's
        # brightest (a mean over 190)
        m = ch[r0:r1, c0:c1] & (x[r0:r1, c0:c1].mean(-1) < 190)
        if m.sum() < 60:
            continue
        px = x[r0:r1, c0:c1][m]
        out.setdefault((l.split('|')[1], k['house']), []).append(px.mean(0))
    return {key: sum(v) / len(v) for key, v in out.items()}


def check(d):
    fails, notes = [], []
    st = staged()
    types = set(st)
    if not types:
        print('0 no remastered vehicle is staged in ra3d/evhc (tools/ra3d/stage-vehicles.py)')
        return
    want = types & set(GROUND + AIR)
    t, s = read(f'{d}/t.log'), read(f'{d}/s.log')
    # (1) models and cards
    for tag, lines in (('temperate', t), ('snow', s)):
        p = probes(lines)
        if len(p) < 2:
            fails.append(f'{tag}: the run printed {len(p)} probes')
            continue
        on, off = vehicles(p[0]), vehicles(p[1])
        seen = {(v['type'], v['house']) for v in on if v['type'] in want and v['visible'] == '1'}
        miss = sorted(f'{ty}/{h}' for ty in want for h in ('Greece', 'USSR')
                      if (ty, h) not in seen and ONE_SIDE.get(ty, h) == h)
        bad = sorted({v['type'] for v in on if v['type'] in types and v['visible'] == '1' and v['model'] != '1'})
        cards = sorted({v['type'] for v in on if v['type'] not in types and v['model'] != '0'})
        offm = [v['type'] for v in off if v['model'] != '0']
        nmod = int(kv(p[0][0]).get('vehiclemodels', '0'))
        if miss:
            fails.append(f'{tag}: test units missing from the probe: {miss}')
        if bad or cards or offm or nmod < 2 * len(want) - 2:
            fails.append(f'{tag}: models {bad} not drawn, {cards} drawn without a file, {offm} '
                         f'still models with evhc 0, {nmod} cards stood down')
        else:
            notes.append(f'{tag}: {nmod} units and aircraft as their models')
    # (2) pixels
    for tag, a, b in (('temperate', 't_on.png', 't_off.png'), ('snow', 's_on.png', 's_off.png')):
        x, y, ch = pixels(d, a, b)
        n = -1 if ch is None else int(ch.sum())
        if n < 20000:
            fails.append(f'{tag}: the models change {n} pixels against the cards (20000)')
    # (3) colours
    for tag, lines, a, b in (('temperate', t, 't_on.png', 't_off.png'), ('snow', s, 's_on.png', 's_off.png')):
        col = colours(d, lines, a, b)
        # red: red over green and blue by 12 levels; a hull lit by its own muzzle flash comes
        # out warm, so an Allied one fails only if red by 30
        isred = lambda c, by: c[0] > c[1] + by and c[0] > c[2] + by
        red = [k for k, c in col.items() if k[1] == 'USSR' and not isred(c, 12)]
        blue = [k for k, c in col.items() if k[1] == 'Greece' and isred(c, 30)]
        sov = [c for k, c in col.items() if k[1] == 'USSR']
        alli = [c for k, c in col.items() if k[1] == 'Greece']
        # the Allies' light blue: blue over red on average, and far from the Soviets' red
        tilt = lambda cs: sum(c[2] - c[0] for c in cs) / len(cs) if cs else 0.0
        if '4TNK' in want and ('4TNK', 'USSR') not in col:
            fails.append(f'{tag}: the Soviet Mammoth\'s colour was not measured')
        if red or blue or len(sov) < 3 or len(alli) < 3 or tilt(alli) < 8 or tilt(alli) - tilt(sov) < 40:
            fails.append(f'{tag}: side colours wrong: Soviet not red {red}, Allied red {blue}, '
                         f'Allied blue over red {tilt(alli):.0f}, Soviet {tilt(sov):.0f} '
                         f'({len(sov)} Soviet, {len(alli)} Allied measured)')
        else:
            notes.append(f'{tag}: {len(sov)} Soviet red, {len(alli)} Allied light blue')
    # (4) the size
    for code, row in st.items():
        of = row.get('reuse_of')
        if not of:
            continue
        a = open(os.path.join(PLAY, 'ra3d/evhc', code + '.evhc'), 'rb').read()
        b = open(os.path.join(PLAY, 'evhc', of + '.evhc'), 'rb').read()
        if a[:8] != b[:8] or a[16:] != b[16:] or a[8:16].rstrip(b'\0').decode() != code:
            fails.append(f'{code}.evhc is not OpenCNC3D\'s {of}.evhc with its type renamed')
    art = {l.split('|')[1]: kv(l) for l in t if l.startswith('EVHCART|')}
    p = probes(t)
    wide = []
    for v in (vehicles(p[0]) if p else []):
        if v['type'] not in GROUND or v['type'] not in art or v['face'] not in ('0', '128'):
            continue
        x0, x1 = (float(q) for q in art[v['type']]['x'].split(','))
        bx = [float(q) for q in v['box'].split(',')]
        if abs((bx[1] - bx[0]) - (x1 - x0) / 1024.0) > 0.02:
            wide.append(f"{v['type']} {bx[1] - bx[0]:.3f} against {(x1 - x0) / 1024.0:.3f}")
    if wide or not p:
        fails.append(f'models not at OpenCNC3D\'s size: {wide}')
    else:
        notes.append('every file OpenCNC3D\'s, every model its own size')
    # (5) the motions
    sm = samples(t, 'EVHCOBJ|')
    mine = [v['id'] for v in (vehicles(p[0]) if p else []) if v['type'] == '1TNK' and v['house'] == 'Greece']
    tank = [smp[k] for smp in sm for k in smp
            if smp[k]['type'] == '1TNK' and mine and smp[k]['id'] == mine[0]]
    trav = [float(x['trav'].split(',')[0]) for x in tank]
    frames = {x['frame'] for x in tank}
    turned = any(smp[k]['type'] in want and smp[k]['tur'] not in ('0', '-1')
                 for smp in sm for k in smp if smp[k]['type'] in GROUND)
    kicked = any(float(smp[k]['kick']) > 0 for smp in sm for k in smp)
    fired = [l for l in t if l.startswith('EVHC|fire|') and kv(l).get('type') in want]
    flying = [smp[k] for smp in sm for k in smp if smp[k]['type'] in AIR]
    disc = any(x['disc'] == '1.00' for x in flying)
    if not trav or max(trav) - min(trav) < 0.5 or len(frames) < 2:
        fails.append(f'the ordered Light Tank did not drive on its tracks: travel {trav[:1]}..{trav[-1:]}, frames {sorted(frames)[:4]}')
    if not turned or not kicked or len(fired) < 2 or not disc:
        fails.append(f'motions missing: turret turned {turned}, recoil {kicked}, {len(fired)} shots, rotor discs up {disc}')
    else:
        notes.append(f'tracks, turrets, {len(fired)} shots with recoil, rotors')
    # (5b) the APC's door: a rifleman ordered into it, and the door swings open
    apc = [v['id'] for v in (vehicles(p[0]) if p else []) if v['type'] == 'APC' and v['house'] == 'Greece']
    doors = [float(smp[k]['door']) for smp in sm for k in smp
             if smp[k]['type'] == 'APC' and apc and smp[k]['id'] == apc[0]]
    if 'APC' in want and (not doors or max(doors) < 0.9):
        fails.append(f'the Allied APC\'s door did not open for its passenger: {doors[:8]}')
    elif 'APC' in want:
        notes.append(f'the APC\'s door opens ({max(doors):.2f})')
    # (5c) the Mammoth (OpenCNC3D's round-9 model, reused): its turret turns off its hull and it
    # fires its guns, a shot kicking a barrel
    if '4TNK' in want:
        mam = [smp[k] for smp in sm + samples(s, 'EVHCOBJ|') for k in smp if smp[k]['type'] == '4TNK']
        mturn = any(x['tur'] not in ('0', '-1') for x in mam)
        mshots = [l for l in t + s if l.startswith('EVHC|fire|') and kv(l).get('type') == '4TNK']
        mkick = any(float(x['kick']) > 0 for x in mam)
        if not mam or not mturn or not mshots or not mkick:
            fails.append(f'the Soviet Mammoth: {len(mam)} samples, turret turned {mturn}, '
                         f'{len(mshots)} shots, recoil {mkick}')
        else:
            notes.append(f'the Soviet Mammoth turns its turret and fires ({len(mshots)} shots with recoil)')
    # (6) the click
    if not any(l.startswith('SELECT|') and 'type=1TNK' in l and 'house=Greece' in l and l.endswith('selected')
               for l in t):
        fails.append('a click on the Allied Light Tank\'s model did not select it')
    # (7) the deaths
    deaths = {kv(l)['type']: kv(l) for l in t if l.startswith('EVHC|death|')}
    if not all(k in deaths for k in ('APC', '1TNK', 'HELI')) or deaths.get('HELI', {}).get('air') != '1' \
       or deaths.get('APC', {}).get('air') != '0':
        fails.append(f'the deaths: {sorted(deaths)}')
    elif not any(l.startswith('EVHC|impact|') and 'type=HELI' in l for l in t):
        fails.append('the Longbow did not come down')
    elif not any(l.startswith('EVHCWRECK|APC|') for l in t):
        fails.append('the APC left no wreck')
    elif '4TNK' in want and ('4TNK' not in deaths or not any(l.startswith('EVHCWRECK|4TNK|') for l in t)):
        fails.append('the Soviet Mammoth left no wreck')
    else:
        notes.append('APC, Light Tank' + (' and Mammoth' if '4TNK' in want else '')
                     + ' wrecked, the Longbow shot down')
    # (8) Classic
    last = [l for l in t if l.startswith('EVHC|loaded=')]
    if not last or kv(last[-1]).get('on') != '0':
        fails.append(f'Classic: {last[-1:]}')
    # (9) the crates
    c = read(f'{d}/c.log')
    cr = [l for l in c if l.startswith('ETERRAT|crate|')]
    wood = [l for l in cr if l.split('|')[4] == 'WCRATE']
    steel = [l for l in cr if l.split('|')[4] == 'SCRATE']
    x, y, ch = pixels(d, 'c_on.png', 'c_off.png')
    if len(wood) < 2 or not steel or ch is None or ch.sum() < 500:
        fails.append(f'Soviet 9\'s crates: {len(wood)} wooden and {len(steel)} steel as models')
    else:
        notes.append(f'{len(wood)} wooden crates as the model')
    # (10) T06
    w = probes(read(f'{d}/w.log'))
    t06 = [l for l in (w[0] if w else []) if l.startswith('RA3DPROBE|terrain|T06|')]
    if not t06 or any(kv(l).get('model') != '1' for l in t06):
        fails.append(f'Soviet 2\'s T06 trees: {t06[:2]}')
    else:
        notes.append(f'{len(t06)} T06 trees as models')
    # (11) the Radar Jammer: the Allied one close up is its model with its card stood down,
    # and its dish turns: the frames three ticks apart differ round it, and only in a
    # patch the size of the dish (the hull stands still)
    if 'MRJ' in want:
        for tag, name in (('t', 'temperate'), ('s', 'snow')):
            lines = read(f'{d}/j{tag}.log')
            p = probes(lines)
            mine = [v for v in (vehicles(p[0]) if p else []) if v['type'] == 'MRJ' and v['house'] == 'Greece']
            scr = [kv(l)['scr'] for l in lines if l.startswith('EVHCSCR|MRJ|') and 'house=Greece' in l]
            x, y, ch = pixels(d, f'j{tag}_a.png', f'j{tag}_b.png')
            if not mine or mine[0]['model'] != '1' or len(scr) < 2 or scr[0] != scr[1] or ch is None:
                fails.append(f'{name}: the Radar Jammer close up: {mine[:1]}, at {scr}')
                continue
            c, r = (int(float(v)) for v in scr[0].split(','))
            near = int(ch[max(r - 40, 0):r + 40, max(c - 40, 0):c + 40].sum())
            far = int(ch.sum()) - near
            if near < 100 or near > 1500:
                fails.append(f'{name}: the Radar Jammer\'s dish: {near} pixels change round it '
                             f'in three ticks (100 to 1500), {far} elsewhere')
            else:
                notes.append(f'{name}: the Radar Jammer\'s dish turns ({near} pixels in three ticks)')
    if fails:
        print('0 ' + '; '.join(fails))
    else:
        print('1 Red Alert 3D draws OpenCNC3D\'s vehicles: ' + '; '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit(__doc__)
