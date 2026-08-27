#!/usr/bin/env python3
"""
mkmesh.py -- convert a scenario pack's MESH bank into the form tier1 draws.

The mesh bank, the texture bank and the type table are byte-identical across every
shipped pack of the same side (verified: SHA-1 over the shared region of all 36 packs
gives exactly two digests, one for the 13 Nod SCB packs and one for the 23 GDI SCG ones).
So this runs twice, ever, and the output is named for the side rather than the mission.

WHAT IS CONVERTED AND WHY

  RGBA8 -> 8-bit indices.  Same reason as the terrain: the rasteriser samples one byte
  per texel into a shade table, which is what makes it affordable on a Pentium II class
  machine. The OBJECT textures get their OWN palette, separate from the terrain's. That
  is not laziness, it is measured: quantising the object bank alone gives a far better
  fit than one palette shared with the terrain, and it costs nothing because a palette
  is a property of a texture bank rather than of the program (sr_use_shade is one
  pointer store between passes).

  Per-vertex RGBA -> a scalar light.  The rasteriser interpolates one brightness, not
  three channels. 82.9% of mesh vertices are already pure grey and collapse exactly.
  The other 17.1% desaturate, and the saturated reds and greens are the visible loss.
  Written down as an open question rather than hidden.

  Untextured triangles -> a 1x1 texture.  1,204 of 7,902 triangles carry texture -1 and
  are vertex-coloured only. Each becomes a one-texel texture holding the nearest palette
  index, so the inner loop never needs a branch for them.

  tools/win98/mkmesh.py <SCEN.pack> <out.t1mesh>
"""
import struct, sys, os
from collections import Counter

def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def i32(b, o): return struct.unpack_from("<i", b, o)[0]

