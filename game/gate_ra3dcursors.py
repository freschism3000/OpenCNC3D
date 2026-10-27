#!/usr/bin/env python3
"""G354: Red Alert 3D draws its remastered cursors under Enhanced, every mouse state
(game/cursor3d_mod.h: c3d_ra_row, the ecur_ra_* loader and attach, c3d_ra_overlay, NO_SCROLL
in c3d_draw_body; game/cursors.h: Red Alert's mouse states and cur_mouse_for_action_ra;
ra3d/ecur/ecur_ra.bin, baked by tools/art/remaster/ra/bake_ecur_ra.py and staged with the
module by tools/ra3d/stage-structures.py).

    python3 game/gate_ra3dcursors.py script DIR     write the runs' missions and scripts
    python3 game/gate_ra3dcursors.py check DIR      read the logs: "1 message" or "0 message"

Run from a play folder with a staged Red Alert module. Two hidden runs, the pointer moved by
script only (never the desk's own):

  t  Soviet 2, temperate
  s  Allied 3, snow, with a wounded rifleman, a damaged power plant of the player's beside
     its medic and engineer

Claims, for every run:
  (1) Red Alert's file loaded and attached whole: 12 meshes;
  (2) every one of its 25 mouse states drawn on the ground (cursor3dmouse) took the row the
      approved cross-reference gives it: the remastered mesh (Red Alert's own for its six,
      OpenCNC3D's for the rest), the no-entry sign over exactly the six refused ones, the
      clock's 100 frames on exactly the animated ones; and each picture differs from the
      plain pointer's where the cursor stands;
  (3) the animations the cartridge never had: the heal, damage and chronosphere-select
      flipbooks step through their frames, the gold spanner turns (and is still under its
      sign), the chronosphere target's two rings close half a cycle apart;
  (4) the scroll arrow on the map's west edge: plain while the view can still go west, under
      the no-entry sign (NO_SCROLL) once it cannot;
  (5) over the radar map with nothing selected the pointer is the radar cursor, drawn as its
      remastered frame (the overlay count rises);
and for s, (6) the engine's own answer through the probe: the medic over the wounded
rifleman is HEAL, the engineer over the damaged power plant is GREPAIR (Red Alert's
ActionType numbers, which Tiberian Dawn's table read as other actions).

PROVEN TO FAIL: the renderer before this work has no cursor3dmouse verb and prints no
ECURRA line; it drew Tiberian Dawn's cartridge state for every Red Alert state and read the
probe's HEAL (26) and GREPAIR (28) as the plain pointer.
NOT COVERED: how the models look (the contact sheets); Tiberian Dawn, whose pictures are
compared byte for byte before and after by hand and whose cursors G294 holds.
"""
import os
import shutil
import sys

PLAY = os.getcwd()
# tag: scenario, theater, side
RUNS = {'t': ('SCU02EA', 'temperate', 'USSR'), 's': ('SCG03EA', 'snow', 'Greece')}
# every mouse state drawn on the ground: cursors.h name -> (mesh prefix, no-entry, animates)
STATES = {
    'normal': ('ECUR00', 0, 0), 'no-move': ('ECUR02', 0, 0), 'move': ('ECUR01', 0, 0),
    'enter': ('ECUR0BF', 0, 1), 'deploy': ('ECUR06', 0, 1), 'select': ('ECUR03', 0, 1),
    'attack': ('ECUR04', 0, 1), 'sell': ('ECUR07', 0, 0), 'sell-unit': ('ECUR07', 0, 0),
    'repair': ('ECUR05', 0, 1), 'no-repair': ('ECUR05', 1, 0), 'no-sell': ('ECUR07', 1, 0),
    'nuke': ('ECUR09', 0, 1), 'airstrike': ('ECUR08', 0, 1), 'demolitions': ('ECUR0C', 0, 0),
    'guard': ('ECUR0D', 0, 0), 'heal': ('EHEAL', 0, 1), 'damage': ('EDAMAGE', 0, 1),
    'grepair': ('EGREPAIR', 0, 1), 'stay-attack': ('ECUR04', 0, 1), 'no-deploy': ('ECUR06', 1, 0),
    'no-enter': ('ECUR0BF0', 1, 0), 'no-grepair': ('EGREPAIR', 1, 0),
    'chrono-select': ('ECHRONO_SELECT', 0, 1), 'chrono-dest': ('ECHRONO_DEST', 0, 1),
}
OWN = ('heal', 'damage', 'grepair', 'no-grepair', 'chrono-select', 'chrono-dest')
# the animated states whose every clock frame is sampled (8 ticks, a third of a cycle and more)
SAMPLED = ('heal', 'damage', 'grepair', 'chrono-select', 'chrono-dest', 'no-grepair')
CUR = (640, 380)          # where the pointer stands for the drawn states (1280 x 720)


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def ini(path):
    return open(path, errors='replace').read().replace('\r', '').splitlines()


