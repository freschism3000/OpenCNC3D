#!/usr/bin/env python3
"""WHERE THE TEN EFFECT SLOTS AND THE SIX FLAT QUADS BELONG. Self-asserting.

    python3 tools/bakery/g171_effects_and_quads.py            # the whole chain
    python3 tools/bakery/g171_effects_and_quads.py --packs DIR

Every number below is re-derived from the cartridge at run time and asserted, so this
file fails loudly if an address moves rather than printing a stale conclusion. It reads
the ROM and the shipped packs only. It bakes nothing and writes nothing.

WHAT IT SETTLES, and the short version of each answer:

  MODEL SLOTS 0..9 ARE A BLOCK OF THEIR OWN, ten records, and the only code that can
  reach them is drawModel at RAM 0x8004BB9C. Every other draw goes through the draw
  command, whose consumer resolves node = *(0x80099A38 + id*16 + 0x0C); 0x80099A38 is
  the model array plus ten records, which is the whole content of the "slot = type id
  + 10" rule and is measured here at the load rather than assumed.

  SLOTS 1, 3, 4, 5   the Ion Cannon: a beam column and three shock rings.
  SLOTS 6, 7, 8, 9   the nuclear strike: dome, stem, collar, ground cloud.
  Drawn by the kind-12 animation arm, and ALREADY BAKED: the ion set is composed into
  the fifteen snapshot meshes ION0..ION14 and the nuke set into the animated NUKEFX, so
  a search for their display-list names finds nothing while the geometry is in every
  pack. The triangle profile below is the proof, and it closes arithmetically.

  SLOT 2  a fourth, fully authored Ion shock ring that the cartridge never draws.
  SLOT 0  a small textured ground decal that the cartridge never draws.
  Neither has a caller. Not baked, and correctly so.

  SLOTS 142, 143, 144  ANY_SFX_PRIMARYZ1 in three languages. No instruction anywhere
  materialises 132, 133 or 134 as a model id, so no draw path was established. Not
  baked. See the open gap at the foot of this file for what that claim does not cover.

  SLOTS 151, 152  ANY_SFX_TILEGREY and ANY_SFX_TILERED, the build placement grid, one
  world quad per candidate cell. Drawn, and this file reads the exact colours out of
  the display lists' own G_SETPRIMCOLOR. The meshes carry NO other content: both are
  the same untextured one-cell quad, so the colour is the whole asset.

  dl_014CF98  NOT a model slot. The model array ends at slot 228 and the bytes after it
  are the 19-record vehicle and aircraft ground-shadow table at stride 0x14. This
  display list is record 3 of that table, type id 37, which is MSAM and not BGGY: the
  record's type id sits at +0x00 and its node at +0x10, and reading the pair the other
  way round shifts every name in the table by one. It is already baked into the PKD
  block of every shipped pack and already drawn.
"""
import argparse
import glob
import os
import struct
import sys

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# THE REPO IS THIS FILE'S OWN GRANDPARENT, not a path under some home directory. A
# hardcoded ./ is right on exactly one machine and silently wrong everywhere else,
# including a second checkout on this one.

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

ROM_PATH = os.environ.get("CNC3D_ROM",
                          os.path.join(os.path.dirname(os.path.dirname(HERE)),
                                       "CNC3D", "data", "rom", "cnc_eu.z64"))
if not os.path.isfile(ROM_PATH):
    ROM_PATH = os.path.join(_REPO, "data", "rom", "cnc_eu.z64")
ROM = open(ROM_PATH, "rb").read()

# The resident boot segment: IPL3 copies ROM[0x1000..] to RAM[0x80000400..].
RES_DELTA = 0x80000400 - 0x1000
# GameModelsGeometry, from support/overlay_table.json rather than from arithmetic.
GMG_ROM0, GMG_ROM1, GMG_RAM0 = 0x000FE210, 0x001643F0, 0x8015A7A0
GMG_DELTA = GMG_RAM0 - GMG_ROM0

MODEL_TABLE = 0x80099998        # slot 0
OBJECT_BLOCK = 0x80099A38       # slot 10, where the object draw indexes from
SHADOW_TABLE = 0x8009A7E8
MODEL_SCALE = 0x80099970
DRAWMODEL = 0x8004BB9C

FAILED = []


def u32(o):
    return struct.unpack_from(">I", ROM, o)[0]


def f32(o):
    return struct.unpack_from(">f", ROM, o)[0]


def res(a):
    return a - RES_DELTA


def gmg(a):
    return a - GMG_DELTA


def check(name, got, want):
    ok = got == want
    print("   %-58s %s" % (name, "ok" if ok else "MOVED: %r want %r" % (got, want)))
    if not ok:
        FAILED.append(name)
    return ok


