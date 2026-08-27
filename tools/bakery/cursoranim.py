#!/usr/bin/env python3
"""
CNC3D -- THE CURSOR ANIMATION, decoded out of the cartridge.

Ten of the twenty console cursor states carry a frame count of 100 in the state table at
RAM 0x80099920 (byte +2 of each 4-byte record; our transcription is C3D_STATE in
game/cursor3d_mod.h) and every one of them used to draw STATIC. This module recovers what
that clock actually drives and hands bake5.py the per-frame data.

It is THREE DIFFERENT MECHANISMS, not one:

  * codes 0x03, 0x04, 0x08, 0x09  -- sub-node TRANSLATION tracks (FEEB0005, cubic Bezier)
  * code  0x06                    -- translation AND rotation (FEEB0002); this is the
                                     DEPLOY diamond, whose four arms travel exactly 200
                                     units radially outward while pitching 70 degrees.
                                     the reported "3D cursor animations seem bugged out".
  * codes 0x0A, 0x0B              -- a per-frame TEXTURE FLIPBOOK (node+0x10), 4 frames of
                                     an 8x8 CI image and 3 frames of a 2x6 RGBA one
  * code  0x05                    -- carries frameCount 100 with NOTHING to animate. That
                                     is the cartridge's own answer and it is recorded as
                                     such rather than filled in with an invention.

THE CLOCK (resident code, ROM = RAM - 0x7FFFF400):
    RAM 0x8004CB10 / ROM 0x04D710   frame = (frameCounter * 4) mod state[st].frameCount
                                    frameCounter = *(u32*)0x80097210
    RAM 0x8004A690 / ROM 0x04B290   t = frame * 160.0 + 1.0
                                    (160.0f at RAM 0x800047B8, 1.0f at RAM 0x800047BC)
  so a cycle is 100 anim frames = 16000 time units, and `frame` only ever takes the 25
  values 0, 4, 8 ... 96.

THE TABLES:
    cursor node table   RAM 0x80099750 / ROM 0x09A350, 16 bytes/entry, node ptr at +0x0C.
                        PRIVATE: reachable from no model-table walk, which is why the
                        cursor art was found late (objgraph2.cursor_node()).
    xform classes       RAM 0x800AC2C0 / ROM 0x0ACEC0, 12 bytes {magic, updateFn, evalFn}.
                        Every cursor node's pose record is CAFEDEAD; none is BEEFED02,
                        which is the class the BUILDINGS animate through.
    track classes       RAM 0x800AC2F0 / ROM 0x0ACEF0, 16 bytes {magic, keySize, init,
                        eval}. The cursors use exactly two: FEEB0005 (keySize 44, vec3,
                        eval RAM 0x8008B6B8) and FEEB0002 (keySize 92, quaternion, init
                        RAM 0x8008E20C, eval RAM 0x8008E388).
    pose record         (0x4C bytes, node+0x08 -> {classTag, data})
                        +0x00 f32[3] default T    +0x0C f32[4] default rotation quat wxyz
                        +0x1C f32[3] default S    +0x28 f32[4] scale-orientation quat
                        +0x38 f32 handedness      +0x3C u32 flags
                        +0x40 Track* T   +0x44 Track* R   +0x48 Track* S
                        Track (12 bytes) = {classDesc*, keys*, keyCount}.
                        No cursor node carries a SCALE track (asserted below).

THE ARITHMETIC, read out of the code and not out of the comments:

  FEEB0005 key (44 bytes): +0x00 s32 time, +0x04 u32 flags, +0x08 f32[3] value,
  +0x14 f32[3] tangent in, +0x20 f32[3] tangent out. For t between key i and key i+1,
  u = (t - t_i)/(t_i+1 - t_i), w = 1 - u, h = (t_i+1 - t_i)/3:
      P0 = V_i            P1 = V_i     + Tout_i    * h
      P3 = V_i+1          P2 = V_i+1   + Tin_i+1   * h
      out = w^3 P0 + 3u w^2 P1 + 3u^2 w P2 + u^3 P3
  The key search (RAM 0x8008AF58) CLAMPS at both ends, so a track shorter than the cycle
  holds its last value and then snaps when the clock wraps. That snap is the console's.

  FEEB0002 key (92 bytes): +0x1C f32[4] {angle, axis[3]} is the AUTHORED rotation; the
  quaternion slots at +0x2C/+0x3C/+0x4C are all the (1,0,0,0) load-time placeholder,
  because the class's init hook (RAM 0x8008E20C) builds the real quaternions AT LOAD,
  CUMULATIVELY. Read the bytes without running that pass and every rotation track reads
  as identity, which is exactly how they have always read here.

  THE COMPOSITION ORDER IS PROVEN, NOT ASSUMED. q_i = q_{i-1} * aa(key_i), i.e. each key
  is a delta in the node's OWN frame. Cursor 0x06's four arms sit at four different
  90-degree yaws and their authored delta axes are -Y, -X, +Y, +X respectively. Under
  local-frame composition all four resolve to the SAME parent-frame rotation (cross-arm
  spread 0.0 to float precision, `verify` below); under parent-frame composition they
  disagree by up to 1.15 in quaternion components. One order makes the four arms of one
  cursor move together, the other does not.

  Between keys this module SLERPs. The console runs SQUAD (RAM 0x800886F0) over control
  quaternions the same load hook builds; the key instants are exact either way and only
  the shape between them can differ. Recorded as a known gap.

  node+0x10 TEXTURE FLIPBOOK: payload {TexAnim*, Gfx*, Gfx*, u32 maxSplits}; TexAnim =
  {u32* timeTable (count+1, last is the PERIOD), u32* imageTable (count), u32 settimg
  word, u32 matchAddr == imageTable[0], Gfx* tail (runtime), u16 count, u16 index}.
  Per frame: tt = ((u32)t) mod period; idx = first i with tt <= timeTable[i+1]. This one
  is a true LOOP where the tracks CLAMP.

WHAT bake5.py GETS. Per animated cursor mesh, one PKB node-animation block in exactly the
shape bake_node_anim writes for the buildings: 100 frames x nparts x a 3x4 DELTA from the
node's REST pose, because bake_mesh_parts has already baked the rest pose into the
vertices. The delta is composed in objgraph2's own convention (the same quat_mat, the
same row-vector chain, the same P_AUTHOR_TO_MESH axis fix), so the identity delta is
today's picture, exactly.

Run this file directly for the verification table.
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import objgraph2 as O2                                              # noqa: E402

RES_DELTA = O2.RES_DELTA                    # resident: ROM = RAM - 0x7FFFF400
XCLASS_RAM = 0x800AC2C0                     # ROM 0x0ACEC0
TCLASS_RAM = 0x800AC2F0                     # ROM 0x0ACEF0
STATE_TAB_RAM = 0x80099920                  # ROM 0x09A520
T_MUL_RAM = 0x800047B8                      # ROM 0x0053B8, 160.0f
T_ADD_RAM = 0x800047BC                      # ROM 0x0053BC, 1.0f
CURSOR_SCALE_RAM = 0x80097984               # ROM 0x098584, 0.25f

FRAME_STEP = 4                              # sll v0,v0,2 at RAM 0x8004CB28
FRAME_COUNT = 100                           # the state table's own count
UNITS_PER_FRAME = 160.0
T_BIAS = 1.0

CLASS_CAFEDEAD = 0xCAFEDEAD
CLASS_BEEFED02 = 0xBEEFED02


# ------------------------------------------------------------------ resident reads
def _res_u32(ram):
    return struct.unpack_from(">I", O2.rom(), ram - RES_DELTA)[0]


def _res_f32(ram):
    return struct.unpack_from(">f", O2.rom(), ram - RES_DELTA)[0]


def _track_classes():
    out = {}
    for i in range(12):
        magic, size, fa, fb = struct.unpack_from(">4I", O2.rom(),
                                                 TCLASS_RAM - RES_DELTA + i * 16)
        out[TCLASS_RAM + i * 16] = dict(magic=magic, size=size, init=fa, eval=fb)
    magics = sorted(v["magic"] for v in out.values())
    assert magics == [0xFEEB0001 + i for i in range(12)], \
        "the track class table is not the twelve FEEB000n records: %s" % \
        [hex(m) for m in magics]
    by = {v["magic"]: v for v in out.values()}
    assert by[0xFEEB0005]["size"] == 44, "FEEB0005 key size moved"
    assert by[0xFEEB0002]["size"] == 92, "FEEB0002 key size moved"
    return out


def check_landmarks():
    """Every constant this module leans on, asserted against the ROM."""
    assert _res_f32(T_MUL_RAM) == 160.0, "the 160.0 cursor time scale moved"
    assert _res_f32(T_ADD_RAM) == 1.0, "the +1.0 cursor time bias moved"
    assert _res_f32(CURSOR_SCALE_RAM) == 0.25, "Cursor Scale is no longer 0.25"
    magic = struct.unpack_from(">I", O2.rom(), XCLASS_RAM - RES_DELTA)[0]
    assert magic == CLASS_CAFEDEAD, "xform class 0 is not CAFEDEAD"
    magic = struct.unpack_from(">I", O2.rom(), XCLASS_RAM - RES_DELTA + 24)[0]
    assert magic == CLASS_BEEFED02, "xform class 2 is not BEEFED02"
    _track_classes()
    st = states()
    hundreds = [i for i, s in enumerate(st) if s[2] == FRAME_COUNT]
    assert len(hundreds) == 10, \
        "expected ten frameCount==100 cursor states, got %d" % len(hundreds)
    assert all(s[2] in (1, FRAME_COUNT) for s in st), \
        "a cursor state carries a frame count that is neither 1 nor 100"


def states():
    """The 20 cursor state records, verbatim: {model, model2, frameCount, ringFlag}."""
    o = STATE_TAB_RAM - RES_DELTA
    return [tuple(O2.rom()[o + i * 4: o + i * 4 + 4]) for i in range(20)]


# ------------------------------------------------------------------ tracks
def _read_track(ram, classes):
    cd = O2.r32(ram)
    keys = O2.r32(ram + 4)
    n = O2.r32(ram + 8)
    assert cd in classes, "cursor track 0x%08X has unknown class 0x%08X" % (ram, cd)
    cls = classes[cd]
    assert 0 < n < 64, "cursor track 0x%08X claims %d keys" % (ram, n)
    return dict(ram=ram, magic=cls["magic"], size=cls["size"], keys=keys, n=n)


def _vec3_keys(tr):
    """FEEB0005, 44-byte keys."""
    assert tr["magic"] == 0xFEEB0005, \
        "a cursor translation track is class 0x%08X, not FEEB0005" % tr["magic"]
    out = []
    for k in range(tr["n"]):
        b = tr["keys"] + k * 44
        out.append(dict(t=struct.unpack_from(">i", O2.rom(), b - O2.GMG_DELTA)[0],
                        flags=O2.r32(b + 4),
                        v=[O2.rf32(b + 8 + i * 4) for i in range(3)],
                        tin=[O2.rf32(b + 0x14 + i * 4) for i in range(3)],
                        tout=[O2.rf32(b + 0x20 + i * 4) for i in range(3)]))
    times = [k["t"] for k in out]
    assert times == sorted(times), "FEEB0005 keys are not time-ordered"
    for k in out:
        # interp type 2 is step/hold. The cursors never use it, so assert rather than
        # silently Bezier through a stepped curve.
        assert ((k["flags"] >> 7) & 7) != 2 and ((k["flags"] >> 10) & 7) != 2, \
            "a stepped FEEB0005 key exists; the Bezier path would be wrong for it"
        assert not (k["flags"] & 0x8000), "the 0x8000 hold bit is set on a cursor key"
    return out


def _quat_keys(tr):
    """FEEB0002, 92-byte keys. The angle-axis at +0x1C is the authored value; the three
    quaternion slots are the load-time (1,0,0,0) placeholder."""
    assert tr["magic"] == 0xFEEB0002, \
        "a cursor rotation track is class 0x%08X, not FEEB0002" % tr["magic"]
    out = []
    for k in range(tr["n"]):
        b = tr["keys"] + k * 92
        ease = [O2.rf32(b + 0x14), O2.rf32(b + 0x18)]
        assert ease == [0.0, 0.0], \
            "a cursor rotation key carries a non-zero ease; RAM 0x8008C0C8 would bend u"
        for off in (0x2C, 0x3C, 0x4C):
            q = [O2.rf32(b + off + i * 4) for i in range(4)]
            assert q == [1.0, 0.0, 0.0, 0.0], \
                "a FEEB0002 quaternion slot is not the load-time placeholder: %s" % q
        out.append(dict(t=struct.unpack_from(">i", O2.rom(), b - O2.GMG_DELTA)[0],
                        angle=O2.rf32(b + 0x1C),
                        axis=[O2.rf32(b + 0x20 + i * 4) for i in range(3)]))
    times = [k["t"] for k in out]
    assert times == sorted(times), "FEEB0002 keys are not time-ordered"
    return out


def _span(keys, t):
    """RAM 0x8008AF58 exactly: clamped at both ends."""
    n = len(keys)
    if t <= keys[0]["t"]:
        return 0, 0.0
    if keys[n - 1]["t"] < t:
        return n - 2, 1.0
    for i in range(n - 1):
        a, b = keys[i]["t"], keys[i + 1]["t"]
        if a <= t < b:
            return i, (t - a) / float(b - a)
    return n - 2, 1.0


def eval_vec3(keys, t):
    """RAM 0x8008B1DC: cubic Bezier built from the Hermite tangents, h = dt/3."""
    if len(keys) == 1:
        return list(keys[0]["v"])
    i, u = _span(keys, t)
    a, b = keys[i], keys[i + 1]
    h = (b["t"] - a["t"]) / 3.0
    w = 1.0 - u
    uu = u * u
    out = []
    for c in range(3):
        P0 = a["v"][c]
        P1 = a["tout"][c] * h + P0
        P3 = b["v"][c]
        P2 = b["tin"][c] * h + P3
        out.append(((w * P0 + 3.0 * u * P1) * w + 3.0 * uu * P2) * w + u * uu * P3)
    return out


def aa_to_quat(angle, axis):
    n = math.sqrt(sum(c * c for c in axis))
    if n < 1e-9:
        return [1.0, 0.0, 0.0, 0.0]
    s = math.sin(angle * 0.5) / n
    return [math.cos(angle * 0.5), axis[0] * s, axis[1] * s, axis[2] * s]


def qmul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return [w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2]


def qconj(q):
    return [q[0], -q[1], -q[2], -q[3]]


def cumulative_quats(keys):
    """The load hook's own product: q_i = q_{i-1} * aa(key_i), q_0 = aa(key_0)."""
    q = aa_to_quat(keys[0]["angle"], keys[0]["axis"])
    out = [q]
    for k in keys[1:]:
        q = qmul(q, aa_to_quat(k["angle"], k["axis"]))
        out.append(q)
    return out


