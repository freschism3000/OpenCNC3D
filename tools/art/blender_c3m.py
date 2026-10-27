"""Export an authored Blender vehicle to a .c3m model for the Enhanced art set.

New assets must follow the model authoring notes as the canonical stage and
shared-look entry point. Use its Blender material/chamfer kit, audit the exported
C3M with tools/art/style_audit.py, and review beside the Mammoth and Construction
Yard in the fixed USER92 engine view. Export validity alone is not visual approval.

Run it inside Blender, never as a plain script. This is the exact invocation that
produced the shipped Mammoth, so the file can be rebuilt from the authoring source:

    blender --background RUNTIMERIG.blend --python tools/art/blender_c3m.py -- \
            --type HTNK --select scene --turret-bone TURRET_YAW \
            --pivot 3.52361989,222.71240234,-35.11024094 \
            --mat Atlas=Mammoth_Atlas:1024 \
            --mat Rocket=Mammoth_Rocket:256 \
            --mat Tread_L=Mammoth_Tread_L:256:tread,noplayer \
            --mat Tread_R=Mammoth_Tread_R:256:tread,noplayer \
            --out game/models3d/HTNK.c3m

THE EARLIER INVOCATION STILL WORKS AND IS KEPT, because a second delivery may well be
shaped like the first: unrigged, one atlas, one belt, and the turret stated by a shared
object origin rather than by a bone.

    blender --background MAMMOTH.blend --python tools/art/blender_c3m.py -- \
            --type HTNK --prefix LOOK05_QA__ --exclude Ground \
            --turret Turret_Armor --texmax 1024 --treadtex 256 \
            --out game/models3d/HTNK.c3m

WHY A SECOND MODEL FILE AND NOT THE MISSION PACK. A mission .pack is the cartridge's
own art: a flat triangle list with one 16x16 texture per part and the console's
lighting baked into the vertex colours, because the renderer that draws it has no
lights and multiplies texture by vertex colour. An authored vehicle is the opposite
shape -- thousands of triangles, one large atlas, and a base colour that is only
correct once a light has been applied to it. Squeezing the second into the first
would throw away everything that makes it worth having, so it gets a file of its own
and a draw path of its own, the way the solid tiberium crystals already do. The pack
keeps the cartridge model, which is what Classic Visuals still draws.

WHAT THIS DECIDES, because Blender states none of it:

  Axes and scale. Blender is z-up and the model is authored in MAP CELLS. The pack's
      frame is x east, y UP, z south at 1024 units to the cell, so a vertex maps
      (X, Y, Z) -> (1024*X, 1024*Z, -1024*Y). The sign on the last one is measured,
      not assumed: --report prints the result beside the cartridge mesh's own bounding
      box, and a tank whose barrels point backwards is a sign flip.

  Parts and pivots. A part is a run of triangles plus a pivot the renderer may rotate
      it about. Objects are grouped by their OBJECT ORIGIN: every object sharing an
      origin away from the world origin is one part, and that origin is its pivot.
      That is how an authored turret arrives -- its pieces are modelled about the ring
      they turn on. --turret names which of those groups the engine's turret facing
      drives; the rest are static.

  Materials. Three maps per material, packed here rather than at load:
      BASE   rgb = base colour (sRGB), a = the player-colour mask
      NORM   rgb = tangent-space normal, OpenGL +Y
      ORM    r = ambient occlusion, g = roughness, b = metallic
      Packing them costs nothing and saves three texture units and two thirds of the
      binds in the draw.

  Resolution. The authoring atlas is 4096 on a side, which is a working resolution and
      not a runtime budget: as RGBA it is 64 MB per map. --texmax resamples it, 1024 by
      default, which is about 4 MB per map and more than a vehicle forty pixels tall
      can show.

WHAT IT DOES NOT DO, and the first line of this changed the day a rigged delivery came.

  NO SKINNING, still, but now as a choice rather than an absence. A rigid delivery
      binds each mesh to ONE bone at full weight, so there is nothing to blend and the
      bone is really a label saying which part a mesh belongs to. --turret-bone reads
      exactly that much of an armature and no more. A genuinely deforming rig would
      need per-vertex weights in the file and a skinning path in the renderer.

  NO ARTICULATION BELOW THE TURRET. A rigged delivery may author barrel recoil,
      launcher pitch, per-tube rocket visibility and destruction groups. The .c3m part
      record carries a role, a pivot and a name and NOTHING ELSE: no parent, no local
      transform, no visibility bit. So a hierarchy cannot be expressed in version 1 of
      the format however plainly the authoring file states it, and everything below the
      turret is flattened into the part it rides. That is correct at rest, and still at
      rest when the gun fires. Carrying it means a version 2, which is safe to attempt:
      a v2 file meets a clean refusal on a v1 build rather than being misread.

  NO NODE ANIMATION, no construction sections, and no second house texture set: the
      player colour arrives as a mask multiplied by the house tint at draw time
      instead, which is what the mask is for.
"""
import argparse
import os
import struct
import sys

