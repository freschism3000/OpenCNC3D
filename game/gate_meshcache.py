"""The batch cache keeps remastered buildings near the shroud and with waving cloth.

usage: gate_meshcache.py script DIR          writes the runs' scripts into DIR
       gate_meshcache.py check DIR           prints "1 message" or "0 reasons"

Four runs measure the cache in steady state (MESHPERF, read before and after ten drawn
frames of a still scene) on two bases the shroud touches:

  rim    SCG05EA, the Nod base at 11,29, beside the always-shrouded rim of the map
  base   SCG08EA, GDI's own base at the start, ringed by unexplored ground, its buildings
         damaged and its flags waving

and two more draw the same ten frames of the base (ticks, a frame with the shroud
reclassified, the shroud lifted and put back, a zoom) once through the cache and once with
it off (OPENCNC3D_MESH_GPU=0, every vertex computed and sent as it always was).

Claims:
  (1) THE CACHE HOLDS THEM: at most 250 batch uploads a frame in each scene, at least 1500
      cache hits in the reference arm with generated empty-pass census disabled.
      The production arm keeps that census enabled: its useful draw/cull and vertex
      counts and pixels must match, with no more uploads or misses. Both arms must
      retain a bounded, stable, live cache without eviction or fallback. Before, the shroud made every building piece near it uncacheable and a
      waving flag made its whole building so, and every pass recomputed and re-sent them:
      672 and 941 uploads a frame.
  (2) THE SAME PICTURE: every one of the ten frames drawn through the cache is the frame
      drawn without it, to within 16 pixels each off by at most 3 levels. (The cache has
      always differed from the uncached path by one pixel, two levels, in the zoomed frame.)
"""
import os
import re
import sys

import numpy as np
from PIL import Image

SCENES = {"rim": ("SCG05EA", "11 29"), "base": ("SCG08EA", "47 48")}
SEQ = ["tick 2", "tick 2", "shrouddarktest", "tick 1", "reveal 1", "reveal 0", "tick 7",
       "zoom max", "tick 3"]


def script(d):
    os.makedirs(d, exist_ok=True)
    os.makedirs(os.path.join(d, "saves"), exist_ok=True)
    for tag, (scen, cam) in SCENES.items():
        for suffix in ("", "_reference"):
            name = tag + suffix
            with open(os.path.join(d, "%s.txt" % name), "w") as f:
                f.write("tick 60\ncam %s\nframebench 10\nmeshperfdump\nframebench 10\nmeshperfdump\nshot %s/%s.png\nquit\n" % (cam, d, name))
    for mode in ("cached", "uncached"):
        with open(os.path.join(d, "seq_%s.txt" % mode), "w") as f:
            f.write("tick 60\ncam %s\n" % SCENES["base"][1])
            f.write("shot %s/%s_00.png\n" % (d, mode))
            for i, step in enumerate(SEQ):
                f.write("%s\nshot %s/%s_%02d.png\n" % (step, d, mode, i + 1))
            f.write("quit\n")


