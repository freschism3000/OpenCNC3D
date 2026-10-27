#!/usr/bin/env python3
"""Copy the verified RA building files into a module without touching other art, then the
OpenCNC3D remasters the Red Alert lab approved as reuse (stage-reuse.py), the remastered
vehicles and aircraft (stage-vehicles.py), the structures' placements (game/eplc_ra, by
tools/ra3d/bake-placements.py) and the effects with forms of their own (written by their own
generators): a placement is staged when its building is, and, for one that deploys out of a
vehicle (the Construction Yard out of the MCV), when that vehicle is staged too, since without
it the yard would unfold out of nothing.

A build stages approved art only. --include-review also stages what is still in review in
the labs (the winter looks, Red Alert's own versions of a tree, a Red Alert vehicle's own
remaster, a structure's stages and placement), for a test folder where the review is done;
make-build.sh, stage-frontend.py and stage-runtime.py never pass it. A structure's stages and
placement in review are baked from the Red Alert lab's exports (--export-root, the shared lab's
build-native/remaster-ra by default) straight into the module, by tools/ra3d/bake-structures.py
--include-review and tools/ra3d/bake-placements.py --include-review: nothing in review is ever
written into game/ebld_ra or game/eplc_ra."""
import argparse
from importlib import import_module
import json
import os
import runpy
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))


ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / 'build-native/remaster-ra'
REUSE_FULL = ('GUN', 'HPAD')
# the walls reused from OpenCNC3D (stage-reuse.py stages their files): Tiberian Dawn's own
# placements, so a sandbag or concrete wall run goes up as it does there (the barbed wire has none)
REUSE_WALL_PLACE = ('SBAG', 'BRIK')


def lab_root():
    """The Red Alert lab's exports: this checkout's, or the shared checkout's a worktree
    names (OPENCNC3D_LAB_ROOT, as the lab's own server takes it)."""
    other = os.environ.get('OPENCNC3D_LAB_ROOT')
    return Path(other) / 'build-native/remaster-ra' if other else LAB


def stage_spec(src, dst):
    """A model's specular maps (<stem>.spec, tools/art/remaster/bake_spec.py) beside its staged
    file: copied when the source has them, removed when it does not."""
    s, d = Path(src).with_suffix('.spec'), Path(dst).with_suffix('.spec')
    if s.is_file():
        shutil.copy2(s, d)
    elif d.is_file():
        d.unlink()


def stage(module, include_review=False, export_root=None):
    stage_terrain_and_infantry(Path(module))
    source = ROOT / 'game/ebld_ra'
    models = sorted(source.glob('*.ebld'))
    if not models:
        raise RuntimeError(f'no RA building exports in {source}')
    target = Path(module) / 'ebld'
    target.mkdir(parents=True, exist_ok=True)
    for model in models:
        shutil.copy2(model, target / model.name)
        stage_spec(model, target / model.name)
    shutil.copy2(source / 'manifest.json', target / 'manifest.json')
    # THE TURRET AND THE HELIPAD ARE OPENCNC3D'S, approved as reuse in the Red Alert lab, on the
    # same plot (no turn, full size, the same footprint): Tiberian Dawn's own files, stages and
    # placement included, staged as they are under their own names
    whole = json.loads((source / 'manifest.json').read_text())
    for code in REUSE_FULL:
        shutil.copy2(ROOT / 'game/ebld' / f'{code}.ebld', target / f'{code}.ebld')
        stage_spec(ROOT / 'game/ebld' / f'{code}.ebld', target / f'{code}.ebld')
        td = json.loads((ROOT / 'game/ebld/manifest.json').read_text())['models'].get(code, {})
        whole['models'][code] = dict(td, stages='reuse', reuse_of=f'game/ebld/{code}.ebld')
    (target / 'manifest.json').write_text(json.dumps(whole, indent=1) + '\n')
    meta = whole['models']
    full = sorted(k for k, v in meta.items() if v.get('stages') not in (None, 'intact only'))
    print(f'RA structures staged: {len(models)} models in {target}'
          + (f'; with their stages: {", ".join(full)}' if full else '; standing models only'))
    review_place = None
    if include_review:
        review_place = stage_review(Path(module), export_root or lab_root())
    # and OpenCNC3D's remasters the Red Alert lab approved as reuse, beside them
    import_module('stage-reuse').stage(module, include_review=include_review)
    # and Red Alert's own props, approved in its lab (the barrels, the mines, the crate stacks,
    # the ice floes, the water crate, the flag location, the barbed-wire fence)
    import_module('stage-props').stage(module)
    # and the remastered vehicles and aircraft, Red Alert's own and the reused ones
    vehicles = import_module('stage-vehicles').stage(module, include_review=include_review)
    stage_placements(Path(module), vehicles, review_place)
    stage_effects(Path(module))
    stage_cursors(Path(module))
    stage_ore(Path(module))