import bpy
import mathutils
import numpy

MAGIC = b"C3DMODL1"
FORMAT_VERSION = 1

ROLE_STATIC, ROLE_TURRET = 0, 1

MAT_TREAD = 1 << 0          # scrolls v with distance travelled
MAT_NO_PLAYER = 1 << 1      # ignore the player mask even where it is not black

CELL = 1024.0               # model units to the map cell


# ---------------------------------------------------------------------------------- #
#  Blender side
# ---------------------------------------------------------------------------------- #

def link_everything():
    """Put every object in the scene so its world matrix is actually evaluated.

    An authoring file can hold objects in collections that are not under the scene's
    own, and an object the view layer never sees keeps a world matrix of identity
    however its location reads. Reading that stale matrix silently drops the turret
    onto the hull floor, which looks like a modelling mistake rather than a missing
    update, so this is done before anything is measured.
    """
    sc = bpy.context.scene
    for ob in bpy.data.objects:
        if ob.name not in sc.collection.objects:
            try:
                sc.collection.objects.link(ob)
            except RuntimeError:
                pass
    bpy.context.view_layer.update()


def visible_meshes(prefix, exclude, scene_only=False):
    """The meshes to export.

    TWO SELECTION RULES, because authoring files come in two shapes and picking the
    wrong rule silently exports the wrong model.

    THE WHOLE FILE (the default). Every mesh datablock, wherever it lives. This is
    right when the file holds one model and some of it sits in collections the scene
    never references, which is the case link_everything exists for.

    THE SCENE ONLY (--select scene). Just what the scene collection references. This
    is right when the file is an AUTHORING CHECKPOINT that carries its own history:
    high-poly sources, lookdev reviews, draft bakes. One such delivery holds 2,657
    mesh objects and 974,062 triangles, of which the 218 objects and 9,990 triangles
    the scene references are the model and everything else is the workings. Exporting
    the whole file there does not fail; it produces a model two orders of magnitude
    too heavy, built from meshes the delivery never meant to ship.
    """
    pool = bpy.context.scene.objects if scene_only else bpy.data.objects
    out = []
    for o in pool:
        if o.type != "MESH" or o.hide_render:
            continue
        if prefix and not o.name.startswith(prefix):
            continue
        short = o.name[len(prefix):] if prefix else o.name
        if short in exclude:
            continue
        out.append((short, o))
    out.sort(key=lambda p: p[0])
    return out


def bone_of(obj):
    """The one bone a rigidly bound mesh hangs off, or None.

    A rigid delivery binds each mesh to a single bone at full weight, so the binding
    is one vertex group and not a weight blend. Anything with more than one group is
    refused by name rather than guessed at: a mesh spanning two bones cannot be a
    rigid part, and averaging it would put geometry somewhere the author never placed.
    """
    if len(obj.vertex_groups) != 1:
        return None
    return obj.vertex_groups[0].name


def bone_chain(arm, name):
    """A bone and every ancestor, so a part can be decided by what it rides."""
    out = []
    b = arm.data.bones.get(name) if arm else None
    while b:
        out.append(b.name)
        b = b.parent
    return out


