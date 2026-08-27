"""Python prototype of the VQA v2 decoder. vqaplay.c is the real one; this is kept as
an independent second implementation to check it against, and because it reads more
easily than the C when you are learning the format.

    python3 vqaprobe.py ../dosdata/movies/LOGO.VQA 0,120,300

Ported from the GPL sources, not guessed:
  common/vqaloader.cpp  chunk handling, CBP0 group accumulation (Groupsize parts)
  common/unvqbuff.cpp   UnVQ_4x2: two pointer planes, pointer2 == 15 means solid fill
  common/lcw.cpp        VPTZ is LCW ("format80") compressed
"""
import struct
import sys

import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "menu", "tools"))
from mixshp import lcw_uncompress  # noqa: E402

KEYS = ("Version Flags Frames W H BlockW BlockH FPS Groupsize Num1Colors CBentries "
        "Xpos Ypos MaxFramesize SampleRate Channels Bits").split()


class VQA(object):
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        d = self.d
        if d[0:4] != b"FORM" or d[8:12] != b"WVQA":
            raise SystemExit("not a VQA: %r" % d[:12])
        if d[12:16] != b"VQHD":
            raise SystemExit("expected VQHD, got %r" % d[12:16])
        n = struct.unpack_from(">I", d, 16)[0]
        f = struct.unpack_from("<HHHHHBBBBHHHHHHBB", d, 20)
        self.hdr = dict(zip(KEYS, f))
        self.pos = 20 + n + (n & 1)

        self.w = self.hdr["W"]
        self.h = self.hdr["H"]
        self.bw = self.hdr["BlockW"]
        self.bh = self.hdr["BlockH"]
        self.bpr = self.w // self.bw            # blocks per row
        self.rows = self.h // self.bh
        self.blocks = self.bpr * self.rows

        self.palette = [(0, 0, 0)] * 256
        self.codebook = None                    # active codebook, bytes
        self.partial = bytearray()              # accumulating CBP0 parts
        self.nparts = 0
        self.pending = None                     # codebook that takes over next frame
        self.frame = bytearray(self.w * self.h)
        self.audio = bytearray()                # raw SND2 payloads, in order

    def frames(self):
        """Yield (index, 8-bit frame bytes, palette) for every VQFR."""
        d = self.d
        off = self.pos
        idx = 0
        while off + 8 <= len(d):
            tag = d[off:off + 4]
            size = struct.unpack_from(">I", d, off + 4)[0]
            body = off + 8
            if tag == b"VQFR":
                self._frame_chunk(body, body + size)
                yield idx, bytes(self.frame), list(self.palette)
                idx += 1
                if self.pending is not None:
                    self.codebook, self.pending = self.pending, None
            elif tag == b"SND2":
                self.audio += d[body:body + size]
            off = body + size + (size & 1)

    def _frame_chunk(self, off, end):
        d = self.d
        while off + 8 <= end:
            tag = d[off:off + 4]
            size = struct.unpack_from(">I", d, off + 4)[0]
            body = off + 8
            payload = d[body:body + size]

            if tag == b"CPL0":
                self.palette = [(min(252, payload[i * 3] << 2), min(252, payload[i * 3 + 1] << 2),
                                 min(252, payload[i * 3 + 2] << 2)) for i in range(size // 3)]
                self.palette += [(0, 0, 0)] * (256 - len(self.palette))
            elif tag == b"CBF0":
                self.codebook = payload
            elif tag == b"CBFZ":
                self.codebook = bytes(lcw_uncompress(payload, 0,
                                                     self.hdr["CBentries"] * self.bw * self.bh)[0])
            elif tag in (b"CBP0", b"CBPZ"):
                # vqaloader.cpp:129 VQA_Load_CBP0: parts accumulate, and after
                # Groupsize of them the accumulation becomes the codebook.
                self.partial += payload
                self.nparts += 1
                if self.nparts == self.hdr["Groupsize"]:
                    blob = bytes(self.partial)
                    if tag == b"CBPZ":
                        blob = bytes(lcw_uncompress(blob, 0,
                                                    self.hdr["CBentries"] * self.bw * self.bh)[0])
                    self.pending = blob
                    self.partial = bytearray()
                    self.nparts = 0
            elif tag in (b"VPTZ", b"VPTR"):
                if tag == b"VPTZ":
                    vpt = bytes(lcw_uncompress(payload, 0, self.blocks * 2)[0])
                else:
                    vpt = payload
                self._unvq(vpt)
            off = body + size + (size & 1)

    def _unvq(self, vpt):
        """unvqbuff.cpp:14 UnVQ_4x2, byte for byte."""
        cb = self.codebook
        buf = self.frame
        w, bpr, bh, bw = self.w, self.bpr, self.bh, self.bw
        n = self.blocks
        for i in range(n):
            p1 = vpt[i]
            p2 = vpt[i + n]
            bx = (i % bpr) * bw
            by = (i // bpr) * bh
            base = by * w + bx
            if p2 == 15:
                for r in range(bh):
                    buf[base + r * w: base + r * w + bw] = bytes([p1]) * bw
            else:
                cbo = ((p2 << 8) | p1) * bw * bh
                for r in range(bh):
                    buf[base + r * w: base + r * w + bw] = cb[cbo + r * bw: cbo + (r + 1) * bw]


if __name__ == "__main__":
    import zlib

    def png(path, w, h, pix, pal):
        raw = bytearray()
        for y in range(h):
            raw.append(0)
            raw += pix[y * w:(y + 1) * w]

        def chunk(tag, payload):
            return (struct.pack(">I", len(payload)) + tag + payload
                    + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

        plte = b"".join(bytes(c) for c in pal) + b"\0" * (768 - 3 * len(pal))
        with open(path, "wb") as fh:
            fh.write(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
                     + chunk(b"PLTE", plte)
                     + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
                     + chunk(b"IEND", b""))

    path = sys.argv[1]
    want = set(int(x) for x in sys.argv[2].split(",")) if len(sys.argv) > 2 else {0, 30, 60}
    v = VQA(path)
    print(v.hdr)
    last = max(want)
    for i, fr, pal in v.frames():
        if i in want:
            png("vqa_%s_%04d.png" % (path.split("/")[-1].split(".")[0], i), v.w, v.h, fr, pal)
            print("wrote frame", i)
        if i >= last:
            break
    print("audio bytes seen:", len(v.audio))
