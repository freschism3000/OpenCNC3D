#!/usr/bin/env python3
"""The cartridge's VEHICLE AND AIRCRAFT SHADOWS, decoded from the ROM.

WHY THIS FILE EXISTS, AND THE MISTAKE IT REPLACES.

A survey in wave 6 concluded "THE CARTRIDGE DRAWS NO SHADOW UNDER A VEHICLE OR
AIRCRAFT", recorded it as SETTLED, and said no vehicle shadow may be
added. It was wrong, and it was disproved in one screenshot of the real console: a
Humvee on grass with an unmistakable cast shadow offset to the north-east.

It went wrong in a way worth remembering. It walked each MODEL's own scene graph and its
children -- and the shadow is not there. It read the draw-command flag word at cmd+0x20
-- and the shadow is selected at cmd+0x24. It enumerated the twelve call sites of the
draw-command allocator -- and the shadow allocates no draw command at all. Four separate
"the only X is Y" claims, each true, adding up to a false conclusion, and not one of them
was ever checked against a picture of the console.

WHERE THE SHADOWS ACTUALLY LIVE.

A SECOND, type-indexed table at RAM 0x8009A7E8 / ROM 0x9B3E8, which nothing in a model
points at:

    18 records of 0x14 bytes:  { u32 typeId; u32 x3 (runtime); Node* shadowNode }
    terminated by typeId == -1

Its keys are exactly the eighteen vehicle and aircraft type ids and nothing else -- no
buildings, walls, trees or infantry, which carry their shadows baked into their own
meshes. Each shadowNode is a LEAF of the form { Gfx* dl, 0, 0, 0 }: geometry and nothing
else, no rest pose, no handle, no children.

THE TRAP THAT WILL COST YOU AN HOUR IF YOU SKIP IT: five of the nodes (FTNK, STNK, ORCA,
TRAN, HTNK) live in the ScriptModels overlay at RAM 0x80122B60, not in
GameModelsGeometry. Resolved with the single GameModelsGeometry offset they decode as
floats and look like garbage. Use the per-segment table below.

WHAT EACH SHADOW IS.

One flat quad. Four vertices, two triangles, all at model y = 0 (STNK alone sits at
y = -7 leptons), sized to that hull's own footprint. Every one of the eighteen shares a
single combiner and a single render mode:

    combiner   colour = PRIM,  alpha = TEXEL0_A          (FCFFFFFF FFFDF2F9)
    rendermode CLR_IN*A_IN + CLR_MEM*(1-A), Z compare ON, Z WRITE OFF   (005049D8)

so a shadow is a BLACK quad whose alpha is an 8-bit intensity texture, alpha-blended over
the ground and depth-tested but never depth-written. PRIM is 000000FF for most, and
010101FF or 121212FF for a few -- near-black, kept exactly rather than rounded, because
the cartridge bothered to differ.

That combiner independently predicts what the capture measures: hue-preserving
darkening toward black (not a colour overlay), at an alpha the screenshot puts near
80/255 -- the same figure as the already-verified infantry shadow.

Five distinct textures over the eighteen (thirteen share one), in two sizes, 16x16 and
16x32 (S is width, T is height), all 8-bit I. And FIVE distinct UV layouts: 0..512 over
16 texels, 0..1024 over 32, plus inset (1..1023) and OVERHANGING (-9..1034) variants. So
UVs are carried per vertex rather than assumed -- an implementation that hard-codes
0..1 gets four of the eighteen visibly wrong.

Run it standalone to see the table:
    python3 tools/romdump/vehicle_shadows.py [path/to/cnc_eu.z64]
"""
import os
import struct
import sys

# RAM -> ROM, per segment. From tools/romdump/segments.py; repeated here as literals so
# this module has no import-order dependency on it, and asserted against it when it can
# be imported.
SEGMENTS = [
    ("ScriptModels",       0x00C6EA0, 0x00DA420, 0x80122B60),
    ("GameModelsTextures", 0x00DA420, 0x00FE210, 0x801369B0),
    ("GameModelsGeometry", 0x00FE210, 0x01643F0, 0x8015A7A0),
]

