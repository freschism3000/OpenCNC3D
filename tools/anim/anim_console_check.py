#!/usr/bin/env python3
"""
anim_console_check.py -- run the cartridge's own BEEFED02 curve code and compare it with
what anim_extract.py baked.

WHY THIS EXISTS. The BEEFED02 evaluator in anim_extract.py is a transcription of RAM
0x8007FC6C, and a transcription can be wrong in ways that still look like an animation:
the version before it blended the key VALUES with slerp and ignored the two control
points, which turned the Advanced Communications Centre's 412-degree dish sweep into a
308-degree sweep the other way, and nothing in the extractor's own self-checks could
tell, because both agree at every key time. This check does not read the evaluator; it
RUNS it. The resident segment, GameModelsGeometry and ScriptModels are loaded into
tools/bakery/n64emu.py, the cartridge's BEEFED02 init (RAM 0x8008A258, which builds the
per-channel controllers and reports the curve's time range) and apply (RAM 0x8008A6C8,
which evaluates all four channels at a time and writes the node's 4x4) are called with
the data the ROM carries, and the matrix that comes back is compared with the one the
extractor's beefed_local_at() builds for the same instant. Whatever the console would
draw is what the bake has to draw.

WHAT IS COMPARED. For every model-table slot whose extracted JSON exists, every node
that carries a moving BEEFED02 curve, at every baked frame f (t = 160 f + 1, the
renderer's own clock), two things against the console's local matrix at t:

  bake:      the node's local transform REBUILT FROM THE JSON THE PACK IS BAKED FROM.
             The JSON carries each node's rest_world and its per-frame delta C
             (anim = rest . C, mesh frame), so anim_world(n) . anim_world(parent)^-1 is
             the local the pack will draw. For a BEEFED02 node that local is the raw
             curve matrix (the author/mesh conjugation cancels for this node class), so
             it compares with the console's 4x4 directly.
  evaluator: beefed_eval() on the same curves at the same t, the input to that bake.

Tolerance 2e-3 on the 3x3 and 0.05 on the translation. The console works in float32 and
the JSON is rounded to 6 and 4 decimals; the largest honest residual measured is 3e-5.

TWO THINGS THE CONSOLE DOES THAT LOOK LIKE ERRORS AND ARE NOT. (1) Its rotation
post-process collapses an exact 180-degree quaternion to identity (0x8007F988 emits
(cos th, sin th n) and 0x800865A4 returns identity when |sin th n|^2 < 1e-10); the SAM
launcher's R curve holds w = 0 from key 2 on, so its frames from 405 to the end of its
period come back as identity from both sides, and that is the agreement the check
requires. (2) Beyond the period the CONSTANT post-infinity mode hands back the cached
last value, post-processed, so a held 180-degree key is identity there too. Both are
reproduced in beefed_post(); the check would fail on a transcription that normalised
without collapsing.

    python3 tools/anim/anim_console_check.py            every extracted slot
    python3 tools/anim/anim_console_check.py 11 23      only these slots
    python3 tools/anim/anim_console_check.py --frames 0,24,48,72,96,100 11

Prints one CONSOLECHECK| line per node with the worst residuals and exits 0 only when
every node is within tolerance; prints the offending frames otherwise.
"""
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("CNC3D_ROOT") or os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "bakery"))
sys.path.insert(0, HERE)
from n64emu import Mem, CPU                     # noqa: E402
import objgraph2 as O2                          # noqa: E402
import anim_extract as AE                       # noqa: E402

ROM = O2.rom()
# The resident segment, exactly as the boot loader places it: ROM 0x1000 at RAM
# 0x80000400 (objgraph2 and anim_extract read it through the same delta).
RES_ROM, RES_END, RES_RAM = 0x0001000, 0x00C6EA0, 0x80000400
STACK = 0x80780000            # well above every loaded segment, inside the 8 MB image
SCRATCH = 0x80790000
ANIM_DIR = os.path.join(ROOT, "tools", "bakery", "anim")
TOL_M, TOL_T = 2e-3, 0.05

ENTRY_INIT = 0x8008A258       # BEEFED02 init: (data, &tmin, &tmax)
ENTRY_APPLY = 0x8008A6C8      # BEEFED02 apply: (data, t, Mtx4x4 out)
ASSERT_HOOK = 0x8005C610      # the cartridge's assert; reaching it is a failed run


