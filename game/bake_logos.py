#!/usr/bin/env python3
"""Bake the cartridge's two faction logos into playable/logos.pack.

WHAT THIS IS FOR
    The score screen ends a mission with a slowly spinning faction logo in the
    top right. 1995 had a flat LOGOS.SHP for Nod only (score.cpp:775-776 draws
    frame 1 at 0,0 when house == HOUSE_BAD) and nothing at all for GDI. The N64
    cartridge carries a real 3D model for BOTH sides, used by the briefing code,
    so the score screen can show the thing the cartridge already had.

WHERE THEY LIVE
    The briefing code's own name table pairs a node pointer with a name string:
        nodes at ROM 0x01DEBE0, names at ROM 0x01DECE0, 16 entries
    Two of those entries are BRF_LOGO_GDI and BRF_LOGO_NOD. Both are leaves --
    NULL handle, no children, no mount transform -- so the display list is the
    whole model and there is no hierarchy to compose. That is why this baker can
    call build_mesh once per logo and stop.

    Vertex RGB bytes on these two are NORMALS, not colours (build_mesh reports
    kind == 'normal'), so the runtime lights them rather than showing baked
    shading. The assert below pins that: if a future ROM revision ships colours
    instead, this fails loudly rather than drawing a black logo.

OUTPUT FORMAT (little endian; app/logo3d.c is the only reader)
    "C3DLOGO1"  u32 nlogo
    per logo:   char name[8]           "GDI" / "NOD", NUL padded
                float radius           max distance of any vertex from centre,
                                       after centring -- the runtime fits this
                                       to the on-screen box, so the two logos
                                       come out the same visual size
                u32 ntex
                  per tex: u16 w, u16 h, then w*h RGBA bytes
                u32 nbatch             triangles grouped by material
                  per batch: u32 tex   (0xFFFFFFFF = untextured)
                             u32 nvert (always a multiple of 3)
                             nvert * float[8] = x,y,z, nx,ny,nz, u,v
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "romdump"))

import fbx_export as FX          # noqa: E402
import textures as TX            # noqa: E402

# THE THIRD ENTRY IS BOTH LOGOS IN ONE MODEL. GDI_LOGO_TITLE_ROOT is the title
# screen's, and it is not a third emblem: its SIX materials are exactly the GDI
# logo's two (0x1E14E0, 0x1E48E0) plus the Nod logo's three (0x1E24E0, 0x1E2CE0,
# 0x1E34E0) plus one untextured, and it is 68 units thick in Z against the briefing
# logos' 36 and 46. It is the two emblems back to back on one disc -- which is the
# version with a BACKSIDE, and the one to draw when a logo is going to turn all the
# way round. Face it at 0 degrees for GDI and at 180 for Nod.
#
# The display lists, from the briefing name table described above.
LOGOS = [("GDI", 0x01E9A50), ("NOD", 0x01EF058), ("TITLE", 0x01F52A0)]

# What the ROM held when this was written. A mismatch means the extraction moved
# under us, and a silently different logo is worse than a failed bake.
EXPECT = {"GDI": (464, 908, 2), "NOD": (458, 717, 3), "TITLE": (581, 978, 6)}


def bake_one(rom, name, off):
    m = FX.build_mesh(rom, off)
    got = (len(m["positions"]), len(m["polys"]), len(m["mats"]))
    assert got == EXPECT[name], "%s: expected %s positions/tris/mats, got %s" % (
        name, EXPECT[name], got)
    assert m["kind"] == "normal", "%s: vertex RGB is %s, not normals" % (name, m["kind"])

    # Centre on the bounding box and measure the radius, so both logos fill the
    # same on-screen box whatever the cartridge's own units were.
    xs = [p[0] for p in m["positions"]]
    ys = [p[1] for p in m["positions"]]
    zs = [p[2] for p in m["positions"]]
    cx, cy, cz = ((min(v) + max(v)) / 2.0 for v in (xs, ys, zs))
    radius = max(((x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2) ** 0.5
                 for x, y, z in m["positions"]) or 1.0

    # Textures, deduplicated by ROM address so a material list that binds the same
    # tile twice does not upload it twice.
    texlist, texidx = [], {}
    for mat in m["mats"]:
        t = mat["tex"]
        if t is None:
            continue
        key = (t["rom"], t["fmt"], t["siz"], t["w"], t["h"], t["pal"])
        if key in texidx:
            continue
        rows = TX.decode_texture(rom, t["rom"], t["fmt"], t["siz"], t["w"], t["h"], t["pal"])
        assert rows is not None, "%s: texture at %#x did not decode" % (name, t["rom"])
        texidx[key] = len(texlist)
        texlist.append((t["w"], t["h"], b"".join(bytes(r) for r in rows)))

    def mat_tex(mi):
        t = m["mats"][mi]["tex"]
        if t is None:
            return 0xFFFFFFFF
        return texidx[(t["rom"], t["fmt"], t["siz"], t["w"], t["h"], t["pal"])]

    # Group triangles by material, keeping first-appearance order so the result is
    # byte-identical run to run.
    batches, order = {}, []
    for fi, tri in enumerate(m["polys"]):
        mi = m["poly_mat"][fi]
        if mi not in batches:
            batches[mi] = []
            order.append(mi)
        for k in range(3):
            vi = fi * 3 + k
            x, y, z = m["positions"][tri[k]]
            nx, ny, nz = m["poly_nrm"][vi]
            u, v = m["poly_uv"][vi]
            batches[mi].append((x - cx, y - cy, z - cz, nx, ny, nz, u, v))

    blob = struct.pack("<8sfI", name.encode(), radius, len(texlist))
    for w, h, px in texlist:
        blob += struct.pack("<HH", w, h) + px
    blob += struct.pack("<I", len(order))
    for mi in order:
        verts = batches[mi]
        blob += struct.pack("<II", mat_tex(mi), len(verts))
        for vt in verts:
            blob += struct.pack("<8f", *vt)
    print("  %-3s %3d verts  %3d tris  %d batches  %d tex  radius %.1f" % (
        name, len(m["positions"]), len(m["polys"]), len(order), len(texlist), radius))
    return blob


def main():
    # NEXT TO THIS SCRIPT, in game/, which is where bake_efx.py and its siblings put
    # theirs. That is not a style preference: game/make-build.sh's pack-refresh loop
    # scans ../tools/bakery/game and game/ and copies every .pack it finds, so a pack
    # written anywhere else is never shipped. This defaulted to playable/ when it was
    # written, which worked on this machine and would have shipped a build with no
    # logos.pack in it -- and BOTH binaries want it now, the app for the score screen
    # and the renderer for the radar bezel.
    out = os.path.join(HERE, "logos.pack")
    if len(sys.argv) > 1:
        out = sys.argv[1]
    rom = FX.load_rom()
    print("baking faction logos")
    blob = struct.pack("<8sI", b"C3DLOGO1", len(LOGOS))
    for name, off in LOGOS:
        blob += bake_one(rom, name, off)
    with open(out, "wb") as f:
        f.write(blob)
    print("wrote %s (%d bytes)" % (out, len(blob)))


if __name__ == "__main__":
    main()
