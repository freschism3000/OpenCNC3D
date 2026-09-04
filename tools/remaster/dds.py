"""DXT1/DXT5 -> RGBA. Small, and only what the terrain tiles actually use."""
import struct
import numpy as np

def _c565(c):
    r = ((c >> 11) & 31); g = ((c >> 5) & 63); b = (c & 31)
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))

def decode(data):
    if data[:4] != b'DDS ':
        raise ValueError('not a DDS')
    size, flags, h, w, pitch, depth, mips = struct.unpack_from('<7I', data, 4)
    pf_flags, = struct.unpack_from('<I', data, 80)
    fourcc = data[84:88]
    off = 4 + size
    if fourcc == b'DXT1':   bpb, alpha = 8, False
    elif fourcc in (b'DXT5', b'DXT3'): bpb, alpha = 16, True
    else: raise ValueError('unsupported fourcc %r' % fourcc)
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = np.zeros((h, w, 4), np.uint8); out[..., 3] = 255
    for by in range(bh):
        for bx in range(bw):
            o = off + (by * bw + bx) * bpb
            blk = data[o:o + bpb]
            co = bpb - 8            # colour half is the last 8 bytes
            c0, c1 = struct.unpack_from('<HH', blk, co)
            bits, = struct.unpack_from('<I', blk, co + 4)
            p0, p1 = _c565(c0), _c565(c1)
            if c0 > c1:
                p2 = tuple((2 * p0[i] + p1[i]) // 3 for i in range(3))
                p3 = tuple((p0[i] + 2 * p1[i]) // 3 for i in range(3))
                a3 = 255
            else:
                p2 = tuple((p0[i] + p1[i]) // 2 for i in range(3))
                p3 = (0, 0, 0); a3 = 0 if not alpha else 255
            pal = (p0, p1, p2, p3)
            if alpha:
                a0, a1 = blk[0], blk[1]
                abits = int.from_bytes(blk[2:8], 'little')
            for py in range(4):
                for px in range(4):
                    y, x = by * 4 + py, bx * 4 + px
                    if y >= h or x >= w: continue
                    idx = (bits >> (2 * (py * 4 + px))) & 3
                    out[y, x, :3] = pal[idx]
                    if alpha:
                        ai = (abits >> (3 * (py * 4 + px))) & 7
                        if a0 > a1: av = a0 if ai == 0 else a1 if ai == 1 else (((8 - ai) * a0 + (ai - 1) * a1) // 7)
                        else:       av = a0 if ai == 0 else a1 if ai == 1 else (0 if ai == 7 else 255 if ai == 6 else (((6 - ai) * a0 + (ai - 1) * a1) // 5))
                        out[y, x, 3] = av
                    elif idx == 3:
                        out[y, x, 3] = a3
    return out