def slerp(q0, q1, u):
    d = sum(a * b for a, b in zip(q0, q1))
    if d < 0.0:
        q1 = [-c for c in q1]
        d = -d
    if d > 0.9995:
        r = [a + (b - a) * u for a, b in zip(q0, q1)]
    else:
        th = math.acos(max(-1.0, min(1.0, d)))
        s = math.sin(th)
        r = [(math.sin((1.0 - u) * th) * a + math.sin(u * th) * b) / s
             for a, b in zip(q0, q1)]
    n = math.sqrt(sum(c * c for c in r)) or 1.0
    return [c / n for c in r]


def qlog(q):
    """A unit quaternion's logarithm: the pure vector (theta/sin theta) * v."""
    v = q[1:]
    n = math.sqrt(sum(c * c for c in v))
    if n < 1e-12:
        return [0.0, 0.0, 0.0]
    th = math.atan2(n, max(-1.0, min(1.0, q[0])))
    return [c * th / n for c in v]


def qexp(v):
    """A pure vector's exponential: the unit quaternion it names."""
    th = math.sqrt(sum(c * c for c in v))
    if th < 1e-12:
        return [1.0, 0.0, 0.0, 0.0]
    s = math.sin(th) / th
    return [math.cos(th), v[0] * s, v[1] * s, v[2] * s]


