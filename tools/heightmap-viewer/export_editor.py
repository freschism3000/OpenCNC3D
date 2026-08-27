#!/usr/bin/env python3
"""CNC3D map editor -- the static tables the browser needs to tell the truth.

The editor must answer, per cell, the same question the engine will answer, and it
must answer it identically or the maps it makes will be quietly wrong. All four
tables below come out of EA's GPL Tiberian Dawn source or the cartridge's own
theater tables, never out of a guess.

  editor_tables.json
    slots      per theater, bank slot -> [template, icon]. A proven bijection:
               DESERT 945 4bpp + 28 8bpp = 973 slots to 973 distinct pairs,
               TEMPERAT 315 + 443 = 758 to 758. Inverted from the TL4/TL8 tables
               the cartridge ships, which is the same lookup n64_terrain.py uses.
    templates  216 rows from brain/vanilla/tiberiandawn/cdata.cpp, in the order of
               its own Pointers[] table: ini name, theater mask, Land, Width,
               Height, AltLand and the AltIcons list. The alt list is what makes a
               cliff walkable: 11 of the 38 SLOPE templates carry one, and it is
               the difference between a plateau you can reach and one you cannot.
    ground     the LandType table itself, Ground[LAND_COUNT] at const.cpp:245.
               `build` is that row's third field; `passable` is its Foot cost being
               non-zero (S1 = 0x00 means no route). So buildable is CLEAR and ROAD
               only, while TIBERIUM and BEACH are passable but not buildable, which
               is why "cliffs and water" is too coarse a rule to use.
    bibbed     which building types lay a bib, from bdata.cpp. A bibbed building
               occupies Width x (Height+1); without this a Construction Yard looks
               legal in the editor and is refused in the game.

Read-only. Writes public/data/editor_tables.json.
"""
import json, os, re, struct

HERE = os.path.dirname(os.path.abspath(__file__))
GPL = os.path.join(HERE, "..", "..", "brain", "vanilla", "tiberiandawn")
EX = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "extracted")
OUT = os.path.join(HERE, "public", "data")

LAND = ["CLEAR", "ROAD", "WATER", "ROCK", "WALL", "TIBERIUM", "BEACH"]


def ground_table():
    """Ground[LAND_COUNT], const.cpp:245. Foot cost S1 (0x00) means impassable."""
    s = open(os.path.join(GPL, "const.cpp"), errors="ignore").read()
    body = s[s.index("GroundType const Ground[LAND_COUNT] = {"):]
    body = body[:body.index("\n};")]
    sdef = dict(re.findall(r"#define\s+(S\d)\s+(0x[0-9A-Fa-f]+)",
                           s[:s.index("GroundType const Ground")]))
    out = {}
    rows = re.findall(r"\{\s*[A-Za-z0-9_]+\s*,\s*\{([^}]*)\}\s*,\s*(true|false)\s*\}", body)
    for i, (costs, build) in enumerate(rows):
        foot = costs.split(",")[0].strip()
        cost = int(sdef.get(foot, "0x00"), 16)
        out[LAND[i]] = dict(build=(build == "true"), foot=cost, passable=cost > 0)
    assert len(out) == len(LAND), out
    return out


def alt_icon_lists():
    """static int const _slopeNNN[] = { 1, 3, -1 } and friends: the icons of a
    template whose land is AltLand rather than Land."""
    s = open(os.path.join(GPL, "cdata.cpp"), errors="ignore").read()
    out = {}
    for m in re.finditer(r"static\s+\w+\s+const\s+(_\w+)\s*\[\s*\]\s*=\s*\{([^}]*)\}", s):
        nums = [int(x) for x in re.findall(r"-?\d+", m.group(2))]
        out[m.group(1)] = [n for n in nums if n >= 0]
    return out


