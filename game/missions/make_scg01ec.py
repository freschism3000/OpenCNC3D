#!/usr/bin/env python3
"""
CNC3D -- SCG01EC, "GDI mission 1 with a whole working base".

WHY THIS EXISTS
  SCG01EB gave the player a construction yard, which proves BUILDING production. It
  cannot prove UNIT production or MCV deploy:
    - unit production needs a barracks (PYLE) and a weapons factory (WEAP) standing,
    - MCV deploy needs an MCV whose deploy footprint is actually clear, and in EB the
      MCV starts at (56,53) where deploying would overlap the pre-placed FACT at
      (53..55, 51..52): UnitClass::Try_To_Deploy puts the FACT origin one cell NW of
      the MCV, i.e. at (55,52), which IS the FACT. The 1995 engine correctly refuses.
  So EC is EB plus three pre-placed structures and one moved unit.

DERIVATION (from SCG01EA.INI, same recipe as make_scg01eb.py, plus four edits)
  1. BuildLevel 1 -> 98            same as EB
  2. Credits 20 -> 100 (=10000)    same as EB
  3. [STRUCTURES] gains, all GoodGuy, all on ground the engine itself called clear
     (probe_eb_placemap.txt ran GAME_STATE_PLACEMENT on SCG01EB and the cells below
     are '#'/'c' in that dump):
       003 FACT at (53,51)  ini 3317   same as EB
       004 NUKE at (51,51)  ini 3315   power stays positive: +100 vs ~65 drain,
                                       so factories run at full speed
       005 PYLE at (53,48)  ini 3125   2x2+bib on clear cells, infantry exit south
       006 WEAP at (50,46)  ini 2994   3x2+bib on clear cells, vehicle exit south
  4. The MCV moves from (56,53) ini 3448 to (57,49) ini 3193. Deploy origin is one
     cell NW = (56,48): the 3x2 FACT footprint (56..58, 48..49) plus bib row (50) is
     all clear per the same placemap, so COMMAND_AT_POSITION on the MCV's own cell
     must succeed and produce a second FACT.

  Cell arithmetic: INI cells are y*64+x (64-wide INI space); the engine reports
  y*128+x (MEGAMAPS). x,y are identical in both.

  The .BIN (terrain) is copied unchanged from SCG01EA.

Run:  python3 make_scg01ec.py            # writes SCG01EC.INI and SCG01EC.BIN alongside
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))

FACT_X, FACT_Y = 53, 51
NUKE_X, NUKE_Y = 51, 51
PYLE_X, PYLE_Y = 53, 48
WEAP_X, WEAP_Y = 50, 46
MCV_X, MCV_Y = 57, 49


def ini_cell(x, y):
    return y * 64 + x


def main():
    src = os.path.join(HERE, "SCG01EA.INI")
    txt = open(src).read()

    def once(old, new, what):
        if txt.count(old) != 1:
            raise SystemExit("SCG01EA.INI: %s -- expected exactly one '%s', found %d"
                             % (what, old.splitlines()[0], txt.count(old)))
        return txt.replace(old, new)

    txt = once("BuildLevel=1", "BuildLevel=98", "build level")
    txt = once("MaxUnit=150\nCredits=20\nEdge=South",
               "MaxUnit=150\nCredits=100\nEdge=South", "GoodGuy credits")

    adds = (
        "003=GoodGuy,FACT,256,%d,0,None\n" % ini_cell(FACT_X, FACT_Y)
        + "004=GoodGuy,NUKE,256,%d,0,None\n" % ini_cell(NUKE_X, NUKE_Y)
        + "005=GoodGuy,PYLE,256,%d,0,None\n" % ini_cell(PYLE_X, PYLE_Y)
        + "006=GoodGuy,WEAP,256,%d,0,None\n" % ini_cell(WEAP_X, WEAP_Y)
    )
    txt = once("[STRUCTURES]\n", "[STRUCTURES]\n" + adds, "structures section")

    txt = once("000=GoodGuy,MCV,256,3448,0,Guard,None",
               "000=GoodGuy,MCV,256,%d,0,Guard,None" % ini_cell(MCV_X, MCV_Y),
               "MCV start cell")

    open(os.path.join(HERE, "SCG01EC.INI"), "w").write(txt)
    shutil.copy(os.path.join(HERE, "SCG01EA.BIN"), os.path.join(HERE, "SCG01EC.BIN"))
    print("wrote SCG01EC.INI + SCG01EC.BIN")
    print("  FACT (%d,%d) ini %d" % (FACT_X, FACT_Y, ini_cell(FACT_X, FACT_Y)))
    print("  NUKE (%d,%d) ini %d" % (NUKE_X, NUKE_Y, ini_cell(NUKE_X, NUKE_Y)))
    print("  PYLE (%d,%d) ini %d" % (PYLE_X, PYLE_Y, ini_cell(PYLE_X, PYLE_Y)))
    print("  WEAP (%d,%d) ini %d" % (WEAP_X, WEAP_Y, ini_cell(WEAP_X, WEAP_Y)))
    print("  MCV  (%d,%d) ini %d (deploy origin %d,%d)"
          % (MCV_X, MCV_Y, ini_cell(MCV_X, MCV_Y), MCV_X - 1, MCV_Y - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
