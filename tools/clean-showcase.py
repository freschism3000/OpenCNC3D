#!/usr/bin/env python3
"""Prune declared retired PNG/PPM frame sequences after verified video delivery.

Keeps authoring inputs, useful QA sheets, final media and protected dependencies.
Never follows symlinks. Activity or verification uncertainty retains candidates.
"""
import argparse
import json
import os
from pathlib import Path
import re
import time
from storage_cleanup import Activity, atomic_json, child, cleanup_lock, real_path, sha, signature

VERSION = re.compile(r'(?:v\d+-production|(?:mammoth|harvester)-v\d+)')
SEQUENCE = {'frames','exposure','left','right','preview','probe','trace-images','capture','native-frames','shutter','raw','shots'}
ASSETS = {'art','source-tools','asset-snapshots','runtime-source','source-before','music','inputs'}


def verify_receipt(root, version):
    if not VERSION.fullmatch(version):
        raise ValueError('invalid film version')
    receipt_path = child(root, version + '/delivery-receipt.json')
    before = signature(receipt_path)
    receipt = json.loads(receipt_path.read_text())
    if receipt.get('verified') is not True or not receipt.get('exports'):
        raise ValueError('hash-verified delivered exports required')
    if receipt.get('raw_frames_required_by_newer_versions') is True:
        raise ValueError('newer film still requires these frames')
    proofs = [(receipt_path, before)]
    for row in receipt['exports']:
        p = real_path(Path(row['desktop_path']))
        if not p.is_file():
            raise ValueError('delivered export missing')
        before_export = signature(p)
        if sha(p) != row['sha256'] or signature(p) != before_export:
            raise ValueError('delivered video hash mismatch or concurrent write')
        proofs.append((p, before_export))
    if signature(receipt_path) != before:
        raise ValueError('delivery receipt changed')
    return proofs


def candidates(root, state):
    protected = [child(root, p) for p in state['protected_frame_roots']]
    for version in state['retired_versions']:
        if not VERSION.fullmatch(version):
            raise ValueError('invalid retired version')
        base = root / version
        if base.is_symlink() or not base.is_dir():
            continue
        for folder, dirs, files in os.walk(base, followlinks=False):
            d = Path(folder)
            rel = d.relative_to(base)
            dirs[:] = [n for n in dirs if n not in ASSETS and not (d/n).is_symlink()
                       and not any((d/n).is_relative_to(q) for q in protected)]
            if any(d.is_relative_to(q) for q in protected):
                continue
            sequence = bool(set(rel.parts) & SEQUENCE)
            review = 'review' in rel.parts or any(n.startswith('native-suspension') for n in rel.parts)
            for name in files:
                p = d / name
                if p.is_symlink() or not p.is_file() or re.search(r'contact|sheet|styleboard', name, re.I):
                    continue
                png = p.suffix.lower() == '.png'
                ppm = p.suffix.lower() == '.ppm' and re.fullmatch(r'(?:f|frame[-_]?)?\d+\.ppm', name, re.I)
                if not ((sequence and (png or ppm)) or
                        (review and png and re.fullmatch(r'(?:frame|decoded|on|off)[-_]\d+\.png', name, re.I))):
                    continue
                yield {'path': str(p.relative_to(root)), 'version': version,
                       'bytes': p.stat().st_size, 'signature': signature(p)}


def execute(args):
    root = real_path(args.root)
    statepath = child(root, 'storage-retention.json')
    state = json.loads(statepath.read_text())
    if args.retire:
        verify_receipt(root, args.retire)
        if any(p == args.retire or p.startswith(args.retire + '/') for p in state['protected_frame_roots']):
            raise ValueError('newer production still reads this version')
        if args.retire not in state['retired_versions']:
            state['retired_versions'].append(args.retire)
        if args.apply:
            atomic_json(statepath, state)
    state_signature = signature(statepath)
    rows = list(candidates(root, state))
    report = {'created': time.time(), 'mode': 'APPLY' if args.apply else 'DRY_RUN',
              'files': rows, 'bytes': sum(r['bytes'] for r in rows),
              'protected_frame_roots': state['protected_frame_roots'],
              'removed_files': 0, 'freed_bytes': 0, 'kept': [], 'errors': []}
    reports = root/'storage-reports'
    reports.mkdir(exist_ok=True)
    dest = reports / (time.strftime('%Y%m%d-%H%M%S') + f'-{time.time_ns()}-' + ('applied' if args.apply else 'plan') + '.json')
    try:
        if rows:
            activity = Activity()
            refreshed = time.time()
            for version in sorted({r['version'] for r in rows}):
                if activity.busy(root/version):
                    report['kept'].append({'version': version, 'reason': 'active process reference'})
                    continue
                try:
                    proofs = verify_receipt(root, version)
                except (OSError, ValueError, KeyError) as error:
                    report['errors'].append({'version': version, 'reason': str(error)})
                    continue
                if args.apply:
                    for row in (r for r in rows if r['version'] == version):
                        if time.time() - refreshed > 30:
                            activity = Activity()
                            refreshed = time.time()
                        if activity.busy(root/version):
                            report['kept'].append({'version': version, 'reason': 'became active'})
                            break
                        if signature(statepath) != state_signature or any(signature(p) != s for p, s in proofs):
                            raise RuntimeError('retention or delivery proof changed; remaining frames retained')
                        p = child(root, row['path'])
                        if signature(p) != row['signature']:
                            raise RuntimeError('frame changed after plan; remaining frames retained')
                        p.unlink()
                        report['removed_files'] += 1
                        report['freed_bytes'] += row['bytes']
    except (OSError, ValueError, RuntimeError) as error:
        report['errors'].append({'reason': str(error)})
    finally:
        atomic_json(dest, report)
    print(f"Retired frames: {report['removed_files']} PNG/PPM files removed; {len(report['errors'])} safety failures")
    print('Report:', dest)
    return bool(report['errors'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--retire')
    args = parser.parse_args()
    try:
        with cleanup_lock():
            return execute(args)
    except BlockingIOError:
        print('KEEP frames: another cleanup is running')
        return 0
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f'KEEP frames: {error}')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
