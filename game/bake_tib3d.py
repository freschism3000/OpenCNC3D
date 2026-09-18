#!/usr/bin/env python3
"""
bake_tib3d.py -- bake solid tiberium crystals into `tib3d.pack`, so the Enhanced
renderer can stand real geometry on a tiberium cell instead of only laying the
cartridge's flat decal on it.

WHERE THE ART COMES FROM, AND WHAT IT IS NOT

It is not the cartridge's. Command & Conquer on the N64 has no tiberium model:
the overlay is twelve 24x24 filmstrips (ANY_TI01..12) drawn flat on the cell, and
bake_dostiberium.py already ships them. There is nothing in the ROM to decode
into a third dimension, so anything standing up here is IMPORTED and says so.

The source is a third-party mod archive, `data/mods/original-tiberium-v4/
TiberiumMod_0.1.big`, distributed as TiberiumMod.rar (sha256
a63d3a64a054da61e28f62d8d41932c2f94120961c2bade8efa229f2244b3d6c) from
https://www.moddb.com/mods/original-tiberium-mod/downloads/original-tiberium-v4
It is a Command & Conquer 3 mod, so its payload is a SAGE compiled asset stream
rather than loose model files. **It carries no licence, so this pack is a
research spike and not shippable art.** The gap log states the terms.

THE CONTAINER, DECODED RATHER THAN GUESSED

  BIG4        u32le archive size, u32be file count, u32be header size, then per
              file u32be offset, u32be size and a NUL-terminated name.

  .manifest   u8 big-endian flag, u8, u16 version (5), then eleven u32le:
              stream checksum, all-types hash, asset count, total instance size,
              max instance/relocation/imports chunk, and the four string buffer
              sizes. Then `count` 44-byte entries -- type id, instance id, type
              hash, instance hash, asset-reference offset and count, name offset,
              source-name offset, instance size, relocation size, imports size --
              followed by the asset-reference, referenced-manifest, name and
              source-name buffers in that order. The four sizes sum with the
              table to the file length exactly, which is how the layout was
              confirmed.

  .bin        u32le stream checksum, then every asset's instance chunk in
              manifest order, each padded up to a 4-byte boundary. The final
              offset lands on the file length exactly. .relo and .imp carry the
              same 4-byte checksum and the same per-asset order.

  relocation  a chunk of u32le byte offsets into the instance data. Each names a
              word holding a pointer, stored as an offset from the instance
              start. Imports are the same shape, and name the words that point
              at another asset.

  W3DMesh     the instance is a memory image. Confirmed layout, all offsets from
              the instance start:
                0x0c  float[3] bounding box min
                0x18  float[3] bounding box max
                0x34  u32 triangle count
                0x38  Triangle* triangles
                0x3c  u32 length, char* effect name ("ObjectsGeneric.fx")
              A Triangle is 24 bytes: u32 index count (always 3), u32* indices,
              float[3] face normal, float plane distance.
              The vertex block is {u32 count, u32 stride, Vertex* data}; it is
              found by looking for the relocated word whose two preceding words
              are the vertex count implied by the indices and a plausible stride,
              and there is exactly one such word in every mesh here. Stride 60 is
              position, normal, RGBA, tangent, binormal, UV; stride 36 drops the
              tangent pair. Both were checked by measuring the length of every
              candidate vector: normal, tangent and binormal all come back 1.000.

  Texture     the instance is u32 0, u32 offset (12), u32 size, then a DDS file.
              CBCrystal is 512x512 DXT1 with ten mip levels and no alpha, which
              is why the crystals draw opaque with depth writes on.

WHAT EACH SOURCE MESH ACTUALLY HOLDS

Every one of the four is THREE identical copies of one clump, laid side by side:
splitting the triangles into connected components and grouping those by centroid
gives three groups of exactly equal triangle count in all four meshes. So one
copy is taken and the other two are dropped.

  CBTIBERIUM.STAGE01    110 tris   a dark rocky vent, no green on it
  CBTIBERIUM.CSTAGE02    84 tris   two crystal shards standing up
  CBTIBERIUM.CSTAGE03   200 tris   a fan of shards
  CBTIBERIUM.CSTAGE01   252 tris   a dense crystal bush, and the tallest

They are written in that order, which is the growth order the renderer reads
them in. CBTIBBL is the same geometry with a blue texture and is not baked: all
twelve Tiberian Dawn overlays are one green mineral, so there is nothing for a
second colour to mean here yet.

AXES AND SCALE

Source is Z-up, X east, Y north. The renderer is Y-up, X east, Z south, so
(X, Y, Z) -> (X, Z, -Y). That mapping has determinant +1, so the winding is
unchanged and no triangle has to be flipped.

Every clump is recentred on its own horizontal bounding box and dropped so its
lowest point is y = 0, then all four are divided by ONE common number -- the
tallest clump's height -- so the relative sizes survive and the renderer's size
dial means "how tall the biggest clump stands, in cells".

THE TEXTURE

The whole 512x512 sheet is box-filtered to 256x256 RGBA and the UVs are left
alone, so the sheet stays a power of two at the 3dfx Voodoo 2's 256-per-side
limit and needs no Tier 1 variant. Half of it is the rock the seed stage uses,
so cropping to the green quadrant would cost a clump.

AND IT IS LIFTED, which is a deviation and is why --lift exists. The sheet was
painted for a shader with an emissive term and a specular map, neither of which
this renderer has: the crystal half measures a mean of (20, 95, 31), so once a
ground shade and a lambert have multiplied it the crystals draw very nearly
black, which is what the first render showed. A gamma of 1.8 raises that to
(59, 142, 74) and clips nothing at all -- the 5th percentile moves 17/75/37 and
the highlights stay where they were, which a linear gain could not do (2.0x
clips 8% of the quadrant). --lift 1 writes the sheet exactly as it was decoded.

    python3 game/bake_tib3d.py -o game/tib3d.pack
    python3 game/bake_tib3d.py --report        measure, write nothing
"""

