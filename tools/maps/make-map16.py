#!/usr/bin/env python3
"""
make_map16.py -- author SCM16EA, a SIXTEEN start-position multiplayer map.

Target engine: brain/vanilla (the shipping brain), built MEGAMAPS + EIGHTPLAYERS.
  brain/vanilla/tiberiandawn/CMakeLists.txt:183 (EIGHTPLAYERS ON)
  brain/vanilla/tiberiandawn/CMakeLists.txt:221 (MEGAMAPS unconditional)

FORMAT: [MAP] Version=1  -- the "MEGA" sparse format.
  MAP_VERSION_MEGA == 1                        defines.h:290
  MAP_CELL_MAX_X_BITS == 7 under MEGAMAPS      defines.h:257
    => MAP_CELL_W = 128, MAP_CELL_TOTAL = 16384
  Version is clamped Bound(...,0,1)            display.cpp:1361
    => Version=2 (the XL format) is NOT loadable by this brain; it would be
       silently misread as Version=1.  128 is therefore the ceiling.

CELL ARITHMETIC: on a Version=1 map every cell number in the INI *and* in the
.BIN is  cell = y*128 + x.  The stride is a property of the FORMAT, never of
[MAP] Width.  Waypoints are used raw when Version==1 and are only put through
Confine_Old_Cell() when Version==0 (display.cpp:1401-1405); the same
Version-conditional conversion guards [OVERLAY] (overlay.cpp:349-356).
"""

import os
import struct
import sys

# ---------------------------------------------------------------- constants

STRIDE = 128          # MAP_CELL_W for a Version=1 map
CELL_TOTAL = 16384    # MAP_CELL_TOTAL

# Playable rect.  The engine stores no bound of its own (MapClass::Set_Map_Dimensions,
# map.cpp:554, just assigns), but the editor's rule is that the rect stays one cell
# inside the array on every side (edit_mod.h:2702-2724), so 126 is the widest legal
# rect and 120 at X=Y=4 is the editor's own "BIG MAP" preset.
RECT_X, RECT_Y, RECT_W, RECT_H = 4, 4, 120, 120

TEMPLATE_WATER = 1    # defines.h:1114; "W1", 1x1, legal in TEMPERATE (cdata.cpp:486-494)
TEMPLATE_CLEAR1 = 0
TEMPLATE_NONE = 255

SCEN = "SCM16EA"
# Writes to game/authored/ by default, which is where this repository keeps maps that are
# OURS rather than the 1995 disc's, beside SCG01EA and SCM91EA. Pass a directory to write
# somewhere else (a scratch dir, or straight into a play folder to try it).
#   python3 tools/maps/make-map16.py [outdir]
_HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = (sys.argv[1] if len(sys.argv) > 1
          else os.path.join(_HERE, "..", "..", "game", "authored"))


def cell(x, y):
    """Cell number for a Version=1 (stride-128) scenario."""
    assert 0 <= x < STRIDE and 0 <= y < STRIDE, (x, y)
    c = y * STRIDE + x
    assert 0 <= c < CELL_TOTAL
    return c


# ------------------------------------------------------------ start points
#
# A 4x4 lattice inside the rect.  Columns and rows at 19/49/79/109 put every
# start 30 cells from its neighbours and >= 15 cells from the rect edge.
# 120*120 / 16 = 900 cells per player; the retail 8-player SCM01EA is 58x49
# = 2842 cells for 8, i.e. ~355 each.  Sixteen players here have 2.5x the
# elbow room each that eight players get on the shipping map.

LANES = [19, 49, 79, 109]
STARTS = [(x, y) for y in LANES for x in LANES]   # row-major: 0..3 top, 12..15 bottom
assert len(STARTS) == 16

# WAYPT_HOME == 26, WAYPT_REINF == 27, WAYPT_COUNT == 28  (defines.h:2578-2586).
# NOT 25 -- waypoint 25 is inside the i<26 start-collection loop
# (scenarioini.cpp:1533) and doubles as the LZ-smoke anchor (trigger.cpp:468).
WAYPT_HOME = 26
WAYPT_REINF = 27
HOME_XY = (64, 64)          # dead centre, so the opening camera looks at the middle


