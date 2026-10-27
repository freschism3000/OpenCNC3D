"""Every model goes through the batch, and every posed soldier is drawn from the card.

usage: gate_meshall.py script DIR          writes the runs' scripts into DIR
       gate_meshall.py check DIR           prints "1 message" or "0 reasons"

Models. Two runs measure SCG08EA's base, GDI's own at the start with its sandbag walls,
vehicles and trees, in steady state (MESHPERF, read before and after ten drawn frames):
once as shipped and once with the batch held to the remastered buildings, the rule before
this change (OPENCNC3D_MESH_ALL=0). Two more draw ten frames of it (ticks, a reclassified
shroud, the shroud lifted and put back, a zoom) once each way.

Soldiers. The "dead" example map (USER93: tanks and riflemen against twenty Nod soldiers)
after its fight, with bodies whole and torn on the ground, drawn once as shipped and once
with every pose drawn from system memory (OPENCNC3D_POSE_GPU=0) and every cached skin
checked against a fresh one (OPENCNC3D_POSECHECK=1).

Claims:
  (1) MODELS ARE NOT SENT A VERTEX AT A TIME: at most a third of the vertices the
      buildings-only rule sends one call at a time, which must itself be at least 20,000
      a frame (else the scene no longer has the walls and vehicles this is about); and at
      most 250 batch uploads a frame, so the vehicles parked in the base are kept rather
      than rebuilt every frame.
  (2) THE SAME PICTURE: each of the ten frames as the buildings-only rule draws it, to
      within 16 pixels each off by at most 3 levels.
  (3) EVERY POSED SOLDIER IS DRAWN FROM THE CARD: every draw a pose served (skins plus
      hits) read it from the card, with one upload per pose, at least ten bodies on the
      ground, and torn ones among them, which now go through the same pose cache.
  (4) THE SKINS THE CACHE HANDS BACK ARE THE SKINS (POSECHECK: none differed, torn
      bodies included), and the soldiers drawn from the card are the soldiers drawn from
      system memory, to within 64 pixels each off by at most 12 levels (macOS fetches a
      draw wholly from buffers by another route, which moves a few edge pixels).
"""
import os
import re
import sys

import numpy as np
from PIL import Image

CAM = "47 48"
SEQ = ["tick 2", "tick 2", "shrouddarktest", "tick 1", "reveal 1", "reveal 0", "tick 7",
       "zoom max", "tick 3"]


def script(d):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "base.txt"), "w") as f:
        f.write("tick 60\ncam %s\nframebench 10\nmeshperfdump\nframebench 10\nmeshperfdump\nquit\n" % CAM)
    for mode in ("all", "bld"):
        with open(os.path.join(d, "seq_%s.txt" % mode), "w") as f:
            f.write("tick 60\ncam %s\n" % CAM)
            f.write("shot %s/%s_00.png\n" % (d, mode))
            for i, step in enumerate(SEQ):
                f.write("%s\nshot %s/%s_%02d.png\n" % (step, d, mode, i + 1))
            f.write("quit\n")
    for mode in ("gpu", "cpu"):
        with open(os.path.join(d, "bodies_%s.txt" % mode), "w") as f:
            f.write("cam 31 30\ntick 300\ncam 31 29\nshot %s/bodies_%s.png\nm3dump\nquit\n" % (d, mode))


def rows(path, tag):
    return [dict(re.findall(r"(\w+)=(\d+)", l)) for l in open(path, errors="replace")
            if l.startswith(tag + "|")]


def picture(pa, pb):
    x = np.asarray(Image.open(pa).convert("RGB")).astype(np.int32)
    y = np.asarray(Image.open(pb).convert("RGB")).astype(np.int32)
    diff = np.abs(x - y).max(2)
    return int((diff > 0).sum()), int(diff.max())


