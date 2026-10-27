#!/usr/bin/env python3
"""Bake Red Alert's structure placements into game/eplc_ra/, the folder tools/ra3d/stage-structures.py
stages into a module's eplc/ (game/ebld_place.h plays them under Enhanced).

Usage: tools/ra3d/bake-placements.py [--export-root build-native/remaster-ra] [--out game/eplc_ra] [TYPE ...]
       tools/ra3d/bake-placements.py --include-review --out MODULE/eplc --ebld MODULE/ebld [TYPE ...]

Every type whose lab export has a placement (place_<TYPE>.json): the Construction Yard's once its
row is approved (its deploy out of the MCV), every other type's once its stages are (the
placement is put up with them). Each is matched against the building file it will play on as
the engine matches it, and refused if a triangle is missing. --include-review bakes the ones
still in review too, for a test folder, and leaves out one that does not match.

THE SAME FILE AS OPENCNC3D'S PLACEMENTS (tools/art/remaster/bake_eplc.py), from the Red Alert
lab's placement (<TYPE>/viewer/place_<TYPE>.json, written by tools/art/remaster/place_rm.py
from tools/art/remaster/ra/place_<TYPE>.py), but carried onto the building as the game draws
it. The lab builds a placement on the export as approved, squared to its cells and at its own
size; the game's file of the building (game/ebld_ra/<TYPE>.ebld, tools/ra3d/bake-structures.py)
stands TURNED on its plot (art/remaster-ra/yaw.json) and SHRUNK about its footprint's centre
(art/remaster-ra/scale.json), and a placement finds that file's triangles by where they lie. So
every part of the building, its motions, its cuts and its dust outlines are taken through the
same turn and size here (a turned part's turns about the same pivot, its slides turned and
shortened, a scale about its pivot along the turned axes: two held turns either side of it),
and the triangles then lie where the game's file has them.

THE VEHICLE IT DEPLOYS FROM (the Construction Yard's MCV, the placement's companion) is not
turned or shrunk: it stands where the engine leaves the MCV, as the game draws the MCV, at its
own size (game/evhc_ra/MCV.evhc). Its parts that travel and are taken over by the building's
(the gantry by the crane, the drum by the arch and the hall) make up the plot's turn and size
on their way: each turns about the footprint's centre by the plot's turn and shrinks to the
plot's size, eased over the whole of its travel (from its first move after it rises with the
slab to its last before it goes), so it arrives exactly where the turned, shrunk building's
part takes over and the lab's hand-overs hold. Its slides come out turned and sized, but for
the lifts in between, which keep their height so the parts still clear the lorry, the last
vertical slide making up the height. A part that does not travel (the lorry, which sinks into
the slab) rises with the turned slab's top and sinks by its own height. The companion's model
is named MCV in the file, the Red Alert type the engine reports.

Each type's inputs are checked first: the building's export against the hashes its game file
was baked from (tools/ra3d/structure-inputs.json), its turn and size against the ones recorded
there, and the companion's export against the hashes game/evhc_ra/manifest.json recorded for
the vehicle's own file. Writes game/eplc_ra/<TYPE>.eplc and game/eplc_ra/manifest.json.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'tools/art/remaster'
sys.path.insert(0, str(TOOLS))
import bake_eplc as B  # noqa: E402

TYPES = tuple('AFLD AGUN APWR ATEK BARR DOME FACT FCOM FIX FTUR GAP GUN HBOX HPAD IRON KENN MSLO PBOX '
              'PDOX POWR PROC SAM SILO SPEN STEK SYRD TENT TSLA WEAP'.split())
COMPANION_TYPE = {'vehicles/MCV': 'MCV'}
GAME_OUT = ROOT / 'game/eplc_ra'


def place_approved(code, review, view):
    """Whether the lab's placement of a type, as exported now, is approved: the Construction
    Yard's with its row (its deploy out of the MCV), every other type's with its stages (the
    placement is one of the stages the lab puts up together), and only if that approval is no
    older than the placement file (one redone since is in review again). Never by the row's
    state: a row back in review has its stages in review, not its standing model."""
    import time
    at = review.get('remaster_approved' if code == 'FACT' else 'stages_approved') or ''
    if not at:
        return False
    if code == 'FACT':
        return True
    made = time.strftime('%Y-%m-%d %H:%M', time.localtime((view / f'place_{code}.json').stat().st_mtime))
    return at >= made


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


class Plot:
    """The turn (degrees counter-clockwise seen from above) and size about the footprint's
    centre on the ground, in the export's frame (cells, X east, Y north, Z up)."""

    def __init__(self, yaw, scale):
        self.yaw, self.s = float(yaw), float(scale)
        r = math.radians(self.yaw)
        self.c, self.sn = math.cos(r), math.sin(r)

    def rot(self, v):
        x, y = float(v[0]), float(v[1])
        out = [x * self.c - y * self.sn, x * self.sn + y * self.c]
        return out + [float(v[2])] if len(v) > 2 else out

    def pt(self, p):
        return [self.s * q for q in self.rot(p)]

    def vec(self, v):
        return self.pt(v)


