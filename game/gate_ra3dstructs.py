#!/usr/bin/env python3
"""G348 and G349: Red Alert 3D's own structures drawn by their remastered stages, placements and
driven clips under Enhanced (game/ebld_mod.h: ebld_ra_staged, ebld_ra_entry, RA_DRIVE; game/
ebld_place.h; the files by tools/ra3d/bake-structures.py and tools/ra3d/bake-placements.py,
staged by tools/ra3d/stage-structures.py).

  gate_ra3dstructs.py script DIR    write the missions and the run scripts into DIR
  gate_ra3dstructs.py check DIR     read the runs' logs: two lines, "G348 1|0 message" (the
                                    placements and the Missile Silo's cover) and "G349 1|0|- message"
                                    (the stages; - when the folder has no structure staged with them)

Run from a play folder with the Red Alert module staged. Each run is a private copy of a
mission, the first four with a block of the player's own structures added where nothing comes
to fight them:

  st  Soviet 5 (temperate, USSR): the block
  ss  Soviet 4, second version (snow, USSR): the block
  at  Allied 9 (temperate, Greece): the block
  as  Allied 2 (snow, Greece): the block, with a Missile Silo whose nuclear missile is fired
  sb  Soviet 2 (temperate, USSR): a Barracks built from the sidebar and sold, nothing added
  wd  Soviet 8 (temperate, USSR): a War Factory added, a Heavy Tank built and rolling out of it
  so  Allied 2 (snow, Greece) and su Soviet 5 (temperate, USSR): a refinery and two Ore Silos,
      ore put into the house's store at empty, half and full, intact and below half strength

The blocks hold every type the module has staged with its stages (read from ra3d/ebld/
manifest.json) or has a placement for, dealt round the four runs, and an intact-only type
beside them in each. Every structure of the block is hit until it
is below half strength and then until it dies.

CLAIMS, G348 (the art in every build):
  (1) every placement the module has (ra3d/eplc, the yard's apart) plays on the block's own
      standing structure of its type to its end and is drawn (eplcplay, as the lab's Place
      button); one of them sold plays it backwards (EPLC|start sell, or the playing entry's
      end why=sold) with its model or placement drawing it on every dump, never its 1996
      card; on Soviet 2 a Barracks built from the sidebar goes up by its placement (EPLC|start
      place to end done) with no build-up card, and sold plays it backwards;
  (2) the Missile Silo's cover is still before the launch (its driven clip at rest on every
      tick: it no longer loops), opens while the engine opens it (ACTIVE, rising every tick to
      fully open), is held open through the launch (AUX1) and closes on its own closing frames
      while the engine closes it (AUX2), ending at rest.
  (6) the Ore Silos' fill is the house's stored ore against its whole capacity (the refinery's
      included) in the 1996 five levels, the same in both silos, at empty, half and full, and
      the picture changes with it, intact and below half strength;
  (7) the War Factory's door opens (stages 1 to 3) and shuts as the tank rolls out; with its door
      clip the factory is its model the whole time and the clip shows the engine's stage, without
      it the 1996 card stands while the door is open;
CLAIMS, G349 (only where the module carries structures with their stages):
  (3) each staged structure is drawn by its model at every strength (RASTRUCT draw=model while
      alive), is chipped by the hits on the side they come from (EBLD|hit lines, a zone and a
      reason each), goes to its damaged model below half strength (EBLD|damaged), and plays its
      own emitters there (its damaged emitters spawned); a staged type with intact emitters (the
      Power Plant's stack smoke, the Flame Tower's pilot flames) plays them standing;
  (4) at its death it breaks into its destroyed stage (EBLD|wreck with pieces), its own explosion
      goes off (EBLDBOOM), the engine's death fireballs on it stand down (EBLD|standdown why=death)
      and the wreck sinks and goes;
  (5) the intact-only structure beside them keeps the old way: no entry of its stages, no wreck;
  (8) THE SEA, where the Naval Yard or the Sub Pen is staged with its stages (run `sw`, Allied
      6, snow: a Naval Yard added in the water for the player, the mission's own Soviet Sub
      Pen): each is destroyed; pieces that come down on the water go in (EBLDSEA|in, each with
      a splash) and every one of the wreck's that went in sinks out of sight (EBLDSEA|gone, the
      same piece, later) before the wreck is cleared, while pieces that came down on the deck or
      the slab do not go in.
PROVEN TO FAIL: main's renderer before this work refuses a staged file (ebld_ra_ready drew the
1996 sprite for it), plays no Red Alert placement but the yard's, and loops the Missile Silo's
cover for ever (its clip on the idle clock).
NOT COVERED: how it looks (the proof sheets); the Construction Yard's crane, the Tesla Coil's charge, the Iron Curtain, the Chronosphere and the rest of RA_DRIVE until
their models carry a clip; Classic, which keeps the 1996 states.
"""
import json
import os
import re
import shutil
import sys