def origin_key(obj):
    """The object's world origin, rounded, as the key a part is grouped by."""
    t = obj.matrix_world.translation
    return (round(t.x, 5), round(t.y, 5), round(t.z, 5))


def to_pack(v):
    """Blender cells (z up) -> pack units (y up, z south)."""
    return (v.x * CELL, v.z * CELL, -v.y * CELL)


def to_pack_dir(v):
    n = mathutils.Vector((v.x, v.z, -v.y))
    if n.length > 1e-9:
        n.normalize()
    return (n.x, n.y, n.z)


# ---------------------------------------------------------------------------------- #
#  Texture packing
# ---------------------------------------------------------------------------------- #

def image_rgba(img, size):
    """An image as a size x size x 4 float array.

    Blender hands back RGBA floats with the BOTTOM row first, which is already the
    order glTexImage2D wants for v increasing upward, so nothing is flipped and the
    loader uploads these bytes as they stand. A map whose side is an exact multiple
    of the target is box filtered, which
    is what a mip reduction does and is the right answer for every channel here; a
    ratio that is not exact falls back to point sampling rather than inventing a
    resampler for a case this pipeline does not produce.
    """
    w, h = img.size
    buf = numpy.empty(w * h * 4, dtype=numpy.float32)
    img.pixels.foreach_get(buf)
    a = buf.reshape(h, w, 4)
    if w % size == 0 and h % size == 0:
        fx, fy = w // size, h // size
        a = a.reshape(size, fy, size, fx, 4).mean(axis=(1, 3))
    else:
        yi = (numpy.arange(size) * h) // size
        xi = (numpy.arange(size) * w) // size
        a = a[yi][:, xi]
    return a


def linear_to_srgb(a):
    return numpy.where(a <= 0.0031308, a * 12.92,
                       1.055 * numpy.power(numpy.clip(a, 0.0, None), 1.0 / 2.4) - 0.055)


def as_bytes(a):
    return numpy.clip(a * 255.0 + 0.5, 0.0, 255.0).astype(numpy.uint8)


def is_srgb(img):
    return img is not None and img.colorspace_settings.name == "sRGB"


def pack_base(colour_img, mask_img, size):
    """rgb = base colour, 8-bit sRGB encoded; a = the player-colour mask.

    An image Blender tags sRGB is handed to Python already LINEARISED, so it has to be
    encoded back before it becomes eight bit or the whole model ships washed out. A
    Non-Color image is handed over as stored and is written straight through.
    """
    c = image_rgba(colour_img, size)[:, :, :3]
    if is_srgb(colour_img):
        c = linear_to_srgb(c)
    out = numpy.zeros((size, size, 4), dtype=numpy.uint8)
    out[:, :, :3] = as_bytes(c)
    if mask_img is not None:
        m = image_rgba(mask_img, size)[:, :, 0]
        if is_srgb(mask_img):
            m = linear_to_srgb(m)
        out[:, :, 3] = as_bytes(m)
    return out.tobytes()


def pack_normal(norm_img, size):
    n = image_rgba(norm_img, size)[:, :, :3] * 2.0 - 1.0
    ln = numpy.sqrt(numpy.maximum((n * n).sum(axis=2, keepdims=True), 1e-12))
    n = n / ln
    out = numpy.zeros((size, size, 4), dtype=numpy.uint8)
    out[:, :, :3] = as_bytes(n * 0.5 + 0.5)
    out[:, :, 3] = 255
    return out.tobytes()


def chan(img, size, default):
    if img is None:
        return numpy.full((size, size), default, dtype=numpy.float32)
    c = image_rgba(img, size)[:, :, 0]
    return linear_to_srgb(c) if is_srgb(img) else c


def pack_orm(ao_img, rough_img, metal_img, size):
    out = numpy.zeros((size, size, 4), dtype=numpy.uint8)
    out[:, :, 0] = as_bytes(chan(ao_img, size, 1.0))
    out[:, :, 1] = as_bytes(chan(rough_img, size, 0.8))
    out[:, :, 2] = as_bytes(chan(metal_img, size, 0.0))
    out[:, :, 3] = 255
    return out.tobytes()


