#!/usr/bin/env python3
"""Stage Red Alert's props (game/ebld_ra_props, baked by tools/ra3d/bake-props.py) into a module.

Usage: tools/ra3d/stage-props.py MODULE      (MODULE is a staged ra3d/ folder)

Copies each prop's .ebld into MODULE/ebld when its row in tools/art/remaster/reviews-ra.json
is still approved and the committed file was baked under that approval (the manifest's
`approval.remaster_approved` is the row's): a row sent back to review after its bake, or
remodelled and approved again without a rebake, is left out and named, so a build carries
approved art only and never an approval the file does not stand for. A file staged by an
earlier run whose row no longer qualifies is removed. The fence's placement (FENC.eplc) is
not copied here: tools/ra3d/stage-structures.py stages placements, and takes it from
game/ebld_ra_props beside the structures' when the fence's file is staged.

Called by tools/ra3d/stage-structures.py, which both packagers run.
"""
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'game/ebld_ra_props'
REVIEWS = ROOT / 'tools/art/remaster/reviews-ra.json'
MANIFEST = 'props-manifest.json'


def stage(module, quiet=False):
    target = Path(module) / 'ebld'
    target.mkdir(parents=True, exist_ok=True)
    mpath = SOURCE / 'manifest.json'
    models = json.loads(mpath.read_text()).get('models', {}) if mpath.is_file() else {}
    reviews = json.loads(REVIEWS.read_text())
    staged, left = {}, []
    for code, info in sorted(models.items()):
        row = reviews.get(f'civil/{code}') or {}
        at = (info.get('approval') or {}).get('remaster_approved')
        src = SOURCE / info['file']
        if row.get('state') != 'approved' or not row.get('remaster_approved'):
            left.append(f'{code}: its row is not approved now')
        elif at != row.get('remaster_approved'):
            left.append(f'{code}: baked under the approval of {at}, the row says '
                        f'{row.get("remaster_approved")}: rebake it (tools/ra3d/bake-props.py)')
        elif not src.is_file():
            left.append(f'{code}: {src.name} is missing')
        else:
            shutil.copy2(src, target / src.name)
            # (its specular maps, tools/art/remaster/bake_spec.py --game ra, when it has them)
            spec, dspec = src.with_suffix('.spec'), (target / src.name).with_suffix('.spec')
            if spec.is_file():
                shutil.copy2(spec, dspec)
            elif dspec.is_file():
                dspec.unlink()
            staged[code] = dict(file=src.name, sha256=info.get('sha256'), kind=info.get('kind'),
                                stages=info.get('stages'))
    old = target / MANIFEST
    if old.is_file():
        for code, info in json.loads(old.read_text()).get('models', {}).items():
            if code not in staged and (target / info['file']).is_file():
                (target / info['file']).unlink()
                if (target / info['file']).with_suffix('.spec').is_file():
                    (target / info['file']).with_suffix('.spec').unlink()
    old.write_text(json.dumps(dict(
        what="Red Alert's props staged into this module by tools/ra3d/stage-props.py",
        models=staged), indent=1) + '\n')
    if not quiet:
        print(f'RA props staged: {len(staged)} in {target}'
              + (f' ({", ".join(staged)})' if staged else ''))
        for l in left:
            print(f'   left out: {l}')
    return staged


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    stage(sys.argv[1])
