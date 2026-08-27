#!/usr/bin/env python3
"""Build the model gallery's data from a baked mission pack.

    python3 tools/model-gallery/export.py [--pack <path>] [--no-exports]

WHERE THE MODELS COME FROM. Not the 2026-08-13 romdump corpus that the old
`assets/model-viewer.html` was built on -- that one predates the house-TLUT fix, so
every mesh in it is Nod-coloured, and its SBAG/CYCL/BRIK/BARB/WOOD are the BULLET
models rather than the walls. This reads the same baked pack the game itself draws
(`tools/bakery/game/*.pack`), so the textures are the ones the renderer binds, the
parts carry the cartridge's own pivots and roles, and the walls are the real ones.

All 100 packs carry an identical asset set (measured: 293 textures, 220 meshes), so
one pack is the whole global corpus.

WHAT IS AN ASSET. The pack's type table maps 547 codes onto 220 meshes. Most of the
surplus is variant meshes -- a building's turn states (`FIXT0..FIXT49` are 50 codes
over 5 meshes) and a cursor's texture flipbook (`CUR0AF0..F3`) -- which belong under
their parent rather than beside it. The gallery therefore lists the 186 primary
meshes, hangs the 34 variants off the parent they belong to, and adds the 16 meshes
the briefing screen names for itself (the GDI and Nod medallions, the EVA wordmark,
and the high-detail hero models), which live in the ROM and never enter a pack.

Names and categories come from EA's GPL source, not from us: see catalog.py.
"""
import argparse
import base64
import json
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "bakery"))
sys.path.insert(0, os.path.join(ROOT, "tools", "art"))
sys.path.insert(0, os.path.join(ROOT, "tools", "romdump"))
sys.path.insert(0, HERE)

import packinspect as PI                                     # noqa: E402
import catalog as CAT                                        # noqa: E402

DEFAULT_PACK = os.path.join(ROOT, "tools", "bakery", "game", "SCB01EA.pack")
OUT = os.path.join(HERE, "public", "data")

TRI_STRIDE = 4 + 2 + 3 * (5 * 4 + 4)
TRI_FMT = "<i2B" + "5f4B" * 3
ROLE = {0: "static", 1: "turret", 2: "rotor"}
MODE = {0: "opaque", 1: "cutout", 2: "shadow", 3: "xlu"}

# A variant mesh is a derived state of another mesh, and the baker says so in the
# name: `_f<N>` is a texture flipbook frame, `_b<N>t<N>` is a turn/stage bucket.
VARIANT = re.compile(r"^(dl_[0-9A-F]+)(_f\d+|_b\d+t\d+)$")

CATEGORY_ORDER = ["Vehicles", "Aircraft", "Structures", "Civilian", "Walls",
                  "Scenery", "Projectiles", "Cursors", "Debris", "Rigs",
                  "Parts", "Logos", "Briefing", "Misc"]

# The four the briefing screen carries as marks rather than as objects.
LOGOS = {"BRF_LOGO_GDI", "BRF_LOGO_NOD", "GDI_LOGO_TITLE_ROOT", "BRF_EVA_ROOT"}


# --------------------------------------------------------------------- geometry
def read_tris(mesh):
    out = []
    b = mesh["tris"]
    for i in range(mesh["ntris"]):
        f = struct.unpack_from(TRI_FMT, b, i * TRI_STRIDE)
        vs = []
        for k in range(3):
            o = 3 + k * 9
            vs.append((f[o], f[o + 1], f[o + 2], f[o + 3], f[o + 4],
                       f[o + 5], f[o + 6], f[o + 7], f[o + 8]))
        out.append(dict(tex=f[0], mode=f[1], wrap=f[2], v=vs))
    return out