# Red Alert's ore, gems and ore mine (game/ra3d_ore_mod.h reads ore/ore3d_<TH>.pack from the
# module; the mine is ebld/MINE_<TH>.ebld), baked by tools/ra3d/bake-ore.py from the lab's
# approved sets and committed in data/ra3d_ore. Staged while every one of the nine rows is
# still approved under the stamp the files were baked under; otherwise left out (named), and
# a copy an earlier run staged is removed, so the ore keeps its 1996 decals.
ORE_FILES = ('ore3d_TEM.pack', 'ore3d_WIN.pack', 'MINE_TEM.ebld', 'MINE_WIN.ebld')


def stage_ore(module):
    src = ROOT / 'data/ra3d_ore'
    dest = {n: (module / ('ebld' if n.endswith('.ebld') else 'ore') / n) for n in ORE_FILES}
    why = None
    man = src / 'manifest.json'
    if not man.is_file():
        why = 'no data/ra3d_ore/manifest.json (run tools/ra3d/bake-ore.py)'
    else:
        baked = json.loads(man.read_text()).get('approval', {})
        reviews = json.loads((ROOT / 'tools/art/remaster/reviews-ra.json').read_text())
        for row, at in sorted(baked.items()):
            r = reviews.get(row) or {}
            if r.get('state') != 'approved' or r.get('remaster_approved') != at:
                why = f'{row} is not approved under the stamp the files were baked under ({at}): rebake'
                break
        missing = [n for n in ORE_FILES if not (src / n).is_file()]
        if not why and missing:
            why = 'missing ' + ', '.join(missing)
    if why:
        for d in dest.values():
            if d.is_file():
                d.unlink()
        print(f'RA ore not staged: {why}; the ore and gems keep their 1996 decals')
        return
    for n, d in dest.items():
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src / n, d)
    print(f'RA ore staged: {", ".join(ORE_FILES)} in {module}')


# Red Alert's six cursors of its own (game/cursor3d_mod.h reads ecur/ecur_ra.bin from the
# module), baked from the approved lab models by tools/art/remaster/ra/bake_ecur_ra.py and
# committed, as Tiberian Dawn's game/ecur/ecur.bin is; the other twenty-two of its cursors
# are drawn from that file.
def stage_terrain_and_infantry(module):
    """Refresh normal-build RA assets as well as first-time frontend installs.

    make-build calls this stage for an existing module, not stage-frontend. Leaving
    these copies only in the first-time installer kept old terrain and soldiers.
    """
    for bank, theater in [('RASNOW', 'SNOW'), ('RATEMP', 'TEMPERAT')]:
        source = ROOT / 'art/remaster/textures' / bank
        subprocess.run([sys.executable, str(ROOT / 'tools/ra3d/bake_ra_terrain3d.py'),
                        'frombank', bank, str(source / f'terrainenh_{bank}.pack'),
                        str(source / f'terrainseam_{bank}.pack'),
                        str(module / f'terrainenh_RA_{theater}.pack'),
                        str(module / f'terrainseam_RA_{theater}.pack')], check=True)
    infantry = sorted((ROOT / 'game/einf_ra').glob('*.c3m'))
    if not infantry:
        raise RuntimeError('No current Red Alert infantry exports in game/einf_ra')
    (module / 'einf').mkdir(parents=True, exist_ok=True)
    for source in infantry:
        shutil.copy2(source, module / 'einf' / source.name)
    print(f'RA native terrain and {len(infantry)} infantry models refreshed in {module}')


