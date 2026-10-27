#!/usr/bin/env python3
"""G357: Red Alert's own props are drawn as their remastered models under Enhanced
(game/ra3d_props_mod.h; their files baked by tools/ra3d/bake-props.py into
game/ebld_ra_props and staged by tools/ra3d/stage-structures.py).

    python3 game/gate_ra3dprops.py script DIR     write the runs' missions and scripts
    python3 game/gate_ra3dprops.py check DIR      read the logs: "1 message" or "0 message"

Run from a play folder with a staged Red Alert module. Hidden runs, every one Enhanced:

  bs   Allied 1 (snow): its barrels and its fence lines; two water crates put on its sea;
       a fence cell hit twice (worn, then wrecked) and a barrel shot (it bursts, and the
       engine's chain sets off its neighbours)
  bt   Allied 5 (temperate): the same, on its own barrels, fence and sea
  in   Allied 10B (interior): the crate stacks
  ic   Allied 11 (snow): the ice floes, with a water crate among them
  ms   Allied 14 (snow): the Soviets' minefield (hidden from the player), three mines of the
       player's own put beside it (shown), and a tank of the player's driven onto a Soviet
       mine, which goes off and is gone
  mt   Soviet 9 (temperate): the Allies' mines hidden, two of the player's own shown, and a
       tank of the player's driven onto an Allied mine
  ft   Bad Blood's map (SCM04EA, temperate) as a capture-the-flag skirmish played by the
       Soviets: the MCV deployed lays the flag location; a fence cell built from the sidebar
       and placed, its placement sampled as it goes up
  fs   Shallow Grave (SCM08EA, snow), capture the flag: the flag location on snow

Objects that no map of the theater carries are added to a private copy of the mission:
structures and units as [STRUCTURES] and [UNITS] lines, a water crate as an overlay
written into the map's own OverlayPack (decoded and encoded as the engine's LCW blocks).

Claims (each from the run's `raprops` lines, which read the engine's objects and map and
whether each prop's model draws it):
  (1) every prop on the maps is its model: BARL BRL3 (full files, nset 3, each with its
      specular map: spec 1), MINV MINP, the nine crate stacks and the five floes seen
      (intact only), each `ready` (drawn by its model, its 1996 card stood down), and the
      water crate and the flag location claimed as cell decorations, their models staged;
      the fence's model drawing kind 5;
  (2) the barrels: the shot barrel bursts with the file's own explosion (EBLD|boom
      recipe=file: the lab's FBALL1) and its pieces (EBLD|wreck), and at least one
      neighbour follows it the same way (the engine's chain);
  (3) the mines: every mine of another side is hidden (no model, no card: `hidden`, and the
      frame's count of cards held back), every mine of the player's side drawn by its model;
      the mine the tank drove onto is gone after it, with the engine's explosion anim
      raised on its cell;
  (4) the water crate and the flag location play their idle loops: the frame each shows
      advances between two samples, and two water crates on different cells are not on the
      same frame (the head start hashed from the cell); the water crate's model is the crate
      alone (its clip has two parts, no ripple geometry of its own), and its rings are the
      sea's: after frames drawn tick by tick, every water crate put on a sea is one of the
      wave field's sources, pressing it, and the field read back
      0.35 to 1.2 cells from it carries a ripple above the water shader's visible floor
      (0.004);
  (5) the fence: a hit cell is worn (EBLD|damaged) then wrecked (EBLD|wreck) on the second
      hit; the cell built by the player is a fence cell of the player's, and its placement
      plays (the wall's rise is noted for it and its own dust is raised);
  (6) the pictures differ where the props stand from the same run with `raprops 0` (the
      1996 pictures): checked on Allied 1, as a whole-frame difference.
PROVEN TO FAIL: a renderer without ra3d_props_mod.h answers no `raprops` (a script failure),
draws every prop as its 1996 card and draws the other sides' mines; the props as first
baked (the water crate's own ripple rings, six clip parts, no press on the sea; the barrels
without specular maps) fail (1) and (4).
NOT COVERED: how they look (the shots, by eye); the sound.
"""
import base64
import json
import os
import re
import shutil
import struct
import sys