def plot_remaster(R, P):
    """The building's export taken through the plot: positions turned and sized, normals turned."""
    R = copy.deepcopy(R)
    for g in R['groups']:
        p = g['p']
        for i in range(0, len(p), 3):
            p[i:i + 3] = P.pt(p[i:i + 3])
        n = g.get('n')
        if n:
            for i in range(0, len(n), 3):
                n[i:i + 3] = P.rot(n[i:i + 3])
    return R


def plot_outline(E, P):
    E = dict(E)
    E['pts'] = [P.pt([q[0], q[1]]) for q in E['pts']]
    E['n'] = [P.rot([q[0], q[1]]) for q in E['n']]
    E['z'] = E['z'] * P.s
    for k in ('edge', 'span'):
        if k in E:
            E[k] = E[k] * P.s
    if 'through' in E and E['through'] is not None:
        E['through'] = E['through'] * P.s
    return E


def plot_clip(c, P):
    return dict(c, p=P.pt(c['p']), n=P.rot(c['n']))


def plot_building_motion(m, P):
    """One motion of a building part, taken through the plot (conjugated: T M T^-1)."""
    t = m['type']
    m = dict(m)
    if t == 'slide':
        m['from'] = P.vec(m['from'])
        if 'to' in m:
            m['to'] = P.vec(m['to'])
        if m.get('overshoot'):
            m['overshoot'] = m['overshoot'] * P.s
        return [m]
    if t == 'hinge':
        m['pivot'] = P.pt(m['pivot'])
        m['axis'] = P.rot(m['axis'])
        return [m]
    if t == 'scale':
        f = m['from']
        f = [f, f, f] if not isinstance(f, (list, tuple)) else list(f)
        piv = P.pt(m['pivot'])
        m['pivot'] = piv
        m['from'] = f
        if abs(f[0] - f[1]) < 1e-9 or not P.yaw:
            return [m]
        # a scale along the export's axes, about a turned pivot along the turned axes: turned
        # back, scaled, turned again (two held turns, which cancel once the scale is over)
        held = dict(type='hinge', pivot=piv, axis=[0.0, 0.0, 1.0], delay=0.0, len=0.001,
                    ease='linear', hold=True)
        return [dict(held, **{'from': -P.yaw, 'to': -P.yaw}), m,
                dict(held, **{'from': P.yaw, 'to': P.yaw})]
    if t == 'shake':
        m['amp'] = m['amp'] * P.s
        return [m]
    if t == 'extend':
        m['paths'] = [[P.pt(q) for q in path] for path in m['paths']]
        return [m]
    if t in ('spin_up', 'morph'):
        # (a morph's offsets are the placement's `morph`, turned and sized with it in
        # plot_placement)
        return [m]
    raise SystemExit(f'bake-placements: a {t} motion has no plot rule')


def is_hide(m):
    return m['type'] == 'slide' and (m.get('to') or [0, 0, 0])[2] <= -3.0


def is_slab_lift(m, top):
    to = m.get('to') or [0, 0, 0]
    return m['type'] == 'slide' and abs(to[0]) < 1e-9 and abs(to[1]) < 1e-9 and abs(to[2] - top) < 1e-9


def travels(a, top):
    return any(m['type'] == 'hinge' or (m['type'] == 'slide' and not is_hide(m) and not is_slab_lift(m, top)
                                        and (abs((m.get('to') or [0, 0, 0])[0]) > 1e-9
                                             or abs((m.get('to') or [0, 0, 0])[1]) > 1e-9))
               for m in a['motions'])


