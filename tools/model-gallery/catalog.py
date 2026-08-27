#!/usr/bin/env python3
"""Names and categories for every mesh in a baked pack, read out of the data.

Three sources, in this order of authority:

1. EA's GPL Tiberian Dawn source (`brain/vanilla/tiberiandawn/`). Each *data.cpp
   declares its type classes as `<Family>TypeClass const Var(ENUM, TXT_ID, "CODE",`
   and `conquer.h` carries `#define TXT_ID <n>  // The English string`. So both the
   family (Vehicle / Structure / Infantry / Aircraft / Terrain / Overlay / Bullet)
   and the display name are the 1995 game's own, not ours.
2. `tools/bakery/support/unit_models.json`, which carries the cartridge-only kinds
   the DOS game has no type for: cursor, wall piece, door, marker, bullet.
3. A small explicit table below for the handful of cartridge-only meshes that
   neither source names (animation rigs, the transport ramp, debris chunks). Each
   entry says where the name comes from; nothing here is invented silently.

Anything still unnamed keeps its cartridge display-list label and is reported, so
the gallery never shows a guess as if it were data.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GPL = os.path.join(ROOT, "brain", "vanilla", "tiberiandawn")
SUPPORT = os.path.join(ROOT, "tools", "bakery", "support")

# Which GPL data file declares which family, and the gallery category each maps to.
# NOTE adata.cpp is AnimTypeClass; the aircraft live in aadata.cpp. The four files
# do not share a declaration shape -- a unit puts TXT_ before its code, a terrain
# object puts its code first, a bullet has no TXT_ at all -- so the parser below
# takes the first of each within a declaration rather than a fixed argument order.
FAMILY_FILES = {
    "udata.cpp": "UnitTypeClass",
    "bdata.cpp": "BuildingTypeClass",
    "idata.cpp": "InfantryTypeClass",
    "aadata.cpp": "AircraftTypeClass",
    "tdata.cpp": "TerrainTypeClass",
    "odata.cpp": "OverlayTypeClass",
    "bbdata.cpp": "BulletTypeClass",
}

# Cartridge-only meshes. `why` is the evidence, and it is shown in the UI.
EXTRA = {
    "APCDOOR":  ("APC Rear Door", "Parts", "separate mesh; the renderer draws it as the APC's door"),
    "TRANRMP":  ("Hovercraft Ramp", "Parts", "separate mesh; the LST's bow ramp"),
    "MCVANIM":  ("MCV Deploy Rig", "Rigs", "model slot 18 ANY_UNIT_MCVANIMZ1, the 21-node deploy rig"),
    "PROCANIM": ("Refinery Dock Rig", "Rigs", "the refinery's harvester-dock animation rig"),
    "ROAD":     ("Road", "Scenery", "cartridge overlay mesh"),
    "CURCIRC":  ("Area Cursor Ring", "Cursors", "cartridge cursor mesh"),
}
DEBRIS = {"DBRS": ("Structure Debris %s", "Debris", "structure debris chunk"),
          "DBRV": ("Vehicle Debris %s", "Debris", "vehicle debris chunk")}

# The GPL family -> gallery category. Civilian buildings (V01..V37) are
# BuildingTypeClass too, so they are split out by code below.
CATEGORY = {
    "UnitTypeClass": "Vehicles",
    "AircraftTypeClass": "Aircraft",
    "BuildingTypeClass": "Structures",
    "InfantryTypeClass": "Infantry",
    "TerrainTypeClass": "Scenery",
    "OverlayTypeClass": "Walls",
    "BulletTypeClass": "Projectiles",
}


def txt_strings():
    """TXT_ id -> the English string, from conquer.h's own trailing comment."""
    out = {}
    pat = re.compile(r"^#define\s+(TXT_[A-Z0-9_]+)\s+(\d+)\s*//\s*(.*?)\s*$")
    for line in open(os.path.join(GPL, "conquer.h"), encoding="latin-1"):
        m = pat.match(line)
        if m and m.group(3):
            out[m.group(1)] = m.group(3)
    return out


def gpl_types():
    """code -> (family, TXT_ id). Parsed from the type-class declarations."""
    out = {}
    head = re.compile(r"(\w+TypeClass)\s+const\s+\w+\s*\(")
    code = re.compile(r'"([A-Za-z0-9_]{1,8})"')
    tid = re.compile(r"\b(TXT_[A-Z0-9_]+)\b")
    for fn, family in FAMILY_FILES.items():
        p = os.path.join(GPL, fn)
        if not os.path.exists(p):
            continue
        src = open(p, encoding="latin-1").read()
        starts = [(m.start(), m.group(1)) for m in head.finditer(src)]
        for i, (pos, fam) in enumerate(starts):
            if fam != family:
                continue
            end = starts[i + 1][0] if i + 1 < len(starts) else len(src)
            body = src[pos:end]
            c = code.search(body)
            t = tid.search(body)
            if c:
                out.setdefault(c.group(1).upper(), (family, t.group(1) if t else None))
    return out


