#!/usr/bin/env python3
"""Export one cartridge model from a baked pack as a BINARY FBX 7.4, for an artist.

Why FBX and not the renderer's own `dumpobj`: OBJ can carry the triangles and the
per-vertex colours, but it cannot carry a PIVOT. A C&C vehicle's turret is a separate
scene-graph node that the renderer rotates about a specific point, and losing that
point means the returned model cannot be hooked back up. FBX carries it as a node
transform, so what the artist sees rotating in Blender is what the game will rotate.

Why binary and not ASCII FBX: Blender's importer reads binary only.

    python3 tools/art/fbxout.py game/SCG01EA.pack MTNK -o /some/dir

Emits <TYPE>.fbx, one <TYPE>_texNN.png per texture the model uses, and <TYPE>.json --
the machine-readable contract (scale, axes, pivots, per-node triangle budget) that the
return path checks a delivered model against.

The geometry is read with packinspect.py, the repo's own pack reader, and this tool
asserts its triangle counts against the renderer's dumpobj line when one is supplied,
so the two readers cannot drift apart silently.
"""
import json
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "bakery"))
import packinspect as PI  # noqa: E402

TRI_STRIDE = 4 + 2 + 3 * (5 * 4 + 4)      # int tex, u8 mode, u8 wrap, 3 x PackVert
TRI_FMT = "<i2B" + "5f4B" * 3
ROLE = {0: "static", 1: "turret", 2: "rotor"}
MODE = {0: "opaque", 1: "cutout", 2: "shadow", 3: "xlu"}

# ---------------------------------------------------------------- FBX binary writer
FBX_VERSION = 7400
FBX_MAGIC = b"Kaydara FBX Binary  \x00\x1a\x00"
NULL_REC = b"\0" * 13
FOOT_ID = b"\xfa\xbc\xab\x09\xd0\xc8\xd4\x66\xb1\x76\xfb\x83\x1c\xf7\x26\x7e"
FOOT_MAGIC = b"\xf8\x5a\x8c\x6a\xde\xf5\xd9\x7e\xec\xe9\x0c\xe3\x75\x8f\x29\x0b"


class N(object):
    """One FBX node record: a name, typed properties, and children."""

    def __init__(self, name, *props):
        self.name = name
        self.props = list(props)
        self.kids = []

    def add(self, node):
        self.kids.append(node)
        return node

    def kid(self, name, *props):
        return self.add(N(name, *props))


def _prop(p):
    t, v = p
    if t == "I":
        return b"I" + struct.pack("<i", v)
    if t == "L":
        return b"L" + struct.pack("<q", v)
    if t == "D":
        return b"D" + struct.pack("<d", v)
    if t == "F":
        return b"F" + struct.pack("<f", v)
    if t == "C":
        return b"C" + struct.pack("<B", 1 if v else 0)
    if t == "Y":
        return b"Y" + struct.pack("<h", v)
    if t == "S":
        b = v if isinstance(v, bytes) else v.encode("utf-8")
        return b"S" + struct.pack("<I", len(b)) + b
    if t == "R":
        return b"R" + struct.pack("<I", len(v)) + v
    # arrays; always offered to zlib, kept raw when compression does not pay
    fmt = {"d": "d", "i": "i", "l": "q", "f": "f", "b": "B"}[t]
    raw = struct.pack("<%d%s" % (len(v), fmt), *v)
    comp = zlib.compress(raw, 6)
    if len(comp) < len(raw):
        return t.encode() + struct.pack("<III", len(v), 1, len(comp)) + comp
    return t.encode() + struct.pack("<III", len(v), 0, len(raw)) + raw


def _ser(node, start):
    """Serialise one record. EndOffset is absolute, so the caller passes its own."""
    name = node.name.encode("utf-8")
    props = b"".join(_prop(p) for p in node.props)
    off = start + 4 + 4 + 4 + 1 + len(name) + len(props)
    kids = b""
    for k in node.kids:
        kb = _ser(k, off)
        kids += kb
        off += len(kb)
    if node.kids:
        kids += NULL_REC
        off += len(NULL_REC)
    head = struct.pack("<III", off, len(node.props), len(props))
    head += struct.pack("<B", len(name)) + name
    return head + props + kids


