#!/usr/bin/env python3
"""
CNC3D -- the console's PER-VERTEX TERRAIN SHADE, recovered from the cartridge.

WHAT THIS IS
------------
The N64 build lights its terrain in SOFTWARE, once, when a cell's four vertices are
created. It does NOT use the RSP's hardware lighting (the terrain's geometry mode
explicitly CLEARS G_LIGHTING -- see terrain_light_notes.md). Every terrain vertex
carries a single 8-bit "shade" byte that the cartridge computes from the heightfield
itself, plus an RGB tint sampled from the scenario's CM<SCEN>.IMG colour map.

This script reproduces that computation bit-for-bit from the ROM's own constants and
writes it out as JSON + a PNG visualisation. It is READ-ONLY on the ROM.

THE FORMULA (disassembled, not guessed -- gterrain.c vertex builder,
RAM 0x801F4984 / ROM 0x196774, shading tail at ROM 0x196B00..0x196BD0)
----------------------------------------------------------------------
For every corner (X, Y) of the 65x65 grid, with H(x,y) = heightmap[y*65+x] * 4
(world units; one cell is 256 world units wide):

    N = (0,0,0)
    if X>0  and Y>0 : N += (0, H(X,Y-1)-H, -256) x (-256, H(X-1,Y)-H, 0)
    if X>0  and Y<64: N += (-256, H(X-1,Y)-H, 0) x (0, H(X,Y+1)-H, +256)
    if X<64 and Y>0 : N += (+256, H(X+1,Y)-H, 0) x (0, H(X,Y-1)-H, -256)
    if X<64 and Y<64: N += (0, H(X,Y+1)-H, +256) x (+256, H(X+1,Y)-H, 0)
    N = normalise(N) * 127.0                      (fn 0x8007B0F8, called twice)
    shade = trunc( AMBIENT + DIFFUSE * dot(L,N) / (|L| * 127.0) )
    shade = 0 if shade < 0 else 255 if shade > 255 else shade

The three constants come out of resident .data and are never written anywhere in the
ROM (whole-ROM xref scan: five reads, zero writes):

    RAM 0x800979A0 / ROM 0x00985A0  41800000  AMBIENT =  16.0
    RAM 0x800979A4 / ROM 0x00985A4  437A0000  DIFFUSE = 250.0
    RAM 0x800979A8 / ROM 0x00985A8  C2800000  L.x     = -64.0
    RAM 0x800979AC / ROM 0x00985AC  42800000  L.y     = +64.0
    RAM 0x800979B0 / ROM 0x00985B0  42800000  L.z     = +64.0
    RAM 0x800979B4 / ROM 0x00985B4  43480000  CM_GAIN = 200.0

Because |N| is forced to 127, dot(L,N)/(|L|*127) is exactly cos(theta), so the
formula is simply  shade = clamp(16 + 250*cos(theta)).  Flat ground gives
cos = 1/sqrt(3) and shade = 160 (0.627 of full brightness).

Axes: X = map column (world +X), Y = map row (world +Z), height = world +Y (up).
L therefore points up, to the WEST (-X) and to the SOUTH (+Z / increasing map row).

THE CM<SCEN>.IMG TINT (colour pass, RAM 0x801F4794 / ROM 0x196584)
------------------------------------------------------------------
Indexed with exactly the same corner order as the heightmap: cm[(cellY+dy)*65 +
(cellX+dx)], u16 big-endian RGBA5551. The code reads R=(v>>11)&31, G=(v>>6)&31,
B=(v>>1)&31 and IGNORES the alpha bit. The final F3D Vtx colour is

    lit    = shade * vtx[7] / 255      (vtx[7] = per-corner shroud/detail byte,
                                        255 when the detail flag 0x80097A04 is 0)
    cn.r   = (lit * R5) * 200.0 / 32768        <- 0..48, NOT 0..255
    cn.g   = (lit * G5) * 200.0 / 32768
    cn.b   = (lit * B5) * 200.0 / 32768
    cn.a   = lit

and the terrain's combiner is  RGB = (TEXEL0 - SHADE) * SHADE_ALPHA.  So the shade
is the LIGHTING (it arrives as SHADE ALPHA) and the CM is a small subtractive tint
on the texture (it arrives as SHADE RGB).  See terrain_light_notes.md.

USAGE
-----
    python3 terrain_shade.py [SCEN] [--rom PATH] [--json OUT.json] [--png OUT.png]
    (defaults: SCG01EA, ../../data/rom/cnc_eu.z64, terrain_shade_<SCEN>.json/.png)
"""
import json, math, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import archive