import argparse
import math
import os
import struct
import sys

# ANCHORED TO THIS FILE, not to the working directory. make-build.sh runs the bakers
# from game/ and a release runs them from the repo root, and a default that is right in
# one place and missing in the other is a build that quietly ships without the art.
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(_HERE)
DEFAULT_BIG = os.path.join(_REPO, 'data', 'mods', 'original-tiberium-v4',
                           'TiberiumMod_0.1.big')
DEFAULT_OUT = os.path.join(_HERE, 'tib3d.pack')

# The four clumps, in growth order. The renderer reads index 0 as the sparsest.
WANTED = ['W3DMesh:CBTIBERIUM.STAGE01',
          'W3DMesh:CBTIBERIUM.CSTAGE02',
          'W3DMesh:CBTIBERIUM.CSTAGE03',
          'W3DMesh:CBTIBERIUM.CSTAGE01']
WANTED_TEX = 'Texture:CBCrystal'
TEX_SIDE = 256



# ---- the BIG archive ----------------------------------------------------------------

def read_big(path):
    d = open(path, 'rb').read()
    if d[:4] != b'BIG4':
        raise SystemExit('%s is not a BIG4 archive' % path)
    count, hdr = struct.unpack('>II', d[8:16])
    off = 16
    out = {}
    for _ in range(count):
        o, s = struct.unpack('>II', d[off:off + 8])
        off += 8
        e = d.index(b'\0', off)
        name = d[off:e].decode('latin1').replace('\\', '/')
        off = e + 1
        out[name] = d[o:o + s]
    return out


# ---- the compiled asset stream ------------------------------------------------------

