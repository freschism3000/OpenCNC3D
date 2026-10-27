#!/usr/bin/env python3
"""G338: Red Alert 3D draws the art it shares with OpenCNC3D in OpenCNC3D's remasters.

  gate_ra3dreuse.py script DIR    write the five run scripts into DIR
  gate_ra3dreuse.py check DIR     read the runs' logs and pictures; first line is
                                  "1 message" or "0 message"

The Red Alert lab approves, row by row, the Tiberian Dawn remasters whose 1996 art is
Tiberian Dawn's own (tools/art/remaster/reviews-ra.json, phase reuse;
tools/ra3d/stage-reuse.py stages their files). The runs (all hidden, Enhanced on each
mission's own 3D scene):

  vil    Allied 5B (temperate): the village with the remasters and, in the same frame,
         without (`ebld 0`); then the church and a T12 tree are hit until they die, and
         the run goes on until the wreck and the snag are gone.
  snow   Soviet 1 (snow): its village, several houses already hurt at the start, with and
         without the remasters; its wooden fence with and without the wall pass.
  marks  Soviet 9 (temperate): its craters and scorch marks with the remastered decals and
         without (`decalenh 0`); its sandbags, chain-link fence and concrete walls with
         and without the wall pass; a fence corner hit twice and a sandbag once.
  smark  Allied 3B (snow): its crater with and without the remastered decal.
  trees  Allied 5 (temperate): every tree's record.

Claims:
  (1) the civilian buildings are their reused models (MISS and V01 to V07 in the
      village), and the village frame differs from the same frame without them;
  (2) every such model stands on its own plot: its box's centre inside the engine's
      footprint and the box within the footprint and 0.75 of a cell round it;
  (3) the houses first seen hurt are worn or broken (stage 1 or 2), at least one broken;
      the church that dies goes to its wreck and the wreck is gone in the end, its
      engine's death craters held back (EBLD standdown smudge lines);
  (4) on temperate ground every tree of the eight reused types is its model and every
      other tree its 1996 card; each model stands on the engine's trunk point inside the
      tree's own cells; the T12 that dies is its charred snag, outlives the engine's tree
      and is gone in the end;
  (5) on snow the reused assets wear their winter looks, the one file Tiberian Dawn's winter
      draws (<T>_WIN.ebld): every house of Soviet 1's village and every tree of a reused
      type is drawn from its winter file, no summer (_TEM) tree file is read there, and
      every other tree is its 1996 card;
  (6) the eight craters and scorch marks of Soviet 9 are drawn in the remastered art, none
      with the dial off, and the frames differ; the snow crater is remastered too and its
      burn does not tint the snow (pixels changed by the decal that turned peach: fewer
      than 300; the temperate burn on snow measured 1976 of them);
  (7) the walls and fences are their reused pieces (SBAG, CYCL, BRIK, WOOD each with a
      model), their frames differ from the wall pass switched off, a hit fence cell is
      damaged before it is wrecked, two cells are wrecked and leave the map's count;
  (8) Soviet 1's steel crate and field overlays reach the reused-cell lists.
"""
import os
import re
import sys

# the tree types the module reuses: read from what stage-reuse.py staged (its manifest's
# foliage rows), so a tree approved for reuse later is held to the same claims; the eight
# first approved when the module carries no manifest
REUSED_TREES = {'T01', 'T02', 'T03', 'T05', 'T07', 'T10', 'T12', 'T13'}
# and the files it staged: a winter look is staged only once its winter row is approved
# (or with --include-review), so on snow a reused asset wears its winter look exactly when
# its <T>_WIN file is among them (None: no manifest, every winter look expected)
STAGED_FILES = None
try:
    import json as _json
    _rows = _json.load(open(os.path.join('ra3d', 'ebld', 'reuse-manifest.json')))['rows']
    REUSED_TREES = {k.split('/')[1] for k in _rows if k.startswith('foliage/') and '_' not in k} or REUSED_TREES
    STAGED_FILES = {n for v in _rows.values() for n in v.get('files', {})}
except (OSError, ValueError, KeyError):
    pass


