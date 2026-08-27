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
    if not _in_model_seg(ram) or depth > 4:
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
    R, Rs = quat_mat(q), quat_mat(qs)
    M3 = _mmul(R, _mmul(_mmul(Rs, _mdiag(s)), _mt(Rs)))
    return M3, t


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
    R, Rs = quat_mat(q), quat_mat(qs)
    M3 = _mmul(R, _mmul(_mmul(Rs, _mdiag(s)), _mt(Rs)))
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
    root = node_ptr(slot)
    if not root:
        return []
    out = []
    I3 = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]

    def rec(ram, PM, Pt, depth=0):
        if not _in_model_seg(ram) or depth > 4:
            return
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
                Pinv = _mt(P_AUTHOR_TO_MESH)
                M3 = _mmul(Pinv, _mmul(M3, P_AUTHOR_TO_MESH))
                t = _mvec_row(t, _mt(P_AUTHOR_TO_MESH))
        WM = _mmul(M3, PM)
        Wt = [a + b for a, b in zip(_mvec_row(t, PM), Pt)]
        # +0x00 is the part's display list; +0x04 is a second skin of the same
        # geometry. PROC's mast, SILO's dome and FACT's crane carry geometry ONLY
        # in the +0x04 slot (their +0x00 is NULL), so fall back to it.
        gfx = r32(ram)
        alt = r32(ram + 4)
        pick = gfx if (gfx and _in_model_seg(gfx)) else (
            alt if (alt and _in_model_seg(alt)) else None)
        out.append(dict(node=ram, kind=kind, M=WM, t=Wt, depth=depth,
                        gfx_rom=ram2rom(pick) if pick else None,
                        from_alt=bool(pick and pick == alt and pick != gfx)))
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