PLAY = os.getcwd()
MAXSTR = {'POWR': 400, 'APWR': 700, 'WEAP': 1000, 'PROC': 900, 'AFLD': 1000, 'SAM': 400,
          'FTUR': 400, 'AGUN': 400, 'FIX': 800, 'PBOX': 400, 'MSLO': 400, 'DOME': 1000,
          'TENT': 800, 'BARR': 800, 'SILO': 300, 'ATEK': 400, 'GAP': 1000, 'HBOX': 600,
          'TSLA': 400, 'KENN': 400, 'FCOM': 400, 'STEK': 600, 'IRON': 400, 'PDOX': 400,
          'FACT': 1000, 'SPEN': 1000, 'SYRD': 1000, 'GUN': 400, 'HPAD': 800}
# footprints (bdata.cpp)
SIZE = {'POWR': (2, 2), 'APWR': (3, 3), 'WEAP': (3, 2), 'PROC': (3, 3), 'AFLD': (3, 2),
        'SAM': (2, 1), 'FTUR': (1, 1), 'AGUN': (1, 2), 'FIX': (3, 3), 'PBOX': (1, 1),
        'MSLO': (2, 1), 'DOME': (2, 2), 'TENT': (2, 2), 'BARR': (2, 2), 'SILO': (1, 1),
        'ATEK': (2, 2), 'GAP': (1, 2), 'HBOX': (1, 1), 'TSLA': (1, 2), 'KENN': (1, 1),
        'FCOM': (2, 2), 'STEK': (3, 3), 'IRON': (2, 2), 'PDOX': (2, 2), 'FACT': (3, 3), 'GUN': (1, 1),
        'HPAD': (2, 2)}
# tag: scenario, house, the block's corner (x, y), the camera, the Barracks' build (camera and
# the cells tried), whether the silo's missile is fired (and at which cell)
RUNS = {
    'st': ('SCU05EA', 'USSR', (30, 70), (34, 72), None, None),
    'ss': ('SCU04EB', 'USSR', (30, 86), (34, 88), None, None),
    'at': ('SCG09EA', 'Greece', (34, 84), (39, 86), None, None),
    'as': ('SCG02EA', 'Greece', (80, 56), (85, 58), None, (90, 50)),
    'sb': ('SCU02EA', 'USSR', None, (74, 56), ((74, 56), [(73, 57), (72, 57), (71, 57)]), None),
    'so': ('SCG02EA', 'Greece', None, (81, 56), None, None),
    'su': ('SCU05EA', 'USSR', None, (31, 70), None, None),
    'wd': ('SCU08EA', 'USSR', None, (96, 101), None, None),
}
# THE WAR FACTORY'S DOOR: a factory added where nothing comes to fight it on Soviet 8, a Heavy
# Tank built from the sidebar and sampled every tick as it rolls out
DOOR_RUN = {'wd': (95, 100)}
# THE ORE SILO'S RUNS: a refinery and two silos added for the player, then ore put into the
# house's store (storeore, the brain's OpenCNC3D_Store_Ore) at these amounts of its 5,000
# capacity, intact and then below half strength
SILO_RUNS = {'so': (80, 56), 'su': (30, 70)}
SILO_STEPS = (0, 2600, 2400, -5000)       # empty, half, full, empty again
NAMES = {'wd': 'Soviet 8, temperate (War Factory)', 'so': 'Allied 2, snow (silos)', 'su': 'Soviet 5, temperate (silos)', 'st': 'Soviet 5, temperate', 'sb': 'Soviet 2, temperate', 'ss': 'Soviet 4, snow', 'at': 'Allied 9, temperate',
         'as': 'Allied 2, snow'}
CONTROLS = ('KENN', 'FCOM', 'GAP', 'STEK', 'DOME', 'SILO', 'TENT', 'BARR', 'MSLO')
SILO_T = 'MSLO'           # the Missile Silo, whose cover the launch drives
WIDTH = 13                # cells a row of the block may take


def staged_types():
    """The types the module has staged with their stages, by its ebld manifest."""
    try:
        m = json.load(open(os.path.join(PLAY, 'ra3d/ebld/manifest.json')))['models']
    except (OSError, ValueError, KeyError):
        return []
    return sorted(k for k, v in m.items() if set(v.get('sets', {})) >= {'INTACT', 'CUT', 'DESTROYED'}
                  and k in SIZE)


