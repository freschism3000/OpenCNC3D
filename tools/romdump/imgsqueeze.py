#!/usr/bin/env python3
"""
CNC3D -- the "compressed IMG family" decoder.  THE FAMILY IS NOT A SEPARATE FORMAT.

WHAT WAS ACTUALLY WRONG
-----------------------
a standing gap was recorded, called "The compressed IMG family (water,
tiberium, score-screen art)": WATER1/WATER2/WATERTST.IMG, TIBERIU1..6.IMG,
TIBPOOT1..4.IMG, ANY_TI01..12.IMG, CYARD1..25.IMG and others were said to be a
second .IMG format that img.py's 16-byte-header decoder could not read, with a
u16BE RGBA5551 pixel stream, "0xFFFE escape markers" and files ~4x smaller than raw.

Every one of those observations was made on the STILL-COMPRESSED bytes.
`archive.py extract` writes, for any entry with a non-zero method, the raw
compressed blob:

    tools/romdump/archive.py, cmd_extract():
        size = r["csize"] if r["method"] else r["usize"]
        blob = d[p:p+size]

So img.py was being pointed at method-0x22 LZ streams.  The "~4x smaller than raw"
was the compression ratio; the "0xFFFE markers" were control words; the "plausible
w/h at bytes 14/16" was a coincidence of literal bytes near the front of the stream.

There is no second format and no wavelet involved.  Run the archive's own
method-0x22 codec (decomp.py, already in this directory and already proven for the
other 817 files) and the result is an ORDINARY 16-byte-header .IMG that img.py
decodes with zero changes.  Verified, and this is what `verify` prints:

    decompressed 773, .IMG header solved 771, empty stubs 2, failed 0

773 real .IMG records; 771 solve; the 2 "stubs" are empty in the cartridge itself
(PIP_LBL2.IMG is a zero-byte record, PIP_RED.IMG is 16 bytes of header with
body_size 0), and both are STORED, so nothing was decompressed wrongly.

(The 116 CAM_*.IMG entries in the three small directories at 0x019EE70 / 0x019F380 /
0x019F890 are NOT part of this and are excluded below: every one of those records has
offset = uncomp_size = comp_size = 0, i.e. those tables are a name-only index with no
data behind it, not a table we have failed to locate.  Irrelevant either way, because
the shipped cameos come from the 1995 DOS discs.)

WHAT THE WATER ART IS
---------------------
    WATER1.IMG   32x32 RGBA5551, opaque   bright royal blue with pale caustic veins
    WATER2.IMG   32x32 RGBA5551, opaque   softer steel-blue swell
    WATERTST.IMG 32x32, a near-duplicate of WATER1 (16 texels differ) -- a dev leftover;
                 gterrain.c never names it, so it is NOT shipped art.
Both tile: the wrap seams are no worse than the interior texel-to-texel delta
(L/R 4.6 vs 7.5 interior for WATER1; L/R 4.5 vs 3.5 for WATER2).

    ANY_TI01..12.IMG  288x24 4bpp + 16-entry RGBA5551 palette = twelve 24x24 tiberium
                      tiles in one strip.  gterrain.c loads any_ti01..06.
    TIBERIU1..6.IMG   172x103 RGBA5551 -- the tiberium ANALYSIS/briefing artwork
                      ("Magnification X1.0" plate), not tile art.
    CYARD1..25.IMG    80x82 RGBA5551 -- a 25-frame animation (score//build screen).

Usage:
    python3 imgsqueeze.py <rom.z64> list                    # every .IMG, with geometry
    python3 imgsqueeze.py <rom.z64> raw   NAME [out.IMG]    # decompressed .IMG bytes
    python3 imgsqueeze.py <rom.z64> png   NAME  out.png     # decoded picture
    python3 imgsqueeze.py <rom.z64> dump  <outdir> [PREFIX] # .IMG + .png for a name prefix
    python3 imgsqueeze.py <rom.z64> verify                  # decode-everything self test

Read-only on the ROM.
"""
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

import archive                      # noqa: E402  directory tables, record layout, codecs
import img as imgmod                # noqa: E402  the 16-byte-header .IMG solver
from jim import write_png           # noqa: E402


# The three small IMG directories at these starts are a name-only index: every record
# has offset = uncomp_size = comp_size = 0, so there is no data behind them to decode.
# They name the 116 CAM_*.IMG cameos, which this project takes from the 1995 DOS discs
# anyway.  Excluded here so they cannot masquerade as a codec failure.
CAMEO_DIRS = (0x019EE70, 0x019F380, 0x019F890)


