#!/usr/bin/env python3
"""Put an artist's FBX back over one mesh in a baked mission pack.

The return path. fbxout.py sends a cartridge model out; this brings one back, so a
type can be remodelled without touching the baker or the ROM. The bake chain is
unchanged and is still the only thing that reads the cartridge:

    cartridge ROM -> display list -> bake5.py -> .pack -> [ this tool ] -> .pack

    python3 tools/art/fbxin.py SCG01EA.pack MTNK "Medium tank.FBX" -o out.pack

WHY AN OVERRIDE AND NOT A BAKER CHANGE. bake5.py reads the ROM and only the ROM, and
that is the property that makes a bake reproducible. A model that did not come from
the cartridge has no display list, no G_VTX batches and no RDP tile state, so it
cannot be expressed as baker input. It is applied afterwards instead, to the baked
artifact, where exactly one mesh record and the textures it needs are rewritten and
every other byte of the pack is copied through unchanged.

WHAT THIS TOOL DECIDES, all of which the cartridge stated and an FBX does not:

  Axes and scale. The pack is x east, y UP, z south, 1024 units to the map cell. A
      DCC file carries its own up axis in GlobalSettings; z-up is mapped
      (x, y, z) -> (x, z, -y). The sign on the last one is not a convention, it is
      measured: --report prints the delivered bounding box beside the mesh being
      replaced, and a vehicle whose barrel ends up pointing backwards is a sign flip.
      Scale defaults to 1 and --fit derives it from the two bounding boxes instead.

  Vertex colour. The renderer has no lights. It multiplies the texture by the vertex
      colour, and that is the whole shading model, so geometry delivered without
      colours draws flat. Where an FBX carries a colour layer it is used. Where it
      does not, --shade normals (the default) bakes a fixed-light ramp from the
      delivered normals into the grey range the cartridge's own baked lighting uses,
      and --shade flat writes white. THIS IS AUTHORED SHADING, not cartridge data:
      a model imported this way is lit by a rule chosen here, not by the console.

  Draw mode and wrap. Both are per triangle in the pack and belong to the display
      list, not to the texture, so new art has no source for them. Everything comes
      in opaque and repeating unless --xlu names a material.

  Running tracks. --tread MATERIAL marks a material's faces as tank track, which the
      renderer scrolls as the vehicle drives. Track faces are re-projected onto a
      repeating strip first: the delivered UVs unwrap the track onto its own atlas
      region, and an atlas cannot be scrolled without smearing what is next to it.
      The projection runs the strip along the vehicle's length and across the track's
      width, repeating every --treadpitch model units. The strip texture is supplied
      with --treadtex, or generated when none exists, in which case it is written out
      beside the pack and is placeholder art meant to be replaced.

  Part roles and pivots. A part is a contiguous run of triangles plus a pivot and a
      role (static, turret, rotor). Role comes from the node name; the pivot defaults
      to the pivot the pack already holds for the part at that index, because a
      remodelled turret usually turns about the same ring as the one it replaces.
      --pivot overrides it per part.

  Textures are appended to the pack's bank, so every existing texture index in every
      other mesh still means what it did. They are resampled down to --texmax on a
      side (256 by default: a Voodoo 2 will not upload more, and it fails by drawing
      untextured rather than by complaining).

WHAT IT DOES NOT DO, and cannot from an FBX alone:

  * No house colours. The cartridge keeps a GDI and a Nod palette of the same texture
    and recolours a unit by swapping them. A modern RGBA image has no palette, so an
    imported model looks the same for both sides until an artist delivers two sets.
  * No construction sections. Those are the display list's own vertex batches and a
    remodelled BUILDING has none, so it would appear in one lump rather than assembling.
    A single section is written, which is correct for a vehicle and wrong for a
    structure.
  * No node animation. Any PKB clip on the mesh being replaced is dropped.
"""
import argparse
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "bakery"))
import fbxread as FB          # noqa: E402
import packinspect as PI      # noqa: E402

MODE_OPAQUE, MODE_CUTOUT, MODE_SHADOW, MODE_XLU = 0, 1, 2, 3
ROLE_STATIC, ROLE_TURRET, ROLE_ROTOR = 0, 1, 2

