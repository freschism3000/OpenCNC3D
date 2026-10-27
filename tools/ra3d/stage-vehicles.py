#!/usr/bin/env python3
"""Stage the remastered vehicles and aircraft Red Alert 3D draws into a module.

Usage: tools/ra3d/stage-vehicles.py MODULE      (MODULE is a staged ra3d/ folder)

ONE FILE PER RED ALERT TYPE. Under Enhanced the renderer draws a Red Alert unit or aircraft
from MODULE/evhc/<TYPE>.evhc, named by the Red Alert type the engine reports (1TNK, TRAN),
and keeps the 1996 sprite card for every type without one (game/evhc_mod.h). The files are
the same EVHC files OpenCNC3D's game draws (tools/art/remaster/bake_evhc.py), from two
places:

  game/evhc_ra/<TYPE>.evhc   a Red Alert vehicle's own remaster, baked under its own name;
                             staged as it is once its row vehicles/<TYPE> in reviews-ra.json
                             is approved (--include-review stages one still in review, for
                             a look in a test folder).
  game/evhc/<TD>.evhc        an OpenCNC3D remaster the Red Alert lab approved as reuse: its
                             row vehicles/<TYPE> in reviews-ra.json in phase "reuse", state
                             "approved", reuse_of naming the OpenCNC3D type, whose own row
                             in reviews.json must be approved too. The file is copied as
                             <TYPE>.evhc with the type in its header renamed (eight ASCII
                             bytes at offset 8, the only place a file names its type), so
                             the model draws for the Red Alert type and a file is never
                             loaded for a type it was not staged for. Nothing else in the
                             file changes.

An own remaster wins over a reuse of the same type. A reuse row whose OpenCNC3D original is
not approved, or whose file is missing, is left out and named. Files an earlier run staged
whose row is gone are removed again (only names this script wrote, read back from the
manifest it leaves).

Writes MODULE/evhc/manifest.json: every type staged, where from, and the SHA-256 of the
source file and of the staged one.
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
TD_FILES = ROOT / 'game/evhc'
RA_FILES = ROOT / 'game/evhc_ra'
MANIFEST = 'manifest.json'
MAGIC = b'EVHC'
TYPE_AT, TYPE_LEN = 8, 8
TYPE_NAME = re.compile(r'^[A-Z0-9]{2,8}$')


def renamed(data, code):
    """The file's bytes with its header's type set to `code`."""
    if data[:4] != MAGIC:
        raise RuntimeError('not an EVHC file')
    name = code.encode('ascii').ljust(TYPE_LEN, b'\0')
    return data[:TYPE_AT] + name + data[TYPE_AT + TYPE_LEN:]


def header_type(data):
    return data[TYPE_AT:TYPE_AT + TYPE_LEN].rstrip(b'\0').decode('ascii', 'replace')


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
    target = module / 'evhc'
    target.mkdir(parents=True, exist_ok=True)
    ra = json.loads(REVIEWS_RA.read_text())
    td = json.loads(REVIEWS_TD.read_text())
    staged, left = {}, []
    # a Red Alert vehicle's own remaster, under its own name, once its lab row is approved
    for p in sorted(RA_FILES.glob('*.evhc')) if RA_FILES.is_dir() else []:
        data = p.read_bytes()
        if data[:4] != MAGIC or header_type(data) != p.stem:
            left.append(f'{p.name} (its header names {header_type(data)!r}, not {p.stem})')
            continue
        state = (ra.get(f'vehicles/{p.stem}') or {}).get('state')
        if state != 'approved' and not include_review:
            left.append(f'{p.name} (the lab\'s vehicles/{p.stem} is '
                        f'{"in " + state if state else "missing"}, not approved; '
                        '--include-review stages it for a look)')
            continue
        (target / p.name).write_bytes(data)
        stage_spec(p, target / p.name)
        h = hashlib.sha256(data).hexdigest()
        staged[p.stem] = {'from': f'game/evhc_ra/{p.name}', 'own': True,
                          'source_sha256': h, 'sha256': h}
    # OpenCNC3D's remasters the Red Alert lab approved as reuse
    for row in sorted(ra):
        r = ra[row]
        if not row.startswith('vehicles/') or not isinstance(r, dict):
            continue
        if r.get('state') != 'approved' or r.get('phase') != 'reuse':
            continue
        code, of = row.partition('/')[2], r.get('reuse_of', '')
        if not TYPE_NAME.match(code) or not TYPE_NAME.match(of or ''):
            left.append(f'{row} (reuse_of {of!r} is not a type name)')
            continue
        if code in staged:
            left.append(f'{row} (Red Alert\'s own remaster of {code} is staged instead)')
            continue
        if td.get(of, {}).get('state') != 'approved':
            left.append(f'{row} (OpenCNC3D\'s {of} is not approved)')
            continue
        src = TD_FILES / f'{of}.evhc'
        if not src.is_file():
            raise RuntimeError(f'{row}: reuse of {of} but game/evhc has no {src.name}')
        data = src.read_bytes()
        if header_type(data) != of:
            raise RuntimeError(f'{src.name} names {header_type(data)!r} in its header')
        out = renamed(data, code)
        (target / f'{code}.evhc').write_bytes(out)
        stage_spec(src, target / f'{code}.evhc')
        staged[code] = {'from': f'game/evhc/{src.name}', 'reuse_of': of,
                        'source_sha256': hashlib.sha256(data).hexdigest(),
                        'sha256': hashlib.sha256(out).hexdigest()}
    # what an earlier run staged and this one did not: its row was withdrawn
    old = target / MANIFEST
    if old.is_file():
        for code in json.loads(old.read_text()).get('types', {}):
            p = target / f'{code}.evhc'
            if code not in staged and p.is_file():
                p.unlink()
                if p.with_suffix('.spec').is_file():
                    p.with_suffix('.spec').unlink()
                print(f'RA vehicles: removed {p.name}, its row is no longer approved')
    old.write_text(json.dumps({'what': 'the remastered vehicles and aircraft Red Alert 3D draws',
                               'types': staged, 'left_out': left}, indent=1) + '\n')
    if not quiet:
        reuse = sorted(k for k, v in staged.items() if not v.get('own'))
        own = sorted(k for k, v in staged.items() if v.get('own'))
        print(f'RA vehicles staged: {len(staged)} in {target}'
              + (f'; reused: {", ".join(f"{k} ({staged[k]["reuse_of"]})" for k in reuse)}' if reuse else '')
              + (f'; own: {", ".join(own)}' if own else ''))
        for l in left:
            print(f'   left out: {l}')
    return staged


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('module', type=Path)
    ap.add_argument('--include-review', action='store_true',
                    help='also stage a Red Alert remaster still in review in the lab (a look in '
                         'a test folder; a build stages approved ones only)')
    a = ap.parse_args()
    try:
        stage(a.module, include_review=a.include_review)
    except RuntimeError as e:
        sys.exit(f'stage-vehicles: {e}')
