#!/usr/bin/env python3
"""Deduplicate only registered, completed, superseded generated outputs.

Never removes unique files or whole build folders. Keeps every archive path using
independent APFS clones of byte-identical retained outputs. Dry run is default.
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import shutil
import time

from storage_cleanup import (Activity, OUTPUT, atomic_json, child, cleanup_lock,
                             independent_clone, real_path, sha, signature)

FORMATS = {'.o', '.obj', '.gch', '.pch', '.dylib', '.exe', '.bin.gz'}
PACKAGES = {'.uasset', '.uexp', '.ubulk'}


def receipt(root, spec):
    path = child(root, spec['path'])
    before = signature(path)
    value = json.loads(path.read_text())
    expected = spec['equals']
    if not expected or not all(k in value and value[k] == v for k, v in expected.items()):
        raise ValueError(f'completion receipt does not verify success: {path}')
    if signature(path) != before:
        raise ValueError(f'completion receipt changed: {path}')
    return path, before


def pairs(group):
    root = real_path(Path(group['root']).expanduser())
    if not root.is_dir():
        return
    hours = float(group.get('min_idle_hours', 24))
    if hours < 24:
        raise ValueError('automatic generated-output cleanup requires at least 24 idle hours')
    if group['kind'] == 'native-cook':
        runs = []
        for run in root.iterdir():
            if run.is_symlink() or not run.is_dir() or not re.fullmatch(r'\d{8}-\d{6}-[0-9a-f]{8}', run.name):
                continue
            try:
                proof = receipt(run, {'path': 'result.json', 'equals': {
                    'pass': True, 'status': 'passed', 'production_inputs_unchanged': True}})
                finished = json.loads(proof[0].read_text())['finished_utc']
                stamp = datetime.fromisoformat(finished).timestamp()
                payload = child(run, 'Cooked')
                if payload.is_dir():
                    runs.append((stamp, payload, proof))
            except (OSError, ValueError, KeyError):
                continue  # Failed/incomplete/unknown runs remain untouched.
        runs.sort(key=lambda row: row[0])
        if len(runs) >= 2:
            _, newest, newest_proof = runs[-1]
            for _, old, proof in runs[:-1]:
                yield old, newest, {'.uasset', '.uexp', '.ubulk', '.ufont', '.ushaderbytecode'}, hours, [proof, newest_proof]
    elif group['kind'] == 'verified-pairs':
        if not group.get('receipts'):
            raise ValueError('explicit completed-build receipts required')
        proofs = [receipt(root, row) for row in group['receipts']]
        for row in group['pairs']:
            old, new = child(root, row['older']), child(root, row['newer'])
            formats = set(row['formats'])
            if not formats or not formats <= FORMATS:
                raise ValueError('only supported generated binary/capture formats may be registered')
            if old == new or old.is_relative_to(new) or new.is_relative_to(old):
                raise ValueError('overlapping output roots retained')
            if old.is_dir() and new.is_dir():
                yield old, new, formats, hours, proofs
    else:
        raise ValueError('unknown retention group kind')


def inventory(root):
    rows = {}
    for folder, dirs, files in os.walk(root, followlinks=False):
        d = Path(folder)
        if '.git' in dirs or '.git' in files:
            raise ValueError(f'Git checkout inside output; retained: {root}')
        dirs[:] = [n for n in dirs if not (d / n).is_symlink()]
        for name in files:
            path = d / name
            if not path.is_symlink() and path.is_file():
                rows[str(path.relative_to(root))] = (path, signature(path))
    return rows


def families(files, formats):
    used = set()
    for name in sorted(files):
        path = Path(name)
        if path.suffix == '.uasset' and '.uasset' in formats:
            family = [str(path.with_suffix(s)) for s in sorted(PACKAGES)
                      if str(path.with_suffix(s)) in files]
            used.update(family)
            yield family
    for name in sorted(files):
        if name not in used and any(name.endswith(ext) for ext in formats) and Path(name).suffix not in PACKAGES:
            yield [name]


def run(registry, apply=False, cache_path=None):
    started = time.time()
    registry_signature = signature(registry)
    config = json.loads(real_path(registry).read_text())
    if config.get('schema') != 1:
        raise ValueError('unsupported generated-output registry')
    cache_path = real_path(cache_path or OUTPUT / '.storage-dedup-cache.json')
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    report = {'created': started, 'mode': 'APPLY' if apply else 'DRY_RUN',
              'files': [], 'kept': [], 'errors': [], 'cached_files': 0,
              'deduplicated_files': 0, 'deduplicated_allocated_bytes': 0,
              'free_before_bytes': shutil.disk_usage(registry.parent).free}
    activity = Activity()  # Failure here stops all mutations.
    refreshed = time.time()
    for group in config['groups']:
        try:
            for old, new, formats, hours, proofs in pairs(group):
                if activity.busy(old) or activity.busy(new):
                    report['kept'].append({'path': str(old), 'reason': 'active output or retained copy'})
                    continue
                before, retained = inventory(old), inventory(new)
                # Include ctime: a preserved old mtime does not make a new copy idle.
                latest = max((max(s[3], s[4]) / 1e9 for _, s in [*before.values(), *retained.values()]), default=started)
                if started - latest < hours * 3600:
                    report['kept'].append({'path': str(old), 'reason': f'written within {hours:g} hours'})
                    continue
                for family in families(before, formats):
                    verified = []
                    for name in family:
                        if name not in retained:
                            break
                        p, a = before[name]
                        q, b = retained[name]
                        if a[2] != b[2]:
                            break
                        key = json.dumps([str(p), str(q)])
                        saved = cache.get(key, {})
                        if saved.get('old') == a and saved.get('retained') == b:
                            verified.append((p, q, a, b, saved['sha256'], key, True))
                            continue
                        digest = sha(p)
                        if digest != sha(q):
                            break
                        if signature(p) != a or signature(q) != b:
                            raise RuntimeError('output changed during hashing; retained')
                        verified.append((p, q, a, b, digest, key, False))
                    else:
                        # Entire UE package must match, including its payload sidecars.
                        for p, q, a, b, digest, key, cached in verified:
                            if cached:
                                report['cached_files'] += 1
                                continue
                            row = {'path': str(p), 'retained_copy': str(q), 'sha256': digest,
                                   'allocated_bytes': p.stat().st_blocks * 512}
                            report['files'].append(row)
                            if apply:
                                if signature(registry) != registry_signature:
                                    raise RuntimeError('retention registry changed; remaining files retained')
                                if time.time() - refreshed > 30:
                                    activity = Activity()
                                    refreshed = time.time()
                                if activity.busy(old) or activity.busy(new):
                                    raise RuntimeError('output became active; remaining files retained')
                                if any(signature(path) != sig for path, sig in proofs):
                                    raise RuntimeError('completion proof changed; remaining files retained')
                                independent_clone(q, p, b, a, digest)
                                cache[key] = {'old': signature(p), 'retained': signature(q), 'sha256': digest}
                                report['deduplicated_files'] += 1
                                report['deduplicated_allocated_bytes'] += row['allocated_bytes']
                        continue
                    report['kept'].append({'path': str(old / family[0]), 'reason': 'unique, changed or unmatched package'})
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            report['errors'].append({'group': group['id'], 'reason': str(error)})
    if apply:
        atomic_json(cache_path, cache)
    report['free_after_bytes'] = shutil.disk_usage(registry.parent).free
    report['finished'] = time.time()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--registry', type=Path, default=OUTPUT / 'storage-retention-builds.json')
    parser.add_argument('--report', type=Path, default=OUTPUT / 'storage-cleanup-latest-generated.json')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    try:
        with cleanup_lock():
            report = run(args.registry, args.apply)
            atomic_json(args.report, report)
        print(f"Generated outputs: {report['deduplicated_files']} duplicates shared; {len(report['errors'])} safety failures")
        return bool(report['errors'])
    except BlockingIOError:
        print('KEEP generated outputs: another cleanup is running')
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        atomic_json(args.report, {'created': time.time(), 'errors': [str(error)], 'mutations': 'not assumed; inspect the run report'})
        print(f'KEEP generated outputs: {error}')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