def squad_tangents(quats):
    """Shoemake's inner control points:
           s_i = q_i * exp(-(log(q_i^-1 q_{i+1}) + log(q_i^-1 q_{i-1})) / 4)
    with the endpoints duplicated, s_0 = q_0 and s_{n-1} = q_{n-1}.

    TWO FREE CHOICES LIVE HERE AND THEY ARE OURS, not decoded. The cartridge's tangent
    builder is RAM 0x8008D450 (ROM 0x8E050) and its tail has never been transcribed --
    tools/anim/anim_extract.py:162-170 registers it as a Kochanek-Bartels spline with
    non-uniform time weighting, reading per-key ease and tension through 0x8008E6E0. The
    choices are (a) UNIFORM logs rather than time-weighted, which is the difference between
    1.97 and 1.40 degrees of peak deviation on cursor 0x06, and (b) endpoint duplication.
    Both are taken because every per-key parameter that routine could read is ZERO on every
    cursor key: the ease pair at key+0x14/+0x18 is (0,0) -- already asserted below -- and
    the three floats at key+0x08..+0x10 are (0,0,0). That is the tension-free case, where
    Kochanek-Bartels degenerates to Shoemake."""
    n = len(quats)
    if n < 3:
        return list(quats)
    out = [list(quats[0])]
    for i in range(1, n - 1):
        qi = quats[i]
        ci = qconj(qi)
        lp = qlog(qmul(ci, quats[i + 1]))
        lm = qlog(qmul(ci, quats[i - 1]))
        out.append(qmul(qi, qexp([-(a + b) / 4.0 for a, b in zip(lp, lm)])))
    out.append(list(quats[-1]))
    return out


