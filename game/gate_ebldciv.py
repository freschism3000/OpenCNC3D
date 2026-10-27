#!/usr/bin/env python3
"""G308's two halves: the scene it stages and the claims it checks.

    python3 gate_ebldciv.py maps DIR        write the two test missions and their scripts
    python3 gate_ebldciv.py check LOGS DIR  read the runs' logs (shots/g308*.log) and print
                                            "1 message" or "0 why"

THE SCENE. Every remastered civilian building and both remastered fences, on copies of two
skirmish maps turned into single-player missions: SCG97EA (temperate, from SCM01EA) holds
the temperate civilians V01 to V19 (the fields and haystacks V12 to V18 in their structure
form among them, the oil pump V19 too) and a run of each fence, barbed wire and wood;
SCG98EA (desert, from SCM02EA) holds the desert civilians V20 to V37. Every building is
owned by Neutral, as the missions place them, and two GoodGuy Medium Tanks a few cells south
of it are ordered to force-fire on one of the cells it stands on (its occupy list's), so
real shells wear it down from the south, break it at half strength and kill it. Each fence
run is five cells with a spur of two north of its middle, and two tanks force-fire on its
second cell.

Each script orders the attacks and then dumps the system's state every ten ticks (ebldump),
with a few shots on the way; the engine prints every stage change as it happens (EBLD|hit,
damaged, wreck, blast, sink, gone). The run folder must hold missions/SCM01EA and SCM02EA
and the packs named in gates.sh.
"""
import json
import os
import re
import sys

# bdata.cpp: each civilian's size and the cells its occupy list blocks (dx, dy from its
# top-left cell); the theater the cartridge's missions place it in
FOOT = {}
for t in ("V01", "V02", "V04", "V20", "V24"):
    FOOT[t] = ((2, 2), [(0, 1), (1, 1)])                      # List0011
for t in ("V03", "V25"):
    FOOT[t] = ((2, 2), [(1, 0), (0, 1), (1, 1)])              # List0111
FOOT["V21"] = ((2, 2), [(0, 0), (1, 0), (1, 1)])              # List1101
for t in ("V05", "V06", "V07", "V22", "V26", "V30", "V31", "V32", "V33"):
    FOOT[t] = ((2, 1), [(0, 0), (1, 0)])                      # List11
for t in ["V%02d" % i for i in range(8, 20)] + ["V23", "V27", "V28", "V29", "V34", "V35", "V36"]:
    FOOT[t] = ((1, 1), [(0, 0)])                              # List1
FOOT["V37"] = ((4, 2), [(1, 0), (2, 0), (3, 0), (1, 1), (2, 1), (3, 1)])   # ListWestwood
# the death's centre the lab gives the ones that stand on part of their footprint
# (fx.json explosion `at`, cells from the footprint centre, x east, y north)
AT = {"V01": (0.0, -0.5), "V02": (0.0, -0.5), "V03": (0.16667, -0.16667), "V04": (0.0, -0.5),
      "V20": (0.0, -0.5), "V21": (0.1667, 0.1667), "V25": (0.1667, -0.1667), "V37": (0.5, 0.0)}

# A building turned on its plot (art/remaster/yaw.json: degrees counter-clockwise seen from
# above, about the footprint centre) has its death's centre turned with it, as the baker
# turns it.
def _turned_at(at):
    import json, math, os
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "art", "remaster", "yaw.json")
    y = json.load(open(p)) if os.path.isfile(p) else {}
    out = {}
    for t, (e, n) in at.items():
        v = y.get(t, 0)
        r = math.radians(float(v)) if isinstance(v, (int, float)) else 0.0
        c, s = math.cos(r), math.sin(r)
        out[t] = (e * c - n * s, e * s + n * c)
    return out


AT = _turned_at(AT)
MAPS = {"SCG97EA": ("SCM01EA", ["V%02d" % i for i in range(1, 20)]),
        "SCG98EA": ("SCM02EA", ["V%02d" % i for i in range(20, 38)])}
FENCES = {"BARB": (8, 54), "WOOD": (20, 54)}           # SCG97EA: the run's west end
TAIL = ["", "[TERRAIN]", "", "[SMUDGE]", "", "[Triggers]", "", "[CellTriggers]", "",
        "[Teams]", "", "[Waypoints]", "", "[Base]", "Count=0", ""]
