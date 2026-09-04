#!/usr/bin/env python3
"""Track register/stack arguments across a MIPS registration chain in the cartridge.

Linear scan over a ROM range, maintaining a shadow of the integer registers for the
simple constant forms the compiler emits in these tables (lui/ori/addiu/li/move), plus
the outgoing-argument stack slots written with sw/sh/sb. At every jal the current
outgoing argument set is snapshotted.
"""
import struct, sys
import g170_rom as R

REGN = ["zero","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3","t4","t5","t6","t7",
        "s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","fp","ra"]

def scan(rom_lo, rom_hi, seg=R.CODE, verbose=False):
    reg = {i: None for i in range(32)}
    reg[0] = 0
    stack = {}
    calls = []
    o = rom_lo
    pending = None
    while o < rom_hi:
        w = R.u32(o)
        op = w >> 26
        rs = (w >> 21) & 31
        rt = (w >> 16) & 31
        rd = (w >> 11) & 31
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0x0F:                      # lui
            reg[rt] = imm << 16
        elif op == 0x09:                    # addiu
            reg[rt] = None if reg.get(rs) is None else (reg[rs] + simm) & 0xFFFFFFFF
        elif op == 0x0D:                    # ori
            reg[rt] = None if reg.get(rs) is None else (reg[rs] | imm)
        elif op == 0x24 or op == 0x0C:      # andi
            reg[rt] = None if reg.get(rs) is None else (reg[rs] & imm)
        elif op == 0x00 and (w & 0x3F) == 0x21:   # addu (move when rt==0)
            a, b = reg.get(rs), reg.get(rt)
            reg[rd] = None if (a is None or b is None) else (a + b) & 0xFFFFFFFF
        elif op == 0x00 and (w & 0x3F) == 0x25:   # or
            a, b = reg.get(rs), reg.get(rt)
            reg[rd] = None if (a is None or b is None) else (a | b)
        elif op == 0x2B and rs == 29:       # sw rt, imm(sp)
            stack[simm] = reg.get(rt)
        elif op == 0x29 and rs == 29:       # sh
            stack[simm] = None if reg.get(rt) is None else reg[rt] & 0xFFFF
        elif op == 0x28 and rs == 29:       # sb
            stack[simm] = None if reg.get(rt) is None else reg[rt] & 0xFF
        elif op == 0x23:                    # lw -> unknown
            reg[rt] = None
        elif op == 0x03:                    # jal
            tgt = ((R.rom2ram(o, seg) & 0xF0000000) | ((w & 0x03FFFFFF) << 2))
            pending = (o, tgt)
        elif op in (0x20,0x21,0x24,0x25,0x30,0x37,0x31,0x35):  # loads
            reg[rt] = None
        if pending and o == pending[0] + 4:
            # delay slot already executed above
            calls.append((pending[0], pending[1], dict(reg), dict(stack)))
            pending = None
        o += 4
    return calls

if __name__ == "__main__":
    lo = int(sys.argv[1], 16); hi = int(sys.argv[2], 16)
    for off, tgt, reg, st in scan(lo, hi):
        args = [reg.get(4), reg.get(5), reg.get(6), reg.get(7)]
        print("ROM 0x%06X jal 0x%08X  a0=%s a1=%s a2=%s a3=%s  stack=%s" % (
            off, tgt,
            *["0x%X" % a if a is not None else "?" for a in args],
            {("sp+0x%X" % k): ("0x%X" % v if v is not None else "?") for k, v in sorted(st.items())}))