def pack_geometry(tris, tex):
    """One asset's geometry, in the layout the viewer's loader expects.

    pos f32[9n] | uv f32[6n] | col u8[12n] | tex i16[n] | mode u8[n] | wrap u8[n]

    The UV scale is the renderer's own (cnc_eyes.cpp:5510): the pack's u,v run 0..1
    over the texture's USED region and the PNG is padded to a power of two, so every
    coordinate is multiplied by uw/w and uh/h. There is NO V flip -- the pack's V is
    top-down and so is the PNG. (fbxout.py carries a 1-v only because FBX is
    bottom-left; that is the same fact stated for a different consumer.)
    """
    n = len(tris)
    pos = bytearray()
    uv = bytearray()
    col = bytearray()
    tix = bytearray()
    mod = bytearray()
    wrp = bytearray()
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for t in tris:
        if t["tex"] >= 0:
            tx = tex[t["tex"]]
            su = float(tx["uw"]) / float(tx["w"])
            sv = float(tx["uh"]) / float(tx["h"])
        else:
            su = sv = 1.0
        for (x, y, z, u, v, r, g, b, a) in t["v"]:
            pos += struct.pack("<3f", x, y, z)
            uv += struct.pack("<2f", u * su, v * sv)
            col += bytes((r, g, b, a))
            for i, c in enumerate((x, y, z)):
                lo[i] = min(lo[i], c)
                hi[i] = max(hi[i], c)
        tix += struct.pack("<h", t["tex"])
        mod += bytes((t["mode"],))
        wrp += bytes((t["wrap"],))
    blob = bytes(pos) + bytes(uv) + bytes(col) + bytes(tix) + bytes(mod) + bytes(wrp)
    while len(blob) % 4:
        blob += b"\0"
    if n == 0:
        lo = hi = [0.0, 0.0, 0.0]
    return blob, n, lo, hi


# --------------------------------------------------------------------- textures
def write_textures(pack, outdir):
    """Every texture in the bank, base and GDI variant, as PNG.

    74 of the 293 carry a GDI variant because the cartridge keeps two house TLUTs and
    they differ on the unit body ramps and the building accents. The base is the
    Nod/neutral table, which is what a neutral or Nod object draws; GoodGuy draws the
    variant. This is the fix the old viewer never got: it decoded everything through
    the Nod table, so GDI kit came out grey.
    """
    from PIL import Image
    os.makedirs(outdir, exist_ok=True)
    meta = []
    for i, tx in enumerate(pack["tex"]):
        im = Image.frombytes("RGBA", (tx["w"], tx["h"]), bytes(tx["data"]))
        im.save(os.path.join(outdir, "t%03d.png" % i))
        if tx["gdi"]:
            Image.frombytes("RGBA", (tx["w"], tx["h"]),
                            bytes(tx["gdi"])).save(os.path.join(outdir, "t%03d_gdi.png" % i))
        meta.append(dict(w=tx["w"], h=tx["h"], uw=tx["uw"], uh=tx["uh"],
                         gdi=bool(tx["gdi"])))
    return meta


# ------------------------------------------------------------------ OBJ / glTF
def write_obj(path, code, tris, texlist, texnames):
    """OBJ + MTL. Loses the pivots (OBJ has no node transforms) and says so."""
    mtl = os.path.splitext(path)[0] + ".mtl"
    with open(path, "w") as f:
        f.write("# %s -- Command & Conquer (N64), exported by CNC3D\n" % code)
        f.write("# axes x=east y=up z=south, 1 map cell = 1024 units\n")
        f.write("# NOTE OBJ carries no pivots: a turret's rotation origin is lost here.\n")
        f.write("#      Use the FBX export if the model has to go back into the game.\n")
        f.write("mtllib %s\n" % os.path.basename(mtl))
        for t in tris:
            for v in t["v"]:
                f.write("v %.6f %.6f %.6f\n" % (v[0], v[1], v[2]))
        for t in tris:
            for v in t["v"]:
                f.write("vt %.6f %.6f\n" % (v[3], 1.0 - v[4]))
        i = 1
        cur = None
        for t in tris:
            slot = texlist.index(t["tex"]) if t["tex"] >= 0 else -1
            if slot != cur:
                f.write("usemtl %s\n" % ("%s_tex%02d" % (code, slot) if slot >= 0
                                         else "%s_flat" % code))
                cur = slot
            f.write("f %d/%d %d/%d %d/%d\n" % (i, i, i + 1, i + 1, i + 2, i + 2))
            i += 3
    with open(mtl, "w") as f:
        for slot, ti in enumerate(texlist):
            f.write("newmtl %s_tex%02d\nKd 1 1 1\nd 1\nmap_Kd %s\n\n"
                    % (code, slot, texnames[slot]))
        f.write("newmtl %s_flat\nKd 1 1 1\n" % code)