def check(d):
    bad, notes = [], []
    per = {}
    for mode in ("all", "bld"):
        r = rows(os.path.join(d, "base_%s.log" % mode), "MESHPERF")
        if len(r) < 2 or "imm_verts" not in r[-1]:
            bad.append("%s: no MESHPERF lines with imm_verts (an older binary, or the run failed)" % mode)
            continue
        a, b = r[-2], r[-1]
        per[mode] = {k: (int(b[k]) - int(a[k])) / 10.0 for k in ("imm_verts", "uploads")}
    if len(per) == 2:
        imm, old, up = per["all"]["imm_verts"], per["bld"]["imm_verts"], per["all"]["uploads"]
        if old < 20000:
            bad.append("the buildings-only rule sends %.0f vertices a frame one at a time (want 20000 or more: "
                       "the scene no longer measures anything)" % old)
        if imm * 3 > old:
            bad.append("%.0f vertices a frame sent one at a time against %.0f under the buildings-only rule "
                       "(want a third or less)" % (imm, old))
        if up > 250:
            bad.append("%.0f batch uploads a frame (want 250 or fewer)" % up)
        notes.append("%.0f vertices a frame sent one at a time (buildings-only rule: %.0f), %.0f uploads"
                     % (imm, old, up))
    worst = (0, 0)
    for i in range(len(SEQ) + 1):
        pa, pb = os.path.join(d, "all_%02d.png" % i), os.path.join(d, "bld_%02d.png" % i)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            bad.append("frame %d was not shot" % i)
            continue
        n, m = picture(pa, pb)
        worst = max(worst, (n, m))
        if n > 16 or m > 3:
            bad.append("frame %d: %d pixels differ from the buildings-only rule, by up to %d levels" % (i, n, m))
    notes.append("%d frames as the buildings-only rule draws them (worst %d pixels by %d)"
                 % (len(SEQ) + 1, worst[0], worst[1]))
    g = rows(os.path.join(d, "bodies_gpu.log"), "M3POSE")
    c = rows(os.path.join(d, "bodies_cpu.log"), "M3POSE")
    corpse = rows(os.path.join(d, "bodies_gpu.log"), "M3CORPSE")
    gibs = rows(os.path.join(d, "bodies_gpu.log"), "INFGIBS")
    if not g or "gpudraws" not in g[-1] or not c or not corpse or not gibs:
        bad.append("bodies: no M3POSE with gpudraws, M3CORPSE or INFGIBS line (an older binary, or the run failed)")
    else:
        g, c = g[-1], c[-1]
        skins, hits, up, gd = (int(g[k]) for k in ("skins", "hits", "uploads", "gpudraws"))
        n, torn = int(corpse[-1]["n"]), int(gibs[-1]["torn"])
        if gd != skins + hits or up != skins or skins == 0:
            bad.append("bodies: %d draws from the card of %d posed (skins %d + hits %d), %d uploads "
                       "(want every draw from the card and one upload a skin)" % (gd, skins + hits, skins, hits, up))
        if n < 10 or torn < 1:
            bad.append("bodies: %d on the ground, %d torn (want 10 or more, and torn ones)" % (n, torn))
        if c.get("check") != "1" or c.get("bad") != "0" or int(c.get("gpudraws", "1")) != 0:
            bad.append("bodies: the checked run reports check=%s bad=%s gpudraws=%s (want 1, 0 and 0)"
                       % (c.get("check"), c.get("bad"), c.get("gpudraws")))
        notes.append("%d bodies (%d torn): %d posed draws all from the card, %d uploads, skins checked bad=%s"
                     % (n, torn, gd, up, c.get("bad")))
    pa, pb = os.path.join(d, "bodies_gpu.png"), os.path.join(d, "bodies_cpu.png")
    if not (os.path.exists(pa) and os.path.exists(pb)):
        bad.append("bodies were not shot")
    else:
        n, m = picture(pa, pb)
        if n > 64 or m > 12:
            bad.append("bodies: %d pixels differ from the system-memory draw, by up to %d levels" % (n, m))
        notes.append("as drawn from system memory to within %d pixels by %d" % (n, m))
    return ("0 " + "; ".join(bad)) if bad else ("1 " + "; ".join(notes))


if __name__ == "__main__":
    if sys.argv[1] == "script":
        script(sys.argv[2])
    else:
        print(check(sys.argv[2]))