def templates():
    """216 TemplateTypeClass rows, in Pointers[] order."""
    s = open(os.path.join(GPL, "cdata.cpp"), errors="ignore").read()
    alt = alt_icon_lists()
    byname = {}
    for m in re.finditer(
            r"TemplateTypeClass\s+const\s+(\w+)\s*\(\s*(TEMPLATE_\w+)\s*,(.*?)\)\s*;",
            s, re.S):
        args = m.group(3)
        # strip the theater mask, which is the only arg containing THEATERF_
        parts = [a.strip() for a in re.split(r",(?![^(]*\))", args)]
        if len(parts) < 8:
            continue
        theater, ini, _txt, land, w, h, altland, alticons = parts[:8]
        ini = ini.strip().strip('"')
        try:
            w, h = int(w), int(h)
        except ValueError:
            continue
        lst = []
        a = alticons.strip()
        mm = re.search(r"(_\w+)", a)
        if mm and mm.group(1) in alt:
            lst = alt[mm.group(1)]
        byname[m.group(2)] = dict(
            ini=ini, theaters=[t for t in ("WINTER", "TEMPERATE", "DESERT")
                               if "THEATERF_" + t in theater],
            land=land.strip().replace("LAND_", ""),
            w=w, h=h,
            altLand=altland.strip().replace("LAND_", ""),
            altIcons=lst)
    # order by the TemplateType enum, which is what a cell record stores
    names, on = [], False
    for line in open(os.path.join(GPL, "defines.h"), errors="ignore"):
        t = line.split("//")[0].strip()
        if t.startswith("TEMPLATE_CLEAR1"):
            on = True
        if on:
            if t.startswith("TEMPLATE_COUNT"):
                break
            if t.startswith("TEMPLATE_"):
                names.append(t.rstrip(",").split()[0].rstrip(","))
    return [byname.get(n, dict(ini=n.replace("TEMPLATE_", ""), theaters=[],
                               land="CLEAR", w=1, h=1, altLand="CLEAR",
                               altIcons=[])) for n in names], names


def slots():
    """bank slot -> (template, icon), per theater, from the cartridge's own tables."""
    out = {}
    for root in ("DESERT", "TEMPERAT"):
        n4 = os.path.getsize(os.path.join(EX, f"DA4/{root}.DA4")) // 288
        n8 = os.path.getsize(os.path.join(EX, f"DA8/{root}.DA8")) // 576
        t4 = open(os.path.join(EX, f"TL4/{root}.TL4"), "rb").read()
        t8 = open(os.path.join(EX, f"TL8/{root}.TL8"), "rb").read()
        rows, seen = [], set()
        for i in range(n4):
            p = (t4[2 * i], t4[2 * i + 1]); rows.append(list(p)); seen.add(p)
        for k in range(n8):
            p = (t8[2 * k], t8[2 * k + 1]); rows.append(list(p)); seen.add(p)
        assert len(seen) == len(rows), f"{root}: not a bijection"
        out[root] = rows
    return out


def bibbed():
    """Which building types lay a bib. bdata.cpp, the IsBibbed ctor argument."""
    s = open(os.path.join(GPL, "bdata.cpp"), errors="ignore").read()
    out = {}
    for b in re.split(r"static\s+BuildingTypeClass\s+const\s+", s)[1:]:
        ini = re.search(r'TXT_[A-Z0-9_]+\s*,[^"]*"([A-Za-z0-9]{2,8})"', b, re.S)
        m = re.search(r"(true|false)\s*,\s*//\s*Requires a bib", b)
        if ini and m:
            out[ini.group(1).upper()] = (m.group(1) == "true")
    return out


PNG = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "png")


BRAIN = os.path.join(HERE, "..", "..", "brain", "vanilla", "tiberiandawn")


def names():
    """The game's own display names, not names I made up.

    Every type class in the brain declares its text id and its INI code on two
    consecutive lines (`TXT_TEMPLE,` then `"TMPL",`), and conquer.h defines each
    text id with the English string in the trailing comment. Pairing the two gives
    the exact word the sidebar uses.
    """
    txt = {}
    for line in open(os.path.join(BRAIN, "conquer.h"), errors="replace"):
        m = re.match(r"\s*#define\s+(TXT_\w+)\s+\d+\s*//\s*(.+?)\s*$", line)
        if m:
            txt[m.group(1)] = m.group(2)
    out = {}
    for f in ("bdata.cpp", "udata.cpp", "idata.cpp", "tdata.cpp", "odata.cpp"):
        path = os.path.join(BRAIN, f)
        if not os.path.exists(path):
            continue
        src = open(path, errors="replace").read()
        # Buildings and units declare the text id first, terrain and overlay the
        # INI code first. Read both orders rather than assuming one.
        pairs = [(m.group(2), m.group(1)) for m in re.finditer(
            r'(TXT_\w+)\s*,[^\n]*\n\s*"([A-Z0-9_]{1,8})"\s*,', src)]
        pairs += [(m.group(1), m.group(2)) for m in re.finditer(
            r'"([A-Z0-9_]{1,8})"\s*,[^\n]*\n\s*(TXT_\w+)\s*,', src)]
        for code, tid in pairs:
            name = txt.get(tid)
            if name and name != "None" and code not in out:
                out[code] = name
    return out


