#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def function(source, name):
    start = source.index("static bool " + name + "(")
    end = source.index("\n}", start) + 2
    return source[start:end]


def mask(textures, count):
    return sum(1 << texture for texture in set(textures) if 0 <= texture < min(count, 64))


def expected_masks(count, sets, scorch):
    all_textures = (1 << min(count, 64)) - 1
    if len(sets) < 2 or count > 64:
        return [all_textures] * 4

    def drawn(set_index, channel, overlay=False):
        triangles, pieces = sets[set_index]
        result = []
        for first, length in pieces:
            for triangle in triangles[first:first + length]:
                if overlay and (triangle[5] != 0 or triangle[4] == 3):
                    continue
                result.append(triangle[channel])
        return result

    intact = drawn(0, 0)
    held = drawn(1, 0) + drawn(1, 3, overlay=True)
    damaged = drawn(1, 1) + drawn(1, 2)
    wreck = drawn(2, 0) + drawn(2, 2) if len(sets) == 3 else damaged
    return [mask(textures, count) for textures in (intact, held, damaged, wreck + [int(scorch)])]


def emit_case(stream, name, count, sets, scorch=-1):
    encoded = name.encode()
    masks = expected_masks(count, sets, scorch)
    stream.write(struct.pack("<I", len(encoded)) + encoded)
    stream.write(struct.pack("<IIf4Q", count, len(sets), scorch, *masks))
    for triangles, pieces in sets + [([], [])] * (3 - len(sets)):
        stream.write(struct.pack("<I", len(triangles)))
        for triangle in triangles:
            stream.write(struct.pack("<4i2I", *triangle))
    return masks


def assets(stream, directory):
    results = []
    for asset in sorted(directory.glob("*.ebld")):
        with asset.open("rb") as source:
            magic, version, name, count, set_count, emitters, parameters = struct.unpack("<4sI8sIIII", source.read(32))
            if magic != b"EBLD" or version not in (5, 6):
                raise RuntimeError("Unsupported asset: " + str(asset))
            params = struct.unpack("<" + "f" * parameters, source.read(4 * parameters))
            textures = []
            for texture in range(count):
                width, height, grid, frames, fps, role, house, divisor, flags, pad, compressed = struct.unpack("<HH8BI", source.read(16))
                source.seek(compressed, 1)
                textures.append({"role": role, "compressed_bytes": compressed,
                                 "logical_upload_bytes": width * height * 4 * (2 if house & 1 else 1)
                                 * (1 if role == 6 else 4 / 3)})
            sets = []
            for set_index in range(set_count):
                piece_count, triangle_count = struct.unpack("<II", source.read(8))
                pieces = []
                for piece in range(piece_count):
                    record = source.read(76)
                    first, length = struct.unpack_from("<II", record, 64)
                    pieces.append((first, length))
                    source.seek(record[72] * 12, 1)
                triangles = []
                for triangle in range(triangle_count):
                    tex, damaged, glow, overlay, mode, kind, wrap, flags = struct.unpack("<4h4B", source.read(12))
                    triangles.append((tex, damaged, glow, overlay, mode, kind))
                    source.seek(132, 1)
                for first, length in pieces:
                    if first + length > len(triangles):
                        raise RuntimeError("Invalid piece range: " + str(asset))
                sets.append((triangles, pieces))
        masks = emit_case(stream, asset.name, count, sets, params[8])
        results.append({"asset": asset.name, "textures": count, "masks": [f"{value:016x}" for value in masks],
                        "damaged_unneeded_compressed_bytes": sum(texture["compressed_bytes"] for index, texture in enumerate(textures)
                                                                 if not (masks[2] & (1 << index))),
                        "all_compressed_bytes": sum(texture["compressed_bytes"] for texture in textures),
                        "damaged_unneeded_role6": [index for index, texture in enumerate(textures)
                                                   if texture["role"] == 6 and not (masks[2] & (1 << index))]})
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--cxxflag", action="append", default=[])
    parser.add_argument("--reference", default="ecf04129d3bb960c93b3970bb60a0226bae79eb7")
    args = parser.parse_args()
    args.repo = args.repo.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    current = (args.repo / "game/ebld_mod.h").read_text()
    previous = subprocess.check_output(["git", "show", args.reference + ":game/ebld_mod.h"], cwd=args.repo, text=True)
    extracted = "\n".join(function(current, name) for name in ("ebld_resident", "ebld_resident_stage"))
    reference = function(previous, "ebld_resident").replace("ebld_resident(", "reference_resident(")
    header = (args.repo / "game/ebld_texture_mask.h").read_text()
    service_source = (args.repo / "game/building_service.h").read_text()
    service_start = service_source.index("static long bservice_upload(")
    service_end = service_source.index("\n}", service_start) + 2
    service = service_source[service_start:service_end].replace("bservice_upload(", "source_bservice_upload(", 1)
    template = Path(__file__).with_name("stage-test.cpp.in").read_text()
    compiled_source = args.out / "stage-test.cpp"
    compiled_source.write_text(template.replace("EXTRACTED_MASKS", header)
                               .replace("EXTRACTED_SERVICE", service)
                               .replace("EXTRACTED_RESIDENCY", extracted.replace("fopen(", "test_fopen("))
                               .replace("EXTRACTED_REFERENCE", reference.replace("fopen(", "test_fopen(")))
    fixture = args.out / "stages.bin"
    count = 0
    with fixture.open("wb") as stream:
        stream.write(struct.pack("<I", 0))
        for textures in (0, 1, 8, 64, 65):
            for set_count in (1, 2, 3):
                triangle = (0, min(2, textures - 1), min(5, textures - 1), min(4, textures - 1), 0, 0)
                special = (63, -1, -1, 20, 3, 0)
                rim = (-1, -1, -1, 21, 0, 1)
                sets = [([triangle, special, rim], [(0, 3)]) for _ in range(set_count)]
                emit_case(stream, f"synthetic:{textures}:{set_count}", textures, sets, min(7, textures - 1))
                count += 1
        provenance = assets(stream, args.assets.resolve()) if args.assets else []
        count += len(provenance)
        stream.seek(0)
        stream.write(struct.pack("<I", count))
    pixels = args.out / "pixels.rgba"
    pixels.write_bytes(bytes(channel for texture in range(64) for channel in (texture, texture, texture, 255)))
    binary = args.out / ("stage-test.exe" if os.name == "nt" else "stage-test")
    command = [args.compiler, "-std=c++14", "-O2", *args.cxxflag, str(compiled_source), "-o", str(binary)]
    compiled = subprocess.run(command, text=True, capture_output=True)
    (args.out / "compile.log").write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    process = subprocess.run([str(binary), str(fixture), str(pixels)], text=True, capture_output=True)
    (args.out / "run.log").write_text(process.stdout + process.stderr)
    print(process.stdout, end="", flush=True)
    process.check_returncode()
    result = json.loads(process.stdout)
    result.update({"reference": args.reference, "compile_command": command, "assets": provenance,
                   "source_sha256": {name: hashlib.sha256((args.repo / name).read_bytes()).hexdigest()
                                     for name in ("game/ebld_mod.h", "game/ebld_texture_mask.h", "game/building_service.h")},
                   "scope": "Source-extracted residency with stubbed GL, independent piece-range texture census; runtime pixels tested separately"})
    (args.out / "verification.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
