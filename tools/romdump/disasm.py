#!/usr/bin/env python3
"""
CNC3D — MIPS (N64) disassembly helper for locating format loaders/decompressors.

The resident boot segment is copied ROM[0x1000..] -> RAM[0x80000400..] by IPL3, so
    RAM  = 0x80000400 + (ROM - 0x1000)
    ROM  = RAM - 0x80000400 + 0x1000
(valid for the resident segment only; overlays load elsewhere.)

Capabilities:
    strings <substr>          find rodata string ROM offsets + computed RAM addrs
    xref <hexRamAddr>         find lui/ori|addiu pairs that materialize an address
    imm <hexImm>              find instructions with a given 16-bit immediate
    dis <hexRomOff> <count>   linear disassemble N instructions from a ROM offset
    func <hexRomOff>          disassemble until a likely function end (jr $ra)
"""
import sys, struct
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS64, CS_MODE_BIG_ENDIAN

ROM = open(sys.argv[1], "rb").read() if len(sys.argv) > 1 else b""
SEG_ROM = 0x1000
SEG_RAM = 0x80000400
def rom2ram(o): return SEG_RAM + (o - SEG_ROM)
def ram2rom(a): return a - SEG_RAM + SEG_ROM

md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_BIG_ENDIAN)

def find_strings(sub):
    sub = sub.encode()
    i = 0
    res = []
    while True:
        j = ROM.find(sub, i)
        if j < 0: break
        # back up to string start (previous null)
        s = j
        while s > 0 and 32 <= ROM[s-1] < 127: s -= 1
        end = ROM.find(b"\x00", j)
        res.append((s, ROM[s:end].decode("latin1", "replace")))
        i = j + 1
    return res

def scan_addr_builders(target):
    """Find lui rX,hi ; (addiu|ori) rX,rX,lo that build `target` (32-bit)."""
    hi = (target >> 16) & 0xFFFF
    lo = target & 0xFFFF
    # addiu sign-extends lo: if lo>=0x8000, hi is +1
    hi_addiu = (hi + 1) & 0xFFFF if lo >= 0x8000 else hi
    hits = []
    # first pass: record lui targets per register
    n = len(ROM)
    luis = {}  # rom_off -> (reg, imm)
    off = SEG_ROM
    while off + 4 <= min(n, 0x140000):
        w = struct.unpack_from(">I", ROM, off)[0]
        op = w >> 26
        if op == 0x0F:  # lui
            rt = (w >> 16) & 0x1F
            imm = w & 0xFFFF
            luis[off] = (rt, imm)
        off += 4
    # second pass: match lui(hi) followed soon by addiu/ori(lo) same reg
    lui_list = sorted(luis.items())
    for o,(rt,imm) in lui_list:
        if imm not in (hi, hi_addiu): continue
        for d in range(4, 64, 4):
            p = o + d
            if p+4 > n: break
            w = struct.unpack_from(">I", ROM, p)[0]
            op = w >> 26
            rs = (w >> 21) & 0x1F; rtt = (w >> 16) & 0x1F; im = w & 0xFFFF
            if op in (0x09, 0x0D) and rs == rt:  # addiu(0x09) / ori(0x0D)
                want_lo = lo if (op==0x0D or imm==hi_addiu) else lo  # ori: raw lo; addiu: signed
                if op == 0x0D and im == lo:
                    hits.append((o, p, "ori")); break
                if op == 0x09 and (im == (lo if lo<0x8000 else lo)) and imm==hi_addiu:
                    hits.append((o, p, "addiu")); break
    return hits

def scan_imm(imm16):
    n = len(ROM); off = SEG_ROM; hits=[]
    while off+4 <= min(n,0x140000):
        w = struct.unpack_from(">I", ROM, off)[0]
        op = w>>26
        if op in (0x08,0x09,0x0C,0x0D,0x0A,0x0B) and (w & 0xFFFF)==imm16:
            hits.append(off)
        off += 4
    return hits

def dis(off, count):
    code = ROM[off:off+count*4]
    for ins in md.disasm(code, rom2ram(off)):
        print(f"  {ram2rom(ins.address):#08x}/{ins.address:#010x}  {ins.mnemonic:8s} {ins.op_str}")

def func(off):
    code = ROM[off:off+0x2000]
    last_jr=False
    for ins in md.disasm(code, rom2ram(off)):
        print(f"  {ram2rom(ins.address):#08x}/{ins.address:#010x}  {ins.mnemonic:8s} {ins.op_str}")
        if last_jr: break
        if ins.mnemonic=="jr" and "ra" in ins.op_str: last_jr=True

if __name__ == "__main__":
    cmd = sys.argv[2]
    if cmd=="strings":
        for o,s in find_strings(sys.argv[3]):
            print(f"  ROM 0x{o:07X}  RAM 0x{rom2ram(o):08X}  {s!r}")
    elif cmd=="xref":
        t=int(sys.argv[3],16)
        for o,p,k in scan_addr_builders(t):
            print(f"  builder @ROM 0x{o:07X} ({k}, lo@0x{p:07X}) -> 0x{t:08X}")
    elif cmd=="imm":
        for o in scan_imm(int(sys.argv[3],16)):
            print(f"  imm @ROM 0x{o:07X} RAM 0x{rom2ram(o):08X}")
    elif cmd=="dis":
        dis(int(sys.argv[3],16), int(sys.argv[4]))
    elif cmd=="func":
        func(int(sys.argv[3],16))