def write_gltf(path, code, tris, texlist, texnames):
    """glTF 2.0, one primitive per texture, buffer embedded as a data URI.

    Vertex colours ride as COLOR_0. They are the cartridge's baked shading, and the
    material is unlit for the same reason the renderer is: the N64 never lit these.
    KHR_materials_unlit is declared so a viewer that understands it does the right
    thing, and the fallback is a full-rough metal-free PBR material, which is as close
    as core glTF gets.
    """
    prims = []
    buf = bytearray()
    accs = []
    views = []

    def add(data, target, comp, typ, count, mn, mx):
        while len(buf) % 4:
            buf.append(0)
        off = len(buf)
        buf.extend(data)
        views.append(dict(buffer=0, byteOffset=off, byteLength=len(data), target=target))
        a = dict(bufferView=len(views) - 1, componentType=comp, count=count, type=typ)
        if mn is not None:
            a["min"], a["max"] = mn, mx
        accs.append(a)
        return len(accs) - 1

    groups = {}
    for t in tris:
        groups.setdefault(t["tex"], []).append(t)
    for ti, gt in groups.items():
        pos, uv, col = bytearray(), bytearray(), bytearray()
        lo, hi = [1e30] * 3, [-1e30] * 3
        for t in gt:
            for v in t["v"]:
                pos += struct.pack("<3f", v[0], v[1], v[2])
                uv += struct.pack("<2f", v[3], v[4])
                col += struct.pack("<4H", v[5] * 257, v[6] * 257, v[7] * 257, v[8] * 257)
                for i, c in enumerate(v[:3]):
                    lo[i] = min(lo[i], c)
                    hi[i] = max(hi[i], c)
        n = len(gt) * 3
        p = add(pos, 34962, 5126, "VEC3", n, lo, hi)
        u = add(uv, 34962, 5126, "VEC2", n, None, None)
        c = add(col, 34962, 5123, "VEC4", n, None, None)
        prim = dict(attributes=dict(POSITION=p, TEXCOORD_0=u, COLOR_0=c), mode=4)
        if ti >= 0:
            prim["material"] = texlist.index(ti)
        prims.append(prim)

    mats, texs, imgs = [], [], []
    for slot, ti in enumerate(texlist):
        imgs.append(dict(uri=texnames[slot]))
        texs.append(dict(source=slot, sampler=0))
        mats.append(dict(
            name="%s_tex%02d" % (code, slot),
            pbrMetallicRoughness=dict(
                baseColorTexture=dict(index=slot), metallicFactor=0.0,
                roughnessFactor=1.0),
            alphaMode="MASK", alphaCutoff=0.5, doubleSided=True,
            extensions={"KHR_materials_unlit": {}}))
    g = dict(
        asset=dict(version="2.0",
                   generator="CNC3D model gallery (from the game's own baked pack)"),
        extensionsUsed=["KHR_materials_unlit"],
        scene=0, scenes=[dict(nodes=[0])],
        nodes=[dict(mesh=0, name=code)],
        meshes=[dict(name=code, primitives=prims)],
        materials=mats, textures=texs, images=imgs,
        samplers=[dict(magFilter=9729, minFilter=9729, wrapS=10497, wrapT=10497)],
        accessors=accs, bufferViews=views,
        buffers=[dict(byteLength=len(buf),
                      uri="data:application/octet-stream;base64," +
                          base64.b64encode(bytes(buf)).decode())])
    with open(path, "w") as f:
        json.dump(g, f)


