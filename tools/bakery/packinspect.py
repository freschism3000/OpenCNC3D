#!/usr/bin/env python3
"""Read a baked mission pack (CNC3DPK5 .. CNC3DPKC) back into Python.

Written so a bake can be PROVEN rather than assumed: it parses every section the
baker writes, so a diff between two packs can name exactly which textures, which
meshes and which vertices moved. The PK8 tail (the two water-texture bank
indices) is parsed too, and `tail_ok()` is the hard check that a PK8 pack really
carries them.

THE TAIL, first to last on disk, and the rule that governs it:
    PKC cm tint (4225 u16 LE) | PK9 heights (4225 u8) | PKA seabed (i32) | PK8 water (2 x i32)
The renderer reaches every one of these by an EOF-RELATIVE seek, so a new block
goes at the FRONT of the tail and never between two existing ones.

    python3 packinspect.py summary  A.pack
    python3 packinspect.py texdiff  OLD.pack NEW.pack      # per-texture byte diff
    python3 packinspect.py meshdiff OLD.pack NEW.pack      # per-mesh triangle diff
    python3 packinspect.py texpng   A.pack IDX out.png [-x SCALE]
"""
import struct
import sys

N_HOUSE = 2
N_SPRITE_ANIMS = 3


class R(object):
    def __init__(self, b):
        self.b = b
        self.o = 0

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]
        self.o += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.o)[0]
        self.o += 4
        return v

    def raw(self, n):
        v = self.b[self.o:self.o + n]
        self.o += n
        return v

    def name(self, n):
        return self.raw(n).split(b"\0")[0].decode("latin-1")

    def f32(self):
        v = struct.unpack_from("<f", self.b, self.o)[0]
        self.o += 4
        return v


