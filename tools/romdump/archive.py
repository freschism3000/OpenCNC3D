#!/usr/bin/env python3
"""
CNC3D -- N64 ROM archive (filesystem) parser + extractor.

The C&C (N64) ROM stores files in ~11 directory tables. Each entry is a fixed
24-byte record:

    struct Entry {              // big-endian
        char  name[12];         // null-padded (exactly 12 => no terminator)
        u32   offset;           // relative to the archive-group data base
        u32   uncomp_size;      // decompressed size
        u32   method_and_comp;  // see below
    };

The last word is (method << 16) | compressed_size for a COMPRESSED entry (method 0x2200
or 0x1100). For a STORED entry it is simply the 32-bit on-ROM size, with no method field
at all: 5 of the ROM's stored files are larger than 0xFFFF (DESERT.DA4, TEMPERAT.DA4,
TEMPERAT.DA8 and the two N1285.MRT) and their size spills into the halfword that reading
it as a method would call 0x0004 / 0x0001 / 0x0003. Proof that this reading is the right
one: with it, every record in all three data-bearing tables tiles exactly against the
next (off[i] + align2(span[i]) == off[i+1], 0 breaks in 1508 records); reading those five
as exotic methods leaves exactly 5 breaks. For every stored entry the word equals
uncomp_size on the nose, all 760 of them.

Files tile contiguously by their ON-ROM size, rounded up to 2 bytes:
off[i] + align2(comp[i] or usize[i]) == off[i+1]. `archive.py check` asserts it.
Every table's data base = table_start + 24*nrecs (the byte right after the last record).

Compression is NOT Westwood LCW; both codecs are a hash-indexed LZ77 in which a match
encodes a 12-bit hash-table SLOT rather than a distance:
  method 0x22 -> decomp.py    (512 buckets x 8 ways, 3-bit rotating way counter)
  method 0x11 -> decomp11.py  (flat 4096 entries, slot = ((40543*x) >> 4) & 0xFFF)
Both are verified: every file decodes to exactly its recorded uncompressed size.
.WLT files carry their own internal wavelet compression on top (still unreversed).

THE TRAP THIS TOOL USED TO SET (read before touching cmd_extract)
-----------------------------------------------------------------
`extract` used to write, for every entry with a non-zero method, the STILL-COMPRESSED
blob, under the file's own name and while the docstring above called itself a "raw
extractor". Anyone who then pointed img.py at those bytes saw an unreadable stream and
concluded the ROM held a second, undecoded image format. That is exactly what happened:
a standing gap was recorded, called "the compressed IMG family" (WATER*,
TIBERIU*, TIBPOOT*, ANY_TI*, CYARD*) for weeks. There is no second format. Those files
are ordinary 16-byte-header .IMG files stored with method 0x2200; decompress first and
img.py solves them with no special case (proof: tools/romdump/imgsqueeze.py verify ->
"decompressed 773, .IMG header solved 771, empty stubs 2, failed 0").

So `extract` now DECOMPRESSES by default. A file that cannot be decompressed is written
with a `.lz` suffix so its bytes can never again be mistaken for decoded content.

Usage:
    python3 archive.py rom/cnc_eu.z64 list                 # list every archive+file
    python3 archive.py rom/cnc_eu.z64 manifest out.csv     # write full CSV manifest
    python3 archive.py rom/cnc_eu.z64 extract assets/out   # decompressed file bytes
    python3 archive.py rom/cnc_eu.z64 extract-lz assets/lz # compressed blobs, *.lz names
    python3 archive.py rom/cnc_eu.z64 check                # self-test: tiling + every codec

Read-only on the ROM.
"""
import sys, os, struct, csv

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # decomp.py / decomp11.py

REC = 24
GROUP_BASE = 0x468A90          # VERIFIED data base for the main table.
# Rule (uniform across all tables): base = dir_start + 24*nrecs, i.e. the byte
# immediately after the last directory record. 0x0460540 + 24*1422 = 0x468A90.
# Trailing tables: dir@0x0A4D778 -> 0x00A4DB80 ; dir@0x0D45008 -> 0x00D45410
# (English and French builds of the same 43-file briefing/audio set).
DATA_END   = 0x1646216

