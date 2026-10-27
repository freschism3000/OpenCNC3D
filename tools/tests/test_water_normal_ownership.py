#!/usr/bin/env python3
"""CPU calibration for the saved-buffer water ownership regression gate.

The positive fixture is an orthographic dry-left/wet-right scene with a level
water bed. Independent mutations reproduce a water normal on dry land, a changed
rock normal, changed depth, empty/inverted owner RT, lost wet classification and
a nonflat wet bed. None launches the game or touches runtime assets.
"""
from pathlib import Path
import argparse
import gzip
import importlib.util
import json
import struct
import tempfile
import unittest

import numpy as np


GATE_PATH = Path(__file__).resolve().parents[1] / 'gate-water-normal-ownership.py'
SPEC = importlib.util.spec_from_file_location('ownership_gate', GATE_PATH)
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


class OwnershipGateCalibration(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='cnc3d-normal-ownership-')
        self.root = Path(self.scratch.name)
        self.height, self.width = 24, 32
        self.depth = np.full((self.height, self.width, 1), .2, dtype='<f4')
        self.normal = np.empty((self.height, self.width, 4), dtype='<f4')
        self.normal[:] = [.65, .9, .45, .88]
        self.normal[:, 16:] = [.5, 1, .5, 71 / 255]
        self.opaque = np.zeros((self.height, self.width, 4), dtype='u1')
        self.opaque[:, :16, 3] = 255
        # At depth .2 this camera maps every pixel to Y=-.04, while X/Z
        # span 0..8 cells. Top-left screen rows increase world Z.
        self.matrix = np.array([[4, 0, 0, 4], [0, 0, 1, .56],
                                [0, -4, 0, 4], [0, 0, 0, 1]], dtype=float)
        self.write_capture('capture', self.normal, self.depth)
        self.write_capture('reference', self.normal, self.depth)
        self.write_mask(self.opaque)
        yy, xx = np.indices((32, 32))
        distance = (xx + .5) / 4 - 4
        field = np.zeros((32, 32, 4), dtype='<u2')
        field[..., 0] = np.round((distance / 32 + .5) * 65535).astype('u2')
        field[..., 3] = np.where(xx >= 16, 65535, 0)
        (self.root / 'field16.bin').write_bytes(b'C3DFLD16' + struct.pack('<5I', 1, 32, 32, 4, 16) + field.tobytes())
        self.args = argparse.Namespace(capture=self.root / 'capture', reference=self.root / 'reference',
            field=self.root / 'field16.bin', opaque_mask=self.root / 'opaque.bin',
            require_mask=True, require_reference=True, dry_roi=[1, 2, 12, 22],
            min_dry=100, min_wet=50, class_tolerance=1.01 / 255, normal_dot_min=.999,
            depth_tolerance=2e-7, plane_levels=[0.0], log=None, bed_gap=.04, bed_tolerance=.02)

    def tearDown(self):
        self.scratch.cleanup()

    def write_capture(self, prefix, normals, depth):
        for name, array, kind in [('normals', normals, 2), ('depth', depth, 1)]:
            data = struct.pack('<3I', self.width, self.height, kind) + array[::-1].astype('<f4').tobytes()
            (self.root / (prefix + '-' + name + '.bin.gz')).write_bytes(gzip.compress(data))
        (self.root / (prefix + '-matrices.json')).write_text(json.dumps(dict(inverse_vp=self.matrix.reshape(-1, order='F').tolist())))

    def write_mask(self, array):
        (self.root / 'opaque.bin').write_bytes(b'C3DOPAQ1' + struct.pack('<3I', 1, self.width, self.height) + array[::-1].tobytes())

    def check_failure(self, message):
        result = GATE.verify(self.args)
        self.assertEqual(result['status'], 'FAIL', result)
        self.assertTrue(any(message in error for error in result['errors']), result['errors'])

    def test_level_wet_bed_and_retained_dry_normals_pass(self):
        result = GATE.verify(self.args)
        self.assertEqual(result['status'], 'PASS', result)
        self.assertEqual(result['dry']['eligible'], 220)
        self.assertGreater(result['wet']['eligible'], 50)
        self.assertEqual(result['reference']['changed_normal_pixels'], 0)

    def test_water_normal_on_dry_cliff_is_rejected(self):
        self.normal[4:10, 3:8] = [.5, 1, .5, 71 / 255]
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('Water normals overwrite 30')

    def test_nonwater_material_relabel_is_rejected(self):
        self.normal[4:10, 3:8, 3] = .95
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('changed_class_pixels: 30')

    def test_wrong_nonwater_normal_is_rejected(self):
        self.normal[4:10, 3:8, :3] = [.5, 1, .5]
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('changed_normal_pixels: 30')

    def test_changed_dry_depth_is_rejected(self):
        self.depth[4:10, 3:8, 0] += .002
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('changed_depth_pixels: 30')

    def test_empty_mask_cannot_vacuously_pass(self):
        self.write_mask(np.zeros_like(self.opaque))
        self.check_failure('Opaque owner mask misses 220')

    def test_inverted_mask_is_rejected(self):
        self.opaque[..., 3] = 255 - self.opaque[..., 3]
        self.write_mask(self.opaque)
        self.check_failure('Opaque owner mask misses 220')

    def test_suppressing_wet_normals_is_rejected(self):
        self.normal[:, 18:, 3] = .88
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('Water normal class is missing')

    def test_relabelling_wet_pixels_as_objects_cannot_hide_failure(self):
        self.normal[:, 18:, 3] = .5
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('Water normal class is missing')

    def test_mask_cannot_hide_reference_visible_wet_pixels(self):
        self.opaque[:, 20:24, 3] = 255
        self.normal[:, 20:24, 3] = .88
        self.write_mask(self.opaque)
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('reference-visible true-wet pixels')

    def test_nonflat_wet_bed_is_rejected(self):
        self.depth[:, 18:, 0] += .05
        self.write_capture('capture', self.normal, self.depth)
        self.check_failure('do not retain a flat water bed')

    def test_missing_reference_and_mask_are_explicit_failures(self):
        self.args.reference = None
        self.args.opaque_mask = None
        self.check_failure('Required opaque owner RT')

    def test_mismatched_camera_is_a_structural_error(self):
        self.matrix[0, 3] += .1
        self.write_capture('reference', self.normal, self.depth)
        with self.assertRaisesRegex(ValueError, 'camera is not identical'):
            GATE.verify(self.args)

    def test_malformed_mask_is_a_structural_error(self):
        self.opaque[0, 0, 0] = 1
        self.write_mask(self.opaque)
        with self.assertRaisesRegex(ValueError, 'zero RGB'):
            GATE.verify(self.args)


if __name__ == '__main__':
    unittest.main()