# The pack's wrap byte carries the RDP's two 2-bit clamp/mirror fields in bits 0..3 and
# nothing else; the renderer masks them and has always ignored the top nibble. That
# nibble now carries the number of frames in a running-track strip: 0 is not track, 1 is
# a continuously scrolled strip, and 2..15 step through a flipbook of that many frames
# stacked down the image. Putting it here rather than in a new pack field means an
# imported model needs no format change and an older renderer draws it standing still.
TREAD_SHIFT, TREAD_MAX = 4, 15

# The grey range the cartridge's own baked vertex lighting occupies on a vehicle,
# measured across the shipped Medium Tank: darkest face 14, brightest 220. A ramp
# baked here is fitted into it so an imported model sits in the same tonal range as
# the models beside it rather than glowing.
SHADE_LO, SHADE_HI = 14, 220
# The light the ramp is baked from, in pack axes (x east, y up, z south), pointing
# from the surface towards the light. High and slightly to the front left, which is
# where the cartridge's own baked highlights sit.
SHADE_DIR = (-0.35, 0.86, 0.37)


# ---------------------------------------------------------------- the delivered file
def fbx_scene(path):
    """Every mesh node of an FBX as (name, triangles, material per triangle), where a
    triangle is three (position, uv, normal) tuples in the file's own axes."""
    ver, root = FB.load(path)
    objs, conns = FB.objects(root)
    child = {}
    for _kind, a, b, prop in conns:
        child.setdefault(b, []).append((a, prop))
    models = [(i, n) for i, n in objs.items() if n.name == "Model"]
    out = []
    for mid, mnode in models:
        geo = mats = None
        geo_id = None
        mats = []
        for cid, prop in child.get(mid, []):
            c = objs.get(cid)
            if c is None:
                continue
            if c.name == "Geometry":
                geo, geo_id = c, cid
            elif c.name == "Material":
                mats.append(FB.name_of(c))
        if geo is None:
            continue
        out.append(dict(name=FB.name_of(mnode), geom=geo, materials=mats,
                        node=mnode, id=mid))
    return ver, out, objs, child


def prop70(node, name):
    """One Properties70 entry's numeric values, or None."""
    pr = node.first("Properties70")
    if pr is None:
        return None
    for e in pr.find("P"):
        if e.props and e.props[0] == name:
            return [v for v in e.props if isinstance(v, float)]
    return None


def node_offset(mnode, path_name):
    """The translation a Model node applies to its geometry, in the file's own axes.

    Two of them, and both matter: Lcl Translation places the node in the scene, and
    GeometricTranslation offsets the mesh under the node without moving the node's
    own pivot. A DCC leaves the second one behind whenever an artist moves geometry
    with the pivot locked, and ignoring it drops a model through its own floor.

    Rotation and scaling are refused rather than half-applied: they are identity in
    everything seen so far, and silently ignoring a real one would move the model in
    a way nobody could see in the numbers this tool prints."""
    off = [0.0, 0.0, 0.0]
    for key in ("Lcl Translation", "GeometricTranslation"):
        v = prop70(mnode, key)
        if v and len(v) >= 3:
            for i in range(3):
                off[i] += v[i]
    for key in ("Lcl Rotation", "GeometricRotation", "PreRotation", "PostRotation"):
        v = prop70(mnode, key)
        if v and any(abs(c) > 1e-6 for c in v[:3]):
            raise SystemExit("%s carries a non-zero %s; bake it into the geometry"
                             % (path_name, key))
    for key in ("Lcl Scaling", "GeometricScaling"):
        v = prop70(mnode, key)
        if v and any(abs(c - 1.0) > 1e-6 for c in v[:3]):
            raise SystemExit("%s carries a non-unit %s; bake it into the geometry"
                             % (path_name, key))
    return tuple(off)