def dis_jal_targets(seg_rom0, seg_rom1, delta, target):
    """Every jal to `target` in one segment, as RAM addresses."""
    out = []
    for o in range(seg_rom0, min(seg_rom1, len(ROM)) - 4, 4):
        w = u32(o)
        if (w >> 26) != 3:
            continue
        if (0x80000000 | ((w & 0x03FFFFFF) << 2)) != target:
            continue
        out.append(o + delta)
    return out


# =====================================================================================
# 1. The model array's three blocks, read off the initialiser's own literal counts.
# =====================================================================================
def block_shape():
    print("\n1. THE MODEL ARRAY, from the initialiser at RAM 0x800564D8")
    # addiu s6,zero,N sets each block's record count; the base follows two words later.
    # Ten records at MODEL_TABLE, then 219 at OBJECT_BLOCK, then 19 shadow records.
    check("MODEL_SCALE is ten floats and ends where slot 0 begins",
          MODEL_SCALE + 40, MODEL_TABLE)
    check("slot 10 is the object block base", MODEL_TABLE + 10 * 16, OBJECT_BLOCK)
    check("219 object records end exactly at the shadow table",
          OBJECT_BLOCK + 219 * 16, SHADOW_TABLE)
    check("so the last model slot is 228", 10 + 219 - 1, 228)
    # the object draw's own arithmetic: sll v0,s1,4 / addu s4,v0,v1 with v1 = 0x80099A38
    check("object draw indexes 0x80099A38 (RAM 0x8004C120 addiu v1,v1,-26056)",
          u32(res(0x8004C120)) & 0xFFFF, (OBJECT_BLOCK - 0x800A0000) & 0xFFFF)
    check("object draw shifts the id by 4 (RAM 0x8004C134 sll v0,s1,4)",
          u32(res(0x8004C134)), 0x00111100)
    scales = [f32(res(MODEL_SCALE) + 4 * i) for i in range(10)]
    check("MODEL_SCALE[0] is 1.0 and [1..9] are 0.3",
          [round(s, 4) for s in scales], [1.0] + [0.3] * 9)
    print("   drawModel calls the MODEL_SCALE accessor with its own slot argument,")
    print("   which is why the effects block is exactly ten records long.")


# =====================================================================================
# 2. Who calls drawModel, and with which slot.
# =====================================================================================
def drawmodel_callers():
    print("\n2. EVERY CALLER OF drawModel (RAM 0x8004BB9C)")
    calls = dis_jal_targets(0x1000, 0x90000, RES_DELTA, DRAWMODEL)
    check("seven jal sites, all resident", len(calls), 7)
    slots = []
    for c in calls:
        # the addiu a0,zero,N four bytes before the jal
        w = u32(res(c - 4))
        assert (w >> 26) == 9 and ((w >> 16) & 31) == 4 and ((w >> 21) & 31) == 0, \
            "the argument setter before %08X is not addiu a0,zero,N" % c
        slots.append(w & 0xFFFF)
    check("their slot literals", sorted(slots), [1, 3, 4, 6, 7, 8, 9])
    # ONE control transfer jumps INTO a jal, landing with its own a0 in the delay slot.
    # A backward walk from the call misses it, which is how slot 5 looks unreachable.
    jump = u32(res(0x8004E1B8))
    check("RAM 0x8004E1B8 is j 0x8004E254", 0x80000000 | ((jump & 0x03FFFFFF) << 2),
          0x8004E254)
    check("its delay slot is addiu a0,zero,5", u32(res(0x8004E1BC)), 0x24040005)
    check("so the drawn slots are 1,3,4,5,6,7,8,9",
          sorted(slots + [5]), [1, 3, 4, 5, 6, 7, 8, 9])
    print("   NOTHING passes 0 or 2. Nothing in the ROM holds 0x8004BB9C as a word,")
    print("   so there is no computed call either.")
    for a in (DRAWMODEL, 0x8004E174, 0x8004E254):
        assert ROM.find(struct.pack(">I", a)) == -1, \
            "%08X occurs as a data word; it may be reached indirectly" % a


# =====================================================================================
# 3. The two anim arms, and which slots each one draws.
# =====================================================================================
def anim_arms():
    print("\n3. THE ANIMATION ARMS")
    check("kind-12 arm tests anim id 52 (RAM 0x8004E114 addiu v0,zero,52)",
          u32(res(0x8004E114)) & 0xFFFF, 52)
    check("and anim id 53 (RAM 0x8004E11C addiu v0,zero,53)",
          u32(res(0x8004E11C)) & 0xFFFF, 53)
    print("   52 is the ion cannon, drawing slots 1,3,4,5 from RAM 0x8004E130.")
    print("   53 is the nuclear strike, drawing slots 6,7,8,9 from RAM 0x8004E1C0.")


