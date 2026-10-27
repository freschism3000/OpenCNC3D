#!/usr/bin/env python3
"""Red Alert's special weapons, each played end to end by the player's own clicks, on an
Allied and a Soviet mission (game/cnc_sidebar.h: sb_super_engine_arm, sb_super_settle and
the held shot in sb_super_fire; game/cnc_eyes.cpp: the CALLBACK_EVENT_SPECIAL_WEAPON_TARGETTING
case of ev_cb, ra_chrono_dest_ok in update_cursor and ui_super_fire; the Red Alert brain's
OpenCNC3D_Probe_Object_At, which answers the destination by the 1996 cursor rule).

    python3 game/gate_ra3dsuper.py script DIR     write the runs' missions and scripts
    python3 game/gate_ra3dsuper.py check DIR      read the logs: "1 message" or "0 message"

Run from a play folder with a staged Red Alert module. Two hidden runs, every click made by
script through the same paths a hand reaches (the sidebar's own button, the map's own left
click), never the desk's pointer:

  a  Allied 8, temperate: the Chronosphere and the Advanced Tech centre the mission already
     gives the player, an airfield and a missile silo beside them, tech level 15 (the
     mission holds its own Chronosphere back with tech levels of 15 in its [General] and
     [PDOX] sections)
  s  Soviet 2, temperate: a construction yard, an airfield, an Iron Curtain and two advanced
     power plants added to the player's base, tech level 15

In a campaign the engine gives the silo's nuke only to a house that is not Soviet and the
Iron Curtain only to one that is (house.cpp Super_Weapon_Handler), so the nuke is fired on
the Allied run and the curtain on the Soviet one.

`superrecharge` (the brain's own debug charge) readies every special at once; the only
other staging is a rocket soldier beside the Allied tanks, two Soviet tanks (spawnobj) for
the curtain and an enemy carrier on the nuke's target. Claims, read from the engine's own state rather than from what the renderer says
it sent:

  Chronosphere (a): over the tank the pointer is CHRONO_SELECT; a click on bare ground
      misses and the special stays armed and charged, the tank where it was; the click on
      a soldier is accepted as the one to move, but the pointer over any destination is
      NO_MOVE and the click is not sent (the 1996 rule, Can_Teleport_Here: infantry never
      teleport), the soldier alive where he stood, until the cancel; the click on
      the tank makes the ENGINE ask for the destination (its targeting event names
      SW_Chrono2, and the latch takes it); the pointer over the destination is CHRONO_DEST;
      the click there puts the tank on that cell, and the Chronosphere is spent.
  GPS (a): never aimed; once charged the engine launches it by itself and its entry leaves
      the sidebar, with no click.
  Iron Curtain (s): a click on bare ground misses and stays armed; the click on a tank spends
      it, and that tank then takes no damage from three hits while its twin, not covered,
      loses strength to the same three.
  Nuclear strike (a): the pointer is the nuke's; the click on the enemy carrier spends it
      and the carrier is destroyed.
  Spy plane, paratroopers, parabombs (both): each click spends its special and the aircraft
      it sends (U2, BADR) enter the world; the paratroopers' riflemen land.

PROVEN TO FAIL: the renderer before this work disarms after the Chronosphere's first click
whatever it hit, never arms SW_Chrono2 (the click on the tank selects it), shows the move
cursor over the destination, and leaves the tank on 28,64 with the Chronosphere charged; its
Iron Curtain is disarmed by the miss as well. This renderer on the brain before this work
shows CHRONO_DEST for the soldier and sends the click, and the engine teleports him to his
death (0/45).
NOT COVERED: the destination's no-move cursor under the shroud and over ground a tank could
not enter (the same engine answer as the soldier's, through the same probe); how any of it
looks.
"""
import os
import shutil
import sys

PLAY = os.getcwd()
RUNS = {'a': ('SCG08EA', 'Greece'), 's': ('SCU02EA', 'USSR')}
# the player's structures each run adds: (type, x, y)
ADD = {
    'a': [('AFLD', 48, 96), ('MSLO', 45, 96)],
    's': [('FACT', 82, 55), ('IRON', 79, 56), ('APWR', 72, 56), ('APWR', 72, 59),
          ('AFLD', 78, 50)],
}


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def mission(d, tag):
    scen, side = RUNS[tag]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().replace('\r', '').splitlines()
    h = L.index('[' + side + ']')
    e = h + 1
    while e < len(L) and not L[e].startswith('['):
        e += 1
    L[h + 1:e] = [l for l in L[h + 1:e] if not l.startswith('TechLevel=')]
    L.insert(h + 1, 'TechLevel=15')
    st = L.index('[STRUCTURES]')
    for k, (t, x, y) in enumerate(ADD[tag]):
        L.insert(st + 1, f'{90 + k}={side},{t},256,{y * 128 + x},0,None,1,0')
    write(path, L)


