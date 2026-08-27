#!/usr/bin/env python3
"""
bake_dostiberium.py -- bake the REAL tiberium overlay art into one little-endian
pack, `dostib.pack`, so the renderer can stop drawing procedural triangles.

TWO SOURCES, ONE CONTAINER. Both are the original media, both are decoded (not
guessed), and both produce the identical pack layout so the renderer wiring is
written once:

  --source n64  (DEFAULT, and the recommendation)
        The CARTRIDGE's own tiberium: ANY_TI01..ANY_TI12.IMG out of
        data/rom/cnc_eu.z64. Each is a 288x24 colour-indexed .IMG (fmt 2,
        4bpp, 16-entry TLUT) = a 12-frame filmstrip of 24x24 tiles. The
        "ANY_" prefix is the cartridge's own statement that this single set
        covers every theater; the ROM holds no theater variants.
        NOTE (correcting an earlier note): these files are NOT in a second,
        undecoded compressed IMG family. They are ordinary archive method
        0x22 entries; tools/romdump/decomp.py decodes all twelve to exactly
        their recorded 0xDB0 uncompressed size, and tools/romdump/img.py's
        standard 16-byte header solves them without a special case. The
        earlier "undecoded" reading came from inspecting the RAW (still
        LZ-compressed) bytes that archive.py's `extract` writes.

  --source dos
        The 1995 MS-DOS theater art: TI1..TI12 SHP in TEMPERAT.MIX /
        DESERT.MIX / WINTER.MIX, three theater sets, 12 frames of 24x24 each.
        Needs the CD mounted (--cd /Volumes/GDI); the repo's data/dosdata does
        NOT carry the theater mixes.

WHAT THE ENGINE SAYS (citations, all under brain/vanilla/tiberiandawn):

  * WHICH SHAPE   -- odata.cpp:162,178,...,338 name the twelve overlays
    "TI1".."TI12"; odata.cpp:858..869 OverlayTypeClass::Init appends the
    theater suffix (const.cpp:267 {"TEMPERATE","TEMPERAT","TEM"}) because
    every tiberium overlay declares IsTheater = true.
    The brain's TIB| export (dllinterface.cpp:8059) sends
    kind = Overlay - OVERLAY_TIBERIUM1, so kind 0..11 == TI1..TI12.

  * WHICH FRAME   -- cell.cpp:1041..1050 CellClass::Draw_It:
        CC_Draw_Shape(otype.Get_Image_Data(),
                      OverlayData,                       <-- the frame index
                      (x + (CELL_PIXEL_W >> 1)),
                      (y + (CELL_PIXEL_H >> 1)),
                      WINDOW_TACTICAL,
                      SHAPE_CENTER | SHAPE_WIN_REL | SHAPE_GHOST,
                      NULL, Map.UnitShadow);
    The frame IS OverlayData, verbatim, no arithmetic. The brain's TIB| line
    sends OverlayData as `stage`, so: frame = stage.

  * WHAT OverlayData MEANS -- cell.cpp:1907..1935 CellClass::Tiberium_Adjust:
        static int _adj[9] = {0, 1, 3, 4, 6, 7, 8, 10, 11};
        ... count = number of the 8 adjacent cells that are also tiberium ...
        OverlayData = _adj[count];
    so it is a DENSITY index in 0..11, and harvesting drives it down
    (cell.cpp:2037/2052 clear it). 12 frames per shape, one per value: a
    denser neighbourhood draws a fuller clump. Verified: every TI SHP in all
    three theaters has exactly 12 frames of exactly 24x24.

  * WORLD SCALE   -- display.h:41 ICON_PIXEL_W 24, display.h:45
    CELL_PIXEL_W == ICON_PIXEL_W. The art is 24x24 = exactly one cell, drawn
    SHAPE_CENTER on the cell centre, i.e. a 1:1 ground decal over the cell
    quad [x,x+1] x [z,z+1]. The renderer's terrain atlas is also 24 texels
    per cell, so this is native scale with no resampling.

  * TRANSPARENCY  -- DOS: palette index 0 is the shape's transparent colour
    and index 4 is the DOS shadow ("ghost") colour, the same rule
    bake_dosinfantry.py and bake_dosmake.py already apply; both become
    alpha 0 here (index 4 occurs 31 times in the whole 144-frame temperate
    set, so nothing of substance is lost).
    N64: the TLUT itself carries the RGBA5551 alpha bit; the transparent
    slot is NOT always index 0 (it is 8 in ANY_TI01, 7 in ANY_TI08, 0 in
    ANY_TI07/11, 1 in the rest), so the baker reads the alpha bit rather
    than assuming a slot.

THE DECISION: SHIP THE CARTRIDGE ART (--source n64 is the default).
  1. The N64 presentation is this project's stated target, and this is the console's
     own tiberium, not a stand-in for it.
  2. It costs nothing in fidelity to 1995: --verify measures 99.96% mask agreement
     against the original CD art, and every one of the 31 disagreeing pixels is the DOS
     shadow colour that this project drops to alpha 0 everywhere else. 0 unexplained.
  3. It is reproducible from the repo alone (data/rom/cnc_eu.z64 is committed). The DOS
     path needs TEMPERAT/DESERT/WINTER.MIX off CD-1, which data/dosdata does NOT carry,
     so a DOS-sourced pack could not be rebuilt by CI or by a fresh clone.
  4. One "ANY" set covers every theater, so the renderer needs no theater selector.
  The DOS path stays supported and tested (--source dos writes the same container with
  three sets), so restoring the 1995 look is a flag, not a rewrite.

PROOF THAT THE TWO SOURCES ARE THE SAME ART (run --verify):
  For all 12 shapes x 12 frames, the N64 tile's transparency mask equals the
  DOS frame's transparency mask on 82944/82944 pixels. Tile t is DOS frame t
  (ties only where consecutive DOS frames share a silhouette). So the frame
  rule above holds unchanged for the cartridge art.

VOODOO 2 FIT:
  Frames are 24x24. 144 frames (12 shapes x 12 frames) are packed into
  256x256 sheets as a 10 x 10 grid of 24x24 slots (240x240 used), so a set
  needs 2 sheets. 256x256 is the Voodoo 2 maximum, both dimensions are powers
  of two, and every UV lands on a texel centre under GL_NEAREST, so GL_CLAMP
  is safe and no sample ever crosses a slot boundary (the same argument
  dosinf_mod.h records for its own sheets). Two texture binds per frame for
  the whole tiberium pass.

FORMAT (all little-endian), magic "DOSTIB1\\0":

    char  magic[8]        "DOSTIB1\\0"
    u32   version         1
    u32   source          0 = N64 cartridge, 1 = 1995 DOS theater art
    u32   set_count       n64: 1   dos: 3
    u32   frame_w         24
    u32   frame_h         24
    u32   types           12
    u32   frames          12
    set_count x {
        char name[12]     "ANY" | "TEMPERATE" | "DESERT" | "WINTER"
        u32  sheets, texw, texh, cols, rows
        (types*frames) x { u16 sheet, u16 x0, u16 y0 }   slot = kind*frames + stage
        sheets x u8 rgba[texw*texh*4]                    straight alpha, 0 or 255
    }

USAGE
    python3 game/bake_dostiberium.py                       # n64 -> game/dostib.pack
    python3 game/bake_dostiberium.py --source dos --cd /Volumes/GDI
    python3 game/bake_dostiberium.py --proof /tmp/tib_proof.png
    python3 game/bake_dostiberium.py --verify --cd /Volumes/GDI   # cross-check only
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))
sys.path.insert(0, os.path.join(ROOT, "tools", "romdump"))

MAGIC = b"DOSTIB1\x00"
VERSION = 1
SRC_N64, SRC_DOS = 0, 1

TYPES = 12          # TI1..TI12 / ANY_TI01..ANY_TI12
FRAMES = 12         # OverlayData 0..11 (cell.cpp:1933 _adj[] tops out at 11)
FW = FH = 24        # display.h:41 ICON_PIXEL_W
TEX = 256           # Voodoo 2 maximum
COLS = TEX // FW    # 10
ROWS = TEX // FH    # 10
PER_SHEET = COLS * ROWS

DOS_TRANSPARENT, DOS_SHADOW = 0, 4

# The three theater sets, DOS side: (set name, mix file stem, shape suffix, palette).
DOS_THEATERS = [("TEMPERATE", "TEMPERAT", "TEM", "TEMPERAT.PAL"),
                ("DESERT",    "DESERT",   "DES", "DESERT.PAL"),
                ("WINTER",    "WINTER",   "WIN", "WINTER.PAL")]


# ---------------------------------------------------------------------------
# sources
# ---------------------------------------------------------------------------

def load_n64(rom_path):
    """[set] -> frames[kind][stage] = bytes(FW*FH*4). One set, named ANY."""
    import imgsqueeze                 # tools/romdump: archive tables + method 0x22 + .IMG

    rom = open(rom_path, "rb").read()
    out = []
    for kind in range(TYPES):
        name = "ANY_TI%02d.IMG" % (kind + 1)
        try:
            # imgsqueeze.find DECOMPRESSES. Never read these records straight out of the
            # ROM: they are method-0x2200 LZ streams, and mistaking those bytes for the
            # file is what invented the phantom "compressed IMG family" (see that tool's
            # header and tools/romdump/archive.py).
            blob = imgsqueeze.find(rom, name)
        except KeyError:
            raise SystemExit("ROM has no %s" % name)
        hdr, _body, w, h = struct.unpack_from(">IIHH", blob, 0)
        fmt, depth, _unk, palc = blob[12], blob[13], blob[14], blob[15]
        palc = palc or 256
        assert (hdr, w, h, fmt, depth, palc) == (16, FW * FRAMES, FH, 2, 0, 16), \
            (name, hdr, w, h, fmt, depth, palc)
        assert len(blob) == hdr + (w * h) // 2 + palc * 2, (name, len(blob))
        pix = blob[hdr:hdr + (w * h) // 2]
        tlut = struct.unpack(">%dH" % palc, blob[-palc * 2:])
        rgba = []
        for v in tlut:
            a = v & 1
            rgba.append((((v >> 11) & 31) * 255 // 31,
                         ((v >> 6) & 31) * 255 // 31,
                         ((v >> 1) & 31) * 255 // 31,
                         255 if a else 0) if a else (0, 0, 0, 0))
        row = []
        for f in range(FRAMES):
            buf = bytearray(FW * FH * 4)
            for y in range(FH):
                for x in range(FW):
                    p = y * w + f * FW + x
                    idx = (pix[p >> 1] >> 4) if (p & 1) == 0 else (pix[p >> 1] & 0xF)
                    buf[(y * FW + x) * 4:(y * FW + x) * 4 + 4] = bytes(rgba[idx])
            row.append(bytes(buf))
        out.append(row)
    return [("ANY", out)]


def load_dos(cd_dir):
    """[(setname, frames[kind][stage])] for the three theaters."""
    from mixshp import MixFile, Shape

    sets = []
    for setname, stem, suffix, palname in DOS_THEATERS:
        mixpath = os.path.join(cd_dir, stem + ".MIX")
        if not os.path.isfile(mixpath):
            raise SystemExit("no %s -- mount the 1995 CD-1 (hdiutil attach ...)" % mixpath)
        mix = MixFile(mixpath)
        pal6 = mix.read(palname)
        assert len(pal6) == 768 and max(pal6) <= 63, palname
        pal8 = [min(252, v << 2) for v in pal6]
        rows = []
        for kind in range(TYPES):
            shp = Shape(mix.read("TI%d.%s" % (kind + 1, suffix)))
            assert (shp.width, shp.height, shp.frames) == (FW, FH, FRAMES), \
                (setname, kind, shp.width, shp.height, shp.frames)
            row = []
            for f in range(FRAMES):
                src = shp.frame(f)
                buf = bytearray(FW * FH * 4)
                for p in range(FW * FH):
                    v = src[p]
                    if v in (DOS_TRANSPARENT, DOS_SHADOW):
                        continue                       # already 0,0,0,0
                    buf[p * 4:p * 4 + 4] = bytes((pal8[v * 3], pal8[v * 3 + 1],
                                                  pal8[v * 3 + 2], 255))
                row.append(bytes(buf))
            rows.append(row)
        sets.append((setname, rows))
    return sets


# ---------------------------------------------------------------------------
# sheeting + pack
# ---------------------------------------------------------------------------

def sheet_set(frames):
    """frames[kind][stage] -> (slots, sheets). Slot k*FRAMES+s at grid position."""
    n = TYPES * FRAMES
    nsheets = (n + PER_SHEET - 1) // PER_SHEET
    sheets = [bytearray(TEX * TEX * 4) for _ in range(nsheets)]
    slots = []
    for kind in range(TYPES):
        for stage in range(FRAMES):
            slot = kind * FRAMES + stage
            s, i = slot // PER_SHEET, slot % PER_SHEET
            x0, y0 = (i % COLS) * FW, (i // COLS) * FH
            src = frames[kind][stage]
            dst = sheets[s]
            for y in range(FH):
                o = ((y0 + y) * TEX + x0) * 4
                dst[o:o + FW * 4] = src[y * FW * 4:(y + 1) * FW * 4]
            slots.append((s, x0, y0))
    return slots, [bytes(s) for s in sheets]


def write_pack(path, source, sets):
    with open(path, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<7I", VERSION, source, len(sets), FW, FH, TYPES, FRAMES))
        for name, frames in sets:
            slots, sheets = sheet_set(frames)
            nb = name.encode("ascii")
            assert len(nb) <= 12, name
            fh.write(nb + b"\x00" * (12 - len(nb)))
            fh.write(struct.pack("<5I", len(sheets), TEX, TEX, COLS, ROWS))
            for s, x0, y0 in slots:
                fh.write(struct.pack("<3H", s, x0, y0))
            for s in sheets:
                fh.write(s)
    return os.path.getsize(path)


# ---------------------------------------------------------------------------
# proof + verification
# ---------------------------------------------------------------------------

def proof_png(path, sets, scale=5):
    """LOOK AT IT: rows = the 12 shapes, columns = OverlayData 0..11, one block
    per theater set, over a checkerboard so alpha 0 is visible as alpha 0."""
    from PIL import Image
    blocks = []
    for name, frames in sets:
        w, h = FRAMES * FW, TYPES * FH
        img = Image.new("RGBA", (w, h))
        for kind in range(TYPES):
            for stage in range(FRAMES):
                t = Image.frombytes("RGBA", (FW, FH), frames[kind][stage])
                img.paste(t, (stage * FW, kind * FH))
        bg = Image.new("RGBA", (w, h))
        for y in range(h):
            for x in range(w):
                bg.putpixel((x, y), (26, 26, 26, 255) if ((x // 4 + y // 4) % 2 == 0)
                            else (46, 46, 46, 255))
        blocks.append((name, Image.alpha_composite(bg, img)))
    W = max(b.size[0] for _, b in blocks)
    H = sum(b.size[1] for _, b in blocks) + 12 * len(blocks)
    out = Image.new("RGB", (W, H), (12, 12, 12))
    y = 0
    for name, b in blocks:
        out.paste(b.convert("RGB"), (0, y))
        y += b.size[1] + 12
    out = out.resize((W * scale, H * scale), Image.NEAREST)
    out.save(path)
    return out.size, [n for n, _ in blocks]


def readback(path, sets):
    """Re-read the written pack with a parser that knows ONLY the documented format,
    rebuild every frame from the sheet pixels through the slot table, and compare it to
    what we baked. This is the renderer's job done in Python: if the C loader mirrors the
    format comment, it sees exactly these pixels. Raises on any disagreement.

    "Tests that pass because nothing objected" is this project's most expensive habit, so
    this asserts three separate things: every frame's pixels, that the slot for
    (kind, stage) really is kind*FRAMES+stage, and that the reader consumes the file to
    the last byte with nothing left over."""
    blob = open(path, "rb").read()
    assert blob[:8] == MAGIC, "bad magic"
    ver, src, nsets, fw, fh, types, frames = struct.unpack_from("<7I", blob, 8)
    assert (ver, fw, fh, types, frames) == (VERSION, FW, FH, TYPES, FRAMES), \
        (ver, fw, fh, types, frames)
    assert nsets == len(sets), (nsets, len(sets))
    o = 8 + 28
    checked = 0
    for name, frames_in in sets:
        nm = blob[o:o + 12].split(b"\x00")[0].decode("ascii")
        assert nm == name, (nm, name)
        o += 12
        nsheets, texw, texh, cols, rows = struct.unpack_from("<5I", blob, o)
        o += 20
        assert (texw, texh, cols, rows) == (TEX, TEX, COLS, ROWS)
        slots = []
        for _ in range(types * frames):
            slots.append(struct.unpack_from("<3H", blob, o))
            o += 6
        sheets = []
        for _ in range(nsheets):
            sheets.append(blob[o:o + texw * texh * 4])
            o += texw * texh * 4
        for kind in range(types):
            for stage in range(frames):
                sh, x0, y0 = slots[kind * frames + stage]
                got = bytearray()
                for y in range(fh):
                    p = ((y0 + y) * texw + x0) * 4
                    got += sheets[sh][p:p + fw * 4]
                if bytes(got) != frames_in[kind][stage]:
                    raise SystemExit("READBACK MISMATCH set %s kind %d stage %d"
                                     % (name, kind, stage))
                checked += 1
    if o != len(blob):
        raise SystemExit("READBACK consumed %d of %d bytes" % (o, len(blob)))
    return checked, o


def verify(rom_path, cd_dir):
    """Cross-check the cartridge art against the 1995 art, mask for mask.

    The ONLY legal disagreement is the DOS shadow index 4, which this baker
    drops to alpha 0 (the project's standing convention, bake_dosinfantry.py /
    bake_dosmake.py) while the cartridge keeps an opaque texel there. Those
    pixels are counted separately and must account for every mismatch."""
    from mixshp import MixFile, Shape

    n64 = load_n64(rom_path)[0][1]
    dos = dict(load_dos(cd_dir))["TEMPERATE"]
    mix = MixFile(os.path.join(cd_dir, "TEMPERAT.MIX"))
    raw = [[Shape(mix.read("TI%d.TEM" % (k + 1))).frame(f) for f in range(FRAMES)]
           for k in range(TYPES)]

    tot = agree = ghost = other = 0
    for kind in range(TYPES):
        for stage in range(FRAMES):
            a, b, r = n64[kind][stage], dos[kind][stage], raw[kind][stage]
            for p in range(FW * FH):
                tot += 1
                if (a[p * 4 + 3] == 0) == (b[p * 4 + 3] == 0):
                    agree += 1
                elif r[p] == DOS_SHADOW:
                    ghost += 1
                else:
                    other += 1
    print("VERIFY|alpha-mask n64 vs dos-temperate: %d/%d agree (%.2f%%), "
          "%d differ only at the DOS shadow index 4, %d unexplained"
          % (agree, tot, 100.0 * agree / tot, ghost, other))
    return other == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", choices=("n64", "dos"), default="n64")
    ap.add_argument("--rom", default=os.path.join(ROOT, "data", "rom", "cnc_eu.z64"))
    ap.add_argument("--cd", default="/Volumes/GDI")
    ap.add_argument("--out", default=os.path.join(HERE, "dostib.pack"))
    ap.add_argument("--proof", default=None)
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()

    if a.verify:
        ok = verify(a.rom, a.cd)
        raise SystemExit(0 if ok else 1)

    if a.source == "n64":
        sets, source = load_n64(a.rom), SRC_N64
    else:
        sets, source = load_dos(a.cd), SRC_DOS

    size = write_pack(a.out, source, sets)
    print("TIBPACK|%s|source=%s|sets=%s|types=%d|frames=%d|%dx%d per frame|"
          "%d sheets/set of %dx%d|%d bytes"
          % (a.out, a.source, ",".join(n for n, _ in sets), TYPES, FRAMES, FW, FH,
             (TYPES * FRAMES + PER_SHEET - 1) // PER_SHEET, TEX, TEX, size))

    nframes, used = readback(a.out, sets)
    print("READBACK|%d frames re-read from the sheets through the slot table, all match|"
          "%d of %d bytes consumed" % (nframes, used, size))

    if a.proof:
        sz, names = proof_png(a.proof, sets)
        print("PROOF|%s|%dx%d|blocks=%s|rows=shape TI1..TI12, "
              "cols=OverlayData 0..11" % (a.proof, sz[0], sz[1], ",".join(names)))


if __name__ == "__main__":
    main()
