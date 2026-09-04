"""
n64emu.py -- a minimal big-endian MIPS III interpreter.

It exists so that a cartridge routine whose format is undocumented can be RUN rather than
reimplemented from a guess. Load the segments the routine needs into a flat 8 MB RDRAM
image, hook the handful of resident calls that would otherwise need a booted OS (the heap,
mostly), call the routine, read the result back out of memory. What comes back is what the
console produces, byte for byte, with no reverse-engineered approximation in between.

Scope, honestly stated: integer MIPS III plus the single- and double-precision FPU ops the
game code actually uses, KSEG0 memory only (0x80000000 + 8 MB), no MMU, no COP0, no
exceptions, no NaN-ordered compares. Delay slots ARE modelled, including branch-likely
nullification, because getting them wrong silently produces plausible garbage.
"""
import struct

M32 = 0xFFFFFFFF
M64 = 0xFFFFFFFFFFFFFFFF

def s32(v): v &= M32; return v - (1 << 32) if v & 0x80000000 else v
def s64(v): v &= M64; return v - (1 << 64) if v & 0x8000000000000000 else v

class Mem:
    BASE = 0x80000000
    SIZE = 0x00800000
    def __init__(self):
        self.b = bytearray(self.SIZE)
    def _o(self, a):
        a &= 0xFFFFFFFF
        o = a - self.BASE
        if o < 0 or o >= self.SIZE:
            raise MemoryError("bad address %08X" % a)
        return o
    def r8(self, a):  return self.b[self._o(a)]
    def r16(self, a): o=self._o(a); return (self.b[o]<<8)|self.b[o+1]
    def r32(self, a): o=self._o(a); return struct.unpack_from(">I", self.b, o)[0]
    def r64(self, a): o=self._o(a); return struct.unpack_from(">Q", self.b, o)[0]
    def w8(self, a, v):  self.b[self._o(a)] = v & 0xFF
    def w16(self, a, v): struct.pack_into(">H", self.b, self._o(a), v & 0xFFFF)
    def w32(self, a, v): struct.pack_into(">I", self.b, self._o(a), v & M32)
    def w64(self, a, v): struct.pack_into(">Q", self.b, self._o(a), v & M64)
    def blit(self, a, data): o=self._o(a); self.b[o:o+len(data)] = data
    def read(self, a, n): o=self._o(a); return bytes(self.b[o:o+n])

REGN = ["zero","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3","t4","t5","t6","t7",
        "s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","fp","ra"]