DEFAULT_ROM = os.path.join(HERE, "..", "..", "data", "rom", "cnc_eu.z64")

# --- the cartridge's own constants, with provenance -------------------------------
AMBIENT = 16.0                                    # RAM 0x800979A0 / ROM 0x00985A0
DIFFUSE = 250.0                                   # RAM 0x800979A4 / ROM 0x00985A4
LIGHT = (-64.0, 64.0, 64.0)                       # RAM 0x800979A8/AC/B0
NORMAL_LEN = 127.0                                # RAM 0x801C881C (gterrain rodata)
CELL = 256.0                                      # RAM 0x801C8804.. (+-256 pairs)
HEIGHT_SCALE = 4.0                                # `sll v0,v0,2` in the builder
CM_GAIN = 200.0                                   # RAM 0x800979B4 / ROM 0x00985B4
CM_SHIFT = 1.0 / 32768.0                          # RAM 0x801C87EC / F4 / FC

CONSTANTS_PROVENANCE = {
    "AMBIENT":    {"value": AMBIENT, "ram": "0x800979A0", "rom": "0x00985A0", "bytes": "41800000"},
    "DIFFUSE":    {"value": DIFFUSE, "ram": "0x800979A4", "rom": "0x00985A4", "bytes": "437A0000"},
    "LIGHT_X":    {"value": LIGHT[0], "ram": "0x800979A8", "rom": "0x00985A8", "bytes": "C2800000"},
    "LIGHT_Y":    {"value": LIGHT[1], "ram": "0x800979AC", "rom": "0x00985AC", "bytes": "42800000"},
    "LIGHT_Z":    {"value": LIGHT[2], "ram": "0x800979B0", "rom": "0x00985B0", "bytes": "42800000"},
    "NORMAL_LEN": {"value": NORMAL_LEN, "ram": "0x801C881C", "rom": "0x016A60C", "bytes": "42FE0000"},
    "CM_GAIN":    {"value": CM_GAIN, "ram": "0x800979B4", "rom": "0x00985B4", "bytes": "43480000"},
    "CM_SHIFT":   {"value": CM_SHIFT, "ram": "0x801C87EC", "rom": "0x016A5DC", "bytes": "38000000"},
}


# --- ROM access -------------------------------------------------------------------
def rom_files(rom_path, names):
    d = archive.load(rom_path)
    want = {n.upper() for n in names}
    out = {}
    for a in archive.all_archives(d):
        for r in a["recs"]:
            n = r["name"].upper()
            if n in want and n not in out and r["usize"]:
                out[n] = archive.unpack(d, r, a["base"])
    return out


