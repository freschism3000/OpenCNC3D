#!/usr/bin/env python3
"""
CNC3D -- terrain HEIGHTMAP extractor (bug #12: the N64's elevation source).

WHERE THE HEIGHTS LIVE (full evidence trail: heightmap_notes.md, same folder)
-----------------------------------------------------------------------------
Each scenario ships a per-CORNER heightmap as an ordinary archive file named
<SCEN>.IMG -- e.g. SCG01EA.IMG. It is a 65x65 8-bit intensity .IMG "image"
(fmt=4, depth=1, 16-byte header), which is why it hid among the 773 IMG files:
it decodes as a picture but it is DATA. 65x65 = one height byte per VERTEX of
the 64x64-cell map (64+1 corners each way).

Proof from the ROM code (not guessed):
  - n64engine.cpp map init, resident RAM 0x80048574 (ROM 0x49174):
    malloc(0x1081) [= 65*65] -> N64Map+0x30, filename sprintf("%s.img", scen)
    with fallback "flat.img" (FLAT.IMG is 4225 zero bytes); a second buffer
    malloc(0x2102) [= 65*65*2] -> N64Map+0x34 from "cm"+name / "cmflat.img"
    (65x65 RGBA5551 per-vertex colour map -- lighting, not height).
  - gterrain.c GTerrain_Init, CodeOverlay RAM 0x801F6748 (ROM 0x198538):
    stores that pointer at gTerrain+4 (gTerrain BSS 0x8021BB50).
  - gterrain.c vertex builder RAM 0x801F4984 (ROM 0x196774):
        lbu  h, heightmap[y*65 + x]
        vertex.y = h * 4            (world units; one cell = 256 units)
        vertex.x = x * 256 ; vertex.z = y * 256
    and samples the 65-stride neighbours for the vertex normal/colour.

So: world-space height of map corner (x, y) = byte[y*65 + x] * 4, i.e. one
byte step = 1/64 of a cell width; the SCG01EA values run 28..130 (sea floor
28-31, beach ~32, main land 64, high plateau 128+). Scenarios with no .IMG
(the multiplayer SCB2x/3x/6x set) use FLAT.IMG = all zero = flat, which is
exactly the engine's own fallback.

Output JSON (documented, native units = raw bytes):
  {
    "scenario":       "SCG01EA",
    "source_file":    "SCG01EA.IMG" | "FLAT.IMG (fallback)",
    "grid":           "corner",     # heights are per cell-corner
    "width": 65, "height": 65,      # corners; the cell map is 64x64
    "unit":  "raw byte; world Y = value*4, cell pitch = 256 world units",
    "corners": [[65 rows x 65 ints]],          # corners[y][x]
    "cells":   [[64 rows x 64 cells]]          # cells[cy][cx] =
  }                                            #   [h(cx,cy), h(cx+1,cy),
                                               #    h(cx,cy+1), h(cx+1,cy+1)]
Usage:
    python3 heights.py [SCEN] [out.json] [--rom path/to/cnc_eu.z64]
    (defaults: SCG01EA, heights_<SCEN>.json next to this script)

Read-only on the ROM. Prints a water-vs-land sanity summary using the
scenario .MAP + theater TL4/TL8 ((template<<8)|icon per tile).
"""
import json, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import archive

DEFAULT_ROM = os.path.join(HERE, "..", "..", "data", "rom", "cnc_eu.z64")

# Water-capable TemplateType ids (WATER*/SHORE*/RIVER*/FORD*/FALLS*/BRIDGE* in
# brain/ea2020/TIBERIANDAWN/DEFINES.H) -- the same set the terrain baker uses.
WATER_RANGES = ((1, 12), (51, 56), (75, 78), (88, 92), (136, 180), (186, 215))
def is_water_template(t):
    return t is not None and any(a <= t <= b for a, b in WATER_RANGES)


def rom_files(rom_path, names):
    d = archive.load(rom_path)
    want = {n.upper() for n in names}
    out = {}
    for a in archive.all_archives(d):
        for r in a["recs"]:
            n = r["name"].upper()
            if n in want and n not in out and r["usize"]:
                out[n] = archive.unpack(d, r, a["base"])
    return out


def parse_img_heights(blob, name):
    hdr, _body, w, h = struct.unpack_from(">IIHH", blob, 0)
    fmt, depth = blob[12], blob[13]
    if not (hdr == 0x10 and w == 65 and h == 65 and fmt == 4 and depth == 1):
        raise SystemExit(f"{name}: not a 65x65 8bpp intensity heightmap "
                         f"(hdr={hdr:#x} w={w} h={h} fmt={fmt} depth={depth})")
    pay = blob[16:16 + 65 * 65]
    assert len(pay) == 65 * 65, (name, len(pay))
    return pay


