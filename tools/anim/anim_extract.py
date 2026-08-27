#!/usr/bin/env python3
import os

# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

"""
CNC3D wave 5 -- THE ANIMATION SYSTEM extractor.

Reads data/rom/cnc_eu.z64 and emits, per animated model slot, a resampled
per-node TRS animation ready for the bakery.

EVERY constant below carries the ROM/RAM address it came from.  The cartridge
names its own source file: the assert strings reached from every routine in
this system are "../src/trackctrl.c" (RAM 0x80006844 / ROM 0x7444) and
"../src/mathutils.c" (RAM 0x80006654).

--------------------------------------------------------------------------
THE CHAIN, as disassembled
--------------------------------------------------------------------------
scene-graph node (0x1C bytes, GameModelsGeometry / ScriptModels)
    +00 Gfx*  dl
    +04 Mtx*  BAKED REST MATRIX (16 s16 int parts then 16 u16 frac parts)
    +08 Handle* {u32 classTag, void* data}
    +0C Node** children (NULL terminated)

classTag 0x800AC2C0 = CAFEDEAD.  Its record is 12 bytes
{tag, 0x8008A0B0, apply=0x8008A2B4}  (ROM 0xACEC0).

CAFEDEAD::apply(this, t, outMtx)   RAM 0x8008A2B4 / ROM 0x8AEB4:
    memcpy(local, this, 0x3C)                      (0x80058070)
    evalT(this->+0x40, t, local+0x00)              -> 0x8008AA60
    evalR(this->+0x44, t, local+0x0C)              -> 0x8008AB28
    evalS(this->+0x48, t, local+0x1C, local+0x28)  -> 0x8008ABF0
    ... then builds the matrix (0x80082104) and writes it (0x80083E08)
Each evalX is `track->cls->[+0x0C](track, t, dst)` -- a virtual dispatch.
So a NULL track leaves the static pose field untouched: the pose record's
+00 T / +0C Q / +1C S / +28 SO are the defaults.

Track object, 12 bytes: {u32 classPtr, void* keys, u32 count}
  (0x8008AD28 reads cls; 0x8008AF58 reads count at +8; 0x8008B5A8 reads keys at +4)

FEEB class table, RAM 0x800AC2F0 / ROM 0xACEF0, 12 records x 16 bytes:
    +00 u32 tag  +04 u32 keyframe stride  +08 void(*compile)(track)  +0C eval
    0001 0x40 compile 0x8008D1A4 eval 0x8008D398
    0002 0x5C compile 0x8008E20C eval 0x8008E388     <-- quaternion, 36 tracks
    0003 0x00 (empty)
    0004 0x28 compile 0x8008D13C eval 0
    0005 0x2C eval 0x8008B6B8                        <-- cubic vec3, 25 tracks
    0006 0x38 compile 0x8008BA9C eval 0x8008BBB8
    0007 0x3C eval 0x8008BC34                        <-- cubic 7-comp, 9 tracks
    0008 0x14 eval 0x8008B624
    0009 0x10 eval 0x8008BE4C                        <-- linear vec3, 16 tracks
    000A 0x14 eval 0x8008BF04                        <-- slerp quat, 11 tracks
    000B 0x20 eval 0x8008BFC0                        <-- lerp vec3 + slerp quat, 2
    000C 0x08 eval 0x8008BD94

Only 0002 0005 0007 0009 000A 000B are reachable from the 236-slot model table.

--- key search / end behaviour (0x8008AF58, ROM 0x8BB58) -------------------
    n = track->count
    if (t <= time(0))            -> (i=0,      u=0.0)          CLAMP
    if (time(n-1) < t)           -> (i=n-2,    u=1.0)          CLAMP   [0x80006858 = 1.0]
    else the first i with time(i) <= t < time(i+1), u=(t-ti)/(ti1-ti)
    fallthrough                  -> (i=n-2,    u=1.0)          [0x8000685C = 1.0]
  There is NO looping and NO wrap inside the evaluator.  Before the first key
  the first key's value is held; after the last, the last key's value is held.

--- time field ------------------------------------------------------------
  0x8008AD28 / 0x8008E5E4 / 0x8008E440 all do  cvt.s.w  on *(s32*)(key+0).
  The time is a 32-bit INTEGER at key+0x00, converted to float on read.

--- family A: cubic BEZIER with tangents (0005, 0007) ---------------------
  ctx built by 0x8008B5A8: {keys, nval, ntan, stride, count}
      0005 -> nval 3, ntan 3      0007 -> nval 7, ntan 3
  key layout (0x8008E490 / E4DC / E524 / E57C):
      +00 s32   time
      +04 u32   flags
      +08       float value[nval]
      +08+4n    float inTan[ntan]
      +08+4n+4m float outTan[ntan]
      size = 8 + 4*nval + 8*ntan     (0005: 44=0x2C, 0007: 60=0x3C -- both match)
  interp core 0x8008B1DC (ROM 0x8BDDC):
      tanOut1 = (flags(i)  >>10)&7 ; tanIn2 = (flags(i+1)>>7)&7
      if either == 2:  STEP     dst[k] = (u==1.0 ? V2[k] : V1[k]) for k<nval
      if (flags(i+1) & 0x8000) and nval==3 and i<count: return, dst UNTOUCHED
      else CUBIC BEZIER, for k<ntan:
          dt = (time(i+1)-time(i)) / 3.0          [0x80006868 = 3.0]
          p  = V1[k] + outTan1[k]*dt
          q  = V2[k] + inTan2 [k]*dt      <-- ROM ADDS. the sign is in the data.
          A  = 1.0 - u                            [0x80006860 = 1.0]
          B  = u*u
          dst[k] = A*(A*(A*V1[k] + 3*u*p) + 3*B*q) + B*u*V2[k]
                                                  [0x8000686C = 0x80006870 = 3.0]
      i.e. exactly the Bernstein cubic with controls V1, p, q, V2.
      NOT Hermite-with-slopes: the record's tangents are already the Bezier
      control OFFSETS scaled by dt.

--- family B: LINEAR / SLERP (0009, 000A, 000B, 000C) ---------------------
  no flags word at all.
      000C  +00 s32 time  +04 float v            lerp1 (0x8008BCC8)
      0009  +00 s32 time  +04 float v[3]         lerp3
      000A  +00 s32 time  +04 float q[4] wxyz    slerp (0x800879B0)
      000B  +00 s32 time  +04 float v[3] +10 float q[4]   lerp3 + slerp
  lerp (0x8008BCC8): dst[k] = (1.0-u)*a[k] + u*b[k]   [0x80006874 = 1.0]
  slerp (0x800879B0, ROM 0x885B0):
      d = dot4(a,b)
      if (d + 1.0 > 1e-6)   [0x8000673C=1.0, 0x80006740=1e-06]
          if (1.0 - d > 1e-6)  s1=sin((1-u)*th)/sin(th), s2=sin(u*th)/sin(th), th=acos(d)
          else                 s1=1-u, s2=u
          out = s1*a + s2*b
      else                     # antipodal: build a perpendicular
          w = (a[3], -a[2], a[1], -a[0])
          out = sin((1-u)*pi/2)*a + sin(u*pi/2)*w
      NOTE the ROM does NOT flip b for the shortest path.

--- family C: FEEB0002, the quaternion track (36 of them) ------------------
  stride 0x5C.  ctx2 built by 0x8008CF68: {keys, ncomp=4, stride, count,
  time(0), time(count-1), 0}.  Accessors:
      0x8008E5E4  time   = (float)*(s32*)(key+0)
      0x8008E634  flags  = *(u32*)(key+4)
      0x8008EC2C  ->key+0x08 ; +0x0C of that = key+0x14, +0x10 = key+0x18
      0x8008EC7C  V      = key+0x1C
      0x8008E9E0  V+0    spin  {float angle, float axis[3]}
      0x8008EA40  V+16   the QUATERNION value
      0x8008EB6C  V+32   squad tangent "a"
      0x8008EBCC  V+48   squad tangent "b"
      => 0x1C + 64 = 0x5C.  Confirmed byte for byte.

  *** THE PIECE EVERY EARLIER PASS MISSED ***
  In the ROM the quaternion at V+16 is IDENTITY in all 131 keys of all 36
  tracks, and the spin angle at V+0 is non-zero in 111 of them.  The
  quaternions are NOT stored: they are COMPUTED AT LOAD by the class's
  compile hook, cls+0x08 = 0x8008E20C -> 0x8008E1B0 -> 0x8008DF98 (ROM 0x8EB98):

      pass 1: for each key: flush-tiny-to-zero on all 4 floats of V+0 (0x80082060)
      pass 2: for each key i:
                  if (flags & 0x10) skip this key
                  normalise the axis V+4..V+12 in place        (0x800821BC)
                  if (angle < 0) negate all 4 floats of V+0    (0x8008DE7C)
                  V+16 = axisAngleToQuat(V+0)                  (0x800870EC)
                         q = (cos(a/2), axis*sin(a/2))         [0x80006730 = 0.5]
                  if (i != 0) V+16 = quatMul(V+16 of key i-1, V+16)  (0x80084B88)
                  flush-tiny-to-zero on V+16                   (0x80081DDC)
      pass 3: for each key: 0x8008D450 computes the squad tangents a and b.

  So the key rotations are a CUMULATIVE product of per-key angle-axis deltas.
  Ignore the compile pass and every FEEB0002 track reads as "no rotation",
  which is what the raw bytes look like and why this never reached a pack.

  evaluation, 0x8008E388 -> 0x8008E250 (ROM 0x8EE50):
      if (i1 != i2): u = ease(u, easeOut(i1)=key+0x18, easeIn(i2)=key+0x14)
                            (0x8008C0C8, ROM 0x8CCC8)
      squad(p=q(i1), a=tanA(i1), b=tanB(i2), q=q(i2), u)   via 0x800886F0
      0x80088414: h = 2*u*(1-u); out = slerp(slerp(p,q,u), slerp(a,b,u), h)
      0x800886F0 only consults the spin angle when angle*0.5 >= pi (a multi
      turn spin); no track in this ROM does, asserted below.

  ease(u,e1,e2), 0x8008C0C8, constants 0x80006878..0x80006890:
      if u==0 or u==1 or e1+e2==0: u
      if e1+e2 > 1: e1,e2 /= (e1+e2)
      k = 1/(2 - e1 - e2)
      u < e1        -> (k/e1)*u*u
      u < 1-e2      -> (2u - e1)*k
      else          -> 1 - (k/e2)*(1-u)^2

  *** REGISTERED GAP ***  The squad TANGENTS a and b are computed by
  0x8008D450 (ROM 0x8E050 .. ~0x8E7xx), a Kochanek-Bartels quaternion spline
  with non-uniform time weighting: it takes log(delta) of the neighbouring
  keys (0x80088198 -> 0x800878C8 then quat-log 0x80087D88) and blends them
  with per-key ease/tension read through 0x8008E6E0, then exponentiates.
  Its tail was NOT transcribed.  This extractor therefore SLERPs between the
  compiled key quaternions.  squad and slerp agree EXACTLY at every key time
  (squad(u=0)=p, squad(u=1)=q), so every keyframe in the output is exact; they
  differ only in the interior easing of a segment.  Every FEEB0002 track in
  this ROM is asserted single-axis below, so the PATH is identical and only
  the speed along it can differ.

--------------------------------------------------------------------------
THE CLOCK
--------------------------------------------------------------------------
The evaluator entry is 0x8007BE30(scene, dl, t) with t a FLOAT in the same
units as the key time field.  Two call paths:

  * in-game objects, RAM 0x8004C108 (the draw-command interpreter that adds
    the +10 model-table bias: s4 = 0x80099A38 + idx*16, and
    0x80099A38 = modelTable 0x80099998 + 10*16):
        f22 = *(float*)(drawCmd + 0x14)                (RAM 0x8004C12C)
        NOT CONFIRMED as the clip time. Traced further, f22's only uses are two
        mul.s at RAM 0x8004C730 / 0x8004C7B0 under a flag-bit-8 branch, feeding
        0x8008FD14 as a single float -- which reads as a fade or size argument.
        Treat this as a lead. The per-model SCALE demonstrably goes elsewhere
        (0x8007C710 inits param+0x24 to 1.0f; 0x8004C630 overwrites it via
        0x8007C81C with the per-slot call at 0x80056768).
        t   = f22 * 160.0 + 1.0                        (RAM 0x8004C7A8..0x8004C7C0)
        consts RAM 0x80004964 = 160.0, 0x80004968 = 1.0
    and the same arithmetic again in 0x8004A690 (RAM 0x8004A70C..0x8004A728,
    consts 0x800047B8 = 160.0, 0x800047BC = 1.0), the shared
    "eval scene at frame" helper that every cursor/marker draw funnels into.

    => 160 TRACK-TIME UNITS PER ANIMATION FRAME, and t = frame*160 + 1.

  * the intro / menu ScriptModels draw, RAM 0x8004BB9C, is a DIFFERENT path:
    t = frameFloat * 160.0 (const RAM 0x80004824 = 160.0) and it sets a
    per-index playback RATE from a 10-entry float table at RAM 0x80099970 /
    ROM 0x9A570 = [1.0, 0.3 x9], read by its only caller 0x80056714.
    *** That table is NOT a per-model clock. ***  It has ten entries, it sits
    immediately below the model table, and 0x80056714 has exactly one caller.
    An earlier note calling RAM 0x8004BB9C "a per-MODEL-INDEX clock" is wrong
    on both counts.  There is no per-instance and no per-slot animation clock:
    the frame number is a field of the draw command.

  NOT FOUND: what the game logic writes into drawCmd+0x14 for an ordinary
  building.  The cursor's own frame is (globalFrameCounter*4) % frameCount
  with frameCount = 100 from the cursor state table (RAM 0x8004CB20..0x8004CB54
  and 0x8004CDF0..0x8004CE24), so the cursor advances 4 anim frames per game
  frame.  No equivalent expression was located for world objects.
"""
import os, sys, json, math, struct

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_ROOT, "tools", "bakery"))
import objgraph2 as O2                     # scene-graph walker (both segments)