def block(types, x0, y0):
    """The structures in rows from (x0, y0), one cell apart: [(type, x, y, hit cell)]."""
    out, x, y, h = [], x0, y0, 1
    for t in types:
        w, hh = SIZE[t]
        if x + w > x0 + WIDTH:
            x, y, h = x0, y + h + 1, 1
        out.append((t, x, y, (x + w // 2, y + hh // 2)))
        x += w + 1
        h = max(h, hh)
    return out


def cell(x, y):
    return y * 128 + x


def write(path, lines):
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def mission(d, tag, rows):
    scen, house = RUNS[tag][0], RUNS[tag][1]
    dst = os.path.join(d, 'm' + tag)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().splitlines()
    st = next(i for i, l in enumerate(L) if l.strip() == '[STRUCTURES]')
    en = st + 1
    while en < len(L) and not L[en].startswith('['):
        en += 1
    nums = [int(m.group(1)) for l in L[st + 1:en] for m in [re.match(r'(\d+)=', l)] if m]
    while en > st + 1 and not L[en - 1].strip():
        en -= 1
    n0 = max(nums) + 1 if nums else 0
    new = [f'{n0 + i}={house},{t},256,{cell(x, y)},0,None,1,0' for i, (t, x, y, _) in enumerate(rows)]
    write(path, L[:en] + new + L[en:])


def placed_types():
    """The types the module has a placement for (ra3d/eplc), the Construction Yard's apart (it
    deploys out of its MCV: G346)."""
    d = os.path.join(PLAY, 'ra3d/eplc')
    return sorted(f[:-5] for f in os.listdir(d) if f.endswith('.eplc') and f[:-5] in SIZE
                  and f[:-5] != 'FACT') \
        if os.path.isdir(d) else []


def own_count(L, house, t):
    """How many of the house's structures of a type the mission itself has (the nth of an
    added one is counted after them)."""
    return sum(1 for l in L if re.match(r'\d+=%s,%s,' % (house, t), l))


SEA_TYPES = {'SYRD': (57, 100, (58, 101)), 'SPEN': (64, 98, (65, 99))}
SEA_RUN = ('SCG06EA', 'Greece', (61, 100))


def sea_staged():
    """The sea structures the module has staged with their stages."""
    try:
        m = json.load(open(os.path.join(PLAY, 'ra3d/ebld/manifest.json')))['models']
    except (OSError, ValueError, KeyError):
        return []
    return sorted(k for k, v in m.items() if k in SEA_TYPES
                  and set(v.get('sets', {})) >= {'INTACT', 'CUT', 'DESTROYED'})


def sea_script(d):
    """(8): the run `sw`, written only where a sea structure is staged with its stages."""
    types = sea_staged()
    if not types:
        return
    scen, house, (cx, cy) = SEA_RUN
    dst = os.path.join(d, 'msw')
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(PLAY, 'ra3d/missions'), dst)
    path = os.path.join(dst, scen + '.INI')
    L = open(path, errors='replace').read().splitlines()
    if 'SYRD' in types:
        st = next(i for i, l in enumerate(L) if l.strip() == '[STRUCTURES]')
        x, y, _ = SEA_TYPES['SYRD']
        L.insert(st + 1, f'90={house},SYRD,256,{cell(x, y)},0,None,0,0')
    write(path, L)
    v = ['tick 2', 'reveal 1', 'tick 40', f'cam {cx} {cy}', 'dist 1400', 'ebldump']
    for k in range(6):
        v += [f'hurtcell {SEA_TYPES[t][2][0]} {SEA_TYPES[t][2][1]}' for t in types] + ['tick 2']
    v += ['tick 20']
    for k in range(12):
        v += [f'hurtcell {SEA_TYPES[t][2][0]} {SEA_TYPES[t][2][1]}' for t in types] + ['tick 1']
    v += ['tick 30', f'shot {d}/sw_sea.png', 'ebldump', 'tick 200', 'ebldump']
    write(f'{d}/sw.txt', v + ['quit'])


def check_sea(d):
    """(8): pieces on the water go in, splash and sink out of sight; those on the deck stay."""
    types = sea_staged()
    if not types:
        return None, None
    text = read(f'{d}/sw.log')
    if not text:
        return 'the sea run (sw) left no log', None
    notes = []
    for t in types:
        x, y, _ = SEA_TYPES[t]
        ids = re.findall(r'RASTRUCT\|%s\|id=(\d+)\|cell=%d,%d\|' % (t, x, y), text)
        if not ids:
            return f'the sea run: no {t} at {x},{y}', None
        key = 2000000 + int(ids[0])
        w = re.search(r'EBLD\|wreck\|key=%d\|frame=(\d+)\|pieces=(\d+)' % key, text)
        if not w:
            return f'the sea run: the {t} did not break into its destroyed stage', None
        wf, pieces = int(w.group(1)), int(w.group(2))
        ins = [kv(l) for l in text.splitlines() if l.startswith(f'EBLDSEA|in|{t}|key={key}|')]
        gone = [kv(l) for l in text.splitlines() if l.startswith(f'EBLDSEA|gone|{t}|key={key}|')]
        after = [q for q in ins if int(q['frame']) >= wf]
        if not after:
            return f'the sea run: no piece of the {t}\'s wreck went into the water', None
        left = [q for q in after if not any(g['piece'] == q['piece'] and int(g['frame']) > int(q['frame'])
                                            for g in gone)]
        if left:
            return (f'the sea run: {len(left)} of the {len(after)} pieces of the {t} that went into '
                    'the water did not sink out of sight'), None
        if len(after) >= pieces:
            return f'the sea run: every piece of the {t} went into the water (none lay on its deck)', None
        notes.append(f'{t}: {len(after)} of its {pieces} wreck pieces went into the water with a splash '
                     f'and sank out of sight, the rest lay on its deck ({len(ins) - len(after)} chips '
                     'before it broke went in too)')
    return None, 'the sea, Allied 6: ' + '; '.join(notes)


def script(d):
    os.makedirs(d, exist_ok=True)
    sea_script(d)
    staged = staged_types()
    placed = placed_types()
    every = sorted(set(staged) | set(placed))
    blocks = [t for t, r in RUNS.items() if r[2]]
    for tag, (scen, house, origin, (cx, cy), build, nuke) in RUNS.items():
        if tag in DOOR_RUN:
            bx, by = DOOR_RUN[tag]
            rows = block(['WEAP'], bx, by)
            mission(d, tag, rows)
            json.dump(dict(rows=[], weap=rows, placed=placed, staged=staged, sold=None, control=None),
                      open(f'{d}/{tag}_block.json', 'w'))
            v = ['tick 2', 'reveal 1', 'tick 300', 'sbclickitem 3TNK', f'cam {cx} {cy}', 'dist 900']
            for k in range(800):
                v += ['tick 1', 'ebldump']
                if k % 8 == 0 and k > 600:
                    v += [f'cam {cx} {cy}', f'shot {d}/{tag}_door.png']
            write(f'{d}/{tag}.txt', v + ['quit'])
            continue
        if tag in SILO_RUNS:
            bx, by = SILO_RUNS[tag]
            rows = block(['PROC', 'SILO', 'SILO'], bx, by)
            mission(d, tag, rows)
            json.dump(dict(rows=[], silos=rows, placed=placed, staged=staged, sold=None, control=None),
                      open(f'{d}/{tag}_block.json', 'w'))
            sx, sy = rows[1][3]
            v = ['tick 2', 'reveal 1', 'tick 200']
            for phase in ('intact', 'damaged'):
                if phase == 'damaged':
                    # (each hurt is 100 of a silo's 300: two leave it at a third)
                    for _, _, _, (hx, hy) in rows[1:]:
                        v += [f'hurtcell {hx} {hy}'] * 2
                    v += ['tick 2']
                for k, amount in enumerate(SILO_STEPS):
                    if amount:
                        v.append(f'storeore {house} {amount}')
                    v += ['tick 2', f'cam {sx} {sy}', 'dist 700', f'shot {d}/{tag}_{phase}{k}.png',
                          'silodump', 'ebldump']
            write(f'{d}/{tag}.txt', v + ['quit'])
            continue
        if not origin:
            # (the sidebar's build alone, in a mission of its own)
            json.dump(dict(rows=[], placed=placed, staged=staged, sold=None, control=None),
                      open(f'{d}/{tag}_block.json', 'w'))
            mission(d, tag, [])
            v = ['tick 2', 'reveal 1', 'tick 10']
            (ex, ey), tries = build
            v += [f'cam {ex} {ey}', 'sbclickitem BARR', 'ready BARR 3000', 'startplace BARR']
            for hx, hy in tries:
                v += [f'hover {hx} {hy}', f'lclickcell {hx} {hy}', 'tick 1']
            v += ['tick 1', 'frames 1', 'ra3dprobe'] * 60
            v += ['ebldump', 'sellmode 1', f'lclickcell {tries[0][0]} {tries[0][1]}', 'sellmode 0']
            v += ['tick 1', 'frames 1', 'ra3dprobe', 'ebldump'] * 60
            write(f'{d}/{tag}.txt', v + ['quit'])
            continue
        i = blocks.index(tag)
        bx, by = origin
        # (the maps are small: the types are dealt round the four runs, so each is proven on
        # one of them, and every run has the intact-only one)
        # (an intact-only type beside them, while one is left)
        control = next((t for t in CONTROLS if t not in staged), None)
        types = every[i::len(blocks)] + ([control] if control else [])
        if nuke and SILO_T not in types:
            types.append(SILO_T)
        rows = block(types, bx, by)
        src = open(os.path.join(PLAY, 'ra3d/missions', scen + '.INI'), errors='replace').read().splitlines()
        mission(d, tag, rows)
        # the structure sold: the last of the block with a placement and no stages, else the last
        # with a placement (left out of the stage claims)
        # (of those, the southernmost: a taller structure in a row south of it would stand
        # over it on the screen and take the click)
        cand = [r for r in rows if r[0] in placed and r[0] not in staged] \
            or [r for r in rows if r[0] in placed]
        sell = max(reversed(cand), key=lambda r: r[2] + SIZE[r[0]][1]) if cand else None
        json.dump(dict(rows=rows, placed=placed, staged=staged, sold=sell, control=control),
                  open(f'{d}/{tag}_block.json', 'w'))
        v = ['tick 2', 'reveal 1', 'tick 40', f'cam {cx} {cy}', 'dist 2400', f'shot {d}/{tag}_intact.png',
             'ebldump']
        # (1) every placement played on the block's own standing structure, then one sold
        seen = {}
        for t, x, y, _ in rows:
            if t in placed:
                v.append(f'eplcplay {t}:{house} {own_count(src, house, t) + seen.get(t, 0)}')
                seen[t] = seen.get(t, 0) + 1
        v += ['tick 1', 'frames 1'] * 70 + ['eplcdump', 'ebldump']
        if sell:
            v += ['sellmode 1', f'lclickcell {sell[3][0]} {sell[3][1]}', 'sellmode 0']
            v += ['tick 1', 'frames 1', 'ebldump'] * 50
        # (2) the silo's missile: its cover still for 30 ticks, then the launch, every tick sampled
        if nuke:
            v += ['superrecharge', 'tick 2']
            v += ['tick 1', 'ebldump'] * 30
            v += ['sbclickitem SW_Nuke', 'tick 1', f'actcell {nuke[0]} {nuke[1]}']
            v += ['tick 1', 'ebldump'] * 70
        # (3) below half
        hurt = [r for r in rows if r is not sell]
        for k in range(12):
            for t, x, y, (hx, hy) in hurt:
                if k < MAXSTR[t] // 200 + 1:
                    v.append(f'hurtcell {hx} {hy}')
            v.append('tick 1')
        v += ['tick 40', f'shot {d}/{tag}_damaged.png', 'ebldump']
        # (4) dead
        for k in range(10):
            for t, x, y, (hx, hy) in hurt:
                v.append(f'hurtcell {hx} {hy}')
            v.append('tick 1')
        v += ['tick 3', f'shot {d}/{tag}_blast.png', 'tick 25', f'shot {d}/{tag}_wreck.png', 'ebldump',
              'tick 220', 'ebldump', 'quit']
        write(f'{d}/{tag}.txt', v)


def read(path):
    try:
        return open(path, errors='replace').read()
    except OSError:
        return ''


def kv(s):
    return dict(p.split('=', 1) for p in s.split('|') if '=' in p)


def check_place(name, text):
    """(1): the Barracks' placement and sale. Returns a failure or a note."""
    starts = [kv(m.group(0)) for m in re.finditer(r'EPLC\|start\|[^\s]*', text)]
    ends = [kv(m.group(0)) for m in re.finditer(r'EPLC\|end\|[^\s]*', text)]
    pl = next((s for s in starts if s.get('type') == 'BARR' and s.get('mode') == 'place'), None)
    if not pl:
        return f'{name}: no Barracks placement started', None
    en = next((e for e in ends if e.get('key') == pl['key'] and e.get('why') == 'done'), None)
    if not en:
        return f'{name}: the Barracks placement did not play to its end', None
    probe = [kv(l) for l in text.splitlines() if l.startswith('RA3DPROBE|place|')]
    if not any(int(p['models']) > 0 for p in probe) or sum(int(p['cards']) for p in probe):
        return f'{name}: the placement drew no model or a build-up card', None
    # (sold while its entry stands, the entry turns to the sale: its end says so)
    sell = next((s for s in starts if s.get('key') == pl['key'] and s.get('mode') == 'sell'), None) \
        or next((e for e in ends if e.get('key') == pl['key'] and e.get('why') == 'sold'), None)
    if not sell:
        return f'{name}: selling the Barracks played no placement', None
    gone = text.find('SELLMODE|1')
    if not re.search(r'RA3DPROBE\|place\|models=[1-9]', text[gone:]):
        return f'{name}: the sale drew no placement', None
    # every dump after the sale: the Barracks drawn by its model (or gone), never its card
    key_id = int(pl['key']) % 1000000
    after = text[text.find('SELLMODE|1'):]
    rows = re.findall(r'RASTRUCT\|BARR\|id=%d\|[^\n]*' % key_id, after)
    cards = [r for r in rows if r.endswith('draw=original')]
    if cards:
        return f'{name}: the Barracks drawn by its 1996 card {len(cards)} times while sold', None
    return None, (f'{name}: a Barracks from the sidebar goes up by its placement (frame {pl["frame"]} '
                  f'to {en["frame"]}) and sold plays it backwards (from frame {sell["frame"]}), no card')


def check_replays(name, text, rows, placed, sold):
    """(1) on the block: each placement played on its standing structure to its end, drawn, and
    the sold one played backwards with its model or placement on every dump, never its card."""
    starts = [kv(m.group(0)) for m in re.finditer(r'EPLC\|start\|[^\s]*', text)]
    ends = [kv(m.group(0)) for m in re.finditer(r'EPLC\|end\|[^\s]*', text)]
    want = sorted({r[0] for r in rows if r[0] in placed})
    if not want:
        return None, None
    drawn = {m.group(1): int(m.group(2)) for m in re.finditer(r'EPLCMODEL\|(\w+)\|[^\n]*\|drawn=(\d+)', text)}
    for t in want:
        st = [x for x in starts if x.get('type') == t and x.get('mode') == 'replay']
        if not st:
            return f'{name}: the {t}\'s placement did not play', None
        if not any(e.get('key') == st[0]['key'] and e.get('why') == 'done' for e in ends):
            return f'{name}: the {t}\'s placement did not play to its end', None
        if drawn.get(t, 0) <= 0:
            return f'{name}: the {t}\'s placement drew nothing', None
    note = f'{name}: {len(want)} placements played ({", ".join(want)})'
    if sold:
        t, x, y = sold[0], sold[1], sold[2]
        ids = re.findall(r'RASTRUCT\|%s\|id=(\d+)\|cell=%d,%d\|' % (t, x, y), text)
        if not ids:
            return f'{name}: the {t} to sell is not in the building list', None
        key = str(2000000 + int(ids[0]))
        sale = [x for x in starts if x.get('key') == key and x.get('mode') == 'sell'] + \
               [e for e in ends if e.get('key') == key and e.get('why') == 'sold']
        if not sale:
            return f'{name}: selling the {t} played no placement', None
        after = text[text.find('SELLMODE|1'):]
        rows_ = re.findall(r'RASTRUCT\|%s\|id=%s\|cell=%d,%d\|[^\n]*' % (t, ids[0], x, y), after)
        if any(r.endswith('draw=original') for r in rows_):
            return f'{name}: the sold {t} drawn by its 1996 card', None
        note += f', the {t} sold backwards ({len(rows_)} dumps by its model)'
    return None, note


def check_silo(name, text):
    """(2): the silo's cover, tick by tick."""
    drv = [kv(l) for l in text.splitlines() if l.startswith('EBLDDRIVE|MSLO|')]
    if len(drv) < 60:
        return f'{name}: {len(drv)} samples of the silo\'s driven clip', None
    seg = [int(x) for x in drv[0]['seg'].split(',')]
    fire = text.find('SUPER|fire|SW_Nuke')
    if fire < 0:
        return f'{name}: the nuclear missile was not fired', None
    before = [kv(l) for l in text[:fire].splitlines() if l.startswith('EBLDDRIVE|MSLO|')]
    if len(before) < 25 or any(float(b['out']) != 0.0 or float(b['frame']) != seg[0] for b in before):
        return f'{name}: the cover moved before the launch (it still loops)', None
    after = [kv(l) for l in text[fire:].splitlines() if l.startswith('EBLDDRIVE|MSLO|')]
    act = [a for a in after if a['doing'] == '2']
    held = [a for a in after if a['doing'] == '4']
    close = [a for a in after if a['doing'] == '5']
    if drv[0].get('sprite') == '1':
        # (a clip of the 1996 pictures: the very frames the 1996 silo draws, 0 to 3 opening
        # (frame 4 is the first of the hold), 4 held, 5 to 7 closing, 0 at rest)
        fa = [int(float(a['frame'])) for a in act]
        fh = {int(float(a['frame'])) for a in held}
        fc = [int(float(a['frame'])) for a in close]
        last = after[-1]
        if not fa or fa != sorted(fa) or fa[-1] < 3 or fh != {4} or not fc \
           or not set(fc) <= {5, 6, 7} or float(last['frame']) != 0.0:
            return f'{name}: the cover\'s 1996 frames: opening {fa}, held {sorted(fh)}, closing {fc}', None
        return None, (f'{name}: the Missile Silo\'s cover still before the launch ({len(before)} ticks), '
                      f'opens on the 1996 frames {sorted(set(fa))}, held on 4 for {len(held)} ticks, closes '
                      f'on {sorted(set(fc))}, at rest after')
    outs = [float(a['out']) for a in act]
    if not act or any(b < a for a, b in zip(outs, outs[1:])) or outs[-1] <= outs[0]:
        return f'{name}: the cover did not open while the engine opened it ({outs[:12]})', None
    if not held or float(held[-1]['out']) != 1.0:
        return f'{name}: the cover not held open through the launch', None
    if not close or not all(int(a['closing']) == 1 or float(a['out']) == 0.0 for a in close) or \
       not any(seg[3] >= 0 and seg[2] <= float(a['frame']) <= seg[3] and int(a['closing']) for a in close):
        return f'{name}: the cover did not close on its closing frames', None
    last = after[-1]
    if float(last['out']) != 0.0 or float(last['frame']) != seg[0]:
        return f'{name}: the cover not back at rest at the end ({last})', None
    return None, (f'{name}: the Missile Silo\'s cover still before the launch ({len(before)} ticks), opens '
                  f'in {len(act)} ticks, held open {len(held)}, closes in {len(close)} on frames '
                  f'{seg[2]} to {seg[3]}, at rest after')


def check_stages(name, text, rows, staged, control):
    """(3) to (5)."""
    if not staged or not rows:
        return None, None
    lines = text.splitlines()
    ids = {}
    for l in lines:
        m = re.match(r'RASTRUCT\|(\w+)\|id=(\d+)\|cell=(\d+),(\d+)\|', l)
        if m:
            ids[(m.group(1), int(m.group(3)), int(m.group(4)))] = int(m.group(2))
    notes = []
    for t, x, y, _ in rows:
        oid = ids.get((t, x, y))
        if oid is None:
            return f'{name}: the {t} added at {x},{y} is not in the building list', None
        if t != control and t not in staged:
            continue
        key = 2000000 + oid
        mine = [l for l in lines if f'key={key}|' in l]
        alive = re.findall(r'RASTRUCT\|%s\|id=%d\|[^\n]*\|str=([1-9]\d*)/[^\n]*draw=(\w+)' % (t, oid), text)
        if t == control:
            if any(l.startswith('EBLD|wreck|') for l in mine):
                return f'{name}: the intact-only {t} broke into a wreck', None
            continue
        if not alive or any(d != 'model' for _, d in alive):
            return f'{name}: the {t} not drawn by its model at every strength', None
        hits = [kv(l) for l in mine if l.startswith('EBLD|hit|')]
        if not hits or any(h.get('how') in (None, '') for h in hits):
            return f'{name}: the {t} took no hits', None
        if not any(l.startswith('EBLD|damaged|') for l in mine):
            return f'{name}: the {t} never went to its damaged model', None
        emit = [kv(l) for l in mine if l.startswith('EBLDEMIT|')]
        if not any(e['state'] == '1' and int(e['spawned']) > 0 for e in emit) and \
           any(e['state'] == '1' for e in emit):
            return f'{name}: the {t}\'s damaged emitters spawned nothing', None
        if any(e['state'] == '0' for e in emit) and not any(e['state'] == '0' and int(e['spawned']) > 0 for e in emit):
            return f'{name}: the {t}\'s intact emitters spawned nothing', None
        wreck = [kv(l) for l in mine if l.startswith('EBLD|wreck|')]
        if not wreck or int(wreck[0].get('pieces', 0)) <= 0:
            return f'{name}: the {t} did not break into its destroyed stage', None
        if not any(l.startswith('EBLDBOOM|') for l in mine):
            return f'{name}: the {t}\'s own explosion did not go off', None
        if not any(l.startswith('EBLD|standdown|') and 'why=death' in l for l in mine):
            return f'{name}: the engine\'s death fireballs on the {t} did not stand down', None
        if not any(l.startswith('EBLD|sink|') for l in mine):
            return f'{name}: the {t}\'s wreck did not sink', None
        notes.append(f'{t} ({len(hits)} hits, {wreck[0]["pieces"]} pieces)')
    return None, (f'{name}: ' + ', '.join(notes)) if notes else None


def check_silo_fill(name, text, d, tag, house):
    """(6) the silos' fill: every silo of the house drawn at the 1996 level of the house's stored
    ore against its capacity (BuildingClass::Shape_Number: five levels), intact and below half
    strength, and the picture changing with it."""
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return f'{name}: no PIL to compare the pictures', None
    parts = text.split('HOUSESTORE|' + house + '|')[1:]
    if len(parts) != 2 * len(SILO_STEPS):
        return f'{name}: {len(parts)} silo samples, {2 * len(SILO_STEPS)} wanted', None
    seen = []
    for k, part in enumerate(parts):
        m = re.match(r'tiberium=(-?\d+)\|capacity=(-?\d+)', part)
        t, c = int(m.group(1)), int(m.group(2))
        want = (min(4, max(0, t * 5 // c)) / 4.0) if c > 0 else 0.0
        fills = sorted({float(f) for f in re.findall(r'EBLDPOSE\|SILO\|[^\n]*kind=fill\|level=([\d.]+)', part)})
        phase = 'intact' if k < len(SILO_STEPS) else 'damaged'
        if phase == 'damaged':
            hurt = [int(x) for x in re.findall(r'RASTRUCT\|SILO\|id=\d+\|cell=\d+,\d+\|str=(\d+)/300', part)]
            if len(hurt) < 2 or any(not 0 < h < 150 for h in hurt[-2:]):
                return f'{name}: the silos are not below half strength and standing ({hurt[-2:]})', None
        if c <= 0 or (phase == 'intact' and fills != [want]):
            return f'{name}: {phase} silos drawn at {fills} for {t} of {c} stored (the 1996 level {want})', None
        seen.append((phase, t, c, want))
    def px(phase, k):
        return np.asarray(Image.open(f'{d}/{tag}_{phase}{k}.png').convert('RGB').crop((320, 160, 720, 460))).astype(int)
    for phase in ('intact', 'damaged'):
        empty, half, full = px(phase, 0), px(phase, 1), px(phase, 2)
        a = int((np.abs(half - empty).max(axis=2) > 12).sum())
        b = int((np.abs(full - half).max(axis=2) > 12).sum())
        if a < 200 or b < 200:
            return f'{name}: {phase}, the silo\'s picture barely changes with its ore ({a}, {b} pixels)', None
    levels = sorted({w for _, _, _, w in seen})
    return None, f'{name}: both silos at the 1996 level of the stored ore ({", ".join("%g" % l for l in levels)}), the pictures changing with it, intact and damaged'


def check_door(name, text, d, tag):
    """(7) the War Factory's door: the engine opens it (door 1 to 3) and shuts it as the tank rolls
    out; a factory whose model carries its door clip is drawn by the model the whole time and
    its clip shows the very stage (frame = stage); one without stays its 1996 card while open."""
    try:
        B = json.load(open(f'{d}/{tag}_block.json'))
        x, y = B['weap'][0][1], B['weap'][0][2]
    except (OSError, ValueError, KeyError, IndexError):
        return f'{name}: no factory', None
    ids = re.findall(r'RASTRUCT\|WEAP\|id=(\d+)\|cell=%d,%d\|' % (x, y), text)
    if not ids:
        return f'{name}: the factory added is not in the building list', None
    rows = re.findall(r'RASTRUCT\|WEAP\|id=%s\|cell=%d,%d\|[^\n]*\|door=(\d+)\|[^\n]*draw=(\w+)' % (ids[0], x, y), text)
    doors = [int(a) for a, _ in rows]
    if max(doors or [0]) < 3 or doors[-1] != 0:
        return f'{name}: the door did not open to 3 and shut again ({sorted(set(doors))})', None
    key = 2000000 + int(ids[0])
    drive = [kv(l) for l in text.splitlines() if l.startswith('EBLDDRIVE|WEAP|key=%d|' % key)]
    if drive:
        bad = [r for r in rows if int(r[0]) > 0 and r[1] != 'model']
        if bad:
            return f'{name}: the factory with its door clip drawn by its card while open', None
        if any(int(float(q['frame'])) != int(q['door']) for q in drive if q.get('sprite') == '1'):
            return f"{name}: the door clip not on the engine's stage", None
        smooth = ''
        if drive[0].get('sprite') != '1':
            # A SMOOTH DOOR: the clip's frame drawn moves every tick, so the door's lift (the
            # largest translation of the clip's parts at the frame drawn) changes on every
            # consecutive tick while it opens, reaches the clip's full opening, and comes
            # back down every tick as it shuts
            lift = [float(q['lift']) for q in drive]
            full = float(drive[0]['full'])
            i = next((k for k, v in enumerate(lift) if v > 0.0), None)
            if i is None or full <= 0.0:
                return f'{name}: the door never moved off its rest', None
            j = i
            while j + 1 < len(lift) and lift[j + 1] > lift[j]:
                j += 1
            if abs(lift[j] - full) > 1e-3 * full:
                return (f'{name}: the door stopped rising at {lift[j]:.1f} of {full:.1f} after '
                        f'{j - i + 1} ticks (a tick that did not move it, or not fully open)'), None
            h = j
            while h + 1 < len(lift) and abs(lift[h + 1] - full) <= 1e-3 * full:
                h += 1
            c = h
            while c + 1 < len(lift) and lift[c + 1] < lift[c]:
                c += 1
            if lift[c] > 0.0:
                return f'{name}: the door stopped shutting at {lift[c]:.1f} after {c - h} ticks', None
            smooth = (f'; drawn smoothly: its lift changed on each of {j - i + 1} consecutive ticks to '
                      f'fully open ({full:.0f} pack units), held {h - j} ticks, down again on each of {c - h}')
        return None, (f"{name}: the War Factory's door opens with the engine (stages 0 to 3, open "
                      f'{sum(1 for x in doors if x == 3)} ticks) as its model, the tank rolling out, and shuts'
                      + smooth)
    if any(int(a) > 0 and b == 'model' for a, b in rows):
        return f'{name}: the factory without a door clip drawn by its model while open', None
    return None, f'{name}: the War Factory has no door clip in this folder: its 1996 card while the door is open'


def check(d):
    staged = staged_types()
    f348, n348, f349, n349 = [], [], [], []
    for tag, (scen, house, b, c, build, nuke) in RUNS.items():
        name = NAMES[tag]
        text = read(f'{d}/{tag}.log')
        if 'GAMEOVER|' in text:
            f348.append(f'{name}: the mission ended during the run')
            continue
        if tag in DOOR_RUN:
            bad, ok = check_door(name, text, d, tag)
            (f348.append(bad) if bad else n348.append(ok))
            continue
        if tag in SILO_RUNS:
            bad, ok = check_silo_fill(name, text, d, tag, house)
            (f348.append(bad) if bad else n348.append(ok))
            continue
        try:
            B = json.load(open(f'{d}/{tag}_block.json'))
            rows = [tuple(r[:3]) + (tuple(r[3]),) for r in B['rows']]
            sold = tuple(B['sold'][:3]) + (tuple(B['sold'][3]),) if B.get('sold') else None
        except (OSError, ValueError, KeyError):
            f348.append(f'{name}: no block')
            continue
        bad, ok = check_replays(name, text, rows, B['placed'], sold)
        if bad:
            f348.append(bad)
        elif ok:
            n348.append(ok)
        rows = [r for r in rows if r != sold]
        for fn, args, fails, notes in ((check_place, (name, text), f348, n348) if build else (None,) * 4,
                                       (check_silo, (name, text), f348, n348) if nuke else (None,) * 4,
                                       (check_stages, (name, text, rows, staged, B.get('control')), f349, n349)):
            if not fn:
                continue
            bad, ok = fn(*args)
            if bad:
                fails.append(bad)
            elif ok:
                notes.append(ok)
    bad, ok = check_sea(d)
    if bad:
        f349.append(bad)
    elif ok:
        n349.append(ok)
    print(('G348 0 ' + '; '.join(f348)) if f348 else
          'G348 1 Red Alert 3D plays its structures\' placements and driven clips: ' + '; '.join(n348))
    if not staged:
        print('G349 - no structure is staged with its stages in this folder (the lab\'s are in '
              'review; tools/ra3d/stage-structures.py --include-review stages them for a test folder)')
    else:
        print(('G349 0 ' + '; '.join(f349)) if f349 else
              f'G349 1 Red Alert 3D draws {len(staged)} structures by their stages ({", ".join(staged)}): '
              + '; '.join(n349))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'script':
        script(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        sys.exit(__doc__)
