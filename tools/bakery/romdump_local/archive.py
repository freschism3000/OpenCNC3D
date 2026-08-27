#!/usr/bin/env python3
"""
CNC3D — N64 ROM archive (filesystem) parser + raw extractor.

The C&C (N64) ROM stores files in ~11 directory tables. Each entry is a fixed
24-byte record:

    struct Entry {              // big-endian
        char  name[12];         // null-padded (exactly 12 => no terminator)
        u32   offset;           // relative to the archive-group data base
        u32   uncomp_size;      // decompressed size
        u32   method_and_comp;  // (method << 16) | compressed_size
    };                          // method: 0x0000 = stored, 0x2200 = LZ-compressed

Files tile contiguously by *compressed* size: off[i] + comp[i] == off[i+1].
Every table's data base = table_start + 24*nrecs (the byte right after the last record).

Compression is NOT Westwood LCW; both codecs are a hash-indexed LZ77 in which a match
encodes a 12-bit hash-table SLOT rather than a distance:
  method 0x22 -> decomp.py    (512 buckets x 8 ways, 3-bit rotating way counter)
  method 0x11 -> decomp11.py  (flat 4096 entries, slot = ((40543*x) >> 4) & 0xFFF)
Both are verified: every file decodes to exactly its recorded uncompressed size.
.WLT files carry their own internal wavelet compression on top (still unreversed).

Usage:
    python3 archive.py rom/cnc_eu.z64 list                 # list every archive+file
    python3 archive.py rom/cnc_eu.z64 manifest out.csv     # write full CSV manifest
    python3 archive.py rom/cnc_eu.z64 extract assets/raw    # dump raw file bytes

Read-only on the ROM.
"""
import sys, os, struct, csv

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

def cmd_extract(d, outdir):
    n = 0
    for a in all_archives(d):
        if a["base"] is None:
            continue  # base unknown for these; skip until pinned
        sub = os.path.join(outdir, f"dir_{a['dir']:07X}")
        os.makedirs(sub, exist_ok=True)
        for r in a["recs"]:
            p = a["base"] + r["off"]
            size = r["csize"] if r["method"] else r["usize"]
            blob = d[p:p+size]
            with open(os.path.join(sub, r["name"]), "wb") as f:
                f.write(blob)
            n += 1
    print(f"extracted {n} raw files -> {outdir}")

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    d = load(sys.argv[1]); cmd = sys.argv[2]
    if cmd == "list":     cmd_list(d)
    elif cmd == "manifest": cmd_manifest(d, sys.argv[3])
    elif cmd == "extract":  cmd_extract(d, sys.argv[3])
    else: print("unknown cmd", cmd)

if __name__ == "__main__":
    main()
