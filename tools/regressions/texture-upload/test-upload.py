#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib


def function(source, name):
    start = source.index("static GLuint " + name + "(")
    end = source.index("\n}", start) + 2
    return source[start:end]


def emit_case(stream, name, width, height, bled, pixels):
    encoded = name.encode("utf-8")
    stream.write(struct.pack("<IIII", len(encoded), width, height, int(bled)))
    stream.write(encoded)
    stream.write(pixels)


def asset_cases(stream, assets):
    count = 0
    provenance = []
    for asset in sorted(assets.glob("*.ebld")):
        data = asset.read_bytes()
        magic, version, name, textures, sets, emitters, parameters = struct.unpack_from("<4sI8sIIII", data)
        if magic != b"EBLD" or version not in (5, 6):
            raise RuntimeError("Unsupported asset: " + str(asset))
        offset = 32 + 4 * parameters
        asset_count = 0
        for texture in range(textures):
            width, height, grid, frames, fps, role, house, divisor, flags, pad, compressed = struct.unpack_from("<HH8BI", data, offset)
            offset += 16
            pixels = zlib.decompress(data[offset:offset + compressed])
            offset += compressed
            size = width * height * 4
            expected = size * (2 if house & 1 else 1) + (width * height if house & 2 else 0)
            if len(pixels) != expected:
                raise RuntimeError("Incorrect decoded size: " + str(asset))
            for variant, first in (("GDI", 0), ("Nod", size)) if house & 1 else (("shared", 0),):
                emit_case(stream, f"{asset.name}:{texture}:{variant}", width, height,
                          bool(flags & 1), pixels[first:first + size])
                count += 1
                asset_count += 1
        provenance.append({"name": asset.name, "sha256": hashlib.sha256(data).hexdigest(), "cases": asset_count})
    return count, provenance


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
    source_paths = ("game/n64fix_mod.h", "game/ebld_mod.h")
    current = [(args.repo / name).read_text() for name in source_paths]
    previous = [subprocess.check_output(["git", "show", args.reference + ":" + name], cwd=args.repo, text=True) for name in source_paths]
    filter_source = (args.repo / "game/fx_filter.h").read_text()
    start = filter_source.index("FX_MAYBE_UNUSED static long fx_bleed_rgba(")
    finish = filter_source.index("\n}", start) + 2
    bleed = filter_source[start:finish].replace("FX_MAYBE_UNUSED ", "", 1)
    extracted = "\n".join((
        bleed,
        function(previous[0], "n64fix_upload").replace("n64fix_upload(", "reference_plain(", 1),
        function(previous[1], "ebld_upload_mips").replace("ebld_upload_mips(", "reference_mips(", 1),
        function(current[0], "n64fix_upload"),
        function(current[1], "ebld_upload_mips"),
    ))
    template = Path(__file__).with_name("upload-test.cpp").read_text()
    compiled_source = args.out / "upload-test.cpp"
    compiled_source.write_text(template.replace("EXTRACTED_FUNCTIONS", extracted))
    fixture = args.out / "textures.bin"
    with fixture.open("wb") as stream:
        stream.write(b"UPTEX001" + struct.pack("<I", 0))
        count = 0
        for width, height in ((1, 1), (1, 17), (19, 1), (8, 8), (63, 65), (128, 64)):
            pixels = bytearray()
            for pixel in range(width * height):
                pixels.extend(((pixel * 37 + 5) % 256, (pixel * 71 + 11) % 256,
                               (pixel * 23 + 17) % 256, 255 if pixel % 7 == 0 else 0))
            emit_case(stream, f"synthetic:{width}x{height}", width, height, False, pixels)
            count += 1
        provenance = []
        if args.assets:
            added, provenance = asset_cases(stream, args.assets.resolve())
            count += added
        stream.seek(8)
        stream.write(struct.pack("<I", count))
    binary = args.out / ("upload-test.exe" if os.name == "nt" else "upload-test")
    command = [args.compiler, "-std=c++14", "-O2", *args.cxxflag, str(compiled_source), "-o", str(binary)]
    compiled = subprocess.run(command, text=True, capture_output=True)
    (args.out / "compile.log").write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    process = subprocess.run([str(binary), str(fixture)], text=True, capture_output=True)
    (args.out / "run.log").write_text(process.stdout + process.stderr)
    print(process.stdout, end="", flush=True)
    process.check_returncode()
    checked = json.loads(process.stdout)
    checked.update({
        "source_sha256": {name: hashlib.sha256((args.repo / name).read_bytes()).hexdigest() for name in source_paths},
        "bleed_sha256": hashlib.sha256(bleed.encode()).hexdigest(),
        "reference": args.reference,
        "compile_command": command,
        "assets": provenance,
        "actual_gl": False,
        "scope": "Exact upload bytes, GL call order/parameters, filter registration and input immutability; no actual driver mip readback",
    })
    (args.out / "verification.json").write_text(json.dumps(checked, indent=2) + "\n")


if __name__ == "__main__":
    main()
