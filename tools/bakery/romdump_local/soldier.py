#!/usr/bin/env python3
"""
CNC3D -- the N64 infantry sprite table (`../src/n64soldier.c`), palettes included.

WHY THIS FILE EXISTS
--------------------
The 67 infantry strips (E1_STAND.IMG ... DP_FDETH.IMG) are CI4 and their .IMG header
says `pal_count = 16`, but the file body stops exactly at the last pixel: there is no
palette in the file. Decoding them without one produced the near-black figures the
verification pass flagged. The TLUTs live in the game code, not in the asset archive,
and this module recovers them.

WHAT IS IN THE ROM (all offsets are the EU cart, cnc_eu.z64)
------------------------------------------------------------
Everything below sits in the `CodeOverlay` segment, ROM 0x1643F0..0x1B67D0 loaded at
RAM 0x801C2600, so `ram = rom + 0x8005E210`.

  ROM 0x1B20D8   palette bank: 206 x 16-entry RGBA5551 TLUTs, 32 bytes each.
                 Entry 15 is the transparent background in every single one of them
                 (alpha bit clear), which is what index 15 -- 86% of all sprite
                 pixels -- decodes to.

  ROM 0x1B3AA0   67 sprite descriptors, 20 bytes each, in Solfiles.eng order:
                     +0x00 u32  pixel pointer   (patched at load; heap)
                     +0x04 u32  0               (filled in by the loader)
                     +0x08 u32  TLUT pointer    -> the bank above
                     +0x0C u8   facings
                     +0x0D u8   stages          facings*stages == frame count
                     +0x0E u8   frame width
                     +0x0F u8   frame height
                     +0x10 u16  bytes per frame (w*h/2, i.e. CI4)
                     +0x12 u16  TMEM row budget
                 Cross-checked against all 67 extracted .IMG headers: every width,
                 every height and every frame count agrees, 67/67, zero mismatches.
                 That is the proof the table is the right one and in the right order.

  ROM 0x1B3FDC   8 pointers into the descriptor table, one per sprite SET, in the order
                 E1 E2 E3 E4 E6 RB MB DP (9,9,9,9,7,9,7,8 descriptors respectively).

  ROM 0x16AAF8   the 20-case switch (RAM 0x801C8D08) that turns an N64 infantry type id
                 2000..2019 into (sprite set, palette index). Decoded in TYPE_MAP below.
                 The palette is selected as `tlut + palette_index*32`, which is literally
                 what the accessor at RAM 0x801FB6E4 does:
                     lw $v1, 8($s1)      ; descriptor.tlut
                     sll $v0, $v0, 5     ; index * 32
                     addu $v1, $v1, $v0

WHAT THE PALETTE INDEX MEANS
----------------------------
Combat infantry own exactly TWO palettes; civilians own ELEVEN.

  * MB (Moebius set) palette 0 vs 1 differ in a single entry: index 0 is A4947B (a tan
    hat) in one and near-black (hair) in the other. Type 2007 picks 0 and type 2008
    picks 1, so those two are Dr Moebius and Dr Chan. Not a lighting variant: a
    different person.
  * DP palettes 0..10 are eleven different sets of clothes. Types 2009..2018 pick 0..9
    and type 2019 picks 10 -- the ten civilians C1..C10 plus Delphi.
  * For the six combat sets, palette 0 is a tan/khaki uniform and palette 1 a blue-grey
    one. Those are the two sides, and the ROM says which is which without ambiguity:
    the sidebar carries two complete cameo sets, CAM_*.IMG and CAM*2.IMG, whose art is
    the same soldiers in exactly these two colourways. The selector at RAM 0x8002BB4C
    reads character 2 of the scenario name -- 'G' as in SCG01EA picks the CAM_* (tan)
    table, anything else, i.e. 'B' as in SCB01EA, picks the CAM*2 (blue-grey) table.
    So palette 0 = GDI, palette 1 = Nod.

Usage:
    python3 soldier.py list                     descriptor table
    python3 soldier.py png <NAME> <pal> <out>   one strip as a PNG
    python3 soldier.py sheet <out.png>          every set, every palette, contact sheet
"""
import os, struct, sys

