#!/usr/bin/env python3
"""
bake_doscrate.py -- the two bonus-crate overlays, WCRATE and SCRATE, baked into
`doscrate.pack` so the renderer can draw a crate instead of leaving an invisible pickup
on the ground.

WHY THIS EXISTS

Tiberian Dawn has the whole crate feature and always did: MapClass::Place_Random_Crate
scatters them when MPlayerGoodies is on, and cell.cpp's pickup hands out money, a unit, a
nuclear missile piece or a change of shroud when a unit drives over one. What this project
was missing was only the PICTURE. The brain's dump filter carried tiberium and walls and
nothing else, so a crate never reached the renderer, and a bonus arrived from nowhere with
no explanation -- which is worse than having no crates at all. The CRATE| dump beside TIB|
and WALL| closes the first half; this closes the second.

NO CD REQUIRED, and that is the point of the two sources below.

  the art       WCRATE.SHP and SCRATE.SHP out of playable/dosdata/CONQUER.MIX, which
                ships with the game. Both are a single 10x11 frame.

  the palette   read back out of playable/dossidebar.pack, NOT from TEMPERAT.PAL on the
                1995 CD. bake_dossidebar.py stores `pal8[768]`, the palette "widened the
                way the engine widens it", and that pack is already built and shipped.
                Every other DOS baker here begins by telling you to mount CD-1; this one
                does not have to, because the one thing it needs from the CD is already
                sitting in a pack beside it. A baker that cannot run on a clean checkout
                is a baker that silently stops being run.

  DOS_TRANSPARENT is index 0 and DOS_SHADOW is index 4, the same two the tiberium baker
  drops. A crate has no shadow of its own; the 4s in these two shapes are the dark side
  of the box and dropping them would punch a hole in it, so ONLY index 0 is dropped here.
  Verified by counting: dropping 4 as well removes a fifth of the wood crate's pixels.

FORMAT (all little-endian), magic "DOSCRT1\\0":

    char  magic[8]        "DOSCRT1\\0"
    u32   version         1
    u32   count           2 (wood, steel -- in that order, and the order IS the wire
                          value: the brain's CRATE| line ends in steel=0 or steel=1)
    count x {
        char name[12]     "WCRATE", "SCRATE"
        u32  w, h
        u8   rgba[w*h*4]  straight RGBA8, alpha a hard 0 or 255
    }
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "menu", "tools"))

MAGIC = b"DOSCRT1\x00"
VERSION = 1
DOS_TRANSPARENT = 0
SHAPES = ["WCRATE", "SCRATE"]      # wood is steel=0, steel is steel=1


def load_palette(barpack):
    """pal8[768] out of an already-built dossidebar.pack. See the note above."""
    with open(barpack, "rb") as fh:
        blob = fh.read()
    if blob[:8] != b"DOSBAR01":
        raise SystemExit("%s: not a dossidebar pack (magic %r)" % (barpack, blob[:8]))
    ver, nshape, nfont, npal = struct.unpack_from("<4I", blob, 8)
    if ver != 1 or npal != 256:
        raise SystemExit("%s: version %d with %d palette entries, expected 1 and 256"
                         % (barpack, ver, npal))
    off = 8 + 16 + 768                      # past the header and pal6
    pal8 = blob[off:off + 768]
    if len(pal8) != 768:
        raise SystemExit("%s: truncated before pal8" % barpack)
    if max(pal8) > 252:
        raise SystemExit("%s: pal8 has a value above 252, so it is not the widened "
                         "palette this expects" % barpack)
    return pal8


def load_shapes(mixpath, pal8):
    from mixshp import MixFile, Shape
    mix = MixFile(mixpath)
    out = []
    for name in SHAPES:
        shp = Shape(mix.read(name + ".SHP"))
        if shp.frames != 1:
            raise SystemExit("%s has %d frames, expected 1" % (name, shp.frames))
        w, h = shp.width, shp.height
        src = shp.frame(0)
        buf = bytearray(w * h * 4)
        opaque = 0
        for p in range(w * h):
            v = src[p]
            if v == DOS_TRANSPARENT:
                continue                    # already 0,0,0,0
            buf[p * 4:p * 4 + 4] = bytes((pal8[v * 3], pal8[v * 3 + 1],
                                          pal8[v * 3 + 2], 255))
            opaque += 1
        if opaque * 4 < w * h:
            raise SystemExit("%s came out less than a quarter opaque (%d of %d): the "
                             "palette or the transparent index is wrong"
                             % (name, opaque, w * h))
        out.append((name, w, h, bytes(buf), opaque))
    return out


def main():
    root = os.path.normpath(os.path.join(HERE, ".."))
    mixpath = os.path.join(root, "playable", "dosdata", "CONQUER.MIX")
    barpack = os.path.join(root, "playable", "dossidebar.pack")
    for p in (mixpath, barpack):
        if not os.path.isfile(p):
            raise SystemExit("missing %s" % p)

    pal8 = load_palette(barpack)
    shapes = load_shapes(mixpath, pal8)

    out = os.path.join(HERE, "doscrate.pack")
    with open(out, "wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<2I", VERSION, len(shapes)))
        for name, w, h, rgba, _op in shapes:
            fh.write(name.encode("ascii").ljust(12, b"\x00"))
            fh.write(struct.pack("<2I", w, h))
            fh.write(rgba)

    # READ IT BACK, because a pack that writes cleanly and loads wrong is the failure
    # every other baker here guards against by doing exactly this.
    with open(out, "rb") as fh:
        blob = fh.read()
    assert blob[:8] == MAGIC, "bad magic on readback"
    ver, n = struct.unpack_from("<2I", blob, 8)
    assert ver == VERSION and n == len(shapes), (ver, n)
    off = 16
    for name, w, h, rgba, _op in shapes:
        got = blob[off:off + 12].rstrip(b"\x00").decode("ascii"); off += 12
        gw, gh = struct.unpack_from("<2I", blob, off); off += 8
        assert (got, gw, gh) == (name, w, h), (got, gw, gh, name, w, h)
        assert blob[off:off + w * h * 4] == rgba, "%s pixels differ on readback" % name
        off += w * h * 4
    assert off == len(blob), "trailing bytes: %d of %d consumed" % (off, len(blob))

    for name, w, h, _rgba, op in shapes:
        print("  %-8s %dx%d, %d opaque pixels" % (name, w, h, op))
    print("wrote %s: %d crates, %d bytes, readback clean" % (out, len(shapes), len(blob)))


if __name__ == "__main__":
    main()
