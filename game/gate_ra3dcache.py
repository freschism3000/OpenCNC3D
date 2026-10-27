#!/usr/bin/env python3
"""G334: Red Alert's ground kept on the GPU (game/ra3d_terrain.h, its cache).

    python3 gate_ra3dcache.py check DIR

DIR holds the runs gates.sh made (logs and shots, names below). Prints one line,
"1 <message>" or "0 <what failed>", and exits 0 either way: the shell reads the verdict.

What each run is for (all hidden, tick 0 for every compared picture: the Red Alert engine
is not run-to-run identical after that, and its ore is left out for the same reason):
  a.log a1..a6.png   Allied 1 (snow) in Enhanced with the shroud, from the buffers: the
                     start view, the shroud's light switched off and on, the synthetic
                     dark pass, the sea switched off, another view zoomed out
  b.log b1..b6.png   the same with OPENCNC3D_TERRAIN_CACHE=0: the per-frame loops
  c.log c1..c3.png   Allied 5 (temperate, its coast) without the shroud, from the buffers,
                     then on the 1995 set switched to at run time (the sheet is rebuilt)
  d.log d1..d3.png   the same through the per-frame loops
  e.log              Allied 1 played: a Ranger driven into the shroud, frames between the
                     ticks, the screen revealed and covered again, with the instrument
                     OPENCNC3D_RA3DCACHE_VERIFY=1 comparing every drawn record with the
                     per-frame computation, bit for bit, from what the GPU holds
"""
import os
import re
import sys


def text(path):
    try:
        return open(path, errors="replace").read()
    except OSError:
        return ""


def same(pa, pb):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(pa).convert("RGB")).astype(int)
    b = np.asarray(Image.open(pb).convert("RGB")).astype(int)
    if a.shape != b.shape:
        return -1
    return int((abs(a - b).max(axis=2) > 0).sum())


def check(d):
    fails, claims = [], []
    j = lambda n: os.path.join(d, n)
    a, b, c, dd, e = (text(j(n + ".log")) for n in "abcde")

    # 1. the buffers were built where they should be and not where they were switched off
    built = re.findall(r"GROUND\|ra\|cache\|SCG01EA\|cells=(\d+)\|floor=(\d+)\|strips=(\d+)\+(\d+)", a)
    if not built or int(built[0][0]) < 1000 or int(built[0][1]) == 0 or int(built[0][2]) == 0:
        fails.append("Allied 1's ground, floor and strips were not built into buffers")
    elif "GROUND|ra|cache|" in b or "GROUND|ra|cache|" in dd:
        fails.append("OPENCNC3D_TERRAIN_CACHE=0 still built the buffers")
    elif len(re.findall(r"GROUND\|ra\|cache\|SCG05EA\|", c)) < 2:
        fails.append("switching Allied 5 to the 1995 set did not rebuild the buffers with the sheet")
    else:
        claims.append(f"Allied 1 kept as {built[0][0]} cells, {built[0][1]} floor cells and "
                      f"{int(built[0][2]) + int(built[0][3])} strips; rebuilt with the sheet")

    # 2. the buffers draw the per-frame loops' pictures, byte for byte
    pairs = [(f"a{k}.png", f"b{k}.png") for k in range(1, 7)] + [(f"c{k}.png", f"d{k}.png") for k in range(1, 4)]
    bad = []
    for pa, pb in pairs:
        if not (os.path.exists(j(pa)) and os.path.exists(j(pb))):
            bad.append(f"{pa} missing")
            continue
        n = same(j(pa), j(pb))
        if n:
            bad.append(f"{pa} {n} px")
    if bad:
        fails.append("the buffers' pictures differ from the loops': " + ", ".join(bad))
    else:
        claims.append(f"{len(pairs)} pictures identical to the per-frame loops")

    # 3. ...and the pictures compared are not all one picture: the shroud's light, the dark
    #    pass, the sea and the set each moved it, and the light came back exactly
    if not bad:
        steps = [("a1.png", "a2.png", "the shroud's light"), ("a3.png", "a4.png", "the dark pass"),
                 ("c2.png", "c3.png", "the 1995 set")]
        flat = [w for pa, pb, w in steps if same(j(pa), j(pb)) < 2000]
        if flat:
            fails.append("these steps did not change the picture, so identity proves nothing: " + ", ".join(flat))
        elif same(j("a1.png"), j("a3.png")):
            fails.append("the shroud's light switched off and on did not come back to the same picture")
        else:
            claims.append("the shroud, the dark pass and the set each changed it")

    # 4. the instrument, over a played stretch with the shroud moving
    v = re.findall(r"GROUND\|ra\|verify\|SCG01EA\|cache=(\d)\|records=(\d+)\|bad=(\d+)\|"
                   r"recoloured=(\d+)\|uploads=(\d+)", e)
    stat = re.findall(r"SHROUDSTAT\|[^\n]*clear=(\d+)", e)
    if "GROUND|ra|verify|MISMATCH" in e or (v and int(v[-1][2])):
        m = re.findall(r"GROUND\|ra\|verify\|MISMATCH\|[^\n]*", e)
        fails.append(f"the instrument found stale records ({v[-1][2] if v else '?'}): {m[:2]}")
    elif len(v) < 20 or int(v[-1][1]) < 10000 or any(x[0] != "1" for x in v):
        fails.append(f"the instrument checked too little ({len(v)} frames, "
                     f"{v[-1][1] if v else 0} records)")
    elif len(stat) < 2 or int(stat[-1]) <= int(stat[0]):
        fails.append("the Ranger did not uncover any ground, so the shroud never moved")
    else:
        later = [int(x[3]) for x in v[1:]]
        quiet = sum(1 for x in v[1:] if int(x[4]) == 0)
        if not any(later):
            fails.append("no frame after the first recoloured a cell although the shroud moved")
        elif quiet < len(v[1:]) // 2:
            fails.append(f"only {quiet} of {len(v) - 1} frames sent nothing: the light is not being kept")
        else:
            claims.append(f"{v[-1][1]} records checked against the per-frame computation over {len(v)} "
                          f"frames, none stale; {sum(1 for x in later if x)} frames recoloured cells "
                          f"as the shroud moved, {quiet} sent nothing")
    return fails, claims


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "check":
        print("0 usage: gate_ra3dcache.py check DIR")
        sys.exit(0)
    fails, claims = check(sys.argv[2])
    print(("0 " + "; ".join(fails)) if fails else ("1 Red Alert's ground from the GPU: " + "; ".join(claims)))
