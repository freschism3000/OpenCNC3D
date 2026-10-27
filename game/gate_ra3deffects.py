#!/usr/bin/env python3
"""G342: Red Alert 3D draws OpenCNC3D's own effects under Enhanced.

  gate_ra3deffects.py script DIR    write the two run scripts into DIR
  gate_ra3deffects.py check DIR     read the runs' logs and pictures; first line is
                                    "1 message" or "0 message"

Red Alert's 3D view used to draw every engine anim (explosions, fire, smoke, muzzle fire)
as its 1996 sprite on a card. Under Enhanced each anim with a Tiberian Dawn counterpart is
now that anim's own OpenCNC3D effect, through Tiberian Dawn's emitters and remastered art
(game/ra3d_effects_mod.h maps the numbers); the cards stand down. The runs (all hidden,
Enhanced on each mission's own 3D scene):

  temp  Allied 5 (temperate). A flamethrower, a medium tank, two riflemen and a V2
        launcher are set down for the player before the Soviet base (`spawnobj`): the
        flamethrower burns a pine, the tank shells the radar dome, the V2 is sent at a
        power plant, the dome is hurt to a fifth and the power plant destroyed; the Soviet
        heavy tanks and V2s fire back (`forcefire`). One frame is taken twice, the second
        time with Enhanced effects off (`gfx efx_enh 0`: the 1996 cards).
  snow  Allied 1 (snow). The Rangers drive at the Tesla Coils, the ore silo is hurt below
        half and the kennel destroyed; every other tick is a picture until the coil fires.

Claims:
  (1) every anim the runs drew as particles took the recipe of its Tiberian Dawn
      counterpart (FIRE3 FIRE_SMALL, GUNFIRE the cannon flash, MINIGUN a GUN_ facing, a dead
      building's FBALL1 FRAG3 ...), and at least six kinds of Red Alert anim were drawn so;
  (2) while Enhanced effects are on, no anim with a counterpart is a card (the probe's
      animcards is 0 whenever anims are drawn as particles), and the same frame with them
      off draws the cards again and differs from it;
  (3) a muzzle flash or a tree's fire on an object drawn as its card started on the card,
      a fire on a building drawn as its model on its roof, a dead building's blast at the
      middle of its plot, and a rocket's trail at the rocket's height;
  (4) one blast per dead building: as many as the structures that died (barrels, which go
      up as fireballs, and the reused civilian buildings, which have the lab's own blast,
      apart);
  (5) a structure below half strength smokes from its roof;
  (6) the brain prints the shots fired (EFX|FIRE) and the bullets in flight (EFX|BULLET),
      and a V2 in the air is drawn at its height;
  (7) the riflemen's rifles flash (the modelled soldiers' own muzzle flash);
  (8) when the Tesla Coil fires, both ends of its bolt glow (two sparks a bolt).
PROVEN TO FAIL: the renderer before this work prints no RA3DPROBE|efx line and fires the
wrong recipes for Red Alert's anims (FIRE3 as PIFFPIFF, MOVEFLSH as GUN_NE), and the brain
before it prints no EFX|FIRE or EFX|BULLET line.
NOT COVERED: how the effects look (looked at by eye, on frame sheets); the Red Alert engine
is not identical from run to run, so the coil is waited for over 120 ticks rather than at a
fixed one, and nothing here compares two runs.
"""
import os
import re
import sys