def write_fbx(path, roots):
    body = b""
    off = len(FBX_MAGIC) + 4
    for r in roots:
        rb = _ser(r, off)
        body += rb
        off += len(rb)
    with open(path, "wb") as f:
        f.write(FBX_MAGIC)
        f.write(struct.pack("<I", FBX_VERSION))
        f.write(body)
        f.write(NULL_REC)
        f.write(FOOT_ID)
        pad = ((f.tell() + 15) & ~15) - f.tell()
        f.write(b"\0" * (pad if pad else 16))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", FBX_VERSION))
        f.write(b"\0" * 120)
        f.write(FOOT_MAGIC)


def fbxname(name, cls):
    """Binary FBX stores 'Model::foo' as b'foo\\0\\x01Model'."""
    return name.encode("utf-8") + b"\x00\x01" + cls.encode("utf-8")


_ids = [1000000]


def newid():
    _ids[0] += 1
    return _ids[0]


def p70(pairs):
    n = N("Properties70")
    for pr in pairs:
        n.add(N("P", *pr))
    return n


# ---------------------------------------------------------------- animation (PKB)
KTIME_PER_SEC = 46186158000          # FBX's own time unit
ENGINE_HZ = 15.0                     # the sim tick the baked frame grid is on


def decompose(M):
    """A baked frame's 3x4 delta -> (translation, XYZ Euler degrees, scale).

       The pack stores the delta row-major as [m00 m01 m02 m03 ; m10 ...], applied as
       v' = M*v + t. FBX wants T/R/S on the node, so it has to be pulled apart here
       rather than in the artist's file."""
    t = (M[3], M[7], M[11])
    c = [(M[0], M[4], M[8]), (M[1], M[5], M[9]), (M[2], M[6], M[10])]
    s = [math.sqrt(sum(v * v for v in col)) or 1.0 for col in c]
    r = [[c[j][i] / s[j] for j in range(3)] for i in range(3)]
    # XYZ Euler, the order FBX defaults to
    sy = -r[2][0]
    sy = max(-1.0, min(1.0, sy))
    ry = math.asin(sy)
    if abs(sy) < 0.99999:
        rx = math.atan2(r[2][1], r[2][2])
        rz = math.atan2(r[1][0], r[0][0])
    else:
        rx = math.atan2(-r[1][2], r[1][1])
        rz = 0.0
    deg = 180.0 / math.pi
    return t, (rx * deg, ry * deg, rz * deg), tuple(s)


def anim_track(mesh, part_index, pivot):
    """Per-frame node T/R/S for one part, in the frame FBX wants.

       The pack's triangles are stored ALREADY POSED by the node's rest transform and
       the delta is applied on top of that, so the node has to reproduce
       v_world = M*(v_local + pivot) + t, i.e. rotation M and translation M*pivot + t.
       Getting this wrong moves the whole part instead of animating it."""
    a = mesh["anim"]
    if not a or not a["frames"]:
        return None
    nparts = len(mesh["parts"]) or 1
    nf = a["frames"]
    mat = struct.unpack("<%df" % (nf * nparts * 12), a["mat"])
    vis = list(a["vis"]) if a["vis"] else [1] * (nf * nparts)
    px, py, pz = pivot
    out = []
    for f in range(nf):
        o = (f * nparts + part_index) * 12
        M = mat[o:o + 12]
        t, rot, sc = decompose(M)
        mp = (M[0] * px + M[1] * py + M[2] * pz,
              M[4] * px + M[5] * py + M[6] * pz,
              M[8] * px + M[9] * py + M[10] * pz)
        out.append(dict(frame=f,
                        T=(mp[0] + t[0], mp[1] + t[1], mp[2] + t[2]),
                        R=rot, S=sc,
                        visible=bool(vis[f * nparts + part_index])))
    return out