ROM = O2.rom()
RES_DELTA = 0x80000400 - 0x1000            # resident: rom = ram - delta
MODEL_TABLE = 0x80099998                   # RAM; 236 x 16, node ptr at +0x0C
TABLE_SLOTS = 236
FEEB_TABLE = 0x800AC2F0                    # RAM; 12 x 16
TAG_CAFEDEAD = 0x800AC2C0
TICKS_PER_FRAME = 160.0                    # RAM 0x80004964 / 0x800047B8 / 0x80004824
T_BIAS = 1.0                               # RAM 0x80004968 / 0x800047BC

def res_u32(a): return struct.unpack_from(">I", ROM, a - RES_DELTA)[0]
def m_off(a):   return O2.ram2rom(a)
def m_u32(a):   return O2.r32(a)
def m_f32(a):   return O2.rf32(a)

# Slot names: joined from tools/bakery/support/unit_models.json (model_index).
# Slots 0..9 are NOT world objects: they are the intro/menu ScriptModels drawn
# by RAM 0x8004BB9C, whose playback rate comes from the 10-entry float table at
# RAM 0x80099970 / ROM 0x9A570 = [1.0, 0.3 x9].
SCRIPT_MODEL_RATE = [1.0] + [0.3] * 9      # ROM 0x9A570

def _slot_names():
    import json as _j
    path = os.path.join(_ROOT, "tools", "bakery", "support", "unit_models.json")
    out = {}
    try:
        for k, v in _j.load(open(path)).items():
            mi = v.get("model_index")
            if mi is not None:
                out.setdefault(mi, []).append(k)
    except Exception:
        pass
    return out