def f2i(f): return struct.unpack(">I", struct.pack(">f", f))[0]
def i2f(i): return struct.unpack(">f", struct.pack(">I", i & 0xFFFFFFFF))[0]


def machine():
    mem = Mem()
    mem.blit(RES_RAM, ROM[RES_ROM:RES_END])
    mem.blit(O2.GMG_LO, ROM[O2.GMG_LO - O2.GMG_DELTA:O2.GMG_HI - O2.GMG_DELTA])
    mem.blit(O2.SM_LO, ROM[O2.SM_LO - O2.SM_DELTA:O2.SM_HI - O2.SM_DELTA])
    cpu = CPU(mem)

    def on_assert(c):
        raise RuntimeError("cartridge assert reached, line %d" % c.r[6])
    cpu.hooks = {ASSERT_HOOK: on_assert}
    return mem, cpu


class ConsoleNode:
    """One BEEFED02 node's curves, initialised by the cartridge's own code."""
    def __init__(self, node):
        self.node = node
        self.mem, self.cpu = machine()
        h = AE.m_u32(node + 8)
        assert h and AE.m_u32(h) == O2.TAG_BEEFED02, "%08X is not a BEEFED02 node" % node
        self.data = AE.m_u32(h + 4)
        self.mem.w32(SCRATCH, 0)
        self.mem.w32(SCRATCH + 4, 0)
        self.cpu.r[29] = STACK
        self.cpu.run(ENTRY_INIT, (self.data, SCRATCH, SCRATCH + 4))
        self.tmin = i2f(self.mem.r32(SCRATCH))
        self.tmax = i2f(self.mem.r32(SCRATCH + 4))

    def local(self, t):
        """The 4x4 the console builds for this node at time t: rows are the
        row-vector matrix objgraph2 uses, row 3 the translation."""
        out = SCRATCH + 0x100
        for i in range(16):
            self.mem.w32(out + 4 * i, 0)
        self.cpu.r[29] = STACK
        self.cpu.run(ENTRY_APPLY, (self.data, f2i(float(t)), out))
        return [[i2f(self.mem.r32(out + (r * 4 + c) * 4)) for c in range(4)]
                for r in range(4)]


def extractor_local(curves, t):
    """The extractor's local matrix at t in the node's own frame: the same four channel
    evaluations beefed_local_at() feeds compose(), before the author/mesh conjugation
    (which the console's matrix does not carry either)."""
    T = AE.beefed_eval(curves["T"], t) if "T" in curves else [0.0, 0.0, 0.0]
    Q = AE.beefed_eval(curves["R"], t) if "R" in curves else [1.0, 0.0, 0.0, 0.0]
    S = AE.beefed_eval(curves["S"], t) if "S" in curves else [1.0, 1.0, 1.0]
    SO = AE.beefed_eval(curves["SO"], t) if "SO" in curves else [1.0, 0.0, 0.0, 0.0]
    return O2.trs_mat(Q, S, SO), T


def mat_angle_deg(A, B):
    """Angle between two rotation-ish 3x3s, through R = A^-1 B on their orthonormal
    parts; a readout for the report, not the pass criterion."""
    def orth(M):
        rows = []
        for r in M:
            n = math.sqrt(sum(x * x for x in r)) or 1.0
            rows.append([x / n for x in r])
        return rows
    a, b = orth(A), orth(B)
    tr = sum(a[i][k] * b[i][k] for i in range(3) for k in range(3))
    return math.degrees(math.acos(max(-1.0, min(1.0, (tr - 1.0) / 2.0))))


def baked_local(entry, parent, fi):
    """The local transform the pack draws for `entry` at its frame index fi, rebuilt
    from the JSON: anim_world = rest_world . C, then anim_world(n) . anim_world(p)^-1.
    Row-vector convention throughout (v' = v @ M + t)."""
    def anim_world(e):
        RM, Rt = e["rest_world"]["M"], e["rest_world"]["t"]
        C = e["frames"][fi]["C"]
        CM, Ct = C[:3], C[3]
        AM = O2._mmul(RM, CM)
        At = [O2._mvec_row(Rt, CM)[k] + Ct[k] for k in range(3)]
        return AM, At
    AM, At = anim_world(entry)
    if parent is None:
        return AM, At
    PM, Pt = anim_world(parent)
    iM, it = AE.mat_inv_affine(PM, Pt)
    LM = O2._mmul(AM, iM)
    Lt = [O2._mvec_row(At, iM)[k] + it[k] for k in range(3)]
    return LM, Lt