class Stream(object):
    """One .manifest / .bin / .relo / .imp set, split into per-asset chunks."""

    def __init__(self, files):
        m = files['data/mod.manifest']
        count = struct.unpack('<I', m[12:16])[0]
        aref, refman, namebuf, srcbuf = struct.unpack('<4I', m[32:48])
        ent_at, ent_size = 0x30, 44
        ents = [struct.unpack('<11I', m[ent_at + i * ent_size:
                                        ent_at + (i + 1) * ent_size])
                for i in range(count)]
        names_at = ent_at + count * ent_size + aref + refman
        if names_at + namebuf + srcbuf != len(m):
            raise SystemExit('manifest string buffers do not close the file')

        def name(off):
            e = m.index(b'\0', names_at + off)
            return m[names_at + off:e].decode('latin1')

        bn, rl, im = (files['data/mod.bin'], files['data/mod.relo'],
                      files['data/mod.imp'])
        chk = struct.unpack('<I', m[4:8])[0]
        for tag, blob in (('bin', bn), ('relo', rl), ('imp', im)):
            if struct.unpack('<I', blob[:4])[0] != chk:
                raise SystemExit('mod.%s does not carry the manifest checksum' % tag)

        self.inst, self.relo, self.imp = {}, {}, {}
        o = r = i = 4
        for e in ents:
            n = name(e[6])
            self.inst[n] = bn[o:o + e[8]]
            self.relo[n] = rl[r:r + e[9]]
            self.imp[n] = im[i:i + e[10]]
            o = (o + e[8] + 3) & ~3
            r += e[9]
            i += e[10]
        if (o, r, i) != (len(bn), len(rl), len(im)):
            raise SystemExit('the asset chunks do not close bin/relo/imp')


# ---- one W3DMesh --------------------------------------------------------------------

