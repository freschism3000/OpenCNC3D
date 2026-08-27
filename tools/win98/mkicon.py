#!/usr/bin/env python3
"""
mkicon.py -- build the Windows 98 program icon out of the game's own 1995 art.

Takes a cameo straight from dossidebar.pack (the same pack the sidebar draws from) and
writes a two-size .ico. Nothing is drawn by hand: the icon is the DOS art, in the DOS
palette, so the thing on the desktop is made of the same pixels as the thing it launches.

Windows 98 wants 32x32 and 16x16, and it is happy with 8-bit colour. Index 0 is the
engine's transparent colour and becomes the icon's AND mask, so the cameo sits on the
desktop background instead of on a black tile.

  tools/win98/mkicon.py <dossidebar.pack> <out.ico> [CAMEO]
"""
import struct, sys, os

def rd32(b, o): return struct.unpack_from("<I", b, o)[0]

def load_pack(path):
    b = open(path, "rb").read()
    assert b[:8] == b"DOSBAR01", "not a DOSBAR01 pack"
    o = 8
    ver, nshape, nfont, npal = struct.unpack_from("<IIII", b, o); o += 16
    pal6 = b[o:o+768]; o += 768
    pal8 = b[o:o+768]; o += 768
    o += 512                                    # the clock translucency table
    shapes = {}
    for _ in range(nshape):
        name = b[o:o+12].split(b"\0")[0].decode("latin-1"); o += 12
        frames, w, h = struct.unpack_from("<III", b, o); o += 12
        n = frames * w * h
        shapes[name] = (frames, w, h, b[o:o+n]); o += n
    return pal8, shapes

def scale(src, sw, sh, dw, dh):
    """nearest neighbour, which is the only honest filter for palette indices"""
    out = bytearray(dw * dh)
    for y in range(dh):
        sy = y * sh // dh
        for x in range(dw):
            out[y*dw + x] = src[sy*sw + (x * sw // dw)]
    return out

def one_image(idx, w, h, pal8):
    """BITMAPINFOHEADER + 256-entry palette + 8bpp XOR bitmap + 1bpp AND mask, bottom-up"""
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 8, 0, 0, 0, 0, 256, 0)
    pal = bytearray()
    for i in range(256):
        pal += bytes((pal8[i*3+2], pal8[i*3+1], pal8[i*3+0], 0))   # BGRA
    xrow = (w + 3) & ~3
    xor = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(idx[y*w:(y+1)*w]) + bytearray(xrow - w)
        xor += row
    arow = ((w + 31) // 32) * 4
    andm = bytearray()
    for y in range(h - 1, -1, -1):
        bits = bytearray(arow)
        for x in range(w):
            if idx[y*w + x] == 0:                       # index 0 is transparent
                bits[x >> 3] |= 0x80 >> (x & 7)
        andm += bits
    return hdr + bytes(pal) + bytes(xor) + bytes(andm)

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    pal8, shapes = load_pack(sys.argv[1])
    want = sys.argv[3] if len(sys.argv) > 3 else "MTNK"
    if want not in shapes:
        print("no cameo %r; have: %s" % (want, ", ".join(sorted(shapes)[:20]))); return 1
    frames, w, h, px = shapes[want]
    print("cameo %s: %dx%d, %d frame(s)" % (want, w, h, frames))

    # The cameo is 32x24. Centre it in a square so the icon is not stretched, on the
    # transparent index so the corners fall away rather than showing letterbox bars.
    side = max(w, h)
    sq = bytearray(side * side)
    ox, oy = (side - w) // 2, (side - h) // 2
    for y in range(h):
        for x in range(w):
            sq[(oy+y)*side + ox+x] = px[y*w + x]

    imgs = [(32, 32), (16, 16)]
    blobs = [one_image(scale(sq, side, side, dw, dh), dw, dh, pal8) for dw, dh in imgs]

    out = bytearray(struct.pack("<HHH", 0, 1, len(blobs)))
    off = 6 + 16 * len(blobs)
    for (dw, dh), blob in zip(imgs, blobs):
        out += struct.pack("<BBBBHHII", dw, dh, 0, 0, 1, 8, len(blob), off)
        off += len(blob)
    for blob in blobs:
        out += blob
    open(sys.argv[2], "wb").write(out)
    print("wrote %s (%d bytes)" % (sys.argv[2], os.path.getsize(sys.argv[2])))
    return 0

if __name__ == "__main__":
    sys.exit(main())