SHADOW_TABLE_ROM = 0x9B3E8      # RAM 0x8009A7E8
RECORD_BYTES = 0x14
EXPECT_COUNT = 18

# The combiner and render mode every one of the eighteen uses. Asserted, not assumed:
# if a future ROM revision differs, this must fail rather than silently draw it wrong.
EXPECT_COMBINE = (0xFCFFFFFF, 0xFFFDF2F9)   # colour = PRIM, alpha = TEXEL0_A
EXPECT_RENDERMODE = 0x005049D8              # blend over, Z test, no Z write

# The quads are emitted in RAW MODEL UNITS, exactly as the body meshes are baked, and
# the renderer applies its own MODEL_SCALE (1/1024) at draw time. Do NOT pre-divide here.
# Getting this wrong is not subtle but it is not obviously wrong either: dividing by 256
# instead produced shadows 4x too big, which reads as "the shadows are broken" rather
# than "the scale is wrong", and the body meshes right next to them looked fine.


def _seg_of(ram):
    for name, rs, re_, rb in SEGMENTS:
        if rb <= ram < rb + (re_ - rs):
            return name, rs + (ram - rb)
    return None, None


def _u32(rom, off):
    return struct.unpack_from(">I", rom, off)[0]


def _walk_dl(rom, dl_ram):
    """Read one shadow display list. Returns the state it sets and the geometry it draws."""
    seg, off = _seg_of(dl_ram)
    assert seg is not None, "shadow display list %08X is in no known segment" % dl_ram
    out = {"timg": None, "verts": None, "nvtx": 0, "tris": 0,
           "prim": None, "combine": None, "rendermode": None,
           "tile": None, "tilesize": None}
    for i in range(256):
        w0, w1 = struct.unpack_from(">II", rom, off + i * 8)
        op = w0 >> 24
        if op == 0xFD:                                    # G_SETTIMG
            out["timg"] = w1
        elif op == 0xF5 and out["tile"] is None:          # G_SETTILE
            out["tile"] = (w0, w1)
        elif op == 0xF2:                                  # G_SETTILESIZE
            out["tilesize"] = w1
        elif op == 0x01:                                  # G_VTX
            out["verts"] = w1
            out["nvtx"] = (w0 >> 12) & 0xFF
        elif op == 0x05:                                  # G_TRI1
            out["tris"] += 1
        elif op == 0x06:                                  # G_TRI2
            out["tris"] += 2
        elif op == 0xFA and out["prim"] is None:          # G_SETPRIMCOLOR
            out["prim"] = w1
        elif op == 0xFC and out["combine"] is None:       # G_SETCOMBINE
            out["combine"] = (w0, w1)
        elif op == 0xE2 and out["rendermode"] is None:    # G_SETOTHERMODE_L
            out["rendermode"] = w1
        elif op == 0xDF:                                  # G_ENDDL
            break
    else:
        raise AssertionError("shadow display list %08X has no G_ENDDL in 256 commands"
                             % dl_ram)
    return out