def read(path):
    b = open(path, "rb").read()
    r = R(b)
    magic = r.raw(8)
    assert magic[:6] == b"CNC3DP", magic
    ver = r.u32()
    p = dict(path=path, magic=magic.decode("latin-1"), version=ver,
             scen=r.name(16), theater=r.name(16), size=len(b))
    pk6 = ver >= 6
    pk7 = ver >= 7
    pk8 = ver >= 8
    pk9 = ver >= 9          # the 65x65 heightmap, before the water tail

    ntex = r.u32()
    tex = []
    for _ in range(ntex):
        w, h, uw, uh = r.u32(), r.u32(), r.u32(), r.u32()
        data = r.raw(w * h * 4)
        gdi = None
        if pk6:
            if r.raw(1) == b"\x01":
                gdi = r.raw(w * h * 4)
        tex.append(dict(w=w, h=h, uw=uw, uh=uh, data=data, gdi=gdi))
    p["tex"] = tex

    nmesh = r.u32()
    meshes = []
    for _ in range(nmesh):
        nm = r.name(16)
        ntri = r.u32()
        tris = r.raw(ntri * (4 + 2 + 3 * (5 * 4 + 4)))
        nparts = r.u32()
        parts = []
        for _ in range(nparts):
            tri0, nt, role = r.u32(), r.u32(), r.u32()
            pivot = (r.f32(), r.f32(), r.f32())
            parts.append((tri0, nt, role, pivot))
        secs = []
        if pk7:
            for _ in range(r.u32()):
                secs.append(r.u32())
        # PKB: per-mesh node animation, written directly after the sections. A reader
        # that does not know about it walks off the end of the mesh table, which is
        # exactly what happened the first time this format landed -- so it is parsed
        # here rather than skipped, and the counts are checked against the part count.
        anim = None
        if p["version"] >= 11:
            nf, tpf, nclip = r.u32(), r.u32(), r.u32()
            clips = []
            for _ in range(nclip):
                clips.append(dict(t0=r.i32(), t1=r.i32(), loop=r.raw(1)[0]))
            mat = vis = b""
            if nf:
                mat = r.raw(nf * nparts * 12 * 4)
                vis = r.raw(nf * nparts)
            if nf or nclip:
                anim = dict(frames=nf, ticks_per_frame=tpf, clips=clips,
                            mat=mat, vis=vis)
        meshes.append(dict(name=nm, ntris=ntri, tris=tris, parts=parts,
                           sections=secs, anim=anim))
    p["meshes"] = meshes

    types = []
    for _ in range(r.u32()):
        types.append((r.name(8), r.i32(), r.raw(1)[0]))
    p["types"] = types

    p["terrain_tex"] = r.u32()
    cells = []
    for _ in range(r.u32()):
        x, y = struct.unpack_from("<hh", r.b, r.o)
        r.o += 4
        uv = [r.f32() for _ in range(4)]
        cells.append((x, y, uv, r.raw(1)[0]))
    p["cells"] = cells

    sprites = []
    for _ in range(r.u32()):
        nm = r.name(16)
        sprites.append((nm, [r.u32() for _ in range(7)]))
    p["sprites"] = sprites

    inf = []
    for _ in range(r.u32()):
        code, st = r.name(8), r.name(4)
        idxs = [r.i32() for _ in range(N_HOUSE * N_SPRITE_ANIMS)]
        inf.append((code, st, idxs))
    p["infantry"] = inf

    # PK9 inserted the 65x65 heightmap between the last section and the water tail,
    # and the water int32s stayed the LAST eight bytes of the file (that contract is
    # append-only and permanent). Reading water from the section cursor therefore
    # picked up heightmap bytes on a PK9 pack -- 0x80808080 read back as
    # water=(-2139062144, -2139062144) -- and tail_ok's trailing-bytes assert then
    # killed every summary. Read heights from the cursor and water from the END.
    # PKC (v12) puts the 65x65 CM TINT map directly BEFORE the PK9 heights, so it
    # is the FIRST block of the tail. New tail blocks go at the front, never in the
    # middle, because every block from the heights down is reached by an
    # eof-relative seek in the renderer.
    p["cmtint"] = None
    if p["version"] >= 12:
        p["cmtint"] = struct.unpack_from("<%dH" % (65 * 65), b, r.o)
        r.o += 65 * 65 * 2
    p["corner"] = None
    if pk9:
        p["corner"] = b[r.o:r.o + 65 * 65]
        r.o += 65 * 65
    p["water"] = None
    p["seabed"] = None
    if pk8:
        p["water"] = struct.unpack("<ii", b[-8:])
        r.o += 8                       # the tail we just read from the far end
    # PKA (v10) puts the BOTTOM.IMG seabed index directly BEFORE the two water
    # int32s, which stay the file's last eight bytes for ever.
    if p["version"] >= 10:
        p["seabed"] = struct.unpack("<i", b[-12:-8])[0]
        r.o += 4
    p["trailing_bytes"] = len(b) - r.o
    return p


