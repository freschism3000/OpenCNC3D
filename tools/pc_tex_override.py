#!/usr/bin/env python3
"""pc_tex_override.py -- draw the terrain with Tiberian Dawn's PC tile art instead of the
cartridge's, for A/B comparison only.

WHY THIS EXISTS

The question this answers (2 Sep 2026): do the cartridge's textures look lower resolution
than the PC game's, the grass and the cliffs especially? The cartridge and the PC draw the same
24x24 cell, so nothing is lower RESOLUTION; what differs is COLOUR DEPTH. The cart splits
its terrain into a 4bpp bank (16 colours from a shared sub-palette, `ND4` picks which) and
an 8bpp bank (the theater's 256). TEMPERAT is 315 tiles of 4bpp and 443 of 8bpp, and
CLEAR1 -- the grass under 91% of GDI 1 -- is at the head of the 4bpp bank. The PC art is
8bpp everywhere.

WHAT THIS SWAPS, AND WHAT IT DELIBERATELY DOES NOT

The cart's TL4/TL8 already say which (template, icon) every bank slot draws, and TD's own
theater MIX is keyed by exactly that pair. So the swap is per-tile and in place: same tile
ids, same atlas packing, same UVs, same cells, same heightmap. Only the texels change.

ALPHA IS KEPT FROM THE CARTRIDGE. On a water-capable template the cart punches the water
out as a transparent hole and the renderer draws its own sea underneath; the PC art paints
water into the tile instead. Taking the PC's alpha would change the water MECHANISM as
well as the art, and then the comparison would not be a comparison. So the hole mask is
the cart's and the colour is the PC's.

Enable with CNC3D_PC_TEX=1 in the environment before anything imports n64_terrain.
This is a test rig, not a shipping path.
"""
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
DEFINES = os.path.join(ROOT, "brain", "vanilla", "tiberiandawn", "defines.h")
CDATA = os.path.join(ROOT, "brain", "vanilla", "tiberiandawn", "cdata.cpp")
CONTENT = os.path.expanduser("~/Library/Application Support/OpenRA/Content/cnc")

sys.path.insert(0, os.path.join(ROOT, "menu", "tools"))
import mixshp  # noqa: E402

TS = 24
ICON_BYTES = TS * TS

# theater root -> (MIX basename, icon extension, palette, cdata flag)
THEATERS = {
    "TEMPERAT": ("temperat.mix", "TEM", "TEMPERAT.PAL", "THEATERF_TEMPERATE"),
    "DESERT":   ("desert.mix",   "DES", "DESERT.PAL",   "THEATERF_DESERT"),
    "WINTER":   ("winter.mix",   "WIN", "WINTER.PAL",   "THEATERF_WINTER"),
}


def _template_enum():
    src = open(DEFINES, errors="ignore").read()
    m = re.search(r"enum TemplateType\s*:?[^{]*\{(.*?)TEMPLATE_COUNT", src, re.S)
    if not m:
        raise SystemExit("no TemplateType enum in " + DEFINES)
    names = re.findall(r"TEMPLATE_([A-Z0-9]+)\s*[,=]", m.group(1))
    return {n: i for i, n in enumerate(names)}


def _theater_templates(flag):
    """[(enum index, IniName)] for every template cdata.cpp flags with `flag`."""
    src = open(CDATA, errors="ignore").read()
    enum = _template_enum()
    out = []
    for m in re.finditer(
            r"TemplateTypeClass\s+const\s*\n?\s*\w+\(\s*TEMPLATE_([A-Z0-9]+)\s*,"
            r"\s*([^\"]*?)\"([A-Z0-9]+)\"", src, re.S):
        name, flags, ini = m.group(1), m.group(2), m.group(3)
        if name not in enum:
            raise SystemExit("cdata template TEMPLATE_%s not in the enum" % name)
        if flag in flags:
            out.append((enum[name], ini))
    seen, uniq = set(), []
    for tid, ini in sorted(out):
        if tid in seen:      # cdata declares CLEAR1 twice, same art
            continue
        seen.add(tid)
        uniq.append((tid, ini))
    return uniq


def _parse_icons(data, ini):
    """One icon set -> {cell index: 576-byte 8bpp image}, via TD's IconControlType.

    struct (brain/vanilla/common/stamp.cpp): i16 Width, Height, Count, Allocated;
    i32 Size, Icons(==0x20), Palettes, Remaps, TransFlag, Map. Map[cell] is the stored
    image index for each of Count cells, 0xFF = the genuine hole of a multi-cell template.
    """
    w, h, count, _alloc, size, icons, _pal, _rem, _trans, imap = \
        struct.unpack_from("<4h6i", data, 0)
    if not (w == TS and h == TS and icons == 0x20):
        raise SystemExit("%s: unexpected icon header w=%d h=%d icons=%#x"
                         % (ini, w, h, icons))
    if size != len(data):
        raise SystemExit("%s: size field %d != file %d" % (ini, size, len(data)))
    out = {}
    for cell, img in enumerate(data[imap:imap + count]):
        if img == 0xFF:
            continue
        o = icons + img * ICON_BYTES
        px = data[o:o + ICON_BYTES]
        if len(px) != ICON_BYTES:
            raise SystemExit("%s: image %d runs off the file" % (ini, img))
        out[cell] = np.frombuffer(px, np.uint8).reshape(TS, TS)
    return out


_cache = {}


def pc_art(root):
    """{(template, icon): (24,24,3) uint8 RGB} for one theater, from TD's own MIX."""
    if root in _cache:
        return _cache[root]
    if root not in THEATERS:
        _cache[root] = {}                      # SNOW/SAND have no TD original
        return _cache[root]
    mixname, ext, palname, flag = THEATERS[root]
    path = os.path.join(CONTENT, mixname)
    if not os.path.exists(path):
        raise SystemExit("PC art override needs " + path)
    mix = mixshp.MixFile(path)
    pal6 = mix.read(palname)
    if pal6 is None or len(pal6) != 768:
        raise SystemExit("no 768-byte %s in %s" % (palname, path))
    if max(pal6) > 63:
        raise SystemExit("%s is not 6-bit VGA (max %d)" % (palname, max(pal6)))
    pal = np.array([(v << 2) | (v >> 4) for v in pal6], np.uint8).reshape(256, 3)

    art, missing = {}, []
    for tid, ini in _theater_templates(flag):
        name = ini + "." + ext
        if not mix.has(name):
            missing.append(ini)
            continue
        for cell, idx in _parse_icons(mix.read(name), ini).items():
            art[(tid, cell)] = pal[idx]
    if missing:
        sys.stderr.write("pc_tex_override: %s flagged %s but absent from the MIX: %s\n"
                         % (", ".join(missing), flag, mixname))
    _cache[root] = art
    return art


def enabled():
    return os.environ.get("CNC3D_PC_TEX", "") not in ("", "0")
