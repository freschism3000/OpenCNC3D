#!/usr/bin/env python3
"""CNC3D -- the viewer's REAL assets, taken from the game's own baked packs.

The first version of this viewer drew objects as flat-coloured geometry and faked
the sea with a blue plane. Both were the viewer's inventions. Neither had to be:
`playable/<SCEN>.pack` is exactly what the shipped game renders, and it carries
the textures, the UVs, the per-vertex colours, the two water tiles and the sea
floor. This reads them out.

MEASURED, not assumed: all 293 textures in a pack are byte-identical at the same
index across every pack, in both theaters, and all 220 meshes are shared by name.
Only texture 0, the 1024x1024 per-map terrain composite, differs per mission, and
the viewer does not need it (it builds terrain from the theater tile atlas, which
covers the DOS maps too). So ONE pack yields the whole global asset set.

FORMATS, from the writer (tools/bakery/bake5.py) and the reader
(game/cnc_eyes.cpp), never from a field name:

  texture   u32 w, u32 h, u32 uw, u32 uh, RGBA8[w*h*4], u8 hasGdi, RGBA8[..] if set
            Already decoded. 74 of 293 carry a second GDI-house variant.
  mesh      char[16] name, u32 ntri, ntri x 78-byte triangle, then parts/sections
  triangle  i32 texIndex (-1 = untextured, use the vertex colour alone)
            u8 mode, u8 wrap
            3 x { f32 x, f32 y, f32 z, f32 u, f32 v, u8 r, u8 g, u8 b, u8 a }
            bake5.py:1703-1708. Positions are raw N64 units, the same space as
            models_full, so the viewer's MODEL_SCALE of 1/1024 still applies. UVs
            are normalised per texture and DO leave [0,1]: 27.5% of triangles
            need real wrapping.
  wrap      S = wrap & 3, T = (wrap >> 2) & 3; 2 = clamp, 1 = mirror, else repeat.
            cnc_eyes.cpp:1708-1719. It is per TRIANGLE, not per texture: the same
            foliage sheet is clamped on a tree crown and repeated elsewhere.
  types     547 x (char[8] code, i32 meshIndex, u8), meshIndex -1 = no mesh.
            Walls are named properly here: SBAG / SBAG_L / SBAG_T / SBAG_X and the
            same for CYCL, BRIK, BARB, WOOD. That is better than walking the ROM
            display lists for them, so this supersedes that path.
  tail      ... PK9 heights | PKA seabed i32 | PK8 water 2 x i32, at EOF.
            packinspect.py's docstring is the authority on the tail contract.

Outputs, all shared by every map:
  public/data/atlas.png      every mesh texture, plus the two water tiles and the
                             sea floor, shelf-packed. Sampled NEAREST, which is
                             what the cartridge does, so no bleed and no padding.
  public/data/assets.json    the atlas rects, the type -> mesh map, the water and
                             seabed rects, and the mesh index
  public/data/assets.bin     every triangle: 16 bytes per vertex

Read-only on the pack.
"""
import json, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export import bleed_rgba

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "bakery"))
PLAYABLE = os.path.join(HERE, "..", "..", "playable")
TRI = 78                       # bytes per baked triangle


def load(path):
    import packinspect
    return packinspect.read(path)


def tail(path):
    b = open(path, "rb").read()
    w1, w2 = struct.unpack_from("<ii", b, len(b) - 8)
    sb = struct.unpack_from("<i", b, len(b) - 12)[0]
    return w1, w2, sb


def triangles(mesh):
    b = mesh["tris"]
    for i in range(mesh["ntris"]):
        o = i * TRI
        ti = struct.unpack_from("<i", b, o)[0]
        mode, wrap = b[o + 4], b[o + 5]
        vs = []
        for k in range(3):
            q = o + 6 + k * 24
            x, y, z, u, v = struct.unpack_from("<fffff", b, q)
            vs.append((x, y, z, u, v, b[q + 20], b[q + 21], b[q + 22], b[q + 23]))
        yield ti, mode, wrap, vs


def shelf_pack(sizes, width=512):
    """-> {key: (x, y)}, height. Tallest-first shelves; NEAREST sampling means no
    padding is needed and none is added, so the atlas stays exact."""
    order = sorted(sizes, key=lambda k: (-sizes[k][1], -sizes[k][0]))
    pos, x, y, shelf = {}, 0, 0, 0
    for k in order:
        w, h = sizes[k]
        if x + w > width:
            x, y, shelf = 0, y + shelf, 0
        pos[k] = (x, y)
        x += w
        shelf = max(shelf, h)
    return pos, y + shelf


