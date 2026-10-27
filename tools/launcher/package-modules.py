#!/usr/bin/env python3
"""Package independently downloadable, exact-version runtime modules.

Inputs are the two release packagers' vetted no-RA folders and the separately staged
RA runtime, never a player's install. No installer, launcher, optional media or user
state is added to a module. Every ZIP has a complete per-file SHA256 manifest; the
catalog binds that manifest and payload with the SHA256 of the whole ZIP.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SHARED_DIRS = frozenset((
    'models3d', 'n64fix', 'ebld', 'eplc', 'evhc', 'einf', 'ecur', 'vfx3d',
    'cinematic', 'foley', 'ambience', 'newaudio',
))
PUBLIC_AUTHORING_DIRS=frozenset(('stone-materials','desert-details','rock-materials'))
RA_LOCAL_MODEL_DIRS=frozenset(('ebld','eplc','evhc'))
TD_DIRS = SHARED_DIRS | {'content', 'missions', 'dosdata'}
RA_DIRS = frozenset(('data', 'audio', 'missions', 'einf', '3d', 'previews', 'ebld',
                     'eplc', 'evhc', 'efx', 'ecur', 'ore'))
RA_ROOT_PACKS = frozenset((
    'clearblend_RA_SNOW.pack', 'groundmask_RA_SNOW.pack',
    'waterbase_RA_TEMPERAT.pack',
    'terrainmaterial_RA_SNOW.pack', 'terrainmaterial_RA_TEMPERAT.pack',
    'terrainsurface_RA_SNOW.pack', 'terrainsurface_RA_TEMPERAT.pack',
))
# RA supports Temperate, Snow and Interior. Its ground reads painted/seam packs
# only under ra3d/; its physical/material readers select RA bank names. Keep TD's
# placeholder Temperate resources, Snow banks, winter rocks and every audio file.
# These exact default TD-only copies are omitted only from the RA download.
RA_UNUSED_TD_PACKS = frozenset((
    'terrainenh_DESERT.pack', 'terrainseam_DESERT.pack', 'clearblend_DESERT.pack',
    'terrainmaterial_DESERT.pack', 'terrainsurface_DESERT.pack',
    'coastmask_DESERT.pack', 'groundmask_DESERT.pack',
    'terrainenh_SAND.pack', 'terrainseam_SAND.pack', 'clearblend_SAND.pack',
    'terrainsurface_SAND.pack', 'coastmask_SAND.pack', 'groundmask_SAND.pack',
    'terrainenh_WINTER.pack', 'terrainseam_WINTER.pack',
    'terrainmaterial_WINTER.pack', 'terrainsurface_WINTER.pack',
    'coastmask_WINTER.pack', 'waterbase_WINTER.pack',
    'foliage3d_DESERT.pack', 'mottle_desert.pack',
))
TD_SHARED_WATER = 'waterbase_RA_TEMPERAT.pack'
TD_PROGRAMS = {
    'macos': {'opencnc3d', 'cnc_eyes', 'TiberianDawn.dylib', 'netcheck',
              'libSDL2-2.0.0.dylib', 'libSDL3.dylib'},
    'windows': {'opencnc3d-game.exe', 'cnc_eyes.exe', 'TiberianDawn.dll',
                'netcheck.exe', 'SDL2.dll', 'OpenCNC3D-Diagnostics.exe'},
}
TD_OPTIONAL_PROGRAMS = {'TiberianDawnXL.dylib', 'TiberianDawnXL.dll'}
PROTECTED_PARTS = frozenset(('movies', 'music', 'saves', 'savegames', 'settings',
                            'user_maps', 'logs', 'shots', 'screenshots', '.git'))


def digest(path: Path) -> str:
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def safe_path(rel: str) -> bool:
    p = PurePosixPath(rel)
    return bool(rel and rel.isascii() and not re.search(r'\s|[\\:]', rel)
                and not p.is_absolute() and str(p) == rel
                and all(x not in ('', '.', '..') for x in p.parts))


def protected(rel: str) -> bool:
    p = PurePosixPath(rel.lower())
    return (any(x in PROTECTED_PARTS or x.startswith('.') for x in p.parts)
            or p.suffix in ('.cfg', '.sav', '.log', '.vqa', '.ocpart')
            or p.name in ('scores.mix', 'transit.mix', 'opencnc3d-media.txt')
            or p.name.startswith(('savegame', 'quicksave', 'autosave')))


def shared(rel: str) -> bool:
    if rel == 'CHANGELOG.txt':
        return True
    p = PurePosixPath(rel)
    if rel in RA_UNUSED_TD_PACKS:return False
    if p.parts[0] in PUBLIC_AUTHORING_DIRS | RA_LOCAL_MODEL_DIRS:return False
    if p.parts[0] in SHARED_DIRS:
        return True
    # The RA frontend reads these shared presentation packs at the install root.
    # Its own missions, menus and native sprites remain under ra3d/.
    return (len(p.parts) == 1 and p.suffix == '.pack'
            and not p.name.startswith(('SC', 'campaign', 'dosclassic', 'dosmenu', 'sidebar'))
            and '_RA_' not in p.name and p.name!='ra-enhanced-hud.pack')


def allowed(rel: str, module: str, platform: str) -> bool:
    if not safe_path(rel) or protected(rel):
        return False
    if rel == 'CHANGELOG.txt':
        return True
    p = PurePosixPath(rel)
    if p.parts[0] in PUBLIC_AUTHORING_DIRS or rel=='ra-enhanced-hud.pack':return False
    if module == 'ra':
        if p.parts[0] != 'ra3d':
            return rel in RA_ROOT_PACKS or shared(rel)
        if len(p.parts) > 2:
            return p.parts[1] in RA_DIRS
        return (len(p.parts) == 2 and (p.suffix == '.pack' or p.parts[1] in {
            'opencnc3d', 'opencnc3d-game.exe', 'RedAlert.dylib', 'RedAlert.dll',
            'libSDL2-2.0.0.dylib', 'libSDL3.dylib', 'SDL2.dll',
            'LICENSE', 'NOTICE.md', 'skirmish-maps.txt'}))
    if p.parts[0] in TD_DIRS:
        return True
    return (len(p.parts) == 1 and
            (rel in TD_PROGRAMS[platform] | TD_OPTIONAL_PROGRAMS | {'CONQUER.MIX', 'temperate_grass_rgb.bin',
                                            'LICENSE', 'NOTICE.md'}
             or (p.suffix == '.pack' and ('_RA_' not in p.name or rel == TD_SHARED_WATER))))


def collect(source: Path, module: str, platform: str, *, prefix: str = '') -> dict[str, Path]:
    result = {}
    for directory, dirs, names in os.walk(source, followlinks=False):
        base = Path(directory)
        # Refuse links before filtering: a stage with a link is not immutable input.
        for name in dirs + names:
            if (base / name).is_symlink():
                raise ValueError(f'symlink in module source: {base / name}')
        dirs[:] = sorted(dirs)
        for name in sorted(names):
            path = base / name
            rel = prefix + path.relative_to(source).as_posix()
            if allowed(rel, module, platform):
                if not path.is_file():
                    raise ValueError(f'not a regular module input: {path}')
                result[rel] = path
    return result


def add_release_notes(files: dict[str, Path]) -> dict[str, Path]:
    """Use the current source release notes for either module's shared root copy."""
    notes = ROOT / 'docs/CHANGELOG.md'
    if notes.is_symlink() or not notes.is_file() or not notes.stat().st_size:
        raise ValueError('missing immutable source release notes: ' + str(notes))
    return dict(files, **{'CHANGELOG.txt': notes})


