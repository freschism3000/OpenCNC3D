#!/usr/bin/env python3
"""G352: Red Alert 3D's Submarine dives through the sea as its model (game/evhc_mod.h:
evhc_dive, evhc_draw_dived_ghosts; game/evhc_ra/SS.evhc with its dive from painter.submerge,
tools/art/remaster/bake_evhc.py).

  gate_ra3dsub.py script DIR    write the missions and the run scripts into DIR
  gate_ra3dsub.py check DIR     read the runs' logs; first line "G352 1|0 message"

Run from a play folder with the Red Alert module staged and SS among ra3d/evhc.

THE RUNS, on private copies of two missions:
  u  Soviet 3 (snow, the player Soviet): a Soviet Submarine at sea with an Allied Destroyer
     six cells east; the submarine left to itself (it dives), then ordered at the Destroyer
     (it comes up to fire); sampled every two ticks with a frame drawn.
  g  Allied 5 (temperate, the player Allied): a Soviet Submarine at sea with nothing to
     fight (it dives), sampled the same way.

CLAIMS:
  (1) the player's own Submarine dives as the engine cloaks it: through CLOAKING it is drawn
      lower on every sample, past awash (0.099 of a cell) and periscope depth (0.214) to
      fully under (0.354) once CLOAKED, never fading (it is drawn on every sample);
  (2) fully under, the player's own is drawn faintly through the sea (the ghost counter
      rises while it is under);
  (3) ordered at the Destroyer it comes up (UNCLOAKING, drawn higher on every sample to the
      surface) and fires its torpedoes surfaced (UVFX|fire with it at depth 0);
  (4) another house's Submarine goes down the same way as the player watches and, once
      under, is not drawn at all.
NOT COVERED: how it looks (the proof pictures); its death at sea (the ships' wreck rule,
G345).
"""
import os
import re
import shutil
import sys

PLAY = os.getcwd()
SS = 'SS'
RUNS = {
    'u': dict(scen='SCU03EA', sub=('USSR', 30, 50, 64), foe=('Greece', 'DD', 36, 50, 192),
              cam=(33, 50), order=True),
    'g': dict(scen='SCG05EA', sub=('USSR', 36, 108, 64), foe=None, cam=(37, 108), order=False),
}
DEPTHS = (0.099, 0.214, 0.354)


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def kv(line):
    return dict(p.split('=', 1) for p in line.split('|') if '=' in p)


def staged():
    return os.path.isfile(os.path.join(PLAY, 'ra3d/evhc', SS + '.evhc'))


def script(d):
    os.makedirs(d, exist_ok=True)
    cell = lambda x, y: y * 128 + x
    for tag, m in RUNS.items():
        dst = os.path.join(d, 'm' + tag)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
        path = os.path.join(dst, m['scen'] + '.INI')
        L = open(path, errors='replace').read().splitlines()
        if not any(l.strip() == '[SHIPS]' for l in L):
            L += ['', '[SHIPS]']
        i = next(k for k, l in enumerate(L) if l.strip() == '[SHIPS]')
        h, x, y, f = m['sub']
        rows = [f'{h},{SS},256,{cell(x, y)},{f},Guard,None']
        if m['foe']:
            fh, ft, fx, fy, ff = m['foe']
            rows.append(f'{fh},{ft},256,{cell(fx, fy)},{ff},Guard,None')
        L[i + 1:i + 1] = [f'{90 + k}={r}' for k, r in enumerate(rows)]
        write(path, L)
        v = ['tick 2', 'reveal 1', f'cam {m["cam"][0]} {m["cam"][1]}', 'dist 1200']
        v += ['tick 2', 'frames 1', 'evhcdump'] * 45
        v += [f'shot {d}/{tag}_under.png']
        if m['order']:
            v += ['echo GATE|order', f'lclickobj {SS}:USSR 0', 'select',
                  f'actclickobj {m["foe"][1]}:{m["foe"][0]} 0']
            v += ['tick 2', 'frames 1', 'evhcdump'] * 60
        write(f'{d}/{tag}.txt', v + ['quit'])


