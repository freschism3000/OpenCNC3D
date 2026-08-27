#!/usr/bin/env python3
"""CNC3D -- the viewer's 3D ASSET layer: every mission's objects, and the meshes.

Two outputs, both derived from files the repo already holds:

  public/data/models.bin    every named N64 mesh as an int16 triangle soup
  public/data/models.json   name -> [firstTriangle, triangleCount, bbox]

and, folded into each per-scenario JSON by export.py, the objects that mission
places: kind, type, cell, facing, house.

THE TRANSFORMS ARE NOT DERIVED HERE, THEY ARE COPIED FROM THE SHIPPED RENDERER.
game/cnc_eyes.cpp is the game people actually play, so it is the authority:

  MODEL_SCALE  = 1/1024                       cnc_eyes.cpp:433
  vertex       vwx = cx + (vx*cos + vz*sin) * MODEL_SCALE     cnc_eyes.cpp:5278
               vwz = cz + (-vx*sin + vz*cos) * MODEL_SCALE    cnc_eyes.cpp:5279
               vwy = vy * MODEL_SCALE + bias                  cnc_eyes.cpp:5332
  facing       a = -face * 2*pi/256                           cnc_eyes.cpp:4955

Those are in CELL units, which is what the viewer's terrain mesh uses too (a cell
is 1.0 wide, a corner's Y is heightByte/64), so the two line up with no fudge.

Cell numbering is Tiberian Dawn's: x = cell & 63, y = cell >> 6, MAP_CELL_W 64.

Building footprints come out of EA's GPL source, not a table anyone typed:
brain/vanilla/tiberiandawn/bdata.cpp, the BSIZE_wh field of every
BuildingTypeClass. A building's INI cell is its top-left cell, so the mesh is
placed at the footprint's centre.

Read-only on everything it reads.
"""
import json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "bakery", "sharecopy", "assets")
MODELS_DIR = os.path.join(ASSETS, "models_full")
BDATA = os.path.join(HERE, "..", "..", "brain", "vanilla", "tiberiandawn", "bdata.cpp")

# The sections we read, and the field the type code sits in. None = the value IS
# the type code (possibly with a trailing field), and the KEY is the cell.
#   TERRAIN     cell = TYPE,trigger
#   OVERLAY     cell = TYPE
#   SMUDGE      cell = TYPE,cell,data
#   STRUCTURES  idx  = house,TYPE,health,cell,facing,trigger
#   UNITS       idx  = house,TYPE,health,cell,facing,mission,trigger
#   INFANTRY    idx  = house,TYPE,health,cell,subcell,mission,facing,trigger
KINDS = ["terrain", "structure", "unit", "infantry", "overlay", "smudge"]
KIND_ID = {k: i for i, k in enumerate(KINDS)}

TIBERIUM = {f"TI{i}" for i in range(1, 13)}
WALLS = {"SBAG", "CYCL", "BRIK", "BARB", "WOOD"}
CRATES = {"WCRATE", "SCRATE"}

# ---------------------------------------------------------------- walls
# The meshes named SBAG/CYCL/BRIK/BARB/WOOD in models_full are NOT walls: they
# are the bullet models at bank slots 93..97 (DRAGON, MISSILE, BOMBLET, BOMB,
# ATOMICUP), which tools/romdump/wall_models_notes.md says outright. The real
# wall geometry is four pieces per type (straight, corner, T, cross) at the model
# table slots below, read straight out of the ROM's display lists.
ROM_PATH = os.path.join(HERE, "..", "..", "data", "rom", "cnc_eu.z64")
MODEL_TABLE = os.path.join(HERE, "..", "bakery", "support", "n64_model_table.json")
GEOM_DELTA = 0x8005C590        # GameModelsGeometry overlay: RAM = ROM + delta
WALL_SLOTS = {"SBAG": 153, "CYCL": 161, "BRIK": 176, "BARB": 184, "WOOD": 188}

# Which of the four pieces to draw and how to turn it, indexed by a 4-bit
# neighbour mask (bit0 N, bit1 E, bit2 S, bit3 W, set when the neighbouring cell
# holds the SAME wall type). Transcribed from the cartridge's own 12-byte-record
# table at RAM 0x802100D0; cnc_eyes.cpp:1938-1947 reads the same one.
WALL_VARIANT = [(0, 0), (0, 192), (0, 0), (1, 64),
                (0, 192), (0, 192), (1, 0), (2, 64),
                (0, 0), (1, 128), (0, 0), (2, 128),
                (1, 192), (2, 192), (2, 0), (3, 0)]