def check_slot(slot, frames=None, verbose=True):
    """-> (n_nodes, n_bad, lines). frames=None takes the slot JSON's own frame list;
    a list restricts the comparison to those of its frames."""
    root = O2.node_ptr(slot)
    path = os.path.join(ANIM_DIR, "slot_%03d.json" % slot)
    if not root or not os.path.exists(path):
        return 0, 0, []
    j = json.load(open(path))
    byname = {e["node"]: e for e in j["nodes"]}
    lines, nn, nbad = [], 0, 0
    for node, _depth in AE.walk(root):
        curves = AE.beefed_curves(node)
        if not curves or not AE.beefed_is_animated(curves):
            continue
        nn += 1
        entry = byname.get("0x%08X" % node)
        assert entry is not None, "slot %d: node %08X moves but is not in %s" % (
            slot, node, path)
        parent = byname.get(entry["parent"]) if entry.get("parent") else None
        con = ConsoleNode(node)
        flist = [(fi, fr["f"]) for fi, fr in enumerate(entry["frames"])
                 if frames is None or fr["f"] in frames]
        worst = dict(bake=(0.0, None, 0.0), eval=(0.0, None, 0.0))
        worst_t = 0.0
        bad = []
        for fi, f in flist:
            t = f * AE.TICKS_PER_FRAME + AE.T_BIAS
            M = con.local(t)
            for leg, (Mp, Tp) in (("bake", baked_local(entry, parent, fi)),
                                  ("eval", extractor_local(curves, t))):
                dM = max(abs(M[r][c] - Mp[r][c]) for r in range(3) for c in range(3))
                dT = max(abs(M[3][c] - Tp[c]) for c in range(3))
                if dM > worst[leg][0]:
                    worst[leg] = (dM, f, mat_angle_deg(Mp, M[:3]))
                worst_t = max(worst_t, dT)
                if dM > TOL_M or dT > TOL_T:
                    bad.append((leg, f, dM, dT))
        ok = not bad
        nbad += 0 if ok else 1
        lines.append("CONSOLECHECK|slot=%d|name=%s|node=0x%08X|frames=%d|f0=%d|f1=%d"
                     "|tmax=%.0f|bake_dM=%.2e|bake_deg=%.4f|bake_at=%s"
                     "|eval_dM=%.2e|eval_deg=%.4f|eval_at=%s|dT=%.4f|%s"
                     % (slot, "/".join(AE.SLOT_NAMES.get(slot, [])) or "-", node,
                        len(flist), flist[0][1] if flist else 0,
                        flist[-1][1] if flist else 0, con.tmax,
                        worst["bake"][0], worst["bake"][2], worst["bake"][1],
                        worst["eval"][0], worst["eval"][2], worst["eval"][1],
                        worst_t, "ok" if ok else "BAD"))
        for leg, f, dM, dT in bad[:8]:
            lines.append("CONSOLECHECK|  bad %s frame %d: dM=%.4f dT=%.3f"
                         % (leg, f, dM, dT))
    if verbose:
        for ln in lines:
            print(ln)
    return nn, nbad, lines


def main(argv):
    frames = None
    slots = []
    i = 0
    while i < len(argv):
        if argv[i] == "--frames":
            i += 1
            frames = [int(x) for x in argv[i].split(",")]
        else:
            slots.append(int(argv[i]))
        i += 1
    if not slots:
        slots = [int(fn[5:8]) for fn in sorted(os.listdir(ANIM_DIR))
                 if fn.startswith("slot_") and fn.endswith(".json")]
    tn = tb = 0
    for s in slots:
        nn, nbad, _ = check_slot(s, frames)
        tn += nn
        tb += nbad
    print("CONSOLECHECK|nodes=%d|bad=%d|tol_M=%g|tol_T=%g|%s"
          % (tn, tb, TOL_M, TOL_T, "PASS" if (tn and not tb) else "FAIL"))
    return 0 if (tn and not tb) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