def find_image(names):
    """The first image matching any of these names, preferring an exact hit.

    A .001 SUFFIX IS A DUPLICATE, NOT A CANDIDATE. Blender renames on collision, so an
    authoring file that imported a map twice holds Foo and Foo.001 with the same
    pixels. Either would do here, but which one a scan returns depends on datablock
    order, and a texture chosen by datablock order is a model that can change between
    two exports of one file. The undecorated name wins.
    """
    def undecorated(img):
        tail = img.name.rsplit(".", 1)[-1]
        return not (len(tail) == 3 and tail.isdigit())

    # A DATABLOCK WITH NO PIXELS IS NOT A MAP, and saying so here is the difference
    # between a refusal and a black texture. An authoring file may reference its images
    # rather than pack them, in which case moving the .blend away from the folder beside
    # it leaves every datablock present, named correctly, and zero by zero. Returning one
    # of those would sail past the "has no base map" check below and bake a black atlas,
    # which is a model that loads, draws, and is wrong.
    live = [i for i in bpy.data.images if i.size[0] and i.size[1]]

    for n in names:
        img = bpy.data.images.get(n)
        if img is not None and img.size[0] and img.size[1]:
            return img
    for n in names:
        hits = [i for i in live if i.name.endswith(n) or n in i.name]
        if not hits:
            continue
        hits.sort(key=lambda i: (not undecorated(i), i.name))
        return hits[0]
    return None


def material_maps(stem):
    """The six maps a PBR material is delivered as, by their conventional suffixes."""
    return {
        "base":  find_image([stem + "_BaseColorB", stem + "_BaseColor"]),
        "mask":  find_image([stem + "_PlayerColorMask"]),
        "norm":  find_image([stem + "_Normal"]),
        "ao":    find_image([stem + "_AO"]),
        "rough": find_image([stem + "_Roughness"]),
        "metal": find_image([stem + "_Metallic"]),
    }


def parse_mat_spec(spec):
    """NAME=STEM:SIZE[:flag,flag] -> (name, stem, size, flags).

    NAME is matched against the tail of the Blender material's name and is what the
    file records. STEM is the image name the six maps hang off. The flags are the
    renderer's, not the art's: `tread` scrolls v with distance travelled and takes the
    TreadUV set, `noplayer` refuses the player tint however the mask reads.
    """
    if "=" not in spec:
        raise ValueError("expected NAME=STEM:SIZE, got %r" % spec)
    name, rest = spec.split("=", 1)
    bits = rest.split(":")
    if len(bits) < 2:
        raise ValueError("expected a size in %r" % spec)
    stem, size = bits[0], int(bits[1])
    flags = 0
    for f in (bits[2].split(",") if len(bits) > 2 and bits[2] else []):
        f = f.strip()
        if f == "tread":
            flags |= MAT_TREAD
        elif f == "noplayer":
            flags |= MAT_NO_PLAYER
        elif f:
            raise ValueError("unknown material flag %r" % f)
    return (name, stem, size, flags)


# ---------------------------------------------------------------------------------- #
#  The build
# ---------------------------------------------------------------------------------- #

class Builder(object):
    def __init__(self):
        self.verts = []         # (px,py,pz, nx,ny,nz, u,v)
        self.index = {}
        self.idx = []
        self.parts = []         # (role, pivot, name)
        self.subs = []          # (part, mat, idx0, nidx)
        self.mats = []          # (base, norm, orm, flags, name)
        self.texs = []          # (w, h, bytes)

    def add_vert(self, key):
        got = self.index.get(key)
        if got is None:
            got = len(self.verts)
            self.index[key] = got
            self.verts.append(key)
        return got

    def add_tex(self, w, h, data):
        self.texs.append((w, h, data))
        return len(self.texs) - 1