SLOT_NAMES = _slot_names()

# ------------------------------------------------------------------ classes
CLASSES = {}
for _i in range(12):
    _a = FEEB_TABLE + _i * 16
    _tag, _stride, _comp, _eval = struct.unpack_from(">4I", ROM, _a - RES_DELTA)
    CLASSES[_a] = dict(tag=_tag, stride=_stride, compile=_comp, eval=_eval)

# eval-site derived (nval, ntan) for the Bezier family; from the 0x8008B5A8 calls
BEZIER_SHAPE = {0xFEEB0005: (3, 3),        # RAM 0x8008B6FC: a2=3 a3=3
                0xFEEB0007: (7, 3)}        # RAM 0x8008BC78: a2=7 a3=3
LINEAR_SHAPE = {0xFEEB0009: ("v3",),       # RAM 0x8008BE4C
                0xFEEB000A: ("q4",),       # RAM 0x8008BF04
                0xFEEB000B: ("v3", "q4"),  # RAM 0x8008BFC0
                0xFEEB000C: ("f1",)}       # RAM 0x8008BD94

# ------------------------------------------------------------------ math
def q_mul(a, b):
    """quatMul, transcribed from RAM 0x80084B88 (ROM 0x85788), 9-multiply form.
    Constants 0x800066D0..0x800066DC are all 0.5."""
    t10 = (a[0] + a[1]) * (b[0] + b[1])
    t14 = (a[3] - a[2]) * (b[2] - b[3])
    t18 = (a[1] - a[0]) * (b[2] + b[3])
    t1c = (a[2] + a[3]) * (b[1] - b[0])
    t20 = (a[1] + a[3]) * (b[1] + b[2])
    t24 = (a[1] - a[3]) * (b[1] - b[2])
    t28 = (a[0] + a[2]) * (b[0] - b[3])
    t2c = (a[0] - a[2]) * (b[0] + b[3])
    return [t14 + (-t20 - t24 + t28 + t2c) * 0.5,
            t10 - ( t20 + t24 + t28 + t2c) * 0.5,
            -t18 + ( t20 - t24 + t28 - t2c) * 0.5,
            -t1c + ( t20 - t24 - t28 + t2c) * 0.5]

def aa_to_quat(aa):
    """RAM 0x800870EC: q = (cos(a/2), axis*sin(a/2)).  0x80006730 = 0.5."""
    h = aa[0] * 0.5
    c, s = math.cos(h), math.sin(h)
    return [c, aa[1] * s, aa[2] * s, aa[3] * s]

def v3_normalise(v):
    """RAM 0x800821BC; zero length -> (1,0,0) (const 0x80006674 = 1.0)."""
    L = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if L == 0.0:
        return [1.0, 0.0, 0.0]
    return [v[0] / L, v[1] / L, v[2] / L]

def flush_tiny(x):
    """RAM 0x80081CA8: clamp a near-zero float to exact zero."""
    return 0.0 if -1e-20 < x < 1e-20 else x

def dot4(a, b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3]

def slerp(a, b, u):
    """RAM 0x800879B0 exactly, including the antipodal branch."""
    d = dot4(a, b)
    if d + 1.0 > 1e-06:                                   # 0x80006740 = 1e-06
        if 1.0 - d > 1e-06:
            th = math.acos(max(-1.0, min(1.0, d)))
            st = math.sin(th)
            s1 = math.sin((1.0 - u) * th) / st
            s2 = math.sin(u * th) / st
        else:
            s1, s2 = 1.0 - u, u
        return [s1*a[0] + s2*b[0], s1*a[1] + s2*b[1],
                s1*a[2] + s2*b[2], s1*a[3] + s2*b[3]]
    w = [a[3], -a[2], a[1], -a[0]]                        # RAM 0x80087C2C
    s1 = math.sin((1.0 - u) * math.pi / 2.0)
    s2 = math.sin(u * math.pi / 2.0)
    return [s1*a[0] + s2*w[0], s1*a[1] + s2*w[1],
            s1*a[2] + s2*w[2], s1*a[3] + s2*w[3]]

def ease_remap(u, e1, e2):
    """RAM 0x8008C0C8."""
    if u == 0.0 or u == 1.0:
        return u
    s = e1 + e2
    if s == 0.0:
        return u
    if s > 1.0:
        e1, e2 = e1 / s, e2 / s
    k = 1.0 / (2.0 - e1 - e2)
    if u < e1:
        return (k / e1) * u * u
    if u < 1.0 - e2:
        return (2.0 * u - e1) * k
    v = 1.0 - u
    return 1.0 - (k / e2) * v * v

# ------------------------------------------------------------------ tracks
def read_track(tp):
    """Track object at RAM tp -> dict, keys decoded, FEEB0002 compiled."""
    cls_ptr = m_u32(tp)
    keys_p  = m_u32(tp + 4)
    count   = m_u32(tp + 8)
    cls = CLASSES.get(cls_ptr)
    if cls is None or not keys_p or not O2._in_model_seg(keys_p) or not count:
        return None
    tag, stride = cls["tag"], cls["stride"]
    base = m_off(keys_p)
    raw = [ROM[base + i*stride: base + (i+1)*stride] for i in range(count)]
    tr = dict(cls=cls_ptr, tag=tag, stride=stride, count=count,
              keys_ram=keys_p, keys_rom=base,
              time=[struct.unpack_from(">i", b, 0)[0] for b in raw])

    def fv(b, o, n): return [struct.unpack_from(">f", b, o + 4*k)[0] for k in range(n)]

    if tag in BEZIER_SHAPE:
        nval, ntan = BEZIER_SHAPE[tag]
        assert stride == 8 + 4*nval + 8*ntan, (hex(tag), stride)
        tr.update(kind="bezier", nval=nval, ntan=ntan,
                  flags=[struct.unpack_from(">I", b, 4)[0] for b in raw],
                  val=[fv(b, 8, nval) for b in raw],
                  itan=[fv(b, 8 + 4*nval, ntan) for b in raw],
                  otan=[fv(b, 8 + 4*nval + 4*ntan, ntan) for b in raw])
    elif tag in LINEAR_SHAPE:
        parts = LINEAR_SHAPE[tag]
        tr.update(kind="linear", parts=list(parts))
        o = 4
        for p in parts:
            n = {"f1": 1, "v3": 3, "q4": 4}[p]
            tr[p] = [fv(b, o, n) for b in raw]
            o += 4*n
        assert stride == o, (hex(tag), stride, o)
    elif tag == 0xFEEB0002:
        assert stride == 0x5C
        spin  = [fv(b, 0x1C, 4) for b in raw]
        stat  = [fv(b, 0x2C, 4) for b in raw]
        flags = [struct.unpack_from(">I", b, 4)[0] for b in raw]
        ez_in = [struct.unpack_from(">f", b, 0x14)[0] for b in raw]
        ez_out= [struct.unpack_from(">f", b, 0x18)[0] for b in raw]
        # --- the compile pass, RAM 0x8008DF98 -----------------------------
        q = [None]*count
        prev = None
        for i in range(count):
            s = [flush_tiny(x) for x in spin[i]]
            if flags[i] & 0x10:                 # RAM 0x8008E040: skipped key
                q[i] = list(prev) if prev is not None else [1.0, 0.0, 0.0, 0.0]
                continue
            ax = v3_normalise(s[1:4])
            s = [s[0]] + ax
            if s[0] < 0.0:                      # RAM 0x8008DE7C
                s = [-x for x in s]
            qi = aa_to_quat(s)
            if i != 0 and prev is not None:
                qi = q_mul(prev, qi)            # RAM 0x8008E114, cumulative
            qi = [flush_tiny(x) for x in qi]
            q[i] = qi
            prev = qi
        tr.update(kind="quat2", spin=spin, static=stat, flags=flags,
                  ease_in=ez_in, ease_out=ez_out, q=q)
    else:
        tr.update(kind="unknown")
    return tr

