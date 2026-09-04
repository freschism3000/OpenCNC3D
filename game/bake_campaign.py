#!/usr/bin/env python3
"""
bake_campaign.py -- flatten the 1995 MS-DOS campaign-flow screens into one
little-endian pack, `campaign.pack`, for the app shell's side-select, score and
map-selection screens.

Everything is decoded with the proven ports in menu/tools/mixshp.py (LCW from
common/lcw.cpp, XOR delta from common/xordelta.cpp) and verified by LOOKING at
the decoded frames (bake writes a contact sheet next to the pack).

WHAT THE ENGINE SAYS (citations, brain/vanilla/tiberiandawn):

  * Side select   -- intro.cpp Choose_Side(): CHOOSE.WSA looping, blitted to
    (0,22) 320x156; STRUGGLE.AUD static loops; GDI_SLCT/NOD_SLCT speech on click.
  * Score screen  -- score.cpp ScoreClass::Presentation: S-GDIIN2.WSA (GDI) /
    SCRSCN1.WSA (Nod) backgrounds; TIME/HISCORE1/HISCORE2 shape loops;
    BAR3YLW/BAR3RED kill bars (shape 120 of BAR3RED is the white flash);
    CREDS.SHP cash counter. All from CONQUER.MIX.
  * Map selection -- mapsel.cpp Map_Selection(): GREYERTH.WSA then E-BWTOCL.WSA
    (the grey-globe spin-in), EARTH_E.WSA (globe spin to Europe, 92 frames with
    per-frame sounds), EUROPE.WSA (227 frames: territory advances + crosshair
    blink via palette 249..254 cycling), CLICK_E.CPS (per-pixel click map),
    COUNTRYE.SHP (country highlight), DARK_E.PAL (statistics-page palette).

WSA format -- the REAL header, common/wsa.cpp:126-141 WSA_FileHeaderType:
    u16 total_frames; u16 x; u16 y; u16 w; u16 h;
    u16 largest_frame_size; i16 flags;          <- the old bake read these two
    u32 offsets[total_frames+2];                   as one u32 and guessed the
    [u8 pal[768] iff flags & 1]                    palette from offsets[0]
  Frame data offsets are file-relative PLUS 768 when the palette is present
  (Get_File_Frame_Offset adds palette_adjust). offsets[0] == 0 would mean
  frame 0 is a delta over pre-existing screen content (WSA_FRAME_0_IS_DELTA);
  none of the files baked here do that, and the bake asserts it.
  Frame i: LCW chunk at offsets[i]..offsets[i+1], decompressed then XOR-applied
  over the accumulator (frame 0 over zeros). offsets[frames+1], when not 0, is
  a loop-back delta; unused here (looping replays frame 0's delta over zeros).

PACK FORMAT (little-endian), magic "CNC3DCPN" version 2:
    char  magic[8]; u32 version = 2; u32 entry_count
    entry: char name[16]; u32 kind; u32 frames, w, h, x, y; u8 pal[768]
      kind 0 (delta anim): u32 chunk_size[frames]; chunks back to back, each
             the UNTOUCHED on-disc LCW'd XOR delta (the app decodes: this is
             what keeps EUROPE's 227 frames at 225 KB instead of 14.5 MB)
      kind 1 (image)  : frames * w*h raw indices
      kind 2 (shapes) : frames * w*h raw indices (colour 0 transparent)
      kind 3 (palette): no pixel payload at all, just the 768-byte palette
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "menu", "tools")))
from mixshp import MixFile, Shape, lcw_uncompress, apply_xor_delta  # noqa: E402

DOSDATA = os.path.normpath(os.path.join(HERE, "..", "data", "dosdata"))
OUT = os.path.join(HERE, "campaign.pack")
MAGIC = b"CNC3DCPN"
VERSION = 2


def wsa_parse(data, name):
    """-> (frames, w, h, x, y, pal, [lcw chunk per frame]) straight off the
    real header. The chunks stay compressed; decoding here is only for proof."""
    nf, x, y, w, h, largest, flags = struct.unpack_from("<6Hh", data, 0)
    offs = struct.unpack_from("<%dI" % (nf + 2), data, 14)
    haspal = bool(flags & 1)
    padj = 768 if haspal else 0
    base = 14 + 4 * (nf + 2)
    pal = data[base:base + 768] if haspal else b"\x00" * 768
    assert offs[0] != 0, "%s: frame 0 is a delta over screen content (unsupported)" % name
    chunks = []
    for i in range(nf):
        chunks.append(data[offs[i] + padj:offs[i + 1] + padj])
        assert len(chunks[-1]) > 0, (name, i)
    return nf, w, h, x, y, bytes(pal), chunks


def wsa_prove(w, h, chunks):
    """Decode every frame the way the app will; returns the full frames for the
    contact sheet. Failure here fails the bake, not the runtime."""
    acc = bytearray(w * h)
    frames = []
    for ch in chunks:
        interm, _n = lcw_uncompress(ch, 0, w * h + 4096)
        apply_xor_delta(acc, bytes(interm), 0)
        frames.append(bytes(acc))
    return frames


def cps_decode(data):
    """CPS: u16 filesize; u16 kind(0x0004 LCW); u32 uncompressed(64000);
    u16 palette_size; [pal]; LCW body -> 320x200 indices + palette."""
    size, kind, uncomp, palsz = struct.unpack_from("<HHIH", data, 0)
    off = 10
    pal = data[off:off + palsz] if palsz else b"\x00" * 768
    off += palsz
    out, _n = lcw_uncompress(data[off:], 0, 64000)
    return bytes(out[:64000]), (pal if palsz == 768 else b"\x00" * 768)


def main():
    transit = MixFile(os.path.join(DOSDATA, "transit", "TRANSIT.MIX"))
    general = MixFile(os.path.join(DOSDATA, "GENERAL.MIX"))
    conquer = MixFile(os.path.join(DOSDATA, "CONQUER.MIX"))

    entries = []   # (tag, kind, nf, w, h, x, y, pal, payload_frames_or_chunks)
    proofs = []    # (tag, w, h, pal, [full frames])  -- for the contact sheet

    def add_wsa(mix, name, tag):
        nf, w, h, x, y, pal, chunks = wsa_parse(mix.read(name), name)
        full = wsa_prove(w, h, chunks)
        entries.append((tag, 0, nf, w, h, x, y, pal, chunks))
        proofs.append((tag, w, h, pal, full))
        print("WSA %-12s -> %-8s: %3d frames %dx%d at (%d,%d), %d B of deltas"
              % (name, tag, nf, w, h, x, y, sum(len(c) for c in chunks)))

    def add_cps(mix, name, tag):
        body, pal = cps_decode(mix.read(name))
        entries.append((tag, 1, 1, 320, 200, 0, 0, pal, [body]))
        proofs.append((tag, 320, 200, pal, [body]))
        print("CPS %-12s -> %s" % (name, tag))

    def add_shp(mix, name, tag):
        shp = Shape(mix.read(name))
        frames = [bytes(shp.frame(i)) for i in range(shp.frames)]
        entries.append((tag, 2, shp.frames, shp.width, shp.height, 0, 0,
                        b"\x00" * 768, frames))
        proofs.append((tag, shp.width, shp.height, None, frames))
        print("SHP %-12s -> %-8s: %3d frames %dx%d" % (name, tag, shp.frames,
                                                       shp.width, shp.height))

    def add_pal(mix, name, tag):
        pal = mix.read(name)
        assert len(pal) == 768, (name, len(pal))
        entries.append((tag, 3, 0, 0, 0, 0, 0, bytes(pal), []))
        print("PAL %-12s -> %s" % (name, tag))

    # side select
    add_wsa(transit, "CHOOSE.WSA", "CHOOSE")
    # score screen (score.cpp ScreenNames + its shape props)
    add_wsa(general, "S-GDIIN2.WSA", "SCORE_G")
    add_wsa(general, "SCRSCN1.WSA", "SCORE_N")
    add_shp(conquer, "TIME.SHP", "TIME")
    add_shp(conquer, "HISCORE1.SHP", "HISCORE1")
    add_shp(conquer, "HISCORE2.SHP", "HISCORE2")
    add_shp(conquer, "BAR3YLW.SHP", "BAR3YLW")
    add_shp(conquer, "BAR3RED.SHP", "BAR3RED")
    add_shp(conquer, "CREDS.SHP", "CREDS")
    # map selection (mapsel.cpp Map_Selection, GDI reels)
    add_wsa(general, "GREYERTH.WSA", "GREYERTH")
    add_wsa(general, "E-BWTOCL.WSA", "E-BWTOCL")
    add_wsa(general, "EARTH_E.WSA", "EARTH_E")
    add_wsa(general, "EUROPE.WSA", "EUROPE")
    add_cps(general, "CLICK_E.CPS", "CLICK_E")
    add_shp(conquer, "COUNTRYE.SHP", "COUNTRYE")
    add_pal(general, "DARK_E.PAL", "DARK_E")
    # map selection, NOD reels. GREYERTH and E-BWTOCL are shared -- the grey globe and its
    # fade to colour are the same for both houses -- and only the three that name a
    # continent differ: EARTH_A spins to Africa where EARTH_E spins to Europe, AFRICA is
    # the territory reel, CLICK_A the per-pixel click map.
    # These were listed in known-gap notes as needing CD-2 and they do not: all three sit
    # in the GENERAL.MIX we already ship, beside the GDI ones. Nobody had looked.
    add_wsa(general, "EARTH_A.WSA", "EARTH_A")
    add_wsa(general, "AFRICA.WSA", "AFRICA")
    add_cps(general, "CLICK_A.CPS", "CLICK_A")

    with open(OUT, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", VERSION, len(entries)))
        for (tag, kind, nf, w, h, x, y, pal, payload) in entries:
            b = tag.encode()
            f.write(b + b"\x00" * (16 - len(b)))
            f.write(struct.pack("<6I", kind, nf, w, h, x, y))
            f.write(pal)
            if kind == 0:
                f.write(struct.pack("<%dI" % nf, *[len(c) for c in payload]))
                for c in payload:
                    f.write(c)
            elif kind == 3:
                pass
            else:
                for fr in payload:
                    assert len(fr) == w * h, (tag, len(fr), w * h)
                    f.write(fr)
    print("wrote %s (%.1f MB)" % (OUT, os.path.getsize(OUT) / 1e6))

    # the LOOK-at-it proof: a contact sheet of first/mid/last frames per entry,
    # decoded exactly the way the app decodes them
    from PIL import Image
    tiles = []
    for (tag, w, h, pal, frames) in proofs:
        nf = len(frames)
        if pal is None:
            pal8 = [((i % 3 == 0) and i or i) and min(255, i) for i in range(768)]
            pal8 = [min(255, (i // 3)) for i in range(768)]  # grey ramp for SHPs
        else:
            pal8 = [min(255, v << 2) for v in pal]
        for fi in sorted(set([0, nf // 2, nf - 1])):
            img = Image.new("RGB", (w, h))
            px = img.load()
            fr = frames[fi]
            for yy in range(h):
                for xx in range(w):
                    v = fr[yy * w + xx]
                    px[xx, yy] = (pal8[v * 3], pal8[v * 3 + 1], pal8[v * 3 + 2])
            tiles.append((tag, fi, img))
    sheet = Image.new("RGB", (3 * 340, ((len(tiles) + 2) // 3) * 220), (20, 20, 30))
    for i, (tag, fi, img) in enumerate(tiles):
        img.thumbnail((320, 200))
        sheet.paste(img, ((i % 3) * 340 + 10, (i // 3) * 220 + 10))
    sheet.save(os.path.join(HERE, "campaign_proof.png"))
    print("proof sheet: campaign_proof.png -- LOOK AT IT before trusting the bake")


if __name__ == "__main__":
    main()