def winter_staged(code):
    return STAGED_FILES is None or f'{code}_WIN.ebld' in STAGED_FILES


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def script(d):
    os.makedirs(d, exist_ok=True)
    v = ['tick 2', 'reveal 1', 'cam 52 72', 'dist 1500', f'shot {d}/vil_on.png', 'ra3dprobe',
         'ebld 0', f'shot {d}/vil_off.png', 'ebld 1', 'tick 1']
    v += ['hurtcell 49 70'] * 5 + ['hurtcell 52 75'] * 9
    v += ['tick 3', 'ra3dprobe', 'tick 40', f'shot {d}/vil_wreck.png', 'ra3dprobe', 'tick 520',
          'ra3dprobe', 'quit']
    write(f'{d}/vil.txt', v)
    write(f'{d}/snow.txt', ['tick 2', 'reveal 1', 'cam 44 57', 'dist 1500',
                            f'shot {d}/snow_on.png', 'ra3dprobe', 'ebld 0',
                            f'shot {d}/snow_off.png', 'ebld 1', 'cam 45 51',
                            f'shot {d}/fence_on.png', 'walls 0', f'shot {d}/fence_off.png',
                            'walls 1', 'quit'])
    write(f'{d}/marks.txt', ['tick 2', 'reveal 1', 'cam 99 88', 'dist 1200',
                             f'shot {d}/marks_on.png', 'ra3dprobe', 'decalenh 0',
                             f'shot {d}/marks_off.png', 'ra3dprobe', 'decalenh 1',
                             'cam 72 32', 'dist 1300', f'shot {d}/walls_on.png', 'walls 0',
                             f'shot {d}/walls_off.png', 'walls 1', 'ra3dprobe',
                             'hurtcell 71 32', 'hurtcell 74 30', 'tick 2', 'ra3dprobe',
                             'hurtcell 71 32', 'tick 20', 'ra3dprobe', 'quit'])
    write(f'{d}/smark.txt', ['tick 2', 'reveal 1', 'cam 91 86', 'dist 1200',
                             f'shot {d}/smark_on.png', 'ra3dprobe', 'decalenh 0',
                             f'shot {d}/smark_off.png', 'quit'])
    write(f'{d}/trees.txt', ['tick 1', 'reveal 1', 'cam 46 84', f'shot {d}/trees.png',
                             'ra3dprobe', 'quit'])


def read(path):
    try:
        return open(path, errors='replace').read().splitlines()
    except OSError:
        return []


def probes(lines):
    """The log cut at each RA3DPROBE|frame line: one list of probe lines per probe."""
    out, cur = [], None
    for l in lines:
        if l.startswith('RA3DPROBE|frame|'):
            cur = [l]
            out.append(cur)
        elif l.startswith('RA3DPROBE|') and cur is not None:
            cur.append(l)
    return out


def kv(line):
    return dict(p.split('=', 1) for p in line.split('|') if '=' in p)


def models(probe):
    out = []
    for l in probe:
        if not l.startswith('RA3DPROBE|model|'):
            continue
        f = l.split('|')
        k = kv(l)
        cx, cy = map(int, k['cell'].split(','))
        fw, fh = map(int, k['foot'].split('x'))
        ax, az = map(float, k['at'].split(','))
        b = list(map(float, k['box'].split(',')))
        s, m = map(int, k['str'].split('/'))
        out.append(dict(what=f[2], type=f[3], cell=(cx, cy), foot=(fw, fh), at=(ax, az), box=b,
                        str=s, max=m, stage=int(k['stage'])))
    return out


def terrains(probe):
    out = []
    for l in probe:
        if not l.startswith('RA3DPROBE|terrain|'):
            continue
        k = kv(l)
        cx, cy = map(int, k['cell'].split(','))
        # (a renderer before the reuse pass prints no model and no point: a card)
        ax, az = map(float, k.get('at', '-1,-1').split(','))
        out.append(dict(type=l.split('|')[2], cell=(cx, cy), model=k.get('model') == '1',
                        at=(ax, az)))
    return out


def walls(probe):
    for l in probe:
        if l.startswith('RA3DPROBE|walls|'):
            return kv(l)
    return {}


def frame(probe):
    return kv(probe[0]) if probe else {}


def differ(d, a, b, cut=24):
    from PIL import Image, ImageChops
    try:
        x = Image.open(f'{d}/{a}').convert('RGB').crop((0, 0, 1040, 720))
        y = Image.open(f'{d}/{b}').convert('RGB').crop((0, 0, 1040, 720))
    except OSError:
        return -1
    diff = ImageChops.difference(x, y).convert('L').point(lambda v: 255 if v > cut else 0)
    return diff.histogram()[255]


def peach(d, on, off):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(f'{d}/{on}').convert('RGB')).astype(int)[:, :1040]
    b = np.asarray(Image.open(f'{d}/{off}').convert('RGB')).astype(int)[:, :1040]
    changed = np.abs(a - b).sum(-1) > 30
    r, bl = a[..., 0], a[..., 2]
    return int((changed & (r > bl + 40) & (r > 170)).sum()), int(changed.sum())


