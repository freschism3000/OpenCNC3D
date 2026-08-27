#!/usr/bin/env python3
"""
CNC3D -- the object graph: why several models were missing their moving part.

A model is not one display list. The 16-byte records of the model table at RAM
0x80099998 hold, in their last word, a pointer to a 0x1C-byte SCENE GRAPH NODE, and
that node can have children:

    node (0x1C bytes, big-endian, in GameModelsGeometry unless noted)
        +0x00 u32  gfx        display list for this part
        +0x04 u32  gfx alt    a second display list, same geometry, different skin
        +0x08 u32  behaviour  shared per part kind (turrets all point at 0x800AC2D8)
        +0x0C u32  children   NULL-terminated array of node pointers, or 0
        +0x10..+0x18          zero at rest (runtime scratch)

The exporter only ever read the root's +0x00, so every model came out as its static
body with the moving part dropped. That is exactly the GUN defect: slot 16 is right,
its root is the 19-triangle hexagonal base pad, and the 32-triangle barrel assembly it
should be wearing is its one child (RAM 0x801AE068, ROM 0x151AD8, bbox 426 x 126 x 779
model units -- long in z, which is a barrel).

The same child list gives the medium and light tanks their turrets, the Jeep and Buggy
their gun mounts, the Advanced Comm Center its dish, the helicopter its rotor disc and
the Weapons Factory its doors.

The first 12 bytes of each model-table record are zero in the ROM and are runtime
scratch, so there is NO per-model position offset stored anywhere in this table. That
matters for the V20 question: see the CHANGELOG.

Usage:
    python3 objgraph.py                 every slot that has children
    python3 objgraph.py <slot>          one slot's tree
"""
import os, struct, sys

HERE  = os.path.dirname(os.path.abspath(__file__))
ROOT  = os.path.abspath(os.path.join(HERE, "..", ".."))
ROM_P = os.path.join(ROOT, "rom", "cnc_eu.z64")

RES_DELTA = 0x80000400 - 0x1000              # resident: rom = ram - delta
GMG_DELTA = 0x8005C590                       # GameModelsGeometry: rom = ram - delta
GMG_LO, GMG_HI = 0x8015A7A0, 0x801C0980
# ScriptModels: the same node structure also lives in the ScriptModels segment
# (RAM 0x80122B60, ROM 0xC6EA0..0xDA420). Nine named models' table records point
# there: ORCA TRAN FTNK STNK HTNK BOAT TMPL FACT SAM. They were previously
# dismissed as "bare Gfx, no scene-graph node" because only GMG pointers were
# accepted; in fact five of them have children (TRAN rotors, HTNK turret,
# BOAT gun, FACT door tree, SAM launcher assembly).
SM_DELTA = 0x80122B60 - 0xC6EA0
SM_LO, SM_HI = 0x80122B60, 0x80122B60 + (0xDA420 - 0xC6EA0)
MODEL_TABLE = 0x80099998
TABLE_SLOTS = 236
NODE_SIZE   = 0x1C
MAX_CHILDREN = 8                             # sanity cap on the NULL-terminated list

_ROM = None


def rom():
    global _ROM
    if _ROM is None:
        _ROM = open(ROM_P, "rb").read()
    return _ROM


def _in_gmg(a):
    return GMG_LO <= a < GMG_HI


def _in_sm(a):
    return SM_LO <= a < SM_HI


def _in_node_space(a):
    return _in_gmg(a) or _in_sm(a)


def _ram2rom(a):
    if _in_gmg(a):
        return a - GMG_DELTA
    if _in_sm(a):
        return a - SM_DELTA
    return None


def _r32(ram):
    return struct.unpack_from(">I", rom(), _ram2rom(ram))[0]


def _f32(ram):
    return struct.unpack_from(">f", rom(), _ram2rom(ram))[0]


def _r32_gmg(ram):          # kept for callers that import it
    return struct.unpack_from(">I", rom(), ram - GMG_DELTA)[0]


def node_ptr(slot):
    """The scene-graph node the model table points a slot at (0 if none/elsewhere)."""
    off = MODEL_TABLE - RES_DELTA + slot * 16 + 12
    p = struct.unpack_from(">I", rom(), off)[0]
    return p if _in_node_space(p) else 0


def _mount(ram):
    """The child's static mount transform, or None.

    node+0x08 points at a pair {behaviour_fn (resident), data}. data+0x0C is a
    block of 8 floats: [tx ty tz  qw qx qy qz  scale]. Blocks whose quaternion
    is all zero (|q| ~ 0) are runtime scratch that the behaviour (e.g. the
    turret spinner 0x800AC2D8) fills every frame -- treat those as identity.
    Verified on TRAN (rear rotor mount q = 45 degrees about z, scale 0.982),
    HTNK (turret at z +130), FACT (door tree, one door hinged 30 degrees),
    SAM (launcher arms mirrored at x +-360..372).
    """
    pair = _r32(ram + 0x08)
    if not _in_node_space(pair):
        return None
    beh = _r32(pair)
    data = _r32(pair + 4)
    if not _in_node_space(data):
        return None
    f = [_f32(data + i * 4) for i in range(8)]
    t, q, s = f[0:3], f[3:7], f[7]
    if abs(q[0]) + abs(q[1]) + abs(q[2]) + abs(q[3]) < 1e-6:
        t, q, s = [0.0, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0], 1.0
    if abs(s) < 1e-6:
        s = 1.0
    return dict(t=t, q=q, s=s, behaviour=beh)


