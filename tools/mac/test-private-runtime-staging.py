#!/usr/bin/env python3
"""Run the real Mac staging script in tiny private fixtures; never stage a real game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE_RECORD = '# Local source build; no verified release asset data ID.\nversion fixture\ndata_id unknown\n'
MARKER = 'private_developer 1\n'
ALIASES = ['opencnc3d', 'cnc3d', 'ra3d/opencnc3d', 'ra3d/cnc3d']


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode() if isinstance(data, str) else data)


def fixture(root, script):
    write(root / 'tools/mac/stage-runtime.py', script)
    write(root / 'game/bibenh.pack', b'BIBENH4\0\x04\0\0\0fixture')
    write(root / 'game/clearblend_TEMPERAT.pack', b'ground fixture')
    for name in ['tools/art/terrain/stage_desert_details.py', 'tools/art/rocks/stage_stone_materials.py']:
        write(root / name, 'import sys\nfrom pathlib import Path\np=Path(sys.argv[1])/"fixture-stagers.log"\nwith p.open("a") as f:f.write("' + Path(name).name + '\\n")\n')
    binary = root / 'built-app'
    write(binary, b'fixture\0OPENCNC3D|build|vfixture|\n')
    runtime = root / 'playable'
    for name in ALIASES:
        write(runtime / name, b'old binary')
    for name in ['opencnc3d-fx.cfg', 'ra3d-fx.cfg', 'saves/player.sav', 'unrelated.pack', 'C&C3D.app/Contents/keep']:
        write(runtime / name, 'unchanged ' + name)
    return runtime, binary


def files(root):
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in root.rglob('*') if p.is_file()}


def run(root, cooked, no_ra=False):
    runtime, binary = root / 'playable', root / 'built-app'
    env = {k: v for k, v in os.environ.items() if not k.startswith('OPENCNC3D_')}
    if cooked is not None:
        env['OPENCNC3D_COOKED'] = cooked
    if no_ra:
        env['OPENCNC3D_NO_RA'] = '1'
    return subprocess.run([sys.executable, str(root / 'tools/mac/stage-runtime.py'), str(runtime), str(binary)],
                          env=env, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--baseline-ref', required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    current = (ROOT / 'tools/mac/stage-runtime.py').read_bytes()
    baseline = subprocess.check_output(['git', 'show', args.baseline_ref + ':tools/mac/stage-runtime.py'], cwd=ROOT)
    reports = []
    for name, cooked, no_ra, mineral in [
        ('developer-unset', None, False, False), ('developer-empty', '', False, False),
        ('cooked-one', '1', False, False), ('cooked-zero-nonempty', '0', False, False),
        ('cooked-other-nonempty', 'yes', False, False), ('developer-no-RA', None, True, False),
        ('developer-mineral', None, False, True),
    ]:
        variants = {}
        for kind, script in [('baseline', baseline), ('current', current)]:
            root = out / name / kind
            runtime, binary = fixture(root, script)
            before = files(runtime)
            if mineral:
                write(runtime / 'stonematerials.pack', b'installed mineral sentinel')
            result = run(root, cooked, no_ra)
            write(root / 'stdout.log', result.stdout)
            write(root / 'stderr.log', result.stderr)
            assert result.returncode == 0, result.stderr
            after = files(runtime)
            expected = BASE_RECORD + (MARKER if kind == 'current' and not cooked else '')
            for record in ['cnc3d-install.txt', 'opencnc3d-install.txt']:
                assert after[record] == expected.encode(), (name, kind, record)
            for build_id in ['BUILD-ID', 'BUILD-ID.txt']:
                assert after[build_id] == b'vfixture macOS\n'
            for alias in ALIASES:
                assert after[alias] == (before[alias] if no_ra and alias.startswith('ra3d/') else binary.read_bytes())
            for path, data in before.items():
                if path not in ALIASES:
                    assert after[path] == data, (name, kind, path)
            assert after['bibenh.pack'] == (root / 'game/bibenh.pack').read_bytes()
            assert after['clearblend_TEMPERAT.pack'] == b'ground fixture'
            assert after['fixture-stagers.log'] == (b'stage_desert_details.py\nstage_stone_materials.py\n' if mineral else b'stage_desert_details.py\n')
            variants[kind] = after
        assert variants['baseline'].keys() == variants['current'].keys()
        for path, data in variants['baseline'].items():
            want = data + MARKER.encode() if not cooked and path in ['cnc3d-install.txt', 'opencnc3d-install.txt'] else data
            assert variants['current'][path] == want, (name, path)
        reports.append({'case': name, 'passed': True, 'private_marker': not bool(cooked), 'all_other_files_byte_exact_vs_actual_baseline_script': True})
    # Rebuilding the same fixture as cooked removes the local-build marker rather
    # than letting a previous development record leak into a public build.
    root = out / 'developer-then-cooked'
    runtime, _ = fixture(root, current)
    assert run(root, None).returncode == 0
    assert (runtime / 'opencnc3d-install.txt').read_text() == BASE_RECORD + MARKER
    assert run(root, '1').returncode == 0
    for name in ['cnc3d-install.txt', 'opencnc3d-install.txt']:
        assert (runtime / name).read_text() == BASE_RECORD
    reports.append({'case': 'developer-then-cooked', 'passed': True, 'stale_private_marker_removed_by_cooked_record': True})
    receipt = {'passed': True, 'script_sha256': hashlib.sha256(current).hexdigest(),
               'baseline_ref': args.baseline_ref, 'baseline_sha256': hashlib.sha256(baseline).hexdigest(),
               'actual_script_executed': True, 'fixture_stagers_only': True,
               'no_real_runtime_network_GPU_or_game_execution': True, 'cases': reports}
    (out / 'RESULTS.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PRIVATE-STAGE|PASS|8 actual-script fixtures; only development records gain private_developer 1')


if __name__ == '__main__':
    main()
