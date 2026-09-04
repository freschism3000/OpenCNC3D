#!/usr/bin/env python3
"""
wlt.py -- decoder for the .WLT wavelet image container of Command & Conquer (N64).

WHAT A .WLT IS
--------------
The cartridge compiles briefing artwork with a wavelet codec whose module name survives
in the ROM (`../src/n64wavelet.c`, at ROM 0x1BC100), along with its own debug strings,
which name its filter bank (CUSTOM / TWO-SIX / FIVE-THREE / HAAR / REVERSIBLE) and its
coefficient models (SPLIT FIELD / ORIENTATION / SCALE & DIAGONAL / SCALE / GLOBAL /
LINE SEGMENTS / ZERO RUNS).  Nothing about that bitstream is documented, and there are
336 distinct .WLT names (337 archive records; MOEBIUS.WLT is listed twice) across more
than a dozen name families, so this decoder does not reimplement the codec: it RUNS the
cartridge's own decoder under a small MIPS III interpreter (n64emu.py) and reads the
pixels back out.  That is exact by construction: the output is what the console
produces, not an approximation of it.

CONTAINER HEADER (big-endian; the routine that parses it is F_PLANESIZE below)
    +0x00  u8   'W'   (0x57) magic; the routine rejects anything else
    +0x01  u8   flags
    +0x02  u32  control word; its top two bits select the colour model
    +0x06  u16  width
    +0x08  u16  height
    +0x0A  u8   coefficient depth  (>=9 means a 2-byte intermediate plane)
Everything after that is the wavelet bitstream, which is why sibling frames of one
animation differ in length: each frame codes only the coefficients it needs.

The decode path:
    F_SCRATCHSIZE(src,len) -> scratch size    F_PLANESIZE(src,len) -> plane size
    F_GIVESCRATCH(buf,len) -> hand the codec scratch
    F_DECODE(src,len,out,info) -> decode; out[+8]=width, out[+12]=height
    F_FIXUP(pixels,n)      -> in-place fix-up of the n pixels (run here too)
The codec itself emits RGB888, w*h*3 bytes.  F_FIXUP packs that in place to N64
RGBA5551 with the alpha bit FORCED TO 1, so a .WLT carries NO transparency: a frame that
looks like a cut-out is a subject on a black field and has to be keyed if it is ever
composited.

EVERY ADDRESS HERE IS A RAM ADDRESS, AND A ROM OFFSET IS ONLY MEANINGFUL WITH ITS
SEGMENT.  The cartridge DMAs overlays, so `rom = ram - delta` and the delta is the
segment's own.  Two segments matter here (both taken from the segment table that
romdump_local/segments.py recovers from the ROM's own loader):

    resident      ROM 0x0001000..0x00C6EA0   RAM 0x80000400   delta 0x7FFFF400
    BriefingCode  ROM 0x01B67D0..0x020E400   RAM 0x801C2600   delta 0x8000BE30

The resident base is not a guess: the ROM header's entry point at +0x08 reads 80000400.
DO NOT convert an address with the wrong segment's delta.  An earlier write-up published
F_PLANESIZE as "RAM 0x801DD790 (ROM 0x1D6960)"; 0x801DD790 is in BriefingCode, so its
ROM offset is 0x801DD790 - 0x8000BE30 = 0x1D1960, and 0x1D6960 is 0x5000 into the wrong
place.  `wlt.py offsets <ROM>` recomputes the whole table from the two segment
constants and disassembles the first instruction at each one, so the published numbers
are measured rather than typed.  Use it instead of copying a number out of prose.

ONE ARCHIVE OFFSET WORTH SAYING OUT LOUD.  The main table's data base is
dir_start + 24*nrecs = 0x0460540 + 24*1422 = 0x468A90, which is what archive_index()
computes for every table.  A base of 0x470000 is off by 0x7570 and yields streams whose
first byte is not 'W'; if a .WLT will not decode, check that first.

USAGE
    python3 wlt.py list    <ROM>                     every .WLT in the cart, with its size
    python3 wlt.py info    <ROM> <NAME.WLT> [...]
    python3 wlt.py offsets <ROM>                     re-derive every published address
    python3 wlt.py verify  <ROM> [-jN] [-s] [manifest.txt]  decode them all and check the
                                     pixels against stored digests: THE evidence run.
                                     -jN workers; -s = the nine-file smoke set only
    python3 wlt.py png     <ROM> <OUTDIR> <NAME.WLT> [...]
    python3 wlt.py sheet   <ROM> <OUT.PNG> <NAME.WLT> [...]
Names are looked up in the ROM's own archive directory, so no pre-extraction step and
no hardcoded offsets are involved.  png and sheet need Pillow; the rest do not.
"""
import os, sys, struct, hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n64emu import Mem, CPU