def squad(p, a, b, q, u):
    """RAM 0x80088414, verbatim: h = 2u(1-u); slerp(slerp(p,q,u), slerp(a,b,u), h).

    Textbook Shoemake. The dispatcher at RAM 0x800886F0 sends multi-turn rotations
    somewhere else, but no cursor key reaches it: the largest cursor key angle is 3.8068
    rad and the test is angle*0.5 >= pi, so 1.90 < pi on every one of them."""
    h = 2.0 * u * (1.0 - u)
    return slerp(slerp(p, q, u), slerp(a, b, u), h)


def eval_quat(keys, quats, t, tangents=None):
    """SQUAD, which is what the console runs. We ran SLERP, which agrees with it exactly
    AT THE KEYS and differs only in the curve between them.

    The size of that difference, measured over the 25 reachable frame times: 0.0000 degrees
    on cursor 0x09 -- a two-key track makes squad collapse to slerp identically -- and
    1.9736 degrees at worst on cursor 0x06, which is 5.11 mesh units at the arms' longest
    lever and about 0.4 px on screen. Small, and built anyway: the evaluator is decoded, so
    running it is cheaper than carrying an approximation and a note explaining it."""
    if len(keys) == 1:
        return list(quats[0])
    i, u = _span(keys, t)
    if tangents is None or len(tangents) != len(quats):
        return slerp(quats[i], quats[i + 1], u)
    return squad(quats[i], tangents[i], tangents[i + 1], quats[i + 1], u)


