#!/usr/bin/env python3
"""
mkverdict.py -- the cartridge's MISSION ACCOMPLISHED / MISSION FAILED banner, for the
Voodoo 2.

The console does not print text at the end of a mission: it blacks the WHOLE screen,
sidebar included, and puts a pre-rendered gold bevelled banner on it (MACCOMP.IMG when
the win flag is set, MFAILED.IMG when it is clear). game/bake_verdict.py already lifted
both out of the ROM into verdict.pack as padded RGBA; this only has to change the pixel
format.

RGB565, NOT palettised, and no chroma key: the banner is drawn on a screen that has just
been cleared to black, and the art's alpha is a hard 0/255 cutout, so a transparent texel
and a black texel are the same pixel. That makes the conversion exact and the draw one
quad with nothing switched on.

  tools/win98/mkverdict.py <verdict.pack> <out.t1vrd>
"""
import struct, sys, os

def to565(px, w, h):
    out = bytearray(w * h * 2)
    for i in range(w * h):
        r, g, b, a = px[i*4], px[i*4+1], px[i*4+2], px[i*4+3]
        if a < 128:
            v = 0                                  # transparent reads as black
        else:
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[i*2] = v & 0xFF
        out[i*2+1] = (v >> 8) & 0xFF
    return bytes(out)

def main(src, dst):
    b = open(src, "rb").read()
    if b[:8] != b"CNC3DVRD":
        sys.exit("not a CNC3DVRD pack: %s" % src)
    o = 8
    ver, count = struct.unpack_from("<II", b, o); o += 8
    if ver != 1:
        sys.exit("unsupported verdict pack version %d" % ver)
    want = {"MACCOMP": None, "MFAILED": None}
    for _ in range(count):
        name = b[o:o+8].split(b"\0")[0].decode("latin-1"); o += 8
        w, h, uw, uh = struct.unpack_from("<IIII", b, o); o += 16
        px = b[o:o + w*h*4]; o += w*h*4
        if name in want and want[name] is None:
            want[name] = (w, h, uw, uh, to565(px, w, h))
            print("  %-8s %dx%d, used %dx%d" % (name, w, h, uw, uh))
    missing = [k for k, v in want.items() if v is None]
    if missing:
        sys.exit("verdict pack has no %s" % ", ".join(missing))
    with open(dst, "wb") as f:
        f.write(b"T1VRD001")
        f.write(struct.pack("<I", 1))
        for k in ("MACCOMP", "MFAILED"):
            w, h, uw, uh, px = want[k]
            f.write(struct.pack("<IIII", w, h, uw, uh))
            f.write(px)
    print("wrote %s (%d KB)" % (dst, os.path.getsize(dst) // 1024))

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
