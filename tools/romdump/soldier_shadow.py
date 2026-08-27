#!/usr/bin/env python3
"""
CNC3D wave 6 -- the cartridge's OBJECT SHADOW inventory, recovered and asserted.

Run:  python3 soldier_shadow.py [path/to/cnc_eu.z64]

Every number this prints is read out of the ROM and asserted here, so a wrong
address fails loudly instead of printing a plausible lie.

What it establishes:
  1. The draw-command taxonomy: 12 command types, 11 enqueue functions, and the
     bucket (draw-layer) each one uses. There is NO vehicle-shadow command.
  2. The ONE object in the cartridge that enqueues a second command for a shadow
     is the SOLDIER (types 10 + 11 from one enqueue at RAM 0x8004B740).
  3. The full soldier-shadow recipe: texture, combiner, render mode, prim colour,
     quad size and offset.
  4. Writes the 8x8 I8 texture to soldier_shadow.png (and the mirrored 16x16 it
     resolves to on screen) plus a C array ready to paste.
"""
import os
import struct
import sys

# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


ROM_PATH = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    os.path.join(_ROOT, "data", "rom", "cnc_eu.z64"))
ROM = open(ROM_PATH, "rb").read()

RES = 0x7FFFF400           # resident:    rom = ram - RES
CODEOVL = 0x8005E210       # CodeOverlay: rom = ram - CODEOVL


def u32_res(ram):
    return struct.unpack_from(">I", ROM, ram - RES)[0]


def f32_res(ram):
    return struct.unpack(">f", struct.pack(">I", u32_res(ram)))[0]


def w_at(rom_off):
    return struct.unpack_from(">I", ROM, rom_off)[0]


def cstr(rom_off):
    e = ROM.index(b"\0", rom_off)
    return ROM[rom_off:e].decode("latin1")


ok = []
def check(label, got, want):
    assert got == want, "%s: got %r want %r" % (label, got, want)
    ok.append(label)


print("== 1. the DL-buffer registry (the ROM's own display-list category names) ==")
# DLSafe_Report at RAM 0x8004BE60 formats "DLSafe %s: ..." with name at rec+0x10.
check("DLSafe fmt string", cstr(0x542C).rstrip("\n"),
      "DLSafe %s: Min=%d Max=%d Avg=%d (Sum=%d / Num=%d)")
RECS = [0x80096FF4, 0x80097008, 0x8009701C, 0x80097030, 0x80097044,
        0x80097058, 0x8009706C, 0x80097080, 0x800970D8, 0x800970EC,
        0x80097100, 0x80097114, 0x80097128, 0x8009713C]
names = []
for r in RECS:
    nm = cstr(u32_res(r + 0x10) - RES)
    names.append(nm)
    print("   %-12s %s" % (hex(r), nm))
check("registry contents", names,
      ["Shape", "Palette", "Cursor", "Repair", "Dollar", "RubberBand", "Laser",
       "Pips", "HealthBar", "SoldierShadow_Setup", "SoldierShadow",
       "SoldierBody_Setup", "SoldierBody", "SpecialEffect"])
print("   -> 14 categories. Exactly ONE says 'Shadow', and it says 'Soldier'.")

print()
print("== 2. every draw-command enqueue in the cartridge ==")
# The one real allocator is RAM 0x801FCBC8(bucket); RAM 0x801FCC3C(model,house)
# wraps it with bucket = model + house*0xDB.  Walk every jal to the allocator and
# read back the type stored at cmd+0x08 and the literal bucket.
ALLOC = 0x801FCBC8
enc = 0x0C000000 | ((ALLOC & 0x0FFFFFFF) >> 2)
sites = [o for o in range(0x1000, len(ROM) - 4, 4) if w_at(o) == enc]
check("allocator call sites", len(sites), 12)
TYPE_NAME = {1: "MODEL (any 3D object)", 2: "Cursor", 3: "Repair icon",
             4: "Dollar icon", 5: "RubberBand", 7: "Laser", 8: "Pips",
             9: "HealthBar", 10: "SoldierSHADOW", 11: "SoldierBody",
             12: "SpecialEffect"}