def build_tiberium(out_dir, verbose=True):
    """The cartridge's own ANY_TI01..12 filmstrips, out of playable/dostib.pack.

    bake_dostiberium.py wrote it and game/dostib_mod.h reads it:
        char[8] "DOSTIB1\0"
        u32 ver(1), source, sets, frameW, frameH, types, frames
        per set: char[12] name, u32 sheets, texW, texH, cols, rows
                 types*frames x {u16 sheet, u16 x0, u16 y0}
                 sheets x RGBA8[texW*texH*4]
    Only set 0 is read; the N64 bake carries exactly one, named "ANY", which is the
    cartridge's own statement that the art is theaterless.

    The growth STAGE is not in the mission INI. The engine derives it, and so does
    this: CellClass::Tiberium_Adjust (cell.cpp:1901) counts how many of the EIGHT
    adjacent cells also hold tiberium and takes _adj[count] with
    _adj = {0,1,3,4,6,7,8,10,11}. cell.cpp:1041 then uses OverlayData as the SHP
    frame index with no arithmetic at all."""
    from PIL import Image
    path = os.path.join(PLAYABLE, "dostib.pack")
    if not os.path.exists(path):
        return None
    b = open(path, "rb").read()
    if b[:7] != b"DOSTIB1":
        return None
    o = 8
    ver, _src, sets, fw, fh, types, frames = struct.unpack_from("<7I", b, o); o += 28
    assert ver == 1 and sets >= 1, (ver, sets)
    name = b[o:o + 12].split(b"\0")[0].decode(); o += 12
    sheets, tw, th, _cols, _rows = struct.unpack_from("<5I", b, o); o += 20
    n = types * frames
    slots = [struct.unpack_from("<3H", b, o + i * 6) for i in range(n)]
    o += n * 6
    sheet_px = tw * th * 4
    img = Image.new("RGBA", (tw * sheets, th))
    for s_i in range(sheets):
        img.paste(Image.frombytes("RGBA", (tw, th), b[o:o + sheet_px]), (s_i * tw, 0))
        o += sheet_px
    img.save(os.path.join(out_dir, "tiberium.png"))
    meta = dict(set=name, frameW=fw, frameH=fh, types=types, frames=frames,
                sheets=sheets, texW=tw, texH=th, atlasW=tw * sheets, atlasH=th,
                slots=[[sh, x, y] for sh, x, y in slots],
                growth=[0, 1, 3, 4, 6, 7, 8, 10, 11],
                note="frame = _adj[count of the 8 adjacent tiberium cells], "
                     "CellClass::Tiberium_Adjust cell.cpp:1901")
    if verbose:
        print(f"tiberium: {types}x{frames} frames of {fw}x{fh} on {sheets} sheets "
              f"-> tiberium.png {tw*sheets}x{th}")
    return meta


