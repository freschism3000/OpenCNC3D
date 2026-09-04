#!/usr/bin/env python3
"""
CNC3D asset baker, PK5: per-part submeshes with mount transforms and roles.

WHAT CHANGED FROM PK4. The PK4 baker (surviving as bake_pk4.pyc, loaded via
pk4mod.py; its behaviour is reproduced byte for byte on SCB01EA before this
extension) flattened a model's scene-graph parts by concatenating each part's
display-list triangles UNTRANSFORMED. Two defects followed:
  1. Models whose scene graph lives in the ScriptModels segment (HTNK, TRAN,
     BOAT, FACT, SAM, PROC, SILO, plus TMPL/FTNK/STNK/ORCA which have no
     children) lost their moving parts entirely, because the old objgraph only
     accepted GameModelsGeometry pointers. The Mammoth had no turret AT ALL.
  2. Models whose parts were found (MTNK, LTNK, JEEP, BGGY, GUN, HARV, APC,
     ATWR) had them drawn at the part's LOCAL origin: the Medium Tank's turret
     sat sunk into the hull at y=-60..93 instead of on top at y=153..306.
This baker walks the graph with objgraph2 (both segments), applies each part's
CAFEDEAD/BEEFED02 mount transform to the baked vertices, and records the part
boundaries in the pack so the renderer can rotate the turret about its own
mount pivot and spin rotors.

PACK FORMAT: "CNC3DPK5", version 5. Identical to PK4 except each mesh gains,
directly after its triangle block:
    u32 nparts                    (>= 1; exactly 1 with role 0 for plain meshes)
    per part:
        u32 tri0, u32 ntris      contiguous range into this mesh's triangles
        u32 role                 0 static, 1 turret, 2 rotor
        f32 pivot[3]             mount pivot, model units, mesh frame
The triangles inside a part's range already carry the mount transform; the
pivot is only there so the renderer can rotate the part FURTHER (turret yaw =
engine tface, rotor spin) about the right vertical axis.

ROLES are derived from engine truth plus geometry, and asserted against the
expected table below so ROM or logic drift fails the bake loudly. Evidence per
model is in parts_roles.json (written on every bake) and in the round report.

PK6: HOUSE COLOURS. The cartridge keeps THREE resident 256-entry TLUTs, 0x400
bytes apart, and a selector function (RAM 0x80055d08, resident code) that loads
one before drawing:
    selector 0            -> ROM 0x98F30   GDI: entries 16..31 are a SAND ramp
    selector > 0          -> ROM 0x99130   Nod/neutral: the same entries BLUE-GREY
                                           (this is the one every earlier bake used
                                           for everything, which is why GDI vehicles
                                           rendered grey)
    selector 0x80000001   -> ROM 0x99330   all-white: the damage flash
    selector -1           -> keep current
The selector values are HousesType: HOUSE_GOOD=0, HOUSE_BAD=1. The two house
tables differ on entries 0..63 (unit body ramps), 99, 103, 160..162 and 188..191
(building accents: RED in the Nod table, gold in the GDI one -- the same split the
DOS game makes with RemapRed). Proven against reference console screenshot: GDI
vehicles and building accents are sand/gold on real hardware.

Format change, magic "CNC3DPK6" version 6: each texture record gains, after its
pixel block, u8 has_gdi and (when 1) a second same-sized pixel block decoded
through the GDI table. Textures the two tables decode identically (terrain,
sprites, anything indexed outside the differing entries) carry has_gdi=0. The
renderer binds the GDI variant for GoodGuy objects and the base for everyone
else, which is exactly the console's selector rule.

PK7: CONSTRUCTION SECTIONS. the reference video of the real cart shows a placed building
assembling PIECE BY PIECE over the ~5 s buildup (and shedding pieces in reverse
when sold) -- not the DOS MAKE.SHP scaffold, which this project wrongly shipped
first. The pieces in the footage are whole coherent chunks (a box, a dish), which
is exactly the granularity of the display list's own G_VTX vertex batches: the
console loads a batch of vertices, draws its triangles, loads the next. So the
SECTION TABLE baked here is read straight out of the cartridge's display lists:
one section per vertex-load batch, in DL order, expressed as starting triangle
indices into the mesh's baked triangle list (which was itself produced by walking
the same DLs in the same order -- the per-mesh assert below proves the two walks
agree triangle for triangle). The renderer then draws the first K sections while
the ENGINE's construction stage runs, K = ceil(sections * (stage+1) / count).
The stage->K mapping is the one part not lifted from the ROM (the bounding code
in the console's builder has not been located); the section set, order and the
reversal on sell are all cartridge/engine truth, and the result is verified
against the console footage.

Format, magic "CNC3DPK7" version 7: each mesh record gains, directly after its
part table:
    u32 nsections                 (>= 1)
    u32 tri0 x nsections          ascending; section k covers [tri0[k], tri0[k+1])

PK8: WATER ART. The cartridge draws its rivers as the PRODUCT of two 32x32
RGBA5551 tiles, WATER1.IMG x WATER2.IMG, at prim alpha 170/255, with the two
layers scrolling in OPPOSITE senses (combiner FC1147FF/FFFFFE38 at ROM
0x019B99C; prim alpha byte 0xAA at RAM 0x80097ABB; the scroll rates
{6.5, 4.6} and {-2.8, -2.2} at RAM 0x80097AC4). Both images live in the ROM's
own archive under method 0x2200, which is the only reason they were ever
thought undecodable: see tools/romdump/imgsqueeze.py, whose `verify` decodes
771 of 771 .IMG records. They are baked here, unchanged, into the ordinary
texture bank.

WATERTST.IMG is deliberately NOT baked: it differs from WATER1 in 16 of 1024
texels and the cartridge's own gterrain.c name table never references it, so it
is a dev leftover rather than shipped art.

Format, magic "CNC3DPK8" version 8: two int32 bank indices, WATER1 then WATER2,
appended at the very END of the file. Appending is the point -- every PK5/6/7
reader parses a PK8 pack correctly and simply stops before the tail. A PK8 pack
whose trailing indices are -1 is a FAILED bake, not a fallback, and build()
asserts rather than writing one.

PK8 also carries the two bakery defects behind the missile's look:
  * the TMEM-line texture stride (fixed in support/vx_rdp.py; it moved exactly
    the 5 textures per pack narrower than one TMEM word, indices 24, 59, 60, 96
    and 101, and left the other 236 byte-identical), and
  * normals baked as vertex colours (flatten_lit_meshes below).

PK9: THE HEIGHTMAP. Every campaign scenario ships a 65x65 per-corner heightmap
disguised as an ordinary intensity .IMG named after the scenario (SCG01EA.IMG),
with FLAT.IMG (all zeros) as the engine's own fallback -- which is exactly why
the bake was flat until now. Decoded provenance: n64engine.cpp loader resident
RAM 0x80048574 (malloc 0x1081 = 65*65, sprintf "%s.img"), gterrain.c vertex
builder RAM 0x801F4984: vtx.y = heightmap[y*65+x] * 4 with cell pitch 256, so
one height unit is 4/256 = 1/64 of a world cell. Full trail in
tools/romdump/heightmap_notes.md; extractor tools/romdump/heights.py.

PKA: THE SEA FLOOR. The console's water surface is alpha-blended at 170/255,
so it needs something underneath. gterrain.c runs a seabed pass (CodeOverlay
ROM 0x19B488..0x19B88C, gated on RAM 0x80097AC0 = 1) that draws BOTTOM.IMG over
every water cell at that cell's own terrain corner heights, exactly one 32x32
tile per world cell, before the water goes on. The terrain texture directory at
ROM 0x16A76C lists bottom.img third, straight after water1/water2.

Format, magic "CNC3DPKA" version 10: one int32 BOTTOM bank index appended
directly BEFORE the two water int32s, which remain the last eight bytes of the
file for ever.

Format, magic "CNC3DPK9" version 9: the 65*65 = 4225 raw corner bytes (row
major, same orientation as the .MAP) appended directly BEFORE the two water
int32s, which stay the LAST eight bytes of the file (that tail contract is
append-only and permanent). A scenario with no heightmap in the ROM bakes
FLAT.IMG's zeros, exactly as the console would have run it.

PKC: THE CM TINT LAYER. The console's terrain ground pass does NOT draw
texel * shade. Its combiner (G_SETCOMBINE 0xFC15982B / 0x4433FFFF emitted at
ROM 0x19A5AC under G_CYC_1CYCLE, ROM 0x19C3C0) is

    out = clamp8( ((TEXEL0 - SHADE_RGB) * SHADE_ALPHA + 0x80) >> 8 )

per channel, where SHADE_ALPHA is the per-corner light the PK9 heightmap
already feeds and SHADE_RGB is a small warm tint the vertex builder reads out
of a SECOND per-corner map. That map is CM<SCEN>.IMG, 65x65 RGBA5551, on
exactly the same corner grid as the heightmap: the loader builds its name by
overwriting the first two characters of "<scen>.img" with "cm" (n64engine.cpp
ROM 0x492A0 / 0x492AC) and falls back to CMFLAT.IMG (ROM 0x5270), which is 4225
copies of 0x0001 -- RGB 0 everywhere, an exact no-op. 54 CM maps exist.

The vertex builder (gterrain.c colour pass, RAM 0x801F4794 / ROM 0x196584)
scales each 5-bit channel by the per-corner light and by 200/32768:

    tint_c = (u32)( (float)(lit * C5) * 200.0f * (1.0f/32768.0f) )

CM_GAIN 200.0 at RAM 0x800979B4 (ROM 0x00985B4), CM_SHIFT 1/32768 at
RAM 0x801C87EC (ROM 0x016A5DC), channel unpack R=(v>>11)&31 (ROM 0x1965E8),
G=(v>>6)&31 (ROM 0x196624), B=(v>>1)&31 (ROM 0x196634) -- bit 0, the RGBA5551
alpha, is never read. The ceiling is 255*31*200/32768 = 48, so the ROM carries
no clamp and needs none. The bake therefore ships the RAW u16 map and lets the
renderer do the cartridge's arithmetic against the light it already computes:
the same split as the heightmap, and the only one that stays right when the
shroud starts modulating the light.

Format, magic "CNC3DPKC" version 12: the 65*65 = 4225 CM entries as u16
LITTLE-endian (byte-swapped from the cartridge's big-endian .IMG at bake time
so the renderer does no swapping), written directly BEFORE the PK9 heights.
BEFORE, not after: every block from the heights down is read by an EOF-relative
fseek, so anything inserted between the heights and the water tail moves those
seeks and breaks them. Full decode trail: tools/romdump/terrain_cm_notes.md.

Format, magic "CNC3DPKG" version 16: TWO changes, and one of them retires a header.

  1. A second terrain atlas. One int32 is written directly after the existing
     terrain-atlas bank index, holding the bank index of the SAME tiles drawn from
     Tiberian Dawn's own theater art instead of the cartridge's (the cart's TL4/TL8
     tables name the PC (template, icon) every bank slot came from, so the two sets
     line up one for one). Enhanced Visuals binds it; -1 means the theater has no DOS
     original -- SNOW and SAND -- and the switch has nowhere to go. The two atlases are
     packed identically and are the same size, because every cell carries ONE set of
     UVs that has to address both. WINTER's bank was built from that art already, so
     its two atlases are byte-identical and share a single bank slot.
     Why a second atlas rather than a second pack: the choice is a live toggle in the
     Visuals dialog, so both have to be resident. Rationale and the measurements behind
     it are recorded with the terrain bake.

  2. PKE is gone. The grid (u32 mapW, mapH after the version) is now written ALWAYS.
     PKE existed only to keep 64x64 packs byte-identical across the PKF change, and
     since PKG rewrites every pack anyway that saving protects nothing -- while the
     split was a live trap, because baking a big grid as PKE mis-read it with 64x64
     tail offsets and nothing detected it. load_pack's `pkf` test is unaffected: it
     asks magic[7] >= 'F' and 'G' satisfies it.

  The insertion is in the SEQUENTIAL region, not the tail. That is safe here and is
  not a repeat of the PKA regression: every tail block is found by an EOF-relative
  fseek, so bytes added ahead of them do not move them, and the sequential reader is
  version-gated on the magic. The water pair remains the file's last eight bytes.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pk4mod
import texbook as TEXBOOK
import objgraph2 as O2

B = pk4mod.load()

# Engine truth (brain udata.cpp / bdata.cpp, "Is it equipped with a combat
# turret?" / "Does it have a rotating turret?"): types whose tface/face aims a
# turret. INI names as used by unit_models.json.
ENGINE_TURRETED = {"LTNK", "MTNK", "HTNK", "MLRS", "JEEP", "BGGY", "MSAM",
                   "BOAT", "GUN", "SAM"}

# Aircraft whose flat disc parts are rotors (HELI = Apache, TRAN = Chinook).
ROTOR_TYPES = {"HELI", "TRAN"}

# The cartridge's house TLUTs (see the PK6 note in the docstring). B.GLOBAL_TLUT
# is the Nod/neutral table 0x99130; this is the GDI one, 0x400 below it.
GDI_TLUT = 0x98F30

# ---------------------------------------------------------------------------
# Construction sections (PK7): triangles per G_VTX batch, read from the DL.
# A tiny dedicated walker rather than the pyc's: all it needs is the batch
# boundaries, and its per-part triangle totals are asserted against the real
# baker's output so the two walks can never silently disagree.
# ---------------------------------------------------------------------------
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "tools", "romdump")))
import segments as SEG  # noqa: E402

_ROM_BYTES = None


def _rom():
    global _ROM_BYTES
    if _ROM_BYTES is None:
        _ROM_BYTES = open(os.path.join(B.SCR, "share", "CNC3D", "rom",
                                       "cnc_eu.z64"), "rb").read()
    return _ROM_BYTES


# ---------------------------------------------------------------------------
# Water art (PK8). imgsqueeze.py lives in the REAL tools/romdump, not in the
# bakery's sharecopy of it, and it must bind to that tree's archive.py: the
# sharecopy's archive.py predates `unpack()` and the stored-size fix. So load it
# by explicit path with tools/romdump ahead of everything, and fail loudly if it
# is not there. No fallback -- a bake without the cartridge's water art is a
# failed bake, not a degraded one.
# ---------------------------------------------------------------------------
_IQ = None


def _imgsqueeze():
    global _IQ
    if _IQ is not None:
        return _IQ
    import importlib.util
    cands = [os.path.normpath(os.path.join(HERE, up, "tools", "romdump"))
             for up in ("..", os.path.join("..", ".."))]
    path = None
    for c in cands:
        if os.path.isfile(os.path.join(c, "imgsqueeze.py")):
            path = c
            break
    if path is None:
        raise SystemExit("bake5: cannot find tools/romdump/imgsqueeze.py "
                         "(looked in %s). The water art cannot be baked "
                         "without it." % ", ".join(cands))
    for stale in ("archive", "decomp", "decomp11"):
        # never let the sharecopy's older archive.py answer imgsqueeze's import
        sys.modules.pop(stale, None)
    saved_path = list(sys.path)
    sys.path.insert(0, path)
    try:
        spec = importlib.util.spec_from_file_location(
            "cnc3d_imgsqueeze", os.path.join(path, "imgsqueeze.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    finally:
        sys.path[:] = saved_path
    assert mod.archive.__file__.startswith(path), \
        "imgsqueeze bound to the wrong archive.py: %s" % mod.archive.__file__
    _IQ = mod
    return _IQ


def bake_heights(scen, override=None, cw=65, ch=65):
    """The scenario's (W+1)x(H+1) corner heightmap out of the ROM, as a flat
    list of cw*ch ints (4225 for the legacy 64x64 cell grid), row major.
    Reuses tools/romdump/heights.py (the disassembly-
    verified extractor) rather than re-deriving the format here. FLAT.IMG's
    zeros when the scenario ships no heightmap -- the engine's own fallback.

    `override` is how AUTHORED elevation reaches the game. Without it this
    function could only ever return what the cartridge shipped, so a map made
    outside the ROM was flat in play no matter what its author drew -- which is
    why the converted skirmish maps are flat today. Pass cw*ch ints (or bytes)
    on the same row-major corner grid and they are used verbatim.
    """
    if override is not None:
        corners = list(override)
        if len(corners) != cw * ch:
            raise SystemExit("bake5: height override is %d values; a %dx%d "
                             "corner grid is %d" % (len(corners), cw, ch, cw * ch))
        bad = [v for v in corners if not (0 <= int(v) <= 255)]
        if bad:
            raise SystemExit("bake5: height override has %d values outside "
                             "0..255 (first %r); the corner map is one byte "
                             "per corner" % (len(bad), bad[0]))
        return [int(v) for v in corners]
    iq = _imgsqueeze()          # guarantees tools/romdump is first on sys.path
    import importlib.util
    hp = os.path.join(os.path.dirname(iq.__file__), "heights.py")
    spec = importlib.util.spec_from_file_location("cnc3d_heights", hp)
    H = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(H)
    rom = os.path.join(os.path.dirname(iq.__file__), "..", "..", "data", "rom",
                       "cnc_eu.z64")
    names = ["%s.IMG" % scen.upper(), "FLAT.IMG"]
    blobs = H.rom_files(rom, names)
    blob = blobs.get(names[0]) or blobs.get("FLAT.IMG")
    if blob is None:
        raise SystemExit("bake5: neither %s nor FLAT.IMG found in the ROM "
                         "archive -- cannot bake a heightmap" % names[0])
    corners = H.parse_img_heights(blob, names[0])
    if len(corners) != cw * ch:
        raise SystemExit("bake5: %s carries a %d-corner heightmap but this "
                         "scenario's cell grid needs %dx%d = %d corners -- the "
                         "ROM only ships 65x65 maps, so a bigger map needs "
                         "--heights" % (names[0], len(corners), cw, ch, cw * ch))
    return corners


def bake_cm(scen, cw=65, ch=65):
    """The scenario's 65x65 CM tint map out of the ROM, as a flat list of 4225
    ints (raw RGBA5551, host order), row major, on the SAME corner grid and the
    SAME index as the heightmap (gterrain.c ROM 0x1965C8..0x1965D4).

    The engine's own naming rule, not ours: overwrite the first two characters
    of "<scen>.img" with "cm" (n64engine.cpp ROM 0x492A0 / 0x492AC), so
    SCG01EA -> CMG01EA.IMG, with CMFLAT.IMG (ROM 0x5270) as the fallback. A
    scenario with no CM map therefore bakes 4225 * 0x0001 = RGB 0 everywhere,
    which is what the console would have run -- an exact no-op, not a guess.

    Hard-asserts the header rather than trusting it: a CM map that is not
    65x65 RGBA5551 would silently mis-index against the heightmap and paint the
    tint onto the wrong corners, which is precisely the kind of failure that
    looks like art rather than like a bug."""
    iq = _imgsqueeze()          # guarantees tools/romdump is first on sys.path
    import importlib.util
    hp = os.path.join(os.path.dirname(iq.__file__), "heights.py")
    spec = importlib.util.spec_from_file_location("cnc3d_heights", hp)
    H = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(H)
    rom = os.path.join(os.path.dirname(iq.__file__), "..", "..", "data", "rom",
                       "cnc_eu.z64")
    cmname = "CM" + scen.upper()[2:] + ".IMG"
    blobs = H.rom_files(rom, [cmname, "CMFLAT.IMG"])
    blob = blobs.get(cmname)
    src = cmname
    if blob is None:
        blob, src = blobs.get("CMFLAT.IMG"), "CMFLAT.IMG (fallback)"
    if blob is None:
        raise SystemExit("bake5: neither %s nor CMFLAT.IMG found in the ROM "
                         "archive -- cannot bake a CM tint layer" % cmname)
    hdr, _body, w, h = struct.unpack_from(">IIHH", blob, 0)
    fmt, depth, unk, palc = blob[12], blob[13], blob[14], blob[15]
    assert fmt == 0x00 and depth == 0x02, \
        "%s: fmt %#x depth %#x, expected 0x00/0x02 (RGBA5551)" % (src, fmt, depth)
    assert palc == 0 and unk == 0, "%s: not direct colour" % src
    # The FLAT fallback is 65x65 because the cartridge is: no scenario CM exists
    # for an authored map, and a big (Version=1) map's corner grid is 129x129.
    # CMFLAT is literally flat -- one value over the whole 65x65 (verified: the
    # distinct-value count is 1) -- so a differently sized flat map is that value
    # over the requested grid, not a resample. A real per-scenario CM that
    # mismatches is still an error: that is art, and stretching art is a lie.
    if (w, h) != (cw, ch) and src.startswith("CMFLAT"):
        m = w * h * 2
        assert len(blob) == hdr + m, "%s: %d bytes, expected %d" % (src, len(blob), hdr + m)
        flat = set(struct.unpack(">%dH" % (w * h), blob[hdr:hdr + m]))
        assert len(flat) == 1, "%s: not flat (%d values); cannot resize it" % (src, len(flat))
        return src, [flat.pop()] * (cw * ch)
    n = cw * ch * 2
    assert hdr == 0x10 and (w, h) == (cw, ch), \
        "%s: %dx%d header %d, expected a %dx%d corner map" % (src, w, h, hdr, cw, ch)
    assert len(blob) == hdr + n, "%s: %d bytes, expected %d" % (src, len(blob), hdr + n)
    # The cartridge stores the map BIG-endian; the pack is little-endian
    # throughout, so the swap happens here and the renderer never sees it.
    vals = list(struct.unpack(">%dH" % (cw * ch), blob[hdr:hdr + n]))
    for v in vals:
        assert 0 <= v <= 0xFFFF
    return src, vals


def bake_vehicle_shadows(bank):
    """-> list of shadow records for the PKD block, textures added to `bank`.

    The cartridge draws a flat quad under every vehicle and aircraft: black (PRIM),
    alpha from an 8-bit intensity texture, alpha-blended, depth-tested, never
    depth-written. Eighteen of them, one per vehicle/aircraft type, held in a SECOND
    node table at ROM 0x9B3E8 that nothing in a model points at -- which is why a survey
    that walked models concluded, wrongly and at length, that they did not exist. The
    decode, the assertions and the full account of that mistake are in
    tools/romdump/vehicle_shadows.py; this only turns its output into pack bytes.

    The I8 texture becomes RGBA with WHITE rgb and the intensity as ALPHA, so the
    renderer reproduces `colour = PRIM, alpha = TEXEL0_A` with a plain GL_MODULATE
    against a vertex colour carrying PRIM: rgb = prim * 1, alpha = 1 * texA. No shader,
    nothing the Voodoo 2 cannot do.
    """
    import importlib.util
    # bake5.py is reachable BOTH as game/bake5.py and as tools/bakery/bake5.py (a
    # symlink), so HERE is not a fixed distance from the repo root and a single
    # os.path.join(HERE, "..") lands in the wrong place from one of them. Resolve the
    # real file first, then search upward for the repo root, and say what was tried.
    tried = []
    path = None
    for base in (os.path.dirname(os.path.realpath(__file__)), HERE):
        for up in ("..", os.path.join("..", "..")):
            cand = os.path.normpath(os.path.join(base, up, "tools", "romdump",
                                                 "vehicle_shadows.py"))
            tried.append(cand)
            if os.path.isfile(cand):
                path = cand
                break
        if path:
            break
    assert path, "vehicle_shadows.py not found. Tried:\n  " + "\n  ".join(tried)
    spec = importlib.util.spec_from_file_location("vehicle_shadows", path)
    VS = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(VS)

    shadows = VS.extract(_rom())
    names = VS._names()
    out = []
    for r in shadows:
        nm = names.get(r["typeId"])
        assert nm, "shadow type id %d has no name in unit_models.json" % r["typeId"]
        texw, texh, texA, verts = r["texw"], r["texh"], r["texA"], r["verts"]

        # THE FOUR AIRCRAFT ARE HALF A SHADOW EACH, AND THE CONSOLE MIRRORS THEM.
        #
        # A10, ORCA, HELI and C17 carry cmS = G_TX_MIRROR over a 16x32 texture whose S
        # coordinates run 0..2: the art is one wing and the fuselage, and the RDP folds
        # it back to draw the other wing. The renderer used to force GL_CLAMP_TO_EDGE on
        # all eighteen, under a comment claiming the cartridge asks for clamp on all
        # eighteen. It does not, and the comment was never derived from anything: nine
        # are wrap, five are clamp, and these four mirror. Under clamp the S in 1..2 half
        # samples the last texel column for its whole width, so every Orca, Apache, A-10
        # and C-17 drew half an aircraft plus a solid slab where the other wing goes.
        # Measured off the shipped pack before this fix: the mirrored half went from 146
        # opaque texels of 512 to 464 for the A10, 169 to 464 for ORCA, 94 to 464 for
        # HELI and 151 to 464 for C17.
        #
        # The fix is here rather than in the renderer, and that is deliberate. Baking the
        # mirror produces one whole 32-wide texture with S back in 0..1, so the renderer
        # keeps its single clamp, the pack format does not change, and Tier 1 gains
        # NOTHING: docs/tier1-gap.md already names "pre-mirrored texture baked at pack
        # time" as the Voodoo 2 answer for GL_MIRRORED_REPEAT, because Glide has no
        # mirror wrap. Doing it at bake time means Tier 1 and Tier 2 draw the same
        # pixels instead of the fallback being a second thing to test.
        mirrored = (r["cmS"] == 1)
        if mirrored:
            wide = bytearray()
            for row in range(texh):
                line = texA[row * texw:(row + 1) * texw]
                wide += line               # S in 0..1, forward
                wide += line[::-1]         # S in 1..2, folded back, which is the mirror
            texA = bytes(wide)
            texw = texw * 2
            verts = [(x, y, z, u * 0.5, v) for (x, y, z, u, v) in verts]

        rgba = bytearray()
        for a in texA:
            rgba += bytes((255, 255, 255, a))
        bank_ix = bank.add_rgba(("vshadow", r["timg"], mirrored), texw, texh, bytes(rgba))

        # Whatever we just did, the quad must end up inside its own texture, because the
        # renderer draws every shadow with clamp and out-of-range S is exactly the bug
        # this is fixing. A texel of slop, which ORCA's authored coordinates use.
        for (_x, _y, _z, u, v) in verts:
            assert -0.05 <= u <= 1.05 and -0.05 <= v <= 1.05, \
                "%s shadow UV (%.3f, %.3f) leaves 0..1 after baking -- it would smear " \
                "under the renderer's clamp" % (nm, u, v)

        out.append((nm, bank_ix, r["prim"], verts))
    assert len(out) == 18, len(out)
    # JEEP and BGGY genuinely share one node, so their bank entries SHOULD collapse.
    # Everything else sharing would mean the bank key is wrong.
    assert len({ix for _n, ix, _p, _v in out}) == 5, \
        "expected 5 distinct shadow textures in the bank, got %d" % len(
            {ix for _n, ix, _p, _v in out})
    return out


def bake_water(bank):
    """-> (index of WATER1, index of WATER2, index of BOTTOM) in the texture bank.

    Hard-asserts 32x32: the cartridge's tile shift and mask (G_SETTILE word1
    0x0001705C at ROM 0x019B9CC, maskS = maskT = 5) wrap at exactly 32 texels,
    and the renderer's GL_REPEAT tiling depends on it.

    BOTTOM.IMG is the SEA FLOOR, and it is not optional. The console's water
    surface is alpha-blended at 170/255, so something has to be underneath it:
    gterrain.c runs a seabed pass (CodeOverlay ROM 0x19B488..0x19B88C, gated on
    RAM 0x80097AC0 = 1) that draws BOTTOM over every water cell at the cell's own
    terrain corner heights, one 32x32 tile per world cell, before the water goes
    on top. The terrain texture directory at ROM 0x16A76C lists it third, right
    after water1.img and water2.img, and gterrain.c loads the three into
    gTerrain+0xC044/+0xC048/+0xC04C -- the water pass reads 0x4044/0x4048 and
    the seabed pass reads 0x404C. Without it a translucent sea blends onto the
    clear colour and reads as flat dark blue."""
    IQ = _imgsqueeze()
    rom = _rom()
    out = []
    for name in ("WATER1", "WATER2", "BOTTOM"):
        res = IQ.imgmod.decode(IQ.find(rom, name))
        assert res is not None, "%s.IMG header does not solve" % name
        m, rows = res
        assert (m["w"], m["h"]) == (32, 32), \
            "%s.IMG is %dx%d, expected 32x32" % (name, m["w"], m["h"])
        assert m["bpp"] == 16, "%s.IMG is %d bpp, expected 16" % (name, m["bpp"])
        px = b"".join(rows)
        assert len(px) == 32 * 32 * 4, name
        out.append(bank.add_rgba(("water", name), 32, 32, px))
    assert out[0] != out[1], \
        "WATER1 and WATER2 collapsed to one bank entry -- the two tiles are " \
        "supposed to differ (the console multiplies one by the other)"
    assert out[2] != out[0] and out[2] != out[1], \
        "BOTTOM collapsed onto a water tile -- the seabed is a different image"
    return out[0], out[1], out[2]


# ---------------------------------------------------------------------------
# Normals baked as vertex colours (the rocket's darkness).
# ---------------------------------------------------------------------------
def _looks_like_packed_normal(r, g, b):
    """A packed unit normal in s8: the VECTOR has length ~127, whatever its direction.

    THIS USED TO TEST FOR AXIS-ALIGNED NORMALS ONLY -- one channel in 126..130 and the
    other two <= 2 -- and that was a real bug with visible pixels, not a tidiness issue.
    It happened to be sufficient for the only meshes that tripped it at the time (the
    rocket and the barbed wire, whose normals are one per box face and therefore all
    axis-aligned), so it looked complete. It is not: an OFF-AXIS normal like (228,121,231)
    or (66,103,31) has unit length but no channel near zero, so it slipped through -- and
    because flatten_lit_meshes only acts on a mesh whose EVERY vertex matches, one
    off-axis vertex left the WHOLE mesh unwhitened.

    MEASURED 24 Aug 2026, when the cartridge's damage overlays were baked: nine of the
    twenty damage display lists issue no G_GEOMETRYMODE at all and therefore inherit
    G_LIGHTING, but only two of them (HOSP, FIX) are entirely axis-aligned. The other
    seven shipped with the renderer modulating the damage texture by a direction vector,
    which drew a bright magenta smear across a damaged Construction Yard's roof and a
    dark green blob on its apron. Sample values that the old test missed: (228,121,231),
    (66,103,31), (237,61,109), (247,114,54), (0,126,254) -- all magnitude about 126.

    The length test is what the docstring above always described, so this is the code
    catching up with its own stated intent. s8: 0..127 is 0..+1, 128..255 is -1..0."""
    def s8(v):
        return v - 256 if v > 127 else v
    x, y, z = s8(r), s8(g), s8(b)
    m2 = x * x + y * y + z * z
    return 118 * 118 <= m2 <= 136 * 136


def flatten_lit_meshes(meshes, verbose=True):
    """Flatten to white any mesh whose EVERY vertex colour is a packed normal.

    A display list that never issues G_GEOMETRYMODE inherits G_LIGHTING from the
    scene, and under G_LIGHTING the vertex's colour slot holds a packed unit
    normal, not a colour. dl_01051A0 (DRAGON / MISSILE / BOMBLET) is such a list
    -- scanned over its full call graph it contains no 0xD9 command at all,
    while the 120MM list dl_0102848 contains two -- so its 36 vertices carry
    (0,0,+-127), (0,+-127,0), (+-127,0,0), one per box face, and modulating the
    body texture by them draws the rocket as near-black red, green and blue
    faces. We do not light meshes, so the honest substitute is white: the body
    texture then shows through as authored. This is a deliberate simplification
    and is recorded as a known gap.

    A KNOWN, CLOSED set of meshes must trip this (EXPECT_LIT below). Anything
    outside that set means either the ROM walk changed or the signature is
    catching a real colour, and either way the bake should stop rather than
    quietly repaint geometry."""
    hit = []
    for name, tris in meshes:
        if not tris:
            continue
        n = tot = 0
        for (_ti, _mode, _wrap, tri) in tris:
            for v in tri:
                tot += 1
                if _looks_like_packed_normal(v[5], v[6], v[7]):
                    n += 1
        if n != tot:
            continue
        hit.append((name, tot))
        for i, (ti, mode, wrap, tri) in enumerate(tris):
            tris[i] = (ti, mode, wrap,
                       [v[:5] + (255, 255, 255, v[8]) for v in tri])
    # Meshes whose display list never issues G_GEOMETRYMODE G_SHADE|G_SHADING_SMOOTH
    # (0x00200004) inherit the scene's G_LIGHTING, so their vertex-colour slot holds
    # a packed unit normal.  dl_01051A0 (DRAGON/MISSILE/BOMBLET) has no 0xD9 command
    # at all; the four BARBWIRE lists issue 0xD9 only for G_CULL_BACK (0x400) and
    # never set G_SHADE -- same situation, same honest substitute (white, so the
    # authored texture shows through).  Verified by walking each list; the
    # SBAG/BRIK/CYCL/WOOD bodies DO set 0x00200004 and carry real vertex colours,
    # and are untouched here.
    EXPECT_LIT = {"dl_01051A0",                                # DRAGON/MISSILE/BOMBLET
                  "dl_0107F40", "dl_0108358",                  # BARB straight, L
                  "dl_0108200", "dl_0108090",                  # BARB T, cross
                  # THE NINE DAMAGE OVERLAYS that inherit G_LIGHTING. Walking each
                  # damage display list over its full call graph, exactly nine of the
                  # twenty issue NO G_GEOMETRYMODE (0xD9) command at all, so their colour
                  # slot holds a packed unit normal rather than a colour and they need
                  # the same honest white substitute the rocket gets. The other eleven do
                  # clr000000/set200004 and carry real colours.
                  # Only the first two were listed when this shipped, because the old
                  # axis-aligned signature could not see the other seven -- see the note
                  # in _looks_like_packed_normal. A damaged Construction Yard was bright
                  # magenta on screen for it.
                  "dl_0110110",                                # HOSP damage, 24/24
                  "dl_0119F58",                                # FIX  damage, 6/6
                  "dl_0142208",                                # EYE + HQ damage (shared)
                  "dl_0148668",                                # WEAP damage
                  "dl_0143658",                                # ATWR damage
                  "dl_00D33B0",                                # FACT damage
                  "dl_0118FE0",                                # PROC damage
                  "dl_0150F88",                                # HAND damage
                  "dl_0112C70",                                # MISS damage
                  # THE NUCLEAR STRIKE, all four slots baked into one mesh. 288/288
                  # vertices are packed unit normals: none of the four lists issues a
                  # G_GEOMETRYMODE command at all, so they inherit the scene's G_LIGHTING
                  # exactly the way dl_01051A0 (the rocket) does. Same situation, same
                  # honest white substitute. One name rather than four because the four
                  # display lists are concatenated into a single mesh before this runs.
                  "NUKEFX"}
    got = set(h[0] for h in hit)
    # EQUALITY, not a subset. The subset form catches a mesh that STARTS whitening but
    # is blind to one that STOPS -- and `set() <= EXPECT_LIT` is True, so a bake that
    # whitened nothing at all would pass in silence. That is the exact regression this
    # tripwire exists for: if a later ROM walk or signature change stops detecting
    # dl_01051A0, the missiles bake dark again (the original bug) and the BARB strands
    # bake near-black, with no error anywhere.
    assert got == EXPECT_LIT, (
        "flatten_lit_meshes: expected exactly %s to be all-packed-normal, got %s "
        "(missing %s, unexpected %s). The ROM walk changed, or the signature is "
        "catching a real colour, or a mesh stopped being detected."
        % (sorted(EXPECT_LIT), sorted(got),
           sorted(EXPECT_LIT - got), sorted(got - EXPECT_LIT)))
    if verbose:
        for name, tot in hit:
            print("lit-mesh flatten: %s (%d vertices, all packed normals) baked white"
                  % (name, tot))
    return hit


def dl_batch_tris(dl_rom):
    """[(ntris of batch0), (ntris of batch1), ...] for one display list, in DL
    order. A batch is one G_VTX load; consecutive loads with no triangles in
    between merge forward (they are one piece being staged in two loads)."""
    rom = _rom()
    resolve = SEG.make_resolver(dl_rom)
    counts = []
    cur = 0        # tris since the last batch boundary
    started = False
    o = dl_rom
    stack = []
    steps = 0
    while steps < 8000:
        steps += 1
        op = rom[o]
        if op == 0x01:                       # G_VTX: a new piece begins
            if started and cur > 0:
                counts.append(cur)
                cur = 0
            started = True
        elif op == 0x05:                     # G_TRI1
            cur += 1
        elif op == 0x06:                     # G_TRI2
            cur += 2
        elif op == 0xDE:                     # G_DL: call or branch
            ram = struct.unpack(">I", rom[o + 4:o + 8])[0]
            tgt = resolve(ram)
            if tgt is not None:
                if rom[o + 1] == 0:          # push: return here after
                    stack.append(o + 8)
                o = tgt
                continue
        elif op == 0xDF:                     # G_ENDDL
            if stack:
                o = stack.pop()
                continue
            break
        o += 8
    if cur > 0:
        counts.append(cur)
    return counts


def _graph_of(um_entry):
    """The scene graph for one unit_models.json entry.

    Two ways in, and both walks (mesh_sections and bake_mesh_parts) must agree:
      model_index -- a slot in the 236-entry model table at RAM 0x80099998;
      node_ram    -- a raw scene-graph node RAM address, for models that are NOT
                     in the model table at all.  The fourteen N64 CURSOR models
                     are the only such family: they hang off a private 16-byte
                     table at RAM 0x80099750 that a model-table walk cannot see
                     (objgraph2.cursor_node()).
    """
    if not B.SUBPARTS:
        return []
    node = um_entry.get("node_ram")
    if node is not None:
        return O2.parts_of_node(int(node, 16))
    slot = um_entry.get("model_index")
    return O2.parts(slot) if slot is not None else []


def mesh_sections(typecode, um_entry, expected_parts):
    """Section table for one mesh: ascending starting-triangle indices, one per
    G_VTX batch, across the same DLs in the same order bake_mesh_parts baked.
    expected_parts is that baker's part table [(tri0, ntris, role, pivot)]; the
    per-part totals are asserted so the walks cannot drift apart silently."""
    graph = _graph_of(um_entry)
    graph = [p for p in graph if p["gfx_rom"] is not None]
    dls = [p["gfx_rom"] for p in graph] if graph \
        else [int(um_entry["dl_rom"], 16)]

    starts = []
    tri0 = 0
    for i, dl in enumerate(dls):
        counts = dl_batch_tris(dl)
        got = sum(counts)
        want = expected_parts[i][1]
        assert got == want, ("%s part %d: section walk found %d tris, baker "
                             "baked %d" % (typecode, i, got, want))
        for c in counts:
            starts.append(tri0)
            tri0 += c
    assert tri0 == expected_parts[-1][0] + expected_parts[-1][1], typecode
    return starts

ROLE_STATIC, ROLE_TURRET, ROLE_ROTOR = 0, 1, 2

# The expected outcome of role derivation, from the survey (survey_parts.py):
# type -> (n_parts_with_gfx, turret part indices, rotor part indices).
# An assert failure here means the ROM walk or the derivation changed.
EXPECT = {
    "MTNK": (2, [1], []),   # beefed child, mount y 213 on hull top 171, barrel z 856
    "LTNK": (2, [1], []),   # beefed child, mount y 182 on hull top 146
    "HTNK": (2, [1], []),   # ScriptModels child, mount y 130 on hull top 118, twin barrels z 961
    "JEEP": (2, [1], []),   # gun mount, y 276 on hull top 253
    "BGGY": (2, [1], []),   # beefed gun mount, y 220 on hull top 212
    "BOAT": (2, [1], []),   # deck gun, y 350 amidships
    "GUN":  (2, [1], []),   # beefed barrel assembly, y 245 on base top 158
    "SAM":  (7, [],  []),   # launcher rest pose is RETRACTED into the pit (posed y<0);
                            # rotating hidden geometry would be pretend: static, documented
    "TRAN": (3, [], [1, 2]),  # two flat discs (y extent 0) at y 292 / 354
    "HELI": (2, [], [1]),     # one flat disc at y 214
}


def xform(v, M, t):
    """Row-vector: mesh = local @ M + t (objgraph2 convention)."""
    return (v[0] * M[0][0] + v[1] * M[1][0] + v[2] * M[2][0] + t[0],
            v[0] * M[0][1] + v[1] * M[1][1] + v[2] * M[2][1] + t[1],
            v[0] * M[0][2] + v[1] * M[1][2] + v[2] * M[2][2] + t[2])


# The three combiners an UNTEXTURED triangle carrying a PRIM colour is allowed to use.
# Decoded from the cartridge's own display lists; anything else stops the bake.
PRIM_SHADE = (0xFC327E64, 0xFFFFF7FB)   # rgb = SHADE * PRIM, a = PRIM_A  (the old rule)
PRIM_ONLY  = (0xFCFFFFFF, 0xFFFDF6FB)   # rgb = PRIM,         a = PRIM_A
PRIM_TEXA  = (0xFCFFFFFF, 0xFFFDF2F9)   # rgb = PRIM,         a = TEXEL0_A
TEX_PRIMA  = (0xFCFF97FF, 0xFF2CFE7F)   # rgb = TEXEL0,       a = TEXEL0_A * PRIM_A

# G_RM_AA_ZB_XLU_SURF: the cartridge's TRANSLUCENT blender, and the only thing in the
# data that says a face is see-through. Recomputed from the gbi.h macro rather than read
# off a table: AA_EN|Z_CMP|IM_RD|CLR_ON_CVG|CVG_DST_WRAP|ZMODE_XLU|FORCE_BL = 0x49D8, and
# both blender cycles are (CLR_IN * A_IN) + (CLR_MEM * 1MA) = 0x00500000. Note G_BL_1MA
# is 0, not 1; get that wrong and you compute 0x005549D8 and conclude the ROM word means
# something else. Z_UPD is CLEAR, which is why these faces must not write depth.
RM_XLU = 0x005049D8
_PRIM_ONLY_SEEN = {}

# The fourth triangle mode. bake_mesh only ever emits 0 opaque / 1 cutout / 2 shadow,
# because it picks from the texture format; 3 is ours to add and the renderer's TriMode
# enum grows to match. Kept as a bare literal on purpose: bake_mesh lives inside
# bake_pk4.pyc, bytecode whose source was lost, so there is no shared enum to import.
MODE_XLU = 3
_XLU_SEEN = {}
_XLU_WHITE_SEEN = {}


def bake_mesh_prim(off, bank):
    """B.bake_mesh, with the RDP PRIMITIVE colour folded back in.

    THE WHITE WHEELS. The cartridge's untextured combiner is FC327E64/FFFFF7FB, which
    decodes to colour = (PRIM - 0) * SHADE + 0 and alpha = PRIM_A. The PK4 baker keeps
    only SHADE and substitutes PRIM verbatim in the one case where SHADE is (0,0,0), so
    an untextured face whose SHADE is white bakes WHITE instead of PRIM. On the JEEP that
    is 32 of its 71 triangles: the four wheel-arch/fender boxes, drawn by the cartridge
    with G_SETPRIMCOLOR 0x1D1D1DFF, i.e. near-black rubber (29,29,29). White blocks where
    black rubber belongs is the reported "white circles around their wheels", and it hits the
    whole Nod line hardest (BGGY 8 tris, STNK 18, FTNK 74, BIKE 6, MSAM 2, LTNK 4), which
    is part of why Nod reads as miscoloured.

    SHADE is taken from B.walk's raw vertex colours rather than un-substituted out of
    bake_mesh's output, so there is no heuristic to get wrong. The two streams are
    index-aligned per triangle (asserted below).

    Textured faces are untouched: their combiner is FC127E24/FFFFF3F9 = TEXEL0 * SHADE,
    which is what the baker already does."""
    tris = B.bake_mesh(off, bank)
    verts, wt = B.walk(B.ROM, off, B.R.make_resolve(off))
    assert len(tris) == len(wt), (hex(off), len(tris), len(wt))
    # THE COMBINER COMES FROM A SECOND WALKER, and it has to. B.walk lives inside
    # bake_pk4.pyc -- bytecode whose source was lost and whose recorded
    # path no longer exists -- so it cannot be taught to
    # track G_SETCOMBINE. vx_rdp.walk can, and does (see `combine` there). It supplies
    # the combiner; B.walk keeps supplying PRIM, which vx_rdp does not track. Index
    # alignment between two independent interpreters is ASSERTED, never assumed: if they
    # ever disagree the fold must stop rather than colour triangles from the wrong list.
    _v2, wt2 = B.R.walk(B.ROM, off, B.R.make_resolve(off))
    cc_by_tri = None
    rm_by_tri = None
    if len(wt2) == len(wt):
        cc_by_tri = [t[3].get("combine") for t in wt2]
        rm_by_tri = [t[3].get("rendermode") for t in wt2]
    out = []
    prim_only_hits = []
    xlu_hits = 0
    white_hits = 0
    for i, (ti, mode, wrap, tri) in enumerate(tris):
        st = wt[i][3]
        prim = st.get("prim")
        cc0 = cc_by_tri[i] if cc_by_tri is not None else None
        rm0 = rm_by_tri[i] if rm_by_tri is not None else None

        # TRANSLUCENCY, WHICH THE PACK HAD NO WAY TO SAY UNTIL NOW.
        #
        # bake_mesh picks its mode from the TEXTURE FORMAT alone: I-format becomes
        # MODE_SHADOW, anything with a zero alpha texel becomes MODE_CUTOUT, everything
        # else MODE_OPAQUE. A texture format cannot tell a canopy from a solid panel, so
        # 69 faces the cartridge draws see-through have been drawing as solid: the Recon
        # Bike's and Jeep's windscreens, the Apache's canopy, the rocket launcher's, the
        # Weapons Factory's open bay, the sandbag and concrete wall shadows, and both
        # power plants' coolant pools, which is why one sees that water in the wrong
        # colour rather than as blue liquid.
        #
        # The vehicle shadows are excluded by the combiner test, not by luck: all
        # eighteen of them use this same blender, but with PRIM_TEXA, and they have their
        # own pass already.
        if rm0 == RM_XLU and cc0 != PRIM_TEXA:
            mode = MODE_XLU
            xlu_hits += 1
            if cc0 == TEX_PRIMA and prim is not None:
                # rgb = TEXEL0 with NO shade term, so the hardware never reads the vertex
                # colour slot -- and what sits in that slot is a packed unit normal,
                # (0,127,0) for the power plants' horizontal discs. GL_MODULATE DOES read
                # it, and multiplying a blue texture by dark green is exactly the colour
                # Reported. White is the identity for MODULATE, which is the only
                # honest way to say "this term does not exist" to fixed-function GL.
                # Alpha is PRIM_A, because the combiner's alpha IS TEXEL0_A * PRIM_A.
                pa2 = prim[3]
                tri = tuple((v[0], v[1], v[2], v[3], v[4], 255, 255, 255, pa2)
                            for v in tri)
                white_hits += 1
                out.append((ti, mode, wrap, tri))
                continue

        if ti >= 0 or prim is None:
            out.append((ti, mode, wrap, tri))
            continue
        pr, pg, pb, pa = prim
        cc = cc0
        nt = []
        for k in range(3):
            v = tri[k]
            sv = verts[wt[i][k]]            # raw SHADE, before any substitution
            sr, sg, sb = sv[5], sv[6], sv[7]
            if cc == PRIM_ONLY:
                # rgb = PRIM, with NO shade term. The list never asks the RDP for
                # shading, so the vertex colour slot is not a colour at all -- folding
                # SHADE in multiplies the PRIM by a packed unit normal and crushes it.
                # That is why the ATTACK cursor's four red arrowheads (228,22,22) baked
                # to (0,10,0), near-black: the reported "the attack cursor is all grey".
                cr, cg, cb = pr, pg, pb
            else:
                cr, cg, cb = (sr * pr) // 255, (sg * pg) // 255, (sb * pb) // 255
            nt.append((v[0], v[1], v[2], v[3], v[4], cr, cg, cb, pa))
        if cc == PRIM_ONLY:
            prim_only_hits.append(i)
        elif cc not in (None, PRIM_SHADE, PRIM_TEXA):
            # The point of raising: an unknown combiner on an untextured PRIM triangle
            # is exactly how the red went missing for a year -- silently folded by the
            # one rule that existed. A new one must stop the bake, not guess.
            raise SystemExit(
                "bake_mesh_prim: mesh 0x%X tri %d is untextured with PRIM %s under an "
                "UNHANDLED combiner %08X/%08X. Decide what it means and add an arm; do "
                "not let it fall through the SHADE fold." %
                (off, i, prim, cc[0], cc[1]))
        out.append((ti, mode, wrap, tuple(nt)))
    if prim_only_hits:
        _PRIM_ONLY_SEEN[off] = len(prim_only_hits)
    if xlu_hits:
        _XLU_SEEN[off] = xlu_hits
    if white_hits:
        _XLU_WHITE_SEEN[off] = white_hits
    return out


# EXACTLY seventeen triangles in the whole cartridge take the PRIM_ONLY arm, and they
# are checked for EQUALITY rather than as a lower bound. A subset check would pass on
# the day the fold silently stops finding them -- which is precisely the failure being
# fixed here, where one missing combiner arm quietly crushed the ATTACK cursor's red to
# near-black and nothing noticed. Keyed by mesh offset, so the count is per mesh and the
# double bake of the two house variants cannot inflate it.
PRIM_ONLY_EXPECT = {
    0x139D70: 1, 0x139E60: 1, 0x139F50: 1, 0x13A040: 1,   # CUR04 ATTACK, red arrowheads
    0x139268: 1, 0x139358: 1, 0x139448: 1, 0x139538: 1,   # CUR08, blue
    0x14D300: 2, 0x14D408: 2,                             # AFLD, red
    0x152738: 4,                                          # HELI
    0x1356F0: 1,                                          # MCVANIM deploy rig
    0x102DC0: 1,                                          # CURSOR_CIRCLES ground triangle
    # The transports' doors, model-table slots 203 and 204. Each is a three-triangle
    # child: one PRIM-only marker plus the two that make the leaf. The arms that draw
    # them are resident ROM 0x0014BD4 (APC, UnitType 5) and CodeOverlay ROM 0x0182358
    # (Chinook, AircraftType 0), and both issue their model as a SECOND draw command
    # beside the hull, exactly as the refinery's arm does.
    0x14A500: 1,                                          # APCDOOR
    0xD5FB0: 1,                                           # TRANRMP
    # THE ION BEAM's flat r=60 disc, one triangle, PRIM E5A6D7FF set at ROM 0x0103890.
    # G_TEXTURE is switched OFF for it at ROM 0x0103898 (D7000000, on-bit clear) and the
    # combiner at ROM 0x01038A8 is PRIM/PRIM_A, so it is untextured and its colour is the
    # prim verbatim -- the same arm the ATTACK cursor's arrowheads take. Worth naming
    # because E5A6D7 is a pale lilac that appears NOWHERE else in the beam: the three
    # shells are E2FDFF, 56B9FF and the inherited core, and an early reading of this
    # effect assigned E5A6D7 to the inner shell and mis-coloured 28 triangles with it.
    0x1035E0: 1,                                          # ION beam, the flat disc
}


# THE TRANSLUCENT FACES, COUNTED PER DISPLAY LIST AND CHECKED FOR EQUALITY.
#
# Sixty nine faces in seventeen display lists over sixteen model slots, and the count is
# keyed by display list rather than being a bare total, because a bare 69 would fire
# spuriously the day a pack legitimately omits a model and would say nothing about WHICH
# one moved. Equality, not a lower bound: this whole item exists because the mode was
# derived from the texture format, and the translucency was silently gone.
XLU_EXPECT = {
    0x0CF090: 4,                                          # FTNK, in ScriptModels
    0x105A48: 2, 0x105B90: 2,                             # NUK2 coolant discs
    0x10EE88: 6, 0x10EF90: 2, 0x10F0F8: 6, 0x10F280: 6,   # BRIK wall shadows
    0x114490: 2,                                          # NUKE coolant disc
    0x11ACD0: 4, 0x11AE28: 6, 0x11AFD0: 8, 0x11B118: 4,   # SBAG wall shadows
    0x147DC0: 3,                                          # WEAP, the open bay
    0x14AF68: 2,                                          # JEEP windscreen
    # MSAM is the ROCKET LAUNCHER, the vehicle a player calls the MRLS. Tiberian Dawn
    # SWAPS the two rocket idents (udata.cpp declares UnitMLRS with the code string
    # "MSAM"), so a search for MLRS examines the wrong vehicle and finds no mesh at all.
    # These two faces are its WINDSCREEN: the raked front panel of the cab, running from
    # the roof's front edge down to the nose. The cartridge draws them with combiner
    # FC327E64/FFFFF7FB, rgb = SHADE * PRIM and alpha = PRIM_A, which has NO TEXEL0 term,
    # over PRIM (49,79,139,135). Untextured translucent blue glass is the CORRECT result
    # here. It has already been reported once as a missing texture, and the tile bound at
    # that point still holds the I-format image from the two shadow faces just before it,
    # which is exactly what makes a lost texture the tempting reading. The combiner is
    # the authority, not the bound image.
    0x14CB50: 2,                                          # MSAM windscreen
    0x152FE0: 4,                                          # HELI canopy
    0x156CB0: 6,                                          # BIKE
    # THE ION CANNON's three shock rings. Render mode 0x005049D8 with combiner
    # TEXEL0 * SHADE, so unlike the beam's halos (which share the blender but carry
    # PRIM_TEXA and are handled in bake_ion_effect) these take the ordinary XLU arm and
    # keep the cartridge's own vertex tints -- white outer, blue inner, and the three
    # rings do not agree: ring 1 is (255,255,255)/(0,132,255) and rings 2 and 3 are
    # (250,253,255)/(0,144,255).
    0x104580: 28, 0x1049A0: 28, 0x104DC0: 28,             # ION shock rings 1..3
}
# The three that also get whitened, because their combiner is TEXEL0 with no SHADE term
# and the vertex slot holds a packed normal rather than a colour.
XLU_WHITE_EXPECT = {0x105A48: 2, 0x105B90: 2, 0x114490: 2}


def check_xlu():
    if _XLU_SEEN != XLU_EXPECT or _XLU_WHITE_SEEN != XLU_WHITE_EXPECT:
        missing = {hex(k): v for k, v in XLU_EXPECT.items() if _XLU_SEEN.get(k) != v}
        extra = {hex(k): v for k, v in _XLU_SEEN.items() if XLU_EXPECT.get(k) != v}
        wmiss = {hex(k): v for k, v in XLU_WHITE_EXPECT.items()
                 if _XLU_WHITE_SEEN.get(k) != v}
        wextra = {hex(k): v for k, v in _XLU_WHITE_SEEN.items()
                  if XLU_WHITE_EXPECT.get(k) != v}
        raise SystemExit(
            "bake5: the translucent face set moved.\n"
            "  expected but not found (or wrong count): %s\n"
            "  found but not expected:                  %s\n"
            "  whitened, expected/not found:            %s\n"
            "  whitened, unexpected:                    %s\n"
            "If a display list legitimately changed, update XLU_EXPECT in the same commit\n"
            "and say in the message which faces moved and why." % (missing, extra, wmiss, wextra))
    print("translucency: %d faces over %d display lists (%d of them whitened), "
          "the cartridge's own XLU blender" %
          (sum(_XLU_SEEN.values()), len(_XLU_SEEN), sum(_XLU_WHITE_SEEN.values())))
    _XLU_SEEN.clear()
    _XLU_WHITE_SEEN.clear()


def check_prim_only():
    # Cleared per bake, or the second scenario in a multi-pack run inherits the first's
    # tally and the equality check passes on stale data.
    if _PRIM_ONLY_SEEN != PRIM_ONLY_EXPECT:
        missing = {k: v for k, v in PRIM_ONLY_EXPECT.items() if _PRIM_ONLY_SEEN.get(k) != v}
        extra = {k: v for k, v in _PRIM_ONLY_SEEN.items() if PRIM_ONLY_EXPECT.get(k) != v}
        raise SystemExit(
            "bake5: the PRIM_ONLY set moved. missing/wrong=%s extra=%s\n"
            "These are the untextured triangles whose colour is PRIM with no\n"
            "SHADE term. If the ROM really changed, update PRIM_ONLY_EXPECT; if not, the\n"
            "combiner tracking regressed and the ATTACK cursor is about to go grey again."
            % ({hex(k): v for k, v in missing.items()},
               {hex(k): v for k, v in extra.items()}))
    print("prim-only combiner: %d triangles over %d meshes, exactly as expected"
          % (sum(_PRIM_ONLY_SEEN.values()), len(_PRIM_ONLY_SEEN)))
    _PRIM_ONLY_SEEN.clear()


# ---------------------------------------------------------------------------
# Node animation (PKB)
# ---------------------------------------------------------------------------
# The real keyframes come out of the cartridge's CAFEDEAD pose records (+0x40/+0x44/+0x48
# track pointers) and are resampled onto a fixed frame grid here, so the renderer only
# ever lerps between two baked poses. Until that extractor lands, CNC3D_FAKEANIM=<type>
# writes a SYNTHETIC clip for one mesh: every node yaws about the model origin, one full
# turn over 32 frames. It exists to prove the whole path -- baker, pack format, loader,
# renderer -- with something whose correct appearance is unmistakable, so that when the
# real data arrives a failure is known to be in the DATA and not in the plumbing.
# It is never written unless the environment variable is set.
import math as _math


def _identity_delta():
    return [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0]


def fake_anim(nparts, frames=32):
    """A synthetic yaw sweep, as a per-frame per-node DELTA against the rest pose."""
    mat, vis = [], []
    for fr in range(frames):
        a = 2.0 * _math.pi * fr / frames
        c, sn = _math.cos(a), _math.sin(a)
        for _ in range(nparts):
            mat.extend([c, 0.0, sn, 0.0,
                        0.0, 1.0, 0.0, 0.0,
                        -sn, 0.0, c, 0.0])
            vis.append(1)
    return dict(frames=frames, ticks_per_frame=1,
                clips=[dict(t0=0, t1=frames, loop=True)], mat=mat, vis=vis)

def bake_mesh_parts(typecode, um_entry, bank):
    """-> (tris, parts) where tris is the PK4 triangle soup (posed) and parts is
    [(tri0, ntris, role, pivot)]. Falls back to the bare dl_rom exactly like the
    PK4 baker when the model table gives no scene graph."""
    graph = _graph_of(um_entry)
    graph = [p for p in graph if p["gfx_rom"] is not None]
    if not graph:
        tris = bake_mesh_prim(int(um_entry["dl_rom"], 16), bank)
        return tris, [(0, len(tris), ROLE_STATIC, (0.0, 0.0, 0.0))]

    # role derivation ------------------------------------------------------
    roles = [ROLE_STATIC] * len(graph)
    pivots = [tuple(p["t"]) for p in graph]
    depth1 = [i for i, p in enumerate(graph) if p["depth"] == 1]
    if typecode in ENGINE_TURRETED and typecode != "SAM" and len(depth1) == 1:
        # the single depth-1 subtree is the turret assembly; every node under it
        # rotates about the subtree ROOT's pivot
        r = depth1[0]
        sub = [i for i in range(len(graph))
               if i == r or (i > r and graph[i]["depth"] > 1)]
        # (walk order: a deeper node after r belongs to r's subtree because r is
        #  the only depth-1 node)
        for i in sub:
            roles[i] = ROLE_TURRET
            pivots[i] = tuple(graph[r]["t"])
    if typecode in ROTOR_TYPES:
        for i, p in enumerate(graph):
            if p["depth"] >= 1 and p["kind"] in ("static", "beefed"):
                roles[i] = ROLE_ROTOR    # each disc spins about its own pivot
    # ----------------------------------------------------------------------

    tris = []
    parts = []
    for i, p in enumerate(graph):
        sub = bake_mesh_prim(p["gfx_rom"], bank)
        posed = []
        for (ti, mode, wrap, tri) in sub:
            nt = []
            for (x, y, z, u, v, r_, g_, b_, a_) in tri:
                px, py, pz = xform((x, y, z), p["M"], p["t"])
                nt.append((px, py, pz, u, v, r_, g_, b_, a_))
            posed.append((ti, mode, wrap, nt))
        parts.append((len(tris), len(posed), roles[i], pivots[i]))
        tris.extend(posed)
    return tris, parts


def check_expected(typecode, parts):
    if typecode not in EXPECT:
        return
    n, tur, rot = EXPECT[typecode]
    have_t = [i for i, p in enumerate(parts) if p[2] == ROLE_TURRET]
    have_r = [i for i, p in enumerate(parts) if p[2] == ROLE_ROTOR]
    assert len(parts) == n, "%s: %d parts, expected %d" % (typecode, len(parts), n)
    assert have_t == tur, "%s: turret parts %s, expected %s" % (typecode, have_t, tur)
    assert have_r == rot, "%s: rotor parts %s, expected %s" % (typecode, have_r, rot)


# ---------------------------------------------------------------------------
# Debris chunk meshes (the VehicleDamage / StructureDamage particle systems)
# ---------------------------------------------------------------------------
# The console draws a debris chunk as a solid, tumbling 3D polyhedron; we drew the
# cartridge's flat ember SPRITE at an invented 0.045 cells. The real chunks are 0.07 to
# 0.49 cells across, which is the reported "the particles that land on the ground are too
# small". Two tables of seven display lists each: VehicleDamage ROM 0xA2BA0,
# StructureDamage ROM 0xA2BBC.
#
# Geometry, colour folding and the four textures are decoded by the extractor,
# whose JSON is consumed verbatim. COLOUR IS ALREADY FOLDED there, per COMBINER rather
# than per "is it untextured":
#   FC327E64/FFFFF7FB -> rgb = shade * prim / 255, a = prim_a
#   FC00FFFF/FFFDF6FB -> rgb = prim verbatim, SHADE DISCARDED (VEH2/VEH3, whose vertex
#                        colours are packed unit normals)
# so nothing here may fold again, and flatten_lit_meshes must not run over them -- which
# is why this is called AFTER it.
#
# They reach the renderer as ordinary TYPE CODES (DBRV0..6, DBRS0..6), the same way the
# cursor models do, so the pack format does not change at all.
DEBRIS_JSON = os.path.join(HERE, "debris_chunks.json")


def bake_debris_chunks(bank, meshes, meshindex, meshparts, meshsections, verbose=True):
    """-> (extra type codes -> mesh name, [(bank index, gdi rgba)])."""
    if not os.path.isfile(DEBRIS_JSON):
        if verbose:
            print("debris: %s absent -- chunks stay sprites (a known gap)"
                  % os.path.basename(DEBRIS_JSON))
        return {}, []
    from PIL import Image        # same late import as the terrain atlas below
    doc = json.load(open(DEBRIS_JSON))
    texdir = os.path.dirname(DEBRIS_JSON)

    texmap, gdi_patch = {}, []
    for t in doc["textures"]:
        img = Image.open(os.path.join(texdir, t["png"])).convert("RGBA")
        assert img.size == (t["w"], t["h"]), t["png"]
        bi = bank.add_rgba(("debris", t["index"]), t["w"], t["h"], img.tobytes())
        texmap[t["index"]] = bi
        if t.get("png_gdi"):
            g = Image.open(os.path.join(texdir, t["png_gdi"])).convert("RGBA")
            assert g.size == img.size, t["png_gdi"]
            gdi_patch.append((bi, g.tobytes()))

    extra = {}
    for m in doc["meshes"]:
        code = ("DBRV%d" if m["family"] == "VEH" else "DBRS%d") % m["index"]
        tris = []
        for tr in m["tris"]:
            ti = texmap[tr["tex"]] if tr["tex"] >= 0 else -1
            vs = tuple((v["x"], v["y"], v["z"], v["u"], v["v"],
                        v["r"], v["g"], v["b"], v["a"]) for v in tr["v"])
            assert len(vs) == 3, code
            tris.append((ti, tr["mode"], tr["wrap"], vs))
        assert len(tris) == m["ntris"], (code, len(tris), m["ntris"])
        name = "debris_%s%d" % (m["family"], m["index"])
        meshindex[name] = len(meshes)
        meshparts[name] = [(0, len(tris), ROLE_STATIC, (0.0, 0.0, 0.0))]
        meshsections[name] = [0]
        meshes.append((name, tris))
        extra[code] = name
    assert len(extra) == 14, len(extra)
    if verbose:
        print("debris: 14 chunk meshes (%d triangles) and %d textures -- the console's "
              "solid tumbling debris, not the ember sprite"
              % (sum(m["ntris"] for m in doc["meshes"]), len(texmap)))
    return extra, gdi_patch


# ---------------------------------------------------------------------------
# Node animation (PKB): the cartridge's building idle clips
# ---------------------------------------------------------------------------
# The console animates a model by driving its SCENE-GRAPH NODES. Each node's CAFEDEAD
# pose record carries up to three keyframed tracks at +0x40 (translation), +0x44
# (rotation) and +0x48 (scale) -- the fields objgraph2.transform_of() has never read,
# which is the whole reason none of this has ever reached a pack.
#
# The rotation tracks are the part everybody missed. FEEB0002 keys store an ANGLE-AXIS at
# +0x1C and an IDENTITY quaternion at +0x2C, and the real quaternion is built at LOAD by
# the class's compile hook (cls+0x08 -> RAM 0x8008DF98), cumulatively across keys. Read
# the bytes without running that pass and all 36 rotation tracks in the cartridge look
# like "no rotation", which is exactly what they have always looked like here.
#
# The extractor owns all of that, plus the evaluators (cubic Bezier for FEEB0005/0007,
# lerp/slerp for 0009/000A/000B/000C) and the resampling. What it hands us per node per
# animation frame is C: the DELTA from that node's rest pose to its animated pose, in
# mesh space. That is the only shape the renderer can use, because bake_mesh_parts has
# already baked the REST pose into the vertices -- see the PKB note in cnc_eyes.cpp.
#
# C is row-vector (v' = v.M + t); the renderer applies v' = M.v + t, so the 3x3 is
# transposed on the way in.
ANIM_DIR = os.path.join(HERE, "anim")

# ---- one-shot rigs -------------------------------------------------------------------
# A clip that does not LOOP has to be told where in itself it is, so it can only be baked
# once something on our side drives it. Counted off the extractor's own loop test, 32 model
# slots are animated, 10 carry a looping clip and 22 are ONE-SHOT ONLY. (An older note said
# only slots 18 and 20 were one-shot. That is false; left open deliberately, where it is
# corrected rather than quietly dropped.) So this is not a list of what exists, it is a
# list of what has a DRIVER.
#
# Slot 18 is the MCV -> Construction Yard deploy rig, ANY_UNIT_MCVANIMZ1. Its driver lives
# in cnc_eyes.cpp (`mcvrig_for`) and reads the CONSTRUCTION YARD's own dostage/makecnt
# while its BState is BSTATE_CONSTRUCTION, because the brain exports those fields for
# RTTI_BUILDING only -- the MCV unit itself reports -1 for all three, measured. Its
# frame mapping is now the cartridge's own (frame = stage * 1.5625, RAM 0x80003D14).
#
# THE STRUCTURES BELOW were added, once the driver was decoded rather than
# guessed. Every structure's animation frame comes from a per-StructType CODE ARM in the
# 59-entry jump table at RAM 0x80003D18, reached from BuildingClass's 3-D draw virtual at
# RAM 0x8003DB94, and each arm is a pure function of the StageClass counter at
# building+0x28 -- the quantity our brain exports as `dostage`. The arm-by-arm decode is
# in docs/animation-drivers.md and the driver is `structure_anim_frame` in cnc_eyes.cpp.
#
# DELIBERATELY ABSENT, and each for a stated reason rather than an oversight:
#   PROC, SILO   their baked clips are IDENTITY on every frame, which contradicts their
#                own decoded arms (SILO has five tiberium fill poses; PROC emits two draw
#                commands). The extractor is under-reading them -- it never populates the
#                per-frame visibility the pack format already carries. Left open deliberately.
#   SAM          its arm reaches frame 400 against a clip we read as 100 frames. Same
#                suspicion, not yet closed.
#   GUN          its arm is driven by turret FACING, not by a clip time; the renderer
#                already turns that turret from engine state.
#
# WEAP was in that excluded list until recently, on the grounds that its arm reads a
# DOOR stage from RAM 0x801D3C70 which the brain does not export. That was the wrong
# reason to leave it BLACK: every structure here is driven off a free-running counter,
# so the missing door stage costs WEAP its exact trigger, not its animation, and it has
# three genuinely moving nodes. It was reported among the buildings with no idle
# animation, and that was right.
# V19 came off this list: once clip_split stopped cutting its animation
# into two fragments (see the pose-aware gap test in tools/anim/anim_extract.py) the oil
# pump's clip returns to its own start pose, so the extractor marks it LOOPING and it no
# longer needs a one-shot driver to say where in itself it is.
# PROCANIM is slot 20, the refinery's SECOND draw command, and it is a one-shot for the
# same reason MCVANIM is: it is an event animation with a driver, not an idle. Its driver
# is PROC's own arm, which walks it with the engine's refinery stage (see the PROC section
# of structure_anim_frame in cnc_eyes.cpp).
ONESHOT_CODES = {"MCVANIM", "PROCANIM", "CURCIRC", "FACT", "NUKE", "NUK2",
                 "AFLD", "HAND", "ATWR", "WEAP"}


def _anim_index():
    ix = os.path.join(ANIM_DIR, "index.json")
    return json.load(open(ix)) if os.path.isfile(ix) else None


def bake_node_anim(code, um_entry, parts, verbose=True):
    """-> the PKB block for this mesh, or None if the slot carries no clip."""
    ix = _anim_index()
    if ix is None:
        return None
    slot = um_entry.get("model_index")
    if slot is None:
        return None
    path = os.path.join(ANIM_DIR, "slot_%03d.json" % int(slot))
    if not os.path.isfile(path):
        return None
    doc = json.load(open(path))
    clips = doc.get("clips") or []
    if not clips:
        return None
    # LOOPING clips are the easy case: an idle animation runs forever off the engine tick
    # and needs no driver at all. A ONE-SHOT clip does need one -- something in the world
    # has to say where in the clip we are -- so a one-shot is baked only for the codes in
    # ONESHOT_CODES, whose driver is written and named there. Baking the others as if they
    # looped would animate a building that should be still, which is why they are skipped
    # rather than quietly made to loop; they stay recorded as a known gap.
    loops = [c for c in clips if c.get("loop")]
    oneshot = code in ONESHOT_CODES
    if not loops and not oneshot:
        return None
    if oneshot:
        # the rig's own clip: the longest non-looping range the extractor segmented
        cands = [c for c in clips if not c.get("loop")] or clips
        lo = max(cands, key=lambda c: c["t1"] - c["t0"])
    else:
        lo = loops[0]

    nparts = len(parts)
    nodes = [n for n in doc["nodes"] if n.get("bake_part_index") is not None]
    if not nodes:
        return None
    nframes = max(len(n["frames"]) for n in nodes if n.get("frames")) if nodes else 0
    if nframes <= 0:
        return None

    # THE ASSERT THAT MAKES THE DELTA MEAN ANYTHING. C is rest^-1 . anim, and it is only
    # correct if `rest` is the transform bake5 actually baked into these vertices. The
    # two structures are computed by different code from different reads, so agreement is
    # real evidence rather than a tautology.
    graph = [q for q in _graph_of(um_entry) if q.get("gfx_rom") is not None]
    for n in nodes:
        bi = n["bake_part_index"]
        assert 0 <= bi < nparts, (code, bi, nparts)
        q = graph[bi]
        rm = n.get("rest_world") or n.get("rest_matrix")
        if isinstance(rm, dict) and "M" in rm:
            for r in range(3):
                for c2 in range(3):
                    assert abs(rm["M"][r][c2] - q["M"][r][c2]) < 1e-3, \
                        ("%s: node %d rest 3x3 disagrees with the baked pose" % (code, bi))
            for k in range(3):
                assert abs(rm["t"][k] - q["t"][k]) < 1e-2, \
                    ("%s: node %d rest translation disagrees with the baked pose"
                     % (code, bi))

    # TURRET AND ROTOR PARTS ARE NOT ANIMATED FROM A CLIP, and this is a decision, not an
    # oversight. Several slots carry looping tracks on exactly the nodes our renderer
    # already drives from ENGINE STATE: HTNK/JEEP/BOAT's turrets and TRAN/HELI's rotors.
    # A canned sweep would fight the aim -- a turret must point at what it is shooting,
    # not follow a timer -- so those nodes keep the engine's answer and only STATIC parts
    # take the cartridge's clip.
    nodes = [n for n in nodes
             if parts[n["bake_part_index"]][2] == ROLE_STATIC]
    if not nodes:
        return None
    # And a clip whose every static node sits at identity for its whole length is a
    # no-op: PROC and SILO both have one. Baking it would spend space and change nothing.
    moved = False
    for n in nodes:
        for fr in (n.get("frames") or []):
            C = fr.get("C")
            if not C:
                continue
            if (max(abs(v) for v in C[3]) > 1e-4
                or max(abs(C[r][c] - (1.0 if r == c else 0.0))
                       for r in range(3) for c in range(3)) > 1e-4):
                moved = True
                break
        if moved:
            break
    if not moved:
        return None

    mat = [0.0] * (nframes * nparts * 12)
    vis = [0] * (nframes * nparts)
    # every part starts at identity and visible, so a part with no track simply holds
    for f in range(nframes):
        for pi in range(nparts):
            o = (f * nparts + pi) * 12
            mat[o + 0] = mat[o + 5] = mat[o + 10] = 1.0
            vis[f * nparts + pi] = 1
    for n in nodes:
        pi = n["bake_part_index"]
        frames = n.get("frames") or []
        for f in range(nframes):
            fr = frames[f] if f < len(frames) else (frames[-1] if frames else None)
            if fr is None:
                continue
            C = fr.get("C")
            if not C:
                continue
            o = (f * nparts + pi) * 12
            # transpose the 3x3, translation into the fourth column
            for r in range(3):
                for c2 in range(3):
                    mat[o + r * 4 + c2] = float(C[c2][r])
                mat[o + r * 4 + 3] = float(C[3][r])
    tpf = int(doc.get("frame_ticks") or ix.get("ticks_per_frame") or 160)
    out = dict(frames=nframes, ticks_per_frame=1,
               clips=[dict(t0=0, t1=nframes, loop=not oneshot)],
               mat=mat, vis=vis)
    if verbose:
        print("anim: %-7s slot %-3s %d nodes animated over %d frames (%s %s..%s ticks, "
              "%d ticks/frame on the cartridge)"
              % (code, slot, len(nodes), nframes,
                 "ONE-SHOT" if oneshot else "loop",
                 lo.get("t0"), lo.get("t1"), tpf))
    return out


# ---------------------------------------------------------------------------
# Cursor animation (PKB, and two per-frame texture sets)
# ---------------------------------------------------------------------------
# Ten of the twenty console cursor states carry a frame count of 100 and every one of
# them used to draw STATIC. The decode lives in cursoranim.py (run it for the
# verification table); this is only the bake.
#
# TRANSLATION / ROTATION CURSORS (codes 0x03, 0x04, 0x06, 0x08, 0x09) reuse the PKB node
# animation section unchanged. That works because a cursor mesh's parts ARE its
# scene-graph nodes: bake_mesh_parts walks the cursor node graph through the same
# _graph_of() branch it uses for everything else and emits one part per node with a
# display list, so a per-node delta lands on exactly the right triangles. The mapping is
# made by NODE ADDRESS rather than by walk position, and asserted, so the two walks
# cannot drift apart silently.
#
# THE FLIPBOOK CURSORS (0x0A, 0x0B) cannot use that section: what changes per frame is the
# G_SETTIMG image address, not a transform. The cheapest faithful representation is a
# per-frame MESH VARIANT carried as an ordinary extra TYPE CODE -- CUR0AF0..CUR0AF3 and
# CUR0BF0..CUR0BF2 -- exactly the way the debris chunks ride the type table. It needs no
# pack format change at all and it costs 4 x 124 and 3 x 22 triangles. The alternative (a
# per-mesh texture-index table in the pack) would have bought about 8 KB and cost a format
# bump. The images are added to the GDI bank too, so the PK6 diff pass sees every texture
# `bank` holds; the cursor itself always draws through the NEUTRAL table (c3d_draw_one
# passes house 0, which is what the console does for a cursor), so those variants are
# carried for consistency rather than used.
import cursoranim as CA


def is_cursor_code(code):
    """A CURSOR code is exactly "CUR" plus two hex digits -- CUR00..CUR0D and the
    flipbook variants' own prefixes. It is NOT "anything beginning with CUR": the
    cartridge also names model slot 107 CURSOR_CIRCLES, and the loose test sent that
    straight into the cursor baker, where int("SOR_CIRCLES", 16) does what you would
    expect. Tightened rather than renaming the model, because the name is the
    cartridge's own (name-pointer array at ROM 0x1DE924, entry 97)."""
    return (len(code) == 5 and code.startswith("CUR")
            and all(c in "0123456789ABCDEFabcdef" for c in code[3:]))