def bounds(times, t):
    """RAM 0x8008AF58.  Clamp both ends; returns (i, u)."""
    n = len(times)
    if n == 1:
        return 0, 0.0
    if t <= times[0]:
        return 0, 0.0
    if times[n-1] < t:
        return n-2, 1.0
    for i in range(n-1):
        a, b = times[i], times[i+1]
        if a <= t < b:
            return i, (t - a) / (b - a)
    return n-2, 1.0

def eval_track(tr, t, fallback):
    """Evaluate one track at time t.  `fallback` is the static pose value the
    ROM leaves in place when a channel is not written."""
    i, u = bounds(tr["time"], t)
    j = min(i + 1, tr["count"] - 1)
    if tr["kind"] == "bezier":
        nval, ntan = tr["nval"], tr["ntan"]
        out = list(fallback) + [0.0] * max(0, nval - len(fallback))
        out = out[:nval]
        f1, f2 = tr["flags"][i], tr["flags"][j]
        if ((f1 >> 10) & 7) == 2 or ((f2 >> 7) & 7) == 2:          # STEP
            src = tr["val"][j] if u == 1.0 else tr["val"][i]
            return list(src)
        if (f2 & 0x8000) and nval == 3 and i < tr["count"]:        # HOLD static
            return list(out)
        dt = (tr["time"][j] - tr["time"][i]) / 3.0
        A, B = 1.0 - u, u * u
        V1, V2 = tr["val"][i], tr["val"][j]
        for k in range(ntan):
            p = V1[k] + tr["otan"][i][k] * dt
            q = V2[k] + tr["itan"][j][k] * dt
            out[k] = A*(A*(A*V1[k] + 3.0*u*p) + 3.0*B*q) + B*u*V2[k]
        return out
    if tr["kind"] == "linear":
        res = []
        for p in tr["parts"]:
            a, b = tr[p][i], tr[p][j]
            if p == "q4":
                res += slerp(a, b, u)
            else:
                res += [(1.0 - u)*a[k] + u*b[k] for k in range(len(a))]
        return res
    if tr["kind"] == "quat2":
        if i != j:
            u = ease_remap(u, tr["ease_out"][i], tr["ease_in"][j])
        return quat2_blend(tr, i, j, u)
    return list(fallback)

# --- FEEB0002 blend, RAM 0x800886F0 (ROM 0x892F0) --------------------------
# 0x800885E8 (ROM 0x891E8): halfTurn(q, axis) = quatMul(q, (0, axis)) -- a pure
# quaternion is a 180 degree rotation, so this is q composed with a half turn.
# Constants: 0x800067A0 = 0.5, 0x800067A8/0x800067B0 = 3.1414926500025264
# (the ROM's own slightly-loose pi), 0x800067B8/C8/D8 = 3.14159265,
# 0x800067C0/D0/D4 = 1.0, 0x800067E0/E4 = 2.0.
PI_LOOSE = 3.1414926500025264
PI_ROM   = 3.14159265

def half_turn(q, axis):
    return q_mul(q, [0.0, axis[0], axis[1], axis[2]])

def quat2_blend(tr, i, j, u):
    """squad is degraded to slerp -- see REGISTERED GAP in the header.  The
    multi-turn arithmetic below IS the ROM's, transcribed."""
    q1, q2 = tr["q"][i], tr["q"][j]
    spin = tr["spin"][j]
    theta = spin[0]
    axis = v3_normalise([flush_tiny(x) for x in spin[1:4]])
    if theta < 0.0:
        theta, axis = -theta, [-x for x in axis]     # RAM 0x8008DE7C did this in place
    half = theta * 0.5
    if half < PI_LOOSE:
        return slerp(q1, q2, u)                      # squad(p=q1, .., q=q2)
    n = 0.0
    while half > PI_LOOSE:
        half -= PI_ROM
        n += 1.0
    if half < 0.0:
        half = 0.0
    w = (u * theta) / PI_ROM
    if w < 1.0:
        return slerp(q1, half_turn(q1, axis), w)     # squad(p=q1,..,q=tmp)
    z = (w + 1.0) - 2.0 * (n + half / PI_ROM)
    if z <= 0.0:
        while w >= 2.0:
            w -= 2.0
        return slerp(q1, half_turn(q1, axis), w)
    t40 = [-x for x in half_turn(q2, axis)]          # RAM 0x80087214 negate
    return slerp(t40, q2, z)

# ------------------------------------------------------------------ graph
def pose_record(node):
    """The CAFEDEAD 0x4C-byte pose record, or None if this node is not CAFEDEAD."""
    h = m_u32(node + 8)
    if h is None or not O2._in_model_seg(h):
        return None
    if m_u32(h) != TAG_CAFEDEAD:
        return None
    d = m_u32(h + 4)
    if not d or not O2._in_model_seg(d):
        return None
    return d


def beefed_local(node):
    """The REST local transform of a BEEFED02 node, in the AUTHOR frame.

    THE BUG THIS FIXES. pose_record() above only accepts CAFEDEAD, and the caller
    used to treat "no pose record" as "identity". BEEFED02 nodes -- the parts the
    game moves at runtime, turrets, rotors, the weapons-factory doors -- therefore
    composed as if they sat at the origin with no rotation, so every node BELOW one
    inherited a wrong parent and the whole subtree's rest pose was wrong. Measured
    before the fix: five nodes across slots 11, 24 and 25 off by up to 650 model
    units of translation, which is most of a cell.

    objgraph2 has always had the right answer (_beefed_transform: the rest pose is
    the first key value of each of the four 0x1C curve descriptors at data+0x00 T,
    +0x1C R, +0x38 S, +0x54 SO). Its values are already in the MESH (Y-up) frame,
    so the author->mesh conjugation is UNDONE here exactly as parts_of_node() does
    it, because compose() below accumulates in the author frame and applies P once
    at the end. The undo is P.M.P^T; see parts_of_node for why writing it the other
    way round doubles the conjugation instead of cancelling it, and for the ROM
    measurement that settles which of the two the cartridge means.

    -> (M3, t) in the author frame, or None if this node is not BEEFED02.
    """
    h = m_u32(node + 8)
    if h is None or not O2._in_model_seg(h):
        return None
    if m_u32(h) != O2.TAG_BEEFED02:
        return None
    d = m_u32(h + 4)
    if not d or not O2._in_model_seg(d):
        return None
    r = O2._beefed_transform(d)
    if r is None:
        return None
    M3, t = r
    Pinv = O2._mt(O2.P_AUTHOR_TO_MESH)                    # == P^T
    return (O2._mmul(O2.P_AUTHOR_TO_MESH, O2._mmul(M3, Pinv)),
            O2._mvec_row(t, Pinv))

