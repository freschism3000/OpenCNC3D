#!/usr/bin/env python3
"""
CNC3D -- objgraph, EXTENDED: scene-graph walking across BOTH model segments,
plus per-part mount transforms. Drop-in superset of tools/romdump/objgraph.py
(node_ptr / walk_node / display_lists keep their signatures and their GMG
behaviour bit-for-bit; they simply now also accept ScriptModels pointers).

WHY. The old walker only accepted pointers into GameModelsGeometry
(RAM 0x8015A7A0..0x801C0980). But nine model-table slots point their scene-graph
node into the ScriptModels segment (RAM 0x80122B60..0x801360E0, ROM = RAM -
0x8005BCC0): TMPL, FACT, SAM, FTNK, STNK, HTNK, BOAT, TRAN, ORCA. For those the
old walker returned nothing, the baker fell back to the bare root display list,
and the models lost their moving parts (HTNK its turret, TRAN its two rotors,
BOAT its deck gun, FACT its 10-part yard, SAM its 7-part launcher). The segment
bounds and deltas come straight from the loader's own segment table (see
tools/romdump/segments.py); the earlier wrong end 0x80130830 was ReplayMission's
RAM base, not ScriptModels' end.

MOUNT TRANSFORMS (proven by disassembly, see tools/mount/mount.py):
    node (0x1C bytes)  +0x00 Gfx* dl   +0x08 Handle* {u32 tagPtr, void* data}
                       +0x0C Node** children (NULL-terminated)
    tag 0x800AC2C0 ("CAFEDEAD"): data = static pose
        +0x00 float3 T   +0x0C float4 quat wxyz   +0x1C float3 S
        +0x28 float4 scale-orientation quat       +0x38 float mirror (-1 negates)
    tag 0x800AC2D8 ("BEEFED02"): runtime-animated part. data = {curvesA, stateB};
        curvesA = four 0x1C curve descriptors T(3) R(4) S(3) So(4); the REST pose
        is each curve's first key value. These are exactly the parts the game
        moves at runtime: turrets, rotors, the weapons-factory doors.

The scene-graph blocks are authored Z-up while the display lists are Y-up; the
whole accumulated transform is conjugated by P_AUTHOR_TO_MESH exactly as
mount.py does (row-vector convention: world = local @ M + t).

No capstone dependency: this file only reads data structures, it disassembles
nothing.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# ROM: private sharecopy first, then the canonical layout when this file is
# dropped into share/CNC3D/tools/romdump.
_ROM_CANDIDATES = [
    os.path.join(HERE, "sharecopy", "rom", "cnc_eu.z64"),
    os.path.abspath(os.path.join(HERE, "..", "..", "rom", "cnc_eu.z64")),
]

RES_DELTA = 0x80000400 - 0x1000              # resident: rom = ram - delta
MODEL_TABLE = 0x80099998
TABLE_SLOTS = 236
NODE_SIZE = 0x1C
MAX_CHILDREN = 8

# name, ram_lo, ram_hi, delta (rom = ram - delta)
GMG_LO, GMG_HI, GMG_DELTA = 0x8015A7A0, 0x801C0980, 0x8005C590
SM_LO, SM_HI, SM_DELTA = 0x80122B60, 0x801360E0, 0x8005BCC0
SEGS = [
    ("GameModelsGeometry", GMG_LO, GMG_HI, GMG_DELTA),
    ("ScriptModels", SM_LO, SM_HI, SM_DELTA),
]

TAG_CAFEDEAD = 0x800AC2C0
TAG_DEADCAFE = 0x800AC2CC
TAG_BEEFED02 = 0x800AC2D8

# Scene-graph recursion cap. There is NO cap in the cartridge; this is purely a
# runaway guard on our side, and for years it was 4, which silently truncated any
# model authored deeper than that. Model slot 18 (ANY_UNIT_MCVANIMZ1, the MCV deploy
# rig) is the first one we bake that is deeper: nodes 0x801B9210 and 0x801B9418 sit at
# depth 5 and 0x801B9134 at depth 6, all three carry geometry AND animation tracks, and
# at cap 4 they were baked nowhere at all.
#
# Blast radius of the lift, measured rather than assumed
# (tools/bakery/support/depth_cap_check.py): over every unit_models.json type that has a
# mesh, the part list -- count, display-list ROM offsets and mount translations -- is
# byte-identical at cap 4 and cap 24, with MCVANIM as the single expected exception,
# because MCVANIM is the type the lift exists for. Measured before MCVANIM was added:
# 182 types, 261 parts, zero differences. Measured after: 183 types, 279 parts at cap 4
# against 282 at cap 24, and the only three that move are slot 18's own deep nodes.
DEPTH_CAP = 24

# ---------------------------------------------------------------- cursors
# The CURSOR models are NOT in the 236-entry model table, which is why a
# model-table walk never reaches them.  The draw site at ROM 0x4D790
# (RAM 0x8004CBB0) indexes a private 16-byte table by a cursor model code
# 0x00..0x0D and takes the scene-graph node from +0x0C; the code itself comes
# from the 20-entry state table at RAM 0x80099920 (byte +0 of each 4-byte
# record).  Both tables live in the RESIDENT segment, so rom = ram - RES_DELTA.
CURSOR_NODE_TABLE = 0x80099750          # ROM 0x9A350, 16-byte records
CURSOR_STATE_TABLE = 0x80099920         # ROM 0x9A520, 20 records x 4 bytes
CURSOR_CODES = 14                       # 0x00..0x0D are the codes the state
                                        # table names; records 0x0E/0x0F carry
                                        # live node pointers (0x801BC3D8 ->
                                        # DL 0x013FC68 and 0x801BBE10 ->
                                        # DL 0x013DE10) but nothing selects them.


def cursor_node(code):
    """Scene-graph node RAM address for one N64 cursor model code (0 if none)."""
    off = CURSOR_NODE_TABLE - RES_DELTA + code * 16 + 12
    p = struct.unpack_from(">I", rom(), off)[0]
    return p if _in_model_seg(p) else 0


def cursor_states():
    """[(modelCode, modelCode2, frameCount, overlayFlag)] x 20, verbatim."""
    o = CURSOR_STATE_TABLE - RES_DELTA
    return [tuple(rom()[o + i * 4: o + i * 4 + 4]) for i in range(20)]


_ROM = None


def rom():
    global _ROM
    if _ROM is None:
        for p in _ROM_CANDIDATES:
            if os.path.exists(p):
                _ROM = open(p, "rb").read()
                break
        else:
            raise IOError("cnc_eu.z64 not found near %s" % HERE)
    return _ROM


def seg_of(a):
    for name, lo, hi, delta in SEGS:
        if lo <= a < hi:
            return name, delta
    return None, None


def _in_model_seg(a):
    return seg_of(a)[0] is not None


def _in_gmg(a):                                  # kept for API compatibility
    return GMG_LO <= a < GMG_HI


def r32(a):
    """u32 at a model-segment RAM address (big-endian), or None."""
    name, delta = seg_of(a)
    if name is None:
        return None
    o = a - delta
    if o + 4 > len(rom()):
        return None
    return struct.unpack_from(">I", rom(), o)[0]


def rf32(a):
    name, delta = seg_of(a)
    if name is None:
        return None
    o = a - delta
    if o + 4 > len(rom()):
        return None
    return struct.unpack_from(">f", rom(), o)[0]


def ram2rom(a):
    name, delta = seg_of(a)
    return None if name is None else a - delta


def node_ptr(slot):
    """The scene-graph node the model table points a slot at (0 if none)."""
    off = MODEL_TABLE - RES_DELTA + slot * 16 + 12
    p = struct.unpack_from(">I", rom(), off)[0]
    return p if _in_model_seg(p) else 0


def walk_node(ram, depth=0):
    """[(depth, node_ram, gfx_ram, gfx_rom)] for this node and everything under it."""
    if not _in_model_seg(ram) or depth > DEPTH_CAP:
        return []
    gfx = r32(ram)
    out = [(depth, ram, gfx, ram2rom(gfx) if _in_model_seg(gfx) else None)]
    kids = r32(ram + 0x0C)
    if kids is not None and _in_model_seg(kids):
        for k in range(MAX_CHILDREN):
            cn = r32(kids + k * 4)
            if not cn:
                break
            out += walk_node(cn, depth + 1)
    return out


def display_lists(slot):
    """Every display list a slot draws, root first. ROM offsets."""
    p = node_ptr(slot)
    if not p:
        return []
    return [rr for _d, _n, _g, rr in walk_node(p) if rr is not None]


# ------------------------------------------------------------- transforms
def quat_mat(q):
    """QuatToMat3 at RAM 0x80086D94. q = (w,x,y,z). 3x3 as nested lists."""
    w, x, y, z = q
    n = x * x + y * y + z * z + w * w
    if n == 0.0:
        return [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
    s = 2.0 / n
    xs, ys, zs = x * s, y * s, z * s
    wx, wy, wz = w * xs, w * ys, w * zs
    xx, xy, xz = x * xs, x * ys, x * zs
    yy, yz = y * ys, y * zs
    zz = z * zs
    return [
        [1.0 - (yy + zz), xy - wz, xz + wy],
        [xy + wz, 1.0 - (xx + zz), yz - wx],
        [xz - wy, yz + wx, 1.0 - (xx + yy)],
    ]


def _mmul(A, B):
    return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)]
            for i in range(3)]


def _mvec_row(v, M):                      # row-vector: v @ M
    return [sum(v[k] * M[k][j] for k in range(3)) for j in range(3)]


def _mt(M):
    return [[M[j][i] for j in range(3)] for i in range(3)]


def _mdiag(s):
    return [[s[0], 0, 0], [0, s[1], 0], [0, 0, s[2]]]


def trs_mat(q, s, qs):
    """The 3x3 a node's rotation, scale and scale-orientation compose to.

    The scale block is Rso . diag(S) . Rso^T -- a scale along axes the
    scale-orientation quaternion picks -- and the node's rotation is applied
    AFTER it, not before. The order only shows on a node with a NON-UNIFORM
    scale and a rotation, because the block is a multiple of the identity
    whenever the scale is uniform, and there is exactly ONE such node in the
    cartridge: 0x801BED58, scale (1.139, 1.834, 1.489). That node belongs to
    the GDI BARRACKS -- model-table slot 30, PYLE, root node 0x801BED7C, whose
    flag is a chain of three BEEFED02 children of which this is the first.
    It does NOT belong to the Hand of Nod: that is slot 31, its tree is
    0x801BFFD8 -> 0x801BFFB4, two nodes, and its one posed node has a UNIFORM
    scale. Walking all 236 model-table slots finds 0x801BED58 in exactly one
    tree, slot 30's.

    The slot is spelled out because an earlier revision of this docstring said
    Hand of Nod, and the mistake is an easy one to make twice: the cartridge's
    own name array at ROM 0x1DE924 (entry = slot - 10) reads slot 30
    NOD_STR_BRRKZ1 and slot 31 NOD_STR_HANDZ1, so the barracks carries a NOD_
    prefix in the cartridge's own debug names while the shipped building is the
    GDI one. Opening the Hand of Nod to check this measurement finds two nodes
    and no non-uniform scale, which makes the measurement read as invented.
    Checked against the cartridge's own baked N64 Mtx at node+0x04 over every
    node in the game that carries one -- 14 BEEFED02 and 93 CAFEDEAD:

        scale block then rotation   14/14 and 93/93   (this)
        rotation then scale block   13/14 and 93/93
        either, with the rotation transposed   7/14 and 53/93

    The last line is also what disposes of the standing theory that quat_mat
    returns a column-vector matrix among row-vector code: transposing it makes
    40 static nodes stop matching the cartridge.
    """
    R, Rs = quat_mat(q), quat_mat(qs)
    return _mmul(_mmul(_mmul(Rs, _mdiag(s)), _mt(Rs)), R)


def _curve_rest(desc_ram, dim):
    """First key value of an animation curve (see mount.py for the proof)."""
    data = r32(desc_ram)
    nkeys = r32(desc_ram + 8)
    stride = r32(desc_ram + 0x0C)
    if data is None or not _in_model_seg(data) or not nkeys or stride != 1 + 3 * dim:
        return None
    return [rf32(data + (stride - dim + i) * 4) for i in range(dim)]


def _beefed_transform(data_ram):
    """BEEFED02 data = {curvesA, stateB}; rest pose from curvesA's first keys."""
    src = r32(data_ram)
    if src is None or not _in_model_seg(src):
        return None
    t = _curve_rest(src + 0x00, 3) or [0.0, 0.0, 0.0]
    q = _curve_rest(src + 0x1C, 4) or [1.0, 0.0, 0.0, 0.0]
    s = _curve_rest(src + 0x38, 3) or [1.0, 1.0, 1.0]
    qs = _curve_rest(src + 0x54, 4) or [1.0, 0.0, 0.0, 0.0]
    return trs_mat(q, s, qs), t