def collect(builder, objs, part_groups, mat_of_slot, uv_of_mat):
    """Emit one submesh per (part, material), in part then material order."""
    for pi, (key, role, name, members) in enumerate(part_groups):
        builder.parts.append((role, key, name))
        per_mat = {}
        for short, obj in members:
            mesh = obj.data
            mw = obj.matrix_world
            nm = mw.to_3x3().inverted_safe().transposed()
            mesh.calc_loop_triangles()
            corner_n = None
            try:
                corner_n = [tuple(c.vector) for c in mesh.corner_normals]
            except (AttributeError, RuntimeError):
                corner_n = None
            for tri in mesh.loop_triangles:
                slot = tri.material_index
                mat_name = mat_of_slot(obj, slot)
                mi = None
                for k, m in enumerate(builder.mats):
                    if m[4] == mat_name:
                        mi = k
                        break
                if mi is None:
                    continue        # a material the caller did not register
                uv_layer = mesh.uv_layers.get(uv_of_mat(mat_name))
                if uv_layer is None:
                    uv_layer = mesh.uv_layers.active
                bucket = per_mat.setdefault(mi, [])
                for li, loop in enumerate(tri.loops):
                    vi = tri.vertices[li]
                    co = mw @ mesh.vertices[vi].co
                    if corner_n is not None:
                        n = nm @ mathutils.Vector(corner_n[loop])
                    elif tri.use_smooth:
                        n = nm @ mesh.vertices[vi].normal
                    else:
                        n = nm @ tri.normal
                    uv = uv_layer.uv[loop].vector if uv_layer else (0.0, 0.0)
                    p = to_pack(co)
                    d = to_pack_dir(n)
                    key_v = (round(p[0], 3), round(p[1], 3), round(p[2], 3),
                             round(d[0], 4), round(d[1], 4), round(d[2], 4),
                             round(uv[0], 5), round(uv[1], 5))
                    bucket.append(builder.add_vert(key_v))
        for mi in sorted(per_mat.keys()):
            ids = per_mat[mi]
            if not ids:
                continue
            idx0 = len(builder.idx)
            builder.idx.extend(ids)
            builder.subs.append((pi, mi, idx0, len(ids)))


def write_c3m(path, b, mtype):
    bb = [1e9, 1e9, 1e9, -1e9, -1e9, -1e9]
    for v in b.verts:
        for k in range(3):
            if v[k] < bb[k]:
                bb[k] = v[k]
            if v[k] > bb[3 + k]:
                bb[3 + k] = v[k]
    out = bytearray()
    out += MAGIC
    out += struct.pack("<8I", FORMAT_VERSION, len(b.verts), len(b.idx), len(b.parts),
                       len(b.subs), len(b.mats), len(b.texs), 0)
    name = mtype.encode("ascii")[:8]
    out += name + b"\0" * (8 - len(name))
    out += struct.pack("<6f", *bb)
    for v in b.verts:
        out += struct.pack("<8f", *v)
    for i in b.idx:
        out += struct.pack("<I", i)
    for role, pivot, pname in b.parts:
        p = to_pack(mathutils.Vector(pivot))
        nb = pname.encode("ascii")[:16]
        out += struct.pack("<I3f", role, p[0], p[1], p[2])
        out += nb + b"\0" * (16 - len(nb))
    for part, mat, idx0, n in b.subs:
        out += struct.pack("<4I", part, mat, idx0, n)
    for base, norm, orm, flags, mname in b.mats:
        out += struct.pack("<3iI", base, norm, orm, flags)
    for w, h, data in b.texs:
        out += struct.pack("<3I", w, h, len(data))
        out += data
    with open(path, "wb") as f:
        f.write(bytes(out))
    return len(out), bb


