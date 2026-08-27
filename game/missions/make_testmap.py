#!/usr/bin/env python3
"""
CNC3D -- SCG90EA, "the test map": a variation of GDI 1 built to exercise everything
the renderer does in about ten minutes of play.

WHY THIS EXISTS
  SCG01EB proved building production, SCG01EC added unit production and MCV deploy.
  Neither has a refinery, a harvester, tiberium, an enemy base, or attack waves, so
  nothing in the shipped mission set makes combat happen on its own. This map does.

THE NAME
  The engine does not parse scenario names: CNC_Start_Custom_Instance only does
  sprintf("%s%s.INI", directory_path, scenario_name), so any filename would load.
  Two things DO read the name, so it still has to look like a retail one:
    - cnc_eyes.cpp: `hk.nod = (scen[2] == 'B')` picks the Nod vs GDI sidebar art,
      so character 3 must be 'G' for a GDI mission.
    - the pack is looked up as <scen>.pack unless --pack overrides it (we override).
  SCG90EA therefore keeps the retail SC<house><nn><lang><variant> shape, and 90 is
  far outside the retail 01..15 range so it can never collide with a real mission.

THE MAP WINDOW  (this is the one real difference from SCG01EA's terrain)
  SCG01EA declares X=36 Y=39 W=26 H=23, i.e. a window on rows 39..61 of the 64x64
  world. Its own INI puts a whole northern half OUTSIDE that window: a tiberium
  field at x40..45 y31..34, sandbag walls along y36..37, and a dozen trees. The N64
  cartridge map baked into SCG01EA.pack covers all 4096 cells, so that northern half
  is real, drawable ground that the retail scenario simply never shows.
  SCG90EA opens the window to X=36 Y=30 W=26 H=32 (rows 30..61) and uses the north
  for the Nod base. The terrain .BIN is copied from SCG01EA unchanged.

THE LAYOUT IS CHECKED, NOT GUESSED
  Every structure origin, unit cell and waypoint below was validated against
  placemap_clean.txt -- a GAME_STATE_PLACEMENT dump taken from a probe scenario that
  held exactly one building, so the clear/blocked verdict for every other cell is the
  engine's own. checklayout.py replays that check (footprints from bdata.cpp:
  FACT 3x2, PROC 3x3, WEAP 3x3, NUKE 2x2, PYLE 2x2, HAND 2x3, AFLD 4x2, all bibbed;
  GTWR/GUN 1x1, SAM 2x1 unbibbed) and reports zero conflicts.

CELL ARITHMETIC
  INI cells are y*64+x (the old 64-wide INI space). The engine reports y*128+x under
  MEGAMAPS. x and y are identical in both.

Run:  python3 make_testmap.py [--out DIR]     # writes SCG90EA.INI + SCG90EA.BIN
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCEN = "SCG90EA"

# ---------------------------------------------------------------- map window ----
MAP_X, MAP_Y, MAP_W, MAP_H = 36, 30, 26, 32          # world rows 30..61, cols 36..61
TACTICAL = (48, 44)                                   # top-left of the opening view

# ------------------------------------------------------------------ GDI base ----
# (name, x, y). Origin is the top-left cell; bibbed buildings also claim the row
# below, which the layout check accounts for.
GDI_STRUCTURES = [
    ("FACT", 53, 51),      # construction yard  -- the whole build tree hangs off it
    ("NUKE", 50, 51),      # power plant
    ("NUKE", 59, 47),      # second plant: +200 against a 115 drain, factories at speed
    ("PROC", 50, 46),      # refinery, so the harvester has somewhere to unload
    ("WEAP", 53, 46),      # weapons factory -> the sidebar's right column fills
    ("PYLE", 57, 53),      # barracks        -> infantry production
    ("GTWR", 50, 44),      # four guard towers: the northern approach is the main
    ("GTWR", 55, 44),      # lane, plus one on each flank. They are the first thing
    ("GTWR", 47, 45),      # the waves chew through, which is the map telling the
    ("GTWR", 58, 45),      # player to start building.
]

# (type, x, y, facing, mission)
GDI_UNITS = [
    ("MCV",  60, 51, 0, "Guard"),   # deploy origin is (59,50); that 3x2+bib is clear
    ("HARV", 49, 45, 0, "Harvest"), # two cells from the refinery
    ("HARV", 52, 45, 0, "Harvest"), # a second one, so one death does not end the economy
    ("MTNK", 57, 50, 0, "Guard"),
    ("MTNK", 58, 52, 0, "Guard"),
    ("MTNK", 56, 50, 0, "Guard"),
    ("MTNK", 57, 52, 0, "Guard"),
    ("JEEP", 59, 53, 0, "Guard"),
    ("BOAT", 53, 59, 192, "Guard"), # the shoreline is inside the window; gives the
                                    # water, the wake and a turreted vessel to draw
]

# (type, x, y, subcell, facing, mission)
GDI_INFANTRY = [
    ("E1", 48, 50, 0, 0,   "Guard"),
    ("E1", 48, 50, 1, 0,   "Guard"),
    ("E1", 49, 50, 2, 0,   "Guard"),
    ("E1", 49, 50, 3, 0,   "Guard"),
    ("E2", 54, 50, 0, 0,   "Guard"),
    ("E2", 54, 50, 1, 0,   "Guard"),
]

# --------------------------------------------------------------- tiberium -------
# A rectangle, not a hand-picked cell list, on purpose: OverlayClass::Mark refuses to
# mark any cell that is not bare buildable ground, so the engine itself filters the
# rectangle down to the legal cells (50 of the 60 asked for). SCG01EA's own northern
# field at x40..45 y31..34 is left alone and is now inside the window too, so the map
# has a second, thoroughly contested patch right next to the Nod base.
#
# WHERE IT SITS was measured, not chosen. First cut x46..56 y39..43 lay straight across
# the lane the attack teams walk down and the harvester was dead by tick 2400 in every
# run. Second cut x52..61 y38..43 moved it east but its north edge reached y38, which is
# where the Nod garrison stands, and the harvester was down to 94/600 hit points by tick
# 750. The shipped rectangle is the southern half of that: it starts three cells in front
# of the guard-tower line at y44 and stops six cells short of the Nod garrison. Cells
# taken by a structure footprint or bib are skipped, because overlay is read before
# structures and a tiberium cell would otherwise refuse the building on top of it.
TIB_X0, TIB_Y0, TIB_X1, TIB_Y1 = 52, 41, 61, 44

# ------------------------------------------------------------------ Nod base ----
NOD_STRUCTURES = [
    ("FACT", 57, 33),      # Nod construction yard
    ("HAND", 50, 32),      # hand of nod   -> infantry production
    ("AFLD", 53, 34),      # airstrip      -> vehicle production
    ("NUKE", 48, 34),      # power
    ("GUN",  50, 36),      # turrets covering the southern approach
    ("GUN",  57, 36),
    ("SAM",  51, 36),
]

# The standing garrison. These are also the recruiting pool the "Create Team"
# trigger draws its first wave from, so they sit on Guard rather than Area Guard.
NOD_INFANTRY = [
    ("E1", 49, 38, 0, 128, "Guard"),
    ("E1", 49, 38, 1, 128, "Guard"),
    ("E1", 51, 38, 2, 128, "Guard"),
    ("E1", 51, 38, 3, 128, "Guard"),
    ("E3", 52, 38, 0, 128, "Guard"),
    ("E3", 52, 38, 1, 128, "Guard"),
    ("E1", 50, 40, 0, 128, "Area Guard"),
    ("E1", 52, 40, 0, 128, "Area Guard"),
]
NOD_UNITS = [
    ("BGGY", 54, 38, 128, "Guard"),
    ("BGGY", 55, 38, 128, "Guard"),
    ("LTNK", 56, 38, 128, "Guard"),
    ("LTNK", 57, 38, 128, "Guard"),
]

# ----------------------------------------------------------------- waypoints ----
# 26 is WAYPT_HOME and 27 is WAYPT_REINF (defines.h). 1 and 3 are the staging cells
# the attack teams walk to before they pick a target.
WAYPOINTS = {
    0: (54, 51),    # GDI base centre
    1: (49, 42),    # main staging cell: north of the tower line, west of tiberium
    2: (47, 46),    # western flank, straight at the refinery (ATKC uses this)
    3: (58, 48),    # eastern approach (unused by the shipped teams; left for hand edits)
    4: (53, 34),    # Nod base centre
    26: (54, 51),
    27: (57, 55),
}

# ---------------------------------------------------------------- attack waves --
# TeamTypeClass::Fill_In field order (teamtype.cpp):
#   House,Roundabout,Learning,Suicide,Autocreate,Mercenary,RecruitPriority,
#   MaxAllowed,InitNum,Fear,ClassCount,Class:Num...,MissionCount,Mission:Arg...,
#   IsReinforcable,IsPrebuilt
# A mission argument on Move is a waypoint index; on Attack* it is the timeout in
# tenths of a minute (TeamClass::AI: TimeOut = Argument * TICKS_PER_MINUTE/10).
TEAMTYPES = [
    ("ATKA", "BadGuy,0,0,0,1,0,20,4,0,0,2,E1:3,E3:1,2,Move:1,Attack Base:40,0,0"),
    ("ATKB", "BadGuy,0,0,0,1,0,20,3,0,0,2,BGGY:2,LTNK:1,2,Move:1,Attack Units:40,0,0"),
    ("ATKC", "BadGuy,0,0,0,1,0,20,2,0,0,2,LTNK:2,E1:2,2,Move:2,Attack Base:60,0,0"),
    ("ATKD", "BadGuy,0,0,0,0,0,20,2,0,0,1,E1:3,2,Move:1,Attack Base:40,0,0"),
]

# TriggerClass::Fill_In field order (trigger.cpp):
#   Event,Action,Data,House,Team,IsPersistant     (0 volatile, 1 semi, 2 persistent)
# HouseClass::AI checks triggers every TICKS_PER_MINUTE/10 == 90 ticks and a Time
# trigger decrements Data on each check, so Data is in units of 90 ticks (6 seconds
# at the engine's 15 ticks per second).
TRIGGERS = [
    ("LOSE", "All Destr.,Lose,0,GoodGuy,None,0"),
    ("WIN",  "All Destr.,Win,0,BadGuy,None,0"),
    ("PROD", "Time,Production,8,BadGuy,None,0"),      # t=720: Nod starts building
    ("AUTO", "Time,Autocreate,3,BadGuy,None,0"),      # t=270: Nod may form its own teams
    ("RUSH", "Time,Create Team,1,BadGuy,ATKD,0"),     # t=90:  the garrison moves out
    ("WAVA", "Time,Reinforce.,12,BadGuy,ATKA,2"),     # t=1080 (72s), then every 1080
    ("WAVB", "Time,Reinforce.,20,BadGuy,ATKB,2"),     # t=1800 (120s), then every 1800
    ("WAVC", "Time,Reinforce.,30,BadGuy,ATKC,2"),     # t=2700 (180s), then every 2700
]

GDI_CREDITS = 15000      # INI credits are hundreds: 150 -> 15000
NOD_CREDITS = 3000      # enough that ACTION_BEGIN_PRODUCTION is not a no-op


def cell(x, y):
    return y * 64 + x


# Footprint sizes come from bdata.cpp; the bib is the row directly below.
BSIZE = {"FACT": (3, 2, True), "PROC": (3, 3, True), "WEAP": (3, 3, True),
         "NUKE": (2, 2, True), "PYLE": (2, 2, True), "HAND": (2, 3, True),
         "AFLD": (4, 2, True), "GTWR": (1, 1, False), "GUN": (1, 1, False),
         "SAM": (2, 1, False)}


def footprint_cells():
    """Every cell a structure (or its bib) will want, so tiberium keeps off them."""
    out = set()
    for (name, x, y) in GDI_STRUCTURES + NOD_STRUCTURES:
        w, h, bib = BSIZE[name]
        for j in range(h):
            for i in range(w):
                out.add((x + i, y + j))
        if bib:
            for i in range(w):
                out.add((x + i, y + h))
    return out


def section(name, lines):
    return "[%s]\n%s\n" % (name, "\n".join(lines) if lines else "")


def carry_over(src_text, name):
    """Lift a whole section body out of SCG01EA.INI verbatim."""
    key = "[%s]\n" % name
    i = src_text.index(key) + len(key)
    j = src_text.index("\n[", i)
    return src_text[i:j].strip("\n").splitlines()


def build(src_text):
    out = []
    out.append(section("BASIC", [
        "CarryOverCap=-1", "CarryOverMoney=0", "Intro=x", "BuildLevel=98",
        "Theme=No theme", "Percent=0", "Player=GoodGuy", "Win=CONSYARD",
        "Lose=GAMEOVER", "Brief=GDI1", "Action=LANDING",
    ]))
    out.append(section("MAP", [
        "TacticalPos=%d" % cell(*TACTICAL),
        "Height=%d" % MAP_H, "Width=%d" % MAP_W,
        "Y=%d" % MAP_Y, "X=%d" % MAP_X, "Theater=TEMPERATE",
    ]))
    out.append(section("GoodGuy", [
        "FlagHome=0", "FlagLocation=0", "MaxBuilding=150",
        "Allies=GoodGuy,Neutral", "MaxUnit=150",
        "Credits=%d" % (GDI_CREDITS // 100), "Edge=South",
    ]))
    out.append(section("BadGuy", [
        "FlagHome=0", "FlagLocation=0", "MaxBuilding=150", "Allies=BadGuy",
        "MaxUnit=222", "Credits=%d" % (NOD_CREDITS // 100), "Edge=North",
    ]))
    out.append(section("Neutral", [
        "FlagHome=0", "FlagLocation=0", "MaxBuilding=150", "Allies=Neutral",
        "MaxUnit=150", "Edge=North", "Credits=0",
    ]))

    units = []
    n = 0
    for house, table in (("GoodGuy", GDI_UNITS), ("BadGuy", NOD_UNITS)):
        for (typ, x, y, face, mission) in table:
            units.append("%03d=%s,%s,256,%d,%d,%s,None"
                         % (n, house, typ, cell(x, y), face, mission))
            n += 1
    out.append(section("UNITS", units))

    inf = []
    n = 0
    for house, table in (("GoodGuy", GDI_INFANTRY), ("BadGuy", NOD_INFANTRY)):
        for (typ, x, y, sub, face, mission) in table:
            inf.append("%03d=%s,%s,256,%d,%d,%s,%d,None"
                       % (n, house, typ, cell(x, y), sub, mission, face))
            n += 1
    out.append(section("INFANTRY", inf))

    st = []
    n = 0
    for house, table in (("GoodGuy", GDI_STRUCTURES), ("BadGuy", NOD_STRUCTURES)):
        for (typ, x, y) in table:
            st.append("%03d=%s,%s,256,%d,0,None" % (n, house, typ, cell(x, y)))
            n += 1
    out.append(section("STRUCTURES", st))

    # Trees and the sandbag walls come straight from GDI 1: they are what the
    # placement probe measured, so the layout check above is only valid with them.
    out.append(section("TERRAIN", carry_over(src_text, "TERRAIN")))
    out.append(section("TEMPLATE", []))

    overlay = list(carry_over(src_text, "OVERLAY"))
    have = {ln.split("=", 1)[0] for ln in overlay}
    taken = footprint_cells()
    added = 0
    for y in range(TIB_Y0, TIB_Y1 + 1):
        for x in range(TIB_X0, TIB_X1 + 1):
            key = str(cell(x, y))
            if key not in have and (x, y) not in taken:
                overlay.append("%s=TI1" % key)
                added += 1
    out.append(section("OVERLAY", overlay))
    out.append(section("SMUDGE", []))

    out.append(section("Triggers", ["%s=%s" % (k, v) for k, v in TRIGGERS]))
    out.append(section("CellTriggers", []))
    out.append(section("Teams", []))
    wp = []
    for i in range(31, -1, -1):
        wp.append("%d=%d" % (i, cell(*WAYPOINTS[i]) if i in WAYPOINTS else -1))
    out.append(section("Waypoints", wp))
    out.append(section("TeamTypes", ["%s=%s" % (k, v) for k, v in TEAMTYPES]))
    out.append(section("Base", ["Count=0"]))

    for h in ("Special", "Multi1", "Multi2", "Multi3", "Multi4", "Multi5", "Multi6"):
        out.append(section(h, [
            "FlagHome=0", "FlagLocation=0", "Allies=%s" % h, "MaxBuilding=150",
            "MaxUnit=150", "Edge=North", "Credits=0",
        ]))
    return "\n".join(out), added


def main():
    outdir = HERE
    if "--out" in sys.argv:
        outdir = sys.argv[sys.argv.index("--out") + 1]
    src = os.path.join(HERE, "SCG01EA.INI")
    if not os.path.exists(src):
        src = os.path.join(outdir, "SCG01EA.INI")
    text = open(src).read()

    ini, ntib = build(text)
    open(os.path.join(outdir, SCEN + ".INI"), "w").write(ini)
    shutil.copy(os.path.join(os.path.dirname(src), "SCG01EA.BIN"),
                os.path.join(outdir, SCEN + ".BIN"))

    print("wrote %s.INI + %s.BIN in %s" % (SCEN, SCEN, outdir))
    print("  map window   X=%d Y=%d W=%d H=%d  (world rows %d..%d)"
          % (MAP_X, MAP_Y, MAP_W, MAP_H, MAP_Y, MAP_Y + MAP_H - 1))
    print("  GDI          %d structures, %d units, %d infantry, %d credits"
          % (len(GDI_STRUCTURES), len(GDI_UNITS), len(GDI_INFANTRY), GDI_CREDITS))
    print("  Nod          %d structures, %d units, %d infantry, %d credits"
          % (len(NOD_STRUCTURES), len(NOD_UNITS), len(NOD_INFANTRY), NOD_CREDITS))
    print("  tiberium     %d cells offered in x%d..%d y%d..%d "
          "(the engine keeps the buildable ones)"
          % (ntib, TIB_X0, TIB_X1, TIB_Y0, TIB_Y1))
    print("  waves        %d team types, %d triggers; first Create Team at tick 90, "
          "first reinforcement at tick 1080" % (len(TEAMTYPES), len(TRIGGERS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