# the dumps: every ten ticks, this many of them
NDUMP = 130


def cell(x, y):
    return y * 64 + x


def fence_cells(kind):
    x0, y0 = FENCES[kind]
    return [(x0 + i, y0) for i in range(5)] + [(x0 + 2, y0 - 1), (x0 + 2, y0 - 2)], (x0 + 1, y0)


def maps(out, sheet=False):
    os.makedirs(out, exist_ok=True)
    layout = {}
    for name, (src_name, types) in MAPS.items():
        src = os.path.join("missions", src_name)
        b = open(src + ".BIN", "rb").read()
        ini = open(src + ".INI", encoding="latin-1").read()
        mapsec = re.search(r"\[MAP\].*?(?=\n\[)", ini, re.S).group(0).replace("\r", "").strip()
        # (inside the map's own playable rectangle, a cell from its edge)
        mx, my = int(re.search(r"\bX=(\d+)", mapsec).group(1)), int(re.search(r"\bY=(\d+)", mapsec).group(1))
        mw = int(re.search(r"\bWidth=(\d+)", mapsec).group(1))
        mh = int(re.search(r"\bHeight=(\d+)", mapsec).group(1))
        clear = lambda x, y: (mx + 1 <= x < mx + mw - 1 and my + 1 <= y < my + mh - 1
                              and b[(y * 64 + x) * 2] in (0, 0xFF))
        busy = set()
        if name == "SCG97EA":
            for kind in FENCES:
                cells, _ = fence_cells(kind)
                for x, y in cells:
                    for dx in range(-2, 3):
                        for dy in range(-3, 7):
                            busy.add((x + dx, y + dy))
        taken, st, un, ov = [], [], [], []
        for i, t in enumerate(types):
            (fw, fh), occ = FOOT[t]
            spot = None
            for y in range(my + 2, my + mh - 8):
                for x in range(mx + 2, mx + mw - 4):
                    if not all(clear(xx, yy) and (xx, yy) not in busy
                               for xx in range(x - 1, x + fw + 1) for yy in range(y - 1, y + fh + 5)):
                        continue
                    if any(abs(x - tx) < 8 and abs(y - ty) < 9 for tx, ty in taken):
                        continue
                    spot = (x, y)
                    break
                if spot:
                    break
            if not spot:
                sys.exit("gate_ebldciv: no room for %s on %s" % (t, name))
            x, y = spot
            taken.append(spot)
            st.append("%03d=Neutral,%s,256,%d,0,None" % (i, t, cell(x, y)))
            ox, oy = occ[-1]                      # a cell it blocks, its south-east one
            for tx in (x, x + fw):
                un.append("%03d=GoodGuy,MTNK,256,%d,0,Guard,None" % (len(un), cell(tx, y + fh + 3)))
            layout.setdefault(name, {"types": [], "fences": {}})
            layout[name]["types"].append(dict(type=t, x=x, y=y, fw=fw, fh=fh,
                                              aim=[x + ox, y + oy]))
        if name == "SCG97EA":
            for kind in FENCES:
                cells, target = fence_cells(kind)
                ov += ["%d=%s" % (cell(cx, cy), kind) for cx, cy in cells]
                for tx in (target[0] - 1, target[0] + 2):
                    un.append("%03d=GoodGuy,MTNK,256,%d,0,Guard,None" % (len(un), cell(tx, target[1] + 4)))
                layout[name]["fences"][kind] = dict(cells=cells, target=list(target))
        text = ["[Basic]", "Player=GoodGuy", "Action=x", "Lose=x", "Win=x", "Brief=x",
                "Intro=x", "BuildLevel=98", "Theme=No theme", "Percent=0", "CarryOverCap=-1",
                "CarryOverMoney=0", "", mapsec, "",
                "[GoodGuy]", "Allies=GoodGuy,Neutral", "MaxBuilding=150", "MaxUnit=150",
                "Credits=0", "Edge=South", "", "[Neutral]", "Allies=Neutral,GoodGuy",
                "MaxBuilding=150", "MaxUnit=150", "Credits=0", "Edge=North", "",
                "[BadGuy]", "Allies=BadGuy", "MaxBuilding=150", "MaxUnit=150", "Credits=0",
                "Edge=North", "", "[STRUCTURES]"] + st + ["", "[UNITS]"] + un + \
            ["", "[INFANTRY]", "", "[OVERLAY]"] + ov + TAIL
        open(os.path.join(out, name + ".INI"), "w", encoding="latin-1", newline="").write("\r\n".join(text))
        open(os.path.join(out, name + ".BIN"), "wb").write(b)
        # THE SCRIPT: first every building whole, shot from close, then the attacks ordered,
        # then a dump every ten ticks. With `sheet` (not the gate's: the pictures checked
        # beside the lab), every building and fence is shot at each stage instead, at the
        # moments the gate's own runs measured: held, damaged, the blast, the wreck at rest,
        # sinking, and the scorch left
        L = layout[name]
        s = ["tick 3", "zoom max", "dist %d" % (900 if sheet else 1500)]
        now = 3

        def look(d):
            return "cam %.1f %.1f" % (d["x"] + d["fw"] / 2.0, d["y"] + d["fh"] / 2.0 + 0.3)

        def fence_look(f):
            tx, ty = f["target"]
            return "cam %.1f %.1f" % (tx + 1.5, ty - 0.2)
        for d in L["types"]:
            s += [look(d), "shot shots/g308_%s_intact.png" % d["type"]]
        for kind, f in sorted(L["fences"].items()):
            s += [fence_look(f), "shot shots/g308_%s_intact.png" % kind]
        s += ["echo g308 whole", "ebldump"]
        n = 0
        for d in L["types"]:
            s += ["cam %.1f %.1f" % (d["x"] + d["fw"] / 2.0, d["y"] + d["fh"] + 2.0), "tick 1"]
            now += 1
            for j in (0, 1):
                s += ["lclickobj MTNK %d" % n, "actcell %d %d c" % tuple(d["aim"])]
                n += 1
        for kind, f in sorted(L["fences"].items()):
            tx, ty = f["target"]
            s += ["cam %d %d" % (tx + 1, ty + 2), "tick 1"]
            now += 1
            for j in (0, 1):
                s += ["lclickobj MTNK %d" % n, "actcell %d %d c" % (tx, ty)]
                n += 1
        if sheet:
            # (the engine frame each round is shot at; the Studio, four times the others'
            # strength, has its own; the fences go at their first level)
            rounds = [("held", 150), ("fblast", 44), ("fwreck", 80), ("damaged", 380),
                      ("blast", 482), ("wreck", 512), ("sink", 575), ("scorch", 800),
                      ("Vheld", 300), ("Vdamaged", 450), ("Vblast", 700), ("Vwreck", 740),
                      ("Vsink", 800), ("Vscorch", 1050)]
            rounds.sort(key=lambda r: r[1])
            for stage, at in rounds:
                if at > now:
                    s.append("tick %d" % (at - now))
                    now = at
                if stage.startswith("f"):
                    for kind, f in sorted(L["fences"].items()):
                        s += [fence_look(f), "shot shots/g308_%s_%s.png" % (kind, stage[1:])]
                else:
                    for d in L["types"]:
                        if (d["type"] == "V37") != stage.startswith("V"):
                            continue
                        st = stage[1:] if stage.startswith("V") else stage
                        s += [look(d), "shot shots/g308_%s_%s.png" % (d["type"], st)]
                s += ["echo g308 round %s" % stage, "ebldump"]
        else:
            for k in range(NDUMP):
                s += ["tick 10", "echo g308 dump %d" % k, "ebldump"]
                # the oil pump close, twice three ticks apart, held and then damaged: its
                # parts move with the cartridge's clip and then the damaged loop
                if k in (10, 30):
                    for v in L["types"]:
                        if v["type"] == "V19":
                            s += [look(v), "shot shots/g308_V19_%d_a.png" % k, "echo g308 pump %d a" % k,
                                  "ebldump", "tick 3", "shot shots/g308_V19_%d_b.png" % k,
                                  "echo g308 pump %d b" % k, "ebldump"]
        s.append("quit")
        open(os.path.join(out, name + (".sheet" if sheet else "") + ".txt"), "w").write("\n".join(s) + "\n")
    json.dump(layout, open(os.path.join(out, "layout.json"), "w"), indent=1)


