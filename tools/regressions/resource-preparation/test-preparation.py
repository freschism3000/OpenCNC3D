#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[3])
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--windows-headroom', action='store_true')
parser.add_argument('--nonwindows-headroom', action='store_true')
parser.add_argument('--compiler', default='clang++')
parser.add_argument('--cxxflag', action='append', default=[])
args = parser.parse_args()
if args.windows_headroom and args.nonwindows_headroom:
    parser.error('Choose only one headroom platform fixture')
args.repo = args.repo.resolve()
args.out = args.out.resolve()
args.out.mkdir(parents=True, exist_ok=False)
source = Path(__file__).with_name('preparation-test.cpp')
binary = args.out / ('test.exe' if os.name == 'nt' else 'test')
command = [args.compiler, '-std=c++14', '-O2', '-I' + str(args.repo / 'game'),
           str(source), '-o', str(binary)] + args.cxxflag
if args.windows_headroom:
    command.append('-DTEST_WINDOWS_HEADROOM=1')
if args.nonwindows_headroom:
    command.append('-DTEST_NONWINDOWS_HEADROOM=1')
subprocess.run(command, check=True, timeout=180)
process = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, cwd=args.out)
(args.out / 'run.log').write_text(process.stdout + process.stderr)
print(process.stdout, end='')
if process.returncode:
    raise RuntimeError(process.stderr + process.stdout)
match = re.search(r'PREPARATION\|checks=(\d+)\|failures=0', process.stdout)
if not match:
    raise RuntimeError('Missing preparation assertions')
result = {
    'passed': True, 'checks': int(match[1]), 'compiler_command': command,
    'Windows_headroom_API_is_mocked': args.windows_headroom,
    'nonWindows_headroom_path_is_forced': args.nonwindows_headroom,
    'actual_scheduler_header_sha256': hashlib.sha256((args.repo / 'game/ebld_prepare.h').read_bytes()).hexdigest(),
    'GL_SDL_and_resource_functions_are_stubs': True, 'no_game_or_UI': True,
    'coverage': ['actual-world and upcoming queued types only', 'Busy column exclusion',
                 'loading once per generation', 'caps and actual capacities', 'old generation cancellation',
                 'Classic/Enhanced roundtrip', 'allocation, driver and I/O rollback', 'demand retry',
                 'non-square mip bytes', 'current damage-stage priority and union', 'repair and persisted wreck',
                 'original ordering control', 'stage-priority memory-cap enforcement'],
}
result['coverage'].extend(['finite required current-stage census', 'required OFF and shot fallback',
                          'unchanged simulation counters', 'required/optional shared accounting',
                          'shroud construction authored-model and disabled-tree exclusions',
                          'persisted wreck orphan snag and shared-owner residency',
                          'visible walls and held/damaged/wreck shared wall owners',
                          'finite composed tree cells and per-cell visibility exclusions',
                          'first-seen lazy clump species and current-theater source admission',
                          'lazy family partial-load rollback and demand retry',
                          'missing malformed oversized and non-intact source and companion refusal',
                          'reserved model CPU ledger and unchanged engine clocks',
                          'required preparation remains TD-only',
                          'required transient/job/headroom/overflow admission',
                          'required allocation driver and I/O rollback', 'required generation reset'])
if args.nonwindows_headroom or os.name != 'nt':
    result['coverage'].append('non-Windows required work stays within original optional platform caps')
(args.out / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