def theater_of(ini_text):
    for line in ini_text.splitlines():
        s = line.strip().upper()
        if s.startswith("THEATER="):
            th = s.split("=", 1)[1].strip()
            return {"DESERT": "DESERT", "TEMPERATE": "TEMPERAT",
                    "WINTER": "WINTER"}.get(th, th[:8])
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    rom = DEFAULT_ROM
    if "--rom" in sys.argv:
        rom = sys.argv[sys.argv.index("--rom") + 1]
    scen = (args[0] if args else "SCG01EA").upper()
    out_path = args[1] if len(args) > 1 else os.path.join(
        HERE, f"heights_{scen}.json")

    names = [f"{scen}.IMG", "FLAT.IMG", f"{scen}.MAP", f"{scen}.INI"]
    files = rom_files(rom, names)

    src = f"{scen}.IMG"
    if src not in files:
        src = "FLAT.IMG"          # the engine's own fallback path
    hm = parse_img_heights(files[src], src)

    corners = [[hm[y * 65 + x] for x in range(65)] for y in range(65)]
    cells = [[[hm[cy * 65 + cx],       hm[cy * 65 + cx + 1],
               hm[(cy + 1) * 65 + cx], hm[(cy + 1) * 65 + cx + 1]]
              for cx in range(64)] for cy in range(64)]

    doc = {
        "scenario": scen,
        "source_file": src if src != "FLAT.IMG" or scen == "FLAT"
                       else "FLAT.IMG (fallback: scenario ships no heightmap)",
        "grid": "corner",
        "width": 65, "height": 65,
        "unit": "raw byte; world Y = value*4, cell pitch = 256 world units "
                "(vertex builder RAM 0x801F4984: vertex.y = byte*4)",
        "corners": corners,
        "cells": cells,
    }
    with open(out_path, "w") as f:
        json.dump(doc, f)
    print(f"wrote {out_path}  ({src}: min={min(hm)} max={max(hm)})")

    # ---- sanity: water-capable cells should sit LOW ------------------------
    if f"{scen}.MAP" in files and f"{scen}.INI" in files:
        th = theater_of(files[f"{scen}.INI"].decode("latin1", "replace"))
        tl = rom_files(rom, [f"{th}.TL4", f"{th}.TL8"]) if th else {}
        tl4, tl8 = tl.get(f"{th}.TL4", b""), tl.get(f"{th}.TL8", b"")
        mp = struct.unpack(">4096H", files[f"{scen}.MAP"])

        def tpl_of(tid):
            if tid == 0xFFFF:
                return None            # clear filler cell
            if tid >= 1024:
                i = (tid - 1024) * 2
                return tl8[i] if i + 1 < len(tl8) else None
            return tl4[tid * 2] if tid * 2 + 1 < len(tl4) else None

        def wet(cx, cy):
            return is_water_template(tpl_of(mp[cy * 64 + cx]))

        def cellavg(cx, cy):
            return sum(cells[cy][cx]) / 4.0

        # Compare each water-capable cell against LAND cells within 2 cells:
        # global means mislead on maps where a river crosses a high plateau
        # (SCB01EA), while a carved channel is always locally low.
        diffs = []
        for cy in range(64):
            for cx in range(64):
                if not wet(cx, cy):
                    continue
                nb = [cellavg(x, y)
                      for dy in range(-2, 3) for dx in range(-2, 3)
                      for x, y in ((cx + dx, cy + dy),)
                      if 0 <= x < 64 and 0 <= y < 64
                      and mp[y * 64 + x] != 0xFFFF and not wet(x, y)]
                if nb:
                    diffs.append(cellavg(cx, cy) - sum(nb) / len(nb))
        if diffs:
            low = sum(1 for v in diffs if v < 0)
            mean = sum(diffs) / len(diffs)
            verdict = "OK: water sits below its banks" if low * 2 > len(diffs) \
                      else ("flat map" if max(hm) == min(hm) else "UNEXPECTED")
            print(f"sanity [{th}]: {len(diffs)} water-capable cells; "
                  f"lower than nearby land: {low}/{len(diffs)} "
                  f"(mean diff {mean:+.1f})  ->  {verdict}")


if __name__ == "__main__":
    main()
