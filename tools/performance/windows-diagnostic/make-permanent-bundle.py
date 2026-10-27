#!/usr/bin/env python3
"""Package the automatic diagnostic game entry and its required asset overlay."""
import argparse
import importlib.util
import json
from pathlib import Path
import zipfile

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('diagnostic_bundle', here / 'make-bundle.py')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary-dir', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--base-version', default=(here.parents[2] / 'VERSION').read_text().strip())
    parser.add_argument('--runtime-asset', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    name = 'OpenCNC3D-Diagnostics.exe'
    binary, identity = helpers.inspect_binary(args.binary_dir / name, args.source_commit, args.base_version)
    marker = b'OPENCNC3D|diagnostics|1'
    if marker not in binary:
        raise ValueError('Automatic diagnostic entry marker is absent')
    normal, normal_identity = helpers.inspect_binary(args.binary_dir / 'opencnc3d-game.exe', args.source_commit, args.base_version)
    if marker in normal or identity['build_marks'] != normal_identity['build_marks']:
        raise ValueError('Normal and diagnostic entry identities are inconsistent')
    asset_name, asset, asset_identity = helpers.inspect_runtime_asset(args.runtime_asset)
    readme = '''OpenCNC3D automatic performance diagnostics

Extract this archive into your complete current OpenCNC3D installation, beside
opencnc3d-game.exe. Allow the small included asset overlay to merge into ebld.
Keep your installed game data and libraries. This archive is an add-on, not a
standalone installation or a numbered release.

Double-click OpenCNC3D-Diagnostics.exe. Play normally, reproduce the slowdown,
and exit. Zip the entire new Logs/session-... folder and send it with your report.
No PowerShell, arguments, uploads or graphics-setting changes are needed.
If the installation is read-only, look in
%LOCALAPPDATA%\\OpenCNC3D\\Logs instead.

The executable is optimized and uses your normal missions, saves and settings.
It records frame sections, render workload, driver/display information, settings,
mission events and context around slow frames. Logger overhead and missing data
are identified; GPU timestamps are elapsed spans, not utilization measurements.
A partial session can still be useful after an abnormal exit. Raw console output,
personal paths and player/network identities are excluded. Inspect before sharing.
'''
    manifest = dict(format=1, package_kind='automatic Windows diagnostic add-on',
                    source_commit=args.source_commit, base_version=args.base_version, binary={name: identity},
                    runtime_assets={asset_name: asset_identity},
                    installed_libraries_preserved=True,
                    automatic_entry_marker=marker.decode(), forced_quality_or_vsync_changes=False)
    blobs = {name: binary, asset_name: asset, 'README.txt': readme.encode(),
             'diagnostic-manifest.json': (json.dumps(manifest, indent=2) + '\n').encode()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for member, data in sorted(blobs.items()):
            archive.writestr(member, data)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() or set(archive.namelist()) != set(blobs):
            raise ValueError('ZIP verification failed')
        for member, data in blobs.items():
            if helpers.digest(archive.read(member)) != helpers.digest(data):
                raise ValueError('ZIP member hash mismatch: ' + member)
    result = dict(status='PASS', size=args.output.stat().st_size,
                  sha256=helpers.digest(args.output.read_bytes()), source_commit=args.source_commit,
                  members=sorted(blobs), binary=identity)
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