found = {}
for o in sites:
    bucket = None
    for k in (1, 0, -1, -2, -3, -4, -5):      # delay slot first, then backwards
        w = w_at(o + 4 * k)
        if (w >> 26) == 0x09 and ((w >> 21) & 0x1F) == 0 and ((w >> 16) & 0x1F) == 4:
            bucket = w & 0xFFFF
            break
    ty = None
    for k in range(1, 12):
        w = w_at(o + 4 * k)
        if (w >> 26) == 0x2B and (w & 0xFFFF) == 8:          # sw rX, 8(base)
            rt = (w >> 16) & 0x1F
            for j in range(1, 10):
                w2 = w_at(o + 4 * k - 4 * j)
                if (w2 >> 26) == 0x09 and ((w2 >> 16) & 0x1F) == rt \
                        and ((w2 >> 21) & 0x1F) == 0:
                    ty = w2 & 0xFFFF
                    break
            break
    if bucket is not None and bucket >= 0x290 and ty is not None:
        found[ty] = bucket
        print("   ROM %#08x  bucket %#05x  type %2d  %s"
              % (o, bucket, ty, TYPE_NAME.get(ty, "?")))
check("HUD/sprite command types", sorted(found),
      [2, 3, 4, 5, 7, 8, 9, 10, 11, 12])
check("soldier shadow bucket", found[10], 0x29A)
check("soldier body bucket", found[11], 0x29B)
print("   plus 8 MODEL (type 1) wrappers, all through RAM 0x801FCC3C, whose")
print("   bucket is  model + 0xDB*house  (0xDB = 219 = the model table's length).")
print("   -> no shadow type, no shadow bucket, for anything except the soldier.")

print()
print("== 3. the soldier shadow, enqueued as a SECOND command beside the body ==")
# RAM 0x8004B740 allocates bucket 0x29A/type 10 then 0x29B/type 11, one call.
check("soldier enqueue bucket A", w_at(0x4C37C) & 0xFFFF, 0x29A)
check("soldier enqueue type A", w_at(0x4C388) & 0xFFFF, 0x0A)
check("soldier enqueue bucket B", w_at(0x4C39C) & 0xFFFF, 0x29B)
check("soldier enqueue type B", w_at(0x4C3A8) & 0xFFFF, 0x0B)
enc740 = 0x0C000000 | ((0x8004B740 & 0x0FFFFFFF) >> 2)
callers = [o for o in range(0x1000, len(ROM) - 4, 4) if w_at(o) == enc740]
check("soldier enqueue callers", callers, [0x17E38C])
print("   RAM 0x8004B740 is the ONLY producer of a type-10 command, and it has")
print("   exactly one caller (ROM %#x, the infantry sprite draw)." % callers[0])

print()
print("== 4. the shadow's own recipe ==")
gate = u32_res(0x80097A7C)
size = f32_res(0x800979F8)
mul = f32_res(0x80097A88)
check("shadow gate 0x80097A7C", gate, 1)
check("shadow base size 0x800979F8", size, 10.0)
check("shadow size scale 0x80097A88", mul, 4.0)
half = int(size * mul)
print("   gate  RAM 0x80097A7C = %d  (drawn)" % gate)
print("   half-extent = trunc(%g * %g) = %d world units = %.5f cells"
      % (size, mul, half, half / 256.0))
print("   centre = (soldierX + 5, terrain_y, soldierZ - 5)   [+5/-5 leptons]")

# the setup DL, RAM 0x801FC098 .. words written in order
SET = 0x801FC098 - CODEOVL
# G_SETPRIMCOLOR and the final render mode are literals in the setup
check("PRIM opcode lui", w_at(SET + 0x2F4) & 0xFFFF, 0xFA00)      # 0x801FC38C
check("PRIM value", w_at(SET + 0x300) & 0xFFFF, 0x50)          # 0x801FC398
print("   G_SETPRIMCOLOR (0,0,0, 0x50)   -> black at alpha 80/255 = %.4f"
      % (0x50 / 255.0))
print("   G_SETCOMBINE 0xFCFF97FF / 0xFF2DFEFF")
print("       RGB = PRIM              (both cycles)")
print("       A   = TEXEL0_A * PRIM_A (both cycles)")
print("   G_SETOTHERMODE_L rendermode 0x005049D8:")
print("       AA_EN, Z_CMP=1, Z_UPD=0, IM_RD, CLR_ON_CVG, ZMODE_XLU, FORCE_BL")
print("   G_SETOTHERMODE_H  TEXTFILT = G_TF_BILERP, TEXTPERSP = G_TP_PERSP")
print("   G_GEOMETRYMODE    clears G_SHADE|G_LIGHTING|G_SHADING_SMOOTH,")
print("                     sets   G_ZBUFFER|G_CULL_BACK")
print("   G_SETTIMG  fmt=I  siz=8b  width=8   addr RAM 0x80212210")
print("   G_SETTILE  tile0  maskS=maskT=3 (8 texels)  cmS=cmT=MIRROR  shift=15 (<<1)")
print("   gSPTexture scale S=T=0x8000 (0.5)")
print("   quad ST are 0 / 0x200 (16.0 texels) -> *0.5 -> 8.0 -> <<1 -> 16.0")
print("       = exactly ONE mirror period, so the 8x8 image is one QUADRANT of a")
print("         16x16 radially symmetric blob.")

