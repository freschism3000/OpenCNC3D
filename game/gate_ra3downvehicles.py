#!/usr/bin/env python3
"""G345: Red Alert 3D draws its own remastered vehicles and ships under Enhanced: the Heavy
Tank, the V2 Rocket, the Ore Truck, the Ranger, the Mobile Gap Generator, the Supply Truck,
the Mine Layer, the Gun Boat, the Transport and the Destroyer.

  gate_ra3downvehicles.py script DIR    write the missions and the run scripts into DIR
  gate_ra3downvehicles.py check DIR     read the runs' logs and pictures; first line is
                                        "1 message" or "0 message"

Run from a play folder with the Red Alert module staged (ra3d/evhc by
tools/ra3d/stage-vehicles.py: game/evhc_ra/<T>.evhc once the lab approves its row). The
claims are made of the types staged there; a type not staged is named and left out.

THE MISSIONS. A private copy of the module's missions with, added to one mission each, a
Heavy Tank, a V2 Rocket, an Ore Truck, a Ranger, a Mobile Gap Generator, a Supply Truck and a
Mine Layer of each side in two rows four cells
apart, two cells between the types (close enough for the tanks, the launchers and the
Rangers to open fire on each other), and a Gun Boat of each side five cells apart at sea:

  t   Allied 5 (temperate, the player Greece): the rows and the boats, the same frames
      without the models (`evhc 0`), a click on the Allied boat and the boat ordered along
      the coast, the Allied Ranger ordered three cells north and the Allied Gap Generator,
      Supply Truck and Mine Layer a few cells off, the fight sampled, the Mine Layer clicked
      on itself to lay a mine, the Soviet boat hurt and then sunk, the Soviet tank
      and launcher of the rows destroyed, the mode switched to Classic;
  s   Soviet 3 (snow, the player USSR): the same rows and boats, the same frames without
      the models, the Soviet Gap Generator, Supply Truck and Mine Layer ordered a few cells
      off, the fight sampled, the Mine Layer clicked on itself to lay a mine;
  f   each theater's mission (ft, fs) with only a Gap Generator of each side added, away
      from the fight: the player's own one, standing still, shot close up every tick for
      twenty ticks (more than a whole turn of its fork);
  g   Soviet 3 with the shroud on and nothing revealed (the player USSR): a Soviet Heavy
      Tank three cells south of an Allied Gap Generator, the shroud read round them for
      eight seconds; g0 the same without the Gap Generator;
  v   Soviet 3 again: a V2 ordered at an Allied War Factory five cells away, followed
      through two launches and the thirty-second reload between them;
  h   Allied 5 as the disc has it: the mission's own Soviet Ore Truck sampled through its
      first harvest and its first load dumped at the refinery;
  l   Allied 5 as the disc has it, from the start: the Allied Transport the mission opens
      with sails in at the west edge, turns to the shore and puts its troops ashore,
      sampled every two ticks through its door opening and closing again;
  d   Allied 5 (temperate): an Allied Destroyer at sea with a Soviet Gun Boat seven cells
      east of it, the same frame without the model, a click on the Destroyer, the fight
      sampled, the Destroyer ordered north-east and sampled sailing, then hurt and
      sunk;
  e   Soviet 3 (snow): the same Destroyer and Gun Boat, the same frame without the model,
      the fight sampled, the Destroyer hurt and sunk.

CLAIMS:
  (1) every unit and ship of the staged types is drawn by its model and its card stands
      down (RA3DPROBE vehicle model=1), Allied and Soviet, in both theaters; with
      `evhc 0` every one is a card again;
  (2) the frames with the models differ from the same frames with the cards on at least
      4000 pixels of the rows, 1500 of the sea and 1500 of each of the two pictures east of
      the rows' (the Gap Generators; the Supply Trucks and Mine Layers) in each theater, and the Soviet models
      are red, the Allied ones not: of the pixels the models change round each (where
      evhcscreen puts it), type by type the Soviet model's share of deep red is at least 2.5
      per cent and 2 more than the Allied one's of its type (both carry the same red lamps
      and, on a V2, the same red rocket nose; the Ore Truck's red is its cab and frame, its
      ore bed grey, so a quarter of its window or less), and no Allied model's is over 8;
  (3) the Gun Boat floats: its model stands on the sea's surface (the probe's height of the
      model over its point is the model's own, its lift 0) and the wreck of the sunk boat
      lies on a water cell and sinks (EVHCWRECK wet=1, then sinking=1);
  (4) the motions: the Heavy Tank's turret turns off its hull and its barrels kick when it
      fires; each side's Ranger fires, its gun turning and kicking, and the ordered Allied
      Ranger drives, its wheels rolling; the Gun Boat's bow gun turns as the engine's turret and kicks when it fires;
      the ordered boat moves; the V2's erector rises while the engine says it stands to
      fire (doing 3) and reaches the 1996 frames' lift, a V2 never standing to fire keeps
      it down, the rocket is drawn only while the engine says one is on the rail (EVHCV2
      rocket against ammo), and at least one launch is seen (ammo 1 then 0);
  (4b) the V2's firing cycle (v: a V2 ordered at a building in range): it launches twice,
      each time from the top of its lift (the drawn lift at least 0.9 of it on the tick
      before), the launch's nose at least half a cell above the ground, the empty section
      lowering and lying down while it reloads, rising again before the reload ends, and
      the rocket drawn only while the engine says one is on the rail;
  (4c) the V2's rocket in flight (v): each launch's rocket is drawn as the model's own
      rocket from the rail's nose (its first drawn nose within 0.05 cells of it), pitched
      up as it leaves (30 degrees and more), level over the top (a sample within 15) and
      nosing down (30 and more below), its nose within 0.2 cells of the ground at its last
      drawn tick; and no V2 projectile card is drawn under Enhanced while the rockets fly
      (EVHCFLYCARDS v2cards 0, the card stood down on every frame a rocket flew);
  (5) the click on the Allied Gun Boat's model selects it;
  (6) damage and death: the hurt Soviet boat is still drawn by its model at its lower
      strength; killed, it and the Soviet tank and launcher of the rows each break into
      their wrecks (EVHC|death, EVHCWRECK);
  (7) the Ore Truck: while the engine says it lifts ore (doing 1) its scoop rises off the
      ground, and at the refinery (doing 2) its body rises on its rams and tips;
  (8) Classic draws none of it (EVHC on=0);
  (9) the Transport: it is drawn by its model, its card stood down, at the heading the
      engine gives it as it turns (the drawn heading against the engine's facing a constant
      offset, the model's own forward, over a turn of at least 64 of the engine's 256);
      when the engine opens its door the far ramp starts down first and the near one
      (opposite it, in the file's east, north, west, south order) last, all four lie down
      while the door stands open (at least 120 degrees: on the water at the full swing, 190
      and more, or resting on the beach short of it, and at least one at the full swing),
      and all four are closed again once the engine has shut it;
  (10) the Mobile Gap Generator (MGG), its fork the 1996 engine's own eight frames (32 to
      39) stepped one a game tick through half a turn, the far tine moving to the left
      (counterclockwise seen from above): (a) every one, Allied and Soviet in both
      theaters, turns its fork 22.5 degrees a tick (56.25 turns a minute) counterclockwise
      seen from above (EVHCSPIN, the draw's own turn, over the fight's samples); (b) close
      up, the pictures one tick apart differ round the fork, a quarter turn (four ticks)
      apart differ more, and a whole turn (sixteen ticks) apart are the same picture; (c)
      the ordered one drives, its wheels rolling and its front pair steering (Allied on
      temperate, Soviet on snow); (d) the gap is the engine's: with an Allied Gap
      Generator the cells round it, the Soviet player's tank's among them, go from clear
      to hidden and are drawn black, and without it they stay as they were.
  (11) the Supply Truck and the Mine Layer (TRUK, MNLY): the player's own of each, sent a few
      cells off, drives, the truck's wheels rolling and its front pair steering (a third of
      full lock or more), the layer's tracks running (three frames of its road wheels or
      more); clicked on itself where it stopped, the layer lays a mine (the engine's count of
      mines goes up), Allied on temperate, Soviet on snow. The layer's chute flap stays shut:
      no engine state opens it;
  (12) the Destroyer (d, e; Allied only, as Red Alert builds it): drawn by its model with
      its card stood down, the frame differing from the same frame with the card on at
      least 1500 pixels, and not red (as (2)); it floats (lift 0, the model's own height
      over the sea, 0.55 to 0.95 of a cell to its mast's top); its missile launcher astern
      is drawn turned off the hull by exactly the engine's turret facing (tface - face) on
      every sample, through at least three angles, and comes onto the Gun Boat (within 2
      of the engine's 256 of the bearing to it); the engine fires its missiles (at least
      two, UVFX|fire), each with the launcher within 48 of that bearing at the next
      sample (a homing missile leaves up to 35 off, vessel.cpp Can_Fire, and the launcher
      turns 16 a tick); in d a click on its model selects it with no health bar standing
      beside it (a ship takes the vehicles' rule: no cartridge model, no bar, where the
      1996 sprite's width stood a bar two cells tall), the ordered Destroyer sails,
      and the roll it is drawn at (draw_mesh's lean witness) holds one value while it
      stands and takes at least three, spread 0.01 radians and more, as it sails; hurt it
      is still its model at its lower strength; killed it breaks into its wreck, which
      lies on a water cell and sinks.
  (12b) the Cruiser (ca, cs; Allied only): the same claims as the Destroyer's (its two gun
      turrets on the engine's turret facing), and its file carries both turrets (EVHCART
      turret=2), the after one resting astern and turned half a turn more so both point
      along the one facing, and both fire, fore and aft in turn (UVFX|fire second 0 and 1).
PROVEN TO FAIL: the renderer before this work draws every Gun Boat as its card (model=0,
no vessel in evhc's rule) and has no V2 erector (no EVHCV2 line, the erector never moves).
The renderer before the Destroyer's work already draws it from its model once it is staged,
but fails (12) twice: a selected Destroyer stands a health bar two cells tall (HB|DD,
dimw=48), and no lean is dumped for its roll (no wob= in EVHCOBJ).
"""
import json
import os
import re
import shutil
import sys