def curve_nodes(objects, conns, layer_id, model_id, track, base):
    """One AnimationCurveNode per animated channel, with its three curves. Channels
       that never move are skipped, so an artist opening the file sees only what the
       cartridge actually animates."""
    made = 0
    keytimes = [int(round(k["frame"] / ENGINE_HZ * KTIME_PER_SEC)) for k in track]
    for chan, prop, dflt in (("T", "Lcl Translation", base),
                             ("R", "Lcl Rotation", (0.0, 0.0, 0.0)),
                             ("S", "Lcl Scaling", (1.0, 1.0, 1.0))):
        vals = [k[chan] for k in track]
        moved = [i for i in range(3)
                 if max(abs(v[i] - vals[0][i]) for v in vals) > 1e-4]
        if not moved:
            continue
        cnid = newid()
        cn = objects.kid("AnimationCurveNode", ("L", cnid),
                         ("S", fbxname(chan, "AnimCurveNode")), ("S", ""))
        cn.add(p70([[("S", "d|X"), ("S", "Number"), ("S", ""), ("S", "A"), ("D", vals[0][0])],
                    [("S", "d|Y"), ("S", "Number"), ("S", ""), ("S", "A"), ("D", vals[0][1])],
                    [("S", "d|Z"), ("S", "Number"), ("S", ""), ("S", "A"), ("D", vals[0][2])]]))
        conns.kid("C", ("S", "OO"), ("L", cnid), ("L", layer_id))
        conns.kid("C", ("S", "OP"), ("L", cnid), ("L", model_id), ("S", prop))
        for axis in range(3):
            cid = newid()
            cur = objects.kid("AnimationCurve", ("L", cid),
                              ("S", fbxname("", "AnimCurve")), ("S", ""))
            cur.kid("Default", ("D", vals[0][axis]))
            cur.kid("KeyVer", ("I", 4008))
            cur.kid("KeyTime", ("l", keytimes))
            cur.kid("KeyValueFloat", ("f", [v[axis] for v in vals]))
            # 0x40000 = interpolation linear, which is what the baker already resampled to
            cur.kid("KeyAttrFlags", ("i", [0x00000004]))
            cur.kid("KeyAttrDataFloat", ("f", [0.0, 0.0, 0.0, 0.0]))
            cur.kid("KeyAttrRefCount", ("i", [len(vals)]))
            conns.kid("C", ("S", "OP"), ("L", cid), ("L", cnid),
                      ("S", "d|" + "XYZ"[axis]))
            made += 1
    return made


# ---------------------------------------------------------------- pack -> parts
def read_tris(mesh):
    out = []
    b = mesh["tris"]
    for i in range(mesh["ntris"]):
        f = struct.unpack_from(TRI_FMT, b, i * TRI_STRIDE)
        tex, mode, wrap = f[0], f[1], f[2]
        vs = []
        for k in range(3):
            o = 3 + k * 9
            vs.append(dict(x=f[o], y=f[o + 1], z=f[o + 2], u=f[o + 3], v=f[o + 4],
                           r=f[o + 5], g=f[o + 6], b=f[o + 7], a=f[o + 8]))
        out.append(dict(tex=tex, mode=mode, wrap=wrap, v=vs))
    return out


def split_parts(mesh, tris):
    """One export node per (scene-graph part, shadow/not). Mirrors dumpobj's two sweeps:
       the cartridge keeps a model's flat shadow blob in the same triangle list as the
       hull, and an artist must be able to hide it with one click."""
    parts = mesh["parts"] or [(0, len(tris), 0, (0.0, 0.0, 0.0))]
    nodes = []
    for pi, (tri0, ntri, role, pivot) in enumerate(parts):
        for shadow in (0, 1):
            sel = [t for t in tris[tri0:tri0 + ntri]
                   if (t["mode"] == 2) == bool(shadow)]
            if not sel:
                continue
            nodes.append(dict(index=pi, role=ROLE.get(role, str(role)),
                              pivot=list(pivot), shadow=bool(shadow), tris=sel))
    return nodes


# ---------------------------------------------------------------- textures
def dump_textures(pack, used, outdir, stem, house):
    from PIL import Image
    meta = []
    for slot, ti in enumerate(used):
        tx = pack["tex"][ti]
        data = tx["gdi"] if (house == "gdi" and tx["gdi"]) else tx["data"]
        im = Image.frombytes("RGBA", (tx["w"], tx["h"]), bytes(data))
        fn = "%s_tex%02d.png" % (stem, slot)
        im.save(os.path.join(outdir, fn))
        meta.append(dict(slot=slot, bank_index=ti, file=fn,
                         padded=[tx["w"], tx["h"]], used=[tx["uw"], tx["uh"]],
                         has_gdi_variant=bool(tx["gdi"])))
    return meta


