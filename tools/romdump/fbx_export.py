#!/usr/bin/env python3
"""
CNC3D -- export cartridge meshes as textured binary FBX.

WHAT THIS IS FOR. The pipeline in model.py / textures.py lifts a display list into an
OBJ plus loose PNGs, which is enough to LOOK at a mesh but loses two things a DCC app
wants: the cartridge's own per-vertex normals, and the fact that one mesh is drawn with
several different textures. This writes FBX 7.4 binary instead, with per-face materials,
the real normals, and the textures embedded in the file.

*** WHERE THE MODELS COME FROM -- read this before adding one ***
Do NOT hardcode display-list offsets. The briefing overlay carries its own scene-graph
name table, and this file parses it, so the mapping is the cartridge's and not ours:

    ROM 0x01DEBE0   16 x 16-byte records, scene-graph Node* at +0x0C
    ROM 0x01DECE0   16 x char*, the matching names, same order

That table is how "BRF_LOGO_GDI" and "BRF_LOGO_NOD" were found. A scene-graph node is
0x1C bytes with the Gfx* display list at +0x00 (see tools/bakery/objgraph2.py for the
full node layout). All three logo nodes have a NULL handle and no children, so the mesh
is exactly the root display list and there is no mount transform to apply.

A whole-segment display-list SCAN does not find these lists. f3d.scan() walks forward
from the first byte that decodes as an opcode and only backtracks eight bytes, so a long
preceding run of opcode-looking data swallows the real start: BRF_LOGO_NOD (0x01EF058)
and GDI_LOGO_TITLE_ROOT (0x01F52A0) are both invisible to it, and the scan's nearby
"dl_01F6140" is a fragment, not a model. The name table is the ground truth.

*** THE VERTEX RGB BYTES ARE NORMALS *OR* COLOURS, PER MESH -- MEASURE, DO NOT ASSUME ***
An F3DEX2 Vtx has four bytes that are a signed unit normal when the list runs with
G_LIGHTING on and a shade colour when it does not, and these lists set the geometry mode
OUTSIDE themselves, so the list alone does not say which. The data does, unambiguously:

    BRF_LOGO_GDI    1040 verts, 515 distinct values, |(r,g,b) as s8| in 125.5-127.0
                    for 100% of them                          -> NORMALS
    BRF_EVA_ROOT     150 verts, THREE distinct values, all r==g==b
                    (75,75,75) (151,151,151) (185,185,185)    -> COLOURS (baked shade)
    BRF_SPEAKER_ROOT  26 verts, four values, all r==g==b      -> COLOURS

`vertex_attr_kind()` below applies that test. Getting it backwards is not subtle but it
is silent: read as normals, EVA's three grey levels become three arbitrary directions and
the wordmark renders dark and wrongly lit; read as colours, the medallion's (0,0,127)
becomes dark blue.

Usage:
    python3 fbx_export.py <outdir> [NAME ...]      # default: the two faction logos
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import segments                                    # noqa: E402
import textures as TX                              # noqa: E402

ROM_CANDIDATES = [
    os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "data", "rom", "cnc_eu.z64")),
    os.path.expanduser("./data/rom/cnc_eu.z64"),
]

# BriefingCode: rom = ram - delta. From the loader's own segment table (segments.py).
BRF_ROM, BRF_RAM = 0x1B67D0, 0x801C2600
BRF_DELTA = BRF_RAM - BRF_ROM

# The two parallel arrays described above.
BRF_NODE_TABLE = 0x01DEBE0
BRF_NAME_TABLE = 0x01DECE0
BRF_TABLE_LEN = 16

DEFAULT_MODELS = ["BRF_LOGO_GDI", "BRF_LOGO_NOD"]


def load_rom():
    for p in ROM_CANDIDATES:
        if os.path.exists(p):
            return open(p, "rb").read()
    raise IOError("cnc_eu.z64 not found (looked in %s)" % ", ".join(ROM_CANDIDATES))


# --------------------------------------------------------------- the name table
def briefing_models(rom):
    """{name: display-list ROM offset} straight out of the cartridge's own table."""
    def u32(o):
        return struct.unpack_from(">I", rom, o)[0]

    def cstr(ram):
        o = ram - BRF_DELTA
        return rom[o:rom.find(b"\0", o)].decode("latin1")

    out = {}
    for i in range(BRF_TABLE_LEN):
        name = cstr(u32(BRF_NAME_TABLE + i * 4))
        node = u32(BRF_NODE_TABLE + i * 16 + 0x0C)
        if not (BRF_RAM <= node < BRF_RAM + (0x20E400 - BRF_ROM)):
            continue
        gfx = u32(node - BRF_DELTA)                 # Node +0x00 = Gfx* display list
        if BRF_RAM <= gfx < BRF_RAM + (0x20E400 - BRF_ROM):
            out[name] = gfx - BRF_DELTA
    return out


