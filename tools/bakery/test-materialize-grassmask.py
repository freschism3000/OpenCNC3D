#!/usr/bin/env python3
"""Exercise pinned production bytes, atomic refusal and source-version updates."""
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('grassmask_source', REPO / 'tools/bakery/materialize-grassmask.py')
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


def raw_identity(raw):
    return {'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}


class MaterializerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cnc3d-grassmask-test-')
        self.root = Path(self.temporary.name)
        (self.root / 'game').mkdir()

    def tearDown(self):
        self.temporary.cleanup()

    def write_version(self, raw, previous=()):
        packed = gzip.compress(raw, compresslevel=9, mtime=0)
        (self.root / HELPER.SOURCE).write_bytes(packed)
        value = {'format': 'opencnc3d-compressed-asset', 'version': 1,
                 'asset': HELPER.ASSET, 'source': HELPER.SOURCE,
                 'raw': raw_identity(raw), 'gzip': raw_identity(packed),
                 'previous_approved_raw': list(previous)}
        (self.root / HELPER.MANIFEST).write_text(json.dumps(value))
        return value

    def assert_no_temporary(self):
        self.assertFalse(list((self.root / 'game').glob('.grassmask-unpack-*')))
        self.assertFalse(list((self.root / 'game').glob('.grassmask-record-*')))

    def test_actual_production_source_exact_repeat_and_clean_checkout(self):
        for name in (HELPER.SOURCE, HELPER.MANIFEST):
            shutil.copyfile(REPO / name, self.root / name)
        (self.root / '.gitignore').write_text('/' + HELPER.ASSET + '\n/' + HELPER.RECORD + '\n')
        for args in (['init', '-q'], ['add', '.'], ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.test', 'commit', '-qm', 'source']):
            subprocess.run(['git', '-C', str(self.root), *args], check=True)
        self.assertEqual(HELPER.materialize(self.root), 'materialized')
        value = json.loads((self.root / HELPER.MANIFEST).read_text())
        self.assertEqual(HELPER.identity(self.root / HELPER.ASSET), value['raw'])
        self.assertEqual(value['raw']['sha256'], '1161d0e48d636b288b03b0b67e0350c8cc1369950237d973ec119ba29f04ea55')
        before = [(self.root / n).stat() for n in (HELPER.ASSET, HELPER.RECORD)]
        self.assertEqual(HELPER.materialize(self.root), 'verified')
        self.assertEqual(HELPER.materialize(self.root, check=True), 'verified')
        after = [(self.root / n).stat() for n in (HELPER.ASSET, HELPER.RECORD)]
        self.assertEqual([(p.st_ino, p.st_mtime_ns) for p in before], [(p.st_ino, p.st_mtime_ns) for p in after])
        status = subprocess.check_output(['git', '-C', str(self.root), 'status', '--porcelain'])
        self.assertEqual(status, b'', 'raw/provenance must not dirty source or build version')
        self.assert_no_temporary()

    def test_actual_gzip_encoding_is_deterministic(self):
        value = json.loads((REPO / HELPER.MANIFEST).read_text())
        encoded = io.BytesIO()
        with gzip.GzipFile(filename='', mode='wb', fileobj=encoded, compresslevel=9, mtime=0) as destination:
            with gzip.open(REPO / HELPER.SOURCE, 'rb') as source:
                shutil.copyfileobj(source, destination, 1024 * 1024)
        self.assertEqual(raw_identity(encoded.getvalue()), value['gzip'])
        self.assertEqual(encoded.getvalue(), (REPO / HELPER.SOURCE).read_bytes())

    def test_unknown_override_and_forged_provenance_cannot_authorize_overwrite(self):
        self.write_version(b'approved')
        output = self.root / HELPER.ASSET
        output.write_bytes(b'local artist override')
        record = self.root / HELPER.RECORD
        record.write_text(json.dumps({'format': 1, 'raw': raw_identity(output.read_bytes())}))
        before = (output.read_bytes(), record.read_bytes())
        with self.assertRaisesRegex(ValueError, 'local grass-mask override preserved'):
            HELPER.materialize(self.root)
        self.assertEqual((output.read_bytes(), record.read_bytes()), before)
        self.assert_no_temporary()

    def test_explicit_reviewed_prior_version_updates_atomically(self):
        self.write_version(b'previous approved')
        HELPER.materialize(self.root)
        output = self.root / HELPER.ASSET
        old, inode = HELPER.identity(output), output.stat().st_ino
        self.write_version(b'new approved', previous=[old])
        self.assertEqual(HELPER.materialize(self.root), 'materialized')
        self.assertEqual(output.read_bytes(), b'new approved')
        self.assertNotEqual(output.stat().st_ino, inode)
        self.assertEqual(HELPER.materialize(self.root), 'verified')
        self.assert_no_temporary()

    def test_prior_record_without_manifest_authorization_is_refused(self):
        self.write_version(b'previous approved')
        HELPER.materialize(self.root)
        self.write_version(b'new approved')
        with self.assertRaisesRegex(ValueError, 'override preserved'):
            HELPER.materialize(self.root)
        self.assertEqual((self.root / HELPER.ASSET).read_bytes(), b'previous approved')

    def test_corrupt_compressed_source_keeps_previous_raw_and_record(self):
        self.write_version(b'previous approved')
        HELPER.materialize(self.root)
        before = [(self.root / name).read_bytes() for name in (HELPER.ASSET, HELPER.RECORD)]
        (self.root / HELPER.SOURCE).write_bytes(b'broken gzip')
        with self.assertRaisesRegex(ValueError, 'compressed grass-mask source'):
            HELPER.materialize(self.root)
        self.assertEqual([(self.root / name).read_bytes() for name in (HELPER.ASSET, HELPER.RECORD)], before)
        self.assert_no_temporary()

    def test_wrong_decompressed_hash_never_publishes_partial_version(self):
        self.write_version(b'previous approved')
        HELPER.materialize(self.root)
        previous = HELPER.identity(self.root / HELPER.ASSET)
        manifest = self.write_version(b'new approved', previous=[previous])
        manifest['raw'] = raw_identity(b'bad approved')
        (self.root / HELPER.MANIFEST).write_text(json.dumps(manifest))
        before = [(self.root / name).read_bytes() for name in (HELPER.ASSET, HELPER.RECORD)]
        with self.assertRaisesRegex(ValueError, 'decompressed grass-mask SHA/size'):
            HELPER.materialize(self.root)
        self.assertEqual([(self.root / name).read_bytes() for name in (HELPER.ASSET, HELPER.RECORD)], before)
        self.assert_no_temporary()

    def test_overlong_decompression_is_bounded_and_preserves_output(self):
        manifest = self.write_version(b'large' * 1000)
        manifest['raw'] = raw_identity(b'small')
        (self.root / HELPER.MANIFEST).write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'exceeds pinned size'):
            HELPER.materialize(self.root)
        self.assertFalse((self.root / HELPER.ASSET).exists())
        self.assert_no_temporary()

    @unittest.skipUnless(hasattr(os, 'symlink'), 'symlinks unavailable')
    def test_symlink_output_source_and_provenance_are_refused(self):
        for name in (HELPER.ASSET, HELPER.SOURCE, HELPER.RECORD):
            with self.subTest(name=name):
                self.write_version(b'approved')
                output = self.root / name
                output.unlink(missing_ok=True)
                victim = self.root / 'external'
                victim.write_bytes(b'external authoring')
                output.symlink_to(victim)
                with self.assertRaisesRegex(ValueError, 'nonregular or symlink'):
                    HELPER.materialize(self.root)
                self.assertEqual(victim.read_bytes(), b'external authoring')
                output.unlink()

    def test_actual_mac_full_code_only_and_windows_entries_refuse_before_build(self):
        helper = self.root / 'tools/bakery/materialize-grassmask.py'
        helper.parent.mkdir(parents=True)
        shutil.copyfile(REPO / 'tools/bakery/materialize-grassmask.py', helper)
        self.write_version(b'approved')
        output = self.root / HELPER.ASSET
        output.write_bytes(b'unique local override')
        for name, code_only in [('game/build.sh', '0'), ('game/build.sh', '1'),
                                ('game/make-build.sh', '1'), ('tools/win/build-win.sh', '1')]:
            with self.subTest(entry=name, code_only=code_only):
                target = self.root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(REPO / name, target)
                result = subprocess.run(['sh', str(target)], env=dict(os.environ, OPENCNC3D_CODE_ONLY=code_only), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Grass-mask source refused: local grass-mask override preserved', result.stdout)
                self.assertEqual(output.read_bytes(), b'unique local override')
        self.assert_no_temporary()


if __name__ == '__main__':
    unittest.main(verbosity=2)