# ---------------------------------------------------------------- archive
# These mirror romdump_local/archive.py, which is where the tables were recovered and
# where the base rule is written down. They are repeated rather than imported so that a
# bakery step can decode a .WLT with two files and no package layout, and because the
# rule here is applied uniformly: base = dir_start + 24*nrecs for every table that is
# not one of the two trailing ones.
REC = 24
DIR_STARTS = [0x019EE70, 0x019F380, 0x019F890, 0x0460540, 0x0A4D778, 0x0D45008]
TRAILING_BASES = {0x0A4D778: 0x00A4DB80, 0x0D45008: 0x00D45410}


def archive_index(rom):
    """name -> (offset, usize, method, csize) for every archive entry."""
    idx = {}
    for start in DIR_STARTS:
        o, recs = start, []
        while True:
            nb = rom[o:o + 12]
            if not nb or nb[0] == 0:
                break
            off, us, mc = struct.unpack_from(">III", rom, o + 12)
            if off > len(rom):
                break
            recs.append((nb.split(b"\0")[0].decode("latin1"), off, us, mc >> 16, mc & 0xFFFF))
            o += REC
            if len(recs) > 4000:
                break
        base = TRAILING_BASES.get(start, start + REC * len(recs))
        for name, off, us, meth, cs in recs:
            idx.setdefault(name.upper(), (base + off, us, meth, cs))
    return idx


def read_entry(rom, idx, name):
    """Raw bytes of one archive entry, archive-decompressed if it is stored that way.

    Exactly three .WLT files carry the archive's own hash-indexed LZ77 (method 0x2200) on
    top of the wavelet stream, so that path is easy to leave untested: RIVG0901.WLT,
    RIVG0903.WLT and RIVG0904.WLT. The other 333 are stored. Most .IMG files are
    compressed, which is where these codecs were reversed; they live beside this file and
    are imported rather than duplicated. RIVG0901 is in REF_SMOKE for this reason."""
    a, us, meth, cs = idx[name.upper()]
    data = rom[a:a + cs]
    if meth == 0:
        return data
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "romdump_local"))
    if meth == 0x2200:
        from decomp import decomp22
        return decomp22(data)
    if meth == 0x1100:
        from decomp11 import decomp11
        return decomp11(data)
    raise NotImplementedError("%s: unknown archive method %04X" % (name, meth))


# ---------------------------------------------------------------- machine
# name, ROM start, ROM end, RAM start. The two segments the decode path lives in.
SEGMENTS = [
    ("resident",     0x0001000, 0x00C6EA0, 0x80000400),
    ("BriefingCode", 0x01B67D0, 0x020E400, 0x801C2600),
]

RESIDENT_ROM, RESIDENT_END, RESIDENT_RAM = SEGMENTS[0][1], SEGMENTS[0][2], SEGMENTS[0][3]
BRIEF_ROM, BRIEF_ROM_END, BRIEF_RAM = SEGMENTS[1][1], SEGMENTS[1][2], SEGMENTS[1][3]

