#!/usr/bin/env python3
"""
CNC3D — .IMG decoder (n64image.c / N64Image_Load).

.IMG shares the same 16-byte header as .JIM, but supports several N64-native pixel
formats. The header does not state bits-per-pixel directly, so we solve it from the
file size (only one combination fits exactly):

    struct ImgHeader {          // big-endian
        u32 header_size;        // 0x10
        u32 body_size;          // 0x10 + packed pixel bytes (sometimes 0)
        u16 width, height;
        u8  fmt;                // 0x00 = RGBA5551, 0x02 = colour-indexed, 0x04 = intensity
        u8  depth_code;         // 0 = 4bpp, 1 = 8bpp, 2 = 16bpp
        u8  unk;
        u8  pal_count;          // palette entries; 0 means 256 (when a palette is present)
    };
    <packed pixels>             // 4/8/16 bpp
    <u16 palette[pal_count]>    // RGBA5551, at end of file, when indexed

Usage:
    python3 img.py info <dir|file>
    python3 img.py png  <in.IMG> <out.png>
    python3 img.py all  <indir> <outdir>
"""
import sys, os, struct, glob
from jim import write_png, rgba5551

def solve(data):
    if len(data) < 16:
        return dict(hdr=0, w=0, h=0, fmt=0, depth=0, unk=0,
                    bpp=None, palc=None, pix=None, ok=False)
    hdr, body, w, h = struct.unpack_from(">IIHH", data, 0)
    fmt, depth, unk, palc = data[12], data[13], data[14], data[15]
    n = len(data)
    for bpp in (4, 8, 16):
        pix = (w * h * bpp) // 8
        for pc in ({0: 256}.get(palc, palc), palc, 0):
            pal = pc * 2
            if hdr + pix + pal == n:
                return dict(hdr=hdr, w=w, h=h, fmt=fmt, depth=depth, unk=unk,
                            bpp=bpp, palc=pc, pix=pix, ok=True)
    return dict(hdr=hdr, w=w, h=h, fmt=fmt, depth=depth, unk=unk,
                bpp=None, palc=None, pix=None, ok=False)

def decode(data):
    m = solve(data)
    if not m["ok"]: return None
    w, h, bpp = m["w"], m["h"], m["bpp"]
    body = data[m["hdr"]:m["hdr"] + m["pix"]]
    pal = None
    if m["palc"]:
        pal = [rgba5551(v) for v in struct.unpack(f">{m['palc']}H", data[-m['palc']*2:])]
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            if bpp == 16:
                o = (y * w + x) * 2
                r, g, b, a = rgba5551(struct.unpack_from(">H", body, o)[0])
            elif bpp == 8:
                i = body[y * w + x]
                r, g, b, a = pal[i] if pal and i < len(pal) else (i, i, i, 255)
            else:  # 4bpp
                o = (y * w + x) // 2
                byte = body[o] if o < len(body) else 0
                i = (byte >> 4) if (x & 1) == 0 else (byte & 0xF)
                r, g, b, a = pal[i] if pal and i < len(pal) else (i * 17,) * 3 + (255,)
            row += bytes((r, g, b, a))
        rows.append(bytes(row))
    return m, rows

def to_png(src, dst):
    res = decode(open(src, "rb").read())
    if res is None: return None
    m, rows = res
    write_png(dst, m["w"], m["h"], rows)
    return m

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "info":
        t = sys.argv[2]
        files = sorted(glob.glob(os.path.join(t, "*.IMG"))) if os.path.isdir(t) else [t]
        bad = 0
        print(f"{'file':16s} {'w':>5s} {'h':>5s} {'bpp':>4s} {'pal':>4s} fmt dep")
        for f in files:
            m = solve(open(f, "rb").read())
            if not m["ok"]: bad += 1
            print(f"{os.path.basename(f):16s} {m['w']:5d} {m['h']:5d} "
                  f"{str(m['bpp']):>4s} {str(m['palc']):>4s} "
                  f"{m['fmt']:#04x} {m['depth']:#04x}{'' if m['ok'] else '   UNSOLVED'}")
        print(f"\nunsolved: {bad}/{len(files)}")
    elif cmd == "png":
        m = to_png(sys.argv[2], sys.argv[3]); print("wrote", sys.argv[3], m and (m["w"], m["h"], m["bpp"]))
    elif cmd == "all":
        indir, outdir = sys.argv[2], sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        ok = bad = 0
        for f in sorted(glob.glob(os.path.join(indir, "*.IMG"))):
            dst = os.path.join(outdir, os.path.basename(f)[:-4] + ".png")
            if to_png(f, dst): ok += 1
            else: bad += 1
        print(f"converted {ok}, unsolved {bad} -> {outdir}")