def layer_values(geom, layer, key, npoly, corners):
    """One FBX layer element read into a flat per-corner list. Handles the mapping and
    reference modes a DCC actually emits: direct or indexed, per corner or per polygon."""
    ln = geom.first(layer)
    if ln is None:
        return None, None
    mapping = ln.first("MappingInformationType").props[0]
    ref = ln.first("ReferenceInformationType")
    ref = ref.props[0] if ref else "Direct"
    data = ln.first(key).props[0]
    idx = None
    for cand in (key + "Index", "Indexes", "Materials"):
        n = ln.first(cand)
        if n is not None and cand != key:
            idx = n.props[0]
            break
    return (mapping, ref, data, idx), ln


def triangles(geom, want_normals=True):
    """The geometry as flat triangles: positions, uvs, normals and a material index
    for each. Polygons are required to be triangles already, which the contract asks
    for so that topology stays the artist's decision."""
    verts = geom.first("Vertices").props[0]
    polys = FB.polygons(geom)
    for p in polys:
        if len(p) != 3:
            raise SystemExit("polygon with %d corners: triangulate before sending"
                             % len(p))

    uvl, _ = layer_values(geom, "LayerElementUV", "UV", len(polys), 3 * len(polys))
    nml, _ = layer_values(geom, "LayerElementNormal", "Normals", len(polys), 3 * len(polys))
    mtl, _ = layer_values(geom, "LayerElementMaterial", "Materials", len(polys), len(polys))

    def corner_uv(ci):
        if uvl is None:
            return (0.0, 0.0)
        mapping, ref, data, idx = uvl
        if mapping != "ByPolygonVertex":
            raise SystemExit("UV mapping %s is not supported" % mapping)
        j = idx[ci] if (ref.startswith("IndexTo") and idx) else ci
        return (data[2 * j], data[2 * j + 1])

    def corner_nrm(ci, vi):
        if nml is None:
            return None
        mapping, ref, data, idx = nml
        if mapping == "ByPolygonVertex":
            j = idx[ci] if (ref.startswith("IndexTo") and idx) else ci
        elif mapping == "ByVertice":
            j = idx[vi] if (ref.startswith("IndexTo") and idx) else vi
        else:
            return None
        return (data[3 * j], data[3 * j + 1], data[3 * j + 2])

    def poly_mat(pi):
        if mtl is None:
            return 0
        mapping, ref, data, idx = mtl
        vals = idx if idx else data
        if mapping == "AllSame":
            return vals[0]
        return vals[pi]

    tris = []
    ci = 0
    for pi, p in enumerate(polys):
        corners = []
        for k in range(3):
            vi = p[k]
            pos = (verts[3 * vi], verts[3 * vi + 1], verts[3 * vi + 2])
            corners.append((pos, corner_uv(ci), corner_nrm(ci, vi)))
            ci += 1
        tris.append((corners, poly_mat(pi)))
    return tris


# ---------------------------------------------------------------- axes and shading
def axis_map(name):
    """(x, y, z) in the file's axes -> the pack's x east, y up, z south.

    z-up is what 3ds Max and Blender write by default. The z flip is what puts the
    model's forward on the pack's forward; it is stated here and checked by eye,
    because an FBX says which axis is up but never which way the vehicle faces."""
    if name == "zup":
        return lambda p: (p[0], p[2], -p[1])
    if name == "yup":
        return lambda p: (p[0], p[1], p[2])
    if name == "zup-noflip":
        return lambda p: (p[0], p[2], p[1])
    raise SystemExit("unknown axis mapping %r" % name)


def shade(normal, mode):
    """A vertex colour for geometry that arrived without one. See SHADE_LO/HI."""
    if mode == "flat" or normal is None:
        return (255, 255, 255, 255)
    n = normal
    ln = math.sqrt(sum(c * c for c in n)) or 1.0
    d = sum(a * b for a, b in zip((c / ln for c in n), SHADE_DIR))
    # Half-lambert: the console's baked lighting has no black faces either, and a
    # plain clamp at zero leaves every underside a flat silhouette.
    t = 0.5 + 0.5 * d
    v = int(round(SHADE_LO + (SHADE_HI - SHADE_LO) * t))
    v = max(0, min(255, v))
    return (v, v, v, 255)