def stage_cursors(module):
    src = ROOT / 'game/ecur/ecur_ra.bin'
    if not src.is_file():
        raise RuntimeError(f'no {src.relative_to(ROOT)}: run tools/art/remaster/ra/bake_ecur_ra.py')
    target = module / 'ecur'
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, target / 'ecur_ra.bin')
    print(f'RA cursors staged: ecur_ra.bin in {target}')


# Red Alert's effects with forms of their own (game/ra3d_atom_mod.h and game/ra3d_efxm_mod.h
# read them from the module's efx/) and the sheets of its own particle effects (efx_ra.pack,
# read beside efx_enh.pack): the game's files, written into the module by each effect's own
# committed generator, never copied from a lab folder (a lab folder can hold a copy in the
# middle of being rebuilt). Each entry: the generator, its arguments ({out} the module's
# efx folder), and the files it must have written there.
CRATES = ('ARMOR', 'CHRONBOX', 'DEVIATOR', 'DOLLAR', 'EARTH', 'EMPULSE', 'FPOWER', 'GPSBOX',
          'INVULBOX', 'INVUN', 'MINE', 'MISSILE2', 'PARABOX', 'RAPID', 'SONARBOX', 'SPEED',
          'STEALTH2', 'TQUAKE')
OTHERS = ('CORPSE1', 'CORPSE2', 'CORPSE3', 'FLAGFLY', 'MOVEFLSH', 'PARABOMB', 'PARACH', 'SPUTDOOR')
EFFECTS = (
    ('tools/art/remaster/ra/nuke3d_ATOMSFX.py', ('--efxm', '{out}/ATOMSFX.efxm'), ('ATOMSFX.efxm',)),
    ('tools/art/remaster/ra/crates3d.py', CRATES, tuple(t + '.efxm' for t in CRATES)),
    ('tools/art/remaster/ra/others3d.py', OTHERS, tuple(t + '.efxm' for t in OTHERS)),
)


def stage_effects(module):
    target = module / 'efx'
    target.mkdir(parents=True, exist_ok=True)
    names = []
    for script, args, outs in EFFECTS:
        lab_outputs = script.endswith(('crates3d.py', 'others3d.py'))
        if lab_outputs:
            # These generators now write complete effect rows (including the game's
            # .efxm) under build-native/.../viewer. Stage those exact generated bytes.
            r = subprocess.run([sys.executable, str(ROOT / script), *args],
                               capture_output=True, text=True)
            sources = [ROOT / 'build-native/remaster-ra/effects' / name[:-5] / 'viewer' / name
                       for name in outs]
            missing = [str(p) for p in sources if not p.is_file()]
            if r.returncode == 0 and not missing:
                for src, name in zip(sources, outs):
                    shutil.copy2(src, target / name)
        else:
            for name in outs:
                if (target / name).exists():
                    (target / name).unlink()
            r = subprocess.run([sys.executable, str(ROOT / script)] + [a.format(out=target) for a in args],
                               capture_output=True, text=True)
            missing = [n for n in outs if not (target / n).is_file()]
        if r.returncode != 0 or missing:
            raise RuntimeError(f'{script} did not write {", ".join(missing) or "its files"}: '
                               f'{r.stderr.strip()[-400:]}')
        names += outs
    # Released generators expose the native sheet writer. A local art-only
    # generator may omit it; keep an existing approved pack in that case, and
    # refuse a fresh installation that would otherwise lose RA's particle sheets.
    sheet = target / 'efx_ra.pack'
    writer = runpy.run_path(str(ROOT / 'tools/art/remaster/ra/effects_ra.py')).get('write_pack')
    if callable(writer):
        writer(str(sheet))
    elif not sheet.is_file():
        raise RuntimeError('RA effect sheet pack is missing and the current art generator has no native pack writer')
    names.append(sheet.name)
    print(f'RA effects staged: {len(names)} files ({", ".join(names)}) in {target}, from their generators')


