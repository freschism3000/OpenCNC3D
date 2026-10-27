import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import storage_cleanup as guard

def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, TOOLS/filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

generated = load('generated', 'clean-generated.py')
showcase = load('showcase', 'clean-showcase.py')
session = load('session', 'clean-session-temp.py')

class Idle:
    def busy(self, root):
        return False

class StorageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir='/private/tmp', prefix='cnc3d-retention-test-')
        self.root = Path(self.temp.name)
        self.registry = self.root/'registry.json'
        self.cache = self.root/'cache.json'
        self.now = time.time() + 3*86400

    def tearDown(self):
        self.temp.cleanup()

    def write(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())

    def native(self, newer_pass=True):
        roots = []
        for name, passed, finished in [('20261019-120000-aaaaaaaa', True, '2026-10-19T12:00:00+00:00'),
                                       ('20261020-120000-bbbbbbbb', newer_pass, '2026-10-20T12:00:00+00:00')]:
            run = self.root/'cooks'/name
            self.write(run/'result.json', {'pass': passed, 'status': 'passed' if passed else 'failed',
                       'production_inputs_unchanged': True, 'finished_utc': finished})
            self.write(run/'Cooked/Content/A.uasset', b'metadata')
            self.write(run/'Cooked/Content/A.uexp', b'payload')
            roots.append(run/'Cooked')
        self.write(self.registry, {'schema': 1, 'groups': [{'id': 'test', 'kind': 'native-cook',
                   'root': str(self.root/'cooks'), 'min_idle_hours': 24}]})
        return roots

    def clean(self, apply=True):
        with patch.object(generated, 'Activity', Idle), patch.object(generated.time, 'time', return_value=self.now):
            return generated.run(self.registry, apply, self.cache)

    def test_duplicate_package_cloned_and_unique_retained(self):
        old, new = self.native()
        self.write(old/'Content/Unique.uasset', b'only copy')
        report = self.clean()
        self.assertEqual(report['errors'], [])
        self.assertEqual(report['deduplicated_files'], 2)
        p, q = old/'Content/A.uexp', new/'Content/A.uexp'
        self.assertNotEqual(p.stat().st_ino, q.stat().st_ino)
        self.assertEqual(p.read_bytes(), q.read_bytes())
        p.write_bytes(b'independent later edit')
        self.assertEqual(q.read_bytes(), b'payload')
        self.assertEqual((old/'Content/Unique.uasset').read_bytes(), b'only copy')

    def test_changed_sidecar_keeps_entire_package(self):
        old, new = self.native()
        self.write(new/'Content/A.uexp', b'new-data')
        inode = (old/'Content/A.uasset').stat().st_ino
        self.assertEqual(self.clean()['deduplicated_files'], 0)
        self.assertEqual((old/'Content/A.uasset').stat().st_ino, inode)

    def test_failed_newest_run_is_not_a_survivor(self):
        self.native(newer_pass=False)
        self.assertEqual(self.clean()['deduplicated_files'], 0)

    def test_active_output_kept(self):
        old, _ = self.native()
        with patch.object(Idle, 'busy', return_value=True):
            self.assertEqual(self.clean()['deduplicated_files'], 0)
        self.assertTrue((old/'Content/A.uexp').exists())

    def test_activity_failure_is_fail_closed(self):
        old, _ = self.native()
        with patch.object(generated, 'Activity', side_effect=RuntimeError('lsof unavailable')):
            with self.assertRaises(RuntimeError):
                generated.run(self.registry, True, self.cache)
        self.assertEqual((old/'Content/A.uexp').read_bytes(), b'payload')

    def test_dry_run_does_not_clone(self):
        old, _ = self.native()
        inode = (old/'Content/A.uexp').stat().st_ino
        self.assertEqual(self.clean(False)['deduplicated_files'], 0)
        self.assertEqual((old/'Content/A.uexp').stat().st_ino, inode)
        self.assertFalse(self.cache.exists())

    def test_unchanged_pair_cached_without_rehashing(self):
        self.native()
        self.clean()
        with patch.object(generated, 'sha', side_effect=RuntimeError('should not rehash')):
            report = self.clean()
        self.assertEqual(report['errors'], [])
        self.assertEqual(report['cached_files'], 2)
        self.assertEqual(report['deduplicated_files'], 0)

    def test_recent_outputs_kept(self):
        self.native()
        self.now = time.time()
        self.assertEqual(self.clean()['deduplicated_files'], 0)

    def test_clone_failure_keeps_original(self):
        old, _ = self.native()
        inode = (old/'Content/A.uexp').stat().st_ino
        with patch.object(generated, 'independent_clone', side_effect=OSError('unsupported filesystem')):
            report = self.clean()
        self.assertTrue(report['errors'])
        self.assertEqual((old/'Content/A.uexp').stat().st_ino, inode)

    def test_path_escape_and_symlinks_refused(self):
        with self.assertRaises(ValueError):
            guard.child(self.root, '../outside')
        (self.root/'link').symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            guard.child(self.root, 'link/new-file')

    def test_missing_completion_proof_keeps_pairs(self):
        self.write(self.registry, {'schema': 1, 'groups': [{'id':'bad','kind':'verified-pairs',
                   'root':str(self.root),'receipts':[],'pairs':[]}]})
        self.assertTrue(self.clean()['errors'])

    def test_changed_receipt_stops_mutation(self):
        old, new = self.native()
        original = generated.sha
        changed = False
        def modify(path):
            nonlocal changed
            result = original(path)
            if not changed:
                changed = True
                self.write(old.parent/'result.json', {'status':'running'})
            return result
        with patch.object(generated, 'sha', side_effect=modify):
            report = self.clean()
        self.assertTrue(report['errors'])
        self.assertEqual(report['deduplicated_files'], 0)

    def test_lock_prevents_overlap(self):
        with patch.dict(os.environ, {'CNC3D_CLEANUP_LOCK': str(self.root/'lock')}):
            with guard.cleanup_lock():
                with self.assertRaises(BlockingIOError):
                    with guard.cleanup_lock():
                        self.fail('second cleanup entered')

    def test_temp_requires_unique_work_attestation_and_72_idle_hours(self):
        folder=self.root/'session';folder.mkdir()
        data={'owner':'OpenCNC3D','completed':True,'disposable':True,'kind':'session'}
        self.write(folder/'.cnc3d-cleanup.json',data)
        row=session.inspect(folder,self.now,[], '')
        self.assertIn('unique',row['keep'])
        data['unique_authoring_files']=False
        self.write(folder/'.cnc3d-cleanup.json',data)
        self.assertIn('72',session.inspect(folder,time.time(),[], '')['keep'])
        self.assertTrue(session.inspect(folder,self.now+86400,[], '')['eligible'])
        self.write(folder/'unique.blend',b'authored model')
        self.assertIn('authoring',session.inspect(folder,self.now+86400,[], '')['keep'])

    def test_busy_process_alias_and_open_files_detected(self):
        from types import SimpleNamespace
        root=self.root/'job';root.mkdir()
        snapshot=SimpleNamespace(stdout='p100\nn'+str(root/'file.o')+'\n')
        processes=SimpleNamespace(stdout='100 1 worker\n')
        with patch.object(guard.subprocess,'run',side_effect=[snapshot,processes]):
            self.assertTrue(guard.Activity().busy(root))
        snapshot=SimpleNamespace(stdout='p100\nn'+str(self.root/'unrelated')+'\n')
        alias=str(root).replace('/private/tmp/','/tmp/')
        processes=SimpleNamespace(stdout='100 1 worker --out='+alias+'/file.o\n')
        with patch.object(guard.subprocess,'run',side_effect=[snapshot,processes]):
            self.assertTrue(guard.Activity().busy(root))

    def film(self):
        r = self.root/'films'
        self.write(r/'storage-retention.json', {'protected_frame_roots':['v01-production/frames/active'],
                   'retired_versions':['v01-production']})
        video = self.root/'delivered.mp4';self.write(video,b'verified final video')
        self.write(r/'v01-production/delivery-receipt.json', {'verified':True,'exports':[
                   {'desktop_path':str(video),'sha256':guard.sha(video)}]})
        for n in ['f00001.ppm','f00002.png','contact-sheet.png','active/f00003.ppm']:
            self.write(r/'v01-production/frames'/n,b'frame')
        self.write(r/'v01-production/art/frames/f00004.ppm',b'original art')
        self.write(r/'v02-production/frames/f00001.ppm',b'current')
        outside=self.root/'unique.ppm';self.write(outside,b'unique')
        (r/'v01-production/frames/f00005.ppm').symlink_to(outside)
        return r

    def clean_film(self, root):
        args=argparse.Namespace(root=root,retire=None,apply=True)
        with patch.object(showcase,'Activity',Idle):
            return showcase.execute(args)

    def test_retired_png_ppm_only_and_protections(self):
        root=self.film()
        self.assertFalse(self.clean_film(root))
        self.assertFalse((root/'v01-production/frames/f00001.ppm').exists())
        for name in ['contact-sheet.png','active/f00003.ppm','f00005.ppm']:
            self.assertTrue((root/'v01-production/frames'/name).exists())
        self.assertTrue((root/'v01-production/art/frames/f00004.ppm').exists())
        self.assertTrue((root/'v02-production/frames/f00001.ppm').exists())
        self.assertTrue((self.root/'unique.ppm').exists())

    def test_bad_video_hash_keeps_frames(self):
        root=self.film();self.write(self.root/'delivered.mp4',b'changed video')
        self.assertTrue(self.clean_film(root))
        self.assertTrue((root/'v01-production/frames/f00001.ppm').exists())

    def test_active_film_keeps_frames(self):
        root=self.film()
        with patch.object(Idle,'busy',return_value=True):
            self.assertFalse(self.clean_film(root))
        self.assertTrue((root/'v01-production/frames/f00001.ppm').exists())

if __name__ == '__main__':
    unittest.main()