def plot_companion(a, P, top, length, rides_traveller):
    """A part of the vehicle it deploys from: see the top of this file. A part riding one
    that travels (the drum's rear segment) moves in its frame and is left as it is."""
    a = copy.deepcopy(a)
    mo = a['motions']
    if rides_traveller:
        a['clip'] = [plot_clip(c, P) for c in a.get('clip', [])]
        return a, None
    if not travels(a, top):
        for m in mo:
            if is_slab_lift(m, top):
                m['to'] = [0.0, 0.0, top * P.s]
        a['clip'] = [plot_clip(c, P) for c in a.get('clip', [])]
        # (its dust outline stays round the lorry, at the turned slab's top)
        if a.get('emerge'):
            a['emerge'] = dict(a['emerge'], z=a['emerge']['z'] * P.s)
        return a, None
    # its travel: from its first move after the slab's lift to the end of its last before it goes
    moves = [m for m in mo if not is_hide(m) and not is_slab_lift(m, top) and m['type'] in ('slide', 'hinge')]
    t0 = min(m['delay'] for m in moves)
    t1 = max(m['delay'] + m['len'] for m in moves)
    # the slides turned and sized, but the lifts in between, which keep their height; the
    # last vertical one makes the height up
    want = sum((m.get('to') or [0, 0, 0])[2] - m['from'][2] for m in mo if m['type'] == 'slide' and not is_hide(m))
    vert = [m for m in mo if m['type'] == 'slide' and not is_hide(m) and not is_slab_lift(m, top)
            and abs((m.get('to') or [0, 0, 0])[2] - m['from'][2]) > 1e-9]
    last = max(vert, key=lambda m: m['delay'] + m['len']) if vert else None
    for m in mo:
        if m['type'] != 'slide':
            continue
        to = list(m.get('to') or [0.0, 0.0, 0.0])
        fr = list(m['from'])
        keep_z = m in vert and m is not last
        f2, t2 = P.vec(fr), P.vec(to)
        if keep_z:
            f2[2], t2[2] = fr[2], to[2]
        m['from'], m['to'] = f2, t2
        if m.get('overshoot'):
            m['overshoot'] = m['overshoot'] * P.s
    if last is not None:
        have = sum(m['to'][2] - m['from'][2] for m in mo if m['type'] == 'slide' and not is_hide(m))
        last['to'][2] += want * P.s - have
    # the plot's turn and size, eased over the travel, about the footprint's centre: a turn,
    # and a size made of a held one at the plot's (it never starts before the end) and one from
    # the plot's inverse to 1 over the travel, their product going from 1 to the plot's size
    span = t1 - t0
    mo += [dict(type='hinge', pivot=[0.0, 0.0, 0.0], axis=[0.0, 0.0, 1.0], **{'from': 0.0, 'to': P.yaw},
                delay=t0, len=span, ease='inout', hold=True),
           dict(type='scale', pivot=[0.0, 0.0, 0.0], **{'from': [P.s] * 3}, delay=length - 0.001,
                len=0.001, ease='linear'),
           dict(type='scale', pivot=[0.0, 0.0, 0.0], **{'from': [1.0 / P.s] * 3}, delay=t0, len=span,
                ease='inout')]
    a['clip'] = [plot_clip(c, P) for c in a.get('clip', [])]
    return a, (round(t0, 3), round(t1, 3))


def plot_placement(PL, P):
    PL = copy.deepcopy(PL)
    top = float(PL['slab_top'])
    length = float(PL['length'])
    travel = {}
    out = []
    acts = PL['actors']
    moving = set()
    for i, a in enumerate(acts):
        if a.get('companion') and (a['parent'] < 0 or not acts[a['parent']].get('companion')) \
           and travels(a, top):
            moving.add(i)
    for i, a in enumerate(acts):
        if a.get('companion'):
            rides = a['parent'] >= 0 and a['parent'] in moving
            a2, tr = plot_companion(a, P, top, length, rides)
            if tr:
                travel[a['name']] = tr
            out.append(a2)
            continue
        a = copy.deepcopy(a)
        a['zmin'] = a['zmin'] * P.s
        a['zmax'] = a['zmax'] * P.s
        a['clip'] = [plot_clip(c, P) for c in a.get('clip', [])]
        mo = []
        for m in a['motions']:
            mo += plot_building_motion(m, P)
        a['motions'] = mo
        for k in ('emerge', 'puff'):
            if a.get(k):
                a[k] = plot_outline(a[k], P)
        out.append(a)
    PL['actors'] = out
    PL['slab_top'] = top * P.s
    # A BENT PART'S OFFSETS (a morph: per group and actor, an offset per corner of each of the
    # actor's triangles, cells, Z up), turned and sized as the corners they move are
    for g, per in (PL.get('morph') or {}).items():
        for act, d in per.items():
            q = []
            for i in range(0, len(d), 3):
                q += P.vec(d[i:i + 3])
            per[act] = q
    return PL, travel