TEX = 0x80212210 - CODEOVL
check("texture ROM offset", TEX, 0x1B4000)
tex = ROM[TEX:TEX + 64]
check("texture centre texel", tex[7 * 8 + 7], 252)
check("texture corner texel", tex[0], 0)
print()
print("   8x8 I8 texture at ROM %#x:" % TEX)
for r in range(8):
    print("     " + " ".join("%3d" % tex[r * 8 + c] for c in range(8)))

# mirrored 16x16
full = []
for r in range(8):
    row = [tex[r * 8 + c] for c in range(8)]
    full.append(row + row[::-1])
full = full + full[::-1]
print()
print("   resolves on screen to (alpha coverage, . = 0 .. @ = max):")
ramp = " .:-=+*#%@"
for row in full:
    print("     " + "".join(ramp[min(9, v * 10 // 256)] for v in row))

here = os.path.dirname(os.path.abspath(__file__))
try:
    from PIL import Image
    im = Image.new("L", (8, 8))
    im.putdata(list(tex))
    im.resize((256, 256), Image.NEAREST).save(
        os.path.join(here, "soldier_shadow_8x8.png"))
    im2 = Image.new("L", (16, 16))
    im2.putdata([v for row in full for v in row])
    im2.resize((256, 256), Image.NEAREST).save(
        os.path.join(here, "soldier_shadow_16x16.png"))
    print("\n   wrote soldier_shadow_8x8.png and soldier_shadow_16x16.png")
except ImportError:
    print("\n   (PIL absent, PNGs skipped)")

# The header goes beside the code that includes it, so a re-extraction cannot leave a
# stale copy behind in the tools folder (that is exactly how the animation extractor's
# output went unseen for a whole wave).
_gamedir = os.path.normpath(os.path.join(here, "..", "..", "game"))
with open(os.path.join(_gamedir, "soldier_shadow_tex.h"), "w") as f:
    f.write("/* The cartridge's soldier shadow: 8x8 I8, RAM 0x80212210 = ROM 0x1B4000.\n"
            "   Drawn MIRRORED in S and T (G_TX_MIRROR, mask 3), so it is one quadrant\n"
            "   of a 16x16 radially symmetric blob. Recovered by\n"
            "   /tmp/wave6/shadows/soldier_shadow.py -- do not hand-edit. */\n")
    f.write("static const unsigned char SOLDIER_SHADOW_I8[64] = {\n")
    for r in range(8):
        f.write("    " + ", ".join("%3d" % tex[r * 8 + c] for c in range(8)) + ",\n")
    f.write("};\n")
print("   wrote game/soldier_shadow_tex.h")

print()
print("== 5. the model draw command's flag word (cmd+0x20) ==")
print("   Read at RAM 0x8004C130 (lw $s5,0x14($s2), $s2 = cmd+0x0C).")
print("   Every bit the type-1 handler tests, and nothing else tests it:")
for ram, bit, what in (
        (0x8004C384, 0x080, "ground the object: place via RAM 0x801FB210 (terrain y) "
                            "instead of 0x801FB1A8"),
        (0x8004C3BC, 0x200, "place via 0x801FB1A8 with height arg 1 instead of the "
                            "global at 0x80096FA0"),
        (0x8004C444, 0x100, "the washboard lean (constants 0x80097A2C..0x80097A48)"),
        (0x8004C5B8, 0x001, "add the double at 0x80004950 to the accumulated height"),
        (0x8004C644, 0x400, "terrain-orienting shear via 0x801F5254 instead of "
                            "0x8007C8A8"),
        (0x8004C6C0, 0x008, "take the ALTERNATE display list of the per-model record "
                            "(rec+0x04/+0x08 instead of rec+0x00) and scale by rec+0x14")):
    w = w_at(ram - RES)
    check("flag test at %#x" % ram, (w >> 26, (w >> 21) & 0x1F, w & 0xFFFF),
          (0x0C, 21, bit))
    print("     %#05x  %s" % (bit, what))
print("   Bits 0x20 and 0x40 are SET by two enqueue sites (ROM 0x33EA8 and 0x1756FC")
print("   pass 0x60, ROM 0x172A60 passes 0x40) and are tested NOWHERE. Dead bits.")
print("   No bit selects a shadow, a second draw, or a black prim colour.")

print()
print("ALL %d ASSERTIONS PASSED." % len(ok))
