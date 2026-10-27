#!/usr/bin/env python3
"""Native exact wind-input cache tests without building or running the game.

Install with wind-unique-cache-fixture.cpp.in in tools/perf. The default source is
<repository>/game/cnc_eyes.cpp; --source accepts a private production candidate.
Supports Clang/GCC/MSVC. --arch arm64 --arch x86_64 selects both macOS targets.
The fixture mocks pack/frame inputs and exercises source-key invalidation, cache
capacity/eviction and bad_alloc fallback. It does not validate real mutation
coverage, draw eligibility or GL uploads. Those require separate verification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

TOKENS = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')


def function(source, name):
    match = re.search(r'static\s+\w+\s+' + re.escape(name) + r'\s*\([^)]*\)\s*\{', source)
    if not match:
        raise ValueError('Missing production function: ' + name)
    depth = 0
    for token in TOKENS.finditer(source, match.end() - 1):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                return source[match.start():token.end()]
    raise ValueError('Unterminated production function: ' + name)


def sha(value):
    return hashlib.sha256(value.encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--cxx', default=os.environ.get('CXX'))
    parser.add_argument('--arch', action='append', choices=('arm64', 'x86_64'), default=[])
    parser.add_argument('--artifacts', type=Path, help='Fresh directory for fixture, logs and source-bound receipt')
    args = parser.parse_args()
    source_path = (args.source or args.repo / 'game/cnc_eyes.cpp').resolve()
    source = source_path.read_text()
    if any(marker in source for marker in (
            'OPENCNC3D_WIND_UNIQUE_BENCH', 'OPENCNC3D_MESH_WIND_UNIQUE_BENCH')):
        raise ValueError('Use the production source, not the private benchmark variant')
    reference = function(source, 'mesh_wind_direct_positions')
    begin = source.index('/* Exact immutable wind-input preparation. */')
    dump = function(source, 'mesh_wind_unique_dump')
    end = source.index(dump, begin) + len(dump)
    production = source[begin:end]
    for name in ('mesh_wind_unique_hash', 'mesh_wind_unique_prepare',
                 'mesh_wind_unique_positions', 'mesh_wind_unique_enabled',
                 'mesh_wind_unique_verify_enabled', 'mesh_wind_unique_clear',
                 'mesh_wind_unique_evict', 'mesh_wind_positions_prepared'):
        function(production, name)
    if 'static const MeshWindUniquePrepared* mesh_wind_unique_source(' not in production:
        raise ValueError('Missing production source-key lookup')
    template_path = Path(__file__).with_name('wind-unique-cache-fixture.cpp.in')
    template = template_path.read_text()
    if template.count('@PRODUCTION_CODE@') != 1:
        raise ValueError('Fixture template must contain one production-code marker')
    fixture = template.replace('@PRODUCTION_CODE@', reference + '\n\n' + production)
    compiler = shlex.split(args.cxx) if args.cxx else [next((name for name in ('clang++', 'g++', 'cl') if shutil.which(name)), '')]
    if not compiler or not compiler[0]:
        raise RuntimeError('No C++ compiler found; supply --cxx')
    msvc = Path(compiler[0]).name.lower() in ('cl', 'cl.exe')
    if args.arch and (sys.platform != 'darwin' or msvc):
        raise ValueError('--arch is only supported for macOS Clang targets')
    temporary = None
    if args.artifacts:
        folder = args.artifacts.resolve()
        folder.mkdir(parents=True, exist_ok=False)
    else:
        temporary = tempfile.TemporaryDirectory(prefix='cnc3d-wind-unique-cache-')
        folder = Path(temporary.name)
    report = {'passed': False, 'source': str(source_path), 'source_sha256': sha(source),
              'reference_function_sha256': sha(reference), 'cache_block_sha256': sha(production), 'template_sha256': sha(template),
              'fixture_sha256': sha(fixture), 'architectures': [],
              'limitations': 'Mock pack/frame inputs; runtime mutations, draw eligibility and GL uploads require separate verification.'}
    try:
        fixture_path = folder / 'wind-unique-cache-test.cpp'
        fixture_path.write_text(fixture)
        for arch in list(dict.fromkeys(args.arch)) or ['native']:
            executable = folder / ('wind-unique-cache-test-' + arch + ('.exe' if os.name == 'nt' else ''))
            if msvc:
                command = compiler + ['/nologo', '/std:c++14', '/O2', '/EHsc', '/W4', str(fixture_path), '/Fe:' + str(executable)]
            else:
                command = compiler + (['-arch', arch] if arch != 'native' else [])
                command += ['-std=c++14', '-O2', '-Wall', '-Wextra', str(fixture_path), '-o', str(executable)]
            row = {'arch': arch, 'compiler': command, 'runs': []}
            report['architectures'].append(row)
            result = subprocess.run(command, cwd=folder, text=True, capture_output=True, timeout=120)
            (folder / (arch + '-compile.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError('Native compilation failed: ' + result.stdout + result.stderr)
            row['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
            for verify in (False, True):
                environment = dict(os.environ)
                environment['OPENCNC3D_MESH_WIND_UNIQUE_VERIFY'] = '1' if verify else '0'
                result = subprocess.run([str(executable)], cwd=folder, env=environment,
                                        text=True, capture_output=True, timeout=120)
                label = arch + ('-verify' if verify else '-normal')
                (folder / (label + '.log')).write_text(result.stdout + result.stderr)
                if result.returncode:
                    raise RuntimeError('Native fixture failed: ' + result.stdout + result.stderr)
                outputs = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
                if len(outputs) != 1 or not outputs[0]['passed'] or outputs[0]['verify'] != int(verify):
                    raise RuntimeError('Missing native test proof')
                if (outputs[0]['mismatches'] != int(verify)
                        or outputs[0]['expected_negative_controls'] != int(verify)
                        or outputs[0]['allocation_throws'] < 5):
                    raise RuntimeError('Unexpected mismatch or missing allocation-failure proof')
                row['runs'].append(outputs[0])
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (folder / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
        if temporary:
            temporary.cleanup()
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
