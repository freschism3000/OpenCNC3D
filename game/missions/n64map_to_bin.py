#!/usr/bin/env python3
"""Convert an N64 C&C .MAP into a PC Tiberian Dawn .BIN.

Evidence for the mapping (all verified, none guessed):

  The ROM ships a per-theater tile-index -> (template, icon) table:
      <THEATER>.TL4   2 bytes per 4bpp tile   (DESERT 945 tiles, TEMPERAT 315)
      <THEATER>.TL8   2 bytes per 8bpp tile   (DESERT  28 tiles, TEMPERAT 443)
  Those counts match the tile image banks exactly at 24x24 px:
      DESERT.DA4   272160 B / 288 B = 945     DESERT.DA8    16128 B / 576 B =  28
      TEMPERAT.DA4  90720 B / 288 B = 315     TEMPERAT.DA8 255168 B / 576 B = 443

  N64 .MAP tile IDs observed across all 80 maps land in exactly those ranges:
      4bpp bank: ID 0 .. N4-1     (desert max seen 944 = last tile; temperate 314 = last)
      8bpp bank: ID 1024 .. 1024+N8-1  (desert 1024..1051 = 28; temperate 1024..1466 = 443)
      0xFFFF = clear

  Every one of the 1731 table entries names a real TD template index whose icon
  number is inside width*height, and whose theater mask matches the table's
  theater. Zero violations.  The only special case is table slots 0..15, which
  are CLEAR1 icons 0..15 (the 16 random clear-terrain variants); maps never
  reference them because clear is encoded as 0xFFFF.

  So: PC .BIN cell bytes == the TL table entry for that tile ID.  A lookup, not
  a reconstruction.
"""
import sys, os, struct, math

# WHERE THE ROM'S EXTRACTED FILES LIVE.
#
# This was once a hardcoded path into a scratch directory, which would have broken the
# moment that directory was cleaned up -- taking the first step of the new-mission
# pipeline with it.
#
# Resolution order: $CNC3D_EXTRACTED, then the repo's own extracted tree, then the
# bakery's local copy. If none of them exists, say so and exit rather than failing later
# with a confusing FileNotFoundError on a tile bank.
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.normpath(os.path.join(_HERE, "..", ".."))
_CANDIDATES = [
    os.environ.get("CNC3D_EXTRACTED"),
    os.path.join(_REPO, "data", "extracted"),
    os.path.join(_REPO, "tools", "bakery", "sharecopy", "assets", "extracted"),
]
EXTRACTED = next((c for c in _CANDIDATES if c and os.path.isdir(c)), None)
if EXTRACTED is None:
    raise SystemExit(
        "n64map_to_bin: cannot find the ROM's extracted files. Tried:\n  "
        + "\n  ".join(str(c) for c in _CANDIDATES)
        + "\nRun: python3 tools/romdump/archive.py data/rom/cnc_eu.z64 extract "
          "data/extracted\nor set CNC3D_EXTRACTED to an existing extract.")
BANK8_BASE = 1024

def theater_root(ini_path):
    for line in open(ini_path, errors='ignore'):
        if line.strip().upper().startswith('THEATER='):
            th = line.split('=', 1)[1].strip().upper()
            return {'DESERT': 'DESERT', 'TEMPERATE': 'TEMPERAT',
                    'WINTER': 'WINTER'}.get(th, th[:8])
    raise SystemExit("no Theater= in " + ini_path)

def load_tl(root):
    tl4 = open(os.path.join(EXTRACTED, 'TL4', root + '.TL4'), 'rb').read()
    tl8 = open(os.path.join(EXTRACTED, 'TL8', root + '.TL8'), 'rb').read()
    return tl4, tl8

def convert(map_path, ini_path, out_path):
    root = theater_root(ini_path)
    tl4, tl8 = load_tl(root)
    n4, n8 = len(tl4) // 2, len(tl8) // 2
    raw = open(map_path, 'rb').read()
    # Grid size from the .MAP's own length: 2 bytes per cell, square grid.
    # 8192 bytes = 64x64 (legacy), 32768 = 128x128 (Version=1).
    ncells, rem = divmod(len(raw), 2)
    w = math.isqrt(ncells)
    if rem or w * w != ncells:
        raise SystemExit("%s is %d bytes; a square .MAP is 2*W*W bytes "
                         "(8192 for 64x64, 32768 for 128x128)" % (map_path, len(raw)))
    ids = struct.unpack('>%dH' % ncells, raw)

    out = bytearray()
    stats = dict(clear=0, bank4=0, bank8=0, unmapped=0)
    for tid in ids:
        if tid == 0xFFFF:
            out += b'\xFF\x00'; stats['clear'] += 1; continue
        if tid < n4:
            t, i = tl4[2 * tid], tl4[2 * tid + 1]; stats['bank4'] += 1
        elif BANK8_BASE <= tid < BANK8_BASE + n8:
            k = tid - BANK8_BASE
            t, i = tl8[2 * k], tl8[2 * k + 1]; stats['bank8'] += 1
        else:
            out += b'\xFF\x00'; stats['unmapped'] += 1; continue
        if t == 0:                 # CLEAR1 variant -> plain clear
            out += b'\xFF\x00'; stats['clear'] += 1
        else:
            out += bytes([t, i])
    assert len(out) == len(raw)
    open(out_path, 'wb').write(out)
    print("%s -> %s  theater=%s  clear=%d bank4=%d bank8=%d unmapped=%d"
          % (os.path.basename(map_path), os.path.basename(out_path), root,
             stats['clear'], stats['bank4'], stats['bank8'], stats['unmapped']))
    return stats

if __name__ == '__main__':
    for scen in sys.argv[1:]:
        convert(os.path.join(EXTRACTED, 'MAP', scen + '.MAP'),
                os.path.join(EXTRACTED, 'INI', scen + '.INI'),
                os.path.join(os.path.dirname(os.path.abspath(__file__)), scen + '.BIN'))