def section(L, name):
    out, on = [], False
    for l in L:
        if l.startswith('['):
            on = l.strip() == '[' + name + ']'
        elif on and '=' in l:
            out.append(l.split('=', 1)[1].split(','))
    return out


def mission(d, tag):
    """The run's copy of the missions; for s, the three things the probe test points at."""
    scen, _, side = RUNS[tag]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = ini(path)
    m = dict(l.split('=', 1) for l in L[L.index('[Map]') + 1:] if '=' in l and not l.startswith('['))
    x0, y0 = int(m['X']), int(m['Y'])
    h = int(m['Height'])
    # the camera on the player's first structure or unit
    cell = None
    for sec in ('STRUCTURES', 'UNITS', 'INFANTRY'):
        for f in section(L, sec):
            if f[0] == side:
                cell = int(f[3])
                break
        if cell is not None:
            break
    cx, cy = cell % 128, cell // 128
    if tag == 's':
        # (the medic and engineer of Allied 3 stand on cell 57,67: a rifleman at a quarter
        # of his strength beside them, and a power plant of the player's at 100 of 256)
        i = L.index('[INFANTRY]')
        L.insert(i + 1, f'90={side},E1,64,{67 * 128 + 58},0,Guard,128,None')
        i = L.index('[STRUCTURES]')
        L.insert(i + 1, f'90={side},POWR,100,{64 * 128 + 60},0,None,1,0')
        write(path, L)
    return cx, cy, x0, y0, h


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag in RUNS:
        cx, cy, x0, y0, h = mission(d, tag)
        v = ['tick 2', 'reveal 1', 'tick 10']
        if tag == 's':
            # first, while the rifleman and the engineer still stand where they were put
            v += ['echo GATE|probe|heal', 'look MEDI:Greece 0', 'tick 1', 'lclickobj MEDI:Greece 0',
                  'tick 1', 'cursorobj E1:Greece 0', f'shot {d}/s_probe_heal.png', 'cursor3ddump',
                  'echo GATE|probe|grepair', 'lclickobj E6:Greece 0', 'tick 1',
                  'cursorobj POWR:Greece 0', f'shot {d}/s_probe_grepair.png', 'cursor3ddump',
                  'deselect', 'tick 1']
        v += [f'cam {cx} {cy}', 'tick 1', 'frames 1', f'cursor {CUR[0]} {CUR[1]}']
        for name in STATES:
            v += [f'echo GATE|state|{name}', f'cursor3dmouse {name}', f'shot {d}/{tag}_{name}.png',
                  'cursor3ddump']
            if name in SAMPLED:
                for _ in range(8):
                    v += ['tick 1', 'cursor3ddump']
        v += ['cursor3dmouse -1']
        # the west edge: the arrow while the view can still go west, then with the view
        # against the edge. The map's exported rectangle has a one-cell collar, which the
        # camera's clamp and the arrow's test both take as the map.
        ey = y0 + h // 2
        wx = x0 - 1 + 0.02
        for what, cam in (('scroll', x0 + 6), ('no-scroll', x0 - 6)):
            v += [f'echo GATE|state|{what}', f'cam {cam} {ey}', 'tick 1',
                  f'cursorat {wx:.2f} {ey + 0.5}', f'shot {d}/{tag}_{what}.png', 'cursor3ddump']
        # the radar: nothing selected, the pointer over a cell of the radar map
        v += ['echo GATE|state|radar', f'cam {cx} {cy}', 'tick 1', f'cursorradar {cx} {cy}',
              f'shot {d}/{tag}_radar.png', 'cursor3ddump']
        write(os.path.join(d, tag + '.txt'), v + ['quit'])