"""---- BEEFED02 ANIMATION -----------------------------------------------------------
THE SECOND NODE CLASS, and the reason no idle animation was seen on any structure.

The scene graph has THREE node handle classes, not one. The table at RAM 0x800AC2C0 is
CAFEDEAD, then BEEFED02 at 0x800AC2D8 (apply 0x8008A6C8), then the FEEB records.
pose_record() above accepts CAFEDEAD ONLY, and slot_tracks() reads its tracks out of the
pose record's +0x40/+0x44/+0x48. A BEEFED02 node has no pose record, so it contributed NO
TRACKS and the slot looked unanimated. beefed_local() (added 15 Aug) fixed those nodes'
REST pose and stopped there; their MOTION has never been read.

What that cost, counted by walking every structure's graph:
    PYLE  3 BEEFED02, 0 CAFEDEAD   -- invisible. This is the static Barracks flag.
    HQ    2 BEEFED02, 0 CAFEDEAD   -- invisible.
    SAM   3 and 3                  -- half read, which is exactly why its draw arm
                                      reaches frame 400 against a clip we read as 100.
    EYE   1 and 5,  AFLD 1 and 2   -- partially read.
    FACT  9 CAFEDEAD, 0 BEEFED02   -- so its fans really are absent from the scene
                                      graph, confirming that measurement independently.

THE CURVE FORMAT, read out of the ROM rather than assumed. A BEEFED02 handle's data
points at four 0x1C-byte curve descriptors: T at +0x00 (3), R at +0x1C (4), S at +0x38
(3), SO at +0x54 (4). objgraph2._curve_rest already parses a descriptor -- {data, ?,
nkeys, stride} -- but reads key 0 only. Each key is `stride` floats, stride == 1 + 3*dim:

    [0]              the key's DURATION in ticks (a DELTA, not an absolute time)
    [1 .. dim]       control point 1     }  NOT USED HERE -- see the honesty note below
    [dim+1 .. 2dim]  control point 2     }
    [2dim+1 .. 3dim] the key's VALUE

Verified on PYLE: durations 0, 20160, 11040, 14880, 1920 accumulate to exactly 48000, and
all three of its nodes share that same 48000-tick period. Every R value is a unit
quaternion to four decimals while the control points are not, which is what tells the
value block from the control blocks.

WHAT IS OURS AND NOT A DECODE, stated plainly: the control points are IGNORED and
consecutive keys are blended with slerp/lerp. The cartridge's evaluator (0x8007FC6C, whose
cycle controller at 0x8007F264 slides the window by whole periods and is why these clips
LOOP where FEEB/CAFEDEAD cannot) presumably runs a cubic through those control points. So
our key poses are exact and our in-betweens are straighter than the console's. Registered
as a known gap."""

def beefed_curves(node):
    """{'T':curve,'R':curve,'S':curve,'SO':curve} for a BEEFED02 node, or None.

    A `curve` is {'time': [cumulative ticks], 'val': [[dim floats]], 'dim': n}."""
    h = m_u32(node + 8)
    if h is None or not O2._in_model_seg(h) or m_u32(h) != O2.TAG_BEEFED02:
        return None
    d = m_u32(h + 4)
    if not d or not O2._in_model_seg(d):
        return None
    src = m_u32(d)
    if not src or not O2._in_model_seg(src):
        return None
    out = {}
    for off, dim, nm in ((0x00, 3, "T"), (0x1C, 4, "R"), (0x38, 3, "S"), (0x54, 4, "SO")):
        desc = src + off
        data, nkeys, stride = m_u32(desc), m_u32(desc + 8), m_u32(desc + 0x0C)
        if not data or not O2._in_model_seg(data) or not nkeys:
            continue
        if stride != 1 + 3 * dim:          # not the shape objgraph2 proved; skip loudly
            continue
        times, vals, acc = [], [], 0.0
        for k in range(nkeys):
            b = data + k * stride * 4
            acc += m_f32(b)                                    # duration, a DELTA
            times.append(acc)
            vals.append([m_f32(b + (1 + 2 * dim + i) * 4) for i in range(dim)])
        out[nm] = dict(time=times, val=vals, dim=dim, count=nkeys)
    return out or None


def beefed_eval(curve, t):
    """One curve at time t. Quaternions slerp, vectors lerp; clamped at both ends
    exactly as the FEEB key search does (0x8008AF58)."""
    ts, vs = curve["time"], curve["val"]
    if len(ts) == 1 or t <= ts[0]:
        return list(vs[0])
    if t >= ts[-1]:
        return list(vs[-1])
    i = 0
    while i + 1 < len(ts) and not (ts[i] <= t <= ts[i + 1]):
        i += 1
    j = min(i + 1, len(ts) - 1)
    span = ts[j] - ts[i]
    u = 0.0 if span <= 0 else (t - ts[i]) / span
    a, b = vs[i], vs[j]
    if curve["dim"] == 4:
        return slerp(a, b, u)
    return [(1.0 - u) * a[k] + u * b[k] for k in range(len(a))]


def beefed_local_at(curves, t):
    """The BEEFED02 node's local transform at time t, in the AUTHOR frame.

    Deliberately built with objgraph2's own _beefed_transform maths and then run
    through the SAME author<-mesh conjugation beefed_local() applies to the rest
    pose, so the animated pose and the rest pose can never end up in different
    frames -- which is the bug class that cost this project a whole wave once."""
    T  = beefed_eval(curves["T"],  t) if "T"  in curves else [0.0, 0.0, 0.0]
    Q  = beefed_eval(curves["R"],  t) if "R"  in curves else [1.0, 0.0, 0.0, 0.0]
    S  = beefed_eval(curves["S"],  t) if "S"  in curves else [1.0, 1.0, 1.0]
    SO = beefed_eval(curves["SO"], t) if "SO" in curves else [1.0, 0.0, 0.0, 0.0]
    M3 = O2.trs_mat(Q, S, SO)
    Pinv = O2._mt(O2.P_AUTHOR_TO_MESH)
    return (O2._mmul(O2.P_AUTHOR_TO_MESH, O2._mmul(M3, Pinv)),
            O2._mvec_row(T, Pinv))


def beefed_is_animated(curves):
    """True if any channel really moves. A BEEFED02 node whose every curve is a
    single key, or two identical keys, is a REST POSE holder and not animation --
    PYLE's T curves are exactly that (2 keys, same value, 0 and 48000)."""
    for c in curves.values():
        if c["count"] < 2:
            continue
        v0 = c["val"][0]
        for v in c["val"][1:]:
            if any(abs(a - b) > 1e-6 for a, b in zip(v, v0)):
                return True
    return False


def static_pose(d):
    return dict(T=[m_f32(d + 4*k) for k in range(3)],
                Q=[m_f32(d + 0x0C + 4*k) for k in range(4)],
                S=[m_f32(d + 0x1C + 4*k) for k in range(3)],
                SO=[m_f32(d + 0x28 + 4*k) for k in range(4)],
                mirror=m_f32(d + 0x38),
                flags=m_u32(d + 0x3C))

def rest_matrix(node):
    """node+0x04 is an N64 Mtx: 16 s16 integer parts then 16 u16 fractions."""
    p = m_u32(node + 4)
    if not p or not O2._in_model_seg(p):
        return None
    o = m_off(p)
    ints = struct.unpack_from(">16h", ROM, o)
    frac = struct.unpack_from(">16H", ROM, o + 32)
    return [[ints[r*4+c] + frac[r*4+c] / 65536.0 for c in range(4)] for r in range(4)]

def walk(root):
    """Full scene-graph walk, cycle guarded, no child-count or depth cap."""
    out, seen = [], set()
    def rec(n, depth):
        if not O2._in_model_seg(n) or n in seen or depth > 16:
            return
        seen.add(n)
        out.append((n, depth))
        kids = m_u32(n + 0x0C)
        if kids and O2._in_model_seg(kids):
            k = 0
            while True:
                cn = m_u32(kids + k*4)
                if not cn or not O2._in_model_seg(cn) or k > 64:
                    break
                rec(cn, depth + 1)
                k += 1
    rec(root, 0)
    return out

# --------------------------------------------------- transform composition
def quat_mat(q):
    """QuatToMat3, RAM 0x80086D94 (same as objgraph2.quat_mat). q=(w,x,y,z)."""
    return O2.quat_mat(q)

def node_local(T, Q, S, SO, mirror):
    """The CAFEDEAD local transform, identical to objgraph2.transform_of()
    but with animated values substituted."""
    M3 = O2.trs_mat(Q, S, SO)
    if mirror == -1.0:
        M3 = [[-e for e in row] for row in M3]
    return M3, list(T)

P = O2.P_AUTHOR_TO_MESH
PT = O2._mt(P)