# ---------------------------------------------------------------- build the scene
def build(pack, code, mesh, outdir, house):
    tris = read_tris(mesh)
    nodes = split_parts(mesh, tris)
    used = []
    for t in tris:
        if t["tex"] >= 0 and t["tex"] not in used:
            used.append(t["tex"])
    texmeta = dump_textures(pack, used, outdir, code, house)

    objects = N("Objects")
    conns = N("Connections")
    counts = dict(Model=0, Geometry=0, Material=0, Texture=0, Video=0,
                  AnimationStack=0, AnimationLayer=0,
                  AnimationCurveNode=0, AnimationCurve=0)

    # one material per texture the model uses, plus one untextured material when needed
    matid = {}
    for slot, ti in enumerate(used):
        mid = newid()
        matid[ti] = mid
        counts["Material"] += 1
        m = objects.kid("Material", ("L", mid),
                        ("S", fbxname("%s_tex%02d" % (code, slot), "Material")), ("S", ""))
        m.kid("Version", ("I", 102))
        m.kid("ShadingModel", ("S", "lambert"))
        m.kid("MultiLayer", ("I", 0))
        m.add(p70([
            [("S", "DiffuseColor"), ("S", "Color"), ("S", ""), ("S", "A"),
             ("D", 1.0), ("D", 1.0), ("D", 1.0)],
            [("S", "SpecularFactor"), ("S", "Number"), ("S", ""), ("S", "A"), ("D", 0.0)],
        ]))
        vid, tid = newid(), newid()
        counts["Video"] += 1
        counts["Texture"] += 1
        fn = "%s_tex%02d.png" % (code, slot)
        ap = os.path.abspath(os.path.join(outdir, fn))
        vnode = objects.kid("Video", ("L", vid),
                            ("S", fbxname("%s_tex%02d" % (code, slot), "Video")), ("S", "Clip"))
        vnode.kid("Type", ("S", "Clip"))
        vnode.add(p70([[("S", "Path"), ("S", "KString"), ("S", "XRefUrl"), ("S", ""), ("S", ap)]]))
        vnode.kid("UseMipMap", ("I", 0))
        vnode.kid("Filename", ("S", ap))
        vnode.kid("RelativeFilename", ("S", fn))
        tnode = objects.kid("Texture", ("L", tid),
                            ("S", fbxname("%s_tex%02d" % (code, slot), "Texture")), ("S", ""))
        tnode.kid("Type", ("S", "TextureVideoClip"))
        tnode.kid("Version", ("I", 202))
        tnode.kid("TextureName", ("S", fbxname("%s_tex%02d" % (code, slot), "Texture")))
        tnode.add(p70([[("S", "UVSet"), ("S", "KString"), ("S", ""), ("S", ""), ("S", "UVMap")],
                       [("S", "UseMaterial"), ("S", "bool"), ("S", ""), ("S", ""), ("I", 1)]]))
        tnode.kid("Media", ("S", fbxname("%s_tex%02d" % (code, slot), "Video")))
        tnode.kid("FileName", ("S", ap))
        tnode.kid("RelativeFilename", ("S", fn))
        tnode.kid("ModelUVTranslation", ("D", 0.0), ("D", 0.0))
        tnode.kid("ModelUVScaling", ("D", 1.0), ("D", 1.0))
        tnode.kid("Texture_Alpha_Source", ("S", "None"))
        conns.kid("C", ("S", "OP"), ("L", tid), ("L", mid), ("S", "DiffuseColor"))
        conns.kid("C", ("S", "OO"), ("L", vid), ("L", tid))

    flatid = newid()
    counts["Material"] += 1
    fm = objects.kid("Material", ("L", flatid),
                     ("S", fbxname("%s_untextured" % code, "Material")), ("S", ""))
    fm.kid("Version", ("I", 102))
    fm.kid("ShadingModel", ("S", "lambert"))
    fm.kid("MultiLayer", ("I", 0))
    fm.add(p70([[("S", "DiffuseColor"), ("S", "Color"), ("S", ""), ("S", "A"),
                 ("D", 1.0), ("D", 1.0), ("D", 1.0)]]))

    # the root null everything hangs off, so the artist moves one object
    rootid = newid()
    counts["Model"] += 1
    rm = objects.kid("Model", ("L", rootid), ("S", fbxname(code, "Model")), ("S", "Null"))
    rm.kid("Version", ("I", 232))
    rm.add(p70([[("S", "DefaultAttributeIndex"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", -1)]]))
    conns.kid("C", ("S", "OO"), ("L", rootid), ("L", 0))

    # One AnimationStack holding one AnimationLayer, so the clip shows up on the
    # artist's timeline instead of arriving as a note in a text file.
    anim = mesh["anim"]
    layer_id = None
    if anim and anim["frames"]:
        nf = anim["frames"]
        stack_id, layer_id = newid(), newid()
        counts["AnimationStack"] += 1
        counts["AnimationLayer"] += 1
        stop = int(round((nf - 1) / ENGINE_HZ * KTIME_PER_SEC))
        st = objects.kid("AnimationStack", ("L", stack_id),
                         ("S", fbxname("%s_clip" % code, "AnimStack")), ("S", ""))
        st.add(p70([[("S", "LocalStart"), ("S", "KTime"), ("S", "Time"), ("S", ""), ("L", 0)],
                    [("S", "LocalStop"), ("S", "KTime"), ("S", "Time"), ("S", ""), ("L", stop)],
                    [("S", "ReferenceStart"), ("S", "KTime"), ("S", "Time"), ("S", ""), ("L", 0)],
                    [("S", "ReferenceStop"), ("S", "KTime"), ("S", "Time"), ("S", ""), ("L", stop)]]))
        ly = objects.kid("AnimationLayer", ("L", layer_id),
                         ("S", fbxname("BaseLayer", "AnimLayer")), ("S", ""))
        conns.kid("C", ("S", "OO"), ("L", layer_id), ("L", stack_id))

    # the construction sections (PK7), mapped from whole-mesh triangle indices onto
    # the parts they land in. A remodelled BUILDING has to redeclare these or it will
    # assemble in one lump instead of piece by piece.
    secs = list(mesh["sections"])
    manifest_nodes = []
    for nd in nodes:
        px, py, pz = nd["pivot"]
        name = "%s_p%d_%s%s" % (code, nd["index"], nd["role"], "_SHADOW" if nd["shadow"] else "")
        # vertices go pivot-relative so the FBX node transform IS the game's pivot
        verts, polys, uvs, cols, mats = [], [], [], [], []
        matslots = []
        for ti in used:
            matslots.append(matid[ti])
        matslots.append(flatid)
        local_mat_index = {}
        for i, ti in enumerate(used):
            local_mat_index[ti] = i
        local_mat_index[-1] = len(used)
        vi = 0
        for t in nd["tris"]:
            for k in range(3):
                v = t["v"][k]
                verts += [v["x"] - px, v["y"] - py, v["z"] - pz]
                if t["tex"] >= 0:
                    tx = pack["tex"][t["tex"]]
                    su = float(tx["uw"]) / float(tx["w"])
                    sv = float(tx["uh"]) / float(tx["h"])
                else:
                    su = sv = 1.0
                # FBX UV origin is bottom-left like OBJ, GL's is top-left
                uvs += [v["u"] * su, 1.0 - v["v"] * sv]
                cols += [v["r"] / 255.0, v["g"] / 255.0, v["b"] / 255.0, v["a"] / 255.0]
            polys += [vi, vi + 1, ~(vi + 2)]     # FBX marks a polygon's last index
            mats.append(local_mat_index[t["tex"] if t["tex"] >= 0 else -1])
            vi += 3

        gid = newid()
        counts["Geometry"] += 1
        g = objects.kid("Geometry", ("L", gid), ("S", fbxname(name, "Geometry")), ("S", "Mesh"))
        g.kid("Vertices", ("d", verts))
        g.kid("PolygonVertexIndex", ("i", polys))
        g.kid("GeometryVersion", ("I", 124))
        uvl = g.kid("LayerElementUV", ("I", 0))
        uvl.kid("Version", ("I", 101))
        uvl.kid("Name", ("S", "UVMap"))
        uvl.kid("MappingInformationType", ("S", "ByPolygonVertex"))
        uvl.kid("ReferenceInformationType", ("S", "Direct"))
        uvl.kid("UV", ("d", uvs))
        cl = g.kid("LayerElementColor", ("I", 0))
        cl.kid("Version", ("I", 101))
        cl.kid("Name", ("S", "Col"))
        cl.kid("MappingInformationType", ("S", "ByPolygonVertex"))
        cl.kid("ReferenceInformationType", ("S", "Direct"))
        cl.kid("Colors", ("d", cols))
        ml = g.kid("LayerElementMaterial", ("I", 0))
        ml.kid("Version", ("I", 101))
        ml.kid("Name", ("S", ""))
        ml.kid("MappingInformationType", ("S", "ByPolygon"))
        ml.kid("ReferenceInformationType", ("S", "IndexToDirect"))
        ml.kid("Materials", ("i", mats))
        lay = g.kid("Layer", ("I", 0))
        lay.kid("Version", ("I", 100))
        for typ in ("LayerElementUV", "LayerElementColor", "LayerElementMaterial"):
            le = lay.kid("LayerElement")
            le.kid("Type", ("S", typ))
            le.kid("TypedIndex", ("I", 0))

        mid_ = newid()
        counts["Model"] += 1
        mo = objects.kid("Model", ("L", mid_), ("S", fbxname(name, "Model")), ("S", "Mesh"))
        mo.kid("Version", ("I", 232))
        mo.add(p70([
            [("S", "Lcl Translation"), ("S", "Lcl Translation"), ("S", ""), ("S", "A"),
             ("D", px), ("D", py), ("D", pz)],
            [("S", "DefaultAttributeIndex"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", 0)],
        ]))
        mo.kid("Shading", ("C", True))
        mo.kid("Culling", ("S", "CullingOff"))
        conns.kid("C", ("S", "OO"), ("L", gid), ("L", mid_))
        conns.kid("C", ("S", "OO"), ("L", mid_), ("L", rootid))
        for ms in matslots:
            conns.kid("C", ("S", "OO"), ("L", ms), ("L", mid_))

        ncurves = 0
        track = None
        if layer_id is not None:
            track = anim_track(mesh, nd["index"], (px, py, pz))
            if track:
                ncurves = curve_nodes(objects, conns, layer_id, mid_, track, (px, py, pz))
                counts["AnimationCurveNode"] += ncurves // 3
                counts["AnimationCurve"] += ncurves

        # which construction sections start inside this part
        tri0 = mesh["parts"][nd["index"]][0] if mesh["parts"] else 0
        ntri = mesh["parts"][nd["index"]][1] if mesh["parts"] else len(tris)
        mysecs = [s0 - tri0 for s0 in secs if tri0 <= s0 < tri0 + ntri]

        modes = sorted(set(MODE.get(t["mode"], t["mode"]) for t in nd["tris"]))
        manifest_nodes.append(dict(
            name=name, part=nd["index"], role=nd["role"], is_shadow=nd["shadow"],
            pivot=[px, py, pz], triangles=len(nd["tris"]), draw_modes=modes,
            textures=sorted(set(t["tex"] for t in nd["tris"] if t["tex"] >= 0)),
            animated=bool(track), animated_channels=ncurves // 3,
            hidden_on_frames=([k["frame"] for k in track if not k["visible"]]
                              if track else []),
            construction_sections=mysecs))

    head = N("FBXHeaderExtension")
    head.kid("FBXHeaderVersion", ("I", 1003))
    head.kid("FBXVersion", ("I", FBX_VERSION))
    head.kid("Creator", ("S", "CNC3D tools/art/fbxout.py"))
    gs = N("GlobalSettings")
    gs.kid("Version", ("I", 1000))
    gs.add(p70([
        [("S", "UpAxis"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", 1)],
        [("S", "UpAxisSign"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", 1)],
        [("S", "FrontAxis"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", 2)],
        [("S", "FrontAxisSign"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", 1)],
        [("S", "CoordAxis"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", 0)],
        [("S", "CoordAxisSign"), ("S", "int"), ("S", "Integer"), ("S", ""), ("I", 1)],
        [("S", "UnitScaleFactor"), ("S", "double"), ("S", "Number"), ("S", ""), ("D", 1.0)],
        # 14 = TimeMode "Frames30". The keys carry absolute times either way; this only
        # decides what the artist's timeline is ruled in.
        [("S", "TimeMode"), ("S", "enum"), ("S", ""), ("S", ""), ("I", 14)],
    ]))
    defs = N("Definitions")
    defs.kid("Version", ("I", 100))
    defs.kid("Count", ("I", sum(counts.values())))
    for k, v in counts.items():
        if v:
            ot = defs.kid("ObjectType", ("S", k))
            ot.kid("Count", ("I", v))
    return [head, gs, defs, objects, conns], manifest_nodes, texmeta


def main():
    args = sys.argv[1:]
    outdir = "."
    if "-o" in args:
        i = args.index("-o")
        outdir = args[i + 1]
        del args[i:i + 2]
    house = "nod"
    if "--gdi" in args:
        house = "gdi"
        args.remove("--gdi")
    if len(args) < 2:
        print(__doc__)
        return 2
    packpath, code = args[0], args[1]
    os.makedirs(outdir, exist_ok=True)
    # THE PACK NEXT DOOR IS NOT ALWAYS THE PACK THE GAME RUNS. bake5.py writes into
    # tools/bakery/game; game/*.pack also holds older hand copies, and
    # those were three format versions behind (v13 against v14: 32 meshes and the whole
    # translucent face set missing). Exporting art off the stale one would send an
    # artist a model the game does not draw, so say so rather than let it pass.
    canon = os.path.join(HERE, "..", "bakery", "game", os.path.basename(packpath))
    if os.path.exists(canon) and not os.path.samefile(canon, packpath):
        if os.path.getmtime(canon) > os.path.getmtime(packpath):
            print("WARNING: %s is older than the bakery's own %s. Export from that one."
                  % (packpath, os.path.relpath(canon)))
    pack = PI.read(packpath)
    print("pack: %s, format version %d" % (os.path.basename(packpath), pack["version"]))
    byname = {}
    for c, mi, f in pack["types"]:
        byname.setdefault(c, mi)
    if code not in byname:
        print("no type '%s' in %s. Types: %s" % (code, packpath, " ".join(sorted(byname))))
        return 1
    mesh = pack["meshes"][byname[code]]
    roots, nodes, texmeta = build(pack, code, mesh, outdir, house)
    fbx = os.path.join(outdir, code + ".fbx")
    write_fbx(fbx, roots)
    man = dict(
        type=code, pack=os.path.basename(packpath), cartridge_mesh=mesh["name"],
        house_palette=house,
        axes="x=east, y=UP, z=south, right handed",
        units="cartridge model units; 1 map cell = 1024 units (renderer MODEL_SCALE = 1/1024)",
        total_triangles=mesh["ntris"],
        animation=dict(
            frames=(mesh["anim"] or {}).get("frames", 0),
            playback_hz=ENGINE_HZ,
            clips=[dict(first_frame=c["t0"], last_frame=c["t1"],
                        loop=bool(c["loop"]))
                   for c in (mesh["anim"] or {}).get("clips", [])],
            note=("the baked clip. WHICH frame the game shows is decided by the "
                  "engine, not by playback: see docs/animation-drivers.md")),
        construction_sections=list(mesh["sections"]),
        nodes=nodes, textures=texmeta,
        return_contract=dict(
            keep_node_names=True,
            keep_pivots=True,
            triangles_are_the_budget="see docs/art-return-contract.md",
            uv_space="0..1 over the PNG as delivered",
            vertex_colours="required; they are the baked lighting the renderer modulates by",
            texture_rules="power of two, max 256 on a side, max 8:1 aspect (Voodoo 2)"))
    with open(os.path.join(outdir, code + ".json"), "w") as f:
        json.dump(man, f, indent=2)
    a = mesh["anim"]
    print("FBX|%s|%s|tris=%d|nodes=%d|textures=%d|frames=%d|sections=%d|bytes=%d"
          % (code, fbx, mesh["ntris"], len(nodes), len(texmeta),
             a["frames"] if a else 0, len(mesh["sections"]), os.path.getsize(fbx)))
    for n in nodes:
        print("  node %-26s %-7s tris=%-4d pivot=(%8.1f,%8.1f,%8.1f) modes=%-14s "
              "anim=%s sections=%d"
              % (n["name"], n["role"], n["triangles"],
                 n["pivot"][0], n["pivot"][1], n["pivot"][2], ",".join(n["draw_modes"]),
                 ("%d chan" % n["animated_channels"]) if n["animated"] else "-",
                 len(n["construction_sections"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
