#!/usr/bin/env python3
"""Stage the Tiberian Dawn remasters the Red Alert lab approved as reuse (and Red Alert's own
versions of them) into a module.

Usage: tools/ra3d/stage-reuse.py MODULE      (MODULE is a staged ra3d/ folder)

THE SAME ART, THE SAME MODEL. Red Alert keeps a good deal of Tiberian Dawn's 1995 art: the
civilian houses and farms, the hospital, the bio lab, the technology center, the steel crate,
most of the temperate trees. The Red Alert lab compares them picture for picture
(tools/art/remaster/ra/crossref.py, art/remaster-ra/crossref.md) and, where the art is the
same and OpenCNC3D's remaster of it is approved, its row in reviews-ra.json says so:
phase "reuse", state "approved", reuse_of naming the OpenCNC3D row. Those are the files
this copies, as they are in game/ebld: one model for both games, so a repaint approved for
OpenCNC3D reaches Red Alert with it.

WHICH ROWS. The lab's categories that are drawn from .ebld files and that the Red Alert 3D
renderer knows how to place (game/ra3d_sprites_mod.h, game/ebld_mod.h):
  civil/<T>    a civilian building (V01 to V19, BIO, HOSP, MISS), a crate (SCRATE, WCRATE)
               or a fence (CYCL, WOOD): game/ebld/<T>.ebld, every stage the file carries;
  foliage/<T>  a tree: game/ebld/<T>_TEM.ebld with its burning and snag stages beside it
               (<T>_TEM_BURN.ebld, <T>_TEM_SNAG.ebld); or a tree clump, TC01 to TC05
               (game/ebld/<T>_TEM.ebld alone: a clump never burns);
  SBAG, BRIK, BARB  the three walls, which the lab lists with the structures: an approved
               row whose structures.json entry keeps OpenCNC3D's remaster (its "keep" says
               reuse) is the same reuse.
THE WINTER LOOK GOES WITH IT. Red Alert's snow shows these assets as Tiberian Dawn's winter
does, and one file serves both (the remaster recipe's Theater looks): where game/ebld holds
a reused asset's winter look, <T>_WIN.ebld (a tree's with <T>_WIN_BURN.ebld and
<T>_WIN_SNAG.ebld), it is staged with the summer file once its winter row is approved
(reviews.json, and reviews-ra.json where the Red Alert lab has a row of its own for it), and
the engine draws it on RASNOW in the summer one's place (ebld_mod.h, ebld_pack_theater). A
winter look still in review leaves the 1996 picture on snow. The Red Alert lab's own rows
for the winter looks (<folder>/<T>_WIN, reusing the same files) are staged the same way.

RED ALERT'S OWN VERSIONS. Where Red Alert redrew a tree a little (crossref.py's ADAPTED:
the same drawing in Red Alert's 1996 colours, or a crown filled out), the lab carries a
version of OpenCNC3D's approved tree instead of the tree itself: its row's phase is
"RA version", it names no reuse_of, and the OpenCNC3D row of the same name is the tree it
adapts. Once that row is approved, the version is staged under the tree's own names, so the
engine draws it in the tree's place: each file from game/ebld_ra_versions where the version
changes it (tools/art/remaster/ra/bake_versions.py), and from game/ebld where it does not (a
recoloured tree's snag, which is bark only; a broadleaf tree's bare winter look). The
version's summer file must be in game/ebld_ra_versions: an approved version that is not
baked stops the staging, rather than OpenCNC3D's tree being staged in its place.

--include-review stages the versions and the winter looks still in review as well, for a
look in a test folder (a later run without it takes them out again). A build never passes
it, so a build carries approved art only.

A reuse row of another kind (the cursors and the effects have packs of their own, the
vehicles their EVHC files, which tools/ra3d/stage-vehicles.py stages beside this) is left
out and counted, so a new approval shows up here rather than vanishing.

The OpenCNC3D row a reuse names must itself be approved in reviews.json; one that is not
is left out and named. Files staged by an earlier run whose row is no longer approved are
removed again (only names this script wrote, read back from the manifest it leaves).

Writes MODULE/ebld/reuse-manifest.json: every row, its files and their SHA-256.
"""
import argparse
import hashlib
import shutil
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
REVIEWS_RA = ROOT / 'tools/art/remaster/reviews-ra.json'
REVIEWS_TD = ROOT / 'tools/art/remaster/reviews.json'
SOURCE = ROOT / 'game/ebld'
VERSIONS = ROOT / 'game/ebld_ra_versions'
VERSION_PHASE = 'RA version'
MANIFEST = 'reuse-manifest.json'

