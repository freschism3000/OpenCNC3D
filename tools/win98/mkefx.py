#!/usr/bin/env python3
"""
mkefx.py -- the cartridge's own particle art (efx.pack) for the Voodoo 2.

Eight texture sequences, none larger than 64x64, straight out of the N64's resident
segment where bake_efx.py found them. Two changes for this card:

  RGBA -> RGB565, PREMULTIPLIED BY ALPHA. The effects are drawn with ADDITIVE blending,
  where a black texel contributes nothing at all, so premultiplying folds the whole alpha
  channel into the colour and the card needs no alpha at all to get the soft edges right.
  A fully transparent texel becomes black and disappears; a half-transparent one
  contributes half its colour, which is what alpha blending would have done.

  ONE TEXTURE PER FRAME, rather than a filmstrip. The frame counts are 1, 6, 8 and 12 and
  a strip of 12 would not be a power of two; the frames themselves already are, and 45
  tiny textures cost 25 KB of a 4 MB TMU.

  tools/win98/mkefx.py <efx.pack> <out.t1efx>
"""
import struct, sys, os

def main(src, dst):
    b = open(src, "rb").read()
    if b[:8] != b"CNC3DEFX":
        sys.exit("not a CNC3DEFX pack: %s" % src)
    o = 8
    ver, count = struct.unpack_from("<II", b, o); o += 8
    if ver != 1:
        sys.exit("unsupported efx pack version %d" % ver)
    seqs, blobs = [], []
    for _ in range(count):
        name = b[o:o+8]; o += 8
        frames, w, h, uw, uh = struct.unpack_from("<5I", b, o); o += 20
        if w & (w-1) or h & (h-1) or w > 256 or h > 256:
            sys.exit("%s is %dx%d, which the Voodoo cannot take" %
                     (name.split(b"\0")[0].decode(), w, h))
        px = bytearray()
        for _f in range(frames):
            f = b[o:o + w*h*4]; o += w*h*4
            for i in range(w*h):
                r, g, bb, a = f[i*4], f[i*4+1], f[i*4+2], f[i*4+3]
                r = r * a // 255; g = g * a // 255; bb = bb * a // 255
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (bb >> 3)
                px += struct.pack("<H", v)
        seqs.append((name, frames, w, h, uw, uh))
        blobs.append(bytes(px))
        print("  %-9s %d frame(s) of %dx%d" %
              (name.split(b"\0")[0].decode(), frames, w, h))
    with open(dst, "wb") as f:
        f.write(b"T1EFX001")
        f.write(struct.pack("<II", 1, len(seqs)))
        for (name, frames, w, h, uw, uh) in seqs:
            f.write(name[:8].ljust(8, b"\0"))
            f.write(struct.pack("<5I", frames, w, h, uw, uh))
        for blob in blobs:
            f.write(blob)
    print("wrote %s (%d KB)" % (dst, os.path.getsize(dst) // 1024))

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