# The Tiberian Dawn recipes a Red Alert anim may fire (ra3d_effects_mod.h's table), or its
# own where it has one.
ALLOWED = {
    'FIRE3': {'FIRE_SMALL'}, 'FIRE2': {'FIRE_MED'}, 'FIRE1': {'FIRE_MED2'},
    'FIRE4': {'FIRE_TINY'}, 'FB2': {'FB2_RA'}, 'GUNFIRE': {'CannonFlash_OURS'},
    'FBALL1': {'FBALL1_RA', 'FRAG3'}, 'FRAG1': {'FRAG1'}, 'ART-EXP1': {'ART_EXP1'},
    'VEH-HIT1': {'VEH_HIT1'}, 'VEH-HIT2': {'VEH_HIT2'}, 'VEH-HIT3': {'VEH_HIT3'},
    'NAPALM1': {'NAPALM1'}, 'NAPALM2': {'NAPALM2'}, 'NAPALM3': {'NAPALM3'},
    'SMOKEY': {'SMOKE_PUFF'}, 'PIFF': {'PIFF'}, 'PIFFPIFF': {'PIFFPIFF'},
    'BURN-S': {'BURN_SMALL', 'ON_FIRE_SMALL'}, 'BURN-M': {'BURN_MED', 'ON_FIRE_MED'},
    'BURN-L': {'BURN_BIG', 'ON_FIRE_BIG'}, 'FLAK': {'FLAK_RA'}, 'ATOMSFX': {'ATOM_BLAST_RA'},
    'SMOKLAND': {'LZ_SMOKE'},
}
# and the anims with a recipe of their own (ra3d_efxm_mod.h; G354 holds them): the large
# fireball's off a standing plot, the flame puff's, the flak's above, and these
for _n in ('SMOKE_M', 'FLMSPT', 'ELECTRO', 'ELECTDOG', 'H2O_EXP1', 'H2O_EXP2', 'H2O_EXP3',
           'TWINKLE1', 'TWINKLE2', 'TWINKLE3', 'ARMOR', 'CHRONBOX', 'DOLLAR', 'INVUN', 'PARABOX',
           'SPEED', 'TQUAKE', 'SPUTDOOR'):
    ALLOWED[_n] = {_n + '_RA'}
FACES = ('N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW')
for f in FACES:
    ALLOWED.setdefault('MINIGUN', set()).add('GUN_' + f)
    ALLOWED.setdefault('SAMFIRE', set()).add('SAM_' + f)


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def script(d):
    os.makedirs(d, exist_ok=True)
    t = ['tick 3', 'reveal 1', 'cam 52 85', 'dist 2600',
         'spawnobj E4 44 82', 'spawnobj 2TNK 52 88', 'spawnobj V2RL 48 85',
         'spawnobj E1 54 88', 'spawnobj E1 55 88', 'tick 2',
         'lclickobj E4:Greece 0', 'order 43 80 c', 'lclickobj 2TNK:Greece 0', 'order 53 85 c',
         'lclickobj V2RL:Greece 0', 'order 59 82 c']
    # (to a fifth of its strength: the engine rolls a fire for each of its cells as it
    # passes half and again as it passes a quarter, so one at least burns on its roof)
    t += ['hurtcell 53 85', 'hurtcell 53 85', 'hurtcell 54 86', 'hurtcell 54 85', 'hurtcell 53 86',
          'hurtcell 53 85', 'hurtcell 54 86', 'hurtcell 54 85']
    t += ['forcefire 3TNK 0 0', 'forcefire V2RL 0 0']
    for i in range(16):
        t += ['tick 3', f'shot {d}/temp_{i:02d}.png', 'ra3dprobe', 'ebldump']
        if i == 4:
            t += ['hurtcell 59 82'] * 4 + ['forcefire 3TNK 0 1', 'forcefire V2RL 0 1']
        if i == 8:
            t += ['gfx efx_enh 0', f'shot {d}/temp_off.png', 'ra3dprobe', 'ebldump', 'gfx efx_enh 1']
    write(f'{d}/temp.txt', t + ['efxdump', 'quit'])
    # (no power plant is touched: the coils fire only while the base has its power)
    s = ['tick 20', 'reveal 1', 'cam 66 56', 'dist 2600',
         'lclickobj JEEP 0', 'order 69 55', 'lclickobj JEEP 1', 'order 70 55',
         'cam 66 60', 'hurtcell 59 64', 'hurtcell 59 64', 'tick 30']
    for i in range(60):
        s += ['tick 2', f'shot {d}/snow_{i:02d}.png', 'ra3dprobe', 'ebldump']
        if i == 10:
            s += ['hurtcell 64 65'] * 5
    write(f'{d}/snow.txt', s + ['efxdump', 'quit'])


def read(path):
    try:
        return open(path, errors='replace').read().splitlines()
    except OSError:
        return []


def kv(line):
    return dict(p.split('=', 1) for p in line.split('|') if '=' in p)