# --------------------------------------------------------------- mesh building
def s8(b):
    return b - 256 if b > 127 else b


def vertex_attr_kind(verts):
    """"normal" or "colour" for the Vtx RGB bytes of one mesh. See the note above.

    The test is the s8 magnitude: a packed unit normal is length ~127 by construction, and
    nothing else has a reason to be. 95% rather than 100% leaves room for the rounding the
    cartridge's own exporter did (the logos measure 125.5-127.0, never exactly 127).
    """
    if not verts:
        return "normal"
    n = 0
    for v in verts:
        x, y, z = s8(v['r']), s8(v['g']), s8(v['b'])
        if 118 <= (x*x + y*y + z*z) ** 0.5 <= 136:
            n += 1
    return "normal" if n >= 0.95 * len(verts) else "colour"


def _geometric_normal(a, b, c):
    ux, uy, uz = b['x']-a['x'], b['y']-a['y'], b['z']-a['z']
    wx, wy, wz = c['x']-a['x'], c['y']-a['y'], c['z']-a['z']
    nx, ny, nz = uy*wz - uz*wy, uz*wx - ux*wz, ux*wy - uy*wx
    ln = (nx*nx + ny*ny + nz*nz) ** 0.5 or 1.0
    return (nx/ln, ny/ln, nz/ln)


def build_mesh(rom, dl_off):
    """(positions, polys, per-poly material index, materials) for one display list.

    Positions are welded by exact coordinate so the mesh comes out connected and
    editable; normals and UVs stay per-polygon-vertex, so nothing is averaged away.
    """
    resolve = segments.make_resolver(dl_off)
    verts, faces, sof, tex, _st = TX.walk_rdp(rom, dl_off, resolve)
    kind = vertex_attr_kind(verts)

    # Material list, ordered by first appearance so face indices are stable.
    states, order = {}, []
    for s in sof:
        if s not in states:
            states[s] = len(order)
            order.append(s)

    pos_index, positions = {}, []
    polys, poly_mat, poly_nrm, poly_uv, poly_col = [], [], [], [], []
    dropped = 0
    for fi, tri in enumerate(faces):
        a, b, c = (verts[i] for i in tri)
        # Drop zero-area triangles: they draw nothing on the RDP and become NaN
        # normals in a DCC app.
        ux, uy, uz = b['x'] - a['x'], b['y'] - a['y'], b['z'] - a['z']
        wx, wy, wz = c['x'] - a['x'], c['y'] - a['y'], c['z'] - a['z']
        if (uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx) == (0, 0, 0):
            dropped += 1
            continue
        idx = []
        for v in (a, b, c):
            key = (v['x'], v['y'], v['z'])
            if key not in pos_index:
                pos_index[key] = len(positions)
                positions.append(key)
            idx.append(pos_index[key])
        polys.append(idx)
        poly_mat.append(states[sof[fi]])
        state = sof[fi]
        t = tex.get(state[0])
        tw, th = (t['w'], t['h']) if t else (1, 1)
        gn = None if kind == "normal" else _geometric_normal(a, b, c)
        for v in (a, b, c):
            if kind == "normal":
                n = (s8(v['r']), s8(v['g']), s8(v['b']))
                ln = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5 or 1.0
                poly_nrm.append((n[0] / ln, n[1] / ln, n[2] / ln))
            else:
                # No normals in the data: the shading is baked into the colours. A flat
                # face normal is the honest stand-in, and it is what the silhouette needs.
                poly_nrm.append(gn)
                poly_col.append((v['r'] / 255.0, v['g'] / 255.0, v['b'] / 255.0))
            # S10.5 texel -> UV. The /32 and the tile size are both load-bearing;
            # see the UV RULE note in textures.py.
            poly_uv.append(((v['u'] / 32.0) / tw, (v['v'] / 32.0) / th))

    mats = []
    for s in order:
        t = tex.get(s[0])
        mats.append(dict(state=s, tex=t))
    return dict(positions=positions, polys=polys, poly_mat=poly_mat,
                poly_nrm=poly_nrm, poly_uv=poly_uv, poly_col=poly_col, kind=kind,
                mats=mats, dropped=dropped,
                raw_verts=len(verts), raw_faces=len(faces))