# --------------------------------------------------------------------- assembly
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", default=DEFAULT_PACK)
    ap.add_argument("--no-exports", action="store_true",
                    help="data only; skip the pre-baked FBX/OBJ/glTF")
    ap.add_argument("--no-briefing", action="store_true",
                    help="skip the 16 ROM briefing meshes (needs the ROM)")
    a = ap.parse_args()

    pack = PI.read(a.pack)
    print("pack %s  v%d  %d meshes, %d textures, %d type codes"
          % (os.path.basename(a.pack), pack["version"], len(pack["meshes"]),
             len(pack["tex"]), len(pack["types"])))

    txt, gpl, km = CAT.build()
    os.makedirs(OUT, exist_ok=True)

    # code -> mesh, and mesh -> codes
    m2c = {}
    for c, mi, _f in pack["types"]:
        m2c.setdefault(mi, []).append(c)

    # which mesh index is the parent of a variant mesh
    byname = {m["name"]: i for i, m in enumerate(pack["meshes"])}
    parent_of = {}
    for i, m in enumerate(pack["meshes"]):
        v = VARIANT.match(m["name"])
        if v and v.group(1) in byname:
            parent_of[i] = byname[v.group(1)]

    texmeta = write_textures(pack, os.path.join(OUT, "tex"))
    print("textures: %d (%d with a GDI variant)"
          % (len(texmeta), sum(1 for t in texmeta if t["gdi"])))

    geo = bytearray()
    assets = []
    unnamed = []
    for mi in sorted(k for k in m2c if k >= 0):
        if mi in parent_of:
            continue                      # a variant; it is attached below
        mesh = pack["meshes"][mi]
        codes = sorted(m2c[mi])
        # the canonical code: the one unit_models.json binds to THIS mesh, else the
        # first that EA's source names, else the shortest.
        code = None
        for c in codes:
            if km.get(c, {}).get("mesh") == mesh["name"]:
                code = c
                break
        if code is None:
            named = [c for c in codes if c in gpl]
            code = named[0] if named else sorted(codes, key=lambda s: (len(s), s))[0]
        tris = read_tris(mesh)
        # Measured, not assumed: a wall's S set is shadow geometry only if every one of
        # its triangles actually draws in a blended pass (MODE_SHADOW or MODE_XLU).
        shadow = bool(tris) and all(t["mode"] in (2, 3) for t in tris)
        name, cat, why = CAT.resolve(code, txt, gpl, km, shadow=shadow)

        blob, n, lo, hi = pack_geometry(tris, pack["tex"])
        off = len(geo)
        geo += blob

        # Every code the pack points at this mesh, with what unit_models.json says about
        # it. A low-confidence alias is shown as low-confidence rather than silently
        # folded in: ROAD lands on the 120MM missile body and its own note says the mesh
        # "is not obviously a road".
        aliases = []
        for c in codes:
            if c == code:
                continue
            k = km.get(c, {})
            nm2, _cat2, _w2 = CAT.resolve(c, txt, gpl, km, shadow=shadow)
            aliases.append(dict(code=c, name=nm2, confidence=k.get("confidence"),
                                same_mesh=(k.get("mesh") == mesh["name"])))

        used = []
        for t in tris:
            if t["tex"] >= 0 and t["tex"] not in used:
                used.append(t["tex"])
        modes = sorted(set(MODE[t["mode"]] for t in tris))
        variants = []
        for vi, pi in parent_of.items():
            if pi != mi:
                continue
            vm = pack["meshes"][vi]
            vtris = read_tris(vm)
            vblob, vn, vlo, vhi = pack_geometry(vtris, pack["tex"])
            voff = len(geo)
            geo += vblob
            variants.append(dict(
                code=sorted(m2c[vi])[0], mesh=vm["name"], codes=sorted(m2c[vi]),
                off=voff, tris=vn, bbox=[vlo, vhi],
                tex=[t for t in dict.fromkeys(t["tex"] for t in vtris) if t >= 0]))
        anim = mesh["anim"]
        assets.append(dict(
            id=code, code=code, name=name, category=cat, provenance=why,
            codes=codes, aliases=aliases, mesh=mesh["name"], off=off, tris=n,
            bbox=[lo, hi], tex=used, modes=modes,
            parts=[dict(tri0=p[0], tris=p[1], role=ROLE.get(p[2], str(p[2])),
                        pivot=list(p[3])) for p in mesh["parts"]],
            sections=len(mesh["sections"]),
            anim=(dict(frames=anim["frames"],
                       clips=[dict(t0=c["t0"], t1=c["t1"], loop=bool(c["loop"]))
                              for c in anim["clips"]]) if anim else None),
            variants=variants,
            gdi=any(texmeta[t]["gdi"] for t in used),
            source="pack"))
        if cat == "Misc" and code == name:
            unnamed.append(code)

    print("assets from the pack: %d (%d variant meshes attached to a parent)"
          % (len(assets), len(parent_of)))

    # ---------------------------------------------------------------- briefing
    brief, rom = {}, None
    if not a.no_briefing:
        try:
            import fbx_export as FE
            rom = FE.load_rom()
            models = FE.briefing_models(rom)
            for nm, dl in models.items():
                m = FE.build_mesh(rom, dl)
                tris, used, lit = brief_tris(rom, m, texmeta,
                                             os.path.join(OUT, "tex"))
                brief[nm] = dict(tris=tris, tex=used, lit=lit, dl=dl)
                blob, n, lo, hi = pack_geometry(tris, texmeta)
                off = len(geo)
                geo += blob
                why = ("named by the cartridge: the briefing overlay's own node and "
                       "name arrays at ROM 0x01DEBE0 / 0x01DECE0")
                if lit:
                    why += (". Its Vtx bytes are packed NORMALS, not baked colour, so "
                            "it is shaded here with the console's own resident light "
                            "(ambient 16, diffuse 250, L = -64/+64/+64)")
                assets.append(dict(
                    id=nm, code=nm, name=brief_name(nm),
                    category="Logos" if nm in LOGOS else "Briefing",
                    provenance=why,
                    codes=[nm], aliases=[], mesh="dl_%07X" % dl, off=off,
                    tris=n, bbox=[lo, hi], tex=used, modes=["opaque"], parts=[],
                    sections=0, anim=None, variants=[], gdi=False, source="rom",
                    lit=lit))
            print("briefing meshes: %d" % len(models))
        except Exception as e:                                  # noqa: BLE001
            print("briefing meshes SKIPPED: %s" % e)

    open(os.path.join(OUT, "geo.bin"), "wb").write(bytes(geo))
    cats = [c for c in CATEGORY_ORDER if any(x["category"] == c for x in assets)]
    manifest = dict(
        pack=os.path.basename(a.pack), pack_version=pack["version"],
        theater=pack["theater"], categories=cats, textures=texmeta,
        assets=sorted(assets, key=lambda x: (CATEGORY_ORDER.index(x["category"]),
                                             x["name"], x["code"])))
    json.dump(manifest, open(os.path.join(OUT, "assets.json"), "w"))
    print("geo.bin %.1f KB, %d assets, %d categories"
          % (len(geo) / 1024.0, len(assets), len(cats)))
    if unnamed:
        print("NO NAME IN ANY SOURCE (%d, shown by cartridge code): %s"
              % (len(unnamed), " ".join(unnamed)))

    if not a.no_exports:
        bake_exports(pack, manifest, brief, rom)