def check(d):
    fails, notes = [], []
    # (1) (2) the village
    vil = read(f'{d}/vil.log')
    vp = probes(vil)
    vm = models(vp[0]) if vp else []
    civ = {m['type'] for m in vm if m['what'] == 'civil'}
    want = {'MISS', 'V01', 'V03', 'V04', 'V05', 'V06', 'V07'}
    if not want <= civ:
        fails.append(f'the village is missing reused models: {sorted(want - civ)}')
    px = differ(d, 'vil_on.png', 'vil_off.png')
    if px < 3000:
        fails.append(f'the village with the remasters differs from without by only {px} px')
    else:
        notes.append(f'{len(civ)} civilian types as models ({px} px against the 1996 cards)')
    snow = read(f'{d}/snow.log')
    sp = probes(snow)
    sm = models(sp[0]) if sp else []
    bad = []
    for m in vm + sm:
        if m['what'] != 'civil':
            continue
        (cx, cy), (fw, fh) = m['cell'], m['foot']
        x0, x1, z0, z1 = m['box']
        mx, mz = (x0 + x1) / 2, (z0 + z1) / 2
        if not (cx <= mx <= cx + fw and cy <= mz <= cy + fh) \
           or x0 < cx - 0.75 or x1 > cx + fw + 0.75 or z0 < cy - 0.75 or z1 > cy + fh + 0.75:
            bad.append(f"{m['type']}@{cx},{cy}")
    if bad:
        fails.append(f'reused models off their plots: {bad[:6]}')
    # (3) the stages
    hurt = [m for m in sm if m['what'] == 'civil' and m['str'] < m['max']]
    if len(hurt) < 3 or any(m['stage'] < 1 for m in hurt) or not any(m['stage'] == 2 for m in hurt):
        fails.append('Soviet 1\'s hurt houses are not worn and broken: '
                     + ', '.join(f"{m['type']} {m['str']}/{m['max']} stage {m['stage']}" for m in hurt))
    else:
        notes.append(f'{len(hurt)} hurt houses worn or broken')
    if not any(l.startswith('DEATHSHED|V01|') for l in vil):
        fails.append('the church did not die')
    wrecks = {re.search(r'key=(\d+)', l).group(1) for l in vil if l.startswith('EBLD|wreck|')}
    gone = {re.search(r'key=(\d+)', l).group(1) for l in vil if l.startswith('EBLD|gone|')}
    if not wrecks or not (wrecks & gone):
        fails.append(f'the church\'s wreck did not play out (wrecks {sorted(wrecks)}, gone {sorted(gone)})')
    if not any(l.startswith('EBLD|standdown|smudge=') for l in vil):
        fails.append('the church\'s engine death craters were not held back under its scorch')
    last = models(vp[-1]) if vp else []
    if any(m['type'] == 'V01' and m['cell'] == (48, 69) for m in last):
        fails.append('the dead church still stands as a model')
    # (4) (5) the trees
    tr = read(f'{d}/trees.log')
    tp = probes(tr)
    recs = terrains(tp[0]) if tp else []
    trees = [r for r in recs if r['type'][0] == 'T' and r['type'][1].isdigit()]
    wrong = [f"{r['type']}@{r['cell'][0]},{r['cell'][1]}" for r in trees
             if r['model'] != (r['type'] in REUSED_TREES)]
    off = [f"{r['type']}@{r['cell'][0]},{r['cell'][1]}" for r in trees if r['model']
           and not (r['cell'][0] <= r['at'][0] < r['cell'][0] + 2
                    and r['cell'][1] <= r['at'][1] < r['cell'][1] + 2)]
    nmodel = sum(1 for r in trees if r['model'])
    if len(trees) < 10 or nmodel < 5:
        fails.append(f'Allied 5 has {len(trees)} trees, {nmodel} as models')
    elif wrong:
        fails.append(f'trees drawn the wrong way (reused types are models, the rest cards): {wrong[:6]}')
    elif off:
        fails.append(f'tree models off their own cells: {off[:6]}')
    else:
        notes.append(f'{nmodel} of {len(trees)} trees as reused models, on their cells')
    et = [l for l in vil if l.startswith('ETREE|')]
    if not any(l.startswith('ETREE|stage|T12|') and '|to=2|' in l for l in et) \
       or not any(l.startswith('ETREE|orphan|T12|') for l in et) \
       or not any(l.startswith('ETREE|gone|T12|') for l in et):
        fails.append('the T12 did not crumble to its snag, outlive its tree and go: ' + '; '.join(et[:4]))
    srecs = terrains(sp[0]) if sp else []
    swrong = [f"{r['type']}@{r['cell'][0]},{r['cell'][1]}" for r in srecs
              if r['type'][0] == 'T' and r['type'][1].isdigit()
              and r['model'] != (r['type'] in REUSED_TREES and winter_staged(r['type']))]
    attached = {m.group(1) for m in (re.search(r'ebld: ra3d/ebld/(\w+)\.ebld read .*attached', l) for l in snow) if m}
    need = {m['type'] + '_WIN' for m in sm if m['what'] == 'civil' and winter_staged(m['type'])}
    need |= {r['type'] + '_WIN' for r in srecs if r['type'] in REUSED_TREES and winter_staged(r['type'])}
    summer = sorted(a for a in attached if a.endswith('_TEM'))
    nsm = sum(1 for r in srecs if r['model'])
    want_models = any(r['type'] in REUSED_TREES and winter_staged(r['type']) for r in srecs)
    if not srecs or (want_models and nsm == 0) or swrong:
        fails.append(f'on snow the trees are drawn the wrong way ({nsm} of {len(srecs)} models; {swrong[:6]})')
    elif not need <= attached:
        fails.append(f'on snow these winter looks were not attached: {sorted(need - attached)}')
    elif summer:
        fails.append(f'on snow summer tree files were read: {summer}')
    else:
        notes.append(f'snow: {len(need)} winter looks attached ({", ".join(sorted(need))}), '
                     f'{nsm} trees as winter models')
    # (6) the marks
    mk = read(f'{d}/marks.log')
    mp = probes(mk)
    on = int(frame(mp[0]).get('marks', '-1')) if mp else -1
    offm = int(frame(mp[1]).get('marks', '-1')) if len(mp) > 1 else -1
    px = differ(d, 'marks_on.png', 'marks_off.png')
    if on != 8 or offm != 0 or px < 500:
        fails.append(f'Soviet 9\'s marks: {on} remastered (8), {offm} with the dial off (0), {px} px apart')
    else:
        notes.append(f'8 marks remastered ({px} px)')
    smk = read(f'{d}/smark.log')
    smp = probes(smk)
    son = int(frame(smp[0]).get('marks', '-1')) if smp else -1
    try:
        pch, chg = peach(d, 'smark_on.png', 'smark_off.png')
    except OSError:
        pch, chg = -1, 0
    if son < 1 or pch < 0 or pch >= 300 or chg < 200:
        fails.append(f'the snow crater: {son} remastered, {pch} of {chg} changed pixels peach (under 300)')
    else:
        notes.append(f'snow burn grey ({pch} of {chg} changed pixels peach)')
    # (7) the walls
    w0 = walls(mp[2]) if len(mp) > 2 else {}
    w1 = walls(mp[3]) if len(mp) > 3 else {}
    w2 = walls(mp[4]) if len(mp) > 4 else {}
    mods = w0.get('models', '0,0,0,0,0').split(',')
    px = differ(d, 'walls_on.png', 'walls_off.png')
    if not all(int(w0.get(k, '0')) > 0 for k in ('SBAG', 'CYCL', 'BRIK')) or mods[:3] != ['1', '1', '1']:
        fails.append(f'Soviet 9\'s walls are not their reused pieces: {w0}')
    elif px < 2000:
        fails.append(f'Soviet 9\'s walls differ from the wall pass off by only {px} px')
    dmg = [l for l in mk if l.startswith('EBLD|damaged|key=9')]
    wr = {re.search(r'key=(\d+)', l).group(1) for l in mk if l.startswith('EBLD|wreck|key=9')}
    cells = [int(w.get('cells', '-1')) for w in (w0, w1, w2)]
    if not dmg or len(wr) < 2 or cells[2] != cells[0] - 2:
        fails.append(f'the hit wall cells: {len(dmg)} damaged, {len(wr)} wrecked (2), cells {cells}')
    else:
        notes.append(f'walls: {px} px against the 1996 cards, a fence cell worn then wrecked, a sandbag wrecked')
    sw = {}
    for p in sp:
        sw = walls(p) or sw
    fpx = differ(d, 'fence_on.png', 'fence_off.png')
    if int(sw.get('WOOD', '0')) < 1 or sw.get('models', '').split(',')[4:5] != ['1'] or fpx < 500:
        fails.append(f'Soviet 1\'s wooden fence: {sw.get("WOOD")} cells, models {sw.get("models")}, {fpx} px')
    # (8) the cell decorations
    if int(sw.get('crates', '0')) < 1 or int(sw.get('fields', '0')) < 1:
        fails.append(f'Soviet 1\'s crate and fields did not reach the lists: {sw}')
    if fails:
        print('0 ' + '; '.join(fails))
    else:
        print('1 Red Alert 3D draws the reused remasters: ' + '; '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit(__doc__)