F_SCRATCHSIZE = 0x801DD3EC
F_GIVESCRATCH = 0x801E99E0
F_PLANESIZE   = 0x801DD790          # also the container-header parser
F_DECODE      = 0x801DBEE0
F_FIXUP       = 0x800508AC
F_SETMODE     = 0x801DE2E4
F_LOADER      = 0x801D9E00          # the mission briefing loader that calls the five above
S_MODULENAME  = 0x801C7F30          # "../src/n64wavelet.c"

# Every address this file publishes, so `offsets` can check the lot in one pass.
NAMED_ADDRS = [
    ("F_FIXUP",       F_FIXUP,       "RGB888 -> RGBA5551, alpha forced to 1"),
    ("malloc5",       0x80073478,    "hooked: alloc(size, tag, 0, file, line)"),
    ("malloc6",       0x800732C4,    "hooked: alloc(pool, size, tag, 0, file, line)"),
    ("realloc",       0x80073420,    "hooked: realloc(pool, ptr, newsize)"),
    ("free",          0x80073548,    "hooked: no-op"),
    ("free2",         0x8007358C,    "hooked: no-op"),
    ("F_LOADER",      F_LOADER,      "briefing image loader; the call order below is its"),
    ("F_DECODE",      F_DECODE,      "decode(src, len, info, extra)"),
    ("F_SCRATCHSIZE", F_SCRATCHSIZE, "scratch bytes needed for this stream"),
    ("F_PLANESIZE",   F_PLANESIZE,   "container header parse -> output plane bytes"),
    ("F_SETMODE",     F_SETMODE,     "codec mode select"),
    ("F_GIVESCRATCH", F_GIVESCRATCH, "hand the codec its scratch buffer"),
    ("S_MODULENAME",  S_MODULENAME,  "string \"../src/n64wavelet.c\""),
]

HEAP = 0x80400000
STACK = 0x806F0000

# The entry points above are addresses in ONE build. NCCP is the PAL/EU cartridge
# (header CRC AE5B9465 C54D6576). Refuse anything else rather than emulate a wrong
# address and hand back plausible noise.
ROM_ID = b"NCCP"
ROM_CRC = bytes.fromhex("ae5b9465c54d6576")


def check_rom(rom):
    if rom[0x3B:0x3F] != ROM_ID or rom[0x10:0x18] != ROM_CRC:
        raise SystemExit("this decoder is built against the %s cartridge (CRC %s); "
                         "the ROM given is %r / %s"
                         % (ROM_ID.decode(), ROM_CRC.hex(),
                            rom[0x3B:0x3F], rom[0x10:0x18].hex()))


def ram_to_rom(addr):
    """(segment name, ROM offset) for a RAM address, or (None, None) if it is in neither."""
    for name, rs, re_, ram in SEGMENTS:
        if ram <= addr < ram + (re_ - rs):
            return name, addr - (ram - rs)
    return None, None


class Machine:
    """The cartridge's briefing-code overlay, resident, in a box."""

    def __init__(self, rom):
        self.mem = Mem()
        self.mem.blit(RESIDENT_RAM, rom[RESIDENT_ROM:RESIDENT_END])
        self.mem.blit(BRIEF_RAM, rom[BRIEF_ROM:BRIEF_ROM_END])
        self.cpu = CPU(self.mem)
        self.brk = HEAP
        # THESE HOOKS ARE INSURANCE, NOT MACHINERY. Instrumented across the cartridge's
        # .WLT files, not one of the five fires: decode() hands the codec every buffer it
        # needs up front, so the game heap is never entered. They are kept because a
        # stream that did allocate would otherwise run the real allocator against a heap
        # that was never initialised, which fails as plausible garbage rather than as an
        # error. 0x8007358C in particular is not the target of any `jal` in the ROM.
        self.cpu.hooks = {
            0x80073478: self._malloc5,     # alloc(size, tag, 0, file, line)
            0x800732C4: self._malloc6,     # alloc(pool, size, tag, 0, file, line)
            0x80073420: self._realloc,
            0x80073548: lambda c: 0,       # free
            0x8007358C: lambda c: 0,
        }

    def alloc(self, n, align=16):
        p = (self.brk + align - 1) & ~(align - 1)
        self.brk = p + ((n + 15) & ~15) + 64
        return p

    def _malloc5(self, c): return self.alloc(c.r[4])
    def _malloc6(self, c): return self.alloc(c.r[5])

    def _realloc(self, c):
        # realloc(pool, ptr, newsize): the decode path only ever shrinks, so keep it
        return c.r[5]

    def call(self, addr, *args):
        self.cpu.r[29] = STACK
        return self.cpu.run(addr, args)