def build(pack_path, out_dir, verbose=True):
    from PIL import Image
    p = load(pack_path)
    w1, w2, sb = tail(pack_path)
    tex, meshes = p["tex"], p["meshes"]
    # The u8 is a confidence, and wall_mesh_named refuses anything below 1
    # (cnc_eyes.cpp:5474-5480). Exactly one type is affected here: ROAD claims mesh 0
    # at confidence 0, which is not a road.
    types = {code: (mi if h >= 1 else -1, h) for code, mi, h in p["types"]}

    # Which textures the geometry actually touches, plus the sea's three, plus the
    # infantry sprite strips.
    #
    #   PackSprite { char name[17]; int tex, frames, facings, fw, fh, w, h; }
    #       cnc_eyes.cpp:747. A strip is a VERTICAL filmstrip of `frames` cells of
    #       fw x fh, laid out facing-major: frame = row * (frames/facings) + stage.
    #   PackInf    { char set[5]; int strip[2][3]; }   cnc_eyes.cpp:759
    #       row 0 GDI, row 1 Nod; column 0 STAND, 1 RUN, 2 DEATH. -1 = no such strip.
    # The console draws infantry as one camera-facing billboard, fw/24 by fh/24 cells
    # (sprite_texels_per_unit is 24 under the N64 camera), standing on the ground.
    used = set()
    for m in meshes:
        for ti, _mode, _wrap, _vs in triangles(m):
            if ti >= 0:
                used.add(ti)
    for i in (w1, w2, sb):
        if i >= 0:
            used.add(i)
    for _nm, v in p["sprites"]:
        if v[0] >= 0:
            used.add(v[0])

    # A GDI-variant blob is a second image of the same size. It gets its own slot.
    slots = []                              # (texIndex, isGdi)
    for i in sorted(used):
        slots.append((i, False))
        if tex[i]["gdi"]:
            slots.append((i, True))
    # UVs are normalised to the USED size, not the padded allocation: the console
    # scales by tx.uw/tx.w, and over all 22,899 textured mesh vertices u*uw and v*uh
    # land on the RDP's 1/32 texel grid 22899 times out of 22899, against 22759 for
    # u*w. So crop the pad off on the way into the atlas. It also drops 109,168 texels
    # of sprite padding.
    sizes = {s: (tex[s[0]]["uw"], tex[s[0]]["uh"]) for s in slots}
    pos, height = shelf_pack(sizes)
    height = 1 << (height - 1).bit_length()          # power of two, kinder to GL
    atlas = Image.new("RGBA", (512, height), (0, 0, 0, 0))
    rect, gdi_of = {}, {}
    for s in slots:
        i, is_gdi = s
        t = tex[i]
        data = t["gdi"] if is_gdi else t["data"]
        img = Image.frombytes("RGBA", (t["w"], t["h"]), bytes(data)).crop(
            (0, 0, t["uw"], t["uh"]))
        # Bleed PER TILE, before pasting. The alpha-0 texels inside a cut-out are
        # black, and the viewer samples this bilinearly now, so an unbled edge
        # darkens. Doing it per tile keeps the atlas's own empty space black (which
        # is what keeps the PNG small) and cannot leak one tile into another.
        atlas.paste(bleed_rgba(img), pos[s])
        idx = len(rect)
        rect[s] = idx
    # stable order: the rect table is indexed the same way the geometry references it
    order = sorted(rect, key=lambda s: (s[0], s[1]))
    rect = {s: n for n, s in enumerate(order)}
    rects = []
    for s in order:
        i = s[0]
        x, y = pos[s]
        rects.append([x, y, tex[i]["uw"], tex[i]["uh"]])
        if s[1]:
            gdi_of[rect[(i, False)]] = rect[s]
    atlas.save(os.path.join(out_dir, "atlas.png"))

    # Geometry. 16 bytes per vertex: pos int16 x3, uv int16 x2 (1/4096), rgba u8 x4,
    # rectIndex u16. UVs run to about +-3, so 1/4096 in an int16 has room to spare.
    blob = bytearray()
    index, tri0 = {}, 0
    UVQ = 4096.0
    for mi, m in enumerate(meshes):
        n = 0
        for ti, mode, wrap, vs in triangles(m):
            r = rect.get((ti, False), 0xFFFF) if ti >= 0 else 0xFFFF
            for (x, y, z, u, v, cr, cg, cb, ca) in vs:
                blob += struct.pack(
                    "<hhhhhBBBBH",
                    max(-32768, min(32767, int(round(x)))),
                    max(-32768, min(32767, int(round(y)))),
                    max(-32768, min(32767, int(round(z)))),
                    max(-32768, min(32767, int(round(u * UVQ)))),
                    max(-32768, min(32767, int(round(v * UVQ)))),
                    cr, cg, cb, ca, r & 0xFFFF)
            n += 1
        index[m["name"]] = dict(t0=tri0, n=n)
        tri0 += n
    open(os.path.join(out_dir, "assets.bin"), "wb").write(bytes(blob))

    # One byte per triangle, parallel to the triangle list: the cartridge's wrap in
    # the low nibble and its TriMode in the high one.
    #   TriMode { MODE_OPAQUE 0, MODE_CUTOUT 1, MODE_SHADOW 2, MODE_XLU 3 }
    #   cnc_eyes.cpp:676. Shadow faces are baked geometry, not lighting: they draw
    #   blended at a 0.012 y bias and a (+5,-5)/256 nudge (cnc_eyes.cpp:5336-5339),
    #   and drawing them as solid geometry is exactly what makes a model look
    #   shattered by black shards.
    wr = bytearray()
    for m in meshes:
        for ti, mode, wrap, vs in triangles(m):
            wr.append((wrap & 15) | ((mode & 3) << 4))
    open(os.path.join(out_dir, "assets_wrap.bin"), "wb").write(bytes(wr))

    meta = dict(
        source=os.path.basename(pack_path),
        note="geometry and textures are the shipped game's own baked pack; the "
             "viewer adds no colour of its own to them",
        atlas="atlas.png", atlasW=512, atlasH=height,
        uvQuant=UVQ, modelScale=1.0 / 1024.0,
        rects=rects, gdiOf=gdi_of,
        meshes=[dict(name=m["name"], **index[m["name"]]) for m in meshes],
        types={code: mi for code, (mi, _h) in types.items()},
        water=[rect.get((w1, False), -1), rect.get((w2, False), -1)],
        seabed=rect.get((sb, False), -1),
        sprites=[dict(name=nm, rect=rect.get((v[0], False), -1), frames=v[1],
                      facings=v[2], fw=v[3], fh=v[4])
                 for nm, v in p["sprites"]],
        infantry={code: idx for code, _st, idx in p["infantry"]},
        spriteTexelsPerUnit=24.0,
        triangles=tri0,
    )
    # The sea's three tiles go out as their own images, not into the atlas: they are
    # sampled with GL_REPEAT and a scrolling, warping UV that runs far outside [0,1],
    # which no atlas rect can contain.
    for nm, i in (("water1", w1), ("water2", w2), ("seabed", sb)):
        if i is not None and i >= 0:
            t = tex[i]
            Image.frombytes("RGBA", (t["w"], t["h"]), bytes(t["data"])).save(
                os.path.join(out_dir, nm + ".png"))

    tib = build_tiberium(out_dir, verbose)
    if tib:
        meta["tiberium"] = tib
    json.dump(meta, open(os.path.join(out_dir, "assets.json"), "w"),
              separators=(",", ":"))
    if verbose:
        print(f"pack {os.path.basename(pack_path)}: {tri0} triangles, "
              f"{len(rects)} atlas rects in 512x{height}, "
              f"{len(blob)} B geometry, water {meta['water']} seabed {meta['seabed']}")
    return meta


if __name__ == "__main__":
    out = os.path.join(HERE, "public", "data")
    os.makedirs(out, exist_ok=True)
    build(os.path.join(PLAYABLE, "SCG01EA.pack"), out)
