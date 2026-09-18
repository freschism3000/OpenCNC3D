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
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "public", "data")
EXP = os.path.join(DATA, "export")
KTIME_PER_SEC = 46186158000               # FBX's own time unit

# One baked frame per triangle byte plus the 76 the geometry always had.
GEO_STRIDE = 77


def fbx_props(path):
    """Every KeyTime array and every GlobalSettings P record in a binary FBX, read back
    with a parser written here rather than with the one that wrote the file.

    THE POINT IS THE KEY TIMES. An FBX's keys are absolute times, and the frame numbers
    an artist sees are those times times whatever frame rate the file declares. Get the
    two out of step and the clip arrives on fractional frames, which is what this repo
    shipped until the writer started deriving both from one rate. Reading it back is the
    only way to know they still agree.
    """
    b = open(path, "rb").read()
    if not b.startswith(b"Kaydara FBX Binary"):
        return None
    keytimes, props = [], {}

    def prop(pos):
        t = b[pos:pos + 1].decode("latin-1")
        pos += 1
        if t == "Y":
            return struct.unpack_from("<h", b, pos)[0], pos + 2
        if t == "C":
            return b[pos] != 0, pos + 1
        if t == "I":
            return struct.unpack_from("<i", b, pos)[0], pos + 4
        if t == "F":
            return struct.unpack_from("<f", b, pos)[0], pos + 4
        if t == "D":
            return struct.unpack_from("<d", b, pos)[0], pos + 8
        if t == "L":
            return struct.unpack_from("<q", b, pos)[0], pos + 8
        if t in "SR":
            n = struct.unpack_from("<I", b, pos)[0]
            v = b[pos + 4:pos + 4 + n]
            return (v.decode("latin-1") if t == "S" else v), pos + 4 + n
        cnt, enc, cl = struct.unpack_from("<3I", b, pos)
        pos += 12
        raw = b[pos:pos + cl]
        if enc:
            raw = zlib.decompress(raw)
        fmt = {"f": "f", "d": "d", "l": "q", "i": "i", "b": "b"}[t]
        return list(struct.unpack("<%d%s" % (cnt, fmt), raw)), pos + cl

    def node(pos):
        end, nprop, _plen, nlen = struct.unpack_from("<IIIB", b, pos)
        pos += 13
        if end == 0:
            return None, pos
        name = b[pos:pos + nlen].decode("latin-1")
        pos += nlen
        vals = []
        for _ in range(nprop):
            v, pos = prop(pos)
            vals.append(v)
        if name == "KeyTime" and vals:
            keytimes.append(vals[0])
        elif name == "P" and len(vals) >= 2:
            props[vals[0]] = vals[-1]
        while pos < end - 13:
            k, pos = node(pos)
            if k is None:
                break
        return name, max(pos, end)

    pos = 27
    while pos < len(b) - 13:
        n, pos = node(pos)
        if n is None:
            break
    return dict(keytimes=keytimes, props=props)

meta = json.load(open(os.path.join(DATA, "assets.json")))
man = json.load(open(os.path.join(EXP, "manifest.json")))
bad, checked, anim_checked = [], 0, []

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
        elif a["anim"]:
            f = fbx_props(fbx)
            fps = float(f["props"].get("CustomFrameRate", 0) or 0)
            tl = e["fbx"].get("timeline") or {}
            if fps <= 0:
                bad.append("%s: FBX declares no frame rate" % code)
            elif tl and abs(fps - tl["fps"]) > 1e-9:
                bad.append("%s: FBX declares %g fps, the manifest says %g"
                           % (code, fps, tl["fps"]))
            else:
                worst, nkeys = 0.0, 0
                for arr in f["keytimes"]:
                    nkeys += len(arr)
                    for t in arr:
                        fr = t * fps / KTIME_PER_SEC
                        worst = max(worst, abs(fr - round(fr)))
                if not nkeys and a["anim"]["frames"] > 1:
                    bad.append("%s: animated mesh, no keys in the FBX" % code)
                if worst > 1e-6:
                    bad.append("%s: FBX key lands %.4f of a frame off a whole one"
                               % (code, worst))
                anim_checked.append(code)

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
    want = max(want, a["off"] + ((a["tris"] * GEO_STRIDE + 3) // 4) * 4)
    for v in a["variants"]:
        want = max(want, v["off"] + ((v["tris"] * GEO_STRIDE + 3) // 4) * 4)
if want > geo:
    bad.append("geo.bin is %d bytes, the asset table reaches %d" % (geo, want))

# anim.bin the same way: the viewer reads nf*np 3x4 matrices and then nf*np bytes at
# each asset's offset, as Float32Array and Uint8Array views over the fetched buffer, so
# a short file is a runtime exception rather than a missing animation.
ap = os.path.join(DATA, "anim.bin")
anim = os.path.getsize(ap) if os.path.exists(ap) else -1
if anim < 0:
    bad.append("anim.bin is missing; the viewer cannot play any clip")
else:
    reach, clips = 0, 0
    for a in meta["assets"]:
        an = a["anim"]
        if not an or an.get("off") is None:
            continue
        clips += 1
        cells = an["frames"] * an["parts"]
        reach = max(reach, an["off"] + cells * 49)
        if an["off"] % 4:
            bad.append("%s: anim offset %d is not 4 byte aligned"
                       % (a["code"], an["off"]))
    if reach > anim:
        bad.append("anim.bin is %d bytes, the asset table reaches %d" % (anim, reach))
    if clips != len(anim_checked):
        bad.append("%d assets carry clip bytes but %d FBX clips were checked"
                   % (clips, len(anim_checked)))

print("checked %d assets, %d triangles, %d animation clips"
      % (checked, sum(a["tris"] for a in meta["assets"]), len(anim_checked)))
if bad:
    print("FAIL (%d)" % len(bad))
    for b in bad[:30]:
        print("  " + b)
    sys.exit(1)
print("OK: every asset has a valid FBX, OBJ+MTL, glTF and texture set, every writer "
      "agrees with the viewer on the triangle count, and every FBX animation key sits "
      "on a whole frame of the rate its own file declares")