def staged_effects(lines):
    """Only attached staged models can supply their own damage/death effects."""
    art = {}
    attached = set()
    objects = {}
    damaged = set()
    emits = []
    events = {}
    fired = set()
    for line in lines:
        fields = line.split('|')
        row = kv(line)
        if line.startswith('EBLDART|') and int(row.get('sets', 0)) >= 2:
            art[fields[1]] = row
        elif line.startswith('EBLDMESH|') and min(int(row.get(n, -1)) for n in ('intact', 'cut', 'wreck')) > 0:
            # The intact whole-model draw counter excludes the damaged pieces.
            attached.add(fields[1])
        elif line.startswith('EBLDOBJ|'):
            key = row['key']
            objects[key] = (fields[1], row['id'])
            if row['stage'] == 'DAMAGED' and 0 < int(row['str']) < int(row['max']):
                damaged.add((key, fields[1]))
        elif line.startswith('EBLDEMIT|'):
            emits.append((fields[1], row))
        elif line.startswith('EBLD|boom|'):
            if row.get('recipe') == 'file' and int(row.get('slots', 0)) > 0:
                events.setdefault(row['key'], []).append(row)
        elif line.startswith('EBLDBOOM|') and int(row.get('parts', 0)) > 0:
            fired.add((row['key'], fields[1]))
    root = {'fire': set(), 'smoke': set()}
    for code, row in emits:
        pair = (row.get('key'), code)
        if code in art and code in attached and pair in damaged and row.get('state') == '1' and row.get('piece') == '-' and int(row.get('spawned', 0)) > 0:
            if row.get('src') in root:
                root[row['src']].add(pair)
    booms = set()
    for key, rows in events.items():
        if key in objects:
            code, ident = objects[key]
            if code in art and code in attached and (key, code) in fired and len(rows) == 1:
                booms.add((code, 'id=' + ident))
    return root, booms


