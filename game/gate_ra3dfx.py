#!/usr/bin/env python3
"""G355: Red Alert 3D plays its own effects under Enhanced (game/ra3d_efxm_mod.h; the
recipes of its own in game/efx_enhanced.h; ra3d/efx/<ANIM>.efxm and efx_ra.pack, written by
the effects' own generators when tools/ra3d/stage-structures.py stages the module).

    python3 game/gate_ra3dfx.py script DIR     write the runs' missions and scripts
    python3 game/gate_ra3dfx.py check DIR      read the logs: "1 message" or "0 message"

Run from a play folder with a staged Red Alert module. Four hidden runs, Enhanced, each
mission's own 3D scene:

  t  Allied 5 (temperate) and  s  Allied 1 (snow): every family raised where the camera
     looks, one after the other, every tick drawn and sampled:
       the 18 crate and power-up pick-ups, the move flash, the GPS rocket and the flag
       (`efxanim`, the engine's own anim);
       the 5 reuses (NAPALM1-3, the eight MINIGUN facings, SMOKLAND);
       the 13 particle effects (FBALL1, FB2, FLAK, SMOKE_M, FLMSPT, TWINKLE1-3, ELECTRO,
       ELECTDOG on land, H2O_EXP1-3 on open water);
       a rifleman set down (`spawnobj`) and killed (`hurtcell`), so the engine lays a real
       corpse under his 3D body.
  pt Soviet 2 (temperate) and  ps  Soviet 1 (snow): a yard and an airfield added for the
     player at tech level 8, the paratroopers and the parabombs called in from the sidebar,
     a real drop each.

Claims, every run in its theater:
  (1) the module's 26 forms and its own sheets loaded (RA3DEFXM|loaded, efx_ra.pack);
  (2) every particle effect fired its own recipe (<ANIM>_RA) and spawned particles, every
      reuse its approved Tiberian Dawn recipe (NAPALM1-3, GUN_ a facing, LZ_SMOKE), and the
      own sheets ARC, SPLASH, TWINKLE, TWINKLE2 and TWINKLE3 were drawn;
  (3) every one-shot form was drawn on every frame of its file, each frame drawing exactly
      the parts the staged file has visible on it, and the flag over a whole wave;
  (4) no anim drawn here kept its 1996 card, but for the electrocutions, which keep theirs
      on purpose (the engine deletes the soldier), and a corpse on no 3D body;
  (5) the real corpse found the 3D soldier's body, drew its stain and its card stood down;
  (6) the parachutes and the parabombs' canopies were drawn on their real drops, over their
      loads, coming down;
  (7) the pictures: each phase's shot differs from the one before it was raised.
Tiberian Dawn is not run here: its pictures are compared byte for byte before and after by
hand. G353 holds the atom bomb, G342 the effects shared with Tiberian Dawn.

PROVEN TO FAIL: the renderer before this work prints no RA3DEFXM line, fires the large
fireball, the flak and the flame puff as Tiberian Dawn's recipes and draws every other anim
named here as its 1996 card.
NOT COVERED: how they look (the frame strips, by eye); a crate picked up by a unit (the
anims are raised by `efxanim`, the engine's own, wherever the engine raises them); a move
order's flash raised by a click (likewise).
"""
import os
import re
import shutil
import struct
import sys

PLAY = os.getcwd()

CRATES = [(42, 'DEVIATOR'), (43, 'DOLLAR'), (44, 'EARTH'), (45, 'EMPULSE'), (46, 'INVUN'),
          (47, 'MINE'), (48, 'RAPID'), (49, 'STEALTH2'), (50, 'MISSILE2'), (61, 'CHRONBOX'),
          (62, 'GPSBOX'), (63, 'INVULBOX'), (64, 'PARABOX'), (65, 'SONARBOX'), (73, 'ARMOR'),
          (74, 'SPEED'), (75, 'FPOWER'), (76, 'TQUAKE')]
MISC = [(51, 'MOVEFLSH'), (59, 'SPUTDOOR'), (79, 'FLAGFLY')]
REUSE = [(7, 'NAPALM1'), (8, 'NAPALM2'), (9, 'NAPALM3'), (41, 'SMOKLAND')] + \
        [(33 + k, 'MINIGUN') for k in range(8)]