PLAY = os.getcwd()
OWN = ['3TNK', 'V2RL', 'HARV', 'JEEP', 'MGG', 'TRUK', 'MNLY']
GAP = 'MGG'
TRUCK, LAYER = 'TRUK', 'MNLY'
# the pictures past the rows' own, east along the rows: (name, file stem, the types it
# measures, the type whose cell the camera stands by, cells east of it)
EXTRA = [('Gap Generators', 'gap', [GAP], GAP, -1),
         ('Supply Trucks and Mine Layers', 'more', [TRUCK, LAYER], TRUCK, 1)]
# the half width, pixels, of the window a type's colour is read in: the Supply Truck's red is
# its cab and wings, round a tilt of drab canvas that fills the usual window
HALF = {TRUCK: 40}
# the mines a Mine Layer lays (the Allies' anti-tank, the Soviets' anti-personnel)
MINES = ('MINV', 'MINP')
SHIP = 'PT'
ALL = OWN + [SHIP]
LST = 'LST'
# THE DESTROYER's own runs: the scenario, the Destroyer's cell and facing, the Soviet Gun
# Boat's (seven cells east, inside the Destroyer's missile range of nine), the camera on
# the two, the cell the Destroyer is sent to (Allied 5 only: in Soviet 3 the player cannot
# order it) and the camera on its sail
DD = 'DD'
# THE CRUISER (Allied, as Red Alert builds it), the same way: an Allied Cruiser at sea with a
# Soviet Gun Boat seven cells east, inside its guns' reach
CA = 'CA'
CA_RUNS = {
    'ca': dict(scen='SCG05EA', dd=(36, 108, 64), foe=(43, 108, 192), cam=(40, 108),
               go=(40, 103), cam_go=(39, 106)),
    'cs': dict(scen='SCU03EA', dd=(30, 50, 64), foe=(37, 50, 192), cam=(34, 50), go=None),
}


def check_ca_turrets(lines, did, bad):
    """The Cruiser's own claims: its file has two turrets (EVHCART turret=2), the after one
    resting astern and turned half a turn more, so both point along the one turret facing;
    and both fire, fore and aft in turn (the engine's second shot is the after turret's,
    vessel.cpp Fire_Coord: UVFX|fire second=0 and second=1)."""
    art = [kv(l) for l in lines if l.startswith(f'EVHCART|{CA}|')]
    if not art or art[0].get('turret') != '2':
        bad.append(f'its turrets: {art[0].get("turret") if art else None} in its file (2)')
        return ''
    sec = {kv(l).get('second') for l in lines if l.startswith('UVFX|fire|')
           and kv(l).get('type') == CA and kv(l).get('id') == did}
    if not {'0', '1'} <= sec:
        bad.append(f'its turrets fired only {sorted(sec)} (the fore 0 and the after 1)')
        return ''
    return ' from both turrets, fore and aft in turn'
DD_RUNS = {
    'd': dict(scen='SCG05EA', dd=(36, 108, 64), foe=(43, 108, 192), cam=(40, 108),
              go=(40, 103), cam_go=(39, 106)),
    'e': dict(scen='SCU03EA', dd=(30, 50, 64), foe=(37, 50, 192), cam=(34, 50), go=None),
}
# scenario, the rows (first cell, spacing, gap), the boats (cell, facing: the Allied one
# first), the cameras on the rows and the sea, the cell the Allied boat is sent to, the
# side whose row is the northern one
MISSIONS = {
    't': dict(scen='SCG05EA', rows=(20, 64, 2, 4), boats=((36, 105, 64), (41, 105, 192)),
              cam_rows=(24, 66), cam_sea=(40, 105), go=(38, 102), first='Greece',
              drive=(29, 61), gapdrive=(31, 61), gapclose=(26, 58),
              drives={TRUCK: (33, 59), LAYER: (35, 61)}),
    's': dict(scen='SCU03EA', rows=(46, 61, 2, 4), boats=((41, 53, 192), (36, 53, 64)),
              cam_rows=(50, 63), cam_sea=(40, 53), go=None, first='USSR',
              gapdrive=(53, 63), gapclose=(48, 64),
              drives={TRUCK: (57, 63), LAYER: (60, 62)}),
}
DIST = 1700
# the cells round the gap run's tank and Gap Generator (x0, y0, x1, y1)
GAPWIN = (44, 58, 56, 70)


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


def section(lines, name):
    start = next((i for i, l in enumerate(lines) if l.strip() == f'[{name}]'), None)
    if start is None:
        return None, None
    end = start + 1
    while end < len(lines) and not lines[end].startswith('['):
        end += 1
    return start, end


def add(lines, name, rows):
    start, end = section(lines, name)
    if start is None:
        lines += ['', f'[{name}]']
        start, end = len(lines) - 1, len(lines)
    nums = [int(m.group(1)) for l in lines[start + 1:end] for m in [re.match(r'(\d+)=', l)] if m]
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    n = max(nums) + 1 if nums else 0
    return lines[:end] + [f'{n + k}={r}' for k, r in enumerate(rows)] + lines[end:]


def before(lines, name, house, typ):
    """How many objects of this house and type the mission itself places in a section:
    the index find_object gives the first one added after them."""
    start, end = section(lines, name)
    if start is None:
        return 0
    return sum(1 for l in lines[start + 1:end]
               if re.match(r'\d+=', l) and l.split('=', 1)[1].split(',')[:2] == [house, typ])


def mission(d, tag, m):
    """A private missions folder with the test rows and boats added to one mission; the
    index of each added object among its house's own of its type."""
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, m['scen'] + '.INI')
    lines = open(path, errors='replace').read().splitlines()
    cell = lambda x, y: y * 128 + x
    x0, y0, step, gap = m['rows']
    first = m['first']
    other = 'USSR' if first == 'Greece' else 'Greece'
    nth = {}
    units = []
    for side, y, face in ((first, y0, 128), (other, y0 + gap, 0)):
        for i, t in enumerate(OWN):
            nth[(side, t)] = before(lines, 'UNITS', side, t)
            units.append(f'{side},{t},256,{cell(x0 + step * i, y)},{face},Guard,None')
    ships = []
    for side, (bx, by, face) in zip(('Greece', 'USSR'), m['boats']):
        nth[(side, SHIP)] = before(lines, 'SHIPS', side, SHIP)
        ships.append(f'{side},{SHIP},256,{cell(bx, by)},{face},Guard,None')
    lines = add(add(lines, 'UNITS', units), 'SHIPS', ships)
    write(path, lines)
    return nth


