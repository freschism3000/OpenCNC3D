#!/usr/bin/env python3
"""Pack the Enhanced sea's WINTER and SNOW data: the coast coverage and the open water.

    python3 tools/bakery/pack_coast_masks.py [out_dir]       (default: game/)

Writes, per bank, from committed art only (Pillow, no image analysis):
    coastmask_<BANK>.pack   the land coverage of every water template cell, from the
                            saved masks in art/remaster/textures/<BANK>/coast_v01
                            (tools/art/remaster/textures/coast_masks.py made them)
    waterbase_<BANK>.pack   the bank's own approved open-water painting (W2, two cells
                            square at 144 texels a cell), the sea's base colour

Formats, both read by game/coast_mask.h and game/water_mod.h:
    "C3DCOAS\\0"  u32 version 1  u32 cell (144)  u32 count
                 count x (16-byte name, u16 template, u16 icon, u8 variant, u8 game 0, u16 0)
                 count x cell*cell bytes, 255 land and 0 water
    "C3DWATR\\0"  u32 version 1  u32 size (288)  size*size*3 bytes of RGB
Every source painting and saved mask is checked against the SHA-256 the mask builder
recorded, and the coast pack against its own, so a repainted template stops the build
instead of shipping a coast that no longer matches its picture.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "art" / "remaster" / "textures"
BANKS = ("WINTER", "SNOW")
FOLDER = "coast_v01"
BASE = 288


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def coast(bank):
    folder = ART / bank / FOLDER
    m = json.loads((folder / "COAST_MANIFEST.json").read_text())
    cell = m["cell"]
    keys, pixels = [], []
    for r in m["templates"]:
        if sha(ROOT / r["source"]) != r["source_sha256"]:
            raise SystemExit("%s/%s was repainted: rebuild its coast (coast_masks.py build %s)"
                             % (bank, r["name"], bank))
        path = folder / r["land_alpha"]
        if sha(path) != r["land_alpha_sha256"]:
            raise SystemExit("saved coast mask changed: %s" % path)
        alpha = Image.open(path).convert("L")
        w, h = r["cells"]
        if alpha.size != (w * cell, h * cell):
            raise SystemExit("coast mask size differs from its template: %s" % path)
        for icon in r["have"]:
            y, x = divmod(icon, w)
            keys.append(r["name"].encode().ljust(16, b"\0")
                        + struct.pack("<HHBBH", r["tid"], icon, r["variant"], 0, 0))
            pixels.append(alpha.crop((x * cell, y * cell, (x + 1) * cell, (y + 1) * cell)).tobytes())
    data = b"C3DCOAS\0" + struct.pack("<III", 1, cell, len(keys)) + b"".join(keys) + b"".join(pixels)
    if len(keys) != m["count"] or hashlib.sha256(data).hexdigest() != m["sha256"]:
        raise SystemExit("%s coast pack differs from its manifest" % bank)
    return data, len(keys)


def waterbase(bank):
    im = Image.open(ART / bank / "W2" / "final_144.png").convert("RGB")
    if im.size != (BASE, BASE):
        raise SystemExit("%s W2 is not %dx%d" % (bank, BASE, BASE))
    return b"C3DWATR\0" + struct.pack("<II", 1, BASE) + im.tobytes()


def main(out):
    out.mkdir(parents=True, exist_ok=True)
    for bank in BANKS:
        data, cells = coast(bank)
        (out / ("coastmask_%s.pack" % bank)).write_bytes(data)
        (out / ("waterbase_%s.pack" % bank)).write_bytes(waterbase(bank))
        print("%s: %d coast cells, open water %dx%d" % (bank, cells, BASE, BASE))


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "game")
