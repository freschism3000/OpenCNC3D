#!/usr/bin/env python3
"""
CNC3D — decompressor for archive compression method 0x22.

Reverse-engineered from the N64 function at RAM 0x80052570 (ROM 0x53170), which the
archive read-core (0x80051bb8) dispatches to when the entry's method byte is 0x22.
This codec covers ~3,482 of the ROM's 5,464 files: all missions (.INI), terrain maps
(.MAP), briefings (.BRF) and most textures (.IMG).

It is NOT Westwood LCW. It is a hash-indexed LZ77 variant: instead of encoding a
back-reference as a distance, a match encodes a 12-bit *hash-table slot*, and the
encoder and decoder maintain byte-identical 4096-entry hash tables keyed on a
3-byte rolling hash. Slots are 8-way (a 3-bit rotating counter picks the way).

Stream layout
-------------
    u8[4]                      header; src[0]==1 selects an alternate codec
                               (RAM 0x80058070) -- not implemented here.
    then, repeatedly:
      u16le flags              control word; consumed LSB-first, and loaded into a
                               17-bit register as (flags | 0x10000) so the sentinel
                               bit signals "exhausted, reload".
      bit==0  LITERAL          copy 1 byte src->dst
      bit==1  MATCH            u8 b1, b2:  slot = ((b1 & 0xF0) << 4) | b2
                                           len  = 3 + (b1 & 0x0F)
                               copy `len` bytes from H[slot], byte-wise (overlap ok)

    Per pass the decoder processes 16 bits normally, but only ONE bit once the input
    pointer passes (end - 0x20); in that tail state it keeps drawing bits from the
    same register until the sentinel forces a reload.

Hash bookkeeping (must mirror the encoder exactly or the tables desync)
-----------------------------------------------------------------------
    h(p)  = ((40543 * ((d[p]<<8) ^ (d[p+1]<<4) ^ d[p+2])) >> 1) & 0xFF8
    insert(p): H[h(p) + rot] = p ; rot = (rot + 1) & 7
    - every 3rd consecutive literal inserts at (pos-3), then the pending count
      is held at 2 (so each further literal keeps inserting);
    - a match first flushes 1-2 pending-literal inserts, then inserts itself at
      H[(slot & 0xFF8) + rot] = match_start.

All 4096 slots start pointing at a fixed 19-byte filler string in rodata
(RAM 0x80004eac = ROM 0x5AAC, "1234567890123456\x0078"), so a match against a
never-populated slot yields that filler rather than reading out of bounds.
"""

HASH_MUL = 40543
DICT_RAM = 0x80004EAC
DICT_ROM = 0x5AAC
DICT_LEN = 0x40           # generous; only the first bytes are ever reachable

def _h(b0, b1, b2):
    return ((HASH_MUL * (((b0 << 8) ^ (b1 << 4) ^ b2) & 0xFFFF)) >> 1) & 0xFF8

def decomp22(src, dict_bytes=b"1234567890123456\x0078" + b"\x00" * 64):
    """Decompress a method-0x22 stream. Returns bytes."""
    if len(src) < 4:
        return b""
    if src[0] == 1:
        raise NotImplementedError("src[0]==1 selects alt codec at RAM 0x80058070")

    n = len(src)
    near = n - 0x20                 # t9
    ip = 4                          # t2
    out = bytearray()               # dst, t1 == len(out)
    D = dict_bytes

    # Virtual address space: [0, len(D)) = filler dict, [DBASE, ...) = output.
    DBASE = len(D)
    H = [0] * 4096                  # all slots -> dict start
    rot = 0                         # t0
    pend = 0                        # t3
    flag = 1                        # t4; ==1 means "exhausted, reload"

    def rd(addr):
        return D[addr] if addr < DBASE else out[addr - DBASE]

    def insert(pos_v):              # pos_v is a virtual address
        nonlocal rot
        h = _h(rd(pos_v), rd(pos_v + 1), rd(pos_v + 2))
        H[h + rot] = pos_v
        rot = (rot + 1) & 7

    while ip < n:
        if flag == 1:               # reload control word
            if ip + 2 > n:
                break
            lo = src[ip]; hi = src[ip + 1]; ip += 2
            flag = lo | (hi << 8) | 0x10000
        nbits = 1 if near < ip else 16

        for _ in range(nbits):
            bit = flag & 1
            flag >>= 1
            if bit:
                # ---- MATCH ----
                if ip + 2 > n: ip = n; break
                t6 = DBASE + len(out)          # match start (virtual)
                b1 = src[ip]; b2 = src[ip + 1]; ip += 2
                slot = ((b1 & 0xF0) << 4) | b2
                length = 3 + (b1 & 0x0F)
                s = H[slot]
                for _k in range(length):
                    out.append(rd(s)); s += 1
                if pend:
                    a = t6 - pend
                    insert(a)
                    if pend == 2:
                        insert(a + 1)
                    pend = 0
                H[(slot & 0xFF8) + rot] = t6
                rot = (rot + 1) & 7
            else:
                # ---- LITERAL ----
                if ip >= n: break
                out.append(src[ip]); ip += 1
                pend += 1
                if pend == 3:
                    insert(DBASE + len(out) - 3)
                    pend = 2
            if flag == 1 or ip >= n:
                break
    return bytes(out)


if __name__ == "__main__":
    import sys
    rom = open(sys.argv[1], "rb").read()
    base = 0x470000
    off  = int(sys.argv[2], 16)
    comp = int(sys.argv[3], 16)
    want = int(sys.argv[4], 16) if len(sys.argv) > 4 else None
    out = decomp22(rom[base + off: base + off + comp])
    pr = sum(1 for b in out if 9 <= b < 127) / max(len(out), 1)
    sys.stderr.write(f"got {len(out)} bytes"
                     + (f" (want {want})" if want else "")
                     + f", printable={pr:.2f}\n")
    sys.stdout.buffer.write(out[:1000])
