#!/usr/bin/env python3
"""
mktib.py -- convert dostib.pack into the form the Windows 98 build draws.

The tiberium art is the cartridge's own ANY_TI filmstrips, 12 growth types x 12 frames of
24x24, already atlased into 256x256 RGBA sheets by bake_dostiberium.py. Two things have to
change for a Voodoo 2:

  RGBA -> PALETTISED. Every other texture on this branch is GR_TEXFMT_P_8 and one page
  costs 64 KB instead of 128. The whole set is quantised to a single 255-colour palette
  with index 0 RESERVED as the hole, exactly as the mesh converter does.

  ALPHA -> A CHROMA KEY. The card has no alpha test in this pass; it has a colour key. So
  any texel the art marks transparent becomes index 0, and index 0 is written MAGENTA in
  the palette. It is deliberately a colour nothing in the art uses: reserving black there
  once made cutout failures look like plausible shadows instead of like failures.

  tools/win98/mktib.py <dostib.pack> <out.t1tib>
"""
import struct, sys, os
from collections import Counter

def main(src, dst):
    b = open(src, "rb").read()
    if b[:7] != b"DOSTIB1":
        sys.exit("not a DOSTIB1 pack: %s" % src)
    o = 8
    ver, source, sets, fw, fh, types, frames = struct.unpack_from("<7I", b, o); o += 28
    if ver != 1 or sets < 1:
        sys.exit("unsupported dostib pack (ver %d, %d sets)" % (ver, sets))
    o += 12                                              # set name
    sheets, texw, texh, cols, rows = struct.unpack_from("<5I", b, o); o += 20
    if texw != 256 or texh != 256:
        sys.exit("sheets are %dx%d; the Voodoo's maximum is 256x256" % (texw, texh))
    slots = []
    for _ in range(types * frames):
        slots.append(struct.unpack_from("<3H", b, o)); o += 6
    px = []
    for s in range(sheets):
        px.append(b[o:o + texw * texh * 4]); o += texw * texh * 4

    # ---- one palette for the whole set ------------------------------------------------
    # 255 colours, because index 0 is the hole. A texel is transparent if the art says so;
    # the threshold is the same 0.5 the desktop build's alpha test uses.
    hist = Counter()
    for s in px:
        for i in range(0, len(s), 4):
            if s[i + 3] >= 128:
                hist[(s[i], s[i + 1], s[i + 2])] += 1
    order = [c for c, _ in hist.most_common()]
    pal = order[:255]
    while len(pal) < 255:
        pal.append((0, 0, 0))
    lut = {c: i + 1 for i, c in enumerate(pal)}

    def nearest(c):
        best, bi = 1 << 30, 1
        for i, q in enumerate(pal):
            d = (c[0]-q[0])**2 + (c[1]-q[1])**2 + (c[2]-q[2])**2
            if d < best:
                best, bi = d, i + 1
        return bi

    pages, snapped, total, holes = [], 0, 0, 0
    for s in px:
        page = bytearray(texw * texh)
        for k in range(texw * texh):
            i = k * 4
            if s[i + 3] < 128:
                holes += 1
                continue                                  # index 0, the hole
            c = (s[i], s[i + 1], s[i + 2])
            total += 1
            j = lut.get(c)
            if j is None:
                j = nearest(c); lut[c] = j; snapped += 1
            page[k] = j
        pages.append(bytes(page))

    print("tiberium: %d types x %d frames of %dx%d, %d sheet(s)"
          % (types, frames, fw, fh, sheets))
    print("palette: %d distinct colours -> 255, %d texels snapped, %d transparent"
          % (len(hist), snapped, holes))

    palette = bytearray(768)
    for i, c in enumerate(pal):
        palette[(i+1)*3+0], palette[(i+1)*3+1], palette[(i+1)*3+2] = c
    palette[0], palette[1], palette[2] = 255, 0, 255       # the hole, loudly

    with open(dst, "wb") as f:
        f.write(b"T1TIB001")
        f.write(struct.pack("<IIIIIII", 1, types, frames, fw, fh, sheets, texw))
        f.write(bytes(palette))
        for sl in slots:
            f.write(struct.pack("<3H", *sl))
        for p in pages:
            f.write(p)
    print("wrote %s (%d KB)" % (dst, os.path.getsize(dst) // 1024))

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