class CPU:
    def __init__(self, mem):
        self.m = mem
        self.r = [0]*32
        self.f = [0]*32          # raw 64-bit FPR words
        self.fcr31 = 0
        self.hi = self.lo = 0
        self.pc = 0
        self.hooks = {}          # addr -> python fn(cpu) -> return value in v0
        self.trace = False
        self.icount = 0

    # ---- FPU helpers (32 fp regs, MIPS III: even-odd pairs for doubles) ----
    def fs(self, i):   return struct.unpack(">f", struct.pack(">I", self.f[i] & M32))[0]
    def setfs(self, i, v):
        self.f[i] = (self.f[i] & ~M32) | struct.unpack(">I", struct.pack(">f", v))[0]
    def fd(self, i):   return struct.unpack(">d", struct.pack(">Q", self.f[i] & M64))[0]
    def setfd(self, i, v): self.f[i] = struct.unpack(">Q", struct.pack(">d", v))[0]
    def fw(self, i):   return s32(self.f[i] & M32)
    def setfw(self, i, v): self.f[i] = (self.f[i] & ~M32) | (v & M32)

    def run(self, entry, args=(), maxi=200_000_000):
        RET = 0x80700000
        for i, a in enumerate(args):
            self.r[4+i] = a & M32
        self.r[31] = RET
        self.pc = entry
        while True:
            if self.pc == RET:
                return self.r[2]
            self.icount += 1
            if self.icount > maxi:
                raise RuntimeError("instruction budget exhausted at %08X" % self.pc)
            npc = self.step()
            if npc is not None:
                self.step_delay()          # delay slot runs before the transfer
                h = self.hooks.get(npc)
                if h is not None:
                    v = h(self)
                    if v is not None:
                        self.r[2] = v & M32
                    self.pc = self.r[31] & M32
                else:
                    self.pc = npc
                if self.pc == RET:
                    return self.r[2]

    def step_delay(self):
        r = self.step()
        if r is not None:
            raise RuntimeError("branch in delay slot at %08X" % self.pc)

    def step(self):
        pc = self.pc
        w = self.m.r32(pc)
        self.pc = (pc + 4) & M32
        return self.exec(w, pc)

    def _call(self, target, retpc):
        self.r[31] = retpc
        return target

    def exec(self, w, pc):
        r = self.r
        op = w >> 26
        rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31
        sa = (w >> 6) & 31; fn = w & 63
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        tgt = (pc + 4) & 0xF0000000 | ((w & 0x3FFFFFF) << 2)
        br = (pc + 4 + (simm << 2)) & M32

        if op == 0:                       # SPECIAL
            if fn == 0:   r[rd] = (r[rt] << sa) & M32 if rd else 0
            elif fn == 2: r[rd] = (r[rt] & M32) >> sa if rd else 0
            elif fn == 3: r[rd] = (s32(r[rt]) >> sa) & M32 if rd else 0
            elif fn == 4: r[rd] = (r[rt] << (r[rs] & 31)) & M32 if rd else 0
            elif fn == 6: r[rd] = (r[rt] & M32) >> (r[rs] & 31) if rd else 0
            elif fn == 7: r[rd] = (s32(r[rt]) >> (r[rs] & 31)) & M32 if rd else 0
            elif fn == 8: return r[rs] & M32                      # jr
            elif fn == 9:                                          # jalr
                t = r[rs] & M32
                res = self._call(t, (pc + 8) & M32)
                if rd: r[rd] = (pc + 8) & M32
                return res
            elif fn == 12: raise RuntimeError("syscall at %08X" % pc)
            elif fn == 13: raise RuntimeError("break at %08X" % pc)
            elif fn == 16: r[rd] = self.hi & M32 if rd else 0
            elif fn == 17: self.hi = r[rs] & M32
            elif fn == 18: r[rd] = self.lo & M32 if rd else 0
            elif fn == 19: self.lo = r[rs] & M32
            elif fn == 24:                                         # mult
                p = s32(r[rs]) * s32(r[rt]); self.lo = p & M32; self.hi = (p >> 32) & M32
            elif fn == 25:                                         # multu
                p = (r[rs] & M32) * (r[rt] & M32); self.lo = p & M32; self.hi = (p >> 32) & M32
            elif fn == 26:                                         # div
                a, b = s32(r[rs]), s32(r[rt])
                if b: q = abs(a)//abs(b); q = -q if (a<0)!=(b<0) else q; self.lo=q & M32; self.hi=(a - q*b) & M32
            elif fn == 27:                                         # divu
                a, b = r[rs] & M32, r[rt] & M32
                if b: self.lo = (a//b) & M32; self.hi = (a % b) & M32
            elif fn in (32, 33): r[rd] = (r[rs] + r[rt]) & M32 if rd else 0   # add/addu
            elif fn in (34, 35): r[rd] = (r[rs] - r[rt]) & M32 if rd else 0   # sub/subu
            elif fn == 36: r[rd] = (r[rs] & r[rt]) & M32 if rd else 0
            elif fn == 37: r[rd] = (r[rs] | r[rt]) & M32 if rd else 0
            elif fn == 38: r[rd] = (r[rs] ^ r[rt]) & M32 if rd else 0
            elif fn == 39: r[rd] = (~(r[rs] | r[rt])) & M32 if rd else 0
            elif fn == 42: r[rd] = 1 if s32(r[rs]) < s32(r[rt]) else 0
            elif fn == 43: r[rd] = 1 if (r[rs] & M32) < (r[rt] & M32) else 0
            elif fn in (44,45): r[rd] = (r[rs] + r[rt]) & M32 if rd else 0    # dadd/daddu (32-bit world)
            elif fn in (46,47): r[rd] = (r[rs] - r[rt]) & M32 if rd else 0
            elif fn == 56: r[rd] = (r[rt] << sa) & M32
            elif fn == 63: r[rd] = (r[rt] << sa) & M32
            else: raise RuntimeError("SPECIAL fn %d at %08X (%08X)" % (fn, pc, w))
            return None
        if op == 1:                       # REGIMM
            v = s32(r[rs])
            if   rt == 0:  return br if v < 0 else None            # bltz
            elif rt == 1:  return br if v >= 0 else None           # bgez
            elif rt == 2:                                          # bltzl
                if v < 0: return br
                self.pc = (self.pc + 4) & M32; return None
            elif rt == 3:
                if v >= 0: return br
                self.pc = (self.pc + 4) & M32; return None
            elif rt == 16: r[31] = (pc+8) & M32; return br if v < 0 else None
            elif rt == 17: r[31] = (pc+8) & M32; return br if v >= 0 else None
            else: raise RuntimeError("REGIMM rt %d at %08X" % (rt, pc))
        if op == 2: return tgt                                     # j
        if op == 3: return self._call(tgt, (pc+8) & M32)           # jal
        if op == 4: return br if (r[rs] & M32) == (r[rt] & M32) else None
        if op == 5: return br if (r[rs] & M32) != (r[rt] & M32) else None
        if op == 6: return br if s32(r[rs]) <= 0 else None
        if op == 7: return br if s32(r[rs]) > 0 else None
        if op in (8, 9):  r[rt] = (r[rs] + simm) & M32 if rt else 0
        elif op == 10: r[rt] = 1 if s32(r[rs]) < simm else 0
        elif op == 11: r[rt] = 1 if (r[rs] & M32) < (simm & M32) else 0
        elif op == 12: r[rt] = r[rs] & imm
        elif op == 13: r[rt] = (r[rs] | imm) & M32
        elif op == 14: r[rt] = (r[rs] ^ imm) & M32
        elif op == 15: r[rt] = (imm << 16) & M32
        elif op == 16:                                             # COP0
            if rs == 0: r[rt] = 0
            elif rs == 4: pass
            else: pass
        elif op == 17: return self.cop1(w, pc)
        elif op == 20:                                             # beql
            if (r[rs] & M32) == (r[rt] & M32): return br
            self.pc = (self.pc + 4) & M32
        elif op == 21:
            if (r[rs] & M32) != (r[rt] & M32): return br
            self.pc = (self.pc + 4) & M32
        elif op == 22:
            if s32(r[rs]) <= 0: return br
            self.pc = (self.pc + 4) & M32
        elif op == 23:
            if s32(r[rs]) > 0: return br
            self.pc = (self.pc + 4) & M32
        elif op == 32: r[rt] = (self.m.r8((r[rs]+simm) & M32) ^ 0x80) - 0x80 & M32
        elif op == 33:
            v = self.m.r16((r[rs]+simm) & M32); r[rt] = (v - 0x10000 if v & 0x8000 else v) & M32
        elif op == 35: r[rt] = self.m.r32((r[rs]+simm) & M32)
        elif op == 36: r[rt] = self.m.r8((r[rs]+simm) & M32)
        elif op == 37: r[rt] = self.m.r16((r[rs]+simm) & M32)
        elif op == 39: r[rt] = self.m.r32((r[rs]+simm) & M32)      # lwu
        elif op == 40: self.m.w8((r[rs]+simm) & M32, r[rt])
        elif op == 41: self.m.w16((r[rs]+simm) & M32, r[rt])
        elif op == 43: self.m.w32((r[rs]+simm) & M32, r[rt])
        elif op == 34:                                             # lwl
            a = (r[rs]+simm) & M32; al = a & ~3; sh = (a & 3)*8
            word = self.m.r32(al); mask = (M32 >> (32-sh)) if sh else 0
            r[rt] = ((word << sh) & M32) | (r[rt] & mask)
        elif op == 38:                                             # lwr
            a = (r[rs]+simm) & M32; al = a & ~3; sh = (3-(a & 3))*8
            word = self.m.r32(al)
            mask = ((M32 << (32-sh)) & M32) if sh else 0
            r[rt] = (word >> sh) | (r[rt] & mask)
        elif op == 42:                                             # swl
            a = (r[rs]+simm) & M32; al = a & ~3; sh = (a & 3)*8
            word = self.m.r32(al)
            mask = ((M32 << (32-sh)) & M32) if sh else 0
            self.m.w32(al, (word & mask) | ((r[rt] & M32) >> sh))
        elif op == 46:                                             # swr
            a = (r[rs]+simm) & M32; al = a & ~3; sh = (3-(a & 3))*8
            word = self.m.r32(al)
            mask = (M32 >> (32-sh)) if sh else 0
            self.m.w32(al, (word & mask) | ((r[rt] << sh) & M32))
        elif op == 49: self.f[rt] = (self.f[rt] & ~M32) | self.m.r32((r[rs]+simm) & M32)   # lwc1
        elif op == 53: self.f[rt] = self.m.r64((r[rs]+simm) & M32)                          # ldc1
        elif op == 57: self.m.w32((r[rs]+simm) & M32, self.f[rt] & M32)                     # swc1
        elif op == 61: self.m.w64((r[rs]+simm) & M32, self.f[rt])                           # sdc1
        elif op == 47: pass                                        # cache
        elif op == 55: r[rt] = self.m.r64((r[rs]+simm) & M32) & M32   # ld (32-bit truncate)
        elif op == 63: self.m.w64((r[rs]+simm) & M32, r[rt])           # sd
        else: raise RuntimeError("op %d at %08X (%08X)" % (op, pc, w))
        r[0] = 0
        return None

    def cop1(self, w, pc):
        rs = (w >> 21) & 31; ft = (w >> 16) & 31; fs = (w >> 11) & 31
        fd = (w >> 6) & 31; fn = w & 63
        imm = w & 0xFFFF; simm = imm - 0x10000 if imm & 0x8000 else imm
        br = (pc + 4 + (simm << 2)) & M32
        if rs == 0:   self.r[ft] = self.f[fs] & M32; return None      # mfc1
        if rs == 4:   self.f[fs] = (self.f[fs] & ~M32) | (self.r[ft] & M32); return None  # mtc1
        if rs == 2 or rs == 6: return None                            # cfc1/ctc1
        if rs == 8:                                                   # BC1
            cond = bool(self.fcr31 & 0x800000)
            want = bool(ft & 1)
            take = (cond == want)
            if ft & 2:                                                # likely
                if take: return br
                self.pc = (self.pc + 4) & M32; return None
            return br if take else None
        if rs == 16 or rs == 17:                                      # S / D
            dbl = (rs == 17)
            g = self.fd if dbl else self.fs
            st = self.setfd if dbl else self.setfs
            if   fn == 0:  st(fd, g(fs) + g(ft))
            elif fn == 1:  st(fd, g(fs) - g(ft))
            elif fn == 2:  st(fd, g(fs) * g(ft))
            elif fn == 3:  st(fd, g(fs) / g(ft))
            elif fn == 4:  st(fd, g(fs) ** 0.5)
            elif fn == 5:  st(fd, abs(g(fs)))
            elif fn == 6:  st(fd, g(fs))
            elif fn == 7:  st(fd, -g(fs))
            elif fn == 32: self.setfs(fd, g(fs))                      # cvt.s
            elif fn == 33: self.setfd(fd, g(fs))                      # cvt.d
            elif fn == 36: self.setfw(fd, int(g(fs)))                 # cvt.w
            elif fn in (12, 13):                                      # round/trunc.w
                v = g(fs); self.setfw(fd, int(v) if fn == 13 else int(round(v)))
            elif fn == 15: self.setfw(fd, int(-(-g(fs)//1)))
            elif fn == 14: self.setfw(fd, int(g(fs)//1))
            elif 48 <= fn <= 63:                                      # C.cond
                a, b = g(fs), g(ft)
                c = fn & 15
                lt = a < b; eq = a == b
                res = ((c & 4) and lt) or ((c & 2) and eq) or False
                if c in (0, 8): res = False
                self.fcr31 = (self.fcr31 & ~0x800000) | (0x800000 if res else 0)
            else: raise RuntimeError("cop1 fmt%d fn %d at %08X" % (rs, fn, pc))
            return None
        if rs == 20:                                                  # W
            v = self.fw(fs)
            if fn == 32: self.setfs(fd, float(v))
            elif fn == 33: self.setfd(fd, float(v))
            else: raise RuntimeError("cop1 W fn %d at %08X" % (fn, pc))
            return None
        raise RuntimeError("cop1 rs %d at %08X" % (rs, pc))