def transform_of(node_ram):
    """(M3, t, kind) in the AUTHOR frame; kind in static|beefed|runtime|none."""
    h = r32(node_ram + 8)
    if h is None or not _in_model_seg(h):
        return None
    tag, data = r32(h), r32(h + 4)
    if tag == TAG_BEEFED02 and data is not None and _in_model_seg(data):
        r = _beefed_transform(data)
        if r is None:
            return ("runtime", tag, data)
        return (r[0], r[1], "beefed")
    if tag != TAG_CAFEDEAD or data is None or not _in_model_seg(data):
        return ("runtime", tag, data)
    t = [rf32(data + i * 4) for i in range(3)]
    q = [rf32(data + 0x0C + i * 4) for i in range(4)]
    s = [rf32(data + 0x1C + i * 4) for i in range(3)]
    qs = [rf32(data + 0x28 + i * 4) for i in range(4)]
    mirror = rf32(data + 0x38)
    M3 = trs_mat(q, s, qs)
    if mirror == -1.0:
        M3 = [[-e for e in row] for row in M3]
    return (M3, t, "static")


# author (Z up) -> mesh (Y up), row-vector, exactly mount.py's P_AUTHOR_TO_MESH
P_AUTHOR_TO_MESH = [[1.0, 0.0, 0.0],
                    [0.0, 0.0, -1.0],
                    [0.0, 1.0, 0.0]]