def wall_meshes():
    """-> {"SBAG0": [triangle floats], ...}, 20 meshes, 234 triangles."""
    sys.path.insert(0, os.path.join(HERE, "..", "romdump"))
    try:
        import model as M
    except ImportError:
        return {}
    if not os.path.exists(ROM_PATH) or not os.path.exists(MODEL_TABLE):
        return {}
    rom = open(ROM_PATH, "rb").read()
    tbl = {e["i"]: e for e in json.load(open(MODEL_TABLE))}
    out = {}
    for name, base in WALL_SLOTS.items():
        for k in range(4):
            e = tbl.get(base + k)
            if not e:
                continue
            try:
                verts, faces = M.walk(rom, int(e["dl"], 16), GEOM_DELTA)
            except Exception:
                continue
            tris = []
            for f in faces:
                for i in f[:3]:
                    v = verts[i]
                    tris.extend([int(v["x"]), int(v["y"]), int(v["z"])])
            if tris:
                out[f"{name}{k}"] = tris
    return out

HOUSES = ["GoodGuy", "BadGuy", "Neutral", "Special", "Multi1", "Multi2",
          "Multi3", "Multi4", "Multi5", "Multi6"]
HOUSE_ID = {h.upper(): i for i, h in enumerate(HOUSES)}


def center_base():
    """TerrainTypeClass CenterBase, in CELLS. A tree's INI cell is the top-left of
    its occupy box and its trunk is well south of it: T01's base offset is
    XYP_COORD(11, 41), which is (0.458, 1.708) cells. Placing a tree at cell
    centre puts it nearly two cells north of where the engine draws it.
    Source: brain/vanilla/tiberiandawn/tdata.cpp, the third ctor argument of every
    TerrainTypeClass; ICON_PIXEL_W is 24 (display.h:41)."""
    td = os.path.join(os.path.dirname(BDATA), "tdata.cpp")
    out = {}
    for b in re.split(r"static\s+TerrainTypeClass\s+const\s+", open(td, errors="ignore").read())[1:]:
        cb = re.search(r"XYP_COORD\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", b)
        ini = re.search(r'"([A-Z0-9]{2,8})"', b)
        if cb and ini:
            out[ini.group(1).upper()] = [int(cb.group(1)) / 24.0,
                                         int(cb.group(2)) / 24.0]
    return out


# COORDINATE const StoppingCoordAbs[5], const.cpp:56, as cell fractions.
# 0xYYYYXXXX over 256 world units per cell: centre, then the four quincunx spots.
STOPPING = [[0.5, 0.5], [0.25, 0.25], [0.75, 0.25], [0.25, 0.75], [0.75, 0.75]]


def footprints():
    """type code -> [width, height] in cells, from the GPL BuildingTypeClass table."""
    s = open(BDATA, errors="ignore").read()
    out = {}
    for b in re.split(r"static\s+BuildingTypeClass\s+const\s+", s)[1:]:
        m = re.search(r"BSIZE_(\d)(\d)", b)
        ini = re.search(r'TXT_[A-Z0-9_]+\s*,[^"]*"([A-Za-z0-9]{2,8})"', b, re.S)
        if m and ini:
            out[ini.group(1).upper()] = [int(m.group(1)), int(m.group(2))]
    return out


def load_obj(path):
    """position-only OBJ -> flat list of triangles, each 9 ints."""
    v, tris = [], []
    for line in open(path):
        if line.startswith("v "):
            v.append([int(round(float(x))) for x in line.split()[1:4]])
        elif line.startswith("f "):
            f = [int(t.split("/")[0]) - 1 for t in line.split()[1:]]
            for k in range(1, len(f) - 1):
                for i in (f[0], f[k], f[k + 1]):
                    tris.extend(v[i])
    return tris


def export_models(out_dir):
    blob = bytearray()
    index = {}
    tri0 = 0
    meshes = []
    for f in sorted(os.listdir(MODELS_DIR)):
        if f.endswith(".obj"):
            meshes.append((f[:-4], load_obj(os.path.join(MODELS_DIR, f))))
    meshes += sorted(wall_meshes().items())
    for name, tris in meshes:
        n = len(tris) // 9
        if not n:
            continue
        xs = tris[0::3]; ys = tris[1::3]; zs = tris[2::3]
        index[name] = dict(
            t0=tri0, n=n,
            bbox=[min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)])
        for c in tris:
            blob += struct.pack("<h", max(-32768, min(32767, c)))
        tri0 += n
    open(os.path.join(out_dir, "models.bin"), "wb").write(bytes(blob))
    json.dump(dict(models=index, triangles=tri0,
                   scale=1.0 / 1024.0,
                   centerBase=center_base(), stopping=STOPPING,
                   # models_full's SBAG/CYCL/BRIK/BARB/WOOD are NOT walls. They are
                   # the BULLET models at bank slots 93..97 (DRAGON, MISSILE,
                   # BOMBLET, BOMB, ATOMICUP); tools/romdump/wall_models_notes.md
                   # says so outright. Drawing walls from them puts six thousand
                   # tiny missiles on the map, so the viewer draws a labelled box
                   # instead and this list is why.
                   notMeshes=sorted(WALLS),
                   wallVariant=WALL_VARIANT,
                   note="int16 triangle soup, model space; multiply by scale for "
                        "cell units. Transform copied from game/cnc_eyes.cpp "
                        "(MODEL_SCALE 1/1024, facing angle -face*2pi/256).",
                   footprints=footprints()),
              open(os.path.join(out_dir, "models.json"), "w"),
              separators=(",", ":"))
    return index, len(blob)


