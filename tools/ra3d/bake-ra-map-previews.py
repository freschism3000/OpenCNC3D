#!/usr/bin/env python3
"""
bake-ra-map-previews.py -- Red Alert's 24 multiplayer maps, listed and pictured for the skirmish
and multiplayer screens.

    python3 tools/ra3d/bake-ra-map-previews.py OUT_MODULE_DIR [--data data/radata] [--proof FILE]

Writes, into the module folder (the play folder's ra3d/):

  skirmish-maps.txt       one line per map, the list a menu reads (format below)
  previews/SCMnnEA.png    one 120 x 120 picture per map, RGBA, the map fitted inside with its
                          aspect kept and the margin left transparent

WHERE EACH PART OF A PICTURE COMES FROM, and nothing is invented:

  ground   the map's own [MapPack], decoded exactly as MapClass::Read_Binary does
           (tools/ra3d/ra_map.py), each cell the mean colour of the very icon the game draws
           there, read out of the theater's bank atlas (tools/bakery/sharecopy/assets/terrain/
           RASNOW_tiles.png or RATEMP_tiles.png, laid out by game/ra_terrain_tiles.h). Clear
           ground is CLEAR1 with the icon picked by the cell's position, as the engine draws it.
  ore      [OverlayPack], decoded the same way: ore (GOLD1..4) in the gold and gems (GEMS1..4)
           in the violet Red Alert's own radar uses for them, so the money shows at this size.
  starts   [Waypoints] 0..7 that are set: a multiplayer map's start positions, in the order
           the engine numbers them, each a small white diamond with a dark edge.

NOT drawn: trees, buildings, units. Their art is not part of this bake, and a blob standing in
for a tree would be an invented fact. A known gap, the same as Tiberian Dawn's previews.

skirmish-maps.txt, one map per line, fields separated by '|':

    SCM01EA|Coastal Influence|SNOW|x,y,w,h|starts|previews/SCM01EA.png|sx,sy;sx,sy;...

  x,y,w,h  the playable rectangle in cells, as [Map] gives it
  starts   how many start positions the map has (the most players it seats)
  the last field lists each start's cell, in waypoint order.

Inputs: data/radata/GENERAL.MIX (the maps, read through tools/ra3d/ramix.py), the two bank
atlases and game/ra_terrain_tiles.h, all committed.
"""
from __future__ import annotations

import argparse
import base64
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))
import ramix  # noqa: E402
from mixshp import lcw_uncompress  # noqa: E402
from ra_map import W, H, ini_sections, decode_mappack  # noqa: E402

from PIL import Image  # noqa: E402

FRAME = 120
ATLAS = {
    "SNOW": os.path.join(ROOT, "tools", "bakery", "sharecopy", "assets", "terrain", "RASNOW_tiles.png"),
    "TEMPERATE": os.path.join(ROOT, "tools", "bakery", "sharecopy", "assets", "terrain", "RATEMP_tiles.png"),
}
TABLE = {"SNOW": "TT_RASNOW", "TEMPERATE": "TT_RATEMP"}
TILES_H = os.path.join(ROOT, "game", "ra_terrain_tiles.h")
COLS, PITCH, TILE = 32, 26, 24

ORE = (204, 160, 52)       # GOLD1..4
GEMS = (160, 72, 176)      # GEMS1..4