def texture_png(rom, t):
    """Decode one bound tile to PNG bytes. Written by hand -- no PIL dependency."""
    rows = TX.decode_texture(rom, t['rom'], t['fmt'], t['siz'], t['w'], t['h'], t['pal'])
    if rows is None:
        return None
    raw = b"".join(b"\0" + bytes(r) for r in rows)          # filter byte 0 per scanline

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", t['w'], t['h'], 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


# --------------------------------------------------------------- FBX binary writer
class E:
    """One FBX node record: a name, a property list, and nested nodes."""

    def __init__(self, name, *props):
        self.name = name.encode("ascii")
        self.props = list(props)
        self.kids = []

    def add(self, name, *props):
        e = E(name, *props)
        self.kids.append(e)
        return e


# Property wrappers. A bare python value would be ambiguous (an int could be I or L).
class P:
    def __init__(self, code, value):
        self.code, self.value = code, value


def i16(v):    return P(b"Y", v)
def boolp(v):  return P(b"C", v)
def i32(v):    return P(b"I", v)
def f64(v):    return P(b"D", v)
def i64(v):    return P(b"L", v)
def s(v):      return P(b"S", v)
def raw(v):    return P(b"R", v)
def af64(v):   return P(b"d", v)
def ai32(v):   return P(b"i", v)


def _prop_bytes(p):
    if p.code == b"Y":  return b"Y" + struct.pack("<h", p.value)
    if p.code == b"C":  return b"C" + struct.pack("<B", 1 if p.value else 0)
    if p.code == b"I":  return b"I" + struct.pack("<i", p.value)
    if p.code == b"D":  return b"D" + struct.pack("<d", p.value)
    if p.code == b"L":  return b"L" + struct.pack("<q", p.value)
    if p.code == b"S":
        b = p.value.encode("utf-8") if isinstance(p.value, str) else p.value
        return b"S" + struct.pack("<I", len(b)) + b
    if p.code == b"R":
        return b"R" + struct.pack("<I", len(p.value)) + p.value
    if p.code in (b"d", b"i"):
        fmt = "<%d%s" % (len(p.value), "d" if p.code == b"d" else "i")
        data = struct.pack(fmt, *p.value)
        comp = zlib.compress(data, 6)
        if len(comp) < len(data):
            return p.code + struct.pack("<III", len(p.value), 1, len(comp)) + comp
        return p.code + struct.pack("<III", len(p.value), 0, len(data)) + data
    raise ValueError(p.code)


SENTINEL = b"\0" * 13


def _write_elem(e, out):
    """Serialise one node. EndOffset is absolute, so this needs the real stream."""
    start = out.tell()
    out.write(b"\0" * 13)                          # placeholder for the three u32 + namelen
    out.write(e.name)
    pl_start = out.tell()
    for p in e.props:
        out.write(_prop_bytes(p))
    pl_len = out.tell() - pl_start
    for k in e.kids:
        _write_elem(k, out)
    if e.kids:
        out.write(SENTINEL)
    end = out.tell()
    out.seek(start)
    out.write(struct.pack("<III", end, len(e.props), pl_len))
    out.write(struct.pack("<B", len(e.name)))
    out.seek(end)


FOOTER_MAGIC = bytes([0xF8, 0x5A, 0x8C, 0x6A, 0xDE, 0xF5, 0xD9, 0x7E,
                      0xEC, 0xE9, 0x0C, 0xE3, 0x75, 0x8F, 0x29, 0x0B])
FOOTER_ID = bytes([0xFA, 0xBC, 0xAB, 0x09, 0xD0, 0xC8, 0xD4, 0x66,
                   0xB1, 0x76, 0xFB, 0x83, 0x1C, 0xF7, 0x26, 0x7E])
VERSION = 7400


def write_fbx(path, roots):
    import io
    out = io.BytesIO()
    out.write(b"Kaydara FBX Binary  \x00\x1a\x00")
    out.write(struct.pack("<I", VERSION))
    for r in roots:
        _write_elem(r, out)
    out.write(SENTINEL)
    out.write(FOOTER_ID)
    while out.tell() % 16 != 0:
        out.write(b"\0")
    out.write(struct.pack("<I", VERSION))
    out.write(b"\0" * 120)
    out.write(FOOTER_MAGIC)
    open(path, "wb").write(out.getvalue())
    return len(out.getvalue())