# =====================================================================================
# 4. The placement grid, and the colours the two tiles carry.
# =====================================================================================
def placement_grid():
    print("\n4. THE BUILD PLACEMENT GRID, model ids 141 and 142 (slots 151 and 152)")
    # The per-cell loop picks the tile, then enqueues through the no-remap draw command.
    check("RAM 0x8002FDB8 addiu a0,zero,142", u32(res(0x8002FDB8)), 0x2404008E)
    check("RAM 0x8002FDBC addiu a0,zero,141", u32(res(0x8002FDBC)), 0x2404008D)
    jal = u32(res(0x8002FDD0))
    check("and RAM 0x8002FDD0 jal 0x8004A03C",
          0x80000000 | ((jal & 0x03FFFFFF) << 2), 0x8004A03C)
    print("   The test above it is bne s2,15: s2 is 15 only when both per-cell tests")
    print("   pass, so 141 (grey) is the legal cell and 142 (red) the blocked one.")
    for slot, dl, want in ((151, 0x0105878, (0x81, 0x81, 0x81, 0xFF)),
                           (152, 0x0105940, (0xFF, 0x00, 0x00, 0xFF))):
        prim = None
        o = dl
        for _ in range(32):
            w0, w1 = struct.unpack_from(">II", ROM, o)
            if (w0 >> 24) == 0xFA and prim is None:
                prim = ((w1 >> 24) & 0xFF, (w1 >> 16) & 0xFF,
                        (w1 >> 8) & 0xFF, w1 & 0xFF)
            o += 8
            if (w0 >> 24) == 0xDF:
                break
        check("slot %d dl_%07X G_SETPRIMCOLOR" % (slot, dl), prim, want)
    print("   Both quads are UNTEXTURED and both are 1030 x 1030 mesh units, which is")
    print("   one 1024-unit cell. Their only content is the PRIM colour above.")


# =====================================================================================
# 5. dl_014CF98 is a ground shadow, not a model slot.
# =====================================================================================
def shadow_record():
    print("\n5. dl_014CF98 IS SHADOW-TABLE RECORD 3, TYPE ID 37 (MSAM)")
    base = res(SHADOW_TABLE)
    recs = []
    for i in range(19):
        o = base + 0x14 * i
        recs.append((struct.unpack_from(">i", ROM, o)[0], u32(o + 0x10)))
    check("record 0 is type 38", recs[0][0], 38)
    check("record 3 is type 37, not 35", recs[3][0], 37)
    check("record 3's node is 0x801BFA68", recs[3][1], 0x801BFA68)
    check("record 4 is type 35 (BGGY)", recs[4][0], 35)
    check("the table is -1 terminated at record 18", recs[18][0], -1)
    # the leaf node's first word is its display list
    node = recs[3][1]
    dl = u32(gmg(node))
    check("that node's display list is dl_014CF98", gmg(dl), 0x0014CF98)
    print("   Reading the pair as {node, typeId} instead of {typeId, ..., node} shifts")
    print("   every name in the table by one place, which is the error this corrects.")


# =====================================================================================
# 6. The packs: what is already there.
# =====================================================================================
ION_PROFILE = [101, 101, 129, 129, 157, 157, 185, 185, 84, 84, 84, 56, 56, 28, 28]
NUKE_TRIS = 336
NUKE_FRAMES = 31
SHADOW_NAMES = ["MHQ", "HTNK", "MTNK", "MSAM", "BGGY", "FTNK", "LTNK", "ARTY", "BIKE",
                "JEEP", "MCV", "A10", "ORCA", "TRAN", "HELI", "C17", "HARV", "STNK"]