def decode_mesh(stream, name):
    d = stream.inst[name]
    rb = stream.relo[name]
    rel = set(struct.unpack('<%dI' % (len(rb) // 4), rb)) if rb else set()

    def u32(o):
        return struct.unpack('<I', d[o:o + 4])[0]

    tn, tp = u32(0x34), u32(0x38)
    tris = []
    for i in range(tn):
        o = tp + i * 24
        n, ip = u32(o), u32(o + 4)
        if n != 3:
            raise SystemExit('%s: a face with %d indices; this reader wants triangles'
                             % (name, n))
        tris.append(tuple(u32(ip + k * 4) for k in range(3)))
    nvert = max(max(t) for t in tris) + 1

    block = None
    for r in sorted(rel):
        if r < 8 or r + 4 > len(d):
            continue      # a chunk can carry a terminator that is not an offset
        if u32(r - 8) == nvert and 12 <= u32(r - 4) <= 128 \
           and u32(r) + nvert * u32(r - 4) <= len(d):
            if block is not None:
                raise SystemExit('%s: more than one candidate vertex block' % name)
            block = (u32(r - 4), u32(r))
    if block is None:
        raise SystemExit('%s: no vertex block found' % name)
    stride, at = block

    pos, nrm, uv = [], [], []
    for i in range(nvert):
        o = at + i * stride
        pos.append(struct.unpack('<3f', d[o:o + 12]))
        nrm.append(struct.unpack('<3f', d[o + 12:o + 24]))
        uv.append(struct.unpack('<2f', d[o + stride - 8:o + stride]))
    return dict(name=name, pos=pos, nrm=nrm, uv=uv, tris=tris, stride=stride)


def one_copy(mesh, copies=3):
    """The source meshes are `copies` identical clumps side by side. Return the
       triangles of one of them, chosen as the group nearest the origin so the
       same clump comes back on every run."""
    n = len(mesh['pos'])
    par = list(range(n))

    def find(a):
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a

    def union(a, b):
        a, b = find(a), find(b)
        if a != b:
            par[a] = b

    seen = {}
    for i, p in enumerate(mesh['pos']):
        k = (round(p[0], 3), round(p[1], 3), round(p[2], 3))
        if k in seen:
            union(i, seen[k])
        else:
            seen[k] = i
    for t in mesh['tris']:
        union(t[0], t[1])
        union(t[0], t[2])

    parts = {}
    for t in mesh['tris']:
        parts.setdefault(find(t[0]), []).append(t)

    items = []
    for ts in parts.values():
        idx = sorted({i for t in ts for i in t})
        cx = sum(mesh['pos'][i][0] for i in idx) / len(idx)
        cy = sum(mesh['pos'][i][1] for i in idx) / len(idx)
        items.append(((cx, cy), ts))

    # Farthest-first seeds then Lloyd, so the split does not depend on the order
    # the components came out of the union-find.
    def dist(a, b):
        return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5

    cent = [min(items, key=lambda it: it[0][0])[0]]
    while len(cent) < copies:
        cent.append(max((it[0] for it in items),
                        key=lambda c: min(dist(c, s) for s in cent)))
    lab = None
    for _ in range(40):
        lab = [min(range(copies), key=lambda j: dist(it[0], cent[j])) for it in items]
        for j in range(copies):
            grp = [items[i][0] for i in range(len(items)) if lab[i] == j]
            if grp:
                cent[j] = (sum(g[0] for g in grp) / len(grp),
                           sum(g[1] for g in grp) / len(grp))

    groups = [[t for i, it in enumerate(items) if lab[i] == j for t in it[1]]
              for j in range(copies)]
    sizes = sorted(len(g) for g in groups)
    if sizes[0] != sizes[-1]:
        raise SystemExit('%s: the %d copies are not equal (%s); the split is wrong'
                         % (mesh['name'], copies, sizes))
    pick = min(range(copies), key=lambda j: dist(cent[j], (0.0, 0.0)))
    return groups[pick]


def standalone(mesh, tris):
    """Re-index a triangle subset, convert Z-up to Y-up, recentre on x/z and drop
       the lowest point onto y = 0."""
    idx = sorted({i for t in tris for i in t})
    remap = {o: n for n, o in enumerate(idx)}
    p = [(mesh['pos'][i][0], mesh['pos'][i][2], -mesh['pos'][i][1]) for i in idx]
    nz = [(mesh['nrm'][i][0], mesh['nrm'][i][2], -mesh['nrm'][i][1]) for i in idx]
    cx = (min(v[0] for v in p) + max(v[0] for v in p)) * 0.5
    cz = (min(v[2] for v in p) + max(v[2] for v in p)) * 0.5
    y0 = min(v[1] for v in p)
    p = [(v[0] - cx, v[1] - y0, v[2] - cz) for v in p]
    return dict(pos=p, nrm=nz, uv=[mesh['uv'][i] for i in idx],
                tris=[tuple(remap[i] for i in t) for t in tris],
                height=max(v[1] for v in p),
                radius=max((v[0] * v[0] + v[2] * v[2]) ** 0.5 for v in p))


# ---- the texture --------------------------------------------------------------------

def dds_rgba(blob):
    """Decode a DXT1 DDS with mip levels down to its top level as RGBA."""
    if blob[:4] != b'DDS ':
        raise SystemExit('the texture asset does not start with a DDS header')
    h, w = struct.unpack('<II', blob[12:20])
    fourcc = blob[84:88]
    if fourcc != b'DXT1':
        raise SystemExit('this reader only decodes DXT1; the sheet is %r'
                         % fourcc.decode('latin1'))
    data = blob[128:]
    out = bytearray(w * h * 4)
    bx = (w + 3) // 4
    for by in range((h + 3) // 4):
        for bxi in range(bx):
            o = (by * bx + bxi) * 8
            c0, c1 = struct.unpack('<HH', data[o:o + 4])
            bits = struct.unpack('<I', data[o + 4:o + 8])[0]

            def rgb(c):
                return (((c >> 11) & 31) * 255 // 31,
                        ((c >> 5) & 63) * 255 // 63,
                        (c & 31) * 255 // 31)

            a, b = rgb(c0), rgb(c1)
            if c0 > c1:
                pal = [a, b,
                       tuple((2 * a[i] + b[i]) // 3 for i in range(3)),
                       tuple((a[i] + 2 * b[i]) // 3 for i in range(3))]
            else:
                pal = [a, b,
                       tuple((a[i] + b[i]) // 2 for i in range(3)),
                       (0, 0, 0)]
            for py in range(4):
                yy = by * 4 + py
                if yy >= h:
                    break
                for px in range(4):
                    xx = bxi * 4 + px
                    if xx >= w:
                        break
                    c = pal[(bits >> (2 * (py * 4 + px))) & 3]
                    p = (yy * w + xx) * 4
                    out[p] = c[0]
                    out[p + 1] = c[1]
                    out[p + 2] = c[2]
                    out[p + 3] = 255
    return w, h, bytes(out)


def box_down(w, h, rgba, side):
    """Box filter to side x side. Only ever asked for an exact power-of-two step."""
    if w == side and h == side:
        return rgba
    if w % side or h % side:
        raise SystemExit('%dx%d does not divide down to %d' % (w, h, side))
    fx, fy = w // side, h // side
    n = fx * fy
    out = bytearray(side * side * 4)
    for y in range(side):
        for x in range(side):
            acc = [0, 0, 0, 0]
            for j in range(fy):
                row = ((y * fy + j) * w + x * fx) * 4
                for i in range(fx):
                    p = row + i * 4
                    acc[0] += rgba[p]
                    acc[1] += rgba[p + 1]
                    acc[2] += rgba[p + 2]
                    acc[3] += rgba[p + 3]
            p = (y * side + x) * 4
            for k in range(4):
                out[p + k] = acc[k] // n
    return bytes(out)


# ---- the pack -----------------------------------------------------------------------

def gamma_lift(rgba, g):
    """Raise the sheet's dark mass without touching its highlights. Alpha is left
       alone; the sheet has none that means anything."""
    if g == 1.0:
        return rgba
    ramp = bytes(min(255, int(255.0 * (i / 255.0) ** (1.0 / g) + 0.5))
                 for i in range(256))
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        out[i] = ramp[out[i]]
        out[i + 1] = ramp[out[i + 1]]
        out[i + 2] = ramp[out[i + 2]]
    return bytes(out)


# THE CARTRIDGE'S OWN TIBERIUM COLOUR, as a ratio to green. Measured off dostib.pack --
# the ANY_TI01..12 filmstrips this renderer already draws flat -- over every opaque texel
# that is green-dominant and brighter than the set's own mean green: (144.9, 170.3, 72.9),
# so (0.851, 1.000, 0.428). The mod's crystal quadrant means (20, 95, 31) instead, a
# ratio of (0.21, 1.000, 0.33): a cold emerald where the cartridge's mineral is a warm
# yellow-green. Pulling one to the other is what stops a field of solid crystals reading
# as a different substance from the overlay it grew out of.
CART_TIB = (0.851, 1.000, 0.428)


def crystal_tint(rgba, amount, k):
    """Pull the sheet's crystals toward the cartridge's own tiberium hue, leaving the
       brown rock alone.

       The mask is the one emissive_alpha uses -- green above the stronger of red and
       blue -- so the two agree by construction: whatever glows is whatever gets tinted,
       and the rock the pods are drawn from is neither. Applied AFTER the gamma lift so
       the ratio that survives to the sheet is the measured one; the lift is a per
       channel curve and tinting before it would land somewhere else."""
    if amount <= 0.0:
        return rgba
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        r, g, b = out[i], out[i + 1], out[i + 2]
        lead = g - max(r, b)
        if lead <= 0:
            continue
        m = min(1.0, lead * k / 255.0) * amount
        for c in range(3):
            want = g * CART_TIB[c]
            v = out[i + c] + (want - out[i + c]) * m
            out[i + c] = 0 if v < 0 else (255 if v > 255 else int(v + 0.5))
    return bytes(out)


def emissive_alpha(rgba, k):
    """Write an emissive mask into the sheet's alpha: how much of a texel glows on its
       own rather than waiting for the sun.

       The source shader had one (the mesh's parameter block names `rEmissive` beside
       `Texture_0`) and this renderer has no shader to carry it, so the mask is derived
       from the sheet instead of invented: green ABOVE the stronger of red and blue.
       Tiberium is the only green thing on the sheet, and the rock the pods are drawn
       from is brown -- measured on the decoded sheet, the crystal quadrant means
       (20, 95, 31), so green leads by 64, and the rock means (50, 38, 33), where green
       LOSES by 12 and the mask is zero. So the crystals glow and the rock they stand in
       does not, with no second texture and no hand-painted mask to keep in step."""
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        lead = out[i + 1] - max(out[i], out[i + 2])
        v = int(lead * k)
        out[i + 3] = 0 if v < 0 else (255 if v > 255 else v)
    return bytes(out)


def crack_tile(side, arms=7):
    """The glowing fracture a tiberium field breaks the ground into: a few thick cracks
       running out from one point, tapering to hairlines, turning in sharp angles, with
       sparse branches that split off thinner than the crack they came from.

       IT IS GENERATED, NOT DECODED, and that is a gap rather than a choice. The mod's
       published screenshot shows a fracture network under every clump, but the texture
       that draws it is not in the archive: the eleven entries hold the crystal sheet,
       its normal and specular maps, the tree sheet and the blue variant, and nothing
       else. In Command & Conquer 3 a tiberium field's ground is a decal out of the BASE
       GAME's packages, which this mod does not redistribute. So the pattern is authored
       here, and the walk is deterministic from a constant seed so two bakes agree.

       THE SHAPE, and each of these was a correction to the cut before it:

         FEW AND THICK. Seven arms, not fifteen. A crack is a structural failure with a
         direction, and a field of them at this camera has to read as a handful of
         strong lines with ground between them rather than as a scribble.
         TAPERED. Widest at the middle where the ground gave way, down to under a texel
         at the tip. A line of even width reads as a drawn stroke and not as a crack.
         ANGULAR. The direction is held for a whole segment and then broken sharply,
         instead of wandering a little every texel: the corners are the thing that says
         stone rather than root.
         SPARSE BRANCHES, each thinner than its parent and never more than two deep.
         NO CONCENTRIC ARCS. An earlier cut drew a few and they came out as hard rings
         around every burst, which is the one part of a pattern the eye joins up.

       The tile is written PREMULTIPLIED and drawn ADDITIVE, which is what makes it
       blend: the renderer adds it to whatever ground is already there instead of
       covering it, so grass stays grass and sand stays sand under a field, only greener
       and lit along the cracks. There is no rock crust; in the screenshot the ground
       inside a field is the terrain, brighter and greener, not a brown patch."""
    # SUBTLE. The wash is the flat green the whole decal adds and the crack is the
    # mineral light along the fracture itself. Both are deliberately low: this is a
    # glow seeping out of the ground, and the first cut lit a field like a signboard.
    WASH = (2.0, 7.0, 3.0)
    CRACK = (110.0, 235.0, 140.0)
    acc = [0.0] * (side * side)
    half = side * 0.5
    seed = [0x51ed270b]

    def rnd():
        seed[0] = (seed[0] * 1103515245 + 12345) & 0x7fffffff
        return seed[0] / float(0x7fffffff)

    def dot(x, y, w, v):
        """A round dab of radius w, so a crack has width and its edge is not a stair."""
        r = int(w) + 1
        ix, iy = int(x), int(y)
        for dy in range(-r, r + 1):
            yy = iy + dy
            if yy < 0 or yy >= side:
                continue
            for dx in range(-r, r + 1):
                xx = ix + dx
                if xx < 0 or xx >= side:
                    continue
                d = ((dx - (x - ix)) ** 2 + (dy - (y - iy)) ** 2) ** 0.5
                if d > w:
                    continue
                a = v if d <= w - 1.0 else v * (w - d)
                p = yy * side + xx
                if acc[p] < a:
                    acc[p] = a

    def crack(x, y, ang, length, w, depth):
        """Straight runs broken by sharp corners, thinning all the way to the tip."""
        travelled = 0.0
        while travelled < length:
            run = length * (0.14 + 0.16 * rnd())      # how far before it turns
            step = 0.8
            # AT LEAST ONE STEP. A short branch can ask for a run under one step, and
            # with n == 0 the loop below advanced nothing while the corner still turned:
            # the walk then spun on the spot for ever. Found by the bake not returning.
            n = max(1, int(run / step))
            for _ in range(n):
                x += math.cos(ang) * step
                y += math.sin(ang) * step
                if x < 0 or y < 0 or x >= side or y >= side:
                    return
                t = 1.0 - travelled / length
                # THE TIP FADES TO NOTHING. It used to bottom out at 0.35, so every arm
                # ended on a visible stub; squaring takes the last fifth of a crack down
                # to a few per cent and the line runs out instead of stopping.
                dot(x, y, max(0.45, w * t), t * t)
                travelled += step
                if travelled >= length:
                    return
            # the corner, and it is a real one
            ang += (0.35 + 0.45 * rnd()) * (1.0 if rnd() > 0.5 else -1.0)
            if depth < 2 and rnd() < 0.55:
                t = 1.0 - travelled / length
                crack(x, y, ang + (0.6 + 0.6 * rnd()) * (1.0 if rnd() > 0.5 else -1.0),
                      length * t * (0.35 + 0.30 * rnd()), w * 0.55, depth + 1)

    for a in range(arms):
        base = (a + 0.35 * (rnd() - 0.5)) * (2.0 * math.pi / arms)
        crack(half, half, base, half * (0.72 + 0.26 * rnd()), 2.6, 0)
    # the ground gave way in the middle, so the middle is open
    dot(half, half, 3.4, 1.0)

    out = bytearray(side * side * 4)
    for y in range(side):
        for x in range(side):
            u = (x + 0.5) / side * 2.0 - 1.0
            v = (y + 0.5) / side * 2.0 - 1.0
            r = (u * u + v * v) ** 0.5
            # THE FADE IS NEARLY ALL OF THE TILE. Full strength only in the middle
            # sixth, then a smoothstep the whole way out to the rim, so a decal has no
            # edge to find at any size: earlier cuts held full strength to a half and
            # then a third of the radius, and the ring where the fall began was still
            # findable once the decals grew.
            if r >= 1.0:
                cover = 0.0
            elif r <= 0.15:
                cover = 1.0
            else:
                n = 1.0 - (r - 0.15) / 0.85
                cover = n * n * (3.0 - 2.0 * n)
            c = acc[y * side + x] * cover
            d = (y * side + x) * 4
            for k in range(3):
                val = WASH[k] * cover + CRACK[k] * c
                out[d + k] = 255 if val > 255 else int(val)
            out[d + 3] = 255      # premultiplied: the pass adds RGB and ignores this
    return bytes(out)


def write_pack(path, clumps, texw, texh, rgba, gside, ground):
    with open(path, 'wb') as f:
        f.write(b'TIB3D1\0\0')
        f.write(struct.pack('<5I', 2, len(clumps), texw, texh, gside))
        for c in clumps:
            f.write(struct.pack('<IIff', len(c['pos']), len(c['tris']),
                                c['height'], c['radius']))
            for i in range(len(c['pos'])):
                f.write(struct.pack('<8f', c['pos'][i][0], c['pos'][i][1],
                                    c['pos'][i][2], c['nrm'][i][0], c['nrm'][i][1],
                                    c['nrm'][i][2], c['uv'][i][0], c['uv'][i][1]))
            for t in c['tris']:
                f.write(struct.pack('<3I', t[0], t[1], t[2]))
        f.write(rgba)
        if gside:
            f.write(ground)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--big', default=DEFAULT_BIG)
    ap.add_argument('-o', '--out', default=DEFAULT_OUT)
    ap.add_argument('--report', action='store_true',
                    help='measure and print, write nothing')
    ap.add_argument('--lift', type=float, default=1.8,
                    help='gamma applied to the sheet; 1 writes it as decoded')
    ap.add_argument('--tint', type=float, default=0.65,
                    help="how far the crystals are pulled toward the cartridge's own "
                         "tiberium hue; 0 leaves the mod's emerald")
    ap.add_argument('--glow', type=float, default=2.6,
                    help='gain on the emissive mask written into the sheet alpha')
    ap.add_argument('--ground', type=int, default=128,
                    help='side of the ground crack tile; 0 writes none')
    a = ap.parse_args()

    if not os.path.exists(a.big):
        sys.exit('no mod archive at %s' % a.big)
    stream = Stream(read_big(a.big))

    clumps = []
    for want in WANTED:
        if want not in stream.inst:
            sys.exit('%s is not in the archive' % want)
        m = decode_mesh(stream, want)
        c = standalone(m, one_copy(m))
        c['src'] = want
        clumps.append(c)

    tall = max(c['height'] for c in clumps)
    for c in clumps:
        s = 1.0 / tall
        c['pos'] = [(v[0] * s, v[1] * s, v[2] * s) for v in c['pos']]
        c['height'] *= s
        c['radius'] *= s

    tw, th, rgba = dds_rgba(stream.inst[WANTED_TEX][12:])
    rgba = box_down(tw, th, rgba, TEX_SIDE)
    before = [sum(rgba[k::4]) / (TEX_SIDE * TEX_SIDE) for k in range(3)]
    ground = b''
    if a.ground:
        ground = crack_tile(a.ground)
    rgba = gamma_lift(rgba, a.lift)
    rgba = crystal_tint(rgba, a.tint, a.glow)
    after = [sum(rgba[k::4]) / (TEX_SIDE * TEX_SIDE) for k in range(3)]
    rgba = emissive_alpha(rgba, a.glow)
    lit = sum(rgba[3::4]) / (TEX_SIDE * TEX_SIDE)

    print('tib3d: %d clumps, %d triangles total, sheet %dx%d -> %dx%d'
          % (len(clumps), sum(len(c['tris']) for c in clumps), tw, th,
             TEX_SIDE, TEX_SIDE))
    print('  sheet mean (%.1f %.1f %.1f) -> (%.1f %.1f %.1f) after gamma %.2f and a '
          '%.2f pull toward the cartridge hue %s'
          % (before[0], before[1], before[2], after[0], after[1], after[2],
             a.lift, a.tint, CART_TIB))
    print('  emissive mask at gain %.2f: mean alpha %.1f of 255' % (a.glow, lit))
    if a.ground:
        gm = [sum(ground[k::4]) / (a.ground * a.ground) for k in range(3)]
        print('  ground crack tile %dx%d, generated, additive, mean (%.1f %.1f %.1f)'
              % (a.ground, a.ground, gm[0], gm[1], gm[2]))
    for i, c in enumerate(clumps):
        print('  %d  %-28s %4d tris %4d verts   height %.3f  radius %.3f'
              % (i, c['src'][8:], len(c['tris']), len(c['pos']),
                 c['height'], c['radius']))
    if a.report:
        return
    write_pack(a.out, clumps, TEX_SIDE, TEX_SIDE, rgba, a.ground, ground)
    print('wrote %s (%d bytes)' % (a.out, os.path.getsize(a.out)))


if __name__ == '__main__':
    main()
