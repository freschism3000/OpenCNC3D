#!/usr/bin/env python3
"""Per-part survey for the turret round: for every model of interest, bake each
scene-graph part's display list, apply the mount transform, and report the
numbers a human needs to name the part (mount height, bbox, elongation, tag).

Output: parts_survey.json + a readable table on stdout.
Mesh frame: x east, y up, z south (the same frame the renderer's draw_mesh uses,
model units, MODEL_SCALE = 1/1024 cells per unit)."""
import json
import sys

import pk4mod
import objgraph2 as O2

B = pk4mod.load()
um = json.load(open("support/unit_models.json"))

WANT = ["MTNK", "LTNK", "HTNK", "JEEP", "BGGY", "MSAM", "TRAN", "BOAT",
        "FACT", "SAM", "PROC", "SILO", "GUN", "HELI", "ORCA", "STNK",
        "FTNK", "TMPL", "APC", "ARTY", "HARV", "ATWR", "GTWR", "OBLI"]


def xform(v, M, t):
    return [v[0] * M[0][0] + v[1] * M[1][0] + v[2] * M[2][0] + t[0],
            v[0] * M[0][1] + v[1] * M[1][1] + v[2] * M[2][1] + t[1],
            v[0] * M[0][2] + v[1] * M[1][2] + v[2] * M[2][2] + t[2]]


def bbox(pts):
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return lo, hi


report = {}
for t in WANT:
    if t not in um or um[t].get("model_index") is None:
        continue
    slot = um[t]["model_index"]
    ps = O2.parts(slot)
    if not ps:
        continue
    bank = B.TexBank()
    entry = []
    for i, p in enumerate(ps):
        if p["gfx_rom"] is None:
            entry.append(dict(idx=i, depth=p["depth"], kind=p["kind"], tris=0,
                              note="no gfx (grouping node)", t=p["t"]))
            continue
        tris = B.bake_mesh(p["gfx_rom"], bank)
        raw = [v[:3] for (_ti, _m, _w, tri) in tris for v in tri]
        posed = [xform(v, p["M"], p["t"]) for v in raw]
        (rl, rh) = bbox(raw) if raw else (None, None)
        (pl, ph) = bbox(posed) if posed else (None, None)
        entry.append(dict(idx=i, depth=p["depth"], kind=p["kind"],
                          gfx_rom="0x%07X" % p["gfx_rom"], from_alt=p.get("from_alt", False),
                          tris=len(tris), t=[round(x, 2) for x in p["t"]],
                          raw_bbox=[[round(x, 1) for x in rl], [round(x, 1) for x in rh]] if raw else None,
                          posed_bbox=[[round(x, 1) for x in pl], [round(x, 1) for x in ph]] if raw else None))
    report[t] = dict(slot=slot, seg=O2.seg_of(O2.node_ptr(slot))[0], parts=entry)

json.dump(report, open("parts_survey.json", "w"), indent=1)

for t, r in report.items():
    print("=== %s  slot %d  (%s)" % (t, r["slot"], r["seg"]))
    for e in r["parts"]:
        if e["tris"] == 0:
            print("   part %d d%d %-7s  %s" % (e["idx"], e["depth"], e["kind"], e.get("note", "")))
            continue
        pl, ph = e["posed_bbox"]
        dims = [round(ph[i] - pl[i], 1) for i in range(3)]
        print("   part %d d%d %-7s %4d tris  mount t=(%7.1f %7.1f %7.1f)%s  posed y=[%6.1f %6.1f]  dims %sx%sx%s"
              % (e["idx"], e["depth"], e["kind"], e["tris"],
                 e["t"][0], e["t"][1], e["t"][2],
                 " ALT" if e.get("from_alt") else "",
                 pl[1], ph[1], dims[0], dims[1], dims[2]))
