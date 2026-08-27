#!/usr/bin/env python3
"""
CNC3D -- SCM90EA, the skirmish slice map: two start positions on real cartridge
terrain, and a Construction Yard pad at each one that is actually clear.

WHY THIS EXISTS
  Skirmish needs a map before it needs anything else, and the cartridge has none:
  the N64 port cut multiplayer, so there is no SCM*.MAP in the ROM and therefore no
  SCM*.pack. The nine retail 1995 skirmish maps can be converted, but that is a
  three-tool pipeline and it produces flat ground, because the cartridge has no
  heightmap for a scenario it never shipped.

  This map takes the cheap route instead, the same one SCG90EA already takes: it
  authors a skirmish INI over an existing CAMPAIGN scenario's terrain .BIN and
  borrows that scenario's pack with --pack. The terrain, the elevation and the
  colour map are then genuine cartridge data with no new tooling at all, and the
  engine's collision (from the .BIN) and the renderer's geometry (from the pack)
  agree everywhere, because the cartridge .MAP was itself derived from that .BIN.

THE NAME
  The engine does not parse scenario names: CNC_Start_Custom_Instance only does
  sprintf("%s%s.INI", directory_path, scenario_name), so any filename would load.
  The name still has to look like a retail one, because other things read it:
    - the sidebar art is chosen by `scen[2] == 'B'`, so character 3 decides the
      Nod vs GDI bezel;
    - the pack is looked up as <scen>.pack unless --pack overrides it (we override).
  SCM90EA keeps the retail SC<house><nn><lang><variant> shape. 'M' is the prefix
  the engine itself uses for a multiplayer scenario (Set_Scenario_Name takes it
  from HouseTypeClass::As_Reference(HOUSE_MULTI1).Prefix), and 90 is far outside
  the retail 01..09 skirmish range, so it can never collide with a real map.

THE SOURCE SCENARIO, AND THE ONE TRAP IN BORROWING IT
  SOURCE below names the campaign scenario whose .BIN is copied byte for byte and
  whose pack is borrowed. Two values must then agree with it and nothing in the
  engine or the bakery cross-checks them:
    - Theater. It lives in the [MAP] section, NOT in [Basic]. Put it in the wrong
      section and the engine silently falls back to the default theater while the
      borrowed pack still renders the source's, and the map loads looking right
      and behaving wrong.
    - The pack passed with --pack. A pack from a different scenario would draw
      terrain the .BIN does not have, so a cliff the player can see would not stop
      a tank and a cliff that stops a tank would not be visible.

  The source is chosen for terrain, not sentiment: it needs two generous, clearly
  separated, connected plateaus, and it needs a cartridge heightmap so the ground
  is not dead flat. Only 55 of the 79 campaign scenarios ship a heightmap; the
  other 24 already bake with the flat fallback, so a source without one would cost
  the map its elevation for nothing.

WHERE THE STARTING FORCES COME FROM (they are NOT in this file)
  [UNITS], [INFANTRY] and [STRUCTURES] are emitted EMPTY, and [Base] holds only
  Count=0. That is not an oversight and it is not a shortcut: in a multiplayer
  game the engine builds both sides itself in Create_Units (scenarioini.cpp),
  which gives every house an MCV plus an escort scaled by the host's unit-count
  option, at the waypoint named by that player's start location. Anything this
  file listed in those sections would be EXTRA, owned by whichever house the INI
  named, and would sit on the map alongside the engine's own forces.

  Checked, not assumed: all nine retail skirmish INIs in the 1995 GENERAL.MIX have
  those three sections either absent or empty, and all nine have [Base] Count=0.

THE WAYPOINTS, AND THE UNCHECKED INDEX
  Create_Units compacts waypoints 0..25 into a 26-entry array, keeping only the
  ones that are not -1, and then indexes that array with the player's start
  location WITH NO BOUND CHECK. So the legal start indices are 0..(number of valid
  waypoints below 26) - 1, which is a per-map number, and a start index past the
  end reads off the end of the array. Four of the nine retail maps have gaps in
  0..7 for exactly this reason and are wrong for any menu that offers "start
  position 1..8".

  This map removes the ambiguity: waypoints 0 and 1 are valid and every other
  waypoint below 26 is -1, so the two legal start indices are 0 and 1 and there
  is no third value that silently half-works. Waypoint 26 is the home cell, which
  is where the engine points the opening view; it is deliberately outside the
  0..25 compaction, so setting it does not shift the start indices.

  Waypoint 27, the reinforcement cell, is left at -1. Retail skirmish maps set it,
  but it is only ever read to walk a reinforcement team onto the map and a
  skirmish has no team types, so a value there would be decoration.

THE CONSTRUCTION YARD PAD, WHICH IS THE POINT OF THE WHOLE FILE
  The first thing anybody does in a skirmish is deploy the MCV, and on most
  existing start positions that click does nothing at all. The engine downgrades
  the deploy action unless a Construction Yard would legally fit at the cell NORTH
  WEST of the MCV, and that footprint is bigger than it looks:

      the structure occupies 3 wide by 2 tall (bdata.cpp List32), it requires a
      bib, a 3-wide building takes the 3 by 2 BIB2 smudge, and the bib is offset
      one row down. Union: a 3 by 3 block whose top-left corner is the cell NW of
      the MCV, which is to say a 3 by 3 block CENTRED ON THE MCV ITSELF.

  Every one of those nine cells must be clear buildable ground. The MCV does not
  block its own pad (the engine lifts it off the map for the test), but a single
  escort unit standing in any of the other eight cells is enough to kill the
  click, and the engine's only complaint is a voice line.

  So both start cells below are chosen for a large open buildable pocket, not
  merely a 3 by 3 one, and the checks at the bottom of this file refuse to write
  the map unless both pads are clear and both pockets are at least PAD_MARGIN
  cells of clear ground in every direction.

  Terrain is the half of this problem a map can fix. The other half is the escort,
  and it is NOT a map problem: the engine scatters the starting units within about
  four cells of the MCV and can drop one inside the pad on any terrain at all.

  MEASURED on this map, driving the real deploy click through the engine (select
  the MCV, command at its own cell, advance, count Construction Yards):

      host unit count :  0   1   2   3   4   5   6   7   8   9  10  12
      first click     : yes  no yes yes yes yes  no  no  no  no  no  no

  Both start positions give the SAME answer at every one of those counts, with the
  blocking unit at mirrored offsets, which is the useful part: the two starts are
  interchangeable, and the variable is the host's unit-count option rather than
  where on this map a player begins. At the counts that fail, the pad is blocked by
  the player's own escort standing one cell from the MCV; at the counts that pass,
  the pad is empty and the first click builds the Construction Yard.

  So a map can guarantee the ground and cannot guarantee the click. Anything that
  wants the opening click to work has to either pick a unit count from the yes row
  or move the escort off the pad before handing control to the player.

TIBERIUM
  A skirmish with no tiberium has no economy, so the fields are authored here
  rather than inherited. The engine filters what it is given: tiberium is marked
  down only on a cell that is bare, has no terrain object and is buildable ground,
  so each rectangle below is offered whole and the engine keeps the legal cells.
  This file applies the same rule first, so the count it prints is the count the
  engine will accept.

  Placement is balanced on purpose, because this map is what the feature is gated
  against and a lopsided economy would show up as an AI-strength bug: each start
  has one field close by, and the other two fields are equidistant from both.
  Fields are kept several cells clear of both start pads, because a tiberium cell
  is passable but NOT buildable and one growing into a pad would break the very
  thing this map exists to guarantee.

THE SOURCE SCENARIO'S OWN OVERLAY IS DROPPED
  Campaign scenarios carry walls and crates in [OVERLAY]. Those belong to a base
  that does not exist here, so they are not copied: a wall is impassable and
  unbuildable, and a line of them left over from a deleted base would shape the
  match for no stated reason. [TERRAIN] IS copied, because trees and rocks are
  scenery that reads as part of the ground the pack draws.

CELL ARITHMETIC
  INI cells are y*64+x, the old 64-wide INI space, which is what a .BIN is indexed
  by too. The engine reports y*128+x internally under MEGAMAPS. x and y are the
  same in both.

Run:  python3 make_skirmish_map.py [--out DIR]    # writes SCM90EA.INI + SCM90EA.BIN
"""
import math
import os
import re
import shutil
import sys
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
SCEN = "SCM90EA"

