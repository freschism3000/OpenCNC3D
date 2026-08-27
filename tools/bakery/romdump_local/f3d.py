#!/usr/bin/env python3
"""
CNC3D — F3DEX2 display-list scanner and overlay-mapping solver.

The N64 build keeps its 3D geometry as F3DEX2 display lists in the uncompressed
overlay region (ROM ~0x100000-0x500000). Display lists reference vertices and other
display lists by *RAM* address, so to lift geometry we must recover the ROM->RAM
mapping (delta D = RAM - ROM) for each overlay bundle.

Two solvers:
  * branch-Hough: G_DL (0xDE) commands branch to other display lists. Every
    (branch_target, known_dl_rom_offset) pair votes for D = target - rom. The true D
    collects many votes; noise does not.
  * vtx-check: given D, a G_VTX address maps to ROM (addr - D) and must parse as a
    plausible 16-byte Vtx array.

Usage:
    python3 f3d.py scan  <rom> <cache.json>     # find DLs, dump their commands
    python3 f3d.py solve <rom> <cache.json>     # recover overlay deltas
"""
import sys, json, struct

# NOTE: 0x00 (G_NOOP) is deliberately EXCLUDED. Including it makes every zero-filled
# region of the ROM parse as an enormous display list (one such false positive claimed
# 20,250 vertices). A real DL must also carry actual geometry -- see scan().
F3DEX2_OPS = {0x01,0x05,0x06,0xDF,0xDE,0xDD,0xDC,0xDB,0xDA,0xD9,0xD8,0xD7,0xD6,0xD5,
              0xE2,0xE3,0xF5,0xFD,0xF3,0xF2,0xE6,0xE7,0xE8,0xF8,0xFA,0xFB,0xFC,0xF0,
              0xF4,0xF6,0xF7,0xF9}
G_VTX, G_DL, G_ENDDL, G_SETTIMG = 0x01, 0xDE, 0xDF, 0xFD
DATA_END = 0x1646216

def scan(rom, min_cmds=10):
    """Locate display lists: runs of valid 8-byte commands terminated by G_ENDDL."""
    d = rom; out = []; o = 0; n = len(d)
    while o + 8 <= DATA_END:
        if d[o] in F3DEX2_OPS:
            s = o; cmds = []
            while o + 8 <= DATA_END and d[o] in F3DEX2_OPS:
                w0, w1 = struct.unpack_from(">II", d, o)
                cmds.append((o, w0, w1)); o += 8
                if d[o-8] == G_ENDDL: break
            nv = sum(1 for _, w0, _ in cmds if (w0 >> 24) == G_VTX)
            nt = sum(1 for _, w0, _ in cmds if (w0 >> 24) in (0x05, 0x06))
            if len(cmds) >= min_cmds and d[o-8] == G_ENDDL and nv >= 1 and nt >= 3:
                out.append((s, cmds))
            else:
                o = s + 8
        else:
            o += 8
    return out

def summarize(dls):
    starts, vtx, branch, timg = [], [], [], []
    for s, cmds in dls:
        starts.append(s)
        for off, w0, w1 in cmds:
            op = w0 >> 24
            if op == G_VTX:      vtx.append((off, (w0 >> 12) & 0xFF, w1))
            elif op == G_DL:     branch.append((off, w1))
            elif op == G_SETTIMG: timg.append((off, w1))
    return starts, vtx, branch, timg

def solve_deltas(starts, branch, top=12):
    """Hough: each (branch target, dl start) pair votes for D = target - start."""
    sset = set(starts)
    votes = {}
    for _, tgt in branch:
        if not (0x80000000 <= tgt < 0x80800000): continue
        for s in starts:
            D = tgt - s
            if D <= 0: continue
            votes[D] = votes.get(D, 0) + 1
    ranked = sorted(votes.items(), key=lambda kv: -kv[1])
    return [(D, v) for D, v in ranked[:top] if v > 1]

def vtx_plausible(d, p, n):
    if p < 0 or p + n * 16 > len(d): return False
    zf = 0; span = 0; xs = set()
    for i in range(n):
        x, y, z, fl, u, v = struct.unpack_from(">hhhHhh", d, p + i*16)
        if max(abs(x), abs(y), abs(z)) > 20000: return False
        if fl == 0: zf += 1
        span = max(span, abs(x), abs(y), abs(z)); xs.add(x)
    return zf >= n * 0.75 and span >= 8 and len(xs) >= max(3, n // 8)

if __name__ == "__main__":
    cmd, rompath = sys.argv[1], sys.argv[2]
    rom = open(rompath, "rb").read()
    if cmd == "scan":
        dls = scan(rom)
        starts, vtx, branch, timg = summarize(dls)
        json.dump(dict(starts=starts, vtx=vtx, branch=branch, timg=timg),
                  open(sys.argv[3], "w"))
        print(f"DLs={len(starts)} G_VTX={len(vtx)} G_DL={len(branch)} G_SETTIMG={len(timg)}")
    elif cmd == "solve":
        c = json.load(open(sys.argv[3]))
        cands = solve_deltas(c["starts"], c["branch"])
        print("candidate overlay deltas (D = RAM - ROM), by branch votes:")
        for D, v in cands:
            good = sum(1 for _, n, a in c["vtx"] if vtx_plausible(rom, a - D, n))
            print(f"   D=0x{D:08X}  branch_votes={v:4d}  plausible_vtx={good}/{len(c['vtx'])}")
