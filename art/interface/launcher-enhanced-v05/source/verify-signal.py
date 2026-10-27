#!/usr/bin/env python3
"""Native CPU verification of EVA idle, hover and click effects; no SDL window.

Usage: verify-signal.py LAUNCHER INSTALL OUTPUT [--source-dir LAUNCHER_SOURCE]
Requires Pillow, numpy and a local C compiler.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('launcher', 'install', 'output'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--source-dir', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'launcher')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    out = args.output.resolve()
    env = dict(os.environ, OPENCNC3D_BACKGROUND='1', SDL_MAC_BACKGROUND_APP='1')

    def shot(name, *flags):
        target = out / (name + '.png')
        result = subprocess.run([str(args.launcher.resolve()), '--dir', str(args.install.resolve()),
                                 '--shot', str(target), *map(str, flags)], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=30)
        (out / (name + '.log')).write_text(result.stdout)
        assert result.returncode == 0, (name, result.returncode, result.stdout[-1200:])
        image = np.array(Image.open(target).convert('RGBA'))
        assert image.shape == (636, 1016, 4)
        seats = [line for line in result.stdout.splitlines()
                 if line.startswith(('LBTN|', 'LMOD|', 'LCHK|', 'LGEAR|'))]
        return image, seats

    baseline, seats = shot('still', '--no-motion')
    rgb = baseline[:, :, :3].astype(np.int16)
    black = rgb.max(2) <= 4
    luminance = rgb.astype(np.int32)
    bright = ((54 * luminance[:, :, 0] + 183 * luminance[:, :, 1] + 19 * luminance[:, :, 2]) >> 8) >= 192
    integrated = {}
    for ms in (0, 2467, 4400, 7000, 12467, 15999, 16000):
        actual, actual_seats = shot('phase-%05d' % ms, '--signal-time', ms)
        assert actual_seats == seats, ('interaction geometry', ms)
        delta = np.abs(actual[:, :, :3].astype(np.int16) - rgb)
        assert np.array_equal(actual[:, :, 3], baseline[:, :, 3]), ('alpha', ms)
        assert not delta[black].any(), ('native shadows', ms)
        assert delta.max() <= 50, ('bounded integrated energy', ms, int(delta.max()))
        assert delta[bright].max() <= 5, ('lettering protection', ms, int(delta[bright].max()))
        integrated[ms] = actual
    assert np.array_equal(integrated[0], integrated[16000]), 'integrated loop closure'
    repeat, _ = shot('repeat-zero', '--signal-time', 0)
    assert np.array_equal(repeat, integrated[0]), 'state-independent repeat'
    disabled, _ = shot('disabled-later', '--no-motion', '--signal-time', 12467)
    assert np.array_equal(disabled, baseline), 'motion-off exact source'

    library = out / ('signal.dylib' if sys.platform == 'darwin' else 'signal.so')
    link = ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-std=gnu89',
                    *link, str(args.source_dir / 'lsignal.c'), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    pointer = ctypes.POINTER(ctypes.c_ubyte)
    lib.ls_signal_apply.argtypes = [pointer, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    lib.ls_signal_react.argtypes = [pointer] + [ctypes.c_int] * 6 + [ctypes.c_uint, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    lib.ls_signal_idle.argtypes = [pointer] + [ctypes.c_int] * 6 + [ctypes.c_uint, ctypes.c_uint]

    def apply(image, ms):
        actual = image.copy()
        lib.ls_signal_apply(actual.ctypes.data_as(pointer), actual.shape[1], actual.shape[0], ms)
        return actual

    maximum = np.zeros(3, dtype=int)
    quadrants = np.zeros(4, dtype=int)
    previous, zero = apply(baseline, 15984), apply(baseline, 0)
    adjacent_maximum = 0.0
    start = time.perf_counter()
    for tick in range(960):
        ms = (tick * 1000 + 59) // 60
        actual = apply(baseline, ms)
        delta = np.abs(actual[:, :, :3].astype(np.int16) - rgb)
        maximum = np.maximum(maximum, delta.max((0, 1)))
        assert delta.max() <= 32
        assert delta[bright].max() <= 3
        assert not delta[black].any()
        assert np.array_equal(actual[:, :, 3], baseline[:, :, 3])
        assert np.array_equal(actual, apply(baseline, ms + 16000))
        changed = (actual[:, :, :3] != zero[:, :, :3]).any(2)
        for i, area in enumerate((changed[:318, :508], changed[:318, 508:],
                                  changed[318:, :508], changed[318:, 508:])):
            quadrants[i] += int(area.any())
        adjacent_maximum = max(adjacent_maximum, float(np.abs(actual[:, :, :3].astype(np.int16) - previous[:, :, :3].astype(np.int16)).mean()))
        previous = actual
    assert (quadrants > 0).all()
    assert adjacent_maximum < 1, ('no frame flashes', adjacent_maximum)
    assert maximum[1] >= 20, ('stronger than v04', maximum.tolist())

    # Effects stay within the real native widget rectangle, including partial
    # off-canvas widgets. Idle, hover, pressed and release-pulse are distinct.
    reactions = []
    for rect in ((100, 216, 186, 186), (88, 543, 192, 42), (-5, -3, 25, 20), (1015, 635, 8, 8)):
        x, y, width, height = rect
        allowed = np.zeros(baseline.shape[:2], bool)
        allowed[max(0, y):min(636, y + height), max(0, x):min(1016, x + width)] = True
        for hot, pressed, age in ((1, 0, 0xffffffff), (1, 1, 0xffffffff), (0, 0, 90), (0, 0, 420), (0, 0, 0xffffffff)):
            actual = baseline.copy()
            lib.ls_signal_react(actual.ctypes.data_as(pointer), 1016, 636, *rect, 7000, hot, pressed, age)
            changed = (actual != baseline).any(2)
            assert not changed[~allowed].any(), ('reaction clip', rect)
            assert np.array_equal(actual[:, :, 3], baseline[:, :, 3])
            if not hot and not pressed and age >= 420:
                assert not changed.any(), 'idle/expired click is no-op'
            elif rect[0] >= 0 and rect[1] >= 0 and rect[2] > 20:
                assert changed.any(), ('visible reaction', rect, hot, pressed, age)
            reactions.append({'rect': rect, 'hot': hot, 'pressed': pressed, 'age_ms': age,
                              'changed_pixels': int(changed.sum())})
    idle_seen = False
    for ms in range(0, 16000, 100):
        actual = baseline.copy()
        lib.ls_signal_idle(actual.ctypes.data_as(pointer), 1016, 636, 100, 216, 186, 186, ms, 17)
        changed = (actual != baseline).any(2)
        allowed = np.zeros(baseline.shape[:2], bool)
        allowed[216:402, 100:286] = True
        assert not changed[~allowed].any(), 'idle component stays in its seat'
        assert np.array_equal(actual[:, :, 3], baseline[:, :, 3])
        idle_seen |= bool(changed.any())
    assert idle_seen, 'visible component idle accent'

    report = {'passed': True, 'period_ms': 16000, 'global_ticks': 960,
              'global_maximum_rgb_delta': maximum.tolist(),
              'worst_adjacent_mean_rgb_delta': adjacent_maximum,
              'quadrants_with_motion_ticks': quadrants.tolist(),
              'pure_black_and_alpha_exact': True, 'motion_off_source_exact': True,
              'all_hit_geometry_exact': True, 'loop_exact': True,
              'visible_hover_pressed_click_and_idle': True, 'reaction_cases': reactions,
              'measurement_seconds': time.perf_counter() - start}
    (out / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