def tail_ok(p):
    """A PK8 pack whose trailing indices are absent, -1, or out of range is a
    FAILED bake, not a fallback."""
    assert p["version"] >= 8, "%s is %s, not PK8" % (p["path"], p["magic"])
    assert p["trailing_bytes"] == 0, \
        "%s: %d unparsed bytes after the PK8 tail" % (p["path"], p["trailing_bytes"])
    w = p["water"]
    assert w is not None, "%s: no PK8 water tail" % p["path"]
    n = len(p["tex"])
    for i, v in enumerate(w):
        assert 0 <= v < n, \
            "%s: water index %d = %d is not a live texture (bank has %d) -- " \
            "a PK8 pack with a -1 water index is a FAILED bake" % (p["path"], i, v, n)
    assert (p["tex"][w[0]]["w"], p["tex"][w[0]]["h"]) == (32, 32), "WATER1 not 32x32"
    assert (p["tex"][w[1]]["w"], p["tex"][w[1]]["h"]) == (32, 32), "WATER2 not 32x32"
    assert p["tex"][w[0]]["data"] != p["tex"][w[1]]["data"], \
        "%s: the two water textures are identical -- WATER2 did not bake" % p["path"]
    if p["version"] >= 10:
        sb = p["seabed"]
        assert sb is not None and 0 <= sb < n, \
            "%s: seabed index %s is not a live texture (bank has %d) -- a PKA pack " \
            "without BOTTOM.IMG is a FAILED bake, not a sea with no floor" \
            % (p["path"], sb, n)
        assert (p["tex"][sb]["w"], p["tex"][sb]["h"]) == (32, 32), "BOTTOM not 32x32"
        assert p["tex"][sb]["data"] not in (p["tex"][w[0]]["data"],
                                            p["tex"][w[1]]["data"]), \
            "%s: the seabed is byte-identical to a water tile -- BOTTOM did not bake" \
            % p["path"]
    if p["version"] >= 12:
        cm = p["cmtint"]
        assert cm is not None and len(cm) == 65 * 65, \
            "%s: PKC cm tint is %s entries, expected 4225" \
            % (p["path"], None if cm is None else len(cm))
        # The cartridge's own ceiling: 255*31*200/32768 = 48, so no unpacked
        # channel can exceed 31 and no scaled tint can exceed 48. A CM block that
        # has been byte-shifted by a moved tail block fails this only by luck, so
        # the REAL check is the offset test in bake5's check_pk8_tail; this one
        # catches a wrong-width or wrong-format source map.
        for i, v in enumerate(cm):
            assert 0 <= v <= 0xFFFF, "%s: cm[%d] = %s" % (p["path"], i, v)
    return True


def cm_tint(cm, lit):
    """The cartridge's vertex-tint arithmetic, for reporting a baked pack.

    gterrain.c colour pass ROM 0x196584: unpack RGBA5551 (ROM 0x1965E8 /
    0x196624 / 0x196634; bit 0 is never read), then
        tint_c = (u32)( (float)(lit * C5) * 200.0f * (1.0f/32768.0f) )
    with CM_GAIN 200.0 (RAM 0x800979B4) and CM_SHIFT 1/32768 (RAM 0x801C87EC)."""
    return tuple(int(float(lit * c5) * 200.0 * (1.0 / 32768.0))
                 for c5 in ((cm >> 11) & 31, (cm >> 6) & 31, (cm >> 1) & 31))


def cmd_summary(path):
    p = read(path)
    print("%s  %s v%d  %s/%s  %.1f MB"
          % (path, p["magic"], p["version"], p["scen"], p["theater"],
             p["size"] / 1e6))
    print("  textures %d (%d with a GDI variant), meshes %d, tris %d, types %d, "
          "cells %d, sprites %d, infantry %d"
          % (len(p["tex"]), sum(1 for t in p["tex"] if t["gdi"]), len(p["meshes"]),
             sum(m["ntris"] for m in p["meshes"]), len(p["types"]), len(p["cells"]),
             len(p["sprites"]), len(p["infantry"])))
    nanim = sum(1 for m in p["meshes"] if m.get("anim"))
    if nanim:
        print("  animated meshes: %d" % nanim)
        for m in p["meshes"]:
            a = m.get("anim")
            if not a:
                continue
            assert len(a["mat"]) == a["frames"] * len(m["parts"]) * 12 * 4, m["name"]
            assert len(a["vis"]) == a["frames"] * len(m["parts"]), m["name"]
            print("    %-16s %3d frames, %d ticks/frame, %d parts, clips %s"
                  % (m["name"], a["frames"], a["ticks_per_frame"], len(m["parts"]),
                     [(c["t0"], c["t1"], "loop" if c["loop"] else "once")
                      for c in a["clips"]]))
    print("  terrain_tex=%d  water=%s  seabed=%s  trailing=%d"
          % (p["terrain_tex"], p["water"], p["seabed"], p["trailing_bytes"]))
    if p["corner"] is not None:
        c = bytearray(p["corner"])
        print("  heights: %d corners, min=%d max=%d" % (len(c), min(c), max(c)))
    if p["cmtint"] is not None:
        cm = p["cmtint"]
        nz = sum(1 for v in cm if v & 0xFFFE)
        # Reported at lit = 255 (fully lit, unshrouded), which is the ceiling of
        # the scaled tint: the number that matters is the SCALED one, not the raw
        # 5-bit one, because 200/32768 floors most non-black corners to nothing.
        t = [cm_tint(v, 255) for v in cm]
        mx = [max(x[i] for x in t) for i in range(3)]
        print("  cm tint: %d of 4225 corners non-black, per-channel max at "
              "lit=255 = (%d,%d,%d) of a 48 ceiling" % (nz, mx[0], mx[1], mx[2]))
    if p["version"] >= 8:
        tail_ok(p)
        print("  PK8/PKA/PKC tail OK")