def brief_name(nm):
    return {
        "BRF_LOGO_GDI": "GDI Medallion",
        "BRF_LOGO_NOD": "Nod Medallion",
        "GDI_LOGO_TITLE_ROOT": "GDI Title Logo",
        "BRF_EVA_ROOT": "EVA Wordmark",
        "BRF_SPEAKER_ROOT": "Briefing Speaker",
        "BRF_POINTYARROW_ROOT": "Briefing Arrow",
        "BRF_3DSCREENS_ROOT": "Briefing Screens",
        "BRF_3DSCREENS_BLACK_TRIANGLE_ROOT": "Briefing Screen Triangle",
        "BRF_3DSCREENS_BLACK_TRIANGLE2_ROOT": "Briefing Screen Triangle 2",
        "BRF_UNIT_ORCA_ROOT": "Orca (briefing)",
        "BRF_UNIT_MAMMOTH_ROOT": "Mammoth Tank (briefing)",
        "BRF_UNIT_GUNBOAT_ROOT": "Gunboat (briefing)",
        "BRF_UNIT_FLAMETANK_ROOT": "Flame Tank (briefing)",
        "BRF_UNIT_STEALTHTANK_ROOT": "Stealth Tank (briefing)",
        "BRF_UNIT_CHINOOK_ROOT": "Chinook (briefing)",
        "BRF_NOD_TEMPLE_ROOT": "Temple of Nod (briefing)",
    }.get(nm, nm)


# The cartridge's own resident directional light, recovered from .data and written up
# in tools/romdump/terrain_light_notes.md: ambient 16, diffuse 250, L = (-64, +64, +64).
# The briefing lists run with G_LIGHTING ON and carry packed NORMALS where a pack mesh
# carries baked colour, so there is nothing to modulate by until a light is applied.
# Using the console's own resident light rather than inventing one keeps this honest;
# it is still this tool's choice of light, and the UI says so on those assets.
LIGHT = (-64.0, 64.0, 64.0)
AMBIENT, DIFFUSE = 16.0 / 255.0, 250.0 / 255.0