HERE   = os.path.dirname(os.path.abspath(__file__))
ROOT   = os.path.abspath(os.path.join(HERE, "..", ".."))          # share/CNC3D
ROM_P  = os.path.join(ROOT, "rom", "cnc_eu.z64")
IMG_D  = os.path.join(ROOT, "assets", "extracted", "IMG")
SOLF_P = os.path.join(ROOT, "assets", "extracted", "ENG", "SOLFILES.ENG")

CO_DELTA  = 0x801C2600 - 0x1643F0      # CodeOverlay: ram = rom + delta
PAL_BANK  = 0x1B20D8                   # 206 x 32 bytes
PAL_COUNT = 206
REC_TABLE = 0x1B3AA0
REC_COUNT = 67
SET_TABLE = 0x1B3FDC
SET_NAMES = ["E1", "E2", "E3", "E4", "E6", "RB", "MB", "DP"]

_ROM = None


def rom():
    global _ROM
    if _ROM is None:
        _ROM = open(ROM_P, "rb").read()
    return _ROM


def names():
    """Solfiles.eng: a u16 offset table then NUL-terminated names, little-endian."""
    b = open(SOLF_P, "rb").read()
    n = struct.unpack_from("<H", b, 0)[0] // 2
    out = []
    for i in range(n):
        o = struct.unpack_from("<H", b, i * 2)[0]
        s = b[o:b.index(b"\0", o)].decode()
        if s:
            out.append(s[:-4].upper())        # drop ".img", match the extracted names
    return out