# ------------------------------------------------------------------ the graph
def _pose_of(node_ram, classes):
    """-> dict(T, Q, S, SO, mirror, trackT, trackR) or None when the node has no pose."""
    h = O2.r32(node_ram + 8)
    if h is None or not O2._in_model_seg(h):
        return None
    tag, data = O2.r32(h), O2.r32(h + 4)
    assert tag != O2.TAG_BEEFED02, \
        "cursor node 0x%08X is BEEFED02; the cursors are all CAFEDEAD" % node_ram
    if tag != O2.TAG_CAFEDEAD or data is None or not O2._in_model_seg(data):
        return None
    p = dict(T=[O2.rf32(data + i * 4) for i in range(3)],
             Q=[O2.rf32(data + 0x0C + i * 4) for i in range(4)],
             S=[O2.rf32(data + 0x1C + i * 4) for i in range(3)],
             SO=[O2.rf32(data + 0x28 + i * 4) for i in range(4)],
             mirror=O2.rf32(data + 0x38), trackT=None, trackR=None)
    tp, rp, sp = O2.r32(data + 0x40), O2.r32(data + 0x44), O2.r32(data + 0x48)
    assert not sp, \
        "cursor node 0x%08X carries a SCALE track; this module does not evaluate one" \
        % node_ram
    if tp:
        tr = _read_track(tp, classes)
        keys = _vec3_keys(tr)
        assert max(abs(a - b) for a, b in zip(keys[0]["v"], p["T"])) < 1e-3, \
            "node 0x%08X: translation key 0 does not match its own rest pose" % node_ram
        p["trackT"] = keys
    if rp:
        tr = _read_track(rp, classes)
        keys = _quat_keys(tr)
        quats = cumulative_quats(keys)
        assert max(abs(a - b) for a, b in zip(quats[0], p["Q"])) < 2e-3, \
            ("node 0x%08X: rotation key 0 does not reproduce its own rest quaternion "
             "(so the wxyz order or the angle/axis read is wrong)" % node_ram)
        p["trackR"] = (keys, quats, squad_tangents(quats))
    return p


def _local(pose, t):
    """(M3, t3) for one node at animation time t, in objgraph2's AUTHOR convention.
    Identical arithmetic to objgraph2.transform_of, with the tracked values swapped in.

    t is None for the REST pose: the pose record's own defaults, tracks ignored. That is
    what objgraph2.transform_of reads and therefore what bake_mesh_parts baked into the
    vertices, so it is the only correct base for a delta. Evaluating the track at t = 1
    instead would be off by the Bezier's first step, which on cursor 0x03 is 0.15 mesh
    units -- small, wrong, and exactly the kind of drift that never shows up as a crash."""
    if pose is None:
        return [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]], [0.0, 0.0, 0.0]
    T = eval_vec3(pose["trackT"], t) if (pose["trackT"] and t is not None) \
        else list(pose["T"])
    Q = eval_quat(pose["trackR"][0], pose["trackR"][1], t, pose["trackR"][2]) \
        if (pose["trackR"] and t is not None) else list(pose["Q"])
    M3 = O2.trs_mat(Q, pose["S"], pose["SO"])
    if pose["mirror"] == -1.0:
        M3 = [[-e for e in row] for row in M3]
    return M3, T