def brief_tris(rom, m, texmeta, texdir):
    """A ROM briefing mesh, converted into the same triangle shape a pack mesh has, so
    everything downstream (the viewer's geometry, OBJ, glTF) has exactly one code path.

    Their textures continue past the pack's 293-entry bank rather than colliding with
    it, and are written unpadded, so the UV scale downstream is 1.
    """
    import fbx_export as FE
    ln = sum(c * c for c in LIGHT) ** 0.5
    L = tuple(c / ln for c in LIGHT)
    base = len(texmeta)
    slot = []
    for mat in m["mats"]:
        t = mat["tex"]
        png = FE.texture_png(rom, t) if t else None
        if png is None:
            slot.append(-1)
            continue
        idx = base + len([s for s in slot if s >= 0])
        open(os.path.join(texdir, "t%03d.png" % idx), "wb").write(png)
        texmeta.append(dict(w=t["w"], h=t["h"], uw=t["w"], uh=t["h"], gdi=False))
        slot.append(idx)

    lit = m["kind"] == "normal"
    tris = []
    for fi, tri in enumerate(m["polys"]):
        vs = []
        for k, vi in enumerate(tri):
            x, y, z = m["positions"][vi]
            u, v = m["poly_uv"][fi * 3 + k]
            if lit:
                nx, ny, nz = m["poly_nrm"][fi * 3 + k]
                d = max(0.0, nx * L[0] + ny * L[1] + nz * L[2])
                c = int(round(min(1.0, AMBIENT + DIFFUSE * d) * 255))
                r = g = b = c
            else:
                fr, fg, fb = m["poly_col"][fi * 3 + k]
                r, g, b = int(fr * 255), int(fg * 255), int(fb * 255)
            vs.append((x, y, z, u, v, r, g, b, 255))
        tris.append(dict(tex=slot[m["poly_mat"][fi]], mode=0, wrap=0, v=vs))
    return tris, [s for s in dict.fromkeys(slot) if s >= 0], lit


# ----------------------------------------------------------------- pre-baked
def bake_exports(pack, manifest, brief, rom):
    """FBX / OBJ / glTF for every asset, written once so the page can stay a static
    site: the browser fetches these and zips them rather than needing a server that can
    run Python.

    The FBX comes from tools/art/fbxout.py, the writer that was verified by reading its
    own output back with an independently written binary FBX parser, and for the ROM
    briefing meshes from tools/romdump/fbx_export.py, verified the same way against
    three.js's FBXLoader.

    ONE FBX serves both houses. The two house palettes differ only in the texture bytes,
    and the file references its PNGs by the same relative names either way, so the
    palette is chosen by which texture folder the zip carries, not by a second model.
    """
    import fbxout as FX
    root = os.path.join(OUT, "export")
    if os.path.exists(root):
        shutil.rmtree(root)
    dirs = {k: os.path.join(root, k)
            for k in ("fbx", "obj", "gltf", "tex", "tex-gdi")}
    for d in dirs.values():
        os.makedirs(d, exist_ok=True)

    from PIL import Image
    files = {}
    for asset in manifest["assets"]:
        code = asset["code"]
        ent = {}
        if asset["source"] == "pack":
            mi = None
            for c, idx, _f in pack["types"]:
                if c == code:
                    mi = idx
                    break
            if mi is None or mi < 0:
                continue
            mesh = pack["meshes"][mi]
            tris = read_tris(mesh)
            roots, _nodes, texmeta = FX.build(pack, code, mesh, dirs["tex"], "nod")
            FX.write_fbx(os.path.join(dirs["fbx"], code + ".fbx"), roots)
            texnames = [t["file"] for t in texmeta]
            if asset["gdi"]:
                used = [t["bank_index"] for t in texmeta]
                FX.dump_textures(pack, used, dirs["tex-gdi"], code, "gdi")
            texlist = []
            for t in tris:
                if t["tex"] >= 0 and t["tex"] not in texlist:
                    texlist.append(t["tex"])
        else:
            g = brief.get(code)
            if not g:
                continue
            tris, texlist, _lit = g["tris"], g["tex"], g["lit"]
            import fbx_export as FE
            FE.export(rom, code, g["dl"], dirs["fbx"])
            texnames = []
            for slot, ti in enumerate(texlist):
                fn = "%s_tex%02d.png" % (code, slot)
                shutil.copyfile(os.path.join(OUT, "tex", "t%03d.png" % ti),
                                os.path.join(dirs["tex"], fn))
                texnames.append(fn)
        ent["fbx"] = dict(model=code + ".fbx", extra=[])
        write_obj(os.path.join(dirs["obj"], code + ".obj"), code, tris, texlist, texnames)
        ent["obj"] = dict(model=code + ".obj", extra=[code + ".mtl"])
        write_gltf(os.path.join(dirs["gltf"], code + ".gltf"), code, tris, texlist, texnames)
        ent["gltf"] = dict(model=code + ".gltf", extra=[])
        ent["tex"] = texnames
        ent["gdi"] = bool(asset["gdi"])
        files[code] = ent
    json.dump(files, open(os.path.join(root, "manifest.json"), "w"))
    print("pre-baked exports for %d assets (fbx, obj, gltf; two texture palettes)"
          % len(files))

if __name__ == "__main__":
    main()