# The SIX real directory tables. An earlier scan reported eleven, but six of those
# "tables" were overlapping sub-ranges of the single main table and inflated every
# count ~3.4x. The main table runs 1422 records and ends at exactly 0x0468A90 --
# which IS the data base: the directory is immediately followed by its data.
DIR_STARTS = [0x019EE70,   #   53 recs, IMG, all-zero offsets (different sub-format)
              0x019F380,   #   53 recs, IMG, ditto
              0x019F890,   #   10 recs, IMG, ditto
              0x0460540,   # 1422 recs, MAIN table -> ends at GROUP_BASE
              0x0A4D778,   #   43 recs, trailing archive, base 0x00A4DB80 (English)
              0x0D45008]   #   43 recs, trailing archive, base 0x00D45410 (French)

TRAILING_BASES = {0x0A4D778: 0x00A4DB80, 0x0D45008: 0x00D45410}

def load(path): return open(path, "rb").read()
def u32(d, o): return struct.unpack_from(">I", d, o)[0]

def read_rec(d, o):
    nb = d[o:o+12]
    if nb[0] == 0: return None
    name = nb.split(b"\x00")[0]
    if not all(32 <= c < 127 for c in name) or len(name) < 3: return None
    off = u32(d, o+12); usz = u32(d, o+16); mc = u32(d, o+20)
    method = mc >> 16; comp = mc & 0xFFFF
    if method not in (0x2200, 0x1100):
        method, comp = 0, mc          # stored: the whole word is the size (see header)
    if off > 0x2000000 or usz > 0x400000: return None
    return dict(name=name.decode("latin1"), off=off, usize=usz,
                method=method, csize=comp)

def parse_dir(d, start):
    recs = []; o = start
    while o+REC <= len(d):
        r = read_rec(d, o)
        if r is None: break
        recs.append(r); o += REC
    return recs

def all_archives(d):
    out = []
    for s in DIR_STARTS:
        recs = parse_dir(d, s)
        base = TRAILING_BASES.get(s, GROUP_BASE)
        out.append(dict(dir=s, base=base, recs=recs))
    return out

def cmd_list(d):
    for a in all_archives(d):
        recs = a["recs"]
        exts = {}
        for r in recs: exts[r["name"].split(".")[-1].upper()] = exts.get(r["name"].split(".")[-1].upper(),0)+1
        print(f"\n[dir@0x{a['dir']:07X}] base={'0x%X'%a['base'] if a['base'] else '??'} "
              f"files={len(recs)} {exts}")
        for r in recs[:4]:
            print(f"    {r['name']:12s} off=0x{r['off']:07X} usize=0x{r['usize']:X} "
                  f"method=0x{r['method']:04X} csize=0x{r['csize']:X}")

def cmd_manifest(d, path):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["dir","base","name","offset","rom_addr","usize","method","csize"])
        for a in all_archives(d):
            for r in recs_iter(a):
                addr = a["base"]+r["off"] if a["base"] else ""
                w.writerow([f"0x{a['dir']:07X}", f"0x{a['base']:X}" if a["base"] else "",
                            r["name"], f"0x{r['off']:X}",
                            f"0x{addr:07X}" if addr!="" else "",
                            f"0x{r['usize']:X}", f"0x{r['method']:04X}", f"0x{r['csize']:X}"])
    print(f"wrote manifest: {path}")

def recs_iter(a):
    for r in a["recs"]: yield r