# ---------------------------------------------------------------- textures
def load_texture(path, texmax):
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    if max(w, h) > texmax:
        s = float(texmax) / max(w, h)
        w, h = max(1, int(round(w * s))), max(1, int(round(h * s)))
        im = im.resize((w, h), Image.LANCZOS)
    for d in (w, h):
        if d & (d - 1):
            raise SystemExit("%s is %dx%d: texture sides must be powers of two" %
                             (path, w, h))
    return w, h, im.tobytes()


def tread_strip(frames, w=32, h=32):
    """A placeholder running-track strip: `frames` frames stacked down one image, each
    the same pattern advanced by one link, so scrolling or stepping it reads as track
    moving under the vehicle. Greyscale, in the same tonal range as the delivered art.

    This is scaffolding, not art. It exists so the motion can be judged before the real
    strip is drawn, and it is written to disk beside the pack so it is visible rather
    than hidden inside one."""
    from PIL import Image
    LINK = 8                                    # rows per track link
    img = Image.new("RGBA", (w, h * frames))
    px = img.load()
    for f in range(frames):
        shift = int(round(f * LINK / float(frames)))
        for y in range(h):
            link = ((y + shift) % LINK)
            # A raised link face with a shadowed gap between links, and guide horns
            # down the middle of the track. The contrast is deliberately strong: the
            # renderer multiplies this by a vertex colour that is already dark, and a
            # subtle pattern at that point is a pattern nobody can see moving.
            band = 150 if link < 5 else 40
            for x in range(w):
                edge = 25 if (x < 3 or x >= w - 3) else 0
                horn = 55 if (w // 2 - 3 <= x < w // 2 + 3 and link < 4) else 0
                v = max(0, min(255, band - edge + horn))
                px[x, f * h + y] = (v, v, v, 255)
    return img


def project_tread_uv(verts, normals, pitch, frames):
    """Re-UV one track triangle onto a repeating strip.

    v runs ALONG the vehicle (pack z) and repeats every `pitch` model units, which is
    the direction the track travels and the only one that may be scrolled. u runs
    ACROSS the strip, and which world axis that is depends on the face: the outer and
    inner walls of a track face sideways, so their across direction is height, while
    the top and bottom runs face up, so theirs is width. The face normal picks between
    them, which is why an unwrap is not needed and a single planar projection would be
    wrong on half the faces.

    A flipbook's v is squeezed into the first frame's band, so the renderer steps
    frames by adding whole multiples of 1/frames."""
    nx = sum(abs(n[0]) for n in normals if n) if any(normals) else 0.0
    ny = sum(abs(n[1]) for n in normals if n) if any(normals) else 1.0
    across = 1 if nx > ny else 0          # 1 = use height (y), 0 = use width (x)
    out = []
    for (x, y, z) in verts:
        u = ((y if across else x) / pitch) % 1.0
        v = ((z / pitch) % 1.0) / float(frames)
        out.append((u, v))
    return out


def tex_record(w, h, rgba):
    return struct.pack("<IIII", w, h, w, h) + rgba + b"\x00"


# ---------------------------------------------------------------- the pack, spliced
def pack_offsets(path):
    """Byte offsets of the two regions this tool rewrites. The pack is written
    sequentially with no offset table, so a record can be replaced by splicing as long
    as the counts before it are patched."""
    b = open(path, "rb").read()
    r = PI.R(b)
    magic = r.raw(8)
    assert magic[:6] == b"CNC3DP", magic
    ver = r.u32()
    if ver >= 15:
        r.u32(), r.u32()
    r.name(16), r.name(16)
    ntex_at = r.o
    ntex = r.u32()
    for _ in range(ntex):
        w, h = r.u32(), r.u32()
        r.u32(), r.u32()
        r.raw(w * h * 4)
        if ver >= 6 and r.raw(1) == b"\x01":
            r.raw(w * h * 4)
    tex_end = r.o
    nmesh = r.u32()
    bounds = []
    for _ in range(nmesh):
        start = r.o
        nm = r.name(16)
        ntri = r.u32()
        r.raw(ntri * PI.TRI_STRIDE)
        nparts = r.u32()
        for _ in range(nparts):
            r.u32(), r.u32(), r.u32()
            r.f32(), r.f32(), r.f32()
        if ver >= 7:
            for _ in range(r.u32()):
                r.u32()
        if ver >= 11:
            nf, _tpf, nclip = r.u32(), r.u32(), r.u32()
            for _ in range(nclip):
                r.i32(), r.i32(), r.raw(1)
            if nf:
                r.raw(nf * nparts * 12 * 4)
                r.raw(nf * nparts)
        bounds.append((nm, start, r.o))
    return b, ver, ntex, ntex_at, tex_end, bounds


def mesh_record(name, tris, parts, ver):
    """One mesh record: name, triangles, parts, one section, no animation."""
    out = bytearray()
    out += name.encode("latin-1")[:15].ljust(16, b"\0")
    out += struct.pack("<I", len(tris))
    for tex, mode, wrap, verts in tris:
        rec = struct.pack("<i2B", tex, mode, wrap)
        for (x, y, z), (u, v), (r, g, b, a) in verts:
            rec += struct.pack("<5f4B", x, y, z, u, v, r, g, b, a)
        out += rec
    out += struct.pack("<I", len(parts))
    for tri0, nt, role, piv in parts:
        out += struct.pack("<III3f", tri0, nt, role, piv[0], piv[1], piv[2])
    if ver >= 7:
        # One section. Sections are the construction assembly order and only a
        # building uses them; a vehicle draws all its triangles at every state.
        out += struct.pack("<II", 1, 0)
    if ver >= 11:
        out += struct.pack("<III", 0, 0, 0)
    return bytes(out)


def role_of(node_name):
    n = node_name.lower()
    if "turret" in n:
        return ROLE_TURRET
    if "rotor" in n or "prop" in n:
        return ROLE_ROTOR
    return ROLE_STATIC


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("type", help="the unit or structure code, e.g. MTNK")
    ap.add_argument("fbx")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--axis", default="zup", choices=("zup", "yup", "zup-noflip"))
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--fit", action="store_true",
                    help="derive the scale from the two bounding boxes instead")
    ap.add_argument("--shade", default="normals", choices=("normals", "flat"))
    ap.add_argument("--texmax", type=int, default=256)
    ap.add_argument("--flipv", default="yes", choices=("yes", "no"),
                    help="FBX puts v=0 at the bottom of the image, the pack at the top")
    ap.add_argument("--tex", action="append", default=[], metavar="MATERIAL=PNG",
                    help="repeatable; defaults to <MATERIAL>_BaseColor.png beside the FBX")
    ap.add_argument("--xlu", action="append", default=[], metavar="MATERIAL",
                    help="repeatable: draw this material's faces translucent")
    ap.add_argument("--cutout", action="append", default=[], metavar="MATERIAL")
    ap.add_argument("--tread", metavar="MATERIAL",
                    help="this material's faces are running track: re-projected onto a "
                         "repeating strip and scrolled by the renderer as the vehicle moves")
    ap.add_argument("--treadpitch", type=float, default=160.0, metavar="UNITS",
                    help="model units the track pattern repeats over (default 160)")
    ap.add_argument("--treadframes", type=int, default=1, metavar="N",
                    help="1 scrolls a strip continuously; 2..15 steps a stacked flipbook")
    ap.add_argument("--treadtex", metavar="PNG",
                    help="the strip; when absent a placeholder is generated and written "
                         "out beside the pack")
    ap.add_argument("--pivot", action="append", default=[], metavar="PART=X,Y,Z")
    ap.add_argument("--report", action="store_true",
                    help="measure and print, change nothing")
    a = ap.parse_args()

    p = PI.read(a.pack)
    types = {t[0]: t[1] for t in p["types"]}
    if a.type not in types:
        raise SystemExit("%s carries no type %s" % (a.pack, a.type))
    mi = types[a.type]
    old = p["meshes"][mi]

    ver, models, objs, child = fbx_scene(a.fbx)
    if not models:
        raise SystemExit("no mesh nodes in %s" % a.fbx)
    # A turret must follow its hull, because a part's triangles are a contiguous run
    # and the pack's part order is what the renderer indexes.
    models.sort(key=lambda m: (role_of(m["name"]), m["name"]))

    conv = axis_map(a.axis)
    mats = []
    for m in models:
        for name in m["materials"]:
            if name not in mats:
                mats.append(name)

    # Geometry first, in the file's own axes, so a scale can be derived from it.
    geo = []
    for m in models:
        geo.append((m, triangles(m["geom"])))

    def bbox(points):
        xs = [q[0] for q in points]; ys = [q[1] for q in points]; zs = [q[2] for q in points]
        return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))

    # Each node's own translation, applied to its geometry before anything else.
    for m, _ts in geo:
        m["offset"] = node_offset(m["node"], m["name"])
        if any(abs(c) > 1e-6 for c in m["offset"]):
            print("  node %-28s offset (%.2f, %.2f, %.2f) applied"
                  % (m["name"], m["offset"][0], m["offset"][1], m["offset"][2]))

    def placed(m, pos):
        o = m["offset"]
        return (pos[0] + o[0], pos[1] + o[1], pos[2] + o[2])

    allpts = [conv(placed(m, c[0])) for m, ts in geo for corners, _mt in ts for c in corners]
    nb = bbox(allpts)
    oldpts = []
    for i in range(old["ntris"]):
        f = struct.unpack_from(PI.TRI_FMT, old["tris"], i * PI.TRI_STRIDE)
        for v in range(3):
            b = 3 + v * 9
            oldpts.append((f[b], f[b + 1], f[b + 2]))
    ob = bbox(oldpts)

    scale = a.scale
    if a.fit:
        sx = (ob[1] - ob[0]) / max(1e-6, nb[1] - nb[0])
        sz = (ob[5] - ob[4]) / max(1e-6, nb[5] - nb[4])
        scale = (sx + sz) / 2.0

    print("%s in %s: %d triangles, %d parts" % (a.type, os.path.basename(a.pack),
                                                old["ntris"], len(old["parts"])))
    print("  pack  bbox  x %8.1f..%8.1f  y %8.1f..%8.1f  z %8.1f..%8.1f" % ob)
    print("%s: %d nodes, %d triangles, materials %s, up axis %s" %
          (os.path.basename(a.fbx), len(models), sum(len(t) for _m, t in geo),
           ", ".join(mats), a.axis))
    print("  model bbox  x %8.1f..%8.1f  y %8.1f..%8.1f  z %8.1f..%8.1f  (scale %.4f)"
          % (nb[0] * scale, nb[1] * scale, nb[2] * scale, nb[3] * scale,
             nb[4] * scale, nb[5] * scale, scale))
    for m, ts in geo:
        print("  node %-28s %4d triangles  role %d" %
              (m["name"], len(ts), role_of(m["name"])))
    have_colour = any(t[0][0][2] is not None and m["geom"].first("LayerElementColor")
                      for m, t in geo)
    if not have_colour:
        print("  no vertex colour layer: shading baked with --shade %s" % a.shade)
    if a.report:
        return 0

    if a.tread and a.tread not in mats:
        raise SystemExit("--tread %r is not one of the materials: %s"
                         % (a.tread, ", ".join(mats)))
    if not 1 <= a.treadframes <= TREAD_MAX:
        raise SystemExit("--treadframes must be 1..%d" % TREAD_MAX)

    # Textures, one per material, appended to the bank so existing indices hold.
    texmap, newtex = {}, []
    given = dict(kv.split("=", 1) for kv in a.tex)
    base = os.path.dirname(os.path.abspath(a.fbx))
    for name in mats:
        if name == a.tread:
            path = a.treadtex
            if not path:
                path = os.path.splitext(a.out)[0] + "_tread_strip.png"
                tread_strip(a.treadframes).save(path)
                print("  no strip given: wrote placeholder %s (%d frame%s)" %
                      (os.path.basename(path), a.treadframes,
                       "" if a.treadframes == 1 else "s"))
        else:
            path = given.get(name) or os.path.join(base, "%s_BaseColor.png" % name)
        if not os.path.exists(path):
            raise SystemExit("no texture for material %r (looked for %s)" % (name, path))
        w, h, rgba = load_texture(path, a.texmax)
        texmap[name] = len(p["tex"]) + len(newtex)
        newtex.append(tex_record(w, h, rgba))
        print("  material %-16s -> texture %3d  %dx%d  %s%s" %
              (name, texmap[name], w, h, os.path.basename(path),
               "  RUNNING TRACK" if name == a.tread else ""))

    mode_of = {}
    for name in mats:
        mode_of[name] = (MODE_XLU if name in a.xlu else
                         MODE_CUTOUT if name in a.cutout else MODE_OPAQUE)

    flipv = (a.flipv == "yes")
    tris, parts, ntread = [], [], [0]
    for m, ts in geo:
        tri0 = len(tris)
        for corners, mt in ts:
            mname = m["materials"][mt] if mt < len(m["materials"]) else mats[0]
            istread = (mname == a.tread)
            pts = [conv(placed(m, pos)) for pos, _uv, _n in corners]
            pts = [(x * scale, y * scale, z * scale) for x, y, z in pts]
            nrms = [conv(n) if n else None for _p, _uv, n in corners]
            if istread:
                uvs = project_tread_uv(pts, nrms, a.treadpitch * scale, a.treadframes)
            else:
                uvs = [(uv[0], (1.0 - uv[1]) if flipv else uv[1])
                       for _p, uv, _n in corners]
            verts = [(pts[k], uvs[k], shade(nrms[k], a.shade)) for k in range(3)]
            wrap = (a.treadframes << TREAD_SHIFT) if istread else 0
            tris.append((texmap[mname], mode_of[mname], wrap, verts))
            ntread[0] += 1 if istread else 0
        parts.append([tri0, len(tris) - tri0, role_of(m["name"]), None])

    # Pivots: the pack's own, part for part, unless overridden. A remodelled turret
    # normally turns about the ring it replaces.
    override = {}
    for kv in a.pivot:
        k, v = kv.split("=", 1)
        override[int(k)] = tuple(float(c) for c in v.split(","))
    for i, part in enumerate(parts):
        if i in override:
            part[3] = override[i]
        elif i < len(old["parts"]):
            part[3] = old["parts"][i][3]
        else:
            part[3] = (0.0, 0.0, 0.0)
        print("  part %d: %d triangles, role %d, pivot (%.1f, %.1f, %.1f)" %
              (i, part[1], part[2], part[3][0], part[3][1], part[3][2]))

    if a.tread:
        print("  running track: %d of %d triangles, repeating every %.0f units, %d frame%s"
              % (ntread[0], len(tris), a.treadpitch * scale, a.treadframes,
                 "" if a.treadframes == 1 else "s"))
        if not ntread[0]:
            raise SystemExit("--tread %r matched no faces" % a.tread)

    blob, pver, ntex, ntex_at, tex_end, bounds = pack_offsets(a.pack)
    nm, mstart, mend = bounds[mi]
    if nm != old["name"]:
        raise SystemExit("mesh walk disagrees with the reader: %r vs %r" % (nm, old["name"]))
    rec = mesh_record(old["name"], tris, parts, pver)
    out = (blob[:ntex_at] + struct.pack("<I", ntex + len(newtex)) +
           blob[ntex_at + 4:tex_end] + b"".join(newtex) +
           blob[tex_end:mstart] + rec + blob[mend:])
    with open(a.out, "wb") as f:
        f.write(out)
    print("wrote %s  (%d -> %d triangles, %d -> %d textures, %+d bytes)" %
          (a.out, old["ntris"], len(tris), ntex, ntex + len(newtex),
           len(out) - len(blob)))

    # Read the result back with the pack's own reader, so a pack that cannot be
    # parsed is a failure here rather than a silent short read in the renderer.
    q = PI.read(a.out)
    m2 = q["meshes"][mi]
    assert m2["name"] == old["name"] and m2["ntris"] == len(tris), "round trip failed"
    assert len(q["tex"]) == ntex + len(newtex)
    assert q["trailing_bytes"] == p["trailing_bytes"], "the tail moved"
    assert [x["name"] for x in q["meshes"]] == [x["name"] for x in p["meshes"]]
    print("verified: %s reads back with %d meshes, %d textures, tail intact" %
          (os.path.basename(a.out), len(q["meshes"]), len(q["tex"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