# ------------------------------------------------------------------ the source --
# The campaign scenario whose terrain .BIN is copied and whose pack is borrowed.
# Its heightmap and colour map exist in the cartridge, so the ground has real
# elevation rather than the flat fallback.
SOURCE = "SCB09EA"
THEATER = "DESERT"          # MUST match the source scenario. Lives in [MAP].
MAP_NAME = "CANYON RUN"

# ------------------------------------------------------------------ map window --
# The source declares X=1 Y=3 W=61 H=59. The southern six rows of that are one
# solid impassable rock shelf, so the window is trimmed to the ground that can
# actually be played on. Trimming is safe in a way that widening is not: the .BIN
# and the pack both cover all 4096 cells, so a narrower window only hides ground,
# where a wider one would expose cells the source never authored.
MAP_X, MAP_Y, MAP_W, MAP_H = 1, 3, 61, 53      # cols 1..61, rows 3..55

# --------------------------------------------------------------- start positions --
# Waypoint 0 and waypoint 1, the only two valid start locations on this map.
# Start 0 sits on the south-west plain, start 1 on the north-east plateau. They
# are on opposite diagonals with the whole map between them.
START0 = (18, 48)
START1 = (54, 10)

# Waypoint 26, the home cell: where the engine points the opening view. Put on the
# midpoint of the two starts so the default view is the ground between the bases
# rather than an arbitrary map centre.
HOME = (36, 28)