# The civilian types, crates and fences the renderer draws from a reused model.
CIVIL = re.compile(r'^(V(0[1-9]|1[0-9])|BIO|HOSP|MISS|SCRATE|WCRATE|CYCL|WOOD)(_WIN)?$')
# The walls the lab lists among its structures.
WALL_ROWS = ('SBAG', 'BRIK', 'BARB')
STRUCTURES = ROOT / 'art/remaster-ra/structures.json'


def pick(name, own):
    """One file of a row's set: Red Alert's version of it where one is baked (own: the row
    is a version), else OpenCNC3D's."""
    if own and (VERSIONS / name).is_file():
        return VERSIONS / name
    return SOURCE / name


def files_for(row, own=False, winter_ok=lambda td_key, ra_key: True):
    """The files a reuse row (or, own, a version row) stands for, or (None, why). A summer
    row takes its winter look with it only where winter_ok says the look is approved."""
    cat, _, code = row.partition('/')
    if cat == 'civil':
        if not CIVIL.match(code):
            return None, 'not a civilian type the Red Alert 3D pass places'
        files = [SOURCE / f'{code}.ebld']
        win = SOURCE / f'{code}_WIN.ebld'
        if not code.endswith('_WIN') and win.is_file() and winter_ok(f'{code}_WIN',
                                                                     f'civil/{code}_WIN'):
            files.append(win)          # (its winter look, drawn on snow)
        return files, None
    if cat == 'foliage':
        m = re.match(r'^(T\d\d|TC0[1-5])(_WIN)?$', code)
        if not m:
            return None, 'not a tree or a tree clump'
        looks = ('WIN',) if m.group(2) else ('TEM', 'WIN')
        files = []
        for th in looks:
            main = pick(f'{m.group(1)}_{th}.ebld', own)
            if th == 'WIN' and not main.is_file() and not m.group(2):
                continue               # (no winter look yet: the 1996 tree on snow)
            if th == 'WIN' and not m.group(2) and not winter_ok(f'foliage/{m.group(1)}_WIN',
                                                                  f'foliage/{m.group(1)}_WIN'):
                continue               # (its winter look still in review: the 1996 tree on snow)
            stages = [pick(f'{m.group(1)}_{th}_{s}.ebld', own) for s in ('BURN', 'SNAG')]
            files += [main] + [p for p in stages if p.is_file()]
        return files, None
    return None, f'the {cat} category is not drawn from .ebld files'


def stage_spec(src, dst):
    """A model's specular maps (<stem>.spec, tools/art/remaster/bake_spec.py) beside its staged
    file: copied when the source has them, removed when it does not."""
    s, d = Path(src).with_suffix('.spec'), Path(dst).with_suffix('.spec')
    if s.is_file():
        shutil.copy2(s, d)
    elif d.is_file():
        d.unlink()