def compose(tree, poses, locals_=None):
    """tree = [(node, depth, parentIndex)], poses = {node: (T,Q,S,SO,mirror)}
    -> {node: (M3, t)} in the MESH frame, objgraph2's row-vector convention.

    locals_ optionally maps a node to a ready-made author-frame (M3, t) that
    replaces the TRS build. BEEFED02 nodes use it: their rest pose comes out of
    four animation CURVES rather than a TRS pose record, so there is no
    quaternion to feed node_local() with (beefed_local())."""
    I3 = [[1.0,0,0],[0,1.0,0],[0,0,1.0]]
    locals_ = locals_ or {}
    acc = {}
    stack = []
    for idx, (n, depth, parent) in enumerate(tree):
        PM, Pt = (acc[parent] if parent is not None else (I3, [0.0,0.0,0.0]))
        if n in locals_:
            M3, t = locals_[n]
        else:
            T, Q, S, SO, mir = poses[n]
            M3, t = node_local(T, Q, S, SO, mir)
        WM = O2._mmul(M3, PM)
        Wt = [a + b for a, b in zip(O2._mvec_row(t, PM), Pt)]
        acc[n] = (WM, Wt)
    return {n: (O2._mmul(PT, O2._mmul(M, P)), O2._mvec_row(t, P))
            for n, (M, t) in acc.items()}

def mat_inv_affine(M, t):
    """Inverse of the row-vector affine (v -> v@M + t).  M is 3x3."""
    a,b,c = M[0]; d,e,f = M[1]; g,h,i = M[2]
    det = a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g)
    inv = [[(e*i-f*h)/det, (c*h-b*i)/det, (b*f-c*e)/det],
           [(f*g-d*i)/det, (a*i-c*g)/det, (c*d-a*f)/det],
           [(d*h-e*g)/det, (b*g-a*h)/det, (a*e-b*d)/det]]
    nt = [-(t[0]*inv[0][0] + t[1]*inv[1][0] + t[2]*inv[2][0]),
          -(t[0]*inv[0][1] + t[1]*inv[1][1] + t[2]*inv[2][1]),
          -(t[0]*inv[0][2] + t[1]*inv[1][2] + t[2]*inv[2][2])]
    return inv, nt

# ------------------------------------------------------------------ main
def slot_tracks(root):
    """[(node, depth, parent, {'T':tr,'R':tr,'S':tr}, poseRecordRAM)]"""
    nodes = walk(root)
    # parent map
    parent, stack = {}, []
    for n, d in nodes:
        while len(stack) > d:
            stack.pop()
        parent[n] = stack[-1] if stack else None
        stack.append(n)
    rows = []
    for n, d in nodes:
        pr = pose_record(n)
        trs = {}
        if pr:
            for off, nm in ((0x40, "T"), (0x44, "R"), (0x48, "S")):
                tp = m_u32(pr + off)
                if tp and O2._in_model_seg(tp):
                    t = read_track(tp)
                    if t:
                        trs[nm] = t
        rows.append((n, d, parent[n], trs, pr))
    return rows


def slot_beefed(root):
    """{node: curves} for every BEEFED02 node under root that actually MOVES.

    Separate from slot_tracks because a BEEFED02 node's animation is not a FEEB
    track object at all -- it is the same four curve descriptors its rest pose
    comes from, read past key 0."""
    out = {}
    for n, _d in walk(root):
        cur = beefed_curves(n)
        if cur and beefed_is_animated(cur):
            out[n] = cur
    return out

def clip_split(times, gap=8000):
    """Extractor-side segmentation (NOT a ROM fact): split the union of key
    times wherever nothing happens for more than `gap` ticks."""
    ts = sorted(set(times))
    if not ts:
        return []
    clips, s = [], ts[0]
    for a, b in zip(ts, ts[1:]):
        if b - a > gap:
            clips.append((s, a)); s = b
    clips.append((s, ts[-1]))
    # DEGENERATE SEGMENTS ARE KEPT, and the caller drops them AFTER it has confirmed the
    # gaps against the pose. Filtering them here is what hid the Hand of Nod: its three
    # keys are at 0, 15840 and 16000, so the first segment is the single point (0, 0),
    # this function threw it away, only one clip came out, and the merge that would have
    # rejoined them never had two clips to look at. One frame of a hundred survived.
    return clips or [(ts[0], ts[-1])]