# --------------------------------------------------------------- scene assembly
def p70(parent):
    return parent.add("Properties70")


def prop(props, name, typ, sub, flags, *vals):
    props.add("P", s(name), s(typ), s(sub), s(flags), *vals)


def objname(name, cls):
    """Binary FBX names an object "Name\\0\\1Class". ASCII FBX writes "Class::Name";
    using the ASCII spelling in a binary file makes importers show "ModelBRF_LOGO_GDI"
    instead of the real name (observed in three.js), so it has to be the binary form."""
    return name + "\x00\x01" + cls


def build_fbx(mesh, name, texfiles):
    """Assemble the FBX node tree for one mesh."""
    uid = [1000000]

    def nid():
        uid[0] += 1
        return uid[0]

    root = []
    hdr = E("FBXHeaderExtension")
    hdr.add("FBXHeaderVersion", i32(1003))
    hdr.add("FBXVersion", i32(VERSION))
    hdr.add("EncryptionType", i32(0))
    ct = hdr.add("CreationTimeStamp")
    ct.add("Version", i32(1000))
    for k, v in (("Year", 1995), ("Month", 1), ("Day", 1), ("Hour", 0),
                 ("Minute", 0), ("Second", 0), ("Millisecond", 0)):
        ct.add(k, i32(v))
    hdr.add("Creator", s("CNC3D fbx_export.py -- Command & Conquer (N64) cartridge"))
    root.append(hdr)

    # FileId is a 16-byte blob. Some importers (Unity has been the reported one) dislike
    # an arbitrary length here, so use the same constant Blender's exporter writes --
    # it pairs with FOOTER_ID below and is the combination every tool has been fed.
    root.append(E("FileId", raw(bytes([0x28, 0xB3, 0x2A, 0xEB, 0xB6, 0x24, 0xCC, 0xC2,
                                       0xBF, 0xC8, 0xB0, 0x2A, 0xA9, 0x2B, 0xFC, 0xF1]))))
    root.append(E("CreationTime", s("1995-01-01 00:00:00:000")))
    root.append(E("Creator", s("CNC3D fbx_export.py")))

    gs = E("GlobalSettings")
    gs.add("Version", i32(1000))
    g = p70(gs)
    prop(g, "UpAxis", "int", "Integer", "", i32(1))
    prop(g, "UpAxisSign", "int", "Integer", "", i32(1))
    prop(g, "FrontAxis", "int", "Integer", "", i32(2))
    prop(g, "FrontAxisSign", "int", "Integer", "", i32(1))
    prop(g, "CoordAxis", "int", "Integer", "", i32(0))
    prop(g, "CoordAxisSign", "int", "Integer", "", i32(1))
    prop(g, "OriginalUpAxis", "int", "Integer", "", i32(1))
    prop(g, "OriginalUpAxisSign", "int", "Integer", "", i32(1))
    prop(g, "UnitScaleFactor", "double", "Number", "", f64(1.0))
    prop(g, "OriginalUnitScaleFactor", "double", "Number", "", f64(1.0))
    prop(g, "TimeMode", "enum", "", "", i32(6))
    root.append(gs)

    docs = E("Documents")
    docs.add("Count", i32(1))
    doc_id = nid()
    d = docs.add("Document", i64(doc_id), s(""), s("Scene"))
    dp = p70(d)
    prop(dp, "SourceObject", "object", "", "")
    prop(dp, "ActiveAnimStackName", "KString", "", "", s(""))
    d.add("RootNode", i64(0))
    root.append(docs)
    root.append(E("References"))

    nmat = len(mesh["mats"])
    defs = E("Definitions")
    defs.add("Version", i32(100))
    defs.add("Count", i32(3 + nmat * 3))
    for typ, cnt in (("GlobalSettings", 1), ("Model", 1), ("Geometry", 1),
                     ("Material", nmat), ("Texture", nmat), ("Video", nmat)):
        if cnt:
            ot = defs.add("ObjectType", s(typ))
            ot.add("Count", i32(cnt))
    root.append(defs)

    objs = E("Objects")
    conns = E("Connections")

    # ---- geometry
    geo_id = nid()
    geo = objs.add("Geometry", i64(geo_id), s(objname(name, "Geometry")), s("Mesh"))
    verts = []
    for x, y, z in mesh["positions"]:
        verts += [float(x), float(y), float(z)]
    geo.add("Vertices", af64(verts))
    pvi = []
    for poly in mesh["polys"]:
        pvi += poly[:-1] + [~poly[-1]]              # last index of a polygon is NOT'd
    geo.add("PolygonVertexIndex", ai32(pvi))
    geo.add("GeometryVersion", i32(124))

    ln = geo.add("LayerElementNormal", i32(0))
    ln.add("Version", i32(101))
    ln.add("Name", s(""))
    ln.add("MappingInformationType", s("ByPolygonVertex"))
    ln.add("ReferenceInformationType", s("Direct"))
    nrm = []
    for n in mesh["poly_nrm"]:
        nrm += [n[0], n[1], n[2]]
    ln.add("Normals", af64(nrm))

    lu = geo.add("LayerElementUV", i32(0))
    lu.add("Version", i32(101))
    lu.add("Name", s("UVMap"))
    lu.add("MappingInformationType", s("ByPolygonVertex"))
    lu.add("ReferenceInformationType", s("IndexToDirect"))
    uv_index, uvs, uvi = {}, [], []
    for u, v in mesh["poly_uv"]:
        # FBX V runs bottom-up; the RDP's T runs top-down.
        key = (round(u, 6), round(1.0 - v, 6))
        if key not in uv_index:
            uv_index[key] = len(uvs) // 2
            uvs += [key[0], key[1]]
        uvi.append(uv_index[key])
    lu.add("UV", af64(uvs))
    lu.add("UVIndex", ai32(uvi))

    if mesh["poly_col"]:
        lc = geo.add("LayerElementColor", i32(0))
        lc.add("Version", i32(101))
        lc.add("Name", s("Col"))
        lc.add("MappingInformationType", s("ByPolygonVertex"))
        lc.add("ReferenceInformationType", s("IndexToDirect"))
        cidx, cols, ci = {}, [], []
        for r, g_, b in mesh["poly_col"]:
            key = (round(r, 6), round(g_, 6), round(b, 6))
            if key not in cidx:
                cidx[key] = len(cols) // 4
                cols.extend([key[0], key[1], key[2], 1.0])
            ci.append(cidx[key])
        lc.add("Colors", af64(cols))
        lc.add("ColorIndex", ai32(ci))

    lm = geo.add("LayerElementMaterial", i32(0))
    lm.add("Version", i32(101))
    lm.add("Name", s(""))
    lm.add("MappingInformationType", s("ByPolygon"))
    lm.add("ReferenceInformationType", s("IndexToDirect"))
    lm.add("Materials", ai32(list(mesh["poly_mat"])))

    lay = geo.add("Layer", i32(0))
    lay.add("Version", i32(100))
    layers = ["LayerElementNormal", "LayerElementUV", "LayerElementMaterial"]
    if mesh["poly_col"]:
        layers.insert(1, "LayerElementColor")
    for t in layers:
        le = lay.add("LayerElement")
        le.add("Type", s(t))
        le.add("TypedIndex", i32(0))

    # ---- model
    mdl_id = nid()
    mdl = objs.add("Model", i64(mdl_id), s(objname(name, "Model")), s("Mesh"))
    mdl.add("Version", i32(232))
    mp = p70(mdl)
    prop(mp, "Lcl Translation", "Lcl Translation", "", "A", f64(0.0), f64(0.0), f64(0.0))
    prop(mp, "Lcl Rotation", "Lcl Rotation", "", "A", f64(0.0), f64(0.0), f64(0.0))
    prop(mp, "Lcl Scaling", "Lcl Scaling", "", "A", f64(1.0), f64(1.0), f64(1.0))
    prop(mp, "DefaultAttributeIndex", "int", "Integer", "", i32(0))
    mdl.add("Shading", boolp(True))
    mdl.add("Culling", s("CullingOff"))

    conns.add("C", s("OO"), i64(mdl_id), i64(0))
    conns.add("C", s("OO"), i64(geo_id), i64(mdl_id))

    # ---- materials / textures / embedded images, in face-index order
    for mi, m in enumerate(mesh["mats"]):
        mat_id = nid()
        mname = "%s_mat%d" % (name, mi)
        mat = objs.add("Material", i64(mat_id), s(objname(mname, "Material")), s(""))
        mat.add("Version", i32(102))
        mat.add("ShadingModel", s("phong"))
        mat.add("MultiLayer", i32(0))
        pp = p70(mat)
        prop(pp, "DiffuseColor", "Color", "", "A", f64(1.0), f64(1.0), f64(1.0))
        prop(pp, "DiffuseFactor", "Number", "", "A", f64(1.0))
        prop(pp, "AmbientColor", "Color", "", "A", f64(0.0), f64(0.0), f64(0.0))
        prop(pp, "SpecularColor", "Color", "", "A", f64(0.2), f64(0.2), f64(0.2))
        prop(pp, "Shininess", "Number", "", "A", f64(20.0))
        prop(pp, "Opacity", "Number", "", "A", f64(1.0))
        conns.add("C", s("OO"), i64(mat_id), i64(mdl_id))

        png, fname = texfiles[mi]
        if png is None:
            continue
        vid_id = nid()
        vid = objs.add("Video", i64(vid_id), s(objname(fname, "Video")), s("Clip"))
        vid.add("Type", s("Clip"))
        vp = p70(vid)
        prop(vp, "Path", "KString", "XRefUrl", "", s(fname))
        vid.add("UseMipMap", i32(0))
        vid.add("Filename", s(fname))
        vid.add("RelativeFilename", s(fname))
        vid.add("Content", raw(png))

        tex_id = nid()
        tex = objs.add("Texture", i64(tex_id), s(objname(fname, "Texture")), s(""))
        tex.add("Type", s("TextureVideoClip"))
        tex.add("Version", i32(202))
        tex.add("TextureName", s(objname(fname, "Texture")))
        tp = p70(tex)
        prop(tp, "UVSet", "KString", "", "", s("UVMap"))
        prop(tp, "UseMaterial", "bool", "", "", i32(1))
        prop(tp, "WrapModeU", "enum", "", "", i32(0))       # 0 = repeat; every tile
        prop(tp, "WrapModeV", "enum", "", "", i32(0))       # here is mask 5 / cm 0
        tex.add("Media", s(objname(fname, "Video")))
        tex.add("FileName", s(fname))
        tex.add("RelativeFilename", s(fname))
        tex.add("ModelUVTranslation", f64(0.0), f64(0.0))
        tex.add("ModelUVScaling", f64(1.0), f64(1.0))
        tex.add("Texture_Alpha_Source", s("None"))
        tex.add("Cropping", i32(0), i32(0), i32(0), i32(0))

        conns.add("C", s("OO"), i64(vid_id), i64(tex_id))
        conns.add("C", s("OP"), i64(tex_id), i64(mat_id), s("DiffuseColor"))

    root.append(objs)
    root.append(conns)
    takes = E("Takes")
    takes.add("Current", s(""))
    root.append(takes)
    return root


