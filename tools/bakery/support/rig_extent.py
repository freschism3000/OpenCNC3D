#!/usr/bin/env python3
"""Per-frame vertical extent of the MCV rig's two subtrees, in CELLS above ground.

Reads the SHIPPED PACK (not the extractor JSON) so it measures exactly what the
renderer draws, and applies the same v' = M.v + t the renderer applies.
"""
import os
import struct
import sys

_ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
sys.path.insert(0, os.path.join(_ROOT, "tools", "bakery"))
import packinspect as PI

MODEL_SCALE = 1.0 / 1024.0
PACK = sys.argv[1] if len(sys.argv) > 1 else \
    os.path.join(_ROOT, "playable", "SCG01EA.pack")
p = PI.read(PACK)
m = next(mm for mm in p["meshes"] if mm["name"] == "dl_01356F0")
nparts = len(m["parts"])
nf = m["anim"]["frames"]
mat = struct.unpack("<%df" % (nf * nparts * 12), m["anim"]["mat"])

TRISZ = 4 + 2 + 3 * (5 * 4 + 4)
print("mesh dl_01356F0: %d parts, %d tris, %d anim frames" % (nparts, m["ntris"], nf))


def tri_verts(i):
    o = i * TRISZ + 6
    out = []
    for k in range(3):
        x, y, z = struct.unpack_from("<fff", m["tris"], o + k * 24)
        out.append((x, y, z))
    return out


pv = []
for (tri0, ntris, role, pivot) in m["parts"]:
    vs = []
    for t in range(tri0, tri0 + ntris):
        vs += tri_verts(t)
    pv.append(vs)

HULL = [1, 2, 3, 4, 5]                        # subtree 0x801B9060, the MCV body
PAD = [0] + list(range(6, 21))                # root + subtree 0x801BA2FC, the yard


def top(f, group):
    hi = None
    for pi in group:
        o = (f * nparts + pi) * 12
        for (x, y, z) in pv[pi]:
            wy = mat[o + 4] * x + mat[o + 5] * y + mat[o + 6] * z + mat[o + 7]
            hi = wy if hi is None else max(hi, wy)
    return (hi or 0.0) * MODEL_SCALE


print("frame   HULL top   PAD top   (cells above the model's ground plane)")
blind = []
for f in range(nf):
    hh, ph = top(f, HULL), top(f, PAD)
    if max(hh, ph) < 0.002:
        blind.append(f)
    if f % 2 == 0:
        print("  %3d    %+7.3f   %+7.3f%s" % (f, hh, ph,
              "   <-- nothing above ground" if max(hh, ph) < 0.002 else ""))
print()
print("frames with NOTHING above the ground plane: %d  %s"
      % (len(blind), ("%d..%d" % (blind[0], blind[-1])) if blind else "-"))