# How much clear buildable ground each start must have in every direction. The pad
# itself needs 1; this asks for a real pocket, so the player has somewhere to put a
# power plant and a refinery without walking the Construction Yard across the map.
PAD_MARGIN = 5

# Minimum separation between the two starts, in cells.
MIN_SEPARATION = 30

# ------------------------------------------------------------------- tiberium ----
# (name, x0, y0, x1, y1) inclusive rectangles, offered whole to the engine.
# F0 and F1 are the home fields, one per start, at matched distance. F2 and F3 are
# equidistant from both starts, so contesting them is a decision rather than a
# geography lesson.
TIBERIUM_FIELDS = [
    ("F0 home, start 0", 24, 42, 34, 49),
    ("F1 home, start 1", 41, 16, 51, 23),
    ("F2 contested, north-west", 7, 3, 17, 10),
    ("F3 contested, south-east", 49, 41, 59, 48),
]

# No tiberium may sit this close to a start cell. The pad is 3x3, so 3 leaves a
# clear ring around it as well.
TIBERIUM_KEEPOUT = 3

# Each start must have a field no further away than this, or its economy is a walk.
TIBERIUM_MAX_HOME_DISTANCE = 12

# The twelve tiberium overlay names. Retail skirmish maps mix all twelve across a
# field rather than using one; the mix below is a fixed function of the cell, so
# the same rectangle always produces the same file.
TIBERIUM_NAMES = ["TI%d" % (n + 1) for n in range(12)]

# House starting money comes from the host's multiplayer options, not from here;
# retail skirmish maps all carry Credits=0 for exactly that reason.
HOUSE_CREDITS = 0
HOUSE_MAX_BUILDING = 150
HOUSE_MAX_UNIT = 150


# The INI cell stride: the grid width of the scenario's .BIN. 64 for every map
# today (the old 64-wide INI space); read_bin() re-derives it from the .BIN's
# own length, so a 128x128 source would set it to 128 without touching the rest.
GRID = 64


def cell(x, y):
    """INI cell number for a map coordinate (the .BIN-wide cell space)."""
    return y * GRID + x


# =================================================================== the engine ==
# Buildability is not guessed. The land type of every template, and which land
# types can be built on, are read out of the engine source in this repository, so
# this file cannot drift away from the rules the engine will actually apply.

VANILLA = os.path.normpath(os.path.join(HERE, "..", "..", "brain", "vanilla", "tiberiandawn"))

