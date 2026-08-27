#!/usr/bin/env python3
"""
CNC3D — recover per-overlay ROM->RAM deltas by joint constraint.

WHY THIS EXISTS
---------------
Display lists address vertices by RAM address, and the overlays holding them are not
in the resident segment, so each needs a delta D = RAM - ROM. Solving one display list
at a time by pattern-matching its vertex block FAILS: many ROM regions parse as
plausible Vtx data, the search locks onto the first one, and the resulting mesh is
triangles indexing scrambled vertices -- visually a cloud of shards.

The fix: every display list inside the SAME overlay shares ONE delta. Ten display
lists with fifty G_VTX commands between them pin D far more tightly than one can.

METHOD
    1. cluster display lists by ROM proximity (an overlay is contiguous)
    2. in each cluster take the single most constraining G_VTX (largest n)
    3. enumerate every ROM position where a valid Vtx[n] array could sit
    4. each such position P proposes D = addr - P
    5. score D by how many of the cluster's OTHER G_VTX commands also land on valid
       Vtx arrays; accept only if nearly all of them do
"""
import struct
import numpy as np

def vtx_strict(d, p, n):
    """A real Vtx array: flag always 0, sane coords, genuine spread."""
    if p < 0 or p + n*16 > len(d): return False
    seen = set(); span = 0
    for i in range(n):
        x, y, z, fl, u, v = struct.unpack_from(">hhhHhh", d, p + i*16)
        if fl != 0: return False
        if max(abs(x), abs(y), abs(z)) > 12000: return False
        if abs(u) > 20000 or abs(v) > 20000: return False
        span = max(span, abs(x), abs(y), abs(z)); seen.add((x, y, z))
    return span >= 8 and len(seen) >= max(3, int(n * 0.6))

def flag_zero_lanes(d):
    """Prefilter: u16 at +6 of every 16-byte lane must be 0 for a Vtx run."""
    N = len(d) // 16
    a = np.frombuffer(d[:N*16], dtype=np.uint8).reshape(N, 16)
    z = ((a[:, 6].astype(np.uint16) << 8 | a[:, 7]) == 0).astype(np.int32)
    return N, np.concatenate([[0], np.cumsum(z)])

def cluster(starts, gap=0x30000):
    out, cur = [], [starts[0]]
    for s in starts[1:]:
        if s - cur[-1] > gap: out.append(cur); cur = [s]
        else: cur.append(s)
    out.append(cur)
    return out

def solve_cluster(d, pairs, N, cs, min_frac=0.85, align=8):
    """pairs = [(n, ram_addr)] for one overlay. Returns (D, score, total)."""
    if not pairs: return None, 0, 0
    n0, a0 = max(pairs, key=lambda p: p[0])
    best = (None, 0)
    for k in range(N - n0):
        if cs[k + n0] - cs[k] != n0:      # cheap flag-zero run test
            continue
        P = k * 16
        if P % align: continue
        if not vtx_strict(d, P, n0): continue
        D = a0 - P
        hit = sum(1 for n, a in pairs if vtx_strict(d, a - D, n))
        if hit > best[1]:
            best = (D, hit)
            if hit == len(pairs): break
    D, hit = best
    if D is None or hit < len(pairs) * min_frac:
        return None, hit, len(pairs)
    return D, hit, len(pairs)