# The tile atlas geometry, identical to export.py and app.js. A terrain brush that
# disagreed with these by one pixel would show the wrong art for every tile.
TS, PITCH, GUTTER, COLS = 24, 26, 1, 32


def hole_slots():
    """Which atlas slots show sea, per theater, by the cartridge's own statement.

    The N64 terrain palettes carry no water colour: a water tile punches an alpha
    hole and the console draws the sea plane underneath (cnc_eyes.cpp:4444). So a
    slot shows sea exactly when its 24x24 tile has a fully transparent texel, which
    is the same test export.py uses to build each map's hole mask. The editor needs
    it because painting a water template has to turn the sea plane on under that
    cell, and painting land has to turn it off.
    """
    from PIL import Image
    out = {}
    for th in ("TEMPERAT", "DESERT"):
        path = os.path.join(OUT, th + "_tiles.png")
        if not os.path.exists(path):
            continue
        a = Image.open(path).convert("RGBA").split()[3]
        rows = a.height // PITCH
        found = []
        for slot in range(rows * COLS):
            col, row = slot % COLS, slot // COLS
            x0, y0 = col * PITCH + GUTTER, row * PITCH + GUTTER
            if x0 + TS > a.width or y0 + TS > a.height:
                continue
            if a.crop((x0, y0, x0 + TS, y0 + TS)).getextrema()[0] == 0:
                found.append(slot)
        out[th] = found
    return out