def walk(blob):
    o = 8 + 4 + 16 + 16
    ntex = u32(blob, o); o += 4
    textures = []
    for _ in range(ntex):
        w, h, uw, uh = struct.unpack_from("<IIII", blob, o); o += 16
        data = o; o += w * h * 4
        flag = blob[o]; o += 1
        gdi = None
        if flag:
            gdi = o; o += w * h * 4
        textures.append([w, h, uw, uh, data, gdi])

    nmesh = u32(blob, o); o += 4
    meshes = []
    for _ in range(nmesh):
        name = blob[o:o+16].split(b"\0")[0].decode("latin-1"); o += 16
        ntri = u32(blob, o); o += 4
        tris = o; o += ntri * 78
        nparts = u32(blob, o); o += 4
        parts = o; o += nparts * 24
        nsec = u32(blob, o); o += 4
        secs = o; o += nsec * 4
        nf   = u32(blob, o); o += 4
        tpf  = u32(blob, o); o += 4
        nclip = u32(blob, o); o += 4
        clips = []
        for _k in range(nclip):
            clips.append((i32(blob, o), i32(blob, o + 4), blob[o + 8])); o += 9
        amat = avis = None
        if nf:
            amat = o; o += nf * nparts * 12 * 4
            avis = o; o += nf * nparts
        meshes.append((name, ntri, tris, nparts, parts, nsec, secs,
                       nf, tpf, clips, amat, avis))

    ntype = u32(blob, o); o += 4
    types = []
    for _ in range(ntype):
        code = blob[o:o+8].split(b"\0")[0].decode("latin-1"); o += 8
        mi = i32(blob, o); o += 4
        o += 1
        types.append((code, mi))
    return textures, meshes, types

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    blob = open(sys.argv[1], "rb").read()
    textures, meshes, types = walk(blob)
    print("textures=%d meshes=%d types=%d" % (len(textures), len(meshes), len(types)))

    # ---- which textures do the meshes actually use, and which are too big -----------
    used = set()
    tricount = 0
    for m in meshes:
        _n, ntri, tris = m[0], m[1], m[2]
        tricount += ntri
        for t in range(ntri):
            ti = i32(blob, tris + t * 78)
            if ti >= 0: used.add(ti)
    print("triangles=%d, distinct textures referenced by meshes=%d" % (tricount, len(used)))

    # ---- one palette for the object bank ---------------------------------------------
    cnt = Counter()
    for ti in sorted(used):
        w, h, uw, uh, data, g = textures[ti]
        # The GDI HOUSE VARIANT counts toward the palette too. The cartridge keeps two
        # texture sets for 64 of these -- the sand table at ROM 0x98F30 for GoodGuy and
        # the blue-grey one at 0x99130 for everyone else -- and quantising only the base
        # set would leave every GDI-only colour to be snapped to the nearest Nod one,
        # which is precisely the difference the whole feature exists to show.
        for src in ((data,) if g is None else (data, g)):
            px = blob[src:src + w * h * 4]
            for y in range(uh):
                row = y * w * 4
                for x in range(uw):
                    p = row + x * 4
                    if px[p + 3] >= 128:
                        cnt[(px[p], px[p+1], px[p+2])] += 1
    print("distinct opaque RGB across the object bank: %d" % len(cnt))

    order = [c for c, _ in cnt.most_common()]
    pal = order[:255]
    while len(pal) < 255: pal.append((0, 0, 0))
    lut = {c: i + 1 for i, c in enumerate(pal)}
    snapped = 0
    for c in order[255:]:
        best, bd = 1, 1 << 30
        for i, q in enumerate(pal):
            d = (c[0]-q[0])**2 + (c[1]-q[1])**2 + (c[2]-q[2])**2
            if d < bd: bd, best = d, i + 1
        lut[c] = best; snapped += cnt[c]
    total = max(sum(cnt.values()), 1)
    print("palette: 255 colours, %.3f%% of texels snapped" % (100.0 * snapped / total))

    def nearest(rgb):
        best, bd = 1, 1 << 30
        for i, q in enumerate(pal):
            d = (rgb[0]-q[0])**2 + (rgb[1]-q[1])**2 + (rgb[2]-q[2])**2
            if d < bd: bd, best = d, i + 1
        return best

    # ---- convert the referenced textures --------------------------------------------
    # index 0 stays transparent, which is what the cutout modes need.
    texrecs, texblob, remap = [], bytearray(), {}
    gdimap = {}                      # converted base index -> converted variant index

    def convert(ti, src):
        w, h, uw, uh = textures[ti][0], textures[ti][1], textures[ti][2], textures[ti][3]
        px = blob[src:src + w * h * 4]
        idx = bytearray(w * h)
        for y in range(uh):
            row = y * w * 4; orow = y * w
            for x in range(uw):
                p = row + x * 4
                if px[p + 3] >= 128:
                    idx[orow + x] = lut[(px[p], px[p+1], px[p+2])]
        out = len(texrecs)
        texrecs.append((w, h, uw, uh, len(texblob)))
        texblob.extend(idx)
        return out

    for ti in sorted(used):
        remap[ti] = convert(ti, textures[ti][4])
    for ti in sorted(used):
        g = textures[ti][5]
        if g is not None:
            gdimap[remap[ti]] = convert(ti, g)
    print("house variants: %d of %d referenced textures carry a GDI set"
          % (len(gdimap), len(used)))

    # ---- the SHADOW ALPHA PLANES ----------------------------------------------------
    #
    # THE ONE THING THAT MAKES SHADOWS POSSIBLE AT ALL, and the reason wiring a shadow
    # pass up without it would have drawn nothing.
    #
    # Every shadow texture in the cartridge is WHITE RGB with its intensity in the ALPHA
    # channel, and 34 of the 35 peak below alpha 128. The palettised conversion above
    # writes index 0 -- the transparent one -- for any texel under 128, so all 34 are
    # already fully transparent in the object bank, and the one exception (the SAM's,
    # peaking at 184) would come out as a solid WHITE plate on the ground. The card
    # reports success either way.
    #
    # So the MODE 2 textures get their own bank, one byte of alpha per texel, uploaded as
    # GR_TEXFMT_ALPHA_8. They are disjoint from every texture the other three modes use,
    # so nothing is stored twice.
    shadow_used = set()
    for m in meshes:
        _n, ntri, tris = m[0], m[1], m[2]
        for t in range(ntri):
            base = tris + t * 78
            if blob[base + 4] == 2:
                ti = i32(blob, base)
                if ti >= 0: shadow_used.add(ti)
    shmap, shrecs, shblob = {}, [], bytearray()
    for ti in sorted(shadow_used):
        w, h, uw, uh, data, g = textures[ti]
        px = blob[data:data + w * h * 4]
        plane = bytearray(w * h)
        for y in range(h):
            for x in range(w):
                plane[y * w + x] = px[(y * w + x) * 4 + 3]
        shmap[ti] = len(shrecs)
        shrecs.append((w, h, uw, uh, len(shblob)))
        shblob.extend(plane)
    print("shadow planes: %d, %d texels, %.1f KB"
          % (len(shrecs), len(shblob), len(shblob) / 1024.0))

    # ---- triangles ------------------------------------------------------------------
    # tier1 triangle, 80 bytes: i32 tex, u8 mode, u8 wrap, 2 pad, then 3 x 24-byte
    # vertices of { f32 x,y,z,u,v; u8 r,g,b,light }. Padded to 4-byte fields on purpose:
    # the source pack is unaligned throughout because of its one-byte GDI flag, and the
    # whole point of converting is that the target does not have to care.
    #
    # EVERY MODE IS CONVERTED NOW, including 2 (the cartridge's baked shadow quads) and
    # 3 (translucent). They used to be dropped here, which put the decision in the wrong
    # place: the pack is the data and the renderer is what knows whether it can draw a
    # blended surface this frame. Dropping them at bake time also silently broke the
    # PART TABLE and the SECTION table, both of which index the original triangle list,
    # and it meant the shadow sets could never be turned on without re-baking.
    T1TRI = 80
    solid_cache = {}
    def solid_texture(rgb):
        """a 1x1 texture for an untextured triangle, so the inner loop stays branchless"""
        if rgb in solid_cache: return solid_cache[rgb]
        i = len(texrecs)
        texrecs.append((1, 1, 1, 1, len(texblob)))
        texblob.append(nearest(rgb))
        solid_cache[rgb] = i
        return i

    meshrecs = []
    tridata, partdata, secdata, animblob = bytearray(), bytearray(), bytearray(), bytearray()
    modehist = Counter()
    nparts_total = 0
    nonvert = 0
    for (name, ntri, tris, nparts, parts, nsec, secs,
         nf, tpf, clips, amat, avis) in meshes:
        first = len(tridata) // T1TRI
        for t in range(ntri):
            base = tris + t * 78
            ti   = i32(blob, base)
            mode = blob[base + 4]
            wrap = blob[base + 5]
            modehist[mode] += 1
            vs = []
            for v in range(3):
                p = base + 6 + v * 24
                x, y, z, u, vv = struct.unpack_from("<fffff", blob, p)
                r, g, b, a = blob[p+20], blob[p+21], blob[p+22], blob[p+23]
                if not (r == g == b): nonvert += 1
                vs.append((x, y, z, u, vv, r, g, b))
            if mode == 2 and ti >= 0 and ti in shmap:
                # A shadow triangle names a plane, not a colour texture. The index is
                # stored with the top bit set so one field carries both banks and an
                # older renderer cannot read it as an ordinary texture by accident.
                nti = 0x40000000 | shmap[ti]
                w, h, uw, uh, _off = shrecs[shmap[ti]]
                us, vs_ = float(uw), float(uh)
            elif ti >= 0:
                nti = remap[ti]
                w, h, uw, uh, _off = texrecs[nti]
                us, vs_ = float(uw), float(uh)
            else:
                avg = tuple(sum(v[5+k] for v in vs) // 3 for k in range(3))
                nti = solid_texture(avg)
                us = vs_ = 1.0
            tridata += struct.pack("<iBBBB", nti, mode, wrap, 0, 0)
            for (x, y, z, u, vv, r, g, b) in vs:
                tridata += struct.pack("<fffff", x, y, z, u * us, vv * vs_)
                # THE VERTEX COLOUR, all three channels, plus the grey the software
                # rasteriser wants. The scalar used to be the only thing stored, which
                # desaturated 17.5% of the bank -- the cartridge's untextured wheels,
                # muzzle flashes and coloured trim -- and there was no way to get it back
                # without re-baking. Both are here now; each backend reads what it can use.
                tridata += struct.pack("<BBBB", r, g, b, (r + g + b) // 3)

        # ---- the part table ---------------------------------------------------------
        # Parts are contiguous ranges of the triangle list and are copied 1:1, INCLUDING
        # empty ones. That alignment is not cosmetic: the animation block is indexed by
        # part number, so dropping a part here would silently shift every node's pose.
        pfirst = len(partdata) // 24
        for k in range(nparts):
            base = parts + k * 24
            tri0 = u32(blob, base)
            ntris = u32(blob, base + 4)
            role = u32(blob, base + 8)
            px, py, pz = struct.unpack_from("<fff", blob, base + 12)
            partdata += struct.pack("<IIIfff", first + tri0, ntris, role, px, py, pz)
        nparts_total += nparts

        # ---- sections: the cartridge's own construction pieces -----------------------
        # One entry per G_VTX batch of the display list, ascending first-triangle indices.
        # Drawing only the first K of them is how the console assembles a building piece
        # by piece, which is what the build-up animation is.
        sfirst = len(secdata) // 4
        for k in range(nsec):
            secdata += struct.pack("<I", first + u32(blob, secs + k * 4))

        # ---- node animation ----------------------------------------------------------
        # The baker resampled every track onto a fixed frame grid and wrote a DELTA
        # (rest-inverse composed with the frame's pose), because our triangles are stored
        # already posed by their node's rest transform. So the renderer multiplies a
        # posed vertex by this and identity means "the picture we draw today".
        aoff = 0
        if nf and nparts:
            aoff = len(animblob)
            animblob += blob[amat:amat + nf * nparts * 12 * 4]
            animblob += blob[avis:avis + nf * nparts]
        else:
            nf = 0
        c0 = clips[0] if clips else (0, nf, 1)
        meshrecs.append((name, first, ntri, pfirst, nparts, sfirst, nsec,
                         nf, tpf if tpf > 0 else 1, aoff, c0[0], c0[1], c0[2]))
        assert len(tridata) % T1TRI == 0, \
            "triangle record is not %d bytes (%d left over)" % (T1TRI, len(tridata) % T1TRI)

    roles = Counter()
    for i in range(len(partdata) // 24):
        roles[struct.unpack_from("<I", partdata, i * 24 + 8)[0]] += 1
    print("parts: %d (role 0 static=%d, 1 turret=%d, 2 rotor=%d)"
          % (nparts_total, roles[0], roles[1], roles[2]))
    print("modes: %s  (0 opaque, 1 cutout, 2 shadow, 3 translucent)" % dict(modehist))
    print("sections: %d, non-grey vertices %d" % (len(secdata) // 4, nonvert))
    nanim = sum(1 for m in meshrecs if m[7])
    print("animation: %d meshes carry clips, %.1f KB of baked pose"
          % (nanim, len(animblob) / 1024.0))
    kept_total = len(tridata) // T1TRI
    assert kept_total == tricount, \
        "converted %d triangles but the pack has %d" % (kept_total, tricount)
    print("converted all %d triangles" % kept_total)

    palette = bytearray(768)
    for i, c in enumerate(pal):
        palette[(i+1)*3+0], palette[(i+1)*3+1], palette[(i+1)*3+2] = c
    # Index 0 is the reserved transparent one. It is MAGENTA on purpose, not black.
    # The software path skips it via SR_Texture.ckey and the Voodoo skips it by
    # chroma-key, and if either is ever misconfigured the result is a screen full of
    # magenta rather than black cutouts that read as correct-but-dark artwork. Leaving
    # it black cost exactly that: the tree line rendered as black silhouettes on the
    # card and looked plausible enough to nearly pass.
    palette[0], palette[1], palette[2] = 255, 0, 255
    with open(sys.argv[2], "wb") as f:
        f.write(b"T1MESH05"); f.write(struct.pack("<I", 5))
        f.write(palette)
        f.write(struct.pack("<I", len(texrecs)))
        for (w, h, uw, uh, off) in texrecs:
            f.write(struct.pack("<IIIII", w, h, uw, uh, off))
        f.write(struct.pack("<I", len(texblob))); f.write(bytes(texblob))
        # The house map, right after the textures it indexes: base -> GDI variant.
        f.write(struct.pack("<I", len(gdimap)))
        for base in sorted(gdimap):
            f.write(struct.pack("<II", base, gdimap[base]))
        # Mesh records are 64 bytes and self-describing: everything that follows is a
        # flat pool the record indexes into.
        f.write(struct.pack("<I", len(meshrecs)))
        for (name, first, n, pf, pn, sf, sn, nf, tpf, aoff, t0, t1, lp) in meshrecs:
            # 15 rather than 16 is deliberate here and safe: mesh NAMES are only ever
            # printed, never compared, and the pack's own longest is 10 characters.
            f.write(name.encode("latin-1")[:15].ljust(16, b"\0"))
            f.write(struct.pack("<IIIIIIIIIiiI", first, n, pf, pn, sf, sn,
                                nf, tpf, aoff, t0, t1, lp))
        f.write(struct.pack("<I", len(secdata) // 4));  f.write(bytes(secdata))
        f.write(struct.pack("<I", len(partdata) // 24)); f.write(bytes(partdata))
        f.write(struct.pack("<I", len(tridata) // T1TRI)); f.write(bytes(tridata))
        f.write(struct.pack("<I", len(animblob)));      f.write(bytes(animblob))
        # The shadow alpha planes: one byte per texel, no palette, referenced only by
        # MODE 2 triangles through an index with bit 30 set.
        f.write(struct.pack("<I", len(shrecs)))
        for (w, h, uw, uh, off) in shrecs:
            f.write(struct.pack("<IIIII", w, h, uw, uh, off))
        f.write(struct.pack("<I", len(shblob)));        f.write(bytes(shblob))
        f.write(struct.pack("<I", len(types)))
        for (code, mi) in types:
            # EIGHT characters, not seven. The record is 8 bytes and the reader compares
            # 8 with strncmp, so a name that fills it exactly needs no terminator -- but
            # this wrote [:7], which silently renamed every 8-character code. There are
            # two: ATOMICUP and PROCANIM, and PROCANIM is the refinery's animation model.
            # So the refinery rig has been reported ABSENT and blamed on the bake since
            # the day it was written, when what was missing was one character.
            f.write(code.encode("latin-1")[:8].ljust(8, b"\0"))
            f.write(struct.pack("<i", mi))
    print("wrote %s (%.2f MB)" % (sys.argv[2], os.path.getsize(sys.argv[2]) / 1e6))
    return 0

if __name__ == "__main__":
    sys.exit(main())