# ------------------------------------------------------------------- terrain
def build_tiles():
    """cell -> (TemplateType, TIcon).  Only non-clear cells are recorded."""
    tiles = {}
    # A two-cell water frame hugging the playable rect. Decorative and, more
    # usefully, an unmistakable landmark for proving the stride is right: if the
    # cell arithmetic were wrong the frame would come back sheared.
    x0, y0 = RECT_X, RECT_Y
    x1, y1 = RECT_X + RECT_W - 1, RECT_Y + RECT_H - 1
    for i in range(x0, x1 + 1):
        for d in (y0, y0 + 1, y1 - 1, y1):
            tiles[cell(i, d)] = (TEMPLATE_WATER, 0)
    for j in range(y0, y1 + 1):
        for d in (x0, x0 + 1, x1 - 1, x1):
            tiles[cell(d, j)] = (TEMPLATE_WATER, 0)
    return tiles


def build_tiberium():
    """cell -> overlay name.  One field per start, offset so it is not under the MCV."""
    field = {}
    # A radius-2 diamond, 13 cells, centred 7 cells south-east of each start.
    diamond = [(dx, dy) for dy in range(-2, 3) for dx in range(-2, 3)
               if abs(dx) + abs(dy) <= 2]
    for idx, (sx, sy) in enumerate(STARTS):
        cx, cy = sx + 7, sy + 7
        for n, (dx, dy) in enumerate(diamond):
            # TI1..TI12 are the twelve tiberium overlays (odata.cpp:162-339).
            field[cell(cx + dx, cy + dy)] = "TI%d" % (1 + ((idx + n) % 12))
    # OverlayClass::Read_INI refuses cell < MAP_CELL_W or > MAP_CELL_TOTAL-MAP_CELL_W
    # (overlay.cpp:367). Assert we never wrote one it would drop.
    for c in field:
        assert STRIDE <= c <= CELL_TOTAL - STRIDE, c
    return field


# ----------------------------------------------------------------- writers
def write_bin(path, tiles):
    """
    Version=1 sparse .BIN.  No header, no count, no magic.
    Record, from MapClass::Read_Binary_Big (map.cpp:997-1002):
        u16 LE cell   (stride 128)
        u8     TemplateType
        u8     TIcon
    struct { CELL Cell; TemplateType TType; unsigned char TIcon; } is 4 bytes
    on this brain because CELL is signed short (defines.h:1695) and
    TemplateType is enum:unsigned char (defines.h:1111).
    Ascending cell order; clear cells omitted (the reader pre-fills every cell
    with 255, map.cpp:988-992).
    """
    with open(path, "wb") as f:
        n = 0
        for c in sorted(tiles):
            t, icon = tiles[c]
            if t in (TEMPLATE_NONE, TEMPLATE_CLEAR1):
                continue
            f.write(struct.pack("<HBB", c, t, icon))
            n += 1
    return n


