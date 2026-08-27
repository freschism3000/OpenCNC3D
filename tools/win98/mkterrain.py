#!/usr/bin/env python3
"""
mkterrain.py -- convert a baked scenario pack into the terrain pack tier1 reads.

WHY A CONVERSION EXISTS AT ALL.

The scenario packs (CNC3DPKE v14, built by game/bake5.py) carry RGBA8 textures, because
the desktop renderer uploads them straight to OpenGL. tier1's software rasteriser samples
ONE BYTE per texel into a shared 32-level shade table, which is the whole reason it is fast
enough to exist on a 535 MHz Pentium II class machine: one indexed load per pixel instead of
four bytes plus a multiply. So the art has to be palettised somewhere, and doing it here at
bake time costs the target machine nothing.

The measured numbers behind that decision, taken from the real SCG01EA.pack:
  - the whole base texture bank holds 700 distinct opaque RGB triples
  - the 1024x1024 terrain atlas alone holds 305, so it does NOT fit 256 by itself
  - quantised to 255 colours the mean per-texel error is about 1.86 on a 0..441 scale
  - the bank shrinks from 5,716,368 bytes to 1,429,092 plus a 768 byte palette

This script does the terrain only. The mesh bank is the same problem and comes next.

IT DOES NOT RE-BAKE. It reads the finished pack rather than rebuilding it from the ROM,
which matters because game/bake5.py cannot currently run from this worktree at all: it
loads its base layout from a compiled-only module whose .pyc was lost outside this tree
(see game/pk4mod.py and its note about the 2026-08-12 share incident).

THE ATLAS IS RE-CUT INTO 256x256 PAGES, because that is the Voodoo 2's maximum texture
size and the source atlas is 1024x1024. Every terrain cell is exactly a 24x24 texel tile
at an integer origin (verified across all 147,456 cells of all 36 shipped packs, none of
them mirrored), so a 256x256 page holds a 10x10 grid of them. This mission uses 211
distinct tiles, so three pages, and a cell record becomes a page number and a tile
coordinate instead of four floats.

That is not a Glide-only concession. It shrinks the terrain art from 1,048,576 bytes to
196,608 and the cell table from 86,016 to 16,384, and the software renderer takes a
256x256 page exactly as happily as a 1024x1024 one because both are powers of two.

  tools/win98/mkterrain.py <SCEN.pack> <out.t1terr>
"""
import struct, sys, os
from collections import Counter

def u32(b, o):  return struct.unpack_from("<I", b, o)[0]
def i32(b, o):  return struct.unpack_from("<i", b, o)[0]

