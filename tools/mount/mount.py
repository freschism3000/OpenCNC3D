#!/usr/bin/env python3
"""Extract the per-part mount transform from the ROM scene graph and apply it to a payload.

Layout proven by disassembly (see mount_transform.md):
  node (0x1C bytes, GameModelsGeometry)
     +0x00 Gfx*  display list
     +0x04 Mtx*  pre-built matrix (runtime)
     +0x08 Handle* {u32 tagPtr, void* data}   <-- the MOUNT TRANSFORM
     +0x0C Node** children (NULL-terminated)
     +0x10 alt mesh descriptor, +0x14 unused, +0x18 proxy
  handle->tag == 0x800AC2C0 (CAFEDEAD): data is a 0x3C-byte transform + 3 channel ptrs
     +0x00 float3 translation
     +0x0C float4 quaternion (w,x,y,z)
     +0x1C float3 scale
     +0x28 float4 scale-orientation quaternion (w,x,y,z)
     +0x38 float  mirror flag (-1.0 negates the 3x3)
     +0x40/+0x44/+0x48  animation channel handles for T / R / S
  handle->tag == 0x800AC2D8 (BEEFED02): runtime-driven, no static pose in ROM
"""
import os, sys, struct
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mlib as M
sys.path.insert(0, M.SHARE + "/tools/romdump")
import objgraph

TAG_CAFEDEAD = 0x800AC2C0
TAG_DEADCAFE = 0x800AC2CC
TAG_BEEFED02 = 0x800AC2D8


def _u32(a): return M.u32(a - M.GMG_DELTA)
def _f32(a): return M.f32(a - M.GMG_DELTA)


def quat_mat(q):
    """QuatToMat3 at RAM 0x80086D94. q = (w,x,y,z). Column-vector convention."""
    w, x, y, z = q
    n = x*x + y*y + z*z + w*w
    if n == 0.0:
        return np.eye(3)
    s = 2.0 / n
    xs, ys, zs = x*s, y*s, z*s
    wx, wy, wz = w*xs, w*ys, w*zs
    xx, xy, xz = x*xs, x*ys, x*zs
    yy, yz = y*ys, y*zs
    zz = z*zs
    return np.array([
        [1.0 - (yy + zz), xy - wz,          xz + wy],
        [xy + wz,         1.0 - (xx + zz),  yz - wx],
        [xz - wy,         yz + wx,          1.0 - (xx + yy)],
    ])


def curve_rest(desc_ram, dim):
    """First key value of an animation curve.
    Descriptor (0x1C): {float* data, u32 degree, u32 nkeys, u32 stride, 0, u32, u32}
    key[i] = [dt, cp1(dim), cp2(dim), value(dim)]  -> stride = 1 + 3*dim
    Proven by the ctor at RAM 0x8007E320, which sums data[stride*i] for i>=1 as the
    curve's total duration."""
    data = _u32(desc_ram)
    nkeys = _u32(desc_ram + 8)
    stride = _u32(desc_ram + 0x0C)
    if not M.in_gmg(data) or nkeys == 0 or stride != 1 + 3*dim:
        return None
    return [_f32(data + (stride - dim + i) * 4) for i in range(dim)]


def beefed_transform(data_ram):
    """BEEFED02 handle data = {ptrA (static curves), ptrB (runtime state)}.
    ptrA is 0x70 bytes = four 0x1C curve descriptors: T(vec3) R(quat) S(vec3) So(quat).
    Proven by the initialiser at RAM 0x8007EE78."""
    src = _u32(data_ram)
    if not M.in_gmg(src):
        return None
    t = curve_rest(src + 0x00, 3) or [0.0, 0.0, 0.0]
    q = curve_rest(src + 0x1C, 4) or [1.0, 0.0, 0.0, 0.0]
    s = curve_rest(src + 0x38, 3) or [1.0, 1.0, 1.0]
    qs = curve_rest(src + 0x54, 4) or [1.0, 0.0, 0.0, 0.0]
    R, Rs = quat_mat(q), quat_mat(qs)
    # Scale block FIRST, then the node's rotation. Only a node with a non-uniform
    # scale and a rotation can tell the two orders apart, and the cartridge's own
    # baked Mtx at node+0x04 picks this one on all 14 BEEFED02 and all 93 CAFEDEAD
    # nodes that carry one (the other order misses one BEEFED02 node).
    return ((Rs @ np.diag(s) @ Rs.T) @ R, np.array(t))


