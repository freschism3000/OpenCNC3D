#!/usr/bin/env python3
"""
CNC3D -- prod: one-command proof of unit production + MCV deploy on SCG01EC.

Runs the three proof scripts through ./cnc_eyes and then ASSERTS on the objdump
evidence, not on exit codes alone:

  proof_units.txt    a NEW GoodGuy E1 (an id absent at baseline) stands limbo=0 within
                     3 cells of the PYLE, and a NEW GoodGuy JEEP within 4 cells of the
                     WEAP. Reinforcement E1s/JEEPs land at the beach (y >= 55) and are
                     excluded by the same distance rule, so they cannot fake a pass.
  proof_deploy.txt   FACT count 1 -> 2 with the new FACT at exactly (56,48), the NW
                     deploy origin of the MCV at (57,49); the MCV is gone.
  proof_hotkeys.txt  stop (CNC_Handle_Unit_Request 5) clears navcell after a move
                     order; guard (request 4) leaves the unit in (Area) Guard.

Usage:  python3 prove.py [--dylib PATH]
Exit 0 only when every assertion holds. Prints one PROVEN/FAILED line per claim.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRATCH = os.path.abspath(os.path.join(HERE, "..", ".."))
DYLIB = os.path.join(SCRATCH, "brain", "vanilla", "build-native", "tiberiandawn",
                     "TiberianDawn.dylib")
CONTENT = os.path.join(SCRATCH, "brain", "stubcontent") + "/"

PYLE = (53, 48.5)   # 2x2 at (53..54, 48..49); exit is the south side
WEAP = (51, 47)     # 3x3 at (50..52, 46..48)
DEPLOY_ORIGIN = (56, 48)

OBJ_RE = re.compile(r"^OBJ\|(\w+)\|(\w+)\|(\w+)\|newcell=\d+\|x=(\d+)\|y=(\d+)\|"
                    r".*?\|limbo=(\d)\|.*?\|id=(\d+)\|")
SEL_RE = re.compile(r"^SEL\|\w+\|(\w+)\|id=(\d+)\|cell=\d+\|x=\d+\|y=\d+\|"
                    r"mission=([^|]+)\|navcell=(-?\d+)")


def run(script):
    cmd = ["./cnc_eyes", "--scen", "SCG01EC", "--pack", "SCG01EA.pack",
           "--cameos", "cameos.pack", "--dospack", "dossidebar.pack",
           "--dylib", DYLIB, "--dir", "missions/", "--content", CONTENT,
           "--script", script]
    p = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True, timeout=600)
    return p.returncode, p.stdout


def sections(out):
    """Split stdout on ECHO| markers -> {marker: [lines]}"""
    sec, cur = {}, None
    for line in out.splitlines():
        if line.startswith("ECHO|"):
            cur = line.split("|", 1)[1]
            sec[cur] = []
        elif cur is not None:
            sec[cur].append(line)
    return sec


def objs(lines, kind, typ, house):
    got = {}
    for ln in lines:
        m = OBJ_RE.match(ln)
        if m and m.group(1) == kind and m.group(2) == typ and m.group(3) == house:
            got[int(m.group(7))] = (int(m.group(4)), int(m.group(5)), int(m.group(6)))
    return got   # id -> (x, y, limbo)


FAILS = []


def claim(name, ok, detail):
    print("%s: %s  (%s)" % ("PROVEN" if ok else "FAILED", name, detail))
    if not ok:
        FAILS.append(name)


def near(pos, centre, r):
    return max(abs(pos[0] - centre[0]), abs(pos[1] - centre[1])) <= r


def main():
    if "--dylib" in sys.argv:
        global DYLIB
        DYLIB = sys.argv[sys.argv.index("--dylib") + 1]

    # regenerate the mission so the proof always tests the tooling too
    subprocess.run([sys.executable, "make_scg01ec.py"],
                   cwd=os.path.join(HERE, "missions"), check=True,
                   capture_output=True)

    # ---- units ---------------------------------------------------------------
    rc, out = run("proof_units.txt")
    open(os.path.join(HERE, "out_units.log"), "w").write(out)
    ok_rc = (rc == 0 and "0 failures" in out)
    claim("units script clean", ok_rc, "exit=%d" % rc)
    sec = sections(out)
    base_e1 = objs(sec.get("BASE", []), "INFANTRY", "E1", "GoodGuy")
    after_e1 = objs(sec.get("AFTER-E1", []), "INFANTRY", "E1", "GoodGuy")
    new_e1 = {i: p for i, p in after_e1.items()
              if i not in base_e1 and p[2] == 0 and near(p, PYLE, 3)}
    claim("E1 produced at the barracks", len(new_e1) == 1,
          "new ids near PYLE: %s" % ({i: p[:2] for i, p in new_e1.items()}))
    base_jp = objs(sec.get("BASE", []), "UNIT", "JEEP", "GoodGuy")
    after_jp = objs(sec.get("AFTER-JEEP", []), "UNIT", "JEEP", "GoodGuy")
    new_jp = {i: p for i, p in after_jp.items()
              if i not in base_jp and p[2] == 0 and near(p, WEAP, 4)}
    claim("JEEP exited the weapons factory", len(new_jp) == 1,
          "new ids near WEAP: %s" % ({i: p[:2] for i, p in new_jp.items()}))
    claim("units column clicked, not scripted around",
          "SIDEBAR-INPUT|build|E1" in out and "SIDEBAR-INPUT|build|JEEP" in out
          and "SIDEBAR-INPUT|scroll|col=1" in out,
          "sb_click drove both builds and the scroll arrow")

    # ---- deploy --------------------------------------------------------------
    rc, out = run("proof_deploy.txt")
    open(os.path.join(HERE, "out_deploy.log"), "w").write(out)
    claim("deploy script clean", rc == 0 and "0 failures" in out, "exit=%d" % rc)
    sec = sections(out)
    base_fact = objs(sec.get("BASE", []), "BUILDING", "FACT", "GoodGuy")
    base_mcv = objs(sec.get("BASE", []), "UNIT", "MCV", "GoodGuy")
    aft_fact = objs(sec.get("AFTER-DEPLOY", []), "BUILDING", "FACT", "GoodGuy")
    aft_mcv = objs(sec.get("AFTER-DEPLOY", []), "UNIT", "MCV", "GoodGuy")
    new_fact = {i: p for i, p in aft_fact.items() if i not in base_fact}
    ok = (len(base_fact) == 1 and len(base_mcv) == 1 and len(aft_mcv) == 0
          and len(new_fact) == 1
          and list(new_fact.values())[0][:2] == DEPLOY_ORIGIN)
    claim("MCV deployed into a FACT at (56,48)", ok,
          "FACT %d->%d, MCV %d->%d, new at %s"
          % (len(base_fact), len(aft_fact), len(base_mcv), len(aft_mcv),
             {i: p[:2] for i, p in new_fact.items()}))

    # ---- guard/stop ----------------------------------------------------------
    rc, out = run("proof_hotkeys.txt")
    open(os.path.join(HERE, "out_hotkeys.log"), "w").write(out)
    claim("hotkeys script clean", rc == 0 and "0 failures" in out, "exit=%d" % rc)
    sels = [SEL_RE.match(l) for l in out.splitlines()]
    sels = [(m.group(3).strip(), int(m.group(4))) for m in sels if m]
    # expected sequence: idle -> Move with nav -> nav cleared by stop -> guard
    ok = (len(sels) >= 4 and sels[1][0] == "Move" and sels[1][1] >= 0
          and sels[2][1] == -1 and "Guard" in sels[3][0] and sels[3][1] == -1)
    claim("stop clears nav, guard guards (unit requests 5/4)", ok,
          "SEL sequence: %s" % sels)

    print()
    if FAILS:
        print("FAILED: %d of 7 claims: %s" % (len(FAILS), FAILS))
        return 1
    print("ALL PROVEN (7 claims)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