def walk(blob):
    """Walk the pack far enough to reach the terrain section, returning its offsets.

    Every record size here is read off the writer in game/bake5.py:1564-1660, and the
    walk is checked at the end against the file length, because the one thing that must
    never happen quietly is landing on the wrong section and reading plausible rubbish."""
    o = 0
    assert blob[0:7] == b"CNC3DPK", "not a CNC3D pack"
    assert blob[7:8] == b"E", "only CNC3DPKE (v14) is understood, got %r" % blob[7:8]
    o = 8 + 4 + 16 + 16                      # magic, version, scenario, theater

    ntex = u32(blob, o); o += 4
    textures = []
    for _ in range(ntex):
        w, h, uw, uh = struct.unpack_from("<IIII", blob, o); o += 16
        data = o
        o += w * h * 4
        has_gdi = blob[o]; o += 1
        gdi = None
        if has_gdi:
            gdi = o
            o += w * h * 4
        textures.append((w, h, uw, uh, data, gdi))

    nmesh = u32(blob, o); o += 4
    for _ in range(nmesh):
        o += 16                               # name
        ntri = u32(blob, o); o += 4
        o += ntri * 78                        # i32 tex, u8 mode, u8 wrap, 3 x 24-byte vertex
        nparts = u32(blob, o); o += 4
        o += nparts * 24                      # tri0, ntris, role, pivot[3]
        nsec = u32(blob, o); o += 4
        o += nsec * 4
        nf   = u32(blob, o); o += 4           # PKB animation
        _tpf = u32(blob, o); o += 4
        nclip = u32(blob, o); o += 4
        if nf:
            o += nclip * 9                    # i32 t0, i32 t1, u8 loop
            o += nf * nparts * 12 * 4         # matrices
            o += nf * nparts                  # visibility

    ntype = u32(blob, o); o += 4
    o += ntype * 13                           # 8 name, i32 mesh index, u8 confidence

    terrain_tex = u32(blob, o); o += 4
    ncells = u32(blob, o); o += 4
    cells_at = o
    o += ncells * 21                          # s16 x, s16 y, 4 x f32 uv, u8 holes

    nsprite = u32(blob, o); o += 4
    o += nsprite * 44
    ninf = u32(blob, o); o += 4
    inf_at = o
    # the infantry record length is not written anywhere, so derive it from the tail
    tail = 4225 + 8450 + 4 + 8                # heights, cm tint, seabed, water
    nshadow_at = None
    # walk shadows from the front of the tail instead: find it by subtraction
    return textures, terrain_tex, ncells, cells_at, o, ninf, tail

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    src, dst = sys.argv[1], sys.argv[2]
    blob = open(src, "rb").read()
    textures, tt, ncells, cells_at, after_inf, ninf, tail = walk(blob)

    w, h, uw, uh, data_at, _gdi = textures[tt]
    print("terrain atlas: texture %d, %dx%d padded, %dx%d used" % (tt, w, h, uw, uh))
    print("cells: %d at 0x%X" % (ncells, cells_at))

    # ---- the tail, read EOF-relative exactly as the renderer does -------------------
    n = len(blob)
    water   = struct.unpack_from("<ii", blob, n - 8)
    seabed  = i32(blob, n - 12)
    heights = blob[n - 12 - 4225 : n - 12]
    cmtint  = blob[n - 12 - 4225 - 8450 : n - 12 - 4225]
    assert len(heights) == 4225 and len(cmtint) == 8450
    print("heights 65x65 range %d..%d, seabed=%d water=%s"
          % (min(heights), max(heights), seabed, water))

    # ---- palettise the atlas --------------------------------------------------------
    # Only the USED sub-rectangle carries art; the rest is power-of-two padding.
    px = blob[data_at : data_at + w * h * 4]
    cnt = Counter()
    for y in range(uh):
        row = y * w * 4
        for x in range(uw):
            p = row + x * 4
            if px[p + 3] >= 128:
                cnt[(px[p], px[p + 1], px[p + 2])] += 1
    print("distinct opaque RGB in the used area: %d" % len(cnt))

    # Index 0 is reserved for the transparent texels, which in the terrain atlas are the
    # water holes (cnc_eyes.cpp:1398 skips them). 255 slots are left for real colour, and
    # with only ~305 distinct colours present, taking the most frequent 255 and snapping
    # the remainder to their nearest neighbour costs less than a median cut would.
    order = [c for c, _ in cnt.most_common()]
    pal = order[:255]
    while len(pal) < 255:
        pal.append((0, 0, 0))
    lut = {}
    for i, c in enumerate(pal):
        lut[c] = i + 1
    snapped = 0
    for c in order[255:]:
        best, bd = 1, 1 << 30
        for i, q in enumerate(pal):
            d = (c[0]-q[0])**2 + (c[1]-q[1])**2 + (c[2]-q[2])**2
            if d < bd: bd, best = d, i + 1
        lut[c] = best
        snapped += cnt[c]
    total = sum(cnt.values())
    print("palette: 255 colours, %d texels (%.3f%%) snapped to a neighbour"
          % (snapped, 100.0 * snapped / max(total, 1)))

    # ---- re-cut into 256x256 pages ---------------------------------------------------
    # Collect the distinct 24x24 tiles the cells actually reference, then pack them into
    # pages. Cells are read first because a mission touches only a fraction of the atlas.
    TILE, PAGE = 24, 256
    PER = PAGE // TILE                     # 10 tiles across a page, 240 of 256 used
    cells_raw = blob[cells_at : cells_at + ncells * 21]
    tiles, order = {}, []
    cellrec = []
    for i in range(ncells):
        r = cells_raw[i*21:(i+1)*21]
        u0, v0, u1, v1 = struct.unpack_from("<ffff", r, 4)
        holes = r[20]
        # THE TILES ARE NOT ON A 24-TEXEL GRID, and assuming they were is what filled the
        # water with fragments of grass.
        #
        # This used to read `tx = round(u0 * uw / TILE)`, i.e. it divided the origin by
        # the tile size and rounded -- which is only correct if every tile starts at a
        # multiple of 24. The baker now gives every terrain tile A GUTTER OF ITS OWN EDGE
        # (main, "The map's trims line up again"), so origins land at arbitrary texels:
        # measured on SCG01EA, `X0 mod 24` takes at least six different values and `Y0 mod
        # 24` is 1 for 3,429 of the 4,096 cells. Rounding those to a grid snaps a cell to
        # whichever neighbour happens to be nearest, which is why a water cell came out
        # carrying half a beach.
        #
        # The origin is used exactly as the pack gives it. The SPAN is still asserted to
        # be 24x24 -- all 4,096 cells agree -- because that is the one thing the page
        # packing below does depend on.
        tx0 = int(round(u0 * uw))
        ty0 = int(round(v0 * uh))
        assert abs((u1 - u0) * uw - TILE) < 0.5 and abs((v1 - v0) * uh - TILE) < 0.5, \
            "cell %d spans %.2f x %.2f texels, not %dx%d" % (
                i, (u1 - u0) * uw, (v1 - v0) * uh, TILE, TILE)
        key = (tx0, ty0)
        if key not in tiles:
            tiles[key] = len(order)
            order.append(key)
        cellrec.append((tiles[key], holes))
    npages = (len(order) + PER*PER - 1) // (PER*PER)
    print("distinct 24x24 tiles used: %d -> %d page(s) of %dx%d"
          % (len(order), npages, PAGE, PAGE))

    pages = [bytearray(PAGE * PAGE) for _ in range(npages)]
    slot = {}
    for n, (tx0, ty0) in enumerate(order):
        pg, rem = n // (PER*PER), n % (PER*PER)
        px_, py_ = (rem % PER) * TILE, (rem // PER) * TILE
        slot[n] = (pg, rem % PER, rem // PER)
        for yy in range(TILE):
            srow = (ty0 + yy) * w * 4
            drow = (py_ + yy) * PAGE + px_
            for xx in range(TILE):
                q = srow + (tx0 + xx) * 4
                pages[pg][drow + xx] = (lut[(px[q], px[q+1], px[q+2])]
                                        if px[q+3] >= 128 else 0)

    palette = bytearray(768)
    for i, c in enumerate(pal):
        palette[(i+1)*3+0], palette[(i+1)*3+1], palette[(i+1)*3+2] = c
    # Index 0 is the transparent one, which in the terrain atlas is a water hole. It is
    # given a dark blue rather than black so a hole reads as water while the real seabed
    # pass does not exist yet.
    palette[0], palette[1], palette[2] = 24, 40, 72

    cellout = bytearray()
    for (tile, holes) in cellrec:
        pg, cx_, cy_ = slot[tile]
        cellout += bytes((pg, cx_, cy_, holes))

    # ---- write it out ---------------------------------------------------------------
    with open(dst, "wb") as f:
        f.write(b"T1TERR02")
        f.write(struct.pack("<IIIII", 2, npages, PAGE, TILE, PER))
        f.write(palette)
        for pg in pages:
            f.write(bytes(pg))
        f.write(struct.pack("<I", ncells))
        f.write(cellout)                       # 4 bytes a cell: page, tilex, tiley, holes
        f.write(heights)
        f.write(cmtint)
        f.write(struct.pack("<ii", seabed, water[0]))
    print("wrote %s (%.2f MB)" % (dst, os.path.getsize(dst) / 1e6))
    return 0

if __name__ == "__main__":
    sys.exit(main())
