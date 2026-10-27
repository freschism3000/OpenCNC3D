#!/usr/bin/env python3
"""Verify the real launcher signal frames without opening a window.

Usage: verify-signal.py LAUNCHER INSTALL OUTPUT [--source-dir LAUNCHER_SOURCE]
Requires Pillow and numpy for pixel measurements and the local C compiler.
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
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('launcher', type=Path)
    p.add_argument('install', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--source-dir', type=Path,
                   default=Path(__file__).resolve().parents[2] / 'launcher')
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    env = dict(os.environ, OPENCNC3D_BACKGROUND='1', SDL_MAC_BACKGROUND_APP='1')

    def shot(name, *options):
        path = output / (name + '.png')
        q = subprocess.run([str(args.launcher.resolve()), '--dir',
                            str(args.install.resolve()), '--shot', str(path),
                            *map(str, options)], env=env, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=30)
        (output / (name + '.log')).write_text(q.stdout)
        assert q.returncode == 0, (name, q.returncode, q.stdout[-1000:])
        image = np.array(Image.open(path).convert('RGBA'))
        assert image.shape == (636, 1016, 4), (name, image.shape)
        return image, q.stdout

    library = output / ('signal.dylib' if sys.platform == 'darwin' else 'signal.so')
    link = ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-std=gnu89',
                    *link, str(args.source_dir / 'lsignal.c'), '-o', str(library)],
                   check=True)
    apply = ctypes.CDLL(str(library)).ls_signal_apply
    apply.argtypes = [ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int,
                      ctypes.c_int, ctypes.c_uint]
    apply.restype = None

    def frame(base, ms):
        image = base.copy()
        apply(image.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte)),
              image.shape[1], image.shape[0], ms)
        return image

    baseline, baseline_log = shot('still', '--no-motion')
    assert 'LSIGNAL|enabled=0|' in baseline_log
    results = []
    seats = [line for line in baseline_log.splitlines()
             if line.startswith(('LBTN|', 'LMOD|', 'LCHK|', 'LGEAR|'))]
    # Exercise integrated composition at scan positions, packet events and wrap.
    for ms in [0, 2467, 4400, 7000, 12467, 15999, 16000]:
        actual, log = shot('integrated-%05d' % ms, '--signal-time', ms)
        assert np.array_equal(actual, frame(baseline, ms)), ('composition', ms)
        assert seats == [line for line in log.splitlines()
                         if line.startswith(('LBTN|', 'LMOD|', 'LCHK|', 'LGEAR|'))]
        assert 'LSIGNAL|enabled=1|ms=%d|period_ms=16000' % ms in log
        results.append({'ms': ms, 'pixel_exact_native_composition': True})
    disabled, _ = shot('disabled-later', '--no-motion', '--signal-time', 7920)
    assert np.array_equal(disabled, baseline), 'disabled effect changes source pixels'
    assert np.array_equal(frame(baseline, 0), frame(baseline, 16000))

    # Cover every animation tick, including rare interference pulses. Bounds
    # protect label contrast and black shadows rather than copying the formula.
    rgb = baseline[:, :, :3].astype(np.int16)
    black = rgb.max(axis=2) <= 4
    bright = rgb.min(axis=2) >= 180
    maximum = np.zeros(3, dtype=int)
    bright_maximum = 0
    changed_quadrants = np.zeros(4, dtype=int)
    worst_adjacent_mean = 0.0
    zero = frame(baseline, 0)
    previous = frame(baseline, 15966)
    start = time.perf_counter()
    for tick in range(480):
        ms = (tick * 1000 + 29) // 30
        actual = frame(baseline, ms)
        delta = np.abs(actual[:, :, :3].astype(np.int16) - rgb)
        maximum = np.maximum(maximum, delta.max(axis=(0, 1)))
        bright_maximum = max(bright_maximum, int(delta[bright].max()))
        assert np.array_equal(actual[:, :, 3], baseline[:, :, 3]), ('alpha', ms)
        assert np.array_equal(actual[:, :, :3][black], baseline[:, :, :3][black]), ('black shadow', ms)
        assert np.array_equal(actual, frame(baseline, ms + 16000)), ('loop', ms)
        changed = (actual[:, :, :3] != zero[:, :, :3]).any(axis=2)
        for q, region in enumerate([changed[:318, :508], changed[:318, 508:],
                                    changed[318:, :508], changed[318:, 508:]]):
            changed_quadrants[q] += int(region.any())
        adjacent = np.abs(actual[:, :, :3].astype(np.int16) -
                          previous[:, :, :3].astype(np.int16)).mean()
        worst_adjacent_mean = max(worst_adjacent_mean, float(adjacent))
        previous = actual
    elapsed = time.perf_counter() - start
    assert (maximum < 24).all(), maximum.tolist()
    assert bright_maximum <= 3, bright_maximum
    assert (changed_quadrants > 0).all(), changed_quadrants.tolist()
    assert worst_adjacent_mean < 1, worst_adjacent_mean

    # A fresh render never accumulates signal tint; repeatability also catches
    # stateful random noise and dependence on how many frames were rendered.
    assert np.array_equal(frame(baseline, 4400), frame(baseline, 4400))
    # Invalid phase values must exit before any SDL window is created.
    for value in ['-1', '3600001', 'abc', None]:
        cli = [str(args.launcher.resolve()), '--signal-time']
        if value is not None:
            cli.append(value)
        q = subprocess.run(cli, env=env, capture_output=True, timeout=5)
        assert q.returncode == 2, ('invalid CLI phase', value, q.returncode)

    report = {'passed': True, 'period_ms': 16000, 'tested_ticks': 480,
              'integrated_frames': results, 'maximum_rgb_delta': maximum.tolist(),
              'bright_text_maximum_delta': bright_maximum,
              'worst_adjacent_mean_rgb_delta': worst_adjacent_mean,
              'quadrants_with_motion_ticks': changed_quadrants.tolist(),
              'full_loop_measurement_seconds': elapsed,
              'alpha_preserved': True, 'black_shadows_preserved': True,
              'exact_loop': True, 'motion_off_source_exact': True,
              'invalid_phase_exits_before_window': True}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