def write_archive(files: dict[str, Path], output: Path, module: str, platform: str,
                  version: str) -> dict:
    if not files:
        raise ValueError('empty module')
    if not re.fullmatch(r'0\.\d+\.\d+', version):
        raise ValueError('modules require an exact release version, without a build suffix')
    manifest_name = f'opencnc3d-module-{module}.txt'
    if manifest_name in files:
        raise ValueError('input contains its own manifest')
    lines = ['module-files 1', f'module {module}', f'version {version}', f'platform {platform}']
    entries = []
    folded = set()
    for rel, path in sorted(files.items()):
        if not allowed(rel, module, platform) or rel.lower() in folded:
            raise ValueError(f'unsafe, unowned or case-colliding module path: {rel}')
        folded.add(rel.lower())
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'not an immutable regular module file: {path}')
        entry = dict(path=rel, size=path.stat().st_size, sha256=digest(path))
        entries.append(entry)
        lines.append(f"file {entry['sha256']} {entry['size']} {rel}")
    if len(entries) >= 20000 or len('\n'.join(lines).encode('ascii')) >= 4 * 1024 * 1024:
        raise ValueError('module manifest exceeds the native controller limits')
    output.parent.mkdir(parents=True, exist_ok=True)
    part = output.with_suffix('.zip.ocpart')
    try:
        with zipfile.ZipFile(part, 'w', zipfile.ZIP_DEFLATED, compresslevel=9,
                             allowZip64=False) as z:
            for entry in entries:
                path = files[entry['path']]
                info = zipfile.ZipInfo(entry['path'], (2026, 1, 1, 0, 0, 0))
                info.create_system = 3
                mode = 0o644 if entry['path'] == 'CHANGELOG.txt' else stat.S_IMODE(path.stat().st_mode)
                info.external_attr = (stat.S_IFREG | mode) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                # z.open(ZipInfo) uses the entry's level, not ZipFile's default.
                info._compresslevel = 9
                # Stream large packs rather than retaining their bytes in memory.
                with path.open('rb') as src, z.open(info, 'w') as dest:
                    shutil.copyfileobj(src, dest)
            info = zipfile.ZipInfo(manifest_name, (2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            info._compresslevel = 9
            z.writestr(info, '\n'.join(lines) + '\n')
        # Read our actual archive, not only its authoring inputs.
        verify_archive(part, module, platform, version)
        os.replace(part, output)
    finally:
        if part.exists():
            part.unlink()
    return dict(module=module, platform=platform, version=version, asset=output.name,
                size=output.stat().st_size, sha256=digest(output), files=entries)


def verify_archive(path: Path, module: str, platform: str, version: str) -> None:
    manifest_name = f'opencnc3d-module-{module}.txt'
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        if len(names) != len(set(x.lower() for x in names)):
            raise ValueError('duplicate module ZIP path')
        lines = z.read(manifest_name).decode('ascii').splitlines()
        if lines[:4] != ['module-files 1', f'module {module}', f'version {version}',
                         f'platform {platform}']:
            raise ValueError('module manifest identity mismatch')
        listed = set()
        for line in lines[4:]:
            kind, sha, size, rel = line.split()
            if kind != 'file' or not allowed(rel, module, platform) or rel in listed:
                raise ValueError(f'unsafe manifest path: {line}')
            listed.add(rel)
            info = z.getinfo(rel)
            if stat.S_ISLNK(info.external_attr >> 16) or info.is_dir():
                raise ValueError('module ZIP contains a symlink or directory entry')
            with z.open(info) as f:
                actual = hashlib.file_digest(f, 'sha256').hexdigest()
            if info.file_size != int(size) or sha != actual:
                raise ValueError(f'module entry checksum mismatch: {rel}')
        if set(names) != listed | {manifest_name}:
            raise ValueError('module ZIP has unlisted files')


def write_catalog(records: list[dict], output: Path, version: str) -> None:
    expected = {(m, p) for m in ('td', 'ra') for p in ('macos', 'windows')}
    if {(r['module'], r['platform']) for r in records} != expected or len(records) != 4:
        raise ValueError('catalog requires exactly TD and RA for both platforms')
    lines = ['modules 1', f'version {version}']
    for r in sorted(records, key=lambda x: (x['module'], x['platform'])):
        if r['version'] != version:
            raise ValueError('catalog cannot mix release versions')
        path = output.parent / r['asset']
        if path.stat().st_size != r['size'] or digest(path) != r['sha256']:
            raise ValueError('catalog asset changed after verification')
        lines.append(f"module {r['module']} {r['platform']} {r['asset']} {r['size']} {r['sha256']}")
    output.write_text('\n'.join(lines) + '\n', encoding='ascii')


def stage_ra(source: Path, stage: Path, app: Path, brain: Path) -> None:
    """Stage current-source RA for both runtime gates and downloadable packages."""
    if stage.exists() or stage.is_symlink():
        raise ValueError('RA release staging must be a new private directory')
    subprocess.run(['python3', str(ROOT / 'tools/ra3d/stage-runtime.py'),
                    '--source', str(source), '--out', str(stage),
                    '--app', str(app), '--brain', str(brain)], check=True)
    subprocess.run(['python3', str(ROOT / 'menu/bake_ra_enhanced_menu.py'),
                    str(stage / 'enhanced-menu.pack')], check=True)
    (stage / 'enhanced-menu.json').unlink(missing_ok=True)
    subprocess.run(['python3', str(ROOT / 'tools/ra3d/bake-enhanced-hud.py'),
                    str(stage / 'enhanced-hud.pack')], check=True)
    shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE')
    shutil.copy2(ROOT / 'NOTICE.md', stage / 'NOTICE.md')


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--version', required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--macos-package', type=Path, required=True)
    ap.add_argument('--windows-package', type=Path, required=True)
    ap.add_argument('--ra-source', type=Path, required=True)
    ap.add_argument('--ra-presentation', type=Path, required=True,
                    help='fresh clean-main play folder containing RA root surface/mask/material packs')
    ap.add_argument('--macos-app', type=Path, required=True)
    ap.add_argument('--macos-brain', type=Path, required=True)
    ap.add_argument('--windows-app', type=Path, required=True)
    ap.add_argument('--windows-brain', type=Path, required=True)
    ap.add_argument('--source-commit', required=True)
    a = ap.parse_args()
    if (a.ra_source / 'ra3d/data/GENERAL.MIX').is_file():
        a.ra_source = a.ra_source / 'ra3d'
    version = a.version.removeprefix('v')
    if not re.fullmatch(r'[0-9a-f]{40}', a.source_commit):
        ap.error('--source-commit must name the full combined main commit')
    records = []
    notes = add_release_notes({})['CHANGELOG.txt']
    notes_sha, notes_size = digest(notes), notes.stat().st_size
    a.out.mkdir(parents=True, exist_ok=True)
    a.out = a.out.resolve()
    with tempfile.TemporaryDirectory(prefix='opencnc3d-module-', dir=a.out) as scratch:
        scratch = Path(scratch).resolve()
        for platform, package, app, brain in (
            ('macos', a.macos_package, a.macos_app, a.macos_brain),
            ('windows', a.windows_package, a.windows_app, a.windows_brain)):
            if (package / 'ra3d').exists():
                ap.error('main full package contains RA: ' + str(package))
            stamp = (package / 'opencnc3d-install.txt').read_text().splitlines()
            if f'version {version}' not in stamp:
                ap.error('main package version differs from module version')
            mark = f'OPENCNC3D|build|v{version}|cooked'.encode('ascii')
            if mark not in app.read_bytes():
                ap.error('RA frontend is not the current cooked release: ' + str(app))
            td = add_release_notes(collect(package, 'td', platform))
            required = TD_PROGRAMS[platform] | {'CONQUER.MIX', 'dosmenu.pack',
                                               'SCG01EA.pack', 'missions/SCG01EA.INI', 'CHANGELOG.txt'}
            if not required <= td.keys():
                ap.error('incomplete TD package: ' + ', '.join(sorted(required - td.keys())))
            target = a.out / f'OpenCNC3D-module-td-{platform}-v{version}.zip'
            records.append(write_archive(td, target, 'td', platform, version))
            stage = scratch / platform / 'ra3d'
            stage_ra(a.ra_source, stage, app, brain)
            if platform == 'windows':
                # Only this new public copy is stripped; compiler outputs remain
                # the byte-exact references retained by the packaging receipt.
                token = stage / '.opencnc3d-public-stage'
                token.write_bytes(b'OpenCNC3D fresh public package v1\n')
                marker = stage.parent / 'ra-debug-strip-marker.json'
                receipt = a.out / 'windows-ra-debug-strip-receipt.json'
                stripper = str(ROOT / 'tools/release/strip-windows-debug.py')
                subprocess.run(['python3', stripper, 'prepare', '--stage', str(stage),
                    '--reference-root', str(app.resolve().parent), '--marker', str(marker),
                    '--receipt', str(receipt)], check=True)
                subprocess.run(['python3', stripper, 'apply', '--marker', str(marker)], check=True)
                token.unlink()
            ra = add_release_notes({rel: path for rel, path in td.items() if shared(rel)})
            ra.update(collect(stage, 'ra', platform, prefix='ra3d/'))
            # Current-main physical/coverage packs are read at the install root.
            # Painted ground/seams remain exclusively in their required RA-local
            # paths below, where every runtime painted-bank consumer finds them.
            for name in sorted(RA_ROOT_PACKS):
                path = a.ra_presentation / name
                if not path.is_file() or path.is_symlink():
                    ap.error('missing current-main RA presentation dependency: ' + str(path))
                ra[name] = path
            required_ra = {'ra3d/' + app.name, 'ra3d/' + brain.name,
                           'ra3d/SCG01EA.pack', 'ra3d/dosmenu.pack',
                           'ra3d/enhanced-menu.pack', 'ra3d/einf/E1.c3m',
                           'ra3d/einf/E7.c3m', 'ra3d/ebld/BARL.ebld',
                           'ra3d/3d/SCU01EA.pack', 'ra3d/efx/efx_ra.pack',
                           'ra3d/missions/SCU01EA.INI', 'ra3d/audio/RA_LOCAL.MIX',
                           'ra3d/terrainenh_RA_SNOW.pack', 'ra3d/terrainenh_RA_TEMPERAT.pack',
                           'ra3d/terrainseam_RA_SNOW.pack', 'ra3d/terrainseam_RA_TEMPERAT.pack',
                           'efx.pack', 'efx_enh.pack', 'sky.pack', 'vfx3d/claims.txt', 'CHANGELOG.txt'}
            if not required_ra <= ra.keys():
                ap.error('incomplete Enhanced RA package: ' + ', '.join(sorted(required_ra - ra.keys())))
            target = a.out / f'OpenCNC3D-module-ra-{platform}-v{version}.zip'
            records.append(write_archive(ra, target, 'ra', platform, version))
    write_catalog(records, a.out / f'OpenCNC3D-v{version}-modules.txt', version)
    receipt = dict(version=version, source_commit=a.source_commit,
                   ra_runtime_input=str(a.ra_source.resolve()), modules=records)
    if digest(notes) != notes_sha or notes.stat().st_size != notes_size or any(
            not any(f['path'] == 'CHANGELOG.txt' and f['sha256'] == notes_sha and f['size'] == notes_size
                    for f in record['files']) for record in records):
        raise ValueError('release notes changed or are missing from a module')
    receipt['release_notes'] = dict(source='docs/CHANGELOG.md', path='CHANGELOG.txt',
                                    sha256=notes_sha, size=notes_size)
    (a.out / 'module-build-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    for r in records:
        print(f"MODULE|{r['module']}|{r['platform']}|files={len(r['files'])}|bytes={r['size']}|sha256={r['sha256']}")


if __name__ == '__main__':
    main()