def decode(rom, data):
    """Decode one .WLT byte string -> (width, height, RGBA5551 bytes)."""
    if len(data) < 12 or data[0] != 0x57:
        raise ValueError("not a .WLT stream (magic %02X)" % (data[0] if data else -1))
    check_rom(rom)
    m = Machine(rom)
    src = m.alloc(len(data) + 32)
    m.mem.blit(src, data)
    n = len(data)

    m.call(F_SETMODE, 6)
    scratch_n = m.call(F_SCRATCHSIZE, src, n)
    scratch = m.alloc(scratch_n)
    m.call(F_GIVESCRATCH, scratch, scratch_n)
    plane_n = m.call(F_PLANESIZE, src, n)
    outbuf = m.alloc(plane_n + 0x10)
    pixels = outbuf + 0x10
    info = m.alloc(0x40)
    extra = m.alloc(0x40)
    m.mem.w32(info + 0, pixels)
    m.mem.w32(info + 4, plane_n)      # +4 is the buffer CAPACITY, not a pointer
    m.call(F_DECODE, src, n, info, extra)
    w = m.mem.r32(info + 8)
    h = m.mem.r32(info + 12)
    if not (0 < w <= 1024 and 0 < h <= 1024):
        raise ValueError("implausible size %dx%d" % (w, h))
    m.call(F_FIXUP, pixels, w * h)
    return w, h, m.mem.read(pixels, w * h * 2)


def rgba5551_to_rgba(buf, w, h):
    out = bytearray(w * h * 4)
    for i in range(w * h):
        v = (buf[2 * i] << 8) | buf[2 * i + 1]
        r = (v >> 11) & 31
        g = (v >> 6) & 31
        b = (v >> 1) & 31
        a = v & 1
        out[4 * i + 0] = (r << 3) | (r >> 2)
        out[4 * i + 1] = (g << 3) | (g >> 2)
        out[4 * i + 2] = (b << 3) | (b >> 2)
        out[4 * i + 3] = 255 if a else 0
    return bytes(out)


def wlt_names(idx):
    return sorted(n for n in idx if n.endswith(".WLT"))


