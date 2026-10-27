#!/usr/bin/env python3
"""G358: Red Alert's ore, gems and ore mine are their remastered models under Enhanced
(game/ra3d_ore_mod.h; the files baked by tools/ra3d/bake-ore.py into data/ra3d_ore and staged
by tools/ra3d/stage-structures.py).

    python3 game/gate_ra3dore.py script DIR     write the runs' scripts
    python3 game/gate_ra3dore.py check DIR      read the logs: "1 message" or "0 message"

Run from a play folder with a staged Red Alert module. Hidden runs, every one Enhanced:

  at   Allied 5 (temperate): its ore field in view; then 2,100 ticks watched, a frame
       drawn and the cells dumped every 50: the Soviets' Ore Truck harvesting, the
       field growing and spreading
  as   Allied 1 (snow): its ore field and its gems; 1,200 ticks watched (its Ore Truck)
  st   Soviet 9 (temperate): an ore mine among ore and gems
  ss   Soviet 4 (snow): an ore mine among ore and gems; 2,100 ticks watched (growth)

Claims (each from the runs' `raore` lines, which read the engine's map, the cells' states and
what the last frame drew):
  (1) every run: the theater's pack is read (RATEMP the temperate one, RASNOW the snow one),
      ore cells in view are drawn as their models (cells, pieces, triangles) and no 1996 ore
      decal is drawn; with `raore 0` the decals are back and no model is drawn, and the
      pictures differ;
  (2) several densities: the ore cells drawn in Allied 5 and Soviet 4 stand at four
      different density frames at least; gem cells are drawn in the three runs that have
      gems in view;
  (3) a harvester takes a cell down: a cell's frame falls between two dumps, and the cell
      the model shows follows it (its target frame is the engine's on the next dump); a
      cell is harvested away (gone from the map);
  (4) the field grows: a cell's frame rises and a new cell appears (growth, a spread);
  (5) the ore mines are their models in their theater's look (MINE_TEM on temperate,
      MINE_WIN on snow), every mine on the map drawn by it and no mine card left; with
      `raore 0` the cards are back;
  (6) the shine: the pack carries both sets' specular maps, and the nuggets and stones in
      view draw their normals and their specular maps into the light pass's targets (so
      the sun's highlight lies on them); with `raore 0` they draw neither.
PROVEN TO FAIL: a renderer without ra3d_ore_mod.h answers no `raore` (a script failure)
and draws the 1996 decals and the mine's card; a renderer or a pack without the shine
reports no specular maps or no pieces in the targets (claim 6).
NOT COVERED: how they look (the shots, by eye); Tiberian Dawn's Tiberium (its pictures
are compared byte for byte outside this gate).
"""
import os
import re
import sys