def _walk_at(root, t, classes):
    """-> {node_ram: (A, tA)} the MESH-space transform of every node at time t.

    The chain and the axis fix are objgraph2.parts_of_node's, line for line, so the
    t = rest case reproduces the transform bake5 baked into the vertices."""
    I3 = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
    out = {}
    order = []

    def rec(ram, PM, Pt, depth=0):
        if not O2._in_model_seg(ram) or depth > 4:
            return
        M3, tt = _local(_pose_of(ram, classes), t)
        WM = O2._mmul(M3, PM)
        Wt = [a + b for a, b in zip(O2._mvec_row(tt, PM), Pt)]
        out[ram] = (WM, Wt)
        order.append(ram)
        kids = O2.r32(ram + 0x0C)
        if kids is not None and O2._in_model_seg(kids):
            for k in range(O2.MAX_CHILDREN):
                cn = O2.r32(kids + k * 4)
                if not cn:
                    break
                rec(cn, WM, Wt, depth + 1)

    rec(root, I3, [0.0, 0.0, 0.0])
    P = O2.P_AUTHOR_TO_MESH
    Pt_ = O2._mt(P)
    for ram in order:
        M, tt = out[ram]
        out[ram] = (O2._mmul(Pt_, O2._mmul(M, P)), O2._mvec_row(tt, P))
    return out, order


def _inv_affine(A, t):
    """Row-vector affine inverse: (A, t)^-1 = (A^-1, -t A^-1)."""
    d = (A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
         - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
         + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]))
    assert abs(d) > 1e-12, "a cursor node's rest transform is singular"
    inv = [[(A[(i + 1) % 3][(j + 1) % 3] * A[(i + 2) % 3][(j + 2) % 3]
             - A[(i + 1) % 3][(j + 2) % 3] * A[(i + 2) % 3][(j + 1) % 3]) / d
            for i in range(3)] for j in range(3)]
    it = [-sum(t[k] * inv[k][j] for k in range(3)) for j in range(3)]
    return inv, it


def frame_times(nframes=FRAME_COUNT):
    return [f * UNITS_PER_FRAME + T_BIAS for f in range(nframes)]


def node_deltas(code, nframes=FRAME_COUNT):
    """-> (order, {node_ram: [C per frame]}) where C is [[3x3 rows], [translation]] in
    the row-vector convention bake_node_anim already writes (v' = v.M + t)."""
    classes = _track_classes()
    root = O2.cursor_node(code)
    assert root, "cursor code 0x%02X has no node" % code
    rest, order = _walk_at(root, None, classes)
    # The rest pose the BAKER used, read by completely different code. Agreement here is
    # what makes the delta mean anything at all.
    graph = {p["node"]: p for p in O2.parts_of_node(root)}
    for ram in order:
        q = graph.get(ram)
        assert q is not None, "node 0x%08X is missing from objgraph2's own walk" % ram
        A, tA = rest[ram]
        for r in range(3):
            for c in range(3):
                assert abs(A[r][c] - q["M"][r][c]) < 1e-3, \
                    "node 0x%08X rest 3x3 disagrees with the baked pose" % ram
        for k in range(3):
            assert abs(tA[k] - q["t"][k]) < 1e-2, \
                "node 0x%08X rest translation disagrees with the baked pose" % ram

    out = {ram: [] for ram in order}
    for t in frame_times(nframes):
        cur, _ = _walk_at(root, t, classes)
        for ram in order:
            Ar, tr = rest[ram]
            Aa, ta = cur[ram]
            Ai, ti = _inv_affine(Ar, tr)
            CM = O2._mmul(Ai, Aa)
            Ct = [sum(ti[k] * Aa[k][j] for k in range(3)) + ta[j] for j in range(3)]
            out[ram].append([CM[0], CM[1], CM[2], Ct])
    return order, out


def animated_codes():
    """Cursor codes whose node graph carries at least one track."""
    classes = _track_classes()
    out = []
    for code in range(O2.CURSOR_CODES):
        root = O2.cursor_node(code)
        if not root:
            continue
        _, order = _walk_at(root, None, classes)
        for ram in order:
            p = _pose_of(ram, classes)
            if p and (p["trackT"] or p["trackR"]):
                out.append(code)
                break
    return out