def main():
    # Default straight into the bakery's own anim/ directory: the baker reads
    # exactly what this writes, with no manual copy step in between (a copy step
    # is how a stale slot_NNN.json outlives a fixed extractor).
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(HERE), "bakery", "anim")
    os.makedirs(outdir, exist_ok=True)
    # part-index join key: exactly bake5.py's filtered walk order
    summary, nchecks = [], 0
    rest_ok = rest_seen = 0
    used_tags = {}
    total_tracks = 0
    so_varies = 0
    spin_over_2pi = 0
    n_beefed_fixed = [0]   # BEEFED02 nodes that used to compose as identity
    n_beefed_anim  = [0]   # BEEFED02 curves that really move (the flag, the dish)

    for slot in range(TABLE_SLOTS):
        root = O2.node_ptr(slot)
        if not root:
            continue
        rows = slot_tracks(root)
        bcurves = slot_beefed(root)
        if not any(r[3] for r in rows) and not bcurves:
            continue
        # bake5.py's mesh part order: O2.parts(slot) filtered on gfx_rom
        bparts = [p for p in O2.parts(slot) if p["gfx_rom"] is not None]
        bindex = {}
        for pi, p in enumerate(bparts):
            bindex.setdefault(p["node"], pi)

        alltimes = []
        for _n, _d, _p, trs, _pr in rows:
            for tr in trs.values():
                alltimes += tr["time"]
                total_tracks += 1
                used_tags[tr["tag"]] = used_tags.get(tr["tag"], 0) + 1
                if tr["kind"] == "quat2":
                    for s in tr["spin"]:
                        if abs(s[0]) * 0.5 >= math.pi:
                            spin_over_2pi += 1
        # BEEFED02 IS NOT SEGMENTED BY clip_split, and that is deliberate.
        # clip_split is an extractor-side heuristic ("split wherever nothing happens
        # for 8000 ticks") and it says so in its own docstring. It is right for FEEB
        # clips, which are laid out as separate events along one long timeline. It is
        # WRONG for BEEFED02, whose evaluator runs a pre/post-infinity cycle
        # controller (RAM 0x8007F264) that slides the window by whole PERIODS -- so
        # the period IS the clip. Applied blindly it cut PYLE's single 48000-tick
        # flag cycle into two "one-shot" fragments at 13920..34240 and 44640..48000,
        # which is exactly backwards for the one animation in the cartridge that
        # provably loops.
        bperiod = 0.0
        for cur in bcurves.values():
            for c in cur.values():
                bperiod = max(bperiod, c["time"][-1])
                n_beefed_anim[0] += 1
        clips = clip_split(alltimes) if alltimes else []
        nbeefed = 1 if bperiod > 0 else 0
        if bperiod > 0:
            # first, because mesh_anim_t() in the renderer takes clips[0] as the idle
            # loop by convention, and the BEEFED02 cycle is precisely that.
            clips = [(0, int(round(bperiod)))] + clips
            alltimes += [0, int(round(bperiod))]
        t0, t1 = min(alltimes), max(alltimes)

        # static poses (the rest pose the baker already bakes into vertices).
        # A node that is neither CAFEDEAD nor BEEFED02 -- the ROOT of most rigs --
        # genuinely has no transform and is identity; a BEEFED02 node is NOT, and
        # falling through to identity there is the bug beefed_local() fixes.
        statics, beefed = {}, {}
        for n, d, p, trs, pr in rows:
            statics[n] = static_pose(pr) if pr else dict(
                T=[0.0]*3, Q=[1.0,0.0,0.0,0.0], S=[1.0]*3,
                SO=[1.0,0.0,0.0,0.0], mirror=1.0, flags=0)
            if pr is None:
                bl = beefed_local(n)
                if bl is not None:
                    beefed[n] = bl
                    n_beefed_fixed[0] += 1
        tree = [(n, d, p) for n, d, p, _t, _pr in rows]
        rest_world = compose(tree, {n: (s["T"], s["Q"], s["S"], s["SO"], s["mirror"])
                                    for n, s in statics.items()}, beefed)

        def pose_at(t):
            got = {}
            for n, d, p, trs, pr in rows:
                s = statics[n]
                T = eval_track(trs["T"], t, s["T"])[:3] if "T" in trs else list(s["T"])
                Q = eval_track(trs["R"], t, s["Q"])[:4] if "R" in trs else list(s["Q"])
                if "S" in trs:
                    v = eval_track(trs["S"], t, list(s["S"]) + list(s["SO"]))
                    S = v[:3]
                    SO = v[3:7] if len(v) >= 7 else list(s["SO"])
                else:
                    S, SO = list(s["S"]), list(s["SO"])
                got[n] = (T, Q, S, SO, s["mirror"])
            return got

        def same_pose(t0, t1):
            """Is every node in exactly the same pose at these two instants?
            Quaternions compare up to sign (q and -q are the same rotation);
            translations compare with a scale-relative tolerance."""
            a, b = pose_at(t0), pose_at(t1)
            for n in a:
                aT, aQ, aS, aSO, _ = a[n]
                bT, bQ, bS, bSO, _ = b[n]
                span = max(1.0, max(abs(x) for x in aT + bT))
                for x, y in zip(aT, bT):
                    if abs(x - y) > 1e-3 * span:
                        return False
                for qa, qb in ((aQ, bQ), (aSO, bSO)):
                    if sum(x*y for x, y in zip(qa, qb)) < 0:
                        qb = [-x for x in qb]
                    for x, y in zip(qa, qb):
                        if abs(x - y) > 2e-3:
                            return False
                for x, y in zip(aS, bS):
                    if abs(x - y) > 1e-3:
                        return False
            return True

        # A GAP IS ONLY A GAP IF NOTHING HAPPENS ACROSS IT.
        #
        # clip_split cuts wherever no KEY falls for 8000 ticks. That mistakes a SPARSE
        # track for a STILL one, and the Hand of Nod is the proof: its one animated node
        # carries a three-key translation track spanning the whole 16000-tick clip, so
        # both intervals between its keys look like gaps, and the extractor kept only the
        # last one -- 15840..16000, ONE frame of a hundred. Between two keys with
        # different values the evaluator is interpolating, and that interpolation IS the
        # animation; there is nothing sparse about the motion, only about the keys.
        #
        # So the candidate cuts stay, but each one is now confirmed against the POSE
        # rather than against the key times: a cut survives only if the pose at both ends
        # of the gap is identical AND still identical at its midpoint. The midpoint test
        # is what stops an excursion that happens to return to its start from reading as
        # stillness. Genuine event boundaries -- the Advanced Guard Tower's two events,
        # 115000 ticks apart with the tower parked in between -- are unaffected, because
        # across those the pose really does not change.
        #
        # The BEEFED02 clip, when there is one, is clips[0] by convention and is not a
        # clip_split product at all, so it is held out of the merge.
        if len(clips) - nbeefed > 1:
            head, tail = clips[:nbeefed], [list(c) for c in clips[nbeefed:]]
            merged = [tail[0]]
            for c in tail[1:]:
                if same_pose(float(merged[-1][1]), float(c[0])) and \
                   same_pose(float(merged[-1][1]),
                             (float(merged[-1][1]) + float(c[0])) * 0.5):
                    merged.append(c)
                else:
                    merged[-1][1] = c[1]
            clips = list(head) + [tuple(c) for c in merged]
        # NOW the degenerate segments go, once the merge has had its say.
        clips = [c for c in clips if c[1] > c[0]] or clips[:1]

        # ---- self-check: resampling AT a key time reproduces that key -----
        for n, d, p, trs, pr in rows:
            for chan, tr in trs.items():
                for ki in range(tr["count"]):
                    t = float(tr["time"][ki])
                    v = eval_track(tr, t, [0.0]*8)
                    if tr["kind"] == "linear":
                        exp = []
                        for part in tr["parts"]:
                            exp += tr[part][ki]
                        for a, b in zip(v, exp):
                            assert abs(a - b) < 1e-4, (slot, hex(n), chan, ki, a, b)
                        nchecks += 1
                    elif tr["kind"] == "quat2":
                        exp = tr["q"][ki]
                        # a quaternion and its negation are the same rotation
                        e2 = exp if sum(x*y for x, y in zip(v, exp)) >= 0 else [-x for x in exp]
                        for a, b in zip(v, e2):
                            assert abs(a - b) < 2e-3, (slot, hex(n), chan, ki, a, b)
                        nchecks += 1
                    elif tr["kind"] == "bezier":
                        exp = tr["val"][ki][:tr["ntan"]]
                        for a, b in zip(v[:tr["ntan"]], exp):
                            assert abs(a - b) < 1e-3 * max(1.0, abs(b)), \
                                (slot, hex(n), chan, ki, a, b)
                        nchecks += 1
                        if tr["nval"] > tr["ntan"]:
                            base = tr["val"][0][tr["ntan"]:]
                            for kk in range(tr["count"]):
                                if any(abs(x - y) > 1e-6 for x, y in
                                       zip(tr["val"][kk][tr["ntan"]:], base)):
                                    so_varies += 1

        # ---- cross-check: node+0x04's baked Mtx row 3 must equal the static
        # T reordered by P_AUTHOR_TO_MESH (the worked example in
        # docs/re-findings.md: 0x8012C880 row 3 = (-744.7306, 244.6417,
        # -161.7566) against pose T (-744.731, -161.757, 244.642)).
        for n, d, p_, trs, pr in rows:
            RMx = rest_matrix(n)
            if RMx is None or pr is None:
                continue
            rest_seen += 1
            want = O2._mvec_row(statics[n]["T"], P)
            if all(abs(RMx[3][k] - want[k]) < 0.05 for k in range(3)):
                rest_ok += 1

        # ---- resample ----------------------------------------------------
        frames = {}
        for (c0, c1) in clips:
            f0 = int(math.floor(c0 / TICKS_PER_FRAME))
            f1 = int(math.ceil(c1 / TICKS_PER_FRAME))
            for f in range(f0, f1 + 1):
                frames.setdefault(f, None)
        flist = sorted(frames)
        per_node = {}
        for f in flist:
            t = f * TICKS_PER_FRAME + T_BIAS
            poses = pose_at(t)
            # compose()'s `locals_` already exists for BEEFED02 rest poses; here the
            # ANIMATED ones override their own entry at this instant. Nodes with no
            # motion keep the rest entry, so a slot that mixes the two (EYE, AFLD,
            # SAM) composes correctly rather than snapping the still nodes to origin.
            blocals = dict(beefed)
            for bn, cur in bcurves.items():
                blocals[bn] = beefed_local_at(cur, t)
            world = compose(tree, poses, blocals)
            for n, (T, Q, S, SO, mir) in poses.items():
                RM, Rt = rest_world[n]
                AM, At = world[n]
                iM, it = mat_inv_affine(RM, Rt)
                CM = O2._mmul(iM, AM)
                Ct = [O2._mvec_row(it, AM)[k] + At[k] for k in range(3)]
                per_node.setdefault(n, []).append(dict(
                    f=f, t=t,
                    T=[round(x, 5) for x in T], Q=[round(x, 6) for x in Q],
                    S=[round(x, 6) for x in S], SO=[round(x, 6) for x in SO],
                    C=[[round(x, 6) for x in r] for r in CM] + [[round(x, 4) for x in Ct]]))

        # ---- loop test ---------------------------------------------------
        def loops(c0, c1):
            """A clip LOOPS if the pose at its last key time equals the pose at
            its first.  Quaternions compare up to sign (q and -q are the same
            rotation); translations compare with a scale-relative tolerance."""
            return same_pose(float(c0), float(c1))

        def clip_loops(c0, c1, idx):
            """Whether this clip loops.

            THE ENDPOINT TEST DOES NOT APPLY TO BEEFED02, and finding that out is the
            whole reason this function exists separately. For a FEEB/CAFEDEAD clip
            "loops" means the author brought the pose back to where it started, so
            comparing the ends is the right question. BEEFED02 loops because its
            EVALUATOR says so: 0x8007FC6C runs the pre/post-infinity cycle controller
            at 0x8007F264, read here instruction by instruction --

                8007F300  lwc1 f0, 0x14(fp) ; cvt.s.w    the cycle count n
                8007F30C  lwc1 f2, (a0)                  the period, chan+0x00
                8007F310  mul.s f0, f0, f2               n * period
                8007F318  add.s f0, f2, f0               base + n*period
                8007F31C  swc1 f0, 0x50(v0)              stored back

            -- the window slides by whole periods forever, with the sign of chan+0x4C
            picking the pre- or post-infinity mode at 0x8007F328. So it repeats whether
            or not the last key lands exactly on the first. Measured on PYLE, it does
            NOT: its three nodes end 0.0022 and 0.0049 away in quaternion terms, just
            outside the endpoint tolerance, and the endpoint test duly called the one
            provably-looping animation in the cartridge a one-shot.

            Corroboration from the data rather than the code alone: PYLE's three nodes
            have 5, 6 and 5 keys with completely different individual durations, and
            all three total EXACTLY 48000. Three independent curves landing on the same
            period is a shared cycle, not a coincidence."""
            if bperiod > 0 and idx == 0:
                return True
            return loops(c0, c1)

        rec = dict(
            slot=slot, name="/".join(SLOT_NAMES.get(slot, [])) or None,
            script_model=(slot < 10),
            script_model_rate=(SCRIPT_MODEL_RATE[slot] if slot < 10 else None),
            root="0x%08X" % root,
            frame_ticks=int(TICKS_PER_FRAME), t_bias=int(T_BIAS),
            t0=t0, t1=t1,
            clips=[dict(t0=c0, t1=c1, f0=int(c0//160), f1=int(math.ceil(c1/160.0)),
                        loop=clip_loops(c0, c1, ci))
                   for ci, (c0, c1) in enumerate(clips)],
            nodes=[])
        for n, d, p, trs, pr in rows:
            if n not in per_node:
                continue
            entry = dict(
                node="0x%08X" % n, depth=d,
                parent=("0x%08X" % p) if p else None,
                bake_part_index=bindex.get(n),          # join key into bake5.py
                gfx_rom=("0x%07X" % O2.ram2rom(m_u32(n))) if (m_u32(n) and O2._in_model_seg(m_u32(n))) else None,
                rest_matrix=rest_matrix(n),
                # The COMPOSED rest pose, mesh frame, objgraph2's row-vector
                # convention. This is the field bake5.bake_node_anim() asserts
                # against its own independently-walked mount transform, which is
                # what makes the delta C = rest^-1 . anim mean anything. It used
                # to be absent, so that assert was inert: it tested
                # `isinstance(rm, dict)` against a 4x4 list and quietly skipped.
                rest_world=dict(M=[[round(x, 6) for x in r] for r in rest_world[n][0]],
                                t=[round(x, 4) for x in rest_world[n][1]]),
                beefed=(n in beefed),
                rest=dict(T=statics[n]["T"], Q=statics[n]["Q"], S=statics[n]["S"],
                          SO=statics[n]["SO"], mirror=statics[n]["mirror"]),
                tracks={k: dict(feeb="0x%08X" % v["tag"], count=v["count"],
                                t0=v["time"][0], t1=v["time"][-1],
                                keys_rom="0x%07X" % v["keys_rom"])
                        for k, v in trs.items()},
                animated=bool(trs),
                frames=per_node[n])
            rec["nodes"].append(entry)
        summary.append(rec)
        with open(os.path.join(outdir, "slot_%03d.json" % slot), "w") as fh:
            json.dump(rec, fh, indent=1)

    index = dict(
        rom="data/rom/cnc_eu.z64",
        ticks_per_frame=int(TICKS_PER_FRAME), t_bias=int(T_BIAS),
        t_formula="t = frame * 160.0 + 1.0   (RAM 0x80004964 / 0x80004968)",
        classes_used={("0x%08X" % k): v for k, v in sorted(used_tags.items())},
        total_tracks=total_tracks,
        slots=[dict(slot=r["slot"], name=r["name"], script_model=r["script_model"],
                    nodes=len(r["nodes"]),
                    tracks=sum(len(n["tracks"]) for n in r["nodes"]),
                    t0=r["t0"], t1=r["t1"],
                    clips=r["clips"]) for r in summary])
    with open(os.path.join(outdir, "index.json"), "w") as fh:
        json.dump(index, fh, indent=1)

    # ------------------------------------------------------------ asserts
    # 102 tracks.  This used to be MORE than objgraph2.parts() could see, because
    # that walker capped recursion at depth 4 and slot 18's rig is 6 deep; with
    # objgraph2.DEPTH_CAP lifted to 24 the two walks now agree node for node,
    # which is what makes bake_part_index a usable join key for slot 18.
    assert total_tracks == 102, total_tracks
    assert set(used_tags) == {0xFEEB0002, 0xFEEB0005, 0xFEEB0007,
                              0xFEEB0009, 0xFEEB000A, 0xFEEB000B}, used_tags
    print("  FEEB0002 keys taking the multi-turn path (spin >= 2pi): %d" % spin_over_2pi)
    assert nchecks > 0
    print("CNC3D wave5 -- animation extractor")
    print("  animated model slots : %d" % len(summary))
    print("  tracks               : %d  %s" % (
        total_tracks, {("FEEB%04X" % (k & 0xFFFF)): v for k, v in sorted(used_tags.items())}))
    print("  key round-trips ok   : %d" % nchecks)
    print("  node+0x04 rest Mtx row3 == P*(static T) : %d/%d" % (rest_ok, rest_seen))
    assert rest_seen and rest_ok * 100 >= rest_seen * 95, (rest_ok, rest_seen)

    # ---- THE REST-POSE CROSS-CHECK ---------------------------------------
    # Every node this extractor emits with a bake_part_index must compose to the
    # SAME rest transform objgraph2 gives the baker, because the baker bakes that
    # transform into the vertices and the per-frame C is a delta against it. The
    # two are computed by different code from different reads, so agreement is
    # evidence and disagreement is a bug -- which is exactly how the BEEFED02
    # identity fall-through was caught.
    nx = nbad = 0
    for r in summary:
        graph = [q for q in O2.parts(r["slot"]) if q["gfx_rom"] is not None]
        for e in r["nodes"]:
            bi = e.get("bake_part_index")
            if bi is None:
                continue
            q, rw = graph[bi], e["rest_world"]
            dM = max(abs(rw["M"][i][j] - q["M"][i][j])
                     for i in range(3) for j in range(3))
            dt = max(abs(rw["t"][k] - q["t"][k]) for k in range(3))
            nx += 1
            if dM > 1e-3 or dt > 1e-2:
                nbad += 1
                print("    REST MISMATCH slot %d node %s dM=%.4f dt=%.3f"
                      % (r["slot"], e["node"], dM, dt))
    print("  rest pose == objgraph2 mount transform : %d/%d nodes" % (nx - nbad, nx))
    assert nbad == 0, nbad
    print("  BEEFED02 nodes given a real rest pose  : %d  (they used to compose as"
          " identity)" % n_beefed_fixed[0])
    print("  FEEB0007 SO channel varies in %d tracks (0 = the ROM's uninitialised"
          " scale-orientation stack slot is harmless)" % so_varies)
    print("  ticks/frame          : 160   t = frame*160 + 1")
    print("  written              : %s/index.json + slot_NNN.json" % outdir)
    for r in summary:
        cl = " ".join("[%d..%d %s]" % (c["t0"], c["t1"], "loop" if c["loop"] else "oneshot")
                      for c in r["clips"])
        print("   slot %3d  nodes %2d  tracks %2d  %s"
              % (r["slot"], len(r["nodes"]),
                 sum(len(n["tracks"]) for n in r["nodes"]), cl))

if __name__ == "__main__":
    main()