def _img_payload(blob, name, want_bpp):
    hdr, _body, w, h = struct.unpack_from(">IIHH", blob, 0)
    fmt, depth = blob[12], blob[13]
    if (w, h) != (65, 65):
        raise SystemExit(f"{name}: expected 65x65, got {w}x{h}")
    n = 65 * 65 * (want_bpp // 8)
    if len(blob) < hdr + n:
        raise SystemExit(f"{name}: short ({len(blob)} bytes, need {hdr + n})")
    return blob[hdr:hdr + n], dict(w=w, h=h, fmt=fmt, depth=depth, hdr=hdr, size=len(blob))


def load_maps(rom_path, scen):
    """Return (heights[65][65], cm[65][65] u16, meta) following the engine's fallbacks."""
    scen = scen.upper()
    # the engine builds the colour-map name by overwriting the first two characters
    # of "<scen>.img" with 'c','m'  ->  SCG01EA.IMG  =>  cmG01EA.img
    cm_name = ("CM" + scen[2:] + ".IMG") if len(scen) > 2 else "CMFLAT.IMG"
    got = rom_files(rom_path, [f"{scen}.IMG", "FLAT.IMG", cm_name, "CMFLAT.IMG"])

    hname = f"{scen}.IMG" if f"{scen}.IMG" in got else "FLAT.IMG"
    cname = cm_name if cm_name in got else "CMFLAT.IMG"
    hpay, hmeta = _img_payload(got[hname], hname, 8)
    cpay, cmeta = _img_payload(got[cname], cname, 16)

    heights = [[hpay[y * 65 + x] for x in range(65)] for y in range(65)]
    cm = [[struct.unpack_from(">H", cpay, (y * 65 + x) * 2)[0] for x in range(65)]
          for y in range(65)]
    meta = {"height_file": hname, "height_meta": hmeta,
            "colour_file": cname, "colour_meta": cmeta}
    return heights, cm, meta


# --- the console's shade ----------------------------------------------------------
def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def corner_shade(heights, X, Y):
    """Exactly what gterrain.c's vertex builder stores in Vtx byte +6."""
    def H(x, y):
        return heights[y][x] * HEIGHT_SCALE

    h0 = H(X, Y)
    n = [0.0, 0.0, 0.0]
    quads = []
    if X > 0 and Y > 0:
        quads.append(((0.0, H(X, Y - 1) - h0, -CELL), (-CELL, H(X - 1, Y) - h0, 0.0)))
    if X > 0 and Y < 64:
        quads.append(((-CELL, H(X - 1, Y) - h0, 0.0), (0.0, H(X, Y + 1) - h0, CELL)))
    if X < 64 and Y > 0:
        quads.append(((CELL, H(X + 1, Y) - h0, 0.0), (0.0, H(X, Y - 1) - h0, -CELL)))
    if X < 64 and Y < 64:
        quads.append(((0.0, H(X, Y + 1) - h0, CELL), (CELL, H(X + 1, Y) - h0, 0.0)))
    for a, b in quads:
        c = _cross(a, b)
        n[0] += c[0]; n[1] += c[1]; n[2] += c[2]

    ln = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
    if ln != 0.0:                       # fn 0x8007B0F8 leaves a zero vector alone
        s = NORMAL_LEN / ln
        n = [n[0] * s, n[1] * s, n[2] * s]

    dot = LIGHT[0] * n[0] + LIGHT[1] * n[1] + LIGHT[2] * n[2]
    llen = math.sqrt(sum(c * c for c in LIGHT))
    v = AMBIENT + DIFFUSE * dot / (llen * NORMAL_LEN)
    v = int(v)                          # trunc.w.s (toward zero)
    return 0 if v < 0 else (255 if v > 255 else v)


def shade_grid(heights):
    return [[corner_shade(heights, x, y) for x in range(65)] for y in range(65)]


def cm_unpack(v):
    """The colour pass' own unpack: R=v>>11, G=(v>>6)&31, B=(v>>1)&31. Alpha unread."""
    return ((v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31, v & 1)


def vertex_colour(shade, cmv, fog=255):
    """The final F3D Vtx cn[4] the cartridge writes (colour pass ROM 0x196584)."""
    lit = (shade * fog) // 255
    r5, g5, b5, _a = cm_unpack(cmv)
    out = []
    for c5 in (r5, g5, b5):
        out.append(int(float(lit * c5) * CM_GAIN * CM_SHIFT) & 0xFF)
    return (out[0], out[1], out[2], lit)


# --- PNG visualisation ------------------------------------------------------------
def write_png(path, shade, cm, scale=8):
    try:
        from PIL import Image
    except ImportError:
        print("  (PIL not available -- skipping PNG)")
        return None
    w = 65
    lit = Image.new("RGB", (w, w))
    tint = Image.new("RGB", (w, w))
    final = Image.new("RGB", (w, w))
    lp, tp, fp = lit.load(), tint.load(), final.load()
    for y in range(w):
        for x in range(w):
            s = shade[y][x]
            lp[x, y] = (s, s, s)
            r, g, b, a = vertex_colour(s, cm[y][x])
            # CM tint shown at 5x so the sparse warm dots are visible at all
            tp[x, y] = (min(255, r * 5), min(255, g * 5), min(255, b * 5))
            # what a mid-grey texel (160,160,160) becomes under the terrain combiner:
            #   out = (TEXEL0 - SHADE_RGB) * SHADE_ALPHA
            t = 160
            fp[x, y] = tuple(max(0, min(255, (t - c) * a // 255)) for c in (r, g, b))
    pad = 6
    sheet = Image.new("RGB", (w * scale * 3 + pad * 4, w * scale + pad * 2), (24, 24, 28))
    for i, im in enumerate((lit, tint, final)):
        sheet.paste(im.resize((w * scale, w * scale), Image.NEAREST),
                    (pad + i * (w * scale + pad), pad))
    sheet.save(path)
    return path


def main(argv):
    scen = "SCG01EA"
    rom = DEFAULT_ROM
    outj = outp = None
    args = list(argv)
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--rom": rom = args[i + 1]; i += 2
        elif a == "--json": outj = args[i + 1]; i += 2
        elif a == "--png": outp = args[i + 1]; i += 2
        elif a.startswith("-"): raise SystemExit(__doc__)
        else: scen = a.upper(); i += 1
    outj = outj or os.path.join(HERE, f"terrain_shade_{scen}.json")
    outp = outp or os.path.join(HERE, f"terrain_shade_{scen}.png")

    heights, cm, meta = load_maps(rom, scen)
    shade = shade_grid(heights)

    flat = [shade[y][x] for y in range(1, 64) for x in range(1, 64)
            if all(heights[y + dy][x + dx] == heights[y][x]
                   for dy in (-1, 0, 1) for dx in (-1, 0, 1))]
    vals = [v for row in shade for v in row]
    nonblack = sum(1 for row in cm for v in row if (v >> 1) & 0x7FFF)

    doc = {
        "scenario": scen,
        "source": meta,
        "grid": "corner",
        "width": 65, "height": 65,
        "order": "shade[y][x], y = map row (world +Z), x = map column (world +X); "
                 "identical indexing to the heightmap",
        "constants": CONSTANTS_PROVENANCE,
        "formula": "shade = clamp(trunc(AMBIENT + DIFFUSE * cos(theta)), 0, 255), "
                   "theta = angle between L=(-64,64,64) and the accumulated "
                   "quadrant-face normal (length forced to 127)",
        "shade": shade,
        "cm_rgba5551": cm,
        "cm_note": "u16 BE; the colour pass reads R=v>>11, G=(v>>6)&31, B=(v>>1)&31 "
                   "and ignores the alpha bit",
        "vertex_colour_example": {
            "note": "final F3D Vtx cn[4] = (r,g,b,a) at fog byte 255",
            "cells": {f"{x},{y}": vertex_colour(shade[y][x], cm[y][x])
                      for (x, y) in ((0, 0), (32, 32), (40, 40), (47, 52), (64, 64))},
        },
        "stats": {
            "shade_min": min(vals), "shade_max": max(vals),
            "shade_mean": round(sum(vals) / len(vals), 3),
            "flat_corner_count": len(flat),
            "flat_corner_shade": sorted(set(flat)),
            "cm_nonblack_corners": nonblack,
        },
    }
    with open(outj, "w") as f:
        json.dump(doc, f, separators=(",", ":"))
    print(f"{scen}: heights={meta['height_file']} colour={meta['colour_file']}")
    print(f"  shade min/mean/max = {doc['stats']['shade_min']}/"
          f"{doc['stats']['shade_mean']}/{doc['stats']['shade_max']}")
    print(f"  flat corners: {len(flat)} -> shade {sorted(set(flat))}")
    print(f"  CM non-black corners: {nonblack} of 4225")
    print(f"  wrote {outj}")
    p = write_png(outp, shade, cm)
    if p:
        print(f"  wrote {p}  (left: shade | middle: CM tint x5 | right: a "
              f"160-grey texel through the terrain combiner)")


if __name__ == "__main__":
    main(sys.argv[1:])