def occupy():
    """Which cells a scenery object actually STANDS on, relative to its stored cell.

    This is the one that bites. A tree's INI cell is not where the tree is: T01's
    occupy list is `_List0010 = {MAP_CELL_W}`, one cell SOUTH, and its CenterBase is
    XYP_COORD(11,41) = 1.708 cells south, which lands inside that occupied cell. The
    engine stores trees a row north of where they stand and the renderer reproduces
    that faithfully, which is exactly why clicking a cell in the editor put the tree a
    cell below the cursor.

    Second-to-last constructor argument of TerrainTypeClass (type.h), decoded with
    MAP_CELL_W = 64 (defines.h:258-270, no MEGAMAPS).
    """
    td = os.path.join(BRAIN, "tdata.cpp")
    src = open(td, errors="ignore").read()
    lists = {}
    for m in re.finditer(r"static\s+short\s+const\s+(_\w+)\s*\[\]\s*=\s*\{(.*?)\}\s*;",
                         src, re.S):
        cells = []
        for term in m.group(2).split(","):
            t = term.strip()
            if not t or "REFRESH_EOL" in t:
                continue
            t = t.replace("MAP_CELL_W", "64")
            if not re.fullmatch(r"[\d\s+*()-]+", t):
                cells = None
                break
            v = eval(t)                       # only arithmetic on integers reaches here
            cells.append([v % 64, v // 64])
        if cells is not None:
            lists[m.group(1)] = cells
    out = {}
    for b in re.split(r"static\s+TerrainTypeClass\s+const\s+", src)[1:]:
        ini = re.search(r'"([A-Z0-9]{2,8})"', b)
        refs = re.findall(r"\(short const\*\)\s*(_\w+)", b)
        if ini and refs:
            out[ini.group(1).upper()] = lists.get(refs[0], [[0, 0]]) or [[0, 0]]
    return out


def gdi_emblem(out_dir):
    """The GDI eagle, cropped square, for the editor's header.

    Straight out of the cartridge's own art (GDI.png in the romdump's PNG tree, the
    160x150 emblem), squared about the disc so it can be spun on its axis without
    wobbling. Nothing is redrawn: the pixels are the game's.
    """
    from PIL import Image
    src = os.path.join(PNG, "GDI.png")
    if not os.path.exists(src):
        return None
    im = Image.open(src).convert("RGBA")
    box = im.split()[3].getbbox() or (0, 0, im.width, im.height)
    im = im.crop(box)
    side = max(im.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
    sq.save(os.path.join(out_dir, "gdi.png"))
    return dict(file="gdi.png", size=side, source="GDI.png")


def cameos(out_dir):
    """The cartridge's own sidebar cameos, packed into one sheet for the palette.

    101 records, 32x24 or 32x25 RGBA, already extracted by tools/romdump/cameos.py.
    They come in two colourways and the cartridge chooses between them on character 2
    of the scenario name: CAM_<TYPE> is the GDI plate, CAM<TYPE>2 the Nod one. That is
    the same switch that picks the infantry colourway, so the palette can honour it
    and show the side you are actually placing for."""
    from PIL import Image
    import glob
    W, H, COLS = 32, 25, 12
    rows = {}
    for f in sorted(glob.glob(os.path.join(PNG, "CAM*.png"))):
        n = os.path.basename(f)[:-4]
        if n.startswith("CAM_"):
            code, side = n[4:], "gdi"
        elif n.endswith("2"):
            code, side = n[3:-1], "nod"
        else:
            continue
        rows.setdefault(code.upper(), {})[side] = f
    keys = sorted(rows)
    sheet = Image.new("RGBA", (COLS * W, ((len(keys) * 2 + COLS - 1) // COLS) * H),
                      (0, 0, 0, 0))
    index, i = {}, 0
    for code in keys:
        ent = {}
        for side in ("gdi", "nod"):
            f = rows[code].get(side)
            if not f:
                continue
            im = Image.open(f).convert("RGBA")
            x, y = (i % COLS) * W, (i // COLS) * H
            sheet.paste(im, (x, y))
            ent[side] = [x, y, im.width, im.height]
            i += 1
        index[code] = ent
    sheet.save(os.path.join(out_dir, "cameos.png"))
    return dict(sheet="cameos.png", w=sheet.width, h=sheet.height, cells=index)


def main():
    os.makedirs(OUT, exist_ok=True)
    tmpl, tnames = templates()
    tbl = dict(
        note="every row here is the engine's own answer, from EA's GPL Tiberian "
             "Dawn source or the cartridge's theater tables. The editor must agree "
             "with it cell for cell or the maps it makes are quietly wrong.",
        land=LAND,
        ground=ground_table(),
        templates=tmpl,
        templateNames=[n.replace("TEMPLATE_", "") for n in tnames],
        names=names(),
        slots=slots(),
        bibbed=bibbed(),
        cameos=cameos(OUT),
        holeSlots=hole_slots(),
        occupy=occupy(),
        emblem=gdi_emblem(OUT),
    )
    json.dump(tbl, open(os.path.join(OUT, "editor_tables.json"), "w"),
              separators=(",", ":"))
    n_alt = sum(1 for t in tmpl if t["altIcons"])
    print(f"templates {len(tmpl)}, with a walkable notch {n_alt}, "
          f"slots DESERT {len(tbl['slots']['DESERT'])} TEMPERAT "
          f"{len(tbl['slots']['TEMPERAT'])}, bibbed "
          f"{sum(1 for v in tbl['bibbed'].values() if v)}/{len(tbl['bibbed'])}")
    print("ground:", {k: (v["build"], v["passable"]) for k, v in tbl["ground"].items()})
    print(f"cameos: {len(tbl['cameos']['cells'])} types on a "
          f"{tbl['cameos']['w']}x{tbl['cameos']['h']} sheet")
    print(f"display names: {len(tbl['names'])} from the brain's own text table")
    print("sea slots:", {k: len(v) for k, v in tbl["holeSlots"].items()})
    print("emblem:", tbl["emblem"])
    off = {k: v for k, v in tbl["occupy"].items() if v != [[0, 0]]}
    print(f"occupy lists: {len(tbl['occupy'])} scenery types, "
          f"{len(off)} of which do NOT stand on their own stored cell")
    print("  e.g.", {k: tbl["occupy"][k] for k in list(off)[:5]})


if __name__ == "__main__":
    main()