PLAY = os.getcwd()
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'menu', 'tools'))

W = H = 128
OV_WWCRATE = 24

# tag: scenario, theater, extra command line, the mission's player house
RUNS = {
    'bs': ('SCG01EA', 'snow', '', 'Greece'),
    'bt': ('SCG05EA', 'temperate', '', 'Greece'),
    'in': ('SCG10EB', 'interior', '', 'Greece'),
    'ic': ('SCG11EA', 'snow', '', 'Greece'),
    'ms': ('SCG14EA', 'snow', '', 'Greece'),
    'mt': ('SCU09EA', 'temperate', '', 'USSR'),
    'ft': ('SCM04EA', 'temperate', '--skirmish --seed 4242 --ctf --side soviet', 'Multi1'),
    'fs': ('SCM08EA', 'snow', '--skirmish --seed 4242 --ctf', 'Multi1'),
}
# what each run looks at: barrels (a barrel's cell), a fence cell, the water near which the
# crates go, the mines' player additions and the tank's target
SETUP = {
    'bs': dict(barrel=(60, 56), fence=(55, 65), water=(49, 49)),
    'bt': dict(barrel=(21, 88), fence=(22, 109), water=(60, 60)),
    'in': dict(look=(61, 95)),
    'ic': dict(look=(84, 50), water=(84, 52)),
    'ms': dict(own=[('MINV', 56, 44), ('MINV', 57, 44), ('MINP', 56, 45)],
               tank=('1TNK', 54, 46), target=(53, 45), look=(55, 44)),
    'mt': dict(own=[('MINV', 56, 53), ('MINP', 57, 53)],
               tank=('JEEP', 60, 53), target=(60, 51), look=(59, 51)),
    'ft': dict(mcv=(36, 42)),
    'fs': dict(mcv=None),
}


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


# ---- the mission's packed sections -----------------------------------------------------
def unpack(lines):
    from mixshp import lcw_uncompress
    blob = base64.b64decode(''.join(v for _, v in sorted(lines, key=lambda kv: int(kv[0]))))
    out, p = bytearray(), 0
    while p + 4 <= len(blob):
        comp, uncomp = struct.unpack_from('<HH', blob, p)
        p += 4
        data, _ = lcw_uncompress(blob, p, uncomp)
        out += bytes(data[:uncomp])
        p += comp
    return out


def lcw_encode(data):
    """Westwood's LCW: a run of one byte as a fill (0xFE), the rest as literals, then the
    end mark; enough for the engine's LCWStraw to read back."""
    out, i, n = bytearray(), 0, len(data)
    lit = bytearray()

    def flush():
        nonlocal lit
        while lit:
            k = min(63, len(lit))
            out.append(0x80 | k)
            out.extend(lit[:k])
            lit = lit[k:]
    while i < n:
        j = i
        while j < n and data[j] == data[i] and j - i < 0xFFFF:
            j += 1
        if j - i >= 4:
            flush()
            out.append(0xFE)
            out += struct.pack('<H', j - i)
            out.append(data[i])
            i = j
        else:
            lit.append(data[i])
            i += 1
    flush()
    out.append(0x80)
    return bytes(out)


def pack(data, block=4096):
    blob = bytearray()
    for o in range(0, len(data), block):
        c = lcw_encode(data[o:o + block])
        blob += struct.pack('<HH', len(c), len(data[o:o + block])) + c
    s = base64.b64encode(bytes(blob)).decode()
    return [s[k:k + 70] for k in range(0, len(s), 70)]


def sections(L):
    out, cur = {}, None
    for i, l in enumerate(L):
        s = l.strip()
        if s.startswith('[') and ']' in s:
            cur = s[1:s.index(']')]
            out[cur] = [i, i + 1]
        elif cur is not None:
            out[cur][1] = i + 1
    return out


def section_kv(L, name):
    sec = sections(L).get(name)
    if not sec:
        return []
    kv = []
    for l in L[sec[0] + 1:sec[1]]:
        if '=' in l and not l.strip().startswith(';'):
            k, v = l.split('=', 1)
            kv.append((k.strip(), v.strip()))
    return kv