PART_LAND = [(0, 'FBALL1'), (1, 'FB2'), (69, 'FLAK'), (18, 'SMOKE_M'), (52, 'FLMSPT'),
             (66, 'TWINKLE1'), (67, 'TWINKLE2'), (68, 'TWINKLE3'), (53, 'ELECTRO'), (55, 'ELECTDOG')]
PART_WATER = [(70, 'H2O_EXP1'), (71, 'H2O_EXP2'), (72, 'H2O_EXP3')]
OWN_RECIPE = [n for _, n in PART_LAND + PART_WATER] + ['ARMOR', 'CHRONBOX', 'DOLLAR', 'INVUN',
                                                       'PARABOX', 'SPEED', 'TQUAKE', 'SPUTDOOR']
SHEETS = ('ARC', 'SPLASH', 'TWINKLE', 'TWINKLE2', 'TWINKLE3')
KEPT = ('ELECTRO', 'ELECTDOG')
FORMS = [n for _, n in CRATES + MISC] + ['PARACH', 'PARABOMB', 'CORPSE1', 'CORPSE2', 'CORPSE3']

# tag: scenario, theater, the land the effects go off on, open water, a free cell for the
# rifleman
RUNS = {
    't': ('SCG05EA', 'temperate', (30, 60), (55, 54), (32, 62)),
    's': ('SCG01EA', 'snow', (72, 53), (63, 76), (73, 53)),
}
DROPS = {
    'pt': ('SCU02EA', 'temperate', (82, 55), (79, 50), (83, 47)),
    'ps': ('SCU01EA', 'snow', (38, 80), (35, 74), (39, 72)),
}


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def efxm_visible(path):
    """Per frame, how many parts the file draws (visible, alpha above nothing)."""
    b = open(path, 'rb').read()
    assert b[:4] == b'EFXM'
    ver, frames, ntex, nparts = struct.unpack_from('<4I', b, 4)
    o = 20
    for _ in range(ntex):
        w, h = struct.unpack_from('<II', b, o + 8)
        o += 16 + w * h * 4
    for _ in range(nparts):
        ntris = struct.unpack_from('<I', b, o + 28)[0]
        o += 32 + ntris * 3 * 24
    on = []
    for f in range(frames):
        n = 0
        for p in range(nparts):
            drawn = b[o + 48]
            a = struct.unpack_from('<f', b, o + 52)[0]
            n += 1 if drawn and a > 0.002 else 0
            o += 68
        on.append(n)
    return frames, on