# const.cpp Ground[]: only these two land types have the build flag set, and only
# these four have a non-zero movement cost for a tracked unit.
LAND_BUILDABLE = {"LAND_CLEAR", "LAND_ROAD"}
LAND_PASSABLE = {"LAND_CLEAR", "LAND_ROAD", "LAND_TIBERIUM", "LAND_BEACH"}


def load_template_lands():
    """TemplateType index -> (land, alt land, set of icons that use the alt land).

    cdata.cpp declares one TemplateTypeClass per template and then lists them, in
    enum order, in TemplateTypeClass::Pointers[]. Each constructor carries the
    template's land type, plus an optional exception land type and the list of
    icon numbers inside the template that use it: a cliff template, for instance,
    is LAND_ROCK with a handful of walkable icons called out by number.
    """
    src = open(os.path.join(VANILLA, "cdata.cpp"), errors="replace").read()

    # The exception lists are plain arrays of icon numbers terminated by -1.
    alt_icons = {}
    for m in re.finditer(r"static char const (_\w+)\[\]\s*=\s*\{([^}]*)\}", src):
        nums = [int(v) for v in re.findall(r"-?\d+", m.group(2))]
        alt_icons[m.group(1)] = set(n for n in nums if n >= 0)

    ctors = {}
    for m in re.finditer(r"static TemplateTypeClass const\s+(\w+)\s*\(([^;]*?)\);", src, re.S):
        args = [a.strip() for a in re.split(r",(?![^(]*\))", m.group(2))]
        if len(args) < 9:
            continue
        #  0 type   1 theaters   2 ini name   3 full name   4 land
        #  5 width  6 height     7 alt land   8 alt icon list
        ref = re.search(r"(_\w+)", args[8])
        icons = alt_icons.get(ref.group(1), set()) if (ref and "NULL" not in args[8]) else set()
        ctors[m.group(1)] = (args[4], args[7], icons, args[2].strip('" '))

    head = src.index("TemplateTypeClass::Pointers[TEMPLATE_COUNT]")
    body = src[head:src.index("};", head)]
    return [ctors[name] for name in re.findall(r"&(\w+)\s*,", body + ",")]


def template_count():
    """TEMPLATE_COUNT, counted from the enum in defines.h."""
    src = open(os.path.join(VANILLA, "defines.h"), errors="replace").read()
    head = src.index("typedef enum TemplateType")
    return len(re.findall(r"TEMPLATE_\w+", src[head:src.index("TEMPLATE_COUNT", head)]))


def land_type(lands, template, icon):
    """The land type the engine will give a cell holding this (template, icon).

    0xFF is 'no template', which the engine treats exactly as clear terrain.
    """
    if template == 0xFF or template >= len(lands):
        return "LAND_CLEAR"
    land, alt_land, alt_icons, _name = lands[template]
    return alt_land if icon in alt_icons else land


# ======================================================================= source ==

def read_bin(path):
    """A .BIN is W*W cells of (template byte, icon byte) in W-wide row order.

    W is derived from the file's own length (2 bytes per cell, square grid):
    8192 bytes = 64x64 (legacy), 32768 = 128x128. Sets GRID as a side effect,
    so every later cell() computation uses the source's real stride."""
    global GRID
    data = open(path, "rb").read()
    n, rem = divmod(len(data), 2)
    w = math.isqrt(n)
    if rem or w * w != n:
        raise SystemExit("%s is %d bytes; a square .BIN is 2*W*W bytes "
                         "(8192 for 64x64, 32768 for 128x128)" % (path, len(data)))
    GRID = w
    return [[(data[(y * w + x) * 2], data[(y * w + x) * 2 + 1]) for x in range(w)]
            for y in range(w)]


def read_section(text, name):
    m = re.search(r"^\[" + name + r"\]\n(.*?)(?=^\[|\Z)", text, re.M | re.S)
    return m.group(1).strip().splitlines() if m else []


