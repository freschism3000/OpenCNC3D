#!/usr/bin/env python3
"""
CNC3D -- SCG01EB, "GDI mission 1, but the player can build".

WHY THIS EXISTS
  The sidebar work needs a mission where the player actually has a factory. Neither
  shipped mission qualifies:
    SCB01EA  Nod 1, BuildLevel 1, the player owns no construction yard at all.
    SCG01EA  GDI 1, the player owns an MCV which has not deployed, so at frame 0 the
             sidebar is empty and stays empty until the MCV is deployed.
  Rather than fake a sidebar, this derives a scenario from SCG01EA with three edits and
  says exactly what they are. Everything else about the mission is untouched, and every
  build action in the test scripts still goes through CNC_Handle_Sidebar_Request.

THE THREE EDITS
  1. [BASIC] BuildLevel 1 -> 98, so the tech tree is not artificially truncated. C&C
     prerequisites still apply: with only a construction yard the sidebar offers six
     items, and a power plant is what unlocks PROC/NUK2/PYLE/FIX.
  2. [GoodGuy] Credits 20 -> 100. The INI value is in hundreds, so 2000 -> 10000. A power
     plant is 300, so this only removes "ran out of money" as a confounder.
  3. [STRUCTURES] one new line: GoodGuy owns a FACT (Construction Yard) at INI cell 3317,
     which is x=53 y=51 in the 64-wide INI cell space (cell = y*64 + x). FACT is 3x2 and
     that spot is clear on this map; the MCV keeps its own start at x=56 y=53.

  The .BIN (terrain) is copied unchanged.

Run:  python3 make_scg01eb.py            # writes SCG01EB.INI and SCG01EB.BIN alongside
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FACT_X, FACT_Y = 53, 51


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
    cell = FACT_Y * 64 + FACT_X
    txt = once("[STRUCTURES]\n",
               "[STRUCTURES]\n003=GoodGuy,FACT,256,%d,0,None\n" % cell,
               "structures section")

    open(os.path.join(HERE, "SCG01EB.INI"), "w").write(txt)
    shutil.copy(os.path.join(HERE, "SCG01EA.BIN"), os.path.join(HERE, "SCG01EB.BIN"))
    print("wrote SCG01EB.INI + SCG01EB.BIN (FACT at ini cell %d = x %d, y %d)"
          % (cell, FACT_X, FACT_Y))
    return 0


if __name__ == "__main__":
    sys.exit(main())