def kv(line):
    out = {}
    for part in line.strip().split('|'):
        if '=' in part:
            k, val = part.split('=', 1)
            out[k] = val
    return out


def crop_diff(a, b, at, r=40):
    from PIL import Image, ImageChops
    box = (at[0] - r, at[1] - r, at[0] + r, at[1] + r)
    d = ImageChops.difference(Image.open(a).convert('RGB').crop(box),
                              Image.open(b).convert('RGB').crop(box)).convert('L')
    return sum(1 for p in d.tobytes() if p > 24)


def blocks(lines):
    """The log cut at each GATE echo: {state: [CURSOR3DRA dicts]}, and the parts' scales."""
    out, cur = {}, None
    for l in lines:
        if l.startswith('ECHO|GATE|'):
            l = l[5:]
            cur = l.strip().split('|', 2)[2] if l.startswith('GATE|state|') else 'probe-' + l.strip().split('|')[2]
            out[cur] = {'dumps': [], 'parts': [], 'other': []}
        elif cur and l.startswith('CURSOR3DRA|'):
            out[cur]['dumps'].append(kv(l))
            out[cur]['parts'].append([])
        elif cur and l.startswith('CURSOR3DRAPART|') and out[cur]['parts']:
            out[cur]['parts'][-1].append(float(kv(l)['scale']))
        elif cur:
            out[cur]['other'].append(l)
    return out


