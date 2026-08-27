#!/usr/bin/env python3
"""
CNC3D -- one-command gate for SCG90EA, the test map.

Regenerates the mission, runs proof_testmap.txt through the real ./cnc_eyes, and then
ASSERTS on engine state rather than on the exit code. Every claim below failed at least
once while the map was being built, so none of them is decoration:

  base is the player's        15 structures at boot, 10 of them GoodGuy, at the exact
                              cells the generator asked for
  sidebar has both columns    >= 12 entries in column 0 and >= 6 in column 1 at tick 2
                              (this was 0/0 for a while: see the note about
                              the player house needing a UNIT before Map.Recalc lands)
  building production works   a NUKE started from the sidebar completes and places, and
                              the player's structure count goes up
  unit production works       clicking the MTNK cameo puts a NEW MTNK on the map
  MCV deploys                 the MCV disappears and a second FACT stands at (59,50)
  the harvesters harvest      PlayerPtr->Tiberium goes 0 -> >= 500 and both harvesters
                              are still alive at tick 1500
  the enemy actually attacks  Nod mobile objects cross into the GDI half of the map, the
                              effects log shows gunfire and vehicle hits, and the death
                              counter is non-zero by tick 1500

Usage:  python3 verify_testmap.py [--rt DIR]
        --rt points at a folder holding cnc_eyes, the packs, the dylib and missions/.
        Defaults to ./rt next to this script.
Exit 0 only when every claim holds.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

OBJ = re.compile(r"^OBJ\|(\w+)\|(\w+)\|(\w+)\|newcell=\d+\|x=(\d+)\|y=(\d+)\|"
                 r".*?\|limbo=(\d)\|.*?\|id=(\d+)\|")

GDI_EXPECTED = {("FACT", 53, 51), ("NUKE", 50, 51), ("NUKE", 59, 47),
                ("PROC", 50, 46), ("WEAP", 53, 46), ("PYLE", 57, 53),
                ("GTWR", 50, 44), ("GTWR", 55, 44), ("GTWR", 47, 45),
                ("GTWR", 58, 45)}
DEPLOY_ORIGIN = (59, 50)

FAILS = []


def claim(name, ok, detail):
    print("%s: %s  (%s)" % ("PROVEN" if ok else "FAILED", name, detail))
    if not ok:
        FAILS.append(name)


def sections(out):
    sec, cur = {}, None
    for line in out.splitlines():
        if line.startswith("ECHO|"):
            cur = line.split("|", 1)[1]
            sec[cur] = []
        elif cur is not None:
            sec[cur].append(line)
    return sec


def objs(lines, kind=None, typ=None, house=None):
    got = []
    for ln in lines:
        m = OBJ.match(ln)
        if not m:
            continue
        if kind and m.group(1) != kind:
            continue
        if typ and m.group(2) != typ:
            continue
        if house and m.group(3) != house:
            continue
        got.append((int(m.group(7)), m.group(2), int(m.group(4)), int(m.group(5)),
                    int(m.group(6))))
    return got   # (id, type, x, y, limbo)


def main():
    rt = os.path.join(HERE, "rt")
    if "--rt" in sys.argv:
        rt = sys.argv[sys.argv.index("--rt") + 1]

    # the generator lives next to this file, or in ./missions when this file sits in
    # game/ the way prove.py does
    gen = os.path.join(HERE, "make_testmap.py")
    if not os.path.exists(gen):
        gen = os.path.join(HERE, "missions", "make_testmap.py")
    gendir = os.path.dirname(gen)
    subprocess.run([sys.executable, gen], check=True, capture_output=True)
    for f in ("SCG90EA.INI", "SCG90EA.BIN"):
        src, dst = os.path.join(gendir, f), os.path.join(rt, "missions", f)
        if os.path.abspath(src) != os.path.abspath(dst):
            open(dst, "wb").write(open(src, "rb").read())
    script = os.path.join(HERE, "proof_testmap.txt")
    open(os.path.join(rt, "proof_testmap.txt"), "w").write(open(script).read())

    cmd = ["./cnc_eyes", "--scen", "SCG90EA", "--pack", "SCG01EA.pack",
           "--cameos", "cameos.pack", "--dospack", "dossidebar.pack",
           "--dosinf", "dosinfantry.pack", "--dylib", "TiberianDawn.dylib",
           "--dir", "missions/", "--content", "content/",
           "--script", "proof_testmap.txt"]
    p = subprocess.run(cmd, cwd=rt, capture_output=True, text=True, timeout=1200)
    out = p.stdout
    open(os.path.join(HERE, "verify.log"), "w").write(out + "\n--- stderr ---\n" + p.stderr)

    claim("script ran clean", p.returncode == 0 and "0 failures" in out,
          "exit=%d" % p.returncode)

    sec = sections(out)
    base = sec.get("T0", [])

    # ---- the base ------------------------------------------------------------
    got = {(t, x, y) for (_, t, x, y, lb) in
           objs(base, "BUILDING", house="GoodGuy") if lb == 0}
    claim("GDI base stands where the generator put it",
          GDI_EXPECTED <= got, "missing: %s" % sorted(GDI_EXPECTED - got))
    nod = objs(base, "BUILDING", house="BadGuy")
    claim("Nod base stands", len(nod) == 7, "%d Nod structures" % len(nod))

    # ---- the sidebar ---------------------------------------------------------
    cols = {0: 0, 1: 0}
    for ln in out.splitlines():
        if ln.startswith("SBITEM|"):
            f = ln.split("|")
            cols[int(f[2].split("=")[1])] += 1
        if ln.startswith("SIDEBAR|MTNK-STARTED"):
            break
    claim("sidebar fills both columns at boot", cols[0] >= 12 and cols[1] >= 6,
          "col0=%d col1=%d" % (cols[0], cols[1]))

    # ---- building production -------------------------------------------------
    placed = re.search(r"SCRIPT\|place\|auto -> cell (\d+),(\d+)", out)
    n0 = len(objs(base, "BUILDING", house="GoodGuy"))
    n1 = len(objs(sec.get("AFTER-NUKE", []), "BUILDING", house="GoodGuy"))
    claim("a sidebar-built power plant reaches the ground",
          "COMPLETED" in out and placed is not None and n1 > n0,
          "structures %d -> %d, placed at %s" % (n0, n1, placed.groups() if placed else "-"))

    # ---- unit production -----------------------------------------------------
    b = {i for (i, _, _, _, _) in objs(base, "UNIT", "MTNK", "GoodGuy")}
    a = {i for (i, _, _, _, lb) in objs(sec.get("AFTER-MTNK", []), "UNIT", "MTNK",
                                        "GoodGuy") if lb == 0}
    claim("the weapons factory produced a new MTNK", len(a - b) >= 1,
          "new MTNK ids %s" % sorted(a - b))

    # ---- MCV deploy ----------------------------------------------------------
    mcv_before = objs(base, "UNIT", "MCV", "GoodGuy")
    after = sec.get("AFTER-DEPLOY", [])
    mcv_after = objs(after, "UNIT", "MCV", "GoodGuy")
    facts = {(x, y) for (_, _, x, y, lb) in objs(after, "BUILDING", "FACT", "GoodGuy")
             if lb == 0}
    claim("the MCV deployed into a second construction yard",
          len(mcv_before) == 1 and not mcv_after and DEPLOY_ORIGIN in facts,
          "MCV %d -> %d, FACTs at %s" % (len(mcv_before), len(mcv_after), sorted(facts)))

    # ---- economy -------------------------------------------------------------
    tib = 0
    for ln in out.splitlines():
        m = re.match(r"SIDEBAR\|T1500\|credits=\d+\|tib=(\d+)/", ln)
        if m:
            tib = int(m.group(1))
    harv = [h for h in objs(sec.get("T1500", []), "UNIT", "HARV", "GoodGuy") if h[4] == 0]
    claim("the harvesters harvest", tib >= 500 and len(harv) == 2,
          "tiberium in store at t1500 = %d, harvesters alive = %d" % (tib, len(harv)))

    # ---- the enemy attacks ---------------------------------------------------
    def invaders(marker):
        return [o for o in objs(sec.get(marker, []), house="BadGuy")
                if o[0] is not None and o[4] == 0 and o[3] >= 44]
    ever = max(len(invaders(m)) for m in ("AFTER-MTNK", "AFTER-DEPLOY", "T1500",
                                          "T2250", "T3000"))
    claim("Nod crosses into the GDI half of the map", ever >= 1,
          "peak Nod objects at y>=44: %d" % ever)

    seen = dict(re.findall(r"EFXSEEN\|([^|]+)\|(\d+)", out))
    deaths = [int(d) for d in re.findall(r"EFXDUMP-BEGIN .*deaths_seen=(\d+)", out)]
    shots = sum(int(v) for k, v in seen.items()
                if k in ("GUNFIRE", "MINIGUN", "PIFF", "PIFFPIFF"))
    hits = sum(int(v) for k, v in seen.items() if k.startswith("VEH-HIT"))
    claim("combat really happens", shots >= 100 and hits >= 10 and max(deaths or [0]) >= 5,
          "muzzle/impact effects=%d, vehicle hits=%d, deaths=%d"
          % (shots, hits, max(deaths or [0])))

    print()
    if FAILS:
        print("FAILED %d of 10 claims: %s" % (len(FAILS), FAILS))
        return 1
    print("ALL PROVEN (10 claims)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
