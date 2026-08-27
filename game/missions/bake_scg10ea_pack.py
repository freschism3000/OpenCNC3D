#!/usr/bin/env python3
"""Rebake SCG01EA.pack's per-cell terrain table for the SCG10EA flume map.

The pack's terrain is a per-SCENARIO bake: one atlas texture plus a cell table of
(x, y, u0, v0, u1, v1, holes) entries, one per playable cell (cnc_eyes.cpp load_pack).
Running SCG10EA against SCG01EA.pack therefore draws GDI-1's ground under the flume.
The engine does not care (terrain lives in the DLL, from the .BIN), but the screenshots
would lie about the water. This tool rewrites ONLY the cell table:

  - every cell of SCG10EA.BIN that is W1 water   -> the uv of a known W1 cell of
    SCG01EA (its atlas patch is a full alpha water hole; draw_water paints it blue)
  - every clear cell                              -> the uv of a known clear cell

Everything else in the pack (textures, meshes incl. BOAT, sprites, infantry strips,
type map) is byte-identical. Output: SCG10EA.pack with its scen field renamed.

Pack layout (from cnc_eyes.cpp load_pack, CNC3DPK4 / CNC3DPK5):
  8s magic | u32 ver | 16s scen | 16s theater
  u32 ntex   { u32 w,h,uw,uh ; w*h*4 RGBA }
  u32 nmesh  { 16s name ; u32 ntri { i32 tex ; u8 mode ; u8 wrap ;
               3 x (5f x,y,z,u,v + 4B rgba) }
               PK5 only: u32 nparts { u32 tri0 ; u32 ntris ; u32 role ;
               3f pivot } }
  u32 ntype  { 8s code ; i32 mesh ; u8 conf }
  u32 terrainTex
  u32 ncell  { i16 x ; i16 y ; 4f u0,v0,u1,v1 ; u8 holes }
  u32 nsprite{ 16s name ; 7 x u32 }
  u32 ninf   { 8s code ; 4B set ; SPRH*SPRANIM x i32 }

r030 assembler adaptation: the turret round moved the shipped packs to CNC3DPK5
(per-part submesh table appended to every mesh record). This tool only splices
the CELL TABLE, everything else is copied verbatim, so PK5 support is purely a
matter of walking past the new part table when locating the cell table. Both
magics are accepted; the output keeps the donor's magic and version.
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)

W1 = 0x01
CLEAR = 0xFF
MAPX, MAPY, MAPW, MAPH = 4, 4, 56, 56


def read_bin(path):
    """-> (bytes, grid width). 2 bytes per cell, square grid: 8192 bytes =
    64x64 (legacy), 32768 = 128x128 (Version=1)."""
    b = open(path, 'rb').read()
    n, rem = divmod(len(b), 2)
    w = math.isqrt(n)
    if rem or w * w != n:
        raise SystemExit("%s is %d bytes; a square .BIN is 2*W*W bytes "
                         "(8192 for 64x64, 32768 for 128x128)" % (path, len(b)))
    return b, w


def main():
    src = os.path.join(GAME, 'SCG01EA.pack')
    ea_bin, ea_w = read_bin(os.path.join(HERE, 'SCG01EA.BIN'))
    my_bin, my_w = read_bin(os.path.join(HERE, 'SCG10EA.BIN'))
    p = open(src, 'rb').read()
    o = 0

    def take(n):
        nonlocal o
        v = p[o:o + n]
        o += n
        return v

    def u32():
        return struct.unpack('<I', take(4))[0]

    magic = take(8)
    assert magic in (b'CNC3DPK4', b'CNC3DPK5', b'CNC3DPK6', b'CNC3DPK7'), magic
    pk5 = magic >= b'CNC3DPK5'
    pk6 = magic >= b'CNC3DPK6'
    pk7 = magic >= b'CNC3DPK7'
    ver = take(4)
    scen = take(16)
    theater = take(16)

    ntex = u32()
    for _ in range(ntex):
        w, h, uw, uh = struct.unpack('<4I', take(16))
        take(w * h * 4)
        if pk6:
            # PK6: u8 has_gdi + optional second pixel block (the GDI house variant)
            has = take(1)
            if has == b'\x01':
                take(w * h * 4)

    nmesh = u32()
    for _ in range(nmesh):
        take(16)
        ntri = u32()
        take(ntri * (4 + 1 + 1 + 3 * (5 * 4 + 4)))
        if pk5:
            nparts = u32()
            take(nparts * (4 + 4 + 4 + 3 * 4))
        if pk7:
            # PK7: the construction section table (u32 count + u32 tri0 each)
            nsec = u32()
            take(nsec * 4)

    ntype = u32()
    take(ntype * (8 + 4 + 1))

    terrain_tex = u32()
    ncell = u32()
    cell_off = o                       # first byte of the cell table
    CELLSZ = 2 + 2 + 16 + 1
    cells = {}
    for _ in range(ncell):
        x, y = struct.unpack('<2h', take(4))
        u0, v0, u1, v1 = struct.unpack('<4f', take(16))
        holes = take(1)[0]
        cells[(x, y)] = (u0, v0, u1, v1, holes)
    tail_off = o                       # sprites + infantry, kept verbatim

    # ---- pick donor uv rects out of the EA bake ----
    def ea_tmpl(x, y):
        return ea_bin[2 * (y * ea_w + x)]

    donor_water = donor_clear = None
    for (x, y), rec in cells.items():
        t = ea_tmpl(x, y)
        if t == W1 and donor_water is None:
            donor_water = rec
        if t == CLEAR and donor_clear is None:
            donor_clear = rec
        if donor_water and donor_clear:
            break
    if donor_water is None:
        # fall back to any fully-holed cell (an all-water patch of a bigger template)
        for rec in cells.values():
            if rec[4]:
                donor_water = rec
                break
    assert donor_water and donor_clear, 'no donor cells found in SCG01EA bake'
    print('water donor uv %s holes=%d' % (donor_water[:4], donor_water[4]))
    print('clear donor uv %s holes=%d' % (donor_clear[:4], donor_clear[4]))

    # ---- build the new cell table straight off SCG10EA.BIN ----
    # The EA bake carries an entry for every cell it had (including off-window spill);
    # emit the same FOOTPRINT (the source table's x,y set) so nothing appears or
    # disappears under the renderer's own cull.
    xs = sorted(set(x for (x, _) in cells))
    ys = sorted(set(y for (_, y) in cells))
    print('source cell window x=%d..%d y=%d..%d (%d cells)'
          % (xs[0], xs[-1], ys[0], ys[-1], len(cells)))
    out_cells = []
    nwater = 0
    for (x, y) in sorted(cells.keys(), key=lambda c: (c[1], c[0])):
        t = my_bin[2 * (y * my_w + x)] if 0 <= x < my_w and 0 <= y < my_w else CLEAR
        rec = donor_water if t == W1 else donor_clear
        if t == W1:
            nwater += 1
        out_cells.append(struct.pack('<2h4fB', x, y, *rec))
    print('cells: %d (%d water)' % (len(out_cells), nwater))

    newscen = b'SCG10EA' + b'\x00' * 9
    out = (magic + ver + newscen + theater
           + p[8 + 4 + 32:cell_off - 8]              # everything up to terrainTex+ncell
           + struct.pack('<II', terrain_tex, len(out_cells))
           + b''.join(out_cells)
           + p[tail_off:])
    dst = os.path.join(GAME, 'SCG10EA.pack')
    open(dst, 'wb').write(out)
    print('wrote %s (%d bytes; source %d)' % (dst, len(out), len(p)))


if __name__ == '__main__':
    sys.exit(main())
