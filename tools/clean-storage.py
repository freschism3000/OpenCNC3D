#!/usr/bin/env python3
"""Run the three guarded retention helpers, recording outcomes and disk space."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from storage_cleanup import OUTPUT, atomic_json, real_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    output = real_path(args.output)
    folder = output / 'storage-runs' / (time.strftime('%Y%m%d-%H%M%S') + f'-{time.time_ns()}')
    folder.mkdir(parents=True)
    tools = Path(__file__).resolve().parent
    extra = ['--apply'] if args.apply else []
    commands = [
        ('temp', [sys.executable, str(tools/'clean-session-temp.py'), '--report', str(folder/'temp.json'), *extra]),
        ('film', [sys.executable, str(tools/'clean-showcase.py'), '--root', str(output/'vehicle-showcase'), *extra]),
        ('generated', [sys.executable, str(tools/'clean-generated.py'), '--registry', str(output/'storage-retention-builds.json'), '--report', str(folder/'generated.json'), *extra])]
    report = {'created': time.time(), 'mode': 'APPLY' if args.apply else 'DRY_RUN',
              'free_before_bytes': shutil.disk_usage(output).free, 'helpers': [],
              'reclaimed_or_shared_bytes': 0, 'failures': []}
    for name, command in commands:
        try:
            process = subprocess.run(command, capture_output=True, text=True, timeout=3300)
            entry = {'name': name, 'returncode': process.returncode,
                     'stdout': process.stdout.strip(), 'stderr': process.stderr.strip()}
            path = folder / (name + '.json')
            if name == 'film':
                paths = [line[len('Report: '):] for line in process.stdout.splitlines() if line.startswith('Report: ')]
                path = Path(paths[-1]) if paths else path
            if path.is_file():
                value = json.loads(path.read_text())
                entry['report'] = str(path)
                report['reclaimed_or_shared_bytes'] += value.get('freed_bytes', 0) + value.get('deduplicated_allocated_bytes', 0)
            if process.returncode:
                report['failures'].append(name)
            report['helpers'].append(entry)
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            report['failures'].append(name)
            report['helpers'].append({'name': name, 'error': str(error)})
        atomic_json(folder/'summary.json', report)
    report['free_after_bytes'] = shutil.disk_usage(output).free
    report['low_space'] = report['free_after_bytes'] < 50_000_000_000
    previous = output/'storage-cleanup-latest-run.json'
    try:
        old = json.loads(previous.read_text())
    except (OSError, ValueError):
        old = {}
    report['new_low_space_warning'] = report['low_space'] and not old.get('low_space', False)
    report['finished'] = time.time()
    atomic_json(folder/'summary.json', report)
    atomic_json(previous, report)
    print(json.dumps({'report': str(folder/'summary.json'),
                      'reclaimed_or_shared_bytes': report['reclaimed_or_shared_bytes'],
                      'free_disk_bytes': report['free_after_bytes'],
                      'failures': report['failures'], 'new_low_space_warning': report['new_low_space_warning']}))
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