def stage_review(module, export_root):
    """THE STAGES AND PLACEMENTS STILL IN REVIEW, for a test folder: every type whose lab export
    has stages is baked full into the module (over its committed file), and every placement the
    lab has into a folder beside it that stage_placements stages from. Returns that folder."""
    if not export_root.is_dir():
        raise RuntimeError(f'--include-review: no Red Alert lab exports in {export_root}')
    bs = import_module('bake-structures')
    target = module / 'ebld'
    staged = [t for t in bs.TYPES if t not in REUSE_FULL and bs.has_stages(export_root / t / 'viewer')]
    man = bs.bake_types(staged, export_root, target, include_review=True)
    mpath = target / 'manifest.json'
    whole = json.loads(mpath.read_text())
    whole['models'].update(man['models'])
    whole['stage'] = man['stage']
    mpath.write_text(json.dumps(whole, indent=1) + '\n')
    bp = import_module('bake-placements')
    place = module / 'eplc-review'
    if place.is_dir():
        shutil.rmtree(place)
    models, left = bp.bake_types([t for t in bp.TYPES if t not in REUSE_FULL], export_root, place,
                                 target, include_review=True)
    (place / 'manifest.json').write_text(json.dumps(dict(
        what='placements in review, baked for this test folder by tools/ra3d/bake-placements.py',
        models=models), indent=1) + '\n')
    for l in left:
        print(f'   placement left out: {l}')
    return place


def stage_placements(module, vehicles, review=None):
    source = ROOT / 'game/eplc_ra'
    target = module / 'eplc'
    meta = {}
    if (source / 'manifest.json').is_file():
        meta = json.loads((source / 'manifest.json').read_text()).get('models', {})
    files = {p.name: p for p in sorted(source.glob('*.eplc'))} if source.is_dir() else {}
    # (the placements in review baked for a test folder win over the committed ones)
    if review is not None:
        rmeta = json.loads((review / 'manifest.json').read_text()).get('models', {})
        for p in sorted(review.glob('*.eplc')):
            files[p.name] = p
            meta[p.stem] = dict(meta.get(p.stem, {}), **rmeta.get(p.stem, {}))
    for code in REUSE_FULL + REUSE_WALL_PLACE:
        files[f'{code}.eplc'] = ROOT / 'game/eplc' / f'{code}.eplc'
        meta[code] = dict(reuse_of=f'game/eplc/{code}.eplc')
    # (the props' placements: the barbed-wire fence's, which the Soviets build cell by cell;
    # staged, as every placement, only when its model is)
    props = ROOT / 'game/ebld_ra_props'
    for p in sorted(props.glob('*.eplc')) if props.is_dir() else []:
        files[p.name] = p
        meta[p.stem] = dict(source=f'game/ebld_ra_props/{p.name}')
    staged, left = [], []
    for p in files.values():
        code = p.stem
        comp = (meta.get(code) or {}).get('companion_model')
        if not (module / 'ebld' / f'{code}.ebld').is_file():
            left.append(f'{p.name} (no {code}.ebld staged)')
        elif comp and comp not in vehicles:
            left.append(f'{p.name} (the {comp} it deploys from is not staged)')
        else:
            staged.append(p)
    if target.is_dir():
        for old in target.glob('*.eplc'):
            if old.name not in {p.name for p in staged}:
                old.unlink()
    if staged:
        target.mkdir(parents=True, exist_ok=True)
        for p in staged:
            shutil.copy2(p, target / p.name)
        (target / 'manifest.json').write_text(json.dumps(dict(
            what="Red Alert's structure placements staged into this module",
            models={p.stem: meta.get(p.stem, {}) for p in staged}), indent=1) + '\n')
    if review is not None:
        shutil.rmtree(review)
    print(f'RA placements staged: {len(staged)} in {target}'
          + (f' ({", ".join(p.stem for p in staged)})' if staged else ''))
    for l in left:
        print(f'   left out: {l}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('module', type=Path)
    ap.add_argument('--include-review', action='store_true',
                    help='also stage the art still in review in the labs (a test folder)')
    ap.add_argument('--export-root', type=Path, default=None,
                    help='the Red Alert lab exports the stages in review are baked from')
    a = ap.parse_args()
    stage(a.module, include_review=a.include_review, export_root=a.export_root)