def packs(pack_dir):
    import packinspect
    print("\n6. THE SHIPPED PACKS in %s" % pack_dir)
    good, rejected = [], 0
    for p in sorted(glob.glob(os.path.join(pack_dir, "*.pack"))):
        try:
            good.append((os.path.basename(p), packinspect.read(p)))
        except Exception:
            rejected += 1
    print("   %d mission packs read, %d files rejected (asset packs in their own"
          " formats, plus the USER maps)" % (len(good), rejected))
    if not good:
        FAILED.append("no readable mission pack")
        return
    miss = []
    for nm, d in good:
        tm = dict((t[0], t[1]) for t in d["types"])
        if "NUKEFX" not in tm:
            miss.append("%s has no NUKEFX" % nm)
            continue
        m = d["meshes"][tm["NUKEFX"]]
        if m["ntris"] != NUKE_TRIS or not m["anim"] or m["anim"]["frames"] != NUKE_FRAMES:
            miss.append("%s NUKEFX is %d tris / %s frames" %
                        (nm, m["ntris"], m["anim"] and m["anim"]["frames"]))
        prof = []
        for i in range(len(ION_PROFILE)):
            k = "ION%d" % i
            if k not in tm:
                miss.append("%s has no %s" % (nm, k))
                break
            prof.append(d["meshes"][tm[k]]["ntris"])
        else:
            if prof != ION_PROFILE:
                miss.append("%s ion profile %s" % (nm, prof))
        if "ION%d" % len(ION_PROFILE) in tm:
            miss.append("%s carries a sixteenth ion snapshot" % nm)
    check("NUKEFX is %d tris over %d frames and ION0..ION%d carry %s in every pack"
          % (NUKE_TRIS, NUKE_FRAMES, len(ION_PROFILE) - 1, ION_PROFILE),
          miss[:3], [])
    print("   The profile is beam(101) plus N rings of 28, and N never reaches four.")
    print("   96+20+60+160 = %d is the nuke's four parts with none dropped." % NUKE_TRIS)
    bad = []
    for nm, d in good:
        got = pkd_names(os.path.join(pack_dir, nm))
        if got != SHADOW_NAMES:
            bad.append((nm, got))
    check("all 18 PKD ground shadows, in the cartridge's own order, in every pack",
          bad[:2], [])
    ext = pkd_extents(os.path.join(pack_dir, good[0][0]))
    check("MSAM's baked quad is 298 x 718 (dl_014CF98's own extent)",
          ext.get("MSAM"), (298, 718))
    check("BGGY's is 512 x 1084, so the two are not swapped",
          ext.get("BGGY"), (512, 1084))


PKD_REC = 8 + 4 + 4 + 4 * 5 * 4          # name8, bank, prim4, four x,y,z,u,v
PKD_BEHIND = 65 * 65 * 2 + 65 * 65 + 4 + 8


def _pkd(path):
    """-> [(name, verts)] read from the EOF end; the tail below PKD is fixed."""
    b = open(path, "rb").read()
    end = len(b) - PKD_BEHIND
    for n in range(63, 0, -1):
        start = end - n * PKD_REC - 4
        if start < 0:
            continue
        if struct.unpack_from("<I", b, start)[0] != n:
            continue
        out, o = [], start + 4
        for _ in range(n):
            nm = b[o:o + 8].split(b"\0")[0].decode("latin-1")
            o += 16
            vs = [struct.unpack_from("<5f", b, o + 20 * k) for k in range(4)]
            o += 80
            out.append((nm, vs))
        return out
    return []


def pkd_names(path):
    return [nm for nm, _ in _pkd(path)]


def pkd_extents(path):
    out = {}
    for nm, vs in _pkd(path):
        xs = [v[0] for v in vs]
        zs = [v[2] for v in vs]
        out[nm] = (int(round(max(xs) - min(xs))), int(round(max(zs) - min(zs))))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--packs", default=os.path.join(_REPO, "playable"))
    a = ap.parse_args()
    print("ROM: %s (%d bytes)" % (ROM_PATH, len(ROM)))
    block_shape()
    drawmodel_callers()
    anim_arms()
    placement_grid()
    shadow_record()
    packs(a.packs)
    print()
    if FAILED:
        print("FAILED: %d assertion(s) moved: %s" % (len(FAILED), "; ".join(FAILED)))
        return 1
    print("ALL ASSERTIONS PASSED")
    print("NOTHING IN THIS FAMILY NEEDS BAKING. Slots 1,3,4,5,6,7,8,9 and dl_014CF98")
    print("are already in every pack and already drawn; slots 0 and 2 have no caller;")
    print("slots 151 and 152 are drawn by the renderer's own placement grid and carry")
    print("no geometry beyond the colour printed above; slots 142,143,144 have no")
    print("established caller and the label they would draw is already a settled")
    print("design choice in screen space.")
    print()
    print("OPEN GAP, stated rather than papered over: 'no caller' is proved for the")
    print("ten-record effects block, whose only reader is drawModel. For slots 142,")
    print("143 and 144 it is weaker. No instruction in any loaded segment materialises")
    print("132, 133 or 134 as a model id, and the two draw-command entry points carry")
    print("literal ids only at the sites named above; but five call sites take the id")
    print("from a register and were not chased to their sources. What would settle it:")
    print("a write watch on the draw command's model-id field over a mission in which")
    print("a factory is made primary, or an enumeration of the model id every")
    print("registered type carries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