def check_run(d, tag):
    scen, theater, side = RUNS[tag]
    label = f'{tag} ({scen}, {theater}, {"Soviet" if side == "USSR" else "Allied"})'
    path = os.path.join(d, tag + '.log')
    if not os.path.isfile(path):
        return f'{label}: no log'
    lines = open(path, errors='replace').read().splitlines()
    att = [kv(l) for l in lines if l.startswith('ECURRA|attached|')]
    if not any(l.startswith('ECURRA|loaded|meshes 12|') for l in lines) or not att \
            or not lines[lines.index(next(l for l in lines if l.startswith('ECURRA|attached|')))].startswith('ECURRA|attached|12 of 12'):
        return f'{label}: Red Alert\'s cursor file did not load and attach its 12 meshes'
    B = blocks(lines)
    for name, (mesh, noentry, anim) in STATES.items():
        b = B.get(name)
        if not b or not b['dumps']:
            return f'{label}: {name} was never drawn'
        k = b['dumps'][0]
        if k.get('mouse') != name or not k.get('mesh', '').startswith(mesh) or k.get('ecur') != '1':
            return f'{label}: {name} drew {k.get("mesh")} (ecur {k.get("ecur")}), wanted {mesh}'
        if k.get('own') != ('1' if name in OWN else '0'):
            return f'{label}: {name}\'s mesh {k.get("mesh")} is {"not " if name in OWN else ""}Red Alert\'s own'
        if int(k['noentry']) != noentry or (int(k['frames']) == 100) != bool(anim):
            return f'{label}: {name} took the row noentry={k["noentry"]} frames={k["frames"]}'
        shot = os.path.join(d, f'{tag}_{name}.png')
        if not os.path.isfile(shot):
            return f'{label}: {name}\'s shot is missing'
        if name != 'normal' and crop_diff(shot, os.path.join(d, f'{tag}_normal.png'), CUR) < 40:
            return f'{label}: {name}\'s picture is the plain pointer\'s where the cursor stands'
    # the animations
    def seen(name, key):
        return [k.get(key) for k in B[name]['dumps']]
    for name in ('heal', 'damage', 'chrono-select'):
        fl = set(seen(name, 'flip'))
        if len(fl) < 2:
            return f'{label}: {name}\'s flipbook did not step ({sorted(fl)})'
    sp = set(seen('grepair', 'spin'))
    if len(sp) < 4 or '-1' in sp:
        return f'{label}: the gold spanner did not turn ({sorted(sp)})'
    if set(seen('no-grepair', 'spin')) != {'-1'}:
        return f'{label}: the gold spanner turned under its sign'
    rings = [p for p in B['chrono-dest']['parts'] if len(p) == 3]
    if len(rings) < 5 or any(p[0] != 1.0 for p in rings) or len({p[1] for p in rings}) < 4 \
            or not any(abs(p[1] - p[2]) > 0.3 for p in rings):
        return f'{label}: the chronosphere target\'s rings did not close half a cycle apart ({rings[:3]})'
    # the edge
    for name, want in (('scroll', '0'), ('no-scroll', '1')):
        k = B[name]['dumps'][-1] if B.get(name) and B[name]['dumps'] else {}
        if k.get('edge') != '192' or k.get('blocked') != want:
            return f'{label}: {name} at the west edge drew edge {k.get("edge")} blocked {k.get("blocked")}'
    if crop_diff(os.path.join(d, f'{tag}_scroll.png'), os.path.join(d, f'{tag}_no-scroll.png'),
                 (640, 360), 360) < 40:
        return f'{label}: the scroll and no-scroll pictures are the same'
    # the radar
    rd = B.get('radar')
    cr = [kv(l) for l in (rd['other'] if rd else []) if l.startswith('CURSORRADAR|')]
    if not cr or cr[0].get('shape') != 'radar' or cr[0].get('onmap') != '0':
        return f'{label}: over the radar the pointer read {cr[:1]}'
    before = int(B['no-scroll']['dumps'][-1]['overlays'])
    if not rd['dumps'] or int(rd['dumps'][-1]['overlays']) <= before or rd['dumps'][-1].get('mesh') != 'ERADAR_CURSOR':
        return f'{label}: the radar frame was not drawn over the radar ({rd["dumps"][-1:] if rd else None})'
    msg = (f'{label}: {len(STATES)} states on the ground each its approved model, the six refused under '
           f'the sign, flipbooks, spin and rings moving, the west edge arrow plain then refused, the '
           f'radar frame over the radar')
    if tag == 's':
        for name, want in (('heal', 'heal'), ('grepair', 'grepair')):
            b = B.get('probe-' + name)
            co = [kv(l) for l in (b['other'] if b else []) if l.startswith('CURSOROBJ|')]
            if not co or co[0].get('shape') != want or not b['dumps'] or b['dumps'][-1].get('mouse') != want:
                return f'{label}: the engine\'s answer for the {name} test read {co[:1] and co[0].get("shape")}'
        msg += '; the probe gives HEAL for the medic and GREPAIR for the engineer'
    return None, msg


def check(d):
    if not os.path.isfile(os.path.join(PLAY, 'ra3d/ecur/ecur_ra.bin')):
        print('0 G354 Red Alert 3D\'s cursors: no ra3d/ecur/ecur_ra.bin staged (tools/ra3d/stage-structures.py)')
        return
    msgs = []
    for tag in RUNS:
        r = check_run(d, tag)
        if isinstance(r, str):
            print(f'0 G354 Red Alert 3D\'s cursors: {r}')
            return
        msgs.append(r[1])
    print('1 G354 Red Alert 3D draws its remastered cursors under Enhanced: ' + '; '.join(msgs))


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        sys.exit(__doc__)
    (script if sys.argv[1] == 'script' else check)(sys.argv[2])
