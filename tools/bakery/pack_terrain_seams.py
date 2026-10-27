#!/usr/bin/env python3
"""Pack the seam margins of a theater's high-resolution terrain set, for the join blend.

    python3 tools/bakery/pack_terrain_seams.py <set_144.json | build dir of the set> <out.pack>

The set is a theater's export in the committed tree (art/remaster/textures/<TH>/
set_144.json, whose templates list their strips under "margins", the files beside each
template's final picture), which is what tools/bakery/pack_remaster_terrain.py packs for
the game. A build dir is one theater's output of tools/art/remaster/textures/tex.py (for
example build-native/remaster/textures/TEMPERAT_144), each template's meta.json listing
its margins, for trying a build before it is exported. game/terrain_seam.h reads the pack:

    "C3DSEAM\\0"  u32 version (1)  u32 cell  u32 margin  u32 count
    count x (u16 template number, u16 icon, u8 side (0 left, 1 top),
             u8 kind (0 a cliff's strip, 1 a feature's: road, shore, river), u8 pad x2)
    count x RGBA strips in the same order: a left strip is margin wide and cell tall,
    a top strip cell wide and margin tall, row-major, the border at its right / bottom

A variant's strips carry the variant and the template's size in the icon field, as its
cells do in the terrain pack (tools/bakery/pack_remaster_terrain.py).
"""
import json
import struct
import sys
from pathlib import Path

from PIL import Image

MAGIC = b"C3DSEAM\0"


def write(out, entries):
    """entries: (template number, icon, side "L"/"T", kind, cell, RGBA image)."""
    keys, payload, cell, margin = [], bytearray(), None, None
    for tid, icon, side, kind, c, img in entries:
        m = img.size[0] if side == "L" else img.size[1]
        if cell is None:
            cell, margin = c, m
        if (c, m) != (cell, margin):
            raise SystemExit("template %d: cell %d margin %d, the pack is %d / %d" % (tid, c, m, cell, margin))
        keys.append((tid, icon, 0 if side == "L" else 1, 1 if kind == "feature" else 0))
        payload += img.tobytes()
    if not keys:
        raise SystemExit("no margins for %s" % out)
    Path(out).write_bytes(MAGIC + struct.pack("<4I", 1, cell, margin, len(keys))
                          + b"".join(struct.pack("<2H2B2x", t, i, sd, k) for t, i, sd, k in keys) + payload)
    print("%s: %d strips (%d of features), cell %d, margin %d" % (
        out, len(keys), sum(k[3] for k in keys), cell, margin))


def pack_set(set_json, out):
    """From an exported set (set_<cell>.json and the strips beside its templates)."""
    set_json = Path(set_json)
    meta = json.loads(set_json.read_text())
    entries = []
    from pack_remaster_terrain import variant_icon
    for name, row in sorted(meta["templates"].items()):
        for r in row.get("margins") or []:
            img = Image.open(set_json.parent / name / r["file"]).convert("RGBA")
            entries.append((row["tid"], r["icon"], r["side"], r.get("kind"), meta["cell"], img))
        # a variant's strips, keyed as its cells are in the terrain pack (variant_icon)
        w, h = row["cells"]
        for var in row.get("variants") or []:
            for r in var["margins"]:
                img = Image.open(set_json.parent / name / r["file"]).convert("RGBA")
                entries.append((row["tid"], variant_icon(r["icon"], var["n"], w, h), r["side"], r.get("kind"),
                                 meta["cell"], img))
    write(out, entries)
    if meta["cell"] == 144:
        from terrain_daylight import apply
        apply(out, set_json.parent.name, "terrainseam")


def pack_build(src, out):
    """From a build dir (each template's meta.json and its margin files)."""
    entries = []
    for meta_path in sorted(Path(src).glob("*/meta.json")):
        meta = json.loads(meta_path.read_text())
        rows = meta.get("margins") or []
        if not rows:
            continue
        tid = meta.get("tid")
        if tid is None:
            raise SystemExit("%s: no template number (rebuild with tex.py)" % meta_path)
        for r in rows:
            img = Image.open(meta_path.parent / r["file"]).convert("RGBA")
            entries.append((tid, r["icon"], r["side"], r.get("kind"), meta["cell"], img))
    write(out, entries)


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    if src.suffix == ".json":
        pack_set(src, out)
    else:
        pack_build(src, out)


if __name__ == "__main__":
    main()
