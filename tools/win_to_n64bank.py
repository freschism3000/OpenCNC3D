#!/usr/bin/env python3
"""win_to_n64bank.py -- build an N64-shaped WINTER terrain bank from TD's PC winter art.

The cartridge shipped DESERT and TEMPERAT tile banks and nothing else -- the N64 port
has no winter theater to extract. But Tiberian Dawn itself does: WINTER.MIX carries
the full PC icon sets (<TEMPLATE>.WIN, IconControlType format, 24x24 8bpp) plus
WINTER.PAL. This tool re-shapes that art into the exact six-file bank layout
n64_terrain.load_theater() reads, so the whole existing pipeline -- gen_terrain_tiles,
stage-skirmish-maps, bake5, the editor's tt_ tables -- works on winter unchanged.

Everything goes into the 8bpp bank (the PC art is 8bpp; there is nothing to quantise):

    DA8  all icons concatenated, 576 B each          TL8  (template, cell) per slot
    ND8  one zero byte per slot (single palette)     PA8  WINTER.PAL, 6->8 bit
    DA4/TL4/ND4/PA4  zero length (n4 = 0; load_theater derives counts from sizes)

TL8 is BUILT rather than inverted, so coverage is exact by construction: one bank slot
per (template, cell) that actually has art. Cells whose icon map entry is 0xFF are the
genuine holes of multi-cell templates (the PC engine never writes them into a .BIN)
and get no slot.

Template ids are the TemplateType enum indices from the GPL headers -- the same ids
PC .BINs use, which is what bin_to_n64map.py and the editor's terrain_tiles.h expect.
Only templates flagged THEATERF_WINTER in cdata.cpp are taken; anything flagged but
absent from the MIX (or vice versa) is reported, not silently skipped.

Run:  python3 tools/win_to_n64bank.py [path/to/winter.mix]
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
EX = os.path.join(ROOT, "tools", "bakery", "sharecopy", "assets", "extracted")
DEFINES = os.path.join(ROOT, "brain", "vanilla", "tiberiandawn", "defines.h")
CDATA = os.path.join(ROOT, "brain", "vanilla", "tiberiandawn", "cdata.cpp")
DEFAULT_MIX = os.path.expanduser(
    "~/Library/Application Support/OpenRA/Content/cnc/winter.mix")

sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))
import mixshp  # noqa: E402

TS = 24
ICON_BYTES = TS * TS  # 576, 8bpp


def template_enum():
    """TEMPLATE_ name -> index, straight off the engine's own enum."""
    src = open(DEFINES, errors="ignore").read()
    m = re.search(r"enum TemplateType\s*:?[^{]*\{(.*?)TEMPLATE_COUNT", src, re.S)
    if not m:
        raise SystemExit("no TemplateType enum in " + DEFINES)
    names = re.findall(r"TEMPLATE_([A-Z0-9]+)\s*[,=]", m.group(1))
    return {n: i for i, n in enumerate(names)}


def theater_templates(flag):
    """[(enum index, IniName)] for every template cdata.cpp flags with `flag`."""
    src = open(CDATA, errors="ignore").read()
    enum = template_enum()
    out = []
    for m in re.finditer(
            r"TemplateTypeClass\s+const\s*\n?\s*\w+\(\s*TEMPLATE_([A-Z0-9]+)\s*,"
            r"\s*([^\"]*?)\"([A-Z0-9]+)\"", src, re.S):
        name, flags, ini = m.group(1), m.group(2), m.group(3)
        if name not in enum:
            raise SystemExit("cdata template TEMPLATE_%s not in the enum" % name)
        if flag in flags:
            out.append((enum[name], ini))
    # cdata declares TEMPLATE_CLEAR1 twice (`Empty` and `Clear`, same "CLEAR1" art);
    # one bank slot per (template, cell), so keep the first of any duplicate id.
    seen, uniq = set(), []
    for tid, ini in sorted(out):
        if tid in seen:
            continue
        seen.add(tid)
        uniq.append((tid, ini))
    return uniq


def winter_templates():
    return theater_templates("THEATERF_WINTER")


def parse_win(data, ini):
    """One .WIN icon set -> [(cell_index, 576-byte image)] using the TD header.

    struct IconControlType (brain/vanilla/common/stamp.cpp): i16 Width, Height,
    Count, Allocated; i32 Size, Icons(==0x20), Palettes, Remaps, TransFlag, Map.
    Map[cell] is the stored image index for each of Count cells, 0xFF = hole.
    """
    w, h, count, _alloc, size, icons, _pal, _rem, _trans, imap = \
        struct.unpack_from("<4h6i", data, 0)
    if not (w == TS and h == TS and icons == 0x20):
        raise SystemExit("%s.WIN: unexpected header w=%d h=%d icons=%#x"
                         % (ini, w, h, icons))
    if size != len(data):
        raise SystemExit("%s.WIN: size field %d != file %d" % (ini, size, len(data)))
    cmap = data[imap:imap + count]
    out = []
    for cell, img in enumerate(cmap):
        if img == 0xFF:
            continue
        o = icons + img * ICON_BYTES
        px = data[o:o + ICON_BYTES]
        if len(px) != ICON_BYTES:
            raise SystemExit("%s.WIN: image %d runs off the file" % (ini, img))
        out.append((cell, px))
    return out


def main():
    mixpath = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_MIX
    mix = mixshp.MixFile(mixpath)

    pal = mix.read("WINTER.PAL")
    if pal is None or len(pal) != 768:
        raise SystemExit("no 768-byte WINTER.PAL in " + mixpath)
    if max(pal) > 63:
        raise SystemExit("WINTER.PAL is not 6-bit VGA (max %d)" % max(pal))
    pa8 = bytes((v << 2) | (v >> 4) for v in pal)

    da8, tl8 = bytearray(), bytearray()
    used, missing = 0, []
    for tid, ini in winter_templates():
        name = ini + ".WIN"
        if not mix.has(name):
            missing.append(ini)
            continue
        if tid > 255:
            raise SystemExit("template id %d does not fit the one-byte TL slot" % tid)
        for cell, px in parse_win(mix.read(name), ini):
            if cell > 255:
                raise SystemExit("%s cell %d does not fit one byte" % (ini, cell))
            da8 += px
            tl8 += bytes((tid, cell))
        used += 1
    n8 = len(tl8) // 2

    os.makedirs(os.path.join(EX, "PA8"), exist_ok=True)
    for ext, blob in (("DA8", bytes(da8)), ("TL8", bytes(tl8)),
                      ("ND8", bytes(n8)), ("PA8", pa8),
                      ("DA4", b""), ("TL4", b""), ("ND4", b""), ("PA4", b"")):
        p = os.path.join(EX, ext, "WINTER." + ext)
        open(p, "wb").write(blob)
    print("WINTER bank: %d templates, %d tiles, %d bytes of art (+%d flagged "
          "winter but absent from the MIX%s)"
          % (used, n8, len(da8), len(missing),
             ": " + " ".join(missing) if missing else ""))


if __name__ == "__main__":
    main()