def records():
    """The 67 descriptors, with the palette count derived from the set table."""
    R = rom()
    out = []
    for i in range(REC_COUNT):
        o = REC_TABLE + i * 20
        data, zero, tlut = struct.unpack_from(">III", R, o)
        fac, stg, w, h = R[o+12], R[o+13], R[o+14], R[o+15]
        fb, budget = struct.unpack_from(">HH", R, o+16)
        out.append(dict(index=i, data=data, zero=zero, tlut=tlut,
                        tlut_rom=tlut - CO_DELTA, facings=fac, stages=stg,
                        w=w, h=h, frame_bytes=fb, budget=budget))
    # set membership, from the 8-pointer table
    bounds = [struct.unpack_from(">I", R, SET_TABLE + k*4)[0] for k in range(8)]
    starts = [(b - CO_DELTA - REC_TABLE) // 20 for b in bounds]
    for k, s in enumerate(starts):
        e = starts[k+1] if k + 1 < 8 else REC_COUNT
        for j in range(s, e):
            out[j]["set"] = SET_NAMES[k]
    # 2 palettes per strip, except the civilians' 11
    for r in out:
        r["npal"] = 11 if r["set"] == "DP" else 2
    return out


def by_name():
    return dict(zip(names(), records()))


# infantry type id -> (sprite set, palette index).  Decoded from the jump table at
# ROM 0x16AAF8; see the module docstring.  `None` means "the caller supplies it", which
# for the six combat sets is the house: 0 = GDI, 1 = Nod.
TYPE_MAP = {
    2000: ("E1", None),   # E1 minigunner
    2001: ("E2", None),   # E2 grenadier
    2002: ("E3", None),   # E3 rocket soldier
    2003: ("E4", None),   # E4 flamethrower
    2004: ("E1", None),   # E5 chem warrior -- the ROM forces set id 0x7D0, i.e. E1 art
    2005: ("E6", None),   # E6 engineer
    2006: ("RB", None),   # RMBO commando
    2007: ("MB", 0),      # Moebius
    2008: ("MB", 1),      # Chan
    2009: ("DP", 0),      # C1
    2010: ("DP", 1),      # C2
    2011: ("DP", 2),      # C3
    2012: ("DP", 3),      # C4
    2013: ("DP", 4),      # C5
    2014: ("DP", 5),      # C6
    2015: ("DP", 6),      # C7
    2016: ("DP", 7),      # C8
    2017: ("DP", 8),      # C9
    2018: ("DP", 9),      # C10 (Nikoomba)
    2019: ("DP", 10),     # Delphi
}

# The same thing keyed by the INI type code the mission files and the GPL brain use.
INI_TYPE = {
    "E1": 2000, "E2": 2001, "E3": 2002, "E4": 2003, "E5": 2004, "E6": 2005,
    "RMBO": 2006, "MOEBIUS": 2007, "CHAN": 2008,
    "C1": 2009, "C2": 2010, "C3": 2011, "C4": 2012, "C5": 2013,
    "C6": 2014, "C7": 2015, "C8": 2016, "C9": 2017, "C10": 2018,
    "DELPHI": 2019,
}


def palette(rom_off, index=0):
    """One 16-entry TLUT as RGBA8888 tuples. Entry 15 comes back fully transparent."""
    w = struct.unpack_from(">16H", rom(), rom_off + index * 32)
    out = []
    for v in w:
        r, g, b = (v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31
        out.append((r*255//31, g*255//31, b*255//31, 255 if (v & 1) else 0))
    return out


def decode(name, pal_index=0):
    """(w, h, RGBA bytes, frames, frame_h) for one strip, correctly coloured."""
    rec = by_name()[name]
    b = open(os.path.join(IMG_D, name + ".IMG"), "rb").read()
    hdr, _body, w, h = struct.unpack_from(">IIHH", b, 0)
    body = b[hdr:]
    if pal_index >= rec["npal"]:
        raise ValueError("%s has %d palettes, asked for %d"
                         % (name, rec["npal"], pal_index))
    pal = palette(rec["tlut_rom"], pal_index)
    out = bytearray()
    for y in range(h):
        for x in range(w):
            v = body[(y * w + x) // 2]
            out += bytes(pal[(v >> 4) if (x & 1) == 0 else (v & 15)])
    return w, h, bytes(out), rec["facings"] * rec["stages"], rec["h"]


# ----------------------------------------------------------------- self check
def verify():
    """Every descriptor must agree with the .IMG it names. 67/67 or we are wrong."""
    bad = []
    for n, r in zip(names(), records()):
        b = open(os.path.join(IMG_D, n + ".IMG"), "rb").read()
        _hdr, _body, w, h = struct.unpack_from(">IIHH", b, 0)
        nf = r["facings"] * r["stages"]
        if w != r["w"] or h != r["h"] * nf or r["frame_bytes"] != r["w"] * r["h"] // 2:
            bad.append((n, (w, h), (r["w"], r["h"], nf)))
    return bad


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    if cmd == "list":
        bad = verify()
        for n, r in zip(names(), records()):
            print("%-10s set=%-2s %2dx%-2d fac=%d stg=%d frames=%2d pals=%2d "
                  "tlut=%08X (rom %06X)"
                  % (n, r["set"], r["w"], r["h"], r["facings"], r["stages"],
                     r["facings"]*r["stages"], r["npal"], r["tlut"], r["tlut_rom"]))
        print("\ndescriptor/IMG mismatches: %d of %d" % (len(bad), REC_COUNT))
        for x in bad:
            print("   ", x)
    elif cmd == "png":
        from jim import write_png
        name, pi, dst = sys.argv[2], int(sys.argv[3]), sys.argv[4]
        w, h, rgba, nf, fh = decode(name, pi)
        write_png(dst, w, h, [rgba[y*w*4:(y+1)*w*4] for y in range(h)])
        print("wrote %s  %dx%d  %d frames of %d" % (dst, w, h, nf, fh))
    elif cmd == "sheet":
        from PIL import Image, ImageDraw
        dst = sys.argv[2]
        rows = []
        for s in SET_NAMES:
            npal = 11 if s == "DP" else 2
            for p in range(npal):
                nm = "%s_STAND" % s
                w, h, rgba, nf, fh = decode(nm, p)
                im = Image.frombytes("RGBA", (w, h), rgba)
                rows.append(("%s pal%d" % (s, p),
                             [im.crop((0, k*fh, w, (k+1)*fh)) for k in range(nf)]))
        Z = 8
        W = 150 + max(sum(f.width + 4 for f in fs) for _, fs in rows) * Z
        H = sum(max(f.height for f in fs) * Z + 10 for _, fs in rows) + 20
        out = Image.new("RGB", (W, H), (58, 58, 68))
        d = ImageDraw.Draw(out)
        y = 10
        for lab, fs in rows:
            d.text((8, y + 6), lab, fill=(255, 255, 255))
            x = 150
            for f in fs:
                big = f.resize((f.width*Z, f.height*Z), Image.NEAREST)
                out.paste(big, (x, y), big)
                x += big.width + 4*Z
            y += max(f.height for f in fs) * Z + 10
        out.save(dst)
        print("wrote", dst, out.size)