def bake(code, export_root, out, inputs, evhc_manifest, strict=True):
    view = export_root / code / 'viewer'
    approved = inputs[code]
    for name, digest in approved['sources'].items():
        # (a test folder's placement is matched against the file baked beside it instead)
        if strict and name in ('remaster.json',) and sha(view / name) != digest:
            raise SystemExit(f'bake-placements: {code}: {name} differs from the export its game file was baked from')
    P = Plot(approved.get('yaw', 0.0), approved.get('scale', 1.0))
    PL = json.loads((view / f'place_{code}.json').read_text())
    R = json.loads((view / 'remaster.json').read_text())
    srcs = {f'{code}/viewer/place_{code}.json': sha(view / f'place_{code}.json'),
            f'{code}/viewer/remaster.json': sha(view / 'remaster.json')}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / code / 'viewer').mkdir(parents=True)
        PL2, travel = plot_placement(PL, P)
        C = PL2.get('companion')
        comp = None
        if C:
            name = COMPANION_TYPE.get(C['model'], Path(C['model']).name)
            cview = export_root / C['model'] / 'viewer'
            rec = (evhc_manifest.get('models') or {}).get(name) or {}
            want = (rec.get('sources') or {}).get('remaster.json')
            got = sha(cview / 'remaster.json')
            if want != got:
                raise SystemExit(f'bake-placements: {code}: the {name} it deploys from is not the export '
                                 f'game/evhc_ra/{name}.evhc was baked from')
            (tmp / name / 'viewer').mkdir(parents=True)
            shutil.copy2(cview / 'remaster.json', tmp / name / 'viewer' / 'remaster.json')
            srcs[f'{C["model"]}/viewer/remaster.json'] = got
            C['model'] = name
            comp = name
        (tmp / code / 'viewer' / f'place_{code}.json').write_text(json.dumps(PL2))
        (tmp / code / 'viewer' / 'remaster.json').write_text(json.dumps(plot_remaster(R, P)))
        info = B.bake(code, str(tmp), str(out))
    info['export'] = f'build-native/remaster-ra/{code}/viewer'
    info['sources'] = srcs
    info['plot'] = dict(yaw=P.yaw, scale=P.s, source='tools/ra3d/structure-inputs.json')
    if comp:
        info['companion_model'] = comp
        info['companion_travel'] = travel
    info['baker'] = 'tools/ra3d/bake-placements.py (tools/art/remaster/bake_eplc.py)'
    return info