def rd(path):
    try:
        with open(path, errors="ignore") as f:
            return f.read().splitlines()
    except OSError:
        return []


def main():
    if len(sys.argv) >= 3 and sys.argv[1] in ("maps", "sheet"):
        maps(sys.argv[2], sys.argv[1] == "sheet")
        return 0
    if len(sys.argv) >= 4 and sys.argv[1] == "check":
        print(check(sys.argv[2], sys.argv[3]))
        return 0
    sys.exit(__doc__)


def check(logs, d):
    A, A2, B = (rd(os.path.join(logs, n + ".log")) for n in ("g308a", "g308a2", "g308b"))
    N, K = rd(os.path.join(logs, "g308n.log")), rd(os.path.join(logs, "g308k.log"))
    layout = json.load(open(os.path.join(d, "layout.json")))
    why = []

    def need(c, m):
        if not c:
            why.append(m)
        return c

    def section(L, name):
        out, on = [], False
        for l in L:
            if l.startswith("ECHO|"):
                on = (l == "ECHO|g308 " + name)
                continue
            if on:
                out.append(l)
        return out
    # (fields an ebldump line gained later, the trees' wind=, winddrawn=, ride after blast=)
    ART = r"^EBLDART\|(\w+)\|v=(\d+)\|sets=(\d)\|wall=([01])\|.*\|drawn=(\d+)\|.*\|clipparts=(\d+)\|idle=(\d)\|cutclip=(\d+)\|blast=(-?\d+),(-?\d+)(?:\|.*)?$"
    said = []
    for L, name in ((A, "SCG97EA"), (B, "SCG98EA")):
        lay = layout[name]
        types = [x["type"] for x in lay["types"]]
        fences = sorted(lay["fences"])
        # (1) EVERY CIVILIAN AND FENCE IS READ AT FIRST SIGHT AND IS A FULL VERSION 6 MODEL
        seen = set(re.match(r"^ebld: ebld/(\w+)\.ebld read at first sight", l).group(1)
                   for l in L if re.match(r"^ebld: ebld/(\w+)\.ebld read at first sight", l))
        need(set(types + fences) <= seen, "%s: not read at first sight: %s"
             % (name, sorted(set(types + fences) - seen)))
        whole = section(L, "whole")
        art = {}
        for l in whole:
            m = re.match(ART, l)
            if m:
                art[m.group(1)] = m
        for t in types + fences:
            m = art.get(t)
            if not need(m, "%s: no EBLDART line for %s" % (name, t)):
                continue
            need(m.group(2) == "6" and m.group(3) == "3", "%s: %s is v=%s sets=%s, want 6 and 3"
                 % (name, t, m.group(2), m.group(3)))
            need((m.group(4) == "1") == (t in FENCES), "%s: %s's wall flag is %s" % (name, t, m.group(4)))
            # (2) THE INTACT MODEL DREW, by the observer, for every building shot whole
            need(t in FENCES or int(m.group(5)) > 0, "%s: %s's intact model was never drawn" % (name, t))
            bx, bz = int(m.group(9)), int(m.group(10))
            if t in AT:
                need((bx, bz) == (round(1024 * AT[t][0]), round(-1024 * AT[t][1])),
                     "%s: %s's blast centre is %d,%d" % (name, t, bx, bz))
            else:
                need((bx, bz) == (0, 0), "%s: %s has a blast centre %d,%d it should not" % (name, t, bx, bz))
        pump = art.get("V19")
        if name == "SCG97EA" and need(pump, "no oil pump"):
            need(pump.group(6) == "5" and pump.group(7) == "0" and pump.group(8) == "5",
                 "the oil pump's clips: %s parts, idle %s, cut %s; want 5, 0, 5"
                 % (pump.group(6), pump.group(7), pump.group(8)))
        # (3) EVERY CIVILIAN GOES THROUGH EVERY STAGE, and the wreck is its destroyed set
        key = {2000000 + i: t for i, t in enumerate(types)}
        ev = {t: {} for t in types}
        for l in L:
            m = re.match(r"^EBLD\|(hit|damaged|wreck|sink|gone|blast)\|key=(\d+)\|(.*)", l)
            if m and int(m.group(2)) in key:
                ev[key[int(m.group(2))]].setdefault(m.group(1), []).append(l)
        mesh = {}
        for l in L:
            m = re.match(r"^EBLDMESH\|(\w+)\|.*\|pieces=(\d+),(\d+)\|chips=(\d+)", l)
            if m:
                mesh[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
        for i, t in enumerate(types):
            e = ev[t]
            cut, dest, chips = mesh.get(t, (0, 0, 0))
            hits = e.get("hit", [])
            need(len(hits) >= 10, "%s: %d hits on %s, want ten or more" % (name, len(hits), t))
            off = [l for l in hits if "|chip=none" not in l]
            need(len(off) >= min(3, chips), "%s: %s lost %d chips of %d" % (name, t, len(off), chips))
            dm = [int(re.search(r"pieces=(\d+)", l).group(1)) for l in e.get("damaged", [])]
            # (a few break with no loose piece: the lab's cut keeps every piece of them up)
            need(len(dm) == 1, "%s: %s's damaged stages %s, want one" % (name, t, dm))
            wr = [int(re.search(r"pieces=(\d+)", l).group(1)) for l in e.get("wreck", [])]
            need(wr == [dest] and dest > 0, "%s: %s's wreck %s, want its %d shards" % (name, t, wr, dest))
            need("sink" in e and "gone" in e, "%s: %s's wreck never sank and went (%s)" % (name, t, sorted(e)))
            # (4) THE DEATH'S CENTRE: on the blocked cells for the eight that stand on part
            # of their footprint, nowhere else
            bl = e.get("blast", [])
            if t in AT:
                x = lay["types"][i]
                wx, wz = x["x"] + x["fw"] / 2.0 + AT[t][0], x["y"] + x["fh"] / 2.0 - AT[t][1]
                m = re.search(r"x=(-?[0-9.]+)\|z=(-?[0-9.]+)", bl[0]) if bl else None
                need(m and abs(float(m.group(1)) - wx) < 0.02 and abs(float(m.group(2)) - wz) < 0.02,
                     "%s: %s's blast at %s, want %.3f,%.3f" % (name, t, bl, wx, wz))
            else:
                need(not bl, "%s: %s blew up off its footprint's centre %s" % (name, t, bl))
        # no old shatter under Enhanced
        sh = [l for l in L if re.match(r"^SHATTER\|V\d\d\|", l)]
        need(not sh, "%s: the old shatter fired for a civilian under Enhanced [%s]" % (name, sh[0] if sh else ""))
        # (5) THE FENCES: the shot cell is blown apart, sinks and goes; one level, so no
        # damaged stage
        for kind, f in sorted(lay["fences"].items()):
            tx, ty = f["target"]
            k = 9000000000 + ty * 512 + tx
            fe = {}
            for l in L:
                m = re.match(r"^EBLD\|(hit|damaged|wreck|sink|gone)\|key=(\d+)\|(.*)", l)
                if m and int(m.group(2)) == k:
                    fe.setdefault(m.group(1), []).append(l)
            wr = [int(re.search(r"pieces=(\d+)", l).group(1)) for l in fe.get("wreck", [])]
            need(len(wr) == 1 and wr[0] > 0, "%s's shot cell: wreck %s, want one with pieces" % (kind, wr))
            need("sink" in fe and "gone" in fe and "damaged" not in fe,
                 "%s's shot cell's stages %s, want wreck, sink, gone and no damaged" % (kind, sorted(fe)))
        said.append("%s: %d civilians%s" % (name, len(types), " and %s" % "/".join(fences) if fences else ""))
    # (6) THE OIL PUMP NODS ON THE CARTRIDGE'S CLIP WHILE HELD, AND STRUGGLES IN ITS OWN LOOP
    # ONCE DAMAGED: three ticks apart the intact frame and the clip pieces have moved; the
    # damaged loop's tick runs on from where the switch started it
    def clip(sec):
        c = [re.match(r"^EBLDCLIP\|V19\|key=\d+\|stage=(\w+)\|intact=(-?\d+)\|idle=0\|phase=[0-9.]+\|cut=(-?\d+)\|start=(-?\d+)$", l)
             for l in sec]
        c = [m for m in c if m]
        mv = {}
        for l in sec:
            m = re.match(r"^EBLDMOVE\|V19\|key=\d+\|piece=(\w+)\|kind=clip\|deg=([0-9.]+)\|moved=([0-9.]+)", l)
            if m:
                mv[m.group(1)] = (float(m.group(2)), float(m.group(3)))
        return (c[0] if c else None), mv
    pa, ma = clip(section(A, "pump 10 a"))
    pb, mb = clip(section(A, "pump 10 b"))
    if need(pa and pb, "no EBLDCLIP line for the pump while held"):
        need(pa.group(1) == "HELD" and int(pb.group(2)) - int(pa.group(2)) in (3, 4),
             "held, the pump's intact frame went %s -> %s in three ticks (%s)"
             % (pa.group(2), pb.group(2), pa.group(1)))
        need(set(ma) >= {"m_beam", "m_crank", "m_pitman"} and ma.get("m_crank") != mb.get("m_crank"),
             "held, the pump's beam, crank and pitman did not move with the clip: %s / %s" % (ma, mb))
    qa, na = clip(section(A, "pump 30 a"))
    qb, nb = clip(section(A, "pump 30 b"))
    if need(qa and qb, "no EBLDCLIP line for the pump once damaged"):
        need(qa.group(1) == "DAMAGED" and int(qb.group(3)) - int(qa.group(3)) in (3, 4)
             and qa.group(4) == qb.group(4) and int(qa.group(4)) >= 0,
             "damaged, the pump's loop went %s -> %s from %s/%s" % (qa.group(3), qb.group(3),
                                                                   qa.group(4), qb.group(4)))
        need(na.get("m_crank") != nb.get("m_crank"), "damaged, the pump's crank stood still")
    # (7) --noebld AND CLASSIC: nothing of it, and the old shatter has the civilians back
    for nm, L in (("--noebld", N), ("Classic", K)):
        need(L and not any("read at first sight" in l for l in L), "%s: a civilian was read" % nm)
        hh = [l for l in L if l.startswith("EBLD|loaded=")]
        need(hh and all("|on=0|" in l for l in hh), "%s: the system was on" % nm)
        need(not any(re.match(r"^EBLDOBJ\|V\d\d\|", l) for l in L), "%s: a civilian has an entry" % nm)
        need(sum(1 for l in L if re.match(r"^SHATTER\|V\d\d\|", l)) >= 15,
             "%s: the old shatter did not take the civilians" % nm)
    # (8) TWO RUNS ARE ONE RUN
    ours = ("EBLD", "EBLDOBJ|", "EBLDCLIP|", "EBLDMOVE|", "EBLDEMIT|")
    # Preparation telemetry includes nondeterministic wall-clock durations and has
    # independent performance tests. Retain every geometry/gameplay EBLD family here.
    la = [l for l in A if l.startswith(ours) and not l.startswith("EBLDPREPARE|")]
    lb = [l for l in A2 if l.startswith(ours) and not l.startswith("EBLDPREPARE|")]
    need(la == lb and la, "the two Enhanced runs printed different EBLD lines")
    same, tot = 0, 0
    for nm in sorted(os.listdir(logs)):
        if nm.startswith("g308_") and nm.endswith(".png"):
            o = os.path.join(logs, "g308r_" + nm[5:])
            if os.path.exists(o):
                tot += 1
                same += open(os.path.join(logs, nm), "rb").read() == open(o, "rb").read()
    need(tot >= 20 and same == tot, "only %d of %d shot pairs of the two runs are identical" % (same, tot))
    if why:
        more = " (and %d more)" % (len(why) - 6) if len(why) > 6 else ""
        return "0 " + "; ".join(why[:6]) + more
    return ("1 %s, every one read at first sight as a version 6 model, drawn whole, worn down "
            "hit by hit, broken at half strength and blown into its shards; the eight that stand "
            "on part of their footprint blow up and scorch on their blocked cells; the oil pump "
            "nods on the cartridge's clip (frame %s to %s) and struggles in its own loop once "
            "damaged (tick %s to %s from %s); the fences' shot cells blown apart; nothing of it "
            "under --noebld or Classic; %d shots byte-identical across two runs"
            % ("; ".join(said), pa.group(2), pb.group(2), qa.group(3), qb.group(3), qa.group(4), tot))



if __name__ == "__main__":
    sys.exit(main())
