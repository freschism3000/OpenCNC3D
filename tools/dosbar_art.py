#!/usr/bin/env python3
"""
dosbar_art.py -- take the DOS sidebar apart into editable PNGs, and put edited PNGs back.

The sidebar is drawn at runtime from `dossidebar.pack`: 8-bit palettised shapes with
index 0 transparent, composited by sidebar/dosbar.c. Nobody can paint on that, so this
exports every shape as a PNG and imports edited PNGs back into a new pack.

    python3 dosbar_art.py export playable/dossidebar.pack art_out/
    python3 dosbar_art.py import playable/dossidebar.pack art_in/ new.pack

Rules for editing, enforced on import:

  * KEEP THE PIXEL SIZE. Each PNG is the shape's own size, and a multi-frame shape is
    one horizontal strip of frames. Every layout number in dosbar.c is absolute, so a
    shape that changes size lands in the wrong place on screen.
  * TRANSPARENCY IS INDEX 0, exported as alpha 0. What you leave transparent stays
    transparent; anything opaque is snapped to the nearest colour in the game's own
    256 colour palette, which is what the hardware had.
  * THE PALETTE IS FIXED. Paint in any colours you like; import reports how far each
    shape had to travel to reach the palette, so a large number is a warning that the
    result will not look like what you drew.

Pack format (little endian), shared with the menu's dosmenu.pack:
    "DOSBAR01", u32 version, u32 shapes, u32 fonts, u32 pal_entries,
    u8 pal6[768], u8 pal8[768], u8 clocktab[512],
    shapes: char name[12], u32 frames, u32 w, u32 h, u8 pixels[frames*w*h]
    fonts:  char name[8],  u32 nchars, u32 maxw, u32 maxh, widths, ytop, rows, cells
"""
import os
import struct
import sys

HDR = struct.Struct("<8sIIII")


def read_pack(path):
    blob = open(path, "rb").read()
    magic, version, nshapes, nfonts, npal = HDR.unpack_from(blob, 0)
    if magic != b"DOSBAR01":
        raise SystemExit("%s: not a DOSBAR01 pack" % path)
    off = HDR.size
    pal6 = blob[off:off + 768]; off += 768
    pal8 = blob[off:off + 768]; off += 768
    clock = blob[off:off + 512]; off += 512

    shapes = []
    for _ in range(nshapes):
        name = blob[off:off + 12].rstrip(b"\0").decode(); off += 12
        frames, w, h = struct.unpack_from("<III", blob, off); off += 12
        n = frames * w * h
        shapes.append([name, frames, w, h, blob[off:off + n]])
        off += n

    return dict(version=version, npal=npal, pal6=pal6, pal8=pal8, clock=clock,
                shapes=shapes, fonts_blob=blob[off:], nfonts=nfonts)


def write_png(path, w, h, rgba):
    import zlib
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b""))


def cmd_export(packpath, outdir):
    p = read_pack(packpath)
    os.makedirs(outdir, exist_ok=True)
    pal = p["pal8"]
    manifest = []
    for name, frames, w, h, px in p["shapes"]:
        W = w * frames
        rgba = bytearray(W * h * 4)
        for f in range(frames):
            for y in range(h):
                row = (f * h + y) * w
                for x in range(w):
                    idx = px[row + x]
                    o = (y * W + f * w + x) * 4
                    if idx:
                        rgba[o] = pal[idx * 3]
                        rgba[o + 1] = pal[idx * 3 + 1]
                        rgba[o + 2] = pal[idx * 3 + 2]
                        rgba[o + 3] = 255
        write_png(os.path.join(outdir, name + ".png"), W, h, rgba)
        manifest.append((name, frames, w, h))

    with open(os.path.join(outdir, "SHAPES.txt"), "w") as fh:
        fh.write("name          frames  frame size   png size\n")
        for name, frames, w, h in manifest:
            fh.write("%-13s %5d   %3dx%-3d      %4dx%-4d\n"
                     % (name, frames, w, h, w * frames, h))
    print("exported %d shapes to %s" % (len(manifest), outdir))
    return manifest


