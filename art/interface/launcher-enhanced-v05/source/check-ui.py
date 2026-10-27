#!/usr/bin/env python3
"""Verify real hidden SDL events and the actual game's Enhanced cursor.

The launcher must implement --hidden-ui-test REPORT_DIR, --shot-mouse X,Y,
and --shot-press. This script never requests a visible launcher or moves the
system pointer. The independent cursor oracle creates a hidden SDL GL context
and includes the unchanged game cursor drawing header.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import zlib

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ui_records(text):
    rows = []
    for line in text.splitlines():
        fields = line.split('|')
        if len(fields) < 2 or fields[0] != 'UIQ':
            continue
        row = {'kind': fields[1]}
        for field in fields[2:]:
            if '=' in field:
                key, value = field.split('=', 1)
                row[key] = value
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('launcher', type=Path)
    parser.add_argument('install', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'launcher')
    parser.add_argument('--sdl-config', type=Path)
    args = parser.parse_args()
    launcher, install, output = args.launcher.resolve(), args.install.resolve(), args.output.resolve()
    assert launcher.is_file() and os.access(launcher, os.X_OK)
    assert install.is_dir() and (install / 'ecur/ecur.bin').is_file()
    assert output != install and install not in output.parents
    assert not output.exists() or not any(output.iterdir()), 'Use a fresh output directory'
    for pattern in ('*update.journal', '*update.done'):
        assert not list(install.glob(pattern)), 'Finish the existing update first'
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OPENCNC3D_BACKGROUND='1', SDL_MAC_BACKGROUND_APP='1')
    if sys.platform == 'darwin':
        env['DYLD_LIBRARY_PATH'] = ':'.join(filter(None, [str(install), str(ROOT / 'playable'),
                                                       env.get('DYLD_LIBRARY_PATH', '')]))
    spec = importlib.util.spec_from_file_location('layout_verify', ROOT / 'tools/launcher/verify-enhanced.py')
    preservation = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(preservation)
    before = preservation.protected_snapshot(install)
    (output / 'protected-before.json').write_text(json.dumps(before, indent=2) + '\n')
    report = {'passed': False, 'visible_window_requested': False,
              'launcher': str(launcher), 'launcher_sha256': sha(launcher), 'installation': str(install)}

    def run(name, command, timeout=120):
        process = subprocess.run([str(value) for value in command], cwd=install, env=env,
                                 capture_output=True, text=True, errors='replace', timeout=timeout)
        log = process.stdout + process.stderr
        (output / (name + '.log')).write_text(log)
        assert process.returncode == 0, (name, process.returncode, log[-2000:])
        print(name, 'exit=0', flush=True)
        return log

    def shot(name, *extra, motion=False):
        path = output / (name + '.png')
        options = [] if motion else ['--no-motion']
        text = run(name, [launcher, '--dir', install, '--view', 'modules', '--scale', '3',
                         *options, '--shot', path, *extra], timeout=45)
        image = np.asarray(Image.open(path).convert('RGBA'))
        assert image.shape == (636, 1016, 4), (name, image.shape)
        return image, text

    try:
        help_text = run('supported-help', [launcher, '--help'], timeout=5)
        assert '--hidden-ui-test' in help_text, 'Refusing unsupported binary: an old launcher could otherwise open visibly'
        assert '--shot-mouse' in help_text and '--shot-press' in help_text, 'Missing safe CPU fixtures'
        gui = output / 'hidden-ui'
        gui.mkdir()
        log = run('hidden-ui', [launcher, '--dir', install, '--hidden-ui-test', gui], timeout=45)
        rows = ui_records(log)
        grouped = {kind: [row for row in rows if row['kind'] == kind]
        for kind in ('flags', 'map', 'close', 'drag', 'motion', 'pacing', 'cursor_gl')}
        assert all(grouped[kind] for kind in grouped), ('Missing hidden UI evidence', rows)
        for row in grouped['flags']:
            flags = int(row['value'])
            assert flags & 0x8 and flags & 0x10 and not flags & 0x4, row
            assert row['hidden'] == row['borderless'] == '1' and row['ever_shown'] == '0', row
            assert row['raise_calls'] == row['focus_calls'] == '0', row
        assert len(grouped['map']) >= 4, 'Need real event mapping at centres, edges and letterboxing'
        for row in grouped['map']:
            for actual, expected in [('nx', 'expected_nx'), ('ny', 'expected_ny'),
                                     ('mx', 'expected_mx'), ('my', 'expected_my')]:
                assert abs(float(row[actual]) - float(row[expected])) < 0.01, row
            assert row['hit'] == row['expected_hit'], row
        for row in grouped['close']:
            assert row['outside_kept'] == row['dragoff_kept'] == row['quit'] == '1', row
        for row in grouped['drag']:
            assert row['header'] == '1' and row['control'] == '0', row
        for row in grouped['motion']:
            assert abs(float(row['nx']) - float(row['expected_nx'])) < 0.01, row
            assert abs(float(row['ny']) - float(row['expected_ny'])) < 0.01, row
            assert row['before_present'] == '1', row
        for row in grouped['pacing']:
            assert row['fixed33'] == '0', row
            assert int(row['frame_count']) >= 3, row
            # Wall timing is evidence, not a display- or CI-dependent pass bound.
        source = (args.source_dir / 'launcher.c').read_text()
        assert not re.search(r'SDL_Delay\s*\(\s*33u?\s*-', source), 'Old deterministic 33 ms delay remains'
        report['hidden_ui_records'] = rows
        report['hardware_frame_timing_is_record_only'] = True

        baseline, baseline_log = shot('cursor-baseline')
        raw = output / 'cursor-baseline.rgba'
        raw.write_bytes(baseline.tobytes())
        sdk = args.sdl_config
        if sdk is None:
            bundled = Path.home() / '.opencnc3d-macsdk/bin/sdl2-config'
            sdk = bundled if bundled.is_file() else Path(shutil.which('sdl2-config') or '')
        assert sdk.is_file(), 'Provide --sdl-config'
        cflags = shlex.split(subprocess.check_output([str(sdk), '--cflags'], text=True))
        libs = shlex.split(subprocess.check_output([str(sdk), '--libs'], text=True))
        oracle = output / 'cursor-reference'
        platform = ['-framework', 'OpenGL'] if sys.platform == 'darwin' else ['-lGL']
        command = ['clang++', '-std=c++14', '-O2', '-Wall', '-Wextra', '-DGL_SILENCE_DEPRECATION',
                   '-I' + str(ROOT / 'menu'), *cflags, str(HERE / 'cursor-reference.cpp'),
                   *libs, *platform, '-o', str(oracle)]
        run('cursor-reference-build', command)
        # The oracle first proves its background transfer itself is pixel-exact.
        reference_background = output / 'oracle-background.rgba'
        run('oracle-background', [oracle, install, raw, reference_background, 1016, 636, -1000, -1000])
        assert reference_background.read_bytes() == raw.read_bytes(), 'GL oracle changed the background'
        cursor_results = []
        asset_crc32 = zlib.crc32((install / 'ecur/ecur.bin').read_bytes()) & 0xffffffff
        for x, y in ((500, 300), (501, 300), (502, 300), (0, 0)):
            name = 'cursor-%d-%d' % (x, y)
            actual, actual_log = shot(name, '--shot-mouse', '%d,%d' % (x, y))
            cursor_lines = [line for line in actual_log.splitlines() if line.startswith('LCURSOR|')]
            assert len(cursor_lines) == 1, cursor_lines
            cursor_fields = dict(field.split('=', 1) for field in cursor_lines[0].split('|')[1:] if '=' in field)
            assert cursor_fields['source'] == 'ecur/ecur.bin' and cursor_fields['mesh'] == 'CUR00'
            assert cursor_fields['smoothing'] == '0'
            assert float(cursor_fields['native_x']) == x and float(cursor_fields['native_y']) == y
            crc = int(cursor_fields['source_crc32'], 16)
            assert crc == asset_crc32, cursor_fields
            reference_path = output / (name + '-reference.rgba')
            run(name + '-oracle', [oracle, install, raw, reference_path, 1016, 636, x, y])
            reference = np.frombuffer(reference_path.read_bytes(), dtype=np.uint8).reshape(baseline.shape)
            changed = (actual != reference).any(axis=2)
            maximum = int(np.abs(actual.astype(np.int16) - reference.astype(np.int16)).max())
            row = {'native_xy': [x, y], 'rgba_sha256': hashlib.sha256(actual.tobytes()).hexdigest(),
                   'game_reference_rgba_sha256': sha(reference_path), 'different_pixels': int(changed.sum()),
                   'maximum_channel_delta': maximum, 'source_crc32': asset_crc32,
                   'software_vs_gl_rasterization_difference_recorded': True}
            cursor_results.append(row)
            # CPU previews and the live GL draw have distinct edge rounding.
            # Prove the source/hotspot above, record every residual here, and
            # compare the actual live GL path exactly below.
        assert cursor_results[0]['rgba_sha256'] != cursor_results[1]['rgba_sha256'] != cursor_results[2]['rgba_sha256'], 'Native pointer still snaps to the legacy three-pixel hit grid'
        report['cursor'] = {'source_asset': str(install / 'ecur/ecur.bin'),
                            'source_asset_sha256': sha(install / 'ecur/ecur.bin'),
                            'game_draw_source': str(ROOT / 'menu/enhanced_menu_cursor.h'),
                            'game_draw_source_sha256': sha(ROOT / 'menu/enhanced_menu_cursor.h'),
                            'mesh': 'CUR00', 'native_scale': 1, 'reference_uses_unchanged_game_draw': True,
                            'cases': cursor_results}
        gpu_results = []
        for index, row in enumerate(grouped['cursor_gl']):
            width, height, x, y = [int(row[key]) for key in ('width', 'height', 'x', 'y')]
            base_path, actual_path = gui / row['base'], gui / row['actual']
            assert base_path.parent == gui and actual_path.parent == gui
            assert base_path.stat().st_size == actual_path.stat().st_size == width * height * 4
            reference_path = output / ('live-gl-reference-%d.rgba' % index)
            run('live-gl-reference-%d' % index, [oracle, install, base_path, reference_path, width, height, x, y])
            actual_bytes, reference_bytes = actual_path.read_bytes(), reference_path.read_bytes()
            exact = actual_bytes == reference_bytes
            gpu_results.append({'native_xy': [x, y], 'size': [width, height], 'pixel_exact': exact,
                                'actual_rgba_sha256': sha(actual_path), 'game_reference_rgba_sha256': sha(reference_path)})
            assert exact, ('Live green cursor does not exactly match the game draw', gpu_results[-1])
        report['cursor']['live_gl_cases'] = gpu_results
        # Hold the synthetic pointer over a harmless button, then hold a press.
        # The CPU fixtures render state only; they must never activate a game.
        def layout(text):
            result = []
            for line in text.splitlines():
                if not line.startswith(('LBTN|', 'LMOD|', 'LCHK|', 'LGEAR|', 'LWINDOW|')):
                    continue
                tag, *fields = line.split('|')
                record = dict(field.split('=', 1) for field in fields if '=' in field)
                result.append((tag, {key: record.get(key) for key in ('i', 'label', 'x', 'y', 'w', 'h', 'hidden', 'drawn')}))
            return result
        target = None
        for tag, fields in layout(baseline_log):
            if tag == 'LBTN' and fields.get('i') == '1':
                target = fields
                break
        assert target and target['hidden'] == '0'
        bx, by, bw, bh = [int(target[key]) for key in ('x', 'y', 'w', 'h')]
        mouse = '%d,%d' % (bx + bw // 2, by + bh // 2)
        hovered, hovered_log = shot('button-hover', '--shot-mouse', mouse)
        pressed, pressed_log = shot('button-press', '--shot-mouse', mouse, '--shot-press')
        assert layout(hovered_log) == layout(pressed_log) == layout(baseline_log), 'Hover/press changed interaction geometry or activated a screen'
        target_region = np.zeros(baseline.shape[:2], dtype=bool)
        target_region[by:by + bh, bx:bx + bw] = True
        hover_pixels = int((target_region & (hovered != baseline).any(axis=2)).sum())
        held_pixels = int((target_region & (pressed != hovered).any(axis=2)).sum())
        assert hover_pixels > 0 and held_pixels > 0, 'Hover/press produced no visible native control feedback'
        idle_signal, idle_signal_log = shot('signal-idle-fixed', '--signal-time', '7000', motion=True)
        hover_signal, hover_signal_log = shot('signal-hover-fixed', '--signal-time', '7000', '--shot-mouse', mouse, motion=True)
        press_signal, press_signal_log = shot('signal-press-fixed', '--signal-time', '7000', '--shot-mouse', mouse, '--shot-press', motion=True)
        assert layout(idle_signal_log) == layout(hover_signal_log) == layout(press_signal_log) == layout(baseline_log)
        idle_delta = idle_signal[:, :, :3].astype(np.int16) - baseline[:, :, :3].astype(np.int16)
        hover_delta = hover_signal[:, :, :3].astype(np.int16) - hovered[:, :, :3].astype(np.int16)
        press_delta = press_signal[:, :, :3].astype(np.int16) - pressed[:, :, :3].astype(np.int16)
        reaction_hover = int((target_region & (hover_delta != idle_delta).any(axis=2)).sum())
        reaction_press = int((target_region & (press_delta != hover_delta).any(axis=2)).sum())
        assert reaction_hover > 0 and reaction_press > 0, 'Signal does not react to hover and held press'
        quadrants = [(slice(0, 318), slice(0, 508)), (slice(0, 318), slice(508, 1016)),
                     (slice(318, 636), slice(0, 508)), (slice(318, 636), slice(508, 1016))]
        motion_per_quadrant = [int((idle_delta[region] != 0).any(axis=2).sum()) for region in quadrants]
        assert all(value > 0 for value in motion_per_quadrant), 'Idle effect does not reach all four UI quadrants'
        report['interaction'] = {'target': target, 'mouse_native': mouse, 'hover_changed_pixels': hover_pixels,
                                 'held_changed_pixels': held_pixels, 'hover_signal_reaction_pixels': reaction_hover,
                                 'held_signal_reaction_pixels': reaction_press,
                                 'fixed_phase_idle_changed_pixels_per_quadrant': motion_per_quadrant,
                                 'geometry_unchanged': True, 'shot_press_activated_nothing': True}
        report['passed'] = True
    finally:
        after = preservation.protected_snapshot(install)
        changed = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
        report['protected_file_count'] = len(before)
        report['protected_files_changed'] = changed
        report['passed'] = report['passed'] and not changed
        (output / 'protected-after.json').write_text(json.dumps(after, indent=2) + '\n')
        (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    assert report['passed'], report
    print(json.dumps({'passed': True, 'hidden_only': True, 'evidence': str(output)}), flush=True)


if __name__ == '__main__':
    main()
