#!/usr/bin/env python3
"""
bake_efx.py -- extract the N64 cartridge's in-game combat-effect textures and bake
them into efx.pack for the renderer's effects module (effects_mod.h).

WHERE THIS ART COMES FROM (proven by disassembly, round r030)
    The N64 game draws combat effects with a particle engine (../src/n64particle.c).
    Ten particle systems live at RAM 0x800EC1B0 (gGameParticleSystems, names in the
    registry at ROM 0x0A2BD4: Intensity, FireBall, VehicleDamage, StructureDamage,
    Shock, Burn_Fire, Burn_Smoke, Ion_Spark, Debris, FlameThrower). Their init
    function (RAM 0x80056944, ROM 0x57544) passes each system a STATIC TEXTURE in the
    resident segment plus format / width / height / frame-count arguments; the frames
    sit back to back, and every block boundary lands exactly on the next system's
    pointer, which pins the formats:
        I8 8-bit intensity, RGBA16 5551, RGBA32 8888   (all big-endian, row-major)
    The per-frame billboard draw is at RAM 0x8004F6FC (G_SETTIMG built from the
    system's image pointer). None of this art exists in the cartridge's file system:
    it is reachable only through these code-derived addresses.

    The engine->effect mapping is ALSO the cartridge's own: n64specialeffect.c keeps
    a 66-entry anim table (CodeOverlay ROM 0x1B6444, {name, spec, count}) whose spec
    blocks list which particle SOURCE each DOS anim spawns (SOURCE table at ROM
    0x1B41C0, 0x44-byte entries -> pcltmp templates at RAM 0x800A202C+). The recipes
    in effects_mod.h follow that table; see EFX_ART there.

Output: efx.pack, magic CNC3DEFX v1.
    u8 magic[8]; u32 ver; u32 nseq;
    per sequence: char name[8]; u32 frames, w, h, uw, uh;  frames * w*h*4 RGBA bytes.
    w/h are padded to powers of two for GL 1.1; uw/uh is the used sub-rect.

Usage: python3 bake_efx.py <cnc_eu.z64> [out.pack]
"""
import struct
import sys
import os

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(os.path.join(_HERE, ".."))

# The cartridge is not in the repo. Pass it as argv[1], or point CNC3D_ROM at it.
ROM_CANDIDATES = [
    p for p in [os.environ.get("CNC3D_ROM"), os.path.join(_ROOT, "data", "rom", "cnc_eu.z64")] if p
]

# (name, RAM address, format, width, height, frames)  -- from the init disassembly.
# RAM -> ROM for the resident segment: rom = ram - 0x80000400 + 0x1000.
SEQUENCES = [
    ("GLOW",     0x8009B3A0, "i8",     32, 32, 1),   # System_Intensity: soft round dot
    ("FIREBALL", 0x8009B7A0, "rgba16", 16, 8, 12),   # System_FireBall: gold burst fade
    ("SMOKE",    0x8009C3A0, "rgba16", 16, 16, 6),   # System_Burn_Smoke: grey puff
    ("SHOCK",    0x8009CFA0, "rgba16", 64, 64, 1),   # System_Shock: blast ring
    ("FLAME",    0x8009EFA0, "rgba16", 12, 16, 8),   # System_Burn_Fire: licking fire
    ("IONSPARK", 0x8009FBA0, "rgba32", 16, 16, 1),   # System_Ion_Spark: blue glow
    ("DEBRIS",   0x8009FFA0, "rgba32", 8, 16, 8),    # System_Debris: ember -> soot
    ("FLTHROW",  0x800A0FA0, "rgba32", 8, 16, 8),    # System_FlameThrower: blue->red
]


def r2o(ram):
    return ram - 0x80000400 + 0x1000


def pot(n):
    p = 1
    while p < n:
        p *= 2
    return p


def decode_frame(rom, off, fmt, w, h):
    """One frame -> row-major RGBA8888 bytes."""
    out = bytearray(w * h * 4)
    for i in range(w * h):
        if fmt == "rgba16":
            px = struct.unpack_from(">H", rom, off + i * 2)[0]
            r = (px >> 11) & 31
            g = (px >> 6) & 31
            b = (px >> 1) & 31
            a = 255 * (px & 1)
            out[i * 4:i * 4 + 4] = bytes((r * 255 // 31, g * 255 // 31, b * 255 // 31, a))
        elif fmt == "rgba32":
            out[i * 4:i * 4 + 4] = rom[off + i * 4:off + i * 4 + 4]
        else:  # i8: white shaped by intensity (the N64 tints it with prim colour)
            v = rom[off + i]
            out[i * 4:i * 4 + 4] = bytes((255, 255, 255, v))
    return bytes(out)


def frame_bytes(fmt):
    return {"rgba16": 2, "rgba32": 4, "i8": 1}[fmt]


def main():
    rompath = None
    for c in ([sys.argv[1]] if len(sys.argv) > 1 else []) + ROM_CANDIDATES:
        if os.path.exists(c):
            rompath = c
            break
    if not rompath:
        sys.exit("bake_efx: give me the cnc_eu.z64 path")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "efx.pack")
    rom = open(rompath, "rb").read()

    f = open(out, "wb")
    f.write(b"CNC3DEFX")
    f.write(struct.pack("<II", 1, len(SEQUENCES)))
    for name, ram, fmt, w, h, frames in SEQUENCES:
        pw, ph = pot(w), pot(h)
        f.write(struct.pack("8s", name.encode()))
        f.write(struct.pack("<IIIII", frames, pw, ph, w, h))
        base = r2o(ram)
        step = w * h * frame_bytes(fmt)
        for fr in range(frames):
            rgba = decode_frame(rom, base + fr * step, fmt, w, h)
            padded = bytearray(pw * ph * 4)
            for y in range(h):
                padded[y * pw * 4:y * pw * 4 + w * 4] = rgba[y * w * 4:(y + 1) * w * 4]
            f.write(bytes(padded))
        print("baked %-8s %dx%d x%d (%s, rom 0x%06X)" % (name, w, h, frames, fmt, base))
    f.close()
    print("wrote %s (%d bytes)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