# ------------------------------------------------------------------ the flipbooks
def flipbook(code):
    """-> dict(images, times, period, count, width, height, fmt, siz, frame_index) for a
    cursor whose node carries a texture animation (node+0x10), else None.

    frame_index is the image the console shows on each of the 100 anim frames."""
    root = O2.cursor_node(code)
    if not root:
        return None
    p_ram = O2.r32(root + 0x10)
    if not p_ram or not O2._in_model_seg(p_ram):
        return None
    ta_ram = O2.r32(p_ram)
    tt = O2.r32(ta_ram)
    it = O2.r32(ta_ram + 4)
    word = O2.r32(ta_ram + 8)
    img0 = O2.r32(ta_ram + 0x0C)
    tail = O2.r32(ta_ram + 0x10)
    cnt = O2.r32(ta_ram + 0x14) >> 16
    assert 0 < cnt < 32, "TexAnim count %d is implausible" % cnt
    assert tail == 0, "TexAnim tail is non-zero in ROM (it is a run-time field)"
    times = [O2.r32(tt + i * 4) for i in range(cnt + 1)]
    imgs = [O2.r32(it + i * 4) for i in range(cnt)]
    assert times[0] == 0 and times == sorted(times), "TexAnim time table is not ascending"
    assert imgs[0] == img0, "TexAnim match address is not imageTable[0]"
    period = times[cnt]
    idx = []
    for t in frame_times():
        tt_ = int(t) % period
        pick = 0
        for i in range(cnt):
            if times[i + 1] < tt_:
                continue
            pick = i
            break
        idx.append(pick)
    fmt = (word >> 21) & 7
    siz = (word >> 19) & 3
    return dict(images=imgs, times=times, period=period, count=cnt,
                fmt=fmt, siz=siz, width=(word & 0xFFF) + 1, frame_index=idx)


def flipbook_codes():
    return [c for c in range(O2.CURSOR_CODES) if flipbook(c) is not None]