def read_png_rgba(path):
    """Minimal PNG reader: 8 bit RGB / RGBA / palette / grey, no interlace."""
    import zlib
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s: not a PNG" % path)
    off, idat, w, h, depth, ctype, plte, trns = 8, b"", 0, 0, 8, 6, None, None
    while off < len(d):
        n = struct.unpack_from(">I", d, off)[0]
        tag = d[off + 4:off + 8]
        payload = d[off + 8:off + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack_from(">IIBB", payload, 0)
        elif tag == b"PLTE":
            plte = payload
        elif tag == b"tRNS":
            trns = payload
        elif tag == b"IDAT":
            idat += payload
        elif tag == b"IEND":
            break
        off += 12 + n
    if depth != 8:
        raise SystemExit("%s: %d bit PNG, save it as 8 bits per channel" % (path, depth))

    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * bpp)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + stride]); pos += stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                p_ = a + b - c
                pa, pb, pc = abs(p_ - a), abs(p_ - b), abs(p_ - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        if ctype == 6:
            rgba[i * 4:i * 4 + 4] = out[i * 4:i * 4 + 4]
        elif ctype == 2:
            rgba[i * 4:i * 4 + 3] = out[i * 3:i * 3 + 3]
            rgba[i * 4 + 3] = 255
        elif ctype == 3:
            idx = out[i]
            rgba[i * 4:i * 4 + 3] = plte[idx * 3:idx * 3 + 3]
            rgba[i * 4 + 3] = trns[idx] if (trns and idx < len(trns)) else 255
        else:
            g = out[i * bpp]
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = g
            rgba[i * 4 + 3] = out[i * bpp + 1] if ctype == 4 else 255
    return w, h, rgba


def cmd_import(packpath, indir, outpath):
    p = read_pack(packpath)
    pal = p["pal8"]
    lut = [(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]) for i in range(256)]
    cache = {}

    newshapes = []
    changed = 0
    for name, frames, w, h, px in p["shapes"]:
        f = os.path.join(indir, name + ".png")
        if not os.path.exists(f):
            newshapes.append((name, frames, w, h, px))
            continue
        pw, ph, rgba = read_png_rgba(f)
        if pw != w * frames or ph != h:
            raise SystemExit("%s is %dx%d, the shape needs %dx%d (%d frame(s) of %dx%d)"
                             % (f, pw, ph, w * frames, h, frames, w, h))
        buf = bytearray(frames * w * h)
        worst = 0
        for fr in range(frames):
            for y in range(h):
                for x in range(w):
                    o = (y * pw + fr * w + x) * 4
                    if rgba[o + 3] < 128:
                        continue                       # stays index 0, transparent
                    key = rgba[o] | (rgba[o + 1] << 8) | (rgba[o + 2] << 16)
                    hit = cache.get(key)
                    if hit is None:
                        r, g, b = rgba[o], rgba[o + 1], rgba[o + 2]
                        best, bestd = 1, 1 << 30
                        for i in range(1, 256):        # never pick 0: it is transparent
                            cr, cg, cb = lut[i]
                            dd = (cr - r) ** 2 + (cg - g) ** 2 + (cb - b) ** 2
                            if dd < bestd:
                                best, bestd = i, dd
                        hit = cache[key] = (best, bestd)
                    buf[(fr * h + y) * w + x] = hit[0]
                    if hit[1] > worst:
                        worst = hit[1]
        newshapes.append((name, frames, w, h, bytes(buf)))
        changed += 1
        print("  %-13s reimported, worst colour distance %d%s"
              % (name, int(worst ** 0.5), "   <-- large, check this one" if worst ** 0.5 > 60 else ""))

    with open(outpath, "wb") as fh:
        fh.write(HDR.pack(b"DOSBAR01", p["version"], len(newshapes), p["nfonts"], p["npal"]))
        fh.write(p["pal6"])
        fh.write(p["pal8"])
        fh.write(p["clock"])
        for name, frames, w, h, px in newshapes:
            fh.write(name.encode().ljust(12, b"\0"))
            fh.write(struct.pack("<III", frames, w, h))
            fh.write(px)
        fh.write(p["fonts_blob"])
    print("wrote %s: %d shapes, %d replaced" % (outpath, len(newshapes), changed))


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "export":
        cmd_export(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 5 and sys.argv[1] == "import":
        cmd_import(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        print(__doc__)
        sys.exit(2)