def transform_of(node_ram):
    """(M3, t) for a scene-graph node, or None. M3 is the 3x3 as written into the F3D
    row-vector matrix's rows, so world_row = local_row @ M3 + t."""
    h = _u32(node_ram + 8)
    if not M.in_gmg(h):
        return None
    tag, data = _u32(h), _u32(h + 4)
    if tag == TAG_BEEFED02 and M.in_gmg(data):
        r = beefed_transform(data)
        return r if r is None else (r[0], r[1], 'beefed')
    if tag != TAG_CAFEDEAD or not M.in_gmg(data):
        return ('runtime', tag, data)
    t = np.array([_f32(data + i*4) for i in range(3)])
    q = [_f32(data + 0x0C + i*4) for i in range(4)]
    s = np.array([_f32(data + 0x1C + i*4) for i in range(3)])
    qs = [_f32(data + 0x28 + i*4) for i in range(4)]
    mirror = _f32(data + 0x38)
    R, Rs = quat_mat(q), quat_mat(qs)
    M3 = (Rs @ np.diag(s) @ Rs.T) @ R          # scale block first; see beefed_transform
    if mirror == -1.0:
        M3 = M3 * -1.0
    return (M3, t)


# The scene-graph transform blocks are in the authoring frame (Z up). The display-list
# vertices were exported into the N64 frame (Y up). P maps author -> mesh, row-vector.
P_AUTHOR_TO_MESH = np.array([[1.0, 0.0, 0.0],
                             [0.0, 0.0, -1.0],
                             [0.0, 1.0, 0.0]])

# Set True to treat BEEFED02 curve data as author-frame like CAFEDEAD (test switch).
BEEFED_AXISFIX = False


def part_transforms(model_index, axisfix=True):
    """World transform per part index, in objgraph.walk_node order.
    Returns list of (M3_4x4rows, t) accumulated down the tree, plus a 'kind' string."""
    root = objgraph.node_ptr(model_index)
    if not root:
        return []
    out = []

    def rec(ram, PM, Pt, depth=0):
        if not M.in_gmg(ram) or depth > 4:
            return
        tr = transform_of(ram)
        kind = 'static'
        if tr is None:
            M3, t = np.eye(3), np.zeros(3)
            kind = 'none'
        elif isinstance(tr[0], str):
            M3, t = np.eye(3), np.zeros(3)
            kind = 'runtime'
        else:
            M3, t = tr[0], tr[1]
            if len(tr) > 2:
                kind = tr[2]
                if not BEEFED_AXISFIX:
                    # BEEFED02 curves are already in the mesh (Y up) frame; undo the
                    # author->mesh conversion that gets applied to the whole tree below.
                    # The undo of X -> P.T @ X @ P is P @ X @ P.T; the other order
                    # conjugates by P^2 = diag(1,-1,-1) instead of cancelling, which
                    # mirrors every rotation about the vertical axis.
                    Pinv = P_AUTHOR_TO_MESH.T
                    M3 = P_AUTHOR_TO_MESH @ M3 @ Pinv
                    t = t @ Pinv
        # row-vector compose: v @ M3 + t, then @ PM + Pt
        WM = M3 @ PM
        Wt = t @ PM + Pt
        gfx = _u32(ram)
        if M.in_gmg(gfx):
            out.append(dict(node=ram, kind=kind, M=WM, t=Wt, depth=depth))
        else:
            out.append(dict(node=ram, kind=kind, M=WM, t=Wt, depth=depth, nogfx=True))
        kids = _u32(ram + 0x0C)
        if M.in_gmg(kids):
            for k in range(8):
                cn = _u32(kids + k*4)
                if cn == 0:
                    break
                rec(cn, WM, Wt, depth + 1)

    rec(root, np.eye(3), np.zeros(3))
    out = [o for o in out if not o.get('nogfx')]
    if axisfix:
        P = P_AUTHOR_TO_MESH
        for o in out:
            o['M'] = P.T @ o['M'] @ P
            o['t'] = o['t'] @ P
    return out
