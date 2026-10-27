#!/usr/bin/env python3
"""Hidden paired wind regression for the shared TD/RA renderer.

Runs one candidate against its own old-path switch. Saves every invocation, decoded
image/float-target comparison, timing, and counter. Never changes runtime assets or
settings. The default three arms include the original CPU arithmetic verifier.
The default matrix suite is unchanged; --optimization shadow tests opaque sun-depth
positions, and --td-skirmish selects the first TD skirmish with completed AI bases.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

from PIL import Image

TARGETS = ('scene', 'depth', 'normals', 'sun', 'terrain-sun', 'infantry-sun')
ARMS = ('off', 'on', 'verify', 'allocation-failure', 'upload-failure')
STATE_TAGS = {'OBJ', 'HOUSE', 'VIEW', 'TICK', 'TICKALPHA', 'PHYSICSCAPTURE',
              'BUILDINGPHYSICSCAPTURE', 'EBLDART', 'EBLDBODY', 'EBLDCLIP',
              'EBLDEMIT', 'EBLDLIGHT', 'EBLDMESH', 'EBLDMOVE', 'EBLDNRM',
              'EBLDOBJ', 'EBLDPOSE', 'EBLDWAVE', 'ETREE', 'ETREEDUMP',
              'ETREESTAGE', 'ETERR', 'ETERRAT', 'ETERRDUMP'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read_cfg(path):
    values = {}
    for line in path.read_text().splitlines():
        fields = line.split('#', 1)[0].split()
        if len(fields) >= 2:
            values[fields[0]] = fields[1]
    return values


def make_script(case, folder, cfg, args):
    # RA must reach the first deployed building: an empty lazy-model cache in the
    # existing renderer otherwise leaves all tree models as sprites at tick five.
    ticks = 400 if case == 'td' and args.td_skirmish else 160
    lines = ['gfxmode 2', 'gfx show_stats 0', f'tick {ticks}', 'reveal 1', 'tickalpha 0.25',
             f'cursor {args.width - 5} {args.height - 5}']
    phase_names = []

    def capture(name, commands):
        phase_names.append(name)
        lines.extend(commands)
        lines.extend([f'echo TREECOMPARE|begin|{name}', 'frames 2',
                      f'frameperf {args.warmup}', f'framebench {args.samples}',
                      'shot ' + (folder / (name + '.png')).as_posix()])
        if not args.no_raw_targets:
            lines.append('renderdump ' + (folder / name).as_posix())
        lines.extend(['objdump', 'ebldump', 'eterrdump', 'physicsdump', 'meshperfdump'])
        if case == 'ra':
            lines.append('ra3dprobe')
        lines.append(f'echo TREECOMPARE|end|{name}')

    views = [(32, 43, 4200), (27, 36, 3000), (80, 55, 3000)] if case == 'ra' else [
        (11, 29, 3000), (31, 41, 4200), (11, 29, 1800)]
    if case == 'td' and args.td_skirmish:
        views = [(44, 36, 4200), (42, 34, 3000), (44, 36, 1800)]
    for index, (x, z, dist) in enumerate(views):
        commands = [f'cam {x} {z}', f'dist {dist}']
        if index:
            commands.extend(['tick 15', 'tickalpha 0.75' if index == 1 else 'tickalpha 0.25'])
        capture('view-' + str(index), commands)
    # Non-uniform transforms in both modules exercise attributes, normal/cutout
    # output, matrix-key invalidation, and returning to the original transform.
    x, z, dist = views[1]
    capture('scale-a', [f'cam {x} {z}', f'dist {dist}', 'gfx etree_height 1.1',
                        'gfx etree_width 0.9', 'gfx etree_vary 0.17'])
    capture('scale-b', ['gfx etree_height 1.5', 'gfx etree_width 1.35', 'gfx etree_vary 0.24'])
    capture('scale-a-return', ['gfx etree_height 1.1', 'gfx etree_width 0.9', 'gfx etree_vary 0.17'])
    capture('authored-return', ['gfx ' + key + ' ' + cfg[key]
                                for key in ('etree_height', 'etree_width', 'etree_vary')])
    capture('fog', ['reveal 0', 'shrouddarktest'])
    capture('reveal-return', ['reveal 1'])
    lines.append('quit')
    return '\n'.join(lines) + '\n', phase_names


def decoded(path):
    if path.suffix == '.png':
        with Image.open(path) as im:
            return im.size, im.convert('RGBA').tobytes()
    blob = gzip.decompress(path.read_bytes())
    require(len(blob) >= 12, 'Missing raw header: ' + str(path))
    w, h, kind = struct.unpack('<iii', blob[:12])
    require(w > 0 and h > 0 and kind in (1, 2), 'Invalid raw header: ' + str(path))
    require(len(blob) == 12 + w * h * (1 if kind == 1 else 4) * 4,
            'Incomplete raw payload: ' + str(path))
    return (w, h, kind), blob


def compare(first, second):
    names = sorted(p.name for p in first.iterdir() if p.name.endswith(('.png', '.bin.gz')))
    other = sorted(p.name for p in second.iterdir() if p.name.endswith(('.png', '.bin.gz')))
    require(names and names == other, 'Capture inventory differs: ' + str(second))
    rows = []
    for name in names:
        sa, a = decoded(first / name)
        sb, b = decoded(second / name)
        row = {'file': name, 'exact': sa == sb and a == b,
               'reference_sha256': hashlib.sha256(a).hexdigest(),
               'candidate_sha256': hashlib.sha256(b).hexdigest()}
        if not row['exact'] and name.endswith('.png') and sa == sb:
            row['different_pixels'] = sum(a[i:i+4] != b[i:i+4] for i in range(0, len(a), 4))
            row['max_channel_delta'] = max(abs(x-y) for x, y in zip(a, b))
        rows.append(row)
    state = lambda p: [line for line in (p / 'game.log').read_text(errors='replace').splitlines()
                       if line.split('|', 1)[0] in STATE_TAGS]
    a, b = state(first), state(second)
    result = {'passed': all(row['exact'] for row in rows) and a == b,
              'state_exact': a == b, 'state_records': len(a), 'targets': rows}
    (second / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def inspect(folder, case, arm, phases, args):
    log = (folder / 'game.log').read_text(errors='replace')
    require(re.search(r'^SCRIPT\|end.*0 failures$', log, re.M), 'Script failed: ' + str(folder))
    require('GAMEOVER|' not in log, 'Mission ended before capture completed: ' + str(folder))
    telemetry = 'MESHWINDDEPTH' if args.optimization == 'shadow' else 'MESHWINDDIRECT'
    records = re.findall(r'^' + telemetry + r'\|([^\r\n]+)', log, re.M)
    require(records, 'Missing ' + telemetry + ' telemetry: ' + str(folder))
    counters = [dict((key, int(value)) for key, value in
                     (part.split('=', 1) for part in row.split('|'))) for row in records]
    require(all(row['mismatches'] == 0 for row in counters), 'CPU verification mismatch: ' + str(folder))
    final = counters[-1]
    if arm == 'off' and args.optimization == 'shadow':
        require(all(row['draws'] == 0 and row['verified'] == 0 for row in counters),
                'Disabled shadow path was exercised: ' + str(folder))
    if arm == 'on':
        require(final['draws'] > 0, 'Optimized path not exercised: ' + str(folder))
        if 'matrix_draws' in final:
            require(final['matrix_draws'] > 0, 'Matrix optimized path not exercised: ' + str(folder))
    elif arm == 'verify':
        require(final['verified'] > 0, 'CPU verifier not exercised: ' + str(folder))
        if 'matrix_verified' in final:
            require(final['matrix_verified'] > 0, 'Matrix CPU verifier not exercised: ' + str(folder))
    elif arm.endswith('failure'):
        require(final['fallbacks'] > 0, 'Forced fallback not exercised: ' + str(folder))
    for phase in phases:
        require((folder / (phase + '.png')).is_file(), 'Missing image ' + phase)
        if not args.no_raw_targets:
            for target in TARGETS:
                require((folder / (phase + '-' + target + '.bin.gz')).is_file(),
                        'Missing raw target ' + phase + '-' + target)
    # Returning to the same transform at the same simulation time must reproduce
    # both visible and normal/depth output, independently of the paired control.
    for suffix in ['.png'] + ([] if args.no_raw_targets else ['-' + t + '.bin.gz' for t in TARGETS]):
        require(decoded(folder / ('scale-a' + suffix)) == decoded(folder / ('scale-a-return' + suffix)),
                'Transform restoration changed output: ' + str(folder / suffix))
    timings = []
    for phase in phases:
        block = log.split('TREECOMPARE|begin|' + phase, 1)[1].split('TREECOMPARE|end|' + phase, 1)[0]
        found = re.search(r'FRAMEBENCH\|n=(\d+)\|median=([\d.]+)\|p90=([\d.]+)', block)
        require(found, 'Missing benchmark ' + phase)
        timings.append({'phase': phase, 'samples': int(found[1]),
                        'median_ms': float(found[2]), 'p90_ms': float(found[3])})
    return {'telemetry': telemetry, 'counters': final, 'timings': timings,
            'raw_targets_checked': not args.no_raw_targets, 'passed': True}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'runtime', 'ra-gfx', 'td-gfx', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--suite', choices=('ra', 'td', 'both'), default='both')
    p.add_argument('--optimization', choices=('matrix', 'shadow'), default='matrix')
    p.add_argument('--td-skirmish', action='store_true',
                   help='Use SCM01EA, eight players and tick 400 instead of the TD forest mission')
    p.add_argument('--arms', default='off,on,verify')
    p.add_argument('--width', type=int, default=1280)
    p.add_argument('--height', type=int, default=720)
    p.add_argument('--samples', type=int, default=60)
    p.add_argument('--warmup', type=int, default=5)
    p.add_argument('--no-raw-targets', action='store_true')
    p.add_argument('--generate-only', action='store_true')
    args = p.parse_args()
    for name in ('candidate', 'runtime', 'ra_gfx', 'td_gfx', 'output'):
        setattr(args, name, getattr(args, name).resolve())
    arms = args.arms.split(',')
    require(len(arms) >= 2 and arms[0] == 'off' and len(set(arms)) == len(arms)
            and all(arm in ARMS for arm in arms), 'Use distinct supported arms beginning with off')
    require(args.samples > 0 and args.warmup > 0, 'Frame counts must be positive')
    args.output.mkdir(parents=True, exist_ok=False)
    supplied = [args.candidate, args.ra_gfx, args.td_gfx]
    for name in ('opencnc3d-fx.cfg', 'ra3d-fx.cfg'):
        path = args.runtime / name
        if path.exists():
            supplied.append(path)
    before = {str(path): sha(path) for path in supplied}
    result = {'inputs': before, 'runtime': str(args.runtime), 'cases': {},
              'optimization': args.optimization, 'td_skirmish': args.td_skirmish,
              'generated_only': args.generate_only, 'passed': False}
    for case in ('ra', 'td'):
        if args.suite not in ('both', case):
            continue
        config = args.ra_gfx if case == 'ra' else args.td_gfx
        cfg = read_cfg(config)
        require(all(k in cfg for k in ('etree_height', 'etree_width', 'etree_vary')),
                'Preset must explicitly contain all three tree transform dials: ' + str(config))
        result['cases'][case] = {'arms': {}, 'comparisons': {}}
        for arm in arms:
            folder = args.output / (case + '-' + arm)
            folder.mkdir()
            (folder / 'saves').mkdir()
            (folder / 'preset.cfg').write_bytes(config.read_bytes())
            script, phases = make_script(case, folder, cfg, args)
            (folder / 'script.txt').write_text(script)
            env = {k: v for k, v in os.environ.items() if not k.startswith('OPENCNC3D_')}
            env.update(OPENCNC3D_BACKGROUND='1', OPENCNC3D_MESH_WIND_DIRECT='1',
                       OPENCNC3D_MESH_WIND_MATRIX='1')
            switch = ('OPENCNC3D_MESH_WIND_DEPTH' if args.optimization == 'shadow'
                      else 'OPENCNC3D_MESH_WIND_MATRIX')
            env[switch] = '0' if arm == 'off' else '1'
            if arm == 'verify':
                env['OPENCNC3D_MESH_WIND_DIRECT_VERIFY'] = '1'
            elif arm == 'allocation-failure':
                env['OPENCNC3D_MESH_WIND_DIRECT_ALLOC_OK'] = '0'
            elif arm == 'upload-failure':
                env['OPENCNC3D_MESH_WIND_DIRECT_UPLOAD_OK'] = '0'
            cmd = [str(args.candidate), '--enhanced', '--nosound', '--ticks', '0',
                   '--w', str(args.width), '--h', str(args.height), '--vsync', '0',
                   '--gfx', str(folder / 'preset.cfg'), '--gfxsave', str(folder / 'private-save.cfg'),
                   '--savedir', str(folder / 'saves'), '--script', str(folder / 'script.txt')]
            if case == 'ra':
                cmd.extend(['--ra3d', '--scen', 'SCM01EA', '--pack', 'ra3d/3d/SCM01EA.pack',
                            '--dylib', 'ra3d/RedAlert.dylib', '--dir', 'ra3d/missions/',
                            '--content', 'ra3d/data', '--dosdata', 'ra3d/audio', '--skirmish',
                            '--ai', '7', '--credits', '10000', '--build', '10', '--seed', '77'])
            else:
                scen = 'SCM01EA' if args.td_skirmish else 'SCG05EA'
                cmd.extend(['--scen', scen, '--pack', scen + '.pack',
                            '--dylib', 'TiberianDawn.dylib', '--dir', 'missions/', '--content', 'content/'])
                if args.td_skirmish:
                    # The parser reads six explicit starts; the last two retain 6,7.
                    # This keeps every start unique and puts the human at cell 58,35.
                    cmd.extend(['--skirmish', '--side', 'gdi', '--ai', '7', '--difficulty', 'normal',
                                '--build', '7', '--credits', '5000', '--unitcount', '0',
                                '--starts', '5,1,2,3,4,0', '--seed', '77'])
            invocation = {'args': cmd, 'cwd': str(args.runtime),
                          'controls': {k: v for k, v in env.items() if k.startswith('OPENCNC3D_')}}
            (folder / 'invocation.json').write_text(json.dumps(invocation, indent=2) + '\n')
            if args.generate_only:
                result['cases'][case]['arms'][arm] = invocation
                continue
            start = time.monotonic()
            with (folder / 'game.log').open('w') as log:
                completed = subprocess.run(cmd, cwd=args.runtime, env=env, stdout=log,
                                           stderr=subprocess.STDOUT, timeout=900)
            require(completed.returncode == 0, 'Renderer failed: ' + str(folder))
            report = inspect(folder, case, arm, phases, args)
            report['elapsed_seconds'] = time.monotonic() - start
            result['cases'][case]['arms'][arm] = report
            print(json.dumps({'case': case, 'arm': arm, **report}), flush=True)
            (folder / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            if arm != 'off':
                result['cases'][case]['comparisons'][arm] = compare(args.output / (case + '-off'), folder)
            (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    require(before == {str(path): sha(path) for path in supplied}, 'Source executable or settings changed during run')
    result['passed'] = not args.generate_only and all(
        comparison['passed'] for case in result['cases'].values() for comparison in case['comparisons'].values())
    (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    if args.generate_only:
        print('Generated scripts and invocations only; no game was launched.')
    else:
        require(result['passed'], 'Exact comparison failed; inspect per-arm comparison.json')
        print('PASS: ' + args.optimization + ' optimization, ' + args.suite
              + ' suite, wind phases, transformed dials, fog, and exact decoded captures')


if __name__ == '__main__':
    main()