def sample(d, tag, m, name, n, keep=False):
    """n samples two ticks apart: the rows and the sea shot, the probe and the dump. Only
    the first and last of a run keep their own picture unless `keep`."""
    cr, cs = m['cam_rows'], m['cam_sea']
    out = []
    for k in range(n):
        nm = f'{name}{k:02d}' if keep or k in (0, n - 1) else name
        out += ['tick 2', f'cam {cr[0]} {cr[1]}', f'shot {d}/{tag}_{nm}r.png',
                f'cam {cs[0]} {cs[1]}', f'shot {d}/{tag}_{nm}s.png', 'ra3dprobe', 'evhcdump']
    return out


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, m in MISSIONS.items():
        nth = mission(d, tag, m)
        cr, cs = m['cam_rows'], m['cam_sea']
        # (the types at the rows' east end, past the rows' picture: pictures of their own)
        x0, y0, step, gap = m['rows']
        cams = [(x0 + step * OWN.index(at) + dx, y0 + gap // 2) for _, _, _, at, dx in EXTRA]
        # (the Rangers at the rows' east end, a picture for the eye: {tag}_ranger.png)
        s = ['tick 2', 'reveal 1', f'dist {DIST}', f'cam {cr[0] + 2} {cr[1]}',
             f'shot {d}/{tag}_ranger.png'] + [f'count {mn}' for mn in MINES]
        for on in (True, False):
            sfx = 'on' if on else 'off'
            for (cx, cy), stem in [(cr, 'rows'), (cs, 'sea')] + [(c, e[1]) for c, e in zip(cams, EXTRA)]:
                s += [f'cam {cx} {cy}', f'shot {d}/{tag}_{stem}_{sfx}.png', 'ra3dprobe']
                s += ['evhcscreen'] if on else []
            s += ['evhcdump', 'evhc 0'] if on else ['evhc 1']
        if m['go']:
            s += [f'cam {cs[0]} {cs[1]}', f'lclickobj PT:Greece {nth[("Greece", SHIP)]}', 'select',
                  f'order {m["go"][0]} {m["go"][1]}']
        if m.get('drive'):
            s += [f'cam {cr[0]} {cr[1]}', f'lclickobj JEEP:Greece {nth[("Greece", "JEEP")]}',
                  'select', f'order {m["drive"][0]} {m["drive"][1]}']
        # (the player's own Gap Generator, Supply Truck and Mine Layer, each sent a few cells
        # off, the camera on the one clicked)
        sent = dict(m.get('drives', {}))
        if m.get('gapdrive'):
            sent[GAP] = m['gapdrive']
        for typ, (gx, gy) in sorted(sent.items()):
            s += [f'cam {x0 + step * OWN.index(typ)} {y0}',
                  f'lclickobj {typ}:{m["first"]} {nth[(m["first"], typ)]}', 'select', f'order {gx} {gy}']
        s += sample(d, tag, m, 'fight', 15, keep=True)
        if m.get('drives'):
            # THE MINE: the player's Mine Layer, arrived, clicked on itself to lay one, then
            # the mines counted and a picture taken
            lay = f'{LAYER}:{m["first"]} {nth[(m["first"], LAYER)]}'
            lx, ly = m['drives'][LAYER]
            s += ['tick 30', f'obj {lay}', f'cam {lx} {ly}', f'lclickobj {lay}', f'actclickobj {lay}', 'tick 60',
                  f'obj {lay}'] + [f'count {mn}' for mn in MINES] + [
                  f'cam {lx} {ly + 1}', 'dist 900', f'shot {d}/{tag}_mine.png', f'dist {DIST}']
        if tag == 't':
            s += [f'hurtunit PT:USSR {nth[("USSR", SHIP)]} 120']
            s += sample(d, tag, m, 'hurt', 2)
            s += [f'hurtunit PT:USSR {nth[("USSR", SHIP)]} 5000',
                  f'hurtunit 3TNK:USSR {nth[("USSR", "3TNK")]} 5000',
                  f'hurtunit V2RL:USSR {nth[("USSR", "V2RL")]} 5000']
            s += sample(d, tag, m, 'dead', 12)
            s += ['gfxmode 0', 'tick 1', 'evhcdump']
        s += ['quit']
        write(f'{d}/{tag}.txt', s)
    # THE GAP GENERATOR'S FORK CLOSE UP: a Gap Generator of each side added to the theater's
    # mission away from the fight, the player's own one standing still; shot every tick
    for tag, m in MISSIONS.items():
        dst = os.path.join(d, 'mf' + tag)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
        fp = os.path.join(dst, m['scen'] + '.INI')
        fl = open(fp, errors='replace').read().splitlines()
        gx, gy = m['gapclose']
        other = 'USSR' if m['first'] == 'Greece' else 'Greece'
        # (Sleep: an unarmed unit on Guard may wander off)
        fl = add(fl, 'UNITS', [f'{m["first"]},{GAP},256,{gy * 128 + gx},0,Sleep,None',
                               f'{other},{GAP},256,{gy * 128 + gx + 6},0,Sleep,None'])
        write(fp, fl)
        # (a second for the picture to settle before the first shot)
        f = ['gfx cloud_on 0', 'tick 2', 'reveal 1', 'dist 500', f'cam {gx} {gy + 1}', 'tick 15', 'evhcscreen',
             'ra3dprobe']
        for k in range(21):
            f += ['tick 1', f'cam {gx} {gy + 1}', f'shot {d}/f{tag}_{k:02d}.png', 'evhcdump']
        f += ['quit']
        write(f'{d}/f{tag}.txt', f)
    # THE GAP: Soviet 3 with the shroud on, nothing revealed; a Soviet Heavy Tank three cells
    # south of an Allied Gap Generator (g) and the tank alone (g0), the shroud round them read
    # and shot every twenty ticks
    for tag, with_gap in (('g', True), ('g0', False)):
        dst = os.path.join(d, 'm' + tag)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
        gp = os.path.join(dst, 'SCU03EA.INI')
        gl = open(gp, errors='replace').read().splitlines()
        rows = [f'USSR,3TNK,256,{67 * 128 + 50},0,Guard,None']
        if with_gap:
            rows.append(f'Greece,{GAP},256,{63 * 128 + 50},0,Guard,None')
        gl = add(gl, 'UNITS', rows)
        write(gp, gl)
        g = ['tick 2', 'dist 1100']
        for k in range(6):
            g += ['tick 20', 'cam 50 65', f'shot {d}/{tag}_{k}.png', 'ra3dprobe', 'shroudstat']
            g += [f'shroudat {x} {y}' for y in range(GAPWIN[1], GAPWIN[3] + 1)
                  for x in range(GAPWIN[0], GAPWIN[2] + 1)]
        g += ['quit']
        write(f'{d}/{tag}.txt', g)
    # THE V2'S FIRING CYCLE: a Soviet V2 on Soviet 3 ordered at an Allied War Factory five
    # cells north (inside its sight, so the order is an attack), dumped every tick through
    # the raise, the launch and the lowering, then through the reload's end and the second
    # launch
    dst = os.path.join(d, 'mv')
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    vp = os.path.join(dst, 'SCU03EA.INI')
    vl = open(vp, errors='replace').read().splitlines()
    cell = lambda x, y: y * 128 + x
    vl = add(vl, 'UNITS', [f'USSR,V2RL,256,{cell(52, 68)},32,Guard,None'])
    vl = add(vl, 'STRUCTURES', [f'Greece,WEAP,256,{cell(51, 63)},0,None,1,0'])
    write(vp, vl)
    # (each dump after a shot: the dump's drawn lift and rocket are what that frame drew)
    v = ['tick 2', 'reveal 1', 'cam 53 66', 'lclickobj V2RL:USSR 0', 'select', 'actcell 52 64']
    # (the first seventy ticks every tick: the raise, the launch and the rocket's whole
    # flight; every third shot kept for the flight's strip)
    for k in range(70):
        v += ['tick 1', f'shot {d}/v_fly{k:02d}.png' if k % 3 == 0 else f'shot {d}/v_cycle.png',
              'evhcdump']
    for k in range(12):
        v += ['tick 30', f'shot {d}/v_cycle.png', 'evhcdump']
    v += ['tick 10']
    for k in range(50):
        v += ['tick 1', f'shot {d}/v_cycle.png', 'evhcdump']
    v += ['quit']
    write(f'{d}/v.txt', v)
    # THE TRANSPORT LANDING TROOPS: Allied 5 opens with an Allied Transport sailing in at the
    # west edge and putting two rocket soldiers and three engineers ashore (its team frc1a);
    # sampled every two ticks from the start through its door opening and closing again
    lcam = 'cam 16 50'
    lv = ['reveal 1', 'dist 900']
    for k in range(75):
        nm = f'l_land{k:02d}.png' if k % 2 == 0 else 'l_land.png'
        lv += ['tick 2', lcam, f'shot {d}/{nm}', 'ra3dprobe', 'evhcdump']
    lv += ['quit']
    write(f'{d}/l.txt', lv)
    if DD in staged():
        script_ship(d, DD, DD_RUNS)
    if CA in staged():
        script_ship(d, CA, CA_RUNS)
    # the Ore Truck through its first harvest and its first dump, on the disc's mission
    h = ['tick 2', 'reveal 1', 'dist 1100', 'tick 480']
    for k in range(40):
        h += ['tick 5', 'cam 61 76', f'shot {d}/h_lift.png', 'evhcdump']
    h += ['tick 760']
    for k in range(80):
        h += ['tick 3', 'cam 61 94', f'shot {d}/h_dump.png', 'evhcdump']
    h += ['quit']
    write(f'{d}/h.txt', h)


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


def dumps(lines, tag):
    """[[{fields}]] per evhcdump, of the lines starting with tag."""
    out, cur = [], None
    for l in lines:
        if l.startswith('EVHC|loaded='):
            cur = []
            out.append(cur)
        elif l.startswith(tag) and cur is not None:
            k = kv(l)
            k['type'] = l.split('|')[1]
            cur.append(k)
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


def colours(x, ch, scr):
    """{(type, house): [share of red]}: of the pixels the models change round each model on
    screen (the hull and the ground its card covered), the share that is deep red (red at
    least 1.8 times green and blue). A Soviet model's house paint makes it large; an Allied
    one has only its lamps and, on a V2, the rocket's red nose."""
    out = {}
    for typ, house, c, r in scr:
        if not (0 <= c < 1040 and 0 <= r < x.shape[0]):
            continue
        h = HALF.get(typ, 24)
        r0, r1, c0, c1 = max(r - h, 0), min(r + h, x.shape[0]), max(c - h, 0), min(c + h, 1040)
        m = ch[r0:r1, c0:c1] & (x[r0:r1, c0:c1].mean(-1) < 190)
        if m.sum() < 60:
            continue
        px = x[r0:r1, c0:c1][m]
        # deep red: the house paint, not a muzzle flash's orange
        red = (px[:, 0] > 50) & (px[:, 0] > 1.8 * px[:, 1]) & (px[:, 0] > 1.8 * px[:, 2])
        out.setdefault((typ, house), []).append(float(red.mean()))
    return out


def screens(lines, n):
    """The n-th evhcscreen block: [(type, house, col, row)]."""
    blocks, cur, prev = [], None, False
    for l in lines:
        is_scr = l.startswith('EVHCSCR|')
        if is_scr and not prev:
            cur = []
            blocks.append(cur)
        if is_scr:
            k = kv(l)
            c, r = (int(float(v)) for v in k['scr'].split(','))
            cur.append((l.split('|')[1], k.get('house', ''), c, r))
        prev = is_scr
    return blocks[n] if n < len(blocks) else []


def spin_check(lines, pp, scene):
    """(10a): every Gap Generator, both sides, turns its fork 22.5 degrees a tick
    counterclockwise seen from above, from one dump to the next."""
    house = {v['id']: v['house'] for pr in pp for v in vehicles(pr) if v['type'] == GAP}
    seq = {}
    for l in lines:
        if l.startswith(f'EVHCSPIN|{GAP}|'):
            k = kv(l)
            seq.setdefault(k['id'], []).append((float(k['t']), float(k['ccw'])))
    rates, sides = [], set()
    for gid, s in seq.items():
        s = sorted(set(s))
        for (t0, a0), (t1, a1) in zip(s, s[1:]):
            if t1 - t0 < 0.5 or t1 - t0 > 7.5:
                continue
            rates.append(((a1 - a0) % 360.0) / (t1 - t0))
            sides.add(house.get(gid, '?'))
    bad = [r for r in rates if abs(r - 22.5) > 0.05]
    if len(rates) < 10 or bad or not {'Greece', 'USSR'} <= sides:
        return False, (f'{scene}: the Gap Generators\' forks: {len(rates)} steps measured on '
                       f'{sorted(sides)}, {len(bad)} not 22.5 degrees a tick counterclockwise '
                       f'({sorted(set(round(r, 2) for r in bad))[:6]})')
    return True, (f'{scene}: both sides\' Gap Generator forks turn 22.5 degrees a tick '
                  f'counterclockwise from above, the 1996 frames\' way ({len(rates)} steps)')


def drive_check(lines, house, typ, steered):
    """The player's own one of a type, sent off: it moves (the probe's first picture against
    the last); a wheeled one's wheels roll (at least three travels) and its front pair steers
    (a third of full lock or more), a tracked one's tracks run (at least three travels and
    three frames of its road wheels)."""
    pp = probes(lines)
    first = [v for v in vehicles(pp[1]) if v['type'] == typ and v['house'] == house] if len(pp) > 1 else []
    last = [v for v in vehicles(pp[-1]) if v['type'] == typ and v['house'] == house] if pp else []
    tid = first[0]['id'] if first else None
    objs = [o for s_ in dumps(lines, 'EVHCOBJ|') for o in s_ if o['type'] == typ and o.get('id') == tid]
    trav = {o['trav'] for o in objs}
    frames = {o['frame'] for o in objs}
    steer = max([abs(float(o['turn'])) for o in objs] or [0.0])
    moved = bool(first and last and first[0]['at'] != last[0]['at'])
    ok = moved and len(trav) >= 3 and (steer >= 0.3 if steered else len(frames) >= 3)
    if not ok:
        return False, (f'sent off: moved {moved} ({first[:1]} {last[:1]}), {len(trav)} travels, '
                       f'{len(frames)} track frames, steer {steer:.2f}')
    what = (f'its wheels rolling, its front pair steering to {steer:.2f} of full lock' if steered
            else f'its tracks running ({len(frames)} frames of its road wheels)')
    return True, f'drives from {first[0]["at"]} to {last[0]["at"]}, {what}'


def fork_close(d, tag, scene):
    """(10b): the player's own Gap Generator standing still, shot every tick: one tick apart
    the pictures differ round the fork, four ticks (a quarter turn) apart more, sixteen
    ticks (a whole turn) apart they are the same picture."""
    from PIL import Image
    import numpy as np
    m = MISSIONS[tag]
    lines = read(f'{d}/f{tag}.log')
    scr = [s for s in screens(lines, 0) if s[0] == GAP and s[1] == m['first']]
    pp = probes(lines)
    mine = [v for v in (vehicles(pp[0]) if pp else []) if v['type'] == GAP and v['house'] == m['first']]
    if not scr or not mine or mine[0]['model'] != '1':
        return False, f'{scene}: the close-up Gap Generator: on screen {scr[:1]}, probe {mine[:1]}'
    gid = mine[0]['id']
    trav = {o['trav'] for s_ in dumps(lines, 'EVHCOBJ|') for o in s_ if o['type'] == GAP and o.get('id') == gid}
    c, r = scr[0][2], scr[0][3]
    try:
        ims = [np.asarray(Image.open(f'{d}/f{tag}_{k:02d}.png').convert('RGB')).astype(int)
               [max(r - 70, 0):r + 10, max(c - 50, 0):c + 50] for k in range(21)]
    except OSError:
        return False, f'{scene}: the close-up pictures are missing'
    diff = lambda a, b: int((np.abs(ims[a] - ims[b]).sum(-1) > 30).sum())
    d1 = [diff(k, k + 1) for k in range(20)]
    d4 = [diff(k, k + 4) for k in range(17)]
    d16 = [diff(k, k + 16) for k in range(5)]
    # (the window holds the fork and the lorry's top round it, not the ground's flickers
    # beside it: a fire's light, a lamp's glow on the snow)
    if len(trav) != 1 or min(d1) < 300 or min(d4) < 800 or max(d16) > 0.2 * min(d4):
        return False, (f'{scene}: the fork close up: {len(trav)} wheel travels (standing: 1), '
                       f'pixels one tick apart {min(d1)}..{max(d1)}, a quarter turn apart '
                       f'{min(d4)}..{max(d4)}, a whole turn apart {d16}')
    return True, (f'{scene}: the fork close up turns every tick ({min(d1)} pixels or more), '
                  f'a quarter turn changes {min(d4)} or more, a whole turn {max(d16)} or fewer')


def shroud_grids(lines):
    out, cur = [], None
    for l in lines:
        if l.startswith('SHROUDSTAT|'):
            cur = {}
            out.append(cur)
        elif l.startswith('SHROUDAT|') and cur is not None:
            q = l.strip().split('|')
            if ',' in q[1] and q[1].replace(',', '').replace('-', '').isdigit():
                x, y = (int(v) for v in q[1].split(','))
                cur[(x, y)] = q[2]
    return out


def gap_check(d):
    """(10d): an Allied Gap Generator hides the cells round it from the Soviet player, the
    tank's among them, and they are drawn black; without it nothing changes."""
    from PIL import Image
    import numpy as np
    g, g0 = shroud_grids(read(f'{d}/g.log')), shroud_grids(read(f'{d}/g0.log'))
    if len(g) < 6 or len(g0) < 6:
        return False, f'the gap: {len(g)} and {len(g0)} shroud samples'
    hid = lambda G: sum(1 for v in G.values() if v == 'HIDDEN')
    tank = (50, 67)
    lum = []
    for name in ('g_5.png', 'g0_5.png'):
        try:
            lum.append(float(np.asarray(Image.open(f'{d}/{name}').convert('L')).astype(float)[:, :1040].mean()))
        except OSError:
            lum.append(-1.0)
    same0 = all(G == g0[0] for G in g0)
    if g[0].get(tank) != 'CLEAR' or g[-1].get(tank) != 'HIDDEN' or hid(g[-1]) < hid(g[0]) + 8        or not same0 or g0[-1].get(tank) != 'CLEAR' or not 0 <= lum[0] < 0.7 * lum[1]:
        return False, (f'the gap: the tank\'s cell {g[0].get(tank)} then {g[-1].get(tank)}, hidden '
                       f'cells {hid(g[0])} then {hid(g[-1])}; without the Gap Generator unchanged '
                       f'{same0} ({g0[-1].get(tank)}); brightness {lum}')
    return True, (f'the gap: an Allied Gap Generator hides {hid(g[-1]) - hid(g[0])} more cells round it '
                  f'from the Soviet player, the tank\'s among them, drawn black (brightness '
                  f'{lum[0]:.0f} against {lum[1]:.0f} without it)')


def check(d):
    fails, notes = [], []
    st = staged()
    types = [t for t in ALL if t in st]
    missing = [t for t in ALL if t not in st]
    if not types:
        print('0 none of ' + ', '.join(ALL) + ' is staged in ra3d/evhc (tools/ra3d/stage-vehicles.py)')
        return
    if missing:
        notes.append('not staged, left out: ' + ', '.join(missing))
    logs = {tag: read(f'{d}/{tag}.log') for tag in ('t', 's', 'h')}
    for tag, scene in (('t', 'temperate'), ('s', 'snow')):
        lines = logs[tag]
        p = probes(lines)
        npic = 2 + len(EXTRA)
        if len(p) < 2 * npic:
            fails.append(f'{scene}: the run printed {len(p)} probes')
            continue
        # (1) models and cards, the rows' probe, the sea's and the pictures east of the
        # rows, with the models and without
        on = [v for k in range(npic) for v in vehicles(p[k])]
        off = [v for k in range(npic, 2 * npic) for v in vehicles(p[k])]
        seen = {(v['type'], v['house']) for v in on if v['type'] in types and v['visible'] == '1'
                and v['model'] == '1'}
        want = {(t, h) for t in types for h in ('Greece', 'USSR')}
        cards = sorted({f'{v["type"]}/{v["house"]}' for v in on if v['type'] in types
                        and v['visible'] == '1' and v['model'] != '1'})
        offm = sorted({v['type'] for v in off if v['model'] != '0'})
        if want - seen or cards or offm:
            fails.append(f'{scene}: not drawn by their models {sorted(want - seen)}, cards {cards}, '
                         f'still models with evhc 0: {offm}')
        else:
            notes.append(f'{scene}: {len(want)} types and sides as their models')
        # (2) pixels and colours
        parts = [('rows', 'rows_on.png', 'rows_off.png', 0, 4000, types),
                 ('sea', 'sea_on.png', 'sea_off.png', 1, 1500, types)]
        for k, (name, stem, which, _, _) in enumerate(EXTRA):
            which = [w for w in which if w in types]
            if which:
                parts.append((name, f'{stem}_on.png', f'{stem}_off.png', 2 + k, 1500, which))
        for part, a, b, n, floor, which in parts:
            x, y, ch = pixels(d, f'{tag}_{a}', f'{tag}_{b}')
            npx = -1 if ch is None else int(ch.sum())
            if npx < floor:
                fails.append(f'{scene} {part}: the models change {npx} pixels against the cards ({floor})')
                continue
            scr = [s for s in screens(lines, n) if s[0] in which]
            col = colours(x, ch, scr)
            # type by type, the Soviet model reads red and redder than the Allied one of its
            # type by a margin (both carry the same red lamps and, on a V2, the same red
            # rocket nose); no Allied model reads red
            pairs, bad = 0, []
            for typ in types:
                sov, alli = col.get((typ, 'USSR'), []), col.get((typ, 'Greece'), [])
                if any(a > 0.08 for a in alli):
                    bad.append(f'Allied {typ} {max(alli):.3f}')
                if not sov or not alli:
                    continue
                pairs += 1
                if min(sov) < 0.025 or min(sov) < max(alli) + 0.02:
                    bad.append(f'{typ} Soviet {min(sov):.3f} against Allied {max(alli):.3f}')
            if bad or pairs < (2 if part == 'rows' else len(which) if part in [e[0] for e in EXTRA] else 1):
                fails.append(f'{scene} {part}: side colours, the red share round each model: '
                             f'{"; ".join(bad) or "none wrong"}, {pairs} types measured both sides')
            else:
                notes.append(f'{scene} {part}: Soviet red, Allied not ({pairs} types compared)')
        # (3) the boat on the sea
        boats = [v for v in on if v['type'] == SHIP and v['model'] == '1']
        if SHIP in types and (not boats or any(float(v['lift']) != 0.0 or not 0.2 < float(v['height']) < 0.5
                                               for v in boats)):
            fails.append(f'{scene}: the Gun Boat is not on the sea: {[(v["lift"], v["height"]) for v in boats]}')
        # (4) the motions
        sm = dumps(lines, 'EVHCOBJ|')
        flat = [o for s in sm for o in s]
        if '3TNK' in types:
            tur = any(o['type'] == '3TNK' and o['tur'] not in ('0', '-1') for o in flat)
            kick = any(o['type'] == '3TNK' and float(o['kick']) > 0 for o in flat)
            if not tur or not kick:
                fails.append(f'{scene}: the Heavy Tank: turret turned {tur}, barrels kicked {kick}')
        if 'JEEP' in types:
            tur = any(o['type'] == 'JEEP' and o['tur'] not in ('0', '-1') for o in flat)
            kick = any(o['type'] == 'JEEP' and float(o['kick']) > 0 for o in flat)
            fired = [l for l in lines if l.startswith('EVHC|fire|') and kv(l).get('type') == 'JEEP']
            shooters = {kv(l)['key'] for l in fired}
            if not tur or not kick or len(shooters) < 2:
                fails.append(f'{scene}: the Ranger: gun turned {tur}, kicked {kick}, {len(fired)} shots '
                             f'from {len(shooters)} of the two')
            else:
                notes.append(f'{scene}: both Rangers\' guns turn, kick and fire ({len(fired)} shots)')
        if SHIP in types:
            tur = {o['tur'] for o in flat if o['type'] == SHIP}
            kick = any(o['type'] == SHIP and float(o['kick']) > 0 for o in flat)
            fired = [l for l in lines if l.startswith('EVHC|fire|') and kv(l).get('type') == SHIP]
            if len(tur) < 3 or not kick or not fired:
                fails.append(f'{scene}: the Gun Boat\'s bow gun: {len(tur)} turret angles, kick {kick}, '
                             f'{len(fired)} shots')
            else:
                notes.append(f'{scene}: the Gun Boat\'s gun turns and kicks ({len(fired)} shots)')
        if 'V2RL' in types:
            v2 = [o for s in dumps(lines, 'EVHCV2|') for o in s]
            up = [o for o in v2 if o['doing'] == '3']
            full = [o for o in v2 if float(o['drawnerect']) >= 0.999]
            wrong = [o for o in v2 if (o['ammo'] == '0') == (o['rocket'] == '1')]
            # a V2 the engine never had standing to fire keeps its erector down throughout
            stood = {o['key'] for o in up}
            down = all(float(o['erect']) == 0.0 for o in v2 if o['key'] not in stood)
            launched = {o['key'] for o in v2 if o['ammo'] == '0'} & {o['key'] for o in v2 if o['ammo'] == '1'}
            if not up or not full or wrong or not launched:
                fails.append(f'{scene}: the V2\'s erector: {len(up)} samples standing to fire, {len(full)} '
                             f'at full lift, {len(wrong)} with the rocket drawn against the engine\'s ammo, '
                             f'{len(launched)} launches seen')
            else:
                notes.append(f'{scene}: the V2s raise their erectors and fire ({len(launched)} launched)')
            if not down:
                fails.append(f'{scene}: a V2 the engine never had standing to fire raised its erector')
        # (10a) the Gap Generators' forks: 22.5 degrees a tick, counterclockwise from above
        if GAP in types:
            notes_or_fail = spin_check(lines, probes(lines), scene)
            (notes if notes_or_fail[0] else fails).append(notes_or_fail[1])
            # (10c) the ordered one drove, its wheels rolling and its front pair steering
            m = MISSIONS[tag]
            pp = probes(lines)
            first = [v for v in vehicles(pp[1]) if v['type'] == GAP and v['house'] == m['first']]
            last = [v for v in vehicles(pp[-1]) if v['type'] == GAP and v['house'] == m['first']]
            gid = first[0]['id'] if first else None
            objs = [o for s_ in dumps(lines, 'EVHCOBJ|') for o in s_ if o['type'] == GAP and o.get('id') == gid]
            trav = {o['trav'] for o in objs}
            steer = max([abs(float(o['turn'])) for o in objs] or [0.0])
            side = 'Allied' if m['first'] == 'Greece' else 'Soviet'
            if not first or not last or first[0]['at'] == last[0]['at'] or len(trav) < 3 or steer < 0.3:
                fails.append(f'{scene}: the ordered {side} Gap Generator: {first[:1]} {last[:1]}, '
                             f'{len(trav)} wheel travels, steer {steer:.2f}')
            else:
                notes.append(f'{scene}: the {side} Gap Generator drives from {first[0]["at"]} to '
                             f'{last[0]["at"]}, its wheels rolling, its front pair steering to {steer:.2f} of full lock')
        # (11) the Supply Truck and the Mine Layer the player sent off: the truck's wheels
        # rolling and its front pair steering, the layer's tracks running; the layer lays a
        # mine where it stopped
        m = MISSIONS[tag]
        side = 'Allied' if m['first'] == 'Greece' else 'Soviet'
        if TRUCK in types:
            ok, msg = drive_check(lines, m['first'], TRUCK, steered=True)
            (notes if ok else fails).append(f'{scene}: the {side} Supply Truck {msg}')
        if LAYER in types:
            ok, msg = drive_check(lines, m['first'], LAYER, steered=False)
            (notes if ok else fails).append(f'{scene}: the {side} Mine Layer {msg}')
            counts = [(kv(l.replace('COUNT|', 'type=', 1)).get('type'), int(kv(l).get('have', -1)))
                      for l in lines if l.startswith('COUNT|')]
            before = sum(h for ty, h in counts[:len(MINES)])
            after = sum(h for ty, h in counts[len(MINES):2 * len(MINES)])
            if len(counts) < 2 * len(MINES) or after <= before:
                fails.append(f'{scene}: the {side} Mine Layer laid no mine (mines {before} then {after})')
            else:
                notes.append(f'{scene}: the {side} Mine Layer lays a mine (mines {before} then {after})')
    # (10b) the fork close up, every tick, and (10d) the gap
    if GAP in types:
        for tag, scene in (('t', 'temperate'), ('s', 'snow')):
            ok, msg = fork_close(d, tag, scene)
            (notes if ok else fails).append(msg)
        ok, msg = gap_check(d)
        (notes if ok else fails).append(msg)
    t = logs['t']
    # (4) the ordered boat moved
    p = probes(t)
    if SHIP in types and p:
        first = [v for v in vehicles(p[1]) if v['type'] == SHIP and v['house'] == 'Greece']
        last = [v for v in vehicles(p[-1]) if v['type'] == SHIP and v['house'] == 'Greece']
        if not first or not last or first[0]['at'] == last[0]['at']:
            fails.append(f'the ordered Gun Boat did not move: {first[:1]} {last[:1]}')
        else:
            notes.append(f'the Gun Boat sails from {first[0]["at"]} to {last[0]["at"]}')
    # (4) the ordered Ranger drove, its wheels rolling
    if 'JEEP' in types and p:
        first = [v for v in vehicles(p[1]) if v['type'] == 'JEEP' and v['house'] == 'Greece']
        last = [v for v in vehicles(p[-1]) if v['type'] == 'JEEP' and v['house'] == 'Greece']
        jid = first[0]['id'] if first else None
        trav = {o['trav'] for s_ in dumps(t, 'EVHCOBJ|') for o in s_
                if o['type'] == 'JEEP' and o.get('id') == jid}
        if not first or not last or first[0]['at'] == last[0]['at'] or len(trav) < 3:
            fails.append(f'the ordered Ranger: {first[:1]} {last[:1]}, {len(trav)} wheel travels')
        else:
            notes.append(f'the Ranger drives from {first[0]["at"]} to {last[0]["at"]}, its wheels '
                         f'rolling')
    # (5) the click
    if SHIP in types and not any(l.startswith('SELECT|') and 'type=PT' in l and 'house=Greece' in l
                                 and l.endswith('selected') for l in t):
        fails.append('a click on the Allied Gun Boat\'s model did not select it')
    # (6) damage and death
    hurt = [v for pp in p for v in vehicles(pp) if v['type'] == SHIP and v['house'] == 'USSR'
            and v['model'] == '1' and int(v['str'].split('/')[0]) < 200]
    deaths = {kv(l)['type'] for l in t if l.startswith('EVHC|death|')}
    wrecks = dumps(t, 'EVHCWRECK|')
    flatw = [w for s in wrecks for w in s]
    need = [x for x in ('PT', '3TNK', 'V2RL') if x in types]
    if SHIP in types and not hurt:
        fails.append('the hurt Gun Boat was not drawn by its model at its lower strength')
    if any(x not in deaths for x in need) or any(not any(w['type'] == x for w in flatw) for x in need):
        fails.append(f'the deaths: {sorted(deaths)} died, wrecks of {sorted({w["type"] for w in flatw})}')
    elif SHIP in types:
        wet = [w for w in flatw if w['type'] == SHIP and w['wet'] == '1']
        sank = [w for w in wet if w['sinking'] == '1']
        if not wet or not sank:
            fails.append(f'the Gun Boat\'s wreck: {len(wet)} samples on water, {len(sank)} sinking')
        else:
            notes.append('the boat hurt, sunk and its wreck going down; tank and launcher wrecked')
    # (7) the Ore Truck
    if 'HARV' in types:
        ore = [o for s in dumps(logs['h'], 'EVHCORE|') for o in s]
        lifts = [float(o['scoop']) for o in ore if o['doing'] == '1']
        dump = [(float(o['lift']), float(o['tip'])) for o in ore if o['doing'] == '2']
        if not lifts or max(lifts) < 0.5 or not dump or max(a for a, b in dump) < 0.99 \
           or max(b for a, b in dump) < 0.5:
            fails.append(f'the Ore Truck: scoop {max(lifts) if lifts else None} while lifting, '
                         f'{len(dump)} dump samples, body {dump[-1:] if dump else None}')
        else:
            notes.append(f'the Ore Truck\'s scoop rises to {max(lifts):.2f}, its body rises and tips '
                         f'to {max(b for a, b in dump):.2f} at the refinery')
    # (4b) the V2's firing cycle
    if 'V2RL' in types:
        vl = read(f'{d}/v.log')
        v2 = [o for s in dumps(vl, 'EVHCV2|') for o in s]
        launches = [kv(l) for l in vl if l.startswith('EVHC|launch|')]
        before = []                                  # the drawn lift on the dump before each launch
        prev = None
        for o in v2:
            if prev is not None and prev['ammo'] != '0' and o['ammo'] == '0':
                before.append(float(prev['drawnerect']))
            prev = o
        empty_down = any(o['ammo'] == '0' and float(o['drawnerect']) == 0.0 for o in v2)
        lowered = any(o['ammo'] == '0' and 0.0 < float(o['drawnerect']) < 0.9 for o in v2)
        wrong = [o for o in v2 if (o['ammo'] == '0') == (o['rocket'] == '1')]
        # the empty section rising before the reload ends (the rail up when the rocket is back)
        rising_empty = any(o['ammo'] == '0' and o['doing'] == '3' and int(o['firein']) <= 8
                           and float(o['erect']) > 0.0 for o in v2)
        tips = [float(l['tip'].split(',')[1]) - float(l.get('ground', '0')) for l in launches]
        if len(launches) < 2 or not before or min(before) < 0.9 or not empty_down or not lowered \
           or wrong or not rising_empty or min(tips or [0.0]) < 0.5:
            fails.append(f'the V2\'s firing cycle: {len(launches)} launches, drawn lift before each '
                         f'{before}, lowered {lowered}, down while empty {empty_down}, the empty '
                         f'rail rising for the reload {rising_empty}, {len(wrong)} rocket against '
                         f'ammo, launch heights {tips}')
        else:
            notes.append(f'the V2 fires from the top of its lift ({len(launches)} launches), lowers '
                         f'empty, reloads and rises again')
        # (4c) the rocket in flight
        fly = {}
        for l in vl:
            if l.startswith('EVHCFLY|'):
                k = kv(l)
                fly.setdefault(k['launch'], []).append(k)
        cards = [kv(l) for l in vl if l.startswith('EVHCFLYCARDS|')]
        bad = []
        for launch, recs in sorted(fly.items(), key=lambda x: int(x[0])):
            drawn = [r for r in recs if r['drawn'] == '1']
            if len(drawn) < 8:
                bad.append(f'launch {launch}: {len(drawn)} samples drawn')
                continue
            n0 = [float(v) for v in drawn[0]['nose'].split(',')]
            tip = [float(v) for v in drawn[0]['tip'].split(',')]
            off = max(abs(a - b) for a, b in zip(n0, tip))
            pitches = [float(r['pitch']) for r in drawn]
            last = drawn[-1]
            low = float(last['nose'].split(',')[1]) - float(last['ground'])
            if off > 0.05 or max(pitches) < 30.0 or min(pitches) > -30.0 \
               or not any(abs(p) < 15.0 for p in pitches) or low > 0.2:
                bad.append(f'launch {launch}: off the rail {off:.3f}, pitch {max(pitches):.0f} to '
                           f'{min(pitches):.0f}, last nose {low:.2f} over the ground')
        v2cards = int(cards[-1]['v2cards']) if cards else -1
        stood = int(cards[-1]['standdown']) if cards else 0
        if len(fly) < 2 or bad or v2cards != 0 or stood < 10:
            fails.append(f'the V2\'s rocket in flight: {len(fly)} flights; {"; ".join(bad) or "none wrong"}; '
                         f'{v2cards} V2 projectile cards drawn, {stood} stood down')
        else:
            notes.append(f'the V2\'s rocket flies as the model\'s own ({len(fly)} flights), up off the '
                         f'rail, level over the top and down onto the target, no projectile card')
    # (9) the Transport landing troops
    if LST in st:
        ll = read(f'{d}/l.log')
        lp, ld = probes(ll), dumps(ll, 'EVHCLST|')
        lo = dumps(ll, 'EVHCOBJ|')
        door_keys = sorted({o['key'] for s_ in ld for o in s_ if int(o['door']) >= 1})
        if not door_keys or len(lp) != len(ld):
            fails.append(f'the Transport: {len(lp)} probes against {len(ld)} dumps, door opened by '
                         f'{door_keys}')
        else:
            key = door_keys[0]
            lid = str(int(key) % 1000000)
            seq = []                               # per sample: probe, ramp dump, object dump
            for k in range(len(lp)):
                pv = [v for v in vehicles(lp[k]) if v['type'] == LST and v['id'] == lid]
                rv = [o for o in ld[k] if o['key'] == key]
                ov = [o for o in (lo[k] if k < len(lo) else []) if o['key'] == key]
                if pv and rv:
                    seq.append((pv[0], rv[0], ov[0] if ov else None))
            doors = [int(r['door']) for _, r, _ in seq]
            ramps = [[float(a) for a in r['ramps'].split(',')] for _, r, _ in seq]
            far = int(seq[0][1]['far']) if seq else 0
            first = {}
            for k, rr in enumerate(ramps):
                for i, a in enumerate(rr):
                    if a > 0.0 and i not in first:
                        first[i] = k
            far = int(next((r['far'] for _, r, _ in seq if int(r['door']) >= 1), far))
            near = (far + 2) % 4
            sides = [i for i in range(4) if i not in (far, near)]
            order_ok = (len(first) == 4 and all(first[far] < first[i] for i in sides)
                        and all(first[i] < first[near] for i in sides))
            opened = [k for k, dd in enumerate(doors) if dd >= 1]
            shut_at = next((k for k in range(opened[-1] + 1, len(doors)) if doors[k] == -1),
                           None) if opened else None
            # (each ramp down: at its full swing on the water, or resting on the beach short
            # of it; at least one at the full swing)
            flat = any(dd == 5 and min(ramps[k]) >= 120.0 and max(ramps[k]) >= 190.0
                       for k, dd in enumerate(doors))
            closed = shut_at is not None and all(max(ramps[k]) == 0.0
                                                 for k in range(shut_at, len(doors)))
            cards = [p_ for p_, _, _ in seq if p_['visible'] == '1' and p_['model'] != '1']
            undrawn = [k for k, (p_, _, o) in enumerate(seq) if p_['visible'] == '1'
                       and (o is None or o['drawn'] != '1')]
            faces = [int(p_['face']) for p_, _, _ in seq]
            offs = {(int(r['face']) - int(p_['face'])) & 255 for p_, r, _ in seq}
            turned = sorted({f_ for f_ in faces})
            span = 0
            if len(turned) > 1:
                gaps = [(turned[(i + 1) % len(turned)] - turned[i]) & 255 for i in range(len(turned))]
                span = 256 - max(gaps)
            if not seq or not order_ok or not flat or not closed or cards or undrawn \
               or len(offs) != 1 or span < 64:
                fails.append(f'the Transport: {len(seq)} samples, ramps first down at {first} (far '
                             f'{far}, near {near}), down while open {flat}, closed after {closed}, '
                             f'{len(cards)} cards, {len(undrawn)} samples undrawn, heading offsets '
                             f'{sorted(offs)} over a turn of {span}')
            else:
                notes.append(f'the Transport drawn at its heading through a turn of {span}, its far '
                             f'ramp down first and the near one last, all four down while the door '
                             f'stands open and closed after')
    # (12) the Destroyer
    if DD in st:
        check_ship(d, DD, DD_RUNS, 'the Destroyer', (('d', 'temperate'), ('e', 'snow')), fails, notes)
    else:
        notes.append('the Destroyer is not staged, left out')
    # (12b) the Cruiser
    if CA in st:
        check_ship(d, CA, CA_RUNS, 'the Cruiser', (('ca', 'temperate'), ('cs', 'snow')), fails,
                   notes, extra=check_ca_turrets)
    else:
        notes.append('the Cruiser is not staged, left out')
    # (8) Classic
    last = [l for l in t if l.startswith('EVHC|loaded=')]
    if not last or kv(last[-1]).get('on') != '0':
        fails.append(f'Classic: {last[-1:]}')
    if fails:
        print('0 ' + '; '.join(fails))
    else:
        print('1 Red Alert 3D draws its own vehicles and ships: ' + '; '.join(notes))


def script_ship(d, T, runs):
    """The Destroyer's runs: a private copy of the mission with an Allied Destroyer and a
    Soviet Gun Boat at sea, sampled every two ticks (three while it sails)."""
    cell = lambda x, y: y * 128 + x
    for tag, m in runs.items():
        dst = os.path.join(d, 'm' + tag)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
        path = os.path.join(dst, m['scen'] + '.INI')
        lines = open(path, errors='replace').read().splitlines()
        n = before(lines, 'SHIPS', 'Greece', T)
        (x, y, f), (fx, fy, ff) = m['dd'], m['foe']
        lines = add(lines, 'SHIPS', [f'Greece,{T},256,{cell(x, y)},{f},Guard,None',
                                     f'USSR,{SHIP},256,{cell(fx, fy)},{ff},Guard,None'])
        write(path, lines)
        cam = f'cam {m["cam"][0]} {m["cam"][1]}'
        me = f'{T}:Greece {n}'

        def samples(name, k, step, c, keep):
            out = []
            for i in range(k):
                nm = f'{tag}_{name}{i:02d}.png' if i % keep == 0 else f'{tag}_{name}.png'
                out += [f'tick {step}', c, f'shot {d}/{nm}', 'ra3dprobe', 'evhcdump']
            return out
        s = ['tick 2', 'reveal 1', f'dist {DIST}', cam, f'shot {d}/{tag}_on.png', 'ra3dprobe',
             'evhcscreen', 'evhcdump', 'evhc 0', cam, f'shot {d}/{tag}_off.png', 'ra3dprobe',
             'evhc 1',
             # (a close picture for the eye: the launcher astern, the bridge, the mast)
             'dist 800', f'cam {x} {y}', f'shot {d}/{tag}_close.png', f'dist {DIST}']
        if m['go']:
            s += [cam, f'lclickobj {me}', 'select', 'hbdump']
        s += samples('fight', 30, 2, cam, 3)
        if m['go']:
            # (the order's cell must be on the screen: the camera on it first; the sail seen
            # from a little further out, so the start and the end are both in the picture)
            s += [f'cam {m["go"][0]} {m["go"][1]}', f'order {m["go"][0]} {m["go"][1]}',
                  'dist 2000']
            # (and hurt and sunk where it sailed: the same camera, still a little further out)
            cam = f'cam {m["cam_go"][0]} {m["cam_go"][1]}'
            s += samples('sail', 25, 3, cam, 4)
        s += [f'hurtunit {me} 150'] + samples('hurt', 2, 2, cam, 1)
        s += [f'hurtunit {me} 5000'] + samples('dead', 16, 3, cam, 3)
        s += ['quit']
        write(f'{d}/{tag}.txt', s)


def check_ship(d, T, runs, name, tags, fails, notes, extra=None):
    """(12) a ship of its own at sea (the Destroyer, the Cruiser), theater by theater; `extra`
    adds a ship's own claims: extra(lines, did, shots, bad) appends to bad and returns a note."""
    import math
    for tag, scene in tags:
        m = runs[tag]
        lines = read(f'{d}/{tag}.log')
        p = probes(lines)
        sm = dumps(lines, 'EVHCOBJ|')
        if len(p) < 4 or len(sm) != len(p) - 1:
            fails.append(f'{name}, {scene}: {len(p)} probes, {len(sm)} dumps')
            continue
        bad = []
        mine = lambda pr: [v for v in vehicles(pr) if v['type'] == T and v['house'] == 'Greece']
        on, off = mine(p[0]), mine(p[1])
        if not on or on[0]['model'] != '1' or on[0]['visible'] != '1' or not off \
           or off[0]['model'] != '0':
            bad.append(f'drawn by its model {on[:1]}, with evhc 0 {off[:1]}')
            fails.append(f'{name}, {scene}: ' + '; '.join(bad))
            continue
        did = on[0]['id']
        # (the pictures: the model against the card, and not red)
        x, y, ch = pixels(d, f'{tag}_on.png', f'{tag}_off.png')
        npx = -1 if ch is None else int(ch.sum())
        if npx < 1500:
            bad.append(f'the model changes {npx} pixels against the card (1500)')
        else:
            scr = [s_ for s_ in screens(lines, 0) if s_[0] == T]
            red = [r for v in colours(x, ch, scr).values() for r in v]
            if not red or max(red) > 0.08:
                bad.append(f'its red share {red}')
        # (floats)
        if float(on[0]['lift']) != 0.0 or not 0.55 < float(on[0]['height']) < 0.95:
            bad.append(f'not on the sea: lift {on[0]["lift"]}, height {on[0]["height"]}')
        # the samples after the first: the probe, then the dump of what that frame drew
        seq = []
        for k in range(1, len(sm)):
            pv = [v for v in vehicles(p[k + 1]) if v['type'] == T and v['id'] == did]
            ov = [o for o in sm[k] if o['type'] == T and o.get('id') == did]
            foe = [v for v in vehicles(p[k + 1]) if v['type'] == SHIP and v['house'] == 'USSR']
            if pv and ov:
                seq.append((k, pv[0], ov[0], foe[0] if foe else None))
        live = [(k, pv, ov, f) for k, pv, ov, f in seq if int(pv['str'].split('/')[0]) > 0
                and ov['drawn'] == '1']
        # (the launcher: exactly the engine's turret facing, through three angles)
        wrong = [(pv['face'], pv['tface'], ov['tur']) for k, pv, ov, f in live
                 if (int(pv['tface']) - int(pv['face'])) & 255 != int(ov['tur'])]
        angles = {ov['tur'] for k, pv, ov, f in live}
        if wrong or len(angles) < 3:
            bad.append(f'the launcher: {len(wrong)} samples off the engine\'s turret facing '
                       f'{wrong[:3]}, {len(angles)} angles')
        # (it fires, the launcher on the Gun Boat): the samples at each shot's tick
        shots = [int(kv(l)['frame']) for l in lines
                 if l.startswith('UVFX|fire|') and kv(l).get('type') == T
                 and kv(l).get('id') == did]
        frames = []
        for k, pv, ov, f in live:
            fr = [l for l in p[k + 1] if l.startswith('RA3DPROBE|frame|')]
            frames.append((int(kv(fr[0]).get('engineframe', '-1')) if fr else -1, pv, f))
        aims, bear = [], []
        for fr, pv, f in frames:
            if f and int(f['str'].split('/')[0]) > 0:
                (ax, az), (bx, bz) = ([float(c) for c in pv['at'].split(',')],
                                      [float(c) for c in f['at'].split(',')])
                b_ = int(round(math.atan2(bx - ax, -(bz - az)) * 128.0 / math.pi)) & 255
                o_ = (int(pv['tface']) - b_) & 255
                bear.append(min(o_, 256 - o_))
        for t in shots:
            near = [(fr, pv, f) for fr, pv, f in frames if f and 0 <= fr - t <= 3]
            if not near:
                continue
            fr, pv, f = near[0]
            (ax, az), (bx, bz) = ([float(c) for c in pv['at'].split(',')],
                                  [float(c) for c in f['at'].split(',')])
            bearing = int(round(math.atan2(bx - ax, -(bz - az)) * 128.0 / math.pi)) & 255
            off_ = (int(pv['tface']) - bearing) & 255
            aims.append(min(off_, 256 - off_))
        # (a homing missile leaves while the launcher is still turning: the engine fires it
        # with the launcher up to 35 of 256 off its target, vessel.cpp Can_Fire, and the
        # launcher turns 16 a tick, so a shot's next sample may find it up to 48 off)
        if len(shots) < 2 or not aims or max(aims) > 48 or not bear or min(bear) > 2:
            bad.append(f'the shots: {len(shots)} fired, the turret off the Gun Boat by '
                       f'{aims} of 256 at the shots, {min(bear) if bear else None} at best')
        more = extra(lines, did, bad) if extra else ''
        # (hurt and sunk)
        hurt = [pv for k, pv, ov, f in seq if pv['model'] == '1'
                and 0 < int(pv['str'].split('/')[0]) < int(pv['str'].split('/')[1])]
        deaths = [kv(l) for l in lines if l.startswith('EVHC|death|') and kv(l).get('type') == T]
        wrecks = [w for s_ in dumps(lines, 'EVHCWRECK|') for w in s_ if w['type'] == T]
        wet = [w for w in wrecks if w['wet'] == '1']
        sank = [w for w in wet if w['sinking'] == '1']
        if not hurt or not deaths or not wet or not sank:
            bad.append(f'hurt as its model {len(hurt)}, {len(deaths)} deaths, wreck samples '
                       f'{len(wrecks)}, on water {len(wet)}, sinking {len(sank)}')
        # (the click, the sail and the roll)
        if m['go']:
            if not any(l.startswith('SELECT|') and f'type={T}' in l and 'house=Greece' in l
                       and l.endswith('selected') for l in lines):
                bad.append('a click on its model did not select it')
            # (selected, no bar: the 1996 sprite's width stood a bar two cells tall)
            hb = [l for l in lines if l.startswith('HBDUMP-BEGIN')]
            tall = [l for l in lines if l.startswith(f'HB|{T}|')]
            if not hb or tall:
                bad.append(f'selected, its health bar: {len(hb)} dumps, {tall[:1]}')
            at = [pv['at'] for k, pv, ov, f in live]
            wob = [tuple(float(c) for c in ov.get('wob', '-9,-9').split(',')) for k, pv, ov, f in live]
            # (standing: the samples before it first moves; sailing: from then on)
            first = next((i for i in range(1, len(at)) if at[i] != at[0]), None)
            if first is None:
                bad.append(f'the ordered {name} did not move from {at[:1]}')
            else:
                stand, sail = set(wob[:first]), sorted({w[1] for w in wob[first:]})
                spread = (sail[-1] - sail[0]) if sail else 0.0
                if len(stand) != 1 or len(sail) < 3 or spread < 0.01 or min(w[0] for w in wob) < -1.0:
                    bad.append(f'its roll: {sorted(stand)} standing, {len(sail)} values sailing '
                               f'over {spread:.4f}')
                else:
                    notes.append(f'{name} sails from {at[0]} to {at[-1]}, its roll '
                                 f'taking {len(sail)} values over {spread:.3f} radians')
        if bad:
            fails.append(f'{name}, {scene}: ' + '; '.join(bad))
        else:
            notes.append(f'{name}, {scene}: its model at sea, its turret on the '
                         f'engine\'s turret facing through {len(angles)} angles and onto the Gun '
                         f'Boat (within {min(bear)} of 256), {len(shots)} shots fired{more}, hurt, '
                         f'sunk and its wreck going down')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit(__doc__)