def check(d):
    from PIL import Image, ImageChops
    fails, notes = [], []
    logs = {r: read(f'{d}/{r}.log') for r in ('temp', 'snow')}
    for r, L in logs.items():
        if not any(l.startswith('SCRIPT|end') and '0 failures' in l for l in L):
            fails.append(f'{r}: the script did not run to its end without a failure')
    # (1) the recipes
    kinds, wrong = set(), []
    for r, L in logs.items():
        for l in L:
            m = re.match(r'^EFXSPAWN\|([^|]+)\|recipe=ANIM_([^|]+)\|particles=(\d+)', l)
            if not m:
                continue
            name, rec, n = m.group(1), m.group(2), int(m.group(3))
            if name in ('LOOPTEST',):
                continue
            if name not in ALLOWED or rec not in ALLOWED[name]:
                wrong.append(f'{r}:{name}->{rec}')
            elif n > 0:
                kinds.add(name)
    if wrong:
        fails.append('anims fired the wrong recipe: ' + ' '.join(sorted(set(wrong))[:8]))
    if len(kinds) < 6:
        fails.append(f'only {len(kinds)} kinds of Red Alert anim were drawn as particles ({" ".join(sorted(kinds))}), want 6')
    # (2) the cards stand down, and come back with the effects off
    probes = {r: [kv(l) for l in L if l.startswith('RA3DPROBE|efx|')] for r, L in logs.items()}
    if not probes['temp'] or not probes['snow']:
        fails.append('no RA3DPROBE|efx line: this renderer does not draw Red Alert\'s effects')
        print('0 ' + '; '.join(fails))
        return
    bad = [p for r in probes for p in probes[r]
           if p.get('enh') == '1' and int(p['particleanims']) > 0 and int(p['animcards']) > 0]
    if bad:
        fails.append(f'{len(bad)} frames drew anims with a counterpart as cards under Enhanced effects')
    off = [p for p in probes['temp'] if p.get('enh') == '0']
    if not off or int(off[0]['animcards']) <= 0:
        fails.append('with Enhanced effects off the cards did not come back')
    try:
        a = Image.open(f'{d}/temp_08.png').convert('RGB')
        b = Image.open(f'{d}/temp_off.png').convert('RGB')
        diff = ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v > 24 else 0)
        changed = diff.histogram()[255]
        notes.append(f'the frame with effects and without differs in {changed} px')
        if changed < 2000:
            fails.append(f'the frame with effects and the same frame without differ in only {changed} px')
    except OSError as e:
        fails.append(f'a control picture is missing ({e})')
    last = {r: probes[r][-1] for r in probes}
    staged = {r: staged_effects(L) for r, L in logs.items()}
    # (3) where they start
    card = max(int(last[r]['cardbases']) for r in last)
    roof = max(int(last[r]['roofbases']) + len(staged[r][0]['fire']) for r in last)
    plot = max(int(last[r]['plotbases']) + len(staged[r][1]) for r in last)
    trail = int(last['temp']['trailbases'])
    for what, n in (('on a card', card), ('on a model\'s roof', roof), ('at a plot\'s middle', plot),
                    ('at a rocket\'s height', trail)):
        if n <= 0:
            fails.append(f'no effect started {what}')
    notes.append(f'bases: card {card}, roof {roof}, plot {plot}, trail {trail}')
    # (4) one blast per dead building
    for r, L in logs.items():
        # (a barrel goes up as a fireball, not a building; a civilian building reused from
        # OpenCNC3D's art has the lab's own blast, ebld_mod.h)
        dead = {(l.split('|')[1], l.split('|')[3]) for l in L if l.startswith('DEATHSHED|') and '|begin|' in l
                and l.split('|')[1] not in ('BARL', 'BRL3', 'BIO', 'HOSP', 'MISS')
                and not re.match(r'^V\d\d$', l.split('|')[1])}
        deaths = len(dead)
        blasts = int(last[r]['blasts']) + len(staged[r][1] & dead)
        notes.append(f'{r}: {deaths} structures died, {blasts} blasts')
        if deaths == 0 or blasts != deaths:
            fails.append(f'{r}: {blasts} building blasts for {deaths} structures that died')
    # (5) the damaged structure's smoke
    if int(last['snow']['smoke']) <= 0 and not any(code == 'SILO' for _, code in staged['snow'][0]['smoke']):
        fails.append('the ore silo below half strength gave no smoke')
    # (6) the brain's shots and bullets
    fires = sum(1 for L in logs.values() for l in L if l.startswith('UVFX|fire|'))
    bullets = max(int(p['bullets']) for r in probes for p in probes[r])
    lifted = max(int(p['bulletslifted']) for r in probes for p in probes[r])
    notes.append(f'{fires} shots, up to {bullets} bullets in flight, {lifted} drawn at their height')
    if fires == 0:
        fails.append('the brain printed no shot (EFX|FIRE)')
    if bullets == 0:
        fails.append('the brain printed no bullet in flight (EFX|BULLET)')
    if lifted == 0:
        fails.append('no projectile was drawn at its height')
    # (7) the rifles
    ir = max(int(last[r]['irflash']) for r in last)
    if ir <= 0:
        fails.append('no rifleman\'s rifle flashed')
    # (8) the coil's glow
    L = logs['snow']
    frames, cur = [], None
    for l in L:
        if l.startswith('RA3DPROBE|frame|'):
            cur = {'zap': int(re.search(r'\|zap=(\d+)', l).group(1)), 'bolts': 0, 'glows': -1}
            frames.append(cur)
        elif cur is not None and l.startswith('RA3DPROBE|bolt|'):
            cur['bolts'] += 1
        elif cur is not None and l.startswith('RA3DPROBE|efx|'):
            cur['glows'] = int(kv(l)['boltglows'])
    zaps = [f for f in frames if f['zap'] > 0 and f['bolts'] > 0]
    if not zaps:
        fails.append('the Tesla Coil fired no bolt that was drawn in 120 ticks')
    elif any(f['glows'] != 2 * f['bolts'] for f in zaps):
        fails.append('a drawn bolt did not glow at both ends')
    else:
        notes.append(f'{len(zaps)} frames with a bolt, each glowing at both ends')
    if fails:
        print('0 ' + '; '.join(fails))
    else:
        print('1 Red Alert 3D draws OpenCNC3D\'s effects: ' + ', '.join(sorted(kinds)) + ' as their Tiberian'
              ' Dawn effects, no card while they draw; ' + '; '.join(notes))


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        print(__doc__)
        sys.exit(2)
    (script if sys.argv[1] == 'script' else check)(sys.argv[2])
