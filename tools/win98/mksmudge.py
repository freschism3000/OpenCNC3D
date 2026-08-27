#!/usr/bin/env python3
"""
mksmudge.py -- scorch marks, craters and building aprons for the Voodoo 2.

smudge.pack ships each theatre as ONE 192x360 sheet: eight 24x24 frames across by fifteen
types down. 360 is not a power of two, so the sheet cannot go on this card as it stands
and the frames are re-tiled into a single 256x256 page instead.

The output is the SAME T1TIB001 format the tiberium uses, because a smudge is drawn
exactly as a tiberium cell is -- one flat quad on the terrain's own corners, cutout, depth
write off -- and there is no reason for a second reader. Two things are folded in here so
the renderer stays generic:

  THE ART ORDER. The engine enumerates CRATER1..6, SCORCH1..6, BIB1..3, and the
  cartridge's terrain loader reads the bibs into slots 1..3 FIRST: art = t + 4 for t <= 11
  and t - 11 above it. The slot table is emitted indexed by the engine's own SmudgeType,
  so the renderer never has to know that.

  FRAMES PAST THE STRIP. Each type has its own frame count (8, 6, 4, then fives, then
  ones), and the console SKIPS a frame past the end rather than clamping (ROM 0x197374).
  Those slots are written with an out-of-range sheet index, which the renderer already
  drops.

  tools/win98/mksmudge.py <smudge.pack> <out.t1smd> [THEATER]
"""
import struct, sys, os
from collections import Counter

FW = FH = 24
PAGE = 256
PER = PAGE // FW          # 10 across

def main(src, dst, want="TEMPERATE"):
    b = open(src, "rb").read()
    if b[:7] != b"SMUDGE1":
        sys.exit("not a SMUDGE1 pack: %s" % src)
    o = 8
    ver, nth, fw, fh, types, maxf = struct.unpack_from("<6I", b, o); o += 24
    if ver != 1 or fw != FW or fh != FH:
        sys.exit("unsupported smudge pack (ver %d, %dx%d frames)" % (ver, fw, fh))
    chosen = None
    for _ in range(nth):
        nm = b[o:o+12].split(b"\0")[0].decode("latin-1"); o += 12
        tid, texw, texh = struct.unpack_from("<3I", b, o); o += 12
        counts = list(struct.unpack_from("<%dI" % types, b, o)); o += 4 * types
        px = b[o:o + texw*texh*4]; o += texw*texh*4
        if nm == want:
            chosen = (nm, texw, texh, counts, px)
    if not chosen:
        sys.exit("no %s theatre in %s" % (want, src))
    nm, texw, texh, counts, px = chosen
    print("theatre %s, sheet %dx%d, frames per type %s" % (nm, texw, texh, counts))

    # ---- palette over the opaque texels -----------------------------------------------
    hist = Counter()
    for i in range(0, len(px), 4):
        if px[i+3] >= 128:
            hist[(px[i], px[i+1], px[i+2])] += 1
    pal = [c for c, _ in hist.most_common()][:255]
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

    page = bytearray(PAGE * PAGE)
    slots = [(0xFFFF, 0, 0)] * (types * 8)
    next_slot = 0
    snapped = 0
    for t in range(types):
        art = t + 4 if t <= 11 else t - 11
        row = art - 1
        if row < 0 or row >= types:
            continue
        for f in range(8):
            if f >= counts[row]:
                continue                                   # past the strip; the console skips
            if next_slot >= PER * PER:
                sys.exit("more than %d frames; a second page would be needed" % (PER*PER))
            dx = (next_slot % PER) * FW
            dy = (next_slot // PER) * FH
            for y in range(FH):
                sy = row * FH + y
                for x in range(FW):
                    sx = f * FW + x
                    i = (sy * texw + sx) * 4
                    if px[i+3] < 128:
                        continue                            # index 0, the hole
                    c = (px[i], px[i+1], px[i+2])
                    j = lut.get(c)
                    if j is None:
                        j = nearest(c); lut[c] = j; snapped += 1
                    page[(dy + y) * PAGE + dx + x] = j
            slots[t * 8 + f] = (0, dx, dy)
            next_slot += 1
    print("palette: %d distinct colours -> 255, %d snapped; %d frames tiled into one page"
          % (len(hist), snapped, next_slot))

    palette = bytearray(768)
    for i, c in enumerate(pal):
        palette[(i+1)*3+0], palette[(i+1)*3+1], palette[(i+1)*3+2] = c
    palette[0], palette[1], palette[2] = 255, 0, 255       # the hole, loudly

    with open(dst, "wb") as f:
        f.write(b"T1TIB001")
        f.write(struct.pack("<IIIIIII", 1, types, 8, FW, FH, 1, PAGE))
        f.write(bytes(palette))
        for sl in slots:
            f.write(struct.pack("<3H", *sl))
        f.write(bytes(page))
    print("wrote %s (%d KB)" % (dst, os.path.getsize(dst) // 1024))

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "TEMPERATE")
