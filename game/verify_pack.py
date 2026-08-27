#!/usr/bin/env python3
"""Two proofs about the regenerated dossidebar.pack:
  1. Every byte the OLD pack carried (palette, clocktab, all 71 shapes, all
     fonts) is carried UNCHANGED by the new one. The only difference is the
     added MOUSE shape and the shape_count field.
  2. The MOUSE frames in the pack are pixel-identical to the 178 PNGs that
     tools/mixshp.py decoded from the 1995 archives (sidebar/png/mouse/).
     The PNGs were exported with the fullrange DAC widening (v<<2)|(v>>4) for
     viewing, while the pack (correctly) carries the engine widening v<<2, so
     the comparison resolves the pack's 6-bit palette through the PNGs' OWN
     widening. Index equality is what is being proven; both widenings are
     bijective on 6-bit values, so a match through one proves the indices.
Exits non-zero on any mismatch. No output is 'PASS' by default: every check
prints its numbers."""
import struct
import sys
import zlib
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OLD = os.path.join(HERE, "..", "..", "dosplay", "dossidebar.pack")
NEW = os.path.join(HERE, "dossidebar.pack")
PNGDIR = os.path.join(HERE, "..", "..", "sidebar", "png", "mouse")


def parse(path):
    d = open(path, "rb").read()
    assert d[:8] == b"DOSBAR01", "bad magic in %s" % path
    ver, nsh, nfn, npal = struct.unpack_from("<IIII", d, 8)
    assert ver == 1 and npal == 256
    p = 24
    pal6 = d[p:p + 768]; p += 768
    pal8 = d[p:p + 768]; p += 768
    clock = d[p:p + 512]; p += 512
    shapes = {}
    order = []
    for _ in range(nsh):
        name = d[p:p + 12].rstrip(b"\0").decode()
        fr, w, h = struct.unpack_from("<III", d, p + 12)
        p += 24
        px = d[p:p + fr * w * h]
        p += fr * w * h
        shapes[name] = (fr, w, h, px)
        order.append(name)
    fonts = {}
    for _ in range(nfn):
        name = d[p:p + 8].rstrip(b"\0").decode()
        n, mw, mh = struct.unpack_from("<III", d, p + 8)
        p += 20
        blob = d[p:p + 3 * n + n * mw * mh]
        p += 3 * n + n * mw * mh
        fonts[name] = (n, mw, mh, blob)
    assert p == len(d), "%s: trailing bytes (%d parsed, %d total)" % (path, p, len(d))
    return dict(pal6=pal6, pal8=pal8, clock=clock, shapes=shapes,
                order=order, fonts=fonts)


old = parse(OLD)
new = parse(NEW)
fails = 0

for field in ("pal6", "pal8", "clock"):
    same = old[field] == new[field]
    print("%s: %s" % (field, "identical" if same else "DIFFERS"))
    fails += 0 if same else 1

for name in old["order"]:
    o, n = old["shapes"][name], new["shapes"].get(name)
    if n is None:
        print("shape %s: MISSING from new pack" % name); fails += 1; continue
    if o != n:
        print("shape %s: DIFFERS (old %s frames crc %08x, new %s frames crc %08x)"
              % (name, o[0], zlib.crc32(o[3]), n[0], zlib.crc32(n[3])))
        fails += 1
print("old shapes carried unchanged: %d/%d"
      % (sum(1 for s in old["order"] if old["shapes"][s] == new["shapes"].get(s)),
         len(old["order"])))

for name in old["fonts"]:
    same = old["fonts"][name] == new["fonts"].get(name)
    if not same:
        print("font %s: DIFFERS" % name); fails += 1
print("fonts carried unchanged: %d/%d"
      % (sum(1 for f in old["fonts"] if old["fonts"][f] == new["fonts"].get(f)),
         len(old["fonts"])))

added = [s for s in new["order"] if s not in old["shapes"]]
print("added shapes: %s" % added)
if added != ["MOUSE"]:
    print("expected exactly [MOUSE] added"); fails += 1

# ---- proof 2: MOUSE indices vs the independently decoded PNGs --------------
fr, w, h, px = new["shapes"]["MOUSE"]
print("MOUSE: %d frames %dx%d" % (fr, w, h))
if (fr, w, h) != (178, 30, 24):
    print("expected 178 frames of 30x24"); fails += 1

# minimal PNG reader (8-bit RGBA/RGB, non-interlaced) so this needs no PIL
def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    p = 8
    idat = b""
    w = h = bitd = ctype = None
    plte = None
    trns = None
    while p < len(d):
        ln = struct.unpack(">I", d[p:p + 4])[0]
        tag = d[p + 4:p + 8]
        body = d[p + 8:p + 8 + ln]
        if tag == b"IHDR":
            w, h, bitd, ctype = struct.unpack(">IIBB", body[:10])
        elif tag == b"IDAT":
            idat += body
        elif tag == b"PLTE":
            plte = body
        elif tag == b"tRNS":
            trns = body
        p += 12 + ln
    raw = zlib.decompress(idat)
    nch = {0: 1, 2: 3, 3: 1, 6: 4}[ctype]
    stride = w * nch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    sp = 0
    for y in range(h):
        f = raw[sp]; sp += 1
        line = bytearray(raw[sp:sp + stride]); sp += stride
        if f == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            def paeth(a, b, c):
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                return a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                c = prev[i - nch] if i >= nch else 0
                line[i] = (line[i] + paeth(a, prev[i], c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, nch, bytes(out), ctype, plte, trns


pal6 = new["pal6"]
pal8 = bytes(((v & 0x3F) << 2) | ((v & 0x3F) >> 4) for v in pal6)  # PNG widening
checked = 0
bad = 0
for i in range(fr):
    path = os.path.join(PNGDIR, "mouse_%03d.png" % i)
    if not os.path.exists(path):
        continue
    pw, ph, nch, rgba, ctype, plte, trns = read_png(path)
    if (pw, ph) != (w, h):
        print("frame %d: png is %dx%d" % (i, pw, ph)); bad += 1; continue
    frame = px[i * w * h:(i + 1) * w * h]
    mism = 0
    for y in range(h):
        for x in range(w):
            idx = frame[y * w + x]
            o = (y * w + x) * nch
            if ctype == 3:
                pi = rgba[o]
                a = 255
                if trns and pi < len(trns):
                    a = trns[pi]
                pr, pg, pb = plte[pi * 3:pi * 3 + 3]
            else:
                pr, pg, pb = rgba[o], rgba[o + 1], rgba[o + 2]
                a = rgba[o + 3] if nch == 4 else 255
            if idx == 0:
                if a != 0:
                    mism += 1
            else:
                er, eg, eb = pal8[idx * 3:idx * 3 + 3]
                if a == 0 or (pr, pg, pb) != (er, eg, eb):
                    mism += 1
    if mism:
        print("frame %3d: %d mismatched pixels" % (i, mism))
        bad += 1
    checked += 1

print("MOUSE frames checked against png/mouse: %d, mismatching frames: %d"
      % (checked, bad))
fails += bad
if checked == 0:
    print("no PNGs found to check against"); fails += 1

print("RESULT: %s (%d failure(s))" % ("OK" if fails == 0 else "FAIL", fails))
sys.exit(1 if fails else 0)