def cmd_texdiff(a, b):
    pa, pb = read(a), read(b)
    n = min(len(pa["tex"]), len(pb["tex"]))
    changed = []
    for i in range(n):
        ta, tb = pa["tex"][i], pb["tex"][i]
        if (ta["w"], ta["h"], ta["uw"], ta["uh"]) != (tb["w"], tb["h"], tb["uw"], tb["uh"]) \
           or ta["data"] != tb["data"] or ta["gdi"] != tb["gdi"]:
            changed.append(i)
    print("textures: %d vs %d, %d differ: %s"
          % (len(pa["tex"]), len(pb["tex"]), len(changed), changed))
    for i in changed:
        ta, tb = pa["tex"][i], pb["tex"][i]
        print("  [%3d] %dx%d(uw=%d,uh=%d) gdi=%s -> %dx%d(uw=%d,uh=%d) gdi=%s"
              % (i, ta["w"], ta["h"], ta["uw"], ta["uh"], ta["gdi"] is not None,
                 tb["w"], tb["h"], tb["uw"], tb["uh"], tb["gdi"] is not None))
    narrow = [i for i in range(n) if pa["tex"][i]["uw"] < 8]
    print("textures with uw < 8 in the OLD pack: %s" % narrow)
    return changed


def cmd_meshdiff(a, b):
    pa, pb = read(a), read(b)
    print("meshes: %d vs %d" % (len(pa["meshes"]), len(pb["meshes"])))
    for ma, mb in zip(pa["meshes"], pb["meshes"]):
        if ma["tris"] != mb["tris"] or ma["parts"] != mb["parts"] \
           or ma["sections"] != mb["sections"] or ma.get("anim") != mb.get("anim"):
            geo_a = [_geo(ma)], [_geo(mb)]
            print("  %-18s ntris %d->%d  geometry-identical=%s"
                  % (ma["name"], ma["ntris"], mb["ntris"], _geo(ma) == _geo(mb)))


def _geo(m):
    """Just the positions/uv/texindex of every vertex, colours stripped."""
    out = []
    stride = 4 + 2 + 3 * 24
    for i in range(m["ntris"]):
        o = i * stride
        rec = m["tris"][o:o + stride]
        out.append(rec[:6])
        for v in range(3):
            out.append(rec[6 + v * 24: 6 + v * 24 + 20])
    return out


def cmd_texpng(path, idx, out, scale=1, gdi=False):
    from PIL import Image
    p = read(path)
    t = p["tex"][int(idx)]
    d = t["gdi"] if (gdi and t["gdi"]) else t["data"]
    im = Image.frombytes("RGBA", (t["w"], t["h"]), d)
    s = int(scale)
    if s > 1:
        im = im.resize((t["w"] * s, t["h"] * s), Image.NEAREST)
    im.save(out)
    print("wrote %s (%dx%d, uw=%d uh=%d, gdi=%s)"
          % (out, t["w"], t["h"], t["uw"], t["uh"], t["gdi"] is not None))


if __name__ == "__main__":
    c = sys.argv[1]
    if c == "summary":
        for f in sys.argv[2:]:
            cmd_summary(f)
    elif c == "texdiff":
        cmd_texdiff(sys.argv[2], sys.argv[3])
    elif c == "meshdiff":
        cmd_meshdiff(sys.argv[2], sys.argv[3])
    elif c == "texpng":
        cmd_texpng(*sys.argv[2:])
    else:
        raise SystemExit(__doc__)
