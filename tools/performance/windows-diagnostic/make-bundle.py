#!/usr/bin/env python3
"""Package verified PE32 diagnostic executables and the isolated-copy tester launcher."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

# Exact library fingerprints from both SHA-verified published Windows v0.8.4
# archives. Full and binaries-only packages contain separate valid engine builds.
BASE_LIBRARIES = {
    'TiberianDawn.dll': dict(variants=[
        dict(size=21786119, sha256='f2354af233b08c768b9d44ad5ddb582bb772a227edb4294c14ac9563227eab98', package='windows_full'),
        dict(size=21786119, sha256='db5f35c428aac460dbc25b57170e54ad4e32aaac772500437778d2b36a9ff019', package='windows_bins'),
    ]),
    'SDL2.dll': dict(variants=[
        dict(size=1912320, sha256='06fd117e3eebd18940edddf240838f128a12443e1405bf3af0d660ade28c012e', package='windows_full_and_bins'),
    ]),
}


# Required runtime overlay is limited to the verified Construction Yard faction atlas overlay.
# The selected installation retains its original geometry and source textures.
ALLOWED_RUNTIME_ASSETS = {
    'ebld/FACT.ebld.fclv': dict(size=149365,
        sha256='0793c780ad0dcf0c65550b4250b49cf1212440fc1219a3d6339c0af2dfc0b251'),
}


def inspect_runtime_asset(path):
    relative = 'ebld/' + path.name
    expected = ALLOWED_RUNTIME_ASSETS.get(relative)
    if expected is None:
        raise ValueError('Only ebld/FACT.ebld.fclv is allowed as a diagnostic runtime asset')
    data = path.read_bytes()
    if len(data) != expected['size'] or digest(data) != expected['sha256'] or data[:8] != b'FCLV0001':
        raise ValueError('Factory runtime cache verification failed: expected the approved size and SHA256')
    return relative, data, dict(expected)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect_binary(path, source_commit, base_version="0.8.4"):
    if not re.fullmatch(r'[0-9a-f]{40}', source_commit):
        raise ValueError('Binary provenance requires the exact 40-digit source commit')
    data = path.read_bytes()
    if len(data) < 256 or data[:2] != b'MZ':
        raise ValueError(f'{path.name}: not a Windows PE executable')
    offset = struct.unpack_from('<I', data, 0x3C)[0]
    if offset + 26 > len(data) or data[offset:offset + 4] != b'PE\0\0':
        raise ValueError(f'{path.name}: invalid PE signature')
    machine = struct.unpack_from('<H', data, offset + 4)[0]
    characteristics = struct.unpack_from('<H', data, offset + 22)[0]
    magic = struct.unpack_from('<H', data, offset + 24)[0]
    if machine != 0x14C or magic != 0x10B:
        raise ValueError(f'{path.name}: must retain the shipped i386 PE32 engine ABI')
    if not characteristics & 0x20:
        raise ValueError(f'{path.name}: large-address-aware flag is missing')
    marks = sorted(set(m.decode('ascii') for m in re.findall(rb'OPENCNC3D\|build\|[^\x00\r\n]{1,160}', data)))
    if not marks:
        raise ValueError(f'{path.name}: diagnostic cooked build identity is missing')
    for mark in marks:
        parsed = re.fullmatch(r'OPENCNC3D\|build\|v' + re.escape(base_version) + r'\+([0-9a-f]{7,40})(?:-dirty)?\|cooked', mark)
        if not parsed or not source_commit.startswith(parsed.group(1)):
            raise ValueError(f'{path.name}: diagnostic build identity does not match the provided source commit')
    if b'OPENCNC3D_PERFTRACE' not in data:
        raise ValueError(f'{path.name}: diagnostic trace hook is absent')
    return data, dict(size=len(data), sha256=digest(data), machine='i386',
                      large_address_aware=True, build_marks=marks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary-dir', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-asset', type=Path, action='append', default=[],
                        help='explicit verified FACT.ebld.fclv cache; never other runtime files')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9a-f]{40}', args.source_commit):
        parser.error('--source-commit must be the exact 40-digit committed source revision')
    here = Path(__file__).resolve().parent
    blobs, entries = {}, {}
    for name in ('opencnc3d-game.exe', 'cnc_eyes.exe'):
        blobs['binaries/' + name], entries[name] = inspect_binary(args.binary_dir / name, args.source_commit)
    identities = {tuple(e['build_marks']) for e in entries.values()}
    if len(identities) != 1:
        raise ValueError('Diagnostic executable build identities disagree')
    runtime_assets = {}
    for path in args.runtime_asset:
        relative, data, entry = inspect_runtime_asset(path)
        if relative in runtime_assets:
            raise ValueError('Duplicate diagnostic runtime asset: ' + relative)
        runtime_assets[relative] = entry
        blobs['assets/' + relative] = data
    manifest = dict(format=1, launcher_revision=3, source_commit=args.source_commit,
                    package_kind='Windows performance diagnostic; not a numbered release',
                    base_version='0.8.4', base_libraries=BASE_LIBRARIES,
                    binaries=entries, runtime_assets=runtime_assets, trace_flag='OPENCNC3D_PERFTRACE',
                    forced_quality_or_vsync_changes=False)
    blobs['diagnostic-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    for name in ('Run-Windows-Diagnostic.cmd', 'Run-Windows-Diagnostic.ps1', 'README.txt'):
        data = (here / name).read_bytes()
        if name.endswith('.cmd'):
            data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
        blobs[name] = data
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in sorted(blobs.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(blobs):
            raise ValueError('ZIP verification failed')
        for name, data in blobs.items():
            if digest(archive.read(name)) != digest(data):
                raise ValueError(f'ZIP member hash mismatch: {name}')
    result = dict(file=str(args.output), size=args.output.stat().st_size,
                  sha256=digest(args.output.read_bytes()), source_commit=args.source_commit,
                  member_count=len(blobs), binaries=entries, runtime_assets=runtime_assets)
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