def dives(lines):
    """Per evhcdump, the submarine's EVHCDIVE fields, with the engine frame of the dump."""
    out = []
    for l in lines:
        if l.startswith('EVHCDIVE|SS|'):
            out.append(kv(l))
        elif l == 'ECHO|GATE|order':
            out.append({'order': '1'})
    return out


def check(d):
    if not staged():
        print('G352 - the Submarine is not staged in this folder (tools/ra3d/stage-vehicles.py)')
        return
    fails, notes = [], []
    for tag, scene in (('u', 'snow, the player\'s own'), ('g', 'temperate, the Soviets\' against the player')):
        try:
            lines = open(f'{d}/{tag}.log', errors='replace').read().splitlines()
        except OSError:
            fails.append(f'{scene}: no log')
            continue
        seq = dives(lines)
        cut = next((i for i, q in enumerate(seq) if q.get('order')), len(seq))
        before = [q for q in seq[:cut] if 'dive' in q]
        down = [q for q in before if q['cloak'] == '1' and q['drawn'] == '1']
        dv = [float(q['dive']) for q in down]
        if len(dv) < 5 or any(b <= a for a, b in zip(dv, dv[1:])) or max(dv) < DEPTHS[1]:
            fails.append(f'{scene}: going down, {len(dv)} samples drawn at {dv[:3]}..{dv[-3:]}')
            continue
        under = [q for q in before if q['cloak'] == '2' or (q['drawn'] == '0' and float(q['dive']) > 0.3)]
        if tag == 'u':
            full = [q for q in before if q['cloak'] == '2' and q['drawn'] == '1']
            if not full or any(abs(float(q['dive']) - DEPTHS[2]) > 1e-3 for q in full):
                fails.append(f'{scene}: fully under drawn at {[q["dive"] for q in full][:3]} '
                             f'({len(full)} samples)')
                continue
            g0 = int(full[0]['ghosts'])
            g1 = int(full[-1]['ghosts'])
            if g1 <= g0:
                fails.append(f'{scene}: fully under, not drawn faintly through the sea '
                             f'(ghosts {g0} to {g1})')
                continue
            after = [q for q in seq[cut + 1:] if 'dive' in q]
            up = [float(q['dive']) for q in after if q['cloak'] == '3']
            # (each torpedo with the depth the submarine is drawn at on the tick it leaves:
            # the first dump after it)
            fires, wait = [], 0
            for l in lines[lines.index('ECHO|GATE|order'):] if 'ECHO|GATE|order' in lines else []:
                if l.startswith('UVFX|fire|type=SS'):
                    wait += 1
                elif l.startswith('EVHCDIVE|SS|') and wait:
                    fires += [float(kv(l)['dive'])] * wait
                    wait = 0
            if wait or any(f > 0.0 for f in fires):
                fails.append(f'{scene}: torpedoes fired at depths {fires} (not surfaced)')
                continue
            surfaced = [q for q in after if q['cloak'] == '0' and float(q['dive']) == 0.0]
            if len(up) < 5 or any(b >= a for a, b in zip(up, up[1:])) or not surfaced or not fires:
                fails.append(f'{scene}: ordered to fire, came up through {len(up)} samples '
                             f'{up[:3]}..{up[-3:]}, {len(surfaced)} surfaced, {len(fires)} torpedoes')
                continue
            notes.append(f'{scene}: down through {len(dv)} drawn samples to {DEPTHS[2]} cells, '
                         f'faint through the sea while under ({g1 - g0} ghost draws), up through '
                         f'{len(up)} samples to fire {len(fires)} torpedoes surfaced')
        else:
            gone = [q for q in before if q['drawn'] == '0']
            if not gone or before.index(gone[0]) < before.index(down[-1]):
                fails.append(f'{scene}: under, still drawn ({len(gone)} samples not drawn)')
                continue
            notes.append(f'{scene}: down through {len(dv)} drawn samples to {max(dv):.3f} cells, '
                         f'then not drawn at all ({len(gone)} samples)')
    if fails:
        print('G352 0 ' + '; '.join(fails))
    else:
        print('G352 1 Red Alert 3D\'s Submarine dives through the sea as its model: ' + '; '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit(__doc__)