def stage(module, quiet=False, include_review=False):
    module = Path(module)
    target = module / 'ebld'
    target.mkdir(parents=True, exist_ok=True)
    ra = json.loads(REVIEWS_RA.read_text())
    td = json.loads(REVIEWS_TD.read_text())
    keep = {s['type']: s.get('keep', '') for s in json.loads(STRUCTURES.read_text())}
    staged, left, other = {}, [], {}
    rows, versions = {}, set()
    held = []

    def winter_ok(td_key, ra_key):
        """A winter look goes with its summer file once its row is approved in OpenCNC3D's
        review file and, where the Red Alert lab has a row of its own for it, there too;
        --include-review stages one still in review (a test folder's look)."""
        ok = (td.get(td_key) or {}).get('state') == 'approved' and \
            (ra_key not in ra or (ra.get(ra_key) or {}).get('state') == 'approved')
        if not ok and not include_review:
            held.append(td_key)
        return ok or include_review

    def version_winter_ok(td_key, ra_key):
        """A Red Alert version's winter look is a row of the Red Alert lab's own (its own
        file, or OpenCNC3D's where the version leaves the winter look unchanged), so it
        goes once that row is approved, whatever OpenCNC3D's row of the type says."""
        ok = (ra.get(ra_key) or {}).get('state') == 'approved'
        if not ok and not include_review:
            held.append(ra_key)
        return ok or include_review
    for row in sorted(ra):
        r = ra[row]
        if not isinstance(r, dict):
            continue
        if r.get('phase') == VERSION_PHASE and (r.get('state') == 'approved' or include_review):
            rows[row] = row            # (the OpenCNC3D tree it adapts has its name)
            versions.add(row)
            continue
        if r.get('state') != 'approved':
            continue
        if r.get('phase') == 'reuse':
            rows[row] = r.get('reuse_of', '')
        elif row in WALL_ROWS and 'reuse' in keep.get(row, ''):
            rows[row] = row
    for row in sorted(rows):
        own = row in versions
        if own and not row.startswith('foliage/'):
            left.append(f'{row} (a Red Alert version of a kind this does not stage yet)')
            continue
        files, why = (([SOURCE / f'{row}.ebld'], None) if row in WALL_ROWS else files_for(row, own, version_winter_ok if own else winter_ok))
        if files is None:
            cat = row.partition('/')[0]
            if why.startswith('the '):
                other[cat] = other.get(cat, 0) + 1
            else:
                left.append(f'{row} ({why})')
            continue
        of = rows[row]
        if td.get(of, {}).get('state') != 'approved':
            left.append(f'{row} (OpenCNC3D\'s {of} is not approved)')
            continue
        missing = [p.name for p in files if not p.is_file()]
        if missing:
            raise RuntimeError(f'{row}: reuse of {of} but game/ebld has no {", ".join(missing)}')
        if own and '_' not in row.partition('/')[2] and files[0].parent != VERSIONS:
            raise RuntimeError(f'{row}: approved as Red Alert\'s version of {of}, but '
                               f'game/ebld_ra_versions has no {files[0].name} '
                               '(tools/art/remaster/ra/bake_versions.py)')
        staged[row] = {'version_of': of, 'files': {}, 'own': []} if own else {'reuse_of': of, 'files': {}}
        for p in files:
            data = p.read_bytes()
            (target / p.name).write_bytes(data)
            stage_spec(p, target / p.name)
            staged[row]['files'][p.name] = hashlib.sha256(data).hexdigest()
            if p.parent == VERSIONS:
                staged[row]['own'].append(p.name)
    # what an earlier run staged and this one did not: its approval was withdrawn
    old = target / MANIFEST
    now = {n for v in staged.values() for n in v['files']}
    if old.is_file():
        for v in json.loads(old.read_text()).get('rows', {}).values():
            for name in v.get('files', {}):
                if name not in now and (target / name).is_file():
                    (target / name).unlink()
                    if (target / name).with_suffix('.spec').is_file():
                        (target / name).with_suffix('.spec').unlink()
                    print(f'RA reuse: removed {name}, its row is no longer approved')
    old.write_text(json.dumps({'what': 'OpenCNC3D remasters reused by Red Alert 3D, and '
                                       'Red Alert\'s own versions of them',
                               'source': 'game/ebld', 'versions': 'game/ebld_ra_versions',
                               'rows': staged,
                               'left_out': left, 'winter_looks_in_review': sorted(set(held)),
                               'include_review': include_review,
                               'other_categories': other}, indent=1) + '\n')
    if not quiet:
        n = sum(len(v['files']) for v in staged.values())
        nv = sum(1 for v in staged.values() if 'version_of' in v)
        print(f'RA reuse staged: {len(staged)} approved rows ({nv} of them Red Alert\'s own '
              f'versions{", those and the winter looks in review included" if include_review else ""}), '
              f'{n} files in {target}')
        for l in left:
            print(f'   left out: {l}')
        if held:
            print(f'   winter looks in review, the 1996 picture on snow: {", ".join(sorted(set(held)))}')
        if other:
            print('   not .ebld art (staged on their own): '
                  + ', '.join(f'{v} {k}' for k, v in sorted(other.items())))
    return staged


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('module', type=Path)
    ap.add_argument('--include-review', action='store_true',
                    help="also stage Red Alert's own versions and the winter looks still in "
                         "review (a test folder's look; a build stages approved ones only)")
    a = ap.parse_args()
    try:
        stage(a.module, include_review=a.include_review)
    except RuntimeError as e:
        sys.exit(f'stage-reuse: {e}')