def parts(slot, axisfix=True):
    """Per-part dicts for a model slot, in walk order (root first):
        node      node RAM
        gfx_rom   the part's display list (ROM offset), None if the node has none
        depth     tree depth (0 = root)
        kind      static | beefed | runtime | none
        M         3x3 mount matrix, row-vector convention (mesh = local @ M + t)
        t         translation, mesh frame, model units
    BEEFED02 curve rest values are already in the mesh (Y-up) frame; the
    author->mesh conjugation is undone for them exactly as mount.py does."""
    return parts_of_node(node_ptr(slot), axisfix)


def parts_of_node(root, axisfix=True):
    """parts(), but from an arbitrary scene-graph node RAM address.

    The cursor models hang off the private cursor node table (cursor_node())
    rather than the model table, so they can only be reached this way.  The
    axisfix block below is MANDATORY for them: without the P^T M P conjugation
    cursor code 0x03's four CAFEDEAD-mounted brackets stand vertically instead
    of lying on the ground."""
    if not root:
        return []
    out = []
    # Cycle guard. It exists only because DEPTH_CAP is now 24 rather than 4: an
    # accidental back-edge that used to cost 4 levels of pointless recursion would
    # now cost 24, and a genuine cycle would never terminate. It visits each node
    # once, which is also what the animation extractor's walk() does, so the two
    # walks stay in step. Measured: it changes nothing that ships (see DEPTH_CAP).
    seen = set()
    I3 = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]

    def rec(ram, PM, Pt, depth=0):
        if not _in_model_seg(ram) or depth > DEPTH_CAP or ram in seen:
            return
        seen.add(ram)
        tr = transform_of(ram)
        kind = "static"
        if tr is None:
            M3, t = I3, [0.0, 0.0, 0.0]
            kind = "none"
        elif isinstance(tr[0], str):
            M3, t = I3, [0.0, 0.0, 0.0]
            kind = "runtime"
        else:
            M3, t = tr[0], tr[1]
            kind = tr[2]
            if kind == "beefed":
                # A BEEFED02 curve's rest values are ALREADY in the mesh (Y up)
                # frame, so the axisfix at the bottom of this function has to be
                # pre-undone for them. The undo of X -> P^T.X.P is P.X.P^T, and
                # writing it the other way round does NOT cancel: P is orthogonal,
                # so applying P^T.X.P twice conjugates by P^2 = diag(1,-1,-1),
                # which reverses every rotation about the vertical (mesh Y) axis.
                # That was the bug behind the SAM site's launcher: its azimuth swept
                # the wrong way, the drawn bearing falling 43.8 degrees for every 45
                # degrees of engine facing instead of rising 45, and the rockets left
                # sideways. Settled against the cartridge's OWN baked N64 Mtx at
                # node+0x04, which is each node's local matrix in the mesh frame. All
                # 14 BEEFED02 nodes carry one and all 14 translations match. The
                # rotation count MOVED when trs_mat's composition was corrected, so
                # both numbers are worth carrying: the shipped order (scale block then
                # rotation) matches 14/14, the earlier order (rotation then scale
                # block) 13/14. The single node that tells the two apart is
                # 0x801BED58, the cartridge's only node carrying both a non-uniform
                # scale and a rotation, out by 0.030 under the earlier order against
                # 1.8e-5 under the shipped one. Both counts are read at the baked
                # matrix's own s15.16 step of 1/65536, which is the floor on any
                # residual here. Against 10 rotations for the doubled conjugation and
                # (for a variant that drops the undo altogether) 8 rotations and no
                # translations at all.
                Pinv = _mt(P_AUTHOR_TO_MESH)
                M3 = _mmul(P_AUTHOR_TO_MESH, _mmul(M3, Pinv))
                t = _mvec_row(t, Pinv)
        WM = _mmul(M3, PM)
        Wt = [a + b for a, b in zip(_mvec_row(t, PM), Pt)]
        # +0x00 is the part's display list; +0x04 is a second skin of the same
        # geometry. PROC's mast, SILO's dome and FACT's crane carry geometry ONLY
        # in the +0x04 slot (their +0x00 is NULL), so fall back to it.
        gfx = r32(ram)
        alt = r32(ram + 4)
        pick = gfx if (gfx and _in_model_seg(gfx)) else (
            alt if (alt and _in_model_seg(alt)) else None)
        # ...and THIRD, the TEXTURE-BOOK PAYLOAD at +0x10. A node whose geometry is driven
        # by a book carries nothing in +0x00 or +0x04; the display list hangs off the
        # payload instead, as { TexAnim* ta, Gfx* src, Gfx* dst, u32 maxSplits }. The ion
        # cannon's beam and its three rings are all shaped this way, and so is the nuke.
        #
        # src (+0x04), NEVER dst (+0x08). They are the same pointer for all five effect
        # slots, so either would appear to work -- but they diverge on FACT, whose dst is
        # 0x801360E0, exactly ScriptModels' ramEnd, i.e. the runtime output buffer the
        # load-time splitter writes into. That address resolves to no ROM offset at all,
        # so a walker coded to dst returns nothing the moment it meets a FACT-shaped
        # payload. bake5.bake_struct_flipbooks already reads src for the same reason.
        #
        # AND IT IS TRIED LAST, after +0x00 and +0x04, which is the part that is easy to
        # get wrong. Written as "when +0x00 is null, take the payload" it would override
        # the +0x04 fallback on three SHIPPED structure nodes -- FACT, PROC and SILO --
        # whose geometry lives in +0x04 and whose payloads point at different addresses
        # holding the same triangle counts (14/14, 2/2, 24/24). Nothing would assert and
        # no count gate would catch it; three buildings would just quietly draw different
        # geometry.
        if pick is None:
            payload = r32(ram + 0x10)
            if payload and _in_model_seg(payload):
                src = r32(payload + 4)
                if src and _in_model_seg(src):
                    pick = src
        out.append(dict(node=ram, kind=kind, M=WM, t=Wt, depth=depth,
                        gfx_rom=ram2rom(pick) if pick else None,
                        from_alt=bool(pick and pick == alt and pick != gfx),
                        from_book=bool(pick and pick != gfx and pick != alt)))
        kids = r32(ram + 0x0C)
        if kids is not None and _in_model_seg(kids):
            for k in range(MAX_CHILDREN):
                cn = r32(kids + k * 4)
                if not cn:
                    break
                rec(cn, WM, Wt, depth + 1)

    rec(root, I3, [0.0, 0.0, 0.0])
    if axisfix:
        P = P_AUTHOR_TO_MESH
        Pt_ = _mt(P)
        for o in out:
            o["M"] = _mmul(Pt_, _mmul(o["M"], P))
            o["t"] = _mvec_row(o["t"], P)
    return out


if __name__ == "__main__":
    slots = [int(a) for a in sys.argv[1:]] or range(TABLE_SLOTS)
    for s in slots:
        p = node_ptr(s)
        if not p:
            continue
        tree = parts(s)
        if len(tree) < 2 and len(sys.argv) == 1:
            continue
        seg = seg_of(p)[0]
        print("slot %3d  node %08X  (%s)" % (s, p, seg))
        for o in tree:
            print("    %s node %08X %-7s gfx %s  t=(%8.2f %8.2f %8.2f)"
                  % ("  " * o["depth"], o["node"], o["kind"],
                     ("%07X" % o["gfx_rom"]) if o["gfx_rom"] is not None else "-",
                     o["t"][0], o["t"][1], o["t"][2]))
