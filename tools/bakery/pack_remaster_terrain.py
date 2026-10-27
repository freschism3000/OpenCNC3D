#!/usr/bin/env python3
"""Pack the Enhanced terrain textures for the game: two files per theater.

    python3 tools/bakery/pack_remaster_terrain.py [out_dir] [--cell 30]

Reads art/remaster/textures/<THEATER>/set_144.json and each template's final_144.png and
seam_144/ strips (the approved remasters at 144 texels a cell, written there by
tools/art/remaster/textures/tex.py export) and writes, default out_dir being each
theater's own folder:

    <out_dir>/terrainenh_<THEATER>.pack    the cells, which game/remaster_tex.h reads
    <out_dir>/terrainseam_<THEATER>.pack   the join blend's strips, which
                                           game/terrain_seam.h reads
                                           (tools/bakery/pack_terrain_seams.py)

With --cell 30 it packs the first set, set.json and final.png at 30 texels a cell, into
terrain30_<THEATER>.pack as before (that set has no strips).

    "C3DT30\\0\\0"  u32 version (1)  u32 cell  u32 count
    count x (u16 template number, u16 icon)
    count x cell*cell*3 bytes of RGB, in the same order

The template number is the engine's TemplateType; the icon is the cell's index within
its template (row-major), and for the plain ground (CLEAR1) the icon the engine picks by
map position, (x & 3) | ((y & 3) << 2), from the 4x4 field.

A template's VARIANTS (other paintings of the same piece, set_144.json "variants") are
packed as more cells of the same template, the icon field carrying, above the icon's own
eight bits, the variant (bits 8-9, 1 or 2) and the template's size in cells, less one
(width bits 10-12, height bits 13-15), which the game needs to tell one placement from the
next. A reader that knows no variants looks such an icon up, finds no tile, and skips it.
"""
import json
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack_terrain_seams  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "art" / "remaster" / "textures"
MAGIC = b"C3DT30\0\0"
VERSION = 1


def variant_icon(icon: int, n: int, w: int, h: int) -> int:
    """The icon field of variant n of a w x h template's cell (see the format above)."""
    if not (0 <= icon < 256 and 1 <= n <= 3 and 1 <= w <= 8 and 1 <= h <= 8):
        raise SystemExit(f"variant {n} of icon {icon} of a {w}x{h} template does not fit the key")
    return icon | (n << 8) | ((w - 1) << 10) | ((h - 1) << 13)


def pack(theater_dir: Path, out: Path, cell_size: int = 30) -> int:
    set_name = "set.json" if cell_size == 30 else "set_%d.json" % cell_size
    final_name = "final.png" if cell_size == 30 else "final_%d.png" % cell_size
    meta = json.loads((theater_dir / set_name).read_text())
    cell = meta["cell"]
    keys, payload = [], bytearray()
    for name, row in sorted(meta["templates"].items(), key=lambda kv: kv[1]["tid"]):
        img = Image.open(theater_dir / name / final_name).convert("RGB")
        w, h = row["cells"]
        if img.size != (w * cell, h * cell):
            raise SystemExit(f"{name}: {final_name} is {img.size}, expected {(w * cell, h * cell)}")
        if row.get("field"):
            icons = [((y * cell, x * cell), (x & 3) | ((y & 3) << 2)) for y in range(h) for x in range(w)]
        else:
            icons = [(((i // w) * cell, (i % w) * cell), i) for i in row["have"]]
        for (y0, x0), icon in icons:
            keys.append((row["tid"], icon))
            payload += img.crop((x0, y0, x0 + cell, y0 + cell)).tobytes()
        for var in row.get("variants") or []:
            vimg = Image.open(theater_dir / name / var["final"]).convert("RGB")
            if vimg.size != img.size:
                raise SystemExit(f"{name}: {var['final']} is {vimg.size}, expected {img.size}")
            for (y0, x0), icon in icons:
                keys.append((row["tid"], variant_icon(icon, var["n"], w, h)))
                payload += vimg.crop((x0, y0, x0 + cell, y0 + cell)).tobytes()
    out.write_bytes(MAGIC + struct.pack("<3I", VERSION, cell, len(keys))
                    + b"".join(struct.pack("<2H", t, i) for t, i in keys) + payload)
    if cell == 144:
        from terrain_daylight import apply
        apply(out, theater_dir.name, "terrainenh")
        from terrain_connectors import apply_file
        apply_file(out)
    return len(keys)


def main() -> None:
    args = sys.argv[1:]
    cell_size = 144
    if "--cell" in args:
        k = args.index("--cell")
        cell_size = int(args[k + 1])
        del args[k:k + 2]
    out_dir = Path(args[0]) if args else None
    set_name = "set.json" if cell_size == 30 else "set_%d.json" % cell_size
    for theater_dir in sorted(p for p in ART.iterdir() if (p / set_name).exists()):
        th = theater_dir.name
        dest = out_dir or theater_dir
        dest.mkdir(parents=True, exist_ok=True)
        if cell_size == 30:
            out = dest / f"terrain30_{th}.pack"
            print(f"{out}: {pack(theater_dir, out, 30)} cells")
            continue
        out = dest / f"terrainenh_{th}.pack"
        print(f"{out}: {pack(theater_dir, out, cell_size)} cells at {cell_size}")
        pack_terrain_seams.pack_set(theater_dir / set_name, dest / f"terrainseam_{th}.pack")


if __name__ == "__main__":
    main()