RUNS = {
    'at': ('SCG05EA', 'temperate', 'RATEMP', (56.5, 68.5), 2100),
    'as': ('SCG01EA', 'snow', 'RASNOW', (73.5, 53.5), 1200),
    'st': ('SCU09EA', 'temperate', 'RATEMP', (80.5, 84.5), 0),
    'ss': ('SCU04EA', 'snow', 'RASNOW', (70.5, 87.5), 2100),
}
GEMS = {'as': (52.0, 64.0), 'st': (32.0, 103.0), 'ss': (70.5, 87.5)}
MINES = ('st', 'ss')


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, (scen, theater, _, (x, y), watch) in RUNS.items():
        v = ['tick 2', 'reveal 1', 'tick 20', f'cam {x} {y}', 'dist 1920', 'tick 1',
             'frames 1', 'raore', f'shot {d}/{tag}_on.png',
             'raore 0', 'tick 1', 'frames 1', 'raore', f'shot {d}/{tag}_off.png',
             'raore 1', 'tick 1', 'frames 1', 'raore']
        if tag in GEMS:
            gx, gy = GEMS[tag]
            v += [f'cam {gx} {gy}', 'tick 1', 'frames 1', 'raore', f'shot {d}/{tag}_gems.png',
                  f'cam {x} {y}', 'tick 1']
        for k in range(watch // 50):
            v += ['tick 50', 'frames 1', 'raore']
        if watch:
            v += [f'shot {d}/{tag}_after.png']
        write(os.path.join(d, tag + '.txt'), v + ['quit'])


# ---- the check ------------------------------------------------------------------------
def kv(line):
    out = {}
    for part in line.strip().split('|'):
        if '=' in part:
            k, val = part.split('=', 1)
            out[k] = val
    return out


def dumps(log):
    """The `raore` dumps in order: (cells {(x, y): (kind, frame, shown)}, mines, end)."""
    out, cells, mines = [], {}, []
    for l in open(log, errors='replace'):
        if l.startswith('RA3DORE|cell|'):
            p = l.strip().split('|')
            d = kv(l)
            x, y = (int(c) for c in d['cell'].split(','))
            cells[(x, y)] = (p[2], int(d['frame']), int(d['shown']), int(d['view']), int(d['built']))
        elif l.startswith('RA3DORE|mine|'):
            mines.append(kv(l))
        elif l.startswith('RA3DORE|end|'):
            out.append((cells, mines, kv(l)))
            cells, mines = {}, []
    return out


def pngdiff(a, b):
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return -1
    A = Image.open(a).convert('RGB')
    B = Image.open(b).convert('RGB')
    d = ImageChops.difference(A, B).convert('L')
    return sum(d.histogram()[25:])


def check(d):
    fails, notes = [], []
    harvested = grown = gone = new = followed = 0
    for tag, (scen, theater, pack, _, watch) in RUNS.items():
        log = os.path.join(d, tag + '.log')
        if not os.path.isfile(log):
            fails.append(f'{tag}: no log')
            continue
        text = open(log, errors='replace').read()
        if not re.search(r'^SCRIPT\|end .*: \d+ lines, 0 failures$', text, re.M):
            fails.append(f'{tag} ({scen}): its script did not run cleanly')
        D = dumps(log)
        if len(D) < 3:
            fails.append(f'{tag} ({scen}): no raore dumps (a renderer without the ore models)')
            continue
        on, off = D[0][2], D[1][2]
        # (1) drawn as models, decals stood down; off: the decals back
        if on.get('theater') != pack or on.get('loaded') != ('2' if pack == 'RASNOW' else '0'):
            fails.append(f'{tag}: the {theater} pack is not the one read ({on.get("theater")}, {on.get("loaded")})')
        if on.get('on') != '1' or int(on.get('drawn', 0)) < 10 or int(on.get('pieces', 0)) <= 0:
            fails.append(f'{tag}: the ore in view is not drawn as its models (drawn {on.get("drawn")}, '
                         f'pieces {on.get("pieces")})')
        if int(on.get('oredecals', -1)) != 0:
            fails.append(f'{tag}: {on.get("oredecals")} 1996 ore decals drawn under the models')
        if int(off.get('oredecals', 0)) <= 0 or int(off.get('drawn', 1)) != 0:
            fails.append(f'{tag}: raore 0 does not bring the 1996 decals back')
        diff = pngdiff(os.path.join(d, tag + '_on.png'), os.path.join(d, tag + '_off.png'))
        if 0 <= diff < 20000:
            fails.append(f'{tag}: the models and the 1996 decals draw the same picture ({diff} pixels differ)')
        # (6) the shine: both sets carry their specular maps, and the pieces in view drew
        # their normals and their maps into the light pass's targets; with raore 0, nothing
        if on.get('specmaps') != '2':
            fails.append(f'{tag}: the pack carries {on.get("specmaps")} specular maps, not 2')
        if int(on.get('nrmtris', 0)) <= 0 or int(on.get('spectris', 0)) <= 0:
            fails.append(f'{tag}: the pieces drew no normals ({on.get("nrmtris")}) or no shine '
                         f'({on.get("spectris")}) for the light pass')
        if int(off.get('nrmtris', 1)) != 0 or int(off.get('spectris', 1)) != 0:
            fails.append(f'{tag}: raore 0 still draws the pieces\' normals or shine')
        # (2) densities and gems
        fr = [int(v) for v in on.get('frames', '').split(',') if v != '']
        distinct = sum(1 for v in fr if v > 0)
        if tag in ('at', 'ss') and distinct < 4:
            fails.append(f'{tag}: the cells drawn stand at {distinct} density frames, not several')
        if tag in GEMS:
            g = D[3][0] if len(D) > 3 else {}
            gems = [c for c, v in g.items() if v[0].startswith('GEM') and v[3] and v[4]]
            if not gems:
                fails.append(f'{tag}: no gem cell drawn as its model')
            else:
                notes.append(f'{tag}: {len(gems)} gem cells drawn')
        # (5) the mines
        if tag in MINES:
            mines = D[0][1]
            want = '2' if pack == 'RASNOW' else '0'
            if not mines:
                fails.append(f'{tag}: no ore mine on the map')
            elif any(m['ready'] != '1' or m['model'] == '-1' or m['theater'] != want for m in mines):
                fails.append(f'{tag}: an ore mine is not its {theater} model')
            if int(on.get('minesdrawn', 0)) != len(mines) or int(on.get('minecards', 1)) != 0:
                fails.append(f'{tag}: {on.get("minecards")} mine cards drawn beside the models')
            if int(off.get('minecards', 0)) <= 0 or int(off.get('minesdrawn', 1)) != 0:
                fails.append(f'{tag}: raore 0 does not bring the mine card back')
        # (3) and (4) the watch
        W = D[3 + (1 if tag in GEMS else 0) - 1:]
        for i in range(1, len(W)):
            a, b = W[i - 1][0], W[i][0]
            for c, va in a.items():
                vb = b.get(c)
                if vb is None:
                    gone += 1
                elif vb[1] < va[1]:
                    harvested += 1
                    # the model goes on to the engine's frame
                    if vb[2] == vb[1]:
                        followed += 1
                elif vb[1] > va[1]:
                    grown += 1
            new += sum(1 for c in b if c not in a)
        if watch:
            notes.append(f'{tag}: watched {watch} ticks')
    if harvested == 0:
        fails.append('no cell was harvested down in any run')
    elif followed == 0:
        fails.append('the models never followed a harvested cell down')
    if gone == 0:
        fails.append('no cell was harvested away')
    if grown == 0 or new == 0:
        fails.append(f'the field did not grow (rises {grown}, new cells {new})')
    msg = (f'ore, gems and mines as their models on Allied and Soviet, temperate and snow; '
           f'{harvested} harvest steps ({followed} followed), {gone} cells taken away, '
           f'{grown} growth steps, {new} new cells; ' + '; '.join(notes))
    if fails:
        return '0 ' + '; '.join(fails)
    return '1 ' + msg


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        raise SystemExit(__doc__)
    if sys.argv[1] == 'script':
        script(sys.argv[2])
    else:
        print(check(sys.argv[2]))
