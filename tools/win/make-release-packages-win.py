#!/usr/bin/env python3
"""Assemble the five Windows release products without compiling a second game.

The base stage and independently verified module ZIPs must be from the same clean
release build. The RA Complete installer is a separate, explicitly selected game;
ordinary OpenCNC3D installers/binaries still contain no RA module payload.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('modules', ROOT / 'tools/launcher/package-modules.py')
modules = importlib.util.module_from_spec(spec)
spec.loader.exec_module(modules)


def archive(path, entries):
    """Write ordered flat/relative files, preserving executability and tail order."""
    part = path.with_suffix('.zip.ocpart')
    with zipfile.ZipFile(part, 'w', zipfile.ZIP_DEFLATED, compresslevel=6,
                         allowZip64=False) as z:
        for name, data in entries:
            if not modules.safe_path(name) or modules.protected(name):
                raise ValueError('unsafe binary update path: ' + name)
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | (0o755 if name.endswith(('.exe', '.dll')) else 0o644)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    with zipfile.ZipFile(part) as z:
        if z.testzip() or z.namelist() != [name for name, _ in entries]:
            raise ValueError('binary update ZIP did not round-trip')
    os.replace(part, path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--version', required=True)
    p.add_argument('--base-stage', type=Path, required=True)
    p.add_argument('--module-dir', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    version = a.version.removeprefix('v')
    if version != (ROOT / 'VERSION').read_text().strip():
        p.error('installer source VERSION differs from the packaged release')
    a.out.mkdir(parents=True, exist_ok=True)
    tag = 'v' + version
    td = a.module_dir / f'OpenCNC3D-module-td-windows-{tag}.zip'
    ra = a.module_dir / f'OpenCNC3D-module-ra-windows-{tag}.zip'
    for code, path in [('td', td), ('ra', ra)]:
        modules.verify_archive(path, code, 'windows', version)
    mark = f'OPENCNC3D|build|v{version}|cooked'.encode()
    for name in ('cnc_eyes.exe', 'opencnc3d-game.exe', 'OpenCNC3D-Diagnostics.exe'):
        if mark not in (a.base_stage / name).read_bytes():
            p.error('stale or uncooked Windows input: ' + name)
    with zipfile.ZipFile(ra) as z:
        if mark not in z.read('ra3d/opencnc3d-game.exe'):
            p.error('RA module frontend differs from the release version')

    # Three separately named installers. Only the Complete variants embed one
    # verified module; the native Module Browser installer remains empty.
    env = dict(os.environ, OPENCNC3D_WIN_ZIP=str(a.out.resolve()))
    for code, source in [('', None), ('td', td), ('ra', ra)]:
        env['OPENCNC3D_SETUP_STAGE'] = str(ROOT / 'build' / ('win-setup-' + (code or 'empty')))
        command = ['sh', str(ROOT / 'tools/win/make-installer-win.sh'), str(a.base_stage)]
        if code:
            command += [code, str(source)]
        subprocess.run(command, env=env, check=True)

    names = ['OpenCNC3D.exe', 'opencnc3d-game.exe', 'OpenCNC3D-Diagnostics.exe',
             'cnc_eyes.exe', 'netcheck.exe', 'TiberianDawn.dll', 'dosclassic.pack',
             'dosmenu.pack', 'enhanced-menu.pack', 'decalenh.pack', 'CONQUER.MIX']
    entries = [(name, (a.base_stage / name).read_bytes()) for name in names]
    entries += [('READ-ME-FIRST.txt', (f'OpenCNC3D binaries {tag}.\n'
                 'Copy over a matching complete Dawn3D installation. Add Game installs\n'
                 'the complete module. Saves and settings are not included.\n').encode()),
                ('BUILD-ID.txt', (tag + ' windows\n').encode()),
                ('SDL2.dll', (a.base_stage / 'SDL2.dll').read_bytes())]
    archive(a.out / f'OpenCNC3DBinaries-windows-{tag}.zip', entries)

    # RA owns its frontend, brain and SDL under ra3d/. Shared launcher files let
    # an existing RA installation acquire the current module/update controller.
    with zipfile.ZipFile(ra) as z:
        entries = [(name, z.read(name)) for name in
                   ['ra3d/opencnc3d-game.exe', 'ra3d/RedAlert.dll', 'ra3d/SDL2.dll']]
    entries += [(name, (a.base_stage / name).read_bytes()) for name in
                ['OpenCNC3D.exe', 'SDL2.dll', 'dosmenu.pack', 'enhanced-menu.pack', 'ecur/ecur.bin']]
    entries += [('READ-ME-RA3D-BINARIES.txt', (f'RA3D binaries {tag}.\n'
                 'Copy over a matching complete RA3D installation. Add Game installs\n'
                 'or updates the verified full module. This archive contains executables\n'
                 'and launcher drawing assets, without game data, saves or settings.\n').encode()),
                ('ra3d/BUILD-ID.txt', (tag + ' windows RA3D\n').encode())]
    archive(a.out / f'RA3DBinaries-windows-{tag}.zip', entries)
    expected = [f'{name}-windows-{tag}.{ext}' for name, ext in (
        ('OpenCNC3DInstaller', 'exe'), ('OpenCNC3DComplete', 'exe'),
        ('RA3DComplete', 'exe'), ('OpenCNC3DBinaries', 'zip'), ('RA3DBinaries', 'zip'))]
    receipt = dict(version=version, platform='windows', native_execution_verified=False,
                   assets=[dict(name=name, size=(a.out / name).stat().st_size,
                                sha256=modules.digest(a.out / name)) for name in expected])
    (a.out / 'windows-package-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('RELEASE-PACKAGES|windows|count=5|version=' + version)


if __name__ == '__main__':
    main()
