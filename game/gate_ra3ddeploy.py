#!/usr/bin/env python3
"""G346: Red Alert 3D's Construction Yard deploys out of its MCV under Enhanced, as the
remastered placement plays it (game/ebld_place.h with game/eplc_ra/FACT.eplc, staged into
ra3d/eplc by tools/ra3d/stage-structures.py): the slab comes up under the lorry, the gantry
lifts off and becomes the yard's crane, the drum turns onto the hall's line and settles into
the vault, the lorry sinks into the slab, the office and the rest rise.

  gate_ra3ddeploy.py script DIR    write the missions and the run scripts into DIR
  gate_ra3ddeploy.py check DIR     read the runs' logs; first line is "1 message" or "0 message"

Run from a play folder with the Red Alert module staged, the MCV and the placement among it.

THE RUNS, each an MCV clicked to deploy on the tick after the mission starts (a private copy of
the module's missions where one is changed), sampled every two ticks through its deploy:

  st  Soviet 5 (temperate, the player USSR): the mission's own Soviet MCV; then a Power Plant
      built and placed from the new yard;
  ss  Soviet 4, second version (snow, the player USSR): the mission's own Soviet MCV; then a
      Power Plant built and placed from the new yard;
  at  Allied 9 (temperate, the player Greece): an Allied MCV added by the Allies' start,
      facing south-east so it turns to deploy; then a Power Plant built and placed from the
      new yard;
  as  Allied 2 (snow, the player Greece): the same at the Allies' start there.

CLAIMS, in each run:
  (1) the MCV is drawn by its model while it stands (RA3DPROBE vehicle model=1) and turns to
      deploy facing south-west, as the engine turns it (its last facing sampled within a
      tick's turn of 160);
  (2) the yard's placement starts the tick the MCV goes (EPLC|start at the frame of the MCV's
      UNITDEATH, whose verdict is no death), with the MCV it deploys from attached
      (EPLCMODEL companion runs) and drawn while it plays, and plays to its end (EPLC|end
      why=done, 3.9 s), the yard then standing as its model;
  (3) the yard's 1996 build-up card never draws while the placement owns it (RA3DPROBE place:
      models counted on the frames it plays, cards 0);
  (4) the new yard works: a Power Plant built from it is placed and stands (a new POWR in the
      building list, going up).
PROVEN TO FAIL: the renderer before this work reads Tiberian Dawn's placements under Red Alert
(none attaches: "0 of 25 placements attached"), draws the yard's 1996 build-up card and has no
RA3DPROBE place line.
NOT COVERED: how it looks (the proof sheets, the in-game frames beside the approved lab strip);
selling the yard (the placement played backwards); Classic, which keeps the 1996 build-up.
"""
import os
import re
import shutil
import sys

PLAY = os.getcwd()
RUNS = {
    # tag: scenario, house, the MCV's cell, change (None: the mission's own MCV; 'add': one
    # added), facing of an added one, whether to build after
    'st': ('SCU05EA', 'USSR', (22, 80), None, 0, True),
    'ss': ('SCU04EB', 'USSR', (33, 90), None, 0, True),
    'at': ('SCG09EA', 'Greece', (40, 90), 'add', 96, True),
    'as': ('SCG02EA', 'Greece', (86, 51), 'add', 96, True),
}
SAMPLES = 50


def cell(x, y):
    return y * 128 + x


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def add_unit(L, row):
    st = next(i for i, l in enumerate(L) if l.strip() == '[UNITS]')
    en = st + 1
    while en < len(L) and not L[en].startswith('['):
        en += 1
    nums = [int(m.group(1)) for l in L[st + 1:en] for m in [re.match(r'(\d+)=', l)] if m]
    while en > st + 1 and not L[en - 1].strip():
        en -= 1
    return L[:en] + [f'{max(nums) + 1 if nums else 0}={row}'] + L[en:]


def mission(d, tag):
    scen, house, (x, y), change, face, _ = RUNS[tag]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().splitlines()
    if change == 'add':
        L = add_unit(L, f'{house},MCV,256,{cell(x, y)},{face},Guard,None')
    write(path, L)


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, (scen, house, (x, y), change, face, build) in RUNS.items():
        mission(d, tag)
        v = ['reveal 1', 'tick 2', f'cam {x} {y}', f'lclickobj MCV:{house} 0', f'actclickobj MCV:{house} 0']
        for k in range(SAMPLES):
            v += ['tick 2', f'cam {x} {y}', f'shot {d}/{tag}_{k:02d}.png' if k % 4 == 0 else f'shot {d}/{tag}.png',
                  'ra3dprobe', 'eplcdump']
        if build:
            v += ['tick 10', 'anims', 'sbclickitem POWR', 'ready POWR 3000', 'startplace POWR', f'cam {x} {y}']
            for cx, cy in ((x + 2, y - 1), (x + 2, y), (x - 1, y + 2), (x - 3, y - 1), (x - 1, y - 3)):
                v += [f'hover {cx} {cy}', f'lclickcell {cx} {cy}', 'tick 2']
            v += ['tick 30', f'cam {x} {y}', f'shot {d}/{tag}_built.png', 'anims']
        v += ['quit']
        write(f'{d}/{tag}.txt', v)


