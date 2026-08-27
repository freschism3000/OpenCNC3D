#!/usr/bin/env python3
"""
CNC3D -- decompressor for archive compression method 0x11.

Reverse-engineered from the N64 function at RAM 0x8005220c (ROM 0x52E0C), which the
archive read-core (0x80051bb8) dispatches to when the entry's method byte is 0x11.

It is the SAME hash-indexed LZ77 family as method 0x22 (RAM 0x80052570), with one
single structural difference: the hash table is a flat 4096-entry table addressed by
a full 12-bit hash, whereas 0x22 uses 512 buckets x 8 ways with a 3-bit rotating
way-counter.  Concretely, in the insert path:

    0x22:  sra v0,v0,1 ; andi v0,0xff8 ; addu v0,rot ; sll v0,2 ; addu tbl ; sw
    0x11:  sra v0,v0,2 ; andi v0,0x3ffc ;             addu tbl ; sw

i.e.  0x22: slot = (((40543*x) >> 1) & 0xFF8) + rot,  rot = (rot+1)&7
      0x11: slot =  ((40543*x) >> 4) & 0xFFF         (no rot at all)

and in the match path 0x22 re-derives the insert slot as (streamSlot & 0xFF8) + rot
while 0x11 simply writes back to H[streamSlot].

Everything else is byte-identical in behaviour:

Stream layout
-------------
    u8[4]                      header; src[0]==1 selects the alternate codec at
                               RAM 0x80058070 (not implemented here).
    then, repeatedly:
      u16le flags              control word, loaded as (flags | 0x10000) so the
                               sentinel bit signals "exhausted, reload"
      bit==0  LITERAL          copy 1 byte src->dst
      bit==1  MATCH            u8 b1, b2:  slot = ((b1 & 0xF0) << 4) | b2
                                           len  = 3 + (b1 & 0x0F)
                               copy `len` bytes from H[slot], byte-wise (overlap ok)

    Per outer pass the decoder consumes exactly 16 bits, except once the input
    pointer has passed (end - 0x20), where it consumes exactly 1 bit per pass and
    reloads only when the sentinel reaches the bottom.  The loop ends when the
    input pointer lands exactly on `end`.

Hash bookkeeping (must mirror the encoder exactly or the tables desync)
-----------------------------------------------------------------------
    h(p)  = ((40543 * ((d[p]<<8) ^ (d[p+1]<<4) ^ d[p+2])) >> 4) & 0xFFF
    insert(p): H[h(p)] = p
    - every 3rd consecutive literal inserts at (pos-3), then the pending count is
      held at 2 (so each further literal keeps inserting);
    - a match first flushes 1-2 pending-literal inserts, then does H[slot] = matchStart.

All 4096 slots start pointing at a fixed filler string in rodata
(RAM 0x80004eac = ROM 0x5AAC, "123456789012345678\0\0"), so a match against a
never-populated slot yields that filler rather than reading out of bounds.
"""

HASH_MUL = 40543
DICT_RAM = 0x80004EAC
DICT_ROM = 0x5AAC

# exact rodata bytes at ROM 0x5AAC, padded generously (a 18-byte match can run past
# the string into the following filenames -- the hardware would too).
DICT = (b"123456789012345678\x00\x00"
        b"CRDTFONT.IMG\x00\x00\x00\x00MENUFONT.IMG\x00\x00\x00\x00")


def _h(b0, b1, b2):
    """Hash of a 3-byte window -> 12-bit flat slot index."""
    x = ((b0 << 8) ^ (b1 << 4) ^ b2) & 0xFFFF
    return ((HASH_MUL * x) >> 4) & 0xFFF


LAST = {}   # diagnostics from the most recent decomp11() call


def decomp11(src, dict_bytes=DICT):
    """Decompress a method-0x11 stream. Returns bytes."""
    if len(src) < 4:
        return b""
    if src[0] == 1:
        raise NotImplementedError("src[0]==1 selects alt codec at RAM 0x80058070")

    n = len(src)
    near = n - 0x20                 # t9 (as an offset into src)
    ip = 4                          # t1
    out = bytearray()               # t0 == dst; len(out) is the write cursor
    D = dict_bytes

    # Virtual address space: [0, len(D)) = filler dict, [DBASE, ...) = output.
    DBASE = len(D)
    H = [0] * 4096                  # all slots -> dict start
    pend = 0                        # t2
    flag = 1                        # t3; ==1 means "exhausted, reload"

    dict_hits = [0]                 # diagnostics only
    past_end = [0]

    def rd(addr):
        if addr < DBASE:
            dict_hits[0] += 1
            return D[addr] if addr >= 0 else 0
        i = addr - DBASE
        if i < len(out):
            return out[i]
        past_end[0] += 1            # would read uninitialised dst on hardware
        return 0

    def insert(pos_v):
        H[_h(rd(pos_v), rd(pos_v + 1), rd(pos_v + 2))] = pos_v

    if ip == n:
        return b""

    while True:
        if flag == 1:                       # reload control word
            if ip + 2 > n:
                break
            lo = src[ip]; hi = src[ip + 1]; ip += 2
            flag = lo | (hi << 8) | 0x10000
        nbits = 1 if near < ip else 16

        for _ in range(nbits):
            bit = flag & 1
            if bit:
                # ---- MATCH ----
                if ip + 2 > n:
                    ip = n; break
                t4 = DBASE + len(out)          # match start (virtual)
                b1 = src[ip]; b2 = src[ip + 1]; ip += 2
                slot = ((b1 & 0xF0) << 4) | b2
                length = 3 + (b1 & 0x0F)
                s = H[slot]
                for _k in range(length):
                    out.append(rd(s)); s += 1
                if pend:
                    a = t4 - pend
                    insert(a)
                    if pend == 2:
                        insert(a + 1)
                    pend = 0
                H[slot] = t4
            else:
                # ---- LITERAL ----
                if ip >= n:
                    break
                out.append(src[ip]); ip += 1
                pend += 1
                if pend == 3:
                    pend = 2
                    insert(DBASE + len(out) - 3)
            flag >>= 1

        if ip >= n:
            break
    LAST.clear()
    LAST.update(ip=ip, n=n, clean=(ip == n), dict_reads=dict_hits[0],
                past_end_reads=past_end[0])
    return bytes(out)


# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import sys
    rom = open(sys.argv[1], "rb").read()
    # NOTE: the archive data base is 0x468A90 (immediately after the 1422-record main
    # directory table), NOT the 0x470000 that tools/romdump/archive.py currently uses.
    # Verified: with 0x468A90 all 817 method-0x22 files AND all 19 method-0x11 files
    # decode to exactly their recorded uncompressed size; with 0x470000, zero do.
    base = 0x468A90
    off = int(sys.argv[2], 16)
    comp = int(sys.argv[3], 16)
    want = int(sys.argv[4], 16) if len(sys.argv) > 4 else None
    out = decomp11(rom[base + off: base + off + comp])
    sys.stderr.write(f"got {len(out)} bytes" + (f" (want {want})" if want else "") + "\n")
    sys.stdout.buffer.write(out)
