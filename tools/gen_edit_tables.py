#!/usr/bin/env python3
"""gen_edit_tables.py -- bake the map editor's legality tables into a C header.

WHY A HEADER AND NOT A FILE THE GAME READS

cnc_eyes carries no JSON parser and no INI parser on purpose -- its module comment says
so, and the reason is the Win98/Voodoo2 target, where every dependency is a liability.
The browser prototype read these tables out of editor_tables.json at runtime; the native
editor cannot, so the same numbers are emitted as C at build time. One source, two
consumers, no parser.

WHERE THE NUMBERS COME FROM

Not from here. This script imports tools/heightmap-viewer/export_editor.py and uses its
parsers, which read EA's GPL Tiberian Dawn source directly:

    Ground[LAND_COUNT]      const.cpp:245  -- what may be built on and walked on
    TemplateTypeClass       cdata.cpp      -- every template's Land, AltLand and the
                                              icon list that earns the AltLand

The AltLand list is the one that matters and the one that is easy to miss: a cliff
template is LAND_ROCK, but the specific icons in its AltIcons list are LAND_CLEAR --
that is how a ramp exists at all. Eleven of the thirty-eight SLOPE templates carry such
a notch. Miss it and every ramp in the game reads as unbuildable rock.

Run:  python3 tools/gen_edit_tables.py        # writes game/edit_tables.h
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "heightmap-viewer"))

import export_editor as EX  # noqa: E402

BRAIN = os.path.join(ROOT, "brain", "vanilla", "tiberiandawn")
OUT = os.path.join(ROOT, "game", "edit_tables.h")



# ------------------------------------------------------------------------------------
#  The placeable catalog
#
#  The native editor shipped with eight hand-transcribed footprints and six of them were
#  wrong, because they were written down as boxes: PROC is four cells inside a 3x2, FIX
#  is a plus, PYLE occupies two cells of its 2x2. A box is not a footprint, and the
#  engine has never used one -- placement is checked against Occupy_List(placement=true)
#  (bdata.cpp:4278), which walks a cell list and, for a bibbed building, PREPENDS the
#  bib: a WxH smudge sitting at row Height()-1, one row taller than the building's own
#  box. That is why a Construction Yard is BSIZE_32 in the table and 3x3 on the ground.
#
#  So: read the lists.
# ------------------------------------------------------------------------------------

def _cell_lists(src):
    """Every `static short const NAME[] = {...}` cell table, as (dx,dy) pairs."""
    out = {}
    for m in re.finditer(
            r"static\s+(?:volatile\s+)?short\s+const\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\}\s*;",
            src, re.S):
        cells, ok = [], True
        for term in m.group(2).split(","):
            t = term.strip()
            if not t or "REFRESH_EOL" in t:
                continue
            t = t.replace("MAP_CELL_W", "64").replace("MCW", "64")
            if not re.fullmatch(r"[\d\s+*()-]+", t):
                ok = False
                break
            v = eval(t)                       # arithmetic on integers only
            cells.append((v % 64, v // 64))
        if ok and cells:
            out[m.group(1)] = cells
    return out


def _buildings():
    src = open(os.path.join(BRAIN, "bdata.cpp"), errors="ignore").read()
    lists = _cell_lists(src)
    bib = EX.bibbed()
    out = []
    for blk in re.split(r"static\s+BuildingTypeClass\s+const\s+", src)[1:]:
        ini = re.findall(r'"([A-Z0-9]{2,8})"', blk)
        if not ini:
            continue
        code = ini[0]
        occ = re.search(r"\(short const\*\)\s*(\w+),\s*//\s*OCCUPYLIST", blk)
        strn = re.search(r"(\d+),\s*//\s*STRNTH", blk)
        size = re.search(r"BSIZE_(\d)(\d)", blk)
        wall = re.search(r"(true|false),\s*//\s*Is this a wall type structure\?", blk)
        cells = set(lists.get(occ.group(1), []) if occ else [])
        w, h = (int(size.group(1)), int(size.group(2))) if size else (1, 1)
        if bib.get(code):
            for x in range(w):
                for y in (h - 1, h):
                    cells.add((x, y))
        out.append({"code": code, "cells": sorted(cells or {(0, 0)}, key=lambda c: (c[1], c[0])),
                    "str": int(strn.group(1)) if strn else 256,
                    "wall": bool(wall and wall.group(1) == "true")})
    return out


def _one_cell(fname, cls, dstr):
    src = open(os.path.join(BRAIN, fname), errors="ignore").read()
    out = []
    for blk in re.split(r"static\s+%s\s+const\s+" % cls, src)[1:]:
        ini = re.findall(r'"([A-Z0-9]{1,8})"', blk)
        strn = (re.search(r"(\d+),\s*//\s*STRENGTH", blk)
                or re.search(r"(\d+),\s*//\s*STRNTH", blk))
        if ini:
            out.append({"code": ini[0], "cells": [(0, 0)],
                        "str": int(strn.group(1)) if strn else dstr, "wall": False})
    return out


def _terrain():
    return [{"code": c, "cells": [tuple(x) for x in cells], "str": 800, "wall": False}
            for c, cells in sorted(EX.occupy().items())]


def _tiberium():
    """ONE brush, and the only group in this catalog that is stated rather than read.

    The engine has twelve tiberium overlays and throws the choice away: Overpass runs
    over the playable rectangle as the scenario is read and Tiberium_Adjust re-picks the
    overlay at random across TIBERIUM1..12 for every cell (cell.cpp:1912). Twelve palette
    entries would be twelve ways to author a number nobody reads back, and the growth
    stage is derived by the same pass rather than authored either.

    Strength 1 matches the wall rows: an overlay cell carries no health field."""
    return [{"code": "TI1", "name": "Tiberium", "cells": [(0, 0)],
             "str": 1, "wall": False}]


def _smudges():
    """The authorable smudges: one crater row and six scorch rows, not fifteen.

    THE ENGINE RE-PICKS A CRATER rather than taking the one the file names. For an
    IsCrater type SmudgeClass::Mark stores SMUDGE_CRATER1 + CellClass::Spot_Index(Coord)
    (smudge.cpp:229), and Read_INI hands it Cell_Coord(cell), which is the cell CENTRE:
    both leptons are CELL_LEPTON_W / 2 (function.h:877), and Spot_Index answers 0 for the
    centre (cell.cpp:1638). So every crater in every INI loads as CRATER1 whatever it was
    called, and Read_INI then keeps the stack level only when the cell's smudge matches
    the one it asked for (smudge.cpp:301), so a CR3 row would throw its data away too.
    That is the same reason tiberium above is one brush rather than twelve. The shipped
    missions agree: over the 100 shipped mission files, 25 of which carry an authored [SMUDGE] section, 221 rows, 47 of them
    CR1 and not one CR2..CR6.

    A scorch is not IsCrater, so Mark stores the type it was handed, and the six are six
    different sprites. Those get six rows.

    Bibs get none. They are the aprons the engine lays under a building by itself, the
    writer never emits one, and authoring one would strand it under nothing the moment
    the building moved.

    Strength 1 matches the wall and tiberium rows: a smudge cell carries no health
    field."""
    return ([{"code": "CR1", "name": "Crater", "cells": [(0, 0)],
              "str": 1, "wall": False}]
            + [{"code": "SC%d" % i, "name": "Scorch Mark %d" % i, "cells": [(0, 0)],
                "str": 1, "wall": False} for i in range(1, 7)])


def catalog():
    """Everything placeable, in the seven groups the sidebar shows.

    Walls come out of the buildings group: the engine models them as BuildingTypeClass
    with IsWall, but they are drawn, saved and edited as overlay, and the editor has a
    wall tool for them already."""
    b = _buildings()
    return [("BUILDING", [x for x in b if not x["wall"]]),
            ("UNIT",     _one_cell("udata.cpp", "UnitTypeClass", 128)),
            ("INFANTRY", _one_cell("idata.cpp", "InfantryTypeClass", 50)),
            ("TERRAIN",  _terrain()),
            ("WALL",     [x for x in b if x["wall"]]),
            ("TIBERIUM", _tiberium()),
            ("SMUDGE",   _smudges())]


def _structtypes():
    """StructType index -> INI code, in the engine's own order.

    ``Built It`` stores a raw StructType index in a trigger's Data field, so an editor
    that shows that number rather than a building name shows nothing at all. The order
    is NOT the declaration order in bdata.cpp -- it is the order of the Pointers table,
    which is the only place StructType and the class objects are tied together."""
    src = open(os.path.join(BRAIN, "bdata.cpp"), errors="ignore").read()
    tbl = re.search(r"BuildingTypeClass const\* const BuildingTypeClass::Pointers"
                    r"\[STRUCT_COUNT\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not tbl:
        raise SystemExit("gen_edit_tables: the StructType Pointers table has moved")
    order = re.findall(r"&(Class\w+),", tbl.group(1))

    # Each &ClassFoo names a `static BuildingTypeClass const ClassFoo(` block, whose
    # first quoted string is the INI code.
    code_of = {}
    for blk in re.split(r"static\s+BuildingTypeClass\s+const\s+", src)[1:]:
        nm = re.match(r"(\w+)", blk)
        ini = re.findall(r'"([A-Z0-9]{2,8})"', blk)
        if nm and ini:
            code_of[nm.group(1)] = ini[0]
    missing = [c for c in order if c not in code_of]
    if missing:
        raise SystemExit("gen_edit_tables: no INI code for %s" % ", ".join(missing))
    return [code_of[c] for c in order]


def emit_catalog(w):
    cats = catalog()
    names = EX.names()
    widest = max(len(r["cells"]) for _, rows in cats for r in rows)
    total = sum(len(rows) for _, rows in cats)

    w("")
    w("/* ------------------------------------------------------------------------------")
    w(" *  The placeable catalog: every type the editor can put on a map, with the cells")
    w(" *  it really occupies. NOT a width and a height -- PROC is four cells inside a")
    w(" *  3x2 and FIX is a plus. Bibbed buildings carry their bib row, because")
    w(" *  Occupy_List(placement) prepends it and the engine checks against that.")
    w(" * ---------------------------------------------------------------------------- */")
    w("")
    w("enum EditKind {")
    for i, (nm, _) in enumerate(cats):
        w("    EDIT_KIND_%-9s = %d," % (nm, i))
    w("    EDIT_KIND_COUNT   = %d" % len(cats))
    w("};")
    w("")
    w("#define EDIT_CELL_MAX  %d" % widest)
    w("#define EDIT_ITEM_N    %d" % total)
    w("")
    w("struct EditItem {")
    w("    const char*    code;      /* the INI code, and the cameo's name            */")
    w("    const char*    name;      /* the game's own display name                   */")
    w("    unsigned char  kind;      /* EditKind                                      */")
    w("    unsigned short str;       /* full strength                                 */")
    w("    unsigned char  n;         /* how many of dx/dy are used                    */")
    w("    signed char    dx[EDIT_CELL_MAX], dy[EDIT_CELL_MAX];")
    w("};")
    w("")
    w("static const EditItem EDIT_ITEMS[EDIT_ITEM_N] = {")
    first = {}
    i = 0
    for nm, rows in cats:
        first[nm] = (i, len(rows))
        w("    /* ---- %s ---- */" % nm)
        for r in rows:
            cells = r["cells"][:widest]
            dx = [str(c[0]) for c in cells] + ["0"] * (widest - len(cells))
            dy = [str(c[1]) for c in cells] + ["0"] * (widest - len(cells))
            disp = (r.get("name") or names.get(r["code"], r["code"])).replace('"', "'")
            w('    { "%s", "%s", EDIT_KIND_%s, %d, %d, {%s}, {%s} },'
              % (r["code"], disp, nm, r["str"], len(cells),
                 ", ".join(dx), ", ".join(dy)))
            i += 1
    w("};")
    w("")
    w("/* Where each group starts in EDIT_ITEMS, and how many it has. */")
    w("static const int EDIT_KIND_FIRST[EDIT_KIND_COUNT] = { %s };"
      % ", ".join(str(first[nm][0]) for nm, _ in cats))
    w("static const int EDIT_KIND_N[EDIT_KIND_COUNT]     = { %s };"
      % ", ".join(str(first[nm][1]) for nm, _ in cats))
    w("static const char* const EDIT_KIND_NAME[EDIT_KIND_COUNT] = { %s };"
      % ", ".join('"%s"' % nm for nm, _ in cats))
    return cats


def main():
    ground = EX.ground_table()
    tmpl, names = EX.templates()

    # The widest AltIcons list decides the row width. Measured, not guessed: a fixed 8
    # would silently truncate a template with more, and the truncation would show up as
    # a ramp nobody can walk on rather than as an error.
    widest = max((len(t["altIcons"]) for t in tmpl if t), default=0)

    L = []
    w = L.append
    w("/* ====================================================================================")
    w(" *  edit_tables.h -- GENERATED by tools/gen_edit_tables.py. Do not edit by hand.")
    w(" *")
    w(" *  The map editor's legality tables, lifted from EA's GPL Tiberian Dawn source:")
    w(" *  Ground[LAND_COUNT] (const.cpp) and TemplateTypeClass (cdata.cpp). The browser")
    w(" *  prototype read the same numbers out of editor_tables.json at runtime; cnc_eyes")
    w(" *  has no JSON parser and is not getting one, so they are baked in here instead.")
    w(" *")
    w(" *  A cell's LandType is NOT simply its template's Land. A template also carries an")
    w(" *  AltLand and a list of icons that earn it, which is how a cliff of LAND_ROCK has")
    w(" *  walkable LAND_CLEAR notches in it. Resolve through edit_land_of().")
    w(" * ==================================================================================== */")
    w("")
    w("#ifndef EDIT_TABLES_H")
    w("#define EDIT_TABLES_H")
    w("")
    w("enum EditLand {")
    for i, nm in enumerate(EX.LAND):
        w("    EDIT_LAND_%-9s = %d," % (nm, i))
    w("    EDIT_LAND_COUNT   = %d" % len(EX.LAND))
    w("};")
    w("")
    w("static const char* const EDIT_LAND_NAME[EDIT_LAND_COUNT] = {")
    w("    " + ", ".join('"%s"' % n for n in EX.LAND))
    w("};")
    w("")
    w("/* Ground[].Build and a foot cost of zero meaning impassable (const.cpp:245). */")
    w("static const unsigned char EDIT_LAND_BUILD[EDIT_LAND_COUNT] = {")
    w("    " + ", ".join("1" if ground[n]["build"] else "0" for n in EX.LAND))
    w("};")
    w("static const unsigned char EDIT_LAND_PASS[EDIT_LAND_COUNT] = {")
    w("    " + ", ".join("1" if ground[n]["passable"] else "0" for n in EX.LAND))
    w("};")
    w("")
    w("#define EDIT_TEMPLATE_COUNT %d" % len(tmpl))
    w("#define EDIT_ALT_MAX        %d" % widest)
    w("")
    w("/* The template's INI name, which is also how the editor groups the terrain")
    w("   drawers: CLEAR/P are ground, W water, SH shore, RV/FALLS/FORD rivers, D roads,")
    w("   B/BR rock, BRIDGE bridges. Cliffs (S*) are deliberately not offered as paint --")
    w("   they are derived from elevation. */")
    w("struct EditTemplate {")
    w("    const char*   name;")
    w("    unsigned char land;      /* EditLand                                        */")
    w("    unsigned char altland;   /* EditLand for the icons in alt[]                 */")
    w("    unsigned char nalt;      /* how many of alt[] are used                      */")
    w("    unsigned char alt[EDIT_ALT_MAX];")
    w("    unsigned char w, h;      /* the template's size in cells                    */")
    w("};")
    w("")
    w("static const EditTemplate EDIT_TEMPLATES[EDIT_TEMPLATE_COUNT] = {")
    for i, t in enumerate(tmpl):
        if not t:
            w('    { "", 0, 0, 0, {%s}, 1, 1 },  /* %d: absent */'
              % (", ".join(["0"] * widest), i))
            continue
        alt = list(t["altIcons"])[:widest]
        alt += [0] * (widest - len(alt))
        w('    { "%s", EDIT_LAND_%s, EDIT_LAND_%s, %d, {%s}, %d, %d },  /* %d */'
          % (t["ini"], t["land"], t["altLand"], len(t["altIcons"]),
             ", ".join(str(a) for a in alt), t["w"], t["h"], i))
    w("};")
    w("")
    w("/* The LandType of one cell, from the (template, icon) pair a .BIN records.")
    w("   255 is TEMPLATE_NONE, which every shipped map uses for plain ground. */")
    w("static inline int edit_land_of(int tmpl_id, int icon)")
    w("{")
    w("    if (tmpl_id == 255) return EDIT_LAND_CLEAR;")
    w("    if (tmpl_id < 0 || tmpl_id >= EDIT_TEMPLATE_COUNT) return EDIT_LAND_CLEAR;")
    w("    {")
    w("        const EditTemplate* t = &EDIT_TEMPLATES[tmpl_id];")
    w("        for (int i = 0; i < (int)t->nalt; i++)")
    w("            if ((int)t->alt[i] == icon) return (int)t->altland;")
    w("        return (int)t->land;")
    w("    }")
    w("}")
    w("")
    cats = emit_catalog(w)
    w("")
    st = _structtypes()
    w("")
    w("/* ------------------------------------------------------------------------------")
    w(" *  StructType, in the engine's own order (bdata.cpp, the Pointers table).")
    w(" *")
    w(" *  A `Built It` trigger stores one of these indices in its Data field, so an")
    w(" *  editor that shows the raw number shows nothing at all: SCB05EA's")
    w(" *  `cnst=Built It,Dstry Trig 'YYYY',6` is really \"when Nod finishes a")
    w(" *  Construction Yard\". Civilian structures follow the buildable ones and are")
    w(" *  legal Data values too, however unlikely.")
    w(" * ---------------------------------------------------------------------------- */")
    w("")
    w("#define EDIT_STRUCT_N %d" % len(st))
    w("static const char* const EDIT_STRUCT_CODE[EDIT_STRUCT_N] = {")
    for i in range(0, len(st), 8):
        w("    " + ", ".join('"%s"' % c for c in st[i:i + 8]) + ("," if i + 8 < len(st) else ""))
    w("};")
    w("")
    w("#endif /* EDIT_TABLES_H */")

    with open(OUT, "w") as fh:
        fh.write("\n".join(L) + "\n")

    notched = sum(1 for t in tmpl if t and t["altIcons"])
    print("wrote %s" % os.path.relpath(OUT, ROOT))
    print("  %d land types, %d templates, %d of them with a walkable notch, widest alt list %d"
          % (len(EX.LAND), len(tmpl), notched, widest))
    print("  buildable: %s" % ", ".join(n for n in EX.LAND if ground[n]["build"]))
    for nm, rows in cats:
        print("  %-9s %d placeable" % (nm, len(rows)))
    print("  passable : %s" % ", ".join(n for n in EX.LAND if ground[n]["passable"]))


if __name__ == "__main__":
    main()
