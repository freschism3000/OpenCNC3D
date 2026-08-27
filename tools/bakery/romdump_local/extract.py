#!/usr/bin/env python3
"""
CNC3D — full asset extractor for the Command & Conquer (N64) ROM.

Walks every archive directory, resolves each entry to its ROM address, decompresses
where needed, and writes real files out by type.

    ROM address = ARCHIVE_BASE + entry.offset      (ARCHIVE_BASE verified = 0x468A90)

Compression methods (from the read-core at RAM 0x80051bb8):
    0x00  stored      -> copy usize bytes verbatim
    0x22  hash-LZ     -> tools/romdump/decomp.py  (SOLVED: 3454/3454 exact)
    0x11  alt codec   -> RAM 0x8005220c           (not yet reversed; dumped raw)

Usage:
    python3 extract.py rom/cnc_eu.z64 assets/extracted
    python3 extract.py rom/cnc_eu.z64 assets/extracted --only INI,MAP,BRF
"""
import sys, os, struct, csv
from decomp import decomp22

REC = 24
ARCHIVE_BASE = 0x468A90
# Six real tables; six earlier "tables" were overlapping sub-ranges of the main one.
# The main table (1422 recs) ends at exactly 0x0468A90 == ARCHIVE_BASE.
DIR_STARTS = [0x019EE70, 0x019F380, 0x019F890, 0x0460540, 0x0A4D778, 0x0D45008]

def u32(d, o): return struct.unpack_from(">I", d, o)[0]

def read_rec(d, o):
    nb = d[o:o+12]
    if not nb or nb[0] == 0: return None
    name = nb.split(b"\x00")[0]
    if not all(32 <= c < 127 for c in name) or len(name) < 3: return None
    off, usz, mc = u32(d, o+12), u32(d, o+16), u32(d, o+20)
    if off > 0x2000000 or usz > 0x400000: return None
    return dict(name=name.decode("latin1"), off=off, usize=usz,
                method=mc >> 24, csize=mc & 0xFFFFFF)

def parse_dir(d, start):
    recs, o = [], start
    while o + REC <= len(d):
        r = read_rec(d, o)
        if r is None: break
        recs.append(r); o += REC
    return recs

def extract(rom_path, outdir, only=None):
    d = open(rom_path, "rb").read()
    os.makedirs(outdir, exist_ok=True)
    seen, stats = set(), {"stored": 0, "lz": 0, "alt": 0, "fail": 0}
    manifest = []
    for ds in DIR_STARTS:
        for r in parse_dir(d, ds):
            key = (r["name"], r["off"])
            if key in seen: continue
            seen.add(key)
            ext = r["name"].split(".")[-1].upper() if "." in r["name"] else "BIN"
            if only and ext not in only: continue
            p = ARCHIVE_BASE + r["off"]
            note = ""
            if r["method"] == 0x00:
                blob = d[p:p + r["usize"]]; stats["stored"] += 1
            elif r["method"] == 0x22:
                src = d[p:p + r["csize"]]
                try:
                    blob = decomp22(src)
                    if len(blob) != r["usize"]:
                        note = f"len {len(blob)}!={r['usize']}"; stats["fail"] += 1
                    else:
                        stats["lz"] += 1
                except Exception as e:
                    blob = src; note = f"ERR {e}"; stats["fail"] += 1
            else:
                blob = d[p:p + max(r["csize"], 1)]
                note = f"raw (method 0x{r['method']:02X})"; stats["alt"] += 1
            sub = os.path.join(outdir, ext)
            os.makedirs(sub, exist_ok=True)
            with open(os.path.join(sub, r["name"]), "wb") as f:
                f.write(blob)
            manifest.append([r["name"], ext, f"0x{p:07X}", r["usize"],
                             f"0x{r['method']:02X}", r["csize"], note])
    with open(os.path.join(outdir, "_manifest.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["name", "ext", "rom_addr", "usize", "method", "csize", "note"])
        w.writerows(manifest)
    print(f"extracted {len(manifest)} files -> {outdir}")
    print(f"  stored={stats['stored']}  lz-decompressed={stats['lz']}  "
          f"raw-alt-codec={stats['alt']}  problems={stats['fail']}")

if __name__ == "__main__":
    only = None
    if "--only" in sys.argv:
        only = set(sys.argv[sys.argv.index("--only") + 1].upper().split(","))
    extract(sys.argv[1], sys.argv[2], only)