def walk_full(ram, depth=0):
    """[{depth, node, gfx_ram, gfx_rom, mount}] for this node and its subtree.

    gfx is the node's +0x00 display list; when that is NULL the +0x04 alternate
    skin is used instead (FACT's fifth child stores its only Gfx there).
    """
    if not _in_node_space(ram) or depth > 6:
        return []
    gfx = _r32(ram)
    if not _in_node_space(gfx):
        alt = _r32(ram + 4)
        if _in_node_space(alt):
            gfx = alt
    out = [dict(depth=depth, node=ram, gfx_ram=gfx,
                gfx_rom=_ram2rom(gfx) if _in_node_space(gfx) else None,
                mount=_mount(ram) if depth > 0 else None)]
    kids = _r32(ram + 0x0C)
    if _in_node_space(kids):
        for k in range(MAX_CHILDREN):
            cn = _r32(kids + k * 4)
            if cn == 0:
                break
            out += walk_full(cn, depth + 1)
    return out


def walk_node(ram, depth=0):
    """[(depth, node_ram, gfx_ram, gfx_rom)] -- legacy 4-tuple view of walk_full."""
    return [(n['depth'], n['node'], n['gfx_ram'], n['gfx_rom'])
            for n in walk_full(ram, depth)]


def display_lists(slot):
    """Every display list a slot draws, root first. ROM offsets."""
    p = node_ptr(slot)
    if not p:
        return []
    return [n['gfx_rom'] for n in walk_full(p) if n['gfx_rom'] is not None]


def part_info(slot):
    """Per-part records aligned with display_lists(slot):
    {rom, depth, mount} where mount composes ancestors for nested nodes.
    mount is the WORLD transform {t, q, s} to apply to that part's vertices
    (v' = t + s * R(q) v), identity for the root."""
    p = node_ptr(slot)
    if not p:
        return []
    out = []
    stack = {}
    for n in walk_full(p):
        if n['gfx_rom'] is None and n['mount'] is None and n['depth'] == 0:
            pass
        d = n['depth']
        local = n['mount'] or dict(t=[0.0, 0.0, 0.0], q=[1.0, 0.0, 0.0, 0.0], s=1.0)
        parent = stack.get(d - 1) if d > 0 else None
        world = _compose(parent, local) if parent else local
        stack[d] = world
        if n['gfx_rom'] is not None:
            seg = 'sm' if _in_sm(n['node']) else 'gmg'
            m = dict(t=world['t'], q=world['q'], s=world['s'])
            if seg == 'sm':
                m = _zup_to_yup(m)
            out.append(dict(rom=n['gfx_rom'], depth=d, seg=seg, mount=m))
    return out


# ScriptModels mount blocks are stored in the game's Z-UP world convention while the
# meshes themselves are Y-up. The conversion is the standard +90-about-x change of
# basis: (x, y, z)_mount -> (x, z, -y)_mesh. Proven by containment: with it, TRAN's
# rotors land on top of the fuselage at both ends, BOAT's gun lands on the deck on the
# centreline, HTNK's turret sits on the hull top, SAM's mast is above the pad, and the
# rotations become physical (rotor phase 45 deg about vertical, FACT door hinge 30 deg
# about vertical); without it, every one of those parts is below ground or off to the
# side of its parent's bounding box.
_C_Q = (0.7071067811865476, -0.7071067811865476, 0.0, 0.0)   # Rx(-90) as quaternion


def _zup_to_yup(m):
    t = m['t']
    ci = (_C_Q[0], -_C_Q[1], -_C_Q[2], -_C_Q[3])
    q = _qmul(_qmul(list(_C_Q), list(m['q'])), list(ci))
    return dict(t=[t[0], t[2], -t[1]], q=q, s=m['s'])


def _qmul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return [aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw]


def _qrot(q, v):
    # rotate v by unit quaternion q = [w,x,y,z]
    w, x, y, z = q
    vx, vy, vz = v
    # t = 2 * cross(q.xyz, v)
    tx = 2 * (y * vz - z * vy)
    ty = 2 * (z * vx - x * vz)
    tz = 2 * (x * vy - y * vx)
    return [vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx)]


def _compose(parent, local):
    """world = parent applied after local: v' = pT + pS * R(pQ) (lT + lS * R(lQ) v)."""
    pt, pq, ps = parent['t'], parent['q'], parent['s']
    lt, lq, ls = local['t'], local['q'], local['s']
    rt = _qrot(pq, lt)
    return dict(t=[pt[i] + ps * rt[i] for i in range(3)],
                q=_qmul(pq, lq), s=ps * ls)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        slots = [int(sys.argv[1])]
    else:
        slots = range(TABLE_SLOTS)
    for s in slots:
        p = node_ptr(s)
        if not p:
            continue
        tree = walk_node(p)
        if len(tree) < 2 and len(sys.argv) == 1:
            continue                      # only show the interesting ones by default
        print("slot %3d  node %08X" % (s, p))
        for d, n, g, rr in tree:
            print("    %s node %08X gfx %08X  rom %s"
                  % ("  " * d, n, g, ("%07X" % rr) if rr is not None else "-"))
