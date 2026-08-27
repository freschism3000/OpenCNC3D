#!/usr/bin/env python3
"""Proof that lifting objgraph2.DEPTH_CAP from 4 to 24 changes nothing that ships.

The scene-graph walker used to hard-code a recursion cap of 4. That is not a
cartridge fact, it is our own runaway guard, and it silently truncated model slot 18
(the MCV deploy rig), whose geometry-and-track-bearing nodes 0x801B9210 / 0x801B9418
sit at depth 5 and 0x801B9134 at depth 6.

This script walks every unit_models.json type that has a mesh under BOTH caps and
compares the thing the baker actually consumes: the ordered list of per-part display
list ROM offsets and mount translations. Exit 0 means the lift is inert for every
shipped type; exit 1 names the ones that moved.

    python3 tools/bakery/support/depth_cap_check.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BAK = os.path.dirname(HERE)
sys.path.insert(0, BAK)
import objgraph2 as O2  # noqa: E402

um = json.load(open(os.path.join(HERE, "unit_models.json")))
types = [k for k in sorted(um) if k != "_meta" and um[k] and um[k].get("mesh")]


def snapshot():
    res = {}
    for k in types:
        e = um[k]
        node = e.get("node_ram")
        if node is not None:
            g = O2.parts_of_node(int(node, 16))
        elif e.get("model_index") is not None:
            g = O2.parts(e["model_index"])
        else:
            g = []
        g = [p for p in g if p["gfx_rom"] is not None]
        res[k] = ([p["gfx_rom"] for p in g],
                  [tuple(round(x, 4) for x in p["t"]) for p in g])
    return res


def main():
    print("unit_models.json types with a mesh: %d" % len(types))
    O2.DEPTH_CAP = 4
    before = snapshot()
    print("DEPTH_CAP  4 : %d parts" % sum(len(v[0]) for v in before.values()))
    O2.DEPTH_CAP = 24
    after = snapshot()
    print("DEPTH_CAP 24 : %d parts" % sum(len(v[0]) for v in after.values()))

    # MCVANIM is the type the lift EXISTS for, so it is expected to change and is not
    # counted as blast radius. Everything else must be untouched.
    EXPECTED = {"MCVANIM"}
    changed = [k for k in types if before[k] != after[k]]
    for k in changed:
        print("  %s %s: %d -> %d parts"
              % ("EXPECTED" if k in EXPECTED else "CHANGED  ", k,
                 len(before[k][0]), len(after[k][0])))
    unexpected = [k for k in changed if k not in EXPECTED]
    print("types whose part list changed: %d (%d unexpected)"
          % (len(changed), len(unexpected)))
    changed = unexpected

    # And the thing the lift is FOR: slot 18 must gain its deep nodes.
    O2.DEPTH_CAP = 4
    s18_shallow = [p for p in O2.parts(18) if p["gfx_rom"] is not None]
    O2.DEPTH_CAP = 24
    s18_deep = [p for p in O2.parts(18) if p["gfx_rom"] is not None]
    print("slot 18 (MCV deploy rig) geometry parts: %d at cap 4, %d at cap 24"
          % (len(s18_shallow), len(s18_deep)))
    gained = [p for p in s18_deep if p["depth"] > 4]
    for p in gained:
        print("   depth %d node %08X dl %07X" % (p["depth"], p["node"], p["gfx_rom"]))
    ok = (not changed) and len(s18_deep) > len(s18_shallow)
    print("VERDICT: %s" % ("inert for shipped types, and slot 18 is complete"
                           if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
