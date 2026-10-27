#!/usr/bin/env python3
"""Export Red Alert's structures from the Red Alert lab: the approved standing model, and its
stages where the lab's stages are approved.

Usage: tools/ra3d/bake-structures.py [--export-root build-native/remaster-ra] [TYPE ...]
       tools/ra3d/bake-structures.py --include-review --out MODULE/ebld [TYPE ...]
       tools/ra3d/bake-structures.py --lock TYPE ...
       tools/ra3d/bake-structures.py --take-model TYPE ...   (a remodel approved: its new lock)

The engine source supplies footprints; the RA lab supplies plot turns, sizes on the plot
(art/remaster-ra/scale.json) and textures.

TWO KINDS OF FILE, chosen per type (game/ebld_mod.h draws both):
  intact   the approved standing model alone (and its idle motion: turrets, spins, flags,
           flipbooks, a fill, a clip). It stands whole at every strength; the engine's 1996
           states play the rest.
  full     the standing model with its stages, as Tiberian Dawn's structures are baked
           (tools/art/remaster/bake_ebld.py): the hit chips and zones, the damaged model, the
           effects (the intact idle smoke included) and the destroyed stage with its
           explosion. Baked when the lab's row carries `stages_approved` from the destruction
           recipe on (RECIPE_FROM), no earlier than its standing model's approval and no
           earlier than the stage export it is read with, and its stage sources match the
           hashes recorded for them in tools/ra3d/structure-inputs.json (`stage_sources`,
           written by --lock). Stages redone after their approval are in review again: the
           type stays intact only. The row's state never decides it (a row back in review has
           its stages in review, not its standing model).

Every full file is held to the stage budget: its destroyed stage at most DESTROYED_PIECES
pieces and DESTROYED_TRIANGLES triangles (the recipe's numbers; an older all-shattered stage
must never ship). An approved one over it is refused.

--include-review bakes the stages still in review too, for a test folder (stage-structures.py
--include-review passes it with the module's ebld/ as --out; a build never does): a stage over
the budget is baked with a warning, and a type whose stage export cannot be baked keeps its
intact file. It refuses to write into game/ebld_ra.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'tools/art/remaster'
GAME_OUT = ROOT / 'game/ebld_ra'
INPUTS = ROOT / 'tools/ra3d/structure-inputs.json'
# Walls use the separate connection-mask path, not a building mesh.
TYPES = 'AFLD AGUN APWR ATEK BARR DOME FACT FCOM FIX FTUR GAP GUN HBOX HPAD IRON KENN MSLO PBOX PDOX POWR PROC SAM SILO SPEN STEK SYRD TENT TSLA WEAP'.split()
# THE DESTRUCTION RECIPE (tools/art/remaster/README.md, "The destroyed state"): a destroyed stage
# starts where the damage ended and breaks by material within a piece budget. Stages approved
# before it are the old all-shattered kind.
RECIPE_FROM = '2026-10-11'
DESTROYED_PIECES = 150
DESTROYED_TRIANGLES = 30000


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def has_stages(view):
    return (view / 'destroy.json').is_file() and (view / 'destroyed.json').is_file()


STAGE_FILES = ('destroy.json', 'destroyed.json', 'fx.json')


def stage_export_time(view):
    """When the lab last wrote the type's stages (its newest stage file), as the review rows
    stamp times: 'YYYY-MM-DD HH:MM', local time."""
    import time
    t = max((view / n).stat().st_mtime for n in STAGE_FILES if (view / n).is_file())
    return time.strftime('%Y-%m-%d %H:%M', time.localtime(t))


def stages_approved(review, view=None, carried=None):
    """Whether the lab's stages of a row, as exported now, are approved for the game: its
    `stages_approved` stamp is from the destruction recipe on, no older than the standing
    model's approval, and no older than the stage export it is read with (stages redone
    after their approval are in review again, whatever the row's state says).

    What counts as the stage export is its geometry and clips (STAGE_FILES): a texture
    resampled after the approval (a damaged atlas halved to fit the texture budget) does not
    move it; the lock (`stage_sources`) records the textures as they are when it is taken.

    `carried` is a model's `stages_carried` in structure-inputs.json: a stage export written
    after the approval that the approval is recorded as covering (a clip retimed with the
    stages otherwise as approved), with the export time it covers and why. It covers that
    export only: a later one is in review again."""
    at = review.get('stages_approved') or ''
    if not at or at < RECIPE_FROM or at < (review.get('remaster_approved') or ''):
        return False
    if carried and carried.get('at', '') > at:
        at = carried['at']
    return view is None or not has_stages(view) or at >= stage_export_time(view)


def riding_view(view, tmp):
    """A PART THAT RIDES THE TURRET: a group naming another as its `parent` (the AA Gun's four
    barrels, recoil groups of the turret) has no place in the game's file of its own, and the
    baker would stand it on the plot with the building. It is folded into its parent, so it
    turns with the turret (its own recoil is the lab's). Returns the folder to bake from: the
    export itself, or a copy in `tmp` whose remaster.json has the riders folded in."""
    R = json.loads((view / 'remaster.json').read_text())
    by = {g['mat']: g for g in R['groups']}
    riders = [g for g in R['groups'] if g.get('parent') in by and g is not by[g['parent']]]
    if not riders:
        return view, []
    keep = []
    for g in R['groups']:
        if g in riders:
            P = by[g['parent']]
            if P.get('tex') != g.get('tex') or P.get('house') != g.get('house'):
                raise SystemExit(f'bake-structures: {view.parent.name}: {g["mat"]} rides {P["mat"]} '
                                 'on another texture; it cannot be folded into it')
            for k in ('p', 'n', 'uv', 'c'):
                if k in P and k in g:
                    P[k] = P[k] + g[k]
            continue
        keep.append(g)
    R['groups'] = keep
    tmp.mkdir(parents=True, exist_ok=True)
    for f in view.iterdir():
        if f.name != 'remaster.json':
            (tmp / f.name).symlink_to(f.resolve())
    (tmp / 'remaster.json').write_text(json.dumps(R))
    return tmp, [g['mat'] for g in riders]


def baker_for(source):
    baker = load('ra_structure_baker', TOOLS / 'bake_ebld.py')
    baker.FOOTPRINT = {k: tuple(v['size']) for k, v in source.items()}
    # The RA anti-aircraft gun is authored north-facing. SAM and GUN keep their
    # existing south/north rest directions. Aiming follows the engine, not lab spin.
    baker.TURRET_FORWARD = {'GUN': 0, 'SAM': 128, 'AGUN': 0}
    baker.YAW_FILE = str(ROOT / 'art/remaster-ra/yaw.json')
    # THE SIZE ON THE PLOT: each model shrunk to stand on its cells as OpenCNC3D's remasters
    # stand on theirs (tools/art/remaster/ra/scale_fit.py), about the footprint's centre
    baker.SCALE_FILE = str(ROOT / 'art/remaster-ra/scale.json')
    # THE DEATH'S JOLT, in footprint cells: Red Alert's types by their own footprints (the
    # table of the shared baker is Tiberian Dawn's, whose SAM, SILO and WEAP are other buildings)
    baker.LAB_SHAKE_CELLS = {k: max(1, round(max(v))) for k, v in baker.FOOTPRINT.items()}
    return baker


def bake_types(types, export_root, out, include_review=False, lock=False, quiet=False):
    reviews = json.loads((TOOLS / 'reviews-ra.json').read_text())
    inputs = json.loads(INPUTS.read_text())
    locked = inputs['models']
    sys.path[:0] = [str(TOOLS / 'ra'), str(TOOLS / 'infantry')]
    structs = load('ra_structure_source', TOOLS / 'ra/structs.py')
    source = structs.classes()
    baker = baker_for(source)
    meta = {v['type']: v for v in json.loads((ROOT / 'art/remaster-ra/structures.json').read_text())}
    manifest = {'game': 'Red Alert',
                'stage': 'approved standing models; full stages where the lab approved them'
                         + ('; stages in review included (a test folder)' if include_review else ''),
                'models': {}}
    warnings, locks, kept = [], {}, []

    def refuse(msg):
        raise SystemExit(f'bake-structures: {msg}')

    def baked(code, out, intact_only):
        view = export_root / code / 'viewer'
        with tempfile.TemporaryDirectory() as tmp:
            src, riders = riding_view(view, Path(tmp) / code / 'viewer')
            info = baker.bake(code, str(src), str(out), str(export_root), intact_only=intact_only)
        if riders:
            # (the file was baked from the export with its riders folded in: the source it
            # stands for is the export's own)
            info['sources']['remaster.json'] = sha(view / 'remaster.json')
            info['notes'] = info.get('notes', []) + [f'folded into the turret: {", ".join(riders)}']
        return info

    def one(code):
        review = reviews[code]
        # (the standing model's approval: a row back in review tonight has its stages in
        # review, never its model, so the row's state decides nothing here)
        if not review.get('remaster_approved') and review.get('state') != 'approved':
            refuse(f'{code}: intact model is still awaiting review')
        view = export_root / code / 'viewer'
        approved = locked[code]
        moved = [name for name, digest in approved['sources'].items() if sha(view / name) != digest]
        if moved and not include_review:
            # A REMODEL IN REVIEW: the lab's standing export has moved on from the approved one
            # (the lock). A build keeps the approved file it has; the review folder takes the new
            # export (--include-review); approving the remodel moves the lock (--relock, or the
            # hashes by hand) and the next bake takes it.
            if (out / f'{code}.ebld').is_file():
                warnings.append(f'{code}: its standing export ({moved[0]}) is not the approved one '
                                f'(a remodel in review): the approved file is kept')
                kept.append(code)
                return
            refuse(f'{code}: {moved[0]} differs from the approved intact export')
        if moved:
            # (a test folder takes the lab's standing export as it is now, with whatever the
            # stages' work changed in it: a clip's moving parts split into their own groups)
            warnings.append(f'{code}: its standing export changed since its approval '
                            f'({", ".join(moved)}); baked as the lab has it now')
        if baker.plot_yaw(code) != approved['yaw'] or list(baker.FOOTPRINT[code]) != approved['footprint']:
            refuse(f'{code}: its approved plot turn or footprint changed')
        if baker.plot_scale(code) != approved.get('scale', 1.0):
            refuse(f'{code}: its size on the plot ({baker.plot_scale(code)}) is not the recorded '
                   f'{approved.get("scale", 1.0)}; record it in structure-inputs.json first')
        rm = json.loads((view / 'remaster.json').read_text())
        if rm['triangles'] > 10000:
            refuse(f'{code}: {rm["triangles"]} triangles exceeds the structure budget')
        if tuple(meta[code]['cells']) != baker.FOOTPRINT[code]:
            refuse(f'{code}: recorded plot disagrees with the current engine footprint')
        ok_stages = stages_approved(review, view, approved.get('stages_carried'))
        if ok_stages and approved.get('stage_sources') and not lock:
            # (approved stages, but not the export the lock recorded: the lab redid them since,
            # and those are in review)
            if any(not (view / n).is_file() or sha(view / n) != d
                   for n, d in approved['stage_sources'].items()):
                warnings.append(f'{code}: its stage export is not the one approved; standing model only')
                ok_stages = False
        full = has_stages(view) and (ok_stages or include_review)
        info = None
        if full:
            if ok_stages and not include_review and not lock:
                want = approved.get('stage_sources')
                if not want:
                    refuse(f'{code}: its stages are approved but their sources are not recorded; '
                           f'tools/ra3d/bake-structures.py --lock {code} first')
                for name, digest in want.items():
                    if sha(view / name) != digest:
                        refuse(f'{code}: {name} differs from the approved stage export')
            try:
                info = baked(code, out, intact_only=False)
            except (SystemExit, AssertionError, KeyError, ValueError) as e:
                if not include_review or ok_stages:
                    raise
                warnings.append(f'{code}: its stages in review could not be baked ({e}); its '
                                f'standing model only')
                info = None
            if info is not None:
                d = info['sets'].get('DESTROYED') or {}
                over = d.get('pieces', 0) > DESTROYED_PIECES or d.get('triangles', 0) > DESTROYED_TRIANGLES
                if over:
                    msg = (f'{code}: its destroyed stage has {d.get("pieces")} pieces and '
                           f'{d.get("triangles")} triangles, over the budget of {DESTROYED_PIECES} '
                           f'and {DESTROYED_TRIANGLES} (the destruction recipe redoes it)')
                    if not include_review:
                        refuse(msg)
                    warnings.append(msg)
                info['stages'] = 'approved' if ok_stages else 'review'
                if ok_stages:
                    locks[code] = {k: v for k, v in info['sources'].items()
                                   if k not in approved['sources']}
        if info is None:
            info = baked(code, out, intact_only=True)
            info['stages'] = 'intact only'
        info['export'] = f'build-native/remaster-ra/{code}/viewer'
        info['footprint_source'] = 'brain/vanilla/redalert/bdata.cpp'
        info['approval'] = {k: review[k] for k in ('state', 'phase', 'at', 'remaster_approved',
                                                    'stages_approved') if k in review}
        # Detect concurrent revisions during the bake, including every sampled texture.
        for name, digest in info['sources'].items():
            if sha(view / name) != digest:
                raise RuntimeError(f'{code}: {name} changed while exporting; review that version first')
        manifest['models'][code] = info

    for code in types:
        try:
            one(code)
        except SystemExit as e:
            # (a test folder takes what can be baked: a type whose standing export has moved on
            # since its approval keeps the file the module already has)
            if not include_review:
                raise
            warnings.append(f'{code}: left as staged ({e})')
    if lock:
        for code, srcs in locks.items():
            inputs['models'][code]['stage_sources'] = dict(sorted(srcs.items()))
        INPUTS.write_text(json.dumps(inputs, indent=1) + '\n')
        print(f'RA structures: stage sources recorded for {", ".join(sorted(locks)) or "none"}')
    if not quiet:
        full = sorted(k for k, v in manifest['models'].items() if v['stages'] != 'intact only')
        print(f'RA structures: {len(manifest["models"])} exports verified; with their stages: '
              + (', '.join(f'{k} ({manifest["models"][k]["stages"]})' for k in full) or 'none'))
        for w in warnings:
            print(f'   warning: {w}')
    return manifest


def geometry_added(a, b):
    """How many triangles export b adds to export a, or -1 when b lacks any of a's (corners,
    texture corners and textures alike, whatever groups they are split into)."""
    ta, tb = _tris(a), _tris(b)
    from collections import Counter
    ca, cb = Counter(ta), Counter(tb)
    if any(cb[k] < v for k, v in ca.items()):
        return -1
    return len(tb) - len(ta)


def same_geometry(a, b):
    """Whether two exports of a model carry the very same triangles."""
    return geometry_added(a, b) == 0


def _tris(R):
    def tris(R):
        out = []
        for g in R['groups']:
            tex = json.dumps(g.get('tex'), sort_keys=True)
            p, uv = g['p'], g['uv']
            for i in range(len(p) // 9):
                out.append((tex, tuple(round(x, 4) for x in p[i * 9:i * 9 + 9]),
                            tuple(round(x, 4) for x in uv[i * 6:i * 6 + 6])))
        return sorted(out)
    return tris(R)


def relock(types, export_root, allow_added=False):
    """THE LOCK MOVED ONTO A CLIP-CARRYING EXPORT. The lab's clip work splits the approved model's
    moving parts into groups of their own (clip_rm.py, keeping the approved export as
    backup/remaster_intact_approved.json); the model is otherwise the same. The lock moves to the
    new remaster.json only when the kept export is the locked one and the two carry the very same
    triangles."""
    inputs = json.loads(INPUTS.read_text())
    for code in types:
        view = export_root / code / 'viewer'
        keep = export_root / code / 'backup/remaster_intact_approved.json'
        m = inputs['models'][code]
        # (`approved_export` is the standing model as it was approved; a lock that has moved
        # before moves again from it, so a clip redone later relocks against the same triangles)
        base = m.get('approved_export', m['sources']['remaster.json'])
        if not keep.is_file() or sha(keep) != base:
            raise SystemExit(f'bake-structures: {code}: no kept approved export matching the lock')
        m['approved_export'] = base
        added = geometry_added(json.loads(keep.read_text()), json.loads((view / 'remaster.json').read_text()))
        if added < 0:
            raise SystemExit(f'bake-structures: {code}: the new export lacks some of the approved triangles')
        if added and not allow_added:
            raise SystemExit(f'bake-structures: {code}: the new export adds {added} triangles to the approved '
                             'ones (a clip\'s own parts); --allow-added moves the lock anyway')
        inputs['models'][code]['sources']['remaster.json'] = sha(view / 'remaster.json')
        print(f'{code}: the lock moved onto the clip-carrying export: every approved triangle in it'
              + (f', {added} added by its clip' if added else ''))
    INPUTS.write_text(json.dumps(inputs, indent=1) + '\n')


def take_model(types, export_root):
    """A REMODEL APPROVED: the lock moves onto the lab's standing export as it is now, its new
    geometry included, only when the row's `remaster_approved` stamp is no older than every
    file of that export the game's file is baked from (the files an intact bake reads). The
    plot turn, size and footprint checks stay as they are."""
    import time
    reviews = json.loads((TOOLS / 'reviews-ra.json').read_text())
    inputs = json.loads(INPUTS.read_text())
    sys.path[:0] = [str(TOOLS / 'ra'), str(TOOLS / 'infantry')]
    structs = load('ra_structure_source', TOOLS / 'ra/structs.py')
    baker = baker_for(structs.classes())
    for code in types:
        at = reviews[code].get('remaster_approved') or ''
        view = export_root / code / 'viewer'
        with tempfile.TemporaryDirectory() as tmp:
            src, _ = riding_view(view, Path(tmp) / 'src' / code / 'viewer')
            info = baker.bake(code, str(src), str(Path(tmp) / 'out'), str(export_root), intact_only=True)
        newest = max(time.strftime('%Y-%m-%d %H:%M', time.localtime((view / n).stat().st_mtime))
                     for n in info['sources'])
        if not at or at < newest:
            raise SystemExit(f'bake-structures: {code}: its standing export ({newest}) is newer than its '
                             f'approval ({at or "none"}); the remodel is still in review')
        m = inputs['models'][code]
        m['sources'] = {n: sha(view / n) for n in sorted(info['sources'])}
        m.pop('approved_export', None)
        m.pop('stage_sources', None)
        print(f'{code}: the lock moved onto the approved remodel ({len(m["sources"])} files, approved {at})')
    INPUTS.write_text(json.dumps(inputs, indent=1) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--export-root', type=Path, default=ROOT / 'build-native/remaster-ra')
    ap.add_argument('--out', type=Path, default=GAME_OUT)
    ap.add_argument('--include-review', action='store_true',
                    help='also bake the stages still in review (a test folder; never into game/ebld_ra)')
    ap.add_argument('--lock', action='store_true',
                    help='record the approved stages\' sources in tools/ra3d/structure-inputs.json')
    ap.add_argument('--relock', action='store_true',
                    help='move the standing model\'s lock onto its clip-carrying export (same triangles)')
    ap.add_argument('--allow-added', action='store_true',
                    help='with --relock: the new export may add triangles of its clip\'s own')
    ap.add_argument('--take-model', action='store_true',
                    help='move the lock onto a remodel the lab approved (new geometry)')
    ap.add_argument('types', nargs='*', choices=TYPES)
    args = ap.parse_args()
    if args.take_model:
        return take_model(args.types, args.export_root)
    if args.relock:
        return relock(args.types, args.export_root, args.allow_added)
    if args.include_review and args.out.resolve() == GAME_OUT.resolve():
        ap.error('--include-review bakes stages still in review: give a test folder with --out')
    manifest = bake_types(args.types or TYPES, args.export_root, args.out,
                          include_review=args.include_review, lock=args.lock)
    args.out.mkdir(parents=True, exist_ok=True)
    mpath = args.out / 'manifest.json'
    if mpath.is_file():
        # (a few types re-baked: the others' entries kept)
        old = json.loads(mpath.read_text()).get('models', {})
        old.update(manifest['models'])
        manifest['models'] = dict(sorted(old.items()))
    mpath.write_text(json.dumps(manifest, indent=1) + '\n')


if __name__ == '__main__':
    main()