def read(path):
    try:
        return open(path, errors='replace').read()
    except OSError:
        return ''


def kv(s):
    return dict(p.split('=', 1) for p in s.split('|') if '=' in p)


def check(d):
    fails, notes = [], []
    for tag, (scen, house, (x, y), change, face, build) in RUNS.items():
        text = read(f'{d}/{tag}.log')
        lines = text.splitlines()
        name = {'st': 'Soviet, temperate', 'ss': 'Soviet, snow', 'at': 'Allied, temperate',
                'as': 'Allied, snow'}[tag]
        # (a run whose mission ended under it shows its end screen, not the deploy)
        if 'GAMEOVER|' in text:
            fails.append(f'{name}: the mission ended during the run')
            continue
        # (1) the MCV, by its model, turning to the south-west
        mcv = [kv(l) for l in lines if l.startswith('RA3DPROBE|vehicle|MCV|') and f'house={house}' in l
               and l.split('|')[4].startswith('kind=')]
        mcv = [m for m in mcv if m.get('at', '').startswith(f'{x}.')]
        deaths = [kv(m.group(0)) for m in re.finditer(r'UNITDEATH\|MCV\|[^\s]*', text)]
        death = next((dd for dd in deaths if dd.get('at', '').startswith(f'{x}.5')), None)
        starts = [kv(m.group(0)) for m in re.finditer(r'EPLC\|start\|[^\s]*', text)]
        ends = [kv(m.group(0)) for m in re.finditer(r'EPLC\|end\|[^\s]*', text)]
        # (sampled every two ticks: the last sample within a tick's turn of it)
        if not mcv or any(m['model'] != '1' for m in mcv) or abs(int(mcv[-1].get('face', -99)) - 160) > 10:
            fails.append(f'{name}: the MCV standing: {len(mcv)} samples, models {sorted({m.get("model") for m in mcv})}, '
                         f'last facing {mcv[-1].get("face") if mcv else None}')
            continue
        # (2) the placement from the tick it goes
        st = next((s for s in starts if s.get('type') == 'FACT' and s.get('mode') == 'place'), None)
        en = next((e for e in ends if st and e.get('key') == st.get('key')), None)
        model = [kv(m.group(0)) for m in re.finditer(r'EPLCMODEL\|FACT\|[^\s]*', text)]
        comp = max((int(m.get('companion', 0)) for m in model), default=0)
        drawn = max((int(m.get('drawn', 0)) for m in model), default=0)
        if not death or death.get('verdict') == 'shatter' or not st or st.get('frame') != death.get('frame') \
           or not en or en.get('why') != 'done' or comp <= 0 or drawn <= 0:
            fails.append(f'{name}: the deploy: the MCV went {death and death.get("frame")} ({death and death.get("verdict")}), '
                         f'the placement started {st and st.get("frame")}, ended {en and en.get("why")}, '
                         f'{comp} companion runs, drawn {drawn}')
            continue
        # (3) no build-up card while it plays
        place = [kv(l) for l in lines if l.startswith('RA3DPROBE|place|')]
        owned = sum(1 for p in place if int(p['models']) > 0)
        cards = sum(int(p['cards']) for p in place)
        if owned < 10 or cards:
            fails.append(f'{name}: the yard owned by its placement on {owned} samples, its card drawn on {cards}')
            continue
        msg = (f'{name}: the MCV turns to the south-west and the yard deploys out of it from the tick it '
               f'goes (frame {st["frame"]} to {en["frame"]}), no build-up card')
        # (4) the yard works
        if build:
            cut = text.find('SCRIPT|sbclickitem|POWR')
            before = set(re.findall(r'ANIM\|POWR\|id=(\d+)', text[:cut])) if cut >= 0 else set()
            after = set(re.findall(r'ANIM\|POWR\|id=(\d+)', text[cut:])) if cut >= 0 else set()
            if cut < 0 or not (after - before):
                fails.append(f'{name}: no Power Plant was placed from the new yard')
                continue
            msg += ', a Power Plant built from it'
        notes.append(msg)
    if fails:
        print('0 ' + '; '.join(fails))
    else:
        print('1 Red Alert 3D deploys its Construction Yard out of the MCV: ' + '; '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit(__doc__)