# ------------------------------------------------------------------ verification
def verify(out=sys.stdout):
    """The numbers an integrator must reproduce. Every one is an assert."""
    check_landmarks()
    p = out.write
    p("CURSOR ANIMATION, out of cnc_eu.z64\n")
    p("clock: frame = (frameCounter*4) mod 100, t = frame*160 + 1  "
      "(25 distinct frames, t = 1..15361)\n\n")

    anim = animated_codes()
    flips = flipbook_codes()
    p("code  mechanism\n")
    classes = _track_classes()
    for code in range(O2.CURSOR_CODES):
        root = O2.cursor_node(code)
        _, order = _walk_at(root, None, classes)
        nt = nr = 0
        for ram in order:
            q = _pose_of(ram, classes)
            if q and q["trackT"]:
                nt += 1
            if q and q["trackR"]:
                nr += 1
        fb = flipbook(code)
        if nt or nr:
            what = "%d nodes: %d translation, %d rotation tracks" % (len(order), nt, nr)
        elif fb:
            what = "texture flipbook, %d images %dx%d, period %d" % (
                fb["count"], fb["width"], len(_fb_rows(fb)), fb["period"])
        else:
            what = "STATIC"
        p("0x%02X  %s\n" % (code, what))
    p("\nstates with frameCount 100: %s\n"
      % ", ".join("%d->0x%02X" % (i, s[0])
                  for i, s in enumerate(states()) if s[2] == FRAME_COUNT))
    silent = sorted({s[0] for s in states()
                     if s[2] == FRAME_COUNT and s[0] not in anim and s[0] not in flips})
    p("frameCount 100 but NOTHING to animate: %s\n\n"
      % (", ".join("0x%02X" % c for c in silent) or "none"))

    # 1. every translation track reproduces its own key values exactly
    worst, ntr = 0.0, 0
    for code in range(O2.CURSOR_CODES):
        root = O2.cursor_node(code)
        _, order = _walk_at(root, None, classes)
        for ram in order:
            q = _pose_of(ram, classes)
            if not q or not q["trackT"]:
                continue
            ntr += 1
            for k in q["trackT"]:
                got = eval_vec3(q["trackT"], float(k["t"]))
                worst = max(worst, max(abs(a - b) for a, b in zip(got, k["v"])))
    assert worst < 1e-3, "the Bezier does not reproduce its own keys (%g)" % worst
    p("  %d translation tracks, worst key round-trip error %.3g units (must be < 1e-3)\n"
      % (ntr, worst))

    # 2. cursor 0x06's four arms travel exactly 200 units radially
    root = O2.cursor_node(6)
    _, order = _walk_at(root, None, classes)
    travel = []
    for ram in order:
        q = _pose_of(ram, classes)
        if not q or not q["trackT"]:
            continue
        ks = q["trackT"]
        travel.append(math.hypot(ks[-1]["v"][0], ks[-1]["v"][1])
                      - math.hypot(ks[0]["v"][0], ks[0]["v"][1]))
    assert len(travel) == 4 and all(abs(x - 200.0) < 0.5 for x in travel), \
        "cursor 0x06's arms do not travel 200 units: %s" % travel
    p("  cursor 0x06 (DEPLOY): four arms travel %s units radially\n"
      % ", ".join("%.1f" % x for x in travel))

    # 3. the cumulative rotation order, proven by cross-arm agreement
    deltas = []
    for ram in order:
        q = _pose_of(ram, classes)
        if not q or not q["trackR"]:
            continue
        _, quats = q["trackR"]
        d = qmul(quats[-1], qconj(quats[0]))
        if d[0] < 0.0:
            d = [-c for c in d]
        deltas.append(d)
    assert len(deltas) == 4
    spread = max(max(abs(deltas[j][c] - deltas[0][c]) for c in range(4))
                 for j in range(4))
    assert spread < 1e-5, \
        "the four arms of cursor 0x06 do not share one parent-frame rotation " \
        "(spread %g); the cumulative composition order is wrong" % spread
    ang = 2.0 * math.acos(max(-1.0, min(1.0, deltas[0][0])))
    p("  cursor 0x06: the same %.1f degree pitch on all four arms, cross-arm spread "
      "%.1e (parent-frame composition gives 1.15)\n" % (math.degrees(ang), spread))

    # 4. cursor 0x04's arms converge
    root4 = O2.cursor_node(4)
    _, order4 = _walk_at(root4, None, classes)
    a0, a1 = [], []
    for ram in order4:
        q = _pose_of(ram, classes)
        if not q or not q["trackT"]:
            continue
        a0.append(math.hypot(*eval_vec3(q["trackT"], 1.0)[:2]))
        a1.append(math.hypot(*eval_vec3(q["trackT"], 15361.0)[:2]))
    assert a0 and all(b < a for a, b in zip(a0, a1)), "cursor 0x04 does not converge"
    p("  cursor 0x04 (SELECT): arm radii %s -> %s, converging\n"
      % (", ".join("%.0f" % x for x in a0), ", ".join("%.0f" % x for x in a1)))

    # 5. the delta at frame 0 is the rest pose, so the un-animated picture is unchanged
    worst0 = 0.0
    for code in sorted(set(anim)):
        _order, dl = node_deltas(code)
        for ram in _order:
            C = dl[ram][0]
            err = max(max(abs(C[r][c] - (1.0 if r == c else 0.0)) for c in range(3))
                      for r in range(3))
            err = max(err, max(abs(x) for x in C[3]))
            worst0 = max(worst0, err)
            assert err < 1.0, \
                "cursor 0x%02X node 0x%08X: frame 0 is nowhere near the rest pose (%g); " \
                "the delta base is wrong" % (code, ram, err)
    p("  frame 0 differs from the rest pose by at most %.4g mesh units (the +1.0 time "
      "bias, i.e. 1/%d of a cell: the static picture is preserved)\n"
      % (worst0, int(1024.0 / max(worst0, 1e-9))))

    # 6. the flipbooks
    for code in flips:
        fb = flipbook(code)
        seq = fb["frame_index"]
        assert sorted(set(seq)) == list(range(fb["count"])), \
            "cursor 0x%02X's flipbook does not use all its images" % code
        p("  cursor 0x%02X flipbook: %d images, index over the 25 reachable frames %s\n"
          % (code, fb["count"], [seq[f] for f in range(0, FRAME_COUNT, FRAME_STEP)]))
    return True


def _fb_rows(fb):
    """Rows of one flipbook image, from the even image spacing."""
    stride = fb["images"][1] - fb["images"][0]
    bpp = {0: 4, 1: 8, 2: 16, 3: 32}[fb["siz"]]
    rowbytes = fb["width"] * bpp // 8
    assert stride % rowbytes == 0, "flipbook stride is not a whole number of rows"
    return range(stride // rowbytes)


if __name__ == "__main__":
    verify()