def bake_cursor_anim(code, um_entry, parts, verbose=True):
    """-> the PKB block for one animated cursor mesh, or None."""
    if not is_cursor_code(code):
        return None
    ccode = int(code[3:], 16)
    if ccode not in CA.animated_codes():
        return None
    graph = [q for q in _graph_of(um_entry) if q.get("gfx_rom") is not None]
    assert len(graph) == len(parts), (code, len(graph), len(parts))
    bypart = {q["node"]: i for i, q in enumerate(graph)}
    order, deltas = CA.node_deltas(ccode)
    nparts = len(parts)
    nframes = CA.FRAME_COUNT
    mat = [0.0] * (nframes * nparts * 12)
    vis = [1] * (nframes * nparts)
    for f in range(nframes):
        for pi in range(nparts):
            o = (f * nparts + pi) * 12
            mat[o + 0] = mat[o + 5] = mat[o + 10] = 1.0
    moved = 0
    for ram in order:
        pi = bypart.get(ram)
        assert pi is not None, \
            "%s: animated node 0x%08X is not one of the baked parts" % (code, ram)
        fr = deltas[ram]
        assert len(fr) == nframes, (code, len(fr))
        for f in range(nframes):
            C = fr[f]
            o = (f * nparts + pi) * 12
            # transpose the 3x3 (the baker is row-vector, the renderer is column-vector)
            # and put the translation in the fourth column
            for r in range(3):
                for c in range(3):
                    mat[o + r * 4 + c] = float(C[c][r])
                mat[o + r * 4 + 3] = float(C[3][r])
        span = max(max(abs(x) for x in C[3]) for C in fr)
        if span > 1.0:
            moved += 1
    assert moved > 0, "%s: every node's delta is stationary over the whole cycle" % code
    if verbose:
        print("anim: %-6s cursor code 0x%02X, %d of %d nodes move over %d frames "
              "(clock: frame = (frameCounter*4) mod 100)"
              % (code, ccode, moved, nparts, nframes))
    return dict(frames=nframes, ticks_per_frame=1,
                clips=[dict(t0=0, t1=nframes, loop=True)], mat=mat, vis=vis)


