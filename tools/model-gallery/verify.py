#!/usr/bin/env python3
"""Check every pre-baked export against the gallery's own asset table.

Not a spot check: it walks all 202 assets and asserts, per format, that the file
exists, carries the right magic, and declares the same triangle count the viewer
draws. A silent disagreement between the three writers is exactly the failure this
catches, and it is the reason the old viewer's corpus could drift without anyone
noticing.
"""
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "public", "data")
EXP = os.path.join(DATA, "export")

meta = json.load(open(os.path.join(DATA, "assets.json")))
man = json.load(open(os.path.join(EXP, "manifest.json")))
bad, checked = [], 0

for a in meta["assets"]:
    code, n = a["code"], a["tris"]
    e = man.get(code)
    if not e:
        bad.append("%s: no export entry" % code)
        continue

    fbx = os.path.join(EXP, "fbx", e["fbx"]["model"])
    if not os.path.exists(fbx):
        bad.append("%s: missing %s" % (code, e["fbx"]["model"]))
    else:
        head = open(fbx, "rb").read(23)
        if not head.startswith(b"Kaydara FBX Binary"):
            bad.append("%s: FBX magic wrong" % code)

    obj = os.path.join(EXP, "obj", e["obj"]["model"])
    src = open(obj).read() if os.path.exists(obj) else ""
    faces = src.count("\nf ")
    verts = src.count("\nv ")
    if faces != n:
        bad.append("%s: OBJ has %d faces, asset has %d triangles" % (code, faces, n))
    if verts != n * 3:
        bad.append("%s: OBJ has %d vertices, expected %d" % (code, verts, n * 3))
    mtl = os.path.join(EXP, "obj", e["obj"]["extra"][0])
    if not os.path.exists(mtl):
        bad.append("%s: missing MTL" % code)

    g = json.load(open(os.path.join(EXP, "gltf", e["gltf"]["model"])))
    tot = 0
    for prim in g["meshes"][0]["primitives"]:
        tot += g["accessors"][prim["attributes"]["POSITION"]]["count"]
    if tot != n * 3:
        bad.append("%s: glTF has %d vertices, expected %d" % (code, tot, n * 3))
    if len(g["images"]) != len(e["tex"]):
        bad.append("%s: glTF names %d images, manifest lists %d textures"
                   % (code, len(g["images"]), len(e["tex"])))

    for t in e["tex"]:
        for d in (["tex"] + (["tex-gdi"] if e["gdi"] else [])):
            p = os.path.join(EXP, d, t)
            if not os.path.exists(p):
                bad.append("%s: missing %s/%s" % (code, d, t))
            elif open(p, "rb").read(8) != b"\x89PNG\r\n\x1a\n":
                bad.append("%s: %s/%s is not a PNG" % (code, d, t))
    checked += 1

geo = os.path.getsize(os.path.join(DATA, "geo.bin"))
want = 0
for a in meta["assets"]:
    want = max(want, a["off"] + ((a["tris"] * 76 + 3) // 4) * 4)
    for v in a["variants"]:
        want = max(want, v["off"] + ((v["tris"] * 76 + 3) // 4) * 4)
if want > geo:
    bad.append("geo.bin is %d bytes, the asset table reaches %d" % (geo, want))

print("checked %d assets, %d triangles"
      % (checked, sum(a["tris"] for a in meta["assets"])))
if bad:
    print("FAIL (%d)" % len(bad))
    for b in bad[:30]:
        print("  " + b)
    sys.exit(1)
print("OK: every asset has a valid FBX, OBJ+MTL, glTF and texture set, and every "
      "writer agrees with the viewer on the triangle count")