def air(v, name, cell):
    """One of the three airfield specials, fired at a cell on screen."""
    x, y = cell
    v += [f'echo GATE|{name}', f'cam {x} {y}', 'tick 1', f'sbclickitem {name}', 'tick 1',
          f'actcell {x} {y}', 'tick 3', 'sbdump']


def script_a(d):
    v = ['tick 2', 'reveal 1', 'tick 100', 'echo GATE|before', 'sbdump', 'spawnobj E3 31 64',
         'superrecharge', 'tick 3', 'echo GATE|charged', 'sbdump', 'obj E3:Greece 0',
         # the Chronosphere. The tank is the third of the player's medium tanks, on 28,64.
         'echo GATE|chrono-arm', 'cam 30 63', 'dist 1100', 'tick 1', 'sbclickitem SW_Chrono',
         'cursorcell 28 64', 'sbdump',
         'echo GATE|chrono-miss', 'actcell 31 61', 'tick 3', 'sbdump', 'obj 2TNK:Greece 2',
         # a soldier: chosen, but no destination is ever his (Can_Teleport_Here)
         'echo GATE|chrono-soldier', 'actcell 31 64', 'tick 3', 'sbdump', 'cursorcell 33 62',
         'actcell 33 62', 'tick 3', 'sbdump', 'obj E3:Greece 0', 'cancel', 'tick 1', 'sbdump',
         'echo GATE|chrono-pick', 'sbclickitem SW_Chrono', 'actcell 28 64', 'tick 3', 'sbdump',
         'cursorcell 33 62',
         f'shot {d}/a_chrono_dest.png',
         'echo GATE|chrono-dest', 'actcell 33 62', 'tick 5', 'sbdump']
    v += [f'obj 2TNK:Greece {i}' for i in range(4)]
    v += ['tick 2', f'shot {d}/a_chrono_after.png']
    # (the nuke's mark: an armoured carrier, a type no Soviet house of the mission owns,
    # handed to the enemy on the target cell)
    v += ['echo GATE|nuke', 'spawnobj APC 60 83 enemy', 'tick 2', 'obj APC:USSR 0', 'cam 60 83',
          'tick 1', 'sbclickitem SW_Nuke', 'cursorcell 60 83', 'actcell 60 83', 'tick 3', 'sbdump']
    v += ['echo GATE|riflemen', 'count E1:Greece']
    air(v, 'SW_SpyMission', (40, 75))
    air(v, 'SW_ParaInfantry', (36, 70))
    air(v, 'SW_ParaBomb', (24, 86))
    v += ['echo GATE|air', 'count U2', 'count BADR']
    for _ in range(6):
        v += ['tick 100', 'echo GATE|later', 'count U2', 'count BADR', 'count E1:Greece',
              'count APC:USSR']
    v += ['sbdump', 'cam 60 83', 'tick 1', f'shot {d}/a_nuke_after.png']
    write(os.path.join(d, 'a.txt'), v + ['quit'])


def script_s(d):
    v = ['tick 2', 'reveal 1', 'tick 100', 'echo GATE|before', 'sbdump',
         'spawnobj 3TNK 80 62', 'spawnobj 3TNK 82 62', 'tick 3',
         'superrecharge', 'tick 3', 'echo GATE|charged', 'sbdump',
         'obj 3TNK:USSR 0', 'obj 3TNK:USSR 1',
         'echo GATE|iron-arm', 'cam 79 60', 'dist 1100', 'tick 1', 'sbclickitem SW_IronCurtain',
         'cursorcell 80 62', 'sbdump',
         'echo GATE|iron-miss', 'actcell 81 60', 'tick 3', 'sbdump',
         'echo GATE|iron-hit', 'actcell 80 62', 'tick 3', 'sbdump']
    v += ['echo GATE|iron-hurt']
    for _ in range(3):
        v += ['hurtobj 3TNK:USSR 0', 'hurtobj 3TNK:USSR 1', 'tick 2']
    v += ['obj 3TNK:USSR 0', 'obj 3TNK:USSR 1']
    v += ['echo GATE|riflemen', 'count E1:USSR']
    air(v, 'SW_SpyMission', (60, 45))
    air(v, 'SW_ParaInfantry', (76, 64))
    air(v, 'SW_ParaBomb', (50, 57))
    v += ['echo GATE|air', 'count U2', 'count BADR']
    for _ in range(6):
        v += ['tick 100', 'echo GATE|later', 'count U2', 'count BADR', 'count E1:USSR']
    v += ['sbdump']
    write(os.path.join(d, 's.txt'), v + ['quit'])


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag in RUNS:
        mission(d, tag)
    script_a(d)
    script_s(d)


