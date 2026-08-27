#!/usr/bin/env python3
"""Shared helpers for the mount-transform investigation. Read-only on the ROM."""
import os, struct, sys
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS64, CS_MODE_BIG_ENDIAN

_ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
# The cartridge is not in the repo. CNC3D_ROM names it; otherwise data/rom/.
ROM_P = os.environ.get("CNC3D_ROM") or os.path.join(_ROOT, "data", "rom", "cnc_eu.z64")
ROM = open(ROM_P, "rb").read()

SEG_ROM, SEG_RAM = 0x1000, 0x80000400
RES_DELTA = SEG_RAM - SEG_ROM          # 0x7FFFF400
GMG_DELTA = 0x8005C590                 # GameModelsGeometry: rom = ram - delta
GMG_LO, GMG_HI = 0x8015A7A0, 0x801C0980
RES_LO, RES_HI = SEG_RAM, SEG_RAM + (0x90DB4 * 4)   # generous

sys.path.insert(0, os.path.join(SHARE, "tools", "romdump"))

def rom2ram(o): return SEG_RAM + (o - SEG_ROM)
def ram2rom(a): return a - SEG_RAM + SEG_ROM

def in_res(a):  return 0x80000400 <= a < 0x800F0000
def in_gmg(a):  return GMG_LO <= a < GMG_HI

def a2o(a):
    """RAM -> ROM for whichever region it lives in (resident or GMG)."""
    if in_gmg(a): return a - GMG_DELTA
    if in_res(a): return ram2rom(a)
    return None

def u32(o):  return struct.unpack_from(">I", ROM, o)[0]
def s32(o):  return struct.unpack_from(">i", ROM, o)[0]
def u16(o):  return struct.unpack_from(">H", ROM, o)[0]
def s16(o):  return struct.unpack_from(">h", ROM, o)[0]
def f32(o):  return struct.unpack_from(">f", ROM, o)[0]

def r32(a):
    o = a2o(a)
    return None if o is None or o + 4 > len(ROM) else u32(o)
def rf32(a):
    o = a2o(a)
    return None if o is None or o + 4 > len(ROM) else f32(o)

md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_BIG_ENDIAN)
md.detail = True

def dis(off, count, out=None):
    lines = []
    code = ROM[off:off + count * 4]
    for ins in md.disasm(code, rom2ram(off)):
        lines.append("  %07X/%08X  %-9s %s" % (ram2rom(ins.address), ins.address,
                                               ins.mnemonic, ins.op_str))
    s = "\n".join(lines)
    if out is None: print(s)
    return s

def func(off, maxins=0x800):
    """Disassemble until the instruction after `jr $ra`."""
    lines = []
    code = ROM[off:off + maxins * 4]
    last = False
    for ins in md.disasm(code, rom2ram(off)):
        lines.append("  %07X/%08X  %-9s %s" % (ram2rom(ins.address), ins.address,
                                               ins.mnemonic, ins.op_str))
        if last: break
        if ins.mnemonic == "jr" and "ra" in ins.op_str: last = True
    return "\n".join(lines)

CODE_LO, CODE_HI = 0x1000, 0x90000     # resident code region (rough)

def find_jal(target_ram, lo=CODE_LO, hi=CODE_HI):
    """Every `jal target` in the resident segment. Returns ROM offsets."""
    want = 0x0C000000 | ((target_ram & 0x0FFFFFFF) >> 2)
    hits = []
    for o in range(lo, min(hi, len(ROM)) - 4, 4):
        if u32(o) == want: hits.append(o)
    return hits

def find_jal_any(target_ram):
    """Every `jal target` anywhere in the ROM (overlays included)."""
    want = 0x0C000000 | ((target_ram & 0x0FFFFFFF) >> 2)
    hits = []
    for o in range(0, len(ROM) - 4, 4):
        if u32(o) == want: hits.append(o)
    return hits

def find_addr_builders(target, lo=CODE_LO, hi=CODE_HI):
    """lui/addiu or lui/ori pairs materialising `target`. Returns (lui_off, lo_off, kind)."""
    hi16 = (target >> 16) & 0xFFFF
    lo16 = target & 0xFFFF
    hi_addiu = (hi16 + 1) & 0xFFFF if lo16 >= 0x8000 else hi16
    hits = []
    end = min(hi, len(ROM))
    for o in range(lo, end - 4, 4):
        w = u32(o)
        if (w >> 26) != 0x0F: continue
        rt, imm = (w >> 16) & 0x1F, w & 0xFFFF
        if imm not in (hi16, hi_addiu): continue
        for d in range(4, 96, 4):
            p = o + d
            if p + 4 > end: break
            w2 = u32(p)
            op2, rs2, im2 = (w2 >> 26), (w2 >> 21) & 0x1F, w2 & 0xFFFF
            if rs2 != rt: continue
            if op2 == 0x0D and im2 == lo16 and imm == hi16:
                hits.append((o, p, "ori")); break
            if op2 == 0x09 and im2 == lo16 and imm == hi_addiu:
                hits.append((o, p, "addiu")); break
            # lw/sw/lwc1/swc1 with the offset as the low half
            if op2 in (0x23, 0x2B, 0x31, 0x39) and im2 == lo16 and imm == hi_addiu:
                hits.append((o, p, {0x23: "lw", 0x2B: "sw", 0x31: "lwc1", 0x39: "swc1"}[op2])); break
            # register clobbered by something else that writes rt -> give up
    return hits

def find_lui_reg(target_hi, lo=CODE_LO, hi=CODE_HI):
    out = []
    for o in range(lo, min(hi, len(ROM)) - 4, 4):
        w = u32(o)
        if (w >> 26) == 0x0F and (w & 0xFFFF) == target_hi:
            out.append((o, (w >> 16) & 0x1F))
    return out

def func_start(off, limit=0x1200):
    """Walk backwards to the previous `jr $ra` + delay slot -> function entry."""
    o = off
    while o > CODE_LO and off - o < limit:
        o -= 4
        w = u32(o)
        if w == 0x03E00008:      # jr $ra
            return o + 8
    return None
