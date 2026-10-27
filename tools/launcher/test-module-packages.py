#!/usr/bin/env python3
"""Disposable package fixtures: real ZIP manifests, corruption and empty NSIS setup.

No game is compiled or run. NSIS wraps tiny inert fixture files; the Windows
installer still needs its separate native first-launch acceptance check.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('module_packages', ROOT / 'tools/launcher/package-modules.py')
packages = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packages)


class Modules(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cnc3d-module-package-fixture-')
        self.base = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self, name, data=b'fixture payload', mode=0o644):
        p = self.base / 'inputs' / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        p.chmod(mode)
        return p

    def archive(self, module, platform, version='0.9.0'):
        names = (['ra3d/opencnc3d' if platform == 'macos' else 'ra3d/opencnc3d-game.exe',
                  'ra3d/ebld/BARL.ebld', 'ra3d/einf/E7.c3m', 'efx.pack', 'vfx3d/claims.txt']
                 if module == 'ra' else
                 ['opencnc3d' if platform == 'macos' else 'opencnc3d-game.exe',
                  'CONQUER.MIX', 'missions/SCG01EA.INI', 'SCG01EA.pack'])
        files = {n: self.fixture(n, n.encode(), 0o755 if 'opencnc3d' in n else 0o644) for n in names}
        out = self.base / f'OpenCNC3D-module-{module}-{platform}-v{version}.zip'
        return packages.write_archive(files, out, module, platform, version)

    def test_roundtrip_complete_deterministic_manifest_and_executable_mode(self):
        record = self.archive('ra', 'macos')
        path = self.base / record['asset']
        first = path.read_bytes()
        self.assertEqual(record, self.archive('ra', 'macos'))
        self.assertEqual(first, path.read_bytes())
        packages.verify_archive(path, 'ra', 'macos', '0.9.0')
        with zipfile.ZipFile(path) as z:
            self.assertFalse(any(n.endswith('/') for n in z.namelist()))
            self.assertEqual(z.getinfo('ra3d/opencnc3d').external_attr >> 16 & 0o777, 0o755)
            text = z.read('opencnc3d-module-ra.txt').decode('ascii')
            self.assertIn('platform macos\n', text)
            self.assertEqual(len(text.splitlines()) - 4, len(record['files']))
        # Identity is binding, even when all payload bytes are intact.
        with self.assertRaises(ValueError):
            packages.verify_archive(path, 'ra', 'windows', '0.9.0')
        # Extra bytes/files may not bypass the declared ownership inventory.
        with zipfile.ZipFile(path, 'a') as z:
            z.writestr('ra3d/missions/unlisted.INI', b'unlisted')
        with self.assertRaises(ValueError):
            packages.verify_archive(path, 'ra', 'macos', '0.9.0')

    def test_reject_player_state_media_symlinks_and_foreign_ownership(self):
        bad = ['../savegame.sav', '/etc/passwd', 'ra3d/../LICENSE', 'ra3d-fx.cfg',
               'ra3d/missions/user_maps/custom.INI', 'ra3d/movies/INTRO.VQA',
               'ra3d/data/SCORES.MIX', 'ra3d/audio/TRANSIT.MIX',
               'ra3d/settings/custom.txt', 'ra3d/unknown/asset.pack',
               'ra3d/Logs/game.txt', 'ra3d/missions/quicksave.INI',
               'SCG01EA.pack', 'TiberianDawn.dylib', 'opencnc3d-launcher']
        for rel in bad:
            with self.subTest(rel=rel):
                self.assertFalse(packages.allowed(rel, 'ra', 'macos'))
                with self.assertRaises(ValueError):
                    packages.write_archive({rel: self.fixture('good')}, self.base / 'bad.zip',
                                           'ra', 'macos', '0.9.0')
        target = self.fixture('real.c3m')
        link = target.with_name('linked.c3m')
        link.symlink_to(target)
        with self.assertRaises(ValueError):
            packages.write_archive({'ra3d/einf/E7.c3m': link}, self.base / 'link.zip',
                                   'ra', 'macos', '0.9.0')
        with self.assertRaises(ValueError):
            packages.collect(target.parent, 'td', 'macos')
        # A pair safe on a case-sensitive source disk must not collide on Windows.
        with self.assertRaises(ValueError):
            packages.write_archive({'ra3d/data/LOCAL.MIX': target, 'ra3d/data/local.mix': target},
                                   self.base / 'case.zip', 'ra', 'windows', '0.9.0')
        for rel in packages.RA_ROOT_PACKS:
            self.assertTrue(packages.allowed(rel, 'ra', 'macos'))
            self.assertEqual(packages.allowed(rel, 'td', 'macos'), rel == packages.TD_SHARED_WATER)
        self.assertFalse(packages.allowed('groundmask_RA_UNKNOWN.pack', 'ra', 'macos'))

    def test_catalog_requires_both_modules_platforms_and_exact_current_zip_hashes(self):
        records = [self.archive(m, p) for m in ('td', 'ra') for p in ('macos', 'windows')]
        catalog = self.base / 'OpenCNC3D-v0.9.0-modules.txt'
        packages.write_catalog(records, catalog, '0.9.0')
        self.assertEqual(len(catalog.read_text().splitlines()), 6)
        with self.assertRaises(ValueError):
            packages.write_catalog(records[:-1], catalog, '0.9.0')
        with self.assertRaises(ValueError):
            packages.write_catalog(records, catalog, '0.9.1')
        (self.base / records[0]['asset']).write_bytes(b'replaced archive')
        with self.assertRaises(ValueError):
            packages.write_catalog(records, catalog, '0.9.0')

    def test_whole_install_manifest_rejects_module_zip_instead_of_misclassifying_it(self):
        zips = []
        for platform in ('macos', 'windows'):
            path = self.base / f'OpenCNC3D-{platform}-v0.9.0.zip'
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr(f'OpenCNC3D-{platform}-v0.9.0/opencnc3d-install.txt',
                           'version 0.9.0\ndata_id ' + 'a' * 64 + '\n')
            zips.append(path)
        output = self.base / 'main-manifest.txt'
        command = ['sh', str(ROOT / 'tools/launcher/make-manifest.sh'), 'v0.9.0', str(output)]
        subprocess.run(command + list(map(str, zips)), check=True, capture_output=True)
        before = output.read_bytes()
        module = self.archive('ra', 'macos')
        result = subprocess.run(command + list(map(str, zips)) + [str(self.base / module['asset'])],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('separately verified module catalog', result.stderr)
        self.assertEqual(before, output.read_bytes())

    def test_real_nsis_bootstrap_contains_art_but_no_game_or_network_wizard(self):
        if not shutil.which('makensis'):
            self.skipTest('NSIS compiler unavailable; native installer build still required')
        stage = self.base / 'windows-package'
        fixture_files = ['OpenCNC3D.exe', 'SDL2.dll', 'READ-ME-WINDOWS.txt', 'LICENSE',
                         'NOTICE.md', 'dosmenu.pack', 'enhanced-menu.pack', 'ecur/ecur.bin']
        for rel in fixture_files:
            p = stage / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'/api/builds inert bootstrap fixture\n')
        # A main full package can contain game files; Setup must not copy them.
        (stage / 'TiberianDawn.dll').write_bytes(b'do not embed game')
        private_stage = ROOT / 'build' / ('fixture-empty-setup-' + uuid.uuid4().hex)
        env = dict(os.environ, OPENCNC3D_SETUP_STAGE=str(private_stage),
                   OPENCNC3D_WIN_ZIP=str(self.base / 'setup-output'))
        try:
            result = subprocess.run(['sh', str(ROOT / 'tools/win/make-installer-win.sh'), str(stage)],
                                    env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('SETUP|empty_launcher=1|automatic_download=0|files=9|', result.stdout)
            actual = {p.relative_to(private_stage / 'payload').as_posix()
                      for p in (private_stage / 'payload').rglob('*') if p.is_file()}
            self.assertEqual(actual, set(fixture_files) | {'opencnc3d.ico'})
            outputs = list((self.base / 'setup-output').glob('OpenCNC3DInstaller-windows-*.exe'))
            self.assertEqual(len(outputs), 1)
            self.assertGreater(outputs[0].stat().st_size, 0)
            script = (ROOT / 'tools/win/installer/opencnc3d.nsi').read_text()
            self.assertNotIn('--install-game', script)
            self.assertIn('opencnc3d-module-removal.txt', script)
            self.assertIn('!ifdef MODULE\n  Page custom', script)
            self.assertNotIn('RMDir /r', script)
        finally:
            shutil.rmtree(private_stage, ignore_errors=True)

    def test_complete_nsis_variants_embed_verified_game_and_media_choices(self):
        if not shutil.which('makensis'):
            self.skipTest('NSIS compiler unavailable')
        stage = self.base / 'complete-fixture'
        for rel in ('OpenCNC3D.exe', 'SDL2.dll', 'READ-ME-WINDOWS.txt', 'LICENSE',
                    'NOTICE.md', 'dosmenu.pack', 'enhanced-menu.pack', 'ecur/ecur.bin'):
            p = stage / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'/api/builds inert fixture\n')
        version = (ROOT / 'VERSION').read_text().strip()
        env = dict(os.environ, OPENCNC3D_WIN_ZIP=str(self.base / 'complete-output'))
        # The project's art bakers use Pillow. The Mac has its maintained art
        # interpreter even when a newer shell Python has no art dependencies.
        if Path('/usr/local/bin/python3.12').is_file():
            env['OPENCNC3D_ART_PYTHON'] = '/usr/local/bin/python3.12'
        for module, prefix in (('td', 'OpenCNC3DComplete'), ('ra', 'RA3DComplete')):
            record = self.archive(module, 'windows', version)
            private_stage = ROOT / 'build' / ('fixture-complete-setup-' + uuid.uuid4().hex)
            env['OPENCNC3D_SETUP_STAGE'] = str(private_stage)
            try:
                result = subprocess.run(['sh', str(ROOT / 'tools/win/make-installer-win.sh'),
                                         str(stage), module, str(self.base / record['asset'])],
                                        env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f'SETUP|module={module}|embedded_verified=1|media_checkboxes=2|files=10|', result.stdout)
                self.assertIn('--media-game ${MEDIA_GAME} --media-add-only',
                              (ROOT / 'tools/win/installer/opencnc3d.nsi').read_text())
                self.assertIn('--media-game ${MEDIA_GAME} --media-add-only',
                              (ROOT / 'tools/win/installer/media-page.nsh').read_text())
                output = self.base / 'complete-output' / f'{prefix}-windows-v{version}.exe'
                self.assertGreater(output.stat().st_size, 0)
                if module == 'ra':
                    art = json.loads((private_stage / 'ra-wizard.json').read_text())
                    self.assertEqual(art['dimensions'], [164, 314])
                    self.assertEqual(len(art['sources']), 2)
            finally:
                shutil.rmtree(private_stage, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