def bake_types(types, export_root, out, ebld_dir, include_review=False):
    """Bake each type's placement that is approved (or, with include_review, any the lab has),
    and match it against the building file it will play on (ebld_dir/<TYPE>.ebld) as the engine
    does at attach: an approved one that misses a triangle is refused, one in review is left
    out with a warning. Returns the manifest's models and what was left out."""
    reviews = json.loads((TOOLS / 'reviews-ra.json').read_text())
    inputs = json.loads((ROOT / 'tools/ra3d/structure-inputs.json').read_text())['models']
    evm = json.loads((ROOT / 'game/evhc_ra/manifest.json').read_text())
    out.mkdir(parents=True, exist_ok=True)
    models, left = {}, []
    for code in types:
        view = export_root / code / 'viewer'
        if not (view / f'place_{code}.json').is_file():
            continue
        if not include_review and (out / f'{code}.eplc').is_file() and code in inputs \
                and sha(view / 'remaster.json') != inputs[code]['sources'].get('remaster.json'):
            # (A REMODEL IN REVIEW: the lab's building has moved on from the approved one, and
            # its placement with it; the build keeps the approved file and its placement)
            left.append(f'{code} [kept]: its building export is not the one its game file was baked from')
            continue
        ok = place_approved(code, reviews.get(code) or {}, view)
        have = out / f'{code}.eplc'
        ebld = Path(ebld_dir) / f'{code}.ebld'
        if not ok and not include_review and have.is_file() and ebld.is_file() \
                and B.match(str(have), str(ebld)) == 0:
            # (the lab has redone an approved placement since: until that is approved the
            # build keeps the approved one, which still finds every triangle of its building)
            left.append(f'{code} [kept]: its placement was redone in the lab since its approval')
            continue
        if not ok and not include_review:
            left.append(f'{code} (its placement is in review in the lab)')
            continue
        if ok and not include_review and sha(view / 'remaster.json') != inputs[code]['sources'].get('remaster.json'):
            # (the lab's building has moved on since the game's file was baked: whatever
            # placement the game has stays, matched to the file it has)
            left.append(f'{code} [kept]: its building export is not the one its game file was baked from')
            continue
        try:
            info = bake(code, export_root, out, inputs, evm, strict=not include_review)
        except SystemExit as e:
            have = out / f'{code}.eplc'
            ebld = Path(ebld_dir) / f'{code}.ebld'
            if ok and not include_review and have.is_file() and ebld.is_file() \
                    and B.match(str(have), str(ebld)) == 0:
                # (the lab's placement cannot be baked from the export as it is now, but the
                # one the game has still finds every triangle of the building file it plays on)
                left.append(f'{code} [kept]: {e}; the game\'s placement still matches {ebld.name}')
                continue
            if ok and not include_review:
                raise
            left.append(f'{code}: {e}')
            continue
        ebld = Path(ebld_dir) / f'{code}.ebld'
        miss = B.match(str(out / f'{code}.eplc'), str(ebld)) if ebld.is_file() else -1
        if miss != 0:
            why = (f'{code}: the placement misses {miss} of the building\'s triangles in {ebld}'
                   if miss > 0 else f'{code}: no building file {ebld} to match it against')
            if ok and not include_review:
                raise SystemExit(f'bake-placements: {why}; re-run the lab\'s --place on the approved model')
            (out / f'{code}.eplc').unlink()
            left.append(why)
            continue
        info['placement'] = 'approved' if ok else 'review'
        models[code] = info
    return models, left


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--export-root', type=Path, default=ROOT / 'build-native/remaster-ra')
    ap.add_argument('--out', type=Path, default=GAME_OUT)
    ap.add_argument('--ebld', type=Path, default=ROOT / 'game/ebld_ra',
                    help='the building files the placements are matched against')
    ap.add_argument('--include-review', action='store_true',
                    help='also bake the placements still in review (a test folder; never into game/eplc_ra)')
    ap.add_argument('types', nargs='*', choices=TYPES)
    a = ap.parse_args()
    if a.include_review and a.out.resolve() == GAME_OUT.resolve():
        ap.error('--include-review bakes placements still in review: give a test folder with --out')
    mpath = a.out / 'manifest.json'
    models = json.loads(mpath.read_text()).get('models', {}) if mpath.is_file() else {}
    new, left = bake_types(a.types or TYPES, a.export_root, a.out, a.ebld, a.include_review)
    models.update(new)
    # (one baked before whose placement is in review now goes: a build ships approved ones)
    for code in [c for c in models if c not in new and (not a.types or c in a.types)]:
        if any(l.startswith(f'{code} (') for l in left):
            (a.out / f'{code}.eplc').unlink(missing_ok=True)
            del models[code]
            print(f'   removed {code}.eplc: its placement is in review again')
    for l in left:
        print(f'   left out: {l}')
    mpath.write_text(json.dumps(dict(
        what="Red Alert's structure placements under Enhanced, carried onto the building as the game "
             "draws it (turned and sized on its plot) by tools/ra3d/bake-placements.py; EPLC files, "
             "layout in tools/art/remaster/bake_eplc.py",
        models=dict(sorted(models.items()))), indent=1) + '\n')
    print(f'manifest: {mpath} ({len(models)} models)')


if __name__ == '__main__':
    main()