def kv(line):
    out = {}
    for part in line.strip().split('|'):
        if '=' in part:
            k, val = part.split('=', 1)
            out[k] = val
    return out


def blocks(lines):
    """The log cut at each GATE echo: [(name, [lines])], in order."""
    out = []
    for l in lines:
        if l.startswith('ECHO|GATE|'):
            out.append((l.strip().split('|', 2)[2], []))
        elif out:
            out[-1][1].append(l.rstrip('\n'))
    return out


def block(B, name):
    for n, L in B:
        if n == name:
            return L
    return []


def entry(L, name):
    """The last sidebar dump of this special in a block: {} when it is not on the strip."""
    e = [kv(l) for l in L if l.startswith('SBENT|') and f'|name={name}|' in l]
    return e[-1] if e else {}


def armed(L):
    d = [kv(l) for l in L if l.startswith('SBDUMP|armed=')]
    return (d[-1].get('armed'), d[-1].get('armedname')) if d else (None, None)


def first(L, prefix):
    return next((l for l in L if l.startswith(prefix)), '')


def count(L, spec):
    c = [kv(l) for l in L if l.startswith(f'COUNT|{spec}|')]
    return int(c[-1]['have']) if c else None


def spent(e):
    return e.get('completed') == '0' and e.get('constructing') == '1'


def check_air(B, side, label):
    """The airfield's three, the same on both sides."""
    for name in ('SW_SpyMission', 'SW_ParaInfantry', 'SW_ParaBomb'):
        L = block(B, name)
        if not first(L, f'SUPER|fire|{name}|'):
            return f'{label}: {name} was never fired'
        if not spent(entry(L, name)):
            return f'{label}: the engine did not spend {name} ({entry(L, name)})'
    A = block(B, 'air')
    if (count(A, 'U2') or 0) < 1 or (count(A, 'BADR') or 0) < 2:
        return f'{label}: the spy plane and the two Badgers did not enter the world (U2 {count(A, "U2")}, BADR {count(A, "BADR")})'
    before = count(block(B, 'riflemen'), f'E1:{side}')
    later = [count(L, f'E1:{side}') for n, L in B if n == 'later']
    if before is None or not later or max(x or 0 for x in later) < before + 1:
        return f'{label}: the paratroopers did not land (riflemen {before} before, {later} after)'
    return None


def check_a(d):
    label = 'Allied 8'
    path = os.path.join(d, 'a.log')
    if not os.path.isfile(path):
        return f'{label}: no log'
    lines = open(path, errors='replace').read().splitlines()
    if not any(l.startswith('SCRIPT|end') and '0 failures' in l for l in lines):
        return f'{label}: the script did not run to its end without a failure'
    B = blocks(lines)
    # GPS: on the strip while charging, launched by the engine the moment it is charged
    if not entry(block(B, 'before'), 'SW_GPS'):
        return f'{label}: the GPS satellite was never on the strip'
    if entry(block(B, 'charged'), 'SW_GPS') or any(l.startswith('SUPER|arm|SW_GPS') for l in lines):
        return f'{label}: the GPS satellite was not launched by the engine on its own'
    # the Chronosphere
    L = block(B, 'chrono-arm')
    if kv(first(L, 'CURSORCELL|')).get('shape') != 'chrono-select' or armed(L) != ('1', 'SW_Chrono'):
        return f'{label}: arming the Chronosphere did not give CHRONO_SELECT over the tank ({first(L, "CURSORCELL|")})'
    L = block(B, 'chrono-miss')
    if armed(L) != ('1', 'SW_Chrono') or entry(L, 'SW_Chrono').get('completed') != '1' \
            or kv(first(L, 'OBJ|2TNK:Greece#2|')).get('cell') != '28,64':
        return f'{label}: the Chronosphere\'s miss on bare ground did not leave it armed and charged ({armed(L)})'
    L = block(B, 'chrono-soldier')
    if kv(first(block(B, 'charged'), 'OBJ|E3:Greece#0|')).get('cell') != '31,64':
        return f'{label}: the soldier was not put on 31,64'
    if not first(L, 'SUPER|arm|SW_Chrono2|id=8|by=engine') \
            or kv(first(L, 'CURSORCELL|')).get('shape') != 'no-move' \
            or not first(L, 'SUPER|target|cell=33,62|REFUSED by the engine') \
            or first(L, 'SUPER|fire|SW_Chrono2') \
            or kv(first(L, 'OBJ|E3:Greece#0|')).get('cell') != '31,64' \
            or not first(L, 'SUPER|disarm|SW_Chrono2|cancel') or armed(L) != ('0', '-'):
        return (f'{label}: a soldier chosen by the Chronosphere was not refused every destination '
                f'with NO_MOVE and kept alive where he stood ({first(L, "CURSORCELL|")}, '
                f'{first(L, "OBJ|E3:Greece#0|")})')
    L = block(B, 'chrono-pick')
    if not first(L, 'SUPER|targetting|SW_Chrono2|') or kv(first(L, 'SUPER|targetting|')).get('id') != '8':
        return f'{label}: the engine never asked for the Chronosphere\'s destination (no targeting event)'
    if armed(L) != ('1', 'SW_Chrono2') or kv(first(L, 'CURSORCELL|')).get('shape') != 'chrono-dest':
        return f'{label}: the destination was not armed with CHRONO_DEST ({armed(L)}, {first(L, "CURSORCELL|")})'
    L = block(B, 'chrono-dest')
    cells = [kv(l).get('cell') for l in L if l.startswith('OBJ|2TNK:Greece#')]
    if not first(L, 'SUPER|fire|SW_Chrono2|id=8|') or armed(L) != ('0', '-') \
            or not spent(entry(L, 'SW_Chrono')) or '33,62' not in cells or '28,64' in cells:
        return f'{label}: the tank was not moved to 33,62 with the Chronosphere spent (tanks on {cells}, {entry(L, "SW_Chrono")})'
    # the nuclear strike
    L = block(B, 'nuke')
    if kv(first(L, 'OBJ|APC:USSR#0|')).get('cell') != '60,83':
        return f'{label}: the nuke\'s mark was not put on 60,83'
    if kv(first(L, 'CURSORCELL|')).get('shape') != 'nuke' or not spent(entry(L, 'SW_Nuke')):
        return f'{label}: the nuclear strike was not aimed and spent ({first(L, "CURSORCELL|")})'
    if count([l for n, L2 in B if n == 'later' for l in L2], 'APC:USSR') != 0:
        return f'{label}: the nuke did not destroy what stood on its target'
    r = check_air(B, 'Greece', label)
    if r:
        return r
    return None, (f'{label}: the GPS satellite launched itself; the Chronosphere missed on bare ground and '
                  f'stayed armed, a soldier was refused every destination (NO_MOVE, the click not sent), the '
                  f'engine asked for the tank\'s destination, CHRONO_SELECT then CHRONO_DEST, the '
                  f'tank moved 28,64 to 33,62 and the charge was spent; the nuke destroyed its mark; spy '
                  f'plane, paratroopers (landed) and parabombs')