def bake_cursor_flipbooks(um, bank, meshes, meshindex, meshparts, meshsections,
                          verbose=True):
    """-> {type code: mesh name} for the per-frame variants of cursors 0x0A and 0x0B.

    Called once per BANK, so the GDI re-walk is OFFERED the same textures under the same
    keys and the PK6 diff pass cannot silently skip them. Measured outcome today: none of
    the seven images decodes differently through the two house TLUTs, so no variant is
    stored, and the cursor would not use one anyway (c3d_draw_one passes house 0). The
    call is here so the "same walk, same keys" invariant the diff rests on stays TRUE,
    not because a variant is expected."""
    extra = {}
    for ccode in CA.flipbook_codes():
        tc = "CUR%02X" % ccode
        assert tc in um, "%s is not in unit_models.json" % tc
        fb = CA.flipbook(ccode)
        dl = int(um[tc]["dl_rom"], 16)
        resolve = B.R.make_resolve(dl)
        _verts, wt = B.walk(B.ROM, dl, resolve)
        base_state = None
        for w in wt:
            if w[3].get("timg") == fb["images"][0]:
                base_state = dict(w[3])
                break
        assert base_state is not None, \
            "%s: no triangle draws the flipbook's own image 0x%08X" % (tc, fb["images"][0])
        idx = []
        for img in fb["images"]:
            st = dict(base_state)
            st["timg"] = img
            key, tw, th, px, _rest = B.decode_tex(st, resolve)
            idx.append(bank.add_rgba(key, tw, th, px))
        assert len(set(idx)) == len(idx), \
            "%s: two flipbook images collapsed onto one bank entry" % tc
        tris, parts = bake_mesh_parts(tc, um[tc], bank)
        base_bi = idx[0]
        nswap = sum(1 for t in tris if t[0] == base_bi)
        assert nswap > 0, \
            "%s: the baked mesh draws nothing with the flipbook's base texture" % tc
        for k, bi in enumerate(idx):
            name = "%s_f%d" % (um[tc]["mesh"], k)
            if name in meshindex:
                continue
            vt = [((bi if ti == base_bi else ti), mode, wrap, tri)
                  for (ti, mode, wrap, tri) in tris]
            meshindex[name] = len(meshes)
            meshparts[name] = list(parts)
            meshsections[name] = list(meshsections.get(um[tc]["mesh"])
                                      or mesh_sections(tc, um[tc], parts))
            meshes.append((name, vt))
            extra["%sF%d" % (tc, k)] = name
        if verbose:
            print("cursor flipbook: %s -> %d frames of a %dx%d image (%d triangles "
                  "reskinned each, period %d ticks)"
                  % (tc, len(idx), fb["width"],
                     (fb["images"][1] - fb["images"][0]) //
                     max(1, fb["width"] * {0: 4, 1: 8, 2: 16, 3: 32}[fb["siz"]] // 8),
                     nswap, fb["period"]))
    return extra


# ---------------------------------------------------------------------------
# THE NUCLEAR STRIKE (PK4 mesh + PKB clip)
# ---------------------------------------------------------------------------
# Four model-table slots, not one: 6 dome, 7 stem, 8 collar, 9 ground cloud. The
# cartridge draws them as four separate script models that happen to share a clock, and
# together they are the mushroom. They are baked here as ONE mesh with FOUR PARTS so a
# single PKB clip drives the whole thing, which is what the pack format already knows how
# to do -- no format change, and the renderer needs one mesh id rather than four.
#
# WHERE THE MOTION LIVES, and it is the one thing about this effect that is easy to get
# wrong. slot_006.json's per-frame T / Q / S are FLAT: identity on all 31 frames, with
# `animated: false` and `tracks: {}`. Read those fields and you conclude the dome is
# static and bake a mushroom whose cap never rises. The motion is in the composed **C**
# matrix, which is what bake_node_anim has always read: C[3] is the translation and the
# 3x3 carries the swell, and over 31 frames the dome's uniform scale runs 1.000 -> 4.676
# with 136 degrees of yaw. Measured, not inferred.
#
# THE 0.3. MODEL_SCALE at ROM 0x09A570 is [1.0, then 0.3 nine times, then zeros], read
# back in this file's own verification, so every effect slot draws at 0.3. Because
#     (v . M + t) * 0.3  ==  v . (M * 0.3) + (t * 0.3)
# the factor folds into the CLIP alone -- all twelve floats of every frame -- and neither
# the vertices nor draw_mesh need to know about it. draw_mesh has no scale parameter and
# MODEL_SCALE in the renderer is a file-scope constant, so the alternative was a
# signature change for a number that is already per-slot data.
#
# THE COLLAR STARTS LATE. Slot 8's clip is t 1280..4800 against everyone else's 0..4800,
# i.e. it does not exist until frame 8 (1280 / 160). Its 23 frames are placed at global
# 8..30 and its animVis is 0 before that. This is the first time anything in this bake
# has written a ZERO into animVis: every existing producer writes all-ones, so
# cnc_eyes.cpp:5764's per-frame visibility test is live but never-exercised code. The
# frames before the collar appears are given its frame-8 matrix rather than identity,
# because cnc_eyes.cpp:5767 lerps af0 -> af1 as soon as af0's vis byte is set and an
# identity there would lerp the collar in from the wrong pose.
NUKE_SLOTS = [(6, 0x0100888, "dome"), (7, 0x0100E68, "stem"),
              (8, 0x01011F0, "collar"), (9, 0x0101E88, "cloud")]
NUKE_MESH = "NUKEFX"
NUKE_FRAMES = 31
# the project owner ASKED FOR 2.5x, 25 Aug 2026: at the cartridge's own 0.3 the mushroom is about a
# tile and a quarter across, which is faithful and reads as small on a modern screen at
# this camera distance. 0.3 * 2.5 = 0.75. The cartridge's number is kept in the comment
# rather than deleted, because it is the thing to return to if faithfulness ever wins.
NUKE_MODEL_SCALE = 0.75            # MODEL_SCALE[6..9] is 0.3 in the ROM; x2.5 by request


def bake_nuke_effect(bank, meshes, meshindex, meshparts, meshsections, meshanim,
                     verbose=True):
    """-> {typecode: meshname} for the nuclear strike, or {} if the anim data is absent."""
    docs = {}
    for slot, _dl, _nm in NUKE_SLOTS:
        path = os.path.join(ANIM_DIR, "slot_%03d.json" % slot)
        if not os.path.isfile(path):
            return {}
        docs[slot] = json.load(open(path))

    tris, parts, mats, viss = [], [], [], []
    for slot, dl, name in NUKE_SLOTS:
        node = docs[slot]["nodes"][0]
        assert int(node["gfx_rom"], 16) == dl, (
            "nuke slot %d: anim JSON names display list %s but this bake expects 0x%07X"
            % (slot, node["gfx_rom"], dl))
        rw = node["rest_world"]
        M, t = rw["M"], rw["t"]
        sub = bake_mesh_prim(dl, bank)
        posed = []
        for (ti, mode, wrap, tri) in sub:
            nt = [(lambda p: (p[0], p[1], p[2], v[3], v[4], v[5], v[6], v[7], v[8]))(
                      xform((v[0], v[1], v[2]), M, t))
                  for v in tri]
            posed.append((ti, mode, wrap, nt))
        parts.append((len(tris), len(posed), ROLE_STATIC, tuple(t)))
        tris.extend(posed)

        frames = node["frames"]
        # global frame index of this part's first key: 0 for everyone but the collar
        f0 = int(round((docs[slot]["t0"] - 1) / float(docs[slot]["frame_ticks"]))) \
            if docs[slot]["t0"] > 1 else 0
        assert f0 + len(frames) == NUKE_FRAMES, (
            "nuke slot %d: %d frames starting at global %d does not fill %d"
            % (slot, len(frames), f0, NUKE_FRAMES))
        pm, pv = [], []
        for g in range(NUKE_FRAMES):
            fr = frames[max(0, g - f0)]        # hold the first key before it appears
            C = fr["C"]
            m12 = [0.0] * 12
            for r in range(3):
                for c in range(3):
                    m12[r * 4 + c] = float(C[c][r]) * NUKE_MODEL_SCALE
                m12[r * 4 + 3] = float(C[3][r]) * NUKE_MODEL_SCALE
            pm.append(m12)
            pv.append(0 if g < f0 else 1)
        mats.append(pm)
        viss.append(pv)

    nparts = len(parts)
    mat = [0.0] * (NUKE_FRAMES * nparts * 12)
    vis = [0] * (NUKE_FRAMES * nparts)
    for pi in range(nparts):
        for f in range(NUKE_FRAMES):
            o = (f * nparts + pi) * 12
            mat[o:o + 12] = mats[pi][f]
            vis[f * nparts + pi] = viss[pi][f]

    meshindex[NUKE_MESH] = len(meshes)
    meshparts[NUKE_MESH] = parts
    meshsections[NUKE_MESH] = []          # not a building: no construction sections
    meshes.append((NUKE_MESH, tris))
    meshanim[NUKE_MESH] = dict(frames=NUKE_FRAMES, ticks_per_frame=1,
                               clips=[dict(t0=0, t1=NUKE_FRAMES, loop=False)],
                               mat=mat, vis=vis)

    # EQUALITY tripwires, the house rule: a subset check is satisfied by an empty bake.
    assert len(parts) == 4, "nuke: expected 4 parts, got %d" % len(parts)
    assert len(tris) == 336, (
        "nuke: expected 336 triangles (96 dome + 20 stem + 60 collar + 160 cloud), got %d"
        % len(tris))
    nhidden = sum(1 for v in vis if v == 0)
    assert nhidden == 8, (
        "nuke: expected exactly 8 hidden part-frames (the collar before frame 8), got %d"
        % nhidden)
    if verbose:
        print("nuke: %s, %d triangles in 4 parts over %d frames at MODEL_SCALE %.2f -- "
              "dome swells %.3fx, collar hidden for its first %d frames"
              % (NUKE_MESH, len(tris), NUKE_FRAMES, NUKE_MODEL_SCALE,
                 mats[0][-1][5] / NUKE_MODEL_SCALE, nhidden))
    return {NUKE_MESH: NUKE_MESH}


# ---------------------------------------------------------------------------
# THE ION CANNON (PK4 snapshot meshes)
# ---------------------------------------------------------------------------
# Five model-table slots: 1 the beam, 2 an undrawn ring, 3/4/5 the three shock rings.
# The whole event is over by clip tick 1600 -- a thin bright column drops out of the sky
# while three blue-white rings fire top down 320 ticks apart at descending heights and
# expand and dissolve.
#
# WHY SNAPSHOTS RATHER THAN ONE ANIMATED MESH. Two things change per frame here, not one:
# the transform AND the texture. The pack can express a per-frame transform (PKB) or a
# per-frame texture (a mesh variant per image, which is how the cursor flipbooks and the
# structure texture books already ride the type table), but expressing BOTH at once for
# four parts with four different schedules would mean 20 physically distinct parts and a
# 1144-triangle part table, because cnc_eyes.cpp:1337 requires a pack's parts to tile its
# triangle list exactly. The engine's own granularity makes that pointless: ANIM_ION_CANNON
# declares 15 stages and the arm doubles the stage, so the renderer can only ever be shown
# a discrete state anyway. So each 160-tick clip slot is baked as ONE finished mesh with
# the motion and the texture already applied, and the renderer picks one by stage. No new
# pack format, no animVis, and 1850 triangles instead of 1144 plus a clip.
#
# THE REST POSE LOOKS LIKE A BUG AND IS NOT. The rings' rest scale is 1.4e-4 (slot 5's is
# 2.5e-3), so a ring posed at rest alone is a POINT, and the per-frame C matrices run to
# ~11000 to compensate -- C is rest^-1 . anim, so its scale is anim/rest. Compose the two
# and the ring is its real size; bake either one alone and you get either a dot or
# something eleven thousand times too big. Both readings look plausible in isolation,
# which is why this note exists.
#
# EVERY SCHEDULE BELOW IS THE CARTRIDGE'S OWN, read back through texbook.py rather than
# tabulated here: the beam's 8-image book (period 1280, one image per 160 ticks) and the
# rings' shared 4-image book on per-slot times -- slot 3 [0,800,1280,1760,17760], slot 4
# [0,1120,1600,2080,18080], slot 5 [0,1440,1920,2400,18400]. Each ring therefore holds
# image 0 for 480 ticks after its own clip start and then advances every 480.
#
# THE BEAM CARRIES TWO BOOKS, which is the thing about this effect most easily got wrong.
# Its seven splits are not seven copies of one book: records 0..5 all match 0x8013BDF0
# (an I8 glow, 72 triangles, the two translucent halos) and record 6 matches 0x80137DF0
# (an RGBA16 core, 28 triangles, the opaque inner shell). Reading only the first record --
# which is what texbook did until this change -- returns the glow and silently drops the
# core, and the beam then bakes with the halo texture on its centre. Measured split by
# split, and the 28 / 1 / 72 triangle split is confirmed straight out of bake_mesh_prim.
ION_TICK = 160
# 0..2400 in 160-tick slots. The transforms are all finished by 1600, but the RINGS' own
# texture book is not: each ring holds image 0 for 480 ticks after its clip starts and then
# advances every 480, and its last two images ARE the dissolve (measured alpha means over
# the four: 99.7, 52.2, 13.3, 0.3). A grid that stopped at the transforms stopped at 1440,
# so images 2 and 3 were decoded, banked and written into every pack completely
# UNREFERENCED, and every ring popped out of existence at half opacity. Ring 3 reaches its
# empty image at t=2400, and the slot BEFORE that -- t=2240, showing image 2 -- is the last
# one that paints anything, so the grid is 0..14. A sixteenth slot lands exactly on the
# empty image and was measured carrying no geometry at all.
# ION_SNAPSHOTS in cnc_eyes.cpp must match; the renderer's clamp is derived from it.
ION_NSLOT = 15
ION_MODEL_SCALE = 0.75             # ROM 0x09A570 says 0.3; x2.5 by request, see NUKE_MODEL_SCALE
# slot, display list, clip t0, clip t1
ION_PARTS = [(1, 0x01035E0,    0, 1120, "beam"),
             (3, 0x0104580,  320,  960, "ring1"),
             (4, 0x01049A0,  640, 1280, "ring2"),
             (5, 0x0104DC0,  960, 1600, "ring3")]


def _ion_book_image(times, t):
    """-> the image index a book shows at absolute clip time t."""
    for i in range(len(times) - 1):
        if times[i] <= t < times[i + 1]:
            return i
    return len(times) - 2


def bake_ion_effect(bank, meshes, meshindex, meshparts, meshsections, verbose=True):
    """-> {typecode: meshname} for the ion cannon, or {} if the anim data is absent."""
    import objgraph2 as _og
    docs = {}
    for slot, _dl, _t0, _t1, _nm in ION_PARTS:
        path = os.path.join(ANIM_DIR, "slot_%03d.json" % slot)
        if not os.path.isfile(path):
            return {}
        docs[slot] = json.load(open(path))

    # ---- the texture books, and one bank entry per image -----------------------------
    beam_book = TEXBOOK.texbook(B.ROM, _og.node_ptr(1))
    assert beam_book and len(beam_book["distinct_books"]) == 2, (
        "ion beam: expected TWO texture books (the I8 glow and the RGBA16 core), got %s. "
        "Reading only split 0 gives one and silently drops the core."
        % (beam_book and beam_book["distinct_books"]))
    glow_ram, core_ram = 0x8013BDF0, 0x80137DF0
    by_match = {}
    for sp in beam_book["splits"]:
        by_match.setdefault(sp["match"], sp)
    assert glow_ram in by_match and core_ram in by_match, (
        "ion beam: the two books are not the measured pair %08X / %08X but %s"
        % (glow_ram, core_ram, [hex(k) for k in by_match]))

    # The base render state for each site, so a substituted image decodes the same way the
    # authored one does. Taken from the beam's own walk rather than invented.
    dl_beam = 0x01035E0
    resolve = B.R.make_resolve(dl_beam)
    _v, wt = B.walk(B.ROM, dl_beam, resolve)
    state_for = {}
    for w in wt:
        timg = w[3].get("timg")
        if timg in (glow_ram, core_ram) and timg not in state_for:
            state_for[timg] = dict(w[3])
    assert len(state_for) == 2, (
        "ion beam: its display list draws with %d of the two book images, not both"
        % len(state_for))

    def book_bank(match_ram):
        out = []
        for img in by_match[match_ram]["images"]:
            st = dict(state_for[match_ram])
            st["timg"] = img
            key, tw, th, px, _rest = B.decode_tex(st, resolve)
            out.append(bank.add_rgba(key, tw, th, px))
        return out
    glow_idx, core_idx = book_bank(glow_ram), book_bank(core_ram)
    assert len(glow_idx) == 8 and len(core_idx) == 8, (
        "ion beam: expected 8 images per book, got %d / %d" % (len(glow_idx), len(core_idx)))

    # the rings all share ONE book; decode it off ring1's own state
    ring_books = {}
    ring_idx = None
    for slot, dl, _t0, _t1, _nm in ION_PARTS[1:]:
        bk = TEXBOOK.texbook(B.ROM, _og.node_ptr(slot))
        assert bk and len(bk["distinct_books"]) == 1, \
            "ion ring slot %d: expected one texture book, got %s" % (slot, bk)
        ring_books[slot] = bk
        if ring_idx is None:
            rres = B.R.make_resolve(dl)
            _rv, rwt = B.walk(B.ROM, dl, rres)
            rst = None
            for w in rwt:
                if w[3].get("timg") == bk["match"]:
                    rst = dict(w[3]); break
            assert rst is not None, "ion ring slot %d draws none of its book's images" % slot
            ring_idx = []
            for img in bk["images"]:
                st = dict(rst); st["timg"] = img
                key, tw, th, px, _rest = B.decode_tex(st, rres)
                ring_idx.append(bank.add_rgba(key, tw, th, px))
    assert len(ring_idx) == 4, "ion rings: expected a 4-image book, got %d" % len(ring_idx)

    # HOW LONG EACH PIECE KEEPS BEING DRAWN, taken from the cartridge's own schedule
    # rather than tabulated here.
    #
    # A RING outlives its transform clip. Its transform freezes at its last key (the `fi`
    # clamp below) while its texture book goes on advancing, and the last two images are
    # the dissolve. It stops being drawn when the book reaches its FINAL image, which is
    # the long empty hold -- times[count-1], i.e. 1760 / 2080 / 2400 for the three rings.
    # Drawing the empty image would cost triangles to paint nothing.
    #
    # THE BEAM DOES NOT, and this one is a decision rather than a measurement, so it is
    # said out loud. Its book has period 1280 and LOOPS, so a beam left running past its
    # clip would pulse through its eight images forever; the spec's own summary is that
    # the column "drops out of the sky" and that the event is over by 1600. Bounded at its
    # clip end. If the console really does hold a planted column there, this is the line to
    # revisit -- registered in known-gap notes rather than left as a silent choice.
    # The beam's bound is ONE SLOT PAST its clip end, because 1120 is the last frame it
    # actually shows (its 8-image book at 160 a frame spans t=0..1120) rather than the
    # point where it stops. The rings' bound is the opposite kind of number: times[-2] is
    # where their EMPTY image begins, so it is excluded. Getting these two the same way
    # round drops the beam's last frame, which is what a first cut of this did.
    hold_to = {1: 1120 + ION_TICK}
    for slot, _dl, _t0, _t1, _nm in ION_PARTS[1:]:
        tms = ring_books[slot]["times"]
        hold_to[slot] = tms[len(tms) - 2]
    assert sorted(hold_to.values()) == [1280, 1760, 2080, 2400], (
        "ion: the hold windows moved, got %s -- the ring books' schedules changed"
        % sorted(hold_to.values()))
    assert (ION_NSLOT - 1) * ION_TICK >= max(hold_to.values()) - ION_TICK, (
        "ion: ION_NSLOT=%d covers only t=%d but a ring is still dissolving at %d"
        % (ION_NSLOT, (ION_NSLOT - 1) * ION_TICK, max(hold_to.values())))

    # ---- the geometry, baked once per part and re-skinned per snapshot ---------------
    base = {}
    for slot, dl, _t0, _t1, name in ION_PARTS:
        tris = bake_mesh_prim(dl, bank)
        base[slot] = tris
    # ---- THE BEAM'S THREE GROUPS, each of which wants something different -------------
    # Measured off its own walk rather than assumed, because the three look alike in the
    # triangle list and want opposite treatment:
    #
    #   72 tris  glow book, rendermode 005049D8 (RM_XLU) + combiner FCFFFFFF/FFFDF2F9
    #            (PRIM_TEXA: rgb = PRIM, alpha = TEXEL0_A). These are the two halo shells.
    #            bake_mesh_prim leaves them alone because RM_XLU + PRIM_TEXA is the
    #            VEHICLE SHADOW signature and shadows have their own pass -- so they
    #            arrived here as MODE_SHADOW (bake_mesh picks that from the I8 texture
    #            format) with their PRIM dropped and a packed normal in the colour slot.
    #            Drawn that way the ion beam's glow is a dark blob. They are translucent
    #            colour: the shell's own PRIM (E2FDFF and 56B9FF) with the I8 texture
    #            supplying alpha.
    #   28 tris  core book, NO combiner and NO rendermode in the list at all -- they run
    #            before the beam's first G_SETCOMBINE and inherit the neutral state
    #            FFFCF279 = colour TEXEL0, alpha TEXEL0_A. That references no SHADE term,
    #            so the vertex slot is not read and white is the honest substitute.
    #    1 tri   untextured, PRIM_ONLY, the pale lilac E5A6D7 disc. bake_mesh_prim has
    #            ALREADY folded its prim in; touching it again would undo that.
    dlb = 0x01035E0
    rb = B.R.make_resolve(dlb)
    _vp, wt_prim = B.walk(B.ROM, dlb, rb)
    _vc, wt_comb = B.R.walk(B.ROM, dlb, rb)
    assert len(wt_prim) == len(base[1]) == len(wt_comb), (
        "ion beam: the three walks disagree about triangle count (%d / %d / %d)"
        % (len(wt_prim), len(base[1]), len(wt_comb)))
    nglow = ncore = nprim = 0
    fixed = []
    for i, (ti, mode, wrap, tri) in enumerate(base[1]):
        prim = wt_prim[i][3].get("prim")
        cc = wt_comb[i][3].get("combine")
        rm = wt_comb[i][3].get("rendermode")
        if rm == RM_XLU and cc == PRIM_TEXA and prim is not None:
            pr, pg, pb, _pa = prim
            tri = [v[:5] + (pr, pg, pb, v[8]) for v in tri]
            mode = MODE_XLU
            nglow += 1
        elif ti >= 0:
            tri = [v[:5] + (255, 255, 255, v[8]) for v in tri]
            ncore += 1
        else:
            nprim += 1
        fixed.append((ti, mode, wrap, tri))
    assert (nglow, ncore, nprim) == (72, 28, 1), (
        "ion beam: expected 72 halo / 28 core / 1 untextured triangles, got %d / %d / %d"
        % (nglow, ncore, nprim))
    base[1] = fixed
    assert len(base[1]) == 101, "ion beam: expected 101 triangles, got %d" % len(base[1])
    for slot in (3, 4, 5):
        assert len(base[slot]) == 28, \
            "ion ring slot %d: expected 28 triangles, got %d" % (slot, len(base[slot]))
    # which bank entry each of the beam's three groups was authored with
    beam_core0, beam_glow0 = core_idx[0], glow_idx[0]
    ring0 = ring_idx[0]

    out = {}
    ndrawn = 0
    for sn in range(ION_NSLOT):
        t = sn * ION_TICK
        tris = []
        parts = []
        for slot, dl, ct0, ct1, name in ION_PARTS:
            if t < ct0:
                continue                      # this piece has not started yet
            if t >= hold_to[slot]:
                continue                      # fully dissolved, or the beam has finished
            node = docs[slot]["nodes"][0]
            frames = node["frames"]
            fi = int(round((t - ct0) / float(ION_TICK)))
            if fi < 0: fi = 0
            if fi >= len(frames): fi = len(frames) - 1
            C = frames[fi]["C"]
            rw = node["rest_world"]
            # image for this piece at this absolute clip time
            if slot == 1:
                k = _ion_book_image(beam_book["times"], t % beam_book["period"])
                remap = {beam_core0: core_idx[k], beam_glow0: glow_idx[k]}
            else:
                k = _ion_book_image(ring_books[slot]["times"], t)
                remap = {ring0: ring_idx[k]}
            # The beam's colours were settled once, above, per GROUP -- and they had to
            # be settled there rather than here, because flatten_lit_meshes only acts on a
            # mesh whose EVERY vertex is a packed normal and seven of these ten snapshots
            # mix the beam with rings whose colours are real. A mixed mesh is never
            # flattened, so a beam left to that pass would ship modulated by a normal
            # vector: the same crush that turned the ATTACK cursor grey.
            p0 = len(tris)
            for (ti, mode, wrap, tri) in base[slot]:
                nt = []
                for v in tri:
                    px, py, pz = xform((v[0], v[1], v[2]), rw["M"], rw["t"])
                    ax, ay, az = xform((px, py, pz), C[:3], C[3])
                    nt.append((ax * ION_MODEL_SCALE, ay * ION_MODEL_SCALE,
                               az * ION_MODEL_SCALE,
                               v[3], v[4], v[5], v[6], v[7], v[8]))
                tris.append((remap.get(ti, ti), mode, wrap, nt))
            parts.append((p0, len(tris) - p0, ROLE_STATIC, (0.0, 0.0, 0.0)))
        if not tris:
            continue
        ndrawn += 1
        name = "ION%d" % sn
        meshindex[name] = len(meshes)
        meshparts[name] = parts
        meshsections[name] = []
        meshes.append((name, tris))
        out[name] = name

    assert ndrawn == ION_NSLOT, \
        "ion cannon: expected %d snapshots to carry geometry, got %d" % (ION_NSLOT, ndrawn)
    if verbose:
        tot = sum(len(meshes[meshindex["ION%d" % i]][1]) for i in range(ION_NSLOT))
        print("ion cannon: %d snapshots ION0..ION%d, %d triangles, beam 8-image book "
              "(I8 glow + RGBA16 core) and a shared 4-image ring book at MODEL_SCALE %.2f"
              % (ndrawn, ION_NSLOT - 1, tot, ION_MODEL_SCALE))
    return out


# ---------------------------------------------------------------------------
# THE RALLY POINT FLAG (PK4 mesh + PKB clip)
# ---------------------------------------------------------------------------
# the project owner, 25 Aug 2026: "Take the Flag Pole and Animated flag from the Barracks, seperate it,
# and use it for the Rally Point for all buildings."
#
# The Barracks has one, and it is the only flag in the cartridge. PYLE's scene graph is
# four nodes: the building body, then THREE chained BEEFED nodes at y=916 marching along
# x, which are the flag's segments. Each is four triangles of grey and white (120,120,120
# and 220,220,220) under a 4x4 texture, and slot_030.json drives them: part 2 swings 266
# units in x and 427 in z with 53 degrees of yaw over the clip, part 3 further still.
# That is a flag flapping, and it is already animated data we ship.
#
# THE POLE IS NOT A NODE. It is the first three triangles of the BODY's own display list --
# untextured, colour (58,53,41), x -687..-611, z 98..167, and 1068 units tall, which is the
# only thing in that mesh reaching up to where the flag hangs. Being a contiguous run at
# the START of the list is what makes lifting it out clean rather than surgical, and the
# assertions below check every one of those properties rather than trusting the range.
#
# GREY ON PURPOSE. The flag's own colours are near-neutral, so the renderer can multiply
# the player's house colour straight through them (GDI gold, Nod red, the multiplayer
# colours) without a palette swap or a second texture. That is why this mesh keeps its
# authored vertex colours instead of being whitened.
RALLY_FLAG_MESH = "RALLYFL"        # 7: the pack asserts len(code) < 8, not <= 8
RALLY_POLE_DL = 0x0144718          # PYLE's body; the pole is triangles 0..2
RALLY_POLE_TRIS = 3
RALLY_FLAG_STEP = 4                # keep every 4th frame of the 301: 76, still smooth


def bake_rally_flag(bank, meshes, meshindex, meshparts, meshsections, meshanim,
                    um, verbose=True):
    """-> {typecode: meshname} for the rally flag, or {} if PYLE or its clip is absent."""
    if "PYLE" not in um:
        return {}
    path = os.path.join(ANIM_DIR, "slot_030.json")
    if not os.path.isfile(path):
        return {}
    doc = json.load(open(path))
    graph = [p for p in _graph_of(um["PYLE"]) if p.get("gfx_rom") is not None]
    assert len(graph) == 4, "PYLE: expected body + 3 flag segments, got %d parts" % len(graph)

    # ---- the pole, lifted out of the body's triangle list ----------------------------
    body = bake_mesh_prim(RALLY_POLE_DL, bank)
    pole = body[:RALLY_POLE_TRIS]
    assert all(t[0] < 0 for t in pole), \
        "rally flag: the pole triangles are textured, so this is not the pole any more"
    ptop = max(v[1] for (_t, _m, _w, tri) in pole for v in tri)
    assert ptop > 1000, "rally flag: the pole only reaches y=%.0f, expected over 1000" % ptop
    assert all(max(v[1] for v in t[3]) <= 600 for t in body[RALLY_POLE_TRIS:]), (
        "rally flag: something AFTER the first %d triangles also reaches the flag's "
        "height, so the pole is no longer a clean prefix of the list" % RALLY_POLE_TRIS)
    # stand it at the origin: the pole's own footprint centre, ground at y=0
    pxs = [v[0] for (_t, _m, _w, tri) in pole for v in tri]
    pzs = [v[2] for (_t, _m, _w, tri) in pole for v in tri]
    ox, oz = (min(pxs) + max(pxs)) * 0.5, (min(pzs) + max(pzs)) * 0.5

    tris, parts = [], []
    def add(sub, M, t, untexture=False):
        p0 = len(tris)
        for (ti, mode, wrap, tri) in sub:
            nt = []
            for v in tri:
                x, y, z = xform((v[0], v[1], v[2]), M, t) if M is not None else (v[0], v[1], v[2])
                nt.append((x - ox, y, z - oz, v[3], v[4], v[5], v[6], v[7], v[8]))
            tris.append((-1 if untexture else ti, mode, wrap, nt))
        parts.append((p0, len(tris) - p0, ROLE_STATIC, (0.0, 0.0, 0.0)))

    add(pole, None, None)                                   # part 0: the pole
    for g in graph[1:]:                                     # parts 1..3: the segments
        # THE FLAG IS DRAWN UNTEXTURED HERE, and that is what makes "the player colour"
        # possible at all. Its cartridge texture is a solid 4x4 of (132,16,8) -- a RED
        # banner -- so a flag left textured is red whatever colour the renderer multiplies
        # through it, and picking the GDI variant only trades red for gold. Dropping the
        # texture leaves the authored vertex greys (120,120,120 and 220,220,220), which
        # are shading rather than colour, so glColor with the house's own colour paints
        # the flag GDI gold, Nod red, or any multiplayer colour and keeps the fold of the
        # cloth. The pole keeps its own dark (58,53,41) and was never textured.
        add(bake_mesh_prim(g["gfx_rom"], bank), g["M"], g["t"], untexture=True)

    # ---- the wave, subsampled off PYLE's own looping clip -----------------------------
    nodes = {n["bake_part_index"]: n for n in doc["nodes"]
             if n.get("bake_part_index") is not None}
    src = len(nodes[1]["frames"])
    keep = list(range(0, src, RALLY_FLAG_STEP))
    nf, nparts = len(keep), len(parts)
    mat = [0.0] * (nf * nparts * 12)
    vis = [1] * (nf * nparts)
    for f in range(nf):
        for pi in range(nparts):
            o = (f * nparts + pi) * 12
            mat[o + 0] = mat[o + 5] = mat[o + 10] = 1.0
    # THE CLIP ROTATES ABOUT PYLE'S ORIGIN, NOT ABOUT THE POLE, and standing the flag at
    # the origin above moved the geometry away from that centre. Applied unchanged, each
    # segment then swings about a point it is no longer attached to and the three come
    # apart into separate rectangles -- measured on screen before this was fixed.
    #   the renderer computes  v' = v . C3 + Ct
    #   the vertices are now   v - o
    #   and what is wanted is  v . C3 + Ct - o
    # so the baked translation becomes  Ct - o + o . C3, which is the same motion taken
    # about the new origin. Only x and z shift; the flag was not moved in y.
    off = (ox, 0.0, oz)
    for pi in range(1, nparts):
        frames = nodes[pi]["frames"]
        for f, si in enumerate(keep):
            C = frames[si]["C"]
            o = (f * nparts + pi) * 12
            for r in range(3):
                for c in range(3):
                    mat[o + r * 4 + c] = float(C[c][r])
            # o . C3 in the renderer's own convention (row r, column c -> C[c][r])
            oc = [sum(off[c] * float(C[c][r]) for c in range(3)) for r in range(3)]
            for r in range(3):
                mat[o + r * 4 + 3] = float(C[3][r]) - off[r] + oc[r]

    meshindex[RALLY_FLAG_MESH] = len(meshes)
    meshparts[RALLY_FLAG_MESH] = parts
    meshsections[RALLY_FLAG_MESH] = []      # not a building: no construction sections
    meshes.append((RALLY_FLAG_MESH, tris))
    meshanim[RALLY_FLAG_MESH] = dict(frames=nf, ticks_per_frame=1,
                                     clips=[dict(t0=0, t1=nf, loop=True)],
                                     mat=mat, vis=vis)

    assert len(tris) == 15, \
        "rally flag: expected 15 triangles (3 pole + 3x4 flag), got %d" % len(tris)
    if verbose:
        print("rally flag: %s, %d triangles in %d parts, %d frames of PYLE's own wave "
              "(every %dth of %d), pole %.0f units tall"
              % (RALLY_FLAG_MESH, len(tris), nparts, nf, RALLY_FLAG_STEP, src, ptop))
    return {RALLY_FLAG_MESH: RALLY_FLAG_MESH}


def bake_damage_overlays(bank, meshes, meshindex, meshparts, meshsections, verbose=True):
    """The cartridge's per-building DAMAGE ART, as ordinary meshes.

    Every structure the console has a model for also has damage art, and the rule that
    selects it is byte for byte the 1995 rule -- BuildingClass::Draw3D tests
    Health_Ratio() against 0x80, half health. The art is NOT a damaged mesh and NOT a
    fourth palette; it is a standalone display list that the console APPENDS over the
    intact building. tools/romdump/damage_art.py extracts the table and asserts every
    address; this only has to bake what that found.

    THE SECTION LIST IS EMPTY, and that is deliberate rather than lazy. It follows
    bake_struct_flipbooks, not bake_debris_chunks: an overlay is not a building that gets
    constructed piece by piece, and game/shatter_mod.h early-outs on a mesh with no
    sections, so an empty list keeps 21 overlays out of the shatter's decomposition and
    out of shatterdump entirely. A [0] here would add 21 lines to a diagnostic two gates
    read.

    The pack format does not change at all: these are extra meshes and extra type-table
    rows, the same shape as the debris chunks and the texture flipbooks.
    """
    import json as _json
    path = os.path.join(HERE, "damage_art.json")
    rows = _json.load(open(path))["records"]

    out, names = {}, {}
    ncomb = 0
    for r in rows:
        if not r["dl_rom"]:
            continue                      # OBLI: damage smoke, no display list
        name = "dl_%07X" % r["dl_rom"]
        if name not in meshindex:
            tris = bake_mesh_prim(r["dl_rom"], bank)
            meshindex[name] = len(meshes)
            meshparts[name] = []
            meshsections[name] = []       # see the note above: EMPTY, not [0]
            meshes.append((name, tris))
        names[name] = 1
        out["%sDMG" % r["ident"]] = name

    # EQUALITY tripwires, never subsets. A subset assert is blind to a row that STOPS
    # appearing, and an empty set satisfies it, so a bake that silently produced nothing
    # would pass. This codebase has been bitten by exactly that; see flatten_lit_meshes.
    assert len(out) == 21, \
        "damage overlays: expected 21 type codes, got %d (%s)" % (len(out), sorted(out))
    assert len(names) == 20, \
        "damage overlays: expected 20 distinct meshes, got %d -- HQ and EYE share one" % len(names)
    if verbose:
        print("damage art: %d structures, %d distinct display lists -- the cartridge's own "
              "half-health overlay, appended over the intact building" % (len(out), len(names)))
    return out


def bake_struct_flipbooks(um, bank, meshes, meshindex, meshparts, meshsections,
                          verbose=True):
    """-> {type code: mesh name} for the structures whose animation is a TEXTURE SWAP.

    THE THIRD ANIMATION CHANNEL, and the reason the Construction Yard's fans have been
    wrong twice. The cartridge hangs a time-indexed texture book off a scene-graph node's
    +0x10 word: {times, images, settimg, match, tail, count}, applied at RAM 0x80080FAC,
    with a load-time splitter at RAM 0x80080E28 that cuts the node's display list either
    side of the matched G_SETTIMG so the console can emit head + chosen image + tail. The
    geometry is drawn ONCE with a substituted texture. tools/bakery/texbook.py carries the
    decode and the assertions.

    A per-frame texture swap IS a mesh variant, so this bakes one mesh per image exactly
    as bake_cursor_flipbooks does for cursors 0x0A and 0x0B, and no pack format changes.

    FACT is three images -- a four-blade fan at three rotations -- over a period of 480
    at a step of 160, which is three frames."""
    extra = {}
    # ONE TYPE CODE PER TIME SLOT, at the 160-unit tick every book's times divide by.
    # The books are NOT uniformly spaced -- the refinery's warning triangle is lit for 1600
    # units and dark for 14400 -- so a plain (frame / N) % count would be wrong for two of
    # the six. Emitting a code per slot puts the cartridge's own schedule in the DATA and
    # leaves the renderer one formula, and it costs only type-table rows: the MESHES are
    # one per image and the slots point at repeats.
    TICK = 160
    for tc, nodes in sorted(TEXBOOK.KNOWN_NODES.items()):
      if tc not in um:
          continue
      for booki, node in enumerate(nodes):
        fb = TEXBOOK.texbook(B.ROM, node)
        assert fb, "%s node %08X was expected to carry a texture book and does not" % (tc, node)
        # THE GEOMETRY IS NOT IN THE TYPE'S ROOT MESH, and that is the discovery that
        # closes this out. The node carrying the book has dl = 0 at node+0x00; its
        # drawable list hangs off the payload instead, as `src`. Our walker follows
        # node+0x00 and node+0x0C only, so these faces have NEVER been extracted -- which
        # is why FACT's 69 baked triangles contain no fan, and why every rule written over
        # them found something else. Walk the payload's own list.
        dl = TEXBOOK.ram_to_rom(fb["src"])
        assert dl is not None, "%s: the book's display list %08X is in no known segment" % (
            tc, fb["src"])
        resolve = B.R.make_resolve(dl)
        _verts, wt = B.walk(B.ROM, dl, resolve)
        base_state = None
        for w in wt:
            if w[3].get("timg") == fb["match"]:
                base_state = dict(w[3])
                break
        assert base_state is not None, (
            "%s: no triangle in the book's own display list %08X draws its image %08X, "
            "so there is nothing to substitute and the swap would be invisible. That is "
            "exactly the shape of the last two wrong answers about these fans."
            % (tc, dl, fb["match"]))
        idx = []
        for img in fb["images"]:
            st = dict(base_state)
            st["timg"] = img
            key, tw, th, px, _rest = B.decode_tex(st, resolve)
            idx.append(bank.add_rgba(key, tw, th, px))
        assert len(set(idx)) == len(idx), \
            "%s: two of the book's images collapsed onto one bank entry" % tc
        tris = bake_mesh_prim(dl, bank)
        parts = []
        base_bi = idx[0]
        nswap = sum(1 for t in tris if t[0] == base_bi)
        assert nswap > 0, \
            "%s: the baked list draws nothing with the book's base texture" % tc
        # One mesh per IMAGE, deduplicated by name so a second scenario reuses it.
        meshfor = []
        for k, bi in enumerate(idx):
            name = "%s_b%dt%d" % (um[tc]["mesh"], booki, k)
            if name not in meshindex:
                vt = [((bi if ti == base_bi else ti), mode, wrap, tri)
                      for (ti, mode, wrap, tri) in tris]
                meshindex[name] = len(meshes)
                meshparts[name] = list(parts)
                meshsections[name] = []
                meshes.append((name, vt))
            meshfor.append(name)

        # ...and one type code per SLOT, walking the book's own time table. times[] has
        # count+1 entries, the last being the period, so slot s covers [times[i], times[i+1]).
        times = fb["times"]
        nslot = int(fb["period"]) // TICK
        assert nslot >= 1 and nslot <= 999, \
            "%s book %d: period %d does not divide the 160-unit tick sensibly" % (
                tc, booki, fb["period"])
        letter = "TUVW"[booki]
        for slot in range(nslot):
            t = slot * TICK
            img = 0
            for i in range(len(idx)):
                if times[i] <= t < times[i + 1]:
                    img = i
                    break
            else:
                img = len(idx) - 1
            code = "%s%s%d" % (tc, letter, slot)
            assert len(code) <= 8, "%s is too long for the pack's 8-byte type field" % code
            extra[code] = meshfor[img]
        if verbose:
            print("texture book: %s book %d -> %d images over period %d in %d slots of "
                  "%d units, %d triangles (%d reskinned per image)"
                  % (tc, booki, len(idx), fb["period"], nslot, TICK, len(tris), nswap))
    return extra


# ---------------------------------------------------------------------------
# THE DESERT FLORA SWAP
# ---------------------------------------------------------------------------
# The cartridge does not ship a separate desert tree TYPE. It ships one set of tree
# types and REMAPS THE MODEL ID AT DRAW TIME when the theater is desert. The draw
# command enqueue at RAM 0x8004A420 (resident ROM 0x04B020) reads the theater word at
# RAM 0x80097150 and, when it is zero, adds 97 to any model id in [103,121]:
#
#     0x8004A430  lw    $v0, 0x7150($v0)      the theater word
#     0x8004A454  bnez  $v0, ...              not theater 0: leave the id alone
#     0x8004A45C  slti  $v0, $s0, 0x67        id < 103: leave it alone
#     0x8004A468  slti  $v0, $s0, 0x7A        id < 122
#     0x8004A470  addiu $s0, $s0, 0x61        +97, in a BRANCH-LIKELY delay slot
#
# Branch-likely means that add runs only when the branch is taken, so the input range
# is exactly [103,121], which is exactly T01..T18. Model slot is id + 10, so the
# outputs are slots 210..228, and those nineteen slots hold exactly FOUR distinct
# scene-graph nodes: two cacti and two scrub trees.
#
# WHY IT IS RESOLVED HERE AND NOT IN THE RENDERER. A pack is baked for ONE scenario and
# therefore for one theater, and the pack's type table is keyed by the INI name the
# renderer already looks the object up by. Binding the name to the right mesh at bake
# time computes the same answer the console computes at draw time, needs no renderer
# change and no pack format change, and costs nothing per frame. The alternative -- a
# theater test inside draw_terrain_object -- would have to re-derive the theater in the
# view layer from a string the pack carries for a different purpose, which is the very
# confusion the resolver below exists to prevent. So: BAKER SIDE, deliberately.
#
# T08 IS THE ONE THAT MUST STAY THEATER DEPENDENT. It is the only tree registered in
# all three theater fields (id 110 in every one), it is placed on 558 desert cells
# across 44 shipped missions AND on temperate cells too, so an unconditional swap would
# put a desert scrub tree in the temperate campaign. T04, T09 and T18 are desert only.
#
# ALL EIGHTEEN ARE BOUND, not only the four a shipped desert mission actually places.
# The cartridge's arithmetic covers the whole range and so does this: T01..T03, T05..T07
# and T10..T17 register -1 in their desert field, so no shipped scenario can place one,
# but the map editor can and the console would draw a cactus if it did. Binding only the
# four would have left a hole that only an authored map could fall into.
#
# THE FOUR FAMILIES, straight off the primary model table (base ROM 0x9A598, stride 16,
# scene-graph node pointer at +0x0C). Slot = n64 id + 97 + 10:
#     210..213 -> node 0x801B46C8  dl_00FE360   tall columnar cactus, 12 tris
#     214..218 -> node 0x801B46E4  dl_00FE660   scrub tree,            8 tris
#     219..222 -> node 0x801B4700  dl_00FE918   small twin cactus,     8 tris
#     223..228 -> node 0x801B471C  dl_00FEBD8   largest scrub tree,    8 tris
# Four, five, four and six slots wide: nineteen slots for eighteen tree types, and the
# one spare is id 111, which no TerrainTypeClass registers. It falls in the five-wide
# family, the only family with room for it. That is the enumeration closing on itself.
# Each family is named here after the one desert-legal tree in it.
DESERT_TERRAIN_FAMILY = ((210, 213, "T04D"), (214, 218, "T08D"),
                         (219, 222, "T09D"), (223, 228, "T18D"))
# The N64 model id of each tree type, from the 32 TerrainTypeClass registrations (ctor
# RAM 0x801EAC0C, jals ROM 0x18CEE0..0x18DD70): T01..T08 are 103..110, id 111 is unused,
# T09..T18 are 112..121. check_terrain_ids below reads the same numbers back out of
# unit_models.json's own n64_model_index fields, so the table and the data cannot drift
# apart in silence.
TERRAIN_N64_ID = dict(("T%02d" % n, (102 + n) if n <= 8 else (103 + n))
                      for n in range(1, 19))
DESERT_TERRAIN_MESH = {}
for _code, _id in TERRAIN_N64_ID.items():
    _slot = _id + 97 + 10
    for _lo, _hi, _entry in DESERT_TERRAIN_FAMILY:
        if _lo <= _slot <= _hi:
            DESERT_TERRAIN_MESH[_code] = _entry
            break
assert len(DESERT_TERRAIN_MESH) == 18 and \
    (DESERT_TERRAIN_MESH["T04"], DESERT_TERRAIN_MESH["T08"],
     DESERT_TERRAIN_MESH["T09"], DESERT_TERRAIN_MESH["T18"]) == \
    ("T04D", "T08D", "T09D", "T18D"), DESERT_TERRAIN_MESH


def check_terrain_ids(um):
    """The tree ids this file derives the swap from must be the ones the data carries.

    A silent disagreement here would bind a tree to the wrong cactus family and nothing
    downstream could tell, because every family is a legal mesh.
    """
    for code, want in sorted(TERRAIN_N64_ID.items()):
        got = um.get(code, {}).get("n64_model_index")
        assert got == want, ("unit_models.json %s has n64_model_index %r, but the "
                             "desert flora swap was derived from %d. One of the two is "
                             "wrong and the swap must not run until it is settled."
                             % (code, got, want))

# THE THEATER IDENTITY, and why it is not the string in the terrain JSON.
#
# There are TWO different theater words in this project and they legitimately disagree.
# game/missions/SCA01EA.INI says Theater=DESERT (what the TD brain runs) and carries
# CNC3DTheater=SAND (what we repaint it with), so its baked terrain JSON says "SAND".
# A flora test keyed on the string "DESERT" therefore MISSES a genuine desert mission,
# and the same hole swallows SNOW, which is our repaint of WINTER. The flora remap is a
# property of the ENGINE theater, because the ids it moves come from the TerrainTypeClass
# per-theater fields the brain itself indexes -- so the engine theater is what decides,
# and the look name never is.
#
# LOOK_THEATER is deliberately EXHAUSTIVE and raises on anything it has not been told
# about: adding a sixth repaint must be a decision, not a silent fall-through to "not
# desert". TD_THEATER is the Tiberian Dawn theater enum, which is also the cartridge's:
# its loader at RAM 0x801E5190 selects DESERT.TL4/.TL8 for 0 and TEMPERAT.TL4/.TL8 for 2
# and prints "Illegal theater type: %d" for anything else.
LOOK_THEATER = {"DESERT": "DESERT", "SAND": "DESERT",
                "TEMPERAT": "TEMPERATE", "TEMPERATE": "TEMPERATE",
                "WINTER": "WINTER", "SNOW": "WINTER"}
TD_THEATER = {"DESERT": 0, "JUNGLE": 1, "TEMPERATE": 2, "WINTER": 3}
THEATER_DESERT = 0


def _ini_theater(scen):
    """The scenario's ENGINE theater off its own INI, or None if no INI is staged.

    USER01, USER50 and the XL test maps have no INI in the extracted tree; they are
    authored maps whose terrain JSON is checked in. Returning None for them is honest,
    and engine_theater below then has exactly one source instead of two.
    """
    path = os.path.join(B.ASSETS, "extracted", "INI", scen.upper() + ".INI")
    if not os.path.isfile(path):
        return None
    for line in open(path, errors="ignore"):
        t = line.strip().upper()
        if t.startswith("THEATER="):
            return t.split("=", 1)[1].strip()
    return None


def engine_theater(scen, look):
    """-> (name, id) the TD theater this scenario runs in. Never guesses.

    Two independent sources: the scenario INI's own Theater= key, and the look name the
    terrain JSON carries. Where both exist they must AGREE through LOOK_THEATER, and a
    disagreement aborts the bake rather than picking one. Where only the look name
    exists it is mapped through the same exhaustive table. An unknown value on either
    side is a hard error, because the failure this guards against is a new theater name
    quietly defaulting to "not desert" and stripping the cacti out of a desert map.
    """
    lk = str(look or "").strip().upper()
    if lk not in LOOK_THEATER:
        raise SystemExit("bake5: %s: terrain JSON theater %r is not a known look name "
                         "(%s). Add it to LOOK_THEATER with the ENGINE theater it "
                         "repaints; do not let it default."
                         % (scen, look, ", ".join(sorted(LOOK_THEATER))))
    from_look = LOOK_THEATER[lk]
    from_ini = _ini_theater(scen)
    if from_ini is not None:
        if from_ini not in TD_THEATER:
            raise SystemExit("bake5: %s: INI Theater=%s is not a Tiberian Dawn theater "
                             "(%s)" % (scen, from_ini, ", ".join(sorted(TD_THEATER))))
        if from_ini != from_look:
            raise SystemExit("bake5: %s: INI Theater=%s but the terrain JSON was "
                             "resolved for look %s, which repaints %s. One of the two "
                             "is stale; re-resolve the terrain JSON rather than baking "
                             "a map whose flora and whose ground disagree."
                             % (scen, from_ini, lk, from_look))
    name = from_ini or from_look
    return name, TD_THEATER[name]


def terrain_source_code(code, theater_id):
    """The unit_models entry whose MESH the INI name `code` draws in this theater."""
    if theater_id == THEATER_DESERT:
        return DESERT_TERRAIN_MESH.get(code, code)
    return code


def build(scen, outpath, verbose=True, heights=None, cmvals=None):
    um = json.load(open(os.path.join(B.SCR, "unit_models.json")))
    terr = json.load(open(os.path.join(B.ASSETS, "terrain", "terrain_%s.json" % scen)))
    # The ENGINE theater, resolved once and used for every theater-dependent decision
    # below. Not terr["theater"]: that is the LOOK name, and SAND is a repaint of
    # DESERT (see engine_theater).
    theater_name, theater_id = engine_theater(scen, terr["theater"])
    check_terrain_ids(um)
    # Cell grid size, from the terrain JSON's own width/height fields. Older
    # JSONs carry no such fields: those are the legacy 64x64 bakes. The
    # per-corner blocks (PK9 heights, PKC tint) sit on a (W+1)x(H+1) grid.
    gw = int(terr.get("width", 64))
    gh = int(terr.get("height", 64))
    cw, ch = gw + 1, gh + 1
    ncorner = cw * ch
    bank = B.TexBank()

    from PIL import Image
    atlas = Image.open(os.path.join(B.ASSETS, "terrain", terr["atlas"])).convert("RGBA")
    aw, ah = atlas.size
    terrain_tex = bank.add_rgba(("terrain", terr["atlas"]), aw, ah, atlas.tobytes())

    # THE DOS ATLAS (PKG), the same tiles drawn from Tiberian Dawn's own theater art.
    # Enhanced Visuals binds this instead of the cartridge one; the cells carry a single
    # set of UVs for both, so the two MUST be the same size and packed identically.
    # -1 means the theater has no DOS original (SNOW, SAND) and the switch has nowhere
    # to go. WINTER's bank was BUILT from that art (tools/win_to_n64bank.py), so its two
    # atlases come out byte-identical and share one bank slot rather than paying twice.
    terrain_dos_tex = -1
    dosname = terr.get("atlasDos")
    if dosname:
        dosatlas = Image.open(os.path.join(B.ASSETS, "terrain", dosname)).convert("RGBA")
        dw, dh = dosatlas.size
        # PROPORTIONAL, not equal. The cells carry NORMALISED uvs, so a second atlas
        # addresses the same tiles whenever it is the first one scaled by a whole number
        # in both axes -- tile, gutter and pitch all multiplied by the same k. That is
        # what lets a higher-resolution atlas (k=4 gives a 96-texel cell, k=5 gives 120)
        # ride in this slot with no change to a single uv. Equality is just k=1.
        assert dw % aw == 0 and dh % ah == 0 and dw // aw == dh // ah, \
            ("%s: the second atlas is %dx%d against the cartridge atlas %dx%d, which is "
             "not a whole-number scale in both axes -- one set of uvs cannot address both"
             % (dosname, dw, dh, aw, ah))
        dosbytes = dosatlas.tobytes()
        # dw/dh, NOT aw/ah. Passing the cartridge's dimensions here was harmless only
        # while the two atlases were the same size: the bank would then pad and copy by
        # the wrong stride and store a mangled sheet. It is the second atlas's own size
        # that describes its bytes.
        terrain_dos_tex = (terrain_tex if dosbytes == atlas.tobytes()
                           else bank.add_rgba(("terrain", dosname), dw, dh, dosbytes))

    meshes, meshindex, meshparts, meshsections, meshanim = [], {}, {}, {}, {}
    roles_doc = {}
    for code in sorted(k for k in um if k != "_meta"):
        m = um[code]["mesh"]
        if m is None or m in meshindex:
            continue
        tris, parts = bake_mesh_parts(code, um[code], bank)
        check_expected(code, parts)
        meshindex[m] = len(meshes)
        meshparts[m] = parts
        meshsections[m] = mesh_sections(code, um[code], parts)
        if os.environ.get("CNC3D_FAKEANIM") == code:
            meshanim[m] = fake_anim(len(parts))
            if verbose:
                print("anim: SYNTHETIC yaw clip on %s (%d parts, 32 frames) -- "
                      "CNC3D_FAKEANIM is a plumbing test, not cartridge data"
                      % (code, len(parts)))
        elif is_cursor_code(code):
            a = bake_cursor_anim(code, um[code], parts, verbose)
            if a is not None:
                meshanim[m] = a
        else:
            a = bake_node_anim(code, um[code], parts, verbose)
            if a is not None:
                meshanim[m] = a
        meshes.append((m, tris))
        if len(parts) > 1 or parts[0][2] != ROLE_STATIC:
            roles_doc[code] = dict(mesh=m, parts=[
                dict(tri0=p[0], ntris=p[1],
                     role=["static", "turret", "rotor"][p[2]],
                     pivot=[round(x, 2) for x in p[3]]) for p in parts])
    # BEFORE the flatten, so the per-frame variants get exactly the same packed-normal
    # treatment their own base mesh does.
    flip_types = bake_cursor_flipbooks(um, bank, meshes, meshindex, meshparts,
                                       meshsections, verbose)
    # Same place and the same reason: the structure texture books are mesh variants too,
    # and they must be walked before the flatten so a variant gets exactly the treatment
    # its own base mesh does.
    flip_types.update(bake_struct_flipbooks(um, bank, meshes, meshindex, meshparts,
                                            meshsections, verbose))
    # BEFORE THE FLATTEN, and the ordering is load-bearing rather than tidy. Two of the
    # damage lists are all-packed-unit-normal (HOSP 24/24, FIX 6/6) and need exactly the
    # whitening every other lit mesh gets. Baked AFTER the flatten they would ship
    # modulated by dark green and look wrong on screen with no error anywhere, which is
    # the worse failure of the two.
    flip_types.update(bake_damage_overlays(bank, meshes, meshindex, meshparts,
                                           meshsections, verbose))
    # BEFORE THE FLATTEN for the same reason again: all 288 of the nuke's vertices carry
    # packed unit normals (dome 76/76, stem 22/22, collar 30/30, cloud 160/160) and no
    # nuke list enables G_LIGHTING, so the colour slot is not a colour. White is the
    # honest substitute and it must be applied here or the mushroom shows up modulated by
    # a normal vector.
    flip_types.update(bake_nuke_effect(bank, meshes, meshindex, meshparts,
                                       meshsections, meshanim, verbose))
    # The ion cannon whitens its own beam per PART (see the note in bake_ion_effect), so
    # unlike every other producer above it does not depend on running before the flatten.
    # It runs here anyway so both effects sit together and the GDI re-walk below can offer
    # its textures to bank2 in the same order.
    flip_types.update(bake_ion_effect(bank, meshes, meshindex, meshparts,
                                      meshsections, verbose))
    # The rally flag keeps its authored grey so the renderer can multiply the player's
    # house colour through it, so like the ion it does not depend on the flatten.
    flip_types.update(bake_rally_flag(bank, meshes, meshindex, meshparts,
                                      meshsections, meshanim, um, verbose))
    flatten_lit_meshes(meshes, verbose)
    # AFTER the flatten: the chunk colours are already folded and two of them are
    # deliberately prim-only, so the packed-normal whitener has nothing to do here and
    # must not be given the chance.
    debris_types, debris_gdi = bake_debris_chunks(
        bank, meshes, meshindex, meshparts, meshsections, verbose)
    if verbose:
        ntot = sum(len(t) for _, t in meshes)
        nturr = sum(1 for m, _ in meshes for p in meshparts[m] if p[2] == ROLE_TURRET)
        nrot = sum(1 for m, _ in meshes for p in meshparts[m] if p[2] == ROLE_ROTOR)
        nsec = sum(len(meshsections[m]) for m, _ in meshes)
        print("meshes: %d, triangles %d, textures %d, turret parts %d, rotor parts %d, "
              "%d construction sections" % (len(meshes), ntot, len(bank.items),
                                            nturr, nrot, nsec))

    # ---- the GDI texture variants (PK6) ----------------------------------
    # Re-run the exact mesh walk with the baker's TLUT swapped to the GDI table,
    # into a second bank. Same walk, same keys, so bank2's items line up with
    # bank's by key; a variant is stored only where the two tables actually
    # decode different pixels. GLOBAL_TLUT is restored even if a bake asserts.
    gdi_data = [None] * len(bank.items)
    # The debris textures carry their GDI variant from the extractor rather than from the
    # re-walk below, because no mesh walk reaches them.
    for bi, rgba in debris_gdi:
        if rgba != bank.items[bi][4]:
            gdi_data[bi] = rgba
    saved_tlut = B.GLOBAL_TLUT
    try:
        B.GLOBAL_TLUT = GDI_TLUT
        bank2 = B.TexBank()
        for code in sorted(k for k in um if k != "_meta"):
            m = um[code]["mesh"]
            if m is None:
                continue
            bake_mesh_parts(code, um[code], bank2)
        # The flipbook cursors' per-frame images go through the SAME pass, into throwaway
        # mesh containers, so every texture in `bank` has been offered to `bank2` under the
        # same key. Without this the diff below would silently skip them and the invariant
        # this whole block rests on ("same walk, same keys") would be false for 5 textures.
        bake_cursor_flipbooks(um, bank2, [], {}, {}, {}, verbose=False)
        bake_struct_flipbooks(um, bank2, [], {}, {}, {}, verbose=False)
        # AND THE DAMAGE OVERLAYS, for exactly the same reason and it is not optional.
        # Their 15 textures go through the house TLUT like every other building texture,
        # and 10 of the 16 decode DIFFERENTLY through the GDI table. Without this line
        # they are never offered to bank2, ship with has_gdi = 0, and every GDI-owned
        # damaged building wears NOD-RED scorch over its sand-coloured shell -- measured
        # on screen, with colour deltas up to 181/255 on up to 30% of a texture's texels.
        bake_damage_overlays(bank2, [], {}, {}, {}, verbose=False)
        # AND THE NUKE. Its fire texture is RGBA16, which carries its own colour and
        # cannot decode differently through a swapped TLUT, so this offers no variant --
        # but it keeps the "same walk, same keys" invariant this diff rests on literally
        # true rather than true-except-for-one-mesh. Skipping the equivalent line for the
        # damage overlays is what put Nod-red scorch on every GDI building.
        bake_nuke_effect(bank2, [], {}, {}, {}, {}, verbose=False)
        bake_ion_effect(bank2, [], {}, {}, {}, verbose=False)
        bake_rally_flag(bank2, [], {}, {}, {}, {}, um, verbose=False)
    finally:
        B.GLOBAL_TLUT = saved_tlut
    n_variant = 0
    for key, i2 in bank2.bykey.items():
        i1 = bank.bykey.get(key)
        if i1 is None:
            continue
        d2 = bank2.items[i2][4]
        if d2 != bank.items[i1][4]:
            assert len(d2) == len(bank.items[i1][4]), key
            gdi_data[i1] = d2
            n_variant += 1
    if verbose:
        print("house textures: %d of %d mesh textures differ between the GDI and "
              "Nod tables and carry a variant" % (n_variant, len(bank2.items)))

    # ---- sprites (byte-identical logic to the PK4 baker) -----------------
    sprites, sprindex = [], {}
    for setname in B.SPRITE_SETS:
        npal = 11 if setname == "DP" else 2
        for (anim, _nf, _nfac) in B.SPRITE_ANIMS:
            for pal in range(npal):
                s_ = B.bake_sprite(setname, anim, pal)
                if not s_:
                    if verbose and pal == 0:
                        print("sprite %s_%s: NOT IN ROM" % (setname, anim))
                    continue
                w, h, rgba, n, fh = s_
                rec = B.SOL.by_name()["%s_%s" % (setname, anim)]
                nm = "%s_%s#%d" % (setname, anim, pal)
                ti = bank.add_rgba(("spr", nm), w, h, rgba)
                sprindex[nm] = len(sprites)
                sprites.append((nm, ti, n, rec["facings"], w, fh, w, h))
    if verbose:
        print("sprites: %d strips (sets x anims x palettes)" % len(sprites))

    infantry = []
    for code in sorted(B.INFANTRY_SETS):
        st, pals = B.INFANTRY_SETS[code]
        idxs = []
        for house in range(B.N_HOUSE):
            for (a, _n, _f) in B.SPRITE_ANIMS:
                idxs.append(sprindex.get("%s_%s#%d" % (st, a, pals[house]), -1))
        infantry.append((code, st, idxs))

    # ---- heightmap (PK9) -------------------------------------------------
    # The scenario's own 65x65 corner heights out of the ROM (SCG01EA.IMG et
    # al); FLAT.IMG's zeros when the scenario has none, which is the engine's
    # own fallback. See the PK9 block in the module docstring for provenance.
    corners = bake_heights(scen, heights, cw, ch)
    if verbose:
        print("heights: %s -> min=%d max=%d (%dx%d corners, unit = 1/64 cell)"
              % (scen, min(corners), max(corners), cw, ch))

    # ---- CM tint layer (PKC) --------------------------------------------
    # The second per-corner map the console's terrain combiner SUBTRACTS from
    # the texel before it modulates by the light. See the PKC block in the
    # module docstring for the ROM trail. Raw RGBA5551; the renderer applies
    # the cartridge's 200/32768 scaling against its own per-corner light.
    if cmvals is None:
        cmsrc, cmvals = bake_cm(scen, cw, ch)
    else:
        # An authored tint layer, on the same corner grid as the heights.
        cmvals = list(cmvals)
        if len(cmvals) != ncorner:
            raise SystemExit("bake5: cm override is %d values; a %dx%d corner "
                             "grid is %d" % (len(cmvals), cw, ch, ncorner))
        cmsrc = "override"
    if verbose:
        nz = sum(1 for v in cmvals if (v & 0xFFFE))
        print("cm tint: %s -> %s, %d of %d corners carry a non-black tint "
              "(%dx%d RGBA5551, same corner grid as the heightmap)"
              % (scen, cmsrc, nz, ncorner, cw, ch))

    # ---- water (PK8) -----------------------------------------------------
    # Appended to the END of the bank on purpose: every existing texture index
    # in the pack keeps its number, so a bake diff stays readable. The two
    # tiles are direct-colour RGBA5551, so they never carry a GDI variant.
    water0, water1, seabed = bake_water(bank)
    vshadows = bake_vehicle_shadows(bank)
    if verbose:
        print("water: WATER1 -> bank %d, WATER2 -> bank %d, BOTTOM (seabed) -> "
              "bank %d (all 32x32 RGBA5551, from the cartridge's own archive)"
              % (water0, water1, seabed))

    # ---- write -----------------------------------------------------------
    # The sprite section grew the bank past the mesh textures the variant pass
    # covered; sprites carry their house colours in their own palettes, so they
    # never have a GDI variant. Pad the list out to match.
    gdi_data += [None] * (len(bank.items) - len(gdi_data))
    assert water0 >= 0 and water1 >= 0 and seabed >= 0, \
        "water bake produced no bank indices"
    w32, wname = B.w32, B.wname
    with open(outpath, "wb") as f:
        # PKG writes the grid ALWAYS, and there is no longer a 64x64 special case.
        # PKE existed to keep 64x64 packs byte-identical across the PKF change; PKG
        # rewrites every pack anyway (it carries a second terrain atlas), so the saving
        # has nothing left to protect and the split was a standing trap -- baking a big
        # grid as PKE mis-read it with 64x64 tail offsets and nothing detected it.
        # One header, one set of offsets, and load_pack's `pkf` test still holds because
        # 'G' > 'F'.
        f.write(b"CNC3DPKG")
        w32(f, 16)
        w32(f, gw)
        w32(f, gh)
        wname(f, scen, 16)
        wname(f, terr["theater"], 16)

        w32(f, len(bank.items))
        for i, (w, h, uw, uh, data) in enumerate(bank.items):
            w32(f, w); w32(f, h); w32(f, uw); w32(f, uh)
            f.write(data)
            if gdi_data[i] is not None:
                f.write(b"\x01")
                f.write(gdi_data[i])
            else:
                f.write(b"\x00")

        w32(f, len(meshes))
        for name, tris in meshes:
            wname(f, name, 16)
            w32(f, len(tris))
            for ti, mode, wrap, tri in tris:
                f.write(struct.pack("<i", ti))
                f.write(bytes((mode, wrap)))
                for (x, y, z, u, v, r, g, b, a) in tri:
                    f.write(struct.pack("<fffff", x, y, z, u, v))
                    f.write(bytes((r, g, b, a)))
            parts = meshparts[name]
            w32(f, len(parts))
            for (tri0, ntris, role, pivot) in parts:
                w32(f, tri0); w32(f, ntris); w32(f, role)
                f.write(struct.pack("<fff", pivot[0], pivot[1], pivot[2]))
            secs = meshsections[name]
            w32(f, len(secs))
            for s in secs:
                w32(f, s)

            # PKB: node animation, one block per mesh, directly after its sections.
            # meshanim[name] is None for a mesh with no tracks, which is most of them
            # and costs 12 bytes each.
            anim = meshanim.get(name)
            nparts = len(parts)
            if anim is None:
                w32(f, 0); w32(f, 0); w32(f, 0)
            else:
                nf = anim["frames"]
                assert len(anim["mat"]) == nf * nparts * 12, \
                    "%s: animation matrix is %d floats, expected %d frames x %d parts x 12" \
                    % (name, len(anim["mat"]), nf, nparts)
                assert len(anim["vis"]) == nf * nparts, \
                    "%s: animation visibility is %d bytes, expected %d" \
                    % (name, len(anim["vis"]), nf * nparts)
                w32(f, nf)
                w32(f, anim["ticks_per_frame"])
                w32(f, len(anim["clips"]))
                for c in anim["clips"]:
                    f.write(struct.pack("<ii", c["t0"], c["t1"]))
                    f.write(bytes((1 if c["loop"] else 0,)))
                f.write(struct.pack("<%df" % (nf * nparts * 12), *anim["mat"]))
                f.write(bytes(bytearray(anim["vis"])))

        types = [k for k in sorted(um) if k != "_meta"]
        w32(f, len(types) + len(debris_types) + len(flip_types))
        swapped = []
        for k in types:
            # The theater swap (see DESERT_TERRAIN_MESH): the NAME the renderer looks
            # up is always k, only the mesh behind it moves.
            src = terrain_source_code(k, theater_id)
            if src != k:
                assert src in um, ("%s: the desert flora swap wants entry %s and "
                                   "unit_models.json has no such entry" % (k, src))
                swapped.append((k, src, um[src]["mesh"]))
            wname(f, k, 8)
            f.write(struct.pack("<i", meshindex.get(um[src]["mesh"], -1)))
            f.write(bytes((B.CONF[um[src]["confidence"]],)))
        if verbose:
            if swapped:
                fam = {}
                for k, _s, m in swapped:
                    fam.setdefault(m, []).append(k)
                print("desert flora: theater %s (TD id %d), %d tree types onto %d "
                      "meshes: %s"
                      % (theater_name, theater_id, len(swapped), len(fam),
                         "; ".join("%s <- %s" % (m, ",".join(sorted(fam[m])))
                                   for m in sorted(fam))))
            else:
                print("desert flora: no swap, theater is %s (TD id %d)"
                      % (theater_name, theater_id))
        # The debris chunks ride the type table too, so the renderer resolves them the
        # same way it resolves the cursor models and the pack format is untouched.
        for code in sorted(debris_types):
            wname(f, code, 8)
            f.write(struct.pack("<i", meshindex[debris_types[code]]))
            f.write(bytes((B.CONF["high"],)))
        # And so do the per-frame flipbook cursors, CUR0AF0.. / CUR0BF0.. -- 7 characters,
        # which is why the type name field's 8 bytes are exactly enough.
        for code in sorted(flip_types):
            assert len(code) < 8, code
            wname(f, code, 8)
            f.write(struct.pack("<i", meshindex[flip_types[code]]))
            f.write(bytes((B.CONF["high"],)))

        w32(f, terrain_tex)
        # PKG: the DOS atlas's bank index, signed so -1 survives the round trip.
        f.write(struct.pack("<i", terrain_dos_tex))
        cells = terr["cells"]
        w32(f, len(cells))
        for c in cells:
            f.write(struct.pack("<hh", c["x"], c["y"]))
            f.write(struct.pack("<ffff", c["u0"], c["v0"], c["u1"], c["v1"]))
            f.write(bytes((1 if c["hasHoles"] else 0,)))

        w32(f, len(sprites))
        for nm, ti, n, nfac, fw, fh, w, h in sprites:
            wname(f, nm, 16)
            w32(f, ti); w32(f, n); w32(f, nfac)
            w32(f, fw); w32(f, fh); w32(f, w); w32(f, h)

        w32(f, len(infantry))
        for code, st, idxs in infantry:
            wname(f, code, 8)
            wname(f, st, 4)
            for i in idxs:
                f.write(struct.pack("<i", i))

        # THE TAIL, and its one rule: every block here is read back by an
        # EOF-RELATIVE fseek, so a new block goes at the FRONT of the tail and
        # never between two existing ones. Order on disk, first to last:
        # PKD (vehicle shadows) is the newest block and so goes FIRST.
        # The two water int32s stay the LAST eight bytes of the file for ever.
        #   PKD shadows | PKC cm tint (8450 B) | PK9 heights (4225 B) | PKA seabed | PK8 water
        w32(f, len(vshadows))
        for nm, bank_ix, prim, verts in vshadows:
            wname(f, nm, 8)
            f.write(struct.pack("<i", bank_ix))
            f.write(bytes(prim))
            for (x, y, z, u, v) in verts:
                f.write(struct.pack("<fffff", x, y, z, u, v))
        f.write(struct.pack("<%dH" % len(cmvals), *cmvals))
        f.write(bytes(bytearray(corners)))
        f.write(struct.pack("<i", seabed))
        f.write(struct.pack("<ii", water0, water1))

    json.dump(roles_doc, open(os.path.join(HERE, "parts_roles.json"), "w"), indent=1)
    check_pk8_tail(outpath, corners, cmvals, vshadows)
    check_prim_only()
    check_xlu()
    if verbose:
        print("wrote %s (%.1f MB)" % (outpath, os.path.getsize(outpath) / 1e6))


def check_pk8_tail(path, corners=None, cmvals=None, vshadows=None):
    """Read the file back and prove the tail is real.

    The whole point of a hard check here is that a PK8 pack whose trailing
    indices are -1 (or missing) is a FAILED bake, not a graceful fallback: the
    renderer would silently draw the old flat placeholder water and the bake
    would look like it had worked.

    The same reasoning now covers the two per-corner blocks. Both are read back
    by their OWN eof-relative offsets, exactly as the renderer reads them, and
    compared byte for byte against what the baker was handed. That is the check
    that would have caught the PKA regression: when the seabed int32 was
    inserted between the heights and the water tail, the heights stayed correct
    on disk and only the READER's offset went stale, so nothing here objected
    and the renderer quietly drew every corner four columns to the east."""
    # Block sizes come from what the baker was handed; the legacy 64x64 grid
    # (4225 corner bytes, 8450 tint bytes) is only the fallback when a block
    # was not passed in at all.
    hsize = len(corners) if corners is not None else 65 * 65
    csize = 2 * len(cmvals) if cmvals is not None else 2 * 65 * 65
    with open(path, "rb") as f:
        head = f.read(12)
        if corners is not None:
            f.seek(-(hsize + 12), os.SEEK_END)
            back = f.read(hsize)
            assert list(bytearray(back)) == list(corners), \
                "%s: the PK9 heights do not read back at -(%d+12) from EOF -- " \
                "a tail block moved and its reader was not updated" % (path, hsize)
        if cmvals is not None:
            f.seek(-(csize + hsize + 12), os.SEEK_END)
            back = struct.unpack("<%dH" % (csize // 2), f.read(csize))
            assert list(back) == list(cmvals), \
                "%s: the PKC cm tint does not read back at -(%d+%d+12) from " \
                "EOF -- a tail block moved and its reader was not updated" \
                % (path, csize, hsize)
            assert all(v <= 0xFFFF for v in back)
        f.seek(-12, os.SEEK_END)
        seabed = struct.unpack("<i", f.read(4))[0]
        tail = f.read(8)
    # One header now: PKG (version 16, u32 mapW/mapH after the version, and a signed
    # DOS-atlas bank index after the terrain one). The tail blocks this check reads are
    # EOF-relative, so the header length plays no part in finding them.
    ver = struct.unpack("<I", head[8:])[0]
    assert (head[:8], ver) == (b"CNC3DPKG", 16), head
    if vshadows is not None:
        # PKD sits at the FRONT of the tail, so its offset from EOF is the whole of
        # everything after it plus its own size -- read it back and compare byte for
        # byte against what the baker was handed. This is the check that would have
        # caught the PKA insertion bug: the heights reader was never moved and every
        # terrain corner came back four columns east for three pack formats.
        blob = b""
        for nm, bank_ix, prim, verts in vshadows:
            blob += nm.encode("ascii")[:8].ljust(8, b"\x00")
            blob += struct.pack("<i", bank_ix)
            blob += bytes(prim)
            for (x, y, z, u, v) in verts:
                blob += struct.pack("<fffff", x, y, z, u, v)
        with open(path, "rb") as f:
            f.seek(-(len(blob) + csize + hsize + 12), os.SEEK_END)
            back = f.read(len(blob))
        assert back == blob, \
            "%s: the PKD vehicle shadows do not read back at their EOF-relative " \
            "offset -- a tail block moved and its reader was not updated" % path
    a, b = struct.unpack("<ii", tail)
    assert a >= 0 and b >= 0 and a != b, \
        "%s: water tail is (%d, %d) -- a negative or duplicated water index means " \
        "the water did not bake. That is a failed bake, not a fallback." % (path, a, b)
    assert seabed >= 0 and seabed != a and seabed != b, \
        "%s: seabed index is %d -- BOTTOM.IMG did not bake. A sea with no floor is " \
        "a failed bake, not a fallback." % (path, seabed)


def _read_hgt(path):
    """An authored (W+1)x(W+1) corner heightmap: raw bytes, row major (4,225
    for the legacy 64x64 cell grid, 16,641 for 128x128).

    This is what the browser map editor writes as <SCEN>.HGT, and it is the same
    payload the cartridge keeps in <SCEN>.IMG after that file's 16-byte header. It
    exists because without it authored elevation cannot reach the game at all: the
    ROM path can only ever return what the cartridge shipped, which is why every
    converted skirmish map is flat in play.

    The length is only checked to be an exact square here; build() checks it
    against the scenario's own cell grid, where that grid is known.
    """
    with open(path, "rb") as fh:
        raw = fh.read()
    side = _math.isqrt(len(raw))
    if side * side != len(raw):
        raise SystemExit("bake5: %s is %d bytes; a corner map is (W+1)*(W+1) "
                         "raw bytes (4225 for a 64x64 map, 16641 for 128x128)"
                         % (path, len(raw)))
    return list(raw)


if __name__ == "__main__":
    outdir = HERE
    scens, heights = [], None
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--heights":
            i += 1
            if i >= len(args):
                raise SystemExit("bake5: --heights needs a path to a .HGT file")
            heights = _read_hgt(args[i])
        elif a.startswith("--"):
            raise SystemExit("bake5: unknown option %s (only --heights)" % a)
        else:
            scens.append(a)
        i += 1
    if heights is not None and len(scens) != 1:
        raise SystemExit("bake5: --heights applies to exactly one scenario, got %d"
                         % len(scens))
    for scen in scens or ["SCB01EA", "SCG01EA"]:
        build(scen, os.path.join(outdir, "game", "%s.pack" % scen), heights=heights)