# ---------------------------------------------------------------- stored reference
# WHY DIGESTS AND NOT JUST "IT DECODED". A decoder that returns w*h pixels of the right
# shape has not proved anything: the interpreter's own header warns that getting a delay
# slot wrong "silently produces plausible garbage", and it does. Deleting the branch-likely
# nullification from n64emu.py's BEQL arm leaves all 336 files decoding to the right size
# with no error raised, and every pixel different. Only a stored digest catches that, so
# these are the reference and `verify` is red without them.
#
# REF_SMOKE is one file per distinct frame size, plus one of the three archive-compressed
# streams, so a smoke run exercises every shape the cartridge codes and both ways a stream
# reaches the decoder. REF_ALL is the sha256 of the sorted "NAME|WxH|sha256" lines of all
# 336, which is one constant that moves if any single pixel anywhere does.
EXPECT_FILES = 336                      # distinct .WLT names (337 records; MOEBIUS twice)
EXPECT_SIZES = 9                        # distinct frame sizes
REF_ALL = "929c5c30578488e1f3c137263276cf54bf168dd49b9c789bfaaca80eed0045e8"
REF_SMOKE = [
    ("A10.WLT",      "88x61",   "c981bd1f2fe4dd157da70a7ec3765db8ec49df49375291d2f8928ef00c3305cb"),
    # Stored above, LZ77 (method 0x2200) below: three .WLT are compressed and this is one,
    # so the smoke set covers both ways a wavelet stream reaches the decoder.
    ("RIVG0901.WLT", "88x61",   "ac29e667cb7247ad411a8327ddd50e3cef4ec96b3f2170b5c7554f7977776e53"),
    ("ATTACK01.WLT", "80x54",   "1e8dc44e7403b2ba8aa61bbf8867abcdd7b1c92013b0869422d3b14ae7286521"),
    ("BURD1.WLT",    "172x103", "b6f429653ae80343ddcc6a02a6a737998a7c9576c2246e99dc587977a6bcea11"),
    ("KANE1100.WLT", "172x104", "608c1a239a3724ba45b13d0f9756651cdfe55f048e11a87e926eec15c867ceca"),
    ("GDIEVA_L.WLT", "160x240", "6a01f785da32fb5722bb821d66addfff98235dbdab5a6c92f6c410be01102419"),
    ("KANE0901.WLT", "172x84",  "f20dba88b3096a8df117616ccfea93b4a8fdf81422ff7362b30c64b7f8c776cb"),
    ("FINL0397.WLT", "120x78",  "583989b688f5dc7450399284d91ad122b3b065eb1b8714ec01d600383205ea87"),
    ("FINL0449.WLT", "117x78",  "23192b7be51f1ee095f2996e6c312334f09cd318a0a406e38c29a2bf123e239f"),
    ("GDIEVA_C.WLT", "236x142", "e9f932a520cdc37caae35fe78c5e141ee5b34a830817a5369ab3b12b445f0890"),
]


# ---------------------------------------------------------------- verify
# Module-level so multiprocessing can pickle the call; the ROM is re-read per worker
# rather than shipped through the pipe.
_ROMPATH = None
_ROM = None


def _worker_init(path):
    global _ROMPATH, _ROM
    _ROMPATH = path
    _ROM = open(path, "rb").read()


def _worker(name):
    try:
        idx = archive_index(_ROM)
        raw = read_entry(_ROM, idx, name)
        hw = struct.unpack_from(">H", raw, 6)[0]
        hh = struct.unpack_from(">H", raw, 8)[0]
        w, h, px = decode(_ROM, raw)
        return (name, w, h, len(raw), hw, hh,
                hashlib.sha256(px).hexdigest(), None)
    except Exception as e:                                   # noqa: BLE001
        return (name, 0, 0, 0, 0, 0, "", "%s: %s" % (type(e).__name__, e))