def measured(d, name, bad):
    path = os.path.join(d, name + ".log")
    if not os.path.exists(path):
        bad.append(name + ": missing measurement log")
        return None
    text = open(path, errors="replace").read()
    if not re.search(r"^SCRIPT\|end .+: \d+ lines, 0 failures$", text, re.M):
        bad.append(name + ": script did not complete without failures")
    def rows(prefix):
        return [dict((k, int(v)) for k, v in re.findall(r"(\w+)=(\d+)", line))
                for line in text.splitlines() if line.startswith(prefix + "|")]
    bench = rows("FRAMEBENCH")
    if len(bench) != 2 or any(row.get("n") != 10 for row in bench):
        bad.append(name + ": expected two ten-frame measurement windows")
    groups = {prefix: rows(prefix) for prefix in
              ("MESHPERF", "MESHCACHE", "MESHPOOL", "MESHGEOMETRY")}
    required = {"MESHPERF": ("hits", "uploads", "draws", "culled", "fragment_ranges", "imm_verts", "entries", "bytes"),
                "MESHCACHE": ("misses", "evictions", "lru", "admission_refused"),
                "MESHPOOL": ("fallbacks", "reserved_bytes", "reserved_stores"),
                "MESHGEOMETRY": ("uploads", "vertices")}
    for prefix, keys in required.items():
        if len(groups[prefix]) != 2 or any(any(key not in row for key in keys) for row in groups[prefix]):
            bad.append(name + ": missing " + prefix + " measurement fields")
            return None
    delta = {prefix: {key: values[1][key] - values[0][key] for key in required[prefix]}
             for prefix, values in groups.items()}
    for prefix, fields in delta.items():
        gauges = {"entries", "bytes", "lru", "reserved_bytes", "reserved_stores"}
        if any(value < 0 for key, value in fields.items() if key not in gauges):
            bad.append(name + ": " + prefix + " counters went backwards")
    a, b = groups["MESHPERF"]
    if a["entries"] <= 0 or a["bytes"] <= 0 or a["entries"] != b["entries"] or a["bytes"] != b["bytes"]:
        bad.append(name + ": persistent cache is empty or did not retain the warm working set")
    if b["entries"] > 8192 or b["bytes"] > 128 * 1024 * 1024:
        bad.append(name + ": persistent cache exceeded its entry or byte bound")
    if delta["MESHPERF"]["hits"] <= 0:
        bad.append(name + ": the warm persistent cache served no hits")
    if delta["MESHPERF"]["uploads"] > 2500:
        bad.append("%s: %.0f batch uploads a frame (want 250 or fewer)" % (name, delta["MESHPERF"]["uploads"] / 10.0))
    if delta["MESHPERF"]["imm_verts"] != 0:
        bad.append(name + ": immediate-mode mesh vertices bypassed the cache")
    for row in groups["MESHCACHE"]:
        if row["lru"] != b["entries"] or row["evictions"] or row["admission_refused"]:
            bad.append(name + ": cache evicted/refused the fixture or lost LRU accounting")
            break
    for row in groups["MESHPOOL"]:
        if row["fallbacks"] or row["reserved_bytes"] > 64 * 1024 * 1024 or row["reserved_stores"] > 2048:
            bad.append(name + ": frame VBO pool fell back or exceeded its bounds")
            break
    return delta


def check(d):
    bad, notes = [], []
    for tag in SCENES:
        current = measured(d, tag, bad)
        reference = measured(d, tag + "_reference", bad)
        if current is None or reference is None:
            continue
        hits = reference["MESHPERF"]["hits"] / 10.0
        if hits < 1500:
            bad.append("%s reference: %.0f cache hits a frame (want 1500 or more)" % (tag, hits))
        for field in ("draws", "culled", "fragment_ranges"):
            if current["MESHPERF"][field] != reference["MESHPERF"][field]:
                bad.append(tag + ": census changed submitted geometry (" + field + ")")
        if current["MESHPERF"]["draws"] <= 0:
            bad.append(tag + ": no mesh geometry was drawn")
        if current["MESHGEOMETRY"]["vertices"] != reference["MESHGEOMETRY"]["vertices"]:
            bad.append(tag + ": census changed resolved geometry vertices")
        for prefix, field in (("MESHPERF", "uploads"), ("MESHGEOMETRY", "uploads"), ("MESHCACHE", "misses")):
            if current[prefix][field] > reference[prefix][field]:
                bad.append(tag + ": census increased warm " + prefix + " " + field)
        pa, pb = os.path.join(d, tag + ".png"), os.path.join(d, tag + "_reference.png")
        if not (os.path.exists(pa) and os.path.exists(pb)):
            bad.append(tag + ": census comparison frame was not shot")
        elif not np.array_equal(np.asarray(Image.open(pa).convert("RGB")), np.asarray(Image.open(pb).convert("RGB"))):
            bad.append(tag + ": census changed pixels against its cache-on reference")
        notes.append("%s %.0f uploads and %.0f useful hits a frame; reference %.0f hits, same geometry/pixels" %
                     (tag, current["MESHPERF"]["uploads"] / 10.0, current["MESHPERF"]["hits"] / 10.0, hits))
    worst = (0, 0)
    for i in range(len(SEQ) + 1):
        pa, pb = os.path.join(d, "cached_%02d.png" % i), os.path.join(d, "uncached_%02d.png" % i)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            bad.append("frame %d was not shot" % i)
            continue
        x = np.asarray(Image.open(pa).convert("RGB")).astype(np.int32)
        y = np.asarray(Image.open(pb).convert("RGB")).astype(np.int32)
        diff = np.abs(x - y).max(2)
        n, m = int((diff > 0).sum()), int(diff.max())
        worst = max(worst, (n, m))
        if n > 16 or m > 3:
            bad.append("frame %d: %d pixels differ from the uncached drawing, by up to %d levels" % (i, n, m))
    notes.append("%d frames as drawn without the cache (worst %d pixels by %d)" % (len(SEQ) + 1, worst[0], worst[1]))
    return ("0 " + "; ".join(bad)) if bad else ("1 " + "; ".join(notes))


if __name__ == "__main__":
    if sys.argv[1] == "script":
        script(sys.argv[2])
    else:
        print(check(sys.argv[2]))