def read_terrain(text):
    """[TERRAIN] entries as {(x, y): "T08"}. These occupy their cell."""
    out = {}
    for line in read_section(text, "TERRAIN"):
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if not key.strip().isdigit():
            continue
        c = int(key)
        out[(c % GRID, c // GRID)] = value.split(",")[0].strip()
    return out


def build_grids(tiles, terrain, lands):
    """Per-cell buildable and passable flags, by the engine's own rules."""
    w = len(tiles[0])
    h = len(tiles)
    buildable = [[False] * w for _ in range(h)]
    passable = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            land = land_type(lands, *tiles[y][x])
            can_build = land in LAND_BUILDABLE
            can_walk = land in LAND_PASSABLE
            if (x, y) in terrain:
                can_build = False       # a tree or a rock occupies the cell
                can_walk = False
            buildable[y][x] = can_build
            passable[y][x] = can_walk
    return buildable, passable


# ======================================================================= checks ==

def in_window(x, y):
    return MAP_X <= x < MAP_X + MAP_W and MAP_Y <= y < MAP_Y + MAP_H


def pad_cells(x, y):
    """The nine cells a Construction Yard would claim if the MCV deployed here."""
    return [(i, j) for j in range(y - 1, y + 2) for i in range(x - 1, x + 2)]


def open_radius(buildable, x, y, cap=8):
    """Largest r for which the (2r+1)-square around (x, y) is buildable and in the window."""
    r = 0
    while r < cap:
        n = r + 1
        if not (in_window(x - n, y - n) and in_window(x + n, y + n)):
            break
        if not all(buildable[j][i]
                   for j in range(y - n, y + n + 1)
                   for i in range(x - n, x + n + 1)):
            break
        r = n
    return r


def route_length(passable, start, goal):
    """Steps along passable ground from start to goal, or None if there is no route."""
    seen = {start}
    queue = deque([(start, 0)])
    while queue:
        (x, y), steps = queue.popleft()
        if (x, y) == goal:
            return steps
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                n = (x + dx, y + dy)
                if in_window(*n) and n not in seen and passable[n[1]][n[0]]:
                    seen.add(n)
                    queue.append((n, steps + 1))
    return None


def chebyshev(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def tiberium_cells(buildable, terrain):
    """Every field rectangle, filtered by the rule the engine itself applies.

    Returns (cells in emit order, per-field report rows).
    """
    cells = []
    rows = []
    for name, x0, y0, x1, y1 in TIBERIUM_FIELDS:
        kept = [(x, y)
                for y in range(y0, y1 + 1)
                for x in range(x0, x1 + 1)
                if in_window(x, y) and buildable[y][x] and (x, y) not in terrain]
        offered = (x1 - x0 + 1) * (y1 - y0 + 1)
        rows.append((name, x0, y0, x1, y1, offered, len(kept),
                     min((chebyshev(c, START0) for c in kept), default=None),
                     min((chebyshev(c, START1) for c in kept), default=None)))
        cells.extend(kept)
    return cells, rows


def check(tiles, terrain, lands, tib_cells, tib_rows, buildable, passable):
    """Everything this map claims about itself, re-derived from the data it ships."""
    problems = []

    if len(lands) != template_count():
        problems.append("template land table has %d entries but the engine enum has %d; "
                        "the table parse is stale and no buildability claim below can "
                        "be trusted" % (len(lands), template_count()))

    for name, start in (("start 0", START0), ("start 1", START1)):
        if not in_window(*start):
            problems.append("%s at %s is outside the map window" % (name, start))
            continue
        blocked = [c for c in pad_cells(*start)
                   if not (in_window(*c) and buildable[c[1]][c[0]])]
        if blocked:
            problems.append("%s at %s: Construction Yard pad blocked at %s"
                            % (name, start, blocked))
        radius = open_radius(buildable, *start)
        if radius < PAD_MARGIN:
            problems.append("%s at %s has only %d cells of clear ground around it, "
                            "wanted %d" % (name, start, radius, PAD_MARGIN))

    separation = chebyshev(START0, START1)
    if separation < MIN_SEPARATION:
        problems.append("the two starts are only %d cells apart, wanted %d"
                        % (separation, MIN_SEPARATION))
    if route_length(passable, START0, START1) is None:
        problems.append("no passable ground route between the two starts: neither side "
                        "could ever reach the other")

    if not in_window(*HOME):
        problems.append("the home cell %s is outside the map window" % (HOME,))

    seen = set()
    for c in tib_cells:
        if c in seen:
            problems.append("tiberium cell %s is listed twice; the fields overlap" % (c,))
        seen.add(c)
    for name, start in (("start 0", START0), ("start 1", START1)):
        near = [c for c in tib_cells if chebyshev(c, start) <= TIBERIUM_KEEPOUT]
        if near:
            problems.append("%d tiberium cells sit within %d of %s, where they would "
                            "block the Construction Yard pad: %s"
                            % (len(near), TIBERIUM_KEEPOUT, name, near[:6]))
    for index, start in ((7, START0), (8, START1)):
        closest = min(row[index] for row in tib_rows if row[index] is not None)
        if closest > TIBERIUM_MAX_HOME_DISTANCE:
            problems.append("the nearest tiberium to %s is %d cells away, wanted %d or "
                            "less" % (start, closest, TIBERIUM_MAX_HOME_DISTANCE))
    if not tib_cells:
        problems.append("no tiberium survived the filter: the map has no economy")

    return problems


# ======================================================================== output ==

def section(name, lines):
    return "[%s]\n%s\n" % (name, "\n".join(lines) if lines else "")


def house_section(name):
    """A [MultiN] block, modelled on the retail skirmish maps.

    FlagHome and FlagLocation are capture-the-flag data; the engine overwrites both
    with 0 when it hands the house its MCV, so 0 is what they are worth here.
    Allies is read only in a single-player game, so it is inert in a skirmish and
    is written self-only rather than pretending to describe a team.
    """
    return section(name, [
        "FlagHome=0",
        "FlagLocation=0",
        "Allies=%s" % name,
        "MaxBuilding=%d" % HOUSE_MAX_BUILDING,
        "MaxUnit=%d" % HOUSE_MAX_UNIT,
        "Edge=North",
        "Credits=%d" % HOUSE_CREDITS,
    ])


def waypoint_lines():
    """Waypoints 31 down to 0, the order the retail files use.

    Only 0, 1 and 26 carry a cell. See the header: 0 and 1 are the two start
    locations and the fact that nothing else below 26 is set is what makes the
    legal start index range exactly 0..1.
    """
    points = {0: START0, 1: START1, 26: HOME}
    return ["%d=%d" % (i, cell(*points[i]) if i in points else -1)
            for i in range(31, -1, -1)]


def build(tiles, terrain, tib_cells):
    out = []
    out.append(section("Basic", [
        "CarryOverCap=-1",
        "CarryOverMoney=0",
        # Intro, Brief, Action, Win and Lose name movies and a briefing screen.
        # A skirmish reaches none of them, and this map has none of its own, so
        # all five are the engine's own "nothing here" value.
        "Intro=x",
        "Brief=x",
        "Action=x",
        "Win=x",
        "Lose=x",
        # Ignored on the multiplayer path, where the build level passed to
        # CNC_Start_Custom_Instance wins. It is read only when the same file is
        # started as a single-player custom map, which is how the renderer work
        # exercises a Multi1 scenario without any multiplayer code, so it is set
        # to the full build list rather than to a campaign mission number.
        "BuildLevel=98",
        "Theme=No theme",
        "Percent=0",
        # The player house. Every house in a skirmish is Multi1..Multi6, and this
        # line is also what lets the map be loaded as an ordinary scenario to test
        # how a Multi house is drawn.
        "Player=Multi1",
        "Name=%s" % MAP_NAME,
    ]))
    out.append(section("MAP", [
        "Height=%d" % MAP_H,
        "Width=%d" % MAP_W,
        "Y=%d" % MAP_Y,
        "X=%d" % MAP_X,
        "Theater=%s" % THEATER,          # [MAP], not [Basic]. See the header.
    ]))
    for n in range(1, 7):
        out.append(house_section("Multi%d" % n))

    # The base node list a campaign AI rebuilds from. A skirmish AI lays its own
    # base out instead, so this is empty by design and not by omission.
    out.append(section("Base", ["Count=0"]))

    # Scenery from the source scenario. These occupy their cells, and the checks
    # above already treat them as blocked, so the pads and the tiberium avoid them.
    out.append(section("TERRAIN", ["%d=%s,None" % (cell(x, y), name)
                                   for (x, y), name in sorted(terrain.items(),
                                                              key=lambda kv: cell(*kv[0]))]))
    out.append(section("OVERLAY", ["%d=%s" % (cell(x, y),
                                              TIBERIUM_NAMES[(x * 7 + y * 3) % 12])
                                   for (x, y) in tib_cells]))
    out.append(section("SMUDGE", []))

    # Empty on purpose: the engine creates both sides in Create_Units. See header.
    out.append(section("INFANTRY", []))
    out.append(section("UNITS", []))
    out.append(section("STRUCTURES", []))

    out.append(section("Waypoints", waypoint_lines()))

    # The engine creates these four houses in every scenario whether or not the
    # file mentions them, and none of them is ever started in a skirmish. The
    # retail skirmish maps carry them, so this one does too.
    for name in ("Neutral", "GoodGuy", "BadGuy", "Special"):
        out.append(house_section(name))
    return "\n".join(out)


def main():
    outdir = HERE
    if "--out" in sys.argv:
        outdir = sys.argv[sys.argv.index("--out") + 1]

    src_ini = os.path.join(HERE, SOURCE + ".INI")
    src_bin = os.path.join(HERE, SOURCE + ".BIN")
    for path in (src_ini, src_bin):
        if not os.path.exists(path):
            raise SystemExit("missing source scenario file: %s" % path)

    lands = load_template_lands()
    tiles = read_bin(src_bin)
    terrain = read_terrain(open(src_ini, errors="replace").read().replace("\r", ""))
    buildable, passable = build_grids(tiles, terrain, lands)
    tib_cells, tib_rows = tiberium_cells(buildable, terrain)

    problems = check(tiles, terrain, lands, tib_cells, tib_rows, buildable, passable)
    if problems:
        print("REFUSING TO WRITE %s: the map does not hold up its own claims" % SCEN)
        for p in problems:
            print("  - %s" % p)
        return 1

    ini = build(tiles, terrain, tib_cells)
    open(os.path.join(outdir, SCEN + ".INI"), "w").write(ini)
    shutil.copy(src_bin, os.path.join(outdir, SCEN + ".BIN"))

    print("wrote %s.INI + %s.BIN in %s" % (SCEN, SCEN, outdir))
    print("  terrain      %s.BIN copied unchanged; theater %s; borrow %s.pack with --pack"
          % (SOURCE, THEATER, SOURCE))
    print("  map window   X=%d Y=%d W=%d H=%d  (cols %d..%d, rows %d..%d)"
          % (MAP_X, MAP_Y, MAP_W, MAP_H,
             MAP_X, MAP_X + MAP_W - 1, MAP_Y, MAP_Y + MAP_H - 1))
    print("  start 0      %s  pad clear, %d cells of clear ground around it"
          % (START0, open_radius(buildable, *START0)))
    print("  start 1      %s  pad clear, %d cells of clear ground around it"
          % (START1, open_radius(buildable, *START1)))
    print("  separation   %d cells apart, %d steps of passable ground between them"
          % (chebyshev(START0, START1), route_length(passable, START0, START1)))
    print("  home cell    %s  (waypoint 26, where the opening view is aimed)" % (HOME,))
    print("  waypoints    0 and 1 valid, everything else below 26 is -1, so the only "
          "legal start indices are 0 and 1")
    print("  forces       [UNITS] [INFANTRY] [STRUCTURES] empty, [Base] Count=0; the "
          "engine creates both sides")
    for name, x0, y0, x1, y1, offered, kept, d0, d1 in tib_rows:
        print("  tiberium     %-26s x%d..%d y%d..%d  %d of %d cells legal, "
              "nearest to start 0 %d, to start 1 %d"
              % (name, x0, x1, y0, y1, kept, offered, d0, d1))
    print("  tiberium     %d cells total" % len(tib_cells))
    return 0


if __name__ == "__main__":
    sys.exit(main())