def write_ini(path, tiberium):
    L = []
    A = L.append
    A("; %s -- SIXTEEN start positions." % SCEN)
    A("; [MAP] Version=1, so EVERY cell number in this file is y*128 + x.")
    A("; Playable rect X=%d Y=%d %dx%d." % (RECT_X, RECT_Y, RECT_W, RECT_H))
    A("")
    A("[Basic]")
    A("Name=SIXTEEN")
    A("Player=Multi1")
    A("CarryOverMoney=0")
    A("CarryOverCap=-1")
    A("BuildLevel=7")
    A("Theme=No theme")
    A("Intro=x")
    A("Brief=x")
    A("Action=x")
    A("Win=x")
    A("Lose=x")
    A("Percent=0")
    A("CNC3DKind=Multi")
    A("")
    # Version FIRST: everything downstream is sized and numbered off it.
    A("[MAP]")
    A("Version=1")
    A("Theater=TEMPERATE")      # Theater lives in [MAP], not [Basic] (display.cpp:1348)
    A("CNC3DTheater=TEMPERATE")
    A("X=%d" % RECT_X)
    A("Y=%d" % RECT_Y)
    A("Width=%d" % RECT_W)
    A("Height=%d" % RECT_H)
    A("")

    # HouseClass::Read_INI (house.cpp:1967) walks HOUSE_FIRST..HOUSE_COUNT and
    # new's every house whether or not a section exists, so these are documentation
    # as much as data.  HOUSE_COUNT is 12 on this brain: GoodGuy, BadGuy, Neutral,
    # Special, Multi1..Multi8 (defines.h:660-677).
    for name in ("GoodGuy", "BadGuy", "Neutral", "Special"):
        A("[%s]" % name)
        A("Credits=0")
        A("MaxBuilding=150")
        A("MaxUnit=150")
        A("Edge=North")
        A("")

    # Sixteen player sections.  Multi1..Multi8 are real houses today; Multi9..Multi16
    # have no HousesType and no IniName on this brain, so HouseClass::Read_INI never
    # looks them up and CCINIClass simply carries them as unreferenced sections.
    # They are written now so the file is complete for the 64-house XL fork.
    edges = ["North", "North", "North", "North",
             "West", "East", "West", "East",
             "West", "East", "West", "East",
             "South", "South", "South", "South"]
    for i in range(16):
        A("[Multi%d]" % (i + 1))
        A("Credits=0")
        A("MaxBuilding=150")
        A("MaxUnit=150")
        A("Edge=%s" % edges[i])
        if i >= 8:
            A("; inert on brain/vanilla: HOUSE_MULTI8 is the last house (defines.h:673)")
        A("")

    A("[Base]")
    A("Count=0")
    A("")

    # Waypoints, written descending the way the retail files and the editor's own
    # writer do.  Reader: display.cpp:1395, for (i = 0; i < WAYPT_COUNT; i++).
    A("[Waypoints]")
    A("%d=%d" % (WAYPT_REINF, cell(*HOME_XY)))
    A("%d=%d" % (WAYPT_HOME, cell(*HOME_XY)))
    for i in range(25, 15, -1):
        A("%d=-1" % i)
    # 15 down to 0.  These MUST be contiguous from 0: Create_Units compacts the
    # valid ones (scenarioini.cpp:1533), so a hole shifts every later start down.
    for i in range(15, -1, -1):
        x, y = STARTS[i]
        A("%d=%d" % (i, cell(x, y)))
    A("")

    A("[TERRAIN]")
    A("")
    A("[OVERLAY]")
    for c in sorted(tiberium):
        A("%d=%s" % (c, tiberium[c]))
    A("")
    for sec in ("SMUDGE", "STRUCTURES", "UNITS", "INFANTRY", "SHIPS", "AIRCRAFT"):
        A("[%s]" % sec)
        A("")
    A("[CellTriggers]")
    A("")
    A("[Triggers]")
    A("")
    A("[TeamTypes]")
    A("")

    with open(path, "w", newline="\r\n") as f:
        f.write("\n".join(L) + "\n")


def main():
    tiles = build_tiles()
    tib = build_tiberium()

    ini_path = os.path.join(OUTDIR, SCEN + ".INI")
    bin_path = os.path.join(OUTDIR, SCEN + ".BIN")
    write_ini(ini_path, tib)
    nrec = write_bin(bin_path, tiles)

    print("wrote %s (%d bytes)" % (ini_path, os.path.getsize(ini_path)))
    print("wrote %s (%d bytes, %d records, 4 bytes each)"
          % (bin_path, os.path.getsize(bin_path), nrec))
    assert os.path.getsize(bin_path) % 4 == 0
    print("\nstart waypoints (Version=1, cell = y*128 + x):")
    for i, (x, y) in enumerate(STARTS):
        print("  %2d  x=%3d y=%3d  cell=%5d" % (i, x, y, cell(x, y)))
    print("  %2d  x=%3d y=%3d  cell=%5d   (WAYPT_HOME)" % (WAYPT_HOME, HOME_XY[0], HOME_XY[1], cell(*HOME_XY)))
    print("  %2d  x=%3d y=%3d  cell=%5d   (WAYPT_REINF)" % (WAYPT_REINF, HOME_XY[0], HOME_XY[1], cell(*HOME_XY)))
    print("\ntiberium cells: %d" % len(tib))


if __name__ == "__main__":
    main()