def parse_ini(path):
    secs, sec = {}, None
    for line in open(path, errors="ignore"):
        s = line.strip()
        if s.startswith("["):
            sec = s.strip("[]")
            secs.setdefault(sec, [])
        elif s and sec and "=" in s:
            k, v = s.split("=", 1)
            secs[sec].append((k.strip(), v.strip()))
    return secs


def objects_of(ini_path, types):
    return objects_from(parse_ini(ini_path), types)


def objects_from(s, types):
    """-> (packed bytes, per-kind counts). 8 bytes per object:
       kind|house<<4 (u8), typeIndex (u16 LE), cell (u16 LE), facing (u8),
       sub-cell (u8, infantry only), pad (u8)."""
    rec = bytearray()
    counts = {k: 0 for k in KINDS}

    def emit(kind, tcode, cell, facing, house, sub=0):
        """sub carries the infantry sub-cell for INFANTRY and the wall
        connectivity mask for a wall OVERLAY; nothing else uses it."""
        tcode = tcode.strip().upper()
        if not tcode or cell is None or not (0 <= cell < 4096):
            return
        if tcode not in types:
            types[tcode] = len(types)
        rec.extend(struct.pack("<BHHBBB", (KIND_ID[kind] & 15) | ((house & 15) << 4),
                               types[tcode], cell, facing & 255, sub & 15, 0))
        counts[kind] += 1

    def num(x):
        try:
            return int(x)
        except (TypeError, ValueError):
            return None

    # A wall piece depends on its neighbours, so the overlay layer is read once
    # before anything is emitted. Bit 0 N, 1 E, 2 S, 3 W, set when the adjacent
    # cell holds the SAME wall type (CellClass::Wall_Update, cell.cpp:1406).
    wall_at = {}
    for k, v in s.get("OVERLAY", []):
        t = v.split(",")[0].strip().upper()
        c = num(k)
        if t in WALLS and c is not None and 0 <= c < 4096:
            wall_at[c] = t

    def wall_mask(cell, t):
        x, y = cell & 63, cell >> 6
        m = 0
        for bit, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
            nx, ny = x + dx, y + dy
            if 0 <= nx < 64 and 0 <= ny < 64 and wall_at.get(ny * 64 + nx) == t:
                m |= 1 << bit
        return m

    for k, v in s.get("TERRAIN", []):
        emit("terrain", v.split(",")[0], num(k), 0, 2)
    for k, v in s.get("OVERLAY", []):
        t = v.split(",")[0].strip().upper()
        c = num(k)
        emit("overlay", t, c, 0, 2,
             wall_mask(c, t) if (t in WALLS and c is not None) else 0)
    for k, v in s.get("SMUDGE", []):
        emit("smudge", v.split(",")[0], num(k), 0, 2)
    for k, v in s.get("STRUCTURES", []):
        f = v.split(",")
        if len(f) >= 5:
            emit("structure", f[1], num(f[3]), num(f[4]) or 0,
                 HOUSE_ID.get(f[0].strip().upper(), 2))
    for k, v in s.get("UNITS", []):
        f = v.split(",")
        if len(f) >= 5:
            emit("unit", f[1], num(f[3]), num(f[4]) or 0,
                 HOUSE_ID.get(f[0].strip().upper(), 2))
    for k, v in s.get("INFANTRY", []):
        # house,TYPE,health,cell,SUBCELL,mission,facing,trigger
        f = v.split(",")
        if len(f) >= 7:
            emit("infantry", f[1], num(f[3]), num(f[6]) or 0,
                 HOUSE_ID.get(f[0].strip().upper(), 2), (num(f[4]) or 0) % 5)
    return bytes(rec), counts


if __name__ == "__main__":
    out = os.path.join(HERE, "public", "data")
    os.makedirs(out, exist_ok=True)
    idx, nbytes = export_models(out)
    print(f"{len(idx)} models, {nbytes} bytes -> models.bin")