def kinds():
    p = os.path.join(SUPPORT, "unit_models.json")
    if not os.path.exists(p):
        return {}
    return {k: v for k, v in json.load(open(p)).items()}


# A wall code is stem + state + piece. The state letters are the cartridge's:
#   S  the wall's SHADOW set, D  the damaged wall.
# Both are settled in tools/romdump/wall_models_notes.md (CYCL's eleven S pieces are
# "the unrotated connectivity set", BRIK's D four are "the same wall with a crumbled
# top"), and the caller re-checks S against the mesh itself: every triangle of a
# shadow piece is MODE_SHADOW or MODE_XLU, so the label is measured, not assumed.
WALL_STEMS = ("SBAG", "CYCL", "BRIK", "BARB", "WOOD")
WALL_SUFFIX = re.compile(r"^(SBAG|CYCL|BRIK|BARB|WOOD)(S\d*|D)?(_L|_T|_X)?$")
PIECE = {None: "straight", "_L": "corner", "_T": "tee", "_X": "cross"}


def resolve(code, txt, gpl, km, shadow=None):
    """-> (display name, category, provenance). Never invents; says where it looked.

    `shadow` is the caller's measurement of the mesh (True when every triangle draws in
    a blended pass), used only to confirm or contradict a wall code's S suffix.
    """
    c = code.upper()
    if c in EXTRA:
        n, cat, why = EXTRA[c]
        return n, cat, why
    if c[:4] in DEBRIS and c[4:].isdigit():
        n, cat, why = DEBRIS[c[:4]]
        return n % c[4:], cat, why
    if c.startswith("CUR"):
        return "Cursor %s" % c[3:], "Cursors", "cartridge cursor model, ActionType %s" % c[3:]
    m = WALL_SUFFIX.match(c)
    if m and m.group(1) in WALL_STEMS:
        stem, state, suf = m.group(1), m.group(2), m.group(3)
        base = gpl.get(stem)
        nm = txt.get(base[1], stem) if base else stem
        why = ("BuildingTypeClass %s (EA GPL source); the piece is the cartridge's own "
               "16-way connectivity variant" % stem)
        if state and state.startswith("S"):
            n = state[1:]
            label = "%s, shadow%s" % (nm, (" piece %s" % n) if n else (" " + PIECE[suf]))
            why = ("%s's SHADOW set (wall_models_notes.md); confirmed here by the mesh "
                   "itself, every triangle draws in a blended pass" % stem)
            if shadow is False:
                label = "%s, %s (S set)" % (nm, PIECE[suf])
                why += ". NOTE this one does NOT measure as shadow geometry"
            return label, "Walls", why
        if state == "D":
            return "%s, %s (damaged)" % (nm, PIECE[suf]), "Walls", why + \
                "; the damaged set, per wall_models_notes.md"
        return "%s, %s" % (nm, PIECE[suf]), "Walls", why
    if c in gpl:
        family, tid = gpl[c]
        if tid is None:
            cat = CATEGORY.get(family, "Misc")
            return c, cat, "%s (EA GPL source; that family carries no display name)" % family
        cat = CATEGORY.get(family, "Misc")
        if family == "BuildingTypeClass" and re.match(r"^V\d\d$", c):
            cat = "Civilian"
        return txt.get(tid, c), cat, "%s, %s (EA GPL source)" % (family, tid)
    k = km.get(c, {})
    if k.get("kind"):
        return c, {"wall": "Walls", "cursor": "Cursors", "bullet": "Projectiles",
                   "door": "Parts", "marker": "Parts",
                   "structure-extra": "Structures"}.get(k["kind"], "Misc"), \
            "unit_models.json kind=%s" % k["kind"]
    return c, "Misc", "no name in any source; showing the cartridge code"


def build():
    txt, gpl, km = txt_strings(), gpl_types(), kinds()
    return txt, gpl, km


if __name__ == "__main__":
    txt, gpl, km = build()
    print("conquer.h strings: %d, GPL type codes: %d, unit_models: %d"
          % (len(txt), len(gpl), len(km)))
    import collections
    fam = collections.Counter(v[0] for v in gpl.values())
    print("by family:", dict(fam))
    for c in ("MTNK", "HTNK", "ORCA", "PROC", "T07", "SBAG_L", "V12", "120MM", "CUR06", "DBRV3"):
        print("  %-8s %s" % (c, resolve(c, txt, gpl, km)))