def cmd_verify(rompath, jobs, manifest, smoke=False):
    rom = open(rompath, "rb").read()
    check_rom(rom)
    idx = archive_index(rom)
    names = [n for n, _wh, _d in REF_SMOKE] if smoke else wlt_names(idx)
    ref = dict((n, (wh, d)) for n, wh, d in REF_SMOKE)
    lines = []
    if jobs > 1:
        import multiprocessing as mp
        with mp.Pool(jobs, initializer=_worker_init, initargs=(rompath,)) as pool:
            res = pool.map(_worker, names, chunksize=2)
    else:
        _worker_init(rompath)
        res = [_worker(n) for n in names]
    okn = failn = hdrmismatch = refok = refbad = 0
    sizes = {}
    canon = []
    for name, w, h, nraw, hw, hh, dig, err in res:
        if err:
            failn += 1
            lines.append("WLT|FAIL|%s|%s" % (name, err))
            continue
        wh = "%dx%d" % (w, h)
        # The header's own w/h and the decoder's must agree, or one of them is being read
        # from the wrong place and the picture is not what the header promised.
        if (w, h) != (hw, hh):
            hdrmismatch += 1
            lines.append("WLT|HDRMISMATCH|%s|header=%dx%d decoded=%s" % (name, hw, hh, wh))
        if name in ref:
            if ref[name] == (wh, dig):
                refok += 1
            else:
                refbad += 1
                lines.append("WLT|REFBAD|%s|want %s %s|got %s %s"
                             % (name, ref[name][0], ref[name][1], wh, dig))
        okn += 1
        sizes[wh] = sizes.get(wh, 0) + 1
        canon.append("%s|%s|%s" % (name, wh, dig))
        lines.append("WLT|ok|%s|%s|in=%d|out=%d|sha256=%s" % (name, wh, nraw, w * h * 2, dig))
    canon.sort()
    alldig = hashlib.sha256("\n".join(canon).encode()).hexdigest()
    if smoke:
        allstate = "smoke"
    elif failn or okn != EXPECT_FILES:
        allstate = "incomplete"
    elif alldig == REF_ALL:
        allstate = "match"
    else:
        allstate = "MISMATCH(%s)" % alldig
    sizestr = ",".join("%s:%d" % (k, v) for k, v in
                       sorted(sizes.items(), key=lambda kv: (-kv[1], kv[0])))
    total = ("WLT|total|files=%d|decoded=%d|failed=%d|hdrmismatch=%d|refok=%d|refbad=%d"
             "|all=%s|distinct_sizes=%d|%s"
             % (len(names), okn, failn, hdrmismatch, refok, refbad,
                allstate, len(sizes), sizestr))
    lines.append(total)
    out = "\n".join(lines)
    print(out)
    if manifest:
        with open(manifest, "w") as f:
            f.write(out + "\n")
        print("manifest -> %s" % manifest)
    # A run that decoded nothing, or that checked no digest, is not a pass.
    bad = (failn or hdrmismatch or refbad or not names or not refok)
    if smoke:
        bad = bad or refok != len(REF_SMOKE)
    else:
        bad = bad or okn != EXPECT_FILES or len(sizes) != EXPECT_SIZES or allstate != "match"
    return 1 if bad else 0