def check_s(d):
    label = 'Soviet 2'
    path = os.path.join(d, 's.log')
    if not os.path.isfile(path):
        return f'{label}: no log'
    lines = open(path, errors='replace').read().splitlines()
    if not any(l.startswith('SCRIPT|end') and '0 failures' in l for l in lines):
        return f'{label}: the script did not run to its end without a failure'
    B = blocks(lines)
    L = block(B, 'iron-arm')
    if kv(first(L, 'CURSORCELL|')).get('shape') != 'airstrike' or armed(L) != ('1', 'SW_IronCurtain'):
        return f'{label}: arming the Iron Curtain did not give its cursor ({first(L, "CURSORCELL|")})'
    L = block(B, 'iron-miss')
    if armed(L) != ('1', 'SW_IronCurtain') or entry(L, 'SW_IronCurtain').get('completed') != '1':
        return f'{label}: the Iron Curtain\'s miss did not leave it armed and charged ({armed(L)})'
    L = block(B, 'iron-hit')
    if not first(L, 'SUPER|disarm|SW_IronCurtain|spent') or armed(L) != ('0', '-') \
            or not spent(entry(L, 'SW_IronCurtain')):
        return f'{label}: the Iron Curtain on the tank was not spent ({entry(L, "SW_IronCurtain")})'
    L = block(B, 'iron-hurt')
    o = {kv(l)['cell']: kv(l)['str'] for l in L if l.startswith('OBJ|3TNK:USSR#')}
    if o.get('80,62') != '400/400' or o.get('82,62', '400/400') == '400/400':
        return f'{label}: the covered tank was not the one spared the three hits ({o})'
    r = check_air(B, 'USSR', label)
    if r:
        return r
    return None, (f'{label}: the Iron Curtain missed on bare ground and stayed armed, then covered the tank '
                  f'it was aimed at, which took three hits at full strength while its twin fell to '
                  f'{o["82,62"]}; spy plane, paratroopers (landed) and parabombs')


def check(d):
    msgs = []
    for f in (check_a, check_s):
        r = f(d)
        if isinstance(r, str):
            print(f'0 G356 Red Alert\'s special weapons: {r}')
            return
        msgs.append(r[1])
    print('1 G356 Red Alert\'s special weapons play end to end: ' + '; '.join(msgs))


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('script', 'check'):
        sys.exit(__doc__)
    (script if sys.argv[1] == 'script' else check)(sys.argv[2])
