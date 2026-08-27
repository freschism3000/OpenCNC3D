#!/usr/bin/env python3
"""
CNC3D — .JIM decoder.

.JIM files are named per MISSION (SCB01EA.JIM pairs with SCB01EA.INI / .MAP), one per
scenario, 56 in the ROM. Layout is a plain N64 CI8 (8-bit colour-index) image:

    struct JimHeader {          // big-endian
        u32 header_size;        // always 0x10
        u32 body_size;          // 0x10 + width*height
        u16 width;
        u16 height;
        u8  bytes_per_palette_entry;   // 2
        u8  unk1, unk2, unk3;          // 01 01 00
    };
    u8  pixels[width * height];        // palette indices
    u16 palette[256];                  // RGBA5551, at end of file (512 bytes)

Verified: file_size == body_size + 512 for every .JIM.

Usage:
    python3 jim.py info  <dir-or-file>
    python3 jim.py png   <file.JIM> <out.png>
    python3 jim.py all   <indir> <outdir>
"""
import sys, os, struct, zlib, glob

def parse(data):
    hdr, body, w, h = struct.unpack_from(">IIHH", data, 0)
    bpp = data[12]
    pixels = data[hdr:hdr + w * h]
    pal_raw = data[-512:]
    pal = struct.unpack(">256H", pal_raw)
    return dict(hdr=hdr, body=body, w=w, h=h, bpp=bpp,
                unk=tuple(data[13:16]), pixels=pixels, pal=pal,
                consistent=(len(data) == body + 512 and body == hdr + w * h))

def rgba5551(v):
    r = (v >> 11) & 0x1F; g = (v >> 6) & 0x1F
    b = (v >> 1) & 0x1F;  a = v & 1
    return (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if a else 0

def write_png(path, w, h, rgba_rows):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + row for row in rgba_rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)

def to_png(src, dst):
    j = parse(open(src, "rb").read())
    lut = [rgba5551(v) for v in j["pal"]]
    rows = []
    for y in range(j["h"]):
        row = bytearray()
        for x in range(j["w"]):
            i = y * j["w"] + x
            r, g, b, a = lut[j["pixels"][i]] if i < len(j["pixels"]) else (0, 0, 0, 0)
            row += bytes((r, g, b, a))
        rows.append(bytes(row))
    write_png(dst, j["w"], j["h"], rows)
    return j

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "info":
        target = sys.argv[2]
        files = sorted(glob.glob(os.path.join(target, "*.JIM"))) if os.path.isdir(target) else [target]
        print(f"{'file':16s} {'w':>5s} {'h':>5s} {'bpp':>4s} {'unk':>10s}  ok")
        for f in files:
            j = parse(open(f, "rb").read())
            print(f"{os.path.basename(f):16s} {j['w']:5d} {j['h']:5d} {j['bpp']:4d} "
                  f"{str(j['unk']):>10s}  {j['consistent']}")
    elif cmd == "png":
        j = to_png(sys.argv[2], sys.argv[3])
        print(f"wrote {sys.argv[3]}  {j['w']}x{j['h']}")
    elif cmd == "all":
        indir, outdir = sys.argv[2], sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        n = 0
        for f in sorted(glob.glob(os.path.join(indir, "*.JIM"))):
            dst = os.path.join(outdir, os.path.basename(f)[:-4] + ".png")
            to_png(f, dst); n += 1
        print(f"converted {n} files -> {outdir}")