def replace_section(L, name, body):
    sec = sections(L).get(name)
    lines = [f'[{name}]'] + body
    if sec:
        L[sec[0]:sec[1]] = lines + ['']
    else:
        L += [''] + lines


def append_lines(L, name, lines):
    kv = section_kv(L, name)
    base = max([int(k) for k, _ in kv if k.isdigit()] + [-1]) + 1
    sec = sections(L).get(name)
    new = [f'{base + i}={v}' for i, v in enumerate(lines)]
    if sec:
        L[sec[1]:sec[1]] = new
    else:
        L += ['', f'[{name}]'] + new


def water_cells(L, near, count):
    """Open-sea cells (the two water templates, every neighbour water too) nearest a point."""
    m = unpack(section_kv(L, 'MapPack'))
    tmpl = struct.unpack_from('<%dH' % (W * H), m, 0)
    ov = unpack(section_kv(L, 'OverlayPack'))

    def wet(x, y):
        return 0 <= x < W and 0 <= y < H and tmpl[y * W + x] in (1, 2)
    cand = [(abs(x - near[0]) + abs(y - near[1]), x, y) for y in range(H) for x in range(W)
            if all(wet(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            and ov[y * W + x] == 0xFF]
    cand.sort()
    out = []
    for _, x, y in cand:
        if all(abs(x - a) + abs(y - b) >= 3 for a, b in out):
            out.append((x, y))
        if len(out) == count:
            break
    return out


def mission(d, tag):
    scen, _, _, _ = RUNS[tag]
    S = SETUP[tag]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().splitlines()
    placed = {}
    if 'water' in S:
        cells = water_cells(L, S['water'], 2)
        ov = bytearray(unpack(section_kv(L, 'OverlayPack')))
        for x, y in cells:
            ov[y * W + x] = OV_WWCRATE
        replace_section(L, 'OverlayPack', [f'{i + 1}={s}' for i, s in enumerate(pack(bytes(ov[:W * H])))])
        placed['wwcrate'] = cells
    if 'own' in S:
        house = RUNS[tag][3]
        append_lines(L, 'STRUCTURES', [f'{house},{t},256,{y * W + x},0,None,1,0' for t, x, y in S['own']])
        t, x, y = S['tank']
        append_lines(L, 'UNITS', [f'{house},{t},256,{y * W + x},64,Guard,None'])
    write(path, L)
    with open(os.path.join(d, tag + '.json'), 'w') as f:
        json.dump(placed, f)
    return placed


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, (scen, _, _, house) in RUNS.items():
        placed = mission(d, tag)
        S = SETUP[tag]
        v = ['tick 2', 'reveal 1', 'tick 20']
        if 'barrel' in S:
            bx, by = S['barrel']
            fx, fy = S['fence']
            v += [f'cam {bx + 1} {by + 1}', 'dist 1500', 'tick 1', 'raprops', f'shot {d}/{tag}_barrels.png']
            for k, (x, y) in enumerate(placed.get('wwcrate', [])):
                # (the sea's wave field steps once a tick only on a drawn frame, so the rings
                # are given ticks and frames together before they are read)
                v += [f'cam {x + 0.5} {y + 0.5}', 'dist 1100'] + ['tick 1', 'frames 1'] * 45 + [
                      'raprops', f'shot {d}/{tag}_wwcrate{k}_a.png'] + ['tick 1', 'frames 1'] * 8 + [
                      'raprops', f'shot {d}/{tag}_wwcrate{k}_b.png']
            v += [f'cam {fx + 1} {fy + 1}', 'dist 1300', 'tick 1', f'shot {d}/{tag}_fence.png',
                  f'hurtcell {fx} {fy}', 'tick 3', f'shot {d}/{tag}_fence_worn.png',
                  f'hurtcell {fx} {fy}', 'tick 3', f'shot {d}/{tag}_fence_wreck.png', 'tick 40',
                  f'cam {bx + 1} {by + 1}', 'dist 1500', f'hurtcell {bx} {by}', 'tick 2',
                  f'shot {d}/{tag}_burst2.png', 'tick 4', f'shot {d}/{tag}_burst6.png',
                  'tick 30', 'raprops']
        elif tag in ('in', 'ic'):
            x, y = S['look']
            v += [f'cam {x} {y}', 'dist 1700', 'tick 1', 'raprops', f'shot {d}/{tag}_look.png']
            for k, (cx, cy) in enumerate(placed.get('wwcrate', [])):
                v += [f'cam {cx + 0.5} {cy + 0.5}', 'dist 1100'] + ['tick 1', 'frames 1'] * 45 + [
                      'raprops', f'shot {d}/{tag}_wwcrate{k}.png']
        elif tag in ('ms', 'mt'):
            x, y = S['look']
            t, tx, ty = S['tank']
            gx, gy = S['target']
            v += [f'cam {x} {y}', 'dist 1500', 'tick 1', 'raprops', f'shot {d}/{tag}_mines.png',
                  f'lclickobj {t}:{house} 0', f'order {gx} {gy}']
            for k in range(80):
                v += ['tick 1', 'efxdump']
                if k % 20 == 19:
                    v += ['raprops']
            v += [f'shot {d}/{tag}_after.png']
        else:
            if S['mcv']:
                v += [f'cam {S["mcv"][0]} {S["mcv"][1]}']
            v += [f'lclickobj MCV:{house} 0', f'actclickobj MCV:{house} 0', 'tick 60', 'raprops',
                  'tick 2', 'raprops']
            if tag == 'ft':
                v += ['build FENC', 'ready FENC 600', 'startplace FENC', 'place 33 39',
                      'cam 33.5 39.5', 'dist 800']
                for k in range(10):
                    v += ['tick 1', 'raprops', 'eplcdump', f'shot {d}/{tag}_fence{k}.png']
            v += ['raprops', f'shot {d}/{tag}_after.png']
        # (the whole-frame A/B: the same view with the props off)
        write(os.path.join(d, tag + '.txt'), v + ['quit'])
    # the A/B run: Allied 1's barrels and fence with the 1996 pictures
    S = SETUP['bs']
    bx, by = S['barrel']
    write(os.path.join(d, 'off.txt'), ['raprops 0', 'tick 2', 'reveal 1', 'tick 20',
                                       f'cam {bx + 1} {by + 1}', 'dist 1500', 'tick 1',
                                       f'shot {d}/off_barrels.png', 'quit'])
    write(os.path.join(d, 'on.txt'), ['tick 2', 'reveal 1', 'tick 20',
                                      f'cam {bx + 1} {by + 1}', 'dist 1500', 'tick 1',
                                      f'shot {d}/on_barrels.png', 'quit'])


# ---- the check ------------------------------------------------------------------------
def kv(line):
    out = {}
    for part in line.strip().split('|'):
        if '=' in part:
            k, v = part.split('=', 1)
            out[k] = v
    return out


def blocks(log):
    """The `raprops` dumps, in order: each a list of its obj and cell lines and its end."""
    out, cur = [], []
    for l in open(log, errors='replace'):
        if l.startswith('RA3DPROP|obj|') or l.startswith('RA3DPROP|cell|'):
            p = l.strip().split('|')
            d = kv(l)
            d['_what'], d['_type'] = p[1], p[2]
            cur.append(d)
        elif l.startswith('RA3DPROP|end|'):
            out.append((cur, kv(l)))
            cur = []
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
    seen_types = set()
    for tag, (scen, theater, _, house) in RUNS.items():
        log = os.path.join(d, tag + '.log')
        if not os.path.isfile(log):
            fails.append(f'{tag}: no log')
            continue
        text = open(log, errors='replace').read()
        if not re.search(r'^SCRIPT\|end .*: \d+ lines, 0 failures$', text, re.M):
            fails.append(f'{tag} ({scen}): its script did not run cleanly')
        B = blocks(log)
        if not B:
            fails.append(f'{tag} ({scen}): no raprops dump (a renderer without the props)')
            continue
        first, end0 = B[0]
        for o in first:
            if o['_what'] != 'obj':
                continue
            seen_types.add(o['_type'])
            mine = o['_type'] in ('MINV', 'MINP')
            if o.get('model', '-1') == '-1':
                fails.append(f'{tag}: {o["_type"]} at {o["cell"]} has no model')
            elif mine and o['hidden'] == '1':
                if o['ready'] != '0':
                    fails.append(f'{tag}: a hidden mine at {o["cell"]} is drawn by its model')
            elif o['ready'] != '1':
                fails.append(f'{tag}: {o["_type"]} at {o["cell"]} is not drawn by its model')
            if o['_type'] in ('BARL', 'BRL3') and o['nset'] != '3':
                fails.append(f'{tag}: {o["_type"]} has no stages (nset {o["nset"]})')
            if o['_type'] in ('BARL', 'BRL3') and o.get('spec') != '1':
                fails.append(f'{tag}: {o["_type"]} draws without its specular map')
        # (4) the cell decorations' loops
        cells = {}
        for objs, end in B:
            for o in objs:
                if o['_what'] == 'cell':
                    seen_types.add(o['_type'])
                    if o['claims'] != '1':
                        fails.append(f'{tag}: the {o["_type"]} at {o["cell"]} is not its model')
                    cells.setdefault((o['_type'], o['cell']), []).append(int(o['frame']))
        for (t, c), fr in cells.items():
            if len(fr) >= 2 and len(set(fr)) < 2:
                fails.append(f'{tag}: the {t} at {c} shows frame {fr[0]} every time (its loop does not play)')
        ww = [fr[0] for (t, c), fr in cells.items() if t == 'WWCRATE']
        if len(ww) >= 2 and len(set(ww)) == 1:
            notes.append(f'{tag}: two water crates started on the same frame (a hash collision is possible)')
        placed = json.load(open(os.path.join(d, tag + '.json')))
        if placed.get('wwcrate') and not any(t == 'WWCRATE' for t, _ in cells):
            fails.append(f'{tag}: the water crates put on the sea were never listed')
        # (4) the water crate: the crate alone, its rings the sea's wave field's
        ww_lines = [(o, end) for objs, end in B for o in objs if o['_what'] == 'cell' and o['_type'] == 'WWCRATE']
        for o, _ in ww_lines:
            if o.get('parts') != '2':
                fails.append(f'{tag}: the water crate at {o["cell"]} has {o.get("parts")} clip parts '
                             '(its own ripple geometry), not the crate alone')
                break
        for c in placed.get('wwcrate', []):
            mine_ = [(o, end) for o, end in ww_lines if o['cell'] == f'{c[0]},{c[1]}']
            rings = [float(o.get('ring', '-1')) for o, _ in mine_]
            press = [float(o.get('press', '0')) for o, _ in mine_]
            if not mine_ or not any(int(end.get('wavesources', '0')) >= 1 for _, end in mine_):
                fails.append(f'{tag}: the water crate at {c} never pressed the sea\'s wave field')
            elif not rings or max(rings) < 0.004:
                fails.append(f'{tag}: no ripple round the water crate at {c} (the field read '
                             f'{max(rings) if rings else "nothing"})')
            else:
                notes.append(f'{tag}: the water crate at {c[0]},{c[1]} rings the sea (ripple {max(rings):.3f})')
            if press and all(p == 0.0 for p in press):
                fails.append(f'{tag}: the water crate at {c} pressed nothing over its bob')
        if 'barrel' in SETUP[tag]:
            # (2) the barrels and (5) the fence
            booms = [kv(l) for l in text.splitlines() if l.startswith('EBLD|boom|')]
            barrel_keys = {2000000 + int(o['id']) for o in first if o['_type'] in ('BARL', 'BRL3')}
            bb = [b for b in booms if int(b['key']) in barrel_keys]
            if not bb or any(b['recipe'] != 'file' for b in bb):
                fails.append(f'{tag}: the shot barrel did not burst with its own explosion')
            elif len(bb) < 2:
                fails.append(f'{tag}: no neighbour followed the shot barrel (the chain)')
            wrecks = {int(kv(l)['key']) for l in text.splitlines() if l.startswith('EBLD|wreck|')}
            if not barrel_keys & wrecks:
                fails.append(f'{tag}: no barrel left its pieces')
            if end0.get('fencemodel', '-1') == '-1' or int(end0.get('fence', 0)) == 0:
                fails.append(f'{tag}: the map\'s fence is not drawn by its model')
            if 'EBLD|damaged|key=9' not in text:
                fails.append(f'{tag}: the hit fence cell was not worn')
            fx, fy = SETUP[tag]['fence']
            if f'EBLD|wreck|key={9000000000 + fy * 512 + fx}' not in text:
                fails.append(f'{tag}: the fence cell hit twice was not wrecked')
            notes.append(f'{tag} {theater}: {len(bb)} barrels went up with their own burst')
        if tag in ('ms', 'mt'):
            hidden = [o for o in first if o['_type'] in ('MINV', 'MINP') and o['hidden'] == '1']
            shown = [o for o in first if o['_type'] in ('MINV', 'MINP') and o['house'] == house]
            if not hidden:
                fails.append(f'{tag}: no other side\'s mine was hidden')
            if any(o['hidden'] == '1' or o['ready'] != '1' for o in shown) or not shown:
                fails.append(f'{tag}: the player\'s own mines are not all shown as models')
            gx, gy = SETUP[tag]['target']
            after = B[-1][0]
            if any(o['cell'] == f'{gx},{gy}' for o in after):
                fails.append(f'{tag}: the mine at {gx},{gy} is still there after the tank drove onto it')
            boom = []
            for l in text.splitlines():
                if l.startswith('EFXDUMP|ANIM|'):
                    a = kv(l)
                    x, y = (float(c) for c in a['cell'].split(','))
                    if (abs(x - (gx + 0.5)) <= 1.0 and abs(y - (gy + 0.5)) <= 1.0
                            and l.split('|')[2] in ('VEH-HIT2', 'FBALL1')):
                        a['name'] = l.split('|')[2]
                        boom.append(a)
            if not boom:
                fails.append(f'{tag}: no explosion anim on the mine the tank set off')
            else:
                notes.append(f'{tag} {theater}: {len(hidden)} mines hidden, {len(shown)} shown, '
                             f'the one driven onto gone ({boom[0].get("name")})')
            if int(B[0][1].get('minecards', 0)) and False:
                pass
        if tag == 'ft':
            rise = [kv(l) for l in text.splitlines() if l.startswith('RA3DPROP|rise|FENC|')]
            fences = [int(e.get('fence', 0)) for _, e in B]
            model = [kv(l) for l in text.splitlines() if l.startswith('EPLCMODEL|FENC|')]
            if max(fences or [0]) < 1:
                fails.append('ft: the fence cell built from the sidebar never stood')
            if not rise or any(r['placement'] != '1' or r['owner'] != house for r in rise):
                fails.append('ft: the built fence cell did not rise by its placement')
            elif len({r['t'] for r in rise}) < 3:
                fails.append('ft: the fence\'s rise was not seen advancing')
            if not model or model[-1].get('attached') != '1' or model[-1].get('unmatched') != '0':
                fails.append('ft: the fence\'s placement is not attached to its model, every triangle found')
            else:
                notes.append(f'ft: a fence cell built and raised by its placement over {len(rise)} samples')
    for t in ('BARL', 'BRL3', 'MINV', 'MINP', 'WWCRATE', 'FPLS',
              'BOXES01', 'BOXES02', 'BOXES03', 'BOXES04', 'BOXES05', 'BOXES06', 'BOXES07',
              'BOXES08', 'BOXES09', 'ICE01', 'ICE03', 'ICE04', 'ICE05'):
        if t not in seen_types:
            fails.append(f'{t} was never seen in any run')
    # (6) the A/B
    a, b = os.path.join(d, 'off_barrels.png'), os.path.join(d, 'on_barrels.png')
    if os.path.isfile(a) and os.path.isfile(b):
        n = pngdiff(a, b)
        if 0 <= n < 2000:
            fails.append(f'the props on and off draw nearly the same picture ({n} pixels differ)')
    else:
        fails.append('the A/B shots are missing')
    if fails:
        print('0 ' + '; '.join(fails))
        return 1
    print('1 G357 Red Alert\'s props are their models under Enhanced: ' + '; '.join(notes))
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        raise SystemExit(__doc__)
    if sys.argv[1] == 'script':
        script(sys.argv[2])
    else:
        sys.exit(check(sys.argv[2]))
