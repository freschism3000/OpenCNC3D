#!/usr/bin/env python3
"""
bake_verdict.py -- bake the N64 cartridge's MISSION ACCOMPLISHED / MISSION FAILED
banner art into verdict.pack for the renderer's verdict module.

WHERE THIS ART COMES FROM (all of it read out of the original cartridge)
    The console does NOT print text at the end of a mission.  The BriefingCode
    overlay routine at ROM 0x1CDBB8 (RAM 0x801D99E8) builds three widgets tagged
    "I0", "I1", "I2"/"I3":

      "I0" / "I1"  BLACK.IMG stretched to 328x240 at x=0 and 320x240 at x=320.
                   Both name pointers are the same string (RAM 0x801EC2FC and
                   0x801EC304 -> 0x801C7EE0 "BLACK.IMG").  Together they cover
                   the WHOLE screen, sidebar included.
      "I2" / "I3"  the verdict banner, chosen off the win flag at RAM 0x80093864
                   (ROM 0x1CDD6C): MACCOMP.IMG when set, MFAILED.IMG when clear
                   (the language word at RAM 0x800901AC swaps in MACCFRCH /
                   MFAILFRC for French).  Positioned at ((640 - 200*n) >> 1,
                   99.0f) -- ROM 0x1CDE0C..0x1CDE4C, the 99.0 being the
                   `lui $a3,0x42c6` at 0x1CDE14.  With the -160 view origin that
                   is x=60, y=99 on a 320x240 screen.

    Archive provenance (dir 0x0460540, data base 0x0468A90):
        MACCOMP.IMG  off 0x043D0D4 (abs 0x08A5B64) method 0x2200  9474 -> 21216 B
        MFAILED.IMG  off 0x043F5D6 (abs 0x08A8066) method 0x2200  6795 -> 21216 B
    Both are 200x53 RGBA5551 with a hard 0/255 alpha cutout (MACCOMP 4239 opaque
    of 10600 texels, MFAILED 2800).

    THE CODEC IS NOT RE-IMPLEMENTED HERE.  tools/romdump/imgsqueeze.py runs the
    archive's own method-0x2200 decompressor and tools/romdump/img.py solves the
    ordinary 16-byte .IMG header; this file only calls them.  The whole
    "compressed IMG family" mistake was a decoder being handed bytes that had not
    been through that dispatch.

Output: verdict.pack, magic CNC3DVRD v1, little-endian u32s (same shape as
efx.pack):
    u8 magic[8]; u32 ver; u32 count;
    per record: char name[8]; u32 w, h, uw, uh;  w*h*4 RGBA bytes
w/h are the power-of-two padding GL 1.1 and the Voodoo2 want (256x64, so no
tiling); uw/uh is the used 200x53 sub-rect.  Padding is transparent black.

Only the ENGLISH pair is baked.  MACCGERM/MFAILGER/MACCFRCH/MFAILFRC are in the
cartridge and are deliberately left out -- left open deliberately.

Usage: python3 game/bake_verdict.py [cnc_eu.z64] [out.pack] [--png DIR]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROMDUMP = os.path.normpath(os.path.join(HERE, "..", "tools", "romdump"))
sys.path.insert(0, ROMDUMP)

import imgsqueeze                                  # noqa: E402  archive + codec
import img as imgmod                               # noqa: E402  .IMG header solver
from jim import write_png                          # noqa: E402

ROM_CANDIDATES = [
    os.path.normpath(os.path.join(HERE, "..", "data", "rom", "cnc_eu.z64")),
]

# (record name in the pack, .IMG name in the cartridge archive)
ENTRIES = [("MACCOMP", "MACCOMP.IMG"),
           ("MFAILED", "MFAILED.IMG")]

BANNER_W, BANNER_H = 200, 53          # the cartridge's own geometry
PAD_W, PAD_H = 256, 64                # power of two for GL 1.1 / Voodoo2


def decode_banner(rom, imgname):
    """-> (rgba bytes, w, h).  imgsqueeze does the decompression, img.py the header."""
    data = imgsqueeze.find(rom, imgname)
    res = imgmod.decode(data)
    if res is None:
        raise SystemExit("bake_verdict: %s header does not solve" % imgname)
    m, rows = res
    assert (m["w"], m["h"]) == (BANNER_W, BANNER_H), \
        "%s is %dx%d, expected %dx%d" % (imgname, m["w"], m["h"], BANNER_W, BANNER_H)
    assert m["bpp"] == 16, "%s is %d bpp, expected 16 (RGBA5551)" % (imgname, m["bpp"])
    rgba = b"".join(rows)
    assert len(rgba) == m["w"] * m["h"] * 4, imgname
    alphas = set(rgba[i * 4 + 3] for i in range(m["w"] * m["h"]))
    assert alphas <= {0, 255}, \
        ("%s carries soft alpha %s -- RGBA5551 has a single alpha bit, so this "
         "would mean the decode is wrong" % (imgname, sorted(alphas)))
    return rgba, m["w"], m["h"], rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    pngdir = None
    if "--png" in sys.argv:
        pngdir = sys.argv[sys.argv.index("--png") + 1]
        args = [a for a in args if a != pngdir]

    rompath = None
    for c in (args[:1] + ROM_CANDIDATES):
        if os.path.exists(c):
            rompath = c
            break
    if not rompath:
        sys.exit("bake_verdict: give me the cnc_eu.z64 path")
    out = args[1] if len(args) > 1 else os.path.join(HERE, "verdict.pack")

    rom = open(rompath, "rb").read()
    f = open(out, "wb")
    f.write(b"CNC3DVRD")
    f.write(struct.pack("<II", 1, len(ENTRIES)))
    for name, imgname in ENTRIES:
        rgba, w, h, rows = decode_banner(rom, imgname)
        f.write(struct.pack("8s", name.encode()))
        f.write(struct.pack("<IIII", PAD_W, PAD_H, w, h))
        padded = bytearray(PAD_W * PAD_H * 4)
        for y in range(h):
            padded[y * PAD_W * 4: y * PAD_W * 4 + w * 4] = rgba[y * w * 4:(y + 1) * w * 4]
        f.write(bytes(padded))
        opaque = sum(1 for i in range(w * h) if rgba[i * 4 + 3] == 255)
        print("baked %-8s %dx%d padded to %dx%d, %d of %d texels opaque (%s)"
              % (name, w, h, PAD_W, PAD_H, opaque, w * h, imgname))
        if pngdir:
            os.makedirs(pngdir, exist_ok=True)
            p = os.path.join(pngdir, name + ".png")
            write_png(p, w, h, rows)
            print("  proof PNG %s" % p)
    f.close()
    print("wrote %s (%d bytes)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