def extract(rom):
    """-> list of dicts, one per vehicle/aircraft shadow, in table order.

    Each: {typeId, dl, verts[4] = (x,y,z in RAW MODEL UNITS, u,v texture coords),
           texw, texh, texA (bytes, one alpha per texel)}
    """
    out = []
    for i in range(EXPECT_COUNT + 1):
        rec = SHADOW_TABLE_ROM + i * RECORD_BYTES
        tid = _u32(rom, rec)
        if tid == 0xFFFFFFFF:
            assert i == EXPECT_COUNT, \
                "shadow table terminates after %d records, expected %d" % (i, EXPECT_COUNT)
            break
        node_ram = _u32(rom, rec + 0x10)
        nseg, noff = _seg_of(node_ram)
        assert nseg is not None, "shadow node %08X (type %d) is in no known segment" % (node_ram, tid)

        # The node is a leaf: { Gfx* dl, 0, 0, 0 }. Anything else means this is not the
        # structure we think it is.
        dl_ram = _u32(rom, noff)
        for k in (1, 2, 3):
            assert _u32(rom, noff + k * 4) == 0, \
                "shadow node %08X field %d is not zero -- not a bare geometry leaf" % (node_ram, k)

        d = _walk_dl(rom, dl_ram)
        assert d["combine"] == EXPECT_COMBINE, \
            "type %d shadow combiner is %08X %08X, expected %08X %08X" % (
                (tid,) + d["combine"] + EXPECT_COMBINE)
        assert d["rendermode"] == EXPECT_RENDERMODE, \
            "type %d shadow rendermode is %08X, expected %08X" % (
                tid, d["rendermode"], EXPECT_RENDERMODE)
        assert d["nvtx"] == 4 and d["tris"] == 2, \
            "type %d shadow is %d verts / %d tris, expected a 4-vertex quad" % (
                tid, d["nvtx"], d["tris"])

        # Texture: 8-bit I, size from SETTILE's masks, cross-checked against SETTILESIZE.
        tw0, tw1 = d["tile"]
        fmt = (tw0 >> 21) & 7
        siz = (tw0 >> 19) & 3
        assert fmt == 4 and siz == 1, \
            "type %d shadow texture is fmt %d siz %d, expected I 8-bit" % (tid, fmt, siz)
        texw = 1 << ((tw1 >> 4) & 0xF)
        texh = 1 << ((tw1 >> 14) & 0xF)
        lrs = (d["tilesize"] >> 12) & 0xFFF
        lrt = d["tilesize"] & 0xFFF
        assert (lrs // 4 + 1, lrt // 4 + 1) == (texw, texh), \
            "type %d shadow SETTILE says %dx%d but SETTILESIZE says %dx%d" % (
                tid, texw, texh, lrs // 4 + 1, lrt // 4 + 1)
        tseg, toff = _seg_of(d["timg"])
        assert tseg is not None, "shadow texture %08X (type %d) is in no known segment" % (
            d["timg"], tid)
        texA = rom[toff:toff + texw * texh]
        assert len(texA) == texw * texh, "type %d shadow texture runs off the segment" % tid
        assert max(texA) > 0, "type %d shadow texture is entirely zero -- it would draw nothing" % tid

        # Geometry. xyz in leptons -> cells; st is S10.5 -> texels -> 0..1.
        vseg, voff = _seg_of(d["verts"])
        assert vseg is not None, "shadow vertices %08X (type %d) are in no known segment" % (
            d["verts"], tid)
        verts = []
        ys = set()
        for k in range(4):
            x, y, z, _flag, s, t = struct.unpack_from(">hhhHhh", rom, voff + k * 16)
            ys.add(y)
            verts.append((float(x), float(y), float(z),
                          (s / 32.0) / texw, (t / 32.0) / texh))
        assert len(ys) == 1, "type %d shadow is not flat: y values %s" % (tid, sorted(ys))

        # THE WRAP MODE, WHICH THIS EXTRACTOR USED TO THROW AWAY AND WHICH FOUR OF THE
        # EIGHTEEN DEPEND ON. G_SETTILE word 1 carries cmS at bit 8 and cmT at bit 18,
        # two bits each: 0 = wrap, 1 = mirror, 2 = clamp. The renderer's comment claimed
        # for a while that all eighteen say clamp; they do not. Measured off the ROM:
        # nine are wrap/wrap, five are clamp/clamp, and A10, ORCA, HELI and C17 are
        # MIRROR in S over a 16x32 texture whose S coordinates run 0..2, because each of
        # those is a HALF aircraft that the console mirrors to make the whole shadow.
        # Read it here and the baker can act on it; drop it here and no amount of care
        # downstream can recover it.
        cmS = (tw1 >> 8) & 3
        cmT = (tw1 >> 18) & 3

        out.append({
            "typeId": tid, "dl": dl_ram, "node": node_ram, "segment": nseg,
            "verts": verts, "texw": texw, "texh": texh, "texA": texA,
            "cmS": cmS, "cmT": cmT,
            "prim": ((d["prim"] >> 24) & 0xFF, (d["prim"] >> 16) & 0xFF,
                     (d["prim"] >> 8) & 0xFF, d["prim"] & 0xFF),
            "timg": d["timg"],
        })

    assert len(out) == EXPECT_COUNT, "found %d shadows, expected %d" % (len(out), EXPECT_COUNT)
    ids = [r["typeId"] for r in out]
    assert len(set(ids)) == EXPECT_COUNT, "duplicate type ids in the shadow table: %s" % ids
    # Every shadow must be black or near-black and fully opaque at PRIM; the alpha comes
    # from the texture. A coloured PRIM would mean this is not a shadow.
    for r in out:
        pr, pg, pb, pa = r["prim"]
        assert pa == 0xFF, "type %d shadow PRIM alpha is %d, expected 255" % (r["typeId"], pa)
        assert max(pr, pg, pb) <= 0x20, \
            "type %d shadow PRIM is (%d,%d,%d) -- not near-black" % (r["typeId"], pr, pg, pb)

    # The wrap modes and the S ranges have to agree, and asserting it here is what stops
    # a future re-extract from quietly changing the shape of the aircraft shadows. A
    # MIRROR record's S runs out to 2 because the texture is half the shadow; a
    # non-mirror record's S stays inside 0..1 (with a texel of slop, which ORCA uses).
    for r in out:
        smax = max(v[3] for v in r["verts"])
        smin = min(v[3] for v in r["verts"])
        if r["cmS"] == 1:
            assert 1.9 <= smax <= 2.1, \
                "type %d mirrors in S but its S range is %.3f..%.3f, not ~0..2" % (
                    r["typeId"], smin, smax)
        else:
            assert smax <= 1.05 and smin >= -0.05, \
                "type %d does not mirror but its S runs %.3f..%.3f outside 0..1" % (
                    r["typeId"], smin, smax)
    nmir = sum(1 for r in out if r["cmS"] == 1)
    assert nmir == 4, "expected 4 mirror-in-S shadows (the aircraft), found %d" % nmir
    return out


def _names():
    """typeId -> INI name, from the bakery's model table when it is reachable."""
    import json
    here = os.path.dirname(os.path.abspath(__file__))
    p = os.path.normpath(os.path.join(here, "..", "bakery", "support", "unit_models.json"))
    if not os.path.isfile(p):
        return {}
    um = json.load(open(p))
    return {v["type_id"]: k for k, v in um.items() if "type_id" in v}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default = os.path.normpath(os.path.join(here, "..", "..", "data", "rom", "cnc_eu.z64"))
    path = sys.argv[1] if len(sys.argv) > 1 else default
    rom = open(path, "rb").read()
    shadows = extract(rom)
    names = _names()
    print("%-6s %-6s %-19s %-9s %-9s %-8s %s" %
          ("typeId", "name", "segment", "node", "dl", "tex", "extent (raw model units)  prim"))
    for r in shadows:
        xs = [v[0] for v in r["verts"]]
        zs = [v[2] for v in r["verts"]]
        print("%-6d %-6s %-19s %08X  %08X  %2dx%-3d  x %6.0f..%-6.0f z %6.0f..%-6.0f  %s  peakA=%d" % (
            r["typeId"], names.get(r["typeId"], "?"), r["segment"], r["node"], r["dl"],
            r["texw"], r["texh"], min(xs), max(xs), min(zs), max(zs),
            "%02X%02X%02X" % r["prim"][:3], max(r["texA"])))
    print("\n%d shadows, all assertions passed." % len(shadows))
    print("distinct textures: %s" % sorted({"%08X" % r["timg"] for r in shadows}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