def unpack(d, rec, base):
    """Decompressed bytes for one record, checked against the recorded size.

    THE TRAP (see the header): the on-ROM bytes of a method-0x2200 or 0x1100 entry are
    an LZ stream, not the file. Never hand those to a format decoder; they will look
    like an unknown format and cost you a week. Everything goes through here.
    """
    p = base + rec["off"]
    if rec["method"] == 0:
        return d[p:p+rec["usize"]]
    raw = d[p:p+rec["csize"]]
    if rec["method"] == 0x2200:
        from decomp import decomp22
        out = decomp22(raw)
    elif rec["method"] == 0x1100:
        from decomp11 import decomp11
        out = decomp11(raw)
    else:
        raise ValueError(f"unknown archive method 0x{rec['method']:04X}")
    if len(out) != rec["usize"]:
        raise ValueError(f"decompressed {len(out)} bytes, directory says {rec['usize']}")
    return out

def cmd_extract(d, outdir, decompress=True):
    """Write every file out. decompress=True (the default) writes USABLE bytes.

    With decompress=False, or for any entry whose codec fails, the compressed blob is
    written as <name>.lz. The suffix is the point: a `.lz` file is self-evidently not
    decoded content, so no decoder ever gets pointed at it by accident again.
    """
    n = lz = bad = 0
    for a in all_archives(d):
        if a["base"] is None:
            continue  # base unknown for these; skip until pinned
        sub = os.path.join(outdir, f"dir_{a['dir']:07X}")
        os.makedirs(sub, exist_ok=True)
        for r in a["recs"]:
            name, blob = r["name"], None
            if decompress:
                try:
                    blob = unpack(d, r, a["base"])
                except Exception as e:
                    print(f"  ! {r['name']}: {e}; writing the compressed blob as .lz")
                    bad += 1
            if blob is None:
                p = a["base"] + r["off"]
                size = r["csize"] if r["method"] else r["usize"]
                blob = d[p:p+size]
                if r["method"]:
                    name += ".lz"
                    lz += 1
            with open(os.path.join(sub, name), "wb") as f:
                f.write(blob)
            n += 1
    what = "decompressed" if decompress else "raw"
    print(f"extracted {n} {what} files ({lz} left compressed as *.lz, {bad} codec "
          f"failures) -> {outdir}")

def cmd_check(d):
    """Assert the record model against the ROM. Exit 0 only if nothing is off.

    Two properties, both of which the header claims and neither of which is obvious:
      1. every entry decompresses to exactly its recorded uncompressed size;
      2. files tile: off[i] + align2(on-ROM size) == off[i+1], with no gaps.
    Property 2 is what proves the stored-size reading in read_rec; it fails on exactly
    the five big stored files if their size halfword is misread as a method.
    """
    bad = 0
    for a in all_archives(d):
        recs = a["recs"]
        if not recs or not any(r["usize"] for r in recs):
            print(f"[dir@0x{a['dir']:07X}] {len(recs)} name-only stubs (off=usize=0), skipped")
            continue
        for i, r in enumerate(recs):
            span = r["csize"] if r["method"] else r["usize"]
            if i + 1 < len(recs):
                want = recs[i+1]["off"] - r["off"]
                if want != span + (span & 1):
                    print(f"  ! {r['name']}: spans {span} but next record starts {want} later")
                    bad += 1
            try:
                out = unpack(d, r, a["base"])
            except Exception as e:
                print(f"  ! {r['name']}: {e}")
                bad += 1
                continue
            if len(out) != r["usize"]:
                print(f"  ! {r['name']}: decoded {len(out)}, table says {r['usize']}")
                bad += 1
        print(f"[dir@0x{a['dir']:07X}] {len(recs)} records checked")
    print("archive check: %s" % ("OK, every record tiles and decodes" if not bad
                                 else f"{bad} PROBLEMS"))
    return 0 if bad == 0 else 1

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    d = load(sys.argv[1]); cmd = sys.argv[2]
    if cmd == "list":     cmd_list(d)
    elif cmd == "manifest": cmd_manifest(d, sys.argv[3])
    elif cmd == "extract":  cmd_extract(d, sys.argv[3])
    elif cmd == "extract-lz": cmd_extract(d, sys.argv[3], decompress=False)
    elif cmd == "check":    sys.exit(cmd_check(d))
    else: print("unknown cmd", cmd)

if __name__ == "__main__":
    main()