def finish(b, args):
    """Write the file and report it. Shared by both material routes, so a
    model built either way is described to the log in exactly one voice."""

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    size, bb = write_c3m(args.out, b, args.type)

    print("C3M|type|%s" % args.type)
    print("C3M|parts|%d" % len(b.parts))
    for i, (role, pivot, pname) in enumerate(b.parts):
        p = to_pack(mathutils.Vector(pivot))
        n = sum(s[3] for s in b.subs if s[0] == i) // 3
        print("C3M|part|%d|%s|role %d|tris %d|pivot %.2f %.2f %.2f"
              % (i, pname, role, n, p[0], p[1], p[2]))
    print("C3M|verts|%d|tris|%d|subs|%d|mats|%d|texs|%d"
          % (len(b.verts), len(b.idx) // 3, len(b.subs), len(b.mats), len(b.texs)))
    for i, (w, h, data) in enumerate(b.texs):
        print("C3M|tex|%d|%dx%d|%d bytes" % (i, w, h, len(data)))
    print("C3M|bbox|x %.1f..%.1f|y %.1f..%.1f|z %.1f..%.1f"
          % (bb[0], bb[3], bb[1], bb[4], bb[2], bb[5]))
    print("C3M|cells|w %.3f|h %.3f|d %.3f"
          % ((bb[3] - bb[0]) / CELL, (bb[4] - bb[1]) / CELL, (bb[5] - bb[2]) / CELL))
    print("C3M|wrote|%s|%d bytes" % (args.out, size))
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--type", required=True, help="the engine's type name, e.g. HTNK")
    ap.add_argument("--prefix", default="", help="only objects whose name starts here")
    ap.add_argument("--select", default="all", choices=["all", "scene"],
                    help="which meshes to consider: every datablock in the file, or "
                         "only what the scene collection references. See visible_meshes")
    ap.add_argument("--exclude", default="", help="comma separated short names to drop")
    ap.add_argument("--turret", default="",
                    help="comma separated short names that ride the turret; their "
                         "shared object origin becomes the turret pivot")
    ap.add_argument("--turret-bone", default="",
                    help="the yaw bone; every mesh whose bone descends from it rides "
                         "the turret. Replaces --turret for a rigged delivery")
    ap.add_argument("--pivot", default="",
                    help="the turret pivot in PACK units, x,y,z. Use the authored "
                         "figure when the delivery states one; a pivot derived from "
                         "geometry is a guess and a wrong one wobbles")
    ap.add_argument("--mat", action="append", default=[],
                    metavar="NAME=STEM:SIZE[:flags]",
                    help="declare one material and the image stem its six maps hang "
                         "off; repeatable. Replaces --atlas-material/--tread-material "
                         "for a delivery with more than one belt texture")
    ap.add_argument("--atlas-material", default="Atlas")
    ap.add_argument("--tread-material", default="Belt")
    ap.add_argument("--texmax", type=int, default=1024)
    ap.add_argument("--treadtex", type=int, default=256)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)

    # LINKING IS PART OF THE WHOLE-FILE RULE AND ONLY THAT RULE. Under --select scene
    # every object considered is already in the scene, so its world matrix is already
    # evaluated and there is nothing to fix; linking the rest in would do the precise
    # damage the selection rule exists to avoid, by dragging the authoring history back
    # into the pool it was just excluded from.
    turret_pivot = None
    if args.pivot:
        try:
            xyz = [float(v) for v in args.pivot.split(",")]
        except ValueError:
            xyz = []
        if len(xyz) != 3:
            print("C3M|FAIL|--pivot wants three comma separated pack units, got %r"
                  % args.pivot)
            return 2
        # IN PACK UNITS AT THE DOOR, BLENDER UNITS EVERYWHERE AFTER IT. A delivery
        # states its pivot the way the renderer will read it, which is the useful way
        # round for whoever types it, but everything downstream here holds Blender
        # coordinates and converts once at write. So this inverts the axis map now
        # rather than leaving a single value in a different frame to the rest:
        #   (X, Y, Z) -> (1024X, 1024Z, -1024Y)  becomes
        #   (px, py, pz) -> (px/1024, -pz/1024, py/1024)
        turret_pivot = (xyz[0] / CELL, -xyz[2] / CELL, xyz[1] / CELL)

    scene_only = (args.select == "scene")
    if not scene_only:
        link_everything()
    else:
        bpy.context.view_layer.update()
    prefix = args.prefix
    exclude = set(x for x in args.exclude.split(",") if x)
    objs = visible_meshes(prefix, exclude, scene_only)
    if not objs:
        print("C3M|FAIL|no visible meshes matched prefix %r" % prefix)
        return 2

    # ---- parts ----
    #
    # TWO WAYS TO DECIDE WHAT RIDES THE TURRET, and which one is right is a fact about
    # the delivery rather than a preference.
    #
    # BY SHARED OBJECT ORIGIN (the default). An unrigged model states its turret by
    # modelling every piece of it about the ring it turns on, so the pieces share an
    # origin away from the world origin and that origin IS the pivot. --turret names
    # one member of that group.
    #
    # BY BONE (--turret-bone). A rigged delivery states it outright: every mesh is
    # bound to one bone, and whether a mesh turns with the turret is whether its bone
    # descends from the yaw bone. This is not a refinement of the origin rule, it
    # replaces it -- 218 rigid parts have 218 origins, so grouping them by origin
    # yields 218 groups and puts all but one of them in the hull.
    part_groups = []
    if args.turret_bone:
        arm = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
        if arm is None:
            arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
        if arm is None:
            print("C3M|FAIL|--turret-bone needs an armature and this file has none")
            return 2
        if args.turret_bone not in arm.data.bones:
            print("C3M|FAIL|no bone named %r in %s" % (args.turret_bone, arm.name))
            return 2
        turret_members, static_members, loose = [], [], []
        for short, obj in objs:
            bn = bone_of(obj)
            if bn is None:
                loose.append(short)
                static_members.append((short, obj))
                continue
            if args.turret_bone in bone_chain(arm, bn):
                turret_members.append((short, obj))
            else:
                static_members.append((short, obj))
        if loose:
            # NOT SILENT. A mesh with no single binding has been put in the hull, which
            # is a guess; it is stated so it can be corrected in the authoring file
            # rather than discovered as a piece that does not turn.
            print("C3M|WARN|%d mesh(es) carry no single bone binding and were put in "
                  "the hull: %s" % (len(loose), ", ".join(sorted(loose)[:8])))
        print("C3M|parts|by bone %s: turret=%d mesh(es) hull=%d"
              % (args.turret_bone, len(turret_members), len(static_members)))
        part_groups.append(((0.0, 0.0, 0.0), ROLE_STATIC, "static",
                            sorted(static_members)))
        if turret_members:
            part_groups.append((turret_pivot, ROLE_TURRET, "turret",
                                sorted(turret_members)))
    else:
        turret_names = set(x for x in args.turret.split(",") if x)
        groups = {}
        for short, obj in objs:
            groups.setdefault(origin_key(obj), []).append((short, obj))
        turret_key = None
        if turret_names:
            for key, members in groups.items():
                if any(s in turret_names for s, _ in members):
                    turret_key = key
                    break
        static_members = []
        for key, members in sorted(groups.items()):
            if key == turret_key:
                continue
            static_members.extend(members)
        part_groups.append(((0.0, 0.0, 0.0), ROLE_STATIC, "static",
                            sorted(static_members)))
        if turret_key is not None:
            part_groups.append((turret_pivot or turret_key, ROLE_TURRET, "turret",
                                sorted(groups[turret_key])))

    b = Builder()

    # ---- materials and their maps ----
    #
    # TWO ROUTES AGAIN, for the same reason the parts have two. The named route below
    # knows one atlas and one belt because that is what the first delivery had. A
    # delivery with four materials, two of them a left and a right belt with DIFFERENT
    # textures, cannot be described that way at all: the old route builds both belt
    # materials out of one set of images, which is correct only while the two sides
    # are the same picture.
    if args.mat:
        try:
            specs = [parse_mat_spec(x) for x in args.mat]
        except ValueError as e:
            print("C3M|FAIL|--mat: %s" % e)
            return 2
        mat_flags = {}
        for name, stem, size, flags in specs:
            maps = material_maps(stem)
            need = [k for k in ("base", "norm", "rough") if maps[k] is None]
            if need:
                print("C3M|FAIL|material %s (stem %s) has no %s map"
                      % (name, stem, ", ".join(need)))
                return 2
            t_base = b.add_tex(size, size, pack_base(maps["base"], maps["mask"], size))
            t_norm = b.add_tex(size, size, pack_normal(maps["norm"], size))
            t_orm = b.add_tex(size, size,
                              pack_orm(maps["ao"], maps["rough"], maps["metal"], size))
            b.mats.append((t_base, t_norm, t_orm, flags, name))
            mat_flags[name] = flags
            print("C3M|material|%s|stem=%s|%dx%d|maps=%s|flags=%d"
                  % (name, stem, size, size,
                     "".join("1" if maps[k] else "0"
                             for k in ("base", "mask", "norm", "ao", "rough", "metal")),
                     flags))

        mat_names = [m[4] for m in b.mats]

        def mat_of_slot(obj, slot):
            if slot < len(obj.material_slots) and obj.material_slots[slot].material:
                nm = obj.material_slots[slot].material.name
                # LONGEST SUFFIX WINS. "Tread_L" and "Atlas" cannot be confused, but
                # a set holding both "Tread" and "Tread_L" can be, and the first match
                # in declaration order would be the shorter one.
                best = None
                for cand in mat_names:
                    if nm.endswith(cand) and (best is None or len(cand) > len(best)):
                        best = cand
                if best is not None:
                    return best
            return mat_names[0]

        def uv_of_mat(name):
            return "TreadUV" if (mat_flags.get(name, 0) & MAT_TREAD) else "BakeUV"

        collect(b, objs, part_groups, mat_of_slot, uv_of_mat)
        return finish(b, args)

    atlas_base = find_image(["Atlas_BaseColorB", "BaseColorB", "BaseColor"])
    atlas_mask = find_image(["Atlas_PlayerColorMask", "PlayerColorMask"])
    atlas_norm = find_image(["Atlas_Normal", "_Normal"])
    atlas_ao = find_image(["Atlas_AO", "_AO"])
    atlas_rough = find_image(["Atlas_Roughness", "Roughness"])
    atlas_metal = find_image(["Atlas_Metallic", "Metallic"])
    tread_base = find_image(["Tread_BaseColor"])
    tread_mask = find_image(["Tread_PlayerColorMask"])
    tread_norm = find_image(["Tread_Normal"])
    tread_ao = find_image(["Tread_AO"])
    tread_rough = find_image(["Tread_Roughness"])
    tread_metal = find_image(["Tread_Metallic"])
    missing = [n for n, i in (("base", atlas_base), ("normal", atlas_norm),
                              ("roughness", atlas_rough)) if i is None]
    if missing:
        print("C3M|FAIL|atlas maps not found: %s" % ", ".join(missing))
        return 2

    ts = args.texmax
    a_base = b.add_tex(ts, ts, pack_base(atlas_base, atlas_mask, ts))
    a_norm = b.add_tex(ts, ts, pack_normal(atlas_norm, ts))
    a_orm = b.add_tex(ts, ts, pack_orm(atlas_ao, atlas_rough, atlas_metal, ts))
    b.mats.append((a_base, a_norm, a_orm, 0, args.atlas_material))

    if tread_base is not None:
        tt = args.treadtex
        t_base = b.add_tex(tt, tt, pack_base(tread_base, tread_mask, tt))
        t_norm = b.add_tex(tt, tt, pack_normal(tread_norm, tt)) if tread_norm else a_norm
        t_orm = b.add_tex(tt, tt, pack_orm(tread_ao, tread_rough, tread_metal, tt))
        for side in ("L", "R"):
            b.mats.append((t_base, t_norm, t_orm, MAT_TREAD | MAT_NO_PLAYER,
                           args.tread_material + "_" + side))

    mat_names = [m[4] for m in b.mats]

    def mat_of_slot(obj, slot):
        if slot < len(obj.material_slots) and obj.material_slots[slot].material:
            nm = obj.material_slots[slot].material.name
            for cand in mat_names:
                if nm.endswith(cand):
                    return cand
        return mat_names[0]

    def uv_of_mat(name):
        return "TreadUV" if name.startswith(args.tread_material) else "BakeUV"

    collect(b, objs, part_groups, mat_of_slot, uv_of_mat)
    return finish(b, args)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