def slot_table(name):
    """{(template, icon): slot} from the generated header."""
    src = open(TILES_H, encoding="utf-8", errors="replace").read()
    a = src.index("static const TtSlot %s[" % name)
    body = src[a:src.index("};", a)]
    out = {}
    for m in re.finditer(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", body):
        t, i, s = int(m.group(1)), int(m.group(2)), int(m.group(3))
        out[(t, i)] = s
    return out


def decode_overlaypack(sec):
    blob = base64.b64decode("".join(v for k, v in sorted(sec, key=lambda kv: int(kv[0]))))
    out, p = bytearray(), 0
    while p + 4 <= len(blob):
        comp, uncomp = struct.unpack_from("<HH", blob, p)
        p += 4
        data, _ = lcw_uncompress(blob, p, uncomp)
        out += bytes(data[:uncomp])
        p += comp
    if len(out) < W * H:
        return [0xFF] * (W * H)
    return list(out[:W * H])


def tile_colours(theater):
    """{slot: (r, g, b)} -- the mean colour of every icon in the theater's bank."""
    img = Image.open(ATLAS[theater]).convert("RGB")
    cache = {}

    def colour(slot):
        if slot not in cache:
            x = (slot % COLS) * PITCH + 1
            y = (slot // COLS) * PITCH + 1
            tile = img.crop((x, y, x + TILE, y + TILE)).resize((1, 1), Image.BOX)
            cache[slot] = tile.getpixel((0, 0))
        return cache[slot]

    return colour


def bake(scen, text, slots, colour_of):
    ini = ini_sections(text)
    mp = dict(ini.get("Map", []))
    theater = mp.get("Theater", "TEMPERATE").upper()
    rx, ry = int(mp.get("X", 0)), int(mp.get("Y", 0))
    rw, rh = int(mp.get("Width", W)), int(mp.get("Height", H))
    name = dict(ini.get("Basic", [])).get("Name", scen)
    tmpl, icon = decode_mappack(ini["MapPack"])
    over = decode_overlaypack(ini["OverlayPack"]) if "OverlayPack" in ini else [0xFF] * (W * H)
    wp = {int(k): int(v) for k, v in ini.get("Waypoints", []) if k.isdigit()}
    starts = [wp[i] for i in range(8) if wp.get(i, -1) >= 0]

    cells = Image.new("RGB", (rw, rh))
    missing = 0
    for y in range(rh):
        for x in range(rw):
            c = (ry + y) * W + (rx + x)
            t, i = tmpl[c], icon[c]
            if t in (0xFFFF, 0xFF):
                t, i = 0, ((rx + x) & 3) | (((ry + y) & 3) << 2)
            s = slots.get((t, i))
            if s is None:
                s = slots.get((0, ((rx + x) & 3) | (((ry + y) & 3) << 2)), 0)
                missing += 1
            rgb = colour_of(s)
            o = over[c]
            if 5 <= o <= 8:
                rgb = ORE
            elif 9 <= o <= 12:
                rgb = GEMS
            cells.putpixel((x, y), rgb)

    scale = min(FRAME / rw, FRAME / rh)
    iw, ih = max(1, round(rw * scale)), max(1, round(rh * scale))
    ox, oy = (FRAME - iw) // 2, (FRAME - ih) // 2
    frame = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
    frame.paste(cells.resize((iw, ih), Image.BOX).convert("RGBA"), (ox, oy))
    marks = []
    for cell in starts:
        cx, cy = cell % W - rx, cell // W - ry
        px = ox + int((cx + 0.5) * scale)
        py = oy + int((cy + 0.5) * scale)
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                d = abs(dx) + abs(dy)
                if d <= 3 and 0 <= px + dx < FRAME and 0 <= py + dy < FRAME:
                    frame.putpixel((px + dx, py + dy), (24, 24, 24, 255) if d == 3 else (255, 255, 255, 255))
        marks.append("%d,%d" % (cell % W, cell // W))
    return dict(scen=scen, name=name, theater=theater, rect=(rx, ry, rw, rh), starts=starts,
                marks=marks, image=frame, missing=missing)


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("out", help="the module folder (a play folder's ra3d/)")
    ap.add_argument("--data", default=os.path.join(ROOT, "data", "radata"))
    ap.add_argument("--proof", help="also write a labelled contact sheet of every preview")
    args = ap.parse_args()

    general = ramix.RaMix(os.path.join(args.data, "GENERAL.MIX"))
    slots = {th: slot_table(TABLE[th]) for th in TABLE}
    colours = {th: tile_colours(th) for th in TABLE}
    os.makedirs(os.path.join(args.out, "previews"), exist_ok=True)
    rows, maps = [], []
    for n in range(1, 25):
        scen = "SCM%02dEA" % n
        raw = general.read(scen + ".INI")
        if raw is None:
            raise SystemExit("bake-ra-map-previews: %s.INI is not in GENERAL.MIX" % scen)
        text = raw.decode("cp437", "replace")
        theater = dict(ini_sections(text).get("Map", [])).get("Theater", "TEMPERATE").upper()
        if theater not in TABLE:
            raise SystemExit("bake-ra-map-previews: %s is in theater %s, which has no bank" % (scen, theater))
        m = bake(scen, text, slots[theater], colours[theater])
        rel = "previews/%s.png" % scen
        m["image"].save(os.path.join(args.out, rel))
        rows.append("%s|%s|%s|%d,%d,%d,%d|%d|%s|%s" % (
            scen, m["name"].replace("|", "/"), m["theater"], *m["rect"], len(m["starts"]), rel,
            ";".join(m["marks"])))
        maps.append(m)
        print("%s  %-24s %-9s %3dx%-3d starts=%d%s" % (
            scen, m["name"], m["theater"], m["rect"][2], m["rect"][3], len(m["starts"]),
            "  (%d cells drawn as clear ground: no icon in the bank)" % m["missing"] if m["missing"] else ""))
    with open(os.path.join(args.out, "skirmish-maps.txt"), "w", encoding="utf-8") as f:
        f.write("# Red Alert's multiplayer maps: scenario|name|theater|x,y,w,h|starts|preview|start cells\n")
        f.write("\n".join(rows) + "\n")
    if args.proof:
        sheet = Image.new("RGBA", (6 * 140, 4 * 150), (40, 40, 40, 255))
        for k, m in enumerate(maps):
            x, y = (k % 6) * 140 + 10, (k // 6) * 150 + 5
            sheet.paste(m["image"], (x, y), m["image"])
        sheet.save(args.proof)
    print("bake-ra-map-previews: %d maps listed in %s" % (len(rows), os.path.join(args.out, "skirmish-maps.txt")))


if __name__ == "__main__":
    main()