def grid(n, cols, x0, y0, step):
    return [(x0 + (i % cols) * step, y0 + (i // cols) * step) for i in range(n)]


def sample(v, ticks, shots=(), tag=''):
    for k in range(ticks):
        v += ['tick 1', 'frames 1', 'efxmdump']
        if k in shots:
            v.append(f'shot {tag}_{k:03d}.png')


def script_run(d, tag):
    scen, _, (bx, by), (wx, wy), (rx, ry) = RUNS[tag]
    p = f'{d}/{tag}'
    v = ['tick 2', 'reveal 1', 'tick 20', 'dist 1500']
    # the crates, a grid of six by three a cell and a half apart
    v += [f'cam {bx} {by}', 'tick 1', 'frames 1', f'shot {p}_crates_before.png']
    for (ty, _), (x, y) in zip(CRATES, grid(len(CRATES), 6, bx - 3.75, by - 1.5, 1.5)):
        v.append(f'efxanim {ty} {x:.2f} {y:.2f}')
    sample(v, 92, (8, 16, 30), f'{p}_crates')
    # the move flash, the GPS rocket and the flag, and the reuses beside them
    v += ['frames 1', f'shot {p}_misc_before.png']
    for (ty, _), (x, y) in zip(MISC, grid(len(MISC), 3, bx - 2.0, by - 1.0, 2.0)):
        v.append(f'efxanim {ty} {x:.2f} {y:.2f}')
    for (ty, _), (x, y) in zip(REUSE, grid(len(REUSE), 6, bx - 3.75, by + 1.0, 1.5)):
        v.append(f'efxanim {ty} {x:.2f} {y:.2f}')
    sample(v, 40, (3, 12, 24), f'{p}_misc')
    v += ['efxdump']
    # the particle effects on land
    v += ['frames 1', f'shot {p}_part_before.png']
    for (ty, _), (x, y) in zip(PART_LAND, grid(len(PART_LAND), 5, bx - 4.0, by - 1.0, 2.0)):
        v.append(f'efxanim {ty} {x:.2f} {y:.2f}')
    sample(v, 40, (2, 6, 14, 30), f'{p}_part')
    v += ['efxdump']
    # on open water
    v += [f'cam {wx} {wy}', 'tick 1', 'frames 1', f'shot {p}_water_before.png']
    for (ty, _), (x, y) in zip(PART_WATER, grid(len(PART_WATER), 3, wx - 1.5, wy, 1.5)):
        v.append(f'efxanim {ty} {x + 0.5:.2f} {y + 0.5:.2f}')
    sample(v, 16, (2, 5, 9), f'{p}_water')
    v += ['efxdump']
    # a rifleman of the player's own, killed where he stands: the engine's own corpse
    v += [f'cam {rx} {ry}', f'spawnobj E1 {rx} {ry}', 'tick 2', 'frames 1', f'shot {p}_corpse_before.png',
          f'hurtcell {rx} {ry}']
    sample(v, 120, (40, 70, 100), f'{p}_corpse')
    write(f'{d}/{tag}.txt', v + ['efxdump', 'quit'])


def mission(d, tag):
    scen, _, (x, y), _, _ = DROPS[tag]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().splitlines()
    st = next(i for i, l in enumerate(L) if l.strip() == '[STRUCTURES]')
    # (a yard, without which the sidebar is empty, and an airfield, which gives the drops)
    L.insert(st + 1, f'90=USSR,FACT,256,{y * 128 + x},0,None,1,0')
    L.insert(st + 2, f'91=USSR,AFLD,256,{y * 128 + x + 4},0,None,1,0')
    h = next(i for i, l in enumerate(L) if l.strip() == '[USSR]')
    e = h + 1
    while e < len(L) and not L[e].startswith('['):
        e += 1
    L[h + 1:e] = [l for l in L[h + 1:e] if not l.startswith('TechLevel=')]
    # (8: the paratroopers come at 5 and the parabombs at 8 (RULES.INI ParaTech,
    # ParabombTech); higher fills the structures tab past the special weapons)
    L.insert(h + 1, 'TechLevel=8')
    write(path, L)


def script_drop(d, tag):
    scen, _, _, (px, py), (bx, by) = DROPS[tag]
    mission(d, tag)
    p = f'{d}/{tag}'
    v = ['tick 2', 'reveal 1', 'tick 90', f'cam {px} {py}', 'dist 1100', 'tick 1', 'superrecharge', 'tick 2',
         'sbclickitem SW_ParaInfantry', 'tick 1', f'actcell {px} {py}', 'tick 2',
         'sbclickitem SW_ParaBomb', 'tick 1', f'actcell {bx} {by}']
    sample(v, 160, tuple(range(40, 160, 20)), f'{p}_inf')
    v += [f'cam {bx} {by}']
    sample(v, 140, tuple(range(0, 140, 20)), f'{p}_bomb')
    write(f'{d}/{tag}.txt', v + ['efxdump', 'quit'])


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag in RUNS:
        script_run(d, tag)
    for tag in DROPS:
        script_drop(d, tag)


def kv(line):
    out = {}
    for part in line.strip().split('|'):
        if '=' in part:
            k, v = part.split('=', 1)
            out[k] = v
    return out


def diff_frac(a, b):
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return None
    d = ImageChops.difference(Image.open(a).convert('RGB'), Image.open(b).convert('RGB')).convert('L')
    return d.point(lambda v: 255 if v > 24 else 0).histogram()[255] / float(d.size[0] * d.size[1])


def read(d, tag):
    path = os.path.join(d, tag + '.log')
    return open(path, errors='replace').read().splitlines() if os.path.isfile(path) else None


def common(lines, label):
    if not any(l.startswith('SCRIPT|end') and '0 failures' in l for l in lines):
        return f'{label}: the script did not run to its end without a failure'
    ld = [kv(l) for l in lines if l.startswith('RA3DEFXM|loaded|')]
    if not ld or ld[0].get('forms') != '26/26':
        return f'{label}: the forms did not all load ({ld[:1]})'
    if not any("Red Alert's own effect art" in l and '5 sequences' in l for l in lines):
        return f'{label}: efx_ra.pack was not read with its five sheets'
    return None


def draws(lines):
    """{(anim, id): {frame: (on, parts, ylift)}}"""
    out = {}
    for l in lines:
        if l.startswith('RA3DEFXM|draw|'):
            f = l.split('|')
            k = kv(l)
            at = k['at'].split(',')
            out.setdefault((f[2], k['id']), {})[int(k['frame'])] = (int(k['on']), int(k['parts']), float(at[2]))
    return out


def check_run(d, tag, files):
    scen, theater, _, _, _ = RUNS[tag]
    label = f'{tag} ({scen}, {theater})'
    lines = read(d, tag)
    if lines is None:
        return f'{label}: no log'
    bad = common(lines, label)
    if bad:
        return bad
    text = '\n'.join(lines)
    # (2) the recipes and the sheets
    # (every dump's line: the anim's recipe as last fired, and the particles its name has
    # spawned so far; a building dying in the mission can fire FBALL1 as its blast too)
    spawn = {}
    for l in lines:
        m = re.match(r'^EFXSPAWN\|([^|]+)\|recipe=ANIM_([^|]+)\|particles=(\d+)', l)
        if m:
            spawn.setdefault(m.group(1), set()).add((m.group(2), int(m.group(3)) > 0))
    for name in OWN_RECIPE:
        if (name + '_RA', True) not in spawn.get(name, set()):
            return f'{label}: {name} fired {sorted(spawn.get(name, []))}, not its own recipe {name}_RA with particles'
    for name, want in (('NAPALM1', 'NAPALM1'), ('NAPALM2', 'NAPALM2'), ('NAPALM3', 'NAPALM3'),
                       ('SMOKLAND', 'LZ_SMOKE'), ('MINIGUN', 'GUN_')):
        if not any(r.startswith(want) and n for r, n in spawn.get(name, set())):
            return f'{label}: the reuse {name} fired {sorted(spawn.get(name, []))}, not {want}'
    seqs = {}
    for l in lines:
        m = re.match(r'^EFXDRAWSEQ\|slot=\d+\|([^|]+)\|enh=(\d+)', l)
        if m:
            seqs[m.group(1)] = max(seqs.get(m.group(1), 0), int(m.group(2)))
    missing = [s for s in SHEETS if seqs.get(s, 0) <= 0]
    if missing:
        return f'{label}: the own sheets {missing} were never drawn'
    # (3) the forms raised by efxanim, every frame, part for part
    dr = draws(lines)
    for name in [n for _, n in CRATES + MISC]:
        inst = [v for (a, _), v in dr.items() if a == name]
        if len(inst) != 1:
            return f'{label}: {name} drawn as {len(inst)} forms, not one'
        got = inst[0]
        frames, on = files[name]
        if name == 'FLAGFLY':
            if len(got) < 14:
                return f'{label}: the flag was drawn on {len(got)} frames, not a whole wave'
        else:
            gone = [f for f in range(frames) if f not in got]
            if gone:
                return f'{label}: {name} never drew frames {gone[:6]} of its {frames}'
        wrong = [f for f, (o, p, _) in got.items() if p != on[f] or o != on[f]]
        if wrong:
            f = wrong[0]
            return f'{label}: {name} frame {f} drew {got[f][1]} parts, the file has {on[f]} on'
    # (4) the cards
    cards = set()
    for l in lines:
        if l.startswith('RA3DEFXM|cards|'):
            cards.update(n for n in l.split('|', 2)[2].split(',') if n)
    kept = [n for n in KEPT if n not in cards]
    if kept:
        return f'{label}: the electrocutions {kept} did not keep their 1996 cards'
    stray = sorted(cards & (set(n for _, n in CRATES + MISC + PART_LAND + PART_WATER + REUSE) - set(KEPT)))
    if stray:
        return f'{label}: {stray} were still drawn as their 1996 cards'
    # (5) the corpse
    # (the rifleman's own, at the cell he was killed on: a mission's battle lays others)
    rx, ry = RUNS[tag][4]
    mine = [l for l in lines if l.startswith('RA3DEFXM|body|')
            and abs(float(kv(l)['cell'].split(',')[0]) - rx - 0.5) < 1.0
            and abs(float(kv(l)['cell'].split(',')[1]) - ry - 0.5) < 1.0]
    if not mine:
        return f'{label}: the rifleman killed on {rx},{ry} left no corpse on his 3D body'
    cid = kv(mine[0])['id']
    cname = mine[0].split('|')[2]
    cd = dr.get((cname, cid), {})
    if len(cd) < 10:
        return f'{label}: the corpse {cname} drew its stain on {len(cd)} frames'
    after = False
    for l in lines:
        if l == mine[0]:
            after = True
        elif after and l.startswith('RA3DEFXM|cards|') and cname in l.split('|', 2)[2].split(','):
            return f'{label}: the corpse on a 3D body kept its 1996 card'
    # (7) the pictures
    notes = []
    for ph, k in (('crates', 16), ('misc', 12), ('part', 6), ('water', 5), ('corpse', 70)):
        a, b = f'{d}/{tag}_{ph}_before.png', f'{d}/{tag}_{ph}_{k:03d}.png'
        if not (os.path.isfile(a) and os.path.isfile(b)):
            return f'{label}: the {ph} shots are missing'
        df = diff_frac(a, b)
        if df is not None:
            if df < 0.004:
                return f'{label}: the {ph} shot is the picture before ({df:.4f} of it changed)'
            notes.append(f'{ph} {df:.1%}')
    n = sum(len(v) for v in dr.values())
    return None, (f'{label}: {len(OWN_RECIPE)} recipes of its own and the 5 reuses fired, the 5 own '
                  f'sheets drawn, 21 forms drawn frame for frame ({n} frames), the electrocutions\' '
                  f'cards kept and no other, {cname} on a 3D body; changed {", ".join(notes)}')


def check_drop(d, tag):
    scen, theater, _, _, _ = DROPS[tag]
    label = f'{tag} ({scen}, {theater}, the drops)'
    lines = read(d, tag)
    if lines is None:
        return f'{label}: no log'
    bad = common(lines, label)
    if bad:
        return bad
    dr = draws(lines)
    msgs = []
    for name, least in (('PARACH', 3), ('PARABOMB', 1)):
        inst = {k: v for k, v in dr.items() if k[0] == name}
        if len(inst) < least:
            return f'{label}: {len(inst)} {name} drawn, wanted at least {least}'
        falls = []
        for v in inst.values():
            fr = sorted(v)
            if len(fr) < 10:
                continue
            falls.append(v[fr[0]][2] - v[fr[-1]][2])
        if not falls or max(falls) < 0.3:
            return f'{label}: no {name} came down (its lift fell by {max(falls) if falls else 0:.2f} at most)'
        msgs.append(f'{len(inst)} {name} coming down by up to {max(falls):.2f}')
    cards = set()
    for l in lines:
        if l.startswith('RA3DEFXM|cards|'):
            cards.update(n for n in l.split('|', 2)[2].split(',') if n)
    if cards & {'PARACH', 'PARABOMB'}:
        return f'{label}: a parachute was still drawn as its 1996 card'
    return None, f'{label}: ' + ', '.join(msgs)


def check(d):
    files = {}
    for name in FORMS:
        path = os.path.join(PLAY, 'ra3d/efx', name + '.efxm')
        if not os.path.isfile(path):
            print(f'0 G355 Red Alert 3D\'s own effects: no ra3d/efx/{name}.efxm staged (tools/ra3d/stage-structures.py)')
            return
        files[name] = efxm_visible(path)
    msgs = []
    for tag in RUNS:
        r = check_run(d, tag, files)
        if isinstance(r, str):
            print(f'0 G355 Red Alert 3D\'s own effects: {r}')
            return
        msgs.append(r[1])
    for tag in DROPS:
        r = check_drop(d, tag)
        if isinstance(r, str):
            print(f'0 G355 Red Alert 3D\'s own effects: {r}')
            return
        msgs.append(r[1])
    print('1 G355 Red Alert 3D plays its own effects under Enhanced: ' + '; '.join(msgs))


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        sys.exit(__doc__)
    (script if sys.argv[1] == 'script' else check)(sys.argv[2])