def entries(rom, include_cameo_dirs=False):
    """Yield (record, data_base) for every file whose bytes are actually in the ROM."""
    for a in archive.all_archives(rom):
        base = a["base"]
        if not base:
            continue
        if a["dir"] in CAMEO_DIRS and not include_cameo_dirs:
            continue
        for r in a["recs"]:
            yield r, base


# ONE codec dispatch for the whole toolchain: archive.unpack.  Do not inline a second
# copy here.  The whole "compressed IMG family" mistake was a decoder being handed bytes
# that had not been through it.
unsqueeze = archive.unpack


def find(rom, name):
    want = name.upper()
    if not want.endswith(".IMG"):
        want += ".IMG"
    for r, base in entries(rom):
        if r["name"].upper() == want:
            return unsqueeze(rom, r, base)
    raise KeyError(name)


def _load(path):
    return open(path, "rb").read()


def cmd_list(rom):
    print("%-14s %5s %5s %4s %4s  fmt  dep  bytes" % ("name", "w", "h", "bpp", "pal"))
    for r, base in entries(rom):
        n = r["name"]
        if not n.upper().endswith(".IMG"):
            continue
        try:
            data = unsqueeze(rom, r, base)
        except Exception as e:                                   # noqa: BLE001
            print("%-14s  ** %s" % (n, e))
            continue
        s = imgmod.solve(data)
        print("%-14s %5d %5d %4s %4s %#04x %#04x %6d%s"
              % (n, s["w"], s["h"], s["bpp"], s["palc"], s["fmt"], s["depth"],
                 len(data), "" if s["ok"] else "   UNSOLVED"))


def cmd_raw(rom, name, out=None):
    data = find(rom, name)
    if out:
        open(out, "wb").write(data)
        print("wrote %s (%d bytes)" % (out, len(data)))
    else:
        sys.stdout.buffer.write(data)


def cmd_png(rom, name, out):
    data = find(rom, name)
    res = imgmod.decode(data)
    if res is None:
        raise SystemExit("%s: header does not solve" % name)
    m, rows = res
    write_png(out, m["w"], m["h"], rows)
    print("wrote %s  %dx%d %dbpp pal=%s" % (out, m["w"], m["h"], m["bpp"], m["palc"]))


def cmd_dump(rom, outdir, prefix=""):
    os.makedirs(outdir, exist_ok=True)
    ok = bad = 0
    for r, base in entries(rom):
        n = r["name"]
        if not n.upper().endswith(".IMG"):
            continue
        if prefix and not n.upper().startswith(prefix.upper()):
            continue
        try:
            data = unsqueeze(rom, r, base)
        except Exception as e:                                   # noqa: BLE001
            print("  %-14s FAILED %s" % (n, e))
            bad += 1
            continue
        open(os.path.join(outdir, n), "wb").write(data)
        res = imgmod.decode(data)
        if res is None:
            print("  %-14s decompressed but header unsolved" % n)
            bad += 1
            continue
        mm, rows = res
        write_png(os.path.join(outdir, n[:-4] + ".png"), mm["w"], mm["h"], rows)
        ok += 1
    print("dumped %d, failed %d -> %s" % (ok, bad, outdir))


def cmd_verify(rom):
    """Decode every .IMG in the main directory.  Exit 0 only if none failed.

    Two entries are empty in the cartridge itself and are counted as stubs, not
    failures: PIP_LBL2.IMG is a zero-byte record, PIP_RED.IMG is 16 bytes of header
    with body_size 0.  Both are stored (method 0), so nothing was decompressed wrong.
    """
    ok = solved = stub = bad = 0
    fails = []
    for r, base in entries(rom):
        n = r["name"]
        if not n.upper().endswith(".IMG"):
            continue
        try:
            data = unsqueeze(rom, r, base)
        except Exception as e:                                   # noqa: BLE001
            fails.append((n, str(e)))
            bad += 1
            continue
        ok += 1
        if len(data) <= 16:
            stub += 1
        elif imgmod.solve(data)["ok"]:
            solved += 1
        else:
            fails.append((n, "header unsolved"))
            bad += 1
    print("decompressed %d, .IMG header solved %d, empty stubs %d, failed %d"
          % (ok, solved, stub, bad))
    for n, why in fails[:20]:
        print("   %-14s %s" % (n, why))
    return 0 if bad == 0 else 1


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    rom = _load(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == "list":
        cmd_list(rom)
    elif cmd == "raw":
        cmd_raw(rom, sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    elif cmd == "png":
        cmd_png(rom, sys.argv[3], sys.argv[4])
    elif cmd == "dump":
        cmd_dump(rom, sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else "")
    elif cmd == "verify":
        return cmd_verify(rom)
    else:
        print("unknown cmd", cmd)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