def export(rom, name, dl_off, outdir):
    mesh = build_mesh(rom, dl_off)
    os.makedirs(outdir, exist_ok=True)
    texfiles = []
    for mi, m in enumerate(mesh["mats"]):
        t = m["tex"]
        if t is None:
            texfiles.append((None, None))
            continue
        png = texture_png(rom, t)
        fname = "%s_tex%d_%07X.png" % (name, mi, t["rom"])
        if png:
            open(os.path.join(outdir, fname), "wb").write(png)
        texfiles.append((png, fname))
    tree = build_fbx(mesh, name, texfiles)
    path = os.path.join(outdir, name + ".fbx")
    size = write_fbx(path, tree)
    return path, size, mesh, texfiles


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    outdir = sys.argv[1]
    wanted = sys.argv[2:] or DEFAULT_MODELS
    rom = load_rom()
    table = briefing_models(rom)
    for name in wanted:
        if name not in table:
            print("!! %s is not in the mission briefing name table (%s)"
                  % (name, ", ".join(sorted(table))))
            continue
        path, size, mesh, texfiles = export(rom, name, table[name], outdir)
        print("%s  dl=0x%07X" % (name, table[name]))
        print("   %d verts (%d before weld), %d polys%s, %d materials, vtx bytes = %s"
              % (len(mesh["positions"]), mesh["raw_verts"], len(mesh["polys"]),
                 "" if not mesh["dropped"] else " (%d degenerate dropped)" % mesh["dropped"],
                 len(mesh["mats"]),
                 "NORMALS" if mesh["kind"] == "normal"
                 else "COLOURS (normals computed from the faces)"))
        for mi, m in enumerate(mesh["mats"]):
            t = m["tex"]
            nf = sum(1 for x in mesh["poly_mat"] if x == mi)
            print("     mat%d  %-4s faces  %s" % (
                mi, nf, "no texture" if t is None else
                "%s%d %dx%d rom=0x%07X -> %s" % (TX.FMT.get(t['fmt'], '?'), t['siz'],
                                                 t['w'], t['h'], t['rom'], texfiles[mi][1])))
        print("   -> %s (%d bytes)" % (path, size))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
