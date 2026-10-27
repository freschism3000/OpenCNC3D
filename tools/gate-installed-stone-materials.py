#!/usr/bin/env python3
"""Read-only verification of the mineral data actually installed in a play folder.

Unlike the authoring-cache gate, this reads runtime pack files and the delivered
stone-materials/v01 maps. Retained paired input files are additionally checked
when available; --inputs makes that check mandatory at an explicit location.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from PIL import Image, ImageChops


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def run(runtime, inputs=None, mirror=None):
    runtime = runtime.resolve()
    author = runtime / 'stone-materials/v01'
    manifest = json.loads((author / 'MANIFEST.json').read_text())
    errors, files = [], {}
    retained = Path(manifest['runtime_root']) if manifest.get('runtime_root') else None
    paired = inputs.resolve() if inputs else (retained if retained and retained.is_dir() else None)
    if inputs and not paired.is_dir():
        errors.append('Paired input folder does not exist: ' + str(paired))
        paired = None

    def verify(path, expected, label):
        try:
            actual = sha(path)
            files[str(path.relative_to(runtime))] = actual
            if actual != expected:
                errors.append(label + ': SHA256 differs from delivered material manifest')
            if mirror:
                other = mirror / path.relative_to(runtime)
                if sha(other) != actual:
                    errors.append(label + ': mirrored runtime differs')
            return path.read_bytes()
        except OSError as e:
            errors.append(label + ': ' + str(e))
            return None

    # Include the exact manifest and input signature in mirror parity too.
    verify(author / 'MANIFEST.json', sha(author / 'MANIFEST.json'), 'manifest')
    signature = author / 'input-signature.txt'
    if signature.exists():
        verify(signature, sha(signature), 'input signature')
    terrain = {}
    for bank, row in manifest['terrain'].items():
        material = verify(runtime / ('terrainmaterial_' + bank + '.pack'), row['output_sha256'], bank + ' material')
        surface = verify(runtime / ('terrainsurface_' + bank + '.pack'), row['source_surface_sha256'], bank + ' physical surface')
        if material and surface:
            n = u32(material, 16)
            head = 20 + n * 24
            if (material[:8] != b'C3DMATL\0' or u32(material, 8) != 1
                    or n != u32(surface, 16) or material[20:head] != surface[20:head]
                    or hashlib.sha256(material[20:head]).hexdigest() != row['key_sha256']):
                errors.append(bank + ': physical/material UV keys differ')
            elif len(zlib.decompress(material[head + 4:])) != n * u32(material, 12) ** 2 * 4:
                errors.append(bank + ': invalid material payload size')
            terrain[bank] = n

    for name, row in manifest['scatter'].items():
        data = verify(runtime / name, row['output_sha256'], name + ' mapped scatter')
        if data:
            # Geometry, UV and instance data end before the four texture streams.
            p = 20
            for _ in range(u32(data, 12)):
                nv, nt, _ = struct.unpack_from('<III', data, p)
                p += 20 + nv * 48 + nt * 12
                bones, frames = struct.unpack_from('<II', data, p)
                p += 8
                if bones or frames:
                    errors.append(name + ': unexpected animated rock geometry')
            p += 4 + u32(data, p) * 12
            if hashlib.sha256(data[:p]).hexdigest() != row['geometry_uv_physics_sha256']:
                errors.append(name + ': geometry/UV/physics prefix changed')
        if paired:
            try:
                if sha(paired / name) != row['source_sha256']:
                    errors.append(name + ': retained paired albedo input changed')
            except OSError as e:
                errors.append(name + ': ' + str(e))

    for name, row in manifest['placed'].items():
        verify(runtime / 'ebld' / name, row['source_sha256'], name + ' paired geometry/albedo')
        verify(runtime / 'ebld' / (Path(name).stem + '.spec'), row['specular_sha256'], name + ' specular')
        if paired:
            try:
                if sha(paired / 'ebld' / name) != row['source_sha256']:
                    errors.append(name + ': retained paired albedo input changed')
            except OSError as e:
                errors.append(name + ': ' + str(e))
    verify(runtime / 'stonematerials.pack', manifest['normal_pack']['sha256'], 'placed-rock normal lookup')

    pairs = [pair for row in manifest['terrain'].values() for pair in row.get('templates', [])]
    for group in ('scatter', 'placed'):
        pairs.extend(pair for row in manifest[group].values() for pair in row['textures'])
    for pair in pairs:
        paths = {}
        for channel in ('normal', 'specular', 'player_colour_mask'):
            record = pair[channel]
            path = author / record['file']
            if verify(path, record['sha256'], record['file']):
                paths[channel] = path
        if len(paths) != 3:
            continue
        with Image.open(paths['normal']) as normal, Image.open(paths['specular']) as specular, Image.open(paths['player_colour_mask']) as mask:
            if (list(normal.size) != pair['dimensions'] or specular.size != normal.size
                    or mask.size != normal.size or mask.mode != 'L' or mask.getextrema() != (0, 0)):
                errors.append(pair['normal']['file'] + ': UV dimensions or explicit player mask differ')
            elif ImageChops.difference(normal.getchannel('A'), specular.getchannel('G')).getbbox():
                errors.append(pair['normal']['file'] + ': normal/specular roughness differs')
    return dict(passed=not errors, runtime=str(runtime), mirror=str(mirror) if mirror else None,
                paired_inputs=str(paired) if paired else None, paired_inputs_verified=paired is not None,
                terrain_cells=terrain, scatter_packs=len(manifest['scatter']),
                placed_models=len(manifest['placed']), paired_maps=len(pairs),
                verified_files=len(files), manifest_sha256=sha(author / 'MANIFEST.json'),
                errors=errors, installed_sha256=files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, help='Require exact retained paired bake inputs')
    parser.add_argument('--mirror', type=Path, help='Also require byte-identical files in another installation')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = run(args.runtime, args.inputs, args.mirror.resolve() if args.mirror else None)
    except (OSError, ValueError, KeyError, struct.error, zlib.error) as e:
        result = dict(passed=False, errors=[str(e)])
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'installed_sha256'}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