def cmd_offsets(rompath):
    """Re-derive every address this file publishes from its OWN segment's delta."""
    rom = open(rompath, "rb").read()
    check_rom(rom)
    print("segment      ROM start ROM end   RAM start delta      RAM end")
    for nm, rs, re_, ram in SEGMENTS:
        print("%-12s %07X   %07X   %08X  %08X   %08X"
              % (nm, rs, re_, ram, ram - rs, ram + (re_ - rs)))
    print()
    print("name           RAM       segment       ROM      first insn")
    bad = 0
    for nm, addr, what in NAMED_ADDRS:
        seg, off = ram_to_rom(addr)
        if seg is None:
            print("%-14s %08X  OUT OF EVERY SEGMENT -- do not publish this" % (nm, addr))
            bad += 1
            continue
        if nm == "S_MODULENAME":
            s = rom[off:off + 32].split(b"\0")[0].decode("latin1")
            print("%-14s %08X  %-12s  %07X  %r" % (nm, addr, seg, off, s))
            if s != "../src/n64wavelet.c":
                print("    that is not the module name; the delta or the address is wrong")
                bad += 1
            continue
        w = struct.unpack_from(">I", rom, off)[0]
        op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        imm = w & 0xFFFF
        s = imm - 0x10000 if imm & 0x8000 else imm
        if op == 9 and rs == 29 and rt == 29:
            ins = "addiu sp,sp,%d   (frame set up: a real function entry)" % s
        else:
            ins = "%08X   (leaf entry: no stack frame)" % w
        print("%-14s %08X  %-12s  %07X  %s" % (nm, addr, seg, off, ins))
    print()
    print("archive main table: base = 0x%07X + 24*%d = 0x%07X"
          % (0x0460540, (0x468A90 - 0x0460540) // 24, 0x468A90))
    idx = archive_index(rom)
    names = wlt_names(idx)
    nonw = [n for n in names if read_entry(rom, idx, n)[:1] != b"W"]
    print("archive: %d distinct .WLT names, %d of them not starting 'W'"
          % (len(names), len(nonw)))
    if nonw:
        print("    " + " ".join(nonw[:8]))
        bad += 1
    if len(names) != EXPECT_FILES:
        bad += 1
    inseg = sum(1 for _n, a, _w in NAMED_ADDRS if ram_to_rom(a)[0] is not None)
    mseg, moff = ram_to_rom(S_MODULENAME)
    modok = mseg is not None and rom[moff:moff + 19] == b"../src/n64wavelet.c"
    print("WLTOFF|segments=%d|addrs=%d|inseg=%d|module=%s|names=%d|nonw=%d|bad=%d"
          % (len(SEGMENTS), len(NAMED_ADDRS), inseg,
             "ok" if modok else "WRONG", len(names), len(nonw),
             bad + (0 if modok else 1)))
    return 1 if (bad or not modok) else 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "offsets":
        return cmd_offsets(argv[2])
    if cmd == "verify":
        jobs, manifest, smoke = 1, None, False
        for a in argv[3:]:
            if a.startswith("-j"):
                jobs = int(a[2:] or "0") or (os.cpu_count() or 1)
            elif a == "-s":
                smoke = True
            else:
                manifest = a
        return cmd_verify(argv[2], jobs, manifest, smoke)
    rom = open(argv[2], "rb").read()
    idx = archive_index(rom)
    if cmd == "list":
        for name in wlt_names(idx):
            a, us, meth, cs = idx[name]
            d = read_entry(rom, idx, name)[:12]
            mark = "" if d[:1] == b"W" else "   NOT A WAVELET STREAM"
            print("%-14s %6d bytes  %3dx%-3d%s"
                  % (name, cs, struct.unpack_from(">H", d, 6)[0],
                     struct.unpack_from(">H", d, 8)[0], mark))
        return 0
    if cmd == "info":
        for name in argv[3:]:
            d = read_entry(rom, idx, name)
            w = struct.unpack_from(">H", d, 6)[0]
            h = struct.unpack_from(">H", d, 8)[0]
            print("%-14s %6d bytes  %3dx%-3d  magic %02X flags %02X ctrl %08X depth %d tail %s"
                  % (name, len(d), w, h, d[0], d[1],
                     struct.unpack_from(">I", d, 2)[0], d[10], d[11:14].hex()))
        return 0
    from PIL import Image
    if cmd == "png":
        outdir = argv[3]
        os.makedirs(outdir, exist_ok=True)
        for name in argv[4:]:
            w, h, px = decode(rom, read_entry(rom, idx, name))
            img = Image.frombytes("RGBA", (w, h), rgba5551_to_rgba(px, w, h))
            p = os.path.join(outdir, os.path.splitext(name)[0].lower() + ".png")
            img.save(p)
            print("%s  %dx%d  -> %s" % (name, w, h, p))
        return 0
    if cmd == "sheet":
        outpng = argv[3]
        frames = []
        for name in argv[4:]:
            w, h, px = decode(rom, read_entry(rom, idx, name))
            frames.append(Image.frombytes("RGBA", (w, h), rgba5551_to_rgba(px, w, h)))
        cols = min(8, len(frames))
        rows = (len(frames) + cols - 1) // cols
        cw = max(f.width for f in frames) + 4
        ch = max(f.height for f in frames) + 4
        sheet = Image.new("RGBA", (cols * cw, rows * ch), (0, 0, 0, 255))
        for i, f in enumerate(frames):
            sheet.paste(f, ((i % cols) * cw + 2, (i // cols) * ch + 2), f)
        sheet.save(outpng)
        print("%d frames -> %s" % (len(frames), outpng))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
